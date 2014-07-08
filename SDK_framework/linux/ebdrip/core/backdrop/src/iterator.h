/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:iterator.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop iterators for iterating over blocks, spans or maps.
 */

#ifndef __ITERATOR_H__
#define __ITERATOR_H__

typedef struct BlockIterator {
  Bool firstTime;
  uint32 bx, by; /* indices for current block */
  uint32 bx1, by1, bx2, by2; /* indices for block bounds */
} BlockIterator;

#define BLOCKITERATOR_INIT {TRUE, 0, 0, 0, 0, 0, 0}

Bool bd_blockIterator(const Backdrop *backdrop, const dbbox_t *bounds,
                      BlockIterator *iterator);
void bd_iteratorPosition(const BlockIterator *iterator,
                         uint32 *bx, uint32 *by);
BackdropBlock **bd_iteratorLocateBlock(const BlockIterator *iterator,
                                       const Backdrop *backdrop);

/**
 * SlotIterator iterates over a line of data picking out the backdrop table
 * indices.  Hides the different data representations from the client code.
 */
typedef struct SlotIterator {
  Bool fastMap;
  uint32 i, num;
  uint32 inc;
  uint8 *data;
} SlotIterator;

void bd_slotIteratorInit(BackdropBlock* block, uint32 yi, Bool compacted,
                         SlotIterator *iterator);
uint8* bd_slotIterator(SlotIterator *iterator);

#endif /* protection for multiple inclusion */

/* Log stripped */
