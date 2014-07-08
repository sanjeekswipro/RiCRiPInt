/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mm_swmemapi.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Instantiate and destroy a memory callback API using allocation from
 * a pool.
 */

#include "core.h"
#include "mm.h"
#include "mmcompat.h"
#include "swmemapi.h"
#include "hqassert.h"
#include "uvms.h"

/** Subclass of \c sw_memory_instance used to store the MM pool from which
    allocations will be taken. */
typedef struct mm_swmemory_instance {
  sw_memory_instance superclass ; /**< Superclass MUST be first entry. */
  mm_pool_t pool ;                /**< Pool from which allocations are taken. */
  int mmclass ;                   /**< Allocation class for entries. */
} mm_swmemory_instance ;

/** Allocation callback for MM pool \c sw_memory_instance implementation. */
static void * RIPCALL mm_swmemory_alloc(sw_memory_instance *instance,
                                        size_t n)
{
  /* Downcast instance to subclass. */
  mm_swmemory_instance *mminstance = (mm_swmemory_instance *)instance ;
  void *memory = NULL ;

  if ( mminstance != NULL ) {
    HQASSERT(mminstance->pool, "No pool for callback allocation") ;
    memory = mm_alloc_with_header(mminstance->pool, n, mminstance->mmclass) ;
  }

  return memory ;
}

/** Free callback for MM pool \c sw_memory_instance implementation. */
static void RIPCALL mm_swmemory_free(sw_memory_instance *instance,
                                     void *p)
{
  /* Downcast instance to subclass. */
  mm_swmemory_instance *mminstance = (mm_swmemory_instance *)instance ;

  if ( mminstance != NULL ) {
    HQASSERT(mminstance->pool, "No pool for callback free") ;
    mm_free_with_header(mminstance->pool, p) ;
  }
}

/** Implementation of the sw_memory_api interface. */
const static sw_memory_api mm_swmemory_implementation = {
  {
    SW_MEMORY_API_VERSION_20071110,           /* version */
    (const uint8 *)"mmpool",                  /* name */
    UVS("Harlequin RIP memory callback API"), /* display_name */
    sizeof(mm_swmemory_instance)              /* N.B. subclass instance size */
  },
  mm_swmemory_alloc,
  mm_swmemory_free,
} ;

sw_memory_instance *mm_swmemory_create(mm_pool_t pool, int mmclass)
{
  mm_swmemory_instance *mminstance ;

  HQASSERT(pool, "No pool for creating MM callback API instance") ;

  /* Allocate sub-class instance. */
  if ( (mminstance = mm_alloc(pool, sizeof(mm_swmemory_instance), mmclass)) == NULL )
    return NULL ;

  mminstance->superclass.implementation = &mm_swmemory_implementation ;
  mminstance->pool = pool ;
  mminstance->mmclass = mmclass ;

  /* Upcast to superclass */
  return &mminstance->superclass ;
}

void mm_swmemory_destroy(sw_memory_instance **pinstance)
{
  mm_swmemory_instance *mminstance ;

  HQASSERT(pinstance, "Nowhere to find API instance") ;

  /* Downcast to subclass instance. */
  if ( (mminstance = (mm_swmemory_instance *)*pinstance) != NULL ) {
    mm_free(mminstance->pool, mminstance, sizeof(mm_swmemory_instance)) ;
    *pinstance = NULL ;
  }
}

/* Log stripped */
