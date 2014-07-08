/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:nopcl5.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for Core PCL5 functions when compiled out.
 */

#include "core.h"
#include "mmcompat.h"
#include "pcl5ops.h"

void pcl5_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

Bool pcl5_font_sel_caches_free(void)
{
  /* Nothing was freed */
  return FALSE ;
}

/* ============================================================================
* Log stripped */
