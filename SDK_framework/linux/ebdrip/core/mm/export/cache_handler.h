/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:cache_handler.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.

 * \brief Interface for a low-memory handling service for a simple cache.
 */

#ifndef __CACHE_HANDLER_H__
#define __CACHE_HANDLER_H__

#include "mm.h" /* mm_pool_t */
#include "lowmem.h"


struct mm_simple_cache_t;


/** \brief Function type for purging a mm_simple_cache_t cache.

  This method should clear a given quantity of data from the cache, if possible.

  \param[in]  cache  The cache to purge.
  \param[out] purged_something  Indicates if anything was purged.
  \param[in]  purge  The amount to purge.
 */
typedef Bool mm_simple_purge_fn(struct mm_simple_cache_t *cache,
                                Bool *purged_something,
                                size_t purge);


/** State structure of a simple cache. */
typedef struct mm_simple_cache_t {
  /** Low memory handler for this cache. */
  low_mem_handler_t low_mem_handler; /* Must be first field! */
  /** Offer structure for the low memory handler to use. */
  low_mem_offer_t offer;
  /** Cost for purging the cache. */
  float cost;
  /** The current amount of data stored in the cache. */
  size_t data_size;
  /** The amount of data stored in the cache after last purge. */
  size_t last_data_size;
  /** Indicates that the last purge attempt managed to free something. */
  Bool last_purge_worked;
  /** Pool of the cache. */
  mm_pool_t pool;
  /** Purge function. */
  mm_simple_purge_fn *purge;
} mm_simple_cache_t;


/** Initializer for the handler of a mm_simple_cache_t. */
extern const low_mem_handler_t mm_simple_cache_handler_init;


#endif /* __CACHE_HANDLER_H__ */

/* Log stripped */
