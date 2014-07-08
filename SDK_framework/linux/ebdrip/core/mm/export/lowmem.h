/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:lowmem.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.

 * \brief
 * Core RIP low-memory handling interfaces.
 */

#ifndef __LOWMEM_H__
#define __LOWMEM_H__

#include "mm.h" /* mm_pool_t */


/** Is the RIP in an low-memory state?

  Testing this is effectively unsynchronized, but that's often good enough.
 */
extern Bool mm_memory_is_low;


/** Type of memory requirements.

  These are used to describe memory requirements to low-memory handlers. */
typedef struct {
  mm_pool_t pool; /**< The pool the memory is needed in, or NULL for unknown */
  size_t size; /**< The amount of memory required, in bytes */
  mm_cost_t cost; /**< Cost of not acquiring the memory, per byte */
} memory_requirement_t;


/** Type of memory release offers

  Low-memory handlers return chains of these to the apportioner to
  offer to release some memory. The apportioner may ask for the entire
  chain to the released at once, but is not obligated to take it as a
  unit. To accept the offer, the apportioner fills in the \a taken_size
  field in each offer and calls the \c low_mem_release_fn.

  The offer_size can be an estimate, so that the handler may not
  ultimately be able to release that much. OTOH, the handler may release
  more than the apportioner requests by \a taken_size (the data
  structure may be such that it's not possible to selectively release
  offers). However, the more accurate the estimate, the more efficiently
  the RIP will work.

  The cost includes the effort to free the memory and the effort to
  restore the data purged, should that become necesssary, scaled by the
  likelihood of having to restore it. */
typedef struct low_mem_offer_t {
  mm_pool_t pool; /**< The pool where memory is offered from */
  size_t offer_size; /**< The maximum size of memory offered,
                          in bytes (approximation) */
  float offer_cost; /**< The cost of freeing the memory offered (per byte) */
  size_t taken_size; /**< The part of the offer taken up in
                          \c low_mem_release_fn */
  struct low_mem_offer_t *next; /**< Pointer to the next part of the offer */
} low_mem_offer_t;


struct low_mem_handler_t;


/** Function type for soliciting a registered handler for an offer for
    some memory.

  \param[in] handler  The handler being solicited.
  \param[in] context  The core context.
  \param[in] count  Number of requests.
  \param[in] requests  An array of requests.

  This takes an array of requests, and returns an offer (or \c NULL), a
  chain of low_mem_offer_t's.

  Computing an offer should be a lightweight operation, since they're are
  solicited quite often. Solicit methods must not allocate, or invoke any
  function in this interface, directly or indirectly.

  If a solicit method takes a lock that precedes a Core lock in the partial
  order, it must be acquired by trylock. (Note that the monitor lock does not
  count as a Core lock, but the low-memory lock does.) Using trylock is strongly
  encouraged for any locks, as soliciting should be quick.

  The apportioner will not invoke a running handler (neither method) in
  a nested call to low-memory handling. Since only one thread can be
  doing low-memory handling at a time, there will be no calls to the
  handler from other threads, either. So handlers do not need to be
  reentrant or synchronize against themselves.

  Note that the offer structures are not freed after the apportioner is finished
  with them. The lifetime of an offer structure must be as long as the
  handler's. It is expected that the handlers preallocate them for each pool
  that they manage and reuse them for each call. */
typedef low_mem_offer_t *
          low_mem_solicit_fn(struct low_mem_handler_t *handler,
                             corecontext_t *context,
                             size_t count, memory_requirement_t* requests);


/** Function type for invoking registered low-mem handlers to free up some
    amount of memory.

  \param[in] handler  The handler being invoked.
  \param[in] context  The core context.
  \param[in] offer  The offer chain that is being taken up.
  \retval FALSE Returned if an error was raised.
  \retval TRUE Returned if there were no errors (whether or not any memory was freed).

  The \c taken_size fields in the offer have been filled in with the
  amount of memory requested from each pool. N.B., this could be more
  than \c offer_size (also, the size of the data structure managed may
  have changed since the offer).

  This doesn't return any success indicators because the apportioner
  will ask the pool and the arena, or simply try to allocate. The
  handler should strive to free at least the requested amount, but not
  more, except when done without significantly increasing the cost. It
  is permissible to fail to free anything, but in that case, the solicit
  method must eventually stop offering.

  A handler must not be deregistered during its own release method.

  If a release method takes a lock that precedes a Core lock in the partial
  order, it must be acquired by trylock. (Note that the monitor lock does not
  count as a Core lock, but the low-memory lock does.)

  The apportioner will not invoke a running handler (neither method) in
  a nested call to low-memory handling. Since only one thread can be
  doing low-memory handling at a time, there will be no calls to the
  handler from other threads, either. So handlers do not need to be
  reentrant or synchronize against themselves. */
typedef Bool low_mem_release_fn(struct low_mem_handler_t *handler,
                                corecontext_t *context, low_mem_offer_t *offer);


/** Type of low-memory handler identifiers. */
typedef int low_mem_id;


/** Description of a low-memory handler to register. */
typedef struct low_mem_handler_t {
  char *name; /**< Name of handler, for debugging */
  memory_tier tier; /**< Tier of memory managed */
  low_mem_solicit_fn *solicit; /**< Offer solicitation method */
  low_mem_release_fn *release; /**< Memory release method */
  Bool multi_thread_safe; /**< Is this handler mt-safe? */
  low_mem_id id; /**< Id of the handler, for internal use */
  Bool running; /**< Is this handler running, for internal use */
} low_mem_handler_t;


/** Register a low-memory handler.

  \param[in] handler  The description of the handler to register.

  The lifetime of the handler structure must extend until deregistration.
 */
Bool low_mem_handler_register(low_mem_handler_t *handler);


/** Deregister a low-memory handler.

  \param[in] handler  Pointer to the handler to deregister.

  This is thread-safe: It will wait if the handler is in use by another
  thread. The restrictions on the solicit methods guarantee a handler is not
  deregistered by the same thread between solicit and release.
 */
void low_mem_handler_deregister(low_mem_handler_t *handler);


/** Try to release enough free memory for the given requirements.

  \param[out] retry  Indicates if enough memory was freed.
  \param[in] context  The core context.
  \param[in] count  Number of requests.
  \param[in] requests  An array of requests.
  \retval FALSE Returned if an error was raised.
  \retval TRUE Returned if there were no errors (whether or not any memory was freed).

  This is typically called after a failed allocation from inside
  mm_alloc and friends. It may also be called from memory acquisition
  loops and fast cached allocation (see \c MM_SAC_ALLOC_FAST()).

  If \a *retry is set to \c FALSE, low-memory handling could not find
  enough free memory. The caller has to give up on this allocation (due
  to fragmentation, it may be useful to attempt the allocation once
  more before giving up).

  This facility is thread-safe and re-entrant. */
Bool low_mem_handle(Bool *retry, corecontext_t *context,
                    size_t count, memory_requirement_t* requests);


/** Adjust the level of reserves the system will attempt to maintain.

  \param[in] context  The core context.
  \param[in] full  Indicates if full reserves or only the final one is needed.
 */
void mm_set_reserves(mm_context_t *context, Bool full);


/** Check if the reserves (pool and arena) are used above the cost given.

  \param[in] limit  The limit (cost) allowed for reserve use.
 */
Bool mm_should_regain_reserves(mm_cost_t limit);


/** Regain reserves to the level set, at the cost given.

  \param[out] enough  Indicates if enough reserves were regained.
  \param[in] context  The core context.
  \param[in] cost  The cost limit for low-memory actions to use.

  The reserves are filled to level set in the context.
 */
Bool mm_regain_reserves(Bool *enough, corecontext_t *context,
                        mm_cost_t cost);

#ifdef ASSERT_BUILD
/** \brief Indicate if the current thread is handling low memory.

    \param[in] context  The core context.

    \retval TRUE  This thread is handling low memory.
    \retval FALSE This thread is not handling low memory.
 */
Bool mm_handling_low_memory(corecontext_t *context);
#endif

/**
 * Is the rip running in a resource poor system and has been given very little
 * memory on startup.
 *
 * Part of the performance prototyping involved reducing the size of a number
 * of fixed hash tables. This helped greatly when running in a limited
 * 24/12Mb configuration. The long term answer is to convert the various hash
 * tables into a more dynamic data structure, allowing the memory used to vary
 * as we are running. But that is a big change, so for now will just have to
 * stick with cutting the size of the hash tables down. But don't want to do
 * that all the time, as it might hurt performance in high-end memory-rich
 * systems. So add a temporary call to determine if we are in a low-mem
 * scenario or not.
 *
 * \todo BMJ 27-Jan-14 : Get rid of this when RIP hash tables made dynamic.
 */
Bool low_mem_configuration(void);

#endif /* __LOWMEM_H__ */

/* Log stripped */
