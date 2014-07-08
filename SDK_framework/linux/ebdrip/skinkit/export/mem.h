/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:mem.h(EBDSDK_P.1) $
 * Memory related utility functions
 */

#ifndef __MEM_H__
#define __MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * \ingroup skinkit
 * \brief Memory-related utility functions.
 *
 * \section mem_h_overview Memory Management in the RIP
 *
 * This file defines the skin interface for memory management. This includes
 * familiar allocation and de-allocation routines for claiming portions of
 * private memory within the skin. It also includes functions for creating and
 * constraining the RIP's main memory arena, which is shared by the core RIP
 * and the skin. The hosting application must create this arena with
 * \c MemInit() before booting the RIP, and destroy it with \c MemFinish()
 * after shutting the RIP down.
 *
 * Once allocated, the RIP memory arena is managed using a sophisticated
 * scheme known as the Memory Pool System (MPS). The MPS interface itself
 * is not defined or described here. For now, it suffices to say that MPS
 * provides the most efficient internal management of the arena, and it
 * does so in a way that is transparent to the skin.
 *
 * \section mem_h_ripmemory RIP Memory and the Skin Pool
 *
 * There are two ways in which the RIP's memory arena can be allocated,
 * depending on the arguments passed to \c MemInit(). If the arena is
 * allocated \e manually, then the host application simply passes a pointer
 * to the base address of the pre-allocated block, along with an indication
 * of its total size. MPS then takes over all internal management of this
 * block, but does not allocate or release the block itself. (In MPS
 * terminology, it is a "client arena"). This is the most common pattern
 * for embedded applications. For host-based applications, it is more common
 * to allocate the arena \e automatically. In this case, no base pointer is
 * passed to \c MemInit(). MPS takes care of the initial memory reservation,
 * as well as all of its internal management.
 *
 * Regardless of how it is allocated, a key characteristic of the RIP arena
 * is that it is \e constrained. It is not allowed to expand beyond the
 * maximum size specified to \c MemInit(). The hosting application is thus
 * able to limit the memory in which the RIP will run.
 *
 * Another important characteristic of the RIP's memory arena is that
 * it contains a <em>skin pool</em>, which is automatically created and managed
 * with MPS. This pool allows the skin to use some portion of the total
 * RIP arena for itself, while the rest is used by the core. This feature
 * is important, because it allows the total size constraint to remain
 * effective for the skin and core in combination.
 *
 * \section mem_h_howto How To Allocate from Skin Code
 *
 * To allocate and free memory within the skin pool, use the \c MemAlloc()
 * and \c MemFree() functions. Almost all memory allocations within the
 * skin should be handled with these functions. In addition, there is a
 * \c MemRealloc() function, whose use in skin programming is
 * discouraged: re-allocators are notoriously prone to misuse. The
 * \c MemRealloc() function has mainly been provided to help the integration
 * of third-party libraries such as Expat, allowing their memory
 * management to be wired up to the skin pool.
 *
 * \section mem_h_system System Memory
 *
 * For the most part, the RIP memory arena services all memory requirements,
 * whether they come from the skin or the core. However, this interface also
 * supports the existence of a second, conceptually-distinct region of
 * memory. This region is broadly known as "system" memory. System memory can
 * be viewed as whatever remains for use by the hosting process, once the
 * RIP's main arena has been allocated. System memory is completely out of
 * bounds to the core RIP, and entirely hidden from the MPS. System memory,
 * where it exists, is managed by the operating system on the host platform
 * (or embedded device). Beyond that broad characterization, this interface
 * does not define exactly what system memory is. In a host-based RIP application,
 * it could simply be "leftover" memory being handled with the
 * low-level C \c malloc() and \c free() functions, or perhaps with the C++
 * \c new and \c delete operators. Alternatively, it might be a region
 * ringfenced for a specific purpose, such as holding a page of raster data.
 * To allocate and free such system memory, use the \c SysAlloc() and
 * \c SysFree() functions. (There is deliberately no \c SysRealloc()
 * function). These functions have default implementations on all supported
 * platforms, but you may also provide your own implementations when calling
 * \c MemInit().
 *
 * \section mem_h_summary In Summary
 *
 * When writing code in the skin layers:
 *
 * - Use \c MemAlloc() and \c MemFree() to allocate skin memory in most
 *   situations.
 *
 * - Use \c SysAlloc() and \c SysFree() to allocate system memory, which
 *   should be used only when you deliberately need to avoid using
 *   the skin pool for some reason.
 *
 * - Avoid the use of any other allocators in the skin.
 *
 * Skin memory management is summarised in the following diagram.
 *
 * \image html SkinMemoryManagement.jpg
 */

#include "std.h"
#include "ripcall.h"
#include "mps.h"

#ifdef DEBUG_BUILD
#define MEM_LOCATION
#else
#ifdef MM_DEBUG_MPSTAG
#define MEM_LOCATION
#endif
#endif

#ifdef MEM_LOCATION
/* For stringifying __LINE__ */
#define MEMSTRING_(x) #x
#define MEMSTRING(x) MEMSTRING_(x)
#endif

/**
 * \brief Defines a function for allocating system memory.
 *
 * Passed in <code>SysMemFns</code> structure to <code>MemInit</code>.
 * Used by <code>SysAlloc</code>.
 */
typedef void * (RIPCALL SysAllocFn)(size_t cbSize);

/**
 * \brief Defines a function for de-allocating system memory.
 *
 * Passed in <code>SysMemFns</code> structure to <code>MemInit</code>.
 * Used by <code>SysFree</code>.
 */
typedef void   (RIPCALL SysFreeFn)(void * pbMem);


/**
 * \brief Defines a structure to hold a set of system memory
 * allocation functions.
 */
typedef struct SysMemFns {
  SysAllocFn   * pAllocFn;
  SysFreeFn    * pFreeFn;
} SysMemFns;

/**
 * \brief Set up the memory system to log to a file.
 *
 * \param[in] mps_log Filename for MPS memory logging. If the MPS library
 * linked in the RIP supports logging, all of the enabled telemetry will be
 * written to this file.
 *
 * \param mps_telemetry MPS telemetry options. If the MPS library linked in
 * the RIP supports logging, this option controls which events are logged to
 * the file.
 *
 * \note This function must be called before \c MemInit() (and
 * \c SwLeSDKStart(), which calls \c MemInit()).
 */
void MemLogInit(const char *mps_log, unsigned long mps_telemetry) ;

/**
 * \brief Initialise the memory arena and create the memory pool for
 * use in skin.
 *
 * <p>The core RIP and the demonstration skin will share the same
 * memory arena. However, the skin will have its own pool within that
 * arena. It is possible to apply a constraint to the amount of memory
 * that the core RIP and the skin are permitted to use in
 * combination. Use the <code>RIP_workingSizeInBytes</code> parameter
 * to do this.
 *
 * <p>It is also possible for the RIP to work within a region of
 * memory that has been pre-allocated by the host application. Use
 * the <code>pMemory</code> argument to do this.
 *
 * <p>This function <em>must</em> be called before booting the RIP,
 * and before any use of the other skin memory-management functions
 * defined in this file.
 *
 * \param RIP_maxAddressSpaceInBytes The size of maximum address space
 * the RIP can use. This value gets filled in.
 *
 * \param RIP_workingSizeInBytes The size of working memory permitted for
 * the RIP, measured in bytes. If you pass zero, the function will
 * attempt to calculate a suitable default based upon the memory
 * configuration of the host machine. If details of the memory
 * configuration cannot be determined, a hard-coded default will be
 * applied.
 *
 * \param pMemory Points to a caller-allocated memory block that the
 * RIP should use. If this is non-NULL, the RIP will work within this
 * memory block, rather than allocating its own memory. The size of
 * the block is defined by <code>RIP_workingSizeInBytes</code>, which must
 * not be zero. If this pointer is NULL, then the RIP will allocate
 * and manage its own memory, suitably constrained by
 * <code>RIP_workingSizeInBytes</code>.
 *
 * \param pSysMemFns Points to a structure providing the suite of
 * system memory handling functions for use by <code>SysAlloc</code>,
 * and <code>SysFree</code>.  If this is NULL the default functions
 * are used, which are implemented using the OS
 * <code>malloc</code> and <code>free</code>.
 *
 * \return The actual size of working memory applied for the RIP. This
 * would normally be equal to <code>RIP_workingSizeInBytes</code>, except
 * for the case where <code>RIP_workingSizeInBytes == 0</code>, where the
 * value returned will be whatever default size was applied.
 */
extern size_t MemInit(
  size_t *RIP_maxAddressSpaceInBytes,
  size_t RIP_workingSizeInBytes,
  void * pMemory,
  SysMemFns * pSysMemFns);

/**
 * \brief Release the arena of managed memory.
 *
 * <p>A call to this function should be one of the final acts in shutting
 * down the host application. Once this function has been called, neither
 * the RIP nor the skin layer will be permitted to use the shared
 * arena of managed memory.
 *
 * \param fError Indicates whether the application is shutting down in
 * an error state. Pass zero if the application is shutting down
 * successfully. Pass any non-zero to indicate an error state.
 */
extern void MemFinish(int32 fError);

/**
 * \brief Allocate a block of memory for use by the skin layer.
 *
 * Always use \c MemAlloc() and \c MemFree() to manage allocations
 * in the skin where possible. These functions allow the memory
 * to be managed more effectively, and also allow the skin code to
 * be subjected to the same heap limitation controls as the core
 * RIP. Direct use of malloc can cause the skin to overflow any
 * such limitations that might be in place.
 *
 * \param cbSize The amount of memory required, measured in bytes.
 *
 * \param fZero Pass <code>TRUE</code> if you require the memory block
 * to be initialized with zeros, otherwise pass <code>FALSE</code>.
 *
 * \param fExitOnFail Controls failing behaviour. If passed as
 * <code>TRUE</code>, the entire host application will be terminated
 * if the allocation fails. Otherwise, a failure will result in
 * a NULL return value. It is usually better to pass <code>FALSE</code>,
 * and write additional code to handle failed allocations.
 *
 * \return A pointer to the allocated memory block, or NULL if
 * the allocation failed (and <code>fExitOnFail==FALSE</code>).
 */
#ifndef MEM_LOCATION
extern void * RIPCALL MemAlloc(size_t cbSize, int32 fZero, int32 fExitOnFail);
#else
#define MemAlloc(cbSize, fZero, fExitOnFail) MemAllocDebug(cbSize, fZero, fExitOnFail, __FILE__ "(" MEMSTRING(__LINE__) ")" )

extern void * RIPCALL MemAllocDebug(size_t cbSize, int32 fZero, int32 fExitOnFail, char * location);
#endif

/**
 * \brief Define byte alignment of allocation via MemAlloc
 */
#define MemAllocAlign(_x) (((_x) + 7) & (~7)) /* .hack.align */

/**
 * \brief Reallocate a block of memory that was allocated by a prior call
 * to <code>MemAlloc()</code>.
 *
 * \param ptr The memory block to be reallocated.
 * \param cbSize The amount of memory required, measured in bytes.
 */
#ifndef MEM_LOCATION
extern void * RIPCALL MemRealloc(void *ptr, size_t cbSize);
#else
#define MemRealloc(ptr, cbSize) MemReallocDebug(ptr, cbSize, __FILE__ "(" MEMSTRING(__LINE__) ")" )

extern void * RIPCALL MemReallocDebug(void *ptr, size_t cbSize, char * location);
#endif

/**
 * \brief Free a block of memory that was allocated by a prior call
 * to <code>MemAlloc()</code>.
 *
 * \param pbMem The memory block to be freed.
 */
extern void   RIPCALL MemFree(void * pbMem);

/**
 * \brief Allocate a block of system memory for use by the skin layer.
 *
 * Uses the function passed via the <code>SysMemFns</code> structure
 * to <code>MemInit</code>, or <code>malloc</code> if no structure
 * was passed.
 *
 * Allocate memory when it is not possible or appropriate to use
 * <code>MemAlloc()</code>.
 *
 * \param cbSize The amount of memory required, measured in bytes.
 *
 * \return A pointer to the allocated memory block, or NULL if
 * the allocation failed.
 */
extern void * RIPCALL SysAlloc(size_t cbSize);

/**
 * \brief Free a block of memory that was allocated by a prior call
 * to <code>SysAlloc()</code>.
 *
 * Uses the function passed via the <code>SysMemFns</code> structure
 * to <code>MemInit</code>, or <code>free</code> if no structure
 * was passed.
 *
 * \param pbMem The memory block to be freed.
 */
extern void RIPCALL SysFree(void * pbMem);

/**
 * \brief Returns the single arena of managed memory that is shared between the
 * core RIP and the skin layer.
 */
extern mps_arena_t MemGetArena(void);

/**
 * \brief Obtain the best possible measure, in megabytes, of the amount of
 * physical RAM installed on the host machine.
 *
 * \return The value in megabytes, or zero if the machine configuration
 * can't be determined.
 */
uint32 GetPhysicalRAMSizeInMegabytes(void);

/**
 * \brief Obtain the best possible measure, in megabytes, of the
 * maximum amount of memory that this process has ever used.
 *
 * \return The value in megabytes, or zero if the machine
 * configuration can't be determined or an error occured.
 */
uint32 GetPeakMemoryUsageInMegabytes(void);

/**
 * \brief Obtain current number of open handles this process has.
 *
 * \return Number of handles open by this process. -1 if the number of
 * handles could not be obtained which should not be treated as an
 * error because the OS may not support the collection of this
 * information.
 */
int32 GetCurrentProcessHandleCount(void);

#ifdef __cplusplus
}
#endif

#endif

