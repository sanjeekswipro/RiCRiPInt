/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_malloc.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for memory allocation support functions.
 *
 */

#ifndef _PMS_MALLOC_H_
#define _PMS_MALLOC_H_

#include "stdlib.h"

#define PMS_MEM_LIMITED_POOLS 
/* PMS_MEM_LIMITED_POOLS can be set without SDK_MEMTRACE.
   However, if SDK_MEM_TRACE is set then PMS_MEM_LIMITED_POOLS must as well,
   Of course, this does not stop you from reworking this implementation to implement
   whatever your requirements are. 
*/
#ifdef SDK_MEMTRACE
#define PMS_MEM_LIMITED_POOLS 
#endif

#ifdef SDK_MEMTRACE
void *OSMallocEx(size_t size, PMS_TyMemPool pool, char *pszFile, int nLine);
void OSFreeEx(void *ptr, PMS_TyMemPool pool, char *pszFile, int nLine);
#define OSMalloc(_size_, _pool_) OSMallocEx(_size_, _pool_, __FILE__, __LINE__)
#define OSFree(_ptr_, _pool_) OSFreeEx(_ptr_, _pool_, __FILE__, __LINE__)
#else
void *OSMallocEx(size_t size, PMS_TyMemPool pool);
void OSFreeEx(void *ptr, PMS_TyMemPool pool);
#define OSMalloc(_size_, _pool_) OSMallocEx(_size_, _pool_)
#define OSFree(_ptr_, _pool_) OSFreeEx(_ptr_, _pool_)
#endif

#ifdef PMS_MEM_LIMITED_POOLS
void DisplayMemStats();
unsigned int CheckMemLeaks();
#endif


#endif /* _PMS_MALLOC_H_ */
