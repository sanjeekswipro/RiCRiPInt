/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:apportioner.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal interfaces between the apportioner and the rest of MM.
 *
 * Some of these interfaces are temporary, needed to integrate the old
 * and the new systems.
 */

#ifndef __APPORTIONER_H__
#define __APPORTIONER_H__

struct corecontext_t ;

#include "mm.h" /* mm_cost_t */
#include "lowmem.h" /* memory_requirement_t */


/** The MM part of the core context. */
struct mm_context_t {
  Bool handling_low_memory; /**< Is this thread handling? */
  Bool low_mem_unsafe; /**< The low-memory system is in a non-reentrant state.

                         This is just for debugging: finding any
                         illegal allocation in the low-mem code. */
  mm_cost_t default_cost; /**< Default cost of allocations. */
  mm_cost_t reserve_level; /**< Level of reserves that should be maintained. */
};


/** Initialize low-memory handling controls. */
Bool apportioner_init(void);


/** Initialize low-memory bits that need to be done early. */
void apportioner_swinit(void);


/** Finish low-memory handling controls */
void apportioner_finish(void);

/** Reasons that we might be entering low-memory handling. */
typedef enum {
  LOWMEM_ALLOC,
  LOWMEM_HANDLE,
  LOWMEM_DEREGISTER_HANDLER,
  LOWMEM_DEFERRED_ALLOC,
  LOWMEM_REGAIN_RESERVES
} lowmem_reason_t ;

/** Enter low-memory handling, serializing access.

    \param[in] context  The core context of this thread.
    \param[in] nested   Pointer to a boolean where the nested call status will
                        be stored. exit_low_mem_handling() must be called with
                        the boolean stored through this pointer.
    \param reason       Reason for entering low memory exclusion.
*/
Bool enter_low_mem_handling(/*@null@*/ struct corecontext_t *context,
                            /*@notnull@*/ /*@out@*/ Bool *nested,
                            lowmem_reason_t reason);


/** Exit low-memory handling, wake up next handler.
    \param[in] context  The core context of this thread.
    \param[in] nested   A boolean indicating if the low memory call is nested.
    \param reason       Reason for entering low memory exclusion.
*/
void exit_low_mem_handling(/*@null@*/ struct corecontext_t *context,
                           Bool nested, lowmem_reason_t reason);


/** Try to release enough free memory for the given requirements, when
    synchronized.

  \param[out] retry  Indicates if enough memory was freed.
  \param[in]  context  The core context.
  \param[in]  count  Number of requests.
  \param[in]  requests  An array of requests.
  \retval FALSE Returned if an error was raised.
  \retval TRUE Returned if there were no errors (whether or not any memory was freed).

  This is like \ref low_mem_handle(), except the caller must
  synchronize, using \ref enter_low_mem_handling(). */
Bool low_mem_handle_guts(Bool *retry, corecontext_t *context,
                         size_t count, memory_requirement_t* requests);


/** Check that cost is valid for allocation. */
Bool is_valid_cost(const mm_cost_t cost);


#endif /* __APPORTIONER_H__ */

/* Log stripped */
