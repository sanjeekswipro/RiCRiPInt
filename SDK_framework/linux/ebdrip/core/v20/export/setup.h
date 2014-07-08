/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:setup.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS stack setup defines
 */

#ifndef __SETUP_H__
#define __SETUP_H__

#include "objstack.h"

/* Backwards compatibility macro definitions */
#define setup_integer(i_) stack_push_integer((i_), &operandstack)
#define setup_real(r_)    stack_push_real((r_), &operandstack)

Bool xsetup_operator(int32 opnumber, int32 type);

#endif /* protection for multiple inclusion */


/* Log stripped */
