/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:dctimpl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private implementation interfaces for JPEG filter
 */

#ifndef __DCTIMPL_H__
#define __DCTIMPL_H__

/* Incomplete type definitions */
typedef uint32 *QUANTTABLE ;
typedef struct COMPONENTINFO COMPONENTINFO ;
typedef struct HUFFTABLE HUFFTABLE ;
typedef struct scaninfo scaninfo ;
typedef struct huffgroup_t huffgroup_t ;
typedef struct DCTSTATE DCTSTATE ;
typedef struct BITBUFFER BITBUFFER;

/*
 * The encoding filter status
 */
#define AT_START_OF_IMAGE 1
#define OUTPUT_SCAN       2
#define DONE_ALL_DATA     3

/*
 * the decoding filter status
 */
#define EXPECTING_SOI  1 /* at beginning - expects start-of-image */
#define IN_SCAN        2 /* in the middle of a scan */
#define EXPECTING_EOI  3 /* just the end-of-image expected */
#define GOT_EOI        4 /* got end-of-image */

enum {
  edctmode_baselinescan,
  edctmode_progressivescan,
  edctmode_multiscan,

  edctmode_numscans
};

#define MAXHUFFTABLES_BASELINE 2
#define MAXHUFFTABLES_PROGRESSIVE 4 /* upto 4 tables are allowed in progressive JPEGs */
#define MAXHUFFTABLES MAXHUFFTABLES_PROGRESSIVE

/* JPEG Marker Codes */

/* Huffman coding */
#define  SOF0    0xc0     /* baseline dct */

#define  SOF1    0xc1     /* extended sequential DCT */
#define  SOF2    0xc2     /* progressive DCT */
#define  SOF3    0xc3     /* lossless (sequential) DCT */

#define  SOF5    0xc5     /* Differential sequential DCT */
#define  SOF6    0xc6     /* Differential progressive DCT */
#define  SOF7    0xc7     /* Differential lossless (sequential) */

/* arithmetic coding */
#define  SOF9    0xc9     /* Extended sequential DCT */
#define  SOF10   0xca     /* Progressive DCT */
#define  SOF11   0xcb     /* Lossless (sequential) */

#define  SOF13   0xcd     /* Differential sequential DCT */
#define  SOF14   0xce     /* Differential progressive DCT */
#define  SOF15   0xcf     /* Differential lossless (sequential) */



#define  DHT     0xc4     /* define Huffman table */
#define  SOI     0xd8     /* start of image */
#define  EOI     0xd9     /* end of image */
#define  SOS     0xda     /* start of scan */
#define  DQT     0xdb     /* define quantization tables */
#define  DNL     0xdc     /* define number of lines */
#define  DRI     0xdd     /* define restart interval */
#define  APP0    0xe0     /* application signalling */
#define  APP1    0xe1     /* application signalling */
#define  APP2    0xe2     /* application signalling */
#define  APP3    0xe3     /* application signalling */
#define  APP4    0xe4     /* application signalling */
#define  APP5    0xe5     /* application signalling */
#define  APP6    0xe6     /* application signalling */
#define  APP7    0xe7     /* application signalling */
#define  APP8    0xe8     /* application signalling */
#define  APP9    0xe9     /* application signalling */
#define  APPA    0xea     /* application signalling */
#define  APPB    0xeb     /* application signalling */
#define  APPC    0xec     /* application signalling */
#define  APPD    0xed     /* application signalling */
#define  APPE    0xee     /* adobe extension - color transform */
#define  APPF    0xef     /* application signalling */
#define  COM     0xfe     /* comment */

#define  RST0    0xd0     /* restart interval */
#define  RST1    0xd1
#define  RST2    0xd2
#define  RST3    0xd3
#define  RST4    0xd4
#define  RST5    0xd5
#define  RST6    0xd6
#define  RST7    0xd7

#define BSTUFF1  0xff     /**< Byte-stuffing trigger      */
#define BSTUFF2  0x00     /**< Byte-stuffing ignored byte */

/* macros - needed in gu_dct.c, gu_splat.c */

/** \brief Limit an integer to 0-255.
 *
 * \code
 * This macro must implement:
 * if ( t > 255 )
 *   t = 255 ;
 * else if ( t < 0 )
 *   t = 0 ;
 * \endcode
 *
 * The implementation below uses no branches.
 */
#define RANGE_LIMIT(_t) INLINE_RANGE32_0((_t), (_t), 0xff)

#define ONE 1L                  /* remove L if long > 32 bits */

#define LG2_DCT_SCALE 16

#define DCT_SCALE (ONE << LG2_DCT_SCALE)
#define ONE_HALF        ((int32) 1 << 15)

#define LG2_OVERSCALE 2
#define OVERSCALE (ONE << LG2_OVERSCALE)

#define FIX(x)  ((int32) ((x) * DCT_SCALE + 0.5))
#define FIXO(x)  ((int32) ((x) * DCT_SCALE / OVERSCALE + 0.5))

#define UNFIXB(_x,_y,_z) BIT_SHIFT32_SIGNED_RIGHT_EXPR((_x)+(_y), (_z))

#define UNFIX(x)    UNFIXB( (x) , (ONE << (LG2_DCT_SCALE-1)) , LG2_DCT_SCALE+0 )
#define UNFIXH(x)   UNFIXB( (x) , (ONE << (LG2_DCT_SCALE+0)) , LG2_DCT_SCALE+1 )
#define UNFIXO(x)   UNFIXB( (x) , (ONE << (LG2_DCT_SCALE-1-LG2_OVERSCALE)) , LG2_DCT_SCALE-LG2_OVERSCALE )

#define OVERSH(x)   ((x) << LG2_OVERSCALE)


/* Bit buffer is currently 8 bits. Performance may be improved by moving to
 * a 32 bit buffer. So introduced these defines as a first step in that
 * direction.
 */

/**
 * Number of bits kept in the bit buffer
 */
#define BB_NBITS 8

/**
 * Mask used to access bit buffer
 */
#define BB_MASK  0xff

#endif /* protection for multiple inclusion */


/* Log stripped */
