/** \file
 * \ingroup jbig
 *
 * $HopeName: COREjbig!src:nojbig.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for JBIG functions when compiled out.
 */

#include "core.h"
#include "fileioh.h"

void jbig2_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

Bool jbig_signature_test(FILELIST *filter)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  return FALSE ;
}

/* Log stripped */
