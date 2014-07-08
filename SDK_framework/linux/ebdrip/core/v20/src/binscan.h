/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:binscan.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Externs and macros to help implements the Level-2 feature of binary tokens
 * and binary sequences as the input to the scanner.
 */

#ifndef __BINSCAN_H__
#define __BINSCAN_H__

#include "tables.h" /* Need byte sex macros and typedefs */

typedef int32 (*scanner_func)(int32 code,
			      FILELIST *flptr) ;

extern scanner_func binary_token_table[] ;


#define theScannerFunc( c )   (binary_token_table[ ( c ) & 0x7F ])


/* the maximum depth of subarrays for printing binary sequences */
#define MAX_ARRAY_DEPTH  50 

/* The object type orderings defined for binary objects */

#define BNULL    0
#define BINTEGER 1
#define BREAL    2
#define BNAME    3
#define BBOOLEAN 4
#define BSTRING  5
#define BINAME   6
#define BARRAY   9
#define BMARK    10

/* Real Format conversion macros */

#define NativeToIEEE( n )      ( n )
#define IEEEToFloat( n )       ( n )
#define FixedToFloat( i , r )\
  ( r == 0 ? (double) i : (double) i / ( 2.0 * (double) (1 << ( r - 1))))


/* the format for binary objects  */

typedef struct {
  uint8      bin_type ;
  uint8      zero ;
  TWOBYTES   length ;
  FOURBYTES  value ;
} BINOBJECT ;

#define theIBOliteral( o )   ((int32)((o)->bin_type) & 0x80)
#define theIBOtype(o)        ((int32)((o)->bin_type) & 0x7f)
#define theIBOzero(o)        ((o)->zero)
#define theIBOsignedlen(o)   (asSignedShort((o)->length))
#define theIBOunsignedlen(o) (asShort((o)->length))
#define theIBObyte2(o)       (asBytes((o)->length))
#define theIBOvalue(o)       ((o)->value)
#define theIBObyte4(o)       (asBytes((o)->value))

/* exported to cietab34.c which wants to read a homogeneous number array too */

extern int32 get_next_bytes(int32 n,
			    register FILELIST *flptr,
			    register uint8  *buffer );

/* To avoid having lots of constants floating around... */

#define BINTOKEN_HNA    149    /* Homogenous number arrays -
                                * 4 byte header (149, number representation, 
			        * 2 byte length) + data
			        */
#define BINTOKEN_EXTHNA 150    /* HQN Extended HNA -
                                * 4 byte header (150, number representation, 0, 0)
			        * data (4 byte length, real data)
			        */

#define HNA_REP_32FP_HI      31 /* 32-bit fixed point, hi-order, scale = 0..31 */
#define HNA_REP_16FP_HI      47 /* 16-bit fixed point, hi-order, scale = (32..47) - 32 */
#define HNA_REP_IEEE_HI      48 /* 32-bit IEEE standard real, high-order byte first */
#define HNA_REP_32NREAL      49 /* 32-bit native real */

#define HNA_REP_LOWORDER    128 /* x >=128 are same as x-128, but given low-order byte first */

#define HNA_REP_32FP_LO     159 /* 32-bit fixed point, lo-order, scale = (128..159) - 128 */
#define HNA_REP_16FP_LO     175 /* 16-bit fixed point, lo-order, scale = (160..175) - 128 - 32 */
#define HNA_REP_IEEE_LO     176 /* 32-bit IEEE standard real, low-order byte first */


#endif /* protection for multiple inclusion */

/* Log stripped */
