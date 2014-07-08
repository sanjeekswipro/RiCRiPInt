/** \file
 * \ingroup mm
 *
 * $HopeName: SWcore!shared:basemap.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Temporary kludge to allow all compounds access to large area of memory.
 * SWv20 keeps a large area called "basemap1" which is used for band space
 * and the like. There are a couple of semaphore functions that check whether
 * a caller can get access to the memory.
 */

#ifndef __BASEMAP_H__
#define __BASEMAP_H__

#define BASEMAP_DEFAULT_SIZE 300000

/** \brief Get the semaphore for the basemap.

    \param[out] memptr   Where the basemap address is stored.
    \param[out] memsize  Where the basemap size is stored.

    \return If the basemap is available, a non-zero semaphore is returned,
            and the size and pointer to the basemap are filled in. This
            basemap semaphore should be passed to \c free_basemap_semaphore()
            when releasing the basemap. If the semaphore is already held, zero
            is returned.

    \note The return value should only be used to assert that the basemap is
          available. It is not a thread-safe lock. */
uint32 get_basemap_semaphore(void **memptr, uint32 *memsize);

/* \brief Free the basemap semaphore.

   \param[in] semaphore  The semaphore value returned by
                         \c get_basemap_semaphore().

   Do not use the basemap pointers after calling this routine. */
void free_basemap_semaphore(uint32 semaphore);

#if defined( ASSERT_BUILD )
/* Use this function in asserts to check if the semaphore is current in
   multi-phase protocols. */
int32 got_basemap_semaphore(uint32 semaphore) ;
#endif

/*
Log stripped */
#endif /* Protection from multiple inclusion */
