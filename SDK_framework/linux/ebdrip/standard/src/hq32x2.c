/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!src:hq32x2.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * FrameWork '64 bit type' Implementation
 */

/* ----------------------- Includes ---------------------------------------- */

#include "std.h"  /* includes hq32x2.h */

#include <math.h>

/* ----------------------- Macros ------------------------------------------ */

#define CAN_FIT_IN_INT32(p32x2)  (                       \
 ((p32x2)->high == 0 && (p32x2)->low <= MAXINT32) ||       \
 ((p32x2)->high == -1 && (p32x2)->low >= (uint32)MININT32) \
)

#define U_CAN_FIT_IN_INT32(pU32x2)  (             \
 (pU32x2)->high == 0 && (pU32x2)->low <= (uint32)MAXINT32   \
)

/* the number 1 << 32 as a double */
#define ONE_SHIFT_LEFT_32  ((double)(1u<<31) * 2.0)

/* ----------------------- Functions --------------------------------------- */

/*
 * Conversion to 32 bit value
 */

/* Convert to 32 bit value */
/* Return TRUE <=> value in range */
/* pReturn is unchanged if value is out of range */

HqBool HQNCALL Hq32x2ToInt32(const Hq32x2 *p32x2, int32 *pReturn)
{
  if (CAN_FIT_IN_INT32(p32x2))
  {
    * pReturn = (int32) p32x2->low;
    return TRUE;
  }

  return FALSE;
}

HqBool HQNCALL HqU32x2ToInt32(const HqU32x2 *pU32x2, int32 *pReturn)
{
  if ( U_CAN_FIT_IN_INT32( pU32x2 ) )
  {
    *pReturn = (int32) pU32x2->low;
    return TRUE;
  }
  return FALSE;
}

HqBool HQNCALL Hq32x2ToUint32(const Hq32x2 *p32x2, uint32 *pReturn)
{
  if ( p32x2->high == 0 )
  {
    * pReturn = p32x2->low;
    return TRUE;
  }

  return FALSE;
}

HqBool HQNCALL HqU32x2ToUint32(const HqU32x2 *pU32x2, uint32 *pReturn)
{
  if ( pU32x2->high == 0 )
  {
    *pReturn = pU32x2->low;
    return TRUE;
  }
  return FALSE;
}


/* Force to 32 bit value */
/* Return valid limit if out of range */

int32 HQNCALL Hq32x2BoundToInt32(const Hq32x2 *p32x2)
{
  /* Force to signed 32 bit value */
  if ( CAN_FIT_IN_INT32( p32x2 ) )
   return (int32) p32x2->low;

  return ( p32x2->high >= 0 ) ? MAXINT32 : MININT32;
}

int32 HQNCALL HqU32x2BoundToInt32(const HqU32x2 *pU32x2)
{
  if ( U_CAN_FIT_IN_INT32( pU32x2 ) )
    return (int32)pU32x2->low;

  return MAXINT32;
}


uint32 HQNCALL Hq32x2BoundToUint32(const Hq32x2 *p32x2)
{
  if ( p32x2->high == 0 ) return p32x2->low;

  /* Force to unsigned 32 bit value */
  return ( p32x2->high > 0 ) ? MAXUINT32 : 0;
}

uint32 HQNCALL HqU32x2BoundToUint32(const HqU32x2 *pU32x2)
{
  if ( pU32x2->high == 0 )
   return pU32x2->low;

  return MAXUINT32;
}


int32 HQNCALL Hq32x2AssertToInt32(const Hq32x2 *p32x2)
{
  /* Force to signed 32 bit value */
  if ( CAN_FIT_IN_INT32( p32x2 ) )
    return (int32) p32x2->low;

  HQFAIL( "Hq32x2 out of range for int32" );

  return ( p32x2->high >= 0 ) ?  MAXINT32 : MININT32;
}

int32 HQNCALL HqU32x2AssertToInt32(const HqU32x2 *pU32x2)
{
  if ( U_CAN_FIT_IN_INT32( pU32x2 ) )
    return (int32)pU32x2->low;

  HQFAIL( "HqU32x2 out of range for int32" );

  return MAXINT32;
}


uint32 HQNCALL Hq32x2AssertToUint32(const Hq32x2 *p32x2)
{
  if ( p32x2->high == 0 )
    return p32x2->low;

  HQFAIL( "Hq32x2 out of range for uint32" );

  /* Force to unsigned 32 bit value */
  return ( p32x2->high > 0 ) ? MAXUINT32 : 0;
}

uint32 HQNCALL HqU32x2AssertToUint32(const HqU32x2 *pU32x2)
{
  if ( pU32x2->high == 0 )
   return pU32x2->low;

  HQFAIL( "HqU32x2 out of range for uint32" );

  return MAXUINT32;
}


/*
 * Conversion to and from ptrdiff_t, size_t values
 */

void HQNCALL Hq32x2FromPtrdiff_t( Hq32x2 * p32x2, ptrdiff_t ptrdiff )
{
  HqBool negative = (ptrdiff < 0) ;

  if ( negative )
    ptrdiff = -ptrdiff ;

  if ( ptrdiff < 0 ) {
    /* This is the maximum negative value. Check it will fit in Hq32x2. */
    HQASSERT(((((ptrdiff - 1) >> 31) >> 31) >> 1) == 0,
             "ptrdiff_t overflows Hq32x2") ;
    p32x2->high = MININT32 ;
    p32x2->low = 0 ;
  } else {
    HQASSERT((((ptrdiff >> 31) >> 31) >> 1) == 0,
             "ptrdiff_t overflows Hq32x2") ;

    p32x2->low = (uint32)ptrdiff;
    /* Use two right shifts, in case 32 bit shift does nothing on this
       architecture. The shift will fill the high bits with zero, because the
       value of ptrdiff is positive. */
    p32x2->high = (int32)((ptrdiff >> 16) >> 16) ;

    if ( negative ) {
      /* Negate p32x2 */
      p32x2->low = ~p32x2->low;
      p32x2->high = ~p32x2->high;
      p32x2->low++;
      if (p32x2->low == 0)
        p32x2->high++;
    }
  }
}


void HQNCALL Hq32x2FromSize_t( Hq32x2 * p32x2, size_t sizet )
{
  HQASSERT((((sizet >> 31) >> 31) >> 1) == 0, "size_t overflows Hq32x2") ;

  p32x2->low = (uint32)sizet;
  /* Use two right shifts, in case 32 bit shift does nothing on this
     architecture. The shift will fill the high bits with zero, because
     size_t is an unsigned type. */
  p32x2->high = (int32)((sizet >> 16) >> 16) ;
}

HqBool HQNCALL Hq32x2ToSize_t(const Hq32x2 * p32x2, size_t *sizet)
{
  size_t result ;

  /* size_t is unsigned */
  if ( p32x2->high < 0 )
    return FALSE ;

  /* Use two 16-bit shifts, in case 32 bit shift does nothing on this
     architecture. */
  result = p32x2->low | (((size_t)p32x2->high << 16) << 16) ;
  /* We can't use sizeof in #if to divine the size of size_t, so check that
     we did store the upper bits correctly, and fail if we couldn't. */
  if ( ((result >> 16) >> 16) != (size_t)p32x2->high )
    return FALSE;

  *sizet = result;
  return TRUE;
}

void HQNCALL HqU32x2FromSize_t( HqU32x2 * p32x2, size_t sizet )
{
  HQASSERT((((sizet >> 31) >> 31) >> 2) == 0, "size_t overflows HqU32x2") ;

  p32x2->low = (uint32)sizet;
  /* Use two right shifts, in case 32 bit shift does nothing on this
     architecture. The shift will fill the high bits with zero, because
     size_t is an unsigned type. */
  p32x2->high = (uint32)((sizet >> 16) >> 16) ;
}


HqBool HQNCALL HqU32x2ToSize_t(const HqU32x2 * p32x2, size_t *sizet)
{
  size_t result ;

  /* Use two 16-bit shifts, in case 32 bit shift does nothing on this
     architecture. */
  result = p32x2->low | (((size_t)p32x2->high << 16) << 16) ;
  /* We can't use sizeof in #if to divine the size of size_t, so check that
     we did store the upper bits correctly, and fail if we couldn't. */
  if ( ((result >> 16) >> 16) != (size_t)p32x2->high )
    return FALSE;

  *sizet = result;
  return TRUE;
}


/*
 * Conversion to and from double values
 */

void HQNCALL Hq32x2FromDouble( Hq32x2 * p32x2, double dbl )
{
  double d = fabs(dbl);

  HQASSERT(dbl <= MAXINT32 * ONE_SHIFT_LEFT_32 &&
           dbl >= MININT32 * ONE_SHIFT_LEFT_32,
           "Double value is outside range of Hq32x2") ;

  p32x2->low = (uint32)fmod(d, ONE_SHIFT_LEFT_32);

  d = floor(d / ONE_SHIFT_LEFT_32);
  p32x2->high = (int32)d;

  if (dbl < 0) {
    /* flip the sign of p32x2 */
    p32x2->low = ~p32x2->low;
    p32x2->high = ~p32x2->high;
    p32x2->low++;
    if (p32x2->low == 0)
      p32x2->high++;
  }
}

void HQNCALL HqU32x2FromDouble(HqU32x2 * pU32x2, double dbl)
{
  HQASSERT(dbl <= MAXUINT32 * ONE_SHIFT_LEFT_32 && dbl >= 0,
           "Double value is outside range of Hq32x2") ;

  pU32x2->low = (uint32)fmod(dbl, ONE_SHIFT_LEFT_32);

  dbl = floor(dbl / ONE_SHIFT_LEFT_32);
  pU32x2->high = (uint32)dbl;
}


double HQNCALL Hq32x2ToDouble(const Hq32x2 *p32x2)
{
  int32  sign = 1;
  uint32 low;
  int32  high;

  low = p32x2->low;
  high = p32x2->high;

  if (high < 0) {
    if (high == MININT32 && low == 0) {
      /* this is a special case - the 64-bit equivalent of MININT32. */
      return ldexp(-1.0, 63);
    } else {
      /* flip the number positive first */
      sign = -1;
      low = ~low;
      high = ~high;
      low++;
      if (low == 0)
        high++;
    }
  }
  return ((double)low + ldexp((double)high, 32)) * (double)sign;
}


double HQNCALL HqU32x2ToDouble(const HqU32x2 * pU32x2)
{
  return (double)pU32x2->low + ldexp((double)pU32x2->high, 32);
}

/*
 * Unary functions on a 64 bit value
 */

int32 HQNCALL Hq32x2Sign(const Hq32x2 *p32x2)
{
  return ( p32x2->high != 0 )
   ? ( ( p32x2->high > 0 ) ? +1 : -1 )
   : ( ( p32x2->low != 0 ) ? +1 : 0 );
}

uint32 HQNCALL HqU32x2Sign(const HqU32x2 * pU32x2)
{
  return ( pU32x2->low == 0 && pU32x2->high == 0 ) ? 0 : 1;
}


/*
 * Binary functions on two Hq32x2
 */

void HQNCALL Hq32x2Add(Hq32x2 *p32x2Result, const Hq32x2 *p32x2A, const Hq32x2 *p32x2B)
{
  uint32        low1 = p32x2A->low;
  int32         high1 = p32x2A->high;
  uint32        low2 = p32x2B->low;
  int32         high2 = p32x2B->high;
  uint32        low;
  int32         carry;

  low = low1 + low2;
  carry = (low < low1) ? 1 : 0;

  p32x2Result->low = low;
  p32x2Result->high = high1 + high2 + carry;
}

void HQNCALL HqU32x2Add(HqU32x2 * pU32x2Result, const HqU32x2 *pU32x2A, const HqU32x2 *pU32x2B)
{
  uint32        low1 = pU32x2A->low;
  uint32        high1 = pU32x2A->high;
  uint32        low2 = pU32x2B->low;
  uint32        high2 = pU32x2B->high;
  uint32        low;
  uint32        carry;

  low = low1 + low2;
  carry = (low < low1) ? 1 : 0;

  pU32x2Result->low = low;
  pU32x2Result->high = high1 + high2 + carry;
}


void HQNCALL Hq32x2Subtract(Hq32x2 *p32x2Result, const Hq32x2 *p32x2A, const Hq32x2 *p32x2B)
{
  uint32        low1 = p32x2A->low;
  int32         high1 = p32x2A->high;
  uint32        low2 = p32x2B->low;
  int32         high2 = p32x2B->high;
  uint32        low;
  int32         carry;

  low = low1 - low2;
  carry = (low > low1) ? 1 : 0;

  p32x2Result->low = low;
  p32x2Result->high = high1 - ( high2 + carry );
}

void HQNCALL HqU32x2Subtract(HqU32x2 *pU32x2Result, const HqU32x2 *pU32x2A, const HqU32x2 *pU32x2B)
{
  uint32        low1 = pU32x2A->low;
  uint32        high1 = pU32x2A->high;
  uint32        low2 = pU32x2B->low;
  uint32        high2 = pU32x2B->high;
  uint32        low;
  uint32        carry;

  low = low1 - low2;
  carry = (low > low1) ? 1 : 0;

  pU32x2Result->low = low;
  pU32x2Result->high = high1 - ( high2 + carry );
}


int32 HQNCALL Hq32x2Compare( const Hq32x2 *p32x2A, const Hq32x2 *p32x2B)
{
  return ( p32x2A->high != p32x2B->high )
   ? ( ( p32x2A->high > p32x2B->high ) ? +1 : -1 )
   :
   (
     ( p32x2A->low != p32x2B->low )
     ? ( ( p32x2A->low > p32x2B->low ) ? +1 : -1 )
     : 0
   );
}

int32 HQNCALL HqU32x2Compare(const HqU32x2 *pU32x2A, const HqU32x2 *pU32x2B)
{
  return ( pU32x2A->high != pU32x2B->high )
   ? ( ( pU32x2A->high > pU32x2B->high ) ? +1 : -1 )
   :
   (
     ( pU32x2A->low != pU32x2B->low )
     ? ( ( pU32x2A->low > pU32x2B->low ) ? +1 : -1 )
     : 0
   );
}


/*
 * Binary functions on a 64 bit value and a uint32
 */
void HQNCALL Hq32x2AddUint32(Hq32x2 *p32x2Result, const Hq32x2 *p32x2A, uint32 ui32)
{
  Hq32x2        tmp;

  Hq32x2FromUint32( &tmp, ui32 );
  Hq32x2Add( p32x2Result, p32x2A, &tmp );
}

void HQNCALL HqU32x2AddUint32(HqU32x2 *pU32x2Result, const HqU32x2 *pU32x2A, uint32 ui32 )
{
  HqU32x2       tmp;

  HqU32x2FromUint32( &tmp, ui32 );
  HqU32x2Add( pU32x2Result, pU32x2A, &tmp );
}


void HQNCALL Hq32x2SubtractUint32(Hq32x2 * p32x2Result, const Hq32x2 *p32x2A, uint32 ui32)
{
  Hq32x2        tmp;

  Hq32x2FromUint32( &tmp, ui32 );
  Hq32x2Subtract( p32x2Result, p32x2A, &tmp );
}

void HQNCALL HqU32x2SubtractUint32(HqU32x2 * pU32x2Result, const HqU32x2 *pU32x2A, uint32 ui32)
{
  HqU32x2       tmp;

  HqU32x2FromUint32( &tmp, ui32 );
  HqU32x2Subtract( pU32x2Result, pU32x2A, &tmp );
}


int32 HQNCALL Hq32x2CompareUint32(const Hq32x2 *p32x2A, uint32 ui32)
{
  Hq32x2        tmp;

  Hq32x2FromUint32( &tmp, ui32 );
  return Hq32x2Compare( p32x2A, &tmp );
}

int32 HQNCALL HqU32x2CompareUint32(const HqU32x2 *pU32x2A, uint32 ui32)
{
  HqU32x2        tmp;

  HqU32x2FromUint32( &tmp, ui32 );
  return HqU32x2Compare( pU32x2A, &tmp );
}


/*
 * Binary functions on a 64 bit value and a int32
 */

void HQNCALL Hq32x2AddInt32(Hq32x2 * p32x2Result, const Hq32x2 *p32x2A, int32 i32)
{
  Hq32x2        tmp;

  Hq32x2FromInt32( &tmp, i32 );
  Hq32x2Add( p32x2Result, p32x2A, &tmp );
}

void HQNCALL Hq32x2SubtractInt32(Hq32x2 * p32x2Result, const Hq32x2 *p32x2A, int32 i32)
{
  Hq32x2        tmp;

  Hq32x2FromInt32( &tmp, i32 );
  Hq32x2Subtract( p32x2Result, p32x2A, &tmp );
}

int32 HQNCALL Hq32x2CompareInt32(const Hq32x2 *p32x2A, int32 i32)
{
  Hq32x2        tmp;

  Hq32x2FromInt32( &tmp, i32 );
  return Hq32x2Compare( p32x2A, &tmp );
}


/*
* Log stripped */
