/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:deferred.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.

 * \brief
 * Deferred allocation interfaces.
 */

#ifndef __DEFERRED_H__
#define __DEFERRED_H__

#include "mm.h"


/** Type of deferred allocations. */
typedef struct deferred_alloc_t deferred_alloc_t;


/** Initialize a deferred allocation.

  \param[in] approx_types Approximate number of different types of allocation
                          (pool & cost)
  \return A deferred allocation handle or NULL, if ran out of memory (a
          VMERROR has been raised). */
deferred_alloc_t *deferred_alloc_init(size_t approx_types);


/** Type of memory allocation requests

  These describe memory allocations in a deferred allocation. */
typedef struct memory_request_t {
  mm_pool_t pool; /**< The pool the memory is needed in */
  size_t size; /**< The size of memory block required, in bytes */
  mm_alloc_class_t class; /* The allocation class */
  MM_DEBUG_LOCN_FIELDS
  struct memory_request_t *next;
    /**< Pointer to next request in this deferred allocation */
  size_t min_count; /**< The minimum number of memory blocks required */
  size_t max_count; /**< The maximum number of memory blocks required */
  mm_cost_t cost; /**< Cost of not acquiring the maximum, per byte */
  void **blocks;
    /**< The blocks allocated for this request, an array of max_count items */
} memory_request_t;


/** Static/auto initializer for \ref memory_request_t, parameterized. */
#define MEMORY_REQUEST_INIT(class, size, count) \
  { NULL, size, MM_ALLOC_CLASS_##class MM_DEBUG_LOCN_ARGS, \
    NULL, count, count, { memory_tier_min, 0.0 }, NULL }


/** Declaration for a \ref memory_request_t and its result array. */
#define MEMORY_REQUEST_VARS(name, class, size, count) \
  void *name##_blocks[count] = { NULL }; \
  memory_request_t name##_request = MEMORY_REQUEST_INIT(class, size, count)


/** Add a chain of requests to a deferred allocation.

  \param[in,out] dalloc  A deferred allocation.
  \param[in,out] requests  A memory request.
  \return  Success indicator.

  The lifetime of the request object must extend to \c
  deferred_alloc_finish() being invoked on this deferred allocation.

  Note that the next pointers will be modified by the allocator to chain
  all the requests of a deferred allocation together. Also, the size
  fields might be modified for various purposes. */
Bool deferred_alloc_add(deferred_alloc_t *dalloc, memory_request_t *requests);


/** Fill in typical fields in a request and add it to a deferred alloc.

  \param[in,out] dalloc  A deferred allocation.
  \param[in,out] request  A memory request.
  \param[in] pool  The pool to allocate from.
  \param[in,out] blocks  The array for the blocks allocated.
  \return  Success indicator.

  This is a convenience function for a typical \c
  deferred_alloc_add. The \a request has been initialized by \ref
  MEMORY_REQUEST_INIT, possibly amended afterwards, now the variable
  fields are filled in from the arguments, and it's added to \a dalloc.
 */
Bool deferred_alloc_add_simple(deferred_alloc_t *dalloc,
                               memory_request_t *request,
                               mm_pool_t pool,
                               void **blocks);


/** Realize a deferred allocation

  \param[in] dalloc  A deferred allocation.
  \param[in] min_cost  The cost of not getting the minimum, per byte.
  \retval FALSE if couldn't allocate min_count blocks for every request.
  \retval TRUE if could allocate at least min_count blocks for every request.

  All the requests in this deferred allocation are allocated, and the \c
  blocks fields are filled in with the results. Unrealized requests
  between \c min_count and \c max_count are filled in with \c NULL. If
  the system is unable to allocate even \c min_count blocks for any of
  the requests, the entire allocation fails and \c FALSE is returned.

  \note Callers who want to ignore running out of memory, should check
  that a different PS error has not been raised (by a low-memory
  handler). Typical callers, who just want to give up, should raise
  VMERROR (this will not override any earlier error).

  It's permissible to call this multiple times on the same argument, to
  allocate the same amount again.

  The blocks should be deallocated through normal \c mm_free(). */
Bool deferred_alloc_realize(deferred_alloc_t *dalloc,
                            mm_cost_t min_cost,
                            corecontext_t *context);


/** Finish a deferred allocation. */
void deferred_alloc_finish(deferred_alloc_t *dalloc);


#endif /* __DEFERRED_H__ */

/* Log stripped */
