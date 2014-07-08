/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:rc4.c(EBDSDK_P.1) $
 * $Id: src:rc4.c,v 1.35.1.1.1.1 2013/12/19 11:25:14 anon Exp $
 *
 * Copyright (C) 2005-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * NOTE: Currently we have no need for the RC4 encode filter, so its
 * held here until there is a need for it.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "swctype.h"
#include "swdevice.h"
#include "dictscan.h"
#include "objects.h"
#include "objstack.h"
#include "fileio.h"
#include "mm.h"
#include "namedef_.h"

#include "hqrc4.h" /* HQN implementation of RC4. */
#include "rc4.h"   /* The exported interface to this file. */

#define RC4BUFFSIZE 4096

/* Initialise the filter.
 */

static Bool rc4FilterInit(FILELIST *filter,
                           OBJECT *args, STACK *stack)
{
  RC4_KEY* state = NULL;
  uint32 keylen;
  uint8 *key;
  int32 pop_args = 0 ;

  enum {
    thematch_RC4Key,
    thematch_dummy
  } ;
  static NAMETYPEMATCH thematch[thematch_dummy + 1] = {
    { NAME_RC4Key , 1, { OSTRING }},
      DUMMY_END_MATCH
  };

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)))
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( args , thematch ))
      return FALSE ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
  } else /* MUST have RC4Key */
    return error_handler( TYPECHECK ) ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  theIBuffer( filter ) = mm_alloc( mm_pool_temp ,
                                   RC4BUFFSIZE + 1 ,
                                   MM_ALLOC_CLASS_RC4_BUFFER ) ;
  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++;
  theIPtr( filter ) = theIBuffer( filter );
  theICount( filter ) = 0;
  theIBufferSize( filter ) = RC4BUFFSIZE;

  state = mm_alloc( mm_pool_temp ,
                    sizeof( RC4_KEY ) ,
                    MM_ALLOC_CLASS_RC4_STATE ) ;

  if ( state == NULL )
    return error_handler( VMERROR ) ;

  args = thematch[thematch_RC4Key].result;
  key = oString(*args);
  keylen = theILen(args);

  RC4_set_key (state, keylen, key) ;

  theIFilterPrivate( filter ) = state;
  theIFilterState( filter ) = FILTER_INIT_STATE;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE;
}

/* Release any structures, bearing in mind that the
 * filter could be in a partially constructed state.
 */

static void rc4FilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in rc4FilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 1 ) ,
             theIBufferSize( filter ) + 1 ) ;
    theIBuffer( filter ) = NULL ;
  }

  if ( theIFilterPrivate( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )theIFilterPrivate( filter ) ,
             sizeof( RC4_KEY )) ;
    theIFilterPrivate( filter ) = NULL ;
  }
}

/* The main decode routine */

static Bool rc4DecodeBuffer( FILELIST *filter , int32 *ret_bytes )
{
  FILELIST *uflptr ;
  uint8 *ptr, *tempbuf ;
  RC4_KEY *state ;
  int32 c ;
  int32 i, bufsize ;
  static uint8 buffer[ RC4BUFFSIZE + 1 ] ;

  HQASSERT( filter , "Null filter in RC4DecodeBuffer." ) ;
  HQASSERT( ret_bytes , "Null ret_bytes in RC4DecodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  state = ( RC4_KEY * )theIFilterPrivate( filter ) ;
  ptr = theIBuffer( filter ) ;
  bufsize = theIBufferSize( filter ) ;

  HQASSERT( uflptr , "Null uflptr in RC4DecodeBuffer." ) ;
  HQASSERT( state , "Null state in RC4DecodeBuffer." ) ;
  HQASSERT( ptr , "Null ptr in RC4DecodeBuffer." ) ;

  tempbuf = &buffer[0] ;
  if ( bufsize > RC4BUFFSIZE + 1) {
    tempbuf = mm_alloc(mm_pool_temp, bufsize,
                       MM_ALLOC_CLASS_RC4_BUFFER);
  }
  if (tempbuf == NULL)
    return error_handler( VMERROR ) ;

  for ( i = 0 ; i < bufsize ; i++ ) {
    if (( c = Getc( uflptr )) == EOF ) {
      *ret_bytes = -i ;
      RC4(state, i, tempbuf, ptr) ;
      if (bufsize > RC4BUFFSIZE + 1)
        mm_free(mm_pool_temp, tempbuf, bufsize) ;
      return TRUE ;
    }
    tempbuf[i] = (uint8)c ;
  }

  RC4(state, i, tempbuf, ptr) ;
  *ret_bytes = i ;

  if (bufsize > RC4BUFFSIZE + 1)
    mm_free(mm_pool_temp, tempbuf, bufsize) ;

  return TRUE ;
}

static Bool rc4EncodeBuffer( FILELIST *filter )
{
  FILELIST *uflptr ;
  uint8 *ptr, *tempbuf ;
  RC4_KEY *state ;
  int32 count, bufsize;
  static uint8 buffer[ RC4BUFFSIZE + 1 ] ;

  HQASSERT( filter , "Null filter in RC4EncodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  state = ( RC4_KEY * )theIFilterPrivate(filter) ;
  ptr = theIBuffer( filter ) ;
  count = theICount( filter ) ;
  bufsize = count ;

  if ( ! count )
    return TRUE ;

  HQASSERT( uflptr , "Null uflptr in RC4EncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( state , "Null state in RC4EncodeBuffer." ) ;
  HQASSERT( ptr , "Null ptr in RC4EncodeBuffer." ) ;

  tempbuf = &buffer[0] ;
  if ( bufsize > RC4BUFFSIZE + 1) {
    tempbuf = mm_alloc(mm_pool_temp, bufsize,
                       MM_ALLOC_CLASS_RC4_BUFFER);
  }
  if (tempbuf == NULL)
    return error_handler( VMERROR ) ;

  RC4(state, bufsize, ptr, tempbuf) ;

  ptr = tempbuf ;
  while ( count-- ) {
    int32 c = *ptr++ ;

    if ( Putc( c , uflptr ) == EOF ) {
      if (bufsize > RC4BUFFSIZE + 1)
        mm_free(mm_pool_temp, tempbuf, bufsize) ;
      return error_handler( IOERROR ) ;
    }
  }

  if (bufsize > RC4BUFFSIZE + 1)
    mm_free(mm_pool_temp, tempbuf, bufsize) ;

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE;
}

void rc4_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* rc4 encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("RC4Encode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       rc4FilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       rc4FilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       rc4EncodeBuffer ,                     /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void rc4_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* rc4 decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("RC4Decode") ,
                       FILTER_FLAG | READ_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       rc4FilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       rc4FilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       rc4DecodeBuffer ,                     /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/* ============================================================================
* Log stripped */
