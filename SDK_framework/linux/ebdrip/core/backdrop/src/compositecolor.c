/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:compositecolor.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This unit has bd_source* functions to map the incoming source color ready for
 * compositing, for example setting up overprint flags for overprinted objects.
 * There is a set of bd_compositeColor* functions to do the calculation from
 * source, mask, background to the result.
 */

#include "core.h"
#include "backdroppriv.h"
#include "compositecolor.h"
#include "composite.h"
#include "iterator.h"
#include "display.h"
#include "pixelLabels.h"
#include "often.h"
#include "monitor.h"
#include "memutil.h"
#include "compositers.h"
#include "halftone.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "gu_chan.h"
#include "swerrors.h"
#include "dl_image.h"
#include "spotlist.h"
#include "gschcms.h"

#if defined( ASSERT_BUILD )
void bd_checkColor(COLORVALUE *color, COLORVALUE alpha, uint32 nComps,
                   Bool premult)
{
  uint32 i;
  HQASSERT(alpha != COLORVALUE_INVALID && alpha <= COLORVALUE_MAX,
           "Alpha is out of range");
  for ( i = 0; i < nComps; ++i ) {
    HQASSERT(color[i] <= COLORVALUE_MAX, "Backdrop color out of range");
    if ( premult )
      HQASSERT(color[i] <= alpha,
               "Premultiplied value should not exceed alpha");
  }
}
#endif /* ASSERT_BUILD */

#if defined( DEBUG_BUILD )
static void bd_traceColor(char *name, COLORVALUE *color, uint32 nComps,
                          COLORVALUE alpha, COLORVALUE groupAlpha)
{
  if ( (backdropDebug & BD_DEBUG_RESULTCOLOR) != 0 ) {
    uint32 i;

    monitorf((uint8*)"%s=[", name);
    for ( i = 0; i < nComps; ++i ) {
      monitorf((uint8*)" %d", color[i]);
    }
    if ( alpha != COLORVALUE_INVALID )
      monitorf((uint8*)" a=%d ", alpha);
    if ( groupAlpha != COLORVALUE_INVALID )
      monitorf((uint8*)"ga=%d ", groupAlpha);
    monitorf((uint8*)"]\n");
  }
}
#else
#define bd_traceColor(name, color, nComps, alpha, groupAlpha)
#endif /* DEBUG_BUILD */

/** \todo MJ: I don't like this function but not sure how best to do this;
   Really the guts of it don't belong in Backdrop. */
static void bd_mergeSpots(CompositeContext *context, const Backdrop *backdrop)
{
  Source *source = &context->source;
  SPOTNO spotNoMerged = context->background.info->spotNo;
  uint32 i;

  HQASSERT(source->overprint &&
           source->info->spotNo != context->background.info->spotNo,
           "Merge screens only for overprinted spans of different spotnos");

  for ( i = 0; i < backdrop->inComps; ++i ) {
    if ( (source->overprintFlags[i] & blit_channel_present) != 0 ) {
      /* Only need to do this for real device colorants which are rendered. */
      COLORANTINDEX *mapping = backdrop->deviceColorantMapping[i];

      if ( mapping ) {
        do {
          SPOTNO spotNo = ht_mergespotnoentry(spotNoMerged,
                                              source->info->spotNo,
                                              source->info->reproType,
                                              *mapping,
                                              backdrop->shared->page->eraseno);
          spotNoMerged = (spotNo == 0 ? spotNoMerged : spotNo);
          HQASSERT(spotNoMerged != -1, "Bad spot no back from spot merge");

          if ( spotNoMerged == source->info->spotNo )
            /* Converegd to source spotNo, no further action required. */
            return;

          ++mapping;
        } while ( *mapping != COLORANTINDEX_UNKNOWN );
      }
    }
  }

  /* Add spot number to list of known spots to help final backdrop render handle
     screen switching efficiently. */
  if ( !spotlist_add_safe(backdrop->shared->page, spotNoMerged,
                          context->result.info->reproType) )
    HQFAIL("groupAddSpotNo failed; this condition should be handled properly");

  context->result.info->spotNo = spotNoMerged;
}

/**
 *  Apply a hierarchy to object types for color management that will occur
 * after compositing. Use the highest priority type of the source and
 * background objects.
 */
static void bd_arbitrateColorManagement(COLORINFO *backgroundInfo,
                                        COLORINFO *resultInfo)
{
  if (backgroundInfo->label == 0)
    return;

  if ( gsc_reproTypePriority(backgroundInfo->reproType) >
       gsc_reproTypePriority(resultInfo->reproType) ) {
    resultInfo->spotNo = backgroundInfo->spotNo;
    resultInfo->colorType = backgroundInfo->colorType;
    resultInfo->reproType = backgroundInfo->reproType;
    resultInfo->origColorModel = backgroundInfo->origColorModel;
  }
}

/**
 * Composites source and mask with the background, storing the composited color
 * in context->result.  By this point all the color inputs have been padded to a
 * common set of colorants.  This function returns true if the resulting color
 * is to be drawn and false if the color makes no contribution, because the
 * result is completely transparent and can safely be ignored.
 */
static Bool bd_compositeColor(CompositeContext *context,
                              const Backdrop *backdrop)
{
  Source *source = &context->source;
  Background *background = &context->background;
  CompositeResult *result = &context->result;
  COLORVALUE *sourceColor = source->color, sourceAlpha = source->alpha;
  COLORVALUE sourceOpacity = source->opacity, sourceShape = source->shape;
  uint32 inComps = backdrop->inComps;

  HQASSERT(source->info->label != 0,
           "Should have already checked for no pixel label");
  HQASSERT(!pcl_hasAttrib(context->pcl),
           "Should have called pcl_compositeColor instead");

  /* Can this span be ignored?  Have already checked source and ignored any
     transparent source spans, but still need to check the mask alpha.  If we
     are in a descendant of a knockout group then transparent spans cannot be
     ignored. */
  if ( !backdrop->knockoutDescendant &&
       context->mask.alpha == COLORVALUE_ZERO )
    return FALSE; /* ignore */

  bd_checkColor(sourceColor, sourceAlpha, inComps, FALSE /* premult */);
  bd_checkColor(background->color, background->alpha, inComps, TRUE /* premult */);

  if ( source->backdrop && !source->backdrop->isolated &&
       sourceAlpha != COLORVALUE_ZERO && sourceAlpha != COLORVALUE_ONE &&
       background->alpha != COLORVALUE_ZERO ) {
    /* Compositing a non-isolated backdrop into another backdrop.  Must first
       remove the initial backdrop contribution from the source backdrop to
       avoid it being counted a second time as source is composited with its
       parent.  Should have exactly the same set of colorants, as this only
       applies to backdrops which are non-isolated. */
    cceRemoveBackdropContribution(inComps, sourceColor, sourceAlpha,
                                  background->color, background->alpha,
                                  result->colorBuf);
    sourceColor = result->colorBuf;
    bd_checkColor(sourceColor, sourceAlpha, inComps, FALSE /* premult */);
  }

  if ( source->overprint ) {
    if ( source->opaque && background->alpha == COLORVALUE_ONE ) {
      /* No transparency involved so use conventional opaque overprinting model. */
      cceOpaqueOverprint(source->overprintFlags, inComps, sourceColor,
                         background->color, result->colorBuf);
    } else {
      /* Overprinting involving transparency is handled by creating an implicit
         non-isolated, non-KO group.  Overprint parameters like OverprintProcess
         have already been applied (when the DL objects were converted to blend
         space).  background->color and result->color may refer to the same
         memory so use result->colorBuf as temp space. */
      COLORVALUE overprintAlpha;

      cceCompatibleOverprint(source->overprintFlags, inComps,
                             sourceColor, sourceAlpha,
                             background->color, background->alpha,
                             result->colorBuf);
      cceCompositeAlpha(sourceAlpha, background->alpha, &overprintAlpha);
      cceDivideAlpha(inComps, result->colorBuf, overprintAlpha, result->colorBuf);
      cceRemoveBackdropContribution(inComps, result->colorBuf, sourceAlpha,
                                    background->color, background->alpha,
                                    result->colorBuf);
    }

    /* Result of overprinting now becomes the source. */
    sourceColor = result->colorBuf;
    bd_checkColor(sourceColor, sourceAlpha, inComps, FALSE /* premult */);
  }

  /* Mask and constant alpha values are interpreted as either opacity or shape
     values depending on alphaIsShape. */
  if ( source->alphaIsShape ) {
    COLORVALUE_MULTIPLY(sourceShape, source->constantAlpha, sourceShape);
    COLORVALUE_MULTIPLY(sourceShape, context->mask.alpha, sourceShape);
  } else {
    COLORVALUE_MULTIPLY(sourceOpacity, source->constantAlpha, sourceOpacity);
    COLORVALUE_MULTIPLY(sourceOpacity, context->mask.alpha, sourceOpacity);
  }
  sourceAlpha = COLORVALUE_INVALID; /* Don't update this unless needed later */

  /* Can this span be ignored (i.e. it's totally transparent and will not affect
     the composited result). */
  if ( sourceShape == COLORVALUE_ZERO ||
       (!backdrop->knockoutDescendant && sourceOpacity == COLORVALUE_ZERO) )
    return FALSE; /* ignore */

  /* Initialise info from the source object (some members may be changed
     later). */
  *result->info = *source->info;

  if ( sourceOpacity == COLORVALUE_ONE && sourceShape == COLORVALUE_ONE &&
       source->blendMode == CCEModeNormal ) {
    /* Normal blend mode and alpha/shape of one simplifies to PS paint model. */
    bd_copyColor(result->color, sourceColor, inComps);

    if ( source->overprint ) {
      GSC_BLACK_TYPE sourceBlackType;

      /* Use color management for the background object if it's object priority
         is higher. */
      bd_arbitrateColorManagement(background->info, result->info);

      /* If the span was overprinted, combine the labels of the source and
         backdrop, but don't add the composited flag. */
      result->info->label |= background->info->label;

      /* Combine 100% black if overprinting the black channel */
      sourceBlackType = COLORINFO_BLACK_TYPE(source->info->lcmAttribs);
      HQASSERT(sourceBlackType > 0 && sourceBlackType < BLACK_N_TYPES,
               "Unexpected black value");
      if ( sourceBlackType == BLACK_TYPE_ZERO )
        COLORINFO_SET_BLACK_TYPE(result->info->lcmAttribs,
                                 COLORINFO_BLACK_TYPE(background->info->lcmAttribs));

      /* When overprinting, the result is marked with independent channels only if
         both the backdrop and source are so marked */
      if ( !COLORINFO_INDEPENDENT_CHANNELS(background->info->lcmAttribs) )
        COLORINFO_SET_INDEPENDENT_CHANNELS(result->info->lcmAttribs, FALSE);
    }
  } else if ( background->alpha == COLORVALUE_ZERO &&
              sourceShape == COLORVALUE_ONE ) {
    /* Background is completely transparent so the result is source
       premultiplied, regardless of the source's alpha and blend mode. */
    cceMultiplyAlpha(inComps, sourceColor, sourceOpacity, result->color);

    /* When compositing, we don't do black preservation */
    COLORINFO_SET_BLACK_TYPE(result->info->lcmAttribs, BLACK_TYPE_NONE);
  } else {
    /* Composite source with background. */
    uint32 spotColorCount = inComps - backdrop->inProcessComps;
    CCE *cce = source->cce;

    cce->composite(backdrop->inProcessComps, sourceColor, sourceOpacity,
                   background->color, background->alpha, result->color);

    /* Blend the spot colors (if present) using the spot color compositer. */
    if ( spotColorCount > 0 ) {
      uint32 spotOffset = backdrop->inProcessComps;
      cce->compositeSpot(spotColorCount, sourceColor + spotOffset, sourceOpacity,
                         background->color + spotOffset, background->alpha,
                         result->color + spotOffset);
    }

    if ( sourceShape != COLORVALUE_ONE )
      /* Do shape weighted averaging on the result and immediate background. */
      cceWeightedAverage(inComps, result->color,
                         context->backgroundForShape.color,
                         context->backgroundForShape.alpha,
                         sourceShape, result->color);

    /* Use color management for the background object if it's object priority
       is higher. */
    bd_arbitrateColorManagement(background->info, result->info);

    /* The result is a composite. Combine both labels, and add the composited
       bit. */
    result->info->label |= background->info->label | SW_PGB_COMPOSITED_OBJECT;
    HQASSERT(result->info->label != SW_PGB_COMPOSITED_OBJECT,
             "Label should not be composited backgrounds only");

    /* Flags are cleared when compositing. Topmost opaque wins. */
    result->info->spflags &= ~(RENDER_BEGINPAGE|RENDER_ENDPAGE|
                               RENDER_UNTRAPPED);

    /* When compositing, we don't do black preservation */
    COLORINFO_SET_BLACK_TYPE(result->info->lcmAttribs, BLACK_TYPE_NONE);

    /* When compositing, the result is marked with independent channels only if
       both the background and source are so marked */
    if ( !COLORINFO_INDEPENDENT_CHANNELS(background->info->lcmAttribs) )
      COLORINFO_SET_INDEPENDENT_CHANNELS(result->info->lcmAttribs, FALSE);
  }

  /* Apply the backdrop label - this is used to give all spans composited in
     this backdrop a particular label. */
  result->info->label |= backdrop->backdropLabel;

  /* Calculate result alphas. Non-KO shape case reduces to standard equations.
     Non-isolated groups additionally require a group alpha which excludes the
     group backdrop's alpha. */
  if ( sourceShape != COLORVALUE_ONE && backdrop->knockout ) {
    /* Do shape-weighted averaging on the result and immediate background.
       Isolated case simplifies down with background->alpha = 0. */
    if ( backdrop->isolated ) {
      cceWeightedAverageAlpha(sourceOpacity, context->backgroundForShape.alpha,
                              sourceShape, result->alpha);
    } else {
      cceWeightedAverageAlpha(sourceOpacity, context->backgroundForShape.groupAlpha,
                              sourceShape, result->groupAlpha);
      cceCompositeAlpha(background->alpha, *result->groupAlpha, result->alpha);
    }
  } else {
    if ( sourceShape == COLORVALUE_ONE )
      sourceAlpha = sourceOpacity;
    else
      COLORVALUE_MULTIPLY(sourceOpacity, sourceShape, sourceAlpha);
    cceCompositeAlpha(sourceAlpha, background->alpha, result->alpha);
    if ( !backdrop->isolated )
      cceCompositeAlpha(sourceAlpha, background->groupAlpha, result->groupAlpha);
  }

  /* Calculate and store result shape only if parent is a KO descendant. */
  if ( backdrop->trackShape )
    cceCompositeAlpha(context->backgroundForShape.shape, sourceShape, result->shape);

  HQASSERT(*result->alpha <= COLORVALUE_MAX, "invalid alpha shape");
  HQASSERT(result->groupAlpha == NULL || *result->groupAlpha <= COLORVALUE_MAX,
           "invalid group alpha");
  HQASSERT(background->alpha <= *result->alpha,
           "Result alpha should not be less than backdrop alpha");

  if ( backdrop->shared->mergeSpots &&
       source->overprint && source->info->spotNo != background->info->spotNo )
    bd_mergeSpots(context, backdrop);

  bd_checkColor(result->color, *result->alpha, inComps, TRUE /* premult */);
  COLORTYPE_ASSERT(result->info->colorType, "bd_compositeColor");
  HQASSERT(result->info->spotNo > 0, "Invalid spotNo");

  bd_traceColor(" res", result->color, inComps, *result->alpha,
                (!backdrop->isolated ? *result->groupAlpha : COLORVALUE_INVALID));

  return TRUE; /* draw */
}

static Bool bd_compositeColorOpaqueKnockout(CompositeContext *context,
                                            const Backdrop *backdrop)
{
  Source *source = &context->source;
  CompositeResult *result = &context->result;

  HQASSERT(source->opaque, "This is supposed to be the opaque case!");
  HQASSERT(!source->overprint, "Expected overprinting off");
  HQASSERT(source->info->label != 0,
           "Should have already checked for no pixel label");
  HQASSERT(!pcl_hasAttrib(context->pcl),
           "Should have called pcl_compositeColor instead");

  bd_checkColor(source->color, source->alpha,
                backdrop->inComps, FALSE /* premultiplied */);

  bd_copyColor(result->color, source->color, backdrop->inComps);

  *result->info = *source->info;

  /* Apply the backdrop label - this is used to give all spans composited in
     this backdrop a particular label. */
  result->info->label |= backdrop->backdropLabel;

  *result->alpha = source->alpha;

  /* Non-isolated groups additional require a group alpha
     which excludes the group backdrop's alpha. */
  if ( !backdrop->isolated )
    *result->groupAlpha = source->alpha;

  /* Store result shape (always 1 here) if parent is a KO descendant. */
  if ( backdrop->trackShape )
    *result->shape = COLORVALUE_ONE;

  bd_checkColor(result->color, *result->alpha,
                backdrop->inComps, TRUE /* premultiplied */);
  COLORTYPE_ASSERT(result->info->colorType, "bd_compositeColorOpaqueKnockout");
  HQASSERT(result->info->spotNo > 0, "Invalid spotNo");

  bd_traceColor(" res", result->color, backdrop->inComps, *result->alpha,
                (!backdrop->isolated ? *result->groupAlpha : COLORVALUE_INVALID));

  return TRUE; /* draw */
}

static Bool bd_compositeColorOpaqueOverprint(CompositeContext *context,
                                             const Backdrop *backdrop)
{
  Source *source = &context->source;
  Background *background = &context->background;
  CompositeResult *result = &context->result;
  GSC_BLACK_TYPE sourceBlackType;

  HQASSERT(source->opaque, "This is supposed to be the opaque case!");
  HQASSERT(source->overprint, "Expected overprinting on");
  HQASSERT(source->info->label != 0,
           "Should have already checked for no pixel label");
  HQASSERT(!pcl_hasAttrib(context->pcl),
           "Should have called pcl_compositeColor instead");

  bd_checkColor(source->color, source->alpha,
                backdrop->inComps, FALSE /* premultiplied */);
  bd_checkColor(background->color, background->alpha,
                backdrop->inComps, TRUE /* premultiplied */);

  if ( background->alpha == COLORVALUE_ONE ) {
    /* No transparency involved so use conventional opaque overprinting model. */
    cceOpaqueOverprint(source->overprintFlags, backdrop->inComps,
                       source->color, background->color,
                       result->color);
  } else {
    /* Overprinting involving transparency is handled by creating an implicit
       non-isolated, non-KO group.  Overprint parameters like OverprintProcess
       have already been applied (when the DL objects were converted to blend
       space).  background->color and result->color may refer to the same memory
       so use result->colorBuf as temp space. */
    COLORVALUE overprintAlpha;

    cceCompatibleOverprint(source->overprintFlags, backdrop->inComps,
                           source->color, source->alpha,
                           background->color, background->alpha,
                           result->colorBuf);
    cceCompositeAlpha(source->alpha, background->alpha, &overprintAlpha);

    cceDivideAlpha(backdrop->inComps, result->colorBuf, overprintAlpha,
                   result->colorBuf);

    cceRemoveBackdropContribution(backdrop->inComps,
                                  result->colorBuf, source->alpha,
                                  background->color, background->alpha,
                                  result->color);
  }

  *result->info = *source->info;

  /* Use color management for the background object if it's object priority
     is higher. */
  bd_arbitrateColorManagement(background->info, result->info);

  /* Combine the labels of the source and backdrop, but don't add the composited
     flag.  Apply the backdrop label - this is used to give all spans composited
     in this backdrop a particular label. */
  result->info->label |= background->info->label | backdrop->backdropLabel;

  /* Combine 100% black if overprinting the black channel */
  sourceBlackType = COLORINFO_BLACK_TYPE(source->info->lcmAttribs);
  HQASSERT(sourceBlackType > 0 && sourceBlackType < BLACK_N_TYPES,
           "Unexpected black value");
  if ( sourceBlackType == BLACK_TYPE_ZERO )
    COLORINFO_SET_BLACK_TYPE(result->info->lcmAttribs,
                             COLORINFO_BLACK_TYPE(background->info->lcmAttribs));

  /* When overprinting, the result is marked with independent channels only if
     both the backdrop and source are so marked */
  if ( !COLORINFO_INDEPENDENT_CHANNELS(background->info->lcmAttribs) )
    COLORINFO_SET_INDEPENDENT_CHANNELS(result->info->lcmAttribs, FALSE);

  *result->alpha = source->alpha;

  /* Non-isolated groups additional require a group alpha
     which excludes the group backdrop's alpha. */
  if ( !backdrop->isolated )
    *result->groupAlpha = source->alpha;

  /* Store result shape (always 1 here) if parent is a KO descendant. */
  if ( backdrop->trackShape )
    *result->shape = COLORVALUE_ONE;

  if ( backdrop->shared->mergeSpots &&
       source->info->spotNo != background->info->spotNo )
    bd_mergeSpots(context, backdrop);

  bd_checkColor(result->color, *result->alpha,
                backdrop->inComps, TRUE /* premultiplied */);
  COLORTYPE_ASSERT(result->info->colorType, "bd_compositeColorOpaqueOverprint");
  HQASSERT(result->info->spotNo > 0, "Invalid spotNo");

  bd_traceColor(" res", result->color, backdrop->inComps, *result->alpha,
                (!backdrop->isolated ? *result->groupAlpha : COLORVALUE_INVALID));

  return TRUE; /* draw */
}

/**
 * Returns true if the current source color is white.
 */
static int8 bd_sourceColorWhite(CompositeContext *context,
                                const Backdrop *backdrop)
{
  COLORVALUE *color = context->source.color;
  uint32 i;

  for ( i = 0; i < backdrop->inComps; ++i ) {
    if ( color[i] < COLORVALUE_ONE )
      return FALSE;
  }

  return TRUE;
}

/**
 * Finishes off source color manipulation into a convenient form for the
 * backdrop, after the sourceColor pointer has been set and basic unpacking has
 * been done.
 */
void bd_sourceColorComplete(CompositeContext *context, const Backdrop *backdrop)
{
  HQASSERT(backdrop, "backdrop is null");

  HQASSERT(!context->source.mappedSourceColor,
           "Source color should already be complete");

  /* For chars and rects etc which have a single color over the whole object
     we only need to map sourceColor once and re-use the results - all spans
     until the next bd_runInfo will have the same color.  Otherwise, for
     multi-colored objects like images we need to map every incoming span color.
  */
  switch ( context->source.opcode ) {
  case RENDER_char:
  case RENDER_rect:
  case RENDER_quad:
  case RENDER_fill:
  case RENDER_mask:
    context->source.mappedSourceColor = TRUE;
    break;
  case RENDER_image:
  case RENDER_gouraud:
  case RENDER_backdrop:
    break;
  case RENDER_shfill:
  case RENDER_shfill_patch:
  case RENDER_vignette:
  case RENDER_hdl:
  case RENDER_group:
  case RENDER_void:
    HQFAIL("Object should not be calling blitter");
    break;
  default:
    HQFAIL("Unexpected listobject type");
    break;
  }

  context->source.whiteSourceColor = FALSE;

  if ( pcl_hasAttrib(context->pcl) ) {
    Bool opaque;

    pcl_sourceColor(context, &opaque);
    context->source.opaque = (int8)opaque;

    context->compositeColor = pcl_compositeColor;
    if ( pcl_patternActive(context->pcl) )
      context->loadRun = bd_loadRunPCLPatternAndBackground;
    else
      context->loadRun = bd_loadRunBackground;
  } else {
    context->source.opaque =
      (int8)(context->source.shape == COLORVALUE_ONE &&
             context->source.alpha == COLORVALUE_ONE &&
             context->source.constantAlpha == COLORVALUE_ONE &&
             context->source.blendMode == CCEModeNormal &&
             context->mask.alpha == COLORVALUE_ONE &&
             context->mask.backdrop == NULL);

    /* Set the compositeColor and loadRun functions to the most appropriate
       specialised functions to minimise the number of tests. */
    if ( context->source.opaque ) {
      if ( context->source.overprint ) {
        context->compositeColor = bd_compositeColorOpaqueOverprint;
        context->loadRun = bd_loadRunBackground;
      } else {
        context->compositeColor = bd_compositeColorOpaqueKnockout;
        context->loadRun = bd_loadRunNoOp;
      }
    } else {
      context->compositeColor = bd_compositeColor;
      if ( context->source.alphaIsShape ||
           context->source.shape != COLORVALUE_ONE )
        context->loadRun = bd_loadRunFractionalShape;
      else if ( context->mask.backdrop )
        context->loadRun = bd_loadRunMaskAndBackground;
      else
        context->loadRun = bd_loadRunBackground;
    }

    /* Note that we only use the white-on-white optimisation within non-isolated
       groups. Within isolated groups, a change in alpha (isolated groups have
       zero-alpha initially) is just as important as a change in color, as far
       as some blend modes are concerned. */
    if ( backdrop->compositeToPage && !backdrop->isolated &&
         context->source.opaque ) {
      /* Normal opaque case. Can optimise away span insertion if it is
         "white-on-white". This is particularly beneficial for LW-like jobs which
         contain numerous white-only objects. */

      /** \todo TODO FIXME @@@
         *** Note this optimisation will result in slightly different behaviour
         for non-white erase colors or where a subsequent transparent object
         composites with the init color (and alpha of zero) instead of the opaque
         white object. For now we've decided to live with these two potential
         issues for the sake of a substantial performance improvement. *** */
      context->source.whiteSourceColor = bd_sourceColorWhite(context, backdrop);
    }
  }
}

/**
 * Compositing a backdrop into its parent so setup overprintFlags and colorBuf
 * as appropriate for all the spans from the source backdrop.
 */
void bd_sourceColorBackdropSetup(CompositeContext *context,
                                 const Backdrop *backdrop,
                                 const Backdrop *sourceBackdrop)
{
  Source *source = &context->source;
  uint32 i;

  HQASSERT(backdrop, "backdrop is null");
  HQASSERT(sourceBackdrop,
           "Should only be used when compositing a backdrop to its parent");
  HQASSERT(!source->color, "Should not have loaded a source color yet");
  HQASSERT(!source->mappedSourceColor,
           "Shouldn't have mapped the source color yet");
  HQASSERT(sourceBackdrop->outComps <= backdrop->inComps,
           "Source colour does not have subset of backdrop components");

  source->backdrop = sourceBackdrop;

  /* Only need to act on overprint when have an overprinted
     omitted colorant or a colorant set to max blt. */
  source->overprint = FALSE;

  /* We've asserted in bd_compositeBackdrop() that the source colorant set is a
     strict prefix of the current colorant set, so if the number of components
     matches, we can just use it directly. */
  if ( source->backdrop->outComps != backdrop->inComps ) {
    int32 *mapping = source->backdrop->parentMapping;

    HQASSERT(backdrop == source->backdrop->parentBackdrop,
             "Source's parentBackdrop must refer to current backdrop");

    for ( i = 0; i < backdrop->inComps; ++i ) {
      int32 mi = mapping[i];
      if ( mi < 0 ) {
        source->overprintFlagsBuf[i] = blit_channel_missing; /* Overprint */
        source->colorBuf[i] = COLORVALUE_ONE;
      } else {
        source->overprintFlagsBuf[i] = blit_channel_present; /* Knockout */
        /* source->colorBuf[i] set in bd_sourceColorBackdrop */
      }
    }
  } else {
    /** \todo ajcd 2008-10-08: Should these ever be set to maxblt or
        overprint? */
    for ( i = 0; i < backdrop->inComps; ++i ) {
      source->overprintFlagsBuf[i] = blit_channel_present; /* Knockout */
    }
  }
  source->overprintFlags = source->overprintFlagsBuf ;
}

/**
 * Compositing a backdrop into its parent so setup overprintFlags and colorBuf
 * as appropriate for all the spans from the source backdrop.
 */
void bd_sourceColorBackdrop(CompositeContext *context, const Backdrop *backdrop)
{
  Source *source = &context->source;

  HQASSERT(backdrop, "backdrop is null");
  HQASSERT(source->backdrop,
           "Should only be used when compositing a backdrop to its parent");
  HQASSERT(source->color, "Should have loaded a source color");
  HQASSERT(!source->mappedSourceColor,
           "Shouldn't have mapped the source color yet");
  HQASSERT(source->backdrop->outComps <= backdrop->inComps,
           "Source colour does not have subset of backdrop components");
  HQASSERT(!source->overprint, "Overprint should be set to false");

  /* If the colorants are the same and in the same order then use the
     source->color directly.  Otherwise use colorBuf and the parentMapping to
     convert the source color to match backdrop colorants. */
  if ( source->backdrop->parentMapping ) {
    int32 *mapping = source->backdrop->parentMapping;
    uint32 i;

    HQASSERT(backdrop == source->backdrop->parentBackdrop,
             "Source's parentBackdrop must refer to current backdrop");

    for ( i = 0; i < backdrop->inComps; ++i ) {
      int32 mi = mapping[i];
      if ( mi < 0 ) {
        HQASSERT(source->overprintFlags[i] == blit_channel_missing, /* Overprint */
                 "overprintFlags should be set to overprint");
        HQASSERT(source->colorBuf[i] == COLORVALUE_ONE,
                 "Overprinted color should be set to one");
      } else {
        HQASSERT(source->overprintFlags[i] == blit_channel_present, /* Knockout */
                 "overprintFlags should be set to paint");
        source->colorBuf[i] = source->color[mi];
      }
    }
    source->color = source->colorBuf;
  }

  bd_sourceColorComplete(context, backdrop);
}

/**
 * Converts a blit color sourceColor into a convenient form for the backdrop.
 */
Bool bd_sourceColorBlit(CompositeContext *context, const Backdrop *backdrop,
                        blit_color_t *sourceColor)
{
  Source *source = &context->source;
  channel_index_t index ;

  HQASSERT(source->backdrop == NULL,
           "Source backdrop should not be set during blitting phase");

  if ( source->mappedSourceColor )
    /* We've done this already, and the results are still valid. */
    return source->drawSource;

  VERIFY_OBJECT(sourceColor, BLIT_COLOR_NAME);
  VERIFY_OBJECT(sourceColor->map, BLIT_MAP_NAME);

  /* Quickie asserts for rough check that mapping hasn't changed. */
  HQASSERT(backdrop->inComps == sourceColor->map->ncolors,
           "Source color mapping cannot match group union");
  HQASSERT(sourceColor->valid & blit_color_quantised, "Blit color is not quantised");
  HQASSERT(sourceColor->map->all_index >= sourceColor->map->nchannels,
           "/All channel should not be present");

  source->alpha = sourceColor->alpha;
  if ( source->alphaIsShape ) {
    source->shape = source->alpha;
    source->opacity = COLORVALUE_ONE;
  } else {
    source->shape = COLORVALUE_ONE;
    source->opacity = source->alpha;
  }

  /* An early return when source doesn't affect the composited result. */
  if ( source->info->label == 0 ||
       source->shape == COLORVALUE_ZERO ||
       (!backdrop->knockoutDescendant &&
        (source->alpha == COLORVALUE_ZERO ||
         source->constantAlpha == COLORVALUE_ZERO)) ) {
    source->drawSource = FALSE;
    return FALSE; /* ignore */
  }

  source->drawSource = TRUE;

  /* Only need to act on overprint when have an overprinted
     omitted colorant or a colorant set to max blt. */
  source->overprint = FALSE;

  /* We can use the blit color's packed data directly if we don't need to
     perform any mappings or colorvalue modification. This will be the case
     if:

     1) This color has no overprinted channels.
     2) This color has no maxblitted channels.
     3) We're not coalescing spans. In this case, we defer reading the
        color from the source until the flush happens, which might be at the
        start of the next object. By that time, the blit color may have
        changed.
  */
  if ( sourceColor->ncolors != backdrop->inComps ||
       blit_color_maxblitted(sourceColor) ) { /* Overprints exist */
    HQASSERT(sourceColor->map->ncolors == backdrop->inComps,
             "Number of colors differ between backdrop and blitmap") ;

    /* Iterate over all color channels. */
    for ( index = 0; index < backdrop->inComps; ++index ) {
      COLORVALUE cv ;
      blit_channel_state_t state = sourceColor->state[index] ;

      if ( (state & blit_channel_present) != 0 ) {
        cv = sourceColor->quantised.qcv[index] ;

        if ( (state & blit_channel_maxblit) != 0 )
          source->overprint = TRUE;
      } else { /* Overprinted channel */
        cv = COLORVALUE_ONE;

        /* If forceProcessKOs is set, the process components are always painted
           for incoming spans.  Process components not present in the source
           spans are padded with COLORVALUE_ONE.  This is used for expanding
           overprinted gray images to CMYK (normally this is handled by the
           front-end color chain, except when the object went through recombine
           interception).  The backdrop already assumes the process colorants
           come first (look at bd_compositeColor). */
        if ( source->forceProcessKOs && index < backdrop->inProcessComps ) {
          state |= blit_channel_present|blit_channel_knockout; /* forced knockout */
        } else {
          /** \todo ajcd 2010-04-13: Why not set cv to COLORVALUE_TRANSPARENT? */
          source->overprint = TRUE;
        }
      }

      source->colorBuf[index] = cv ;
      source->overprintFlagsBuf[index] = state ;
    }

    source->color = source->colorBuf;
    source->overprintFlags = source->overprintFlagsBuf;
  } else if ( source->coalescable ) {
    /* Not overprinted, but we cannot use the blit_color_t directly. */
    for ( index = 0 ; index < backdrop->inComps ; ++index ) {
      HQASSERT(backdrop->inColorants[index] == sourceColor->map->channel[index].ci,
               "Mapping from blit colorant to backdrop colorant order is wrong");
      HQASSERT((sourceColor->state[index] & (blit_channel_present|blit_channel_maxblit))
               == blit_channel_present,
               "Blit color must not be overprinted for direct copy") ;
      source->colorBuf[index] = sourceColor->quantised.qcv[index];
      source->overprintFlagsBuf[index] = sourceColor->state[index];
    }

    source->color = source->colorBuf;
    source->overprintFlags = source->overprintFlagsBuf;
  } else {
    /* Can use blit color fields directly. */
    source->color = sourceColor->quantised.qcv ;
    source->overprintFlags = sourceColor->state ;
  }

  bd_sourceColorComplete(context, backdrop);

  return TRUE; /* draw source */
}

/* Log stripped */
