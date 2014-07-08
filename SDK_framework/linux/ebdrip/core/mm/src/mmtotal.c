/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmtotal.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Collects info on MM pool usage (allocation and object totals)
 */

#include "core.h"
#include "mm.h"
#include "mmpool.h"
#include "mmtotal.h"
#include "monitor.h"
#include "hqmemset.h"

/*
 * If totals aren't compiled in then this isn't required either
 */
#ifdef MM_DEBUG_TOTAL


/* --- Exported routines --- */

/* Initialise totals, for a new pool */
void mm_debug_total_zero( mm_pool_t pool )
{
  HQASSERT( pool, "mm_debug_total_zero: pool is NULL" ) ;

  HqMemZero(&pool->totals, sizeof( mm_debug_total_t )) ;
}


/* A pool is being emptied, reset the current totals */
void mm_debug_total_clear( mm_pool_t pool )
{
  HQASSERT( pool, "mm_debug_total_clear: pool is NULL" ) ;

  pool->totals.current_alloc = 0 ;
  pool->totals.current_obj   = 0 ;
}

/* Update totals for a newly allocated object */
void mm_debug_total_alloc( mm_pool_t pool, mm_size_t size )
{
  HQASSERT( pool, "mm_debug_total_alloc: pool is NULL" ) ;

  pool->totals.overall_alloc += size ;
  pool->totals.overall_obj++ ;
  pool->totals.current_alloc += size ;
  pool->totals.current_obj++ ;
  if ( pool->totals.current_alloc > pool->totals.highest_alloc ) {
    pool->totals.highest_alloc = pool->totals.current_alloc ;
    pool->totals.highest_obj   = pool->totals.current_obj ;
  }
}

/* Update totals for a dead object */
void mm_debug_total_free( mm_pool_t pool, mm_size_t size )
{
  HQASSERT( pool, "mm_debug_total_free: pool is NULL" ) ;

  pool->totals.current_alloc -= size ;
  pool->totals.current_obj-- ;
}

/* Update totals for a truncated object */
void mm_debug_total_truncate( mm_pool_t pool, mm_size_t sizediff )
{
  HQASSERT( pool, "mm_debug_total_free: pool is NULL" ) ;

  pool->totals.current_alloc -= sizediff ;
}

/* For ps pools, we don't have a free method, so we can't track the
 * number of live objects properly
 */
void mm_debug_total_update( mm_pool_t pool )
{
  mm_size_t size ;

  HQASSERT( pool, "mm_debug_total_update: pool is NULL" ) ;

  size = mm_pool_size( pool ) - mm_pool_free_size( pool ) ;
  if ( size > pool->totals.highest_alloc ) {
    pool->totals.highest_alloc = size ;
    pool->totals.highest_obj = pool->totals.current_obj ;
  }
  pool->totals.current_alloc = size ;
  pool->totals.current_obj = 0 ;
}

/* Update fragmentation */
void mm_debug_total_allocfail( mm_pool_t pool )
{
  mm_size_t free_size = mm_pool_free_size(pool);
  if ( free_size > pool->totals.highest_frag )
    pool->totals.highest_frag = free_size;
}


/* Report the various totals */

void mm_debug_total_overall( mm_pool_t pool, mm_size_t *alloc, int32 *obj )
{
  HQASSERT( pool,  "mm_debug_total_overall: pool is NULL" ) ;
  HQASSERT( alloc, "mm_debug_total_overall: alloc is NULL" ) ;
  HQASSERT( obj,   "mm_debug_total_overall: size is NULL" ) ;

  *alloc = pool->totals.overall_alloc ;
  *obj   = pool->totals.overall_obj ;
}

void mm_debug_total_highest( mm_pool_t pool, mm_size_t *alloc, int32 *obj,
                             mm_size_t *frag )
{
  HQASSERT( pool,  "mm_debug_total_highest: pool is NULL" ) ;
  HQASSERT( alloc, "mm_debug_total_highest: alloc is NULL" ) ;
  HQASSERT( obj,   "mm_debug_total_highest: obj is NULL" ) ;
  HQASSERT( frag,  "mm_debug_total_highest: frag is NULL" );

  *alloc = pool->totals.highest_alloc ;
  *obj   = pool->totals.highest_obj ;
  *frag  = pool->totals.highest_frag;
}

void mm_debug_total_current( mm_pool_t pool, mm_size_t *alloc, int32 *obj )
{
  HQASSERT( pool,  "mm_debug_total_current: pool is NULL" ) ;
  HQASSERT( alloc, "mm_debug_total_current: alloc is NULL" ) ;
  HQASSERT( obj,   "mm_debug_total_current: size is NULL" ) ;

  *alloc = pool->totals.current_alloc ;
  *obj   = pool->totals.current_obj ;
}


/* SAC stats */

/* These are only approximate.  If the real statistics are needed, the
 * SAC mechanism itself should be extended. */


/* Initialise totals, for a new sac */
void mm_debug_total_sac_init( mm_pool_t pool,
                              mm_sac_classes_t classes,
                              int32 count )
{
  int32 idx ;

  HQASSERT( pool, "mm_debug_total_sac_init: pool is NULL" ) ;
  HQASSERT( classes, "mm_debug_total_sac_init: classes is NULL" ) ;
  HQASSERT( count > 0 && count <= MPS_SAC_CLASS_LIMIT,
            "mm_debug_total_sac_init: count is out of range" ) ;

  mm_debug_total_sac_zero( pool ) ;

  pool->sac_stats.count = count ;
  for ( idx = 0 ; idx < count ; idx++ ) {
    pool->sac_stats.classes[ idx ].max_cache_size  = classes[ idx ].block_size ;
    pool->sac_stats.classes[ idx ].max_cache_count = classes[ idx ].cached_count ;
  }
}


/* Clear the totals, when destroying an existing sac */
void mm_debug_total_sac_zero( mm_pool_t pool )
{
  HQASSERT( pool, "mm_debug_total_sac_zero: pool is NULL" ) ;

  HqMemZero(&pool->sac_stats, sizeof( mm_debug_sac_total_t ));
}


/* Flushing an sac just empties it of all remaining (unused) cached
 * allocations - this does not affect allocations in use
 */
void mm_debug_total_sac_flush( mm_pool_t pool )
{
  int32 class ;

  HQASSERT( pool, "mm_debug_total_sac_flush: pool is NULL" ) ;

  for ( class = 0 ; class < pool->sac_stats.count ; class++ )
    pool->sac_stats.classes[ class ].current_freeobj = 0 ;
}


/* Update sac stats for a newly allocated object */
void mm_debug_total_sac_alloc( mm_pool_t pool, mm_size_t size )
{
  int32 classidx ;
  mm_debug_sac_class_t *classptr ;

  HQASSERT( pool, "mm_debug_total_sac_alloc: pool is NULL" ) ;

  for ( classidx = 0 ; classidx < pool->sac_stats.count ; classidx++ ) {
    if ( size <= pool->sac_stats.classes[ classidx ].max_cache_size )
      break ;
  }

  /* over-large allocs don't get cached */
  if ( classidx == pool->sac_stats.count ) {
    pool->sac_stats.large_obj++ ;
    if ( size > pool->sac_stats.large_alloc )
      pool->sac_stats.large_alloc = size ;
    return ;
  }

  classptr = & pool->sac_stats.classes[ classidx ] ;

  classptr->overall_allocobj++ ;
  if ( ++classptr->current_allocobj > classptr->highest_allocobj )
    classptr->highest_allocobj = classptr->current_allocobj ;
  if ( classptr->current_freeobj ) {
    classptr->current_freeobj-- ;
    classptr->cache_hit++ ;
  } /* BUG: There's an else branch missing here */
}


/* Update sac stats for a dead object */
void mm_debug_total_sac_free( mm_pool_t pool, mm_size_t size )
{
  int32 classidx ;
  mm_debug_sac_class_t *classptr ;

  HQASSERT( pool, "mm_debug_total_sac_free: pool is NULL" ) ;

  for ( classidx = 0 ; classidx < pool->sac_stats.count ; classidx++ ) {
    if ( size <= pool->sac_stats.classes[ classidx ].max_cache_size )
      break ;
  }

  /* over-large allocs don't get cached */
  if ( classidx == pool->sac_stats.count )
    return ;

  classptr = & pool->sac_stats.classes[ classidx ] ;

  classptr->current_allocobj-- ;
  if ( classptr->current_freeobj < (int32)(classptr->max_cache_count) )
    classptr->current_freeobj++ ;
  /* BUG: There's an else branch missing here */
  if ( classptr->current_freeobj > classptr->highest_freeobj )
    classptr->highest_freeobj++ ;
}


/* Report sac stats */

void mm_debug_total_sac_report( mm_pool_t pool )
{
  int32 class ;

  HQASSERT( pool, "mm_debug_total_sac_report: pool is NULL" ) ;

  monitorf(( uint8 * )"Sac statistics for sac at 0x%08x in pool 0x%08x:\n",
           pool->sac, pool ) ;

  for ( class = 0 ; class < pool->sac_stats.count ; class++ ) {
    mm_debug_sac_class_t *classptr = & pool->sac_stats.classes[ class ] ;

    monitorf(( uint8 * )"  %5d bytes(%3d): %d allocs, %d hits, (%d/%d) allocs, (%d/%d) frees\n",
             classptr->max_cache_size,
             classptr->max_cache_count,
             classptr->overall_allocobj,
             classptr->cache_hit,
             classptr->current_allocobj,
             classptr->highest_allocobj,
             classptr->current_freeobj,
             classptr->highest_freeobj ) ;
  }

  monitorf(( uint8 * )"  %d over-sized allocations (largest %d bytes)\n",
           pool->sac_stats.large_obj,
           pool->sac_stats.large_alloc ) ;
}


#endif /* MM_DEBUG_TOTAL */


/* Log stripped */
