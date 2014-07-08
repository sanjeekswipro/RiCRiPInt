/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:imcolcvt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Code for unpacking input data, interleaving it, applying decode arrays
 * and color converting resultant pixels.
 */

#include "core.h"
#include "imcolcvt.h"
#include "caching.h"   /* PENTIUM_CACHE_LOAD */
#include "gschcms.h"   /* REPRO_COLOR_MODEL */
#include "gu_chan.h"   /* guc_backdropRasterStyle */
#include "halftone.h"  /* ht_allocateForm */
#include "often.h"     /* SwOftenUnsafe */
#include "swerrors.h"  /* VMERROR */
#include "display.h"   /* dl_alloc */
#include "mm.h"        /* mm_pool_t */
#include "graphict.h"  /* GS_COLORinfo */
#include "color.h"     /* HT_TRANSFORM_INFO */
#include "rcbadjst.h"  /* rcba_doing_color_adjust */
#include "gschtone.h"  /* gsc_getSpotno */
#include "gscfastrgb2gray.h" /* gsc_dofastrgb2gray */
#include "hqmemcpy.h"

/* -------------------------------------------------------------------------- */
struct IM_COLCVT {
  DL_STATE *page ;
  GS_COLORinfo *colorInfo ;
  mm_pool_t *pools ;
  int32 **ndecodes ;
  int32 incomps ;
  SPOTNO spotno ;
  HTTYPE reprotype ;
  int32 nconv ;
  int32 cvtsize ;
  int32 *coldec ;
  COLORVALUE *colcvt ;
  uint8 *cvtbuf ;
  int32 oncomps ;
  COLORANTINDEX *ocolorants ;
  Bool justDecode ;
  Bool out16 ;
  Bool top8 ;
  int32 method ;
  int32 colorType ;
  Bool *htDoForms ;
  HT_TRANSFORM_INFO *htTransformInfo ;
} ;

/* -------------------------------------------------------------------------- */
/* Forward statics. */
static void im_planar( COLORVALUE *src , int32 oncomps , int32 nconv ,
                       uint8 *dst, int32 offset, Bool out16, Bool top8 );

/* -------------------------------------------------------------------------- */
IM_COLCVT *im_colcvtopen( GS_COLORinfo *colorInfo , mm_pool_t *pools ,
                          int32 *ndecodes[] , int32 incomps , int32 nconv ,
                          Bool out16 , int32 method , int32 colorType ,
                          Bool justDecode )
{
  Bool result = FALSE;
  IM_COLCVT *imc, init = {0} ;
  GUCR_RASTERSTYLE *targetRS = gsc_getTargetRS(colorInfo);

  HQASSERT( GSC_USE_FASTRGB2GRAY == method || ndecodes != NULL ||
            GSC_USE_FASTRGB2CMYK == method,
            "ndecodes NULL in im_colcvtopen" ) ;
  HQASSERT( incomps > 0 , "incomps should be > 0" ) ;
  HQASSERT( nconv > 0 , "nconv should be > 0" ) ;
  switch (method) {
  case GSC_USE_PS_PROC:
  case GSC_USE_INTERPOLATION:
  case GSC_USE_FASTRGB2GRAY:
  case GSC_USE_FASTRGB2CMYK:
    break;
  default:
    HQFAIL("Unexpected interpolation method");
  }

  imc = dl_alloc(pools, sizeof(IM_COLCVT), MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( imc == NULL ) {
    ( void )error_handler( VMERROR ) ;
    return NULL ;
  }
  *imc = init;
#define return USE_goto_cleanup

  imc->page = CoreContext.page ;
  imc->colorInfo = colorInfo ;
  imc->pools = pools;
  imc->ndecodes = ndecodes ;
  imc->incomps = incomps ;
  imc->nconv = nconv ;
  if ( justDecode )
    imc->oncomps = incomps;
  else {
    imc->spotno = gsc_getSpotno(colorInfo) ;
    imc->reprotype = gsc_getRequiredReproType(colorInfo, colorType) ;

    if ( !gsc_getDeviceColorColorants(colorInfo, colorType,
                                      &imc->oncomps, &imc->ocolorants) )
      goto cleanup;

    /* Prepare to quantise colors to device codes, but only if this is a backend
       chain to final device space.  Doing quantisation outside of Tom's Tables
       ensures consistency between direct- and backdrop-rendered regions. */
    if ( gsc_fCompositing(colorInfo, colorType) &&
         !guc_backdropRasterStyle(targetRS) ) {
      int32 i;

      imc->htDoForms = dl_alloc(pools, imc->oncomps * sizeof(Bool),
                                MM_ALLOC_CLASS_IMAGE_CONVERT);
      imc->htTransformInfo = dl_alloc(pools, imc->oncomps * sizeof(HT_TRANSFORM_INFO),
                                      MM_ALLOC_CLASS_IMAGE_CONVERT);
      if ( imc->htDoForms == NULL || imc->htTransformInfo == NULL ) {
        (void)error_handler(VMERROR);
        goto cleanup;
      }
      for ( i = 0 ; i < imc->oncomps ; ++i ) {
        GUCR_COLORANT *colorant = NULL;
        const GUCR_COLORANT_INFO *info;

        gucr_colorantHandle(targetRS, imc->ocolorants[i], &colorant);

        imc->htDoForms[i] = (gucr_halftoning(targetRS) &&
                             colorant != NULL &&
                             gucr_colorantDescription(colorant, &info));
      }
      ht_setupTransforms(imc->spotno, imc->reprotype, imc->oncomps, imc->ocolorants,
                         targetRS, imc->htTransformInfo) ;
    }
  }
  imc->justDecode = justDecode ;
  imc->out16 = out16 ;
  imc->top8 = imc->htTransformInfo == NULL ;
  imc->method = method ;
  imc->colorType = colorType ;

  imc->cvtsize = ( out16 ? 2 : 1 ) * imc->oncomps * nconv ;
  imc->coldec = dl_alloc(pools, GSC_BLOCK_MAXCOLORS * incomps * sizeof(int32),
                         MM_ALLOC_CLASS_IMAGE_CONVERT);
  imc->colcvt = dl_alloc(pools,
                         GSC_BLOCK_MAXCOLORS * imc->oncomps * sizeof(COLORVALUE),
                         MM_ALLOC_CLASS_IMAGE_CONVERT);
  imc->cvtbuf = dl_alloc(pools, imc->cvtsize , MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( imc->coldec == NULL || imc->colcvt == NULL || imc->cvtbuf == NULL ) {
    (void)error_handler(VMERROR);
    goto cleanup;
  }

  result = TRUE;
 cleanup:
  if ( !result ) {
    im_colcvtfree( imc );
    imc = NULL;
  }
#undef return
  return imc ;
}

void im_colcvtfree( IM_COLCVT *imc )
{
  HQASSERT( imc , "imc NULL in im_colcvtfree" ) ;
  if ( imc->coldec )
    dl_free(imc->pools, imc->coldec,
            GSC_BLOCK_MAXCOLORS * imc->incomps * sizeof(int32),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( imc->colcvt )
    dl_free(imc->pools, imc->colcvt,
            GSC_BLOCK_MAXCOLORS * imc->oncomps * sizeof(COLORVALUE),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( imc->cvtbuf )
    dl_free(imc->pools, imc->cvtbuf, imc->cvtsize,
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( imc->htDoForms )
    dl_free(imc->pools, imc->htDoForms,
            imc->oncomps * sizeof(Bool),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( imc->htTransformInfo )
    dl_free(imc->pools, imc->htTransformInfo,
            imc->oncomps * sizeof(HT_TRANSFORM_INFO),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
  dl_free(imc->pools, imc, sizeof(IM_COLCVT),
          MM_ALLOC_CLASS_IMAGE_CONVERT);
}

Bool im_colcvt_n( IM_COLCVT *imc , int32 *ubuf , uint8 **rbuf , uint32 nsamples )
{
  Bool retval;
  int32 saved_n = imc->nconv;

  imc->nconv = nsamples;
  retval = im_colcvt(imc, ubuf, rbuf);
  imc->nconv = saved_n;

  return retval;
}

Bool im_colcvt( IM_COLCVT *imc , int32 *ubuf , uint8 **rbuf )
{
  int32 nconv = imc->nconv , iconv = nconv ;
  int32 incomps = imc->incomps , oncomps = imc->oncomps ;
  uint8 *wrkbuf = imc->cvtbuf ;
  COLORVALUE *colcvt = imc->colcvt ;
  int32 *unpack = ubuf , *decode = imc->coldec ;

  HQASSERT( GSC_USE_FASTRGB2GRAY == imc->method || imc->ndecodes ||
            GSC_USE_FASTRGB2CMYK == imc->method,
            "ndecodes NULL in im_colcvt" ) ;
  HQASSERT( ubuf , "ubuf NULL in im_colcvt" ) ;
  HQASSERT( imc->nconv > 0 , "im_colcvt called with <= 0 bytes" ) ;
  HQASSERT( imc->cvtbuf , "cvtbuf NULL in im_colcvt" ) ;

  /* Go through the scanline's worth of data, limited to processing only
     GSC_BLOCK_MAXCOLORS values at a time. */
  while ( iconv > 0 ) {
    int32 i, tconv = iconv > GSC_BLOCK_MAXCOLORS ? GSC_BLOCK_MAXCOLORS : iconv ;

    /* Apply decodes array to unpacked buf and store result in 'decode'. */
    if (imc->justDecode ||
        imc->method == GSC_USE_INTERPOLATION ||
        imc->method == GSC_USE_PS_PROC) {
      im_decode( imc->ndecodes, unpack, decode, incomps, tconv );
    }

    if ( imc->justDecode ) {
      /* justDecode is used for alpha-channels and image filtering. */
      int32 nvalues = tconv * incomps;
      float *fdecode = (float*)decode;
      if ( imc->out16 ) {
        for ( i = 0; i < nvalues; ++i )
          colcvt[i] = FLOAT_TO_COLORVALUE(fdecode[i]);
      } else { /* 8 bit values stored in 16 bit containers */
        for ( i = 0; i < nvalues; ++i )
          colcvt[i] = (COLORVALUE)(COLORVALUE_TO_UINT8(FLOAT_TO_COLORVALUE(fdecode[i])));
      }
      HQASSERT(oncomps == incomps, "oncomps and incomps must match for im_planar");
    } else {
      /* The normal image color-conversion methods. */
      switch ( imc->method ) {
      case GSC_USE_PS_PROC:
        if ( !gsc_invokeChainBlock( imc->colorInfo , imc->colorType ,
                                    ( float * )decode , colcvt , tconv ) )
          return FALSE;
        break;
      case GSC_USE_INTERPOLATION:
        if ( !gsc_invokeChainBlockViaTable( imc->colorInfo , imc->colorType ,
                                            decode , colcvt , tconv ) )
          return FALSE;
        /* If quantisation was included in the invocation then the halftone
           cache needs populating with the interpolated colors. */
        if ( imc->htTransformInfo == NULL )
          if ( !gsc_populateHalftoneCache( imc->colorInfo , imc->colorType ,
                                           colcvt , tconv ) )
            return FALSE;
        break;
      case GSC_USE_FASTRGB2CMYK:
        if (!gsc_fastrgb2cmyk_do(imc->colorInfo, imc->colorType, unpack, colcvt,
                                 tconv)) {
          return (FALSE);
        }
        if (imc->htTransformInfo == NULL) {
          if (!gsc_populateHalftoneCache(imc->colorInfo, imc->colorType, colcvt,
                                         tconv)) {
            return (FALSE);
          }
        }
        break;
      case GSC_USE_FASTRGB2GRAY:
        if ( !gsc_fastrgb2gray_do( imc->colorInfo , imc->colorType ,
                                   unpack , colcvt , tconv ) )
          return FALSE;
        break;
      default:
        HQFAIL("Unrecognised color-conversion method") ;
      }

      /* Quantise colors to device codes, but only if this is a back-end chain
         to final device space.  Doing quantisation outside of Tom's Tables
         ensures consistency between direct- and backdrop-rendered regions. */
      if ( imc->htTransformInfo != NULL ) {
        ht_doTransformsMultiAll(imc->oncomps, colcvt, imc->htTransformInfo,
                                colcvt, tconv);
        for ( i = 0; i < imc->oncomps; ++i )
          if ( imc->htDoForms[i] &&
               ht_allocateForm(imc->page->eraseno, imc->spotno, imc->reprotype,
                               imc->ocolorants[i], tconv, &colcvt[i],
                               imc->oncomps, NULL) )
            imc->htDoForms[i] = FALSE; /* all forms now allocated */
      }
    }

    /* Having color converted a load of colors, we now need to planar them. */
    im_planar(colcvt, oncomps, tconv, wrkbuf, nconv, imc->out16, imc->top8) ;

    /* Note we really do only need to increment wrkbuf like this.
       wrkbuf gets planarized results, so after doing tconv samples we have a buffer
       with something like CCCC...MMMM....YYYY....KKKK.... we need to increment
       to point to the next unfilled C */
    wrkbuf += ( imc->out16 ? 2 * tconv : tconv ) ;
    unpack += ( tconv * incomps ) ;
    iconv -= tconv ;
    SwOftenUnsafe() ;
  }

  *rbuf = imc->cvtbuf ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
void im_coldecode( IM_COLCVT *imc , int32 *ubuf )
{
  int32 *unpack , *decode ;

  HQASSERT( imc , "imc NULL in im_colorcvt" ) ;
  HQASSERT( imc->ndecodes , "ndecodes NULL in im_colorcvt" ) ;
  HQASSERT( ubuf , "ubuf NULL in im_colorcvt" ) ;
  HQASSERT( imc->nconv > 0 , "im_colorcvt called with <= 0 bytes" ) ;
  HQASSERT( imc->cvtbuf , "cvtbuf NULL in im_colorcvt" ) ;

  unpack = ubuf ;
  decode = ubuf ;

  im_decode( imc->ndecodes , unpack , decode , imc->incomps , imc->nconv ) ;
}

/* -------------------------------------------------------------------------- */
static void im_decode1( int32 *unpack , int32 *decode , int32 nconv ,
                        int32 *decode1 )
{
  while (( nconv -= 8 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode1[ unpack[ 1 ]] ;
    decode[ 2 ] = decode1[ unpack[ 2 ]] ;
    decode[ 3 ] = decode1[ unpack[ 3 ]] ;
    decode[ 4 ] = decode1[ unpack[ 4 ]] ;
    decode[ 5 ] = decode1[ unpack[ 5 ]] ;
    decode[ 6 ] = decode1[ unpack[ 6 ]] ;
    decode[ 7 ] = decode1[ unpack[ 7 ]] ;
    unpack += 8 ;
    decode += 8 ;
  }
  nconv += 8 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += 1 ;
    decode += 1 ;
  }
}

#ifdef IM_16BPP
static void im_decodeN1( int32 *unpack , int32 *decode , int32 nconv ,
                         int32 incomps ,
                         int32 *decode1 )
{
  while (( nconv -= 8 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
  nconv += 8 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
}
#endif

static void im_decode2( int32 *unpack , int32 *decode , int32 nconv ,
                        int32 *decode1 ,
                        int32 *decode2 )
{
  while (( nconv -= 4 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode1[ unpack[ 2 ]] ;
    decode[ 3 ] = decode2[ unpack[ 3 ]] ;
    decode[ 4 ] = decode1[ unpack[ 4 ]] ;
    decode[ 5 ] = decode2[ unpack[ 5 ]] ;
    decode[ 6 ] = decode1[ unpack[ 6 ]] ;
    decode[ 7 ] = decode2[ unpack[ 7 ]] ;
    unpack += 8 ;
    decode += 8 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    unpack += 2 ;
    decode += 2 ;
  }
}

static void im_decodeN2( int32 *unpack , int32 *decode , int32 nconv ,
                         int32 incomps ,
                         int32 *decode1 ,
                         int32 *decode2 )
{
  while (( nconv -= 4 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
}

static void im_decode3( int32 *unpack , int32 *decode , int32 nconv ,
                        int32 *decode1 ,
                        int32 *decode2 ,
                        int32 *decode3 )
{
  while (( nconv -= 3 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    decode[ 3 ] = decode1[ unpack[ 3 ]] ;
    decode[ 4 ] = decode2[ unpack[ 4 ]] ;
    decode[ 5 ] = decode3[ unpack[ 5 ]] ;
    decode[ 6 ] = decode1[ unpack[ 6 ]] ;
    decode[ 7 ] = decode2[ unpack[ 7 ]] ;
    decode[ 8 ] = decode3[ unpack[ 8 ]] ;
    unpack += 9 ;
    decode += 9 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    unpack += 3 ;
    decode += 3 ;
  }
}

static void im_decodeN3( int32 *unpack , int32 *decode , int32 nconv ,
                         int32 incomps ,
                         int32 *decode1 ,
                         int32 *decode2 ,
                         int32 *decode3 )
{
  while (( nconv -= 3 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
}

static void im_decode4( int32 *unpack , int32 *decode , int32 nconv ,
                        int32 *decode1 ,
                        int32 *decode2 ,
                        int32 *decode3 ,
                        int32 *decode4 )
{
  while (( nconv -= 2 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    decode[ 3 ] = decode4[ unpack[ 3 ]] ;
    decode[ 4 ] = decode1[ unpack[ 4 ]] ;
    decode[ 5 ] = decode2[ unpack[ 5 ]] ;
    decode[ 6 ] = decode3[ unpack[ 6 ]] ;
    decode[ 7 ] = decode4[ unpack[ 7 ]] ;
    unpack += 8 ;
    decode += 8 ;
  }
  nconv += 2 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    decode[ 3 ] = decode4[ unpack[ 3 ]] ;
    unpack += 4 ;
    decode += 4 ;
  }
}

static void im_decodeN4( int32 *unpack , int32 *decode , int32 nconv ,
                         int32 incomps ,
                         int32 *decode1 ,
                         int32 *decode2 ,
                         int32 *decode3 ,
                         int32 *decode4 )
{
  while (( nconv -= 2 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    decode[ 3 ] = decode4[ unpack[ 3 ]] ;
    unpack += incomps ;
    decode += incomps ;
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    decode[ 3 ] = decode4[ unpack[ 3 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
  nconv += 2 ;
  while (( nconv -= 1 ) >= 0 ) {
    decode[ 0 ] = decode1[ unpack[ 0 ]] ;
    decode[ 1 ] = decode2[ unpack[ 1 ]] ;
    decode[ 2 ] = decode3[ unpack[ 2 ]] ;
    decode[ 3 ] = decode4[ unpack[ 3 ]] ;
    unpack += incomps ;
    decode += incomps ;
  }
}

/* -------------------------------------------------------------------------- */
/* Apply N Decode arrays (e.g. Hex == 6); do in groups of 4. */
void im_decode( int32 *ndecodes[] ,
                int32 *unpack , int32 *decode ,
                int32 incomps , int32 nconv )
{
  switch ( incomps ) {
    int32 i , n ;
  case 1:
    im_decode1( unpack , decode , nconv , ndecodes[ 0 ] ) ;
    break ;
  case 2:
    im_decode2( unpack , decode , nconv , ndecodes[ 0 ] , ndecodes[ 1 ] ) ;
    break ;
  case 3:
    im_decode3( unpack , decode , nconv , ndecodes[ 0 ] , ndecodes[ 1 ] ,
                ndecodes[ 2 ] ) ;
    break ;
  case 4:
    im_decode4( unpack , decode , nconv , ndecodes[ 0 ] , ndecodes[ 1 ] ,
                ndecodes[ 2 ] , ndecodes[ 3 ] ) ;
    break ;
  default:
    n = ( incomps - 4 ) & (~3) ;
    if ( incomps - n == 4 || incomps - n == 7 )
      n += 4 ;
    for ( i = 0 ; i < n ; i += 4 ) {
      int32 *tunpack , *tdecode ;
      tunpack = unpack + i ;
      tdecode = decode + i ;
      im_decodeN4( tunpack , tdecode , nconv ,
                   incomps ,
                   ndecodes[ i + 0 ] ,
                   ndecodes[ i + 1 ] ,
                   ndecodes[ i + 2 ] ,
                   ndecodes[ i + 3 ] ) ;
    }
    while ( i + 3 <= incomps ) {
      int32 *tunpack , *tdecode ;
      tunpack = unpack + i ;
      tdecode = decode + i ;
      im_decodeN3( tunpack , tdecode , nconv ,
                   incomps ,
                   ndecodes[ i + 0 ] ,
                   ndecodes[ i + 1 ] ,
                   ndecodes[ i + 2 ] ) ;
      i += 3 ;
    }
    if ( i < incomps ) {
      int32 *tunpack , *tdecode ;
      tunpack = unpack + i ;
      tdecode = decode + i ;
      im_decodeN2( tunpack , tdecode , nconv ,
                   incomps ,
                   ndecodes[ i + 0 ] ,
                   ndecodes[ i + 1 ] ) ;
    }
  }
}

/* -------------------------------------------------------------------------- */
static void im_planar1_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst )
{
  while (( nconv -= 8 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst + 7 ) ;
    dst[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst[ 1 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst[ 2 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst[ 3 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ;
    dst[ 4 ] = COLORVALUE_TO_UINT8(src[ 4 ]) ;
    dst[ 5 ] = COLORVALUE_TO_UINT8(src[ 5 ]) ;
    dst[ 6 ] = COLORVALUE_TO_UINT8(src[ 6 ]) ;
    dst[ 7 ] = COLORVALUE_TO_UINT8(src[ 7 ]) ;
    dst += 8 ;
    src += 8 ;
  }
  nconv += 8 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst += 1 ;
    src += 1 ;
  }
}

static void im_planar2_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                               int32 offset )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst1[ 1 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst2[ 1 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ;
    dst1[ 2 ] = COLORVALUE_TO_UINT8(src[ 4 ]) ;
    dst2[ 2 ] = COLORVALUE_TO_UINT8(src[ 5 ]) ;
    dst1[ 3 ] = COLORVALUE_TO_UINT8(src[ 6 ]) ;
    dst2[ 3 ] = COLORVALUE_TO_UINT8(src[ 7 ]) ;
    dst1 += 4 ;
    dst2 += 4 ;
    src += 8 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst1 += 1 ;
    dst2 += 1 ;
    src += 2 ;
  }
}

static void im_planar3_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                               int32 offset )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[  0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[  1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[  2 ]) ;
    dst1[ 1 ] = COLORVALUE_TO_UINT8(src[  3 ]) ;
    dst2[ 1 ] = COLORVALUE_TO_UINT8(src[  4 ]) ;
    dst3[ 1 ] = COLORVALUE_TO_UINT8(src[  5 ]) ;
    dst1[ 2 ] = COLORVALUE_TO_UINT8(src[  6 ]) ;
    dst2[ 2 ] = COLORVALUE_TO_UINT8(src[  7 ]) ;
    dst3[ 2 ] = COLORVALUE_TO_UINT8(src[  8 ]) ;
    dst1[ 3 ] = COLORVALUE_TO_UINT8(src[  9 ]) ;
    dst2[ 3 ] = COLORVALUE_TO_UINT8(src[ 10 ]) ;
    dst3[ 3 ] = COLORVALUE_TO_UINT8(src[ 11 ]) ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
    src += 12 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    src += 3 ;
  }
}

static void im_planar4_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                               int32 offset )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;
  uint8 *dst4 = dst3 + offset ;

  while (( nconv -= 3 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[  0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[  1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[  2 ]) ;
    dst4[ 0 ] = COLORVALUE_TO_UINT8(src[  3 ]) ;
    dst1[ 1 ] = COLORVALUE_TO_UINT8(src[  4 ]) ;
    dst2[ 1 ] = COLORVALUE_TO_UINT8(src[  5 ]) ;
    dst3[ 1 ] = COLORVALUE_TO_UINT8(src[  6 ]) ;
    dst4[ 1 ] = COLORVALUE_TO_UINT8(src[  7 ]) ;
    dst1[ 2 ] = COLORVALUE_TO_UINT8(src[  8 ]) ;
    dst2[ 2 ] = COLORVALUE_TO_UINT8(src[  9 ]) ;
    dst3[ 2 ] = COLORVALUE_TO_UINT8(src[ 10 ]) ;
    dst4[ 2 ] = COLORVALUE_TO_UINT8(src[ 11 ]) ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
    src += 12 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst4[ 0 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
    src += 4 ;
  }
}

static void im_planar2N_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                int32 offset , int32 oncomps )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ; src += oncomps ;
    dst1[ 1 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 1 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ; src += oncomps ;
    dst1[ 2 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 2 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ; src += oncomps ;
    dst1[ 3 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 3 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ; src += oncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
  }
}

static void im_planar3N_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                int32 offset , int32 oncomps )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ; src += oncomps ;
    dst1[ 1 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 1 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 1 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ; src += oncomps ;
    dst1[ 2 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 2 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 2 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ; src += oncomps ;
    dst1[ 3 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 3 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 3 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ; src += oncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
  }
}

static void im_planar4N_8_top8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                int32 offset , int32 oncomps )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;
  uint8 *dst4 = dst3 + offset ;

  while (( nconv -= 3 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst4[ 0 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ; src += oncomps ;
    dst1[ 1 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 1 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 1 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst4[ 1 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ; src += oncomps ;
    dst1[ 2 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 2 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 2 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst4[ 2 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ; src += oncomps ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = COLORVALUE_TO_UINT8(src[ 0 ]) ;
    dst2[ 0 ] = COLORVALUE_TO_UINT8(src[ 1 ]) ;
    dst3[ 0 ] = COLORVALUE_TO_UINT8(src[ 2 ]) ;
    dst4[ 0 ] = COLORVALUE_TO_UINT8(src[ 3 ]) ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
static void im_planar1_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst )
{
  while (( nconv -= 8 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst + 7 ) ;
    dst[ 0 ] = ( uint8 )src[ 0 ] ;
    dst[ 1 ] = ( uint8 )src[ 1 ] ;
    dst[ 2 ] = ( uint8 )src[ 2 ] ;
    dst[ 3 ] = ( uint8 )src[ 3 ] ;
    dst[ 4 ] = ( uint8 )src[ 4 ] ;
    dst[ 5 ] = ( uint8 )src[ 5 ] ;
    dst[ 6 ] = ( uint8 )src[ 6 ] ;
    dst[ 7 ] = ( uint8 )src[ 7 ] ;
    dst += 8 ;
    src += 8 ;
  }
  nconv += 8 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst[ 0 ] = ( uint8 )src[ 0 ] ;
    dst += 1 ;
    src += 1 ;
  }
}

static void im_planar2_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                int32 offset )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst1[ 1 ] = ( uint8 )src[ 2 ] ;
    dst2[ 1 ] = ( uint8 )src[ 3 ] ;
    dst1[ 2 ] = ( uint8 )src[ 4 ] ;
    dst2[ 2 ] = ( uint8 )src[ 5 ] ;
    dst1[ 3 ] = ( uint8 )src[ 6 ] ;
    dst2[ 3 ] = ( uint8 )src[ 7 ] ;
    dst1 += 4 ;
    dst2 += 4 ;
    src += 8 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    src += 2 ;
  }
}

static void im_planar3_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                int32 offset )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = ( uint8 )src[  0 ] ;
    dst2[ 0 ] = ( uint8 )src[  1 ] ;
    dst3[ 0 ] = ( uint8 )src[  2 ] ;
    dst1[ 1 ] = ( uint8 )src[  3 ] ;
    dst2[ 1 ] = ( uint8 )src[  4 ] ;
    dst3[ 1 ] = ( uint8 )src[  5 ] ;
    dst1[ 2 ] = ( uint8 )src[  6 ] ;
    dst2[ 2 ] = ( uint8 )src[  7 ] ;
    dst3[ 2 ] = ( uint8 )src[  8 ] ;
    dst1[ 3 ] = ( uint8 )src[  9 ] ;
    dst2[ 3 ] = ( uint8 )src[ 10 ] ;
    dst3[ 3 ] = ( uint8 )src[ 11 ] ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
    src += 12 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst3[ 0 ] = ( uint8 )src[ 2 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    src += 3 ;
  }
}

static void im_planar4_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                int32 offset )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;
  uint8 *dst4 = dst3 + offset ;

  while (( nconv -= 3 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = ( uint8 )src[  0 ] ;
    dst2[ 0 ] = ( uint8 )src[  1 ] ;
    dst3[ 0 ] = ( uint8 )src[  2 ] ;
    dst4[ 0 ] = ( uint8 )src[  3 ] ;
    dst1[ 1 ] = ( uint8 )src[  4 ] ;
    dst2[ 1 ] = ( uint8 )src[  5 ] ;
    dst3[ 1 ] = ( uint8 )src[  6 ] ;
    dst4[ 1 ] = ( uint8 )src[  7 ] ;
    dst1[ 2 ] = ( uint8 )src[  8 ] ;
    dst2[ 2 ] = ( uint8 )src[  9 ] ;
    dst3[ 2 ] = ( uint8 )src[ 10 ] ;
    dst4[ 2 ] = ( uint8 )src[ 11 ] ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
    src += 12 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst3[ 0 ] = ( uint8 )src[ 2 ] ;
    dst4[ 0 ] = ( uint8 )src[ 3 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
    src += 4 ;
  }
}

static void im_planar2N_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                 int32 offset , int32 oncomps )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ; src += oncomps ;
    dst1[ 1 ] = ( uint8 )src[ 0 ] ;
    dst2[ 1 ] = ( uint8 )src[ 1 ] ; src += oncomps ;
    dst1[ 2 ] = ( uint8 )src[ 0 ] ;
    dst2[ 2 ] = ( uint8 )src[ 1 ] ; src += oncomps ;
    dst1[ 3 ] = ( uint8 )src[ 0 ] ;
    dst2[ 3 ] = ( uint8 )src[ 1 ] ; src += oncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
  }
}

static void im_planar3N_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                 int32 offset , int32 oncomps )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst3[ 0 ] = ( uint8 )src[ 2 ] ; src += oncomps ;
    dst1[ 1 ] = ( uint8 )src[ 0 ] ;
    dst2[ 1 ] = ( uint8 )src[ 1 ] ;
    dst3[ 1 ] = ( uint8 )src[ 2 ] ; src += oncomps ;
    dst1[ 2 ] = ( uint8 )src[ 0 ] ;
    dst2[ 2 ] = ( uint8 )src[ 1 ] ;
    dst3[ 2 ] = ( uint8 )src[ 2 ] ; src += oncomps ;
    dst1[ 3 ] = ( uint8 )src[ 0 ] ;
    dst2[ 3 ] = ( uint8 )src[ 1 ] ;
    dst3[ 3 ] = ( uint8 )src[ 2 ] ; src += oncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst3[ 0 ] = ( uint8 )src[ 2 ] ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
  }
}

static void im_planar4N_bottom8( COLORVALUE *src , int32 nconv , uint8 *dst ,
                                 int32 offset , int32 oncomps )
{
  uint8 *dst1 = dst ;
  uint8 *dst2 = dst1 + offset ;
  uint8 *dst3 = dst2 + offset ;
  uint8 *dst4 = dst3 + offset ;

  while (( nconv -= 3 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst3[ 0 ] = ( uint8 )src[ 2 ] ;
    dst4[ 0 ] = ( uint8 )src[ 3 ] ; src += oncomps ;
    dst1[ 1 ] = ( uint8 )src[ 0 ] ;
    dst2[ 1 ] = ( uint8 )src[ 1 ] ;
    dst3[ 1 ] = ( uint8 )src[ 2 ] ;
    dst4[ 1 ] = ( uint8 )src[ 3 ] ; src += oncomps ;
    dst1[ 2 ] = ( uint8 )src[ 0 ] ;
    dst2[ 2 ] = ( uint8 )src[ 1 ] ;
    dst3[ 2 ] = ( uint8 )src[ 2 ] ;
    dst4[ 2 ] = ( uint8 )src[ 3 ] ; src += oncomps ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint8 )src[ 0 ] ;
    dst2[ 0 ] = ( uint8 )src[ 1 ] ;
    dst3[ 0 ] = ( uint8 )src[ 2 ] ;
    dst4[ 0 ] = ( uint8 )src[ 3 ] ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
static void im_planar2_16( COLORVALUE *src , int32 nconv , uint16 *dst ,
                           int32 offset )
{
  uint16 *dst1 = dst ;
  uint16 *dst2 = dst1 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst1[ 1 ] = ( uint16 )src[ 2 ] ;
    dst2[ 1 ] = ( uint16 )src[ 3 ] ;
    dst1[ 2 ] = ( uint16 )src[ 4 ] ;
    dst2[ 2 ] = ( uint16 )src[ 5 ] ;
    dst1[ 3 ] = ( uint16 )src[ 6 ] ;
    dst2[ 3 ] = ( uint16 )src[ 7 ] ;
    dst1 += 4 ;
    dst2 += 4 ;
    src += 8 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    src += 2 ;
  }
}

static void im_planar3_16( COLORVALUE *src , int32 nconv , uint16 *dst ,
                           int32 offset )
{
  uint16 *dst1 = dst ;
  uint16 *dst2 = dst1 + offset ;
  uint16 *dst3 = dst2 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = ( uint16 )src[  0 ] ;
    dst2[ 0 ] = ( uint16 )src[  1 ] ;
    dst3[ 0 ] = ( uint16 )src[  2 ] ;
    dst1[ 1 ] = ( uint16 )src[  3 ] ;
    dst2[ 1 ] = ( uint16 )src[  4 ] ;
    dst3[ 1 ] = ( uint16 )src[  5 ] ;
    dst1[ 2 ] = ( uint16 )src[  6 ] ;
    dst2[ 2 ] = ( uint16 )src[  7 ] ;
    dst3[ 2 ] = ( uint16 )src[  8 ] ;
    dst1[ 3 ] = ( uint16 )src[  9 ] ;
    dst2[ 3 ] = ( uint16 )src[ 10 ] ;
    dst3[ 3 ] = ( uint16 )src[ 11 ] ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
    src += 12 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst3[ 0 ] = ( uint16 )src[ 2 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    src += 3 ;
  }
}

static void im_planar4_16( COLORVALUE *src , int32 nconv , uint16 *dst ,
                           int32 offset )
{
  uint16 *dst1 = dst ;
  uint16 *dst2 = dst1 + offset ;
  uint16 *dst3 = dst2 + offset ;
  uint16 *dst4 = dst3 + offset ;

  while (( nconv -= 3 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = ( uint16 )src[  0 ] ;
    dst2[ 0 ] = ( uint16 )src[  1 ] ;
    dst3[ 0 ] = ( uint16 )src[  2 ] ;
    dst4[ 0 ] = ( uint16 )src[  3 ] ;
    dst1[ 1 ] = ( uint16 )src[  4 ] ;
    dst2[ 1 ] = ( uint16 )src[  5 ] ;
    dst3[ 1 ] = ( uint16 )src[  6 ] ;
    dst4[ 1 ] = ( uint16 )src[  7 ] ;
    dst1[ 2 ] = ( uint16 )src[  8 ] ;
    dst2[ 2 ] = ( uint16 )src[  9 ] ;
    dst3[ 2 ] = ( uint16 )src[ 10 ] ;
    dst4[ 2 ] = ( uint16 )src[ 11 ] ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
    src += 12 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst3[ 0 ] = ( uint16 )src[ 2 ] ;
    dst4[ 0 ] = ( uint16 )src[ 3 ] ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
    src += 4 ;
  }
}

static void im_planar2N_16( COLORVALUE *src , int32 nconv , uint16 *dst ,
                            int32 offset , int32 oncomps )
{
  uint16 *dst1 = dst ;
  uint16 *dst2 = dst1 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ; src += oncomps ;
    dst1[ 1 ] = ( uint16 )src[ 0 ] ;
    dst2[ 1 ] = ( uint16 )src[ 1 ] ; src += oncomps ;
    dst1[ 2 ] = ( uint16 )src[ 0 ] ;
    dst2[ 2 ] = ( uint16 )src[ 1 ] ; src += oncomps ;
    dst1[ 3 ] = ( uint16 )src[ 0 ] ;
    dst2[ 3 ] = ( uint16 )src[ 1 ] ; src += oncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
  }
}

static void im_planar3N_16( COLORVALUE *src , int32 nconv , uint16 *dst ,
                            int32 offset , int32 oncomps )
{
  uint16 *dst1 = dst ;
  uint16 *dst2 = dst1 + offset ;
  uint16 *dst3 = dst2 + offset ;

  while (( nconv -= 4 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 3 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 3 ) ;
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst3[ 0 ] = ( uint16 )src[ 2 ] ; src += oncomps ;
    dst1[ 1 ] = ( uint16 )src[ 0 ] ;
    dst2[ 1 ] = ( uint16 )src[ 1 ] ;
    dst3[ 1 ] = ( uint16 )src[ 2 ] ; src += oncomps ;
    dst1[ 2 ] = ( uint16 )src[ 0 ] ;
    dst2[ 2 ] = ( uint16 )src[ 1 ] ;
    dst3[ 2 ] = ( uint16 )src[ 2 ] ; src += oncomps ;
    dst1[ 3 ] = ( uint16 )src[ 0 ] ;
    dst2[ 3 ] = ( uint16 )src[ 1 ] ;
    dst3[ 3 ] = ( uint16 )src[ 2 ] ; src += oncomps ;
    dst1 += 4 ;
    dst2 += 4 ;
    dst3 += 4 ;
  }
  nconv += 4 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst3[ 0 ] = ( uint16 )src[ 2 ] ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
  }
}

static void im_planar4N_16( COLORVALUE *src , int32 nconv , uint16 *dst ,
                            int32 offset , int32 oncomps )
{
  uint16 *dst1 = dst ;
  uint16 *dst2 = dst1 + offset ;
  uint16 *dst3 = dst2 + offset ;
  uint16 *dst4 = dst3 + offset ;

  while (( nconv -= 3 ) >= 0 ) {
    PENTIUM_CACHE_LOAD( dst1 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst2 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst3 + 2 ) ;
    PENTIUM_CACHE_LOAD( dst4 + 2 ) ;
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst3[ 0 ] = ( uint16 )src[ 2 ] ;
    dst4[ 0 ] = ( uint16 )src[ 3 ] ; src += oncomps ;
    dst1[ 1 ] = ( uint16 )src[ 0 ] ;
    dst2[ 1 ] = ( uint16 )src[ 1 ] ;
    dst3[ 1 ] = ( uint16 )src[ 2 ] ;
    dst4[ 1 ] = ( uint16 )src[ 3 ] ; src += oncomps ;
    dst1[ 2 ] = ( uint16 )src[ 0 ] ;
    dst2[ 2 ] = ( uint16 )src[ 1 ] ;
    dst3[ 2 ] = ( uint16 )src[ 2 ] ;
    dst4[ 2 ] = ( uint16 )src[ 3 ] ; src += oncomps ;
    dst1 += 3 ;
    dst2 += 3 ;
    dst3 += 3 ;
    dst4 += 3 ;
  }
  nconv += 3 ;
  while (( nconv -= 1 ) >= 0 ) {
    dst1[ 0 ] = ( uint16 )src[ 0 ] ;
    dst2[ 0 ] = ( uint16 )src[ 1 ] ;
    dst3[ 0 ] = ( uint16 )src[ 2 ] ;
    dst4[ 0 ] = ( uint16 )src[ 3 ] ; src += oncomps ;
    dst1 += 1 ;
    dst2 += 1 ;
    dst3 += 1 ;
    dst4 += 1 ;
  }
}

/* -------------------------------------------------------------------------- */
static void im_planar( COLORVALUE *src , int32 oncomps , int32 nconv ,
                       uint8 *dst, int32 offset, Bool out16, Bool top8 )
{
  if ( out16 ) {
    switch ( oncomps ) {
    int32 i , n ;
    case 1:
      HqMemCpy(dst, src, 2*nconv);
      break ;
    case 2:
      im_planar2_16( src , nconv , ( uint16 * )dst , offset ) ;
      break ;
    case 3:
      im_planar3_16( src , nconv , ( uint16 * )dst , offset ) ;
      break ;
    case 4:
      im_planar4_16( src , nconv , ( uint16 * )dst , offset ) ;
      break ;
    default:
      n = ( oncomps - 4 ) & (~3) ;
      if ( oncomps - n == 4 || oncomps - n == 7 )
        n += 4 ;
      for ( i = 0 ; i < n ; i += 4 )
        im_planar4N_16( src + i , nconv ,
                        ( uint16 * )dst + i * offset ,
                        offset ,
                        oncomps ) ;
      while ( i + 3 <= oncomps ) {
        im_planar3N_16( src + i , nconv ,
                        ( uint16 * )dst + i * offset ,
                        offset ,
                        oncomps ) ;
        i += 3 ;
      }
      if ( i < oncomps )
        im_planar2N_16( src + i , nconv ,
                        ( uint16 * )dst + i * offset ,
                        offset ,
                        oncomps ) ;
    }
  }
  else if ( top8 ) {
    switch ( oncomps ) {
    int32 i , n ;
    case 1:
      im_planar1_8_top8( src , nconv , dst ) ;
      break ;
    case 2:
      im_planar2_8_top8( src , nconv , dst , offset ) ;
      break ;
    case 3:
      im_planar3_8_top8( src , nconv , dst , offset ) ;
      break ;
    case 4:
      im_planar4_8_top8( src , nconv , dst , offset ) ;
      break ;
    default:
      n = ( oncomps - 4 ) & (~3) ;
      if ( oncomps - n == 4 || oncomps - n == 7 )
        n += 4 ;
      for ( i = 0 ; i < n ; i += 4 )
        im_planar4N_8_top8( src + i , nconv ,
                            dst + i * offset ,
                            offset ,
                            oncomps ) ;
      while ( i + 3 <= oncomps ) {
        im_planar3N_8_top8( src + i , nconv ,
                            dst + i * offset ,
                            offset ,
                            oncomps ) ;
        i += 3 ;
      }
      if ( i < oncomps )
        im_planar2N_8_top8( src + i , nconv ,
                            dst + i * offset ,
                            offset ,
                            oncomps ) ;
    }
  }
  else {
    switch ( oncomps ) {
    int32 i , n ;
    case 1:
      im_planar1_bottom8( src , nconv , dst ) ;
      break ;
    case 2:
      im_planar2_bottom8( src , nconv , dst , offset ) ;
      break ;
    case 3:
      im_planar3_bottom8( src , nconv , dst , offset ) ;
      break ;
    case 4:
      im_planar4_bottom8( src , nconv , dst , offset ) ;
      break ;
    default:
      n = ( oncomps - 4 ) & (~3) ;
      if ( oncomps - n == 4 || oncomps - n == 7 )
        n += 4 ;
      for ( i = 0 ; i < n ; i += 4 )
        im_planar4N_bottom8( src + i , nconv ,
                             dst + i * offset ,
                             offset ,
                             oncomps ) ;
      while ( i + 3 <= oncomps ) {
        im_planar3N_bottom8( src + i , nconv ,
                             dst + i * offset ,
                             offset ,
                             oncomps ) ;
        i += 3 ;
      }
      if ( i < oncomps )
        im_planar2N_bottom8( src + i , nconv ,
                             dst + i * offset ,
                             offset ,
                             oncomps ) ;
    }
  }
}

/* Log stripped */
