/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:rsd.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Reusable Stream Decode (RSD) filter implementation.
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "namedef_.h"
#include "mm.h"

#include "fileio.h"
#include "rsdstore.h"
#include "rsdblist.h" /* RSD_ACCESS_RAND */
#include "rsd.h"

/* -------------------------------------------------------------------------- */
#define RSD_LEN_NOTSET -1
/* -------------------------------------------------------------------------- */
typedef struct rsd_state {
  RSD_STORE *store ; /* Where the data is cached. */

  int8 asyncread ;   /* Advance external file to EOD when false. */
  int8 circular ;    /* HQN EXTENSION ONLY: Wrap back to the start when at EOF. */
  int8 checkpos ;    /* File position needs updating. */
  int8 intent ;      /* Usage hint (eg image/threshold). */

  int32 position ;   /* Requested filter position. */
  int32 length ;     /* Number of bytes in data. */
  int32 read ;       /* Bytes read (including bytes left in buffer). */
} RSD_STATE ;

/* -------------------------------------------------------------------------- */
static Bool rsd_createfilterlist( FILELIST *flptr ,
                                  OBJECT *name , OBJECT *parms ,
                                  Bool rewindable ) ;
static Bool rsd_seekable( FILELIST *flptr ) ;
static int32 rsdSetFilePosition(FILELIST *filter, const Hq32x2 *position) ;

/* -------------------------------------------------------------------------- */
#define RSD_ASSERT_FILTER( _func, _filter ) MACRO_START \
  HQASSERT( _filter && \
            isIRSDFilter( _filter ) && \
            isIOpenFile( _filter ) && \
            theIFilterPrivate( _filter ) , \
            _func ": RSD filter is in inconsistent state" ) ; \
MACRO_END

/* -------------------------------------------------------------------------- */
static Bool rsdFilterInit( FILELIST *filter , OBJECT *args ,
                           STACK *stack )
{
  RSD_STATE *state ;
  OBJECT *filter_list = NULL ;
  OBJECT *parms_list = NULL ;
  int32 pop_args = 0 ;

  enum {
    rsd_Filter, rsd_DecodeParms, rsd_Intent, rsd_AsyncRead, rsd_dummy
  } ;
  static NAMETYPEMATCH thematch[rsd_dummy + 1] = {
    { NAME_Filter      | OOPTIONAL, 2, { ONAME, OARRAY }},
    { NAME_DecodeParms | OOPTIONAL, 3, { ONAME, OARRAY, ODICTIONARY }},
    { NAME_Intent      | OOPTIONAL, 1, { OINTEGER }},
    { NAME_AsyncRead   | OOPTIONAL, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  } ;

  HQASSERT( filter , "filter is null in rsdFilterInit" ) ;

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args != NULL && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( args , thematch ))
      return FALSE ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
    filter_list = thematch[rsd_Filter].result ;
    parms_list = thematch[rsd_DecodeParms].result ;
  } else
    args = NULL ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    if (theIUnderFile(filter) != NULL) {
      FILELIST* uflptr = theIUnderFile(filter);
      if (isISkipLF(uflptr)) {
        /* The PS scanner stops on CR which could be followed by a LF. This
         * needs to be consumed if present so the block code records the correct
         * stream start position. Can't rely on FilterFillBuff to consume any LF
         * since that isn't called until after the start position has been
         * recorded. */
        int32 ch;
        if ((ch = Getc(uflptr)) != EOF) {
          if (ch != LF) {
            UnGetc(ch, uflptr);
          }
        }
        ClearICRFlags(uflptr);
      }
    }

    ++pop_args ;
  }

  theIBuffer( filter ) = NULL ;
  theIPtr( filter ) = NULL ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = 0 ;
  theIReadSize( filter ) = 0 ;
  /* RSD Filter recognises only EMPTY, LASTCHAR, EOF and ERR filter states. */
  theIFilterState( filter ) = FILTER_EMPTY_STATE ;
  SetIRewindableFlag( filter ) ;
  SetIRSDFlag( filter ) ;

  if ( theIFilterPrivate( filter ) == NULL ) {
    int32 accesshint ;
    Bool seekable ;

    /* Create and initialise RSD state (first time around). */
    state = ( RSD_STATE * ) mm_alloc( mm_pool_temp , sizeof( RSD_STATE ) ,
                                      MM_ALLOC_CLASS_RSD_STATE ) ;
    if ( state == NULL )
      return error_handler( VMERROR ) ;

    state->store = NULL ;
    state->intent = ( int8 ) 0 ; /* Default is 0, image data. */
    if ( (args != NULL) && (thematch[rsd_Intent].result != NULL) ) {
      int32 intent = oInteger(*thematch[rsd_Intent].result) ;
      if ( intent < 0 || intent > 3 ) {
        /* Intent must be [ 0 , 3 ]. */
        mm_free( mm_pool_temp , ( mm_addr_t ) state , sizeof( RSD_STATE )) ;
        return error_handler( RANGECHECK ) ;
      }
      state->intent = ( int8 ) intent ;
    }
    accesshint = ( state->intent == 3 ) ? RSD_ACCESS_RAND : RSD_ACCESS_SEQN ;
    state->asyncread = FALSE ;
    if ( args != NULL && thematch[rsd_AsyncRead].result != NULL )
      state->asyncread = ( int8 ) oBool(*thematch[rsd_AsyncRead].result) ;
    state->circular = FALSE ;
    state->checkpos = FALSE ;
    state->position = 0 ;
    state->length = RSD_LEN_NOTSET ;
    state->read = 0 ;

    seekable = rsd_seekable( theIUnderFile( filter ) ) ;

    if ( ! rsd_createfilterlist( filter , filter_list , parms_list , seekable )) {
      mm_free( mm_pool_temp , ( mm_addr_t ) state , sizeof( RSD_STATE )) ;
      return FALSE ;
    }

    state->store =
      rsd_storeopen( theIUnderFile( filter ) , seekable , accesshint ) ;

    if ( state->store == NULL ) {
      mm_free( mm_pool_temp , ( mm_addr_t ) state , sizeof( RSD_STATE )) ;
      HQASSERT( error_signalled(), "rsd_storeopen failed, but didn't set up error" );
      return FALSE ;
    }

    /* If there was no data then set filter to EOF state */
    if ( rsd_storelength(state->store) == 0 ) {
      SetIEofFlag(filter);
      theIFilterState(filter) = FILTER_EOF_STATE;
    }

    /* Note that we don't close rewindable sources because PDF may want to
       look for a StreamDecode dictionary in the filter chain. */
    if ( ! seekable && !isIRewindable(theIUnderFile(filter)) ) {
      FILELIST *underlying = theIUnderFile( filter );
      HQASSERT( underlying != NULL , "missing underlying file or filter" ) ;
      /* Close the underlying file if it's open; an underlying filter would
         already have been closed automatically, but an underlying file wouldn't
         have been. */
      if ( isIOpenFile( underlying ))
        (void)(*theIMyCloseFile(underlying))(underlying, CLOSE_IMPLICIT);

      /* Remove the original source (now closed) from the RSD filter chain */
      theIUnderFile( filter ) = NULL ;
    }

    theIFilterPrivate( filter ) = state ;
  }

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* rsdFillBuff overrides FilterFillBuff entirely since its closing on
   last char semantics are not applicable. */
static int32 rsdFillBuff( FILELIST *filter )
{
  RSD_STATE *state ;
  int32 bytes = 0 ;
  int32 bytes_left = 0 ;

  RSD_ASSERT_FILTER( "rsdFillBuff", filter ) ;

  state = theIFilterPrivate( filter ) ;

  switch ( theIFilterState( filter )) {

  case FILTER_EOF_STATE :

    if ( state->circular ) {
      /* HQN EXTENSION AND NOT PART OF THE RSD STANDARD SPECIFICATION. */
      /* Wrap back to the start. */
      ClearIEofFlag( filter ) ;
      state->position = 0 ;
      state->checkpos = TRUE ;
      /* Fallthru into FILTER_EMPTY_STATE. */
    }
    else if ( ! state->checkpos ) {
      /* We get here if we are reading past the EOF. All we have to do
         is set EOF flag, and stay in this state. */
      SetIEofFlag( filter ) ;
      return EOF ;
    }
    /* Fallthru */

  case FILTER_EMPTY_STATE :

    /* May not be in the right position, owing to a resetfile,
       a setfileposition or a flushfile.
     */

    if ( state->checkpos ) {
      int32 offset ;

      state->checkpos = FALSE ;
      /* filter count should be -1 if this routine is called via Getc
         or 0 if called directly by GetNextBuf. */
      HQASSERT( theICount( filter ) == -1 || theICount( filter ) == 0 ,
                "Expected filter count to be either -1 or 0" ) ;
      theICount( filter ) = 0 ;

      if ( state->position == state->length ) {
        HQASSERT( isIEof( filter ) , "EOF not set" ) ;
        theIFilterState(filter) = FILTER_EOF_STATE ;
        return EOF ;
      }

      HQASSERT( ! isIEof( filter ) , "Expected setfilepos etc to clear EOF flag" ) ;

      if ( state->position < state->read - theIReadSize( filter ) ||
           state->position >= state->read ) {
        /* Need to read from the store as required data not in current buffer. */
        int32 actual_pos ;

        if (!rsd_storeseek( state->store , state->position , & actual_pos ))
          return ioerror_handler( filter ) ;

        HQASSERT( actual_pos <= state->position ,
                  "actual_pos should not occurr after position" ) ;
        state->read = actual_pos ;

        theIFilterState( filter ) = FILTER_EMPTY_STATE ;

        if ( GetNextBuf( filter ) == EOF ) { /* Safe now checkpos == FALSE. */
          theIFilterState(filter) = FILTER_EOF_STATE ;
          return EOF ;
        }
      }
      /* Can just seek within current buffer. */
      offset = state->position - state->read + theIReadSize( filter ) + 1 ;
      theIPtr( filter ) = theIBuffer( filter ) + offset ;
      theICount( filter ) = theIReadSize( filter ) - offset ;
      bytes_left = theICount( filter ) + 1 ;
      bytes = ( state->read == state->length ? -bytes_left : bytes_left ) ;
    }
    else {

      if ( ! ( *theIFilterDecode( filter ))( filter , & bytes ))
        return ioerror_handler( filter ) ;

      bytes_left = ( bytes < 0 ) ? -bytes : bytes ;

      /* Set ptr and bytes count in buffer, adjusted for byte returned. */
      theIPtr( filter ) = theIBuffer( filter ) + 1 ;
      theICount( filter ) = bytes_left - 1 ;
      theIReadSize( filter ) = bytes_left ;
      state->read += theIReadSize( filter ) ;
    }

    if ( bytes < 0 ) {
      theIFilterState( filter ) = FILTER_LASTCHAR_STATE ;
      theILastChar( filter ) = * ( theIPtr( filter ) + bytes_left - 2 ) ;
      theICount( filter ) -= 1 ;
    }
    else
      theIFilterState( filter ) = FILTER_EMPTY_STATE ;

    /* Only drop thru into FILTER_LASTCHAR_STATE when
       have the last char of the last buffer */
    if ( bytes_left > 1 || bytes > 0 )
      return theIPtr(filter)[-1] ;

    /* Fallthru */

  case FILTER_LASTCHAR_STATE :

    if ( state->checkpos ) {
      theIFilterState( filter ) = FILTER_EMPTY_STATE ;
      return GetNextBuf( filter ) ;
    }
    else {
      theIFilterState( filter ) = FILTER_EOF_STATE ;
      theIPtr( filter ) = & ( theILastChar( filter ) ) + 1 ;
      theICount( filter ) = 0 ;
      SetIEofFlag( filter ) ;
      return theIPtr(filter)[-1] ;
    }

  case FILTER_ERR_STATE :
    return ioerror_handler( filter ) ;

  default :
    HQFAIL( "rsdFillBuff: illegal filter state" ) ;
  } /* also return EOF to default arm of switch */

  return EOF ;
}

/* -------------------------------------------------------------------------- */
static int32 rsdFilterClose(
  FILELIST* filter,
  int32     flag)
{
  int32       result = 0;
  FILELIST*   uflptr;
  RSD_STATE*  state;

  HQASSERT(filter != NULL,
           "rsdFilterClose: NULL filter pointer");
  HQASSERT(isIRSDFilter(filter),
           "rsdFilterClose: not an RSD filter");
  HQASSERT(((flag == CLOSE_EXPLICIT) || (flag == CLOSE_IMPLICIT) || (flag == CLOSE_FORCE)),
           "rsdFilterClose: unknown close flag");

  if ( !isIOpenFile(filter) ) {
    /* Nothing to do if already been closed! */
    return(result);
  }

  state = theIFilterPrivate(filter);
  HQASSERT((state != NULL),
           "rsdFilterClose: no state");

  /* Close RSD filters if due to a restore or garbage collection, or from an
   * explicit close on the filter unless it is a HT threshold file */
  if ( ((flag == CLOSE_EXPLICIT) && !state->circular) || (flag == CLOSE_FORCE) ) {
    SetIClosingFlag(filter);

    /* Close source if requested */
    if ( isICST(filter) ) {
      uflptr = theIUnderFile(filter);
      if ( uflptr &&
           isIOpenFileFilterById(theIUnderFilterId(filter), uflptr) ) {
        /* While this filter may be being closed implicitly, the closing of the
         * source is explicit */
        result = (*theIMyCloseFile(uflptr))(uflptr, CLOSE_EXPLICIT);
      }
    }

    /* Dispose of RSD internal state and buffers */
    (*theIMyDisposeFile(filter))(filter);

    ClearIClosingFlag(filter);

    /* Mark filter closed */
    SetIEofFlag(filter);
    ClearIOpenFlag(filter);
  }

  return result ;
}

/* -------------------------------------------------------------------------- */
static void rsdFilterDispose( FILELIST *filter )
{
  RSD_STATE *state ;

  HQASSERT(filter != NULL,
           "rsdFilterDispose: NULL filter pointer");
  HQASSERT(isIRSDFilter(filter),
           "rsdFilterDispose: not an RSD filter");

  state = theIFilterPrivate( filter ) ;
  if ( state != NULL ) {
    if ( state->store != NULL ) {
      rsd_storeclose(state->store);
    }
    mm_free( mm_pool_temp , ( mm_addr_t ) state , sizeof( RSD_STATE )) ;
    theIFilterPrivate(filter) = NULL;
  }
}

/* -------------------------------------------------------------------------- */
static int32 rsdBytesAvailable( FILELIST *filter, Hq32x2* avail )
{
  RSD_STATE *state ;
  int32 bytes ;

  HQASSERT((avail != NULL),
           "rsdBytesAvailable: NULL bytecount pointer");
  RSD_ASSERT_FILTER( "rsdBytesAvailable", filter ) ;

  state = theIFilterPrivate( filter ) ;

  if ( state->length == RSD_LEN_NOTSET )
    state->length = rsd_storelength( state->store ) ;

  if ( state->checkpos ) {
    bytes = state->length - state->position ;
  } else {
    bytes = state->length - state->read + theICount( filter ) ;
    if (theIFilterState( filter ) == FILTER_LASTCHAR_STATE)
      ++bytes;
  }

  HQASSERT( bytes >= 0 && bytes <= state->length ,
            "bytes is out of expected range in rsdBytesAvailable" ) ;
  Hq32x2FromInt32(avail, bytes);
  return 0 ;
}

/* -------------------------------------------------------------------------- */
static int32 rsdFlushFile( FILELIST *filter )
{
  RSD_STATE *state ;

  RSD_ASSERT_FILTER( "rsdFlushFile", filter ) ;

  state = theIFilterPrivate( filter ) ;

  if ( state->length == RSD_LEN_NOTSET )
    state->length = rsd_storelength( state->store ) ;

  state->position = state->length ;
  if ( ! state->checkpos ) {
    /* Reset count to force a call to the fillbuff routine. */
    theICount( filter ) = 0 ;
    state->checkpos = TRUE ;
  }
  SetIEofFlag( filter ) ;
  return 0 ;
}

/* -------------------------------------------------------------------------- */
static int32 rsdResetFile( FILELIST *filter )
{
  Hq32x2 file_pos;

  RSD_ASSERT_FILTER( "rsdResetFile", filter ) ;
  Hq32x2FromUint32(&file_pos, 0u);
  return rsdSetFilePosition( filter , &file_pos ) ;
}

/* -------------------------------------------------------------------------- */
static int32 rsdSetFilePosition(FILELIST *filter, const Hq32x2 *position)
{
  RSD_STATE *state ;
  int32 pos;
  int32 res;

  HQASSERT((position != NULL),
           "rsdSetFilePosition: NULL position pointer");
  RSD_ASSERT_FILTER( "rsdSetFilePosition", filter ) ;

  state = theIFilterPrivate( filter ) ;

  res = Hq32x2ToInt32(position, &pos);
  HQASSERT((res),
           "rsdSetFilePosition: position beyond 2GB file limit");
  if ( pos == 0 && state->read == 0 ) {
    state->checkpos = FALSE ;
    state->position = pos ;
    return 0 ;
  }

  /* Setting position to anything other than 0 implies the length of
     the data must be known. */
  if ( state->length == RSD_LEN_NOTSET )
    state->length = rsd_storelength( state->store ) ;

  if ( pos < 0 || pos > state->length )
    return EOF ;

  state->position = pos;
  if ( ! state->checkpos ) {
    /* Reset count to force a call to the fillbuff routine. */
    theICount( filter ) = 0 ;
    state->checkpos = TRUE ;
  }

  if ( pos == state->length )
    SetIEofFlag( filter ) ;
  else
    ClearIEofFlag( filter ) ;
  return 0 ;
}

/* -------------------------------------------------------------------------- */
static int32 rsdFilePosition( FILELIST *filter, Hq32x2* file_pos )
{
  RSD_STATE *state ;
  int32 bytes ;

  HQASSERT((file_pos != NULL),
           "rsdFilePosition: NULL position pointer");
  RSD_ASSERT_FILTER( "rsdFilePosition", filter ) ;

  state = theIFilterPrivate( filter ) ;

  if ( state->checkpos )
    bytes = state->position ;
  else {
    bytes = state->read - theICount( filter ) ;

    if (theIFilterState( filter ) == FILTER_LASTCHAR_STATE ||
        theIFilterState( filter ) == FILTER_EOF_STATE)
      --bytes ;
  }

  HQASSERT( state->length == RSD_LEN_NOTSET ||
            (bytes >= 0 && bytes <= state->length) ,
            "bytes is out of expected range in rsdBytesAvailable" ) ;
  Hq32x2FromInt32(file_pos, bytes);
  return 0 ;
}

/* -------------------------------------------------------------------------- */
static Bool rsdDecodeBuffer( FILELIST *filter , int32 *ret_bytes )
{
  RSD_STATE *state ;

  RSD_ASSERT_FILTER( "rsdDecodeBuffer", filter ) ;

  state = theIFilterPrivate( filter ) ;

  return rsd_storeread( state->store , & theIBuffer( filter ) , ret_bytes ) ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_createfilterlist( FILELIST *rsdfilter ,
                                  OBJECT *name ,
                                  OBJECT *parms ,
                                  Bool rewindable )
{
  OBJECT emptydict = OBJECT_NOTVM_NOTHING ;
  OBJECT filtero = OBJECT_NOTVM_NOTHING ;
  FILELIST *flptr ;
  int32 len ;
  Bool isarray = FALSE ;
  SFRAME myframe ;
  STACK mystack = { EMPTY_STACK, NULL, FRAMESIZE, STACK_TYPE_OPERAND } ;

  mystack.fptr = &myframe ;

  if ( name == NULL )
    return TRUE ;

  HQASSERT(rsdfilter && isIRSDFilter(rsdfilter), "No RSD filter") ;

  flptr = theIUnderFile( rsdfilter ) ;
  HQASSERT( flptr , "flptr NULL" ) ;

  file_store_object(&filtero, flptr, LITERAL) ;

  if ( oType(*name) == ONAME ) {
    len = 1 ;
    if ( parms ) {
      if ( oType(*parms) == OARRAY )
        return error_handler( TYPECHECK ) ;
    }
  } else {
    HQASSERT(oType(*name) == OARRAY, "only know about names & arrays" ) ;
    len = theLen(*name) ;
    name = oArray(*name) ;
    if ( parms ) {
      if ( oType(*parms) == ODICTIONARY )
        return error_handler( TYPECHECK ) ;
      if ( oType(*parms) == OARRAY ) {
        if ( len != theLen(*parms) )
          return error_handler( RANGECHECK ) ;
        parms = oArray(*parms) ;
        isarray = TRUE ;
      }
    }
  }

  if ( parms == NULL ) {
    parms = & emptydict ;
    if ( !ps_dictionary(&emptydict, 0) )
      return FALSE ;
  }

  while ( len > 0 ) {
    uint8 *filter_name ;
    int32 name_length, find_error ;

    if ( oType(*name) != ONAME )
      return error_handler( TYPECHECK ) ;

    if ( oType(*parms) != ONULL && oType(*parms) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;

    filter_name = theICList(oName(*name)) ;
    name_length = theINLen(oName(*name)) ;

    flptr = filter_external_find(filter_name, name_length, &find_error, TRUE) ;
    if ( flptr == NULL ) {
      if ( find_error != NOT_AN_ERROR )
        return error_handler(find_error) ;
      flptr = filter_standard_find(filter_name, name_length) ;
      if ( flptr == NULL )
        return error_handler(UNDEFINED) ;
    }

    /* Push the parameters on the stack and create filter */
    {
      Bool success;

      success = push(&filtero, &mystack);
      if (oType(*parms) != ONULL) {
        /* Only push the parameters if the are not null */
        success = success && push(parms, &mystack);
      }
      if (!success || !filter_create_object(flptr, &filtero, NULL, &mystack))
        return FALSE;
    }

    HQASSERT(isEmpty(mystack), "Filter didn't remove args from stack") ;

    flptr = oFile(filtero) ;
    if ( rewindable )
      SetIRewindableFlag( flptr ) ;

    --len ;
    ++name ;
    if ( isarray )
      ++parms ;
  }

  theIUnderFile( rsdfilter ) = flptr ;
  theIUnderFilterId( rsdfilter ) = theIFilterId( flptr ) ;

  return TRUE;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_seekable( FILELIST *flptr )
{
  DEVICE_FILEDESCRIPTOR d ;
  DEVICELIST *dev ;
  Hq32x2 ext ;

  if ( isIRSDFilter(flptr) )
    return TRUE ;

  d = theIDescriptor( flptr ) ;
  if ( ( dev = theIDeviceList( flptr )) == NULL )
    /* It is a filter and therefore not seekable. */
    return FALSE ;

  /* Try to find the file extent; will succeed if seekable. */
  return (*theIBytesFile(dev))( dev , d , & ext , SW_BYTES_TOTAL_ABS ) ;
}

/* -------------------------------------------------------------------------- */
/* HQN EXTENSION AND NOT PART OF THE STANDARD RSD SPECIFICATION. */
/* Accessor function to allow private circular flag to be set/cleared by
 * external routines. */
void rsdSetCircularFlag( FILELIST *filter , int32 value )
{
  RSD_STATE *state ;

  RSD_ASSERT_FILTER( "rsdSetCircularFlag", filter ) ;

  HQASSERT( BOOL_IS_VALID(value) ,
           "rsdSetCircularFlag: boolean value expected" ) ;

  state = theIFilterPrivate( filter ) ;
  state->circular = ( int8 ) value ;
}

/* -------------------------------------------------------------------------- */

void rsd_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* reusable stream decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("ReusableStreamDecode") ,
                       FILTER_FLAG | READ_FLAG | RSD_FLAG ,
                       0, NULL , 0 ,
                       rsdFillBuff,                          /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       rsdFilterInit,                        /* initfile */
                       rsdFilterClose,                       /* closefile */
                       rsdFilterDispose,                     /* disposefile */
                       rsdBytesAvailable,                    /* bytesavail */
                       rsdResetFile,                         /* resetfile */
                       rsdFilePosition,                      /* filepos */
                       rsdSetFilePosition,                   /* setfilepos */
                       rsdFlushFile,                         /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       rsdDecodeBuffer,                      /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/* Log stripped */
