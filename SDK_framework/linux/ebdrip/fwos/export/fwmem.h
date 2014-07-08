/* Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWMEM_H__
#define __FWMEM_H__

/* $HopeName: HQNframework_os!export:fwmem.h(EBDSDK_P.1) $
 * FrameWork External Memory Manager Interface
 */

/* Log stripped */

/* This file provides FrameWork's memory management.
 *
 * The following functions are provided:
 *
 * General-purpose allocator:
 *   FwMemAlloc - allocate a block of memory. Various options are allowed,
 *                such as exiting if the allocation fails, or filling the
 *                block with zeroes (see below).
 *   FwMemFree  - free a block of memory
 *   FwMemAllocSafely - allocate memory to a pointer, which must be NULL.
 *                This is intended to reduce memory leaks by reassignment
 *                of a pointer without a preceeding free.
 *   FwMemFreeSafely - the companion to FwMemAllocSafely, which frees
 *                a non-NULL pointer, and sets the pointer to NULL.
 *   FwMemFreeSize - Fills a sized block of memory with garbage (in debug
 *                build) and then frees it.
 *   FwMemFreeSizeSafely - a combination of FwMemFreeSize and FwMemFreeSafely
 *
 * Special-purpose allocators:
 *   FwMemPoolCreate - create a special-purpose memory pool, which will
 *                     be one of the types below.
 *   FwMemPoolDestroy - destroy a special-purpose memory pool, returning
 *                      all associated memory to the system.
 *   FwMemPoolAlloc - allocate a block of memory from a specific pool.
 *   FwMemPoolFree - free a block of memory allocated from a specific pool.
 *                   This can be a no-op on some pools.
 * List of supported memory pool types:
 *  FWMEM_POOL_ALLOC_ONLY - a fast space-efficient suballocator
 *
 *
 * Several levels of debugging support are provided, see below for
 * information.
 *
 * Debugging support functions provided are:
 *   FwMemReport - list all known allocated blocks.
 *   fFwIsPtrValid - checks that a pointer points to within an
 *                allocated block.
 *   FwMemCheckBlocks - checks all allocated blocks for overwrites/corruption
 */

/* see fwcommon.h */
#include "fwcommon.h"  /* Common */
                       /* Is External */
#include "fxmem.h"     /* Platform Dependent */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
#define FWMEM_DEBUG
#endif /* DEBUG_BUILD */

/* Extra flags to the memory allocation function
 * FwMemAlloc requires one of the ALLOC_FAIL flags, OR'd with one
 * of the INIT_ flags. There are no defaults.
 */

/* If the alloc fails, return NULL */
#define FWMEM_ALLOC_FAIL_NULL  (1 << 0)

/* If the alloc fails, then exit the application */
#define FWMEM_ALLOC_FAIL_EXIT  (1 << 1)

/* Initialize allocated memory with zeros */
#define FWMEM_INIT_ZEROED      (1 << 2)

/* Leave allocated memory uninitialized */
#define FWMEM_INIT_UNSPECIFIED (1 << 3)

/* This should be the maximum possible flag value, and is used for limit
 * checking.
 */
#define FWMEM_ALLOC_LIMIT      ((1 << 4) - 1)
typedef uint32 FwMemAllocFlag;

/* Levels of debugging support (only take effect when FWMEM_DEBUG is defined) */

/* No debugging support at all.
 */
#define FWMEM_DBGLEV_NONE      (0)

/* Fills in memory on initial allocation, and fills in memory
 * with calls to FwMemFreeSize.
 * Keeps track of attempts to free memory twice.
 * Keeps count of the net number of allocs made through FwMemAlloc/Free.
 */
#define FWMEM_DBGLEV_1         (1)

/* Previous debug level, plus:
 * Fills in memory on all FwMemFree calls (not just FwMemFreeSize)
 * Adds tracking of where in the source an allocation/free was made
 * (allows printout with FwMemReport).
 * Allows bounds checking (use FwMemCheckBlocks)
 * Allows valid pointer checking (see fFwMemValidPtr).
 */
#define FWMEM_DBGLEV_2         (2)

/* Maximum allowed debug level - used for limit checking
 */
#define FWMEM_DBGLEV_LIMIT     (2)

/*
 * Context for this library
 */

typedef void* (*alloc_fp)( size_t cbSize );
typedef void (*free_fp)( void* pvBlock );

typedef struct FwMemContext_s {
  alloc_fp                      alloc;
  free_fp                       free;
  uint32                        debug_level;
#ifdef FW_PLATFORM_MEM_CONTEXT
  FwPlatformMemContext          platform;
#endif
} FwMemContext;


struct FwMemPool;
typedef struct FwMemPool FwMemPool;

/* ----- Basic Level Memory Management Functions ----- */

/*
 * These macros just call the support functions defined below, but each
 * macro provides the function with the filename and line number of
 * where it has been called.
 */

/*
 * Allocate a block of memory, and act on the result according
 * to the alloc flags.
 */
#ifdef FWMEM_DEBUG
#define FwMemAlloc(cbSize, AllocFlags) (FcMemAlloc_INTERNAL( \
    (cbSize), \
    (AllocFlags), \
    (__FILE__), \
    (__LINE__) \
    ))
#else /* !FWMEM_DEBUG */
#define FwMemAlloc(cbSize, AllocFlags) (FcMemAlloc_INTERNAL( \
    (cbSize), \
    (AllocFlags) \
    ))
#endif /* FWMEM_DEBUG */

/*
 * Allocate a block of memory, and act on the result according
 * to the alloc flags. This is the companion to FwMemFreeSafely
 */
#define FwMemAllocSafely(ppvBlock, cbSize, AllocFlags) MACRO_START \
    HQASSERT(NULL != (ppvBlock), "NULL ppv" "Block"); \
    HQASSERT(NULL == *(ppvBlock), "Unsafe alloc"); \
    *(ppvBlock) = FwMemAlloc((cbSize), (AllocFlags)); \
MACRO_END

/*
 * Free a block of memory.
 */

#ifdef FWMEM_DEBUG
#define FwMemFree(pvBlock) (FcMemFree_INTERNAL( \
    (pvBlock), \
    (__FILE__), \
    (__LINE__) \
    ))
#else /* !FWMEM_DEBUG */
#define FwMemFree(pvBlock) (FcMemFree_INTERNAL((pvBlock)))
#endif /* FWMEM_DEBUG */


/*
 * Free a block of memory of a given size,
 * first filling it with garbage (in the debug build only)
 */
#ifdef FWMEM_DEBUG
#define FwMemFreeSize(pvBlock, cbSize) (FcMemFreeSize_INTERNAL( \
    (pvBlock), \
    (cbSize), \
    (__FILE__), \
    (__LINE__) \
    ))
#else /* !FWMEM_DEBUG */
#define FwMemFreeSize(pvBlock, cbSize) (FcMemFree_INTERNAL((pvBlock)))
#endif


/*
 * Free a block of memory, and destroy the pointer to it.
 * This is the companion to FwMemAllocSafely
 */
#define FwMemFreeSafely(ppvBlock) MACRO_START \
  HQASSERT(NULL != (ppvBlock), "NULL ppv" "Block"); \
  if( NULL != *(ppvBlock) ) { \
    FwMemFree(*(ppvBlock)); \
    *(ppvBlock) = NULL; \
  } \
MACRO_END


/*
 * Free a block of memory of a given size, and destroy the pointer to it.
 */
#define FwMemFreeSizeSafely(ppvBlock, cbSize) MACRO_START \
  HQASSERT(NULL != (ppvBlock), "NULL ppv" "Block"); \
  if( NULL != *(ppvBlock) ) { \
    FwMemFreeSize(*(ppvBlock), (cbSize)); \
    *(ppvBlock) = NULL; \
  } \
MACRO_END

extern void FwMemExitMemoryExhausted( void );

/* ----- END Basic Level Memory Management Functions ----- */

/* ----- Higher level Memory Management Functions ----- */

#ifdef FWMEM_DEBUG

/* Does a pointer point to within a block allocated by this
 * library?
 * On less than FWMEM_DBGLEV_2 debugging this always returns TRUE.
 */
extern int32 fFwMemPtrValid(void* pv);

/*
 * Reports information on all allocated blocks
 */
extern void FwMemReport(void);

/*
 * Reports information on overwritten blocks
 */
extern void FwMemCheckBlocks(void);

/*
 * Resets the counts of total and live allocations to 0
 */
extern void FwMemResetAllocationTracking(void);

#else /* !FWMEM_DEBUG */

#define fFwMemPtrValid(pv)  (TRUE)
#define FwMemReport()       EMPTY_STATEMENT()
#define FwMemCheckBlocks()  EMPTY_STATEMENT()

#endif /* FWMEM_DEBUG */

/* Obtain the theoretical user address space for a process running on
 * this platform, measured in K. The value is supplied to a
 * caller-allocated unsigned 64-bit integer, although on 32-bit
 * platforms it will never be necessary to use the high-order word. We
 * use the 64-bit structure in anticipation of future situations where
 * a 32-bit measurement is not sufficient.
 *
 * On a 32-bit platform, the theoretical range of addresses for a
 * process is 4 gigabytes (0x00000000 - 0xFFFFFFFF). Many platforms do
 * not allow user code to work with addresses throughout this
 * range. This function takes account of the knowledge we have about
 * the various platforms, and returns an appropriate value.
 */
extern void FwMemGetMaxAddressSpaceSizeInK( /* out */ HqU32x2 *pSize );

/* Obtain the theoretical user address space for a process running on
 * this platform, measured in bytes.
 */
extern void FwMemGetMaxAddressSpaceSizeInBytes( /* out */ size_t *pSize );

/* ----- END Higher level Memory Management Functions ----- */

/* ----- Special-purpose Memory Pool Functions -----   */

/* List of provided pool classes. */
#define FWMEM_POOL_ALLOC_ONLY   0

/* Number of supported pool classes; used for limit checking. */
#define FWMEM_POOL_LIMIT        1

/* Largest alignment supported by mem pools. */
#define FWMEM_MAX_ALIGNMENT     8

/* Create a special-purpose pool of the specified class.  AllocFlags
 * will apply to all allocations.  Alignment must be a power of two.  If
 * created, the pool should be able to allocate at least one block of
 * size cbInitialSize; if the pool can be extended, cbGrowExtra is the
 * minimum block size that can be allocated following the request that
 * caused the pool extension before it needs to be extended again.
 */
extern FwMemPool * FwMemPoolCreate
(
  uint32            nClass,
  size_t            cbInitialSize,
  size_t            cbGrowExtra,
  uint32            alignment,
  FwMemAllocFlag    AllocFlags
);

/*
 * Release all memory allocated from pool and destroy it.
 */
extern void FwMemPoolDestroy( FwMemPool ** ppPool );

/*
 * Allocate memory from pool.
 */
extern void * FwMemPoolAlloc( FwMemPool * pPool, size_t cbSize );

/*
 * Free memory block allocated from pool, if supported.
 */
extern void FwMemPoolFree( FwMemPool * pPool, void * pvBlock );

/* ----- END Special-purpose Memory Pool Functions -----   */

/* ----- Support functions ----- */

/* These should never be called directly, use the macros instead.
 * The macros provide the function with the filename and line number of each
 * allocated block, which aids in tracking memory leaks.
 */

/*
 * Allocate a block of memory
 */
extern void* FcMemAlloc_INTERNAL(
    size_t          cbSize,
    FwMemAllocFlag  AllocFlags
#ifdef FWMEM_DEBUG
    ,const char*    pszFile,
    uint32          nLine
#endif /* FWMEM_DEBUG */
    );


/*
 * Free a block of memory
 */
extern void FcMemFree_INTERNAL(
    void*           pvBlock
#ifdef FWMEM_DEBUG
    ,const char*    pszFile,
    uint32          nLine
#endif /* FWMEM_DEBUG */
    );

#ifdef FWMEM_DEBUG

/*
 * Fill a sized block of memory up with garbage, and free it
 */
extern void FcMemFreeSize_INTERNAL(
    void*           pvBlock,
    size_t          cbSize,
    const char*     pszFile,
    uint32          nLine
    );

#endif /* FWMEM_DEBUG */

/* ----- END Support functions ----- */

#ifdef __cplusplus
}
#endif

#endif /* !__FWMEM_H__ */
