/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gs_cache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color cache - stores and retrieves dl_colors
 *
 * ColorCache bits.
 *
 * The colorcache is actually a collection of caches, one per class of
 * colorchain.  There is a top level hash table used to locate the cache for a
 * particular chain class, and this gives a pointer to the 'real' cache to map
 * from input colorvalues to dl colors.  Different chains with the same hash
 * values are simply stored as a sequential list.
 *
 * The colorchain classes are identified by matching against a list consisting
 * of the input colorspace of the chain head, and the types of each link, along
 * with any ids specific (and private) to that link type.
 *
 * Rather than having to scan the whole hash table when we want to traverse all
 * the caches for reset and purge operations, each cache has two next pointers.
 * One indicates the next in the sequential list for matching hash keys, the
 * other simply points to any old other cache (currently in reverse allocation
 * order, as this is the simplest to implement), simply to provide an easy way
 * to walk all the caches.
 *
 * Each cache has a hashtable which is keyed by input colorvalues, leading to a
 * sequential list that is matched against longhand.
 * To save the overhead of memory allocating calls for adding new entries, we
 * allocate a block of memory (called a datatable) and use this to store new
 * cache entries, until it's full, at which point another block can be
 * allocated.  The downside of this method is that you have to free an entire
 * cache at once, but I don't believe that to be a problem.
 */

#include "core.h"
#include "gs_cachepriv.h"
#include "hqmemcpy.h"           /* HqMemCpy */
#include "lowmem.h"             /* low_mem_offer_t */
#include "monitor.h"            /* monitorf */
#include "swerrors.h"           /* error_handler */
#include "dlstate.h"            /* DL_STATE */
#include "gscheadpriv.h"        /* GS_CHAINinfo */
#include "render.h"             /* backend_color_state */
#include "gschead.h"            /* GSC_BLACK_TYPE */


/* Define this for tracing info about cache effectiveness
#define TRACE_CACHE
*/

/* For the length of the list of COC_ENTRY in a hash position in a cache */
typedef uint8 HASH_DEPTH;

/* Number of slots in top level table */
#define COC_HEADHASH_SIZE (256)

/* Number of caches on a single hash key in the top level hash table after
   which we start looking to re-use exising caches in the same list */
#define COC_HASHDEPTH_REUSE_LIMIT (5)

/*
 * Size of one data block to alloc for the cache data.
 * There may be many blocks in one cache
 *
 * COC datatable allocs are one of the biggest allocations during
 * PartialPaint. Reducing the size of the COC datatable has no measurable
 * performance hit for basic jobs and helps cut PP memory use.
 */
static uint32 coc_datatable_size()
{
  if ( low_mem_configuration() )
    return 512;
  else
    return 8192;
}

/* The maximum length of a list hanging off a hash position in a color cache */
#define COC_MAX_HASH_DEPTH (5)

/*
 * Number of slots in each chains hash table
 *
 * Lots of memory being used by COC cache as hash size is so big.
 * Allow the size to be reduced. A size of 101 is more that enough for
 * most basic jobs, but some go a little slower.  So leave the size at
 * 1201 while we are optimising performance.
 */
static int32 coc_hashtable_size()
{
  if ( low_mem_configuration() )
    return 1201;
  else
    return 2048;
}

/* Color cache top-level structure */

struct COC_STATE {
  /* A hashed array of caches. A list of color caches hangs of each hash position */
  COC_HEAD *gColorCache[COC_HEADHASH_SIZE];

  /* The total number of caches */
  uint32 gCacheCount;

  /* The number of inactive caches, i.e. not associated with a color chain. */
  uint32 gPurgedHeads;

  /* An id given to the next new cache created. Every cache has a different id. */
  uint32 gNextGenerationNumber;

#ifdef TRACE_CACHE
  /* For providing details of the effectiveness of the color cache. All these
   * values are reset at the end of each page */

  /* Gives a histogram of the positions in their respective lists that the set
   * of looked up colors were found. An effective cache would find the majority
   * colors in the first few positions to avoid the expense of traversing a long
   * list to no effect. */
  uint32 gNumFoundAtDepth[COC_MAX_HASH_DEPTH];

  /* The maximum depth at which any color was found in any list. */
  HASH_DEPTH gMaxDepthFound;

  /* The total number of color lookup requests made in all caches. */
  uint32 gTotalLookups;
#endif
};


typedef uint8 COC_STYLE;

/* Determine the items that are cached. They are used in COC_STYLE values */
enum {
  /* Store dl_color */
  COC_USE_DL_COLOR = 3,

  /* Store input color values to the final link in the chain */
  COC_USE_FINAL_CLINK
};


/* This is where the COC_HEAD structures go.  The hash and data tables are
   allocated in the mm_pool_coc */
#define mm_pool_head mm_pool_color

#define DATATABLE_MAX_ITEMS(_entsize) \
((int32)((coc_datatable_size() - sizeof(COC_DATATABLE)) / (_entsize)))

#define DATATABLE_MAX_INDEX(_entsize) (DATATABLE_MAX_ITEMS(_entsize) - 1)

#define DATATABLE_MAX_OFFSET(_entsize) \
     (DATATABLE_MAX_INDEX(_entsize) * (_entsize))


/* Memory allocation safety and protection routines:
   mm_low_handler can call coc_purge(FALSE), so allocations from the colorcache
   need to make sure they are safe from purging, whilst allowing other bits of
   the cache to be clobbered */

#define COC_ASSERT_ALLOC_SAFE(_cache)                                   \
     HQASSERT((_cache)->refCnt > 0 && !((_cache)->hashtable),           \
              "Colorcache allocation not safe");

#define COC_BEGIN_PROTECTED_ALLOC(_cache)                               \
  { COC_ENTRY **_ht_save_ = (_cache)->hashtable;                        \
    (_cache)->hashtable = NULL;                                         \
    cocState->gPurgedHeads++;

#define COC_END_PROTECTED_ALLOC(_cache)                                 \
    (_cache)->hashtable = _ht_save_;                                    \
    --cocState->gPurgedHeads;                                           \
  }

/* The data special to the normal color cache.
 * The data has been marked with I or O to indicate the data used as input to
 * a color lookup, and those that are returned by the lookup.
 * It's been necessary to munge 2 fields together to avoid spilling into another
 * word (not good because there are so many of these things in existence).
 */
typedef struct COC_ENTRY_DLC {
  dl_color_t dl_color;            /* O */
  uint8 spflags;                  /* O */
  uint8 blackType;
#ifdef DOC_HOW_blackType_IS_MUNGED
  uint8 lookupBlackType : 3;      /* I */
  uint8 insertBlackType : 3;      /* O */
#endif
  COLORVALUE opacity;             /* I */
} COC_ENTRY_DLC;

#define BT_LOOKUP_BITS               (3)
#define BT_LOOKUP_MASK               ((1 << BT_LOOKUP_BITS) - 1)
#define LOOKUP_BLACK_TYPE(blackType) ((blackType) & BT_LOOKUP_MASK)
#define INSERT_BLACK_TYPE(blackType) ((blackType) >> BT_LOOKUP_BITS)

typedef struct COC_ENTRY {
  struct COC_ENTRY *pnext;

  /* Effectively, there is this union of structures. C makes it difficult to
   * implement this neatly without consuming excessive unnecessary space in the
   * structures. So they are implemented manually.
   */
#ifdef IMPLEMENT_MANUALLY
  union {
    /* Matches input values with a dl_color */
    struct {
      COC_ENTRY_DLC dlc;
      USERVALUE oColors[m];  /* m is the input space dimension */
    } dlc;

    /* Matches input values with the input values to the final chain link */
    struct {
      USERVALUE iColors[m];  /* m is the input space dimension */
      USERVALUE oColors[n];  /* n is the dimension of final color chain link */
    } finalLink;
  } u;
#endif
} COC_ENTRY;

typedef struct COC_DATATABLE {
  struct COC_DATATABLE *pnext;
  int32 entry_size;
  int32 offset;
  COC_ENTRY *data;
} COC_DATATABLE;


struct COC_HEAD {
  /* The next cache in the list with the same hash key. */
  COC_HEAD *pnext;

  /* The number of color chains associated with this cache */
  cc_counter_t refCnt;

  /* A unique id for this cache */
  uint32 generationNumber;

  /* Image LUTs for the set of CLIDs in pChainClass */
  struct imlut_t *imlut;

  /* The flavour of color cache, determines the kind of output values. */
  COC_STYLE style;

  /* The number of input color values for all entries in this cache. */
  int32 n_inComps;

  /* The number of output color values for all entries in this cache. */
  int32 n_outComps;

  /* The number of cache hits for this cache. */
  int32 chits;

  /* The number of entries present in the cache. */
  int32 population;

  /* The number of lookups made against the cache */
  int32 clookups;

  /* The first datatable containing COC_ENTRY values. There may be a list of
   * tables. The hashtable lists are interleaved within this table. */
  COC_DATATABLE *datatable;

  /* An array of cache entry lists. One list for each hash position */
  COC_ENTRY **hashtable;

  /* The length of the list hanging off each hashtable position, and the maximum
   * length of all lists in this cache. Used to limit the length of each list and
   * when to recycle COC_ENTRY values to avoid lists getting too long. */
  HASH_DEPTH *hashDepth;
  HASH_DEPTH maxHashDepth;

  /* The hash position of this color cache in cocState->gColorCache. */
  uintptr_t hashKey;

  /* The CLID list used to identify this cache. */
  int16 slotCount;
  int16 linkCount;
  CLID *pChainClass;

  /* The page and color cache to which this cache head belongs */
  DL_STATE *page;
  COC_STATE *cocState;
};

static void coc_clear(COC_HEAD *cache, Bool release_dlcolors);

static Bool coc_create_tables(COC_HEAD *cache);

#ifdef TRACE_CACHE
#define COCTRACEF(x) monitorf x;
#else
#define COCTRACEF(x) EMPTY_STATEMENT();
#endif


/* ---------------------------------------------------------------------- */
Bool coc_start(COC_STATE **cocStateRef)
{
  COC_STATE *cocState, init = {0};

  HQASSERT(*cocStateRef == NULL, "cocState already exists");

  cocState = mm_alloc(mm_pool_color, sizeof(COC_STATE), MM_ALLOC_CLASS_COC);
  if ( cocState == NULL )
    return error_handler(VMERROR);

  *cocState = init;

  *cocStateRef = cocState;
  return TRUE;
}

void coc_finish(COC_STATE **cocStateRef)
{
  if ( *cocStateRef != NULL ) {
    COC_STATE *cocState = *cocStateRef;

    /* Reset the ColorCache to free any non- dl pool memory. Don't need to
       release dl_colors as these are freed as part of the dl pool. */
    coc_reset(cocState, FALSE);

    mm_free(mm_pool_color, cocState, sizeof(COC_STATE));
    *cocStateRef = NULL;
  }
}

/* ---------------------------------------------------------------------- */
/* Reset the entire colorcache.
   If a cache entry is still referenced by colorchains, we keep the COC_HEAD so
   the chain isn't pointing at thin air.  This seems to me to be neater and
   more efficient than nulling out all the pCache fields for all the chains in
   all the gstates.
   */
void coc_reset(COC_STATE *cocState, Bool release_dlcolors)
{
  uint32 count = 0;
  uint32 werePurged = 0;
  uint32 hashKey;
#ifdef TRACE_CACHE
  uint32 finalCacheCount = cocState->gCacheCount;
#endif

  HQASSERT(cocState->gPurgedHeads <= cocState->gCacheCount,
           "coc_re: purged/count mismatch");

  if ( cocState->gCacheCount > 0 ) {

    for (hashKey = 0; hashKey < COC_HEADHASH_SIZE; hashKey++) {
      COC_HEAD **prev = &cocState->gColorCache[hashKey];
      COC_HEAD *pCache = *prev;
      while ( pCache != NULL ) {
        HQASSERT(pCache->refCnt >= 0,"coc_re: -ve ref count");
        HQASSERT(pCache->hashKey < COC_HEADHASH_SIZE,"coc_re: dubious key");

        if ( pCache->hashtable != NULL )
          coc_clear(pCache, release_dlcolors);
        else
          werePurged++;

        if ( !release_dlcolors )
          /* Implies the page has finished, and can't continue referring
             to image LUTs because they about to be freed.  Otherwise we
             must be partial-painting and can still allow matches. */
          pCache->imlut = NULL;

        if ( pCache->refCnt == 0 ) {
          /* No chains refer to the cache, so we can clobber it. */
          *prev = pCache->pnext;
          --cocState->gCacheCount;
          mm_free(mm_pool_head, pCache,
                  sizeof(COC_HEAD) + pCache->slotCount * sizeof(CLID));
        } else { /* We're keeping this one */
          prev = &pCache->pnext;
          count++;
        }
        pCache = *prev;
      }
    }

    HQASSERT(cocState->gCacheCount == count, "Inconsistent gCacheCount");
    HQASSERT(cocState->gPurgedHeads == werePurged, "Inconsistent gPurgedHeads");
  }

  if ( count > 0 )
    COCTRACEF(((uint8*)"  : Kept: %d color caches due to refcount\n", count));

  cocState->gCacheCount = cocState->gPurgedHeads = count;
#ifdef TRACE_CACHE
  {
    uint32 i;
    uint32 totalHits = 0;

    for (i = 0; i <= cocState->gMaxDepthFound; i++)
      totalHits += cocState->gNumFoundAtDepth[i];

    if (totalHits > 0)
      COCTRACEF(((uint8*)"Num caches %d, Total lookups %d, Total hits %d, Max depth found %d\n",
                finalCacheCount, cocState->gTotalLookups, totalHits, cocState->gMaxDepthFound));

    for (i = 0; i <= cocState->gMaxDepthFound; i++) {
      if (cocState->gNumFoundAtDepth[i] > 0)
        COCTRACEF(((uint8*)"Depth %d: Num found %d\n", i, cocState->gNumFoundAtDepth[i]));
    }

    for (i = 0; i <= cocState->gMaxDepthFound; i++)
      cocState->gNumFoundAtDepth[i] = 0;
    for (; i < COC_MAX_HASH_DEPTH; i++)
      HQASSERT(cocState->gNumFoundAtDepth[i] == 0, "Expected zero depth found");
    cocState->gMaxDepthFound = 0;
  }

  cocState->gTotalLookups = 0;
#endif
}

#ifdef ASSERT_BUILD
static void assert_hashDepth(COC_HEAD *cache)
{
  uint32 maxHashDepth = 0;
  int32 i;

  if (cache == NULL)
    return;

  if (cache->hashtable != NULL) {
    for ( i = 0; i < coc_hashtable_size() ; i++ ) {
      COC_ENTRY *ccentry = cache->hashtable[i];
      uint32 count = 0;
      while (ccentry != NULL) {
        ++count;
        ccentry = ccentry->pnext;
      }
      HQASSERT(cache->hashDepth[i] == count, "Inconsistent hashDepth");

      if (count > maxHashDepth)
        maxHashDepth = count;
    }
  }

  HQASSERT(cache->maxHashDepth == maxHashDepth, "Inconsistent maxHashDepth");
}
#else
#define assert_hashDepth(x)
#endif


#ifdef TRACE_CACHE
/* Optional debug code to show how well the hashing is going.
   output is a list of length:count pairs, indicating that there are count
   entries in the cache with a list of length elements hanging off them.
   */
void coc_trace_cache_population(COC_HEAD *cache)
{
  int32 i;

  /* Build an array indicating the length of each hash key */
  if ( cache->population > 1 ) {
    assert_hashDepth(cache);

    /* Now keep passing across the array, counting the number of hits at a given
       depth, and remembering the next highest depth for the next pass */
    {
      int32 seen = 0;
      int32 current = 0;

      while ( seen < cache->population ) {
        int32 next = MAXINT32;
        int32 count = 0;

        for ( i = 0; i < coc_hashtable_size() ; ++i ) {
          int32 this = cache->hashDepth[i];
          if ( this == current ) {
            ++count;
          } else if ( this > current && this < next ) {
            next = this;
          }
        }
        COCTRACEF(((uint8*)"    %4d: %4d\n", current, count));

        seen += (count * current);
        current = next;
      }
    }
  }
}

/* Debug code to return number of datatables allocated */
int32 coc_trace_cache_data_depth(COC_HEAD *cache)
{
  int32 i = 0;
  COC_DATATABLE *dt = cache->datatable;

  while ( dt ) {
    ++i;
    dt = dt->pnext;
  }
  return i;
}

/* Trace the layout pattern of the top level chain cache */
void coc_trace_headhash(COC_STATE *cocState)
{
  int32 i;
  int32 hashDepth[COC_HEADHASH_SIZE];

  /* Build an array indicating the length of each hash key */
  for ( i = 0; i < COC_HEADHASH_SIZE ; i++ ) {
    COC_HEAD *chead = cocState->gColorCache[i];
    int32 count = 0;
    while ( chead ) {
      ++count;
      chead = chead->pnext;
    }
    hashDepth[i] = count;
  }

  COCTRACEF(((uint8*)"Top level hashtable: %d entries\n", cocState->gCacheCount));

  /* Now keep passing across the array, counting the number of hits at a given
     depth, and remembering the next highest depth for the next pass */
  {
    uint32 seen = 0;
    int32 current = 0;

    while ( seen < cocState->gCacheCount ) {
      int32 next = MAXINT32;
      int32 count = 0;

      for ( i = 0; i < COC_HEADHASH_SIZE ; ++i ) {
        int32 this = hashDepth[i];
        if ( this == current ) {
          ++count;
        } else if ( this > current && this < next ) {
          next = this;
        }
      }
      COCTRACEF(((uint8*)"    %4d: %4d\n", current, count));

      seen += (count * current);
      current = next;
    }
  }
}

/* Debug code to print out the CLIDs of a cache.
   Potentially useful eg when looking at what's prodding the free_candidate
   code in head_create */
static void coc_show_clids(CLID *chainClass, int32 num_slots, int32 num_links,
                           uint32 key)
{
  int32 i = 0;
  int32 j = 2;
  int32 link;

  COCTRACEF(((uint8*)"CLID: (%3d %2d %2d) %2d %2d", key, num_links, num_slots,
           chainClass[0], chainClass[1]));

  for ( link = 0; link < num_links; ++link ) {
    int32 idcount = (int32) chainClass[j + 1];
    COCTRACEF(((uint8*)" | %2d %2d", chainClass[j], idcount));
    j += 2;
    for ( i = 0; i < idcount ; ++i )
      COCTRACEF(((uint8*)" %2d", chainClass[ j++ ]));
  }

  if ( j < num_slots ) {
    COCTRACEF(((uint8*)" ||", chainClass[ j++ ]));
    while ( j < num_slots )
      COCTRACEF(((uint8*)" %2d", chainClass[ j++ ]));
  }

  COCTRACEF(((uint8*)"\n"));
}
#else
#define coc_trace_cache_population(cache)
#define coc_trace_cache_data_depth(cache)
#define coc_trace_headhash(cocState)
#define coc_trace_cache_population(cache)
#endif

/* For each flavour of color cache, return the required pointers to color values
 * in the color chain */
static inline void getChainInputPtr(GS_CHAINinfo *chain,
                                    USERVALUE    **chain_iCols)
{
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(chain != NULL, "chain NULL");
  HQASSERT(chain_iCols != NULL, "chain_iCols NULL");

  chainContext = chain->context;
  HQASSERT(chainContext != NULL && chainContext->pnext != NULL, "chain NULL");
  HQASSERT(chainContext->pCache != NULL, "pCache NULL");
  HQASSERT(chainContext->pCache->style == COC_USE_DL_COLOR ||
           chainContext->pCache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");

  *chain_iCols = chain->iColorValues;
}

static inline void getChainOutputPtr_fl(GS_CHAINinfo  *chain,
                                        USERVALUE     **chain_oCols)
{
  CLINK *pLink;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(chain != NULL, "chain NULL");
  HQASSERT(chain_oCols != NULL, "chain_oCols NULL");

  chainContext = chain->context;
  HQASSERT(chainContext != NULL && chainContext->pnext != NULL, "chain NULL");
  HQASSERT(chainContext->pCache != NULL, "pCache NULL");
  HQASSERT(chainContext->pCache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");

  pLink = chainContext->pnext;
  while (pLink->pnext != NULL)
    pLink = pLink->pnext;

  *chain_oCols = pLink->iColorValues;
}

/* For each flavour of color cache, return the required pointers to cache
 * entry values */
static inline void getCacheInputPtr(COC_HEAD *cache,
                                    COC_ENTRY *ccentry,
                                    USERVALUE **cache_iCols)
{
  COC_ENTRY_DLC *cache_dlc;

  HQASSERT(cache != NULL, "cache NULL");
  HQASSERT(ccentry != NULL, "ccentry NULL");
  HQASSERT(cache_iCols != NULL, "cache_iCols NULL");
  HQASSERT(cache->style == COC_USE_DL_COLOR ||
           cache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");

  switch (cache->style) {
  case COC_USE_DL_COLOR:
    cache_dlc = (COC_ENTRY_DLC *)(ccentry + 1);
    *cache_iCols = (USERVALUE *)((cache_dlc) + 1);
    break;
  case COC_USE_FINAL_CLINK:
    *cache_iCols = (USERVALUE *)(ccentry + 1);
    break;
  default:
    HQFAIL("Bad cache style");
    *cache_iCols = NULL;
    break;
  }
}

static inline void getCacheOutputPtr_dl(COC_HEAD *cache,
                                        COC_ENTRY *ccentry,
                                        COC_ENTRY_DLC **cache_dlc)
{
  UNUSED_PARAM(COC_HEAD *, cache);

  HQASSERT(cache != NULL, "cache NULL");
  HQASSERT(ccentry != NULL, "ccentry NULL");
  HQASSERT(cache_dlc != NULL, "cache_dlc NULL");
  HQASSERT(cache->style == COC_USE_DL_COLOR,
           "Unexpected color cache style");

  *cache_dlc = (COC_ENTRY_DLC *)(ccentry + 1);
}

static inline void getCacheOutputPtr_fl(COC_HEAD *cache,
                                        COC_ENTRY *ccentry,
                                        USERVALUE **cache_oCols)
{
  USERVALUE *cache_iCols;

  HQASSERT(cache != NULL, "cache NULL");
  HQASSERT(ccentry != NULL, "ccentry NULL");
  HQASSERT(cache_oCols != NULL, "cache_oCols NULL");
  HQASSERT(cache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");

  cache_iCols = (USERVALUE *)(ccentry + 1);
  *cache_oCols = cache_iCols + cache->n_inComps;
}

/* Reset an individual cache (ie one associated with a particular class of
   chain) by deleting all the cache.
  release_dlcolors:
    if true: call dlc_release() on each dl_color_t in the cache, otherwise
    don't bother (because we know that the dl pool is about to be dropped on
    the floor anyway, so there's no point).
  NOTE: This doesn't free the COC_HEAD structure, only the other allocated bits
    hanging off it.
*/
static void coc_clear(COC_HEAD *cache,
                      Bool release_dlcolors)
{
  COC_DATATABLE *dtable;
  int32 i;

  HQASSERT(cache != NULL, "No cache given to coc_clr");
  HQASSERT(cache->style == COC_USE_DL_COLOR ||
           cache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");

  COCTRACEF(((uint8*)"  Refs: %d Population: %d, Lookups: %d,  Hits: %d, Style: %d,  Datatables: %d\n",
             cache->refCnt, cache->population, cache->clookups, cache->chits, cache->style,
             coc_trace_cache_data_depth(cache)));

  coc_trace_cache_population(cache);

  assert_hashDepth(cache);

  /* Free all the dl colors first */
  if ( release_dlcolors && cache->hashtable != NULL ) {
    switch (cache->style) {
    case COC_USE_DL_COLOR:
      for (i = coc_hashtable_size() - 1; i >= 0; i--) {
        COC_ENTRY *ccentry = cache->hashtable[i];

        while (ccentry != NULL) {
          COC_ENTRY_DLC *cache_dlc;

          getCacheOutputPtr_dl(cache, ccentry, &cache_dlc);

          dlc_release(cache->page->dlc_context, &cache_dlc->dl_color);

          ccentry = ccentry->pnext;
        }
      }
      break;
    case COC_USE_FINAL_CLINK:
      break;
    }
  }

  cache->chits = 0;
  cache->population = 0;
  cache->clookups = 0;

  dtable = cache->datatable;
  while ( dtable != NULL ) {
    COC_DATATABLE *nextdtable = dtable->pnext;
    mm_free(mm_pool_coc, dtable, coc_datatable_size());
    dtable = nextdtable;
  }
  if ( cache->hashtable != NULL )
    mm_free(mm_pool_coc, cache->hashtable,
            sizeof(COC_ENTRY*) * coc_hashtable_size());
  if ( cache->hashDepth != NULL )
    mm_free(mm_pool_coc, cache->hashDepth,
            sizeof(HASH_DEPTH) * coc_hashtable_size());

  cache->datatable = NULL;
  cache->hashtable = NULL;
  cache->hashDepth = NULL;
  cache->maxHashDepth = 0;

  assert_hashDepth(cache);
}


/* Low memory purge routine.
   If the killall flag is true, then clear the whole cache, otherwise thow out
   the 'least used things' whatever that means
   */
#define COC_MIN_REPRIEVE_LEVEL (5.0)
static Bool coc_purge(COC_STATE *cocState)
{
  uint32 count = 0;
  uint32 purged = cocState->gPurgedHeads;
  uint32 werePurged = 0;
  uint32 freed = 0;
  uint32 hashKey;

  HQASSERT(purged <= cocState->gCacheCount,
           "coc_pu: purged/count mismatch");

  /* Pass over the colorcache, chucking out some stuff.
     NOTE: This routine can be called by mm_low_handler.  The allocation calls
     are concious of this, and decide when/how to protect themselves, based on
     the use of pCache->hashtable and pCache->refCnt in the 2 if statements
     below.  If this routine changes, you must check the protection logic for
     the calls to coc_create_tables and coc_datatable_create.
   */
  for ( hashKey = 0 ; hashKey < COC_HEADHASH_SIZE ; hashKey++ ) {
    COC_HEAD **prev = &cocState->gColorCache[hashKey];
    COC_HEAD *pCache = *prev;

    while ( pCache != NULL ) {
      if ( pCache->hashtable == NULL )
        werePurged++;

      if ( pCache->hashtable != NULL &&
           (pCache->chits == 0 ||
            pCache->refCnt == 0 ||
            pCache->chits / pCache->population < COC_MIN_REPRIEVE_LEVEL ) ) {
        coc_clear(pCache, TRUE);
        purged++;
      }

      if ( pCache->refCnt == 0 ) {
        /* No chains refer to the cache, so we can clobber it.
           Need to update the walking pointers tho' */
        *prev = pCache->pnext;
        freed++;
        --cocState->gCacheCount;
        --purged;
        mm_free(mm_pool_head, pCache,
                sizeof(COC_HEAD) + pCache->slotCount * sizeof(CLID));
      }
      else { /* We're keeping this one, non zero refcount */
        prev = &pCache->pnext;
        count++;
      }
      pCache = *prev;
    }
  }

  HQASSERT(cocState->gPurgedHeads == werePurged, "Inconsistent gPurgedHeads");
  HQASSERT(cocState->gCacheCount == count, "Inconsistent gCacheCount");
  HQASSERT(purged <= count,
           "coc_pu: managed to purge more items than the cache holds");

  COCTRACEF(((uint8*)"%d %d %d\n",
             cocState->gCacheCount, purged, cocState->gPurgedHeads));

  COCTRACEF(((uint8*)"  purged %d items\n",
             purged - cocState->gPurgedHeads));

  if ( freed == 0 ) {
    if ( purged == cocState->gPurgedHeads )
      return FALSE;
  }

  cocState->gPurgedHeads = purged;

  return TRUE;
}


/** Solicit method of the color cache low-memory handler. */
static low_mem_offer_t *coc_lowmem_solicit(low_mem_handler_t *handler,
                                           corecontext_t *context,
                                           size_t count,
                                           memory_requirement_t* requests)
{
  /** \todo the backend color cache is currently ignored */
  COC_STATE *cocState = frontEndColorState->cocState;
  static low_mem_offer_t offer;
  Bool kill_all;
  uint32 hashKey;
  size_t purgeable = 0, freeable = 0;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  /* Can't/won't do anything if the cache is empty or we've purged all
     the entries already. */
  HQASSERT(cocState->gPurgedHeads <= cocState->gCacheCount, "purged/count mismatch");
  if ( cocState->gCacheCount == 0 || cocState->gPurgedHeads == cocState->gCacheCount )
    return NULL;

  kill_all = context->between_operators;
  for ( hashKey = 0 ; hashKey < COC_HEADHASH_SIZE ; hashKey++ ) {
    COC_HEAD *pCache = cocState->gColorCache[hashKey];

    while ( pCache != NULL ) {
      HQASSERT(pCache->refCnt >= 0,"-ve ref count");
      HQASSERT(pCache->hashKey < COC_HEADHASH_SIZE,"dubious key");
      if ( pCache->hashtable != NULL &&
           (kill_all
            || (pCache->chits == 0 || pCache->refCnt == 0
                || pCache->chits/pCache->population < COC_MIN_REPRIEVE_LEVEL)) )
        ++purgeable;
      if ( pCache->refCnt == 0 )
        freeable += sizeof(COC_HEAD) + pCache->slotCount * sizeof(CLID);
      pCache = pCache->pnext;
    }
  }
  if ( purgeable == 0 )
    return NULL;

  offer.pool = mm_pool_coc; /* @@@@ mm_pool_head, also DL pool */
  /** \todo Inaccurate size prediction for now */
  offer.offer_size = purgeable * sizeof(COC_ENTRY*) * coc_hashtable_size();
  offer.offer_cost = 2.0;
  offer.next = NULL;
  return &offer;
}


/** Release method of the color cache low-memory handler. */
static Bool coc_lowmem_release(low_mem_handler_t *handler,
                               corecontext_t *context,
                               low_mem_offer_t *offer)
{
  /** \todo the backend color cache is currently ignored */
  COC_STATE *cocState = frontEndColorState->cocState;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t *, offer);

  COCTRACEF(((uint8*)"coc_purge: %d entries, %d purged  Killall: %d\n",
             cocState->gCacheCount, purged, context->between_operators));
  coc_trace_headhash(cocState);

  /* This being single-threaded, the cache can't have changed since solicit. */
  if ( context->between_operators )
    coc_reset(cocState, TRUE);
  else {
    Bool freed = coc_purge(cocState);
    HQASSERT(freed, "Failed to purge though offered.");
    UNUSED_PARAM(Bool, freed);
  }
  return TRUE;
}


/** The color cache low-memory handler. */
static low_mem_handler_t coc_lowmem_handler = {
  "Color cache",
  memory_tier_ram, coc_lowmem_solicit, coc_lowmem_release, FALSE,
  0, FALSE };
/* The low mem handler registered global does not need initialising in a init
 * function since it is only read in the finish function which can only be called
 * if the start function has been called which does the initialisation.
 */
static Bool coc_lowmem_handler_registered = FALSE;

Bool coc_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  return coc_lowmem_handler_registered = low_mem_handler_register(&coc_lowmem_handler);
}

void coc_swfinish(void)
{
  if (coc_lowmem_handler_registered) {
    low_mem_handler_deregister(&coc_lowmem_handler);
    coc_lowmem_handler_registered = FALSE;
  }
}


#define COC_CHAINCLASS_MAXITEMS (256)
static int32 coc_count_idslots(CLINK *pLink)
{
  int32 slots = 0;

  HQASSERT(pLink, "coc_count_idslots: no pointer");

  for ( ; pLink ; pLink = pLink->pnext ) {
    slots += 2;
    if ( pLink->idcount < 0 ) {
      HQASSERT(pLink->idcount == COLCACHE_DISABLE ||
               pLink->idcount == COLCACHE_NYI,
               "coc_count_idslots: bogus magic idcount value");
      return -1;
    }
    else
      slots += pLink->idcount;

    if ( pLink->pnext == NULL && pLink->iColorants )
      slots += pLink->n_iColorants;
  }

  return slots;
}


/* We've got a chain with no cache pointer.
   If we already have a cache for this chain class, then fill in pCache, else
   create a new one based on this chain.
   It's possible that we might have a COC_HEAD, with no underlying cache - the
   cache may have been flushed by a low memory condition.  Check for this and
   attempt to re-create if needed */
Bool coc_head_create(COC_STATE *cocState, GS_CHAINinfo *chain)
{
  CLID chainClass[ COC_CHAINCLASS_MAXITEMS ];
  int32 num_links = 0;
  int32 num_slots = 0;
  uintptr_t key;
  CLINK *pLink;
  CLINK *finalLink = NULL;
  COC_HEAD *cache;
#ifdef DISABLE_STANDALONE_COLOR_CACHE
  int32 hashdepth = 0;
  COC_HEAD *free_candidate = NULL;
#endif    /* DISABLE_STANDALONE_COLOR_CACHE */
  COC_STYLE style;
  mm_size_t size;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(chain != NULL, "chain NULL");

  chainContext = chain->context;
  HQASSERT(chainContext != NULL && chainContext->pnext != NULL, "chain NULL");
  HQASSERT(chainContext->pCache == NULL, "pCache NULL");
  HQASSERT((chainContext->cacheFlags & GSC_ENABLE_COLOR_CACHE) != 0,
           "coc_head_cr: should not create when disabled.");

  /* Avoid creating the coc head on first use because many chains are 'single
   * shot' chains that don't need the color cache. The ones that need the cache
   * won't suffer greatly from evaluating one color twice.
   */
  if ((chainContext->cacheFlags & GSC_COLOR_CACHE_FIRST_USE) != 0) {
    chainContext->cacheFlags &= ~GSC_COLOR_CACHE_FIRST_USE;
    return FALSE;
  }

  switch (chain->chainStyle) {
  case CHAINSTYLE_COMPLETE:
    style = COC_USE_DL_COLOR;
    break;
  case CHAINSTYLE_DUMMY_FINAL_LINK:
    style = COC_USE_FINAL_CLINK;
    break;
  default:
    HQFAIL("Unexpected color cache style");
    return FALSE;
  }

  /* Preemptively set the disabled flag so we don't repeatedly come in here on
   * failure when there is insufficient memory for a cache. The flag will be
   * cleared on successful exit.
   * NB. The interaction with the color chain cache is that the flag will be
   * cleared whenever a chain is reused because the chain head will be newly
   * created. So at most we'll enter here once whenever a chain is created or
   * reused.
   */
  chainContext->cacheFlags &= ~GSC_ENABLE_COLOR_CACHE;

  /* First construct the chainClass that identifies the type of chain */
  key = (CLID)chain->iColorSpace;
  chainClass[ num_slots++ ] = chain->iColorSpace;
  chainClass[ num_slots++ ] = chain->n_iColorants;

  pLink = chainContext->pnext;
  HQASSERT(pLink, "No clinks in chain in coc_head_cr");

  /* Color chains are short enough that we might as well walk the chain
   * early, to see if the static storage (chainClass) is large enough.
   */
  {
    int32 extra_slots = coc_count_idslots(pLink);
    if ( extra_slots < 0 ) { /* one of the special cases */
      /* Both possible magic values currently mean disable the cache, so mark
         the head as non-cacheing, and quit */
      return FALSE;
    }
    if ( extra_slots + num_slots > COC_CHAINCLASS_MAXITEMS ) {
      /* If we have too many links in the color chain to reliably confirm a
       * chain match, then give up and don't cache. But the number is sufficiently
       * large that that isn't expected to ever happen in the real world.
       */
      HQFAIL("Rather a lot of CLID slots in this chain");
      return FALSE;
    }
  }

  while ( pLink != NULL ) {
    ++num_links;

    HQASSERT(num_slots + 2 <= COC_CHAINCLASS_MAXITEMS, "slotCount overflow");
    chainClass[ num_slots++ ] = (CLID)pLink->linkType;
    chainClass[ num_slots++ ] = (CLID)pLink->idcount;

    if ( pLink->idcount > 0 ) { /* Copy the fields across */
      CLID *ptr = pLink->idslot;
      int32 i;
      HQASSERT(ptr, "slot pointer/idcount mismatch in coc_head_cr");
      HQASSERT(num_slots + pLink->idcount <= COC_CHAINCLASS_MAXITEMS,
               "slotCount overflow");
      for ( i = 0; i < pLink->idcount; ++i )
        chainClass[ num_slots++ ] = ptr[i];
    }

    /* @@ Might want to change this shift amount once we have an idea how long
       a typical id chain is */
    if ( pLink->pnext != NULL )
      key = (key << 5) | pLink->linkType;
    else {
      HQASSERT((style == COC_USE_DL_COLOR &&
                (pLink->linkType == CL_TYPEdevicecode ||
                 pLink->linkType == CL_TYPEpresep ||
                 pLink->linkType == CL_TYPEluminosity)) ||
               (style == COC_USE_FINAL_CLINK &&
                pLink->linkType == CL_TYPEdummyfinallink),
               "Bad terminating link in color cache?");
      HQASSERT(num_slots + pLink->n_iColorants <= COC_CHAINCLASS_MAXITEMS,
               "slotCount overflow");
      if ( pLink->iColorants ) {
        int32 i;
        for ( i = 0; i < pLink->n_iColorants; ++i ) {
          chainClass[ num_slots++ ] = (CLID)pLink->iColorants[i];
          key = (key << 5) | pLink->iColorants[i];
        }
      }
    }

    finalLink = pLink;
    pLink = pLink->pnext;
  }

  key += key >> 16;
  key += key >> 8;
  key &= (COC_HEADHASH_SIZE - 1);

  /* Allocate space for a COC_HEAD with the CLID slots immedialely afterwards.
     This allocation is safe from a possible cache purge in mm_low_handler, as
     we're not linked into the cache yet.
   */
  size = sizeof(COC_HEAD) + num_slots * sizeof(CLID);

  cache = mm_alloc(mm_pool_head, size, MM_ALLOC_CLASS_COC);
  if ( cache == NULL )
    return FALSE;   /* Not VMERROR because this failure is safe */

  cache->hashKey = key;
  cache->refCnt = 1;
  cache->generationNumber = cocState->gNextGenerationNumber++;
  cache->style = style;
  cache->imlut = NULL;
  cache->n_inComps = chain->n_iColorants;

  switch (style) {
  case COC_USE_DL_COLOR:
    cache->n_outComps = -1;   /* unused */
    break;
  case COC_USE_FINAL_CLINK:
    cache->n_outComps = finalLink->n_iColorants;
    break;
  }
  cache->linkCount = (int16)num_links;

  cache->slotCount = (int16)num_slots;
  /* Copy the id slots across */
  cache->pChainClass = (CLID*)(cache + 1);
  HqMemCpy(cache->pChainClass, chainClass, num_slots * sizeof(CLID));

  /* No need to protect against mm_low_handler, as this cache isn't inserted
     in the top level cache yet. */
  if ( ! coc_create_tables(cache) ) {
    mm_free(mm_pool_head, cache, size);
    return FALSE;
  }
  HQASSERT(cache->hashtable && cache->datatable, "no tables in coc_head_cr");

  cache->page = get_core_context()->page;
  cache->cocState = cocState;

  /* add to list for hashkey */
  cache->pnext = cocState->gColorCache[key];
  cocState->gColorCache[key] = cache;

  chainContext->pCache = cache;
  ++cocState->gCacheCount;

  /* Created a new cache, re-enable the cache flag */
  chainContext->cacheFlags |= GSC_ENABLE_COLOR_CACHE;

  return TRUE;
}

void coc_reserve(COC_HEAD *cache)
{
  CLINK_RESERVE(cache);
}

void coc_release(COC_HEAD *cache)
{
  /** \todo Should use CLINK_RELEASE but the rest of the code isn't thread safe either */
  --cache->refCnt;
}


static COC_DATATABLE* coc_datatable_create(COC_STYLE style,
                                           int32 iComps,
                                           int32 oComps)
{
  COC_DATATABLE *datatable;
  int32 entsize = 0;

  HQASSERT(style == COC_USE_DL_COLOR || style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");
  HQASSERT(iComps > 0, "iComps zero");

  datatable = mm_alloc(mm_pool_coc, coc_datatable_size(), MM_ALLOC_CLASS_COC);
  if ( datatable == NULL ) {
    return NULL;        /* Not VMERROR because this failure is safe */
  }

  /* Each flavour of color cache has different sized entries, but all entries
   * are the same size within any one cache. */
  switch (style) {
  case COC_USE_DL_COLOR:
    entsize = sizeof (COC_ENTRY) +
              sizeof (COC_ENTRY_DLC) +
              iComps * sizeof (USERVALUE);
    break;
  case COC_USE_FINAL_CLINK:
    HQASSERT(oComps > 0, "oComps zero");
    entsize = sizeof (COC_ENTRY) +
              (iComps + oComps) * sizeof (USERVALUE);
    break;
  }
  HQASSERT(entsize % 4 == 0, "cache entry not divisible by 4");

  datatable->pnext = NULL;
  datatable->entry_size = entsize; /* in bytes  */
  datatable->data = (COC_ENTRY*)(datatable + 1);
  /* Offset indicates the next 'free' bit of the datatable to use.  It moves
     high to low to simplify checking for a full table.
     If offset < 0, then this table is full and we need to chain on another
     one. */
  datatable->offset = DATATABLE_MAX_OFFSET(entsize);
  HQASSERT(datatable->offset + entsize + sizeof(COC_DATATABLE) <=
           coc_datatable_size(), "Max offset calculation incorrect");

  return datatable;
}

/* Allocate hash and datatables for 'num' component colors, and put them into
   the given cache head. */
static Bool coc_create_tables(COC_HEAD *cache)
{
  COC_ENTRY **newhashtable = NULL;
  HASH_DEPTH *newHashDepth = NULL;
  COC_DATATABLE *newdata = NULL;
  mm_size_t hashtablesize = coc_hashtable_size() * sizeof(COC_ENTRY*);
  mm_size_t hashDepthSize = coc_hashtable_size() * sizeof(HASH_DEPTH);

  newhashtable = mm_alloc(mm_pool_coc, hashtablesize, MM_ALLOC_CLASS_COC);
  newHashDepth = mm_alloc(mm_pool_coc, hashDepthSize, MM_ALLOC_CLASS_COC);

  newdata = coc_datatable_create(cache->style, cache->n_inComps, cache->n_outComps);
  if (newhashtable == NULL || newHashDepth == NULL || newdata == NULL) {
    if (newhashtable != NULL)
      mm_free(mm_pool_coc, newhashtable, hashtablesize);
    if (newHashDepth != NULL)
      mm_free(mm_pool_coc, newHashDepth, hashDepthSize);
    if (newdata != NULL)
      mm_free(mm_pool_coc, newdata, coc_datatable_size());
    return FALSE;       /* Not VMERROR because this failure is safe */
  }

  cache->datatable = newdata;
  cache->hashtable = newhashtable;
  cache->hashDepth = newHashDepth;
  cache->chits = 0;
  cache->population = 0;
  cache->clookups = 0;

  HqMemZero(cache->hashtable, coc_hashtable_size() * sizeof(COC_ENTRY*));
  HqMemZero(cache->hashDepth, coc_hashtable_size() * sizeof(HASH_DEPTH));
  cache->maxHashDepth = 0;

  return TRUE;
}

#define GETKEYCOL(_chain, _index) \
  ((*(int32*)&((_chain)->iColorValues[(_index)])) << (_index) )

/* Try and force integer loads and compares for speed */
#define INTCMP(_a, _b) (*((uint32*)( &(_a) )) == *((uint32*)( &(_b) )))


Bool coc_lookup(GS_CHAINinfo *chain, uint32 *return_hash)
{
#ifdef TRACE_CACHE
  COC_STATE *cocState;
#endif
  uint32 hashkey = 0;
  COC_ENTRY **htable;
  COC_ENTRY *ccentry;
  COC_ENTRY **prev;
  COC_HEAD *cache;
  HASH_DEPTH hashDepth = 0;
  USERVALUE *chain_oCols;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(chain != NULL, "chain NULL");

  chainContext = chain->context;
  HQASSERT(chainContext != NULL && chainContext->pnext != NULL, "chain NULL");

  cache = chainContext->pCache;
  HQASSERT(cache != NULL, "no cache in chain? in coc_lu_color");
  HQASSERT(cache->refCnt > 0, "coc_lu: refcount should be > 0");
  HQASSERT(cache->style == COC_USE_DL_COLOR || cache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");
  HQASSERT(chain->n_iColorants == cache->n_inComps,
           "chain/cache component clash");

#ifdef TRACE_CACHE
  cocState = cache->cocState;
  cocState->gTotalLookups++;
  cache->clookups++;
#endif

  /* Bail out if cmyk overprintprocess keys are present.
   * NB. We don't cache these because they are so rarely used and because they'd
   * require another word in the cache structure.
   */
  if (chain->overprintProcess != 0)
    return FALSE;

  /* Generate the hash key: */
  switch ( chain->n_iColorants ) {

  default: {
    int32 i = chain->n_iColorants - 1;
    do {
      hashkey += (uint32)GETKEYCOL(chain, i);
    } while ( --i > 3 );
  }
  /* FALLTHRU */

  case 4:
    hashkey += (uint32) ( GETKEYCOL(chain, 0) +
                          GETKEYCOL(chain, 1) +
                          GETKEYCOL(chain, 2) +
                          GETKEYCOL(chain, 3));
    break;

  case 3:
    hashkey = (uint32) ( GETKEYCOL(chain, 0) +
                         GETKEYCOL(chain, 1) +
                         GETKEYCOL(chain, 2));
    break;

  case 2:
    hashkey = (uint32) ( GETKEYCOL(chain, 0) +
                         GETKEYCOL(chain, 1));
    break;

  case 1:
    hashkey = (uint32) GETKEYCOL(chain, 0);
    break;
  } /* switch */

  hashkey += (uint32)dl_currentopacity(cache->page->dlc_context);

  hashkey += hashkey >> 16;
  hashkey += hashkey >> 8;
  hashkey &= (coc_hashtable_size() - 1);

  /* Make sure we initialise the return hash key value so any early exits don't
     have to remember to set it up */
  *return_hash = hashkey;

  htable = cache->hashtable;
  if (htable == NULL) {
    /* No hashtable, so this chain must have been purged.
       Just return the hashkey, and the coc_insert will try and re-create the
       tables */
    return FALSE;
  }

  prev = &htable[hashkey];
  ccentry = *prev;
  while (ccentry != NULL) {
    USERVALUE *cache_iCols;
    Bool hit = TRUE;

    getCacheInputPtr(cache, ccentry, &cache_iCols);

    switch ( chain->n_iColorants ) {

    default: {
      int32 i = chain->n_iColorants - 1;
      do {
        hit = hit && ( INTCMP(chain->iColorValues[i], cache_iCols[i]) );
      } while ( --i > 3 && hit );

      if ( ! hit )
        break;
    }
    /* FALLTHRU */

    case 4:
      hit = hit &&
        ( INTCMP(chain->iColorValues[0], cache_iCols[0]) &&
          INTCMP(chain->iColorValues[1], cache_iCols[1]) &&
          INTCMP(chain->iColorValues[2], cache_iCols[2]) &&
          INTCMP(chain->iColorValues[3], cache_iCols[3]) );
      break;

    case 3:
      hit = ( INTCMP(chain->iColorValues[0], cache_iCols[0]) &&
              INTCMP(chain->iColorValues[1], cache_iCols[1]) &&
              INTCMP(chain->iColorValues[2], cache_iCols[2]) );
      break;

    case 2:
      hit = ( INTCMP(chain->iColorValues[0], cache_iCols[0]) &&
              INTCMP(chain->iColorValues[1], cache_iCols[1]) );
      break;

    case 1:
      hit = ( INTCMP(chain->iColorValues[0], cache_iCols[0]) );
      break;

    } /* switch */

    if (hit) {
      COC_ENTRY_DLC *cache_dlc;
      USERVALUE *cache_oCols;
      int32 i;

      switch (cache->style) {
      case COC_USE_DL_COLOR: {
        dlc_context_t *dlc_context = cache->page->dlc_context;

        getCacheOutputPtr_dl(cache, ccentry, &cache_dlc);

        /* Now compare the dlc specific keys */
        hit = chain->inBlackType == LOOKUP_BLACK_TYPE(cache_dlc->blackType) &&
              dl_currentopacity(dlc_context) == cache_dlc->opacity;
        if (!hit)
          break;

        /* Hit - Populate the dl color, etc. */
        dlc_release(dlc_context, dlc_currentcolor(dlc_context));
        if (!dlc_copy(dlc_context, dlc_currentcolor(dlc_context),
                      &cache_dlc->dl_color))
          return FALSE ;
        dl_set_currentspflags(dlc_context, cache_dlc->spflags);
        dl_set_currentblacktype(dlc_context,
                                INSERT_BLACK_TYPE(cache_dlc->blackType));
        break;
       }
      case COC_USE_FINAL_CLINK:
        getChainOutputPtr_fl(chain, &chain_oCols);
        getCacheOutputPtr_fl(cache, ccentry, &cache_oCols);
        /* Populate the output numbers */
        for (i = 0; i < cache->n_outComps; i++)
          chain_oCols[i] = cache_oCols[i];
        break;
      }

      if (hit) {
        ++cache->chits;

        /* Move to top of list */
        *prev = ccentry->pnext;
        ccentry->pnext = htable[hashkey];
        htable[hashkey] = ccentry;

#ifdef TRACE_CACHE
        cocState->gNumFoundAtDepth[hashDepth]++;
        if (hashDepth > cocState->gMaxDepthFound)
          cocState->gMaxDepthFound = hashDepth;
#endif

        return TRUE ;
      }
    }

    hashDepth++;

    prev = &ccentry->pnext;
    ccentry = *prev;
  }

  /* Missed: return the calculated hash key for insert routine */

  return FALSE;
}

/* Hashkey passed in here was returned from coc_lookup, to save re-calculating
   it here */
void coc_insert(GS_CHAINinfo *chain,
                uint32 hashkey)
{
  COC_STATE *cocState;
  COC_DATATABLE *dtable;
  COC_ENTRY *ccentry;
  COC_HEAD *cache;
  USERVALUE *cache_iCols;
  USERVALUE *cache_oCols;
  USERVALUE *chain_iCols;
  USERVALUE *chain_oCols;
  COC_ENTRY_DLC *cache_dlc;
  int32 i;
  Bool result = FALSE;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(chain != NULL, "chain NULL");

  chainContext = chain->context;
  HQASSERT(chainContext != NULL, "chainContext NULL");

  cache = chainContext->pCache;
  cocState = cache->cocState;
  HQASSERT(cache != NULL, "no cache");
  HQASSERT(cache->refCnt > 0, "refcount should be > 0");
  HQASSERT(cache->style == COC_USE_DL_COLOR || cache->style == COC_USE_FINAL_CLINK,
           "Unexpected color cache style");
  HQASSERT(chain->n_iColorants == cache->n_inComps,
           "chain/cache component clash");
  HQASSERT(hashkey < (uint32)coc_hashtable_size(), "bad hash");

  /* Bail out if cmyk overprintprocess keys are present */
  if (chain->overprintProcess != 0)
    return;

  if (cache->hashtable == NULL) {
    HQASSERT(cocState->gPurgedHeads > 0, "coc_ins: Purged head but no counter");
    /* Been purged - try to re-create. */
    COC_ASSERT_ALLOC_SAFE(cache);
    if ( ! coc_create_tables(cache) )
      return;
    --cocState->gPurgedHeads;
  }

  dtable = cache->datatable;
  HQASSERT(dtable, "No datatable in coc_ins");

  HQASSERT(cache->n_inComps == chain->n_iColorants,
           "hash and chain colorant mismatch in coc_ins");

  if (cache->hashDepth[hashkey] >= COC_MAX_HASH_DEPTH) {
    COC_ENTRY *newTail;

    HQASSERT(cache->hashDepth[hashkey] == COC_MAX_HASH_DEPTH,
             "Hash depth too big");

    /* Find the last entry on this hash list and reuse it for the new entry */
    ccentry = cache->hashtable[hashkey];
    while (ccentry->pnext->pnext != NULL)
      ccentry = ccentry->pnext;

    /* Remove the tail from the list in preparation for adding it back at the head */
    newTail = ccentry;
    ccentry = ccentry->pnext;
    newTail->pnext = NULL;

    cache->hashDepth[hashkey]--;
    cache->population--;
  }
  else {
    if ( dtable->offset  < 0 ) {
       /* Table full, so extend with another one if we can */
      COC_DATATABLE *newtable;

      /* Extend by adding a new table */
      COC_BEGIN_PROTECTED_ALLOC(cache);
        COC_ASSERT_ALLOC_SAFE(cache);
        newtable = coc_datatable_create(cache->style, cache->n_inComps, cache->n_outComps);
      COC_END_PROTECTED_ALLOC(cache);

      if (newtable == NULL)
        return;

      newtable->entry_size = dtable->entry_size;
      newtable->data = (COC_ENTRY*)(newtable + 1);
      newtable->offset = DATATABLE_MAX_OFFSET(newtable->entry_size);
      HQASSERT(newtable->offset + newtable->entry_size +
               sizeof(COC_DATATABLE) <=
               coc_datatable_size(), "Max offset calculation incorrect");

      newtable->pnext = dtable;
      cache->datatable = newtable;
      dtable = newtable;
   }

    ccentry = (COC_ENTRY *) &( ((uint8*)(dtable->data))[dtable->offset] );
    dtable->offset -= dtable->entry_size;
  }

  getChainInputPtr(chain, &chain_iCols);
  getCacheInputPtr(cache, ccentry, &cache_iCols);

  for (i = 0; i < cache->n_inComps; i++)
    cache_iCols[i] = chain_iCols[i];

  switch (cache->style) {
  case COC_USE_DL_COLOR: {
    dlc_context_t *dlc_context = cache->page->dlc_context;

    getCacheOutputPtr_dl(cache, ccentry, &cache_dlc);

    cache_dlc->blackType = (chain->inBlackType & BT_LOOKUP_MASK);
    cache_dlc->blackType |= (dl_currentblacktype(dlc_context) << BT_LOOKUP_BITS);
    cache_dlc->opacity = dl_currentopacity(dlc_context);

    cache_dlc->spflags = dl_currentspflags(dlc_context);
    result = dlc_copy(dlc_context, &cache_dlc->dl_color,
                      dlc_currentcolor(dlc_context));
    break;
   }
  case COC_USE_FINAL_CLINK:
    getChainOutputPtr_fl(chain, &chain_oCols);
    getCacheOutputPtr_fl(cache, ccentry, &cache_oCols);

    /* Populate the output numbers */
    for (i = 0; i < cache->n_outComps; i++)
      cache_oCols[i] = chain_oCols[i];
    result = TRUE;
    break;
  }

  if (result) {
    /* All okay, update pointers */
    ccentry->pnext = cache->hashtable[hashkey];
    cache->hashtable[hashkey] = ccentry;

    cache->population++;
    cache->hashDepth[hashkey]++;
    if (cache->hashDepth[hashkey] > cache->maxHashDepth)
      cache->maxHashDepth = cache->hashDepth[hashkey];
  }
  else {
    /* Same reason as above */
    dtable->offset += dtable->entry_size;
  }
}

Bool coc_generationNumber(COC_STATE *cocState, GS_CHAINinfo* chain,
                          uint32* pGeneratioNumber)
{
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(chain != NULL, "chain NULL");
  HQASSERT(pGeneratioNumber != NULL, "pGeneratioNumber NULL");

  chainContext = chain->context;
  HQASSERT(chainContext != NULL, "chainContext NULL");

  HQASSERT(chain->n_iColorants > 0,
           "coc_generationNumber: 0 number of colorants");

  if (chain->n_iColorants == 0 ||
      (chainContext->cacheFlags & GSC_ENABLE_COLOR_CACHE) == 0) {
    *pGeneratioNumber = 0xFFFFFFFFu; /* set an unlikely value */
    return TRUE;
  }

  if (chainContext->pCache == NULL) {
    /* Avoid the first use optimisation */
    chainContext->cacheFlags &= ~GSC_COLOR_CACHE_FIRST_USE;
    /* failure to create the color cache means no generation available */
    if (! coc_head_create(cocState, chain) )
      return FALSE;
  }
  HQASSERT(chainContext->pCache != NULL, "Still could not create a color cache");

  *pGeneratioNumber = chainContext->pCache->generationNumber;

  return TRUE;
}

struct imlut_t* coc_imlut_get(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo* chain;
  GS_CHAIN_CONTEXT *chainContext;

  chain = colorInfo->chainInfo[colorType];
  HQASSERT(chain != NULL, "chain NULL");

  chainContext = chain->context;

  if (chainContext == NULL) {
    /* This function is called from outside color, so be defensive */
    HQFAIL("chainContext NULL");
    return NULL;
  }

  if ( chainContext->pCache != NULL )
    return chainContext->pCache->imlut;

  return NULL;
}

Bool coc_imlut_set(GS_COLORinfo *colorInfo, int32 colorType, struct imlut_t *imlut)
{
  GS_CHAINinfo* chain;
  GS_CHAIN_CONTEXT *chainContext;

  chain = colorInfo->chainInfo[colorType];
  HQASSERT(chain != NULL, "chain NULL");

  chainContext = chain->context;

  if (chainContext == NULL) {
    HQFAIL("chainContext NULL");
    return FALSE;
  }

  /* Image LUT caching relies on the COC cache being enabled. */
  if ((chainContext->cacheFlags & GSC_ENABLE_COLOR_CACHE) == 0)
    return FALSE;

  if ( chainContext->pCache == NULL ) {
    /* Avoid the first use optimisation */
    chainContext->cacheFlags &= ~GSC_COLOR_CACHE_FIRST_USE;
    if ( !coc_head_create(colorInfo->colorState->cocState, chain) )
      return FALSE;
  }

  chainContext->pCache->imlut = imlut;

  return TRUE;
}

/* Log stripped */
