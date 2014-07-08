/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:flate.c(EBDSDK_P.1) $
 * $Id: src:flate.c,v 1.76.1.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Flate encode and decode filters
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "namedef_.h"
#include "monitor.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"

#include "diff.h"
#include "flate.h"
#include "zlib.h"

#define FLATEENCODEBUFFSIZE 1024
#define COMPRESSION_BUFF_SIZE (1024 * 32)

/* This is what the filter will ask for each time. */
#define FLATEDECODEBUFFSIZE 1024 * 8
/* Only works on MS Windows
#define DEBUG_FLATE_PERFORMANCE 1
*/

#if defined(DEBUG_FLATE_PERFORMANCE)
/* This code will work on Win9X or NT/2K and probably XP. Actually, it
would even work in Linux if you can figure out how to get GCC to emit
inline assembler!

Of course, the time comes out in cycles, not seconds, and oddly there
seems to be no API to get the machine's speed. You can either
"calibrate" the results (by getting the count, sleeping for (say) one
second, getting the count again, and doing some trivial math), or just
use cycles directly and don't worry about seconds (it's great for
comparing one algorithm to another, or finding the slow parts of your
program)

One warning, however: certain machines, notably laptops, can slow down
their processor speed when nothing important seems to be
happening. Since the calibration sleep is exactly one of those times,
you can get some seriously wrong results from the calibration. */
__int64 GetMachineCycleCount()
{
   __int64 cycles;
   _asm rdtsc; /* won't work on 486 or below - only pentium or above */
   _asm lea ebx,cycles;
   _asm mov [ebx],eax;
   _asm mov [ebx+4],edx;
   return cycles;
}
#endif

typedef struct {
  /* The status of the ZLIB/DEFLATE filter, enumerated above. */
  int32  status ;

  /* Optional dictionary items. */
  int32  effort ;

  /* For flate encoding, this is the difference buffer. dbuflen will
     be -1 if no differencing was done. */

  /* For flate decode, we deflate into this buffer if we have a
     predictor greater than one, and then do differencing into the
     output. If no differencing is needed, we deflate directly into
     the output buffer. */
  uint8  *dbuf ;
  int32  dbuflen ;

  z_stream c_stream;

  /* Stuff for differencing (if necessary). */
  DIFF_STATE diff ;

  /* Used to detect EOF in decode filter. */
  Bool is_eof ;

  /* Whether or not a missing or mismatched checksum means raises an
     error or merely a warning. Ought to be Bool, but its an int which
     comes from the PS world. */
  int32 error_on_checksum_failure ;

#if defined(DEBUG_FLATE_PERFORMANCE)
  __int64 start_cpu_cycle_count ;
  Bool first_time_called ;
#endif

} FLATE_STATE ;

static void* flateenc_alloc(
  void*   opaque,
  uint32  items,
  uint32  size)
{
  UNUSED_PARAM(void*, opaque);
  return(mm_alloc_with_header(mm_pool_temp, (mm_size_t)(items*size), MM_ALLOC_CLASS_FLATE_ZLIB));
  /* NB. vmerrors are handled in the clients below */
} /* flateenc_alloc */

static void flateenc_free(
  void*   opaque,
  void*   p)
{
  UNUSED_PARAM(void*, opaque);
  mm_free_with_header(mm_pool_temp, p);

} /* flateenc_free */

/* ----------------------------------------------------------------------------
 * Flate encode
 * ----------------------------------------------------------------------------
 */
static Bool flateEncodeFilterInit( FILELIST *filter,
                                   OBJECT *args ,
                                   STACK *stack )
{
  FLATE_STATE *state = NULL ;
  int zliberr;
  int32 pop_args = 0 ;

  enum {
    match_Effort, match_n_entries
  } ;
  static NAMETYPEMATCH thematch[match_n_entries + 1] = {
    { NAME_Effort | OOPTIONAL, 1, { OINTEGER }},
    DUMMY_END_MATCH
  };

  HQASSERT(isIOutputFile(filter), "FlateEncode is a not an output filter.") ;

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  theIFilterPrivate( filter ) = NULL ;

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( args , thematch ))
      return FALSE ;
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
                                              FLATEENCODEBUFFSIZE + 1,
                                              MM_ALLOC_CLASS_FLATE_BUFFER ) ;
  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  /* The filter machinery writes information into the byte before the
     buffer pointer. */
  theIBuffer( filter )++ ;

  /* Allocate my private state structure. */
  state = ( FLATE_STATE * )mm_alloc( mm_pool_temp ,
                                     sizeof( FLATE_STATE ) ,
                                     MM_ALLOC_CLASS_FLATE_STATE ) ;
  if ( state == NULL ) {
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEENCODEBUFFSIZE + 1) ;
    return error_handler( VMERROR ) ;
  }

  state->c_stream.zalloc = flateenc_alloc;
  state->c_stream.zfree = flateenc_free;
  state->c_stream.opaque = (voidpf)0;
  state->c_stream.next_in = Z_NULL;
  state->c_stream.avail_in = 0;
  state->c_stream.next_out = Z_NULL;
  state->c_stream.avail_out = 0;

  state->effort = Z_DEFAULT_COMPRESSION;

  /* These two are used for encode, but initalise non the less. */
  state->is_eof = FALSE ;
  state->error_on_checksum_failure = FALSE ;

  state->dbuflen = 1024 + 1; /* A guess start size. */
  state->dbuf = ( uint8 * )mm_alloc( mm_pool_temp ,
                                     state->dbuflen ,
                                     MM_ALLOC_CLASS_FLATE_STATE ) ;
  if ( state->dbuf == NULL ) {
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEENCODEBUFFSIZE + 1 ) ;
    mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE)) ;
    return error_handler( VMERROR ) ;
  }

  if (args) {
    /* get args out of the the dictionary match structure */
    if ( thematch[match_Effort].result != NULL )
      state->effort = oInteger(*thematch[match_Effort].result) ;
  }

  /* Init differencing (maybe) */
  if ( ! diffInit( &state->diff , args )) {
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEENCODEBUFFSIZE + 1) ;
    if (state->dbuf != NULL) {
      mm_free( mm_pool_temp , ( mm_addr_t )state->dbuf ,
               state->dbuflen ) ;
    }
    mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE));
    return FALSE ;
  }

  zliberr = deflateInit(&(state->c_stream), state->effort);
  if (zliberr != Z_OK) {
    /* Close differencing */
    diffClose(&state->diff);
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEENCODEBUFFSIZE + 1) ;
    if (state->dbuf != NULL) {
      mm_free( mm_pool_temp , ( mm_addr_t )state->dbuf ,
               state->dbuflen ) ;
    }
    mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE));

    if (zliberr == Z_MEM_ERROR)
      return error_handler( VMERROR ) ;
    else
      return error_handler( IOERROR ) ;
  }

  theIFilterPrivate( filter ) = state ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = FLATEENCODEBUFFSIZE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void flateEncodeFilterDispose( FILELIST *filter )
{
  int zliberr;
  FLATE_STATE *state ;

  HQASSERT( filter , "filter NULL in flateEncodeFilterDispose." ) ;

  if (theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEENCODEBUFFSIZE + 1) ;
    theIBuffer( filter ) = NULL ;
  }

  /* Free the state structure too. */

  state = ( FLATE_STATE * )theIFilterPrivate( filter ) ;
  if ( state )
  {
      diffClose( &state->diff ) ;

      zliberr = deflateEnd(&(state->c_stream));
      if (zliberr != Z_OK) {
        /* TBD: can I return a non zero reply */
      }

      if (state->dbuf != NULL) {
        mm_free( mm_pool_temp , ( mm_addr_t )state->dbuf ,
                 state->dbuflen ) ;
        state->dbuf = NULL;
        state->dbuflen = -1;
      }

      mm_free( mm_pool_temp ,
               ( mm_addr_t )state ,
               sizeof( FLATE_STATE )) ;
      theIFilterPrivate( filter ) = NULL ;
  }
}

static Bool flateEncodeBuffer( FILELIST *filter )
{
  int32 inputbuflen ;
  uint8 *inputbuf ;
  uint8 *outputbuf ;
  int32 compressedlen ;
  uint8 abuf[COMPRESSION_BUFF_SIZE] ;
  int32 dbuflen ;

  FILELIST *uflptr ;
  int zliberr ;
  FLATE_STATE *state ;

  uint8 *src_to_diff_ptr = NULL ;
  int32 src_to_diff_count = -1;

  HQASSERT( filter , "filter NULL in flateEncodeBuffer." ) ;

  state = ( FLATE_STATE * )theIFilterPrivate( filter ) ;
  HQASSERT( state , "state NULL in flateEncodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  HQASSERT( uflptr , "uflptr NULL in flateEncodeBuffer." ) ;

  inputbuf = theIBuffer( filter ) ;
  HQASSERT( inputbuf , "inputbuf NULL in flateEncodeBuffer." ) ;

  outputbuf = &abuf[0] ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  dbuflen = state->dbuflen ; /* We are destructive on this. */

  /* Maybe apply diff on buffer. */

  src_to_diff_ptr = inputbuf ;
  inputbuflen = src_to_diff_count = theICount( filter ) ;

  if (! diffEncode( &state->diff, src_to_diff_ptr, &inputbuflen,
                    state->dbuf, &dbuflen ))
    return FALSE ;


  /* Optimisation for when there is no diff's */
  if (dbuflen == -1) {
    state->c_stream.next_in = (Bytef*)inputbuf ;
    state->c_stream.avail_in = inputbuflen ;
    src_to_diff_count = 0 ;
  } else {
    state->c_stream.next_in = (Bytef*)state->dbuf ;
    state->c_stream.avail_in = dbuflen ;

    src_to_diff_count -= inputbuflen ;
    src_to_diff_ptr += inputbuflen ;
  }

  if (isIClosing( filter ))
  {
    for (;;) {
      outputbuf = &abuf[0] ;
      state->c_stream.next_out = outputbuf ;
      state->c_stream.avail_out = COMPRESSION_BUFF_SIZE ;

      /* if we have exhausted all the data that needs to
       * be differenced (i.e src_count == 0), then Z_FINISH
       * the stream. O/w continue to output the data as usual.
       * The src_count == 0 branch might be called multiple times.
       */

      if ( src_to_diff_count ==  0 )
      {
        zliberr = deflate(&(state->c_stream), Z_FINISH) ;

        if (zliberr != Z_STREAM_END && zliberr != Z_OK) {
          if (zliberr == Z_MEM_ERROR)
            return error_handler( VMERROR ) ;
          else
            return error_handler( IOERROR ) ;
        }
      }
      else
      {
        /* This branch should only occur if there was differencing done
         * AND there differencing had to be done in multiple parts.
         */
        zliberr = deflate(&(state->c_stream), Z_NO_FLUSH) ;

        if (zliberr != Z_STREAM_END && zliberr != Z_OK) {
          if (zliberr == Z_MEM_ERROR)
            return error_handler( VMERROR ) ;
          else
            return error_handler( IOERROR ) ;
        }

        inputbuflen = src_to_diff_count ;
        dbuflen = state->dbuflen ;

        if (! diffEncode( &state->diff, src_to_diff_ptr , &inputbuflen,
                          state->dbuf, &dbuflen ))
          return FALSE ;

        HQASSERT( dbuflen != -1 , "differencing failed in FlateEncode" ) ;

        state->c_stream.next_in = (Bytef*)state->dbuf ;
        state->c_stream.avail_in = dbuflen ;

        src_to_diff_count -= inputbuflen ;
        src_to_diff_ptr += inputbuflen ;

        HQASSERT (src_to_diff_count >= 0,
                  "Negative count for data to difference in flateEncodeBuffer" ) ;
      }


      /* We need to work out how many bytes were output */
      compressedlen = COMPRESSION_BUFF_SIZE - state->c_stream.avail_out ;

      /* Output compressed data */
      while ( compressedlen-- ) {
        int32 c ;
        c = *outputbuf++ ;
        if ( Putc( c , uflptr ) == EOF )
          return error_handler( IOERROR ) ;
      }

      if (zliberr == Z_STREAM_END)
        break;
    }

  }
  else
  {
    for (;;)
    {
      /* Outerloop is here to account for the possibility that
       * differencing might have to be done in chunks. Inputbuflen
       * is the amount of source data, pre-differencing.
       */

      /* Slurp up the entire input buffer */
      while (state->c_stream.avail_in > 0)
      {
        outputbuf = &abuf[0] ;
        state->c_stream.next_out = outputbuf ;
        state->c_stream.avail_out = COMPRESSION_BUFF_SIZE ;

        zliberr = deflate(&(state->c_stream), Z_NO_FLUSH) ;

        if (zliberr != Z_OK) {
          if (zliberr == Z_MEM_ERROR)
            return error_handler( VMERROR ) ;
          else
            return error_handler( IOERROR ) ;
        }

        /* We need to work out how many bytes were output */
        compressedlen = COMPRESSION_BUFF_SIZE - state->c_stream.avail_out ;
        if (compressedlen == 0)
          break;

        /* Output compressed data */
        while ( compressedlen-- ) {
          int32 c ;
          c = *outputbuf++ ;
          if ( Putc( c , uflptr ) == EOF )
            return error_handler( IOERROR ) ;
        }

        HQASSERT(state->c_stream.avail_in == 0, "More data remains with in input buffer");

      }

      if ( src_to_diff_count > 0 )
      {
        /* more data to difference - reset the pointers for
         * compression and output.
         */
        inputbuflen = src_to_diff_count ;
        dbuflen = state->dbuflen ;

        if (! diffEncode( &state->diff, src_to_diff_ptr , &inputbuflen,
                          state->dbuf, &dbuflen ))
          return FALSE ;

        HQASSERT( dbuflen != -1 , "differencing failed in FlateEncode" ) ;

        state->c_stream.next_in = (Bytef*)state->dbuf ;
        state->c_stream.avail_in = dbuflen ;

        src_to_diff_count -= inputbuflen ;
        src_to_diff_ptr += inputbuflen ;

        HQASSERT (src_to_diff_count >= 0,
                  "Negative count for data to difference in flateEncodeBuffer" ) ;

      }
      else
      {
        HQASSERT ( src_to_diff_count == 0 ,
                   "Negative count for data to difference in flateEncodeBuffer" ) ;

        break ;
      }
    }
  }

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
 * Flate decode
 * ----------------------------------------------------------------------------
 */

static Bool flateDecodeFilterInit(FILELIST *filter,
                                  OBJECT *args, STACK *stack)
{
  int zliberr;
  FLATE_STATE *state = NULL;
  int32 pop_args = 0 ;

  enum {
    match_ErrorOnChecksumFailure, match_n_entries
  } ;
  static NAMETYPEMATCH thematch[match_n_entries + 1] = {
    { NAME_ErrorOnChecksumFailure | OOPTIONAL , 1 , { OBOOLEAN }} ,
    DUMMY_END_MATCH
  };

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  theIFilterPrivate( filter ) = NULL ;

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
                                              FLATEDECODEBUFFSIZE + 1,
                                              MM_ALLOC_CLASS_FLATE_BUFFER ) ;
  if (theIBuffer( filter ) == NULL)
    return error_handler( VMERROR ) ;

  /* The filter machinery writes information into the byte before the
     buffer pointer. */
  theIBuffer( filter )++ ;

  /* Allocate my private state structure. */
  state = ( FLATE_STATE * )mm_alloc( mm_pool_temp ,
                                     sizeof( FLATE_STATE ) ,
                                     MM_ALLOC_CLASS_FLATE_STATE ) ;
  if ( state == NULL ) {
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1 ,
             FLATEDECODEBUFFSIZE + 1) ;
    theIBuffer(filter) = NULL;
    return error_handler( VMERROR ) ;
  }

  state->c_stream.zalloc = flateenc_alloc ;
  state->c_stream.zfree = flateenc_free ;
  state->c_stream.opaque = (voidpf)0 ;
  state->c_stream.next_out = Z_NULL ;
  state->c_stream.avail_out = 0 ;
  state->c_stream.next_in  = Z_NULL ;
  state->c_stream.avail_in = 0 ;

  state->status = 0 ; /* not used for inflate (decode) */
  state->effort = Z_DEFAULT_COMPRESSION ;
  state->is_eof = FALSE ; /* not used for deflate (encode) */
  state->error_on_checksum_failure = TRUE ;
  state->dbuflen = -1 ;
  state->dbuf = NULL ;
#if defined(DEBUG_FLATE_PERFORMANCE)
  state->first_time_called = TRUE ;
#endif

  if ( args ) {
    if ( ! dictmatch( args , thematch )) {
      mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE)) ;
      mm_free( mm_pool_temp ,
               theIBuffer( filter ) - 1,
               FLATEDECODEBUFFSIZE + 1) ;
      theIBuffer(filter) = NULL;
      return FALSE ;
    }

    /* 0: ErrorOnFlateChecksumFailure */

    if ( thematch[match_ErrorOnChecksumFailure].result ) {
      state->error_on_checksum_failure =
        oBool(*thematch[match_ErrorOnChecksumFailure].result) ;
    }
  }

  if (! diffInit(&state->diff , args)) {
    mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE)) ;
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEDECODEBUFFSIZE + 1) ;
    theIBuffer(filter) = NULL;
    return FALSE ;
  }

  if (state->diff.predictor > 1) {
    state->dbuflen = FLATEDECODEBUFFSIZE ; /* same size as decode filter
                                              buffer size */
    state->dbuf = ( uint8 * )mm_alloc( mm_pool_temp ,
                                       state->dbuflen ,
                                       MM_ALLOC_CLASS_FLATE_STATE ) ;
    if (state->dbuf == NULL) {
      diffClose(&state->diff) ;
      mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE)) ;
      mm_free( mm_pool_temp ,
               theIBuffer( filter ) - 1,
               FLATEDECODEBUFFSIZE + 1) ;
      theIBuffer(filter) = NULL;
      return error_handler( VMERROR ) ;
    }
  }

  /* We need MAX_WBITS because we want checksum checking etc.. */
  zliberr = inflateInit2(&(state->c_stream), MAX_WBITS) ;

  if (zliberr != Z_OK) {
    diffClose(&state->diff) ;
    if (state->dbuf != NULL)
      mm_free( mm_pool_temp , ( mm_addr_t )state->dbuf ,
               state->dbuflen ) ;
    mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(FLATE_STATE)) ;
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEDECODEBUFFSIZE + 1) ;
    theIBuffer(filter) = NULL;
    if (zliberr == Z_MEM_ERROR)
      return error_handler( VMERROR ) ;
    else
      return error_handler( IOERROR ) ;
  }

  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = FLATEDECODEBUFFSIZE ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;
  theIFilterPrivate( filter ) = state ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

/* Release any structures, bearing in mind that the
 * filter could be in a partially constructed state.
 */
static void flateDecodeFilterDispose( FILELIST *filter )
{
  int zliberr;
  FLATE_STATE *state ;

  HQASSERT( filter , "filter NULL in flateDecodeFilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             theIBuffer( filter ) - 1,
             FLATEDECODEBUFFSIZE + 1 ) ;
    theIBuffer( filter ) = NULL ;
  }

  /* Free the state structure too. */

  state = ( FLATE_STATE * )theIFilterPrivate( filter ) ;
  if ( state )
  {
      diffClose(&state->diff) ;

      zliberr = inflateEnd(&(state->c_stream));
      if (zliberr != Z_OK) {
        /* TBD: can I return a non zero reply */
      }

      if (state->dbuf != NULL)
        mm_free(mm_pool_temp, ( mm_addr_t )state->dbuf,
                state->dbuflen ) ;

      mm_free( mm_pool_temp ,
               ( mm_addr_t )state ,
               sizeof( FLATE_STATE )) ;
      theIFilterPrivate( filter ) = NULL ;
  }
}

static Bool flateDecodeBuffer( FILELIST *filter , int32 *ret_bytes )
{
  FILELIST *uflptr ;
  int zliberr  = Z_OK ;
  FLATE_STATE *state ;
  uint8 *out_buf, *inflate_buf ;
  int32 inflated_bytes_written = 0, bytes_read = 0 ;
  uint32 out_bufsize, inflate_buf_size ;
#if defined(DEBUG_FLATE_PERFORMANCE)
  static uint32 total_bytes_out, total_bytes_in ;
#endif
  static const unsigned char jaws_empty[] = {0x58, 0x85, 1, 0, 0, 0, 0, 0, 1, 0x0A} ;

  HQASSERT( filter , "Null filter in flateDecodeBuffer." ) ;
  HQASSERT( ret_bytes , "Null ret_bytes in flateDecodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  state = ( FLATE_STATE * )theIFilterPrivate( filter ) ;
  out_buf = theIBuffer( filter ) ;
  out_bufsize = theIBufferSize( filter ) ;

  HQASSERT( uflptr , "Null uflptr in flateDecodeBuffer." ) ;
  HQASSERT( state , "Null state in flateDecodeBuffer." ) ;
  HQASSERT( out_buf , "Null out_buf in flateDecodeBuffer." ) ;

#if defined(DEBUG_FLATE_PERFORMANCE)
  if (state->first_time_called) {
    total_bytes_out = 0 ;
    total_bytes_in = 0 ;
    state->start_cpu_cycle_count = GetMachineCycleCount() ;
    state->first_time_called = FALSE ;
  }
#endif

  state->c_stream.avail_out = 0 ;
  state->c_stream.avail_in = 0;

  /* If we need differencing, we go through an internal buffer, else
     directly to the output buffer. */
  if (state->dbuf != NULL) {
    inflate_buf = state->dbuf ;
    inflate_buf_size = state->dbuflen ;
  } else {
    inflate_buf = out_buf ;
    inflate_buf_size = out_bufsize ;
  }

  /* loop to make sure we get output from zlib */
  do {
    int checksum_mismatch = 0;


    /* Read more bytes in. This is a bit of a hack although does the
       identical thing to a Getc() loop into a buffer except in this
       case we use the underlying file buffer as a look ahead for zlib
       as we expect zlib to detect the end of the flate stream and
       hence in the PS world, we do not read too many characters. We
       then increment the buffer pointer by how much zlib has told us
       it has read. */
    if (! state->is_eof && state->c_stream.avail_in == 0) {

      if ( !EnsureNotEmptyFileBuff(uflptr) ) {
        state->is_eof = TRUE ;
        break;
      }

      state->c_stream.next_in  = theIPtr(uflptr) ;
      state->c_stream.avail_in = theICount(uflptr) ;

      if (state->c_stream.total_in == 0 &&
          state->c_stream.avail_in >= 10 &&
          ! HqMemCmp(state->c_stream.next_in, 10, jaws_empty, 10)) {
        /* JAWS PDF generator encodes empty stream as jaws_empty[].
         * The stream declares that the data block length is zero
         * but zlib routines regard a zero length data block to be an error.
         */
        *ret_bytes = 0 ;
        return TRUE ;
      }
    }

    if (state->c_stream.avail_out == 0) {
      state->c_stream.next_out = inflate_buf ;
      state->c_stream.avail_out = inflate_buf_size ;
    }

    zliberr = Z_OK ;
    while (state->c_stream.avail_out > 0 &&
           state->c_stream.avail_in > 0 &&
           zliberr == Z_OK) {
      zliberr = inflate_ggs(&(state->c_stream), Z_SYNC_FLUSH,
                            ! state->error_on_checksum_failure,
                            &checksum_mismatch) ;
    }

    if (checksum_mismatch == 1) {
      monitorf(( uint8 * )UVS("%%%%[ Info: Flate checksum mismatch ]%%%%\n")) ;
    }

    if (zliberr != Z_OK && zliberr != Z_STREAM_END) {
      /* The only data error we can recover from are checksum mismatch
         data errors. */
      if (zliberr == Z_DATA_ERROR) {
        if (checksum_mismatch == 0 || state->error_on_checksum_failure) {
          return error_handler( IOERROR ) ;
        }
      } else if (zliberr == Z_MEM_ERROR) {
        return error_handler( VMERROR ) ;
      } else {
        return error_handler( IOERROR ) ;
      }
    }

    if (checksum_mismatch == 1) {
      monitorf(( uint8 * )UVS("%%%%[ Warning: Since the stream could not be "
                              "verified, page correctness cannot be guaranteed "
                              "]%%%%\n")) ;
    }

    inflated_bytes_written = inflate_buf_size - state->c_stream.avail_out ;
    bytes_read = CAST_PTRDIFFT_TO_INT32(state->c_stream.next_in - theIPtr(uflptr)) ;

#if defined(DEBUG_FLATE_PERFORMANCE)
    total_bytes_in += bytes_read ;
#endif

    theICount(uflptr) -= bytes_read ;
    theIPtr(uflptr) += bytes_read ;

    /* get more data if we have written no data, there is no more
       input data, we have not seen EOF nor the end of the inflated
       stream from zlib. */
  } while (state->c_stream.avail_out > 0 && state->c_stream.avail_in == 0 &&
           ! state->is_eof && zliberr != Z_STREAM_END) ;

  if (inflated_bytes_written > 0) {
    int32 diff_bytes = inflated_bytes_written ;

    /* Maybe apply differencing to the final (possibly partial)
       buffer. */
    if (state->diff.predictor > 1 &&
        ! diffDecode(&state->diff, state->dbuf, inflated_bytes_written,
                     out_buf, &diff_bytes ))
      return error_handler( IOERROR ) ;

    HQASSERT(diff_bytes <= inflated_bytes_written,
             "Wrong byte count from diffDecode.") ;

    if (zliberr == Z_STREAM_END || (state->is_eof &&
                                    state->c_stream.avail_in == 0 &&
                                    state->c_stream.avail_out == inflate_buf_size)) {
      *ret_bytes = -diff_bytes ;
#if defined(DEBUG_FLATE_PERFORMANCE)
      {
        uint32 elapsed_cpu_cycles = (uint32)(GetMachineCycleCount() - state->start_cpu_cycle_count) ;
        total_bytes_out += diff_bytes ;
        HQTRACE(TRUE, ("TOTAL: %u, bytes in=%u, bytes out=%u", elapsed_cpu_cycles,
                                                               total_bytes_in,
                                                               total_bytes_out)) ;
      }
#endif

    } else {
      *ret_bytes = diff_bytes ;
#if defined(DEBUG_FLATE_PERFORMANCE)
      total_bytes_out += diff_bytes ;
#endif
    }
  } else {
    *ret_bytes = 0 ;

#if defined(DEBUG_FLATE_PERFORMANCE)
    {
      uint32 elapsed_cpu_cycles = (uint32)(GetMachineCycleCount() - state->start_cpu_cycle_count) ;
      HQTRACE(TRUE, ("TOTAL: %u, bytes in=%u, bytes out=%u", elapsed_cpu_cycles,
                                                             total_bytes_in,
                                                             total_bytes_out)) ;
    }
#endif
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------------
 * Flate filters
 * ----------------------------------------------------------------------------
 */
void flate_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("FlateEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       flateEncodeFilterInit,                /* initfile */
                       FilterCloseFile,                      /* closefile */
                       flateEncodeFilterDispose,             /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       flateEncodeBuffer ,                   /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void flate_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("FlateDecode") ,
                       FILTER_FLAG | READ_FLAG | EXPANDS_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       flateDecodeFilterInit,                /* initfile */
                       FilterCloseFile,                      /* closefile */
                       flateDecodeFilterDispose,             /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       flateDecodeBuffer ,                   /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/* ----------------------------------------------------------------------------
* Log stripped */
