/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:notiff.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for TIFF functions when compiled out.
 */

#include "core.h"
#include "coreinit.h"
#include "fileioh.h"
#include "mmcompat.h"
#include "t6params.h"
#include "hqmemset.h"
#include "tiffexec.h"

static Bool tiffexec_swstart(struct SWSTART *params)
{
  TIFF6PARAMS *tiff6params ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Initialise empty TIFF params, it is used in the save structure */
  CoreContext.tiff6params = tiff6params = mm_alloc_static(sizeof(TIFF6PARAMS)) ;
  if ( tiff6params == NULL )
    return FALSE ;

  HqMemZero(tiff6params, sizeof(TIFF6PARAMS));

  return TRUE ;
}

void tiffexec_C_globals(core_init_fns *fns)
{
  fns->swstart = tiffexec_swstart ;
}

Bool tiff_signature_test(FILELIST *filter)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  return FALSE ;
}

/* Log stripped */
