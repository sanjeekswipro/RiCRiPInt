/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:subfile.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * SubFileDecode filter.
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "namedef_.h"

#include "fileio.h"
#include "subfile.h"

/* subfile filter routines */

/*
 * A decoding filter uses:
 *   descriptor:     A pointer to a SUBFILEDECODESTATE structure.
 *   filter_status:  As with all decoding filters.
 */


enum subfile_source { source_Under, source_Fill, source_Own };

typedef struct subfiledecodestate {
  uint8 *eod_buffer; /* Holds EOD string, and used as a filter buffer, so an
                        extra byte at the beginning */
  size_t eod_string_len;
  int32 eod_count;
  uint8 buf[2];
  int i_match; /* next unmatched char in EOD string */
  int *overlap; /* overlap array for KMP string match */
  enum subfile_source next_source;
} SUBFILEDECODESTATE ;


Bool subfileFilterInit( FILELIST *filter,
                        OBJECT *args ,
                        STACK *stack )
{
  SUBFILEDECODESTATE *subfile_state ;
  int32 eod_count ;
  uint8 *eod_string ;
  int32 eod_string_len ;
  OBJECT *thestring = NULL ;
  OBJECT *thecount = NULL ;
  int32 pop_args = 0 ;
  size_t i;
  int *overlap;

  enum {
    match_EODCount, match_EODString, match_n_entries
  } ;
  static NAMETYPEMATCH thematch[match_n_entries + 1] = {
    { NAME_EODCount | OOPTIONAL  , 1 , { OINTEGER }} ,
    { NAME_EODString | OOPTIONAL , 1 , { OSTRING }} ,
    DUMMY_END_MATCH
  } ;

  HQASSERT( filter , "filter NULL in subfileFilterInit" ) ;
  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  /* Check for optional arguments. Either have count string, dict count
     string or dict on stack if args dict parameter not present. */
  if ( ! args ) {
    OBJECT *theo ;

    if ( isEmpty(*stack) )
      return error_handler( STACKUNDERFLOW ) ;

    theo = theITop(stack) ;
    if ( oType(*theo) != ODICTIONARY ) {
      if ( theIStackSize(stack) < 1 )
        return error_handler( STACKUNDERFLOW ) ;

      if ( oType(*theo) != OSTRING )
        return error_handler( TYPECHECK ) ;

      if ( ! oCanRead(*theo) )
        if ( ! object_access_override(theo))
          return error_handler( INVALIDACCESS ) ;
      thestring = theo ;

      theo = stackindex(1 , stack ) ;
      if ( oType(*theo) != OINTEGER )
        return error_handler( TYPECHECK ) ;
      thecount = theo ;

      pop_args = 2 ;
    }

    if ( theIStackSize(stack) >= pop_args ) {
      args = stackindex(pop_args, stack) ;
      if ( oType(*args) == ODICTIONARY )
        ++pop_args ;
    }
  }

  HQASSERT((thestring == NULL && thecount == NULL) ||
           (thestring != NULL && thecount != NULL),
           "String and count must be both set or both clear") ;

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) )
      if ( ! object_access_override(oDict(*args)))
        return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( args , thematch ))
      return FALSE ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;

    /* Now, if values were supplied on the stack, they take priority over
     * any in the dictionary (it's just for CloseSource presumably). But
     * if there weren't discrete arguments on the stack, both of the keys
     * are required.
     */

    if ( ! thecount )
      thecount = thematch[match_EODCount].result ;
    else
      HQTRACE(thematch[match_EODCount].result != NULL,
              ("Warning: ignoring EODCount value from dictionary in SubFileDecode.")) ;

    if ( ! thestring )
      thestring = thematch[match_EODString].result ;
    else
      HQTRACE(thematch[match_EODString].result != NULL,
              ("Warning: ignoring EODString value from dictionary in SubFileDecode.")) ;
  } else
    args = NULL ;

  if ( !thecount )
    return error_handler( UNDEFINED ) ;

  eod_count = oInteger(*thecount) ;
  if ( eod_count < 0 )
    return error_handler( RANGECHECK ) ;

  if ( !thestring )
    return error_handler( UNDEFINED ) ;

  eod_string = oString(*thestring) ;
  eod_string_len = theILen( thestring ) ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  subfile_state = ( SUBFILEDECODESTATE * )
                    mm_alloc( mm_pool_temp ,
                              sizeof( SUBFILEDECODESTATE ) ,
                              MM_ALLOC_CLASS_SUBFILE_STATE ) ;
  if ( ! subfile_state )
    return error_handler( VMERROR ) ;

  subfile_state->eod_buffer = mm_alloc(mm_pool_temp, eod_string_len + 1,
                                       MM_ALLOC_CLASS_SUBFILE_STATE);
  if ( subfile_state->eod_buffer == NULL )
    return error_handler(VMERROR);
  HqMemCpy(&subfile_state->eod_buffer[1], eod_string, eod_string_len);
  subfile_state->eod_string_len = eod_string_len ;
  subfile_state->eod_count = eod_count ;
  subfile_state->i_match = 0;

  overlap = mm_alloc(mm_pool_temp, sizeof(int) * (eod_string_len + 1),
                     MM_ALLOC_CLASS_SUBFILE_STATE);
  if ( overlap == NULL )
    return error_handler(VMERROR);
  overlap[0] = -1;
  for ( i = 0 ; i < (size_t)eod_string_len ; ++i ) {
    int o1 = overlap[i+1] = overlap[i] + 1;
    while ( o1 > 0 && eod_string[i] != eod_string[o1 - 1] )
      o1 = overlap[i+1] = overlap[o1 - 1] + 1;
  }
  subfile_state->overlap = overlap;

  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = 2;
  theIFilterState( filter ) = FILTER_INIT_STATE ;
  theIFilterPrivate( filter ) = subfile_state ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}


static void subfileFilterDispose( FILELIST *filter )
{
  SUBFILEDECODESTATE *state;

  HQASSERT( filter , "filter NULL in subfileFilterDispose." ) ;

  state = (SUBFILEDECODESTATE *)theIFilterPrivate(filter);

  if ( state != NULL ) {
    if ( state->eod_buffer != NULL )
      mm_free(mm_pool_temp, state->eod_buffer, state->eod_string_len + 1);
    if ( state->overlap != NULL )
      mm_free(mm_pool_temp, state->overlap,
              sizeof(int) * (state->eod_string_len + 1));
    mm_free(mm_pool_temp, state, sizeof(SUBFILEDECODESTATE));
    theIFilterPrivate( filter ) = NULL ;
  }
}

/* ----------------------------------------------------------------------------
   function:            subfileDecodeBuffer   author:              Luke Tunmer
   creation date:       30-Jul-1991           last modification:   ##-###-####
---------------------------------------------------------------------------- */
static Bool subfileDecodeBuffer( FILELIST *filter , int32 *ret_bytes )
{
  FILELIST *uflptr;
  int32 c;
  int32 count= 0; /* #bytes returned, <= 0 on EOD */
  int32 ucount= 0; /* #bytes consumed from underfile buffer */
    /* @@@@ consider changing from ucount to ptr */
  uint8 *eod_string;
  int32 eod_string_len;
  int32 eod_count;
  SUBFILEDECODESTATE *state ;
  int *overlap;
  int i_match;

  HQASSERT( filter , "filter NULL in subfileDecodeBuffer." ) ;
  HQASSERT( ret_bytes , "ret_bytes NULL in subfileDecodeBuffer." ) ;

  uflptr = theIUnderFile( filter ) ;
  state = (SUBFILEDECODESTATE *)theIFilterPrivate(filter);
  HQASSERT( uflptr , "uflptr NULL in subfileDecodeBuffer." ) ;
  HQASSERT( state , "state NULL in subfileDecodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  eod_string = &state->eod_buffer[1];
  eod_string_len = state->eod_string_len ;
  eod_count = state->eod_count ;
  overlap = state->overlap;
  i_match = state->i_match;

  /* Three possible sources:
      Own: Read 1 char (that was returned by GetNextBuf) from own buffer.
      Under: Read from underfile buffer.
      EOD_fail: Read a prefix from the EOD string.
    Normally, this alternates between Own & Under. If there's a partial match of
    the EOD string at the end of a buffer, this is not passed through; if the
    match fails, state EOD_fail is entered to pass the bytes held back. */

  if ( theIFilterState(filter) == FILTER_INIT_STATE )
    state->next_source = uflptr->count == 0 ? source_Fill : source_Under;

 refill:
  switch ( state->next_source ) {
  case source_Under:
    filter->buffer = uflptr->ptr;
    HQASSERT(isIFilter(uflptr) || uflptr->buffer < uflptr->ptr,
             "No space for filter lastchar");
    ucount = count = uflptr->count;
    state->next_source = source_Fill;
    break;
  case source_Fill:
    HQASSERT(uflptr->count == 0, "Should have emptied underfile buffer.");
    c = GetNextBuf(uflptr);
    if ( c == EOF ) {
      if ( isIIOError(uflptr) )
        return FALSE;
      if ( i_match > 0 && eod_count == 0 ) /* failed match after a hold */ {
        /* So, pass the bytes of the failed match. */
        filter->buffer = eod_string;
        *ret_bytes = (int32)-i_match; /* negative for EOD */
      } else {
        filter->buffer = &state->buf[1]; /* caller might still store into it */
        *ret_bytes = 0;
      }
      return TRUE;
    }
    state->buf[1] = (uint8)c;
    /* fallthrough */
  case source_Own:
    filter->buffer = &state->buf[1];
    count = 1; ucount = 0;
    state->next_source = source_Under;
    break;
  }

  if ( eod_string_len == 0 ) {
    if ( eod_count == 0 ) /* pass everything through */
      goto output;
    else { /* pass eod_count bytes through */
      count = min(count, eod_count);
      ucount = ucount == 0 ? 0 : count;
      eod_count -= count;
      if ( eod_count == 0 )
        count = -count;
    }
  } else { /* pass until eod string */
    uint8 *ptr, *end;

    for ( ptr = filter->buffer, end = ptr + count ; ptr < end ; ++ptr )
    rematch:
      if ( *ptr == eod_string[i_match] ) {
        ++i_match;
        if ( i_match == eod_string_len ) /* matched EOD */ {
          i_match = 0; /* don't match any substring of that again */
          if ( --eod_count > 0 )
            continue;
          else { /* final EOD */
            count = (int32)(filter->buffer - ptr - 1); /* negative for EOD */
            ucount = ucount == 0 ? 0 : -count; /* consume the match */
            if ( state->eod_count == 0 ) /* not counting, don't pass EOD */
              count = min(0, count + eod_string_len);
            goto output;
          }
        }
      } else if ( i_match > 0 ) /* failed partial match */ {
        if ( state->i_match > 0 && eod_count == 0 ) /* after a hold */ {
          int next = overlap[i_match];

          /* Reset to continue current source next time, */
          state->next_source =
            state->next_source == source_Fill ? source_Under : source_Own;
          ucount = ptr - filter->buffer;
          /* instead pass the bytes of the failed match. */
          filter->buffer = eod_string;
          count = (int32)(i_match - next);
          /* Setup next search. */
          i_match = next;
          goto output;
        }
        i_match = overlap[i_match];
        goto rematch;
      }

    /* reached end of buffer without a full match */
    if ( i_match > 0 && eod_count == 0 ) /* partial match, must hold */
      count -= i_match;
    if ( count <= 0 ) /* only EOD bytes in this buffer */ {
      if ( ucount > 0 ) {
        uflptr->ptr += ucount; uflptr->count = 0;
      }
      state->i_match = i_match; /* record i_match at start of buffer */
      goto refill; /* So sue me. */
    }
  }
 output:
  if ( ucount > 0 ) {
    uflptr->count -= ucount; uflptr->ptr += ucount;
  }
  if ( count > 0 ) {
    state->eod_count = eod_count ;
    state->i_match = i_match;
  }
  *ret_bytes = count ;
  return TRUE ;
}


void subfile_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* subfile decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("SubFileDecode") ,
                       FILTER_FLAG | READ_FLAG | DELIMITS_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       subfileFilterInit,                    /* initfile */
                       FilterCloseFile,                      /* closefile */
                       subfileFilterDispose,                 /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       subfileDecodeBuffer ,                 /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/*
Log stripped */
