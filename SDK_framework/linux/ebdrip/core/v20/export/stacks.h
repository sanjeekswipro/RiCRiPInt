/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:stacks.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS stack definitions
 */

#ifndef __STACKS_H__
#define __STACKS_H__

/*----------------------------------------------------------------------------
  Define the opd, dict & exec stacks
----------------------------------------------------------------------------*/

#include "objstack.h"

/* MAIN THREE STACKS, GRAPHICS STACK AND GRAPHICS STATE  */
extern STACK operandstack;
extern STACK executionstack;
extern STACK dictstack;
extern STACK temporarystack;

#endif /* __STACKS_H__ */

/* Log stripped */
