/** \file
 * \ingroup core
 *
 * $HopeName: COREtables!export:tables.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Table and macro declarations for bit and byte sexing (order of bits within
 * a byte and bytes within a word), and hex character tables.
 */

#ifndef __TABLES_H__
#define __TABLES_H__

/*------------- Bit sexing (order of bits within a byte/word) ----------------*/

extern int8 highest_bit_set_in_byte[256] ; /* -1 if no bit set */
extern uint8 count_bits_set_in_byte[256] ;
extern uint8 reversed_bits_in_byte[256] ;
extern uint8 rlelength[256] ;

#define HIGHEST_BIT_SET_LIMIT (10)
extern int8 highest_bit_set_in_bits[1 << HIGHEST_BIT_SET_LIMIT]; /* -1 if no bit set */

/* -1 if no bit set */
#define HIGHEST_BIT_SET_32(_v, _hi) \
MACRO_START \
  uint32 _tmp1_ = (_v) >> 16 ; \
  if ( _tmp1_ > 0 ) { \
    uint32 _tmp2_ = (_v) >> 24 ; \
    (_hi) = ( _tmp2_ > 0 \
              ? 24 + highest_bit_set_in_byte[_tmp2_] \
              : 16 + highest_bit_set_in_byte[_tmp1_ & 0xFF] ) ; \
  } else { \
    uint32 _tmp2_ = (_v) >> 8 ; \
    (_hi) = ( _tmp2_ > 0 \
              ? 8 + highest_bit_set_in_byte[_tmp2_] \
              : highest_bit_set_in_byte[(_v)] ) ; \
  } \
MACRO_END

/*-------------- Byte sexing (order of bytes within a word) -----------------*/

/* These macros are used to swap bytes to the natural order required
 * by the machine.
 */

#ifdef highbytefirst

#define HighOrder4Bytes( buff )  EMPTY_STATEMENT()
#define LowOrder4Bytes( buff )   BYTE_SWAP32_PTR(buff)
#define HighOrder2Bytes( buff )  EMPTY_STATEMENT()
#define LowOrder2Bytes( buff )   BYTE_SWAP16_PTR(buff)

#else  /* low byte first */

#define HighOrder4Bytes( buff )  BYTE_SWAP32_PTR(buff)
#define LowOrder4Bytes( buff )   EMPTY_STATEMENT()
#define HighOrder2Bytes( buff )  BYTE_SWAP16_PTR(buff)
#define LowOrder2Bytes( buff )   EMPTY_STATEMENT()

#endif

/* to avoid the compiler forcing type casts, the following unions
   allows a group of bytes to be interpreted in different ways.
*/

typedef struct {
  union {
    uint32 uintval ;
    int32  sintval ;
    float  floatval ;
    uint16 ushortvals[2] ;
    int16  sshortvals[2] ;
    uint8  bytes[4] ;
    int8   chars[4] ;
  } u ;
} FOURBYTES ;

typedef struct {
  union {
    uint16  ushortvals[1] ;
    int16   sshortvals[1] ;
    uint8   bytes[2] ;
  } u ;
} TWOBYTES ;

#define asReal( p )        ((p).u.floatval)
#define asInt( p )         ((p).u.uintval)
#define asSignedInt( p )   ((p).u.sintval)
#define asShort( p )       ((p).u.ushortvals[0])
#define asSignedShort( p ) ((p).u.sshortvals[0])
#define asBytes( p )       ((p).u.bytes)
#define asChars( p )       ((p).u.chars)
#define asFloat( p )       ((p).u.floatval)

/*------------------ Character tables (except swctype.h) --------------------*/

/* Hex conversion tables. char_to_nibble returns -1 if not a hex char. Index
   of -1 is OK for char_to_nibble. hex_char_from_nibble returns an uppercase
   hex digit for the nibble specified. */
extern int8 *char_to_hex_nibble; /* 257 entries, -1..255 */
extern uint8 nibble_to_hex_char[16];

/*
Log stripped */
#endif /* Protection from multiple inclusion */
