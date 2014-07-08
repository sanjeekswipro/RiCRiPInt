/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:nozip.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for ZIP functions when compiled out.
 */

#include "core.h"
#include "swstart.h"
#include "zipdev.h"
#include "writeonly_zipdev.h"

void zipdev_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do. */
}

void writeonly_zipdev_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do. */
}

/* Log stripped */
