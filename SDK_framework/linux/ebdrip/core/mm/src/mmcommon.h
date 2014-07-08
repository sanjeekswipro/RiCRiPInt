/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmcommon.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal definitions to build SWmm_common.
 */

#ifndef __MMCOMMON_H__
#define __MMCOMMON_H__

/* mm_init
 *   - If 'block' is NULL then the mm system will atempt to create a vm
 *     arena of upto size 'size'
 *   - Otherwise 'block' is assumed, as before, to point to a block of
 *     allocated memory of size 'size'
 *
 * Note that for platforms which do not support vm arenas (currently this
 * is just MacOs), stub functions have been provided by the mm library.
 * It is the responsibilty of the caller (i.e. coreskin) to ensure that
 * unsupported platforms only use the original client arena functionality.
 *
 * Returns MM_FAILURE if insufficient memory was passed, or too much was
 * requested.
 */

extern mm_result_t mm_init( mps_arena_t arena,
                            mm_size_t addrsize,
                            mm_size_t workingsize,
                            mm_size_t emergencysize,
                            int32     useallmem ) ;

/* Notify the memory manager of an exit from the RIP. */
extern void mm_finish( int32 abort ) ;

/*
Log stripped */
#endif /* Protection from multiple inclusion */
