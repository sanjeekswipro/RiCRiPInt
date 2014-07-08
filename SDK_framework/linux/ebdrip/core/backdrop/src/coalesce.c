/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:coalesce.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Coalesce spans back into blocks to allow the line repeat optimisation to be
 * applied.  The coalescing code is a workaround for fill objects not using
 * block blits.  Block blitting is required for the line repeat optimisation to
 * apply, so this code coalesces spans from span blits back into blocks.  The
 * initial run is first broken up into sub-runs (modulo block width) to increase
 * the number of blocks being coalesced.  Each block can grow and be flushed
 * independently.
 */

#include "core.h"
#include "backdroppriv.h"
#include "composite.h"
#include "compositecolor.h"
#include "blitcolors.h"
#include "display.h"
#include "swerrors.h"
#include "monitor.h"

struct SpanCoalesce {
  const Backdrop *backdrop;
  uint32    numBlocks;
  dbbox_t  *blocks;
  uint32    x1Hint, x2Hint;
};

Bool bd_coalesceNew(int32 xsize, mm_cost_t cost, SpanCoalesce **newCoalesce)
{
  SpanCoalesce *coalesce, init = {0};
  uint32 numBlocks = (xsize + BLOCK_DEFAULT_WIDTH - 1) / BLOCK_DEFAULT_WIDTH;

  coalesce = bd_resourceAllocCost(sizeof(SpanCoalesce), cost);
  if ( coalesce == NULL )
    return error_handler(VMERROR);

  HQASSERT(numBlocks > 0, "Coalesce block width is zero") ;

  *coalesce = init;
  coalesce->numBlocks = numBlocks;
  coalesce->blocks = bd_resourceAllocCost(numBlocks * sizeof(dbbox_t), cost);
  if ( coalesce->blocks == NULL ) {
    bd_coalesceFree(&coalesce);
    return error_handler(VMERROR);
  }

  bd_coalesceInit(coalesce);

  *newCoalesce = coalesce;
  return TRUE;
}

void bd_coalesceFree(SpanCoalesce **freeCoalesce)
{
  if ( *freeCoalesce != NULL ) {
    SpanCoalesce *coalesce = *freeCoalesce;

    bd_resourceFree(coalesce->blocks, coalesce->numBlocks * sizeof(dbbox_t));
    bd_resourceFree(coalesce, sizeof(SpanCoalesce));
    *freeCoalesce = NULL;
  }
}

void bd_coalesceInit(SpanCoalesce *coalesce)
{
  uint32 i;

  HQASSERT(coalesce != NULL, "Coalesce object missing");
  HQASSERT(coalesce->blocks != NULL, "No blocks in coalesce structure");

  for ( i = 0; i < coalesce->numBlocks; ++i ) {
    bbox_clear(&coalesce->blocks[i]);
  }

  coalesce->x1Hint = coalesce->numBlocks - 1;
  coalesce->x2Hint = 0;
  coalesce->backdrop = NULL;
}

static void bd_coalesceFlushBlock(CompositeContext *context, uint32 bi)
{
  SpanCoalesce *coalesce = context->coalesce;
  dbbox_t *block = &coalesce->blocks[bi];

  if ( !bbox_is_empty(block) ) {
    dbbox_t tblock = *block;
    bbox_clear(block);

#if defined( DEBUG_BUILD )
    {
      static Bool debugCoalesce = FALSE;
      if ( debugCoalesce )
        monitorf((uint8*)"coalesceFlushBlock: rows %d, cols %d, bbox (%d %d %d %d)]\n",
                 tblock.y2 - tblock.y1 + 1, tblock.x2 - tblock.x1 + 1,
                 tblock.x1, tblock.y1, tblock.x2, tblock.y2);
    }
#endif

    HQASSERT(context->source.mappedSourceColor,
             "Expected sourceColor to be mapped already");
    bd_compositeBlock(context, coalesce->backdrop,
                      tblock.x1, tblock.y1,
                      tblock.x2 - tblock.x1 + 1,
                      tblock.y2 - tblock.y1 + 1,
                      NULL /* sourceColor mapped already */);

    if ( coalesce->x1Hint == bi )
      coalesce->x1Hint++;
    if ( bi > 0 && coalesce->x2Hint == bi )
      coalesce->x2Hint--;
  }
}

void bd_coalesceFlush(CompositeContext *context)
{
  SpanCoalesce *coalesce = context->coalesce;
  uint32 bi;

  HQASSERT(coalesce->blocks, "No blocks in coalesce structure") ;

  for ( bi = coalesce->x1Hint; bi <= coalesce->x2Hint; ++bi ) {
    bd_coalesceFlushBlock(context, bi);
  }

  coalesce->x1Hint = coalesce->numBlocks - 1;
  coalesce->x2Hint = 0;
  coalesce->backdrop = NULL;
}

static void bd_coalesceSubSpan(CompositeContext *context,
                               const Backdrop *backdrop, uint32 bi,
                               blit_color_t *sourceColor,
                               dcoord y, dcoord x1, dcoord x2)
{
  SpanCoalesce *coalesce = context->coalesce;
  dbbox_t *block = &coalesce->blocks[bi];

  HQASSERT(coalesce->blocks, "No blocks in coalesce structure") ;
  HQASSERT(coalesce->backdrop == NULL || coalesce->backdrop == backdrop,
           "A change of backdrop should have forced a flush");
  coalesce->backdrop = backdrop;

  /* Try to extend an existing block vertically. */
  if ( y == (block->y2 + 1) &&
       x1 == block->x1 && x2 == block->x2 ) {
    block->y2 = y;
    return;
  }

  /* Flush the block and start again. */
  bd_coalesceFlushBlock(context, bi);
  (void)bd_sourceColorBlit(context, backdrop, sourceColor);
  bbox_store(block, x1, y, x2, y);

  /* Update the hint values, which cut down the work of coalesceFlush. */
  if ( coalesce->x1Hint > bi )
    coalesce->x1Hint = bi;
  if ( coalesce->x2Hint < bi )
    coalesce->x2Hint = bi;
}

void bd_coalesceSpan(CompositeContext *context,
                     const Backdrop *backdrop, blit_color_t *sourceColor,
                     dcoord y, dcoord x1, dcoord runLen)
{
  dcoord xi = bd_xPixelIndex(backdrop->shared, x1);
  dcoord bx = bd_xBlockIndex(backdrop->shared, x1);

  HQASSERT(context->source.coalescable,
           "Coalescing span for non-coalescable object") ;

  /* Split the run up into regions to help get more blocks. */
  do {
    dcoord runLenBlock, x2 ;

    INLINE_MIN32(runLenBlock, BLOCK_DEFAULT_WIDTH, runLen + xi) ;
    runLenBlock -= xi ;
    x2 = x1 + runLenBlock - 1;

    bd_coalesceSubSpan(context, backdrop, bx, sourceColor, y, x1, x2);

    ++bx;
    xi = 0; /* span for subsequent blocks must start at zero */
    x1 = x2 + 1;
    runLen -= runLenBlock;
  } while ( runLen > 0 );
}

/* Log stripped */
