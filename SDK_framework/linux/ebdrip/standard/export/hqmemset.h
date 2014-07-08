/** \file
 * \ingroup hqmem
 *
 * $HopeName: HQNc-standard!export:hqmemset.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * SW API for setting a blocks of memory to a fixed value, proving a
 * uniform alternative to the C functions memset/memfill/bzero.
 *
 * In fact on some platforms they map down onto those functions. On others,
 * they call out to the OS, or execute our own optimised routines. The
 * decision of which to use on each platform should be based on careful
 * testing.
 *
 * Memset-like functionality has been found to be a rip bottleneck for many
 * different types of jobs on many different platforms. Its execution is
 * therefore often time critical. Therefore a suite of functions that can be
 * optimised on a per platform/build basis is important. But they will be
 * called a huge number of times from bit-setting code, so even the function
 * call overhead can be important. Thus the API is actually implemented as a
 * set of macros, so it is guaraneteed the code can be placed inline where
 * appropriate.
 */

#ifndef __HQMEMSET_H__
#define __HQMEMSET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "std.h"

/*
 * APIs are of the form HqMemSetXX/HqMemZero, but these need to be
 * macros for performance reasons. So have a function call equivalent of
 * each of these with a '_f' suffix.
 *
 * For short runs (<= 32 bytes) it is best to do the memory fill as an
 * inline set of direct assignments. This is best achieved by a switch
 * on the fill size with each one doing one byte and then dropping through.
 * The default case is handled by calling out to the function version.
 */

/**
 * For ease of profiling and performance testing it is often the case
 * that having all memsets as a very simple implementation in their own
 * function (and not inline) is useful. If this is required, set the
 * following define to 1.
 */
#define SIMPLEST_MEMSET 0

void HQNCALL HqMemSet8_f(uint8 *ptr, uint8 val, size_t count);
void HQNCALL HqMemSet16_f(uint16 *ptr, uint16 val, size_t count);
void HQNCALL HqMemSet32_f(uint32 *ptr, uint32 val, size_t count);
#ifdef HQN_INT64
void HQNCALL HqMemSet64_f(uint64 *ptr, uint64 val, size_t count);
#endif

#if SIMPLEST_MEMSET

#define HqMemSet8(_p, _v, _c) HqMemSet8_f(_p, _v, _c)
#define HqMemSet16(_p, _v, _c) HqMemSet16_f(_p, _v, _c)
#define HqMemSet32(_p, _v, _c) HqMemSet32_f(_p, _v, _c)
#define HqMemSet64(_p, _v, _c) HqMemSet64_f(_p, _v, _c)

#else /* !SIMPLEST_MEMSET */

#define HqMemSet8(_p, _v, _c) MACRO_START        \
  register uint8 *_p_ = (_p) ;                   \
  register uint8  _v_ = (_v) ;                   \
  register size_t _c_ = (_c) ;                   \
  switch ( _c_ )                                 \
  {                                              \
    case 32: _p_[31] = _v_; /* FALLTHROUGH */    \
    case 31: _p_[30] = _v_; /* FALLTHROUGH */    \
    case 30: _p_[29] = _v_; /* FALLTHROUGH */    \
    case 29: _p_[28] = _v_; /* FALLTHROUGH */    \
    case 28: _p_[27] = _v_; /* FALLTHROUGH */    \
    case 27: _p_[26] = _v_; /* FALLTHROUGH */    \
    case 26: _p_[25] = _v_; /* FALLTHROUGH */    \
    case 25: _p_[24] = _v_; /* FALLTHROUGH */    \
    case 24: _p_[23] = _v_; /* FALLTHROUGH */    \
    case 23: _p_[22] = _v_; /* FALLTHROUGH */    \
    case 22: _p_[21] = _v_; /* FALLTHROUGH */    \
    case 21: _p_[20] = _v_; /* FALLTHROUGH */    \
    case 20: _p_[19] = _v_; /* FALLTHROUGH */    \
    case 19: _p_[18] = _v_; /* FALLTHROUGH */    \
    case 18: _p_[17] = _v_; /* FALLTHROUGH */    \
    case 17: _p_[16] = _v_; /* FALLTHROUGH */    \
    case 16: _p_[15] = _v_; /* FALLTHROUGH */    \
    case 15: _p_[14] = _v_; /* FALLTHROUGH */    \
    case 14: _p_[13] = _v_; /* FALLTHROUGH */    \
    case 13: _p_[12] = _v_; /* FALLTHROUGH */    \
    case 12: _p_[11] = _v_; /* FALLTHROUGH */    \
    case 11: _p_[10] = _v_; /* FALLTHROUGH */    \
    case 10: _p_[ 9] = _v_; /* FALLTHROUGH */    \
    case  9: _p_[ 8] = _v_; /* FALLTHROUGH */    \
    case  8: _p_[ 7] = _v_; /* FALLTHROUGH */    \
    case  7: _p_[ 6] = _v_; /* FALLTHROUGH */    \
    case  6: _p_[ 5] = _v_; /* FALLTHROUGH */    \
    case  5: _p_[ 4] = _v_; /* FALLTHROUGH */    \
    case  4: _p_[ 3] = _v_; /* FALLTHROUGH */    \
    case  3: _p_[ 2] = _v_; /* FALLTHROUGH */    \
    case  2: _p_[ 1] = _v_; /* FALLTHROUGH */    \
    case  1: _p_[ 0] = _v_; /* FALLTHROUGH */    \
    case  0: break;                              \
    default: HqMemSet8_f(_p_, _v_, _c_); break;  \
  } \
MACRO_END

#define HqMemSet16(_p, _v, _c) MACRO_START        \
  register uint16 *_p_ = (_p) ;                   \
  register uint16  _v_ = (_v) ;                   \
  register size_t  _c_ = (_c) ;                   \
  switch ( _c_ )                                  \
  {                                               \
    case 16: _p_[15] = _v_; /* FALLTHROUGH */     \
    case 15: _p_[14] = _v_; /* FALLTHROUGH */     \
    case 14: _p_[13] = _v_; /* FALLTHROUGH */     \
    case 13: _p_[12] = _v_; /* FALLTHROUGH */     \
    case 12: _p_[11] = _v_; /* FALLTHROUGH */     \
    case 11: _p_[10] = _v_; /* FALLTHROUGH */     \
    case 10: _p_[ 9] = _v_; /* FALLTHROUGH */     \
    case  9: _p_[ 8] = _v_; /* FALLTHROUGH */     \
    case  8: _p_[ 7] = _v_; /* FALLTHROUGH */     \
    case  7: _p_[ 6] = _v_; /* FALLTHROUGH */     \
    case  6: _p_[ 5] = _v_; /* FALLTHROUGH */     \
    case  5: _p_[ 4] = _v_; /* FALLTHROUGH */     \
    case  4: _p_[ 3] = _v_; /* FALLTHROUGH */     \
    case  3: _p_[ 2] = _v_; /* FALLTHROUGH */     \
    case  2: _p_[ 1] = _v_; /* FALLTHROUGH */     \
    case  1: _p_[ 0] = _v_; /* FALLTHROUGH */     \
    case  0: break;                               \
    default: HqMemSet16_f(_p_, _v_, _c_); break; \
  } \
MACRO_END

#define HqMemSet32(_p, _v, _c) MACRO_START        \
  register uint32 *_p_ = (_p) ;                   \
  register uint32  _v_ = (_v) ;                   \
  register size_t  _c_ = (_c) ;                   \
  switch ( _c_ )                                 \
  {                                               \
    case  8: _p_[ 7] = _v_; /* FALLTHROUGH */     \
    case  7: _p_[ 6] = _v_; /* FALLTHROUGH */     \
    case  6: _p_[ 5] = _v_; /* FALLTHROUGH */     \
    case  5: _p_[ 4] = _v_; /* FALLTHROUGH */     \
    case  4: _p_[ 3] = _v_; /* FALLTHROUGH */     \
    case  3: _p_[ 2] = _v_; /* FALLTHROUGH */     \
    case  2: _p_[ 1] = _v_; /* FALLTHROUGH */     \
    case  1: _p_[ 0] = _v_; /* FALLTHROUGH */     \
    case  0: break;                               \
    default: HqMemSet32_f(_p_, _v_, _c_); break; \
  } \
MACRO_END

#define HqMemSet64(_p, _v, _c) MACRO_START        \
  register uint64 *_p_ = (_p) ;                   \
  register uint64  _v_ = (_v) ;                   \
  register size_t  _c_ = (_c) ;                   \
  switch ( _c_ )                                  \
  {                                               \
    case  4: _p_[ 3] = _v_; /* FALLTHROUGH */     \
    case  3: _p_[ 2] = _v_; /* FALLTHROUGH */     \
    case  2: _p_[ 1] = _v_; /* FALLTHROUGH */     \
    case  1: _p_[ 0] = _v_; /* FALLTHROUGH */     \
    case  0: break;                               \
    default: HqMemSet64_f(_p_, _v_, _c_); break;  \
  } \
MACRO_END

#endif /* SIMPLEST_MEMSET */

/*
 * Macro for setting the value of an array of pointers
 */
#ifdef PLATFORM_IS_64BIT
/* GCC requires cast through char * to suppress alias optimisation and
   warnings because char pointers can be aliased. */
#define HqMemSetPtr(_p, _v, _c) HqMemSet64((uint64 *)(char *)(_p), (uint64)(uintptr_t)(_v), (_c))
#else
/* GCC requires cast through char * to suppress alias optimisation and
   warnings because char pointers can be aliased. */
#define HqMemSetPtr(_p, _v, _c) HqMemSet32((uint32 *)(char *)(_p), (uint32)(uintptr_t)(_v), (_c))
#endif

#define HqMemZero(_p, _c) HqMemSet8_f((uint8 *)(_p), (uint8)0, (size_t)(_c))

#ifdef __cplusplus
}
#endif

#endif  /* __HQMEMSET_H__ */

