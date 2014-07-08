/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!src:alpha.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Alpha compositers and converters.
 */

#include "core.h"

#include "compositeMacros.h"

/* --Separate/Premultiplied conversions-- */

/* Multiply 'count' color values in 'src' by 'alpha', storing the results
in 'result'.
*/
void cceMultiplyAlpha(uint32 count,
                      const COLORVALUE* src,
                      COLORVALUE alpha,
                      COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++)
    result[i] = Multiply(src[i], alpha);
}

/* Divide 'count' color values in 'src' by 'alpha', storing the results in
'result'. If the alpha is zero, the color is copied to the result.
*/
void cceDivideAlpha(uint32 count,
                    const COLORVALUE* src,
                    COLORVALUE alpha,
                    COLORVALUE* result)
{
  uint32 i;

  if (alpha == COLORVALUE_ZERO) {
    for (i = 0; i < count; i ++)
      result[i] = COLORVALUE_ZERO;
  } else if (alpha == COLORVALUE_ONE) {
    for (i = 0; i < count; i ++)
      result[i] = src[i];
  }
  else {
    for (i = 0; i < count; i ++)
      result[i] = Divide(src[i], alpha);
  }
}

/* --Alpha compositing variants-- */

/* Composite a list of alpha values.
*/
void cceCompositeAlpha(COLORVALUE alpha1,
                       COLORVALUE alpha2,
                       COLORVALUE* result)
{
  if ( alpha1 == COLORVALUE_ONE || alpha2 == COLORVALUE_ONE )
    *result = COLORVALUE_ONE;
  else
    *result = (COLORVALUE)((uint32)alpha1 + alpha2 - Multiply(alpha1, alpha2));
}

/* Log stripped */
