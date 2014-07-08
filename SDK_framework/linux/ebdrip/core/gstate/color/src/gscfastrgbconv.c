/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscfastrgb2gray.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fast RGB to gray conversion.
 */

#include "core.h"
#include "gscfastrgbconvpriv.h"
#include "gs_colorpriv.h"        /* COLOR_STATE */
#include "gscheadpriv.h"         /* GS_CHAINinfo */
#include "mm.h"                  /* mm_alloc */
#include "swerrors.h"            /* error_handler */


#define FASTRGB2GRAY_MAX_BPC ( 12 )
#define FASTRGB2GRAY_PRECISION ( 12 )
#define FASTRGB2GRAY_DEV_LUT_SIZE ( 1 << FASTRGB2GRAY_PRECISION )


struct GS_FASTRGB2GRAY_STATE {
  /* fp_values used to convert the scaled integer color values of the
   * optimised color conversion into f.p values required by
   * device code calculation.
   */
  USERVALUE fp_values[ 1 << FASTRGB2GRAY_PRECISION ] ;

  /* LUT based color converison for RGB images going to Monochrome. */
  /* LUTs used to convert RGB to Gray are simple to construct, and
   * may be shared amongst images of the same BPC and Decode arrays.
   * Experimentation suggests that a complex scheme for caching RGB
   * to Gray LUTs has little benefit, so we use the simplest scheme
   * where only the last built data is avaiable for re-use.
   * RGB to Gray LUTS can be reused amongst all images that share
   * same BPC and Decode array number and values.
   * OTOH, we do have to take care to share Device code LUTs amongst
   * all applicable chains as they are expensive to construct.
   * Require color chain invocation to build device code LUT.
   * NB There might be some (small?) gain from copying a device code lut
   * from the color chain to a statically allocation array before using
   * the data in the lut, to enable data to accesed with direct addressing.
   *
   * RGB and Gray component LUTs use a fixed precision (number of bits) to
   * represent the colorvalues as integers. Gray colorvalues are then
   * mapped to device codes via another table ( associated to the color
   * chain for the image ). The precision determines the size of LUT
   * device code LUT. RGB LUTs size depends on BPC for image.
   */
  COLORVALUE opt_lut_lut_r[ 1 << FASTRGB2GRAY_MAX_BPC ];
  COLORVALUE opt_lut_lut_g[ 1 << FASTRGB2GRAY_MAX_BPC ];
  COLORVALUE opt_lut_lut_b[ 1 << FASTRGB2GRAY_MAX_BPC ];
  uint32 opt_lut_bpc;
  int bpc_shift;
  float opt_lut_Decode[3][2];

/* Simple tracking of allocations, check for leaks. */
#ifdef DEBUG_BUILD
  uint32 devCodeLutCount;
#endif
};

struct GS_FASTRGB2CMYK_STATE {
  uint32 bpc;
  COLORVALUE *r_decode;
  COLORVALUE *g_decode;
  COLORVALUE *b_decode;
};

static void fastrgb2gray_make_gray_fp_lut(GS_FASTRGB2GRAY_STATE *state)
{
  const COLORVALUE max_sample = ( 1 << FASTRGB2GRAY_PRECISION ) - 1 ;
  COLORVALUE gray_val = max_sample ;

  state->fp_values[ gray_val ] = 1.0 ;

  while (gray_val--) {
    state->fp_values[gray_val] = (float)gray_val / max_sample ;
  }
}

static void make_opt_lut(GS_FASTRGB2GRAY_STATE *state,
                         uint32 bpc, float *r_decode, float *g_decode,
                         float *b_decode)
{
  COLORVALUE *opt_lut_lut_r = state->opt_lut_lut_r;
  COLORVALUE *opt_lut_lut_g = state->opt_lut_lut_g;
  COLORVALUE *opt_lut_lut_b = state->opt_lut_lut_b;
  uint32 tablesize = 1 << bpc ;
  uint32 max_sample;
  COLORVALUE scale = ( 1 << FASTRGB2GRAY_PRECISION ) - 1 ;

  max_sample = tablesize - 1 ;

  while ( tablesize-- ) {
    opt_lut_lut_r[tablesize] =
      (COLORVALUE) (( scale * 0.30f * r_decode[tablesize]) + 0.5 );

    opt_lut_lut_g[tablesize] =
      (COLORVALUE) (( scale * 0.59f * g_decode[tablesize]) + 0.5 );

    opt_lut_lut_b[tablesize] =
      (COLORVALUE) (( scale * 0.11f * b_decode[tablesize]) + 0.5 );

    HQASSERT( opt_lut_lut_r[tablesize] + opt_lut_lut_b[tablesize] +
              opt_lut_lut_g[tablesize] <= scale ,  "FAST RGB LUT OVERFLOW" );
  }

  state->opt_lut_bpc = bpc;
  state->opt_lut_Decode[0][0] = r_decode[0] ;
  state->opt_lut_Decode[0][1] = r_decode[max_sample] ;
  state->opt_lut_Decode[1][0] = g_decode[0] ;
  state->opt_lut_Decode[1][1] = g_decode[max_sample] ;
  state->opt_lut_Decode[2][0] = b_decode[0] ;
  state->opt_lut_Decode[2][1] = b_decode[max_sample] ;

  return ;
}

/* ---------------------------------------------------------------------- */

/*
 * To support optimized color conversion of (RGB) images based on LUTs,
 * the color chain can be associated with a LUT that emulates its device code
 * link. The device code LUT is created when a suitable image is identified.
 * Once created, the device code LUT is cached as part of the color chain in
 * the color chain cache.
 * Device code LUT will map color values to device codes.
 * NB : the RGB to Gray conversion LUTs are created independently of the
 * color chain.
 * The device code LUT maps scaled integer colorvalues to device codes.
 * The scale of these color values is compiled in constant.
 */

static FASTRGB2GRAY_LUT *fastrgb2gray_new_dev_code_lut(
                                                   GS_FASTRGB2GRAY_STATE *state)
{
  FASTRGB2GRAY_LUT *tmp;

  tmp = mm_alloc( mm_pool_color,
                  sizeof(FASTRGB2GRAY_LUT)
                    + ((1 << FASTRGB2GRAY_PRECISION ) * sizeof(COLORVALUE)),
                  MM_ALLOC_CLASS_FASTRGB );
  if ( tmp == NULL) {
    (void) error_handler(VMERROR);
    return NULL;
  }

  tmp->fastrgb2grayState = state;
  tmp->devCodes = (COLORVALUE *) (tmp + 1) ;

#ifdef DEBUG_BUILD
  ++state->devCodeLutCount;
#endif
  return tmp ;
}

void cc_fastrgb2gray_freelut(FASTRGB2GRAY_LUT *dev_code )
{
  GS_FASTRGB2GRAY_STATE *state;

  HQASSERT( dev_code , "Missing fastrgb2gray dev_code lut") ;

  state = dev_code->fastrgb2grayState;

  mm_free( mm_pool_color, dev_code,
           sizeof(FASTRGB2GRAY_LUT)
            + ((1 << FASTRGB2GRAY_PRECISION ) * sizeof(COLORVALUE)));

#ifdef DEBUG_BUILD
  --state->devCodeLutCount;
#endif
}

/* All device code LUTS are of a fixed length at the moment. */
static COLORVALUE *fastrgb2gray_get_dev_code_lut(GS_FASTRGB2GRAY_STATE *state,
                                                 GS_COLORinfo *colorInfo,
                                                 int32 colorType,
                                                 Bool bCreate)
{
  GS_CHAINinfo *colorChain;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT( colorInfo != NULL, "colorInfo NULL" );

  colorChain = colorInfo->chainInfo[colorType] ;
  HQASSERT( colorChain != NULL, "colorChain NULL" );

  chainContext = colorChain->context;

  if ( chainContext->devCodeLut != NULL )
    return chainContext->devCodeLut->devCodes ;
  else if ( bCreate ) {
    chainContext->devCodeLut = fastrgb2gray_new_dev_code_lut(state);
    if ( chainContext->devCodeLut == NULL )
      return NULL;
    else
      return chainContext->devCodeLut->devCodes ;
  }
  else
    return NULL ;
}

/* ---------------------------------------------------------------------- */

Bool cc_fastrgb2gray_create(GS_FASTRGB2GRAY_STATE **stateRef)
{
  GS_FASTRGB2GRAY_STATE *state;

  HQASSERT(*stateRef == NULL, "fastrgb2gray state already exists");

  state = mm_alloc(mm_pool_color, sizeof(GS_FASTRGB2GRAY_STATE),
                   MM_ALLOC_CLASS_FASTRGB);
  if ( state == NULL )
    return error_handler(VMERROR);

  state->opt_lut_bpc = 0 ;
  fastrgb2gray_make_gray_fp_lut(state);
#ifdef DEBUG_BUILD
  state->devCodeLutCount = 0;
#endif

  *stateRef = state;
  return TRUE;
}

void cc_fastrgb2gray_destroy(GS_FASTRGB2GRAY_STATE **stateRef)
{
  if ( *stateRef != NULL ) {
    mm_free(mm_pool_color, *stateRef, sizeof(GS_FASTRGB2GRAY_STATE));
    *stateRef = NULL;
  }
}

/* Calculate Gray device code value from RGB input. Simple addition of the
 * correpsonding components from RGB tables to give Gray, then mapped to
 * device code.
 */
Bool gsc_fastrgb2gray_do(GS_COLORinfo *colorInfo, int32 colorType,
                         int32 *piColorValues, COLORVALUE *poColorValues,
                         int32 ncolors )
{
  GS_FASTRGB2GRAY_STATE *state = colorInfo->colorState->fastrgb2grayState;
  COLORVALUE *opt_lut_lut_r = state->opt_lut_lut_r;
  COLORVALUE *opt_lut_lut_g = state->opt_lut_lut_g;
  COLORVALUE *opt_lut_lut_b = state->opt_lut_lut_b;
  COLORVALUE *devCodes;
  int shift = state->bpc_shift;

  devCodes = fastrgb2gray_get_dev_code_lut(state, colorInfo, colorType, FALSE) ;

  HQASSERT( devCodes, "Lost dev code LUT for fast rgb to gray" );

  while (ncolors >= 8) {
    poColorValues[0] = devCodes[opt_lut_lut_r[piColorValues[0] >> shift]
                                  + opt_lut_lut_g[piColorValues[1] >> shift]
                                  + opt_lut_lut_b[piColorValues[2] >> shift]];

    poColorValues[1] = devCodes[opt_lut_lut_r[piColorValues[3] >> shift]
                                  + opt_lut_lut_g[piColorValues[4] >> shift]
                                  + opt_lut_lut_b[piColorValues[5] >> shift]];

    poColorValues[2] = devCodes[opt_lut_lut_r[piColorValues[6] >> shift]
                                  + opt_lut_lut_g[piColorValues[7] >> shift]
                                  + opt_lut_lut_b[piColorValues[8] >> shift]];

    poColorValues[3] = devCodes[opt_lut_lut_r[piColorValues[9] >> shift]
                                  + opt_lut_lut_g[piColorValues[10] >> shift]
                                  + opt_lut_lut_b[piColorValues[11] >> shift]];

    poColorValues[4] = devCodes[opt_lut_lut_r[piColorValues[12] >> shift]
                                  + opt_lut_lut_g[piColorValues[13] >> shift]
                                  + opt_lut_lut_b[piColorValues[14] >> shift]];

    poColorValues[5] = devCodes[opt_lut_lut_r[piColorValues[15] >> shift]
                                  + opt_lut_lut_g[piColorValues[16] >> shift]
                                  + opt_lut_lut_b[piColorValues[17] >> shift]];

    poColorValues[6] = devCodes[opt_lut_lut_r[piColorValues[18] >> shift]
                                  + opt_lut_lut_g[piColorValues[19] >> shift]
                                  + opt_lut_lut_b[piColorValues[20] >> shift]];

    poColorValues[7] = devCodes[opt_lut_lut_r[piColorValues[21] >> shift]
                                  + opt_lut_lut_g[piColorValues[22] >> shift]
                                  + opt_lut_lut_b[piColorValues[23] >> shift]];
    piColorValues += 24;
    ncolors -= 8;
    poColorValues += 8;
  }

  while (ncolors--) {
    *poColorValues++ =  devCodes[opt_lut_lut_r[piColorValues[0] >> shift]
                                    + opt_lut_lut_g[piColorValues[1] >> shift]
                                    + opt_lut_lut_b[piColorValues[2] >> shift]];
    piColorValues += 3;
  }

  return TRUE;
}

/* Instantiate the various LUT tables for the fast RGB to Gray.
 * Called only when it is known that fast rgb to gray conversion for
 * images is actually needed. Tables created may be cached either
 * in static variables ( rgb to gray LUT ) or as part of color chain
 * state ( Gray to Device code LUT ).
 * Call this routine just before color conversion is to begin. If
 * color chain changes after that, and is not invalidated, the LUTs
 * might be out of sync with the chain.
 * Using a separate call to allocate and populate LUTs means we
 * ensure that color conversion can progress when invoked.
 */
Bool gsc_fastrgb2gray_prep(GS_COLORinfo *colorInfo, int32 colorType,
                           uint32 bpc, float *r_decode, float *g_decode,
                           float *b_decode )
{
  GS_FASTRGB2GRAY_STATE *state = colorInfo->colorState->fastrgb2grayState;
  uint32 max_sample ;
  uint32 tablesize ;
  COLORVALUE scale ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;
  CLINK *pFirstLink;
  int shift = 0;

  /* Allow bpc over max, and truncate down to max prior to conversion. */
  if ( bpc > FASTRGB2GRAY_MAX_BPC ) {
    shift = bpc - FASTRGB2GRAY_MAX_BPC;
    bpc = FASTRGB2GRAY_MAX_BPC;
  }

  HQASSERT( bpc > 0  &&
            bpc <=  FASTRGB2GRAY_MAX_BPC,
              "Bad table size when making fast rgb to gray luts");

  HQASSERT( r_decode != NULL && g_decode != NULL && b_decode != NULL,
               "Must have decode arrays for all channels to build LUTS" );

  tablesize = 1 << bpc ;
  max_sample = tablesize - 1 ;

  scale = ( 1 << FASTRGB2GRAY_PRECISION ) - 1 ;

  /* reuse rgb to gray LUTs if they fit current image. */
  if ( state->opt_lut_bpc != bpc ||
       state->opt_lut_Decode[0][0] != r_decode[0] ||
       state->opt_lut_Decode[0][1] != r_decode[max_sample] ||
       state->opt_lut_Decode[1][0] != g_decode[0] ||
       state->opt_lut_Decode[1][1] != g_decode[max_sample] ||
       state->opt_lut_Decode[2][0] != b_decode[0] ||
       state->opt_lut_Decode[2][1] != b_decode[max_sample] ) {
    make_opt_lut(state, bpc, r_decode, g_decode, b_decode);
  }
  state->bpc_shift = shift;

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  pFirstLink = chainContext->pnext ;

  if ( colorChain->context->devCodeLut == NULL ) {
    uint32 tconv = scale + 1 ;
    COLORVALUE *devCodes ;
    USERVALUE *fps = state->fp_values ; /* fp_values maps 0 to 1.0 in scale steps */

    /* Get access to the dev code LUT. Can force the creation of one here if
     * required.
     * Device code LUTs are all fixed sizes, determined by precision of
     * color values in the fast rgb to gray path.
     */
    devCodes = fastrgb2gray_get_dev_code_lut(state, colorInfo, colorType, TRUE);
    if ( devCodes == NULL ) {
      cc_fastrgb2gray_freelut( colorChain->context->devCodeLut ) ;
      colorChain->context->devCodeLut = NULL ;
      return FALSE;
    }
    HQASSERT(devCodes != NULL, "devCodes NULL");

    while ( tconv ) {
      uint32 iconv = tconv > GSC_BLOCK_MAXCOLORS ? GSC_BLOCK_MAXCOLORS : tconv;

      if (!invokeDevicecodeBlock( pFirstLink , fps , devCodes , iconv ))
        return FALSE;

      tconv -= iconv ;
      fps += iconv ;
      devCodes += iconv ;
    }
  }

  return TRUE ;
}


Bool cc_fastrgb2cmyk_create(GS_FASTRGB2CMYK_STATE **stateRef)
{
  GS_FASTRGB2CMYK_STATE *state;

  HQASSERT(*stateRef == NULL, "fastrgb2cmyk state already exists");

  state = mm_alloc(mm_pool_color, sizeof(GS_FASTRGB2CMYK_STATE),
                   MM_ALLOC_CLASS_FASTRGB);
  if ( state == NULL )
    return error_handler(VMERROR);

  state->bpc = 0;
  state->r_decode = state->g_decode = state->b_decode = NULL;
  *stateRef = state;
  return TRUE;
}

void cc_fastrgb2cmyk_destroy(GS_FASTRGB2CMYK_STATE **stateRef)
{
  if ( *stateRef != NULL ) {
    mm_free(mm_pool_color, *stateRef, sizeof(GS_FASTRGB2CMYK_STATE));
    *stateRef = NULL;
  }
}


static inline void rgbtocmyk_method2(int32 *rgb, COLORVALUE *cmyk)
{
#define NARROW_CV(color) MACRO_START \
  if ( (color) < COLORVALUE_ZERO ) (color) = COLORVALUE_ZERO ; \
  else if ( (color) > COLORVALUE_ONE ) (color) = COLORVALUE_ONE ; \
  MACRO_END

  int32 c = rgb[0];
  int32 m = rgb[1];
  int32 y = rgb[2];
  int32 k;
  INLINE_MAX32(k, c, m);
  INLINE_MAX32(k, y, k);

  /* BG/UCR must match the PS in hqnebd. */
  {
    double k_float = COLORVALUE_TO_USERVALUE(COLORVALUE_ONE - k);
    int32 bg  = (int32)FLOAT_TO_COLORVALUE(k_float > 0.5 ? (k_float - 0.5) * 1.5 : 0);
    int32 ucr = (int32)FLOAT_TO_COLORVALUE(k_float > 0.5 ? (k_float - 0.5) * 0.5 : 0);

    if ( ucr != COLORVALUE_ZERO ) {
      c = c + ucr; NARROW_CV(c);
      m = m + ucr; NARROW_CV(m);
      y = y + ucr; NARROW_CV(y);
    }
    k = COLORVALUE_ONE - bg;
  }

  cmyk[0] = CAST_TO_COLORVALUE(c);
  cmyk[1] = CAST_TO_COLORVALUE(m);
  cmyk[2] = CAST_TO_COLORVALUE(y);
  cmyk[3] = CAST_TO_COLORVALUE(k);
}

/* Calculate CMYK device code value from RGB input. */
/* N.B. Results seem pretty mixed on whether this is any faster than allowing
 * im_decode to happen and taking piColorValues from 'decode' rather than
 * 'unpack' followed by doing this using floats.  Alternatively using LUTs
 * like for gsc_fastrgb2gray_do seems to be as fast as this or possibly
 * a bit faster, but may give some color differences as only 12 bit precision.
 */
Bool gsc_fastrgb2cmyk_do(GS_COLORinfo *colorInfo, int32 colorType,
                         int32 *piColorValues, COLORVALUE *poColorValues,
                         int32 ncolors )
{
  GS_FASTRGB2CMYK_STATE *state = colorInfo->colorState->fastrgb2cmykState;
  uint32 bpc = state->bpc;
  COLORVALUE *r_decode = state->r_decode;
  COLORVALUE *g_decode = state->g_decode;
  COLORVALUE *b_decode = state->b_decode;

  UNUSED_PARAM(int32, colorType);

  HQASSERT(bpc == 8 || bpc == 16,
           "Unexpected bits per comp in gsc_fastrgb2cmyk_do");
  switch (colorInfo->params.rgbToCMYKMethod) {
  default:
    HQFAIL("New method of rgb to cmyk conversion must be added to fastrgb2cmyk");
    /*FALLTHRU*/
  case 0:
    /* Equivalent to specifying identity procs for BG/UCR. */
    if ( r_decode != NULL && g_decode != NULL && b_decode != NULL ) {
      while (ncolors--) {

        int32 c = r_decode[piColorValues[0]];
        int32 m = g_decode[piColorValues[1]];
        int32 y = b_decode[piColorValues[2]];
        int32 k, k2;
        INLINE_MAX32(k, c, m);
        INLINE_MAX32(k, y, k);

        k2 = COLORVALUE_ONE - k;

        *poColorValues++ = CAST_TO_COLORVALUE(k2 + c);
        *poColorValues++ = CAST_TO_COLORVALUE(k2 + m);
        *poColorValues++ = CAST_TO_COLORVALUE(k2 + y);
        *poColorValues++ = CAST_TO_COLORVALUE(k);

        piColorValues += 3;
      }
    } else {
      if (bpc == 16) {
        while (ncolors--) {

          int32 c = piColorValues[0];
          int32 m = piColorValues[1];
          int32 y = piColorValues[2];
          int32 k, k2;
          INLINE_MAX32(k, c, m);
          INLINE_MAX32(k, y, k);

          k2 = COLORVALUE_ONE - k;

          *poColorValues++ = CAST_TO_COLORVALUE(k2 + c);
          *poColorValues++ = CAST_TO_COLORVALUE(k2 + m);
          *poColorValues++ = CAST_TO_COLORVALUE(k2 + y);
          *poColorValues++ = CAST_TO_COLORVALUE(k);

          piColorValues += 3;
        }
      }
      else {
        while (ncolors--) {

          int32 c = piColorValues[0] << 8;
          int32 m = piColorValues[1] << 8;
          int32 y = piColorValues[2] << 8;
          int32 k, k2;
          INLINE_MAX32(k, c, m);
          INLINE_MAX32(k, y, k);

          k2 = COLORVALUE_ONE - k;

          *poColorValues++ = CAST_TO_COLORVALUE(k2 + c);
          *poColorValues++ = CAST_TO_COLORVALUE(k2 + m);
          *poColorValues++ = CAST_TO_COLORVALUE(k2 + y);
          *poColorValues++ = CAST_TO_COLORVALUE(k);

          piColorValues += 3;
        }
      }
    }
    break;

  case 1:
    /* Equivalent to specifying "{pop 0}" procs for BG/UCR. */
    if ( r_decode != NULL && g_decode != NULL && b_decode != NULL ) {
      while (ncolors--) {
        *poColorValues++ = r_decode[piColorValues[0]];
        *poColorValues++ = g_decode[piColorValues[1]];
        *poColorValues++ = b_decode[piColorValues[2]];
        *poColorValues++ = COLORVALUE_ONE;

        piColorValues += 3;
      }
    } else {
      if (bpc == 16) {
        while (ncolors--) {
          *poColorValues++ = CAST_TO_COLORVALUE(piColorValues[0]);
          *poColorValues++ = CAST_TO_COLORVALUE(piColorValues[1]);
          *poColorValues++ = CAST_TO_COLORVALUE(piColorValues[2]);
          *poColorValues++ = COLORVALUE_ONE;

          piColorValues += 3;
        }
      }
      else {
        while (ncolors--) {
          *poColorValues++ = CAST_TO_COLORVALUE(piColorValues[0] << 8);
          *poColorValues++ = CAST_TO_COLORVALUE(piColorValues[1] << 8);
          *poColorValues++ = CAST_TO_COLORVALUE(piColorValues[2] << 8);
          *poColorValues++ = COLORVALUE_ONE;

          piColorValues += 3;
        }
      }
    }
    break;

  case 2:
    /* Try to match Acrobat's ICC rgb to cmyk conversion using custom BG/UCR procs. */
    if ( r_decode != NULL && g_decode != NULL && b_decode != NULL ) {
      while (ncolors--) {
        int32 rgb[3];

        rgb[0] = r_decode[piColorValues[0]];
        rgb[1] = g_decode[piColorValues[1]];
        rgb[2] = b_decode[piColorValues[2]];

        rgbtocmyk_method2(rgb, poColorValues);

        piColorValues += 3;
        poColorValues += 4;
      }
    } else {
      if (bpc == 16) {
        while (ncolors--) {
          rgbtocmyk_method2(piColorValues, poColorValues);

          piColorValues += 3;
          poColorValues += 4;
        }
      }
      else {
        while (ncolors--) {
          int32 rgb[3];

          rgb[0] = piColorValues[0] << 8;
          rgb[1] = piColorValues[1] << 8;
          rgb[2] = piColorValues[2] << 8;

          rgbtocmyk_method2(rgb, poColorValues);

          piColorValues += 3;
          poColorValues += 4;
        }
      }
    }
    break;
  }

  return TRUE;
}


Bool gsc_fastrgb2cmyk_prep( GS_COLORinfo *colorInfo, uint32 bpc,
                            COLORVALUE *r_decode, COLORVALUE *g_decode,
                            COLORVALUE *b_decode )
{
  GS_FASTRGB2CMYK_STATE *state = colorInfo->colorState->fastrgb2cmykState;

  HQASSERT( bpc == 8  || bpc ==16, "Unexpected bit depth for fastrgb2cmyk");

  state->bpc = bpc;
  state->r_decode = r_decode;
  state->g_decode = g_decode;
  state->b_decode = b_decode;

  return TRUE;
}

/* Log stripped */
