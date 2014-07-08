/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imstore_priv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image storage private data
 */

#ifndef __IMSTORE_PRIV_H__
#define __IMSTORE_PRIV_H__

#include "mm.h"                 /* mm_pool_t */
#include "imstore.h"
#include "imfile.h"
#include "hqbitvector.h"

/* Comments here on things that have not been done:
 * a) Split image blocks into smaller sub-blocks.
 *      (can only do this when following defines store in ims).
 * b) Use a strip method (as a fallback for if low on memory).
 *      (probably won't do).
 * c) Check for allocation of initial blocks in im_storeopen.
 *      (perhaps flush font cache, maybe test higher so can PP).
 * d) Allocation of initial blocks in im_storeopen is done as one big
 *    malloc - should be row by row.
 * e) Use multiple pools for blist blocks, std blocks & everything else
 */

/* IM_BLOCK_DEFAULT_
   Default width, height, size in bytes.  Actual width, height, size
   (stored in the image store) may be different from the default.
 */
#define IM_BLOCK_DEFAULT_WIDTH  (128)
#define IM_BLOCK_DEFAULT_HEIGHT (128)
#define IM_BLOCK_DEFAULT_SIZE   (IM_BLOCK_DEFAULT_WIDTH * IM_BLOCK_DEFAULT_HEIGHT)

#define IM_BLOCK_MIN      (512)
#define IM_BLOCK_VAR_MIN    (8) /* for variable sized blocks */

#define IM_ROWS_PER_LUT   (2)
#define IM_MAX_LUT_SIZE   (IM_ROWS_PER_LUT * IM_BLOCK_DEFAULT_WIDTH)

/* Low memory handling variables. Over time it may be that these will be tuned
 * or converted into parameters.
 */
#define IM_MIN_COMPRESSION_SIZE           (4 * IM_BLOCK_MIN)
#define IM_MIN_WRITETODISK_SIZE           (1 * IM_BLOCK_MIN)
#define IM_MAX_COMPRESSION_RATIO          (0.6)
#define IM_MAX_BLISTS_TO_PURGE            (1000)
#define IM_MAX_PURGED_BLOCK_FRACTION      (0.001)
#define IM_MAX_PURGED_STORE_FRACTION      (0.01)

enum {
  IM_COMPRESS_NONE,
  IM_COMPRESS_TOO_BIG, /* Data was too random, or too small to be compressed */
  IM_COMPRESS_FAILED,  /* Compression failed due to vmerror */
  IM_COMPRESS_LZW,
  IM_COMPRESS_CCITT,
  IM_COMPRESS_FLATE,
  IM_COMPRESS_B32, /* special compression for 32bit images */
  IM_COMPRESS_COPY
};

enum {
  IM_STORAGE_NONE,
  IM_STORAGE_MEMORY,
  IM_STORAGE_DISK,
  IM_STORAGE_UNIFORM_BLOCK,   /* Image block has uniform data */
  IM_STORAGE_UNIFORM_VARIANT  /* Normal image block has uniform data, but
                               * still requires expansion into a data block
                               * when rendering */
};

/* States of low memory handling for stores. */
enum {
  IM_ACTION_OPEN_FOR_WRITING, /* Initial state and changes in im_storeclose */
  IM_ACTION_COMPRESSION,      /* Blocks may be compressed */
  IM_ACTION_SHAREBLISTS_1,    /* Blists may be moved to the global blist */
  IM_ACTION_WRITETODISK,      /* Blocks may be purged to disk */
  IM_ACTION_SHAREBLISTS_2,    /* Blists may be moved to the global blist */
  IM_ACTION_NOTHINGMORE,      /* Nothing else can be done */
  IM_NUM_ACTIONS
};

enum {
  xIM_STATE_INTERPRETING,
  xIM_STATE_RENDERING
};

/** Values to go into the block flags field */
enum {
  /** A block is completely uniform in color */
  IM_BLOCKFLAG_IS_UNIFORM =                   0x01,

  /** A process is currenty rewriting data into the block (SMP only) */
  IM_BLOCKFLAG_IS_LOADING =                   0x02,

  /** No further writes to this block (for backdrops,
     also indicates whether in insert or compact mode) */
  IM_BLOCKFLAG_WRITE_COMPLETE =               0x04,

  /** Checked block to see if it is completely uniform in color (normal images
     only). Used in low memory handling as a possible optimisation to avoid
     compressing or writing to disk. */
  IM_BLOCKFLAG_CHECKED_FOR_UNIFORM_DATA =     0x08,

  /** Trimmed blocks get this flag indicating that it's OK for them to have
      no data.  When image filtering we can have blocks that may be disposed
      of or reused */
  IM_BLOCKFLAG_NO_LONGER_NEEDED =             0x20
} ;

typedef struct IM_PLANE       IM_PLANE;
typedef struct IM_STORE_NODE  IM_STORE_NODE;

/** Each plane is split up into blocks of data. So that multiple images can
 * share disk read back blocks, we try and make the block sizes the same
 * across images. This only fails when we have an excessively large image.
 */
struct IM_PLANE {
  IM_BLOCK**  blocks;
  IM_BLIST*   blisth;
  int32       yCompressed;       /**< The next row to try compressing. */
  int32       yPurged;           /**< The next row to try purging. */
  int32       nDesiredBlists;    /**< The number of blists desired, the min
                                      required for interpretation and rendering */
  int32       nBlists;           /**< The number of blists allocated during
                                      interpretation */
};


/** A link structure for preconversion reserve chunks. */
typedef struct {
  size_t size;
  void *next;
  /* followed by the rest of the chunk */
} im_reserve_chunk_t;


/** A store containing all of an image's data. */
struct IM_STORE {
  IM_SHARED *im_shared;    /**< The image store is part of this page. */
  uint8 openForWriting;    /**< open/closed */
  uint8 bpp;               /**< Bits per pixel of image. */
  uint8 bpps;              /**< Bits per pixel of image (stored as a shift). */
  uint8 action;            /**< What this image store can do for low memory
                                handling. */
  int16 nplanes;           /**< Number of planes in the image (for packing) */
  int16 abytes;            /**< block allocation size, used for ordering linked
                                list of store nodes */

  ibbox_t obbox;           /**< Original store area as image space bbox. */
  ibbox_t tbbox;           /**< Trimmed area as image space bbox. */

  int32 blockWidth;        /**< Block width in bytes (right-most col of blocks
                                may be less)*/
  int32 blockHeight;       /**< Block height in bytes (bottom row of blocks
                                may be less)*/
  uint32 flags;            /**< combination of IMS_ bits */
  uint8 *swapmem;          /**< Spare memory when swapping x/y for 1 bit data */

  int32 xblock;            /**< Number of blocks across image. */
  int32 yblock;            /**< Number of blocks down   image. */

  int32 nblocks;           /**< Number of blocks per image plane. */
  IM_PLANE **planes;       /**< Array of planes in the image. */

  int32 stdblocks;         /**< Whole number of standard blocks. */
  int32 extblocks;         /**< Extra bytes in small blocks. */

  im_reserve_chunk_t *reserves; /**< Chain of preconversion reserves. */
  uint8 purged;            /**< Indicates whether any blocks were purged */
  uint8 blistPurgeRow;     /**< Write blocks to disk or compress at the end of
                                scanning a row */

  int32 band;              /**< Used for intelligent low memory handling. */

  IM_STORE *next;          /**< For finding images available for low memory
                                purging */
  IM_STORE *prev;          /**< For finding images available for low memory
                                purging */
  bitvector_element_t *row_repeats; /**< Bit flags indicating whether a row is
                                         a repeat of the previous row. Flag
                                         also set if row isn't identical but
                                         close enough. */
};

#define NUM_RENDERING_BLISTS(ims) ((int32)(NUM_THREADS() * ims->xblock))
#define NUM_INTERPRETATION_BLISTS(ims) ((ims->flags & IMS_XYSWAP)? ims->yblock : ims->xblock)

typedef struct IMFPARAMS {
  int32 name;
  int32 integer;
} IMFPARAMS;


struct IM_STORE_NODE {
  int32           abytes;
  IM_STORE        *ims[IM_NUM_ACTIONS];
  int32           nStores[IM_NUM_ACTIONS];
  int32           nBlocks[IM_NUM_ACTIONS];
  IM_BLIST        *blistHead;
  IM_BLIST        *blistTail;
  int32           nBlists;
  mm_pool_t       pool;
  IM_STORE_NODE   *next;
  IM_STORE_NODE   *prev;
};

typedef struct IM_STORE_LINK IM_STORE_LINK;

struct IM_STORE_LINK {
  IM_STORE *ims;
  IM_STORE_LINK *next, *prev;
};

struct BLIST_CTX;

struct IM_SHARED {
  DL_STATE *page;

  mm_pool_t mm_pool_imbfix;     /**< For image data allocs of IM_BLOCK_DEFAULT_SIZE */
  mm_pool_t mm_pool_imbvar;     /**< For other image data alloc sizes */

  struct BLIST_CTX *blist_ctx;  /**< blist context info */
  IM_STORE_NODE *im_list;       /**< Points to a list of all image stores with
                                     blocks to purge, organised by size for use
                                     in low memory purging */
  int32     im_nStoresTotal;    /**< count of ims's in all lists */
  int32     im_nBlocksTotal;    /**< count of blocks in all lists */
  int32     im_nStores[IM_NUM_ACTIONS]; /**< total of nStores in all nodes
                                             (per action) */
  int32     im_nBlocks[IM_NUM_ACTIONS]; /**< total of nBlocks in all nodes
                                             (per action) */

  IM_FILE_CTXT *imfile_ctxt;    /**< Context for paged out image store data. */

  IM_STORE_LINK *if_list;      /**< Linked-list of image filter stores */
};

Bool im_planenew(IM_STORE *ims, int32 planei);
int32 im_index_bx(IM_STORE* ims, int32 x);
int32 im_index_by(IM_STORE* ims, int32 y);
int32 im_index_xi(IM_STORE* ims, int32 x);
int32 im_index_yi(IM_STORE* ims, int32 y);
int32 im_storepurgeOne( IM_STORE *ims );
Bool im_purgeglobal(IM_SHARED *im_shared);
void im_unlinkims(IM_STORE *ims, uint8 action);
void im_relinkims(IM_STORE *ims, uint8 action);
Bool im_linkims(IM_STORE *ims, uint8 action);
int32 im_bb_get_and_check(IM_STORE *ims, int32 bx, int32 by, int32 planei);
void ims_set_action(IM_STORE *ims, Bool close);
Bool ims_can_compress(IM_STORE *ims);
Bool ims_can_write2disk(IM_STORE *ims);

void im_datafree(IM_SHARED *im_shared, void *data, size_t abytes);
mm_addr_t im_dataalloc(IM_SHARED *im_shared, size_t abytes, mm_cost_t cost);

#if defined(DEBUG_BUILD)
extern int32 debug_imstore;
/** Bit fields for ripvar debugging */
enum {
  IMSDBG_REPORT = 1,
  IMSDBG_BLOCKMEM = 2,
  IMSDBG_REPORT_INCOMPLETE = 4,
  IMSDBG_WARN = 8,
  IMSDBG_LOWMEM = 16
} ;
#endif

/* ---------------------------------------------------------------------- */
#endif /* protection for multiple inclusion */


/* Log stripped */
