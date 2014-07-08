/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:procfilt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS procedure filter
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "dictscan.h"
#include "namedef_.h"

#include "fileio.h"

#include "stacks.h"
#include "control.h"

static int32 procedureFilterFlushBuff( int32 c , FILELIST *filter ) ;
static int32 procedureFilterFillBuff( FILELIST *filter ) ;

#define isProcedureTarget(_f) (theIFillBuffer(_f) == procedureFilterFillBuff)
#define isProcedureSource(_f) (theIFlushBuffer(_f) == procedureFilterFlushBuff)


/* ----------------------------------------------------------------------------
   functions:           Procedure Filter  author:              Luke Tunmer
   creation date:       15-Jul-1991       last modification:   ##-###-####

   Routines to implement a procedure as a source or a target for a filter.

---------------------------------------------------------------------------- */
static int32 procedureFilterFillBuff( FILELIST *filter )
{
  OBJECT *o ;
  int32  len ;

  if ( ! push(&theIParamDict(filter), &executionstack) )
    return EOF ;
  if (! interpreter(1, NULL) ) {
    return EOF ;
  }
  if ( isEmpty( operandstack )) {
    (void)error_handler( STACKUNDERFLOW ) ;
    return EOF ;
  }
  o = theTop( operandstack ) ;
  if ( oType(*o) != OSTRING ) {
    (void)error_handler( TYPECHECK ) ;
    return EOF ;
  }
  if ( ! oCanRead(*o) && !object_access_override(o) ) {
    (void)error_handler( INVALIDACCESS ) ;
    return EOF ;
  }
  if ( ( len = theILen( o )) == 0 ) {
    pop( &operandstack );
    SetIEofFlag( filter ) ;
    ClearIOpenFlag( filter ) ;
    return EOF ;
  }
  theIBufferSize( filter ) = len ;
  theIReadSize( filter ) = len ;
  theICount( filter ) = len - 1 ;
  theIBuffer( filter ) = oString(*o) ;
  theIPtr( filter ) = theIBuffer( filter ) + 1 ;
  pop( &operandstack );
  return ( int32 ) *(theIBuffer( filter )) ;
}


/* Last Modified:       27-Apr-1995
 * Modification history:
 *      08-Mar-95 (N.Speciner); fix bug 5152. No longer pass to the filter
 *              procedure a string one shorter than the buffer size. Since
 *              most procedure filters just return the passed in string, this
 *              had the effect of shortening the filter by one byte on each
 *              call to procedureFilterFlushBuff, since the string returned
 *              from the procedure is used to set the new buffer address and
 *              length.
 *              Fix involves putting the passed in character into the buffer
 *              (if it will fit) before calling the procedure, and otherwise
 *              adding it as the first character of the new buffer (i.e., after
 *              the procedure is called) if the buffer is full (or 0 length,
 *              as on first call to procedure) when this routine is called.
 *              (NOTE, however, that this routine is called with the count one
 *              greater than the number of characters actually in the buffer,
 *              with the passed in character making the difference).
 *      09-Mar-95 (N.Speciner); add some assertions.
 *      27-Apr-95 (N.Speciner); fix bug with assertion discovered by
 *              A.Aitchison: when the buffersize is 1, the flush is called with
 *              a count of 2, which tripped the below ASSERT looking for
 *              equality of size and count. Change the assert to look for
 *              count at least as big as buffersize.
 */
static int32 procedureFilterFlushBuff( int32 c , FILELIST *filter )
{
  OBJECT *o ;
  int32  len ;
  register int32 charsToDo = 1; /* come in with 1 character to output */

  HQASSERT(theIBufferSize( filter ) == 0 ||
           theICount( filter ) >= theIBufferSize( filter ),
           "procedure-filter buffer is not full, but flushbuff called");
  oString(snewobj) = theIBuffer( filter ) ;

  if (theICount( filter ) == theIBufferSize( filter)) {
    HQASSERT(theIPtr( filter ) != NULL,
             "procedure-filter buffer pointer unset, but chars in buffer");
    *(theIPtr( filter )++) = (uint8) c ;
    charsToDo = 0;
  }
  theLen( snewobj ) = CAST_TO_UINT16(theICount( filter ) - charsToDo) ;

  /* This assert tests that we didn't overflow the buffer, basically, but it's
     complicated by the fact that we use a flush to acquire the initial buffer
     (so zero is okay as a size, if there's no string yet).
  */
  HQASSERT( theIBufferSize( filter ) >= theLen( snewobj ) ||
            (theIBufferSize( filter ) == 0 && theLen( snewobj ) == 0),
            "procedure-filter buffer overflowed");

  if ( theIBufferSize( filter ) < theLen( snewobj )) /* buffer not supplied? */
    theLen( snewobj ) = CAST_TO_UINT16( theIBufferSize( filter ));

  if (! push( &snewobj , &operandstack ) ||
      ! push( &tnewobj , &operandstack ) ||
      ! push(&theIParamDict(filter) , &executionstack )) {
    theICount( filter ) = 0;
    return EOF ;
  }
  if ( ! interpreter( 1 , NULL )) {
    theICount( filter ) = 0;
    return EOF ;
  }
  if ( isEmpty( operandstack )) {
    (void)error_handler( STACKUNDERFLOW ) ;
    theICount( filter ) = 0;
    return EOF ;
  }
  o = theTop( operandstack ) ;
  if ( oType(*o) != OSTRING ) {
    (void)error_handler( TYPECHECK ) ;
    theICount( filter ) = 0;
    return EOF ;
  }
  if ( ! oCanRead(*o) && !object_access_override(o) ) {
    (void)error_handler( INVALIDACCESS ) ;
    theICount( filter ) = 0;
    return EOF ;
  }
  if ( ( len = theILen( o )) == 0 ) {
    (void)error_handler( TYPECHECK ) ;
    theICount( filter ) = 0;
    return EOF ;
  }

  theIPtr( filter ) = theIBuffer( filter ) = oString(*o) ;
  HQASSERT(theIPtr( filter ) != NULL,
           "procedure-filter buffer has a null value on procedure return");
  theIBufferSize( filter ) = len ;
  if ((theICount( filter ) = charsToDo) != 0)
    *(theIPtr( filter )++) = (uint8) c ;

  pop( &operandstack );
  return 0 ;
}


static int32 procedureFilterCloseFile(FILELIST *filter, int32 flag)
{
  OBJECT *o ;
  error_context_t *errcontext = get_core_context_interp()->error;
  int32 old_err = FALSE;

  UNUSED_PARAM(int32, flag);

  theLen(snewobj) = CAST_TO_UINT16(theICount(filter)) ;
  oString(snewobj) = (theLen(snewobj) == 0 ? NULL : theIBuffer(filter)) ;

  /* This assert tests that we didn't overflow the buffer, basically, but it's
     complicated by the fact that we use a flush to acquire the initial buffer
     (so zero is okay as a size, if there's no string yet).
   */
  HQASSERT( theIBufferSize( filter ) >= theLen( snewobj ) ||
            (theIBufferSize( filter ) == 0 && theLen( snewobj ) == 0),
            "procedure-filter buffer overflowed for close");

  SetIClosingFlag( filter ) ;

  if (! push( &snewobj , &operandstack ) ||
      ! push( &fnewobj , &operandstack ) ||
      ! push(&theIParamDict(filter) , &executionstack) ) {
    theICount( filter ) = 0;
    return EOF ;
  }
  if ( error_signalled_context(errcontext) )
    error_save_context(errcontext, &old_err);
  if ( ! interpreter(1, NULL) ) {
    if ( old_err )
      error_restore_context(errcontext, old_err);
    theICount( filter ) = 0;
    return EOF ;
  }
  if ( old_err )
    error_restore_context(errcontext, old_err);

  if ( isEmpty( operandstack )) {
    (void)error_handler( STACKUNDERFLOW ) ;
    theICount( filter ) = 0;
    return EOF ;
  }
  o = theTop( operandstack ) ;
  if ( oType(*o) != OSTRING ) {
    (void)error_handler( TYPECHECK ) ;
    theICount( filter ) = 0;
    return EOF ;
  }
  pop( &operandstack ) ;

  ClearIClosingFlag( filter ) ;
  SetIEofFlag( filter ) ;
  HQASSERT( ! isIRewindable( filter ) ,
            "Rewindable procedure filter - how did we manage that?" ) ;
  ClearIOpenFlag( filter ) ;

  return 0 ;
}



/** Routine to cope with procedure filters whose string buffer has been
   reclaimed by a restore.

   Buffer is reset, and additionally:
   Input  - if buffer is not empty reader will 'lose' data - can only
            error here
   Output - if buffer is not empty then we try to flush the buffer. If
            this is not successful, then the filter's errorhandler is
            called, and an error returned.
---------------------------------------------------------------------------- */
static Bool procedureFilterDoRestore( FILELIST *filter )
{
  int32 c ;

  HQASSERT(isProcedureTarget(filter) || isProcedureSource(filter),
           "procedureFilterDoRestore called with non-procedure filter" ) ;

  if ( theICount( filter ) > 0 ) { /* Uh Oh - buffer contains data */
    if ( isProcedureTarget( filter )) { /* Its a write - we can flush it! */
      /* But FlushBuffer methods expect a char - so we remove the last char
       * and re-add it. Don't adjust count - FlushBuff assumes this has
       * already been incremented on entry.
       */
      c = (int32) *(--theIPtr( filter )) ;
      if ((*theIFlushBuffer( filter ))( c , filter ) == EOF )
        return (*theIFileLastError( filter ))( filter ) ;
    }
    else { /* procedureSource - unrecoverable situation */
      theIFilterState( filter ) = FILTER_ERR_STATE ;
      SetIIOErrFlag( filter ) ;
    }
  }

  /* Whatever happened, the buffer will soon be invalid so we reset it here
   * in the hope that the filter will recreate the buffer next time we need
   * to use it.
   */
  theIBufferSize( filter ) = 0 ;
  theIReadSize( filter ) = 0 ;
  theICount( filter ) = 0 ;
  theIBuffer( filter ) = NULL ;
  theIPtr( filter ) = NULL ;
  return TRUE ;
}


Bool checkValidProcFilters( int32 slevel )
{
  FILELIST *flptr, *funder ;
  int32 aslevel ;
  filelist_iterator_t iter ;

  aslevel = NUMBERSAVES( slevel ) ;

  for ( flptr = filelist_first(&iter,
                               TRUE /* local */,
                               (aslevel <= MAXGLOBALSAVELEVEL) /* global */) ;
        flptr ;
        flptr = filelist_next(&iter) ) {
    /* Skip if not a filter, or if closed */
    if ( ! isIFilter( flptr ) || ! isIOpenFile( flptr ))
      continue ;

    /* Skip if filter does not have an underlying file
     * (ie it is a dummy filter eg string/procedure etc.).
     */
    if (( funder = theIUnderFile( flptr )) == NULL )
      continue ;

    /* Skip if underlying file is not open */
    if ( ! isIOpenFileFilterById( theIUnderFilterId( flptr ) , funder ))
      continue ;

    /* Does this underlying file have a(n active) procedure source/target? */
    if (( isProcedureSource( funder ) || isProcedureTarget( funder )) &&
        theIBuffer( funder )) {
      /* Buffer is a string -> composite object */
      if ( mm_ps_check( aslevel,
                        (mm_addr_t) theIBuffer( funder )) != MM_SUCCESS ) {
        /* Buffer will become invalid */
        if ( ! procedureFilterDoRestore( funder ))
          return FALSE ;
      }
    }
  }
  return TRUE ;
}


/** For a procedure filter, the argument is an array rather than a
   dictionary. There is no underlying file to a procedure filter. */
static Bool procedureFilterInit(FILELIST *filter, OBJECT *args, STACK *stack)
{
  int32 pop_args = 0 ;

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  if ( ! args ) {
    if ( isEmpty(*stack) )
      return error_handler(STACKUNDERFLOW) ;

    args = theITop(stack) ;
    pop_args = 1 ;
  }

  if ( !oExec(*args) ||
       (oType(*args) != OARRAY && oType(*args) != OPACKEDARRAY) )
    return error_handler(TYPECHECK) ;

  if ( !oCanExec(*args) )
    if ( ! object_access_override(args) )
      return error_handler(INVALIDACCESS) ;

  OCopy(theIParamDict(filter), *args) ;

  /* Always close a procedure source/target. */
  SetICSTFlag(filter) ;

  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

/* Routine to create the standard proc filter */
void procedure_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%procedureEncode") ,
                       FILTER_FLAG | WRITE_FLAG | OPEN_FLAG ,
                       0, NULL, 0 ,
                       FilterError,                          /* fillbuff */
                       procedureFilterFlushBuff,             /* flushbuff */
                       procedureFilterInit,                  /* initfile */
                       procedureFilterCloseFile,             /* closefile */
                       FilterDispose,                        /* disposefile */
                       FilterBytesAvailNoOp,                 /* bytesavail */
                       FilterNoOp,                           /* resetfile */
                       FilterFilePosNoOp,                    /* filepos */
                       FilterSetFilePosNoOp,                 /* setfilepos */
                       FilterNoOp,                           /* filterflushfile */
                       FilterEncodeError ,                   /* filterencode */
                       FilterDecodeError ,                   /* filterdecode */
                       FilterLastError ,                     /* lasterror */
                       0, NULL, NULL, NULL ) ;
}

void procedure_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%procedureDecode") ,
                       FILTER_FLAG | READ_FLAG | OPEN_FLAG ,
                       0, NULL, 0 ,
                       procedureFilterFillBuff,              /* fillbuff */
                       FilterFlushBufError ,                 /* flushbuff */
                       procedureFilterInit,                  /* initfile */
                       FilterCloseFile,                      /* closefile */
                       FilterDispose,                        /* disposefile */
                       FilterBytesAvailNoOp,                 /* bytesavail */
                       FilterNoOp,                           /* resetfile */
                       FilterFilePosNoOp,                    /* filepos */
                       FilterSetFilePosNoOp,                 /* setfilepos */
                       FilterNoOp,                           /* filterflushfile */
                       FilterEncodeError ,                   /* filterencode */
                       FilterDecodeError ,                   /* filterdecode */
                       FilterLastError ,                     /* lasterror */
                       0, NULL, NULL, NULL ) ;
}


/* Log stripped */
