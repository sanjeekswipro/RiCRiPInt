/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:eexec.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2000-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines for encoding and decoding eexec through filters
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "mm.h"
#include "mmcompat.h" /* mm_alloc_with_header */
#include "tables.h"
#include "fileio.h"

#include "eexec.h"

/* Decryption macros. */
#define DECRYPT_BYTE(byte,state)          (uint8)( (byte) ^ ( (state) >> 8 ))
#define DECRYPT_CHANGE_STATE(byte,state,add,mul) MACRO_START \
 (state) = (uint16) ( (state) + (byte) ) ; \
 (state) = (uint16) ( (state) * (mul) ) ; \
 (state) = (uint16) ( (state) + (add) ) ; \
MACRO_END
#define ENCRYPT_BYTE(byte,state)          (uint8)( (byte) ^ ( (state) >> 8 ))
#define ENCRYPT_CHANGE_STATE(byte,state,add,mul) MACRO_START \
 (state) = (uint16) ( (state) + (byte) ) ; \
 (state) = (uint16) ( (state) * (mul) ) ; \
 (state) = (uint16) ( (state) + (add) ) ; \
MACRO_END

#define EEXEC_ENCODE_BUFFER_SIZE 1024

/*-------------------------------------------------
 *
 * Routines for FILELIST structure for the eexec filter.
 *
 *   The eexec filter has a buffer size of 1 so that the
 *   fillbuff routine is effectively called for each getc
 *   from the overlying file. As in all filters, it is
 *   the fillbuff routine that does the decode. We cannot
 *   have a bigger buffer for eexec since there is no
 *   EOD marker in an eexec'd stream, so we may munch
 *   through characters that shouldn't be decoded.
 *
 *-------------------------------------------------
*/

static int32 eexecFilterFillBuff( register FILELIST *flptr )
{
  /* hex version of eexec - see below for binary version */
  int32 c1 , c2 , inbyte , outbyte ;
  register int8 *lhex_table ;
  register FILELIST *nflptr ;

  if ( isIEof( flptr ))
    return EOF ;

  lhex_table = char_to_hex_nibble ;
  nflptr = theIUnderFile( flptr ) ;

  c1 = Getc( nflptr ) ;
  c2 = Getc( nflptr ) ;
  inbyte = ( lhex_table[ c1 ] << 4 ) | lhex_table[ c2 ] ;

  /* Negative res implies problems with reading character. */
  if ( inbyte < 0 ) {
    while ( lhex_table[ c1 ] < 0 ) {
      if ( c1 < 0 ) {
        if ( isIIOError( nflptr ))
          return ioerror_handler( flptr ) ;
        ( void )(*theIMyCloseFile(flptr))( flptr, CLOSE_IMPLICIT ) ;
        return ( EOF ) ;
      }

      c1 = c2 ;
      c2 = Getc( nflptr ) ;
    }
    while ( lhex_table[ c2 ] < 0 ) {
      if ( c2 < 0 ) {
        if ( isIIOError( nflptr ))
          return ioerror_handler( flptr ) ;
        c2 = '0' ;
        break ;
      }
      c2 = Getc( nflptr ) ;
    }
    inbyte = ( lhex_table[ c1 ] << 4 ) | lhex_table[ c2 ] ;
  }

  /* Decrypt byte - only return if have skipped the seed. */
  outbyte = DECRYPT_BYTE( inbyte , theIFilterState( flptr )) ;
  /* Modify state. */
  DECRYPT_CHANGE_STATE( inbyte , theIFilterState( flptr ) ,
                       EEXEC_ADD , EEXEC_MULT ) ;

  theICount( flptr ) = 0 ; /* returning the one char */
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theIBuffer( flptr )[ -1 ] = (uint8)outbyte;
  return ( outbyte ) ;
}

static int32 eexecbFilterFillBuff( register FILELIST *flptr )
{
  /* binary version of eexec */
  int32 inbyte , outbyte ;
  register FILELIST *nflptr ;

  if ( isIEof( flptr ))
    return EOF ;

  nflptr = theIUnderFile( flptr ) ;

  if (( inbyte = Getc( nflptr )) == EOF ) {
    if ( isIIOError( nflptr ))
      return ioerror_handler( flptr ) ;
    ( void )(*theIMyCloseFile(flptr))( flptr, CLOSE_IMPLICIT ) ;
    return ( EOF ) ;
  }

  /* Decrypt byte - only return if have skipped the seed. */
  outbyte = DECRYPT_BYTE( inbyte , theIFilterState( flptr )) ;
  /* Modify state. */
  DECRYPT_CHANGE_STATE( inbyte , theIFilterState( flptr ) ,
                       EEXEC_ADD , EEXEC_MULT ) ;

  theICount( flptr ) = 0 ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theIBuffer( flptr )[ -1 ] = (uint8)outbyte;
  return ( outbyte ) ;
}

/** This routine is called as the first FillBuff routine. It decides whether
 * to use the hex or binary buffer FillBuff routine and replaces that routine
 * in the filter structure, before calling it. This is required because of a
 * race condition with the underlying file in eexecFilterInit, which prevents
 * all initialisation being done there. */
static int32 eexecFilterStartBuff( register FILELIST *flptr )
{
  int32 i, tmp;
  int32 t[ 8 ];
  register FILELIST *nflptr ;

  nflptr = theIUnderFile( flptr ) ;

  theIFilterState( flptr ) = EEXEC_SEED ;

/* Read first four bytes */
  for ( i = 0 ; i < 4 ; ++i )
    if (( t[ i ] = Getc( nflptr )) == EOF )
      return (*theIFileLastError( nflptr ))( nflptr ) ;

/* Different behaviour for binary and hex */
  if (( char_to_hex_nibble[ t[ 0 ] ] >= 0 ) &&
      ( char_to_hex_nibble[ t[ 1 ] ] >= 0 ) &&
      ( char_to_hex_nibble[ t[ 2 ] ] >= 0 ) &&
      ( char_to_hex_nibble[ t[ 3 ] ] >= 0 )) {
/* Read second four bytes. */
    for ( i = 4 ; i < 8 ; ++i ) {
      t[ i ] = Getc( nflptr ) ;
      if ( char_to_hex_nibble[ t[ i ] ] < 0 ) /* Not hex OR EOF. */
        return error_handler( IOERROR ) ;
    }
/* Seed the eexec. */
    for ( i = 0 ; i < 8 ; i += 2 ) {
      tmp = ( char_to_hex_nibble[ t[ i ] ] << 4 ) | char_to_hex_nibble[ t[ i + 1 ] ] ;
      DECRYPT_CHANGE_STATE(tmp, theIFilterState(flptr),
                           EEXEC_ADD , EEXEC_MULT ) ;
    }

    theIFillBuffer( flptr ) = eexecFilterFillBuff ;
  }
  else {
/* Seed the binary eexec. */
    DECRYPT_CHANGE_STATE( t[ 0 ] , theIFilterState( flptr ) ,
                         EEXEC_ADD , EEXEC_MULT ) ;
    DECRYPT_CHANGE_STATE( t[ 1 ] , theIFilterState( flptr ) ,
                         EEXEC_ADD , EEXEC_MULT ) ;
    DECRYPT_CHANGE_STATE( t[ 2 ] , theIFilterState( flptr ) ,
                         EEXEC_ADD , EEXEC_MULT ) ;
    DECRYPT_CHANGE_STATE( t[ 3 ] , theIFilterState( flptr ) ,
                         EEXEC_ADD , EEXEC_MULT ) ;

    theIFillBuffer( flptr ) = eexecbFilterFillBuff ;
  }

  return (*theIFillBuffer( flptr ))( flptr );
}

static Bool eexecFilterInit(FILELIST *flptr, OBJECT *args, STACK *stack)
{
  int32 bufsize ;
  int32 pop_args = 0 ;

  HQASSERT(flptr, "No filter to initialise." ) ;

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
      return error_handler(INVALIDACCESS) ;
    if ( ! FilterCheckArgs(flptr, args) )
      return FALSE ;
    OCopy(theIParamDict(flptr), *args) ;
  } else
    args = NULL ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(flptr, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  /* Require a buffer size of 1 for input, so we don't read past EOF */
  bufsize = isIInputFile(flptr) ? 0 : EEXEC_ENCODE_BUFFER_SIZE ;

  theIBuffer(flptr) = (uint8 *)mm_alloc_with_header(mm_pool_temp,
                                                    bufsize + 1,
                                                    MM_ALLOC_CLASS_EEXEC_BUFFER) ;
  if ( theIBuffer( flptr ) == NULL )
    return error_handler( VMERROR ) ;

  ++theIBuffer(flptr) ;
  theIBufferSize(flptr) = bufsize ;
  theIPtr(flptr) = theIBuffer(flptr) ;
  theICount(flptr) = 0 ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void eexecFilterDispose( register FILELIST *flptr )
{
  HQASSERT( flptr , "flptr NULL in eexecFilterDispose." ) ;

  if ( theIBuffer( flptr )) {
    mm_free_with_header( mm_pool_temp , ( mm_addr_t )(theIBuffer(flptr) - 1)) ;
    theIBuffer( flptr ) = NULL ;
  }
}

static Bool eexecEncodeBuffer( FILELIST *filter )
{
  register FILELIST *uflptr ;
  register int32    count ;
  register uint8    *ptr ;
  uint16 state ;

  HQASSERT( filter , "filter NULL" ) ;

  count = theICount( filter ) ;
  ptr = theIBuffer( filter ) ;
  uflptr = theIUnderFile( filter ) ;

  if ( ! count && ! isIClosing( filter ))
    return TRUE ;

  HQASSERT( uflptr , "uflptr NULL." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( ptr , "ptr NULL." ) ;

  state = (uint16)theIFilterState(filter) ;

  while ( count-- ) {
    uint8 cipher = ENCRYPT_BYTE( *ptr, state ) ;
    ENCRYPT_CHANGE_STATE(cipher, state, EEXEC_ADD, EEXEC_MULT) ;

    if ( TPutc(cipher, uflptr) == EOF )
      return error_handler( IOERROR ) ;

    ptr++ ;
  }

  theIFilterState(filter) = (int32)state ;
  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE ;
}

/* eexec filter initialisation */
void eexec_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("EExecEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0 , NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       eexecFilterInit,                      /* initfile */
                       FilterCloseFile,                      /* closefile */
                       eexecFilterDispose,                   /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       eexecEncodeBuffer,                    /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       EEXEC_SEED,                           /* state */
                       NULL, NULL, NULL ) ;
}

void eexec_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("EExecDecode") ,
                       FILTER_FLAG | READ_FLAG ,
                       0 , NULL , 0 ,
                       eexecFilterStartBuff,                 /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       eexecFilterInit,                      /* initfile */
                       FilterCloseFile,                      /* closefile */
                       eexecFilterDispose,                   /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       EEXEC_SEED,                           /* state */
                       NULL, NULL, NULL ) ;
}

/*
Log stripped */
