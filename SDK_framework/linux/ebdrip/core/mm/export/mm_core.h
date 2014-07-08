/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:mm_core.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support from the Core RIP for the Memory Manager
 *
 * This file is part of the Core RIP. It provides any types and/or
 * values required by the memory manager (which are not part of std.h),
 * and also any interfaces of the MM specialized for the Core.
 */

#ifndef __MM_CORE_H__
#define __MM_CORE_H__


/* == Allocation classes == */

/* All allocations take a "class" argument, to use for debugging        */
/* (memory maps, &c). It is macroized away when not debugging. Classes  */
/* are of the type mm_alloc_class_t, and take values from this enum.    */

enum mm_alloc_class_e {
#define MM_ALLOC_CLASS(name) MM_ALLOC_CLASS_##name,
#ifndef DOXYGEN_SKIP
#include "mm_class.h"
#endif /* !DOXYGEN_SKIP */
  MM_ALLOC_CLASS_LIMIT /* terminate the list, and provide size */
#undef MM_ALLOC_CLASS
} ;


/* == Save levels == */


/* The following values used to be defined in psvm.h. The memory        */
/* manager needs them to manage PostScript virtual memory.              */

#define MINSAVELEVEL   0
#define MAXSAVELEVELS 31
#define SAVELEVELINC   2
#define NUMBERSAVES(_sl)     ((_sl)>>1)

/* This is the maximum save level supported in global PS VM. For        */
/* instance, a value of 1 here means that global PS VM supports save    */
/* levels 0 and 1 and 2 (sic). */

#define MAXGLOBALSAVELEVEL 1


/** Controls automatic GCs: -2: none, -1: global only, 0: local and global. */
extern int gc_mode;

/** Counter to indicate if it's safe to GC, > 0 is unsafe. */
extern int gc_safety_level;

/** Notify that this operator has been checked to be safe for GC. */
#define gc_safe_in_this_operator() --gc_safety_level

/** Notify that the following code has not been vefired as safe for GC. */
#define gc_unsafe_from_here_on() ++gc_safety_level


#if defined(DEBUG_BUILD)
/** Debugging counter, giving the number of times to fail the low memory
    test, forcing low-memory handling. */
extern unsigned debug_lowmemory_count ;
#endif


/* == Pools == */


/* Pool types - expand as required (just make sure the mm_pooltype_map  */
/* table in SWmm_common!src:vm.c table is updated accordingly).         */

enum mm_pooltype_e {
  DL_POOL_TYPE = 0,
  DL_FAST_POOL_TYPE,
  TEMP_POOL_TYPE,
  COLOR_POOL_TYPE,
  COC_POOL_TYPE,
  TABLE_POOL_TYPE,
  PCL_POOL_TYPE,
  PCLXL_POOL_TYPE,
  PSVM_POOL_TYPE,
  PSVM_DEBUG_POOL_TYPE,
  PSVMFN_POOL_TYPE,
  PSVMFN_DEBUG_POOL_TYPE,
  PDF_POOL_TYPE,
  IRR_POOL_TYPE,
  TRAP_POOL_TYPE,
  IMBFIX_POOL_TYPE,
  IMBVAR_POOL_TYPE,
  RSD_POOL_TYPE,
  TIFF_POOL_TYPE,
  SHADING_POOL_TYPE,
  XML_PARSE_POOL_TYPE,
  XML_SUBSYSTEM_POOL_TYPE,
  BAND_POOL_TYPE,
  BDSTATE_POOL_TYPE,
  BDDATA_POOL_TYPE,
  RLE_POOL_TYPE,
  HTFORM_POOL_TYPE,
  NUM_POOL_TYPES
} ;


/* Pool parameters (correspond to pool types listed in mm_pooltype_e):
 * For epdl and epdr based pools, parameters are
 *  ExtendBy, AvgSize, Alignment
 * For mv based pools, parameters are
 *  ExtendBy, AvgSize, MaxSize (alignment is fixed at 8 bytes for mv)
 * For mvff, parameters are
 *  ExtendBy, AvgSize, Alignment, slotHigh, arenaHigh, firstFit
 *
 * The ExtendBy value of 65536 (for dl and temp pools) is roughly analogous
 * to the old ScriptWorks behaviour; the other values are just made up!
 */

/* These pools now share the same paramters; any pool-specific parameters
 * are hidden inside vm.c along with the identifier of the poolclass
 * to use.
 * Pool parameters required here are:
 *  ExtendBy, AvgSize, Alignment
 */

#ifdef PLATFORM_IS_64BIT
#define MM_MINIMUM_POOL_ALIGN ((size_t)8)
#else
#define MM_MINIMUM_POOL_ALIGN ((size_t)4)
#endif

#define MM_SEGMENT_SIZE ((size_t)65536)

#define MM_DL_POOL_ALIGN      MM_MINIMUM_POOL_ALIGN
#define MM_TABLE_POOL_ALIGN   MM_MINIMUM_POOL_ALIGN
#define MM_TIFF_POOL_ALIGN    MM_MINIMUM_POOL_ALIGN
#if defined (Solaris)
/* Solaris Sparc required doubles to be 8-byte aligned */
#define MM_XML_POOL_ALIGN     ((size_t)8)
#else
#define MM_XML_POOL_ALIGN     MM_MINIMUM_POOL_ALIGN
#endif
#define MM_TEMP_POOL_ALIGN    ((size_t)8)
#define MM_COLOR_POOL_ALIGN   ((size_t)8)
#define MM_PDF_POOL_ALIGN     ((size_t)8)
#define MM_PCL_POOL_ALIGN     ((size_t)8)
#define MM_PCLXL_POOL_ALIGN   ((size_t)8)
#define MM_SHADING_POOL_ALIGN ((size_t)8)
#define MM_COC_POOL_ALIGN     ((size_t)512)
#define MM_RSD_POOL_ALIGN     ((size_t)512)

/* Trapping DL pool needs to be 8 byte aligned. The structures
 * allocated by trapper include doubles and thus 8 byte alignment is a
 * requirement for Sparc, Irix and Mac.
 */
#define MM_TRAP_POOL_ALIGN    ((size_t)8)

#define DL_POOL_PARAMS          MM_SEGMENT_SIZE, ( size_t )512,   MM_DL_POOL_ALIGN
#define TABLE_POOL_PARAMS       MM_SEGMENT_SIZE, ( size_t )64,    MM_TABLE_POOL_ALIGN
#define TIFF_POOL_PARAMS        MM_SEGMENT_SIZE, ( size_t )512  , MM_TIFF_POOL_ALIGN
#define XML_POOL_PARAMS         MM_SEGMENT_SIZE, ( size_t )512,   MM_XML_POOL_ALIGN
#define TEMP_POOL_PARAMS        MM_SEGMENT_SIZE, ( size_t )32,    MM_TEMP_POOL_ALIGN
#define COLOR_POOL_PARAMS       MM_SEGMENT_SIZE, ( size_t )64,    MM_COLOR_POOL_ALIGN
#define PDF_POOL_PARAMS         MM_SEGMENT_SIZE, ( size_t )32,    MM_PDF_POOL_ALIGN
#define PCL_POOL_PARAMS         MM_SEGMENT_SIZE, ( size_t )32,    MM_PCL_POOL_ALIGN
#define PCLXL_POOL_PARAMS       MM_SEGMENT_SIZE, ( size_t )32,    MM_PCLXL_POOL_ALIGN
#define SHADING_POOL_PARAMS(_s) MM_SEGMENT_SIZE, ( size_t )(_s) , MM_SHADING_POOL_ALIGN
#define COC_POOL_PARAMS         MM_SEGMENT_SIZE, ( size_t )8192,  MM_COC_POOL_ALIGN
#define RSD_POOL_PARAMS         MM_SEGMENT_SIZE, ( size_t )16384, MM_RSD_POOL_ALIGN
#define TRAP_POOL_PARAMS        MM_SEGMENT_SIZE, ( size_t )512,   MM_TRAP_POOL_ALIGN


extern mm_pool_t mm_pool_color ;     /* color chains et al. */
extern mm_pool_t mm_pool_coc ;       /* color cache */

extern mm_pool_t mm_pool_temp ;      /* all other memory */


/* == PostScript VM == */


extern mm_pool_t mm_pool_ps_local ;  /* PostScript local VM */
extern mm_pool_t mm_pool_ps_global ; /* PostScript global VM */
extern mm_pool_t mm_pool_ps ;        /* One of the above */
extern mm_pool_t mm_pool_ps_typed_local ;  /* PostScript local VM, typed */
extern mm_pool_t mm_pool_ps_typed_global ; /* PostScript global VM, typed */
extern mm_pool_t mm_pool_ps_typed ;        /* One of the above */


/* allocating PostScript strings and objects. The sizes are in _bytes_ */
mm_addr_t mm_ps_alloc_obj(mm_pool_t pool, size_t size);
extern mm_addr_t mm_ps_alloc_typed( mm_pool_t pool, size_t size );
extern mm_addr_t mm_ps_alloc_weak( mm_pool_t pool, size_t size );
extern mm_addr_t mm_ps_alloc_string( mm_pool_t pool, size_t size );

/* Shorthand macros for local, global and current object and string
   allocation */
#define get_lomemory( size )  ((OBJECT *)mm_ps_alloc_obj(mm_pool_ps_local , (size) * sizeof(OBJECT)))
#define get_gomemory( size )  ((OBJECT *)mm_ps_alloc_obj(mm_pool_ps_global, (size) * sizeof(OBJECT)))
#define get_lsmemory( size )  ((uint8 *)mm_ps_alloc_string(mm_pool_ps_local , (size)))
#define get_gsmemory( size )  ((uint8 *)mm_ps_alloc_string(mm_pool_ps_global, (size)))

#define get_omemory( size )  ((OBJECT *)mm_ps_alloc_obj(mm_pool_ps, (size) * sizeof(OBJECT)))
#define get_smemory( size )  ((uint8 *)mm_ps_alloc_string(mm_pool_ps, (size)))

#define MM_PS_TYPED_ALIGNMENT (8)

#define MM_PS_ALIGNMENT (sizeof(OBJECT))


/* Saving and Restoring. PSVM pools support a notion of "current save   */
/* level". Every object is allocated "at" the current save level.       */
/* mm_ps_save increments the save level on the local pool (and on the   */
/* global pool if the old level is MAXGLOBALSAVELEVEL or below).        */
/* mm_ps_restore restores to a lower level, freeing all objects         */
/* allocated above that level.                                          */

/* mm_ps_save(slevel) performs a save in PSVM. slevel is the save       */
/* level _after_ the save.                                              */

extern void mm_ps_save( size_t level );

/* mm_ps_restore(slevel) notifies the MM of a restore. slevel is the    */
/* save level to which we are restoring.                                */

extern void mm_ps_restore( size_t level );

/* mm_ps_check (level, what) checks that 'what' does not point into     */
/* PSVM allocated at a save level higher than 'level'. Returns          */
/* MM_SUCCESS if it does not (this includes pointers which are not to   */
/* PSVM at all, and NULL pointers).                                     */

extern mm_result_t mm_ps_check( size_t level, mm_addr_t what );

/* garbage_collect (do_local, do_global) does a forced GC in PS VM.  The
 * caller specifies which of local and global VM are collected. */

Bool garbage_collect(Bool do_local, Bool do_global);


/* == DL Allocation Promises == */

/* If a 'promise' has been given, then allocations to the promised      */
/* amount will all succeed. Only one promise can be issued per pool at  */
/* any one time.                                                        */

/* A 'promise' is obtained by mm_dl_promise(pool, total_size). If this  */
/* does not return MM_SUCCESS, there was insufficient memory and the    */
/* promise is not issued.                                               */

extern mm_result_t mm_dl_promise( mm_pool_t pool, size_t size );

/* The next piece of promised memory is obtained by                     */
/* mm_dl_promise_next(pool,next_size). This fails with an assert if the */
/* promise is exhausted.                                                */

extern mm_addr_t mm_dl_promise_next( mm_pool_t pool, size_t size );

/* Promise memory can be reduced while it is still active by            */
/* mm_dl_promise_shrink(pool,bytes_to_reduce_by).                       */
/* This fails with an assert if the you try to reduce it below 0        */

extern void mm_dl_promise_shrink( mm_pool_t pool, size_t size );

/* When the promised allocation is complete, mm_dl_promise_end(pool)    */
/* ends the promise and allows any unused promised memory to be         */
/* re-used. The final size of the promise is returned                   */

extern size_t mm_dl_promise_end( mm_pool_t pool ) ;

/* If the currently promised allocation turns out to be un-needed,      */
/* mm_dl_promise_free(pool) abandons the promise and frees any memory   */
/* allocated from the promise. It can also be called after a promise is */
/* ended, in which case it will free the last promise.                  */

extern void mm_dl_promise_free( mm_pool_t pool ) ;


/* object_finalize - finalize an object */

extern void object_finalize( mm_addr_t obj ) ;


#if defined( ASSERT_BUILD )
/** Control of debugging trace for low memory. */
extern Bool debug_lowmemory;
#endif


/* mm_set_gc_threshold -- set GC threshold and link to alert flag */

double mm_set_gc_threshold(double threshold, Bool *alert);

/* mm_gc_threshold_exceeded -- has GC threshold been exceeded? */

Bool mm_gc_threshold_exceeded(void);


#endif  /* __MM_CORE_H__ */

/* Log stripped */
