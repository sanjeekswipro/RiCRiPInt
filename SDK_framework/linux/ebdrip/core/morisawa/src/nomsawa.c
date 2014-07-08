/** \file
 * \ingroup morisawa
 *
 * $HopeName: SWmorisawa!src:nomsawa.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stub functions for decrypting Morisawa fonts for non-Morisawa customers.
 */

#include "core.h"

#include "morisawa.h"

Bool MPS_decrypt(uint8 *from, int32 len, uint8 *to)
{
  UNUSED_PARAM(uint8 *, from);
  UNUSED_PARAM(int32, len);
  UNUSED_PARAM(uint8 *, to);

  return FALSE ;        /* any other customers cannot use Morisawa fonts */
}

Bool MPS_supported(void)
{
  return FALSE ;        /* RIP does not support Morisawa fonts */
}

void morisawa_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

/* Log stripped */
