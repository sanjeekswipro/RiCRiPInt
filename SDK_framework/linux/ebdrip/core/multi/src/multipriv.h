/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!src:multipriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal interfaces within multi-thread compound.
 */

#ifndef __MULTIPRIV_H__
#define __MULTIPRIV_H__

#include "threadapi.h"
#include "mlock.h"
#include "taskres.h"

/** \ingroup multi */
/** \{ */

/** \brief Mark type for task graph walking algorithms. */
typedef int task_mark_t ;

/** \brief Get a task mark value suitable for initialising requirements. */
task_mark_t requirement_mark_init(void) ;

/** \brief Separate pool for task and multi-thread primitive allocations. */
extern mm_pool_t mm_pool_task ;

/** \brief Internal function to perform a pthread condition wait, dependent
    on the current task not being cancelling.

    This function is used when doing condition variable waits, to narrow the
    window for potential deadlock if a thread is not woken.

    \param[in] cond  The condition to wait upon. */
void multi_condvar_wait_task(multi_condvar_t *cond) ;

/** \brief Raw version of multi_condvar_wait() without task cancellation
    marking, for use by task system only.

    \param[in] cond  The condition to wait upon. */
void multi_condvar_wait_raw(multi_condvar_t *cond) ;

/** \brief An array or open addressed hash table from ID to resource pointer
    for a resource assignment or pool. */
struct resource_lookup_t {
  unsigned int nentries ; /**< Number of entries in hash table. */
  resource_entry_t *entries[1] ; /**< Extendible array. */
} ;

#define resource_lookup_size(n_)                                        \
  (offsetof(resource_lookup_t, entries[0]) +                            \
   (n_) * (offsetof(resource_lookup_t, entries[1]) -                    \
           offsetof(resource_lookup_t, entries[0])))

/** \brief Get the start index to look for an entry in a resource lookup.

    \param[in] lookup  A lookup table to search.
    \param id          The resource Id to look for first.

    \return An unsigned integer in the range of the lookup table.
*/
unsigned int resource_lookup_first(resource_lookup_t *lookup,
                                   resource_id_t id) ;

/** \brief Insert a resource entry into a lookup table.

    \param[in,out] lookup  A lookup table to insert the entry into.
    \param[in] entry       The resource entry to insert.

    The lookup table must be locked while the entry is inserted.
 */
void resource_lookup_insert(resource_lookup_t *lookup,
                            resource_entry_t *entry) ;

/** \brief Lock a resource lookup table.

    \param[in,out] pool_  The pool in which the lookup table is located.
    \param[out] out_      The dereferencable pointer for the locked table.

    This uses a lightweight locking mechanism to prevent a lookup table from
    being replaced whilst clients are accessing it. */
#define resource_lookup_lock(pool_, out_) MACRO_START                   \
  VERIFY_OBJECT((pool_), RESOURCE_POOL_NAME) ;                          \
  spinlock_pointer_checked(&(pool_)->lookup, RES_LOOKUP_LOCK_INDEX, out_) ; \
MACRO_END

/** \brief Try to lock a resource lookup table.

    \param[in,out] pool_  The pool in which the lookup table is located.
    \param[out] out_      The dereferencable pointer for the locked table.
    \param[out] didlock_  A Boolean to indicate if managed to lock.

    This uses a lightweight locking mechanism to prevent a lookup table from
    being replaced whilst clients are accessing it. */
#define resource_lookup_trylock(pool_, out_, didlock_) MACRO_START      \
  VERIFY_OBJECT((pool_), RESOURCE_POOL_NAME) ;                          \
  spintrylock_pointer_checked(&(pool_)->lookup, RES_LOOKUP_LOCK_INDEX,  \
                              out_, didlock_);                          \
MACRO_END

/** \brief Unlock a resource lookup table.

    \param[in,out] pool_  The pool in which the lookup table is located.
    \param[in] ulck_      The new lookup table pointer.
*/
#define resource_lookup_unlock(pool_, ulck_) MACRO_START                \
  VERIFY_OBJECT((pool_), RESOURCE_POOL_NAME) ;                          \
  spinunlock_pointer_checked(&(pool_)->lookup, RES_LOOKUP_LOCK_INDEX, ulck_) ; \
MACRO_END

/** \brief Create a resource entry for a pool.

    \param[in] pool  The pool in which to create an entry.
    \param[in] owner The owner to assign to the new entry.
    \param[in] cost  The allocation cost for the entry.

    \return An unowned resource entry, or NULL on failure. This routine
            does not raise a VMERROR if it fails, because it may be used
            to speculatively allocate entries when fixing resources.

    \note The resource entry is counted against the pool's \c nresources
    field, however it is the caller's responsibility to insert the entry into
    the pool's lookup table. */
resource_entry_t *resource_entry_create(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
                                        /*@notnull@*/ /*@in@*/ mm_cost_t cost,
                                        /*@null@*/ void *owner) ;

/** \brief Free a resource entry from a pool.

    \param[in] pool  The pool from which to free the entry.
    \param[in] ptr   Where the resource entry to free is stored.

    \note The resource entry is counted against the pool's \c nresources
    field, however it is the caller's responsibility to remove the entry from
    the pool's lookup table. */
void resource_entry_free(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
                         /*@notnull@*/ /*@in@*/ /*@out@*/ resource_entry_t **ptr) ;

/** \brief Requirements for the resources a task group needs to run.

    A resource requirement can be seen as a template for the task group
    structure that will be created later. Task groups may be associated with
    an expression in the requirement's tree, requiring all of the minimum
    resources specified by that expression to be available before any of the
    group's task are scheduled. Not every node in the expression tree
    corresponds to a task group, some are intermediate nodes storing partial
    computation values. */
struct resource_requirement_t {
  hq_atomic_counter_t refcount ;
  requirement_state_t state ;  /**< Can we provision using this requirement? */
  task_mark_t mark ;           /**< Visitor mark for provision walk. */
  requirement_node_t *node ;   /**< Expression tree. */
  /** Resource pools for each resource type (refcounted). The pool pointers
      are only modified by resource_requirement_set_pool() and
      resource_requirement_remove_pool(). */
  resource_pool_t *pools[N_RESOURCE_TYPES] ;
  OBJECT_NAME_MEMBER
} ;

#define RESOURCE_REQUIREMENT_NAME "Task resource requirement"

/** \brief A node in the resource requirement expression tree, collecting
    computed values for minimum and maximum resource allocations. */
struct requirement_node_t {
  requirement_node_id id ;           /**< ID used to find expression. */
  requirement_op_t op ;              /**< Operator combining children. */
  resource_requirement_t *requirement ; /**< Requirement this expression is attached to (not refcounted). */
  requirement_node_t *left, *right ; /**< Operands for combining op. */
  unsigned int ngroups ;             /**< Number of groups provisioned. */
  unsigned int smin ;                /**< Minimum simultaneous instances. */
  unsigned int smax ;                /**< Maximum simultaneous instances. */
  unsigned int minimum[N_RESOURCE_TYPES] ; /**< Minimum of these required. */
  unsigned int maximum[N_RESOURCE_TYPES] ; /**< Maximum of these we can use. */
  OBJECT_NAME_MEMBER
} ;

#define REQUIREMENT_NODE_NAME "Task resource requirement node"

/** \brief Return the minimum number of groups that a node must be able to
    provision simultaneously. */
#define requirement_smin(node_, nthreads_)                              \
  ((node_)->smin != REQUIREMENT_SIMULTANEOUS_THREADS                    \
   ? (node_)->smin                                                      \
   : (unsigned int)(nthreads_))

/** \brief Return the maximum number of groups that a node may provision
    simultaneously. */
#define requirement_smax(node_, nthreads_)                              \
  ((node_)->smax != REQUIREMENT_SIMULTANEOUS_THREADS                    \
   ? (node_)->smax                                                      \
   : (node_)->smin == REQUIREMENT_SIMULTANEOUS_THREADS                  \
   ? (unsigned int)(nthreads_)                                          \
   : max((unsigned int)(nthreads_), (node_)->smin))

/** \brief Lock a resource requirement expression tree.

    \param[in,out] ptr_  The requirement containing the expression tree.
    \param[out] out_     The dereferencable pointer for the resource expression.

    This uses a lightweight locking mechanism to prevent a resource
    expression tree table from being modified whilst clients are accessing
    it. */
#define requirement_node_lock(req_, out_) MACRO_START                   \
  VERIFY_OBJECT((req_), RESOURCE_REQUIREMENT_NAME) ;                    \
  spinlock_pointer_checked(&(req_)->node, REQ_NODE_LOCK_INDEX, out_) ;  \
MACRO_END

/** \brief Unlock a resource requirement expression tree.

    \param[in,out] ptr_  The requirement containing the expression tree.
    \param[in] ulck_     The new expression tree pointer.
*/
#define requirement_node_unlock(req_, ulck_) MACRO_START                \
  VERIFY_OBJECT((req_), RESOURCE_REQUIREMENT_NAME) ;                    \
  spinunlock_pointer_checked(&(req_)->node, REQ_NODE_LOCK_INDEX, ulck_) ; \
MACRO_END

/** \brief Spinlock a pointer, checking for deadlocks using a lock rank.

    \param[in,out] ptr_   The address of the pointer to lock.
    \param[in] rank_      The lock index to check.
    \param[out] out_      The dereferencable version of the locked pointer.

    This uses a lightweight locking mechanism to prevent a pointer being
    accessed. */
#define spinlock_pointer_checked(ptr_, rank_, out_) MACRO_START         \
  check_deadlock_assertions(rank_) ; \
  spinlock_pointer(ptr_, out_, HQSPIN_YIELD_ALWAYS) ;                   \
  record_lock_rank_held(rank_); \
MACRO_END

/** \brief Try to spinlock a pointer, checking for deadlocks using a lock rank.

    \param[in,out] ptr_   The address of the pointer to lock.
    \param[in] rank_      The lock index to check.
    \param[out] out_      The dereferencable version of the locked pointer.
    \param[out] didlock_  A Boolean to indicate if managed to lock.

    This uses a lightweight locking mechanism to prevent a pointer being
    accessed. */
#define spintrylock_pointer_checked(ptr_, rank_, out_, didlock_) MACRO_START \
  spintrylock_pointer(ptr_, out_, didlock_);                            \
  if ( didlock_ ) { \
    record_lock_rank_held(rank_); \
  } \
MACRO_END

/** \brief Unlock a spinlocked pointer.

    \param[in,out] ptr_   The address of the pointer to lock.
    \param[in] rank_      The lock index to check.
    \param[in] ulck_      The new pointer after unlocking.
*/
#define spinunlock_pointer_checked(ptr_, rank_, ulck_) MACRO_START       \
  clear_lock_rank_held(rank_); \
  spinunlock_pointer(ptr_, ulck_) ;                                     \
MACRO_END


#ifdef ASSERT_BUILD
/** Assertion test to check that no locks are held by the calling thread. */
Bool assert_test_no_locks_held(void) ;
void check_deadlock_assertions(lock_rank_t rank) ;
void verify_pool_entries(resource_pool_t *pool, resource_lookup_t *lookup) ;
void record_lock_rank_held(lock_rank_t rank);
void clear_lock_rank_held(lock_rank_t rank);

/* Assert that the resource lookup table is locked (if tf_ is TRUE) or
   unlocked (if tf_ is FALSE). */
#define ASSERT_LOOKUP_LOCKED(pool_, tf_, msg_) MACRO_START     \
  resource_lookup_t *_lookup_ = NULL ; /* stupid MSVC */       \
  Bool _locked_ ;                                              \
  VERIFY_OBJECT((pool_), RESOURCE_POOL_NAME) ;                 \
  spintrylock_pointer(&(pool_)->lookup, _lookup_, _locked_);   \
  if ( _locked_ )                                              \
    spinunlock_pointer(&(pool_)->lookup, _lookup_) ;           \
  HQASSERT(_locked_ != (tf_), msg_) ;                          \
MACRO_END

/* Assert that the requirement tree is locked (if tf_ is TRUE) or unlocked
   (if tf_ is FALSE). */
#define ASSERT_NODE_LOCKED(req_, tf_, msg_) MACRO_START           \
  requirement_node_t *_node_ = NULL ; /* stupid MSVC */           \
  Bool _locked_ ;                                                 \
  VERIFY_OBJECT((req_), RESOURCE_REQUIREMENT_NAME) ;              \
  spintrylock_pointer(&(req_)->node, _node_, _locked_);           \
  if ( _locked_ )                                                 \
    spinunlock_pointer(&(req_)->node, _node_) ;                   \
  HQASSERT(_locked_ != (tf_), msg_) ;                             \
MACRO_END

#else /* !ASSERT_BUILD */
#define check_deadlock_assertions(n) EMPTY_STATEMENT()
#define verify_pool_entries(p, l) EMPTY_STATEMENT()
#define record_lock_rank_held(r) EMPTY_STATEMENT()
#define clear_lock_rank_held(r) EMPTY_STATEMENT()

#define ASSERT_LOOKUP_LOCKED(pool_, tf_, msg_) EMPTY_STATEMENT()
#define ASSERT_NODE_LOCKED(req_, tf_, msg_) EMPTY_STATEMENT()
#endif /* !ASSERT_BUILD */

#if defined(METRICS_BUILD)
extern int requirements_total_allocated ;
extern int requirements_current_allocated ;
extern int requirements_peak_allocated ;
#endif

/** \} */

#endif /* __MULTIPRIV_H__ */

/* Log stripped */
