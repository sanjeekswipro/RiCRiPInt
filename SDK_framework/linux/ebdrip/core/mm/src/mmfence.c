/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmfence.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fencepost support routines
 */

#include "core.h"
#include "mmfence.h"
#include "mm.h"
#include "mmtag.h"
#include "mmpool.h"


#if (defined(MM_DEBUG_FENCEPOST) || defined(MM_DEBUG_FENCEPOST_LITE))


#define MM_DEBUG_FENCEPOST_VALUE (0xdeadbeef)


/* Check the fenceposts for a single object. Lots of unused arguments
 * because this has to be passed to mm_debug_tag_apply.
 */
static void mm_debug_check_fencepost( mm_addr_t ptr,
                               mm_size_t oldsize,
                               mm_size_t size,
                               mm_pool_t pool,
                               mm_alloc_class_t class,
                               mm_size_t seq,
                               char *file,
                               int line,
                               mm_debug_watch_t reason )
{
  uint32 *p = ( uint32 * )ptr ;
  UNUSED_PARAM( mm_size_t , oldsize ) ;
  UNUSED_PARAM( mm_pool_t , pool ) ;
  UNUSED_PARAM( mm_alloc_class_t , class ) ;
  UNUSED_PARAM( mm_size_t , seq ) ;
  UNUSED_PARAM( char * , file ) ;
  UNUSED_PARAM( int , line ) ;
  UNUSED_PARAM( mm_debug_watch_t , reason ) ;

  if ( pool->mps_debug )
    return;
  HQASSERT(( p[ 0 ] == MM_DEBUG_FENCEPOST_VALUE ) &&
           ( p[ 1 ] == MM_DEBUG_FENCEPOST_VALUE ),
           "Leading fenceposts corrupted" ) ;
  p = UPPER_FENCEPOST( ptr, size ) ;
  HQASSERT(( p[ 0 ] == MM_DEBUG_FENCEPOST_VALUE ) &&
           ( p[ 1 ] == MM_DEBUG_FENCEPOST_VALUE ),
           "Trailing fenceposts corrupted" ) ;
}


/* Check the fenceposts for a single object. The ptr, and size are without
the fenceposts, hence the -2/+2 to adjust p.
 */
void mm_debug_check_single_fencepost(mm_pool_t pool,
                                     mm_addr_t ptr, mm_size_t size)
{
  uint32 *p = ( uint32 * )ptr ;

  if ( pool->mps_debug )
    return;
  p -=2;
  HQASSERT(( p[ 0 ] == MM_DEBUG_FENCEPOST_VALUE ) &&
           ( p[ 1 ] == MM_DEBUG_FENCEPOST_VALUE ),
           "Leading fencepost corrupted" ) ;
  p = UPPER_FENCEPOST( ptr, size ) ;
  p +=2;
  HQASSERT(( p[ 0 ] == MM_DEBUG_FENCEPOST_VALUE ) &&
           ( p[ 1 ] == MM_DEBUG_FENCEPOST_VALUE ),
           "Trailing fencepost corrupted" ) ;
}


/* Check all the fenceposts. May be a useful function to call from a
 * debugger
 */

void mm_debug_check_fenceposts( void )
{
  mm_debug_tag_apply( mm_debug_check_fencepost ) ;
}

/* Set the fenceposts for a single object */

mm_addr_t mm_debug_set_fencepost(mm_pool_t pool, mm_addr_t ptr, mm_size_t size)
{
  uint32 *p = ( uint32 * )ptr ;
  if ( pool->mps_debug )
    return ptr;
  p[ 0 ] = MM_DEBUG_FENCEPOST_VALUE ;
  p[ 1 ] = MM_DEBUG_FENCEPOST_VALUE ;
  p = UPPER_FENCEPOST( ptr, size ) ;
  p[ 0 ] = MM_DEBUG_FENCEPOST_VALUE ;
  p[ 1 ] = MM_DEBUG_FENCEPOST_VALUE ;
  return ABOVE_FENCEPOST(ptr);
}

/* When an object is truncated, a new fencepost must be written at the
 * new end of the object */

void mm_debug_truncate_fencepost( mm_addr_t ptr )
{
  uint32 *p = ( uint32 * )ptr ;
  p[ 0 ] = MM_DEBUG_FENCEPOST_VALUE ;
  p[ 1 ] = MM_DEBUG_FENCEPOST_VALUE ;
}

#endif /* defined(MM_DEBUG_FENCEPOST) || defined(MM_DEBUG_FENCEPOST_LITE) */



/* Log stripped */
