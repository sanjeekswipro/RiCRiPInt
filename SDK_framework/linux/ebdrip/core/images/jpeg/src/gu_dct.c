/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:gu_dct.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation functions for DCT (JPEG) filter.
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "fileio.h"
#include "filterinfo.h"
#include "rsd.h"
#include "tables.h"

#include "dct.h"
#include "dctimpl.h"
#include "gu_dct.h"
#include "gu_splat.h"

#include "namedef_.h"
#include "objstack.h"

/*-------------------------------------------------------------*/

typedef int32 DCT_RESULT; /* Error status of any DCT action */
enum { /* Possible values of error status... */
  DCT_OK  =  1, /* All is well */
  DCT_ERR = -1, /* Malformed DC data error */
  DCT_EOI = -2  /* Encountered unexpected end-of-image (EOI) */
};

static void collect_simple_sample_block( register DCTSTATE *dct,
                                 register uint8 *buff,
                                 register int16 block[8][8],
                                 COMPONENTINFO *ci );
static void collect_end_sample_block( register DCTSTATE *dct,
                                     register uint8 *buff,
                                     register int16 block[8][8],
                                     int32 rows_maxi,
                                     int32 cols_maxi,
                                     COMPONENTINFO *ci );
static void forward_dct( int16 block[8][8] );


static void quantize( int16 block[8][8], int32 zz[64], uint32 *qtable );
static Bool encode_dc_huffman( register DCTSTATE *dct,
                               int32 zz[64],
                               HUFFTABLE *hufftable,
                               int32 *prediction,
                               FILELIST *filter );
static Bool encode_ac_huffman( register DCTSTATE *dct,
                               int32 zz[64],
                               HUFFTABLE *hufftable,
                               FILELIST *filter );
static Bool encode_simple_MDU( register DCTSTATE *dct,
                               uint8 *buffer,
                               FILELIST *filter );
static Bool encode_incomplete_MDU( register DCTSTATE *dct,
                                   uint8 *buffer,
                                   int32 rows,    /* rows in incomplete MDU */
                                   int32 cols,    /* cols in incomplete MDU */
                                   FILELIST *filter ) ;
static Bool put_16bit_num(register int32 c, register FILELIST *flptr);
static Bool output_bits( register scaninfo *info,
                         register int32 code,
                         register int32 nbits,
                         FILELIST *filter );
static int32 get_component( int32 id, int32 start, DCTSTATE *dctstate );

static void convert_RGB_to_YUV( uint8 *buff, int32 nbytes );
static void convert_CMYK_to_YUVK( uint8 *buff, int32 nbytes );
static void color_transform_MDU( register DCTSTATE *dct ,
                                 register uint8 *buffer ) ;
static void transfer_direct_colors( register DCTSTATE *dct ,
                                    uint8 *buffer ) ;
static void transfer_YUV_to_RGB( register DCTSTATE *dct ,
                                 uint8* buffer ) ;
static void transfer211_YUV_to_RGB( register DCTSTATE *dct ,
                                    uint8* buffer ) ;
static void transfer_YUVK_to_CMYK( register DCTSTATE *dct ,
                                   uint8* buffer ) ;

static Bool decode_progressive(DCTSTATE *dct, FILELIST *flptr);

static Bool process_restart( register DCTSTATE *dct, FILELIST *flptr );

static Bool fetch_zigzag(DCTSTATE *dct, FILELIST *flptr, scaninfo * info);

static DCT_RESULT skip_baseline_zigzag(DCTSTATE *dct, FILELIST *flptr,
                                       scaninfo *info);

static DCT_RESULT skip_zigzag_DC_successive( DCTSTATE *dct,
                                             FILELIST *flptr,
                                             scaninfo * info );
static DCT_RESULT skip_zigzag_DC( DCTSTATE *dct,
                                  FILELIST *flptr,
                                  scaninfo * info );
static DCT_RESULT skip_zigzag_AC( DCTSTATE *dct,
                                  FILELIST *flptr,
                                  scaninfo * info );
static DCT_RESULT skip_zigzag_AC_successive( DCTSTATE *dct,
                                             FILELIST *flptr,
                                             scaninfo * info );

static DCT_RESULT fetch_zigzag_DC_successive( DCTSTATE *dct,
                                              FILELIST *flptr,
                                              scaninfo * info );
static DCT_RESULT fetch_zigzag_DC( DCTSTATE *dct,
                                   FILELIST *flptr,
                                   scaninfo * info );
static DCT_RESULT fetch_zigzag_AC_successive( DCTSTATE *dct,
                                              FILELIST *flptr,
                                              scaninfo * info );
static DCT_RESULT fetch_zigzag_AC( DCTSTATE *dct,
                                   FILELIST *flptr,
                                   scaninfo * info );

static Bool decode_noninterleaved( DCTSTATE *dct,
                                   FILELIST *flptr, scaninfo * info );

static DCT_RESULT decode_zigzag_band_DC( DCTSTATE *dct,
                                         FILELIST *flptr,
                                         scaninfo * info );
static DCT_RESULT decode_zigzag_band_AC( DCTSTATE *dct,
                                         FILELIST *flptr,
                                         scaninfo * info );
static DCT_RESULT decode_normal_zigzag( DCTSTATE *dct,
                                        FILELIST *flptr,
                                        scaninfo * info );

typedef DCT_RESULT (*zigzag_decode)(DCTSTATE *dct,
                                    FILELIST *flptr,
                                    scaninfo * info );
enum {
  e_zigzag_DC,
  e_zigzag_DC_successive,
  e_zigzag_AC,
  e_zigzag_AC_successive,

  e_zigzag_baseline_noninterleaved,

  e_num_scantypes
};

static zigzag_decode zigzag_skip[e_num_scantypes] = {
  skip_zigzag_DC,
  skip_zigzag_DC_successive,
  skip_zigzag_AC,
  skip_zigzag_AC_successive,
  skip_baseline_zigzag
};

static zigzag_decode zigzag_fetch[e_num_scantypes] = {
  fetch_zigzag_DC,
  fetch_zigzag_DC_successive,
  fetch_zigzag_AC,
  fetch_zigzag_AC_successive,

  NULL
};

static zigzag_decode zigzag_decodes[e_num_scantypes] = {
    decode_zigzag_band_DC,
    fetch_zigzag_DC_successive,
    decode_zigzag_band_AC,
    fetch_zigzag_AC_successive,

    decode_normal_zigzag
};

/*------------------------------------------------------------*/


#define SIN_1_4 FIX(0.7071067811856476)
#define COS_1_4 SIN_1_4

#define SIN_1_8 FIX(0.3826834323650898)
#define COS_1_8 FIX(0.9238795325112870)
#define SIN_3_8 COS_1_8
#define COS_3_8 SIN_1_8

#define SIN_1_16 FIX(0.1950903220161282)
#define COS_1_16 FIX(0.9807852804032300)
#define SIN_7_16 COS_1_16
#define COS_7_16 SIN_1_16

#define SIN_3_16 FIX(0.5555702330196022)
#define COS_3_16 FIX(0.8314696123025450)
#define SIN_5_16 COS_3_16
#define COS_5_16 SIN_3_16

#define OSIN_1_4 FIXO(0.707106781185647)
#define OCOS_1_4 OSIN_1_4

#define OSIN_1_8 FIXO(0.3826834323650898)
#define OCOS_1_8 FIXO(0.9238795325112870)
#define OSIN_3_8 OCOS_1_8
#define OCOS_3_8 OSIN_1_8

#define OSIN_1_16 FIXO(0.1950903220161282)
#define OCOS_1_16 FIXO(0.9807852804032300)
#define OSIN_7_16 OCOS_1_16
#define OCOS_7_16 OSIN_1_16

#define OSIN_3_16 FIXO(0.5555702330196022)
#define OCOS_3_16 FIXO(0.8314696123025450)
#define OSIN_5_16 OCOS_3_16
#define OCOS_5_16 OSIN_3_16

#define RST0_SYNC (-1) /* A value it can't take in practise. */

/* ----------------------------------------------------------------------------

   Static data for DCT Encode and Decode

---------------------------------------------------------------------------- */


/* Debugging switches */

#if defined( ASSERT_BUILD )
static Bool debug_dctdecode = TRUE ;
#endif


static uint32 zig_zag[8][8] = {
  {  0,  2,  3,  9, 10, 20, 21, 35 },
  {  1,  4,  8, 11, 19, 22, 34, 36 },
  {  5,  7, 12, 18, 23, 33, 37, 48 },
  {  6, 13, 17, 24, 32, 38, 47, 49 },
  { 14, 16, 25, 31, 39, 46, 50, 57 },
  { 15, 26, 30, 40, 45, 51, 56, 58 },
  { 27, 29, 41, 44, 52, 55, 59, 62 },
  { 28, 42, 43, 53, 54, 60, 61, 63 }
} ;


/* this is the correct zig zag order
 * but the encode filter is uses the transpose and
 * then tranpose the DCT coeff again into filter buffer
 */
/*
static uint32 zig_zag[8][8] = {
   0,  1,  5,  6, 14, 15, 27, 28,
   2,  4,  7, 13, 16, 26, 29, 42,
   3,  8, 12, 17, 25, 30, 41, 43,
   9, 11, 18, 24, 31, 40, 44, 53,
  10, 19, 23, 32, 39, 45, 52, 54,
  20, 22, 33, 38, 46, 51, 55, 60,
  21, 34, 37, 47, 50, 56, 59, 61,
  35, 36, 48, 49, 57, 58, 62, 63,
};
*/

uint32 encoded_adobe_dc_hufftable[] = {
  /* the number of codes for bit lengths 1 to 16 ... */
  0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  /* the symbol definitions */
  0x03, 0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
} ;
int32 adobe_dc_huff_length = NUM_ARRAY_ITEMS(encoded_adobe_dc_hufftable);


uint32 encoded_adobe_ac_hufftable[] = {
  /* the number of codes for bit lengths 1 to 16 ... */
  0x00, 0x02, 0x01, 0x03, 0x02, 0x04, 0x04, 0x03, 0x05, 0x05, 0x05, 0x02,
  0x00, 0x00, 0x01, 0x7D,
  /* the symbol definitions */
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x21, 0x06, 0x12, 0x31, 0x41,
  0x07, 0x13, 0x51, 0x61, 0x22, 0x71, 0x81, 0x14, 0x32, 0x91, 0xA1, 0xB1,
  0x08, 0x23, 0x42, 0x52, 0xC1, 0x15, 0x33, 0x62, 0x72, 0xD1, 0xE1, 0xF0,
  0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
  0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74,
  0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
  0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2,
  0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5,
  0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
  0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2,
  0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
  0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
} ;
int32 adobe_ac_huff_length = NUM_ARRAY_ITEMS(encoded_adobe_ac_hufftable);




/* the default quantization table, in zig-zag order */
uint32 encoded_adobe_qtable[] = {
  0x10, 0x0B, 0x0C, 0x0E, 0x0C, 0x0A, 0x10, 0x0E,
  0x0D, 0x0E, 0x22, 0x11, 0x10, 0x13, 0x18, 0x28,
  0x1A, 0x18, 0x16, 0x16, 0x28, 0x31, 0x23, 0x25,
  0x1D, 0x28, 0x3A, 0x33, 0x3D, 0x3C, 0x39, 0x33,
  0x38, 0x37, 0x40, 0x48, 0x5C, 0x4E, 0x40, 0x44,
  0x57, 0x45, 0x37, 0x38, 0x50, 0x6D, 0x51, 0x57,
  0x5F, 0x62, 0x67, 0x68, 0x67, 0x3E, 0x4D, 0x71,
  0x79, 0x70, 0x64, 0x78, 0x5C, 0x65, 0x67, 0x63,
} ;


/* ptrs into below YUV-to-RGB conversion tables */
static int32 *v_r_tab, *u_b_tab, *v_g_tab, *u_g_tab;

#define TAB_MARGIN 64  /* margin to allow unclipped indices */
#define YUV_RGB_TABLESIZE ( 256 + 2 * TAB_MARGIN )

static int32 V_R_tab[YUV_RGB_TABLESIZE]; /* => V to R conversion */
static int32 U_B_tab[YUV_RGB_TABLESIZE]; /* => U to B conversion */
static int32 V_G_tab[YUV_RGB_TABLESIZE]; /* => V to G conversion */
static int32 U_G_tab[YUV_RGB_TABLESIZE]; /* => U to G conversion */

static int32 R_Y_tab[256]; /* => table for R to Y conversion */
static int32 G_Y_tab[256]; /* => table for G to Y conversion */
static int32 B_Y_tab[256]; /* => table for B to Y conversion */
static int32 R_U_tab[256]; /* => table for R to U conversion */
static int32 G_U_tab[256]; /* => table for G to U conversion */
static int32 B_U_tab[256]; /* => table for B to U conversion */
static int32 R_V_tab[256]; /* => table for R to V conversion */
static int32 G_V_tab[256]; /* => table for G to V conversion */
static int32 B_V_tab[256]; /* => table for B to V conversion */

static HqBool inited_RGB_to_YUV_tables;
static HqBool inited_YUV_to_RGB_tables;

/* ----------------------------------------------------------------------------

   Routines for unpacking PostScript Options to the DCT filter

---------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
   function:            unpack_samples_array  author:              Luke Tunmer
   creation date:       24-Sep-1991           last modification:   ##-###-####
   arguments:
   description:

   Unpacks the HSamples or VSamples arrays passed in the filter dictionary.

---------------------------------------------------------------------------- */
Bool unpack_samples_array(DCTSTATE *dctstate, OBJECT *sarray, Bool horiz)
{
  int32 len , v , i ;

  len = theLen(*sarray) ;
  if ( len != dctstate->colors)
    return error_handler( TYPECHECK ) ;
  sarray = oArray(*sarray) ;
  for ( i = 0 ; i < len ; i++ ) {
    if ( oType(*sarray) != OINTEGER )
      return error_handler( TYPECHECK );
    v = oInteger(*sarray) ;
    if (( v <= 0 ) || ( v > 4 ))
      return error_handler( TYPECHECK ) ;
    if ( horiz )
      dctstate->components[i].num_hsamples = v ;
    else
      dctstate->components[i].num_vsamples = v ;
    sarray++ ;
  }
  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            unpack_quant_array author:              Luke Tunmer
   creation date:       24-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Unpacks the array of Quantization tables given in the filter dictionary

---------------------------------------------------------------------------- */
Bool unpack_quant_array(DCTSTATE *dctstate, OBJECT *qarray)
{
  OBJECT *o , *numo ;
  int32 len , type , i , j ;
  int32 found ;
  double qfactor ;
  uint32*  qtable;

  len = theLen(*qarray) ;
  if ( len != dctstate->colors)
    return error_handler( RANGECHECK ) ;
  o = qarray = oArray(*qarray) ;
  dctstate->num_qtables = 0 ;
  qfactor = dctstate->qfactor ;

  for ( i = 0 ; i < len ; i++ ) {
    type = oType(*o) ;
    if ( type != OSTRING && type != OARRAY && type != OPACKEDARRAY )
      return error_handler( TYPECHECK ) ;
    if ( theLen(*qarray) != 64 )
      return error_handler( RANGECHECK ) ;
    /* check if used previously */
    found = FALSE ;
    for ( j = 0 ; j < i ; j++ ) {
      if ( OBJECT_GET_D1( qarray[j] ) == OBJECT_GET_D1( *o )) {
        found = TRUE ;
        dctstate->components[i].qtable_number =
          dctstate->components[j].qtable_number ;
        break ;
      }
    }
    if ( ! found ) {
      qtable = ( uint32 * )mm_alloc( mm_pool_temp ,
                                     64 * sizeof( uint32 ) ,
                                     MM_ALLOC_CLASS_DCT_QUANT ) ;
      if ( qtable == NULL )
        return error_handler( VMERROR ) ;

      if ( type == OSTRING ) {
        uint8 *str = oString(*o) ;
        for ( j = 0 ; j < 64 ; j++ ) {
          USERVALUE q = (USERVALUE)(*str * qfactor + .5) ;
          if ( q > 255.0f )
            q = 255.0f ;
          qtable[j] = (uint32) q ;
          str++ ;
        }
      } else {
        /* unpack array into qtable */
        for ( j = 0, numo = oArray(*o) ; j < 64 ; ++j, ++numo ) {
          USERVALUE q ;
          if ( !object_get_real(numo, &q) )
            return FALSE ;
          q = (USERVALUE)(q * qfactor + 0.5) ;
          if ( q > 255.0f )
            q = 255.0f ;
          qtable[j] = (uint32) q ;
        }
      }

      dctstate->quanttables[dctstate->num_qtables++] = qtable ;
    }

    o++ ;
  }

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            unpack_huff_array author:              Luke Tunmer
   creation date:       24-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Unpacks the array of Huffman tables given in the filter dictionary

---------------------------------------------------------------------------- */
Bool unpack_huff_array(DCTSTATE *dctstate, OBJECT *harray)
{
  OBJECT *o ;
  HUFFTABLE *huff ;
  int32    len , hlen , type , i , j ;
  Bool    found , dc ;
  uint32*  htable ;
  uint32 * dc_huffs;
  uint32 * ac_huffs;
  huffgroup_t * group;

  len = theLen(*harray) ;
  if ( len != ( 2 * dctstate->colors))
    return error_handler( RANGECHECK ) ;
  o = harray = oArray(*harray) ;
  dctstate->dc_huff.num = 0 ;
  dctstate->ac_huff.num = 0 ;
  dc = TRUE ;

  dc_huffs = dctstate->currinfo->dc_huff_number;
  ac_huffs = dctstate->currinfo->ac_huff_number;

  for ( i = 0 ; i < len ; i++ ) {
    if (dc)
      group = &dctstate->dc_huff;
    else
      group = &dctstate->ac_huff;

    type = oType(*o) ;
    if ( type != OSTRING && type != OARRAY && type != OPACKEDARRAY )
      return error_handler( TYPECHECK ) ;
    if ( (hlen = theLen(*o)) < 16 )
      return error_handler( RANGECHECK ) ;
    /* check if used previously */
    found = FALSE ;
    for ( j = 0 ; j < i ; j++ ) {
      if ( OBJECT_GET_D1( harray[j] ) == OBJECT_GET_D1( *o )) {
        found = TRUE ;
        if ( dc )
          dc_huffs[i] = (j & 0x1 ? ac_huffs[j] : dc_huffs[j]) ;
        else
          ac_huffs[i] = (j & 0x1 ? ac_huffs[j] : dc_huffs[j]) ;
        break ;
      }
    }
    if ( ! found ) {
      if ( group->num == 2 )
        return error_handler( RANGECHECK ) ;

      htable = ( uint32 * )mm_alloc( mm_pool_temp ,
                                     hlen * sizeof( uint32 ) ,
                                     MM_ALLOC_CLASS_HUFF_TABLE ) ;
      if ( htable == NULL )
        return error_handler( VMERROR ) ;

      if ( type == OSTRING ) {
        uint8 *pCharTable = oString(*o);
        for (j = 0; j< hlen; j++) {
           htable[j] = *pCharTable++;
        }

      } else {
        /* unpack array into qtable */
        OBJECT *numo = oArray(*o) ;
        for ( j = 0 ; j < hlen ; j++ ) {
          if ( oType(*numo) == OINTEGER )
            htable[j] = (uint32)oInteger(*numo) ;
          else if ( oType(*numo) == OREAL )
            htable[j] = (uint32)(oReal(*numo) + .5) ;
          else {
            HQFAIL("Huffman table entry type not defined") ;
            return error_handler(TYPECHECK) ;
          }
          numo++ ;
        }
      }

      huff = &group->tables[group->num++] ;
      huff->encoded_length = hlen ;
      huff->encoded_hufftable = htable ;
      if ( ! make_huffman_table( huff , dc , TRUE ))
        return FALSE ;
    }
    dc = ! dc ;
    o++ ;
  }

  return TRUE ;
}

/*
 * Return TRUE if the horiz or vertical sampling ratios are non-integral.
 * That is 3:2 or 4:3.
 */
Bool check_non_integral_ratios(DCTSTATE *dctstate)
{
  int32 i ;
  Bool check ;
  COMPONENTINFO *ci ;

  if ( dctstate->max_hsamples < 3 && dctstate->max_vsamples < 3 )
    return FALSE ; /* cannot be non-integral ratios between 1's and 2's */

  /* check horizontal first */
  check = FALSE ;
  ci = dctstate->components ;
  for ( i = 0 ; i < dctstate->colors ; i++ , ci++ ) {
    if ( ci->num_hsamples == 3 ) {
      check = TRUE ;
      break ;
    }
  }
  if ( check ) {
    ci = dctstate->components ;
    for ( i = 0 ; i < dctstate->colors ; i++ , ci++ ) {
      if ( ci->num_hsamples == 2 || ci->num_hsamples == 4 )
        return TRUE ;
    }
  }

  /* check vertical */
  check = FALSE ;
  ci = dctstate->components ;
  for ( i = 0 ; i < dctstate->colors ; i++ , ci++ ) {
    if ( ci->num_vsamples == 3 ) {
      check = TRUE ;
      break ;
    }
  }
  if ( check ) {
    ci = dctstate->components ;
    for ( i = 0 ; i < dctstate->colors ; i++ , ci++ ) {
      if ( ci->num_hsamples == 2 || ci->num_hsamples == 4 )
        return TRUE ;
    }
  }
  return FALSE ;
}



/* ----------------------------------------------------------------------------

   Routines for Encode DCT

---------------------------------------------------------------------------- */



/*
 * Put an unsigned 16 bit number into the output stream
 */
static Bool put_16bit_num( register int32 c, register FILELIST *flptr )
{
  register int32 c1 , c2 ;
  Bool result ;

  c1 = ( c >> 8 ) & 0xff ;
  c2 = c & 0xff ;
  result = (Putc(c1, flptr) != EOF &&
            Putc(c2, flptr) != EOF) ;

  if ( !result )
    result = error_handler(IOERROR) ;

  return result ;
}


/*
 * Write the 0xff marker, followed by the code to the flptr stream
 */
Bool output_marker_code( register int32 code, register FILELIST *flptr )
{
  Bool result = (Putc(0xff, flptr) != EOF &&
                 Putc(code, flptr) != EOF) ;

  if ( !result )
    result = error_handler(IOERROR) ;

  return result ;
}

/*
 * Output Adobe extension field
 */
Bool output_adobe_extension(FILELIST *filter, DCTSTATE *dctstate)
{
  register FILELIST *flptr ;
  register int32 i;

  flptr = theIUnderFile( filter ) ;

  if ( ! output_marker_code( APPE , flptr ) ||
       ! put_16bit_num( 9 , flptr ) ||
       ( Putc( 0 , flptr ) == EOF ) ||
       ( Putc( 0x64 , flptr ) == EOF ))
    return error_handler( IOERROR ) ;

  for ( i = 0 ; i < 4 ; i++ ) {
    if ( Putc( 0 , flptr ) == EOF )
      return error_handler( IOERROR ) ;
  }

  if ( Putc( dctstate->colortransform , flptr ) == EOF )
    return error_handler( IOERROR ) ;

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            output_quanttables author:              Luke Tunmer
   creation date:       24-Sep-1991        last modification:   ##-###-####
   arguments:
   description:

   Output all the quantization tables which are being used by JPEG as part of
   the JPEG header.

---------------------------------------------------------------------------- */
Bool output_quanttables(FILELIST *filter)
{
  int32 i ;
  int32 table_num ;
  register int32 c ;
  register uint32 *ptr ;
  DCTSTATE *dctstate ;
  FILELIST *flptr ;

  dctstate = theIFilterPrivate( filter ) ;
  flptr = theIUnderFile( filter ) ;

  for (table_num = 0 ;
       (uint32)table_num < dctstate->num_qtables ;
       table_num++ ) {
    if ( ! output_marker_code( DQT , flptr ))
      return FALSE ;
    /* output the length field */
    if ( ! put_16bit_num( 67 , flptr ))
      return FALSE ;

    /* output the (precision / table number) byte */
    if ( Putc( table_num , flptr ) == EOF )
      return error_handler( IOERROR ) ;

    /* output the 64 values in the quantization table */
    ptr = dctstate->quanttables[ table_num ] ;
    for ( i = 0 ; i < 64 ; i++ ) {
      c = *ptr++ ;
      if ( Putc( c , flptr ) == EOF )
        return error_handler( IOERROR ) ;
    }
  }

  return TRUE ;
}




/* ----------------------------------------------------------------------------
   function:            output_start_of_frame author:              Luke Tunmer
   creation date:       25-Sep-1991           last modification:   ##-###-####
   arguments:
   description:

   Output the SOF marker, followed by the frame signalling parameters.
---------------------------------------------------------------------------- */
Bool output_start_of_frame(FILELIST *filter)
{
  register DCTSTATE *dctstate ;
  register FILELIST *flptr ;
  register int32 ncomponents , i ;
  register int32 c ;

  dctstate = theIFilterPrivate( filter ) ;
  flptr = theIUnderFile( filter ) ;

  if ( ! output_marker_code( SOF0 , flptr ))
    return FALSE ;
  /* output the length of the frame signalling parameters */
  ncomponents = dctstate->colors ;
  c = ncomponents * 3 + 8 ;
  if ( ! put_16bit_num( c , flptr ))
    return FALSE ;
  /* output the precision of the frame - 8 bits */
  if ( Putc( 8 , flptr ) == EOF )
    return error_handler( IOERROR ) ;
  /* output the number of lines */
  if ( ! put_16bit_num( dctstate->rows , flptr ))
    return FALSE ;
  /* output the line length */
  if ( ! put_16bit_num( dctstate->columns , flptr ))
    return FALSE ;

  /* output the component specification for each component */
  if ( Putc( ncomponents , flptr ) == EOF )
    return error_handler( IOERROR ) ;
  for ( i = 0 ; i < ncomponents ; i++ ) {
    /* number assigned to kth image component */
    if ( Putc( dctstate->components[ i ].id_num , flptr ) == EOF )
      return error_handler( IOERROR ) ;
    c = (uint8) (dctstate->components[i].num_hsamples << 4 |
                 dctstate->components[i].num_vsamples) ;
    if ( Putc( c , flptr ) == EOF )
      return error_handler( IOERROR ) ;
    c = dctstate->components[i].qtable_number ;
    if ( Putc( c , flptr ) == EOF )
      return error_handler( IOERROR ) ;
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            output_hufftables author:              Luke Tunmer
   creation date:       25-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Output the Huffman tables being used by the JPEG compressor as part of
   the JPEG header.
---------------------------------------------------------------------------- */
Bool output_hufftables(FILELIST *filter)
{
  register DCTSTATE *dctstate ;
  register FILELIST *flptr ;
  register int32    c , j , i ;
  register uint32  *ptr ;
  int32    len , bytes ;


  dctstate = theIFilterPrivate( filter ) ;
  flptr = theIUnderFile( filter ) ;

  if ( ! output_marker_code( DHT , flptr ))
    return FALSE ;

  /* calculate the total length of the hufftable sections */
  bytes = 2 ; /* 2 bytes in the length field */
  for ( i = 0 ; i < (int32)dctstate->dc_huff.num ; i++ )
    bytes += dctstate->dc_huff.tables[i].encoded_length + 1 ;
  for ( i = 0  ; i < (int32)dctstate->ac_huff.num ; i++ )
    bytes += dctstate->ac_huff.tables[i].encoded_length + 1 ;
  if ( ! put_16bit_num( bytes , flptr ))
    return FALSE ;

  /* output dc hufftables */
  for ( i = 0 ; i < (int32)dctstate->dc_huff.num ; i++ ) {
    /* output the dc hufftable number */
    c = i ;
    if ( Putc( c , flptr ) == EOF )
      return error_handler( IOERROR ) ;
    len = dctstate->dc_huff.tables[i].encoded_length ;
    ptr = dctstate->dc_huff.tables[i].encoded_hufftable ;
    for ( j = 0 ; j < len ; j++ ) {
      c = *ptr++ ;
      if ( Putc( c , flptr ) == EOF )
        return error_handler( IOERROR ) ;
    }
  }

  /* output ac hufftables */
  for ( i = 0 ; i < (int32)dctstate->ac_huff.num ; i++ ) {
    /* output the ac hufftable number */
    c = i | 0x10 ;
    if ( Putc( c , flptr ) == EOF )
      return FALSE ;
    len = dctstate->ac_huff.tables[i].encoded_length ;
    ptr = dctstate->ac_huff.tables[i].encoded_hufftable ;
    for ( j = 0 ; j < len ; j++ ) {
      c = *ptr++ ;
      if ( Putc( c , flptr ) == EOF )
        return error_handler( IOERROR ) ;
    }
  }

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            output_scan       author:              Luke Tunmer
   creation date:       25-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Output the SOS marker code, followed by the scan signalling parameters.
---------------------------------------------------------------------------- */
Bool output_scan(FILELIST *filter)
{
  register DCTSTATE *dctstate ;
  register FILELIST *flptr ;
  register int32    c , i ;
  int32    len , colors ;
  uint32 * dc_huffs;
  uint32 * ac_huffs;


  dctstate = theIFilterPrivate( filter ) ;
  flptr = theIUnderFile( filter ) ;
  colors = dctstate->colors ;

  if ( ! output_marker_code( SOS , flptr ))
    return FALSE ;

  /* calculate length of scan parameters */
  len = 6 + 2 * colors ;
  if ( ! put_16bit_num( len , flptr ))
    return FALSE ;

  /* output component specifications */
  if ( Putc( colors , flptr ) == EOF )
    return error_handler( IOERROR ) ;

  dc_huffs = dctstate->currinfo->dc_huff_number;
  ac_huffs = dctstate->currinfo->ac_huff_number;
  for ( i = 0 ; i < colors ; i++ ) {
    c = dctstate->components[i].id_num ;
    if ( Putc( c , flptr ) == EOF )
      return error_handler( IOERROR ) ;

    c = (uint8) ( (int32) (dc_huffs[i]) << 4 |
      (int32) (ac_huffs[i]) ) ;
    if ( Putc( c , flptr ) == EOF )
      return error_handler( IOERROR ) ;
  }

  /* output the start of spectral selection byte */
  if ( Putc( 0 , flptr ) == EOF )
    return error_handler( IOERROR ) ;
  /* output the end of spectral selection byte */
  if ( Putc( 63 , flptr ) == EOF )
    return error_handler( IOERROR ) ;
  /* output the successive approx bits */
  if ( Putc( 0 , flptr ) == EOF )
    return error_handler( IOERROR ) ;

  return TRUE ;
}



#define INVALID_CODE -1
#define LONG_CODE    -2 /* these two values must not be positive */


/* ----------------------------------------------------------------------------
   function:            make_huffman_table    author:              Luke Tunmer
   creation date:       21-Aug-1991           last modification:   ##-###-####
   arguments:
   description:

   Expand the encoded form of the hufftable into the arrays code_lengths and
   codes. (See p91 JPEG specs).

---------------------------------------------------------------------------- */
Bool make_huffman_table(HUFFTABLE *hufftable, Bool dctable, Bool encoding)
{
  int32 ncodes ;
  int32 count , value , max, prefix;
  register int32 i, j, length, code;
  register int32 p ;
  int32 huffsize[257];
  uint32 huffcode[257];

  int32  *hashtable;

  /* get the number of codes in the hufftable */
  if ( dctable ) {
    ncodes = hufftable->encoded_length - 16 ;
  } else
    ncodes = 256 ;
  hufftable->num_of_codes = ncodes ;

  hufftable->huffval = hufftable->encoded_hufftable + 16 ;

  hufftable->hashtable = ( int32 * )mm_alloc( mm_pool_temp ,
                                              256 * sizeof( int32 ) ,
                                              MM_ALLOC_CLASS_HUFF_TABLE ) ;
  if ( hufftable->hashtable == NULL )
    return error_handler( VMERROR ) ;

  hashtable = hufftable->hashtable;

  for (i=0; i<256; i++) {
    hashtable[i]  = INVALID_CODE;
  }

  count = 0 ;
  for ( length = 1 ; length <= 16 ; length++ ) {
    value = hufftable->encoded_hufftable[ length - 1 ] ;
    for ( i = 0 ; i < value ; i++ ) {
      huffsize[ count++ ] =  length ;
    }
  }
  huffsize[count] = 0;

  code = 0 ;
  length = huffsize[0];
  i = 0 ;


  while ( huffsize[i] ) {
    while ( huffsize[i] == length ) {
      huffcode[i] = code;

      /* set up the hash table for this code */

      if ( length <= 8 ) {
        max = 1 << (8-length);
        prefix = code << (8-length);
        value = (length<<16) | ( hufftable->huffval[i]);
        for (j=0; j< max; j++)
          hashtable[ prefix|j ] = value;
      } else {
        prefix = code >> (length-8);
        hashtable[ prefix ] = LONG_CODE;
      }
      code++; i++;
    }
    code <<= 1 ;
    length++ ;
  }

  if ( encoding ) {
    register uint32  *plengths ;
    register uint32 *pcodes ;

    plengths = ( uint32 * )mm_alloc( mm_pool_temp ,
                                     ncodes * sizeof( uint32 ) ,
                                     MM_ALLOC_CLASS_HUFF_TABLE ) ;
    if ( plengths == NULL )
      return error_handler( VMERROR ) ;
    hufftable->code_lengths = plengths ;

    pcodes = ( uint32 * )mm_alloc( mm_pool_temp ,
                                   ncodes * sizeof( uint32 ) ,
                                   MM_ALLOC_CLASS_HUFF_TABLE ) ;
    if ( pcodes == NULL )
      return error_handler( VMERROR ) ;
    hufftable->codes = pcodes ;


    HqMemZero(plengths, ncodes * sizeof(uint32));

    for (p = 0; p < i; p++) {
      value = hufftable->huffval[p] ;
      pcodes[value] = huffcode[p];
      plengths[value] = huffsize[p];
    }

  } else {
    register uint32 *mincode ;
    register int32  *maxcode ;
    register uint32  *valptr ;
    register int32 length ;
    register uint32 *ncodes_bp  ;

    hufftable->mincode = ( uint32 * )mm_alloc( mm_pool_temp ,
                                               16 * sizeof( uint32 ) ,
                                               MM_ALLOC_CLASS_HUFF_TABLE ) ;
    if ( hufftable->mincode == NULL )
      return error_handler( VMERROR ) ;
    hufftable->maxcode = ( int32 * )mm_alloc( mm_pool_temp ,
                                              16 * sizeof( uint32 ) ,
                                              MM_ALLOC_CLASS_HUFF_TABLE ) ;
    if ( hufftable->maxcode == NULL )
      return error_handler( VMERROR ) ;
    hufftable->valptr = ( uint32 * )mm_alloc( mm_pool_temp ,
                                              16 * sizeof( uint32 ) ,
                                              MM_ALLOC_CLASS_HUFF_TABLE ) ;
    if ( hufftable->valptr == NULL )
      return error_handler( VMERROR ) ;

    p = 0 ;
    ncodes_bp = hufftable->encoded_hufftable ;
    mincode = hufftable->mincode ;
    maxcode = hufftable->maxcode ;
    valptr = hufftable->valptr ;
    /* length is one less than the actual length! */
    for ( length = 0 ; length < 16 ; length++ ) {
      if ( (int32) (ncodes_bp[ length ]) == 0 ) {
        maxcode[ length ] = -1 ;
      } else {
        valptr[ length ] = (uint32) p ;
        mincode[ length ] = huffcode[p] ;
        p += (int32) (ncodes_bp[ length ]) - 1 ;
        maxcode[ length ] = huffcode[ p ] ;
        p++ ;
      }
    }

#ifdef debug_hufftable
    /* print out hufftable */
    {
      int32 j , mask ;
      monitorf("Huffman %s table:\n", dctable ? "DC" : "AC" ) ;
      for ( i = 0 ; i < ncodes ; i++ ) {
        if ( plengths[i] ) {
          monitorf("%x/%x\t%d\t", i >> 4 , i & 0xf , plengths[i]) ;
          mask = 1 << ( plengths[i] - 1 ) ;
          for ( j = 0 ; j < plengths[i] ; j++ ) {
            monitorf("%c", pcodes[i] & mask ? '1' : '0' ) ;
            mask >>= 1 ;
          }
          monitorf("\n") ;
        }
      }
    }
#endif
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            init_RGB_to_YUV_tables       author:   Luke Tunmer
   creation date:       13-Aug-1992       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool init_RGB_to_YUV_tables( void )
{
  int32 i ;

  if ( inited_RGB_to_YUV_tables )
    return TRUE;

  inited_RGB_to_YUV_tables = TRUE ;

  for ( i = 0 ; i < 256 ; i++ ) {
    R_Y_tab[i] = FIX(0.29900) * i;
    G_Y_tab[i] = FIX(0.58700) * i  + ONE_HALF;
    B_Y_tab[i] = FIX(0.11400) * i;
    R_U_tab[i] = (-FIX(0.16874)) * i;
    G_U_tab[i] = (-FIX(0.33126)) * i  + ONE_HALF*(256);
    B_U_tab[i] = FIX(0.50000) * i;
    R_V_tab[i] = FIX(0.50000) * i;
    G_V_tab[i] = (-FIX(0.41869)) * i  + ONE_HALF*(256);
    B_V_tab[i] = (-FIX(0.08131)) * i;
  }
  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            convert_RGB_to_YUV author:              Luke Tunmer
   creation date:       10-Sep-1991        last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static void convert_RGB_to_YUV( uint8 *buff, int32 nbytes )
{
  register int32 r, g, b, i ;

  for ( i = 0 ; i < nbytes ; i += 3 ) {
    r = buff[0] ;
    g = buff[1] ;
    b = buff[2] ;
    /* If the inputs are 0..255, the outputs of these equations
     * must be too; we do not need an explicit range-limiting operation.
     * Hence the value being shifted is never negative, and we don't
     * need the general RIGHT_SHIFT macro.
     */
    /* Y */
    *buff++ = (uint8) ((R_Y_tab[r] + G_Y_tab[g] + B_Y_tab[b]) >> 16 );
    /* U */
    *buff++ = (uint8) ((R_U_tab[r] + G_U_tab[g] + B_U_tab[b]) >> 16 );
    /* V */
    *buff++ = (uint8) ((R_V_tab[r] + G_V_tab[g] + B_V_tab[b]) >> 16 );
  }
}



/* ----------------------------------------------------------------------------
   function:            convert_CMYK_to_YUVK author:              Luke Tunmer
   creation date:       10-Sep-1991          last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static void convert_CMYK_to_YUVK( uint8 *buff, int32 nbytes )
{
  register int32 r, g, b, i ;

  for ( i = 0 ; i < nbytes ; i += 4 ) {
    r = 255 - (int32)buff[0] ;
    g = 255 - (int32)buff[1] ;
    b = 255 - (int32)buff[2] ;
    /* If the inputs are 0..255, the outputs of these equations
     * must be too; we do not need an explicit range-limiting operation.
     * Hence the value being shifted is never negative, and we don't
     * need the general RIGHT_SHIFT macro.
     */
    /* Y */
    *buff++ = (uint8)((R_Y_tab[r] + G_Y_tab[g] + B_Y_tab[b]) >> 16 );
    /* U */
    *buff++ = (uint8) ((R_U_tab[r] + G_U_tab[g] + B_U_tab[b]) >> 16 );
    /* V */
    *buff++ = (uint8) ((R_V_tab[r] + G_V_tab[g] + B_V_tab[b]) >> 16 );
    /* k unmodified */
    buff++ ;
  }
}





/* ----------------------------------------------------------------------------
   function:            collect####sample author:              Luke Tunmer
   creation date:       23-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   These two functions pick out of buff the samples for the current component
   and insert them into the block of 64 int16's (after level shifting).
   The collect_end_sample_block routine is used if there are not enough rows
   or columns to fill the block. The routine duplicates the last rows/cols
   as required.
   Each value in the 8x8 sample is an average of a rectangular group of
   pixels h_skip wide and v_skip deep.
---------------------------------------------------------------------------- */
static void collect_simple_sample_block( register DCTSTATE *dct,
                                        register uint8 *buff,
                                        register int16 block[8][8],
                                        COMPONENTINFO *ci )
{
  register int32 i , j , h , v ;
  register uint8 *p , *src_ptr ;
  register int32 val ;
  register int32 v_s , h_s , b , s , s2 , c ;

  v_s = ci->v_skip ;
  h_s = ci->h_skip ;
  s = ci->sample_size ;
  s2 = ci->sample_size2 ;
  b = dct->bytes_in_scanline ;
  c = dct->colors ;

  if ( v_s == 1 && h_s == 1 ) {
    /* optimized case for general loop below */
    for ( j = 0 ; j < 8 ; j++ ) {
      src_ptr = buff ;
      for ( i = 0 ; i < 8 ; i++ ) {
        /* average out the values in the sample size */
        val = *src_ptr ;
        block[i][j] = (int16)
          ((( val + s2) / s ) - 128 ) ;
        src_ptr += c ;
      }
      buff += b ;
    }
  } else {
    for ( j = 0 ; j < 8 ; j++ ) {
      src_ptr = buff ;
      for ( i = 0 ; i < 8 ; i++ ) {
        /* average out the values in the sample size */
        val = 0 ;
        for ( v = 0 ; v < v_s ; v++ ) {
          p = src_ptr + v * b ;
          for ( h = 0 ; h < h_s ; h++ ) {
            val += *p ;
            p += c ;
          }
        }
        block[i][j] = (int16)
          ((( val + s2) / s ) - 128 ) ;
        src_ptr += c * h_s ;
      }
      buff += b * v_s ;
    }
  }
}


static void collect_end_sample_block( register DCTSTATE *dct,
                                     register uint8 *buff,
                                     register int16 block[8][8],
                                     int32 rows_maxi,
                                     int32 cols_maxi,
                                     COMPONENTINFO *ci )
{
  register int32 i , j , h , v , r , c ;

  for ( j = 0 ; j < 8 ; j++ ) {
    for ( i = 0 ; i < 8 ; i++ ) {
      register int32 val = 0 ;

      /* average out the values in the sample size */
      for ( v = 0 ; v < (int32)ci->v_skip ; v++ ) {
        r = ( j + v ) * (int32)ci->v_skip ;
        for ( h = 0 ; h < (int32)ci->h_skip ; h++ ) {
          register uint8 *p = buff ;

          c = ( i + h ) * (int32)ci->h_skip ;
          if ( c > cols_maxi )
            p += cols_maxi * dct->colors ;
          else
            p += c * dct->colors ;

          if ( r > rows_maxi )
            p += rows_maxi * dct->bytes_in_scanline ;
          else
            p += r * dct->bytes_in_scanline ;

          val += *p ;
        }
      }
      block[i][j] = (int16)
        ((( val + (int32)ci->sample_size2) / (int32)ci->sample_size ) - 128 ) ;
    }
  }
}

/*
 *
 *
 * This implementation is based on Appendix A.2 of the book
 * "Discrete Cosine Transform---Algorithms, Advantages, Applications"
 * by K.R. Rao and P. Yip  (Academic Press, Inc, London, 1990).
 * It uses scaled fixed-point arithmetic instead of floating point.
 *
 * Most of the numbers (after multiplication by the constants) are
 * (logically) shifted left by LG2_DCT_SCALE. This is undone by UNFIXH
 * before assignment to the output array. Note that we want an additional
 * division by 2 on the output (required by the equations).
 *
 * If right shifts are unsigned, then there is a potential problem.
 * However, shifting right by 16 and then assigning to a short
 * (assuming short = 16 bits) will keep the sign right!!
 *
 * For other shifts,
 *
 *     ((x + (1 << 30)) >> shft) - (1 << (30 - shft))
 *
 * gives a nice right shift with sign (assuming no overflow). However, all the
 * scaling is such that this isn't a problem. (Is this true?)
 */


/*
 * Perform the forward DCT on one block of samples.
 *
 */

static void forward_dct( int16 block[8][8] )
{
  /* the middle parts of these two loops are the same - just the unpacking
   * and packing is different. Unrolled for speed.
   */
  int32 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  int32 tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, tmp16, tmp17;
  int32 tmp25, tmp26;
  int32 in0, in1, in2, in3, in4, in5, in6, in7;
  int16 *in ;
  int32 i ;

  for (i = 0; i < 8; i++) {
    /* perform one dimensional FDCT on rows */
    /* fast_dct_8( block[ i ] , 1); */
    in = block[i] ;
    in0 = *in++;
    in1 = *in++;
    in2 = *in++;
    in3 = *in++;
    in4 = *in++;
    in5 = *in++;
    in6 = *in++;
    in7 = *in++;

    tmp0 = in7 + in0;
    tmp1 = in6 + in1;
    tmp2 = in5 + in2;
    tmp3 = in4 + in3;
    tmp4 = in3 - in4;
    tmp5 = in2 - in5;
    tmp6 = in1 - in6;
    tmp7 = in0 - in7;

    tmp10 = tmp3 + tmp0 ;
    tmp11 = tmp2 + tmp1 ;
    tmp12 = tmp1 - tmp2 ;
    tmp13 = tmp0 - tmp3 ;

    /* Now using tmp10, tmp11, tmp12, tmp13 */
    in = block[i] ;
    in[0] = (int16) UNFIXH((tmp10 + tmp11) * SIN_1_4);
    in[4] = (int16) UNFIXH((tmp10 - tmp11) * COS_1_4);

    in[2] = (int16) UNFIXH(tmp13*COS_1_8 + tmp12*SIN_1_8);
    in[6] = (int16) UNFIXH(tmp13*SIN_1_8 - tmp12*COS_1_8);

    tmp16 = UNFIXO((tmp6 + tmp5) * SIN_1_4);
    tmp15 = UNFIXO((tmp6 - tmp5) * COS_1_4);

    /* Now using tmp10, tmp11, tmp13, tmp14, tmp15, tmp16 */

    tmp14 = OVERSH(tmp4) + tmp15;
    tmp25 = OVERSH(tmp4) - tmp15;
    tmp26 = OVERSH(tmp7) - tmp16;
    tmp17 = OVERSH(tmp7) + tmp16;

    /* These are now overscaled by OVERSCALE */

    /* tmp10, tmp11, tmp12, tmp13, tmp14, tmp25, tmp26, tmp17 */

    in[1] = (int16) UNFIXH(tmp17*OCOS_1_16 + tmp14*OSIN_1_16);
    in[7] = (int16) UNFIXH(tmp17*OCOS_7_16 - tmp14*OSIN_7_16);
    in[5] = (int16) UNFIXH(tmp26*OCOS_5_16 + tmp25*OSIN_5_16);
    in[3] = (int16) UNFIXH(tmp26*OCOS_3_16 - tmp25*OSIN_3_16);
  }

  for ( i = 0 ; i < 8 ; i++ ) {
    /* perform one dimensional FDCT on columns */
    /*     fast_dct_8( &block[0][ i ] , 8); */

    in = &block[0][i] ;
    in0 = in[0] ;
    in1 = in[1*8] ;
    in2 = in[2*8] ;
    in3 = in[3*8] ;
    in4 = in[4*8] ;
    in5 = in[5*8] ;
    in6 = in[6*8] ;
    in7 = in[7*8] ;

    tmp0 = in7 + in0;
    tmp1 = in6 + in1;
    tmp2 = in5 + in2;
    tmp3 = in4 + in3;
    tmp4 = in3 - in4;
    tmp5 = in2 - in5;
    tmp6 = in1 - in6;
    tmp7 = in0 - in7;

    tmp10 = tmp3 + tmp0 ;
    tmp11 = tmp2 + tmp1 ;
    tmp12 = tmp1 - tmp2 ;
    tmp13 = tmp0 - tmp3 ;

    tmp16 = UNFIXO((tmp6 + tmp5) * SIN_1_4);
    tmp15 = UNFIXO((tmp6 - tmp5) * COS_1_4);

    /* Now using tmp10, tmp11, tmp12, tmp13 */
    in[0  ] = (int16) UNFIXH((tmp10 + tmp11) * SIN_1_4);
    in[4*8] = (int16) UNFIXH((tmp10 - tmp11) * COS_1_4);

    in[2*8] = (int16) UNFIXH(tmp13*COS_1_8 + tmp12*SIN_1_8);
    in[6*8] = (int16) UNFIXH(tmp13*SIN_1_8 - tmp12*COS_1_8);

    /* Now using tmp10, tmp11, tmp13, tmp14, tmp15, tmp16 */

    tmp14 = OVERSH(tmp4) + tmp15;
    tmp25 = OVERSH(tmp4) - tmp15;
    tmp26 = OVERSH(tmp7) - tmp16;
    tmp17 = OVERSH(tmp7) + tmp16;

    /* These are now overscaled by OVERSCALE */

    /* tmp10, tmp11, tmp12, tmp13, tmp14, tmp25, tmp26, tmp17 */

    in[1*8] = (int16) UNFIXH(tmp17*OCOS_1_16 + tmp14*OSIN_1_16);
    in[7*8] = (int16) UNFIXH(tmp17*OCOS_7_16 - tmp14*OSIN_7_16);
    in[5*8] = (int16) UNFIXH(tmp26*OCOS_5_16 + tmp25*OSIN_5_16);
    in[3*8] = (int16) UNFIXH(tmp26*OCOS_3_16 - tmp25*OSIN_3_16);
  }
}


/* ----------------------------------------------------------------------------
   function:            quantize          author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Run the quantise algorithm on each number from the 8x8 int16's input block,
   placing the result into the zz array in zigzag order.
---------------------------------------------------------------------------- */
static void quantize( int16 block[8][8], int32 zz[64], uint32 *qtable )
{
  register int32 u , v ;
  register int32 C , Q , F ;
  register int32 index ;

  for ( u = 0 ; u < 8 ; u++ ) {
    for ( v = 0 ; v < 8 ; v++ ) {
      index = zig_zag[u][v] ;
      F = block[u][v] ;
      Q = qtable[index] ;
      if ( F < 0 )
        C = (F - (Q >> 1) ) / Q ;
      else
        C = (F + (Q >> 1) ) / Q ;
      zz[index] =  C ;
    }
  }
}



/* ----------------------------------------------------------------------------
   function:            encode_dc_huffman author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Encode the zz[0] number of the zigzag array using the DC hufftable and
   the prediction, writing the output to the file flptr.
---------------------------------------------------------------------------- */
static Bool encode_dc_huffman(register DCTSTATE *dct,
                              int32 zz[64],
                              HUFFTABLE *hufftable,
                              int32 *prediction,
                              FILELIST *filter )
{
  int32 diff , d , cat ;

  d = diff = zz[0] - *prediction ;
  *prediction = zz[0] ;

  if ( d < 0 ) {
    d = -d ;
    diff-- ;
  }

  cat = 0 ;
  while ( d ) {
    d >>= 1 ;
    cat++ ;
  }

  if (! output_bits( dct->currinfo , hufftable->codes[cat] ,
                    hufftable->code_lengths[cat] , filter ))
    return FALSE ;

  if ( cat )
    if (! output_bits( dct->currinfo , diff , cat , filter ))
      return FALSE ;

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            encode_ac_huffman author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Encode the numbers zz[1] to zz[63] using the AC hufftable, writing the
   the bits to the file flptr.
---------------------------------------------------------------------------- */
static Bool encode_ac_huffman(register DCTSTATE *dct,
                              int32 zz[64],
                              HUFFTABLE *hufftable,
                              FILELIST *filter )
{
  int32 ssss ;
  int32 C ;
  int32 K = 0 , R = 0 ;
  int32 d , I ;

  for (;;) {
    K++ ;
    if (( d = zz[ K ]) == 0 ) {
      if ( K == 63 ) {
        if (! output_bits( dct->currinfo , hufftable->codes[0] ,
                          hufftable->code_lengths[0] , filter ))
          return FALSE ;
        break ;
      } else
        R++ ;
    } else {
      while ( R > 15 ) {
        if (! output_bits( dct->currinfo , hufftable->codes[240] ,
                          hufftable->code_lengths[240] , filter ))
          return FALSE ;
        R -= 16 ;
      }
      C = d ;
      if ( d < 0 ) {
        d = -d ;
        C-- ;
      }

      ssss = 0 ;
      while ( d ) {
        d >>= 1 ;
        ssss++ ;
      }
      I = ( R << 4 ) | ssss ;
      if (! output_bits( dct->currinfo , hufftable->codes[I] ,
                        hufftable->code_lengths[I] , filter ))
        return FALSE ;
      if ( ! output_bits( dct->currinfo , C , ssss , filter ))
        return FALSE ;
      R = 0 ;
      if ( K == 63 )
        break ;
    }
  }
  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            encode_simple_MDU author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Call the appropriate routines to sample each component in the MDU, and
   then pass the result through the FDCT, the quantization and the Huffman
   encoding respectively.
   The MDU is in the middle of the image, so no checks need to be made for
   MDUs overrunning the right edge or the bottom of the image.
---------------------------------------------------------------------------- */
static Bool encode_simple_MDU(register DCTSTATE *dct,
                              uint8 *buffer,
                              FILELIST *filter )
{
  COMPONENTINFO *ci ;
  int16  dct_block[8][8] ;
  int32  zz[64] ;
  uint8 *sample_buffer ;
  uint8 *row_buffer ;
  int32 hsamples , vsamples ;
  int32 compind , v , h ;
  uint32 * dcs, * acs;

  dcs = dct->currinfo->dc_huff_number;
  acs = dct->currinfo->ac_huff_number;

  for ( compind = 0 ; compind < dct->colors ; compind++ ) {
    ci = &dct->components[compind] ;
    hsamples = ci->num_hsamples ;
    vsamples = ci->num_vsamples ;

    row_buffer = buffer ;

    /* loop through the vertical blocks in the MDU for current component */
    for ( v = 0 ; v < vsamples ; v++ ) {

      sample_buffer = row_buffer ;
      /* loop through horizontal blocks in the MDU for current component */
      for ( h = 0 ; h < hsamples ; h++ ) {

        collect_simple_sample_block( dct , sample_buffer , dct_block, ci ) ;

        forward_dct( dct_block ) ;

        quantize(dct_block, zz, dct->quanttables[(int32)ci->qtable_number]);

        if (! encode_dc_huffman( dct , zz ,
                                &dct->dc_huff.tables[(int32) dcs[compind] ],
                                &ci->dc_prediction ,
                                filter ))
          return FALSE ;

        if (! encode_ac_huffman( dct , zz ,
                                &dct->ac_huff.tables[(int32) acs[compind]],
                                filter ))
          return FALSE ;

        sample_buffer += 8 * dct->colors * (int32)ci->h_skip ;
      }

      row_buffer += 8 * dct->bytes_in_scanline * (int32)ci->v_skip ;
    }

    buffer++ ; /* move onto next component */
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            encode_incomplete_MDU  author:              Luke Tunmer
   creation date:       06-Sep-1991            last modification:   ##-###-####
   arguments:
   description:

   Call the appropriate routines to sample each component in the MDU, and
   then pass the result through the FDCT, the quantization and the Huffman
   encoding respectively.
   The MDU is at the right edge or the bottom of the image, so checks need
   to be made in order to duplicate the last row/column.
---------------------------------------------------------------------------- */
static Bool encode_incomplete_MDU(register DCTSTATE *dct,
                                  uint8 *buffer,
                                  int32 rows,    /* rows in incomplete MDU */
                                  int32 cols,    /* cols in incomplete MDU */
                                  FILELIST *filter )
{
  int16 dct_block[8][8] ;
  int32 zz[64] ;
  int32 compind ;

  if ( (int32)dct->rows_in_MDU < rows )
    rows = dct->rows_in_MDU ;
  if ( (int32)dct->cols_in_MDU < cols )
    cols = dct->cols_in_MDU ;

  HQASSERT(rows > 0 && cols > 0,
           "Rows or cols zero in encode_incomplete_MDU") ;

  for ( compind = 0 ; compind < dct->colors ; compind++ ) {
    COMPONENTINFO *ci = &dct->components[compind] ;
    int32 hsamples = ci->num_hsamples ;
    int32 vsamples = ci->num_vsamples ;
    int32 rows_reqd = 8 * (int32)ci->v_skip ;
    int32 cols_reqd = 8 * (int32)ci->h_skip ;
    int32 rows_last = rows - 1 ;        /* last index into row */
    uint8 *row_buffer = buffer ;
    int32 v ;
    uint32 dc_huff_number = dct->currinfo->dc_huff_number[compind];
    uint32 ac_huff_number = dct->currinfo->ac_huff_number[compind];

    /* loop through the vertical blocks in the MDU for current component */
    for ( v = 0 ; v < vsamples ; v++ ) {
      uint8 *sample_buffer = row_buffer ;
      int32 cols_last = cols - 1 ;      /* last index into col */
      int32 h ;

      /* loop through horizontal blocks in the MDU for current component */
      for ( h = 0 ; h < hsamples ; h++ ) {

        if ( cols_last + 1 < cols_reqd || rows_last + 1 < rows_reqd ) {
          collect_end_sample_block( dct , sample_buffer , dct_block ,
                                    rows_last , cols_last , ci ) ;

        } else {
          collect_simple_sample_block( dct , sample_buffer , dct_block , ci ) ;
        }
        forward_dct( dct_block ) ;

        quantize(dct_block, zz, dct->quanttables[(int32)ci->qtable_number]);

        if (! encode_dc_huffman( dct , zz ,
                                 &dct->dc_huff.tables[(int32)dc_huff_number],
                                 &ci->dc_prediction,
                                 filter ))
          return FALSE ;

        if (! encode_ac_huffman( dct , zz ,
                                 &dct->ac_huff.tables[(int32)ac_huff_number],
                                 filter ))
          return FALSE ;

        if ( cols_last >= cols_reqd ) {
          sample_buffer += dct->colors * cols_reqd ;
          cols_last -= cols_reqd ;
        } else {
          sample_buffer += dct->colors * cols_last ;
          cols_last = 0 ;
        }
      }

      if ( rows_last >= rows_reqd ) {
        row_buffer += dct->bytes_in_scanline * rows_reqd ;
        rows_last -= rows_reqd ;
      } else {
        row_buffer += dct->bytes_in_scanline * rows_last ;
        rows_last = 0 ;
      }
    }

    buffer++ ; /* move onto next component */
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            encode_scan       author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   This routine is called to process the encoding filter's buffer. It loops
   through the data outputting as many MDUs (minimum data units) as there
   are in the buffer. The order of sampling within an MDU is defined to be:

   for each component
       sample (horizontal samples) * (vertical samples)
          for each sample
             quantize, huffman encode, and output to underlying file.

   An MDU can be incomplete if it's the last one in each row or the last row.
   An incomplete MDU pads the missing numbers with the last row/column.
---------------------------------------------------------------------------- */
Bool encode_scan(FILELIST *filter, register DCTSTATE *dct)
{
  uint8    *MDU_start ;
  int32    col ;
  int32    mdu ;
  int32    MDUs_in_scan ;

  MDU_start = theIBuffer( filter ) ;

  if ( dct->dct_status == AT_START_OF_IMAGE ) {
    /* first timeinto the scan - therefore init the encoder */
    dct->current_row = 0 ;
    dct->currinfo->bbuf.nbits = 0 ;
    dct->currinfo->bbuf.data = 0 ;
    /* initialise the DC prediction for each component */
    dct->components[0].dc_prediction = 0 ;
    dct->components[1].dc_prediction = 0 ;
    dct->components[2].dc_prediction = 0 ;
    dct->components[3].dc_prediction = 0 ;
  }

  if ( dct->colortransform ) {
    if ( dct->colors == 3 )
      convert_RGB_to_YUV( theIBuffer( filter ) , theICount( filter )) ;
    else if ( dct->colors == 4 )
      convert_CMYK_to_YUVK( theIBuffer( filter ) , theICount( filter )) ;
  }

  /* no of complete MDUs */
  MDUs_in_scan = dct->columns / (int32)dct->cols_in_MDU ;
  col = 0 ;

  if ( (int32)dct->rows - (int32)dct->current_row < (int32)dct->rows_in_MDU ) {
    /* handle the last row in the image which has incomplete MDUs */
    for ( mdu = 0 ; mdu < MDUs_in_scan ; mdu++ ) {

      if ( ! encode_incomplete_MDU( dct , MDU_start ,
                                   dct->rows - (int32) dct->current_row ,
                                   dct->columns - col , filter ))
        return FALSE ;

      MDU_start += (int32) dct->cols_in_MDU * dct->colors ;
      col += dct->cols_in_MDU ;
    }
  } else {
    /* handle the complete MDUs in the scanline */
    for ( mdu = 0 ; mdu < MDUs_in_scan ; mdu++ ) {

      if ( ! encode_simple_MDU( dct , MDU_start , filter ))
        return FALSE ;

      MDU_start += (int32) dct->cols_in_MDU * dct->colors ;
      col += dct->cols_in_MDU ;
    }
  }

  /* handle the last incomplete MDU in the scanline , if there is one */
  if ( col < dct->columns ) {
    if ( ! encode_incomplete_MDU( dct , MDU_start ,
                                  dct->rows - (int32) dct->current_row ,
                                  dct->columns - col , filter ))
      return FALSE ;
  }
  dct->current_row = (uint16)(dct->current_row + dct->rows_in_MDU) ;
  if ( (int32)dct->current_row >= (int32)dct->rows ) {
    /* come to the end of the image - write out any remaining bits */
    if ( dct->currinfo->bbuf.nbits ) {
      /* stuff 1's into remaining bits */
      if ( !output_bits(dct->currinfo, 0xff,
            BB_NBITS - dct->currinfo->bbuf.nbits, filter) )
        return FALSE ;
    }
    theICount( filter ) = 0 ;
    theIPtr( filter ) = theIBuffer( filter ) ;
    /* mark filter so that no more writes can happen to it */
    dct->dct_status = DONE_ALL_DATA ;

    return TRUE ;
  }
  dct->dct_status = OUTPUT_SCAN ;

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE ;
}



/**
 * Write out nbits of the code to the flptr. The BITBUFFER struct holds bits
 * from the previous call, and any bits left over from this one. It returns
 * FALSE if the underlying file returns an ioerror on a Putc. If a 0xFF is
 * output, it must be followed by a 0x00 byte.
 */
static Bool output_bits(register scaninfo *info,
                        register int32 code,
                        register int32 nbits,
                        FILELIST *filter )
{
  register int32 bb_nbits = info->bbuf.nbits;
  register uint32 bb_data = info->bbuf.data;
  FILELIST *flptr = theIUnderFile(filter);

  /* mask out high order bits */
  code &= ~( (uint32)-1 <<  nbits ) ;
  if ( (BB_NBITS - bb_nbits ) >= nbits ) {
    bb_nbits += nbits ;
    bb_data |= code << ( BB_NBITS - bb_nbits ) ;
    if ( bb_nbits == BB_NBITS ) {
      if ( Putc( bb_data , flptr ) == EOF )
        return error_handler( IOERROR ) ;
      if ( (int32) bb_data == 0xff )
        if ( Putc( 0x00 , flptr ) == EOF )
          return error_handler( IOERROR ) ;
      bb_data = 0 ;
      bb_nbits = 0 ;
    }
  } else {
    nbits -= BB_NBITS - bb_nbits ;
    bb_data |= code >> nbits ;
    if ( Putc( bb_data , flptr ) == EOF )
      return error_handler( IOERROR ) ;
    if ( (int32) bb_data == 0xff )
      if ( Putc( 0x00 , flptr ) == EOF )
        return error_handler( IOERROR ) ;
    bb_data = 0 ;
    bb_nbits = 0 ;
    if ( nbits >= BB_NBITS ) {
      nbits -= BB_NBITS ;
      bb_data = (uint8) (code >> nbits) ;
      if ( Putc( bb_data , flptr ) == EOF )
        return error_handler( IOERROR ) ;
      if ( (int32) bb_data == 0xff )
        if ( Putc( 0x00 , flptr ) == EOF )
          return error_handler( IOERROR ) ;
      bb_data = 0 ;
      bb_nbits = 0 ;
    }
    if ( nbits ) {
      bb_data = (uint8) (code << ( BB_NBITS - nbits )) ;
      bb_nbits = nbits ;
    }
  }

  info->bbuf.nbits = bb_nbits ;
  info->bbuf.data = bb_data ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------

   Routines for Decode DCT

---------------------------------------------------------------------------- */


/*
 * Get an unsigned 16 bit number from the input stream
 */
int32 dct_get_16bit_num( register FILELIST *flptr )
{
  register int32 c1 , c2 ;

  if ((( c1 = Getc( flptr )) == EOF ) ||
      (( c2 = Getc( flptr )) == EOF ))
    return -1 ;
  return ( c1 << 8 ) + c2 ;
}


/* ----------------------------------------------------------------------------
   function:            get_marker_code   author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   This routine finds the next 0xff byte and the following marker byte from
   the underlying file.

   "All markers shall be assigned two-byte codes: a 'FF' byte followed by a
   second byte which is not equal to 0 or 'FF'." - the spec
---------------------------------------------------------------------------- */
Bool get_marker_code(int32 *pcode, register FILELIST *flptr)
{
  register int32 c ;

  do{
    do {
      if (( c = Getc( flptr )) == EOF )
        return FALSE ;
    } while (c != 0xff);
    do {
      if (( c = Getc( flptr )) == EOF )
        return FALSE ;
    } while ( c == 0xff ) ;
  } while ((c == 0x00) || (c == 0xff));
  *pcode = c ;
  return TRUE ;
}


/*
 * Find the next component with a particular id number
 */
static int32 get_component( int32 id, int32 start, DCTSTATE *dctstate )
{
  int32 c ;

  for ( c = start ; c < 4 ; c++ ) {
    if ( (int32) (dctstate->components[c].id_num) == id )
      return c ;
  }
  return -1 ;
}

/*
 * Check second byte of byte stuffed pair for actually being an
 * end-of-image (EOI) marker.
 */
static DCT_RESULT check_eoi(int32 ch)
{
  return (ch == EOI) ? DCT_EOI : DCT_ERR;
}

/**
 * This routine extracts the next nbits from the underlying file and returns
 * the result though ret_bits.
 */
static DCT_RESULT get_bits(register scaninfo *info, FILELIST *flptr,
                           int32 nbits, int32 *ret_bits)
{
  int32 bits, ch;
  register int32 bb_nbits = info->bbuf.nbits;
  register int32 bb_data = info->bbuf.data;

  bits = ( bb_data & ( BB_MASK >> ( BB_NBITS - bb_nbits ))) <<
    ( nbits - bb_nbits);
  nbits -= bb_nbits;
  if (( ch = Getc( flptr )) == EOF )
    return DCT_ERR;
  if ( ch == BSTUFF1 )
  {
    if ((( ch = Getc( flptr )) == EOF ) || ( ch != BSTUFF2 ))
      return check_eoi(ch);
    ch = BSTUFF1;
  }
  bb_data = ch;
  bb_nbits = BB_NBITS;
  if ( bb_nbits < nbits )
  {
    bits |= ( bb_data << ( nbits - bb_nbits ));
    nbits -= bb_nbits;
    if (( ch = Getc( flptr )) == EOF )
      return DCT_ERR;
    if ( ch == BSTUFF1 )
    {
      if ((( ch = Getc( flptr )) == EOF ) || ( ch != BSTUFF2 ))
        return check_eoi(ch);
      ch = BSTUFF1;
    }
    bb_data = ch;
    bb_nbits = BB_NBITS - nbits;
    bits |= ( bb_data >> bb_nbits );
  }
  else
  {
    bb_nbits -= nbits;
    bits |= ( bb_data >> bb_nbits );
  }
  *ret_bits = bits;
  info->bbuf.nbits = bb_nbits;
  info->bbuf.data  = (uint8)bb_data;
  return DCT_OK;
}

/**
 * A macro wrapper around the function get_bits to try and
 * avoid the function call overhead in the case of just need a few
 * bits from our bit buffer.
 * Note : Macro includes a return which can cause surprising code flow,
 * so indicate this in the macro name.
 */
#define GET_BITS_MAY_RETURN(_info, _flptr, _nbits, _ret_bits) MACRO_START \
  if ( (_nbits) <= (_info)->bbuf.nbits )                                  \
  {                                                                       \
    (_info)->bbuf.nbits -= (_nbits);                                      \
    (_ret_bits) = ((_info)->bbuf.data >> (_info)->bbuf.nbits) &           \
                  (0xff >> ( BB_NBITS - (_nbits)));                       \
  }                                                                       \
  else                                                                    \
  {                                                                       \
    DCT_RESULT _err_;                                                     \
    if ( (_err_ = get_bits((_info), (_flptr), (_nbits), &(_ret_bits))) < 0 ) \
      return _err_;                                                       \
  }                                                                       \
MACRO_END

/**
 * This routine determines the next code from the underlying file. There are
 * several possibilities. To speed thing up, try what is in the bit buffer,
 * if not a valid code, then extract 8 bits.  If not a valid code,
 * then extract the rest one bit at a time until a valid bit-pattern is built
 * up.  The corresponding Huffman code is returned through ret_code.
 */
static DCT_RESULT bb_get_next_code(register scaninfo *info, FILELIST *flptr,
                                   int32 *ret_code, HUFFTABLE *huff)
{
  int32 code;
  register int32 value,  ch, length;
  register int32 *maxcode   = huff->maxcode;
  register int32 *hashtable =  huff->hashtable;
  register int32 bb_nbits = info->bbuf.nbits;
  register int32 bb_data = info->bbuf.data;


  if ( bb_nbits )
  {
    /* Try what is in buffer first, but  only if there is something there!
     * Zero pad on right to BB_NBITS bits
     */
    code = (bb_data << (BB_NBITS - bb_nbits)) & BB_MASK;
    value = hashtable[code];
    if ( value >=0 )
    {
      length = (int32) (value >> 16);
      /* two possibilties
         length <= bb_nbits ==> valid code
         length > bb_nbits  ==> code is longer */
      if (length <= bb_nbits)
      {
        info->bbuf.nbits -= length;
        *ret_code = (int32) (value & 0xFFFF);
        return DCT_OK;
      }
    }
  }
  /* if we get here, then code is longer than the buffer */
  /* so ask for 8 bits, and no end problems should arise */

  GET_BITS_MAY_RETURN(info, flptr, 8, code);

  value = hashtable[code];

  if ( value == INVALID_CODE )  /* invalid huffman code */
    return DCT_ERR;

  else if ( value == LONG_CODE )
  {
    /* code length > 8 */
    /* use code from the original function */

    bb_nbits = info->bbuf.nbits;
    bb_data = info->bbuf.data;
    length = 7;  /* one less than number of bits collected so far */

    do
    {
      if ( bb_nbits == 0 )
      {
        if (( ch = Getc( flptr )) == EOF )
          return DCT_ERR;
        if ( ch == BSTUFF1 )
        {
          if (( ch = Getc( flptr )) == EOF )
          {
            info->bbuf.data = 0;
            return DCT_ERR;
          }
          if ( ch == BSTUFF2 )
            ch = BSTUFF1;
          else
          {
            info->bbuf.data = ch;
            return check_eoi(ch);
          }
        }
        bb_data = ch;
        bb_nbits = BB_NBITS;
      }
      bb_nbits--;
      length++;
      code = ( code << 1 ) | (( bb_data >> bb_nbits ) & 0x0001);
    } while ( code > maxcode[length] );

    if ( length > 15 )
      return DCT_ERR;

    ch = (int32) (huff->valptr[ length ]) + code
      - (int32) (huff->mincode[ length ]);
    *ret_code = huff->huffval[ch];

    info->bbuf.nbits = bb_nbits;
    info->bbuf.data =  bb_data;
    return DCT_OK;
  }
  else
  {
    /* valid code, length is <= 8 */
    /*  Put back unused part of code */

    length = (int32) (value >> 16);

    info->bbuf.nbits += 8 - length;
    HQASSERT(info->bbuf.nbits <= 8,"Invalid DCT code");

    *ret_code = (int32) (value & 0xFFFF);
    return DCT_OK;
  }
}

/**
 * A macro wrapper around the function bb_get_next_code() to try and
 * avoid the function call overhead in the case of just need a few
 * bits from our partial buffer.
 */
#define BB_CODE_MAY_RETURN(_info, _flptr, _ret_code, _huff) MACRO_START  \
  DCT_RESULT _err_;                                                      \
  if ( (_info)->bbuf.nbits )                                             \
  {                                                                      \
    /* Try partial bits first, if present. Zero pad to BB_NBITS bits */  \
    int32 _v_ = (_huff)->hashtable[((_info)->bbuf.data <<                \
                          (BB_NBITS-(_info)->bbuf.nbits)) & 0xFF];       \
    if ( _v_ >= 0  && (int32)(_v_ >> 16) <= (_info)->bbuf.nbits )        \
    {                                                                    \
        (_info)->bbuf.nbits -= (int32)(_v_ >> 16);                       \
        (_ret_code) = (int32)(_v_ & 0xFFFF);                             \
    }                                                                    \
    else if ( (_err_ = bb_get_next_code(_info, _flptr, &(_ret_code), _huff)) < 0 ) \
      return _err_;                                                      \
  }                                                                      \
  else if ( (_err_ = bb_get_next_code(_info, _flptr, &(_ret_code), _huff)) < 0 )   \
    return _err_;                                                        \
MACRO_END

/* ----------------------------------------------------------------------------
   function:            init_YUV_to_RGB_tables  author:            Luke Tunmer
   creation date:       13-Aug-1992             last modification: ##-###-####
   arguments:
   description:

   Init the static tables for YUV->RGB conversions. This algorithm
   comes from the PD JPEG code. Nifty because it removes all multiplications
   from the inner loop.
---------------------------------------------------------------------------- */
Bool init_YUV_to_RGB_tables( void )
{
  int32 i, x ;

  if ( inited_YUV_to_RGB_tables )
    return TRUE;

  inited_YUV_to_RGB_tables = TRUE ;

  /* shift u v table_starts by TAB_MARGIN to allow neg margin */
  v_r_tab = V_R_tab + TAB_MARGIN;
  u_b_tab = U_B_tab + TAB_MARGIN;
  v_g_tab = V_G_tab + TAB_MARGIN;
  u_g_tab = U_G_tab + TAB_MARGIN;

  /* add 128 to v_r and u_b tables
   * add 64 to  u_g and v_g tables
   * then no need to shift  y by 128 */

  for (i = 0; i < 256; i++) {
    /* i is the actual input pixel value, in the range 0..255 */
    /* The U or V value we are thinking of is x = i - 255/2 */
    x = i - 128;
    /* V=>R value is nearest int to 1.40200 * x */

    v_r_tab[i] = BIT_SHIFT32_SIGNED_RIGHT_EXPR(FIX(1.40200) * x + ONE_HALF,
                                               16) + 128;

    /* U=>B value is nearest int to 1.77200 * x */
    u_b_tab[i] = BIT_SHIFT32_SIGNED_RIGHT_EXPR(FIX(1.77200) * x + ONE_HALF,
                                               16) + 128;

    /* V=>G value is scaled-up -0.71414 * x */
    v_g_tab[i] = (- FIX(0.71414)) * x + FIX (64);
    /* U=>G value is scaled-up -0.34414 * x */
    /* We also add in ONE_HALF so that need not do it in inner loop */
    u_g_tab[i] = (- FIX(0.34414)) * x + ONE_HALF + FIX(64);
  }

  /* now set up margins on both ends */
  for (i=1; i <= TAB_MARGIN; i++) {
    v_r_tab[-i] = v_r_tab[0];
    v_r_tab[i+255] = v_r_tab[255];

    u_b_tab[-i] = u_b_tab[0];
    u_b_tab[i+255] = u_b_tab[255];

    v_g_tab[-i] = v_g_tab[0];
    v_g_tab[i+255] = v_g_tab[255];

    u_g_tab[-i] = u_g_tab[0];
    u_g_tab[i+255] = u_g_tab[255];
  }

  /* Now shift start as u v are no longer centered */
  v_r_tab += 128;
  u_b_tab += 128;
  v_g_tab += 128;
  u_g_tab += 128;

  return TRUE ;
}



/* one method of sign-extending a huffman code. I assume this
 * one is faster on most platforms.
 */
#define huff_EXTEND(x,s) if (!(x & extend_test[s])) x += extend_offset[s];

static int32 extend_test[16] = {  /* entry n is 2**(n-1) */
  0, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
  0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000
};

static int32 extend_offset[16] = { /* entry n is (-1 << n) + 1 */
  0, ((-1)<<1) + 1, ((-1)<<2) + 1, ((-1)<<3) + 1, ((-1)<<4) + 1,
  ((-1)<<5) + 1, ((-1)<<6) + 1, ((-1)<<7) + 1, ((-1)<<8) + 1,
  ((-1)<<9) + 1, ((-1)<<10) + 1, ((-1)<<11) + 1, ((-1)<<12) + 1,
  ((-1)<<13) + 1, ((-1)<<14) + 1, ((-1)<<15) + 1
};

static void (*splat_array[64])(register int32 value) =
{
  splat_00,
  splat_01, splat_10,
  splat_20, splat_11, splat_02,
  splat_03, splat_12, splat_21, splat_30,
  splat_40, splat_31, splat_22, splat_13, splat_04,
  splat_05, splat_14, splat_23, splat_32, splat_41, splat_50,
  splat_60, splat_51, splat_42, splat_33, splat_24, splat_15, splat_06,
  splat_07, splat_16, splat_25, splat_34, splat_43, splat_52, splat_61, splat_70,
  splat_71, splat_62, splat_53, splat_44, splat_35, splat_26, splat_17,
  splat_27, splat_36, splat_45, splat_54, splat_63, splat_72,
  splat_73, splat_64, splat_55, splat_46, splat_37,
  splat_47, splat_56, splat_65, splat_74,
  splat_75, splat_66, splat_57,
  splat_67, splat_76,
  splat_77,
};

static Bool decode_progressive(DCTSTATE *dct, FILELIST *flptr)
{
  scaninfo * pinfo;
  for (pinfo = dct->currinfo;pinfo;pinfo = pinfo->next) {
    if ((pinfo->compbits & (1 << dct->compind)) == 0)
      continue;
    /* move to current position in this scan */
    if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

    if ((*theIMySetFilePos( flptr ))( flptr , &pinfo->SOSo ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

    if ( zigzag_decodes[pinfo->type](dct, flptr, pinfo) < 0 )
      return FALSE;

    if ( (*theIMyFilePos(flptr))(flptr, &pinfo->SOSo) == EOF )
      return (*theIFileLastError(flptr))(flptr);
  }

  if (dct->successive) {
    /* values still need dequantised and IDCTed if using successive
     * approximation
     */
    int32 i;
    register QUANTTABLE qtable = dct->quanttables[(int32)dct->current_ci->qtable_number];

    for (i = 0;i < 64;i++)
      splat_array[i](dct->coeffs[i]*qtable[i]);
  }

  return TRUE;
}


static Bool decode_noninterleaved(DCTSTATE *dct,
                                  FILELIST *flptr,
                                  scaninfo * info )
{
  /* move to current position in this scan */
  if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;

  if ((*theIMySetFilePos( flptr ))( flptr , &info->SOSo ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;

  if ( decode_normal_zigzag(dct,flptr,info) < 0 )
    return FALSE;

  if ( (*theIMyFilePos(flptr))(flptr, &info->SOSo) == EOF )
    return (*theIFileLastError(flptr))(flptr);

  return TRUE;
}

static DCT_RESULT decode_zigzag_band_DC(DCTSTATE *dct,
                                        FILELIST *flptr,
                                        scaninfo * info )
{
  int32 s, r,i;
  /* decode the DC coefficient */
  BB_CODE_MAY_RETURN(info, flptr, s, dct->dc_huff.current);
  r = 0 ;

  if ( s ) {
    GET_BITS_MAY_RETURN(info, flptr, s, r);
    huff_EXTEND( r, s );
  }

  r += dct->current_ci->dc_prediction;

  if (dct->successive) {
    HQASSERT(dct->coeffs,"coefficiets array missing");

    dct->coeffs[ 0 ] = r << info->Al;

    /* zero AC coefficients */
    for (i = 1;i < 64;i++)
      dct->coeffs[i] = 0;

  } else {
    register QUANTTABLE qtable = dct->quanttables[(int32)dct->current_ci->qtable_number];

    if ( r != 0 )
      splat_00((r << info->Al)*qtable[0]);
    else
      zero_tile();
  }

  dct->current_ci->dc_prediction = ( int16 )r;

  return DCT_OK;
}


static DCT_RESULT decode_zigzag_band_AC(DCTSTATE *dct,
                                        FILELIST *flptr,
                                        scaninfo * info )
{
  int32 s , k , r , end;
  HUFFTABLE *ac_huff = &dct->ac_huff.tables[(int32)  info->ac_huff_number[0]];
  register QUANTTABLE qtable = dct->quanttables[(int32)dct->current_ci->qtable_number];

  if (info->EOBrun) {
    info->EOBrun--;
    return DCT_OK;
  }

  k = info->Ss;
  HQASSERT(k,"AC coefficent is DC");
  end = info->Se;

  /* decode AC coefficients */
  for ( ; k <= end ; k++ ) {
    BB_CODE_MAY_RETURN(info, flptr, r, ac_huff);

    s = r & 0x0f ;
    r = r >> 4 ;

    if ( s ) {
      k += r ;

      GET_BITS_MAY_RETURN(info, flptr, s, r);

      if ( k > end ) {
        HQTRACE( debug_dctdecode, ( "too many AC coefficients" )) ;
        break ;
      }

      huff_EXTEND( r , s ) ;

      if (dct->successive) {
        dct->coeffs[k] = r << info->Al;
      } else {
        splat_array[k]((r << info->Al)*qtable[k]);
      }

    } else {
      if ( r != 15 ) {
        info->EOBrun = 1 << r;
        if (r) {
          GET_BITS_MAY_RETURN(info, flptr, r, r);
          info->EOBrun += r;
        }
        info->EOBrun--;
        break ;
      }
      k += 15 ;
    }
  }

  return DCT_OK;
}

/**
 * Read count bytes from flptr.  We make sure that there is this much data
 * available to read.
 */
Bool dct_read_data(FILELIST *flptr, int32 count, int32 len, int32 *readcount,
                   uint8* buff)
{
  int32 bytesread = 0;

  if ( *readcount + count > len )
    return FALSE;

  while ( bytesread < count ) {
    int32 ch ;
    if ((ch = Getc(flptr)) == EOF)
      return FALSE ;
    buff[bytesread++] = CAST_TO_UINT8(ch) ;
    (*readcount)++;
  }
  return TRUE;
}

/**
 * Skip count bytes from flptr.  We make sure that there is this much data
 * available to skip
 */
Bool dct_skip_data(FILELIST *flptr , int32 count , int32 len, int32 *readcount)
{
  int32 bytesread = 0;

  if ( *readcount + count > len )
    return FALSE;

  while ( bytesread < count )
  {
    int32 ch ;
    /* EOF shouldn't be possible as we have already checked there is
     * logically enough data
     */
    if ((ch = Getc(flptr)) == EOF)
      return FALSE;
    (*readcount)++;
    bytesread++;
  }
  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            dct_skip_comment      author:              Luke Tunmer
   creation date:       09-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Skip over the bytes that make up a JPEG comment
---------------------------------------------------------------------------- */
Bool dct_skip_comment( FILELIST *flptr )
{
  int32 len ;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2 ;
  while ( len-- ) {
    if ( Getc( flptr ) == EOF )
      return FALSE ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            decode_APPD       author:              Derek Denyer
   creation date:       02-Oct-2005       last modification:   ##-###-####
   arguments:
   description:

---------------------------------------------------------------------------- */
Bool decode_APPD( FILELIST *flptr, DCTSTATE *dctstate )
{
  int32 slen, len, bytesread = 0;
  uint8 buff[18];
  OBJECT value = OBJECT_NOTVM_NOTHING;
  double xdensity, ydensity;

  /* Get length of segment including length bytes */
  if ((len = dct_get_16bit_num(flptr)) < 0)
    return FALSE;

  len -= 2;

  if (len >= 18) {

    if (!dct_read_data(flptr, 18, len, &bytesread, buff))
      return FALSE;

    /* Is support for 2.5 really necesssary? */
    if (HqMemCmp(buff, 18, NAME_AND_LENGTH("Adobe_Photoshop2.5")) == 0) {
      /* Read 9 bytes & decode*/
      if (!dct_read_data(flptr, 9, len, &bytesread, buff))
        return !isIEof(flptr);
      xdensity = (buff[1] << 8) + buff[2];
      xdensity += ((buff[3] << 8) + buff[4]) / 65536.0;
      ydensity = (buff[5] << 8) + buff[6];
      ydensity += ((buff[7] << 8) + buff[8]) / 65536.0;

      /* Zero x and y densities are illegal according to the JFIF spec, but we
         ignore them and pretend the resolution data did not exist.  Default
         resolution is handled by XPS (96dpi) or HqnImage (180dpi). */
      if (xdensity > 0.0) {
        object_store_real(&value, (float)xdensity);
        if (filter_info_callback(dctstate->match, NAME_XResolution, &value,
                                 &dctstate->match_done))
          return dctstate->match_done;
      }
      if (ydensity > 0.0) {
        object_store_real(&value, (float)ydensity);
        if (filter_info_callback(dctstate->match, NAME_YResolution, &value,
                                 &dctstate->match_done))
          return dctstate->match_done;
      }
    }
    else if (HqMemCmp(buff, 18,
                      NAME_AND_LENGTH("Photoshop 3.0\000\070BIM")) == 0) {
      /* Note \070 is for the benefit of the MS VC compiler that doesn't like an
       * 8 after \000 */
      Bool more_records;
      int32 id;

      do {
        more_records = FALSE;
        /* Read 2 bytes for tag id */
        if (!dct_read_data(flptr, 2, len, &bytesread, buff))
          return !isIEof(flptr);
        id = (buff[0] << 8) + buff[1];
        /* Read one byte (a string length) */
        if (!dct_read_data(flptr, 1, len, &bytesread, buff))
          return !isIEof(flptr);
        slen = buff[0];
        slen += ((slen + 1) % 2);
        if (!dct_skip_data(flptr, slen, len, &bytesread))
          return !isIEof(flptr);
        if (id == 0x3ed) {
          if (!dct_read_data(flptr, 16, len, &bytesread, buff))
            return !isIEof(flptr);
          xdensity = (buff[4] << 8) + buff[5];
          xdensity += ((buff[6] << 8) + buff[7]) / 65536.0;
          ydensity = (buff[12] << 8) + buff[13];
          ydensity += ((buff[14] << 8) + buff[15]) / 65536.0;

          /* Zero x and y densities are illegal according to the JFIF spec,
           * but we ignore them and pretend the resolution data did not exist.
           * Default resolution is handled by XPS (96dpi) or HqnImage (180dpi).
           */
          if (xdensity > 0.0) {
            object_store_real(&value, (float)xdensity);
            if (filter_info_callback(dctstate->match, NAME_XResolution,
                                     &value, &dctstate->match_done))
              return dctstate->match_done;
          }
          if (ydensity > 0.0) {
            object_store_real(&value, (float)ydensity);
            if (filter_info_callback(dctstate->match, NAME_YResolution,
                                     &value, &dctstate->match_done))
              return dctstate->match_done;
          }
          break;
        }
        if (!dct_read_data(flptr, 4, len, &bytesread, buff))
          return !isIEof(flptr);
        slen = (((((buff[0] << 8) + buff[1]) << 8) + buff[2]) << 8) + buff[3];
        slen += (slen % 2);
        if ( !dct_skip_data(flptr, slen, len, &bytesread))
          return !isIEof(flptr);

        if (len - bytesread >= 4) {
          if (!dct_read_data(flptr, 4, len, &bytesread, buff))
            return !isIEof(flptr);
          if (HqMemCmp(buff, 4, NAME_AND_LENGTH("8BIM")) == 0 ) {
            more_records = TRUE;
          }
        }
      } while (more_records);
    }
  }

  /* Not an Adobe segment or not for monochrome image - read to end of it */
  if (!dct_skip_data(flptr, len - bytesread, len, &bytesread))
        return FALSE;

  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            decode_APPE       author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   The APPE marker code has been found. This routine reads in the Adobe
   extension paramters. The last byte in the field is the ColorTransform
   indicator.
   The APPE marker is documented in Adobe TN 5116, Section 18.
---------------------------------------------------------------------------- */
Bool decode_APPE( FILELIST *flptr, DCTSTATE *dctstate )
{
  int32 len, bytesread = 0 ;
  uint8 buff[12] ;

  /* Get length of segment including length bytes */
  if ( (len = dct_get_16bit_num(flptr)) < 0 )
    return FALSE;

  len -= 2;

  if ( len >= 12 ) {

    if ( !dct_read_data(flptr, 12 , len, &bytesread, buff))
      return FALSE;

    /* An Adobe APPE marker is always 14 bytes including the length - first 5
     * bytes in Adobe segment should say 'Adobe' */
    if ( HqMemCmp(buff, 5, NAME_AND_LENGTH("Adobe")) == 0 ) {
      /* Found Adobe string - ignore next 6 bytes (3 2-byte values for
         version number, flag zero, flag one. */

      /* Finally get the ColorTransform byte */
      if ( buff[11] > 2 ) {
        HQFAIL("Unknown Adobe JPEG ColorTransform value - please email core.");
        return FALSE ;
      }

      dctstate->colortransform = buff[11] ;
    }
  }

  /* Not an Adobe segment or not for monochrome image - read to end of it */
  if ( !dct_skip_data(flptr, len-bytesread, len, &bytesread))
    return FALSE;
  return TRUE ;
}

/*
 * Decode the Application signalling marker.
 *
 * Check for a JFIF marker.  This is effectively just an integrity check: it
 * used to force the color transform to 1, on the probably invalid assumption
 * that JFIF files are always 3-color and/or they always need transforming.
 * The ColorTransform in the PostScript can now only be overridden by one
 * supplied in the APPE marker (if any).  The default value if it is needed is
 * decided upon once the SOF marker is seen and the number of colors is known
 * definitively.
 *
 */

#define JFIF_LEN 14

Bool decode_APP0( FILELIST *flptr, DCTSTATE *dctstate )
{
  int32 len, bytesread = 0 ;
  uint8 buff[JFIF_LEN];

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2;

  if (len >= JFIF_LEN) {

    if ( !dct_read_data(flptr, JFIF_LEN , len, &bytesread, buff))
      return FALSE;

    /* check for JFIF name */
    if (buff[0]==0x4A && buff[1]==0x46 && buff[2]==0x49 &&
        buff[3]==0x46 && buff[4]==0) {
      USERVALUE xres, yres ;
      OBJECT value = OBJECT_NOTVM_NOTHING ;

      /* Major version must be 1 - just check for interest */
      HQTRACE(debug_dctdecode && (buff[5] != 1), ("Unknown JFIF version"));
      /* Minor version should be 0..2, but try to process anyway if newer */
      HQTRACE(debug_dctdecode && (buff[6] > 2),
              ("Newer version of JFIF - there may be problems"));

      xres = (USERVALUE)((buff[8] << 8) | buff[9]) ;
      yres = (USERVALUE)((buff[10] << 8) | buff[11]) ;

      switch ( buff[7] ) { /* Look at unit size */
      default:
        HQTRACE(debug_dctdecode,("Unrecognised JFIF unit size")) ;
        /* DROP THRU */
      case 0: /* No units, just pixel sizes */
        break ;
      case 2: /* dpcm: convert to dpi */
        xres *= 2.54f ;
        yres *= 2.54f ;
        /*@fallthrough@*/
      case 1: /* dpi */
        /* Zero x and y densities are illegal according to the JFIF spec,
         * but we ignore them and pretend the resolution data did not exist.
         * Default resolution is handled by XPS (96dpi) or HqnImage (180dpi).
         */
        if ( xres > 0.0f ) {
          object_store_real(&value, xres) ;
          if ( filter_info_callback(dctstate->match, NAME_XResolution, &value,
                                    &dctstate->match_done) )
            return dctstate->match_done ;
        }
        if ( yres > 0.0f ) {
          object_store_real(&value, yres) ;
          if ( filter_info_callback(dctstate->match, NAME_YResolution, &value,
                                    &dctstate->match_done) )
            return dctstate->match_done ;
        }
      }
    }
  }

  /* consume the rest of the marker */
  if ( !dct_skip_data(flptr, len-bytesread, len, &bytesread))
    return FALSE;
  return TRUE ;
}

/*
 * Decode the APP2 record.  This has ICC Profile information.
 *
 *
 */
#define ICCPROF_LEN 14

Bool decode_APP2( FILELIST *flptr, DCTSTATE *dctstate )
{
  int32 len, bytesread = 0 ;
  uint8 buff[ICCPROF_LEN];

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2;

  if (len >= ICCPROF_LEN) {
    if ( !dct_read_data(flptr, ICCPROF_LEN, len, &bytesread, buff))
      return FALSE;

    /* Check for the icc tag we are interested in */
    if ( HqMemCmp(buff, 11, NAME_AND_LENGTH("ICC_PROFILE")) == 0 ) {
      OBJECT *ar1;
      int num_markers =  buff[13];
      int seq_no =  buff[12];
      int i ;
      Bool missing;
      uint8 *str;

      /*
       * num_markers may be validly 0, in which case we should skip data
       * without error and not try and allocate an array of size 0.
       * Could also invalidly by less than seq_no, which should throw an error.
       */
      if ( seq_no > num_markers )
        return FALSE;

      if ( num_markers != 0 ) {

        if (oType(dctstate->icc_profile_chunks) != OARRAY) {
          ar1 =  mm_alloc(mm_pool_temp,
                          sizeof(OBJECT)*num_markers,
                          MM_ALLOC_CLASS_DCT_BUFFER);
          if (ar1 == NULL)
            return error_handler(VMERROR) ;

          theTags(dctstate->icc_profile_chunks) = OARRAY|LITERAL| UNLIMITED;
          theLen(dctstate->icc_profile_chunks) = CAST_TO_UINT16(num_markers);
          oArray(dctstate->icc_profile_chunks) = ar1;

          for (i=0;i<num_markers;i++)
            ar1[i] = onothing ; /* Struct copy to set slot properties */
        }

        ar1 = oArray( dctstate->icc_profile_chunks );
        ar1 += (seq_no-1) ;

        /*
         * Two possible problems : If the total number of slots has
         * changed causing the sequence number to fall off the end of
         * the array, or if we have seen this element before.
         * Other problem cases do not error, but instead silently ignore
         * APP2 data, so do the same here.
         */
        if ( seq_no > theLen(dctstate->icc_profile_chunks) ||
              oType(*ar1) == OSTRING ) {
          return dct_skip_data(flptr, len-bytesread, len, &bytesread);
        }

        str = (uint8*)mm_alloc(mm_pool_temp,
                               len - bytesread,
                               MM_ALLOC_CLASS_DCT_BUFFER);
        if (str == NULL)
          return error_handler(VMERROR) ;

        theTags(*ar1) = OSTRING|LITERAL|READ_ONLY;
        theLen(*ar1) =  CAST_TO_UINT16(len - bytesread);
        oString(*ar1) = str;

        if ( !dct_read_data(flptr, len - bytesread, len, &bytesread, str)) {
          return FALSE;
        }

        ar1 = oArray( dctstate->icc_profile_chunks );
        for (missing = FALSE, i=0; i<num_markers; i++)
        {
          if (oType(ar1[i]) != OSTRING) {
            missing = TRUE;
            break ;
          }
        }
        if (!missing)
        {
          /* We have all the data offsets, now pass back the array of strings
           * with the profile data
           */
          if (filter_info_callback(dctstate->match, NAME_EmbeddedICCProfile,
                &dctstate->icc_profile_chunks, &dctstate->match_done))
            return dctstate->match_done;
        }
      }
    }
  }
  /* consume the rest of the marker */
  if ( !dct_skip_data(flptr, len-bytesread, len, &bytesread))
    return FALSE;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            decode_SOF        author:              Luke Tunmer
   creation date:       09-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   This routine gets the start-of-frame parameters from the underlying file,
   and initialises the appropriate fields in the DCTSTATE structure.
---------------------------------------------------------------------------- */
Bool decode_SOF(FILELIST *filter, DCTSTATE *dctstate, int32 frame_code)
{
  int32 len ;
  int32 compind ;
  int32 i , c ;
  int32 samples ;
  uint8 *buff ;
  FILELIST *flptr ;
  COMPONENTINFO *ci ;
  OBJECT value = OBJECT_NOTVM_NOTHING ;
  float high_decode, low_decode;

  flptr = theIUnderFile( filter ) ;

  ci = NULL;   /* init to keep compiler quiet */

  if ( frame_code == SOF1 || frame_code == SOF2 ) {
    /* extended sequential dct can have four Huff tables for each of AC and DC,
     * but baseline can only have two.
     */
    dctstate->max_hufftables = MAXHUFFTABLES_PROGRESSIVE;

    if (frame_code == SOF2)
      dctstate->mode = edctmode_progressivescan;
  }

  if (( len = dct_get_16bit_num( flptr )) < 0)
    return FALSE ;
  len -= 8 ;
  if ( len < 0 )
    return FALSE ;

  /* get the  precision */
  if (( c = Getc( flptr )) == EOF )
    return FALSE ;
  if ( c != 8 )  /* Progressive DCT can support 12 bits but we don't (yet) */
    return FALSE ;

  object_store_integer(&value, c) ;
  if ( filter_info_callback(dctstate->match, NAME_BitsPerComponent, &value,
                            &dctstate->match_done) )
    return dctstate->match_done ;

  if (( c = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  dctstate->rows = c ;

  object_store_integer(&value, c) ;
  if ( filter_info_callback(dctstate->match, NAME_Height, &value,
                            &dctstate->match_done) )
    return dctstate->match_done ;

  if (( c = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  dctstate->columns = c ;

  object_store_integer(&value, c) ;
  if ( filter_info_callback(dctstate->match, NAME_Width, &value,
                            &dctstate->match_done) )
    return dctstate->match_done ;

  /* Width 0 0 -Height 0 Height */
  if ( filter_info_ImageMatrix(dctstate->match,
                               dctstate->columns, 0.0, 0.0, -dctstate->rows,
                               0.0, dctstate->rows, &dctstate->match_done) )
    return dctstate->match_done ;

  dctstate->endblock = (dctstate->columns + 7)/8;

  if (( compind = Getc( flptr )) == EOF )
    return FALSE ;
  if (( 3 * compind ) != len ) /* the length info is wrong */
    return FALSE ;

  dctstate->colors = compind ;

  /* Supply a default ColorTransform if not already explicitly set by
   * either the dictionary passed to the filter or an APPE marker.
   */

  if ( dctstate->colortransform == -1 ) {
    if ( dctstate->colors == 3 )
      dctstate->colortransform = 1 ;
    else
      dctstate->colortransform = 0 ;
  }

  if (dctstate->colors == 4 )
  {
    low_decode = 1.0f;
    high_decode = 0.0f;
  }
  else
  {
    low_decode = 0.0f;
    high_decode = 1.0f;
  }

  if ( filter_info_Decode(dctstate->match, dctstate->colors, low_decode,
                          high_decode, &dctstate->match_done) )
    return dctstate->match_done ;

  switch ( dctstate->colors ) {
  case 1:
    object_store_name(&value, NAME_DeviceGray, LITERAL) ;
    break ;
  case 3:
    object_store_name(&value, NAME_DeviceRGB, LITERAL) ;
    break ;
  case 4:
    object_store_name(&value, NAME_DeviceCMYK, LITERAL) ;
    break ;
  default:
    HQFAIL("Unsupported number of colours in JPEG image") ;
    /*@fallthrough@*/
  case 2: /* ColorSpace must be known by caller */
    object_store_null(&value) ;
    break ;
  }
  if ( oType(value) == ONAME ) {
    if ( filter_info_callback(dctstate->match, NAME_ColorSpace, &value,
                              &dctstate->match_done) )
      return dctstate->match_done ;
  }

  /* Now we know for sure whether we need to init the tables. */

  if ( dctstate->colortransform > 0 )
    if ( ! init_YUV_to_RGB_tables())
      return FALSE ;

  dctstate->max_hsamples = 0 ;
  dctstate->max_vsamples = 0 ;

  dctstate->blocks_in_MDU = 0 ;

  /* sample_211 is used to optimise YUV to RGB transformations for the common
   * sampling ratios (of 2:1:1).
   */
  dctstate->sample_211 = (dctstate->colortransform == 1) ||
                         (dctstate->colortransform == 2);
  if (compind != 3)
    dctstate->sample_211 = 0;

  for ( i = 0 ; i < compind ; i++ ) {
    ci = &dctstate->components[i] ;
    if (( len = Getc( flptr )) == EOF )
      return FALSE ;
    ci->id_num = len ;
    if (( samples = Getc( flptr )) == EOF )
      return FALSE ;
     if (( len = (samples >> 4)) > 4 )
       return FALSE ;
    ci->num_hsamples = len ;
    if (i==0 && samples != 0x22)
      dctstate->sample_211 = 0; /* 0x22 is 2x2 sampling */
    if (i!=0 && samples != 0x11)
      dctstate->sample_211 = 0;

    if ( len > (int32)dctstate->max_hsamples )
      dctstate->max_hsamples = len ;
    if (( len = samples & 0x0f ) > 4 )
      return FALSE ;
    ci->num_vsamples = len ;
    if ( len > (int32)dctstate->max_vsamples )
      dctstate->max_vsamples = len ;
    if (( len = Getc( flptr )) == EOF )
      return FALSE ;
    if ( len > 4 )
      return FALSE ;
    ci->qtable_number = (uint8)len ;

    dctstate->blocks_in_MDU = (dctstate->blocks_in_MDU +
               (ci->num_hsamples * ci->num_vsamples)) ;
  }

  if ( dctstate->colors == 1 ) {
    /* If there is only one color component, then we can't be interleaving. */
    ci->num_vsamples = 1 ;
    ci->num_hsamples = 1 ;
    dctstate->max_vsamples = 1 ;
    dctstate->max_hsamples = 1 ;
  }

  dctstate->cols_in_MDU =  (8 * dctstate->max_hsamples) ;
  dctstate->rows_in_MDU =  (8 * dctstate->max_vsamples) ;

  dctstate->non_integral_ratio = check_non_integral_ratios( dctstate ) ;

  ci = dctstate->components ;
  for ( i = 0 ; i < compind ; i++ , ci++ ) {
    ci->h_skip = ((int32)dctstate->max_hsamples / (int32)ci->num_hsamples) ;
    ci->v_skip = ((int32)dctstate->max_vsamples / (int32)ci->num_vsamples) ;
    ci->sample_size = ((int32) ci->h_skip * (int32) ci->v_skip) ;
    ci->sample_size2 = ((int32)ci->sample_size / 2) ;
    ci->mdu_block = (int32 *)mm_alloc(mm_pool_temp,
                                      64 * dctstate->max_hsamples *
                                      dctstate->max_vsamples * sizeof(int32),
                                      MM_ALLOC_CLASS_DCT_BUFFER);
    if (ci->mdu_block == NULL)
      return error_handler( VMERROR );
  }

  /* continue to calculate MDU values */
  dctstate->num_mdublocks = dctstate->colors ;
  dctstate->bytes_in_scanline = dctstate->columns * dctstate->colors ;

  /* allocate filter buffer space */
  len = dctstate->bytes_in_scanline * (int32) dctstate->rows_in_MDU ;
  buff = ( uint8 * )mm_alloc( mm_pool_temp ,
                              len + 1 ,
                              MM_ALLOC_CLASS_DCT_BUFFER ) ;
  if ( buff == NULL )
    return error_handler( VMERROR );

  buff++ ;
  theIBuffer( filter ) = buff ;
  theIPtr( filter ) = buff ;
  theIBufferSize( filter ) = len ;

  return TRUE ;
}

/* fetch all the coefficients for this block so far collected */
static Bool fetch_zigzag(DCTSTATE *dct, FILELIST *flptr, scaninfo * info)
{
  scaninfo * pinfo;
  Hq32x2 startpos;
  int32 i;
  int32 bb_nbits, bb_data;
  Bool ret = TRUE;

  if ( (*theIMyFilePos(flptr))(flptr, &startpos) == EOF ) {
    ret =  (*theIFileLastError( flptr ))( flptr ) ;
    goto fetch_zigzag_err;
  }

  for (i = 0;i <64;i++)
    dct->coeffs[i] = 0;

  for (pinfo = dct->info;pinfo;pinfo = pinfo->next) {
    if ((pinfo->compbits & (1 << dct->compind)) == 0)
      continue;

    if ((*theIMyResetFile( flptr ))( flptr ) == EOF ) {
      ret =  (*theIFileLastError( flptr ))( flptr ) ;
      goto fetch_zigzag_err;
    }
    if ((*theIMySetFilePos( flptr ))( flptr , &pinfo->cSOSo ) == EOF ) {
      ret =  (*theIFileLastError( flptr ))( flptr );
      goto fetch_zigzag_err;
    }

    bb_nbits = pinfo->bbuf.nbits;
    bb_data = pinfo->bbuf.data;
    dct->currinfo = pinfo;

    pinfo->bbuf = pinfo->a_bbuf;

    if ( zigzag_fetch[pinfo->type]( dct , flptr , pinfo) < 0 ) {
      ret =  FALSE;
      goto fetch_zigzag_err;
    }

    pinfo->a_bbuf= pinfo->bbuf;

    pinfo->bbuf.nbits = bb_nbits;
    pinfo->bbuf.data = bb_data;

    if ( (*theIMyFilePos(flptr))(flptr, &pinfo->cSOSo) == EOF ) {
      ret = ((*theIFileLastError(flptr))(flptr));
      goto fetch_zigzag_err;
    }
  }

  if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
    return (*theIFileLastError( flptr ))( flptr );
  if ((*theIMySetFilePos( flptr ))( flptr , &startpos ) == EOF )
    return (*theIFileLastError( flptr ))( flptr );

fetch_zigzag_err:

  dct->currinfo = info;

  return ret;
}

static DCT_RESULT fetch_zigzag_AC(DCTSTATE *dct,
                                  FILELIST *flptr,
                                  scaninfo * info )
{
  int32 s , k , r , end;
  HUFFTABLE *ac_huff = &dct->ac_huff.tables[(int32) info->ac_huff_number[0]];

  if (info->EOBrun) {
    info->EOBrun--;
    return DCT_OK;
  }

  k = info->Ss;
  end = info->Se;
  HQASSERT(k != 0,"DC coefficient found when looking for AC");

  /* decode AC coefficients */
  for ( ; k <= end ; k++ ) {
    BB_CODE_MAY_RETURN(info, flptr, r, ac_huff);

    s = r & 0x0f ;
    r = r >> 4 ;

    if ( s ) {
      k += r ;

      GET_BITS_MAY_RETURN(info, flptr, s, r);

      if ( k > end ) {
        HQTRACE( debug_dctdecode, ( "too many AC coefficients" )) ;
        break ;
      }

      huff_EXTEND( r , s ) ;

      dct->coeffs[ k ] = r << info->Al;
    } else {
      if ( r != 15 ) {
        info->EOBrun = 1 << r;
        if (r) {
          GET_BITS_MAY_RETURN(info, flptr, r, r);
          info->EOBrun += r;
        }
        info->EOBrun--;
        break ;
      }
      k += 15 ;
    }
  }

  return DCT_OK;
}



static DCT_RESULT skip_zigzag_AC(DCTSTATE *dct,
                                 FILELIST *flptr,
                                 scaninfo * info )
{
  int32 s , k , r, end ;
  HUFFTABLE *ac_huff = &dct->ac_huff.tables[(int32) info->ac_huff_number[0]];

  /* skip the block if the EOB run is unfinished */
  if (info->EOBrun) {
    info->EOBrun--;
    return DCT_OK;
  }

  k = info->Ss;
  end = info->Se;

  HQASSERT(k!=0,"unexpected dc coefficient");

  /* decode AC coefficients */
  for ( ; k <= end ; k++ ) {

    BB_CODE_MAY_RETURN(info, flptr, r, ac_huff);

    s = r & 0x0f ;
    r = r >> 4 ;

    if ( s ) {
      k += r ;

      GET_BITS_MAY_RETURN(info, flptr, s, r);

      if ( k > end ) {
        HQTRACE( debug_dctdecode, ( "too many AC coefficients" )) ;
        break ;
      }
   }
    else {
      if ( r != 15 ) {
        info->EOBrun = 1 << r;
        if (r) {
          GET_BITS_MAY_RETURN(info, flptr, r, r);
          info->EOBrun += r;
        }
        info->EOBrun--;
        break ;
      }
      k += 15 ;
    }
  }
  return DCT_OK;
}

static DCT_RESULT fetch_zigzag_AC_successive(DCTSTATE *dct,
                                             FILELIST *flptr,
                                             scaninfo * info )
{
  int32 s , k , r, d, end ;
  HUFFTABLE *ac_huff = &dct->ac_huff.tables[(int32) info->ac_huff_number[0]];
  int32 p1 = 1 << info->Al;
  int32 m1 = (-1) << info->Al;

  k = info->Ss;
  end = info->Se;

  HQASSERT(k!=0,"unexpected dc coefficient");

  /* skip the block if the EOB run is unfinished */
  if (info->EOBrun == 0) {

    /* decode AC coefficients */
    for ( ; k <= end ; k++ ) {

      BB_CODE_MAY_RETURN(info, flptr, r, ac_huff);

      s = r & 0x0f ;
      r = r >> 4 ;

      if ( s ) {

        if ( s != 1 ) {
          HQTRACE( debug_dctdecode, ( "bad DC code" )) ;
          break ;
        }

        GET_BITS_MAY_RETURN(info, flptr, 1, d);

        if (d) {
          s = p1;
        } else {
          s = m1;
        }
      }
      else {
        if ( r != 15 ) {
          info->EOBrun = 1 << r;
          if (r) {
            GET_BITS_MAY_RETURN(info, flptr, r, d);
            info->EOBrun += d;
          }
          break ;
        }
      }

      do{
          if (dct->coeffs[k] != 0) {
            GET_BITS_MAY_RETURN(info, flptr, 1, d);

            if (d) {
              if ((dct->coeffs[k] & p1) == 0) {
                if (dct->coeffs[k] >= 0)
                  dct->coeffs[k] += p1;
                else
                  dct->coeffs[k] += m1;
              }
            }
          } else {
            if (--r < 0)
              break;
          }

          k++;

      } while (k <= end);

      if ((s) && (k < 64))
        dct->coeffs[k] = s;
    }
  }

  /* skip the block if the EOB run is unfinished */
  if (info->EOBrun > 0) {
    for ( ; k <= end; k++)
    {
      if (dct->coeffs[k] != 0)
      {
        GET_BITS_MAY_RETURN(info, flptr, 1, d);

        if (d) {
          if ((dct->coeffs[k] & p1) == 0) {
            if (dct->coeffs[k] >= 0)
              dct->coeffs[k] += p1;
            else
              dct->coeffs[k] += m1;
          }
        }
      }
    }
    info->EOBrun--;
  }

  return DCT_OK;
}



static DCT_RESULT skip_zigzag_AC_successive(DCTSTATE *dct,
                                            FILELIST *flptr,
                                            scaninfo * info )
{
  int32 s , k , r, d, end ;
  HUFFTABLE *ac_huff = &dct->ac_huff.tables[(int32) info->ac_huff_number[0]];

  /* progressive scanning using successive approximation requires
     prior knowledge of the coefficient already coded. So we need to
     go back and read in the previous scans again. */
  if (!fetch_zigzag(dct,flptr,info))
    return DCT_ERR;

  k = info->Ss;
  end = info->Se;

  HQASSERT(k!=0,"unexpected dc coefficient");

  /* skip the block if the EOB run is unfinished */
  if (info->EOBrun == 0) {

    /* decode AC coefficients */
    for ( ; k <= end ; k++ ) {

      BB_CODE_MAY_RETURN(info, flptr, r, ac_huff);

      s = r & 0x0f ;
      r = r >> 4 ;

      if ( s ) {

        if ( s != 1 ) {
          HQTRACE( debug_dctdecode, ( "bad DC code" )) ;
          break ;
        }

        GET_BITS_MAY_RETURN(info, flptr, 1, d);
      } else {
        if ( r != 15 ) {
          info->EOBrun = 1 << r;
          if (r) {
            GET_BITS_MAY_RETURN(info, flptr, r, d);
            info->EOBrun += d;
          }
          break ;
        }
      }

      do{
          if (dct->coeffs[k] != 0) {
            GET_BITS_MAY_RETURN(info, flptr, 1, d);
          } else {
            if (--r < 0)
              break;
          }

          k++;

      } while (k <= end);
    }
  }

  /* skip the block if the EOB run is unfinished */
  if (info->EOBrun > 0) {
    for ( ; k <= end; k++) {
      if (dct->coeffs[k] != 0) {
        GET_BITS_MAY_RETURN(info, flptr, 1, d);
      }
    }
    info->EOBrun--;
  }

  return DCT_OK;
}


static DCT_RESULT fetch_zigzag_DC_successive( DCTSTATE *dct,
                                              FILELIST *flptr,
                                              scaninfo * info )
{
  int32 s;

  /* note the DC coeff in dct->coeffs[0] is uptodate*/

  /* decode the DC coefficient */

  GET_BITS_MAY_RETURN(info, flptr, 1, s);

  if (s) {
    dct->coeffs[0] |= 1 << info->Al;
  }

  return DCT_OK;
}

static DCT_RESULT skip_zigzag_DC(DCTSTATE *dct,
                                 FILELIST *flptr,
                                 scaninfo * info )
{
  int32 s;
  /* decode the DC coefficient */
  BB_CODE_MAY_RETURN(info, flptr, s, dct->dc_huff.current);

  if ( s ) {
    GET_BITS_MAY_RETURN(info, flptr, s, s);
  }

  return DCT_OK;
}

static DCT_RESULT skip_zigzag_DC_successive(DCTSTATE *dct,
                                            FILELIST *flptr,
                                            scaninfo * info )
{
  int32 s;
  UNUSED_PARAM( DCTSTATE * , dct ) ;

  /* decode the DC coefficient */

  GET_BITS_MAY_RETURN(info, flptr, 1, s);
  return DCT_OK;
}



static DCT_RESULT skip_baseline_zigzag(DCTSTATE *dct,
                                       FILELIST *flptr,
                                       scaninfo * info )
{
  int32 s, k, r;

  /* decode the DC coefficient */
  BB_CODE_MAY_RETURN(info, flptr, s, dct->dc_huff.current);
  r = 0 ;

  if ( s ) {
    GET_BITS_MAY_RETURN(info, flptr, s, r);
  }

  /* decode AC coefficients */
  for (k = 1 ; k <= 63 ; k++ ) {
    BB_CODE_MAY_RETURN(info, flptr, r, dct->ac_huff.current);

    s = r & 0x0f ;
    r = r >> 4 ;

    if ( s ) {
      k += r ;

      GET_BITS_MAY_RETURN(info, flptr, s, r);

      if ( k > 63 ) {
        HQTRACE( debug_dctdecode, ( "too many AC coefficients" )) ;
        break ;
      }
    } else {
      if ( r != 15 )
        break ;
      k += 15 ;
    }
  }
  return DCT_OK;
}

/**
 * List of DCT coefficients
 *
 * The coefficients form an 8by8 array, but that array will often be sparse.
 * So, for performance reasons, store the coefficients in a list.
 * Input values are in range -128 <= x <= 128, so pushing them through the
 * DCT transform will put them into the range -1024 < coefficient <= 1024.
 * So if the list needs to store these values and indices (0...63) then an
 * int16 will do.
 */
typedef struct DCTLIST
{
  int16 nc;    /**< number of coefficients present in the list */
  struct
  {
    int16 zzi;   /* coeffieicent index (in zig-zag ordering) */
    int16 val;   /* coeffieicent value */
  } coeff[64]; /* maximum of 64 coefficients in 8by8 array */
} DCTLIST;

/**
 * Decode AC coefficients : main loop
 *
 * This code loop is a huge performance bottleneck and has
 * been carefuly analysed and optimised. Do not alter without
 * testing any performance effect.
 */
static DCT_RESULT jpg_decode_ac(DCTSTATE *dct, FILELIST *flptr, scaninfo *info,
                                DCTLIST *dctlist)
{
  HUFFTABLE *huff = dct->ac_huff.current;
  uint32 pbyte;
  int32 k, s, r, pbits;

  pbits = info->bbuf.nbits;
  pbyte = info->bbuf.data;
  for (k = 1; k <= 63; k++ )
  {
    int32 ch, value, code;

    if ( pbits )
    {
      /* Try partial bits first, if present. Zero pad to 8 bits */
      value = huff->hashtable[(pbyte << (8-pbits)) & BB_MASK];

      if ( value >= 0  && (int32)(value >> 16) <= pbits )
      {
        pbits -= (int32)(value >> 16);
        r = (int32)(value & 0xFFFF);
        goto got_bits;
      }
    }
    /* if we get here, then code is longer than the buffer */
    /* so ask for 8 bits, and no end problems should arise */

    code = (pbyte << (8 - pbits)) & BB_MASK;
    if (( ch = Getc( flptr )) == EOF )
      return DCT_ERR;
    if ( ch == BSTUFF1 )
    {
      if ((( ch = Getc( flptr )) == EOF ) || ( ch != BSTUFF2 ))
        return check_eoi(ch);
      ch = BSTUFF1;
    }
    pbyte = ch;
    code |= ( pbyte >> pbits );

    value = huff->hashtable[code];

    if ( value == INVALID_CODE )  /* invalid huffman code */
      return DCT_ERR;
    else if ( value == LONG_CODE )
    {
      int32 *maxcode = huff->maxcode;
      int32 length = 7; /* one less than number of bits collected so far */

      /* code length > 8 */
      /* use code from the original function */

      do
      {
        if ( pbits == 0 )
        {
          if (( ch = Getc( flptr )) == EOF )
            return DCT_ERR;
          if ( ch == BSTUFF1 )
          {
            if (( ch = Getc( flptr )) == EOF )
            {
              pbyte = 0;
              return DCT_ERR;
            }
            if ( ch == BSTUFF2 )
              ch = BSTUFF1;
            else
            {
              pbyte = ch;
              return check_eoi(ch);
            }
          }
          pbyte = ch;
          pbits = BB_NBITS;
        }
        pbits--;
        length++;
        code = ( code << 1 ) | (( pbyte >> pbits ) & 0x0001);
      } while ( code > maxcode[length] );

      if ( length > 15 )
        return DCT_ERR;

      ch = (int32)(huff->valptr[length]) + code
        - (int32) (huff->mincode[length]);
      r = huff->huffval[ch];
    }
    else
    {
      /* valid code, length is <= 8 : Put back unused part of code */
      pbits += 8 - (int32)(value >> 16);
      HQASSERT(pbits <= 8,"Invalid DCT code");
      r = (int32) (value & 0xFFFF);
    }
    got_bits:

    s = r & 0x0f;
    r = r >> 4;

    if ( s )
    {
      k += r;

      if ( s <= pbits )
      {
        pbits -= s;
        r = (pbyte >> pbits) & (BB_MASK >> ( 8 - s));
      }
      else
      {
        int32 nbits = s;

        r = ( pbyte & ( BB_MASK >> ( 8 - pbits ))) <<
          ( nbits - pbits);
        nbits -= pbits;
        if (( ch = Getc( flptr )) == EOF )
          return DCT_ERR;
        if ( ch == BSTUFF1 ) {
          if ((( ch = Getc( flptr )) == EOF ) || ( ch != BSTUFF2 ))
            return check_eoi(ch);
          ch = BSTUFF1;
        }
        pbyte = ch;
        pbits = BB_NBITS;
        if ( pbits < nbits )
        {
          r |= ( pbyte << ( nbits - pbits ));
          nbits -= pbits;
          if (( ch = Getc( flptr )) == EOF )
            return DCT_ERR;
          if ( ch == BSTUFF1 )
          {
            if ((( ch = Getc( flptr )) == EOF ) || ( ch != BSTUFF2 ))
              return check_eoi(ch);
            ch = BSTUFF1;
          }
          pbyte = ch;
          pbits = BB_NBITS - nbits;
          r |= ( pbyte >> pbits );
        }
        else
        {
          pbits -= nbits;
          r |= ( pbyte >> pbits );
        }
      }
      if ( k > 63 ) {
        /* Badly formed DCT. Silently ignore for compatibility */
        HQTRACE(debug_dctdecode, ("too many AC coefficients"));
        break;
      }
      if (!(r & extend_test[s]))
        r += extend_offset[s];
      /* Coeffcient out of range. Silently ignore for compatibility */
      HQTRACE(debug_dctdecode && (r > 1024 || r < -1024),("Invalid JPG Value"));
      dctlist->coeff[dctlist->nc].zzi = (int16)k;
      dctlist->coeff[dctlist->nc].val = (int16)r;
      dctlist->nc++;
    }
    else
    {
      if ( r != 15 )
        break;
      k += 15;
    }
  }
  info->bbuf.nbits = pbits;
  info->bbuf.data = pbyte;
  return DCT_OK;
}

/**
 * Decode the JPEG AC and DC coefficients
 */
static DCT_RESULT jpg_ac_dc(DCTSTATE *dct, FILELIST *flptr, scaninfo *info,
                            DCTLIST *dctlist)
{
  int32 s, r = 0;

  dctlist->nc = 0; /* no coeffients found yet */

  /* Decode the DC coefficient */
  BB_CODE_MAY_RETURN(info, flptr, s, dct->dc_huff.current);

  if ( s )
  {
    GET_BITS_MAY_RETURN(info, flptr, s, r);
    huff_EXTEND(r, s);
  }
  r += dct->current_ci->dc_prediction;
  dct->current_ci->dc_prediction = (int16)r;
  if ( r )
  {
    r = (r << info->Al);
    /* Coeffcient out of range. Silently ignore for compatibility */
    HQTRACE(debug_dctdecode && (r > 1024 || r < -1024),("Invalid JPG Value"));
    dctlist->nc++;
    dctlist->coeff[0].zzi = 0;
    dctlist->coeff[0].val = (int16)r;
  }
  return jpg_decode_ac(dct, flptr, info, dctlist);
}

/**
 * 8 unique elements of inverse dct matrix,
 * where Ax = C(x)*cos(PI*x/16) [ C(x) = (x == 0 ? 1/sqrt(2) : 1) ]
 */
#define A0 0x5A82
#define A1 0x7D8A
#define A2 0x7642
#define A3 0x6A6E
#define A4 0x5A82
#define A5 0x471D
#define A6 0x30FC
#define A7 0x18F9

/**
 * Convert a zig-zag index into a regular index
 */
static int32 unzig[] =
{
   0,  1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

/**
 * Is the new inverse DCT enabled yet ?
 */
Bool new_jpg_idct = TRUE;

/**
 * Calculate the JPEG Inverse DCT
 *
 * Two different methods used based on the sparseness of the coefficient array.
 * If the array is sparse, then carry out the transform one element at a time.
 * Else decompose the 2D inverse DCT into two 1D ones and apply them in order.
 *
 * The formula for the 2-D inverse DCT transformation is given by
 *
 *   f(x,y) = sum(u=0...7, C(u)/2 * sum(v=0...7, C(v)/2 * F(u,v) *
 *            cos(u*PI*(2x+1)/16) * cos(v*PI*(2*y+1)/16) ) )
 *
 *   where C(x) = 1/sqrt(2) if x == 0
 *              = 1         otherwise
 *   and F(x,y) is the de-coded DCT coefficient at location (x,y)
 *
 * But this formula can be re-organised to be a pair of successive 1-D
 * transformations, which can be calculated much more effeciently. i.e.
 *
 * Let
 *   D(a,b) = C(a)/2 * cos(PI*a*(2*b+1)/16)
 * Then
 *   f(x,y) = sum(u=0...7, sum(v=0...7, * F(u,v) * D(u,x) * D(v,y) ) )
 * can be re-written as
 *   F'(x,v) = sum(u=0...7, F(u,v)  * D(u,x) )
 *   f(x,y)  = sum(v=0...7, F'(x,v) * D(v,y) )
 *
 * Now D(a,b) is periodic in both arguments, with period 8. So it is only of
 * interest for 0 <= a,b <= 7, so a 64 entry lookup table could be used for
 * these values, in a fixed-point format.
 *
 * But in fact, because of the periodicy of the cosine function used as a
 * basis for D(a,b), the 64 element array only has 8 different entries
 * (allowing for +/-). i.e. If we define Ax = C(x)*cos(PI*x/16) Then the
 * D(a,b) matrix can be written as
 *
 *   A0  A1  A2  A3  A4  A5  A6  A7
 *   A0  A3  A6 -A7 -A4 -A1 -A2 -A5
 *   A0  A5 -A6 -A1 -A4  A7  A2  A3
 *   A0  A7 -A2 -A5  A4  A3 -A6 -A1
 *   A0 -A7 -A2  A5  A4 -A3 -A6  A1
 *   A0 -A5 -A6  A1 -A4 -A7  A2 -A3
 *   A0 -A3  A6  A7 -A4  A1 -A2  A5
 *   A0 -A1  A2 -A3  A4 -A5  A6 -A7
 *
 * Which has many symmetries and patterns within it which can be exploited to
 * reduced the number of multiplies and adds required. This can be taken
 * further with double angle and other trig identities, to find various
 * relationships between the constants.
 * e.g.
 *   sqrt(2)*A0 = cos(0*PI/16) = 1
 *           A1 = cos(1*PI/16) = sqrt(2 + sqrt(2 + sqrt(2)))/2
 *           A2 = cos(2*PI/16) = sqrt(2 + sqrt(2))/2
 *           A3 = cos(3*PI/16) = sqrt(2 + sqrt(2 - sqrt(2)))/2
 *           A4 = cos(4*PI/16) = sqrt(2)/2
 *           A5 = cos(5*PI/16) = sqrt(2 - sqrt(2 - sqrt(2)))/2
 *           A6 = cos(6*PI/16) = sqrt(2 - sqrt(2))/2
 *           A7 = cos(7*PI/16) = sqrt(2 - sqrt(2 + sqrt(2)))/2
 *
 * So the transform loops can be unrolled to minimise the number of
 * multiplies and adds required.
 *
 * Also note we are doing a double transform using fixed point, so care has
 * to be taken to ensure that overflows are not possible. Becasue this is
 * two sequential calculations, we have to adjust for the fixed-point twice
 * rather than once, which implies many more bits may be needed.
 * A dequantised co-efficient will be in the range -128 < v < +128
 * So the sum of eight of them times IDCT matrix coefficients which can be as
 * big as 0.5 is basically 4 times the range, i.e. 10 bits scaled up by the
 * fixed-point factor.
 * If we use 16.16 fixed point, then out double transform goes from
 * 8 bits -> 26 bits -> 44 bits -> 28 bits (re-normalising)
 * So we have to introduce a de-scale somewhere in the middle of the
 * process to avoid overflowing 32 bits.
 * Do this as 1 12 bit shift for stage 1, and a 4 bit shift for stage 2.
 * This maximise accuracy whilst avoiding overflow.
 */
static void jpg_calc_idct(DCTSTATE *dct, DCTLIST *dctlist)
{
  QUANTTABLE qtable = dct->quanttables[(int32)dct->current_ci->qtable_number];
  int32 i, v[8], tile[8][8], ws[8][8], dtrans[64];

  /*
   * If new code not yet enabled just do the work via the splay array.
   */
  if ( !new_jpg_idct )
  {
    if ( dctlist->nc == 0 || dctlist->coeff[0].zzi != 0 )
      zero_tile();
    for ( i = 0; i < dctlist->nc ; i++ )
    {
      int zzi = dctlist->coeff[i].zzi;

      splat_array[zzi](dctlist->coeff[i].val*qtable[zzi]);
    }
    return;
  }

  /*
   * Two case happen often enough to be worth special casing as quickly as
   * possible.
   *   a) All of the de-quantised coefficients are zero
   *   b) All are zero except the first (AC) coefficient
   * After that if we only have relatively few non-zero coefficients, then
   * deal with them one at a time via the array of splat functions.
   * Otherwise do the work via two decoupled 1D transforms.
   */
  if ( dctlist->nc == 0 )
  {
    zero_tile();
    return;
  }
  else if ( dctlist->nc == 1 && dctlist->coeff[0].zzi == 0 )
  {
    splat_array[0]((int32)dctlist->coeff[0].val*qtable[0]);
    return;
  }
  else if ( dctlist->nc <= 16 )
  {
    if ( dctlist->coeff[0].zzi != 0 )
      zero_tile();
    for ( i = 0; i < dctlist->nc ; i++ )
    {
      int32 zzi = dctlist->coeff[i].zzi;

      splat_array[zzi](dctlist->coeff[i].val*qtable[zzi]);
    }
    return;
  }

  /*
   * Un-zigzag the coefficients and de-quantise them up-front to avoid
   * repeated calculations in the main loops below.
   *
   * It would also seem to be worth testing rows to see if all coefficients
   * are zero. Then we can special case the code to avoid loads of multiplies
   * when all the factors are zero. But in fact because we are using the
   * alternate 'splat' methodology above for sparse arrays, this will not
   * happen very often. And the extra testing slows us down by more than we
   * gain. So don't bother doing it.
   */
  HqMemZero(dtrans, 64*sizeof(int32));
  for ( i = 0; i < dctlist->nc; i++ )
  {
    int32 zz_i = dctlist->coeff[i].zzi;

    dtrans[unzig[zz_i]] = dctlist->coeff[i].val * qtable[zz_i];
  }

  /**
   * The two linear transforms have been unrolled and the dct array elements
   * put in explicitly. Then the 64 DCT elements have been reduced to the
   * 8 (+/-) unique values A0/A1/.../A7.
   * Looks a bit ungainly at the moment, but the next step is to factor out
   * the repeated multiplies to get better performance. Not sure how much
   * this is necessary as the compiler on Windows does alot of it for you.
   * Need to examine other platforms as well.
   * \todo BMJ 02-Apr-09 :  Further performance gains to be had ?
   */
  for ( i = 0; i < 8; i++ ) /* First 1D DCT : columns -> work array */
  {
    int32 *d = &dtrans[i*8];

    v[0] = A0*d[0]+A1*d[1]+A2*d[2]+A3*d[3]+A4*d[4]+A5*d[5]+A6*d[6]+A7*d[7];
    v[1] = A0*d[0]+A3*d[1]+A6*d[2]-A7*d[3]-A4*d[4]-A1*d[5]-A2*d[6]-A5*d[7];
    v[2] = A0*d[0]+A5*d[1]-A6*d[2]-A1*d[3]-A4*d[4]+A7*d[5]+A2*d[6]+A3*d[7];
    v[3] = A0*d[0]+A7*d[1]-A2*d[2]-A5*d[3]+A4*d[4]+A3*d[5]-A6*d[6]-A1*d[7];
    v[4] = A0*d[0]-A7*d[1]-A2*d[2]+A5*d[3]+A4*d[4]-A3*d[5]-A6*d[6]+A1*d[7];
    v[5] = A0*d[0]-A5*d[1]-A6*d[2]+A1*d[3]-A4*d[4]-A7*d[5]+A2*d[6]-A3*d[7];
    v[6] = A0*d[0]-A3*d[1]+A6*d[2]+A7*d[3]-A4*d[4]+A1*d[5]-A2*d[6]+A5*d[7];
    v[7] = A0*d[0]-A1*d[1]+A2*d[2]-A3*d[3]+A4*d[4]-A5*d[5]+A6*d[6]-A7*d[7];

    ws[0][i] = ((v[0] + (1<<11)) >> 12);
    ws[1][i] = ((v[1] + (1<<11)) >> 12);
    ws[2][i] = ((v[2] + (1<<11)) >> 12);
    ws[3][i] = ((v[3] + (1<<11)) >> 12);
    ws[4][i] = ((v[4] + (1<<11)) >> 12);
    ws[5][i] = ((v[5] + (1<<11)) >> 12);
    ws[6][i] = ((v[6] + (1<<11)) >> 12);
    ws[7][i] = ((v[7] + (1<<11)) >> 12);
  }
  for ( i = 0; i < 8; i++ ) /* Second 1D DCT : work array -> result */
  {
    int32 *d = &ws[i][0];

    v[0] = A0*d[0]+A1*d[1]+A2*d[2]+A3*d[3]+A4*d[4]+A5*d[5]+A6*d[6]+A7*d[7];
    v[1] = A0*d[0]+A3*d[1]+A6*d[2]-A7*d[3]-A4*d[4]-A1*d[5]-A2*d[6]-A5*d[7];
    v[2] = A0*d[0]+A5*d[1]-A6*d[2]-A1*d[3]-A4*d[4]+A7*d[5]+A2*d[6]+A3*d[7];
    v[3] = A0*d[0]+A7*d[1]-A2*d[2]-A5*d[3]+A4*d[4]+A3*d[5]-A6*d[6]-A1*d[7];
    v[4] = A0*d[0]-A7*d[1]-A2*d[2]+A5*d[3]+A4*d[4]-A3*d[5]-A6*d[6]+A1*d[7];
    v[5] = A0*d[0]-A5*d[1]-A6*d[2]+A1*d[3]-A4*d[4]-A7*d[5]+A2*d[6]-A3*d[7];
    v[6] = A0*d[0]-A3*d[1]+A6*d[2]+A7*d[3]-A4*d[4]+A1*d[5]-A2*d[6]+A5*d[7];
    v[7] = A0*d[0]-A1*d[1]+A2*d[2]-A3*d[3]+A4*d[4]-A5*d[5]+A6*d[6]-A7*d[7];

    tile[0][i] = ((v[0] + (1<<3)) >> 4);
    tile[1][i] = ((v[1] + (1<<3)) >> 4);
    tile[2][i] = ((v[2] + (1<<3)) >> 4);
    tile[3][i] = ((v[3] + (1<<3)) >> 4);
    tile[4][i] = ((v[4] + (1<<3)) >> 4);
    tile[5][i] = ((v[5] + (1<<3)) >> 4);
    tile[6][i] = ((v[6] + (1<<3)) >> 4);
    tile[7][i] = ((v[7] + (1<<3)) >> 4);
  }
  splat_tile(tile);
}

/**
 * Decode a JPEG MDU which has been encoded with the usual zigzag ordering.
 *
 * This is a two stage process, first the DCT coefficients (both AC and DC)
 * have to be parsed from the stream, then these values can be used to carry
 * out the inverse DCT transform.
 *
 * The inverse DCT transform basically involves multiplying every element in
 * an 8by8 array by every element in a fixed DCT 8by8 array. This can result
 * in 64*64 multiplies and even more adds being required, which can be very
 * slow.
 *
 * There are two optimisation routes :
 *  1) You can assume that many of the coefficients will often be zero, and
 *     create the code loops so that they deal with each coeffcient in turn
 *     and can be easily optimised to be a no-op if that coefficient is 0.
 *  2) You can break down the 2D DCT transform into two successive 1D ones.
 *     i.e. 8by8 * 8by8 turns into two successive 8 * 8 operations.
 *
 * The second method is much more powerful, being able to reduce an N^2 to
 * a 2*N one. But in the case of very sparse arrays, method 1 has been shown
 * to win. The code was originally developed to only use method 1. Method 2
 * was developed to speed things up. But given method 1 stills wins for very
 * sparse arrays is seems sensible to create a hybrid of the two.
 *
 * The balance point between the two approaches is hard to find, as it will
 * depend on the relative speeds of multiply/add/memory-access on any given
 * platform. A rough and ready switch-over point has been added based on
 * performance testing on Windows. It may be worth further testing on other
 * platforms to see if the threshold need to be moved.
 *
 * The two methods perform operations in a different order with different
 * degrees of precision. So it is often the case that rounding variations
 * will occur and cause output pixel differences by a single part in 256
 * ( or perhaps two if you are unlucky ). This is unavoidable and does
 * not introduce any visible quality artifacts.
 *
 * But because of the number of QA chnages this will introduce, method 2
 * is currently disabled into its development stabilises.
 * \todo BMJ 03-Apr-09 :  enable DCT method 2
 */
static DCT_RESULT decode_normal_zigzag(DCTSTATE *dct, FILELIST *flptr,
                                       scaninfo *info)
{
  DCTLIST dctlist;
  DCT_RESULT err;

  if ( (err = jpg_ac_dc(dct, flptr, info, &dctlist)) < 0 ) {
    if ( err == DCT_EOI ) {
      dct->dct_status = GOT_EOI;
      return DCT_OK;
    }
    return err;
  }
  jpg_calc_idct(dct, &dctlist);
  return DCT_OK;
}

static DCT_RESULT fetch_zigzag_DC(DCTSTATE *dct,
                                  FILELIST *flptr,
                                  scaninfo * info )
{
  int32 s,r;
  BB_CODE_MAY_RETURN(info, flptr, s, dct->dc_huff.current);

  r = 0 ;

  if ( s ) {
    GET_BITS_MAY_RETURN(info, flptr, s, r);
    huff_EXTEND( r, s ) ;
  }

  r += dct->current_ci->dc_prediction ;

  dct->coeffs[0] = r << info->Al;

  dct->current_ci->dc_prediction = ( int16 )r ;

  return DCT_OK;
}

static Bool skip_MDU(register DCTSTATE *dct, FILELIST *flptr)
{
  int32 k , compind , v , h, hsamples, vsamples;
  zigzag_decode skip;
  scaninfo * info = dct->currinfo;
  uint32 * dc_huffindex;
  uint32 * ac_huffindex;

  if ( dct->restart_interval ) {
    if ( dct->restarts_to_go == 0 ) {
      if ( ! process_restart( dct , flptr ))
        return FALSE ;
    }
    dct->restarts_to_go-- ;
  }

  skip = zigzag_skip[info->type];

  /* loop through each component in the MDU */
  dc_huffindex = info->dc_huff_number;
  ac_huffindex = info->dc_huff_number;
  if (dct->sample_211) {
    dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[0]];
    dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[0]];

    compind = info->interleave_order[0];
    dct->compind = compind;
    dct->current_ci = &dct->components[compind];

    /* skip 4 y dct_blocks */
    if ( skip( dct , flptr , info) < 0 )
      return FALSE ;

    if ( skip( dct , flptr , info) < 0 )
      return FALSE ;

    if ( skip( dct , flptr , info) < 0 )
      return FALSE ;

    if ( skip( dct , flptr , info) < 0 )
      return FALSE ;

    /* skip 1 u dct_block */

    compind = info->interleave_order[1];
    dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[1]];
    dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[1]];
    dct->compind = compind;
    dct->current_ci = &dct->components[compind];
    if ( skip( dct , flptr , info) < 0 )
      return FALSE ;

    /* skip 1 u dct_block */

    dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[2]];
    dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[2]];
    compind = info->interleave_order[2];
    dct->compind = compind;
    dct->current_ci = &dct->components[compind];
    if ( skip( dct , flptr , info) < 0 )
      return FALSE ;
  } else {
    for ( k = 0 ; k < (int32)info->comp_in_scan ; k++ ) {
      dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[k]];
      dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[k]];
      compind = info->interleave_order[k];
      dct->compind = compind;
      dct->current_ci = &dct->components[compind] ;
      hsamples = dct->current_ci->num_hsamples ;
      vsamples = dct->current_ci->num_vsamples ;

      /* loop through the vertical blocks in the MDU */
      if (info->comp_in_scan == 1) {
        if (dct->rowsleft < vsamples * 8)
          vsamples = (dct->rowsleft + 7)/8;
        if (dct->endblock < hsamples)
          hsamples = dct->endblock;
      }

      for ( v = 0 ; v < vsamples ; v++ ) {

        /* loop through each horizontal block in the MDU */
        for ( h = 0 ; h < hsamples ; h++ ) {
          if ( skip( dct , flptr , info) < 0 )
            return FALSE ;
        }
      }
    }
  }
  return TRUE ;
}

static Bool skip_scan(FILELIST *flptr, DCTSTATE *dct)
{
  int32     MDUs_in_scan ;
  int32     mdu;

  HQASSERT(dct != NULL, "skip_scan - NULL filter context pointer.");

  /* reset the decoder */

  /* check Huffman and Quantization tables have been initialized */
  if (( ! dct->num_qtables ) ||
        ( ! dct->dc_huff.num )
)/*        || ( ! dct->ac_huff.num )) ### */
    return FALSE ;

  if ( ! dct->rows_in_MDU ) /* SOS not called */
    return FALSE ;

  /* initialise the DC prediction for each component */
  dct->current_row = 0 ;
  dct->currinfo->bbuf.nbits = 0 ;
  dct->currinfo->bbuf.data = 0 ;
  dct->next_restart_num = RST0_SYNC ;
  dct->restarts_to_go = dct->restart_interval ;

  MDUs_in_scan = (int32)dct->columns / (int32)dct->cols_in_MDU ;

  /* initialise decoder working vars */
  do{
    /* no of complete MDUs */
    dct->current_col = 0 ;

    dct->rowsleft = (int32)dct->rows - (int32)dct->current_row;

    if ( dct->rowsleft < (int32)dct->rows_in_MDU )
      dct->nrows = dct->rowsleft;
    else
      dct->nrows = (int32) dct->rows_in_MDU;

    dct->ncols = (int32) dct->cols_in_MDU;

    for ( mdu = 0 ; mdu < MDUs_in_scan ; mdu++ ) {

      if ( ! skip_MDU( dct , flptr) )
        return FALSE ;
      dct->current_col += dct->cols_in_MDU ;
    }

    /* handle the last incomplete MDU in the scanline , if there is one */

    if ( dct->current_col < dct->columns ) {
      if ( ! skip_MDU( dct , flptr ))
        return FALSE ;
      dct->ncols = dct->columns - dct->current_col;
    }

    dct->current_row += (uint16)dct->rows_in_MDU ;
  } while ( (int32)dct->current_row < (int32)dct->rows );

  return TRUE ;
}


/* for a progressive image, note the file position of the start of each scan
   after their respective SOS marker. Also note the Huffman table number
*/
static Bool find_multiple_scans(FILELIST *filter, DCTSTATE *dct)
{
  Hq32x2     SOSo;
  int32      code = 0;
  FILELIST * flptr;
  int32      Qis_spectral = FALSE;
  scaninfo * nextinfo = NULL;
  scaninfo * currinfo = NULL;
  int32      startmode;

  HQASSERT(dct->RSD, "no private RSD detected for progressive dct filter");

  flptr = theIUnderFile(filter);
  startmode = dct->mode;

  if (dct->mode == edctmode_multiscan)
    dct->currinfo->type = e_zigzag_baseline_noninterleaved;

  if ( (*theIMyFilePos(flptr))(flptr, &dct->currinfo->SOSo) == EOF )
    return (*theIFileLastError(flptr))(flptr) ;

  dct->currinfo->bbuf.nbits = 0;
  dct->currinfo->bbuf.data = 0;

  /* skip the DC coefficients */
  if ( ! skip_scan( flptr , dct ))
    return FALSE ;

  /* find the next SOS */
  while (get_marker_code( &code , flptr )) {
    switch (code) {
    case DHT:
      if (!decode_DHT( filter , dct ))
        return FALSE;
      break;
    case SOS:
      currinfo = ( scaninfo * )mm_alloc( mm_pool_temp ,
                                         sizeof( scaninfo ) ,
                                         MM_ALLOC_CLASS_DCT_BUFFER ) ;
      if (currinfo == NULL)
        return error_handler( VMERROR ) ;

      currinfo->next = NULL;

      dct->currinfo = currinfo;

      if ( ! decode_SOS( filter , dct ))
        return FALSE;

      /* determine what type of scan we have */
      Qis_spectral = (currinfo->Ah == 0);
      if (currinfo->Ss == 0) {
        /* DC coeff */
        if (Qis_spectral) {
          if (currinfo->Se == 63) {
            currinfo->type = e_zigzag_baseline_noninterleaved;
          } else {
            HQASSERT(currinfo->Se == 0,"bad range in progressive DCT");
            currinfo->type = e_zigzag_DC;
          }
        } else {
          currinfo->type = e_zigzag_DC_successive;
        }

      } else {
        /* AC coeff */
        currinfo->type = Qis_spectral ? e_zigzag_AC:e_zigzag_AC_successive;
      }

      /* note the current file position */
      if ( (*theIMyFilePos(flptr))(flptr, &SOSo) == EOF )
        return (*theIFileLastError(flptr))(flptr) ;

      currinfo->bbuf.nbits = 0;
      currinfo->bbuf.data = 0;
      currinfo->SOSo = SOSo;
      currinfo->a_bbuf.nbits = 0;
      currinfo->a_bbuf.data = 0;
      currinfo->cSOSo = SOSo;

      currinfo->EOBrun = 0;

      HQASSERT(currinfo->EOBrun == 0,"EOB count not zero");

      if (!Qis_spectral) {
        dct->successive = TRUE;
        if (dct->coeffs_base == NULL) {
          /* note that these coefficients will be freed when dct is destoyed */
          dct->coeffs_size = sizeof( int32 ) * 64;
          dct->coeffs_base = ( int32 * )mm_alloc( mm_pool_temp ,
                                                  dct->coeffs_size,
                                                  MM_ALLOC_CLASS_DCT_BUFFER ) ;
          if (dct->coeffs_base == NULL)
            return error_handler( VMERROR ) ;
          dct->coeffs = dct->coeffs_base;
        }
      }

      /* skip the entropy encoded scan data */
      if ( ! skip_scan( flptr , dct ))
        return FALSE ;

      if (dct->info == NULL) {
        dct->info = currinfo;
        nextinfo = currinfo;

        /* copy array reference to main state */
        dct->info = currinfo;
      } else {
        HQASSERT(nextinfo->next == NULL,"confused list pointer");
        nextinfo->next = currinfo;
        nextinfo = currinfo;
      }

      if (!Qis_spectral) {
        scaninfo * xinfo;
        for (xinfo = dct->info ;xinfo;xinfo = xinfo->next) {
          xinfo->a_bbuf.nbits = 0;
          xinfo->a_bbuf.data = 0;
          xinfo->bbuf.nbits = 0;
          xinfo->bbuf.data = 0;
          xinfo->cSOSo = xinfo->SOSo;
        }
      }

      HQASSERT(currinfo->EOBrun == 0,"EOB count not zero");
      /* remember where we are for the end */
      if ( (*theIMyFilePos(flptr))(flptr, &dct->lastpos) == EOF ) {
        return (*theIFileLastError(flptr))(flptr) ;
      }
      break;
    case EOI:
      break;
    default:
      HQFAIL("unexpected marker");
      return FALSE;
    }
  }

  if (Qis_spectral) {
    scaninfo * xinfo;
    for (xinfo = dct->info ;xinfo;xinfo = xinfo->next) {
      xinfo->a_bbuf.nbits = 0;
      xinfo->a_bbuf.data = 0;
      xinfo->bbuf.nbits = 0;
      xinfo->bbuf.data = 0;
      xinfo->cSOSo = xinfo->SOSo;
    }
  }

  dct->mode = startmode;

  return TRUE;
}

/**
 * When a scan has just one colorant its blocks are ordered in raster form
 * across the page despite it having vertical and horizontal dimensions
 * (eg 2x2).
 * HOWEVER, other components which are 1x1 still map on top of the 2x2 area.
 * This is a royal faff for our streamed decoder. To fix this we increase the
 * MDU size to the image width by the maximum block depth (2 in the example
 * above).
 * This requires some reallocation of block memory.
 */
static Bool multiscan_rejig_MDU(DCTSTATE *dct, Bool progressive)
{
  int32 i;
  COMPONENTINFO *ci;
  int32 blocksize;
  uint32 old_max_hsamples;

  old_max_hsamples = dct->max_hsamples;

  blocksize = 64 * dct->max_hsamples * dct->max_vsamples * sizeof( int32 );

  dct->rejig = FALSE;
  dct->blocks_in_MDU = 0 ;
  dct->max_hsamples = 0;

  for (ci = dct->components, i = 0 ; i < dct->colors ; i++, ci++ ) {
    mm_free( mm_pool_temp ,( mm_addr_t )ci->mdu_block, blocksize);
    ci->num_hsamples_old = ci->num_hsamples;

    ci->num_hsamples = dct->columns + (old_max_hsamples * 8) -1;
    ci->num_hsamples /= 8 * old_max_hsamples;
    ci->num_hsamples *= ci->num_hsamples_old;

    ci->coeffs_offset = dct->blocks_in_MDU * 64;

    if (ci->num_hsamples > dct->max_hsamples)
      dct->max_hsamples = ci->num_hsamples;

    dct->blocks_in_MDU += ci->num_hsamples * ci->num_vsamples ;
  }

  dct->subMDUs = (dct->max_hsamples + old_max_hsamples - 1)/old_max_hsamples;
  dct->cols_in_MDU = (8 * (int32)dct->max_hsamples) ;
  dct->non_integral_ratio = check_non_integral_ratios( dct ) ;
  blocksize = 64 * dct->max_hsamples * dct->max_vsamples * sizeof( int32 );

  for ( ci = dct->components, i = 0 ; i < dct->colors ; i++ , ci++ ) {
    ci->sample_size = ((int32) ci->h_skip * (int32) ci->v_skip) ;
    ci->sample_size2 = ((int32)ci->sample_size / 2) ;
    ci->mdu_block = (int32 *)mm_alloc(mm_pool_temp,
                                      blocksize,
                                      MM_ALLOC_CLASS_DCT_BUFFER);
    if (ci->mdu_block == NULL)
      return error_handler( VMERROR ) ;
  }

  if (progressive) {
    if (dct->coeffs_base) {
      mm_free( mm_pool_temp ,
             ( mm_addr_t )dct->coeffs_base ,
             dct->coeffs_size);
    }

    dct->coeffs_size = 64 * sizeof(int32) * dct->blocks_in_MDU;
    dct->coeffs_base = (int32 *)mm_alloc(mm_pool_temp,
                         dct->coeffs_size,
                         MM_ALLOC_CLASS_DCT_BUFFER);
    if (dct->coeffs_base == NULL)
      return error_handler( VMERROR ) ;
    dct->coeffs = dct->coeffs_base;
  }

  return TRUE ;
}





/* ----------------------------------------------------------------------------
   function:            decode_SOS        author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   A start-of-scan marker code has been found. This routines interprets its
   parameters and further initialises the DCTSTATE structure.
---------------------------------------------------------------------------- */
Bool decode_SOS(FILELIST *filter, DCTSTATE *dct)
{
  COMPONENTINFO *ci ;
  int32    component ;
  FILELIST *flptr ;
  int32    len , i , compind , id , tables ;
  int32   data;
  scaninfo * info = dct->currinfo;

  flptr = theIUnderFile( filter ) ;

  if (( len = dct_get_16bit_num( flptr )) == EOF )
    return FALSE ;
  if (( len -= 6 ) < 0 )
    return FALSE ;

  if (( compind = Getc( flptr )) == EOF )
    return FALSE ;
  if ( compind > 4 )
    return FALSE ;
  if (( compind * 2 ) != len )
    return FALSE ;

  info->comp_in_scan = compind ;

  component = 0;
  info->compbits = 0;
  for ( i = 0 ; i < compind ; i++ ) {
    if (( id = Getc( flptr )) == EOF )
      return FALSE ;
    if (( component = get_component( id , component , dct )) < 0 )
      return FALSE ;
    ci = &dct->components[component] ;

    if (( tables = Getc( flptr )) == EOF )
      return FALSE ;
    len = tables >> 4 ;
    if (len >= (int32)dct->dc_huff.num)
      return FALSE ;
    info->dc_huff_number[i] = dct->dc_huff.reindex[len];

    len = tables & 0x0f ;
/*     if (len >= (int32)dct->ac_huff.num)
      return FALSE ; ### */
    info->ac_huff_number[i] = dct->ac_huff.reindex[len];

    /* set up the interleave order */
    info->interleave_order[i] = component ;
    info->compbits |= (1 << component);
  }

  if (compind == 1) {
    dct->sample_211 = 0;

    ci = &dct->components[info->interleave_order[0]];

    if (dct->rejig && (ci->num_vsamples > 1)) {
      /* this component assumes a block layout but uses
         raster style layout. So rejig the MDU to be the
         width of the image.
      */
      switch (dct->mode) {
      case edctmode_baselinescan:
        dct->mode = edctmode_multiscan;
        if (!multiscan_rejig_MDU(dct, FALSE))
          return FALSE;
        break;
      case edctmode_progressivescan:
        if (!multiscan_rejig_MDU(dct, TRUE))
          return FALSE;
        break;
      }
    }
  }

  switch (dct->mode) {
  case edctmode_baselinescan:
    /* skip over 3 bytes - not used in baseline system */
    if (( Getc( flptr ) == EOF ) || ( Getc( flptr ) == EOF ) ||
        ( Getc( flptr ) == EOF ))
      return FALSE ;

    info->Ss = 0;
    info->Se = 63;
    info->Ah = 0;
    info->Al = 0;

    break;

  case edctmode_progressivescan:
  case edctmode_multiscan:
    if (( info->Ss = Getc( flptr )) == EOF ||
        ( info->Se = Getc( flptr )) == EOF ||
        ( data = Getc( flptr )) == EOF )
      return FALSE ;

    info->Ah = data >> 4;
    info->Al = data & 0x0f;

    /* find filepositions for other scans
       (involves a single level recursive call to decode_SOS) */

    /* if underlying file is not an RSD then insert a private
       RSD as the underlying file. The dct code is responsible
       for removing this when closing. */
    if (!dct->RSD) {
      Hq32x2  startpos;
      Bool    priv_file = FALSE;

      if ( !isIRSDFilter(flptr) ) {
        corecontext_t *context = get_core_context_interp();
        Bool global;
        Bool g2 = (theISaveLevel(flptr) & GLOBMASK) == ISGLOBAL ;
        Bool l_ret;
        FILELIST *rsdFilter;

        /* allocate memory globally so as not to fox PDF filters with a PS RSD
           we pretend flptr is global for the moment as child objects inherit
           the alloc mode */
        global = setglallocmode(context, TRUE ) ;
        theISaveLevel(flptr) = (theISaveLevel(flptr) & ~GLOBMASK) | ISGLOBAL;

        l_ret = filter_layer(flptr,
                             NAME_AND_LENGTH("ReusableStreamDecode"),
                             NULL, &rsdFilter);

        /* restore allocmode (in file too) */
        theISaveLevel(flptr) &= ~GLOBMASK ;
        if (g2)
          theISaveLevel(flptr) |= ISGLOBAL;
        else
          theISaveLevel(flptr) |= ISLOCAL;
        setglallocmode(context, global ) ;

        if (!l_ret)
          return FALSE;

        flptr = rsdFilter;
        theIUnderFile(filter) = flptr ;
        priv_file = TRUE;
      }
      dct->RSD = TRUE ;

      /* note start position of data. we will return here */
      if ( (*theIMyFilePos(flptr))(flptr, &startpos) == EOF ) {
        dct->RSD = priv_file ;
        return (*theIFileLastError(flptr))(flptr);
      }

      /* parse all other scans and note their start positions
       * (single level recursive)
       */
      if (!find_multiple_scans( filter , dct )) {
        dct->RSD = priv_file ;
        return FALSE;
      }

      /* rewind the file back to where we left off */
      if ( (*theIMyResetFile(flptr))(flptr) == EOF ||
           (*theIMySetFilePos(flptr))(flptr, &startpos) == EOF ) {
        dct->RSD = priv_file ;
        return (*theIFileLastError( flptr ))( flptr );
      }

      dct->RSD = priv_file ;
    }
    break;
  default:
    HQFAIL("Bad JPEG mode");
    break;
  }

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            decode_DRI        author:              Luke Tunmer
   creation date:       25-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Found a define-restart-interval marker. Extract the number and place into
   the DCTSTATE structure.
---------------------------------------------------------------------------- */


Bool decode_DRI(FILELIST *flptr, DCTSTATE *dctstate)
{
  int32 len ;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;

  if ( len != 4 )
    return FALSE ;
  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;

  dctstate->restart_interval = dctstate->restarts_to_go = len ;
  dctstate->next_restart_num = RST0_SYNC ;

  return TRUE ;
}

Bool skip_DRI( FILELIST *flptr )
{
  int32 len ;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;

  if ( len != 4 )
    return FALSE ;
  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            process_restart   author:              Luke Tunmer
   creation date:       25-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Skip to the next restart marker. Return FALSE if it is the wrong one,
   or if there is a problem with the underlying file. Reset the
   decoder's DC predictions.
---------------------------------------------------------------------------- */
static Bool process_restart(register DCTSTATE *dct, FILELIST *flptr)
{
  int32 c ;
  int32 next_restart_num ;

  do {
    do {                        /* skip any non-FF bytes */
      if (( c = Getc( flptr )) == EOF )
        return FALSE ;
    } while (c != 0xFF);
    do {                        /* skip any duplicate FFs */
      if (( c = Getc( flptr )) == EOF )
        return FALSE ;
    } while ( c == 0xFF );
  } while ( c == 0 );             /* repeat if it was a stuffed FF/00 */

  next_restart_num = dct->next_restart_num ;
  /* We sync our RST0 seqence checking to the first one encountered. */
  if ( next_restart_num == RST0_SYNC ) {
    next_restart_num = c - RST0 ;
    if ( next_restart_num < 0 || next_restart_num > 7 )
      return FALSE ;
  }
  else {
    if ( c != ( RST0 + next_restart_num ))
      return FALSE ;
  }

  dct->currinfo->bbuf.nbits = 0 ;
  dct->currinfo->bbuf.data = 0xff ;
  /* Re-initialize DC predictions to 0 */
  for (c = 0; c < (int32)dct->currinfo->comp_in_scan; c++ )
    dct->components[c].dc_prediction = 0 ;

  /* Update restart state */
  dct->restarts_to_go = dct->restart_interval;
  dct->next_restart_num = (( next_restart_num + 1 ) & 7) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            decode_DQT        author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   A define-quantization-table marker code has been found. This routine
   reads in the bytes and initialises the appropriate quant table in the
   DCTSTATE structure.
---------------------------------------------------------------------------- */
Bool decode_DQT(FILELIST *filter, DCTSTATE *dctstate)
{
  register FILELIST *flptr ;
  register int32 c ;
  int32    i , len , tablenum, precision, num_qtables ;
  uint32   *qtable, *p;

  flptr = theIUnderFile( filter ) ;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2 ;

  num_qtables = 0 ;
  while (len > 0) {

    if (( tablenum = Getc( flptr )) == EOF )
      return error_handler( IOERROR ) ;

    precision = tablenum >> 4;
    tablenum = tablenum & 0x0F;
    if ( tablenum > 3 )
      return error_handler( IOERROR ) ;

    switch (precision) {
    case 0:
      len -= 65;
      break;
    case 1:
      len -= 129;
      break;
    default:
      HQFAIL("Unsupported precision value in DQT");
      return error_handler( RANGECHECK );
    }

    if (len < 0)
      return error_handler( IOERROR ) ;

    /* Allocate space for the table then read it in. */
    qtable = ( uint32 * )mm_alloc( mm_pool_temp ,
                                   64 * sizeof( uint32 ) ,
                                   MM_ALLOC_CLASS_DCT_QUANT ) ;
    if ( qtable == NULL )
      return error_handler( VMERROR ) ;

    dctstate->quanttables[ tablenum ] = qtable ;
    p = qtable ;
    for ( i = 0 ; i < 64 ; i++ ) {
      if ((c = Getc( flptr )) == EOF )
        return error_handler( IOERROR ) ;

      if (precision == 1) {  /* table has 16 bit values */
        int32 c2 = Getc(flptr);
        if (c2 == EOF)
          return error_handler( IOERROR );
        c = (c << 8) | c2;
      }
      *p++ = c ;
    }

    num_qtables++ ;
  }

  dctstate->num_qtables += num_qtables ;

  return TRUE ;
}


static Bool reinit_hufftable_group(huffgroup_t * group,
                                   HUFFTABLE **huff,
                                   int32 table)
{
  if (group->last >= group->maxnum) {
    HUFFTABLE * temphuff;
    int32 newtotal;

    newtotal = group->maxnum + MAXHUFFTABLES;
    temphuff = mm_alloc( mm_pool_temp ,
                         sizeof(HUFFTABLE) * newtotal ,
                         MM_ALLOC_CLASS_DCT_BUFFER ) ;
    if (temphuff == NULL)
      return error_handler( VMERROR ) ;
    HqMemCpy(temphuff, group->tables, sizeof(HUFFTABLE)*group->maxnum);
    HqMemZero(temphuff + group->maxnum, sizeof(HUFFTABLE)*MAXHUFFTABLES);
    mm_free( mm_pool_temp ,
       ( mm_addr_t )group->tables ,
       ( mm_size_t )sizeof( HUFFTABLE ) * group->maxnum) ;

    group->maxnum = newtotal;
    group->tables = temphuff;
  }
  *huff = &group->tables[ group->last ] ;
  group->reindex[table] = group->last;
  group->last++;

  return TRUE;
}


/* ----------------------------------------------------------------------------
   function:            decode_DHT        author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   A define-huffman-table marker code has been found. This routine reads in
   all the huff tables defined by the following bytes, and initialises the
   appropriate tables in the DCTSTATE structure.
---------------------------------------------------------------------------- */
Bool decode_DHT(FILELIST *filter, DCTSTATE *dctstate)
{
  FILELIST *flptr ;
  HUFFTABLE *huff ;
  int32 len , nbytes ;
  int32 table , c , i ;
  uint32 *ptable , *p , buff[16] ;
  huffgroup_t * huffgroup;
  int32 ac;

  flptr = theIUnderFile( filter ) ;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2 ;

  do {
    if ( ( len -= 17 ) < 0 )
      return error_handler( IOERROR ) ;
    if (( table = Getc( flptr )) == EOF )
      return error_handler( IOERROR ) ;
    if ((table & 0x0f) >= dctstate->max_hufftables ||
        (table >> 4) > 1 )
      return error_handler( IOERROR ) ;
    ac = table & 0xf0;
    table &= 0xf;
    if ( ac ) /* AC */
      huffgroup = &dctstate->ac_huff;
    else
      huffgroup = &dctstate->dc_huff;

    huff = &huffgroup->tables[table] ;

    nbytes = 0 ;
    /* read the 16 bit-length bytes */
    for ( i = 0 ; i < 16 ; i++ ) {
      if (( c = Getc( flptr )) == EOF )
        return error_handler( IOERROR ) ;
      buff[i] = c ;
      nbytes += c ;
    }
    if (( len -= nbytes ) < 0 )
      return error_handler( IOERROR ) ;

    if (huff->encoded_hufftable) {
      if (dctstate->mode != edctmode_baselinescan) {
        if (!reinit_hufftable_group(huffgroup, &huff, table))
            return FALSE;
      } else {
        free_huff_table(huff);
        huffgroup->num-- ;
      }
    }
    ptable = ( uint32 * )mm_alloc( mm_pool_temp ,
                                   ( nbytes + 16 )*sizeof( uint32 ) ,
                                   MM_ALLOC_CLASS_HUFF_TABLE ) ;
    if ( ptable == NULL )
      return error_handler( VMERROR ) ;

    huff->encoded_hufftable = ptable ;
    huff->encoded_length = nbytes + 16 ;

    HqMemCpy( ptable , buff , 16 * sizeof(uint32)) ;
    p = ptable + 16 ;

    for ( i = 0 ; i < nbytes ; i++ ) {
      if (( c = Getc( flptr )) == EOF )
        return error_handler( IOERROR ) ;
      *(p++) = c ;
    }
    if ( ! make_huffman_table( huff , !ac , FALSE ))
      return FALSE;
    huffgroup->num++ ;
  } while ( len ) ;

  return TRUE ;
}

/*
   Decode a single baseline scan. (This code is basically the original
   baseline DCT code).

   Call the appropriate routines to decode the number of blocks in an MDU.
   Each block must be dequantized, passed through the IDCT, and then unpacked
   into the filter's buffer.
*/
static Bool decode_MDU_normal(DCTSTATE *dct, FILELIST *flptr)
{
  int32 dct_block[64];
  int32 k , hsamples, vsamples;
  int32 *r_ptr, *rc_ptr ;
  COMPONENTINFO * ci;
  register int32 i,j, h_shift, v_shift,  h_size, v_size;
  register int32 *dst, *src;
  scaninfo * info;
  uint32 * dc_huffindex;
  uint32 * ac_huffindex;

  if ( dct->restart_interval ) {
    if ( dct->restarts_to_go == 0 ) {
      if ( ! process_restart( dct , flptr ))
        return FALSE ;
    }
    dct->restarts_to_go-- ;
  }

  info = dct->currinfo;
  dc_huffindex = info->dc_huff_number;
  ac_huffindex = info->ac_huff_number;
  /* loop through each component in the MDU */
  if (dct->sample_211) {
    ci = &dct->components[0];
    dct->current_ci = ci;
    r_ptr = ci->mdu_block;

    /* decode 4 y dct_blocks */
    dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[0]];
    dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[0]];
    dct->compind = 0;
    HQASSERT(ci->num_hsamples == 2,"bad val");
    HQASSERT(ci->num_vsamples == 2,"bad val");
    dct->v = 0;
    dct->h = 0;
    if ( decode_normal_zigzag( dct , flptr , info) < 0 )
      return FALSE ;
    unfix_tile( r_ptr, 8);
    if ( dct->dct_status == GOT_EOI )
      return TRUE;

    dct->h = 1;
    if ( decode_normal_zigzag( dct , flptr , info) < 0 )
      return FALSE ;
    unfix_tile( r_ptr+8, 8);
    if ( dct->dct_status == GOT_EOI )
      return TRUE;

    dct->v = 1;
    dct->h = 0;
    if ( decode_normal_zigzag( dct , flptr , info) < 0 )
     return FALSE ;
    unfix_tile( r_ptr+128, 8);
    if ( dct->dct_status == GOT_EOI )
      return TRUE;

    dct->h = 1;
    if ( decode_normal_zigzag( dct , flptr , info) < 0 )
      return FALSE ;
    unfix_tile( r_ptr+136, 8);
    if ( dct->dct_status == GOT_EOI )
      return TRUE;

    /* decode 1 u dct_block */

    dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[1]];
    dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[1]];
    ci = &dct->components[1];
    dct->current_ci = ci;
    dct->v = 0;
    dct->h = 0;
    dct->compind = 1;
    HQASSERT(ci->num_hsamples == 1,"bad val");
    HQASSERT(ci->num_vsamples == 1,"bad val");
    r_ptr = ci->mdu_block;
    if ( decode_normal_zigzag( dct , flptr , info) < 0 )
      return FALSE ;
    unfix_tile( r_ptr, 0 );
    if ( dct->dct_status == GOT_EOI )
      return TRUE;

    /* decode 1 u dct_block */

    dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[2]];
    dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[2]];
    ci = &dct->components[2];
    dct->current_ci = ci;
    dct->compind = 2;
    HQASSERT(ci->num_hsamples == 1,"bad val");
    HQASSERT(ci->num_vsamples == 1,"bad val");
    r_ptr = ci->mdu_block;
    if ( decode_normal_zigzag( dct , flptr , info) < 0 )
      return FALSE ;
    unfix_tile( r_ptr, 0 );
    if ( dct->dct_status == GOT_EOI )
      return TRUE;

  } else {
    for ( k = 0 ; k < (int32)info->comp_in_scan ; k++ ) {
      dct->dc_huff.current = &dct->dc_huff.tables[dc_huffindex[k]];
      dct->ac_huff.current = &dct->ac_huff.tables[ac_huffindex[k]];
      dct->compind = info->interleave_order[k];
      ci = &dct->components[dct->compind] ;
      dct->current_ci = ci;
      hsamples = ci->num_hsamples;
      vsamples = ci->num_vsamples;

      r_ptr = ci->mdu_block;
      h_size = 8 * ci->h_skip;
      v_size = 8 * ci->v_skip;

      if (h_size==32) {
        h_shift = 2;
      } else {
        if (h_size==16)
          h_shift = 1;
        else
          h_shift = 0;
      }

      if (v_size==32) {
        v_shift = 2;
      } else {
        if (v_size==16)
          v_shift = 1;
        else
          v_shift = 0;
      }

      /* loop through the vertical blocks in the MDU */
      for ( dct->v = 0 ; dct->v < vsamples ; dct->v++ ) {
        rc_ptr = r_ptr;

        /* loop through each horizontal block in the MDU */
        for ( dct->h = 0 ; dct->h < hsamples ; dct->h++ ) {
          if ( decode_normal_zigzag( dct , flptr , info) < 0 )
            return FALSE ;
          unfix_tile( (int32 *)dct_block, 0 );
          if ( dct->dct_status == GOT_EOI )
            return TRUE;


          /* up sample by v_skip * h_skip */
          /* v_shift and h_shift downsample to dct_block */

          dst = rc_ptr;
          src = (int32 *)dct_block;
          j = 0;
          while (j < v_size) {
            for (i=0; i< h_size; i++)
              *(dst + i) =  *(src + (i>>h_shift));
            j++;
            src =  (int32 *)dct_block + ((j >> v_shift) << 3);
            dst += dct->cols_in_MDU;
          }
          rc_ptr += h_size;
        } /* end of up sampling */

        r_ptr += dct->cols_in_MDU * v_size;
      }
    }
  }
  return TRUE ;
}

/**
 * Decode progressive scans assume that all progressive blocks are in the
 * same order
 */
static Bool decode_MDU_progressive(DCTSTATE *dct, FILELIST *flptr)
{
  int32 dct_block[64];
  int32 k , hsamples, vsamples;
  int32 *r_ptr, *rc_ptr ;
  COMPONENTINFO * ci;
  scaninfo * info = dct->currinfo;
  register int32 i,j, h_shift, v_shift,  h_size, v_size;
  register int32 *dst, *src;

  if ( dct->restart_interval ) {
    if ( dct->restarts_to_go == 0 ) {
      if ( ! process_restart( dct , flptr ))
        return FALSE ;
    }
    dct->restarts_to_go-- ;
  }

  HQASSERT(dct->sample_211 == 0,"unlikely to have just composite dc coeffs.");
  /* loop through each component in the MDU */
  for ( k = 0 ; k < (int32)info->comp_in_scan ; k++ ) {
    dct->dc_huff.current = &dct->dc_huff.tables[info->dc_huff_number[k]];
    dct->ac_huff.current = &dct->ac_huff.tables[info->ac_huff_number[k]];
    dct->compind = info->interleave_order[k];
    dct->current_ci = ci = &dct->components[dct->compind];
    hsamples = ci->num_hsamples;
    vsamples = ci->num_vsamples;

    r_ptr = ci->mdu_block;
    h_size = 8 * ci->h_skip;
    v_size = 8 * ci->v_skip;

    if (h_size==32) {
      h_shift = 2;
    } else {
      if (h_size==16)
        h_shift = 1;
      else
        h_shift = 0;
    }

    if (v_size==32) {
      v_shift = 2;
    } else {
      if (v_size==16)
        v_shift = 1;
      else
        v_shift = 0;
    }

    /* loop through the vertical blocks in the MDU */
    for ( dct->v = 0 ; dct->v < vsamples ; dct->v++ ) {
      rc_ptr = r_ptr;

      /* loop through each horizontal block in the MDU */
      for ( dct->h = 0 ; dct->h < hsamples ; dct->h++ ) {
        if (! decode_progressive( dct , flptr))
          return FALSE ;
        unfix_tile( (int32 *)dct_block, 0 );


        /* up sample by v_skip * h_skip */
        /* v_shift and h_shift downsample to dct_block */

        dst = rc_ptr;
        src = (int32 *)dct_block;
        j = 0;
        while (j < v_size) {
          for (i=0; i< h_size; i++)
            *(dst + i) =  *(src + (i>>h_shift));
          j++;
          src =  (int32 *)dct_block + ((j >> v_shift) << 3);
          dst += dct->cols_in_MDU;
        }
        rc_ptr += h_size;
      } /* end of up sampling */

      r_ptr += dct->cols_in_MDU * v_size;
    }
  }
  return TRUE ;
}

/* then take each block in a rejigged MDU then IQUANT, IDCT and output */
static void decode_MDU_prog_rejigged_inverse( DCTSTATE *dct )
{
  int32 i,k,l,offset,v,h,j,h_shift,v_shift;
  int32 hsamples,vsamples,h_size,v_size;
  int32 *r_ptr, *rc_ptr ;
  COMPONENTINFO * ci;
  QUANTTABLE qtable;
  int32 dct_block[64];
  int32 *dst, *src;

  ci = dct->components;

  /* then take each block and IQUANT, IDCT and output */
  for ( k = 0 ; k < (int32)dct->colors; k++, ci++ ) {

    qtable = dct->quanttables[(int32) ci->qtable_number];
    hsamples = ci->num_hsamples_old;
    vsamples = ci->num_vsamples;

    h_size = 8 * ci->h_skip;
    v_size = 8 * ci->v_skip;

    if (h_size==32) {
      h_shift = 2;
    } else {
      if (h_size==16)
        h_shift = 1;
      else
        h_shift = 0;
    }

    if (v_size==32) {
      v_shift = 2;
    } else {
      if (v_size==16)
        v_shift = 1;
      else
        v_shift = 0;
    }

    offset = 0;

    for (l = 0; l < dct->subMDUs;l++) {
      r_ptr = ci->mdu_block + offset;

      /* loop through the vertical blocks in the MDU */
      for ( v = 0 ; v < vsamples ; v++ ) {
        rc_ptr = r_ptr;
        /* loop through each horizontal block in the MDU */
        for ( h = 0 ; h < hsamples ; h++ ) {

          dct->coeffs = dct->coeffs_base +
                      ci->coeffs_offset +
                      (64 * ((l * hsamples) + h + (v * ci->num_hsamples) ) );

          for (i = 0;i < 64;i++)  {
            splat_array[i](dct->coeffs[i]*qtable[i]);
          }
          unfix_tile( (int32 *)dct_block, 0 );

          /* up sample by v_skip * h_skip */
          /* v_shift and h_shift downsample to dct_block */
          dst = rc_ptr;
          src = (int32 *)dct_block;
          j = 0;
          while (j < v_size) {
            for (i=0; i< h_size; i++)
              *(dst + i) =  *(src + (i>>h_shift));
            j++;
            src =  (int32 *)dct_block + ((j >> v_shift) << 3);
            dst += dct->cols_in_MDU;
          }
          rc_ptr += h_size;
        } /* end of up sampling */

        r_ptr += dct->cols_in_MDU * v_size;
      }
      offset += h_size * hsamples;
    }
  }
}


/**
 * Decode progressive scans no assumption is made that all progresssive blocks
 * are in the same order. The MDU is widened (rejigged) to the page width and
 * appropriate coeffs are put in the right place. The entire MDU is then
 * IDCTed etc at the end.
 */
static Bool decode_MDU_progressive_rejigged(DCTSTATE *dct, FILELIST *flptr)
{
  int32 hsamples, vsamples;
  COMPONENTINFO * ci;
  scaninfo * info;
  int32 k,l,h,v;

  if ( dct->restart_interval ) {
    if ( dct->restarts_to_go == 0 ) {
      if ( ! process_restart( dct , flptr ))
        return FALSE ;
    }
    dct->restarts_to_go-- ;
  }


  for (info = dct->currinfo;info;info = info->next) {

    /* move to current position in this scan */
    if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

    if ((*theIMySetFilePos( flptr ))( flptr , &info->SOSo ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

    /* read a full MDU in from each scan */

    if (info->comp_in_scan > 1) {
      /* loop through the original "sub" MDUs within the rejigged MDU */

      for (l = 0; l < dct->subMDUs;l++) {

        for ( k = 0 ; k < (int32)info->comp_in_scan; k++ ) {
          dct->dc_huff.current = &dct->dc_huff.tables[info->dc_huff_number[k]];
          dct->ac_huff.current = &dct->ac_huff.tables[info->ac_huff_number[k]];
          dct->compind = info->interleave_order[k];
          dct->current_ci = ci = &dct->components[dct->compind] ;

          vsamples = ci->num_vsamples;
          hsamples = ci->num_hsamples_old;
          dct->coeffs = dct->coeffs_base + ci->coeffs_offset +
                        (64 * l * hsamples);

          /* composite MDUs are left intact */
          /* loop through the vertical blocks in the MDU */
          for ( v = 0 ; v < vsamples ; v++ ) {
            /* loop through each horizontal block in the MDU */
            for ( h = 0 ; h < hsamples ; h++ ) {

              if ( zigzag_decodes[info->type](dct,flptr,info) < 0 )
                return FALSE;
              dct->coeffs += 64;
            }

            dct->coeffs += 64 * (ci->num_hsamples - hsamples);
          }
        }
      }
    } else {
      int32 blocksize;
      int32 currcols,cols;

      dct->dc_huff.current = &dct->dc_huff.tables[info->dc_huff_number[0]];
      dct->ac_huff.current = &dct->ac_huff.tables[info->ac_huff_number[0]];
      dct->compind = info->interleave_order[0];
      dct->current_ci = ci = &dct->components[dct->compind] ;

      dct->coeffs = dct->coeffs_base + ci->coeffs_offset;
      blocksize = ci->num_vsamples == 1? 16:8;
      vsamples = ci->num_vsamples;
      hsamples = ci->num_hsamples_old * dct->subMDUs;
      cols = dct->columns;

      if (dct->maxrows < (int32)ci->num_vsamples)
        vsamples = dct->maxrows;

      for (v = 0;v < vsamples; v++) {
        currcols = 0;
        for (l = 0; l < hsamples; l++) {

          if (currcols < cols) {
            if ( zigzag_decodes[info->type](dct,flptr,info) < 0 )
              return FALSE;
          }

          currcols += blocksize;
          dct->coeffs += 64;
        }
      }
    }

    if ( (*theIMyFilePos(flptr))(flptr, &info->SOSo) == EOF )
      return (*theIFileLastError(flptr))(flptr) ;
  }

  decode_MDU_prog_rejigged_inverse( dct );

  return TRUE ;
}



/**
 * Decode scans of a multiscan "baseline" type file. MDU has been widened
 * (rejigged) to be the image width due to block layouts being raster for
 * single component scans
 */
static Bool decode_MDU_noninterleaved(DCTSTATE *dct, FILELIST *flptr)
{
  int32 dct_block[64];
  int32 hsamples,vsamples;
  int32 *r_ptr, *rc_ptr ;
  COMPONENTINFO * ci;
  scaninfo * info;
  register int32 i,j, h_shift, v_shift,  h_size, v_size;
  register int32 *dst, *src;
  int32 h,v;

  if ( dct->restart_interval ) {
    if ( dct->restarts_to_go == 0 ) {
      if ( ! process_restart( dct , flptr ))
        return FALSE ;
    }
    dct->restarts_to_go-- ;
  }

  for (info = dct->currinfo;info;info = info->next) {
    HQASSERT(info->comp_in_scan == 1,"unexpected composite scan found");
    dct->compind = info->interleave_order[0];
    ci = &dct->components[dct->compind] ;
    dct->current_ci = ci;
    hsamples = ci->num_hsamples;
    if (hsamples > dct->endblock)
      hsamples = dct->endblock;

    if (dct->maxrows > (int32)ci->num_vsamples)
      vsamples = ci->num_vsamples;
    else
      vsamples = dct->maxrows;

    h_size = 8 * ci->h_skip;
    v_size = 8 * ci->v_skip;

    if (h_size==32) {
      h_shift = 2;
    } else {
      if (h_size==16)
        h_shift = 1;
      else
        h_shift = 0;
    }

    if (v_size==32) {
      v_shift = 2;
    } else {
      if (v_size==16)
        v_shift = 1;
      else
        v_shift = 0;
    }

    r_ptr = ci->mdu_block;

    /* loop through the vertical blocks in the MDU */
    for ( v = 0 ; v < vsamples ; v++ ) {
      rc_ptr = r_ptr;
      /* loop through each horizontal block in the MDU */
      for ( h = 0 ; h < hsamples ; h++ ) {
        if (! decode_noninterleaved( dct , flptr, info))
          return FALSE ;
        unfix_tile( (int32 *)dct_block, 0 );
        /* up sample by v_skip * h_skip */
        /* v_shift and h_shift downsample to dct_block */

        dst = rc_ptr;
        src = (int32 *)dct_block;
        j = 0;
        while (j < v_size) {
          for (i=0; i< h_size; i++)
            *(dst + i) =  *(src + (i>>h_shift));
          j++;
          src =  (int32 *)dct_block + ((j >> v_shift) << 3);
          dst += dct->cols_in_MDU;
        }
        rc_ptr += h_size;
      } /* end of up sampling */
      r_ptr += h_size * v_size * ci->num_hsamples;
    }
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            decode_scan       author:              Luke Tunmer
   creation date:       06-Sep-1991       last modification:   ##-###-####
   arguments:
   description:

   Decode enough MDU's to fill one horizontal scan. This should fill
   the filter's buffer if it is not the last row in the image.

   Reset is true if this is the start of the image.

Y 4Aug97 removed some unused variables.
---------------------------------------------------------------------------- */
Bool decode_scan(FILELIST *filter,
                 DCTSTATE *dct,
                 int32    *ret_bytes,
                 Bool     reset)
{
  FILELIST* flptr;
  int32     MDUs_in_scan;
  int32     mdu;
  int32 (*decode_MDU)( DCTSTATE *dct , FILELIST *flptr );
  scaninfo * info;

  uint8*    pFilterBuffer =  theIBuffer( filter );

  HQASSERT(dct != NULL, "decode_scan - NULL filter context pointer.");

  flptr = theIUnderFile( filter );

  dct->currinfo = &dct->default_info;
  info = dct->currinfo;

  decode_MDU = decode_MDU_normal;

  switch (dct->mode) {
  case edctmode_progressivescan:
    info->next = dct->info;
    if (!dct->rejig) {
      decode_MDU = decode_MDU_progressive_rejigged;
      dct->successive = TRUE;
    } else {
      decode_MDU = decode_MDU_progressive;
    }

    HQASSERT(info->Ss == 0,"Not DC scan");
    HQASSERT(info->Ah == 0,"Not DC scan");
    info->type = e_zigzag_DC;
    break;
  case edctmode_multiscan:
    decode_MDU = decode_MDU_noninterleaved;
    info->next = dct->info;
    break;
  case edctmode_baselinescan:
    decode_MDU = decode_MDU_normal;
    break;
  default:
    HQFAIL("unhandled scan mode");
  }

  if ( reset ) { /* reset the decoder */
    /* check Huffman and Quantization tables have been initialized */
    if (( ! dct->num_qtables ) ||
        ( ! dct->dc_huff.num ) || ( ! dct->ac_huff.num )) {
      return error_handler( IOERROR ) ;
    }

    if ( ! dct->rows_in_MDU ) { /* SOS not called */
      return error_handler( IOERROR ) ;
    }
    /* initialise the DC prediction for each component */
    dct->components[0].dc_prediction = 0 ;
    dct->components[1].dc_prediction = 0 ;
    dct->components[2].dc_prediction = 0 ;
    dct->components[3].dc_prediction = 0 ;
    dct->current_row = 0 ;
    info->bbuf.nbits = 0 ;
    info->bbuf.data = 0 ;
    dct->next_restart_num = RST0_SYNC ;
    dct->restarts_to_go = dct->restart_interval ;
  }

  /* no of complete MDUs */
  MDUs_in_scan = (int32)dct->columns/(int32)dct->cols_in_MDU ;
  dct->current_col = 0 ;

  dct->rowsleft = (int32)dct->rows - (int32)dct->current_row;
  dct->maxrows = (7 + dct->rowsleft)/8;
  if ( dct->rowsleft < (int32)dct->rows_in_MDU )
    dct->nrows = dct->rowsleft;
  else
    dct->nrows = (int32) dct->rows_in_MDU;

  dct->ncols = (int32) dct->cols_in_MDU;

  for ( mdu = 0 ; mdu < MDUs_in_scan ; mdu++ ) {

    if ( ! decode_MDU( dct , flptr) ) {
      return error_handler( IOERROR );
    }

    color_transform_MDU(dct, pFilterBuffer);
    dct->current_col += dct->cols_in_MDU;
    pFilterBuffer += dct->cols_in_MDU * dct->colors;
  }

  if ( dct->dct_status == GOT_EOI )
    return TRUE;

  /* handle the last incomplete MDU in the scanline , if there is one */
  if ( dct->current_col < dct->columns ) {
    if ( ! decode_MDU( dct , flptr )) {
      return error_handler( IOERROR ) ;
    }
    dct->ncols = dct->columns - dct->current_col;
    color_transform_MDU(dct, pFilterBuffer);
  }

  dct->current_row = (uint16)(dct->current_row + dct->rows_in_MDU);

  if ( (int32)dct->current_row >= (int32)dct->rows ) {
    /* come to the end of the image */
    dct->dct_status = EXPECTING_EOI ;
    *ret_bytes = dct->bytes_in_scanline *
      ( (int32) dct->rows_in_MDU + dct->rows - (int32) dct->current_row );

    if (dct->RSD) {
      /* let the RSD filter suck up the last of the file */
      int32 code = 0;

      /*move to the end of the last scan */
      if ( (*theIMyResetFile(flptr))(flptr) == EOF ||
           (*theIMySetFilePos(flptr))(flptr, &dct->lastpos) == EOF )
        return (*theIFileLastError( flptr ))( flptr ) ;

      if ( ! get_marker_code( &code , flptr ))
        return error_handler( IOERROR ) ;
      if ( code != EOI ) {
        if (code != SOS)
          return error_handler( IOERROR ) ;
      }
    }
  } else {
    *ret_bytes = dct->bytes_in_scanline * (int32) dct->rows_in_MDU ;
  }

  return TRUE ;
}



static void color_transform_MDU(register DCTSTATE *dct, register uint8 *buffer)
{
  HQASSERT(!dct->sample_211 || (dct->colortransform == 1) ||
                               (dct->colortransform == 2),
           "Inconsistent value of sample_211");

  switch ( dct->colortransform ) {
  case 1:
  case 2:  /* 2 is undocumented but seems to mean the same as 1 in all
            * examples we have seen.
            * Storm tell us they use 2 for CMYK, reserving 1 for RGB.
            * Earlier, we assumed 2 meant the CMY should be inverted,
            * but this was wrong: we were misled by a bug in Storm's
            * EPS JPEG files, which they have now corrected.
            *
            * The IJG JPEG6a example code has this comment:
            *
            * We write the color transform byte as 1 if the JPEG color
            * space is YCbCr, 2 if it's YCCK, 0 otherwise.  Adobe's
            * definition has to do with whether the encoder performed
            * a transformation, which is pretty useless.
            */
    if ( dct->colors == 3) {
      if (dct->sample_211)
        transfer211_YUV_to_RGB( dct, buffer);
      else
        transfer_YUV_to_RGB( dct, buffer);
    }
    else if ( dct->colors == 4 )
      transfer_YUVK_to_CMYK( dct,buffer);
    else
      transfer_direct_colors(dct, buffer);
    break;
  default:
    transfer_direct_colors( dct, buffer);

    /* no color transform. This case includes
     *  gray and rgb  and also rgbk images
     */

    break;
  }
  return;
}

static void transfer_direct_colors( register DCTSTATE *dct,
                                    uint8 *buffer)
{

  int32 *y_block, *u_block, *v_block, *k_block;
  register int32 i,j;
  register int32 nrows, ncols;
  register int32 *y_ptr, *u_ptr, *v_ptr, *k_ptr;
  register uint8 *rc_ptr;
  register int32 t;

  nrows = dct->nrows;
  ncols = dct->ncols;

  switch (dct->colors) {
  case 1:
    y_block = dct->components[0].mdu_block;
    for (i=0; i< nrows; i++) {
      rc_ptr = buffer;
      y_ptr = y_block;

      for (j=0; j<ncols; j++) {
        t = (*y_ptr++) + 128;
        RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;
      }
      buffer += dct->bytes_in_scanline;
      y_block += dct->cols_in_MDU;
    }
    break;
  case 2:
    y_block = dct->components[0].mdu_block;
    u_block = dct->components[1].mdu_block;
    for (i= 0; i< nrows; i++) {
      rc_ptr = buffer;
      y_ptr = y_block; u_ptr = u_block;
      for ( j = 0 ; j < ncols ; j++ ) {
        t = (*y_ptr++) + 128;
        RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;

        t = (*u_ptr++) + 128;
        RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;
      }
      y_block += dct->cols_in_MDU;
      u_block += dct->cols_in_MDU;
      buffer += dct->bytes_in_scanline;
    }
    break;
  case 3:
    y_block = dct->components[0].mdu_block;
    u_block = dct->components[1].mdu_block;
    v_block = dct->components[2].mdu_block;
    /* really rgb blocks, but never mind */
    for (i= 0; i< nrows; i++) {
      rc_ptr = buffer;
      y_ptr = y_block; u_ptr = u_block; v_ptr = v_block;
      for ( j = 0 ; j < ncols ; j++ ) {
        t = (*y_ptr++) + 128;
        RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;

        t = (*u_ptr++) + 128;
        RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;

        t = (*v_ptr++) + 128;
        RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;
      }
      y_block += dct->cols_in_MDU;
      u_block += dct->cols_in_MDU;
      v_block += dct->cols_in_MDU;
      buffer += dct->bytes_in_scanline;
    }
    break;
  case 4:
    /* this case was not catered for until task 20665
     * but must be allowed. Presumably cmyk->cmyk case
     */
    y_block = dct->components[0].mdu_block;
    u_block = dct->components[1].mdu_block;
    v_block = dct->components[2].mdu_block;
    k_block = dct->components[3].mdu_block;
    /* presumably yuvk block */
    for (i= 0; i< nrows; i++) {
      rc_ptr = buffer;
      y_ptr = y_block; u_ptr = u_block; v_ptr = v_block; k_ptr = k_block;
      for ( j = 0 ; j < ncols ; j++ ) {
        t = (*y_ptr++) + 128; RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;

        t = (*u_ptr++) + 128; RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;

        t = (*v_ptr++) + 128; RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;

        t = (*k_ptr++) + 128; RANGE_LIMIT(t);
        *rc_ptr++ = (uint8) t;
      }
      y_block += dct->cols_in_MDU;
      u_block += dct->cols_in_MDU;
      v_block += dct->cols_in_MDU;
      k_block += dct->cols_in_MDU;
      buffer += dct->bytes_in_scanline;
    }
    break;

  default:
    HQFAIL("Unrecognised number of colours in DCT");
  }
  return;
}

/* ----------------------------------------------------------------------------
   function:            transfer_YUV_to_RGB author:           Behnam BaniEqbal
   creation date:       10-jun-1997        last modification:   ##-###-####
   arguments:
      dct - the dct state - it contains yuv blocks.
      buffer - pointer to output buffer for rgb triples.
   description: Convert from YUV to RGB colour spaces.
   Makes use of repeat u-v coeff by reusing converted rgb triples.
---------------------------------------------------------------------------- */
static void transfer_YUV_to_RGB( register DCTSTATE *dct,
                                uint8* buffer)

{
  /* Taken from the think.com PD JPEG implementation.
   * Performs the matrix multiplication by table look up.
   */
  int32 *y_block, *u_block, *v_block;
  register int32 y, u, v, i,j, o;
  register int32 nrows, ncols;
  register int32 *y_ptr, *u_ptr, *v_ptr;
  register uint8 *rc_ptr;


  y_block = dct->components[0]. mdu_block;
  u_block = dct->components[1]. mdu_block;
  v_block = dct->components[2]. mdu_block;
  nrows = dct->nrows;
  ncols = dct->ncols;

  for (i= 0; i< nrows; i++) {
    rc_ptr = buffer;
    y_ptr = y_block; u_ptr = u_block; v_ptr = v_block;
    for ( j = 0 ; j < ncols ; j++ ) {
      y = *y_ptr++;
      u = *u_ptr++;
      v = *v_ptr++;

      /* Note: if the inputs were computed directly from RGB values,
       * range-limiting would be unnecessary here; but due to possible
       * noise in the DCT/IDCT phase, we do need to apply range limits.
       */
      /* red */
      o = y + v_r_tab[v];
      RANGE_LIMIT( o ) ;
      *rc_ptr++ = (uint8)o ;

      /* green */
      o = y + BIT_SHIFT32_SIGNED_RIGHT_EXPR(u_g_tab[u] + v_g_tab[v], 16);
      RANGE_LIMIT( o ) ;
      *rc_ptr++ = (uint8)o ;

      /* blue */
      o = y + u_b_tab[u];
      RANGE_LIMIT( o ) ;
      *rc_ptr++ = (uint8)o;

    }
    y_block += dct->cols_in_MDU;
    u_block += dct->cols_in_MDU;
    v_block += dct->cols_in_MDU;
    buffer += dct->bytes_in_scanline;
  }
  return;
}

/* ----------------------------------------------------------------------------
   function:      transfer211_YUV_to_RGB author:           Behnam BaniEqbal
   creation date:       10-jun-1997        last modification:   ##-###-####
   arguments:
      dct - the dct state - it contains yuv blocks.
      it assumes that the sampling ratios are 2x2 1x1 1x1.
      the U-V blocks are NOT up sampled.
      buffer - pointer to output buffer for rgb triples.
   description: Convert from YUV to RGB colour spaces.
   Makes use of repeat u-v coeff by reusing converted rgb triples.
---------------------------------------------------------------------------- */
static void transfer211_YUV_to_RGB( register DCTSTATE *dct,
                                    uint8* buffer)

{
  /* Taken from the think.com PD JPEG implementation.
   * Performs the matrix multiplication by table look up.
   */
  int32 *y_block, *u_block, *v_block;
  int32 y, u, v, i,j;
  int32 nrows, ncols;
  register int32 *y_ptr, *u_ptr, *v_ptr;
  register uint8 *rc_ptr;
  register int32 r,g,b,r1,g1,b1,y1;
  int32 DoExtraRow, DoExtraCol;


  y_block = dct->components[0]. mdu_block;
  u_block = dct->components[1]. mdu_block;
  v_block = dct->components[2]. mdu_block;
  nrows = dct->nrows;
  ncols = dct->ncols;

  /* The basic algorithm below assumes an even number of both rows and columns.
     To cope with an odd number, the 'DoExtraRow' and 'DoExtraCol' flags are
     used, and the additional code appears rather clumsy, but all in the name
     of reasonable run-time efficiency.
  */
  if ((nrows & 1) > 0) {   /* Odd number of rows */
    DoExtraRow = TRUE;
    nrows--;
  } else {
    DoExtraRow = FALSE;
  }

  if ((ncols & 1) > 0) {   /* Odd number of cols */
    DoExtraCol = TRUE;
    ncols--;
  } else {
    DoExtraCol = FALSE;
  }

  for (i= 0; i< nrows; i+=2) {
    rc_ptr = buffer;
    y_ptr = y_block; u_ptr = u_block; v_ptr = v_block;

    for ( j = 0 ; j < ncols ; j+=2 ) {
      y = *y_ptr++;
      u = *u_ptr++;
      v = *v_ptr++;

      /* Note: if the inputs were computed directly from RGB values,
       * range-limiting would be unnecessary here; but due to possible
       * noise in the DCT/IDCT phase, we do need to apply range limits.
       *
       * Also - we need to remember the rgb values of this first pixel.
       * It's needed to calculate values for the other pixels below,
       * because range_limit destroys the value. We use r, r1, etc.
       */
      r = r1 = y + v_r_tab[v];      /* red */
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;

      /* green */
      g = g1 = y + BIT_SHIFT32_SIGNED_RIGHT_EXPR(u_g_tab[u] + v_g_tab[v], 16);
      RANGE_LIMIT( g1 ) ;
      *rc_ptr++ = (uint8) g1 ;


      b = b1 = y + u_b_tab[u];      /* blue */
      RANGE_LIMIT( b1 ) ;
      *rc_ptr++ = (uint8) b1;

      /* Now compute rgb of the other three pixels. */
      y1 = *y_ptr++ - y;
      r1 = r + y1;
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;
      g1 = g + y1;
      RANGE_LIMIT( g1 );
      *rc_ptr++ = (uint8) g1;
      b1 = b + y1;
      RANGE_LIMIT( b1 );
      *rc_ptr++ = (uint8) b1;

      /* Next row down for the next two pixels */
      rc_ptr += dct->bytes_in_scanline - 6;

      /* we are handling 2x2 y data with 1x1 for u and v
         as the data is packed for a 16x16 macro block for
         the y we skip 16 pixels to get to the next line
         remember that y_ptr has already been advanced twice
         above */
      #define Y_WIDTH 16

      /* the y data is arranged in blocks of Y_WIDTH */
      y1 = *(y_ptr + Y_WIDTH - 2) - y;
      r1 = r + y1;
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;
      g1 = g + y1;
      RANGE_LIMIT( g1 );
      *rc_ptr++ = (uint8) g1;
      b1 = b + y1;
      RANGE_LIMIT( b1 );
      *rc_ptr++ = (uint8) b1;

      y1 = *(y_ptr + Y_WIDTH - 1) - y;
      r1 = r + y1;
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;
      g1 = g + y1;
      RANGE_LIMIT( g1 );
      *rc_ptr++ = (uint8) g1;
      b1 = b + y1;
      RANGE_LIMIT( b1 );
      *rc_ptr++ = (uint8) b1;

      /* Restore to previous line, 2 columns along */
      rc_ptr -= dct->bytes_in_scanline;
    }  /* next col */

    if (DoExtraCol) {
      y = *y_ptr++;
      u = *u_ptr++;
      v = *v_ptr++;

      /* 1st row */
      r = r1 = y + v_r_tab[v];
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;

      g = g1 = y + BIT_SHIFT32_SIGNED_RIGHT_EXPR(u_g_tab[u] + v_g_tab[v], 16);
      RANGE_LIMIT( g1 );
      *rc_ptr++ = (uint8) g1 ;

      b = b1 = y + u_b_tab[u];
      RANGE_LIMIT( b1 ) ;
      *rc_ptr++ = (uint8) b1;

      /* Next row down */
      rc_ptr += dct->bytes_in_scanline - 3;
      y1 = *(y_ptr + Y_WIDTH - 1) - y;
      r1 = r + y1;
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;
      g1 = g + y1;
      RANGE_LIMIT( g1 );
      *rc_ptr++ = (uint8) g1;
      b1 = b + y1;
      RANGE_LIMIT( b1 );
      *rc_ptr++ = (uint8) b1;

      /* Restore to previous line */
      rc_ptr -= dct->bytes_in_scanline;
    }

    y_block += 32;
    u_block += 8;
    v_block += 8;
    buffer += 2 * dct->bytes_in_scanline;
  } /* next row */

  if (DoExtraRow) {
    rc_ptr = buffer;
    y_ptr = y_block; u_ptr = u_block; v_ptr = v_block;

    for ( j = 0 ; j < ncols ; j+=2 ) {
      y = *y_ptr++;
      u = *u_ptr++;
      v = *v_ptr++;

      /* 1st col */
      r = r1 = y + v_r_tab[v];
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;

      g = g1 = y + BIT_SHIFT32_SIGNED_RIGHT_EXPR(u_g_tab[u] + v_g_tab[v], 16);
      RANGE_LIMIT( g1 ) ;
      *rc_ptr++ = (uint8) g1 ;

      b = b1 = y + u_b_tab[u];
      RANGE_LIMIT( b1 ) ;
      *rc_ptr++ = (uint8) b1;

      /* 2nd col */
      y1 = *y_ptr++ - y;
      r1 = r + y1;
      RANGE_LIMIT( r1 );
      *rc_ptr++ = (uint8) r1;
      g1 = g + y1;
      RANGE_LIMIT( g1 );
      *rc_ptr++ = (uint8) g1;
      b1 = b + y1;
      RANGE_LIMIT( b1 );
      *rc_ptr++ = (uint8) b1;

    }  /* next col */

    if (DoExtraCol) {
      y = *y_ptr++;
      u = *u_ptr++;
      v = *v_ptr++;

      r1 = y + v_r_tab[v];
      RANGE_LIMIT(r1);
      *rc_ptr++ = (uint8) r1;

      g1 = y + BIT_SHIFT32_SIGNED_RIGHT_EXPR(u_g_tab[u] + v_g_tab[v], 16);
      RANGE_LIMIT( g1 ) ;
      *rc_ptr++ = (uint8) g1 ;

      b1 = y + u_b_tab[u];
      RANGE_LIMIT( b1 ) ;
      *rc_ptr++ = (uint8) b1;
    }
  } /* extra row */

  return;
}

/* ----------------------------------------------------------------------------
   function:            transfer_YUVK_to_CMYK  author:       behnam BaniEqbal
   creation date:       10-Sep-1991        last modification:   ##-###-####
   arguments:
            pOutputBuffer   - Pointer to byte destination buffer.
            pInputBuffer    - Pointer to Longword input buffer(only lower byte
                              significant.).
            cBytesToConvert - Count of bytes to write to output buffer.

   description: Convert from YUVK to CMYK colour spaces.

---------------------------------------------------------------------------- */
static void transfer_YUVK_to_CMYK( register DCTSTATE *dct,
                                   uint8* buffer)
{
  /* Taken from the think.com PD JPEG implementation.
   * Performs the matrix multiplication by table look up.
   */
  int32 *y_block, *u_block, *v_block, *k_block;
  register int32 y, u, v, i,j, o;
  register int32 nrows, ncols;
  register int32 *y_ptr, *u_ptr, *v_ptr, *k_ptr;
  register uint8 *rc_ptr;


  y_block = dct->components[0]. mdu_block;
  u_block = dct->components[1]. mdu_block;
  v_block = dct->components[2]. mdu_block;
  k_block = dct->components[3]. mdu_block;
  nrows = dct->nrows;
  ncols = dct->ncols;

  for (i= 0; i< nrows; i++) {
    rc_ptr = buffer;
    y_ptr = y_block; u_ptr = u_block; v_ptr = v_block; k_ptr = k_block;
    for ( j = 0 ; j < ncols ; j++ ) {
      y = *y_ptr++;
      u = *u_ptr++;
      v = *v_ptr++;

      /* Note: if the inputs were computed directly from RGB values,
       * range-limiting would be unnecessary here; but due to possible
       * noise in the DCT/IDCT phase, we do need to apply range limits.
       */
      /* Note: if the inputs were computed directly from RGB values,
       * range-limiting would be unnecessary here; but due to possible
       * noise in the DCT/IDCT phase, we do need to apply range limits.
       */
      /* cyan */
      o = 255 - ( y + v_r_tab[v] ) ;
      RANGE_LIMIT( o ) ;
      *rc_ptr++ = (uint8) o ;

      /* magenta */
      o = 255 - (y + BIT_SHIFT32_SIGNED_RIGHT_EXPR(u_g_tab[u] + v_g_tab[v], 16));
      RANGE_LIMIT( o ) ;
      *rc_ptr++ = (uint8) o ;

      /* yellow */
      o = 255 - ( y + u_b_tab[u] ) ;
      RANGE_LIMIT( o ) ;
      *rc_ptr++ = (uint8) o ;

      /* shift k because this is not done before*/
      o = (*k_ptr++) + 128;
      RANGE_LIMIT(o);
      *rc_ptr++ = (uint8) o;
    }
    y_block += dct->cols_in_MDU;
    u_block += dct->cols_in_MDU;
    v_block += dct->cols_in_MDU;
    k_block += dct->cols_in_MDU;
    buffer += dct->bytes_in_scanline;
  }
  return;
}

void init_C_globals_gu_dct(void)
{
#if defined( ASSERT_BUILD )
  debug_dctdecode = TRUE ;
#endif
  v_r_tab = u_b_tab = v_g_tab = u_g_tab = NULL ;
  inited_RGB_to_YUV_tables = FALSE ;
  inited_YUV_to_RGB_tables = FALSE ;
}

/*
Log stripped */
