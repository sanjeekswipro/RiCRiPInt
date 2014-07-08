/* Copyright (C) 1999-2013 Global Graphics Software Ltd. All Rights Reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:skinkit.c(EBDSDK_P.1) $
 */

/**
 * \file
 * \ingroup skinkit

 * \brief This file implements the interface between the "push" model
 * of the \c SwLe example skin functions, and the natural "pull" model
 * of the core Harlequin RIP interface. It does this by synchronizing
 * the thread on which the RIP is running with the calling application
 * thread.
 */

#include "skinkit.h"
#include "kit.h"
#include "sync.h"
#include "ripthread.h"
#include "mem.h"
#include "hqstr.h"  /* strlen_int32 */
#include "devparam.h"
#include "threadapi.h"
#include "swevents.h"
#include "progevts.h"
#include "probelog.h"
#include "swtrace.h"
#include "swtimelines.h"
#include "timer.h"
#include "skindevs.h"

/* core interface */
#include "swcopyf.h"      /* vswncopyf(), equivalent to vsnprintf() */

#ifdef LESEC
#include "handshake.h"
#endif

#include <stdlib.h>    /* exit */
#include <stdio.h>
#include <stddef.h>    /* offsetof */
#include <string.h>
#include <errno.h>

/**
 * \brief Used to implement a simple state machine representing the
 * host application's calls to the high-level \c SwLe RIP controller
 * functions, SwLeStart(), SwLeJobStart(), SwLePs() and SwLeJobEnd().
 * This state machine is used to assert that the application is
 * calling into the \c SwLe interface in the correct order.
 */
enum
{
  NOTHING_CALLED = 1, /*!< No \c SwLe functions called yet. */
  RIPSTART_CALLED,    /*!< SwLeStart() has been legally called. */
  JOBSTART_CALLED,    /*!< SwLeJobStart() has been legally called. */
  PS_CALLED,          /*!< SwLePs() has been legally called. */
  JOBEND_CALLED       /*!< SwLeJobEnd() has been legally called. */
};

/**
 * \brief Used to implement a simple state machine representing the
 * lifecycle of the RIP.
 */
enum
{
  UNSTARTED = 1,         /*!< No attempt has been made to boot the RIP. */
  INITIALISING,          /*!< The RIP thread is preparing to boot the RIP. */
  WAITING_FOR_JOB_START, /*!< The RIP has been booted, and is now
                           waiting for the application thread to submit
                           a job. */
  PROCESSING_JOB_START,  /*!< The RIP is processing initial job configuration. */
  PROCESSING_PS,         /*!< The RIP is processing a PostScript job. */
  PROCESSING_JOB_ERROR,  /*!< The current job has errored. */
  PROCESSING_JOB_END,    /*!< The current job is finished. */
  EXITING,               /*!< The RIP is in the process of exiting. */
  EXITED                 /*!< The RIP has exited. */
};


/**
 * \brief Gathers together various data involved in controlling the
 * RIP and recording its current state.
 */
typedef struct
{
  int32                   appState; /*!< Enum value recording high-level application's calls to \c SwLe functions. */
  int32                   ripState; /*!< Enum value recording state of the application's underlying RIP. */
  int32                   fConfigJobFailed; /*!< \c TRUE <=> the configuration PostScript passed to SwLeJobStart() failed. */
  SwLeMONITORCALLBACK   * pfnMonitor; /*!< Holds the monitor callback implementation pointer passed to SwLeStart(). */
  SwLeRASTERCALLBACK    * pfnRaster;  /*!< Holds the raster callback implementation pointer passed to SwLeJobStart(). */
  SwLeRASTERSTRIDE      * pfnRasterStride; /*!< Holds the raster stride callback implementation pointer passed to SwLeSetMemoryCallbacks(). */
  SwLeRASTERREQUIREMENTS * pfnRasterRequirements; /*!< Holds the raster requirements callback implementation pointer passed to SwLeSetMemoryCallbacks(). */
  SwLeRASTERDESTINATION * pfnRasterDestination;  /*!< Holds the raster destination callback implementation pointer passed to SwLeSetMemoryCallbacks(). */
  void                  * pJobContext; /*!< Holds any opaque application-defined per-job context data passed to SwLeJobStart(). */
  SwLeRIPEXITCALLBACK   * pfnRipExit; /*!< Holds the rip exit callback passed to SwLeSetRipExitFunction(). */
  SwLeRIPREBOOTCALLBACK * pfnRipReboot; /*!< Holds the rip reboot callback passed to SwLeSetRipRebootFunction(). */
  int32                   cbBuffer; /*!< Length in bytes of the \c pBuffer value. */
  uint8                 * pBuffer;  /*!< A \c pBuffer value passed
                                      either to SwLeJobStart() or
                                      SwLeJobPs(), depending on the
                                      application state. */
  int32                   nErrorLevel; /*!< System error report level. */
} ControlData;



static ControlData controlData =
{
  NOTHING_CALLED,  /* appState */
  UNSTARTED,       /* ripState */
  FALSE,           /* fConfigJobFailed */
  NULL,            /* pfnMonitor */
  NULL,            /* pfnRaster */
  NULL,            /* pfnRasterStride */
  NULL,            /* pfnRasterRequirements */
  NULL,            /* pfnRasterDestination */
  NULL,            /* pJobContext */
  NULL,            /* pfnRipExit */
  NULL,            /* pfnRipReboot */
  0,               /* cbBuffer */
  NULL,            /* pBuffer */
  0                /* nErrorLevel */
};


static void * app_blocking_sema = NULL;
static void * rip_blocking_sema = NULL;

static int32 rip_exit_status = 0;

void KSwLeStop(void);
static void PYieldToRIP(void);
static void PYieldToApp(void);

/**
 * \brief Performs early startup actions.
 *
 * \param pfnMonitor The callback for monitor messages.
 *
 * \return \c TRUE upon success, \c FALSE upon failure. If this function
 * fails, the caller should not proceed with trying to boot the RIP.
 */
static HqBool SwStartupCommon( SwLeMONITORCALLBACK *pfnMonitor );

static void SwStopCommon(void);

static void init_C_runtime_skinkit(void);

/* ---------------------------------------------------------------------- */
/* Event handlers to detect job failure. */
static sw_event_result HQNCALL job_error_handler(void *context, sw_event *event)
{
  SWMSG_ERROR *msg = event->message;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_ERROR))
    return SW_EVENT_ERROR;

#if 0
  /* Test features of error handling. */
  SkinMonitorf("%%%%[ Exception; Job %ld; Page %u; Fail %s; Error number %u; Error %.*s; Command %.*s ]%%%%\n",
               msg->job_cookie, msg->page_number,
               msg->fail_job ? "true" : "false",
               msg->error_number,
               msg->error_name.length, msg->error_name.string,
               msg->command.length, msg->command.string) ;
#if 0
  /* Setting these fields allows the RIP to continue regardless of render
     errors. */
  msg->fail_job = FALSE ;
  msg->suppress_handling = TRUE ;
#endif
#endif

  if (msg->fail_job)
    controlData.fConfigJobFailed = TRUE;

  return SW_EVENT_CONTINUE;
}

static sw_event_handlers error_handlers[] = {
  {&job_error_handler, NULL, 0, SWEVT_INTERPRET_ERROR, SW_EVENT_DEFAULT},
  {&job_error_handler, NULL, 0, SWEVT_RENDER_ERROR, SW_EVENT_DEFAULT},
} ;

/* -------------------------------------------------------------------------- */
/* Retained raster event probes. */

/* These are probably contiguous, but for safety, list them. */
const static int rr_event_trace_ids[] = {
  SW_TRACE_RR_PAGE_DEFINE,
  SW_TRACE_RR_PAGE_READY,
  SW_TRACE_RR_PAGE_COMPLETE,
  SW_TRACE_RR_ELEMENT_DEFINE,
  SW_TRACE_RR_ELEMENT_LOCK,
  SW_TRACE_RR_ELEMENT_UNLOCK,
  SW_TRACE_RR_ELEMENT_PENDING,
  SW_TRACE_RR_ELEMENT_QUERY,
  SW_TRACE_RR_ELEMENT_UPDATE_RASTER,
  SW_TRACE_RR_ELEMENT_UPDATE_HITS,
  SW_TRACE_RR_CONNECT,
  SW_TRACE_RR_DISCONNECT
} ;

/* For each retained raster event, this highest-priority handler brackets the
   remaining handlers within an ENTER/EXIT pair. */
static sw_event_result HQNCALL probe_rr_event(void * context, sw_event * ev)
{
  sw_event_result result ;
  intptr_t designator = (intptr_t)ev->message ; /* the probe designator */
  int id = 0 ;

  UNUSED_PARAM(void *, context) ;

  switch (ev->type) {
# define IS(is) id = SW_TRACE_ ## is ; break
  case SWEVT_RR_PAGE_DEFINE:           IS(RR_PAGE_DEFINE) ;
  case SWEVT_RR_PAGE_READY:            IS(RR_PAGE_READY) ;
  case SWEVT_RR_PAGE_COMPLETE:         IS(RR_PAGE_COMPLETE) ;
  case SWEVT_RR_ELEMENT_DEFINE:        IS(RR_ELEMENT_DEFINE) ;
  case SWEVT_RR_ELEMENT_LOCK:          IS(RR_ELEMENT_LOCK) ;
  case SWEVT_RR_ELEMENT_UNLOCK:        IS(RR_ELEMENT_UNLOCK) ;
  case SWEVT_RR_ELEMENT_PENDING:       IS(RR_ELEMENT_PENDING) ;
  case SWEVT_RR_ELEMENT_QUERY:         IS(RR_ELEMENT_QUERY) ;
  case SWEVT_RR_ELEMENT_UPDATE_RASTER: IS(RR_ELEMENT_UPDATE_RASTER) ;
  case SWEVT_RR_ELEMENT_UPDATE_HITS:   IS(RR_ELEMENT_UPDATE_HITS) ;
  case SWEVT_RR_CONNECT:               IS(RR_CONNECT) ;
  case SWEVT_RR_DISCONNECT:            IS(RR_DISCONNECT) ;
# undef IS
  default: return SW_EVENT_CONTINUE ;
  }

  /* Trace how long the handlers take */
  SwLeProbe(id, SW_TRACETYPE_ENTER, designator) ;
  result = SwEventTail(ev) ;
  SwLeProbe(id, SW_TRACETYPE_EXIT, designator) ;

  /* IMPORTANT! We never want to continue back into the other handlers again */
  if (result == SW_EVENT_CONTINUE)
    result = SW_EVENT_FORCE_UNHANDLED ;

  return result ;
}

/* Highest priority handlers to probe retained raster events. */
static sw_event_handlers rr_event_probes[] = {         /* highest priority */
  {&probe_rr_event, NULL, 0, SWEVT_RR_PAGE_DEFINE,           MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_PAGE_READY,            MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_PAGE_COMPLETE,         MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_DEFINE,        MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_LOCK,          MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_UNLOCK,        MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_PENDING,       MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_QUERY,         MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_UPDATE_RASTER, MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_ELEMENT_UPDATE_HITS,   MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_CONNECT,               MAXINT32},
  {&probe_rr_event, NULL, 0, SWEVT_RR_DISCONNECT,            MAXINT32},
} ;

/* ---------------------------------------------------------------------- */
/*
 * One last_error TLS value is used for all the skin devices.  This works
 * because the device protocol is that last_error() must be called immediately
 * on failure.
 */
static pthread_key_t skindevices_key;

int32 RIPCALL skindevices_last_error(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  return CAST_INTPTRT_TO_INT32((intptr_t)pthread_getspecific(skindevices_key));
}

void skindevices_set_last_error(int32 error)
{
  int res = pthread_setspecific(skindevices_key, (void*)((intptr_t)error));
  UNUSED_PARAM(int, res);
  HQASSERT(res == 0, "pthread_setspecific failed");
}

/* ---------------------------------------------------------------------- */
/* Timeline debug probes. */
const static int tl_trace_ids[] = {
  SW_TRACE_TIMELINE,
  SW_TRACE_FILE_PROGRESS,
  SW_TRACE_JOB_STREAM_TL,
  SW_TRACE_JOB_TL,
  SW_TRACE_INTERPRET_PAGE_TL,
  SW_TRACE_RENDER_PAGE_TL,
  SW_TRACE_RR_SCANNING_TL,
  SW_TRACE_TRAP_PREPARATION_TL,
  SW_TRACE_TRAP_GENERATION_TL,
  SW_TRACE_PGB_TL,
} ;

static sw_event_result HQNCALL probe_timeline(void *context, sw_event *ev)
{
  SWMSG_TIMELINE *msg = ev->message ;
  int trace_id, trace_type ;
  intptr_t designator ;

  UNUSED_PARAM(void *, context);
  HQASSERT(msg != NULL, "No timeline message") ;

  trace_id = SW_TRACE_TIMELINE ;

  switch ( msg->type ) {
  case SWTLT_FILE_INTERPRET:   trace_id = SW_TRACE_FILE_PROGRESS ; break ;
  case SWTLT_JOB_STREAM:       trace_id = SW_TRACE_JOB_STREAM_TL ; break ;
  case SWTLT_JOB:              trace_id = SW_TRACE_JOB_TL ; break ;
  case SWTLT_INTERPRET_PAGE:   trace_id = SW_TRACE_INTERPRET_PAGE_TL ; break ;
  case SWTLT_RENDER_PAGE:      trace_id = SW_TRACE_RENDER_PAGE_TL ; break ;
  case SWTLT_SCANNING_PAGES:   trace_id = SW_TRACE_RR_SCANNING_TL ; break ;
  case SWTLT_TRAP_PREPARATION: trace_id = SW_TRACE_TRAP_PREPARATION_TL ; break ;
  case SWTLT_TRAP_GENERATION:  trace_id = SW_TRACE_TRAP_GENERATION_TL ; break ;
  case SWTLT_PGB:              trace_id = SW_TRACE_PGB_TL ; break ;
  }

  if ( trace_id == SW_TRACE_TIMELINE ) {
    designator = msg->type ;
  } else {
    designator = msg->ref ;
  }

  switch ( ev->type ) {
  case EVENT_TIMELINE_START:    trace_type = SW_TRACETYPE_ENTER ; break ;
  case EVENT_TIMELINE_TITLE:    trace_type = SW_TRACETYPE_TITLE ; break ;
  case EVENT_TIMELINE_EXTEND:   trace_type = SW_TRACETYPE_EXTEND ; break ;
  case EVENT_TIMELINE_PROGRESS: trace_type = SW_TRACETYPE_PROGRESS ; break ;
  case EVENT_TIMELINE_ENDING:   trace_type = SW_TRACETYPE_ENDING ; break ;
  case EVENT_TIMELINE_ABORTING: trace_type = SW_TRACETYPE_ABORTING ; break ;
  case EVENT_TIMELINE_ENDED:
  case EVENT_TIMELINE_ABORTED:  trace_type = SW_TRACETYPE_EXIT ; break ;
  default:
    return SW_EVENT_CONTINUE ;
  }

  SwLeProbe(trace_id, trace_type, designator) ;

  return SW_EVENT_CONTINUE ;
}

/* Highest priority handler to probe timelines. */
static sw_event_handlers tl_probe_handlers[] = {      /* highest priority */
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_START,    MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_TITLE,    MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_EXTEND,   MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_PROGRESS, MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_ENDING,   MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_ABORTING, MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_ENDED,    MAXINT32},
  {&probe_timeline, NULL, 0, EVENT_TIMELINE_ABORTED,  MAXINT32},
} ;

void begin_event_probes()
{
  int i ;

  /* If any of the timeline probes are enabled, enable the timeline probe
     handler. */
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(tl_trace_ids) ; ++i ) {
    if ( g_pabTraceEnabled[tl_trace_ids[i]] ) {
      (void)SwRegisterHandlers(tl_probe_handlers,
                               NUM_ARRAY_ITEMS(tl_probe_handlers)) ;
      break ;
    }
  }
  if ( i < NUM_ARRAY_ITEMS(tl_trace_ids) ) {
    /* All timeline probes except for SW_TRACE_TIMELINE itself use the
       designator for matching. */
    for ( i = 0 ; i < NUM_ARRAY_ITEMS(tl_trace_ids) ; ++i ) {
      if ( tl_trace_ids[i] != SW_TRACE_TIMELINE )
        SwLeProbe(tl_trace_ids[i], SW_TRACETYPE_OPTION,
                  SW_TRACEOPTION_TIMELINE) ;
    }
  }

  /* If any of the retained raster event probes are enabled, register the
     handler. */
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(rr_event_trace_ids) ; ++i ) {
    if ( g_pabTraceEnabled[rr_event_trace_ids[i]] ) {
      (void)SwRegisterHandlers(rr_event_probes,
                               NUM_ARRAY_ITEMS(rr_event_probes)) ;
      break ;
    }
  }
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(rr_event_trace_ids) ; ++i ) {
    SwLeProbe(rr_event_trace_ids[i], SW_TRACETYPE_OPTION,
              SW_TRACEOPTION_TIMELINE) ;
  }
}

void end_event_probes()
{
  (void)SwDeregisterHandlers(rr_event_probes,
                             NUM_ARRAY_ITEMS(rr_event_probes)) ;

  (void)SwDeregisterHandlers(tl_probe_handlers,
                             NUM_ARRAY_ITEMS(tl_probe_handlers)) ;
}

/* ---------------------------------------------------------------------- */

static void *HQNCALL timer_alloc(size_t size)
{
  return MemAlloc(size, FALSE, FALSE);
}

static void HQNCALL timer_free(void* mem)
{
  MemFree(mem);
}

/* ---------------------------------------------------------------------- */

static pthread_mutex_t monitor_mtx = PTHREAD_MUTEX_INITIALIZER ;

/* ------------------------- SwLe External Functions ------------------------*/

HqBool SwLeInitRuntime( void * pContext )
{
  UNUSED_PARAM(void *, pContext);
  init_C_runtime_skinkit();
  return TRUE ;
}

HqBool SwLeSDKStart(size_t *RIP_maxAddressSpaceInBytes,
                    size_t *RIP_workingSizeInBytes,
                    void * pMemory,
                    SysMemFns * pSysMemFns,
                    uint8 **reasonText)
{
  uint8 *dummyReason ;

  /* Need to call this before any memory allocation. */
  *RIP_workingSizeInBytes = MemInit(RIP_maxAddressSpaceInBytes,
                                    *RIP_workingSizeInBytes,
                                    pMemory, pSysMemFns);

  if ( reasonText == NULL )
    reasonText = &dummyReason ;
  *reasonText = NULL ;

  if ( rdr_start() ) {
    if ( event_start() ) {
      if ( timeline_start() ) {
        if ( hqn_timer_init(timer_alloc, timer_free) ) {
          /* All subsystems initialised correctly. */
          return TRUE ;
        }
        timeline_end() ;
        if ( *reasonText == NULL )
          *reasonText = (uint8 *)"Cannot start the timer subsystem" ;
      }
      event_end() ;
      if ( *reasonText == NULL )
        *reasonText = (uint8 *)"Cannot start the timeline subsystem" ;
    }
    rdr_end() ;
    if ( *reasonText == NULL )
      *reasonText = (uint8 *)"Cannot start the event subsystem" ;
  }
  if ( *reasonText == NULL )
    *reasonText = (uint8 *)"Cannot start the RDR subsystem" ;

  MemFinish(EXIT_FAILURE) ;
  return FALSE ;
}

void SwLeSDKEnd(int32 exitCode)
{
  /* Shut down timer system */
  hqn_timer_finish();

  /* Shut down timelines before Events. */
  timeline_end() ;

  /* Shut down event before RDR. */
  event_end() ;

  /* Shut down RDR before MPS. */
  rdr_end() ;

  /* Shut down MPS. */
  MemFinish(exitCode) ;
}

void SwLeMemInit(size_t RIP_maxAddressSpaceInBytes, size_t RIP_workingSizeInBytes,
                 size_t RIP_emergencySizeInBytes,
                 void * pMemory)
{
  InitRipMemory(RIP_maxAddressSpaceInBytes, RIP_workingSizeInBytes,
                RIP_emergencySizeInBytes, pMemory);
}

HqBool SwLeStart(size_t                RIP_maxAddressSpaceInBytes,
                 size_t                RIP_workingSizeInBytes,
                 size_t                RIP_emergencySizeInBytes,
                 void                * pMemory,
                 SwLeMONITORCALLBACK * pfnMonitor)
{
  rip_exit_status = 0;

  InitRipMemory(RIP_maxAddressSpaceInBytes, RIP_workingSizeInBytes,
                RIP_emergencySizeInBytes, pMemory) ;

  if ( !SwStartupCommon( pfnMonitor) )
  {
    return FALSE;
  }

  /* Create the semaphores that will synchronize the application thread
   * with the RIP thread.
   */
  if ((app_blocking_sema = PKCreateSemaphore(0)) == NULL) {
    SkinMonitorf( "Failed to create semaphore, errno=%d\n", errno ) ;
    SwStopCommon();
    return FALSE ;
  }

  if ((rip_blocking_sema = PKCreateSemaphore(0)) == NULL) {
    SkinMonitorf( "Failed to create semaphore, errno=%d\n", errno ) ;
    SwStopCommon();
    return FALSE ;
  }

  if (! StartRIPOnNewThread() ) {
    SwStopCommon();
    return FALSE;
  }

  /* Block on this semaphore until the RIP has booted sufficiently */
  PKWaitOnSemaphore(app_blocking_sema);

  if (rip_exit_status != 0) {
    SwStopCommon();
    return (FALSE);
  }
  return (TRUE);
}

/**
 * \brief Request the RIP to stop.
 */
int32 SwLeStop(void)
{
  static uint8* pQuitPS = (uint8*) "$printerdict /superstop dup put systemdict begin quit" ;

  int32 exitCode;

  if (controlData.ripState == EXITING)
  {
    /* RIP has already stopped. */
    exitCode = 0;
  } else {
    if (SwLeJobStart(strlen_uint32((char*) pQuitPS), pQuitPS, NULL) ) {
      SwLeWaitForRIPThreadToExit();
    }

    /* Only get the exit code after the thread has terminated. */
    exitCode = SwLeExitCode();
  }

  SwStopCommon();

  return exitCode;
}


void SwLeWaitForRIPThreadToExit(void)
{
  WaitForRIPThreadToExit();
}


void SwLeShutdown(void)
{
  SwDllShutdown();
}


void SwLeSetTickleTimerFunctions(SwStartTickleTimerFn * pfnSwStartTickleTimer,
                                 SwStopTickleTimerFn * pfnSwStopTickleTimer)
{
  setTickleTimerFunctions( pfnSwStartTickleTimer, pfnSwStopTickleTimer );
}

void SwLeSetRipExitFunction(SwLeRIPEXITCALLBACK * pfnRipExit)
{
  controlData.pfnRipExit = pfnRipExit;
}

void SwLeSetRipRebootFunction(SwLeRIPREBOOTCALLBACK * pfnRipReboot)
{
  controlData.pfnRipReboot = pfnRipReboot;
}

HqBool SwLeJobStart(uint32             cbBuffer,
                    uint8              * pBuffer,
                    void               * pJobContext )
{
  if (controlData.ripState >= EXITING)
    return FALSE;

  if (controlData.appState != RIPSTART_CALLED &&
      controlData.appState != JOBEND_CALLED)
  {
    /* The application is calling things in the wrong order */
    HQFAIL( "Unexpected appState in SwLeJobStart" );
    return FALSE;
  }
  controlData.fConfigJobFailed = FALSE;
  controlData.appState = JOBSTART_CALLED;
  controlData.cbBuffer = (int32)cbBuffer;
  controlData.pBuffer = pBuffer;
  controlData.pJobContext = pJobContext;

  PYieldToRIP();

  if (controlData.ripState == PROCESSING_JOB_ERROR || controlData.fConfigJobFailed )
  {
    /* Update state variables as for job end, because client won't be calling SwLeJobEnd() */
    controlData.appState = JOBEND_CALLED;
    controlData.ripState = PROCESSING_JOB_END;
    return FALSE;
  }

  return TRUE;
}



void SwLeSetRasterCallbacks(SwLeRASTERSTRIDE *pfnRasterStride,
                            SwLeRASTERREQUIREMENTS *pfnRasterRequirements,
                            SwLeRASTERDESTINATION *pfnRasterDestination,
                            SwLeRASTERCALLBACK *pfnRaster)
{
  controlData.pfnRasterStride = pfnRasterStride;
  controlData.pfnRasterRequirements = pfnRasterRequirements;
  controlData.pfnRasterDestination = pfnRasterDestination;
  controlData.pfnRaster = pfnRaster;
}



HqBool SwLePs(uint32 cbBuffer, uint8 * pBuffer)
{
  if (controlData.ripState >= EXITING)
    return FALSE;

  if (controlData.appState != JOBSTART_CALLED &&
      controlData.appState != PS_CALLED)
  {
    /* The application is calling things in the wrong order */
    HQFAIL( "Unexpected appState in SwLePs" );
    return FALSE;
  }
  controlData.appState = PS_CALLED;

  controlData.cbBuffer = (int32)cbBuffer;
  controlData.pBuffer = pBuffer;

  PYieldToRIP();

  if (controlData.ripState == PROCESSING_JOB_ERROR || controlData.fConfigJobFailed )
    return FALSE;

  return TRUE;
}


HqBool SwLeJobEnd(void)
{
  if (controlData.ripState >= EXITING)
    return TRUE;

  if (controlData.appState != JOBSTART_CALLED &&
      controlData.appState != PS_CALLED)
  {
    /* The application is calling things in the wrong order */
    HQFAIL( "Unexpected appState in SwLeJobEnd" );
    return FALSE;
  }
  controlData.appState = JOBEND_CALLED;

  controlData.cbBuffer = -1;

  PYieldToRIP();

  return !controlData.fConfigJobFailed;
}

void* SwLeGetDeviceHandle( uint8 *pszDevice )
{
  /* The skin handle is actually just the DEVICELIST* in disguise. The reverse
     cast is performed by SwLeGetIntDevParam() etc. */
  return (void*) SwFindDevice( pszDevice );
}

HqBool SwLeGetIntDevParam(void *pDeviceHandle, uint8 *pszParamName, int32 *pInt)
{
  DEVICELIST *pDev = pDeviceHandle;
  if ( pDev == NULL )
  {
    return FALSE;
  }
  else
  {
    DEVICEPARAM param = { 0 };
    int32 paramResult;
    theDevParamName( param ) = pszParamName;
    theDevParamNameLen( param ) = strlen_int32( (char*) pszParamName );
    paramResult = (*theIGetParam( pDev ))( pDev, &param );
    if ( paramResult == ParamAccepted && param.type == ParamInteger )
    {
      (*pInt) = param.paramval.intval;
      return TRUE;
    }
    else
    {
      return FALSE;
    }
  }
}

HqBool SwLeGetBoolDevParam(void *pDeviceHandle, uint8 *pszParamName,
                           HqBool *pBool)
{
  DEVICELIST *pDev = pDeviceHandle;
  if ( pDev == NULL )
  {
    return FALSE;
  }
  else
  {
    DEVICEPARAM param = { 0 };
    int32 paramResult;
    theDevParamName( param ) = pszParamName;
    theDevParamNameLen( param ) = strlen_int32( (char*) pszParamName );
    paramResult = (*theIGetParam( pDev ))( pDev, &param );
    if ( paramResult == ParamAccepted && param.type == ParamBoolean )
    {
      (*pBool) = param.paramval.boolval;
      return TRUE;
    }
    else
    {
      return FALSE;
    }
  }
}

HqBool SwLeGetFloatDevParam(void *pDeviceHandle, uint8 *pszParamName,
                            float *pFloat)
{
  DEVICELIST *pDev = pDeviceHandle;
  if ( pDev == NULL )
  {
    return FALSE;
  }
  else
  {
    DEVICEPARAM param = { 0 };
    int32 paramResult;
    theDevParamName( param ) = pszParamName;
    theDevParamNameLen( param ) = strlen_int32( (char*) pszParamName );
    paramResult = (*theIGetParam( pDev ))( pDev, &param );
    if ( paramResult == ParamAccepted && param.type == ParamFloat )
    {
      (*pFloat) = param.paramval.floatval;
      return TRUE;
    }
    else
    {
      return FALSE;
    }
  }
}

HqBool SwLeGetStringDevParam(void *pDeviceHandle, uint8 *pszParamName,
                             uint8 **ppStr, int32 *pStrLen)
{
  DEVICELIST *pDev = pDeviceHandle;
  if ( pDev == NULL )
  {
    return FALSE;
  }
  else
  {
    DEVICEPARAM param = { 0 };
    int32 paramResult;
    theDevParamName( param ) = pszParamName;
    theDevParamNameLen( param ) = strlen_int32( (char*) pszParamName );
    paramResult = (*theIGetParam( pDev ))( pDev, &param );
    if ( paramResult == ParamAccepted && param.type == ParamString )
    {
      (*ppStr) = param.paramval.strval;
      (*pStrLen) = param.strvallen;
      return TRUE;
    }
    else
    {
      return FALSE;
    }
  }
}

HqBool SwLeProcessingError( void )
{
  return controlData.fConfigJobFailed
         || controlData.ripState == PROCESSING_JOB_ERROR;
}

void SwLeProcessingPs( void )
{
  controlData.ripState = PROCESSING_PS;
}

void SwLeProcessingJobEnd( void )
{
  controlData.ripState = PROCESSING_JOB_END;
}

void SkinExit(int32 n, uint8 *text )
{
  rip_exit_status = n;

  controlData.ripState = EXITING;

  if ( controlData.pfnRipExit )
  {
    (controlData.pfnRipExit)(n, text);
  }
  else
  {
    if ( n != 0 && text != NULL )
      SkinMonitorf( "exit RIP (%s)\n", (char*)text );
  }

#ifdef LESEC
  SwSecShutdown();
#endif
}

void SkinReboot(void)
{
  if ( controlData.pfnRipReboot )
  {
    (controlData.pfnRipReboot)();
  }
  else
  {
    SkinMonitorf( "Warning: RIP reboot called\n" );
  }
}


int32 SwLeExitCode(void)
{
  return rip_exit_status;
}


void SkinMonitorl(int32 cbData, uint8 * pszMonitorData)
{
  KCallMonitorCallback(cbData, pszMonitorData) ;
}

void SkinMonitorf(const char *pszFormat, ...)
{
  va_list vlist;

  va_start( vlist, pszFormat );
  SkinVMonitorf( pszFormat, vlist );
  va_end( vlist );
}

void SkinVMonitorf(const char *pszFormat, va_list vlist)
{
#define MONITOR_MESSAGE_BUFSIZE 2048
  uint8 szMsg[ MONITOR_MESSAGE_BUFSIZE ];
  int cCh;
  char *pszTruncWarning = " \nWarning: Previous monitor message truncated.\n";

  cCh = vswncopyf( szMsg, MONITOR_MESSAGE_BUFSIZE, (uint8*) pszFormat, vlist );

  if ( cCh >= MONITOR_MESSAGE_BUFSIZE )
  {
    KCallMonitorCallback( MONITOR_MESSAGE_BUFSIZE, szMsg );
    KCallMonitorCallback( strlen_int32( pszTruncWarning ),
                          (uint8*) pszTruncWarning );
  }
  else if ( cCh > 0 )
  {
    KCallMonitorCallback( cCh, szMsg );
  }
  /* message discarded if vswncopyf() returns <= 0 */
}

/* ------------------------------ External Functions ----------------------------*/

int32 KGetConfigAvailable(void)
{
  if (controlData.ripState == PROCESSING_PS)
  {
    /* The RIP is asking for the next job already. The previous one
     * must have errored.  Finish the job off for the application.
     */
    controlData.ripState = PROCESSING_JOB_ERROR;
    PYieldToApp();

    if (controlData.appState != JOBEND_CALLED)
      return -1;

    controlData.ripState = PROCESSING_JOB_START;

    return 0;
  }

  /* Wait for app to provide some data */
  PYieldToApp();

  return ( controlData.cbBuffer == -1 ) ? 0 : controlData.cbBuffer;
}


HqBool KGetConfigData(int32 cbMax, uint8 ** ppData, uint32 * pcbData)
{
  if (controlData.ripState != INITIALISING &&
      controlData.ripState != PROCESSING_JOB_START &&
      controlData.ripState != PROCESSING_JOB_END)
  {
    /* the RIP has called back on an unexpected API. */
    HQFAIL( "Unexpected ripState in KGetConfigData" );
    return FALSE;
  }

  if (controlData.ripState == INITIALISING ||
      controlData.ripState == PROCESSING_JOB_END)
  {
    controlData.ripState = WAITING_FOR_JOB_START;

    /* We should only get here after the app has provided the
     * first chunk of config data, i.e. after the yield in
     * KGetConfigAvailable() has returned.
     */

    if (controlData.appState != JOBSTART_CALLED || controlData.cbBuffer == 0)
    {
      /* the app is out of step with the RIP, or no data is available */
      return FALSE;
    }
    controlData.ripState = PROCESSING_JOB_START;
  }

  if (controlData.cbBuffer <= 0)
  {
    /* Either the app has started the job but did not pass in any config data,
     * or the RIP has consumed all the provided config data.
     */
    controlData.ripState = PROCESSING_PS;

    return FALSE;
  }
  else
  {
    /* pass back no more than cbMax bytes */
    if (cbMax > controlData.cbBuffer)
      cbMax = controlData.cbBuffer;

    *ppData = controlData.pBuffer;
    *pcbData = cbMax;

    /* flag that this config data has been consumed */
    controlData.pBuffer += cbMax;
    controlData.cbBuffer -= cbMax;

    controlData.ripState = PROCESSING_JOB_START;

    return TRUE;
  }
}


HqBool KGetJobData(int32 cbMax, uint8 ** ppData, uint32 * pcbData)
{
  if (controlData.ripState != PROCESSING_JOB_START &&
      controlData.ripState != PROCESSING_PS)
  {
    /* this is really a fatal error - we've got out of step */
    HQFAIL( "Unexpected ripState in KGetJobData" );
    return FALSE;
  }
  controlData.ripState = PROCESSING_PS;

  for (;;)
  {
    if (controlData.cbBuffer == 0)
    {
      /* Either the app has started the job, but not given us any data yet, or
       * the RIP has consumed some data previously given by a SwLePs call. Yield
       * back to the application so that another SwLePs call can hand data over.
       */
      PYieldToApp();

      if (controlData.appState != PS_CALLED &&
          controlData.appState != JOBEND_CALLED)
      {
        return FALSE;
      }
    }
    else if (controlData.cbBuffer == -1)
    {
      /* the job's finished */
      controlData.ripState = PROCESSING_JOB_END;

      return FALSE;
    }
    else
    {
      /* pass back no more than cbMax bytes */
      if (cbMax > controlData.cbBuffer)
        cbMax = controlData.cbBuffer;

      *ppData = controlData.pBuffer;
      *pcbData = cbMax;

      /* flag that this data has been consumed */
      controlData.pBuffer += cbMax;
      controlData.cbBuffer -= cbMax;

      controlData.ripState = PROCESSING_PS;

      return TRUE;
    }
  }
}

HqBool KCallRasterCallback(RasterDescription * pRasterDescription, uint8 * pBuffer)
{
  /*
   * If we are recombining then the ripState can be PROCESSING_JOB_END
   * as recombine takes over control of when to output the page.
   */
  if (controlData.ripState != PROCESSING_PS &&
      controlData.ripState != PROCESSING_JOB_END)
    return FALSE;

  /* hand the raster data over to the application */
  if (controlData.pfnRaster != NULL) {
    int32 result ;
    SwLeProbe(SW_TRACE_KCALLRASTERCALLBACK, SW_TRACETYPE_ENTER, (intptr_t)0);
    result = controlData.pfnRaster(controlData.pJobContext, pRasterDescription, pBuffer);
    SwLeProbe(SW_TRACE_KCALLRASTERCALLBACK, SW_TRACETYPE_EXIT, (intptr_t)0);
    return result ;
  }

  return TRUE;
}


int32 KCallRasterStride(uint32 *puStride)
{
  if (controlData.pfnRasterStride != NULL)
  {
    return controlData.pfnRasterStride(controlData.pJobContext, puStride);
  }

  return 0;
}

int32 KCallParamCallback(SwLeParamCallback *skin_hook, const void *param)
{
  int32 result ;

  HQASSERT(skin_hook != NULL, "No skin hook for parameter") ;

  SwLeProbe(SW_TRACE_KCALLPARAMCALLBACK, SW_TRACETYPE_ENTER, (intptr_t)param);
  result = (*skin_hook)(controlData.pJobContext, param);
  SwLeProbe(SW_TRACE_KCALLPARAMCALLBACK, SW_TRACETYPE_EXIT, (intptr_t)param);

  return result ;
}

HqBool KParamSetCallback(PARAM params[], size_t n_params,
                         const char *paramname, int32 paramtype,
                         SwLeParamCallback *pfnParamCallback)
{
  size_t i ;

  HQASSERT(params != NULL, "No parameters to set callback") ;
  HQASSERT(paramname != NULL, "No parameter name") ;

  for (i = 0; i < n_params; ++i ) {
    HQASSERT(params[i].name != NULL, "Parameter is not named") ;
    if ( strcmp(params[i].name, paramname) == 0 ) {
      if ( paramtype != params[i].type )
        return FALSE ;

      params[i].skin_hook = pfnParamCallback ;
      return TRUE ;
    }
  }

  return FALSE ;
}

int32 KCallRasterRequirements(RASTER_REQUIREMENTS * pRasterRequirements,
                              int32 fRenderingStarting)
{
  int32 result = 0;

  if (controlData.pfnRasterRequirements != NULL) {
    SwLeProbe(SW_TRACE_KCALLRASTERREQUIREMENTSCALLBACK, SW_TRACETYPE_ENTER, (intptr_t)0);
    result = controlData.pfnRasterRequirements(controlData.pJobContext,
                                               pRasterRequirements,
                                               fRenderingStarting);
    SwLeProbe(SW_TRACE_KCALLRASTERREQUIREMENTSCALLBACK, SW_TRACETYPE_EXIT, (intptr_t)0);
    if ( result < 1 )
      pRasterRequirements->handled = TRUE;
  } else
    pRasterRequirements->handled = TRUE;

  return result;
}


int32 KCallRasterDestination(RASTER_DESTINATION * pRasterDestination,
                             int32 nFrameNumber)
{
  int32 result = 0;

  if (controlData.pfnRasterDestination != NULL) {
    SwLeProbe(SW_TRACE_KCALLRASTERDESTINATIONCALLBACK, SW_TRACETYPE_ENTER, (intptr_t)0);
    result = controlData.pfnRasterDestination(controlData.pJobContext,
                                              pRasterDestination,
                                              nFrameNumber);
    SwLeProbe(SW_TRACE_KCALLRASTERDESTINATIONCALLBACK, SW_TRACETYPE_EXIT, (intptr_t)0);
    if ( result < 1 )
      pRasterDestination->handled = TRUE;
  } else
    pRasterDestination->handled = TRUE;

  return result;
}


void KCallMonitorCallback(int32 cbData, uint8 * pszMonitorData)
{
  if (controlData.pfnMonitor != NULL) {
    HqBool do_lock = (controlData.ripState >= INITIALISING &&
                      controlData.ripState <= EXITING) ;
    if ( do_lock )
      pthread_mutex_lock(&monitor_mtx) ;

    controlData.pfnMonitor((uint32)cbData, pszMonitorData);

    if ( do_lock )
      pthread_mutex_unlock(&monitor_mtx) ;
  }
}


void KSwLeStop(void)
{
  /* Release app semaphore */
  if (app_blocking_sema != NULL) {
    PKSignalSemaphore(app_blocking_sema);
  }
}


/* Set system error record level */
HqBool KSetSystemErrorLevel(int32 errlevel )
{
  if (errlevel < 0 || errlevel > 2)
    return FALSE;
  controlData.nErrorLevel = errlevel ;
  return TRUE;
}

int32 KGetSystemErrorLevel(void)
{
  return controlData.nErrorLevel ;
}

/* ------------------------------ Internal Functions ----------------------------*/

/**
 * \brief This function should only be called by the main application
 * thread when it wants to yield to the RIP thread.
 */
static void PYieldToRIP(void)
{
  HQASSERT(app_blocking_sema != NULL, "No application semaphore");
  HQASSERT(rip_blocking_sema != NULL, "No RIP semaphore");

  /* Wake the RIP up */
  PKSignalSemaphore(rip_blocking_sema);

  /* Block on this semaphore until the RIP comes around again for the next job */
  PKWaitOnSemaphore(app_blocking_sema);
}


/**
 * \brief This function should only be called by the RIP thread when
 * it wants to yield to the main application thread.
 */
static void PYieldToApp(void)
{
  HQASSERT(app_blocking_sema != NULL, "No application semaphore");
  HQASSERT(rip_blocking_sema != NULL, "No RIP semaphore");

  /* wake the application up  */
  PKSignalSemaphore(app_blocking_sema);

  /* And make the RIP got to sleep */
  PKWaitOnSemaphore(rip_blocking_sema);
}

static void SwStopCommon(void)
{
  if (app_blocking_sema != NULL) {
    PKDestroySemaphore(app_blocking_sema);
    app_blocking_sema = NULL;
  }
  if (rip_blocking_sema != NULL) {
    PKDestroySemaphore(rip_blocking_sema);
    rip_blocking_sema = NULL;
  }
  PKSemaFinish();

  (void)SwDeregisterHandlers(error_handlers, NUM_ARRAY_ITEMS(error_handlers)) ;

  progevts_finish();

  (void)SwDeregisterHandlers(tl_probe_handlers,
                             NUM_ARRAY_ITEMS(tl_probe_handlers)) ;

  /* Destroy monitor mutex */
  (void) pthread_mutex_destroy(&monitor_mtx) ;

  (void)pthread_key_delete(skindevices_key);

  controlData.ripState = EXITED;
}

static HqBool SwStartupCommon( SwLeMONITORCALLBACK *pfnMonitor )
{
  controlData.appState = RIPSTART_CALLED;
  controlData.ripState = INITIALISING;
  controlData.pfnMonitor = pfnMonitor;

  /* Create TLS key for last error for all skin devices. */
  if ( pthread_key_create(&skindevices_key, NULL) != 0 ) {
    fprintf(stderr, "Can't init devices key\n") ;
    return FALSE ;
  }
  skindevices_set_last_error(DeviceNoError);

  /* Initialise monitor mutex */
  if (pthread_mutex_init(&monitor_mtx, NULL)) {
    fprintf(stderr, "Can't init monitor mutex\n") ;
    return FALSE ;
  }

#ifdef LESEC
  {
    SwSecInitStruct secData;

    SecInit( &secData );

    SwSecInit( &secData, MemGetArena() );
  }
#endif

  begin_event_probes() ;

  /* Override RIP's built-in job state reporting until we've finished the
     startup jobs. */
  if (!progevts_init()) {
    SkinMonitorf("Cannot start progress system.\n");
    return FALSE;
  }

  /* Register error handlers at default priority so embedded skin can
     override them. */
  if ( SwRegisterHandlers(error_handlers,
                          NUM_ARRAY_ITEMS(error_handlers)) != SW_RDR_SUCCESS ) {
    progevts_finish() ;
    end_event_probes() ;
    SkinMonitorf("Cannot start error monitoring.\n");
    return FALSE;
  }

  return TRUE;
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
void init_C_globals_filedev(void) ;
void init_C_globals_pgbdev(void) ;
void init_C_globals_ramdev(void) ;
void init_C_globals_ripthread(void) ;
#ifdef HAS_RLE
void init_C_globals_scrndev(void) ;
#endif
#ifdef METRO
void init_C_globals_xpsconfstate(void) ;
#endif

static void init_C_runtime_skinkit(void)
{
  app_blocking_sema = NULL;
  rip_blocking_sema = NULL;
  rip_exit_status = 0 ;

  init_C_globals_filedev() ;
  init_C_globals_pgbdev() ;
  init_C_globals_ramdev() ;
  init_C_globals_ripthread() ;
#ifdef HAS_RLE
  init_C_globals_scrndev() ;
#endif
#ifdef METRO
  init_C_globals_xpsconfstate() ;
#endif
}

