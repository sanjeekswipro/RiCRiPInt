/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!export:mlock.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Multi-process resource lock functions
 */

#ifndef __MLOCK_H__
#define __MLOCK_H__ 1

#include "threadapi.h"
#include "hqatomic.h"

/**
 * \defgroup multi Multi-processing support.
 * \ingroup core
 * \{ */


struct core_init_fns ;
void mlock_C_globals(
  struct core_init_fns* fns);

/** \brief Limit on the number of threads. */
#define NTHREADS_LIMIT 32


typedef unsigned int thread_id_t;


/* Lightweight locks
 * =================
 */

/** \brief Mutex lock indices.
 *
 * Ordering does not matter here for rank determination: there is a partial
 * order table in mlock.c that determines whether it is OK to claim a lock
 * when others are held. However, the order does correspond to the \c
 * lock_partial_order table in mlock.c, so new entries should be added at the
 * end of the list (just before LOCK_RANK_LIMIT). The partial order table
 * MUST be updated when adding a new lock.
 *
 * There must be 31 or fewer lock indices.
 */
typedef enum {
  SOLE_LOCK_CLASS, /* 0 */
  ASYNCPS_LOCK_INDEX,
  MONITOR_LOCK_INDEX,
  LOWMEM_LOCK_INDEX,
  TRAP_LOCK_INDEX,
  PGB_LOCK_INDEX,
  IMBLOCK_LOCK_INDEX,
  HT_LOCK_INDEX,
  HT_FORMCLASS_INDEX,
  HT_CACHE_LOCK_INDEX,
  NFILL_LOCK_INDEX, /* 10 */
  GOURAUD_LOCK_INDEX,
  BACKDROP_LOCK_INDEX,
  RSD_LOCK_INDEX,
  IMSTORE_LOCK_INDEX,
  RESERVE_LOCK_INDEX,
  TASK_LOCK_INDEX,
  COLCVT_LOCK_INDEX,
  RES_LOOKUP_LOCK_INDEX,
  INPUTPAGE_LOCK_INDEX,
  OUTPUTPAGE_LOCK_INDEX, /* 20 */
  RETAINEDRASTER_LOCK_INDEX,
  REQ_NODE_LOCK_INDEX,
  IRR_LOCK_INDEX,
  GST_LOCK_INDEX,
  LOCK_RANK_LIMIT /* Must be last: length of lock table */
} lock_rank_t;

/* Mutexes */

typedef struct multi_mutex_t {
  pthread_mutex_t pmutex;
  lock_rank_t rank; /**< Rank for deadlock control */
#ifdef ASSERT_BUILD
  unsigned int nlocks;  /**< Recursive lock count. */
#endif /* ASSERT_BUILD */
#ifdef PROBE_BUILD
  int wait_trace ;  /**< Probe entered while waiting on mutex. */
  int hold_trace ;  /**< Probe entered while holding mutex. */
#endif
} multi_mutex_t;

void multi_mutex_init(multi_mutex_t *mutex, lock_rank_t rank,
                      Bool recursive, int wait_trace, int hold_trace);
void multi_mutex_finish(multi_mutex_t *mutex);
void multi_mutex_lock(multi_mutex_t *mutex);
Bool multi_mutex_trylock(multi_mutex_t *mutex);
void multi_mutex_unlock(multi_mutex_t *mutex);

#ifdef ASSERT_BUILD
/** \brief Assertion function to determine when a mutex is locked.

    This function uses the trylock interface to test if a mutex is locked,
    but it returns the mutex to its previous state. It can be used in
    assertions. */
Bool multi_mutex_is_locked(multi_mutex_t *lock) ;
#endif


/* Condition variables */

typedef struct multi_condvar_t {
  pthread_cond_t pcond;
  multi_mutex_t *mutex;
  /** The condition variable refcount isn't an hq_atomic_counter_t because
      for nearly all use cases the associated mutex will be locked when it
      is manipulated anyway. So I've decided to avoid the extra memory barrier
      and decree that the associated mutex must be locked when manipulating the
      refcount in all cases. */
  int refcount;
#ifdef PROBE_BUILD
  int wait_trace ;  /**< Probe entered while waiting on condition. */
#endif
} multi_condvar_t;

/** \brief Initialise a static or stack-allocated condition variable.

    \param cond   The condition variable to initialise
    \param mutex  The mutex that the condition variable is based on.  See
                  \c multi_condvar_wait() for details on how this is used.
    \param wait_trace  A probe ID that will be used to instrument the amount
                  of time waiting in the condition. If SW_TRACE_INVALID is
                  given, no instrumentation probes will be called.

    \note This function will assert if the underlying pthread function fails.
    It should return an error instead. This condition variable may have have
    extra references to it acquired and released using
    \c multi_condvar_acquire() and \c multi_condvar_release(), so long as
    they nest correctly. It must be finalised using \c multi_condvar_finish().
*/
void multi_condvar_init(/*@out@*/ /*@notnull@*/ multi_condvar_t *cond,
                        /*@in@*/ /*@notnull@*/ multi_mutex_t *mutex,
                        int wait_trace);

/** \brief Destroy a static or stack-allocated condition variable.

    \param cond   The condition variable to destroy

    This function should never be used on a dynamically-allocated condition
    variable. */
void multi_condvar_finish(/*@in@*/ /*@notnull@*/ multi_condvar_t *cond);

/** \brief Allocate and initialise a dynamically-allocated condition
    variable.

    \param mutex  The mutex that the condition variable is based on. See
                  \c multi_condvar_wait() for details on how this is used.
    \param wait_trace  A probe ID that will be used to instrument the amount
                  of time waiting in the condition. If SW_TRACE_INVALID is
                  given, no instrumentation probes will be called.

    \returns A pointer to an initialised condition variable if successful, or
    NULL if not successful. If successful, the condition variable created
    must be released by \c multi_condvar_release(). It must \b not be
    finalised with \c multi_condvar_finish().

    \note Since this function creates a new condition variable, which cannot
    have any other references to it, there is no need to have the underlying
    mutex locked. */
/*@notnull@*/
multi_condvar_t *multi_condvar_create(/*@in@*/ /*@notnull@*/ multi_mutex_t *mutex,
                                      int wait_trace) ;

/** \brief Clone a reference to a condition variable.

    \param cond  The condition variable pointer to clone.

    \returns Another pointer referencing the condition variable. The cloned
    reference \b must be released with \c multi_condvar_release().

    \note The condition variable's underlying mutex \b must be locked while
    making this call. */
/*@dependent@*/ /*@notnull@*/
multi_condvar_t *multi_condvar_acquire(/*@in@*/ /*@notnull@*/ multi_condvar_t *cond) ;

/** \brief Release a reference to a condition variable.

    \param condptr  A pointer to the condition variable reference to release.
                  On entry, the reference must represent a valid condition
                  variable. On exit, the condition variable pointer is NULL.

    When the last reference to a dynamically-allocated condition variable is
    released, the condition variable is destroyed. All condition variable
    references acquired through \c multi_condvar_create() and \c
    multi_condvar_acquire() MUST be released.

    \note The condition variable's underlying mutex \b must be locked while
    making this call. */
void multi_condvar_release(/*@in@*/ /*@notnull@*/ multi_condvar_t **condptr) ;

/** \brief Wait on a condition variable being signalled.

    \param cond   The condition variable to wait on.

    This function must be called with the condition variable's underlying
    mutex locked; the condition variable releases it while waiting, then
    atomically re-locks the mutex when the wait completes. Callers of this
    function should always use a guard condition and loop around the wait. The
    condition variable may be signalled spuriously by interrupts, and may
    also be signalled when the RIP is quitting. */
void multi_condvar_wait(/*@in@*/ /*@notnull@*/ multi_condvar_t *cond);

/** \brief Wait on a condition variable being signalled, or until a specified
    time.

    \param cond   The condition variable to wait on.
    \param abstimeus   The condition variable to wait on.

    \retval TRUE  The timeout expired.
    \retval FALSE The condition variable was signalled.

    This function must be called with the condition variable's underlying
    mutex locked; the condition variable releases it while waiting, then
    atomically re-locks the mutex when the wait completes. Callers of this
    function should always use a guard condition and loop around the wait.
    The condition variable may be signalled spuriously by interrupts, and may
    also be signalled when the RIP is quitting.

    Timed waits require access to an absolute time, expressed in microseconds
    since the start of the pthread epoch, when the time will expire. */
Bool multi_condvar_timedwait(/*@in@*/ /*@notnull@*/ multi_condvar_t *cond,
                             HqU32x2 abstimeus);

/** \brief Signal that one thread can be released from a condition variable
    wait.

    \param cond   The condition variable to signal.

    If there are any threads waiting on the condition variable, one of them
    is allowed to proceed. Note that this only allows current threads waiting
    on a condition variable to proceed: it is not a semaphore operation. */
void multi_condvar_signal(/*@in@*/ /*@notnull@*/ multi_condvar_t *cond);

/** \brief Signal that all threads can be released from a condition variable
    wait.

    \param cond   The condition variable to signal.

    If there are any threads waiting on the condition variable, all of them
    are allowed to proceed. Note that this only allows current threads waiting
    on a condition variable to proceed: it is not a semaphore operation. */
void multi_condvar_broadcast(/*@in@*/ /*@notnull@*/ multi_condvar_t *cond);


/**** One-item-per-thread read-write locks ****/


typedef struct {
  pthread_cond_t wcond;
  pthread_cond_t rcond;
  int state; /* -1: write-locked, 0: unlocked, >0: read-locked <state> times */
  unsigned int waiters;
  void *item;
} multi_rwlock_item_t;


typedef struct {
  multi_rwlock_item_t items[NTHREADS_LIMIT];
  size_t thread_locks[NTHREADS_LIMIT];
  pthread_mutex_t pmutex;
  lock_rank_t rank;
  unsigned int waiters ; /**< Total number of waiters. */
#ifdef PROBE_BUILD
  int wait_trace ;   /**< Probe entered while locking. */
  int read_trace ;   /**< Probe entered while holding read lock. */
  int write_trace ;  /**< Probe entered while holding write lock. */
#endif
} multi_rwlock_t;


enum { MULTI_RWLOCK_WRITE, MULTI_RWLOCK_READ };


void multi_rwlock_init(multi_rwlock_t *lock, lock_rank_t rank,
                       int wait_trace, int read_trace, int write_trace);
void multi_rwlock_finish(multi_rwlock_t *lock);
void multi_rwlock_lock(multi_rwlock_t *lock, void *item, int mode);
void multi_rwlock_unlock(multi_rwlock_t *lock);

/** \brief Unlock the rwlock if another thread wants it.

    \param lock  The read-write lock.

    \retval TRUE  The read-write lock was unlocked.
    \retval FALSE The read-write lock was not unlocked.
*/
Bool multi_rwlock_unlock_if_wanted(multi_rwlock_t *lock);
void multi_rwlock_wr_to_rd(multi_rwlock_t *lock, void *item);
Bool multi_rwlock_check(multi_rwlock_t *lock, void *item, int mode);

/** \} */

#endif /* protection for multiple inclusion */


/* Log stripped */
