/** \file
 * \ingroup pdf
 *
 * $HopeName: SWpdf!src:streamd.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements the Stream decode filter for PDF.
 *
 * Note that the streamFilterInit routine sets theIParamDict(filter) to
 * point to the parameters dictionary.
 *
 * Contrary to "normal" filters, this filter can obtain its input from
 * different sources.  In the first instance, the source is from the
 * underlying file as usual. But... :-
 * 1. When the filter is indeed implementing a standard direct PDF stream
 *    object, the code here tends to assume that it's the bottom-most
 *    filter (in the potential chain of filters) in that the underlying
 *    file is assumed to be the genuine PDF file.
 * 2. If, in the stream parameters dictionary, there's a 'filename' entry
 *    then this filter explicitly opens the named file and obtains its
 *    input directly from it.  This implements PDF streams where the
 *    stream dictionary has a /F entry naming an external file.
 * 3. If, in the stream parameters dictionary, there's a 'HqEmbeddedStream'
 *    entry, then this filter assumes its value is another file/filter
 *    and references it as for [2] above - i.e. as an external file. This
 *    implements the case where a /F entry resolves to an embedded file
 *    (/EF entry) which is actually a separate PDF stream object.
 *
 * Things to do (or could do):
 * (a) work out how to replay streams, for pdf patterns.
 * (b) use Length key exactly like Adobe(?) - read exactly Length bytes.
 * (c) use Length key as a hint for buffer size.
 * (d) cache the bytes from the underlying file for patterns if Length key
 *     appropriate.
 *
 * (Change log is at the end of the file)
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "mm.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "dicthash.h"
#include "dictscan.h"
#include "namedef_.h"
#include "fileio.h"
#include "fileops.h"

#include "streamd.h"

/* ---------------------------------------------------------------------- */
/* STREAM DECODE structure definitions etc */

#define LENGTHSTEPBACK     2
#define STREAMBUFFSIZE  1024
#define STREAMBUFFXTRA     1 + LENGTHSTEPBACK

/* states for the 'flags' field (bit numbers) */
#define SDS_STATE_NONE 0
#define SDS_STATE_INTERNAL 1      /* proper "this" stream */
#define SDS_STATE_EXTERNAL 2      /* external file */
#define SDS_STATE_EXT_STREAM 4    /* external stream */
#define SDS_STATE_INITIALIZED 16

struct internaldecodestate {
  int32 slen ;   /**< How far through the word "endstream" we've progressed. */
  int32 state ;  /**< Which "endstream", "\012endstream", ... we're in. */
  int32 strict ; /**< Length key must be correct. */
};

#define PS_FILENAME_BUFFER_LEN 1024

struct externaldecodestate {
  OBJECT * filespec; /**< Postsript filename of file to open. */
  OBJECT theofile;   /**< The currently open file. */
  FILELIST * nflptr; /**< filelist object of the currently open file (theofile). */
};

typedef struct streamdecodestate {
  int32 flags;      /**< Contain information about what sort of 'stream' we have open. */
  int32 length;     /**< This is a hint only; really read up to word "endstream". */
  int32 read;       /**< How many bytes we've read. */
  Hq32x2 filepos;         /**< File position required for next read. */
  Hq32x2 rewindfilepos;   /**< Data required to reset on a rewind. */
  FILELIST *uflptr;
  union {
    struct externaldecodestate external;
    struct internaldecodestate internal;
  } specific;
} STREAMDECODESTATE ;


/* ---------------------------------------------------------------------- */
#define theIInternalStream(sds_) ((sds_)->specific.internal)
#define theIExternalStream(sds_) ((sds_)->specific.external)

static Bool streamOpenExternalFile(OBJECT *filename, int32 openflags,
                                   int32 psflags, OBJECT *resultfile);

/* ---------------------------------------------------------------------- */
static Bool streamFilterInit(FILELIST *filter,
                             OBJECT *args ,
                             STACK *stack )
{
  STREAMDECODESTATE *sds ;
  int32 pop_args = 0 ;

  /* *** Add a new match to this NAMETYPEMATCH and you
         may need to add it to streamEncodeIgnoreKeys as well *** */
  enum {
    sd_Length, sd_Position, sd_Strict, sd_filename, sd_HqEmbeddedStream,
    sd_n_entries
  } ;
  static NAMETYPEMATCH stream_dict[sd_n_entries + 1] = {
    { NAME_Length                       , 1 , { OINTEGER }} ,
    { NAME_Position                     , 2 , { OINTEGER, OFILEOFFSET }} ,
    { NAME_Strict | OOPTIONAL           , 1 , { OBOOLEAN }} ,
    { NAME_filename | OOPTIONAL         , 1 , { OSTRING }} ,
    { NAME_HqEmbeddedStream | OOPTIONAL , 1 , { OFILE }} ,
    DUMMY_END_MATCH
  } ;

  HQASSERT( filter , "filter NULL in streamFilterInit" ) ;
  HQASSERT(isIInputFile(filter), "StreamDecode is a not an input filter.") ;
  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && (oType(*args) == ODICTIONARY )) {
    OBJECT * obj;

    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;

    if ( ! dictmatch( args , stream_dict ))
      return FALSE ;

    /* Check for Length & Position errors. */
    if (oInteger(*stream_dict[sd_Length].result) < 0 )
      return error_handler( RANGECHECK ) ;

    obj = stream_dict[sd_Position].result;
    if (oType(*obj) == OFILEOFFSET) {
      Hq32x2 val;
      FileOffsetToHq32x2(val, *obj);
      if (Hq32x2CompareInt32(&val,0) < 0)
        return error_handler( RANGECHECK ) ;
    }else {
      if (oInteger(*obj) < 0)
        return error_handler( RANGECHECK ) ;
    }

    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;

    OCopy( theIParamDict( filter ), *args ) ;

  } else {
    args = NULL ;
  }

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    /* Underlying object of null means a deferred stream */
    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  /* Need to allocate an extra 2 bytes: The first will go in front of
   * the filter buffer and the second is used in streamDecodeBuffer
   * where the extra byte may be required to place an extra stream
   * byte in the buffer. */
  theIBuffer( filter ) = ( uint8 * )mm_alloc( mm_pool_temp ,
                                              STREAMBUFFSIZE + STREAMBUFFXTRA ,
                                              MM_ALLOC_CLASS_STREAM_BUFFER ) ;
  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++ ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = STREAMBUFFSIZE ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;
  theIFilterPrivate( filter ) = NULL ;

  sds = ( STREAMDECODESTATE * )mm_alloc( mm_pool_temp ,
                                         sizeof( STREAMDECODESTATE ) ,
                                         MM_ALLOC_CLASS_STREAM_STATE ) ;
  if ( sds == NULL )
    return error_handler( VMERROR ) ;

  theIFilterPrivate( filter ) = sds ;

  sds->read = 0 ;
  sds->uflptr = NULL;

  if ( args ) {
    if (stream_dict[sd_HqEmbeddedStream].result) {    /* HqEmbeddedStream */
      theIExternalStream(sds).theofile = *stream_dict[sd_HqEmbeddedStream].result ;
      Hq32x2FromInt32(&sds->filepos, 0);
      sds->rewindfilepos = sds->filepos;
      sds->flags = SDS_STATE_EXTERNAL | SDS_STATE_EXT_STREAM ;
      sds->length = 0 ;
    } else if ( stream_dict[sd_filename].result ) {    /* filename */
      theTags( theIExternalStream(sds).theofile ) = ONULL ;
      theIExternalStream(sds).filespec = stream_dict[sd_filename].result ;
      Hq32x2FromInt32(&sds->filepos, 0);
      sds->rewindfilepos = sds->filepos;
      sds->flags = SDS_STATE_EXTERNAL ;
      sds->length = 0 ;
    }
    else {
      int32 strict ;
      OBJECT * obj;

      sds->flags = SDS_STATE_INTERNAL | SDS_STATE_INITIALIZED;

      /* Obtain the length "hint" from the filter's associated dictionary. */
      sds->length = oInteger(*stream_dict[sd_Length].result) ;

      /* Obtain the position from the filter's associated dictionary. */
      obj = stream_dict[sd_Position].result;
      if (oType(*obj) == OINTEGER)
        Hq32x2FromInt32(&sds->filepos,oInteger(*obj));
      else
        FileOffsetToHq32x2(sds->filepos, *obj);

      sds->rewindfilepos = sds->filepos;

      strict = (stream_dict[sd_Strict].result == NULL ?
                FALSE :
                oBool(*stream_dict[sd_Strict].result));

      theIInternalStream(sds).slen    = 0 ;
      theIInternalStream(sds).state   = 0 ;
      theIInternalStream(sds).strict  = strict ;

      sds->uflptr = theIUnderFile( filter );
    }
  } else {
    sds->flags = SDS_STATE_INTERNAL | SDS_STATE_INITIALIZED;
    Hq32x2FromInt32(&sds->filepos, 0);
    sds->rewindfilepos = sds->filepos;
    sds->length = 0 ;
  }

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static int32 streamFillBuff( register FILELIST *filter )
{
  int32 c ;
  int32 state ;
  FILELIST *uflptr ;
  STREAMDECODESTATE *sds ;

  HQASSERT( filter , "filter NULL in streamFillBuff" ) ;

  switch ( theIFilterState( filter )) {

  case FILTER_INIT_STATE :
  case FILTER_EMPTY_STATE :

    sds = theIFilterPrivate( filter ) ;
    HQASSERT( sds , "sds NULL in streamFillBuff" ) ;

    /* If reading through an external file (or embedded stream), the state
       will not be initialised by streamFilterInit.  */
    if ( !( sds->flags & SDS_STATE_INITIALIZED ) ) {
      HQASSERT(( sds->flags & SDS_STATE_EXTERNAL ) ,
               "streamFillBuff trying to initialize a non external stream" );

      /* Open the external file (but not an embedded stream). */
      if ((sds->flags & SDS_STATE_EXT_STREAM) == 0) {
        if ( !streamOpenExternalFile( theIExternalStream( sds ).filespec ,
                                      (SW_RDONLY | SW_FROMPS) , READ_FLAG ,
                                      &theIExternalStream( sds ).theofile) ) {
          return ioerror_handler( filter ) ;
        }
      }

      theIExternalStream( sds ).nflptr =
        oFile( theIExternalStream(sds).theofile );
      sds->flags |= SDS_STATE_INITIALIZED;
    }

    /* Check underlying file is in correct position before reading next buffer,
     * but only for internal streams.
     */
    if (sds->flags & SDS_STATE_INTERNAL) {
      Hq32x2 file_pos;
      Hq32x2 file_pos_end;

      uflptr = sds->uflptr;

      if ( (*theIMyFilePos(uflptr))(uflptr, &file_pos) == EOF )
        return EOF ;

      Hq32x2AddInt32(&file_pos_end, &sds->filepos,
                     theIInternalStream(sds).slen);

      if ( Hq32x2Compare(&file_pos, &file_pos_end) != 0 ) {
        file_pos = file_pos_end;
        if ( (*theIMyResetFile(uflptr))(uflptr) == EOF ||
             (*theIMySetFilePos(uflptr))(uflptr, &file_pos) == EOF )
          return EOF ;
      }
    }

    /* Read the next buffer full. */
    state = theIFilterState( filter ) ;
    if (( c = FilterFillBuff( filter )) == EOF )
      return EOF ;

    HQASSERT( c >= 0 , "Not expecting negative values" ) ;

    /* The private state could have disappeared if FilterFillBuff
       closes the filter. It's all hideously complicated. */
    sds = theIFilterPrivate( filter ) ;

    /* Update position of where the next read should come from.  Don't
       do this when FilterFillBuff jumps straight to FILTER_EOF_STATE,
       bypassing FILTER_LASTCHAR_STATE, as private state will have
       been freed.  FILTER_LASTCHAR_STATE can be skipped if the
       'endstream' sensing fails. */
    if ( theIFilterState( filter ) != FILTER_EOF_STATE ) {
      Hq32x2AddInt32(&sds->filepos, &sds->filepos, theIReadSize( filter ));
      if ( state == FILTER_INIT_STATE )
        Hq32x2AddInt32(&sds->filepos, &sds->filepos, 1);
    }

    return c ;

  case FILTER_LASTCHAR_STATE:
    /*@fallthrough@*/

  case FILTER_EOF_STATE:
  case FILTER_ERR_STATE:
    return FilterFillBuff( filter ) ;

  default:
    HQFAIL("Streamfillbuf: illegal filter state");
  } /* also return EOF to default arm of switch */
  return EOF ;
}

/* ---------------------------------------------------------------------- */
static int32 streamFilterClose( FILELIST *filter, int32 flag )
{
  int32 result = 0 ;
  FILELIST *uflptr ;
  STREAMDECODESTATE *sds ;

  UNUSED_PARAM(int32, flag);

  HQASSERT( filter , "filter NULL in streamFilterClose." ) ;

  SetIClosingFlag( filter ) ;

  sds = theIFilterPrivate(filter) ;

  if ( sds ) {
    if ( sds->flags & SDS_STATE_INITIALIZED ) {
      if ( sds->flags & SDS_STATE_EXTERNAL ) {
        if ( ! file_close( &theIExternalStream( sds ).theofile ))
          result = EOF ;
      }
    }
  }

  uflptr = theIUnderFile( filter ) ;

  if ( uflptr && isICST( filter ) &&
       isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr )) {
    /* While this filter may be being closed implicitly, the closing of the
     * source is explicit */
    if (( *theIMyCloseFile( uflptr ))( uflptr, CLOSE_EXPLICIT ) == EOF )
      result = EOF ;
  }

  (*theIMyDisposeFile(filter))(filter) ;

  ClearIClosingFlag( filter ) ;
  SetIEofFlag( filter ) ;
  if ( ! isIRewindable( filter ))
    ClearIOpenFlag( filter ) ;

  if ( result == EOF )
    ( void )error_handler( IOERROR ) ;

  return result ;
}

/* ---------------------------------------------------------------------- */
static void streamFilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in streamFilterDispose." ) ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 1 ) ,
             theIBufferSize( filter ) + STREAMBUFFXTRA ) ;
    theIBuffer( filter ) = NULL ;
  }

  if ( theIFilterPrivate( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )theIFilterPrivate( filter ) ,
             sizeof( STREAMDECODESTATE )) ;
    theIFilterPrivate( filter ) = NULL ;
  }
}

/* ---------------------------------------------------------------------- */
static Bool externalStreamDecodeBuffer(FILELIST *filter, int32 *ret_bytes,
                                       STREAMDECODESTATE *sds )
{
  register int32    c ;
  register uint8    *ptr ;
  register uint8    *str ;
  register uint8    *end ;
  register FILELIST *uflptr ;
  register int32    bytes_left ;

  uflptr = theIExternalStream(sds).nflptr;
  HQASSERT( uflptr , "uflptr NULL in streamDecodeBuffer" ) ;

  ptr = str = theIBuffer( filter ) ;
  HQASSERT( ptr , "ptr NULL in streamDecodeBuffer" ) ;

  bytes_left = theIBufferSize( filter ) ;
  HQASSERT((bytes_left > 0), "externalstreamDecodeBuffer: buffer size < 0");

  c = EOF ; /* remove compiler warning */
  end = str + bytes_left ;

  while ((ptr < end) && ( c = Getc( uflptr )) != EOF ) {
    (*ptr++) = ( uint8 )c ;
  }
  /* If c == EOF then negate the number of byte read to */
  /* indicate that we've hit EOF */
  (*ret_bytes) = (c == EOF) ? CAST_PTRDIFFT_TO_INT32(str - ptr)
                            : CAST_PTRDIFFT_TO_INT32(ptr - str) ;

  sds->read += CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Return Length bytes of data, then continue returning data up to but not
 * including the first match to one of the endstream key strings.
 * For properly specified streams, the Length should be exact and we will
 * find the endstream with minimum fuss.
 */
static Bool internalStreamDecodeBuffer(FILELIST *filter, int32 *ret_bytes,
                                       STREAMDECODESTATE *sds )
{
  register int32    c ;
  register uint8    *ptr ;
  register uint8    *str ;
  register uint8    *end ;
  register int32    slen ;
  register int32    state ;
  register FILELIST *uflptr ;
  register int32    bytes_left ;

  enum eKeys {eNone, eCR, eLF, eCRLF, NUM_KEYS};
#define LONGEST_KEY     eCRLF
  uint8 *keys[NUM_KEYS] = {
    ( uint8 * )"endstream" ,         /* fallback state */
    ( uint8 * )"\015endstream" ,     /* CR endstream */
    ( uint8 * )"\012endstream" ,     /* LF endstream */
    ( uint8 * )"\015\012endstream" , /* CR LF endstream */
  } ;

  uflptr = sds->uflptr;
  HQASSERT( uflptr , "uflptr NULL in streamDecodeBuffer" ) ;

  ptr = str = theIBuffer( filter ) ;
  HQASSERT( ptr , "ptr NULL in streamDecodeBuffer" ) ;

  bytes_left = sds->length - sds->read ;
  end = str + bytes_left ;
  if ( sds->read >= sds->length ||
       bytes_left > theIBufferSize( filter ))
    end = str + theIBufferSize( filter ) ;

  slen = theIInternalStream(sds).slen ;
  state = theIInternalStream(sds).state ;
  HQASSERT( slen >= 0 && slen < strlen_int32( (char *) keys[ LONGEST_KEY ] ) ,
            "slen is bad value" ) ;
  HQASSERT( state >= 0 && state < NUM_KEYS,
            "state is bad value" ) ;

  if ( bytes_left > 0 ) {
    /* Have a /Length key, so read up to that many bytes before we start checking. */
    HQASSERT( slen == 0 , "slen should be zero" ) ;
    HQASSERT( state == 0 , "state should be zero" ) ;
    do {
      if (( c = Getc( uflptr )) == EOF )
        return error_handler(IOERROR) ;

      (*ptr++) = ( uint8 )c ;
    } while ( ptr < end ) ;

    /* If we've NOT read /Length bytes then return. Probably the buffer is full */
    if ( sds->read + CAST_PTRDIFFT_TO_INT32( ptr - str ) != sds->length ) {
      (*ret_bytes) = CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
      sds->read += CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
      return TRUE ;
    }
  }
  else {
    /* Push into the stream any characters that might have overflowed
     * the end of the buffer space from a read below.
     */
    if ( slen > 0 ) {
      HqMemCpy( ptr , keys[ state ] , slen ) ;
      ptr += slen ;
      HQASSERT(ptr < end, "filter buffer overflowed");

      slen = 0 ;
      state = 0 ;
    }
  }

  HQASSERT(slen == 0, "slen != 0");

  /* We've read Length bytes. Now continue reading one byte at a time, returning
   * when we, a) see the first match to any of the endstream key strings, or b) we
   * run out of data (error condition).
   * One interesting case is if Length is too small and we have an otherwise normal
   * stream terminated by CRLFendstream. For this, we will terminate the stream at
   * the byte before the CR with no knowledge of whether the CR is actually part of
   * the data. If Length were properly specified that problem wouldn't happen.
   */
  do {
    int32 state;
    uint8 *kptr = NULL;
    uint8 *kstr = NULL;

    for (state = 0; state < NUM_KEYS; state++) {
      uint8 nc ;

      kstr = keys[ state ] ;
      kptr = kstr + slen ;      /* slen could be non-zero from eCR to eCRLF promotion */

      /* Match this string for as long as we can... */
      do {
        nc = (*kptr++) ;
        /* If we match a key string then we have normal stream termination */
        if ( nc == '\0' ) {
          /* Matches, so return EOF (aka -ret_bytes). */
          (*ret_bytes) = - CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
          sds->read += CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
          HQTRACE( theIInternalStream(sds).strict &&
                   CAST_PTRDIFFT_TO_INT32( ptr - str ) != bytes_left ,
                   ("StreamDecode: /Length is out by %d bytes",
                   CAST_PTRDIFFT_TO_INT32(ptr - str ))) ;
          return TRUE ;
        }
        if (( c = Getc( uflptr )) == EOF )
          return error_handler(IOERROR) ;
      } while ( nc == c ) ;
      UnGetc( c , uflptr ) ;

      /* We must do something to cope with the fact that the key strings cannot
       * be discriminated by the first byte alone, yet we can only unget one
       * char. This hack is one way of handling it.
       * If the first char is CR but next isn't 'e' then the only other possible
       * match is for eCRLF.
       * If we've failed to match the 2nd char then there isn't a match so bail out.
       */
      slen = CAST_PTRDIFFT_TO_INT32(kptr - kstr) - 1 ;

      if (slen > 0) {
        if (slen == 1 && state == eCR)
          state = eCRLF - 1;
        else {
          /* Go back to the client with the data we have so far.
           * Do this to simplify the code, remember that we're here because Length
           * wasn't correctly specified so don't go overboard to optimise it.
           * Either pass on the slen bytes to the client here or, if necessary
           * return to the 'if (slen > 0)' case above.
           */
          if ( ptr + slen <= end ) {
            /* We must never return 0 bytes to the client because that's
             * interpreted as EOF. This bit avoids that possibility.
             */
            HqMemCpy( ptr , kstr , slen ) ;
            ptr += slen ;
            slen = 0;
          }
          (*ret_bytes) = CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
          sds->read += CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
          theIInternalStream(sds).state  = state ;
          theIInternalStream(sds).slen  = slen ;
          return TRUE ;
        }
      }
    }

    /* Get the next character and go back to recheck for endstream */
    c = Getc( uflptr ) ;
    HQASSERT( c != EOF , "This is a character we UnGetc'd" ) ;
    (*ptr++) = ( uint8 )c ;
  } while ( ptr < end ) ;

  HQASSERT(slen == 0, "slen != 0");
  theIInternalStream(sds).state  = 0 ;    /* Not relevant for slen == 0 */
  theIInternalStream(sds).slen  = slen ;

  (*ret_bytes) = CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
  sds->read += CAST_PTRDIFFT_TO_INT32( ptr - str ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool streamDecodeBuffer(FILELIST *filter, int32 *ret_bytes)
{
  STREAMDECODESTATE *sds ;

  HQASSERT( filter , "filter NULL in streamDecodeBuffer" ) ;
  HQASSERT( ret_bytes , "ret_bytes NULL in streamDecodeBuffer" ) ;

  sds = theIFilterPrivate(filter) ;
  HQASSERT( sds , "sds NULL in streamDecodeBuffer" ) ;
  if (sds->flags & SDS_STATE_EXTERNAL)
    return externalStreamDecodeBuffer(filter, ret_bytes, sds);
  else
    return internalStreamDecodeBuffer(filter, ret_bytes, sds);
}

/* ---------------------------------------------------------------------- */
static Bool streamOpenExternalFile(OBJECT *filename, int32 openflags,
                                   int32 psflags, OBJECT *resultfile)
{
  FILELIST * flptr;
  if (! file_open(filename, openflags, psflags, FALSE, 0, resultfile))
    return FALSE;
  flptr = oFile(*resultfile) ;
  if ( ! isIOpenFileFilter( resultfile , flptr ) ||
       ! isIInputFile( flptr ) ||
       isIEof( flptr ))
    return error_handler( IOERROR ) ;
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 streamPos(FILELIST *filter, Hq32x2 *pos)
{
  STREAMDECODESTATE *sds ;

  HQASSERT(filter , "No filter" ) ;
  HQASSERT(pos , "Nowhere to put position") ;

  /* It is possible that the filter may already have hit EOF, and had its
     private data disposed of. */
  if ( (sds = theIFilterPrivate(filter)) == NULL )
    return EOF ;

  Hq32x2Subtract(pos, &sds->filepos, &sds->rewindfilepos) ;
  Hq32x2SubtractInt32(pos, pos, theICount(filter)) ;

  return 0 ;
}

/* ---------------------------------------------------------------------- */
static int32 streamSetPos(FILELIST *filter, const Hq32x2 *pos)
{
  STREAMDECODESTATE *sds ;
  Hq32x2 destination ;
  Bool reset = TRUE ;
  static Hq32x2 zero = { 0, 0 } ;
  Hq32x2 first_endpos ; /* position of end of first buffer */
  Bool  sub = FALSE;

  HQASSERT(filter , "No filter") ;
  HQASSERT(pos , "No position to go to") ;

  /* Special case to force a reset for zero, it's used quite a bit in PDF. */
  if ( Hq32x2Compare(&zero, pos) == 0 )
    return FilterSetPos(filter, pos) ;

  if ( (sds = theIFilterPrivate(filter)) != NULL ) {
    Hq32x2Subtract(&destination, &sds->filepos, &sds->rewindfilepos) ;
    Hq32x2SubtractInt32(&destination, &destination, theIReadSize(filter)) ;

    /* If the current position is before the position we want to go to, we
       don't need to reset and start again. */
    if ( Hq32x2Compare(&destination, pos) <= 0 )
      reset = FALSE ;
  } /* else no StreamDecode state, may have been in EOF or EMPTY state. */

  if ( reset ) {
    if ( FilterSetPos(filter, &zero) == EOF )
      return EOF ;

    sds = theIFilterPrivate(filter) ;
    HQASSERT(sds, "No StreamDecode state") ;
  }

  /* Convert the relative position within the stream to an absolute position. */
  Hq32x2Add(&destination, pos, &sds->rewindfilepos) ;

  /* calculate end position of first buffer so we know if we are still in
     that region. Subsequent buffers need to account for the last character */
  Hq32x2AddInt32(&first_endpos, &sds->rewindfilepos, theIReadSize(filter)) ;

  for (;;) {
    uint8 *buffer ;
    int32 bytes, difference ;
    /*buffer positions*/
    Hq32x2 curr_start ;
    Hq32x2 offset ;

    Hq32x2SubtractInt32(&curr_start, &sds->filepos, theIReadSize(filter)) ;

    /* if we are not in the first buffer then flag that we need to
       subtract 1 from the position. */
    Hq32x2Subtract(&offset, &curr_start, &first_endpos) ;
    if ( Hq32x2ToInt32(&offset, &difference) ) {
      if (difference >= 0)
        sub = TRUE;
    }

    Hq32x2Subtract(&offset, &destination, &curr_start) ;

    /* If the position desired is inside the last buffer we read, we can
       shuffle the pointers and return. */
    if ( Hq32x2ToInt32(&offset, &difference) ) {
      HQASSERT(difference >= 0, "Overran seek position in StreamDecode") ;
      if ( sub) {
        /* not the first buffer so remove 1 from the difference
           to account for last character being carried over. */
        difference--;
        HQASSERT(difference >= -1,"problem with stream position seeking.");
      }

      if ( difference <= theIReadSize(filter) ) {
        theICount(filter) = theIReadSize(filter) - difference ;
        theIPtr(filter) = theIBuffer(filter) + difference ;
        break ;
      }
    }

    /* Get the next buffer of data. */
    filter->count = 0;
     if ( !GetFileBuff(filter, theIBufferSize(filter), &buffer, &bytes) )
      return EOF ;
  }

  return 0 ;
}

void stream_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* stream decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("StreamDecode") ,
                       FILTER_FLAG | READ_FLAG | DELIMITS_FLAG ,
                       0, NULL, 0,
                       streamFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       streamFilterInit,                     /* initfile */
                       streamFilterClose,                    /* closefile */
                       streamFilterDispose,                  /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       streamPos,                            /* filepos */
                       streamSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       streamDecodeBuffer ,                  /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL) ;
}


/* ---------------------------------------------------------------------- */
/* Log stripped */
