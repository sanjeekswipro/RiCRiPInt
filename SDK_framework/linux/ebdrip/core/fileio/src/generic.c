/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:generic.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Generic filter implementation
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
#include "devices.h"
#include "namedef_.h"

#include "fileio.h"
#include "generic.h"
#include "progress.h"

/* This is a GROSS hack to get this filter to compile. I haven't yet decided
   how to handle this routine; neither COREdevices nor COREobjects knows
   about the other, and I would like to keep it that way. device_set_params
   is logically a layer on top of both of them. It's not part of COREfileio,
   because it imparts no file-related functionality, and is certainly not at
   home in SWv20. It just seems a bit much to have a separate compound for
   just this one function; it may be worthwhile creating a separate compound
   to add the device parameter functionality though. */

extern int32 device_set_params(OBJECT *thekey, OBJECT *theval, void *argBlockPtr) ;

/* -----------------------------------------------------------------------
  new core rip functions for generic filters  - Cindy Wells 29-Sep-1992
-------------------------------------------------------------------------- */

static Bool genericFilterInit( FILELIST *filter ,
                               OBJECT *args ,
                               STACK *stack )
{
  DEVICELIST  *dlist;
  uint8       *buff;
  DEVICE_FILEDESCRIPTOR desc;
  int32 flag;
  int32 pop_args = 0 ;

  HQTRACE( debug_filters , ( "In genericFilterInit" )) ;

  dlist = theIDeviceList( filter );
  HQASSERT(dlist, "encountered bad filter");
  theINextDev( dlist ) = ( DEVICELIST *)filter;   /* connect up strs */

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
      return error_handler(INVALIDACCESS ) ;
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

  if ( args ) {
    if ( ! walk_dictionary( args, device_set_params, (void *)dlist) )
      return FALSE ;
  }

  /* allocate space for buffer */
  theIBufferSize( filter ) = (*theIGetBuffSize( dlist ))( dlist );
  buff = ( uint8 * )mm_alloc( mm_pool_temp , theIBufferSize( filter ) + 4 ,
                              MM_ALLOC_CLASS_FILTER_BUFFER ) ;
  if ( buff == NULL )
    return error_handler( VMERROR ) ;
  theIBuffer( filter ) = buff + 4 ;
  theIPtr( filter ) = buff + 4 ;
  theICount( filter ) = 0 ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  if ( isIInputFile( filter ))
    flag = SW_RDONLY ;
  else                         /* assume isIOutputFile is TRUE */
    flag = SW_WRONLY ;

 /* open a file on the transform device */

  desc = ( * theIOpenFile( dlist ))( dlist , NULL , flag ) ;
  if ( desc < 0 )
    return device_error_handler(dlist);

  theIDescriptor( filter ) = desc ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static int32 genericFilterCloseAbort( FILELIST * filter, int32 fAbort )
{
  /* close transform file and dismount transform device if no other
     filter of this type is active */

  int32 result = 0 ;
  DEVICELIST *dlist ;
  FILELIST *uflptr ;

  HQASSERT( filter , "filter NULL in genericFilterCloseAbort." ) ;

  HQTRACE( debug_filters , ( "In genericFilterCloseAbort" )) ;

  dlist = theIDeviceList( filter ) ;

  SetIClosingFlag( filter ) ;

  if ( isIOutputFile( filter ))
    result = ( *theIMyFlushFile( filter ))( filter ) ;

  uflptr = theIUnderFile( filter ) ;

  if ( uflptr && isICST( filter ) &&
       isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr )) {
    /* While this filter may be being closed implicitly, the closing of the
     * source is explicit */
    if (( *theIMyCloseFile( uflptr ))( uflptr, CLOSE_EXPLICIT ) == EOF )
      result = EOF ;
  }

  if ( dlist ) {
    DEVICE_FILEDESCRIPTOR desc = theIDescriptor( filter ) ;

    HQASSERT( theIBuffer( filter ) != NULL , "encountered bad filter" ) ;

    if ( fAbort )
     (void)( *theIAbortFile( dlist ))( dlist , desc ) ;
    else
     (void)( *theICloseFile( dlist ))( dlist , desc ) ;

    (void)closeReadFileProgress( filter ) ;

    /* call to dismount device unconditional */
    (void)( *theIDevDismount( dlist ))( dlist ) ;

    device_free(dlist) ;

    theIDeviceList( filter ) = NULL ;
  }

  if (theIBuffer(filter)) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 4 ) ,
             ( mm_size_t )( theIBufferSize( filter ) + 4 )) ;
    theIBuffer( filter ) = NULL ;
  }

  ClearIClosingFlag( filter ) ;
  SetIEofFlag( filter ) ;
  if ( ! isIRewindable( filter ))
    ClearIOpenFlag( filter ) ;

  return result ;
}

static int32 genericFilterClose( FILELIST * filter, int32 flag )
{
  UNUSED_PARAM(int32, flag);

  return genericFilterCloseAbort( filter, FALSE ) ;
}

static Bool genericFilterLastError( FILELIST *filter )
{
  Bool result = FALSE;
  DEVICELIST *dev;

  HQTRACE( debug_filters , ( "In genericFilterLastError" )) ;

  /* I (JJ) am not totally convinced by this test. It's throwing an error from
   * the device if one hasn't already been thrown by the filter code.
   * Right or wrong, it corresponds to what was there before.
   */
  if ( newerror <= 0 ) {
    dev = theIDeviceList( filter ) ;
    HQASSERT( dev, "Null device in genericFilterLastError" );
    result = device_error_handler( dev ) ;
  }

  if ( isIOpenFile( filter ))
    (void)genericFilterCloseAbort( filter , TRUE ) ;

  return result ;
}

static Bool genericEncodeBuffer( FILELIST *filter )
{
  register DEVICELIST *dev;
  register int32      count ;
  register int32      nbytes ;
  register uint8      *ptr ;

  count = theICount( filter ) ;
  ptr = theIBuffer( filter ) ;
  dev = theIDeviceList( filter );

  nbytes = (*theIWriteFile( dev ))( dev, theIDescriptor(filter), ptr, count );
  if ( nbytes == -1 ) {
    return device_error_handler( dev );
  }

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  return TRUE ;
}

static Bool genericDecodeBuffer( FILELIST *filter, int32 *ret_bytes )
/* called from fillbuff() */
{
  DEVICELIST *dev;
  uint8      *buff;
  int32      nbytes, len;

  HQTRACE( debug_filters , ( "In genericDecodeBuffer" )) ;

  dev = theIDeviceList( filter );
  HQASSERT(dev, "encountered bad filter");
  buff = theIBuffer( filter );
  len = theIBufferSize( filter );

  nbytes = (*theIReadFile( dev ))( dev, theIDescriptor(filter), buff, len );

  if ( nbytes >= 0 ) {
    *ret_bytes = nbytes ;
    return TRUE;
  }

  return device_error_handler( dev );
}


/* Initialise generic filters */
void generic_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("GENERICEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       genericFilterInit,                    /* initfile */
                       genericFilterClose,                   /* closefile */
                       FilterDispose,                        /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       genericEncodeBuffer,                  /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       genericFilterLastError,               /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void generic_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr,
                       NAME_AND_LENGTH("GENERICDecode") ,
                       FILTER_FLAG | READ_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       genericFilterInit,                    /* initfile */
                       genericFilterClose,                   /* closefile */
                       FilterDispose,                        /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       genericDecodeBuffer ,                 /* filterdecode */
                       genericFilterLastError,               /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


/* Log stripped */
