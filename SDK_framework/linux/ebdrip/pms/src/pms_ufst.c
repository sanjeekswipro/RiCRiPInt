/* Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_ufst.c(EBDSDK_P.1) $
 * 
 */

/*! \file
 *  \ingroup PMS
 *  \brief UFST application functions
 *
 * These functions are application-provided functions required by the UFST library
 */

#include "pms_ufst.h"
#if GG_CUSTOM
#include "pms_malloc.h"
#endif

/* -------------------------------------------------------------------------- */
PCLchId2PtrFn * g_pmsPCLchId2Ptr = 0;
PCLchId2PtrFn * g_pmsPCLglyphId2Ptr = 0;

VOID init_pcleo_callback_data( FSP0 )
{
}

LPUB8 PCLchId2ptr( FSP UW16 chId )
{
  LPUB8 ptr = (LPUB8) 0;

  if( g_pmsPCLchId2Ptr != 0 )
  {
#if UFST_REENTRANT
    /* &if_state is probably OK in either case, but &(*pIFS) != pIFS even if
       *(&(*pIFS)) == *pIFS, but I don't know if this is important. */
    ptr = (g_pmsPCLchId2Ptr)(pIFS, chId);
#else
    ptr = (g_pmsPCLchId2Ptr)(&if_state, chId);
#endif
  }

  return ptr;
}

LPUB8 PCLglyphID2Ptr( FSP UW16 glyphId )
{
  LPUB8 ptr = (LPUB8) 0;

  if( g_pmsPCLglyphId2Ptr != 0 )
  {
#if UFST_REENTRANT
    ptr = (g_pmsPCLglyphId2Ptr)(pIFS, glyphId);
#else
    ptr = (g_pmsPCLglyphId2Ptr)(&if_state, glyphId);
#endif
  }

  return ptr;
}
/* -------------------------------------------------------------------------- */
#if GG_CUSTOM

/* Memory allocation routines may be provided by the core UFST module */

PCLallocFn    * g_pmsPCLalloc = 0;
PCLfreeFn     * g_pmsPCLfree = 0;

void * GGalloc(FSP size_t size)
{
  if (g_pmsPCLalloc != 0)
    return (g_pmsPCLalloc)(&if_state, size) ;
  else
    return OSMalloc(size, PMS_MemoryPoolPMS) ;
}

void GGfree(FSP void * block)
{
  if (g_pmsPCLfree != 0)
    (g_pmsPCLfree)(&if_state, block) ;
  else
    OSFree(block, PMS_MemoryPoolPMS) ;

  return ;
}

#endif
/* -------------------------------------------------------------------------- */
