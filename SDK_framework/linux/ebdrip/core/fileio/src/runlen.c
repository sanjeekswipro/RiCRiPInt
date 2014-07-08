/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:runlen.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Run-length filter implementation
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "hqmemset.h"

#include "runlen.h"

/* An encoding filter uses the following FILESTRUCT fields:
 *   descriptor:     The record size (given as arg to filter operator)
 *   filter_status : The current record count.
 *
 * A decoding filter uses:
 *   filter_status:  As with all decoding filters.
 */


static Bool runlengthFilterInit( FILELIST *filter,
                                 OBJECT *args ,
                                 STACK *stack )
{
  int32 buffsize = RUNLENGTHBUFFSIZE ;
  int32 pop_args = 0 ;

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  if ( isIOutputFile( filter )) { /* encoding filter */
    OBJECT *theo ;
    int32  record_size ;

    /* Can't pass args directly to RunLengthEncode, since it
     * has no dictionary only form (Adobe oversight?). We might have
     * to add our own if we ever want RLE for pdf out.
     */
    HQASSERT( ! args ,
              "Can't handle direct dictionary arguments in RunLengthEncode." ) ;

    if ( isEmpty(*stack) )
      return error_handler( STACKUNDERFLOW ) ;

    theo = theITop(stack) ;
    pop_args = 1 ;

    if ( oType(*theo) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    record_size = oInteger(*theo) ;
    if ( record_size < 0 ||
         record_size > 65536 )
      return error_handler( RANGECHECK ) ;

    theIDescriptor( filter ) = record_size ;
    if ( buffsize < record_size )
      buffsize = record_size ;
  }

  if ( ! args && theIStackSize(stack) >= pop_args ) {
    args = stackindex(pop_args, stack) ;
    if ( oType(*args) == ODICTIONARY )
      ++pop_args ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(args) )
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

  theIBuffer( filter ) = ( uint8 * )
                         mm_alloc_with_header( mm_pool_temp ,
                                               buffsize + 1 ,
                                               MM_ALLOC_CLASS_RUNLEN_BUFFER ) ;
  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++ ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = buffsize ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void runlengthFilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in runlengthFilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free_with_header( mm_pool_temp ,
                         ( mm_addr_t )( theIBuffer( filter ) - 1 )) ;
    theIBuffer( filter ) = NULL ;
  }
}

/**
 * This routine gets characters from the filter's underlying file, and
 * decodes them according to the runlength scheme. It returns the number
 * of bytes placed into the filter's buffer through the ret_bytes arguments.
 * This number is negative if the EOD is found.
 * It returns FALSE if anything goes wrong. The caller is responsible for
 * setting the IOERROR condition.
 *
 * \param[in,out]  filter       Input and output streams for the filter
 * \param[out]     ret_bytes    Number of bytes placed in output buffer
 *                              (negative if EOD found)
 * \return                      Success status
 */
static Bool runlengthDecodeBuffer(FILELIST *filter, int32 *ret_bytes)
{
  int32    count, length;
  FILELIST *uflptr;
  uint8    *ptr;

  HQASSERT(filter, "NULL /RunLengthDecode filter");

  uflptr = filter->underlying_file;
  ptr = filter->buffer;

  HQASSERT(uflptr, "NULL /RunLengthDecode input buffer");
  HQASSERT(ptr, "NULL /RunLengthDecode output buffer");

  count = 0;
  for (;;)
  {
    register int32 ch = Getc(uflptr);

    if ( ch == EOF || ch == EOD )
    {
      /* found the EOD marker, or EOF on underlying file */
      count = -count;
      break;
    }
    if ( ch < 128 ) /* ch + 1 bytes to follow */
    {
      length = ch + 1;
      if ( ( count + length ) <= filter->buffersize )
      {
        count += length;
        /* If buffer is full enough avoid Getc() loop overhead */
        if ( length < uflptr->count )
        {
          HqMemCpy(ptr, uflptr->ptr, length);
          uflptr->count -= length;
          uflptr->ptr   += length;
          ptr += length;
        }
        else
        {
          while ( length-- )
          {
            if ( (ch = Getc(uflptr)) == EOF )
              return error_handler(IOERROR);
            *ptr++ = (uint8)ch;
          }
        }
      }
      else /* current run will not fit in */
      {
        UnGetc(ch, uflptr);
        break;
      }
    }
    else /* ch > 128, replicate single byte */
    {
      length = 257 - ch;
      if ( ( count + length ) <= filter->buffersize )
      {
        if (( ch = Getc( uflptr )) == EOF )
          return error_handler(IOERROR);
        count += length;
        HqMemSet8(ptr, (uint8)ch, length);
        ptr += length;
      }
      else /* current replciation will not fit into buffer */
      {
        UnGetc(ch , uflptr);
        break;
      }
    }
  }
  *ret_bytes = count;
  return TRUE;
}

static Bool runlengthEncodeBuffer( FILELIST *filter )
{
  register FILELIST *uflptr ;
  register int32    count , c , length ;
  register int32    record_size , record_count ;
  register uint8    *ptr ;
  register uint8    *next , *prev , *lastptr ;

  HQASSERT( filter , "filter NULL in runlengthEncodeBuffer." ) ;

  ptr = theIBuffer( filter ) ;
  record_size = DEVICE_FILEDESCRIPTOR_TO_INT32(theIDescriptor( filter )) ;
  uflptr = theIUnderFile( filter ) ;
  count = theICount( filter ) ;

  if ( ! count && ! isIClosing( filter ))
    return TRUE ;

  HQASSERT( uflptr , "uflptr NULL in runlengthEncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( ptr , "ptr NULL in runlengthEncodeBuffer." ) ;

  if ( record_size == 0 )
    record_size = count ;

  while (( count >= record_size ) && count ) { /* do all the records in the buffer */
    record_count = record_size ;
    lastptr = ptr + record_size ;
    while ( record_count > 1 ) { /* calculate spans within the record */
      prev = ptr ;
      next = prev + 1 ;
      if (( int32 )( *next ) == ( int32 )( *prev )) {
        /* span of the same byte */
        length = 1 ;
        do {
          length++ ;
          next++ ;
          if (( length == 128 ) || ( next == lastptr ))
            break ;
        } while (( int32 )( *next ) == ( int32 )( *prev )) ;
        /* output span */
        c = 257 - length ;
        if (( Putc( c , uflptr ) == EOF ) ||
            ( Putc( *ptr , uflptr ) == EOF ))
          return error_handler( IOERROR ) ;
        ptr += length ;
        record_count -= length ;
      } else {
        /* get a non-repeating span */
        length = 0 ;
        do {
          if ( ++length == 128 )
            break ;
          next++ ; prev++ ;
          if ( next == lastptr ) {
            length++ ;
            break ;
          }
        } while (( int32 )( *next ) != ( int32 )( *prev )) ;
        c = length - 1 ;
        record_count -= length ;
        if ( Putc( c , uflptr ) == EOF )
          return error_handler( IOERROR ) ;
        while ( length-- ) {
          if ( Putc( *ptr , uflptr ) == EOF )
            return error_handler( IOERROR ) ;
          ptr++ ;
        }
      }
    }
    if ( record_count == 1 ) {
      /* output the last byte in the record */
      if (( Putc( 0 , uflptr ) == EOF ) ||
          ( Putc( *ptr , uflptr ) == EOF ))
        return error_handler( IOERROR ) ;
      ptr++ ;
    }
    count -= record_size ;
  }

  if ( isIClosing( filter )) {
    if ( count ) {
      /* force the last partial record out */
      theIDescriptor( filter ) = count ;
      if ( ! runlengthEncodeBuffer( filter ))
        return error_handler( IOERROR ) ;
    }
    else {
      if ( Putc( 128 , uflptr ) == EOF )
        return error_handler( IOERROR ) ;
    }
  }
  else {
    if ( count ) {
      /* an incomplete record is left in the buffer */
      HqMemMove( theIBuffer( filter ) , ptr , count ) ;
    }
  }

  theICount( filter ) = count ;
  theIPtr( filter ) = theIBuffer( filter ) + count ;

  return TRUE ;
}

void runlen_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* runlength encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("RunLengthEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       runlengthFilterInit,                  /* initfile */
                       FilterCloseFile,                      /* closefile */
                       runlengthFilterDispose,               /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       runlengthEncodeBuffer ,               /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void runlen_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* runlength decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("RunLengthDecode"),
                       FILTER_FLAG | READ_FLAG | EXPANDS_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       runlengthFilterInit,                  /* initfile */
                       FilterCloseFile,                      /* closefile */
                       runlengthFilterDispose,               /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       runlengthDecodeBuffer ,               /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/* Log stripped */
