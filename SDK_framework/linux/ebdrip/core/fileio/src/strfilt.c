/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:strfilt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * String filter implementation
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "objstack.h"
#include "mm.h"
#include "mmcompat.h"
#include "dictscan.h"
#include "namedef_.h"

#include "fileio.h"
#include "strfilt.h"

/* ----------------------------------------------------------------------------
   functions:           String Filter     author:              Luke Tunmer
   creation date:       16-Jul-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */

static int32 stringFilterFillBuff( register FILELIST *filter )
{
  /* come to the end of the string, therefore... */
  SetIEofFlag( filter ) ;
  ClearIOpenFlag( filter ) ;

  return ( EOF ) ;
}

static int32 stringFilterFlushBuff( int32 c, FILELIST *filter )
{
#ifdef  HAS_PRAGMA_UNUSED
#pragma unused( c )
#else
  UNUSED_PARAM(int32, c);
#endif  /* HAS_PRAGMA_UNUSED */

  /* the buffer size stored in the filter struct is one larger than the
   * actual size of the buffer so that this routine can fail when it is
   * called with c. (See implementation of Putc macro).
   */
  SetIEofFlag( filter ) ;
  ClearIOpenFlag( filter ) ;

  return ( EOF ) ;
}

/* For a string filter, the argument is a string rather than a
   dictionary. There is no underlying file to a string filter. */
static Bool stringFilterInit(FILELIST *filter, OBJECT *args, STACK *stack)
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

  if ( oType(*args) != OSTRING )
    return error_handler(TYPECHECK) ;

  if ( isIInputFile(filter) ) {
    if ( ! oCanRead(*args) && ! object_access_override(args) )
      return error_handler(INVALIDACCESS) ;

    theICount(filter) = theIBufferSize(filter) = theILen(args) ;
  } else {
    if ( ! oCanWrite(*args) && ! object_access_override(args) )
      return error_handler(INVALIDACCESS) ;

    theIBufferSize(filter) = theILen(args) + 1 ;
  }
  theIBuffer(filter) = theIPtr(filter) = oString(*args) ;

  OCopy(theIParamDict(filter), *args) ;

  /* Always close a string source/target. */
  SetICSTFlag(filter) ;

  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

/* string filter initialisation */
void string_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%stringEncode") ,
                       FILTER_FLAG | WRITE_FLAG | OPEN_FLAG ,
                       0, NULL , 0 ,
                       FilterError,             /* fillbuff */
                       stringFilterFlushBuff ,  /* flushbuff */
                       stringFilterInit,        /* initfile */
                       FilterCloseFile,         /* closefile */
                       FilterDispose,           /* disposefile */
                       FilterBytesAvailNoOp,    /* bytesavail */
                       FilterNoOp,              /* resetfile */
                       FilterFilePosNoOp,       /* filepos */
                       FilterSetFilePosNoOp,    /* setfilepos */
                       FilterNoOp,              /* flushfile */
                       FilterEncodeError,       /* filterencode */
                       FilterDecodeError,       /* filterdecode */
                       FilterLastError ,        /* lasterror */
                       0, NULL, NULL,
                       NULL ) ;
}

void string_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%stringDecode") ,
                       FILTER_FLAG | READ_FLAG | OPEN_FLAG ,
                       0, NULL , 0 ,
                       stringFilterFillBuff,    /* fillbuff */
                       FilterFlushBufError,     /* flushbuff */
                       stringFilterInit,        /* initfile */
                       FilterCloseFile,         /* closefile */
                       FilterDispose,           /* disposefile */
                       FilterBytesAvailNoOp,    /* bytesavail */
                       FilterNoOp,              /* resetfile */
                       FilterFilePosNoOp,       /* filepos */
                       FilterSetFilePosNoOp,    /* setfilepos */
                       FilterNoOp,              /* flushfile */
                       FilterEncodeError,       /* filterencode */
                       FilterDecodeError,       /* filterdecode */
                       FilterLastError ,                     /* lasterror */
                       0, NULL, NULL,
                       NULL ) ;
}


/* Log stripped */
