/** \file
 * \ingroup hqmem
 *
 * $HopeName: HQNc-standard!src:hqmemset.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Harlequin standard memory set/fill implementation
 */

#include "hqmemset.h"
#include "caching.h"

/**
 * Check that the pointer is aligned to the given boundary
 */
#define PTR_IS_ALIGNED(_p, _a) ((((uintptr_t)(_p)) & ((_a) - 1)) == 0)

/**
 * Set the block of memory to the given 8bit value
 *
 * On most desktop O/S builds, just using memset() if the fastest possibility.
 * (memset is only slower for small counts, and this has already been dealt
 * with by the macro wrapper.
 *
 * On other less reliable platforms, do it ourselves by doing the first and
 * last few bytes by hand, and the middle in 8-unrolled longword loops.
 *
 * On MS Windows (at least on a Intel dual core CPU with msdev8), __stosb()
 * was found to be quicker that memset for large values of count (greater
 * than about 1kBytes). But this will require intrinsics headers and possibly
 * run-time control, so support has not been added as yet.
 *
 * \todo BMJ 11-Mar-09 :  Look into compile-time/run-time use of intrinsics
 * especially __stosb()
 *
 * Linux also seemed to have an optimisation of inlining memset and using
 * the stos-type intrinsics. But I could not get it to work on the platforms
 * I tried it on. See __USE_STRING_INLINES in /usr/include/bits/string.h.
 *
 * Note : may want to prefix functions with "__declspec(noinline)" on Windows
 * when doing performance analysis to ensure fuunctions appear in profiles.
 *
 * \todo BMJ 11-Mar-09 :  Further investigation into intriniscs on unix
 * platforms.
 */
void HQNCALL HqMemSet8_f(uint8 *ptr, uint8 val, size_t count)
{
  HQASSERT(ptr, "Memory setting : NULL pointer");

#if SIMPLEST_MEMSET
  {
    size_t i;

    for ( i = 0; i < count ; i++ )
      ptr[i] = val;
  }
#elif defined(WIN32)||defined(MACINTOSH)||defined(UNIX)||defined(VXWORKS)
  {
    (void)memset((void *)ptr, (int)val, count);
  }
#else
  {
    if ( count == 0 )
      return;

    if ( count >= 8 )
    {
      register int32 wval;
      register intptr_t align = ((intptr_t)ptr & 3);
      register int32 *wptr;

      /* Align to word boundary, then use word copy */
      switch ( align )
      {
        case 1: *ptr++ = val;
        case 2: *ptr++ = val;
        case 3: *ptr++ = val;
        count -= 4 - align;
      }
      wptr = (int32 *)ptr;
      wval = (val & 0xff);
      wval |= wval << 8;
      wval |= wval << 16;

      while ( count >= 32 )
      {
        PENTIUM_CACHE_LOAD(wptr + 7);
        wptr[0] = wval;
        wptr[1] = wval;
        wptr[2] = wval;
        wptr[3] = wval;
        wptr[4] = wval;
        wptr[5] = wval;
        wptr[6] = wval;
        wptr[7] = wval;
        wptr += 8;
        count -= 32;
      }
      while ( count >= 4 )
      {
        *wptr++ = wval;
        count -= 4;
      }
      ptr = (int8 *)wptr;
    }
    HQASSERT(count < 8, "Count too large\n");

    switch (count)
    {
      case 7: ptr[6] = val; /* FALLTHROUGH */
      case 6: ptr[5] = val; /* FALLTHROUGH */
      case 5: ptr[4] = val; /* FALLTHROUGH */
      case 4: ptr[3] = val; /* FALLTHROUGH */
      case 3: ptr[2] = val; /* FALLTHROUGH */
      case 2: ptr[1] = val; /* FALLTHROUGH */
      case 1: ptr[0] = val; /* FALLTHROUGH */
      case 0: break;
    }
  }
#endif
}

/**
 * Set the block of memory to the given 16bit value
 *
 * No performance testing done on this variation as yet, so just
 * implement it as an 8-unrolled loop with PENTIUM_CACHE_LOAD
 *
 * \todo BMJ 12-Mar-09 :  Check optimallity of thi simplementation
 */
void HQNCALL HqMemSet16_f(uint16 *ptr, uint16 val, size_t count)
{
  HQASSERT(ptr, "Memory setting : NULL pointer");
  HQASSERT(PTR_IS_ALIGNED(ptr, 2), "Memory fill on unaligned 16bit pointer");

#if SIMPLEST_MEMSET
  {
    size_t i;

    for ( i = 0; i < count ; i++ )
      ptr[i] = val;
  }
#else
  {
    register size_t n;

    for ( n = count >> 3; n != 0; n--, ptr += 8 )
    {
      PENTIUM_CACHE_LOAD(ptr + 7);
      ptr[0] = val;
      ptr[1] = val;
      ptr[2] = val;
      ptr[3] = val;
      ptr[4] = val;
      ptr[5] = val;
      ptr[6] = val;
      ptr[7] = val;
    }
    switch ( (count & 7) )
    {
      case 7 : ptr[6] = val;
      case 6 : ptr[5] = val;
      case 5 : ptr[4] = val;
      case 4 : ptr[3] = val;
      case 3 : ptr[2] = val;
      case 2 : ptr[1] = val;
      case 1 : ptr[0] = val;
    }
  }
#endif
}

/**
 * Set the block of memory to the given 32bit value.
 *
 * On windows, with a recent (msdev8) MS compiler, if you leave the
 * code as a simple for loop the optimiser will detect the idiom
 * and generate "rep stosd" assembler. This is the fastest
 * implementation for large values of count, better than any unrolled
 * for loop, and equivalent to using the __stosd() intrinsic.
 * [ 2.5 Gbytes/sec versus 1.5 for an unrolled loop ]
 * For small values of count the best is just a switch statement with
 * sequential assignments. So this function is wrapped in a macro that deals
 * with small values of count inline.
 *
 * On other platforms, a 8-unrolled loop with use of the PENTIUM_CACHE_LOAD
 * macro (if appropriate) was found to be the fastest.
 *
 * On Vxworks, a 32-unrolled loop was shown to go a little faster.
 * [ Vxworks target platform is PowerPC, so PENTIUM_CACHE_LOAD will be a
 * null macro anyway. So not sure if that should be in the loop or not
 * for any Intel Vxworks platforms. Tried various other tricks to effect
 * the cache on Vxworks/PowerPC, but with no gains. ]
 */
void HQNCALL HqMemSet32_f(uint32 *ptr, uint32 val, size_t count)
{
  HQASSERT(ptr, "Memory setting : NULL pointer");
  HQASSERT(PTR_IS_ALIGNED(ptr, 4), "Memory fill on unaligned 32bit pointer");

#if SIMPLEST_MEMSET || defined(WIN32)
  {
    size_t i;

    for ( i = 0; i < count ; i++ )
      ptr[i] = val;
  }
#elif defined(VXWORKS)
  {
    register size_t n;

    for ( n = count >> 5; n != 0; n--, ptr += 32 )
    {
      ptr[ 0] = val;
      ptr[ 1] = val;
      ptr[ 2] = val;
      ptr[ 3] = val;
      ptr[ 4] = val;
      ptr[ 5] = val;
      ptr[ 6] = val;
      ptr[ 7] = val;
      ptr[ 8] = val;
      ptr[ 9] = val;
      ptr[10] = val;
      ptr[11] = val;
      ptr[12] = val;
      ptr[13] = val;
      ptr[14] = val;
      ptr[15] = val;
      ptr[16] = val;
      ptr[17] = val;
      ptr[18] = val;
      ptr[19] = val;
      ptr[20] = val;
      ptr[21] = val;
      ptr[22] = val;
      ptr[23] = val;
      ptr[24] = val;
      ptr[25] = val;
      ptr[26] = val;
      ptr[27] = val;
      ptr[28] = val;
      ptr[29] = val;
      ptr[30] = val;
      ptr[31] = val;
    }
    switch ( (count & 31) )
    {
      case 31 : ptr[30] = val; /* FALLTHROUGH */
      case 30 : ptr[29] = val; /* FALLTHROUGH */
      case 29 : ptr[28] = val; /* FALLTHROUGH */
      case 28 : ptr[27] = val; /* FALLTHROUGH */
      case 27 : ptr[26] = val; /* FALLTHROUGH */
      case 26 : ptr[25] = val; /* FALLTHROUGH */
      case 25 : ptr[24] = val; /* FALLTHROUGH */
      case 24 : ptr[23] = val; /* FALLTHROUGH */
      case 23 : ptr[22] = val; /* FALLTHROUGH */
      case 22 : ptr[21] = val; /* FALLTHROUGH */
      case 21 : ptr[20] = val; /* FALLTHROUGH */
      case 20 : ptr[19] = val; /* FALLTHROUGH */
      case 19 : ptr[18] = val; /* FALLTHROUGH */
      case 18 : ptr[17] = val; /* FALLTHROUGH */
      case 17 : ptr[16] = val; /* FALLTHROUGH */
      case 16 : ptr[15] = val; /* FALLTHROUGH */
      case 15 : ptr[14] = val; /* FALLTHROUGH */
      case 14 : ptr[13] = val; /* FALLTHROUGH */
      case 13 : ptr[12] = val; /* FALLTHROUGH */
      case 12 : ptr[11] = val; /* FALLTHROUGH */
      case 11 : ptr[10] = val; /* FALLTHROUGH */
      case 10 : ptr[ 9] = val; /* FALLTHROUGH */
      case  9 : ptr[ 8] = val; /* FALLTHROUGH */
      case  8 : ptr[ 7] = val; /* FALLTHROUGH */
      case  7 : ptr[ 6] = val; /* FALLTHROUGH */
      case  6 : ptr[ 5] = val; /* FALLTHROUGH */
      case  5 : ptr[ 4] = val; /* FALLTHROUGH */
      case  4 : ptr[ 3] = val; /* FALLTHROUGH */
      case  3 : ptr[ 2] = val; /* FALLTHROUGH */
      case  2 : ptr[ 1] = val; /* FALLTHROUGH */
      case  1 : ptr[ 0] = val; /* FALLTHROUGH */
      case  0 : break;
    }
  }
#else
  {
    register size_t n;

    for ( n = count >> 3; n != 0; n--, ptr += 8 )
    {
      PENTIUM_CACHE_LOAD(ptr + 7);
      ptr[0] = val;
      ptr[1] = val;
      ptr[2] = val;
      ptr[3] = val;
      ptr[4] = val;
      ptr[5] = val;
      ptr[6] = val;
      ptr[7] = val;
    }
    switch ( (count & 7) )
    {
      case 7 : ptr[6] = val; /* FALLTHROUGH */
      case 6 : ptr[5] = val; /* FALLTHROUGH */
      case 5 : ptr[4] = val; /* FALLTHROUGH */
      case 4 : ptr[3] = val; /* FALLTHROUGH */
      case 3 : ptr[2] = val; /* FALLTHROUGH */
      case 2 : ptr[1] = val; /* FALLTHROUGH */
      case 1 : ptr[0] = val; /* FALLTHROUGH */
      case 0 : break;
    }
  }
#endif
}

#ifdef HQN_INT64
/**
 * Set the block of memory to the given 64bit value.
 *
 * No performance measurements on this function as yet, so
 * just implement it as a for loop until data available.
 *
 * \todo BMJ 12-Mar-09 :  work out optimal version of this function
 */
void HQNCALL HqMemSet64_f(uint64 *ptr, uint64 val, size_t count)
{
  HQASSERT(ptr, "Memory setting : NULL pointer");
  HQASSERT(PTR_IS_ALIGNED(ptr, 8), "Memory fill on unaligned 64bit pointer");

  {
    size_t i;

    for ( i = 0; i < count ; i++ )
      ptr[i] = val;
  }
}
#endif /* !HQN_INT64 */

/*
* Log stripped */
