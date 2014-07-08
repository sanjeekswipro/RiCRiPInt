/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_malloc.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Memory allocation support functions.
 *
 */
#include "pms.h"
#include "pms_malloc.h"
#include "pms_platform.h"

#ifdef SDK_MEMTRACE
#define PMS_MALLOC_TRACE PMS_SHOW
#else
#define PMS_MALLOC_TRACE(_x_, ...)
#endif

#include <stdlib.h>     /* for malloc and free */

#ifdef WIN32
#include <malloc.h>     /* for malloc and free */
#include <memory.h>     /* for memset */
#endif
#if defined(linux)
#include <string.h>     /* for memset, memcpy */
#endif
/*! \brief PMS malloc data structure
*/
typedef struct {
  int bMemIsInitialised; /*!< Memory pool initialised */
#ifdef PMS_MEM_LIMITED_POOLS
  size_t iCurrentMemory;    /*!< amount of used memory */
  size_t iAvailMemory;      /*!< amount of total memory pool given by the user */
  size_t iPeakMemory;       /*!< maximum peak memory, for debugging purpose */
#endif
} PMS_TyMemoryUsage;

typedef struct {
#ifdef SDK_MEMTRACE
  char *pszFile;
  unsigned int nLine;
#endif
  char *pszThread;
  size_t cbSize;
  unsigned int nPool;
  unsigned int nCount;
} PMS_TyMemTraceHdr;

/** \brief Current amount of memory allocated by PMS. */
PMS_TyMemoryUsage l_tPMSMem[PMS_NumOfMemPools] = { {0},{0},{0},{0},{0} }; 

#ifdef PMS_MEM_LIMITED_POOLS
void * l_apUsed[PMS_NumOfMemPools][1000];
unsigned int l_uCount[PMS_NumOfMemPools];
#endif
#ifdef SDK_MEMTRACE
static char l_chFirstCharFileName_BestGuess = ' '; /* Depends on drive source was compiled from. Typically '.' on Unix style, and typically 'C' on Windows. */
#endif
char l_szDefaultThread[4]="GGS";

/**
 * \brief Allocate memory from PMS pool.
 *
 * Allocate the requested bytes from PMS memory pool and pass back the start address.\n
 */
#ifdef SDK_MEMTRACE
void *OSMallocEx(size_t size, PMS_TyMemPool pool, char *pszFile, int nLine)
#else
void *OSMallocEx(size_t size, PMS_TyMemPool pool)
#endif
{
  void *ptr;
  unsigned char *p;
#ifdef PMS_MEM_LIMITED_POOLS
  PMS_TyMemTraceHdr hdr;
#endif 

  if(!l_tPMSMem[pool].bMemIsInitialised)
  {
#ifdef PMS_MEM_LIMITED_POOLS
    memset(&l_apUsed[pool][0], 0, sizeof(l_apUsed[pool]));
    l_uCount[pool] = (unsigned int)-1;
    l_tPMSMem[pool].iCurrentMemory = 0;
    l_tPMSMem[pool].iPeakMemory = 0;

    switch(pool)
    {
      case PMS_MemoryPoolSys:
        l_tPMSMem[pool].iAvailMemory = g_tSystemInfo.cbSysMemory;
        if((g_tSystemInfo.cbSysMemory > 0) && (g_tSystemInfo.cbRIPMemory > g_tSystemInfo.cbSysMemory))
          PMS_SHOW_ERROR("*** Warning : SYS MEMORY POOL %d has to be greater than [-m <RIP memory in MB>] %d.\n",g_tSystemInfo.cbSysMemory,g_tSystemInfo.cbRIPMemory);
        break;
      case PMS_MemoryPoolApp:
        l_tPMSMem[pool].iAvailMemory = g_tSystemInfo.cbAppMemory;
        break;
      case PMS_MemoryPoolJob:
        l_tPMSMem[pool].iAvailMemory = g_tSystemInfo.cbJobMemory;
        break;
      case PMS_MemoryPoolMisc:
        l_tPMSMem[pool].iAvailMemory = g_tSystemInfo.cbMiscMemory;
        break;
      case PMS_MemoryPoolPMS:
        l_tPMSMem[pool].iAvailMemory = g_tSystemInfo.cbPMSMemory;
        break;
      default:
        PMS_SHOW_ERROR("*** OSMalloc: Invalid memory pool - %d ***\n",pool);
        l_tPMSMem[pool].iAvailMemory = 0;
    }
  
#endif

    l_tPMSMem[pool].bMemIsInitialised = 1;
  }

#ifdef PMS_MEM_LIMITED_POOLS
  /* Must be multiple of 8 bytes to be aligned for doubles on un*x platforms */
  size+=((sizeof(PMS_TyMemTraceHdr)+7)&~7);

  if(l_tPMSMem[pool].iAvailMemory)
  {
    if((l_tPMSMem[pool].iAvailMemory < l_tPMSMem[pool].iCurrentMemory) || (l_tPMSMem[pool].iAvailMemory - l_tPMSMem[pool].iCurrentMemory < (int)size))
    {
      PMS_SHOW_ERROR(" ***ASSERT*** OSMalloc; lack of memory in memory pool. size=%u, pool=%d\n", size, pool);
      return NULL;
    }
  }
#endif

  ptr=(void*)malloc(size);

  p = (unsigned char*)ptr;

  if(!ptr) {
    PMS_SHOW_ERROR(" ***ASSERT*** OSMalloc returning NULL, %u, %d\n", size, pool);
  } 
#ifdef PMS_MEM_LIMITED_POOLS
  else 
  {
    memset(&hdr,0xAA,sizeof(hdr));
    hdr.cbSize=size;
    hdr.nPool=pool;
#ifdef SDK_MEMTRACE
    hdr.nLine=nLine;
    hdr.pszFile=pszFile;
#endif
    hdr.pszThread=l_szDefaultThread;

    /*Critical section to avoid simultaneous access of global variable by multiple threads (PMS and OIL)*/
    /*CRITICAL SECTION - START*/
    /* Check that critical section has been created. The create cs routine calls this malloc function */
    if(g_csMemoryUsage) {
      PMS_EnterCriticalSection(g_csMemoryUsage);
    }

    hdr.nCount=++l_uCount[pool]; 
    memcpy(p, &hdr, sizeof(PMS_TyMemTraceHdr));
    p += ((sizeof(PMS_TyMemTraceHdr)+7)&~7);

#ifdef SDK_MEMTRACE
    if(l_chFirstCharFileName_BestGuess==' ') {
      l_chFirstCharFileName_BestGuess = pszFile[0];
    }
    if(l_uCount[pool] < (sizeof(l_apUsed[pool]) / sizeof(l_apUsed[pool][0])))
      l_apUsed[pool][l_uCount[pool]] = p;
#endif
    l_tPMSMem[pool].iCurrentMemory+=(unsigned int)size;

    if(g_csMemoryUsage) {
      PMS_LeaveCriticalSection(g_csMemoryUsage);
    }
    /*CRITICAL SECTION - END*/

    if(l_tPMSMem[pool].iCurrentMemory > l_tPMSMem[pool].iPeakMemory)
    {
      l_tPMSMem[pool].iPeakMemory = l_tPMSMem[pool].iCurrentMemory;
    }

    PMS_MALLOC_TRACE("OSMalloc, from %s(%d), %u, 0x%p, %d bytes, pool %d, returning p=0x%p\n", pszFile, nLine, l_uCount[pool], ptr, size, pool, p);
  } 
#endif

  return ((void*)p);
}

/**
 * \brief Deallocate memory from PMS pool.
 *
 * Deallocate the memory chunk from PMS memory pool pointed to by the start address.\n
 */
#ifdef SDK_MEMTRACE
void OSFreeEx(void *ptr, PMS_TyMemPool pool, char *pszFile, int nLine)
#else
void OSFreeEx(void *ptr, PMS_TyMemPool pool)
#endif
{
#ifndef SDK_MEMTRACE
#ifndef PMS_MEM_LIMITED_POOLS
  UNUSED_PARAM((PMS_TyMemPool), pool);
#endif
#endif

  if(ptr)
  {
#ifdef PMS_MEM_LIMITED_POOLS
    PMS_TyMemTraceHdr *pHdr;
    unsigned char *p = (unsigned char *)ptr;
    size_t size;
    unsigned int uPool;
    unsigned int uCount;
#ifdef SDK_MEMTRACE
    char *pszFileAlloc;
    int nLineAlloc;
#endif
    char *pszThread;

    p-=((sizeof(PMS_TyMemTraceHdr)+7)&~7);

    pHdr = (PMS_TyMemTraceHdr *)p;

#ifdef SDK_MEMTRACE
    nLineAlloc = pHdr->nLine;
    pszFileAlloc = pHdr->pszFile;
#endif
    pszThread = pHdr->pszThread;
    uCount = pHdr->nCount;
    uPool = pHdr->nPool;
    size = pHdr->cbSize;

#ifdef SDK_MEMTRACE
    PMS_MALLOC_TRACE("OSFree, from %s(%d), %u, 0x%p (actual 0x%p), %d bytes, pool %d, malloc from %s(%d) in thread %s\n", pszFile, nLine, uCount, ptr, p, size, pool, pszFileAlloc?pszFileAlloc:"null", nLineAlloc, pszThread?pszThread:"null"); 

    if(!pszThread || pszThread[0]!='G') {
      PMS_SHOW_ERROR("OSFree: Freeing memory that appears to have been stamped on. Unexpected thread string \"%s\"\n", pszThread?pszThread:"null"); 
      PMS_SHOW_ERROR("OSFree, from %s(%d), %u, 0x%p (actual 0x%p), %d bytes, pool %d, malloc from %s(%d) in thread %s\n", pszFile, nLine, uCount, ptr, p, size, pool, pszFileAlloc, nLineAlloc, pszThread); 
    }

    /* You may want to extend this check if your sources are compiled from various drives */
    if(!pszFileAlloc || (pszFileAlloc[0]!=l_chFirstCharFileName_BestGuess)) {
      PMS_SHOW_ERROR("OSFree: Freeing memory that appears to have been stamped on. Unexpected source file string \"%s\"\n", pszFileAlloc?pszFileAlloc:"null"); 
      PMS_SHOW_ERROR("OSFree, from %s(%d), %u, 0x%p (actual 0x%p), %d bytes, pool %d, malloc from %s(%d) in thread %s\n", pszFile, nLine, uCount, ptr, p, size, pool, pszFileAlloc, nLineAlloc, pszThread); 
    }
#endif 

    free(p);
#else
    free(ptr);
#endif

#ifdef PMS_MEM_LIMITED_POOLS
    /*Critical section to avoid simultaneous access of global variable by multiple threads (PMS and OIL)*/
    /*CRITICAL SECTION - START*/
    if(g_csMemoryUsage) {
      PMS_EnterCriticalSection(g_csMemoryUsage);
    }

    l_tPMSMem[pool].iCurrentMemory-=size;
#ifdef SDK_MEMTRACE
    if(uCount < (sizeof(l_apUsed[pool]) / sizeof(l_apUsed[pool][0])))
      l_apUsed[pool][uCount] = NULL;
#endif

    if(g_csMemoryUsage) {
      PMS_LeaveCriticalSection(g_csMemoryUsage);
    }
    /*CRITICAL SECTION - END*/
#endif
  }
}

#ifdef PMS_MEM_LIMITED_POOLS
/**
 * \brief Display memory PMS memory pool statistics.
 *
 */
void DisplayMemStats()
{
  int pool;
  int i;
  PMS_TyMemTraceHdr *pHdr;
  size_t size;
  unsigned int uPool;
  unsigned int uCount;
  char *pszThread;
#ifdef SDK_MEM_TRACE
  char *pszFileAlloc;
  int nLineAlloc;
#endif
  unsigned int *p;
  
  PMS_SHOW_ERROR("Memory pool statistics\n");
  for(pool = 0; pool < PMS_NumOfMemPools; pool++)
  {
    if(l_tPMSMem[pool].bMemIsInitialised)
    {
      switch (pool)
      {
      case PMS_MemoryPoolSys:
          PMS_SHOW_ERROR("Sys ");
          break;
      case PMS_MemoryPoolApp:
          PMS_SHOW_ERROR("App ");
          break;
      case PMS_MemoryPoolJob:
          PMS_SHOW_ERROR("Job ");
          break;
      case PMS_MemoryPoolMisc:
          PMS_SHOW_ERROR("Misc");
          break;
      case PMS_MemoryPoolPMS:
          PMS_SHOW_ERROR("PMS ");
          break;
      default:
          PMS_SHOW_ERROR("??? ");
          break;
      }

      if(l_tPMSMem[pool].iAvailMemory>0)
      {
        PMS_SHOW_ERROR("\tAvail: %u\tPeak: %u\tCurrent: %u\n", 
          l_tPMSMem[pool].iAvailMemory, 
          l_tPMSMem[pool].iPeakMemory,
          l_tPMSMem[pool].iCurrentMemory);
      }
      else
      {
        PMS_SHOW_ERROR("\tAvail: no limit\tPeak: %u\tCurrent: %u\n", 
          l_tPMSMem[pool].iPeakMemory,
          l_tPMSMem[pool].iCurrentMemory);
      }
    }
    for(i = 0; i < (sizeof(l_apUsed[pool]) / sizeof(l_apUsed[pool][0])); i++) {
      if(l_apUsed[pool][i]) {
        PMS_SHOW_ERROR("Found unfreed memory at %d, %p\n", i, l_apUsed[pool][i]);
        p = (unsigned int*)l_apUsed[pool][i];

        pHdr = (PMS_TyMemTraceHdr *)(p - ((sizeof(PMS_TyMemTraceHdr)+7)&~7));

#ifdef SDK_MEM_TRACE
        nLineAlloc = pHdr->nLine;
        pszFileAlloc = pHdr->pszFile;
#endif
        uCount = pHdr->nCount;
        uPool = pHdr->nPool;
        size = pHdr->cbSize;
        pszThread = pHdr->pszThread;

#ifdef SDK_MEM_TRACE
        PMS_SHOW_ERROR("allocation %u from %s(%d) in thread %s, size %u, pool %u\n", uCount, pszFileAlloc, nLineAlloc, pszThread, size, uPool); 
#else
        PMS_SHOW_ERROR("allocation %u in thread %s, size %u, pool %u\n", uCount, pszThread, size, uPool); 
#endif
      }
    }
  }
  PMS_SHOW_ERROR("\n");
}

/**
 * \brief Check for memory leaks and display if there are any.
 *
 */
unsigned int CheckMemLeaks()
{
  unsigned int retval=0;

  int pool;

  for(pool = 0; pool < PMS_NumOfMemPools; pool++)
  {
    if(l_tPMSMem[pool].bMemIsInitialised)
    {
      if(l_tPMSMem[pool].iCurrentMemory>0)
      {
        PMS_SHOW_ERROR("*** CheckMemLeaks: Pool %d - possible leak of %d ***\n", 
          pool,
          l_tPMSMem[pool].iCurrentMemory);
        retval = 1;
      }
    }
  }
  PMS_SHOW("\n");

  return (retval);
}
#endif

