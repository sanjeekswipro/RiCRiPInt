/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:aes.c(EBDSDK_P.1) $
 * $Id: src:aes.c,v 1.15.1.1.1.1 2013/12/19 11:25:14 anon Exp $
 *
 * Copyright (C) 2005-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * NOTE: Currently we have no need for the AES encode filter, so it
 * has NOT been implemented, although boiler plate code is in place
 * for it.
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
#include "hqmemcpy.h"
#include "hqmemset.h"

#include "hqaes.h" /* HQN implementation of AES. */
#include "aes.h"   /* The exported interface to this file. */

/* Make sure AESBUFFSIZE is a factor of 16 */
#define AESBUFFSIZE 4096

typedef struct AES_context {
  AES_KEY aeskey ;
  AES_KEY encrypt_aeskey ;

  /* AES CBC initilisation vector which is the first 16 bytes of the
     stream. */
  Bool found_iv ;
  uint8 iv[16] ;

  /* We process every buffer in 16 byte chunks. The max possible bytes
     which might need caching until we can process the next 16 byte
     block is 15 bytes. */
  int32 storage_len ;
  uint8 storage[15] ;

  /* The real length of the stream minus the padding at the end. NOTE:
     We don't currently use this. */
  int32 decrypted_stream_len ;
} AES_context ;

/* Initialise the filter.
 */

static Bool aesFilterInit(FILELIST *filter,
                           OBJECT *args, STACK *stack)
{
  AES_context *state = NULL;
  uint32 keylen;
  uint8 *key;
  int32 pop_args = 0 ;

  enum {
    thematch_AESKey,
    thematch_dummy
  } ;
  static NAMETYPEMATCH thematch[thematch_dummy + 1] = {
    { NAME_AESKey , 1, { OSTRING }},
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
  } else /* MUST have AESKey */
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
                                   AESBUFFSIZE + 1 ,
                                   MM_ALLOC_CLASS_AES_BUFFER ) ;

  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++;
  theIPtr( filter ) = theIBuffer( filter );
  theICount( filter ) = 0;
  theIBufferSize( filter ) = AESBUFFSIZE;

  state = mm_alloc( mm_pool_temp ,
                    sizeof( AES_context ) ,
                    MM_ALLOC_CLASS_AES_STATE ) ;
  if ( state == NULL )
    return error_handler( VMERROR ) ;

  HqMemZero((uint8 *)state, sizeof(AES_context)) ;

  args = thematch[thematch_AESKey].result;
  key = oString(*args);
  keylen = theILen(args);

  if (AES_set_decrypt_key(key, keylen * 8, &(state->aeskey) ) < 0 ||
      AES_set_encrypt_key(key, keylen * 8, &(state->encrypt_aeskey) ) < 0)
    return error_handler( CONFIGURATIONERROR ) ;

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
static void aesFilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in aesFilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 1 ) ,
             theIBufferSize( filter ) + 1 ) ;
    theIBuffer( filter ) = NULL ;
  }

  if ( theIFilterPrivate( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )theIFilterPrivate( filter ) ,
             sizeof( AES_context )) ;
    theIFilterPrivate( filter ) = NULL ;
  }
}

/* The main decode routine */

static Bool aesDecodeBuffer( FILELIST *filter , int32 *ret_bytes )
{
  FILELIST *uflptr ;
  uint8 *ptr, *tempbuf, *orig_buf, *insert_ptr ;
  AES_context *state ;
  int32 c, i, bufsize, new_bufsize, storage_len, remainder_sixteen,
        padding_len ;
  Bool is_eof ;
  static uint8 buffer[ AESBUFFSIZE + 1 ] ;

  HQASSERT( filter , "Null filter in AESDecodeBuffer." ) ;
  HQASSERT( ret_bytes , "Null ret_bytes in AESDecodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  state = ( AES_context * )theIFilterPrivate( filter ) ;
  ptr = theIBuffer( filter ) ;
  bufsize = theIBufferSize( filter ) ;

  HQASSERT( uflptr , "Null uflptr in AESDecodeBuffer." ) ;
  HQASSERT( state , "Null state in AESDecodeBuffer." ) ;
  HQASSERT( ptr , "Null ptr in AESDecodeBuffer." ) ;

  storage_len = state->storage_len ;

  HQASSERT(storage_len <= 16, "storage len is greater than 16") ;

  orig_buf = &buffer[0] ;
  new_bufsize = bufsize + storage_len ;

  if ( new_bufsize > AESBUFFSIZE + 1) {
    orig_buf = mm_alloc(mm_pool_temp, new_bufsize ,
                        MM_ALLOC_CLASS_AES_BUFFER);
  }
  if (orig_buf == NULL)
    return error_handler( VMERROR ) ;

  /* orig_buf is only used to deallocate the buffer at the very end
     from now on. */
  tempbuf = orig_buf ;

  insert_ptr = tempbuf + storage_len ;

  padding_len  = 0 ;

  is_eof = FALSE ;
  /* read stream into memory buffer */
  for ( i = 0 ; i < bufsize ; i++ ) {
    if (( c = Getc( uflptr )) == EOF ) {
      bufsize = i ;
      is_eof = TRUE ;
      break ;
    }
    *insert_ptr = (uint8)c ;
    insert_ptr++ ;
  }

  /* Peek ahead one byte to see if we are really at the EOF and hence
     treat this buffer as the last one. See request 64870. */
  if (! is_eof) {
    if ((c = Getc( uflptr )) == EOF) {
      is_eof = TRUE ;
    } else {
      UnGetc(c, uflptr);
    }
  }

  if (storage_len > 0) {
    HqMemCpy(tempbuf, state->storage, storage_len) ;
    bufsize += storage_len ;
  }

  /* bufsize is actual length of bytes we need to process. */

  remainder_sixteen = bufsize % 16 ;

  /* Chop off any remainder into our storage. */
  if (remainder_sixteen != 0) {
    HqMemCpy(state->storage, &tempbuf[bufsize - remainder_sixteen - 1],
             remainder_sixteen) ;
    state->storage_len = remainder_sixteen ;
  }

  bufsize -= remainder_sixteen ;

  HQASSERT(bufsize % 16 == 0, "bufsize modulo 16 is not zero") ;

  /* Do we have and can get the iv */
  if (bufsize >= 16 && ! state->found_iv) {
    HqMemCpy(state->iv, tempbuf, 16) ;
    state->found_iv = TRUE ;
    tempbuf += 16 ;
    bufsize -= 16 ;
  }

  if (bufsize >= 16) {
    /* AES_cbc_encrypt() loops around the 16 byte CBC blocks */
    AES_cbc_encrypt(tempbuf, ptr, bufsize, &(state->aeskey),
                    state->iv, AES_DECRYPT) ;

    state->decrypted_stream_len += bufsize ;

    if (is_eof) {

      /** \todo Get the last byte in the decrypted stream which ought
         to specify the padding length. Currently it appears not to,
         so we ignore for now. */
      padding_len = (int32)ptr[bufsize - 1] ;

      state->decrypted_stream_len -= padding_len ;
      if (bufsize >= padding_len)
        bufsize -= padding_len ;

      *ret_bytes = -bufsize ;
    } else {
      *ret_bytes = bufsize ;
    }

  } else {
    *ret_bytes = 0 ;
  }

  if (new_bufsize > AESBUFFSIZE + 1)
    mm_free(mm_pool_temp, orig_buf, new_bufsize) ;

  return TRUE ;
}

static Bool aesEncodeBuffer( FILELIST *filter )
{
  FILELIST *uflptr ;
  uint8 *ptr, *tempbuf ;
  AES_context *state ;
  int32 count, bufsize;
  static uint8 buffer[ AESBUFFSIZE + 1 ] ;

  HQASSERT( filter , "Null filter in AESEncodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  state = ( AES_context * )theIFilterPrivate(filter) ;
  ptr = theIBuffer( filter ) ;
  count = theICount( filter ) ;
  bufsize = count ;

  if ( ! count )
    return TRUE ;

  HQASSERT( uflptr , "Null uflptr in AESEncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( state , "Null state in AESEncodeBuffer." ) ;
  HQASSERT( ptr , "Null ptr in AESEncodeBuffer." ) ;

  tempbuf = &buffer[0] ;
  if ( bufsize > AESBUFFSIZE + 1) {
    tempbuf = mm_alloc(mm_pool_temp, bufsize,
                       MM_ALLOC_CLASS_AES_BUFFER);
  }
  if (tempbuf == NULL)
    return error_handler( VMERROR ) ;

  /** \todo THIS IS NOT IMPLEMENTED */

  ptr = tempbuf ;
  while ( count-- ) {
    int32 c = *ptr++ ;

    if ( Putc( c , uflptr ) == EOF ) {
      if (bufsize > AESBUFFSIZE + 1)
        mm_free(mm_pool_temp, tempbuf, bufsize) ;
      return error_handler( IOERROR ) ;
    }
  }

  if (bufsize > AESBUFFSIZE + 1)
    mm_free(mm_pool_temp, tempbuf, bufsize) ;

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE;
}

void aes_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* aes encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("AESEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       aesFilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       aesFilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       aesEncodeBuffer ,                     /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void aes_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* aes decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("AESDecode") ,
                       FILTER_FLAG | READ_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       aesFilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       aesFilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       aesDecodeBuffer ,                     /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/* ============================================================================
* Log stripped */
