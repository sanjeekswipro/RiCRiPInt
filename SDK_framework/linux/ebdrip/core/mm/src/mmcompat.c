/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmcompat.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Compatibility Layer above the Memory Management Interface
 */

#include <stdio.h>
#include "core.h"
#include "uvms.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "swdevice.h"
#include "swerrors.h"

#define MM_HEADER_SIG           ((mm_size_t)0xa110c515)
#ifdef MMCOMPAT_PARANOID
#define MM_HEADER_SIG_Z         ((mm_size_t)0xa1102e10)
#define MM_RELEASED_SIG         ((mm_size_t)0xa110f1ee)
#endif


mm_addr_t mm_alloc_with_header_proc( mm_pool_t pool,
                                     mm_size_t size,
                                     mm_alloc_class_t class
                                     MM_DEBUG_LOCN_PARMS )
{
  mm_size_t *result ;

  size += 2 * sizeof( mm_size_t ) ;

  result = ( mm_size_t * )mm_alloc_thru( pool, size, class ) ;
  if ( result ) {
    result[ 0 ] = MM_HEADER_SIG ;
    result[ 1 ] = size ;
#ifdef MMCOMPAT_PARANOID
    if ( 2 * sizeof( mm_size_t ) == result[ 1 ] )
      result[ 0 ] = MM_HEADER_SIG_Z ;
#endif
    return ( mm_addr_t )( result + 2 ) ;
  }
  else
    return result ;
}

mm_addr_t mm_realloc_with_header_proc( mm_pool_t pool,
                                       mm_addr_t what,
                                       mm_size_t size,
                                       mm_alloc_class_t class
                                       MM_DEBUG_LOCN_PARMS )
{
  char *newptr ;
  mm_size_t oldsize ;
  char *oldptr = ( char * )what ;
  mm_size_t *psize = ( mm_size_t * )what ;

  newptr = mm_alloc_with_header_proc( pool, size, class MM_DEBUG_LOCN_THRU ) ;
  if ( newptr != NULL && what != NULL) {
    oldsize = psize[ -1 ] - 2 * sizeof( mm_size_t ) ;
    HQASSERT( oldsize > 0 , "illegal sized free header" ) ;
    if ( size < oldsize )
      oldsize = size ;
    HqMemCpy( newptr , oldptr , oldsize ) ;
    mm_free_with_header( pool, what ) ;
  }
  return ( mm_addr_t )newptr ;
}

void mm_free_with_header( mm_pool_t pool, mm_addr_t what )
{
  mm_size_t *result ;
  if (what == NULL) {
    return;
  }
  HQASSERT( what, "illegal mm_free_with_header() of zero" ) ;
  result = (( mm_size_t * )what ) - 2 ;

#ifdef MMCOMPAT_PARANOID
  HQASSERT( result[ 0 ] == MM_RELEASED_SIG ||
            result[ 0 ] == MM_HEADER_SIG ||
            result[ 0 ] == MM_HEADER_SIG_Z,
            "illegal signature in memory object header" ) ;
  HQASSERT( result[ 0 ] != MM_RELEASED_SIG,
            "memory object has already been freed!" ) ;
  HQASSERT(( result[ 0 ] == MM_HEADER_SIG_Z ) ==
             ( result[ 1 ] == ( mm_size_t )( 2 * sizeof( mm_size_t ))),
            "memory object zero length flag mismatch" ) ;
  result[ 0 ] = MM_RELEASED_SIG ;
#else
  HQASSERT( result[ 0 ] == MM_HEADER_SIG,
            "illegal signature in memory object header" ) ;
#endif

  mm_free( pool, ( mm_addr_t )result, result[ 1 ] ) ;
}

mm_result_t mm_alloc_multi_hetero_with_headers_proc( mm_pool_t pool,
                                                     mm_size_t count,
                                                     mm_size_t sizes[],
                                                     mm_alloc_class_t classes[],
                                                     mm_addr_t returns[]
                                                     MM_DEBUG_LOCN_PARMS )
{
  mm_size_t i ;
  mm_result_t result ;

  for ( i = 0 ; i < count ; i++ )
    sizes[ i ] += 2 * sizeof( mm_size_t ) ;
  result = mm_alloc_multi_hetero_thru( pool, count, sizes, classes, returns ) ;
  if ( result == MM_SUCCESS ) {
    for ( i = 0 ; i < count ; i++ ) {
      (( mm_size_t * )returns[ i ] )[ 0 ] = MM_HEADER_SIG ;
      (( mm_size_t * )returns[ i ] )[ 1 ] = sizes[ i ] ;
      returns[ i ] = ( mm_addr_t )((( mm_size_t * )returns[ i ] ) + 2 ) ;
    }
  }
  for ( i = 0 ; i < count ; i++ )
    sizes[ i ] -= 2 * sizeof( mm_size_t ) ;
  return result ;
}

mm_result_t mm_alloc_multi_homo_with_headers_proc( mm_pool_t pool,
                                                   mm_size_t count,
                                                   mm_size_t size,
                                                   mm_alloc_class_t class,
                                                   mm_addr_t returns[]
                                                   MM_DEBUG_LOCN_PARMS )
{
  mm_size_t i ;
  mm_result_t result ;

  size += 2 * sizeof( mm_size_t ) ;
  result = mm_alloc_multi_homo_thru( pool, count, size, class, returns ) ;
  if ( result == MM_SUCCESS ) {
    for ( i = 0 ; i < count ; i++ ) {
      (( mm_size_t * )returns[ i ] )[ 0 ] = MM_HEADER_SIG ;
      (( mm_size_t * )returns[ i ] )[ 1 ] = size ;
#ifdef MMCOMPAT_PARANOID
      if ( 2 * sizeof( mm_size_t ) == (( mm_size_t * )returns[ i ] )[ 1 ] )
        (( mm_size_t * )returns[ i ] )[ 0 ] = MM_HEADER_SIG_Z ;
#endif
      returns[ i ] = ( mm_addr_t )((( mm_size_t * )returns[ i ] ) + 2 ) ;
    }
  }
  return result ;
}


uint8 * RIPCALL SwAlloc(int32 size)
{
  HQASSERT(size > 0, "Non-positive size handed to SwAlloc");

  return (uint8*)mm_alloc_with_header(mm_pool_temp,
                                      (size_t)size, MM_ALLOC_CLASS_EXTERN);
}


uint8 * RIPCALL SwRealloc( void *where, int32 bytes )
{
  HQASSERT(bytes > 0, "Non-positive size handed to SwRealloc");

  return (uint8*)mm_realloc_with_header(mm_pool_temp, (mm_addr_t)where,
                                        (size_t)bytes, MM_ALLOC_CLASS_EXTERN);
}


void RIPCALL SwFree( void *what )
{
  mm_free_with_header( mm_pool_temp, ( mm_addr_t )what ) ;
}


void *mm_alloc_static( size_t size )
{
  void *retmem ;

  HQASSERT(mm_pool_fixed != NULL, "no fixed pool for static allocation");
  HQASSERT(!rip_is_inited(), "Static alloc after rip initialisation");
  retmem = (void *)mm_alloc(mm_pool_fixed, size, MM_ALLOC_CLASS_STATIC);
  /* Trap with FAILURE() here for debugging, rather than forcing all callers
     to do so. We don't call error_handler(), because this routine is used
     extensively during bootup, before the error handler is usable. */
  if ( retmem == NULL )
    return FAILURE(NULL) ;
  return retmem ;
}

char *mm_pretty_memory_string(uint32 size_in_kib, char *out_buf)
{
  float value;
  char *format;
  int num_bytes;

  enum {
    ONE_TIB = 0,
    ONE_GIB,
    ONE_MIB,
    ONE_KIB,
    NUM_MEM_UNITS /* MUST be last. */
  };

  static char *mem_str[NUM_MEM_UNITS] = {
    "%.0f TiB (%u KiB)",
    "%.0f GiB (%u KiB)",
    "%.0f MiB (%u KiB)",
    "%u KiB"
  };

#define ONE_TIB_IN_K (1024 * 1024 * 1024) /* TiB in KiB */
#define ONE_GIB_IN_K (1024 * 1024) /* GiB in KiB */
#define ONE_MIB_IN_K (1024 ) /* MiB in KiB */
#define ONE_KIB_IN_K (1) /* KiB in KiB */

  HQASSERT(out_buf != NULL, "out_buf is NULL");
  HQASSERT(size_in_kib < ((uint32)ONE_TIB_IN_K * 2), "size_in_kb too large");

  if (size_in_kib > ONE_TIB_IN_K) {
    format = mem_str[ONE_TIB];
    value = (float)size_in_kib / ONE_TIB_IN_K ;
  } else if (size_in_kib > ONE_GIB_IN_K) {
    format = mem_str[ONE_GIB];
    value = (float)size_in_kib / ONE_GIB_IN_K ;
  } else if (size_in_kib > ONE_MIB_IN_K) {
    format = mem_str[ONE_MIB];
    value = (float)size_in_kib / ONE_MIB_IN_K ;
  } else {
    format = mem_str[ONE_KIB];
    value = (float)size_in_kib / ONE_KIB_IN_K ;
  }

  if (size_in_kib > ONE_MIB_IN_K) {
    num_bytes = sprintf(out_buf, format, value, size_in_kib);
  } else {
    num_bytes = sprintf(out_buf, format, size_in_kib);
  }

  HQASSERT(num_bytes <= MAX_MM_PRETTY_MEMORY_STRING_LENGTH,
           "wrote too many bytes, overflowed pretty print buffer");

  return out_buf;
}


/* Log stripped */
