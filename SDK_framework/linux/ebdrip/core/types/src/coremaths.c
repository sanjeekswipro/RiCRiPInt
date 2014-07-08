/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:coremaths.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * General maths functions common to Core RIP
 */

#include "core.h"
#include "coremaths.h"

/** Compute the greatest common divisor of two unsigned numbers. */
unsigned int ugcd(unsigned int n, unsigned int j)
{
  if ( j == 0 )
    return n ;
  if ( n == 0 )
    return j ;

  return ugcd(j, n % j) ;
}

/** Approximate greatest common divisor of two floating point numbers.
    Difference with integer is termination test, which in this case takes an
    epsilon, below which we assume it is close enough to zero. */
SYSTEMVALUE approx_gcd(SYSTEMVALUE n, SYSTEMVALUE j, SYSTEMVALUE epsilon)
{
  if ( fabs(j) < epsilon )
    return n ;
  if ( fabs(n) < epsilon )
    return j ;

  return approx_gcd(j, fmod(n, j), epsilon) ;
}

/** Compute the lowest common multiple of two unsigned numbers. */
unsigned int ulcm(unsigned int n, unsigned int j)
{
  unsigned int gcd = ugcd(n, j);

  if ( gcd == 0 )
    return 0;
  else
    return (n * j) / gcd;
}

/* Log stripped */
