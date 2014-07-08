/** \file
 *
 * $HopeName: SWle-security!src:lesecgen.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

/* ----------------------------- Includes ---------------------------------- */

#include "lesecgen.h"

#include "hqmemcpy.h" /* HqMemCpy */
#include "hqrc4.h"    /* RC4 */

#ifdef WIN32
#include "hqwindows.h"
#else
#if !defined(VXWORKS) && !defined(__CC_ARM)
#include <sys/time.h>
#endif
#ifndef MACOSX
#define _POSIX_SOURCE
#include <time.h>
#ifndef __CC_ARM
#include <unistd.h>
#endif
#endif
#endif

/* ---------------------------- Defines ------------------------------------ */

#define LESEC_RC4_KEY_LEN 16

static uint8 lesecRC4key[ LESEC_RC4_KEY_LEN ] =
{
  0x45, 0x81, 0x03, 0xde, 0xb9, 0x15, 0xfa, 0x33,
  0x22, 0xeb, 0xa3, 0x59, 0xd0, 0x4a, 0x74, 0x6c
};


#define N_RANDOM_BYTES         7

#define RANDOM_BYTES_OFFSET    0                                       /* N_RANDOM_BYTES */
#define LESEC_LENGTH_OFFSET    (RANDOM_BYTES_OFFSET + N_RANDOM_BYTES)  /* short */
#define LESEC_VERSION_OFFSET   (LESEC_LENGTH_OFFSET + 2)               /* byte */
#define LESEC_MAGIC_OFFSET     (LESEC_VERSION_OFFSET + 1)              /* long */
#define OEM_ID_OFFSET          (LESEC_MAGIC_OFFSET + 4)                /* short */
#define SEC_NO_OFFSET          (OEM_ID_OFFSET + 2)                     /* short */
#define RES_LIMIT_OFFSET       (SEC_NO_OFFSET + 2)                     /* short */
#define PRODUCT_VERSION_OFFSET (RES_LIMIT_OFFSET + 2)                  /* byte */
#define PLATFORM_OFFSET        (PRODUCT_VERSION_OFFSET + 1)            /* byte */
#define HANDSHAKE_LEN_OFFSET   (PLATFORM_OFFSET + 1)                   /* byte */
#define HANDSHAKE_OFFSET       (HANDSHAKE_LEN_OFFSET + 1)

#define PASSWORD_ID_OFFSET     0    /* byte */
#define PASSWORD_VALUE_OFFSET  1    /* long */
#define LESEC_PASSWORD_SIZE    5

/*
 * Data length =
 *   data before handshake (up to and including HANDSHAKE_LEN_OFFSET) +
 *   handshake len +
 *   1 (no of passwords) + no of passwords * LESEC_PASSWORD_SIZE +
 *   1 (check byte)
 */
#define DATA_LEN( _handshakeLen_, _nPasswords_ ) \
  (HANDSHAKE_OFFSET + (_handshakeLen_) + 1 + ((_nPasswords_) * LESEC_PASSWORD_SIZE) + 1)

/*
 * Pad data to be a multiple of 16 bytes
 * Pad by at least the size of a password entry
 */
#define PADDED_LENGTH_MULTIPLE 16
#define MIN_PADDING LESEC_PASSWORD_SIZE
#define PADDED_DATA_LEN( _unpaddedLen_ ) \
  (((((_unpaddedLen_) + MIN_PADDING) / PADDED_LENGTH_MULTIPLE) + 1) * PADDED_LENGTH_MULTIPLE)


#define LESEC_VERSION 2u

#define LESEC_MAGIC 0x8765439cu

/* The first N_RANDOM_BYTES of _buffer_ will have been set to random
 * data.  Mask the values with this data so that the results of
 * generating data multiple times for the same values are likely to
 * be completely different.
 * MASK_BYTE does this masking.
 */
#define MASK_BYTE( _buffer_, _offset_, _value_ ) \
  ((_value_) ^ ((_buffer_)[ (_offset_) % N_RANDOM_BYTES ]))

/* Get/set a byte in the buffer, doing the necessary masking.
 */
#define GET_BYTE( _buffer_, _offset_ ) \
  ((uint32)MASK_BYTE( (_buffer_), (_offset_), (_buffer_)[ (_offset_) ]))
#define SET_BYTE( _buffer_, _offset_, _value_ ) MACRO_START \
  (_buffer_)[ (_offset_) ] = MASK_BYTE( (_buffer_), (_offset_), (_value_) ); \
MACRO_END

/* Read/write values, doing the necessary bit shifting for
 * multibyte values.
 */
#define READ_BYTE( _buffer_, _offset_ ) \
  GET_BYTE( (_buffer_), (_offset_) )
#define WRITE_BYTE( _buffer_, _offset_, _value_ ) MACRO_START \
  HQASSERT( _value_ <= 0xFF, "value out of range" ); \
  SET_BYTE( (_buffer_), (_offset_), (uint8)((_value_) & 0xFF) ); \
MACRO_END

#define READ_SHORT( _buffer_, _offset_ ) \
  ( GET_BYTE( (_buffer_), (_offset_) ) \
  | ( GET_BYTE( (_buffer_), (_offset_) + 1 ) << 8u ) )
#define WRITE_SHORT( _buffer_, _offset_, _value_ ) MACRO_START \
  HQASSERT( _value_ <= 0xFFFF, "value out of range" ); \
  SET_BYTE( (_buffer_), (_offset_), (uint8)((_value_) & 0xFF) ); \
  SET_BYTE( (_buffer_), ((_offset_) + 1), (uint8)(((_value_) & 0xFF00) >> 8) ); \
MACRO_END

#define READ_LONG( _buffer_, _offset_ ) \
  ( GET_BYTE( (_buffer_), (_offset_) ) \
  | ( GET_BYTE( (_buffer_), (_offset_) + 1 ) << 8u ) \
  | ( GET_BYTE( (_buffer_), (_offset_) + 2 ) << 16u ) \
  | ( GET_BYTE( (_buffer_), (_offset_) + 3 ) << 24u ) )
#define WRITE_LONG( _buffer_, _offset_, _value_ ) MACRO_START \
  SET_BYTE( (_buffer_), (_offset_), (uint8)((_value_) & 0xFF) ); \
  SET_BYTE( (_buffer_), ((_offset_) + 1), (uint8)(((_value_) & 0xFF00) >> 8) ); \
  SET_BYTE( (_buffer_), ((_offset_) + 2), (uint8)(((_value_) & 0xFF0000) >> 16) ); \
  SET_BYTE( (_buffer_), ((_offset_) + 3), (uint8)(((_value_) & 0xFF000000) >> 24) ); \
MACRO_END


/* ------------------------ Forward function declarations ------------------ */

static uint8 calculateCheckByte( uint32 len, uint8 * pData );


/* ---------------------------- Exported functions ------------------------- */

void lesecEncode( uint32 len, uint8 * pIn, uint8 * pOut )
{
  RC4_KEY rc4key;

  RC4_set_key( &rc4key, LESEC_RC4_KEY_LEN, lesecRC4key );
  RC4( &rc4key, len, pIn, pOut );
}


void lesecDecode( uint32 len, uint8 * pIn, uint8 * pOut )
{
  lesecEncode( len, pIn, pOut );
}


int32 lesecValidateData( uint32 oemID, uint32 len, uint8 * pData )
{
  int32 fValid = FALSE;

  if( READ_SHORT( pData, LESEC_LENGTH_OFFSET ) == len
    && READ_BYTE( pData, LESEC_VERSION_OFFSET ) == LESEC_VERSION
    && getOemID( pData ) == oemID
    && READ_LONG( pData, LESEC_MAGIC_OFFSET ) == LESEC_MAGIC )
  {
    uint8 checkByte = calculateCheckByte( len - 1, pData );

    if( checkByte == READ_BYTE( pData, len - 1 ) )
    {
      fValid = TRUE;
    }
  }

  return fValid;
}


uint32 getOemID( uint8 * pData )
{
  return READ_SHORT( pData, OEM_ID_OFFSET );
}


uint32 getSecurityNo( uint8 * pData )
{
  return READ_SHORT( pData, SEC_NO_OFFSET );
}


uint32 getResLimit( uint8 * pData )
{
  return READ_SHORT( pData, RES_LIMIT_OFFSET );
}


uint32 getProductVersion( uint8 * pData )
{
  return READ_BYTE( pData, PRODUCT_VERSION_OFFSET );
}


uint32 getPlatform( uint8 * pData )
{
  return READ_BYTE( pData, PLATFORM_OFFSET );
}


uint8 * copyHandshake( uint8 * pData, uint32 * pcbLen )
{
  uint32  cbHandshakeLen = READ_BYTE( pData, HANDSHAKE_LEN_OFFSET );
  uint8 * pHandshake = malloc( cbHandshakeLen );

  HqMemCpy( pHandshake, pData + HANDSHAKE_OFFSET, cbHandshakeLen );
  *pcbLen = cbHandshakeLen;

  return pHandshake;
}


void freeHandshake( uint8 * pHandshake )
{
  free( pHandshake );
}


LeSecPassword * copyPasswords( uint8 * pData, uint32 * pnPasswords )
{
  uint32          cbHandshakeLen = READ_BYTE( pData, HANDSHAKE_LEN_OFFSET );
  uint32          nPasswordOffset;
  uint32          nPasswords;
  LeSecPassword * pPasswords = NULL;

  nPasswordOffset = HANDSHAKE_OFFSET + cbHandshakeLen;
  nPasswords = READ_BYTE( pData, nPasswordOffset );
  nPasswordOffset++;

  if( nPasswords > 0 )
  {
    uint32 i;

    pPasswords = (LeSecPassword *) malloc( nPasswords * sizeof( LeSecPassword ) );

    for( i = 0; i < nPasswords; i++ )
    {
      pPasswords[ i ].id = READ_BYTE( pData, nPasswordOffset + PASSWORD_ID_OFFSET );
      pPasswords[ i ].value = READ_LONG( pData, nPasswordOffset + PASSWORD_VALUE_OFFSET );

      nPasswordOffset += LESEC_PASSWORD_SIZE;
    }
  }

  *pnPasswords = nPasswords;

  return pPasswords;
}


void freePasswords( LeSecPassword * pPasswords )
{
  free( pPasswords );
}


uint8 * createData( uint32 oemID, uint32 secNo,
  uint32 resLimit, uint32 productVersion, uint32 platform,
  uint32 cbHandshakeLen, uint8 * pHandshake,
  uint32 nPasswords, LeSecPassword * pPasswords,
  uint32 * pcbLen )
{
  uint32  cbDataLen;
  uint32  cbPaddedLen;
  uint8 * pData;
  uint32  nPasswordOffset;
  uint32  i;
  uint32  checkByte;

  HQASSERT( oemID <= 0xFFFF, "oemID out of range" );
  HQASSERT( resLimit <= 0xFFFF, "resLimit out of range" );
  HQASSERT( productVersion <= 0xFF, "productVersion out of range" );
  HQASSERT( platform <= 0xFF, "platform out of range" );
  HQASSERT( nPasswords <= 0xFF, "nPasswords out of range" );

  cbDataLen = DATA_LEN( cbHandshakeLen, nPasswords );
  cbPaddedLen = PADDED_DATA_LEN( cbDataLen );

  pData = (uint8 *) malloc( cbPaddedLen );

  fillBuffer( pData, N_RANDOM_BYTES );

  WRITE_SHORT( pData, LESEC_LENGTH_OFFSET, cbPaddedLen );
  WRITE_BYTE( pData, LESEC_VERSION_OFFSET, LESEC_VERSION );

  WRITE_LONG( pData, LESEC_MAGIC_OFFSET, LESEC_MAGIC );

  WRITE_SHORT(  pData, OEM_ID_OFFSET, oemID );
  WRITE_SHORT(  pData, SEC_NO_OFFSET, secNo );
  WRITE_SHORT(  pData, RES_LIMIT_OFFSET, resLimit );
  WRITE_BYTE( pData, PRODUCT_VERSION_OFFSET, productVersion );
  WRITE_BYTE( pData, PLATFORM_OFFSET, platform );

  WRITE_BYTE( pData, HANDSHAKE_LEN_OFFSET, cbHandshakeLen );
  HqMemCpy( pData + HANDSHAKE_OFFSET, pHandshake, cbHandshakeLen );

  nPasswordOffset = HANDSHAKE_OFFSET + cbHandshakeLen;
  WRITE_BYTE( pData, nPasswordOffset, nPasswords );
  nPasswordOffset++;
  
  for( i = 0; i < (nPasswords & 0xFF); i++ )
  {
    HQASSERT( pPasswords[ i ].id <= 0xFF, "Password ID out of range" );

    WRITE_BYTE( pData, nPasswordOffset + PASSWORD_ID_OFFSET, pPasswords[ i ].id );
    WRITE_LONG( pData, nPasswordOffset + PASSWORD_VALUE_OFFSET, pPasswords[ i ].value );

    nPasswordOffset += LESEC_PASSWORD_SIZE;
  }

  HQASSERT( nPasswordOffset == cbDataLen - 1, "Data length mismatch" );

  fillBuffer( pData + nPasswordOffset, cbPaddedLen - cbDataLen );

  checkByte = calculateCheckByte( cbPaddedLen - 1, pData );
  WRITE_BYTE( pData, cbPaddedLen - 1, checkByte );

  *pcbLen = cbPaddedLen;

  return pData;
}


void freeData( uint8 * pData )
{
  free( pData );
}


/* ------------------------- Random number generation ---------------------- */

/* Converted to macros so as to retain appearance of original code while
 * removing the need for any static data.  Using a new seed for each buffer
 * is good enough for our purposes.
 */

/* 
   A C-program for MT19937, with initialization improved 2002/1/26.
   Coded by Takuji Nishimura and Makoto Matsumoto.

   Before using, initialize the state by using init_genrand(seed)  
   or init_by_array(init_key, key_length).

   Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.                          

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote 
        products derived from this software without specific prior written 
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


   Any feedback is very welcome.
   http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
   email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)
*/

/* Period parameters */  
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

/* initializes mt[N] with a seed */
#define init_genrand(s)                                           \
{                                                                 \
    HQASSERT( mti == N+1, "init_genrand already called" );        \
                                                                  \
    mt[0]= s & 0xffffffffUL;                                      \
    for (mti=1; mti<N; mti++) {                                   \
        mt[mti] =                                                 \
	    (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti);     \
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */ \
        /* In the previous versions, MSBs of the seed affect   */ \
        /* only MSBs of the array mt[].                        */ \
        /* 2002/01/09 modified by Makoto Matsumoto             */ \
        mt[mti] &= 0xffffffffUL;                                  \
        /* for >32 bit machines */                                \
    }                                                             \
}

/* generates a random number on [0,0xffffffff]-interval */
#define genrand_int32(r)                                          \
{                                                                 \
    unsigned long y;                                              \
    static unsigned long mag01[2]={0x0UL, MATRIX_A};              \
    /* mag01[x] = x * MATRIX_A  for x=0,1 */                      \
                                                                  \
    if (mti >= N) { /* generate N words at one time */            \
        int kk;                                                   \
                                                                  \
        HQASSERT( mti != N+1, "init_genrand not called" );        \
                                                                  \
        for (kk=0;kk<N-M;kk++) {                                  \
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);        \
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];      \
        }                                                         \
        for (;kk<N-1;kk++) {                                      \
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);        \
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];  \
        }                                                         \
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);              \
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];          \
                                                                  \
        mti = 0;                                                  \
    }                                                             \
                                                                  \
    y = mt[mti++];                                                \
                                                                  \
    /* Tempering */                                               \
    y ^= (y >> 11);                                               \
    y ^= (y << 7) & 0x9d2c5680UL;                                 \
    y ^= (y << 15) & 0xefc60000UL;                                \
    y ^= (y >> 18);                                               \
                                                                  \
    r = y;                                                        \
}


void fillBuffer( uint8 * pBuffer, uint32 len )
{
  unsigned long mt[N]; /* the array for the state vector  */
  int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

  unsigned long  s;
  uint32         i;
  unsigned long  r;

#if defined (WIN32)
  SYSTEMTIME st;
  GetSystemTime(&st);
  s = st.wMilliseconds + 1000*(st.wSecond + 60*(st.wMinute + 60*(st.wHour + 24*st.wDay)));
#elif defined (VXWORKS)
  s = time(NULL);
    /* arm9/threadx port, wanted to avoid compiler error for timeval, can be removed later */
  /* need to re-look into this */
#elif defined (THREADX)
  s = time(NULL);
#else
  struct timeval tp;
  gettimeofday( &tp, NULL );
  s = tp.tv_usec;
#endif

  init_genrand( s );

  for( i = 0; i < len; i++ )
  {
    genrand_int32(r);
    pBuffer[ i ] = (uint8) (r & 0xFF);
  }
}


/* ------------------------- Internal  functions  -------------------------- */

static uint8 calculateCheckByte( uint32 len, uint8 * pData )
{
  uint32 i;
  uint8  checkByte = 0xd5;

  for( i = 0; i < len; i ++ )
  {
    checkByte ^= pData[ i ];
  }

  return checkByte;
}


/*  
* Log stripped */

/* end of lesecgen.c */
