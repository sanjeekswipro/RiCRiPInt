/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:block.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#include "core.h"
#include "backdroppriv.h"
#include "block.h"
#include "table.h"
#include "iterator.h"
#include "resource.h"
#include "composite.h"
#include "imfile.h"
#include "params.h"
#include "swerrors.h"
#include "display.h"
#include "hqmemcpy.h"
#include "monitor.h"

enum {
  STORAGE_MEMORY,
  STORAGE_DISK,
  STORAGE_UNIFORM
};

enum {
  /** No further writes to this block.  Indicates whether in insert or compact
      mode. */
  BLOCKFLAG_COMPLETE = 1,

  /** Indicates that a backdrop block has changed since being initialised.
      (Note, non-isolated groups are initialised from the parents immediate
      backdrop and are therefore deemed touched from the beginning). */
  BLOCKFLAG_TOUCHED = 2,

  /** Indicates the block can purged to free the data, lines and table memory.
      The data will need to be written to disk and will then need to temporarily
      use a resource for the subsequent block read.  The flag is only set if the
      block needs to be retained across bands or frames, and writing to disk is
      allowed. */
  BLOCKFLAG_PURGEABLE = 4
};

struct BackdropBlock {
  BackdropBlock    *prev, *next;    /**< Chain of purgeable blocks (a subset of blocks). */

  BackdropResource *resource;       /**< Memory for data, lines and tables owned by
                                         resource if present. */
  uint8             storage;        /**< STORAGE_ */
  uint8             flags;          /**< BLOCKFLAG_ */
  uint16            width;          /**< Width of this block in bytes or pixels. */
  uint16            height;         /**< Height of this block in bytes or pixels. */
  uint32            nComps;         /**< Number of components in each table. */

  uint16            dataBytes;      /**< Bytes allocated to data. */
  uint16            linesBytes;     /**< Bytes allocated to lines. */

  uint8            *data;           /**< RLE or map indices into table (varies per line). */
  BackdropLine     *lines;          /**< One per line to indicate RLE or map, ptr to table. */

  IM_FILES         *file;           /**< Pointer into the file list (for STORAGE_DISK). */
  int32             foffset;        /**< If >= 0 gives the disk offset. */
};

size_t bd_blockSize(void)
{
  return sizeof(BackdropBlock);
}

static Bool bd_blockTablesAlloc(const BackdropShared *shared,
                                BackdropBlock *inBlock, BackdropBlock *outBlock,
                                int32 tableType, mm_cost_t cost)
{
  BackdropTable *prevInputTable = NULL, *prevOutputTable = NULL;
  uint32 yi;

  HQASSERT(inBlock->resource != NULL, "Input block should have a resource");
  HQASSERT(outBlock->resource == NULL, "Output block should not have a resource");
  HQASSERT(shared->retention != BD_RETAINNOTHING,
           "retain band or page must be true");
  HQASSERT(inBlock->storage != STORAGE_UNIFORM,
           "Uniform block encountered, no lines");

  for ( yi = 0; yi < inBlock->height; ++yi ) {
    BackdropLine *inLine = &inBlock->lines[yi];
    BackdropLine *outLine = &outBlock->lines[yi];

    if ( !inLine->repeat ) {
      HQASSERT(inLine->table, "missing input backdrop table");

      if ( inLine->table != prevInputTable ) {
        /* This line starts another table. */
        uint16 nUsedSlots;
        size_t nBytes;

        prevInputTable = inLine->table;
        nUsedSlots = bdt_getNUsedSlots(prevInputTable);

        nBytes = bdt_size(tableType, outBlock->nComps, nUsedSlots);

        prevOutputTable = bd_dataAlloc(shared, nBytes, cost);
        if ( prevOutputTable == NULL )
          return FALSE; /* Caller decides whether to give a vmerror. */

        /* Must initialise the table to ensure bd_blockTablesFree
           will work correctly (tables are individually sized). */
        bdt_init(tableType, outBlock->nComps, nUsedSlots, prevOutputTable);
      }

      outLine->table = prevOutputTable;
    }
  }

  return TRUE;
}

static void bd_blockTablesFree(const BackdropShared *shared,
                               BackdropBlock *block)
{
  BackdropTable *prevTable = NULL;
  uint32 yi;

  HQASSERT(block != NULL, "block is null");
  HQASSERT(block->resource == NULL, "cannot free blocks with a resource");
  HQASSERT(block->lines != NULL, "block lines is null");
  HQASSERT(shared->retention != BD_RETAINNOTHING,
           "retain band or page must be true");

  for ( yi = 0; yi < block->height; ++yi ) {
    BackdropLine *line = &block->lines[yi];

    if ( prevTable != line->table && line->table ) {
      size_t nBytes;
      prevTable = line->table;
      nBytes = bdt_getNBytesAlloc(prevTable, block->nComps);
      bd_dataFree(shared, prevTable, nBytes);
    }
    line->table = NULL;
  }
}

static size_t bd_blockTablesSize(BackdropBlock *block)
{
  size_t tableBytes = 0;
  BackdropTable *prevTable = NULL;
  uint32 yi;

  for ( yi = 0; yi < block->height; ++yi ) {
    BackdropLine *line = &block->lines[yi];

    if ( !line->repeat && prevTable != line->table ) {
      prevTable = line->table;
      tableBytes += bdt_getNBytesUsed(prevTable, block->nComps);
    }
  }
  return tableBytes;
}

static Bool bd_blockTablesToDisk(BackdropBlock *block, IM_FILES *file)
{
  BackdropTable *prevTable = NULL;
  uint32 yi;

  for ( yi = 0; yi < block->height; ++yi ) {
    BackdropLine *line = &block->lines[yi];

    if ( !line->repeat && prevTable != line->table ) {
      prevTable = line->table;
      if ( !bdt_tableToDisk(prevTable, block->nComps, file) )
        return FALSE;
    }
  }
  return TRUE;
}

/**
 * Reads the set of backdrop tables in a block back from disk after being
 * purged during a low memory situation. The tables written into a backdrop
 * resource which is sufficiently large to hold the retained tables.
 */
static Bool bd_blockTablesFromDisk(BackdropBlock *block)
{
  BackdropResource *resource;
  BackdropTable **tables;
  BackdropTable *prevTable = NULL, *currTable = NULL;
  uint32 yi, itable = 0;

  HQASSERT(block != NULL, "block is null");
  HQASSERT(bd_isComplete(block), "block must be completed");
  HQASSERT(block->resource != NULL, "block  resource is null");
  HQASSERT(block->storage == STORAGE_DISK,
           "Unexpected block storage type");

  resource = block->resource;
  tables = bd_resourceTables(resource);

  for ( yi = 0; yi < block->height; ++yi ) {
    BackdropLine *line = &block->lines[yi];

    if ( !line->repeat ) {
      /* The ptrs to the backdrop tables in BackdropLine are no longer valid,
         but can still be used to indicate the positioning of new tables. */
      if ( line->table != prevTable ) {
        /* This line starts another table. */
        prevTable = line->table;
        currTable = tables[itable++];
        if ( !bdt_tableFromDisk(currTable, block->nComps, block->file) )
          return FALSE;
      }
      line->table = currTable;
    }
  }

  return TRUE;
}

static void bd_blockFree(const BackdropShared *shared,
                         BackdropBlock **freeBlock, Bool freeBlockStruct)
{
  BackdropBlock *block = *freeBlock;

  if ( block == NULL )
    return;

  /* Take block off the purgeable list, it's about to be freed. */
  bd_setPurgeable(shared, block, FALSE, NULL);

  if ( block->resource != NULL ) {
    /* Block is using a resource so don't free block data and lines - just
       release the resource.  The block struct may or may not come from the
       resource as well. */
    if ( freeBlockStruct ) {
      if ( block != bd_resourceBlock(block->resource) )
        bd_stateFree(shared, block, bd_blockSize());
      *freeBlock = NULL;
    } else {
      block->resource = NULL;
      block->data = NULL;
      block->lines = NULL;
    }
  } else {
    if ( block->data != NULL ) {
      bd_dataFree(shared, block->data, block->dataBytes);
      block->data = NULL;
    }
    if ( block->lines != NULL ) {
      if ( block->storage == STORAGE_UNIFORM ) {
        /* A uniform block requires one table containing a single entry.  Note,
           uses state memory since uniform blocks are never purged. */
        bd_stateFree(shared, block->lines, block->linesBytes);
      } else {
        bd_blockTablesFree(shared, block);
        bd_dataFree(shared, block->lines, block->linesBytes);
      }
      block->lines = NULL;
    }
    if ( freeBlockStruct ) {
      bd_stateFree(shared, block, bd_blockSize());
      *freeBlock = NULL;
    }
  }
}

static void bd_copyToNonIsolated(BackdropTable *tableFrom, uint32 nCompsFrom,
                                 BackdropTable *tableTo, uint32 nCompsTo,
                                 BackdropBlock *blockFrom, uint32 yi,
                                 Bool trackShape)
{
  uint16 nUsedSlots = bdt_getNUsedSlots(tableFrom);

  bdt_init(trackShape ? BDT_NONISOLATED_SHAPE : BDT_NONISOLATED,
           nCompsTo, BACKDROPTABLE_MAXSLOTS, tableTo);
  bdt_setNUsedSlots(tableTo, nUsedSlots);

  if ( !blockFrom->lines[yi].repeat ) {
    SlotIterator iterator;
    uint8 *slot;

    bd_slotIteratorInit(blockFrom, yi, FALSE, &iterator);

    while ( (slot = bd_slotIterator(&iterator)) != NULL ) {
      bdt_copyToNonIsolated(tableFrom, nCompsFrom, tableTo, nCompsTo, slot[0]);
    }
  }
}

static void bd_setLine(BackdropLine *lines, uint32 yi, uint32 offset,
                       uint32 nRunsInBlock)
{
  lines[yi].offset = CAST_UNSIGNED_TO_UINT16(offset);
  lines[yi].nRuns  = CAST_UNSIGNED_TO_UINT8(nRunsInBlock);
  lines[yi].repeat = FALSE;
}

static void bd_copyLine(BackdropLine *dest, BackdropLine *src,
                        uint32 yi, uint32 offset)
{
  dest[yi].nRuns = src[yi].nRuns;
  dest[yi].offset = CAST_UNSIGNED_TO_UINT16(offset);
  dest[yi].repeat = src[yi].repeat;
}

/**
 * Calculate the dimensions of the given block.  This is complicated by the last
 * column and row potentially being smaller, and also there may be multiple rows
 * of blocks per region and the last row of blocks in each region may
 * potentially be a different height.
 */
static void bd_setBlockDims(const Backdrop *backdrop, uint32 bx, uint32 by,
                            BackdropBlock *block)
{
  BackdropShared *shared = backdrop->shared;

  /* Last column of blocks may be a different width.
     Width must be at least two, as two bytes are required for single span of
     RLE.  This happens when the last column is one pixel wide.  It's worth
     allowing RLE in this case to simplify initialising, compositing, and
     compacting the block. */
  if ( bx == shared->xblock - 1 ) {
    uint32 xi = bd_xPixelIndex(shared, shared->width - 1);
    block->width = xi == 0 ? 2 : CAST_UNSIGNED_TO_UINT16(xi + 1);
  }
  else { /* Standard width. */
    HQASSERT(shared->blockWidth >= 2, "blockWidth must be at least two");
    block->width = CAST_UNSIGNED_TO_UINT16(shared->blockWidth);
  }

  /* Very last row of blocks may be a different height.
     Don't use backdrop->yblock for this test since the BANDED_COMPOSITING
     optimisation means that backdrop->yblock may be less. All page backdrops
     have the same yblock, so just pick the first. */
  if ( by == shared->pageBackdrops->yblock - 1 ) {
    uint32 yi = bd_yPixelIndex(shared, shared->height - 1);
    block->height = CAST_UNSIGNED_TO_UINT16(yi + 1);
  }
  /* Last row of blocks in each region may be a different height too. */
  else if ( ((by + 1) % shared->blockRowsPerRegion) == 0 ) {
    uint32 yi = bd_yPixelIndex(shared, shared->regionHeight - 1);
    block->height = CAST_UNSIGNED_TO_UINT16(yi + 1);
  }
  else { /* Standard height. */
    block->height = CAST_UNSIGNED_TO_UINT16(shared->blockHeight);
  }
}

/**
 * Make a new insertable block from a resource.
 */
static void bd_blockNew(const Backdrop *backdrop,
                        const CompositeContext *context,
                        uint32 bx, uint32 by,
                        BackdropBlock **refBlock, BackdropTable ***tables)
{
  uint32 bb = bd_blockIndex(backdrop, bx, by);
  BackdropResource *resource;
  BackdropBlock *block, init = {0};

  resource = bd_resourceGet(backdrop, context, bx, by);
  block = bd_resourceBlock(resource);

  *block = init;
  block->resource = resource;
  block->storage = STORAGE_MEMORY;
  bd_setBlockDims(backdrop, bx, by, block);
  block->nComps = backdrop->inComps;
  block->data = bd_resourceData(resource, &block->dataBytes);
  block->lines = bd_resourceLines(resource, &block->linesBytes);
  block->foffset = -1;

  HQASSERT(backdrop->blocks[bb] == NULL, "A block already exists");
  backdrop->blocks[bb] = block;

  *refBlock = block;
  *tables = bd_resourceTables(resource);
}

void bd_blockInit(const Backdrop *backdrop, const CompositeContext *context,
                  uint32 bx, uint32 by)
{
  BackdropBlock *block;
  BackdropLine *lines;
  BackdropTable **tables;
  uint32 yi, itable = 0, offset = 0;

  /* Make a new block from a resource, and insert it into the backdrop. */
  bd_blockNew(backdrop, context, bx, by, &block, &tables);

  lines = block->lines;
  HQASSERT(lines, "lines data is null");

  if ( backdrop->isolated ) {
    uint8 currIndex = CAST_SIGNED_TO_UINT8(block->width - 1);
    uint8 *data = block->data;

    for ( yi = 0; yi < block->height; ++yi ) {
      lines[yi].table = tables[itable++];

      bdt_init(backdrop->trackShape ? BDT_ISOLATED_SHAPE : BDT_ISOLATED,
               backdrop->inComps, BACKDROPTABLE_MAXSLOTS, lines[yi].table);

      data[0] = storeRunLen(block->width);
      data[1] = currIndex;
      bd_setLine(lines, yi, offset, 1);
      data += block->width;
      if ( yi == 0 || !backdrop->shared->allowLineRepeats ) {
        bdt_initEntry(lines[yi].table, backdrop->inComps, currIndex,
                      backdrop->initial.color, backdrop->initial.alpha,
                      backdrop->initial.groupAlpha, &backdrop->initial.info);
      } else {
        lines[yi].repeat = TRUE;
      }

      offset += block->width;
    }

    /* For isolated backdrop, indicate block has not yet been touched by any
       spans, unless the backdrop is for a softmask from luminosity when it is
       initialised to an opaque color. */
    bd_setTouched(block, backdrop->softMaskType == LuminositySoftMask);

  } else {
    /* Non-isolated */
    Backdrop *initialBackdrop = backdrop->initial.backdrop;
    BackdropBlock *initialBlock =
      initialBackdrop->blocks[bd_blockIndex(initialBackdrop, bx, by)];
    BackdropLine *initialLines = initialBlock->lines;

    HQASSERT(!bd_isComplete(initialBlock), "Parent block must not be complete");
    HQASSERT(initialBlock->width == block->width && initialBlock->height == block->height,
             "Block dimensions must match");

    for ( yi = 0; yi < block->height; ++yi ) {
      lines[yi].table = tables[itable++];
      bd_copyToNonIsolated(initialLines[yi].table, initialBackdrop->inComps,
                           lines[yi].table, backdrop->inComps, initialBlock,
                           yi, backdrop->trackShape);
      bd_copyLine(lines, initialLines, yi, offset);

      offset += block->width;
    }

    HqMemCpy(block->data, initialBlock->data, block->dataBytes);
    bd_setTouched(block, bd_isTouched(initialBlock));
  }
}

void bd_blockSwap(const Backdrop *backdrop1, const Backdrop *backdrop2,
                  uint32 bx, uint32 by)
{
  uint32 bb1 = bd_blockIndex(backdrop1, bx, by);
  uint32 bb2 = bd_blockIndex(backdrop2, bx, by);

  BackdropBlock *block1 = backdrop1->blocks[bb1];
  BackdropBlock *block2 = backdrop2->blocks[bb2];

  BackdropResource *resource1 = block1->resource;
  BackdropResource *resource2 = block2->resource;

  HQASSERT(block1->resource != NULL && block1->resource != NULL,
           "Blocks still compositing must have a resource");

  block1->resource = resource2;
  block2->resource = resource1;
  bd_resourceSwap(resource1, resource2);

  backdrop1->blocks[bb1] = block2;
  backdrop2->blocks[bb2] = block1;
}

static Bool bd_copyLineAndData(BackdropShared *shared,
                               BackdropBlock *src, BackdropBlock *dest,
                               int32 tableType, mm_cost_t cost)
{
  uint32 yi;

  dest->dataBytes = src->dataBytes;

  dest->data = bd_dataAlloc(shared, dest->dataBytes, cost);
  if ( dest->data == NULL )
    return FALSE;
  HqMemCpy(dest->data, src->data, dest->dataBytes);

  dest->linesBytes =
    CAST_SIGNED_TO_UINT16(sizeof(BackdropLine) * dest->height);
  dest->lines = bd_dataAlloc(shared, dest->linesBytes, cost);
  if ( dest->lines == NULL ) {
    bd_blockFree(shared, &dest, FALSE);
    return FALSE;
  }

  HqMemCpy(dest->lines, src->lines, dest->linesBytes);
  for ( yi = 0; yi < dest->height; ++yi ) {
    dest->lines[yi].table = NULL;
  }

  if ( !bd_blockTablesAlloc(shared, src, dest, tableType, cost) ) {
    bd_blockFree(shared, &dest, FALSE);
    return FALSE;
  }

  return TRUE;
}

static inline Bool bd_isPurgeable(BackdropBlock *block)
{
  return ((block->flags & BLOCKFLAG_PURGEABLE) != 0);
}

Bool bd_retainedBlockNew(Backdrop *backdrop, BackdropBlock *block,
                         int32 tableType, BackdropBlock **pblock)
{
  BackdropBlock *oblock = NULL;

  HQASSERT(!backdrop->trackShape, "Never need to track shape in page backdrop");

  oblock = bd_stateAlloc(backdrop->shared, bd_blockSize());
  if ( oblock == NULL )
    return error_handler(VMERROR);

  HqMemCpy(oblock, block, bd_blockSize());
  HQASSERT(!bd_isPurgeable(oblock), "Purgeable set");
  oblock->nComps = backdrop->outComps;
  oblock->resource = NULL;
  oblock->data = NULL;
  oblock->dataBytes = 0;
  oblock->lines = NULL;
  oblock->linesBytes = 0;

  if ( block->storage == STORAGE_UNIFORM ) {
    /* A uniform block requires one table containing a single entry. */
    BackdropLine *lines;
    size_t linesBytes;

    linesBytes = bdt_size(tableType, backdrop->outComps, 1);
    linesBytes += 4; /* mm_pool 4 byte aligned, want 8 byte alignment. */

    /* Uniform blocks are not purged to disk so cannot use a resource and this
       alloc must succeed (allow full low memory handling).  Note, uses the
       state memory since uniform blocks are never purged. */
    lines = bd_stateAlloc(backdrop->shared, linesBytes);
    if ( lines == NULL ) {
      bd_blockFree(backdrop->shared, &oblock, TRUE);
      return error_handler(VMERROR);
    }
    oblock->lines = lines;
    oblock->linesBytes = CAST_SIGNED_TO_UINT16(linesBytes);
    bd_setUniform(oblock);
  } else {
    /* Non-uniform block. */

    /* Try allocating a minimally sized block to retain the data in memory
       across bands or frames.  Any extra backdrop resources may be freed, but
       there's no point purging other backdrop blocks. */
    if ( !bd_copyLineAndData(backdrop->shared, block, oblock, tableType,
                             purgeCost) ) {
      /* Failing to create a non-uniform retained block is not an error, and can
         instead reuse the input block's resource temporarily and then write the
         output block to disk. */
      oblock->resource = block->resource;
      oblock->data = bd_resourceData(block->resource, NULL);
      oblock->dataBytes = block->dataBytes;
      oblock->lines = bd_resourceLines(block->resource, NULL);
      oblock->linesBytes = block->linesBytes;
    }
  }
  *pblock = oblock;
  return TRUE;
}

size_t bd_blockPurgeSize(BackdropBlock *block)
{
  return block->dataBytes + block->linesBytes + bd_blockTablesSize(block);
}

static Bool bd_blockToDisk(const BackdropShared *shared, BackdropBlock *block)
{
  int16 falign = CAST_SIGNED_TO_INT16(BLOCK_MIN_ALIGN);
  size_t totalBytes = bd_blockPurgeSize(block);
  IM_FILES *file = NULL;
  int32 foffset = 0;
  Bool ok;

  HQASSERT(bd_isComplete(block), "block must be complete");
  HQASSERT(block->storage == STORAGE_MEMORY, "Unexpected block storage");
  HQASSERT(block->data != NULL, "Block data missing");
  HQASSERT(block->file == NULL && block->foffset == -1,
           "Already written block to disk");

  multi_mutex_lock(&backdropLock);

  ok = ( im_fileoffset(shared->imfileCtxt, falign, (int32)totalBytes,
                       &file, &foffset) &&
         im_fileseek(file, foffset) &&
         im_filewrite(file, block->data, block->dataBytes) &&
         im_filewrite(file, (uint8*)block->lines, block->linesBytes) &&
         bd_blockTablesToDisk(block, file) );

  if ( ok ) {
    /* Cast away constness and update shared safely. */
    BackdropShared *sharedSafe = (BackdropShared*)shared;

    /* Only store this data when the write has succeeded. */
    block->file = file;
    block->foffset = foffset;
    block->storage = STORAGE_DISK;

#if defined(DEBUG_BUILD)
    if ( sharedSafe->nBlocksToDisk == 0 )
      monitorf((uint8*)"Starting to purge backdrop\n");
#endif

    /* nBlocksToDisk is only changed here (besides initialisation) and it is
       used to decide whether to try allocating a retained block (it's value
       isn't critical). */
    ++sharedSafe->nBlocksToDisk;
#if defined( DEBUG_BUILD ) || defined( METRICS_BUILD )
    sharedSafe->nBytesToDisk += totalBytes;
#endif
  }

  multi_mutex_unlock(&backdropLock);
  return ok;
}

static Bool bd_blockFromDisk(BackdropBlock *block)
{
  Bool ok;

  HQASSERT(block->storage == STORAGE_DISK, "Unexpected block storage type");
  HQASSERT(block->resource != NULL, "Expected to have a backdrop resource");
  HQASSERT(block->data && block->lines, "data and lines should be present");

  multi_mutex_lock(&backdropLock);

  ok = ( im_fileseek(block->file, block->foffset) &&
         im_fileread(block->file, block->data, block->dataBytes) &&
         im_fileread(block->file, (uint8*)block->lines, block->linesBytes) &&
         bd_blockTablesFromDisk(block) );

  multi_mutex_unlock(&backdropLock);

  if ( ok )
    bd_blockCheck(block);
  return ok;
}

/**
 * If the block is on disk find a resource and read the block back in.  If a
 * resource exists already it means the block has been read in and it doesn't
 * need to be done again.
 */
Bool bd_blockLoad(const Backdrop *backdrop, const CompositeContext *context,
                  uint32 bx, uint32 by, BackdropBlock *block)
{
  HQASSERT(block->storage == STORAGE_DISK, "Don't need to load block");
  HQASSERT(bd_isComplete(block), "Expected completed block");

  /* If the block has a resource then it has already been loaded
     (block->storage is left set at STORAGE_DISK). */
  if ( block->resource == NULL ) {
    BackdropResource *resource;

    /* Block already exists, just need to assign data and lines.  dataBytes and
       linesBytes are already set to the compacted size and should be left
       alone. */
    resource = bd_resourceGet(backdrop, context, bx, by);
    block->resource = resource;
    block->data = bd_resourceData(resource, NULL);
    block->lines = bd_resourceLines(resource, NULL);

    if ( !bd_blockFromDisk(block) )
      return FALSE;
  }
  return TRUE;
}

/**
 * bd_blockReader is called repeatedly after bd_readerNext to read the contents
 * of a backdrop block.  It returns a bounded area with color and info, or null
 * indicating the block has been read.
 */
const dbbox_t *bd_blockReader(CompositeContext *context,
                              const Backdrop *backdrop,
                              uint8 **color8, COLORVALUE **color,
                              COLORINFO **info)
{
  BackdropReader *reader = &context->reader;
  dbbox_t *bounds = &reader->bounds;
  uint8 slot;

  HQASSERT(reader->block != NULL, "Must have a block to read");
  HQASSERT(!bd_isPurgeable(reader->block) && reader->block->lines != NULL &&
           (bd_isUniform(reader->block) || reader->block->data != NULL),
           "Block is not suitable for reading");

  if ( reader->blockFinish ) {
    return NULL;
  } else if ( reader->blockStart ) {
    reader->blockStart = FALSE;

    /* Calculate the read bounds for this block. */
    bbox_store(&reader->readBoundsForBlock, reader->xTopLeft, reader->yTopLeft,
               reader->xTopLeft + bd_blockWidth(reader->block) - 1,
               reader->yTopLeft + bd_blockHeight(reader->block) - 1);
    bbox_intersection(&reader->readBoundsForBlock, &reader->readBounds,
                      &reader->readBoundsForBlock);
    HQASSERT(!bbox_is_empty(&reader->readBoundsForBlock) &&
             bbox_contains(&reader->readBounds, &reader->readBoundsForBlock),
             "readBoundsForBlock is invalid");

    /* Check for a uniform block. */
    if ( bd_isUniform(reader->block) ) {
      bdt_getOutput(bd_uniformTable(reader->block), backdrop->outComps, 0,
                    color, color8, info);
      reader->blockFinish = TRUE;
      return &reader->readBoundsForBlock;
    }

    /* Initialise bounds for the first read from this block (x1/x2 are set to
       xTopLeft rather than readBoundsForBlock.x1 to handle the RLE case below). */
    bounds->x1 = bounds->x2 = reader->xTopLeft;
    bounds->y1 = bounds->y2 = reader->readBoundsForBlock.y1;
  } else {
    /* Already reading from the block so just advance to the next pixel. */
    bounds->x1 = bounds->x2 = bounds->x2 + 1;

    if ( bounds->x1 > reader->readBoundsForBlock.x2 ) {
      /* Reached end of line; go to the next. */
      bounds->x1 = bounds->x2 = reader->xTopLeft;
      bounds->y1 = bounds->y2 = bounds->y2 + 1;

      if ( bounds->y1 > reader->readBoundsForBlock.y2 ) {
        /* No more lines in this block; get the next block. */
        reader->blockFinish = TRUE;
        return NULL;
      }
    }
  }

  /* Check for a start of a line. */
  if ( bounds->x1 == reader->xTopLeft ) {
    int32 xSkip = reader->readBoundsForBlock.x1 - reader->xTopLeft;

    bd_lineAndDataRepeatSrc(reader->block, bounds->y1 - reader->yTopLeft,
                            &reader->line, &reader->data);

    /* Skip pixels before readBoundsForBlock.x1. */
    if ( xSkip > 0 ) {
      if ( bd_isRLE(reader->line) ) {
        while ( (bounds->x1 + readRunLen(reader->data[0]) - 1)
                < reader->readBoundsForBlock.x1 ) {
          bounds->x1 += readRunLen(reader->data[0]);
          reader->data += 2;
        }
      } else {
        bounds->x1 += xSkip;
        reader->data += xSkip;
      }
      bounds->x2 = bounds->x1;
    }

    /* Count repeated lines. */
    for ( ; bounds->y2 < reader->readBoundsForBlock.y2 &&
            reader->block->lines[bounds->y2 + 1 - reader->yTopLeft].repeat;
          ++bounds->y2 ) {
      EMPTY_STATEMENT();
    }
  }

  HQASSERT(bounds->x1 == bounds->x2, "x1 and x2 should be the same");
  if ( bd_isRLE(reader->line) ) {
    /* Already got RLE; just read the next run. */
    slot = reader->data[1];
    bounds->x2 += readRunLen(reader->data[0]) - 1;
    bbox_clip_x(&reader->readBoundsForBlock, bounds->x1, bounds->x2);
    reader->data += 2;
  } else {
    /* Got a map, but check for runs. */
    slot = reader->data[0];
    ++reader->data;
    while ( bounds->x2 < reader->readBoundsForBlock.x2 &&
            reader->data[0] == slot ) {
      ++bounds->x2;
      ++reader->data;
    }
  }

  HQASSERT(!bbox_is_empty(bounds) &&
           bbox_contains(&reader->readBoundsForBlock, bounds), "Invalid bounds");
  bdt_getOutput(reader->line->table, backdrop->outComps, slot,
                color, color8, info);
  return bounds;
}

Bool bd_blockPurge(const BackdropShared *shared, BackdropBlock **blockRef)
{
  BackdropBlock *purgeBlock = *blockRef;

  HQASSERT(bd_isPurgeable(purgeBlock) && bd_isComplete(purgeBlock) &&
           bd_isStorageMemory(purgeBlock) && bd_blockResource(purgeBlock) == NULL,
           "Block shouldn't be on the purgeable list");

  if ( !bd_blockToDisk(shared, purgeBlock) )
    return FALSE;

  bd_blockFree(shared, blockRef, FALSE);
  HQASSERT(!bd_isPurgeable(purgeBlock), "Block shouldn't be purgeable now");

  return TRUE;
}

/** If a block is using a resource then release it for the next region set.
    Blocks in the page backdrop are retained across bands and frames as
    necessary (in memory or on disk).  For example, a block can be reclaimed at
    the end of the region set (to release the resource) and then again at the
    end of the band (freed entirely, for band-interleaved output). */
Bool bd_blockReclaim(const Backdrop *backdrop,
                     BackdropBlock **pblock, Bool bandclose, Bool canKeep)
{
  BackdropShared *shared = backdrop->shared;
  BackdropBlock *block = *pblock;

  HQASSERT(!canKeep || bd_isComplete(block) || !bd_isTouched(block),
           "Should not be trying to reclaim an uncompleted block");

  if ( canKeep &&
       backdrop->compositeToPage &&
       (shared->retention == BD_RETAINPAGE ||
        (shared->retention == BD_RETAINBAND && !bandclose)) ) {
    /* Ensure another thread won't purge this block whilst we're deciding how to
       handle it here (this can happen if the block was marked purgeable in a
       prior reclaim). */
    bd_setPurgeable(shared, block, FALSE, NULL);

    if ( block->resource != NULL ) {
      /* Need to retain this block, but release the resource. */
      HQASSERT(block->storage != STORAGE_UNIFORM,
               "Do not need a resource for a uniform block");
      if ( block->storage == STORAGE_MEMORY ) {
        /* In a low memory situation and used a resource to store the final
           output block.  This block needs to be written straight to disk
           to allow the resource to be made available for subsequent regions. */
        if ( !bd_blockToDisk(shared, block) )
          return error_handler(IOERROR);
      }
      bd_blockFree(shared, pblock, FALSE);
    } else {
      /* Retained block, no resource. */
      if ( bd_isStorageMemory(block) )
        /* Make the retained block eligible for purging now the current region
           iteration has finished reading it. */
        bd_setPurgeable(shared, block, TRUE, NULL);
    }
  } else {
    /* Finished with the block altogether. */
    bd_blockFree(shared, pblock, TRUE);
  }
  return TRUE;
}

void bd_blockCheck(BackdropBlock *block)
{
#if defined( ASSERT_BUILD )
  switch ( block->storage ) {
  case STORAGE_DISK:
    HQASSERT(block->file && block->foffset != -1,
             "File and file offset not set up for STORAGE_DISK block");
    if ( !block->resource ) {
      HQASSERT(!block->data && !block->lines,
               "For a block on disk, shouldn't have data and lines if no resource");
      break;
    }
    /* fall through */
  case STORAGE_MEMORY: {
    BackdropLine *lines = bd_blockLine(block, 0);
    uint32 offset = 0, yi;

    HQASSERT(block->data, "Backdrop block data is missing");
    HQASSERT(block->lines, "Backdrop block lines is missing");
    HQASSERT(!lines[0].repeat, "First line can never be a repeat");

    if ( bd_isComplete(block) ) {
      for ( yi = 0; yi < block->height; ++yi ) {
        if ( !lines[yi].repeat ) {
          HQASSERT(offset == lines[yi].offset, "Bad offset in backdrop block");
          if ( bd_isRLE(&lines[yi]) ) {
            HQASSERT(lines[yi].nRuns > 0, "nRuns cannot be zero for RLE");
            offset += 2 * lines[yi].nRuns;
          } else {
            HQASSERT(lines[yi].nRuns == 0, "nRuns must be zero for non-RLE");
            offset += block->width;
          }
        }
      }
      HQASSERT(offset == block->dataBytes, "Unexpected data allocation size");
    } else {
      HQASSERT(block->resource, "All insertable blocks should be using a resource");
      for ( yi = 0; yi < block->height; ++yi ) {
        HQASSERT(offset == lines[yi].offset, "Bad offset in backdrop block");
        if ( bd_isRLE(&lines[yi]) )
          HQASSERT(lines[yi].nRuns == 1, "nRuns can only be one in insert mode");
        else
          HQASSERT(lines[yi].nRuns == 0, "nRuns must be zero for non-RLE");
        offset += block->width;
      }
      HQASSERT(offset <= block->dataBytes, "Unexpected data allocation size");
    }
    break;
  }
  case STORAGE_UNIFORM:
    HQASSERT(bdt_getNUsedSlots(bd_uniformTable(block)) == 1,
             "Expected only one used slot for uniform table");
    break;
  default:
    HQFAIL("Unrecognised backdrop block storage type");
  }
#else
  UNUSED_PARAM(BackdropBlock*, block);
#endif /* ASSERT_BUILD */
}

#if defined( DEBUG_BUILD ) || defined( METRICS_BUILD )
void bd_blockPrint(BackdropBlock *block, uint32 nComps)
{
  COLORVALUE *color, alpha, shape;
  COLORINFO *info;
  uint32 i;

  monitorf((uint8*)"Block %p\n", block);
  switch ( block->storage ) {
  case STORAGE_DISK:
    monitorf((uint8*)"\tBlock on disk\n");
    if ( !block->resource )
      break;
    /* fall through */
  case STORAGE_MEMORY: {
    BackdropLine *lines = bd_blockLine(block, 0);
    BackdropTable *prevTable = NULL;
    uint32 yi;
    monitorf((uint8*)"Block is %s\n",
             bd_isComplete(block) ? "complete" : "insertable");
    for ( yi = 0; yi < block->height; ++yi ) {
      BackdropTable *table = lines[yi].table;
      uint8 *data = bd_blockData(block, yi);
      if ( table && table != prevTable ) {
        /* Print the table. */
        uint16 nUsedSlots = bdt_getNUsedSlots(table), iSlot;
        monitorf((uint8*)"\ttable %p, %d slots:\n", yi, nUsedSlots);
        for ( iSlot = 0; iSlot < nUsedSlots; ++iSlot ) {
          bdt_get(table, nComps, CAST_UNSIGNED_TO_UINT8(iSlot),
                  &color, &alpha, &shape, &info);
          monitorf((uint8*)"\tcolor (%d): ", iSlot);
          for ( i = 0; i < nComps; ++i ) {
            monitorf((uint8*)"%d, ", color[i]);
          }
          monitorf((uint8*)"\n");
          data += 2;
        }
        prevTable = table;
      }
      if ( lines[yi].repeat ) {
        monitorf((uint8*)"\tline %d is a repeat\n", yi);
      } else if ( bd_isRLE(&lines[yi]) ) {
        uint32 iRun;
        monitorf((uint8*)"\tline %d (table %p) is RLE (%d runs):",
                 yi, table, lines[yi].nRuns);
        /* Print the runs. */
        for ( iRun = 0; iRun < lines[yi].nRuns; ++iRun ) {
          monitorf((uint8*)"(%d,%d), ", data[0], data[1]);
          data += 2;
        }
        monitorf((uint8*)"\n");
      } else {
        uint32 iRun;
        monitorf((uint8*)"\tline %d (table %p) is a map:", yi, table);
        /* Print the map. */
        for ( iRun = 0; iRun < block->width; ++iRun ) {
          monitorf((uint8*)"%d, ", data[0]);
          ++data;
        }
        monitorf((uint8*)"\n");
      }
    }
    break;
  }
  case STORAGE_UNIFORM: {
    BackdropTable *table = bd_uniformTable(block);
    monitorf((uint8*)"\tBlock is uniform\n");
    bdt_get(table, nComps, 0, &color, &alpha, &shape, &info);
    monitorf((uint8*)"\tcolor: ");
    for ( i = 0; i < nComps; ++i ) {
      monitorf((uint8*)"%d, ", color[i]);
    }
    monitorf((uint8*)"\n");
    break;
  }
  default:
    HQFAIL("Unrecognised backdrop block storage type");
  }
}
#endif /* DEBUG_BUILD */

#if defined( METRICS_BUILD )
static size_t bd_debugCountDuplicates(BackdropTable *table, uint32 nComps)
{
  uint16 nUsedSlots = bdt_getNUsedSlots(table), i, j;
  size_t count = 0;

  for ( i = 0; i < nUsedSlots; ++i ) {
    for ( j = i + 1; j < nUsedSlots; ++j ) {
      if ( bdt_equalEntry(table, table, nComps,
                          CAST_UNSIGNED_TO_UINT8(i), CAST_UNSIGNED_TO_UINT8(j)) )
        ++count;
    }
  }
  return count;
}
#endif

void bd_blockDebug(CompositeContext *context, Backdrop *backdrop,
                   BackdropBlock *block)
{
  UNUSED_PARAM(CompositeContext*, context);
  UNUSED_PARAM(Backdrop*, backdrop);
  UNUSED_PARAM(BackdropBlock*, block);

#if defined( DEBUG_BUILD )
 { /* If you want to see what's in a specific block call bd_blockPrint on it. */
  static Bool printBlocks = FALSE;
  if ( printBlocks )
    bd_blockPrint(block, backdrop->inComps);
 }
#endif

#if defined( METRICS_BUILD )
 { /* Gather stats on the block.  Gathering pixel or run data is slow and
      therefore restricted to debug metric builds.*/
  CompositeMetrics *metrics = &context->metrics;

  switch ( block->storage ) {
  case STORAGE_DISK:
    if ( !block->resource )
      break;
    /* fall through */
  case STORAGE_MEMORY: {
    BackdropLine *lines = bd_blockLine(block, 0);
    BackdropTable *prevTable = NULL;
    uint32 yi;
    for ( yi = 0; yi < block->height; ++yi ) {
      if ( !lines[yi].repeat ) {
        BackdropTable *table = lines[yi].table;

        if ( table && table != prevTable ) {
          metrics->nDuplicateEntries += bd_debugCountDuplicates(table, backdrop->inComps);
          prevTable = table;
        }

        if ( backdrop->compositeToPage ) {
          if ( bd_isRLE(&lines[yi]) ) {
            ++metrics->nRLELines;
            metrics->nRuns += lines[yi].nRuns;
          } else {
            ++metrics->nMapLines;
            metrics->nPixels += bd_blockWidth(block);
          }
        }
      }
    }
    break;
  }
  case STORAGE_UNIFORM:
    ++metrics->nUniformBlocks;
    break;
  default:
    HQFAIL("Unrecognised backdrop block storage type");
  }
 }
#endif
}

Bool bd_isTouched(BackdropBlock *block)
{
  return ((block->flags & BLOCKFLAG_TOUCHED) != 0);
}

void bd_setTouched(BackdropBlock *block, Bool isTouched)
{
  if ( isTouched )
    block->flags |= BLOCKFLAG_TOUCHED;
  else
    block->flags &= ~BLOCKFLAG_TOUCHED;
}

Bool bd_isComplete(BackdropBlock *block)
{
  return ((block->flags & BLOCKFLAG_COMPLETE) != 0);
}

void bd_setComplete(BackdropBlock *block, Bool isCompleted)
{
  if ( isCompleted )
    block->flags |= BLOCKFLAG_COMPLETE;
  else
    block->flags &= ~BLOCKFLAG_COMPLETE;
}

void bd_setPurgeable(const BackdropShared *shared, BackdropBlock *block,
                     Bool isPurgeable, Bool *before)
{
  if ( shared->retention == BD_RETAINNOTHING ||
       shared->imfileCtxt == NULL /* Can't allow block purging without a disk. */ ) {
    /* Can avoid a lock in this case. */
    HQASSERT(!bd_isPurgeable(block),
             "Purgeable blocks can only occur when retaining the backdrop, and have a disk");
    if ( before != NULL )
      *before = FALSE;
    return;
  }

  multi_mutex_lock(&backdropLock);

  if ( before )
    *before = bd_isPurgeable(block);

  if ( isPurgeable != bd_isPurgeable(block) ) {
    /* Cast away constness; modifying shared in a safe way. */
    BackdropShared *sharedSafe = (BackdropShared*)shared;

    if ( isPurgeable ) {
      HQASSERT(bd_isComplete(block) && bd_isStorageMemory(block) &&
               block->resource == NULL && block->file == NULL,
               "Block shouldn't be marked as purgeable");
      if ( sharedSafe->imfileCtxt != NULL /* write to disk allowed */ ) {
        /* Add block to head of purgeable list. */
        block->flags |= BLOCKFLAG_PURGEABLE;
        HQASSERT(block->prev == NULL && block->next == NULL,
                 "prev/next links should be null");
        block->next = sharedSafe->purgeableBlocks;
        if ( sharedSafe->purgeableBlocks != NULL )
          sharedSafe->purgeableBlocks->prev = block;
        sharedSafe->purgeableBlocks = block;
      }
    } else {
      /* Remove block from purgeable list. */
      if ( sharedSafe->purgeableBlocks == block )
        sharedSafe->purgeableBlocks = block->next;
      if ( block->prev != NULL )
        block->prev->next = block->next;
      if ( block->next != NULL )
        block->next->prev = block->prev;
      block->prev = block->next = NULL;
      block->flags &= ~BLOCKFLAG_PURGEABLE;
    }
  }

  multi_mutex_unlock(&backdropLock);
}

BackdropResource *bd_blockResource(BackdropBlock *block)
{
  return block->resource;
}

uint8 *bd_blockData(BackdropBlock *block, uint32 yi)
{
  return block->data + block->lines[yi].offset;
}

uint32 bd_blockWidth(BackdropBlock *block)
{
  return block->width;
}

uint32 bd_blockHeight(BackdropBlock *block)
{
  return block->height;
}

BackdropLine *bd_blockLine(BackdropBlock *block, uint32 yi)
{
  return &block->lines[yi];
}

void bd_lineAndDataRepeatSrc(BackdropBlock *block, uint32 yi,
                             BackdropLine **line, uint8 **data)
{
  yi = bd_findRepeatSource(block->lines, yi);

  *line = bd_blockLine(block, yi);
  *data = bd_blockData(block, yi);
}

/**
 * Called after an insertable block has been compacted and therefore the new
 * size should be no larger than the existing size.
 */
void bd_setDataBytes(BackdropBlock *block, uint32 dataBytes)
{
  HQASSERT(block->resource, "Shouldn't try to change dataBytes without a resource");
  HQASSERT(dataBytes <= block->dataBytes, "Should be trying to reduce dataBytes!");
  block->dataBytes = CAST_SIGNED_TO_UINT16(dataBytes);
}

int32 bd_findRepeatSource(BackdropLine *lines, uint32 yi)
{
  int32 y = (int32)yi;

  HQASSERT(!lines->repeat,
           "Can't have a repeat on the first line of a block");

  /* Search backwards until the line containing the source data is found. */
  for ( ; y >= 0 && lines[y].repeat; --y ) {
    EMPTY_STATEMENT();
  }
  HQASSERT(y >= 0 && !lines[y].repeat,
           "Couldn't find source line for the repeat");

  return (uint32)y;
}

Bool bd_isStorageMemory(BackdropBlock *block)
{
  return block->storage == STORAGE_MEMORY;
}

Bool bd_isUniform(BackdropBlock *block)
{
  return block->storage == STORAGE_UNIFORM;
}

void bd_setUniform(BackdropBlock *block)
{
  block->storage = STORAGE_UNIFORM;
}

/**
 * Uniform blocks have one table containing just one entry.  This is simply
 * referred to by the lines ptr to save having another ptr in the block
 * structure, or the hassle of a union.  The uniform block table is allocated
 * from state memory which is only 4 byte aligned, and 8 byte alignment is
 * required.
 */
BackdropTable *bd_uniformTable(BackdropBlock *block)
{
  HQASSERT(bd_isUniform(block), "Expected uniform block");
  if ( block->resource )
    return bd_blockLine(block, 0)->table;
  else
    return (BackdropTable*)DWORD_ALIGN_UP(uintptr_t, block->lines);
}

/* Log stripped */
