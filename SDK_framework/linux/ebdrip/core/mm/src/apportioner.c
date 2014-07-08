/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:apportioner.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.

 * \brief
 * Multithreaded Core RIP low-memory handling logic.
 */

#include "core.h"
#include "apportioner.h"
#include "mm.h"
#include "mm_core.h" /* MM_SEGMENT_SIZE */
#include "lowmem.h"
#include "mmlog.h"
#include "mmpool.h"
#include "mps.h"
#include "hqassert.h"
#include "swerrors.h"
#include "mlock.h"
#include "monitor.h"
#include "rdrapi.h"
#undef INTERRUPTS_AVAILABLE /** \todo Review access to core interrupt state. */
#if INTERRUPTS_AVAILABLE
#include "interrupts.h"
#else
#include "taskh.h"
#endif
#include "swtrace.h"
#include "ripmulti.h" /* multi_constrain_to_single */
#include "timing.h"   /* probe_begin/probe_end */
#include <float.h> /* FLT_MAX */


/** MPS log symbol for entry to a low-memory handler. */
static mps_word_t low_entry_sym;

/** MPS log symbol for exit from a low-memory handler. */
static mps_word_t low_exit_sym;


/** Flag for emitting low memory tracing. */
static Bool low_mem_trace;


const mm_cost_t mm_cost_none = { memory_tier_min, -1.0f };

const mm_cost_t mm_cost_easy = { memory_tier_ram, 1.01f };

/** Macro version of mm_cost_below_reserves. */
#define MM_COST_BELOW_RESERVES { memory_tier_partial_paint, -1.0f }

const mm_cost_t mm_cost_below_reserves = MM_COST_BELOW_RESERVES;

/** Macro version of mm_cost_normal */
#define MM_COST_NORMAL { memory_tier_reserve_pool, 1e3f }

const mm_cost_t mm_cost_normal = MM_COST_NORMAL;

const mm_cost_t mm_cost_all = { memory_tier_reserve_pool, 1e6f };


Bool mm_cost_less_than(const mm_cost_t cost1, const mm_cost_t cost2)
{
  return cost1.tier < cost2.tier
         || (cost1.tier == cost2.tier && cost1.value < cost2.value);
}


mm_cost_t mm_cost_min(const mm_cost_t cost1, const mm_cost_t cost2)
{
  return cost1.tier < cost2.tier
         || (cost1.tier == cost2.tier && cost1.value < cost2.value)
    ? cost1 : cost2;
}


Bool is_valid_cost(const mm_cost_t cost)
{
  return cost.tier >= memory_tier_min && cost.tier <= memory_tier_limit
    /* cost.value can be anything, just not so large that calculations
       overflow. A -1.0 value is allowed for allocation costs as an easy
       way to express a cost just below any offer on a tier. */
    && (cost.value > 0.0f || cost.value == -1.0f);
}


void mm_set_allocation_cost(mm_context_t *context, const mm_cost_t cost)
{
  HQASSERT(context != NULL, "Null context");
  HQASSERT(is_valid_cost(cost), "Invalid cost");
  context->default_cost = cost;
}


mm_cost_t mm_allocation_cost(mm_context_t *context)
{
  HQASSERT(context != NULL, "Null context");
  return context->default_cost;
}


size_t at_least_a_segment(mm_pool_t pool, size_t size)
{
  return max(size, pool != NULL ? pool->segment_size : MM_SEGMENT_SIZE);
}


Bool mm_handling_low_memory(corecontext_t *context)
{
  if ( context == NULL )
    return FALSE ;

  HQASSERT(context->mm_context, "NULL context");

  return context->mm_context->handling_low_memory ;
}

/** Mutex for low-memory handling. */
static multi_mutex_t lowmem_mutex;

/** Condvar for waiting to handle low memory. */
static multi_condvar_t lowmem_condvar;


/** Flag set when some thread is handling low memory.

  Protected by lowmem_mutex. */
static Bool handling_low_memory;


Bool enter_low_mem_handling(corecontext_t *context, Bool *nested,
                            lowmem_reason_t reason)
{
  UNUSED_PARAM(lowmem_reason_t, reason) ; /* Probes only */

  if ( context != NULL && context->mm_context != NULL ) {
    *nested = context->mm_context->handling_low_memory ;
    if ( *nested ) {
      probe_begin(SW_TRACE_HANDLING_LOWMEM, (intptr_t)reason) ;
      return TRUE ;
    }
  } else {
    *nested = FALSE ;
  }

  multi_mutex_lock(&lowmem_mutex);
  while ( handling_low_memory ) {
    multi_condvar_wait(&lowmem_condvar);
#if INTERRUPTS_AVAILABLE
    if ( !interrupts_clear(allow_interrupt) ) {
      multi_mutex_unlock(&lowmem_mutex);
      return report_interrupt(allow_interrupt);
    }
#else
    if ( task_cancelling() ) {
      multi_mutex_unlock(&lowmem_mutex);
      return error_handler(INTERRUPT);
    }
#endif
  }
  /* Only one thread at a time gets past this point. */
  handling_low_memory = TRUE; /* flag that a thread is now handling */
  if ( context != NULL && context->mm_context != NULL )
    context->mm_context->handling_low_memory = TRUE ;

  probe_begin(SW_TRACE_HANDLING_LOWMEM, (intptr_t)reason) ;
  multi_mutex_unlock(&lowmem_mutex);
  return TRUE;
}


void exit_low_mem_handling(corecontext_t *context, Bool nested,
                           lowmem_reason_t reason)
{
  UNUSED_PARAM(lowmem_reason_t, reason) ; /* Probes only */

  HQASSERT(handling_low_memory,
           "Exiting low-memory handling while not handling");

  if ( nested ) {
    probe_end(SW_TRACE_HANDLING_LOWMEM, (intptr_t)reason) ;
    return ;
  }

  multi_mutex_lock(&lowmem_mutex);
  handling_low_memory = FALSE; /* flag not handling anymore */
  if ( context != NULL && context->mm_context != NULL ) {
    HQASSERT(context->mm_context->handling_low_memory,
             "Exiting low-memory handling while not handling");
    context->mm_context->handling_low_memory = FALSE ;
  }
  multi_condvar_signal(&lowmem_condvar);
  probe_end(SW_TRACE_HANDLING_LOWMEM, (intptr_t)reason) ;
  multi_mutex_unlock(&lowmem_mutex);
}


/** Take parts of offer to satisfy the given request. */
static size_t satisfy_from_offer(memory_requirement_t* request,
                                 size_t requested, Bool only_same_pool,
                                 low_mem_offer_t *offer, memory_tier offer_tier)
{
  low_mem_offer_t *offer_part;
  size_t needed = requested;

  HQASSERT(offer_tier <= request->cost.tier, "Offer tier too high");

  for ( offer_part = offer ; offer_part != NULL
          ; offer_part = offer_part->next ) {
    /* N.B.: Offers that match the request must be rejected, lest the
       system try to move memory from a resource to itself. */
    if ( offer_tier < request->cost.tier
         || (offer_tier == request->cost.tier
             && offer_part->offer_cost < request->cost.value) ) {
      size_t avail = offer_part->offer_size - offer_part->taken_size;
      if ( avail > 0
           && (!only_same_pool
               || (offer_part->pool != NULL
                   && offer_part->pool == request->pool)) ) {
        /* Take first offer that matches. This is suboptimal if offers are not
           in order of increasing cost. For now, all offers have same cost. */
        size_t taken = only_same_pool ? min(avail, needed)
          : at_least_a_segment(offer_part->pool, min(avail, needed));
        offer_part->taken_size += taken;
        if ( needed <= taken )
          return 0;
        needed -= taken;
      }
    }
  }
  return needed;
}


/** Decide what to take from offer and return total cost and size of that.

  \param[in] count  Number of requirements.
  \param[in] requests  An array of requirements.
  \param[in] offer  The offer chain to evaluate.
  \param[in] offer_tier  The tier of the offer chain.
  \param[out] total_cost_out  Total cost of the parts taken.
  \param[out] total_size_out  Total size of the parts taken.
 */
static void evaluate_offer(size_t count, memory_requirement_t* requests,
                           low_mem_offer_t *offer, memory_tier offer_tier,
                           float *total_cost_out, size_t *total_size_out)
{
  float total_cost = 0.0;
  size_t total_size = 0, i;
  low_mem_offer_t *offer_part;

  /* Check offer and init taken_size to 0. */
  for ( offer_part = offer ; offer_part != NULL
          ; offer_part = offer_part->next ) {
    HQASSERT(offer_part->offer_cost > 0.0f, "Negative offer");
    HQASSERT(offer_tier < mm_cost_all.tier
             || offer_part->offer_cost < mm_cost_all.value,
             "Offer larger than top cost");
    offer_part->taken_size = 0;
  }
  for ( i = 0 ; i < count ; ++i ) {
    /* Try to get from the same pool, the rest from any pool. */
    size_t needed = satisfy_from_offer(&requests[i], requests[i].size, TRUE,
                                       offer, offer_tier);
    if ( needed > 0 )
      (void)satisfy_from_offer(&requests[i],
                               at_least_a_segment(requests[i].pool, needed),
                               FALSE, offer, offer_tier);
  }
  for ( offer_part = offer ; offer_part != NULL /* total up */
          ; offer_part = offer_part->next ) {
    HQASSERT(offer_part->offer_size > 0, "Zero offer");
    if ( offer_part->taken_size > 0 ) {
      HQASSERT(FLT_MAX / offer_part->taken_size > offer_part->offer_cost,
               "Offer multiply overflow");
      HQASSERT(FLT_MAX - total_cost
               > offer_part->offer_cost * offer_part->taken_size,
               "Offer sum overflow");
      total_cost += offer_part->offer_cost * offer_part->taken_size;
      total_size += offer_part->taken_size;
    }
  }
  *total_cost_out = total_cost; *total_size_out = total_size;
}


/** Invokes the release method of the handler, setting up the context. */
static Bool invoke_release(low_mem_handler_t *handler,
                           corecontext_t *context, low_mem_offer_t *offer,
                           mm_cost_t max_cost, float average_cost)
{
  Bool no_error;
  mm_cost_t saved_cost = context->mm_context->default_cost;

  UNUSED_PARAM(float, average_cost);
  MM_LOG((LOG_LI, "%u %f %s", (unsigned)handler->tier, (double)average_cost,
          handler->name));
  mps_telemetry_label(NULL, low_entry_sym);
  HQTRACE(low_mem_trace,
          ("Invoking %u %f %s", (unsigned)handler->tier, (double)average_cost,
           handler->name));
  context->mm_context->default_cost = max_cost;
  handler->running = TRUE;
  no_error = handler->release(handler, context, offer);
  handler->running = FALSE;
  context->mm_context->default_cost = saved_cost;
  MM_LOG((LOG_LO, "%u %d %s", (unsigned)handler->tier, (int)no_error,
          handler->name));
  mps_telemetry_label(NULL, low_exit_sym);
  return no_error;
}


/** Limit to start worrying about repeated handler invocations. */
#define TOO_MANY_INVOKES 1000


/**
 * Cache of low-mem handlers for each tier.
 *
 * It has been found that in ultra-low-memory situations, we can call the
 * low-memory handling code very frequently, and iterating the handlers via
 * RDR becomes a huge bottleneck. So to avoid this, it it worth having a
 * cache of the RDR low-mem iteration. The code has a fixed upper bound on
 * the size of this cache. If the size is exceeded, it falls back to the slow
 * RDR iteration, after issuing a warning message. [ I'm currently seeing about
 * 20 or 30 handlers, so an upper limit of 60 should be more than adequate. If
 * this is exceeded, we want to know about it, so a warning is useful. ]
 *
 * The cache is marked as invalid on startup, or when a handler is added or
 * removed. Then it can be lazily validated as needed. The synchronisation
 * or the cache with the addition of a handler is not important, as long as
 * the behaviour is stable. Whereas the synchronisation with a handler removal
 * is OK, as it is protected by the low-mem mutex.
 */
#define MAX_CACHED_HANDLERS 60
static struct {
  sw_rdr_iterator *it; /**< Preallocated RDR iterator */
  Bool cache_valid;    /**< Is cache array up-to-date ? */
  int32 nhandlers;     /**< How many handlers in array are valid,
                            < 0 => cannot use cache (e.g. too many handlers) */
  low_mem_handler_t *cache[MAX_CACHED_HANDLERS]; /**< The low-mem handlers */
} tier_handlers[memory_tier_limit];

static void clear_tier_handlers(Bool close)
{
  memory_tier tier;

  for ( tier = memory_tier_min ; tier < memory_tier_limit ; ++tier ) {
    int32 i;

    if ( close && tier_handlers[tier].it != NULL )
        (void)SwFoundRDR(tier_handlers[tier].it);
    tier_handlers[tier].it = NULL;
    tier_handlers[tier].cache_valid = FALSE;
    tier_handlers[tier].nhandlers = 0;
    for ( i = 0; i < MAX_CACHED_HANDLERS; i++ )
      tier_handlers[tier].cache[i] = NULL;
  }
}

/**
 * Return the next low-mem handler to use, preferably via the RDR cache,
 * or via direct RDR iteration if that is not possible.
 */
static low_mem_handler_t *next_handler(memory_tier tier, int32 handler_i)
{
  sw_rdr_iterator *it = tier_handlers[tier].it;
  void *rdr;
  sw_rdr_result res;

  HQASSERT(it, "Lost low-mem RDR iterator");
  if ( !tier_handlers[tier].cache_valid ) {
    int32 n = 0;

    while ( n < MAX_CACHED_HANDLERS && SwNextRDR(it, NULL, NULL, NULL, &rdr,
                                                 NULL) == SW_RDR_SUCCESS ) {
      tier_handlers[tier].cache[n++] = (low_mem_handler_t *)rdr;
    }
    res = SwRestartFindRDR(it); /* rewind iterator for next use */
    HQASSERT(res == SW_RDR_SUCCESS, "Failed to rewind RDR iterator.");
    if ( n >= MAX_CACHED_HANDLERS ) {
      static Bool tier_handlers_have_warned = FALSE;
      if ( !tier_handlers_have_warned ) {
        monitorf((uint8 *)"Low-mem handler cache size exceeded\n");
        tier_handlers_have_warned = TRUE;
      }
      n = -1;
    }
    tier_handlers[tier].nhandlers = n;
    tier_handlers[tier].cache_valid = TRUE;
  }
  if ( tier_handlers[tier].nhandlers >= 0 ) {
    if ( handler_i < tier_handlers[tier].nhandlers )
      return tier_handlers[tier].cache[handler_i];
    else
      return NULL;
  } else {
    if ( SwNextRDR(it, NULL, NULL, NULL, &rdr, NULL) == SW_RDR_SUCCESS )
      return (low_mem_handler_t *)rdr;
    res = SwRestartFindRDR(it); /* rewind iterator for next use */
    HQASSERT(res == SW_RDR_SUCCESS, "Failed to rewind RDR iterator.");
    return NULL;
  }
}

Bool low_mem_handle_guts(Bool *retry, corecontext_t *context,
                         size_t count, memory_requirement_t* requests)
{
  memory_tier tier = memory_tier_min;
  mm_cost_t max_cost = { memory_tier_min, FLT_MIN };
  size_t i;
  Bool no_error = TRUE;
  Bool single_thread_locked = FALSE;
  Bool invoked_some_handler = FALSE;
#if defined( ASSERT_BUILD )
  unsigned retry_count = 0;
#endif

  HQASSERT(retry != NULL, "NULL retry");
  HQASSERT(context != NULL, "NULL context");
  HQASSERT(count > 0, "No requests");
  HQASSERT(requests != NULL, "NULL requests");

  HQASSERT(!context->mm_context->low_mem_unsafe, "Improper low-mem recursion!");
  /* @@@@ nested calls should combine with other requests */
  for ( i = 0 ; i < count ; ++i ) { /* find max cost */
    if ( mm_cost_less_than(max_cost, requests[i].cost) )
      max_cost = requests[i].cost;
  }
  *retry = FALSE;
  do { /* try handlers until success, error, or no offers */
    /* For now, just find best offer. */
    low_mem_offer_t *best_offer = NULL;
    low_mem_handler_t *best_handler = NULL;
    float lowest_average = FLT_MAX;
    low_mem_handler_t *handler;
    int32 handler_i = 0;

    HQASSERT(retry_count != TOO_MANY_INVOKES /* assert once at the limit */,
             "Too many low-memory handlers invoked.");
#if defined( ASSERT_BUILD )
    if ( ++retry_count > TOO_MANY_INVOKES+10 )
      low_mem_trace = FALSE; /* stop traces hosing the log */
    else if ( retry_count > TOO_MANY_INVOKES )
      low_mem_trace = TRUE; /* force trace on */
#endif
    context->mm_context->low_mem_unsafe = TRUE;
    while ( (handler = next_handler(tier, handler_i++)) != NULL ) {
      low_mem_offer_t *offer = NULL;

      if ( !handler->running ) {
        if ( !single_thread_locked && !handler->multi_thread_safe )
          /* If needed, try to restrict to a single thread. */
          single_thread_locked = multi_constrain_to_single();
        if ( single_thread_locked || handler->multi_thread_safe )
          /* Only call if managed to ensure thread safety */
          offer = handler->solicit(handler, context, count, requests);
        /* For now, just take lowest offer. Ultimately, collect all offers. */
        if ( offer != NULL ) {
          float total_cost;
          size_t total_size;

          evaluate_offer(count, requests, offer, tier,
                         &total_cost, &total_size);
          /* Take the offer with lowest average cost. */
          if ( total_size > 0 && total_cost/total_size < lowest_average ) {
            lowest_average = total_cost/total_size;
            best_offer = offer; best_handler = handler;
          }
        }
      }
    }
    context->mm_context->low_mem_unsafe = FALSE;

    if ( best_offer != NULL ) { /* is it good enough? */

      if ( single_thread_locked && best_handler->multi_thread_safe ) {
        /* If this handler doesn't need it, don't restrict threads. */
        multi_unconstrain_to_single();
        single_thread_locked = FALSE;
      }

      invoked_some_handler = TRUE;
      no_error = invoke_release(best_handler, context, best_offer,
                                max_cost, lowest_average);

      if ( no_error ) {
        /* Estimate if there's enough free to satisfy the requests. */
        size_t free_in_arena =
          mps_arena_commit_limit(mm_arena) - mps_arena_committed(mm_arena);
        for ( i = 0 ; i < count ; ++i ) {
          size_t need_from_arena;

          if ( requests[i].pool != NULL ) {
            size_t free_in_pool = mm_pool_free_size(requests[i].pool);
            if ( free_in_pool < requests[i].size )
              need_from_arena =
                max(requests[i].size - free_in_pool,
                    requests[i].pool->segment_size);
            else
              need_from_arena = 0;
          } else
            need_from_arena = max(requests[i].size, MM_SEGMENT_SIZE);
          if ( need_from_arena > free_in_arena )
            break; /* not enough free */
          free_in_arena -= need_from_arena;
        }
        if ( i == count ) { /* found enough for every request */
          *retry = TRUE; break; /* Let the caller retry alloc */
        }
      } else {
        HQTRACE(TRUE, ("Error in low-memory handler %s: %d",
                       best_handler->name,
                       (int)newerror_context(context->error)));
        HQASSERT(newerror_context(context->error) != VMERROR,
                 "A low-memory handler returned a VMerror.");
        no_error = FALSE; break; /* Got an error, give up */
      }
    } else /* best_offer == NULL, no decent offers, try next tier */
      ++tier;
#if INTERRUPTS_AVAILABLE
    if ( !interrupts_clear(allow_interrupt) ) {
      (void)report_interrupt(allow_interrupt);
      no_error = FALSE; break; /* Got an error, give up */
    }
#endif
  } while ( tier <= max_cost.tier );
  if ( single_thread_locked )
    multi_unconstrain_to_single();
  if ( tier > max_cost.tier && invoked_some_handler )
    *retry = TRUE; /* Though failed to release enough, can try one last time. */
  return no_error;
}


Bool low_mem_handle(Bool *retry, corecontext_t *context,
                    size_t count, memory_requirement_t* requests)
{
  Bool no_error;
  Bool nested_call;

  HQASSERT(retry != NULL, "NULL retry");
  HQASSERT(context != NULL, "NULL context");
  HQASSERT(count > 0, "No requests");
  HQASSERT(requests != NULL, "NULL requests");

  if ( !enter_low_mem_handling(context, &nested_call, LOWMEM_HANDLE) )
    return FALSE; /* interrupted */

  no_error = low_mem_handle_guts(retry, context, count, requests);

  exit_low_mem_handling(context, nested_call, LOWMEM_HANDLE);

  return no_error;
}



Bool low_mem_handler_register(low_mem_handler_t *handler)
{
  sw_rdr_id rdr_id;
  sw_rdr_result res;

  HQASSERT(handler != NULL, "NULL handler");
  HQASSERT(memory_tier_min <= handler->tier
           && handler->tier < memory_tier_limit,
           "Invalid tier");
  HQASSERT(handler->solicit != NULL, "NULL solicit");
  HQASSERT(handler->release != NULL, "NULL release");

  res = SwRegisterRDRandID(RDR_CLASS_LOW_MEM, (sw_rdr_type)handler->tier,
                           &rdr_id, handler, 0, SW_RDR_NORMAL);

  tier_handlers[handler->tier].cache_valid = FALSE;

  if ( res != SW_RDR_SUCCESS ) {
    HQASSERT(res == SW_RDR_ERROR_MEMORY, "Unexpected RDR error");
    return error_handler(VMERROR);
  }
  handler->id = (low_mem_id)rdr_id;
  handler->running = FALSE;
  return TRUE;
}


void low_mem_handler_deregister(low_mem_handler_t *handler)
{
  corecontext_t *context = get_core_context();
  Bool nested_call;
  sw_rdr_result res;

  HQASSERT(context == NULL || context->mm_context == NULL ||
           !context->mm_context->low_mem_unsafe,
           "Solicit method attempting deregistration");
  HQASSERT(handler != NULL, "NULL handler");
  /* Can't assert id */
  HQASSERT(memory_tier_min <= handler->tier
           && handler->tier < memory_tier_limit,
           "Invalid tier");

  /* Without a context, can't tell if nested, but during init/finish, there's no
     way this could be inside a handler, anyway. */
  while ( !enter_low_mem_handling(context, &nested_call, LOWMEM_DEREGISTER_HANDLER) )
    EMPTY_STATEMENT(); /* If interrupted, retry; can't break contract. */

  tier_handlers[handler->tier].cache_valid = FALSE;

  res = SwDeregisterRDR(RDR_CLASS_LOW_MEM, (sw_rdr_type)handler->tier,
                        (sw_rdr_id)handler->id, handler, 0);
  HQASSERT(res == SW_RDR_SUCCESS ||
           res == SW_RDR_ERROR_UNKNOWN ||
           res == SW_RDR_ERROR_IN_USE, "Failed to deregister low-mem handler");

  exit_low_mem_handling(context, nested_call, LOWMEM_DEREGISTER_HANDLER);
}


/** Initial MM context values.
 *
 * Sets default cost to use all handlers and keep full reserves.
 */
#define INITIAL_MM_CONTEXT \
  { FALSE, FALSE, MM_COST_NORMAL, MM_COST_BELOW_RESERVES }


/** MM context for the main thread. */
static mm_context_t main_mm_context;


/** Context localiser for the MM context. */
static void mm_context_specialise(corecontext_t *context,
                                  context_specialise_private *data)
{
  mm_context_t mm_context = INITIAL_MM_CONTEXT;
  context->mm_context = &mm_context;
  context_specialise_next(context, data);
}


/** Structure for registering the MM context localiser. */
static context_specialiser_t mm_context_specialiser = {
  mm_context_specialise, NULL
};


/** Have the lowmem mutex/condvar been initialized? */
static Bool apportioner_inited = FALSE;


void apportioner_swinit(void)
{
  /* Needs to be early to get on all threads. */
  context_specialise_add(&mm_context_specialiser);
  /* Used by (de)registration. */
  multi_mutex_init(&lowmem_mutex, LOWMEM_LOCK_INDEX, FALSE,
                   SW_TRACE_LOWMEM_ACQUIRE, SW_TRACE_LOWMEM_HOLD);
  multi_condvar_init(&lowmem_condvar, &lowmem_mutex, SW_TRACE_LOWMEM_WAIT);
}


Bool apportioner_init(void)
{
  memory_tier tier;

  /* These can init late, because booting in low memory is nonsense. */
  apportioner_inited = TRUE;
  for ( tier = memory_tier_min ; tier < memory_tier_limit ; ++tier ) {
    if ( (tier_handlers[tier].it = SwFindRDRbyType(RDR_CLASS_LOW_MEM,
                                             (sw_rdr_type)tier)) == NULL )
      return FALSE;
  }
  CoreContext.mm_context = &main_mm_context;
  low_entry_sym = mps_telemetry_intern("low mem entry");
  low_exit_sym = mps_telemetry_intern("low mem exit");
  return TRUE;
}


void apportioner_finish(void)
{
  if ( apportioner_inited )
    clear_tier_handlers(TRUE);
  multi_condvar_finish(&lowmem_condvar);
  multi_mutex_finish(&lowmem_mutex);
}


void init_C_globals_apportioner(void)
{
  const mm_context_t init_context = INITIAL_MM_CONTEXT;

  clear_tier_handlers(FALSE);
  handling_low_memory = FALSE;
  low_mem_trace = FALSE;
  main_mm_context = init_context;
  mm_context_specialiser.next = NULL;
}


/* Log stripped */
