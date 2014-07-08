/** \file
 * \ingroup psloop
 *
 * $HopeName: SWv20!src:control.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Control loop for PostScript interpreter.
 */

#include "core.h"
#include "control.h"

#include "mm.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "fileio.h"
#include "swdevice.h"
#include "swoften.h"
#include "devices.h"
#include "monitor.h"
#include "swcopyf.h"
#include "swpdfout.h"
#include "fontcache.h" /* BLIMIT */
#include "fontparam.h" /* FontParams */
#include "namedef_.h"
#include "debugging.h"
#include "taskh.h"
#include "timing.h"

#include "pscontext.h"
#include "dicthash.h"
#include "bitblts.h"
#include "matrix.h"
#include "ndisplay.h"
#include "display.h"
#include "graphics.h"
#include "routedev.h"
#include "dlstate.h"
#include "params.h"
#include "psvm.h"
#include "saves.h"
#include "devops.h"
#include "scanner.h"
#include "stacks.h"
#include "fileops.h"
#include "render.h"
#include "execops.h"
#include "stackops.h"
#include "statops.h"
#include "dictops.h"
#include "miscops.h"
#include "metrics.h"
#include "jobmetrics.h"
#include "startup.h"
#include "often.h"
#include "ripmulti.h" /* multi_nthreads */
#include "mlock.h" /* THREAD_LOCAL_DEFINE */
#include "gstate.h"
#include "asyncps.h"
#include "progupdt.h"
#include "typeops.h" /* do_cvs */
#include "trap.h"

#include "gschtone.h"
#include "gs_cache.h"   /* for coc_purge */
#include "ripdebug.h"   /* for register_ripvar */

#include "rcbcntrl.h"
#include "pclAttrib.h"

#include "lowmem.h" /* mm_should_regain_reserves */
#include "eventapi.h"
#include "corejob.h"
#include "gu_chan.h"
#include "interrupts.h"
#include "swevents.h"
#include "riptimeline.h"
#include "surface.h" /* surface deselect method */

#include <stdarg.h>


static void interpreter_loop(ps_context_t *pscontext);
static Bool interpreter_misc(ps_context_t *pscontext, OBJECT *theo, int32 tagstype);

OBJECT *topDictStackObj = NULL;
Bool execStackSizeNotChanged = FALSE;
uint8* zipArchiveName = NULL;

OBJECT errobject;
Bool allow_interrupt = TRUE;

Bool rip_work_in_progress = TRUE ; /* Reset at the end of bootstrap. */
static Bool rip_was_in_progress = FALSE ; /* Used to detect unencapsulated jobs. */


int gc_safety_level;


/* See control.h for a description of these globals. */
int32 ps_interpreter_level = 0 ;
Bool exiting_rip_cleanly = FALSE ;


Bool dosomeaction = FALSE;


void break_handler(NAMECACHE* name) ;

static void handleerror(void) ;

/* ---------------------------------------------------------------------- */
/* If you uncomment the following, it will print the name of every operator
 * as the interpreter loop executes it. Please don't remove this very
 * useful bit of debugging
 */

#if defined( DEBUG_BUILD )

unsigned debug_lowmemory_count = 0;

enum {
  DEBUG_CONTROL_LITERAL   = 1,
  DEBUG_CONTROL_EXECUTION = 2,
  DEBUG_CONTROL_OPERATORS = 4,
  DEBUG_CONTROL_LOOKUP    = 8,
  DEBUG_CONTROL_JOB       = 256,
  DEBUG_CONTROL_NOBREAK   = 0x20000000,
  DEBUG_CONTROL_NOSPACE   = 0x40000000,
  DEBUG_CONTROL_LEFT      = DEBUG_CONTROL_NOSPACE,
  DEBUG_CONTROL_RIGHT     = DEBUG_CONTROL_NOBREAK,
  DEBUG_CONTROL_INTERNAL  = DEBUG_CONTROL_NOBREAK|DEBUG_CONTROL_NOSPACE
} ;

int32 debug_control_switch = 0;         /* dynamic debugging switch */
int32 debug_control_width = 0;          /* count of chars on current line */

#define DEBUG_CONTROL_WIDTH 80          /* width to format output */

#undef theIOpCall
#define theIOpCall(v) iopcall(v)

static void debug_control_output(uint8 *clist, int32 len, int32 flags)
{
  if ( ((debug_control_switch & ~DEBUG_CONTROL_INTERNAL) & flags) != 0 ) {
    debug_control_width += len;
    if ( debug_control_width >= DEBUG_CONTROL_WIDTH &&
         (flags & DEBUG_CONTROL_NOBREAK) == 0 ) {
      monitorf(( uint8 *)"\n");
      debug_control_width = len ;
    }
    monitorf(( uint8 *)"%.*s", len, clist);
    if ( (flags & DEBUG_CONTROL_NOSPACE) == 0 ) {
      monitorf(( uint8 *)" ");
      debug_control_width += 1 ;
    }
  }
}

OPFUNCTION iopcall( OPERATOR * op )
{
  debug_control_output(op->opname->clist, op->opname->len,
                       DEBUG_CONTROL_OPERATORS) ;
  return op->opcall;
}

static void debug_literal(OBJECT *olist)
{
  /* Literal object debugging */
  if ( (debug_control_switch & DEBUG_CONTROL_LITERAL) == 0 )
    return ;

  switch ( oType(*olist) ) {
    NAMECACHE *nptr ;
    FILELIST *file ;
    uint8 number[50] ;
  case ONOTHING:
    debug_control_output((uint8 *)"--nothing--", 11, DEBUG_CONTROL_LITERAL) ;
    break ;
  case OINTEGER:
    swcopyf(number, (uint8 *)"%d", oInteger(*olist)) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  case OREAL:
    swcopyf(number, (uint8 *)"%f", oReal(*olist)) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  case OINFINITY:
    debug_control_output((uint8 *)"--infinity--", 11, DEBUG_CONTROL_LITERAL) ;
    break ;
  case OBOOLEAN:
    if ( oBool(*olist) )
      debug_control_output((uint8 *)"true", 4, DEBUG_CONTROL_LITERAL) ;
    else
      debug_control_output((uint8 *)"false", 5, DEBUG_CONTROL_LITERAL) ;
    break ;
  case OOPERATOR:
    debug_control_output((uint8 *)"--", 2, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;
    debug_control_output(theIOpName(oOp(*olist))->clist,
                         theIOpName(oOp(*olist))->len,
                         DEBUG_CONTROL_OPERATORS|DEBUG_CONTROL_INTERNAL) ;
    debug_control_output((uint8 *)"--", 2, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;
    break ;
  case OMARK:
    debug_control_output((uint8 *)"[", 1, DEBUG_CONTROL_LITERAL) ;
    break ;
  case ONULL:
    debug_control_output((uint8 *)"null", 4, DEBUG_CONTROL_LITERAL) ;
    break ;
  case OFONTID:
    swcopyf(number, (uint8 *)"--FID(%d)--", oFid(*olist)) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  case ONAME:
    nptr = oName(*olist) ;
    debug_control_output((uint8 *)"/", 1, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;
    debug_control_output(theICList(nptr), nptr->len,
                         DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;
    break ;
  case OSAVE:
    swcopyf(number, (uint8 *)"--save(%d)--", save_level(oSave(*olist))) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  case ODICTIONARY:
    debug_control_output((uint8 *)"--dict<<", 8, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;

    if ( oDict(*olist) == oDict(systemdict) ) {
      swcopyf(number, (uint8 *)"systemdict") ;
    } else if ( oDict(*olist) == oDict(userdict) ) {
      swcopyf(number, (uint8 *)"userdict") ;
    } else if ( oDict(*olist) == oDict(globaldict) ) {
      swcopyf(number, (uint8 *)"globaldict") ;
    } else if ( oDict(*olist) == oDict(internaldict) ) {
      swcopyf(number, (uint8 *)"internaldict") ;
    } else {
      swcopyf(number, (uint8 *)"%p", oDict(*olist)) ;
    }

    debug_control_output(number, strlen_int32((char *)number),
                         DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_INTERNAL) ;
    debug_control_output((uint8 *)">>--", 4, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;
    break ;
  case OSTRING:
    debug_control_output((uint8 *)"(", 1, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;
    debug_control_output(oString(*olist), theLen(*olist),
                         DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_INTERNAL) ;
    debug_control_output((uint8 *)")", 1, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;
    break ;
  case OFILE:
    file = oFile(*olist) ;
    debug_control_output((uint8 *)"--file(", 7, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;
    debug_control_output(theICList(file), file->len,
                         DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_INTERNAL) ;
    debug_control_output((uint8 *)")--", 3, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;

    break ;
  case OARRAY:
  case OPACKEDARRAY:
    swcopyf(number, (uint8 *)"%p", oArray(*olist)) ;
    debug_control_output((uint8 *)"--array[", 8, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_INTERNAL) ;
    debug_control_output((uint8 *)"]--", 3, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;
    break ;
  case OGSTATE:
    swcopyf(number, (uint8 *)"--gstate(%d,%d)--",
            oGState(*olist)->gType, oGState(*olist)->gId) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  case OINDIRECT:
    swcopyf(number, (uint8 *)"--PDF(%d,%d)--", oXRefID(*olist),
            theGen(*olist)) ;
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  case OLONGSTRING:
    debug_control_output((uint8 *)"(", 1, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_LEFT) ;
    debug_control_output(theLSCList(*oLongStr(*olist)),
                         theLSLen(*oLongStr(*olist)),
                         DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_INTERNAL) ;
    debug_control_output((uint8 *)")", 1, DEBUG_CONTROL_LITERAL|DEBUG_CONTROL_RIGHT) ;
    break ;
  case OFILEOFFSET:
    if (theLen(*olist)) {
      swcopyf(number, (uint8 *)"--fileoffset (0x%x%08x)--", ((unsigned)theLen(*olist)),
            (unsigned)olist->_d1.vals.fileoffset_low) ;
    } else {
      swcopyf(number, (uint8 *)"--fileoffset (%u)--",
            (unsigned)olist->_d1.vals.fileoffset_low) ;
    }
    debug_control_output(number, strlen_int32((char *)number), DEBUG_CONTROL_LITERAL) ;
    break ;
  }
}
#else
#define debug_control_output(clist, len, flags) EMPTY_STATEMENT()
#define debug_literal(olist) EMPTY_STATEMENT()
#endif  /* DEBUG_BUILD */


/* ---------------------------------------------------------------------- */
/** Announce that a job is configuring. */
Bool configjobinternal_(ps_context_t *pscontext)
{
  corecontext_t *context ;

#ifdef DEBUG_BUILD
  if ( (debug_control_switch & DEBUG_CONTROL_JOB) != 0 )
    monitorf((uint8 *)"CONFIGJOBINTERNAL\n") ;
#endif

  HQASSERT(!rip_was_in_progress, "RIP in progress when configuring job") ;
  rip_work_in_progress = rip_was_in_progress = TRUE ;

  context = ps_core_context(pscontext) ;
  corejob_begin(context->page);

  return TRUE;
}

/** Announce that the job is a real job. */
Bool realjobinternal_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext) ;
  DL_STATE *page = context->page ;

#ifdef DEBUG_BUILD
  if ( (debug_control_switch & DEBUG_CONTROL_JOB) != 0 )
    monitorf((uint8 *)"REALJOBINTERNAL\n") ;
#endif

  corejob_running(page) ;
  corejob_page_begin(page) ;

  return TRUE ;
}

/** Announce that a job failed. Consumes all of the values on the stack down
    to the mark, leaves a boolean indicating whether the error handling
    actions should be ignored. If the event handler either marks the event as
    failing, or leaves an existing failure mark alone, the core job status is
    marked as failing. */
Bool failjobinternal_(ps_context_t *pscontext)
{
  OBJECT *theo;
  corecontext_t *context ;
  DL_STATE *page ;
  corejob_t *job ;
  int32 nparams, command_len = 0 ;
  NAMECACHE *name ;
  SWMSG_ERROR error ;
  uint8 command[128] ; /* .error limits strings to 128 */

  if ( (nparams = num_to_mark()) < 0 )
    return error_handler(UNMATCHEDMARK) ;

  /* Above the mark, we have:
     failure (boolean)
     errorname (string/name)
     command (string/name/operator)
     ...

     we also want:
     errorinfo
     filename
     line number
  */

  if ( nparams < 3 )
    return error_handler(RANGECHECK) ;

  context = ps_core_context(pscontext) ;
  page = context->page ;
  if ( page == NULL || page->job == NULL )
    return error_handler(VMERROR);
  job = page->job ;
  VERIFY_OBJECT(job, CORE_JOB_NAME) ;

  if ( pdfout_enabled( ) )
    pdfout_seterror(context->pdfout_h, FALSE);

  error.timeline = page->job->timeline ;
  error.page_number = page->pageno ;

  /* Failure boolean */
  theo = stackindex(nparams - 1, &operandstack) ;
  if ( oType(*theo) != OBOOLEAN )
    return error_handler(TYPECHECK) ;
  error.fail_job = oBool(*theo) ;

  error.suppress_handling = FALSE ;

  /* errorname */
  theo = stackindex(nparams - 2, &operandstack) ;
  name = NULL ;
  switch ( oType(*theo) ) {
  case OSTRING:
    error.error_name.length = theLen(*theo) ;
    error.error_name.string = oString(*theo) ;
    if ( theLen(*theo) <= MAXPSNAME )
      name = lookupname(oString(*theo), theLen(*theo)) ;
    break ;
  case ONAME:
    name = oName(*theo) ;
    error.error_name.length = theINLen(name) ;
    error.error_name.string = theICList(name) ;
    break ;
  default:
    error.error_name.length = 0 ;
    error.error_name.string = (uint8 *)"" ;
    break ;
  }

  /* command */
  theo = stackindex(nparams - 3, &operandstack) ;
  if ( !do_cvs(theo, command, sizeof(command), &command_len) )
    return FALSE ;

  error.command.string = command ;
  error.command.length = (size_t)command_len ;

  /* PS error number reverse-engineered from name. */
  if ( name != NULL &&
       theINameNumber(name) >= NAME_dictfull &&
       theINameNumber(name) <= NAME_undefinedresource ) {
    error.error_number = theINameNumber(name) - NAME_dictfull + DICTFULL ;
  } else {
    error.error_number = 0 ;
  }

  /** \todo ajcd 2011-04-19: Temporarily, until I fill in detail: */
  error.detail = NULL ;

  if ( SwEvent(SWEVT_INTERPRET_ERROR, &error, sizeof(error)) >= SW_EVENT_ERROR )
    HQTRACE(TRUE,
            ("Error from SwEvent(SWEVT_INTERPRET_ERROR, 0x%p, %u)",
             &error, sizeof(error))) ;

#ifdef DEBUG_BUILD
  if ( (debug_control_switch & DEBUG_CONTROL_JOB) != 0 )
    monitorf((uint8 *)"ERROR(failure=%s,suppress=%s,number=%d,name=%.*s,command=%.*s)\n",
             error.fail_job ? "true" : "false",
             error.suppress_handling ? "true" : "false",
             error.error_number,
             error.error_name.length, error.error_name.string,
             error.command.length, error.command.string) ;
#endif

  if ( error.fail_job )
    job->failed = TRUE ;

  npop(nparams, &operandstack) ; /* Leave mark on stack */

  /* Convert mark to boolean indicating handler suppression */
  Copy(theTop(operandstack), error.suppress_handling ? &tnewobj : &fnewobj) ;

  return TRUE ;
}

/** Announce that a job is ending. Takes a boolean on the stack, indicating
    if the job stopped abnormally. If true, the job is marked as failing. */
Bool endjobinternal_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  corecontext_t *context ;
  Bool result = TRUE ;

  if ( isEmpty( operandstack ) )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  context = ps_core_context(pscontext) ;

  rip_work_in_progress = FALSE ;

  /* The RIP starts with the boot-up "job" automatically in progress.
     There's an extra false setjobinprogress at the end of the boot-up code
     to note that it's complete. We use a separate variable to detect this
     case, and also handle the missing end job from unencapsulated jobs. */
  if ( rip_was_in_progress ) {
    DL_STATE *page = context->page ;
    task_t *complete = NULL ;

    VERIFY_OBJECT(page->job, CORE_JOB_NAME) ;

    /* It is unsafe to finish a job with references to the erase and ready
       tasks from the DL, so delete and reconstruct a new page around
       the corejob_end() and corejob_create() calls. */
    dl_clear_page(page) ;

    if ( page->pageno != 0 )
      corejob_page_end(page, FALSE /* No content on last page */) ;

    if ( oBool(*theo) )
      page->job->failed = TRUE ;

    /* Create an asynchronous join task for the old job group, and make
       it the previous task of the new task group. This may flush the
       previous job immediately, if the asynchronous join task could not
       be created. Release the page's current job object. */
    corejob_end(page, &complete, oBool(*theo)) ;

    if ( !corejob_create(page, complete) ) {
      if ( complete != NULL )
        task_release(&complete) ;

      /* Flush any pages from previous jobs on failure. This will release
         the page-job references, freeing the job object. When the job
         object is released, it will join the group containing the completion
         task. */
      (void)dl_pipeline_flush(1, oBool(*theo)) ;
      result = FALSE ;
      HQFAIL("Untested: couldn't create core job object") ;
    }

#ifdef DEBUG_BUILD
    if ( (debug_control_switch & DEBUG_CONTROL_JOB) != 0 )
      monitorf((uint8 *)"ENDJOBINTERNAL(%s)\n", oBool(*theo) ? "true" : "false") ;
#endif

    /** \todo ajcd 2011-07-10: Does it matter that we didn't set
        rip_was_in_progress if this fails on the first try? */
    if ( !dl_begin_page(page) )
      return FALSE ;
  }

  pop(&operandstack) ;

  rip_was_in_progress = rip_work_in_progress ;

  return result;
}

/* ---------------------------------------------------------------------- */

void start_interpreter(void)
{
  char buf1[MAX_MM_PRETTY_MEMORY_STRING_LENGTH] ;
  char buf2[MAX_MM_PRETTY_MEMORY_STRING_LENGTH] ;
  char buf3[MAX_MM_PRETTY_MEMORY_STRING_LENGTH] ;
  corecontext_t *context = get_core_context_interp() ;

  error_clear_context(context->error);

#if defined( DEBUG_BUILD )
  register_ripvar(NAME_debug_control, OINTEGER, &debug_control_switch) ;
#endif

  if ( ! dostart()) {
    (void)dispatch_SwExit(1, "Failed to start interpreter" ) ;
    return ;
  }

  if ( ! rip_ready() ) {
    (void)dispatch_SwExit(1, "Failed post-boot initialisation" ) ;
    return ;
  }

  device_type_report_ticklefuncs();

  monitorf(UVM("Initial RIP virtual memory: %s used, %s commit limit, %s reserved\n"),
           mm_pretty_memory_string((uint32)(mps_arena_committed( mm_arena ) / 1024), buf1),
           mm_pretty_memory_string((uint32)(mps_arena_commit_limit( mm_arena ) / 1024), buf2),
           mm_pretty_memory_string((uint32)(mps_arena_reserved( mm_arena ) / 1024), buf3)) ;

  /* make first call on interpreter */
  ps_interpreter_level = 0 ;
  exiting_rip_cleanly = FALSE ;

  (void)interpreter( theStackSize( executionstack ) + 1, NULL ) ;
  HQASSERT(ps_interpreter_level == 0, "ps_interpreter_level is not zero") ;
}


/* stop_interpreter - stop the interpreter */
void stop_interpreter(void)
{
  corecontext_t *context = get_core_context_interp() ;
  task_group_t *group ;
  DL_STATE *page ;

  HQASSERT(context, "No core context for thread calling stop_interpreter") ;
  HQASSERT(IS_INTERPRETER(),
           "stop_interpreter must be called on interpreter thread") ;

  page = context->page ;

  /* Clear the input page */
  dl_clear_page(page) ;

  /* Flush any remaining pages that are still rendering out of the pipeline */
  (void)dl_pipeline_flush(1, TRUE /*no messages*/) ;
  if ( page->job != NULL )
    corejob_end(page, NULL, TRUE /*no messages*/) ;

  /* Cancel any remaining tasks in flight. */
  group = task_group_root() ;
  task_group_cancel(group, INTERRUPT) ;
  task_group_release(&group) ;

  /* Deselect the surface. */
  if ( page->surfaces != NULL ) {
    if ( page->surfaces->deselect != NULL )
      (page->surfaces->deselect)(&page->sfc_inst, FALSE) ;
    page->surfaces = NULL ;
    page->sfc_inst = NULL ;
  }

  /* Release the last input page's job and rasterstyle. */
  HQASSERT(page->next_task == NULL && page->all_tasks == NULL,
           "Didn't clear last page's tasks") ;
  if ( page->hr != NULL )
    guc_discardRasterStyle(&page->hr) ;

  dostop();
}


/* ---------------------------------------------------------------------- */
/** The main PostScript interpreter entry point.
 *
 *  n           the number of items on the execution stack to execute
 * *trap_exit   a null pointer if an exit continues to exit beyond
 *              the caller; otherwise, on return the Boolean pointed
 *              to will be set to TRUE if an exit was found and FALSE
 *              otherwise
 */
Bool interpreter(int32 n, Bool *trap_exit)
{
  OBJECT * o1, * o2 ;
  int32 i ;
  OBJECT exitobject = {
    {{ONULL|EXECUTABLE, ISNOTVM|ISLOCAL|SAVEMASK, ISINTERPRETEREXIT}},
    {{(void *)(intptr_t)-1}}
  } ;
  corecontext_t *context = get_core_context_interp() ;

  /*
   * Catch any calls by renderer or compositing threads back into
   * the interpreter. Eventually can just be an assert, as it should not
   * be possible. But at the moment, it can and does happen as MTC is
   * under development.
   */
  /**
   * \todo BMJ 31-Mar-10 : stop all possibilities of this happening.
   */
  HQASSERT(context, "Interpreter has no context");
  if ( !(context->is_interpreter) ) {
    monitorf(( uint8 * )"Compositing thread invoking the interpreter\n");
    HQFAIL("Compositing thread invoking the interpreter");
    return error_handler(UNREGISTERED);
  }

  HQASSERT(!error_signalled_context(context->error), "We shouldn't be starting a new interpreter in an error state!") ;
  HQASSERT(!context->between_operators, "Low-memory handlers shouldn't get here.");

  /* insert an "interpreter return" object into the execution stack: first
   * move the top few objects up one, then replace the original with the new
   * object.
   */
  o2 = theTop( executionstack ) ;
  if ( !push( o2, & executionstack ) )
    return FALSE ;

  o1 = stackindex( 1, & executionstack ) ;
  for ( i = 2 ; i <= n ; i++ ) {
    o2 = o1;
    o1 = stackindex( i, & executionstack ) ;
    Copy( o2, o1 ) ;
  }
  OCopy(*o1, exitobject) ;

  /* hang on to the errobject */
  if ( !push( &errobject, &temporarystack ) )
     return FALSE ;

  /* execute until we hit the special object */
  ps_interpreter_level++ ;
  interpreter_loop(context->pscontext) ;
  ps_interpreter_level-- ;

  if (! exiting_rip_cleanly) {
    /* restore the errobject */
    Copy( &errobject, theTop( temporarystack )) ;
    pop( &temporarystack ) ;

    o1 = theTop( executionstack ) ;
    i = oInteger(*o1) ;
    if ( i < 0 ) {
      pop( & executionstack ) ;
      return TRUE ;
    }
    else if ( i == NAME_exit && trap_exit ) {
      pop( & executionstack ) ;
      *trap_exit = TRUE ;
      return error_handler(NOT_AN_ERROR) ;
    }
    else {
      /* we hit a stop or a non-trapped exit: redo the stop or exit and return
         FALSE to tell the caller to tidy up and abort */
      theTags(*o1) = OOPERATOR | EXECUTABLE ;
      HQASSERT( i >= 0 && i < OPS_COUNTED ,
                "interpreter creating segv operator" ) ;
      oOp(*o1) = & system_ops[ i ] ;
      if ( trap_exit ) {
        *trap_exit = FALSE ;
      }
      return error_handler(NOT_AN_ERROR) ;
    }
    /* This should not be reached. */
  }

  return TRUE ;
}


/** Helper macro to call an operator, maintaining the recursion counters, and
    handle any errors. */
#define call_operator(opobj, pscontext) MACRO_START \
  ++gc_safety_level; \
  if ( ! theIOpCall(oOp(opobj))(pscontext) )  \
    handleerror(); \
  gc_safety_level = saved_gc_safety_level; \
  dl_safe_recursion = saved_dl_safe_recursion; \
MACRO_END


/**
   This procedure is the main control loop for the interpreter.  It controls
   all the operations that happen by using the following algorithm :

    1. Obtain the type of the object on the top of the execution stack.
    2. If it is a file or string, then scan its characters according
       to the PostScript syntax rules.
    3. If it is a name, then look  it up in the context of the current
       dictionary stack, and push its value onto the execution stack.
    4. If it is an operator, then simply execute its built-in value.
    5. If it is an array, then obtain its objects one-by-one, and
       push them onto the execution stack.
    6. If it is a control construct then perform the appropriate action.
    7. Go back to number 1.
*/

/* ---------------------------------------------------------------------- */
static void interpreter_loop(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  SYSTEMPARAMS *systemparams = context->systemparams;
  register int32 tagstype ;
  register int32 length ;
  Bool looping = FALSE ;
  register OBJECT *theo , *olist , *errptr ;
  register NAMECACHE *nptr ;
  register FILELIST *flptr ;
  int32 cacheloop ;
  OBJECT *cacheolist ;
  int saved_dl_safe_recursion = dl_safe_recursion ;
  int saved_gc_safety_level = gc_safety_level;

  UNUSED_PARAM(corecontext_t *, context); /* only in asserts */
  errptr = ( & errobject ) ;

  /* Make sure we don't recurse indefinitely, by imposing a limit on the
   * recursion depth. Note that we deliberately fall through into the
   * for(ever) loop to process the error.
   */
  if ( ps_interpreter_level >= systemparams->MaxInterpreterLevel ) {
    ( void )error_handler( LIMITCHECK ) ;
    handleerror() ;
  }

  probe_begin(SW_TRACE_INTERPRET_LEVEL, ps_interpreter_level) ;

  while (! exiting_rip_cleanly) {
    SwOftenSafe();
    HQASSERT(!context->error->old_error, "Uncleared error after op.");

    if ( mm_memory_is_low || dosomeaction )
      if ( !handleNormalAction() )
        handleerror();

    theo = theTop( executionstack ) ;
    Copy( errptr , theo ) ;

    tagstype = oType(*theo) ;
    if ( tagstype == OARRAY ) {
CTRL_ARRAY :
CTRL_PACKEDARRAY :
      if ( theLen(*theo) > 0 ) {
        looping = TRUE ;
        do {
          SwOftenSafe();
          HQASSERT(!context->error->old_error, "Uncleared error after op.");

          olist = oArray(*theo) ;
          ++oArray(*theo) ;
          if ( --theLen(*theo) == 0 ) {  /* Used for tail-end recursion */
            debug_control_output((uint8 *)"}", 1, DEBUG_CONTROL_EXECUTION) ;
            pop( & executionstack ) ;
            looping = FALSE ;
          }
          if ( ! oExecutable(*olist)) {
            debug_literal(olist) ;
            if ( ! push( olist , & operandstack )) {
              handleerror() ;
              looping = FALSE ;
            }
          }
          else {
            tagstype = oType(*olist) ;
            if ( tagstype == ONAME ) {  /* Executable name. */
              cacheolist = olist ;
/* if ( inNameCacheSlot[ ?? ] ... */
#if 1
              nptr = oName(*cacheolist) ;

#if defined(DEBUG_BUILD)
              /* [51291] Break on marked executable names */
              if (nptr->flags & NCFLAG_BREAK)
                break_handler(nptr) ;
#endif

              debug_control_output(theICList(nptr), nptr->len, DEBUG_CONTROL_LOOKUP|DEBUG_CONTROL_LEFT) ;
              debug_control_output((uint8 *)" -> ", 4, DEBUG_CONTROL_LOOKUP|DEBUG_CONTROL_INTERNAL) ;
              olist = nptr->dictobj ;
              if ( olist ) {
                if ( oInteger(olist[ -2 ]) ) { /* On dictstack*/
                  olist = nptr->dictval ;
                  goto CTRL_ARRAY_PRE_END_NAME ;
                }
              }
#endif
              Copy( errptr , cacheolist ) ;
              if (( olist =
                   fast_extract_hash( topDictStackObj , cacheolist ))
                  != NULL )
                goto CTRL_ARRAY_PRE_END_NAME ;
              HQASSERT(!newerror_context(context->error),
                       "fast_extract_hash should not have set an error") ;
              length = theStackSize( dictstack ) - 1 ;
              if ( theISaveLangLevel( workingsave ) >= 2 )
                length -= 1 ;
              for ( cacheloop = 1 ; cacheloop < length ; ++cacheloop ) {
                if (( olist =
                     fast_extract_hash(stackindex( cacheloop , & dictstack ) ,
                                       cacheolist )) != NULL )
                  goto CTRL_ARRAY_PRE_END_NAME ;
                HQASSERT(!newerror_context(context->error),
                         "fast_extract_hash should not have set an error") ;
              }
              if (( olist = fast_user_extract_hash( cacheolist )) != NULL )
                goto CTRL_ARRAY_PRE_END_NAME ;
              HQASSERT(!newerror_context(context->error),
                       "fast_user_extract_hash should not have set an error") ;
              if ( theISaveLangLevel( workingsave ) >= 2 ) {
                if (( olist = fast_global_extract_hash( cacheolist )) != NULL )
                  goto CTRL_ARRAY_PRE_END_NAME ;
                HQASSERT(!newerror_context(context->error),
                         "fast_global_extract_hash should not have set an error") ;
              }
              if ( NULL == ( olist = fast_sys_extract_hash( cacheolist ))) {
                HQASSERT(!newerror_context(context->error),
                         "fast_sys_extract_hash should not have set an error") ;
                ( void )error_handler( UNDEFINED ) ;
                handleerror() ;
                looping = FALSE ;
                goto CTRL_ARRAY_END_NAME ;
              }
CTRL_ARRAY_PRE_END_NAME :
              if ( ! oExecutable(*olist)) {
                debug_literal(olist) ;
                if ( ! push( olist , & operandstack )) {
                  handleerror() ;
                  looping = FALSE ;
                }
              }
              else {
                tagstype = oType(*olist) ;

                if ( tagstype == OARRAY ) {
#if defined( DEBUG_BUILD )
                  /* copy the NAME object on the stack, and turn the original
                     into a "function call debug context" */
                  OBJECT * tmpo ;
                  if (!push(cacheolist, &executionstack))
                    handleerror() ;
                  tmpo = theTop( executionstack ) ;
                  theTags(*tmpo) ^= ONAME ^ ONULL ;
                  theLen(*tmpo) = ISPROCEDURE ;
#endif
                  debug_control_output((uint8 *)"{", 1, DEBUG_CONTROL_EXECUTION) ;
                  looping = FALSE ;
                  if ( ! push( olist , & executionstack ))
                    handleerror() ;
                }
                else if ( tagstype == OOPERATOR ) {
                  execStackSizeNotChanged = TRUE ;
                  Copy( errptr , olist ) ;
                  call_operator(*olist, pscontext);
                  looping &= execStackSizeNotChanged ;
                }
                else
                  switch ( tagstype ) {
                  case OFILE :
                    currfileCache = NULL ;
                    /* this is a valid check to do even on a dead filter, because
                       even if the filter has been reused, it has the same direction */
                    if ( ! isIInputFile(oFile(*olist)) ) {
                      ( void )error_handler( INVALIDACCESS ) ;
                      handleerror() ;
                      break ;
                    }
                  case OPACKEDARRAY :
                    debug_control_output((uint8 *)"{", 1, DEBUG_CONTROL_EXECUTION) ;
                    /* FALLTHRU */
                  case OSTRING : case ONAME :
                    looping = FALSE ;
                    if ( ! push( olist , & executionstack ))
                      handleerror() ;
                    break ;
                  case ONULL :
                    break ;
                  default:
                    if ( ! push( olist , & operandstack ))
                      handleerror() ;
                    break ;
                  }
              }
CTRL_ARRAY_END_NAME :
              EMPTY_STATEMENT() ;
            }
            else if ( tagstype == OOPERATOR ) { /* Executable name. */
              execStackSizeNotChanged = TRUE ;
              Copy( errptr , olist ) ;
              call_operator(*olist, pscontext);
              looping &= execStackSizeNotChanged ;
            }
            else if (( tagstype == OARRAY ) || ( tagstype == OPACKEDARRAY )) {
              if ( ! push( olist , & operandstack )) {
                handleerror() ;
                looping = FALSE ;
              }
            }
            else {
              switch ( tagstype ) {
              case OFILE :
                currfileCache = NULL ;
                /* this is a valid check to do even on a dead filter, because
                   even if the filter has been reused, it has the same direction */
                if ( ! isIInputFile(oFile(*olist)) ) {
                  ( void )error_handler( IOERROR ) ;
                  handleerror() ;
                  looping = FALSE ;
                  break ;
                }
              case OSTRING :
                looping = FALSE ;
                if ( ! push( olist , & executionstack ))
                  handleerror() ;
                break ;
              case ONULL :
                break ;
              default:
                if ( ! push( olist , & operandstack )) {
                  handleerror() ;
                  looping = FALSE ;
                }
              }
            }
          }
        } while ( looping ) ;
      }
      else
        pop( & executionstack ) ;
    }

    else if ( tagstype == ONOTHING ) {
      call_operator(*theo, pscontext);
    }

    else if ( tagstype == OPACKEDARRAY )
      goto CTRL_PACKEDARRAY ;

    else if ( tagstype == ONAME ) {
CTRL_NAME :
/* if ( inNameCacheSlot[ ?? ] ... */
#if 1
      nptr = oName(*theo) ;

#if defined(DEBUG_BUILD)
      /* [51291] Break on marked executable names */
      if (nptr->flags & NCFLAG_BREAK)
        break_handler(nptr) ;
#endif

      debug_control_output(theICList(nptr), nptr->len, DEBUG_CONTROL_EXECUTION|DEBUG_CONTROL_LEFT) ;
      debug_control_output((uint8 *)" -> ", 4, DEBUG_CONTROL_LOOKUP|DEBUG_CONTROL_INTERNAL) ;
      olist = nptr->dictobj ;
      if ( olist ) {
        if ( oInteger(olist[ -2 ]) ) { /* On dictstack*/
          olist = nptr->dictval ;
          goto CTRL_NAME_PRE_END_NAME ;
        }
      }
#endif
      Copy( errptr , theo ) ;
      if (( olist = fast_extract_hash( topDictStackObj , theo )) != NULL )
        goto CTRL_NAME_PRE_END_NAME ;
      HQASSERT(!newerror_context(context->error), "fast_extract_hash should not have set newerror") ;
      length = theStackSize( dictstack ) - 1 ;
      if ( theISaveLangLevel( workingsave ) >= 2 )
        length -= 1 ;
      for ( cacheloop = 1 ; cacheloop < length ; ++cacheloop ) {
        if (( olist =
             fast_extract_hash(stackindex( cacheloop , & dictstack ) , theo ))
            != NULL )
          goto CTRL_NAME_PRE_END_NAME ;
        HQASSERT(!newerror_context(context->error), "fast_extract_hash should not have set newerror") ;
      }
      if (( olist = fast_user_extract_hash( theo )) != NULL )
        goto CTRL_NAME_PRE_END_NAME ;
      HQASSERT(!newerror_context(context->error), "fast_user_extract_hash should not have set newerror") ;
      if ( theISaveLangLevel( workingsave ) >= 2 ) {
        if (( olist = fast_global_extract_hash( theo )) != NULL )
          goto CTRL_NAME_PRE_END_NAME ;
        HQASSERT(!newerror_context(context->error), "fast_global_extract_hash should not have set newerror") ;
      }
      if ( NULL == ( olist = fast_sys_extract_hash( theo ))) {
        HQASSERT(!newerror_context(context->error), "fast_sys_extract_hash should not have set newerror") ;
        pop( & executionstack ) ;
        ( void )error_handler( UNDEFINED ) ;
        handleerror() ;
        goto CTRL_NAME_END_NAME ;
      }
CTRL_NAME_PRE_END_NAME :
      if ( ! oExecutable(*olist)) {
        debug_literal(olist) ;
        pop( & executionstack ) ;
        if ( ! push( olist , & operandstack ))
          handleerror() ;
      }
      else {
        tagstype = oType(*olist) ;

        if ( tagstype == OARRAY ) {
#if defined( DEBUG_BUILD )
          /* copy the NAME object on the stack, and turn the original into
             a "function call debug context" */
          if (!push(theo, &executionstack))
            handleerror() ;
          theTags(*theo) ^= ONAME ^ ONULL ;
          theLen(*theo) = ISPROCEDURE ;
          theo = theTop( executionstack ) ;
#endif
          debug_control_output((uint8 *)"{", 1, DEBUG_CONTROL_EXECUTION) ;
          Copy( theo , olist ) ;
          goto CTRL_ARRAY ;
        }
        else if ( tagstype == OOPERATOR ) {
          pop( & executionstack ) ;
          Copy( errptr , olist ) ;
          call_operator(*olist, pscontext);
        }
        else
          switch ( tagstype ) {
          case OFILE :
            currfileCache = NULL ;
            /* this is a valid check to do even on a dead filter, because
               even if the filter has been reused, it has the same direction */
            if ( ! isIInputFile(oFile(*olist))) {
              ( void )error_handler( INVALIDACCESS ) ;
              handleerror() ;
              break ;
            }
          case OPACKEDARRAY :
            debug_control_output((uint8 *)"{", 1, DEBUG_CONTROL_EXECUTION) ;
            /* FALLTHRU */
          case OSTRING : case ONAME :
            Copy( theo , olist ) ;
            break ;
          case ONULL :
            pop( & executionstack ) ;
            break ;
          default:
            pop( & executionstack ) ;
            if ( ! push( olist , & operandstack ))
              handleerror() ;
            break ;
          }
      }
CTRL_NAME_END_NAME:
      EMPTY_STATEMENT() ;
    }

    else if ( tagstype == OFILE ) {
      flptr = oFile(*theo) ;
      if ( isIOpenFileFilter( theo, flptr )) {

        do {
          HQASSERT(!context->error->old_error, "Uncleared error after op.");

          if ( isIEof( flptr )) {
            uint16 filter_id = theLen(*theo) ;
            pop( &executionstack ) ; /* invalidates theo */
            looping = FALSE ;
            currfileCache = NULL ;

            if ( isIOpenFileFilterById( filter_id , flptr )) /* needed for filters */
              if ( (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT )) {
                (void) error_handler( IOERROR ) ;
                handleerror() ;
                break ;
              }
          }
          else {
            SwOftenSafe ();

            if ( ! f_scanner( flptr , systemparams->ParseComments, TRUE )) {
              handleerror() ;
              break ;
            }

            if ( scannedObject ) {
              scannedObject = FALSE ;
              olist = theTop( operandstack ) ;
              if ( oExecutable(*olist)) {
                tagstype = oType(*olist) ;
                if ( tagstype == ONAME ) {
                  looping = FALSE ;
                  if ( ! push( olist , & executionstack )) {
                    handleerror() ;
                    pop( & operandstack ) ;
                    break ;
                  }
                  pop( & operandstack ) ;
                  theo = theTop( executionstack ) ;
                  Copy( errptr , theo ) ;
                  goto CTRL_NAME ;
                }
                else {
                  switch ( tagstype ) {
                  case OFILE :
                    currfileCache = NULL ;
                    /* this is a valid check to do even on a dead filter, because
                       even if the filter has been reused, it has the same direction */
                    if ( ! isIInputFile( oFile(*olist))) {
                      ( void )error_handler( IOERROR ) ;
                      handleerror() ;
                      looping = FALSE ;
                      break ;
                    }
                  case OSTRING :
                  case OOPERATOR :
                    looping = FALSE ;
                    if ( ! push( olist , & executionstack ))
                      handleerror() ;
                  case ONULL :
                    pop( & operandstack ) ;
                    break ;
                  }
                }
              }
            }
            else if ( scannedBinSequence ) {
              /* scannedObject is FALSE, check for scannedBinSequence */
              scannedBinSequence = FALSE ;
              looping = FALSE ;
              olist = theTop( operandstack ) ;
              if ( ! push( olist , & executionstack )) {
                handleerror() ;
                pop( & operandstack ) ;
                break ;
              }
              pop( & operandstack ) ;
              theo = theTop( executionstack ) ;
              Copy( errptr , theo ) ;
              goto CTRL_ARRAY ;
            }
            else if ( isIEof( flptr )) {
              uint16 filter_id = theLen(*theo) ;
              pop( &executionstack ) ; /* invalidates theo */
              looping = FALSE ;
              currfileCache = NULL ;

              if ( isIOpenFileFilterById( filter_id, flptr ))   /* needed for filters */
                if ( (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT )) {
                  (void) error_handler( IOERROR ) ;
                  handleerror() ;
                  break ;
                }
            }
          }
        } while ( looping ) ;
      }
      else {
        currfileCache = NULL ;
        pop( & executionstack ) ;
      }
    }
    else if ( tagstype == ONULL &&
              theLen(*theo) == ISINTERPRETEREXIT ) {
      probe_end(SW_TRACE_INTERPRET_LEVEL, ps_interpreter_level) ;
      return ;
    } else {
      if ( ! interpreter_misc(pscontext, theo, tagstype) )
        handleerror() ;
      dl_safe_recursion = saved_dl_safe_recursion;
    }
  }

  probe_end(SW_TRACE_INTERPRET_LEVEL, ps_interpreter_level) ;
}

/* ---------------------------------------------------------------------- */
static Bool interpreter_misc(ps_context_t *pscontext, OBJECT *theo, int32 tagstype)
{
  corecontext_t *context = pscontext->corecontext ;
  register int32  length ;
  register uint8  *clist ;
  register OBJECT *olist ;
  int32 lineno, where ;
  Bool looping ;
  OPFUNCTION function ;

  UNUSED_PARAM(corecontext_t *, context); /* only in asserts */

  switch ( tagstype ) {
  case OOPERATOR :
    function = theIOpCall(oOp(*theo)) ;
    pop( & executionstack ) ;
    return (*function)(pscontext) ;
    /*break ;*/

  case OSTRING :
    length = theLen(*theo) ;
    clist = oString(*theo) ;

    if ( length == 0 ) {
      pop( & executionstack ) ;
      return TRUE ;
    }

    lineno = 1 ;
    where = 0 ;

    looping = TRUE ;
    do {
      SwOftenSafe() ;
      HQASSERT(!context->error->old_error, "Uncleared error after op.");

      if ( ! s_scanner( clist , length , & where , &lineno , FALSE , TRUE ))
        return FALSE ;

      if ( where >= length ) {
        looping = FALSE ;
        pop( & executionstack ) ;
      }

      if ( scannedObject ) {
        scannedObject = FALSE ;
        olist = theTop( operandstack ) ;
        if ( oExecutable(*olist)) {
          switch ( oType(*olist) ) {
          case OFILE :
            currfileCache = NULL ;
            /* this is a valid check to do even on a dead filter, because
               even if the filter has been reused, it has the same direction */
            if ( ! isIInputFile(oFile(*olist)))
              return error_handler( INVALIDACCESS ) ;
          case OSTRING :
          case ONAME :
          case OOPERATOR :
            if ( where < length ) {
              theLen(*theo) = CAST_TO_UINT16(length - where) ;
              oString(*theo) = clist + where ;
            }
            if ( ! push( olist , & executionstack ))
              return FALSE ;
            pop( & operandstack ) ;
            return TRUE ;
          case ONULL :
            pop( & operandstack ) ;
            break ;
          }
        }
      }
      else if ( scannedBinSequence ) {
        scannedBinSequence = FALSE ;
        olist = theTop( operandstack ) ;
        if ( where < length ) {
          theLen(*theo) = CAST_TO_UINT16(length - where) ;
          oString(*theo) = clist + where ;
        }
        if ( ! push( olist , & executionstack )) {
            return FALSE ;
        }
        pop( & operandstack ) ;
        return TRUE ;
      }
    } while ( looping ) ;
    break ;

  case OMARK :
    if ( theLen(*theo) ) {
      allow_interrupt = FALSE ;       /* superstop disables interrupt */
      clear_interrupts();
    }
    pop( & executionstack ) ;
    return push( & fnewobj , & operandstack ) ;

  case ONULL :
    switch ( theLen(*theo) ) {
    case ISPATHFORALL:
      return xpathforall((PATHFORALL *)(void*)oOther(*theo)) ;

    case ISPROCEDURE:
      pop( &executionstack ) ;
      break ;

    default:
      HQFAIL( "Unexpected ONULL in interpreter_misc()" ) ;
      pop( & executionstack ) ; /* For safety */
      break ;
    }
    break ;

  default :
    HQFAIL( "Unexpected object type in interpreter_misc()" ) ;
    pop( & executionstack ) ; /* For safety */
  }
  return TRUE ;
}


Bool interpreter_clean(OBJECT *execme, ...)
{
  va_list args ;
  OBJECT *operand ;
  Bool result = TRUE ;
  int32 osize = theStackSize(operandstack) ;
  int32 dsize = theStackSize(dictstack) ;
  corecontext_t *context = get_core_context_interp() ;
  ps_context_t *pscontext = context->pscontext ;

  va_start(args, execme) ;

  while ( result && (operand = va_arg(args, OBJECT *)) != NULL ) {
    result = push(operand, &operandstack) ;
  }

  va_end(args) ;

  if ( result ) {
    if ( oType(*execme) == OFILE )
      currfileCache = NULL ;

    result = (push(execme, &executionstack) &&
              interpreter(1, NULL) &&
              (theStackSize(operandstack) >= osize || error_handler(STACKUNDERFLOW)) &&
              (theStackSize(dictstack) >= dsize || error_handler(STACKUNDERFLOW))) ;
  }

  if ( !result ) { /* Cleanup stacks to original level */
    if ( theStackSize(operandstack) > osize )
      npop(theStackSize(operandstack) - osize, &operandstack) ;
    while ( theStackSize(dictstack) > dsize && end_(pscontext) )
      EMPTY_STATEMENT() ;
  }

  return result ;
}

/* ---------------------------------------------------------------------- */

static int32 did = 1 ;

Bool setDistillEnable( Bool enable )
{
  if ( pdfout_enabled() != enable ) {
    corecontext_t *context = get_core_context_interp() ;

    if ( context->systemparams->PDFOut ) {
      if ( enable ) {
        thegsDistillID(*gstateptr) = did++ ;

        if ( ! pdfout_beginjob(context) )
          return FALSE ;
      }
      else {
        thegsDistillID(*gstateptr) = 0 ;

        if ( ! pdfout_endjob(context) )
          return FALSE ;
      }
    }
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */


mm_cost_t low_mem_handling_cost(mm_context_t *mm_context, Bool limit_to_pp)
{
  const mm_cost_t partial_paint_cost = { memory_tier_partial_paint, 1.0f };
  mm_cost_t current_cost = mm_allocation_cost(mm_context);

  return limit_to_pp ? mm_cost_min(current_cost, partial_paint_cost)
                     : current_cost;
}


Bool low_mem_regain_reserves_with_pp(Bool *got_reserves,
                                     corecontext_t *context)
{
  Bool no_error;
  Bool tried_pp;

  if ( async_action_level > 0 )
    return TRUE; /* Unsafe in an async action @@@@ move to handlers */
  for ( tried_pp = FALSE ; ; tried_pp = TRUE ) {
    mm_cost_t handling_cost =
      low_mem_handling_cost(context->mm_context, !tried_pp);

    do {
      context->between_operators = TRUE;
      gc_mode = gc_safety_level == 0
                ? (int)context->userparams->VMReclaim : -2;
      HQTRACE(debug_lowmemory, ("mm_regain_reserves %d", gc_mode));
      no_error = mm_regain_reserves(got_reserves, context, handling_cost);
      gc_mode = -2;
      context->between_operators = FALSE;
      if ( !no_error )
        return FALSE;
      if ( *got_reserves )
        return TRUE;
    } while ( task_wait_for_memory() );

    if ( tried_pp )
      break; /* Can't do anything more */
    if ( partial_paint_allowed(context) ) {
      HQTRACE(debug_lowmemory, ("Partial paint"));
      if ( !rip_to_disk(context) )
        return FALSE;
    }
  }
  return TRUE;
}


/* Note: get_core_context() is currently a heavyweight function so only call if
 * absolutely need to.
 */
Bool handleNormalAction(void)
{
  dosomeaction = FALSE ;

  if (!interrupts_clear(allow_interrupt)) {
    return (report_interrupt(allow_interrupt));
  }

  SwOftenSafe();

  if ( mm_memory_is_low ) {
    Bool got_reserves;
    return low_mem_regain_reserves_with_pp(&got_reserves,
                                           get_core_context_interp());
  } else if ( mm_gc_threshold_exceeded() ) {
    corecontext_t *context = get_core_context_interp();

    if ( gc_safety_level == 0
         && context->userparams->VMReclaim > -2 ) {
      if ( !garbage_collect(context->userparams->VMReclaim != -1, TRUE) )
        return FALSE ;
    } else
      dosomeaction = TRUE;
  }
  return TRUE;
}


Bool low_mem_handle_between_ops(Bool *retry, Bool *tried_pp,
                                corecontext_t *context,
                                size_t count, memory_requirement_t* requests)
{
  Bool ok;

  HQASSERT(context->is_interpreter, "Between-ops handling outside interpreter");

  /* Do low-memory handling enabling between_operators and GC. */
  context->between_operators = TRUE;
  gc_mode = gc_safety_level == 0
            ? (int)context->userparams->VMReclaim : -2;
  HQTRACE(debug_lowmemory, ("low_mem_handle_between_ops %d", gc_mode));
  ok = low_mem_handle(retry, context, count, requests);
  gc_mode = -2;
  context->between_operators = FALSE;
  if ( !ok )
    return FALSE;

  if ( !*retry && mm_memory_is_low )
    *retry = task_wait_for_memory();

  if ( !*retry && !*tried_pp && partial_paint_allowed(context) ) {
    dl_erase_nr eraseno_before = context->page->eraseno;

    HQTRACE(debug_lowmemory, ("Partial paint"));
    if ( !rip_to_disk(context) ) {
      *tried_pp = context->page->eraseno != eraseno_before;
      return FALSE;
    }
    *retry = TRUE; *tried_pp = TRUE;
  }
  return TRUE;
}


/** \brief Handle low memory.
 *
 *   ripped_to_disk tells us whether or not we did a partial paint this time
 *   around. If caller doesn't care about this, may pass NULL ptr.
 */
int32 handleLowMemory( int32 actionNumber ,
                       int32 tryflags ,
                       Bool *ripped_to_disk)
{
  Bool no_error, retry, tried_pp = (tryflags & TRY_RIPTODISK) == 0;
  corecontext_t *context = get_core_context();
  mm_cost_t handling_cost =
    low_mem_handling_cost(context->mm_context, !tried_pp);
  memory_requirement_t request =
    { NULL, 64 * 1024, { memory_tier_ram, 0.0f }};
  request.cost = handling_cost;

  HQASSERT(actionNumber < 10000, "handleLowMemory loop too long");

  if ( async_action_level > 0 )
    return 0; /* Unsafe in an async action @@@@ move to handlers */
  SwOftenSafe();
  actionNumber++;
  no_error = low_mem_handle_between_ops(&retry, &tried_pp, context, 1, &request);
  if ( ripped_to_disk )
    *ripped_to_disk = tried_pp && (tryflags & TRY_RIPTODISK) != 0;
  return !no_error ? -1 : (retry ? actionNumber : 0);
}

/* ---------------------------------------------------------------------- */

corecontext_t *ps_core_context(const ps_context_t *pscontext)
{
  HQASSERT(pscontext != NULL, "No PostScript context") ;
  return pscontext->corecontext ;
}


void setDynamicGlobalDefaults(void)
{
  corecontext_t *context = get_core_context() ;
  SYSTEMPARAMS *systemparams = context->systemparams ;
  USERPARAMS *userparams = context->userparams ;
  FONTSPARAMS *fontparams = context->fontsparams ;
  double megas;
  double maxfontcache;
  double maxcachechars;
  double maxcachematrix;

  megas = CAST_SIZET_TO_DOUBLE( mm_working_size() / 1048576 ) ;
  HQASSERT( megas >= 8.0 ,
            "These calculations assume at least 8 MiB working size" ) ;

  /* This is aimed at matching the values intended by the previous
     incarnation of the code, but without the int32 overflows. We now
     assume that it's no longer 1985 and we always have 8MiB or
     more. Quite where these expressions come from is shrouded in
     mystery. What it boils down to is around half (for small memory
     configurations) up to three quarters of memory is available for
     the font cache to grow into, up to a hard limit of 2 2GiB. Note
     that the expressions could be simplified further (e.g. the first
     one is just "megas - 3") but they were left like this for easier
     comparison with previous branches. The only deviation from
     previous behaviour here is that we didn't used to add the second
     and third terms to the total if the working size was more than 4
     GB (not GiB, for some reason). So for large 64 bit systems, font
     cache was limited to 36% of the working size. In practice that
     was probably plenty, but in any case we now have a slightly less
     crazy curve and all that happens as a result is we get to the 2
     GiB hard limit more readily. */

  maxfontcache = BLIMIT *
    ( 0.5 * megas + 0.25 * ( megas - 4.0 ) + 0.25 * ( megas - 8.0 )) ;
  maxcachechars = CLIMIT *
    ( 0.25 * megas + 0.125 * ( megas - 4.0 ) + 0.125 * ( megas - 8.0 )) ;
  maxcachematrix = MLIMIT *
    ( 0.25 * megas + 0.125 * ( megas - 4.0 ) + 0.125 * ( megas - 8.0 )) ;

  fontparams->MaxFontCache = ( maxfontcache > MAXINT32 ) ? MAXINT32 :
    ( int32 )maxfontcache ;
  fontparams->MaxCacheChars = ( maxcachechars > MAXINT32 ) ? MAXINT32 :
    ( int32 )maxcachechars ;
  fontparams->MaxCacheMatrix = ( maxcachematrix > MAXINT32 ) ? MAXINT32 :
    ( int32 )maxcachematrix ;

  userparams->MinFontCompress = 2048 + 1024 + 512 + 256 + 128 ;
  userparams->MaxFontItem = 65536 + 32768 ;
  userparams->MaxUPathItem = userparams->MaxFontItem * 4 ;
  systemparams->MaxUPathCache = fontparams->MaxFontCache / 4 ;

  /** \todo max_simultaneous_tasks is the wrong limit to use here, we want the
      limit of simultaneously working tasks, but that's not available this early
      in the bootstrap. - PPP 2013-09-25 */
  systemparams->MaxBandMemory = (megas > 15.0 * max_simultaneous_tasks()) ?
                                   (USERVALUE)(1.5 * max_simultaneous_tasks()) :
                                   (USERVALUE)(0.1 * megas);
  systemparams->DynamicBandLimit = (max_simultaneous_tasks() + 1) / 2;
}


/* Error handling routines */
/* ----------------------- */


int32 newerror_context(error_context_t *errcontext)
{
  return errcontext->new_error ;
}


#ifdef DEBUG_BUILD

int32 olderror_context(error_context_t *errcontext)
{
  return errcontext->old_error ;
}

int32 origerror_context(error_context_t *errcontext)
{
  return errcontext->orig_error ;
}

#endif


void setup_error_name( uint8 *errname )
{
  OBJECT name = OBJECT_NOTVM_NOTHING ;

  theTags( name ) = (uint8)( ONAME | EXECUTABLE ) ;

  if ( (oName(name) = cachename(errname ,
                                (uint32)strlen((char *)errname))) == NULL ) {
    /* Setup some name so we don't crash. */
    oName(name) = system_names + NAME_unregistered ;
  }

  Copy(&errobject, &name) ;
}


Bool error_signalled_context(error_context_t *errcontext)
{
  return errcontext->old_error != FALSE;
}


static inline Bool unclearable_error(int32 error)
{
  return error == INTERRUPT || error == TIMEOUT;
}


static int32 error_save_level = 0 ;


/* Save error already signalled. Note that failures in any recursive
   interpreter() call will result in handleerror() being called for them too,
   however they will not set up the error information if the error save level is
   not zero. */
void error_save_context(error_context_t *errcontext, int32 *savederror)
{
  int32 err = errcontext->old_error ;

  HQASSERT(get_core_context()->is_interpreter, "Error saving outside interpreter");
  HQASSERT(err >= NOT_AN_ERROR && err <= UNDEFINEDRESOURCE && err != FALSE,
           "Unknown error saved") ;
  *savederror = err ;
  if ( error_save_level == 0 )
    errcontext->interrupting = unclearable_error(err)
      || (err == NOT_AN_ERROR && unclearable_error(errcontext->orig_error));
  ++error_save_level ;
  errcontext->new_error = FALSE; errcontext->old_error = FALSE;
  /* The current detail will be preserved, because errors are not handled within
     the save. */
}


void error_restore_context(error_context_t *errcontext, int32 savederror)
{
  HQASSERT(get_core_context()->is_interpreter, "Error saving outside interpreter");
  HQASSERT(error_save_level > 0, "Error restored when not saved") ;
  HQASSERT(savederror >= NOT_AN_ERROR && savederror <= UNDEFINEDRESOURCE &&
           savederror != FALSE, "Unknown error restored") ;
  if ( errcontext->new_error == NOT_AN_ERROR ) {
    /* Doing a stop out of the error now being discarded, drop it. */
    HQASSERT(oOp(*theTop(executionstack))->opname->namenumber == NAME_superstop
             || oOp(*theTop(executionstack))->opname->namenumber == NAME_stop
             || oOp(*theTop(executionstack))->opname->namenumber == NAME_exit,
             "Unknown stop type in error_restore.");
    pop(&executionstack);
  }
  if ( !unclearable_error(errcontext->old_error) )
    errcontext->old_error = savederror;
  --error_save_level ;
  if ( error_save_level == 0 && errcontext->interrupting ) {
    HQASSERT(unclearable_error(errcontext->old_error)
             || (errcontext->old_error == NOT_AN_ERROR
                 && unclearable_error(errcontext->orig_error)),
             "Not interrupting when promised");
    errcontext->interrupting = FALSE;
  }
}


/** Reset the error state after handling. */
static void error_reset_context(error_context_t *errcontext)
{
  errcontext->orig_error = errcontext->old_error;
  errcontext->new_error = FALSE; errcontext->old_error = FALSE;
  if ( error_save_level == 0 )
    /* Within a save, we keep the old detail, but never handle errors. The
       condition is OK outside interpreter, where there's never any detail. */
    errcontext->got_detail = FALSE;
}


/* Clear any errors signalled, but reraise interrupts and timeouts in the
   interpreter , so they are not lost. They should really be unclearable in all
   tasks, as they should abort all computation, but this interface doesn't offer
   any way of doing that. Other tasks will presumably terminate eventually. */
void error_clear_context(error_context_t *errcontext)
{
  if ( unclearable_error(errcontext->old_error) ) {
    /* Reraise if interpreting, and not already handling an interrupt. */
    if ( get_core_context()->is_interpreter ) {
      if ( !errcontext->interrupting )
        switch ( errcontext->old_error ) {
        case INTERRUPT: raise_interrupt(); break;
        case TIMEOUT: raise_timeout(); break;
        default: HQFAIL("Unknown unclearable error");
        }
    } else
      HQFAIL("Clearing an interrupt or timeout");
  }
  errcontext->new_error = FALSE ;
  errcontext->old_error = FALSE ;
  if ( error_save_level == 0 )
    /* Within a save, we keep the old detail, but never handle errors. The
       condition is OK outside interpreter, where there's never any detail. */
    errcontext->got_detail = FALSE;
}


void error_clear_newerror_context(error_context_t *errcontext)
{
  errcontext->new_error = FALSE ;
}


int32 error_latest_context(error_context_t *errcontext)
{
  int32 current_error = errcontext->old_error ;

  return (current_error == NOT_AN_ERROR || current_error == FALSE)
    /* This is not called, if there hasn't been an error, so origerror is set. */
    ? errcontext->orig_error
    : current_error;
}


#if defined( ASSERT_BUILD )
/** A catch-all for low level failures
 *
 * For the sake of brevity much code returned FALSE (or similar) at the
 * point of failure detection, leaving it to a parent to call error_handler
 * (e.g. the font code) making debugging harder. This routine is used by
 * the FAILURE() macro. Setting a breakpoint here catches such failures
 * at the point of detection. See FAILURE() in swerrors.h
 */
void failure_handler(void)
{
  return;
}
#endif

#if defined(DEBUG_BUILD)
/** A breakpoint for Postscript debugging
 *
 * Breakpoints can be set on executable names using the hqnsetbreakpoint
 * operator in ripdebug.c, or by manipulating the NAMECACHE flag manually.
 * Attaching a debugger breakpoint to this stub allows easier debugging of
 * procsets and Postscript jobs.
 */
void break_handler(NAMECACHE* name)
{
  UNUSED_PARAM(NAMECACHE *, name) ;   /* For reference */

  /* [51291] Call out to a GUI here to allow interactive debugging */

  return;
}
#endif


/* Basically sets up the error code, which handleerror() then deals with.  After
 * stack overflows, the stack involved is popped down.
 */
Bool error_handler(int32 errorno)
{
  corecontext_t *context = get_core_context() ;
  error_context_t *errcontext = context->error ;

  HQASSERT(errorno >= NOT_AN_ERROR && errorno <= UNDEFINEDRESOURCE && errorno != FALSE,
           "Unknown error raised") ;

  if ( context->is_interpreter ) {
    ps_context_t *pscontext = context->pscontext ;

    if ( errorno == NOT_AN_ERROR ) {
      /* Note that NOT_AN_ERROR overrides the value of any existing signalled
         error. This is because NOT_AN_ERROR is typically used when returning
         from a recursive interpreter context. Errors occurring within the
         recursive context will have called handleerror() already, NOT_AN_ERROR
         signals to the parent context that it need not call the error
         handler. If an error was signalled but ignored before calling
         interpreter(), then we don't want it to be thrown just because a
         recursive interpreter call failed and cleaned up already. (This is
         also the reason for having the save/restore signalled error functions,
         for the very few cases where we want to call interpreter() but
         preserve the error information. */
      errcontext->new_error = errorno ;
      errcontext->old_error = errorno ;
      return FALSE ;
    }

    if ( errorno == TIMEOUT )
      clear_timeout();

    if ( errorno == INTERRUPT ) {
      corejob_t *job = context->page->job;

      clear_interrupts();
      if ( job != NULL && task_group_is_cancelled(job->task_group) )
        errorno = task_group_error(job->task_group); /* propagate cancel */
    }

    if ( errorno == DISKVMERROR ) {
      monitorf(UVS("System Warning: Insufficient disk workspace\n") ) ;
      ( void )erasepage_(pscontext) ;
      errorno = VMERROR ;
    }

    /* Set error indicators. */
    errcontext->new_error = errorno ;
    if ( !errcontext->old_error )
      errcontext->old_error = errorno ;

    /* Clear any stacks as appropriate. */
    if ( errorno == EXECSTACKOVERFLOW )
      ( void )stop_(pscontext) ;
    else if ( errorno == STACKOVERFLOW )
      ( void )clear_(pscontext) ;
    else if ( errorno == DICTSTACKOVERFLOW )
      ( void )cleardictstack_(pscontext) ;
  } else {
#if defined( ASSERT_BUILD )
    switch ( errorno ) {
    case EXECSTACKOVERFLOW:
    case STACKOVERFLOW:
    case DICTSTACKOVERFLOW:
        HQFAIL( "Got an errorno in a renderer process that I don't know how to handle" ) ;
    }
#endif

    /* In a non-interpreter task, NOT_AN_ERROR is used to indicate that the
       result of some task graph is now unwanted, so we should abort the task
       graph, but be able to detect that the failure was innocuous. In the
       non-interpreter tasks, we want to allow any real error thrown to
       override the unwanted status. */
    if ( errorno > 0 || !errcontext->new_error )
      errcontext->new_error = errorno ;
    if ( errcontext->old_error <= 0 )
      errcontext->old_error = errorno ;
    /* Just return FALSE; we'll catch the error later. */
  }
  return FALSE ;
}

/** These procedures sets the error handler in motion. */
static void handleerror(void)
{
  OBJECT *theo ;
  OBJECT *errdict ;
  OBJECT n_edict = OBJECT_NOTVM_NAME(NAME_errordict, LITERAL);
  OBJECT errname = OBJECT_NOTVM_NOTHING;
  corecontext_t *context = get_core_context_interp() ;
  error_context_t *errcontext = context->error ;
  int32 err = errcontext->old_error ;

  if ( err == 0 ) {
    HQFAIL("No error set up in handleerror");
    err = UNREGISTERED ;
  }
  HQASSERT(context->is_interpreter, "handleerror called outside interpreter");
  HQASSERT(err >= NOT_AN_ERROR && err <= UNDEFINEDRESOURCE,
           "Unknown error somehow set");

  /* Forces us to break out of inner interpretation loops... */
  execStackSizeNotChanged = FALSE ;
  /* This handling of NOT_AN_ERROR is to enable us to return from recursive
   * interpreter calls that fail. */
  if ( err == NOT_AN_ERROR ) {
    error_clear_context(errcontext);
    return;
  }
  if ( error_save_level > 0 ) {
    /* If control ends up here, this must be an interpreter inside the
       save/restore.  Don't run an error handler, just unwind. */
    /** \todo With proper error contexts, we could run the handler without
        destroying the error info outside the restore. */
    stop_(context->pscontext);
    error_reset_context(errcontext);
    return;
  }
  /* Allow all low-memory handling now. */
  if ( context->mm_context != NULL ) /* can be NULL during bootstrap */
    mm_set_allocation_cost(context->mm_context, mm_cost_all);

  if ( !context->error->got_detail ) {
    Bool res = object_error_info(&onull, &onull);
    UNUSED_PARAM(Bool, res);
    HQASSERT(res, "Failed to clear error info.");
  }

  /* Push errobject on stack (but not for INTERRUPT and TIMEOUT, per RB) */
  if ( err != INTERRUPT && err != TIMEOUT ) {
    switch (oType(errobject)) {
    case ONOTHING :
      theTags( errobject ) = OOPERATOR | EXECUTABLE ;
      /* we want the number of the alias operator not the internal one */
      HQASSERT(theINameNumber(theIOpName(oOp(errobject))) >= 0 &&
               theINameNumber(theIOpName(oOp(errobject))) < OPS_COUNTED ,
               "interpreter creating segv operator" ) ;
      oOp(errobject) = & system_ops[theINameNumber(theIOpName(oOp(errobject)))];
      break ;
    case OMARK :
      theTags( errobject ) = OOPERATOR | EXECUTABLE ;
      oOp(errobject) = & system_ops[NAME_stopped];
      break ;
    case ONULL :
      theTags( errobject ) = OOPERATOR | EXECUTABLE ;
      switch ( theLen( errobject )) {
      case ISPATHFORALL :
        oOp(errobject) = & system_ops[NAME_pathforall];
        break ;
      }
      break ;
    }
    if ( !push( &errobject, &operandstack ))
      /* Clears the stack if hit the limit, so try again. */
      (void)push( &errobject, &operandstack );
  }

  errdict = fast_sys_extract_hash(&n_edict);

  theTags(errname) = ONAME | EXECUTABLE;
  oName(errname) = &system_names[NAME_dictfull + err - 1];
  if (( theo = fast_extract_hash( errdict , & errname )) != NULL )
    if ( !setup_pending_exec( theo, FALSE ))
      /* Assuming it failed to push, try again. */
      if ( !setup_pending_exec( theo, FALSE ))
        /* Would only fail twice if out of memory. The final reserve should make
           that impossible, so assert. */
        HQFAIL("Failed to exec PS error handler");

  error_reset_context(errcontext) ;
}


/** Insert a key and a value as error information into \c $error.
 *
 * This satisfies a definition in COREobjects, providing extended error
 * information for dictmatch et al.
 *
 * Sets $error/ErrorParams/errorinfo to be a two element array
 * [ thekey theval ].  Returns success flag.
 * .error moves this into $error/errorinfo when it handles the error, ensuring
 * that it will be cleared on a subsequent error.
 *
 * As a special case, if both the key and the value are ONULL, we
 * remove errorinfo from ErrorParams. This is for cases where we want
 * to provide info in case there's an error in code outside of the
 * core, like this:
 */
Bool object_error_info(const OBJECT *thekey, const OBJECT *theval)
{
  corecontext_t *context = get_core_context_interp() ;
  OBJECT n_dolerr = OBJECT_NOTVM_NAME(NAME_DollarError, LITERAL) ;
  OBJECT n_errinfo = OBJECT_NOTVM_NAME(NAME_errorinfo, LITERAL) ;
  OBJECT o = OBJECT_NOTVM_NOTHING ;
  OBJECT *errdict ;
  OBJECT *newarray ;
  Bool res;

  HQASSERT(context->is_interpreter, "set_errorinfo called from renderer process" ) ;
  /* get $error from systemdict */
  if ( NULL == (errdict = fast_sys_extract_hash( &n_dolerr))) {
    /* This can happen during bootup: just skip saving the error info
       since there's nowhere to put it. */
    return TRUE ;
  }
  /* look up /ErrorParams in $error */
  if ( NULL == (errdict = fast_extract_hash_name(errdict, NAME_ErrorParams)) ) {
    return TRUE ;
  }

  if ( oType( *thekey ) == ONULL && oType( *theval ) == ONULL ) {
    context->error->got_detail = FALSE;
    return remove_hash( errdict , &n_errinfo , TRUE ) ;
  }
  else {
    if ( NULL == (newarray = get_omemory(2)) )
      return error_handler( VMERROR );
    Copy( newarray , thekey ) ;
    Copy( newarray + 1 , theval ) ;
    theTags( o ) = OARRAY | LITERAL | UNLIMITED ;
    SETGLOBJECT(o, context) ;
    theLen( o ) = 2 ;
    oArray( o ) = newarray ;
    res = insert_hash( errdict , &n_errinfo, &o ) ;
    HQASSERT(res || newerror == VMERROR, "Couldn't insert error info");
    context->error->got_detail = TRUE;
    return res;
  }
}


Bool detail_error_handler(int32 errorno, const char *detail)
{
  corecontext_t *context = get_core_context_interp();

  if ( error_save_level > 0 ||
       !context->is_interpreter || context->error->old_error ) {
    /* If olderror is already set or an error is saved, keep that error info. */
    /** \todo TODO FIXME @@@ paulc 21526: renderers throw away error detail */
    return error_handler( errorno ) ;
  } else {
    OBJECT thestring = OBJECT_NOTVM_NOTHING ;
    int32 detail_len = strlen_int32(detail) ;
    uint8 *pstring = (uint8 *) get_gsmemory( detail_len ) ;
    if ( pstring == NULL ) {
      return error_handler( VMERROR ) ;
    }
    HqMemCpy( pstring, detail, detail_len ) ;
    theTags( thestring ) = OSTRING | UNLIMITED | LITERAL ;
    SETGLOBJECTTO(thestring, TRUE) ;
    oString(thestring) = pstring ;
    theLen( thestring ) = (unsigned short) detail_len ;
    return errorinfo_error_handler( errorno, &onull, &thestring ) ;
  }
}

/* See header for doc */
Bool vdetailf_error_handler(int32 errorno, const char *format, va_list vlist)
{
  char buffer[1000];  /* tough turnips to anyone who wants more than 1K */

  vswcopyf((uint8 *)buffer, (uint8 *)format, vlist);
  return detail_error_handler( errorno, buffer ) ;
}

/* See header for doc */
Bool detailf_error_handler(int32 errorno, const char *format, ... )
{
  Bool result ;
  va_list vlist;

  va_start( vlist, format ) ;
  result = vdetailf_error_handler( errorno , format, vlist ) ;
  va_end( vlist );
  return result ;
}


#if defined(DEBUG_BUILD)
Bool lowmemory_(ps_context_t *pscontext)
{
  OBJECT *o1, *o2 ;

  if ( theStackSize(operandstack) < 1 )
    return error_handler(STACKUNDERFLOW) ;

  o2 = theTop(operandstack) ;
  o1 = &o2[-1] ;
  if ( !fastStackAccess(operandstack) )
    o1 = stackindex(1, &operandstack) ;

  if ( oType(*o1) != OINTEGER || oType(*o2) != OINTEGER )
    return error_handler(TYPECHECK) ;

  debug_lowmemory_count = (unsigned)oInteger(*o1);
  mm_memory_is_low = debug_lowmemory_count > 0;

  if ( oInteger(*o2) != 0 ) {
    if ( partial_paint_allowed(pscontext->corecontext) ) {
      if ( !rip_to_disk(pscontext->corecontext) )
        return FALSE;
    } else
      return error_handler(LIMITCHECK);
  }

  npop(2, &operandstack) ;
  return TRUE ;
}
#endif

/** File runtime initialisation */
void init_C_globals_control(void)
{
  errobject = onothing ;
  topDictStackObj = NULL;
  execStackSizeNotChanged = FALSE;
  zipArchiveName = NULL;
  allow_interrupt = TRUE;
  rip_work_in_progress = TRUE ;
  rip_was_in_progress = FALSE ;
  ps_interpreter_level = 0 ;
  gc_safety_level = 0;
  exiting_rip_cleanly = FALSE ;
  error_save_level = 0 ;
  dosomeaction = FALSE;

  did = 1 ;
#if defined( DEBUG_BUILD )
  debug_lowmemory_count = 0 ;
  debug_control_switch = 0 ;
  debug_control_width = 0 ;
#endif
}

/*
Log stripped */
