/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmfence.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fencepost defines and types
 */

#ifndef __MMFENCE_H__
#define __MMFENCE_H__

#include "mm.h"


#if defined( USE_MM_DEBUGGING ) || defined( DEBUG_BUILD )
#undef  MM_DEBUG_FENCEPOST
#define MM_DEBUG_FENCEPOST_LITE
#else
#undef  MM_DEBUG_FENCEPOST
#undef  MM_DEBUG_FENCEPOST_LITE
#endif


#if defined(MM_DEBUG_FENCEPOST) || defined(MM_DEBUG_FENCEPOST_LITE)

/* When fenceposting is on, every allocated object includes 8 bytes at
 * each end containing a fencepost value. Using the tagging mechanism,
 * we can check every fencepost.
 */


/* Check all the fenceposts. May be a useful function to call from a
 * debugger
 */
void mm_debug_check_fenceposts( void ) ;

void mm_debug_check_single_fencepost(mm_pool_t pool, mm_addr_t ptr, size_t size);


#define FENCEPOST_SIZE              ( 8 )

#define ADJUST_FOR_FENCEPOSTS(size) \
  (DWORD_ALIGN_UP( size_t, size ) + FENCEPOST_SIZE + FENCEPOST_SIZE)
#define BELOW_FENCEPOST(ptr) ((mm_addr_t)((char*)(ptr) - FENCEPOST_SIZE))
#define ABOVE_FENCEPOST(ptr) ((mm_addr_t)((char*)(ptr) + FENCEPOST_SIZE))
#define UPPER_FENCEPOST(ptr, size) \
  (PTR_ALIGN_UP_P2(uint32*, (char*)(ptr) + (size), 8) - 2)


/** Set the fenceposts for a single object, and return the adjusted ptr. */
mm_addr_t mm_debug_set_fencepost(mm_pool_t pool, mm_addr_t ptr, size_t size);


/* When an object is truncated, a new fencepost must be written at the
 * new end of the object
 */
void mm_debug_truncate_fencepost( mm_addr_t ptr ) ;


#else /* ! MM_DEBUG_FENCEPOST || defined(MM_DEBUG_FENCEPOST_LITE) */


#define FENCEPOST_SIZE                    ( 0 )
#define ADJUST_FOR_FENCEPOSTS(size)       ( size )
#define BELOW_FENCEPOST(ptr)              ( ptr )
#define ABOVE_FENCEPOST(ptr)              ( ptr )
#define mm_debug_check_single_fencepost(pool, ptr, size) EMPTY_STATEMENT()
#define mm_debug_check_fenceposts()       EMPTY_STATEMENT()
#define mm_debug_set_fencepost(pool, ptr, size) (ptr)
#define mm_debug_truncate_fencepost(ptr)  EMPTY_STATEMENT()


#endif /* MM_DEBUG_FENCEPOST || defined(MM_DEBUG_FENCEPOST_LITE) */

#endif /* __MMFENCE_H__ */

/* Log stripped */
