/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:coretypes.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core types functional support.
 */

#include "core.h"


/* Range initialiser
 */
Range rangeNew(uint32 origin, uint32 length)
{
  Range self;

  self.origin = origin;
  self.length = length;

  return self;
}

/* Does the range contain the passed value?
 */
Bool rangeContains(Range self, uint32 value)
{
  return value >= self.origin && value - self.origin < self.length;
}


Range rangeIntersection(Range a, Range b)
{
  Range result;
  uint32 top = min(a.origin + a.length, b.origin + b.length);

  result.origin = max(a.origin, b.origin);
  result.length = top > result.origin ? top - result.origin : 0;
  return result;
}


Range rangeUnion(Range a, Range b)
{
  Range result;
  uint32 top;

  if ( a.length == 0 )
    return b;
  if ( b.length == 0 )
    return a;
  result.origin = min(a.origin, b.origin);
  top = max(a.origin + a.length, b.origin + b.length);
  result.length = top - result.origin;
  return result;
}


/* --Size2d methods-- */

Size2d size2dNew(uint32 width, uint32 height)
{
  Size2d self;

  self.width = width;
  self.height = height;
  return self;
}


/* Log stripped */
