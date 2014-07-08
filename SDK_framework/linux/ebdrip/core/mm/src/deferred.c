/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:deferred.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Deferred allocation implementation.
 */

#include "core.h"
#include "deferred.h"
#include "mps.h"
#include "apportioner.h" /* low_mem_handle_guts */
#include "context.h"
#include "mmreserve.h" /* mm_should_regain_reserves */
#include "mmpool.h"
#include "mmtag.h"
#include "mmlog.h"
#include "mmfence.h"
#include "vm.h" /* MPSTAG_FN */
#include "mm.h"
#include "hqmemcpy.h"
#include "swerrors.h"


/** A structure to hold the data for a deferred allocation. */
struct deferred_alloc_t {
  memory_request_t *requests; /**< List of requests in this alloc */
  size_t requirements_size; /**< Number of slots in \a requirements */
  size_t requirements_used; /**< Number of slots used in \a requirements */
  /** A requirements array for this deferred alloc */
  memory_requirement_t *requirements;
};


/** Swap the contents of two variables. */
#define SWAP(type, a, b) MACRO_START \
  type tmp_ = (a); (a) = (b); (b) = tmp_; \
MACRO_END


inline Bool mm_cost_equal(mm_cost_t cost1, mm_cost_t cost2)
{
  return cost1.tier == cost2.tier && cost1.value == cost2.value;
}


#ifdef ASSERT_BUILD
/** Assert the validity of a memory request. */
void check_request(memory_request_t *req, Bool alloced)
{
  size_t i;

  HQASSERT(req->pool != NULL, "No pool");
  HQASSERT(req->size > 0, "Zero-sized allocation");
  HQASSERT(req->size < TWO_GB, "allocation exceeds 2 GB limit");
  /* Nothing to assert about class. */
  /* We're missing location checks! */
  /* Nothing to assert about next. */
  HQASSERT(req->min_count <= req->max_count, "Inconsistent min&max counts");
  HQASSERT(req->max_count > 0, "Vacuous requests for no blocks");
  HQASSERT(req->min_count == req->max_count || is_valid_cost(req->cost),
           "Invalid cost");
  if ( !alloced )
    for ( i = 0 ; i < req->max_count ; ++i )
      HQASSERT(req->blocks[i] == NULL, "Block pointer not cleared");
}
#endif


/** Find the requirements slot for the given parameters. */
static size_t find_requirement_slot(deferred_alloc_t *dalloc,
                                    mm_pool_t pool, mm_cost_t cost)
{
  size_t i;

  for ( i = 0 ; i < dalloc->requirements_used ; ++ i )
    if ( dalloc->requirements[i].pool == pool
         && mm_cost_equal(dalloc->requirements[i].cost, cost) )
      break;
  HQASSERT(i < dalloc->requirements_used, "Didn't find the requirements");
  return i;
}


/* Amend requirements: Either update a matching one or create a new one. */
static Bool amend_requirements(deferred_alloc_t *dalloc,
                               memory_request_t *request,
                               size_t count, mm_cost_t cost)
{
  size_t i;

  /* Find requirements slot. */
  for ( i = 0 ; i < dalloc->requirements_used ; ++ i )
    if ( request->pool == dalloc->requirements[i].pool
         && mm_cost_equal(cost, dalloc->requirements[i].cost) )
      break;
  if ( i < dalloc->requirements_used ) { /* found a match */
    dalloc->requirements[i].size += request->size * count;
    return TRUE;
  }
  if ( i == dalloc->requirements_size ) { /* there's no free slot */
    /* Realloc larger requirements array. */
    size_t new_size = dalloc->requirements_size
                      + min(dalloc->requirements_size/4, 2);
    memory_requirement_t *new_reqs =
      mm_alloc(mm_pool_temp,
               new_size * sizeof(memory_requirement_t),
               MM_ALLOC_CLASS_MM);
    if ( new_reqs == NULL )
      return error_handler(VMERROR);
    HqMemCpy(new_reqs, dalloc->requirements,
             dalloc->requirements_used * sizeof(memory_requirement_t));
    mm_free(mm_pool_temp, dalloc->requirements,
            dalloc->requirements_size * sizeof(memory_requirement_t));
    dalloc->requirements = new_reqs;
    dalloc->requirements_size = new_size;
  }
  /* Now install the new requirement in the slot found/alloced. */
  dalloc->requirements[i].pool = request->pool;
  dalloc->requirements[i].cost = cost;
  dalloc->requirements[i].size = request->size * count;
  ++dalloc->requirements_used;
  return TRUE;
}


deferred_alloc_t *deferred_alloc_init(size_t approx_types)
{
  memory_requirement_t *reqs;
  deferred_alloc_t *dalloc;
  size_t req_slots = approx_types * 2; /* one for min and max each */

  reqs = mm_alloc(mm_pool_temp, req_slots * sizeof(memory_requirement_t),
                  MM_ALLOC_CLASS_MM);
  if ( reqs == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  dalloc = mm_alloc(mm_pool_temp, sizeof(deferred_alloc_t), MM_ALLOC_CLASS_MM);
  if ( dalloc == NULL ) {
    mm_free(mm_pool_temp, reqs, req_slots * sizeof(memory_requirement_t));
    (void)error_handler(VMERROR);
    return NULL;
  }
  dalloc->requests = NULL;
  dalloc->requirements_size = req_slots; dalloc->requirements_used = 0;
  dalloc->requirements = reqs;
  return dalloc;
}


void deferred_alloc_finish(deferred_alloc_t *dalloc)
{
  mm_free(mm_pool_temp, dalloc->requirements,
          dalloc->requirements_size * sizeof(memory_requirement_t));
  mm_free(mm_pool_temp, dalloc, sizeof(deferred_alloc_t));
}


Bool deferred_alloc_add(deferred_alloc_t *dalloc, memory_request_t *requests)
{
  memory_request_t *request, *last = NULL;

  HQASSERT(dalloc != NULL, "No alloc to add to");
  HQASSERT(requests != NULL, "No requests to add");
#ifdef ASSERT_BUILD
  for ( request = requests ; request != NULL ; request = request->next )
    check_request(request, FALSE);
#endif

  for ( request = requests ; request != NULL ; request = request->next ) {
    HQASSERT(request->min_count == request->max_count, "Flexible requests NYI.");
    if ( !request->pool->mps_debug )
      request->size = ADJUST_FOR_FENCEPOSTS(request->size);
    last = request; /* remember this for later */
  }
  /* Link the whole chain in front of the current requests. */
  last->next = dalloc->requests; dalloc->requests = requests;
  return TRUE;
}


Bool deferred_alloc_add_simple(deferred_alloc_t *dalloc,
                               memory_request_t *request,
                               mm_pool_t pool,
                               void **blocks)
{
  request->pool = pool; request->blocks = blocks;
  return deferred_alloc_add(dalloc, request);
}


/** Attempt to allocate the requests in deferred alloc. */
static Bool deferred_alloc_try(deferred_alloc_t *dalloc,
                               mm_cost_t *max_cost,
                               size_t *unsatisfied_requirements)
{
  memory_request_t *request;
  Bool success = TRUE;
  mps_res_t res = MPS_RES_FAIL;
#ifdef MM_DEBUG_MPSTAG
  mps_debug_info_s dinfo;
#endif

  for ( request = dalloc->requests ; request != NULL
          ; request = request->next ) {
    size_t size = request->size;
    mm_pool_t pool = request->pool;
    size_t i, req_index = find_requirement_slot(dalloc, pool, *max_cost);

#ifdef MM_DEBUG_MPSTAG
    dinfo.location = mm_location_label(request->location);
    dinfo.mps_class = alloc_class_label[request->class];
#endif
    for ( i = 0 ; i < request->max_count ; ++i )
      if ( request->blocks[i] == NULL ) {
        res = MPSTAG_FN(mps_alloc)(&request->blocks[i],
                                   pool->mps_pool, size
                                   MPSTAG_ARG);
        if ( res == MPS_RES_OK ) {
          dalloc->requirements[req_index].size -= size;
          if ( dalloc->requirements[req_index].size == 0 ) {
            /* Now satisfied, swap the last one in its place. */
            --*unsatisfied_requirements;
            if ( req_index != *unsatisfied_requirements )
              SWAP(memory_requirement_t,
                   dalloc->requirements[req_index],
                   dalloc->requirements[*unsatisfied_requirements]);
          }
#ifdef MM_DEBUG_TAG
# ifdef MM_DEBUG_LOCN
#  ifdef MM_DEBUG_MPSTAG
          mm_debug_tag_add(request->location, 0,
                           request->blocks[i], size, pool, request->class);
#  else /* !MM_DEBUG_MPSTAG */
          mm_debug_tag_add(request->file, request->line,
                           request->blocks[i], size, pool, request->class);
#  endif
# else /* !MM_DEBUG_LOCN */
          mm_debug_tag_add("not specified", 0,
                           request->blocks[i], size, pool, request->class);
# endif
#endif
          mm_debug_total_alloc(pool, size);
          request->blocks[i] =
            mm_debug_set_fencepost(pool, request->blocks[i], size);
        } else
          success = FALSE;
      }
  }
  HQASSERT(!success || *unsatisfied_requirements == 0,
           "Miscounted requirements");
  return success;
}


/** Clean up an aborted deferred alloc, deallocating everything. */
static void deferred_alloc_untry(deferred_alloc_t *dalloc)
{
  memory_request_t *request;

  for ( request = dalloc->requests ; request != NULL
          ; request = request->next ) {
    size_t size = request->size;
    mm_pool_t pool = request->pool;
    size_t i;

    for ( i = 0 ; i < request->max_count ; ++i )
      if ( request->blocks[i] != NULL ) {
        void *base = BELOW_FENCEPOST(request->blocks[i]);
        mps_free(pool->mps_pool, base, size);
        mm_debug_tag_free(base, size, pool);
        mm_debug_total_free(pool, size);
      } else
        break; /* rest are all NULL */
  }
}


Bool deferred_alloc_realize(deferred_alloc_t *dalloc,
                            mm_cost_t cost_for_min,
                            corecontext_t *context)
{
  size_t unsatisfied_requirements;
  Bool success = FALSE;
  mm_cost_t max_cost = cost_for_min;
  memory_request_t *request;

  HQASSERT(dalloc != NULL, "No alloc to realize");
  HQASSERT(is_valid_cost(cost_for_min), "Invalid cost");
#ifdef ASSERT_BUILD
  for ( request = dalloc->requests ; request != NULL
          ; request = request->next ) {
    check_request(request, FALSE);
    HQASSERT(request->min_count == request->max_count
             || mm_cost_less_than(request->cost, cost_for_min),
             "Contradictory costs");
  }
#endif
  /* Nothing to assert about context. */

  /* Collect requirements. */
  dalloc->requirements_used = 0;
  for ( request = dalloc->requests ; request != NULL
          ; request = request->next ) {
    if ( !amend_requirements(dalloc, request,
                             request->min_count, cost_for_min) )
      return FALSE;
    /* @@@@ add max requirements */
  }
  unsatisfied_requirements = dalloc->requirements_used;

  if ( !mm_should_regain_reserves(cost_for_min) )
    success = deferred_alloc_try(dalloc, &max_cost, &unsatisfied_requirements);
  if ( !success ) {
    /* This block is like mm_low_mem_alloc, but with multiple requirements. */
    Bool nested_call;
    Bool enough_reserves = TRUE /* pacify the compiler */;
    Bool retry = TRUE /* pacify the compiler */;

    /* Synchronize for low memory handling. */
    if ( !enter_low_mem_handling(context, &nested_call, LOWMEM_DEFERRED_ALLOC) )
      return FALSE; /* error or interrupt */

    if ( !mm_regain_reserves_for_alloc(&enough_reserves, context, max_cost) )
      goto error;
    if ( !enough_reserves )
      goto error; /* deny alloc */

    do {
      success = deferred_alloc_try(dalloc,
                                   &max_cost, &unsatisfied_requirements);
      if ( success )
        break;
      /* @@@@ If max_cost decreases, should regain reserves. */
      if ( !low_mem_handle_guts(&retry, context,
                                unsatisfied_requirements,
                                dalloc->requirements) )
        break; /* error, so give up */
    } while ( retry );
  error:
    exit_low_mem_handling(context, nested_call, LOWMEM_DEFERRED_ALLOC);
  }
  /* @@@@ logging */
  if ( !success )
    deferred_alloc_untry(dalloc);
  return success;
}


/* Log stripped */
