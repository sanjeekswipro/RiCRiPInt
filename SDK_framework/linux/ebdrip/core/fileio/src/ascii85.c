/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:ascii85.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of ASCII85 filter
 */

/* ----------------------------------------------------------------------------
   file:                ASCII 85          author:              Luke Tunmer
   creation date:       15-Jul-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "tables.h"

#include "ascii85.h"

/* Replacement for chartype.h from SWv20, to decouple compounds */
#define IsWhiteSpace(c) (isspace(c) || (c) == '\0')

/* Define for buffer size */

#define ASCII85BUFFSIZE 1024

typedef struct ASCII85DECODESTATE {
  int32 cached_error ;
} ASCII85DECODESTATE ;

static Bool delay_filter_error(ASCII85DECODESTATE *ascii85state, int32 error,
                               int32 *ret_bytes, int32 count)
{
  ascii85state->cached_error = error ;
  *ret_bytes = count ;
  return TRUE ;
}

/* ascii85 filter routines */

static Bool ascii85FilterInit( FILELIST *filter ,
                               OBJECT *args ,
                               STACK *stack )
{
  ASCII85DECODESTATE *ascii85state ;
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

  if ( isIInputFile( filter )) {
    /* decoding filter requires an extra byte at the begining in order
     * to cope with an UnGetc. However, ascii85DecodeBuffer writes four
     * bytes at a time, so we add 4 here to keep the resulting buffer word
     * aligned.
     */
    theIBuffer( filter ) = ( uint8 * )mm_alloc( mm_pool_temp ,
                                                ASCII85BUFFSIZE + 4 ,
                                                MM_ALLOC_CLASS_ASCII_85 ) ;

    if ( theIBuffer( filter ) == NULL )
      return error_handler( VMERROR ) ;

    theIBuffer( filter ) += 4 ; /* keep the buffer word-aligned */

    /* Allocate filter private state. */
    ascii85state = (ASCII85DECODESTATE *)mm_alloc( mm_pool_temp ,
                                                   sizeof(ASCII85DECODESTATE) ,
                                                   MM_ALLOC_CLASS_ASCII_85_STATE ) ;
    if ( ascii85state == NULL )
      return error_handler( VMERROR ) ;
    theIFilterPrivate( filter ) = ascii85state ;
    ascii85state->cached_error = NOT_AN_ERROR ;
  } else {
    theIBuffer( filter ) = ( uint8 * )mm_alloc( mm_pool_temp ,
                                                ASCII85BUFFSIZE ,
                                                MM_ALLOC_CLASS_ASCII_85 ) ;

    if ( theIBuffer( filter ) == NULL )
      return error_handler( VMERROR ) ;
  }

  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = ASCII85BUFFSIZE ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void ascii85FilterDispose( FILELIST *filter )
{
  ASCII85DECODESTATE *ascii85state ;
  HQASSERT( filter , "filter NULL in ascii85FilterDispose." ) ;

  if ( isIOutputFile( filter )) {
    if ( theIBuffer( filter )) {
      mm_free( mm_pool_temp ,
               ( mm_addr_t )theIBuffer( filter ) ,
               theIBufferSize( filter )) ;
      theIBuffer( filter ) = NULL ;
    }
  } else {
    if ( theIBuffer( filter )) {
      mm_addr_t buffer = ( mm_addr_t )( theIBuffer( filter ) - 4 ) ;
      mm_size_t size = theIBufferSize( filter ) + 4 ;
      mm_free( mm_pool_temp , buffer , size ) ;
      theIBuffer( filter ) = NULL ;

      ascii85state = theIFilterPrivate( filter ) ;
      if (ascii85state != NULL) {
        mm_free( mm_pool_temp , ascii85state , sizeof(ASCII85DECODESTATE) ) ;
        theIFilterPrivate( filter ) = NULL ;
      }
    }
  }
}

/**
 * Is character a valid part of an ascii85 5-tuple
 */
#define IS85(c) (( (c) >= '!') && ( (c) <= 'u'))

/**
 * This routine gets characters from the filter's underlying file, and
 * decodes them according to the ascii85 scheme. It returns the number
 * of bytes placed into the filter's buffer through the ret_bytes arguments.
 * This number is negative if the EOD is found.
 * It returns FALSE if anything goes wrong.
 *
 * \param[in,out]  filter       Input and output streams for the filter
 * \param[out]     ret_bytes    Number of bytes placed in output buffer
 *                              (negative if EOD found)
 * \return                      Success status
 */
static Bool ascii85DecodeBuffer(FILELIST *filter, int32 *ret_bytes)
{
  ASCII85DECODESTATE *ascii85state ;
  uint32    *ptr, *end;
  int32     i5, nbytes;
  FILELIST  *in;
  FOURBYTES  fb;

  HQASSERT(filter, "NULL /ASCII85Decode filter");
  ascii85state = theIFilterPrivate( filter ) ;
  HQASSERT( ascii85state, "ascii85state NULL in ascii85DecodeBuffer." ) ;

  if (ascii85state->cached_error != NOT_AN_ERROR)
    return error_handler( ascii85state->cached_error ) ;

  in = filter->underlying_file;
  ptr = (uint32 *)(filter->buffer);
  end = ptr + (filter->buffersize/sizeof(uint32));
  asInt(fb) = 0;

  HQASSERT(in, "NULL /ASCII85Decode input buffer");
  HQASSERT(ptr,  "NULL /ASCII85Decode output buffer");

  for ( i5 = 0, nbytes = 0; ptr < end; )
  {
    /*
     * Optimised code for the typical case :
     *
     * 0xffffffff (ascii85)-> s8W-! == 82/23/54/12/0
     * So if the first byte is less than 's'
     * then we do not need to test for overflow
     */
    if ( i5 == 0 && in->count > 5 && in->ptr[0] >= '!' && in->ptr[0] < 's' &&
         IS85(in->ptr[1]) && IS85(in->ptr[2]) &&
         IS85(in->ptr[3]) && IS85(in->ptr[4]) )
    {
      FOURBYTES  b4;

      asInt(b4) = POWER4 * (uint32)(in->ptr[0] - 33) +
                  POWER3 * (uint32)(in->ptr[1] - 33) +
                  POWER2 * (uint32)(in->ptr[2] - 33) +
                  POWER1 * (uint32)(in->ptr[3] - 33) +
                           (uint32)(in->ptr[4] - 33);

      HighOrder4Bytes(asBytes(b4));
      *ptr++ = asInt(b4);
      in->count -= 5;
      in->ptr   += 5;
    }
    else
    {
      register int32 ch = Getc(in);

      if ( IS85(ch) ) /* Part of valid ascii85 5-tuple */
      {
        if ( i5 == 4 )
        {
          if ( asInt(fb) > MAXHIGH4BYTES )
            return error_handler(IOERROR);
          asInt(fb) = POWER4 * (uint32)asBytes(fb)[BYTE_INDEX(0)] +
                      POWER3 * (uint32)asBytes(fb)[BYTE_INDEX(1)] +
                      POWER2 * (uint32)asBytes(fb)[BYTE_INDEX(2)] +
                      POWER1 * (uint32)asBytes(fb)[BYTE_INDEX(3)] +
                               (uint32)(ch - 33);
          HighOrder4Bytes(asBytes(fb));
          *ptr++ = asInt(fb);
          i5 = 0;
          asInt(fb) = 0;
        }
        else
          asBytes(fb)[BYTE_INDEX(i5++)] = (uint8)(ch - 33);
      }
      else if ( ch == 'z') /* special zero case */
      {
        if ( i5 != 0 )
          return error_handler(IOERROR);
        *ptr++ = 0;
      }
      else if ((ch == EOF ) || ( ch == '~' )) /* found EOD marker */
      {
        nbytes = (int32)(sizeof(uint32)*(ptr - (uint32 *)(filter->buffer)));

        if ( isIIOError(in) )
          return FALSE;
        if ( ch == '~' )
        {
          do
          {
            ch = Getc(in);
          } while ( IsWhiteSpace(ch) );

          if ( ch != '>')
            return error_handler(IOERROR);
        }
        if ( i5 > 0 ) /* only partial 5-tuple */
        {
          if ( i5 == 1 )
            return delay_filter_error(ascii85state, IOERROR, ret_bytes, nbytes);
          if ( asInt( fb ) > MAXHIGH4BYTES )
            return error_handler( IOERROR );
          asInt(fb) = POWER4 * (uint32)asBytes(fb)[BYTE_INDEX(0)] +
                      POWER3 * (uint32)asBytes(fb)[BYTE_INDEX(1)] +
                      POWER2 * (uint32)asBytes(fb)[BYTE_INDEX(2)] +
                      POWER1 * (uint32)asBytes(fb)[BYTE_INDEX(3)];
          if ( (int32)asBytes(fb)[BYTE_INDEX(i5 - 1)] >= 128 ) /* carry 1 */
            asInt(fb) += (uint32) (1 << (( 4 - i5 + 1 ) * 8 ));
          HighOrder4Bytes(asBytes(fb));
          *ptr++ = asInt(fb);
          nbytes = -(nbytes + i5 - 1);
        }
        else
          nbytes = -nbytes;
        break;
      }
      else if ( !IsWhiteSpace(ch) ) /* skip spaces, everything else errors */
        return error_handler(IOERROR);
    }
  }
  if ( nbytes == 0 )
    nbytes = (int32)(sizeof(uint32)*(ptr - (uint32 *)(filter->buffer)));
  *ret_bytes = nbytes;
  return TRUE;
}


static Bool ascii85EncodeBuffer( FILELIST *filter )
{
  register FILELIST *uflptr ;
  register int32    count , i ;
  register uint32   c ;
  register uint32   *ptr ;
  FOURBYTES         fb ;
  FOURBYTES         out ; /* high order bytes of the output 5-tuple */

  HQASSERT( filter , "filter NULL in ascii85EncodeBuffer." ) ;

  count = theICount( filter ) ;
  uflptr = theIUnderFile( filter ) ;
  ptr = ( uint32 * )theIBuffer( filter ) ;

  if ( ! count && ! isIClosing( filter ))
    return TRUE ;

  HQASSERT( uflptr , "uflptr NULL in ascii85EncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( ptr , "ptr NULL in ascii85EncodeBuffer." ) ;

  while ( count >= 4 ) {
    asInt( fb ) = *ptr ;
    HighOrder4Bytes( asBytes( fb )) ;

    c = asInt( fb ) / POWER4 ;
    asInt( fb ) -= c * POWER4 ;
    asBytes( out )[ 0 ] = ( uint8 )c ;
    c = asInt( fb ) / POWER3 ;
    asInt( fb ) -= c * POWER3 ;
    asBytes( out )[ 1 ] = ( uint8 )c ;
    c = asInt( fb ) / POWER2 ;
    asInt( fb ) -= c * POWER2 ;
    asBytes( out )[ 2 ] = ( uint8 )c ;
    c = asInt( fb ) / POWER1 ;
    asInt( fb ) -= c * POWER1 ;
    asBytes( out )[ 3 ] = ( uint8 )c ;

    if (( asInt( out ) == 0 ) && ( asInt( fb ) == 0 )) { /* output 'z' */
      if ( Putc( 'z' , uflptr ) == EOF )
        return error_handler( IOERROR ) ;
      theIFilterState( filter )++ ;
    } else { /* output five chars */
      for ( i = 0 ; i < 4 ; i++ ) {
        c = ( uint32 )asBytes( out )[ i ] + 33 ;
        if ( Putc( c , uflptr ) == EOF )
          return error_handler( IOERROR ) ;
      }
      c = asInt( fb ) + 33 ;
      if ( Putc( c , uflptr ) == EOF )
        return error_handler( IOERROR ) ;
      theIFilterState( filter ) += 5 ;
    }
    if ( theIFilterState( filter ) >= 65 ) {
      theIFilterState( filter ) = 0 ;
      if ( Putc( '\n' , uflptr ) == EOF )
        return error_handler( IOERROR ) ;
    }

    count -= 4 ; ptr++ ;
  }

  /* Copy remaining bytes (1, 2 or 3) to start of buffer. */

  if ( count )
    *(( uint32 * )theIBuffer( filter )) = *ptr ;

  if ( isIClosing( filter )) {
    if ( count > 0 ) {
      FOURBYTES fb ;
      FOURBYTES out ;
      uint32 i ;
      uint32 c ;

      /* There may be a faster way to do this, but my brain is suffering
       * enough. This conversion is only required once for the end of the
       * filter, and it is not worthwhile optimizing it. The problem
       * is that the bytes chopped off the end of the 5-tuple, may have
       * caused a carry when converted back to base-256. The description on
       * p.129 is oversimplified.
       */

      asInt( fb ) = 0 ;
      for ( i = 0 ; ( int32 )i < count ; i++ )
        asBytes( fb )[ BYTE_INDEX( i ) ] = theIBuffer( filter )[ i ] ;
      c = asInt( fb ) / POWER4 ;
      asInt( fb ) -= c * POWER4 ;
      asBytes( out )[ 0 ] = ( uint8 )c ;
      c = asInt( fb ) / POWER3 ;
      asInt( fb ) -= c * POWER3 ;
      asBytes( out )[ 1 ] = ( uint8 )c ;
      c = asInt( fb ) / POWER2 ;
      asInt( fb ) -= c * POWER2 ;
      asBytes( out )[ 2 ] = ( uint8 )c ;
      c = asInt( fb ) / POWER1 ;
      asInt( fb ) -= c * POWER1 ;
      asBytes( out )[ 3 ] = ( uint8 )c ;

      for ( i = 0 ; ( int32 )i <= count ; i++ ) {
        c = ( uint32 ) asBytes( out )[ i ] + 33 ;
        if ( Putc( c , uflptr ) == EOF )
          return error_handler( IOERROR ) ;
      }
    }

    if (( Putc( '~' , uflptr ) == EOF ) ||
        ( Putc( '>' , uflptr ) == EOF ))
      return error_handler( IOERROR ) ;
  }
  else {
    theICount( filter ) = count ;
    theIPtr( filter ) = theIBuffer( filter ) + count ;
  }

  return TRUE ;
}

void ascii85_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* ascii85 encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("ASCII85Encode"),
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       ascii85FilterInit,                    /* initfile */
                       FilterCloseFile,                      /* closefile */
                       ascii85FilterDispose,                 /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       ascii85EncodeBuffer ,                 /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void ascii85_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* ascii85 decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("ASCII85Decode"),
                       FILTER_FLAG | READ_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       ascii85FilterInit,                    /* initfile */
                       FilterCloseFile,                      /* closefile */
                       ascii85FilterDispose,                 /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError ,                   /* filterencode */
                       ascii85DecodeBuffer ,                 /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/* Log stripped */
