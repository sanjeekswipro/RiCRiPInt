/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gs_table.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color conversion (Tom's) tables.
 */

#include "core.h"
#include "gs_tablepriv.h"

#include "color.h"              /* ht_getClear */
#include "dlstate.h"            /* page->color */
#include "gu_chan.h"            /* guc_backdropRasterStyle */
#include "metrics.h"            /* sw_metrics_group */
#include "mm.h"                 /* mm_alloc */
#include "lowmem.h"
#include "deferred.h"
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "swerrors.h"           /* error_handler */
#include "hqmemset.h"
#include "timing.h"

#include "gs_colorpriv.h"       /* GS_CHAINinfo */
#include "gs_cache.h"           /* GSC_ENABLE_3_COMP_TABLE_POINTER_CACHE */
#include "gscheadpriv.h"        /* GSC_FIRST_LINK_NORMAL */
#include "gschtone.h"           /* gsc_getSpotno */
#include "gscparamspriv.h"      /* colorUserParams */
#include "gsctintpriv.h"        /* cc_tinttransformiscomplex */
#include "mlock.h"
#include "swtrace.h"
#include "objnamer.h"


/**
 * How many bits are allocated to the fractional part of the numbers coming in
 * to the tables.
 */
#define GSC_FRACNSHIFT  8
#define GSC_FRACNSIDE   ( 1 << GSC_FRACNSHIFT )
#define GSC_FRACNMASK   ( GSC_FRACNSIDE - 1 )

#define SCALED_COLOR(_maxIndex)   ( (_maxIndex) << GSC_FRACNSHIFT )

/**
 * Interpolation macros should be used in the following order:
 *   GST_FIRST_INTERPOLATE, optionally GST_INTERPOLATE, then GST_ROUND.
 *
 * The GST_FIRST_INTERPOLATE result value is not shifted down after the
 * interpolation calculation giving an extra GST_IFRACBITS of precision for
 * intermediate values.  This extra precision is removed by the final step of
 * calling GST_ROUND.  GST_INTERPOLATE uses truncation to go from
 * 2*GST_IFRACBITS extra precision back to GST_IFRACBITS of extra precision.
 *
 * Tetrahedral cases do interpolation directly and only use GST_ROUND.
 */
#define GST_INTERPOLATE_WITHOUT_SHIFT( _res , _fr , _lo , _hi )   \
MACRO_START                                                       \
  int32 _fr_ = (_fr) ;                                            \
  int32 _lo_ = (_lo) ;                                            \
  int32 _hi_ = (_hi) ;                                            \
  (_res) = _fr_ * ( _hi_ - _lo_ ) + ( _lo_ << GST_IFRACBITS ) ;   \
MACRO_END

#define GST_FIRST_INTERPOLATE GST_INTERPOLATE_WITHOUT_SHIFT

/** GST_INTERPOLATE doesn't round and should only be used after
    GST_FIRST_INTERPOLATE. */
#define GST_INTERPOLATE( _res , _fr , _lo , _hi )         \
MACRO_START                                               \
  uint32 _res_ ;                                          \
  GST_INTERPOLATE_WITHOUT_SHIFT( _res_, _fr, _lo, _hi ) ; \
  (_res) = _res_ >> GST_IFRACBITS ;                       \
MACRO_END

#define GST_ROUND( _res ) (((_res) + GST_IFRACADDN) >> GST_IFRACBITS)

/**
 * Increment a table index, clamping to the maximum index.
 */
#define INDEX_INCREMENT_AND_CLIP(_index) \
MACRO_START                                   \
  HQASSERT( (_index) >= 0 && (_index) < colorTable->cubeSide, \
            "index should be in range" ) ;    \
  (_index)++;                                 \
  if ((_index) == colorTable->cubeSide)       \
    (_index) = colorTable->cubeSide - 1 ;     \
MACRO_END

/**
 * This macro invalidates color cube cache lookup
 */
#define INVALIDATE_COLORCUBE_CACHE_LOOKUP( _colorTable ) \
MACRO_START                                         \
  (_colorTable)->indicesId = INDICES_ID_INVALID ;   \
  (_colorTable)->indices[ 0 ] = -1 ;                \
MACRO_END

/**
 * This is the maximum number of bits that the cornerPtrs cache can support. It
 * limits the number of cubeSideBits that can fit into a 32 bit word. We
 * reserve the top 2 bits for error checking.
 */
#define INDICES_HASH_BITS     (30)

/**
 * The top 2 bits of the 'ptrsEntry.id' field marks the cornerPtrs cache
 * entry as invalid. The bottom 30 bits are available for INDICES_HASH_BITS
 * worth of Id.
 */
#define INDICES_ID_INVALID    (0xC0000000)

/*
 * A table is a cube of mini-cubes enclosing a small enough volume that we
 * consider it linear for the purpose of interpolation. The number of mini-cubes
 * in one side of the cube is the FasterColorGridPoints systemparam. Since the
 * number of dimensions is not limited and the size of the cube grows
 * exponentially with dimension, the table is likely to require partial purging
 * as part of low memory handling, we build up the cube in a lazy, sparse
 * fashion by allocating 1d arrays of pointers as necessary for the first M-1
 * dimensions. The last dimension is a 1d array containing the color values
 * amongst other things. The colors at each grid point within the table are
 * also lazily populated.
 */

/** This marks off the final 1d array that contains the color values. */
#define SOLID_DIMS  (1)

/**
 * When interpolating, we want to know a number of things for each mini-cube
 * (given an "anchor" point, of the corner in the mini-cube nearest the
 * origin):
 * a) Do all corners of this mini-cube have a valid color?
 * b) Is interpolation valid for this mini-cube?
 * c) Does the anchor point have a valid color?
 * These flags are packed into the 'valid' byte of the mini-cube.
 */
#define GST_CUBE_TESTED     (0x01)
#define GST_CUBE_LINEAR     (0x02)
#define GST_COLOR_PRESENT   (0x04)
#define GST_VALID_MASK      (0x07)

typedef uint8 validMask ;

typedef struct colorNd {
  COLORVALUE  color[ 1 ] ; /* Use struct hack to create an array of oncomps. */
} colorNd ;

typedef struct ptrsEntry {
  uint32      id ;
  COLORVALUE  **cornerPtrs ;
} ptrsEntry ;

typedef struct colorCube1d {
  validMask           *valid ;        /* Points to an array of cubeSide */
  struct colorCube1d  *next ;
  struct colorCube1d  *prev ;
  uint32              timestamp ;
  colorNd             *color[ 1 ] ;   /* Use struct hack to create an array of cubeSide ptrs */
} colorCube1d ;

typedef union colorCubeNd {
  union colorCubeNd  *cn;
  struct colorCube1d *c1;
} colorCubeNd ;

/**
 * Extra precision bits within which we interpolate.
 * Note that this can be 8 at most, since we have 15 bit device codes which we
 * scale up by this amount, and then multiple by GSC_FRACNSHIFT (8).
 */
#define GST_IFRACBITS 8
#define GST_IFRACADDN (( 1 << GST_IFRACBITS ) >> 1 )

/*----------------------------------------------------------------------------*/
/* A pile of debug stats in GSC_TABLE which show memory useage, interpolation
   accuracy, etc...
#define GST_EVAL_LOOKUPS
#define GST_EVAL_INT_ERROR
#define GST_EVAL_MEM_TOTAL
#define GST_EVAL_MEM_POOLS
 */

#if defined GST_EVAL_LOOKUPS || defined GST_EVAL_INT_ERROR || defined GST_EVAL_MEM_TOTAL
#define GST_DEBUG_VARIABLES
#endif

#ifdef GST_DEBUG_VARIABLES

#define DEBUG_INCREMENT(_num, _inc)   \
MACRO_START                           \
  _num += _inc;                       \
MACRO_END

#define DEBUG_DECREMENT(_num, _inc)   \
MACRO_START                           \
  _num -= _inc;                       \
MACRO_END
#else
#define DEBUG_INCREMENT(_num, _inc) MACRO_START MACRO_END
#define DEBUG_DECREMENT(_num, _inc) MACRO_START MACRO_END
#endif

/*----------------------------------------------------------------------------*/
typedef struct colorTableNd {
  int32         incomps ;       /* Dimension of input components (AFTER RCB TINT). */
  int32         oncomps ;       /* Dimension of output components. */

  int32         cubeSide ;      /* Number of grid points on each side of the hyper cube. */
  int32         maxTableIndex ; /* Max index (== cubeSide - 1). */
  int32         cubeSideBits ;  /* Number of bits containing cubeSide. Used for
                                 * packing and shifting */

  int32         *indices ;      /* Indices of the origin of the mini-cube in the
                                 * color table for input pixel. */
  int32         *incIndices ;   /* Incremented 'indices' of the opposite side of
                                 * mini-cube from the origin in all dimensions */
  int32         *tmpIndices ;   /* Indices of a particular grid point within the
                                 * mini-cube as used when populating it. */
  int32         *tmpIncIndices ;/* An assist array used for a high performance
                                 * algorithm to derive tmpIndices. */
  uint32        indicesHash ;   /* Hash of current color for quick access to cornerPtrs array. */
  uint32        indicesId ;     /* Id of current color for quick access to cornerPtrs array. */

  int32         *fractns ;      /* Fractional pixel value used for interpolation. */
  float         *errordc ;      /* Array of validation error scaling factors. */
  float         errordv ;       /* Value of validation error. */
  int32         *results ;      /* Temporary output of interpolation used in some methods. */

  COLORVALUE    **cornerPtrs ;  /* The last used array of pointers to colors to be
                                 * interpolated. The array contains every grid point
                                 * in the local mini-cube of 2^incomps points. It
                                 * points to an element of cornerPtrsCache */
  ptrsEntry     **cornerPtrsCache ; /* Cache of cornerPtrs arrays. It's organised
                                     * by hashing the current table indices and
                                     * storing the MRU entry */
  uint32        cornerPtrsCacheBits;/* Determines cornerPtrsCacheSize, the number of
                                     * entries in cornerPtrsCache */
  uint32        cornerPtrsCacheSize;/* always 2^cornerPtrsCacheBits */
  uint32        cornerPtrsCacheMask;/* always cornerPtrsCacheSize - 1 */
  ptrsEntry     **cornerPtrsFallback;/* Used if cornerPtrsCache is blown away in low memory handling */

  /* Info for >4 component tables. All corners of a mini-cube aren't evaluated
   * before interpolation, so the required corners are lazily populated during
   * interpolation. */
  struct {
    GS_COLORinfo *colorInfo;
    int32       colorType;
    int32       *origOrder;     /* After sorting fractns in order, this holds orig order. */
  } N;

                                /* Interpolation function for the right number of components */
  Bool          (*interpolatefunc)( struct colorTableNd *colorTable , COLORVALUE *poColorValues ) ;

  float         *scratch ;      /* Memory used to transiently hold input color values. */

  float         *rangeb ;       /* Base  value of input color space we map table into. */
  float         *ranges ;       /* Scale value of input color space we map table into. */
  float         *rangen ;       /* Scale value of input color space we map table into. */

  Bool          beingused;      /* modify purge behaviour(front-end only). */
  colorCube1d   *cubemru ;      /* Most recently used color 1d's. */
  colorCubeNd   cube ;          /* The loads of data... */

  /* Copies of values from CoreContext.page */
  int           fasterColorMethod;
  USERVALUE     fasterColorSmoothness;

  mm_pool_t     mm_pool_table ; /* Memory pool used for cube/table allocations. */
  Bool purgeable; /* Any purgeable objects in the table? */

#ifdef GST_DEBUG_VARIABLES
  /* Debug variables subject to GST_EVAL_LOOKUPS */
  int32 nLookupHits ;          /* Number of mini-cube indices lookups. */
  int32 nLookupMisses ;        /* Number of non-cached lookups. */
  int32 nPtrsCacheHits ;       /* Number of cornerPtrs cache hits */
  int32 nPtrsCacheMisses ;     /* Number of cornerPtrs cache misses */
  int32 nPtrsCacheDuplicates ; /* Number of cornerPtrs cache duplicates */
  int32 nVerified ;            /* Number of mini-cubes requiring verification. */
  int32 nValidate ;            /* Number of mini-cubes requiring validation. */
  int32 nValidateSum ;         /* Sum of errors for mini-cubes requiring validation. */
  int32 nPopulate ;            /* Number of mini-cube corners requiring population. */

  /* Debug variables subject to GST_EVAL_MEM_TOTAL */
  int32 nMemCubeN ;            /* Memory used for N cubes. */
  int32 nMemCube1 ;            /* Memory used for 1 cubes. */
  int32 nMemColor ;            /* Memory used for colors. */
  int32 nMemCornerPtrs ;       /* Memory used for cornerPtrs. */
  int32 nMemTotal ;            /* Memory used for all cube structures. */

  /* Debug variables subject to GST_EVAL_INT_ERROR */
  int32 nInterpolate ;          /* Number of points requiring interpolation. */
  int32 nInterpolateError ;     /* Sum of interpolation errors */
#endif

} colorTableNd ;

/*----------------------------------------------------------------------------*/
struct gsc_table {
  colorTableNd      *colorTable ;
  COLOR_STATE       *colorState ;
  Bool              frontEnd;
  GSC_TABLE         *next ;
} ;

/*----------------------------------------------------------------------------*/
struct GSC_TOMSTABLES {
  low_mem_handler_t lowmem_handler;
  COLOR_STATE *colorState;
  GSC_TABLE *head;
  OBJECT_NAME_MEMBER
};

#define TOMSTABLES_NAME "Tom's Tables"

/*----------------------------------------------------------------------------*/

static Bool gsc_tomstables_start(COLOR_STATE *colorState);
static void gsc_tomstables_finish(COLOR_STATE *colorState);
static void gst_c1remove( colorCube1d *c1 , colorCube1d **c1h ) ;
static Bool gst_purgeCube( colorTableNd *colorTable ,
                           colorCubeNd *pcube ,
                           int32 incomps ,
                           uint32 timestamp ,
                           Bool preserve ) ;
static void gst_purgeColorN(colorTableNd *colorTable, double fraction_to_purge,
                            Bool preserveInUseValues);
static Bool gst_getColorN( colorTableNd *colorTable,
                           int32 *indices,
                           colorNd ***color, validMask **valid) ;
static Bool gst_getInterpolateColorsN( colorTableNd *colorTable ,
                                       GS_COLORinfo *colorInfo , int32 colorType ,
                                       colorNd **firstColor , validMask **firstValid ) ;
static Bool gst_populateColorN( colorTableNd *colorTable ,
                                int32 *indices ,
                                GS_COLORinfo *colorInfo , int32 colorType ,
                                colorNd **color , validMask *valid ) ;
static Bool gst_evaluateCentreN( colorTableNd *colorTable ,
                                 GS_COLORinfo *colorInfo , int32 colorType ,
                                 COLORVALUE *poColorValues ) ;
#ifdef GST_EVAL_INT_ERROR
static Bool gst_evaluateColorN( colorTableNd *colorTable ,
                                GS_COLORinfo *colorInfo , int32 colorType ,
                                int32 *piColorValues , COLORVALUE *poColorValues ) ;
#endif
static Bool gst_validateColorsN( colorTableNd *colorTable ,
                                 COLORVALUE *poColorValues ) ;
static inline void extractCorners_1(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols);
static inline void extractCorners_2(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols);
static inline void extractCorners_3(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols);
static inline void extractCorners_4(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols);
static void checkIndices(colorTableNd *colorTable);
static Bool gst_interpolate1( colorTableNd *colorTable ,
                              COLORVALUE *poColorValues ) ;
static Bool gst_interpolate2( colorTableNd *colorTable ,
                              COLORVALUE *poColorValues ) ;
static Bool gst_interpolate2_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues ) ;
static Bool gst_interpolate3_cubic( colorTableNd *colorTable ,
                                    COLORVALUE *poColorValues ) ;
static Bool gst_interpolate3_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues ) ;
static Bool gst_interpolate4_cubic( colorTableNd *colorTable ,
                                    COLORVALUE *poColorValues ) ;
static Bool gst_interpolate4_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues ) ;
static Bool  gst_interpolateN_cubic( colorTableNd *colorTable ,
                                     COLORVALUE *poColorValues ) ;
static Bool  gst_interpolateN_tetrahedral( colorTableNd *colorTable ,
                                           COLORVALUE *poColorValues ) ;

static colorTableNd *gst_allocTable( int32 incomps , int32 oncomps ) ;
static void gst_freeTable(colorTableNd **pColorTable);
static colorCubeNd *gst_allocCubeN( colorTableNd *colorTable, int32 cubeSide ) ;
static void gst_freeCubeN( colorTableNd *colorTable , colorCubeNd *cn ,
                           int32 cubeSide ) ;
static colorCube1d *gst_allocCube1( colorTableNd *colorTable, int32 cubeSide ) ;
static void gst_freeCube1( colorTableNd *colorTable, colorCube1d *c1,
                           int32 cubeSide ) ;
static colorNd *gst_allocColorN( colorTableNd *colorTable, int32 oncomps ) ;
static void  gst_freeColorN( colorTableNd *colorTable, colorNd **pcolor,
                             int32 oncomps ) ;
static ptrsEntry *gst_allocCornerPtrsEntry( colorTableNd *colorTable,
                                            int32 incomps ) ;
static void gst_freeCornerPtrsEntry( colorTableNd *colorTable,
                                     ptrsEntry **pCacheEntry,
                                     int32 incomps ) ;


static Bool gst_rangeinit(GS_COLORinfo *colorInfo, int32 colorType,
                           int32 incomps , int32 maxTableIndex ,
                          float *rangeb , float *ranges , float *rangen ) ;
static Bool gst_errordcinit(GS_COLORinfo *colorInfo, int32 colorType,
                            int32 oncomps, USERVALUE fasterColorSmoothness,
                            float *errordc, float *errordv ) ;

static Bool gst_getIndicesNoCache(colorTableNd *colorTable , int32 *piColorValues);
static Bool gst_getIndicesWithCache1C(colorTableNd *colorTable , int32 *piColorValues);
static Bool gst_getIndicesWithCache2C(colorTableNd *colorTable , int32 *piColorValues);
static Bool gst_getIndicesWithCache3C(colorTableNd *colorTable , int32 *piColorValues);
static Bool gst_getIndicesWithCache4C(colorTableNd *colorTable , int32 *piColorValues);

static int32 maxTableIndex(COLOR_PAGE_PARAMS *colorPageParams);

/*----------------------------------------------------------------------------*/


#ifdef METRICS_BUILD


static struct gst_metrics {
  size_t gst_pool_max_size;
  int32 gst_pool_max_objects;
  size_t gst_pool_max_frag;
} gst_metrics;

static Bool gst_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("ColorTable")) )
    return FALSE;
  SW_METRIC_INTEGER("PeakPoolSize", (int32)gst_metrics.gst_pool_max_size);
  SW_METRIC_INTEGER("PeakPoolObjects", gst_metrics.gst_pool_max_objects);
  SW_METRIC_INTEGER("PeakPoolFragmentation", (int32)gst_metrics.gst_pool_max_frag);
  sw_metrics_close_group(&metrics);
  sw_metrics_close_group(&metrics);
  return TRUE;
}

static void gst_metrics_reset(int reason)
{
  struct gst_metrics init = { 0 };
  UNUSED_PARAM(int, reason);
  gst_metrics = init;
}

static sw_metrics_callbacks gst_metrics_hook = {
  gst_metrics_update,
  gst_metrics_reset,
  NULL
};


#endif /* METRICS_BUILD */


void init_C_globals_gs_table(void)
{
  /*****************************************************************************

    Globals are only allowed for frontend color transforms. If an item needs to
    be used for both frontend and backend transforms then it should be put into
    COLOR_STATE.

  *****************************************************************************/

#ifdef METRICS_BUILD
  gst_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&gst_metrics_hook);
#endif
}



/**
 * The public interface to this file are all the cc_*() & gsc_*() functions.
 * You make a table conversion open call, convert what you want and then free
 * it. Low memory handling completes the use of the 4 public routines.
 *
 * This routine is the one that you must call to set the ball rolling.
 * It allocate the private pool used by the table and all the other data
 * structures, and initialises them. It also sets up the various data
 * structures it needs for the fallback cache, color converting mini-cube
 * points & determining mini-cube interpolation validity.
 */
GSC_TABLE *cc_createTomsTable(GS_COLORinfo *colorInfo, int32 colorType)
{
  colorTableNd *colorTable ;
  GSC_TABLE *gst ;
  int32 incomps;
  int32 oncomps;
  COLORANTINDEX *dummyColorants;
  uint32 i;
  int32 ncomps;
  int32 bits;
  uint32 size;
  uint32 mask;
  ptrsEntry **cornerPtrsAlloc1;
  ptrsEntry **cornerPtrsAllocN = NULL;
  GS_CHAINinfo *colorChain;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT( colorInfo , "colorInfo NULL" ) ;

  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  incomps = colorInfo->chainInfo[colorType]->n_iColorants;
  if (!gsc_getDeviceColorColorants(colorInfo, colorType,
                                   &oncomps, &dummyColorants))
    return NULL;

  HQASSERT( incomps <= 16 , "incomps rather large" ) ;
  HQASSERT( incomps >= SOLID_DIMS , "incomps must be >= SOLID_DIMS (aka 1)" ) ;
  HQASSERT( oncomps > 0 , "oncomps should be > 0" ) ;

  colorTable = gst_allocTable( incomps, oncomps ) ;
  if ( colorTable == NULL )
    return NULL ;

  /* Get the range of the color space so that we know how to map cube
   * points into the input color space. Also get the error normalisers
   * for when testing interpolation and the jitter arrays when
   * truncating. */
  if ( ! gst_rangeinit( colorInfo , colorType ,
                        incomps , colorTable->maxTableIndex ,
                        colorTable->rangeb , colorTable->ranges , colorTable->rangen )) {
    gst_freeTable(&colorTable);
    return NULL ;
  }
  if ( ! gst_errordcinit(colorInfo, colorType,
                         oncomps, colorTable->fasterColorSmoothness,
                         colorTable->errordc, &colorTable->errordv)) {
    gst_freeTable(&colorTable);
    return NULL ;
  }
  for (ncomps = 0; ncomps < incomps; ncomps++) {
    colorTable->indices[ncomps] = -1 ;
    colorTable->incIndices[ncomps] = -1 ;
    colorTable->tmpIndices[ncomps] = -1 ;
    colorTable->tmpIncIndices[ncomps] = -1 ;
  }

  /* Establish the best size for the cornerPtrs cache for the value of incomps.
   * This is a compromise between memory (which rises exponentially with incomps)
   * and performance.
   *
   * For the moment this performance boost is only enabled for 1 - 4 component
   * images. A different way will need to be found to boost the performance
   * of images with a larger number of components because memory use rises
   * exponentially. In any case, the method will only work for a limited
   * number of colorants because indicesId can only hold the number of
   * cubeSideBits that will fit into INDICES_HASH_BITS.
   */
  HQASSERT(colorTable->cubeSideBits <= 6, "The number of grid points is invalid");
  switch (incomps) {
  case 1:
    bits = colorTable->cubeSideBits;
    break ;
  case 2:
    bits = colorTable->cubeSideBits;
    break;
  case 3:
    if ((chainContext->cacheFlags & GSC_ENABLE_3_COMP_TABLE_POINTER_CACHE) != 0)
      bits = 5;
    else
      bits = 0;
    break;
  case 4:
    if ((chainContext->cacheFlags & GSC_ENABLE_4_COMP_TABLE_POINTER_CACHE) != 0)
      bits = 4;
    else
      bits = 0;
    break;
  default:
    bits = 0;
    break;
  }
  if (bits > colorTable->cubeSideBits)
    bits = colorTable->cubeSideBits;

  /* We will now make potentially 2 allocations for the cornerPtrs cache. One is
   * cornerPtrsCache, an array of 'size' for the main cornerPtrs cache used for
   * 1-4 component images. The other is cornerPtrsFallback, an allocation of
   * just one structure used for both N component images and also as a fallback
   * in low memory handling of 1-4 component images - when the main array might
   * get blown away. */
  cornerPtrsAlloc1 = mm_alloc( mm_pool_color,
                               sizeof(ptrsEntry *),
                               MM_ALLOC_CLASS_COLOR_TABLE ) ;
  if ( cornerPtrsAlloc1 == NULL ) {
    gst_freeTable(&colorTable);
    (void) error_handler( VMERROR );
    return NULL ;
  }

  size = 1 << (incomps * bits);
  mask = (1 << bits) - 1;

  if ( bits > 0 ) {
    cornerPtrsAllocN = mm_alloc( colorTable->mm_pool_table,
                                 sizeof(ptrsEntry *) * size,
                                 MM_ALLOC_CLASS_COLOR_TABLE ) ;
    if ( cornerPtrsAllocN == NULL ) {
      /* We're in low memory, fall back to a size of just one */
      bits = 0;
      size = 1 << (incomps * bits);
      mask = (1 << bits) - 1;
    }
  }

  colorTable->cornerPtrsCacheBits = bits;
  colorTable->cornerPtrsCacheSize = size;
  colorTable->cornerPtrsCacheMask = mask;
  if ( bits > 0 ) {
    colorTable->cornerPtrsCache = cornerPtrsAllocN;
    colorTable->cornerPtrsFallback = cornerPtrsAlloc1;
  }
  else {
    colorTable->cornerPtrsCache = cornerPtrsAlloc1;
    colorTable->cornerPtrsFallback = NULL;
  }

  for (i = 0; i < colorTable->cornerPtrsCacheSize; i++)
    colorTable->cornerPtrsCache[i] = NULL;

  switch (colorTable->fasterColorMethod) {
  default:
    HQFAIL("Unexpected interpolation method");
  case NAME_Tetrahedral:
  case NAME_Cubic:
    break;
  }

  switch( incomps ) {
  case 1:
    colorTable->interpolatefunc = gst_interpolate1;
    break ;
  case 2:
    if (colorTable->fasterColorMethod == NAME_Tetrahedral)
      colorTable->interpolatefunc = gst_interpolate2_tetrahedral;
    else
      colorTable->interpolatefunc = gst_interpolate2;
    break;
  case 3:
    if (colorTable->fasterColorMethod == NAME_Tetrahedral)
      colorTable->interpolatefunc = gst_interpolate3_tetrahedral;
    else
      colorTable->interpolatefunc = gst_interpolate3_cubic;
    break;
  case 4:
    if (colorTable->fasterColorMethod == NAME_Tetrahedral)
      colorTable->interpolatefunc = gst_interpolate4_tetrahedral;
    else
      colorTable->interpolatefunc = gst_interpolate4_cubic;
    break;
  default:
    if (colorTable->fasterColorMethod == NAME_Tetrahedral)
      colorTable->interpolatefunc = gst_interpolateN_tetrahedral;
    else
      colorTable->interpolatefunc = gst_interpolateN_cubic;
    break;
  }

#ifdef GST_DEBUG_VARIABLES
  colorTable->nLookupHits = 0 ;
  colorTable->nLookupMisses = 0 ;
  colorTable->nPtrsCacheHits = 0 ;
  colorTable->nPtrsCacheMisses = 0 ;
  colorTable->nPtrsCacheDuplicates = 0 ;
  colorTable->nVerified = 0 ;
  colorTable->nValidate = 0 ;
  colorTable->nValidateSum = 0 ;
  colorTable->nPopulate = 0 ;
  colorTable->nMemCubeN = 0 ;
  colorTable->nMemCube1 = 0 ;
  colorTable->nMemColor = 0 ;
  colorTable->nMemCornerPtrs = 0 ;
  colorTable->nMemTotal = 0 ;
  colorTable->nInterpolate = 0 ;
  colorTable->nInterpolateError = 0 ;
#endif

  gst = mm_alloc( mm_pool_color, sizeof(GSC_TABLE), MM_ALLOC_CLASS_COLOR_TABLE );
  if ( gst == NULL ) {
    mm_free(mm_pool_color, cornerPtrsAlloc1, sizeof(ptrsEntry *));
    gst_freeTable(&colorTable);
    (void)error_handler( VMERROR );
    return NULL;
  }
  /* Setup colorTable pointer. This is done before struct is added to linked
     list. Also store whether we are in the interpreter thread, and thus
     creating a table for use in the frontEnd, or not. This allows us to
     use different levels of concurrency control for the two cases. */
  gst->colorTable = colorTable ;
  gst->colorState = colorInfo->colorState ;
  gst->frontEnd   = (gst->colorState == frontEndColorState);

  if ( colorInfo->colorState->tomsTables == NULL ) {
    if ( !gsc_tomstables_start(colorInfo->colorState) ) {
      mm_free(mm_pool_color, gst, sizeof(GSC_TABLE));
      mm_free(mm_pool_color, cornerPtrsAlloc1, sizeof(ptrsEntry *));
      gst_freeTable(&colorTable);
      (void)error_handler( VMERROR );
      return NULL;
    }
  }

  /* The GSC_TABLE structures go into a global linked list. This is so that they
   * can be used in low memory handling. Later on we may add the CLINK id slots
   * to this structure and manage them like the color cache (which means that
   * they can get reused between images).
   *
   * This global linked list is stored in the colorState, and can be accessed
   * my multiple threads. But the interpreter and rendering threads work off
   * different colorStates, so have no contentions. So the only issue is
   * having multiple rendering threads contending over the same linked list.
   * But there is a high-level color-conversion mutex preventing multiple
   * rendering threads using this list at the same time. So all that remains
   * as a problem is contention between a rendering thread and a  low-memory
   * action. But the only thing the rendering thread can do is add a new entry
   * to the head of the list. While the low-memory actions need to iterate
   * through the list, starting at the head. There is no problem between these
   * two possible actions, provided read/writes to the head of the list are
   * atomic.
   */
  gst->next = colorInfo->colorState->tomsTables->head ;
  /**
   * \todo BMJ 01-Jun-12 : Need to keep these assignnments in order on a
   * relaxed memory order platform. Currently OK as protected at a higher
   * level by the colcvtLock. Need to introduce a MemoryBarrier-like macro
   * for this, or use thread-safe linked lists or some such.
   */
  colorInfo->colorState->tomsTables->head = gst ;

  return gst ;
}

/**
 * A mutex for access to list of tomsTables
 *
 * \todo BMJ 01-Aug-12 : Mutex used for backEnd control currently means that
 * when a TomsTable is being invoked other TomsTables cannot be purged. Need
 * to re-structure code to remove this backEnd restriction.
 */
static multi_mutex_t gst_mutex;

/**
 * This routine is used to destroy a table.
 */
void cc_destroyTomsTable( GSC_TABLE *gst )
{
  COLOR_STATE *colorState;
  colorTableNd *colorTable ;
  GSC_TABLE **gstRef ;
  Bool frontEnd;

  HQASSERT( gst , "gst NULL" ) ;
  frontEnd = gst->frontEnd;

  colorState = gst->colorState;
  HQASSERT(colorState->tomsTables != NULL, "Missing Tom's Tables state");

  /*
   * Destroying a frontEnd table is not a problem, as no other threads can be
   * currently accessing either this table or the linked-list of TomsTables
   * in which it lives. But for the backEnd, either the table or the linked
   * list in which it lives might currently be in use by another rendering
   * thread. So have to take the mutex to avoid any contentions issues.
   * Just hold it for the duration of this destroy call.
   */
  if ( !frontEnd )
    multi_mutex_lock(&gst_mutex);

  colorTable = gst->colorTable;

#ifdef GST_EVAL_INT_ERROR
  monitorf((uint8 *)"Interpolation errors: (%d, %d)\n",
           colorTable->nInterpolateError, colorTable->nInterpolate ) ;
#endif
#ifdef GST_EVAL_LOOKUPS
  monitorf((uint8 *)" Cached / Non-cached lookups, colors populated: (%d, %d, %d)\n",
           colorTable->nLookupHits, colorTable->nLookupMisses, colorTable->nPopulate ) ;
  monitorf((uint8 *)" Corner pointers cache Hits, Misses, Duplicates: (%d, %d, %d)\n",
           colorTable->nPtrsCacheHits, colorTable->nPtrsCacheMisses, colorTable->nPtrsCacheDuplicates ) ;
#endif
#ifdef GST_EVAL_MEM_TOTAL
  monitorf((uint8 *)"Memory total: (%d)\n", colorTable->nMemTotal ) ;
  monitorf((uint8 *)"Memory cubes Nd/1d: (%d, %d)\n",
           colorTable->nMemCubeN, colorTable->nMemCube1 ) ;
  monitorf((uint8 *)"Memory color colors/pointers: (%d, %d)\n",
           colorTable->nMemColor, colorTable->nMemCornerPtrs ) ;
#endif
#ifdef GST_EVAL_MEM_POOLS
  monitorf((uint8 *)"Pool memory: (%d, %d)\n",
           mm_pool_size( colorTable->mm_pool_table ),
           mm_pool_free_size( colorTable->mm_pool_table ));
#endif

#if defined(METRICS_BUILD)
    { /* Track peak memory allocated in all pools in the list. The method of
       * adding the peak memory used by all color table pools may not be strictly
       * accurate, but it's something like the best we can do.
       */
      GSC_TABLE *tgst = colorState->tomsTables->head;
      size_t total_max_size = 0, all_max_frag = 0;
      int32 total_max_objects = 0;

      while (tgst != NULL) {
        size_t max_size = 0, max_frag = 0;
        int32 max_objects ;

        mm_debug_total_highest(tgst->colorTable->mm_pool_table,
                               &max_size, &max_objects, &max_frag);
        total_max_size += max_size; total_max_objects += max_objects;
        if ( all_max_frag < max_frag )
          all_max_frag = max_frag;
        tgst = tgst->next ;
      }
      if (gst_metrics.gst_pool_max_size < total_max_size) {
        gst_metrics.gst_pool_max_size = total_max_size;
        gst_metrics.gst_pool_max_objects = total_max_objects;
      }
      if (gst_metrics.gst_pool_max_frag < all_max_frag)
        gst_metrics.gst_pool_max_frag = all_max_frag;
    }
#endif

  for ( gstRef = &colorState->tomsTables->head;
        *gstRef != NULL && *gstRef != gst;
        gstRef = &(*gstRef)->next )
    EMPTY_STATEMENT();
  HQASSERT( *gstRef == gst , "Somehow did not find gst" ) ;
  *gstRef = (*gstRef)->next;

  mm_free(mm_pool_color, /* The small cache can be in either slot, see alloc. */
          colorTable->cornerPtrsCacheBits > 0
          ? colorTable->cornerPtrsFallback
          : colorTable->cornerPtrsCache,
          sizeof(ptrsEntry *));
  gst_freeTable(&colorTable);
  mm_free( mm_pool_color, gst, sizeof(GSC_TABLE) );

  if ( colorState->tomsTables->head == NULL )
    gsc_tomstables_finish(colorState);

  if ( !frontEnd )
    multi_mutex_unlock(&gst_mutex);
}

/**
 * Routine to determine whether a color is cached in the cornerPtrs cache,
 * and if not create the necessary cache entry.
 * Used to be first half of bigger function gst_interpolateColorN(),
 * but split into sub-functions for better performance.
 */
static Bool gst_iscachedColorN(colorTableNd *colorTable, Bool *ok)
{
  Bool cacheHit = FALSE;
  ptrsEntry *cachedColor;

  if (colorTable->cornerPtrsCacheBits == 0) {
    /* There is no point checking any further because if the mini-cube were the
     * same as the last used one, then we would already know because that info
     * is returned by gst_getIndicesNoCache() and friends.
     */
    HQASSERT(colorTable->indicesId == INDICES_ID_INVALID &&
             colorTable->cornerPtrsCacheSize == 1 &&
             colorTable->indicesHash == 0,
             "cornerPtrsCache inconsistent");

    /* If 'cornerPtrs' is NULL, this must be the first time this function has been
     * called so we'll allow the remainder of the function to be executed for
     * the allocation of 'cornerPtrs'.
     */
    if (colorTable->cornerPtrs != NULL)
      return FALSE;

    HQASSERT(colorTable->indicesId == INDICES_ID_INVALID, "Unexpected indicesId");
  }
  else {
    HQASSERT(colorTable->indicesId != INDICES_ID_INVALID &&
             colorTable->cornerPtrsCacheSize != 1 &&
             colorTable->indicesHash < colorTable->cornerPtrsCacheSize,
             "cornerPtrsCache inconsistent");
  }

  cachedColor = colorTable->cornerPtrsCache[colorTable->indicesHash];
  if ( cachedColor != NULL ) {
    if (cachedColor->id == colorTable->indicesId) {
      /* If we get here, we want to ensure that the cube hasn't failed the
       * linearity test. The id is set to an invalid value below to force
       * a failure when we return to here in a future test.
       */
      cacheHit = TRUE;
      DEBUG_INCREMENT(colorTable->nPtrsCacheHits, 1) ;
    }
    else
      DEBUG_INCREMENT(colorTable->nPtrsCacheDuplicates, 1) ;
  } else {
    DEBUG_INCREMENT(colorTable->nPtrsCacheMisses, 1) ;

    cachedColor = gst_allocCornerPtrsEntry(colorTable, colorTable->incomps);

    if (colorTable->cornerPtrsCache == colorTable->cornerPtrsFallback) {
      /* Four cases to consider, colorTable->cornerPtrsCache[colorTable->indicesHash] &
       * cachedColor may both be NULL or non-NULL.
       */
      if (cachedColor != NULL) {
        if (colorTable->cornerPtrsCache[colorTable->indicesHash] != NULL)
          gst_freeCornerPtrsEntry(colorTable,
                                  &colorTable->cornerPtrsCache[colorTable->indicesHash],
                                  colorTable->incomps);
      }
      else
        cachedColor = colorTable->cornerPtrsCache[colorTable->indicesHash];
    }

    if (cachedColor == NULL) {
      *ok = FALSE;
      (void) error_handler(VMERROR);
      return FALSE;
    }

    colorTable->cornerPtrsCache[colorTable->indicesHash] = cachedColor;
  }

  HQASSERT(cachedColor->cornerPtrs, "cornerPtrs NULL");
  colorTable->cornerPtrs = cachedColor->cornerPtrs ;
  return cacheHit;
}

/**
 * Routine to calculate a color value if is was not found in the color cache.
 * Used to be second half of bigger function gst_interpolateColorN(),
 * but split into sub-functions for better performance.
 */
static Bool gst_doColorN(colorTableNd *colorTable,
                         GS_COLORinfo *colorInfo,
                         int32 colorType,
                         COLORVALUE *poColorValues)
{
  colorNd   *color;
  validMask *valid;

  HQASSERT( colorInfo , "colorInfo NULL" ) ;

  /* This identifies the required mini-cube corners and ensures that colors are
   * present in all of them before moving on.
   */
  if ( !gst_getInterpolateColorsN(colorTable, colorInfo, colorType,
                                  &color, &valid) ) {
    return FALSE;
  }

  HQASSERT((*valid & GST_COLOR_PRESENT) != 0,
           "GST_COLOR_PRESENT should be set");
  HQASSERT(((*valid & GST_CUBE_TESTED) != 0 && (*valid & GST_CUBE_LINEAR) == 0)
             || colorTable->cornerPtrsCacheBits == 0 ||
                colorTable->cornerPtrs[0] == color->color,
           "inconsistent cornerPtrs (with the pointers cache active)");
  HQASSERT((*valid & ~GST_VALID_MASK) == 0, "validity mask corrupt");


  colorTable->cornerPtrsCache[colorTable->indicesHash]->id = colorTable->indicesId;

  /* Check validity of interpolation. */
  if ( (*valid & GST_CUBE_LINEAR) == 0 ) {
    /* Verify that either interpolation is valid or have tried before and
     * failed.
     */
    if ( (*valid & GST_CUBE_TESTED) == 0) {
      /* Not checked validity of interpolation yet, so go do so for images with
       * 1 - 4 components.
       * What we need to do is to transform the center point of the cube, and
       * check the result of that against interpolation. We use poColorValues
       * to store the temporary result of transforming the center point of the
       * cube.
       * We're not going to do this for 5+ components because the cost of
       * evaluating all grid points is prohibitive to the point that no-one
       * will want to do it.
       */
      if (colorTable->fasterColorSmoothness < 1.0 && colorTable->incomps <= 4) {
        if ( !gst_evaluateCentreN(colorTable, colorInfo, colorType,
                                  poColorValues ))
          return FALSE;

        if ( gst_validateColorsN(colorTable, poColorValues))
          *valid |= GST_CUBE_LINEAR;
      }
      else
        *valid |= GST_CUBE_LINEAR;

      *valid |= GST_CUBE_TESTED;
    }

    if ( (*valid & GST_CUBE_LINEAR) == 0 ) {
#ifdef ACCURATE_SUB_MINI_CUBES
      /* This section is reserved for a possible future enhancement where a
       * non-linear mini-cube could be more accurately evaluated by making use
       * of the central color which we now know. It will be trickier, but we do
       * have twice the information so we could interpolate more accurately.
       */
#endif
    }
  }
  return TRUE;
}

/**
 * Do color conversion using TomsTable methodology.
 *
 * Generic version with arbitrary parameters. This has been specialised
 * to various typical cases by repliacting the code and removing the
 * unnecessary conditionals.
 *
 * \todo BMJ 13-Jul-09 : Work out and better way of maintaining any duplicates.
 */
static Bool do_tt_generic(colorTableNd *colorTable, GS_COLORinfo *colorInfo,
                          int32 colorType, int32 *in, COLORVALUE *out,
                          int32 ncolors, int32 incomps, int32 oncomps,
                          Bool (*ifunc)(colorTableNd *, COLORVALUE *),
                          Bool (*gfunc)(colorTableNd *, int32 *))
{
  int32 i, bad_vals[4], *last;
  Bool ok = TRUE;

  /* force 1st comparison to fail to avoid needing test inside loop */
  bad_vals[0] = in[0] - 1;
  last = bad_vals;

  /* Save the N structure every time we're called because it's required when
   * caching colour chains and sharing them between colorInfo's.
   */
  if (incomps > 4) {
    colorTable->N.colorInfo = colorInfo;
    colorTable->N.colorType = colorType;
  }

  do {
    Bool useLast = FALSE;

    /* Check for the previous input color being the same as this one; if it is
     * we can just copy the previous result. */
    switch ( incomps ) {
      case 4:
        useLast = (last[0] == in[0] && last[1] == in[1] &&
                   last[2] == in[2] && last[3] == in[3]);
        break;
      case 3:
        useLast = (last[0] == in[0] && last[1] == in[1] && last[2] == in[2]);
        break;
      case 2:
        useLast = (last[0] == in[0] && last[1] == in[1]);
        break;
      case 1:
        useLast = (last[0] == in[0]);
        break;
    }

    if ( useLast ) {
      for (i = 0; i < oncomps; i ++)
        out[i] = out[i - oncomps];
    } else {
      /* Calculate the indices required for the mini-cube, then go get
       * all the points in the mini-cube & interpolate.
       */
      if ( !(*gfunc)(colorTable, in) && !gst_iscachedColorN(colorTable, &ok) ) {
        if ( ok ) {
          DEBUG_INCREMENT(colorTable->nLookupMisses, 1);

          ok = gst_doColorN(colorTable, colorInfo, colorType, out);
        }
        if ( !ok )
          break;
      }
      else
        DEBUG_INCREMENT(colorTable->nLookupHits, 1);

#ifdef GST_EVAL_INT_ERROR
        if ( !gst_evaluateColorN(colorTable, colorInfo, colorType, in, out) ) {
          ok = FALSE;
          break;
        }
#endif

      ok = (*ifunc)(colorTable, out); /* Then interpolate them. */
      if ( !ok )
        break;
    }
    last = in;
    in += incomps;
    out += oncomps;
  } while (--ncolors > 0 );
  return ok;
}

/**
 * Specialised version of the generic TomsTable code, for the case of
 * incomps == 3, oncomps ==4, tertahedral interpolation, and 4bit corner cache.
 *
 * \todo BMJ 13-Jul-09 :  Share code in some way with generic routine
 */
static Bool do_tt_34tet4(colorTableNd *colorTable, GS_COLORinfo *colorInfo,
                         int32 colorType, int32 *in, COLORVALUE *out,
                         int32 ncolors)
{
  int32 bad_vals[3], *last;

  HQASSERT(colorTable->cornerPtrsCacheBits == 4, "Invalid cornerPtrsCacheBits");

  /* force 1st comparison to fail to avoid needing test inside loop */
  bad_vals[0] = in[0] - 1;
  last = bad_vals;

  do {
    if ( last[0] == in[0] && last[1] == in[1] && last[2] == in[2] ) {
      out[0] = out[-4];
      out[1] = out[-3];
      out[2] = out[-2];
      out[3] = out[-1];
    } else {
      int32 icrdiff, indicesHash, index, value, *indices, *fractns;
      uint32 indicesId;

      indices = colorTable->indices;
      fractns = colorTable->fractns;

      value = in[0];
      index = value >> GSC_FRACNSHIFT;
      icrdiff = ( indices[0] - index );
      indicesId = index;
      indicesHash = index & 0xf;
      indices[0] = index;
      fractns[0] = value & GSC_FRACNMASK;

      value = in[1];
      index = value >> GSC_FRACNSHIFT;
      icrdiff |= ( indices[1] - index );
      indicesId <<= 4;
      indicesId += index;
      indicesHash <<= 4;
      indicesHash += index & 0xf;
      indices[1] = index;
      fractns[1] = value & GSC_FRACNMASK;

      value = in[2];
      index = value >> GSC_FRACNSHIFT;
      icrdiff |= ( indices[2] - index );
      indicesId <<= 4;
      indicesId += index;
      indicesHash <<= 4;
      indicesHash += index & 0xf;
      indices[2] = index;
      fractns[2] = value & GSC_FRACNMASK;

      colorTable->indicesId = indicesId;
      colorTable->indicesHash = indicesHash;

#ifdef ASSERT_BUILD
      if (icrdiff != 0) {
        int32 i;

        for ( i = 0; i < 3; i++ ) {
          colorTable->incIndices[i] = -1;
          colorTable->tmpIndices[i] = -1;
          colorTable->tmpIncIndices[i] = -1;
        }
      }
#endif

      if ( icrdiff != 0 ) {
        Bool ok = TRUE;

        /*
         * Following routines may alloc and thus invoke a low memory purge,
         * which may blow away some or all of the cornerPtrsCache.
         * This would invalidate the pre-requisites under which this
         * specialised routine was called. So if it happens, fail and get the
         * higher level code to retry with the more generic version of
         * this code.
         */
        if ( !gst_iscachedColorN(colorTable, &ok) ) {
          if ( ok )
            ok = gst_doColorN(colorTable, colorInfo, colorType, out);
          if ( !ok )
            return FALSE;
        }
        if ( colorTable->cornerPtrsCacheBits != 4 ) /* we have had a purge */
          return error_handler(VMERROR); /* fail and retry with generic version */
      }
      {
        COLORVALUE  *v1, *v2, *v3, *v4;
        int32       fac1, fac2, fac3, fac4;
        int32       *fractns = colorTable->fractns;
        int32       xf = fractns[0], yf = fractns[1], zf = fractns[2];
        uint32      a0, tetrahedron = 0;
        COLORVALUE  **cp = colorTable->cornerPtrs;

        if ( xf >= yf )
          tetrahedron |= 1;
        if ( xf >= zf )
          tetrahedron |= 2;
        if ( yf >= zf )
          tetrahedron |= 4;

        switch ( tetrahedron ) {
          case (1 | 2 | 4) :
            fac1 = (1 << GST_IFRACBITS) - xf;
            fac2 = xf - yf;
            fac3 = yf - zf;
            fac4 = zf;
            v1 = cp[0];
            v2 = cp[1];
            v3 = cp[3];
            v4 = cp[7];
            break;
          case (1 | 2) :
            fac1 = (1 << GST_IFRACBITS) - xf;
            fac2 = xf - zf;
            fac3 = zf - yf;
            fac4 = yf;
            v1 = cp[0];
            v2 = cp[1];
            v3 = cp[5];
            v4 = cp[7];
            break;
          case (1) :
            fac1 = (1 << GST_IFRACBITS) - zf;
            fac2 = zf - xf;
            fac3 = xf - yf;
            fac4 = yf;
            v1 = cp[0];
            v2 = cp[4];
            v3 = cp[5];
            v4 = cp[7];
            break;
          case (0) :
            fac1 = (1 << GST_IFRACBITS) - zf;
            fac2 = zf - yf;
            fac3 = yf - xf;
            fac4 = xf;
            v1 = cp[0];
            v2 = cp[4];
            v3 = cp[6];
            v4 = cp[7];
            break;
          case (4) :
            fac1 = (1 << GST_IFRACBITS) - yf;
            fac2 = yf - zf;
            fac3 = zf - xf;
            fac4 = xf;
            v1 = cp[0];
            v2 = cp[2];
            v3 = cp[6];
            v4 = cp[7];
            break;
          case (2 | 4) :
            fac1 = (1 << GST_IFRACBITS) - yf;
            fac2 = yf - xf;
            fac3 = xf - zf;
            fac4 = zf;
            v1 = cp[0];
            v2 = cp[2];
            v3 = cp[3];
            v4 = cp[7];
            break;
          default :
            HQFAIL("Unexpected tetrahedron");
            return FALSE;
        }
        a0 = v1[ 3 ] * fac1 + v2[ 3 ] * fac2 + v3[ 3 ] * fac3 + v4[ 3 ] * fac4;
        out[ 3 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
        a0 = v1[ 2 ] * fac1 + v2[ 2 ] * fac2 + v3[ 2 ] * fac3 + v4[ 2 ] * fac4;
        out[ 2 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
        a0 = v1[ 1 ] * fac1 + v2[ 1 ] * fac2 + v3[ 1 ] * fac3 + v4[ 1 ] * fac4;
        out[ 1 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
        a0 = v1[ 0 ] * fac1 + v2[ 0 ] * fac2 + v3[ 0 ] * fac3 + v4[ 0 ] * fac4;
        out[ 0 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
      }
    }
    last = in;
    in += 3;
    out += 4;
  } while (--ncolors > 0 );
  return TRUE;
}

/**
 * Thread-safe version of invoking Toms Table code, protected in the
 * appropriate manner (mutex for rendering threads, beingused for
 * interpreter)
 */
static Bool safe_invokeTomsTable(GS_COLORinfo *colorInfo, int32 colorType,
                                 int32 *piColorValues,
                                 COLORVALUE *poColorValues,
                                 int32 ncolors,
                                 colorTableNd *colorTable)
{
  Bool result  = TRUE, use_generic = TRUE;
  Bool (*getIndicesFunc)(colorTableNd *colorTable, int32 *piColorValues) = NULL;
  Bool (*interpolatefunc)(struct colorTableNd *colorTable,
                          COLORVALUE *poColorValues);
  int32 incomps, oncomps;

  HQASSERT(colorInfo, "colorInfo NULL");
  HQASSERT(piColorValues, "piColorValues NULL");
  HQASSERT(poColorValues, "poColorValues NULL");
  HQASSERT(ncolors > 0, "ncolors should be > 0");
  HQASSERT(colorTable->indicesHash < colorTable->cornerPtrsCacheSize,
           "indicesHash out of range");

  incomps = colorTable->incomps ;
  oncomps = colorTable->oncomps ;
  interpolatefunc = colorTable->interpolatefunc;

  probe_begin(SW_TRACE_INTERPRET_TOMSTABLE, 0);

  /* We're going to select the optimal method for obtaining the 'indices' into
   * the current mini-cube.
   * NB. In the do..while loop below it's possible that cornerPtrsCacheBits will
   * get reset to zero in low memory handling, but it doesn't matter if this
   * function doesn't change within the loop because gst_getIndicesNoCache() is
   * simply more optimal for that case.
   */
  getIndicesFunc = gst_getIndicesNoCache;
  if (colorTable->cornerPtrsCacheBits != 0) {
    switch (incomps) {
    case 1: getIndicesFunc = gst_getIndicesWithCache1C; break;
    case 2: getIndicesFunc = gst_getIndicesWithCache2C; break;
    case 3: getIndicesFunc = gst_getIndicesWithCache3C; break;
    case 4: getIndicesFunc = gst_getIndicesWithCache4C; break;
    default:
      HQFAIL("Unexpected number of components");
      return FALSE;
    }
  }

  if ( incomps == 3 && oncomps == 4 && colorTable->cornerPtrsCacheBits == 4 &&
       colorTable->cornerPtrsCacheMask == 0xf &&
       colorTable->cubeSideBits == 4 &&
       interpolatefunc == gst_interpolate3_tetrahedral ) {
    use_generic = FALSE;
    result = do_tt_34tet4(colorTable, colorInfo, colorType, piColorValues,
                           poColorValues, ncolors);

    /* Specialised code can fail for one of three reasons :-
     *  a) cornerPtrsCache changed size and specialised code cannot cope
     *  b) Ran out of memory and generated a VMERROR
     *  c) Generated some other sort of error
     *
     * Want to retry with the generic code in case a), But rather than doing
     * a slow involved test to isolate this case, its easier just to clear
     * the error and retry with the generic code whenever we have an error.
     * This minimises the critical path tests and lets the generic code do all
     * the error handling, which it is good at.
     */
    if ( !result ) {
      if ( error_latest() == VMERROR  ) {
        error_clear();
        use_generic = TRUE; /* retry with generic routine */
        INVALIDATE_COLORCUBE_CACHE_LOOKUP(colorTable);
      }
    }
  }
  if ( use_generic )
    result = do_tt_generic(colorTable, colorInfo, colorType, piColorValues,
                           poColorValues, ncolors, incomps, oncomps,
                           interpolatefunc, getIndicesFunc);

  probe_end(SW_TRACE_INTERPRET_TOMSTABLE, 0);

  /* If there was an error need to invalidate color cube cache lookup */
  if ( !result )
    INVALIDATE_COLORCUBE_CACHE_LOOKUP(colorTable);

  return result;
}

/**
 * This routine is used to convert a block of data using the table.
 * The input and output data is assumed to be stored in 32 bit containers
 * in a pixel interleaved format.
 */
Bool cc_invokeTomsTable(GS_COLORinfo *colorInfo, int32 colorType,
                        int32 *piColorValues,
                        COLORVALUE *poColorValues,
                        int32 ncolors,
                        GSC_TABLE *gst)
{
  colorTableNd *colorTable;
  Bool ok;

  HQASSERT(gst, "gst NULL");
  colorTable = gst->colorTable;
  HQASSERT(colorTable , "colorTable somehow NULL");
  if ( gst->frontEnd ) {
    colorTable->beingused = TRUE;
  } else {
    multi_mutex_lock(&gst_mutex);
  }
  ok = safe_invokeTomsTable(colorInfo, colorType, piColorValues,
                            poColorValues, ncolors, colorTable);
  if ( gst->frontEnd ) {
    colorTable->beingused = FALSE;
  } else {
    multi_mutex_unlock(&gst_mutex);
  }
  return ok;
}

Bool gst_do_offer(COLOR_STATE *cs, low_mem_offer_t *table_offer)
{
  size_t needed, purged = 0;
  Bool frontEnd;
  GSC_TABLE *gst;

  HQASSERT(table_offer != NULL && table_offer->next == NULL, "Bad offer");
  needed = table_offer->taken_size;

  /* Have to take the gst mutex before we can examine the linked list of
   * TomsTables. This is to read the "frontEnd" status to see if we lock
   * is actually required or not. If not needed we can release it immeditaley,
   * else hold it for the duration of the purge to prevent a destroy removing
   * a table from the linked list as we are iterating. Do a trylock and if
   * this fails then refuse to do any purging.
   */
  if ( !multi_mutex_trylock(&gst_mutex) )
    return TRUE;
  if ( cs->tomsTables == NULL || cs->tomsTables->head == NULL ) {
    multi_mutex_unlock(&gst_mutex);
    return TRUE;
  }
  frontEnd = cs->tomsTables->head->frontEnd;
  if ( frontEnd ) /* Don't actually need the lock as no contentions possible */
    multi_mutex_unlock(&gst_mutex);

  for ( gst = cs->tomsTables->head; gst != NULL && purged < needed;
        gst = gst->next ) {
    colorTableNd *colorTable = gst->colorTable;
    /* If asked for a segment or more, assume it's for another pool
       and count managed size, otherwise alloced size. */
    size_t (*size_fn)(mm_pool_t pool) = needed >= MM_SEGMENT_SIZE ?
                                        mm_pool_size : mm_pool_alloced_size;

    do { /* Purge this table, until purged enough or all. */
      Bool preserveInUseValues = frontEnd ?  colorTable->beingused : FALSE;
      mm_pool_t pool = colorTable->mm_pool_table;
      size_t size_before = size_fn(pool);
      size_t alloc_before = mm_pool_alloced_size(pool);
      /* Purge at least 1/10, so won't loop excessively. */
      gst_purgeColorN(colorTable, needed - purged < size_before ?
                      max((double)(needed - purged) / size_before, 0.1) : 1.0,
                      preserveInUseValues);
      purged += size_before - size_fn(pool);
      colorTable->purgeable = (alloc_before != mm_pool_alloced_size(pool));
    } while ( purged < needed && colorTable->purgeable );
  }
  if ( !frontEnd )
    multi_mutex_unlock(&gst_mutex);
  return TRUE;
}

/**
 * Calculate and return a low-memory offer for purging the TomsTable list.
 */
/*
 * Difficult to have the offer structures embedded in the TomsTable list as
 * it is possible for entries to be destroyed between the offer and the
 * release. Could have a separate offer list with an independent lifetime,
 * but then you have the problem of lining offer and table entries back-up
 * again at release time. So just take the simplest approach and have a single
 * static offer structure used by all threads. No contention issues as only one
 * low-mem can be in progress at one time. Sum the freeable memory available
 * into a single total, but keep the offered pool entry NULL.
 */
static low_mem_offer_t *gst_calc_offer(COLOR_STATE *cs,
                                       memory_requirement_t* requests)
{
  static low_mem_offer_t gst_offer;
  Bool frontEnd;
  GSC_TABLE *gst;

  if ( cs == NULL )
    return NULL;
  gst_offer.offer_size = 0;
  gst_offer.pool = NULL;
  gst_offer.offer_cost = 20.0f;
  gst_offer.next = NULL;

  /* Hold the mutex for the duration of the TomsTable list iteration,
   * unless it turns out it is a frontEnd list, in which case we can release
   * the lock immediately. Do a trylock to take the lock, and if we fail then
   * do not allow a purge.
   */
  if ( !multi_mutex_trylock(&gst_mutex) )
    return NULL;
  if ( cs->tomsTables == NULL || cs->tomsTables->head == NULL ) {
    multi_mutex_unlock(&gst_mutex);
    return NULL;
  }
  frontEnd = cs->tomsTables->head->frontEnd;
  if ( frontEnd )
    multi_mutex_unlock(&gst_mutex);

  for ( gst = cs->tomsTables->head; gst != NULL; gst = gst->next ) {
    colorTableNd *colorTable = gst->colorTable;
    Bool freeWholeSeg = FALSE;

    HQASSERT( colorTable != NULL, "colorTable somehow NULL" );
    if ( frontEnd ) {
      freeWholeSeg = !colorTable->beingused;
    } else {
      /* The release will free the entire segment, but not if the table is
       * in use if we eventually action this offer. Table may or may not be in
       * use now, but don't know what state it will be in at release time.
       * Assume tables are unlocked more often that locked, so guess the
       * whole segment will be freed.
       */
      freeWholeSeg = TRUE;
    }
    if ( colorTable->purgeable
         /* If can't free seg, no point offering for any except same pool. */
         && (freeWholeSeg || requests[0].pool == colorTable->mm_pool_table)
         && ((colorTable->cornerPtrsCache != NULL
              && colorTable->cornerPtrsCacheBits != 0)
             || colorTable->cubemru != NULL) ) {
      /* When purge can release entire segments to other pools, it's
         appropriate to offer the pool free space as well. */
      gst_offer.offer_size += freeWholeSeg ?
                    mm_pool_size(colorTable->mm_pool_table) :
                    mm_pool_alloced_size(colorTable->mm_pool_table);
    }
  }
  if ( !frontEnd )
    multi_mutex_unlock(&gst_mutex);
  return gst_offer.offer_size > 0 ? &gst_offer : NULL;
}


/** Solicit method of the front-end color tables low-memory handler. */
static low_mem_offer_t *gst_solicit(low_mem_handler_t *handler,
                                    corecontext_t *context,
                                    size_t count,
                                    memory_requirement_t* requests)
{
  GSC_TOMSTABLES *tomsTables;

  HQASSERT(context != NULL, "No context");
  UNUSED_PARAM(size_t, count);
  UNUSED_PARAM(memory_requirement_t*, requests);

  /* Find address of containing parent structure in which the handler is
     embedded */
  HQASSERT(handler, "No Tom's Tables handler");
  tomsTables = (GSC_TOMSTABLES *)((char *)handler -
                                  offsetof(GSC_TOMSTABLES, lowmem_handler));
  VERIFY_OBJECT(tomsTables, TOMSTABLES_NAME);

  if ( !context->is_interpreter && tomsTables->colorState == frontEndColorState )
    /* The table list is not thread-safe, but only the interpreter
       thread uses it. */
    return NULL;

  return gst_calc_offer(tomsTables->colorState, requests);
}


/** Release method of the color tables low-memory handler. */
static Bool gst_release(low_mem_handler_t *handler,
                        corecontext_t *context,
                        low_mem_offer_t *offer)
{
  GSC_TOMSTABLES *tomsTables;

  UNUSED_PARAM(corecontext_t*, context);

  HQASSERT(handler, "No Tom's Tables handler");
  tomsTables = (GSC_TOMSTABLES *)((char *)handler -
                                  offsetof(GSC_TOMSTABLES, lowmem_handler));
  VERIFY_OBJECT(tomsTables, TOMSTABLES_NAME);

  return gst_do_offer(tomsTables->colorState, offer);
}


/* Called lazily when the first GS_TABLE is created. */
static Bool gsc_tomstables_start(COLOR_STATE *colorState)
{
  const static low_mem_handler_t gst_low_handler = {
    "color table purge",
    memory_tier_ram, gst_solicit, gst_release, TRUE,
    0, FALSE };

  HQASSERT(colorState->tomsTables == NULL, "Tom's Tables already started");

  colorState->tomsTables = mm_alloc(mm_pool_color, sizeof(GSC_TOMSTABLES),
                                    MM_ALLOC_CLASS_COLOR_TABLE);
  if ( colorState->tomsTables == NULL )
    return error_handler(VMERROR);

  colorState->tomsTables->lowmem_handler = gst_low_handler;
  colorState->tomsTables->colorState = colorState;
  colorState->tomsTables->head = NULL;
  NAME_OBJECT(colorState->tomsTables, TOMSTABLES_NAME);

  if ( !low_mem_handler_register(&colorState->tomsTables->lowmem_handler) ) {
    gsc_tomstables_finish(colorState);
    return FALSE;
  }

  return TRUE;
}


/* Called lazily when the last GS_TABLE is freed. */
static void gsc_tomstables_finish(COLOR_STATE *colorState)
{
  if ( colorState->tomsTables != NULL ) {
    HQASSERT(colorState->tomsTables->head == NULL,
             "Not all Tom's Tables have been freed");
    low_mem_handler_deregister(&colorState->tomsTables->lowmem_handler);
    UNNAME_OBJECT(colorState->tomsTables);
    mm_free(mm_pool_color, colorState->tomsTables, sizeof(GSC_TOMSTABLES));
    colorState->tomsTables = NULL;
  }
}


/**
 * Purge table data. It marks each 1-D array in
 * the table with a time stamp and removes the last used fraction of them with
 * the caveat that if the low memory handler was called whilst the table is
 * being used higher in the call stack, then required data is protected from
 * being purged.
 */
static void gst_purgeColorN(colorTableNd *colorTable, double fraction_to_purge,
                            Bool preserveInUseValues)
{
  uint32 i;

  HQASSERT( colorTable != NULL , "colorTable NULL" ) ;

  /* Step 1.
   * Free the existing cornerPtrs cache and replace it with cornerPtrsFallback
   * which contains just a single entry to keep life relatively simple. Not that
   * it is simple because anywhere an allocation is done after opening a table,
   * we have to bear in mind the state changed here could upset us. In particular,
   * we have to be careful to preserve colorTable->cornerPtrs in
   * gst_getInterpolateColorsN().
   * NB. This could be made more subtle by incrementally reducing the size of the
   * cache but will be hard to get right because of the necessity to preserve a
   * lot more cornerPtrs arrays when transferring data to the new cache.
   */
  if ( colorTable->cornerPtrsCache != NULL && colorTable->cornerPtrsCacheBits != 0 ) {
    ptrsEntry *cachedColor = NULL;

    /* Preserve colorTable->cornerPtrs (if it's non-NULL) */
    if (colorTable->cornerPtrs != NULL) {
      uint32 indicesHash = colorTable->indicesHash;
      HQASSERT(colorTable->cornerPtrsCache[indicesHash] == NULL ||
               colorTable->cornerPtrsCache[indicesHash]->cornerPtrs == colorTable->cornerPtrs,
               "Unexpected colorTable->cornerPtrs");
      cachedColor = colorTable->cornerPtrsCache[indicesHash];
      colorTable->cornerPtrsCache[indicesHash] = NULL;
    }

    for (i = 0; i < colorTable->cornerPtrsCacheSize; i++) {
      if (colorTable->cornerPtrsCache[i] != NULL)
        gst_freeCornerPtrsEntry(colorTable,
                                &colorTable->cornerPtrsCache[i],
                                colorTable->incomps);
    }
    mm_free(colorTable->mm_pool_table,
            colorTable->cornerPtrsCache,
            sizeof(ptrsEntry *) * colorTable->cornerPtrsCacheSize);

    colorTable->cornerPtrsCacheBits = 0;
    colorTable->cornerPtrsCacheSize = 1 << (colorTable->incomps * colorTable->cornerPtrsCacheBits);
    colorTable->cornerPtrsCacheMask = (1 << colorTable->cornerPtrsCacheBits) - 1;
    HQASSERT(colorTable->cornerPtrsCacheBits == 0, "Must recalculate indicesHash");
    colorTable->indicesHash = 0;
    colorTable->indicesId = INDICES_ID_INVALID;

    colorTable->cornerPtrsCache = colorTable->cornerPtrsFallback ;
    for (i = 0; i < colorTable->cornerPtrsCacheSize; i++)
      colorTable->cornerPtrsCache[i] = NULL;

    /* Restore colorTable->cornerPtrs */
    if (colorTable->cornerPtrs != NULL)
      colorTable->cornerPtrsCache[0] = cachedColor;

    if ( !preserveInUseValues )
      INVALIDATE_COLORCUBE_CACHE_LOOKUP(colorTable) ;
    return;
  }

  /* Step 2.
   * Free up part of the table by freeing a fraction of the LRU 1d arrays. If,
   * as a result of this, upper 1d arrays become free, then free them as well.
   */
  if ( colorTable->cubemru ) {
    int32 *indices = colorTable->indices ;
    int32 *incIndices = colorTable->incIndices ;
    uint32 timestamp ;
    int32 j;

    /* First of all set the timestamps so we know what to delete.
     * Note the list of cubes is circular.
     */
    colorCube1d *c1h = colorTable->cubemru ;
    colorCube1d *c1  = c1h ;
    timestamp = 0u ;
    do {
      c1->timestamp = ++timestamp ;
      c1 = c1->next ;
    } while ( c1 != c1h ) ;

    if (preserveInUseValues) {
      /* We have to set the incremented indices to identify the colors and 1d
       * arrays that need preserving when we're purging a table whilst a color
       * conversion is in progress.
       * The incIndices might not be set if the cube corners cache is active,
       * or perhaps they're not set yet. But if they are set, then they should
       * be correct for this case.
       */
      for (j = 0; j < colorTable->incomps; j++) {
        int32 index = indices[j];

        HQASSERT(index >= 0 && index < colorTable->cubeSide,
                 "index out of range");
        INDEX_INCREMENT_AND_CLIP(index);
        HQASSERT(incIndices[j] == -1 || incIndices[j] == index,
                 "Inconsistent cube index");
        incIndices[j] = index;
      }
    }

    ( void )gst_purgeCube(colorTable, &colorTable->cube, colorTable->incomps,
                          (uint32)(timestamp * (1.0 - fraction_to_purge)),
                          preserveInUseValues);

    /* If we removed any data, then we need to invalidate the last mini-cube
     * used cache because it may be invalid now.
     */
    if ( !preserveInUseValues ) {
      INVALIDATE_COLORCUBE_CACHE_LOOKUP(colorTable) ;
    }
  }
}

/**
 * This routine recursively iterates over a cube, freeing any of the colors
 * and 1d arrays it can. If any colors don't get freed then all the 1d arrays
 * above it must be retained.
 */
static Bool gst_purgeCube(colorTableNd *colorTable,
                          colorCubeNd *pcube ,
                          int32 incomps ,
                          uint32 timestamp ,
                          Bool preserve)
{
  Bool retval ;
  int32 *indices ;
  int32 *incIndices ;

  HQASSERT( colorTable , "colorTable NULL in gst_purgeCube" ) ;
  HQASSERT( pcube , "pcube NULL in gst_purgeCube" ) ;
  HQASSERT( incomps >= SOLID_DIMS , "incomps should be >= SOLID_DIMS" ) ;

  indices = NULL ;
  incIndices = NULL ;
  if ( preserve ) {
    indices = colorTable->indices ;
    incIndices = colorTable->incIndices ;
  }

  retval = TRUE ;

  if ( incomps > SOLID_DIMS ) {
    int32 index ;
    for ( index = 0 ; index < colorTable->cubeSide ; ++index ) {
      colorCubeNd *tcube = &pcube->cn[ index ] ;
      Bool npreserve ;
      npreserve = preserve &&
                  ( indices[ incomps - 1 ] == index ||
                    incIndices[ incomps - 1 ] == index ) ;
      if ( npreserve )
        retval = FALSE ;
      if ( tcube->cn != NULL ) {
        /* gst_purgeCube returns FALSE if we did not remove everything, so must keep. */
        if ( ! gst_purgeCube( colorTable , tcube , incomps - 1 , timestamp , npreserve ))
          retval = FALSE ;
      }
    }
    /* If we freed everything off here, then we can free this too. */
    if ( retval ) {
      gst_freeCubeN( colorTable, pcube->cn, colorTable->cubeSide ) ;
      pcube->cn = NULL;
    }
  }
  else {
    colorCube1d *c1 ;
    c1 = pcube->c1 ;
    if ( c1 != NULL ) {
      if ( c1->timestamp < timestamp )
        retval = FALSE ;
      else {
        int32 index ;
        for ( index = 0 ; index < colorTable->cubeSide ; ++index ) {
          Bool npreserve ;
          colorNd *color ;
          colorNd **pcolor ;
          npreserve = preserve &&
                      ( indices[ incomps - 1 ] == index ||
                        incIndices[ incomps - 1 ] == index ) ;
          if ( npreserve )
            retval = FALSE ;
          else {
            pcolor = & c1->color[ index ] ;
            color = *pcolor ;
            if ( color != NULL )
              gst_freeColorN( colorTable , pcolor , colorTable->oncomps ) ;
            else
              HQASSERT((c1->valid[index] & GST_COLOR_PRESENT) == 0,
                        "Inconsistent color present flag");
            c1->valid[index] &= ~GST_COLOR_PRESENT;
          }
        }
      }
      if ( retval ) {
        /* If we freed everything off here, then we can free this too. */
        gst_c1remove( c1 , & colorTable->cubemru ) ;
        gst_freeCube1( colorTable , c1 , colorTable->cubeSide ) ;
        pcube->c1 = NULL;
      }
      else {
        int32 index ;
        /* We are keeping this bottom level 1d array, but it's possible that
         * other data belonging to mini-cubes with an origin on this array might
         * have disappeared, so we must nuke the valid array to indicate this.
         * ie. every mini-cube must be retested for full population before reuse.
         * GST_CUBE_LINEAR need not be cleared as it potentially saves a bit of
         * effort if and when the mini-cube gets repopulated.
         */
        for ( index = 0 ; index < colorTable->cubeSide ; ++index ) {
          c1->valid[index] &= ~GST_CUBE_TESTED;
        }
      }
    }
  }

  return retval ;
}

/**
 * This routine removes a 1d array from the LRU cache.
 */
static void gst_c1remove( colorCube1d *c1 , colorCube1d **c1h )
{
  colorCube1d *tc1 ;

  HQASSERT( c1 , "c1 NULL in gst_c1remove" ) ;
  HQASSERT( c1h , "c1h NULL in gst_c1remove" ) ;

  tc1 = (*c1h) ;

  c1->next->prev = c1->prev ;
  c1->prev->next = c1->next ;

  if ( tc1 == c1 ) {
    tc1 = tc1->next ;
    if ( tc1 == c1 )
      tc1 = NULL ;
    (*c1h) = tc1 ;
  }
}

/**
 * This routine adds a 1d array to the head of the LRU cache.
 */
static void gst_c1addhead( colorCube1d *c1 , colorCube1d **c1h )
{
  colorCube1d *tc1 ;

  HQASSERT( c1 , "c1 NULL in gst_c1addhead" ) ;
  HQASSERT( c1h , "c1h NULL in gst_c1addhead" ) ;

  tc1 = (*c1h) ;
  if ( tc1 == NULL ) {
    c1->next = c1 ;
    c1->prev = c1 ;
    (*c1h) = c1 ;
  }
  else {
    c1->next = tc1 ;
    c1->prev = tc1->prev ;

    c1->next->prev = c1 ;
    c1->prev->next = c1 ;
    (*c1h) = c1 ;
  }
}

/**
 * This routine provides a generic method for getting the addresses of a color
 * and valid mask at the grid point indicated by 'colorTable->indices'.
 * It is not assumed that any of the required data is there, so it must check
 * that the required data structures are present in all dimensions.
 */
static Bool gst_getColorN( colorTableNd *colorTable,
                           int32 *indices ,
                           colorNd ***color, validMask **valid )
{
  int32 incomps ;
  colorCubeNd *pcube ;
  colorCube1d *c1 ;
  colorCube1d **pc1 ;

  HQASSERT( colorTable , "colorTable NULL in gst_getColorN" ) ;

  incomps = colorTable->incomps ;

  /* First of all get the main cube. */
  pcube = &colorTable->cube ;
  for (incomps = colorTable->incomps - 1; incomps >= SOLID_DIMS; incomps--) {
    HQASSERT(indices[incomps] >= 0 && indices[incomps] < colorTable->cubeSide,
             "index out of range" ) ;
    if ( pcube->cn == NULL ) {
      pcube->cn = gst_allocCubeN( colorTable, colorTable->cubeSide ) ;
      if ( pcube->cn == NULL )
        return FALSE ;
    }

    pcube = &pcube->cn[indices[incomps]];
  }
  pc1 = &pcube->c1;
  if ( *pc1 == NULL ) {
    *pc1 = gst_allocCube1( colorTable, colorTable->cubeSide ) ;
    if ( *pc1 == NULL )
      return FALSE ;
    gst_c1addhead( *pc1 , & colorTable->cubemru ) ;
  }
  c1 = *pc1 ;
  HQASSERT( c1 , "Should always end up with non-NULL c1" ) ;

  /* Now get the color. */
  HQASSERT( indices[incomps] >= 0 && indices[incomps] < colorTable->cubeSide,
            "index out of range" ) ;

  *color = &c1->color[ indices[incomps] ] ;
  *valid = &c1->valid[ indices[incomps] ] ;

  return TRUE ;
}

/**
 * This routine locates all the mini-cube corners required for interpolating.
 * The mini-cube is indicated by 'colorTable->indices'.
 */
static Bool gst_getInterpolateColorsN( colorTableNd *colorTable ,
                                       GS_COLORinfo *colorInfo , int32 colorType ,
                                       colorNd **firstColor , validMask **firstValid )
{
  int32 i;
  int32 j;
  int32 n;
  int32 incomps ;
  COLORVALUE *cornerPtrsArray[16];
  COLORVALUE **cornerPtrs;
  int32 *indices = colorTable->indices;
  int32 *incIndices = colorTable->incIndices;
  int32 *tmpIndices = colorTable->tmpIndices;
  int32 *tmpIncIndices = colorTable->tmpIncIndices;

  HQASSERT( colorTable , "colorTable NULL in gst_getInterpolateColorsN" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in gst_getInterpolateColorsN" ) ;
  HQASSERT( firstColor , "color NULL in gst_getInterpolateColorsN" ) ;
  HQASSERT( firstValid , "valid NULL in gst_getInterpolateColorsN" ) ;

  incomps = colorTable->incomps ;

  /* We need to be protected from cornerPtrs being blown away by low memory
   * handling. This could happen during any allocation within this function.
   * Most allocations are called from this function, the other allocations
   * are also controlled.
   */
  HQASSERT(incomps <= 4 || colorTable->cornerPtrsCacheSize == 1,
           "Assumption of cornerPtrsCacheSize == 1 broken");
  cornerPtrs = (incomps <= 4) ? cornerPtrsArray : colorTable->cornerPtrs;

  *firstColor = NULL;
  *firstValid = NULL;

  /* We have to set the incremented indices for use within this function. Normally,
   * the values will be -1, but it's possible that they have been set in low
   * memory handling in which case they should be the correct values.
   */
  for (j = 0; j < incomps; j++) {
    int32 index = indices[j];

    HQASSERT(index >= 0 && index < colorTable->cubeSide,
             "index out of range");
    tmpIndices[j] = index;
    INDEX_INCREMENT_AND_CLIP(index);
    HQASSERT(incIndices[j] == -1 || incIndices[j] == index,
             "Inconsistent cube index");
    incIndices[j] = index;
    tmpIncIndices[j] = index;
  }

  /* Evaluate 2^incomps grid points, the loop is unrolled with 2 evaluations
   * per loop which is convenient because the second 'pcolor' slot must exist
   * if the first slot exists due to the way the slots are on the same 1d
   * allocation.
   * During the loop, 'indices' and 'incIndices' are untouched because they are
   * used by the low memory handler to work out which data to avoid freeing.
   * The grid point that will be evaluated is identified by 'tmpIndices' whose
   * values will change in every loop. For performance, index values are swapped
   * between 'tmpIndices' and 'tmpIncIndices' using a fast algoritm.
   */
  n = 1 << incomps;
  for (i = 0; i < n; i += 2) {
    colorNd *color ;
    colorNd **pcolor ;
    validMask *valid ;

    /* This loop is loop-unrolled by 2 for optimisation reasons. Since the
     * table structure ends in a solid 1D array, we can guarantee that a pointer
     * to the next color exists, even if the color itself doesn't exist yet.
     */
    if ( !gst_getColorN( colorTable, colorTable->tmpIndices, &pcolor, &valid ) )
      return FALSE ;
    HQASSERT( pcolor != NULL , "pcolor NULL" ) ;
    if ( (*valid & GST_COLOR_PRESENT) == 0 ) {
      if ( ! gst_populateColorN( colorTable ,
                                 tmpIndices ,
                                 colorInfo , colorType ,
                                 pcolor , valid ))
        return FALSE ;
    }
    color = *pcolor ;
    HQASSERT(color != NULL, "color NULL" ) ;
    cornerPtrs[ 0 ] = color->color ;

    /* Only want validation data for first entry. */
    if ( *firstColor == NULL ) {
      *firstColor = *pcolor ;
      *firstValid = valid ;

      HQASSERT((*valid & GST_COLOR_PRESENT) != 0, "GST_COLOR_PRESENT FALSE");

      /* Evaluation of grid points is lazy with more than 4 dimensions because
       * there are often a number of zero values that don't require interpolation.
       * Also, for tetrahedral interpolation only a small proportion of corners
       * are required. The cost of evaluating all grid points for a mini-cube
       * isn't so great for 3 or 4 dimensions, but for 5+ the cost outweighs the
       * cost of the additional tests required in the interpolation function.
       */
      if (incomps > 4) {
        return TRUE ;
      }

      if ( (*valid & GST_CUBE_TESTED) != 0 ) {
        /* We know that the current mini-cube has been evaluated, so all of its
         * corners exist. If we aren't using the pointer cache, we can leave
         * here now and the interpolators will get the required corners directly.
         * It is faster doing it that way for the tetrahedral method because we
         * only need a small subset of the corners, while for the cubic method it
         * doesn't matter whether they are assigned here or in the interpolator.
         * OTOH. If we are using the pointer cache, and it's being used with a
         * good hit rate, it is better to assign the pointer cache values here.
         */

        cornerPtrs = colorTable->cornerPtrs;

        if (colorTable->cornerPtrsCacheBits != 0) {
          checkIndices(colorTable);

          switch (incomps) {
          case 1:
            extractCorners_1(&colorTable->cube, indices, incIndices, cornerPtrs);
            break;
          case 2:
            extractCorners_2(&colorTable->cube, indices, incIndices, cornerPtrs);
            break;
          case 3:
            extractCorners_3(&colorTable->cube, indices, incIndices, cornerPtrs);
            break;
          case 4:
            extractCorners_4(&colorTable->cube, indices, incIndices, cornerPtrs);
            break;

          default:
            HQFAIL("Unexpected use of CornerPtrsCache with > 4 colorants");
            break;
          }
        }

        return TRUE;
      }
    }


    /* Get the next entry by incrementing pcolor except for the last slot - the
     * slot must exist now because it's on the same 1d allocation. */
    if ( indices[ 0 ] < colorTable->cubeSide - 1 ) {
      pcolor = pcolor + 1 ;
      valid = valid + 1 ;
    }
    HQASSERT( pcolor != NULL , "pcolor NULL" ) ;
    if ( (*valid & GST_COLOR_PRESENT) == 0 ) {
      /* Populate the other corner of the mini-cube in the first dimension. */
      tmpIndices[ 0 ] = incIndices[ 0 ];
      if ( ! gst_populateColorN( colorTable ,
                                 tmpIndices ,
                                 colorInfo , colorType ,
                                 pcolor , valid ))
        return FALSE ;
      tmpIndices[ 0 ] = indices[ 0 ];
    }
    color = *pcolor ;
    HQASSERT(color != NULL, "color NULL" ) ;
    cornerPtrs[ 1 ] = color->color ;

    cornerPtrs += 2 ;

    /* Swap over indices as required to populate mini-cube; note we leave the
     * bottom indices value alone, since it was dealt with above. This is a
     * clever and fast algorithm that walks through the table until we end up
     * populating all the corners in our mini-cube.
     * When input dimension is 1 (i.e. only 2 points to get) do nothing.
     */
    if ( n > 2 ) {
      int32 ip1 = ((( i + 2 ) & ( n - 1 )) ^ 1 ) ;
      int32 ips = 2 ;
      int32 j = 1;
      do {
        int32 index = tmpIndices[ j ] ;
        int32 incIndex = tmpIncIndices[ j ] ;
        tmpIndices[ j ] = incIndex ;
        tmpIncIndices[ j ] = index ;
        j++;
        ip1 ^= ips ;
        ips <<= 1 ;
      } while ( ip1 != i + 1 ) ;
    }
  }

  /* Now copy into colorTables->cornerPtrs safe in the knowledge that it wasn't
   * blown away by low memory handling during this function.
   */
  if (incomps <= 4) {
    for (i = 0; i < n; i++)
      colorTable->cornerPtrs[i] = cornerPtrsArray[i];
  }

  return TRUE ;
}

/**
 * This routine is used to populate the corners of a mini-cube.
 */
static Bool gst_populateColorN( colorTableNd *colorTable ,
                                int32 *indices ,
                                GS_COLORinfo *colorInfo , int32 colorType ,
                                colorNd **color , validMask *valid )
{
  int32 incomps ;
  float *inputColor ;
  float *rangeb ;
  float *ranges ;

  HQASSERT( colorTable , "colorTable NULL in gst_populateColorN" ) ;
  HQASSERT( indices , "indices NULL in gst_populateColorN" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in gst_populateColorN" ) ;
  HQASSERT( color , "color NULL in gst_populateColorN" ) ;
  HQASSERT( valid , "valid NULL in gst_populateColorN" ) ;

  DEBUG_INCREMENT(colorTable->nPopulate, 1) ;

  if (*color == NULL) {
    *color = gst_allocColorN( colorTable, colorTable->oncomps ) ;
    if ( *color == NULL )
      return FALSE ;
  }

  inputColor = colorTable->scratch ;
  rangeb = colorTable->rangeb ;
  ranges = colorTable->ranges ;

  for (incomps = 0; incomps < colorTable->incomps; incomps++)
    inputColor[ incomps ] = rangeb[ incomps ] + ranges[ incomps ] * indices[ incomps ] ;

  if ( ! gsc_invokeChainBlock( colorInfo ,  colorType ,
                               inputColor , (*color)->color , 1 ))
    return FALSE ;

  *valid |= GST_COLOR_PRESENT;

  return TRUE ;
}

/**
 * This routine evaluates the centre of a mini-cube to see if interpolation
 * is valid at that point. Uses gsc_invokeChainBlock for the evaluation.
 */
static Bool gst_evaluateCentreN(colorTableNd *colorTable ,
                                GS_COLORinfo *colorInfo , int32 colorType ,
                                COLORVALUE *poColorValues )
{
  int32 incomps ;
  int32 *indices ;
  float *inputColor ;
  float *rangeb ;
  float *ranges ;

  HQASSERT( colorTable , "colorTable NULL in gst_evaluateCentreN" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in gst_evaluateCentreN" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_evaluateCentreN" ) ;

  DEBUG_INCREMENT(colorTable->nVerified, 1) ;

  indices = colorTable->indices ;
  inputColor = colorTable->scratch ;
  rangeb = colorTable->rangeb ;
  ranges = colorTable->ranges ;

  for (incomps = 0; incomps < colorTable->incomps; incomps++) {
    int32 index = indices[ incomps ] ;
    int32 incIndex = index ;
    INDEX_INCREMENT_AND_CLIP(incIndex);

    inputColor[ incomps ] = rangeb[ incomps ] +
                            ranges[ incomps ] * ( 0.5f * ( index + incIndex )) ;
  }

  return gsc_invokeChainBlock( colorInfo ,  colorType ,
                               inputColor , poColorValues , 1 ) ;
}

#ifdef GST_EVAL_INT_ERROR
/**
 * This routine evaluates a color transform directly for use in testing accuracy.
 */
static Bool gst_evaluateColorN(colorTableNd *colorTable ,
                               GS_COLORinfo *colorInfo , int32 colorType ,
                               int32 *piColorValues , COLORVALUE *poColorValues )
{
  int32 incomps ;
  float *inputColor ;
  float *rangeb ;
  float *rangen ;

  HQASSERT( colorTable , "colorTable NULL in gst_evaluateColorN" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in gst_evaluateColorN" ) ;
  HQASSERT( piColorValues , "piColorValues NULL in gst_evaluateColorN" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_evaluateColorN" ) ;

  inputColor = colorTable->scratch ;
  rangeb = colorTable->rangeb ;
  rangen = colorTable->rangen ;

  for (incomps = 0; incomps < colorTable->incomps; incomps++)
    inputColor[incomps] = rangeb[incomps] + rangen[incomps] * piColorValues[incomps];

  return gsc_invokeChainBlock( colorInfo ,  colorType ,
                               inputColor , poColorValues , 1 ) ;
}
#endif

/**
 * This routine interpolates the mini-cube exactly at the mid-point and
 * compares the results against a precise transform at that mid-point.
 * It returns TRUE if the interpolation is accurate within a given percentage.
 */
static Bool gst_validateColorsN(colorTableNd *colorTable ,
                                COLORVALUE *poColorValues )
{
  int32 oncomps ;
  float errord2 ;
  float *errordc ;

  Bool interpolationValid ;

  HQASSERT( colorTable , "colorTable NULL in gst_validateColorsN" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_validateColorsN" ) ;

  /* Take each output component one by one. */
  interpolationValid = TRUE ;

  errordc = colorTable->errordc ;

  errord2 = 0.0f ;
  for (oncomps = 0; oncomps < colorTable->oncomps; oncomps++) {
    int32 i ;
    int32 incomps ;
    int32 *results ;
    COLORVALUE **cornerPtrs ;

    /* Set initial values. */
    cornerPtrs = colorTable->cornerPtrs ;
    results = colorTable->results ;
    for (i = 0; i < 1 << colorTable->incomps; i++)
      results[i] = cornerPtrs[i][oncomps] << GST_IFRACBITS ;

    /* Interpolate exactly at mid-point, trickling down interpolated points. */
    results = colorTable->results ;
    incomps = colorTable->incomps;
    while (incomps-- > 0) {
      int32 *srcn = results ;
      int32 *dstn = results ;
      for (i = 0; i < 1 << incomps; i++) {
        int32 lo = *srcn++ ;
        int32 hi = *srcn++ ;
        *dstn++ = ( lo + hi + 1 ) >> 1 ;
      }
    }

    /* Interpolation result is in results[0] */
    { int32 d ;
      float ftmp;
      /* d may be -ve but doesn't matter because we take the square */
      d = (( results[0] + GST_IFRACADDN ) >> GST_IFRACBITS ) - poColorValues[oncomps];
      ftmp = d * errordc[oncomps];
      errord2 += ftmp * ftmp ;

      DEBUG_INCREMENT(colorTable->nValidateSum, abs(d)) ;
      DEBUG_INCREMENT(colorTable->nValidate, 1) ;
    }
  }

  HQASSERT(errord2 <= colorTable->oncomps, "errord2 too large, either d or errordc is wrong");
  if ( errord2 > colorTable->errordv )
    interpolationValid = FALSE ;

  return interpolationValid ;
}

/**
 * Convenience macros for accessing the colors within a fully populated
 * mini-cube.
 * We define these for the optimised cases of 1-4 component colorspaces. The
 * indexing is arranged to cycle through the components in a regular way that
 * matches how they are assigned in gst_getInterpolateColorsN().
 */
#define CORNER_1_0(pcn)  pcn->c1->color[indices[0]]->color;
#define CORNER_1_1(pcn)  pcn->c1->color[incIndices[0]]->color;

#define CORNER_2_0(pcn)  pcn->cn[indices[1]].c1->color[indices[0]]->color;
#define CORNER_2_1(pcn)  pcn->cn[indices[1]].c1->color[incIndices[0]]->color;
#define CORNER_2_2(pcn)  pcn->cn[incIndices[1]].c1->color[indices[0]]->color;
#define CORNER_2_3(pcn)  pcn->cn[incIndices[1]].c1->color[incIndices[0]]->color;

#define CORNER_3_0(pcn)  pcn->cn[indices[2]].cn[indices[1]].c1->color[indices[0]]->color
#define CORNER_3_1(pcn)  pcn->cn[indices[2]].cn[indices[1]].c1->color[incIndices[0]]->color
#define CORNER_3_2(pcn)  pcn->cn[indices[2]].cn[incIndices[1]].c1->color[indices[0]]->color
#define CORNER_3_3(pcn)  pcn->cn[indices[2]].cn[incIndices[1]].c1->color[incIndices[0]]->color
#define CORNER_3_4(pcn)  pcn->cn[incIndices[2]].cn[indices[1]].c1->color[indices[0]]->color
#define CORNER_3_5(pcn)  pcn->cn[incIndices[2]].cn[indices[1]].c1->color[incIndices[0]]->color
#define CORNER_3_6(pcn)  pcn->cn[incIndices[2]].cn[incIndices[1]].c1->color[indices[0]]->color
#define CORNER_3_7(pcn)  pcn->cn[incIndices[2]].cn[incIndices[1]].c1->color[incIndices[0]]->color

#define CORNER_4_0(pcn)   pcn->cn[indices[3]].cn[indices[2]].cn[indices[1]].c1->color[indices[0]]->color
#define CORNER_4_1(pcn)   pcn->cn[indices[3]].cn[indices[2]].cn[indices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_2(pcn)   pcn->cn[indices[3]].cn[indices[2]].cn[incIndices[1]].c1->color[indices[0]]->color
#define CORNER_4_3(pcn)   pcn->cn[indices[3]].cn[indices[2]].cn[incIndices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_4(pcn)   pcn->cn[indices[3]].cn[incIndices[2]].cn[indices[1]].c1->color[indices[0]]->color
#define CORNER_4_5(pcn)   pcn->cn[indices[3]].cn[incIndices[2]].cn[indices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_6(pcn)   pcn->cn[indices[3]].cn[incIndices[2]].cn[incIndices[1]].c1->color[indices[0]]->color
#define CORNER_4_7(pcn)   pcn->cn[indices[3]].cn[incIndices[2]].cn[incIndices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_8(pcn)   pcn->cn[incIndices[3]].cn[indices[2]].cn[indices[1]].c1->color[indices[0]]->color
#define CORNER_4_9(pcn)   pcn->cn[incIndices[3]].cn[indices[2]].cn[indices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_10(pcn)  pcn->cn[incIndices[3]].cn[indices[2]].cn[incIndices[1]].c1->color[indices[0]]->color
#define CORNER_4_11(pcn)  pcn->cn[incIndices[3]].cn[indices[2]].cn[incIndices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_12(pcn)  pcn->cn[incIndices[3]].cn[incIndices[2]].cn[indices[1]].c1->color[indices[0]]->color
#define CORNER_4_13(pcn)  pcn->cn[incIndices[3]].cn[incIndices[2]].cn[indices[1]].c1->color[incIndices[0]]->color
#define CORNER_4_14(pcn)  pcn->cn[incIndices[3]].cn[incIndices[2]].cn[incIndices[1]].c1->color[indices[0]]->color
#define CORNER_4_15(pcn)  pcn->cn[incIndices[3]].cn[incIndices[2]].cn[incIndices[1]].c1->color[incIndices[0]]->color

static inline void extractCorners_1(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols)
{
  cols[0] = CORNER_1_0(pcn);
  cols[1] = CORNER_1_1(pcn);
}

static inline void extractCorners_2(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols)
{
  cols[0] = CORNER_2_0(pcn);
  cols[1] = CORNER_2_1(pcn);
  cols[2] = CORNER_2_2(pcn);
  cols[3] = CORNER_2_3(pcn);
}

static inline void extractCorners_3(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols)
{
  cols[0] = CORNER_3_0(pcn);
  cols[1] = CORNER_3_1(pcn);
  cols[2] = CORNER_3_2(pcn);
  cols[3] = CORNER_3_3(pcn);
  cols[4] = CORNER_3_4(pcn);
  cols[5] = CORNER_3_5(pcn);
  cols[6] = CORNER_3_6(pcn);
  cols[7] = CORNER_3_7(pcn);
}

static inline void extractCorners_4(colorCubeNd *pcn,
                                    int32       *indices,
                                    int32       *incIndices,
                                    COLORVALUE  **cols)
{
  cols[0]  = CORNER_4_0(pcn);
  cols[1]  = CORNER_4_1(pcn);
  cols[2]  = CORNER_4_2(pcn);
  cols[3]  = CORNER_4_3(pcn);
  cols[4]  = CORNER_4_4(pcn);
  cols[5]  = CORNER_4_5(pcn);
  cols[6]  = CORNER_4_6(pcn);
  cols[7]  = CORNER_4_7(pcn);
  cols[8]  = CORNER_4_8(pcn);
  cols[9]  = CORNER_4_9(pcn);
  cols[10] = CORNER_4_10(pcn);
  cols[11] = CORNER_4_11(pcn);
  cols[12] = CORNER_4_12(pcn);
  cols[13] = CORNER_4_13(pcn);
  cols[14] = CORNER_4_14(pcn);
  cols[15] = CORNER_4_15(pcn);
}

static void checkIndices(colorTableNd *colorTable)
{
#if ASSERT_BUILD
  int32 i;
  for (i = 0; i < colorTable->incomps; i++) {
    int32 index = colorTable->indices[i];
    INDEX_INCREMENT_AND_CLIP(index);
    HQASSERT(colorTable->incIndices[i] == index, "Inconsistent cube index");
  }
#endif

  UNUSED_PARAM(colorTableNd *, colorTable);
}

/*----------------------------------------------------------------------------*/
/**
 * These routines interpolate a mini-cube. Common cases of 1-4 input
 * components are optimised and loop expanded. The values being interpolated
 * are 16 bit device codes. 8 bits of interpolation precision are used and
 * 8 bits of fraction from the table space (so in total we fit into a 32 bit
 * integer).
 * For 3 and 4 component images, there are two methods - cubic and tetrahedral
 * which are selected via the FasterColorMethod userparam.
 */
static Bool gst_interpolate1( colorTableNd *colorTable ,
                              COLORVALUE *poColorValues )
{
  int32       oncomps ;
  int32       *fractns = colorTable->fractns ;
  int32       fr0 = fractns[ 0 ] ;
  COLORVALUE  **cols ;
  COLORVALUE  *colsArray[2] ;

  HQASSERT( colorTable , "colorTable NULL in gst_interpolate1" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_interpolate1" ) ;

  /* If the pointers cache is active we can use it, otherwise we have to evaluate
   * the corners given the current set of mini-cube indices.
   */
  if (colorTable->cornerPtrsCacheBits != 0)
    cols = colorTable->cornerPtrs ;
  else {
    cols = colsArray ;
    checkIndices(colorTable);
    extractCorners_1(&colorTable->cube, colorTable->indices, colorTable->incIndices, cols);
  }

  for (oncomps = 0; oncomps < colorTable->oncomps; oncomps++) {
    uint32 a0 ;

    /* Interpolate at fractional points, trickling down interpolated points. */
    GST_FIRST_INTERPOLATE( a0 , fr0 , cols[0][oncomps] , cols[1][oncomps] ) ;

    a0 = GST_ROUND( a0 ) ;

#ifdef GST_EVAL_INT_ERROR
    { int32 d = poColorValues[oncomps] - a0 ;
      INLINE_ABS32(d, d) ;
      colorTable->nInterpolateError += d ;
      colorTable->nInterpolate += 1 ;
    }
#endif

    poColorValues[oncomps] = CAST_TO_COLORVALUE(a0) ;
  }

  return TRUE;
}

static Bool gst_interpolate2( colorTableNd *colorTable ,
                              COLORVALUE *poColorValues )
{
  int32       oncomps ;
  int32       *fractns = colorTable->fractns ;
  int32       fr0 = fractns[ 0 ] ;
  int32       fr1 = fractns[ 1 ] ;
  COLORVALUE  **cols ;
  COLORVALUE  *colsArray[4] ;

  HQASSERT( colorTable , "colorTable NULL in gst_interpolate2" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_interpolate2" ) ;

  /* If the pointers cache is active we can use it, otherwise we have to evaluate
   * the corners given the current set of mini-cube indices.
   */
  if (colorTable->cornerPtrsCacheBits != 0)
    cols = colorTable->cornerPtrs ;
  else {
    cols = colsArray ;
    checkIndices(colorTable);
    extractCorners_2(&colorTable->cube, colorTable->indices, colorTable->incIndices, cols);
  }

  for (oncomps = 0; oncomps < colorTable->oncomps; oncomps++) {
    uint32 a0 , a1 ;

    /* Interpolate at fractional points, trickling down interpolated points. */
    GST_FIRST_INTERPOLATE( a0 , fr0 , cols[0][oncomps] , cols[1][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a1 , fr0 , cols[2][oncomps] , cols[3][oncomps] ) ;

    GST_INTERPOLATE( a0 , fr1 , a0 , a1 ) ;

    a0 = GST_ROUND( a0) ;

#ifdef GST_EVAL_INT_ERROR
    { int32 d = poColorValues[oncomps] - a0 ;
      INLINE_ABS32(d, d) ;
      colorTable->nInterpolateError += d ;
      colorTable->nInterpolate += 1 ;
    }
#endif

    poColorValues[oncomps] = CAST_TO_COLORVALUE(a0) ;
  }

  return TRUE;
}

static Bool gst_interpolate2_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues )
{
  int32       n ;
  COLORVALUE  *v1 ;
  COLORVALUE  *v2 ;
  COLORVALUE  *v3 ;
  int32       fac1 ;
  int32       fac2 ;
  int32       fac3 ;
  int32       *fractns = colorTable->fractns ;
  int32       xf = fractns[ 0 ] ;
  int32       yf = fractns[ 1 ] ;
  uint32      a0 ;

  uint32 tetrahedron = 0 ;


  /* This macro is a convenient way of implementing the interpolation with and
   * without the pointers cache. If the pointers cache is active we can use it,
   * otherwise we have to evaluate the corners given the current set of mini-cube
   * indices.
   */
#define ASSIGN_VERTICES_2(vertex_0, vertex_1, vertex_2)  \
  if (colorTable->cornerPtrsCacheBits != 0) {           \
    COLORVALUE  **cornerPtrs = colorTable->cornerPtrs;  \
    v1 = cornerPtrs[vertex_0];                          \
    v2 = cornerPtrs[vertex_1];                          \
    v3 = cornerPtrs[vertex_2];                          \
  } else {                                              \
    int32 *indices = colorTable->indices;               \
    int32 *incIndices = colorTable->incIndices;         \
    colorCubeNd *pcn = &colorTable->cube;               \
    checkIndices(colorTable);                           \
    v1 = CORNER_2_##vertex_0(pcn);                      \
    v2 = CORNER_2_##vertex_1(pcn);                      \
    v3 = CORNER_2_##vertex_2(pcn);                      \
  }

  /* The inequalities are used to determine which tetrahedron contains the input
   * value.  The output value is calculated by interpolating between the four
   * vertices of the tetrahedron.  The factors are ratios between sub-volumes of
   * the tetrahedron.
   */
  if ( xf >= yf )
    tetrahedron |= 1 ;

  switch ( tetrahedron ) {
  case (1) :
    /* xf >= yf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - yf; fac3 = yf;
    ASSIGN_VERTICES_2(0, 1, 3);
    break ;
  case (0) :
    /* yf > xf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - xf; fac3 = xf;
    ASSIGN_VERTICES_2(0, 2, 3);
    break ;
  default :
    /* Illegal combination */
    HQFAIL("Tetrahedral interpolator failed") ;
    return FALSE;
  }

  n = colorTable->oncomps;
  for (;;) {
    switch (n) {
    default:
    case 4:
      a0 = v1[ 3 ] * fac1 + v2[ 3 ] * fac2 + v3[ 3 ] * fac3 ;
      poColorValues[ 3 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 3:
      a0 = v1[ 2 ] * fac1 + v2[ 2 ] * fac2 + v3[ 2 ] * fac3 ;
      poColorValues[ 2 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 2:
      a0 = v1[ 1 ] * fac1 + v2[ 1 ] * fac2 + v3[ 1 ] * fac3 ;
      poColorValues[ 1 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 1:
      a0 = v1[ 0 ] * fac1 + v2[ 0 ] * fac2 + v3[ 0 ] * fac3 ;
      poColorValues[ 0 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    }

    n -= 4;
    if (n > 0) {
      v1 += 4;
      v2 += 4;
      v3 += 4;
      poColorValues += 4;
    }
    else
      break;
  }

  return TRUE;
}

static Bool gst_interpolate3_cubic( colorTableNd *colorTable ,
                                    COLORVALUE *poColorValues )
{
  int32       oncomps ;
  int32       *fractns = colorTable->fractns ;
  int32       fr0 = fractns[ 0 ] ;
  int32       fr1 = fractns[ 1 ] ;
  int32       fr2 = fractns[ 2 ] ;
  COLORVALUE  **cols ;
  COLORVALUE  *colsArray[8] ;

  HQASSERT( colorTable , "colorTable NULL in gst_interpolate3" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_interpolate" ) ;

  /* If the pointers cache is active we can use it, otherwise we have to evaluate
   * the corners given the current set of mini-cube indices.
   */
  if (colorTable->cornerPtrsCacheBits != 0)
    cols = colorTable->cornerPtrs ;
  else {
    cols = colsArray ;
    checkIndices(colorTable);
    extractCorners_3(&colorTable->cube, colorTable->indices, colorTable->incIndices, cols);
  }

  for (oncomps = 0; oncomps < colorTable->oncomps; oncomps++) {
    uint32 a0 , a1 , a2 , a3 ;

    /* Interpolate at fractional points, trickling down interpolated points. */
    GST_FIRST_INTERPOLATE( a0 , fr0 , cols[0][oncomps] , cols[1][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a1 , fr0 , cols[2][oncomps] , cols[3][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a2 , fr0 , cols[4][oncomps] , cols[5][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a3 , fr0 , cols[6][oncomps] , cols[7][oncomps] ) ;

    GST_INTERPOLATE( a0 , fr1 , a0 , a1 ) ;
    GST_INTERPOLATE( a1 , fr1 , a2 , a3 ) ;

    GST_INTERPOLATE( a0 , fr2 , a0 , a1 ) ;

    a0 = GST_ROUND( a0 ) ;

#ifdef GST_EVAL_INT_ERROR
    { int32 d = poColorValues[oncomps] - a0 ;
      INLINE_ABS32(d, d) ;
      colorTable->nInterpolateError += d ;
      colorTable->nInterpolate += 1 ;
    }
#endif

    poColorValues[oncomps] = CAST_TO_COLORVALUE(a0) ;
  }

  return TRUE;
}

static Bool gst_interpolate3_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues )
{
  int32       n ;
  COLORVALUE  *v1 ;
  COLORVALUE  *v2 ;
  COLORVALUE  *v3 ;
  COLORVALUE  *v4 ;
  int32       fac1 ;
  int32       fac2 ;
  int32       fac3 ;
  int32       fac4 ;
  int32       *fractns = colorTable->fractns ;
  int32       xf = fractns[ 0 ] ;
  int32       yf = fractns[ 1 ] ;
  int32       zf = fractns[ 2 ] ;
  uint32      a0 ;

  uint32 tetrahedron = 0 ;


  /* This macro is a convenient way of implementing the interpolation with and
   * without the pointers cache. If the pointers cache is active we can use it,
   * otherwise we have to evaluate the corners given the current set of mini-cube
   * indices.
   */
#define ASSIGN_VERTICES_3(vertex_0, vertex_1, vertex_2, vertex_3)  \
  if (colorTable->cornerPtrsCacheBits != 0) {           \
    COLORVALUE  **cornerPtrs = colorTable->cornerPtrs;  \
    v1 = cornerPtrs[vertex_0];                          \
    v2 = cornerPtrs[vertex_1];                          \
    v3 = cornerPtrs[vertex_2];                          \
    v4 = cornerPtrs[vertex_3];                          \
  } else {                                              \
    int32 *indices = colorTable->indices;               \
    int32 *incIndices = colorTable->incIndices;         \
    colorCubeNd *pcn = &colorTable->cube;               \
    checkIndices(colorTable);                           \
    v1 = CORNER_3_##vertex_0(pcn);                      \
    v2 = CORNER_3_##vertex_1(pcn);                      \
    v3 = CORNER_3_##vertex_2(pcn);                      \
    v4 = CORNER_3_##vertex_3(pcn);                      \
  }

  /* The inequalities are used to determine which tetrahedron contains the input
   * value.  The output value is calculated by interpolating between the four
   * vertices of the tetrahedron.  The factors are ratios between sub-volumes of
   * the tetrahedron.
   */
  if ( xf >= yf )
    tetrahedron |= 1 ;
  if ( xf >= zf )
    tetrahedron |= 2 ;
  if ( yf >= zf )
    tetrahedron |= 4 ;

  switch ( tetrahedron ) {
  case (1 | 2 | 4) :
    /* xf >= yf >= zf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - yf; fac3 = yf - zf; fac4 = zf;
    ASSIGN_VERTICES_3(0, 1, 3, 7);
    break ;
  case (1 | 2) :
    /* xf >= zf > yf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - zf; fac3 = zf - yf; fac4 = yf;
    ASSIGN_VERTICES_3(0, 1, 5, 7);
    break ;
  case (1) :
    /* zf > xf >= yf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - xf; fac3 = xf - yf; fac4 = yf;
    ASSIGN_VERTICES_3(0, 4, 5, 7);
    break ;
  case (0) :
    /* zf > yf > xf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - yf; fac3 = yf - xf; fac4 = xf;
    ASSIGN_VERTICES_3(0, 4, 6, 7);
    break ;
  case (4) :
    /* yf >= zf > xf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - zf; fac3 = zf - xf; fac4 = xf;
    ASSIGN_VERTICES_3(0, 2, 6, 7);
    break ;
  case (2 | 4) :
    /* yf > xf >= zf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - xf; fac3 = xf - zf; fac4 = zf;
    ASSIGN_VERTICES_3(0, 2, 3, 7);
    break ;
  default :
    /* Illegal combination */
    HQFAIL("Tetrahedral interpolator failed") ;
    return FALSE;
  }

  n = colorTable->oncomps;
  for (;;) {
    switch (n) {
    default:
    case 4:
      a0 = v1[ 3 ] * fac1 + v2[ 3 ] * fac2 + v3[ 3 ] * fac3 + v4[ 3 ] * fac4 ;
      poColorValues[ 3 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 3:
      a0 = v1[ 2 ] * fac1 + v2[ 2 ] * fac2 + v3[ 2 ] * fac3 + v4[ 2 ] * fac4 ;
      poColorValues[ 2 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 2:
      a0 = v1[ 1 ] * fac1 + v2[ 1 ] * fac2 + v3[ 1 ] * fac3 + v4[ 1 ] * fac4 ;
      poColorValues[ 1 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 1:
      a0 = v1[ 0 ] * fac1 + v2[ 0 ] * fac2 + v3[ 0 ] * fac3 + v4[ 0 ] * fac4 ;
      poColorValues[ 0 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    }

    n -= 4;
    if (n > 0) {
      v1 += 4;
      v2 += 4;
      v3 += 4;
      v4 += 4;
      poColorValues += 4;
    }
    else
      break;
  }

  return TRUE;
}

static Bool gst_interpolate4_cubic( colorTableNd *colorTable ,
                                    COLORVALUE *poColorValues )
{
  int32       oncomps ;
  int32       *fractns = colorTable->fractns ;
  int32       fr0 = fractns[ 0 ] ;
  int32       fr1 = fractns[ 1 ] ;
  int32       fr2 = fractns[ 2 ] ;
  int32       fr3 = fractns[ 3 ] ;
  COLORVALUE  **cols ;
  COLORVALUE  *colsArray[16] ;

  HQASSERT( colorTable , "colorTable NULL in gst_interpolate4" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_interpolate4" ) ;

  /* If the pointers cache is active we can use it, otherwise we have to evaluate
   * the corners given the current set of mini-cube indices.
   */
  if (colorTable->cornerPtrsCacheBits != 0)
    cols = colorTable->cornerPtrs ;
  else {
    cols = colsArray ;
    checkIndices(colorTable);
    extractCorners_4(&colorTable->cube, colorTable->indices, colorTable->incIndices, cols);
  }

  for (oncomps = 0; oncomps < colorTable->oncomps; oncomps++) {
    uint32 a0 , a1 , a2 , a3 , a4, a5, a6, a7 ;

    /* Interpolate at fractional points, trickling down interpolated points. */

    GST_FIRST_INTERPOLATE( a0 , fr0 , cols[0][oncomps] , cols[1][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a1 , fr0 , cols[2][oncomps] , cols[3][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a2 , fr0 , cols[4][oncomps] , cols[5][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a3 , fr0 , cols[6][oncomps] , cols[7][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a4 , fr0 , cols[8][oncomps] , cols[9][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a5 , fr0 , cols[10][oncomps] , cols[11][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a6 , fr0 , cols[12][oncomps] , cols[13][oncomps] ) ;
    GST_FIRST_INTERPOLATE( a7 , fr0 , cols[14][oncomps] , cols[15][oncomps] ) ;

    GST_INTERPOLATE( a0 , fr1 , a0 , a1 ) ;
    GST_INTERPOLATE( a1 , fr1 , a2 , a3 ) ;
    GST_INTERPOLATE( a2 , fr1 , a4 , a5 ) ;
    GST_INTERPOLATE( a3 , fr1 , a6 , a7 ) ;

    GST_INTERPOLATE( a0 , fr2 , a0 , a1 ) ;
    GST_INTERPOLATE( a1 , fr2 , a2 , a3 ) ;

    GST_INTERPOLATE( a0 , fr3 , a0 , a1 ) ;

    a0 = GST_ROUND( a0 ) ;

#ifdef GST_EVAL_INT_ERROR
    { int32 d = poColorValues[oncomps] - a0 ;
      INLINE_ABS32(d, d) ;
      colorTable->nInterpolateError += d ;
      colorTable->nInterpolate += 1 ;
    }
#endif

    poColorValues[oncomps] = CAST_TO_COLORVALUE(a0) ;
  }

  return TRUE;
}

static Bool gst_interpolate4_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues )
{
  int32       n ;
  COLORVALUE  *v1 ;
  COLORVALUE  *v2 ;
  COLORVALUE  *v3 ;
  COLORVALUE  *v4 ;
  COLORVALUE  *v5 ;
  int32       fac1 ;
  int32       fac2 ;
  int32       fac3 ;
  int32       fac4 ;
  int32       fac5 ;
  int32       *fractns = colorTable->fractns ;
  int32       wf = fractns[ 0 ] ;
  int32       xf = fractns[ 1 ] ;
  int32       yf = fractns[ 2 ] ;
  int32       zf = fractns[ 3 ] ;
  uint32      a0 ;

  uint32 tetrahedron = 0 ;

  /* This macro is a convenient way of implementing the interpolation with and
   * without the pointers cache. If the pointers cache is active we can use it,
   * otherwise we have to evaluate the corners given the current set of mini-cube
   * indices.
   */
#define ASSIGN_VERTICES_4(vertex_0, vertex_1, vertex_2, vertex_3, vertex_4)  \
  if (colorTable->cornerPtrsCacheBits != 0) {           \
    COLORVALUE  **cornerPtrs = colorTable->cornerPtrs;  \
    v1 = cornerPtrs[vertex_0];                          \
    v2 = cornerPtrs[vertex_1];                          \
    v3 = cornerPtrs[vertex_2];                          \
    v4 = cornerPtrs[vertex_3];                          \
    v5 = cornerPtrs[vertex_4];                          \
  } else {                                              \
    int32 *indices = colorTable->indices;               \
    int32 *incIndices = colorTable->incIndices;         \
    colorCubeNd *pcn = &colorTable->cube;               \
    checkIndices(colorTable);                           \
    v1 = CORNER_4_##vertex_0(pcn);                      \
    v2 = CORNER_4_##vertex_1(pcn);                      \
    v3 = CORNER_4_##vertex_2(pcn);                      \
    v4 = CORNER_4_##vertex_3(pcn);                      \
    v5 = CORNER_4_##vertex_4(pcn);                      \
  }

  /* The inequalities are used to determine which tetrahedron contains the input
   * value.  The output value is calculated by interpolating between the five
   * vertices of the tetrahedron.  The factors are ratios between sub-volumes of
   * the tetrahedron.
   */
  if ( wf >= xf )
    tetrahedron |= 1 ;
  if ( wf >= yf )
    tetrahedron |= 2 ;
  if ( wf >= zf )
    tetrahedron |= 4 ;
  if ( xf >= yf )
    tetrahedron |= 8 ;
  if ( xf >= zf )
    tetrahedron |= 16 ;
  if ( yf >= zf )
    tetrahedron |= 32 ;

  switch ( tetrahedron ) {
  case (1 | 2 | 4 | 8 | 16 | 32) :
    /* wf >= xf >= yf >= zf */
    fac1 = (1 << GST_IFRACBITS) - wf; fac2 = wf - xf; fac3 = xf - yf; fac4 = yf - zf; fac5 = zf;
    ASSIGN_VERTICES_4(0, 1, 3, 7, 15);
    break ;
  case (1 | 2 | 4 | 8 | 16) :
    /* wf >= xf >= zf > yf */
    fac1 = (1 << GST_IFRACBITS) - wf; fac2 = wf - xf; fac3 = xf - zf; fac4 = zf - yf; fac5 = yf;
    ASSIGN_VERTICES_4(0, 1, 3, 11, 15);
    break ;
  case (1 | 2 | 4 | 16 | 32) :
    /* wf >= yf > xf >= zf */
    fac1 = (1 << GST_IFRACBITS) - wf; fac2 = wf - yf; fac3 = yf - xf; fac4 = xf - zf; fac5 = zf;
    ASSIGN_VERTICES_4(0, 1, 5, 7, 15);
    break ;
  case (1 | 2 | 4 | 32) :
    /* wf >= yf >= zf > xf */
    fac1 = (1 << GST_IFRACBITS) - wf; fac2 = wf - yf; fac3 = yf - zf; fac4 = zf - xf; fac5 = xf;
    ASSIGN_VERTICES_4(0, 1, 5, 13, 15);
    break ;
  case (1 | 2 | 4 | 8) :
    /* wf >= zf > xf >= yf */
    fac1 = (1 << GST_IFRACBITS) - wf; fac2 = wf - zf; fac3 = zf - xf; fac4 = xf - yf; fac5 = yf;
    ASSIGN_VERTICES_4(0, 1, 9, 11, 15);
    break ;
  case (1 | 2 | 4) :
    /* wf >= zf > yf > xf */
    fac1 = (1 << GST_IFRACBITS) - wf; fac2 = wf - zf; fac3 = zf - yf; fac4 = yf - xf; fac5 = xf;
    ASSIGN_VERTICES_4(0, 1, 9, 13, 15);
    break ;
  case (2 | 4 | 8 | 16 | 32) :
    /* xf > wf >= yf >= zf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - wf; fac3 = wf - yf; fac4 = yf - zf; fac5 = zf;
    ASSIGN_VERTICES_4(0, 2, 3, 7, 15);
    break ;
  case (2 | 4 | 8 | 16) :
    /* xf > wf >= zf > yf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - wf; fac3 = wf - zf; fac4 = zf - yf; fac5 = yf;
    ASSIGN_VERTICES_4(0, 2, 3, 11, 15);
    break ;
  case (4 | 8 | 16 | 32) :
    /* xf >= yf > wf >= zf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - yf; fac3 = yf - wf; fac4 = wf - zf; fac5 = zf;
    ASSIGN_VERTICES_4(0, 2, 6, 7, 15);
    break ;
  case (8 | 16 | 32) :
    /* xf >= yf >= zf > wf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - yf; fac3 = yf - zf; fac4 = zf - wf; fac5 = wf;
    ASSIGN_VERTICES_4(0, 2, 6, 14, 15);
    break ;
  case (2 | 8 | 16) :
    /* xf >= zf > wf >= yf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - zf; fac3 = zf - wf; fac4 = wf - yf; fac5 = yf;
    ASSIGN_VERTICES_4(0, 2, 10, 11, 15);
    break ;
  case (8 | 16) :
    /* xf >= zf > yf > wf */
    fac1 = (1 << GST_IFRACBITS) - xf; fac2 = xf - zf; fac3 = zf - yf; fac4 = yf - wf; fac5 = wf;
    ASSIGN_VERTICES_4(0, 2, 10, 14, 15);
    break ;
  case (1 | 4 | 16 | 32) :
    /* yf > wf >= xf >= zf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - wf; fac3 = wf - xf; fac4 = xf - zf; fac5 = zf;
    ASSIGN_VERTICES_4(0, 4, 5, 7, 15);
    break ;
  case (1 | 4 | 32) :
    /* yf > wf >= zf > xf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - wf; fac3 = wf - zf; fac4 = zf - xf; fac5 = xf;
    ASSIGN_VERTICES_4(0, 4, 5, 13, 15);
    break ;
  case (4 | 16 | 32) :
    /* yf > xf > wf >= zf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - xf; fac3 = xf - wf; fac4 = wf - zf; fac5 = zf;
    ASSIGN_VERTICES_4(0, 4, 6, 7, 15);
    break ;
  case (16 | 32) :
    /* yf > xf >= zf > wf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - xf; fac3 = xf - zf; fac4 = zf - wf; fac5 = wf;
    ASSIGN_VERTICES_4(0, 4, 6, 14, 15);
    break ;
  case (32) :
    /* yf >= zf > xf > wf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - zf; fac3 = zf - xf; fac4 = xf - wf; fac5 = wf;
    ASSIGN_VERTICES_4(0, 4, 12, 14, 15);
    break ;
  case (1 | 32) :
    /* yf >= zf > wf >= xf */
    fac1 = (1 << GST_IFRACBITS) - yf; fac2 = yf - zf; fac3 = zf - wf; fac4 = wf - xf; fac5 = xf;
    ASSIGN_VERTICES_4(0, 4, 12, 13, 15);
    break ;
  case (1 | 2 | 8) :
    /* zf > wf >= xf >= yf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - wf; fac3 = wf - xf; fac4 = xf - yf; fac5 = yf;
    ASSIGN_VERTICES_4(0, 8, 9, 11, 15);
    break ;
  case (1 | 2) :
    /* zf > wf >= yf > xf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - wf; fac3 = wf - yf; fac4 = yf - xf; fac5 = xf;
    ASSIGN_VERTICES_4(0, 8, 9, 13, 15);
    break ;
  case (2 | 8) :
    /* zf > xf > wf >= yf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - xf; fac3 = xf - wf; fac4 = wf - yf; fac5 = yf;
    ASSIGN_VERTICES_4(0, 8, 10, 11, 15);
    break ;
  case (8) :
    /* zf > xf >= yf > wf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - xf; fac3 = xf - yf; fac4 = yf - wf; fac5 = wf;
    ASSIGN_VERTICES_4(0, 8, 10, 14, 15);
    break ;
  case (1) :
    /* zf > yf > wf >= xf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - yf; fac3 = yf - wf; fac4 = wf - xf; fac5 = xf;
    ASSIGN_VERTICES_4(0, 8, 12, 13, 15);
    break ;
  case (0) :
    /* zf > yf > xf > wf */
    fac1 = (1 << GST_IFRACBITS) - zf; fac2 = zf - yf; fac3 = yf - xf; fac4 = xf - wf; fac5 = wf;
    ASSIGN_VERTICES_4(0, 8, 12, 14, 15);
    break ;
  default :
    /* Illegal combination */
    HQFAIL("Tetrahedral interpolator failed") ;
    return FALSE;
  }

  n = colorTable->oncomps;
  for (;;) {
    switch (n) {
    default:
    case 4:
      a0 = v1[ 3 ] * fac1 + v2[ 3 ] * fac2 + v3[ 3 ] * fac3 + v4[ 3 ] * fac4 + v5[ 3 ] * fac5;
      poColorValues[ 3 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 3:
      a0 = v1[ 2 ] * fac1 + v2[ 2 ] * fac2 + v3[ 2 ] * fac3 + v4[ 2 ] * fac4 + v5[ 2 ] * fac5;
      poColorValues[ 2 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 2:
      a0 = v1[ 1 ] * fac1 + v2[ 1 ] * fac2 + v3[ 1 ] * fac3 + v4[ 1 ] * fac4 + v5[ 1 ] * fac5;
      poColorValues[ 1 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    case 1:
      a0 = v1[ 0 ] * fac1 + v2[ 0 ] * fac2 + v3[ 0 ] * fac3 + v4[ 0 ] * fac4 + v5[ 0 ] * fac5;
      poColorValues[ 0 ] = CAST_TO_COLORVALUE(GST_ROUND(a0));
    }

    n -= 4;
    if (n > 0) {
      v1 += 4;
      v2 += 4;
      v3 += 4;
      v4 += 4;
      v5 += 4;
      poColorValues += 4;
    }
    else
      break;
  }

  return TRUE;
}

/**
 * This routine interpolate values for images with 5+ components via a cubic
 * method. In principle, the algorithm is a simple extension of the other cubic
 * methods, but with differences for performance.
 * It is rare to get such images with all input values set to non-zero, so we can
 * avoid some expense by not interpolating the dimensions with zero values. For
 * best performance, the components should be sorted to put the zero values at
 * the end of the list because the biggest number of operations is done on the
 * first components.
 * For the other cubic methods, the mini-cube is fully populated. Even for 4
 * components, that's 16 corners which isn't too bad. For 5+ components the cost
 * of full population is prohibitive in both performance and memory. So, this
 * function cannot assume full population and must ensure the existence of each
 * corner used.
 */
static Bool gst_interpolateN_cubic( colorTableNd *colorTable ,
                                    COLORVALUE *poColorValues )
{
  int32 i;
  int32 n;
  int32 *fractns;
  int32 *indices;
  int32 *incIndices;
  int32 *tmpIndices;
  int32 *tmpIncIndices;
  int32 *origOrder;
  int32 *results;
  int32 nNonZero;
  colorNd *color;
  colorNd **pcolor;
  validMask *valid;

  int32 oncomps ;

  HQASSERT( colorTable , "colorTable NULL in gst_interpolateN" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_interpolateN" ) ;
  HQASSERT( colorTable->incomps > 4 , "incomps <= 4 in gst_interpolateN" ) ;

  fractns = colorTable->fractns ;
  indices = colorTable->indices;
  incIndices = colorTable->incIndices;
  tmpIndices = colorTable->tmpIndices;
  tmpIncIndices = colorTable->tmpIncIndices;
  origOrder = colorTable->N.origOrder;
  results = colorTable->results;


  /* Sort the fractions into ascending order. i.e. with zero values at the end */
  nNonZero = colorTable->incomps;
  for (i = 0; i < colorTable->incomps; i++) {
    origOrder[i] = i;
    /* If we coincide with a grid point then don't interpoate that component */
    if (fractns[i] == 0) {
      origOrder[i] = -1;
      nNonZero--;
    }
  }

  for (i = 0; i < colorTable->incomps; i++) {
    int32 j;

    for (j = i + 1; j < colorTable->incomps; j++) {
      if (fractns[j] > fractns[i]) {
        int32 tmp = fractns[j];
        fractns[j] = fractns[i];
        fractns[i] = tmp;
        tmp = origOrder[j];
        origOrder[j] = origOrder[i];
        origOrder[i] = tmp;
      }
    }
  }

  /* The tmpIndices will be initialised with the origin of the mini-cube, but
   * all other corners will be obtained in sequence by modifying one dimension at a
   * time later.
   */
  for (i = 0; i < colorTable->incomps; i++) {
    HQASSERT(tmpIndices[i] == indices[i] && tmpIncIndices[i] == incIndices[i],
             "Inconsistent indices");
  }


  for (oncomps = 0; oncomps < colorTable->oncomps; oncomps++) {
    int32 fr ;
    int32 comp;
    int32 *srcn;
    int32 *dstn;
    int32 a0;

    /* Get the values for all the required corners. This will explicitly not
     * populate some corners that aren't needed because the fractional part in that
     * dimension is zero.
     * The loop is unrolled with evaluations per loop for performance. If nNonZero
     * is 0 then we'll evaluate 2 corners, not 1, but that's not too bad.
     * NB. Unlike the similar code in gst_getInterpolateColorsN() we cannot rely
     * on the second slot existing because the indices order has been sorted.
     */
    n = 1 << nNonZero;
    for (i = 0; i < n; i += 2) {

      if (!gst_getColorN(colorTable, tmpIndices, &pcolor, &valid))
        return FALSE ;
      HQASSERT(pcolor != NULL , "pcolor NULL");
      if ((*valid & GST_COLOR_PRESENT) == 0) {
        HQASSERT(oncomps == 0, "Color somehow disappeared from the first iteration");
        if (!gst_populateColorN(colorTable,
                                tmpIndices,
                                colorTable->N.colorInfo , colorTable->N.colorType,
                                pcolor, valid))
          return FALSE ;
      }
      color = *pcolor ;
      HQASSERT(color != NULL, "color NULL" ) ;
      results[i] = color->color[oncomps];

      /* The second value in this iteration, with adjusted first indices value */
      tmpIndices[origOrder[0]] = incIndices[origOrder[0]];
      if (!gst_getColorN(colorTable, tmpIndices, &pcolor, &valid))
        return FALSE ;
      HQASSERT(pcolor != NULL , "pcolor NULL");
      if ((*valid & GST_COLOR_PRESENT) == 0) {
        HQASSERT(oncomps == 0, "Color somehow disappeared from the first iteration");
        /* Populate the other corner of the mini-cube in the first dimension. */
        if (!gst_populateColorN(colorTable,
                                tmpIndices,
                                colorTable->N.colorInfo , colorTable->N.colorType,
                                pcolor, valid))
          return FALSE ;
      }
      tmpIndices[origOrder[0]] = indices[origOrder[0]];
      color = *pcolor ;
      HQASSERT(color != NULL, "color NULL" ) ;
      results[i + 1] = color->color[oncomps];

      /* Swap over indices as required to populate mini-cube; note we leave the
       * bottom indices value alone, since it was dealt with above. This is a
       * clever and fast algorithm that walks through the table until we end up
       * populating all the corners in our mini-cube.
       * When input dimension is 1 (i.e. only 2 points to get) do nothing.
       */
      if (n > 2) {
        int32 ip1 = (((i + 2) & (n - 1)) ^ 1);
        int32 ips = 2;
        int32 j = 1;
        do {
          int32 index = tmpIndices[origOrder[j]];
          int32 incIndex = tmpIncIndices[origOrder[j]];
          tmpIndices[origOrder[j]] = incIndex;
          tmpIncIndices[origOrder[j]] = index;
          j++;
          ip1 ^= ips;
          ips <<= 1;
        } while (ip1 != i + 1);
      }
    }

    /* The initial interpolation for the first dimension with a step-up in
     * precision. NB. it is avoided for nNonZero == 0 & 1 which is handled
     * separately below.
     */
    if (nNonZero > 1) {
      fr = fractns[0];    /* fractns is in sorted order */
      for (i = 0, n = 0; n < (1 << nNonZero); i++, n += 2) {
        GST_FIRST_INTERPOLATE( results[i] , fr , results[n] , results[n + 1] ) ;
      }
    }

    /* Do the interpolation. 'nNoneZero' could be any value from 0 -> incomps,
     * so we have to cope with arbitrary entries into the switch.  There is
     * special handling of 0 and 1 values, but the others trickle down from
     * whichever entry value to case 2.
     * NB. The first stage of interpolation has already been done above.
     */
    switch (nNonZero) {

    default:
      /* Interpolate at fractional points, trickling down interpolated points.
       * The first component has already been interpolated above.
       */
      for (comp = 1; comp < nNonZero - 3; comp++) {
        srcn = results ;
        dstn = results ;
        /* n is the number of input values for this iteration */
        n = 1 << (nNonZero - comp);
        fr = fractns[comp];    /* fractns is in sorted order */
        do {
          GST_INTERPOLATE( dstn[ 0x0 ] , fr , srcn[ 0x0 ] , srcn[ 0x1 ] ) ;
          GST_INTERPOLATE( dstn[ 0x1 ] , fr , srcn[ 0x2 ] , srcn[ 0x3 ] ) ;
          GST_INTERPOLATE( dstn[ 0x2 ] , fr , srcn[ 0x4 ] , srcn[ 0x5 ] ) ;
          GST_INTERPOLATE( dstn[ 0x3 ] , fr , srcn[ 0x6 ] , srcn[ 0x7 ] ) ;
          GST_INTERPOLATE( dstn[ 0x4 ] , fr , srcn[ 0x8 ] , srcn[ 0x9 ] ) ;
          GST_INTERPOLATE( dstn[ 0x5 ] , fr , srcn[ 0xA ] , srcn[ 0xB ] ) ;
          GST_INTERPOLATE( dstn[ 0x6 ] , fr , srcn[ 0xC ] , srcn[ 0xD ] ) ;
          GST_INTERPOLATE( dstn[ 0x7 ] , fr , srcn[ 0xE ] , srcn[ 0xF ] ) ;
          dstn += 8 ;
          srcn += 16 ;
        } while (( n -= 16 ) > 0 ) ;
      }
      /* DROP THROUGH */

    case 4:
      /* Interpolate the last three. */

      srcn = results ;
      dstn = results ;
      fr = fractns[nNonZero - 3];    /* fractns is in sorted order */
      GST_INTERPOLATE( dstn[ 0x0 ] , fr , srcn[ 0 ] , srcn[ 1 ] ) ;
      GST_INTERPOLATE( dstn[ 0x1 ] , fr , srcn[ 2 ] , srcn[ 3 ] ) ;
      GST_INTERPOLATE( dstn[ 0x2 ] , fr , srcn[ 4 ] , srcn[ 5 ] ) ;
      GST_INTERPOLATE( dstn[ 0x3 ] , fr , srcn[ 6 ] , srcn[ 7 ] ) ;
      /* DROP THROUGH */

    case 3:
      srcn = results ;
      dstn = results ;
      fr = fractns[nNonZero - 2];    /* fractns is in sorted order */
      GST_INTERPOLATE( dstn[ 0x0 ] , fr , srcn[ 0 ] , srcn[ 1 ] ) ;
      GST_INTERPOLATE( dstn[ 0x1 ] , fr , srcn[ 2 ] , srcn[ 3 ] ) ;
      /* DROP THROUGH */

    case 2:
      srcn = results ;
      fr = fractns[nNonZero - 1];    /* fractns is in sorted order */
      GST_INTERPOLATE( a0 , fr , srcn[ 0 ] , srcn[ 1 ] ) ;
      a0 = GST_ROUND( a0 ) ;
      /* Don't drop through. Cases 0 & 1 is handled specially. */
      break;

    case 1:
      srcn = results ;
      fr = fractns[nNonZero - 1];    /* fractns is in sorted order */
      GST_FIRST_INTERPOLATE( a0 , fr , srcn[ 0 ] , srcn[ 1 ] ) ;
      a0 = GST_ROUND( a0 ) ;
      /* Don't drop through. Case 0 is handled specially. */
      break;

    case 0:
      /* We can just use the value from the mini-cube origin because there is no
       * interpolation and no step-up in precision to deal with.
       */
      a0 = results[ 0x0 ];
      break;
    }

#ifdef GST_EVAL_INT_ERROR
    { int32 d = poColorValues[oncomps] - a0 ;
      INLINE_ABS32(d, d) ;
      colorTable->nInterpolateError += d ;
      colorTable->nInterpolate += 1 ;
    }
#endif

    poColorValues[oncomps] = CAST_TO_COLORVALUE(a0) ;
  }

  return TRUE;
}

/**
 * This routine interpolates values for images with 5+ components via the
 * tetrahedral method. In principle, the algorithm is an extension of the other
 * tetrahedral methods for 3 and 4 components.
 * In order to find the correct n-hedron, the fractional part of the input
 * color values must be sorted into descending order.
 * It is rare to get such images with all input values set to non-zero, so we can
 * avoid some expense by not interpolating the dimensions with zero values. Since
 * the values must be sorted in order, missing out the zero values is trivial.
 * For the other tetrahedral methods, the mini-cube is fully populated. Even for 4
 * components, that's 16 corners which isn't too bad. For 5+ components the cost
 * of full population is prohibitive in both performance and memory. So, this
 * function cannot assume full population and must ensure the existence of each
 * corner used. The number of corners used in the algorithm is (N + 1) which is
 * far less than the number in a mini-cube for 5+ components.
 */
static Bool gst_interpolateN_tetrahedral( colorTableNd *colorTable ,
                                          COLORVALUE *poColorValues )
{
  int32 i;
  int32 n;
  int32 *fractns;
  int32 *indices;
  int32 *incIndices;
  int32 *tmpIndices;
  int32 *origOrder;
  int32 *results;
  int32 nNonZero;

  HQASSERT( colorTable , "colorTable NULL in gst_interpolateN" ) ;
  HQASSERT( poColorValues , "poColorValues NULL in gst_interpolateN" ) ;
  HQASSERT( colorTable->incomps > 4 , "incomps <= 4 in gst_interpolateN" ) ;

  fractns = colorTable->fractns ;
  indices = colorTable->indices;
  incIndices = colorTable->incIndices;
  tmpIndices = colorTable->tmpIndices;
  origOrder = colorTable->N.origOrder;
  results = colorTable->results;


  /* Sort the fractions into ascending order. i.e. with zero values at the end */
  nNonZero = colorTable->incomps;
  for (i = 0; i < colorTable->incomps; i++) {
    origOrder[i] = i;
    /* If we coincide with a grid point then don't interpolate that component */
    if (fractns[i] == 0) {
      origOrder[i] = -1;
      nNonZero--;
    }
  }

  for (i = 0; i < colorTable->incomps; i++) {
    int32 j;

    for (j = i + 1; j < colorTable->incomps; j++) {
      if (fractns[j] > fractns[i]) {
        int32 tmp = fractns[j];
        fractns[j] = fractns[i];
        fractns[i] = tmp;
        tmp = origOrder[j];
        origOrder[j] = origOrder[i];
        origOrder[i] = tmp;
      }
    }
  }

  for (i = 0; i < colorTable->oncomps; i++)
    results[i] = 0;

  /* The tmpIndices will be initialised with the origin of the mini-cube, but
   * other corners will be obtained in sequence by later modifying one dimension
   * at a time.
   */
  for (i = 0; i < colorTable->incomps; i++)
    tmpIndices[i] = indices[i];

  /* This loop obtains the color of each of the (N + 1) vertices in turn and
   * accumulates the final value for all output components.
   */
  for (i = 0; i < nNonZero + 1; i++) {
    COLORVALUE *val;
    colorNd *color;
    colorNd **pcolor;
    validMask *valid;
    int32 fac;

    if (i > 0) {
      HQASSERT(origOrder[i - 1] >= 0, "Negative order");
      tmpIndices[origOrder[i - 1]] = incIndices[origOrder[i - 1]];
    }

    if (!gst_getColorN(colorTable, colorTable->tmpIndices, &pcolor, &valid))
      return FALSE ;
    HQASSERT(pcolor != NULL , "pcolor NULL");
    if ((*valid & GST_COLOR_PRESENT) == 0) {
      if (!gst_populateColorN(colorTable,
                              tmpIndices,
                              colorTable->N.colorInfo , colorTable->N.colorType,
                              pcolor, valid))
        return FALSE ;
    }
    color = *pcolor ;
    HQASSERT(color != NULL, "color NULL" ) ;

    /* Assign factors to the ordered list. Ths steps up the precision of
     * intermediate results.
     * NB. fractns is in sorted order.
     */
    if (i == 0)
      fac = (1 << GST_IFRACBITS) - fractns[0];
    else if (i == nNonZero)
      fac = fractns[i - 1];
    else
      fac = fractns[i - 1] - fractns[i];

    val = color->color;
    results = colorTable->results;

    /* Accumulate the output values for each corner in the n-hedron, for all
     * output components.
     */
    n = colorTable->oncomps;
    for (;;) {
      switch (n) {
      default:
      case 4:
        results[3] += val[3] * fac;
      case 3:
        results[2] += val[2] * fac;
      case 2:
        results[1] += val[1] * fac;
      case 1:
        results[0] += val[0] * fac;
      }

      n -= 4;
      if (n > 0) {
        val += 4;
        results += 4;
      }
      else
        break;
    }
  }

  /* Adjust the output values to required precision after the accumulation was
   * done in higher precision.
   */
  results = colorTable->results;
  for (i = 0; i < colorTable->oncomps; i++)
    poColorValues[i] = CAST_TO_COLORVALUE(GST_ROUND(results[i]));

  return TRUE;
}

/** Create the top-level structure for the table. */
static colorTableNd *gst_allocTable( int32 incomps , int32 oncomps )
{
  colorTableNd *colorTable ;
  /* results is used in 2 ways, oncomps is used in gst_interpolateN_tetrahedral. */
  size_t results_size = max((1 << incomps), oncomps);
  corecontext_t *context = get_core_context();
  COLOR_PAGE_PARAMS *colorPageParams = &context->page->colorPageParams;
  deferred_alloc_t *da = deferred_alloc_init(1);
  MEMORY_REQUEST_VARS(color_table, COLOR_TABLE, sizeof(colorTableNd), 1);
  MEMORY_REQUEST_VARS(ints, COLOR_TABLE, 0, 6);
  MEMORY_REQUEST_VARS(floats, COLOR_TABLE, 0, 4);
  MEMORY_REQUEST_VARS(result, COLOR_TABLE, 0, 1);
  MEMORY_REQUEST_VARS(errordc, COLOR_TABLE, 0, 1);
  Bool succ;

  if ( da == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  ints_request.size = incomps * sizeof(int32);
  floats_request.size = incomps * sizeof(float);
  result_request.size = results_size * sizeof(int32);
  errordc_request.size = oncomps * sizeof(float);
  succ = deferred_alloc_add_simple(da, &color_table_request,
                                   mm_pool_color, color_table_blocks)
    && deferred_alloc_add_simple(da, &ints_request,
                                 mm_pool_color, ints_blocks)
    && deferred_alloc_add_simple(da, &floats_request,
                                 mm_pool_color, floats_blocks)
    && deferred_alloc_add_simple(da, &result_request,
                                 mm_pool_color, result_blocks)
    && deferred_alloc_add_simple(da, &errordc_request,
                                 mm_pool_color, errordc_blocks)
    && deferred_alloc_realize(da, mm_cost_normal, context);
  deferred_alloc_finish(da);
  if ( !succ ) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  colorTable = color_table_blocks[0];

  /* Copies of params from the current page to avoid TLS access */
  colorTable->fasterColorMethod = colorPageParams->fasterColorMethod;
  colorTable->fasterColorSmoothness = colorPageParams->fasterColorSmoothness;

  colorTable->incomps = incomps ;
  colorTable->oncomps = oncomps ;
  colorTable->cubeSide = maxTableIndex(colorPageParams) + 1 ;
  colorTable->maxTableIndex = maxTableIndex(colorPageParams) ;
  colorTable->cubeSideBits = 0 ;
  {
    int32 n = colorTable->maxTableIndex;
    while (n > 0) {
      colorTable->cubeSideBits++ ;
      n >>= 1;
    }
  }
  colorTable->indices = ints_blocks[0];
  colorTable->incIndices = ints_blocks[1];
  colorTable->tmpIndices = ints_blocks[2];
  colorTable->tmpIncIndices = ints_blocks[3];
  colorTable->indicesHash = 0 ;
  colorTable->indicesId = INDICES_ID_INVALID ;
  colorTable->fractns = ints_blocks[4];
  colorTable->errordc = errordc_blocks[0];
  colorTable->errordv = 0.0 ;
  colorTable->results = result_blocks[0];
  colorTable->cornerPtrs = NULL ;
  colorTable->cornerPtrsCache = NULL ;
  colorTable->cornerPtrsCacheBits = 0 ;
  colorTable->cornerPtrsCacheSize = 0 ;
  colorTable->cornerPtrsCacheMask = 0 ;
  colorTable->cornerPtrsFallback = NULL ;

  colorTable->N.colorInfo = NULL ;
  colorTable->N.colorType = GSC_UNDEFINED ;
  colorTable->N.origOrder = ints_blocks[5];

  colorTable->interpolatefunc = NULL ;

  colorTable->scratch = floats_blocks[0];
  colorTable->rangeb = floats_blocks[1];
  colorTable->ranges = floats_blocks[2];
  colorTable->rangen = floats_blocks[3];
  colorTable->beingused = FALSE;
  colorTable->cubemru = NULL ;
  colorTable->cube.cn = NULL ;

  colorTable->mm_pool_table = NULL ;
  colorTable->purgeable = TRUE;

  if ( mm_pool_create(&colorTable->mm_pool_table, TABLE_POOL_TYPE,
                      TABLE_POOL_PARAMS) != MM_SUCCESS ) {
    gst_freeTable(&colorTable);
    (void) error_handler( VMERROR );
    return NULL ;
  }

  /* Need to set this to invalid value, for cache lookup to work. */
  INVALIDATE_COLORCUBE_CACHE_LOOKUP(colorTable) ;

  return colorTable ;
}


/** Destroy the top-level structure for the table. */
static void gst_freeTable(colorTableNd **pColorTable)
{
  colorTableNd *colorTable;
  int32 incomps;
  int32 oncomps;
  size_t results_size;

  HQASSERT(pColorTable && *pColorTable != NULL, "pColorTable NULL");

  colorTable = *pColorTable;
  incomps = colorTable->incomps, oncomps = colorTable->oncomps;
  results_size = max((1 << incomps), oncomps);

  if (colorTable->mm_pool_table != NULL)
    mm_pool_destroy(colorTable->mm_pool_table);

  mm_free(mm_pool_color, colorTable->indices, incomps * sizeof(int32));
  mm_free(mm_pool_color, colorTable->incIndices, incomps * sizeof(int32));
  mm_free(mm_pool_color, colorTable->tmpIndices, incomps * sizeof(int32));
  mm_free(mm_pool_color, colorTable->tmpIncIndices, incomps * sizeof(int32));
  mm_free(mm_pool_color, colorTable->fractns, incomps * sizeof(int32));
  mm_free(mm_pool_color, colorTable->errordc, oncomps * sizeof(float));
  mm_free(mm_pool_color, colorTable->results, results_size * sizeof(int32));
  mm_free(mm_pool_color, colorTable->N.origOrder, incomps * sizeof(int32));
  mm_free(mm_pool_color, colorTable->scratch, incomps * sizeof(float));
  mm_free(mm_pool_color, colorTable->rangeb, incomps * sizeof(float));
  mm_free(mm_pool_color, colorTable->ranges, incomps * sizeof(float));
  mm_free(mm_pool_color, colorTable->rangen, incomps * sizeof(float));

  mm_free( mm_pool_color, colorTable, sizeof(colorTableNd) );

  *pColorTable = NULL;
}


/*----------------------------------------------------------------------------*/

/**
 * Allocate a 1d array in the sparse table and zero its contents.
 */
static colorCubeNd *gst_allocCubeN( colorTableNd *colorTable, int32 cubeSide )
{
  colorCubeNd *cn ;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL in gst_allocCubeN" ) ;

  cn = mm_alloc(colorTable->mm_pool_table,
                sizeof(colorCubeNd) * cubeSide,
                MM_ALLOC_CLASS_COLOR_TABLE);
  if ( cn == NULL ) {
    (void) error_handler( VMERROR );
    return NULL;
  }
  DEBUG_INCREMENT(colorTable->nMemCubeN, sizeof( colorCubeNd ) * cubeSide) ;
  DEBUG_INCREMENT(colorTable->nMemTotal, sizeof( colorCubeNd ) * cubeSide) ;
  colorTable->purgeable = TRUE;
  HqMemZero(cn, sizeof(colorCubeNd) * cubeSide);

  return cn ;
}

/**
 * Free a 1d array in the sparse table.
 */
static void gst_freeCubeN( colorTableNd *colorTable, colorCubeNd *cn,
                           int32 cubeSide )
{
  int32 i;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL in gst_freeCubeN" ) ;
  HQASSERT( cn , "pcube NULL in gst_freeCubeN" ) ;

  for (i = 0; i < cubeSide; i++)
    HQASSERT(cn[i].cn == NULL, "cn memory leak");

  mm_free( colorTable->mm_pool_table , cn , sizeof( colorCubeNd ) * cubeSide ) ;
  DEBUG_DECREMENT(colorTable->nMemCubeN, sizeof( colorCubeNd ) * cubeSide) ;
  DEBUG_DECREMENT(colorTable->nMemTotal, sizeof( colorCubeNd ) * cubeSide) ;
}

/**
 * Allocate a final 1d array in the sparse table and zero its contents.
 */
static colorCube1d *gst_allocCube1( colorTableNd *colorTable, int32 cubeSide )
{
  colorCube1d *c1 ;
  int32 size;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL in gst_allocCube1" ) ;

  size = sizeof(colorCube1d) +
         cubeSide * sizeof(validMask) +         /* valid */
         (cubeSide - 1) * sizeof(colorNd *);    /* color (uses the struct hack */

  c1 = mm_alloc( colorTable->mm_pool_table , size , MM_ALLOC_CLASS_COLOR_TABLE ) ;
  if ( c1 == NULL ) {
    (void) error_handler( VMERROR );
    return NULL;
  }
  DEBUG_INCREMENT(colorTable->nMemCube1, size) ;
  DEBUG_INCREMENT(colorTable->nMemTotal, size) ;
  colorTable->purgeable = TRUE;
  HqMemZero(c1, size);

  c1->valid = (validMask *) &c1->color[cubeSide];    /* Allocated at the end of the struct */

  return c1 ;
}

/**
 * Free a final 1d array in the sparse table.
 */
static void gst_freeCube1( colorTableNd *colorTable, colorCube1d *c1,
                           int32 cubeSide )
{
  int32 i;
  int32 size;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL in gst_freeCube1" ) ;
  HQASSERT( c1 , "c1 NULL in gst_freeCube1" ) ;

  for (i = 0; i < cubeSide; i++) {
    HQASSERT(c1->color[i] == NULL, "c1->color memory leak");
    HQASSERT((c1->valid[i] & GST_COLOR_PRESENT) == 0, "Inconsistent color present flag");
  }

  size = sizeof(colorCube1d) +
         cubeSide * sizeof(validMask) +         /* valid */
         (cubeSide - 1) * sizeof(colorNd *);    /* color (uses the struct hack */

  mm_free( colorTable->mm_pool_table , c1 , size) ;

  DEBUG_DECREMENT(colorTable->nMemCube1, size) ;
  DEBUG_DECREMENT(colorTable->nMemTotal, size) ;
}

/**
 * This routine allocate color storage.
 * Storage is done using 16 bit COLORVALUES.
 */
static colorNd *gst_allocColorN( colorTableNd *colorTable, int32 oncomps )
{
  int32 size ;
  colorNd *color ;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL in gst_allocColorN" ) ;
  HQASSERT( oncomps > 0 , "oncomps should be > 0 in gst_allocColorN" ) ;

  /* The struct hack was used, so allocate the extra components as extras */
  size = sizeof(colorNd) + (oncomps - 1) * sizeof( COLORVALUE ) ;

  color = mm_alloc( colorTable->mm_pool_table , size , MM_ALLOC_CLASS_COLOR_TABLE ) ;
  if ( color == NULL ) {
    (void) error_handler( VMERROR );
    return NULL;
  }
  colorTable->purgeable = TRUE;
  DEBUG_INCREMENT(colorTable->nMemColor, size) ;
  DEBUG_INCREMENT(colorTable->nMemTotal, size) ;

  return color ;
}

/**
 * This routine frees color storage. Storage is done using 16 bit COLORVALUES.
 */
static void gst_freeColorN( colorTableNd *colorTable, colorNd **pcolor,
                            int32 oncomps )
{
  int32 size ;
  colorNd *color ;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL in gst_freeColorN" ) ;
  HQASSERT( pcolor , "pcolor NULL in gst_freeColorN" ) ;
  HQASSERT( oncomps > 0 , "oncomps should be > 0 in gst_freeColorN" ) ;

  color = *pcolor ;
  HQASSERT( color , "color unexpectedly NULL" ) ;
  *pcolor = NULL ;

  size = sizeof(colorNd) + (oncomps - 1) * sizeof( COLORVALUE ) ;
  mm_free( colorTable->mm_pool_table , color , size ) ;

  DEBUG_DECREMENT(colorTable->nMemColor, size) ;
  DEBUG_DECREMENT(colorTable->nMemTotal, size) ;
}

/*----------------------------------------------------------------------------*/
/**
 * This routine allocates an entry in the corner pointers cache for optimising
 * access to the list of color values at each corner of a mini-cube.
 */
static ptrsEntry *gst_allocCornerPtrsEntry( colorTableNd *colorTable,
                                            int32 incomps )
{
  int32 size;
  ptrsEntry *cacheEntry;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL" ) ;
  HQASSERT( incomps > 0 , "incomps should be > 0" ) ;

  cacheEntry = mm_alloc( colorTable->mm_pool_table ,
                         sizeof(ptrsEntry),
                         MM_ALLOC_CLASS_COLOR_TABLE ) ;
  if (cacheEntry == NULL) {
    /* Error handling done in clients */
    return NULL;
  }

  DEBUG_INCREMENT(colorTable->nMemCornerPtrs, sizeof(ptrsEntry)) ;
  DEBUG_INCREMENT(colorTable->nMemTotal, sizeof(ptrsEntry)) ;

  cacheEntry->id = INDICES_ID_INVALID;
  size = ( (size_t)1 << incomps ) * sizeof( COLORVALUE * );
  cacheEntry->cornerPtrs = mm_alloc( colorTable->mm_pool_table ,
                                     size, MM_ALLOC_CLASS_COLOR_TABLE ) ;
  if (cacheEntry->cornerPtrs == NULL) {
    mm_free(colorTable->mm_pool_table, cacheEntry, sizeof(ptrsEntry));
    /* Error handling done in clients */
    return NULL;
  }
  colorTable->purgeable = TRUE;
  DEBUG_INCREMENT(colorTable->nMemCornerPtrs, size) ;
  DEBUG_INCREMENT(colorTable->nMemTotal, size) ;

  return cacheEntry;
}

/**
 * This routine frees an entry in the corner pointers cache.
 */
static void gst_freeCornerPtrsEntry( colorTableNd *colorTable,
                                     ptrsEntry **pCacheEntry,
                                     int32 incomps )
{
  int32 size ;
  ptrsEntry *cacheEntry;

  HQASSERT( colorTable->mm_pool_table , "mm_pool_table NULL" ) ;
  HQASSERT( pCacheEntry , "pCacheEntry NULL" ) ;
  HQASSERT( incomps > 0 , "incomps should be > 0" ) ;

  cacheEntry = *pCacheEntry ;
  HQASSERT( cacheEntry , "cacheEntry unexpectedly NULL" ) ;
  *pCacheEntry = NULL ;

  size = ( (size_t)1 << incomps ) * sizeof( COLORVALUE * );
  mm_free( colorTable->mm_pool_table , cacheEntry->cornerPtrs , size);

  DEBUG_DECREMENT(colorTable->nMemCornerPtrs, size) ;
  DEBUG_DECREMENT(colorTable->nMemTotal, size) ;

  mm_free( colorTable->mm_pool_table , cacheEntry , sizeof(ptrsEntry) );

  DEBUG_DECREMENT(colorTable->nMemCornerPtrs, sizeof(ptrsEntry)) ;
  DEBUG_DECREMENT(colorTable->nMemTotal, sizeof(ptrsEntry)) ;
}

/**
 * This routine calculates the constants for each colorant in the color space
 * that are required to go from a) the range of the table to that of the color
 * space, and, b) the range of the scaled input colors to that of the
 * color space.
 */
static Bool gst_rangeinit( GS_COLORinfo *colorInfo , int32 colorType ,
                           int32 incomps , int32 maxTableIndex ,
                           float *rangeb , float *ranges , float *rangen )
{
  int32 i ;

  HQASSERT( colorInfo , "colorInfo NULL in gst_rangeinit" ) ;
  HQASSERT( incomps > 0 , "incomps should be > 0 in gst_rangeinit" ) ;
  HQASSERT( rangeb , "rangeb NULL in gst_rangeinit" ) ;
  HQASSERT( ranges , "ranges NULL in gst_rangeinit" ) ;
  HQASSERT( rangen , "rangen NULL in gst_rangeinit" ) ;

  for ( i = 0 ; i < incomps ; ++i ) {
    SYSTEMVALUE range1m0 ;
    SYSTEMVALUE range[ 2 ] ;

    if ( ! gsc_range( colorInfo ,  colorType , i , range ))
      return FALSE ;

    /* Calculate constants so we can perform "a * x + b". */
    range1m0 = range[ 1 ] - range[ 0 ] ;
    rangeb[ i ] = ( float )range[ 0 ] ;
    ranges[ i ] = ( float )( range1m0 / maxTableIndex ) ;
    rangen[ i ] = ( float )( range1m0 / SCALED_COLOR(maxTableIndex) ) ;
  }

  return TRUE ;
}

/**
 * This routine calculates the error multipliers that are required to go from
 * a device code delta to a normalised percentage delta. These are required
 * since we calculate the error based on a measurement of euclidean distance
 * squared.
 */
static Bool gst_errordcinit(GS_COLORinfo *colorInfo, int32 colorType,
                            int32 oncomps, USERVALUE fasterColorSmoothness,
                            float *errordc, float *errordv)
{
  int32 i ;
  SPOTNO spotno = 0 ;
  HTTYPE httype;
  int32 nColorants ;
  COLORANTINDEX *iColorants ;

  HQASSERT( colorInfo , "colorInfo NULL in gst_errordcinit" ) ;
  HQASSERT( oncomps , "oncomps should be > 0 in gst_errordcinit" ) ;
  HQASSERT( errordc , "errordc NULL in gst_errordcinit" ) ;
  HQASSERT( errordv , "errordv NULL in gst_errordcinit" ) ;

  /* We need the list of device color colorants so that we can determine
   * the number of gray levels for each one.
   */
  if ( ! gsc_getDeviceColorColorants( colorInfo , colorType ,
                                      & nColorants ,
                                      & iColorants ))
    return FALSE ;
  HQASSERT( oncomps == nColorants , "Differing idea of ncomps" ) ;

  spotno = gsc_getSpotno(colorInfo);
  httype = gsc_getRequiredReproType(colorInfo, colorType);
  for (i = 0; i < oncomps; i++) {
    /* Calculate a normalisation factor which maps this colorant's gray
     * levels into a [0 1.0] interval. */
    errordc[i] =  1.0f / ht_getClear(spotno, httype, iColorants[i],
                                     colorInfo->deviceRS);
  }

  /* Default for this is an average of 4% in each output channel;
   * use the square in calculations to avoid sqrt.
   */
  *errordv = fasterColorSmoothness * fasterColorSmoothness * oncomps ;

  return TRUE ;
}

/**
 * A series of functions to obtain 'indices' that identify the mini-cube
 * containing the current color value. The corners of the mini-cube will be used
 * for interpolating an estimate of pixel's color.
 *
 * The functions also calculate the (8 bit) fractional part of the color which is
 * also required for the interpolation.
 *
 * For efficiency, we reuse the data from the last used mini-cube if it is the
 * same. This is derived from the "or" of all the differences in the mini-cube
 * 'indices' between the previous set of indices and this one. If this "or" ends
 * up being zero then we can use the previous mini-cube.
 *
 * This particular function is specialised for when the cornerPtrs cache isn't
 * active.
 */
static Bool gst_getIndicesNoCache(colorTableNd *colorTable, int32 *piColorValues)
{
  int32 icrdiff ;
  int32 index ;
  int32 value ;
  int32 n ;
  int32 *indices ;
  int32 *fractns ;

  HQASSERT( colorTable , "colorTable NULL");
  HQASSERT( piColorValues , "piColorValues NULL");

  indices = colorTable->indices ;
  fractns = colorTable->fractns ;

  icrdiff = 0 ;

  n = colorTable->incomps;
  for (;;) {
    switch (n) {
    default:
    case 4:
      value = piColorValues[3] ;
      index = value >> GSC_FRACNSHIFT ;
      HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
      icrdiff |= ( indices[3] - index ) ;
      indices[3] = index ;
      fractns[3] = value & GSC_FRACNMASK ;

    case 3:
      value = piColorValues[2] ;
      index = value >> GSC_FRACNSHIFT ;
      HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
      icrdiff |= ( indices[2] - index ) ;
      indices[2] = index ;
      fractns[2] = value & GSC_FRACNMASK ;

    case 2:
      value = piColorValues[1] ;
      index = value >> GSC_FRACNSHIFT ;
      HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
      icrdiff |= ( indices[1] - index ) ;
      indices[1] = index ;
      fractns[1] = value & GSC_FRACNMASK ;

    case 1:
      value = piColorValues[0] ;
      index = value >> GSC_FRACNSHIFT ;
      HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
      icrdiff |= ( indices[0] - index ) ;
      indices[0] = index ;
      fractns[0] = value & GSC_FRACNMASK ;
    }

    n -= 4;
    if (n > 0) {
      piColorValues += 4;
      indices += 4;
      fractns += 4;
    }
    else
      break;
  }

#ifdef ASSERT_BUILD
  if (icrdiff != 0) {
    int32 incomps ;
    for (incomps = 0; incomps < colorTable->incomps; incomps++) {
      colorTable->incIndices[incomps] = -1;
      colorTable->tmpIndices[incomps] = -1;
      colorTable->tmpIncIndices[incomps] = -1;
    }
  }
#endif

  HQASSERT(colorTable->indicesId == INDICES_ID_INVALID,
           "The cornerPtrs id should be invalid");

  return ( icrdiff == 0 );
}

/**
 * The same comments as for gst_getIndicesNoCache() apply.
 *
 * Except this function is specialised for when the cornerPtrs cache is active and,
 * there are 1 components, i.e. icolorCacheBits != 0 and incomps == 1, when
 * we calculate the hash and id values.
 */
static Bool gst_getIndicesWithCache1C(colorTableNd *colorTable, int32 *piColorValues)
{
  int32 icrdiff, indicesHash, index, value, *indices, *fractns;
  uint32 indicesId ;

  HQASSERT(colorTable , "colorTable NULL");
  HQASSERT(piColorValues , "piColorValues NULL");

  indices = colorTable->indices;
  fractns = colorTable->fractns;

  value = piColorValues[0];
  index = value >> GSC_FRACNSHIFT;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" );
  icrdiff = ( indices[0] - index );
  indicesId = index;
  indicesHash = index & colorTable->cornerPtrsCacheMask;
  indices[0] = index;
  fractns[0] = value & GSC_FRACNMASK;

  HQASSERT(colorTable->cubeSideBits * colorTable->incomps <= INDICES_HASH_BITS,
           "Too many components to use pointer cache");
  colorTable->indicesId = indicesId ;
  colorTable->indicesHash = indicesHash ;

  HQASSERT((colorTable->indicesId & INDICES_ID_INVALID) == 0,
           "The invalid cornerPtrs id bit shouldn't be set in normal use");

  HQASSERT((int32) colorTable->indicesHash <= colorTable->cornerPtrsCacheSize - 1,
           "indicesHash is out of scope");

#ifdef ASSERT_BUILD
  if (icrdiff != 0) {
    int32 incomps ;
    for (incomps = 0; incomps < colorTable->incomps; incomps++) {
      colorTable->incIndices[incomps] = -1;
      colorTable->tmpIndices[incomps] = -1;
      colorTable->tmpIncIndices[incomps] = -1;
    }
  }
  if (colorTable->cornerPtrsCacheBits == 0)
    colorTable->indicesId = INDICES_ID_INVALID;
#endif

  return ( icrdiff == 0 );
}

/**
 * The same comments as for gst_getIndicesNoCache() apply.
 *
 * Except this function is specialised for when the cornerPtrs cache is active and,
 * there are 2 components, i.e. icolorCacheBits != 0 and incomps == 2, when
 * we calculate the hash and id values.
 */
static Bool gst_getIndicesWithCache2C(colorTableNd *colorTable, int32 *piColorValues)
{
  int32 icrdiff, indicesHash, index, value, *indices, *fractns;
  uint32 indicesId ;

  HQASSERT(colorTable , "colorTable NULL");
  HQASSERT(piColorValues , "piColorValues NULL");

  indices = colorTable->indices;
  fractns = colorTable->fractns;

  value = piColorValues[0];
  index = value >> GSC_FRACNSHIFT;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" );
  icrdiff = ( indices[0] - index );
  indicesId = index;
  indicesHash = index & colorTable->cornerPtrsCacheMask;
  indices[0] = index;
  fractns[0] = value & GSC_FRACNMASK;

  value = piColorValues[1];
  index = value >> GSC_FRACNSHIFT;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" );
  icrdiff |= ( indices[1] - index );
  indicesId <<= colorTable->cubeSideBits;
  indicesId += index;
  indicesHash <<= colorTable->cornerPtrsCacheBits;
  indicesHash += index & colorTable->cornerPtrsCacheMask;
  indices[1] = index;
  fractns[1] = value & GSC_FRACNMASK;

  HQASSERT(colorTable->cubeSideBits * colorTable->incomps <= INDICES_HASH_BITS,
           "Too many components to use pointer cache");
  colorTable->indicesId = indicesId ;
  colorTable->indicesHash = indicesHash ;

  HQASSERT((colorTable->indicesId & INDICES_ID_INVALID) == 0,
           "The invalid cornerPtrs id bit shouldn't be set in normal use");

  HQASSERT((int32) colorTable->indicesHash <= colorTable->cornerPtrsCacheSize - 1,
           "indicesHash is out of scope");

#ifdef ASSERT_BUILD
  if (icrdiff != 0) {
    int32 incomps ;
    for (incomps = 0; incomps < colorTable->incomps; incomps++) {
      colorTable->incIndices[incomps] = -1;
      colorTable->tmpIndices[incomps] = -1;
      colorTable->tmpIncIndices[incomps] = -1;
    }
  }
  if (colorTable->cornerPtrsCacheBits == 0)
    colorTable->indicesId = INDICES_ID_INVALID;
#endif

  return ( icrdiff == 0 );
}

/**
 * The same comments as for gst_getIndicesNoCache() apply.
 *
 * Except this function is specialised for when the cornerPtrs cache is active and,
 * there are 3 components, i.e. icolorCacheBits != 0 and incomps == 3, when
 * we calculate the hash and id values.
 */
static Bool gst_getIndicesWithCache3C(colorTableNd *colorTable, int32 *piColorValues)
{
  int32 icrdiff, indicesHash, index, value, *indices, *fractns;
  uint32 indicesId ;

  HQASSERT(colorTable , "colorTable NULL");
  HQASSERT(piColorValues , "piColorValues NULL");

  indices = colorTable->indices;
  fractns = colorTable->fractns;

  value = piColorValues[0];
  index = value >> GSC_FRACNSHIFT;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" );
  icrdiff = ( indices[0] - index );
  indicesId = index;
  indicesHash = index & colorTable->cornerPtrsCacheMask;
  indices[0] = index;
  fractns[0] = value & GSC_FRACNMASK;

  value = piColorValues[1];
  index = value >> GSC_FRACNSHIFT;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" );
  icrdiff |= ( indices[1] - index );
  indicesId <<= colorTable->cubeSideBits;
  indicesId += index;
  indicesHash <<= colorTable->cornerPtrsCacheBits;
  indicesHash += index & colorTable->cornerPtrsCacheMask;
  indices[1] = index;
  fractns[1] = value & GSC_FRACNMASK;

  value = piColorValues[2];
  index = value >> GSC_FRACNSHIFT;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" );
  icrdiff |= ( indices[2] - index );
  indicesId <<= colorTable->cubeSideBits;
  indicesId += index;
  indicesHash <<= colorTable->cornerPtrsCacheBits;
  indicesHash += index & colorTable->cornerPtrsCacheMask;
  indices[2] = index;
  fractns[2] = value & GSC_FRACNMASK;

  HQASSERT(colorTable->cubeSideBits * colorTable->incomps <= INDICES_HASH_BITS,
           "Too many components to use pointer cache");
  colorTable->indicesId = indicesId ;
  colorTable->indicesHash = indicesHash ;

  HQASSERT((colorTable->indicesId & INDICES_ID_INVALID) == 0,
           "The invalid cornerPtrs id bit shouldn't be set in normal use");

  HQASSERT((int32) colorTable->indicesHash <= colorTable->cornerPtrsCacheSize - 1,
           "indicesHash is out of scope");

#ifdef ASSERT_BUILD
  if (icrdiff != 0) {
    int32 incomps ;
    for (incomps = 0; incomps < colorTable->incomps; incomps++) {
      colorTable->incIndices[incomps] = -1;
      colorTable->tmpIndices[incomps] = -1;
      colorTable->tmpIncIndices[incomps] = -1;
    }
  }
  if (colorTable->cornerPtrsCacheBits == 0)
    colorTable->indicesId = INDICES_ID_INVALID;
#endif

  return ( icrdiff == 0 );
}

/**
 * The same comments as for gst_getIndicesNoCache() apply.
 *
 * Except this function is specialised for when the cornerPtrs cache is active and,
 * there are 4 components, i.e. icolorCacheBits != 0 and incomps == 4, when
 * we calculate the hash and id values.
 */
static Bool gst_getIndicesWithCache4C(colorTableNd *colorTable, int32 *piColorValues)
{
  int32 icrdiff ;
  int32 indicesHash ;
  uint32 indicesId ;
  int32 index ;
  int32 value ;
  int32 *indices ;
  int32 *fractns ;

  HQASSERT( colorTable , "colorTable NULL");
  HQASSERT( piColorValues , "piColorValues NULL");

  indices = colorTable->indices ;
  fractns = colorTable->fractns ;

  value = piColorValues[0] ;
  index = value >> GSC_FRACNSHIFT ;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
  icrdiff = ( indices[0] - index ) ;
  indicesId = index;
  indicesHash = index & colorTable->cornerPtrsCacheMask;
  indices[0] = index ;
  fractns[0] = value & GSC_FRACNMASK ;

  value = piColorValues[1] ;
  index = value >> GSC_FRACNSHIFT ;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
  icrdiff |= ( indices[1] - index ) ;
  indicesId <<= colorTable->cubeSideBits;
  indicesId += index;
  indicesHash <<= colorTable->cornerPtrsCacheBits;
  indicesHash += index & colorTable->cornerPtrsCacheMask;
  indices[1] = index ;
  fractns[1] = value & GSC_FRACNMASK ;

  value = piColorValues[2] ;
  index = value >> GSC_FRACNSHIFT ;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
  icrdiff |= ( indices[2] - index ) ;
  indicesId <<= colorTable->cubeSideBits;
  indicesId += index;
  indicesHash <<= colorTable->cornerPtrsCacheBits;
  indicesHash += index & colorTable->cornerPtrsCacheMask;
  indices[2] = index ;
  fractns[2] = value & GSC_FRACNMASK ;

  value = piColorValues[3] ;
  index = value >> GSC_FRACNSHIFT ;
  HQASSERT( index >= 0 && index < colorTable->cubeSide , "index out of range" ) ;
  icrdiff |= ( indices[3] - index ) ;
  indicesId <<= colorTable->cubeSideBits;
  indicesId += index;
  indicesHash <<= colorTable->cornerPtrsCacheBits;
  indicesHash += index & colorTable->cornerPtrsCacheMask;
  indices[3] = index ;
  fractns[3] = value & GSC_FRACNMASK ;

  HQASSERT(colorTable->cubeSideBits * colorTable->incomps <= INDICES_HASH_BITS,
           "Too many components to use pointer cache");
  colorTable->indicesId = indicesId ;
  colorTable->indicesHash = indicesHash ;

  HQASSERT((colorTable->indicesId & INDICES_ID_INVALID) == 0,
           "The invalid cornerPtrs id bit shouldn't be set in normal use");

  HQASSERT((int32) colorTable->indicesHash <= colorTable->cornerPtrsCacheSize - 1,
           "indicesHash is out of scope");

#ifdef ASSERT_BUILD
  if (icrdiff != 0) {
    int32 incomps ;
    for (incomps = 0; incomps < colorTable->incomps; incomps++) {
      colorTable->incIndices[incomps] = -1;
      colorTable->tmpIndices[incomps] = -1;
      colorTable->tmpIncIndices[incomps] = -1;
    }
  }
  if (colorTable->cornerPtrsCacheBits == 0)
    colorTable->indicesId = INDICES_ID_INVALID;
#endif

  return ( icrdiff == 0 );
}

/**
 * Some utilities to let the rest of the world know things that depend on the
 * size of the table.
 */
static int32 maxTableIndex(COLOR_PAGE_PARAMS *colorPageParams)
{
  return colorPageParams->fasterColorGridPoints - 1;
}

/**
 * The image code will scale numbers to this value to give us a nice fractional
 * part for accurate interpolation.
 */
int32 gsc_scaledColor(DL_STATE *page)
{
  return SCALED_COLOR( maxTableIndex(&page->colorPageParams) );
}


#ifdef DEBUG_BUILD
/* Some functions to use in the debugger to allow a basic
 * printout of the colors in a color cube. */

/**
 * Print out the given color value
 */
static void dump_color(struct colorNd *cv, int32 ncolors)
{
  int32 j;

  if ( cv == NULL )
    monitorf(( uint8 * )"--");
  else {
    for ( j = 0; j < ncolors; j++ ) {
      if ( j != 0 )
        monitorf(( uint8 * )"/");
      monitorf(( uint8 * )"%x", (int32)cv->color[j]);
    }
  }
}

/**
 * Recursive function to print out the contents of the given color cube
 */
static void dump_cube(colorTableNd *ct, colorCubeNd *cube, int32 dim)
{
  int32 j;

  monitorf(( uint8 * )"dim %d\n", dim);
  if ( dim == SOLID_DIMS ) { /* switch over to 1d array */
    if ( cube->c1 == NULL )
      monitorf(( uint8 * )"NULL\n");
    else {
      struct colorCube1d *c1 = cube->c1;

      for ( j=0; j < ct->cubeSide ; j++ ) {
        if ( j != 0 )
          monitorf(( uint8 * )",");
        if ( c1->valid[j] & GST_COLOR_PRESENT )
          dump_color(c1->color[j], ct->oncomps);
        else
          monitorf(( uint8 * )"?");
      }
      monitorf(( uint8 * )"\n");
    }
  } else {
    if ( cube->cn == NULL )
      monitorf(( uint8 * )"NULL\n");
    else {
      for ( j=0; j < ct->cubeSide ; j++ )
        dump_cube(ct, &(cube->cn[j]), dim-1);
    }
  }
}

/**
 * Debug function to print the contents of the specified color table.
 * Its a bit clunky at the moment, but useful in the debugger to see what a
 * Toms table looks like.
 */
void dump_colortable(colorTableNd *ct)
{
  monitorf(( uint8 * )"TomsTables cube of side %d\n", ct->cubeSide);

  if ( ct->incomps == 3 ) {
    int32 x, y, z;
    colorCubeNd *cube0 = &(ct->cube), *cube1, *cube2;
    struct colorCube1d *c1;

    if ( cube0 == NULL ) {
      monitorf(( uint8 * )"cube0 NULL\n");
    } else {
      for ( x = 0; x < ct->cubeSide; x++ ) {
        cube1 = &(cube0->cn[x]);
        if ( cube1== NULL ) {
          monitorf(( uint8 * )"cube1 NULL\n");
        } else {
          for ( y = 0; y < ct->cubeSide; y++ ) {
            cube2 = &(cube1->cn[y]);
            if ( cube2 == NULL ) {
              monitorf(( uint8 * )"cube2 NULL\n");
            } else {
              monitorf(( uint8 * )"(%d,%d) : ", x, y);
              c1 = cube2->c1;
              if ( c1 == NULL ) {
                monitorf(( uint8 * )"NULL\n");
              } else {
                for ( z = 0; z < ct->cubeSide; z++ ) {
                  if ( z != 0 )
                    monitorf(( uint8 * )",");
                  if ( c1->valid[z] & GST_COLOR_PRESENT )
                    dump_color(c1->color[z], ct->oncomps);
                  else
                    monitorf(( uint8 * )"?");
                }
                monitorf(( uint8 * )"\n");
              }
            }
          }
        }
      }
    }
  }
  else
    dump_cube(ct, &(ct->cube), ct->incomps);
}

#endif /* DEBUG_BUILD */


Bool gst_swstart(void)
{
  multi_mutex_init(&gst_mutex, GST_LOCK_INDEX, FALSE,
                   SW_TRACE_GST_ACQUIRE, SW_TRACE_GST_HOLD);

  return TRUE;
}


void gst_finish(void)
{
  multi_mutex_finish(&gst_mutex);
}


/* Log stripped */
