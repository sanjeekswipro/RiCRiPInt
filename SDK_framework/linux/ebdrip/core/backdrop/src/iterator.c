/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:iterator.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop iterators for iterating over blocks, spans or maps.
 */

#include "core.h"
#include "backdroppriv.h"
#include "iterator.h"
#include "block.h"

Bool bd_blockIterator(const Backdrop *backdrop, const dbbox_t *bounds,
                      BlockIterator *iterator)
{
  HQASSERT(iterator != NULL, "iterator is null");
  HQASSERT(bounds->x1 != MAXDCOORD || bounds->y1 != MAXDCOORD ||
           bounds->x2 != MINDCOORD || bounds->y2 != MINDCOORD,
           "bounds is not set");
  if ( iterator->firstTime ) {
    HQASSERT(bounds != NULL, "bounds is null");
    HQASSERT(!bbox_is_empty(bounds),
             "bounds is not a valud bbox");

    iterator->firstTime = FALSE;

    iterator->bx1 = bd_xBlockIndex(backdrop->shared, bounds->x1);
    iterator->by1 = bd_yBlockIndex(backdrop->shared, bounds->y1);
    HQASSERT(iterator->bx1 <= backdrop->shared->xblock, "bx1 out of range");
    HQASSERT(iterator->by1 <= backdrop->shared->pageBackdrops->yblock,
             "by1 out of range");

    iterator->bx2 = bd_xBlockIndex(backdrop->shared, bounds->x2);
    iterator->by2 = bd_yBlockIndex(backdrop->shared, bounds->y2);
    HQASSERT(iterator->bx2 <= backdrop->shared->xblock, "bx2 out of range");
    HQASSERT(iterator->by2 <= backdrop->shared->pageBackdrops->yblock,
             "by2 out of range");

    HQASSERT(iterator->bx1 <= iterator->bx2, "no blocks to iterate over");
    HQASSERT(iterator->by1 <= iterator->by2, "no blocks to iterate over");

    iterator->bx = iterator->bx1;
    iterator->by = iterator->by1;
  } else {
    ++(iterator->bx);
    if ( iterator->bx > iterator->bx2 ) {
      /* go to next row */
      iterator->bx = iterator->bx1;
      ++(iterator->by);
    }
  }
  HQASSERT(iterator->bx >= iterator->bx1 && iterator->bx <= iterator->bx2,
           "bx out of range");
  HQASSERT(iterator->by >= iterator->by1 && iterator->by <= iterator->by2,
           "by out of range");

  /* any more blocks to follow? */
  return (iterator->bx < iterator->bx2 || iterator->by < iterator->by2);
}

void bd_iteratorPosition(const BlockIterator *iterator,
                         uint32 *bx, uint32 *by)
{
  *bx = iterator->bx;
  *by = iterator->by;
}

#ifndef S_SPLINT_S /* Doesn't parse for some unknown reason */

BackdropBlock **bd_iteratorLocateBlock(const BlockIterator *iterator,
                                       const Backdrop *backdrop)
{
  uint32 bb = bd_blockIndex((Backdrop*)backdrop,
                           iterator->bx, iterator->by);
  return &backdrop->blocks[bb];
}

#endif

/* SlotIterator iterates over a line of data picking out the backdrop table
   indices.  Hides the different data representations from the client code. */
void bd_slotIteratorInit(BackdropBlock *block, uint32 yi, Bool compacted,
                         SlotIterator *iterator)
{
  BackdropLine *line = bd_blockLine(block, yi);

  iterator->i = 0;
  if ( bd_isRLE(line) ) {
    /* RLE data for either insert or compacted blocks. */
    iterator->fastMap = FALSE;
    iterator->num = line->nRuns;
    iterator->inc = 2;
    iterator->data = bd_blockData(block, yi) + 1;
  } else if ( compacted ) {
    /* Block compacted and not RLE (too few consecutive indices). */
    iterator->fastMap = FALSE;
    iterator->num = bd_blockWidth(block);
    iterator->inc = 1;
    iterator->data = bd_blockData(block, yi);
  } else {
    /* Insert mode block and not RLE, but table indices indicate run length anyway. */
    int32 width = bd_blockWidth(block);
    iterator->fastMap = TRUE;
    iterator->num = width;
    iterator->data = bd_blockData(block, yi);
    /* inc tracks the index in the previous 'run' (if data[0] == 0, inc is -1). */
    iterator->inc = iterator->data[width - 1] - width;
  }
}

uint8* bd_slotIterator(SlotIterator *iterator)
{
  if ( iterator->i < iterator->num ) {
    uint8 *data = iterator->data;

    if ( iterator->fastMap ) {
      int32 runLen = data[0] - iterator->inc;
      iterator->data += runLen;
      iterator->inc = data[0];
      iterator->i += runLen;
    } else {
      iterator->data += iterator->inc;
      ++iterator->i;
    }
    return data;
  } else {
    return NULL;
  }
}

/* Log stripped */
