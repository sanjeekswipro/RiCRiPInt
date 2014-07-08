/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:cvcolcvt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Converts a buffer of COLORVALUEs between color spaces.  The color chain
 * specified by colorInfo and colorType must be setup with the right input
 * and output spaces.
 */

#include "core.h"
#include "imcolcvt.h"
#include "cvcolcvt.h"
#include "color.h"
#include "group.h"
#include "gschcms.h"
#include "gu_chan.h"
#include "halftone.h"
#include "often.h"
#include "renderom.h"
#include "swerrors.h"
#include "display.h"
#include "cvdecodes.h"
#include "dl_image.h"
#include "gschtone.h"
#include "context.h" /* CoreContext */
#include "dlstate.h"
#include "mlock.h"
#include "swstart.h"
#include "swtrace.h"
#include "coreinit.h"
#include "gscfastrgb2gray.h"

struct CV_COLCVT {
  mm_pool_t *pools;
  GS_COLORinfo *colorInfo;
  int32 colorType;
  Bool in16, out16;
  uint32 inComps, outComps;
  COLORANTINDEX *outColorants;
  int32 method;
  PACKDATA *packer;
  float **cv_fdecodes;
  int32 **cv_ndecodes;
  uint32 alloc_ncomps;
  int32 *decodedbuf;
  COLORVALUE *convertedbuf; /* only used if out16 is false */
  Bool *htDoForms;
  dl_erase_nr erasenr;
  SPOTNO defaultSpot;
  HTTYPE defaultRepro;
  HT_TRANSFORM_INFO *htTransformInfo;
  DL_OMITDATA *omit_data;
  Bool fromPageGroup;
  LateColorAttrib *lca;
};

/**
 * A mutex to use in cv_colcvt() to lock the use of buffers in CV_COLCVT and
 * also around the color conversion code, which is not thread-safe.
 */
static multi_mutex_t colcvtLock;


static Bool cv_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params);

  multi_mutex_init(&colcvtLock, COLCVT_LOCK_INDEX, FALSE,
                   SW_TRACE_COLCVT_ACQUIRE, SW_TRACE_COLCVT_HOLD);

  return TRUE;
}

static void cv_finish(void)
{
  multi_mutex_finish(&colcvtLock);
}

void cvcolcvt_C_globals(core_init_fns *fns)
{
  fns->swinit = cv_swinit;
  fns->finish = cv_finish;
}

void cvcolcvt_lock(void)
{
  multi_mutex_lock(&colcvtLock);
}

void cvcolcvt_unlock(void)
{
  multi_mutex_unlock(&colcvtLock);
}

CV_COLCVT *cv_colcvtopen(DL_STATE *page, int32 method,
                         GS_COLORinfo *colorInfo, int32 colorType,
                         Bool in16, Bool out16, uint32 inComps,
                         uint32 outComps, COLORANTINDEX *outColorants,
                         Bool fromPageGroup, LateColorAttrib *lca)
{
  Bool result = FALSE;
  CV_COLCVT *converter, init = {0};
  COLORSPACE_ID space = gsc_getcolorspace(colorInfo, colorType);
  GUCR_RASTERSTYLE *targetRS = gsc_getTargetRS(colorInfo);

  HQASSERT(method != GSC_NO_EXTERNAL_METHOD_CHOICE,
           "Method for image color-conversion is not set");
  HQASSERT(method != GSC_QUANTISE_ONLY ||
           !guc_backdropRasterStyle(gsc_getTargetRS(colorInfo)),
           "Device space target required for quantisation");
  HQASSERT(in16 || method == GSC_QUANTISE_ONLY,
           "Don't expect 8 bit input values when color converting");
  HQASSERT(out16 || !guc_backdropRasterStyle(gsc_getTargetRS(colorInfo)),
           "Blend space target should be 16 bit outut");

  converter = dl_alloc(page->dlpools, sizeof(CV_COLCVT),
                       MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( converter == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  *converter = init;
#define return USE_goto_cleanup

  converter->pools = page->dlpools;

  /* Copy the colorInfo to ensure the color space remains unchanged
     between now and cv_colcvt. */
  converter->colorInfo = dl_alloc(page->dlpools, gsc_colorInfoSize(),
                                  MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( converter->colorInfo == NULL ) {
    (void)error_handler(VMERROR);
    goto cleanup;
  }

  if ( !gsc_copycolorinfo_withstate(converter->colorInfo, colorInfo,
                                    page->colorState) )
    goto cleanup;

  converter->colorType = colorType;
  converter->in16 = in16;
  converter->out16 = out16;
  converter->inComps = inComps;
  converter->outComps = outComps;

  /** \todo GSC_USE_FASTRGB2GRAY doesn't work ATM so override with Tom's Tables.
     The fastrgb2gray tables are created and then the color context that
     contains them is destroyed by various gsc functions called from
     cv_coltransform().
     See 368698 for details. */
#define NO_GSC_USE_FASTRGB2GRAY
  if ( method == GSC_USE_FASTRGB2GRAY )
    method = GSC_USE_INTERPOLATION;

  /* Each span in the backdrop contains the colorType from the original DL
     object.  Color conversion for image spans is determined by 'method'
     (probably Tom's Tables) and other types always use GSC_USE_PS_PROC. */
  converter->method = method;

  if ( method != GSC_QUANTISE_ONLY ) {
    /* Unpacks data ready for color conversion. */
    converter->packer = pd_packdataopen(page->dlpools,
                                        inComps, GSC_BLOCK_MAXCOLORS,
                                        16, TRUE, /* already interleaving */
                                        FALSE, /* N/A */ FALSE, /* N/A */
                                        (PD_UNPACK|PD_NATIVE|PD_NOAPPEND));
    if ( !converter->packer )
      goto cleanup;

    /* Decodes array maps from COLORVALUE range to input color values. */
    if ( !cv_alloc_decodes(page, inComps, ColorspaceIsSubtractive(space),
                           &converter->cv_fdecodes, &converter->cv_ndecodes,
                           &converter->alloc_ncomps) )
      goto cleanup;

    converter->decodedbuf =
      dl_alloc(page->dlpools, GSC_BLOCK_MAXCOLORS * inComps * sizeof(int32),
               MM_ALLOC_CLASS_IMAGE_CONVERT);

    if ( converter->decodedbuf == NULL ) {
      (void)error_handler(VMERROR);
      goto cleanup;
    }
  }

#ifdef NO_GSC_USE_FASTRGB2GRAY
  /* RGB2GRAY currently causes corruption (368698) so is not used.
   * Since the branch wont be called don't compile the branch in for now. */
  if ( method == GSC_USE_FASTRGB2GRAY ) {
    /* method applies to GSC_IMAGE only. Other types use gsc_invokeChainBlock. */
    if ( !gsc_fastrgb2gray_prep(converter->colorInfo, colorType, 16,
                                converter->cv_fdecodes[0],
                                converter->cv_fdecodes[1],
                                converter->cv_fdecodes[2]) ) {
      (void)error_handler(VMERROR);
      goto cleanup;
    }
  } else
#endif
  if ( method == GSC_USE_FASTRGB2CMYK ) {
    if ( !gsc_fastrgb2cmyk_prep(converter->colorInfo, 16, NULL, NULL, NULL) ) {
      (void)error_handler(VMERROR);
      goto cleanup;
    }
  }

  if ( fromPageGroup ) {
    /* Set up default transforms for the current spot;
       on-the-fly uses the same spot for all colors. */
    size_t i;

    if ( !out16 )
      converter->convertedbuf =
        dl_alloc(page->dlpools, GSC_BLOCK_MAXCOLORS * outComps * sizeof(COLORVALUE),
                 MM_ALLOC_CLASS_IMAGE_CONVERT);

    converter->outColorants = dl_alloc(page->dlpools,
                                       outComps * sizeof(COLORANTINDEX),
                                       MM_ALLOC_CLASS_IMAGE_CONVERT);

    converter->htDoForms = dl_alloc(page->dlpools,
                                    outComps * sizeof(Bool),
                                    MM_ALLOC_CLASS_IMAGE_CONVERT);

    converter->htTransformInfo =
      dl_alloc(page->dlpools, outComps * sizeof(HT_TRANSFORM_INFO),
               MM_ALLOC_CLASS_IMAGE_CONVERT);

    if ( (!out16 && converter->convertedbuf == NULL) ||
         converter->outColorants == NULL || converter->htDoForms == NULL ||
         converter->htTransformInfo == NULL ) {
      (void)error_handler(VMERROR);
      goto cleanup;
    }
    for ( i = 0 ; i < outComps ; ++i ) {
      GUCR_COLORANT *colorant = NULL;
      const GUCR_COLORANT_INFO *info;

      gucr_colorantHandle(targetRS, outColorants[i], &colorant) ;

      converter->outColorants[i] = outColorants[i];
      converter->htDoForms[i] = (gucr_halftoning(targetRS) &&
                                 colorant != NULL &&
                                 gucr_colorantDescription(colorant, &info));
    }
    converter->erasenr = CoreContext.page->eraseno;
    converter->defaultSpot = gsc_getSpotno(colorInfo);
    converter->defaultRepro = gsc_getRequiredReproType(colorInfo, colorType);
    ht_setupTransforms(converter->defaultSpot, converter->defaultRepro,
                       outComps, outColorants, gsc_getTargetRS(colorInfo),
                       converter->htTransformInfo);

    /* Pre-allocate all the forms to avoid significant contention on the
       halftone lock.  This is only possible if we're just using the halftone
       currently set.  ht_set_all_levels_used is not thread safe and therefore
       can only be called from the interpreter. */
    if ( colorType != GSC_BACKDROP && IS_INTERPRETER() ) {
      ht_defer_allocation();
      for ( i = 0 ; i < outComps ; ++i ) {
        if ( converter->htDoForms[i] ) {
          ht_set_all_levels_used(converter->erasenr, converter->defaultSpot,
                                 converter->defaultRepro, outColorants[i]);
          converter->htDoForms[i] = FALSE;
        }
      }
      ht_resume_allocation(converter->defaultSpot, TRUE);
    }
  }

  converter->omit_data = page->omit_data;
  converter->fromPageGroup = fromPageGroup;
  converter->lca = lca;

  result = TRUE;
 cleanup:
  if ( !result )
    cv_colcvtfree(page, &converter);
#undef return
  return converter;
}

void cv_colcvtfree(DL_STATE *page, CV_COLCVT **freeConverter)
{
  if ( *freeConverter ) {
    CV_COLCVT *converter = *freeConverter;

    if ( converter->colorInfo ) {
      gsc_freecolorinfo(converter->colorInfo);
      dl_free(page->dlpools, converter->colorInfo,
              gsc_colorInfoSize(), MM_ALLOC_CLASS_IMAGE_CONVERT);
    }

    if ( converter->packer )
      pd_packdatafree(converter->packer);

    if ( converter->alloc_ncomps > 0 )
      cv_free_decodes(page, &converter->cv_fdecodes, &converter->cv_ndecodes,
                      &converter->alloc_ncomps);

    if ( converter->decodedbuf )
      dl_free(page->dlpools, converter->decodedbuf,
              GSC_BLOCK_MAXCOLORS * converter->inComps * sizeof(int32),
              MM_ALLOC_CLASS_IMAGE_CONVERT);

    if ( converter->convertedbuf )
      dl_free(page->dlpools, converter->convertedbuf,
              GSC_BLOCK_MAXCOLORS * converter->outComps * sizeof(COLORVALUE),
              MM_ALLOC_CLASS_IMAGE_CONVERT);

    if ( converter->htTransformInfo )
      dl_free(page->dlpools, converter->htTransformInfo,
              converter->outComps * sizeof(HT_TRANSFORM_INFO),
              MM_ALLOC_CLASS_IMAGE_CONVERT);
    if ( converter->htDoForms )
      dl_free(page->dlpools, converter->htDoForms,
              converter->outComps * sizeof(Bool),
              MM_ALLOC_CLASS_IMAGE_CONVERT);

    if ( converter->outColorants )
      dl_free(page->dlpools, converter->outColorants,
              converter->outComps * sizeof(COLORANTINDEX),
              MM_ALLOC_CLASS_IMAGE_CONVERT);

    *freeConverter = NULL;
  }
}

static Bool cv_coltransform(CV_COLCVT *converter, uint32 nColors,
                            COLORINFO *info, COLORVALUE *inColors,
                            COLORVALUE *outColors)
{
  uint32 inComps = converter->inComps, outComps = converter->outComps, i;
  int32 *unpackedbuf, *decodedbuf = converter->decodedbuf;
  int32 chainColorType = converter->colorType;
  uint32 j;

  HQASSERT(nColors <= GSC_BLOCK_MAXCOLORS, "nColors too big");

  /* Unpack COLORVALUEs into 32-bit containers as expected by the color chain. */
  pd_unpack(converter->packer, (void*)&inColors, 1 /* 1 buf means interleaved */,
            &unpackedbuf, nColors);

  for ( i = 0; i < nColors; ) {
    uint32 nSameColorType = nColors;
    int32 spanColorType = chainColorType;

    if ( info ) {
      /* Need to set properties for each set of colors. */
      uint8 spanReproType = info[i].reproType;
      REPRO_COLOR_MODEL spanOrigColorModel = info[i].origColorModel;
      uint8 span_lcmAttribs = info[i].lcmAttribs;
      uint8 spanRenderingIntent = COLORINFO_RENDERING_INTENT(span_lcmAttribs);
      GSC_BLACK_TYPE spanBlackType = COLORINFO_BLACK_TYPE(span_lcmAttribs);
      uint8 spanIndependentChannels = COLORINFO_INDEPENDENT_CHANNELS(span_lcmAttribs);
      LateColorAttrib lca;

      spanColorType = info[i].colorType;
      COLORTYPE_ASSERT(spanColorType, "cv_coltransform");

      /* Coaleasce colors with same properties into a one invoke call. */
      j = i + 1;
      while ( j < nColors &&
              info[j].colorType == spanColorType &&
              info[j].reproType == spanReproType &&
              info[j].origColorModel == spanOrigColorModel &&
              info[j].lcmAttribs == span_lcmAttribs ) {
        ++j;
      }
      nSameColorType = j - i;

      /* Set the intent from the original object. */
      if ( !gsc_setRequiredReproType(converter->colorInfo, chainColorType, spanReproType) )
        return FALSE;

      /* Normally inherit the rendering intent and color model from the
         containing group as per PDFRM. An exception is made for the color
         management of objects and groups painted directly into a non-ICCBased
         page group, and for NamedColor, details in bdt_updatePageGroupLateColor().
         NB. ICCBased page groups have an LCA, non-ICCBased page groups don't. */
      lca.renderingIntent = spanRenderingIntent;
      lca.origColorModel = spanOrigColorModel;
      lca.blackType = spanBlackType;
      lca.independentChannels = spanIndependentChannels;
      if ( converter->lca != NULL ) {
        lca.renderingIntent = converter->lca->renderingIntent;
        if ( lca.origColorModel != REPRO_COLOR_MODEL_NAMED_COLOR )
          lca.origColorModel = converter->lca->origColorModel;
      }
      else
        HQASSERT(converter->fromPageGroup, "Only page groups may not have an lca");

      if ( !groupSetColorAttribs(converter->colorInfo, chainColorType, spanReproType, &lca))
        return FALSE;
    }

    /* It is assumed that imc->method applies only to GSC_IMAGE.
     * The color convertions are done using the GSC_BACKDROP chain, partly to
     * get a consistent colorant set because all overprint issues will have been
     * sorted up front, partly because this colorType handles color values
     * differently in the devicecode link.
     */
    if ( converter->method == GSC_USE_PS_PROC || spanColorType != GSC_IMAGE ) {
      im_decode((int32**)converter->cv_fdecodes, unpackedbuf, decodedbuf,
                inComps, nSameColorType);

      if ( !gsc_invokeChainBlock(converter->colorInfo, chainColorType,
                                 (float *)decodedbuf, outColors, nSameColorType) )
        return FALSE;
    }
    else if ( converter->method == GSC_USE_INTERPOLATION) {
      im_decode(converter->cv_ndecodes, unpackedbuf, decodedbuf,
                inComps, nSameColorType);

      if ( !gsc_invokeChainBlockViaTable(converter->colorInfo, chainColorType,
                                         decodedbuf,
                                         outColors, nSameColorType) )
        return FALSE;

      if ( !gsc_populateHalftoneCache(converter->colorInfo, chainColorType,
                                      outColors, nSameColorType) )
        return FALSE;
    }
#ifdef NO_GSC_USE_FASTRGB2GRAY
    /* RGB2GRAY currently causes corruption (368698) so is not used.
     * Since the branch wont be called don't compile the branch in for now. */
    else if ( converter->method == GSC_USE_FASTRGB2GRAY ) {
      if ( !gsc_fastrgb2gray_do(converter->colorInfo, chainColorType,
                                unpackedbuf, outColors, nSameColorType) )
        return FALSE;
    }
#endif
    else if ( converter->method == GSC_USE_FASTRGB2CMYK ) {
      if ( !gsc_fastrgb2cmyk_do(converter->colorInfo, chainColorType,
                                unpackedbuf, outColors, nSameColorType) )
        return FALSE;

      if ( !gsc_populateHalftoneCache(converter->colorInfo, chainColorType,
                                      outColors, nSameColorType) )
        return FALSE;
    }
    else
      HQFAIL( "Invalid image converter method" );

    if ( info ) {
      /* The independentChannels is only passed back as true to the parent group
         if all child groups also have independent channels. All the colors
         coalesced into this run must be marked. */
      if (!gsc_getfSoftMaskLuminosityChain(converter->colorInfo, chainColorType)) {
        Bool independentChannels;

        if (!gsc_hasIndependentChannels(converter->colorInfo, chainColorType, FALSE,
                                        &independentChannels))
          return FALSE;

        if ( !independentChannels ) {
          for (j = i; j < i + nSameColorType; j++)
            COLORINFO_SET_INDEPENDENT_CHANNELS(info[j].lcmAttribs, independentChannels);
        }
      }
    }

    unpackedbuf += inComps * nSameColorType;
    decodedbuf += inComps * nSameColorType;
    outColors += outComps * nSameColorType;

    i += nSameColorType;
  }
  return TRUE;
}

/* Quantise colors to device codes. */
static Bool cv_quantise(CV_COLCVT *converter,
                        uint32 nColors, COLORINFO *info,
                        COLORVALUE *inColors, COLORVALUE *outColors)
{
  COLORANTINDEX *outColorants = converter->outColorants;
  uint32 outComps = converter->outComps, i;
  HT_TRANSFORM_INFO *htTransformInfo = converter->htTransformInfo;

  if ( info == NULL ) {
    /* All the colors use the default spot and the transforms have already been
       set up accordingly. Omit testing handled elsewhere by client. */
    ht_doTransformsMultiAll(outComps, inColors, htTransformInfo, outColors,
                            nColors);
    for ( i = 0; i < outComps; ++i )
      if ( converter->htDoForms[i] &&
           ht_allocateForm(converter->erasenr, converter->defaultSpot,
                           converter->defaultRepro, outColorants[i],
                           nColors, &outColors[i], outComps, NULL) )
        converter->htDoForms[i] = FALSE; /* all forms now allocated */
  } else {
    /* Each color can specify its own spot, indicated by info. */
    GUCR_RASTERSTYLE *targetRS = gsc_getTargetRS(converter->colorInfo);
    Bool halftoning = gucr_halftoning(targetRS);
    Bool omit = guc_fOmitBlankSeparations(targetRS);

    HQASSERT(targetRS == gsc_getRS(converter->colorInfo),
             "target/device raster styles inconsistent");

    for ( i = 0; i < nColors; ) {
      SPOTNO spotno = info[i].spotNo;
      HTTYPE httype = info[i].reproType;

      /* The conversion is optimised by grouping colors with the same spotno. */
      ht_setupTransforms(spotno, httype, outComps, outColorants,
                         targetRS, htTransformInfo);
      for ( ; i < nColors; inColors += outComps, outColors += outComps, ++i ) {
        Bool maybe_marked = FALSE;

        if ( info[i].label == 0 ) { /* Skip unused color. */
          size_t ocomp;

          for ( ocomp = 0 ; ocomp < outComps ; ++ocomp )
            outColors[ocomp] = 0; /* Need a value acceptable to the 16->8 cast */
          continue;
        }
        if ( spotno != info[i].spotNo || httype != info[i].reproType )
          break; /* Go back to outer loop with new spotno/type */

        if ( omit )
          omit = omit_backdrop_color(converter->omit_data, outComps, outColorants,
                                     inColors, &info[i], &maybe_marked);

        ht_doTransforms(outComps, inColors, htTransformInfo, outColors);

        if ( halftoning ) {
          size_t ocomp;
          for ( ocomp = 0; ocomp < outComps; ++ocomp ) {
            if ( converter->htDoForms[ocomp] ) {
              (void)ht_allocateForm(converter->erasenr, spotno, httype,
                                    outColorants[ocomp], 1, &outColors[ocomp],
                                    outComps, NULL);
              /* Can't set 'htDoForms[ocomp] = FALSE' as above, because there
                 are multiple spotnos here. */
            }
          }
        }

        if ( omit && maybe_marked )
          omit = omit_backdrop_devicecolor(converter->omit_data, outComps,
                                           outColorants, outColors, spotno, httype);
      }
    }
  }
  return TRUE;
}


Bool cv_colcvt(CV_COLCVT *converter, uint32 nColors,
               COLORINFO *info, COLORVALUE *inColors,
               COLORVALUE *outColors, uint8 *outColors8)
{
  Bool result = FALSE;

  HQASSERT(nColors <= GSC_BLOCK_MAXCOLORS, "Limited to GSC_BLOCK_MAXCOLORS");

  /* Lock around use of buffers in CV_COLCVT and also the color conversion
     code which is not thread safe. */
  multi_mutex_lock(&colcvtLock);
#define return USE_goto_cleanup

  /* 16-bit goes direct to output; 8-bit to buffer then packed to output */
  if ( !converter->out16 )
    outColors = converter->convertedbuf;

  if ( converter->method != GSC_QUANTISE_ONLY ) {
    /* Color convert to the next blend space or device space. */
    if ( !cv_coltransform(converter, nColors, info, inColors, outColors) )
      goto cleanup;

    inColors = outColors;
  }

  if ( converter->fromPageGroup )
    /* Quantise to device codes. */
    if ( !cv_quantise(converter, nColors, info, inColors, outColors) )
      goto cleanup;

  if ( !converter->out16 ) { /* Pack 8-bit output */
    size_t i, n = converter->outComps * nColors;
    for ( i = 0; i < n; ++i )
      outColors8[i] = CAST_UNSIGNED_TO_UINT8(outColors[i]);
  }

  SwOftenUnsafe();

  result = TRUE;
 cleanup:
  multi_mutex_unlock(&colcvtLock);
#undef return
  return result;
}

void cv_quantiseplane(CV_COLCVT *converter, uint32 nColors,
                      int plane, unsigned int offset, unsigned int stride,
                      COLORVALUE *inColors,
                      COLORVALUE *outColors, uint8 *outColors8)
{
  COLORANTINDEX ci;
  HT_TRANSFORM_INFO *htTransformInfo;
  Bool *htDoForms;
  COLORVALUE convertedbuf[GSC_BLOCK_MAXCOLORS];
  unsigned int ostride;

  HQASSERT(nColors <= GSC_BLOCK_MAXCOLORS, "Limited to GSC_BLOCK_MAXCOLORS");
  HQASSERT(converter->method == GSC_QUANTISE_ONLY, "Expected to be quantising");
  HQASSERT(plane < (int)converter->outComps, "Bad plane index");
  HQASSERT(stride <= converter->outComps, "Bad stride value");

  /* 16-bit goes direct to output; 8-bit to buffer then packed to output */
  if ( converter->out16 ) {
    outColors += offset;
    ostride = stride;
  } else {
    outColors = convertedbuf;
    ostride = 1; /* convertedbuf is big enough for a stride of one only. */
  }

  ci = converter->outColorants[plane];
  htTransformInfo = &converter->htTransformInfo[plane];
  htDoForms = &converter->htDoForms[plane];

  /* All the colors use the default spot and the transforms have already been
     set up accordingly. */
  if ( converter->in16 )
    ht_doTransformsMulti(nColors, inColors + offset, stride,
                         *htTransformInfo, outColors, ostride);
  else
    ht_doTransformsMulti8(nColors, ((uint8*)inColors) + offset, stride,
                          *htTransformInfo, outColors, ostride);

  if ( *htDoForms &&
       ht_allocateForm(converter->erasenr, converter->defaultSpot,
                       converter->defaultRepro,
                       ci, nColors, outColors, ostride, NULL) )
    *htDoForms = FALSE; /* all forms now allocated */

  if ( !converter->out16 ) { /* Pack 8-bit output */
    outColors8 += offset;
    while ( nColors-- > 0 ) {
      *outColors8 = CAST_UNSIGNED_TO_UINT8(*outColors);
      outColors8 += stride; /* Use final stride here. */
      outColors += ostride;
    }
  }

  SwOftenUnsafe();
}

/* Log stripped */
