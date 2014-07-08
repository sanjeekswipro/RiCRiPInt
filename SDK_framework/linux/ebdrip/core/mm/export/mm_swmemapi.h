/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:mm_swmemapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Instantiate and destroy a memory callback API to allocate from a pool.
 */

#ifndef __MM_SWMEMAPI_H__
#define __MM_SWMEMAPI_H__

#include "swmemapi.h"
#include "mm.h"

/** \brief Create a new \c sw_memory_instance using the SWmm_common
    allocation functions.

    \param[in] pool The MM pool from which callback allocations will
    be taken.

    \param[in] mmclass  The MM allocation class used to tag allocations.

    \return A non-NULL callback API instance pointer if successful, NULL on
    error. If successful, the pointer must be destroyed with
    mm_swmemory_destroy later. */
sw_memory_instance *mm_swmemory_create(mm_pool_t pool, int mmclass) ;

/** \brief Destroy a \c sw_memory_instance.

    \param[in] pinstance  A pointer to the callback API instance. After this call,
    the API instance pointer will be cleared. */
void mm_swmemory_destroy(sw_memory_instance **pinstance);


#endif /* Protection from multiple inclusion */

/* Log stripped */
