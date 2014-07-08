/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_malloc.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This header file contains the definitions of the memory 
 *   allocation support functions.
 *
 */

#include "oil_interface_oil2pms.h"
#include "oil_malloc.h"
#include "pms_export.h"

/* extern variables */
extern OIL_TyJob *g_pstCurrentJob;

/**
 * \brief Memory allocation function, analogous to \c malloc()
 *
 * This function provides an OIL-specific abstraction layer to control memory allocation.
 * In addition to specifying the required amount of memory, two additional parameters 
 * specify which pool the memory should be allocated from, and whether or not a failure to 
 * allocate memory should block until allocation is successful, or return NULL.
 * \param[in] pool                  Specifies the memory pool to allocate the memory from
 * \param[in] bBlockType            Specifies whether or not the function should block other processes if no memory is available.
 * \param[in] size                  Specifies the amount of memory to be allocated, in bytes.
 * \return    The function returns a pointer to the allocated memory.  The pointer is of type \c void*.
 */
#ifdef SDK_MEMTRACE
void *OIL_mallocEx(OIL_TyMemPool pool, OIL_TyMemBlockType bBlockType, size_t size, char *pszFile, int nLine)
#else
void *OIL_mallocEx(OIL_TyMemPool pool, OIL_TyMemBlockType bBlockType, size_t size)
#endif
{
    void *ptr;
    int nResult = 0;
    unsigned int bWait;

    if(pool > OIL_NumOfMemPools)
    {
      HQASSERT(pool <= OIL_NumOfMemPools,
               "OIL_malloc: Invalid Memory Pool, nothing to freed");
      return NULL;
    }

    do
    {
      switch(pool)
      {
        case OILMemoryPoolSys:
#ifdef SDK_MEMTRACE
          nResult=PMS_Malloc(size,&ptr,PMS_MemoryPoolSys,pszFile,nLine);
#else
          nResult=PMS_Malloc(size,&ptr,(int)PMS_MemoryPoolSys);
#endif
          break;
        case OILMemoryPoolJob:
#ifdef SDK_MEMTRACE
          nResult=PMS_Malloc(size,&ptr,PMS_MemoryPoolJob,pszFile,nLine);
#else
          nResult=PMS_Malloc(size,&ptr,(int)PMS_MemoryPoolJob);
#endif
          break;
        case OILMemoryPoolApp:
#ifdef SDK_MEMTRACE
          nResult=PMS_Malloc(size,&ptr,PMS_MemoryPoolApp,pszFile,nLine);
#else
          nResult=PMS_Malloc(size,&ptr,(int)PMS_MemoryPoolApp);
#endif
          break;
       case OILMemoryPoolMisc:
#ifdef SDK_MEMTRACE
          nResult=PMS_Malloc(size,&ptr,PMS_MemoryPoolMisc,pszFile,nLine);
#else
          nResult=PMS_Malloc(size,&ptr,(int)PMS_MemoryPoolMisc);
#endif
          break;
        default:
          ptr=NULL;
          HQASSERT(pool < OIL_NumOfMemPools, "OIL_malloc : INVALID POOL ID");
      }

      /* check if malloc succeeded */
      bWait = FALSE;
      if ((nResult!=1)||(ptr==NULL))
      {
        /* something failed - should this wait? */
        if ((g_pstCurrentJob) && (g_pstCurrentJob->uPagesInOIL > 0) && (bBlockType==OIL_MemBlock))
        {
          bWait = TRUE;
          OIL_RelinquishTimeSlice();  /* allow other processes to get time */
        }
      }
    }
    while (bWait);

    HQASSERT(nResult==1, ("OIL_malloc : PMS_Malloc failed"));
    HQASSERTV(ptr!=NULL, ("OIL_malloc : PMS_Malloc failed to allocate %u bytes from pool %d\n", size, pool));

    return (ptr);
}

/**
 * \brief Memory freeing function, analogous to \c free().
 *
 * An abstraction layer to release memory.\n
 *
 * This function provides an OIL-specific abstraction layer to control memory allocation.
 * In addition to specifying a pointer to the memory to be freed, an additional parameter 
 * specifies the pool to which the memory should be returned.  This should always be the 
 * pool from which the memory was originally allocated.
 * \param[in] pool                  Specifies the pool to return the memory to.
 * \param[in] ptr                   A pointer to the memory to be freed.
 */
#ifdef SDK_MEMTRACE
void OIL_freeEx(OIL_TyMemPool pool, void *ptr, char *pszFile, int nLine)
#else
void OIL_freeEx(OIL_TyMemPool pool, void *ptr)
#endif
{
    HQASSERT(ptr, ("OIL_free: Freeing NULL pointer"));

    switch(pool)
    {
      case OILMemoryPoolSys:
#ifdef SDK_MEMTRACE
        PMS_Free(ptr, PMS_MemoryPoolSys, pszFile, nLine);
#else
        PMS_Free(ptr, PMS_MemoryPoolSys);
#endif
        break;
      case OILMemoryPoolJob:
#ifdef SDK_MEMTRACE
        PMS_Free(ptr, PMS_MemoryPoolJob, pszFile, nLine);
#else
        PMS_Free(ptr, PMS_MemoryPoolJob);
#endif
        break;
      case OILMemoryPoolApp:
#ifdef SDK_MEMTRACE
        PMS_Free(ptr, PMS_MemoryPoolApp, pszFile, nLine);
#else
        PMS_Free(ptr, PMS_MemoryPoolApp);
#endif
        break;
      case OILMemoryPoolMisc:
#ifdef SDK_MEMTRACE
        PMS_Free(ptr, PMS_MemoryPoolMisc, pszFile, nLine);
#else
        PMS_Free(ptr, PMS_MemoryPoolMisc);
#endif
        break;
      default:
        HQFAIL("OIL_malloc : Invalid memory pool ID\n");
    }
}

