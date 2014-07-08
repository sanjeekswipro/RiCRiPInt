/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!src:swstart.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Entry points for ScriptWorks Core RIP
 */

#include "core.h"
#include "coreinit.h"
#include "contexti.h"
#include "swstart.h"
#include "swtrace.h"
#include "swcopyf.h"
#include "objecth.h"      /* objects_* */
#include "fonth.h"        /* fonts_* */
#include "fileio.h"       /* fileio_* */
#include "v20start.h"     /* postscript_*, stop_interpreter */
#include "mm.h"           /* mem_* */
#include "renderh.h"      /* render_* */
#include "swpdf.h"        /* pdf_C_globals */
#include "swpdfin.h"      /* pdfin_* */
#include "swpdfout.h"     /* pdfout_* */
#include "xmlops.h"       /* xml_* */
#include "pcl5ops.h"      /* pcl5_* */
#include "pclxlops.h"     /* pclxl_* */
#include "xps.h"          /* xps_* */
#include "zipdev.h"       /* zipdev_* */
#include "writeonly_zipdev.h" /* writeonly_zipdev_* */
#include "threadapi.h"
#include "imagesinit.h"   /* images_C_globals et. al. */
#include "cryptinit.h"    /* crypt_C_globals et. al. */
#include "imagecontext.h" /* imagecontext_* */
#include "cvcolcvt.h"     /* cvcolcvt_C_globals */
#include "devices.h"      /* devices_* */
#include "ripmulti.h"     /* IS_INTERPRETER */
#include "monitori.h"     /* monitor_swinit */
#include "control.h"      /* rip_work_in_progress */
#include "timing.h"       /* execution_profile_* */
#include "pfin.h"         /* pfin_* */
#include "timerapi.h"
#include "rdrapi.h"
#include "eventapi.h"     /* SwRDR* and SwEvent* APIs and indirection */
#include "apis.h"
#include "timelineapi.h"
#include "blobdata.h"     /* blobdata_* */
#include "swdataimpl.h"   /* dataapi_* */
#include "cmm_module.h"   /* cmm_* et. al. */
#include "ht_module.h"    /* htm_* et. al. */
#include "idlom.h"
#include "wcscmm.h"       /* wcscmm_* */
#include "fltrdimg.h"     /* filtering_C_globals */
#include "halftone.h"     /* halftone_C_globals */
#include "trap.h"         /* trap_C_globals */
#include "dlinit.h"       /* dodl_swstart */
#include "gsinit.h"       /* gstate_swstart */
#include "psinit.h"       /* ps_swstart */
#include "backdropinit.h" /* backdrop_swstart */
#include "functns.h"      /* functions_C_globals */
#include "rcbinit.h"      /* recombine_C_globals */
#include "taskh.h"        /* task_C_globals */
#include "corejob.h"      /* corejob_C_globals */
#include "saves.h"
#include "params.h"
#include "metrics.h"
#include "jobmetrics.h"
#include "asyncps.h"
#include "interrupts.h"
#include "progupdt.h"
#include "devops.h"
#include "riptimeline.h"
#include "irr.h"

#include "fpexception.h"

/** This variable is an exception to the C runtime initialisation policy,
    since it must be unpacked by SwInit() before the C runtime
    initialisation can be run. */
Bool swstart_must_return = FALSE ;

static Bool swexit_has_been_called = FALSE ;
static int32 cached_error_code = 0 ;

/** This variable is an exception to the C runtime initialisation policy,
    since it is itself used to determine if the RIP (and runtime
    initialisation) has been called. */
static enum {
  CORE_NOT_INITIALISED,
  CORE_RUNTIME_INIT,
  CORE_DOING_SWINIT,
  CORE_DONE_SWINIT,
  CORE_DOING_SWSTART,
  CORE_STARTING_INTERPRETER,
  CORE_DOING_POSTBOOT,
  CORE_RUNNING_INTERPRETER,
  CORE_QUITTING
} core_init_state = CORE_NOT_INITIALISED ;

/** This variable is an exception to the C runtime initialisation policy, it
    is used by the core initialisation to track the unwinding stack for
    initialisations. */
static int32 core_init_error = 0 ;

/** The Core RIP's master timeline. */
sw_tl_ref core_tl_ref ;

/** The master priority. All RIP priorities are relative to this. */
sw_tl_priority core_tl_priority ;

/* The trace handler is stored here rather than in timing.c because the
   trace handler is set during unconditionally from the SwStart tags. */
SwTraceHandlerFn *probe_handler = NULL ;

static void rip_finish(void) ;
static void swcore_C_globals(core_init_fns *fns) ;
static void set_swstart_must_return(SWSTART *params) ;

/* Invoke the client SwExit() function but only if it has not already
   been called. This function is Boolean *purely* so people may use
   the short hand "return dispatch_SwExit()" which is often whats
   wanted. */
Bool dispatch_SwExit(int32 code, char* string)
{
  if (! swexit_has_been_called) {
    swexit_has_been_called = TRUE ;

    /* Cache the error code from the first call to this function. */
    cached_error_code = code ;

    /* Call it here as we will never return to SwStart() where the RIP
       is "finished". */
    if (! exiting_rip_cleanly) {
      rip_finish() ;
    }

    /* This should be the *ONLY* call to SwTerminating() directly from within
       the core. We want to inform the skin that we're stopping after
       performing the RIP shutdown actions, because we may want to delete
       files before shutting down the skin's file devices. */
    SwTerminating(code, (uint8*)string) ;
  }

  /* Ensure that we exit the process if the client code has not
     requested that SwStart() returns. */
  if (! swstart_must_return) {
    exit(code) ;
  }

  return FALSE ;
}

/* ========================================================================== */
/* Find the APIs supplied by the skin. We can do nothing without them.

   In the non-DLL build there is only one set of these pointers so we could
   use them immediately, but in the DLL build there are two and we must
   initialise ours. Doing the DLL thing in the non-DLL build is entirely
   benign (as long as we don't zero them first) and means that this file
   doesn't need conditional compilation or we don't need two other compounds
   to supply this function and a stub version for the two builds. This is
   simpler.
*/

#ifndef FAILURE
#define FAILURE(val) val
#endif

/** \todo FIXME: This to move to skinapis and a stub in the skin */

static Bool FindSkinAPIs( SWSTART *start )
{
  void * vpapi ;
  int32 i ;
  sw_rdr_api_20110201 * api = NULL ;

  /* Find the RDR API from the SWSTART structure */

  /* Note that we use a local pointer (api) so that in the non-DLL GUI build we
     don't spend any time with (the global) rdr_api set to null!
  */
  for (i = 0; start[i].tag != SWEndTag; i++) {
    if (start[i].tag == SWRDRAPITag) {
      api = (sw_rdr_api_20110201 *)start[i].value.pointer_value ;
      break ;
    }
  }
  if (api)
    rdr_api = api ;
  else
    return FAILURE(FALSE) ;     /* No RDR api - we're doomed */

  /* RDR "function calls" (indirected) now available */

  /* Find the Event API */
  if (SwFindRDR(RDR_CLASS_API, RDR_API_EVENT, 20110330, &vpapi, NULL)
      != SW_RDR_SUCCESS || vpapi == 0)
    return FAILURE(FALSE) ;   /* Event is required */
  event_api = (sw_event_api_20110330 *) vpapi ;

  /* Find the pthreads API */
  if (SwFindRDR(RDR_CLASS_API, RDR_API_PTHREADS, 20111021, &vpapi, NULL)
      != SW_RDR_SUCCESS || vpapi == 0)
    return FAILURE(FALSE) ;   /* pthreads is required */
  pthread_api = (sw_pthread_api_20111021 *) vpapi ;

  /* Also check that the pthreads DLL has been loaded if applicable */
  if (!pthread_api->valid) {
    pthread_api = NULL ;
    return FAILURE(FALSE) ;   /* somehow it hasn't been initialised */
  }

  /* Find the timer API */
  if (SwFindRDR(RDR_CLASS_API, RDR_API_TIMER, 20110324, &vpapi, NULL)
      != SW_RDR_SUCCESS || vpapi == 0)
    return FAILURE(FALSE) ;   /* Event is required */
  timer_api = (sw_timer_api_20110324 *) vpapi ;

  /* Find the timeline API */
  if (SwFindRDR(RDR_CLASS_API, RDR_API_TIMELINE, 20110623, &vpapi, NULL)
      != SW_RDR_SUCCESS || vpapi == 0)
    return FAILURE(FALSE) ;   /* Timeline is required */
  timeline_api = (sw_timeline_api_20110623 *) vpapi ;

  return TRUE ;
}

/* ========================================================================== */
/** Initialisation and finalisation table.

    The init table has five function pointers for each module. These are:

    1) Runtime init. These functions get the runtime into a known state. They
       \e must not call any functions that allocate memory, or can fail in
       any way. Runtime init is called on the first entry to the RIP.
    2) Preboot initialisation function. These are called during SwInit(), and
       may allocate memory, and prepare state structures ready to accept
       core module registration calls. If the preboot initialisation succeeds,
       then the finaliser for the module will be called on shutdown. The
       SwStart() arguments are available for these functions to examine.
    3) Boot initialisation function. The boot functions are called during
       SwStart(), to prepare the interpreter and RIP state. The SwStart()
       arguments are available for these functions to examine.
    4) Post-boot initialisation functions. The post-boot functions are called
       the end of SwStart(), after the interpreter state is set up ready to go,
       but before the interpreter starts running.
    5) Finalisation function. These are called in reverse order at shutdown,
       but only if one of the pre-boot, boot, or post-boot functions succeeded.

    The order of the first few functions in this table is critical. After
    that, the order should not matter much. When adding new entries, please
    add them before the tasks entry at the end of the table, and try to
    avoid coding the functions so that they rely on state initialised by
    another module during the same phase (it's not always possible). Relying
    on state initialised in a previous phase is OK.
*/
static core_init_t init_functions[] =
{
  /* Must be before mem_init(), because MPS needs pthreads loaded */
  CORE_INIT_LOCAL("apis", FindSkinAPIs, NULL, NULL, NULL),
  /* Multi-render needs to come before systemparams, so the SwInit() phase
     initialises the RendererThreads systemparam correctly. Must also come
     before the first module that uses any locks. */
  CORE_INIT("multi_render", multi_render_C_globals),
  /* Must be before any memory allocation (i.e. just about everything else). */
  CORE_INIT("memory", mem_C_globals),
  /* It's really convenient to allow modules to initialise metrics in
     C_globals routine, however we must deallocate them before the memory
     pools are destroyed. */
  CORE_INIT("metrics", sw_metrics_C_globals),
  CORE_INIT("jobmetrics", sw_jobmetrics_C_globals),
  /* Start of SWcore initialisation. Must be before all other SwInit
     functions, to capture startup time, create per-thread core context,
     and start the monitor to print failure messages. */
  CORE_INIT("core", swcore_C_globals),
  /* Must be before all other remaining SwInit functions. This sets up the
     per-thread core_context_t. */
  CORE_INIT_LOCAL("context", context_swinit, context_swstart, NULL, context_finish),
  /* Monitor is needed early, so we can print out failure messages. */
  CORE_INIT_LOCAL("monitor", monitor_swinit, NULL, NULL, monitor_finish),
  CORE_INIT("asyncps", asyncps_C_globals),
  CORE_INIT("interrupts", interrupts_C_globals),
  CORE_INIT("progress", progress_C_globals),
  CORE_INIT("job", corejob_C_globals),
  CORE_INIT_LOCAL("saves", NULL, saves_swstart, NULL, saves_finish),
  /* The v20 initialisation is clearly in the wrong place, but will be
     removed when v20 is separated into other modules. It's only used for
     C globals initialisation: */
  CORE_INIT("v20", v20_C_globals),
  /* v20 render factored out to do post boot initialisation */
  CORE_INIT("v20render", v20render_C_globals),
  /* Now the rest of the init functions may follow. We start with the basic
     modules, and work up to more complicated ones. */
  CORE_INIT("objects", objects_C_globals),
  CORE_INIT("sw_data_api", dataapi_C_globals),
  CORE_INIT("systemparams", systemparams_C_globals),
  CORE_INIT("userparams", userparams_C_globals),
  CORE_INIT("mischookparams", mischookparams_C_globals),
  CORE_INIT("devices", devices_C_globals),
  CORE_INIT("filter", filter_C_globals),
  /* fileio_init() MUST be after devices_init() and fileio finalizer must be
   * called before filter finalizer. */
  CORE_INIT("fileio", fileio_C_globals),
  /* By this time, we should be able to initialise in almost any order: */
  CORE_INIT("sw_blob_api", blobdata_C_globals),
  CORE_INIT("crypt", crypt_C_globals),
  CORE_INIT("halftone", halftone_C_globals),
  CORE_INIT("gstate", gstate_C_globals),
  CORE_INIT("DODL", dodl_C_globals), /* Must be before fonts. */
  CORE_INIT("fonts", fonts_C_globals),
  CORE_INIT("functions", functions_C_globals),
  CORE_INIT("images", images_C_globals),
  CORE_INIT("imagecontext", imagecontext_C_globals),
  CORE_INIT("IRR page buffer", irr_pgb_C_globals),
  CORE_INIT("cvcolcvt", cvcolcvt_C_globals),
  CORE_INIT("backdrop", backdrop_C_globals),
  CORE_INIT("Recombine", rcbn_C_globals),
  CORE_INIT("pagebuffer", pagebuffer_C_globals),
  CORE_INIT("render", render_C_globals),
  CORE_INIT("postscript", postscript_C_globals),
  CORE_INIT("HDLT", hdlt_C_globals),
  /* Must be before pdfin and pdfout. */
  CORE_INIT("pdf", pdf_C_globals),
  CORE_INIT("pdfin", pdfin_C_globals),
  CORE_INIT("pdfout", pdfout_C_globals),
  CORE_INIT("zipdev", zipdev_C_globals),
  CORE_INIT("wzipdev", writeonly_zipdev_C_globals),
  CORE_INIT("xml", xml_C_globals),
  CORE_INIT("xps", xps_C_globals),
  CORE_INIT("pcl5", pcl5_C_globals),
  CORE_INIT("pclxl", pclxl_C_globals),
  CORE_INIT("CMM", cmm_C_globals),
  CORE_INIT("modular halftone", htm_C_globals),
  /* WCS must be after CMM and XML */
  CORE_INIT("wcs", wcscmm_C_globals),
  CORE_INIT("trapping", trapping_C_globals),
  CORE_INIT("ea-checks", ea_checks_C_globals),
#ifdef FPEXCEPTION
  /* Add a specialiser to enable fp exceptions per thread, should be first
   * specialiser in the list so it covers action of subsequent specialisers.
   * Specialiser list is prepend only so must be last one added. */
  CORE_INIT("fpe", fpe_C_globals),
#endif /* DEBUG_BUILD */
  /* Tasks init must be last, because swstart routine spawns worker thread
     pool. The worker threads must be spawned after all context specialisers
     have been added. */
  CORE_INIT("tasks", task_C_globals)  /* MUST BE LAST */
} ;

/** This function gets called once, when the interpreter is just about to
    start. */
Bool rip_ready(void)
{
  HQASSERT(( (CORE_BASE_MODULE_SWEXIT + NUM_ARRAY_ITEMS(init_functions)) <
             CORE_BASE_SWEXIT), "exit code range for modules needs to be made bigger") ;
  HQASSERT(core_init_state == CORE_STARTING_INTERPRETER,
           "Postboot starting in wrong state") ;

  core_init_state = CORE_DOING_POSTBOOT ;

  /* Restart init recursion and accumulated error numbers */
  core_init_error = CORE_BASE_MODULE_SWEXIT ;

  if ( !core_postboot_run(init_functions, NUM_ARRAY_ITEMS(init_functions)) )
    return FALSE ;

#ifdef METRICS_BUILD
  sw_metrics_reset(SW_METRICS_RESET_POSTBOOT) ;
#endif

  core_init_state = CORE_RUNNING_INTERPRETER ;

  return TRUE ;
}

static void rip_finish(void)
{
  if (core_init_state >= CORE_STARTING_INTERPRETER &&
      core_init_state <= CORE_RUNNING_INTERPRETER)
    stop_interpreter();

  core_init_state = CORE_QUITTING ;

  /* Call finish functions in reverse order. */
  core_finish_run(init_functions, NUM_ARRAY_ITEMS(init_functions)) ;

  core_init_state = CORE_NOT_INITIALISED ;

  return ;
}

/** Error dispatch catch-all for module initialisation. If the module init
    calls dispatch_SwExit() itself it is likely to have more detail but it is
    OK for a module init function to just return FALSE. In that case this
    lookup at least tells us which module failed. */
static Bool core_init_failed(char *name)
{
  char message_buf[1024] ;
  swcopyf((uint8*)message_buf, (uint8 *)"Unable to initialise \"%s\" sub-system.", name) ;

  HQASSERT(core_init_error < CORE_BASE_SWEXIT,
           "Exit code range for modules needs to be made bigger") ;

  return dispatch_SwExit(CORE_BASE_MODULE_SWEXIT + core_init_error, message_buf) ;
}

void core_C_globals_run(core_init_t initialisers[], size_t n_init)
{
  size_t i ;

  HQASSERT(core_init_state == CORE_RUNTIME_INIT,
           "Initialising globals when not in runtime init phase") ;

  for (i = 0 ; i < n_init ; ++i ) {
    /* Runtime initialiser must fill in remaining functions. */
    initialisers[i].needs_finish = FALSE ;
    if ( initialisers[i].init_C_globals != NULL ) {
      initialisers[i].fns.swinit = NULL ;
      initialisers[i].fns.swstart = NULL ;
      initialisers[i].fns.postboot = NULL ;
      initialisers[i].fns.finish = NULL ;

      /* No errors are permitted in runtime initialisation. */
      (*initialisers[i].init_C_globals)(&initialisers[i].fns) ;
    }
    HQASSERT(initialisers[i].fns.finish == NULL ||
             (initialisers[i].fns.swinit != NULL ||
              initialisers[i].fns.swstart != NULL ||
              initialisers[i].fns.postboot != NULL),
             "Finaliser exists without swinit/swstart/postboot function") ;
  }

}

#ifdef ASSERT_BUILD

static Bool trace_initialisers = FALSE;

#define FAIL_PHASE_INIT     (0)
#define FAIL_PHASE_START    (1)
#define FAIL_PHASE_POSTBOOT (2)
#define FAIL_PHASES         (3)

static char* module_fail[FAIL_PHASES] = {
  "", "", ""
};

static char* phases[FAIL_PHASES] = {
  "init:", "start:", "postboot:"
};

static
void setup_module_fail(void)
{
  char* var;
  int32 i;

  /* Look for environment var SW_FAIL_MODULE which if present identifies which
   * module to pretend as failed in which phase.
   *
   * Scan the variable string for a phase and a module name:
   *   SW_FAIL_MODULE=init:color
   * For now look for known phase in variable rather than extract name from
   * variable and see if a known phase.  If match a phase name, set module fail
   * name for phase to start of the name after the phase indicator.
   */
  if ((var = getenv("SW_FAIL_MODULE")) != NULL) {
    trace_initialisers = TRUE;
    for (i = 0; i < FAIL_PHASES; i++) {
      if (strncmp(var, phases[i], strlen(phases[i])) == 0) {
        module_fail[i] = var + strlen(phases[i]);
        break;
      }
    }
  }
}

static
Bool force_module_fail(
  char* module_name,
  int32 phase)
{
  return (trace_initialisers && strcmp(module_name, module_fail[phase]) == 0);
}

#else /* !ASSERT_BUILD */

#define setup_module_fail()     EMPTY_STATEMENT()
#define force_module_fail(a, b) (FALSE)

#endif /* !ASSERT_BUILD */


Bool core_swinit_run(core_init_t initialisers[], size_t n_init,
                     SWSTART *params)
{
  size_t i ;

  HQASSERT(core_init_state == CORE_DOING_SWINIT,
           "Running SwInit initialisers when not in SwInit()") ;

  HQTRACE(trace_initialisers, ("Doing initialisers ..."));
  for (i = 0 ; i < n_init ; ++i, ++core_init_error ) {
    if ( initialisers[i].fns.swinit ) {
      HQTRACE(trace_initialisers, ("Initializing: %s", initialisers[i].name));
      if ( force_module_fail(initialisers[i].name, FAIL_PHASE_INIT) ||
           !(*initialisers[i].fns.swinit)(params) ) {
        HQTRACE(trace_initialisers, ("... failed"));
        /* In this case we know that no previous phases have run, so we can
           use the current number as the number of finalisers to check. */
        core_finish_run(initialisers, i) ;
        return core_init_failed(initialisers[i].name) ;
      }
      initialisers[i].needs_finish = TRUE ;
    }
  }
  HQTRACE(trace_initialisers, ("... initialisers finished."));

  return TRUE ;
}

Bool core_swstart_run(core_init_t initialisers[], size_t n_init,
                      SWSTART *params)
{
  size_t i ;

  HQASSERT(core_init_state == CORE_DOING_SWSTART,
           "Running SwStart initialisers when not in SwStart()") ;

  HQTRACE(trace_initialisers, ("Doing starters ..."));
  for (i = 0 ; i < n_init ; ++i, ++core_init_error ) {
    if ( initialisers[i].fns.swstart ) {
      HQTRACE(trace_initialisers, ("Starting: %s", initialisers[i].name));
      if ( force_module_fail(initialisers[i].name, FAIL_PHASE_START) ||
           !(*initialisers[i].fns.swstart)(params) ) {
        HQTRACE(trace_initialisers, ("... failed"));
        /* A previous phase may have successfully initialised modules after
           this one in the list, so run all of the finalisers. */
        core_finish_run(initialisers, n_init) ;
        return core_init_failed(initialisers[i].name) ;
      }
      initialisers[i].needs_finish = TRUE ;
    }
  }
  HQTRACE(trace_initialisers, ("... starters finished."));

  return TRUE ;
}

Bool core_postboot_run(core_init_t initialisers[], size_t n_init)
{
  size_t i ;

  HQTRACE(trace_initialisers, ("Doing postboot ..."));
  for (i = 0 ; i < n_init ; ++i, ++core_init_error ) {
    if ( initialisers[i].fns.postboot ) {
      HQTRACE(trace_initialisers, ("PostBoot: %s", initialisers[i].name));
      if ( force_module_fail(initialisers[i].name, FAIL_PHASE_POSTBOOT) ||
           !(*initialisers[i].fns.postboot)() ) {
        HQTRACE(trace_initialisers, ("... failed"));
        /* A previous phase may have successfully initialised modules after
           this one in the list, so run all of the finalisers. */
        core_finish_run(initialisers, n_init) ;
        return core_init_failed(initialisers[i].name) ;
      }
      initialisers[i].needs_finish = TRUE ;
    }
  }
  HQTRACE(trace_initialisers, ("... postbooters finished."));

  return TRUE ;
}

void core_finish_run(core_init_t initialisers[], size_t n_init)
{
  HQTRACE(trace_initialisers, ("Doing finishers ..."));
  /* Call finish functions in reverse order. */
  while ( n_init-- > 0 ) {
    if (initialisers[n_init].needs_finish) {
      if ( initialisers[n_init].fns.finish != NULL ) {
        /* Report module being finished to track crashes. */
        HQTRACE(trace_initialisers, ("Finishing: %s", initialisers[n_init].name));
        (*initialisers[n_init].fns.finish)();
      }
      initialisers[n_init].needs_finish = FALSE ;
    }
  }
  HQTRACE(trace_initialisers, ("... finishers finished."));
}


/* ----------------------------------------------------------------------------
 * C runtime setup.
 * ---------------------------------------------------------------------------- */
/* Reset all globals. Load any additional DLL's required (like pthreads.dll)
 * and any other runtime stuff. */

static void init_C_globals_swstart(void)
{
  /* Default is to call exit() for backward compatibility. */
  exiting_rip_cleanly = FALSE ;
  /* Ensure that SwExit() is only ever called once. */
  swexit_has_been_called = FALSE ;
  /* We have not started the interpreter yet. */
  cached_error_code = 0 ;

  core_tl_ref = SW_TL_REF_INVALID ;
  core_tl_priority = SW_TL_PRIORITY_UNKNOWN ;

  probe_handler = NULL ;
}

/* ----------------------------------------------------------------------------
 * Exported RIP control functions
 * ---------------------------------------------------------------------------- */

Bool RIPCALL SwInit( SWSTART *params )
{
  Bool result ;

#ifdef FPEXCEPTION
  /* STEP -1. Enable fp exceptions for all the RIP does in the startup thread */

  enable_fp_exceptions();

/* Use following block of code to test fp exceptions are firing when turning
 * exceptions on on a new platform/compiler. */
#undef MIKES_FP_TEST
#if defined(MIKES_FP_TEST)
  {
    /* Simple test that exceptions are working in non-release builds and are
     * masked in release builds. */
    double x = 1.0;
    double y = 0.0;
    double z = x/y;
  }
#endif /* MIKES_FP_TEST */
#endif /* FPEXCEPTION */

  /* STEP 0. We need to know whether to return or exit the process on
     failure. */

  swstart_must_return = FALSE ;
  set_swstart_must_return(params) ;

  /* STEP 1. Init C globals and anything else related to the C
     runtime environment. */

  if (core_init_state != CORE_NOT_INITIALISED) {
    HQFAIL("SwInit() called more than once");
    /** \todo ajcd 2009-02-09: Should this really shut down the
        previously-initialised RIP instance? Why not just exit instead? */
    return dispatch_SwExit( swexit_error_meminit_01, "SwInit() called more than once.") ;
  }

  core_init_state = CORE_RUNTIME_INIT ;

  /* Run all of the C runtime initialiser functions. Always the VERY first
     thing SwInit does. Emulates executable load time static
     initialization. */
  core_C_globals_run(init_functions, NUM_ARRAY_ITEMS(init_functions)) ;

  /* At this point, all of the C statics and globals should have been set to
     their initial values. */
  core_init_state = CORE_DOING_SWINIT ;

  /* STEP 2. Run all of the preboot initialiser functions. These functions
     may fail, causing an immediate cleanup and exit. */

  /* Restart init recursion and accumulated error numbers */
  core_init_error = CORE_BASE_MODULE_SWEXIT ;

  setup_module_fail();

  result = core_swinit_run(init_functions, NUM_ARRAY_ITEMS(init_functions), params) ;

  HQASSERT(core_init_error < CORE_BASE_SWEXIT,
           "Exit code range for modules needs to be made bigger") ;

  /* Clear core context from the startup thread */
  clear_core_context();

  if ( result )
    core_init_state = CORE_DONE_SWINIT ;

  /* STEPS 3 & 4 are run during SwStart. */

  return result ;
}

/* Initial entry point to RIP. This initialises all of the modules present,
   and then calls the interpreter initialisation. */
HqBool RIPCALL SwStart(SWSTART *params)
{
  Bool result ;

#ifdef FPEXCEPTION
  /* Ensure fp exceptions enabled when the RIP is run in a different thread to
   * the one used to initialise it */
  enable_fp_exceptions();
#endif /* FPEXCEPTION */

  /* One way or another SwInit is *ALWAYS* called before
     SwStart(). */
  if ( core_init_state == CORE_NOT_INITIALISED ) {
    /* If SwInit fails, it calls rip_finish() */
    if ( !SwInit(params) )
      return FALSE ;
  }

  if ( core_init_state != CORE_DONE_SWINIT ) {
    HQFAIL("SwStart called with core in inconsistent state") ;
    return FALSE ;
  }

  /* Set this parameter again, in case it was only supplied to SwStart(). */
  set_swstart_must_return(params) ;

  core_init_state = CORE_DOING_SWSTART ;

  /* Restart init recursion and accumulated error numbers */
  core_init_error = CORE_BASE_MODULE_SWEXIT ;

  result = core_swstart_run(init_functions, NUM_ARRAY_ITEMS(init_functions),
                            params) ;

  HQASSERT(core_init_error < CORE_BASE_SWEXIT,
           "Exit code range for modules needs to be made bigger") ;

  if ( !result ) {
    HQASSERT(core_init_state == CORE_NOT_INITIALISED,
             "SwStart failure didn't clean up properly") ;
    return FALSE ;
  }

  core_init_state = CORE_STARTING_INTERPRETER ;

  /* We don't care about the return status of the interpreter. It
     takes care of error handling itself. It also deals with the calls
     to dispatch_SwExit() under any error condition which may
     arise. */
  start_interpreter() ;

  /* If we get this far there are two scenarios:

     a. start_interpreter has returned raising an error condition
     itself (i.e. It has called dispatch_SwExit()).

     b. Is returning cleanly under no error condition.

     Since we don't know which, we will invoke a non-error call to
     dispatch_SwExit(). This is OK because dispatch_SwExit() protects
     itself when called multiple times ensuring the client only gets
     to see the first dispatch. */
  rip_finish() ;

  /* Make sure client always gets a call to SwExit() under all
     conditions. */
  if (exiting_rip_cleanly) {
    (void)dispatch_SwExit(0, NULL) ;
    return TRUE ;
  }

  /* The only way start_interpreter will return is if
     exiting_rip_cleanly is TRUE which is caught above. */
  HQFAIL("Should NEVER reach here.") ;

  /* If there is a bug in the code and the above assert fires (because
     of a bug in the RIP), the function will return anyway. This is
     the best we can do at this stage and release builds should
     continue to work. */
  return dispatch_SwExit(1, "exiting_rip_cleanly protocol error.") ;
}

/* Declare here for now until code is ripped out and untagled
   somewhat. Ho hum. */
void RIPCALL SwStop( void )
{
  if (swstart_must_return) {
    /* If only we could call quit_() directly, but we can't. But if we
       could, this is what the code should do.

    exiting_rip_cleanly = TRUE ;
    (void)quit_() ;
    */
  } else {
    /* This is historic code and can't be removed until the GUI RIP
       products have been refactored. The GUI products DO NOT
       currently make use of swstart_must_return (yet). */
    HQASSERT(IS_INTERPRETER(),
             "SwStop should only be called from the interpreter thread.");
    if (IS_INTERPRETER()) {
      rip_finish() ;
    }
  }
}

Bool rip_is_inited(void)
{
  return (core_init_state >= CORE_RUNNING_INTERPRETER);
}

Bool rip_is_quitting(void)
{
  return (core_init_state == CORE_QUITTING);
}

static void set_swstart_must_return(SWSTART *params)
{
  int32 i ;

  for (i = 0; params[i].tag != SWEndTag; i++) {
    if (params[i].tag == SWSwStartReturnsTag ) {
      swstart_must_return = params[i].value.int_value ;
      break;
    }
  }
}

static Bool swcore_swinit(SWSTART *params)
{
  int i ;
  sw_tl_ref parent = SW_TL_REF_INVALID ;

  for ( i = 0 ; params[i].tag != SWEndTag ; ++i ) {
    switch ( params[i].tag ) {
    case SWTraceHandlerTag:
      probe_handler = params[i].value.probe_function ;
      break ;
    case SWTimelineParentTag:
      parent = params[i].value.int_value ;
      break ;
    }
  }

  probe_other(SW_TRACE_CORE, SW_TRACETYPE_MARK, 0) ;

  core_tl_priority = SwTimelineGetPriority(parent) ;
  if ( core_tl_priority == SW_TL_PRIORITY_UNKNOWN )
    core_tl_priority = SW_TL_PRIORITY_NORMAL ;

  if ( (core_tl_ref = SwTimelineStart(SWTLT_CORE, parent,
                                      0 /*start*/,
                                      SW_TL_EXTENT_INDETERMINATE /*end*/,
                                      SW_TL_UNIT_JOBS, core_tl_priority,
                                      NULL /*context*/,
                                      NULL /*title*/, 0 /*length*/))
       == SW_TL_REF_INVALID )
    return FALSE ;

  return TRUE ;
}

static Bool swcore_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  probe_begin(SW_TRACE_CORE, 0) ;

  return TRUE ;
}

static void swcore_finish(void)
{
  if ( SwTimelineEnd(core_tl_ref) != SW_TL_SUCCESS ) {
    /* Probably some child holding it open. That would almost certainly be a
       bug. On the other hand, there's no context associated with the root
       timeline, so maybe we could allow it? */
    CHECK_TL_SUCCESS(SwTimelineAbort(core_tl_ref, 0)) ;
    HQFAIL("Core timeline refused to end") ;
  }
  core_tl_ref = SW_TL_REF_INVALID ;

  probe_end(SW_TRACE_CORE, 0) ;
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( context )
IMPORT_INIT_C_GLOBALS( saves )

static void swcore_C_globals(core_init_fns *fns)
{
  /* context has its own entry point */
  init_C_globals_context() ;
  init_C_globals_saves() ;
  init_C_globals_swstart() ;

  fns->swinit = swcore_swinit ;
  fns->swstart = swcore_swstart ;
  fns->finish = swcore_finish ;
}

/* ----------------------------------------------------------------------------
Log stripped */
