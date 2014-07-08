/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hqspin.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Spinlocks implemented using operations.
 *
 * These spinlocks are implemented in a separate file because they include
 * platform-specific header files to yield the processor in the event of a
 * contended spinlock
 */

#ifndef __HQSPIN_H__
#define __HQSPIN_H__

#include "platform.h"
#include "hqatomic.h"

/** \def yield_processor
 * \brief Yield the processor to another thread, if there is one runnable.
 */
#if defined(WIN32)
#  include "hqwindows.h"
#  define yield_processor() (void)SwitchToThread()
#elif defined(MACINTOSH)
#  include <sched.h>
#  define yield_processor() (void)sched_yield()
#elif defined(linux)
#  include <sched.h>
#  define yield_processor() (void)sched_yield()
#else
#  define yield_processor() EMPTY_STATEMENT()
#endif

/** \brief Constants for yields per spinlock cycle. */
enum {
  HQSPIN_YIELD_NEVER = 0,  /**< Practically never yield processor. */
  HQSPIN_YIELD_ALWAYS = 1  /**< Yield processor every spinlock cycle. */
} ;

/** \brief Aliased void pointer, to quieten GCC warnings and prevent harmful
    optimisations. */
typedef MAY_ALIAS(void *) spinlock_void_ptr ;

/** \brief Lock a pointer using atomic operations.
 *
 * \param[in,out] addr_  The address of the pointer to lock.
 * \param[out] locked_   The pointer value to dereference in the locked
 *                       section.
 * \param[in] count_     Number of spin cycles before each processor yield.
 *
 * \c spinlock_pointer uses the lowest bit as lock mark, so is only valid for
 * halfword or greater aligned pointers. It modifies the stored value of the
 * pointer in memory, and loads a dereferencable version of the pointer into
 * a variable. Within the locked section, code must use the dereferencable
 * version of the pointer, and not re-load the original pointer from memory.
 */
#define spinlock_pointer(addr_, locked_, count_) MACRO_START            \
  spinlock_void_ptr *_addr_ = (spinlock_void_ptr *)(addr_) ;            \
  void *_result_, *_swapfor_ ;                                          \
  HQASSERT(sizeof(**(addr_)) > 1 && sizeof(**(addr_)) == sizeof(*(locked_)), \
           "Spinlock pointer type alignment invalid") ;                 \
  for (;;) {                                                            \
    unsigned int _count_ = (count_) ;                                   \
    HqBool _didcas_ ;                                                   \
    do {                                                                \
      _result_ = (void *)((intptr_t)*(_addr_) & ~(intptr_t)1) ;         \
      _swapfor_ = (void *)((intptr_t)_result_ | 1) ;                    \
      HqAtomicCASPointer(_addr_, _result_, _swapfor_, _didcas_, spinlock_void_ptr) ; \
    } while ( !_didcas_ && --_count_ != 0 ) ;                           \
    if ( _didcas_ )                                                     \
      break ;                                                           \
    yield_processor() ;                                                 \
  }                                                                     \
  locked_ = _result_ ;                                                  \
MACRO_END


/** \brief Try to lock a pointer using atomic operations.
 *
 * \param[in,out] addr_  The address of the pointer to lock.
 * \param[out] locked_   The pointer value to dereference in the locked
 *                       section, only set if \c didlock_ is \c TRUE.
 * \param[out] didlock_  A Boolean to indicate if managed to lock.
 *
 * This is like \c spinlock_pointer, except that it doesn't spin if it can't get
 * the lock, it just returns.
 */
#define spintrylock_pointer(addr_, locked_, didlock_) MACRO_START       \
  spinlock_void_ptr *_addr_ = (spinlock_void_ptr *)(addr_) ;            \
  void *_result_, *_swapfor_ ;                                          \
  HQASSERT(sizeof(**(addr_)) > 1 && sizeof(**(addr_)) == sizeof(*(locked_)), \
           "Spinlock pointer type alignment invalid") ;                 \
  _result_ = (void *)((intptr_t)*(_addr_) & ~(intptr_t)1) ;             \
  _swapfor_ = (void *)((intptr_t)_result_ | 1) ;                        \
  HqAtomicCASPointer(_addr_, _result_, _swapfor_, didlock_, spinlock_void_ptr) ; \
  if ( didlock_ )                                                       \
    locked_ = _result_;                                                 \
MACRO_END


/** \brief Unlock a pointer using atomic operations.
 *
 * \param[out] addr_     The address of the pointer to unlock.
 * \param[in] unlocked_  The new value of the unlocked pointer.
 *
 * \c spinunlock_pointer unlocks a pointer that was locked using \c
 * spinlock_pointer. The lowest bit of the pointer is used as lock mark, so
 * this is only valid for halfword or greater aligned pointers. The new value
 * of the unlocked pointer will usually be the dereferencable pointer value
 * saved by \c spinlock_pointer, but it may also be a different pointer of
 * the correct type. This can be used to safely replace objects, by locking
 * the only pointer reference to the object, but unlocking with NULL or a
 * different object pointer.
 */
#define spinunlock_pointer(addr_, unlocked_) MACRO_START                \
  Bool _didcas_ ;                                                       \
  HQASSERT(sizeof(**(addr_)) > 1, "Spinlock pointer type alignment invalid") ; \
  HQASSERT(((intptr_t)*(addr_) & 1) != 0, "Pointer is not currently locked") ; \
  /* Need a release barrier to prevent compiler optimising over the unlock. */ \
  HqAtomicCASPointer(addr_, *(addr_), unlocked_, _didcas_, spinlock_void_ptr) ; \
  HQASSERT(_didcas_, "Failed to unlock spinlock") ;                     \
MACRO_END

/** \brief Lock a counter semaphore using atomic operations.
 *
 * \param[in,out] addr_  The address of the counter to lock.
 * \param[in] count_     Number of spin cycles before each processor yield.
 *
 * Semaphore counters are locked when non-zero, unlocked when zero.
 * \c spinlock_counter will wait until a counter is zero, and lock it using
 * the value 1. The semaphore counter may be test-locked by using an atomic
 * increment, testing if the value before increment was zero, and decrementing
 * the semaphore to release it.
 */
#define spinlock_counter(addr_, count_) MACRO_START                     \
  for (;;) {                                                            \
    unsigned int _count_ = (count_) ;                                   \
    HqBool _didcas_ ;                                                   \
    do {                                                                \
      HqAtomicCAS((addr_), 0, 1, _didcas_) ;                            \
    } while ( !_didcas_ && --_count_ != 0 ) ;                           \
    if ( _didcas_ )                                                     \
      break ;                                                           \
    yield_processor() ;                                                 \
  }                                                                     \
MACRO_END

/** \brief Unlock a semaphore counter using atomic operations.
 *
 * \param[out] addr_     The address of the counter to unlock.
 */
#define spinunlock_counter(addr_) MACRO_START                           \
  hq_atomic_counter_t _after_ ;                                         \
  HqAtomicDecrement((addr_), _after_) ;                                 \
  HQASSERT(_after_ >= 0, "Counter semaphore was not locked") ;          \
MACRO_END

#endif /* __HQSPIN_H__ */

