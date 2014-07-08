/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hqatomic.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Macros implementing atomic operations.
 */

#ifndef __HQATOMIC_H__
#define __HQATOMIC_H__

#include "platform.h"
#include "hqtypes.h"
#include <signal.h>

/** \page hqatomic Atomic operations for multi-thread variable access.

   We provide a set of minimal atomic operations, which we can support
   effectively across multiple compilers and processor architectures, using
   either compiler intrinsics or in-line assembly. The minimal set of
   operations provided are:

     HqAtomicIncrement()  - Increment a signed integral value.
     HqAtomicDecrement()  - Decrement a signed integral value.
     HqAtomicCAS()        - Compare and swap two signed integral values.
     HqAtomicCASPointer() - Compare and swap two pointers.

   In principle, all other atomic operations can be built on top of CAS
   (compare and swap). However, it's just a bit too clunky having to
   implement some simple operations on top of CAS.

   Major compiler versions that support atomic operations are defined first.
   If the define HQ_ATOMIC_SUPPORTED is not defined, then the architecture
   specific assembly versions may be used. The equivalent prototypes for the
   operations implemented are:

   \code
   typedef ... hq_atomic_counter_t ;
   \endcode

   An integer type suitable for the atomic increment and decrement.

   \code
   HqAtomicIncrement(hq_atomic_counter_t *ptr, hq_atomic_counter_t& before) ;
   \endcode

   Atomically increment the contents of \c *ptr, and return the value before
   it was incremented in \c before.

   \code
   HqAtomicDecrement(hq_atomic_counter_t *ptr, hq_atomic_counter_t& after) ;
   \endcode

   Atomically decrement the contents of \c *ptr, and return the value after
   it was decremented in \c after.

   \code
   HqAtomicCAS(hq_atomic_counter_t *ptr, hq_atomic_counter_t compare,
               hq_atomic_counter_t swap, HqBool& swapped) ;
   \endcode

   Atomically compare the value of \c *ptr with \c compare, and if they
   match, swap the contents of \c *ptr for \c swap. Store a boolean in
   \c swapped indicating if the swap was performed.

   \code
   HqAtomicCASPointer(type **ptr, type *compare, type *swap,
                      HqBool& swapped, type) ;
   \endcode

   Atomically compare the value of \c *ptr with \c compare, and if they
   match, swap the contents of \c *ptr for \c swap. Store a boolean in
   \c swapped indicating if the swap was performed. (This is the
   same operation as \c HqAtomicCAS, but allows pointers to be compared and
   swapped.)

 */


/****************************************************************************/

#if defined(_MSC_VER)
#include <intrin.h> /* Using intrinsics directly. */

#define HQ_ATOMIC_SUPPORTED 1

typedef long hq_atomic_counter_t ;

#pragma intrinsic (_InterlockedIncrement)
#define HqAtomicIncrement(ptr_, before_) MACRO_START \
  before_ = _InterlockedIncrement(ptr_) - 1 ; \
MACRO_END

#pragma intrinsic (_InterlockedDecrement)
#define HqAtomicDecrement(ptr_, after_) MACRO_START \
  after_ = _InterlockedDecrement(ptr_) ; \
MACRO_END

#pragma intrinsic (_InterlockedCompareExchange)
#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) MACRO_START \
  hq_atomic_counter_t _compareto_ = (compareto_) ; \
  swapped_ = (_compareto_ == _InterlockedCompareExchange((ptr_), (swapfor_), _compareto_)) ; \
MACRO_END

/* InterlockedCompareExchangePointer isn't available on x86, unfortunately. */
#if PLATFORM_IS_32BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) \
  HqAtomicCAS((hq_atomic_counter_t *)(ptr_), \
              (hq_atomic_counter_t)(compareto_), \
              (hq_atomic_counter_t)(swapfor_), swapped_)
#elif PLATFORM_IS_64BIT
#pragma intrinsic (_InterlockedCompareExchangePointer)
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) MACRO_START \
  void *_compareto_ = (compareto_) ;                                    \
  swapped_ = (_compareto_ == _InterlockedCompareExchangePointer((ptr_), (swapfor_), _compareto_)) ; \
MACRO_END
#else
#error Platform is neither 32 nor 64 bits, no pointer CAS implemented
#endif

/****************************************************************************/

#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1))

#define HQ_ATOMIC_SUPPORTED 1

typedef int hq_atomic_counter_t ;

#define HqAtomicIncrement(ptr_, before_) MACRO_START \
  before_ = __sync_fetch_and_add((ptr_), 1) ; \
MACRO_END

#define HqAtomicDecrement(ptr_, after_) MACRO_START \
  after_ = __sync_sub_and_fetch((ptr_), 1) ; \
MACRO_END

#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) MACRO_START \
  swapped_ = __sync_bool_compare_and_swap((ptr_), (compareto_), (swapfor_)) ; \
MACRO_END

#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) MACRO_START \
  swapped_ = __sync_bool_compare_and_swap((ptr_), (compareto_), (swapfor_)) ; \
MACRO_END

#elif NORMAL_OS == P_MACOS

#include <libkern/OSAtomic.h>

#define HQ_ATOMIC_SUPPORTED 1

typedef int32_t hq_atomic_counter_t ;

#define HqAtomicIncrement(ptr_, before_) MACRO_START \
  before_ = OSAtomicIncrement32Barrier(ptr_) - 1 ; \
MACRO_END

#define HqAtomicDecrement(ptr_, after_) MACRO_START \
  after_ = OSAtomicDecrement32Barrier(ptr_) ; \
MACRO_END

#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) MACRO_START \
  swapped_ = OSAtomicCompareAndSwap32Barrier((compareto_), (swapfor_), (ptr_)) ; \
MACRO_END

/* The OSAtomicCompareAndSwapPtr interface only appeared in MacOS X 10.5. I
   haven't found the best way to determine the SDK version, so use the only
   define that I can find in 10.5 that's not in 10.4 to discriminate. */
#if defined(OS_ATOMIC_QUEUE_INIT)
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) \
  swapped_ = OSAtomicCompareAndSwapPtrBarrier((compareto_), (swapfor_), (ptr_)) ; \
MACRO_END
#elif PLATFORM_IS_32BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) MACRO_START \
  swapped_ = OSAtomicCompareAndSwap32Barrier((int32_t)(compareto_), (int32_t)(swapfor_), (int32_t *)(ptr_)) ; \
MACRO_END
#elif PLATFORM_IS_64BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) MACRO_START \
  swapped_ = OSAtomicCompareAndSwap64Barrier((int64_t)(compareto_), (int64_t)(swapfor_), (int64_t *)(ptr_)) ; \
MACRO_END
#else
#error Platform is neither 32 nor 64 bits, no pointer CAS implemented
#endif /* SDK 10.4 */

#endif /* MSVC, GCC >= 4.1.1, MacOS checked now */

/****************************************************************************/

#if !defined(HQ_ATOMIC_SUPPORTED)
/* There were no intrinsics for atomic operations. Roll our own by hand. */

/****************************************************************************/

#if (MACHINE == P_INTEL || (MACHINE == P_POWERMAC && __LITTLE_ENDIAN__)) && defined(__GNUC__) /* x86 gcc < 4.1 */

#define HQ_ATOMIC_SUPPORTED 1

typedef int32 hq_atomic_counter_t ;

#define HqAtomicIncrement(ptr_, before_) MACRO_START \
  register hq_atomic_counter_t _out_ = 1 ; \
  __asm__ __volatile__ ( \
    "lock\n\t" \
    "xaddl %0,%1\n" \
    : "+r" (_out_), "+m" (*(ptr_)) /* Outputs */ \
    : /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  before_ = _out_ ; \
MACRO_END

#define HqAtomicDecrement(ptr_, after_) MACRO_START \
  register hq_atomic_counter_t _out_ = -1 ; \
  __asm__ __volatile__ ( \
    "lock\n\t" \
    "xaddl %0,%1\n\t" \
    "dec   %0\n" \
    : "+r" (_out_), "+m" (*(ptr_)) /* Outputs */ \
    : /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  after_ = _out_ ; \
MACRO_END

#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) MACRO_START \
  register uint8 _swapped_ ; \
  __asm__ __volatile__ ( \
    "lock\n\t" \
    "cmpxchgl %2,%0\n\t" \
    "setz     %b1\n" /* %b1 means low-byte portion of %1 */ \
    : "+m" (*(ptr_)), "=q" (_swapped_) /* Outputs */        \
    : "r" (swapfor_), "a" (compareto_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  swapped_ = (HqBool)_swapped_ ; \
MACRO_END

#if PLATFORM_IS_32BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) \
  HqAtomicCAS((ptr_), (compareto_), (swapfor_), swapped_)
#elif PLATFORM_IS_64BIT
#error x64 architecture not supported with old GCC (pre 4.1.1).
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) MACRO_START \
  register uint8 _swapped_ ; \
  __asm__ __volatile__ ( \
    "lock\n\t" \
    "cmpxchgq %2,%0\n\t" \
    "setz     %b1\n" /* %b1 means low-byte portion of %1 */ \
    : "+m" (*(ptr_)), "=q" (_swapped_) /* Outputs */ \
    : "r" (swapfor_), "a" (compareto_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  swapped_ = (HqBool)_swapped_ ; \
MACRO_END
#else
#error Platform is neither 32 nor 64 bits, no pointer CAS implemented
#endif /* Platform width */

/****************************************************************************/

#elif (MACHINE == P_PPC || (MACHINE == P_POWERMAC && __BIG_ENDIAN__)) && defined(__GNUC__) /* PPC gcc < 4.1 */

#define HQ_ATOMIC_SUPPORTED 1

typedef int32 hq_atomic_counter_t ;

#define HqAtomicIncrement(ptr_, before_) MACRO_START \
  register hq_atomic_counter_t _out_, _tmp_ ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "lwarx  %0,0,%1\n\t" \
    "addi   %2,%0,1\n\t" \
    "stwcx. %2,0,%1\n\t" \
    "bne-   0b\n" \
    : "=r" (_out_) /* Outputs */ \
    : "b" (ptr_), "r" (_tmp_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  before_ = _out_ ; \
MACRO_END

#define HqAtomicDecrement(ptr_, after_) MACRO_START \
  register hq_atomic_counter_t _out_ ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "lwarx  %0,0,%1\n\t" \
    "subi   %0,%0,1\n\t" \
    "stwcx. %0,0,%1\n\t" \
    "bne-   0b\n" \
    : "=r" (_out_) /* Outputs */ \
    : "b" (ptr_), "0" (_out_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  after_ = _out_ ; \
MACRO_END

#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) MACRO_START \
  register hq_atomic_counter_t _old_, _compareto_ = (compareto_) ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "lwarx  %0,0,%1\n\t" \
    "cmpw   %0,%2\n\t" \
    "bne-   1f\n\t" \
    "stwcx. %3,0,%1\n\t" \
    "bne-   0b\n" \
    "1:\n" \
    : "=r" (_old_) /* Outputs */ \
    : "b" (ptr_), "r" (_compareto_), "r" (swapfor_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  swapped_ = (_old_ == _compareto_) ; \
MACRO_END

#if PLATFORM_IS_32BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) \
  HqAtomicCAS((ptr_), (compareto_), (swapfor_), swapped_)
#elif PLATFORM_IS_64BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) MACRO_START \
  register void *_old_, _compareto_ = (compareto_) ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "ldarx  %0,0,%1\n\t" \
    "cmpd   %0,%2\n\t" \
    "bne-   1f\n\t" \
    "stdcx. %3,0,%1\n\t" \
    "bne-   0b\n" \
    "1:\n" \
    : "=r" (_old_) /* Outputs */ \
    : "b" (ptr_), "r" (_compareto_), "r" (swapfor_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */ \
  ); \
  swapped_ = (_old_ == _compareto_) ; \
MACRO_END
#else
#error Platform is neither 32 nor 64 bits, no pointer CAS implemented
#endif /* Platform size */

/****************************************************************************/

#elif MACHINE == P_ARMv6 && defined(__GNUC__) /* ARM architecture v6 gcc < 4.1 */

#define HQ_ATOMIC_SUPPORTED 1

typedef int32 hq_atomic_counter_t ;

#define HqAtomicIncrement(ptr_, before_) MACRO_START \
  register hq_atomic_counter_t _out_, _tmp_ ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "ldrex %0,[%2]\n\t" \
    "add   %0,%0,#1\n\t" \
    "strex %1,%0,[%2]\n\t" \
    "cmp %1,#0\n\t" \
    "b.ne  0b\n\t" \
    "sub   %0,%0,#1\n" \
    : "=r" (_out_), "r" (_tmp_) /* Outputs */ \
    : "r" (ptr_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */             \
  ); \
  before_ = _out_ ; \
MACRO_END

#define HqAtomicDecrement(ptr_, after_) MACRO_START \
  register hq_atomic_counter_t _out_, _tmp_ ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "ldrex %0,[%2]\n\t" \
    "sub   %0,%0,#1\n\t" \
    "strex %1,%0,[%2]\n\t" \
    "cmp %1,#0\n\t" \
    "b.ne  0b\n" \
    : "=r" (_out_), "r" (_tmp_) /* Outputs */ \
    : "r" (ptr_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */             \
  ); \
  after_ = _out_ ; \
MACRO_END

#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) MACRO_START \
  register hq_atomic_counter_t _old_, _compareto_ = (compareto_) ; \
  __asm__ __volatile__ ( \
    "\n" \
    "0:\n\t" \
    "ldrex    %0,[%1]\n\t" \
    "cmp      %0,%2\n\t" \
    "strex.eq %0,%3,[%1]\n\t" \
    "b.ne     1f\n\t" \
    "cmp      %0,#0\n\t" \
    "b.ne     0b\n\t" \
    "mov      %0,%2\n" \
    "1:\n" \
    : "=r" (_old_) /* Outputs */ \
    : "r" (ptr_), "r" (_compareto_), "r" (swapfor_) /* Inputs */ \
    : "memory", "cc" /* Clobbers */                                  \
  ); \
  swapped_ = (_old_ == _compareto_) ; \
MACRO_END

#if PLATFORM_IS_32BIT
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) \
  HqAtomicCAS((ptr_), (compareto_), (swapfor_), swapped_)
#elif PLATFORM_IS_64BIT
#error No 64-bit Pointer CAS implemented
#else
#error Platform is neither 32 nor 64 bits, no pointer CAS implemented
#endif

/****************************************************************************/

#elif MACHINE == P_ARMv5 && defined(__ghs__) /* ARM ghs */


#error NYI GHS ARMv5

/****************************************************************************/

#elif S_SPLINT_S
typedef int hq_atomic_counter_t ;
#define HqAtomicIncrement(ptr_, before_) EMPTY_STATEMENT()
#define HqAtomicDecrement(ptr_, after_) EMPTY_STATEMENT()
#define HqAtomicCAS(ptr_, compareto_, swapfor_, swapped_) EMPTY_STATEMENT()
#define HqAtomicCASPointer(ptr_, compareto_, swapfor_, swapped_, type_) \
  EMPTY_STATEMENT()
#else
#error No definition for atomic operations for this platform
#endif

#endif /* !HQ_ATOMIC_SUPPORTED */


/* Note: This is a preliminary interface to memory barriers, that is subject to
   change. But at least we can find the code that needs them. */


#define HqMemoryBarrierLoadLoad() EMPTY_STATEMENT()

#define HqMemoryBarrierLoadStore() EMPTY_STATEMENT()

/** Memory barrier to prevent stores preceding the barrier being reordered after
    loads following the barrier. (NYI) */
#define HqMemoryBarrierStoreLoad() EMPTY_STATEMENT()

#define HqMemoryBarrierStoreStore() EMPTY_STATEMENT()

/** Memory barrier to prevent all load/store reordering across it. (NYI) */
#define HqMemoryBarrier() EMPTY_STATEMENT()


#endif /* __HQATOMIC_H__ */

