/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:track_swmemapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Instantiate and destroy a tracking memory callback API.
 *
 * The tracking callback API can dispose of all allocations for a module when
 * the module is shut down, to avoid leaks.
 */

#ifndef __TRACK_SWMEMAPI_H__
#define __TRACK_SWMEMAPI_H__

#include "swmemapi.h"

/** \brief Create a new tracking allocator.

     \param[in] pool  The memory pool to take allocations from.

     \param mmclass  The debugging tag to apply to allocations.

     \return If successful, a non-NULL subclass of \c sw_memory_instance. If
     unsuccessful, NULL.
*/
sw_memory_instance *track_swmemory_create(mm_pool_t pool, int mmclass) ;

/** \brief Purge all of the allocations in a tracking allocator.

     \param[in] instance  A tracking allocator, previously created by \c
     track_swmemory_create().
*/
void track_swmemory_purge(sw_memory_instance *instance) ;

/** \brief Clear all memory allocations out of a PFIN tracking allocator and
    then destroy the allocator itself.

    \param[in] pinstance  A pointer to a tracking allocator handle, previously
    created by \c track_swmemory_create(). On exit, the allocator handle is
    set to \c NULL.
 */
void track_swmemory_destroy(sw_memory_instance **pinstance) ;

/** \brief Return a tracking allocator's freed memory indicator.

    \param[in] instance  A pointer to a tracking allocator handle, previously
    created by \c track_swmemory_create().

    \return A flag indicating if anything has been freed from the allocator
    since the last time \c track_swmemory_free() was called.
*/
Bool track_swmemory_freed(sw_memory_instance *instance) ;

/** \brief Return amount of memory allocated in a tracking allocator.

    \param[in] instance  A pointer to a tracking allocator handle, previously
    created by \c track_swmemory_create().

    \return The total size of allocations in the tracking allocator.
*/
size_t track_swmemory_size(sw_memory_instance *instance) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
