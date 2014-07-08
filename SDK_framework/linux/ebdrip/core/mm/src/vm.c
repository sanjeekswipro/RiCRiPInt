 /** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:vm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file implements the ScriptWorks MM Interface (see impl.h.mm).
 * Most of the code simply calls the appropriate part of the MPS
 * interface.  The code for promises and multiple allocations
 * (mm_dl_promise_* and mm_alloc_multi_*) is non-trivial.  Also, several
 * logging features and a sophisticated low-memory handler are attached.
 *
 * .assume.epdl.sub-block.free: This tag is referenced in this file
 * at the points where we assume that sub-blocks can be freed.
 */

#include <stdarg.h>

#include "core.h"
#include "vm.h"
#include "mm.h"
#include "mmcommon.h"
#include "mm_core.h"
#include "mmps.h"
#include "mmreserve.h"
#include "mmpool.h"
#include "mmtag.h"
#include "mmlog.h"
#include "mmfence.h"
#include "apportioner.h"
#include "coreinit.h"
#include "swstart.h"
#include "swtrace.h"
#include "hqmemset.h"
#include "hqspin.h"
#include "mlock.h"

#include "mps.h"
#include "mpscmv.h"
#include "mpscmvff.h"
#include "mpscepdl.h"
#include "mpscepvm.h"
#include "mmwatch.h"
#include "monitor.h"

#ifdef FAIL_AFTER_N_ALLOCS
#include <stdlib.h> /* atol */

unsigned long n_alloc_calls = 1;
unsigned long fail_after_n = 0;
#endif


#if defined( ASSERT_BUILD )
Bool debug_lowmemory = FALSE ;
#endif


/* == Location Debugging == */

#if defined(MM_DEBUG_TAG) || defined(MM_DEBUG_LOGGING)
#define MM_DEBUG_LOGGING_AND_TAGS_USE_PARAM(t,p)        /* nothing */
#else
#define MM_DEBUG_LOGGING_AND_TAGS_USE_PARAM(t,p)        UNUSED_PARAM(t,p)
#endif

static void mm_location_init(void);


/* == Scribble debugging == */


#ifdef MM_DEBUG_SCRIBBLE

static void mm_debug_scribble( mm_addr_t ptr, size_t bytes )
{
  uint8 *dst ;

  dst = ( uint8 * )ptr ;
#ifdef MM_NONALIGNED_PTRS
  switch (( uint32 )dst & 0x03 ) { /* Align ptr */
  case 0x1: *dst++ = ( uint8 )0x65 ; /* e */ bytes-- ;
  case 0x2: *dst++ = ( uint8 )0x61 ; /* a */ bytes-- ;
  case 0x3: *dst++ = ( uint8 )0x64 ; /* d */ bytes-- ;
  }
#else
  HQASSERT((( uintptr_t )dst & 0x03 ) == 0,
           "ptr isn't aligned to 4 byte boundary in mm_debug_scribble" ) ;
#endif /* MM_NONALIGNED_PTRS */
  if ( bytes >> 2 ) {
#ifdef highbytefirst
    HqMemSet32((uint32 *)dst, (uint32)0x64656164, bytes >> 2); /* dead */
#else
    HqMemSet32((uint32 *)dst, (uint32)0x64616564, bytes >> 2); /* dead */
#endif /* highbytefirst */
    dst   += (( bytes >> 2 ) << 2 ) ;
    bytes -= (( bytes >> 2 ) << 2 ) ;
  }
  switch ( bytes ) { /* Use up remaining bytes */
  case 3: dst[ 2 ] = ( uint8 )0x61 ; /* a */
  case 2: dst[ 1 ] = ( uint8 )0x65 ; /* e */
  case 1: dst[ 0 ] = ( uint8 )0x64 ; /* d */
  }
}

#else

#define mm_debug_scribble( ptr, bytes )  EMPTY_STATEMENT()

#endif /* MM_DEBUG_SCRIBBLE */


/* == MPS telemetry == */

mps_word_t alloc_class_label[MM_ALLOC_CLASS_LIMIT];


/* == Pool configuration == */

/* The outside world knows only about the pool types listed in mm_pooltype_e.
 * Here, we decide what underlying mm pool class that each pool type will
 * use. This table also contains various pool parameters that are specific
 * to the different pool classes so that appropriate behaviour is achieved
 * for currently selected pool class.
 * To experiment with different pool classes for any given core rip pool,
 * only the first entry (poolclass) of the below table need be modifed.
 */
typedef struct {
  char           *name ;
  mm_pool_class_t poolclass ;
  size_t          maxpoolsize ; /* For mv (soft limit) */
  mps_bool_t      slothigh ;    /* For mvff */
  mps_bool_t      arenahigh ;   /* For mvff */
  mps_bool_t      firstfit ;    /* For mvff */
  Bool            mps_debug;
  mps_word_t      sym ;         /* For mps telemetry */
} mm_pooltype_map_t ;

/* To emulate EPDL or EPDR-like behaviour using mvff */
#define EPDL_LIKE (mps_bool_t)0, (mps_bool_t)0, (mps_bool_t)1
#define EPDR_LIKE (mps_bool_t)1, (mps_bool_t)1, (mps_bool_t)1
/* For mv */
#define MAXPOOLSIZE ((size_t)(2048u * 1024u * 1024u))
/* For epvm */
#define NO_TABLE_PARAMS (size_t)-1, (mps_bool_t)0, (mps_bool_t)0, (mps_bool_t)0

/* For the DL pools, it's not quite as simple to switch pool types as
 * with other pools (which just involve changing the appropriate entry
 * in the table below) because of the extra functionality they have to
 * support (i.e. left->right grow, promises, mm_pool_clear) so currently
 * the only alternatives is MVFF. */


mm_pooltype_map_t mm_pooltype_map[ NUM_POOL_TYPES ] = {
  {     "DL_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  {"DL_FAST_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, TRUE, 0 },
  {   "TEMP_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {  "COLOR_POOL_TYPE", MM_POOL_MV,   256 * 1024,  EPDR_LIKE, FALSE, 0 },
  {    "COC_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {  "TABLE_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  {  "PCL_POOL_TYPE"  , MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  {  "PCLXL_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  {  "PSVM_POOL_TYPE",  MM_POOL_EPVM, NO_TABLE_PARAMS, FALSE, 0 },
  {  "PSVM_DEBUG_POOL_TYPE", MM_POOL_EPVM_DEBUG, NO_TABLE_PARAMS, FALSE, 0 },
  {"PSVMFN_POOL_TYPE",  MM_POOL_EPFN, NO_TABLE_PARAMS, FALSE, 0 },
  {"PSVMFN_DEBUG_POOL_TYPE", MM_POOL_EPFN_DEBUG, NO_TABLE_PARAMS, FALSE, 0 },
  {    "PDF_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {    "IRR_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  {   "TRAP_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  { "IMBFIX_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  { "IMBVAR_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {    "RSD_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {   "TIFF_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {"SHADING_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {"XML_PARSE_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {"XML_SUBSYSTEM_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {   "BAND_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  {"BDSTATE_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDL_LIKE, FALSE, 0 },
  { "BDDATA_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  { "RLE_POOL_TYPE",    MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
  { "HTFORM_POOL_TYPE", MM_POOL_MVFF, MAXPOOLSIZE, EPDR_LIKE, FALSE, 0 },
} ;


char *get_pooltype_name( mm_pooltype_t type )
{
  if ( type > NUM_POOL_TYPES )
    return NULL ;
  return mm_pooltype_map[ type ].name ;
}


/* Global variables defined in the interface. see impl.h.swmm.mm */

static mm_pool_t mm_pool_list = NULL ; /* To iterate over all pools */

mm_pool_t mm_pool_fixed = NULL; /* 'RIP lifetime' memory */
mm_pool_t mm_pool_temp  = NULL; /* 'temporary' memory */
mm_pool_t mm_pool_color = NULL; /* 'color' memory */
mm_pool_t mm_pool_coc   = NULL; /* 'color cache' memory */

/* handle on mps */
mps_arena_t mm_arena = NULL ;


/* == Pool creation == */


/** A spinlock counter for access to \c mm_pool_list. */
hq_atomic_counter_t mm_pool_list_lock;


typedef struct {
  mm_pooltype_t type;
  mm_pool_class_t poolclass;
  size_t segsize;
  size_t avgsize;
  size_t alignment;
  va_list params;
} POOL_CREATE_DATA;


static mps_res_t do_pool_create( mps_addr_t *pool, void *args, size_t size
#ifdef MM_DEBUG_MPSTAG
                                 , mps_debug_info_s *dinfo
#endif
                                 )
{
  mps_res_t       mps_res = MPS_RES_OK ;
  mps_pool_t      mps_pool = NULL ;
  mm_pooltype_t   type ;
  mm_pool_class_t poolclass ;
  size_t       segsize ;
  size_t       avgsize ;
  size_t       alignment ;
  va_list         params ;
  POOL_CREATE_DATA *poolCreateData = args ;

  UNUSED_PARAM(size_t, size) ;
#ifdef MM_DEBUG_MPSTAG
  UNUSED_PARAM(mps_debug_info_s *, dinfo);
#endif
  type = poolCreateData->type;
  poolclass = poolCreateData->poolclass;
  segsize = poolCreateData->segsize;
  avgsize = poolCreateData->avgsize;
  alignment = poolCreateData->alignment;
  va_copy( params, poolCreateData->params );

  HQASSERT( type < NUM_POOL_TYPES, "pool type is invalid in mm_pool_create" ) ;

  /* Select pool parameters appropriate to the current pool class */
  switch ( poolclass ) {
  case MM_POOL_EPDL:
    /* Use segsize, avgsize, alignment from params */
    mps_res = mps_pool_create( &mps_pool, mm_arena, mps_class_epdl(),
                               segsize, avgsize, alignment ) ;
    break ;

  case MM_POOL_EPDR:
    /* Use segsize, avgsize, alignment from params */
    mps_res = mps_pool_create( &mps_pool, mm_arena, mps_class_epdr(),
                               segsize, avgsize, alignment ) ;
    break ;

  case MM_POOL_MV:
    /* Use segsize, avgsize from params; maxpoolsize from table.
     * Alignment is fixed at 8.  */
    mps_res = mps_pool_create( &mps_pool, mm_arena, mps_class_mv(),
                               segsize, avgsize,
                               mm_pooltype_map[ type ].maxpoolsize ) ;
    break ;

  case MM_POOL_MVFF:
    /* Use segsize, avgsize, alignment from params; mvff-specific
     * params (slothigh, arenahigh, firstfit) from table.  */
    mps_res = mps_pool_create( &mps_pool, mm_arena, mps_class_mvff(),
                               segsize, avgsize, alignment,
                               mm_pooltype_map[ type ].slothigh,
                               mm_pooltype_map[ type ].arenahigh,
                               mm_pooltype_map[ type ].firstfit ) ;
    break ;

  case MM_POOL_EPVM:
    mps_res = mps_pool_create_v( &mps_pool, mm_arena, mps_class_epvm(),
                                 params ) ;
    break ;

  case MM_POOL_EPVM_DEBUG:
    mps_res = mps_pool_create_v( &mps_pool, mm_arena, mps_class_epvm_debug(),
                                 params ) ;
    break ;

  case MM_POOL_EPFN:
    mps_res = mps_pool_create_v( &mps_pool, mm_arena, mps_class_epfn(),
                                 params ) ;
    break ;

  case MM_POOL_EPFN_DEBUG:
    mps_res = mps_pool_create_v( &mps_pool, mm_arena, mps_class_epfn_debug(),
                                 params ) ;
    break ;

  default:
    HQFAIL( "Unknown pool class in mm_pool_create" ) ;
  }

  va_end( params ) ;

  *pool = mps_pool ;

  return mps_res ;
}


mm_result_t mm_pool_create( mm_pool_t *pool, mm_pooltype_t type, ... )
{
  mps_res_t       mps_res = MPS_RES_OK ;
  mps_pool_t      mps_pool = NULL ;
  mm_pool_class_t poolclass ;
  va_list         params ;
  size_t segsize = 0;

  HQASSERT( pool, "pool NULL in mm_pool_create" ) ;
  HQASSERT( type < NUM_POOL_TYPES, "pool type is invalid in mm_pool_create" ) ;
  HQASSERT( mm_arena, "mm_arena is NULL in mm_pool_create" ) ;

  /* Get mm pool class for this pool type */
  poolclass = mm_pooltype_map[ type ].poolclass ;

  /* Create the mps pool. */
  {
    size_t avgsize = 0, alignment = 0;
    POOL_CREATE_DATA poolCreateData ;
    mps_res_t res = MPS_RES_FAIL;
    mps_addr_t p = NULL ;
#ifdef MM_DEBUG_MPSTAG
    mps_debug_info_s dinfo; /* not used, since pool_create doesn't support it */
#endif
    memory_requirement_t request;

    va_start( params, type ) ;

    /* For non-EPVM, we need to extract common pool parameters from our
     * parameter list.  */
    if ( poolclass != MM_POOL_EPVM && poolclass != MM_POOL_EPVM_DEBUG
         && poolclass != MM_POOL_EPFN && poolclass != MM_POOL_EPFN_DEBUG ) {
      segsize   = va_arg( params, size_t );
      avgsize   = va_arg( params, size_t );
      alignment = va_arg( params, size_t );
    }

    poolCreateData.type = type;
    poolCreateData.poolclass = poolclass;
    poolCreateData.segsize = segsize;
    poolCreateData.avgsize = avgsize;
    poolCreateData.alignment = alignment;
    va_copy( poolCreateData.params, params );

    if ( !mm_should_regain_reserves(mm_cost_normal) )
      res = do_pool_create( &p, &poolCreateData, 0 MPSTAG_ARG );
    if ( res != MPS_RES_OK ) {
      request.pool = NULL; request.size = 200 /* approx */;
      request.cost = mm_cost_normal;
      res = mm_low_mem_alloc( &p, &CoreContext, &request,
                              &do_pool_create, &poolCreateData MPSTAG_ARG );
    }
    if ( res != MPS_RES_OK ) {
      va_end( poolCreateData.params ) ;
      va_end( params ) ;
      return MM_FAILURE ;
    }
    mps_pool = p ;

    va_end( poolCreateData.params ) ;
    va_end( params ) ;
  }

  if ( mps_res == MPS_RES_OK ) {
    static struct mm_pool_t fixed_pool;
    void *v ;

    /* Allocate memory for the pool structure from the fixed pool - or
       if creating the fixed pool, use the static above. */
    HQASSERT( mm_pool_fixed != NULL || pool == &mm_pool_fixed,
              "Creating non-fixed pool before fixed pool exists in mm_pool_create" ) ;
    if ( mm_pool_fixed != NULL ) {
      v = mm_alloc_cost( mm_pool_fixed, sizeof(struct mm_pool_t),
                         mm_cost_normal, MM_ALLOC_CLASS_MM );
      if ( v == NULL ) {
        mps_pool_destroy( mps_pool );
        return MM_FAILURE;
      }
      *pool = v;
    } else
      *pool = &fixed_pool;

    /* Now initialise the pool */
    HqMemZero(*pool, sizeof( struct mm_pool_t ));
    ( *pool )->mps_pool = mps_pool ;
    ( *pool )->class    = poolclass ;
    ( *pool )->type     = type ;
    ( *pool )->segment_size = segsize;
    ( *pool )->mps_debug = mm_pooltype_map[type].mps_debug;
    mm_debug_total_zero( *pool ) ;

    /* Link in to global pool list */
    spinlock_counter(&mm_pool_list_lock, 1);
    ( *pool )->next = mm_pool_list ;
    mm_pool_list    = *pool ;
    spinunlock_counter(&mm_pool_list_lock);

    if ( mm_pooltype_map[ type ].sym == 0 ) {
      mm_pooltype_map[ type ].sym =
        mps_telemetry_intern( mm_pooltype_map[ type ].name ) ;
    }
    mps_telemetry_label(( mps_addr_t )mps_pool, mm_pooltype_map[ type ].sym ) ;

    MM_LOG(( LOG_PC, "0x%08x %d %d",
             ( uint32 )*pool,
             ( uint32 )poolclass,
             ( uint32 )sizeof( struct mm_pool_t ))) ;
    return MM_SUCCESS ;
  }
  return MM_FAILURE ;
}


void mm_pool_destroy( mm_pool_t pool )
{
  mps_pool_t mps_pool;
  mm_pool_t *poolptr ;

  HQASSERT( pool, "pool is NULL in mm_pool_destroy" ) ;
  HQASSERT( mm_pool_fixed, "fixed pool is NULL in mm_pool_destroy" ) ;

#ifdef MM_DEBUG_FENCEPOST_LITE
  mm_debug_check_fenceposts() ;
#endif
  mm_debug_tag_free_pool( pool ) ;

  /* Remove pool from global list */
  spinlock_counter(&mm_pool_list_lock, 1);
  for ( poolptr = &mm_pool_list ; *poolptr ; poolptr = &( *poolptr )->next ) {
    if ( *poolptr == pool ) {
      *poolptr = pool->next ;
      break ;
    }
  }
  spinunlock_counter(&mm_pool_list_lock);

  mps_pool = pool->mps_pool;
  if ( pool != mm_pool_fixed )
    mm_free( mm_pool_fixed, (mm_addr_t)pool, sizeof(struct mm_pool_t) );
  mps_pool_destroy( mps_pool );

  MM_LOG(( LOG_PD, "0x%08x %d %d",
           ( uint32 )pool,
           ( uint32 )pool->class,
           sizeof( struct mm_pool_t ))) ;
  mm_recheck_reserves();
}

/* mm_pool_walk
 *   - iterates over every pool that is currently active, applying poolfn
 *     to that pool.
 *   - returns TRUE iff poolfn returns TRUE for all pools - ie if one or
 *     more calls to poolfn return FALSE, then mm_pool_walk returns FALSE
 *
 * Note that it is not guaranteed safe to call mm_pool_walk with
 * mm_pool_destroy as the pool fn, since mm_pool_fixed is used to allocate
 * memory for mm_pool_t structures for every (other) pool and must
 * therefore be the last pool destroyed (and would have to be the last
 * pool in the list - this is currently the case, but may change).
 */
int32 mm_pool_walk( mm_pool_fn poolfn )
{
  mm_pool_t poolptr ;
  int32 res = TRUE ;

  HQASSERT( poolfn, "poolfn is NULL in mm_pool_walk" ) ;

  spinlock_counter(&mm_pool_list_lock, 1);
  for ( poolptr = mm_pool_list ; poolptr ; poolptr = poolptr->next ) {
    res = ( *poolfn )( poolptr ) && res ;
  }
  spinunlock_counter(&mm_pool_list_lock);
  return res ;
}

#define ONE_MEG (1024.0*1024.0)

/*
 * Print out pool usage information.
 *
 * Useful debug function to analyse where all the memory is being used.
 */
void mm_print_pool_usage(char *title)
{
  mm_pool_t poolptr;
  long i, j;
  struct {
    size_t size, free, npools;
    char *name;
  } mem[NUM_POOL_TYPES];
  size_t t_size = 0, t_free = 0;

  for ( i = 0; i < NUM_POOL_TYPES; i++ ) {
    mem[i].size = mem[i].free = mem[i].npools = 0;
    mem[i].name = mm_pooltype_map[i].name;
  }
  spinlock_counter(&mm_pool_list_lock, 1);
  for ( poolptr = mm_pool_list ; poolptr ; poolptr = poolptr->next ) {
    mm_pooltype_t t = poolptr->type;

    if ( t >= 0 && t < NUM_POOL_TYPES ) {
      size_t sz = mm_pool_size(poolptr);
      size_t ff = mm_pool_free_size(poolptr);
      mem[t].size += sz; t_size += sz;
      mem[t].free += ff; t_free += ff;
      mem[t].npools++;
    }
  }
  spinunlock_counter(&mm_pool_list_lock);

  for ( i = 0; i < NUM_POOL_TYPES; i++ ) {
    for ( j = i+1; j < NUM_POOL_TYPES; j++ ) {
      if ( mem[i].size < mem[j].size ) {
        size_t swp_a, swp_b, swp_c;
        char *swp_n;
        swp_a = mem[i].size;
        swp_b = mem[i].free;
        swp_c = mem[i].npools;
        swp_n = mem[i].name;
        mem[i].size = mem[j].size;
        mem[i].free = mem[j].free;
        mem[i].npools = mem[j].npools;
        mem[i].name = mem[j].name;
        mem[j].size = swp_a;
        mem[j].free = swp_b;
        mem[j].npools = swp_c;
        mem[j].name = swp_n;
      }
    }
  }

  monitorf((uint8 *)"---- pool states : %s ----\n", title);
  monitorf((uint8 *)"  Total alloc: %.2f Mb, free: %.2f Mb\n",
      t_size/ONE_MEG, t_free/ONE_MEG);
  monitorf((uint8 *)"  Available bytes %.2f Mb (%.2f Mb with reserve)\n",
    mm_no_pool_size(0)/ONE_MEG, mm_no_pool_size(1)/ONE_MEG);
  for ( i = 0; i < NUM_POOL_TYPES; i++ ) {
    if ( mem[i].size > 65536 ) {
      monitorf((uint8 *)"  %s: %.1f Mb, %.1f%% free\n", mem[i].name,
          mem[i].size/ONE_MEG, mem[i].free*100.0/mem[i].size);
    }
  }
  monitorf((uint8 *)"------------------------------\n");
}


mm_result_t mm_ap_create(mps_ap_t *ap, mm_pool_t pool)
{
  return mps_ap_create(ap, pool->mps_pool) == MPS_RES_OK
    ? MM_SUCCESS : MM_FAILURE;
}


void mm_ap_destroy(mps_ap_t ap, mm_pool_t pool)
{
  UNUSED_PARAM(mm_pool_t, pool);
  mps_ap_destroy(ap);
}


/* == Initialization == */

Bool is_low_mem_configuration = FALSE;

Bool low_mem_configuration(void)
{
  return is_low_mem_configuration;
}

static mm_result_t mm_init_fail( void ) ;
static void mm_destroypools(Bool abort);

/* mm_init - initializes common core pools and sets memory reserve limits
 *
 * Returns MM_FAILURE if insufficient memory was passed, or too much was
 * requested.
 */
mm_result_t mm_init( mps_arena_t arena,
                     size_t addr_space_size,
                     size_t working_size,
                     size_t extension_size,
                     int32     useallmem )
{
  mm_result_t res ;
  size_t i;

  HQASSERT( mm_arena == NULL, "mm_arena is non-NULL in mm_init" ) ;
  HQASSERT( addr_space_size > 0, "addr_space_size param to mm_init was = 0" );
  HQASSERT( working_size > 0, "working_size param to mm_init was = 0" );
  /* Quick integrity check on mm_pooltype_map table */
  HQASSERT( sizeof( mm_pooltype_map ) / sizeof( mm_pooltype_map_t ) ==
              NUM_POOL_TYPES,
            "mm_init: mm_pooltype_map table is out out sync with mm_pooltype_e" ) ;

  /* If we start-up with less that 65Mb of RIP memory, then that counts as 
     as a low-memory system. */
  if ( working_size < 65*1024*1024 )
    is_low_mem_configuration = TRUE;

  if ( mm_arena != NULL )
    return FALSE; /* init sequence confusion in the skin */
  mm_arena = arena;

#if defined(FAIL_AFTER_N_ALLOCS)
  { /* This setting should survive a reboot, hence the static flag. */
    static Bool get_setting = TRUE;

    if ( get_setting ) {
      long setting;
      char *e = getenv("fail_after_n");

      get_setting = FALSE;
      if ( e != NULL && (setting = atol(e)) > 0 )
        fail_after_n = (unsigned long)setting;
    }
  }
#endif /* FAIL_AFTER_N_ALLOCS */

  MM_LOG_INIT() ;

  /* Init allocation class names */
  i = 0;
#define MM_ALLOC_CLASS( name ) \
  alloc_class_label[ i++ ] = mps_telemetry_intern( #name );
#ifndef DOXYGEN_SKIP
#include "mm_class.h"
#endif /* !DOXYGEN_SKIP */
#undef MM_ALLOC_CLASS

  mm_location_init();

  apportioner_swinit(); /* Do before reserve registers handlers */

  /* Start by creating the fixed (must be done first) and temp pools */
  res = mm_pool_create( & mm_pool_fixed, TEMP_POOL_TYPE, TEMP_POOL_PARAMS ) ;
  if ( res != MM_SUCCESS )
    return MM_FAILURE;
  res = mm_pool_create( & mm_pool_temp, TEMP_POOL_TYPE, TEMP_POOL_PARAMS ) ;
  if ( res != MM_SUCCESS )
    return mm_init_fail();

  if ( !mm_reserve_create() )
    return mm_init_fail();
  if ( !mm_extension_init(addr_space_size, working_size, extension_size,
                          useallmem) )
    return mm_init_fail();

  res = mm_pool_create( & mm_pool_color, COLOR_POOL_TYPE, COLOR_POOL_PARAMS ) ;
  if ( res == MM_SUCCESS ) {
    /* The color sac classes (heuristics, based on my observations)
     * This totals up to 57856 bytes, which leaves us some spare for larger
     * allocs and overflows which aren't worth cacheing
     */
    struct mm_sac_classes_t sac_classes[] = { /* size, num, freq */
      {   64, 16,  1 },
      {   96, 64, 32 },
      {  112, 64, 64 },
      {  128, 64, 32 },
      {  192, 32, 16 },
      {  256, 12,  2 },
      {  512, 12,  8 },
      { 1664, 12,  4 }} ;
    res = mm_sac_create( mm_pool_color,
                         sac_classes,
                         sizeof( sac_classes ) / sizeof( sac_classes[ 0 ] )) ;
  }
  if ( res != MM_SUCCESS )
    return mm_init_fail() ;

  /* Pool for the colorcache */
  res = mm_pool_create( & mm_pool_coc, COC_POOL_TYPE, COC_POOL_PARAMS ) ;
  if ( res != MM_SUCCESS )
    return mm_init_fail() ;

  if ( !mm_ps_start() )
    return mm_init_fail() ;

  mm_tag_init() ;
  mm_watch_init();

  MM_LOG(( LOG_IS, "0x%08x 0x%08x 0x%08x 0x%08x",
           ( uint32 )mm_pool_fixed,
           ( uint32 )mm_pool_temp,
           ( uint32 )mm_pool_ps_local,
           ( uint32 )mm_pool_ps_global )) ;
  return MM_SUCCESS ;
}


static mm_result_t mm_init_fail( void )
{
  mm_extension_finish();
  mm_reserve_destroy();
  mm_destroypools(TRUE);

  MM_LOG(( LOG_IF, "0x%08x 0x%08x 0x%08x 0x%08x",
           ( uint32 )mm_pool_fixed,
           ( uint32 )mm_pool_temp,
           ( uint32 )mm_pool_ps_local,
           ( uint32 )mm_pool_ps_global )) ;

  mm_arena = NULL ;
  return MM_FAILURE ;
}


/* Called when the RIP is about to exit.                                */
void mm_finish( Bool abort )
{
  mm_watch_finish() ;
  mm_tag_finish() ;
  mm_extension_finish();
  mm_reserve_destroy();
  mm_ps_finish();
  apportioner_finish(); /* do this late, in case of low memory */
  mm_destroypools( abort );

  MM_LOG(( LOG_QU, "" )) ;
  MM_LOG_FINISH() ;

  mm_arena = NULL ;
}


/* mm_destroypools -- destroys pools
 *
 * In a normal shutdown, either after some error during init or as part
 * of mm_finish, destroys the static pools.  In an abort, destroys all
 * pools.  */
static void mm_destroypools( Bool abort )
{
  mm_pool_t pool;

  /* Get rid of the hard-coded pools in the reverse order they were created,
   * if indeed they have been created.
   */
  if ( mm_pool_coc ) {
    mm_pool_destroy( mm_pool_coc ) ;
    mm_pool_coc = NULL ;
  }
  if ( mm_pool_color ) {
    mm_sac_destroy( mm_pool_color ) ;
    mm_pool_destroy( mm_pool_color ) ;
    mm_pool_color = NULL ;
  }
  if ( mm_pool_temp ) {
    mm_pool_destroy(mm_pool_temp);
    mm_pool_temp = NULL;
  }

  /* mm_pool_fixed must be last, because it contains the pool descriptors. */
  /* Normally, all the other pools should have been destroyed by now. */
  if ( !abort && mm_pool_fixed )
    HQASSERT(mm_pool_list == mm_pool_fixed && mm_pool_fixed->next == NULL,
             "unexpected pool not destroyed during regular exit");

  /* Destroy any pools in the list except mm_pool_fixed */
  /* Don't lock as this must be the last core thread. */
  for ( pool = mm_pool_list; pool != NULL; ) {
    mm_pool_t next = pool->next;
    if ( pool != mm_pool_fixed )
      mm_pool_destroy(pool);
    pool = next;
  }

  /* Everything gone - now safe to get rid of the last pool : mm_pool_fixed */
  if ( mm_pool_fixed ) {
    mm_pool_destroy(mm_pool_fixed);
    mm_pool_fixed = NULL;
  }
  HQASSERT(mm_pool_list == NULL, "pools not all destroyed");
}


#ifdef MM_DEBUG_MPSTAG


typedef struct {
  char *location; /* NULL means unused */
  mps_word_t label;
} mm_location_entry_t;

#define MM_LOCATIONS_COUNT (999)

/* A closed hash table to store labels for each location. */
static mm_location_entry_t mm_locations[MM_LOCATIONS_COUNT];


static void mm_location_init(void)
{
  size_t i;
  mm_location_entry_t init = { 0 } ;

  for (i = 0; i < NUM_ARRAY_ITEMS(mm_locations); i++)
    mm_locations[i] = init;
}



mps_word_t mm_location_label( char *location )
{
  size_t hash, i;

  hash = (((size_t)location >> 2) ^ ((size_t)location << 6))
         % MM_LOCATIONS_COUNT;
  i = hash;
  do {
    if (mm_locations[i].location == location)
      return mm_locations[i].label;
    if (mm_locations[i].location == NULL) {
      mm_locations[i].location = location;
      mm_locations[i].label = mps_telemetry_intern(location);
      return mm_locations[i].label;
    }
    i++; if (i == MM_LOCATIONS_COUNT) i = 0;
  } while (i != hash);
  /* Didn't find it, and no free slots, give up and just make a new one. */
  return mps_telemetry_intern(location);
}


#else  /* MM_DEBUG_MPSTAG */


static void mm_location_init(void)
{
}


#endif  /* MM_DEBUG_MPSTAG */


/* == General alloc/free routines == */


void mm_free( mm_pool_t pool,
              mm_addr_t what,
              size_t size )
{
  HQASSERT( pool != NULL, "mm_free: pool parm was NULL" ) ;
  HQASSERT( what != NULL, "mm_free: what parm was NULL" ) ;
  HQASSERT( size != 0, "mm_free: zero-sized object" ) ;

#ifdef MM_DEBUG_FENCEPOST
  mm_debug_check_fenceposts() ;
#endif

  mm_debug_check_single_fencepost( pool, what, size );
  mm_debug_scribble( what, size ) ;
  if ( !pool->mps_debug ) {
    size = ADJUST_FOR_FENCEPOSTS( size );
    what = BELOW_FENCEPOST( what );
  }

  mps_free( pool->mps_pool, ( mps_addr_t )what, size );

  mm_debug_tag_free( what, size, pool ) ;
  mm_debug_total_free( pool, size ) ;

  MM_LOG(( LOG_MF, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )what, ( uint32 )size )) ;
  mm_recheck_reserves();
}


void mm_truncate( mm_pool_t pool,
                  mm_addr_t base,
                  size_t oldsize,
                  size_t newsize )
{
  mm_addr_t end;
  size_t surplus;

  HQASSERT( pool != NULL, "mm_truncate: pool parm was NULL" ) ;
  HQASSERT( base != NULL, "mm_truncate: base parm was NULL" ) ;
  HQASSERT( oldsize != 0, "mm_truncate: zero sized object" ) ;
  HQASSERT( newsize != 0, "mm_truncate: can't truncate to object zero size" ) ;
  HQASSERT( newsize < oldsize, "mm_truncate: newsize must be less then oldsize" ) ;

  /* First check existing fenceposts */
#ifdef MM_DEBUG_FENCEPOST
  mm_debug_check_fenceposts() ;
#endif
  mm_debug_check_single_fencepost(pool, base, oldsize);
  if ( !pool->mps_debug ) {
    /* Adjust pointer and sizes for fenceposts and write new upper one */
    base = BELOW_FENCEPOST(base);
    oldsize = ADJUST_FOR_FENCEPOSTS(oldsize);
    newsize = ADJUST_FOR_FENCEPOSTS(newsize);
    /* On 64-bit Windows, this becomes true in some instances, so do not
       fall though code which causes asserts. */
    if (oldsize == newsize)
      return;
    mm_debug_truncate_fencepost(UPPER_FENCEPOST(base, newsize));
  }
  /* Calc start and size of surplus memory and scribble over it all */
  end = (mm_addr_t)((char*)base + newsize);
  surplus = (size_t)(oldsize - newsize);
  HQASSERT((surplus > (size_t)0),
           "mm_truncate: no change in memsize (ignoreable if fenceposting)");

  mm_debug_scribble(end, surplus);

#ifndef VALGRIND_BUILD
  /* Free off surplus */
  mps_free(pool->mps_pool, (mps_addr_t)end, (size_t)surplus);
#endif

  /* Update memory tag table and total allocated in pool*/
#ifndef MM_DEBUG_MPSTAG
  mm_debug_tag_truncate(base, oldsize, newsize, pool);
#endif
  mm_debug_total_truncate(pool, surplus);

  /* Log call - note uses modified ptr and sizes when fenceposting */
  MM_LOG((LOG_MT, "0x%08x 0x%08x 0x%08x 0x%08x",
          (uint32)pool, (uint32)base,
          (uint32)oldsize, (uint32)newsize));
  mm_recheck_reserves();
}


void mm_pool_clear( mm_pool_t pool )
{
  HQASSERT( pool != NULL, "mm_pool_clear called on NULL pool" ) ;

#ifdef MM_DEBUG_FENCEPOST
  mm_debug_check_fenceposts() ;
#endif
  if ( pool->sac != NULL )
    mm_sac_flush( pool ); /** \todo Should move auto SAC flush to MPS. */
  mps_pool_clear( pool->mps_pool );
  mm_debug_tag_free_pool( pool ) ;
  mm_debug_total_clear( pool ) ;

  MM_LOG(( LOG_PE, "0x%08x %d",
           (uint32)pool, (uint32)MM_SUCCESS ));
  mm_recheck_reserves();
}


/** An illegal context value, so the code can tell if it was set. */
#define CONTEXT_NOT_SET ((corecontext_t *)(intptr_t)1)


#ifdef MM_DEBUG_ALLOC_CLASS
mm_addr_t mm_alloc_class( mm_pool_t pool,
                          size_t size,
                          mm_alloc_class_t class
                          MM_DEBUG_LOCN_PARMS )
#else
mm_addr_t mm_alloc_noclass( mm_pool_t pool,
                            size_t size
                            MM_DEBUG_LOCN_PARMS )
#endif
{
#ifndef MM_DEBUG_ALLOC_CLASS
  const mm_alloc_class_t class = MM_ALLOC_CLASS_UNSPECIFIED;
#endif
  mps_res_t res = MPS_RES_FAIL;
  mps_addr_t p = NULL ;
#ifdef MM_DEBUG_MPSTAG
  mps_debug_info_s dinfo;
#endif
  corecontext_t *context = CONTEXT_NOT_SET;
  memory_requirement_t request;

  MM_DEBUG_LOGGING_AND_TAGS_USE_PARAM( mm_alloc_class_t, class ) ;
  HQASSERT( pool != NULL, "attempted allocation in NULL pool" );
  HQASSERT( size != 0, "illegal zero-sized allocation attempt" );
  HQASSERT( size < TWO_GB, "allocation exceeds 2 GB limit" );

  if ( !pool->mps_debug )
    size = ADJUST_FOR_FENCEPOSTS( size );
  MM_LOG(( LOG_MI, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )size, ( uint32 )class )) ;
  MPSTAG_SET_DINFO(class);
  if ( reserves_allow_alloc(&context) ) {
    res = MPSTAG_FN(mps_alloc)(&p, pool->mps_pool, size MPSTAG_ARG);
    if ( res != MPS_RES_OK && context == CONTEXT_NOT_SET )
      context = &CoreContext; /* haven't fetched it; will need it */
  }
  /* If no context (skin thread or init), can't do low-mem. */
  if ( res != MPS_RES_OK && context != NULL ) {
    request.pool = pool; request.size = size;
    request.cost = context->mm_context->default_cost;
    res = mm_low_mem_alloc(&p, context, &request,
                           (alloc_fn *)&MPSTAG_FN(mps_alloc),
                           pool->mps_pool MPSTAG_ARG);
  }
  if ( res == MPS_RES_OK ) {
    MM_DEBUG_TAG_ADD( p, size, pool, class ) ;
    mm_debug_total_alloc( pool, size ) ;
    p = mm_debug_set_fencepost( pool, p, size );
  }
  MM_LOG(( LOG_MO, "0x%08x 0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )p, ( uint32 )size, ( uint32 )class )) ;
  return p ;
}


#ifdef MM_DEBUG_ALLOC_CLASS
mm_addr_t mm_alloc_cost_class( mm_pool_t pool,
                               size_t size,
                               mm_cost_t cost,
                               mm_alloc_class_t class
                               MM_DEBUG_LOCN_PARMS )
#else
mm_addr_t mm_alloc_cost_noclass( mm_pool_t pool,
                                 size_t size,
                                 mm_cost_t cost
                                 MM_DEBUG_LOCN_PARMS )
#endif
{
#ifndef MM_DEBUG_ALLOC_CLASS
  const mm_alloc_class_t class = MM_ALLOC_CLASS_UNSPECIFIED;
#endif
  mps_res_t res = MPS_RES_FAIL;
  mps_addr_t p = NULL;
#ifdef MM_DEBUG_MPSTAG
  mps_debug_info_s dinfo;
#endif
  memory_requirement_t request;

  MM_DEBUG_LOGGING_AND_TAGS_USE_PARAM(mm_alloc_class_t, class);
  HQASSERT(pool != NULL, "attempted allocation in NULL pool");
  HQASSERT(size != 0, "illegal zero-sized allocation attempt");
  HQASSERT(size < TWO_GB, "allocation exceeds 2 GB limit");
  HQASSERT(is_valid_cost(cost), "invalid cost for allocation");

  if ( !pool->mps_debug )
    size = ADJUST_FOR_FENCEPOSTS( size );
  MM_LOG(( LOG_MI, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )size, ( uint32 )class ));
  MPSTAG_SET_DINFO(class);
  if ( !mm_should_regain_reserves(cost) )
    res = MPSTAG_FN(mps_alloc)(&p, pool->mps_pool, size MPSTAG_ARG);
  if ( res != MPS_RES_OK ) {
    request.pool = pool; request.size = size; request.cost = cost;
    res = mm_low_mem_alloc(&p, &CoreContext, &request,
                           (alloc_fn *)&MPSTAG_FN(mps_alloc),
                           pool->mps_pool MPSTAG_ARG);
  }
  if ( res == MPS_RES_OK ) {
    MM_DEBUG_TAG_ADD( p, size, pool, class );
    mm_debug_total_alloc( pool, size );
    p = mm_debug_set_fencepost( pool, p, size );
  }
  MM_LOG(( LOG_MO, "0x%08x 0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )p, ( uint32 )size, ( uint32 )class ));
  return p;
}


/* == Support for SACs (segregated allocation caches) == */


static mps_res_t MPS_CALL mm_sac_alloc_wrapper( mps_addr_t *p,
                                                void *args, size_t size
#ifdef MM_DEBUG_MPSTAG
                                                , mps_debug_info_s *dinfo
#endif
                                                )
{
  mps_res_t res ;

#ifdef MM_DEBUG_MPSTAG
  UNUSED_PARAM(mps_debug_info_s *, dinfo);
#endif
  MPS_SAC_ALLOC_FAST( res, *p, (mps_sac_t)args, size, TRUE );
  return res ;
}


#ifdef MM_DEBUG_ALLOC_CLASS
mm_addr_t mm_sac_alloc_class( mm_pool_t pool,
                              size_t size,
                              mm_alloc_class_t class
                              MM_DEBUG_LOCN_PARMS )
#else
mm_addr_t mm_sac_alloc_noclass( mm_pool_t pool,
                                size_t size
                                MM_DEBUG_LOCN_PARMS )
#endif
{
#ifndef MM_DEBUG_ALLOC_CLASS
  const mm_alloc_class_t class = MM_ALLOC_CLASS_UNSPECIFIED;
#endif
  mps_res_t res = MPS_RES_FAIL;
  mps_addr_t p = NULL ;
#ifdef MM_DEBUG_MPSTAG
  mps_debug_info_s dinfo; /* not used, since SACs don't support it */
#endif
  corecontext_t *context = CONTEXT_NOT_SET;
  memory_requirement_t request;

#if defined(MM_DEBUG_LOCN) && defined(MM_DEBUG_MPSTAG)
  UNUSED_PARAM(char *, location);
#endif
  MM_DEBUG_LOGGING_AND_TAGS_USE_PARAM( mm_alloc_class_t, class ) ;
  HQASSERT( pool != NULL, "attempted allocation in NULL pool" );
  HQASSERT( size != 0 , "illegal zero-sized allocation attempt" );
  HQASSERT( size < TWO_GB, "allocation exceeds 2 GB limit" );
  HQASSERT( pool->sac != NULL, "sac is NULL" );

  size = ADJUST_FOR_FENCEPOSTS( size ) ;
  MM_LOG(( LOG_SI, "0x%08x 0x%08x 0x%08x 0x%08x",
           (uint32)pool, (uint32)pool->sac, (uint32)size, (uint32)class ));
  if ( reserves_allow_alloc(&context) ) {
#ifndef VALGRIND_BUILD
    MPS_SAC_ALLOC_FAST( res, p, pool->sac, size, TRUE );
#else
#ifdef MM_DEBUG_ALLOC_CLASS
    p = mm_alloc_class( pool, size, class MM_DEBUG_LOCN_THRU );
#else
    p = mm_alloc_noclass( pool, size MM_DEBUG_LOCN_THRU );
#endif  /* MM_DEBUG_ALLOC_CLASS */
    res = (p != NULL) ? MPS_RES_OK : MPS_RES_MEMORY;
#endif  /* VALGRIND_BUILD */
    if ( res != MPS_RES_OK && context == CONTEXT_NOT_SET )
      context = &CoreContext; /* haven't fetched it; will need it */
  }
  /* If no context (skin thread or init), can't do low-mem. */
  if ( res != MPS_RES_OK && context != NULL ) {
    request.pool = pool; request.size = size;
    request.cost = context->mm_context->default_cost;
    res = mm_low_mem_alloc(&p, context, &request, &mm_sac_alloc_wrapper,
                           pool->sac MPSTAG_ARG);
  }
  if ( res == MPS_RES_OK ) {
    MM_DEBUG_TAG_ADD( p, size, pool, class ) ;
    mm_debug_total_sac_alloc( pool, size ) ;
    p = mm_debug_set_fencepost( pool, p, size ) ;
  }
  MM_LOG(( LOG_SO, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x",
           (uint32)pool, (uint32)pool->sac,
           (uint32)p, (uint32)size, (uint32)class ));
  return p ;
}


void mm_sac_free( mm_pool_t pool, mm_addr_t what, size_t size )
{
  HQASSERT( pool != NULL, "mm_sac_free: pool parm was NULL" ) ;
  HQASSERT( what != NULL, "mm_sac_free: what parm was NULL" ) ;
  HQASSERT( size != 0, "mm_sac_free: zero-sized object" ) ;
  HQASSERT( pool->sac != NULL, "mm_sac_free: sac is NULL" ) ;

#ifdef MM_DEBUG_FENCEPOST
  mm_debug_check_fenceposts() ;
#endif

  mm_debug_check_single_fencepost( pool, what, size );
  mm_debug_scribble( what, size ) ;

  size = ADJUST_FOR_FENCEPOSTS( size ) ;
  what = BELOW_FENCEPOST( what ) ;

#ifndef VALGRIND_BUILD
  MPS_SAC_FREE_FAST( pool->sac, what, size );
#else
  mm_free( pool, what, size );
#endif

  mm_debug_tag_free( what, size, pool ) ;
  mm_debug_total_sac_free( pool, size ) ;

  MM_LOG(( LOG_SF, "0x%08x 0x%08x 0x%08x 0x%08x",
           ( uint32 )pool,
           ( uint32 )pool->sac,
           ( uint32 )what,
           ( uint32 )size )) ;
  mm_recheck_reserves();
}


mm_result_t mm_sac_create( mm_pool_t pool,
                           mm_sac_classes_t classes,
                           size_t count )
{
  mps_sac_t sac ;
  size_t i;
  mps_sac_classes_s adjusted[MPS_SAC_CLASS_LIMIT];

  HQASSERT( pool != NULL, "mm_sac_create: pool is NULL" ) ;
  HQASSERT( pool->sac == NULL, "mm_sac_create: sac has already been created" ) ;
  HQASSERT( classes != NULL, "mm_sac_create: classes is NULL" ) ;
  HQASSERT( count > 0 && count <= MPS_SAC_CLASS_LIMIT,
            "mm_sac_create: count is out of range" ) ;
  HQASSERT( sizeof(mps_sac_class_s) == sizeof(struct mm_sac_classes_t),
            "mm_sac_create: class size differs between MM and MPS") ;
  HQASSERT( !pool->mps_debug, "SACs do not support MPS debugging yet" );

  for ( i = 0 ; i < count ; ++i ) {
    adjusted[i].mps_block_size = ADJUST_FOR_FENCEPOSTS(classes[i].block_size);
    adjusted[i].mps_cached_count = classes[i].cached_count;
    adjusted[i].mps_frequency = classes[i].frequency;
  }
  if ( mps_sac_create( &sac, pool->mps_pool, count, adjusted ) != MPS_RES_OK )
    return MM_FAILURE ;

  pool->sac = sac ;
  mm_debug_total_sac_init( pool, classes, (int32)count ) ;

#ifdef MM_DEBUG_LOGGING
  {
    size_t index;

    MM_LOG(( LOG_SC, "0x%08x 0x%08x 0x%08x",
             ( uint32 )pool,
             ( uint32 )pool->sac,
             ( uint32 )count ));
    for ( index = 0 ; index < count ; ++index )
      MM_LOG(( LOG_SL, "0x%08x: 0x%08x 0x%08x 0x%08x",
               (uint32)index,
               (uint32)classes[index].block_size,
               (uint32)classes[index].cached_count,
               (uint32)classes[index].frequency));
  }
#endif /* MM_DEBUG_LOGGING */

  return MM_SUCCESS ;
}

void mm_sac_flush( mm_pool_t pool )
{
  HQASSERT( pool != NULL, "mm_sac_flush: pool is NULL" ) ;
  HQASSERT( pool->sac != NULL, "mm_sac_flush: sac is NULL" ) ;

  mps_sac_flush( pool->sac ) ;
  mm_debug_total_sac_flush( pool ) ;
  MM_LOG(( LOG_SE, "0x%08x %x08x", ( uint32 )pool, ( uint32 )pool->sac )) ;
}

void mm_sac_destroy( mm_pool_t pool )
{
  HQASSERT( pool != NULL, "mm_sac_destroy: pool is NULL" ) ;
  HQASSERT( pool->sac != NULL, "mm_sac_destroy: sac is NULL" ) ;

  mps_sac_destroy( pool->sac ) ;
  pool->sac = NULL ;

  mm_debug_total_sac_zero( pool ) ;
  MM_LOG(( LOG_SD, "0x%08x %x08x", ( uint32 )pool, ( uint32 )pool->sac )) ;
}

/* Is the sac for this pool valid? Returns MM_SUCCESS if it is */
mm_result_t mm_sac_present( mm_pool_t pool )
{
  HQASSERT( pool != NULL, "mm_sac_present: pool is NULL" ) ;

  return pool->sac != NULL ? MM_SUCCESS : MM_FAILURE;
}


mps_sac_t mm_pool_sac( mm_pool_t pool )
{
  HQASSERT( pool != NULL, "pool is NULL" );
  return pool->sac;
}


#ifdef METRICS_BUILD
static void mm_track_fragmentation(void)
{
  mm_pool_t other;

  spinlock_counter(&mm_pool_list_lock, 1);
  for ( other = mm_pool_list ; other != NULL ; other = other->next )
    mm_debug_total_allocfail(other);
  spinunlock_counter(&mm_pool_list_lock);
}
#endif


/* == Low Memory Handling == */


inline Bool reserves_allow_alloc(corecontext_t **context_out)
{
  corecontext_t *context;

  if ( !mm_memory_is_low ) /* unsynchronized, but it doesn't have to be exact */
    return TRUE;
  *context_out = context = &CoreContext;
  return context == NULL || context->mm_context == NULL
     /* NULL context on skin thread or during init, allow alloc */
     || !mm_should_regain_reserves(context->mm_context->default_cost);
}


/** Low-memory handling for allocation */
mps_res_t mm_low_mem_alloc(mps_addr_t *p, corecontext_t *context,
                           memory_requirement_t *request,
                           alloc_fn *fn, void *args
#ifdef MM_DEBUG_MPSTAG
                         , mps_debug_info_s *dinfo
#endif
                           )
{
  mps_res_t res = MPS_RES_FAIL;
  Bool nested_call;
  Bool enough_reserves = TRUE /* pacify the compiler */;
  Bool retry = TRUE /* pacify the compiler */;

  HQASSERT( request != NULL, "request param is NULL" );
  HQASSERT( fn != NULL, "fn param is NULL" );

#ifdef METRICS_BUILD
  mm_track_fragmentation();
#endif
  if ( context == NULL ) /* skin thread or init, can't do low-mem */
    return MPS_RES_FAIL;
#if defined(FAIL_AFTER_N_ALLOCS)
  if ( fail_after_n > 0 && n_alloc_calls >= fail_after_n ) {
    fail_after_n = 0;
    return MPS_RES_FAIL;
  }
#endif
  HQTRACE(debug_lowmemory,
          ("mm_low_mem_alloc %1d:%8.2e %d",
           request->cost.tier, request->cost.value, request->size));

  /* Synchronize for low memory handling. */
  if ( !enter_low_mem_handling(context, &nested_call, LOWMEM_ALLOC) )
    return MPS_RES_FAIL; /* error or interrupt */

  /* Even if reserves were full before, another thread could have taken
     some, so regain unconditionally. */
  if ( !mm_regain_reserves_for_alloc(&enough_reserves, context, request->cost) )
    goto error;
  if ( !enough_reserves )
    goto error; /* deny alloc */

  /* The basic scheme is to call the apportioner, which calls a
     low-memory handler; if it was able to free up some memory, retry
     the allocation; this continues until either the allocation succeeds
     or the apportioner is unable to free up any more memory. */
  do {
#if defined(DEBUG_BUILD)
    if ( debug_lowmemory_count > 0 ) {
      --debug_lowmemory_count;
      if ( debug_lowmemory_count == 0 )
        /* @@@@ assuming memory isn't low - this hack will be obsolete RSN */
        mm_memory_is_low = FALSE;
      res = MPS_RES_FAIL;
    } else
#endif
#ifdef MM_DEBUG_MPSTAG
    res = (*fn)(p, args, request->size, dinfo);
#else
    res = (*fn)(p, args, request->size);
#endif
    if ( res == MPS_RES_OK )
      break;
    if ( !low_mem_handle_guts(&retry, context, 1, request) )
      break; /* error, so give up */
  } while ( retry );
error:
  exit_low_mem_handling(context, nested_call, LOWMEM_ALLOC);
  return res;
}


/* == Measuring Pools == */

/* How many bytes are assigned to this pool?                               */
/* Undefined behaviour if called before the memory manager is initialized. */
size_t mm_pool_size( mm_pool_t pool )
{
  size_t managed_size, free_size;

  HQASSERT( pool, "Invalid NULL pool" );

  mps_pool_size(pool->mps_pool, &managed_size, &free_size);
  return managed_size;
}

/* How many free bytes are there in this pool?                             */
/* Undefined behaviour if called before the memory manager is initialized. */
size_t mm_pool_free_size( mm_pool_t pool )
{
  size_t managed_size, free_size;

  HQASSERT( pool, "Invalid NULL pool" );

  mps_pool_size(pool->mps_pool, &managed_size, &free_size);
  return free_size;
}


/* How many allocated bytes are there in this pool? */
size_t mm_pool_alloced_size( mm_pool_t pool )
{
  size_t managed_size, free_size;

  HQASSERT( pool, "Invalid NULL pool" );

  mps_pool_size(pool->mps_pool, &managed_size, &free_size);
  return managed_size - free_size;
}


/* Displays memory usage stats for each pool */
static int32 mm_pool_memusage( mm_pool_t pool )
{
  monitorf(( uint8 * )"  %s: %d bytes, %d free\n",
           mm_pooltype_map[ pool->type ].name,
           mm_pool_size( pool ), mm_pool_free_size( pool )) ;
  return TRUE ;
}

void mm_pool_memstats( void )
{
  ( void )mm_pool_walk( & mm_pool_memusage ) ;
}


Bool mm_pool_check( mm_pool_t pool, mm_addr_t addr )
{
  HQASSERT( pool, "Invalid NULL pool" );
  return mps_pool_has_addr(pool->mps_pool, addr);
}


/* == Multiple Allocation == */


#ifdef MM_DEBUG_ALLOC_CLASS
mm_result_t mm_alloc_multi_homo_class( mm_pool_t pool,
                                       size_t count,
                                       size_t size,
                                       mm_alloc_class_t class,
                                       mm_addr_t returns[]
                                       MM_DEBUG_LOCN_PARMS )
#else
mm_result_t mm_alloc_multi_homo_noclass( mm_pool_t pool,
                                         size_t count,
                                         size_t size,
                                         mm_addr_t returns[]
                                         MM_DEBUG_LOCN_PARMS )
#endif
{
#ifndef MM_DEBUG_ALLOC_CLASS
  const mm_alloc_class_t class = MM_ALLOC_CLASS_UNSPECIFIED;
#endif
  size_t n ;
  mm_addr_t alloc ;

  HQASSERT( pool != NULL, "mm_alloc_multi_homo_class: Invalid NULL pool" ) ;

  n = count ;
  while ( n-- > 0 ) {
    alloc = mm_alloc_thru( pool, size, class );
    returns[ n ] = alloc ;
    if ( alloc == NULL ) {
      while (( ++n ) < count ) {
        mm_free( pool, returns[ n ], size ) ;
        returns[ n ] = NULL ;
      }
      return MM_FAILURE ;
    }
  }
  return MM_SUCCESS ;
}


#ifdef MM_DEBUG_ALLOC_CLASS
mm_result_t mm_alloc_multi_hetero_class( mm_pool_t pool,
                                         size_t count,
                                         size_t sizes[],
                                         mm_alloc_class_t classes[],
                                         mm_addr_t returns[]
                                         MM_DEBUG_LOCN_PARMS )
#else
mm_result_t mm_alloc_multi_hetero_noclass( mm_pool_t pool,
                                           size_t count,
                                           size_t sizes[],
                                           mm_addr_t returns[]
                                           MM_DEBUG_LOCN_PARMS )
#endif
{
  size_t n ;
  mm_addr_t alloc ;

  HQASSERT( pool != NULL, "mm_alloc_multi_hetero_class: Invalid NULL pool" ) ;

  n = count ;
  while ( n-- > 0 ) {
#ifdef MM_DEBUG_ALLOC_CLASS
    mm_alloc_class_t class = classes[ n ];
#else
    const mm_alloc_class_t class = MM_ALLOC_CLASS_UNSPECIFIED;
#endif
    alloc = mm_alloc_thru( pool, sizes[ n ], class );
    returns[ n ] = alloc ;
    if ( alloc == NULL ) {
      while (( ++n ) < count ) {
        mm_free( pool, returns[ n ], sizes[ n ] ) ;
        returns[ n ] = NULL ;
      }
      return MM_FAILURE ;
    }
  }
  return MM_SUCCESS ;
}


/* == DL Allocation Promises == */

/* If a 'promise' has been given, then allocations to the promised
 * amount will all succeed.  Only one promise can be issued per pool at
 * any one time.  */

/* A promise is obtained by mm_dl_promise(pool, total_size).  If this
 * returns MM_FAILURE, there was insufficient memory and the promise is
 * not issued.  */
mm_result_t mm_dl_promise( mm_pool_t pool, size_t size )
{
  mm_addr_t promise ;
  mm_result_t res ;

  HQASSERT( pool != NULL, "mm_dl_promise: Invalid NULL pool" ) ;
  HQASSERT( pool->type == DL_POOL_TYPE, "mm_dl_promise: Non-DL pool" ) ;
  HQASSERT( size != 0, "mm_dl_promise: Illegal zero sized promise" ) ;
  HQASSERT( pool->the.dl_pool.promise_base == NULL,
            "mm_dl_promise: Promise already made" ) ;

  size = WORD_ALIGN_UP(size_t, size);
  promise = mm_alloc( pool, size, MM_ALLOC_CLASS_PROMISE ) ;
  if ( promise == NULL ) {
    res = MM_FAILURE ;
  }
  else {
    pool->the.dl_pool.promise_base = promise ;
    pool->the.dl_pool.promise_top = (mm_addr_t)((char *)promise + size ) ;
    pool->the.dl_pool.promise_next = promise ;
    res = MM_SUCCESS ;
  }

  MM_LOG(( LOG_DB, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )promise, ( uint32 )size )) ;
  return res ;
}

/* The next piece of promised memory is obtained by
 * mm_dl_promise_next(pool, next_size) */
mm_addr_t mm_dl_promise_next( mm_pool_t pool, size_t size )
{
  mm_addr_t result ;

  HQASSERT( pool != NULL, "mm_dl_promise_next: Invalid NULL pool" ) ;
  HQASSERT( pool->type == DL_POOL_TYPE, "mm_dl_promise_next: Non-DL pool" ) ;
  HQASSERT( size != 0, "mm_dl_promise_next: Illegal zero-sized promise request" ) ;
  HQASSERT( pool->the.dl_pool.promise_base != NULL,
            "mm_dl_promise_next: Promise request with no active promise" ) ;

  size = WORD_ALIGN_UP(size_t, size);
  if ((size_t)((char *)pool->the.dl_pool.promise_top -
                  (char *)pool->the.dl_pool.promise_next ) < size ) {
    result = NULL ;
  }
  else {
    result = pool->the.dl_pool.promise_next ;
    pool->the.dl_pool.promise_next = (mm_addr_t)((char *)result + size ) ;
    HQASSERT( WORD_IS_ALIGNED( uintptr_t, result ), "unaligned result" ) ;
  }

  MM_LOG(( LOG_DN, "0x%08x 0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )size, ( uint32 )result )) ;
  return result ;
}

/* Reduce the amount of promise in use by the given number of bytes via
 * mm_dl_promise_next(pool, bytes_to_reduce_by)
 */
void mm_dl_promise_shrink(mm_pool_t pool, size_t size)
{
  mm_addr_t result;

  HQASSERT(pool != NULL, "Invalid NULL pool");
  HQASSERT(pool->type == DL_POOL_TYPE, "Non-DL pool");
  HQASSERT(size != 0, "Illegal zero-sized promise shrink");
  HQASSERT(pool->the.dl_pool.promise_base != NULL, "No active promise");

  /* can only shrink it by aligned down size */
  size = (size_t)WORD_ALIGN_DOWN(size_t, size);

  result = pool->the.dl_pool.promise_next;
#ifndef VALGRIND_BUILD
  pool->the.dl_pool.promise_next = (mm_addr_t)((char *)result - size);
#endif
  HQASSERT(pool->the.dl_pool.promise_base <=
           pool->the.dl_pool.promise_next, "shrinking promise too far");
}

/* When the promised allocation is complete, mm_dl_promise_end(pool)
 * ends the promise and allows any unused memory to be re-used.  */
size_t mm_dl_promise_end( mm_pool_t pool )
{
  mm_addr_t base, next, top ;
  size_t new_size ;

  HQASSERT( pool != NULL, "mm_dl_promise_end: Invalid NULL pool" ) ;
  HQASSERT( pool->type == DL_POOL_TYPE, "mm_dl_promise_end: Non-DL pool" ) ;
  HQASSERT( pool->the.dl_pool.promise_base != NULL,
            "mm_dl_promise_end: Ending non-existent promise" ) ;

  base = pool->the.dl_pool.promise_base ;
  next = pool->the.dl_pool.promise_next ;
  top  = pool->the.dl_pool.promise_top ;
  HQASSERT( top >= next && next >= base, "Inconsistent promise ptrs") ;

  if ( next == base ) {
    /* If we didn't touch the promise then free everything and remove
     * the promise.  */
    mm_free( pool, base, (size_t)((char *)top - (char *)base ));
    pool->the.dl_pool.promise_top  = NULL ;
    pool->the.dl_pool.promise_next = NULL ;
    new_size = 0 ;
  }
  else {
    size_t old_size ;

    /* Otherwise, if there is still some promised memory left then
     * truncate the promise, freeing the unused remainder.
     * .assume.epdl.sub-block.free
     */
    old_size = (size_t)((char *)top  - (char *)base ) ;
    new_size = (size_t)((char *)next - (char *)base ) ;
    if ( old_size != new_size )
      mm_truncate( pool, base, old_size, new_size ) ;

    /* set up pointers so a free will kill the used memory.
     * see .promise.free.ended */
    pool->the.dl_pool.promise_top  = next ;
    pool->the.dl_pool.promise_next = base ;
  }

  /* remove promise so a new promise can be made */
  pool->the.dl_pool.promise_base = NULL ;
  MM_LOG(( LOG_DE, "0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )base )) ;

  return new_size ;
}

/* mm_dl_promise_free -- free promise
 *
 * If the currently promised allocation turns out to be un-needed,
 * mm_dl_promise_free(pool) abandons the promise and frees any memory
 * allocated from the promise.  It can also be called after a promise is
 * ended, in which case it will free the last promise.  */
void mm_dl_promise_free( mm_pool_t pool )
{
  mm_addr_t free ;

  HQASSERT( pool != 0, "mm_dl_promise_free: Invalid zero pool" ) ;
  HQASSERT( pool->type == DL_POOL_TYPE, "mm_dl_promise_free: Non-DL pool" ) ;
  HQASSERT( pool->the.dl_pool.promise_next != NULL,
            "mm_dl_promise_free: Freeing non-existent promise "
            "(ignorable, but needs fixing properly)" ) ;
  /* .promise.free.ended: check that the promise has already been ended */
  if ( pool->the.dl_pool.promise_base == NULL ) {
    free = pool->the.dl_pool.promise_next ;
  }
  else {
    /* the promise is ongoing */
    free = pool->the.dl_pool.promise_base ;
  }

  /* A bit of defensive programming: protect release builds from free
   * being called twice on the same promise.  */
  if ( free == NULL ) {
    return ;
  }

  /* free the promise - this will be the whole promise, or the used
   * part of an ended (and probably truncated) promise.
   * .assume.epdl.sub-block.free
   */
  mm_free( pool, free,
           (size_t)((char *)pool->the.dl_pool.promise_top - (char *)free ));
  /* remove promise */
  pool->the.dl_pool.promise_base = NULL ;
  pool->the.dl_pool.promise_next = NULL ;
  MM_LOG(( LOG_DF, "0x%08x 0x%08x",
           ( uint32 )pool, ( uint32 )free )) ;
}


static void init_C_globals_vm(void)
{
  size_t i ;

#if defined( ASSERT_BUILD )
  debug_lowmemory = FALSE;
#endif
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(alloc_class_label) ; ++i )
    alloc_class_label[i] = 0 ;

  mm_pool_list = NULL ;
  mm_pool_fixed = NULL ;
  mm_pool_temp = NULL ;
  mm_pool_color = NULL ;
  mm_pool_coc = NULL ;
  mm_arena = NULL ;
  mm_location_init() ;
  /* n_alloc_calls, fail_after_n must survive reboot */
  is_low_mem_configuration = FALSE;
}


static Bool mem_swinit(SWSTART *params)
{
  mm_result_t rv = MM_FAILURE;
  int32 i ;

  for ( i = 0; params[i].tag != SWEndTag; i++ )
  {
    if ( params[i].tag == SWMemoryTag )
    {
      SWSTART_MEMORY memoryInfo = *(SWSTART_MEMORY *)params[i].value.pointer_value;
      rv = mm_init( memoryInfo.arena, memoryInfo.sizeA, memoryInfo.sizeA,
                    0, FALSE );
      break;
    }
    else if ( params[i].tag == SWMemCfgTag )
    {
      SWSTART_MEMCFG memcfgInfo = *(SWSTART_MEMCFG *)params[i].value.pointer_value;
      rv = mm_init( memcfgInfo.arena,
                    memcfgInfo.maxAddrSpaceSize, memcfgInfo.workingSize,
                    memcfgInfo.emergencySize, memcfgInfo.allowUseAllMem );
      break;
    }
  }

  HQASSERT( params[i].tag != SWEndTag, "No memory tag in array passed to SwInit()" );

  if (rv != MM_SUCCESS) {
    return dispatch_SwExit( swexit_error_meminit_03, "Could not configure memory as specified.") ;
  }
  return TRUE ;
}

extern Bool rip_work_in_progress;

static void mem_finish(void)
{
  mm_finish(rip_work_in_progress);
}

IMPORT_INIT_C_GLOBALS(mmlog)
IMPORT_INIT_C_GLOBALS(mmps)
IMPORT_INIT_C_GLOBALS(mmreserve)
IMPORT_INIT_C_GLOBALS(mmtag)
IMPORT_INIT_C_GLOBALS(mmwatch)
IMPORT_INIT_C_GLOBALS(mpslibep)
IMPORT_INIT_C_GLOBALS(apportioner)

void mem_C_globals(core_init_fns *fns)
{
  init_C_globals_mmlog() ;
  init_C_globals_mmps() ;
  init_C_globals_mmreserve() ;
  init_C_globals_mmtag() ;
  init_C_globals_mmwatch() ;
  init_C_globals_mpslibep() ;
  init_C_globals_apportioner() ;
  init_C_globals_vm() ;

  fns->swinit = mem_swinit ;
  fns->postboot = apportioner_init;
  fns->finish = mem_finish ;
}

/*
* Log stripped */
