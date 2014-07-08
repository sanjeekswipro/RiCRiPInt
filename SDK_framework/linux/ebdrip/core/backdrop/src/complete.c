/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:complete.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop code to 'complete' a block, and this involves:
 * - Compacting the block down now no further span insertion is going
 *   to happen.
 * - Removing duplicate entries, expunging and merging tables.
 * - Colour converting to the next blend space or final device space.
 */

#include "core.h"
#include "backdroppriv.h"
#include "iterator.h"
#include "block.h"
#include "resource.h"
#include "coalesce.h"
#include "composite.h"
#include "hqmemcpy.h"
#include "memutil.h"
#include "often.h"
#include "dlstate.h"
#include "pclGstate.h"

/**
 * Determine if the block is uniform; in this case only need a table
 * with one entry.
 */
static Bool bd_compactUniform(BackdropBlock *block, uint32 nComps)
{
  uint32 width = bd_blockWidth(block), height = bd_blockHeight(block), yi;
  BackdropLine *lines = bd_blockLine(block, 0);
  uint8 *data = bd_blockData(block, 0);

  Bool uniformBlock = bd_isRLE(&lines[0]);
  uint8 uniformIndex = (uint8)(uniformBlock ? data[1] : 0 /* anything */);
  BackdropTable *uniformTable = lines[0].table;

  data += width;
  for ( yi = 1; yi < height && uniformBlock; ++yi ) {
    uniformBlock =
      lines[yi].repeat ||
      (bd_isRLE(&lines[yi]) &&
       bdt_equalEntry(uniformTable, lines[yi].table,
                      nComps, uniformIndex, data[1]));
    data += width;
  }
  if ( uniformBlock) {
    if ( uniformIndex != 0 )
      bdt_copyEntry(uniformTable, uniformTable, nComps, uniformIndex, 0);
    bdt_setNUsedSlots(uniformTable, 1);
    bd_setUniform(block);
    return TRUE;
  }
  return FALSE;
}

/**
 * Determine whether lines takes up less memory as RLE or as a map.  Often
 * adjacent runs have equal table entries and can be merged together.
 */
static uint32 bd_compactMap(BackdropLine *line, uint8 *dataSrc, uint8 *dataDst,
                            uint32 width, uint32 nComps)
{
  BackdropTable *table = line->table;
  uint8 rleBuf[BLOCK_DEFAULT_WIDTH], *rleDst;
  uint32 nBytesToCopy = 0, xi;
  uint32 nRuns, maxRuns = width / 2;

  /* If there is enough space, make RLE in-situ, so don't have to copy it. */
  rleDst = (uint32)(dataSrc - dataDst) >= width ? dataDst : rleBuf;

  for ( xi = 0, nRuns = 0; xi < width; ++nRuns ) {
    /* Use property that run length can easily be derived from table index. */
    uint32 runLen = dataSrc[xi] - xi + 1, newRunLen = runLen;

    /* Check for duplicate adjacent entries and extend the current run. */
    while ( (xi + newRunLen) < width &&
            bdt_equalEntry(table, table, nComps, dataSrc[xi], dataSrc[xi + newRunLen]) ) {
      runLen = newRunLen;
      newRunLen = dataSrc[xi + newRunLen] - xi + 1;
    }
    if ( runLen != newRunLen ) {
      /* Merged adjacent runs and need to update dataSrc in case RLE fails. */
      uint8 newIndex = dataSrc[xi + runLen];
      uint32 i;
      for ( i = 0; i < runLen; ++i ) {
        dataSrc[xi + i] = newIndex;
      }
    }

    if ( nRuns < maxRuns ) {
      /* Can keep converting to RLE. */
      rleDst[nBytesToCopy++] = storeRunLen(newRunLen);
      rleDst[nBytesToCopy++] = dataSrc[xi];
    }

    xi += newRunLen;
  }

  if ( nRuns <= maxRuns ) {
    /* Less memory in RLE form; used rleBuf if dataSrc and dataDst overlap. */
    if ( rleDst != dataDst )
      HqMemCpy(dataDst, rleBuf, nBytesToCopy);
    bd_setRLE(line, nRuns);
  }
  else {
    /* Keep as a map; may need to shuffle down data in memory. */
    nBytesToCopy = width; /* copy whole line */
    if ( dataSrc != dataDst )
      HqMemMove(dataDst, dataSrc, nBytesToCopy); /* copes with overlap */
  }

  return nBytesToCopy;
}

/**
 * For insertable blocks, the data in a backdrop block is of a fixed size with
 * each line using the same quantity of bytes.  After insertion has finished the
 * maps can be compacted down to occupy a smaller amount of memory.  If the
 * whole block is a uniform color then do not need 'data' at all, otherwise on a
 * line by line basis choose either a map or RLE depending on which format is
 * the more compact.
 */
static void bd_compactData(BackdropBlock *block, int32 nComps)
{
  uint32 width = bd_blockWidth(block), height = bd_blockHeight(block);
  BackdropLine *lines = bd_blockLine(block, 0);
  uint8 *dataDst = bd_blockData(block, 0);
  uint32 offset = 0, yi;

  HQASSERT(!bd_isComplete(block), "Backdrop block must be in insert mode");

  /* First look for a uniform block. */
  if ( bd_compactUniform(block, nComps) )
    return; /* finished */

  /* Not a uniform block so compact each line separately, choosing which ever is
     smaller between a repeat, RLE or a map. */
  for ( yi = 0; yi < height; ++yi ) {
    uint8 *dataSrc = bd_blockData(block, yi);
    uint32 nBytesToCopy;

    HQASSERT(dataDst <= dataSrc, "dataDst cannot be greater than dataSrc");

    if ( lines[yi].repeat ) {
      /* This line is the same as the previous line so nothing else needs to be stored. */
      HQASSERT((uint32)readRunLen(dataSrc[0]) == width,
               "single rle span must cover whole line");
      HQASSERT(lines[yi].nRuns == 1, "expected a single RLE run");
      nBytesToCopy = 0;
      lines[yi].table = NULL;
    } else if ( bd_isRLE(&lines[yi]) ) {
      /* Already have a single span of RLE (requiring only 2 bytes). */
      HQASSERT((uint32)readRunLen(dataSrc[0]) == width,
               "single rle span must cover whole line");
      HQASSERT(lines[yi].nRuns == 1, "expected a single RLE run");
      nBytesToCopy = 2;
      if ( dataSrc != dataDst ) {
        dataDst[0] = dataSrc[0];
        dataDst[1] = dataSrc[1];
      }
    } else {
      /* Have a contone map; would it be more compact as RLE? */
      nBytesToCopy = bd_compactMap(&lines[yi], dataSrc, dataDst, width, nComps);
    }

    HQASSERT(lines[yi].offset == (yi * width),
             "offset should be width for insert mode");
    lines[yi].offset = CAST_SIGNED_TO_UINT16(offset);
    dataDst += nBytesToCopy;
    offset += nBytesToCopy;
  }
  HQASSERT(offset == (uint32)(dataDst - bd_blockData(block, 0)),
           "unexpected offset/dataDst value");
  bd_setDataBytes(block, offset);
}

static int16 bd_findDuplicate(BackdropTable *table1, BackdropTable *table2,
                              uint32 nComps, uint8 slot2, int16 *hashTable)
{
  int16 hash = bdt_hashVal(table2, nComps, slot2);

  if ( hashTable[hash] != HASH_UNSET ) {
    if ( !bdt_equalEntry(table1, table2, nComps,
                         CAST_SIGNED_TO_UINT8(hashTable[hash]), slot2) ) {
      /* Got a hash collision; clear out the existing entry to indicate
         new entry needs inserting. */
      hashTable[hash] = HASH_UNSET;
    }
  }

  return hash;
}

/**
 * Remove duplicate entries from a table in the block.  The block has already
 * been compacted.  hashTable is reused for subsequent table merging.
 */
static void bd_expungeTable(BackdropBlock *block, uint32 nComps, uint32 yi,
                            int16 *hashTable)
{
  BackdropLine *line = bd_blockLine(block, yi);
  BackdropTable *table = line->table;
  uint16 nUsedSlots = 0;
  SlotIterator iterator;
  uint8 *slot;

  HQASSERT(yi >= 0 && yi < bd_blockHeight(block), "yi out of range");
  HQASSERT(!bd_isComplete(block), "Backdrop block must be in insert mode");
  HQASSERT(!line->repeat, "Should have already checked for line repeat");
  HQASSERT(table, "backdrop table table is missing on this line");

  /* Initialise the hash table. */
  HqMemSet16((uint16*)hashTable, (uint16)HASH_UNSET, HASH_SIZE);

  bd_slotIteratorInit(block, yi, TRUE, &iterator);

  /* Slot indices are in ascending numerical order.  Just need to remove gaps
     between them and check for duplicates. */
  while ( (slot = bd_slotIterator(&iterator)) != NULL ) {
    int16 hash = bd_findDuplicate(table, table, nComps, slot[0], hashTable);

    if ( hashTable[hash] != HASH_UNSET ) {
      /* Duplicate entry; replace entry with the entry already present. */
      slot[0] = CAST_SIGNED_TO_UINT8(hashTable[hash]);
    } else {
      /* Unless no entries have been expunged so far we'll
         need to copy the entry from iTest to iDest. */
      uint8 iDest = CAST_UNSIGNED_TO_UINT8(nUsedSlots);
      if ( slot[0] != iDest ) {
        HQASSERT(slot[0] > iDest, "src index must be ahead of dest index");
        bdt_copyEntry(table, table, nComps, slot[0], iDest);
        slot[0] = iDest;
      }
      hashTable[hash] = iDest;
      ++nUsedSlots;
    }
  }

  HQASSERT(nUsedSlots >= 1, "Must have at least one entry after table expunging");
  bdt_setNUsedSlots(table, nUsedSlots);
}

/**
 * Merge table2 into table1.  table1 must have been expunged of duplicate
 * entries and the hashTable contains the hash values for table1.
 */
static void bd_mergeTablePair(BackdropBlock *block, uint32 nComps,
                              uint32 yi1, uint32 yi2, int16 *hashTable)
{
  BackdropLine *lines = bd_blockLine(block, 0);
  BackdropTable *table1 = lines[yi1].table, *table2 = lines[yi2].table;
  uint16 nUsedSlots1 = bdt_getNUsedSlots(table1);
  SlotIterator iterator;
  uint8 *slot;

  HQASSERT(yi1 >= 0 && yi1 < bd_blockHeight(block), "yi1 out of range");
  HQASSERT(yi2 >= 0 && yi2 < bd_blockHeight(block), "yi2 out of range");
  HQASSERT(yi1 < yi2, "yi2 is bad");
  HQASSERT(!bd_isComplete(block), "backdrop block must be in insert mode");
  HQASSERT(!lines[yi1].repeat, "yi1 should not be a repeat line");
  HQASSERT(!lines[yi2].repeat, "yi2 should not be a repeat line");

  /* Temporarily set used slots to max to allow new entries to be added. */
  bdt_setNUsedSlots(table1, BACKDROPTABLE_MAXSLOTS);

  bd_slotIteratorInit(block, yi2, TRUE, &iterator);

  /* Iterate over the used slots in table2 and try find matches in table1, or
     failing that merge the entry. */
  while ( (slot = bd_slotIterator(&iterator)) != NULL ) {
    int16 hash = bd_findDuplicate(table1, table2, nComps, slot[0], hashTable);

    if ( hashTable[hash] != HASH_UNSET ) {
      /* A slot from table2 already exists in table1. */
      slot[0] = CAST_SIGNED_TO_UINT8(hashTable[hash]);
    } else {
      /* Should not attempt merge if cannot guarantee success. */
      uint8 iDest = CAST_UNSIGNED_TO_UINT8(nUsedSlots1);
      HQASSERT(nUsedSlots1 < BACKDROPTABLE_MAXSLOTS,
               "Should always have enough room to merge table2 into table1");
      bdt_copyEntry(table2, table1, nComps, slot[0], iDest);
      slot[0] = iDest;
      hashTable[hash] = iDest;
      ++nUsedSlots1;
    }
  }

  /* Update linedata to refer to the merged table and set final used slots count. */
  lines[yi2].table = table1;
  bdt_setNUsedSlots(table1, nUsedSlots1);
}

/**
 * After span insertion has been completed for this block we can merge the
 * backdrop tables together, reducing the number of color conversions required
 * and the total amount of memory required for the color converted output
 * tables.
 */
static void bd_mergeTables(BackdropBlock *block, uint32 nComps)
{
  BackdropLine *lines = bd_blockLine(block, 0);
  int16 hashTable[HASH_SIZE];
  uint32 ys1, ye1, yi2;
  Bool mergedTables;
  uint16 nUsedSlots1 = 0;

  HQASSERT(!bd_isComplete(block), "Backdrop block must be in insert mode");

  /* No table merging to do if we have a uniform block. */
  if ( bd_isUniform(block) )
    return;

  ys1 = ye1 = 0;
  yi2 = 1;

  mergedTables = FALSE;

  for ( ;; ) {
    /* Try to merge table at yi2 into table covering lines ys1 to ye1. */

    /* If the last pair of tables did not merge expunge table1 to maximise
       chance of merging tables for this pair; if the tables did merge then
       the merged table (still table1) does not need to be expunged. */
    if ( !mergedTables ) {
      HQASSERT(ys1 == ye1, "ys1 and ye1 should refer to same line");
      bd_expungeTable(block, nComps, ys1, hashTable);
      nUsedSlots1 = bdt_getNUsedSlots(lines[ys1].table);
    }

    if ( ye1 == (bd_blockHeight(block) - 1) )
      break; /* no more tables */

    if ( lines[yi2].repeat )
      /* Line is a repeat so merge is a no-op. */
      mergedTables = TRUE;
    else if ( bd_isRLE(&lines[yi2]) &&
              (nUsedSlots1 + lines[yi2].nRuns) <= BACKDROPTABLE_MAXSLOTS ) {
      /* Second table can be merged into first, even if no duplicates found. */
      bd_mergeTablePair(block, nComps, ys1, yi2, hashTable);
      nUsedSlots1 = bdt_getNUsedSlots(lines[ys1].table);
      mergedTables = TRUE;
    } else
      /* Second table will only fit if there are enough duplicates; it's
         a pain to back off if it won't fit, so don't bother trying. */
      mergedTables = FALSE;

    /* Move on to next pair of tables. */
    ys1 = (mergedTables ? ys1 : yi2);
    ye1 = yi2;
    ++yi2;
  }
}

/**
 * Color convert inputTable into outputTable.  Color conversion can be either
 * between blend spaces or to the final device space.  Also divides out the
 * alpha from the premultiplied color values, or composites the final backdrop
 * with the page.  Note, for the appropriate conditions outputTable can be the
 * same as inputTable and this saves a substantial amount of copying.
 */
static Bool bd_convertTable(Backdrop *backdrop, BackdropTable *inputTable,
                            BackdropTable *outputTable)
{
  /* This function is a no-op for soft masks from alpha;
     the alpha channel exists already. */
  if ( backdrop->softMaskType == AlphaSoftMask )
    return TRUE;

  /* PCL is opaque and has its own compositing rules and therefore the following
     step does not apply. */
  if ( !backdrop->shared->page->opaqueOnly ) { /* was !pclGstateIsEnabled() */
    if ( backdrop->compositeToPage ) {
      /* Composite the results with the page's erase color. Do not need to
         subsequently divide out the alpha since alpha is implicitly 1.0 for all
         the table entries as a result of compositing with the page (page alpha
         is defined as 1.0). */
      bdt_compositeToPage(inputTable, backdrop->inComps, backdrop->pageColor);
    } else {
      /* Colors are premultiplied and alpha must now be divided out. */
      bdt_divideAlpha(inputTable, backdrop->inComps);
    }
  }

  /* Backdrop may need converting to a different blend space, or the final
     device space. */
  if ( backdrop->converter != NULL ) {
    if ( !bdt_colorConvert(inputTable, backdrop->outTableType,
                           backdrop->outComps, backdrop->converter,
                           outputTable) )
      return FALSE;
  } else if ( !backdrop->isolated ) {
    /* The final alpha channel is the group alpha. */
    HQASSERT(inputTable == outputTable, "Should be reusing table");
    bdt_setAlphaFromGroupAlpha(outputTable);
  }

  if ( backdrop->pageGroupLCA != NULL )
    bdt_updatePageGroupLateColor(outputTable, backdrop->pageGroupLCA);

  if ( backdrop->softMaskTransfer != NULL )
    bdt_applySoftMaskTransfer(outputTable, backdrop->softMaskTransfer);

  return TRUE;
}

/**
 * Color convert inputBlock into outputBlock.  Color conversion can be either
 * between blend spaces or to the final device space.  Also divides out the
 * alpha from the premultiplied color values, or composites the final backdrop
 * with the page.  Note, for the appropriate conditions outputBlock can be the
 * same as inputBlock and this saves a substantial amount of copying.
 */
static Bool bd_convertBlock(Backdrop *backdrop, BackdropBlock *inputBlock,
                            BackdropBlock *outputBlock)
{
  if ( bd_isUniform(inputBlock) ) {
    if ( !bd_convertTable(backdrop, bd_uniformTable(inputBlock),
                          bd_uniformTable(outputBlock)) )
      return FALSE;
  } else {
    BackdropLine *inputLines = bd_blockLine(inputBlock, 0);
    BackdropLine *outputLines = bd_blockLine(outputBlock, 0);
    BackdropTable *prevInputTable = NULL, *prevOutputTable = NULL;
    uint32 height = bd_blockHeight(inputBlock), yi;

    for ( yi = 0; yi < height; ++yi ) {
      if ( !inputLines[yi].repeat && inputLines[yi].table != prevInputTable ) {
        /* This line starts another table. */
        prevInputTable = inputLines[yi].table;
        prevOutputTable = outputLines[yi].table;

        /* Produce a color-converted output table from the input table. */
        if ( !bd_convertTable(backdrop, prevInputTable, prevOutputTable) )
          return FALSE;
      }
      outputLines[yi].table = prevOutputTable;
    }
  }
  return TRUE;
}

/**
 * bd_blockComplete takes an insertable block and produces a compacted and
 * colour-converted block ready for final rendering or compositing into the
 * parent backdrop.  Once a block has been completed no further compositing into
 * the block is possible.  The output block may be allocated from another
 * resource or if it needs to be retained (depending on the output interleaving
 * mode) it will be allocated.  If there is not enough memory to allocate the
 * block a resource is used temporarily until the block is written to disk at
 * the next opportunity.
 */
static Bool bd_blockComplete(CompositeContext *context, Backdrop *backdrop,
                             BackdropBlock *inputBlock, uint32 bb)
{
  BackdropBlock *outputBlock = NULL;

  HQASSERT(!bd_isComplete(inputBlock), "Backdrop block must be in insert mode");
  bd_blockCheck(inputBlock);

  /* Squash block data down to save memory. */
  bd_compactData(inputBlock, backdrop->inComps);

  /* Remove the unused entries from the tables and merge the tables together to
     reduce number of colors to convert and to save space in the final output
     tables. */
  bd_mergeTables(inputBlock, backdrop->inComps);

  /* Now is a useful point to check and print the block or gather stats. */
  bd_blockDebug(context, backdrop, inputBlock);

  /* If need to retain the block for the course of the band or frame then first
     attempt to allocate a 'retained block' to try to keep everything in memory
     (retained block is the precise size to hold the compacted output block). */
  if ( backdrop->compositeToPage &&
       backdrop->shared->retention != BD_RETAINNOTHING ) {
    if ( !bd_retainedBlockNew(backdrop, inputBlock, backdrop->outTableType,
                              &outputBlock) )
      return FALSE;

    /* Replace the input block with the allocated retained block. */
    backdrop->blocks[bb] = outputBlock;
  } else {
    /* If not retaining this backdrop then the inputBlock can be reused,
       which saves a lot of copying. */
    outputBlock = inputBlock;
  }

  /* Color convert the input block to output block. */
  if ( !bd_convertBlock(backdrop, inputBlock, outputBlock) )
    return FALSE;

  /* Finish off new output block. */
  bd_setComplete(outputBlock, TRUE);
  bd_blockCheck(outputBlock);

  return TRUE;
}

/**
 * bd_regionComplete converts an area of the backdrop from the blend space to
 * its parent backdrop's blend space or the final device color space.  In the
 * process the backdrop blocks covering the region are compressed and the tables
 * are merged together.  Once a backdrop block has been completed no further
 * compositing in that block may take place.  Block poaching is an optimisation
 * to save work when compositing one backdrop into another.  If the right
 * conditions are met the child block can be swapped with the equivalent block
 * in the parent.
 */
Bool bd_regionComplete(CompositeContext *context, Backdrop *backdrop,
                       Bool canPoach, const dbbox_t *bounds)
{
  Backdrop *poachBackdrop = canPoach ? backdrop->parentBackdrop : NULL;
  BlockIterator iterator = BLOCKITERATOR_INIT;
  Bool more;

  bd_coalesceFlush(context);

  /* Isolated attributes must match so that tables have same type.  Can't poach
     between softmasks because they have different initialisation rules.  Blocks
     must have identical colorspace and colorants. */
  context->source.canPoach =
    (int8)(
#ifdef DEBUG_BUILD
           (backdropDebug & BD_DISABLE_POACH_BLOCKS) == 0 &&
#endif
           poachBackdrop && !backdrop->compositeToPage &&
           backdrop->isolated == poachBackdrop->isolated &&
           backdrop->softMaskType == EmptySoftMask &&
           poachBackdrop->softMaskType == EmptySoftMask &&
           backdrop->converter == NULL &&
           backdrop->inComps == poachBackdrop->inComps &&
           backdrop->pageGroupLCA == NULL);

  do {
    BackdropBlock *block, *poachBlock;
    uint32 bx, by, bb;

    more = bd_blockIterator(backdrop, bounds, & iterator);
    bd_iteratorPosition(&iterator, &bx, &by);

    bb = bd_blockIndex(backdrop, bx, by);
    block = backdrop->blocks[bb];
    if ( context->source.canPoach && !bd_isTouched(block) )
      continue; /* empty block, will just be skipped on composite */

    poachBlock = (context->source.canPoach
                  ? poachBackdrop->blocks[bd_blockIndex(poachBackdrop, bx, by)]
                  : NULL);

    /* Check if corresponding block in the parent is untouched. */
    if ( !poachBlock || bd_isTouched(poachBlock) )
      if ( !bd_blockComplete(context, backdrop, block, bb) )
        return FALSE;

    SwOftenUnsafe();

  } while ( more );

  return TRUE;
}

/* Log stripped */
