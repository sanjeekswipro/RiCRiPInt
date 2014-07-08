/** \file
 * \ingroup hdphoto
 *
 * $HopeName: COREwmphoto!src:nowmp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for WMP functions when compiled out.
 */

#include "core.h"
#include "coreinit.h"
#include "fileioh.h"
#include "mmcompat.h"
#include "wmpparams.h"
#include "hqmemset.h"
#include "swstart.h"

static Bool wmp_swstart(SWSTART *params)
{
  WMPPARAMS *wmpparams ;
  UNUSED_PARAM(SWSTART*, params) ;

  /* Initialise empty WMP params, it is used in the save structure */
  CoreContext.wmpparams = wmpparams = mm_alloc_static(sizeof(WMPPARAMS)) ;
  if ( wmpparams == NULL )
    return FALSE ;

  HqMemZero(wmpparams, sizeof(WMPPARAMS)) ;

  return TRUE ;
}

void wmp_C_globals(core_init_fns *fns)
{
  fns->swstart = wmp_swstart ;
}

Bool wmp_signature_test(FILELIST *filter)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  return FALSE ;
}

/* Log stripped */
