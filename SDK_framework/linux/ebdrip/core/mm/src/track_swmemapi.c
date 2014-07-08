/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:track_swmemapi.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * This file implements a sw_memory_api subclass to track allocations. This
 * is used to when communicating with some external modules, so that memory
 * leaks can be avoided, and all of the allocated memory can be recovered
 * when module instances are destructed.
 */

#include "core.h"
#include "ripcall.h"
#include "mm.h"
#include "uvms.h"
#include "hqassert.h"
#include "swmemapi.h"
#include "track_swmemapi.h"
#include "hqmemset.h"

/** The location for pointers returned for allocations of size zero; these
    are successful, so shouldn't return NULL, but shouldn't point anywhere
    that the dynamic allocator could return. */
static uint8 invalid_ptr_location ;

/** A pointer that cannot possibly be returned by the dynamic allocator. */
#define TRACK_INVALID_PTR ( (void*) &invalid_ptr_location )

/** Linked list of memory allocations. */
typedef struct sw_track_mem {
  struct sw_track_mem* next ;
  size_t              size ;
} sw_track_mem ;

/** Subclass of \c sw_memory_instance used to store the tracking data, pool,
    and class from which allocations will be taken. */
typedef struct track_swmemory_instance {
  sw_memory_instance superclass ; /**< Superclass MUST be first entry. */

  mm_pool_t pool ;           /**< Pool from which allocations are taken. */
  int mmclass ;              /**< Allocation class for entries. */
  size_t memory ;            /**< total memory use by the module */
  Bool freed ;               /**< free() has been called. */
  sw_track_mem* bin[32] ;    /**< Hash of lists of memory allocations */
} track_swmemory_instance ;

/** \brief  Tracking memory allocation.

    Allocations within a tracking allocator are linked to the subclass
    instance, so that we can avoid memory leaks by a module. This is achieved
    by extending the length of the allocation by the size of a sw_track_mem*,
    and returning a pointer past this data.

    \param[in] instance The \c track_memory_instance superclass to which to
    attach this allocation.

    \param[in] size  The amount of memory to claim.

    \retval  A pointer to the allocated block, or zero if there is insufficient
    memory available.
  */
static void * RIPCALL track_swmemory_alloc(sw_memory_instance *instance,
                                           size_t size)
{
  /* Downcast instance to subclass. */
  track_swmemory_instance *track_mm = (track_swmemory_instance *)instance ;
  sw_track_mem *ptr = NULL ;

  if ( size == 0 )
    return TRACK_INVALID_PTR ;

  if ( track_mm != NULL ) {
    size += sizeof(sw_track_mem) ;

    HQASSERT(track_mm->pool, "No pool for callback allocation") ;
    ptr = mm_alloc(track_mm->pool, size, track_mm->mmclass) ;
    if ( ptr != NULL ) {
      /* Add to the list of allocations for this TRACK context.
       * There are 32 linked lists, indexed by a hash of the address.
       */
      /* Calculate the hash index. */
      int i = (int)((intptr_t)ptr) ;
      i = (i >> 7) & 31 ;

      HqMemZero((uint8 *)ptr, CAST_SIZET_TO_INT32(size));

      /* add to appropriate linked list */
      ptr->next = track_mm->bin[i] ;
      ptr->size = size ;
      track_mm->bin[i] = ptr ;

      /* step past the structure */
      ptr++ ;

      track_mm->memory += size ;
    }
  }

  return ptr ;
}

/** \brief Tracking allocator memory freeing.

    Allocations within a tracking allocator are removed from a particular
    allocation instance. If tracking allocator is not able to find the
    allocation in the instances's lists, it will NOT be freed.

    It is acceptable to call track_swmemory_free with \c NULL or
    \c TRACK_INVALID_PTR.
  */
static void RIPCALL track_swmemory_free(sw_memory_instance *instance,
                                        void *ptr)
{
  /* Downcast instance to subclass. */
  track_swmemory_instance *track_mm = (track_swmemory_instance *)instance ;

  if ( track_mm != NULL && ptr != NULL && ptr != TRACK_INVALID_PTR) {
    sw_track_mem ** p, * a = NULL, * temp = ((sw_track_mem*)ptr) - 1 ;
    int i =  (int)((intptr_t)temp) ;
    i = (i >> 7) & 31 ;
    /* find the allocation */
    p = &track_mm->bin[i] ;
    while ( (a = *p) != NULL ) {
      if (a == temp) {
        /* detach from the list */
        *p = a->next ;
        break ;
      }
      p = & a->next ;
    }
    if (a == NULL) {
      /* failed to find allocation... this is not good! */
      HQFAIL("Failed to free TRACK module memory") ;
      return ;
    }
    track_mm->memory -= temp->size ;
    track_mm->freed = TRUE ; /* tell track_austerity() that we did something */

    HQASSERT(track_mm->pool, "No pool for callback free") ;
    mm_free(track_mm->pool, temp, temp->size) ;
  }
}

/** Tracking allocator subclass of the sw_memory_api interface. */
const static sw_memory_api track_swmemory_implementation = {
  {
    SW_MEMORY_API_VERSION_20071110,           /* version */
    (const uint8 *)"tracker",                /* name */
    UVS("Harlequin RIP tracking memory callback API"), /* display_name */
    sizeof(track_swmemory_instance)           /* N.B. subclass instance size */
  },
  track_swmemory_alloc,
  track_swmemory_free,
} ;

/* Create a new tracking allocator. */
sw_memory_instance *track_swmemory_create(mm_pool_t pool, int mmclass)
{
  track_swmemory_instance *track_mm ;

  HQASSERT(pool, "No pool for creating TRACK callback API instance") ;

  /* Allocate sub-class instance. */
  if ( (track_mm = mm_alloc(pool, sizeof(track_swmemory_instance), mmclass)) == NULL )
    return NULL ;

  HqMemZero((uint8 *)track_mm, (int32)sizeof(track_swmemory_instance));

  track_mm->superclass.implementation = &track_swmemory_implementation ;
  track_mm->pool = pool ;
  track_mm->mmclass = mmclass ;
  track_mm->memory = 0 ;
  track_mm->freed = FALSE ;

  /* Upcast to superclass */
  return &track_mm->superclass ;
}

/* Clear all memory allocations out of a TRACK tracking allocator. */
void track_swmemory_purge(sw_memory_instance *instance)
{
  track_swmemory_instance *track_mm ;

  /* Downcast to subclass instance. */
  if ( (track_mm = (track_swmemory_instance *)instance) != NULL ) {
    uint32 i ;

    for (i = 0 ; i < 32 ; i++) {
      sw_track_mem *a = track_mm->bin[i] ;
      track_mm->bin[i] = NULL ;

      while (a) {
        sw_track_mem *n = a->next ;
        track_mm->memory -= a->size ;
        mm_free(track_mm->pool, (void*)a, a->size) ;
        a = n ;
        track_mm->freed = TRUE ; /* tell track_austerity() that we did something */
      }
    }
    HQASSERT(track_mm->memory == 0, "Lost some TRACK module memory") ;
  }
}

/* Clear all memory allocations out of a TRACK tracking allocator and then
    destroy the allocator itself. */
void track_swmemory_destroy(sw_memory_instance **pinstance)
{
  track_swmemory_instance *track_mm ;

  HQASSERT(pinstance, "Nowhere to find API instance") ;

  /* Destroy all allocations in the tracker. */
  track_swmemory_purge(*pinstance) ;

  /* Downcast to subclass instance. */
  if ( (track_mm = (track_swmemory_instance *)*pinstance) != NULL ) {
    /* Destroy the tracking instance itself. */
    mm_free(track_mm->pool, track_mm, sizeof(track_swmemory_instance)) ;
    *pinstance = NULL ;
  }
}

/* Return freed memory status of a tracking allocator. */
Bool track_swmemory_freed(sw_memory_instance *instance)
{
  track_swmemory_instance *track_mm ;
  Bool result = FALSE ;

  /* Downcast to subclass instance. */
  if ( (track_mm = (track_swmemory_instance *)instance) != NULL ) {
    result = track_mm->freed ;
    track_mm->freed = FALSE ;
  }

  return result ;
}

/* Return amount of memory allocated in a tracking allocator. */
size_t track_swmemory_size(sw_memory_instance *instance)
{
  track_swmemory_instance *track_mm ;
  size_t result = 0 ;

  /* Downcast to subclass instance. */
  if ( (track_mm = (track_swmemory_instance *)instance) != NULL ) {
    result = track_mm->memory ;
  }

  return result ;
}

/* ========================================================================== */

/* Log stripped */
