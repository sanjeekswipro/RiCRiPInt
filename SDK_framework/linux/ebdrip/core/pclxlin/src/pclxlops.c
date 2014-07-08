/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript "pclxlexec" operator and pclxl_init() and pclxl_finish()
 * methods which are all that are published to the outside world (currently)
 */

#include "core.h"
#include "coreinit.h"
#include "namedef_.h"   /* NAME_Strict, NAME_DebugPCLXL etc. */
#include "stacks.h"     /* isEmpty(), operandstack etc. */
#include "params.h"     /* SWSTART* */
#include "swerrors.h"   /* error_handler() */
#include "control.h"    /* ps_interpreter_level */
#include "progress.h"
#include "uelflush.h"
#include "timing.h"

#include "pcl.h"
#include "pclxlcontext.h"
#include "pclxluserstream.h"
#include "pclxlscan.h"
#include "pclxldebug.h"
#include "pclxlerrors.h"
#include "pclxlpassthrough.h"
#include "pclxlimage.h"
#include "pclxloperators.h"


PCLXL_CONTEXT pclxl_job_lifetime_context = NULL ;

/** File runtime initialisation */
static void init_C_globals_pclxlops(void)
{
  pclxl_job_lifetime_context = NULL ;
}

/**
 * \brief Initialise PCL XL handling module.
 */
static Bool pclxl_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /* None of these creates GC roots or pools, so we can fall out on error */
  if ( !pclxl_config_params_init() ||
       !pclxl_user_streams_init() ||
       !pclxl_pass_through_init() ||
       !pclxl_image_decode_filter_init() )
    return FALSE ;

  return TRUE ;
}

/**
 * \brief Tidy up PCL XL handling module on application exit.
 */
static void pclxl_finish(void)
{
  pclxl_user_streams_finish() ;
  pclxl_pass_through_finish() ;
}

IMPORT_INIT_C_GLOBALS( pclxldebug )
IMPORT_INIT_C_GLOBALS( pclxlfont )
IMPORT_INIT_C_GLOBALS( pclxlpassthrough )
IMPORT_INIT_C_GLOBALS( pclxluserstream )

/** Compound runtime initialisation */
void pclxl_C_globals(core_init_fns *fns)
{
  init_C_globals_pclxldebug() ;
  init_C_globals_pclxlfont() ;
  init_C_globals_pclxlops() ;
  init_C_globals_pclxlpassthrough() ;
  init_C_globals_pclxluserstream() ;

  fns->swstart = pclxl_swstart ;
  fns->finish = pclxl_finish ;
}

/**
 * \brief The PostScript "pclxlexec" operator.
 */
Bool pclxlexec_(ps_context_t *pscontext)
{
  /**
   * \brief the "pclxlexec" PostScript operator expects exactly one
   * argument on the PostScript operand stack and this argument must
   * be an open-for-input file stream
   */

  /* The first thing to do is perform a whole lot of validation that
   * this is indeed what is on the operand stack.
   */
  corecontext_t *corecontext = ps_core_context(pscontext);
  int32 num_args = 0;
  int32 stack_size = 0;
  OBJECT* pclxl_file_object = NULL;
  OBJECT* dictionary_object = NULL;
  int32 saved_ps_interpreter_level = ps_interpreter_level;
  FILELIST* flptr = NULL;
  int32 scan_result = 0;
  int32 ss1, ss2;

  /* I am assuming that "operandstack" and "error_handler" are globals
   * provided to any/all PostScript operators.
   */

  /* We do not allow recursive pclxlexec calls. */
  if (pclxl_job_lifetime_context != NULL) {
    return error_handler(UNDEFINED);
  }

  gc_safe_in_this_operator();
  /* All PS objects below are parts of the arguments, so GC finds them through
     the operand stack. */

  if ( isEmpty(operandstack) ) {
    /* An empty PostScript stack is clearly wrong. */
    return error_handler(STACKUNDERFLOW);

  } else {
    /* Ok, let's grab the top item on the stack. */

    stack_size = theStackSize(operandstack);
    pclxl_file_object = TopStack(operandstack, stack_size);
    num_args++;

    /* From the PCL5 "pcl5exec" (operator) we seem to allow an
     * additional optional operand that is a PostScript dictionary
     * containing some sort of "parameters".
     */

    if ( oType(*pclxl_file_object) == ODICTIONARY ) {
      dictionary_object = pclxl_file_object;

      if ( stack_size < 1 ) {
        return error_handler(STACKUNDERFLOW);

      } else if ( fastStackAccess(operandstack) ) {
        pclxl_file_object = (&dictionary_object[-1]);
        num_args++;

      } else {
        pclxl_file_object = stackindex(1, &operandstack);
        num_args++;
      }
    }

    /* At any rate, we'd better darn well have an OFILE now. */

    if ( oType(*pclxl_file_object) != OFILE ) {
      /* Nope, it's not a file. */
      return error_handler(TYPECHECK);

    } else if ( ((flptr = oFile(*pclxl_file_object)) == NULL) ||
                (!isIOpenFileFilter(pclxl_file_object, flptr)) ) {
      /* Not sure if this call actually attempted to open the file But
       * for certain the file is *not* open (now) So we aren't likely
       * to be able to read from it.
       */
      return error_handler(IOERROR);

    } else if ( !isIInputFile(flptr) ) {
      /* It may be an open file, but it is not open for *read* */
      return error_handler(IOERROR);

    } else if ( isIEof(flptr) ) {
      /* It is open and readable, but is already at EOF */
      return error_handler(IOERROR);
    }
  }

  /* Okay, we have an open PCL XL flptr and optionally a PostScript
   * dictionary of additional parameters.  So we can go ahead and
   * process the flptr.
   */
  probe_begin(SW_TRACE_INTERPRET_PCLXL, 0) ;
  if ( ! pclxl_create_context(corecontext, dictionary_object, &pclxl_job_lifetime_context) ) {
    /* We failed to create a new PCLXL_CONTEXT We are expecting the
     * error to have been logged so we just return FALSE here.
     */
    HQASSERT((ps_interpreter_level >= saved_ps_interpreter_level), "ps interpreter level has become corrupt");

    ps_interpreter_level = saved_ps_interpreter_level;
    (void) uelflush(pclxl_file_object);
    (void) file_close(pclxl_file_object);
    probe_end(SW_TRACE_INTERPRET_PCLXL, 0) ;
    return FALSE;
  }
  else {
    pclxl_job_lifetime_context->config_params.job_config_dict = dictionary_object;
  }

  /* save the stack size before executing PCL commands*/
  ss1 = theStackSize(operandstack);
  if ( !setReadFileProgress(flptr) ||
       !pclxl_parser_push_stream(pclxl_job_lifetime_context->parser_context, flptr) ||
       ((scan_result = pclxl_scan(pclxl_job_lifetime_context->parser_context)) < EOF) ) {
    /* We were expecting EOF, but we have received a (more) negative
     * result code.  So something went wrong during the processing of
     * the PCLXL stream We must still remember to delete the
     * "context".
     */
    (void) uelflush(pclxl_file_object);
    /* check stack size after an error, adjust stack if needed */
    ss2 = theStackSize(operandstack);
    if (ss2 > ss1) {
      npop(ss2 - ss1, &operandstack);
    }
    error_clear_context(corecontext->error);
  }
  HQASSERT((theStackSize(operandstack) == ss1), "plcxlexec_: interpreter left objects on stack");

  /*
   * Note that we attempt to report "errors"
   * regardless of whether the above PCLXL "scan" of the input stream was successful or not
   * because the "errors" may also include PCLXL *warnings*
   * which may have simply "noted in passing"
   */

  pclxl_report_errors(pclxl_job_lifetime_context);

  /*
   * Theoretically, if the job was successfully processed
   * then all the transient "resources" (i.e. temporary soft fonts, patterns etc.)
   * will already have been cleared-up/released as part of the handling of the
   * EndPage, RemoveFont and EndSession operators.
   *
   * However, in the event of premature termination of the job
   * and the above flushing upto and including UEL,
   * we will have skipped passed thse PCLXL operators
   * and therefore skipped this clear-up/resource release
   *
   * So we attempt it (again) now.
   * Note that we also request EndPage-related clear-up
   * because we may have aborted mid-page
   */

  (void) pclxl_release_resources(pclxl_job_lifetime_context, TRUE);

  /*
   * We can now close our use of the input job stream
   * and destroy the PCLXL context because we have now finished with them both
   */

  (void) file_close(pclxl_file_object);
  pclxl_destroy_context(&pclxl_job_lifetime_context);
  probe_end(SW_TRACE_INTERPRET_PCLXL, 0) ;

  HQASSERT(pclxl_job_lifetime_context == NULL,
           "pclxl_job_lifetime_context is not NULL") ;

  if ( scan_result >= EOF ) {
    /* We only pop the parameters off the operand stack if we were
     * successful.
     */
    npop(num_args, &operandstack);
  }

  HQASSERT((ps_interpreter_level >= saved_ps_interpreter_level), "ps interpreter level has become corrupt");
  ps_interpreter_level = saved_ps_interpreter_level;

  return TRUE;
}

/******************************************************************************
* Log stripped */
