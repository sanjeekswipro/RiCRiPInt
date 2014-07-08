/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_malloc.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This header file contains the declarations of the memory 
 *   allocation support functions.
 * 
 * The memory management support for the OIL includes OIL-specific
 * versions of the standard \c malloc() and \c free() functions. It
 * also supports the allocation of memory from defined memory pools, and
 * the concept of blocking and non-blocking memory allocation
 * requests.
 */


#ifndef _OIL_MALLOC_H_
#define _OIL_MALLOC_H_

#include "stdlib.h"

/*! \brief An enumeration defining the available memory pools.
*/
enum {
    OILMemoryPoolSys,        /*!< allocate from System memory pool  */
    OILMemoryPoolApp,        /*!< allocate from Application memory pool  */
    OILMemoryPoolJob,        /*!< allocate from Job memory pool  */
    OILMemoryPoolMisc,        /*!< allocate from other memory pool  */
    OIL_NumOfMemPools,
};
typedef int OIL_TyMemPool;

/*! \brief An enumeration defining the blocking and non-blocking memory allocation request types.
*/
enum {
    OIL_MemBlock,           /*!< if malloc fails - block  */
    OIL_MemNonBlock,        /*!< if malloc fails return NULL */
};
typedef int OIL_TyMemBlockType;

#ifdef SDK_MEMTRACE
void *OIL_mallocEx(OIL_TyMemPool pool, OIL_TyMemBlockType bBlockType, size_t size, char *pszFile, int nLine);
void OIL_freeEx(OIL_TyMemPool pool, void *ptr, char *pszFile, int nLine);
#define OIL_malloc(_pool_, _block_, _size_) OIL_mallocEx(_pool_, _block_, _size_, __FILE__, __LINE__)
#define OIL_free(_pool_, _ptr_) OIL_freeEx(_pool_, _ptr_, __FILE__, __LINE__)
#else
void *OIL_mallocEx(OIL_TyMemPool pool, OIL_TyMemBlockType bBlockType, size_t size);
void OIL_freeEx(OIL_TyMemPool pool, void *ptr);
#define OIL_malloc(_pool_, _block_, _size_) OIL_mallocEx(_pool_, _block_, _size_)
#define OIL_free(_pool_, _ptr_) OIL_freeEx(_pool_, _ptr_)
#endif


#endif /* _OIL_MALLOC_H_ */
