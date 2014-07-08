/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!src:nometrics.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All Rights Reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#include "core.h"
#include "swerrors.h"
#include "swstart.h"
#include "stacks.h"
#include "objects.h"
#include "fileio.h"
#include "utils.h"

/* Metrics initialisation. */
void sw_metrics_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do. */
}
void sw_jobmetrics_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do. */
}

Bool jobmetrics_(ps_context_t *pscontext)
{
  OBJECT thed = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Having no metrics is not an error, just a noop. */
  if ( ! ps_dictionary(&thed, 0) )
    return FALSE ;

  return push( &thed , &operandstack ) ;
}

Bool metricsreset_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;
  return TRUE;
}

Bool metricsemit_(ps_context_t *pscontext)
{
  OBJECT *ofile ;
  FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Need some arguments. */
  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  /* Pick off the top object and check that its a file. */
  ofile = theTop(operandstack) ;
  if (oType(*ofile) != OFILE)
    return error_handler(TYPECHECK) ;

  /* Check its open and writable. */
  flptr = oFile(*ofile) ;
  if (! isIOpenFileFilter(ofile, flptr) || !isIOutputFile(flptr))
    return error_handler(IOERROR) ;

  /* Do exactly nothing with it :-) */

  /* Cleanup arguments. */
  pop(&operandstack) ;

  return TRUE ;
}

Bool setmetrics_(ps_context_t *pscontext)
{
  Bool value ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Get and typecheck the boolean, but don't do anything with it. */
  return get1B(&value) ;
}

/* Log stripped */
