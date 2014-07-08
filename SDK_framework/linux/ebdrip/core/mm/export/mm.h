/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:mm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core RIP memory management interface.
 *
 * This file describes the interface supported by the core RIP memory
 * management system.
 */

#ifndef __MM_H__
#define __MM_H__

#include "mps.h"
#include <stdlib.h>

struct core_init_fns ;

/**
 * \defgroup mm Memory management interface.
 * \ingroup core
 * \{ */

/* Result values (type mm_result_t) are taken from this enum. The enum  */
/* may be extended later, with more specific failure * values. '0' will */
/* always be the only success value.                                    */

enum mm_result_e {
  MM_SUCCESS = 0,                /* success; could be non-zero */
  MM_FAILURE                     /* general failure */
  /* this enum may be extended later, with other failure types */
} ;


typedef void * mm_addr_t;   /**< allocation addresses */
typedef size_t mm_size_t;   /**< allocation sizes (deprecated) */
typedef int mm_result_t;    /**< results (from mm_result_e) */
typedef int mm_pooltype_t;  /**< pool types (from mm_pooltype_e) */
typedef int mm_alloc_class_t; /**< alloc classes for debugging
                                   (from mm_alloc_class_e) */

/* The abstract pool data type. struct mm_pool_t is internal to the MM  */
/* code, and not exported.                                              */
typedef struct mm_pool_t *mm_pool_t ;


/** Type of memory tiers. */
typedef int memory_tier;
enum {
  memory_tier_min = 1,
  memory_tier_ram = memory_tier_min,
  memory_tier_disk,
  memory_tier_partial_paint,
  memory_tier_suppress_mt,
  memory_tier_trash_vm,
  memory_tier_reserve_pool,
  memory_tier_limit
};


/** Type of memory allocation cost. */
typedef struct {
  memory_tier tier; /**< Tier for the cost */
  float value; /**< Cost within the tier */
} mm_cost_t;


/** Cost value lower than any offer. */
extern const mm_cost_t mm_cost_none;

/** Cost value for just above the "easy" handlers.

  @@@@ Transitional trick until better costs are determined. */
extern const mm_cost_t mm_cost_easy;

/** Cost value just below the arena reserve, but above any normal handler. */
extern const mm_cost_t mm_cost_below_reserves;

/** The normal cost (all except the final reserve). */
extern const mm_cost_t mm_cost_normal;

/** Cost value higher than any offer (but safe to use in calculations). */
extern const mm_cost_t mm_cost_all;


/** Comparison predicate for \c mm_cost_t. */
Bool mm_cost_less_than(const mm_cost_t cost1, const mm_cost_t cost2);


/** Minimum of two costs. */
mm_cost_t mm_cost_min(const mm_cost_t cost1, const mm_cost_t cost2);


/** Memory management context, private to the MM module. */
typedef struct mm_context_t mm_context_t;


/** Set the allocation cost in the context given. */
void mm_set_allocation_cost(mm_context_t *context, const mm_cost_t cost);


/** Return the allocation cost from the context given. */
mm_cost_t mm_allocation_cost(mm_context_t *context);


/** Execute body with the default allocation cost adjusted as given. */
#define MM_DO_WITH_COST(mm_context, cost, body) MACRO_START \
    mm_cost_t old_cost_ = mm_current_cost(mm_context); \
    mm_set_current_cost(mm_context, cost); \
    body; \
    mm_set_current_cost(mm_context, old_cost_); \
  MACRO_END


/* Initialisation and finalisation. These use the name mem_* because of a
   conflict with mm_finish(). */
void mem_C_globals(struct core_init_fns *fns) ;

/* == Exported globals == */

struct mps_arena_s; /* from SWmps */
extern struct mps_arena_s *mm_arena;


/* == Pools == */


extern mm_pool_t mm_pool_fixed ;     /* RIP lifetime allocations */


/* For creating new pools */
extern mm_result_t mm_pool_create(
  /*@notnull@*/ /*@out@*/                 mm_pool_t *pool ,
                                          mm_pooltype_t type ,
                                          ... ) ;

/* For disposing of old pools */
extern void mm_pool_destroy(
  /*@notnull@*/ /*@out@*/ /*@only@*/      mm_pool_t pool ) ;

/* free everything in this pool. Initially only implemented for         */
/* DL pools.                                                            */
extern void mm_pool_clear(
  /*@notnull@*/ /*@in@*/                  mm_pool_t pool ) ;

/* How many bytes are assigned to this pool?                            */
extern size_t mm_pool_size(
  /*@notnull@*/ /*@out@*/                 mm_pool_t pool ) ;

/* How many free bytes are there in this pool?                          */
extern size_t mm_pool_free_size(
  /*@notnull@*/ /*@in@*/                  mm_pool_t pool ) ;

/* How many allocated bytes are there in this pool? */
size_t mm_pool_alloced_size(
  /*@notnull@*/ /*@in@*/     mm_pool_t pool );

/* How many bytes are not assigned to any pool?                         */
extern size_t mm_no_pool_size( Bool include_reserve ) ;

/* Allow us to iterate over the current pools                           */
typedef Bool (*mm_pool_fn)( mm_pool_t pool );
extern Bool mm_pool_walk( mm_pool_fn poolfn );
extern void  mm_pool_memstats( void ) ;

/** Check if the address is managed by the given pool. */
Bool mm_pool_check(
  /*@notnull@*/ /*@in@*/ mm_pool_t pool,
  /*@in@*/ mm_addr_t addr );

/* How many bytes are managed by the memory manager?                    */
extern size_t mm_total_size( void ) ;

/* Returns the workingsize - this is intended to be the amount that the
 * RIP can grow to without causing paging
 */
extern size_t mm_working_size( void ) ;

/* Attempts to reset the commit limit reporting level, so that we only
 * give one warning message (per level) per page (debug builds only)
 */
#if defined( DEBUG_BUILD )
extern void mm_commit_level_report_reset( void ) ;
#else
#define mm_commit_level_report_reset()  EMPTY_STATEMENT()
#endif /* DEBUG_BUILD */


/* == Debugging Support == */

/* There are various kinds of debugging, controlled by macros: */

/* MM_DEBUG_TOTAL                                                       */
/*   - keep object/byte totals for each pool. This helps with           */
/*     leak detection and is quite cheap.                               */

/* MM_DEBUG_TAG                                                         */
/*   - tracks each allocated object, detecting overlapped               */
/*     allocations or incorrect frees. This is currently expensive,     */
/*     and uses non-MM memory (malloc) to keep tracking information.    */

/* MM_DEBUG_LOCN                                                        */
/*   - when used with MM_DEBUG_TAG, records the file and line           */
/*     of each allocation in the tag.                                   */
/*   - This requires recompilation of the core RIP.                     */

/* MM_DEBUG_ALLOC_CLASS                                                 */
/*   - Requires each allocation point to identify the 'class'           */
/*     of the object being allocated. These classes (values of          */
/*     type mm_alloc_class_t, from enum mm_alloc_class_e, see           */
/*     mm_core.h) are useful if MM_DEBUG_TAG or MM_DEBUG_WATCH          */
/*     are on.                                                          */
/*   - This requires recompilation of the core RIP.                     */

/* MM_DEBUG_WATCH                                                       */
/*   - allows the core RIP to interrogate the MM about                  */
/*     allocated objects, and to watch as objects are allocated         */
/*     and freed.                                                       */
/*   - This implies MM_DEBUG_TAG.                                       */

/* MM_DEBUG_WATCH_TOTALS */
/*   - Used with MM_DEBUG_WATCH. Sets the watcher mm_totals_tree going  */
/*     on boot, so that at any time, you can call mm_totals_tree_dump   */
/*     to show all live allocation totals in the debug log.             */
/*   - Makes a relatively small out-of-band (malloc) allocation for     */
/*     each total being tracked, so it does perturb memory behaviour.   */

/* MM_DEBUG_FENCEPOST                                                   */
/*   - keeps fenceposts at either end of each allocated object,         */
/*     and checks every fencepost at every allocation and free.         */
/*   - This changes the layout of objects in memory.                    */
/*   - The checking is very frequent and therefore expensive.           */
/*   - This implies MM_DEBUG_TAG.                                       */

/* MM_DEBUG_FENCEPOST_LITE                                              */
/*   - keeps fenceposts at either end of each allocated object,         */
/*     and checks the fencepost at free and resize.                     */
/*   - This changes the layout of objects in memory.                    */
/*   - This implies MM_DEBUG_TAG.                                       */

/* MM_DEBUG_LOGGING                                                     */
/*   - When this is on, calls through the MMI are recorded in a log.    */
/*   - These logs can be used for debugging, measuring, and tuning the  */
/*     memory manager. They are probably not useful for developing the  */
/*     RIP.                                                             */

/* MM_DEBUG_SCRIBBLE                                                    */
/*   - When this is on, memory is scribbled over as it is freed, so     */
/*     that use of pointers refering to this dead memory can be easily  */
/*     uncovered.                                                       */
/*   - Memory is overwritten with 0x64656164 "dead".                    */
/*   - Note that this is fairly expensive, resulting in ~10% slowdown.  */


/* MM debugging is now compiled in (but not necessarily enabled) for all
 * debug builds, or if USE_MM_DEBUGGING is defined.
 *
 * The overhead of having this compiled in but not enabled is insignificant
 * (esp when compared with overhead of asserts in general); even enabling
 * it only results in a 2% slowdown (but does increase memory requirements).
 *
 * To enable mm debugging by default, USE_MM_DEBUGGING must be defined,
 * otherwise the debug_mm_usetags global must be set to true (under the
 * debugger) before the core rip starts up - i.e., in SwStart).
 */

#if defined( USE_MM_DEBUGGING ) || defined( DEBUG_BUILD ) || defined ( ENABLE_MM_DEBUG_TOTAL )

#define MM_DEBUG_TOTAL
#else

#undef MM_DEBUG_TOTAL
#endif

#if defined( USE_MM_DEBUGGING ) || defined( DEBUG_BUILD )

#define MM_DEBUG_LOCN
#define MM_DEBUG_ALLOC_CLASS
#define MM_DEBUG_WATCH

#elif defined(MM_DEBUG_MPSTAG)

#define MM_DEBUG_LOCN
#define MM_DEBUG_ALLOC_CLASS
#undef  MM_DEBUG_WATCH

#else

#undef  MM_DEBUG_LOCN
#undef  MM_DEBUG_ALLOC_CLASS
#undef  MM_DEBUG_WATCH

#endif


/* STRING -- expands into a string of the expansion of the argument
 *
 * E.g., if we have:
 *   #define a b
 * STRING(a) will expand into "b".
 */

#define STRING_(x) #x
#define STRING(x) STRING_(x)


/* When MM_DEBUG_LOCN is on, we need applications of the                */
/* allocation macros to pass the filename and line number as            */
/* additional arguments. This is done through the following             */
/* pre-processor hack.                                                  */

/*   .._THRU is used in mmcompat functions.  Implicit in _thru macros.  */
/* Unfortunately if you change .._THRU, you must change its target      */
/* too, because you cannot specify macro args in another macro.         */

#ifdef MM_DEBUG_LOCN

#ifdef MM_DEBUG_MPSTAG

#define MM_DEBUG_LOCN_ARGS              , __FILE__ " " STRING(__LINE__)
#define MM_DEBUG_LOCN_PARMS             , char *location
#define MM_DEBUG_LOCN_FIELDS              char *location;
#define MM_DEBUG_LOCN_THRU              , location

#else

#define MM_DEBUG_LOCN_ARGS              , __FILE__, __LINE__
#define MM_DEBUG_LOCN_PARMS             , char *file, int line
#define MM_DEBUG_LOCN_FIELDS              char *file; int line;
#define MM_DEBUG_LOCN_THRU              ,       file,     line

#endif

#else

#define MM_DEBUG_LOCN_ARGS
#define MM_DEBUG_LOCN_PARMS
#define MM_DEBUG_LOCN_FIELDS
#define MM_DEBUG_LOCN_THRU

#endif /* MM_DEBUG_LOCN */


/* MM_DEBUG_TOTAL:
 *
 * Highest totals are the high-water mark since the mm pool was
 * (re)created; the total bytes allocated, and the number of objects
 * that represents (the number of objects at the time of the highest
 * byte allocation, rather than recording, separately, the highest
 * object count), and the highest fragmentation (free space at a time
 * some allocation failed (in another pool)).
 */
void mm_debug_total_highest( mm_pool_t pool,
                             size_t *alloc, int32 *obj, size_t *frag );


/* A 'watcher' function can be informed about objects in the DL and     */
/* temporary pools. Watchers may be called on events, or applied to     */
/* every live object.                                                   */

/* One argument to a watcher gives the reason for the call. That        */
/* reason, of type mm_debug_watch_t, is a value from this enum:         */

enum mm_debug_watch_e {
  MM_WATCH_ALLOC,       /* this object has just been allocated */
  MM_WATCH_FREE,        /* this object has just been freed */
  MM_WATCH_TRUNCATE,    /* this object has just been truncated */
  MM_WATCH_LIVE         /* this is a live object */
} ;

typedef int mm_debug_watch_t; /* values from mm_debug_watch_e */

/* This is the type of a 'watcher' function. The arguments are all      */
/* self-explanatory except 'seq', which is a sequence number: all       */
/* allocated objects are numbered from zero, so this can indicate the   */
/* relative age of the object.                                          */

typedef void ( * mm_debug_watcher_t )( mm_addr_t ptr,
                                       size_t oldsize,
                                       size_t size,
                                       mm_pool_t pool,
                                       mm_alloc_class_t class,
                                       size_t seq,
                                       char *file,
                                       int line,
                                       mm_debug_watch_t watch ) ;

#ifdef MM_DEBUG_WATCH

/* mm_debug_watch(f) installs 'f' as the current watcher and returns    */
/* the previous current watcher. NULL means "no watcher". If there is   */
/* a current watcher, it is invoked on every allocation and free, with  */
/* MM_WATCH_ALLOC or MM_WATCH_FREE as the 'watch' argument.             */

extern mm_debug_watcher_t mm_debug_watch( mm_debug_watcher_t watcher ) ;

/* mm_debug_watch_live(f) applies f to every live object in the DL      */
/* and temp pools.                                                      */

extern void mm_debug_watch_live( mm_debug_watcher_t watcher ) ;

/* Provide a stock watch function for general use.                      */

extern int debug_mm_watchlevel;
extern void mm_trace( mm_addr_t ptr, size_t oldsize, size_t size,
                      mm_pool_t pool, mm_alloc_class_t class,  size_t seq,
                      char *file, int line, mm_debug_watch_t watch ) ;
extern int mm_trace_close(void);

#else

#define mm_debug_watch(watch)        EMPTY_STATEMENT()
#define mm_debug_watch_live(watcher) EMPTY_STATEMENT()

#endif /* MM_DEBUG_WATCH */


/* == New log interface == */

/* Initial value for telemetry filters. */
extern unsigned long mm_telemetry_control;


/* == General Allocation and Freeing == */

#include "mm_core.h" /* Need alloc classes to be able to use allocation */


/* Free a single object in the given pool. The size must be correct.    */

#define mm_free GG_mm_free
/* rename to avoid name clash on Tornado */
extern void GG_mm_free(
  /*@notnull@*/ /*@in@*/                mm_pool_t pool,
  /*@notnull@*/ /*@out@*/ /*@only@*/    mm_addr_t what,
                                        size_t size ) ;

/* Truncate an existing allocation. The oldsize must be the correct     */
/* size of the current allocation; newsize specifies the required size  */
/* after the truncation, and must be less than oldsize; data from base  */
/* to base + newsize is guaranteed to be unchanged.                     */
/* Note: this routine is not _really_ intended for general use.         */

void mm_truncate( mm_pool_t pool,
                  mm_addr_t base,
                  size_t oldsize,
                  size_t newsize ) ;

/* Allocation is macroized to take a 'class' argument. The interface    */
/* is through three facilities (either macros or functions):            */

/* mm_alloc(pool,size,class)                                            */

/* allocates a single object in the given pool. The size must be        */
/* non-zero. Returns NULL if the allocation fails.                      */

/* mm_alloc_multi_homo(pool,count,size,class,returns)                   */
/* mm_alloc_multi_hetero(pool,count,size,classes,returns)               */

/* perform multiple allocations. _homo allocates many objects of a      */
/* single size and class. _hetero allocates many objects of             */
/* independent sizes and classes. If all the allocations succeed, the   */
/* call returns MM_SUCCESS. If any allocation fails, all the allocated  */
/* objects are freed and the call returns MM_FAILURE.                   */

extern /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t mm_alloc_class(
  /*@notnull@*/ /*@in@*/        mm_pool_t pool,
                                size_t size,
                                mm_alloc_class_t class
                                MM_DEBUG_LOCN_PARMS )
     /*@ensures MaxSet(result) == (size - 1); @*/ ;

extern /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t  mm_alloc_noclass(
  /*@notnull@*/ /*@in@*/        mm_pool_t pool,
                                size_t size
                                MM_DEBUG_LOCN_PARMS )
     /*@ensures MaxSet(result) == (size - 1); @*/ ;

extern /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t mm_alloc_cost_class(
  /*@notnull@*/ /*@in@*/        mm_pool_t pool,
                                size_t size,
                                mm_cost_t cost,
                                mm_alloc_class_t class
                                MM_DEBUG_LOCN_PARMS )
     /*@ensures MaxSet(result) == (size - 1); @*/ ;

extern /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t  mm_alloc_cost_noclass(
  /*@notnull@*/ /*@in@*/        mm_pool_t pool,
                                size_t size,
                                mm_cost_t cost
                                MM_DEBUG_LOCN_PARMS )
     /*@ensures MaxSet(result) == (size - 1); @*/ ;

extern /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t  mm_sac_alloc_class(
  /*@notnull@*/ /*@in@*/        mm_pool_t pool,
                                size_t size,
                                mm_alloc_class_t class
                                MM_DEBUG_LOCN_PARMS )
     /*@ensures MaxSet(result) == (size - 1); @*/ ;

extern /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t  mm_sac_alloc_noclass(
  /*@notnull@*/ /*@in@*/        mm_pool_t pool,
                                size_t size
                                MM_DEBUG_LOCN_PARMS )
     /*@ensures MaxSet(result) == (size - 1); @*/ ;

extern mm_result_t mm_alloc_multi_homo_class(
                       /*@notnull@*/ /*@in@*/ mm_pool_t pool,
                                              size_t count,
                                              size_t size,
                                              mm_alloc_class_t class,
                                              mm_addr_t returns[]
                                              MM_DEBUG_LOCN_PARMS ) ;

extern mm_result_t mm_alloc_multi_homo_noclass(
                         /*@notnull@*/ /*@in@*/ mm_pool_t pool,
                                                size_t count,
                                                size_t size,
                                                mm_addr_t returns[]
                                                MM_DEBUG_LOCN_PARMS ) ;

extern mm_result_t mm_alloc_multi_hetero_class(
                         /*@notnull@*/ /*@in@*/ mm_pool_t pool,
                                                size_t count,
                                                size_t sizes[],
                                                mm_alloc_class_t classes[],
                                                mm_addr_t returns[]
                                                MM_DEBUG_LOCN_PARMS ) ;

extern mm_result_t mm_alloc_multi_hetero_noclass(
                           /*@notnull@*/ /*@in@*/ mm_pool_t pool,
                                                  size_t count,
                                                  size_t sizes[],
                                                  mm_addr_t returns[]
                                                  MM_DEBUG_LOCN_PARMS ) ;

#ifdef MM_DEBUG_ALLOC_CLASS
/* = = = MM_DEBUG_ALLOC_CLASS information passed through = = = */
/* - - - automatically insert DEBUG_LOCN info - - - */
#define mm_alloc(pool, size, class) \
  (mm_alloc_class(pool, size, class MM_DEBUG_LOCN_ARGS))

#define mm_alloc_cost(pool, size, cost, class) \
  (mm_alloc_cost_class(pool, size, cost, class MM_DEBUG_LOCN_ARGS))

#define mm_sac_alloc(pool, size, class) \
  (mm_sac_alloc_class(pool, size, class MM_DEBUG_LOCN_ARGS))

#define mm_alloc_multi_homo(pool, count, size, class, returns ) \
  (mm_alloc_multi_homo_class(pool, count, size, class, returns MM_DEBUG_LOCN_ARGS))

#define mm_alloc_multi_hetero(pool, count, sizes, classes, returns) \
  (mm_alloc_multi_hetero_class(pool, count, sizes, classes, returns MM_DEBUG_LOCN_ARGS))

/* - - - for passing through DEBUG_LOCN info - - - */
#define mm_alloc_thru(pool, size, class  ) \
  (mm_alloc_class(pool, size, class  MM_DEBUG_LOCN_THRU))

#define mm_alloc_multi_homo_thru(pool, count, size, class, returns ) \
  (mm_alloc_multi_homo_class(pool, count, size, class, returns MM_DEBUG_LOCN_THRU ))

#define mm_alloc_multi_hetero_thru(pool, count, sizes, classes, returns ) \
  (mm_alloc_multi_hetero_class(pool, count, sizes, classes, returns MM_DEBUG_LOCN_THRU ))

#else /* ! MM_DEBUG_ALLOC_CLASS */

/* = = = MM_DEBUG_ALLOC_CLASS information silently discarded = = = */
/* - - - automatically insert DEBUG_LOCN info - - - */
#define mm_alloc(pool, size, class) \
  ((void)(class), mm_alloc_noclass(pool, size MM_DEBUG_LOCN_ARGS))

#define mm_alloc_cost(pool, size, cost, class) \
  ((void)(class), mm_alloc_cost_noclass(pool, size, cost MM_DEBUG_LOCN_ARGS))

#define mm_sac_alloc(pool, size, class) \
  ((void)(class), mm_sac_alloc_noclass(pool, size MM_DEBUG_LOCN_ARGS))

#define mm_alloc_multi_homo(pool, count, size, class, returns) \
  ((void)(class), mm_alloc_multi_homo_noclass(pool, count, size, returns MM_DEBUG_LOCN_ARGS))

#define mm_alloc_multi_hetero(pool, count, sizes, classes, returns) \
  ((void)((classes)[0]), mm_alloc_multi_hetero_noclass(pool, count, sizes, returns MM_DEBUG_LOCN_ARGS))

/* - - - for passing through DEBUG_LOCN info - - - */
#define mm_alloc_thru(pool, size, class ) \
  ((void)(class), mm_alloc_noclass(pool, size MM_DEBUG_LOCN_THRU))

#define mm_alloc_multi_homo_thru(pool, count, size, class, returns ) \
  ((void)(class), mm_alloc_multi_homo_noclass(pool, count, size, returns MM_DEBUG_LOCN_THRU))

#define mm_alloc_multi_hetero_thru(pool, count, sizes, classes, returns ) \
  ((void)((classes)[0]), mm_alloc_multi_hetero_noclass(pool, count, sizes, returns MM_DEBUG_LOCN_THRU))

#endif /* MM_DEBUG_ALLOC_CLASS */


/* == SACs (segregated allocation caches) == */

/* See mm_sac_alloc, mm_sac_alloc_class, and mm_sac_alloc_noclass above */

extern void mm_sac_free( mm_pool_t pool,
                         mm_addr_t what,
                         size_t size ) ;

/* Defines class configuration for sac, for up to MPS_SAC_CLASS_LIMIT
 * (currently, 8) classes.
 */
typedef struct mm_sac_classes_t {
  size_t block_size ;   /* Max size cached by this class */
  size_t cached_count ; /* Max number of allocs cached for this class */
  unsigned  frequency ;    /* Approx frequency of allocs for this class */
} *mm_sac_classes_t ;

extern mm_result_t mm_sac_create( mm_pool_t pool,
                                  mm_sac_classes_t classes,
                                  size_t count ) ;
extern void mm_sac_flush( mm_pool_t pool ) ;
extern void mm_sac_destroy( mm_pool_t pool ) ;
extern mm_result_t mm_sac_present( mm_pool_t pool ) ;

/** Get the SAC from the pool. */
extern mps_sac_t mm_pool_sac( mm_pool_t pool );


/* == APs (allocation points, pointer-incrementing cache) == */

/** Create an ap on the given pool. */
mm_result_t mm_ap_create( mps_ap_t *ap, mm_pool_t pool );

/** Destroy an ap, associated with the given pool. */
void mm_ap_destroy( mps_ap_t ap, mm_pool_t pool );

/** Allocate from an ap, not thread-safe. */
#define MM_AP_ALLOC(p, ap, size) MACRO_START \
  do { \
    mps_res_t mps_res_; \
    MPS_RESERVE_BLOCK(mps_res_, p, ap, size); \
    if ( mps_res_ != MPS_RES_OK ) { \
      p = NULL; break;             \
    } \
  } while ( !mps_commit(ap, p, size) ); \
MACRO_END


/* Alignment macros for sizes
 *
 * P2 versions are for alignments that are powers of two.
 * Note: PTR_ALIGN_UP doesn't work for a null pointer.
 */

#if ! defined( ASSERT_BUILD ) /* { */

#define SIZE_ALIGN_DOWN_P2(x, a) ((x) & ~((size_t)(a) - 1))
#define SIZE_ALIGN_UP_P2(x, a) SIZE_ALIGN_DOWN_P2((x) + (a) - 1, a)
#define SIZE_IS_ALIGNED_P2(x, a) (((x) & ((size_t)(a) - 1)) == 0)

#else /* } { */

#define IS_POWER_OF_2(a) (((a) & ((a) - 1)) == 0)

#define SIZE_ALIGN_DOWN_P2(x, a) \
  (IS_POWER_OF_2(a) ? (void)0 : HQFAIL("Alignment was not 2^n in a P2 macro"), \
   (x) & ~((size_t)(a) - 1))
#define SIZE_ALIGN_UP_P2(x, a) \
  (IS_POWER_OF_2(a) ? (void)0 : HQFAIL("Alignment was not 2^n in a P2 macro"), \
   SIZE_ALIGN_DOWN_P2((x) + (a) - 1, a))
#define SIZE_IS_ALIGNED_P2(x, a) \
  (IS_POWER_OF_2(a) ? (void)0 : HQFAIL("Alignment was not 2^n in a P2 macro"), \
   ((x) & ((size_t)(a) - 1)) == 0)

#endif /* ! defined( ASSERT_BUILD ) } */


#define SIZE_ALIGN_DOWN(x, a) ((x) - (x) % (size_t)(a))
#define SIZE_ALIGN_UP(x, a) SIZE_ALIGN_DOWN((x) + (a) - 1, a)
#define SIZE_IS_ALIGNED(x, a) ((x) % (size_t)(a) == 0)


#if ! defined( ASSERT_BUILD ) /* { */

#define PTR_ALIGN_DOWN_P2(type, x, a) \
  ((type)((uintptr_t)(x) & ~((uintptr_t)(a) - 1)))
#define PTR_ALIGN_UP_P2(type, x, a) \
  ((type)(((uintptr_t)(x) + (a) - 1) & ~((uintptr_t)(a) - 1)))
#define PTR_IS_ALIGNED_P2(type, x, a) \
  (((uintptr_t)(x) & ((uintptr_t)(a) - 1)) == 0)

#else /* } { */

#define PTR_ALIGN_DOWN_P2(type, x, a) \
  (IS_POWER_OF_2(a) ? (void)0 : HQFAIL("Alignment was not 2^n in a P2 macro"), \
   (type)((uintptr_t)(x) & ~((uintptr_t)(a) - 1)))
#define PTR_ALIGN_UP_P2(type, x, a) \
  (IS_POWER_OF_2(a) ? (void)0 : HQFAIL("Alignment was not 2^n in a P2 macro"), \
   (type)(((uintptr_t)(x) + (a) - 1) & ~((uintptr_t)(a) - 1)))
#define PTR_IS_ALIGNED_P2(type, x, a) \
  (IS_POWER_OF_2(a) ? (void)0 : HQFAIL("Alignment was not 2^n in a P2 macro"), \
   ((uintptr_t)(x) & ((uintptr_t)(a) - 1)) == 0)

#endif /* ! defined( ASSERT_BUILD ) } */


#define PTR_ALIGN_DOWN(type, x, a) \
  ((type)((char*)(x) - (uintptr_t)(x) % (uintptr_t)(a)))
#define PTR_ALIGN_UP(type, x, a) \
  ((type)((char*)(x) + (a) - 1 - ((uintptr_t)(x) - 1) % (uintptr_t)(a)))
#define PTR_IS_ALIGNED(type, x, a) \
  ((uintptr_t)(x) % (uintptr_t)(a) == 0)

/* Alignment macros for any integer types. (u)intptrt, (u)int128,
   (u)int64, (u)int32, (u)int16, (u)int8. The intention is that a WORD
   is 4 bytes and a DWORD is 8 bytes. When checking if a pointer is
   aligned, use uintptr_t or intptr_t, NOT void*. Examples:

     Bool is_aligned = DWORD_IS_ALIGNED(uint32, my_uint) ;
     Bool is_aligned = DWORD_IS_ALIGNED(uintptr_t, my_ptr)
     my_ptr = WORD_ALIGN_DOWN(uintptr_t, old_ptr) ;

  These are deprecated, because they hide the constant 4/8 -- you
  usually want one of the SIZE_ macros.
*/

#define DWORD_ALIGN_UP(type, x)       (((type)(x)+7)&~7)
#define DWORD_ALIGN_DOWN(type, x)     ((type)(x)&~7)
#define DWORD_IS_ALIGNED(type, x)     (((type)(x)&7)==0)

#define WORD_ALIGN_UP(type, x)        (((type)(x)+3)&~3)
#define WORD_ALIGN_DOWN(type, x)      ((type)(x)&~3)
#define WORD_IS_ALIGNED(type, x)      (((type)(x)&3)==0)

/** \} */

/*
* Log stripped */

#endif /* __MM_H__ */
