/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:nopdfin.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for PDF input functions when compiled out.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "objecth.h"
#include "fileioh.h"
#include "graphict.h"
#include "swpdf.h"
#include "mps.h"
#include "mmcompat.h"
#include "pdfparam.h"
#include "hqmemset.h"

static Bool pdfin_swstart(struct SWSTART *params)
{
  PDFPARAMS *pdfparams ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Initialise empty PDF params, it is used in the save structure */
  CoreContext.pdfparams = pdfparams = mm_alloc_static(sizeof(PDFPARAMS)) ;
  if ( pdfparams == NULL )
    return FALSE ;

  HqMemZero(pdfparams, sizeof(PDFPARAMS)) ;

  return TRUE ;
}

void pdfin_C_globals(core_init_fns *fns)
{
  fns->swstart = pdfin_swstart ;
}

PDFXCONTEXT *pdfin_xcontext_base = NULL ;

Bool pdf_x_filter_preflight(FILELIST *flptr, OBJECT *args, STACK *stack)
{
  UNUSED_PARAM(FILELIST *, flptr) ;
  UNUSED_PARAM(OBJECT *, args) ;
  UNUSED_PARAM(STACK *, stack) ;
  return TRUE ;
}

Bool pdf_exec_stream(struct OBJECT *stream, int stream_type)
{
  UNUSED_PARAM(OBJECT *, stream) ;
  UNUSED_PARAM(int, stream_type) ;
  return error_handler(INVALIDACCESS) ;
}

typedef Bool (*gs_walk_fn)(GSTATE *, void *) ;

Bool pdf_walk_gstack(gs_walk_fn gs_fn, void *args)
{
  UNUSED_PARAM(gs_walk_fn, gs_fn) ;
  UNUSED_PARAM(void *, args) ;
  return TRUE ;
}

Bool pdf_getStrictpdf(PDFXCONTEXT *pdfxc)
{
  UNUSED_PARAM(PDFXCONTEXT *, pdfxc) ;
  return FALSE ;
}

mps_res_t MPS_CALL pdfparams_scan(mps_ss_t ss, void *p, size_t s)
{
  UNUSED_PARAM(mps_ss_t, ss) ;
  UNUSED_PARAM(void *, p) ;
  UNUSED_PARAM(size_t, s) ;
  return MPS_RES_OK ;
}

Bool pdf_newpagedevice( void )
{
  return TRUE ;
}

/* Log stripped */
