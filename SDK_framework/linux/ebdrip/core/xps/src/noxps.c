/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:noxps.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for XPS functions when compiled out.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "xps.h"
#include "mps.h"
#include "mmcompat.h"
#include "hqmemset.h"

static Bool xps_swstart(SWSTART *params)
{
  XPSPARAMS *xpsparams ;

  UNUSED_PARAM(SWSTART*, params) ;

  /* Initialise Context.xpsparams, it is used in the save structure */
  CoreContext.xpsparams = xpsparams = mm_alloc_static(sizeof(XPSPARAMS)) ;
  if ( xpsparams == NULL )
    return FALSE ;

  HqMemZero(xpsparams, sizeof(XPSPARAMS)) ;

  return TRUE ;
}

static void xps_finish(void)
{
  CoreContext.xpsparams = NULL ;
}

void xps_C_globals(core_init_fns *fns)
{
  fns->swstart = xps_swstart ;
  fns->finish = xps_finish ;
}

mps_res_t xpsparams_scan(mps_ss_t ss, void *p, size_t s)
{
  UNUSED_PARAM( mps_ss_t, ss );
  UNUSED_PARAM( void*, p );
  UNUSED_PARAM( size_t, s );

  return MPS_RES_OK;
}

void xps_icc_cache_purge(int32 slevel)
{
  UNUSED_PARAM(int32, slevel) ;
}

/* Log stripped */
