/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imb32.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Image block storage Implementation for 32 bit data
 *
 * With introduction of MS wide gamut images in XPS, it is necessary
 * to extend the size and accuracy of image data kept in the image store.
 * Given the XPS input is floating point data, and the color conversion
 * process needs data in floating point format, it is natural to keep the
 * image samples in the image store as floating-point data.
 * However, there is a great deal of redundancy in the floating point format,
 * so it is possible to compress the data in many cases.
 *
 * This module supplies abstractions for storing and accessing 32bit
 * floating-point image values, plus routines to compress and decompress the
 * data on the fly.
 *
 * The IEEE-754 standard for single precision 32bit floating point numbers
 * specifies 23 fraction bits, 8 exponent bits, and 1 sign bit arranged as
 *
 *   31                22                                           0
 *    S E E E E E E E E F F F F F F F F F F F F F F F F F F F F F F F
 *
 * The exponent is biased by 127, and exponents of 0 and 255 are reserved
 * for special purposes. This leaves allowable exponents -126 to +127
 * [ Exponent of 0 represents a denomalised number (no hidden leading 1)
 *   and exponent of 255 represents infinity (F == 0) and NaN (F != 0)
 *
 * The most signifant bit of the fraction is always 1, and is not stored.
 * So the value representing by a floating point number with
 *   sign = S, exponent = E, fraction = F is
 *                  S * 1.F * 2^(E-127)
 *
 * Hence 1.0  = 0/127/0    = 0x3f800000
 *       2.0  = 0/128/0    = 0x40000000
 *       0.5  = 0/126/0    = 0x3f000000
 *       0.0  = 0/0/0      = 0x00000000
 *      -1.0  = 1/127/0    = 0xbf800000
 *       0.75 = 0/126/1... = 0x3f400000
 *
 * Standard compression techniques (e.g zlib etc.) for raw image data
 * have been found to be unsuitable because
 *   a) They are typically too slow
 *   b) They have large startup and overhead costs
 *   c) They achieve poor compression as they un-aware of the image
 *      geometry, and so cannot take full benefit of any spatial coherence
 *   d) It is hard to force them to keep a specifiable number of bits of
 *      floating point accuracy.
 *
 * So an alternative entropy encoding scheme has been developed, where
 * compression can be thought of as a four stage process
 *  1) Normalise : Put data into a standard format
 *  2) Predict : Guess the next value based on previous sample values
 *  3) Compare : Guess versus actual data, resulting in a 'difference'
 *  4) Encode  : Encode difference using some Huffman-like scheme.
 *
 *  \todo BMJ 12-Sep-07 :  This is just a template implementation at the
 *  moment. Huffman data needs to be better tailored to sample probability
 *  distributions, and code needs to be heavily optimised. But lets get it
 *  plumbed in first.
 */

#include "core.h"
#include "imb32.h"

/**
 * Bit position of highest bit set in a byte
 * e.g. tbit[5] = 3 as 3rd bit is set (lowest bit counting as 1)
 * \todo BMJ 12-Sep-07 :  I think this is a duplicate of another core rip
 * table. Figure out which one and share it.
 * \todo ajcd 2009-11-20: It's highest_bit_set_in_byte, except that
 * tbit[x] == highest_bit_set_in_byte[x]+1
 */
static int32 tbit[256] =
{
  0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

/**
 * Bit position of highest bit set in a 16bit word
 */
#define TBIT16(_x) ((((_x) & 0xff00) == 0)?tbit[(_x)]:8+tbit[(_x)>>8])

/**
 * Bit position of highest bit set in a 32bit word
 */
#define TBITS32(_x) ((((_x) & 0xffff0000) == 0)?TBIT16(_x):16+TBIT16((_x)>>16))

/**
 * Huffman keys and lengths for encoding data length from 0 to 31
 */
static struct
{
  uint32 key, len;
} huf[32] =
{
  { 0x3C,  6 }, { 0xFE,  8 }, { 0xFD,  8 }, { 0xFC,  8 },
  { 0x3E,  6 }, { 0x0E,  4 }, { 0x0D,  4 }, { 0x09,  4 },
  { 0x08,  4 }, { 0x00,  3 }, { 0x01,  3 }, { 0x02,  3 },
  { 0x03,  3 }, { 0x0A,  4 }, { 0x0B,  4 }, { 0x0C,  4 },
  { 0x3D,  6 }, { 0xFF0, 12}, { 0xFF1, 12}, { 0xFF2, 12},
  { 0xFF3, 12}, { 0xFF4, 12}, { 0xFF5, 12}, { 0xFF6, 12},
  { 0xFF7, 12}, { 0xFF8, 12}, { 0xFF9, 12}, { 0xFFA, 12},
  { 0xFFB, 12}, { 0xFFC, 12}, { 0xFFD, 12}, { 0xFFE, 12},
};

/**
 * Backwards huffman mapping
 */
static uint32 rev_huf[4096];

/**
 * Initialise image store 32bit float block code
 * \return    Success status
 */
void init_C_globals_imb32(void)
{
  int32 i;

  for ( i = 0 ; i < 4096; i++ )
    rev_huf[i] = 0;
  for ( i = 0 ; i < 32; i++ )
    rev_huf[huf[i].key] = i;
}

/**
 * Image data is compressed as a bitsream.
 * This data structure acts as an abstraction for reading/writing
 * indivual bits from memory.
 */
typedef struct IMB32BS
{
  uint32 *ptr32;
  uint32 bits_free;
} IMB32BS;

/**
 * Output the specified number of bits of data
 */
void out_bits(IMB32BS *out, uint32 bits, uint32 data)
{
  HQASSERT(bits > 0 && bits <= 32, "corrupt number of bits");

  if ( out->bits_free < bits )
  {
    out->ptr32[0] |= data >> (bits - out->bits_free);
    out->ptr32[1]  = data << (32 - bits + out->bits_free);
    out->ptr32++;
    out->bits_free = 32 - bits + out->bits_free;
  }
  else
  {
    out->ptr32[0] |= data << (out->bits_free - bits);
    if ( (out->bits_free -= bits) == 0 )
    {
      out->ptr32++;
      out->ptr32[0] = 0;
      out->bits_free = 32;
    }
  }
}

/**
 * Work out the different constants needed to normalise the image data,
 * given the data style
 */
void get_norms(int32 style, uint32 *shft, float *norm1, uint32 *norm2)
{
  /* Check we can cram it into a byte */
  HQASSERT(style >= 0 && style <= 255, "style value out of range");

  switch ( style )
  {
    case FLT0TO1:
      *shft = 0;
      *norm1 = 1.0;
      *norm2 = 0x3f800000;
      break;
    case FLT0TO1WAS8BIT:
      *shft = 14;
      *norm1 = 1.0;
      *norm2 = 0x3f800000;
      break;
    case FLTM4TOP4:
      *shft = 0;
      *norm1 = 12.0;
      *norm2 = 0x41000000;
      break;
    case FLTISBYTES:
      *shft = 0;
      *norm1 = 0.0;
      *norm2 = 0x0;
      break;
    default:
      /* Set values to silence compiler warnings */
      *shft = 0;
      *norm1 = 0.0;
      *norm2 = 0x0;
      HQFAIL("Unexpected imb32 style");
  }
}

/**
 * Compress the given block of floating point data, returning the number
 * of resulting bytes.
 *
 * \param[in]   style      Characterisation of input floating point data
 * \param[in]   src        Pointer to the floating-point input data
 * \param[in]   width      width of block of floating point samples
 * \param[in]   height     height of block of floating point samples
 * \param[out]  dst        Pointer to resulting compressed data
 * \param[in]   maxbytes   Maximum number of bytes of output allowed
 * \return                 Number of compressed bytes created
 */
int32 imb32_compress(int32 style, float *src, uint32 width, uint32 height,
                     uint32 *dst, uint32 maxbytes)
{
  uint32 x, y, norm2, shft;
  float norm1;
  IMB32BS out;

  get_norms(style, &shft, &norm1, &norm2);

  out.ptr32 = dst;
  out.ptr32[0] = (style<<24);
  out.bits_free = 24;

  for ( y = 0; y < height; y++ )
  {
    uint32 prev = 0;

    /* worst case expansion test to keep it ouside inner loop */
    if ( maxbytes - ((out.ptr32 - dst)*sizeof(uint32)) < 2*width )
      return -1; /* running out of space in the dest buffer */

    if ( style == FLTISBYTES )
    {
      for ( x = 0; x < width; x++ )
      {
        uint8  val  = ((uint8 *)src)[x+y*width];
        uint8  diff = (uint8)(prev ^ val);
        uint32 bits = tbit[diff];

        out_bits(&out, huf[bits].len, huf[bits].key);
        if ( bits != 0 )
          out_bits(&out, bits, diff);

        prev = val;
      }
    }
    else
    {
      for ( x = 0; x < width; x++ )
      {
        union { float f; uint32 i; } ff;
        uint32 val, diff, bits;

        ff.f = src[x+y*width] + norm1;
        val  = ((ff.i ^ norm2) >> shft);
        diff = prev ^ val;
        bits = TBITS32(diff);

        out_bits(&out, huf[bits].len, huf[bits].key);
        if ( bits != 0 )
          out_bits(&out, bits, diff);
        prev = val;
      }
    }
  }
  if ( out.bits_free < 32 )
    out.ptr32++;

  return (int32)((out.ptr32 - dst)*sizeof(uint32));
}

/**
 * Read the specified number of bits from the given bitstream
 */
static uint32 in_bits(IMB32BS *in, uint32 bits)
{
  uint32 w;

  HQASSERT(bits > 0 && bits <= 32, "corrupt number of bits");

  if ( in->bits_free  < bits )
  {
    w  = in->ptr32[0] << (bits - in->bits_free);
    w |= in->ptr32[1] >> (32 - bits + in->bits_free);
    in->ptr32++;
    in->bits_free = 32 - bits + in->bits_free;
  }
  else
  {
    w = (in->ptr32[0] >> (in->bits_free - bits));
    if ( (in->bits_free -= bits) == 0 )
    {
      in->ptr32++;
      in->bits_free = 32;
    }
  }
  w = w & ((1<<bits)-1);
  return w;
}

/**
 * Read the next sample word
 */
static uint32 read_data(IMB32BS *in)
{
  uint32 bits;

  bits = in_bits(in, 3);
  if ( bits & 0x4 )
  {
    bits = 2*bits + in_bits(in, 1);
    if ( bits == 0xF )
    {
      bits = 4*bits + in_bits(in, 2);
      if ( bits == 0x3F )
      {
        bits = 4*bits + in_bits(in, 2);
        if ( bits == 0xFF )
        {
          bits = 16*bits + in_bits(in, 4);
        }
      }
    }
  }
  if ( (bits = rev_huf[bits]) == 0 )
    return 0;
  return in_bits(in, bits);
}

/**
 * De-Compress the given block of data back into floating-point format
 *
 * \param[in]   src      Pointer to the compressed input data
 * \param[in]   bytes    Number of bytes of input
 * \param[out]  dst      Pointer to resulting floating-point data
 * \param[in]   width    width of block of floating point samples
 * \param[in]   height   height of block of floating point samples
 * \return               Success status
 */
Bool imb32_decompress(uint32 *src, uint32 bytes, float *dst, uint32 width,
                      uint32 height)
{
  int32 style;
  uint32 x, y, shft, round, norm2;
  float norm1 = 1.0;
  IMB32BS in;

  in.ptr32 = src;
  in.bits_free = 24;
  style = in.ptr32[0]>>24;
  get_norms(style, &shft, &norm1, &norm2);
  round = ((shft != 0) ? ((1<<(shft-1))/2) : 0);

  for ( y = 0; y < height; y++ )
  {
    uint32 prev = 0;

    if ( style == FLTISBYTES )
    {
      for ( x = 0; x < width; x++ )
      {
        uint8 diff = (uint8)read_data(&in);
        uint8 val = (uint8 )(diff ^ prev);

        ((uint8 *)dst)[x+y*width] = val;

        prev = val;
      }
    }
    else
    {
      for ( x = 0; x < width; x++ )
      {
        uint32 diff = read_data(&in);
        uint32 val = diff ^ prev;
        union { uint32 i; float f; } valUP;

        valUP.i = ((val << shft) + round)^norm2;
        dst[x+y*width] = valUP.f - norm1;
        prev = val;
      }
    }
  }
  if ( in.bits_free < 32 )
    in.ptr32++;

  return bytes == (int32)((in.ptr32 - src)*sizeof(uint32));
}

#ifdef STAND_ALONE_TEST

#include <malloc.h>
#include <stdio.h>
#include <math.h>

void main()
{
  int32 x, y, dim = 128, style, w = dim;
  float *data = (float *)malloc(dim*dim*sizeof(float));
  float *back = (float *)malloc(dim*dim*sizeof(float));
  uint32 bytes, *out = (uint32 *)malloc(dim*dim*10*sizeof(uint32));
  double maxerr;
  FILE *f = fopen("imb32.tst","rb");

  HQASSERT(f && data && back && out,"Failed to get going");
  fread((void *)data, 1, dim*dim*sizeof(float), f);
  fclose(f);

  (void)imb32_init();

  /* for ( style = FLT0TO1; style <= FLTISBYTES; style++ ) */
  for ( style = FLTISBYTES; style <= FLTISBYTES; style++ )
  {
    if ( style == FLTISBYTES )
      w = dim*4;

    bytes = imb32_compress(style, data, w, dim, out, dim*dim*10);

    printf("style = %d\n", style);
    printf("128*128 floats compressed to %d bytes\n",bytes);
    printf("ratio = %.1f%% bits=%.1f\n", bytes*100.0/(128*128*sizeof(float)),
                                         bytes*8.0/(128*128));

    for ( x = 0; x < dim ; x++ )
      for ( y = 0; y < dim ; y++ )
        back[x+y*dim] = 0.0f;

    (void)imb32_decompress(out, bytes, back, w, dim);

    maxerr = 0.0;
    for ( x = 0; x < dim ; x++ )
    {
      for ( y = 0; y < dim ; y++ )
      {
        float err = data[x+y*dim] - back[x+y*dim];
        if ( err < 0.0 )
          err = -err;
        if ( err > maxerr )
          maxerr = err;
      }
    }
    printf("Re-expanded : maxerr = %.9f%% : bits %f\n", 100.0*maxerr,
       maxerr > 0.0 ? -log(maxerr)/log(2.0) : 32.0);
    printf("--------------------------------------------------------\n");
  }
}

uint32 hqassert_val = 0x01dead10;

void HqAssert(char *pszFilename, int nLine, char *pszMessage)
{
  fprintf(stderr, "IMB32 Assert failed in file %s at line %ld: %s\n",
          pszFilename, nLine, pszMessage);
}

void HqAssertPhonyExit(void)
{
}

/** \todo BMJ 12-Sep-07 :  figure out how to do stand-alone test harness */
/*
cl -Wp64 -MDd -W4 -nologo -TC -Zi -GS -RTCs -DMULTI_PROCESS=116 -DASSERT_BUILD -DDEBUG_BUILD -DNOOPT_BUILD -DWIN32 -D_X86_=1 -DRIPCALL=__cdecl -DMPS_CALL=__cdecl -DZEXPORT=__cdecl -I. -I..\export -I ..\..\shared -I..\..\..\standard\export -I..\..\interface\control -I..\..\types\export -I..\..\objects\export -I..\..\mm\export -I..\..\..\mps\export -I..\..\errors\export -DSTAND_ALONE_TEST=1 -I..\..\devices\export -I..\..\cce\export -I..\..\morisawa\export -I..\..\tables\export -I..\..\devices\export -I..\..\multi\export -I..\..\trapping\export -I..\..\errors\export -I..\..\objects\export -I..\..\types\export -I..\..\fileio\export -I..\..\pdf\export -I..\..\v20\export -I..\..\fonts\export -I..\..\pdfin\export -I..\..\xml\export -I..\..\hdlt\export -I..\..\pdfout\export -I..\..\xps\export -I..\..\mm\export -I..\..\render\export -I..\..\zipdev\export -I..\..\obj\c_src\a\msvcnt_7_0\misc -I..\..\interface\screening -I..\..\..\checksum\export -I..\..\..\openssl\export -I..\..\..\cmmeg\export -I..\..\..\palantir\export -I..\..\..\customer\export -I..\..\..\papertyp\export -I..\..\..\dllfuncs\export -I..\..\..\ptdev\export -I..\..\..\dlliface\export -I..\..\..\pthreads\export -I..\..\..\encrypt\export -I..\..\..\quantify\export -I..\..\..\filtereg\export -I..\..\..\refiface\export -I..\..\..\fwos\export -I..\..\..\rom\export -I..\..\..\htmeg\export -I..\..\..\security\export -I..\..\..\icolor\export -I..\..\..\sign\export -I..\..\..\icu\export -I..\..\..\skinkit\export -I..\..\..\le-security\export -I..\..\..\standard\export -I..\..\..\libgenxml\export -I..\..\..\threads\export -I..\..\..\liblittlecms\export -I..\..\..\unicode\export -I..\..\..\libpng\export -I..\..\..\uri\export -I..\..\..\md5\export -I..\..\..\zlib\export -I..\..\..\mps\export imb32.c
*/

#endif

/* Log stripped */
