/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWsecurity!src:dngbits.c(EBDSDK_P.1) $
 *
* Log stripped */

#include "dongle.h"


uint32 getBitsN( uint32 InputValue, uint32 FirstBit, uint32 LastBit, uint32 BitsInValue )
{
  uint32 NumBits = (LastBit) - (FirstBit);
  uint32 OutputValue;

#ifndef ASSERT_BUILD
  UNUSED_PARAM(uint32, BitsInValue);
#endif

  HQASSERT( (NumBits) < BitsInValue, "GET_BITS_N: bad first/last bit values." );
  HQASSERT( (InputValue) < (1u << BitsInValue), "GET_BITS_N: InputValue overflows byte." );
  HQASSERT( (FirstBit) < BitsInValue, "GET_BITS_N: Bad first bit value." );
  HQASSERT( (LastBit) < BitsInValue, "GET_BITS_N: Bad last bit value." );

  (OutputValue) = ((InputValue) >> FirstBit) & ((1 << ((NumBits) + 1)) - 1);

  HQASSERT( (OutputValue) < (1u << BitsInValue), "GET_BITS_N: OutputValue overflows byte." );

  return OutputValue;
}

uint32 setBitsN( uint32 OutputValue, uint32 FirstBit, uint32 LastBit, uint32 BitsInValue, uint32 InputValue )
{
  uint32 Clear = ( ((1u << (1+(LastBit))) - 1) - ( ((1u << (FirstBit))) - 1) );

#ifndef ASSERT_BUILD
  UNUSED_PARAM(uint32, BitsInValue);
#endif

  HQASSERT( (LastBit) >= (FirstBit) && ((LastBit) - (FirstBit)) < BitsInValue, "SET_BITS_N: bad first/last bit values." );
  HQASSERT( (InputValue) < (1u << (1+((LastBit) - (FirstBit)))), "SET_BITS_N: InputValue overflows byte." );
  HQASSERT( (FirstBit) < BitsInValue, "SET_BITS_N: Bad first bit value." );
  HQASSERT( (LastBit) < BitsInValue, "SET_BITS_N: Bad last bit value." );

  (OutputValue) &= ~Clear;
  (OutputValue) |= ((InputValue) << (FirstBit));

  HQASSERT( (OutputValue) < (1u << BitsInValue), "SET_BITS_N: OutputValue overflows byte." );

  return OutputValue;
}


/* eof dngbits.c */
