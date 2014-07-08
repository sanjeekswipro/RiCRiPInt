/** \file
 * \ingroup hqmem
 *
 * $HopeName: HQNc-standard!export:hqmemcpy.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Two entry points for copying blocks of memory, closely shadowing the ANSI
 * functions memcpy and memmove.
 *
 * In fact on some platforms they map down onto those functions. On others,
 * they call out to the OS, or execute our own optimised routines. The
 * decision of which to use on each platform should be based on careful testing.
 */

#ifndef __HQMEMCPY_H__
#define __HQMEMCPY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "std.h"

/*@+fcnmacros -incondefs@*/


/* For ScriptWorks core RIP only products, we already have a requirement that
 * the OEM provide an implementation of bcopy. Other products which use these
 * routines and are shipped as a static library on platforms which don't use
 * the ANSI versions will have the same requirement.
 */

/* The three preprocessor identifiers USE_INLINE_MEMCPY, USE_INLINE_MEMMOVE and
 * BCOPY_OVERLAP_SAFE may or may not be defined in platform.h. The decision
 * is based on speed tests for the best routine on a given platform and on
 * documentation to determine the overlap safety of bcopy. Debug builds all
 * assert that blocks passed to HqMemCpy don't overlap - hence the need for
 * the wrapper function HqMemCpy_Assert.
 */

void HQNCALL HqMemCpy_Assert(
  /*@notnull@*/ /*@out@*/       void *dest ,
  /*@notnull@*/ /*@in@*/        void *src ,
                                int32 count ) ;

void HQNCALL bcopy_safe(
  /*@notnull@*/ /*@in@*/        char *src ,
  /*@notnull@*/ /*@out@*/       char *dest ,
                                int32 count ) ;

/* HqMemCpy - moves _count bytes from _src to _dest. Correct results when the
 *            blocks overlap are not guaranteed.
 */

void HQNCALL HqMemCpy(
  /*@notnull@*/ /*@out@*/       void *dest ,
  /*@notnull@*/ /*@in@*/        void *src ,
                                int32 count )
     /*@modifies *dest@*/
     /*@requires maxSet(dest) >= count /\ maxRead(src) >= count@*/
     /*@ensures maxRead(dest) == count@*/ ;

#if defined( USE_INLINE_MEMCPY ) || defined( USE_INLINE_MEMMOVE )
#include <string.h>
#endif

#if defined( USE_INLINE_MEMCPY )
#define HqMemCpy_Raw( _dest , _src , _count ) \
        (void)memcpy(( void * )( _dest ) , ( void * )( _src ) , ( size_t )( _count ))
#else
#define HqMemCpy_Raw( _dest , _src , _count ) \
        bcopy(( char * )( _src ) , ( char * )( _dest ) , ( int )( _count ))
#endif /* defined( USE_INLINE_MEMCPY ) */

#if ! defined( ASSERT_BUILD )
#define HqMemCpy( _dest , _src , _count ) HqMemCpy_Raw( _dest , _src , _count )
#else
#define HqMemCpy( _dest , _src , _count ) \
        HqMemCpy_Assert(( void * )( _dest ) , ( void * )( _src ) , ( int32 )( _count ))
#endif


/* HqMemMove - moves _count bytes from _src to _dest, correctly coping with
 *             overlapping blocks.
 */

void HQNCALL HqMemMove(
  /*@notnull@*/ /*@out@*/       void *dest ,
  /*@notnull@*/ /*@in@*/        void *src ,
                                int32 count )
     /*@modifies *dest@*/
     /*@requires maxSet(dest) >= count /\ maxRead(src) >= count@*/
     /*@ensures maxRead(dest) == count@*/ ;

#if defined( USE_INLINE_MEMMOVE )
#define HqMemMove( _dest , _src , _count ) \
        (void)memmove(( void * )( _dest ) , ( void * )( _src ) , ( size_t )( _count ))
#else
#if defined( BCOPY_OVERLAP_SAFE )
#define HqMemMove( _dest , _src , _count ) \
        bcopy(( char * )( _src ) , ( char * )( _dest ) , ( int )( _count ))
#else
#define HqMemMove( _dest , _src , _count ) \
        bcopy_safe(( char * )( _src ) , ( char * )( _dest ) , ( int32 )( _count ))
#endif /* defined( BCOPY_OVERLAP_SAFE ) */
#endif /* defined( USE_INLINE_MEMMOVE ) */

/*@=fcnmacros =incondefs@*/

#ifdef __cplusplus
}
#endif

#endif  /* __HQMEMCPY_H__ */

