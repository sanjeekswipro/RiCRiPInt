/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmps.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011, 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript VM management with GC
 */

#include <float.h>
#include <stddef.h>
#include "core.h"
#include "mmps.h"
#include "gcscan.h"
#include "mm.h"
#include "mmpool.h"
#include "lowmem.h"
#include "apportioner.h" /* mm_context_t */
#include "mmlog.h"
#include "mmreserve.h"
#include "mmtotal.h"
#include "vm.h"
#include "mps.h"
#include "mpscepvm.h"
#include "objects.h"
#include "metrics.h"


/* Global variables defined in the interface */

mm_pool_t mm_pool_ps_local  = NULL ;     /* PostScript local VM */
mm_pool_t mm_pool_ps_global = NULL ;     /* PostScript global VM */
mm_pool_t mm_pool_ps = NULL ;            /* One of the above */

mm_pool_t mm_pool_ps_typed_local = NULL ;  /* PostScript local VM, typed */
mm_pool_t mm_pool_ps_typed_global = NULL ; /* PostScript global VM, typed */
mm_pool_t mm_pool_ps_typed = NULL ;        /* One of the above */


/** Current save level */
static mps_epvm_save_level_t save_level ;

/** Lowest save level since last GC */
static mps_epvm_save_level_t lowest_save_level_since_gc;


/* mm_format_ps -- the MPS format of the PS objects */
static mps_fmt_t mm_format_ps = NULL ;

/* mm_format_ps_typed -- the MPS format of the typed structures */
static mps_fmt_t mm_format_ps_typed = NULL ;


static double gc_threshold = DBL_MAX;

static Bool *gc_alert = NULL;

/** Total PS VM allocation since last GC. */
static double alloc_since_gc = 0.0;

/** New PS VM allocation on each save level since last GC. */
static size_t allocs_since_gc[MAXSAVELEVELS+1];


#define NOTREACHED              HQFAIL("Unreachable statement")
#define ADDR_ADD(p, s)          ((mps_addr_t)((char *)(p) + (s)))


/* ps_no_copy -- a dummy copy method for the format */
static void MPS_CALL ps_no_copy( mps_addr_t old, mps_addr_t new )
{
  UNUSED_PARAM( mps_addr_t, old ) ;
  UNUSED_PARAM( mps_addr_t, new ) ;
  NOTREACHED ;
}


/* ps_no_fwd -- a dummy forwarding method for the format */
static void MPS_CALL ps_no_fwd(mps_addr_t old, mps_addr_t new)
{
  UNUSED_PARAM( mps_addr_t, old ) ;
  UNUSED_PARAM( mps_addr_t, new ) ;
  NOTREACHED ;
}


/* ps_no_isfwd -- a dummy isfwd method for the format */
static mps_addr_t MPS_CALL ps_no_isfwd( mps_addr_t object )
{
  NOTREACHED;
  return object;
}


/* ps_no_pad -- a dummy pad method for the format */
static void MPS_CALL ps_no_pad( mps_addr_t addr, size_t size )
{
  UNUSED_PARAM( mps_addr_t, addr );
  UNUSED_PARAM( size_t, size ) ;
  NOTREACHED ;
}


/* ps_fixed_format -- the actual format object for PS VM */
static struct mps_fmt_fixed_s ps_fixed_format = {
  ( mps_align_t )MM_PS_ALIGNMENT,
  ps_scan, ps_no_fwd, ps_no_isfwd, ps_no_pad
} ;


/* ps_typed_format -- the actual format object for typed structs */
static struct mps_fmt_A_s ps_typed_format = {
  ( mps_align_t )MM_PS_TYPED_ALIGNMENT,
  ps_typed_scan, ps_typed_skip,
  ps_no_copy, ps_no_fwd, ps_no_isfwd, ps_no_pad
} ;



/* Allocation routine for epvm memory, as called by generic allocator */
static mps_res_t MPS_CALL epvm_alloc( mps_addr_t *p, void *args, size_t size
#ifdef MM_DEBUG_MPSTAG
                                    , mps_debug_info_s *dinfo
#endif
                             )
{
  mps_ap_t ap ;
  mps_res_t res ;

#ifdef MM_DEBUG_MPSTAG
  UNUSED_PARAM(mps_debug_info_s *, dinfo);
#endif
  ap = args ;
  do {
    res = mps_reserve( p, ap, size ) ;
    if ( res != MPS_RES_OK )
      return res ;
  } while ( ! mps_commit( ap, *p, size )) ;
  return MPS_RES_OK ;
}


#ifdef MM_DEBUG_SCRIBBLE

static struct TYPED_ILLEGAL_OBJECT
  { uint32 a ;
    uint32 b ;
  } illegalTypedObject = { 0xdeadbeef, 0xdeadbeef } ;

static mps_pool_debug_option_s freecheckTypedOptions =
  { NULL, 0, (void *)&illegalTypedObject, sizeof(struct TYPED_ILLEGAL_OBJECT) };

static /*const*/ OBJECT illegalObject =
  { { OINVALID, 0, 0 }, (void*)(uintptr_t)0xdeadbeef };
static mps_pool_debug_option_s freecheckOptions =
  { NULL, 0, (void *)&illegalObject, sizeof(OBJECT) };

#endif /* MM_DEBUG_SCRIBBLE */


/* ps_pool_create -- create an MM_POOL_PSVM pool */
static mps_res_t ps_pool_create( mm_pool_t *new_pool_output,
                                 mps_fmt_t format,
                                 mps_epvm_save_level_t maxsavelevel,
                                 mps_epvm_save_level_t minsavelevel )
{
  mm_pool_t pool ;
  mps_res_t res ;

  if ( mm_pool_create( &pool,
#ifdef MM_DEBUG_SCRIBBLE
                       PSVM_DEBUG_POOL_TYPE, &freecheckOptions,
#else
                       PSVM_POOL_TYPE,
#endif
                       format, maxsavelevel, minsavelevel )
       != MM_SUCCESS )
    return MPS_RES_FAIL ;
  pool->segment_size = 4 * 1024; /* Magic knowledge: It's page size! */

  res = mps_ap_create( &pool->the.epvm_pool.obj_ap, pool->mps_pool, TRUE ) ;
  if ( res == MPS_RES_OK ) {
    res = mps_ap_create( &pool->the.epvm_pool.string_ap, pool->mps_pool, FALSE ) ;
    if ( res == MPS_RES_OK ) {
      pool->the.epvm_pool.save_level = minsavelevel;
      *new_pool_output = pool ;
      return MPS_RES_OK ;
    }
    mps_ap_destroy( pool->the.epvm_pool.obj_ap ) ;
  }
  mm_pool_destroy( pool ) ;
  return res ;
}


/* ps_pool_destroy -- destroy an MM_POOL_PSVM pool */
static void ps_pool_destroy( mm_pool_t pool )
{
  HQASSERT( pool, "pool is NULL in ps_pool_destroy" ) ;
  HQASSERT( pool->type == PSVM_POOL_TYPE || pool->type == PSVM_DEBUG_POOL_TYPE,
            "Wrong type of pool" ) ;
  mps_ap_destroy( pool->the.epvm_pool.string_ap ) ;
  mps_ap_destroy( pool->the.epvm_pool.obj_ap ) ;
  mm_pool_destroy( pool ) ;
}


/* ps_typed_pool_create -- create an MM_POOL_PSVMFN pool */
static mps_res_t ps_typed_pool_create( mm_pool_t *new_pool_output,
                                       mps_fmt_t format,
                                       mps_epvm_save_level_t maxsavelevel,
                                       mps_epvm_save_level_t minsavelevel )
{
  mm_pool_t pool ;
  mps_res_t res ;

  if ( mm_pool_create( &pool,
#ifdef MM_DEBUG_SCRIBBLE
                       PSVMFN_DEBUG_POOL_TYPE, &freecheckTypedOptions,
#else
                       PSVMFN_POOL_TYPE,
#endif
                       format, maxsavelevel, minsavelevel )
       != MM_SUCCESS )
    return MPS_RES_FAIL ;

  res = mps_ap_create( &pool->the.epfn_pool.exact_ap, pool->mps_pool,
                       mps_rank_exact());
  if ( res == MPS_RES_OK ) {
    res = mps_ap_create( &pool->the.epfn_pool.weak_ap, pool->mps_pool,
                         mps_rank_weak());
    if ( res == MPS_RES_OK ) {
      *new_pool_output = pool;
      return MPS_RES_OK;
    }
    mps_ap_destroy( pool->the.epfn_pool.exact_ap );
  }
  mm_pool_destroy( pool ) ;
  return res ;
}


/* ps_typed_pool_destroy -- destroy an MM_POOL_PSVMFN pool */
static void ps_typed_pool_destroy( mm_pool_t pool )
{
  HQASSERT( pool, "pool is NULL in ps_typed_pool_destroy" ) ;
  HQASSERT( pool->type == PSVMFN_POOL_TYPE || pool->type == PSVMFN_DEBUG_POOL_TYPE,
            "Wrong type of pool" ) ;
  mps_ap_destroy( pool->the.epfn_pool.exact_ap );
  mps_ap_destroy( pool->the.epfn_pool.weak_ap );
  mm_pool_destroy( pool ) ;
}


/* mm_ps_alloc_obj -- allocate an object in PSVM */
mm_addr_t mm_ps_alloc_obj( mm_pool_t pool, size_t size )
{
  mps_addr_t p = NULL /* pacify compiler */;
  mps_res_t res;
  mps_ap_t obj_ap ;
  OBJECT *o ;
  OBJECT transfer ;
  corecontext_t *corecontext = get_core_context_interp() ;
  memory_requirement_t request;

  HQASSERT( pool != NULL, "Null pool (mm_ps_alloc_obj)" ) ;
  HQASSERT( pool->class == MM_POOL_EPVM || pool->class == MM_POOL_EPVM_DEBUG,
            "Wrong class of pool" ) ;
  HQASSERT( size != 0, "Zero-sized allocation (mm_ps_alloc_obj)" ) ;
  HQASSERT( SIZE_IS_ALIGNED( size, MM_PS_ALIGNMENT ), "Odd-sized allocation (mm_ps_alloc_obj)" ) ;

  obj_ap = pool->the.epvm_pool.obj_ap ;
  HQASSERT(corecontext != NULL, "No context for PS alloc");
  do { /* in practice, this will not loop */
    if ( !mm_memory_is_low
         || !mm_should_regain_reserves(corecontext->mm_context->default_cost) )
      res = mps_reserve( &p, obj_ap, size );
    else
      res = MPS_RES_FAIL;
    if ( res != MPS_RES_OK ) {
      request.pool = pool; request.size = size;
      request.cost = corecontext->mm_context->default_cost;
      if ( mm_low_mem_alloc(&p, corecontext, &request, &epvm_alloc, obj_ap
#ifdef MM_DEBUG_MPSTAG
                            , NULL
#endif
                            )
           != MPS_RES_OK ) {
        MM_LOG(( LOG_AO, "0x%08x 0x%08x 0x%08x",
                 ( uint32 )pool, ( uint32 )size, ( uint32 )p ));
        return NULL;
      }
      break;
    }
  } while ( !mps_commit( obj_ap, p, size ));

  alloc_since_gc += size;
  allocs_since_gc[pool->the.epvm_pool.save_level] += size;
  if ( alloc_since_gc > gc_threshold && gc_alert != NULL )
    *gc_alert = TRUE;

  mm_debug_total_alloc( pool, size ) ;
  MM_LOG(( LOG_AO, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )size, ( uint32 )p )) ;

  /* Set the slot flags for all of the objects just allocated to indicate
     this is a PSVM object, saved at the current savelevel, and the
     object in the slot is local. Since we're touching the slot contents,
     we might as well initialise the object tags to ONULL. */
  theMark(transfer) = (int8)(ISPSVM|ISLOCAL|GLMODE_SAVELEVEL(pool == mm_pool_ps_global, corecontext)) ;
  theTags(transfer) = ONULL | LITERAL ;
  theLen(transfer) = 0 ;

  for ( o = (OBJECT *)p ; size > 0 ; ++o, size -= sizeof(OBJECT) ) {
    OBJECT_SET_D0(*o,OBJECT_GET_D0(transfer)) ; /* Set slot properties */
    OBJECT_SCRIBBLE_D1(*o);
  }
  return ( mm_addr_t )p ;
}


/* An illegal context value, so the code can tell if it was set. */
#define CONTEXT_NOT_SET ((corecontext_t *)(intptr_t)1)


/* Generic PS allocator */
#ifdef MM_DEBUG_LOGGING
static inline mm_addr_t mm_ps_alloc_fn(mm_pool_t pool, mps_ap_t ap, size_t size,
                                       char *log)
#else
static inline mm_addr_t mm_ps_alloc_fn(mm_pool_t pool, mps_ap_t ap, size_t size)
#endif
{
  mps_addr_t p = NULL /* pacify compiler */;
  corecontext_t *context = CONTEXT_NOT_SET;
  memory_requirement_t request;

  HQASSERT( size != 0, "Zero-sized allocation" ) ;

  size = SIZE_ALIGN_UP( size, MM_PS_TYPED_ALIGNMENT );
  do { /* in practice, this will not loop */
    mps_res_t res = MPS_RES_FAIL;

    if ( reserves_allow_alloc(&context) ) {
      res = mps_reserve( &p, ap, size );
      if ( res != MPS_RES_OK && context == CONTEXT_NOT_SET )
        context = get_core_context_interp(); /* haven't fetched it; will need it */
    }
    if ( res != MPS_RES_OK ) {
      HQASSERT(context != NULL, "No context for PS alloc");
      request.pool = pool; request.size = size;
      request.cost = context->mm_context->default_cost;
      if ( mm_low_mem_alloc(&p, context, &request, &epvm_alloc, ap
#ifdef MM_DEBUG_MPSTAG
                            , NULL
#endif
                            )
           != MPS_RES_OK ) {
        MM_LOG(( log, "0x%08x 0x%08x 0x%08x",
                 ( uint32 )pool, ( uint32 )size, ( uint32 )p ));
        return NULL;
      }
      break;
    }
  } while ( !mps_commit( ap, p, size ));

  alloc_since_gc += size;
  allocs_since_gc[pool->the.epvm_pool.save_level] += size;
  if ( alloc_since_gc > gc_threshold && gc_alert != NULL )
    *gc_alert = TRUE;

  mm_debug_total_alloc( pool, size ) ;
  MM_LOG(( log, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )size, ( uint32 )p )) ;
  return (mm_addr_t)p;
}


#ifdef MM_DEBUG_LOGGING
#define mm_ps_alloc(pool, ap, size, log) mm_ps_alloc_fn(pool, ap, size, log)
#else
#define mm_ps_alloc(pool, ap, size, log) mm_ps_alloc_fn(pool, ap, size)
#endif


/* mm_ps_alloc_typed -- allocate a typed structure in PSVM */
mm_addr_t mm_ps_alloc_typed( mm_pool_t pool, size_t size )
{
  HQASSERT( pool != NULL, "Null pool" ) ;
  HQASSERT( pool->class == MM_POOL_EPFN || pool->class == MM_POOL_EPFN_DEBUG,
            "Wrong class of pool" ) ;

  return mm_ps_alloc(pool, pool->the.epfn_pool.exact_ap,
                     SIZE_ALIGN_UP(size, MM_PS_TYPED_ALIGNMENT), LOG_AT);
}


/* mm_ps_alloc_weak -- allocate a weak typed structure in PSVM */
mm_addr_t mm_ps_alloc_weak( mm_pool_t pool, size_t size )
{
  HQASSERT( pool != NULL, "Null pool" ) ;
  HQASSERT( pool->class == MM_POOL_EPFN || pool->class == MM_POOL_EPFN_DEBUG,
            "Wrong class of pool" ) ;

  return mm_ps_alloc(pool, pool->the.epfn_pool.weak_ap,
                     SIZE_ALIGN_UP(size, MM_PS_TYPED_ALIGNMENT), LOG_AW);
}


/* mm_ps_alloc_string -- allocate a string in PSVM */
mm_addr_t mm_ps_alloc_string( mm_pool_t pool, size_t size )
{
  HQASSERT( pool != NULL, "Null pool (mm_ps_alloc_string)" ) ;
  HQASSERT( pool->class == MM_POOL_EPVM || pool->class == MM_POOL_EPVM_DEBUG,
            "Wrong class of pool" ) ;

  return mm_ps_alloc(pool, pool->the.epvm_pool.string_ap,
                     SIZE_ALIGN_UP(size, MM_PS_ALIGNMENT), LOG_AS);
}


/* mm_ps_save -- do a PS save */
void mm_ps_save( size_t new_level )
{
  HQASSERT( new_level > MINSAVELEVEL &&
            new_level <= MAXSAVELEVELS, "Bad save level (mm_ps_save)" ) ;
  HQASSERT( new_level == save_level + 1, "save level wrong (mm_ps_save)" ) ;

  mps_epvm_save( mm_pool_ps_local->mps_pool ) ;
  mm_pool_ps_local->the.epvm_pool.save_level = new_level;
  mps_epvm_save( mm_pool_ps_typed_local->mps_pool ) ;
  mm_pool_ps_typed_local->the.epfn_pool.save_level = new_level;
  if ( new_level <= MAXGLOBALSAVELEVEL + 1 ) {
    mps_epvm_save( mm_pool_ps_global->mps_pool ) ;
    mm_pool_ps_global->the.epvm_pool.save_level = new_level;
    mps_epvm_save( mm_pool_ps_typed_global->mps_pool ) ;
    mm_pool_ps_typed_global->the.epfn_pool.save_level = new_level;
  }
  save_level = new_level ;
  MM_LOG(( LOG_PS, "0x%08x", new_level )) ;
}


/* mm_ps_restore -- do a PS restore to the given level */
void mm_ps_restore( size_t level )
{
  size_t i;

  HQASSERT( level + 1 > MINSAVELEVEL && level < MAXSAVELEVELS,
            "Bad save level (mm_ps_restore)" ) ;
  HQASSERT( level < save_level, "save level too high (mm_ps_restore)" ) ;

  mps_epvm_restore( mm_pool_ps_local->mps_pool,
                    ( mps_epvm_save_level_t )level ) ;
  mm_pool_ps_local->the.epvm_pool.save_level = level;
  mm_debug_total_update( mm_pool_ps_local ) ;
  mps_epvm_restore( mm_pool_ps_typed_local->mps_pool,
                    ( mps_epvm_save_level_t )level ) ;
  mm_pool_ps_typed_local->the.epfn_pool.save_level = level;
  mm_debug_total_update( mm_pool_ps_typed_local ) ;

  if ( level <= MAXGLOBALSAVELEVEL ) {
    mps_epvm_restore( mm_pool_ps_global->mps_pool,
                     ( mps_epvm_save_level_t )level ) ;
    mm_pool_ps_global->the.epvm_pool.save_level = level;
    mm_debug_total_update( mm_pool_ps_global ) ;
    mps_epvm_restore( mm_pool_ps_typed_global->mps_pool,
                     ( mps_epvm_save_level_t )level ) ;
    mm_pool_ps_typed_global->the.epfn_pool.save_level = level;
    mm_debug_total_update( mm_pool_ps_typed_global ) ;
  }

  for ( i = save_level ; i > level ; --i )
    allocs_since_gc[i] = 0;
  save_level = level ;
  if ( save_level < lowest_save_level_since_gc )
    lowest_save_level_since_gc = save_level;
  MM_LOG(( LOG_PR, "0x%08x", level )) ;
  mm_recheck_reserves();
}


/* mm_ps_check -- checks that the save level of a pointer is not too high
 *
 * Checks that 'what' does not point into PSVM allocated at a save level
 * higher than 'level'. Returns MM_SUCCESS if it does not (this includes
 * pointers which are not to PSVM at all, and NULL pointers).
 */
mm_result_t mm_ps_check( size_t level, mm_addr_t what )
{
  mps_pool_t pool ;
  mps_epvm_save_level_t real_level ;

  HQASSERT( level + 1 > MINSAVELEVEL && level <= MAXSAVELEVELS,
            "Bad save level (mm_ps_check)" ) ;
  HQASSERT( level <= save_level, "save level too high (mm_ps_check)" ) ;

  if ( what == NULL )
    return MM_SUCCESS ;
  if ( mps_epvm_check( &pool, &real_level, mm_arena, ( mps_addr_t )what ) &&
       real_level > ( mps_epvm_save_level_t )level )
    return MM_FAILURE ;
  return MM_SUCCESS ;
}


static size_t total_vm_size(void)
{
  return mm_pool_alloced_size(mm_pool_ps_global)
    + mm_pool_alloced_size(mm_pool_ps_typed_global)
    + mm_pool_alloced_size(mm_pool_ps_local)
    + mm_pool_alloced_size(mm_pool_ps_typed_local);
}


/* mm_set_gc_threshold -- set GC threshold and link to alert flag
 *
 * Sets the threshold to the given value, or nearest reasonable one, and
 * returns the value set, except -1.0 means default = no threshold.  Also
 * takes a pointer to the alert flag to be set when the threshold is
 * reached.  If the threshold has already been reached, sets the flag
 * immediately. */
double mm_set_gc_threshold( double threshold, Bool *alert )
{
  double lower_limit = (double)mps_arena_commit_limit( mm_arena ) / 4;

  gc_threshold = ( threshold == -1.0 ) ? DBL_MAX : max( threshold, lower_limit );
  gc_alert = alert;

  if ( alloc_since_gc > gc_threshold && gc_alert != NULL )
    *gc_alert = TRUE;

  return gc_threshold;
}


/* mm_gc_threshold_exceeded -- has GC threshold been exceeded? */
Bool mm_gc_threshold_exceeded( void )
{
  return ( alloc_since_gc > gc_threshold );
}


uint32 gc_count;


/* garbage_collect -- do a PS VM garbage collection
 *
 * The caller specifies which of local and global VM are collected. */
Bool garbage_collect(Bool do_local, Bool do_global )
{
  Bool res ;
  mps_message_t message ;
  mps_message_type_t messtype ;
  size_t i;
#ifdef ASSERT_BUILD
  size_t size_before = total_vm_size();
#endif

  HQASSERT( do_local || do_global, "Vacuous call to garbage_collect" );

  ++gc_count;
  if ( do_local && do_global )
    res = ( mps_arena_collect( mm_arena ) == MPS_RES_OK ) ;
  else
    if ( do_local )
      res = ( mps_epvm_collect( mm_pool_ps_local->mps_pool,
                                mm_pool_ps_typed_local->mps_pool )
              == MPS_RES_OK ) ;
    else
      res = ( mps_epvm_collect( mm_pool_ps_global->mps_pool,
                                mm_pool_ps_typed_global->mps_pool )
              == MPS_RES_OK ) ;

  if ( do_local ) {
    mm_debug_total_update( mm_pool_ps_local ) ;
    mm_debug_total_update( mm_pool_ps_typed_local ) ;
  }
  if ( do_global ) {
    mm_debug_total_update( mm_pool_ps_global ) ;
    mm_debug_total_update( mm_pool_ps_typed_global ) ;
  }
  mm_recheck_reserves();
  MM_LOG(( LOG_GC, "%d %d", (int)do_global, (int)do_local )) ;

  /* Run all pending finalizations */
  /* NOTE: Must do it now, because EPFN requires finalizations are run
   * before the next GC! */
  messtype = mps_message_type_finalization() ;
  while ( mps_message_get( &message, mm_arena, messtype )) {
    mps_addr_t obj ;

    mps_message_finalization_ref( &obj, mm_arena, message ) ;
    object_finalize ( (mm_addr_t)obj ) ;
    mps_message_discard( mm_arena, message ) ;
  }

  HQTRACE(debug_lowmemory, ("GC reclaimed %d", size_before - total_vm_size()));
  alloc_since_gc = 0.0;
  for ( i = 0 ; i <= save_level ; ++i )
    allocs_since_gc[i] = 0;
  lowest_save_level_since_gc = save_level;
  return res ;
}


int gc_mode = -2; /* @@@@ decide on the interface: context or fn? */


/** Disk tier to RAM tier approximate conversion ratio.

  Speed ratio is about 1e6, but assume ram 1.0 means about 100 memory
  accesses to rebuild the data.
 */
#define DISK_TO_RAM (1e6 / 100)


/** VM extension to disk tier approximate conversion ratio

  Disk tier 1.0 is 1 write + 1 read; VM extension will trash several times. */
#define EXTENSION_TO_DISK (10.0)


/** Solicit method of the GC low-memory handlers. */
static low_mem_offer_t *mm_gc_solicit(low_mem_handler_t *handler,
                                      corecontext_t *context,
                                      size_t count,
                                      memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  size_t total_vm, new_alloc_since_gc = 0, i;
  double cost_ratio;
#define GC_HYSTERESIS_THRESHOLD (100000)

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->between_operators || gc_mode <= -2 )
    return NULL;

  if ( alloc_since_gc < GC_HYSTERESIS_THRESHOLD )
    /* Not enough in VM has changed after last GC to be worth doing again. */
    return NULL;
  total_vm = total_vm_size(); /* @@@@ Don't count string segments in cost. */
  for ( i = 0 ; i <= save_level ; ++i )
    new_alloc_since_gc += allocs_since_gc[i];
  cost_ratio = 1e1 * (double)total_vm /* cost ~ total */
    /* Mainly will reclaim from new allocation after last GC, */
    / (new_alloc_since_gc
       /* plus pointer mutation slowly creates reclaimable data. */
       + 1e-4 * alloc_since_gc);
  /* @@@@ Should also track roots shrinking. */
  switch ( handler->tier ) {
  case memory_tier_ram: {
    offer.offer_cost = (float)cost_ratio;
    if ( offer.offer_cost > (float)DISK_TO_RAM )
      return NULL;
  } break;
  case memory_tier_disk: {
    offer.offer_cost = (float)(cost_ratio / DISK_TO_RAM);
    if ( offer.offer_cost > (float)EXTENSION_TO_DISK )
      return NULL;
  } break;
  case memory_tier_trash_vm: {
    /* This handler will only really matter if partial paint is blocked. No
       limit so we always try this before giving up. */
    offer.offer_cost = (float)(cost_ratio / (DISK_TO_RAM * EXTENSION_TO_DISK));
  } break;
  }

  offer.pool = NULL; /* @@@@ It's in the PS pools, so build a chain of them. */
  offer.offer_size = new_alloc_since_gc;
  /* offer.indivisible = TRUE; */ /* @@@@ maybe just GC is special-cased */
  offer.next = NULL;
  return &offer;
}


/** Release method of the GC low-memory handler. */
static Bool mm_gc_release(low_mem_handler_t *handler,
                          corecontext_t *context, low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  return garbage_collect(gc_mode != -1, TRUE);
}


/** The GC low-memory handler for the RAM tier. */
static low_mem_handler_t gc_handler_ram = {
  "Garbage collection (RAM)",
  memory_tier_ram, mm_gc_solicit, mm_gc_release, FALSE,
  0, FALSE };


/** The GC low-memory handler for the disk tier. */
static low_mem_handler_t gc_handler_disk = {
  "Garbage collection (disk)",
  memory_tier_disk, mm_gc_solicit, mm_gc_release, FALSE,
  0, FALSE };


/** The GC low-memory handler for the trash_VM tier. */
static low_mem_handler_t gc_handler_trash_vm = {
  "Garbage collection (trash_VM)",
  memory_tier_trash_vm, mm_gc_solicit, mm_gc_release, FALSE,
  0, FALSE };


#ifdef METRICS_BUILD

static Bool vm_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PSVM")) )
    return FALSE;
  SW_METRIC_INTEGER("gc_count", gc_count);
  sw_metrics_close_group(&metrics);
  sw_metrics_close_group(&metrics);
  return TRUE;
}


static void vm_metrics_reset(int reason)
{
  UNUSED_PARAM(int, reason);
  gc_count = 0;
}


static sw_metrics_callbacks vm_metrics_hook = {
  vm_metrics_update,
  vm_metrics_reset,
  NULL
};

#endif


/** Initialize the PS VM pools. */
Bool mm_ps_start(void)
{
  mps_fmt_t format_ps, format_ps_typed ;
  mm_pool_t pool_ps_global = NULL, pool_ps_local = NULL,
    pool_ps_typed_global = NULL, pool_ps_typed_local = NULL;

  HQASSERT( mm_arena, "mm_arena is NULL in mm_ps_start" );

  if ( mps_fmt_create_fixed( &format_ps, mm_arena, &ps_fixed_format )
       != MPS_RES_OK )
    goto failFormat;

  if ( mps_fmt_create_A( &format_ps_typed, mm_arena, &ps_typed_format )
       != MPS_RES_OK )
    goto failTypedFormat;

  if ( ps_pool_create( &pool_ps_global, format_ps,
                       MAXGLOBALSAVELEVEL + 1, MINSAVELEVEL )
       != MPS_RES_OK )
    goto failGlobal;

  if ( ps_pool_create( &pool_ps_local, format_ps,
                       MAXSAVELEVELS, MINSAVELEVEL )
       != MPS_RES_OK )
    goto failLocal;

  if ( ps_typed_pool_create( &pool_ps_typed_global, format_ps_typed,
                             MAXGLOBALSAVELEVEL + 1, MINSAVELEVEL )
       != MPS_RES_OK )
    goto failTypedGlobal;

  if ( ps_typed_pool_create( &pool_ps_typed_local, format_ps_typed,
                             MAXSAVELEVELS, MINSAVELEVEL )
       != MPS_RES_OK )
    goto failTypedLocal;

  if ( !low_mem_handler_register(&gc_handler_ram) )
    goto failRAMRegister;
  if ( !low_mem_handler_register(&gc_handler_disk) )
    goto failDiskRegister;
  if ( !low_mem_handler_register(&gc_handler_trash_vm) )
    goto failTrashRegister;

  mps_message_type_enable( mm_arena, mps_message_type_finalization());
#ifdef METRICS_BUILD
  sw_metrics_register(&vm_metrics_hook);
#endif
  mm_format_ps = format_ps;
  mm_format_ps_typed = format_ps_typed;
  mm_pool_ps_global = pool_ps_global;
  mm_pool_ps_local = pool_ps_local;
  mm_pool_ps_typed_global = pool_ps_typed_global;
  mm_pool_ps_typed_local = pool_ps_typed_local;
  save_level = MINSAVELEVEL;

  /* Initial memory allocation mode is set to be local VM. */
  mm_pool_ps = mm_pool_ps_local;
  mm_pool_ps_typed = mm_pool_ps_typed_local;

  return TRUE;

failTrashRegister:
  low_mem_handler_deregister(&gc_handler_disk);
failDiskRegister:
  low_mem_handler_deregister(&gc_handler_ram);
failRAMRegister:
  ps_typed_pool_destroy( pool_ps_typed_local );
failTypedLocal:
  ps_typed_pool_destroy( pool_ps_typed_global );
failTypedGlobal:
  ps_pool_destroy( pool_ps_local );
failLocal:
  ps_pool_destroy( pool_ps_global );
failGlobal:
  mps_fmt_destroy( format_ps_typed );
failTypedFormat:
  mps_fmt_destroy( format_ps );
failFormat:
  return FALSE;
}


/** Finish the PS VM pools. */
void mm_ps_finish( void )
{
  low_mem_handler_deregister(&gc_handler_trash_vm);
  low_mem_handler_deregister(&gc_handler_disk);
  low_mem_handler_deregister(&gc_handler_ram);
  if ( mm_pool_ps_typed_local != NULL )
  {
    ps_typed_pool_destroy( mm_pool_ps_typed_local );
    mm_pool_ps_typed_local = NULL;
  }
  if ( mm_pool_ps_typed_global != NULL )
  {
    ps_typed_pool_destroy( mm_pool_ps_typed_global );
    mm_pool_ps_typed_global = NULL;
  }
  if ( mm_pool_ps_local != NULL )
  {
    ps_pool_destroy( mm_pool_ps_local );
    mm_pool_ps_local = NULL;
  }
  if ( mm_pool_ps_global != NULL )
  {
    ps_pool_destroy( mm_pool_ps_global );
    mm_pool_ps_global = NULL;
  }
  if ( mm_format_ps_typed != NULL )
  {
    mps_fmt_destroy( mm_format_ps_typed );
    mm_format_ps_typed = NULL;
  }
  if ( mm_format_ps != NULL )
  {
    mps_fmt_destroy( mm_format_ps );
    mm_format_ps = NULL;
  }
}


void init_C_globals_mmps(void)
{
  size_t i;

  mm_pool_ps_local = NULL ;
  mm_pool_ps_global = NULL ;
  mm_pool_ps = NULL ;
  mm_pool_ps_typed_local = NULL ;
  mm_pool_ps_typed_global = NULL ;
  mm_pool_ps_typed = NULL ;

  save_level = lowest_save_level_since_gc = 0;

  mm_format_ps = NULL ;
  mm_format_ps_typed = NULL ;
  gc_threshold = DBL_MAX ;
  alloc_since_gc = 0.0 ;
  for ( i = 0 ; i <= MAXSAVELEVELS ; ++i )
    allocs_since_gc[i] = 0;
  gc_alert = NULL ;
#ifdef METRICS_BUILD
  vm_metrics_reset(SW_METRICS_RESET_BOOT);
#endif
}

/*
* Log stripped */
