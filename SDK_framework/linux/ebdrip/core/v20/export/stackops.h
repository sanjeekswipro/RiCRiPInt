/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:stackops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS stack operations
 */

#ifndef __STACKOPS_H__
#define __STACKOPS_H__

int32 gcd(int32 n, int32 j);
int32 num_to_mark(void);

typedef struct {
 int32 operandstackSize;
 int32 dictstackSize;
 int32 temporarystackSize;
 int32 executionstackSize;
 int32 savelevel;
} STACK_POSITIONS;

void saveStackPositions(STACK_POSITIONS *positions);
Bool restoreStackPositions(STACK_POSITIONS *positions, Bool restoresavelevel);

#endif /* protection for multiple inclusion */

/* Log stripped */
