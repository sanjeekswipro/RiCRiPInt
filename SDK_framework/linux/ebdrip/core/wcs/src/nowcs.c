/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!src:nowcs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for WCS CMM functions when compiled out.
 */

#include "core.h"
#include "wcscmm.h"
#include "swstart.h"

void wcscmm_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

sw_cmm_api* wcscmm_instance(void)
{
  return NULL ;
}

/*
* Log stripped */
