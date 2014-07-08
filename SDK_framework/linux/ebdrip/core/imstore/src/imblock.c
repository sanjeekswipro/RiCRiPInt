/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imblock.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image block storage implementation
 */

#include "core.h"
#include "coreinit.h"

#include "swtrace.h"            /* SW_TRACE_INVALID */
#include "control.h"            /* interrupts_clear */
#include "dicthash.h"           /* fast_insert_hash */
#include "display.h"            /* DL_LASTBAND */
#include "dlstate.h"            /* DL_STATE */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "imageo.h"             /* IMAGEOBJECT */
#include "mlock.h"              /* multi_mutex_* */
#include "mm.h"
#include "mmcompat.h"           /* mm_alloc_static */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_Columns */
#include "often.h"              /* SwOftenUnsafe */
#include "rcbcntrl.h"           /* rcbn_intercepting */
#include "devops.h"             /* pagedevice_is_rendering */
#include "strfilt.h"            /* string_decode_filter */
#include "swerrors.h"           /* VMERROR */
#include "interrupts.h"

#include "imb32.h"              /* imb32_compress */
#include "imblist.h"            /* blist_setblock */
#include "imfile.h"             /* im_fileoffset */
#include "imstore_priv.h"
#include "imblock.h"
#include "imstore.h"
#include "imcontext.h"
#include "hqmemcmp.h"


struct SWSTART ; /* for swstart routine */

/**
 * Definition of an image block, private to this module
 *
 * IM_BLOCK structure was shared amongst all of the modules, and was
 * modified in various ways by various bits of code.
 * Taken the defintion private, and added a somewhat random API on top.
 * Current API evolved from original usage, and needs to be cleaned-up into
 * a much simpler interface.
 */
struct IM_BLOCK {
  uint8 storage;       /**< Gives the storage format. */
  uint8 compress;      /**< Gives the compression format. */
  uint8 flags;         /**< IM_BLOCKFLAG_* */
  uint8 refcount;      /**< Number of processes using this block */

  IM_FILES *ffile;     /**< The file handle if purged to disk. */
  int32 foffset;       /**< If >= 0 gives the disk offset. */

  uint8 *data;         /**< Block of memory where data stored. */

  uint8 *cdata;        /**< Block of memory where compressed data stored. */
  int16 cbytes;        /**< Size of compressed data. */

  int16 xsize;         /**< Size of this block in pixels. */
  int16 ysize;         /**< Size of this block in pixels. */

  int16 xbytes;        /**< Offset from one line to the next. */
  int16 ybytes;        /**< Number of bytes in one column. */

  int16 sbytes;        /**< Stored bytes in this block. */
  int16 rbytes;        /**< Bytes to read from file (different in xyswap case) */
  int16 tbytes;        /**< Total bytes in this block. */
  int16 abytes;        /**< Allocated data bytes in this block.
                            Normally == tbytes, but end row isn't */

  uint16 uniformColor; /**< Color of uniform block. */
  IM_BLIST *blist;     /**< Pointer into the block list cache.*/
};

static multi_condvar_t im_load_condvar;
static multi_condvar_t im_get_condvar;
static multi_mutex_t im_block_mutex;

void im_blockForbid(void)
{
  multi_mutex_lock(&im_block_mutex);
}

void im_blockAllow(void)
{
  multi_mutex_unlock(&im_block_mutex);
}

static Bool im_block_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  multi_mutex_init(&im_block_mutex, IMBLOCK_LOCK_INDEX, FALSE,
                   SW_TRACE_IM_BLOCK_ACQUIRE, SW_TRACE_IM_BLOCK_HOLD);
  multi_condvar_init(&im_load_condvar, &im_block_mutex, SW_TRACE_IM_LOAD_WAIT);
  multi_condvar_init(&im_get_condvar, &im_block_mutex, SW_TRACE_IM_GET_WAIT);
  return TRUE;
}

static void im_block_finish(void)
{
  multi_condvar_finish(&im_get_condvar);
  multi_condvar_finish(&im_load_condvar);
  multi_mutex_finish(&im_block_mutex);
}

void im_block_C_globals(core_init_fns *fns)
{
  fns->swstart = im_block_swstart ;
  fns->finish = im_block_finish ;
}

Bool im_blocksetup(IM_STORE *ims, int32 plane, int32 bx, int32 by)
{
  IM_BLOCK** pblock;
  IM_BLOCK* block;
  int32 bpps, xsize, ysize;
  int32 xbytes, tbytes;
  int32 bb;

  HQASSERT(bx <= ims->xblock && by <= ims->yblock, "bx/by out of range");

  bb = bx + by * ims->xblock;
  HQASSERT(bb >= 0 && bb < ims->nblocks, "block out of range");
  pblock = & ims->planes[plane]->blocks[bb];
  block = *pblock;

  if (*pblock == NULL) {
    *pblock = dl_alloc(ims->im_shared->page->dlpools, sizeof(IM_BLOCK),
                       MM_ALLOC_CLASS_IMAGE_DATA);
    if (*pblock == NULL)
      return error_handler(VMERROR);

    block = *pblock;
  }

  block->storage  = IM_STORAGE_NONE;
  block->compress = IM_COMPRESS_NONE;
  block->flags    = 0;
  block->refcount = 0;
  block->ffile    = NULL;
  block->foffset  = -1;
  block->cdata    = NULL;
  block->cbytes   = -1;
  block->data     = NULL;
  block->xsize    = 0;
  block->ysize    = 0;
  block->xbytes   = 0;
  block->ybytes   = 0;
  block->sbytes   = 0;
  block->rbytes   = 0;
  block->tbytes   = 0;
  block->abytes   = 0;
  block->uniformColor = 0;
  block->blist    = NULL;

  /* Work out how big the block needs to be. */
  bpps = ims->bpps;
  xbytes = ims->blockWidth;
  xsize  = ( xbytes << 3 ) >> bpps;
  if ( bx == ims->xblock - 1 ) {
    int32 imsxsize = ims->obbox.x2 - ims->obbox.x1 + 1;
    xbytes = ((imsxsize << bpps ) + 7 ) >> 3;
    xbytes = im_index_xi(ims, xbytes);
    xsize = imsxsize & ( xsize - 1 );
    if ( xbytes == 0 ) {
      xbytes = ims->blockWidth;
      xsize = ( xbytes << 3 ) >> bpps;
    }
  }
  block->xsize  = CAST_SIGNED_TO_INT16(xsize);
  block->xbytes = CAST_SIGNED_TO_INT16(xbytes);

  ysize = ims->blockHeight;
  if ( by == ims->yblock - 1 ) {
    int32 imsysize = ims->obbox.y2 - ims->obbox.y1 + 1;
    ysize = im_index_yi(ims, imsysize);
    if ( ysize == 0 )
      ysize = ims->blockHeight;
  }
  block->ysize  = CAST_SIGNED_TO_INT16(ysize);
  block->ybytes = CAST_SIGNED_TO_INT16((( ysize << bpps ) + 7 ) >> 3 );

  if ( (ims->flags & IMS_XYSWAP) && ims->bpps < 3 )
    /* Round up to the nearest multiple of 8. */
    tbytes = xbytes * (( ysize + 7 ) & (~7));
  else
    tbytes = xbytes * ysize;
  block->tbytes = CAST_SIGNED_TO_INT16(tbytes);

  if ( ims->flags & IMS_XYSWAP )
    block->rbytes = CAST_SIGNED_TO_INT16(block->ybytes * xsize);
  else
    block->rbytes = block->tbytes;

  return TRUE;
}

static void block_from_blist(IM_BLOCK *block, IM_BLIST *blist,
                             Bool preserveStorage)
{
  int16 abytes;

  HQASSERT(block != NULL, "Null block");
  HQASSERT(blist != NULL, "Null blist");

  blist_setblock(blist, block);
  block->blist = blist;
  block->data = blist_getdata(blist, &abytes);
  block->abytes = abytes;
  if ( !preserveStorage )
    block->storage = IM_STORAGE_MEMORY;
}

static int16 get_abytes(IM_BLIST *blist)
{
  int16 abytes;

  (void)blist_getdata(blist, &abytes);
  return abytes;
}

static Bool im_blockalloc(IM_STORE *ims, IM_BLOCK *block,
                          int32 abytes, int32 plane,
                          int32 bx, int32 by, Bool preAlloc )
{
  IM_BLIST *blist = NULL;

  HQASSERT(ims, "ims NULL");
  HQASSERT(block, "block NULL");
  HQASSERT(!im_blockcomplete(block), "block must not be complete");
  HQASSERT(block->data == NULL, "block data already allocated");
  HQASSERT(block->abytes == 0, "block abytes should be 0");
  HQASSERT(abytes > 0, "abytes should be > 0");

  if ( ims->flags & IMS_XYSWAP ) {
    int32 tmp = bx;
    bx = by;
    by = tmp;
  }

  /* Attempt to find memory for the block data. No low-memory handling
     because it's cheapest to recycle an image block. */
  if (!ims->blistPurgeRow)
    block->data = im_dataalloc(ims->im_shared, abytes, mm_cost_none);
  if ( block->data == NULL ) {
    if (!ims->blistPurgeRow && !preAlloc) {
      /* If we are in low memory for the first time with the current image,
       * then purge a row from it, if possible, as the most effective way of
       * regaining memory.
       * NB. There is never anything to purge in the current store when
       * pre-allocating.
       */
      (void)im_storepurgeOne(ims);

      /* In future, the store will be purged at the end of each row.
       * NB. Don't purge while pre-allocating since low memory handling in
       * the pre-allocation loop will potentially partial paint, etc which
       * may obviate the need to purge this store at all.
       */
      ims->blistPurgeRow = TRUE;
    }
    blist = blist_find(ims, abytes, plane, bx, TRUE, FALSE);

    if ( blist == NULL ) {
      block->data = im_dataalloc(ims->im_shared, abytes, mm_cost_normal);
      if ( block->data == NULL && (ims->flags & IMS_DESPERATE) != 0 )
        /* If can't alloc, try to steal a blist desperately. */
        blist = blist_find(ims, abytes, plane, bx, TRUE, TRUE);
    }
    if ( blist != NULL )
      block_from_blist(block, blist, TRUE);

    /* We failed to grab a data block, so give up here */
    if (block->data == NULL)
      return error_handler(VMERROR);

    if ( blist != NULL ) {
      HQASSERT(blist_getdata(blist, NULL) != NULL, "block data got lost");
      HQASSERT(get_abytes(blist) >= abytes, "got blist but abytes too small");
      blist_setblock(blist, block);
      block->blist = blist;
    }
  }

  if ( blist != NULL )
    block->abytes    = get_abytes(blist);
  else {
    block->abytes    = CAST_SIGNED_TO_INT16(abytes);

    /* Create a minimum number of blists */
    if ( ims->planes[plane]->nBlists < ims->planes[plane]->nDesiredBlists ) {
      if ( ( blist = blist_create(ims->im_shared, bx, FALSE,
                                  block, 0, mm_cost_normal) ) == NULL )
        return error_handler(VMERROR);

      blist_link(blist, ims->planes[plane]);
    }
  }

  if ( block->abytes == IM_BLOCK_DEFAULT_SIZE )
    ims->stdblocks += 1;
  else
    ims->extblocks += block->abytes;

  return TRUE;
}

static Bool im_blocknew(IM_STORE *ims, IM_BLOCK *block,
                        int32 plane, int32 bx, int32 by, Bool preAlloc)
{
  int32 abytes;

  HQASSERT(ims, "ims NULL in im_blocknew");
  HQASSERT(block, "block NULL in im_blocknew");
  HQASSERT(plane >= 0, "plane should be >= 0");
  HQASSERT(plane < ims->nplanes, "plane should be < ims->nplanes");
  HQASSERT(bx >= 0, "bx should be >= 0");
  HQASSERT(bx < ims->xblock, "bx should be < ims->xblock");
  HQASSERT(by >= 0, "by should be >= 0");
  HQASSERT(by < ims->yblock, "by should be > ims->yblock");

  abytes = im_blockDefaultAbytes(ims);

  if ( ! im_blockalloc(ims, block, abytes, plane, bx, by, preAlloc))
    return FALSE;

  /* Update fields that have changed. */
  block->storage = IM_STORAGE_MEMORY;
  HQASSERT(block->abytes >= abytes, "block->abytes too small");

  HQASSERT(block->rbytes <= block->tbytes,
           "Didn't allocate enough bytes for rotation");
  HQASSERT(block->tbytes <= block->abytes, "Didn't allocate enough bytes");

  return TRUE;
}

int16 im_blockDefaultAbytes(IM_STORE *ims)
{
  int32 abytes;
  int32 xbytes;
  int32 xsize, ysize;

  xsize = ims->obbox.x2 - ims->obbox.x1 + 1;
  xbytes = ((xsize << ims->bpps) + 7) >> 3;
  xbytes = im_index_xi(ims, xbytes);
  if ( xbytes == 0 )
    xbytes = ims->blockWidth;

  ysize = ims->obbox.y2 - ims->obbox.y1 + 1;
  ysize = im_index_yi(ims, ysize);
  if ( ysize == 0 )
    ysize = ims->blockHeight;

  abytes = (ims->xblock > 1 ? ims->blockWidth : xbytes);
  if (ims->yblock > 1)
    abytes *= ims->blockHeight;
  else if ( (ims->flags & IMS_XYSWAP) && ims->bpps < 3 )
    abytes *= ( ysize + 7 ) & (~7);
  else
    abytes *= ysize;

  return CAST_SIGNED_TO_INT16(abytes);
}

static uint32 im_rtab1[16] =
{
  0x00000000, 0x00000001, 0x00000100, 0x00000101,
  0x00010000, 0x00010001, 0x00010100, 0x00010101,
  0x01000000, 0x01000001, 0x01000100, 0x01000101,
  0x01010000, 0x01010001, 0x01010100, 0x01010101
};
static uint32 im_rtab2[16] =
{
  0x00000000, 0x00000002, 0x00000200, 0x00000202,
  0x00020000, 0x00020002, 0x00020200, 0x00020202,
  0x02000000, 0x02000002, 0x02000200, 0x02000202,
  0x02020000, 0x02020002, 0x02020200, 0x02020202
};
static uint32 im_rtab3[16] =
{
  0x00000000, 0x00000004, 0x00000400, 0x00000404,
  0x00040000, 0x00040004, 0x00040400, 0x00040404,
  0x04000000, 0x04000004, 0x04000400, 0x04000404,
  0x04040000, 0x04040004, 0x04040400, 0x04040404
};
static uint32 im_rtab4[16] =
{
  0x00000000, 0x00000008, 0x00000800, 0x00000808,
  0x00080000, 0x00080008, 0x00080800, 0x00080808,
  0x08000000, 0x08000008, 0x08000800, 0x08000808,
  0x08080000, 0x08080008, 0x08080800, 0x08080808
};
static uint32 im_rtab5[16] =
{
  0x00000000, 0x00000010, 0x00001000, 0x00001010,
  0x00100000, 0x00100010, 0x00101000, 0x00101010,
  0x10000000, 0x10000010, 0x10001000, 0x10001010,
  0x10100000, 0x10100010, 0x10101000, 0x10101010
};
static uint32 im_rtab6[16] =
{
  0x00000000, 0x00000020, 0x00002000, 0x00002020,
  0x00200000, 0x00200020, 0x00202000, 0x00202020,
  0x20000000, 0x20000020, 0x20002000, 0x20002020,
  0x20200000, 0x20200020, 0x20202000, 0x20202020
};
static uint32 im_rtab7[16] =
{
  0x00000000, 0x00000040, 0x00004000, 0x00004040,
  0x00400000, 0x00400040, 0x00404000, 0x00404040,
  0x40000000, 0x40000040, 0x40004000, 0x40004040,
  0x40400000, 0x40400040, 0x40404000, 0x40404040
};
static uint32 im_rtab8[16] =
{
  0x00000000, 0x00000080, 0x00008000, 0x00008080,
  0x00800000, 0x00800080, 0x00808000, 0x00808080,
  0x80000000, 0x80000080, 0x80008000, 0x80008080,
  0x80800000, 0x80800080, 0x80808000, 0x80808080
};

static void im_blockxyswap1(IM_STORE *ims, IM_BLOCK *block)
{
  int32 x, y;

  for ( x = 0; x < block->xsize; x += 8 ) {
    for ( y = 0; y < block->ysize; y += 8 ) {
      uint32 data0 = 0u, data1 = 0u;

#define IM_RLOAD1( _src, _data0, _data1, _tab, _ybytes ) MACRO_START \
  uint8 _cval_ = (_src)[ 0 ]; \
  (_data0) |= (_tab)[ _cval_ >> 4 ]; \
  (_data1) |= (_tab)[ _cval_ & 15 ]; \
  (_src) += (_ybytes); \
MACRO_END
      { int32 ybytes = block->ybytes;
        uint8 *src = ims->swapmem + ( y >> 3 ) + x * ybytes;
        IM_RLOAD1( src, data0, data1, im_rtab8, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab7, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab6, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab5, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab4, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab3, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab2, ybytes );
        IM_RLOAD1( src, data0, data1, im_rtab1, ybytes );
      }
#define IM_RSAVE1( _dst, _data, _shift, _xbytes ) MACRO_START \
  (_dst)[ 0 ] = ( uint8 )((_data) >> (_shift)); \
  (_dst) += (_xbytes); \
MACRO_END
      { int32 xbytes = block->xbytes;
        uint8 *dst = block->data + ( x >> 3 ) + y * xbytes;
        IM_RSAVE1( dst, data0, 24, xbytes );
        IM_RSAVE1( dst, data0, 16, xbytes );
        IM_RSAVE1( dst, data0,  8, xbytes );
        IM_RSAVE1( dst, data0,  0, xbytes );
        IM_RSAVE1( dst, data1, 24, xbytes );
        IM_RSAVE1( dst, data1, 16, xbytes );
        IM_RSAVE1( dst, data1,  8, xbytes );
        IM_RSAVE1( dst, data1,  0, xbytes );
      }
    }
  }
}

static void im_blockxyswap2( IM_STORE *ims, IM_BLOCK *block )
{
  int32 x, y;

  for ( x = 0; x < block->xsize; x += 4 ) {
    for ( y = 0; y < block->ysize; y += 4 ) {
      uint32 src0, src1, src2, src3;

      { int32 ybytes = block->ybytes;
        uint8 *src = ims->swapmem + ( y >> 2 ) + x * ybytes;
        src0 = src[ 0 ]; src += ybytes;
        src1 = src[ 0 ]; src += ybytes;
        src2 = src[ 0 ]; src += ybytes;
        src3 = src[ 0 ]; src += ybytes;
      }
      { int32 xbytes = block->xbytes;
        uint8 *dst = block->data + ( x >> 2 ) + y * xbytes;
        dst[ 0 ] = ( uint8 )
                   ((( src0 >> 0 ) & 0xc0) |
                    (( src1 >> 2 ) & 0x30) |
                    (( src2 >> 4 ) & 0x0c) |
                    (( src3 >> 6 ) & 0x03) );
        dst += xbytes;
        dst[ 0 ] = ( uint8 )
                   ((( src0 << 2 ) & 0xc0) |
                    (( src1 << 0 ) & 0x30) |
                    (( src2 >> 2 ) & 0x0c) |
                    (( src3 >> 4 ) & 0x03) );
        dst += xbytes;
        dst[ 0 ] = ( uint8 )
                   ((( src0 << 4 ) & 0xc0) |
                    (( src1 << 2 ) & 0x30) |
                    (( src2 >> 0 ) & 0x0c) |
                    (( src3 >> 2 ) & 0x03) );
        dst += xbytes;
        dst[ 0 ] = ( uint8 )
                   ((( src0 << 6 ) & 0xc0) |
                    (( src1 << 4 ) & 0x30) |
                    (( src2 << 2 ) & 0x0c) |
                    (( src3 << 0 ) & 0x03) );
        dst += xbytes;
      }
    }
  }
}

static void im_blockxyswap4( IM_STORE *ims, IM_BLOCK *block )
{
  int32 x, y;

  for ( x = 0; x < block->xsize; x += 2 ) {
    for ( y = 0; y < block->ysize; y += 2 ) {
      uint32 src0, src1;

      { int32 ybytes = block->ybytes;
        uint8 *src = ims->swapmem + ( y >> 1 ) + x * ybytes;
        src0 = src[ 0 ]; src += ybytes;
        src1 = src[ 0 ]; src += ybytes;
      }
      { int32 xbytes = block->xbytes;
        uint8 *dst = block->data + ( x >> 1 ) + y * xbytes;
        dst[ 0 ] = ( uint8 )
                   ((( src0 >> 0 ) & 0xf0) |
                    (( src1 >> 4 ) & 0x0f) );
        dst += xbytes;
        dst[ 0 ] = ( uint8 )
                   ((( src0 << 4 ) & 0xf0) |
                    (( src1 >> 0 ) & 0x0f) );
        dst += xbytes;
      }
    }
  }
}

static void im_blockxyswap8( IM_STORE *ims, IM_BLOCK *block )
{
  int32 x, y;

  for ( x = 0; x < block->xsize; x ++ ) {
    uint8 *src = ims->swapmem + x * block->ybytes;
    uint8 *dst = block->data + x;
    for ( y = 0; y < block->ysize; y ++ ) {
      *dst = *src;
      src ++;
      dst += block->xbytes;
    }
  }
}

static void im_blockxyswap16( IM_STORE *ims, IM_BLOCK *block )
{
  int32 x, y;

  for ( x = 0; x < block->xsize; x ++ ) {
    uint16 *src = (uint16 *) (ims->swapmem + x * block->ybytes);
    uint16 *dst = (uint16 *) (block->data) + x;
    for ( y = 0; y < block->ysize; y ++ ) {
      *dst = *src;
      src ++;
      dst += block->xbytes >> 1;
    }
  }
}

static void im_blockxyswap(IM_STORE *ims, IM_BLOCK *block)
{
  HQASSERT(ims, "ims NULL");
  HQASSERT(block, "block NULL");
  HQASSERT(ims->swapmem, "somehow lost xyswap memory");

  block->sbytes = block->rbytes = block->tbytes;
  if ( block->abytes == ims->abytes ) {
   /* Can simply xyswap into xyswap buffer & swap over - as long as we
    * remember to update the blist (if this block has one).
    * NB. swapmem has a size of ims->abytes. The block may be larger if it was
    *     a global block.
    */
    uint8 *data = block->data;

    block->data = ims->swapmem;
    if ( block->blist ) {
      int16 abytes;
      (void)blist_getdata(block->blist, &abytes);
      blist_setdata(block->blist, ims->swapmem, abytes);
    }
    ims->swapmem = data;
  } else {
    /* Need to copy into xyswap buffer and xyswap back into original. */
    HQASSERT(block->tbytes <= block->abytes, "block too large for copy");
    HQASSERT(block->tbytes <= ims->abytes, "block too large for copy");
    HQASSERT(block->blist && blist_wasGlobal(block->blist),
        "Non-standard block should have global data");
    HqMemCpy(ims->swapmem, block->data, block->tbytes);
  }

  switch ( ims->bpp ) {
    case 1:
      im_blockxyswap1(ims, block);
      break;
    case 2:
      im_blockxyswap2(ims, block);
      break;
    case 4:
      im_blockxyswap4(ims, block);
      break;
    case 8:
      im_blockxyswap8(ims, block);
      break;
    case 16:
      im_blockxyswap16(ims, block);
      break;
    default:
      HQFAIL("corrupt bpp");
  }
}

static Bool im_blockrealloc(IM_STORE *ims, IM_BLOCK *block,
                            int32 plane, int32 bx, Bool rendering)
{
  /* This routine attempts to locate a block to use for a certain position.
   * It does this by locating the moveable block for this column, or failing
   * to find that it locates any block from any image, or failing that any
   * block from this image.
   */
  IM_BLIST *blist = NULL;

  HQASSERT(block->data == NULL, "image block with data");
  HQASSERT((block->flags & IM_BLOCKFLAG_IS_UNIFORM) == 0,
            "image block with uniform color");
  HQASSERT(block->storage == IM_STORAGE_MEMORY ||
            block->storage == IM_STORAGE_DISK ||
            block->storage == IM_STORAGE_UNIFORM_VARIANT,
            "Unexpected block storage type");

  blist = blist_find(ims, ims->abytes, plane, bx, TRUE, rendering);

  if ( blist != NULL ) {
    /* actually found a block */
    HQASSERT(get_abytes(blist) >= block->tbytes, "got blist : abytes too small");

    block_from_blist(block, blist, TRUE);
  } else {
    /* Lets go ahead and manufacture the storage (blist & block data) if we
     * can.
     */
    block->data = im_dataalloc(ims->im_shared, block->abytes, mm_cost_none);
    if ( block->data == NULL ) {
      /** \todo BMJ 29-Jun-11 :  Not sure if this is right. There is a retry
       * loop in the caller but it relies on true being returned. Are we
       * ever retrying ?
       */
      return error_handler(VMERROR);
    }
    if ( ims->planes[plane]->nBlists < ims->planes[plane]->nDesiredBlists ) {
      if ( ( blist = blist_create(ims->im_shared, bx, FALSE,
                                  block, 0, mm_cost_normal) ) == NULL )
        return error_handler(VMERROR);

      blist_link(blist, ims->planes[plane]);
    }
  }
  return TRUE;
}

static Bool im_filter(uint8 *filtername, IMFPARAMS *params, int32 nparams,
                      uint8 *srcbuf, int32 srclen, int32 srcrepeat,
                      uint8 *dstbuf, int32 dstlen, int32 *dstused)
{
  Bool result;
  FILELIST *srcflptr, srcfilter;
  FILELIST *dstflptr, dstfilter;
  OBJECT intobj = OBJECT_NOTVM_NOTHING;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;
  OBJECT dictobj = OBJECT_NOTVM_NOTHING;
#define IM_COMPRESSDLEN 4
  OBJECT dictobjects[ NDICTOBJECTS(IM_COMPRESSDLEN) ];

  if ( nparams > IM_COMPRESSDLEN )
    return FALSE;

  srcflptr = filter_standard_find(filtername,
                                  strlen_int32((char *)filtername));
  HQASSERT(srcflptr, "Didn't find compression/decompression filter");
  srcfilter = *srcflptr; srcflptr = & srcfilter;

  /* Create the output/input string (buffer) filter. Don't know if dstlen
     can be > 65535 yet, so this may need tweaking. */
  dstflptr = & dstfilter;
  if ( (theIFlags(srcflptr) & WRITE_FLAG ) != 0 ) {
    string_encode_filter(dstflptr);
    theLen(snewobj) = CAST_SIGNED_TO_UINT16(dstlen);
    oString(snewobj) = dstbuf;
  } else {
    string_decode_filter(dstflptr);
    theLen(snewobj) = CAST_SIGNED_TO_UINT16(srclen);
    oString(snewobj) = srcbuf;
  }
  result = (*theIMyInitFile(dstflptr))(dstflptr, &snewobj, NULL);
  HQASSERT(result, "Should not have had problems initialising string filter");

  /* Setup OBJECTs we need to control filter. */
  init_dictionary(&dictobj, IM_COMPRESSDLEN, UNLIMITED,
                  dictobjects, ISNOTVMDICTMARK(SAVEMASK));

  theTags(nameobj) = ONAME;
  theTags(intobj) = OINTEGER;
  while ((--nparams) >= 0 ) {
    oName(nameobj) = system_names + params[ nparams ].name;
    oInteger(intobj) = params[ nparams ].integer;
    result = insert_hash_with_alloc( & dictobj, & nameobj, & intobj,
                                     INSERT_HASH_NORMAL,
                                     no_dict_extension, NULL);
    HQASSERT(result, "Should not have had problems setting up filter dict");
  }

  /* Create the compression/decompression filter. */
  if ( ! (*theIMyInitFile(srcflptr))(srcflptr, & dictobj, NULL )) {
    /* dispose for string filter (dstflptr) is a noop */
    return (*theIFileLastError(srcflptr))(srcflptr );
  }

  /* Set the output/input of the compression/decompression
     filter to be the string (buffer). */
  theIUnderFile(srcflptr) = dstflptr;
  theIUnderFilterId(srcflptr) = theIFilterId(dstflptr );

  /* Do all the compression; should have called filter init, so don't need to
     fake initial state as was done before. Both CCITTFax and LZW set the
     state. */
  HQASSERT(theIFilterState(srcflptr ) == FILTER_INIT_STATE,
           "Source filter didn't start in initial state");
  if ((theIFlags(srcflptr) & WRITE_FLAG ) != 0 ) {
    while ((--srcrepeat) >= 0 ) {
      theICount(srcflptr) = srclen;
      HqMemCpy(theIBuffer(srcflptr), srcbuf, srclen);
      if ( ! (*theIFilterEncode(srcflptr))(srcflptr ))
        return (*theIFileLastError(srcflptr))(srcflptr);
      theIFilterState(srcflptr) = FILTER_EMPTY_STATE;
      srcbuf += srclen;
    }
    theICount(srcflptr) = 0;  /* Must set this as the close does an Encode. */
    theIFilterState(srcflptr) = FILTER_EOF_STATE;
    if ((*theIMyCloseFile(srcflptr))(srcflptr, CLOSE_EXPLICIT ) == EOF )
      return FALSE;
    (*dstused) = theICount(dstflptr);
  }
  else {
    (*dstused) = dstlen;
    do {
      int32 bytes;
      if ( ! (*theIFilterDecode(srcflptr))(srcflptr, & bytes ))
        return (*theIFileLastError(srcflptr))(srcflptr);
      theIFilterState(srcflptr) = FILTER_EMPTY_STATE;
      if ( bytes < 0 )
        bytes = -bytes;
      if ( bytes == 0 )
        return error_handler(UNREGISTERED);
      if ( bytes > dstlen )
        return error_handler(UNREGISTERED);
      HqMemCpy(dstbuf, theIBuffer(srcflptr), bytes);
      dstbuf += bytes;
      dstlen -= bytes;
    } while ( dstlen > 0 );
    theIFilterState(srcflptr) = FILTER_EOF_STATE;
    if ( (*theIMyCloseFile(srcflptr))(srcflptr, CLOSE_EXPLICIT ) == EOF )
      return FALSE;
  }
  return TRUE;
}

static Bool im_blockcompress(IM_STORE *ims, IM_BLOCK *block, uint8 *buffer,
                             int32 bufsize, int32 *bufused)
{
  block->compress = IM_COMPRESS_TOO_BIG;

  if ( ims->bpp == 1 ) {
    IMFPARAMS ccittparams[ 3 ] =
    {
      { NAME_Columns, 0x7FFFFFFF },
      { NAME_Rows   , 0x7FFFFFFF },
      { NAME_K      , -1 }
    };
    ccittparams[ 0 ].integer = block->xsize;
    ccittparams[ 1 ].integer = block->ysize;

    if ( im_filter((uint8 *)"CCITTFaxEncode", ccittparams, 3,
                    block->data, block->xbytes, block->ysize,
                    buffer, bufsize, bufused ))
      block->compress = IM_COMPRESS_CCITT;
    else
      error_clear();
  }
  else if ( ims->bpp == 32 ) {
    if ( (*bufused = imb32_compress(FLT0TO1, (float *)(block->data),
                                    block->xsize, block->ysize,
                                    (uint32 *)buffer, bufsize)) > 0 )
      block->compress = IM_COMPRESS_B32;
  }
  else {
    IMFPARAMS lzwparams[ 3 ] =
    {
      { NAME_Columns         , 0x7FFFFFFF },
      { NAME_BitsPerComponent, 0x7FFFFFFF },
      { NAME_Colors          , 1 }
    };
    lzwparams[ 0 ].integer = ims->bpp > 8 ? 2 * block->xsize : block->xsize;
    lzwparams[ 1 ].integer = ims->bpp > 8 ? 8 : ims->bpp;

    if ( im_filter((uint8 *)"LZWEncode", lzwparams, 3,
                    block->data, block->xbytes, block->ysize,
                    buffer, bufsize, bufused ))
      block->compress = IM_COMPRESS_LZW;
    else
      error_clear();
  }

  return (block->compress != IM_COMPRESS_TOO_BIG);
}

static Bool im_blockdecompress(IM_STORE *ims, IM_BLOCK *block, uint8 *buffer)
{
  int32 used;
  if ( block->compress == IM_COMPRESS_CCITT ) {
    IMFPARAMS ccittparams[ 3 ] =
    {
      { NAME_Columns, 0x7FFFFFFF },
      { NAME_Rows   , 0x7FFFFFFF },
      { NAME_K      , -1 }
    };
    ccittparams[ 0 ].integer = block->xsize;
    ccittparams[ 1 ].integer = block->ysize;

    if ( !im_filter((uint8 *)"CCITTFaxDecode", ccittparams, 3,
                      buffer, block->cbytes, 1,
                      block->data, block->xbytes * block->ysize, & used ))
      return FALSE;
    HQASSERT(used == block->xbytes * block->ysize,
      "Should have decompressed whole buffer");
  }
  else if ( block->compress == IM_COMPRESS_LZW ) {
    IMFPARAMS lzwparams[ 3 ] =
    {
      { NAME_Columns         , 0x7FFFFFFF },
      { NAME_BitsPerComponent, 0x7FFFFFFF },
      { NAME_Colors          , 1 }
    };
    lzwparams[ 0 ].integer = ims->bpp > 8 ? 2 * block->xsize : block->xsize;
    lzwparams[ 1 ].integer = ims->bpp > 8 ? 8 : ims->bpp;

    if ( ! im_filter((uint8 *)"LZWDecode", lzwparams, 3,
                      buffer, block->cbytes, 1,
                      block->data, block->xbytes * block->ysize, & used ))
      return FALSE;
    HQASSERT(used == block->xbytes * block->ysize,
      "Should have decompressed whole buffer");
  }
  else if ( block->compress == IM_COMPRESS_B32 ) {
    if ( !imb32_decompress((uint32 *)buffer, block->cbytes,
                           (float *)(block->data), block->xsize, block->ysize) )
      return FALSE;
  }
  else {
    HQASSERT(block->compress == IM_COMPRESS_COPY, "invalid compress method");
    HqMemCpy( block->data, buffer, block->cbytes );
  }

  return TRUE;
}

static Bool im_blockisICompressed(IM_BLOCK *block)
{
  return block->compress != IM_COMPRESS_NONE &&
         block->compress != IM_COMPRESS_TOO_BIG &&
         block->compress != IM_COMPRESS_FAILED;
}

/* Checks to see if a data block is uniform in color.
 * This may seem an expensive function but this is only called when in low
 * memory handling or when trying avoid compositing softmask images.  This
 * function usually returns very quickly if it isn't uniform.  For some classes
 * of jobs with large uniform area of image the savings in purging uniform data
 * to disk can be large.
 */
Bool im_blockUniform(IM_STORE *ims, IM_BLOCK *block, Bool freeData)
{
  Bool uniformData = TRUE;
  uint16 uniformColor = 0;
  uint32 i;

  if ( (block->flags & IM_BLOCKFLAG_CHECKED_FOR_UNIFORM_DATA) != 0 )
    return block->storage == IM_STORAGE_UNIFORM_VARIANT ;

  block->flags |= IM_BLOCKFLAG_CHECKED_FOR_UNIFORM_DATA;

  /* Check for a compressed or trimmed block */
  if (block->data == NULL)
    return FALSE;

  if ( ims->bpp == 16 ) {
    uint16 *data = (uint16*)block->data ;
    uint32 npixels = block->tbytes >> 1 ;

    HQASSERT((block->tbytes & 1) == 0, "tbytes must be even for 16-bit image data") ;

    uniformColor = data[0] ;

    for ( i = 1; i < npixels; ++i ) {
      if ( data[i] != uniformColor ) {
        uniformData = FALSE;
        break;
      }
    }
  } else {
    /* @@JJ Does not work properly for partial bytes at end of 1, 2 & 4 bpp data. */
    uint8 *data = block->data ;
    uint32 npixels = block->tbytes ;

    uniformColor = block->data[0] ;

    for ( i = 1; i < npixels; ++i ) {
      if ( data[i] != data[0] ) {
        uniformData = FALSE;
        break;
      }
    }
  }

  if (uniformData) {
    block->storage = IM_STORAGE_UNIFORM_VARIANT;
    /* Save the color index into the x lut
     * NB. No need to worry about xyswap here because the run length will end
     * up being the xsize regardless of the orientation of xyswap.
     */
    block->uniformColor = uniformColor;

    if ( freeData ) {
      HQASSERT(block->blist == NULL,
               "Didn't expect a blist when freeing data");
      im_datafree(ims->im_shared, block->data, block->abytes );
      block->data = NULL;
    }
    return TRUE;
  }

  return FALSE;
}

/**
 * Convert an image block to compressed format
 */
static void im_blocktocomp(IM_STORE *ims, IM_BLOCK *block, Bool freeData)
{
  int32 bufsize;
  int32 bufused = 0;
  uint8 buffer[ IM_BLOCK_DEFAULT_SIZE ];
  uint8 *pBuffer = buffer;

  HQASSERT(ims, "ims NULL");
  HQASSERT(block, "block NULL");

  HQASSERT(block->compress == IM_COMPRESS_NONE, "Got bad block->compress");
  HQASSERT(block->cdata == NULL, "Got bad block->cdata");
  HQASSERT(block->cbytes == -1, "Got bad block->cbytes");
  HQASSERT(block->storage  == IM_STORAGE_MEMORY, "Got bad block->storage");
  HQASSERT(ims_can_compress(ims), "This image is too small to compress");

  if (block->tbytes < IM_MIN_COMPRESSION_SIZE)
    block->compress = IM_COMPRESS_TOO_BIG;
  else {
    Bool allow_purge = ims->im_shared->page->im_purge_allowed;

    ims->im_shared->page->im_purge_allowed = FALSE;
    /* Compress the block data into a stack buffer. */
    bufsize = (int32) (block->tbytes * IM_MAX_COMPRESSION_RATIO);
    if ( im_blockcompress(ims, block, buffer, bufsize, & bufused) )
      HQASSERT(bufused <= bufsize, "Compression returned too much data");
    else
      HQASSERT(block->compress == IM_COMPRESS_TOO_BIG,
               "im_blockcompress did nothing");
    ims->im_shared->page->im_purge_allowed = allow_purge;
  }

  if (block->compress == IM_COMPRESS_TOO_BIG) {
    /* If we couldn't compress the data, either because it was too small a
     * block or because it was too random to compress, AND the block has a
     * blist, have a go at a simple copy to free the blist for other blocks.
     * Also copy short blocks at right and bottom of image where appropriate.
     */
    if (block->blist != NULL || block->tbytes <= block->abytes *
        IM_MAX_COMPRESSION_RATIO) {
      pBuffer = block->data;
      bufused = block->tbytes;
      block->compress = IM_COMPRESS_COPY;
    } else {
      return;
    }
  }

  /** \todo The actual cost of im block compression should be
      determined. For now, blocks itself. */
  block->cdata = im_dataalloc(ims->im_shared, bufused, mm_cost_easy);
  if ( block->cdata == NULL ) {
    block->compress = IM_COMPRESS_FAILED;
    return;
  }

  block->cbytes = CAST_SIGNED_TO_INT16(bufused);
  HqMemCpy(block->cdata, pBuffer, bufused);

  if ( block->abytes == IM_BLOCK_DEFAULT_SIZE )
    ims->stdblocks -= 1;
  else
    ims->extblocks -= block->abytes;

  HQASSERT(block->data, "lost image data during compression");

  if ( freeData ) {
    HQASSERT(block->blist == NULL, "Didn't expect a blist when freeing data");
    im_datafree(ims->im_shared, block->data, block->abytes );
    block->data = NULL;
  }
}

/**
 * Write the given image block to disk, and then (optionally) free the data
 */
Bool im_blocktodisk(IM_STORE *ims, IM_BLOCK *block, Bool freeData)
{
  int32 bytes, foffset = 0;
  IM_FILES *ffile = NULL;
  uint8 *data;
  int16 falign;

  HQASSERT(ims, "ims NULL");
  HQASSERT(block, "block NULL");
  HQASSERT(block->storage == IM_STORAGE_MEMORY, "Got bad block->storage");
  HQASSERT(block->data != NULL ||
            block->cdata != NULL, "Got bad block->data/block->cdata");
  HQASSERT(block->ffile == NULL, "Got bad block->ffile");
  HQASSERT(block->foffset == -1, "Got bad block->foffset");
  HQASSERT(ims_can_write2disk(ims) || (block->blist != NULL &&
            blist_wasGlobal(block->blist) && !blist_global(block->blist)),
            "This image is too small to write to disk "
            "(but is allowed when called from im_store_release");

  /* Note we could be writing out a compressed or uncompressed block. */
  if ( !im_blockisICompressed(block) ) {
    bytes = block->tbytes;
    data = block->data;
  } else {
    bytes = block->cbytes;
    data = block->cdata;
  }
  HQASSERT(data, "Actual data used NULL");

  /* Keep blocks in three files; full size (16K) blocks in one file,
   * block aligned data (512 byte) in another and all others in a
   * third file. This gives best alignment, contiguity and clustering
   * all of which help increase performance.
   */
  if (bytes == IM_BLOCK_DEFAULT_SIZE)
    falign = CAST_SIGNED_TO_INT16(IM_BLOCK_DEFAULT_SIZE);
  else if (bytes % IM_BLOCK_MIN == 0)
    falign = CAST_SIGNED_TO_INT16(IM_BLOCK_MIN);
  else
    falign = CAST_SIGNED_TO_INT16(IM_BLOCK_VAR_MIN);

  if ( !im_fileoffset(ims->im_shared->imfile_ctxt,
                      falign, bytes, &ffile, &foffset) ||
       !im_fileseek(ffile, foffset) ||
       !im_filewrite(ffile, data, bytes) )
    return FALSE;

  /* Only store this data when the write has succeeded. */
  block->ffile = ffile;
  block->foffset = foffset;

  block->storage = IM_STORAGE_DISK;

  if ( !im_blockisICompressed(block) ) {
    if ( bytes == IM_BLOCK_DEFAULT_SIZE )
      ims->stdblocks -= 1;
    else
      ims->extblocks -= block->abytes;
  }

  if ( freeData ) {
    HQASSERT(block->blist == NULL, "Didn't expect a blist when freeing data");
    if ( !im_blockisICompressed(block) ) {
      im_datafree(ims->im_shared, data, block->abytes );
      block->data = NULL;
    } else {
      im_datafree(ims->im_shared, data, block->cbytes );
      block->cdata = NULL;
    }
  }

  return TRUE;
}

void im_blockvalidate(IM_BLOCK *block, Bool checkData)
{
#if defined( ASSERT_BUILD )
  IM_BLIST *blist = block->blist;

  HQASSERT(block != NULL && block->blist != NULL &&
           blist_getblock(blist) == block &&
           get_abytes(blist) == block->abytes,
           "block and blist inconsistent");
  if ( checkData )
    HQASSERT(blist_getdata(blist, NULL) == block->data,
             "block and blist inconsistent");
#else
  UNUSED_PARAM(IM_BLOCK *, block);
  UNUSED_PARAM(Bool, checkData);
#endif
}

Bool im_blocktrim(IM_STORE *ims, int32 plane, IM_BLOCK **pblock,
                  Bool trimcolumn)
{
  IM_SHARED *im_shared = ims->im_shared;
  IM_BLOCK *block;

  HQASSERT(ims->planes[plane] != NULL, "planes null");
  HQASSERT(pblock != NULL, "pblock null");

  block = *pblock;
  if ( block == NULL )
    return FALSE; /* Already trimmed this block */
  if ( (block->flags & IM_BLOCKFLAG_NO_LONGER_NEEDED) != 0 )
    return FALSE; /* Already trimmed this block */

  if ( block->blist == NULL ) {
    /* Standard block with no blist associations */
    if ( block->data != NULL )
      im_datafree(im_shared, block->data, block->abytes);
  }
  else {
    im_blockvalidate(block, FALSE);

    if ( blist_global(block->blist) ) /* From global list - clear block */
      blist_setblock(block->blist, NULL);
    else if ( trimcolumn ) {
      /* Move blist to global list. */
      blist_unlink(block->blist, ims->planes[plane]);
      blist_setblock(block->blist, NULL);
      blist_linkGlobal(im_shared, block->blist, TRUE);
    }
    else /* May still need for subsequent rows */
      blist_setblock(block->blist, NULL);
  }

  /* Free any compressed data */
  if ( block->cdata != NULL ) {
    im_datafree(im_shared, block->cdata, block->cbytes);
  }

  /* tidy up the block */
  block->flags |= IM_BLOCKFLAG_NO_LONGER_NEEDED;
  block->blist = NULL; /* just drop any blist */
  block->storage = IM_STORAGE_NONE;
  block->compress = IM_COMPRESS_NONE;
  block->data = NULL;
  block->cdata = NULL;
  block->ffile = NULL;

  blist_checkGlobal(im_shared);
  return TRUE;
}

int32 im_blockprealloc(IM_STORE *ims, int32 planei, int32 bb, int32 bx, int32 by)
{
  IM_BLOCK *block;
  int32 bytes;

  SwOftenUnsafe();

  if ( ims->planes[planei] == NULL ) {
    if ( !im_planenew(ims, planei) )
      return -1;
  }
  block = ims->planes[planei]->blocks[bb];
  HQASSERT(block, "im_blockprealloc: somehow lost block");
  HQASSERT(block->ffile == NULL && block->compress == IM_COMPRESS_NONE,
           "im_blockprealloc: block should not be on disk or compressed");
  HQASSERT(block->data == NULL && block->abytes == 0,"Expected data == NULL");

  if ( !im_blocknew(ims, block, planei, bx, by, TRUE))
    return -1;

  HQASSERT(block->data != NULL, "Expected data != NULL");
  HQASSERT(block->storage == IM_STORAGE_MEMORY, "block should be in memory");

  /* Load how many bytes we need to copy. */
  bytes = (ims->flags & IMS_XYSWAP) ? block->ybytes : block->xbytes;
  return bytes;
}

static Bool nearly_same(uint8 *p0, uint8 *p1, int32 nbytes)
{
  int32 dd, maxd = 64;
  uint8 *end = p0 + nbytes;

  while ( p0 + 8 < end ) {
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
  }
  while ( p0 < end ) {
    dd = *p0++ - *p1++; if ( dd > maxd || dd < -maxd ) return FALSE;
  }
  return TRUE;
}

static void im_set_row_repeats(IM_STORE *ims, IM_BLOCK *block, int32 y)
{
  int32 ye = y + block->ysize, xbytes = block->xbytes;
  uint8 *data, *data_next;
  Bool near = FALSE; /* Do test for nearly-the-same rows */

  HQASSERT(y < ims->obbox.y2 - ims->obbox.y1 + 1, "y is out of range");

  if ( (ims->flags & IMS_ROWREPEATS_NEAR) != 0 &&
       (ims->flags & IMS_DOWNSAMPLED) == 0 )
    near = TRUE;

  if ( ims->row_repeats == NULL ||
       (ims->flags & IMS_RECYCLED) != 0 || /* row_repeats set already */
       block->storage == IM_STORAGE_UNIFORM_VARIANT )
    return;

  data = block->data;
  data_next = data + xbytes;
  for ( ++y; y < ye; ++y ) {
    if ( near ) {
      if ( BITVECTOR_IS_SET(ims->row_repeats, y) ) {
        if ( !nearly_same(data, data_next, xbytes) )
          BITVECTOR_CLEAR(ims->row_repeats, y);
      }
    } else {
      if ( BITVECTOR_IS_SET(ims->row_repeats, y) &&
           HqMemCmp(data, xbytes, data_next, xbytes) != 0 )
        BITVECTOR_CLEAR(ims->row_repeats, y);
    }
    data = data_next;
    data_next += xbytes;
  }
}

/**
 * Write the given data into the specified image block
 */
int32 im_blockwrite(IM_STORE *ims, int32 planei, int32 bb, int32 bx, int32 by,
                    uint8 *buf)
{
  IM_BLOCK *block;
  int32 bytes;

  SwOftenUnsafe();

  if ( ims->planes[ planei ] == NULL ) {
    if ( !im_planenew(ims, planei) )
      return -1;
  }
  block = ims->planes[planei]->blocks[bb];
  HQASSERT(block, "somehow lost block");
  HQASSERT(!im_blockcomplete(block), "block must not be complete");
  HQASSERT(block->ffile == NULL && block->compress == IM_COMPRESS_NONE,
           "block should not be on disk or compressed");
  if ( block->data == NULL ) {
    if ( !im_blocknew(ims, block, planei, bx, by, FALSE ))
      return -1;
  }
  HQASSERT(block->data, "somehow lost data");
  HQASSERT(block->storage == IM_STORAGE_MEMORY, "block should be in memory");

  /* Load how many bytes we need to copy. */
  bytes = (ims->flags & IMS_XYSWAP) ? block->ybytes : block->xbytes;

  HqMemCpy(block->data + block->sbytes, buf, bytes);

  if ( ims->bpp == 32 ) {
    uint8 *ptr = (uint8 *)block->data + block->sbytes;
    int32 i;

    /*
     * Initial test code to get 32bit images going.
     * Test data is byte-swapped. Not quite sure where it should
     * be un-swapped. Do it here for now.
     */
    /**
     * \todo BMJ 12-Sep-07 :  code is work in progress
     */
    HQASSERT(bytes % 4 == 0, "32bit image problem");

    for ( i=0; i < bytes; i+= 4 ) {
      uint8 swap;

      swap = ptr[0];
      ptr[0] = ptr[3];
      ptr[3] = swap;
      swap = ptr[1];
      ptr[1] = ptr[2];
      ptr[2] = swap;
      ptr += 4;
    }
  }

  block->sbytes = CAST_SIGNED_TO_INT16(block->sbytes + bytes);
  HQASSERT(block->sbytes <= block->tbytes, "Wrote too many bytes into block");
  HQASSERT(block->sbytes <= block->rbytes,
           "Wrote too many bytes into xyswapd block");
  if ( block->sbytes == block->rbytes ) /* block write completed */ {
    block->flags |= IM_BLOCKFLAG_WRITE_COMPLETE;

    /* Since we've written more data to this store, there is a possibility
     * that it can be compressed.
     */
    ims_set_action(ims, TRUE);

    if ( ims->flags & IMS_XYSWAP )
      im_blockxyswap(ims, block);

    im_set_row_repeats(ims, block, by * ims->blockHeight);
  }
  return bytes;
}

static Bool im_blockGetMoveable(IM_STORE *ims, IM_BLOCK *block,
                                int32 plane, int32 bx, Bool rendering);

Bool im_blockaddr(IM_STORE *ims, int32 plane, int32 bb, int32 bx,
                  int32 x, int32 y, uint8 **rbuf, int32 *rpixels)
{
  int32 xi, yi;
  IM_BLOCK *block;

  HQASSERT(bb >= 0 && bb < ims->nblocks, "block out of range");

  HQASSERT(ims->planes[plane], "Did not allocate plane");
  HQASSERT(ims->planes[plane]->blocks, "Should have blocks");
  block = ims->planes[ plane ]->blocks[ bb ];
  HQASSERT(block != NULL, "im_blockaddr: block has been trimmed away");

  /* Moveable block handling */
  if ( im_blockisIMoveable(block) ) {
    if (!im_blockGetMoveable(ims, block, plane, bx, TRUE))
      return FALSE;
  }
  HQASSERT(block->data, "Trying to read but lost data");

  xi = im_index_xi(ims, x);
  yi = im_index_yi(ims, y);
  *rpixels = block->xbytes - xi;

  if (block->storage == IM_STORAGE_UNIFORM_VARIANT)
    *rbuf = block->data;
  else
    *rbuf = block->data + block->xbytes * yi + xi;

  return TRUE;
}

void im_blockfree(IM_STORE *ims, int32 iplane)
{
  IM_SHARED *im_shared = ims->im_shared;
  IM_PLANE *plane = ims->planes[iplane];
  int32 iblock;

  for ( iblock = 0; iblock < ims->nblocks; ++iblock ) {
    IM_BLOCK *block = plane->blocks[iblock];

    if ( block != NULL ) { /* May have been trimmed */
      if ( block->blist != NULL )
        blist_setblock(block->blist, NULL);
      else {
        if ( block->data != NULL )
          im_datafree(im_shared, block->data, block->abytes);

        if ( block->cdata != NULL ) /* Free any compressed data */
          im_datafree(im_shared, block->cdata, block->cbytes);
      }
      dl_free(im_shared->page->dlpools, (mm_addr_t)block,
              sizeof(IM_BLOCK), MM_ALLOC_CLASS_IMAGE_DATA);
    }
  }
  /* Now move the image plane blists to the global list. */
  while ( ims->planes[iplane]->blisth != NULL ) {
    IM_BLIST *blist = ims->planes[iplane]->blisth;

    blist_unlink(blist, ims->planes[iplane]);
    blist_linkGlobal(im_shared, blist, TRUE);
  }
  /* And lastly free the blocks structure */
  dl_free(im_shared->page->dlpools, (mm_addr_t)(plane->blocks),
          ims->nblocks * sizeof(IM_BLOCK *), MM_ALLOC_CLASS_IMAGE_DATA);
}

static Bool im_blockGetMoveable(IM_STORE *ims, IM_BLOCK *block,
                                int32 plane, int32 bx, Bool rendering)
{
  IM_BLIST *blist;
  Bool announce_block = FALSE;
  im_context_t *im_context = CoreContext.im_context ;

  HQASSERT(ims, "ims NULL");
  HQASSERT(plane >= 0, "plane should only be >= 0");
  HQASSERT(plane < ims->nplanes, "plane out of range");
  HQASSERT(ims->planes[ plane ], "Did not allocate plane");
  HQASSERT(block->storage == IM_STORAGE_MEMORY ||
           block->storage == IM_STORAGE_DISK ||
           block->storage == IM_STORAGE_UNIFORM_VARIANT,
           "Unexpected block storage type");

  im_blockForbid();
  HQASSERT(im_context->blockinuse == NULL, "already have a block in use");
  im_context->blockinuse = block;
  block->refcount++;

  if ( ( block->flags & IM_BLOCKFLAG_IS_LOADING ) != 0 ) {
    do {
      HQASSERT(block->refcount > 1,
               "Block is_loading but there's no-one to load it");
      multi_condvar_wait(&im_load_condvar);
      if ( !interrupts_clear(allow_interrupt) ) {
        im_blockAllow();
        return report_interrupt(allow_interrupt);
      }
    } while ( ( block->flags & IM_BLOCKFLAG_IS_LOADING ) != 0 );
  }

  if ( block->data == NULL ) {
    block->flags |= IM_BLOCKFLAG_IS_LOADING;
    for (;; ) {
      if ( !im_blockrealloc(ims, block, plane, bx, rendering) ) {
        im_blockAllow();
        return FALSE;
      }
      if ( block->data != NULL )
        break;
      /* Allocation failed.  Sleep waiting for a block to become free
       * and try again. */
      multi_condvar_wait(&im_get_condvar);
      if ( !interrupts_clear(allow_interrupt) ) {
        im_blockAllow();
        return report_interrupt(allow_interrupt);
      }
    }
    /* We've marked the block is_loading, so now no-one will frob it until
     * we clear that mark. */
    im_blockAllow();

    /* Data can either be:
     * a) compressed in memory.
     * b) compressed on disk.
     * c) non-compressed on disk.
     */

    if ( block->storage == IM_STORAGE_MEMORY ) {
      HQASSERT(im_blockisICompressed( block ),
               "block should either be compressed and/or on disk");
      if ( ! im_blockdecompress( ims, block, block->cdata ) ) {
        HQFAIL( "Somehow failed to decompress memory block" );
        return FALSE;
      }
    }
    else if ( block->storage == IM_STORAGE_UNIFORM_VARIANT ) {
      /* Expand the uniform color value into the data referenced by
       * im_storeread
       */
      uint32 npixels = block->xbytes, i ;

      if ( ims->bpp == 16 ) {
        uint16 *data = (uint16*)block->data ;

        HQASSERT((block->xbytes & 1) == 0, "xbytes must be even for 16-bit image data") ;
        npixels >>= 1 ;

        for (i = 0; i < npixels; i++)
          data[i] = block->uniformColor;
      } else {
        uint8 uniformColor8 = CAST_UNSIGNED_TO_UINT8(block->uniformColor) ;

        for (i = 0; i < npixels; i++)
          block->data[i] = uniformColor8;
      }
    }
    else {
      if ( !im_fileseek(block->ffile, block->foffset) )
        return FALSE;
      if ( !im_blockisICompressed( block ) ) {
        if ( !im_fileread(block->ffile, block->data, block->tbytes) )
          return FALSE;
      } else {
        /* Try and use a stack buffer if possible. It should be because
         * the allocation is the max size of the original data.
         */
        uint8 buffer[ IM_BLOCK_DEFAULT_SIZE ];
        if ( !im_fileread(block->ffile, buffer, block->cbytes) )
          return FALSE;
        if ( ! im_blockdecompress( ims, block, buffer ) )
          return FALSE;
      }
    }
    /* OK, claim the lock to clear the is_loading flag. */
    im_blockForbid();
    block->flags &= ~IM_BLOCKFLAG_IS_LOADING;
    if ( block->refcount > 1 )
      /* Someone else cares that we got this */
      announce_block = TRUE;
  }

  blist = block->blist;
  if ( blist ) {
    if ( blist_global(blist) ) {
      /* Go take off the global list and add to the local list. */
      blist_unlinkGlobal(ims->im_shared, blist);
      blist_link(blist, ims->planes[plane]);
    }
  }
  blist_checkGlobal(ims->im_shared);

  if ( announce_block ) {
    /* Multiple threads might be waiting for different blocks, so broadcast. */
    multi_condvar_broadcast(&im_load_condvar);
  }
  /* Frobbing done, we can release the lock now.*/
  im_blockAllow();

  return TRUE;
}

void im_blockreadrelease(corecontext_t *context)
{
  im_context_t *im_context = context->im_context ;
  IM_BLOCK *block ;

  if ( (block = im_context->blockinuse) != NULL ) {
    im_blockForbid();
    block->refcount--;
    im_context->blockinuse = NULL;
    /* And wake up anyone waiting for a block to be freed */
    /* Multiple threads might be waiting for different sizes, so broadcast. */
    multi_condvar_broadcast(&im_get_condvar);
    im_blockAllow();
  }
}

/**
 * Purge a block either by compressing it or writing it to disk
 */
int32 im_blockpurge(IM_STORE *ims, IM_BLOCK *block, Bool from_blist)
{
  Bool ok = TRUE;
  int32 purgedBlocks = 0;

  if ( block != NULL && block->storage == IM_STORAGE_MEMORY &&
       block->compress == IM_COMPRESS_NONE ) {
    if ( im_blockUniform(ims, block, block->blist == NULL) )
      return 1;
    if ( ims->action == IM_ACTION_COMPRESSION ) {
      if ( block->compress == IM_COMPRESS_NONE ) {
        im_blocktocomp(ims, block, block->blist == NULL);
        HQASSERT(block->compress != IM_COMPRESS_NONE,
                 "image block compression did nothing");
        ok = (block->compress != IM_COMPRESS_FAILED);
        if ( ok && im_blockisICompressed(block) )
          ++purgedBlocks;

        if ( from_blist && !im_blockisICompressed(block) ) {
          /* If we have a blist associated with a block that didn't
             compress then give it one more chance by purging to disk.
             But only if we are allowed to spill to disk ! */
          if ( ims_can_write2disk(ims) ) {
            ok = im_blocktodisk(ims, block, FALSE);
            if ( ok && block->storage == IM_STORAGE_DISK )
              ++purgedBlocks;
          }
        }
      }
    } else {
      ok = im_blocktodisk(ims, block, block->blist == NULL);
      if ( ok && block->storage == IM_STORAGE_DISK )
        ++purgedBlocks;
    }
  }
  return ok ? purgedBlocks : -1;
}

Bool im_blockclose(IM_STORE *ims, IM_PLANE *plane, Bool *incomplete)
{
  int32 j;

  for ( j = 0; j < ims->nblocks; ++j ) {
    IM_BLOCK *block = plane->blocks[j];

    /* If the block read never completed, do the work of finishing it off */
    if ( block != NULL ) { /* May have been trimmed */
      if ( !im_blockcomplete(block) ) {
        if ( block->data ) {
          *incomplete = TRUE ;

          /* Zero the remainder of the data.  Image adjustment may need
             to look at the whole block and can't be expected to know
             what's uninitialised. */
          HqMemZero(block->data + block->sbytes,
                    block->tbytes - block->sbytes);

          if ( ims->flags & IMS_XYSWAP )
            im_blockxyswap( ims , block );

          /* Just pretend that it completed... */
          block->flags |= IM_BLOCKFLAG_WRITE_COMPLETE;
          block->sbytes = block->tbytes ;

          im_set_row_repeats(ims, block, (j / ims->xblock) * ims->blockHeight);

          ims_set_action(ims, TRUE);
        }
        else {
          /* image striping might leave blocks without data. */
          *incomplete = TRUE;
          block->flags |= IM_BLOCKFLAG_WRITE_COMPLETE;
        }
      }
    }
  }

  if ( plane->blisth != NULL )
    blist_purge2disk(ims, plane);

  /* xyswapped images have special requirements because the blist associations
   * are required to be different in the renderer vs. the interpreter.
   */
  if ( ims->flags & IMS_XYSWAP ) {
    if ( plane->blisth != NULL )
      blist_xyswap(ims, plane);
  }
  return TRUE;
}

/**
 * Attempt to purge some image blocks for the given plane
 *
 * \todo BMJ 12-Sep-07 :  This code was duplicated for use by
 * image filtering. Have merged them back to a single copy, but
 * may have affected image filtering. Needs to re-examined/tested
 */
Bool im_blockpurgeplane(IM_STORE *ims, IM_PLANE *plane,
                        int32 ty, int32 *purgedBlocks)
{
  int32 tx, start_tx, end_tx, inc_tx;
  Bool rowComplete = TRUE;

  HQASSERT(plane->blocks != NULL , "Allocated plane but not blocks");

  /* Adjust the order in which blocks are purged to disk within a row.
     Doing so gives good locality to the image store file, and improves
     rendering time considerably. */
  if ( ims->flags & IMS_XFLIP ) {
    start_tx = ims->xblock - 1;
    end_tx = -1;
    inc_tx = -1;
  } else {
    start_tx = 0;
    end_tx = ims->xblock;
    inc_tx = 1;
  }

  for ( tx = start_tx; tx != end_tx; tx += inc_tx ) {
    IM_BLOCK *block = plane->blocks[tx + ty * ims->xblock];

    if ( block != NULL ) {
      if ( !im_blockcomplete(block) || block->storage == IM_STORAGE_NONE ) {
        if ( ims->openForWriting ) /* if still changing, redo row */
          rowComplete = FALSE;
      } else if ( block->storage == IM_STORAGE_MEMORY && block->refcount == 0 ) {
        /* Found a purgeable block.  Either release the blist to make it usable
           or free the block memory. */
        int32 res = im_blockpurge(ims, block, FALSE);
        if ( res == -1 )
          return FALSE;
        HQASSERT(res == 0 || res == 1, "Bad return value from im_blockpurge");
        *purgedBlocks += res;
      }
    }
  }

  /* Set the optimisers for use elsewhere */
  if ( rowComplete ) {
    if ( ims->action == IM_ACTION_COMPRESSION)
      plane->yCompressed++;
    else
      plane->yPurged++;
  }
  HQASSERT(plane->yCompressed <= ims->yblock, "yCompressed too large");
  HQASSERT(plane->yPurged <= ims->yblock, "yPurged too large");

  return TRUE;
}

void im_blockclear(IM_BLIST *blist, int32 abytes, Bool xxx)
{
  IM_BLOCK *block = blist_getblock(blist);

  UNUSED_PARAM(int32, abytes);
  UNUSED_PARAM(Bool, xxx);

#if 0
  /** \todo BMJ 29-Aug-07 :  sort this out */
  if ( 0 && xxx ) {
    /* Not yet implemented/tested/required? */
    /* this is a heavy weight filter, we want to save to disk, instead of
       throwing away, and regenerating later */

    /* First check to see if the data is uniform which is as good as
     * it gets.
     */
    if ( im_blockUniform(ims, block , FALSE) ) {
      block->blist = NULL;
      block->data = NULL;
    }
    if ( block->data != NULL ) {
      if ( im_blocktodisk(ims, block, FALSE) ) {
        block->blist = NULL;
        block->data = NULL;
      }
    }
    if (block->data == NULL) {
      /* we managed to get rid of the data somehow */
      /* break block/blist association */
      block->blist = NULL;
      blist_setblock(blist, NULL);
    }
  }
  else {}
#endif

  if ( block == NULL )
    return;

  HQASSERT(get_abytes(blist) >= abytes, "blist not big enough");
  if ( !xxx ) /** \todo BMJ 29-Aug-07 :  not sure why this test is needed */
    im_blockvalidate(block, TRUE);
  HQASSERT(block->storage != IM_STORAGE_NONE, "block is always allocated");
  HQASSERT(block->sbytes == block->tbytes,
           "blist block is not actually complete");
  HQASSERT(im_blockcomplete(block), "blist block is not actually complete");
  HQASSERT(im_blockisusable(block), "block should be usable for nulling");

  block->data = NULL;
  block->blist = NULL;
  if ( block->storage == IM_STORAGE_MEMORY && !im_blockisICompressed(block) )
      block->storage = IM_STORAGE_NONE;
  blist_setblock(blist, NULL);
}

void im_blockrelease(IM_STORE *ims, IM_BLIST *blist, int32 nullblock,
                     IM_PLANE *plane, int32 band)
{
  IM_SHARED *im_shared = ims->im_shared;
  IM_BLOCK *block = blist_getblock(blist);

  if ( blist_wasGlobal(blist) ) {
    /* If we are called because preallocation of memory failed, then frig
     * it to make things look ok to the renderer.
     */
    if ( nullblock || (block != NULL && block->sbytes == 0 ) ) {
      if ( block != NULL ) {
        HQASSERT(block->refcount == 0, "block should be usable for nulling");
        block->data = NULL;
        if ( block->storage == IM_STORAGE_MEMORY &&
             !im_blockisICompressed(block) )
          block->storage = IM_STORAGE_NONE;
        block->blist = NULL;
        blist_setblock(blist, NULL);
        /* This line avoids an assert in im_blockalloc */
        block->abytes = 0;
      }
      block = NULL;
    }

    if (block == NULL || im_blockisusable(block)) {
      if (block != NULL) {
        HQASSERT(im_blockcomplete(block),
          "Need to release part full blocks to the global list");
      }
      blist_unlink(blist, plane);
      blist_linkGlobal(im_shared, blist,
                       (band == ims->band || band == DL_LASTBAND));
    }
    else
      HQASSERT(block == NULL || im_blockisIMoveable(block)
               || block->compress == IM_COMPRESS_FAILED
               || block->compress == IM_COMPRESS_TOO_BIG,
               "Global blist attached to non-moveable block");
  }
}

/**
 * Debug function returning a char indicating the storage state of the
 * given block
 */
static char im_storage(IM_BLOCK *block)
{
  if ( block == NULL )
    return '.';

  switch ( block->storage ) {
    case IM_STORAGE_NONE:
      return '0';
    case IM_STORAGE_MEMORY: {
      switch ( block->compress ) {
        case IM_COMPRESS_NONE:
          return 'm';
        case IM_COMPRESS_TOO_BIG:
          return '+';
        case IM_COMPRESS_FAILED:
          return '!';
        case IM_COMPRESS_LZW:
          return 'l';
        case IM_COMPRESS_CCITT:
          return 'c';
        case IM_COMPRESS_FLATE:
          return 'f';
        case IM_COMPRESS_B32:
          return '3';
        case IM_COMPRESS_COPY:
          return 'y';
        default:
          return '?';
      }
    }
    case IM_STORAGE_DISK:
      return 'd';
    case IM_STORAGE_UNIFORM_BLOCK:
      return 'u';
    case IM_STORAGE_UNIFORM_VARIANT:
      return 'v';
    default:
      return '?';
  }
}

/**
 * Debug function to create a pretty ascii graphic printout of the
 * state of an image block
 *
 * \param[in] ims       The image store object
 * \param[in] plane     The plane to make a debug print of
 */
void im_blockreport(IM_STORE *ims, IM_PLANE *plane)
{
  int32 tx, ty;
  int32 xmax = ims->xblock;
  uint8 string[2*400+10];

  if ( xmax > 400 )
    xmax = 400;

  for ( ty = 0; ty < ims->yblock; ++ty ) {
    for ( tx = 0; tx < xmax; ++tx ) {
      IM_BLOCK *block = plane->blocks[tx + ty * ims->xblock];

      string[tx*2]     = im_storage(block);
      if ( block != NULL && block->data != NULL && block->blist )
        string[tx*2 + 1] = im_blockisusable(block) ? 'b' : 'B';
      else
        string[tx*2 + 1] = ' ';
    }
    if ( ims->xblock > xmax ) {
      string[tx*2]   = ' ';
      string[tx*2+1] = '.';
      string[tx*2+2] = '.';
      string[tx*2+3] = '.';
      tx += 2;
    }
    string[tx*2]   = '\n';
    string[tx*2+1] = '\0';
    monitorf((uint8 *)"%s", string);
  }
}

void im_blocknull(IM_BLOCK *block)
{
  HQASSERT(im_blockisusable(block), "block should be usable for nulling");
  block->data = NULL;
  block->blist = NULL;
}

#if defined( ASSERT_BUILD )
/**
 * Return if the given block could be purged if we wanted to.
 */
static Bool can_purge_block(IM_STORE *ims, IM_BLOCK *block)
{
  if ( block == NULL )
    return FALSE;
  if ( !im_blockcomplete(block) )
    return FALSE;
  if ( im_blockisIMoveable(block) )
    return FALSE;
  if ( block->storage == IM_STORAGE_NONE ||
       block->storage == IM_STORAGE_UNIFORM_BLOCK )
    return FALSE;

  if ( ims_can_compress(ims) && !(block->compress == IM_COMPRESS_FAILED ||
                                  block->compress == IM_COMPRESS_TOO_BIG ) )
    return TRUE;
  else
    return ims_can_write2disk(ims);
}
#endif /* ASSERT_BUILD */

void im_blockcheckNoPurgeable(IM_STORE *ims, IM_PLANE *plane, int32 ty)
{
#if defined( ASSERT_BUILD )
  int32 tx;

  HQASSERT(plane->blocks != NULL, "Allocated plane but not blocks");

  for ( tx = 0; tx < ims->xblock; ++tx ) {
    IM_BLOCK *block = plane->blocks[tx + ty * ims->xblock];

    HQASSERT(!can_purge_block(ims, block), "Unexpected purgeable block");
  }
#else
  UNUSED_PARAM(IM_STORE *, ims);
  UNUSED_PARAM(IM_PLANE *, plane);
  UNUSED_PARAM(int32, ty);
#endif
}

void im_blockcheckrelease(IM_BLOCK *block)
{
  UNUSED_PARAM(IM_BLOCK *, block);
  HQASSERT(block == NULL || im_blockisIMoveable(block) ||
           block->compress == IM_COMPRESS_FAILED ||
           block->compress == IM_COMPRESS_TOO_BIG,
           "Global blist attached to non-moveable block");
}

IM_BLOCK **im_blocknalloc(IM_SHARED *im_shared, int32 nblocks)
{
  IM_BLOCK **blocks;

  blocks = dl_alloc(im_shared->page->dlpools, nblocks * sizeof(IM_BLOCK *),
                    MM_ALLOC_CLASS_IMAGE_DATA);
  if (blocks == NULL)
    (void) error_handler(VMERROR);

  return blocks;
}

void im_blockerrfree(IM_STORE *ims, IM_PLANE *plane, int32 planei)
{
  int32 i;

  for ( i = 0; i < ims->nblocks; ++i ) {
    if ( plane->blocks[i] != NULL ) {
      dl_free(ims->im_shared->page->dlpools, (mm_addr_t)plane->blocks[i],
              sizeof(IM_BLOCK), MM_ALLOC_CLASS_IMAGE_DATA);
      plane->blocks[i] = NULL;
    }
  }
  dl_free(ims->im_shared->page->dlpools, (mm_addr_t)plane->blocks,
          ims->nblocks * sizeof(IM_BLOCK *), MM_ALLOC_CLASS_IMAGE_DATA);
  plane->blocks = NULL;

  dl_free(ims->im_shared->page->dlpools, (mm_addr_t)plane, sizeof(IM_PLANE),
          MM_ALLOC_CLASS_IMAGE_DATA);
  ims->planes[planei] = NULL;
}

/**
 * im_blockexists should not be called from the renderer, because of the
 * last argument to im_blockGetMoveable.
 */
Bool im_blockexists(IM_STORE *ims, int32 planei, int32 bb, int32 x,
                    Bool checkComplete)
{
  IM_BLOCK *block;

  if ( ims->planes[planei] == NULL || ims->planes[planei]->blocks == NULL )
    return FALSE;

  block = ims->planes[planei]->blocks[bb];
  if ( block == NULL)
    return FALSE;
  if ( checkComplete ) {
      if (!(im_blockcomplete(block) &&
          (( block->flags & IM_BLOCKFLAG_NO_LONGER_NEEDED) ||
           ( block->storage != IM_STORAGE_NONE))))
        return FALSE;
  }
  else {
    /* Moveable block handling */
    if ( im_blockisIMoveable(block) && (block->data == NULL) &&
         (block->storage != 0) ) {
      if (!im_blockGetMoveable(ims, block, planei, x, FALSE /* rendering */))
        return FALSE;
    }
    if (( block->data == NULL) || (block->sbytes == 0))
      return FALSE;
  }
  return TRUE;
}

/**
 * im_blockgetdata should not be called from the renderer, because of the
 * last argument to im_blockGetMoveable.
 */
Bool im_blockgetdata(IM_STORE *ims, int32 planei, int32 bx, int32 bb,
                     uint8 **rbuf , uint32 *rbytes)
{
  IM_BLOCK *block = ims->planes[planei]->blocks[bb];

  HQASSERT(block != NULL, "block has been trimmed away");

  /* Moveable block handling */
  if ( im_blockisIMoveable(block) && block->data == NULL &&
       block->storage != 0 ) {
    if ( !im_blockGetMoveable(ims, block, planei, bx, FALSE /* rendering */) )
      return FALSE;
  }
  /* if the block is has a global blist we need to grab it back */
  if ( block->blist != NULL) {
    if ( blist_global(block->blist) ) {
      /* Go take off the global list and add to the local list. */
      blist_unlinkGlobal(ims->im_shared, block->blist);
      blist_link(block->blist, ims->planes[planei]);
    }
  }
  HQASSERT(block->data , "Trying to read but lost data");
  block->refcount++;
  *rbytes = block->tbytes;
  *rbuf = block->data;

  return TRUE;
}

Bool im_blockblistalloc(IM_STORE *ims, int32 planei,
                        int32 bb, int32 bx, int32 by, Bool reuse_blists)
{
  IM_BLOCK *block = ims->planes[planei]->blocks[bb];

  HQASSERT(block , "somehow lost block");
  if (( block->data == NULL ) && (block->storage == IM_STORAGE_NONE)) {
    block->flags &= ~IM_BLOCKFLAG_WRITE_COMPLETE;
    block->abytes = 0;
  }
  /* Moveable block handling */
  if ( im_blockisIMoveable(block) && (block->data == NULL) &&
       (block->storage != 0) )
    if (!im_blockGetMoveable(ims, block, planei, bx, FALSE))
      return FALSE;
  HQASSERT(block , "Somehow lost block");

  if ( block->data == NULL ) {
    IM_BLIST *blist = NULL;

    /* try to find a blist we can use */
    if ( reuse_blists ) {
      /** \todo pekka 2013-10-23 Reconsider use of desperate arg. */
      blist = blist_find(ims, ims->abytes, planei, bx, TRUE, FALSE);
      if (blist != NULL) {
        HQASSERT(blist_getblock(blist) == NULL,"block not NULL");
        block_from_blist(block, blist, FALSE);
      }
    }
    if ( block->data == NULL )
      if ( !im_blocknew(ims, block, planei, bx, by, FALSE))
        return FALSE;
  }
  HQASSERT(block->storage == IM_STORAGE_MEMORY, "block should be in memory");
  HQASSERT(block->data, "somehow lost data");
  block->flags &= ~IM_BLOCKFLAG_WRITE_COMPLETE;
  HQASSERT(block->refcount == 0, "somehow refcount !=0");

  return TRUE;
}

void im_blocklock(IM_BLOCK *block)
{
  HQASSERT(block != NULL, "block has been trimmed away");
  HQASSERT(block->refcount < 255, "block refcount suspiciously large");

  block->refcount++;
}

void im_blockunlock(IM_BLOCK *block, Bool disposable)
{
  HQASSERT(block != NULL, "block has been trimmed away");
  HQASSERT(block->refcount > 0 , "refcount should be > 0");

  block->flags |= IM_BLOCKFLAG_WRITE_COMPLETE;
  if ( disposable )
    block->flags |= IM_BLOCKFLAG_NO_LONGER_NEEDED;
  block->sbytes = block->tbytes;
  if (block->refcount > 0)
    block->refcount--;
}

void im_block_blist_check(IM_BLOCK *block, IM_BLIST *blist)
{
  UNUSED_PARAM(IM_BLIST *, blist);

  HQASSERT(block->blist == blist,
    "block doesn't point back to blist" );
  HQASSERT(im_blockisIMoveable(block),
           "Non-moveable block on global blist");
  if ( block->storage == IM_STORAGE_UNIFORM_VARIANT )
    /* Nothing particular to assert */
    EMPTY_STATEMENT();
  else if ( block->storage == IM_STORAGE_DISK )
    HQASSERT(block->ffile != NULL,
      "Block on disk but file pointer NULL");
  else
    HQASSERT(block->cdata != NULL,
             "Block not on disk but compressed data absent");
  HQASSERT(block->data != NULL,
    "NULL data on global blist");
  HQASSERT(im_blockcomplete(block), "Incomplete block on global list");
  HQASSERT(block->tbytes <= get_abytes(blist),
    "Inconsistent block size on global blist");
  HQASSERT(block->abytes <= get_abytes(blist),
    "Block on blist is too big");
  HQASSERT(block->refcount == 0, "Block referenced");
  HQASSERT((block->flags & IM_BLOCKFLAG_IS_LOADING) == 0,
           "Block has flags set");
}


#define MAX_COLUMN_CHECK (50)

void im_block_acheck(IM_BLOCK *block, IM_BLIST *blist)
{
  UNUSED_PARAM(IM_BLIST *, blist);

  HQASSERT(block->blist == blist, "block doesn't point back to blist");

  if ( block->storage == IM_STORAGE_DISK )
    HQASSERT(block->ffile != NULL, "Block on disk but file pointer NULL");
  else if ( im_blockisICompressed( block ) )
    HQASSERT(block->cdata != NULL,
      "Block not on disk but compressed data absent");
  HQASSERT(block->data != NULL, "NULL data on plane blist");
  HQASSERT(im_blockcomplete(block), "Incomplete block on list");
  HQASSERT(block->tbytes <= get_abytes(blist),
    "Inconsistent block size on plane blist");
  HQASSERT(block->abytes <= get_abytes(blist), "Block on blist is too big");
  HQASSERT(block->refcount == 0, "Block referenced");
  HQASSERT((block->flags & IM_BLOCKFLAG_IS_LOADING) == 0, "Block flags set");
}

void im_block_pre_render_assertions(IM_SHARED *im_shared)
{
#if defined( ASSERT_BUILD )
  IMAGEOBJECT *im_list;
  Bool global_shared = FALSE;
  int32 column_check_array[MAX_COLUMN_CHECK+1];
  int32 *column_check = &column_check_array[1]; /* so can index with -1 */
  IM_STORE_NODE *node;

  HQASSERT(im_shared, "No DL image file state");

  /* If there's just one valid block on the shared blist then
   * in theory everything can use that.  It's not very efficient,
   * but it shouldn't deadlock */

  for ( node = im_shared->im_list; node != NULL ; node = node->next )
    if ( node->blistHead != NULL ) {
      blist_pre_render_assertions(node->blistHead);
      global_shared = TRUE;
    }

  for ( im_list = im_shared->page->im_list; im_list != NULL; im_list = im_list->next ) {
    IM_STORE *ims = im_list->ims;

    if ( ims != NULL ) {
      int32 i, j;

      if ( ims->openForWriting )
        continue; /* unfinished at a partial paint, not rendered */
      for ( j = -1; j < ims->xblock && j < MAX_COLUMN_CHECK; j++ )
        column_check[j] = 0;

      for ( i = 0; i < ims->nplanes; i++ ) {
        IM_PLANE *imp = ims->planes[i];

        column_check[-1] = 0;

        /* NULL image planes are possible when recombining (the planes array
           is sparsely populated and indexed by colorant indices). */
        if ( imp ) {
          int32 yblock, bx, by;

          if ( imp->blisth != NULL )
            blist_ccheck(imp->blisth, column_check, MAX_COLUMN_CHECK);

          yblock = ims->yblock;

          for ( bx = 0; bx < ims->xblock; bx++ ) {
            int32 need_moveable = FALSE, found_moveable = FALSE;
            for ( by = 0; by < yblock; by++ ) {
              IM_BLOCK *block = imp->blocks[bx + by * ims->xblock];

              if ( block != NULL ) {
                HQASSERT(im_blockcomplete(block), "Incomplete block");
                if ( im_blockisIMoveable( block ) ) {
                  if ( block->storage == IM_STORAGE_DISK )
                    HQASSERT(block->ffile != NULL,
                      "Block on disk but file pointer NULL");
                  else if ( im_blockisICompressed( block ) )
                    HQASSERT(block->cdata != NULL,
                      "Block not on disk but compressed data absent");
                }
                if ( block->storage == IM_STORAGE_UNIFORM_BLOCK ) {
                  HQASSERT(block->data == NULL,
                           "uniform block shouldn't have data");
                  HQASSERT(block->cdata == NULL,
                           "uniform block shouldn't have cdata" );
                }
                else if ( block->storage == IM_STORAGE_UNIFORM_VARIANT ) {
                  /* NB. This storage type can have data */
                  HQASSERT(block->cdata == NULL,
                           "uniform block shouldn't have cdata");
                }
                else if ( block->storage == IM_STORAGE_NONE ) {
                  /* ignore blocks that have been trimmed */
                }
                else if ( block->data == NULL ) {
                  HQASSERT(im_blockisIMoveable(block),
                      "non-moveable block has no data");
                  need_moveable = TRUE;
                }
                else if ( block->blist != NULL) {
                  if ( im_blockisIMoveable(block) )
                    found_moveable = TRUE;

                  HQASSERT(blist_getblock(block->blist) == block,
                           "blist gets block wrong");
                  HQASSERT(blist_getbx(block->blist) == bx ||
                           blist_getbx(block->blist) == -1,
                           "blist gets column wrong");
                  HQASSERT(global_shared || !blist_global(block->blist),
                           "rogue global blist found in image");
                }
                else
                  HQASSERT(!im_blockisIMoveable(block),
                    "moveable block has data but isn't on blist");
              }
            }
            if ( bx < MAX_COLUMN_CHECK && column_check[bx] == 0) {
              if ( column_check[-1] > 0)
                column_check[-1]--;
              else
                HQASSERT(global_shared || !need_moveable || found_moveable,
                  "Column needs moveable block but hasn't got one");
            }
          }
        }
      }
    }
  }
#else /* !ASSERT_BUILD  */
  UNUSED_PARAM(IM_SHARED*, im_shared);
#endif
}

void im_blockblistinit(IM_BLIST *blist, Bool doFlags)
{
  IM_BLOCK *block = blist_getblock(blist);
  int16 abytes;

  block->blist     = blist;
  if ( doFlags )
    block->flags   = IM_BLOCKFLAG_WRITE_COMPLETE;
  block->storage   = IM_STORAGE_MEMORY;
  block->data      = blist_getdata(blist, &abytes);
  block->abytes    = abytes;
}

int32 im_blockwidth(IM_BLOCK *block)
{
  return block->xsize;
}

int32 im_blockheight(IM_BLOCK *block)
{
  return block->ysize;
}

uint16 im_blockUniformColor(IM_BLOCK *block)
{
  return block->uniformColor ;
}

uint8 *im_blockdata(IM_BLOCK *block, int32 y)
{
  return block->data + y * block->xbytes;
}

uint8 im_blockstorage(IM_BLOCK *block)
{
  return block->storage;
}

IM_BLIST *im_blockblist(IM_BLOCK *block)
{
  return block->blist;
}

void im_blockreopen(IM_BLOCK *block)
{
  if ( block->storage == IM_STORAGE_UNIFORM_VARIANT )
    block->storage = IM_STORAGE_MEMORY;
  block->flags &=
    ~(IM_BLOCKFLAG_WRITE_COMPLETE | IM_BLOCKFLAG_CHECKED_FOR_UNIFORM_DATA);
  block->sbytes = 0;
}

Bool im_blockisusable(IM_BLOCK *block)
{
  return block->refcount == 0 &&
         (block->flags & IM_BLOCKFLAG_WRITE_COMPLETE) != 0 &&
         im_blockisIMoveable(block);
}

Bool im_blockisIMoveable(IM_BLOCK *block)
{
  return ( (block->storage == IM_STORAGE_DISK) ||
           (block->storage == IM_STORAGE_UNIFORM_VARIANT) ||
           im_blockisICompressed(block) ||
           ((block->flags & IM_BLOCKFLAG_NO_LONGER_NEEDED) != 0) );
}

void im_blocktrim_ycheck(IM_STORE *ims, int32 bx, int32 by)
{
#if defined( ASSERT_BUILD )
  int32 planei, bb = bx + by * ims->xblock;

  HQASSERT(bb >= 0 && bb < ims->nblocks, "block out of range");
  for ( planei = 0 ; planei < ims->nplanes ; ++planei ) {
    IM_PLANE *plane = ims->planes[planei];

    if ( plane != NULL ) {
      HQASSERT((plane->blocks[bb] == NULL) ||
               (plane->blocks[bb]->flags & IM_BLOCKFLAG_NO_LONGER_NEEDED),
               "im_storetrim_ycheck: Should have already "
               "called im_storetrim_x (or x1 changed since)");
    }
  }
#else /* !ASSERT_BUILD */
  UNUSED_PARAM(IM_STORE *, ims);
  UNUSED_PARAM(int32, bx);
  UNUSED_PARAM(int32, by);
#endif /* ASSERT_BUILD */
}

int32 im_blocksizeof(void)
{
  return (int32)sizeof(IM_BLOCK);
}

void im_block2blist(IM_BLIST *blist, Bool cpBytes)
{
  IM_BLOCK *block = blist_getblock(blist);
  int16 abytes;

  (void)blist_getdata(blist, &abytes);
  blist_setdata(blist, block->data, abytes);
  block->blist = blist;
  if ( cpBytes )
    blist_setdata(blist, block->data, block->abytes);
  else
    im_blockvalidate(block, FALSE);
}

Bool im_blockcomplete(IM_BLOCK *block)
{
  HQASSERT(block, "block is NULL");
  return ((block->flags & IM_BLOCKFLAG_WRITE_COMPLETE) != 0);
}

/* Quick hack function : should disappear when I get the API sorted */
void im_block_null(IM_BLOCK *block, int32 id)
{
  switch (id) {
    case 0:
      block->blist = NULL;
      break;
    case 1:
      HQASSERT(im_blockisusable(block), "block should be usable for nulling");
      block->data = NULL;
      break;
  }
}


Bool im_blockdone(IM_STORE *ims, int32 planei, int32 bb)
{
  IM_BLOCK *block;

  if ( planei == IMS_ALLPLANES )
    planei = 0;

  block = ims->planes[planei]->blocks[bb];
  return im_blockcomplete(block);
}

/* Log stripped */
