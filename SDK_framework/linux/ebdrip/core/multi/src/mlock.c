/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!src:mlock.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Multi-threading synchronization features and thread-local variables.
 */

#ifndef MACINTOSH
#define _XOPEN_SOURCE 500 /* pthread_mutexattr_settype */
#endif

#include "core.h"
#include "coreinit.h"
#include "mm.h"
#include "lowmem.h"
#include "mlock.h"
#include "ripmulti.h"
#include "multipriv.h"
#include "hqbitops.h"
#include "timing.h"
#include "swstart.h" /* SwThreadIndex() */
#include "threadapi.h"
#include <stdlib.h> /* abs */
#include <time.h> /* time_t */
#include "hqspin.h" /* Must be last include because of hqwindows.h */

/* Versions of the probe start/end that only probe if the id is not invalid.
   These are used to give all multi-control structures automatic trace
   IDs. */
#ifdef PROBE_BUILD

#define multi_probe_begin(_mp, _f) MACRO_START \
  if ( (_mp)->_f != SW_TRACE_INVALID ) \
    probe_begin((_mp)->_f, (_mp)) ; \
MACRO_END

#define multi_probe_end(_mp, _f) MACRO_START \
  if ( (_mp)->_f != SW_TRACE_INVALID ) \
    probe_end((_mp)->_f, (_mp)) ; \
MACRO_END

#else /* !PROBE_BUILD */
#define multi_probe_begin(_mp, _f) EMPTY_STATEMENT()
#define multi_probe_end(_mp, _f) EMPTY_STATEMENT()
#endif /* !PROBE_BUILD */

/*********** Initialization ***********/


/**** Deadlock checking ****/
/* Use an unsigned 32bit integer since the current BIT etc. macros all explicit
 * produce 32bit unsigned integers which cause warnings when combined with
 * 64bit integers (e.g. uintptr_t etc.)
 */
typedef uint32 lock_bits_t ;

#if defined( ASSERT_BUILD )

/* Helper macros to define the lock rank table */
#define ALLOW_CONCURRENT  TRUE, 0
#define NOT_CONCURRENT    FALSE, 0

/** \brief Lock rank tracking table.

    There is one entry per lock rank:
    o a bit vector of other lock ranks that must not be held at the time the
      lock rank is taken.

    Additional entries to help track lock-rank usage.
    o a flag to indicate if locks of the lock rank can be concurrently held.
    o the TLS key for the count of concurrently-held locks of the lock rank
*/
static
struct LOCK_RANK {
  lock_bits_t   partial_order;
  Bool          concurrent;  /* Can be held concurrently. */
  pthread_key_t nlocks;      /* Concurrency count is per thread, tracked via TLS */
} lock_rank[LOCK_RANK_LIMIT] = {
  /* SOLE_LOCK_CLASS cannot be held with anything else */
  {BITS_ALL, NOT_CONCURRENT},
  /* ASYNCPS_LOCK_INDEX can be held with anything else, except itself. */
  {BIT(ASYNCPS_LOCK_INDEX), NOT_CONCURRENT},
  /* MONITOR_LOCK_INDEX can be claimed with any lock held except itself */
  {BIT(MONITOR_LOCK_INDEX), NOT_CONCURRENT},
  /* LOWMEM_LOCK_INDEX can be claimed with any lock held except the monitor */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX), NOT_CONCURRENT},
  /* TRAP_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) |
    BIT(BACKDROP_LOCK_INDEX) | BIT(TRAP_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* PGB_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(TRAP_LOCK_INDEX) |
    BIT(PGB_LOCK_INDEX), NOT_CONCURRENT},
  /* IMBLOCK_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(TRAP_LOCK_INDEX) |
    BIT(PGB_LOCK_INDEX) | BIT(IMBLOCK_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* HT_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(HT_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* HT_FORMCLASS_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(HT_FORMCLASS_INDEX),
    NOT_CONCURRENT},
  /* HT_CACHE_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX), NOT_CONCURRENT},
  /* NFILL_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(TRAP_LOCK_INDEX) |
    BIT(PGB_LOCK_INDEX) | BIT(IMBLOCK_LOCK_INDEX) |
    BIT(HT_LOCK_INDEX) | BIT(NFILL_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* GOURAUD_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(TRAP_LOCK_INDEX) |
    BIT(PGB_LOCK_INDEX) | BIT(IMBLOCK_LOCK_INDEX) |
    BIT(HT_LOCK_INDEX) | BIT(NFILL_LOCK_INDEX) | BIT(GOURAUD_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* BACKDROP_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX), NOT_CONCURRENT},
  /* RSD_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX), NOT_CONCURRENT},
  /* IMSTORE_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(IMSTORE_LOCK_INDEX),
    NOT_CONCURRENT},
  /* RESERVE_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(RESERVE_LOCK_INDEX),
    NOT_CONCURRENT},
  /* TASK_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(TASK_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* COLCVT_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) | BIT(COLCVT_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* RES_LOOKUP_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX), ALLOW_CONCURRENT},
  /* INPUTPAGE_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) |
    BIT(INPUTPAGE_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* OUTPUTPAGE_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) |
    BIT(INPUTPAGE_LOCK_INDEX) | BIT(OUTPUTPAGE_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* RETAINEDRASTER_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) |
    BIT(RETAINEDRASTER_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), NOT_CONCURRENT},
  /* REQ_NODE_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) |
    BIT(RES_LOOKUP_LOCK_INDEX) | BIT(REQ_NODE_LOCK_INDEX), ALLOW_CONCURRENT},
  /* IRR_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX), NOT_CONCURRENT},
  /* GST_LOCK_INDEX */
  {BIT(MONITOR_LOCK_INDEX) | BIT(LOWMEM_LOCK_INDEX) |
    BIT(GST_LOCK_INDEX), NOT_CONCURRENT}
};

#define VALID_LOCK_RANK(r)  ((r) >= 0 && (r) < LOCK_RANK_LIMIT)


/* Utility macros to access a TLS pointer as uintptr_t */
#define GET_TLS(key_) ((uintptr_t)pthread_getspecific(key_))

#define SET_TLS(key_, value_) \
  MACRO_START \
    int res; \
    res = pthread_setspecific((key_), (void*)((uintptr_t)value_)); \
    HQASSERT(res == 0, "failed to set TLS value"); \
  MACRO_END


/* Per thread held-lock bit vector implemented using TLS, abusing the TLS
 * pointer so that no actual storage needs to be allocated.
 */
static pthread_key_t locks_held_per_thread_key;

/* Use a uintptr_t for the lock-rank concurrency count to minimise casting when
 * accessing/storing in TLS pointer.
 */
typedef uintptr_t lock_rank_nlocks_t;

/* Get lock-rank bit vector from TLS pointer */
#define GET_LOCKS_HELD() \
  CAST_UINTPTRT_TO_UINT32(GET_TLS(locks_held_per_thread_key))

/* Set lock-rank bit vector in TLS pointer */
#define SET_LOCKS_HELD(lh)  SET_TLS(locks_held_per_thread_key, (lh))

/* Get lock-rank nlocks count */
#define GET_LOCK_RANK_NLOCKS(rank_) GET_TLS(lock_rank[rank_].nlocks)

/* Set lock-rank nlocks count */
#define SET_LOCK_RANK_NLOCKS(rank_, nlocks_) \
  SET_TLS(lock_rank[rank_].nlocks, (nlocks_))


/* Is lock currently held by the thread */
#define IS_HELD_LOCK_RANK(locks, rank) ((locks) & BIT(rank))

/* Record new lock held by thread */
void record_lock_rank_held(
  lock_rank_t rank)
{
  lock_bits_t locks_held = GET_LOCKS_HELD();
  lock_rank_nlocks_t nlocks;

  HQASSERT(VALID_LOCK_RANK(rank), "Invalid rank");

  if (lock_rank[rank].concurrent) {
    nlocks = GET_LOCK_RANK_NLOCKS(rank);
    if (nlocks++ == 0) {
      HQASSERT(!IS_HELD_LOCK_RANK(locks_held, rank), "Recording lock already held");
      locks_held |= BIT(rank);
    }
    SET_LOCK_RANK_NLOCKS(rank, nlocks);
  } else {
    HQASSERT(!IS_HELD_LOCK_RANK(locks_held, rank), "Recording lock already held");
    locks_held |= BIT(rank);
  }
  SET_LOCKS_HELD(locks_held);
}

/* Clear existing lock held by thread */
void clear_lock_rank_held(
  lock_rank_t rank)
{
  lock_bits_t locks_held = GET_LOCKS_HELD();
  lock_rank_nlocks_t nlocks;

  HQASSERT(VALID_LOCK_RANK(rank), "Invalid rank");

  if (lock_rank[rank].concurrent) {
    nlocks = GET_LOCK_RANK_NLOCKS(rank);
    if (--nlocks == 0) {
      HQASSERT(IS_HELD_LOCK_RANK(locks_held, rank), "Clearing lock not held");
      locks_held &= ~BIT(rank);
    }
    SET_LOCK_RANK_NLOCKS(rank, nlocks);
  } else {
    HQASSERT(IS_HELD_LOCK_RANK(locks_held, rank), "Clearing lock not held");
    locks_held &= ~BIT(rank);
  }
  SET_LOCKS_HELD(locks_held);
}

/* Setup mutex lock tracking */
#define setup_mutex_lock(m_, r_) \
  MACRO_START \
    multi_mutex_t* mutex_ = (m_); \
    mutex_->rank = (r_); \
    mutex_->nlocks = 0; \
  MACRO_END

/* Record new lock on mutex */
#define record_mutex_lock(m_) \
  MACRO_START \
    multi_mutex_t* mutex_ = (m_); \
    if (mutex_->nlocks++ == 0) {\
      record_lock_rank_held(mutex_->rank); \
    } \
  MACRO_END

/* Clear lock on mutex */
#define clear_mutex_lock(m_) \
  MACRO_START \
    multi_mutex_t* mutex_ = (m_); \
    HQASSERT(mutex_->nlocks > 0, "mutex lock count already zero when clearing lock"); \
    if (--mutex_->nlocks == 0) { \
      clear_lock_rank_held(mutex_->rank); \
    } \
  MACRO_END

void check_deadlock_assertions(lock_rank_t rank)
{
  lock_bits_t locks_held = GET_LOCKS_HELD();

  HQASSERT(VALID_LOCK_RANK(rank), "Invalid rank");
  /* Is the lock allowed with any of the locks already held? */
  HQASSERT((locks_held & lock_rank[rank].partial_order) == 0,
           "Lock violates partial order constraint");
}

Bool assert_test_no_locks_held(void)
{
  return (GET_LOCKS_HELD() == 0);
}

#else /* !ASSERT_BUILD */

#define setup_mutex_lock(m, r)  EMPTY_STATEMENT()
#define record_mutex_lock(m)    EMPTY_STATEMENT()
#define clear_mutex_lock(m)     EMPTY_STATEMENT()

#endif /* !ASSERT_BUILD */


/**** Spinlocks ****/

void multi_counter_spinlock(hq_atomic_counter_t *pointer,
                            unsigned int spincount, lock_rank_t rank)
{
  UNUSED_PARAM(lock_rank_t, rank) ;
  check_deadlock_assertions(rank) ;
  spinlock_counter(pointer, spincount) ;
  record_lock_rank_held(rank);
}

void multi_counter_spinunlock(hq_atomic_counter_t *pointer, lock_rank_t rank)
{
  UNUSED_PARAM(lock_rank_t, rank) ;
  clear_lock_rank_held(rank);
  spinunlock_counter(pointer) ;
}

/**** Mutexes ****/

void multi_mutex_init(multi_mutex_t *mutex, lock_rank_t rank, Bool recursive,
                      int wait_trace, int hold_trace)
{
  pthread_mutexattr_t attr;
  int res;

  HQASSERT(VALID_LOCK_RANK(rank), "Invalid rank");
  res = pthread_mutexattr_init(&attr);
  HQASSERT(res == 0, "mutexattr_init failed");
  if (recursive) {
    HQASSERT((lock_rank[rank].partial_order & (1 << rank)) == 0,
             "Recursive mutex should not have self in lock rank table") ;
    res = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    HQASSERT(res == 0, "mutexattr_settype failed");
  } else {
    HQASSERT((lock_rank[rank].partial_order & (1 << rank)) != 0,
             "Non-recursive mutex should have self in lock rank table") ;
#ifdef ASSERT_BUILD
    res = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    HQASSERT(res == 0, "mutexattr_settype failed");
#endif
  }
  res = pthread_mutex_init(&mutex->pmutex, &attr);
  HQASSERT(res == 0, "mutex init failed");
  setup_mutex_lock(mutex, rank);
  UNUSED_PARAM(lock_rank_t, rank);
#ifdef PROBE_BUILD
  mutex->wait_trace = wait_trace ;
  mutex->hold_trace = hold_trace ;
#else
  UNUSED_PARAM(int, wait_trace);
  UNUSED_PARAM(int, hold_trace);
#endif
}


void multi_mutex_finish(multi_mutex_t *mutex)
{
  int res;

  res = pthread_mutex_destroy(&mutex->pmutex);
  HQASSERT(res == 0, "mutex destroy failed");
}


void multi_mutex_lock(multi_mutex_t *mutex)
{
  int res;

  multi_probe_begin(mutex, wait_trace) ;
  check_deadlock_assertions(mutex->rank);
  res = pthread_mutex_lock(&mutex->pmutex);
  HQASSERT(res == 0, "mutex lock failed");
  record_mutex_lock(mutex);
  multi_probe_end(mutex, wait_trace) ;
  multi_probe_begin(mutex, hold_trace) ;
}


Bool multi_mutex_trylock(multi_mutex_t *mutex)
{
  int res;

  multi_probe_begin(mutex, wait_trace) ;
  res = pthread_mutex_trylock(&mutex->pmutex);
  HQASSERT(res == 0 || res == EBUSY || res == EDEADLK, "mutex trylock failed");
  multi_probe_end(mutex, wait_trace) ;
  if ( res == 0 ) {
    record_mutex_lock(mutex);
    multi_probe_begin(mutex, hold_trace) ;
  }
  return (res == 0) ;
}


void multi_mutex_unlock(multi_mutex_t *mutex)
{
  int res;

  multi_probe_end(mutex, hold_trace) ;
  clear_mutex_lock(mutex);
  res = pthread_mutex_unlock(&mutex->pmutex);
  HQASSERT(res == 0, "mutex unlock failed");
}

#ifdef ASSERT_BUILD
Bool multi_mutex_is_locked(multi_mutex_t *mutex)
{
  /* Use explicit pthread calls to avoid tracing, since this function is only
     used for assertions. We also won't bother with the deadlock
     assertions. */
  int res = pthread_mutex_trylock(&mutex->pmutex);
  if (res == 0) {
    res = pthread_mutex_unlock(&mutex->pmutex);
    HQASSERT(res == 0, "mutex unlock failed");
    return FALSE ; /* it wasn't locked beforehand */
  } else {
    HQASSERT(res == EBUSY || res == EDEADLK, "mutex trylock failed");
    return TRUE ; /* it was locked beforehand */
  }
}
#endif

/**** Condition variables ****/

#ifdef ASSERT_BUILD
static Bool is_mutex_locked(pthread_mutex_t *mutex)
{
  if (pthread_mutex_trylock(mutex) == 0) {
    pthread_mutex_unlock(mutex);
    return FALSE;
  } else
    return TRUE;
}
#endif


void multi_condvar_init(multi_condvar_t *cond, multi_mutex_t *mutex,
                        int wait_trace)
{
  int res;

  HQASSERT(cond != NULL, "No condition variable to initialise");
  HQASSERT(mutex != NULL, "No mutex for condvar");
  res = pthread_cond_init(&cond->pcond, NULL);
  HQASSERT(res == 0, "condvar init failed");
  cond->refcount = 1 ;
  cond->mutex = mutex;
#ifdef PROBE_BUILD
  cond->wait_trace = wait_trace ;
#else
  UNUSED_PARAM(int, wait_trace);
#endif
}

void multi_condvar_finish(multi_condvar_t *cond)
{
  int res;

  HQASSERT(cond != NULL, "condvar init failed");
  HQASSERT(cond->refcount == 1, "Condition variable refcount wrong");
  HQASSERT(mm_pool_task == NULL || !mm_pool_check(mm_pool_task, cond),
           "Using wrong condvar finish routine for MM allocation") ;
  res = pthread_cond_destroy(&cond->pcond);
  HQASSERT(res == 0, "condvar destroy failed");
  UNUSED_PARAM(int, res) ;
}

multi_condvar_t *multi_condvar_create(multi_mutex_t *mutex, int wait_trace)
{
  multi_condvar_t *cond ;

  HQASSERT(mm_pool_task != NULL, "No pool for condvars");
  HQASSERT(mutex != NULL, "No mutex for condvar");

  /* It's a shame we can't use a SAC alloc here, because we could probably
     pre-allocate enough conditions so that we are never likely to run into
     problems at critical points. We can't use a SAC allocate because they're
     not synchronised, so we'd need to take a lock around the alloc. We'll
     just have to rely on the task pool being fairly lightly loaded, so there
     will usually be plenty of room in the default segment size allocation.
     The task pool also shouldn't suffer too badly from fragmentation,
     because it's only got a few object allocation sizes, and most of those
     use SACs anyway. */
  if ( (cond = mm_alloc(mm_pool_task, sizeof(multi_condvar_t),
                        MM_ALLOC_CLASS_CONDITION)) != NULL ) {
    if ( pthread_cond_init(&cond->pcond, NULL) != 0 ) {
      mm_free(mm_pool_task, cond, sizeof(multi_condvar_t)) ;
      return NULL ;
    }
    cond->refcount = 1 ;
    cond->mutex = mutex;
#ifdef PROBE_BUILD
    cond->wait_trace = wait_trace ;
#else
    UNUSED_PARAM(int, wait_trace);
#endif
  }

  return cond ;
}

multi_condvar_t *multi_condvar_acquire(multi_condvar_t *cond)
{
  HQASSERT(cond != NULL, "No condition variable to acquire");
  HQASSERT(cond->mutex != NULL, "No condition variable mutex") ;
  HQASSERT(is_mutex_locked(&cond->mutex->pmutex),
           "Condvar mutex not locked while acquiring reference");
  HQASSERT(cond->refcount > 0, "Condition variable was previously released") ;
  ++cond->refcount ;

  return cond ;
}

void multi_condvar_release(multi_condvar_t **condptr)
{
  multi_condvar_t *cond ;

  HQASSERT(condptr != NULL, "Nowhere to find condition variable");
  cond = *condptr ;
  HQASSERT(cond != NULL, "No condition variable to release");
  HQASSERT(cond->mutex != NULL, "No condition variable mutex") ;
  HQASSERT(is_mutex_locked(&cond->mutex->pmutex),
           "Condvar mutex not locked while releasing reference");

  HQASSERT(cond->refcount > 0, "Condition variable was previously released") ;
  if ( --cond->refcount == 0 ) {
    int res = pthread_cond_destroy(&cond->pcond);
    HQASSERT(res == 0, "condvar destroy failed");
    UNUSED_PARAM(int, res) ;
    mm_free(mm_pool_task, cond, sizeof(multi_condvar_t)) ;
  }

  *condptr = NULL ;
}

void multi_condvar_wait_raw(multi_condvar_t *cond)
{
  int res;
  multi_mutex_t *mutex;

  (void)multi_condvar_acquire(cond) ;
  mutex = cond->mutex;
  HQASSERT(mutex != NULL, "No condition variable mutex") ;
  multi_probe_begin(cond, wait_trace) ;
  multi_probe_end(mutex, hold_trace) ;
  /* No deadlock check, because the locks are the same before and after */
  HQASSERT(mutex->nlocks == 1, "condvar wait on lock with recursively held lock");
  clear_mutex_lock(mutex);
  res = pthread_cond_wait(&cond->pcond, &mutex->pmutex);
  HQASSERT(res == 0, "condvar wait failed");
  record_mutex_lock(mutex);
  multi_probe_begin(mutex, hold_trace) ;
  multi_probe_end(cond, wait_trace) ;
  multi_condvar_release(&cond) ;
}

void multi_condvar_wait(multi_condvar_t *cond)
{
  multi_mutex_t *mutex;

  mutex = cond->mutex;
  (void)multi_condvar_acquire(cond) ;
  multi_probe_begin(cond, wait_trace) ;
  multi_probe_end(mutex, hold_trace) ;
  /* No deadlock check, because the locks are the same before and after */
  HQASSERT(mutex->nlocks == 1, "condvar wait on lock with recursively held lock");
  clear_mutex_lock(mutex);
  multi_condvar_wait_task(cond) ;
  record_mutex_lock(mutex);
  multi_probe_begin(mutex, hold_trace) ;
  multi_probe_end(cond, wait_trace) ;
  multi_condvar_release(&cond) ;
}

Bool multi_condvar_timedwait(multi_condvar_t *cond, HqU32x2 abstimeus)
{
  int res;
  struct timespec timeout ;
  multi_mutex_t *mutex;

  (void)multi_condvar_acquire(cond) ;
  mutex = cond->mutex;
  HQASSERT(mutex != NULL, "No condition variable mutex") ;
  HQASSERT(is_mutex_locked(&mutex->pmutex), "condvar wait with mutex unlocked");
  multi_probe_begin(cond, wait_trace) ;
  /* Convert an HqU32x2 number of microseconds into a seconds and nanoseconds
     pair. */
  {
#if defined(HQN_INT64)
    uint64 usecs = ((uint64)abstimeus.high << 32) + abstimeus.low ;
    timeout.tv_sec = (time_t)(usecs / 1000000) ;
    timeout.tv_nsec = (long)(usecs - (uint64)timeout.tv_sec * 1000000) * 1000 ;
#else
    /* A double doesn't have enough precision to calculate the microsecond to
       nanosecond conversion without significant loss. The 32x2 time can be
       decomposed as:

       M  = 1000000
       t = (floor(high/M)*M + high%M) * 2^32 + floor(low/M)*M + low%M

       Dividing by M to get the whole number of seconds, we get:

       s = (floor(high/M) + (high%M)/M) * 2^32 + floor(low/M) + (low%M)/M

       The first term (floor(high/M)*2^32) is not representable in a 32-bit
       integer, and would indicate a time past 2038, when the 32-bit Unix
       epoch runs out, so we just assert it won't happen and drop the term.
       By then, we should have 64-bit int support even on embedded platforms,
       and with any luck I'll have retired.

       The final term contributes fractional bits that may affect the rounding
       of the seconds.

       The second term can be calculated using a reciprocal multiplication by
       0x431bde82d7b634db and shifting right by 18. Or we can use a double
       for the terms independently, and hope it doesn't lose too much (they
       shouldn't, because the multiplication by BIGGEST_INTEGER will just
       affect the exponent). BIGGEST_INTEGER is 2^31, which is why we divide
       by 500000 rather than one million. */
    HqU32x2 tv_usec ;
    uint32 test ;

    HQASSERT(abstimeus.high / 1000000 == 0,
             "Can't convert microseconds to timespec") ;
    timeout.tv_sec = (time_t)(BIGGEST_INTEGER * abstimeus.high / 500000.0
                              + abstimeus.low / 1000000.0) ;

    /* Take tv_sec * 1000000 off abstimeus using long multiplication. */
    HqU32x2FromUint32(&tv_usec, (uint32)timeout.tv_sec) ;
    for ( test = 1000000 ; test != 0 ; test >>= 1 ) {
      if ( (test & 1) != 0 )
        HqU32x2Subtract(&abstimeus, &abstimeus, &tv_usec) ;
      HqU32x2Add(&tv_usec, &tv_usec, &tv_usec) ;
    }

    HQASSERT(abstimeus.high == 0 && abstimeus.low < 1000000,
             "Long multiplication failed") ;

    timeout.tv_nsec = (long)abstimeus.low * 1000 ;
#endif
  }
  multi_probe_end(mutex, hold_trace) ;
  /* No deadlock check, because the locks are the same before and after */
  HQASSERT(mutex->nlocks == 1, "condvar wait on lock with recursively held lock");
  clear_mutex_lock(mutex);
  res = pthread_cond_timedwait(&cond->pcond, &mutex->pmutex, &timeout);
  HQASSERT(res == 0 || res == ETIMEDOUT, "condvar timed wait failed");
  record_mutex_lock(mutex);
  multi_probe_begin(mutex, hold_trace) ;
  multi_probe_end(cond, wait_trace) ;
  multi_condvar_release(&cond) ;

  return res == ETIMEDOUT ;
}


void multi_condvar_signal(multi_condvar_t *cond)
{
  int res;

  HQASSERT(is_mutex_locked(&cond->mutex->pmutex),
           "condvar signal with mutex unlocked");
  res = pthread_cond_signal(&cond->pcond);
  HQASSERT(res == 0, "condvar signal failed");
}


void multi_condvar_broadcast(multi_condvar_t *cond)
{
  int res;

  HQASSERT(is_mutex_locked(&cond->mutex->pmutex),
           "condvar broadcast with mutex unlocked");
  res = pthread_cond_broadcast(&cond->pcond);
  HQASSERT(res == 0, "condvar broadcast failed");
}


/**** One-item-per-thread read-write locks ****/

/* Architecture: A slot is reserved from items[] for any item being
 * locked, shared by all threads.  thread_locks[] maps from thread index
 * to items[] index locked by the thread, if any.  NB: It is crucial to
 * always reuse any pre-existing slot for the item, lest any waiters
 * about to wake up find their item moved away!  There can be at most
 * one slot for any item, because of this reuse. */


/* RWLOCK_UNLOCKED -- used in thread_locks[] to indicate unlocked */
#define RWLOCK_UNLOCKED NTHREADS_LIMIT


#ifdef ASSERT_BUILD

void rwlock_check_consistency(multi_rwlock_t *lock)
{
  unsigned int j; size_t i;
  unsigned int lockers = 0, waiters = 0, locks = 0;
  unsigned int locks_each_item[NTHREADS_LIMIT];

  for (j = 0; j < NTHREADS_LIMIT; ++j) {
    locks_each_item[j] = 0;
    if (lock->thread_locks[j] != RWLOCK_UNLOCKED) {
      ++lockers;
      HQASSERT(lock->items[lock->thread_locks[j]].state != 0,
               "thread_locks and items state disagree");
    }
  }
  for (i = 0; i < NTHREADS_LIMIT; ++i) {

    waiters += lock->items[i].waiters;
    locks += (unsigned)abs(lock->items[i].state);
    if ( lock->thread_locks[i] != RWLOCK_UNLOCKED )
      ++locks_each_item[lock->thread_locks[i]];
  }
  for (j = 0; j < NTHREADS_LIMIT; ++j)
    HQASSERT(locks_each_item[j] == (unsigned)abs(lock->items[j].state),
             "thread_locks and item lock count mismatch");
  HQASSERT(lockers + waiters <= NTHREADS_LIMIT, "too many claims");
  HQASSERT(lockers == locks, "lockers and locks don't match");
}

#else

#define rwlock_check_consistency(lock) EMPTY_STATEMENT()

#endif


void multi_rwlock_init(multi_rwlock_t *lock, lock_rank_t rank,
                       int wait_trace, int read_trace, int write_trace)
{
  int res;
  unsigned int j; size_t i;

  HQASSERT(VALID_LOCK_RANK(rank), "Invalid rank");
  res = pthread_mutex_init(&lock->pmutex, NULL);
  HQASSERT(res == 0, "rwlock mutex init failed");
  for (i = 0; i < NTHREADS_LIMIT; ++i) {
    res = pthread_cond_init(&lock->items[i].wcond, NULL);
    HQASSERT(res == 0, "rwlock condvar init failed");
    res = pthread_cond_init(&lock->items[i].rcond, NULL);
    HQASSERT(res == 0, "rwlock condvar init failed");
    lock->items[i].state = 0; lock->items[i].waiters = 0;
    lock->items[i].item = NULL;
  }
  for (j = 0; j < NTHREADS_LIMIT; ++j)
    lock->thread_locks[j] = RWLOCK_UNLOCKED;
  lock->rank = rank;
  lock->waiters = 0;
#ifdef PROBE_BUILD
  lock->wait_trace = wait_trace ;
  lock->read_trace = read_trace ;
  lock->write_trace = write_trace ;
#else
  UNUSED_PARAM(int, wait_trace);
  UNUSED_PARAM(int, read_trace);
  UNUSED_PARAM(int, write_trace);
#endif
}


void multi_rwlock_finish(multi_rwlock_t *lock)
{
  int res;
  size_t i;

  for (i = 0; i < NTHREADS_LIMIT; ++i) {
    HQASSERT(lock->items[i].state == 0, "destroying locked lock");
    HQASSERT(lock->items[i].waiters == 0, "destroying requested lock");
    res = pthread_cond_destroy(&lock->items[i].wcond);
    HQASSERT(res == 0, "rwlock condvar destroy failed");
    res = pthread_cond_destroy(&lock->items[i].rcond);
    HQASSERT(res == 0, "rwlock condvar destroy failed");
  }
  res = pthread_mutex_destroy(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex destroy failed");
}


void multi_rwlock_lock(multi_rwlock_t *lock, void *item, int mode)
{
  int res;
  thread_id_t my_tid = SwThreadIndex();
  size_t i;

  multi_probe_begin(lock, wait_trace) ;
  HQASSERT(mode == MULTI_RWLOCK_WRITE || mode == MULTI_RWLOCK_READ, "bogus mode");
  if ( lock->thread_locks[my_tid] != RWLOCK_UNLOCKED ) {
    HQFAIL("locking when already locked");
    return; /* recover after assert, because otherwise it hangs */
  }
  check_deadlock_assertions(lock->rank);

  res = pthread_mutex_lock(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex lock failed");
  rwlock_check_consistency(lock);

  /* Find any pre-existing slot for this item. */
  for (i = 0; i < NTHREADS_LIMIT; ++i)
    if (lock->items[i].item == item)
      goto got_slot;
  /* Find a free slot and lock it. */
  for (i = 0; i < NTHREADS_LIMIT; ++i)
    if (lock->items[i].state == 0 && lock->items[i].waiters == 0)
      break;
  HQASSERT(i < NTHREADS_LIMIT, "rwlock failed to find a free slot");
  lock->items[i].item = item;
  goto lock_it;

 got_slot:
  /* Wait until unlocked or, for READ only, read-locked. */
  /** \todo ajcd 2011-02-15: How should this interact with a task cancel? The
      cancel would like to break it out of the cond wait, but that would mean
      that the lock acquisition fails. That's not part of the contract of
      the multi_rwlock interface. */
  while (mode == MULTI_RWLOCK_WRITE ? (lock->items[i].state != 0)
                                    : (lock->items[i].state == -1)) {
    ++lock->waiters;
    ++lock->items[i].waiters;
    res = pthread_cond_wait(mode == MULTI_RWLOCK_WRITE ? &lock->items[i].wcond
                                                       : &lock->items[i].rcond,
                            &lock->pmutex);
    HQASSERT(res == 0, "rwlock condvar wait failed");
    --lock->items[i].waiters;
    --lock->waiters;
  }
 lock_it:
  lock->items[i].state =
    (mode == MULTI_RWLOCK_WRITE) ? -1 : lock->items[i].state + 1;
  lock->thread_locks[my_tid] = i;
  res = pthread_mutex_unlock(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex unlock failed");
  multi_probe_end(lock, wait_trace) ;
  if ( mode == MULTI_RWLOCK_WRITE )
    multi_probe_begin(lock, write_trace) ;
  else
    multi_probe_begin(lock, read_trace) ;
}


void multi_rwlock_wr_to_rd(multi_rwlock_t *lock, void *item)
{
  int res;
  thread_id_t my_tid = SwThreadIndex();
  size_t i = lock->thread_locks[my_tid];

  UNUSED_PARAM(void*, item);

  if ( !(i != RWLOCK_UNLOCKED
         && lock->items[i].state == -1 && lock->items[i].item == item ) ) {
    HQFAIL("not holding a write lock in multi_rwlock_wr_to_rd");
    return; /* recover after assert, because otherwise it hangs */
  }
  multi_probe_begin(lock, wait_trace) ;
  multi_probe_end(lock, write_trace) ;

  /* NB: Must lock to ensure waiters don't miss the broadcast. */
  res = pthread_mutex_lock(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex lock failed");
  rwlock_check_consistency(lock);

  lock->items[i].state = 1; /* change to read-lock */
  res = pthread_cond_broadcast(&lock->items[i].rcond);
  HQASSERT(res == 0, "rwlock condvar broadcast failed");

  res = pthread_mutex_unlock(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex unlock failed");
  multi_probe_end(lock, wait_trace) ;
  multi_probe_begin(lock, read_trace) ;
}


void multi_rwlock_unlock(multi_rwlock_t *lock)
{
  int res;
  thread_id_t my_tid = SwThreadIndex();
  size_t i = lock->thread_locks[my_tid];
  Bool writer ;

  if ( i == RWLOCK_UNLOCKED ) {
    HQFAIL("unlocking but not holding the lock");
    return; /* recover after assert, because otherwise it hangs */
  }
  HQASSERT(VALID_LOCK_RANK(lock->rank), "Invalid rank");
  res = pthread_mutex_lock(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex lock failed");
  rwlock_check_consistency(lock);

  HQASSERT(lock->items[i].state != 0, "item was not locked");
  lock->thread_locks[my_tid] = RWLOCK_UNLOCKED;
  if (lock->items[i].state == -1) {
    lock->items[i].state = 0;
    res = pthread_cond_broadcast(&lock->items[i].rcond);
    HQASSERT(res == 0, "rwlock condvar broadcast failed");
    writer = TRUE ;
  } else {
    --lock->items[i].state;
    writer = FALSE ;
  }
  if (lock->items[i].state == 0) {
    res = pthread_cond_signal(&lock->items[i].wcond);
    HQASSERT(res == 0, "rwlock condvar signal failed");
  }

  res = pthread_mutex_unlock(&lock->pmutex);
  HQASSERT(res == 0, "rwlock mutex unlock failed");

  if ( writer )
    multi_probe_end(lock, write_trace) ;
  else
    multi_probe_end(lock, read_trace) ;
}


Bool multi_rwlock_unlock_if_wanted(multi_rwlock_t *lock)
{
  if ( lock->waiters == 0 )
    return FALSE ;

  multi_rwlock_unlock(lock) ;

  return TRUE ;
}


/* multi_rwlock_check -- check if the current thread has a lock on item
 *
 * Note that this returns TRUE if asked about a read lock and it's write-locked.
 */

Bool multi_rwlock_check(multi_rwlock_t *lock, void *item, int mode)
{
  size_t i = lock->thread_locks[SwThreadIndex()];

  /* Can't call rwlock_check_consistency() without taking the mutex. */
  return (i != RWLOCK_UNLOCKED && lock->items[i].item == item
          && (mode == MULTI_RWLOCK_WRITE ? (lock->items[i].state == -1) : TRUE));
}


/*
 * Slight platform dependency; Performance of thread-local storage is
 * better with the Windows native API.  Functions below are just
 * indirections so the windows header isn't visible to clients of
 * mlock.h.  Note that the pthread library uses opaque pointers as key
 * values, so the interface for the TLS could be combined to work on
 * single definition of TLS key.
 */
#ifdef WIN32

#include "hqwindows.h"

int NativeTlsAlloc(unsigned long *var)
{
  *var = TlsAlloc();
  return (*var != TLS_OUT_OF_INDEXES);
}

int NativeTlsFree(unsigned long var)
{
  return TlsFree(var) != 0;
}

int NativeTlsSetValue(unsigned long var, void * val)
{
  return TlsSetValue(var, val) != 0;
}

void * NativeTlsGetValue(unsigned long var)
{
  return TlsGetValue(var);
}

#endif  /* WIN32 */

#if 0 /* defined(ASSERT_BUILD) */
/** \todo ajcd 2011-09-18: This isn't right yet, and I don't have time to
    fix it now. I'd like to assert that the lock table is well-formed, and
    isn't missing restrictions. However, it's a partial order table, this
    function is checking the wrong thing. */
#include <stdio.h> /* sprintf */

Bool lock_cycle_check(lock_rank_t rank, lock_bits_t checked, char *message)
{
  lock_rank_t i ;

  /* If we've examined all of the locks, there's no more to do. */
  if ( checked == (1 << LOCK_RANK_LIMIT) - 1 )
    return TRUE ;

  if ( checked == 0 ) {
    sprintf(message, "Lock rank table cycle detected: %d", rank) ;
    message += strlen(message) ;
    checked = 1 << rank ;
  }

  for ( i = 0 ; i < LOCK_RANK_LIMIT ; ++i ) {
    lock_bits_t notafter = lock_rank[i].partial_order ;
    if ( (notafter & (1 << i)) == 0 ) {
      sprintf(message, "->%d", i) ;
      if ( i == rank ) { /* Found the lock we're checking. */
        if ( checked != 1 << rank ) /* Trivial recursion is OK for checking. */
          return FALSE ;
      } else if ( (checked & (1 << i)) == 0 &&
                  !lock_cycle_check(rank, checked | (1 << i),
                                    message + strlen(message)) ) {
        return FALSE ;
      }
    }
  }

  return TRUE ;
}
#endif

#ifdef ASSERT_BUILD
static Bool mlock_swinit(
  SWSTART* params)
{
  int res;
  int i;

  UNUSED_PARAM(SWSTART*, params);

  if (pthread_key_create(&locks_held_per_thread_key, NULL) != 0) {
    return (FALSE);
  }

  for (i = 0; i < LOCK_RANK_LIMIT; i++) {
    if (lock_rank[i].concurrent) {
      if (pthread_key_create(&lock_rank[i].nlocks, NULL) != 0) {
        res = pthread_key_delete(locks_held_per_thread_key);
        while (--i >= 0 ) {
          if (lock_rank[i].concurrent) {
            res = pthread_key_delete(lock_rank[i].nlocks);
          }
        }
        return (FALSE);
      }
    }
  }

  return (TRUE);
}

static void mlock_finish(void)
{
  int res;
  int i;

  res = pthread_key_delete(locks_held_per_thread_key);
  UNUSED_PARAM(int, res);
  HQASSERT(res == 0, "locks_held_per_thread_key delete failed");

  for (i = 0; i < LOCK_RANK_LIMIT; i++) {
    if (lock_rank[i].concurrent) {
      res = pthread_key_delete(lock_rank[i].nlocks);
      HQASSERT(res == 0, "failed to delete per thread rank nlocks counter");
    }
  }
}
#endif /* ASSERT_BUILD */

void mlock_C_globals(
  core_init_fns* fns)
{
#ifdef ASSERT_BUILD
  int i;

  HQASSERT(LOCK_RANK_LIMIT < sizeof(lock_bits_t)*CHAR_BIT,
           "mlock_C_globals: more lock ranks than tracking bits");

  locks_held_per_thread_key = 0;
  for (i = 0; i < LOCK_RANK_LIMIT; i++) {
    lock_rank[i].nlocks = 0;
  }

  fns->swinit = mlock_swinit;
  fns->finish = mlock_finish;
#else /* !ASSERT_BUILD */
  UNUSED_PARAM(core_init_fns*, fns);

#endif /* !ASSERT_BUILD */
}


/* Log stripped */
