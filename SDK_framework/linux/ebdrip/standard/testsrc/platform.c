/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 * $HopeName: HQNc-standard!testsrc:platform.c(EBDSDK_P.1) $
 *
 * Attempt to divine some of the defines required by platform.h for this
 * machine.
 *
 * Now also does other useful platform tests.
 */

#include "std.h"
#include <stdio.h>

/* Win64: size=1 */
struct padding_1_bytes_1 {
  uint8  a ;
} ;

/* Win64: size=2 */
struct padding_2_bytes_1 {
  uint16  a ;
} ;

/* Win64: size=4 */
struct padding_3_bytes_1 {
  uint16  a ;
  uint8   b ;
} ;

/* Win64: size=3 */
struct padding_3_bytes_2 {
  uint8  a ;
  uint8  b ;
  uint8  c ;
} ;

/* Win64: size=4 */
struct padding_3_bytes_3 {
  uint8   a ;
  uint16  b ;
} ;

/* Win64: size=8 */
struct padding_5_bytes_1 {
  uint32 a ;
  uint8  b ;
} ;

/* Win64: size=8 */
struct padding_5_bytes_2 {
  uint8  b ;
  uint32 a ;
} ;

/* Win64: size=8 */
struct padding_6_bytes_1 {
  uint32 a ;
  uint8  b ;
  uint8  c ;
} ;

struct padding_9_bytes_1 {
  uint32 a ;
  uint32 b ;
  uint8  c ;
} ;

/* Win64: size=12 */
struct padding_9_bytes_2 {
  uint8  c ;
  uint32 a ;
  uint32 b ;
} ;

/* Win64: size=16 */
struct padding_9_bytes_3 {
  double a ;
  uint8  b ;
} ;

/* Win64: size=16 */
struct padding_9_bytes_4 {
  double a ;
  uint8  b ;
} ;

/* Win64: size=16 */
struct padding_9_bytes_5 {
  uint8  b ;
  double a ;
} ;

/* Win64: size=16 */
struct padding_9_bytes_6 {
  uint8  a ;
  void*  b ;
} ;

/* Win64: size=16 */
struct padding_9_bytes_7 {
  uint32  a ;
  void*   b ;
} ;

/* Win64: size=12 */
struct padding_10_bytes_1 {
  uint32 a ;
  uint32 b ;
  uint8  c ;
  uint8  d ;
} ;

/* Function used to hopefully avoid over-eager optimisation and warnings
   about shift widths. */
uint32 shiftby(uint32 i)
{
  return i * 8 ;
}

int main(int argc, char *argv[])
{
  int fail = 0 ;

  printf("/** sizeof(char)=%d sizeof(short)=%d sizeof(int)=%d\n    sizeof(long)=%d sizeof(float)=%d sizeof(double)=%d sizeof(size_t)=%d **/\n",
          (int)sizeof(char), (int)sizeof(short), (int)sizeof(int), (int)sizeof(long), (int)sizeof(float), (int)sizeof(double), (int)sizeof(size_t)) ;

  /* Some padding checks to keep one sane. */
  printf("/** padding: \
sizeof(5_bytes_1)=%d \
sizeof(5_bytes_2)=%d \
sizeof(9_bytes_1)=%d\n    \
sizeof(9_bytes_2)=%d \
sizeof(6_bytes_1)=%d \
sizeof(10_bytes_1)=%d\n    \
sizeof(9_bytes_4)=%d \
sizeof(9_bytes_5)=%d \
sizeof(9_bytes_6)=%d\n    \
sizeof(9_bytes_7)=%d \
sizeof(9_bytes_3)=%d \
sizeof(1_bytes_1)=%d\n    \
sizeof(2_bytes_1)=%d \
sizeof(3_bytes_1)=%d \
sizeof(3_bytes_2)=%d \
sizeof(3_bytes_3)=%d \
**/\n",
         (int)sizeof(struct padding_5_bytes_1),
         (int)sizeof(struct padding_5_bytes_2),
         (int)sizeof(struct padding_9_bytes_1),
         (int)sizeof(struct padding_9_bytes_2),
         (int)sizeof(struct padding_6_bytes_1),
         (int)sizeof(struct padding_10_bytes_1),
         (int)sizeof(struct padding_9_bytes_4),
         (int)sizeof(struct padding_9_bytes_5),
         (int)sizeof(struct padding_9_bytes_6),
         (int)sizeof(struct padding_9_bytes_7),
         (int)sizeof(struct padding_9_bytes_3),
         (int)sizeof(struct padding_1_bytes_1),
         (int)sizeof(struct padding_2_bytes_1),
         (int)sizeof(struct padding_3_bytes_1),
         (int)sizeof(struct padding_3_bytes_2),
         (int)sizeof(struct padding_3_bytes_3)) ;

#if defined(HQN_INT64) && ( defined(WIN64) || defined(WIN32) )
  /* Windows only because of printf() syntax. */
  {
    size_t x = (size_t)-1 ;

  /* For example, the constant OxFFFFFFFFL is a signed long. On a
     32-bit system, this sets all the bits, but on a 64-bit system,
     only the lower order 32-bits are set, resulting in the value
     0x00000000FFFFFFFF.

     If you want to turn all the bits on, a portable way to do this is
     to define a size_t constant with a value of -1. This turns all
     the bits on since twos-compliment arithmetic is used: size_t x =
     (size_t)-1 ; */
    printf("/** x = (size_t)-1 ; = 0x%I64x **/\n", x) ;

  /* Another problem that might arise is the setting of the most
     significant bit. On a 32-bit system, the constant 0x80000000 is
     used. But a more portable way of doing this is to use a shift
     expression: (size_t)1 << ((sizeof(size_t) * 8) - 1); */
    x = (size_t)1 << ((sizeof(size_t) * 8) - 1) ;
    printf("/** (size_t)1 << ((sizeof(size_t) * 8) - 1) ; = 0x%I64x **/\n", x) ;
  }
#endif

 
#if defined(HQN_INT64)
  {
    if (sizeof(uint64) != 8) {
      printf("/** uint64 type is not 8 bytes! **/\n") ;
    }
  }
#endif /* HQN_INT64 */

  /* First, determine highbytefirst. */
  {
    union {
      uint32 word ;
      uint8 bytes[4] ;
    } u ;
    int hbf ;

    u.word = 0x12345678u ;
    /* 10010  00110100  01010110  01111000
       0x12   0x34      0x56      0x78 */

    if ( u.bytes[0] == 0x12 && u.bytes[1] == 0x34 &&
         u.bytes[2] == 0x56 && u.bytes[3] == 0x78 ) {
      printf("#define highbytefirst 1\n") ;
      printf("/** Big-endian **/\n") ;
      hbf = 1 ;
    } else if ( u.bytes[0] == 0x78 && u.bytes[1] == 0x56 &&
                u.bytes[2] == 0x34 && u.bytes[3] == 0x12 ) {
      printf("#define lowbytefirst 1\n") ;
      printf("#undef highbytefirst\n") ;
      printf("/** Little-endian **/\n") ;
      hbf = 0 ;
    } else {
      printf("Unknown byte ordering\n") ;
      hbf = -1 ;
    }

#if defined(highbytefirst)
#define hbf_current 1
#else
#define hbf_current 0
#endif
    if ( hbf != hbf_current ) {
      printf("/** highbytefirst does NOT match current definition **/\n") ;
      fail = 1 ;
    }
  }

#if defined(HQN_INT64)
  /* Sanity check on the ordering of 64-bit integers. */
  {
    union {
      uint64 word ;
      uint8 bytes[8] ;
    } u ;
    int hbf ;

    u.word = UINT64(0x1234567890ABCDEF) ;
    /* 10010  00110100  01010110  01111000  10010000  10101011  11001101  11101111
       0x12   0x34      0x56      0x78      0x90      0xAB      0xCD      0xEF */

    if ( u.bytes[0] == 0x12 && u.bytes[1] == 0x34 &&
         u.bytes[2] == 0x56 && u.bytes[3] == 0x78 &&
         u.bytes[4] == 0x90 && u.bytes[5] == 0xAB &&
         u.bytes[6] == 0xCD && u.bytes[7] == 0xEF ) {
      hbf = 1 ;

      printf("/** 64-bit high byte first **/\n") ;
    } else if ( u.bytes[0] == 0xEF && u.bytes[1] == 0xCD &&
                u.bytes[2] == 0xAB && u.bytes[3] == 0x90 &&
                u.bytes[4] == 0x78 && u.bytes[5] == 0x56 &&
                u.bytes[6] == 0x34 && u.bytes[7] == 0x12 ) {
      hbf = 0 ;
      printf("/** 64-bit low byte first **/\n") ;
    } else {
      printf("Unknown byte ordering\n") ;
      hbf = -1 ;
    }

    if ( hbf != hbf_current ) {
      printf("/** highbytefirst does NOT match current definition for 64 bit integers **/\n") ;
      fail = 1 ;
    }

    /* Test if 64-bit transfer maps to 2 x 32 bit decode. In theory
       should only work on little endian machines. */
    {
      union {
        uint64 word ;
        uint8 bytes[8] ;
      } u64_1 ;
      union {
        uint32 word ;
        uint8 bytes[4] ;
      } u32_1 ;
      union {
        uint32 word ;
        uint8 bytes[4] ;
      } u32_2 ;
      uint8 transfer[8] ;
      uint8 * pTransfer = transfer;

      u64_1.bytes[0] = (uint8)0x12 ;
      u64_1.bytes[1] = (uint8)0x34 ;
      u64_1.bytes[2] = (uint8)0x56 ;
      u64_1.bytes[3] = (uint8)0x78 ;
      u64_1.bytes[4] = (uint8)0x90 ;
      u64_1.bytes[5] = (uint8)0xAB ;
      u64_1.bytes[6] = (uint8)0xCD ;
      u64_1.bytes[7] = (uint8)0xEF ;

      *((uint64 *)pTransfer) = u64_1.word ;

      u32_1.word = *((uint32 *)pTransfer) ;
      u32_2.word = *((uint32 *)&transfer[4]) ;

#if defined(highbytefirst)
      if ( u32_1.bytes[0] == 0x12 && u32_1.bytes[1] == 0x34 &&
           u32_1.bytes[2] == 0x56 && u32_1.bytes[3] == 0x78 &&

           u32_2.bytes[0] == 0x90 && u32_2.bytes[1] == 0xAB &&
           u32_2.bytes[2] == 0xCD && u32_2.bytes[3] == 0xEF ) {
        printf("/** 64-bit to 32-bit decode worked OK **/\n") ;
      } else {
        printf("/** 64-bit to 32-bit decode did NOT work **/\n") ;
      }
#else
      if ( u32_1.bytes[0] == 0x78 && u32_1.bytes[1] == 0x56 &&
           u32_1.bytes[2] == 0x34 && u32_1.bytes[3] == 0x12 &&

           u32_2.bytes[0] == 0xEF && u32_2.bytes[1] == 0xCD &&
           u32_2.bytes[2] == 0xAB && u32_2.bytes[3] == 0x90 ) {
        printf("/** 64-bit to 32-bit decode worked OK **/\n") ;
      } else {
        printf("/** 64-bit to 32-bit decode did NOT work **/\n") ;
      }
#endif
    }
  }
#endif

  /* Determine RIGHT_SHIFT_IS_SIGNED */
  {
    int rss ;
    int32 i32 = -30 ;

    rss = ((i32 >> 1) == -15 && (i32 >> 2) == -8 &&
           (i32 >> 3) == -4  && (i32 >> 4) == -2 &&
           (i32 >> 5) == -1 && (i32 >> 6) == -1) ;

    if ( rss ) {
      printf("#define RIGHT_SHIFT_IS_SIGNED 1\n") ;
    } else {
      printf("#undef RIGHT_SHIFT_IS_SIGNED\n") ;
    }

#if defined(RIGHT_SHIFT_IS_SIGNED)
#define rss_current 1
#else
#define rss_current 0
#endif
    if ( rss != rss_current ) {
      printf("/** RIGHT_SHIFT_IS_SIGNED does NOT match current definition **/\n") ;
      fail = 1 ;
    }
  }

  /* Determine Can_Shift_32, and equivalent for 64-bits */
  {
    uint32 u32 = 0x12345678u ;
    int cs32 = ((u32 << shiftby(sizeof(u32))) == 0u &&
                (u32 >> shiftby(sizeof(u32))) == 0u) ;
#if defined(HQN_INT64)
    uint64 u64 = (uint64)u32 | ((uint64)u32 << 32) ;
    int cs64 = ((u64 << shiftby(sizeof(u64))) == 0u &&
                (u64 >> shiftby(sizeof(u64))) == 0u) ;
#endif

    if ( cs32 ) {
      printf("#define Can_Shift_32 1\n") ;
    } else {
      printf("#undef Can_Shift_32\n") ;
    }

#if defined(Can_Shift_32)
#define cs_current 1
#else
#define cs_current 0
#endif
    if ( cs32 != cs_current ) {
      printf("/** Can_Shift_32 does NOT match current definition **/\n") ;
      fail = 1 ;
    }
#if defined(HQN_INT64)
    if ( cs64 != cs_current ) {
      printf("/** Can_Shift_32 does NOT work for 64-bit quantities **/\n") ;
      fail = 1 ;
    } else {
      printf("/** Can_Shift_32 DOES work for 64-bit quantities **/\n") ;
    }
#endif
  }

  return fail ;
}

/* Log stripped */
