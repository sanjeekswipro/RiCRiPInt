/** \file
 * \ingroup rleblit
 *
 * $HopeName: CORErender!src:rleblt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for RLE blitting.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "rleblt.h"
#include "objnamer.h"
#include "interrupts.h"
#include "namedef_.h"
#include "ripdebug.h"

#include "swoften.h"
#include "often.h"
#include "swerrors.h" /* error_handler */
#include "swrle.h"
#include "swcmm.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "hqbitops.h"
#include "hqbitvector.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "surface.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "display.h"
#include "pixelLabels.h" /* SW_PGB_INVALID_OBJECT */
#include "dlstate.h"
#include "gu_chan.h"  /* gucr_valuesPerComponent */
#include "render.h"
#include "devops.h"
#include "toneblt.h"   /* bitclipn */
#include "control.h"   /* interrupts_clear */
#include "cce.h"
#include "group.h"
#include "rleColorantMapping.h" /* RleColorantMap et. al. */
#include "pclPatternBlit.h"
#include "backdrop.h" /* bd_readSoftmaskLine */
#include "imgblts.h"
#include "builtin.h"
#include "taskres.h" /* TASK_RESOURCE_RLE_STATES */
#include "htrender.h" /* ht_screen_index */
#include "surfacecb.h" /* sheet_rs_handle */
#include "bandtable.h" /* band_t */
#include "imageo.h" /* IMAGEOBJECT */


static mm_pool_t mm_pool_rle ;


/** RUN_REPEAT repeat count disposition bits. */
enum {
  REPEAT_COUNT_SAME = 0,    /**< Same as previous repeat count. */
  REPEAT_COUNT_GREATER = 1, /**< One more than previous repeat count. */
  REPEAT_COUNT_NEW = 2,     /**< Not related to previous repeat count. */
  REPEAT_COUNT_LESS = 3     /**< One less than previous repeat count. */
} ;

/** The details that we save to determine if the colorants are the same. */
typedef struct {
  /** Blit colormaps are created with reference to a rasterstyle. */
  uint32 rasterstyle_id;
  /* The blit colormap pointer may be transitory (it could be on the stack), so
     can't be trusted, but the rasterstyle ids are unique. */

  /** The number of possible channels in the colorant set. */
  channel_index_t nchannels ;

  /** The number of colors actually present in the colorant set. */
  channel_index_t ncolors ;

  /** The number of maxblit colors actually present in the colorant set. */
  channel_index_t nmaxblits ;

  /** States of the channels present in the colorant set. */
  blit_channel_state_t channels[BLIT_MAX_CHANNELS] ;
} colorants_state_t ;

/** A mask cache entry. Softmasks can be rendered in stages when memory is
tight (i.e. a backdrop block at a time), therefore a particular set of softmask
data requires an x-extent to be uniquely identifyable. */
typedef struct {
  SoftMaskAttrib* mask;
  dbbox_t area;
} RleSoftmaskCacheEntry;

/* The number of entries in the soft mask cache. */
#define RLE_SMASK_CACHE_SIZE 7

/** Per-line RLE state. */
typedef struct RLESTATE {
  uint32    * blockstart;
  uint32    * blockheader;
  uint32    * blockend;
  uint32    * firstblock;
  uint32    * currentword;
  dcoord      currentposition;        /* maximum 24 bits usable */
  int32       screenid;               /* screen currently active on this line */
  colorants_state_t colorants;
  LISTOBJECT *last_object ;

  /** Bitstream setup for output to currentword. */
  struct {
    /** Number of low bits reserved in current word when writing bitstream. */
    uint8 reserved ;

    /** Remaining bits (32..0) left in current word when writing bitstream. */
    uint8 remaining ;

    /** Size of the bitstream allocated. */
    size_t total ;
  } bits ;

  /** RUN_REPEAT/STORE/SOFTMASK optimiser. */
  struct {
    /** Start of current repeat (if any). */
    uint32 *header ;

    /** Space used by RUN_REPEAT_STOREs on this line. */
    size_t cache_mem_hwm ;

    /** Size (in bits) of the current RUN_REPEAT_STORE output when we should
        stop and start a new store. This is the cache size available adjusted
        for the worst-case run output size. */
    size_t recache_limit ;

    /** Number of RUN_REPEAT_COPY lines following this. */
    int32 copies ;

    /** The next RUN_REPEAT_STORE id to be used on this line. */
    uint16 cache_id_hwm ;

    /** The current number of spans produced for RUN_REPEAT, RUN_REPEAT_STORE,
        or RUN_SOFTMASK. */
    uint16 spanCount;

    /** The length of the last span put into a RUN_REPEAT/STORE/SOFTMASK */
    uint8 lastSpan ;
  } repeat ;

  /** The current pixel type label. */
  uint8 current_pixel_label;

  /** The current rendering intent. */
  uint8 current_rendering_intent;

  /** A bitfield marking which RUN_INFO tags are currently non-null in the
      line. */
  uint16 run_info_tags_present ;

  /** Transparency state. */
  struct {
    /** The current mask ID. If there is no current mask, this will be -1,
        otherwise this refers to an entry in the cache. When a new soft mask is
        set, it is first looked up in the mask cache; if it's not present then
        it must be output to the RLE and addred to the cache. */
    int16 maskId;

    /** The current alpha. */
    uint16 alpha;
    /** The current alphaIsShape */
    uint8 alphaIsShape;
    /** The current blend mode. */
    uint8 blendMode;

    /** This cache is used to track the soft masks output so far on this
        line. */
    RleSoftmaskCacheEntry maskCache[RLE_SMASK_CACHE_SIZE];
  } trans ;
} RLESTATE ;

/* Return the rle state for the passed line. */
#define GET_RLE_STATE(rb_, y_) \
  ((rb_)->p_ri->p_rs->surface_handle.band->rlestates \
   + ((y_) - (rb_)->p_ri->p_rs->forms->retainedform.hoff - (rb_)->y_sep_position))

/** RLE parameters are internalised in a surface instance, so they can be
    handled correctly for pipelined pages. */
struct surface_instance_t {
  /** Reference count is used to determine how many pages share this set of RLE
      parameters. */
  hq_atomic_counter_t refcount ;

  /* RUN_REPEAT_STORE cache information. These parameters allow the consumer
     to specify the nature of the caches they will use to store
     RUN_REPEAT_STORE records, allow the rip to decide when
     RUN_REPEAT_STOREs can be used. */

  /** The number of caches in the consumer used to store RUN_REPEAT_STORE
      records. */
  size_t runCacheNumber ;

  /** The size of each cache in the consumer used to store RUN_REPEAT_STORE
      records. */
  size_t runCacheSize ;

  /** Do the RUN_REPEAT_STORE caches in the consumer occupy a contiguous
      block of memory? If true, RUN_REPEAT_STORE records with content larger
      than a single cache will occupy multiple consecutive caches. */
  Bool runCacheConsecutive ;

  /** Do we omit RUN_OBJECT_TYPE records? */
  Bool runObjectType ;

  /** Do we omit RUN_GROUP_OPEN/RUN_GROUP_CLOSE records? */
  Bool runTransparency ;

  /** When emitting RUN_GROUP_OPEN/RUN_GROUP_CLOSE records, do we suppress
      compositing? */
  Bool runNoComposite ;

  /** Do we output a single block header to the PGB device? If FALSE, we
      write each block separately to the PGB device. */
  Bool runLineOutput ;

  /** Do we output RUN_PARTIAL_OVERPRINT records for maxblits? */
  Bool runPartialOverprint ;

  OBJECT_NAME_MEMBER
} ;

#define RLE_INSTANCE_NAME "RLE surface instance"

/** Separation data is preserved across all sheet instances that are used to
    render a single separation. The separations data includes a lists of all
    colorants used in each sheet need to be preserved over partial paint, and
    the summary highwatermarks for the run repeat stores. We attach the
    separation data to the page instance, and destroy it at the end of the
    page. Optimally, we'd store the highwatermark for the run repeat store
    data of each line separately, to determine the maximum number and size of
    caches required for each line. In practice, that's a bit much, so we
    store the maximums for each separation. */
typedef struct rle_separation_t {
  int32 sheet_id ;                /**< Sheet identifier for this separation. */
  struct rle_separation_t *next ; /**< Next separation's data. */
  /** Colorants on this sheet. The colorant list contains pointers into the
      display list's rasterstyle. We need the colorant numbering to be the
      same after a partial paint as before. */
  ColorantList *colorants ;
  /** Run repeat store cache space used by previous paint. This uses an atomic
      for easy assignment from \a next_repeat_mem_hwm. */
  hq_atomic_counter_t prev_repeat_mem_hwm ;
  /** Run repeat store cache space used in next paint. This uses an atomic
      because it may be updated by multiple bands at the same time. */
  hq_atomic_counter_t next_repeat_mem_hwm ;
  /** Last run repeat store ID used in previous paint. This uses an atomic
      for easy assignment from \a next_repeat_id_hwm. */
  hq_atomic_counter_t prev_repeat_id_hwm ;
  /** First run repeat store ID available in next paint. This uses an atomic
      because it may be updated by multiple bands at the same time. */
  hq_atomic_counter_t next_repeat_id_hwm ;
  /** There can be up to 16 separate sets of info, identified by the
      RUN_INFO record's type field, and each has a separate sequence of
      re-use identifiers. For any one type, the identifiers are assigned
      starting from 65535 downwards (limited by the length of the identifer
      field). A new id is allocated each time rleNewObject() is called;
      because we tend to render many lines of an object successively (i.e.
      for a single call to rleNewObject()), reuse should be fairly good.
      If we run out of identifiers, we reserve 0 to be assigned explicitly
      each time, and never re-used. */
  struct {
    /** First run info ID used in previous paint. This uses an atomic for
        easy assignment from \a next. */
    hq_atomic_counter_t prev ;
    /** First run info ID available in next paint. This uses an atomic
        because it may be updated by multiple bands at the same time. */
    hq_atomic_counter_t next ;
  } run_info_ids[1 << RLE_LEN_INFO_TYPE] ;
} rle_separation_t ;

struct surface_page_t {
  /** Surface parameters for this page (and other shared pages). */
  surface_instance_t *params ;

  /** The separation data which can and should be preserved across partial
      paints. */
  rle_separation_t *sepdata ;

  OBJECT_NAME_MEMBER
} ;

#define RLE_PAGE_NAME "RLE surface page instance"

struct surface_sheet_t {
  surface_page_t *rlepage ;   /**< The page instance we're rendering. */
  RleColorantMap *map ;       /**< RLE colorant map for this sheet. */
  rle_separation_t *sepdata ; /**< Separation data for this sheet preserved
                                   across partial paints. This is just a
                                   convenience reference, the reference
                                   from the page data's separation list is
                                   used for lifecycle management. */
  resource_pool_t *band_pool ; /**< The resource pool for the output band. */

  OBJECT_NAME_MEMBER
} ;

#define RLE_SHEET_NAME "RLE surface sheet instance"

#ifdef DEBUG_BUILD
/** Bit mask of RLE debugging options */
enum {
  DEBUG_RLE_NO_OVERFLOW = 1,    /* No RLE band overflow. */
  DEBUG_RLE_COMPLETE_LINES = 2, /* No RLE incomplete lines. */
  DEBUG_RLE_FORCE_SPLIT = 4,    /* Force split. */
  DEBUG_RLE_NO_EXTEND = 8       /* No band memory extensions. */
} ;

static int32 debug_rle = 0 ;
#endif

#define RLE_GET_ALPHA(_alpha) \
  ((uint16)(((_alpha) * RLE_ALPHA_MAX) / COLORVALUE_MAX))

typedef struct {
  RleColorantMap* map;
  uint16 alpha;
  uint8 alphaIsShape;
  uint8 blendMode;
  RleSoftmaskCacheEntry mask;
  TranAttrib *tranAttrib ;
} RleTransparencyState;

/** RLE tracking state. */
struct surface_band_t {
  /** Uncounted reference to surface instance, containing the RLE parameters.
      We don't reference count this because the surface_band_t scope is
      guaranteed to be shorter than the surface_page_t scope and hence
      surface_instance_t scope. */
  const surface_instance_t *params ;

  /** Temporarily disable RLE record output if non-zero; this is used when
      compositing softmasks. */
  uint32 disabled ;

  /** Is the RIP required to composite the current group (softmask or
      RUN_NO_COMPOSITE is false)? */
  Bool compositing;

  /** The number of output bits. */
  uint32 bitDepth;

  /** The shift to apply to color values when producing monochrome RUN_SIMPLE
   * records. */
  uint32 monoSimpleShift;

  /** Current state of RLE generation. When RLE block memory runs out, we set
      the state to anything except RLE_GEN_OK. Public functions that generate
      RLE can return immediately in this case, and we filter out the
      remainder of the DL objects. */
  enum {
    RLE_GEN_ERROR,  /**< Unrecoverable error in generation. */
    RLE_GEN_RETRY,  /**< Retriable error in generation. */
    RLE_GEN_OK      /**< No error in generation. */
  } runAbort ;

  /** Count of the number of RUN_REPEAT records being constructed; we cannot
     purge completed blocks when this is true as the RUN_REPEAT header is not
     complete until all runs in the record have been written. */
  uint32 constructingRunRepeat ;

  /** Are we doing color RLE? If so, we need colorants. */
  Bool color_rle ;

  /** Current colorant for mono RLE. */
  COLORANTINDEX ci;

  /** The state of the RUN_REPEAT optimiser. */
  enum {
    NO_RUN_REPEAT,         /**< Don't generate RUN_REPEAT. */
    DO_RUN_REPEAT,         /**< Generate RUN_REPEAT. */
    DO_RUN_REPEAT_IF_EASY, /**< Generate RUN_REPEAT if short spans. */
    DO_RUN_STORE_OR_REPEAT /**< Generate RUN_REPEAT_STORE if caches available. */
  } run_repeat_state ;

  /** Total run repeat cache store size */
  size_t repeat_cache_total ;

  /** Maximum RUN_REPEAT span length. */
  dcoord max_run_repeat ;

  /** Maximum RUN_SIMPLE span length. */
  dcoord max_run_simple ;

  uint8 *next_free_block, *block_limit;

  /** The band height used to start this render. */
  int32 band_rh ;

  /** The current transparency state, including the current mapping from DL
      colorant indices to the colorants listed in the PGB. */
  RleTransparencyState transparencyState;

  /** \todo - This is a hack.
      This value is used as an offset to outputform->hOff in group open/close
      functions, to work around outputform->hOff being set to zero by pattern
      rendering. */
  int32 realHOff;

  /** The rlestates array. */
  RLESTATE * rlestates ;

  /** This structure maintain the state for producing RUN_INFO records. This is
      use to determine when to re-use RUN_INFOs to save space, and when they are
      required in the first place.

      A RUN_INFO record contains data associated with a particular graphic
      object, established by the setgstatetagdict and setgstatetag
      operators. */
  struct {

    /** current_listobject is set to point to display list objects as each is
       encountered by the renderer, when that object has some info to emit.
       Set to NULL when there is no info. */
    LISTOBJECT *current_listobject ;

    /** There can be up to 16 separate sets of info, identified by the
       RUN_INFO record's type field, and each has a separate sequence of
       re-use identifiers. For any one type, the identifiers are assigned
       starting from 65535 downwards (limited by the length of the identifer
       field). A new id is allocated each time rleNewObject() is called;
       because we tend to render many lines of an object successively (i.e.
       for a single call to rleNewObject()), reuse should be fairly good.

       If we run out of identifiers, we reserve 0 to be assigned explicitly
       each time, and never re-used. 'identifiers', therefore, keeps track of
       the most recently assigned identifier for each type. Note that this
       mechanism means that (a) we may assign a different identifier for the
       same object (and therefore the same data) on a subsequent band or
       backdrop region, and (b) we don't re-use the same identifier when a
       different object but with the same data is encountered.

       These really should be in the RLESTATE, but 16 identifiers for every
       line is a bit much, so we share them across the whole band. */
    uint32 identifiers[(1 << RLE_LEN_INFO_TYPE)] ;

    /** When a new object is encountered (the renderer calls rleNewObject())
       we could assign its identifier there and then. However, it may be that
       we never actually render the object, for whatever reason, so to
       conserve identifiers, we only allocate it lazily when the first span
       of the object is output. This flag keeps track of when this has been
       done. */
    Bool current_listobject_needs_identifiers ;

    /** This refers to the info description (hanging off the object's state),
        which among other things says how big the data associated with each
        tag type is. It could be accessed via current_listobject, but we
        record it separately for efficiency. Set to NULL if there is no info
        associated with the current object. */
    GSTAGSTRUCTUREOBJECT * current_gts ;

    /** Likewise, the actual data associated with the object (which is
        actually stored in contiguous memory immediately preceding the
        object), could be accessed via current_listobject, but is referenced
        separately for efficiency. Set to NULL if no info is associated with
        the current object */
    uint32 *current_data ;

    /** current_tags records in a bit array which info types have been
        allocated identifiers from the identifiers array. This is used to
        accumulate the info types used for an object, and recorded for each
        scanline in the state, so that if a type is no longer present in a
        subsequent object, it can be cancelled with a 0 RUN_INFO record */
    uint32 current_tags ;

    /** In order to reuse the data assigned a run info identifier, we must
        have issued a non-re-used run info earlier on the same scanline or a
        previous scanline. However, not all objects are rendered top to
        bottom - the tiles of a rotated image for example. So we work round
        this be emitting a non-re-use info each time we render into an
        earlier scanline. Most of the time, that will be the only one, since
        we don't often go higher up the band. This variable keeps track of
        the earliest scanline in the band encountered so far for the current
        object, so that when we encounter an even earlier one we can output
        the data again. */
    RLESTATE * earliest_scanline_data_emitted_for_so_far ;

    /** Of course, we need to set earliest_scanline_data_emitted_for_so_far
        initially so that the first time a span for an object is encountered,
        it must be the earliest so far and therefore doesn't re-use. So on
        each new band we store a reference to that to initialise
        earliest_scanline_data_emitted_for_so_far for each new object */
    RLESTATE * scanline_after_end_of_band;
  } run_info;

  band_t *pband ; /**< The output band resource that we're writing to. */
  surface_sheet_t *sheet ; /**< The RLE sheet structure. */

  OBJECT_NAME_MEMBER
} ;

#define RLE_TRACKER_NAME "RLE tracker"

static Bool addPositionIfNeeded(surface_band_t *tracker, RLESTATE *state,
                                dcoord position) ;
static Bool addColorantsIfNeeded(surface_band_t *tracker, RLESTATE *state,
                                 const blit_color_t *color) ;

static uint8 cceBlendModeToRle(uint8 cceMode);

#ifdef BLIT_RLE_TRANSPARENCY
/** The compositing surface for transparent RLE. After sending the spans out
    to RLE, transparent RLE functions forward spans to this surface. */
static const transparency_surface_t *rlecolor_backdrop ;
#endif

/* Returns the number of words available in the current block.
*/
#define RLE_BLOCK_AVAILABLE(_state) \
  CAST_PTRDIFFT_TO_INT32((_state)->blockend - ((_state)->currentword + 1))

/* Returns true if there are less than 'required' words in the current block.
*/
#define RLE_BLOCK_OVERFLOW(_state, _required) \
  (RLE_BLOCK_AVAILABLE(_state) < (_required))

/* Check that the current block has not overflowed. */
#define RLE_OVERFLOW_CHECK(_state) MACRO_START \
  HQASSERT((_state)->currentword < (_state)->blockend, \
           "RLE_OVERFLOW_CHECK - block overflow."); \
MACRO_END

/* Write a word to the RLE stream. */
#define RLE_WRITE(_state, _word) MACRO_START \
  (_state)->currentword ++; \
  RLE_OVERFLOW_CHECK(_state); \
  *(_state)->currentword = (_word); \
MACRO_END

#ifdef ASSERT_BUILD
static int32 rle_inst_allocated ;
static int32 rle_page_allocated ;
static int32 rle_sheet_allocated ;

/** Check that the linked list of blocks for the passed state has not been
corrupted.
*/
static void check_block_consistency(RLESTATE* state)
{
  /* The first word of each block (starting with state->firstblock) points to
  the next block in the list. */
  if (state->firstblock != NULL) {
    uint32* nextBlock = RLEBLOCK_GET_NEXT(state->firstblock);
    while (nextBlock != NULL) {
      /* Try to write to the next block. If the pointer is bad, this should
      cause an access violation (note that it's often ok to read from bad
      memory). Obviously this isn't a perfect test - sometimes it may be
      possible to write to a bad pointer, but it's better than nothing. */
      uint32 temp = nextBlock[0];
      nextBlock[0] = 0;
      nextBlock[0] = temp;

      nextBlock = RLEBLOCK_GET_NEXT(nextBlock);
    }
  }
}

/** Check the block list for each line has not been corrupted.
*/
static void check_band_consistency(surface_band_t *tracker)
{
  int32 totalLines = tracker->band_rh;
  RLESTATE *lineState = tracker->rlestates;

  while ( totalLines-- > 0 ) {
    check_block_consistency(lineState++);
  }
}
#endif /* ASSERT_BUILD */

/** Get the first data block pointer. */
uint32 *rle_block_first(dcoord y, dcoord y1)
{
  Bool hit ;
  RLESTATE *state = task_resource_fix(TASK_RESOURCE_RLE_STATES, y1, &hit) ;

  HQASSERT(state != NULL && hit, "RLE states not previously prepared") ;
  HQASSERT(y >= y1, "RLE line offset out of range") ;
  /* Find the rle state for the passed line. */
  state += y - y1 ;
  return state->firstblock ;
}

/* ========================================================================== */
/* RLESTATE resources */

static resource_source_t rle_resource ;

static Bool rle_resource_alloc(resource_pool_t * pool, resource_entry_t * entry,
                               mm_cost_t cost)
{
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  entry->resource = mm_alloc_cost(pool->source->data, pool->key, cost,
                                  MM_ALLOC_CLASS_RLESTATE) ;

  return (entry->resource != NULL) ;
}

static void rle_resource_free(resource_pool_t * pool, resource_entry_t * entry)
{
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  mm_free(pool->source->data, entry->resource, pool->key) ;
}

static size_t rle_resource_size(resource_pool_t * pool,
                                const resource_entry_t * entry)
{
  UNUSED_PARAM(const resource_entry_t *, entry) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  return (size_t) pool->key ;
}

static Bool rle_resource_make(resource_source_t *source, resource_pool_t *pool)
{
  UNUSED_PARAM(resource_source_t *, source) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  pool->alloc      = rle_resource_alloc ;
  pool->free       = rle_resource_free ;
  pool->entry_size = rle_resource_size ;

  return TRUE ;
}

static mm_pool_t rle_resource_pool(const resource_source_t * source)
{
  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME) ;

  return source->data ;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Set memory requirement for RLE states for each line of the retainedform. */
static Bool rle_states_memory(DL_STATE *page, requirement_node_t *bandvals)
{
  resource_requirement_t *req ;
  resource_pool_t *pool ;

  HQASSERT(page != NULL, "No page to set up RLE state for") ;
  HQASSERT(IS_INTERPRETER(), "Setting up RLE resource pools too late") ;
  req = page->render_resources ;
  HQASSERT(req != NULL, "No resource requirement to update RLE pool") ;
  HQASSERT(!resource_requirement_is_ready(req),
           "Resource requirement cannot be updated when ready") ;

  pool = resource_pool_get(&rle_resource,
                           TASK_RESOURCE_RLE_STATES,
                           page->band_lines * sizeof(RLESTATE)) ;
  if (pool == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_RLE_STATES, pool))
    return FALSE ;

  return requirement_node_setmin(bandvals, TASK_RESOURCE_RLE_STATES, 1) ;
  /* This is then fetched ("fixed") in rle_band_render() */
}

/* ---------------------------------------------------------------------- */
/* Surface lifecycle management */
static Bool rle_select(surface_instance_t **instance, const sw_datum *pagedict,
                       const sw_data_api *dataapi, Bool continued)
{
  surface_instance_t inst = { 0 } ;
  sw_data_result result ;
  DL_STATE *page ;

  /** \todo ajcd 2012-11-08: For compatibility, some of the RLE parameters
      are settable from the top-level page device dictionary. This should
      be deprecated, and all parameters should be set from the RunLengthDetails
      dictionary. */
  enum {
    m_RunLengthDetails,
    m_RunOverlap,
    m_detail_start, /* Start of detail and pagedevice params */
    m_RunCacheNumber = m_detail_start,
    m_RunCacheSize,
    m_RunCacheConsecutive,
    m_RunPartialOverprint,
    m_pd_end, /* End of pagedevice-only params */
    m_RunObjectType = m_pd_end,
    m_RunTransparency,
    m_RunNoComposite,
    m_RunLineOutput,
    m_detail_end /* End of detail-only params */
  } ;
  static sw_data_match match[m_detail_end] = {
    /* These used in top-level match only: */
    {SW_DATUM_BIT_DICT, SW_DATUM_STRING("RunLengthDetails")},
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING, SW_DATUM_STRING("RunOverlap")},
    /* These used in details match and top-level match for compatibility: */
    {SW_DATUM_BIT_INTEGER|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunCacheNumber")},
    {SW_DATUM_BIT_INTEGER|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunCacheSize")},
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunCacheConsecutive")},
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunPartialOverprint")},
    /* These used only in details match: */
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunObjectType")},
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunTransparency")},
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunNoComposite")},
    {SW_DATUM_BIT_BOOLEAN|SW_DATUM_BIT_NOTHING,
     SW_DATUM_STRING("RunLineOutput")},
  } ;

  UNUSED_PARAM(Bool, continued) ;

  HQASSERT(instance != NULL, "Nowhere to put surface instance") ;
  HQASSERT(*instance == NULL, "Surface instance already exists") ;
  HQASSERT(IS_INTERPRETER(), "Selecting surface from non-interpreter task") ;

  /* Non-zero/false defaults for RLE parameters. Default to RunLineOutput,
     since both the GUI and LE output mechanisms both need it set by default.
     It's possible that an LE customer could re-write the PGB device and
     raster handler to set it false, but they probably won't. */
  inst.refcount = 1 ;
  inst.runLineOutput = TRUE ;

  if ( (result = dataapi->match(pagedict, match, m_pd_end)) == SW_DATA_OK ) {
    if ( match[m_RunOverlap].value.type == SW_DATUM_TYPE_BOOLEAN &&
         !match[m_RunOverlap].value.value.boolean )
      return detail_error_handler(CONFIGURATIONERROR,
                                  "RunOverlap must be true for RunLength output.");
    if ( match[m_RunCacheNumber].value.type == SW_DATUM_TYPE_INTEGER ) {
      if ( match[m_RunCacheNumber].value.value.integer < 0 )
        return detail_error_handler(CONFIGURATIONERROR,
                                    "RunCacheNumber must not be negative.") ;
      inst.runCacheNumber = match[m_RunCacheNumber].value.value.integer ;
    }
    if ( match[m_RunCacheSize].value.type == SW_DATUM_TYPE_INTEGER ) {
      if ( match[m_RunCacheSize].value.value.integer < 0 )
        return detail_error_handler(CONFIGURATIONERROR,
                                    "RunCacheSize must not be negative.") ;
      inst.runCacheSize = match[m_RunCacheSize].value.value.integer ;
    }
    if ( match[m_RunCacheConsecutive].value.type == SW_DATUM_TYPE_BOOLEAN )
      inst.runCacheConsecutive = match[m_RunCacheConsecutive].value.value.boolean ;
    if ( match[m_RunPartialOverprint].value.type == SW_DATUM_TYPE_BOOLEAN )
      inst.runPartialOverprint = match[m_RunPartialOverprint].value.value.boolean ;

    /* Check RunLengthDetails sub-dictionary for parameters. */
    if ( (result = dataapi->match(&match[m_RunLengthDetails].value,
                                  &match[m_detail_start],
                                  m_detail_end-m_detail_start)) == SW_DATA_OK ) {
      if ( match[m_RunCacheNumber].value.type == SW_DATUM_TYPE_INTEGER ) {
        if ( match[m_RunCacheNumber].value.value.integer < 0 )
          return detail_error_handler(CONFIGURATIONERROR,
                                      "RunCacheNumber must not be negative.") ;
        inst.runCacheNumber = match[m_RunCacheNumber].value.value.integer ;
      }
      if ( match[m_RunCacheSize].value.type == SW_DATUM_TYPE_INTEGER ) {
        if ( match[m_RunCacheSize].value.value.integer < 0 )
          return detail_error_handler(CONFIGURATIONERROR,
                                      "RunCacheSize must not be negative.") ;
        inst.runCacheSize = match[m_RunCacheSize].value.value.integer ;
      }
      if ( match[m_RunCacheConsecutive].value.type == SW_DATUM_TYPE_BOOLEAN )
        inst.runCacheConsecutive = match[m_RunCacheConsecutive].value.value.boolean ;
      if ( match[m_RunPartialOverprint].value.type == SW_DATUM_TYPE_BOOLEAN )
        inst.runPartialOverprint = match[m_RunPartialOverprint].value.value.boolean ;
      if ( match[m_RunObjectType].value.type == SW_DATUM_TYPE_BOOLEAN )
        inst.runObjectType = match[m_RunObjectType].value.value.boolean ;
      if ( match[m_RunTransparency].value.type == SW_DATUM_TYPE_BOOLEAN )
        inst.runTransparency = match[m_RunTransparency].value.value.boolean ;
      if ( match[m_RunNoComposite].value.type == SW_DATUM_TYPE_BOOLEAN )
        inst.runNoComposite = match[m_RunNoComposite].value.value.boolean ;
      if ( match[m_RunLineOutput].value.type == SW_DATUM_TYPE_BOOLEAN )
        inst.runLineOutput = match[m_RunLineOutput].value.value.boolean ;
    }
  }
  /** \todo ajcd 2012-11-08: We could just use error_from_sw_data_result()
      for this, but I don't really want to make the datum compound internals
      visible to CORErender. */
  switch ( result ) {
  case SW_DATA_OK:
    break ;
  case SW_DATA_ERROR_UNDEFINED:
    return error_handler(UNDEFINED) ;
  case SW_DATA_ERROR_TYPECHECK:
    return error_handler(TYPECHECK) ;
  case SW_DATA_ERROR_INVALIDACCESS:
    return error_handler(INVALIDACCESS) ;
  default:
    HQFAIL("Unexpected return type from data API") ;
    return error_handler(UNREGISTERED) ;
  }

  page = CoreContext.page ;
  if ( inst.runTransparency
       && gucr_interleavingStyle(page->hr) == GUCR_INTERLEAVINGSTYLE_MONO )
    return detail_error_handler(CONFIGURATIONERROR,
                                "Transparent RLE only works in composite.");

  if ( (*instance = mm_alloc(mm_pool_rle, sizeof(inst),
                             MM_ALLOC_CLASS_RLE_SURFACE_INSTANCE)) == NULL )
    return error_handler(VMERROR) ;
#ifdef ASSERT_BUILD
  ++rle_inst_allocated ;
#endif

  /** \todo ajcd 2012-11-08: We'd prefer to have these in the introduce_dl call,
      but, but unfortunately several other calls in between retiring the old
      DL and introducing a new DL rely on it. */
  page->rle_flags = RLE_GENERATING ;
  if ( inst.runLineOutput )
    page->rle_flags |= RLE_LINE_OUTPUT ;
  if ( inst.runTransparency ) {
    page->rle_flags |= RLE_TRANSPARENCY;
    if ( inst.runNoComposite )
      page->rle_flags |= RLE_NO_COMPOSITE;
  }
  if ( inst.runObjectType )
    page->rle_flags |= RLE_OBJECT_TYPE;

  NAME_OBJECT(&inst, RLE_INSTANCE_NAME) ;
  **instance = inst ;

  return TRUE ;
}

static void rle_instance_release(surface_instance_t **instance)
{
  hq_atomic_counter_t after ;

  HQASSERT(instance != NULL, "No where to find RLE surface instance") ;
  VERIFY_OBJECT(*instance, RLE_INSTANCE_NAME) ;

  HqAtomicDecrement(&(*instance)->refcount, after) ;
  HQASSERT(after >= 0, "Page data already released") ;
  if ( after == 0 ) {
    UNNAME_OBJECT(*instance) ;
#ifdef ASSERT_BUILD
    --rle_inst_allocated ;
#endif
    mm_free(mm_pool_rle, *instance, sizeof(**instance)) ;
  }

  *instance = NULL ;
}

static void rle_deselect(surface_instance_t **instance, Bool continues)
{
  /* We don't preserve RLE instance data across deselect/select because the
     parameters may be different. Each set of parameters is a new
     instance. */
  UNUSED_PARAM(Bool, continues) ;

  /** \todo ajcd 2012-11-08: We'd prefer to have these in the retire_dl call,
      but, but unfortunately several other calls in between retiring the old
      DL and introducing a new DL rely on it. */
  CoreContext.page->rle_flags = 0 ;

  rle_instance_release(instance) ;
}

static Bool rle_introduce_dl(surface_handle_t *handle, DL_STATE *page,
                             Bool continued)
{
  requirement_node_t *bandvals ;

  HQASSERT(handle != NULL, "Nowhere to find RLE instance or page data") ;
  HQASSERT(page != NULL, "No page to introduce") ;

  /* Set memory requirement for the RLE states. We're testing whether bandvals
     exist rather than asserting it because this function can be called during
     DL construction cleanup, when requirement construction may have failed. */
  if ( page->render_resources == NULL ||
       (bandvals = requirement_node_find(page->render_resources,
                                         REQUIREMENTS_BAND_GROUP)) == NULL ||
       !rle_states_memory(page, bandvals))
    return FALSE;

  if ( !continued ) {
    surface_instance_t *params ;
    surface_page_t *rlepage ;
    hq_atomic_counter_t before ;

    params = handle->instance ;
    VERIFY_OBJECT(params, RLE_INSTANCE_NAME) ;

    if ( (rlepage = mm_alloc(mm_pool_rle, sizeof(*rlepage),
                             MM_ALLOC_CLASS_RLE_SURFACE_PAGE)) == NULL )
      return error_handler(VMERROR) ;
#ifdef ASSERT_BUILD
    ++rle_page_allocated ;
#endif

    HqAtomicIncrement(&params->refcount, before) ;
    HQASSERT(before > 0, "RLE instance should have previously been referenced") ;
    rlepage->params = params ;
    /* Construction of the sheet colorant lists is done at the time the sheet
       is rendered, because we need to wait until omissions are decided. */
    rlepage->sepdata = NULL ;

    NAME_OBJECT(rlepage, RLE_PAGE_NAME) ;

    handle->page = rlepage ;
  } else {
    /* Continuing across partial paint: we should already have page data. */
    VERIFY_OBJECT(handle->page, RLE_PAGE_NAME) ;
  }

  return TRUE ;
}

static void rle_retire_dl(surface_handle_t *handle, DL_STATE *page,
                          Bool continues)
{
  surface_page_t *rlepage ;

  UNUSED_PARAM(DL_STATE *, page) ;

  HQASSERT(handle != NULL, "Nowhere to find RLE page data") ;
  rlepage = handle->page ;
  VERIFY_OBJECT(rlepage, RLE_PAGE_NAME) ;
  HQASSERT(page != NULL, "No page to retire") ;

  if ( !continues ) {
    rle_separation_t *sepdata ;

    handle->page = NULL ;

    while ( (sepdata = rlepage->sepdata) != NULL ) {
      rlepage->sepdata = sepdata->next ;
      colorantListDestroy(&sepdata->colorants) ;
      mm_free(mm_pool_rle, sepdata, sizeof(*sepdata)) ;
    }

    rle_instance_release(&rlepage->params) ;
    UNNAME_OBJECT(rlepage) ;
#ifdef ASSERT_BUILD
    --rle_page_allocated ;
#endif
    mm_free(mm_pool_rle, rlepage, sizeof(*rlepage)) ;
  }
}

const GUCR_COLORANT_INFO *rle_sheet_colorants(sheet_data_t *sheet,
                                              surface_handle_t handle,
                                              ColorantListIterator *iterator,
                                              Bool *groupColorant)
{
  DL_STATE *page = CoreContext.page ;

  UNUSED_PARAM(sheet_data_t *, sheet) ;

  /* If not doing runlength we can't have a colorant list. */
  if ( DOING_RUNLENGTH(page) ) {
    surface_sheet_t *rlesheet = handle.sheet ;
    VERIFY_OBJECT(rlesheet, RLE_SHEET_NAME) ;
    HQASSERT(rlesheet->sepdata != NULL &&
             rlesheet->sepdata->sheet_id == guc_getSeparationId(sheet_rs_handle(sheet)),
             "Sheet handle colorant list doesn't match ID") ;
    return colorantListGetFirst(rlesheet->sepdata->colorants, iterator,
                                groupColorant) ;
  }

  return NULL ;
}

RleColorantMap *rle_sheet_colorantmap(sheet_data_t *sheet,
                                      surface_handle_t handle)
{
  surface_sheet_t *rlesheet = handle.sheet ;
  UNUSED_PARAM(sheet_data_t *, sheet) ;
  VERIFY_OBJECT(rlesheet, RLE_SHEET_NAME) ;
  return rlesheet->map ;
}

/* ========================================================================== */
/** Extract colorant matching information from blit color, and unpack blit
    color values specially. We unpack the color values explicitly, because
    backdrop blit handling needs to rescale them before outputting as RLE. */
static void rle_colorants(const blit_color_t *color,
                          const blit_colormap_t *map,
                          colorants_state_t *colorants)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(colorants, "Nowhere to put colorant matching details") ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  HQASSERT(map == color->map, "Color map not from blit color") ;

  colorants->rasterstyle_id = map->rasterstyle_id;
  colorants->nchannels = map->nchannels ;
  colorants->ncolors = color->ncolors ;
  colorants->nmaxblits = color->nmaxblits ;

  /* Copy rendered and maxblitted channels. */
  HqMemCpy(colorants->channels, color->state, map->nchannels * sizeof(blit_channel_state_t)) ;
}

/* ---------------------------------------------------------------------- */

/** \brief Allocate a new block of memory for the passed RLESTATE.

    \param tracker  The band's RLE tracking structure.
    \param state    The RLE state for a particular line.

    \retval TRUE    A new RLE block was allocated.
    \retval FALSE   A new RLE block could not be allocated.

    Memory is taken from the band buffers in fixed-sized blocks, which are
    joined in a linked list for each output line (as represented by the
    RLESTATE structure).

    Each block has a simple structure; the first word contains a pointer to
    the next block. The second word is the block header (which uses the
    RUN_HEADER opcode). The remaining data is arbitrary RLE records.

    Should we run out of memory in the band buffers, we enter a new state
    where the RLE tracker's \c runAbort field is not RLE_GEN_OK, which
    prevents any further RLE from being produced, and the band is retried
    with a smaller height, but with the same amount of memory available. */
static Bool rleNewBlock(surface_band_t *tracker, RLESTATE *state)
{
  uint32 * p;
  int32 blockSizeInBytes = RLE_BLOCK_SIZE_WORDS * sizeof(int32);

#ifdef ASSERT_BUILD
  check_band_consistency(tracker);
#endif

  if ( tracker->runAbort != RLE_GEN_OK )
    return FALSE ;

  /** \todo ajcd 2013-03-28: Optimise the current block to make space. */

  /* We need a new block; first close off the current one, if any. */
  if (state->blockheader) {
    /* Remember that currentword points to the last word written, NOT the next
    free word. */
    *state->blockheader = (RUN_HEADER | 0x100) |
                          ( CAST_PTRDIFFT_TO_UINT32((state->currentword + 1) - state->blockheader) << 18);
  }

  /* Check for overflow of the current band buffer. */
  if (tracker->next_free_block + blockSizeInBytes > tracker->block_limit ) {
    HQASSERT(tracker->runAbort != RLE_GEN_ERROR,
             "Incorrectly resetting RLE generation state") ;
    if (
#ifdef DEBUG_BUILD
        (debug_rle & DEBUG_RLE_NO_EXTEND) != 0 ||
#endif
        !alloc_band_extension(tracker->sheet->band_pool, tracker->pband) ) {
      tracker->runAbort = RLE_GEN_RETRY;
      return FAILURE(FALSE);
    }
    tracker->next_free_block = (uint8 *)tracker->pband->next->mem ;
    tracker->block_limit = tracker->next_free_block + tracker->pband->next->size_to_write ;
    HQASSERT(tracker->next_free_block + blockSizeInBytes <= tracker->block_limit,
             "RLE does not fit in newly allocated block") ;
  }

  p = (uint32 *) tracker->next_free_block;
  tracker->next_free_block += blockSizeInBytes;

  if (state->blockstart != NULL) {
    /* Write the pointer to the next block in the previous block. */
    RLEBLOCK_SET_NEXT(state->blockstart, p);
  } else {
    /* This is the first block output on this line. */
    state->firstblock = p;
    state->colorants.nchannels = 0;
    state->colorants.ncolors = 0;
    state->colorants.rasterstyle_id = 0;
    state->current_pixel_label = SW_PGB_INVALID_OBJECT;
  }

  state->blockstart = p;
  state->blockend = p + RLE_BLOCK_SIZE_WORDS;
  state->blockheader = p + POINTER_SIZE_IN_WORDS;
  RLEBLOCK_SET_NEXT(state->blockstart, NULL);

  /* The 'currentword' pointer always points to the last word written, NOT the
  next free word. */
  state->currentword = state->blockheader;
  return TRUE;
}


/** \brief Ensure that there are a minimum number of words in the current RLE
    block.

    \param tracker  The RLE tracking structure for this band.
    \param state    The RLE state for the line being rendered.
    \param required The number of words required.

    \retval TRUE  The block had the required number of words in it, or a new
                  block was allocated that has the required number of words
                  free.
    \retval FALSE The band does not have enough words left. The tracker's
                  \c runAbort field is set to indicate whether we should retry
                  with a smaller band height, or fail.
*/
static Bool reserveWords(surface_band_t *tracker, RLESTATE *state, int32 required)
{
  if (RLE_BLOCK_OVERFLOW(state, required)) {
    if ( !rleNewBlock(tracker, state) ) {
      HQASSERT(tracker->runAbort != RLE_GEN_OK,
               "RLE block failed but not runAbort") ;
      return FALSE;
    }
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Write \a n bits of data in the least-significant bits of \a word to the
    stream.
 */
static Bool rleWriteBits(surface_band_t *tracker, RLESTATE *state,
                         uint32 word, int32 n)
{
  HQASSERT(n >= 0 && n <= 32, "Invalid number of bits to write") ;
  HQASSERT((word & (uint32)(-1 << n)) == 0, "Too many bits set") ;

  if ( state->bits.remaining < n ) {
    n -= state->bits.remaining ;

    if ( state->bits.remaining > 0 ) {
      *state->currentword |= (word >> n) << state->bits.reserved ;
    }

    if ( !reserveWords(tracker, state, 1) )
      return FALSE ;

    RLE_WRITE(state, 0) ; /* Allocate and clear next word */
    state->bits.reserved = 0 ;
    state->bits.remaining = 32 ;
    state->bits.total += 32 ;

    /* Clear the bits we've written. */
    word &= ~(uint32)(-1 << n) ;
  }

  state->bits.remaining = CAST_SIGNED_TO_UINT8(state->bits.remaining - n) ;
  *state->currentword |= word << (state->bits.remaining + state->bits.reserved) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Run repeat optimisers. These are called from the bitfill/blkfill functions,
   after having been enabled at a higher level (either image blit or
   gouraud span). */

/** Determine if it's reasonable to use a RUN_REPEAT_STORE, and if so, set
    the cache limit and memory base for the state.

    \param tracker  The RLE tracker for this band.
    \param state    The state of the RUN_REPEAT_STORE line.
    \param hwm      Memory used by RUN_REPEAT_STOREs on affected lines so far.
    \param ncolors  The number of colors in the RUN_REPEAT.

    \retval FALSE   Don't use RUN_REPEAT_STORE.
    \retval TRUE    Use RUN_REPEAT_STORE. The recache limit and the high water
                    mark for the line's RLE state are updated.
 */
static Bool repeat_storable(surface_band_t *tracker, RLESTATE *state,
                            size_t hwm, channel_index_t ncolors)
{
  size_t s1, s2, estimate ;

  /* Are we likely to get more than a few runs into a single entry?
     This is an inaccurate, rough estimate, but should be good enough
     for guessing if it's worth starting a RUN_REPEAT_STORE. */
  estimate = ((ncolors * tracker->bitDepth
               + 10 /*runlen*/ + 32 /*header*/) * 16 /*runs*/
              + 7 /*roundup*/) / 8 /*bits*/ ;

  /* Get two cache slot sizes. If run caches are not consecutive, the first
     figure may be the remaining round-off of a previous cache, and may not be
     large enough to be usable. */
  s1 = tracker->repeat_cache_total - hwm ;
  s2 = 0 ;
  if ( !tracker->params->runCacheConsecutive && s1 > 0 ) {
    /* We can't merge the caches, so find the amount to the next
       boundary. */
    size_t n ;

    if ( s1 >= tracker->params->runCacheSize )
      s2 = tracker->params->runCacheSize ;

    n = (s1 - 1) / tracker->params->runCacheSize ;
    s1 -= n * tracker->params->runCacheSize ;
  }

  /* Reset the remaining cache space for the current line. The totals
     used will be propagated to the other lines when the
     RUN_REPEAT_STORE is closed. */
  if ( estimate <= s1 ) {
    state->repeat.cache_mem_hwm = hwm ;
    state->repeat.recache_limit = s1 ;
  } else if ( estimate <= s2 ) {
    /* Can't use the first slot, so bump the highwater mark past it. */
    state->repeat.cache_mem_hwm = hwm + s1 ;
    state->repeat.recache_limit = s2 ;
  } else {
    /* Estimate won't fit in either slot. */
    return FALSE ;
  }

  /* Convert the cache slot size to bits */
  state->repeat.recache_limit *= 8 ;

  /* Adjust the recache limit for the worst-case estimate for memory
     (12 bpc, 2 for the run flag, 6 for the runlength). */
  state->repeat.recache_limit -= ncolors * 12 + 8 ;

  return TRUE ;
}

/** Start collecting spans for a RUN_REPEAT, RUN_REPEAT_STORE, or
    RUN_REPEAT_SOFTMASK record. */
static Bool run_repeat_open(surface_band_t *tracker, uint8 opcode, uint8 id,
                            RLESTATE *state, dcoord lines, dcoord x)
{
  uint32 repeat_type_id = opcode | (id << 24) ;
  RLESTATE *firstState = state ;

  HQASSERT(lines > 0, "No lines for RUN_REPEAT/STORE/SOFTMASK") ;
  do {
    if ( !addPositionIfNeeded(tracker, state, x) ||
         !reserveWords(tracker, state, 1) ) {
      while ( state > firstState ) {
        --tracker->constructingRunRepeat ;
        --state ;
        state->repeat.header = NULL ;
        state->repeat.lastSpan = 0 ;
        state->repeat.copies = 0 ;
      }
      return FALSE ;
    }

    RLE_WRITE(state, repeat_type_id) ;

    /* Store the current write position; we'll finish the record header after
       the runs have been enumerated. */
    state->repeat.header = state->currentword ;
    state->repeat.spanCount = 0 ;
    /* 12-bit RLE defines the initial span count. Other forms don't, so we
       set their repeat.lastSpan to 255. Since all repeat forms but 8-bit
       monochrome limit run lengths to 63, this is never within one
       of the previous span. */
    state->repeat.lastSpan = tracker->bitDepth == 12 ? 2 : 255 ;

    if ( opcode == RUN_REPEAT_STORE ) {
      opcode = RUN_REPEAT_COPY ;
      repeat_type_id = opcode | (id << 24) ;
      state->repeat.copies = lines - 1 ;
    } else {
      state->repeat.copies = 0 ;
      /* Set cache size to unlimited. This will be overridden for
         RUN_REPEAT_STORE, and allows other RUN_REPEAT variants to avoid type
         tests for size. */
      state->repeat.recache_limit = (size_t)-1 ;
    }

    /* Prepare bitstream for writing. The first write will clear and reserve
       the next word. */
    state->bits.reserved = 0 ;
    state->bits.remaining = 0 ;
    state->bits.total = 32 ; /* We've used one word for the header. */

    ++state ;
    ++tracker->constructingRunRepeat ;
  } while ( --lines > 0 ) ;

  return TRUE ;
}

/** Finalise collecting spans for a RUN_REPEAT, RUN_REPEAT_STORE, or
    RUN_REPEAT_SOFTMASK record. RUN_REPEAT_COPY lines should never be passed
    directly to this function, even though they have repeat headers. They'll
    be closed by the call for the RUN_REPEAT_STORE line that started them.
    This can be ensured by closing lines using a forward iteration loop
    at the end of an image. */
static void run_repeat_close(surface_band_t *tracker, RLESTATE *state,
                             dcoord lines)
{
  HQASSERT(tracker->constructingRunRepeat > 0,
           "No RUN_REPEAT/RUN_REPEAT_STORE/RUN_SOFTMASK under construction.");
  tracker->constructingRunRepeat -= lines;

  HQASSERT(lines > 0, "No lines for RUN_REPEAT/STORE/COPY") ;
  do {
    int32 copies = state->repeat.copies ;

    HQASSERT(state->repeat.header != NULL &&
             ((*state->repeat.header & RLE_MASK_RECORD_TYPE) == RUN_REPEAT ||
              (*state->repeat.header & RLE_MASK_RECORD_TYPE) == RUN_REPEAT_STORE ||
              (*state->repeat.header & RLE_MASK_RECORD_TYPE) == RUN_SOFTMASK),
             "Not closing a valid RUN_REPEAT/STORE/SOFTMASK") ;

    /* If the line has RUN_REPEAT_COPY copies, close them and propagate the
       IDs and memory high watermarks to the copies. */
    if ( copies > 0 ) {
      uint16 id = CAST_UNSIGNED_TO_UINT8(*state->repeat.header >> 24) ;

      HQASSERT((*state->repeat.header & RLE_MASK_RECORD_TYPE) == RUN_REPEAT_STORE,
               "Only RUN_REPEAT_STORE can have copies") ;

      tracker->constructingRunRepeat -= copies;

      /* Reset the cache ID high water mark and the remaining space. If we're
         going to partial paint, we cannot reuse this ID in the next paint,
         and we need to allow for the RUN_REPEAT_STOREs allocated so far on
         this line. */
      state->repeat.cache_id_hwm = ++id ;

      HQASSERT((state->bits.total & 7) == 0,
               "Bits written not a whole number of bytes") ;
      state->repeat.cache_mem_hwm += state->bits.total >> 3 ;

      do {
        HQASSERT((*state[copies].repeat.header & RLE_MASK_RECORD_TYPE) == RUN_REPEAT_COPY,
                 "Copy header should contain RUN_REPEAT_COPY") ;

        /* The high watermarks must increase, because when we opened the
           original RUN_REPEAT_STORE we based the ID and memory off the
           maximum of the high water marks on the lines affected. */
        HQASSERT(state[copies].repeat.cache_id_hwm < id,
                 "Copy line already has higher cache ID high water mark") ;
        HQASSERT(state[copies].repeat.cache_mem_hwm < state->repeat.cache_mem_hwm,
                 "Copy line already has higher cache memory high water mark") ;

        state[copies].currentposition = state->currentposition ;
        state[copies].repeat.cache_id_hwm = id ;
        state[copies].repeat.cache_mem_hwm = state->repeat.cache_mem_hwm ;
        state[copies].repeat.header = NULL ;
        state[copies].repeat.lastSpan = 0 ;
      } while ( --copies > 0 ) ;

      HQASSERT(lines == 1, "Should only be closing one line when copies present") ;
    }

    HQASSERT(state->repeat.spanCount > 0 ||
             tracker->runAbort != RLE_GEN_OK,
             "No spans on RUN_REPEAT line") ;
    *state->repeat.header |= state->repeat.spanCount << 8 ;
    state->repeat.header = NULL ;
    state->repeat.spanCount = 0 ;
    state->repeat.lastSpan = 0 ;
    ++state ;
  } while ( --lines > 0 ) ;
}

/** Peephole optimiser to add span to a RUN_REPEAT, RUN_REPEAT_STORE, or
    RUN_REPEAT_SOFTMASK record, if active.

    \param tracker The RLE tracker for this band.
    \param state   The RLE state of the first line in a block to render.
    \param xs      The left X position of the block to render.
    \param len     The number of pixels to render in X.
    \param lines   The number of lines to render in Y.
    \param ncolors The number of color channels in the packed color.
    \param colors  The packed color channels, scaled appropriately for the
                   output RLE depth.

    \retval FALSE The span was not captured into a RUN_REPEAT, so the caller
                  should go ahead and handle it as normal.
    \retval TRUE  The span was captured into a RUN_REPEAT. The caller should
                  ignore it.
*/
static inline Bool run_repeat_optimise(surface_band_t *tracker,
                                       RLESTATE *state, dcoord xs, dcoord len,
                                       dcoord lines, channel_index_t ncolors,
                                       const unsigned short colors[])
{
  uint8 opcode = RUN_REPEAT ;
  uint8 id = 0 ;

  HQASSERT(lines > 0, "No lines for RUN_REPEAT") ;
  HQASSERT(len > 0, "Runlength must be positive for RUN_REPEAT") ;
  HQASSERT(ncolors > 0, "No colors for RUN_REPEAT") ;

  /* Don't try to do anything if we've already aborted. */
  if (tracker->runAbort != RLE_GEN_OK)
    return TRUE ; /* Pretend we did it */

  switch ( tracker->run_repeat_state ) {
  default:
    HQFAIL("Invalid repeat state") ;
  case NO_RUN_REPEAT:
    return FALSE ; /* No peephole optimisation */
  case DO_RUN_REPEAT_IF_EASY:
    if ( state->repeat.header != NULL ) {
      /* If the run is too long for one span, close off run repeats. */
      /** \todo ajcd 2010-05-25: We might want some hysteresis in the run
          length decision, so we don't keep starting and stopping run
          repeats. */
      if (len > tracker->max_run_repeat )
        run_repeat_close(tracker, state, lines) ;
    }
    /* Don't make this an else on the previous condition, run_repeat_close()
       can clear the repeat header. */
    if ( state->repeat.header == NULL && len > tracker->max_run_repeat ) {
      /* If long span and not started RUN_REPEAT, don't start now. */
      return FALSE ;
    }

    break ;
  case DO_RUN_STORE_OR_REPEAT:
    if ( lines > 1 ) { /* No point in RUN_REPEAT_STORE if only one line. */
      if ( state->repeat.header == NULL ) {
        size_t mem = state->repeat.cache_mem_hwm ;
        uint16 nextid = state->repeat.cache_id_hwm ;
        dcoord index = 0 ;

        /* Find the minimum amount of memory left in the run caches, and the
           first ID available on all of the lines. */
        for ( index = 1 ; index < lines ; ++index ) {
          if ( state[index].repeat.cache_mem_hwm > mem )
            mem = state[index].repeat.cache_mem_hwm ;
          if ( state[index].repeat.cache_id_hwm > nextid )
            nextid = state[index].repeat.cache_id_hwm ;
        }

        if ( nextid < 256 && repeat_storable(tracker, state, mem, ncolors) ) {
          /* We definitely can start storing, so put RUN_REPEAT_COPY on
             subsequent lines and optimise the first line. */
          opcode = RUN_REPEAT_STORE ;
          id = CAST_UNSIGNED_TO_UINT8(nextid) ;
          if ( !run_repeat_open(tracker, opcode, id, state, lines, xs) )
            return TRUE ; /* Pretend we did it. */
        }
      }
    }

    break ;
  case DO_RUN_REPEAT:
    break ;
  }

  do { /* split runlen into palatable pieces */
    RLESTATE *lineState = state ;
    dcoord lineCount = lines ;
    dcoord runLen ;

    INLINE_MIN32(runLen, len, tracker->max_run_repeat) ;

    do { /* for all lines */
      int32 extent = 1 ;

      if ( lineState->repeat.header ) {
        /* If we're about to overflow the total span count, or run out of cache
           space, close and start a new entry. Starting a new entry can expand
           the number of lines we touch, if we've run out of RUN_REPEAT_STORE
           ids or cache space. */
        opcode = CAST_UNSIGNED_TO_UINT8(*lineState->repeat.header & RLE_MASK_RECORD_TYPE) ;
        id = CAST_UNSIGNED_TO_UINT8(*lineState->repeat.header >> 24) ;
        HQASSERT(opcode == RUN_REPEAT || opcode == RUN_REPEAT_STORE,
                 "Not a RUN_REPEAT or RUN_REPEAT_STORE") ;

        extent += lineState->repeat.copies ;

        if ( lineState->currentposition != xs ||
             lineState->bits.total > lineState->repeat.recache_limit ||
             lineState->repeat.spanCount == 0xffffu ) {
          run_repeat_close(tracker, lineState, 1) ; /* Also closes copies */

          if ( opcode == RUN_REPEAT_STORE ) {
            /* If we either ran out of IDs or space, change to RUN_REPEATs. */
            if ( (uint8)++id == 0 ||
                 !repeat_storable(tracker, lineState, lineState->repeat.cache_mem_hwm, ncolors) ) {
              opcode = RUN_REPEAT ;
              id = 0 ;
              extent = 1 ;
            }
          }
        }
      }

      /* Default is to start a RUN_REPEAT. */
      if ( lineState->repeat.header == NULL &&
           !run_repeat_open(tracker, opcode, id, lineState, extent, xs) )
        return TRUE /* Pretend we did it */ ;

      /* Handle the multiplicity of RLE repeat colour formats. */
      if ( tracker->bitDepth == 12 ) { /* 12-bit color RLE */
        const unsigned short *pcolor = colors ;
        channel_index_t count = ncolors ;
        dcoord runDiff = runLen - lineState->repeat.lastSpan ;

        /* 12-bit color RLE has a 2-bit runlength difference flag,
           conditionally followed by a 6-bit runlength, then all of the
           colors with no padding. */
        if ( abs(runDiff) <= 1 ) {
          if ( !rleWriteBits(tracker, lineState, runDiff & 3, 2) )
            return TRUE ; /* Pretend we did it */
        } else {
          if ( !rleWriteBits(tracker, lineState, REPEAT_COUNT_NEW, 2) ||
               !rleWriteBits(tracker, lineState, runLen, 6) )
            return TRUE ; /* Pretend we did it */
        }

        do {
          if ( !rleWriteBits(tracker, lineState, *pcolor++, 12) )
            return TRUE ; /* Pretend we did it */
        } while ( --count ) ;
      } else if ( tracker->bitDepth == 10 && ncolors > 1 ) { /* 10-bit color RLE */
        const unsigned short *pcolor = colors ;
        channel_index_t count = ncolors ;
        dcoord runDiff = runLen - lineState->repeat.lastSpan ;

        /* 10-bit color RLE has a 10-bit runlength (6 bits used), then all of
           the colors. The top 2 bits of each word contain a runlength
           difference flag, applying to the first run starting in the word.
           The very first runlength is always explicit. */

        /* If at the start of a word, write a new repeat count indicator.
           We'll patch these up later if necessary. */
        if ( (lineState->bits.remaining & 31) == 0 ) {
          if ( !rleWriteBits(tracker, lineState, REPEAT_COUNT_NEW, 2) )
            return TRUE ; /* Pretend we did it */
        }

        if ( abs(runDiff) <= 1 &&
             /* Only use top bits if not already used in this word: */
             (*lineState->currentword >> 30) == REPEAT_COUNT_NEW ) {
          *lineState->currentword ^= (REPEAT_COUNT_NEW ^ runDiff) << 30 ;
        } else {
          if ( !rleWriteBits(tracker, lineState, runLen, 10) )
            return TRUE ; /* Pretend we did it */
        }

        do {
          if ( (lineState->bits.remaining & 31) == 0 ) {
            if ( !rleWriteBits(tracker, lineState, REPEAT_COUNT_NEW, 2) )
              return TRUE ; /* Pretend we did it */
          }

          if ( !rleWriteBits(tracker, lineState, *pcolor++, 10) )
            return TRUE ; /* Pretend we did it */
        } while ( --count ) ;
      } else { /* 8 or 10-bit single color RLE. */
        /* 8- and 10-bit single color RLE have a series of 8-bit (10-bit)
           color followed by an 8-bit (6-bit) runlengths. */
        HQASSERT(ncolors == 1, "8/10-bit RLE has too many colors") ;
        if ( !rleWriteBits(tracker, lineState, colors[0], tracker->bitDepth) ||
             !rleWriteBits(tracker, lineState, runLen, 16 - tracker->bitDepth) )
          return TRUE ; /* Pretend we did it */
      }

      lineState->currentposition += runLen ;
      lineState->repeat.lastSpan = CAST_SIGNED_TO_UINT8(runLen) ;
      lineState->repeat.spanCount++ ;
      HQASSERT(lineState->repeat.spanCount > 0, "Wrapped span count") ;

      lineState += extent ;
      lineCount -= extent ;
    } while ( lineCount > 0 ) ;

    xs += runLen ;
    len -= runLen ;
  } while ( len > 0 ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** Always add a new position. */
static inline Bool addPosition(surface_band_t *tracker, RLESTATE *state,
                               dcoord position)
{
  if ( !reserveWords(tracker, state, 1) )
    return FALSE;

  RLE_WRITE(state, RUN_POSITION | (position << 8));
  state->currentposition = position;

  return TRUE;
}

/** Add a new position, if it differs from the current position. */
static Bool addPositionIfNeeded(surface_band_t *tracker, RLESTATE *state,
                                dcoord position)
{
  if (position != state->currentposition)
    return addPosition(tracker, state, position) ;

  return TRUE;
}


/** Add a RUN_SCREEN if the screen has changed. */
static Bool addScreenIfNeeded(surface_band_t *tracker, RLESTATE* state,
                              render_blit_t *rb)
{
  int32 new_index;

  if ( (blit_quantise_state(rb->color) & blit_quantise_mid) == 0 )
    return TRUE; /* no need for screen */
  new_index = tracker->color_rle
    ? rb->color->quantised.spotno
    : ht_screen_index(rb->color->quantised.spotno, rb->color->quantised.type,
                      tracker->ci);
  if ( new_index == -1 )
    return detail_error_handler(CONFIGURATIONERROR,
                                "Modular screens not compatible with RLE output");
  if ( state->screenid == new_index )
    return TRUE; /* Screen has not changed - nothing to do. */
  if ( !reserveWords(tracker, state, 1) )
    return FALSE;
  RLE_WRITE(state, RUN_SCREEN | (new_index << RLE_SHIFT_SCREEN_ID));
  state->screenid = new_index;
  return TRUE;
}

/** State for softmask callback. */
typedef struct softmask_run_t {
  surface_band_t *tracker ;
  RLESTATE *state ;

  OBJECT_NAME_MEMBER
} softmask_run_t ;

#define SOFTMASK_RUN_NAME "Softmask run data"

/** Write a run of softmask alpha data to the current line. This is the
    callback function passed to the backdrop softmask iterator in
    addSoftmaskIfNeeded(). */
static Bool addSoftmaskRun(void *lineStatePointer,
                           uint16 maskAlpha,
                           uint32 length)
{
  softmask_run_t *run = lineStatePointer ;
  surface_band_t *tracker ;
  RLESTATE *state ;

  VERIFY_OBJECT(run, SOFTMASK_RUN_NAME);
  tracker = run->tracker ;
  state = run->state ;

  do {
    dcoord runLen;

    /* The run length in the record is a 6-bit field. */
    INLINE_MIN32(runLen, (dcoord)length, 63) ;

    if ( !rleWriteBits(tracker, state, RLE_GET_ALPHA(maskAlpha), 10) ||
         !rleWriteBits(tracker, state, runLen, 6) )
      return FALSE ;
    state->currentposition += runLen ;
    state->repeat.lastSpan = CAST_SIGNED_TO_UINT8(runLen) ;
    /** \todo ajcd 2010-05-25: Close and open new run when span count
        hits limit (65535). */
    HQASSERT(state->repeat.spanCount <= 0xfffe, "Overflowed span count") ;
    state->repeat.spanCount++ ;

    length -= runLen ;
  } while (length > 0) ;

  return TRUE;
}

/**
 * Return the mask ID for the passed SoftMaskAttrib pointer. Returns -1 if
 * 'mask' is null.
 */
static int32 softMaskCacheHash(SoftMaskAttrib* mask, dbbox_t* bounds)
{
  uint32 areaHash = bounds->x1 ^ bounds->x2 ^ bounds->y1 ^ bounds->y2;
  return CAST_UINTPTRT_TO_INT32((((uintptr_t)mask >> 4) + areaHash) % RLE_SMASK_CACHE_SIZE);
}

/**
 * Returns true if the passed cache entries are identical.
 */
static Bool softMaskCacheEntryEqual(RleSoftmaskCacheEntry* a,
                                    RleSoftmaskCacheEntry* b)
{
  return a->mask == b->mask && bbox_equal(&a->area, &b->area);
}

/** Add a softmask record if required. Note that a 'y' coordinate is required
    because the correct line within the softmask data must be obtained. */
static Bool addSoftmaskIfNeeded(surface_band_t *tracker,
                                RLESTATE *lineState, dcoord y,
                                RleSoftmaskCacheEntry *softmask, int32 id)
{
  Bool result = TRUE ;

  HQASSERT(id < RLE_SMASK_CACHE_SIZE, "addSoftmask - id invalid.");

  if (id >= 0 &&
      ! softMaskCacheEntryEqual(&lineState->trans.maskCache[id], softmask)) {
    /* There is a mask and it is not currently in the cache. */
    Backdrop *backdrop = groupGetBackdrop(softmask->mask->group);

    HQASSERT(softmask && softmask->mask != NULL, "addSoftmask - no mask set.");

    /* Preceed the mask data with a position to indicate where the mask starts. */
    if ( !addPosition(tracker, lineState, softmask->area.x1) )
      return FALSE;

    /* Store the mask in the cache. */
    lineState->trans.maskCache[id] = *softmask;

    /* Increment the mask id - 0 is a special value indicating no mask; note
       this addition is also applied when outputting RUN_TRANSPARENT records
       - see addRunTransparentIfNeeded(). */
    ++id;

    result = run_repeat_open(tracker, RUN_SOFTMASK, CAST_SIGNED_TO_UINT8(id),
                             lineState, 1, softmask->area.x1) ;

    if ( result ) {
      softmask_run_t run_data ;

      run_data.tracker = tracker ;
      run_data.state = lineState ;
      NAME_OBJECT(&run_data, SOFTMASK_RUN_NAME) ;

      /* Output the line. This function will repeatedly call addSoftmaskRun()
         to enumerate all the runs in the line. */
      result = bd_readSoftmaskLine(backdrop, y,
                                   softmask->area.x1, softmask->area.x2,
                                   &addSoftmaskRun, &run_data) ;
      UNNAME_OBJECT(&run_data) ;
    }

    if ( lineState->repeat.header )
      run_repeat_close(tracker, lineState, 1) ;
  }

  return result ;
}

/** Add a RUN_TRANSPARENT record if the current line's transparency state does
    not match the current object's state.

    Note that the mask cache must contain the correct mask for the current
    object. */
static Bool addRunTransparentIfNeeded(surface_band_t *tracker,
                                      RLESTATE *lineState,
                                      RleSoftmaskCacheEntry *softmask,
                                      int32 id)
{
  UNUSED_PARAM(RleSoftmaskCacheEntry *, softmask) ;

  HQASSERT(id == -1 || lineState->trans.maskCache[id].mask != NULL,
           "addRunTransparentIfNeeded - required mask not in the cache.");
  HQASSERT(id == -1 ||
           softMaskCacheEntryEqual(&lineState->trans.maskCache[id], softmask),
           "addRunTransparentIfNeeded - required mask does not match that cached.");

  /* Do we need a new RUN_TRANSPARENT? */
  if (id != lineState->trans.maskId ||
      tracker->transparencyState.alpha != lineState->trans.alpha ||
      tracker->transparencyState.alphaIsShape != lineState->trans.alphaIsShape ||
      tracker->transparencyState.blendMode != lineState->trans.blendMode) {

    lineState->trans.maskId = (int16)id;
    lineState->trans.alpha = tracker->transparencyState.alpha;
    lineState->trans.alphaIsShape = tracker->transparencyState.alphaIsShape;
    lineState->trans.blendMode = tracker->transparencyState.blendMode;

    /* Output the RUN_TRANSPARENT. */
    if ( !reserveWords(tracker, lineState, 1) )
      return FALSE;

    /* Note that we use 'maskId + 1' - zero is a special value meaning
    'no mask'. Note that we also apply this when outputting the mask data - see
    rleEmitSoftmask()*/
    RLE_WRITE(lineState,
              RUN_TRANSPARENT
              | (tracker->transparencyState.alpha << 6)
              | (tracker->transparencyState.blendMode << 16)
              | (tracker->transparencyState.alphaIsShape ? 1<<23 : 0)
              | ((id + 1) << RLE_SHIFT_REPEAT_ID)) ;
  }

  return TRUE;
}

Bool addTransparencyIfNeeded(surface_band_t *tracker, RLESTATE *lineState,
                             dcoord y)
{
  RleSoftmaskCacheEntry *softmask = &tracker->transparencyState.mask;
  int32 id = -1 ;

  if ( softmask->mask != NULL )
    id = softMaskCacheHash(softmask->mask, &softmask->area) ;

  return addSoftmaskIfNeeded(tracker, lineState, y, softmask, id) &&
         addRunTransparentIfNeeded(tracker, lineState, softmask, id) ;
}

/** Add an object type record if the current pixel label does not match the
    last used. */
static Bool addObjectTypeIfNeeded(surface_band_t *tracker, RLESTATE *state,
                                  render_blit_t *rb)
{
  channel_output_t currentLabel;
  uint8 rendering_intent;

  /* Only output object type records if required (this is controlled by a page
  device key. */
  if ( !tracker->params->runObjectType )
    return TRUE;

  VERIFY_OBJECT(rb->color, BLIT_COLOR_NAME) ;
  currentLabel = rb->color->quantised.qcv[rb->color->map->type_index];
  HQASSERT( (currentLabel << RLE_SHIFT_OBJECT_TYPE) <= RLE_MASK_OBJECT_TYPE,
            "Type label mapped out of RLE range." );
  rendering_intent = rb->color->rendering_intent;

  if ((uint8)currentLabel != state->current_pixel_label
      || rendering_intent != state->current_rendering_intent) {
    if (! reserveWords(tracker, state, 1) )
      return FALSE;

    RLE_WRITE(state,
              RUN_OBJECT_TYPE
              | (((uint8)currentLabel << RLE_SHIFT_OBJECT_TYPE) &
                 RLE_MASK_OBJECT_TYPE)
              | (rendering_intent << RLE_SHIFT_RENDERING_INTENT));
    state->current_pixel_label = (uint8)currentLabel;
    state->current_rendering_intent = rendering_intent;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool addInfosIfNeeded(surface_band_t *tracker, RLESTATE* state)
{
  uint32 missing_infos ;
  uint32 i ;

  if (state->last_object == tracker->run_info.current_listobject)
    /* Info has not changed - nothing to do. */
    return TRUE;

  if ( tracker->run_info.current_listobject_needs_identifiers ) {
    HQASSERT( tracker->run_info.current_listobject, "NULL listobject can't need codes" ) ;
    /* Give this object some repeat codes of its own */
    for ( i = 0; i < tracker->run_info.current_gts->num_dl_blocks; i++ ) {
      int32 tag_type = tracker->run_info.current_gts->tags[i].block.type_num ;
      tracker->run_info.current_tags |= (1 << i) ;
      if ( tracker->run_info.identifiers[tag_type] > 0 )
        tracker->run_info.identifiers[tag_type]-- ;
    }
    tracker->run_info.current_listobject_needs_identifiers = FALSE ;
  }

  /* Dispose of old tags */
  missing_infos = state->run_info_tags_present & ~ tracker->run_info.current_tags ;
  if ( missing_infos != 0 ) {
    for ( i = 0; i < (1U << RLE_LEN_INFO_TYPE); i++ ) {
      if ( missing_infos & (1 << i) ) {
        if ( !reserveWords(tracker, state, 1) )
          return FALSE;

        /* identifier is zero, count is zero, reuse is zero */
        RLE_WRITE(state, (i << RLE_SHIFT_INFO_TYPE) | RUN_INFO) ;
        state->run_info_tags_present &= ~ (1 << i) ;
      }
    }
  }

  /* Now add new tags */
  if ( tracker->run_info.current_listobject != NULL ) {
    for ( i = 0; i < tracker->run_info.current_gts->num_dl_blocks; i++ ) {
      TAG_BLOCK_INFO *tbi = & tracker->run_info.current_gts->tags[i].block ;
      int32 tag_type = tbi->type_num ;
      uint32 repeatcode = tracker->run_info.identifiers[tag_type] ;
      uint32 *source, *send ;

      if ( state <= tracker->run_info.earliest_scanline_data_emitted_for_so_far || repeatcode == 0) {
        /* Write the whole data */
        if ( !reserveWords(tracker, state, 1 + tbi->data_words) )
          return FALSE;

        RLE_WRITE(state, (repeatcode << RLE_SHIFT_INFO_ID) |
                         (tag_type << RLE_SHIFT_INFO_TYPE) |
                         (tbi->data_words << RLE_SHIFT_INFO_COUNT) |
                         RUN_INFO) ;
        HQASSERT( tracker->run_info.current_data, "tags but no data?" ) ;
        source = tracker->run_info.current_data + tbi->data_woffset;
        send = source + tbi->data_words ;
        while ( source < send ) {
          RLE_WRITE(state, *source) ;
          source ++ ;
        }
      } else {
        if ( !reserveWords(tracker, state, 1) )
          return FALSE;

        RLE_WRITE(state, (repeatcode << RLE_SHIFT_INFO_ID) |
                         (tag_type << RLE_SHIFT_INFO_TYPE) |
                         (1U << RLE_SHIFT_INFO_REUSE) | RUN_INFO) ;
      }
      state->run_info_tags_present |= (1 << tag_type) ;
    }
    if ( state <= tracker->run_info.earliest_scanline_data_emitted_for_so_far )
      tracker->run_info.earliest_scanline_data_emitted_for_so_far = state ;
  }

  state->last_object = tracker->run_info.current_listobject ;
  return TRUE;
}

/** A wrapper for functions which output details which can vary for each
    display list object. */
static Bool addObjectDetailsIfNeeded(surface_band_t *tracker, RLESTATE *state,
                                     render_blit_t *rb)
{
  return addObjectTypeIfNeeded(tracker, state, rb) &&
         addInfosIfNeeded(tracker, state) &&
         addScreenIfNeeded(tracker, state, rb);
}

/* ---------------------------------------------------------------------- */

/** Start rendering a new output sheet. */
static Bool rle_sheet_begin(surface_handle_t *handle, sheet_data_t *sheet_data)
{
  DL_STATE *page = CoreContext.page ;
  surface_page_t *rlepage ;
  surface_sheet_t *rlesheet ;
  rle_separation_t *sepdata ;
  int32 sheet_id ;

  HQASSERT(handle != NULL, "Nowhere to find RLE page data") ;
  rlepage = handle->page ;
  VERIFY_OBJECT(rlepage, RLE_PAGE_NAME) ;

  /* Create a colorant list for this sheet if we've not done so already. */
  sheet_id = guc_getSeparationId(sheet_rs_handle(sheet_data)) ;
  for ( sepdata = rlepage->sepdata ;
        sepdata != NULL && sepdata->sheet_id != sheet_id ;
        sepdata = sepdata->next )
    EMPTY_STATEMENT() ;

  if ( sepdata == NULL ) {
    int32 i;

    if ( (sepdata = mm_alloc(mm_pool_rle, sizeof(*sepdata),
                             MM_ALLOC_CLASS_RLE_COLORANT_LIST)) == NULL )
      return error_handler(VMERROR) ;

    if ( (sepdata->colorants = colorantListNew(mm_pool_rle)) == NULL ) {
      mm_free(mm_pool_rle, sepdata, sizeof(*sepdata)) ;
      return FALSE ;
    }
    sepdata->sheet_id = sheet_id ;
    sepdata->prev_repeat_mem_hwm = sepdata->next_repeat_mem_hwm = 0 ;
    sepdata->prev_repeat_id_hwm = sepdata->next_repeat_id_hwm = 0 ;
    sepdata->next = rlepage->sepdata ;

    /* Initialise the RUN_INFO id array. This can only be done prior to the
       first paint of a page; it's not safe to attempt to reuse id's on each
       band because the same band may be rendered multiple times, either
       during a partial paint or during backdrop rendering. */
    for ( i = 0; i < (1 << RLE_LEN_INFO_TYPE); ++i ) {
      /* This is one greater than the last valid code */
      sepdata->run_info_ids[i].prev =
        sepdata->run_info_ids[i].next = (1u << RLE_LEN_INFO_ID) ;
    }

    rlepage->sepdata = sepdata ;
  } else {
    /* Starting a new sheet pass over an existing separation. The
       highwatermark we output from the previous pass now becomes the
       starting value for this pass. */
    sepdata->prev_repeat_mem_hwm = sepdata->next_repeat_mem_hwm ;
    sepdata->prev_repeat_id_hwm = sepdata->next_repeat_id_hwm ;
  }

  /* Add the non-omitted colorants to the colorant list, unless they're already
     there. */
  if ( !colorantListAddFromRasterStyle(sepdata->colorants,
                                       sheet_rs_handle(sheet_data),
                                       TRUE /*sheet only*/) )
    return FALSE ;

  /* If we're doing a compositing pass, then add the group colorants to the
     colorant list. */
  if ( rlepage->params->runTransparency &&
       pass_doing_transparency(sheet_pass(sheet_data)) ) {
    if ( !groupRLEColorants(sepdata->colorants, page) )
      return FALSE;
  }

  if ( (rlesheet = mm_alloc(mm_pool_rle, sizeof(*rlesheet),
                            MM_ALLOC_CLASS_RLE_SURFACE_SHEET)) == NULL )
    return error_handler(VMERROR) ;
#ifdef ASSERT_BUILD
  ++rle_sheet_allocated ;
#endif

  /* Construct a colorant map. When producing transparent RLE, this will be
     changed whenever a group is rendered, but the mapping may be required
     before any groups are rendered (e.g. halftone screen exporting).
     The colorant list may change between partial paints of a sheet, so the
     colorant map needs to be rebuilt each time. */
  if ( (rlesheet->map =
        rleColorantMapNew(page->dlpools, page->hr, sepdata->colorants)) == NULL ) {
    mm_free(mm_pool_rle, rlesheet, sizeof(*rlesheet)) ;
#ifdef ASSERT_BUILD
    --rle_sheet_allocated ;
#endif
    return FALSE;
  }

  rlesheet->rlepage = rlepage ;
  rlesheet->sepdata = sepdata ;
  rlesheet->band_pool =
    resource_requirement_get_pool(page->render_resources, TASK_RESOURCE_BAND_OUT) ;
  HQASSERT(rlesheet->band_pool != NULL, "No output band pool for RLE") ;
  NAME_OBJECT(rlesheet, RLE_SHEET_NAME) ;

  handle->sheet = rlesheet ;

  return TRUE ;
}

/** Finish rendering an output sheet. */
static Bool rle_sheet_end(surface_handle_t handle, sheet_data_t *sheet_data,
                          Bool result)
{
  surface_sheet_t *rlesheet ;
  surface_page_t *rlepage ;

  UNUSED_PARAM(Bool, result) ;

  rlesheet = handle.sheet ;
  VERIFY_OBJECT(rlesheet, RLE_SHEET_NAME) ;
  rleColorantMapDestroy(&rlesheet->map) ;
  resource_pool_release(&rlesheet->band_pool) ;

  rlepage = rlesheet->rlepage ;
  VERIFY_OBJECT(rlepage, RLE_PAGE_NAME) ;
  handle.page = rlepage ;

  UNNAME_OBJECT(rlesheet) ;
  mm_free(mm_pool_rle, rlesheet, sizeof(*rlesheet)) ;
#ifdef ASSERT_BUILD
  --rle_sheet_allocated ;
#endif

  if ( pass_is_final(sheet_pass(sheet_data)) ) {
    int32 sheet_id = guc_getSeparationId(sheet_rs_handle(sheet_data)) ;
    rle_separation_t *sepdata, **prev = &rlepage->sepdata ;
    while ( (sepdata = *prev) != NULL ) {
      if ( sepdata->sheet_id == sheet_id ) {
        *prev = sepdata->next ;
        colorantListDestroy(&sepdata->colorants) ;
        mm_free(mm_pool_rle, sepdata, sizeof(*sepdata)) ;
        break ;
      }
      prev = &sepdata->next ;
    }
  }

  return TRUE ;
}

/** RLE image blit works out the extent of any possible run repeats, and sets
    up to capture them. */
static void imagebltrle(render_blit_t *rb, imgblt_params_t *params,
                        imgblt_callback_fn *callback,
                        Bool *result)
{
  dcoord y1, y2 ;
  surface_band_t *tracker ;

  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  /* Don't try to do anything if we've already aborted. */
  if (tracker->runAbort != RLE_GEN_OK)
    return;

  /* Work out the vertical extent of the image. */
  if ( params->orthogonal ) {
    y1 = params->image->bbox.y1 ;
    y2 = params->image->bbox.y2 ;

    if ( tracker->disabled ||
         params->type == IM_BLIT_KNOCKOUT ) {
      tracker->run_repeat_state = NO_RUN_REPEAT ;
    } else if ( params->type == IM_BLIT_MASK ||
                rb->clipmode == BLT_CLP_COMPLEX ) {
      /* Use run repeat if easy to get pixel merging behaviour. */
      tracker->run_repeat_state = DO_RUN_REPEAT_IF_EASY ;
    } else {
      /* We'll try to build RUN_REPEAT_STORE if we're going to save
         significant space doing so. The details of tracking the memory are
         left to the individual lines. */
      if ( params->ncols > 1 ) {
        tracker->run_repeat_state = DO_RUN_STORE_OR_REPEAT ;
      } else {
        tracker->run_repeat_state = DO_RUN_REPEAT ;
      }
    }
  } else {
    if ( tracker->disabled ) {
      tracker->run_repeat_state = NO_RUN_REPEAT ;
    } else if ( params->type == IM_BLIT_IMAGE &&
                rb->clipmode != BLT_CLP_COMPLEX ) {
      tracker->run_repeat_state = DO_RUN_REPEAT ;
    } else {
      /* Use run repeat if easy to get pixel merging behaviour for knockouts
         and masks. */
      tracker->run_repeat_state = DO_RUN_REPEAT_IF_EASY ;
    }

    y1 = params->image->bbox.y1 ;
    y2 = params->image->bbox.y2 ;
  }
  INLINE_MAX32(y1, rb->p_ri->clip.y1, y1) ;
  INLINE_MIN32(y2, y2, rb->p_ri->clip.y2) ;

  if ( tracker->run_repeat_state != NO_RUN_REPEAT ) {
    const blit_color_t *color = rb->color ;
    RLESTATE *lineState = GET_RLE_STATE(rb, y1) ;
    dcoord y ;

    for ( y = y1 ; y <= y2 ; ++y, ++lineState )
      if ( !addColorantsIfNeeded(tracker, lineState, color) ||
           !addTransparencyIfNeeded(tracker, lineState, y) ||
           !addObjectDetailsIfNeeded(tracker, lineState, rb) ) {
        *result = tracker->runAbort != RLE_GEN_ERROR;
        return;
      }
  }

  imagebltn(rb, params, callback, result) ;

  if ( tracker->run_repeat_state != NO_RUN_REPEAT ) {
    RLESTATE* lineState = GET_RLE_STATE(rb, y1) ;
    dcoord y ;

    for ( y = y1 ; y <= y2 ; ++y, ++lineState ) {
      if ( lineState->repeat.header )
        run_repeat_close(tracker, lineState, 1) ;
      HQASSERT(lineState->repeat.header == NULL, "Still have repeat header") ;
      HQASSERT(lineState->repeat.spanCount == 0, "Still have span count") ;
      HQASSERT(lineState->repeat.lastSpan == 0, "Still have last span set") ;
    }
  }

  HQASSERT(tracker->constructingRunRepeat == 0,
           "Shouldn't still be constructing run repeat after image") ;
  tracker->run_repeat_state = NO_RUN_REPEAT ;
  if ( tracker->runAbort == RLE_GEN_ERROR )
    *result = FALSE;
}

/** Set the transparency state values in the passed RLESTATE to their defaults.
*/
static void rleStateDefaultTransparency(RLESTATE* self)
{
  HQASSERT(self != NULL,
           "rleStateDefaultTransparency - 'lineState' cannot be null");

  self->trans.maskId = -1;
  self->trans.alpha = RLE_ALPHA_MAX;
  self->trans.alphaIsShape = FALSE;
  self->trans.blendMode = 0;
}

/** Initialise the passed state with start-of-line defaults.

    \param tracker The RLE tracker for this band.
    \param page  The DL about the be rendered.
    \param state The RLE state being initialised.
    \param repeat_mem_hwm  The previous highwatermark for run cache memory.
    \param repeat_id_hwm  The previous highwatermark for run IDs.
    \param continued  TRUE if the render continues an earlier band or partial
                      paint.
*/
static void rleStateInit(surface_band_t *tracker, DL_STATE *page,
                         RLESTATE *state,
                         size_t repeat_mem_hwm, uint16 repeat_id_hwm,
                         Bool continued)
{
  int cacheIndex;

  state->blockstart = NULL;
  state->blockheader = NULL;
  state->blockend = NULL;
  state->firstblock = NULL;
  state->currentword = NULL;

  /* Start at an unfeasible position if continuing from a previous state,
     we need to issue a RUN_POSITION. */
  state->currentposition = continued ? MAXDCOORD : 0;

  state->screenid = continued ? -1
    : (tracker->color_rle ? page->default_spot_no
       : ht_screen_index(page->default_spot_no, HTTYPE_DEFAULT, tracker->ci));

  state->colorants.rasterstyle_id = 0;
  state->colorants.nchannels = 0;
  state->colorants.ncolors = 0;
  state->colorants.nmaxblits = 0;

  state->last_object = NULL ;

  state->bits.reserved = 0 ;
  state->bits.remaining = 0 ;
  state->bits.total = 0 ;

  state->repeat.header = NULL;
  state->repeat.cache_mem_hwm = repeat_mem_hwm ;
  state->repeat.recache_limit = 0 ;
  state->repeat.copies = 0 ;
  state->repeat.cache_id_hwm = repeat_id_hwm ;
  state->repeat.spanCount = 0 ;
  state->repeat.lastSpan = 0 ;

  state->current_pixel_label = SW_PGB_INVALID_OBJECT;
  state->current_rendering_intent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
  state->run_info_tags_present = 0;

  /* Transparency state. */
  for (cacheIndex = 0; cacheIndex < RLE_SMASK_CACHE_SIZE; cacheIndex ++)
    state->trans.maskCache[cacheIndex].mask = NULL;

  rleStateDefaultTransparency(state);
}

/** Band localiser for RLE. We use this function to initialise, then render,
    then finalise a band of RLE data. */
static Bool rle_band_render(surface_handle_t *handle,
                            const render_state_t *rs,
                            render_band_callback_fn *callback,
                            render_band_callback_t *data,
                            surface_bandpass_t *bandpass)
{
  int32 i, lineCount, nValues;
  dcoord width ;
  RLESTATE *state;
  Bool result = FALSE, continued ;
  surface_sheet_t *rlesheet ;
  surface_band_t rle_tracker ;
  rle_separation_t *sepdata ;
  size_t repeat_mem ;
  uint16 repeat_id ;

  HQASSERT(rs != NULL, "No render state") ;
  HQASSERT(callback != NULL, "No render band callback") ;
  HQASSERT(data != NULL, "No render band callback data") ;

  rlesheet = handle->sheet ;
  VERIFY_OBJECT(rlesheet, RLE_SHEET_NAME) ;
  sepdata = rlesheet->sepdata ;

  rle_tracker.sheet = rlesheet ;

  VERIFY_OBJECT(rlesheet->rlepage, RLE_PAGE_NAME) ;
  rle_tracker.params = rlesheet->rlepage->params ;

  rle_tracker.pband = task_resource_fix(TASK_RESOURCE_BAND_OUT,
                                        rs->cs.bandlimits.y2, &continued) ;
  HQASSERT(rle_tracker.pband != NULL, "No output band") ;
  HQASSERT(continued, "Output band was not already fixed") ;
  HQASSERT(rle_tracker.pband->next == NULL, "Output band already extended") ;

  rle_tracker.next_free_block = (uint8 *)rle_tracker.pband->mem;
  rle_tracker.block_limit = rle_tracker.next_free_block + rle_tracker.pband->size_to_write ;

  nValues = gucr_valuesPerComponent(rs->hr);
  switch ( nValues ) {
  case 256:
    rle_tracker.bitDepth = 8;
    rle_tracker.monoSimpleShift = 8;
    rle_tracker.max_run_repeat = (1 << 8) - 1 ;
    rle_tracker.max_run_simple = (1 << 16) - 1 ;
    break;

  case 1024:
    rle_tracker.bitDepth = 10;
    rle_tracker.monoSimpleShift = 6;
    rle_tracker.max_run_repeat = (1 << 6) - 1 ;
    rle_tracker.max_run_simple = (1 << 16) - 1 ;
    break;

  case 4096:
    rle_tracker.bitDepth = 12;
    rle_tracker.monoSimpleShift = 0 ;
    rle_tracker.max_run_repeat = (1 << 6) - 1 ;
    rle_tracker.max_run_simple = (1 << 13) - 1 ;
    break;

  default:
    HQFAIL("Unexpected valuesPerComponent for RLE");
    return error_handler(UNREGISTERED) ;
  }

  rle_tracker.color_rle = (gucr_interleavingStyle(rs->hr) == GUCR_INTERLEAVINGSTYLE_PIXEL) ;
  rle_tracker.ci = COLORANTINDEX_UNKNOWN ;
  if ( !rle_tracker.color_rle ) {
    const GUCR_COLORANT *sole_colorant = gucr_colorantsStart(rs->hf);
    const GUCR_COLORANT_INFO *colorantInfo ;
    Bool res;

    HQASSERT(sole_colorant != NULL, "No colorant in mono RLE");
    res = gucr_colorantDescription(sole_colorant, &colorantInfo);
    HQASSERT(res, "Non-renderable channel in mono RLE");
    rle_tracker.ci = colorantInfo->colorantIndex;
  }
  rle_tracker.disabled = 0 ;
  rle_tracker.compositing = FALSE;
#ifdef BLIT_RLE_TRANSPARENCY
  rle_tracker.compositing =
    (rs->page->rle_flags & (RLE_NO_COMPOSITE|RLE_TRANSPARENCY))
    == RLE_TRANSPARENCY;
#endif

  rle_tracker.run_info.current_gts = NULL ;
  rle_tracker.run_info.current_listobject = NULL ;
  rle_tracker.run_info.current_listobject_needs_identifiers = FALSE ;
  rle_tracker.run_info.current_data = NULL ;
  rle_tracker.run_info.current_tags = 0 ;
  for ( i = 0; i < (1 << RLE_LEN_INFO_TYPE); ++i ) {
    rle_tracker.run_info.identifiers[i] = sepdata->run_info_ids[i].prev ;
  }

  rle_tracker.runAbort = RLE_GEN_OK;
  rle_tracker.constructingRunRepeat = 0;
  rle_tracker.run_repeat_state = NO_RUN_REPEAT ;
  rle_tracker.repeat_cache_total = rle_tracker.params->runCacheNumber * rle_tracker.params->runCacheSize ;

  rle_tracker.band_rh = rs->forms->retainedform.rh;
  rle_tracker.realHOff = rs->forms->retainedform.hoff;

  rle_tracker.transparencyState.map = rlesheet->map ;
  rle_tracker.transparencyState.alpha = RLE_ALPHA_MAX;
  rle_tracker.transparencyState.alphaIsShape = FALSE;
  rle_tracker.transparencyState.blendMode = 0;
  rle_tracker.transparencyState.mask.mask = NULL;
  bbox_clear(&rle_tracker.transparencyState.mask.area) ;
  rle_tracker.transparencyState.tranAttrib = NULL;

  rle_tracker.rlestates = task_resource_fix(TASK_RESOURCE_RLE_STATES,
                                            rs->cs.bandlimits.y1, NULL) ;
  HQASSERT(rle_tracker.rlestates != NULL, "Resource pool failure") ;

  /** \todo ajcd 2012-10-10: Check these are still OK. */
  rle_tracker.run_info.earliest_scanline_data_emitted_for_so_far =
    rle_tracker.run_info.scanline_after_end_of_band =
    rle_tracker.rlestates + rle_tracker.band_rh ;

  NAME_OBJECT(&rle_tracker, RLE_TRACKER_NAME) ;

  /* This loop below can in fact fail quite easily at low res (e.g., 72 dpi)
  because we may fail to allocate a block of (empty) rle data for each
  scan-line. This can be because a typical size for a block is 128 words and
  there can be a large number of scanlines in a band at low res. These two facts
  mean that we can end up needing more than the 128 kB or so that is available.
  If that happens, then proceed to render the rle line by line. */
  lineCount = rle_tracker.band_rh;
  width = rs->cs.bandlimits.x2 - rs->cs.bandlimits.x1 + 1 ;

  /* The state initialisation is split into two sections because block
     allocation checks the whole band for consistency (in an asserted build).
     Therefore each state is initialised before any blocks are allocated. */
  continued = (rs->cs.bandlimits.x1 != 0 || rs->page->rippedtodisk) ;
  repeat_mem = CAST_SIGNED_TO_SIZET(sepdata->prev_repeat_mem_hwm) ;
  repeat_id = CAST_SIGNED_TO_UINT16(sepdata->prev_repeat_id_hwm) ;
  for (i = 0, state = rle_tracker.rlestates; i < lineCount; i ++, state ++) {
    rleStateInit(&rle_tracker, rs->page, state,
                 repeat_mem, repeat_id, continued);
  }

#define return DO_NOT_return_goto_cleanup_INSTEAD!
  /* Allocate blocks for each state; this must be done after all the states have
     been initialised - see comment above. */
  for (i = 0, state = rle_tracker.rlestates; i < lineCount; i ++, state ++) {
    if ( !rleNewBlock(&rle_tracker, state) ) {
      HQASSERT(rle_tracker.runAbort != RLE_GEN_OK,
               "RLE block failed but not runAbort") ;
      goto cleanup ;
    }
  }

  /* Before calling the band render callback, install the RLE tracker as
     the surface handle. The blit functions will be able to access this
     through the render state's surface_handle field. */
  handle->band = &rle_tracker ;

  /* Actually do the band render callback */
  result = (*callback)(data) ;
#ifdef DEBUG_BUILD
  if ( (debug_rle & DEBUG_RLE_FORCE_SPLIT) != 0 && width == rs->page->page_w )
    rle_tracker.runAbort = RLE_GEN_RETRY ;
#endif
  if ( !result || rle_tracker.runAbort != RLE_GEN_OK )
    goto cleanup ;

  /* And terminate the lines. Note that we skip the RUN_END_OF_LINE if we're
     not the last X split of the line. */
  for ( i = 0, state = rle_tracker.rlestates ; i < lineCount ; ++i, ++state ) {
    if ( bandpass->current_pass == 0 ) {
      if ( !reserveWords(&rle_tracker, state, 1) ) {
        HQASSERT(rle_tracker.runAbort != RLE_GEN_OK,
                 "Failed to reserve RLE but not runAbort") ;
        goto cleanup ;
      }

      RLE_WRITE(state, RUN_END_OF_LINE);
    }

    /* Extract highest repeat cache ID and memory. */
    if ( state->repeat.cache_id_hwm > repeat_id )
      repeat_id = state->repeat.cache_id_hwm ;
    if ( state->repeat.cache_mem_hwm > repeat_mem )
      repeat_mem = state->repeat.cache_mem_hwm ;

    /* close off block */
    * (state->blockheader) = (RUN_HEADER | 0x100) |
                             (CAST_PTRDIFFT_TO_UINT32(state->currentword - state->blockheader + 1) << 18);
  }

  /* Update repeat ID and memory highwatermark for the next pass using atomic
     operations, in case multiple bands are updating at the same time. */
  {
    hq_atomic_counter_t new_id = repeat_id ;
    hq_atomic_counter_t new_mem = CAST_SIZET_TO_INT32(repeat_mem) ;
    Bool done ;

    do { /* Update next pass repeat ID high water mark atomically */
      hq_atomic_counter_t next_id = sepdata->next_repeat_id_hwm ;
      if ( new_id <= next_id )
        break ;
      HqAtomicCAS(&sepdata->next_repeat_id_hwm, next_id, new_id, done) ;
    } while ( !done ) ;

    do { /* Update next pass repeat memory high water mark atomically */
      hq_atomic_counter_t next_mem = sepdata->next_repeat_mem_hwm ;
      if ( new_mem <= next_mem )
        break ;
      HqAtomicCAS(&sepdata->next_repeat_mem_hwm, next_mem, new_mem, done) ;
    } while ( !done ) ;

    for ( i = 0; i < (1 << RLE_LEN_INFO_TYPE); ++i ) {
      new_id = rle_tracker.run_info.identifiers[i] ;
      do {/* Update next pass run info identifier low water mark atomically */
        hq_atomic_counter_t next_id = sepdata->run_info_ids[i].next ;
        if ( new_id <= next_id )
          break ;
        HqAtomicCAS(&sepdata->run_info_ids[i].next, next_id, new_id, done) ;
      } while ( !done ) ;
    }
  }

 cleanup:
  if ( rle_tracker.runAbort == RLE_GEN_RETRY ) {
    /* Signal retry with smaller band. */
    /** \todo ajcd 2012-10-24: We may want to choose a much smaller band, so
        that really hard jobs don't have to repeatedly sub-divide.
        Sub-dividing by 2 each time will take log_2(h) times longer to render
        in the limit, where each line has to be rendered separately (h is the
        band height). */
#ifdef DEBUG_BUILD
    if ( (debug_rle & DEBUG_RLE_NO_OVERFLOW) == 0 )
#endif
    {
      if ( lineCount == 1 ) {
#ifdef DEBUG_BUILD
        if ( (debug_rle & DEBUG_RLE_COMPLETE_LINES) == 0 )
#endif
        {
          /* We've already sub-divided down to one line. If we have more than
             one pixel width, split the band horizontally. */
          if ( width > 1 ) {
            bandpass->action = SURFACE_PASS_XSPLIT ;
            bandpass->current_pass = 1 ; /* Don't add RUN_END_OF_LINE. */
            bandpass->serialize = TRUE ;
          } /* else will limitcheck below. */
        }
      } else {
        bandpass->action = SURFACE_PASS_YSPLIT ;
        /* Remainder can be handled in parallel, unless compositing (because
           the backdrop blocks cannot be created in strips). We won't output
           it before the initial lines anyway. */
        if ( (rs->pass_region_types & RENDER_REGIONS_BACKDROP) != 0 )
          bandpass->serialize = TRUE ;
      }
    }

    /* Throw an error if we aborted but have no retry action to do. */
    if ( bandpass->action == SURFACE_PASS_DONE )
      result = error_handler(LIMITCHECK);
  } else if ( rle_tracker.runAbort == RLE_GEN_ERROR ) {
    result = FALSE;
  } else {
    /* The RLE generated OK, but the render callback may have failed, so
       use the result value. */
    HQASSERT(rle_tracker.runAbort == RLE_GEN_OK,
             "Invalid RLE generation state") ;
  }

  UNNAME_OBJECT(&rle_tracker) ;

#undef return
  return result;
}

/** Introduce a new list object to the RLE system; set RUN_INFO data as
    required. */
static void rleSetInfos(surface_band_t *tracker, LISTOBJECT *lobj)
{
  GSTAGSTRUCTUREOBJECT *gts ;

  tracker->run_info.earliest_scanline_data_emitted_for_so_far = tracker->run_info.scanline_after_end_of_band ;
  tracker->run_info.current_tags = 0; /* These get set when codes are set up */

  /** \todo ajcd 2008-10-10: Maybe shouldn't be used inside a vignette
      object? */
  if ( lobj != NULL &&
       lobj->opcode != RENDER_erase &&
       lobj->objectstate != NULL &&
       (gts = lobj->objectstate->gstagstructure) != NULL &&
       gts->dl_words > 0 ) {
    tracker->run_info.current_gts = gts;
    tracker->run_info.current_listobject = lobj;
    tracker->run_info.current_listobject_needs_identifiers = TRUE ;
    tracker->run_info.current_data = ((uint32 *)lobj) - gts->dl_words;
  } else {
    /* object not of interest (may need to cancel inheritance of tag
       data from a previous object that did have some). */
    tracker->run_info.current_gts = NULL ;
    tracker->run_info.current_listobject = NULL ;
    tracker->run_info.current_listobject_needs_identifiers = FALSE ;
    tracker->run_info.current_data = NULL ;
  }
}

/* ====================================================================== */

/* Color rle functions */

/** Add a short or long colorants record according to the current colorants
    state, using the bitvector supplied and the opcodes supplied. */
static Bool addColorantsRecord(surface_band_t *tracker, RLESTATE *state,
                               const blit_colormap_t *map,
                               blit_channel_state_t mask,
                               uint32 op_short, uint32 op_long,
                               Bool knockout)
{
  uint32 count, shift ;
  channel_index_t cindex ;
  uint32 indices[BLIT_MAX_CHANNELS] ;

  HQASSERT(BOOL_IS_VALID(knockout), "Knockout bit should be TRUE or FALSE") ;

  /* We need one word for a RUN_COLORANTS, or the header of a
  RUN_COLORANTS_LONG (in which case additional words will be checked for as
  needed). */
  if ( !reserveWords(tracker, state, 1) )
    return FALSE;

  HQASSERT(map->nchannels == state->colorants.nchannels,
           "Current colorant set out of sync with colormap") ;

  /* First scan and map the colorant indices, and determine if they will fit
     into a short record, building the op_short code into a record as we
     go. */
  count = 0 ;
  shift = RLE_SHIFT_COLORANTS_N ;
  for ( cindex = 0 ; cindex < map->nchannels ; ++cindex ) {
    if ( (state->colorants.channels[cindex] & mask) == mask &&
         map->channel[cindex].type == channel_is_color ) {
      COLORANTINDEX ci = map->channel[cindex].ci ;
      uint32 cn ;

      HQASSERT(ci == COLORANTINDEX_ALL || ci >= 0,
               "Invalid colorant index for a color channel") ;

      /* Why would we care if the colorant was unidentified? */
      (void)rleColorantMapGet(tracker->transparencyState.map, ci, &cn) ;

      indices[count] = cn ;

      if ( cn > (RLE_MASK_COLORANTS_N >> RLE_SHIFT_COLORANTS_N) ) {
        /* Indicate we cannot use the short version by making the short
           opcode invalid. */
        op_short = 0 ;
      } else if ( op_short != 0 ) {
        /* Could still be short version */
        op_short |= cn << shift ;
        shift -= RLE_SHIFTNEXT_COLORANTS ;
      }

      ++count ;
    }
  }

  if ( count <= 7 && op_short != 0 ) { /* Fits in short-form record */
    RLE_WRITE(state,
              op_short |
              (!knockout << RLE_SHIFT_COLORANTS_OVERPRINT) |
              (count << RLE_SHIFT_COLORANTS_COUNT)) ;
  } else { /* Long-record form required. */
    uint32 index ;

    HQASSERT(count > 0, "Too few colorant indices detected") ;
    HQASSERT(count <= (RLE_MASK_DECILE >> RLE_SHIFT_DECILE),
             "Too many colorant indices detected") ;
    RLE_WRITE(state,
              op_long |
              (!knockout << RLE_SHIFT_COLORANTS_OVERPRINT) |
              (indices[0] << (RLE_SHIFT_DECILE - RLE_SHIFTNEXT_DECILE)) |
              (count << RLE_SHIFT_DECILE));

    /* Do the remaining colorants in groups of up to three */
    for ( index = 1 ; index < count ; ++index ) {
      uint32 cn = indices[index] ;
      switch ( index % 3 ) {
      case 1: /* The first in each block of 3 */
        /* Reserve an extra word to avoid overflow asserts for subsequent
           colorants. */
        if ( !reserveWords(tracker, state, 2) )
          return FALSE;
        RLE_WRITE(state, cn << RLE_SHIFT_DECILE);
       break ;
      case 2: /* 2nd in each block of 3 */
        RLE_OVERFLOW_CHECK(state);
        *state->currentword |= cn << (RLE_SHIFT_DECILE - RLE_SHIFTNEXT_DECILE) ;
        break ;
      case 0: /* 3rd in each block of 3 */
        RLE_OVERFLOW_CHECK(state);
        *state->currentword |= cn ;
        break ;
      }
    }
  }

  return TRUE;
}


/** Output a RUN_COLORANTS[_LONG] record. */
static Bool addColorants(surface_band_t *tracker, RLESTATE *state,
                         const blit_colormap_t *map)
{
  return addColorantsRecord(tracker, state, map, blit_channel_present,
                            RUN_COLORANTS, RUN_COLORANTS_LONG,
                            FALSE /*Overprint bit set*/) ;
}

/** Output a RUN_PARTIAL_OVERPRINT[_LONG] record. */
static Bool addPartialOverprint(surface_band_t *tracker, RLESTATE * state,
                                const blit_colormap_t *map)
{
  return addColorantsRecord(tracker, state, map, blit_channel_maxblit,
                            RUN_PARTIAL_OVERPRINT, RUN_PARTIAL_OVERPRINT_LONG,
                            TRUE /*Overprint bit zero*/) ;
}

/** Determine if the colorants of the current render state are the same as
    those of the current color on this line. If not, we need to insert a new
    RUN_COLORANTS or RUN_COLORANTS_LONG record, and appropriate maxblit
    record too. */
static Bool addColorantsIfNeeded(surface_band_t *tracker, RLESTATE *state,
                                 const blit_color_t *color)
{
  Bool same = FALSE;
  Bool add_maxblits = FALSE ;
  Bool remove_maxblits = FALSE ;
  channel_index_t index ;
  const blit_colormap_t *map ;
  colorants_state_t *colorants ;
  Bool runPartialOverprint = tracker->params->runPartialOverprint;

  if ( !tracker->color_rle )
    return TRUE ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  colorants = &state->colorants ;
  /* There's either one colorant (fixed over the lifetime of the RLE state), or
     all colorants from the rasterstyle (from the current group), so it's enough
     to check the rasterstyle plus overprints and maxblits. */
  if ( colorants->rasterstyle_id == map->rasterstyle_id &&
       colorants->nchannels == map->nchannels &&
       colorants->ncolors == color->ncolors ) {
    same = TRUE ;

    /* So far, the colorants could be the same. Now test if the same color
       channels are present, and the maxblits are unchanged. */
    for ( index = 0 ; index < map->nchannels ; ++index ) {
      /* Are the colorant sets the same? */
      if ( map->channel[index].type != channel_is_color )
        continue;
      if ( (color->state[index] & blit_channel_present) !=
           (colorants->channels[index] & blit_channel_present) ) {
        same = FALSE ;
        break ; /* We have sufficient to complete the task now */
      }

      /* Difference of the old and new maxblits. */
      if ( runPartialOverprint )
        switch ( (color->state[index] & blit_channel_maxblit)
                 - (colorants->channels[index] & blit_channel_maxblit) ) {
        case blit_channel_maxblit: add_maxblits = TRUE; break;
        case 0: break;
        default: remove_maxblits = TRUE; break;
        }
    }
  }

  /* The RLE spec says that repeated RUN_PARTIAL_OVERPRINTs are interpreted
     as the union of the sets of colorants. If we want to reduce the maxblits,
     then we need to repeat the RUN_COLORANTS or RUN_COLORANTS_LONG to reset
     the maxblits. */
  if ( !same || remove_maxblits ) {
    rle_colorants(color, map, colorants) ;
    if ( !addColorants(tracker, state, map) )
      return FALSE ;

    /* We've now reset the maxblit status. The simplest way to indicate if
       we should repeat the RUN_PARTIAL_OVERPRINTS record is to indicate
       that we should add maxblits if there are any present. */
    add_maxblits = runPartialOverprint && color->nmaxblits > 0;
  }

  if ( add_maxblits ) {
    if ( same && !remove_maxblits ) /* Haven't already copied colorants */
      rle_colorants(color, map, colorants) ;
    /* Ideally, just list the added colorants, but this'll do for now. */
    if ( runPartialOverprint )
      if ( !addPartialOverprint(tracker, state, map) )
        return FALSE ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Return the first line state that is within the bandlimits within the render
    state. The first y position within the bandlimits is returned via the 'y'
    parameter, along with the total number of lines within the band limits. */
static RLESTATE *getRleStateWithinBandlimits(surface_band_t *tracker,
                                             const dbbox_t *bbox,
                                             dcoord *y, dcoord *totalLines)
{
  dcoord y1, y2, hoff;
  RLESTATE* lineState;

  HQASSERT(bbox != NULL && y != NULL && totalLines != NULL,
           "getRleStateWithinBandlimits - parameters cannot be NULL.");

  /* Even if within a pattern, or other nested group, we issue RUN_GROUP
     records for all of the base DL scanlines that might be affected by this
     group. If the particular group doesn't render anything within that
     range, we'll have RUN_GROUP_OPEN immediately followed by
     RUN_GROUP_CLOSE. The render state's bandlimits define the area of the
     base DL that might possibly be affected by this rendering operation. The
     y_sep_position isn't taken into account here, but that should be safe,
     because there is an error thrown if separation positioning is used with
     RLE transparency (they just don't make sense together). */
  y1 = bbox->y1;
  y2 = bbox->y2;

  /* Set y to the first line of this band - we may adjust this later. */
  hoff = tracker->realHOff;
  HQASSERT(y1 >= hoff && y1 <= y2 && y2 < hoff + tracker->band_rh,
           "getRleStateWithinBandlimits - invalid band limits.");

  /* Note the band limits are inclusive. */
  *y = y1;
  *totalLines = y2 - y1 + 1;
  lineState = tracker->rlestates + y1 - hoff ;

  return lineState;
}

/**
 * Open a new group.
 *
 * This method will set the global transparency state, and that for each line
 * in the band, to the defaults (which is the defined initial state for
 * objects within a group).
 */
static void rleGroupOpen(surface_band_t *tracker, const dbbox_t *bbox,
                         Bool knockout, Bool isolated,
                         DEVICESPACEID colorspaceId)
{
  int32 colorspace;
  dcoord totalLines, y;
  RLESTATE* lineState;

  /* Don't try to do anything if we've already aborted. */
  if (tracker->runAbort != RLE_GEN_OK)
    return;

  switch (colorspaceId) {
  default:
    HQFAIL("Invalid colorspaceID.");
    /* Fallthrough. */

  case DEVICESPACE_Gray:
    colorspace = 0;
    break;

  case DEVICESPACE_RGB:
    colorspace = 1;
    break;

  case DEVICESPACE_CMYK:
    colorspace = 2;
    break;
  }

  lineState = getRleStateWithinBandlimits(tracker, bbox, &y, &totalLines);

  HQASSERT(totalLines > 0, "No lines in band") ;
  do {
    /* Output the group's softmask if present. Add the transparency state and
       gstag data for the groups as a whole. */
    if ( !addTransparencyIfNeeded(tracker, lineState, y) ||
         !addInfosIfNeeded(tracker, lineState) )
      return;

    /* The group starts with default transparency state. */
    rleStateDefaultTransparency(lineState);

    if ( !reserveWords(tracker, lineState, 1) )
      return;

    RLE_WRITE(lineState,
              RUN_GROUP_OPEN | (isolated << 6) |
              (knockout << 7) | (colorspace << 8));

    /* Invalidate the colorants in the linestate, so the first object in the
       group will enumerate the colorants. */
    lineState->colorants.rasterstyle_id = 0;
    lineState->colorants.nchannels = lineState->colorants.ncolors =
      lineState->colorants.nmaxblits = 0 ;

    ++y ;
    ++lineState ;
  } while ( --totalLines > 0 ) ;

  return;
}

/**
 * Close the current group. This will add a RUN_GROUP_CLOSE record to every
 * line in the current band.
 *
 * \todo This method results in a large number of empty groups being produced
 * in the RLE, since the group contents may not cover the whole band. Perhaps
 * the bounds of the group could be used to limit this?
 */
static void rleGroupClose(surface_band_t *tracker, const dbbox_t *bbox)
{
  dcoord totalLines, y;
  RLESTATE* lineState;

  /* Don't try to do anything if we've already aborted. */
  if (tracker->runAbort != RLE_GEN_OK)
    return;

  lineState = getRleStateWithinBandlimits(tracker, bbox, &y, &totalLines);

  HQASSERT(totalLines > 0, "No lines in band") ;
  do {
    if ( !reserveWords(tracker, lineState, 1) )
      return;

    /* The 'currentword' member points to the last word with something in
    it (!), so we need to skip over it first. */
    RLE_WRITE(lineState, RUN_GROUP_CLOSE);

    ++lineState ;
  } while ( --totalLines > 0 ) ;

  return;
}

/** Convert the passed cce blend mode to an RLE blend mode value. */
static uint8 cceBlendModeToRle(uint8 cceMode)
{
  switch (cceMode) {
  default:
    HQFAIL("Unknown blend mode.");
    /* Fallthrough. */

  case CCEModeNormal:
    return 0;

  case CCEModeMultiply:
    return 1;

  case CCEModeScreen:
    return 2;

  case CCEModeOverlay:
    return 3;

  case CCEModeSoftLight:
    return 9;

  case CCEModeHardLight:
    return 8;

  case CCEModeColorDodge:
    return 6;

  case CCEModeColorBurn:
    return 7;

  case CCEModeDarken:
    return 4;

  case CCEModeLighten:
    return 5;

  case CCEModeDifference:
    return 10;

  case CCEModeExclusion:
    return 11;

  case CCEModeHue:
    return 12;

  case CCEModeSaturation:
    return 13;

  case CCEModeColor:
    return 14;

  case CCEModeLuminosity:
    return 15;
  }
}

/** Set the current transparency state.
*/
static void rleSetTransparency(surface_band_t *tracker,
                               const render_info_t *ri, TranAttrib* state)
{
  HQASSERT(ri != NULL, "No render info.");
  HQASSERT(state != NULL, "No transparency attribute");

  tracker->transparencyState.alpha = RLE_GET_ALPHA(state->alpha);
  tracker->transparencyState.alphaIsShape = state->alphaIsShape;
  tracker->transparencyState.blendMode = cceBlendModeToRle(state->blendMode);

  if (state->softMask == NULL || state->softMask->type == EmptySoftMask) {
    tracker->transparencyState.mask.mask = NULL;
  } else {
    /* Limit the mask area to export to the intersection of the mask area and
     * the band limits. */
    dbbox_t maskArea = groupSoftMaskArea(state->softMask->group);
    bbox_intersection(&ri->p_rs->cs.bandlimits, &maskArea,
                      &tracker->transparencyState.mask.area);
    tracker->transparencyState.mask.mask = state->softMask;
  }

  tracker->transparencyState.tranAttrib = state ;
}

/* ---------------------------------------------------------------------- */
/* Surface details for RLE. */

/** Render preparation function for toneblits packs current color. */
static surface_prepare_t render_prepare_rle(surface_handle_t handle,
                                            render_info_t *p_ri)
{
  blit_color_t *color ;
  TranAttrib *tranAttrib ;
  surface_band_t *tracker ;

  tracker = handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;
  HQASSERT(p_ri, "No render info") ;

  if ( tracker->runAbort != RLE_GEN_OK ) {
    if ( tracker->runAbort == RLE_GEN_ERROR )
      return SURFACE_PREPARE_FAIL ;
    return SURFACE_PREPARE_SKIP ;
  }

  tranAttrib = p_ri->lobj->objectstate->tranAttrib ;

  if ( tranAttrib != tracker->transparencyState.tranAttrib ) {
    TranAttrib tranDefault ;

    if ( tranAttrib == NULL ) {
      /* If no transparency attributes were supplied, use the default values */
      tranDefault.storeEntry.next = NULL ;
      tranDefault.blendMode = CCEModeNormal ;
      tranDefault.alphaIsShape = FALSE ;
      tranDefault.alpha = COLORVALUE_ONE ;
      tranDefault.softMask = NULL ;
      tranAttrib = &tranDefault ;
    }

    rleSetTransparency(tracker, p_ri, tranAttrib) ;
  }

  rleSetInfos(tracker, p_ri->lobj);

  color = p_ri->rb.color ;
  blit_color_quantise(color) ;
  blit_color_pack(color) ;

  return SURFACE_PREPARE_OK ;
}

/** RLE color packing doesn't use the bit sizes and offsets; it simply packs
    present colorants into successive shorts. */
static void blit_color_pack_rle(blit_color_t *color)
{
  const blit_colormap_t *map ;
  channel_index_t index, pindex ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  /* For RLE, forget about bit offsets, sizes, etc. Pack the quantised colors
     into the next available short. */
  for ( index = pindex = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_present) != 0 &&
         map->channel[index].type == channel_is_color ) {
      /* Channel is not missing, and is a real color rather than alpha or
         type, so we should extract and map it. */
      channel_output_t output = (channel_output_t)(color->quantised.qcv[index] * map->channel[index].pack_mul + map->channel[index].pack_add) ;
      color->packed.channels.shorts[pindex++] = output ;
    }
  }

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif
}

/** RLE doesn't need colors expanded, it doesn't copy rasters */
static void blit_color_expand_rle(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_packed, "Blit color is not packed") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_expanded), "Blit color is already expanded") ;
#endif

#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

/** RLE doesn't need overprint masks, it uses the state directly. */
static void blit_overprint_mask_rle(blit_packed_t *packed,
                                    const blit_color_t *color,
                                    blit_channel_state_t mask,
                                    blit_channel_state_t state)
{
  UNUSED_PARAM(blit_packed_t *, packed) ;
  UNUSED_PARAM(const blit_color_t *, color) ;
  UNUSED_PARAM(blit_channel_state_t, mask) ;
  UNUSED_PARAM(blit_channel_state_t, state) ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
}

static void rle_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  map->pack_quantised_color = blit_color_pack_rle ;
  map->expand_packed_color = blit_color_expand_rle ;
  map->overprint_mask = blit_overprint_mask_rle ;
}

/* ---------------------------------------------------------------------- */

#ifdef BLIT_RLE_COLOR
/** Add a color RUN_SIMPLE record. */
static Bool addColorSimple(surface_band_t *tracker, render_blit_t *rb,
                           RLESTATE *state,
                           dcoord start, dcoord totalLen, dcoord y)
{
  /* Don't try to do anything if we've already aborted. */
  if (tracker->runAbort != RLE_GEN_OK)
    return FALSE;

  if ( !addColorantsIfNeeded(tracker, state, rb->color) ||
       !addTransparencyIfNeeded(tracker, state, y) ||
       !addObjectDetailsIfNeeded(tracker, state, rb) ||
       !addPositionIfNeeded(tracker, state, start) )
    return FALSE;

  do { /* split runlen into pieces */
    dcoord runLen ;
    const unsigned short *pcolor = rb->color->packed.channels.shorts ;
    channel_index_t count = rb->color->ncolors ;
    uint32 lengthBits = 16 ;

    INLINE_MIN32(runLen, totalLen, tracker->max_run_simple) ;

    if ( !reserveWords(tracker, state, 1) )
      return FALSE ;

    RLE_WRITE(state, RUN_SIMPLE) ;

    /* The RUN_SIMPLE opcode takes 6 bits, leaving 26 bits left in the current
       word. */
    state->bits.reserved = 6 ;
    state->bits.remaining = 26 ;
    state->bits.total = 32 ; /* We've used one word for the header. */

    if ( tracker->bitDepth == 12 ) {
      uint32 longForm = 0 ;
      lengthBits = 9 ;

      /* If possible we only use 9 bits for the run-length as this allows us
         to fits four color values in two words, otherwise we use 13 bits.
         The first bit in the stream selects between these two
         representations. */
      if ( runLen >= (1 << 9) ) {
        lengthBits = 13 ;
        longForm = 1 ;
      }
      if ( !rleWriteBits(tracker, state, longForm, 1) )
        return FALSE ;
    }

    if ( !rleWriteBits(tracker, state, runLen, lengthBits) )
      return FALSE ;

    do {
      /* If at the start of a 10-bit color word, write a new repeat count
         indicator. We'll patch these up later if necessary. */
      if ( (state->bits.remaining & 31) == 0 && tracker->bitDepth == 10 ) {
        if ( !rleWriteBits(tracker, state, 0, 2) )
          return FALSE ;
      }

      if ( !rleWriteBits(tracker, state, *pcolor++, tracker->bitDepth) )
        return FALSE ;
    } while ( --count ) ;

    state->currentposition += runLen ;
    totalLen -= runLen ;
  } while ( totalLen > 0 ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */

static void bitfillrlecolor(render_blit_t *rb, dcoord y, dcoord xsp, dcoord xep)
{
  const blit_color_t *color = rb->color ;
  RLESTATE *state = GET_RLE_STATE(rb, y);
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  xep = xep - xsp + 1;
  xsp += rb->x_sep_position;

  if ( !run_repeat_optimise(tracker, state, xsp, xep, 1,
                            color->ncolors, color->packed.channels.shorts) ) {
    (void)addColorSimple(tracker, rb, state, xsp, xep, y) ;
  }
}

static void bitcliprlecolor(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  bitclipn(rb, y, xs, xe, bitfillrlecolor);
}

/* ---------------------------------------------------------------------- */
static void blkfillrlecolor(render_blit_t *rb,
                            dcoord ys, dcoord ye, dcoord xsp, dcoord xep)
{
  const blit_color_t *color = rb->color ;
  RLESTATE *state = GET_RLE_STATE(rb, ys) ;
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  xep = xep - xsp + 1;
  xsp += rb->x_sep_position;
  ye = ye - ys + 1 ; /* Number of lines in block */

  if ( !run_repeat_optimise(tracker, state, xsp, xep, ye,
                            color->ncolors, color->packed.channels.shorts) ) {
    do {
      if ( !addColorSimple(tracker, rb, state, xsp, xep, ys) )
        return ;
      ++ys ; ++state ;
    } while ( --ye > 0 ) ;
  }
}

static void blkcliprlecolor(render_blit_t *rb, dcoord ys, dcoord ye,
                            dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfillrlecolor) ;
}

/* ---------------------------------------------------------------------- */
/* Erase each new raster. */
static void areahalfrlecolor(render_blit_t *rb, FORM *formptr)
{
  render_blit_t rb_copy = *rb ;
  channel_index_t index ;

  HQASSERT(rb_copy.color->valid & blit_color_packed,
           "Packed color not set for area") ;
  HQASSERT(rb_copy.color->ncolors > 0, "No colors for area fill") ;
  HQASSERT(formptr == &rb->p_ri->p_rs->forms->retainedform,
           "areahalfrlecolor: different form");

  /* The RLE definition says that lines are assumed to be pre-filled with 0's
     in all channels (any channels not present in the current color are
     assumed zero). We have to explicitly test zero, rather than the blit
     quantise state, because the RLE spec is written in terms of the output
     color. */
  /* Make sure the loop doesn't test the type channel. */
  HQASSERT(rb_copy.color->map->type_index >= rb_copy.color->ncolors,
           "Type channel in unexpected place");
  for ( index = 0 ;; ) {
    if ( rb_copy.color->packed.channels.shorts[index] != 0 )
      break ;
    if ( ++index == rb_copy.color->ncolors )
      return ;
  }

  /* otherwise fill the whole band area  */

  rb_copy.x_sep_position = 0;
  blkfillrlecolor(&rb_copy, formptr->hoff, formptr->hoff + formptr->rh - 1,
                  0, formptr->w - 1);
}

/* ---------------------------------------------------------------------- */
extern void gspan_n(render_blit_t *, dcoord, dcoord, dcoord) ;
extern void gspan_noise_n(render_blit_t *, dcoord, dcoord, dcoord) ;

static void rle_gspan_n(render_blit_t *rb, register dcoord y,
                        register dcoord xs, register dcoord xe)
{
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  if ( rb->p_painting_pattern == NULL && !tracker->disabled ) {
    RLESTATE *state = GET_RLE_STATE(rb, y);

    if ( !addColorantsIfNeeded(tracker, state, rb->color) ||
         !addTransparencyIfNeeded(tracker, state, y) ||
         !addObjectDetailsIfNeeded(tracker, state, rb) )
      return ;

    tracker->run_repeat_state = DO_RUN_REPEAT_IF_EASY ;

    gspan_n(rb, y, xs, xe) ;

    if ( state->repeat.header )
      run_repeat_close(tracker, state, 1) ;

    tracker->run_repeat_state = NO_RUN_REPEAT ;
  } else {
    gspan_n(rb, y, xs, xe) ;
  }
}

static void rle_gspan_noise_n(render_blit_t *rb, register dcoord y,
                              register dcoord xs, register dcoord xe)
{
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  if ( rb->p_painting_pattern == NULL && !tracker->disabled ) {
    RLESTATE *state = GET_RLE_STATE(rb, y);

    if ( !addColorantsIfNeeded(tracker, state, rb->color) ||
         !addTransparencyIfNeeded(tracker, state, y) ||
         !addObjectDetailsIfNeeded(tracker, state, rb) )
      return ;

    tracker->run_repeat_state = DO_RUN_REPEAT_IF_EASY ;

    gspan_noise_n(rb, y, xs, xe) ;

    if ( state->repeat.header )
      run_repeat_close(tracker, state, 1) ;

    tracker->run_repeat_state = NO_RUN_REPEAT ;
  } else {
    gspan_noise_n(rb, y, xs, xe) ;
  }
}

/* ---------------------------------------------------------------------- */

/** Alternative ValuesPerComponent for rlecolor surface. */
const static sw_datum rlecolor_values[] = {
  SW_DATUM_INTEGER(1024), SW_DATUM_INTEGER(4096),
} ;

/** Pagedevice match for RLE color contone surface. */
const static sw_datum rlecolor_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"), SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_PIXEL),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(TRUE),
  SW_DATUM_STRING("ValuesPerComponent"),
    SW_DATUM_ARRAY(&rlecolor_values[0], SW_DATA_ARRAY_LENGTH(rlecolor_values)),
} ;

/** The RLE contone color surface set. */
static surface_set_t rlecolor_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&rlecolor_dict[0],
                                 SW_DATA_DICT_LENGTH(rlecolor_dict))) ;

#ifdef BLIT_RLE_TRANSPARENCY
/* ---------------------------------------------------------------------- */
/** Pagedevice match for RLE transparent RunLengthDetails. */
const static sw_datum rletrans_detail[] = {
  SW_DATUM_STRING("RunTransparency"), SW_DATUM_BOOLEAN(TRUE),
} ;

/** Pagedevice match for RLE transparent 10-bit color contone surface. */
const static sw_datum rletrans10_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"), SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_PIXEL),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(TRUE),
  SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(1024),
  SW_DATUM_STRING("RunLengthDetails"),
    SW_DATUM_DICT(&rletrans_detail[0], SW_DATA_DICT_LENGTH(rletrans_detail)),
} ;

/** The RLE 10-bit transparency contone color surface set. */
static surface_set_t rletrans10_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&rletrans10_dict[0],
                                 SW_DATA_DICT_LENGTH(rletrans10_dict))) ;

/** Pagedevice match for RLE transparent 12-bit color contone surface. */
const static sw_datum rletrans12_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"), SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_PIXEL),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(TRUE),
  SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(4096),
  SW_DATUM_STRING("RunLengthDetails"),
    SW_DATUM_DICT(&rletrans_detail[0], SW_DATA_DICT_LENGTH(rletrans_detail)),
} ;

/** The RLE 12-bit transparency contone color surface set. */
static surface_set_t rletrans12_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&rletrans12_dict[0],
                                 SW_DATA_DICT_LENGTH(rletrans12_dict))) ;

/* ---------------------------------------------------------------------- */
/* Transparent RLE trampolines */
static void bitfillrletrans(render_blit_t *rb, dcoord y, dcoord xsp, dcoord xep)
{
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  if ( !tracker->disabled )
    bitfillrlecolor(rb, y, xsp, xep) ;

  if ( tracker->compositing )
    rlecolor_backdrop->base.baseblits[BLT_CLP_NONE].spanfn(rb, y, xsp, xep) ;
}

static void bitcliprletrans(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  bitclipn(rb, y, xs, xe, bitfillrletrans);
}

static void blkfillrletrans(render_blit_t *rb,
                            dcoord ys, dcoord ye, dcoord xsp, dcoord xep)
{
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  if ( !tracker->disabled )
    blkfillrlecolor(rb, ys, ye, xsp, xep) ;

  if ( tracker->compositing )
    rlecolor_backdrop->base.baseblits[BLT_CLP_NONE].blockfn(rb, ys, ye, xsp, xep) ;
}

static void blkcliprletrans(render_blit_t *rb, dcoord ys, dcoord ye,
                            dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfillrletrans) ;
}

/* Erase each new raster. */
static void areahalfrletrans(render_blit_t *rb, FORM *formptr)
{
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  if ( !tracker->disabled )
    areahalfrlecolor(rb, formptr) ;

  if ( tracker->compositing )
    rlecolor_backdrop->base.areafill(rb, formptr) ;
}


static surface_prepare_t rletrans_prepare(surface_handle_t handle,
                                          render_info_t *p_ri)
{
  surface_prepare_t result = render_prepare_rle(handle, p_ri) ;
  if ( result == SURFACE_PREPARE_OK ) {
    surface_band_t *tracker = handle.band ;
    VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;
    if ( tracker->compositing )
      result = rlecolor_backdrop->base.prepare(handle, p_ri) ;
  }
  return result ;
}


static Bool rletrans_regionCreate(surface_handle_t handle,
                                  struct CompositeContext *bd_context,
                                  DEVICESPACEID colorspace,
                                  Bool is_soft_mask,
                                  Bool is_pattern,
                                  Bool is_knockout,
                                  surface_backdrop_t group_handle,
                                  surface_backdrop_t initial_handle,
                                  surface_backdrop_t target_handle,
                                  TranAttrib *to_target,
                                  const dbbox_t *bbox,
                                  render_region_callback_fn *callback,
                                  render_region_callback_t *data)
{
  Bool result = TRUE;
  Bool old_compositing = TRUE;
  RleTransparencyState old_trans ;
  LISTOBJECT *old_lobj ;
  surface_band_t *tracker = handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  old_trans = tracker->transparencyState ;
  old_lobj = tracker->run_info.current_listobject ;

  /* The region colorant map will be restored as part of old_trans after this
     region is created. */
  tracker->transparencyState.map = region_rle_colorant_map(data) ;

  if ( is_soft_mask ) {
    /* Disable transparent RLE if it is active so the current group (and
       any of its children) will not be sent to the RLE stream. */
    tracker->disabled++;
    /* Make sure this is composited. */
    old_compositing = tracker->compositing; tracker->compositing = TRUE;
  } else if ( !tracker->disabled ) {
    rleGroupOpen(tracker, bbox, is_knockout, initial_handle == NULL, colorspace);
  }

  if ( tracker->compositing )
    result = rlecolor_backdrop->region_create(handle, bd_context, colorspace,
                                              is_soft_mask, is_pattern, is_knockout,
                                              group_handle, initial_handle,
                                              target_handle, to_target,
                                              bbox, callback, data);
  else
    result = (*callback)(data);

  if ( is_soft_mask ) {
    tracker->compositing = old_compositing;
    tracker->disabled-- ;
  } else if ( !tracker->disabled ) {
    rleGroupClose(tracker, bbox);
  }

  /* Restore the object tracker state to the same as it was at the start
     of the region, in case another region is rendered in the set. */
  tracker->transparencyState = old_trans ;
  rleSetInfos(tracker, old_lobj) ;

  return result ;
}

/** RLE color packing doesn't use the bit sizes and offsets; it simply packs
    present colorants into successive shorts. */
static void blit_color_pack_rletrans(blit_color_t *color,
                                     channel_output_t maxvalue)
{
  const blit_colormap_t *map ;
  channel_index_t index, pindex ;
  uint32 rounding ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  /* Transparent RLE is defined as subtractive, so invert the backdrop's
     additive channels and apply rounding in one step. The calculation
     required to flip and round is:

     (cv_max - qcv[i] + 0.5) * maxvalue / cv_max

     Distributing maxvalue into the parenthesis gives:

     (cv_max * maxvalue + 0.5 * maxvalue - qcv[i] * maxvalue) / cv_max
  */
  HQASSERT(COLORVALUE_MAX * maxvalue <= MAXUINT32 - (maxvalue >> 1),
           "Transparent RLE rounding overflow") ;
  rounding = COLORVALUE_MAX * maxvalue + (maxvalue >> 1) ;

  /* For RLE, forget about bit offsets, sizes, etc. Pack the quantised colors
     into the next available short. Transparency RLE is quantised to what the
     transparency surface needs (COLORVALUE). */
  for ( index = pindex = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_present) != 0 &&
         map->channel[index].type == channel_is_color ) {
      /* Channel is not missing, and is a real color rather than alpha or
         type, so we should extract and map it. */
      channel_output_t output = (channel_output_t)((rounding - color->quantised.qcv[index] * maxvalue) / COLORVALUE_MAX) ;
      color->packed.channels.shorts[pindex++] = output ;
    }
  }

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif
}

static void blit_color_pack_rletrans10(blit_color_t *color)
{
  blit_color_pack_rletrans(color, 1023) ;
}

static void rletrans10_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  map->pack_quantised_color = blit_color_pack_rletrans10 ;
  map->expand_packed_color = blit_color_expand_rle ;
  map->overprint_mask = blit_overprint_mask_rle ;
}

static void blit_color_pack_rletrans12(blit_color_t *color)
{
  blit_color_pack_rletrans(color, 4095) ;
}

static void rletrans12_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  map->pack_quantised_color = blit_color_pack_rletrans12 ;
  map->expand_packed_color = blit_color_expand_rle ;
  map->overprint_mask = blit_overprint_mask_rle ;
}

#endif /* BLIT_RLE_TRANSPARENCY */

/* ---------------------------------------------------------------------- */

static void init_rlecolor(void)
{
  unsigned int i, j ;

  /* The contone RLE output surface. */
  static surface_t rlecolor = SURFACE_INIT ;

  {
    static const surface_t *indexed[N_SURFACE_TYPES] ;

    /* No tone specialisations for RLE blits. */
    rlecolor.baseblits[BLT_CLP_NONE].spanfn =
      rlecolor.baseblits[BLT_CLP_RECT].spanfn = bitfillrlecolor ;
    rlecolor.baseblits[BLT_CLP_COMPLEX].spanfn = bitcliprlecolor ;

    rlecolor.baseblits[BLT_CLP_NONE].blockfn =
      rlecolor.baseblits[BLT_CLP_RECT].blockfn = blkfillrlecolor ;
    rlecolor.baseblits[BLT_CLP_COMPLEX].blockfn = blkcliprlecolor ;

    rlecolor.baseblits[BLT_CLP_NONE].charfn =
      rlecolor.baseblits[BLT_CLP_RECT].charfn =
      rlecolor.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

    rlecolor.baseblits[BLT_CLP_NONE].imagefn =
      rlecolor.baseblits[BLT_CLP_RECT].imagefn =
      rlecolor.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltrle ;

    /* No object map on the side blits for backdrop */

    /* Ignore maxblits */
    for ( j = 0 ; j < BLT_MAX_N ; ++j ) {
      for ( i = 0 ; i < BLT_CLP_N ; ++i ) {
        rlecolor.maxblits[j][i].spanfn = next_span ;
        rlecolor.maxblits[j][i].blockfn = next_block ;
        rlecolor.maxblits[j][i].charfn = next_char ;
        rlecolor.maxblits[j][i].imagefn = next_imgblt ;
      }
    }

    /* No ROP blits for rlecolor */

    init_pcl_pattern_blit(&rlecolor) ;

    /* Builtins for intersect, pattern, and gouraud */
    surface_intersect_builtin(&rlecolor) ;
    surface_pattern_builtin(&rlecolor) ;

    surface_gouraud_builtin_tone_multi(&rlecolor) ;
    rlecolor.gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = rle_gspan_n ;
    rlecolor.gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = rle_gspan_noise_n ;

    rlecolor.areafill = areahalfrlecolor ;
    rlecolor.prepare = render_prepare_rle ;
    rlecolor.blit_colormap_optimise = rle_blitmap_optimise ;

    rlecolor.n_rollover = 3 ;
    rlecolor.screened = FALSE ;
    rlecolor.image_depth = 12 ;
    rlecolor.render_order = SURFACE_ORDER_DEVICELR|SURFACE_ORDER_DEVICETB ;

    builtin_clip_N_surface(&rlecolor, indexed) ;

    /* The surface we've just completed is part of a set implementing RLE
       halftone output. Add it and all of the associated surfaces to the
       surface array for this set. */
    rlecolor_set.indexed = indexed ;
    rlecolor_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

    indexed[SURFACE_OUTPUT] = &rlecolor ;
    surface_set_trap_builtin(&rlecolor_set, indexed);
    surface_set_transparency_builtin(&rlecolor_set, &rlecolor, indexed) ;

    rlecolor_set.select = &rle_select ;
    rlecolor_set.deselect = &rle_deselect ;
    rlecolor_set.introduce_dl = &rle_introduce_dl ;
    rlecolor_set.retire_dl = &rle_retire_dl ;
    rlecolor_set.sheet_begin = &rle_sheet_begin ;
    rlecolor_set.sheet_end = &rle_sheet_end ;
    rlecolor_set.band_localiser = &rle_band_render ;
    rlecolor_set.packing_unit_bits = 32 ;

    /* Now that we've filled in the RLE surface description, hook it up so
       that it can be found. */
    surface_set_register(&rlecolor_set) ;
  }

  /**************************************************************************/

#ifdef BLIT_RLE_TRANSPARENCY
  /* We need separate surface sets for 10 and 12-bit transparent RLE because
     the blit colour pack method needs to differ, and there is no reasonable
     way of passing the output depth into it. It's called with a backdrop
     colormap setup, so the colormap is the same for both cases. */
  rlecolor_backdrop = surface_find_transparency(&rlecolor_set) ;
  HQASSERT(rlecolor_backdrop, "No transparency surface") ;

  {
    /* The 10-bit transparent RLE transparency surface with trampolining. */
    static transparency_surface_t rletrans10 = TRANSPARENCY_SURFACE_INIT ;
    static const surface_t *indexed[N_SURFACE_TYPES] ;

    /* Copy the backdrop surface, and modify for the RLE transparency
       overrides. */
    rletrans10 = *rlecolor_backdrop ;
    rletrans10.base.baseblits[BLT_CLP_NONE].spanfn =
      rletrans10.base.baseblits[BLT_CLP_RECT].spanfn = bitfillrletrans ;
    rletrans10.base.baseblits[BLT_CLP_COMPLEX].spanfn = bitcliprletrans ;

    rletrans10.base.baseblits[BLT_CLP_NONE].blockfn =
      rletrans10.base.baseblits[BLT_CLP_RECT].blockfn = blkfillrletrans ;
    rletrans10.base.baseblits[BLT_CLP_COMPLEX].blockfn = blkcliprletrans ;

    rletrans10.base.baseblits[BLT_CLP_NONE].imagefn =
      rletrans10.base.baseblits[BLT_CLP_RECT].imagefn =
      rletrans10.base.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltrle ;

    rletrans10.base.gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = rle_gspan_n ;
    rletrans10.base.gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = rle_gspan_noise_n ;

    rletrans10.base.areafill = areahalfrletrans ;
    rletrans10.base.prepare = rletrans_prepare ;
    /* backdrop blit optimisation uses no-op functions because it only uses
       quantised color values, so we can replace it with the RLE color
       pack/expand instead. */
    rletrans10.base.blit_colormap_optimise = rletrans10_blitmap_optimise ;
    rletrans10.base.render_order = SURFACE_ORDER_DEVICELR|SURFACE_ORDER_DEVICETB ;

    rletrans10.region_create = rletrans_regionCreate ;

    rletrans10_set.indexed = indexed ;
    rletrans10_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

    indexed[SURFACE_OUTPUT] = &rlecolor ;
    indexed[SURFACE_TRANSPARENCY] = &rletrans10.base ;

    surface_set_trap_builtin(&rletrans10_set, indexed);
    rletrans10_set.select = &rle_select ;
    rletrans10_set.deselect = &rle_deselect ;
    rletrans10_set.introduce_dl = &rle_introduce_dl ;
    rletrans10_set.retire_dl = &rle_retire_dl ;
    rletrans10_set.sheet_begin = &rle_sheet_begin ;
    rletrans10_set.sheet_end = &rle_sheet_end ;
    rletrans10_set.band_localiser = &rle_band_render ;
    rletrans10_set.packing_unit_bits = 32 ;

    /* Now that we've filled in the RLE surface description, hook it up so
       that it can be found. */
    surface_set_register(&rletrans10_set) ;
  }
  {
    /* The 12-bit transparent RLE transparency surface with trampolining. */
    static transparency_surface_t rletrans12 = TRANSPARENCY_SURFACE_INIT ;
    static const surface_t *indexed[N_SURFACE_TYPES] ;

    /* Copy the backdrop surface, and modify for the RLE transparency
       overrides. */
    rletrans12 = *rlecolor_backdrop ;
    rletrans12.base.baseblits[BLT_CLP_NONE].spanfn =
      rletrans12.base.baseblits[BLT_CLP_RECT].spanfn = bitfillrletrans ;
    rletrans12.base.baseblits[BLT_CLP_COMPLEX].spanfn = bitcliprletrans ;

    rletrans12.base.baseblits[BLT_CLP_NONE].blockfn =
      rletrans12.base.baseblits[BLT_CLP_RECT].blockfn = blkfillrletrans ;
    rletrans12.base.baseblits[BLT_CLP_COMPLEX].blockfn = blkcliprletrans ;

    rletrans12.base.baseblits[BLT_CLP_NONE].imagefn =
      rletrans12.base.baseblits[BLT_CLP_RECT].imagefn =
      rletrans12.base.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltrle ;

    rletrans12.base.gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = rle_gspan_n ;
    rletrans12.base.gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = rle_gspan_noise_n ;

    rletrans12.base.areafill = areahalfrletrans ;
    rletrans12.base.prepare = rletrans_prepare ;
    /* backdrop blit optimisation uses no-op functions because it only uses
       quantised color values, so we can replace it with the RLE color
       pack/expand instead. */
    rletrans12.base.blit_colormap_optimise = rletrans12_blitmap_optimise ;
    rletrans12.base.render_order = SURFACE_ORDER_DEVICELR|SURFACE_ORDER_DEVICETB ;

    rletrans12.region_create = rletrans_regionCreate ;

    rletrans12_set.indexed = indexed ;
    rletrans12_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

    indexed[SURFACE_OUTPUT] = &rlecolor ;
    indexed[SURFACE_TRANSPARENCY] = &rletrans12.base ;

    surface_set_trap_builtin(&rletrans12_set, indexed);
    rletrans12_set.select = &rle_select ;
    rletrans12_set.deselect = &rle_deselect ;
    rletrans12_set.introduce_dl = &rle_introduce_dl ;
    rletrans12_set.retire_dl = &rle_retire_dl ;
    rletrans12_set.sheet_begin = &rle_sheet_begin ;
    rletrans12_set.sheet_end = &rle_sheet_end ;
    rletrans12_set.band_localiser = &rle_band_render ;
    rletrans12_set.packing_unit_bits = 32 ;

    /* Now that we've filled in the RLE surface description, hook it up so
       that it can be found. */
    surface_set_register(&rletrans12_set) ;
  }
#endif /* BLIT_RLE_TRANSPARENCY */
}
#endif /* BLIT_RLE_COLOR */

/* ---------------------------------------------------------------------- */

#ifdef BLIT_RLE_MONO
/**
 * Add a simple monochrome run.
 */
static void addMonoSimple(surface_band_t *tracker, render_blit_t *rb,
                          RLESTATE *state,
                          dcoord start, dcoord totalLen, uint32 color)
{
  HQASSERT(tracker->bitDepth == 8 || tracker->bitDepth == 10,
           "Invalid bit depth.");

  /* Don't try to do anything if we've already aborted. */
  if (tracker->runAbort != RLE_GEN_OK)
    return ;

  if ( !addObjectDetailsIfNeeded(tracker, state, rb) ||
       !addPositionIfNeeded(tracker, state, start) )
    return;

  HQASSERT(totalLen > 0, "No run for mono RUN_SIMPLE") ;
  do {
    dcoord runLen ;

    INLINE_MIN32(runLen, totalLen, 0xffff) ;

    if ( !reserveWords(tracker, state, 1) )
      return ;

    RLE_WRITE(state, RUN_SIMPLE | (runLen << 16) | color);

    state->currentposition += runLen ;
    totalLen -= runLen;
  } while ( totalLen > 0 ) ;
}

/* ---------------------------------------------------------------------- */

static void bitfillrle(render_blit_t *rb , dcoord y , dcoord xsp , dcoord xep)
{
  RLESTATE * state;
  blit_color_t *color = rb->color ;
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;

  state = GET_RLE_STATE(rb, y);
  xep = xep - xsp + 1;
  xsp += rb->x_sep_position;

  if ( !run_repeat_optimise(tracker, state, xsp, xep, 1,
                            color->ncolors, color->packed.channels.shorts) ) {
    uint32 rle_color = color->packed.channels.shorts[0] << tracker->monoSimpleShift;
    addMonoSimple(tracker, rb, state, xsp, xep, rle_color);
  }
}

static void bitcliprle(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  bitclipn(rb, y, xs, xe, bitfillrle);
}

/* ---------------------------------------------------------------------- */
static void blkfillrle(render_blit_t *rb, dcoord ys, dcoord ye,
                       dcoord xsp, dcoord xep)
{
  RLESTATE * state;
  blit_color_t *color = rb->color ;
  surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
  VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;

  state = GET_RLE_STATE(rb, ys);

  xep = xep - xsp + 1;
  xsp += rb->x_sep_position;
  ye = ye - ys + 1 ; /* Number of lines block. */

  if ( !run_repeat_optimise(tracker, state, xsp, xep, ye,
                            color->ncolors, color->packed.channels.shorts) ) {
    uint32 rle_color = color->packed.channels.shorts[0] << tracker->monoSimpleShift ;

    do {
      addMonoSimple(tracker, rb, state, xsp, xep, rle_color);
      ++state ;
    } while ( --ye > 0 ) ;
  }
}

static void blkcliprle(render_blit_t *rb, dcoord ys, dcoord ye,
                       dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfillrle) ;
}

/* ---------------------------------------------------------------------- */
/* Erase new mono RLE bitmap. */
static void areahalfrle(render_blit_t *rb, FORM *formptr)
{
  render_blit_t rb_copy = *rb;

  HQASSERT(rb_copy.color->valid & blit_color_packed,
           "Packed color not set for area") ;
  HQASSERT(rb_copy.color->ncolors == 1,
           "Should have one color for area") ;
  HQASSERT(formptr == &rb->p_ri->p_rs->forms->retainedform,
           "areahalfrle: different form");

  /* The RLE definition says that lines are assumed to be pre-filled with 0's
     in all channels (any channels not present in the current color are
     assumed zero. We have to explicitly test zero, rather than the blit
     quantise state, because the RLE spec is written in terms of the output
     color. */
  if ( rb_copy.color->packed.channels.shorts[0] == 0 )
    return ;

  rb_copy.x_sep_position = 0;
  blkfillrle(&rb_copy, formptr->hoff, formptr->hoff + formptr->rh - 1,
             0, formptr->w - 1);
}

/* ---------------------------------------------------------------------- */
extern void gspan_1(render_blit_t *, dcoord, dcoord, dcoord) ;
extern void gspan_noise_1(render_blit_t *, dcoord, dcoord, dcoord) ;

static void rle_gspan_1(render_blit_t *rb, register dcoord y,
                        register dcoord xs, register dcoord xe)
{
  if ( rb->p_painting_pattern == NULL ) {
    RLESTATE *state = GET_RLE_STATE(rb, y);
    surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
    VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

    if ( !addObjectDetailsIfNeeded(tracker, state, rb) )
      return ;

    tracker->run_repeat_state = DO_RUN_REPEAT_IF_EASY ;

    gspan_1(rb, y, xs, xe) ;

    if ( state->repeat.header )
      run_repeat_close(tracker, state, 1) ;

    tracker->run_repeat_state = NO_RUN_REPEAT ;
  } else {
    gspan_1(rb, y, xs, xe) ;
  }
}

static void rle_gspan_noise_1(render_blit_t *rb, register dcoord y,
                              register dcoord xs, register dcoord xe)
{
  if ( rb->p_painting_pattern == NULL ) {
    RLESTATE *state = GET_RLE_STATE(rb, y);
    surface_band_t *tracker = rb->p_ri->p_rs->surface_handle.band ;
    VERIFY_OBJECT(tracker, RLE_TRACKER_NAME) ;

    if ( !addObjectDetailsIfNeeded(tracker, state, rb) )
      return ;

    tracker->run_repeat_state = DO_RUN_REPEAT_IF_EASY ;

    gspan_noise_1(rb, y, xs, xe) ;

    if ( state->repeat.header )
      run_repeat_close(tracker, state, 1) ;

    tracker->run_repeat_state = NO_RUN_REPEAT ;
  } else {
    gspan_noise_1(rb, y, xs, xe) ;
  }
}

/* ---------------------------------------------------------------------- */

/** Alternative ValuesPerComponent for monochrome RLE surface. */
const static sw_datum rlemono_values[] = {
  SW_DATUM_INTEGER(256), SW_DATUM_INTEGER(1024),
} ;

/** Pagedevice match for RLE monochrome contone surface. */
const static sw_datum rlemono_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"), SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(TRUE),
  SW_DATUM_STRING("ValuesPerComponent"),
    SW_DATUM_ARRAY(&rlemono_values[0], SW_DATA_ARRAY_LENGTH(rlemono_values)),
} ;

/** The RLE monochrome surface set. */
static surface_set_t rlemono_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&rlemono_dict[0],
                                 SW_DATA_DICT_LENGTH(rlemono_dict))) ;

static void init_rlemono(void)
{
  unsigned int i, j ;

  /** The mono RLE contone output surface. */
  static surface_t rlemono = SURFACE_INIT ;
  static const surface_t *indexed[N_SURFACE_TYPES] ;

  /* No tone specialisations for RLE blits. */
  rlemono.baseblits[BLT_CLP_NONE].spanfn =
    rlemono.baseblits[BLT_CLP_RECT].spanfn = bitfillrle ;
  rlemono.baseblits[BLT_CLP_COMPLEX].spanfn = bitcliprle ;

  rlemono.baseblits[BLT_CLP_NONE].blockfn =
    rlemono.baseblits[BLT_CLP_RECT].blockfn = blkfillrle ;
  rlemono.baseblits[BLT_CLP_COMPLEX].blockfn = blkcliprle ;

  rlemono.baseblits[BLT_CLP_NONE].charfn =
    rlemono.baseblits[BLT_CLP_RECT].charfn =
    rlemono.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  rlemono.baseblits[BLT_CLP_NONE].imagefn =
    rlemono.baseblits[BLT_CLP_RECT].imagefn =
    rlemono.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltrle ;

  /* No object map on the side blits for backdrop */

  /* Ignore maxblits */
  for ( j = 0 ; j < BLT_MAX_N ; ++j ) {
    for ( i = 0 ; i < BLT_CLP_N ; ++i ) {
      rlemono.maxblits[j][i].spanfn = next_span ;
      rlemono.maxblits[j][i].blockfn = next_block ;
      rlemono.maxblits[j][i].charfn = next_char ;
      rlemono.maxblits[j][i].imagefn = next_imgblt ;
    }
  }

  /* No ROP blits for rlemono */

  init_pcl_pattern_blit(&rlemono) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&rlemono) ;
  surface_pattern_builtin(&rlemono) ;

  surface_gouraud_builtin_tone(&rlemono) ;
  rlemono.gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = rle_gspan_1 ;
  rlemono.gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = rle_gspan_noise_1 ;

  rlemono.areafill = areahalfrle ;
  rlemono.prepare = render_prepare_rle ;
  rlemono.blit_colormap_optimise = rle_blitmap_optimise ;

  rlemono.n_rollover = 3 ;
  rlemono.screened = FALSE ;
  rlemono.image_depth = 10 ;
  rlemono.render_order = SURFACE_ORDER_DEVICELR|SURFACE_ORDER_DEVICETB ;

  /* Note that we use the spanlist clip method for RLE, because we want to
     push whole spans through the clip. */
  builtin_clip_N_surface(&rlemono, indexed) ;

  /* The surface we've just completed is part of a set implementing RLE
     mono contone output. Add it and all of the associated surfaces to
     the surface array for this set. */
  rlemono_set.indexed = indexed ;
  rlemono_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

  indexed[SURFACE_OUTPUT] = &rlemono ;
  surface_set_trap_builtin(&rlemono_set, indexed);
  surface_set_transparency_builtin(&rlemono_set, &rlemono, indexed) ;

  rlemono_set.select = &rle_select ;
  rlemono_set.deselect = &rle_deselect ;
  rlemono_set.introduce_dl = &rle_introduce_dl ;
  rlemono_set.retire_dl = &rle_retire_dl ;
  rlemono_set.sheet_begin = &rle_sheet_begin ;
  rlemono_set.sheet_end = &rle_sheet_end ;
  rlemono_set.band_localiser = &rle_band_render ;
  rlemono_set.packing_unit_bits = 32 ;

  /* Now that we've filled in the RLE surface description, hook it up so
     that it can be found. */
  surface_set_register(&rlemono_set) ;
}
#endif /* BLIT_RLE_MONO */

static void init_C_globals_rleblt(void)
{
  resource_source_t source_init = {0} ;

  /* Asserts about static properties of RLE. */
  HQASSERT(0 == RLE_EMPTY_OBJECT &&
           SW_PGB_USER_OBJECT == RLE_USER_OBJECT &&
           SW_PGB_NAMEDCOLOR_OBJECT == RLE_NAMEDCOLOR_OBJECT &&
           SW_PGB_BLACK_OBJECT == RLE_BLACK_OBJECT &&
           SW_PGB_LW_OBJECT == RLE_LW_OBJECT &&
           SW_PGB_TEXT_OBJECT == RLE_TEXT_OBJECT &&
           SW_PGB_VIGNETTE_OBJECT == RLE_VIGNETTE_OBJECT &&
           SW_PGB_IMAGE_OBJECT == RLE_IMAGE_OBJECT &&
           SW_PGB_COMPOSITED_OBJECT == RLE_COMPOSITED_OBJECT,
           "RLE flags do not match pixel label flags") ;

  mm_pool_rle = NULL ;

  rle_resource           = source_init ;
  rle_resource.refcount  = 1 ;
  rle_resource.make_pool = rle_resource_make ;
  rle_resource.mm_pool   = rle_resource_pool ;
  NAME_OBJECT(&rle_resource, RESOURCE_SOURCE_NAME) ;

#ifdef BLIT_RLE_TRANSPARENCY
  rlecolor_backdrop = NULL ;
#endif

#ifdef ASSERT_BUILD
  rle_inst_allocated = 0 ;
  rle_page_allocated = 0 ;
  rle_sheet_allocated = 0 ;
#endif
}

static Bool rleblt_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  if ( mm_pool_create(&mm_pool_rle,
                      RLE_POOL_TYPE, MM_SEGMENT_SIZE,
                      (size_t)sizeof(RLESTATE)*64, (size_t)8) != MM_SUCCESS ) {
    HQFAIL("Pool creation failed") ;
    return FAILURE(FALSE) ;
  }

  rle_resource.data = mm_pool_rle ;

  if ( !resource_source_low_mem_register(&rle_resource) ) {
    mm_pool_destroy(mm_pool_rle) ;
    return FALSE ;
  }

#ifdef DEBUG_BUILD
  register_ripvar(NAME_debug_rle, OINTEGER, &debug_rle);
#endif

  return TRUE ;
}

static Bool rleblt_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  /* Do these in swstart, because the trapping surface isn't initialised
     until swinit is done. */
#ifdef BLIT_RLE_MONO
  init_rlemono() ;
#endif
#ifdef BLIT_RLE_COLOR
  init_rlecolor() ;
#endif

  return TRUE ;
}

static void rleblt_finish(void)
{
  resource_source_low_mem_deregister(&rle_resource) ;
  mm_pool_destroy(mm_pool_rle) ;

  HQASSERT(rle_resource.refcount == 1, "Incorrect reference count") ;

  UNNAME_OBJECT(&rle_resource) ;

  HQASSERT(rle_inst_allocated == 0,
           "Didn't clean up RLE surface instances correctly") ;
  HQASSERT(rle_page_allocated == 0,
           "Didn't clean up RLE surface page data correctly") ;
  HQASSERT(rle_sheet_allocated == 0,
           "Didn't clean up RLE surface sheet data correctly") ;
}

void rleblt_C_globals(core_init_fns *fns)
{
  init_C_globals_rleblt() ;

  fns->swinit = rleblt_swinit ;
  fns->swstart = rleblt_swstart ;
  fns->finish = rleblt_finish ;
}

/*
Log stripped */
