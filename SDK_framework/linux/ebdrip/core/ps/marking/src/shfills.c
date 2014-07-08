/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:src:shfills.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Operators for shaded fills.
 */

#include "core.h"

#include "graphics.h"
#include "gstack.h"
#include "shadex.h"
#include "stacks.h"

Bool shfill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_shfill(&operandstack, gstateptr, GS_SHFILL_SHFILL) ;
}

/* ----------------------------------------------------------------------------
Log stripped */
