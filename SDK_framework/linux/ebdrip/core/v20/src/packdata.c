/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:packdata.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utility functions to unpack data according 1,2,4,8,12 bits per
 * pixel into int32 containers. Also to pack int32 data samples
 * back down. Unpack routines produce interleaved data when given
 * planar - this code is initially for images but will eventually
 * be used in many over other places, eg functns.c.
 */


/* packdata.c */

#include "core.h"
#include "mm.h"
#include "mmcompat.h"
#include "caching.h"

#include "swerrors.h"
#include "objects.h"
#include "control.h"
#include "display.h"

#include "hqmemset.h"

#include "packdata.h"

/* ========================================================================== */
STATIC void unpack_1( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 7 ) & 1 ;
    ubuf[ 1 ] = ( tmp >> 6 ) & 1 ;
    ubuf[ 2 ] = ( tmp >> 5 ) & 1 ;
    ubuf[ 3 ] = ( tmp >> 4 ) & 1 ;
    ubuf[ 4 ] = ( tmp >> 3 ) & 1 ;
    ubuf[ 5 ] = ( tmp >> 2 ) & 1 ;
    ubuf[ 6 ] = ( tmp >> 1 ) & 1 ;
    ubuf[ 7 ] = ( tmp >> 0 ) & 1 ;
    ubuf += 8 ; 
    nbuff += 1 ;
    nconv -= 8 ;
  }
  if ( nconv >= 1 ) {
    uint8 tmp ;
    uint8 offset ;
    tmp = nbuff[ 0 ] ;
    offset = 8u ;
    do {
      offset -= 1 ;
      ubuf[ 0 ] = ( tmp >> offset ) & 1 ;
      ubuf += 1 ;
      nconv -= 1 ;
    } while ( nconv >= 1 ) ;
  }
}

STATIC void unpack_interleave_1( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 7 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 6 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 5 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 3 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 2 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 1 ) & 1 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 1 ;
    ubuf += ncomps ;
    nbuff += 1 ;
    nconv -= 8 ;
  }
  if ( nconv >= 1 ) {
    uint8 tmp ;
    uint8 offset ;
    tmp = nbuff[ 0 ] ;
    offset = 8u ;
    do {
      offset -= 1 ;
      ubuf[ 0 ] = ( tmp >> offset ) & 1 ;
      ubuf += ncomps ;
      nconv -= 1 ;
    } while ( nconv >= 1 ) ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void unpack_2( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 6 ) & 3 ;
    ubuf[ 1 ] = ( tmp >> 4 ) & 3 ;
    ubuf[ 2 ] = ( tmp >> 2 ) & 3 ;
    ubuf[ 3 ] = ( tmp >> 0 ) & 3 ;
    tmp = nbuff[ 1 ] ;
    ubuf[ 4 ] = ( tmp >> 6 ) & 3 ;
    ubuf[ 5 ] = ( tmp >> 4 ) & 3 ;
    ubuf[ 6 ] = ( tmp >> 2 ) & 3 ;
    ubuf[ 7 ] = ( tmp >> 0 ) & 3 ;
    ubuf += 8 ; 
    nbuff += 2 ;
    nconv -= 8 ;
  }
  if ( nconv >= 4 ) {
    uint8 tmp ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 6 ) & 3 ;
    ubuf[ 1 ] = ( tmp >> 4 ) & 3 ;
    ubuf[ 2 ] = ( tmp >> 2 ) & 3 ;
    ubuf[ 3 ] = ( tmp >> 0 ) & 3 ;
    ubuf += 4 ; 
    nbuff += 1 ;
    nconv -= 4 ;
  }
  if ( nconv >= 1 ) {
    uint8 tmp ;
    uint8 offset ;
    tmp = nbuff[ 0 ] ;
    offset = 8u ;
    do {
      offset -= 2 ;
      ubuf[ 0 ] = ( tmp >> offset ) & 3 ;
      ubuf += 1 ;
      nconv -= 1 ;
    } while ( nconv >= 1 ) ;
  }
}

STATIC void unpack_interleave_2( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 6 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 2 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 3 ;
    ubuf += ncomps ;
    tmp = nbuff[ 1 ] ;
    ubuf[ 0 ] = ( tmp >> 6 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 2 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 3 ;
    ubuf += ncomps ;
    nbuff += 2 ;
    nconv -= 8 ;
  }
  if ( nconv >= 4 ) {
    uint8 tmp ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 6 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 2 ) & 3 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 3 ;
    ubuf += ncomps ;
    nbuff += 1 ;
    nconv -= 4 ;
  }
  if ( nconv >= 1 ) {
    uint8 tmp ;
    uint8 offset ;
    tmp = nbuff[ 0 ] ;
    offset = 8u ;
    do {
      offset -= 2 ;
      ubuf[ 0 ] = ( tmp >> offset ) & 3 ;
      ubuf += ncomps ;
      nconv -= 1 ;
    } while ( nconv >= 1 ) ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void unpack_4( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf[ 1 ] = ( tmp >> 0 ) & 15 ;
    tmp = nbuff[ 1 ] ;
    ubuf[ 2 ] = ( tmp >> 4 ) & 15 ;
    ubuf[ 3 ] = ( tmp >> 0 ) & 15 ;
    tmp = nbuff[ 2 ] ;
    ubuf[ 4 ] = ( tmp >> 4 ) & 15 ;
    ubuf[ 5 ] = ( tmp >> 0 ) & 15 ;
    tmp = nbuff[ 3 ] ;
    ubuf[ 6 ] = ( tmp >> 4 ) & 15 ;
    ubuf[ 7 ] = ( tmp >> 0 ) & 15 ;
    ubuf += 8 ; 
    nbuff += 4 ;
    nconv -= 8 ;
  }
  while ( nconv >= 2 ) {
    uint8 tmp ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf[ 1 ] = ( tmp >> 0 ) & 15 ;
    ubuf += 2 ; 
    nbuff += 1 ;
    nconv -= 2 ;
  }
  if ( nconv == 1 ) {
    ubuf[ 0 ] = ( nbuff[ 0 ] >> 4 ) & 15 ;
  }
}

STATIC void unpack_interleave_4( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 15 ;
    ubuf += ncomps ;
    tmp = nbuff[ 1 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 15 ;
    ubuf += ncomps ;
    tmp = nbuff[ 2 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 15 ;
    ubuf += ncomps ;
    tmp = nbuff[ 3 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( tmp >> 0 ) & 15 ;
    ubuf += ncomps ;
    nbuff += 4 ;
    nconv -= 8 ;
  }
  while ( nconv >= 2 ) {
    uint8 tmp ;
    tmp = nbuff[ 0 ] ;
    ubuf[ 0 ] = ( tmp >> 4 ) & 15 ;
    ubuf += ncomps ; 
    ubuf[ 0 ] = ( tmp >> 0 ) & 15 ;
    ubuf += ncomps ; 
    nbuff += 1 ;
    nconv -= 2 ;
  }
  if ( nconv == 1 ) {
    ubuf[ 0 ] = ( nbuff[ 0 ] >> 4 ) & 15 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void unpack_8( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    ubuf[ 0 ] = nbuff[ 0 ] ;
    ubuf[ 1 ] = nbuff[ 1 ] ;
    ubuf[ 2 ] = nbuff[ 2 ] ;
    ubuf[ 3 ] = nbuff[ 3 ] ;
    ubuf[ 4 ] = nbuff[ 4 ] ;
    ubuf[ 5 ] = nbuff[ 5 ] ;
    ubuf[ 6 ] = nbuff[ 6 ] ;
    ubuf[ 7 ] = nbuff[ 7 ] ;
    ubuf += 8 ; 
    nbuff += 8 ;
    nconv -= 8 ;
  }
  while ( nconv >= 1 ) {
    ubuf[ 0 ] = nbuff[ 0 ] ;
    ubuf += 1 ; 
    nbuff += 1 ;
    nconv -= 1 ;
  }
}

STATIC void unpack_interleave_8( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    ubuf[ 0 ] = nbuff[ 0 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 1 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 2 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 3 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 4 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 5 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 6 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff[ 7 ] ;
    ubuf += ncomps ;
    nbuff += 8 ;
    nconv -= 8 ;
  }
  while ( nconv >= 1 ) {
    ubuf[ 0 ] = nbuff[ 0 ] ;
    ubuf += ncomps ;
    nbuff += 1 ;
    nconv -= 1 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void unpack_12( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[  1 ] ;
    ubuf[ 0 ] = ( nbuff[  0 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf[ 1 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  2 ] ;
    tmp = nbuff[  4 ] ;
    ubuf[ 2 ] = ( nbuff[  3 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf[ 3 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  5 ] ;
    tmp = nbuff[  7 ] ;
    ubuf[ 4 ] = ( nbuff[  6 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf[ 5 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  8 ] ;
    tmp = nbuff[ 10 ] ;
    ubuf[ 6 ] = ( nbuff[  9 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf[ 7 ] = ( ( tmp & 15 ) << 8 ) | nbuff[ 11 ] ;
    ubuf += 8 ;
    nbuff += 12 ;
    nconv -= 8 ;
  }
  while ( nconv >= 2 ) {
    uint8 tmp ;
    tmp = nbuff[  1 ] ;
    ubuf[ 0 ] = ( nbuff[  0 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf[ 1 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  2 ] ;
    ubuf += 2 ;
    nbuff += 3 ;
    nconv -= 2 ;
  }
  if ( nconv == 1 ) {
    ubuf[ 0 ] = ( nbuff[  0 ] << 4 ) | ( nbuff[  1 ] >> 4 ) ;
  }
}

STATIC void unpack_interleave_12( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    uint8 tmp ;
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    tmp = nbuff[  1 ] ;
    ubuf[ 0 ] = ( nbuff[  0 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  2 ] ;
    ubuf += ncomps ;
    tmp = nbuff[  4 ] ;
    ubuf[ 0 ] = ( nbuff[  3 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  5 ] ;
    ubuf += ncomps ;
    tmp = nbuff[  7 ] ;
    ubuf[ 0 ] = ( nbuff[  6 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  8 ] ;
    ubuf += ncomps ;
    tmp = nbuff[ 10 ] ;
    ubuf[ 0 ] = ( nbuff[  9 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( ( tmp & 15 ) << 8 ) | nbuff[ 11 ] ;
    ubuf += ncomps ;
    nbuff += 12 ;
    nconv -= 8 ;
  }
  while ( nconv >= 2 ) {
    uint8 tmp ;
    tmp = nbuff[  1 ] ;
    ubuf[ 0 ] = ( nbuff[  0 ] << 4 ) | ( tmp >> 4 ) ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( ( tmp & 15 ) << 8 ) | nbuff[  2 ] ;
    ubuf += ncomps ;
    nbuff += 3 ;
    nconv -= 2 ;
  }
  if ( nconv == 1 ) {
    ubuf[ 0 ] = ( nbuff[  0 ] << 4 ) | ( nbuff[  1 ] >> 4 ) ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void unpack_16( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    ubuf[ 0 ] = ( nbuff[  0 ] << 8 ) | nbuff[  1 ] ;
    ubuf[ 1 ] = ( nbuff[  2 ] << 8 ) | nbuff[  3 ] ;
    ubuf[ 2 ] = ( nbuff[  4 ] << 8 ) | nbuff[  5 ] ;
    ubuf[ 3 ] = ( nbuff[  6 ] << 8 ) | nbuff[  7 ] ;
    ubuf[ 4 ] = ( nbuff[  8 ] << 8 ) | nbuff[  9 ] ;
    ubuf[ 5 ] = ( nbuff[ 10 ] << 8 ) | nbuff[ 11 ] ;
    ubuf[ 6 ] = ( nbuff[ 12 ] << 8 ) | nbuff[ 13 ] ;
    ubuf[ 7 ] = ( nbuff[ 14 ] << 8 ) | nbuff[ 15 ] ;
    ubuf += 8 ; 
    nbuff += 16 ;
    nconv -= 8 ;
  }
  while ( nconv >= 1 ) {
    ubuf[ 0 ] = ( nbuff[  0 ] << 8 ) | nbuff[  1 ] ;
    ubuf += 1 ; 
    nbuff += 2 ;
    nconv -= 1 ;
  }
}

STATIC void unpack_native_16( uint8 *nbuff , int32 nconv , int32 *ubuf )
{
  uint16 *nbuff16 = ( uint16 * )nbuff ;

  while ( nconv >= 8 ) {
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    ubuf[ 0 ] = nbuff16[ 0 ] ;
    ubuf[ 1 ] = nbuff16[ 1 ] ;
    ubuf[ 2 ] = nbuff16[ 2 ] ;
    ubuf[ 3 ] = nbuff16[ 3 ] ;
    ubuf[ 4 ] = nbuff16[ 4 ] ;
    ubuf[ 5 ] = nbuff16[ 5 ] ;
    ubuf[ 6 ] = nbuff16[ 6 ] ;
    ubuf[ 7 ] = nbuff16[ 7 ] ;
    ubuf    += 8 ; 
    nbuff16 += 8 ;
    nconv   -= 8 ;
  }
  while ( nconv >= 1 ) {
    ubuf[ 0 ] = nbuff16[ 0 ] ;
    ubuf    += 1 ; 
    nbuff16 += 1 ;
    nconv   -= 1 ;
  }
}

STATIC void unpack_interleave_16( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  while ( nconv >= 8 ) {
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    ubuf[ 0 ] = ( nbuff[  0 ] << 8 ) | nbuff[  1 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[  2 ] << 8 ) | nbuff[  3 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[  4 ] << 8 ) | nbuff[  5 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[  6 ] << 8 ) | nbuff[  7 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[  8 ] << 8 ) | nbuff[  9 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[ 10 ] << 8 ) | nbuff[ 11 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[ 12 ] << 8 ) | nbuff[ 13 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = ( nbuff[ 14 ] << 8 ) | nbuff[ 15 ] ;
    ubuf += ncomps ;
    nbuff += 16 ;
    nconv -= 8 ;
  }
  while ( nconv >= 1 ) {
    ubuf[ 0 ] = ( nbuff[  0 ] << 8 ) | nbuff[  1 ] ;
    ubuf += ncomps ; 
    nbuff += 2 ;
    nconv -= 1 ;
  }
}

STATIC void unpack_interleave_native_16( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf )
{
  uint16 *nbuff16 = ( uint16 * )nbuff ;

  while ( nconv >= 8 ) {
    PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
    ubuf[ 0 ] = nbuff16[ 0 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 1 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 2 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 3 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 4 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 5 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 6 ] ;
    ubuf += ncomps ;
    ubuf[ 0 ] = nbuff16[ 7 ] ;
    ubuf += ncomps ;
    nbuff16 += 8 ;
    nconv   -= 8 ;
  }
  while ( nconv >= 1 ) {
    ubuf[ 0 ] = nbuff16[ 0 ] ;
    ubuf += ncomps ; 
    nbuff16 += 1 ;
    nconv   -= 1 ;
  }
}

/* ========================================================================== */
/* When packing into 1,2,4 bits per component, routines postfixed with */
/* 'align' are byte aligned on the plane boundary (ie ensures that data */
/* starting a plane begins in a new byte). */
/* all pack routines assume masking done on expansion so none is done */
/* on contraction. */

STATIC void pack_1( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  nconv *= ncomps ;

  while ( nconv >= 8 ) {
    int32 buf ;
    nconv -= 8 ;
    buf  = ( src[ 0 ] << 7 ) ;
    buf |= ( src[ 1 ] << 6 ) ;
    buf |= ( src[ 2 ] << 5 ) ;
    buf |= ( src[ 3 ] << 4 ) ;
    buf |= ( src[ 4 ] << 3 ) ;
    buf |= ( src[ 5 ] << 2 ) ;
    buf |= ( src[ 6 ] << 1 ) ;
    buf |= ( src[ 7 ] << 0 ) ;
    dst[ 0 ] = ( uint8 )buf ;
    dst += 1 ; src += 8 ;
  }
  if ( nconv >= 1 ) {
    int32 offset ;
    int32 buf ;
    offset = 8 ;
    buf = 0 ;
    do {
      offset -= 1 ;
      buf |= ( src[ 0 ] << offset ) ;
      src += 1 ;
      nconv -= 1 ;
    } while ( nconv >= 1 ) ;
    dst[ 0 ] = ( uint8 )buf ;
  }
}

STATIC void pack_1_align( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  int32 snconv ;

  snconv = nconv ;

  while ( ncomps >= 1 ) {
    ncomps -= 1 ;
    nconv = snconv ;
    while ( nconv >= 8 ) {
      int32 buf ;
      nconv -= 8 ;
      buf  = ( src[ 0 ] << 7 ) ;
      buf |= ( src[ 1 ] << 6 ) ;
      buf |= ( src[ 2 ] << 5 ) ;
      buf |= ( src[ 3 ] << 4 ) ;
      buf |= ( src[ 4 ] << 3 ) ;
      buf |= ( src[ 5 ] << 2 ) ;
      buf |= ( src[ 6 ] << 1 ) ;
      buf |= ( src[ 7 ] << 0 ) ;
      dst[ 0 ] = ( uint8 )buf ;
      dst += 1 ; src += 8 ;
    }
    if ( nconv >= 1 ) {
      int32 offset ;
      int32 buf ;
      offset = 8 ;
      buf = 0 ;
      do {
        offset -= 1 ;
        buf |= ( src[ 0 ] << offset ) ;
        src += 1 ;
        nconv -= 1 ;
      } while ( nconv >= 1 ) ;
      dst[ 0 ] = ( uint8 )buf ;
      dst += 1 ;
    }
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_2( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  nconv *= ncomps ;

  while ( nconv >= 4 ) {
    int32 buf ;
    nconv -= 4 ;
    buf  = ( src[ 0 ] << 6 ) ;
    buf |= ( src[ 1 ] << 4 ) ;
    buf |= ( src[ 2 ] << 2 ) ;
    buf |= ( src[ 3 ] << 0 ) ;
    dst[ 0 ] = ( uint8 )buf ;
    dst += 1 ; src += 4 ;
  }
  if ( nconv >= 1 ) {
    int32 offset ;
    int32 buf ;
    offset = 8 ;
    buf = 0 ;
    do {
      offset -= 2 ;
      buf |= ( src[ 0 ] << offset ) ;
      src += 1 ;
      nconv -= 1 ;
    } while ( nconv >= 1 ) ;
    dst[ 0 ] = ( uint8 )buf ;
  }
}

STATIC void pack_2_align( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  int32 snconv ;

  snconv = nconv ;

  while ( ncomps >= 1 ) {
    ncomps -= 1 ;
    nconv = snconv ;
    while ( nconv >= 4 ) {
      int32 buf ;
      nconv -= 4 ;
      buf  = ( src[ 0 ] << 6 ) ;
      buf |= ( src[ 1 ] << 4 ) ;
      buf |= ( src[ 2 ] << 2 ) ;
      buf |= ( src[ 3 ] << 0 ) ;
      dst[ 0 ] = ( uint8 )buf ;
      dst += 1 ; src += 4 ;
    }
    if ( nconv >= 1 ) {
      int32 offset ;
      int32 buf ;
      offset = 8 ;
      buf = 0 ;
      do {
        offset -= 2 ;
        buf |= ( src[ 0 ] << offset ) ;
        src += 1 ;
        nconv -= 1 ;
      } while ( nconv >= 1 ) ;
      dst[ 0 ] = ( uint8 )buf ;
      dst += 1 ;
    }
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_4( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  nconv *= ncomps ;

  while ( nconv >= 16 ) {
    nconv -= 16 ;
    PENTIUM_CACHE_LOAD( dst + 7 ) ;
    dst[ 0 ]  = ( uint8 )(( src[  0 ] << 4 ) | src[  1 ] ) ;
    dst[ 1 ]  = ( uint8 )(( src[  2 ] << 4 ) | src[  3 ] ) ;
    dst[ 2 ]  = ( uint8 )(( src[  4 ] << 4 ) | src[  5 ] ) ;
    dst[ 3 ]  = ( uint8 )(( src[  6 ] << 4 ) | src[  7 ] ) ;
    dst[ 4 ]  = ( uint8 )(( src[  8 ] << 4 ) | src[  9 ] ) ;
    dst[ 5 ]  = ( uint8 )(( src[ 10 ] << 4 ) | src[ 11 ] ) ;
    dst[ 6 ]  = ( uint8 )(( src[ 12 ] << 4 ) | src[ 13 ] ) ;
    dst[ 7 ]  = ( uint8 )(( src[ 14 ] << 4 ) | src[ 15 ] ) ;
    dst += 8 ; src += 16 ;
  }
  while ( nconv >= 2 ) {
    nconv -= 2 ;
    dst[ 0 ]  = ( uint8 )(( src[ 0 ] << 4 ) | src[ 1 ] ) ;
    dst += 1 ; src += 2 ;
  }
  if ( nconv == 1 ) {
    dst[ 0 ]  = ( uint8 )( src[ 0 ] << 4 ) ;
  }
}

STATIC void pack_4_align( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  int32 snconv ;

  snconv = nconv ;

  while ( ncomps >= 1 ) {
    ncomps -= 1 ;
    nconv = snconv ;
    while ( nconv >= 16 ) {
      nconv -= 16 ;
      PENTIUM_CACHE_LOAD( dst + 7 ) ;
      dst[ 0 ]  = ( uint8 )(( src[  0 ] << 4 ) | src[  1 ] ) ;
      dst[ 1 ]  = ( uint8 )(( src[  2 ] << 4 ) | src[  3 ] ) ;
      dst[ 2 ]  = ( uint8 )(( src[  4 ] << 4 ) | src[  5 ] ) ;
      dst[ 3 ]  = ( uint8 )(( src[  6 ] << 4 ) | src[  7 ] ) ;
      dst[ 4 ]  = ( uint8 )(( src[  8 ] << 4 ) | src[  9 ] ) ;
      dst[ 5 ]  = ( uint8 )(( src[ 10 ] << 4 ) | src[ 11 ] ) ;
      dst[ 6 ]  = ( uint8 )(( src[ 12 ] << 4 ) | src[ 13 ] ) ;
      dst[ 7 ]  = ( uint8 )(( src[ 14 ] << 4 ) | src[ 15 ] ) ;
      dst += 8 ; src += 16 ;
    }
    while ( nconv >= 2 ) {
      nconv -= 2 ;
      dst[ 0 ]  = ( uint8 )(( src[ 0 ] << 4 ) | src[ 1 ] ) ;
      dst += 1 ; src += 2 ;
    }
    if ( nconv == 1 ) {
      dst[ 0 ]  = ( uint8 )( src[ 0 ] << 4 ) ;
      dst += 1 ; src += 1 ;
    }
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_8( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  nconv *= ncomps ;

  while ( nconv >= 8 ) {
    nconv -= 8 ;
    PENTIUM_CACHE_LOAD( dst + 7 ) ;
    dst[ 0 ] = ( uint8 ) src[ 0 ] ;
    dst[ 1 ] = ( uint8 ) src[ 1 ] ;
    dst[ 2 ] = ( uint8 ) src[ 2 ] ;
    dst[ 3 ] = ( uint8 ) src[ 3 ] ;
    dst[ 4 ] = ( uint8 ) src[ 4 ] ;
    dst[ 5 ] = ( uint8 ) src[ 5 ] ;
    dst[ 6 ] = ( uint8 ) src[ 6 ] ;
    dst[ 7 ] = ( uint8 ) src[ 7 ] ;
    dst += 8 ; src += 8 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst[ 0 ] = ( uint8 ) src[ 0 ] ;
    dst += 1 ; src += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_12( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  nconv *= ncomps ;

  while ( nconv >= 8 ) {
    nconv -= 8 ;
    PENTIUM_CACHE_LOAD( dst + 11 ) ;
    dst[  0 ] = ( uint8 )(  src[ 0 ] >> 4 ) ;
    dst[  1 ] = ( uint8 )(( src[ 0 ] << 4 ) | ( src[ 1 ] >> 8 )) ;
    dst[  2 ] = ( uint8 )(  src[ 1 ] << 0 ) ;
    dst[  3 ] = ( uint8 )(  src[ 2 ] >> 4 ) ;
    dst[  4 ] = ( uint8 )(( src[ 2 ] << 4 ) | ( src[ 3 ] >> 8 )) ;
    dst[  5 ] = ( uint8 )(  src[ 3 ] << 0 ) ;
    dst[  6 ] = ( uint8 )(  src[ 4 ] >> 4 ) ;
    dst[  7 ] = ( uint8 )(( src[ 4 ] << 4 ) | ( src[ 5 ] >> 8 )) ;
    dst[  8 ] = ( uint8 )(  src[ 5 ] << 0 ) ;
    dst[  9 ] = ( uint8 )(  src[ 6 ] >> 4 ) ;
    dst[ 10 ] = ( uint8 )(( src[ 6 ] << 4 ) | ( src[ 7 ] >> 8 )) ;
    dst[ 11 ] = ( uint8 )(  src[ 7 ] << 0 ) ;
    dst += 12 ; src += 8 ;
  }
  while ( nconv >= 2 ) {
    nconv -= 2 ;
    dst[ 0 ] = ( uint8 )(  src[ 0 ] >> 4 ) ;
    dst[ 1 ] = ( uint8 )(( src[ 0 ] << 4 ) | ( src[ 1 ] >> 8 )) ;
    dst[ 2 ] = ( uint8 )(  src[ 1 ] << 0 ) ;
    dst += 3 ; src += 2 ;
  }
  if ( nconv == 1 ) {
    dst[ 0 ] = ( uint8 )( src[ 0 ] >> 4 ) ;
    dst[ 1 ] = ( uint8 )( src[ 0 ] << 4 ) ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_16( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  uint16 *dst16 ;

  nconv *= ncomps ;

  dst16 = ( uint16 * ) dst ;

  while ( nconv >= 8 ) {
    nconv -= 8 ;
    PENTIUM_CACHE_LOAD( dst16 + 7 ) ;
    dst16[ 0 ] = ( uint16 ) src[ 0 ] ;
    dst16[ 1 ] = ( uint16 ) src[ 1 ] ;
    dst16[ 2 ] = ( uint16 ) src[ 2 ] ;
    dst16[ 3 ] = ( uint16 ) src[ 3 ] ;
    dst16[ 4 ] = ( uint16 ) src[ 4 ] ;
    dst16[ 5 ] = ( uint16 ) src[ 5 ] ;
    dst16[ 6 ] = ( uint16 ) src[ 6 ] ;
    dst16[ 7 ] = ( uint16 ) src[ 7 ] ;
    dst16 += 8 ; src += 8 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst16[ 0 ] = ( uint16 ) src[ 0 ] ;
    dst16 += 1 ; src += 1 ;
  }
}

/* ========================================================================== */
/* pack_pad routines pack 1,2,4 bit per component pixels into 4,8,16 containers. */
/* pack_M_pad_N: M is bits per component; N is container size. */
/* eg. 1 bit rgb -> _rgb uses pack_1_pad_4 */
/* If no padding is required, expect to use ordinary pack routines. */

STATIC void pack_1_pad_4( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  HQASSERT( ncomps == 3 , "pack_1_pad_4: ncomps must be 3" ) ;
  UNUSED_PARAM( int32 , ncomps ) ;

  while ( nconv >= 2 ) {
    int32 buf ;
    nconv -= 2 ;
    buf  = ( src[ 0 ] << 6 ) ;
    buf |= ( src[ 1 ] << 5 ) ;
    buf |= ( src[ 2 ] << 4 ) ;
    buf |= ( src[ 3 ] << 2 ) ;
    buf |= ( src[ 4 ] << 1 ) ;
    buf |= ( src[ 5 ] << 0 ) ;
    dst[ 0 ] = ( uint8 )buf ;
    dst += 1 ; src += 6 ;
  }
  if ( nconv == 1 ) {
    int32 buf ;
    buf  = ( src[ 0 ] << 6 ) ;
    buf |= ( src[ 1 ] << 5 ) ;
    buf |= ( src[ 2 ] << 4 ) ;
    dst[ 0 ] = ( uint8 )buf ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_1_pad_8( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  HQASSERT( nconv > 0 , "pack_1_pad_8: nconv <= 0" ) ;

  src += ncomps - 1 ;

  while ( nconv > 0 ) {
    int32 buf ;
    nconv -= 1 ;
    buf = 0 ;
    switch( ncomps ) {
    case 7 :
      buf |= ( src[ -6 ] << 6 ) ;
    case 6 :
      buf |= ( src[ -5 ] << 5 ) ;
    case 5 :
      break ;
    default :
      HQFAIL( "pack_1_pad_8: ncomps must be 5,6,7" ) ;
    }
    buf |= ( src[ -4 ] << 4 ) ;
    buf |= ( src[ -3 ] << 3 ) ;
    buf |= ( src[ -2 ] << 2 ) ;
    buf |= ( src[ -1 ] << 1 ) ;
    buf |= ( src[  0 ] << 0 ) ;
    dst[ 0 ] = ( uint8 )buf ;
    dst += 1 ; src += ncomps ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_1_pad_16( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  uint16 *dst16 ;

  HQASSERT( nconv > 0 , "pack_1_pad_16: nconv <= 0" ) ;

  dst16 = ( uint16 * ) dst ;

  src += ncomps - 1 ;

  while ( nconv > 0 ) {
    int32 buf ;
    nconv -= 1 ;
    buf = 0 ;
    switch( ncomps ) {
    case 15 :
      buf |= ( src[ -14 ] << 14 ) ;
    case 14 :
      buf |= ( src[ -13 ] << 13 ) ;
    case 13 :
      buf |= ( src[ -12 ] << 12 ) ;
    case 12 :
      buf |= ( src[ -11 ] << 11 ) ;
    case 11 :
      buf |= ( src[ -10 ] << 10 ) ;
    case 10 :
      buf |= ( src[  -9 ] <<  9 ) ;
    case 9 :
      break ;
    default :
      HQFAIL( "pack_1_pad_16: ncomps must be 9,10,11,12,13,14,15" ) ;
    }
    buf |= ( src[  -8 ] <<  8 ) ;
    buf |= ( src[  -7 ] <<  7 ) ;
    buf |= ( src[  -6 ] <<  6 ) ;
    buf |= ( src[  -5 ] <<  5 ) ;
    buf |= ( src[  -4 ] <<  4 ) ;
    buf |= ( src[  -3 ] <<  3 ) ;
    buf |= ( src[  -2 ] <<  2 ) ;
    buf |= ( src[  -1 ] <<  1 ) ;
    buf |= ( src[   0 ] <<  0 ) ;
    dst16[ 0 ] = ( uint16 )buf ;
    dst16 += 1 ; src += ncomps ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_2_pad_8( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  HQASSERT( ncomps == 3 , "pack_2_pad_8: ncomps must equal 3" ) ;
  UNUSED_PARAM( int32 , ncomps ) ;

  while ( nconv >= 2 ) {
    int32 buf ;
    nconv -= 2 ;
    buf  = ( src[ 0 ] << 4 ) ;
    buf |= ( src[ 1 ] << 2 ) ;
    buf |= ( src[ 2 ] << 0 ) ;
    dst[ 0 ] = ( uint8 )buf ;
    buf  = ( src[ 3 ] << 4 ) ;
    buf |= ( src[ 4 ] << 2 ) ;
    buf |= ( src[ 5 ] << 0 ) ;
    dst[ 1 ] = ( uint8 )buf ;
    dst += 2 ; src += 6 ;
  }
  if ( nconv == 1 ) {
    int32 buf ;
    buf  = ( src[ 0 ] << 4 ) ;
    buf |= ( src[ 1 ] << 2 ) ;
    buf |= ( src[ 2 ] << 0 ) ;
    dst[ 0 ] = ( uint8 )buf ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_2_pad_16( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  uint16 *dst16 ;

  HQASSERT( nconv > 0 , "pack_2_pad_16: nconv <= 0" ) ;

  dst16 = ( uint16 * ) dst ;

  src += ncomps - 1 ;

  while ( nconv > 0 ) {
    int32 buf ;
    nconv -= 1 ;
    buf = 0 ;
    switch( ncomps ) {
    case 7 :
      buf |= ( src[ -6 ] << 12 ) ;
    case 6 :
      buf |= ( src[ -5 ] << 10 ) ;
    case 5 :
      break ;
    default :
      HQFAIL( "pack_2_pad_16: ncomps must be 5,6,7" ) ;
    }
    buf |= ( src[ -4 ] <<  8 ) ;
    buf |= ( src[ -3 ] <<  6 ) ;
    buf |= ( src[ -2 ] <<  4 ) ;
    buf |= ( src[ -1 ] <<  2 ) ;
    buf |= ( src[  0 ] <<  0 ) ;
    dst16[ 0 ] = ( uint16 )buf ;
    dst16 += 1 ; src += ncomps ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void pack_4_pad_16( int32 * src , int32 ncomps , int32 nconv , uint8 *dst )
{
  uint16 *dst16 ;

  HQASSERT( ncomps == 3 , "pack_4_pad_16: ncomps must be 3" ) ;
  UNUSED_PARAM( int32 , ncomps ) ;

  dst16 = ( uint16 * ) dst ;

  while ( nconv >= 2 ) {
    int32 buf ;
    nconv -= 2 ;
    buf  = ( src[ 0 ] << 8 ) ;
    buf |= ( src[ 1 ] << 4 ) ;
    buf |= ( src[ 2 ] << 0 ) ;
    dst16[ 0 ] = ( uint16 )buf ;
    buf  = ( src[ 3 ] << 8 ) ;
    buf |= ( src[ 4 ] << 4 ) ;
    buf |= ( src[ 5 ] << 0 ) ;
    dst16[ 1 ] = ( uint16 )buf ;
    dst16 += 2 ; src += 6 ;
  }
  if ( nconv == 1 ) {
    int32 buf ;
    buf  = ( src[ 0 ] << 8 ) ;
    buf |= ( src[ 1 ] << 4 ) ;
    buf |= ( src[ 2 ] << 0 ) ;
    dst16[ 0 ] = ( uint16 )buf ;
  }
}

/* ========================================================================== */
STATIC void planar_1( int32 *src , int32 nconv , int32 *dst )
{
  while ( nconv >= 8 ) {
    nconv -= 8 ;
    PENTIUM_CACHE_LOAD( dst + 7 ) ;
    dst[ 0 ] = src[ 0 ] ;
    dst[ 1 ] = src[ 1 ] ;
    dst[ 2 ] = src[ 2 ] ;
    dst[ 3 ] = src[ 3 ] ;
    dst[ 4 ] = src[ 4 ] ;
    dst[ 5 ] = src[ 5 ] ;
    dst[ 6 ] = src[ 6 ] ;
    dst[ 7 ] = src[ 7 ] ;
    dst += 8 ;
    src += 8 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst[ 0 ] = src[ 0 ] ;
    dst += 1 ; src += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void planar_2( int32 *src , int32 nconv , int32 *dst , int32 offset )
{
  int32 *dst1 = dst ;
  int32 *dst2 = dst1 + offset ;
  
  while ( nconv >= 4 ) {
    nconv -= 4 ;
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst1[ 1 ] = src[ 2 ] ;
    dst2[ 1 ] = src[ 3 ] ;
    dst1[ 2 ] = src[ 4 ] ;
    dst2[ 2 ] = src[ 5 ] ;
    dst1[ 3 ] = src[ 6 ] ;
    dst2[ 3 ] = src[ 7 ] ;
    dst1 += 4 ;
    dst2 += 4 ;
    src += 8 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    src += 2 ;
  }
}

STATIC void planar_2N( int32 *src , int32 nconv , int32 *dst , int32 offset , int32 ncomps )
{
  int32 *dst1 = dst ;
  int32 *dst2 = dst1 + offset ;
  
  while ( nconv >= 4 ) {
    nconv -= 4 ;
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ; src += ncomps ;
    dst1[ 1 ] = src[ 0 ] ;
    dst2[ 1 ] = src[ 1 ] ; src += ncomps ;
    dst1[ 2 ] = src[ 0 ] ;
    dst2[ 2 ] = src[ 1 ] ; src += ncomps ;
    dst1[ 3 ] = src[ 0 ] ;
    dst2[ 3 ] = src[ 1 ] ; src += ncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ; src += ncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void planar_3( int32 *src , int32 nconv , int32 *dst , int32 offset )
{
  int32 *dst1 = dst ;
  int32 *dst2 = dst1 + offset ;
  int32 *dst3 = dst2 + offset ;
  
  while ( nconv >= 4 ) {
    nconv -= 4 ;
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = src[  0 ] ;
    dst2[ 0 ] = src[  1 ] ;
    dst3[ 0 ] = src[  2 ] ;
    dst1[ 1 ] = src[  3 ] ;
    dst2[ 1 ] = src[  4 ] ;
    dst3[ 1 ] = src[  5 ] ;
    dst1[ 2 ] = src[  6 ] ;
    dst2[ 2 ] = src[  7 ] ;
    dst3[ 2 ] = src[  8 ] ;
    dst1[ 3 ] = src[  9 ] ;
    dst2[ 3 ] = src[ 10 ] ;
    dst3[ 3 ] = src[ 11 ] ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
    src += 12 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst3[ 0 ] = src[ 2 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    src += 3 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void planar_3N( int32 *src , int32 nconv , int32 *dst , int32 offset , int32 ncomps )
{
  int32 *dst1 = dst ;
  int32 *dst2 = dst1 + offset ;
  int32 *dst3 = dst2 + offset ;
  
  while ( nconv >= 4 ) {
    nconv -= 4 ;
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst3[ 0 ] = src[ 2 ] ; src += ncomps ;
    dst1[ 1 ] = src[ 0 ] ;
    dst2[ 1 ] = src[ 1 ] ;
    dst3[ 1 ] = src[ 2 ] ; src += ncomps ;
    dst1[ 2 ] = src[ 0 ] ;
    dst2[ 2 ] = src[ 1 ] ;
    dst3[ 2 ] = src[ 2 ] ; src += ncomps ;
    dst1[ 3 ] = src[ 0 ] ;
    dst2[ 3 ] = src[ 1 ] ;
    dst3[ 3 ] = src[ 2 ] ; src += ncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst3[ 0 ] = src[ 2 ] ; src += ncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
STATIC void planar_4( int32 *src , int32 nconv , int32 *dst , int32 offset )
{
  int32	*dst1 = dst ;
  int32	*dst2 = dst1 + offset ;
  int32	*dst3 = dst2 + offset ;
  int32	*dst4 = dst3 + offset ;
  
  while ( nconv  >= 3 ) {
    nconv -= 3 ;
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = src[  0 ] ;
    dst2[ 0 ] = src[  1 ] ;
    dst3[ 0 ] = src[  2 ] ;
    dst4[ 0 ] = src[  3 ] ;
    dst1[ 1 ] = src[  4 ] ;
    dst2[ 1 ] = src[  5 ] ;
    dst3[ 1 ] = src[  6 ] ;
    dst4[ 1 ] = src[  7 ] ;
    dst1[ 2 ] = src[  8 ] ;
    dst2[ 2 ] = src[  9 ] ;
    dst3[ 2 ] = src[ 10 ] ;
    dst4[ 2 ] = src[ 11 ] ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
    src += 12 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst3[ 0 ] = src[ 2 ] ;
    dst4[ 0 ] = src[ 3 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
    src += 4 ;
  }
}

STATIC void planar_4N( int32 *src , int32 nconv , int32 *dst , int32 offset , int32 ncomps )
{
  int32	*dst1 = dst ;
  int32	*dst2 = dst1 + offset ;
  int32	*dst3 = dst2 + offset ;
  int32	*dst4 = dst3 + offset ;
  
  while ( nconv >= 3 ) {
    nconv -= 3 ;
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst3[ 0 ] = src[ 2 ] ;
    dst4[ 0 ] = src[ 3 ] ; src += ncomps ;
    dst1[ 1 ] = src[ 0 ] ;
    dst2[ 1 ] = src[ 1 ] ;
    dst3[ 1 ] = src[ 2 ] ;
    dst4[ 1 ] = src[ 3 ] ; src += ncomps ;
    dst1[ 2 ] = src[ 0 ] ;
    dst2[ 2 ] = src[ 1 ] ;
    dst3[ 2 ] = src[ 2 ] ;
    dst4[ 2 ] = src[ 3 ] ; src += ncomps ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
  }
  while ( nconv >= 1 ) {
    nconv -= 1 ;
    dst1[ 0 ] = src[ 0 ] ;
    dst2[ 0 ] = src[ 1 ] ;
    dst3[ 0 ] = src[ 2 ] ;
    dst4[ 0 ] = src[ 3 ] ; src += ncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
  }
}

/* ========================================================================== */
struct PACKDATA {
  mm_pool_t *pools;
  int32 *ubuf ;
  uint8 *pbuf ;
  int32 *ebuf ;
  int32 usize ;
  int32 psize ;
  int32 esize ;
  int32 ncomps ;
  int32 nconv ;
  int32 nconvp ;
  int32 bpp ;
  int32 use ;
  int32 interleaved ;
  void (*packproc)( int32 * , int32 , int32 , uint8 * ) ;
  void (*unpackfn)( uint8 *nbuff , int32 nconv , int32 *ubuf ) ;
  void (*unpackfnN)( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf ) ;
} ;

#define upk_void NULL

STATIC void (*unpackfns[])( uint8 *nbuff , int32 nconv , int32 *ubuf ) = {
  upk_void ,
  unpack_1 ,     unpack_2 ,    upk_void , unpack_4 ,
  upk_void ,     upk_void ,    upk_void , unpack_8 ,
  upk_void ,     upk_void ,    upk_void , unpack_12 ,
  upk_void ,     upk_void ,    upk_void , unpack_16
} ;

STATIC void (*unpackfnsN[])( uint8 *nbuff , int32 nconv , int32 ncomps , int32 *ubuf ) = {
  upk_void            ,
  unpack_interleave_1 , unpack_interleave_2 , upk_void , unpack_interleave_4 ,
  upk_void            , upk_void            , upk_void , unpack_interleave_8 ,
  upk_void            , upk_void            , upk_void , unpack_interleave_12 ,
  upk_void            , upk_void            , upk_void , unpack_interleave_16
} ;

/* -------------------------------------------------------------------------- */
PACKDATA *pd_packdataopen( mm_pool_t *pools,
                           int32 ncomps , int32 nconv , int32 bpp ,
                           int32 interleaved , int32 pad ,
                           int32 pack12into16 , int32 use )
{
  PACKDATA *pd ;
  int32 usize ;
  int32 psize ;
  int32 esize ;

  HQASSERT( ncomps > 0 , "ncomps must be > 0" ) ;
  HQASSERT( nconv > 0 , "nconv must be > 0" ) ;
  HQASSERT( bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 12 || bpp == 16,
            "bpp must be 1,2,4,8,12,16" ) ;
  HQASSERT( interleaved == TRUE || interleaved == FALSE ,
            "interleaved must be TRUE or FALSE" ) ;
  HQASSERT( pad == TRUE || pad == FALSE ,
            "pad must be TRUE or FALSE" ) ;
  HQASSERT( pack12into16 == TRUE || pack12into16 == FALSE ,
            "pack12into16 must be TRUE or FALSE" ) ;

  pd = ( PACKDATA * )
    dl_alloc(pools, sizeof(PACKDATA), MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( pd == NULL ) {
    ( void )error_handler( VMERROR ) ;
    return ( PACKDATA * ) NULL ;
  }

  usize = nconv * ncomps * sizeof( int32 ) ;
  HQASSERT( usize > 0 , "usize should be > 0" ) ;
  psize = 0 ;
  esize = usize ;

  pd->pools = pools;
  pd->ubuf  = ( int32 * ) NULL ;
  pd->pbuf  = ( uint8 * ) NULL ;
  pd->ebuf  = ( int32 * ) NULL ;
  
  pd->usize = usize ;
  pd->psize = psize ;
  pd->esize = esize ;

  pd->unpackfn = NULL ;
  pd->unpackfnN = NULL ;

  if ( ( use & PD_UNPACK ) != 0 ) {
    /* buffer for unpacked data */
    pd->ubuf = ( int32 * )
      dl_alloc(pools, usize , MM_ALLOC_CLASS_IMAGE_CONVERT);
    if ( pd->ubuf == NULL ) {
      pd_packdatafree( pd ) ;
      ( void )error_handler( VMERROR ) ;
      return ( PACKDATA * ) NULL ;
    }
    
    if ( ( use & PD_CLEAR ) != 0 )
      HqMemZero( ( uint8 * )pd->ubuf , usize ) ;

    if ( interleaved || ncomps == 1 ) {
      HQASSERT( bpp > 0 &&
                bpp < sizeof( unpackfns ) / sizeof( unpackfns[ 0 ] ) ,
                "bpp out of range" ) ;
      if ( ( use & PD_NATIVE ) != 0 && bpp == 16 )
        pd->unpackfn = unpack_native_16 ;
      else
        pd->unpackfn = unpackfns[ bpp ] ;
      HQASSERT( pd->unpackfn , "illegal value for bpp" ) ;
    }
    else {
      HQASSERT( bpp > 0 &&
                bpp < sizeof( unpackfnsN ) / sizeof( unpackfnsN[ 0 ] ) ,
                "bpp out of range" ) ;
      if ( ( use & PD_NATIVE ) != 0 && bpp == 16 )
        pd->unpackfnN = unpack_interleave_native_16 ;
      else
        pd->unpackfnN = unpackfnsN[ bpp ] ;
      HQASSERT( pd->unpackfnN , "unpack function null" ) ;
    }
  }

  pd->packproc = NULL ;

  if ( ( use & PD_PACK ) != 0 ) {
    int32 tbpp , tncomps ;

    tbpp = bpp ;
    if ( bpp == 12 && pack12into16 )
      tbpp = 16 ; /* pack 12 bpp into 16 */

    tncomps = ncomps ;
    if ( pad ) {
      /* round ncomps up to next container size */
      switch ( ncomps ) {
      case 3 :
        tncomps = 4 ;
        break ;
      case 5 : case 6 : case 7 :
        tncomps = 8 ;
        break ;
      case 9 : case 10 : case 11 : case 12 : case 13 : case 14 : case 15 :
        tncomps = 16 ;
        break ;
      default :
        HQFAIL( "Unexpected ncomps when pad == TRUE" ) ;
      }
    }

    /* Note first and third are actually the same, but the formulae
     * reflect the code so one can easily determine correctness.
     */
    if ( pad )
      psize = ( nconv * tbpp * tncomps + 7 ) >> 3 ;
    else if ( ( use & PD_ALIGN ) != 0 )
      psize = ncomps * (( nconv * tbpp + 7 ) >> 3 ) ;
    else
      psize = ( nconv * ncomps * tbpp + 7 ) >> 3 ;

    pd->psize = psize ;

    /* buffer for packed data */
    pd->pbuf = ( uint8 * )
      dl_alloc(pools, psize, MM_ALLOC_CLASS_IMAGE_CONVERT);
    if ( pd->pbuf == NULL ) {
      pd_packdatafree( pd ) ;
      ( void )error_handler( VMERROR ) ;
      return ( PACKDATA * ) NULL ;
    }

    switch ( tbpp ) {
    case 1 :
      if ( pad ) {
        switch ( ncomps ) {
        case 3 :
          pd->packproc = pack_1_pad_4 ;
          break ;
        case 5 : case 6 : case 7 :
          pd->packproc = pack_1_pad_8 ;
          break ;
        case 9 : case 10 : case 11 : case 12 : case 13 : case 14 : case 15 :
          pd->packproc = pack_1_pad_16 ;
          break ;
        }
      }
      else if ( ( use & PD_ALIGN ) != 0 )
        pd->packproc = pack_1_align ;
      else
        pd->packproc = pack_1 ;
      break ;
    case 2 :
      if ( pad ) {
        switch( ncomps ) {
        case 3 :
          pd->packproc = pack_2_pad_8 ;
          break ;
        case 5 : case 6 : case 7 :
          pd->packproc = pack_2_pad_16 ;
          break ;
        }
      }
      else if ( ( use & PD_ALIGN ) != 0 )
        pd->packproc = pack_2_align ;
      else
        pd->packproc = pack_2 ;
      break ;
    case 4 :
      if ( pad ) {
        if ( ncomps == 3 )
          pd->packproc = pack_4_pad_16 ;
      }
      else if ( ( use & PD_ALIGN ) != 0 )
        pd->packproc = pack_4_align ;
      else
        pd->packproc = pack_4 ;
      break ;
    case 8 :
      pd->packproc = pack_8 ;
      break ;
    case 12:
      pd->packproc = pack_12 ;
      break ;
    case 16 : /* 12 into 16 */
      pd->packproc = pack_16 ;
      break ;
    }
    HQASSERT( pd->packproc ,
        "packproc NULL: unexpected combination of bpp, ncomps, pad" ) ;
  }

  if ( ( use & PD_PLANAR ) != 0 ) {
    /* Need this extra buffer if doing planarization */
    pd->ebuf = ( int32 * )
      dl_alloc(pools, esize, MM_ALLOC_CLASS_IMAGE_CONVERT);
    if ( pd->ebuf == NULL ) {
      pd_packdatafree( pd ) ;
      ( void )error_handler( VMERROR ) ;
      return ( PACKDATA * ) NULL ;
    }
  }

  pd->ncomps = ncomps ;
  pd->nconv = nconv ;
  pd->nconvp = 0 ;
  pd->bpp = bpp ;
  pd->use = use ;
  pd->interleaved = interleaved ;

  return pd ;
}

int32 pd_packdatasize( PACKDATA *pd )
{
  HQASSERT( pd , "pd_packdatasize: pd is null" ) ;
  
  return pd->psize ;
}

void pd_packdatafree( PACKDATA *pd )
{
  HQASSERT( pd , "pd_packdatafree: pd is null" ) ;
  if ( pd->ubuf != NULL )
    dl_free(pd->pools, (mm_addr_t)pd->ubuf, pd->usize,
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( pd->pbuf != NULL )
    dl_free(pd->pools, (mm_addr_t)pd->pbuf, pd->psize,
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( pd->ebuf != NULL )
    dl_free(pd->pools, (mm_addr_t)pd->ebuf, pd->esize,
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  dl_free(pd->pools, (mm_addr_t)pd , sizeof(PACKDATA),
          MM_ALLOC_CLASS_IMAGE_CONVERT);
}

/* -------------------------------------------------------------------------- */
/**
 * Use pd_unpack() or pd_unpack_icomp() wrappers (defined in the header) instead
 * of calling pd_unpackdata() directly.
 */
void pd_unpackdata( PACKDATA *pd ,
                    uint8 *pbuffs[] , int32 nbuffs , int32 icomp ,
                    int32 **rbuf , int32 tconv )
{
  int32 *tubuf ;

  HQASSERT( pd , "pd NULL" ) ;
  HQASSERT( pbuffs , "pbuffs NULL" ) ;
  HQASSERT( nbuffs >= 1 , "nbuffs < 1" ) ;
  HQASSERT( rbuf , "rbuf NULL" ) ;
  HQASSERT( ( tconv > 0 ) && (( tconv + pd->nconvp ) <= pd->nconv ) ,
            "tconv out of range" ) ;
  HQASSERT( icomp == -1 || !pd->interleaved , "Do not expect interleaved data" ) ;

  /* Note that for the time being 16bpp is a special case in that the pack
   * routine packs data according to the endian-ness of the architecture,
   * rather than enforcing highbyte first. So we need an unpack routine to
   * cope with this, as well as one to explicitly unpack 16bpp highbyte
   * first (which is how PS supplies the data).
   */

  if ( pd->interleaved || pd->ncomps == 1 ) {
    tubuf = ( pd->ubuf + pd->nconvp ) ;
    (*pd->unpackfn)( pbuffs[ 0 ] , tconv * pd->ncomps , tubuf ) ;
  }
  else {
    int32 i ;

    if ( icomp != -1 ) {
      /* Unpack one component at a time. */
      HQASSERT( nbuffs == 1 , "Should be doing one buf at a time" ) ;
      HQASSERT( 0 <= icomp && icomp < pd->ncomps , "icomp out of range" ) ;
      if ( pbuffs[ 0 ] ) {
        tubuf = ( pd->ubuf + ( pd->nconvp * pd->ncomps )) + icomp ;
        (*pd->unpackfnN)( pbuffs[ 0 ] , tconv , pd->ncomps , tubuf ) ;
      }
    }
    else if ( nbuffs == 1 ) {
      /* All components planarised in the same buffer. */
      int32 offset = (( tconv * pd->bpp ) + 7 ) >> 3 ;
      uint8 *pbuf = pbuffs[ 0 ] ;
      tubuf = ( pd->ubuf + ( pd->nconvp * pd->ncomps )) ;
      for ( i = 0 ; i < pd->ncomps ; ++i ) {
        (*pd->unpackfnN)( pbuf , tconv , pd->ncomps , tubuf ) ;
        pbuf += offset ;
        tubuf += 1 ;
      }
    }
    else {
      /* Each component has its own buffer. */
      tubuf = ( pd->ubuf + ( pd->nconvp * pd->ncomps )) ;
      for ( i = 0 ; i < pd->ncomps ; ++i ) {
        if ( pbuffs[ i ] )
          (*pd->unpackfnN)( pbuffs[ i ] , tconv , pd->ncomps , tubuf ) ;
        tubuf += 1 ;
      }
    }
  }

  /* Normally the unpacked results of successive calls to pd_unpack are appended
     to a single buffer until the buffer is full.  If PD_NOAPPEND is set then
     each call of pd_unpack starts the buffer from the beginning.  If unpacking
     one component per call then update nconvp on the last component only. */
  if ( ( pd->use & PD_NOAPPEND ) == 0 &&
       ( icomp == -1 || ( icomp + 1 ) == pd->ncomps ) ) {
    pd->nconvp += tconv ;
    if ( pd->nconvp == pd->nconv )
      pd->nconvp = 0 ;
    HQASSERT( pd->nconvp < pd->nconv , "bad nconvp" ) ;
  }

  *rbuf = pd->ubuf ;
}

/* -------------------------------------------------------------------------- */
/* eg. 1 bit per component: RGBRGB... */
/* eg. container size 4 bpp, 1 bit per component: RGB_RGB_... */
void pd_pack( PACKDATA *pd , int32 *ubuf , uint8 **rbuf, int32 tconv )
{
  HQASSERT( pd , "pd_pack: pd NULL" ) ;
  HQASSERT( pd->packproc , "pd_pack: pd->packproc NULL" ) ;
  HQASSERT( tconv > 0 && tconv <= pd->nconv,
            "pd_pack: tconv out of range" ) ;

  /* collapse back into bpp */
  (*pd->packproc)( ubuf , pd->ncomps , tconv , pd->pbuf ) ;

  *rbuf = pd->pbuf ;
}

/* -------------------------------------------------------------------------- */
void pd_planarize( PACKDATA *pd , int32 *ubuf , int32 **rbuf )
{
  int32 offset , ncomps , nconv , *ebuf ;

  HQASSERT( pd , "pd_planarize: pd NULL" ) ;
  HQASSERT( pd->ebuf , "pd_planarize: no planar buffer" ) ;
  HQASSERT( ubuf , "pd_planarize: ubuf NULL" ) ;
  HQASSERT( rbuf , "pd_planarize: rbuf NULL" ) ;

  offset = pd->nconv ;
  ncomps = pd->ncomps ;
  nconv = pd->nconv ;
  ebuf = pd->ebuf ;

  switch ( ncomps ) {
    int32 i , n ;
  case 1:
    planar_1( ubuf , nconv , ebuf ) ;
    break ;
  case 2:
    planar_2( ubuf , nconv , ebuf , offset ) ;
    break ;
  case 3:
    planar_3( ubuf , nconv , ebuf , offset ) ;
    break ;
  case 4:
    planar_4( ubuf , nconv , ebuf , offset ) ;
    break ;
  default:
    n = ( ncomps - 4 ) & (~3) ;
    if ( ncomps - n == 4 || ncomps - n == 7 )
      n += 4 ;
    for ( i = 0 ; i < n ; i += 4 )
      planar_4N( ubuf + i , nconv ,
                 ebuf + i * offset ,
                 offset ,
                 ncomps ) ;
    while ( i + 3 <= ncomps ) {
      planar_3N( ubuf + i , nconv ,
                 ebuf + i * offset ,
                 offset ,
                 ncomps ) ;
      i += 3 ;
    }
    if ( i < ncomps )
      planar_2N( ubuf + i , nconv ,
                 ebuf + i * offset ,
                 offset ,
                 ncomps ) ;
  }

  *rbuf = ebuf ;
}

/* -------------------------------------------------------------------------- */
/* Inverts the (packed) data in buf, into pd->pbuf, based on the
 * number of levels (for each component) that the data uses.
 * Note that we could be inverting in place (ie buf == pd->pbuf)
 */
void pd_invert( PACKDATA *pd , uint8 *buf , int32 *maxlevels ,
		uint8 **rbuf )
{
  int32 n , lidx ;

  HQASSERT( pd ,        "pd_invert: pd NULL" ) ;
  HQASSERT( buf ,       "pd_invert: buf NULL" ) ;
  HQASSERT( maxlevels , "pd_invert: maxlevels NULL" ) ;
  HQASSERT( rbuf ,      "pd_invert: rbuf NULL" ) ;

  n = pd->ncomps * pd->nconv ;
  lidx = 0 ;

  switch ( pd->bpp ) {
  case 8: {
    uint8 *dst8 = pd->pbuf ;
    while ( n >= 8 ) {
      n -= 8 ;
      PENTIUM_CACHE_LOAD( buf + 7 ) ;
      dst8[ 0 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 0 ] ) ;
      dst8[ 1 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 1 ] ) ;
      dst8[ 2 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 2 ] ) ;
      dst8[ 3 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 3 ] ) ;
      dst8[ 4 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 4 ] ) ;
      dst8[ 5 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 5 ] ) ;
      dst8[ 6 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 6 ] ) ;
      dst8[ 7 ] = ( uint8 )( maxlevels[ lidx++ % pd->ncomps ] - buf[ 7 ] ) ;
      dst8 += 8 ;
      buf  += 8 ;
      lidx %= pd->ncomps ;
    }
    while ( n >= 1 ) {
      n -= 1 ;
      dst8[ 0 ] = ( uint8 )( maxlevels[ lidx++ ] - buf[ 0 ] ) ;
      dst8 += 1 ;
      buf  += 1 ;
      lidx %= pd->ncomps ;
    }
    break ;
  }

  case 16: {
    uint16 *buf16 = ( uint16 * )buf ;
    uint16 *dst16 = ( uint16 * )pd->pbuf ;
    while ( n >= 8 ) {
      n -= 8 ;
      PENTIUM_CACHE_LOAD( buf16 + 7 ) ;
      dst16[ 0 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 0 ] ) ;
      dst16[ 1 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 1 ] ) ;
      dst16[ 2 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 2 ] ) ;
      dst16[ 3 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 3 ] ) ;
      dst16[ 4 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 4 ] ) ;
      dst16[ 5 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 5 ] ) ;
      dst16[ 6 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 6 ] ) ;
      dst16[ 7 ] = ( uint16 )( maxlevels[ lidx++ % pd->ncomps ] - buf16[ 7 ] ) ;
      dst16 += 8 ;
      buf16 += 8 ;
      lidx %= pd->ncomps ;
    }
    while ( n >= 1 ) {
      n -= 1 ;
      dst16[ 0 ] = ( uint16 )( maxlevels[ lidx++ ] - buf16[ 0 ] ) ;
      dst16 += 1 ;
      buf16 += 1 ;
      lidx %= pd->ncomps ;
    }
    break ;
  }

  default:
    HQFAIL( "pd_invert: unexpected bpp; should be 8 or 16" ) ;
  }
  *rbuf  = pd->pbuf ;
}

/* -------------------------------------------------------------------------- */
void pd_set( PACKDATA *pd , int32 val , int32 plane )
{
  HQASSERT( pd != NULL , "pd NULL in pd_set" ) ;
  
  HQASSERT( pd->ubuf != NULL , "pd->ubuf is NULL" ) ;
  HQASSERT( pd->usize > 0 , "pd->usize <= 0" ) ;
  
  if ( plane == PD_ALLPLANES )
    HqMemSet32((uint32 *)pd->ubuf, val, (uint32)( pd->usize >> 2 ));
  else {
    int32 nconv ;
    int32 ncomps ;
    int32 *ubuf ;
    HQASSERT( plane >= 0 &&
              plane < pd->ncomps , "plane out of range" ) ;
    ubuf = pd->ubuf + plane ;
    nconv = pd->nconv ;
    ncomps = pd->ncomps ;
    while ( nconv >= 8 ) {
      PENTIUM_CACHE_LOAD( ubuf + 7 ) ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      nconv -= 8 ;
    }
    while ( nconv >= 1 ) {
      ubuf[ 0 ] = val ;
      ubuf += ncomps ;
      nconv -= 1 ;
    }
  }
}

/* -------------------------------------------------------------------------- */
void pd_copy( PACKDATA *pd_src , PACKDATA *pd_dst ,
              int32 offset_src , int32 offset_dst , int32 offset_adj ,
              int32 plane )
{
  int32 ncomps ;
  int32 rw_src ;
  int32 rw_dst ;
  int32 *buf_src ;
  int32 *buf_dst ;

  HQASSERT( pd_src != NULL , "pd_src NULL in pd_copy" ) ;
  HQASSERT( pd_dst != NULL , "pd_dst NULL in pd_copy" ) ;
  HQASSERT( offset_src >= 0 , "offset_src should be >= 0" ) ;
  HQASSERT( offset_dst >= 0 , "offset_dst should be >= 0" ) ;
  HQASSERT( plane >= 0 , "plane should be >= 0" ) ;
  
  ncomps = pd_src->ncomps ;
  HQASSERT( pd_src->ncomps == pd_dst->ncomps , "Should not call pd_copy on different comps" ) ;

  buf_src = pd_src->ubuf + offset_src * pd_src->ncomps + plane ;
  buf_dst = pd_dst->ubuf + offset_dst * pd_dst->ncomps + plane ;
  
  rw_src = pd_src->nconv - offset_src ;
  rw_dst = pd_dst->nconv - offset_dst ;

  if ( rw_src + offset_adj > rw_dst )
    rw_src = rw_dst - offset_adj ;
  if ( offset_adj < 0 ) {
    rw_src += offset_adj ;
    buf_src -= offset_adj * pd_src->ncomps ;
  }
  else
    buf_dst += offset_adj * pd_dst->ncomps ;

  while ( rw_src >= 8 ) {
    PENTIUM_CACHE_LOAD( buf_dst + 7 ) ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    rw_src -= 8 ;
  }
  while ( rw_src >= 1 ) {
    buf_dst[ 0 ] = buf_src[ 0 ] ;
    buf_dst += ncomps ;
    buf_src += ncomps ;
    rw_src -= 1 ;
  }
}


/* Log stripped */
