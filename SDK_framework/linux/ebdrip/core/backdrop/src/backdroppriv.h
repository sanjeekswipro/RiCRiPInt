/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:backdroppriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __BACKDROPPRIV_H__
#define __BACKDROPPRIV_H__

#include "backdrop.h"
#include "cce.h"
#include "cvcolcvt.h"
#include "hqmemcpy.h"
#include "mm.h"
#include "imfile.h"
#include "mlock.h"
#include "taskres.h" /* resource_pool_t */

typedef struct BackdropBlock BackdropBlock;
typedef struct BackdropResource BackdropResource;

/**
 * BackdropShared contains all the information that is common to all backdrops,
 * and contains the resources that can be shared out to the backdrops.
 */
struct BackdropShared {
  /** The page is required spotlist_add, which is used to track spot numbers
      for efficient rendering of the backdrop when there are multiple screens. */
  DL_STATE          *page;

  /** dl pools for backdrop state and data allocations.  State memory is not
      purged, whereas data memory is.  The two allocation types are kept
      separate to avoid fragmentation. */
  mm_pool_t          statePool, dataPool;

  Bool               multiThreaded;

  /** First page backdrop for final rendering (multiple page backdrops if imposing pages). */
  Backdrop          *pageBackdrops;

  /** Linked-list of all the backdrops.*/
  Backdrop          *backdrops;
  uint32             nBackdrops;

  /** Largest input number of components of all backdrops. */
  uint32             inCompsMax;

  /** Width and height of each backdrop. */
  uint32             width, height;

  /** Bytes per table and number of tables, for all insertable blocks. */
  size_t             tableSize;
  uint32             nTables;

  /** Estimated values used for last resource provision (bd_resourceUpdate).
      (shared->inCompsMax and shared->tableSize are the final values set during
      backdrop creation.) */
  struct {
    Bool             nonisolatedPresent;
    uint32           inCompsMax;
    size_t           tableSize;

    uint32           maxBackdropDepth;

    /** Minimum number of resources guaranteed to be able to composite any single
        region.  Maximum number of resources possibly required for one band
        without sharing. */
    uint32           nMinResources, nMaxResources;
  } provisioned;

  /** Limit nMaxResources to a lower figure to help avoid contention with other
      resources. */
  uint32             nLimResources;

  /** There is always a backdrop block boundary at the band boundary to enable
      backdrops to be composited independently in bands. */
  uint32             regionHeight;

  /** Each region may have multiple rows of backdrop blocks, because backdrop
      block height is at most BLOCK_DEFAULT_HEIGHT. */
  uint32             blockRowsPerRegion;

  /** Number of region rows per band. */
  uint32             regionRowsPerBand;

  /** Width and height of backdrop blocks, except for right most column and
      bottommost row. */
  uint32             blockWidth, blockHeight;

  /** Number of blocks across backdrop (yblock may differ between backdrops and
      is therefore in Backdrop). */
  uint32             xblock;

  /** Flags specified to bd_backdropPrepare determining spot merging, output
      interleaving style etc. */
  uint32             retention; /**< RETAINNOTHING, RETAINBAND or RETAINFRAME. */
  Bool               mergeSpots;

  /** Max backdrop nesting depth. */
  uint32             maxBackdropDepth;

  /** Linked list of purgeable blocks for bd_backdropPurge. */
  BackdropBlock     *purgeableBlocks;
  IM_FILE_CTXT      *imfileCtxt;
  uint32             nBlocksToDisk;
#if defined( DEBUG_BUILD ) || defined( METRICS_BUILD )
  size_t             nBytesToDisk;
#endif

  /** Allow resources to be allocated to fulfill a request; don't bother if
      already started freeing resources in low memory. */
  Bool               dynamicResourceAlloc;

  /** Line repeat optimisation is disabled for PCL patterns. */
  Bool               allowLineRepeats;

  /** Temporary grab of memory to ensure there will be enough to composite.
      From UserParams.BackdropReserveSize. */
  size_t             backdropReserveSize;
};

struct Backdrop {
  Backdrop          *next;               /**< Next backdrop. */
  Backdrop          *nextPage;           /**< Next page backdrop. */
  BackdropShared    *shared;             /**< The shared backdrop state. */
  dbbox_t            bounds;             /**< Bounding box of the contents. */

  unsigned           depth;

  uint32             yblock;             /**< Number of blocks down backdrop (xblock is the same for
                                              all backdrops and is therefore in BackdropShared). */

  BackdropBlock    **blocks;             /**< Block pointers. */
  uint32             nBlocks;

  uint32             inComps;            /**< Number of input colorants. */
  uint32             inProcessComps;     /**< Number of process colorants in the input set (<= inComps). */
  COLORANTINDEX*     inColorants;        /**< List of input colorants contained (process colorants first). */

  uint32             outComps;           /**< Number of colorants in the output set of colorants. */
  COLORANTINDEX     *outColorants;       /**< List of output colorants (same as input if no color conversion). */

  struct CV_COLCVT  *converter;          /**< converter is used for color conversion between blend spaces. */
  LateColorAttrib   *pageGroupLCA;       /**< For transfering the rendering intent to the output backdrop
                                              from backdrops painted directly into the page group. */

  COLORVALUE        *pageColor;          /**< Opaque color for final composite onto the page. */

  struct {
    Backdrop          *backdrop;         /**< Initial backdrop for non-isolated backdrops. */
    COLORVALUE        *color;            /**< Initial color, alpha, group alpha and info for isolated backdrop. */
    COLORVALUE         alpha;
    COLORVALUE         groupAlpha;
    COLORINFO          info;
  } initial;

  Backdrop          *parentBackdrop;     /**< Backdrop is composited into this parent backdrop. */
  int32             *parentMapping;      /**< Colorant mapping from backdrop output colorants
                                              to parent backdrop input colorants. */

  COLORANTINDEX    **deviceColorantMapping; /**< For mapping an input colorant to its final device colorant.
                                                 Used for haltone merging for overprinted objects. */

  SoftMaskType       softMaskType;       /**< EmptySoftMask, AlphaSoftMask or LuminositySoftMask */
  struct FN_INTERNAL *softMaskTransfer;  /**< An optional transfer function to be applied to
                                              the alpha for luminosity softmasks. */
  COLORVALUE         defaultMaskAlpha;   /**< Alpha value for luminosity softmasks where there were no objects. */

  int8               isolated;
  int8               knockout;
  int8               knockoutDescendant;
  int8               compositeToPage;
  int8               trackShape;
  uint8              backdropLabel;
  int32              outTableType;
};

/**
 * The maximum number of entries possible in a table.  This is only
 * limited by the fact that backdrop stores indices in a uint8.
 */
#define BACKDROPTABLE_MAXSLOTS (256)

#define BLOCK_DEFAULT_SIZE   (16384)
#define BLOCK_DEFAULT_WIDTH  BACKDROPTABLE_MAXSLOTS
#define BLOCK_DEFAULT_HEIGHT (BLOCK_DEFAULT_SIZE / BLOCK_DEFAULT_WIDTH)
#define BLOCK_MIN_ALIGN      (8)

/**
 * Macros to read and store run lengths without branches.  Uses zero value to
 * mean a run length of 256.
 *   readRunLen - Expand run length, where 0=256, 1-255 are themselves.
 *   storeRunLen - Compress run length of 1-256 to a single byte.
 */
#define readRunLen(_byte) ((int32)(_byte) | (((int32)(_byte) - 1) & 0x100))
#define storeRunLen(_runlen) (uint8)(_runlen)

/** Debug control for backdrop. */
#if defined( DEBUG_BUILD )
extern int32 backdropDebug;

enum {
  /** Print various stats at the end of compositing a page. */
  BD_DEBUG_STATS             = 0x01,

  /** Print messages at key stages affecting memory, resources etc. */
  BD_DEBUG_MEMORY            = 0x02,

  /** Print summary of each bd_compositeSpan/Block/Backdrop call (verbose). */
  BD_DEBUG_COMPOSITING       = 0x04,

  /** Print each result color made by bd_compositeColor* (verbose). */
  BD_DEBUG_RESULTCOLOR       = 0x08,

  /** Scribble over data to help catch any uses of uninitialised data. */
  BD_DEBUG_SCRIBBLE          = 0x10,

  /** Highlight pixels that have been composited (changes color of output):
      Cyan pixels mean result didn't need compositing (wasteful compositing);
      Magenta pixels mean result did require compositing (necessary).  Note,
      this only works for CMYK output. */
  BD_DEBUG_COMPOSITEDPIXELS  = 0x20,

  /** Disable repeated lines optimisation. */
  BD_DISABLE_LINE_REPEATS    = 0x40,

  /** Disable poach blocks optimisation */
  BD_DISABLE_POACH_BLOCKS    = 0x80
};
#endif

/**
 * Backdrop retained block cost. Blocks are written to disk in low memory (band,
 * frame or separations) and usually read back three times for M,Y,K channels.
 */
#define BD_PURGE_COST_TIER memory_tier_disk
#define BD_PURGE_COST_VALUE 3.0f
extern const mm_cost_t purgeCost;

/**
 * bd_stateAlloc/Free are used for backdrop state memory.
 */
void *bd_stateAlloc(const BackdropShared *shared, size_t bytes);
void bd_stateFree(const BackdropShared *shared, void *addr, size_t bytes);

/**
 * bd_dataAlloc/Free are used for backdrop block data that is purgeable in low
 * memory.  Currently allocations use the image variable size pool.
 */
void *bd_dataAlloc(const BackdropShared *shared, size_t bytes,
                   mm_cost_t cost);
void bd_dataFree(const BackdropShared *shared, void *addr, size_t bytes);

/**
 * bd_dataFreeSize returns the amount of memory currently available
 * for backdrop data.
 */
size_t bd_dataFreeSize(const BackdropShared *shared);

/**
 * bd_resourceAlloc/Free are used for resource memory (compositing contexts,
 * backdrop resources and backdrop blocks).
 */
extern mm_pool_t resourcePool;
void *bd_resourceAlloc(size_t bytes);
void *bd_resourceAllocCost(size_t bytes, mm_cost_t cost);
void bd_resourceFree(void *addr, size_t bytes);
mm_pool_t bd_resourcePool(const resource_source_t *source);

struct BackdropReserve;
void bd_allocBackdropReserve(size_t reserveSize, Bool forceAlloc,
                             struct BackdropReserve **reserveHead);
void bd_freeBackdropReserve(struct BackdropReserve **reserveHead);

/**
 * Given an x value, returns the block index for the x component.
 */
uint32 bd_xBlockIndex(const BackdropShared *shared, dcoord x);

/**
 * Given a y value, returns the block index for the y component.
 */
uint32 bd_yBlockIndex(const BackdropShared *shared, dcoord y);

/**
 * Given an x value, returns the pixel index into the block
 * for the x component.
 */
uint32 bd_xPixelIndex(const BackdropShared *shared, dcoord x);

/**
 * Given a y value, returns the line index into the block
 * for the y component.
 */
uint32 bd_yPixelIndex(const BackdropShared *shared, dcoord y);

/**
 * Given a bx and by, returns the index into the block ptrs.  May return
 * different bb values for same bx and by on different backdrops (because not
 * all backdrop have a full set of block ptrs.
 */
uint32 bd_blockIndex(const Backdrop *backdrop, uint32 bx, uint32 by);

#define bd_copyColor(_dest, _src, _nColors) \
  switch ( _nColors ) { \
  default: \
    HqMemCpy(_dest, _src, (_nColors) * sizeof(COLORVALUE)); \
    break; \
  case 4: _dest[3] = _src[3]; \
  case 3: _dest[2] = _src[2]; \
  case 2: _dest[1] = _src[1]; \
  case 1: _dest[0] = _src[0]; \
  }

extern multi_mutex_t backdropLock;

#endif /* protection for multiple inclusion */

/* Log stripped */
