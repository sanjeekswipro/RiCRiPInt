/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmtotal.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Collects info on MM pool usage (allocation and object totals)
 */

#ifndef __MMTOTAL_H__
#define __MMTOTAL_H__

#include "mps.h"
#include "mm.h"

#ifdef MM_DEBUG_TOTAL

/* We now have 3 sorts of totals, for basic pool allocations:
 *   current is represents 'live' objects;
 *   highest is the high-water mark since the mm pool was (re)created;
 *   overall is the cumulative total since the mm pool was (re)created
 *
 * For each type, we record the total bytes allocated, and the number of
 * objects that represents. Specifically with highest, we capture the number
 * of objects at the time of the highest byte allocation (rather than 
 * recording, separately, the highest object count).
 *
 * Note that for epvm pools, where we have no inidividual free mechanism, we
 * derive the allocation current total at the time of a restore (which is
 * effectively a 'multi' free) from the total space claimed by that pool, and
 * the free space within the pool. In this case, the obj field represents the
 * number of objects created since the last restore.
 *
 * Additionally, these totals may overflow (wrapping around) - particularly
 * overall_alloc.
 */
typedef struct mm_debug_total_s {
  mm_size_t overall_alloc ;
  int32     overall_obj ;
  mm_size_t highest_alloc ;
  int32     highest_obj ;
  mm_size_t current_alloc ;
  int32     current_obj ;
  mm_size_t highest_frag;
} mm_debug_total_t ;

void mm_debug_total_zero( mm_pool_t pool ) ;
void mm_debug_total_clear( mm_pool_t pool ) ;
void mm_debug_total_alloc( mm_pool_t pool, mm_size_t size ) ;
void mm_debug_total_free( mm_pool_t pool, mm_size_t size ) ;
void mm_debug_total_truncate( mm_pool_t pool, mm_size_t sizediff ) ;
void mm_debug_total_update( mm_pool_t pool ) ;
void mm_debug_total_allocfail( mm_pool_t pool );


/* Pools may also have cached allocations, using SACs. We want to be able to
 * examine the effectiveness of the SAC - to see if the frequency and
 * distribution specified on creation is suitable for that SACs actual usage.
 * We also want to monitor the number of allocation requests which are too
 * large to be cached - so we can adjust the class sizes if necessary.
 * This is done using a similar structure to above per allocation class
 * [size], with additional fields to record the SAC configuration.
 */

typedef struct mm_debug_sac_class_s {
  mm_size_t max_cache_size ;   /* Max size of objs to cache */
  mm_size_t max_cache_count ;  /* Max num of objs to cache */
  int32     overall_allocobj ; /* Total num of objs alloc'ed in this class */
  int32     highest_allocobj ; /* Highest num of objs alloc'ed at any one time */
  int32     current_allocobj ; /* Current num of objs alloc'ed */
  int32     highest_freeobj ;  /* Highest num of free (cached) objs at any one time */
  int32     current_freeobj ;  /* Currnet num of free (cached) objs */
  int32     cache_hit ;        /* Num of cached allocs (from previous alloc & free) */
} mm_debug_sac_class_t ;

typedef struct mm_debug_sac_total_s {
  int32                count ;                          /* Number of classes */
  mm_debug_sac_class_t classes[ MPS_SAC_CLASS_LIMIT ] ; /* Stats for each class */
  mm_size_t            large_alloc ;                    /* Largest oversized alloc */
  int32                large_obj ;                      /* Overall num of oversized objs */
} mm_debug_sac_total_t ;

void mm_debug_total_sac_init( mm_pool_t pool,
                              mm_sac_classes_t classes,
                              int32 count ) ;
void mm_debug_total_sac_zero( mm_pool_t pool ) ;
void mm_debug_total_sac_flush( mm_pool_t pool ) ;
void mm_debug_total_sac_alloc( mm_pool_t pool, mm_size_t size ) ;
void mm_debug_total_sac_free( mm_pool_t pool, mm_size_t size ) ;
void mm_debug_total_sac_report( mm_pool_t pool ) ;

#else /* MM_DEBUG_TOTAL */

#define mm_debug_total_zero( pool )               EMPTY_STATEMENT()
#define mm_debug_total_clear( pool )              EMPTY_STATEMENT()
#define mm_debug_total_alloc( pool, size )        EMPTY_STATEMENT()
#define mm_debug_total_free( pool, size  )        EMPTY_STATEMENT()
#define mm_debug_total_truncate( pool, sizediff ) EMPTY_STATEMENT()
#define mm_debug_total_update( pool )             EMPTY_STATEMENT()

#define mm_debug_total_sac_init( pool, classes, count ) EMPTY_STATEMENT()
#define mm_debug_total_sac_zero( pool )                 EMPTY_STATEMENT()
#define mm_debug_total_sac_flush( pool )                EMPTY_STATEMENT()
#define mm_debug_total_sac_alloc( pool, size )          EMPTY_STATEMENT()
#define mm_debug_total_sac_free( pool, size  )          EMPTY_STATEMENT()
#define mm_debug_total_sac_report( pool )               EMPTY_STATEMENT()

#endif /* MM_DEBUG_TOTAL */

#endif /* __MMTOTAL_H__ */


/* Log stripped */
