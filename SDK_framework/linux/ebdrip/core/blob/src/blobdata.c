/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!src:blobdata.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Shared blob data cache for charstring and map data. This data cache is not
 * font-specific, it could be used for other purposes too.
 */

#include "core.h"
#include "uvms.h"
#include "objnamer.h"
#include "swerrors.h"
#include "often.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "mps.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "gcscan.h"
#include "blobdata.h"
#include "swstart.h"
#include "coreinit.h"
#include "lowmem.h"
#include "cache_handler.h"


/* ----------------------------------------------------------------------- */
#if defined(ASSERT_BUILD)
enum {
  BLOBDATA_TRACE_FRAMES = 1
} ;

static uint32 blobdata_trace = 0 ;
#endif

/* ----------------------------------------------------------------------- */
typedef struct blobdata_block_t blobdata_block_t ;

/** The blobdata structure associates a number of data blocks with an object,
    and a set of methods to fill those data blocks. Blocks are sorted by
    offset within the file. Blocks may overlap each other; attempts will be
    made when reading blocks to prevent this, but if a frame is requested
    spanning two previously-allocated blocks, it may occur. The object source
    pointer contained within the blob data entry may become the only
    remaining references to PostScript objects, so it either needs GC
    scanning or GC finalisation. */
struct blobdata_t {
  blobdata_t *next ;  /**< Blobs are linked in a list within each cache. */
  blobdata_cache_t *cache ;     /**< The cache instance owning this blob. */
  OBJECT *source ; /**< Data source is a live reference, it needs GC scanning. */
  const blobdata_methods_t *methods ; /**< Callbacks to populate blocks. */
  blobdata_private_t *data ;    /**< Persistant data for methods. */
  uint32 inuse ;                /**< Blobs referencing this source. */
  uint32 nblocks ;              /**< Number of blocks on block list. */
  int mode ;                    /**< Modes used to open blob data. */
  blobdata_block_t *blocks ;    /**< Block list for this source. */
  OBJECT_NAME_MEMBER
} ;

/** \brief Blob debug name. */
#define BLOB_DATA_NAME "Blob data"


/** A block of data cached in the store for a blob. The data allocated for
    the block is stored after the block header, with padding to account for
    aligning the data pointer between the header and the data pointer. */
struct blobdata_block_t {
  blobdata_block_t *next ; /**< Next block of data, sorted by address and alignment. */
  Hq32x2 start ;     /**< Start offset of the block in the data. */
  size_t length ;    /**< Length of data stored in block. */
  size_t allocated ; /**< Allocated length (>= block length). */
  uint32 lock ;      /**< Lock for in-use data. */
  uint8 *data ;      /**< Pointer to the allocated data. */
  OBJECT_NAME_MEMBER
} ;

/** \brief Blob data debug name. */
#define BLOB_BLOCK_NAME "Blob block"

/** \brief Calculate the allocated size of a block, including any alignment
    padding between the block header and the data. */
#define BLOBDATA_BLOCK_SIZE(block_) ((block_)->data + (block_)->allocated - (uint8 *)(block_))

/** \brief Return the minimum byte addition to align a pointer to a power of
    two boundary. */
#define BLOBDATA_ADJUSTMENT(ptr_, alignment_) \
  (-(intptr_t)(ptr_) & (intptr_t)((alignment_) - 1))

/** Multiple instances of blob data caches can be created, for different
    purposes (blob data, CMM blobs, etc). The parameters controlling the
    cache sizing and storage behaviour are in this cache instance
    structure. */
struct blobdata_cache_t {
  /** Low-memory handling data for this cache. */
  mm_simple_cache_t memory;

  /** Pointer to next initialised blob data cache. */
  blobdata_cache_t *next ;

  /** Open cache blocks. These are the roots of the data cache instances. */
  blobdata_t *blob_data ;

  /** The maximum amount of data before blocks are recycled. The default font
      data limit is 1024 * 1024 (1 MB). */
  size_t data_limit ;

  /** The lock indicates the current generation of open blobdata routines.
      Whenever the number of open blobdata handles returns to zero, the lock
      is incremented. When a frame is opened, its block's lock is set to the
      same as the current lock. Blocks with the current lock cannot be
      purged. Blob data entries are reference counted, so that closing the
      last open one will call the close method. */
  uint32 data_lock ;

  /** The number of blob maps open in this cache. The lock is increased when
      this reduces to zero. This is not a particularly good block purging
      strategy, it means that for each blob data cache, all memory maps on
      all blobs have to be closed before any data blocks can be released. */
  uint32 data_maps_open ;

  /** Default block size used for caching the blob data. Make this a
      multiple of the disk block size for best performance. Frame request
      sizes may be rounded up to increase the potential for data re-use. */
  size_t read_quantum ;

  /** All block allocations are rounded up to multiple of this size. This
      makes it much easier to find blocks than can be re-sized or
      re-cycled. */
  size_t alloc_quantum ;

  /** In order to prevent the data list from getting too long, we will trim
      any unused data stores which do not have blocks allocated beyond this
      limit while scanning for a data object (the limit allows the most
      recently used sources to retain their information even if their blocks
      are stolen). Pro-actively purging the list like this limits the amount
      of data that a GC scan will fix; we cannot call the destroy methods
      during a GC scan because the MPS is not re-entrant. The value of this
      parameter is a pure guess; for the font data cache the most that would
      make sense would be 16, which would allow one full-size (32k) block for
      each font and still fit in the default blob data cache limit. */
  uint32 trim_limit ;

  /** References to this blob cache from blob instances and blobdata_t. */
  uint32 refcount ;

  /** GC root for this blob cache. */
  mps_root_t gc_root ;

  OBJECT_NAME_MEMBER
} ;

#define BLOB_CACHE_NAME "Blob cache"


/** The global list of instantiated blob data caches. */
blobdata_cache_t *blob_data_caches = NULL ;


/* Forward typedef of object-source blob. */
typedef struct obj_blob obj_blob ;

/* Forward typedef of mapping context subclass. */
typedef struct obj_blob_map obj_blob_map ;

/** \brief Subclass of \c sw_blob_instance for use with the \c
    blobdata_cache_t implementation.

    The blobdata structure associates a number of data blocks with an object,
    and a set of methods to fill those data blocks. Blocks are sorted by
    offset within the file. Blocks may overlap each other; attempts will be
    made when reading blocks to prevent this, but if a frame is requested
    spanning two previously-allocated blocks, it may occur. The object source
    pointer contained within the blob data entry may become the only
    remaining references to PostScript objects, so it either needs GC
    scanning or GC finalisation. */
struct obj_blob {
  sw_blob_instance super ;      /**< Instance superclass must be first. */
  blobdata_t *data ;            /**< The cache data for this blob. This is
                                     shared between all blobs open on the
                                     same identifiable source, whether or not
                                     they were created by cloning the same
                                     original blob. */
  blobdata_cache_t *cache ;     /**< The cache instance owning this blob. */
  Hq32x2 currentptr ;           /**< Current seek location of blob. */
  int mode ;                    /**< Mode flags used to open blob. */
  uint32 refcount ;             /**< The number of references to this blob
                                     subclass, including references from the
                                     open mapping contexts. */
  obj_blob *next ;              /**< List of blobs owned by cache. */
  obj_blob_map *mapped ;        /**< The mapping contexts open on this blob. */
  OBJECT_NAME_MEMBER
} ;

/** \brief Blob debug name. */
#define SW_BLOB_NAME "Blobdata sw_blob"

/** \brief The blob structure encapsulates the underlying source, and
    the data access methods. */
struct obj_blob_map {
  sw_blob_map super ;        /**< Instance superclass must be first. */
  obj_blob *blob ;           /**< The blob owning this mapping region. */
  obj_blob_map *next ;       /**< Blob maps are linked in a list. */
  blobdata_block_t *blocks ; /**< The blocks used by this region. */
  OBJECT_NAME_MEMBER
} ;

/** \brief Blob map debug name. */
#define SW_BLOB_MAP_NAME "Blobdata sw_blob_map"

/* Desirability of alignment, lowest is most preferable. */
static const uint8 alignment_rank[BLOB_MAX_ALIGNMENT] = {
  0, 3, 2, 3, 1, 3, 2, 3
} ;

#define BLOCK_ORDER_ASSERT(block_) MACRO_START \
  HQASSERT((block_)->next == NULL || \
           Hq32x2Compare(&(block_)->start, &(block_)->next->start) < 0 || \
           (Hq32x2Compare(&(block_)->start, &(block_)->next->start) == 0 && \
            ((block_)->length > (block_)->next->length || \
             ((block_)->length == (block_)->next->length && \
              alignment_rank[(uintptr_t)(block_)->data & (BLOB_MAX_ALIGNMENT - 1)] < \
              alignment_rank[(uintptr_t)(block_)->next->data & (BLOB_MAX_ALIGNMENT - 1)]))), \
           "Block list out of order") ; \
MACRO_END

/* ----------------------------------------------------------------------- */

#if defined(ASSERT_BUILD)
static Bool full_sanity_checking = FALSE ;

  /* I've had a lot of trouble losing blocks. Sanity check that the cache is
     populated, that non-inuse caches don't have locked blocks, that the
     block order is correct, that the number of blocks in the chains are
     correct, that the size matches the advertised size, etc. Use as a trace
     condition. */
Bool blobdata_sanity_check(void)
{
  blobdata_cache_t *blob_cache ;
  Bool sane = TRUE ;

  if ( !full_sanity_checking )
    return TRUE ;

  for ( blob_cache = blob_data_caches ; blob_cache ; blob_cache = blob_cache->next ) {
    blobdata_t *blob_data ;
    size_t total_size = 0 ;
    size_t total_open = 0 ;

    VERIFY_OBJECT(blob_cache, BLOB_CACHE_NAME) ;

    HQASSERT(blob_cache->refcount > 0, "Blob cache refcounting gone wrong") ;
    for ( blob_data = blob_cache->blob_data ; blob_data ; blob_data = blob_data->next ) {
      blobdata_block_t *block ;
      uint32 blob_blocks = 0 ;

      VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

      for ( block = blob_data->blocks ; block ; block = block->next ) {
        VERIFY_OBJECT(block, BLOB_BLOCK_NAME) ;
        BLOCK_ORDER_ASSERT(block) ;

        /* This condition is currently not possible, but is not precluded by
           the blobdata design. If more than one blob is open at once, this
           could happen, because the lock is not bumped until all blobs are
           closed. */
        HQASSERT(blob_data->inuse ||
                 blob_cache->data_lock != block->lock,
                 "Block is locked in blob that is not in use") ;

        ++blob_blocks ;
        total_size += BLOBDATA_BLOCK_SIZE(block) ;
      }

      HQASSERT(blob_blocks == blob_data->nblocks,
               "Mismatch between reported and actual blob blocks") ;

      total_open += blob_data->inuse ;
      total_size += sizeof(blobdata_t) ;
    }

    HQASSERT(total_size == blob_cache->memory.data_size,
             "Mismatch between reported and actual blob data size") ;
  }
  return sane ;
}
#endif


/* ----------------------------------------------------------------------- */

static Bool blob_cache_purge(mm_simple_cache_t *mm_cache,
                             Bool *purged_something, size_t purge);


/* Initialise a blob cache instance. */
blobdata_cache_t *blob_cache_init(char *name,
                                  size_t data_limit,
                                  size_t alloc_quantum,
                                  size_t read_quantum,
                                  uint32 trim_limit,
                                  mm_cost_t cost,
                                  Bool multi_thread_safe,
                                  mm_pool_t pool)
{
  blobdata_cache_t *cache ;
  mps_res_t res ;

  HQASSERT(data_limit > 0, "Cache instance data limit too small") ;
  HQASSERT(alloc_quantum > 0, "Cache block alloc quantum must be non-zero") ;
  HQASSERT((alloc_quantum & (alloc_quantum - 1)) == 0,
           "Cache block alloc quantum is not a power of two") ;
  HQASSERT(read_quantum > 0, "Cache block read quantum must be non-zero") ;
  HQASSERT((read_quantum & (read_quantum - 1)) == 0,
           "Cache block read quantum is not a power of two") ;

  if ( (cache = mm_alloc(pool, sizeof(blobdata_cache_t),
                         MM_ALLOC_CLASS_BLOB_CACHE)) == NULL ) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  /* Set up GC root for blob data subsystem */
  res = mps_root_create(&cache->gc_root,
                        mm_arena, mps_rank_exact(),
                        0, blobdata_scan, cache, 0) ;
  if ( res != MPS_RES_OK ) {
    mm_free(pool, cache, sizeof(blobdata_cache_t)) ;
    (void)detail_error_handler(VMERROR, "Cannot create GC root for data cache.") ;
    return NULL ;
  }

  /* Initialise cache parameters */
  cache->memory.low_mem_handler = mm_simple_cache_handler_init;
  cache->memory.low_mem_handler.name = name;
  cache->memory.low_mem_handler.tier = cost.tier;
  cache->memory.low_mem_handler.multi_thread_safe = multi_thread_safe;
  /* no init needed for offer */
  cache->memory.data_size = cache->memory.last_data_size = 0;
  cache->memory.last_purge_worked = TRUE;
  cache->memory.cost = cost.value;
  cache->memory.pool = pool;
  cache->memory.purge = &blob_cache_purge;
  cache->blob_data = NULL ;
  cache->data_limit = data_limit ;
  cache->data_lock = 0 ;
  cache->data_maps_open = 0 ;
  cache->alloc_quantum = alloc_quantum ;
  cache->read_quantum = read_quantum ;
  cache->trim_limit = trim_limit ;
  cache->refcount = 1 ;
  NAME_OBJECT(cache, BLOB_CACHE_NAME) ;

  /* Link into global cache list. */
  cache->next = blob_data_caches ;
  blob_data_caches = cache ;

  if ( !low_mem_handler_register(&cache->memory.low_mem_handler) ) {
    mps_root_destroy(cache->gc_root);
    mm_free(pool, cache, sizeof(blobdata_cache_t));
    return NULL;
  }
  return cache ;
}

/* Destroy a blob cache. Note that this routine only resets the pointer to
   NULL if the last reference to the cache was destroyed. */
void blob_cache_destroy(blobdata_cache_t **where)
{
  blobdata_cache_t **prev, *curr ;

  HQASSERT(where, "Nowhere for find blob cache") ;

  curr = *where ;
  VERIFY_OBJECT(curr, BLOB_CACHE_NAME) ;

  HQASSERT(curr->refcount > 0, "Blob cache refcounting gone wrong") ;
  if ( --curr->refcount == 0 ) {
    /* Find this cache instance in the global list, and unlink it. We don't
       expect many blob caches, and they will be destroyed extremely rarely,
       so a linear search is OK. */
    for ( prev = &blob_data_caches ; (curr = *prev) != NULL ; prev = &curr->next ) {
      VERIFY_OBJECT(curr, BLOB_CACHE_NAME) ;

      if ( curr == *where ) {
        *prev = curr->next ;

        HQASSERT(curr->blob_data == NULL,
                 "Blob cache refcounting gone wrong; data still exists") ;

        low_mem_handler_deregister(&curr->memory.low_mem_handler);
        mps_root_destroy(curr->gc_root);
        UNNAME_OBJECT(curr) ;
        mm_free(curr->memory.pool, curr, sizeof(blobdata_cache_t)) ;
        break ;
      }
    }

    HQASSERT(curr == *where, "Blob data cache not found on global list") ;
    *where = NULL ;
  }
}

/* ----------------------------------------------------------------------- */

/** \brief Open a blobdata object.

    If \c blobdata_open succeeds, \c blobdata_close MUST be called. The
    blobdata methods pointer MUST be a pointer to static or global data; the
    methods may get called even after blobdata_close has been called. The
    blobdata cache may hold onto some data even over restores, if it can
    identify the data by a global reference. The pointer returned by
    blobdata_open is only valid between matched open and close methods, even
    if the data is retained. */
static sw_blob_result blobdata_open(blobdata_cache_t *cache,
                                    OBJECT *source,
                                    const blobdata_methods_t *methods,
                                    int mode,
                                    blobdata_t **new_blob_data)
{
  blobdata_t *blob_data, **blob_prev ;
  uint32 nblobs = 0 ;
  sw_blob_result result ;

  HQASSERT(new_blob_data, "Nowhere to put blob data") ;
  HQASSERT(cache, "No blob data cache") ;
  blob_prev = &cache->blob_data ;

  HQASSERT(source, "No blob data source") ;
  HQASSERT(methods, "No blob data methods") ;
  HQASSERT(methods->same, "No blob cache comparison method") ;
  HQASSERT(methods->create, "No blob source create method") ;
  HQASSERT(methods->open, "No blob source open method") ;
  HQASSERT(methods->close, "No blob source close method") ;

  /* Search for a blob data block cache that matches. Object identity must
     match; non-identity matches are handled through the same method. We can
     only use the same method if we know that we are using the same
     comparison scheme. We keep the previous pointer so that we can shift the
     blob data found to the start of the list, making it quicker to find the
     most recently used blob data. */
  while ( (blob_data = *blob_prev) != NULL ) {
    VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;
    if ( blob_data->methods == methods &&
         (blob_data->source == source ||
          (*methods->same)(source, blob_data->source)) ) {
      /* We have found the same data source. */
      Bool reopen_immediately = FALSE ;

      if ( blob_data->inuse ) {
        /* The blob source is already in use. Check for exclusive access. */
        if ( (mode & SW_EXCL) != 0 || (blob_data->mode & SW_EXCL) != 0 )
          return FAILURE(SW_BLOB_ERROR_ACCESS) ;

        /* Check for consistency of font open flags. */
        if ( (mode & SW_FONT) != (blob_data->mode & SW_FONT) )
          return FAILURE(SW_BLOB_ERROR_ACCESS) ;

        /* Check if we're opening a writable blob which was previously read
           only. In this case we need to close and re-open the source, to
           allow the data provider to see the changed flags. There is a
           cleanup problem if the close succeeds, but the re-open fails. */
        if ( (mode & (SW_RDWR|SW_WRONLY)) != 0 &&
             (blob_data->mode & (SW_RDWR|SW_WRONLY)) == 0 ) {
          (*methods->close)(blob_data->source, blob_data->data) ;
          reopen_immediately = TRUE ;
        }
#define return DO_NOT_RETURN_UNTIL_AFTER_reopen_immediately!

        /* Merge now-consistent mode flags. */
        mode |= blob_data->mode ;
      }

      /* Work around stupid allocation of bits for SW_RDWR flag. */
      if ( (mode & (SW_RDONLY|SW_WRONLY)) == (SW_RDONLY|SW_WRONLY) )
        mode = (mode & ~(SW_RDONLY|SW_WRONLY)) | SW_RDWR ;

      blob_data->mode = mode ;

      /** \todo NOT RIGHT FOR RESTORED FILES. */

      /* The blob_data object is a local object created when creating blob
         data entries; we can modify its access as we see fit. We will now
         modify its access to the laxer of the original and the new value.
         This is done because two separate blobs can be opened with different
         object permissions, but can share the same data source. */
      if ( oCanWrite(*source) && !oCanWrite(*blob_data->source) )
        theTags(*blob_data->source) =
          CAST_TO_UINT8((theTags(*blob_data->source) & ~ACCEMASK) | CANWRITE) ;

      /* We closed the data source because it wasn't opened for writable
         access previously. The invariant should be that if inuse is true,
         the data source is open, so try to restore that situation. */
#undef return
      if ( reopen_immediately ) {
        HQASSERT(blob_data->inuse, "Shouldn't be reopening unless in use") ;
        if ( (result = (*methods->open)(blob_data->source, blob_data->data, mode)) != SW_BLOB_OK ) {
          HQFAIL("Re-open of blob with new permissions unsuccessful") ;
          return FAILURE(result) ;
        }
      }

      /* Un-link and re-link at the start of the chain */
      *blob_prev = blob_data->next ;
      break ;
    } else if ( ++nblobs > cache->trim_limit &&
                !blob_data->inuse &&
                blob_data->blocks == NULL ) {
      HQASSERT(blob_data->nblocks == 0,
               "blob data not in use but blocks still exist") ;
      *blob_prev = blob_data->next ;

      if ( blob_data->source != NULL ) {
        (*blob_data->methods->destroy)(blob_data->source, &blob_data->data) ;
        HQASSERT(blob_data->data == NULL,
                 "blob data destroy method failed to clean up private data") ;
      }

      UNNAME_OBJECT(blob_data) ;
      mm_free(cache->memory.pool, blob_data, sizeof(blobdata_t));

      cache->memory.data_size -= sizeof(blobdata_t) ;
      blob_cache_destroy(&cache) ;
      HQASSERT(cache, "Blob cache refcounting broken; lost active cache") ;
    } else {
      blob_prev = &blob_data->next ;
    }

    SwOftenUnsafe() ;
  }

  /* If we didn't find a blob data entry for this source, create a new one.
     The object is copied into new object memory, because we don't know where
     the pointer came from. It could be a pointer from the C, PostScript or
     graphics stack, in which case its contents will change unpredictably. */
  if ( !blob_data ) {
    OBJECT *copy ;

    /* Don't mind if mm_alloc fails after get_lomemory, the object memory will
       be returned by a restore or GC. */
    if ( (copy = get_lomemory(1)) == NULL ||
         (blob_data = (blobdata_t *)mm_alloc(cache->memory.pool,
                                             sizeof(blobdata_t),
                                             MM_ALLOC_CLASS_BLOB_DATA)) == NULL ) {
      return FAILURE(SW_BLOB_ERROR_MEMORY) ;
    }

    Copy(copy, source) ;
    blob_data->cache = cache ;
    blob_data->source = copy ;
    blob_data->methods = methods ;
    blob_data->data = NULL ;
    blob_data->inuse = 0 ;
    blob_data->nblocks = 0 ;
    blob_data->mode = mode ;
    blob_data->blocks = NULL ;

    if ( (result = (*methods->create)(source, &blob_data->data)) != SW_BLOB_OK ) {
      mm_free(cache->memory.pool, (mm_addr_t)blob_data, sizeof(blobdata_t));
      return FAILURE(result) ;
    }

    NAME_OBJECT(blob_data, BLOB_DATA_NAME) ;

    ++cache->refcount ;
    HQASSERT(cache->refcount > 0, "Blob data cache refcount wrapped") ;
    cache->memory.data_size += sizeof(blobdata_t);
  }

  /* Insert entry at head of list; for entries that we matched and removed from
     the list, this implements MRU behaviour. */
  blob_data->next = cache->blob_data ;
  cache->blob_data = blob_data ;

  HQTRACE(!blobdata_sanity_check(), ("blob data inconsistent during open\n")) ;

  /* If we haven't called the open method already, copy the source object and
     call it. The protection against calling the open method multiple times
     allows the available and close methods to be able to do predictable
     modifications to the object if necessary. Use the blob data source
     object rather than the matching object. */
  if ( blob_data->inuse == 0 ) {
    /* We can safely return on failure, the blob data entry will be purged on
       restore or when flushed. */
    if ( (result = (*methods->open)(blob_data->source, blob_data->data, mode)) != SW_BLOB_OK )
      return FAILURE(result) ;
  }

  /* Finally, note that this blob is now in use. */
  ++blob_data->inuse ;

  *new_blob_data = blob_data ;

  return SW_BLOB_OK ;
}

/** \brief Close a blobdata object. This MUST be called to close a blob data
    object if \c blobdata_open succeeded. */
static void blobdata_close(blobdata_t **blob_ptr)
{
  blobdata_t *blob_data ;
  blobdata_cache_t *cache ;

  HQASSERT(blob_ptr, "No blob data pointer") ;

  blob_data = *blob_ptr ;
  HQASSERT(blob_data, "No blob data") ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;
  HQASSERT(blob_data->inuse, "blob data not in use") ;
  HQASSERT(blob_data->methods->close, "No blob source close method") ;

  cache = blob_data->cache ;
  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;

  /* Decrement inuse count, but don't actually delete the blob data until
     restored or flushed. Don't clear the blob data pointer unless it is the
     last open instance of this font, we might be in a recursive call that
     uses the same blob data pointer (e.g. Type 4 SEAC). */
  if ( --blob_data->inuse == 0 ) {
    (*blob_data->methods->close)(blob_data->source, blob_data->data) ;
    *blob_ptr = NULL ;
  }

  HQTRACE(!blobdata_sanity_check(), ("blob data inconsistent after close")) ;
}

/* ----------------------------------------------------------------------- */

/* This is the main routine used by consumers of blob data. It provides
   access to a contiguous buffer ("frame") of data, starting at the specified
   offset and having a specified length. The frame of data persists until the
   blobdata_close routine is called. */
uint8 *blobdata_frame(blobdata_t *blob_data, Hq32x2 start,
                      size_t length, size_t alignment)
{
  blobdata_block_t *search_block, **search_prev ;
  blobdata_block_t *found_block = NULL, *misaligned_block = NULL ;
  blobdata_cache_t *cache ;
  Hq32x2 offset, end, blimit = HQ32X2_INIT_ZERO ;
  size_t soffset, avlength, length_wanted ;
  uint8 *avframe ;
  const blobdata_methods_t *methods ;

  HQASSERT(alignment > 0 &&                      /* Alignment is positive */
           (alignment & (alignment - 1)) == 0 && /* and is a power of 2 */
           alignment <= BLOB_MAX_ALIGNMENT,      /* and is in range*/
           "Invalid frame alignment") ;

  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  /* TT and CFF clients have been known to ask for zero-length frames. In some
     cases, the logic to prevent asking for such frames is too tortuous, so
     provide a special null frame pointer back if requested. */
  if ( length == 0 ) {
    uint8 nullframe[BLOB_MAX_ALIGNMENT] = { '\0' } ;
    /* To align the null frame pointer correctly, we take the address of the
       last null byte declared above, and round the address down until it
       hits an appropriate boundary. */
    return (uint8 *)((uintptr_t)&nullframe[BLOB_MAX_ALIGNMENT - 1] & ~(uintptr_t)(alignment - 1)) ;
  }

  HQASSERT(length > 0, "No data requested") ;
  HQASSERT(blob_data->inuse, "blob data not in use") ;

  cache = blob_data->cache ;
  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;

  HQTRACE(!blobdata_sanity_check(),
          ("blob data inconsistent before opening frame")) ;

  /* Prepare the end offset of the frame. */
  Hq32x2FromSize_t(&end, length) ;
  Hq32x2Add(&end, &start, &end) ;

  /** \todo Try using a tree for block storage. The sort order is lowest base
      address first, sub-sorted by highest end address first. */

  /* Find the first block that may contain this data. Because we want the
     data to be aligned in a specific way, even if this block does contain
     the data, we may not be able to use it. I had attempted an even more
     baroque scheme to copy parts of existing blocks that overlap the data,
     but it just doesn't seem worth it. It may even be quicker to read one
     contiguous section from disc, because there aren't any extra seeks
     preventing the OS from read-ahead. */
  for ( search_prev = &blob_data->blocks ;
        (search_block = *search_prev) != NULL ;
        search_prev = &search_block->next ) {
    VERIFY_OBJECT(search_block, BLOB_BLOCK_NAME) ;
    BLOCK_ORDER_ASSERT(search_block) ;

    Hq32x2Subtract(&offset, &start, &search_block->start) ;

    if ( Hq32x2Sign(&offset) < 0 ) {
      /* Block start address is too high. Treat this the same as running off
         the end of the list. */
      HQASSERT(found_block == NULL, "Block should not have been found") ;
      break ;
    }

    /* In case of extreme offsets caused by mapping ends of very long files,
       test rather than assert that the offset fits in size_t. */
    if ( Hq32x2ToSize_t(&offset, &soffset) ) {
      uint8 *data = search_block->data + soffset ;

      HQASSERT(soffset + length >= soffset && soffset + length >= length,
               "Extreme offset wrapped data length") ;

      if ( soffset + length <= search_block->length ) {
        /* All of the data is in this block. */
        if ( ((uintptr_t)data & (uintptr_t)(alignment - 1)) == 0 ) {
          /* The data block is aligned OK for this request. */

          search_block->lock = cache->data_lock ;
          HQTRACE((blobdata_trace & BLOBDATA_TRACE_FRAMES) != 0,
                  ("Frame %u(%u) +%u found in %u+%u at %u(%p)",
                   start.low, alignment, length,
                   search_block->start.low, search_block->length, soffset, data)) ;
          return data ;
        }

        /* The data is in the block, but it is misaligned. Save this block
           reference for later, it'll be quicker to copy the data to the right
           alignment than to read from disc. */
        if ( misaligned_block == NULL )
          misaligned_block = search_block ;
      } else {
        /* The block does not contain all of the required data. However,
           if it can be extended to include the data and is aligned OK,
           use it. */
        if ( soffset + length <= search_block->allocated &&
             ((uintptr_t)data & (uintptr_t)(alignment - 1)) == 0 ) {
          /* This block can be extended to include the data required. */
          found_block = search_block ;
          break ;
        }
      }
    }

    SwOftenUnsafe() ;
  }

  /* Set the block limit to the start of the
     first block after the end of the requested frame. The block limit
     will be used to determine the maximum allocation size if we need to
     allocate a new block. */
  while ( search_block != NULL ) {
    if ( Hq32x2Compare(&search_block->start, &end) >= 0 ) {
      /* Block starts beyond end of requested frame. This block start
         will limit over-reads of new data, to avoid overlapping
         blocks. */
      blimit = search_block->start ;
      break ;
    }
    search_block = search_block->next ;
  }

  methods = blob_data->methods ;
  HQASSERT(methods && methods->available && methods->read,
           "No blob data availability or read methods") ;

  /* No existing block covers the frame. See if we can get enough data
     directly from the source to cover the frame without copying. */
  avlength = 0 ;
  if ( (avframe = (*methods->available)(blob_data->source, blob_data->data,
                                        start, &avlength)) != NULL ) {
    if ( avlength >= length &&
         ((uintptr_t)avframe & (uintptr_t)(alignment - 1)) == 0 ) {
      /* The data block is sufficient size and is aligned OK for this
         request. */
      HQTRACE((blobdata_trace & BLOBDATA_TRACE_FRAMES) != 0,
              ("Frame %u(%u) +%u available from %p (+%u)",
               start.low, alignment, length, avframe, avlength)) ;
      return avframe ;
    }
  } else {
    HQASSERT(avlength == 0, "Nothing available, but method returned non-zero") ;
  }

  /* If we found an extensible block, detach it from the blob cache to
     simplify the control flow. */
  if ( found_block != NULL ) {
    HQASSERT(*search_prev == found_block,
             "Previous pointer does not indicate found block") ;
    *search_prev = found_block->next ;
    found_block->next = NULL ;
    --blob_data->nblocks ;
    cache->memory.data_size -= BLOBDATA_BLOCK_SIZE(found_block);
  }

  /* We didn't have the data instantly available, so we need to decide how
     much to try and buffer. We want a compromise between buffering the whole
     data source and searching too many small fragments. Our goals when
     deciding on a block size and alignment are:

     1) Don't re-read information from the disc.
     2) Don't read huge amounts of information that won't be used
     3) Keep block sizes to standard sizes, to make re-cycling block easier
     4) Avoid overlapping blocks where possible.

     So, the decision tree will be:

     * If we found a block that could be extended to include this data, read
       the extra data into it. This will happen if the allocation is large
       enough to cover the frame and the frame pointer returned would be
       aligned correctly, but the current data available is not enough.
     * If we did not have an extendable block, and are already storing more
       than the cache limit, search for a block of a suitable size to steal.
     * If we couldn't steal a block, try allocating a new block, rounding the
       allocation size up to a standard quantum.
     * If we had data available without copying, then it is likely that the
       frame requested crosses the boundaries between two or more
       non-contiguous segments. We will allocate the smallest block to cover
       the frame, hypothesising that allocations either side of this frame
       are likely to be available without copying.
     * When filling the block, if there was data available without copying,
       and trim it to the start
       of the first block after this frame. We are unlikely to want to exceed
       this limit anyway, because frame requests tend to fall at table
       boundaries within the data. This is also why we don't extend any
       data before the offset.
  */

  /* Before calling memory allocation functions, which may call the
     low-memory handler, lock the block containing the misaligned data, to
     prevent it from being stolen. */
  if ( misaligned_block )
    misaligned_block->lock = cache->data_lock ;

  /* At this point, the following should hold:

     1) found_block is not NULL only if a block suitable for extension was
        found, and the block data was aligned correctly.
     2) avlength is the length returned by the available method. It is either
        insufficient to cover the request, or the alignment of the frame
        returned is incorrect.
     3) misaligned_block is not NULL only if a block containing the data, but
        aligned wrongly was found.
  */
  if ( found_block == NULL &&
       cache->memory.data_size >= cache->data_limit ) {
    /* We do not have a suitable extendable block, and we've reached the
       limit on the amount we should be caching. Go and steal a block. */
    blobdata_t *search_blob ;
    blobdata_t *candidate_blob = NULL ;
    blobdata_block_t **candidate_prev = NULL ;
    enum {
      BLOBDATA_CANDIDATE_ORPHAN,     /* Orphaned blocks selected first */
      BLOBDATA_CANDIDATE_OTHER,      /* Different blob, not in use */
      BLOBDATA_CANDIDATE_INUSE,      /* Different blob, in use */
      BLOBDATA_CANDIDATE_SAME,       /* Same font */
      BLOBDATA_CANDIDATE_NONE
    } candidate_order = BLOBDATA_CANDIDATE_NONE ;
    uint32 candidate_lock = cache->data_lock ;

    /* Try to steal a block from another blob data entry, within the size
       ranges specified. We prefer to take orphaned blocks (blocks which were
       in use at the time a spanning block was allocated which shadowed the
       block), blocks from other fonts which aren't in use, blocks from other
       fonts which are in use, and finally blocks from the same font. We also
       prefer to take the oldest block found that matches. We'll accept the
       first suitable orphan or block from a font that's not in use. */
    for ( search_blob = cache->blob_data ;
          search_blob && candidate_order > BLOBDATA_CANDIDATE_OTHER ;
          search_blob = search_blob->next ) {
      int32 search_blob_order = BLOBDATA_CANDIDATE_OTHER ;
      Hq32x2 highest_end = HQ32X2_INIT_ZERO ; /* High water mark for orphan detection */

      VERIFY_OBJECT(search_blob, BLOB_DATA_NAME) ;

      /* Set an offset to be applied to the candidate score for all the
         blocks in this blob. Prefer other blobs and blobs that aren't in
         use to poaching blocks from this blob. */
      if ( search_blob == blob_data )
        search_blob_order = BLOBDATA_CANDIDATE_SAME ;
      else if ( search_blob->inuse )
        search_blob_order = BLOBDATA_CANDIDATE_INUSE ;

      for ( search_prev = &search_blob->blocks ;
            (search_block = *search_prev) != NULL ;
            search_prev = &search_block->next ) {
        Hq32x2 search_end ;
        int32 search_order = search_blob_order ;
        uint8 *realigned ;

        VERIFY_OBJECT(search_block, BLOB_BLOCK_NAME) ;
        BLOCK_ORDER_ASSERT(search_block) ;

        Hq32x2FromSize_t(&search_end, search_block->length) ;
        Hq32x2Add(&search_end, &search_end, &search_block->start) ;

        /* Orphaned blocks are those covered by a previous block. Because
           blocks are sorted by start order, we can detect them by testing if
           the block end is smaller than the highest end point so far. */
        if ( Hq32x2Compare(&search_end, &highest_end) > 0 )
          highest_end = search_end ;
        else
          search_order = BLOBDATA_CANDIDATE_ORPHAN ; /* This block is orphaned */

        /* Determine the start of the data if we realign for this block. */
        realigned = (uint8 *)(search_block + 1) ;
        realigned += BLOBDATA_ADJUSTMENT(realigned, alignment) ;

        /* Cannot steal blocks that are in use, or are too small after
           realignment. */
        if ( search_block->lock != cache->data_lock &&
             search_block->data + search_block->allocated >= realigned + length ) {
          /* If this is a better candidate than the existing one, use it. */
          if ( search_order < candidate_order ||
               (search_order == candidate_order &&
                search_block->lock < candidate_lock) ) {
            candidate_order = search_order ;
            candidate_prev = search_prev ;
            candidate_blob = search_blob ;
            candidate_lock = search_block->lock ;

            /* If this is an orphan, quit searching because the block is
               otherwise useless. */
            if ( candidate_order == BLOBDATA_CANDIDATE_ORPHAN )
              break ;
          }
        }

        SwOftenUnsafe() ;
      }
    }

    if ( candidate_order != BLOBDATA_CANDIDATE_NONE ) {
      uint8 *realigned ;

      HQASSERT(candidate_prev != NULL, "No candidate block pointer") ;

      found_block = *candidate_prev ;
      HQASSERT(found_block != NULL, "No candidate block") ;
      HQASSERT(found_block->lock != cache->data_lock, "Candidate block is in use") ;

      /* Unlink the candidate block from the blob data cache, and re-link it
         into the right blob list. */
      *candidate_prev = found_block->next ;
      --candidate_blob->nblocks ;
      cache->memory.data_size -= BLOBDATA_BLOCK_SIZE(found_block);

      /* Reset the start and end addresses of the block, we'll let the
         generic extension code handle reading the appropriate data. */
      found_block->start = start ;
      found_block->length = 0 ;

      /* Re-align the data pointer. */
      realigned = (uint8 *)(found_block + 1) ;
      realigned += BLOBDATA_ADJUSTMENT(realigned, alignment) ;
      found_block->allocated += found_block->data - realigned ;
      found_block->data = realigned ;
    }

    HQTRACE(!blobdata_sanity_check(),
            ("blob data inconsistent after stealing block")) ;
  }

  if ( found_block == NULL ) {
    /* We did not find a suitable block. Allocate one, and align its start
       address such that the frame returned will be aligned correctly. */
    size_t required = sizeof(blobdata_block_t) + length + alignment - 1 ;
    size_t allocsize = required ;
    size_t alimit, adjust ;

    /* If we are not restricting the read size because we have zero-copy
       data, ensure the allocation size is at least one disc block size. */
    if ( avlength == 0 && allocsize < cache->read_quantum )
      allocsize = cache->read_quantum ;

    /* If restricted by a backstop block, reduce the allocation to reach the
       backstop. If not found, the backstop limit is zero, so the offset will
       be negative and the conversion to size_t will fail. */
    Hq32x2Subtract(&offset, &blimit, &start) ;
    if ( Hq32x2ToSize_t(&offset, &alimit) && alimit < allocsize ) {
      if ( alimit > required )
        allocsize = alimit ;
      else
        allocsize = required ;
    }

    /* Always round up the allocation size to nearest allocation quantum for
       easier re-use of blocks. */
    allocsize = SIZE_ALIGN_UP(allocsize, cache->alloc_quantum);
    if ( (found_block = (blobdata_block_t *)mm_alloc(cache->memory.pool,
                                                     allocsize,
                                                     MM_ALLOC_CLASS_BLOB_DATA))
         == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    /* Set the data address such that the frame we're interested in has the
       right alignment. We can be more generous with alignment than was
       requested. The adjustment calculated here is the minimum amount to add
       to the base allocation address to align the frame as requested. */
    adjust = (size_t)BLOBDATA_ADJUSTMENT(found_block + 1, alignment) ;
    found_block->data = (uint8 *)(found_block + 1) + adjust ;
    found_block->allocated = allocsize - adjust - sizeof(blobdata_block_t) ;
    found_block->next = NULL ;
    found_block->lock = 0 ;
    /* We're going to use the generic extension code to read data into the
       block, so start off by making the block empty. */
    found_block->start = start ;
    found_block->length = 0 ;
    NAME_OBJECT(found_block, BLOB_BLOCK_NAME) ;
  }

  HQASSERT(found_block != NULL, "Should have found or allocated data block by this time") ;

  /* The offset from the the start of the block to the desired frame. */
  Hq32x2Subtract(&offset, &start, &found_block->start) ;
  if ( !Hq32x2ToSize_t(&offset, &soffset) )
    HQFAIL("Offset of data in block too large for size_t") ;
  HQASSERT(soffset < found_block->allocated &&
           soffset + length <= found_block->allocated,
           "Frame should fit in block") ;

  /* If there was an amount available immediately, but it wasn't aligned
     correctly, or wasn't sufficient size, see if we can use any of it. */
  if ( avlength > 0 ) {
    /* If the block doesn't include or abut the start of the available frame,
       fall through to the general purpose reading code. Also, if the
       available frame ends before the data already in the block, then there
       is no useful data to copy. */
    if ( found_block->length >= soffset &&
         found_block->length < soffset + avlength ) {
      /* Restrict the amount we copy to fit in the allocated block. More than
         we need could have been available, but aligned incorrectly. */
      if ( avlength + soffset > found_block->allocated )
        avlength = found_block->allocated - soffset ;

      HqMemCpy(found_block->data + found_block->length,
               avframe + found_block->length - soffset,
               avlength) ;

      /* Increase the block length by the amount we copied. */
      found_block->length += avlength ;

      if ( found_block->length >= soffset + length )
        goto insert_block ;
    }
  }

  /* If we found a misaligned block containing the data, copy any remainder
     into the found block */
  if ( misaligned_block != NULL ) {
    Hq32x2 moffset ;
    size_t smoffset ;

    /* Offset between start of misaligned block and end of found block. */
    Hq32x2FromSize_t(&moffset, found_block->length) ;
    Hq32x2Add(&moffset, &moffset, &found_block->start) ;
    Hq32x2Subtract(&moffset, &moffset, &misaligned_block->start) ;

    /* Test the boundaries of the misaligned block rather than assert, partly
       out of paranoia, but also with a thought that in future the misaligned
       block may become the largest block to overlap the start of the
       frame. */
    if ( Hq32x2ToSize_t(&moffset, &smoffset) &&
         smoffset < misaligned_block->length ) {
      /* In English, the amount we need is where we want to end up, minus the
         amount already in the found block. */
      size_t copysize = soffset + length - found_block->length ;

      if ( copysize + smoffset > misaligned_block->length )
        copysize = misaligned_block->length - smoffset ;

      HqMemCpy(found_block->data + found_block->length,
               misaligned_block->data + smoffset,
               copysize) ;

      /* Increase the block length by the amount we copied. */
      found_block->length += copysize ;

      if ( found_block->length >= soffset + length )
        goto insert_block ;
    }
  }

  /* We didn't have the required data in memory, so prepare to read it from
     the source. If we had data available but couldn't use it, we won't round
     up the read size, because other requests may be satisfied without
     buffering. If we're going to disc, we might as well store the whole
     block read from disc, so round up the end of a request to the next file
     read quantum boundary. After rounding up, we'll limit the read to the
     backstop limit to avoid overlapping data. */

  /** \todo Hqx encryption adds an offset to encrypted data, causing these
      reads to span actual disc blocks. We may want to take this into
      account. Hqx encryption works, but may read partial blocks across block
      boundaries, which is less than optimal. */

  /* If do not have zero-copy data, round up the amount of data read into
     this block. */
  if ( avlength == 0 ) {
    /* Round up the end of the request to the end of a read quantum. */
    end.low |= cache->read_quantum - 1 ;
    Hq32x2AddUint32(&end, &end, 1) ;

    /* If we have a backstop limit, then trim the read to that position. */
    if ( !Hq32x2IsZero(&blimit) && Hq32x2Compare(&end, &blimit) > 0 )
      end = blimit ;
  }

  /* Work out the start address to read to, and the number of bytes needed. */
  Hq32x2FromSize_t(&offset, found_block->length) ;
  Hq32x2Add(&start, &offset, &found_block->start) ;
  Hq32x2Subtract(&offset, &end, &found_block->start) ;
  if ( !Hq32x2ToSize_t(&offset, &length_wanted) )
    HQFAIL("Bytes needed in block should convert to size_t") ;

  /* Trim the read to the allocated space in the block. */
  if ( length_wanted > found_block->allocated )
    length_wanted = found_block->allocated ;

  /* Read as much data as we can. */
  found_block->length += (*methods->read)(blob_data->source,
                                          blob_data->data,
                                          found_block->data + found_block->length,
                                          start,
                                          length_wanted - found_block->length) ;

  /* If we got enough to cover the request, we're OK, regardless of whether
     we asked for more. */
  if ( found_block->length < soffset + length ) {
    /* Failed to read the entire frame */
    HQASSERT(found_block->lock != cache->data_lock, "Freeing locked block") ;
    UNNAME_OBJECT(found_block) ;
    mm_free(cache->memory.pool, found_block, BLOBDATA_BLOCK_SIZE(found_block));
    return FAILURE(NULL) ;
  }

 insert_block:
  /* Re-insert block into blob list at the appropriate place. */
  for ( search_prev = &blob_data->blocks ;
        (search_block = *search_prev) != NULL ;
        search_prev = &search_block->next ) {
    int32 comparison ;

    VERIFY_OBJECT(search_block, BLOB_BLOCK_NAME) ;
    BLOCK_ORDER_ASSERT(search_block) ;

    comparison = Hq32x2Compare(&found_block->start, &search_block->start) ;
    if ( comparison <= 0 ) {
      if ( comparison < 0 )
        break ; /* New block starts before search block. */

      /* New block starts at same address as old block. Sub-sort by length. */
      if ( found_block->length >= search_block->length ) {
        if ( found_block->length > search_block->length )
          break ; /* New block is longer than search block */

        /* New block is identical to search block. Sort so that the more
           general base alignment is first. */
        if ( alignment_rank[(uintptr_t)found_block->data & (BLOB_MAX_ALIGNMENT - 1)] <
             alignment_rank[(uintptr_t)search_block->data & (BLOB_MAX_ALIGNMENT - 1)] )
          break ; /* New block has preferable alignment to search block. */
      }
    }
  }

  found_block->next = *search_prev ;
  *search_prev = found_block ;
  ++blob_data->nblocks ;
  cache->memory.data_size += BLOBDATA_BLOCK_SIZE(found_block);

  if ( found_block->length >= soffset + length ) {
    found_block->lock = cache->data_lock ;
    return found_block->data + soffset ;
  }

  /* Failed to read the entire frame requested */
  return FAILURE(NULL) ;
}

/* ----------------------------------------------------------------------- */


/** \brief Clear a given quantity of data from a blob data cache.

  \param[in]  mm_cache  The cache to purge.
  \param[out] purged_something  Indicates if anything was purged.
  \param[in]  purge  The amount to purge.

  This won't touch data currently in use, so it may fail to clear as
  much as requested.
 */
static Bool blob_cache_purge(mm_simple_cache_t *mm_cache,
                             Bool *purged_something, size_t purge)
{
  blobdata_t *blob_data, **blob_prev ;
  size_t orig_size, target_size ;
  blobdata_cache_t *cache = (blobdata_cache_t*)mm_cache;

  HQASSERT(cache, "No blob cache instance to purge") ;
  blob_prev = &cache->blob_data ;
  orig_size = cache->memory.data_size;

  HQTRACE(!blobdata_sanity_check(), ("blob data inconsistent before purge")) ;

  /* Free unused blocks, and unused blob data entries. Quite a few blobdata
     entries based on file objects will be retained through the purge levels,
     because these are slow to regenerate. Entries based on strings may
     represent files, and will not have blocks allocated, so they are
     retained with the same duration as files. Entries based on arrays can be
     easily copied again from PostScript, and entries with no blocks can
     always be removed. */
  target_size = orig_size - purge;
  cache->memory.data_size = 0;

  while ( (blob_data = *blob_prev) != NULL ) {
    blobdata_block_t *block, **block_prev = &blob_data->blocks ;
    Bool always ;

    VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

    /* These data source types can always be purged. */
    always = (blob_data->source == NULL ||
              oType(*blob_data->source) == OSTRING ||
              oType(*blob_data->source) == OLONGSTRING ||
              oType(*blob_data->source) == OARRAY ||
              oType(*blob_data->source) == OPACKEDARRAY) ;

    while ( (block = *block_prev) != NULL ) {
      size_t size ;

      VERIFY_OBJECT(block, BLOB_BLOCK_NAME) ;
      BLOCK_ORDER_ASSERT(block) ;

      /* Purge the block if we have retained enough already, or we can
         easily regenerate this data source type, and the block is not
         currently in use. */
      size = BLOBDATA_BLOCK_SIZE(block) ;
      if ( (always || cache->memory.data_size >= target_size) &&
           (block->lock != cache->data_lock || !blob_data->inuse) ) {
        *block_prev = block->next ;
        UNNAME_OBJECT(block) ;
        mm_free(cache->memory.pool, block, size);
        --blob_data->nblocks ;
      } else {
        cache->memory.data_size += size;
        block_prev = &block->next ;
      }

      SwOftenUnsafe() ;
    }

    /* Don't collect top-level structure if it has blocks, or is in use. */
    if ( !blob_data->inuse && blob_data->blocks == NULL ) {
      HQASSERT(blob_data->blocks == NULL && blob_data->nblocks == 0,
               "blob data not in use but blocks still exist") ;
      *blob_prev = blob_data->next ;

      if ( blob_data->source != NULL ) {
        (*blob_data->methods->destroy)(blob_data->source, &blob_data->data) ;
        HQASSERT(blob_data->data == NULL,
                 "blob data destroy method failed to clean up private data") ;
      }

      UNNAME_OBJECT(blob_data) ;
      mm_free(cache->memory.pool, blob_data, sizeof(blobdata_t));
      blob_cache_destroy(&cache) ;
      HQASSERT(cache, "Blob cache refcounting broken; lost active cache") ;
    } else {
      cache->memory.data_size += sizeof(blobdata_t);
      blob_prev = &blob_data->next ;
    }

    SwOftenUnsafe() ;
  }
  HQTRACE(!blobdata_sanity_check(), ("blob data inconsistent after purge")) ;
  HQASSERT(orig_size >= cache->memory.data_size, "Inconsistent blob data size") ;
  *purged_something = orig_size > cache->memory.data_size;
  return TRUE;
}


/* Set the blob data cache limit. This is not a hard limit, it is the level
   at which the cache will start poaching blocks from other fonts if
   possible. */
void blob_cache_set_limit(blobdata_cache_t *cache, size_t limit)
{
  Bool dummy;

  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;

  /* If we have more than this amount currently stored, purge it. */
  if ( cache->memory.data_size > limit )
    (void)blob_cache_purge(&cache->memory,
                           &dummy, cache->memory.data_size - limit);
  cache->data_limit = limit ;
}

/* Return the blob data cache limit. */
size_t blob_cache_get_limit(blobdata_cache_t *cache)
{
  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;

  return cache->data_limit ;
}


/* ----------------------------------------------------------------------- */

/* This is an internal hook for the VM system to call the restored methods
   of cached data. */
void blob_restore_commit(int32 savelevel)
{
  int32 numsaves = NUMBERSAVES(savelevel) ;
  blobdata_cache_t *cache ;

  for ( cache = blob_data_caches ; cache != NULL ; cache = cache->next ) {
    blobdata_t *blob_data, **blob_prev = &cache->blob_data ;

    HQTRACE(!blobdata_sanity_check(),
            ("blob data inconsistent before restore")) ;

    /* Free all unused blocks, and unused blob data entries. */
    while ( (blob_data = *blob_prev) != NULL ) {
      VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

      /* Test if the data source will be restored. Force a restore attempt when
         at the global save sevel, so that blob data loaded during startup
         doesn't hang around for ever unused. */
      if ( blob_data->source != NULL &&
           (numsaves <= MAXGLOBALSAVELEVEL ||
            mm_ps_check(numsaves, blob_data->source) != MM_SUCCESS) ) {
        OBJECT *restored ;

        /* See if the methods provide a way of retaining the data. If the
           source is non-null after this call, then we can keep the data. */
        restored = (*blob_data->methods->restored)(blob_data->source,
                                                   blob_data->data,
                                                   savelevel) ;

        if ( restored == NULL ) {
          blobdata_block_t *block ;

          /* Source is about to be restored, so delete any referenced
             blocks. */
          while ( (block = blob_data->blocks) != NULL ) {
            size_t size = BLOBDATA_BLOCK_SIZE(block) ;

            VERIFY_OBJECT(block, BLOB_BLOCK_NAME) ;
            BLOCK_ORDER_ASSERT(block) ;
            HQASSERT(block->lock != cache->data_lock,
                     "Source data restored but font block still in use") ;

            blob_data->blocks = block->next ;
            UNNAME_OBJECT(block) ;
            mm_free(cache->memory.pool, block, size);
            cache->memory.data_size -= size ;
            --blob_data->nblocks ;

            SwOftenUnsafe() ;
          }

          HQASSERT(blob_data->nblocks == 0 && blob_data->blocks == NULL,
                   "Block count does not match block list") ;

          (*blob_data->methods->destroy)(blob_data->source, &blob_data->data) ;
          HQASSERT(blob_data->data == NULL,
                   "blob data destroy method failed to clean up private data") ;

          if ( !blob_data->inuse ) {
            *blob_prev = blob_data->next ;
            UNNAME_OBJECT(blob_data) ;
            mm_free(cache->memory.pool, blob_data, sizeof(blobdata_t));
            cache->memory.data_size -= sizeof(blobdata_t);
            blob_cache_destroy(&cache) ;
            HQASSERT(cache, "Blob cache refcounting broken; lost active cache") ;
          } else {
            /* Mark source as restored. */
            blob_data->source = NULL ;
            blob_prev = &blob_data->next ;
          }
        } else {
          /* We said we could keep the data. Check that we didn't lie. */
          HQASSERT(mm_ps_check(numsaves, restored) == MM_SUCCESS,
                   "blob data source will be restored") ;
          blob_data->source = restored ;
          blob_prev = &blob_data->next ;
        }
      } else {
        blob_prev = &blob_data->next ;
      }

      SwOftenUnsafe() ;
    }

    /* If we've restored past the global save level, then no object should be
       reachable, and we should have restored all blob data. */
    HQASSERT(numsaves > MAXGLOBALSAVELEVEL || cache->memory.data_size == 0,
             "Global restore did not clear all blob data") ;
  }

  HQTRACE(!blobdata_sanity_check(), ("blob data inconsistent after restore")) ;

}

/* This is an internal hook for the VM system to perform a GC scan of the blob
   data caches. */
mps_res_t MPS_CALL blobdata_scan(mps_ss_t ss, void *p, size_t s)
{
  blobdata_cache_t *cache = p ;
  blobdata_t *blob_data ;

  UNUSED_PARAM(size_t, s);

  HQTRACE(!blobdata_sanity_check(), ("blob data inconsistent before scan")) ;

  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;

  MPS_SCAN_BEGIN( ss )
    for ( blob_data = cache->blob_data ; blob_data ; blob_data = blob_data->next ) {
      VERIFY_OBJECT(blob_data, BLOB_DATA_NAME);
      /* Fix the blob data source objects, so they won't be collected. */
      MPS_RETAIN( &blob_data->source, TRUE );
    }
  MPS_SCAN_END( ss );

  return MPS_RES_OK ;
}


/** \brief Singleton blob data cache. */
blobdata_cache_t *global_blob_store = NULL ;

static void init_C_globals_blobdata(void)
{
  global_blob_store = NULL ;
  blob_data_caches = NULL ;

#ifdef ASSERT_BUILD
  blobdata_trace = 0 ;
  full_sanity_checking = FALSE ;
#endif
}

static Bool blobdata_swstart(SWSTART *params)
{
  static mm_cost_t cost = { memory_tier_disk, 5.0f };
  UNUSED_PARAM(SWSTART *, params) ;

  if ( (global_blob_store =
        blob_cache_init("Global blob store",
                        1024u * 1024u, /* 1 MB data before purging */
                        FILEBUFFSIZE, /* Block allocation size */
                        FILEBUFFSIZE, /* Block quantum */
                        1,            /* Retained closed blob limit */
                        cost,
                        FALSE, /* not mt-safe, renderers use WCS blobs */
                        mm_pool_temp  /* Pool for allocations */
                        )) == NULL )
    return FALSE ;
  return TRUE ;
}

static void blobdata_finish(void)
{
  if ( global_blob_store != NULL ) {
    blob_cache_destroy(&global_blob_store) ;
    HQASSERT(global_blob_store == NULL,
             "Blob refcounting may be broken, or blob hasn't been closed") ;
  }
}

void blobdata_C_globals(core_init_fns *fns)
{
  init_C_globals_blobdata() ;

  fns->swstart = blobdata_swstart ;
  fns->finish = blobdata_finish ;
}

/* ----------------------------------------------------------------------- */
/* Functions to translate blob interface to blob data cache. */

/** Create a new blob structure, and link it to a blob store. This routine is
    used by the sw_blob_api_objects::open and
    sw_blob_factory_objects::open_named methods, and also by \c
    blob_from_object_with_methods(), which creates a blob ab initio for the
    RIP to hand out to modules.

    This routine takes opens a blob data source even if it exists (this is
    necessary to merge the mode flags), and then creates a blob structure
    for it.
*/
static sw_blob_result obj_blob_create(OBJECT *source, int32 mode,
                                      blobdata_cache_t *cache,
                                      const blobdata_methods_t *methods,
                                      obj_blob **blob)
{
  obj_blob *new_blob ;
  blobdata_t *data ;
  sw_blob_result result ;

  HQASSERT(cache, "No blob data cache when creating new blob") ;
  HQASSERT(source, "No data source when creating new blob") ;
  HQASSERT(methods, "No data access methods when creating new blob") ;
  HQASSERT(blob, "Nowhere to put new blob when creating new blob") ;

  if ( (result = blobdata_open(cache, source, methods, mode, &data)) != SW_BLOB_OK )
    return result ;

  HQASSERT(sizeof(obj_blob) == sw_blob_api_objects.info.instance_size,
           "Blob subclass size doesn't match implementation instance size") ;
  if ( (new_blob = mm_alloc(cache->memory.pool, sizeof(obj_blob),
                            MM_ALLOC_CLASS_SW_BLOB)) == NULL ) {
    blobdata_close(&data) ;
    return FAILURE(SW_BLOB_ERROR_MEMORY) ;
  }

  new_blob->super.implementation = &sw_blob_api_objects ;
  new_blob->data = data ;
  new_blob->cache = cache ;
  Hq32x2FromUint32(&new_blob->currentptr, 0u) ;
  new_blob->mode = mode ;
  new_blob->refcount = 1 ;
  new_blob->mapped = NULL ;
  NAME_OBJECT(new_blob, SW_BLOB_NAME) ;

  /* Count reference from cache to blob. */
  ++cache->refcount ;
  HQASSERT(cache->refcount > 0, "Blob data cache refcount wrapped") ;

  *blob = new_blob ;

  return SW_BLOB_OK ;
}

static void RIPCALL objblob_map_close(/*@in@*/ /*@notnull@*/ sw_blob_map **map) ;


/** \brief Create another reference for this blob. We will inherit the
    initial value of the file pointer from the current instance, however the
    memory mappings will not be inherited. */
static sw_blob_result RIPCALL objblob_open(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                           int mode,
                                           /*@out@*/ /*@notnull@*/ sw_blob_instance **blob)
{
  /* Downcast to blob subclass */
  obj_blob *old_blob = (obj_blob *)instance ;
  obj_blob *new_blob ;
  blobdata_t *source ;
  blobdata_cache_t *cache ;
  sw_blob_result result ;

  if ( old_blob == NULL || blob == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(old_blob, SW_BLOB_NAME) ;

  /* Only supported flags set. */
  /** \todo Should we support SW_APPEND, SW_TRUNC, SW_CREAT? */
  if ( (mode & ~(SW_RDONLY|SW_WRONLY|SW_RDWR|SW_FONT|SW_EXCL)) != 0 )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  cache = old_blob->cache ;
  HQASSERT(cache, "No blobdata cache for source") ;

  source = old_blob->data ;
  HQASSERT(source, "No data source in old blob") ;

  /* The data source object may have been reclaimed by restore or GC. */
  if ( source->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  if ( (result = obj_blob_create(source->source, mode, cache,
                                 source->methods, &new_blob)) != SW_BLOB_OK )
    return result ;

  HQASSERT(new_blob, "No blob returned, but success reported") ;

  /* Inherit the current file position from the existing blob. */
  new_blob->currentptr = old_blob->currentptr ;

  *blob = &new_blob->super ;

  return SW_BLOB_OK ;
}

/** \brief Close a blob reference. Note that the memory doesn't disappear until
    the last map is removed. */
static void RIPCALL objblob_close(/*@in@*/ /*@notnull@*/ sw_blob_instance **where)
{
  /* Downcast handle pointer to blob subclass */
  obj_blob *blob, **prev = (obj_blob **)where ;

  HQASSERT(prev, "Nowhere to find blob") ;

  blob = *prev ;
  HQASSERT(blob != NULL, "No blob to free") ;

  HQASSERT(blob->refcount > 0, "Blob refcounting broken; blob already zero") ;
  if ( --blob->refcount == 0 ) {
    blobdata_cache_t *cache = blob->cache ;

    HQASSERT(blob->mapped == NULL, "Blob mappings present but refcount is zero") ;

    blobdata_close(&blob->data) ;

    mm_free(cache->memory.pool, blob, sizeof(obj_blob));
    blob_cache_destroy(&cache) ;
    HQASSERT(cache, "Blob cache refcounting broken; lost active cache") ;

    *prev = NULL ;
  }
}

/** \brief Get the length of a blob. */
static sw_blob_result RIPCALL objblob_length(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                             /*@out@*/ /*@notnull@*/ Hq32x2 *bytes)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;
  const blobdata_methods_t *methods ;

  if ( blob == NULL || bytes == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  methods = blob_data->methods ;
  HQASSERT(methods && methods->length, "No blob data length method") ;

  /* Rely on the underlying data provider to cache the length, if hard to
     obtain. */
  return (*methods->length)(blob_data->source, blob_data->data, bytes) ;
}

/** \brief Read a buffer of data from the current position in a blob. */
static sw_blob_result RIPCALL objblob_read(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                           /*@out@*/ /*@notnull@*/ void *buffer, size_t byteswanted,
                                           /*@out@*/ /*@notnull@*/ size_t *bytesread)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;
  const blobdata_methods_t *methods ;
  Hq32x2 hq32read ;

  if ( blob == NULL || buffer == NULL || bytesread == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  if ( (blob->mode & (SW_RDONLY|SW_RDWR)) == 0 )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  methods = blob_data->methods ;
  HQASSERT(methods && methods->read, "No blob data read method") ;

  /** \todo For now, ignore the mapped blocks, which may contain the data we
      want, and read the data directly from the original source. This may be
      a suitable strategy long-term, because clients will tend to use only
      one of the memory mapping calls or the file-like calls. */
  if ( (*bytesread = (*methods->read)(blob_data->source, blob_data->data,
                                      buffer, blob->currentptr,
                                      byteswanted)) == 0 )
    return FAILURE(SW_BLOB_ERROR_EOF) ;

  /* Update current pointer with bytes read. Note that the current pointer is
     independent for each blob accessing the same source. */
  Hq32x2FromSize_t(&hq32read, *bytesread) ;
  Hq32x2Add(&blob->currentptr, &blob->currentptr, &hq32read) ;

  return SW_BLOB_OK ;
}

/** \brief Write a buffer of data to the current position in a blob. */
static sw_blob_result RIPCALL objblob_write(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                            /*@in@*/ /*@notnull@*/ void *buffer, size_t bytestowrite)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;
  const blobdata_methods_t *methods ;
  sw_blob_result result ;
  Hq32x2 hq32write ;

  if ( blob == NULL || buffer == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  if ( (blob->mode & (SW_WRONLY|SW_RDWR)) == 0 )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  /* If there are any mapped blocks in use on the blob, then we cannot write
     to the destination. */
  if ( blob_data->nblocks > 0 )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  methods = blob_data->methods ;
  HQASSERT(methods && methods->write, "No blob data write method") ;

  if ( (result = (*methods->write)(blob_data->source, blob_data->data,
                                   buffer, blob->currentptr, bytestowrite)) != SW_BLOB_OK )
    return result ;

  /* Update current pointer with bytes written. Note that the current pointer
     is independent for each blob accessing the same source. */
  Hq32x2FromSize_t(&hq32write, bytestowrite) ;
  Hq32x2Add(&blob->currentptr, &blob->currentptr, &hq32write) ;

  return SW_BLOB_OK ;
}

/** \brief Adjust the blob's current position. */
static sw_blob_result RIPCALL objblob_seek(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                           Hq32x2 where, int offset)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;
  Hq32x2 length, extra ;
  sw_blob_result result ;

  /* We need the length of the blob regardless, to compare the modified
     pointer with. */
  if ( (result = objblob_length(instance, &length)) != SW_BLOB_OK )
    return result ;

  /* objblob_length has done the basic type and sanity checking for us. */
  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;
  HQASSERT(blob_data->source != NULL, "Blob source has expired") ;

  switch ( offset ) {
  case SW_INCR:
    /* Increment current pointer. */
    Hq32x2Add(&where, &where, &blob->currentptr) ;
    break ;
  case SW_SET:
    break ;
  case SW_XTND:
    Hq32x2Add(&where, &where, &length) ;
    break ;
  default:
    return FAILURE(SW_BLOB_ERROR_INVALID) ;
  }

  /* Is new pointer position less than zero? */
  if ( Hq32x2Sign(&where) < 0 )
    return FAILURE(SW_BLOB_ERROR_EOF) ;

  /* Are we seeking past the end of the blob? */
  Hq32x2Subtract(&extra, &where, &length) ;
  if ( Hq32x2CompareInt32(&extra, 0) > 0 ) {
    const blobdata_methods_t *methods ;
#define BLOB_EXTEND_ZEROS 1024u
    uint32 extend = BLOB_EXTEND_ZEROS ;
    uint8 zeros[BLOB_EXTEND_ZEROS] ;

    /* If blob isn't writable, cannot seek beyond end. We can seek beyond the
       end with blocks mapped, because the write cannot change the contents of
       any of those blocks. */
    if ( (blob_data->mode & (SW_WRONLY|SW_RDWR)) == 0 )
      return FAILURE(SW_BLOB_ERROR_EOF) ;

    /* Don't use basemap for the zero block. We don't know if we'll be called
       while a lock already exists on it. */
    HqMemZero(zeros, BLOB_EXTEND_ZEROS) ;

    methods = blob_data->methods ;
    HQASSERT(methods && methods->write, "No blob data write method") ;

    do {
      /* Find the maximum amount to extend the blob in this iteration. */
      if ( Hq32x2CompareUint32(&extra, extend) < 0 ) {
        Bool ok = Hq32x2ToUint32(&extra, &extend) ;
        UNUSED_PARAM(Bool, ok) ;
        HQASSERT(ok, "Should have been able to fit extra in uint32") ;
      }

      /* Write zero buffer to the end of the blob. */
      if ( (result = (*methods->write)(blob_data->source, blob_data->data,
                                       zeros, length, (size_t)extend)) != SW_BLOB_OK )
        return result ;

      /* Update the remaining amount and the end position. */
      Hq32x2SubtractUint32(&extra, &extra, extend) ;
      Hq32x2AddUint32(&length, &length, extend) ;
    } while ( !Hq32x2IsZero(&extra) ) ;

    HQASSERT(Hq32x2Compare(&length, &where) == 0,
             "Seek position should be same as new blob length") ;
  }

  blob->currentptr = where ;

  return SW_BLOB_OK ;
}

/** \brief Return the current position in the blob. */
static sw_blob_result RIPCALL objblob_tell(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                           /*@out@*/ /*@notnull@*/ Hq32x2 *where)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;

  if ( blob == NULL || where == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  *where = blob->currentptr ;

  return SW_BLOB_OK ;
}

/** \brief Return the type of protection that the blob data uses. */
static sw_blob_result RIPCALL objblob_protection(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                                 /*@out@*/ /*@notnull@*/ sw_blob_protection *protection)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;
  const blobdata_methods_t *methods ;

  if ( blob == NULL || protection == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  methods = blob_data->methods ;
  HQASSERT(methods && methods->protection, "No blob data protection method") ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  *protection = (*methods->protection)(blob_data->source, blob_data->data) ;

  return SW_BLOB_OK ;
}

/** \brief Open a memory map context for a blob. */
static sw_blob_result RIPCALL objblob_map_open(/*@in@*/ /*@notnull@*/ sw_blob_instance *instance,
                                               /*@out@*/ /*@notnull@*/ sw_blob_map **map)
{
  /* Downcast to blob subclass */
  obj_blob *blob = (obj_blob *)instance ;
  blobdata_t *blob_data ;
  blobdata_cache_t *cache ;
  obj_blob_map *mapping ;

  if ( blob == NULL || map == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  if ( (blob->mode & (SW_RDONLY|SW_RDWR)) == 0 )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( (mapping = mm_alloc(blob->cache->memory.pool, sizeof(obj_blob_map),
                           MM_ALLOC_CLASS_SW_BLOB_MAP)) == NULL )
    return FAILURE(SW_BLOB_ERROR_MEMORY) ;

  /* Track the number of data maps open */
  cache = blob->cache ;
  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;
  ++cache->data_maps_open ;

  /* Can't free blob until map is closed. */
  ++blob->refcount ;

  /* There's nothing actually stored in a blob map, it's just used to track
     the open mapping contexts. It may end up being used to track the blocks
     allocated for each blob data in future, to improve the low-memory
     purging ability. */
  mapping->super.implementation = blob->super.implementation ;
  mapping->blob = blob ;
  mapping->next = blob->mapped ;
  blob->mapped = mapping ;
  NAME_OBJECT(mapping, SW_BLOB_MAP_NAME) ;

  *map = &mapping->super ;

  return SW_BLOB_OK ;
}

/** \brief Map a section of a blob into a contiguous block of memory. */
static sw_blob_result RIPCALL objblob_map_region(/*@in@*/ /*@notnull@*/ sw_blob_map *instance,
                                                 Hq32x2 start, size_t length, size_t alignment,
                                                 /*@out@*/ /*@notnull@*/ uint8 **mapping)
{
  /* Downcast to blob map subclass */
  obj_blob_map *map = (obj_blob_map *)instance ;
  obj_blob *blob ;
  blobdata_t *blob_data ;

  if ( map == NULL || mapping == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  if ( alignment == 0 ||                     /* Alignment is positive */
       (alignment & (alignment - 1)) != 0 || /* and is a power of 2 */
       alignment > BLOB_MAX_ALIGNMENT )      /* and is within range */
    return FAILURE(SW_BLOB_ERROR_INVALID) ;  /* or it's an error. */

  VERIFY_OBJECT(map, SW_BLOB_MAP_NAME) ;

  blob = map->blob ;
  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  if ( blob_data->source == NULL )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  if ( (*mapping = blobdata_frame(blob_data, start, length, alignment)) == NULL )
    return SW_BLOB_ERROR_MEMORY ;

  return SW_BLOB_OK ;
}

/** \brief Close a memory mapping context for a blob, returning any
    allocated buffers to the RIP. */
static void RIPCALL objblob_map_close(/*@in@*/ /*@notnull@*/ sw_blob_map **map)
{
  /* Downcast handle pointer to blob subclass */
  obj_blob_map *curr, **prev = (obj_blob_map **)map ;
  obj_blob *blob ;
  blobdata_t *blob_data ;
  blobdata_cache_t *cache ;

  if ( prev == NULL )
    return ;

  curr = *prev ;
  if ( curr == NULL )
    return ;

  VERIFY_OBJECT(curr, SW_BLOB_MAP_NAME) ;

  blob = curr->blob ;
  VERIFY_OBJECT(blob, SW_BLOB_NAME) ;

  blob_data = blob->data ;
  VERIFY_OBJECT(blob_data, BLOB_DATA_NAME) ;

  cache = blob->cache ;
  VERIFY_OBJECT(cache, BLOB_CACHE_NAME) ;

  /* Yuck, I'd prefer to use a doubly-linked list for quick removal, but I'm
     not wrapping it myself. There won't be that many maps (most likely one
     per blob), so a single list isn't too time-consuming. */
  for ( prev = &blob->mapped ; (curr = *prev) != NULL ; prev = &curr->next ) {
    VERIFY_OBJECT(curr, SW_BLOB_MAP_NAME) ;

    if ( &curr->super == *map ) {
      /* We could unlock all blocks for this blobdata when the inuse count
         reaches zero, but that would involve iterating over the blocks. It's
         fairly rare to have multiple blobdata objects open, so we'll just
         unlock when all of them are closed. */
      if ( --cache->data_maps_open == 0 )
        ++cache->data_lock ;

      *prev = curr->next ;
      mm_free(cache->memory.pool, curr, sizeof(obj_blob_map));

      /* Free reference to blob from map. */
      objblob_close((sw_blob_instance **)(void *)&blob);

      return ;
    }
  }

  HQFAIL("Blob map was not found on blob mapping list") ;
}

/* The fileio/data cache blob API definition. */
const sw_blob_api sw_blob_api_objects = {
  {
    SW_BLOB_API_VERSION_20071115,
    (const uint8 *)"swblob",
    UVS("Harlequin RIP blob API"),
    sizeof(obj_blob) /* Object-based blob instance size */
  },
  objblob_open,
  objblob_close,
  objblob_length,
  objblob_read,
  objblob_write,
  objblob_seek,
  objblob_tell,
  objblob_protection,
  objblob_map_open,
  objblob_map_region,
  objblob_map_close
} ;

/* ----------------------------------------------------------------------- */

/* Set up a blob source from a block of memory. */
sw_blob_result blob_from_memory(void *memory, size_t size, int32 mode,
                                blobdata_cache_t *store,
                                sw_blob_instance **blob)
{
  OBJECT string = OBJECT_NOTVM_NOTHING ;
  uint8 access ;

  HQASSERT(memory != NULL, "No memory to initialise blob source from") ;
  HQASSERT(store != NULL, "Blob cache is not yet initialised") ;
  HQASSERT(blob != NULL, "Nowhere to put blob") ;

  /* At least one of the access tags must be specified. */
  if ( (mode & (SW_RDWR|SW_WRONLY|SW_RDONLY)) == 0 ) {
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;
  }

  /* The object access tags are not the primary determinant of the access
     enabled. */
  if ( (mode & (SW_RDWR|SW_WRONLY)) != 0 )
    access = UNLIMITED ;
  else
    access = READ_ONLY ;

  if ( size <= MAXPSSTRING ) {
    theTags(string) = OSTRING | LITERAL | access ;
    theLen(string) = CAST_UNSIGNED_TO_UINT16(size) ;
    if ( size != 0 )
      oString(string) = memory ;
    else
      oString(string) = NULL ;
  } else {
    LONGSTR *longstr ;

    HQASSERT(size <= MAXINT32, "Memory blob too long for OLONGSTRING") ;

    if ( (longstr = (LONGSTR *)get_lsmemory(sizeof(LONGSTR))) == NULL )
      return FAILURE(SW_BLOB_ERROR_MEMORY);

    theTags(string) = OLONGSTRING | LITERAL | access ;
    theLen(string) = 0 ;
    oLongStr(string) = longstr ;
    theLSLen(*longstr) = CAST_UNSIGNED_TO_INT32(size) ;
    theLSCList(*longstr) = memory ;
  }

  SETGLOBJECTTO(string, FALSE) ;

  return blob_from_object(&string, mode, store, blob) ;
}

/* Set up a blob source from a FILELIST. */
sw_blob_result blob_from_file(FILELIST *file, int32 mode,
                              blobdata_cache_t *store,
                              sw_blob_instance **blob)
{
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;

  HQASSERT(file != NULL, "No file to initialise blob source from") ;
  HQASSERT(store != NULL, "Blob cache is not yet initialised") ;
  HQASSERT(blob != NULL, "Nowhere to put blob") ;

  file_store_object(&fileo, file, LITERAL) ;

  return blob_from_object(&fileo, mode, store, blob) ;
}

/* Set up a blob source from an OBJECT (OSTRING, OLONGSTRING or OFILE). */
sw_blob_result blob_from_object(OBJECT *object, int32 mode,
                                blobdata_cache_t *store,
                                sw_blob_instance **blob)
{
  const blobdata_methods_t *methods ;

  HQASSERT(object != NULL, "No object to initialise blob source from") ;
  HQASSERT(store != NULL, "Blob cache is not yet initialised") ;
  HQASSERT(blob != NULL, "Nowhere to put blob") ;

  switch ( oType(*object) ) {
  case OSTRING:
    methods = &blobdata_string_methods ;
    break ;
  case OLONGSTRING:
    methods = &blobdata_longstring_methods ;
    break ;
  case OFILE:
    methods = &blobdata_file_methods ;
    break ;
  case OARRAY:
    methods = &blobdata_array_methods ;
    break ;
  default:
    return FAILURE(SW_BLOB_ERROR_INVALID) ;
  }

  return blob_from_object_with_methods(object, mode, store, methods, blob) ;
}

/* Set up a blob source from an OBJECT (OSTRING, OLONGSTRING or OFILE). */
sw_blob_result blob_from_object_with_methods(OBJECT *object, int32 mode,
                                             blobdata_cache_t *store,
                                             const blobdata_methods_t *methods,
                                             sw_blob_instance **blob)
{
  obj_blob *new_blob ;
  sw_blob_result result ;

  HQASSERT(object != NULL, "No object to initialise blob source from") ;
  HQASSERT(store != NULL, "Blob cache is not yet initialised") ;
  HQASSERT(methods != NULL, "No methods to access blob data") ;
  HQASSERT(blob != NULL, "Nowhere to put blob") ;

  /* The swdevice open tags are a mess, they have a separate bit for SW_RDWR.
     We won't insist that they are exclusive, and we'll also default the
     access mode from the object. */
  if ( (mode & (SW_RDWR|SW_WRONLY|SW_RDONLY)) == 0 ) {
    /* If the object is a file, get the more specific mode from the file
       object itself. Otherwise fallback to the PostScript permissions. */
    if ( oType(*object) == OFILE ) {
      FILELIST *file = oFile(*object) ;

      if ( theIFlags(file) & READ_FLAG ) {
        if ( theIFlags(file) & WRITE_FLAG )
          mode = SW_RDWR ;
        else
          mode = SW_RDONLY ;
      } else if ( theIFlags(file) & WRITE_FLAG ) {
          mode = SW_WRONLY ;
      } else {
        return FAILURE(SW_BLOB_ERROR_ACCESS) ;
      }
    } else {
      if ( oCanRead(*object) ) {
        mode = SW_RDWR ;
      } else if ( oCanWrite(*object) ) {
        mode = SW_RDONLY ;
      } else {
        return FAILURE(SW_BLOB_ERROR_ACCESS) ;
      }
    }
  }

  HQASSERT((mode & (SW_RDONLY|SW_WRONLY|SW_RDWR)) != 0,
           "No access mode set for blob") ;

  if ( (result = obj_blob_create(object, mode, store, methods, &new_blob)) != SW_BLOB_OK )
    return result ;

  HQASSERT(new_blob, "Reported success but no blob") ;

  *blob = &new_blob->super ;

  return SW_BLOB_OK ;
}

/*---------------------------------------------------------------------------*/

/** \brief Open a blob based upon a file handle found on a device. */
static sw_blob_result RIPCALL objblob_open_named(
  /*@in@*/ /*@notnull@*/ sw_blob_factory_instance *instance,
  /*@in@*/ /*@notnull@*/ uint8* name, size_t name_len,
  int mode,
  /*@out@*/ /*@notnull@*/ sw_blob_instance **blob)
{
  OBJECT filename = OBJECT_NOTVM_NOTHING, fileobject = OBJECT_NOTVM_NOTHING ;
  int32 psflags ;

  if ( instance != &sw_blob_factory_objects ||
       name == NULL || name_len > LONGESTFILENAME ||
       blob == NULL )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  /* Filename will be copied inside file_open, so a stack-allocated object
     is OK against save/restore and GC. */
  theTags(filename) = OSTRING | READ_ONLY | LITERAL ;
  SETGLOBJECTTO(filename, TRUE) ;
  theLen(filename) = CAST_UNSIGNED_TO_UINT16(name_len) ;
  oString(filename) = name ;

  psflags = 0 ;
  if ( (mode & (SW_RDWR|SW_RDONLY)) != 0 )
    psflags |= READ_FLAG ;
  if ( (mode & (SW_RDWR|SW_WRONLY)) != 0 )
    psflags |= WRITE_FLAG ;

  if ( !file_open(&filename, mode, psflags,
                  (mode & SW_APPEND) != 0 /*append*/,
                  0 /*baseflag*/, &fileobject) )
    return SW_BLOB_ERROR_ACCESS ;

  return blob_from_object(&fileobject, mode, global_blob_store, blob) ;
}

/* Forward typedef to ensure that the object blob factory instance size is
   correct. We don't subclass the factory instance for this blob factory, but
   this typedef will ensure the right size is used if we ever do want to. */
typedef sw_blob_factory_instance obj_blob_factory ;

/* The blob factory API implementation, opening new blob instances from named
   files on the RIP's filesystem. The new blob instances use the
   sw_blob_api_objects implementation. */
const static sw_blob_factory_api obj_blob_factory_implementation = {
  {
    SW_BLOB_FACTORY_API_VERSION_20071116,
    (const uint8 *)"swblobfactory",
    UVS("Harlequin RIP filesystem blob factory"),
    sizeof(obj_blob_factory) /* Instance size */
  },
  objblob_open_named
} ;

/* We don't subclass the factory instance for the sw_blob_factory_objects
   factory. The open_named() method uses blob_from_objects(), which is not
   parameterised on the blob implementation, so this is a singleton. */
const obj_blob_factory sw_blob_factory_objects = {
  &obj_blob_factory_implementation
} ;

/*---------------------------------------------------------------------------*/

/* Log stripped */
