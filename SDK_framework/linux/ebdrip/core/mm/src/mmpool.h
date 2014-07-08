/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmpool.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The internal representation of memory management pools
 */

#ifndef __MMPOOL_H__
#define __MMPOOL_H__

#include "mm.h"
#include "mps.h"
#include "mpscepvm.h"
#include "mmtotal.h"            /* For mm_debug_total_t */


typedef int mm_pool_class_t; /* pool classes (from mm_pool_class_e) */

/* Pool classes (type mm_pool_class_t) are taken from this enum.  */
enum mm_pool_class_e {
  MM_POOL_EPDL = 0,
  MM_POOL_EPVM,
  MM_POOL_EPVM_DEBUG,
  MM_POOL_EPFN,
  MM_POOL_EPFN_DEBUG,
  MM_POOL_MV,
  MM_POOL_EPDR,
  MM_POOL_MVFF,

  MM_POOL_NUMCLASSES
} ;

#define isValidPoolClass( _pool ) (( _pool )->class >= 0 && \
                                   ( _pool )->class < MM_POOL_NUMCLASSES )


typedef struct mm_pool_fns_t mm_pool_fns_t ; /* fn dispatch table */

/* Each pool class may have its own set of core methods */

struct mm_pool_fns_t {
  mps_class_t ( MPS_CALL *class )( void ) ;
  size_t ( MPS_CALL *size )( mps_pool_t pool ) ;
  size_t ( MPS_CALL *freesize )( mps_pool_t pool ) ;
} ;


/* mm_pool_t is a wrapper around an MPS pool */

struct mm_pool_t {
  mps_pool_t        mps_pool ;  /* Underlying MPS pool */
  mm_pool_class_t   class ;     /* Pool class; selector for union & fntable */
  mm_pooltype_t     type ;      /* Pool type; mainly for debugging info */
  size_t segment_size; /* Segment size of the pool (smallest if multiple). */
  union {
    struct {
      mps_epvm_save_level_t save_level;
      mps_ap_t obj_ap ;
      mps_ap_t string_ap ;
    } epvm_pool ;
    struct {
      /* NB. The two save_level fields must be in the same position. */
      mps_epvm_save_level_t save_level;
      mps_ap_t exact_ap ;
      mps_ap_t weak_ap ;
    } epfn_pool ;
    struct {                    /* Promise info for the dl */
      mm_addr_t promise_base ;
      mm_addr_t promise_top ;
      mm_addr_t promise_next ;
    } dl_pool ;
  }                 the ;       /* Pool-specific info */
  mps_sac_t         sac ;       /* Any pool _may_ use cached allocations */
  mm_pool_fns_t    *fntable ;   /* Fn pointers for various pool methods */
  struct mm_pool_t *next ;      /* To iterate over list of pools */
  Bool mps_debug; /* Indicates using MPS fenceposts, not MM */
#ifdef MM_DEBUG_TOTAL
  mm_debug_total_t     totals ;    /* Debugging info - pool usgage */
  mm_debug_sac_total_t sac_stats ; /* Debugging info - sac usage */
#endif /* MM_DEBUG_TOTAL */
} ;


/** Returns the name for a pool type. */
char *get_pooltype_name(mm_pooltype_t type);


/** Rounds up an allocation size to the segment size of the pool.

  This is not guaranteed to be an accurate prediction of additional memory
  needed (after an allocation of this size has already failed), because this is
  not intended to constrain the cleverness of the pool code. (Some pools have
  two segment sizes already.) It's just better than not taking the segments into
  account at all.
 */
size_t at_least_a_segment(mm_pool_t pool, size_t size);


/*
* Log stripped */

#endif /* __MMPOOL_H__ */
