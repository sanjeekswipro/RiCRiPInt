/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:cache_handler.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.

 * \brief Low-memory handling service for a simple cache.
 */

#include "core.h"
#include "cache_handler.h"
#include "lowmem.h"


/** Solicit method of a simple cache low-memory handler. */
static low_mem_offer_t *mm_simple_cache_solicit(low_mem_handler_t *handler,
                                                 corecontext_t *context,
                                                 size_t count,
                                                 memory_requirement_t* requests)
{
  mm_simple_cache_t *cache;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  cache = (mm_simple_cache_t *)handler;
  if ( !context->between_operators || cache->data_size == 0
       || (!cache->last_purge_worked
           && cache->data_size == cache->last_data_size) )
    return NULL;

  cache->offer.pool = cache->pool;
  cache->offer.offer_size = cache->data_size;
  cache->offer.offer_cost = cache->cost;
  cache->offer.next = NULL;
  return &cache->offer;
}


/** Release method of a simple cache low-memory handler. */
static Bool mm_simple_cache_release(low_mem_handler_t *handler,
                                     corecontext_t *context,
                                     low_mem_offer_t *offer)
{
  mm_simple_cache_t *cache;
  Bool res;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(corecontext_t*, context);

  cache = (mm_simple_cache_t *)handler;
  res = cache->purge(cache, &cache->last_purge_worked,
                     min(offer->taken_size, cache->data_size));
  cache->last_data_size = cache->data_size;
  return res;
}


const low_mem_handler_t mm_simple_cache_handler_init = {
  "",
  memory_tier_disk, mm_simple_cache_solicit, mm_simple_cache_release, FALSE,
  0, FALSE };


/* Log stripped */
