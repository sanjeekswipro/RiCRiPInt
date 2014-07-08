/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*  
 * $HopeName: SWsecurity!src:dllutils.c(EBDSDK_P.1) $
 * Utilities for 'DLL' security
 * Extracted from SWv20!src:ascii85.c, SWv20!src:hqcrypt.c
 *
* Log stripped */

#include "std.h"
#include "dllutils.h"
#include "dllsec.h"


typedef struct {
  union {
    uint32 intval ;
    uint8  bytes[4] ;
  } u ;
} DLLFOURBYTES ;

#define asInt( p )         ((p).u.intval)
#define asBytes( p )       ((p).u.bytes)

#define Swap4Bytes( p ) MACRO_START \
   uint8 _c ; \
   _c = *p , *p = *(p + 3) , *(p + 3) = _c ;\
   _c = *(p + 1) , *(p + 1) = *(p + 2) , *(p + 2) = _c ;\
MACRO_END

#ifdef highbytefirst
#define HighOrder4Bytes( buff )  
#else
#define HighOrder4Bytes( buff )  Swap4Bytes( ( buff ) )
#endif


/*
 * ASCII 85
 */


/* ASCII85 defines */

#define POWER4 52200625
#define POWER3 614125
#define POWER2 7225
#define POWER1 85
#define MAXHIGH4BYTES 1377252876

#ifdef highbytefirst
#define BYTE_INDEX( i )  ( i )
#else
#define BYTE_INDEX( i )  ( 3 - (i) )
#endif


#define Ascii85DecodeBuffer   countPGBs
#define Ascii85EncodeBuffer   opInProgress


/*
 * Decode Ascii85
 * Return no of bytes written to outBuffer
 */
static uint32 Ascii85DecodeBuffer( uint8 * inBuffer, uint32 inBytes, uint8 * outBuffer )
{
  int32      c , i ;
  uint8    * inPtr;
  uint32   * outPtr ;
  DLLFOURBYTES  fb ;

  HQASSERT( (inBytes % 5) == 0, "Can only decode exact multiples of 5" );

  inPtr = inBuffer;
  outPtr = (uint32 *) outBuffer;

  i = 0 ;
  asInt( fb ) = 0 ;
  while ( inPtr < inBuffer + inBytes )
  {
    c = *inPtr++;
  
    if (( c < '!') || ( c > 'u'))
    {
      /* Out of range value */
      return 0 ;
    }
    else
    {
      if ( i == 4 )
      {
        if ( asInt( fb ) > MAXHIGH4BYTES )
          return 0 ;

        asInt( fb ) = POWER4 * (uint32)asBytes(fb)[BYTE_INDEX(0)] +
          POWER3 * (uint32)asBytes(fb)[BYTE_INDEX(1)] +
          POWER2 * (uint32)asBytes(fb)[BYTE_INDEX(2)] + 
          POWER1 * (uint32)asBytes(fb)[BYTE_INDEX(3)] + (uint32) (c - 33 ) ;
        HighOrder4Bytes( asBytes(fb) ) ;
        *outPtr++ = asInt(fb) ;
        i = 0 ;
        asInt( fb ) = 0 ;
      }
      else
      {
        asBytes(fb)[BYTE_INDEX(i++)] = (uint8) ( c - 33 );
      }
    }
  }

  HQASSERT( i == 0, "Internal error" );

  return CAST_PTRDIFFT_TO_UINT32(((uint8 *)outPtr) - outBuffer);
}


/*
 * Encode Ascii85
 * Return no of bytes written to outBuffer
 */
static uint32 Ascii85EncodeBuffer( uint8 * inBuffer, uint32 inBytes, uint8 * outBuffer )
{
  int32      count , i ;
  uint32     c ;
  uint32   * inPtr ;
  uint8    * outPtr;
  DLLFOURBYTES  fb ;
  DLLFOURBYTES  out ; /* high order bytes of the output 5-tuple */

  HQASSERT( (inBytes % 4) == 0, "Can only encode exact multiples of 4" );

  count = inBytes;
  inPtr = (uint32 *) inBuffer;
  outPtr = outBuffer;

  while ( count >= 4 )
  {
    asInt( fb ) = *inPtr ;
    HighOrder4Bytes( asBytes( fb )) ;

    c = asInt( fb ) / POWER4 ;
    asInt( fb ) -= c * POWER4 ;
    asBytes( out )[0] = (uint8) c ;
    c = asInt( fb ) / POWER3 ;
    asInt( fb ) -= c * POWER3 ;
    asBytes( out )[1] = (uint8) c ;
    c = asInt( fb ) / POWER2 ;
    asInt( fb ) -= c * POWER2 ;
    asBytes( out )[2] = (uint8) c ;
    c = asInt( fb ) / POWER1 ;
    asInt( fb ) -= c * POWER1 ;
    asBytes( out )[3] = (uint8) c ;
    
    /* _always_ output five chars */
    for ( i = 0 ; i < 4 ; i++ )
    {
      c = (uint32) asBytes( out )[i] + 33 ;
      *outPtr++ = (uint8) c;
    }
    c = asInt( fb ) + 33 ;
    *outPtr++ = (uint8) c;

    count -= 4;
    inPtr++;
  }

  return CAST_PTRDIFFT_TO_UINT32(outPtr - outBuffer);
}

/*
 * END OF ASCII 85
 */



/*
 * HQCRYPT
 */

#define HqEncrypt   ParseError
/* #define HqDecrypt   ReportRes - defined in dllutils.h */

/* Constants to control encryption */

#define  E_ROTATE  31
#define  E_XOR     0x01030507u

#define STEP_KEY(key, b) MACRO_START                                    \
{                                                                       \
  uint32 rotate_;                                                       \
  /* choose a rotate in the range 1..E_ROTATE inclusive */              \
  /* (this should avoid 0 and 32 since shifts are */                    \
  /* implemented modulo 32 on some processors) */                       \
  rotate_ = 1 + ((b) % E_ROTATE);                                       \
  (key) = (((key) << rotate_) | ((key) >> (32-rotate_))) ^ (b) ^ E_XOR; \
}                                                                       \
MACRO_END

static void HqEncrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf)
{
  int32 i;
  uint8 b;
  uint32 key;

/*
  HQASSERT(pKey != (uint32 *)NULL, "invalid key");
  HQASSERT(nBytes > 0 &&
           pInBuf != (uint8 *)NULL &&
           pOutBuf != (uint8 *)NULL, "invalid buffer");
*/

  key = *pKey;
  for (i = 0; i < nBytes; i++)
  {
    b = *pInBuf++;
    /* encrypt the byte */
    b = (b ^ (uint8)key) + (uint8)(key >> 8);
    *pOutBuf++ = b;
    /* step the key for the next encryption */
    /* using the encrypted byte */
    STEP_KEY(key, b);
  }
  /* return the key for subsequent encryption calls */
  *pKey = key;
}

void HqDecrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf)
{
  int32 i;
  uint8 b;
  uint32 key;

/*
  HQASSERT(pKey != (uint32 *)NULL, "invalid key");
  HQASSERT(nBytes > 0 &&
           pInBuf != (uint8 *)NULL &&
           pOutBuf != (uint8 *)NULL, "invalid buffer");
*/

  key = *pKey;
  for (i = 0; i < nBytes; i++)
  {
    b = *pInBuf++;
    /* decrypt the byte */
    *pOutBuf++ = (b - (uint8)(key >> 8)) ^ (uint8)key;
    /* step the key for the next decryption */
    /* using the encrypted byte */
    STEP_KEY(key, b);
  }
  /* return the key for subsequent decryption calls */
  *pKey = key;
}


/*
 * END OF HQCRYPT
 */


/*
 * CRC
 */

#define dll_makecrctable label45
#define dllCRCchecksum   pollInputs


STATIC uint32 dll_crc_table[256];
STATIC int32 dll_madetable = 0;

STATIC void dll_makecrctable(void)
{
  uint32 i;

  for (i=0; i < 256; i++) {
    int32 shift = 0;
    uint32 extra = 0x8d;
    uint32 j = i;
    uint32 total = 0;

    while (j > 0) {
      if (j & 1) {
        total ^= extra << shift;
      }
      shift++;
      j >>= 1;
    };
    dll_crc_table[i] = total;
  }
  dll_madetable = 1;
}


STATIC uint32 dllCRCchecksum(uint32 crc, uint32 *data, int32 len)
{
  uint32 temp, word, mask;
  uint32 *edata;

  if(!dll_madetable){
    dll_makecrctable();
  }
  mask = 0xFF;
  for (edata = data + len; data < edata;) {
    word = *data++;
    temp = dll_crc_table[crc >> 24];
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = dll_crc_table[crc >> 24];
    word >>= 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = dll_crc_table[crc >> 24];
    word >>= 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = dll_crc_table[crc >> 24];
    word >>= 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
  }
  return(crc);
}

#define CRCto2chars(crc, twochars)                      \
MACRO_START                                             \
{                                                       \
  uint8 byteValue_ = 0x3A;                              \
  byteValue_ ^= (uint8)(((crc) & 0xFF000000) >> 24);    \
  byteValue_ ^= (uint8)(((crc) & 0x00FF0000) >> 16);    \
  byteValue_ ^= (uint8)(((crc) & 0x0000FF00) >> 8);     \
  byteValue_ ^= (uint8)(((crc) & 0x000000FF));          \
  (twochars)[0] = '!' + ((byteValue_ & 0xF8) >> 3);     \
  (twochars)[1] = 's' - ((byteValue_ & 0x1F));          \
}                                                       \
MACRO_END

/*
 * End CRC
 */


/*
 * External functions to put it all together
 */

uint32 encodeLicence( uint32 serialNo, uint8 * decodedLicence, uint32 decodedLength, uint8 * encodedLicence )
{
  uint32  key = serialNo;
  uint8   encryptedLicence[ LICENCE_LENGTH ];
  uint8   ascii85Licence[ ASCII85_LENGTH ];
  uint32  crc;
  uint8   checksum[2];
  int32   i;

  if( decodedLength != LICENCE_LENGTH )
  {
    return 0;
  }

  /* Encrypt */
  HqEncrypt( &key, LICENCE_LENGTH, decodedLicence, encryptedLicence );
  
  /* Convert to ASCII 85 */
  if( Ascii85EncodeBuffer( encryptedLicence, LICENCE_LENGTH, ascii85Licence ) != ASCII85_LENGTH )
  {
    return 0;
  }
  
  /* Add CRC */
  {
    uint8  * pu8ascii85 = ascii85Licence;
    uint32 * pu32ascii85 = (uint32 *)pu8ascii85;
    crc = *pu32ascii85;
    crc = dllCRCchecksum( crc, pu32ascii85, ASCII85_LENGTH / sizeof( uint32) );
  }
  CRCto2chars( crc, checksum );
  
  for( i=0; i<ASCII85_LENGTH; i++)
  {
    encodedLicence[i] = ascii85Licence[i];
  }
  for( i=0; i<2; i++)
  {
    encodedLicence[ASCII85_LENGTH+i] = checksum[i];
  }
  
  return ENCODED_LENGTH;
}

uint32 decodeLicence( uint32 serialNo, uint8 * encodedLicence, uint32 encodedLength, uint8 * decodedLicence )
{
  uint32  crc;
  uint8   checksum[2];
  uint32  key = serialNo;
  uint8   encryptedLicence[ LICENCE_LENGTH ];
  uint8 * ascii85Licence = encodedLicence;

  if( encodedLength != ENCODED_LENGTH )
  {
    return 0;
  }

  /* Check and remove CRC */
  crc = *((uint32 *)encodedLicence);
  crc = dllCRCchecksum( crc, (uint32 *)encodedLicence, ASCII85_LENGTH / sizeof( uint32) );
  CRCto2chars( crc, checksum );
  
  if( checksum[ 0 ] != encodedLicence[ ASCII85_LENGTH ]
    || checksum[ 1 ] != encodedLicence[ ASCII85_LENGTH + 1 ] )
  {
    return 0;
  }

  /* Convert from ASCII 85 */
  if( Ascii85DecodeBuffer( ascii85Licence, ASCII85_LENGTH, encryptedLicence ) != LICENCE_LENGTH )
  {
    return 0;
  }
  
  /* Decrypt */
  HqDecrypt( &key, LICENCE_LENGTH, encryptedLicence, decodedLicence );
  
  return LICENCE_LENGTH;
}


/* eof dllutils.c */
