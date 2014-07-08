/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:preconvert.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The preconvert code color converts DL objects from their blend spaces to
 * device space ready for direct rendering.
 */

#include "core.h"
#include "swerrors.h"
#include "gs_color.h"
#include "display.h"
#include "preconvert.h"
#include "dlstate.h"
#include "dl_store.h"
#include "gu_chan.h"
#include "group.h"
#include "groupPrivate.h"
#include "gschtone.h"
#include "dl_shade.h"
#include "often.h"
#include "imageadj.h"
#include "hdl.h"
#include "hdlPrivate.h"
#include "dl_bbox.h"
#include "dl_foral.h"
#include "cmpprog.h"
#include "miscops.h"
#include "params.h"
#include "rcbcntrl.h"
#include "spdetect.h"
#include "rcbadjst.h"
#include "region.h"
#include "dl_image.h"
#include "shadesetup.h"
#include "vnobj.h"
#include "shadex.h"
#include "swevents.h"
#include "corejob.h"
#include "cvcolcvt.h"
#include "imexpand.h" /* im_expand_setup_on_the_fly et. al. */
#include "pclAttrib.h" /* dlc_from_packed_color */
#include "imstore.h"

struct Preconvert {
  GS_COLORinfo *colorInfo;
  int32 imageMethod;
  GUCR_RASTERSTYLE *inputRS;
  OBJECT colorSpace;
  COLORSPACE_ID processSpace;
  uint32 nProcessComps;
  uint32 nAllocComps;
  uint32 nCurrentComps;
  USERVALUE *colorantFloats;
  COLORANTINDEX *colorantIndices;
  COLORANTINDEX *unionColorants;
  COLORVALUE *colorantValues;
  COLORANTINDEX *maxBltColorants;
  uint32 nMaxBltColorants;
};

static Bool preconvert_setcolorspace(Preconvert *preconvert,
                                     int32 colorType, Bool overprint,
                                     Bool hasSpots, Bool luminosity);

static Bool preconvert_colorants_alloc(const DL_STATE *page,
                                       Preconvert *preconvert, uint32 nComps)
{
  HQASSERT(preconvert->nAllocComps == 0, "Colorants may already exist");
  preconvert->nAllocComps = nComps;

  preconvert->colorantFloats = dl_alloc(page->dlpools,
                                        sizeof(USERVALUE) * nComps,
                                        MM_ALLOC_CLASS_PRECONVERT);
  preconvert->colorantIndices = dl_alloc(page->dlpools,
                                         sizeof(COLORANTINDEX) * nComps,
                                         MM_ALLOC_CLASS_PRECONVERT);
  preconvert->unionColorants = dl_alloc(page->dlpools,
                                        sizeof(COLORANTINDEX) * nComps,
                                        MM_ALLOC_CLASS_PRECONVERT);
  preconvert->colorantValues = dl_alloc(page->dlpools,
                                        sizeof(COLORVALUE) * nComps,
                                        MM_ALLOC_CLASS_PRECONVERT);

  /* nMaxBltColorants is taken from the device RS. */
  gucr_colorantCount(gsc_getRS(preconvert->colorInfo),
                     &preconvert->nMaxBltColorants);
  preconvert->maxBltColorants =
    dl_alloc(page->dlpools,
             sizeof(COLORANTINDEX) * preconvert->nMaxBltColorants,
             MM_ALLOC_CLASS_PRECONVERT);

  if ( preconvert->colorantFloats == NULL ||
       preconvert->colorantIndices == NULL ||
       preconvert->unionColorants == NULL ||
       preconvert->colorantValues == NULL ||
       preconvert->maxBltColorants == NULL )
    return error_handler(VMERROR);

  /* Load the colorant indices. */
  gucr_colorantIndices(preconvert->inputRS,
                       preconvert->unionColorants, &nComps);
  HQASSERT(nComps == preconvert->nAllocComps, "nAllocComps is out of date");
  return TRUE;
}

/**
 * Create a 'preconvert' object which allows dl colors to be mapped from
 * one blend space to the next.  Each group that applies a different blend
 * space to that of its parent requires a preconvert object.
 */
Bool preconvert_new(DL_STATE *page, GS_COLORinfo *colorInfo, OBJECT colorSpace,
                    COLORSPACE_ID processSpace, uint32 nProcessComps,
                    GUCR_RASTERSTYLE *inputRS, Preconvert **newPreconvert)
{
  Preconvert *preconvert, init = {0};

  if ( inputRS == gsc_getTargetRS(colorInfo) ) {
    /* Don't need preconvert if input and output blend spaces are the same. */
    *newPreconvert = NULL;
    return TRUE;
  }

  preconvert = dl_alloc(page->dlpools, sizeof(Preconvert),
                        MM_ALLOC_CLASS_PRECONVERT);
  if ( preconvert == NULL )
    return error_handler(VMERROR);
  *preconvert = init;

  preconvert->colorInfo = dl_alloc(page->dlpools, gsc_colorInfoSize(),
                                   MM_ALLOC_CLASS_PRECONVERT);
  if ( preconvert->colorInfo == NULL ) {
    preconvert_free(page, &preconvert);
    return error_handler(VMERROR);
  }

  if ( !gsc_copycolorinfo_withstate(preconvert->colorInfo, colorInfo,
                                    page->colorState) ) {
    preconvert_free(page, &preconvert);
    return FALSE;
  }

  Copy(&preconvert->colorSpace, &colorSpace);
  preconvert->imageMethod = GSC_NO_EXTERNAL_METHOD_CHOICE;
  preconvert->processSpace = processSpace;
  preconvert->nProcessComps = nProcessComps;
  preconvert->inputRS = inputRS;
  guc_reserveRasterStyle(preconvert->inputRS);

  if ( !preconvert_update(page, preconvert) ) {
    preconvert_free(page, &preconvert);
    return FALSE;
  }

  *newPreconvert = preconvert;
  return TRUE;
}

static void preconvert_colorants_free(const DL_STATE *page,
                                      Preconvert *preconvert)
{
  if ( preconvert->colorantFloats ) {
    dl_free(page->dlpools, preconvert->colorantFloats,
            sizeof(USERVALUE) * preconvert->nAllocComps,
            MM_ALLOC_CLASS_PRECONVERT);
    preconvert->colorantFloats = NULL;
  }
  if ( preconvert->colorantIndices ) {
    dl_free(page->dlpools, preconvert->colorantIndices,
            sizeof(COLORANTINDEX) * preconvert->nAllocComps,
            MM_ALLOC_CLASS_PRECONVERT);
    preconvert->colorantIndices = NULL;
  }
  if ( preconvert->unionColorants ) {
    dl_free(page->dlpools, preconvert->unionColorants,
            sizeof(COLORANTINDEX) * preconvert->nAllocComps,
            MM_ALLOC_CLASS_PRECONVERT);
    preconvert->unionColorants = NULL;
  }
  if ( preconvert->colorantValues ) {
    dl_free(page->dlpools, preconvert->colorantValues,
            sizeof(COLORVALUE) * preconvert->nAllocComps,
            MM_ALLOC_CLASS_PRECONVERT);
    preconvert->colorantValues = NULL;
  }
  if ( preconvert->maxBltColorants ) {
    dl_free(page->dlpools, preconvert->maxBltColorants,
            sizeof(COLORANTINDEX) * preconvert->nMaxBltColorants,
            MM_ALLOC_CLASS_PRECONVERT);
    preconvert->maxBltColorants = NULL;
  }
  preconvert->nAllocComps = 0;
}

void preconvert_free(DL_STATE *page, Preconvert **freePreconvert)
{
  if ( *freePreconvert ) {
    Preconvert *preconvert = *freePreconvert;

    if ( preconvert->colorInfo ) {
      gsc_freecolorinfo(preconvert->colorInfo);
      dl_free(page->dlpools, preconvert->colorInfo, gsc_colorInfoSize(),
              MM_ALLOC_CLASS_PRECONVERT);
    }

    if ( preconvert->inputRS )
      guc_discardRasterStyle(&preconvert->inputRS);

    preconvert_colorants_free(page, preconvert);

    dl_free(page->dlpools, preconvert, sizeof(Preconvert),
            MM_ALLOC_CLASS_PRECONVERT);

    *freePreconvert = NULL;
  }
}

/**
 * Check if colorants have been added to the raster style since last time and
 * update the colorant workspace and image method accordingly. Needs calling
 * within preconvert_probe() because probing happens in the front-end, as may
 * blank separation detection with currentseparationorder().  Otherwise
 * preconvert_update() only needs calling again when the owning group is closed,
 * which fixes the colorant set for the group.
 */
Bool preconvert_update(const DL_STATE *page, Preconvert *preconvert)
{
  uint32 nComps;

  gucr_colorantCount(preconvert->inputRS, &nComps);
  if ( nComps != preconvert->nAllocComps ) {
    preconvert_colorants_free(page, preconvert);
    if ( !preconvert_colorants_alloc(page, preconvert, nComps) )
      return FALSE;
    preconvert->imageMethod = GSC_NO_EXTERNAL_METHOD_CHOICE;
  }

  /* Set up a colorspace for setting the image conversion method. */
  if ( preconvert->imageMethod == GSC_NO_EXTERNAL_METHOD_CHOICE ) {
    size_t iComp;
    preconvert->nCurrentComps = nComps;
    for ( iComp = 0; iComp < nComps; ++iComp )
      preconvert->colorantIndices[iComp] = preconvert->unionColorants[iComp];
    if ( !preconvert_setcolorspace(preconvert, GSC_IMAGE,
                                   FALSE /* not used for the method */,
                                   nComps > preconvert->nProcessComps,
                                   FALSE /* not used for the method */) )
      return FALSE;

    /* Set color conversion method now, when the color chain is set up with the
       full set of colorants. The method applies to the backdrop and any
       preconverted image in this group. Otherwise using Tom's Tables for one
       and invokeBlock for another may lead to differences between direct and
       backdrop regions. */
    HQASSERT(sizeof(COLORVALUE) == 2,
             "COLORVALUE has changed and bits-per-pixel value needs reviewing");
    preconvert->imageMethod =
      im_determine_method(TypeImageImage, nComps, 16,
                          preconvert->colorInfo, GSC_IMAGE);
  }
  return TRUE;
}

/** Get the image conversion method.
 *
 * Assumes preconvert_update() has been called.
 */
int32 preconvert_method(Preconvert *preconvert)
{
  HQASSERT(preconvert->imageMethod != GSC_NO_EXTERNAL_METHOD_CHOICE,
           "image method not set");
  return preconvert->imageMethod;
}

static Bool preconvert_setcolorspace(Preconvert *preconvert,
                                     int32 colorType, Bool overprint,
                                     Bool hasSpots, Bool luminosity)
{
  GS_COLORinfo *colorInfo = preconvert->colorInfo;

  COLORTYPE_ASSERT(colorType, "preconvert");

  /* First set the associated profile which comes from the colorspace for the
     source group. */
  if ( !gsc_setAssociatedProfile(colorInfo, preconvert->processSpace,
                                 (gsc_isInvertible(colorInfo, &preconvert->colorSpace)
                                  ? &preconvert->colorSpace : &onull)) )
    return FALSE;

  if ( hasSpots || preconvert->nCurrentComps < preconvert->nProcessComps ) {
    /* Converting spots; or both process and spot colorants;
       or a subset of the process */
    if ( !gsc_spacecache_setcolorspace(colorInfo,
                                       preconvert->inputRS,
                                       colorType,
                                       preconvert->nCurrentComps,
                                       preconvert->colorantIndices,
                                       TRUE, /* fCompositing */
                                       guc_getCMYKEquivalents,
                                       NULL) )
      return FALSE;
  } else {
    /* Only have process colorants, set a simple device space */
    if ( !gsc_setcolorspacedirectforcompositing(colorInfo, colorType,
                                                preconvert->processSpace) )
      return FALSE;
  }

  /** \todo @@@ TODO FIXME The luminosity flag must be set AFTER the color space
      since the color space will reset the flag to default when it builds a new
      chain head... */
  if ( !gsc_setoverprint(colorInfo, colorType, overprint) ||
       !gsc_setLuminosityChain(colorInfo, colorType, luminosity) )
    return FALSE;

  return TRUE;
}

/**
 * Overprint simplify is an optimisation on overprinted objects to reduce the
 * amount of compositing.  Overprinted objects with spot colorants that are not
 * being output may need compositing, but if the spot colorants are white and
 * paint on white (as determined by mark_region_map_for_overprints()) then
 * compositing can be avoided by removing the white spot colorants before
 * preconversion.  If the spots aren't removed they will be converted to process
 * potentially knocking out the background incorrectly.  This optimisation is
 * primarily for the recombine of Shira jobs which cannot easily be
 * object-recombined.
 *
 * preconvert_overprint_simplify() determines if the object is overprinting and
 * whether the colorants can be selectivey ignored (only color is the dl color).
 */
Bool preconvert_overprint_simplify(LISTOBJECT *lobj)
{
  switch ( lobj->opcode ) {
  case RENDER_char: case RENDER_rect: case RENDER_quad:
  case RENDER_fill: case RENDER_mask:
    return ( (lobj->spflags & RENDER_KNOCKOUT) == 0 &&
             (lobj->spflags & RENDER_PATTERN) == 0 );
  }
  return FALSE;
}

/**
 * Read a dl color and unpack into the colorant buffers.  Determines
 * if spot colors are used.
 */
static void preconvert_unpack(Preconvert *preconvert, dl_color_t *dlc,
                              Bool *hasSpots, Bool overprintSimplify)
{
  dlc_tint_t tint = dlc_check_black_white(dlc);
  COLORVALUE cv = COLORVALUE_ONE;

  switch ( tint ) {
  default:
    HQFAIL("Unexpected dl color tint value");
  case DLC_TINT_BLACK:
    cv = 0;
  case DLC_TINT_WHITE:
  case DLC_TINT_OTHER: {
    /* Handles Black/White/All/Other-type dl colors. */
    uint32 iReduced = 0, iComp;
    for ( iComp = 0; iComp < preconvert->nAllocComps; ++iComp ) {
      COLORANTINDEX ci = preconvert->unionColorants[iComp];

      if ( ci != COLORANTINDEX_NONE ) {
        if ( tint != DLC_TINT_OTHER ||
             (dlc_set_indexed_colorant(dlc, ci) &&
              dlc_get_indexed_colorant(dlc, ci, &cv)) ) {
          GUCR_COLORANT *colorant;
          const GUCR_COLORANT_INFO *colorantInfo;

          gucr_colorantHandle(preconvert->inputRS, ci, &colorant);
          (void)gucr_colorantDescription(colorant, &colorantInfo);

          /* If overprintSimplify is enabled on this object, overprinted white
             spot colorants that are not being output must be ignored to get
             correct output. The other part to this optimisation is in
             mark_region_map_for_overprints(). */
          if ( !overprintSimplify || cv != COLORVALUE_ONE ||
               colorantInfo->colorantType == COLORANTTYPE_PROCESS ||
               guc_equivalentRealColorantIndex(preconvert->inputRS, ci, NULL) ) {
            USERVALUE cvf = COLORVALUE_TO_USERVALUE(cv);
            NARROW_01(cvf);

            preconvert->colorantIndices[iReduced] = ci;
            preconvert->colorantValues[iReduced] = cv;
            preconvert->colorantFloats[iReduced] = cvf;
            ++iReduced;
          }
        }
      }
    }
    preconvert->nCurrentComps = iReduced;
    break;
   }
  }

  if ( preconvert->nAllocComps == preconvert->nProcessComps ) {
    /* Definitely no spots in dlc */
    *hasSpots = FALSE;
  } else if ( preconvert->nCurrentComps > preconvert->nProcessComps ) {
    /* Definitely spots in dlc */
    *hasSpots = TRUE;
  } else {
    uint32 iComp;
    /* May or may not be spots in dlc, check type of each colorant */
    *hasSpots = FALSE;
    for ( iComp = 0; iComp < preconvert->nCurrentComps; ++iComp ) {
      GUCR_COLORANT* colorant;
      const GUCR_COLORANT_INFO *colorantInfo;

      gucr_colorantHandle(preconvert->inputRS,
                          preconvert->colorantIndices[iComp],
                          &colorant);
      if ( gucr_colorantDescription(colorant, &colorantInfo) &&
           colorantInfo->colorantType != COLORANTTYPE_PROCESS ) {
        *hasSpots = TRUE;
        break;
      }
    }
  }
}

/**
 * Transfer maxblt information from the original color to the new color.
 */
static Bool preconvert_maxblits(DL_STATE *page, Preconvert *preconvert,
                                dl_color_t *dlc)
{
  uint32 iComp, nMaxBlts = 0;

  if ( !dlc_doing_maxblt_overprints(dlc) || preconvert->nMaxBltColorants == 0 )
    return TRUE;

  for ( iComp = 0; iComp < preconvert->nCurrentComps; ++iComp ) {
    COLORANTINDEX ci = preconvert->colorantIndices[iComp];

    if ( ci != COLORANTINDEX_NONE &&
         dlc_set_indexed_colorant(dlc, ci) &&
         dlc_colorant_is_overprinted(dlc, ci) ) {
      /* Colorant is present and max-blt'd. */
      COLORANTINDEX* cimap;
      if ( guc_equivalentRealColorantIndex(preconvert->inputRS, ci, &cimap) ) {
        do {
          preconvert->maxBltColorants[nMaxBlts++] = *cimap;
          HQASSERT(nMaxBlts <= preconvert->nMaxBltColorants,
                   "maxBltColorants list too short");
          ++cimap;
        } while (*cimap != COLORANTINDEX_UNKNOWN);
      }
    }
  }

  if ( nMaxBlts > 1 ) {
    /* Sort maxBltColorants. */
    uint32 limit, i, nUnique;
    for ( limit = nMaxBlts; limit > 0; --limit ) {
      Bool swapped = FALSE;
      for ( i = 1; i < limit; ++i ) {
        if ( preconvert->maxBltColorants[i - 1]
             > preconvert->maxBltColorants[i] ) {
          COLORANTINDEX temp = preconvert->maxBltColorants[i - 1];
          preconvert->maxBltColorants[i - 1] = preconvert->maxBltColorants[i];
          preconvert->maxBltColorants[i] = temp;
          swapped = TRUE;
        }
      }
      if ( !swapped )
        break;
    }

    /* Remove duplicates. */
    nUnique = 1;
    for ( i = 1; i < nMaxBlts; ++i ) {
      if ( preconvert->maxBltColorants[i]
           != preconvert->maxBltColorants[nUnique - 1] ) {
        if ( nUnique != i )
          preconvert->maxBltColorants[nUnique] =
            preconvert->maxBltColorants[i];
        ++nUnique;
      }
    }
    nMaxBlts = nUnique;
  }

  /* Merge newly derived overprinting information with existing
     dlc_currentcolor. */
  if ( nMaxBlts > 0 &&
       !dlc_apply_overprints(page->dlc_context,
                             DLC_MAXBLT_OVERPRINTS, DLC_REPLACE_OP,
                             nMaxBlts, preconvert->maxBltColorants,
                             dlc_currentcolor(page->dlc_context)) )
    return FALSE;

  return TRUE;
}

/**
 * Color convert dlc from one blend space to the next.  May be called repeatedly
 * for each group in the group hierarchy until a device color is obtained.  The
 * result color is stored in dlc_currentcolor.
 */
Bool preconvert_dlcolor(Group *group, int32 colorType, SPOTNO spotno,
                        uint8 reproType, LateColorAttrib *lateColorAttrib,
                        Bool overprint, Bool overprintSimplify,
                        dl_color_t *dlc)
{
  Preconvert *preconvert = groupPreconvert(group);
  GS_COLORinfo *colorInfo = preconvert->colorInfo;
  Bool luminosity = groupGetUsage(group) == GroupLuminositySoftMask;
  Bool hasSpots = FALSE;
  Bool nextIndependentChannels;
  LateColorAttrib lca = *lateColorAttrib; /* local copy */
  LateColorAttrib *groupLCA;

  HQASSERT(!rcbn_intercepting(), "Expected recombine interception to be disabled");

  /* Unpack the dl color into the lists of colorant indices and values. */
  preconvert_unpack(preconvert, dlc, &hasSpots, overprintSimplify);

  /* Set up a none dl color if no colorants unpacked. */
  if ( preconvert->nCurrentComps == 0 ) {
    dlc_context_t *dlc_context = groupPage(group)->dlc_context;
    dlc_release(dlc_context, dlc_currentcolor(dlc_context));
    dlc_get_none(dlc_context, dlc_currentcolor(dlc_context));
    return TRUE;
  }

  if ( !gsc_setSpotno(colorInfo, spotno) )
    return FALSE;

  if ( !preconvert_setcolorspace(preconvert, colorType, overprint,
                                 hasSpots, luminosity) )
    return FALSE;

  /* Normally inherit the rendering intent and color model from the
     containing group as per PDFRM. An exception is made for the color
     management of objects and groups painted directly into a non-ICCBased
     page group, and for NamedColor, details in update_pagegroup_lca().
     NB. ICCBased page groups have an LCA, non-ICCBased page groups don't. */
  groupLCA = groupGetAttrs(group)->lobjLCA;
  if ( groupLCA != NULL ) {
    lca.renderingIntent = groupLCA->renderingIntent;
    if ( lca.origColorModel != REPRO_COLOR_MODEL_NAMED_COLOR )
      lca.origColorModel = groupLCA->origColorModel;
  }
  else
    HQASSERT( groupGetUsage(group) == GroupPage, "Only page groups may not have an lca" );

  if ( !groupSetColorAttribs(colorInfo, colorType, reproType, &lca))
    return FALSE;

  /* Color values in dlc are additive; if using a subtractive space must
     flip the values. */
  if ( ColorspaceIsSubtractive(gsc_getcolorspace(colorInfo, colorType)) ) {
    uint32 i;
    for ( i = 0; i < preconvert->nCurrentComps; ++i ) {
      NARROW_01(preconvert->colorantFloats[i]);
      preconvert->colorantFloats[i] = 1.0f - preconvert->colorantFloats[i];
    }
  }

  /* Set the color values and invoke the chain.  The resulting dl color is
     stored in dlc_currentcolor and it is up to the caller to grab the color
     from here and deal with however it sees fit. */
  if ( !gsc_setcolordirect(colorInfo, colorType, preconvert->colorantFloats) ||
       !gsc_invokeChainSingle(colorInfo, colorType) )
    return FALSE;

  /* The independentChannels is only passed back as true to the parent group if
     all child groups also have independent channels. */
  if ( !gsc_hasIndependentChannels(colorInfo, colorType, FALSE,
                                   &nextIndependentChannels) )
    return FALSE;
  lateColorAttrib->independentChannels = lateColorAttrib->independentChannels &&
                                         nextIndependentChannels;

  /* Transfer max-blits from dlc to dlc_currentcolor. */
  if ( !preconvert_maxblits(groupPage(group), preconvert, dlc) )
    return FALSE;

  return TRUE;
}

/**
 * When compositing, the rendering intent applied to color conversions is normally
 * inherited from the containing group as per PDFRM. We also use the same rule
 * when applying intercept profiles to device spaces. An exception is made for
 * the color management of non-ICCBased page groups to the output, for which we
 * use the intent the objects and groups had when they were painted into the page
 * group.
 *
 * This function transfers the rendering intent for all backdrops painted directly
 * into the page group, irrespective of whether compositing or color conversion
 * was applied to them, so the correct intent can be used in the output stage.
 *
 * We have chosen to apply the same rule to the color model, except for NamedColor
 * because spots are composited in their own channel.
 */
static void update_pagegroup_lca(Group *group, LateColorAttrib *currentLCA)
{
  Group *parent = groupParent(group);

  if ( parent != NULL && groupGetUsage(parent) == GroupPage ) {
    LateColorAttrib *parentLCA = groupGetAttrs(parent)->lobjLCA;
    /* ICCBased page groups have an LCA, non-ICCBased page groups don't */
    if ( parentLCA == NULL ) {
      LateColorAttrib *groupLCA = groupGetAttrs(group)->lobjLCA;
      if ( groupLCA != NULL ) {
        currentLCA->renderingIntent = groupLCA->renderingIntent;
        if ( currentLCA->origColorModel != REPRO_COLOR_MODEL_NAMED_COLOR )
          currentLCA->origColorModel = groupLCA->origColorModel;
      }
    }
  }
}

/**
 * Helper function for gsc_ICCCacheTransfer(), which needs to cache all tables
 * within ICC profiles prior to their use in the back end. The most convenient
 * means of doing this is to build color chains for all groups since this will
 * provoke the caching of profiles used within those chains.
 * The color chain must be built using using all colorants in the group.
 * Plain device space groups are straightforward. For groups with spots, the
 * chains will use an interceptdevicen chain which will provoke the caching of
 * ICC profiles used with named color interception as well as those used with
 * device space color management.
 * Black tint preservation can require additional ICC tables. These are unpacked
 * lazily during color chain invokes, rather than construction. So, it is
 * necessary to invoke each chain as well.
 */
Bool preconvert_invoke_all_colorants(Group *group, int32 colorType,
                                     uint8 reproType, LateColorAttrib *lateColorAttrib)
{
  Preconvert *preconvert = groupPreconvert(group);
  GS_COLORinfo *colorInfo;
  Bool luminosity = groupGetUsage(group) == GroupLuminositySoftMask;
  uint32 iComp;

  /* No color conversion required if the raster styles are the same */
  if (groupInputRasterStyle(group) == groupOutputRasterStyle(group))
    return TRUE;

  HQASSERT(preconvert != NULL, "preconvert NULL");
  colorInfo = preconvert->colorInfo;

  for ( iComp = 0; iComp < preconvert->nAllocComps; ++iComp )
    preconvert->colorantIndices[iComp] = preconvert->unionColorants[iComp];
  preconvert->nCurrentComps = preconvert->nAllocComps;

  if ( !preconvert_setcolorspace(preconvert, colorType,
                                 FALSE /* not used for the method */,
                                 preconvert->nAllocComps > preconvert->nProcessComps,
                                 luminosity) )
    return FALSE;

  if (!groupSetColorAttribs(colorInfo, colorType, reproType, lateColorAttrib))
    return FALSE;

  /* The chain construction is enough to provoke the caching of most ICC profiles */
  if ( !gsc_constructChain(colorInfo, colorType))
    return FALSE;

  /* The invoke is important for setups with black tint preservation */
  if (!gsc_invokeChainSingle(colorInfo, colorType))
    return FALSE;

  /* NB. There is no need to modify the lateColorAttrib because we aren't
     recursing through the group stack in this function. */

  return TRUE;
}

/**
 * preconvert_probe is used for linearity testing in shfills.  It puts the
 * resulting device color in dlc_currentcolor.
 */
Bool preconvert_probe(Group *group, int32 colorType, SPOTNO spotno,
                      uint8 reproType, dl_color_t *dlc,
                      LateColorAttrib *lateColorAttrib)
{
  /* When doing the probe, we'll use the same color info as we use for
     evaluating the output color. */
  DL_STATE *page = groupPage(group);
  dl_color_t *dlc_current = dlc_currentcolor(page->dlc_context);
  LateColorAttrib currentLCA = *lateColorAttrib;      /* local copy */
  /* preconvert_probe shouldn't affect currentspflags, we're only interested
     in the color. */
  uint8 spflags = dl_currentspflags(page->dlc_context);

  HQASSERT(colorType == GSC_SHFILL || colorType == GSC_SHFILL_INDEXED_BASE,
           "Unexpected colorType");
  HQASSERT(reproType == REPRO_TYPE_VIGNETTE, "Unexpected reproType");

  /* Copy source dlc over to current color in case preconvert is a no-op. */
  if ( dlc != dlc_current ) {
    dlc_release(page->dlc_context, dlc_current);
    if ( !dlc_copy(page->dlc_context, dlc_current, dlc) )
      return FALSE;
    dlc = dlc_current;
  }

  for ( ; group && !groupIsSoftMask(group) && !dlc_is_none(dlc);
        group = groupParent(group) ) {
    Preconvert *preconvert = groupPreconvert(group);

    /* A null preconvert means color conversion for this group is a no-op. */
    if ( preconvert != NULL ) {
      if ( !preconvert_update(page, preconvert) ||
           !preconvert_dlcolor(group, colorType, spotno, reproType, &currentLCA,
                               FALSE /* overprint doesn't matter - just probing */,
                               FALSE /* similarly for overprintSimplify */,
                               dlc) ) {
        dl_set_currentspflags(page->dlc_context, spflags);
        return FALSE;
      }
      dlc = dlc_current;
    }

    update_pagegroup_lca(group, &currentLCA);
  }

  dl_set_currentspflags(page->dlc_context, spflags);
  return TRUE;
}

/** Preconvert an lobj's dl color to device colors, without changing the
 *  lobj itself. Iterates through multiple blend spaces if required.
 *  This function does not preconvert image data.
 *
 *  Note : This operation has been added to support trapping, which even
 *  in two pass rendering, does a form of on demand preconversion on the dl
 *  color of lobjs.
 *  It might be better folded into preconvert_on_the_fly.
 *
 *  \param group The group enclosing the LISTOBJECT whose color is to be
 *               preconverted.
 *  \param lobj  The list object whose color is to be preconverted.
 *  \param dlc   A pointer to a dlc_color_t into which the preconverted color
 *               is placed. The caller should release this color if function
 *               preconvert_dlcolor_to_devicespace returns TRUE. dlc should be
 *               cleared before calling this function.
 *
 */
Bool preconvert_lobj_color_to_devicespace(Group *group, LISTOBJECT *lobj,
                                          dl_color_t *dlc)
{
  LateColorAttrib currentLCA = *lobj->objectstate->lateColorAttrib;   /* local copy */
  dlc_context_t *dlc_context = groupPage(group)->dlc_context;
  Bool overprint = (lobj->spflags & RENDER_KNOCKOUT) == 0;
  Bool overprintSimplify = preconvert_overprint_simplify(lobj);

  if ( !dlc_from_lobj(dlc_context, lobj, dlc) )
    return FALSE;

  for ( ; group != NULL && !dlc_is_none(dlc); group = groupParent(group) ) {
    if ( groupPreconvert(group) != NULL ) {
      if ( !preconvert_dlcolor(group, DISPOSITION_COLORTYPE(lobj->disposition),
                               lobj->objectstate->spotno,
                               DISPOSITION_REPRO_TYPE(lobj->disposition),
                               &currentLCA, overprint, overprintSimplify, dlc) ) {
          dlc_release(dlc_context, dlc);
          return FALSE;
        }

      dlc_release(dlc_context, dlc);
      dlc_copy_release(dlc_context, dlc, dlc_currentcolor(dlc_context));
    }

    update_pagegroup_lca(group, &currentLCA);
  }

  return TRUE;
}

/**
 * Preconvert the lobj dl color without replacing lobj->p_ncolor.  For images
 * the on-the-fly image setup is created lazily once the color space has been
 * set by preconvert_dlcolor.
 *
 * Note, on-the-fly conversion disables direct render of nested groups so there
 * is never more than one color conversion required.
 *
 * dlcResult may be identical to dlc, in which case the result color will
 * overwrite the existing dlc color, therefore the dlc passed in should be from
 * a weak copy.
 */
static Bool preconvert_on_the_fly_unsafe(Group *group, LISTOBJECT *lobj,
                                         int32 colorType, dl_color_t *dlc,
                                         dl_color_t *dlcResult)
{
  DL_STATE *page = groupPage(group);
  Preconvert *preconvert;
  Bool overprint = (lobj->spflags & RENDER_KNOCKOUT) == 0;
  Bool overprintSimplify = preconvert_overprint_simplify(lobj);
  LateColorAttrib currentLCA = *lobj->objectstate->lateColorAttrib;    /* local copy */

  /** \todo Single-pass compositing restricts direct rendering to objects in the
      page group blend space. It should be changed to allow preconversion
      between multiple blend spaces to match two-pass compositing. Image OTF
      needs more work to do this. So eventually OTF will loop over groups
      repeatedly color-converting, like preconvert_lobj_to_devicespace. */
  while ( groupGetUsage(group) != GroupPage ) {
    HQASSERT(groupPreconvert(group) == NULL,
             "On-the-fly preconversion currently works on one blend space only");
    group = groupParent(group);
  }

  preconvert = groupPreconvert(group);
  if ( preconvert == NULL || dl_is_none(lobj->p_ncolor) )
    return TRUE;

  if ( !preconvert_dlcolor(group, colorType, lobj->objectstate->spotno,
                           DISPOSITION_REPRO_TYPE(lobj->disposition),
                           &currentLCA, overprint, overprintSimplify, dlc) )
    return FALSE;

  /* Move the new color into dlcResult if supplied. */
  if ( dlcResult != NULL && dlcResult != dlc_currentcolor(page->dlc_context) )
    dlc_copy_release(page->dlc_context, dlcResult,
                     dlc_currentcolor(page->dlc_context));

  /* Create the on-the-fly expander setup if not already done so.
     The colorInfo passed in is now set with the correct color space. */
  if ( lobj->opcode == RENDER_image &&
       !im_converting_on_the_fly(lobj->dldata.image->ime) &&
       !im_expand_setup_on_the_fly(lobj->dldata.image, page,
                                   preconvert->imageMethod,
                                   preconvert->colorInfo, colorType) )
    return FALSE;

  return TRUE;
}

Bool preconvert_on_the_fly(Group *group, LISTOBJECT *lobj, int32 colorType,
                           dl_color_t *dlc, dl_color_t *dlcResult)
{
  Bool result;

  cvcolcvt_lock();
  result = preconvert_on_the_fly_unsafe(group, lobj, colorType, dlc, dlcResult);
  cvcolcvt_unlock();
  return result;
}

/**
 * Replace the dl color in lobj with the preconverted color.
 */
static Bool preconvert_lobj_color(Group *group, LISTOBJECT *lobj,
                                  int32 colorType, Bool overprintSimplify,
                                  LateColorAttrib *currentLCA)
{
  Bool overprint = (lobj->spflags & RENDER_KNOCKOUT) == 0;
  dlc_context_t *dlc_context = groupPage(group)->dlc_context;
  dl_color_t dlc;

  HQASSERT((lobj->marker & MARKER_DEVICECOLOR) == 0,
           "Already have device color");
  HQASSERT((lobj->spflags & RENDER_RECOMBINE) == 0,
           "Do not expect recombine colorants");
  HQASSERT(currentLCA != lobj->objectstate->lateColorAttrib,
           "Must not directly modify the object state");

  dlc_from_dl_weak(lobj->p_ncolor, &dlc);

  if ( !preconvert_dlcolor(group, colorType, lobj->objectstate->spotno,
                           DISPOSITION_REPRO_TYPE(lobj->disposition),
                           currentLCA, overprint, overprintSimplify, &dlc) )
    return FALSE;

  dl_release(dlc_context, &lobj->p_ncolor);
  dlc_to_lobj_release(lobj, dlc_currentcolor(dlc_context));

  if ( groupGetUsage(group) == GroupPage ) {
    HQASSERT(!guc_backdropRasterStyle(
               gsc_getTargetRS(groupPreconvert(group)->colorInfo)),
             "Expected device rasterstyle to be the target");
    lobj->marker |= MARKER_DEVICECOLOR;
  }
  return TRUE;
}

typedef struct {
  Group *group;
  int32 colorType;
  SPOTNO spotno;
  uint8 reproType;
  LateColorAttrib *lateColorAttrib;
  Bool overprint;
  dl_color_t *merged_color;
} PreconvertGouraud;

static Bool preconvert_gouraudcolors(p_ncolor_t *ncolor, void *data)
{
  PreconvertGouraud *pcg_data = data;
  dlc_context_t *dlc_context = groupPage(pcg_data->group)->dlc_context;
  dl_color_t *dlc_current, dlc;

  dlc_current = dlc_currentcolor(dlc_context);
  dlc_from_dl_weak(*ncolor, &dlc);

  if ( !preconvert_dlcolor(pcg_data->group, pcg_data->colorType,
                           pcg_data->spotno, pcg_data->reproType,
                           pcg_data->lateColorAttrib, pcg_data->overprint,
                           FALSE, &dlc) )
    return FALSE;

  dl_release(dlc_context, ncolor);
  if ( !dlc_to_dl(dlc_context, ncolor, dlc_current) )
    return FALSE;

  if ( dlc_is_clear(pcg_data->merged_color) ) {
    dlc_copy_release(dlc_context, pcg_data->merged_color, dlc_current);
    return TRUE;
  }

  if ( !dlc_merge_shfill(dlc_context, pcg_data->merged_color, dlc_current) )
    return FALSE;

  dlc_release(dlc_context, dlc_current);

  return TRUE;
}

#if !defined POOR_SHADING
/* Remove this declaration when PoorShading is removed */
static Bool preconvert_lobj(Group *group, LISTOBJECT *lobj,
                            LateColorAttrib *currentLCA);
#endif

/**
 * Preconvert all the elements of a shfill using the colorType and reproType
 * from the main shfill DL object.  Produce a merge color containing the union
 * set of colorants and make this the shfill dl color.
 */
static Bool preconvert_shfill_elements(Group *group, LISTOBJECT *lobj,
                                       int32 colorType, uint8 reproType,
                                       LateColorAttrib *lateColorAttrib)
{
  dlc_context_t *dlc_context = groupPage(group)->dlc_context;
  DLRANGE dlrange;
  dl_color_t merged_color;

  hdlDlrange(lobj->dldata.shade->hdl, &dlrange);
  dlc_clear(&merged_color);

  /** \todo ajcd 2004-02-22: This should use another forall call
     when shfills are changed to use HDLs (but currently doesn't). */
  for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
        dlrange_next(&dlrange) ) {
    LISTOBJECT *subdlobj = dlrange_lobj(&dlrange);
    if ( subdlobj->opcode == RENDER_gouraud ) {
      /* Recursively convert all colors in a gouraud object. */
      dl_color_t gouraud_merged_color;
      PreconvertGouraud pcg_data;

      /* Attributes from the main shfill object apply to all elements. */
      pcg_data.group = group;
      pcg_data.colorType = colorType;
      pcg_data.spotno = lobj->objectstate->spotno;
      pcg_data.reproType = reproType;
      pcg_data.lateColorAttrib = lateColorAttrib;
      pcg_data.overprint = (lobj->spflags & RENDER_KNOCKOUT) == 0;
      dlc_clear(&gouraud_merged_color);
      pcg_data.merged_color = &gouraud_merged_color;

      if ( !gouraud_iterate_dlcolors(subdlobj, preconvert_gouraudcolors,
                                     &pcg_data) )
        return FALSE;

      HQASSERT(!dlc_is_clear(&gouraud_merged_color),
               "Merged colour is clear after gouraud iteration");

      /* Copy merged color to gouraud lobj, in place of unconverted color, and
         merge into overall shfill color. */
      dl_release(dlc_context, &subdlobj->p_ncolor);
      if ( !dlc_to_lobj(dlc_context, subdlobj, &gouraud_merged_color) )
        return FALSE;
      dlrange.writeBack = TRUE;
      if ( dlc_is_clear(&merged_color) ) {
        if ( !dlc_copy(dlc_context, &merged_color, &gouraud_merged_color) )
          return FALSE;
      } else {
        if ( !dlc_merge_shfill(dlc_context, &merged_color,
                               &gouraud_merged_color) )
          return FALSE;
      }
      /* If group is the page group then the dl color must be a device color. */
      if ( groupGetUsage(group) == GroupPage )
        subdlobj->marker |= MARKER_DEVICECOLOR;

    } else {
      /* Not a gouraud object. */
#if !defined POOR_SHADING
      /* Remove this clause when PoorShading is removed */
      if (subdlobj->opcode == RENDER_image) {
        if (! preconvert_lobj(group, subdlobj, lateColorAttrib))
          return FALSE;
      }
      else
#endif
      {
        if ( !preconvert_lobj_color(group, subdlobj, colorType, FALSE,
                                    lateColorAttrib) )
          return FALSE;
      }

      /* Merge converted object color into overall shfill color. */
      if ( dlc_is_clear(&merged_color) ) {
        if ( !dlc_from_lobj(dlc_context, subdlobj, &merged_color) )
          return FALSE;
      } else {
        dl_color_t dlc_converted;
        dlc_from_lobj_weak(subdlobj, &dlc_converted);
        if ( !dlc_merge_shfill(dlc_context, &merged_color, &dlc_converted) )
          return FALSE;
      }
    }

    SwOftenUnsafe();
  }

  /* Replace the shfill object's dl color with the merged color. */
  dl_release(dlc_context, &lobj->p_ncolor);
  dlc_to_lobj_release(lobj, &merged_color);
  return TRUE;
}

static Bool preconvert_pcl_pattern_color(Group *group, pcl_color_union *ucolor,
                                         Bool firstTime, int32 colorType,
                                         SPOTNO spotno, uint8 reproType,
                                         LateColorAttrib *lca)
{
  DL_STATE *page = groupPage(group) ;
  dlc_context_t *dlc_context = page->dlc_context ;
  dl_color_t *dlc_current = dlc_currentcolor(dlc_context) ;
  Bool white = FALSE ;

  /* Use the special white for the preconverted white to make it easier
     to detect in the pattern blitters. */
  if ( firstTime ) {
    if ( ucolor->packed == (page->virtualDeviceSpace == SPACE_DeviceCMYK
                            ? PCL_PACKED_CMYK_WHITE
                            : PCL_PACKED_RGB_WHITE) ) {
      dlc_release(dlc_context, dlc_current) ;
      dlc_get_white(dlc_context, dlc_current) ;
      white = TRUE ;
    } else {
      /* Take a copy of the merged HDL color to get the right number of
         colorants, then unpack the PCL packed color using it as a template. */
      if ( !dlc_copy(dlc_context, dlc_current, hdlColor(groupHdl(group))) ||
           !dlc_from_packed_color(dlc_context, dlc_current, ucolor->packed) )
        return FALSE ;
    }
  } else {
    if ( !dlc_from_dl(dlc_context, &ucolor->ncolor, dlc_current) )
      return FALSE ;
    white = dlc_is_white(dlc_current) ;
  }

  if ( !white &&
       !preconvert_dlcolor(group, colorType, spotno, reproType,
                           lca, FALSE /*overprint*/,
                           FALSE /*overprintSimplify*/, dlc_current) )
    return FALSE ;

  return dlc_to_dl(dlc_context, &ucolor->ncolor, dlc_current) ;
}

static Bool preconvert_pcl_pattern_line(Group *group, Bool firstTime,
                                        pcl_color_union *colors, uint32 ncolors,
                                        int32 colorType, SPOTNO spotno,
                                        uint8 reproType, LateColorAttrib *lca)
{
  uint32 i, j ;

  for ( j = 0 ; j < ncolors ; ) {
    pcl_color_union color = colors[j] ;

    /* Search for runs of the same color, overshooting by one at the end. */
    for ( i = j ; ++i < ncolors ; ) {
      if ( colors[i].i != color.i )
        break ;
    }

    /* Pre-convert the color we found. */
    if ( !preconvert_pcl_pattern_color(group, &color, firstTime, colorType,
                                       spotno, reproType, lca) )
      return FALSE ;

    /* Store it into all similar pixels. */
    while ( j < i ) {
      colors[j++] = color ;
    }
  }

  return TRUE ;
}

static Bool preconvert_pcl_pattern(Group *group, PclDLPattern *pattern,
                                   int32 colorType, SPOTNO spotno,
                                   uint8 reproType, LateColorAttrib *lca)
{
  uint32 y ;
  Bool firstTime = (pattern->preconverted == PCL_PATTERN_PRECONVERT_NONE) ;

  /** \todo ajcd 2013-08-23: The pattern may be used by multiple objects, it
      shouldn't inherit the color type, repro type, or spot number from any
      arbitrary object. */

  if ( pattern->paletteSize > 0 ) {
    if ( !preconvert_pcl_pattern_line(group, firstTime,
                                      pattern->palette, pattern->paletteSize,
                                      colorType, spotno, reproType, lca) )
      return FALSE ;
  } else {
    for ( y = 0 ; y < pattern->height ; ++y ) {
      PclDLPatternLine *line = &pattern->lines[y] ;
      /* Only convert lines that are not marked as duplicates of another. */
      if ( line->repeats == 0 ) {
        pcl_color_union *entries = pattern->data.pixels + line->offset ;
        if ( line->type == PCL_PATTERNLINE_FULL ) {
          if ( !preconvert_pcl_pattern_line(group, firstTime,
                                            entries, pattern->width,
                                            colorType, spotno, reproType, lca) )
            return FALSE ;
        } else {
          uint32 runs ;
          for ( runs = line->type - PCL_PATTERNLINE_RLE0 ;
                runs > 0 ; --runs, entries += 2 ) {
            /* Convert the color portion of the (run,color) pair */
            if ( !preconvert_pcl_pattern_color(group, &entries[1], firstTime,
                                               colorType, spotno, reproType,
                                               lca) )
              return FALSE ;
          }
        }
      }
    }
  }

  pattern->preconverted = PCL_PATTERN_PRECONVERT_STARTED ;
  return TRUE ;
}

static Bool preconvert_pcl_pattern_to_devicespace(Group *group,
                                                  PclDLPattern *pattern,
                                                  int32 colorType,
                                                  SPOTNO spotno,
                                                  uint8 reproType,
                                                  LateColorAttrib *lca)
{
  LateColorAttrib currentLCA = *lca;   /* local copy */
  currentLCA.blackType = BLACK_TYPE_MODIFIED ;

  for ( ; group != NULL; group = groupParent(group) ) {
    /* A null preconvert means color conversion for this group is a no-op. */
    if ( groupPreconvert(group) != NULL ) {
      if ( !preconvert_pcl_pattern(group, pattern, colorType, spotno,
                                   reproType, &currentLCA) )
        return FALSE;
    }

    update_pagegroup_lca(group, &currentLCA);
  }

  pattern->preconverted = PCL_PATTERN_PRECONVERT_DEVICE ;
  return TRUE;
}

static Bool preconvert_lobj(Group *group, LISTOBJECT *lobj,
                            LateColorAttrib *currentLCA)
{
  Preconvert *preconvert = groupPreconvert(group);
  int32 colorType = DISPOSITION_COLORTYPE(lobj->disposition);
  Bool overprintSimplify = preconvert_overprint_simplify(lobj);

  HQASSERT((lobj->spflags & RENDER_RECOMBINE) == 0,
           "Do not expect recombine colorants");

  switch ( lobj->opcode ) {
  case RENDER_erase:
    /* Erase is made with a device color from the start and never needs
       preconverting. */
    return error_handler(UNREGISTERED);
  case RENDER_char:
  case RENDER_mask:
    HQASSERT(colorType == GSC_FILL || colorType == GSC_STROKE,
             "Unexpected colorType for erase/char/mask");
    /* fall through */
  case RENDER_rect:
  case RENDER_quad:
  case RENDER_fill:
  case RENDER_group:
  case RENDER_hdl:
    /* HDLs might be seen at this point where Groups have been optimised away
       when Group is known to not use transparency.  The HDL elements are
       preconvert by the outer dl_forall. */
    if ( !preconvert_lobj_color(group, lobj, colorType, overprintSimplify,
                                currentLCA) )
      return FALSE;
    break;
  case RENDER_image:
    /* We could have GSC_SHFILL with PoorShading. */
    HQASSERT(colorType == GSC_IMAGE || colorType == GSC_SHFILL,
             "Unexpected colorType for image");
    HQASSERT(preconvert->imageMethod != GSC_NO_EXTERNAL_METHOD_CHOICE,
             "Method for image color-conversion is not set");
    if ( !preconvert_lobj_color(group, lobj, colorType, overprintSimplify,
                                currentLCA) ||
         !im_adj_adjustimage(groupPage(group), TRUE /* safe to update image */,
                             lobj, preconvert->colorantFloats,
                             preconvert->colorantIndices,
                             preconvert->colorInfo, colorType,
                             preconvert->imageMethod) )
      return FALSE;
    break;
  case RENDER_vignette: {
    DLRANGE dlrange;

    HQASSERT(colorType == GSC_VIGNETTE, "Unexpected colorType for vignette");
    hdlDlrange(lobj->dldata.vignette->vhdl, &dlrange);
#if defined( ASSERT_BUILD )
    for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
          dlrange_next(&dlrange) ) {
      HQASSERT(dl_equal_colorants(lobj->p_ncolor,
                                  dlrange_lobj(&dlrange)->p_ncolor),
               "Trying to preconvert a vignette with inconsistent "
               "paint masks through its elements");
    }
#endif
    if ( !preconvert_lobj_color(group, lobj, colorType, overprintSimplify,
                                currentLCA) )
      return FALSE;

    for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
          dlrange_next(&dlrange) ) {
      if ( !preconvert_lobj_color(group, dlrange_lobj(&dlrange), colorType,
                                  overprintSimplify, currentLCA) )
        return FALSE;
      SwOftenUnsafe();
    }
    break;
  }
  case RENDER_shfill: {
    LateColorAttrib tmpLCA = *currentLCA;

#if defined( ASSERT_BUILD )
    HDL *hdl = lobj->dldata.shade->hdl;
    DLRANGE dlrange;
    hdlDlrange(hdl, &dlrange);
    for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
          dlrange_next(&dlrange) ) {
      HQASSERT(dl_equal_colorants(lobj->p_ncolor, dlrange_lobj(&dlrange)->p_ncolor),
               "Trying to preconvert a shfill with inconsistent "
               "paint masks through its elements");
    }
#endif

    HQASSERT(colorType == GSC_SHFILL, "Unexpected colorType for shfill");
    /* Note the use of tmpLCA, currentLCA will be modified by preconvert_lobj_color
       whilst we need the unmodified value in preconvert_shfill_elements. */
    if ( !preconvert_lobj_color(group, lobj, colorType, overprintSimplify,
                                currentLCA) ||
         !preconvert_shfill_elements(group, lobj, colorType,
                                     DISPOSITION_REPRO_TYPE(lobj->disposition),
                                     &tmpLCA) )
      return FALSE;
    break;
  }
  case RENDER_gouraud:
    HQFAIL("Should have dealt with Gourauds (via parent Shfill)");
    break;
  case RENDER_shfill_patch:
    HQFAIL("Shfill patches should already be converted to Gourauds");
    break;
  case RENDER_cell:
    HQFAIL("Do not expect to have to preconvert Cell objects");
    break;
  default:
    HQFAIL("Unrecognised opcode, may need adding to the preconvert code");
    break;
  }

  if ( lobj->objectstate->pclAttrib != NULL &&
       lobj->objectstate->pclAttrib->dlPattern != NULL &&
       lobj->objectstate->pclAttrib->dlPattern->preconverted == PCL_PATTERN_PRECONVERT_NONE ) {
    if ( !preconvert_pcl_pattern_to_devicespace(group,
                                                lobj->objectstate->pclAttrib->dlPattern,
                                                colorType,
                                                lobj->objectstate->spotno,
                                                DISPOSITION_REPRO_TYPE(lobj->disposition),
                                                currentLCA) )
      return FALSE;
  }

  return TRUE;
}

/**
 * Repeatedly preconvert the DL object with respect to each group in the stack
 * all the way down to the page group when the result will be a device color.
 */
static Bool preconvert_lobj_to_devicespace(Group *group, LISTOBJECT *lobj)
{
  LateColorAttrib currentLCA = *lobj->objectstate->lateColorAttrib;   /* local copy */

  for ( ; group != NULL; group = groupParent(group) ) {
    /* A null preconvert means color conversion for this group is a no-op. */
    if ( groupPreconvert(group) != NULL ) {
      if ( !preconvert_lobj(group, lobj, &currentLCA) )
        return FALSE;
    }

    update_pagegroup_lca(group, &currentLCA);
  }
  return TRUE;
}

/**
 * Can the DL object be preconverted?
 *
 * 1) Don't preconvert any color which is already a device color.
 *
 * 2) Don't bother preconverting an object which has MARKER_OMNITRANSPARENT set,
 * as it will definitely have been composited instead.
 *
 * 3) Don't bother preconverting a pattern sub-dl if it contains any transparent
 * objects (it will never be direct-rendered).
 *
 * 4) None dl color objects are ignored and not rendered.
 *
 * 5) Check the region map and decide whether to allow preconverting based on
 * the transparency strategy.
 *
 * NOTE: The sole purpose of this function is to decide if the object in hand
 * can be preconverted.  Since this decision depends on the transparency
 * strategy, and also when the preconverting happens, the function should not be
 * used for any other purpose.
 */
static Bool preconvert_required(Group *group, HDL *hdl, LISTOBJECT *lobj,
                                Bool insidepattern, int32 direct_only)
{
  BitGrid *regionMap = groupPage(group)->regionMap;

  if ( (lobj->marker & MARKER_DEVICECOLOR) != 0 ||
       (lobj->marker & MARKER_OMNITRANSPARENT) != 0 ||
       (insidepattern && hdlTransparent(hdl)) ||
       dl_is_none(lobj->p_ncolor) )
    return FALSE;

  if ( direct_only ) {
    /* Preconvert if certain the object won't also need compositing, otherwise
       the object may be converted on-the-fly if doing single-pass. */
    return ( regionMap == NULL || /* whole page is direct rendered */
             (!insidepattern && /* bbox not appropriate if inside pattern */
              bitGridGetBoxMapped(regionMap, &lobj->bbox) == BGAllClear) );
  } else {
    /* Any object that intersects direct regions can be preconverted. */
    return ( regionMap == NULL || /* whole page is direct rendered */
             insidepattern || /* bbox not appropriate if inside pattern */
             bitGridGetBoxMapped(regionMap, &lobj->bbox) != BGAllSet );
  }
}

/** Set up images to render to both region types for single-pass. */
static Bool preconvert_singlepass_setup(Group *group, LISTOBJECT *lobj)
{
  Preconvert *preconvert;
  int32 colorType = DISPOSITION_COLORTYPE(lobj->disposition);
  Bool overprint = (lobj->spflags & RENDER_KNOCKOUT) == 0;
  Bool overprintSimplify = preconvert_overprint_simplify(lobj);
  LateColorAttrib currentLCA = *lobj->objectstate->lateColorAttrib;   /* local copy */
  dl_color_t dlc;

  HQASSERT(lobj->opcode == RENDER_image, "Only images require special set up");

  /** \todo Single-pass compositing restricts direct rendering to objects in the
      page group blend space. It should be changed to allow preconversion
      between multiple blend spaces to match two-pass compositing. Image OTF
      needs more work to do this. So eventually OTF will loop over groups
      repeatedly color-converting, like preconvert_lobj_to_devicespace. */
  while ( groupGetUsage(group) != GroupPage ) {
    HQASSERT(groupPreconvert(group) == NULL,
             "On-the-fly preconversion currently works on one blend space only");
    group = groupParent(group);
  }

  preconvert = groupPreconvert(group);
  if ( preconvert == NULL )
    return TRUE;

  dlc_from_dl_weak(lobj->p_ncolor, &dlc);

  /* Set up colorInfo with the correct color space and parameters (ignoring
     the resulting dlc_currentcolor). */
  if ( !preconvert_dlcolor(group, colorType, lobj->objectstate->spotno,
                           DISPOSITION_REPRO_TYPE(lobj->disposition),
                           &currentLCA, overprint, overprintSimplify, &dlc) )
    return FALSE;

  /* Set up an alternate image LUT or on-the-fly conversion for the direct regions. */
  if ( !im_adj_adjustimage(groupPage(group),
                           FALSE, /* preserve image for compositing */
                           lobj, preconvert->colorantFloats,
                           preconvert->colorantIndices,
                           preconvert->colorInfo, colorType,
                           preconvert->imageMethod) )
    return FALSE;

  return TRUE;
}

static Bool preconvert_callback(DL_FORALL_INFO *info)
{
  Group *group = hdlEnclosingGroup(info->hdl);
  Bool insidepattern = (info->reason & DL_FORALL_PATTERN) != 0;
  int32 transparency_strategy = *((int32*)info->data);
  LISTOBJECT *lobj = info->lobj;

  /* If the object is not in a group it doesn't need preconverting. */
  if ( group == NULL )
    return TRUE;

  HQASSERT((info->reason & (DL_FORALL_NONE|DL_FORALL_SOFTMASK|
           DL_FORALL_SHFILL)) == 0 && !dl_is_none(lobj->p_ncolor),
           "DL walk passed excluded object");
  HQASSERT(transparency_strategy >= 1 && transparency_strategy <= 2,
           "Unexpected transparency_strategy value");

  /* Preconvert the object if it only needs direct rendering now. */
  if ( preconvert_required(group, info->hdl, lobj, insidepattern,
                           transparency_strategy == 1) ) {
    if ( !preconvert_lobj_to_devicespace(group, lobj) )
      return FALSE;

    info->dlrange.writeBack = TRUE;
  }
  /* Not preconverting, but may need to set up the lobj for single-pass
     rendering to both direct and backdrop regions. */
  else if ( transparency_strategy == 1 && lobj->opcode == RENDER_image &&
            preconvert_required(group, info->hdl, lobj, insidepattern, FALSE) ) {
    if ( !preconvert_singlepass_setup(group, lobj) )
      return FALSE;

    info->dlrange.writeBack = TRUE;
  }

  updateDLProgressTotal(1.0, PRECONVERT_PROGRESS);
  SwOftenUnsafe();
  return TRUE;
}

/**
 * A quick scan over the groups to decide whether there are objects that may
 * need preconverting.  Want to avoid showing the progress dial unnecessarily.
 * This function may return a false-positive.
 */
static Bool preconvert_possible(DL_STATE *page)
{
  HDL_LIST *hlist;

  for ( hlist = page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    Group *group = hdlGroup(hlist->hdl);

    if ( group != NULL && groupPreconvert(group) != NULL &&
         !groupMustComposite(group) )
      return TRUE; /* May need to do some preconverting. */
  }
  return FALSE; /* Preconverting definitely not required. */
}

Bool preconvert_dl(DL_STATE *page, int32 transparency_strategy)
{
  Bool result;
  DL_FORALL_INFO info = {0};
  int32 trans_strategy = transparency_strategy;

  /* Don't want progress dial if preconvert is a no-op. */
  if ( !preconvert_possible(page) )
    return TRUE;

  openDLProgress(page, PRECONVERT_PROGRESS);

  info.page = page;
  info.hdl = dlPageHDL(page);
  info.inflags = DL_FORALL_USEMARKER|DL_FORALL_GROUP|DL_FORALL_PATTERN;
  info.data = &trans_strategy;

  result = dl_forall(&info, preconvert_callback);

  /* Close any imfile descriptors opened during image adjustment. */
  result = im_shared_filecloseall(page->im_shared) && result ;

  closeDLProgress(page, PRECONVERT_PROGRESS);

  return result;
}

/* Log stripped */
