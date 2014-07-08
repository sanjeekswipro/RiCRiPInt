/** \file
 * \ingroup png
 *
 * $HopeName: COREpng!src:nopng.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for PNG functions when compiled out.
 */

#include "core.h"
#include "fileioh.h"
#include "pngfilter.h"

void png_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

Bool png_signature_test(FILELIST *filter)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  return FALSE ;
}

/* Log stripped */
