/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:caching.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Cache pre-load functions for Pentium
 */

#ifndef __CACHING_H__
#define __CACHING_H__

#include "platform.h"	/* for machine type definitions */

#if PLATFORM_MACHINE == P_INTEL
/* Cache pre-load macros for Pentium; the Pentium has an 8K two-way set
   associative writeback data cache and an 8K read-only instruction cache,
   with a cache line size of 32 bytes. The cache lines are read and written
   using burst transfer mode, so writing into the cache and then spilling the
   cache line to secondary cache/main memory is quicker than writing through
   to memory. Depending on how the second level cache is organised, it may be
   much faster (and usually is a bit faster) to pre-load a cache line, write
   into the cache line, and then flush the cache. This can give substantial
   performance benefits when writing out large quantities of data.
*/
#define PENTIUM_CACHE_LOAD(address) MACRO_START \
  if ( *((volatile uint8 *)(address)) ) {       \
    EMPTY_STATEMENT() ;                         \
  }                                             \
  else {                                        \
    EMPTY_STATEMENT() ;                         \
  }                                             \
MACRO_END

#else /* ! INTEL */

#define PENTIUM_CACHE_LOAD(address) /* Nothing doing, not a Pentium */

#endif /* ! INTEL */

#endif  /* __CACHING_H__ */

