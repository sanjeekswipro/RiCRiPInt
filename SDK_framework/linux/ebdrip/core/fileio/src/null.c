/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:null.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Null filter implementation.
 */



/* ----------------------------------------------------------------------------
   file:                NULL              author:              Luke Tunmer
   creation date:       31-Jul-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "swctype.h"
#include "swdevice.h"
#include "objects.h"
#include "objstack.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"

#include "null.h"

/* null encode filter routines */

#define NULLBUFFSIZE 1024

static Bool nullFilterInit( FILELIST *filter,
                            OBJECT *args ,
                            STACK *stack )
{
  int32 pop_args = 0 ;

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
  } else
    args = NULL ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  theIBuffer( filter ) = ( uint8 * )mm_alloc( mm_pool_temp ,
                                              NULLBUFFSIZE ,
                                              MM_ALLOC_CLASS_NULL_BUFFER ) ;
  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = NULLBUFFSIZE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void nullFilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in nullFilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )theIBuffer( filter ) ,
             NULLBUFFSIZE ) ;
    theIBuffer( filter ) = NULL ;
  }
}

static Bool nullEncodeBuffer(  FILELIST *filter )
{
  int32 count ;
  FILELIST *uflptr ;
  uint8 *ptr ;

  HQASSERT( filter , "filter NULL in nullEncodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  ptr = theIBuffer( filter ) ;
  count = theICount( filter ) ;

  if ( ! count )
    return TRUE ;

  HQASSERT( uflptr , "uflptr NULL in nullEncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( ptr , "ptr NULL in nullEncodeBuffer." ) ;

  while ( count-- ) {
    int32 c ;

    c = *ptr++ ;
    if ( Putc( c , uflptr ) == EOF )
      return error_handler( IOERROR ) ;
  }

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE ;
}

void null_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* null encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("NullEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       nullFilterInit,                       /* initfile */
                       FilterCloseFile,                      /* closefile */
                       nullFilterDispose,                    /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       nullEncodeBuffer,                     /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/* Log stripped */
