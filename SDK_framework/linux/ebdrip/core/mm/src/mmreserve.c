/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmreserve.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Manage the MM reserve pool and arena extension.
 */

#include "core.h"
#include "mmreserve.h"
#include "mmlog.h"
#include "mm.h"
#include "lowmem.h"
#include "apportioner.h" /* low_mem_handle_guts */
#include "vm.h" /* fail_after_n */
#include "mps.h"
#include "mpscmvff.h"
#include "mlock.h"
#include "swtrace.h"
#include "monitor.h"
#include "hqassert.h"


Bool mm_memory_is_low;


/** The current commit limit. */
static size_t mm_commit_limit;


/** The initial commit limit. */
static size_t mm_commit_base;


/* == Reserve pool == */


#ifdef REGAIN_AT_FREE
/** Mutex for reserve pool and arena extension. */
static multi_mutex_t reserve_mutex;
#endif


/* mm_pool_reserve is the pool used to reserve memory, for use in low-memory
 * situations.
 * mm_reserve_level is the current reserve level; 0 => reserve not used.
 * mm_reserve_numlevels indicates the number of reserve levels available
 */
static mps_pool_t mm_pool_reserve = NULL;
static unsigned int mm_reserve_level;
static unsigned int mm_reserve_numlevels;


static Bool mm_reserve_get(mm_cost_t cost);


/** Block size for reserve memory
 *
 * It is allocated in n*64 kB chunks to match other pools. */
#define MM_RESERVE_BLOCK ( (size_t)64 * 1024 )


typedef struct {
  size_t          size;
  mm_addr_t       ptr;
  mm_cost_t cost;  /**< Cost for this level of extension */
} mm_reserve_t;

#define MM_RESERVE_MAXLEVELS 3
mm_reserve_t mm_reserve_table[MM_RESERVE_MAXLEVELS] = {
  { 8 * MM_RESERVE_BLOCK, NULL,
    { memory_tier_reserve_pool, 1.0f } },
  { 4 * MM_RESERVE_BLOCK, NULL,
    { memory_tier_reserve_pool, 1.0f } },
  { 8 * MM_RESERVE_BLOCK, NULL,
    { memory_tier_reserve_pool, 1e3f } },
};


/** Returns the size of the remaining reserved memory, not counting the final
    reserve. */
static size_t mm_reserve_size(void)
{
  size_t total = 0;
  unsigned level;

  for ( level = mm_reserve_level ; level < mm_reserve_numlevels-1 ; level++ )
    total += mm_reserve_table[level].size;
  return total;
}


/** Tries to reclaim as much of the reserve as is more valuable than the
    given cost. Returns success. */
static Bool mm_reserve_get(mm_cost_t limit)
{
  mps_res_t res;

  HQASSERT( mm_reserve_level >= 0 &&
            mm_reserve_level <= mm_reserve_numlevels,
            "mm_reserve_get: mm_reserve_level is invalid" );

  while ( mm_reserve_level > 0
          && mm_cost_less_than(limit,
                               mm_reserve_table[mm_reserve_level-1].cost) ) {
    res = mps_alloc( & mm_reserve_table[mm_reserve_level - 1].ptr,
                     mm_pool_reserve,
                     (size_t)mm_reserve_table[mm_reserve_level - 1].size );
    if ( res != MPS_RES_OK )
      return FALSE;
    mm_reserve_level--;
    MM_LOG(( LOG_RG, "%u 0x%08x 0x%08x",
             mm_reserve_level + 1,
             (unsigned)mm_reserve_table[mm_reserve_level].ptr,
             mm_reserve_table[mm_reserve_level].size ));
  }
  if ( mm_reserve_level == 0 && mm_commit_limit == mm_commit_base ) {
#if defined(DEBUG_BUILD)
    if ( debug_lowmemory_count > 0 )
      --debug_lowmemory_count;
    else
#endif
      mm_memory_is_low = FALSE;
  }
  return TRUE;
}


/** \brief Refill the reserve pool, down to the limit given, using only
    actions below the cost given.

 Must be called inside the low-memory synchronization. */
static Bool mm_regain_reserve_pool(Bool *enough, corecontext_t *context,
                                   mm_cost_t fill_limit, mm_cost_t cost)
{
  Bool retry_low_mem = TRUE /* pacify the compiler */;
  memory_requirement_t request;

  *enough = TRUE;
  request.pool = NULL; /* There's no mm pool, just an MPS pool */
  do {
    if ( mm_reserve_get(fill_limit) )
      return TRUE; /* got it now */
    /* @@@@ Change this to an array of requests. Or even return a
       request to be combine with original? */
    request.size = mm_reserve_table[mm_reserve_level-1].size;
    request.cost = mm_cost_min(mm_reserve_table[mm_reserve_level-1].cost, cost);
    if ( !low_mem_handle_guts(&retry_low_mem, context, 1, &request) ) {
      *enough = FALSE; return FALSE; /* error */
    }
  } while ( retry_low_mem );
  *enough = FALSE;
  return TRUE; /* didn't get it */
}


/** Solicit method of the reserve pool low-memory handler. */
static low_mem_offer_t *mm_reserve_solicit(low_mem_handler_t *handler,
                                           corecontext_t *context,
                                           size_t count,
                                           memory_requirement_t* requests)
{
  static low_mem_offer_t offer;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

#ifdef REGAIN_AT_FREE
  if ( !multi_mutex_trylock(&reserve_mutex) )
    return NULL; /* give up if can't get the lock */
  if ( mm_reserve_level == mm_reserve_numlevels ) {
    multi_mutex_unlock(&reserve_mutex);
    return NULL;
  }
#else
  if ( mm_reserve_level == mm_reserve_numlevels )
    return NULL;
#endif
  /* @@@@ could offer enough for requests, in a single block */
  offer.pool = NULL;
  offer.offer_size = mm_reserve_table[mm_reserve_level].size;
  offer.offer_cost = mm_reserve_table[mm_reserve_level].cost.value;
  offer.next = NULL;

#ifdef REGAIN_AT_FREE
  multi_mutex_unlock(&reserve_mutex);
#endif
  return &offer;
}


/** Release method of the reserve pool low-memory handler. */
static Bool mm_reserve_release(low_mem_handler_t *handler,
                               corecontext_t *context, low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(low_mem_offer_t*, offer);

#ifdef REGAIN_AT_FREE
  if ( !multi_mutex_trylock(&reserve_mutex) )
    return TRUE; /* give up if can't get the lock */
#endif
  /* Other threads could have reduced the reserve level since the offer,
     but this thread still needs memory, so release the current level. */
  MM_LOG(( LOG_RF, "%u 0x%08x 0x%08x",
           mm_reserve_level + 1,
           (unsigned)mm_reserve_table[mm_reserve_level].ptr,
           mm_reserve_table[mm_reserve_level].size ));
  mps_free( mm_pool_reserve,
            mm_reserve_table[mm_reserve_level].ptr,
            mm_reserve_table[mm_reserve_level].size );
  mm_reserve_table[mm_reserve_level].ptr = NULL;
  mm_reserve_level++;
  mm_memory_is_low = TRUE;
#ifdef REGAIN_AT_FREE
  multi_mutex_unlock(&reserve_mutex);
#endif
  return TRUE;
}


/** The reserve pool low-memory handler. */
static low_mem_handler_t reserve_pool_handler = {
  "Reserve pool use",
  memory_tier_reserve_pool, mm_reserve_solicit, mm_reserve_release, TRUE,
  0, FALSE };


Bool mm_reserve_create(void)
{
  mps_word_t sym;
  Bool res;
  unsigned level;

  HQASSERT(mm_arena, "mm_reserve_create called without valid mm_arena");
  HQASSERT(mm_pool_reserve == NULL,
           "mm_reserve_create called when mm_pool_reserve already created");

#ifdef REGAIN_AT_FREE
  multi_mutex_init(&reserve_mutex, RESERVE_LOCK_INDEX, FALSE,
                   SW_TRACE_RESERVE_ACQUIRE, SW_TRACE_RESERVE_HOLD);
#endif
  if ( mps_pool_create(&mm_pool_reserve, mm_arena,
                       mps_class_mvff(), DL_POOL_PARAMS, FALSE, FALSE, TRUE)
       != MPS_RES_OK ) {
#ifdef REGAIN_AT_FREE
    multi_mutex_destroy(&reserve_mutex) ;
#endif
    return FALSE;
  }

  mm_reserve_level =
    mm_reserve_numlevels = MM_RESERVE_MAXLEVELS; /* No reserve yet */
  for ( level = mm_reserve_level ; level < mm_reserve_numlevels-1 ; level++ )
    HQASSERT(mm_cost_less_than(mm_reserve_table[level].cost, mm_cost_normal),
             "Normal cost doesn't include reserve.");
  HQASSERT(!mm_cost_less_than(mm_reserve_table[mm_reserve_numlevels-1].cost,
                              mm_cost_normal),
           "Normal cost includes final reserve.");

  sym = mps_telemetry_intern("MM reserve pool");
  mps_telemetry_label((mps_addr_t)mm_pool_reserve, sym);

  res = mm_reserve_get(mm_cost_below_reserves);
  /* This effectively asserts mm_cost_below_reserves is small enough. */
  HQASSERT( res, "init: didn't get reserve!" );

  res = low_mem_handler_register(&reserve_pool_handler);
  if ( !res )
    mps_pool_destroy(mm_pool_reserve);
  return res;
}


void mm_reserve_destroy(void)
{
  unsigned level;

  if ( mm_pool_reserve != NULL ) {
    low_mem_handler_deregister(&reserve_pool_handler);
    mps_pool_destroy(mm_pool_reserve);
#ifdef REGAIN_AT_FREE
    multi_mutex_finish(&reserve_mutex);
#endif
  }
  mm_pool_reserve = NULL;
  for ( level = mm_reserve_level ; level < mm_reserve_numlevels ; level++ ) {
    mm_reserve_table[level].ptr = NULL;
    MM_LOG((LOG_RF, "%u 0x%08x", level + 1, mm_reserve_table[level].size));
  }
  mm_reserve_level = 0; /* Don't try to regain */
}


/* == Commit limit extension == */


/** Structure for controlling commit limit extension with a range. */
typedef struct {
  low_mem_handler_t handler; /**< Low-memory handler for this extension. */
  low_mem_offer_t offer; /**< Offer structure for this extension. */
  size_t base;
  size_t limit;
  /** Minimum size of each extension step. This causes other low-mem actions to
      be tried before the next step. This should balance the cost of asking all
      those handlers vs. cost of trashing. */
  size_t delta;
  mm_cost_t cost; /**< Cost for this type of extension. */
  Bool reported; /**< Has use of this extension been notified? */
} mm_commit_extension_t;


/** Structure for controlling arena extension. */
static mm_commit_extension_t mm_arena_extension;

/** Structure for controlling extension to use all VM (upto the address
    space limit). */
static mm_commit_extension_t mm_use_all;


#if defined( DEBUG_BUILD )
/** Reset the commit limit reporting, so that we only
    give one warning message (per range) per page. */
void mm_commit_level_report_reset(void)
{
  mm_arena_extension.reported = FALSE;
  mm_use_all.reported = FALSE;
}
#endif /* DEBUG_BUILD */


/** \brief Reduce the commit limit as far as possible within the
    extension, but only down to the cost given.

  Callers must shrink in the right order, so this will not be called when the
  commit limit is above the range of this extension, unless the cost limit is
  above it as well (this simplifies the callers).
 */
static Bool mm_shrink(mm_commit_extension_t *extension, mm_cost_t cost_limit)
{
  size_t curr_limit;
  Bool enough = TRUE;

  curr_limit = mm_commit_limit;
  HQASSERT(curr_limit == mps_arena_commit_limit( mm_arena ),
           "mm_commit_limit out of synch with MPS");
  HQASSERT(mm_commit_limit >= mm_arena_extension.base
           && mm_commit_limit <= mm_use_all.limit,
           "mm_commit_limit out of range");

  if ( !mm_cost_less_than(cost_limit, extension->cost) )
    return TRUE;

  HQASSERT(curr_limit <= extension->limit,
           "Shrinking using the wrong extension.");
  while ( curr_limit > extension->base ) {
    size_t trylimit;

    /* Now reduce the current limit, capping at the base for this level */
    trylimit = curr_limit - extension->delta;
    if ( trylimit > curr_limit /* wrap around */ || trylimit < extension->base )
      trylimit = extension->base;
    /* Try to set the new limit */
    if ( mps_arena_commit_limit_set( mm_arena, trylimit ) != MPS_RES_OK ) {
      enough = FALSE; break; /* still holding too much memory! */
    }
    curr_limit = trylimit;
  }
  if ( curr_limit != mm_commit_limit ) {
    mm_commit_limit = curr_limit;
    MM_LOG(( LOG_CS, "0x%08x", mm_commit_limit ));
    HQASSERT(mm_reserve_level == 0,
             "Shouldn't get here while using the reserve pool.");
    if ( mm_commit_limit == mm_commit_base )
      mm_memory_is_low = FALSE;
  }
  return enough;
}


/** \brief Reduce the commit limit, down to the limit given, using only
    actions below the cost given.

 Must only be called within low-memory synchronization. */
static Bool mm_regain_extensions(Bool *enough, corecontext_t *context,
                                 mm_cost_t fill_limit, mm_cost_t cost)
{
  Bool retry_low_mem = TRUE /* pacify the compiler */;
  memory_requirement_t request;
  mm_commit_extension_t *extension;

  *enough = TRUE;
  if ( mm_commit_limit <= mm_arena_extension.base )
    return TRUE; /* quick return for the common case of no extensions */
  request.pool = NULL;
  do {
    if ( mm_shrink(&mm_use_all, fill_limit) )
      if ( mm_shrink(&mm_arena_extension, fill_limit) )
        return TRUE; /* got it now */
      else
        extension = &mm_arena_extension;
    else
      extension = &mm_use_all;
    /* @@@@ Change this to an array of requests. Or even return a
       request to be combine with original? */
    request.size = mm_commit_limit - extension->base;
    request.cost = mm_cost_min(extension->cost, cost);
    if ( !low_mem_handle_guts(&retry_low_mem, context, 1, &request) ) {
      *enough = FALSE; return FALSE; /* error */
    }
  } while ( retry_low_mem );
  *enough = FALSE;
  return TRUE; /* didn't get it */
}


/** Solicit method of the commit extension low-memory handlers. */
static low_mem_offer_t *mm_commit_solicit(low_mem_handler_t *handler,
                                          corecontext_t *context,
                                          size_t count,
                                          memory_requirement_t* requests)
{
  mm_commit_extension_t *extension = (mm_commit_extension_t *)handler;
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  HQASSERT(mm_commit_limit >= mm_arena_extension.base
           && mm_commit_limit <= mm_use_all.limit,
           "mm_commit_limit out of range");
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

#ifdef REGAIN_AT_FREE
  if ( !multi_mutex_trylock(&reserve_mutex) )
    return NULL; /* give up if can't get the lock */
  if ( mm_commit_limit >= extension->limit ) {
    multi_mutex_unlock(&reserve_mutex);
    return NULL;
  }
#else
  if ( mm_commit_limit >= extension->limit )
    return NULL;
#endif
  extension->offer.offer_size = extension->limit - mm_commit_limit;
  extension->offer.offer_cost = extension->cost.value;

#ifdef REGAIN_AT_FREE
  multi_mutex_unlock(&reserve_mutex);
#endif
  return &extension->offer;
}


/** Shared release method of the commit extension low-memory handlers. */
static Bool mm_commit_release(low_mem_handler_t *handler,
                              corecontext_t *context, low_mem_offer_t *offer)
{
  mm_commit_extension_t *extension = (mm_commit_extension_t *)handler;
  size_t limit;
  mps_res_t res;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(corecontext_t*, context);

#ifdef REGAIN_AT_FREE
  if ( !multi_mutex_trylock(&reserve_mutex) )
    return TRUE; /* give up if can't get the lock */
  /* Other threads might have reduced the commit limit since the offer,
     but this thread still needs the same amount memory as before. */
#endif
  HQASSERT(mm_commit_limit == mps_arena_commit_limit(mm_arena),
           "mm_commit_limit out of synch with MPS");
  HQASSERT(mm_commit_limit < extension->limit,
           "Commit limit raised unexpectedly.");
  limit = min(mm_commit_limit + max(offer->taken_size, extension->delta),
              extension->limit);
  res = mps_arena_commit_limit_set(mm_arena, limit);
  HQASSERT(res == MPS_RES_OK, "Failed to set commit limit");
  mm_commit_limit = limit;
  mm_memory_is_low = TRUE;
  MM_LOG(( LOG_CX, "0x%08x", mm_commit_limit ));
#if defined( DEBUG_BUILD )
  { /* If extending without limit, warn the user. */
    mm_commit_extension_t *extension =
      limit <= mm_arena_extension.limit ? &mm_arena_extension : &mm_use_all;

    if ( !extension->reported ) {
      if ( extension != &mm_arena_extension )
        monitorf((uint8 *)"Warning: using all available memory\n");
      extension->reported = TRUE;
    }
  }
#endif
#ifdef REGAIN_AT_FREE
  multi_mutex_unlock(&reserve_mutex);
#endif
  return TRUE;
}


static mm_commit_extension_t mm_arena_extension = {
  { "Extension to arena reserve", memory_tier_partial_paint,
    mm_commit_solicit, mm_commit_release, TRUE,
   0, FALSE },
  { NULL, 0, 0.0f, 0, NULL },
  0, 0, 0, { memory_tier_limit, 0.0f }, FALSE /* dummy values */
};


static mm_commit_extension_t mm_use_all = {
  { "Extension to use all VM", memory_tier_trash_vm,
    mm_commit_solicit, mm_commit_release, TRUE,
    0, FALSE },
  { NULL, 0, 0.0f, 0, NULL },
  0, 0, 0, { memory_tier_limit, 0.0f }, FALSE /* dummy values */
};


Bool mm_extension_init(size_t addr_space_size,
                       size_t working_size, size_t extension_size,
                       Bool use_all_mem)
{
  size_t extended_commit_limit;
  Bool res;

  MM_LOG(( LOG_IP, "0x%08x 0x%08x 0x%08x 0x%08x %d",
           (unsigned)mm_arena,
           addr_space_size, working_size, extension_size,
           (int)use_all_mem ));

  if ( working_size + extension_size < working_size ) /* overflow? */
    extended_commit_limit = (size_t)-1;
  else
    extended_commit_limit = working_size + extension_size;
  if ( addr_space_size < extended_commit_limit )
    return FALSE; /* a configuration error in the skin */
  mm_commit_base = working_size;

  /* Extension beyond working_size, into arena extension, to reduce partial
   * paints. */

  /* Always init, so it all works when the extension is configured off. */
  mm_arena_extension.base = mm_arena_extension.limit = working_size;
  mm_arena_extension.cost = mm_cost_none;

  if ( extension_size > 0 ) {
    mm_arena_extension.limit = extended_commit_limit;
    mm_arena_extension.delta = 256*1024; /* guess */
    mm_arena_extension.cost.tier = memory_tier_partial_paint;
    mm_arena_extension.cost.value = 0.1f;
    HQASSERT(mm_cost_less_than(mm_arena_extension.cost, mm_cost_normal),
             "Default cost doesn't include arena extension.");
    HQASSERT(mm_cost_less_than(mm_cost_below_reserves, mm_arena_extension.cost),
             "mm_cost_below_reserves includes arena extension.");

    res = low_mem_handler_register(&mm_arena_extension.handler);
    if ( !res )
      return FALSE;
  }

  /* Extension beyond arena extension, intended to be permitted only after
   * a partial paint has been attempted (or is not allowed). */

  /* Always init, so it all works when the extension is configured off. */
  mm_use_all.base = mm_use_all.limit = mm_arena_extension.limit;
  mm_use_all.cost = mm_cost_none;

  if ( use_all_mem ) {
    if ( addr_space_size > mm_arena_extension.limit ) {
      mm_use_all.limit = addr_space_size;
      mm_use_all.delta = 256*1024; /* guess */
      mm_use_all.cost.tier = memory_tier_trash_vm;
      mm_use_all.cost.value = 1.0f;
      HQASSERT(mm_cost_less_than(mm_use_all.cost, mm_cost_normal),
               "Default cost doesn't include use-all extension.");
      HQASSERT(mm_cost_less_than(mm_cost_below_reserves, mm_use_all.cost),
               "mm_cost_below_reserves includes use-all extension.");

      res = low_mem_handler_register(&mm_use_all.handler);
      if ( !res )
        return FALSE;
    }
  }

  /* Now set the current commit limit */
  mm_commit_limit = working_size;
  if ( mps_arena_commit_limit_set( mm_arena, mm_commit_limit ) != MPS_RES_OK )
    return FALSE;

  /* Try to hang onto a few a spare segments. Note that we just use a small
   * and fairly arbitrary number here. It's small 'cos there are few pools
   * which exhibit the problem of repeated allocs and frees that cause
   * segments to be created and destroyed (examples: dl pool, and to a much
   * lesser extent, color pool - as it overflows onto a second segment). */
  mps_arena_spare_commit_limit_set( mm_arena, (size_t)4 * 64 * 1024 );
  return TRUE;
}


void mm_extension_finish(void)
{
  if ( mm_use_all.base != mm_use_all.limit )
    low_mem_handler_deregister(&mm_use_all.handler);
  if ( mm_arena_extension.base != mm_arena_extension.limit )
    low_mem_handler_deregister(&mm_arena_extension.handler);
  mm_commit_limit = mm_arena_extension.base; /* stop regaining limit */
}


/* == Common interfaces for all reserves == */


void mm_set_reserves(mm_context_t *context, Bool full)
{
  HQASSERT(context != NULL, "Null context");
  /* This should probably take an enum describing the level, but Boolean will do
     for now. */
  context->reserve_level = full ? mm_cost_below_reserves : mm_cost_normal;
}


Bool mm_should_regain_reserves(mm_cost_t limit)
{
  unsigned int reserve_level;
#if defined(FAIL_AFTER_N_ALLOCS)
  if ( fail_after_n > 0 && ++n_alloc_calls >= fail_after_n )
    /* Push the caller into a low-memory loop, which will fail. */
    return TRUE;
#endif /* FAIL_AFTER_N_ALLOCS */
#if defined(DEBUG_BUILD)
  if ( debug_lowmemory_count > 0 )
    /* Push it into a low-memory loop, where the rest will be done. */
    return TRUE;
#endif
  /* Not synchronized, because regain doesn't have to be timely */
  if ( !mm_memory_is_low )
    return FALSE;
  if ( mm_commit_limit > mm_arena_extension.base
       && mm_cost_less_than(limit,
                            mm_commit_limit <= mm_arena_extension.limit
                            ? mm_arena_extension.cost : mm_use_all.cost) )
    return TRUE;
  reserve_level = mm_reserve_level; /* read it just once, as it could change
                                       between the clauses below */
  return reserve_level > 0
         && mm_cost_less_than(limit,
                              mm_reserve_table[reserve_level-1].cost);
}


Bool mm_regain_reserves_guts(Bool *enough, corecontext_t *context,
                             mm_cost_t fill_limit, mm_cost_t cost)
{
  if ( !mm_regain_reserve_pool(enough, context, fill_limit, cost) )
    return FALSE;
  if ( *enough )
    return mm_regain_extensions(enough, context, fill_limit, cost);
  return TRUE;
}


Bool mm_regain_reserves(Bool *enough, corecontext_t *context,
                        mm_cost_t cost)
{
  Bool res;
  Bool nested_call = context->mm_context->handling_low_memory;

  if ( !enter_low_mem_handling(context, &nested_call, LOWMEM_REGAIN_RESERVES) )
    return FALSE; /* error or interrupt */

  res = mm_regain_reserves_guts(enough, context,
                                context->mm_context->reserve_level, cost);

  exit_low_mem_handling(context, nested_call, LOWMEM_REGAIN_RESERVES);

  return res;
}


Bool mm_refill_reserves_guts(Bool *enough, mm_cost_t limit)
{
  *enough = mm_reserve_get(limit)
    && (mm_commit_limit <= mm_arena_extension.base /* quick test for the
                                                      common case */
        || (mm_shrink(&mm_use_all, limit)
            && mm_shrink(&mm_arena_extension, limit)));
  return TRUE;
}


void mm_refill_reserves(void)
{
  Bool dummy;

  /* Test not synchronized, because avoiding the lock is one of its
     benefits, and regain doesn't have to be timely */
  if ( mm_commit_limit > mm_arena_extension.base || mm_reserve_level > 0 ) {
    /* If not regaining at free, all calls are either single-threaded or in a
       low-memory handler, so no synchronization needed. */
#ifdef REGAIN_AT_FREE
    multi_mutex_lock(&reserve_mutex);
#endif
    (void)mm_refill_reserves_guts(&dummy, mm_cost_below_reserves);
#ifdef REGAIN_AT_FREE
    multi_mutex_unlock(&reserve_mutex);
#endif
  }
}


/* == Memory queries == */


/* How many bytes are managed by the memory manager? */
size_t mm_total_size(void)
{
  HQASSERT( mm_arena, "mm_total_size called before mm_init" );
  return mm_use_all.limit;
}


/** How many bytes are not assigned to any pool?
 *
 * Undefined behaviour if called before the memory manager is initialized. */
size_t mm_no_pool_size( int32 include_reserve )
{
  size_t size;

  HQASSERT( mm_arena, "mm_no_pool_size called before mm_init" );

  /* Not synchronized because accuracy is not required. */
  size = mm_total_size() - mps_arena_committed(mm_arena);
  /* Note that the reserve may be re-used for other things if we run low
   * so we count it as 'unassigned' memory, unless include_reserve states
   * otherwise. */
  if ( include_reserve )
    size += mm_reserve_size();
  return size + mps_arena_spare_committed(mm_arena);
}


/* Returns the working size - this is intended to be the amount that the
 * RIP can grow to without causing paging. */
size_t mm_working_size(void)
{
  HQASSERT( mm_arena, "mm_working_size called before mm_init" );
  return mm_arena_extension.base;
}


void init_C_globals_mmreserve(void)
{
  mm_memory_is_low = FALSE;
  mm_pool_reserve = NULL;
  mm_reserve_level = 0;
  mm_reserve_numlevels = 0;
  mm_commit_limit = 0;
  mm_arena_extension.base = mm_arena_extension.limit = 0;
  mm_use_all.base = mm_use_all.limit = 0;
}

/*
* Log stripped */
