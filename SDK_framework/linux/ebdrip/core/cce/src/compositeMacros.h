/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!src:compositeMacros.h(EBDSDK_P.1) $
 * $Id: src:compositeMacros.h,v 1.24.4.1.1.1 2013/12/19 11:24:45 anon Exp $
 *
 * Copyright (C) 2001-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A set of useful compositing related macros.
 */

#ifndef __COMPOSITEMACROS_H__
#define __COMPOSITEMACROS_H__

#include "cce.h"

/* --Public Macros-- */

/* All of these macros may evaluate each parameter more than once */

/**
 * Fixed point multiply of a_ and b_.
 * This should only be used to multiply two compatible fixed point numbers.
 */
#define Multiply(a_, b_) \
  (COLORVALUE)(((uint32)(a_) * (uint32)(b_)) / (uint32)COLORVALUE_ONE)

/**
 * Fixed point divide of a_ by b_ This should not be used for general divide
 * operations - it is only intended to be used when both of the parameters are
 * fixed point representations of a number <= to 1. The result is limited to
 * COLORVALUE_ONE. Divides by zero return zero.
 */
#define Divide(a_, b_)                                               \
  (COLORVALUE)(((a_) | (b_)) == 0 ? 0 :                              \
               ((b_) <= (a_) ? COLORVALUE_ONE :                      \
                ((uint32)(a_) * COLORVALUE_ONE) / (uint32)(b_)))

/**
 * Perform the first two stages of the transparency calculation (the
 * source/background blend, not including the ' + aS * aB * Blend(x,y)' term),
 * using pre-multiplied alpha values.
 * Parameters: the source and background's separate alpha (aS, aB), and the
 * pre-multiplied color/alpha values (pS, pB)
 */
#define TranCalcPart(aS_, aB_, pS_, pB_) \
  (COLORVALUE)(Multiply(COLORVALUE_ONE - (aS_), (pB_)) + \
               Multiply(COLORVALUE_ONE - (aB_), (pS_)))

/**
 * Perform the full transparency calculation, using pre-multiplied alpha.
 * Parameters: the source and background's separate alpha (aS, aB),
 * pre-multiplied color/alpha values (pS, pB), and the result of the blend
 * function (bR)
 */
#define TranCalcFull(aS_, aB_, pS_, pB_, bR_) \
  (COLORVALUE)(Multiply(COLORVALUE_ONE - (aS_), (pB_)) + \
               Multiply(COLORVALUE_ONE - (aB_), (pS_)) + \
               Multiply(Multiply((aS_), (aB_)), (bR_)))

#endif

/* Log stripped */
