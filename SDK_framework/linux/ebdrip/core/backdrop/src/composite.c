/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:composite.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * composite.c contains functions to handle the compositing of DL objects into a
 * backdrop.  Before a DL object is rendered bd_runInfo must be called to setup
 * the source and mask properties (contained in the CompositeContext) for the
 * object.  The renderer then produces blit spans which are sent to
 * bd_compositeSpan or blit blocks which are sent to bd_compositeBlock.
 * Additionally, to handle nested backdrops, there exists a specialised
 * function, bd_compositeBackdrop, to composite one backdrop into another.
 * Often one line contains the same data as the previous line and the backdrop
 * code maintains this information to avoid doing unnecessary work.  Repeated
 * lines come from block blits or from span blits that have been coalesced back
 * into blocks by the coalesce.c backdrop code.
 *
 * One layer below the three bd_composite* functions sits
 * bd_compositeSpanIntoBlock.  This function is responsible for loading the
 * inputs (from backdrop tables) and setting up the result pointers (into the
 * backdrop table) ready for a compositeColor function to perform the
 * compositing operation.  There are various specialised load and compositeColor
 * functions for different cases (eg, softmasks and PCL).
 *
 * bd_compositeSpanIntoBlock inserts a span into a single line of a single
 * backdrop block, although the calling code may set subsequent lines to be
 * repeats.  bd_compositeSpanIntoBlock can represent a line in an insertable
 * block as a map or a one span of RLE (allowing one span of RLE makes
 * initialising empty blocks cheaper).  In the map case each value in 'data'
 * contains an index into a backdrop table containing the color and other
 * related information (spot number, color type etc).  The indices in the data
 * are chosen carefully so that consecutive values effectively making up a run
 * all have the same index and that the index can be used to indicate the number
 * of consecutive pixels.  In other words, even though the data is stored as a
 * map the run length information is still present.
 *
 * For example, for runs of 0-3, 4-5 and 6-8 the table indices in the map would
 * be:
 *
 *           xi | 0 1 2 3 4 5 6 7 8
 *         data | 3 3 3 3 5 5 8 8 8
 *
 * The run length for a span starting at a given xi is calculated by:
 *   uint8 runLen = data[xi] - xi + 1;
 *
 * And color, alpha and info for run 0-3 is contained in slot 3 of the table.
 *
 * For insertable blocks, the backdrop data and tables are allocated large
 * enough to ensure that the worse case of every pixel being different can be
 * handled.  With the above method of assigning table indices there is no
 * searching for an available slot in the table and unused table entries do not
 * need to be expunged until at the end of compositing the block.
 */

#include "core.h"
#include "backdroppriv.h"
#include "composite.h"
#include "compositecolor.h"
#include "resource.h"
#include "iterator.h"
#include "display.h"
#include "pixelLabels.h"
#include "often.h"
#include "monitor.h"
#include "blitcolors.h"
#include "swerrors.h"
#include "hqmemcpy.h"

/**
 * The compositing context must be reset for each new source object.
 * Catch any abuses by wiping as much as possible in a debug build.
 */
void bd_wipeContext(CompositeContext *context, const Backdrop *backdrop)
{
#if defined( DEBUG_BUILD )
  /* Can't zero the whole structure because of CCE, PCL and coalesce. */
  uint32 i;
  /* Don't wipe context->bandBounds */
  context->spanX = context->spanY = -1;
  context->xiNext = context->yiNext = -1;
  context->source.opaque = FALSE;
  context->source.overprint = FALSE;
  context->source.forceProcessKOs = FALSE;
  context->source.opcode = RENDER_void;
  context->source.mappedSourceColor = FALSE;
  context->source.drawSource = FALSE;
  context->source.whiteSourceColor = FALSE;
  context->source.overprintFlags = NULL;
  for ( i = 0; i < backdrop->inComps; ++i ) {
    context->source.overprintFlagsBuf[i] = blit_channel_missing;
  }
  context->source.backdrop = NULL;
  /* Leave source->canPoach set to whatever was last set by bd_regionComplete. */
  context->source.color = NULL;
  context->source.alpha = COLORVALUE_INVALID;
  context->source.constantAlpha = COLORVALUE_INVALID;
  context->source.info = NULL;
  context->source.blendMode = CCEModeUnspecified;
  context->mask.backdrop = NULL;
  context->mask.block = NULL;
  context->mask.line = NULL;
  context->mask.xi = 0;
  context->mask.runIndex = 0;
  context->mask.data = NULL;
  context->mask.alpha = COLORVALUE_INVALID;
  context->background.block = NULL;
  context->background.line = NULL;
  context->background.xi = -1;
  context->background.data = NULL;
  context->background.color = NULL;
  context->background.alpha = COLORVALUE_INVALID;
  context->background.groupAlpha = COLORVALUE_INVALID;
  context->background.info = NULL;
  context->result.color = NULL;
  context->result.alpha = NULL;
  context->result.groupAlpha = NULL;
  context->result.info = NULL;
  context->loadRun = NULL;
  context->compositeColor = NULL;
#else
  UNUSED_PARAM(CompositeContext*, context);
  UNUSED_PARAM(const Backdrop*, backdrop);
#endif
}

/**
 * bd_runInfo is called once per object per region and is used to set the
 * properties of the incoming spans/blocks/backdrops.
 */
void bd_runInfo(CompositeContext *context,
                const Backdrop *backdrop,
                const LISTOBJECT *lobj,
                blit_color_t *color,
                Bool forceProcessKOs,
                int8 overrideColorType)
{
  Source *source = &context->source;
  STATEOBJECT *state = lobj->objectstate;
  int8 colorType = DISPOSITION_COLORTYPE(lobj->disposition);
  uint8 reproType = DISPOSITION_REPRO_TYPE(lobj->disposition);
  REPRO_COLOR_MODEL origColorModel;
  uint8 renderingIntent;
  GSC_BLACK_TYPE blackType;
  Bool independentChannels;

  HQASSERT(reproType < REPRO_N_TYPES, "Repro reproType is not valid");
  HQASSERT((lobj->spflags & RENDER_RECOMBINE) == 0,
           "Recombine pseudo colorants should already have been mapped");

  bd_coalesceFlush(context);

  bd_wipeContext(context, backdrop); /* debug builds only */

  /* xiNext and yiNext are the anticipated xi/yi values for the span.  They
     reduce setup work per span.  They need to be cleared for a new object. */
  context->xiNext = context->yiNext = -1;

  /* Set the mask properties of the object. */

  context->mask.backdrop = tranAttribSoftMaskBackdrop(lobj->objectstate->tranAttrib);
  context->mask.alpha = COLORVALUE_ONE;

  /* Set the source properties of the object. */

  source->alphaIsShape = state->tranAttrib->alphaIsShape;
  source->forceProcessKOs = (int8)forceProcessKOs;
  source->opcode = lobj->opcode;
  source->mappedSourceColor = FALSE;
  source->drawSource = TRUE;
  source->whiteSourceColor = FALSE;

  if ( lobj->opcode == RENDER_backdrop ) {
    /* Source object is a backdrop (compositing one backdrop into another). */
    bd_sourceColorBackdropSetup(context, backdrop, lobj->dldata.backdrop);
    /* Leave source->canPoach set to whatever was last set by bd_regionComplete. */
    source->coalescable = FALSE ;
  } else {
    source->backdrop = NULL;
    source->canPoach = FALSE;
    source->coalescable = (int8)((lobj->opcode == RENDER_fill ||
                                  lobj->opcode == RENDER_quad) &&
                                 !context->mask.backdrop) ;
  }

  source->alpha = COLORVALUE_ONE;
  source->constantAlpha = state->tranAttrib->alpha;
  if ( overrideColorType != GSC_UNDEFINED )
    colorType = overrideColorType;
  origColorModel = state->lateColorAttrib->origColorModel;
  renderingIntent = state->lateColorAttrib->renderingIntent;
  blackType = state->lateColorAttrib->blackType;
  independentChannels = state->lateColorAttrib->independentChannels;
  COLORINFO_SET(source->infoBuf, color->quantised.spotno, colorType, reproType,
                origColorModel, renderingIntent, blackType, independentChannels,
                lobj->spflags & (RENDER_BEGINPAGE|RENDER_ENDPAGE|RENDER_UNTRAPPED),
                pixelLabelFromDisposition(lobj->disposition));
  source->info = &source->infoBuf;

  source->blendMode = state->tranAttrib->blendMode;
  (void)cceSetBlendMode(source->cce, &source->blendMode, 1);

  /* Set the PCL properties of the object. */

  if ( context->pcl )
    pcl_runInfo(context->pcl, state->pclAttrib);
}

/**
 * About to insert a span and need to replace a repeat with the source of the
 * repeat.  Copy all the tables entries from the source line to the insertion
 * line and update data to match.
 */
static void bd_loadRepeat(BackdropBlock *block, uint32 nComps, uint32 yi)
{
  BackdropLine *lines = bd_blockLine(block, 0);
  BackdropTable* tableSrc, *tableDst;
  uint8 *dataSrc, *dataDst;
  uint32 yiSrc;

  HQASSERT(yi > 0, "Can't have a repeat on the first line of a block");
  HQASSERT(bd_isRLE(&lines[yi]) && lines[yi].nRuns == 1,
           "Repeat lines should be initialised to a single run");

  /* Search backwards until the line containing the source data is found. */
  yiSrc = bd_findRepeatSource(lines, yi);

  tableSrc = lines[yiSrc].table;
  tableDst = lines[yi].table;

  dataSrc = bd_blockData(block, yiSrc);
  dataDst = bd_blockData(block, yi);

  if ( bd_isRLE(&lines[yiSrc]) ) {
    /* Both source and insertion are RLE. */
    HQASSERT(lines[yiSrc].nRuns == 1, "Expected single spand of RLE only");
    bdt_copyEntry(tableSrc, tableDst, nComps, dataSrc[1], dataDst[1]);
  } else {
    /* Source is a map and insertion is RLE. */
    SlotIterator iterator;
    uint8 *slot;

    /* Copy table entries from the source table to the insertion table. */
    bd_slotIteratorInit(block, yiSrc, FALSE, &iterator);
    while ( (slot = bd_slotIterator(&iterator)) != NULL ) {
      bdt_copyEntry(tableSrc, tableDst, nComps, slot[0], slot[0]);
    }

    /* And copy the data from source. */
    HqMemCpy(dataDst, dataSrc, bd_blockWidth(block));

    bd_setMap(&lines[yi]); /* not RLE anymore */
  }

  lines[yi].repeat = FALSE; /* not a repeat anymore */
}

/**
 * Count the number of lines we can do in one go.
 */
static uint32 bd_countRepeats(CompositeContext *context,
                              uint32 yi, uint32 height,
                              BackdropLine *sLine,
                              BackdropLine *mLine,
                              BackdropLine *bLine)
{
  uint32 repeatCount = 0;

  /* Line repeats must be disabled if there's an active PCL pattern,
     because the result is dependent on spanX and spanY values. */
  if ( pcl_hasAttrib(context->pcl) && pcl_patternActive(context->pcl) )
    return 0;

  for ( ; yi < height &&
          (!sLine || sLine[yi].repeat) &&
          (!mLine || mLine[yi].repeat) &&
          (!bLine || bLine[yi].repeat);
        ++yi ) {
    ++repeatCount;
  }

  return repeatCount;
}

/**
 * For a given table and index, bd_loadSource sets up all the color, alpha and
 * info data for the source, ready for the compositeColor operation.
 */
static Bool bd_loadSource(CompositeContext *context,
                          const Backdrop *backdrop, BackdropTable *table,
                          uint8 sourceIndex)
{
  Source *source = &context->source;

  bdt_get(table, source->backdrop->outComps, sourceIndex,
          &source->color, &source->alpha, &source->shape, &source->info);

  /* An early return when source doesn't affect the composited result. */
  if ( source->info->label == 0 ||
       source->shape == COLORVALUE_ZERO ||
       (!backdrop->knockoutDescendant &&
        (source->alpha == COLORVALUE_ZERO ||
         source->constantAlpha == COLORVALUE_ZERO)) ) {
    return FALSE; /* ignore */
  } else {
    COLORVALUE_DIVIDE(source->alpha, source->shape, source->opacity);
    bd_sourceColorBackdrop(context, backdrop);
    return TRUE; /* draw */
  }
}

/**
 * For a given table and index, bd_loadMask sets up the alpha data for the mask,
 * ready for the compositeColor operation.
 */
static void bd_loadMask(Mask *mask, const Backdrop *backdrop, uint8 maskIndex)
{
  UNUSED_PARAM(const Backdrop*, backdrop);

  bdt_getAlpha(mask->line->table, maskIndex, &mask->alpha);
}

/**
 * For a given table and index, bd_loadBackground sets up all the color, alpha
 * and info data for the background, ready for the compositeColor operation.
 */
static void bd_loadBackground(Background *background,
                              const Backdrop *backdrop, uint8 backgroundIndex)
{
  uint32 inComps = backdrop->knockout
    ? backdrop->initial.backdrop->inComps : backdrop->inComps;

  bdt_get(background->line->table, inComps, backgroundIndex,
          &background->color, &background->alpha, &background->shape,
          &background->info);
  if ( !backdrop->knockout ) {
    /* result->info could alias background->info, so copy it */
    background->infoBuf = *background->info;
    background->info = &background->infoBuf;
  }

  if ( backdrop->isolated )
    background->groupAlpha = background->alpha;
  else if ( backdrop->knockout )
    background->groupAlpha = COLORVALUE_ZERO;
  else
    bdt_getGroupAlpha(background->line->table, backgroundIndex,
                      &background->groupAlpha);
}

static void bd_loadBackgroundForShape(Background *background,
                                      const Backdrop *backdrop,
                                      uint8 backgroundIndex)
{
  /* Background for shape loads the immediate color and alphas, regardless of
     the knockout flag. */
  bdt_get(background->line->table, backdrop->inComps, backgroundIndex,
          &background->color, &background->alpha, &background->shape,
          &background->info);
  if ( backdrop->isolated )
    background->groupAlpha = background->alpha;
  else
    bdt_getGroupAlpha(background->line->table, backgroundIndex,
                      &background->groupAlpha);
  /* Buffer the immediate color since it is replaced in the table before
     the weighted average calc uses it. */
  bd_copyColor(background->colorBuf, background->color, backdrop->inComps);
  background->color = background->colorBuf;
  background->info = NULL; /* Not used and would need copying if it were. */
}

/**
 * The bd_loadRun* functions determine the run length from xi for the current
 * line in a block, taking into account the mask, background and PCL pattern.
 * There are multiple variants, to reduce tests, since mask, background and
 * pattern are not always present.  For example if the background is the same
 * across this block line then it just needs to be loaded once and this is done
 * in bd_setLine.
 */
uint32 bd_loadRunNoOp(CompositeContext *context,
                      const Backdrop *backdrop,
                      uint32 xi, uint32 runLen)
{
  /* Used when background, mask and PCL pattern are not required or constant
     across this line in their respective blocks. */
  UNUSED_PARAM(CompositeContext*, context);
  UNUSED_PARAM(const Backdrop*, backdrop);
  UNUSED_PARAM(uint32, xi);
  return runLen;
}

uint32 bd_loadRunBackground(CompositeContext *context,
                            const Backdrop *backdrop,
                            uint32 xi, uint32 runLen)
{
  Background *background = &context->background;

  if ( background->data ) {
    /* Background is a map and the block insertable, and therefore we can make
       use of the property that run length can easily be derived from table
       index.  This makes finding xi and calculating the run length easy. */
    uint8 *data = background->data;
    uint32 run = data[xi] - xi + 1;

    HQASSERT(run > 0, "Can't have a zero length run");
    HQASSERT(!bd_isComplete(background->block), "Background block cannot be complete");
    HQASSERT(!bd_isRLE(background->line), "Expected background to be a map");

    if ( background->xi == -1 || data[xi] != data[background->xi] ) {
      bd_loadBackground(background, backdrop, data[xi]);
      background->xi = xi;
    }
    if ( runLen > run )
      runLen = run;
  }

  return runLen;
}

uint32 bd_loadRunMaskAndBackground(CompositeContext *context,
                                   const Backdrop *backdrop,
                                   uint32 xi, uint32 runLen)
{
  /* Mask */
  if ( context->mask.data ) {
    /* The mask block is complete and therefore we can either have RLE or map
       data.  If the data is a map then run length must be determined by testing
       adjacent indices. */
    Mask *mask = &context->mask;
    uint32 run;

    HQASSERT(bd_isComplete(mask->block), "Mask block must be complete");

    /* This assumes xi goes left to right and if this isn't the case bd_setLine
       is called to restart.  mask->xi refers to the next value to be loaded and
       we may need to skip over runs to find the one that covers xi. */
    if ( xi >= mask->xi ) {
      if ( bd_isRLE(context->mask.line) ) {
        run = readRunLen(mask->data[mask->runIndex]);
        while ( xi > (mask->xi + run - 1) ) {
          mask->xi += run;
          mask->runIndex += 2;
          run = mask->data[mask->runIndex];
        }
        run -= xi - mask->xi;
        bd_loadMask(mask, backdrop, mask->data[mask->runIndex + 1]);
      } else {
        run = 1;
        while ( run < runLen && mask->data[xi] == mask->data[xi + run] ) {
          ++run;
        }
        mask->xi = xi + run;
        bd_loadMask(mask, backdrop, context->mask.data[xi]);
      }
    } else {
      /* xi loaded already */
      run = mask->xi - xi;
    }
    HQASSERT(run > 0, "Can't have a zero length run");
    if ( runLen > run )
      runLen = run;
  }

  /* Background */
  runLen = bd_loadRunBackground(context, backdrop, xi, runLen);

  return runLen;
}

/** For sources with a fractional shape (in a knockout group) an extra background
    is required to track the immediate background. */
uint32 bd_loadRunFractionalShape(CompositeContext *context,
                                 const Backdrop *backdrop,
                                 uint32 xi, uint32 runLen)
{
  if ( context->backgroundForShape.data ) {
    /* Background is a map and the block insertable, and therefore we can make
       use of the property that run length can easily be derived from table
       index.  This makes finding xi and calculating the run length easy. */
    Background *background = &context->backgroundForShape;
    uint8 *data = background->data;
    uint32 run = data[xi] - xi + 1;

    HQASSERT(run > 0, "Can't have a zero length run");
    HQASSERT(!bd_isComplete(background->block), "Background block cannot be complete");
    HQASSERT(!bd_isRLE(background->line), "Expected background to be a map");

    if ( background->xi == -1 || data[xi] != data[background->xi] ) {
      bd_loadBackgroundForShape(background, backdrop, data[xi]);
      background->xi = xi;
    }
    if ( runLen > run )
      runLen = run;
  }

  /* Load the regular background and optionally the mask. */ 
  runLen = bd_loadRunMaskAndBackground(context, backdrop, xi, runLen);

  return runLen;
}

uint32 bd_loadRunPCLPatternAndBackground(CompositeContext *context,
                                         const Backdrop *backdrop,
                                         uint32 xi, uint32 runLen)
{
  /* Background */
  runLen = bd_loadRunBackground(context, backdrop, xi, runLen);

  /* PCL */
  runLen = pcl_loadRun(context->pcl, xi, runLen);

  return runLen;
}

/**
 * For each line in a block determine whether mask, background or PCL pattern
 * need to be tracked and loaded as spans are inserted into the line.  If a
 * property is consistent across the line then it only needs loading once for
 * the line.
 */
static void bd_setLine(CompositeContext *context,
                       const Backdrop* backdrop, uint32 yi)
{
  /* Mask */
  context->mask.data = NULL;
  if ( context->mask.block ) {
    Mask *mask = &context->mask;
    uint8 *data;

    bd_lineAndDataRepeatSrc(mask->block, yi, &mask->line, &data);

    if ( bd_isRLE(mask->line) && mask->line->nRuns == 1 ) {
      bd_loadMask(mask, backdrop, data[1]);
    } else {
      mask->xi = 0;
      mask->runIndex = 0;
      mask->data = data;
    }
  }

  /* Background */
  context->background.data = NULL;
  if ( context->background.block ) {
    Background *background = &context->background;
    uint8 *data;

    bd_lineAndDataRepeatSrc(background->block, yi, &background->line, &data);

    if ( bd_isRLE(background->line) ) {
      HQASSERT(background->line->nRuns == 1,
               "Only one run expected for insertable block");
      bd_loadBackground(background, backdrop, data[1]);
    } else {
      background->xi = -1;
      background->data = data;
    }
  }

  /* Background for AlphaIsShape */
  context->backgroundForShape.data = NULL;
  if ( context->backgroundForShape.block ) {
    Background *background = &context->backgroundForShape;
    uint8 *data;

    bd_lineAndDataRepeatSrc(background->block, yi, &background->line, &data);

    if ( bd_isRLE(background->line) ) {
      HQASSERT(background->line->nRuns == 1,
               "Only one run expected for insertable block");
      bd_loadBackgroundForShape(background, backdrop, data[1]);
    } else {
      background->xi = -1;
      background->data = data;
    }
  }

  /* PCL */
  if ( pcl_patternActive(context->pcl) ) {
    pcl_setPattern(context->pcl, context->spanX, context->spanY);
  }
}

/**
 * Some objects render multiple spans consecutively and in this case the
 * overhead of bd_setLine and the repeat handling can be reduced by tracking the
 * next expected xi value for a given yi.  The xiNext and yiNext hints are
 * cleared between objects and blocks.
 */
static void bd_prepareLine(CompositeContext *context,
                           const Backdrop *backdrop, BackdropBlock *block,
                           uint32 xi, uint32 yi, int32 repeatCount)
{
  if ( (int32)yi != context->yiNext ) {
    BackdropLine *yilines = bd_blockLine(block, yi);
    uint32 yNext;

    /* Check if this line is a repeat and load its values ready for span
       insertion. */
    if ( yilines[0].repeat )
      bd_loadRepeat(block, backdrop->inComps, yi);

    /* Check if the following line is a repeat and ensure it is still valid
       after the insertion. */
    yNext = yi + repeatCount + 1;
    if ( yNext < bd_blockHeight(block) &&
         yilines[repeatCount + 1].repeat )
      bd_loadRepeat(block, backdrop->inComps, yNext);

    context->yiNext = yi;
    context->xiNext = -1; /* Force xi reset since line changed. */
  }

  if ( (int32)xi != context->xiNext ) {
    bd_setLine(context, backdrop, yi);

    context->xiNext = xi;
  }
}

/**
 * Setup the mask and background blocks required to composite the current
 * backdrop block.
 */
static void bd_setBlock(CompositeContext *context,
                        const Backdrop *backdrop, uint32 bx, uint32 by)
{
  /* Mask */
  if ( context->mask.backdrop ) {
    Mask *mask = &context->mask;
    uint32 bb = bd_blockIndex(mask->backdrop, bx, by);

    mask->block = mask->backdrop->blocks[bb];
    if ( mask->block == NULL )
      mask->alpha = mask->backdrop->defaultMaskAlpha;
    else {
      HQASSERT(bd_isComplete(mask->block),
               "Mask block must be in complete mode");
      if ( bd_isUniform(mask->block) ) {
        bdt_getAlpha(bd_uniformTable(mask->block), 0, &mask->alpha);
        mask->block = NULL;
      }
    }
  } else {
    context->mask.block = NULL;
    context->mask.alpha = COLORVALUE_ONE;
  }

  /* Background */
  if ( backdrop->knockout ) {
    if ( backdrop->isolated ) {
      context->background.color = backdrop->initial.color;
      context->background.alpha = backdrop->initial.alpha;
      context->background.groupAlpha = backdrop->initial.groupAlpha;
      context->background.info = (COLORINFO*)&backdrop->initial.info; /* cast away const */
      context->background.block = NULL;
    } else {
      uint32 bb = bd_blockIndex(backdrop->initial.backdrop, bx, by);
      context->background.block = backdrop->initial.backdrop->blocks[bb];
      HQASSERT(!bd_isComplete(context->background.block),
               "Background block must not be complete");
    }
  } else {
    uint32 bb = bd_blockIndex(backdrop, bx, by);
    context->background.block = backdrop->blocks[bb];
    HQASSERT(!bd_isComplete(context->background.block),
             "Background block must not be complete");
  }

  /* Background for AlphaIsShape or a source backdrop with shape. */
  if ( context->source.alphaIsShape ||
       (context->source.backdrop != NULL &&
        context->source.backdrop->trackShape) ) {
    /* This background tracks immediate background regardless of KO flag. */
    uint32 bb = bd_blockIndex(backdrop, bx, by);
    context->backgroundForShape.block = backdrop->blocks[bb];
    HQASSERT(!bd_isComplete(context->backgroundForShape.block),
             "Background block must not be complete");
  } else
    context->backgroundForShape.block = NULL;

  /* Changed block so clear xiNext and yiNext hints. */
  context->xiNext = context->yiNext = -1;
}

/**
 * About to insert a new run but the existing run starts before xi and needs
 * fixing up to preserve the map and table relationship.  This involves copying
 * the existing run's table entry to a new index and updating the values in what
 * remains of the existing run.  A description of the map format for insertable
 * blocks is given at the top of this file.
 */
static void bd_adjustPrecedingRun(uint8 *data, BackdropTable *table,
                                  uint32 nComps, uint32 xi)
{
  uint8 oldIndex = data[xi];

  if ( xi > 0 && oldIndex == data[xi - 1] ) {
    uint8 newIndex = CAST_SIGNED_TO_UINT8(xi - 1);

    do {
      data[--xi] = newIndex;
    } while ( xi > 0 && data[xi - 1] == oldIndex );

    bdt_copyEntry(table, table, nComps, oldIndex, newIndex);
  }
}

/**
 * This function is responsible for loading the inputs (from backdrop tables)
 * and setting up the result pointers (into the backdrop table) ready for a
 * compositeColor function to perform the compositing operation.
 */
static void bd_compositeSpanIntoBlock(CompositeContext *context,
                                      const Backdrop *backdrop,
                                      BackdropBlock *block,
                                      uint32 xi, uint32 yi,
                                      uint32 runLenBlock, uint32 repeatCount)
{
  uint32 blockWidth = bd_blockWidth(block);
  BackdropLine *line = bd_blockLine(block, yi);
  uint8 *data = bd_blockData(block, yi);
  BackdropTable *table = line->table;
  uint8 newIndex;
  uint32 runLen;

  bd_prepareLine(context, backdrop, block, xi, yi, repeatCount);

  runLen = context->loadRun(context, backdrop, xi, runLenBlock);

  if ( runLen == blockWidth ) {
    /* Whole line in the block is the same. */
    HQASSERT(xi == 0, "xi must be zero");
    newIndex = CAST_SIGNED_TO_UINT8(runLen - 1);
    bdt_setResultPtrs(table, backdrop->inComps, newIndex, &context->result);
    if ( context->compositeColor(context, backdrop) ) {
      if ( !bd_isRLE(line) ) {
        data[0] = storeRunLen(runLen);
        data[1] = newIndex;
        bd_setRLE(line, 1);
      }
      xi += runLen;
    }
  } else {
    /* Prepare the current backdrop for insertion. */
    if ( bd_isRLE(line) ) {
      /* One run will not cover whole block width, convert RLE to map. */
      HQASSERT(data[1] == CAST_SIGNED_TO_UINT8(blockWidth - 1),
               "index for RLE data should match index for map");
      HqMemSet8(data, data[1], blockWidth);
      bd_setMap(line);
    }

    while ( runLenBlock > 0 ) {
      newIndex = CAST_SIGNED_TO_UINT8(xi + runLen - 1);
      bd_adjustPrecedingRun(data, table, backdrop->inComps, xi);
      bdt_setResultPtrs(table, backdrop->inComps, newIndex, &context->result);
      if ( context->compositeColor(context, backdrop) ) {
        if ( data[xi] != newIndex )
          HqMemSet8(&data[xi], newIndex, runLen);
      }
      xi += runLen;
      runLenBlock -= runLen;

      if ( runLenBlock > 0 )
        runLen = context->loadRun(context, backdrop, xi, runLenBlock);
    }
  }

  context->xiNext = xi;
}

/**
 * bd_compositeSpan is the backdrop function underlying the span blit.  It is
 * used to composite the spans from an object into the backdrop.  To make use of
 * the repeated line optimisations in the backdrop it is better to blit in
 * blocks where possible, but this function does attempt to coalese spans back
 * into blocks to get most of the benefit back.  Currently fills and quads
 * are always span blitted only.
 */
void bd_compositeSpan(CompositeContext *context, const Backdrop *backdrop,
                      dcoord x, dcoord y, dcoord runLen,
                      blit_color_t *sourceColor)
{
  uint32 xi, yi, bx, by, bb;

#if defined( DEBUG_BUILD )
  if ( (backdropDebug & BD_DEBUG_COMPOSITING) != 0 )
    monitorf((uint8 *)"bd_compositeSpan %x (%d,%d) +%d\n", backdrop, x, y, runLen);
#endif

  if ( !bd_sourceColorBlit(context, backdrop, sourceColor) )
    return; /* source is safe to ignore, eg completely transparent */

  /* - Ignore anything that isn't a fill or a quad.
     - If the object references a softmask then it must be composited
     immediately.  The softmask is reclaimed immediately after the last object
     referencing it is rendered, therefore we cannot wait for coalesceFlush to
     be called from bd_regionComplete. */
  if ( context->source.coalescable ) {
    bd_coalesceSpan(context, backdrop, sourceColor, y, x, runLen) ;
    return;
  }

  xi = bd_xPixelIndex(backdrop->shared, x);
  yi = bd_yPixelIndex(backdrop->shared, y);
  bx = bd_xBlockIndex(backdrop->shared, x);
  by = bd_yBlockIndex(backdrop->shared, y);
  bb = bd_blockIndex(backdrop, bx, by);

  context->spanX = x  - xi; /* spanX set to start of block */
  context->spanY = y;

  do {
    BackdropBlock *block = backdrop->blocks[bb];
    uint32 blockWidth = bd_blockWidth(block);
    uint32 runLenBlock = min((uint32)runLen, blockWidth - xi);

    if ( bd_isTouched(block) || !context->source.whiteSourceColor ) {
      HQASSERT(bx < backdrop->shared->xblock, "span overran blocks");
      bd_setBlock(context, backdrop, bx, by);

      bd_compositeSpanIntoBlock(context, backdrop, block, xi, yi, runLenBlock, 0);

      bd_setTouched(block, TRUE);
    }
    ++bb;
    ++bx;
    xi = 0; /* spans for subsequent blocks must start at zero */
    runLen -= runLenBlock;
    context->spanX += blockWidth;
  } while ( runLen > 0 );
}

/**
 * bd_compositeBlock is the backdrop function underlying the block blit.  It is
 * used to composite the blocks from an object into the backdrop.  Block
 * blitting gives a big performance benefit over repeatedly calling the span
 * function because the backdrop optimising repeated lines.
 */
void bd_compositeBlock(CompositeContext *context, const Backdrop *backdrop,
                       dcoord x, dcoord y, dcoord columns, dcoord rows,
                       blit_color_t *sourceColor)
{
#if defined( DEBUG_BUILD )
  if ( (backdropDebug & BD_DEBUG_COMPOSITING) != 0 )
    monitorf((uint8 *)"bd_compositeBlock %x (%d,%d) +%d,+%d\n",
             backdrop, x, y, columns, rows);
#endif

  /* No bd_coalesceFlush here or else coalesced blocks will never be
     composited! */

  if ( !bd_sourceColorBlit(context, backdrop, sourceColor) )
    return; /* source is safe to ignore, eg completely transparent */

  do {
    BackdropBlock *block;
    uint32  xi, yi, bx, by, bb, rowsThisBlock;
    dcoord runLen = columns;

    xi = bd_xPixelIndex(backdrop->shared, x);
    yi = bd_yPixelIndex(backdrop->shared, y);
    bx = bd_xBlockIndex(backdrop->shared, x);
    by = bd_yBlockIndex(backdrop->shared, y);
    bb = bd_blockIndex(backdrop, bx, by);

    block = backdrop->blocks[bb];
    rowsThisBlock = min((uint32)rows, bd_blockHeight(block) - yi);
    HQASSERT(rowsThisBlock > 0, "Must have some rows in this block");
    context->spanX = x - xi; /* spanX set to start of block */

    do {
      BackdropLine *yilines = bd_blockLine(block, yi);
      uint32 blockWidth = bd_blockWidth(block);
      uint32 runLenBlock = min((uint32)runLen, blockWidth - xi), yOff;

      if ( bd_isTouched(block) || !context->source.whiteSourceColor ) {
        BackdropLine *m_yilines = NULL;

        HQASSERT(bx < backdrop->shared->xblock, "span overran blocks");
        bd_setBlock(context, backdrop, bx, by);

        if ( context->mask.block )
          m_yilines = bd_blockLine(context->mask.block, yi);

        context->spanY = y;

        if ( context->source.opaque && !context->source.overprint &&
             runLenBlock == bd_blockWidth(block) &&
             !pcl_patternActive(context->pcl) ) {
          /* Source block is full block width, opaque and knocking out, and
             therefore obliterates existing content and can set some line repeats. */
          uint32 repeatCount = rowsThisBlock - 1;

          bd_compositeSpanIntoBlock(context, backdrop, block, xi, yi,
                                    runLenBlock, repeatCount);

          for ( yOff = 1; yOff < rowsThisBlock; ++yOff ) {
            /* The main thing here is to set the repeat flag.  The other
               settings are required because repeat lines are assumed to be set
               to a single run. */
            uint8 *data = bd_blockData(block, yi + yOff);
            data[0] = storeRunLen(blockWidth);
            data[1] = CAST_SIGNED_TO_UINT8(blockWidth - 1);
            bd_setRLE(&yilines[yOff], 1);
            yilines[yOff].repeat = TRUE;
          }
        } else {
          for ( yOff = 0; yOff < rowsThisBlock; ) {
            /* Count the number of lines we can do in one go. */
            uint32 repeatCount =
              bd_countRepeats(context, yOff + 1, rowsThisBlock,
                              NULL, m_yilines, yilines);

            bd_compositeSpanIntoBlock(context, backdrop, block, xi, yi + yOff,
                                      runLenBlock, repeatCount);

            context->spanY += repeatCount + 1;
            yOff += repeatCount + 1;
          }
        }

        bd_setTouched(block, TRUE);
      }
      ++bb;
      ++bx;
      block = backdrop->blocks[bb];
      xi = 0; /* spans for subsequent blocks must start at zero */
      runLen -= runLenBlock;
      context->spanX += blockWidth;
    } while ( runLen > 0 );

    rows -= rowsThisBlock;
    y += rowsThisBlock;
  } while ( rows > 0 );
}

/**
 * bd_compositeBackdrop is used to composite one backdrop into another and is
 * used for nested groups.  It shortcuts the most of the normal rendering
 * process.
 */
void bd_compositeBackdrop(CompositeContext *context,
                          const Backdrop *backdrop,  const dbbox_t *bounds)
{
  BlockIterator iterator = BLOCKITERATOR_INIT;
  const Backdrop *sourceBackdrop = context->source.backdrop;
  Bool more;

  bd_coalesceFlush(context);

  do {
    BackdropBlock *block, *sblock;
    uint32 bx, by, sbb, yi, blockWidth, blockHeight;

    more = bd_blockIterator(backdrop, bounds, & iterator);
    bd_iteratorPosition(&iterator, &bx, &by);
    block = *bd_iteratorLocateBlock(&iterator, backdrop);
    blockWidth = bd_blockWidth(block);
    blockHeight = bd_blockHeight(block);

    sbb = bd_blockIndex(sourceBackdrop, bx, by);
    sblock = sourceBackdrop->blocks[sbb];

#if defined( DEBUG_BUILD )
    if ( (backdropDebug & BD_DEBUG_COMPOSITING) != 0 )
      monitorf((uint8 *)"bd_compositeBackdrop %x->%x block %d\n",
               sourceBackdrop, backdrop, sbb);
#endif

    /* If there is no source data, there is nothing to do. */
    if ( sblock && bd_isTouched(sblock) ) {
      BackdropLine *lines = bd_blockLine(block, 0);
      BackdropLine *sLines = bd_blockLine(sblock, 0);
      BackdropLine *mLines = NULL;

#if defined( METRICS_BUILD )
      ++context->metrics.nPoachCandidates;
#endif
      if ( context->source.canPoach && !bd_isTouched(block) ) {
        /* Parent block is untouched, can simply swap blocks between backdrops. */
#if defined( METRICS_BUILD )
        ++context->metrics.nPoachedBlocks;
#endif
        bd_blockSwap(backdrop, sourceBackdrop, bx, by);
        continue;
      }

      bd_setBlock(context, backdrop, bx, by);

      if ( context->mask.block )
        mLines = bd_blockLine(context->mask.block, 0);

      /* Data may be extracted from the source block in three ways: as a uniform
         block, or, line by line, as RLE or a map. */
      if ( bd_isUniform(sblock) ) {
        if ( bd_loadSource(context, backdrop, bd_uniformTable(sblock), 0) ) {
          for ( yi = 0; yi < blockHeight; ) {
            /* Count the number of lines we can do in one go. */
            uint32 repeatCount =
              bd_countRepeats(context, yi + 1, blockHeight, NULL, mLines, lines);

            bd_compositeSpanIntoBlock(context, backdrop, block, 0 /* xi */, yi,
                                      blockWidth /* runLenBlock */, repeatCount);

            yi += repeatCount + 1;
          }
          bd_setTouched(block, TRUE);
        }
      } else { /* Non-uniform block */
        for ( yi = 0; yi < blockHeight; ) {
          uint32 syi = bd_findRepeatSource(sLines, yi);
          uint8 *sdata = bd_blockData(sblock, syi);

          /* Count the number of lines we can do in one go. */
          uint32 repeatCount =
            bd_countRepeats(context, yi + 1, blockHeight, sLines, mLines, lines);

          if ( bd_isRLE(&sLines[syi]) ) {
            uint32 n, xi = 0;

            for ( n = sLines[syi].nRuns; n > 0; --n ) {
              uint32 runLen = readRunLen(sdata[0]);

              if ( bd_loadSource(context, backdrop, sLines[syi].table, sdata[1]) )
                bd_compositeSpanIntoBlock(context, backdrop, block, xi, yi, runLen, repeatCount);

              xi += runLen;
              sdata += 2;
            }
          } else {
            uint32 runLen = 1, xi;

            for ( xi = 1; xi <= blockWidth; ++xi ) {
              if ( xi < blockWidth && sdata[xi - 1] == sdata[xi] ) {
                ++runLen;
              } else {
                if ( bd_loadSource(context, backdrop, sLines[syi].table, sdata[xi - runLen]) )
                  bd_compositeSpanIntoBlock(context, backdrop, block, xi - runLen, yi,
                                            runLen, repeatCount);
                runLen = 1;
              }
            }
          }
          yi += repeatCount + 1;
        }
        bd_setTouched(block, TRUE);
      }
    }
  } while ( more );

  SwOftenUnsafe();
}

/* Log stripped */
