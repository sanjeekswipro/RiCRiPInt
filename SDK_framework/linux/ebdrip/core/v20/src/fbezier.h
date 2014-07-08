/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:fbezier.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bezier curve reduction API
 */

#ifndef __FBEZIER_H__
#define __FBEZIER_H__

/**
 * Enumeration defining the circumstanes the bezier chop code will call
 * the supplied callback function.
 */
enum 
{
  BEZ_POINTS = 1, /* call the callback with appoximating linear segments */
  BEZ_CTRLS = 2 , /* call callback with initial/final bezier control points */
  BEZ_BEZIERS = 4 /* call callback with each recursive bezier generated */
};

Bool bezchop(FPOINT pnts[4], int32 (*func)(FPOINT *, void *, int32), void *data,
             int32 flags);

void bezeval(FPOINT pnts[4], SYSTEMVALUE t, SYSTEMVALUE *x, SYSTEMVALUE *y);

int32 freduce(FPOINT pnts[4]);
void  nextfbez(FPOINT pnts[4],FPOINT ftmp1[4],FPOINT ftmp2[4]);

#endif /* protection for multiple inclusion */

/* Log stripped */
