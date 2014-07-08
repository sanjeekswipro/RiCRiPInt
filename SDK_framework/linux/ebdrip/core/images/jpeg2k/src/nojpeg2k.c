/** \file
 * \ingroup jpeg2000
 *
 * $HopeName: HQNjpeg2k-kak6!src:nojpeg2k.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for JPEG2000 functions when compiled out.
 */

#include "core.h"
#include "fileioh.h"
#include "jpeg2000.h"

void jpeg2000_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

void jpeg2000_override_cs(FILELIST *filter, Bool cs_state)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  UNUSED_PARAM(Bool, cs_state) ;
}

Bool jpeg2000_signature_test(FILELIST *filter)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  return FALSE ;
}

/* Log stripped */
