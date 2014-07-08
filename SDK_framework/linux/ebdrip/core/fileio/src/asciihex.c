/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:asciihex.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of ASCIIHEX Filter
 */



/* ----------------------------------------------------------------------------
   file:                ASCII HEX         author:              Luke Tunmer
   creation date:       15-Jul-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */
#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "tables.h"

#include "asciihex.h"

/* Replacement for chartype.h from SWv20, to decouple compounds */
#define IsWhiteSpace(c) (isspace(c) || (c) == '\0')

/* Definition for buffer size */

#define ASCIIHEXBUFFSIZE 1024

/* asciihex filter routines */

static Bool asciihexFilterInit( FILELIST *filter ,
                                OBJECT *args ,
                                STACK *stack )
{
  int32 pop_args = 0 ;

  HQASSERT( filter , "filter NULL in asciihexFilterInit." ) ;

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

  theIBuffer( filter ) = mm_alloc( mm_pool_temp ,
                                   ASCIIHEXBUFFSIZE + 1 ,
                                   MM_ALLOC_CLASS_ASCII_HEX ) ;

  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++ ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = ASCIIHEXBUFFSIZE ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void asciihexFilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in asciihexFilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 1 ) ,
             ASCIIHEXBUFFSIZE + 1 ) ;
    theIBuffer( filter ) = NULL ;
  }
}

static Bool asciihexDecodeBuffer( FILELIST *filter, int32 *ret_bytes )
{
  register int8     *lhex_table = char_to_hex_nibble ;
  register uint8    *ptr ;
  register FILELIST *uflptr ;
  register int32    c, c1 , c2 , i ;

  HQASSERT( filter , "filter NULL in asciihexDecodeBuffer." ) ;
  HQASSERT( ret_bytes , "ret_bytes NULL in asciihexDecodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  ptr = theIBuffer( filter ) ;

  HQASSERT( uflptr , "uflptr NULL in asciihexDecodeBuffer." ) ;
  HQASSERT( ptr , "ptr NULL in asciihexDecodeBuffer." ) ;

  for ( i = 0 ; i < theIBufferSize( filter ) ; i++ ) {
    for ( ; ; ) {
      if ((( c = Getc( uflptr )) == EOF ) || c == '>' ) {
        *ret_bytes = - i ;
        return TRUE ;
      }
      if (( c1 = ( int32 )lhex_table[ c ]) >= 0 ) {
        break;
      } else if ( ! IsWhiteSpace( c )) {
        /* Adobe hack: chars which are not asciihex or whitespace
         * cause the filter to die with an ioerror - but not before the
         * buffered characters are returned to the caller. So if there are
         * buffered characters, return them (plus this one), and put the
         * current char back, so that the next time we come into this routine
         * we can return with an error. The incorrect character will not be
         * seen by this filters caller, and it will be consumed from the
         * underlying file.
         */
        if ( i == 0 ) {
          return error_handler( IOERROR ) ;
        } else {
          UnGetc( c , uflptr ) ;
          *ptr = ( uint8 )c ;
          /* only return the number of bytes encoded so far
           * without counting this one. */
          *ret_bytes = i ;
          return TRUE ;
        }
      }
    }
    c1 <<= 4 ;
    for ( ; ; ) {
      if ((( c = Getc( uflptr )) == EOF ) || c == '>' ) {
        *ptr = ( uint8 )c1 ;
        *ret_bytes = - ( i + 1 ) ;
        return TRUE ;
      }
      if (( c2 = ( int32 )lhex_table[ c ]) >= 0 ) {
        break ;
      } else if ( ! IsWhiteSpace( c )) {
        if ( i == 0 ) {
          return error_handler( IOERROR ) ;
        } else {
          UnGetc( c , uflptr ) ;
          *ptr = ( uint8 )c1 ;
          *ret_bytes = i + 1 ;
          return TRUE ;
        }
      }
    }
    *ptr++ = ( uint8 )( c1 + c2 ) ;
  }

  *ret_bytes = i ;

  return TRUE ;
}

static Bool asciihexEncodeBuffer( FILELIST *filter )
{
  register FILELIST *uflptr ;
  register int32    count , c1 , c2 ;
  register uint8    *lhex_table ;
  register uint8    *ptr ;

  HQASSERT( filter , "filter NULL in asciihexEncodeBuffer." ) ;

  count = theICount( filter ) ;
  lhex_table = nibble_to_hex_char ;
  ptr = theIBuffer( filter ) ;
  uflptr = theIUnderFile( filter ) ;

  if ( ! count && ! isIClosing( filter ))
    return TRUE ;

  HQASSERT( uflptr , "uflptr NULL in asciihexEncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( ptr , "ptr NULL in asciihexEncodeBuffer." ) ;

  while ( count-- ) {
    c1 = ( int32 )lhex_table[( int32 )*ptr >> 4 ] ;
    c2 = ( int32 )lhex_table[( int32 )*ptr & 15 ] ;
    if (( Putc( c1 , uflptr ) == EOF ) ||
        ( Putc( c2 , uflptr ) == EOF ))
      return error_handler( IOERROR ) ;
    ptr++ ;
    if (( theIFilterState( filter ) += 2 ) == 64 ) {
      theIFilterState( filter ) = 0 ;
      if ( Putc( '\n' , uflptr ) == EOF )
        return error_handler( IOERROR ) ;
    }
  }

  if ( isIClosing( filter ) && ( Putc( '>', uflptr ) == EOF ))
    return error_handler( IOERROR ) ;

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE ;
}

void asciihex_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* asciihex encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("ASCIIHexEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0 , NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       asciihexFilterInit,                   /* initfile */
                       FilterCloseFile,                      /* closefile */
                       asciihexFilterDispose,                /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       asciihexEncodeBuffer ,                /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void asciihex_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* asciihex decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("ASCIIHexDecode"),
                       FILTER_FLAG | READ_FLAG ,
                       0 , NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       asciihexFilterInit,                   /* initfile */
                       FilterCloseFile,                      /* closefile */
                       asciihexFilterDispose,                /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       asciihexDecodeBuffer ,                /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/* Log stripped */
