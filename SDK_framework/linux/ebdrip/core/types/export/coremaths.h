/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:coremaths.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * General maths functions common to Core RIP
 */

#ifndef __COREMATHS_H__
#define __COREMATHS_H__

/** Compute the greatest common divisor of two unsigned numbers. */
unsigned int ugcd(unsigned int n, unsigned int j);

/** Approximate greatest common divisor of two floating point numbers.
    Difference with integer is termination test, which in this case takes an
    epsilon, below which we assume it is close enough to zero. */
SYSTEMVALUE approx_gcd(SYSTEMVALUE n, SYSTEMVALUE j, SYSTEMVALUE epsilon);

/** Compute the lowest common multiple of two unsigned numbers. */
unsigned int ulcm(unsigned int n, unsigned int j);

#endif

/* Log stripped */
