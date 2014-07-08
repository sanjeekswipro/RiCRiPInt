/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!src:fileio.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * File module initialisation, file functions.
 */


#include "core.h"

#include "coreparams.h"         /* module_params_t */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "swoften.h"            /* public file */
#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "swstart.h"
#include "coreinit.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "gcscan.h"
#include "basemap.h"
#include "namedef_.h"
#include "devices.h"
#include "hqmemset.h"

#include "fileio.h"
#include "fileparam.h"
#include "progress.h"
#include "rsd.h"               /* rsdFilterFree */


/* Stores the standard files & the files' list */
FILELIST *lvm_filefilters = NULL ;
FILELIST *gvm_filefilters = NULL ;

/* Modular fileio system/userparams */
static Bool fileio_set_systemparam(corecontext_t *context,
                                   uint16 name, OBJECT *theo) ;
static Bool fileio_get_systemparam(corecontext_t *context,
                                   uint16 name, OBJECT *result) ;

static NAMETYPEMATCH fileio_system_match[] = {
  { NAME_LowMemRSDPurgeToDisk | OOPTIONAL, 1 , {OBOOLEAN}},
  DUMMY_END_MATCH
} ;

static module_params_t fileio_system_params = {
  fileio_system_match,
  fileio_set_systemparam,
  fileio_get_systemparam,
  NULL
} ;


/** A sentinel closed FILELIST, to avoid dangling pointers. */

static FILELIST closed_sentinel = {
  tag_FILELIST, 0, 11, (uint8*)"%closedfile",
  0, 0, 0, NULL,
  NULL, 0, NULL, 0, 0,
  FileError,                            /* fillbuff */
  FileFlushBufError,                    /* flushbuff */
  FileInitError,                        /* initfile */
  FileCloseError,                       /* closefile */
  FileDispose,                          /* disposefile */
  /* FileDispose does not need an error version because it is a void and does
     nothing when there is no buffer. */
  FileError2,                           /* bytesavail */
  FileError,                            /* resetfile */
  FileError2,                           /* filepos */
  FileError2Const,                      /* setfilepos */
  FileError,                            /* flushfile */
  FileEncodeError,                      /* filterencode */
  FileDecodeError,                      /* filterdecode */
  FilterDecodeInfoError,                /* filterdecodeinfo */
  FileLastError,                        /* lasterror */
  0, 0, NULL, 0, NULL, 0, 0,
  ISNOTVM | ISLOCAL | SAVEMASK,
  OBJECT_NOTVM_NULL, 0
};


/* Routine to initialise the files and filters subsystem. This routine must be
   called before any other routines in this module (not asserted yet). */
static Bool fileio_swstart(SWSTART *params)
{
  corecontext_t *context = get_core_context() ;

  UNUSED_PARAM(SWSTART*, params) ;

  /* Initialise fileio configuration parameters */
  context->fileioparams = mm_alloc_static(sizeof(FILEIOPARAMS)) ;
  if (context->fileioparams == NULL)
    return FALSE ;

  context->fileioparams->LowMemRSDPurgeToDisk = TRUE ;

  HQASSERT(fileio_system_params.next == NULL,
           "Already linked system params accessor") ;

  /* Link accessors into global list */
  fileio_system_params.next = context->systemparamlist ;
  context->systemparamlist = &fileio_system_params ;

  return initReadFileProgress();
}

static void fileio_finish(void)
{
  fileio_restore( -1 ) ;
  termReadFileProgress();
}

static void init_C_globals_fileio(void)
{
  lvm_filefilters = NULL ;
  gvm_filefilters = NULL ;
  fileio_system_params.next = NULL ;
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( lzw )
IMPORT_INIT_C_GLOBALS( progress )

void fileio_C_globals(core_init_fns *fns)
{
  init_C_globals_fileio() ;
  init_C_globals_lzw() ;
  init_C_globals_progress() ;

  fns->swstart = fileio_swstart ;
  fns->finish = fileio_finish ;
}

/* Filelist iterators */
FILELIST *filelist_first(filelist_iterator_h iter, int32 local, int32 global)
{
  HQASSERT(iter, "No filelist iterator") ;

  iter->current = NULL ;
  iter->flags = global ; /* Do we still have the global list to try? */
  if ( local )
    iter->current = lvm_filefilters ;

  return filelist_next(iter) ;
}

FILELIST *filelist_next(filelist_iterator_h iter)
{
  FILELIST *current ;

  HQASSERT(iter, "No filelist iterator") ;

  while ( iter->current == NULL ) {
    if ( !iter->flags ) /* Done with local and global lists */
      return NULL ;
    iter->current = gvm_filefilters ;
    iter->flags = FALSE ;
  }

  current = iter->current ;
  iter->current = current->next ;

  return current ;
}

/* ----------------------------------------------------------------------------
   function:            init_filelist_struct(..)      author:   Luke Tunmer
   creation date:       10-Jun-1991        last modification:   ##-###-####
   arguments:           ( see below )
   description:

   This procedure initialises the flptr with the values given as args.

---------------------------------------------------------------------------- */
void init_filelist_struct( FILELIST *flptr,
                           uint8    *name,
                           int32    len,
                           int32    flags,
                           DEVICE_FILEDESCRIPTOR descriptor,
                           uint8    *buffer,
                           int32    buffsize,
                           FILELIST_FILLBUFF fillbuff,
                           FILELIST_FLUSHBUFF flushbuff,
                           FILELIST_INITFILE initfile,
                           FILELIST_CLOSEFILE closefile,
                           FILELIST_DISPOSEFILE disposefile,
                           FILELIST_BYTESAVAILABLE bytesavail,
                           FILELIST_RESETFILE resetfile,
                           FILELIST_FILEPOSITION filepos,
                           FILELIST_SETFILEPOSITION setfilepos,
                           FILELIST_FLUSHFILE flushfile,
                           FILELIST_FILTERENCODE filterencode,
                           FILELIST_FILTERDECODE filterdecode,
                           FILELIST_LAST_ERROR lasterror,
                           int32    filterstate,
                           DEVICELIST *dev,
                           FILELIST *underfile,
                           FILELIST *next )
{
  corecontext_t *corecontext = get_core_context() ;
  flptr->typetag = tag_FILELIST ;
  theIError( flptr ) = CAST_SIGNED_TO_INT16( NOT_AN_ERROR ) ;
  theICList( flptr ) = name ;
  theINLen( flptr ) = CAST_TO_UINT16(len) ;
  theISaveLevel( flptr ) = corecontext->is_interpreter
    ? CAST_TO_UINT8(corecontext->savelevel | corecontext->glallocmode)
    : ISNOTVM | ISLOCAL | SAVEMASK ;
  theIFlags( flptr ) = flags ;
  theIFilterId( flptr ) = 0 ; /* provisionally */
  theIDescriptor( flptr ) = descriptor ;
  theIBuffer( flptr ) = buffer ;
  theICount( flptr ) = 0 ;
  theIPtr( flptr ) = buffer ;
  theIBufferSize( flptr ) = buffsize ;
  theIFillBuffer( flptr ) = fillbuff ;
  theIFlushBuffer( flptr ) = flushbuff ;
  theIMyInitFile( flptr ) = initfile ;
  theIMyCloseFile( flptr ) = closefile ;
  theIMyDisposeFile( flptr ) = disposefile ;
  theIMyBytesAvailable( flptr ) = bytesavail ;
  theIMyResetFile( flptr ) = resetfile ;
  theIMyFilePos( flptr ) = filepos ;
  theIMySetFilePos( flptr ) = setfilepos ;
  theIMyFlushFile( flptr ) = flushfile ;
  theIFilterEncode( flptr ) = filterencode ;
  theIFilterDecode( flptr ) = filterdecode ;
  theIFilterDecodeInfo( flptr ) = FilterDecodeInfoError ;
  theIFileLastError( flptr ) = lasterror ;
  theIFilterState( flptr ) = filterstate ;
  theIDeviceList( flptr ) = dev ;
  theIFileLineNo( flptr ) = 1 ;
  theIUnderFile( flptr ) = underfile ;
  theIUnderFilterId( flptr ) = 0 ; /* provisionally */
  flptr->next =  next ;
  theIParamDict(flptr) = onull ; /* Struct copy to set slot properties */
  theIPDFContextID( flptr ) = 0 ;
}

/* Create a new object for an existing file, getting the savelevel and flags
   from the filelist. This is similar to the object_store_* routines, but isn't
   in COREobjects because that compound does not depend on COREfileio. */
void file_store_object(OBJECT *fileo, FILELIST *flptr, uint8 litexec)
{
  Bool glallocmode = FALSE ;

  HQASSERT(fileo, "No object to store filelist in") ;
  HQASSERT(flptr, "No filelist for new object") ;
  HQASSERT(litexec == LITERAL || litexec == EXECUTABLE,
           "literal/executable flag has invalid value") ;

  /* PDF filelists are always local, so they cannot be stored in PS global
     dictionaries. */
  if ( (theISaveLevel(flptr) & GLOBMASK) == ISGLOBAL )
    glallocmode = TRUE ;

  theTags(*fileo) = CAST_TO_UINT8(OFILE | litexec |
                                  (isIOutputFile(flptr) ? UNLIMITED : READ_ONLY)) ;
  SETGLOBJECTTO(*fileo, glallocmode) ;
  theLen(*fileo) = theIFilterId(flptr) ;
  oFile(*fileo) = flptr ;
}

/* ps_scan_file -- a scan method for file objects */
mps_res_t MPS_CALL ps_scan_file( size_t *len_out, mps_ss_t scan_state, FILELIST *fl )
{
  mps_res_t res;

  MPS_SCAN_BEGIN( scan_state )
    /* The next pointer is weak, so we don't fix it. */
    /* scan underlying file */
    MPS_RETAIN( &theIUnderFile( fl ), TRUE);
  MPS_SCAN_END( scan_state );
  /* scan param dict */
  res = ps_scan_field( scan_state, &theIParamDict( fl ));
  *len_out = filelist_size( fl );
  return res;
}


/* filelist_size - return the size of the filelist block (not rounded) */
size_t filelist_size( FILELIST *fl )
{
  /* The names of real files are allocated in the same block. */
  HQASSERT( ((theIFlags(fl) & FILTER_FLAG) || (theIFlags(fl) & STDFILE_FLAG)) || theICList(fl) == (uint8 *)(fl + 1),
            "Name not contiguous w/file" );
  return sizeof( FILELIST ) +
            ((( theIFlags(fl) & FILTER_FLAG ) || (theIFlags(fl) & STDFILE_FLAG))
              ? 0
              : (( theIFlags(fl) & BASE_FILE_FLAG )
                  ? LONGESTFILENAME
                  : ( theINLen(fl) + 1 )));
}


/*-------------------------------------------------
 *
 * Routines for FILELIST structure for real files.
 *
 *-------------------------------------------------
*/
int32 FileError( FILELIST *flptr )
{
  UNUSED_PARAM( FILELIST *, flptr );

  HQASSERT( ! isIFilter(flptr), "FileError called on filter");
  return  EOF  ;
}

Bool FileEncodeError( FILELIST *filter )
{
  UNUSED_PARAM( FILELIST *, filter );

  HQASSERT( !isIFilter(filter), "FileEncodeError called on filter" ) ;

  return FALSE ;
}

Bool FileDecodeError( FILELIST *filter, int32* pb )
{
  UNUSED_PARAM( FILELIST *, filter );
  UNUSED_PARAM( int32*, pb );

  HQASSERT( !isIFilter(filter), "FileDecodeError called on filter" ) ;

  return FALSE ;
}

int32 FileFlushBufError( int32 c, FILELIST *flptr )
{
  UNUSED_PARAM( int32, c );
  UNUSED_PARAM( FILELIST *, flptr );

  HQASSERT( ! isIFilter(flptr), "FileFlushBufError called on filter");
  return  EOF  ;
}

Bool FileInitError( FILELIST *flptr, OBJECT* args, STACK* stack )
{
  UNUSED_PARAM( FILELIST *, flptr );
  UNUSED_PARAM( OBJECT*, args );
  UNUSED_PARAM( STACK*, stack );

  HQASSERT( ! isIFilter(flptr), "FileInitFileError called on filter");
  return FALSE  ;
}

int32 FileError2(FILELIST *flptr, Hq32x2* n)
{
  UNUSED_PARAM( FILELIST *, flptr );
  UNUSED_PARAM(Hq32x2 *, n);

  HQASSERT( ! isIFilter(flptr), "FileError2 called on filter");
  return  EOF  ;
}

int32 FileError2Const(FILELIST *flptr, const Hq32x2* n)
{
  UNUSED_PARAM( FILELIST *, flptr );
  UNUSED_PARAM(const Hq32x2 *, n);

  HQASSERT( ! isIFilter(flptr), "FileError2Const called on filter");
  return  EOF  ;
}

int32 FileCloseError( FILELIST *flptr, int32 flag )
{
  UNUSED_PARAM( FILELIST *, flptr );
  UNUSED_PARAM(int32, flag);

  HQASSERT( ! isIFilter(flptr), "FileCloseError called on filter");
  return  EOF  ;
}

static uint8 BufferNotAllocated[1] = {0};

/* Lazy file buffer allocation (the old FileInit)... [sab] */
Bool LazyFileInit( FILELIST *flptr )
{
  DEVICELIST *dev ;
  int32 bufflength ;

  HQASSERT( ! isIFilter(flptr), "LazyFileInit called on filter");
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "initialising bad file");

  bufflength = -1 ;
  if ( theIGetBuffSize( dev ))
    bufflength = (*theIGetBuffSize( dev ))( dev ) ;
  if ( bufflength < 0 ) {
    if ( isDeviceSmallBuff( dev ))
      /* For hysterical raisins this flag in the Flags field of the device
       * struct indicates whether a smaller buffer should be used
       */
      bufflength = FONTFILEBUFFSIZE ;
    else
      bufflength = FILEBUFFSIZE ;
  }
  if ( bufflength != 0 ) {
    if ( NULL == ( theIBuffer( flptr ) = (uint8 *)
            mm_alloc(mm_pool_temp, bufflength, MM_ALLOC_CLASS_FILE_BUFFER))) {
      theIBuffer( flptr ) = BufferNotAllocated;
      return error_handler( VMERROR );
    }
  } else {
    theIBuffer( flptr ) = NULL;
  }

  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theIBufferSize( flptr ) = bufflength ;

  return TRUE ;
}

Bool perhaps_lazy(FILELIST *flptr)
{
  /* Lazy buffer allocation... [sab] */
  if ( theIBuffer( flptr ) == BufferNotAllocated ) {
    if ( !LazyFileInit( flptr ) )
      return FALSE;
  }
  return TRUE;
}

int32 FileFillBuff( FILELIST *flptr )
/* flptr must be an input file */
{
  DEVICELIST *dev ;

  HQASSERT( ! isIFilter(flptr), "FileFillBuff called on filter");

  if ( isIEof( flptr ))
    return EOF ;
  dev = theIDeviceList(flptr);
  HQASSERT(dev && isIOpenFile(flptr), "reading from closed file");

  if ( !perhaps_lazy(flptr) )
    return EOF;

  if ( ( theICount( flptr )  =
        (*theIReadFile( dev ))( dev , theIDescriptor( flptr ) ,
                               theIBuffer( flptr ) ,
                               theIBufferSize( flptr ))) <= 0 ) {
    if ( theICount( flptr ) < 0 ) {
      theIReadSize(flptr) = 0;
      return ioerror_handler( flptr ) ;
    }
    /* Postscript closes files after reaching EOF */
    SetIEofFlag( flptr ) ;

    /* Set the readsize to 0 since we hit EOF */
    theIReadSize(flptr) = 0;
    return EOF ;
  }
  SetIDoneFillFlag( flptr ) ;
  /* update the readsize to that of this buffer */
  theIReadSize(flptr) = theICount(flptr);
  theICount( flptr )-- ;
  theIPtr( flptr ) = theIBuffer( flptr ) + 1;
  return ( int32 ) *theIBuffer( flptr ) ;
}

int32 FileFlushBuff( int32 c, FILELIST *flptr )
/* flptr must be an output file */
{
  DEVICELIST *dev ;

  /* We may have been called from Putc as a side effect of lazy buffer
     allocation. If so, don't flush.
   */
  if ( theIBuffer( flptr ) == BufferNotAllocated ) {
    if ( !LazyFileInit( flptr ) )
      return EOF;
    /**
     * We don't know here whether we were called from Putc or TPutc, but
     * there's only a difference if c==LF for line-buffered files. Flushing
     * a line feed as the first character output would seem unlikely to be
     * critical, so we'll do what TPutc does.
     *
     * Problem is that FlushBuff used to be just called when the buffer was
     * full in some way. But now with Lazy allocation, it can also get called
     * initially to create the buffer. And in this case, we do not have enough
     * context to know whether we should be doing a real flush or not. Very
     * unlikely to actually cause a problem, but architecturally wrong. Remove
     * the assert as we know exactly when it fires (jobs starting with
     * "(LF) print" ). Leave resolution of the problem to planned review of
     * Lazy File Buffering.
     *
     * \todo BMJ 27-Feb-14 : Review issues with lazy-file buffering and
     *                       line buffering.
     */
    theICount( flptr ) = 1 ;
    *theIPtr( flptr )++ = ( uint8 ) c ;
    return c ;
  }

  dev = theIDeviceList(flptr);
  HQASSERT( ! isIFilter(flptr), "FileFlushBuff called on filter");
  HQASSERT(dev && isIOpenFile(flptr), "flushing closed file");
  HQASSERT(theIPtr( flptr ) - theIBuffer( flptr ) < theIBufferSize( flptr ) ,
           "Walked off end of file buffer" );
  *theIPtr( flptr )++ = ( uint8 ) c ;
  if ( (*theIWriteFile( dev ))( dev , theIDescriptor( flptr ) ,
                               theIBuffer( flptr ),
                               theICount( flptr )) != theICount( flptr ))
    return EOF ;
  ClearIDoneFillFlag( flptr ) ;
  theICount( flptr ) = 0 ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  return 0 ;
}

Bool FileInit( FILELIST *flptr ,
               OBJECT *args ,
               STACK *stack )
{
  DEVICELIST *dev ;

  UNUSED_PARAM( OBJECT * , args );
  UNUSED_PARAM( STACK * , stack );

  HQASSERT( ! isIFilter(flptr), "FileInit called on filter");
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "initialising bad file");

  /* With lazy fileio buffer allocation, do not alloc
   * the buffer now. but when we first use it.
   */
  theIBuffer( flptr ) = BufferNotAllocated ;
  theIPtr( flptr ) = 0 ;
  theIBufferSize( flptr ) = 0 ;
  theICount( flptr ) = 0 ;

  return TRUE;
}

int32 FileClose( FILELIST *flptr, int32 flag )
/* either input or output file */
{
  int32 nodeverror ;
  DEVICELIST *dev ;

  UNUSED_PARAM(int32, flag);

  HQASSERT( ! isIFilter(flptr), "FileClose called on filter");
  dev = theIDeviceList(flptr);
  HQASSERT(dev && isIOpenFile(flptr), "closing closed file");

  nodeverror = closeReadFileProgress( flptr ) ;

  ClearIOpenFlag( flptr ) ;

  (*theIMyDisposeFile(flptr))(flptr) ;

  if ( (*theICloseFile( dev ))( dev , theIDescriptor( flptr )) == EOF )
    return EOF ;

  return ( nodeverror ? 0 : EOF ) ;
}

void FileDispose( FILELIST *flptr )
/* either input or output file */
{
  HQASSERT( flptr , "flptr NULL in FileDispose." ) ;

  if (theIBuffer( flptr ) && theIBuffer( flptr ) != BufferNotAllocated ) {
    mm_free(mm_pool_temp, (mm_addr_t)theIBuffer(flptr),
            theIBufferSize(flptr));
    theIBuffer( flptr ) = NULL ;
  }
}

Bool FileLastError( FILELIST *flptr )
{
  DEVICELIST * dev ;
  int32 result ;

  dev = theIDeviceList( flptr ) ;
  HQASSERT(dev, "encountered bad file" ) ;
  HQASSERT( ! isIFilter( flptr ) , "FileLastError called on filter" ) ;

  result = device_error_handler( dev ) ;

  ( void )closeReadFileProgress( flptr ) ;

  ClearIOpenFlag( flptr ) ;
  SetIEofFlag( flptr ) ;

  (*theIMyDisposeFile(flptr))(flptr) ;

  ( void )( * theIAbortFile( dev ))( dev , theIDescriptor( flptr )) ;

  return result ;
}

int32 FileBytes( FILELIST *flptr, Hq32x2* avail )
/* must be an input file */
{
  DEVICE_FILEDESCRIPTOR d;
  DEVICELIST * dev ;

  HQASSERT((avail != NULL),
           "FileBytes: NULL bytecount pointer.");

  HQASSERT( ! isIFilter(flptr), "FileBytes called on filter");
  if ( isIEof( flptr )) {
    return -1 ;
  }
  if (( isIReadWriteFile( flptr )) && ( ! isIDoneFill( flptr )) &&
      ( theICount( flptr ) > 0 ))
    /* a RW file which was last written to */
    return -1 ; /* !!! EOF? */
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "encountered bad file");
  d = theIDescriptor( flptr ) ;
  if (! (*theIBytesFile(dev))(dev, d, avail, SW_BYTES_AVAIL_REL))
    return EOF;
  Hq32x2AddInt32(avail, avail, theICount(flptr));
  /* Checks for allowable byte counts (i.e. <=2GB for bytesavailable operator)
   * is now the responsibility of the caller.
   */
  return 0 ;
}


int32 FileReset( FILELIST *flptr )
{
  HQASSERT( ! isIFilter(flptr), "FileReset called on filter");
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  ClearIEofFlag(flptr) ;
  return  0  ;
}


int32 FilePos( FILELIST *flptr, Hq32x2* file_pos )
{
  DEVICELIST *dev ;

  HQASSERT((file_pos != NULL),
           "FilePos: NULL position pointer.");
  HQASSERT( ! isIFilter(flptr), "FilePos called on filter");

  Hq32x2FromUint32(file_pos, 0u);
  if ( isIEof(flptr) ) {
    return 0 ;
  }
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "encountered bad file");

  if (! (*theISeekFile(dev))(dev, theIDescriptor(flptr), file_pos, SW_INCR))
    return EOF ;

  if ( isIReadWriteFile( flptr )) {
    if ( isIDoneFill( flptr ))    /* we last did a read */
      Hq32x2SubtractInt32(file_pos, file_pos, theICount(flptr));
    else
      Hq32x2AddInt32(file_pos, file_pos, theICount(flptr));
  } else if ( isIInputFile( flptr )) {
    Hq32x2SubtractInt32(file_pos, file_pos, theICount(flptr));
  } else
    /* output file */
    Hq32x2AddInt32(file_pos, file_pos, theICount(flptr));

  return 0 ;
}


int32 FileSetPos(FILELIST *flptr, const Hq32x2* offset)
{
  DEVICELIST * dev;
  DEVICE_FILEDESCRIPTOR d;
  Hq32x2                filePos,startPos;

  HQASSERT((offset != NULL),
           "FileSetPos: NULL offset pointer.");

  HQASSERT( ! isIFilter(flptr), "FileSetPos called on filter");
  dev = theIDeviceList(flptr);
  HQASSERT(dev && isIOpenFile(flptr), "encountered bad file");
  d = theIDescriptor( flptr );

  /* get the existing size of the file */
  if ( !(*theIBytesFile(dev))(dev, d, &filePos, SW_BYTES_TOTAL_ABS) )
    return EOF;

  if ( Hq32x2Compare(offset, &filePos) > 0 ) {
    /* If offset beyond end then extension is required */
    int32       chunk, res, maxsize;
    uint32      sema ;
    void       *zero ;
    Hq32x2  extra;

    /* we need to do a reset before the seek, in case the calling code didn't do one */
    if ( (*theIMyResetFile(flptr))(flptr) == EOF )
      return EOF;

    /* First seek to the end, use filePos in case changed in between */
    if ( !(*theISeekFile(dev))(dev, d, &filePos, SW_SET) )
      return EOF;

    /* Find out how much extra space needed */
    Hq32x2Subtract(&extra, offset, &filePos);

    if ( (sema = get_basemap_semaphore(&zero, (uint32 *)&maxsize)) == 0 )
      return EOF ;

    HQASSERT(maxsize > 0, "Cannot cope with basemap size > 2GB") ;

    /* Clear out as much of basemap1 as reqd */
    if ( Hq32x2CompareInt32(&extra, maxsize) >= 0 ) {
      chunk = maxsize;
    } else {
      res = Hq32x2ToInt32(&extra, &chunk);
      HQASSERT((res),
               "FileSetPos: partial basemap1 clear > 2GB");
    }
    HqMemZero(zero, chunk);

    /* Write whole basemap1's worth of file data */
    while ( Hq32x2CompareInt32(&extra, maxsize) >= 0 ) {
      if ( maxsize != (*theIWriteFile(dev))(dev, d, zero, maxsize) ) {
        free_basemap_semaphore(sema) ;
        return EOF ;
      }
      Hq32x2SubtractInt32(&extra, &extra, maxsize);
    }

    /* Handle last partial basemap1 worth of file data */
    if ( Hq32x2CompareInt32(&extra, 0) > 0 ) {
      res = Hq32x2ToInt32(&extra, &chunk);
      HQASSERT((res),
               "FileSetPos: final write > 2GB");
      if ( chunk != (*theIWriteFile(dev))(dev, d, zero, chunk) ) {
        free_basemap_semaphore(sema) ;
        return EOF ;
      }
    }

    free_basemap_semaphore(sema) ;
  }
  else {
    /* check if we have an input file or we have a r/w file that we have just read from */
    if ((isIReadWriteFile( flptr ) && isIDoneFill( flptr )) || isIInputFile( flptr )) {
      if ( flptr->count > 0) { /* if we havn't already done a reset, note: this means that
                              if we have read to the end of the buffer, and
                             therefore count is zero, this test means we won't optimize
                             the seek */
        if (flptr->readsize > 0) {  /* if we have anything in the buffer */
          Hq32x2FromInt32(&filePos, 0);
          /* find current location on the underlying file */
          if (! (*theISeekFile(dev))(dev, theIDescriptor(flptr), &filePos, SW_INCR))
              return EOF ;
          /* check we are not seeking beyond current position - 1 */
          if ( Hq32x2Compare(offset, &filePos) < 0 ) {
            Hq32x2SubtractInt32(&startPos, &filePos, flptr->readsize);
            /* check if we are seeking somewhere within the buffer we have */
            if ( Hq32x2Compare(offset, &startPos) >= 0 ) {
              Hq32x2  result;
              Hq32x2Subtract(&result, offset, &startPos);
              HQASSERT(result.high == 0, "Cannot seek > 32 bit offset") ; /* we are using signed hq32's here */
              /* adjust pointer and count to new location */
              flptr->ptr = flptr->buffer + result.low;
              flptr->count = flptr->readsize - result.low;
              ClearIEofFlag(flptr) ;    /* we do this because a FileReset would have done this */
              return 0;
            }
          }
        }
      }
    }
    /* we need to do a reset before the seek, in case the calling code didn't do one */
    if ( (*theIMyResetFile(flptr))(flptr) == EOF )
      return EOF;

    {
      Hq32x2 devoff = *offset ;
      /* Use a copy of the offset, because the device interface seek is not
         (and can not be) constified. */
      if ( !(*theISeekFile(dev))(dev, d, &devoff, SW_SET) )
        return EOF ;
    }
  }

  /* All done! Current position is at the end */
  return 0 ;
}


int32 FileFlushFile( FILELIST *flptr )
{
  int32 bytes ;
  DEVICELIST *dev ;
  Hq32x2   filePos;

  HQASSERT( ! isIFilter(flptr), "FileFlushFile called on filter");
  if (( isIReadWriteFile( flptr )) && ( isIDoneFill( flptr ))) {
    /* this RW file was last read from, so throw buffer away */
    ClearIDoneFillFlag( flptr ) ;
    theICount( flptr ) = 0 ;
    theIPtr( flptr ) = theIBuffer( flptr ) ;
    return 0 ;
  }
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "encountered bad file");
  if ( isIOutputFile( flptr )) {
    if ( theICount( flptr )) {
      bytes = (*theIWriteFile( dev ))( dev , theIDescriptor( flptr ) ,
                                      theIBuffer( flptr ),
                                      theICount( flptr )) ;
      if ( bytes != theICount( flptr ))
        return EOF ;
    }
  } else {
    SetIEofFlag( flptr ) ;
    Hq32x2FromUint32(&filePos, 0u);
    if (! (*theISeekFile(dev))(dev, theIDescriptor(flptr),
                               &filePos, SW_XTND))
      return EOF ;
  }
  ClearIDoneFillFlag( flptr ) ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  return 0 ;
}


/* --------------------------------------------------------------------
 *
 *  Allow direct access to a file's buffer, for efficiency.
 *  Returns FALSE if no bytes were available (i.e. EOF was found,
 *  or there was an error); TRUE if something valid is in the buffer.
 *
 * --------------------------------------------------------------------
 */

Bool GetFileBuff( FILELIST *flptr, int32 max_bytes_wanted, uint8 **return_ptr, int32 *bytes_returned )
{
  int32 count ;

  HQASSERT( max_bytes_wanted > 0, "Max bytes wanted should be > 0");

  if ( ! EnsureNotEmptyFileBuff( flptr ) ) {
    *bytes_returned = 0 ;
    return FALSE ;
  }

  count = max_bytes_wanted ;
  if (theICount( flptr ) < count)
    count = theICount( flptr ) ;
  *return_ptr = theIPtr( flptr ) ;
  *bytes_returned = count ;
  theICount( flptr ) -= count ;
  theIPtr( flptr ) += count ;
  return TRUE ;
}

/* EnsureNotEmptyFileBuff
 * ----------------------
 * If buffer is empty, attempt to fill it.
 * Does not change the file-position.
 * Returns TRUE iff buffer is then not empty.
 */
Bool EnsureNotEmptyFileBuff( FILELIST *flptr )
{
  if ( !perhaps_lazy(flptr) )
    return FALSE;

  if ( theICount( flptr ) <= 0 ) {
    if ( GetNextBuf( flptr ) == EOF ) {
      return FALSE ;
    }
    HQASSERT( theICount( flptr ) >= 0 , "EnsureNotEmptyFileBuff: bad Count" ) ;

    /* GetNextBuf fills, and then gets the first character; so, unget it */
    theICount( flptr ) += 1 ;
    theIPtr( flptr ) -= 1 ;
  }
  return TRUE ;
}

/* The same thing as GetFileBuff, but for writing. */

Bool PutFileBuff( FILELIST *flptr , int32 max_bytes_wanted ,
                  uint8 **return_ptr , int32 *bytes_returned )
{
  int32 count = max_bytes_wanted ;

  if ( !perhaps_lazy(flptr) )
    return FALSE;

  HQASSERT( flptr , "flptr NULL in PutFileBuff." ) ;
  HQASSERT( max_bytes_wanted > 0 , "Max bytes wanted should be > 0" ) ;
  HQASSERT( return_ptr , "return_ptr NULL in PutFileBuff." ) ;
  HQASSERT( bytes_returned , "bytes_returned NULL in PutFileBuff." ) ;

  count = theIBufferSize( flptr ) - theICount( flptr ) ;

  /* There must always be room for at least 1 byte (see
     FileFlushBuff) */
  if ( theIBufferSize( flptr ) - theICount( flptr ) <= max_bytes_wanted )
    if (( *theIMyFlushFile( flptr ))( flptr ) == EOF)
      return FALSE ;

  *return_ptr = theIPtr( flptr ) ;
  if ( count > max_bytes_wanted )
    count = max_bytes_wanted ;
  *bytes_returned = count ;
  theICount( flptr ) += count ;
  theIPtr( flptr ) += count ;

  return TRUE ;
}


/* Check the underlying device of files/filters, and disconnect if the file is
   closed. Fail if any open files remain on this device. This is typically
   used by the device_closing() hook to determine if a device can be closed. */
Bool fileio_check_device(DEVICELIST *dev)
{
  FILELIST *flptr ;
  filelist_iterator_t iter ;

  for ( flptr = filelist_first(&iter, TRUE, TRUE) ; /* Local and global */
        flptr ;
        flptr = filelist_next(&iter) ) {
    if (theIDeviceList(flptr) == dev) {
      /* check to see if it's a closed base file, in which case
       * disconnect the device from the flptr.
       */
      if (isIBaseFile(flptr) &&
          !isIOpenFile(flptr)) {
        theIDeviceList(flptr) = NULL;
      } else {
        return error_handler(INVALIDACCESS) ;
      }
    }
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            fileio_restore(..) author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           s_id .
   description:

   This procedure closes any files that were created after the save that is
   being restored.  Also, any links to these files are removed.

---------------------------------------------------------------------------- */
void fileio_restore( int32 value )
{
  FILELIST *curr ;
  FILELIST **prev ;
  FILELIST **next = NULL ;
  error_context_t *err_ctx = get_core_context_interp()->error;
  Bool in_error = error_signalled_context(err_ctx);
  int32 saved_error = FALSE;

  value = NUMBERSAVES(value) ;
  if ( value <= MAXGLOBALSAVELEVEL )
    next = &gvm_filefilters ;

  if ( in_error )
    error_save_context(err_ctx, &saved_error);
  /* Since this modifies the filelist, it cannot use the filelist iterators.
     Iterate over the local filelist, and the global one only if the save
     level is low enough. */
  for ( prev = &lvm_filefilters ; prev != NULL ; prev = next, next = NULL ) {
    while (( curr = *prev ) != NULL ) {
      /* If file was created after the corresponding "save", remove it. */
      if ( NUMBERSAVES(theISaveLevel(curr)) > value ) {
        /* we don't need to worry about dead filters here: all previous uses
           of the file object will have been closed and if this one is open
           we want to close it whether or not it was previously reused. In
           other words, we don't care about any relationships with PS objects
           here */
        if ( isIOpenFile( curr )) {
          Bool res = TRUE;

          if ( isIOutputFile( curr ))
            res = (*theIMyFlushFile( curr ))( curr ) != EOF;
          res = (*theIMyCloseFile( curr ))( curr, CLOSE_FORCE ) != EOF && res;
          if ( !res )
            error_clear_context(err_ctx);
        }
        *prev = curr->next ;
      }
      else {
        prev = & curr->next ;
      }
    }
  }
  if ( in_error )
    error_restore_context(err_ctx, saved_error);
  /* Do another pass to remove any underlying files that might be left dangling
   * after the restore in the event that the underlying file has a longer
   * lifetime.
   * Care has to be taken with global objects. We need to walk over the global
   * list in case the underlying file is local and about to be restored. Of
   * course, we don't want to break the link if the underlying file was global.
   */
  next = &gvm_filefilters;
  for ( prev = &lvm_filefilters ; prev != NULL ; prev = next, next = NULL ) {
    while (( curr = *prev ) != NULL ) {
      FILELIST *under = curr->underlying_file ;

      if ( under != NULL && NUMBERSAVES(theISaveLevel(under)) > value
           && (under->sid & GLOBMASK) == ISLOCAL ) {
        HQASSERT(under->sid <= (SAVEMASK | ISGLOBAL), "Underlying file corrupted; perhaps deallocated.");
        HQASSERT(!isIOpenFile( under ), "underlying file should have been closed");
        curr->underlying_file = NULL;
      }
      prev = & curr->next ;
    }
  }
}

/**
 * \param pdfContextId  Id of context being purged.
 * \param purged  If not \c NULL, only purging this file.
 *
 * When a PDF execution context is destroyed (with all its files) or a PDF
 * stream is purged, any Postscript filters which overlay those files or filters
 * will have dangling references to deallocated memory.
 *
 * This method should be called when that is about to happen. It scans the local
 * and global filelists for FILELISTs with underlying files which belong to the
 * specified set. Any such FILELISTS are closed and have their underlying file
 * pointer nulled to remove all references to the soon-to-be-deallocated memory.
 */
void fileio_close_pdf_filters(int32 pdfContextId, FILELIST *purged)
{
  FILELIST *curr ;
  FILELIST **prev ;
  FILELIST **next = &gvm_filefilters ;

  for ( prev = &lvm_filefilters ; prev != NULL ; prev = next, next = NULL ) {
    while (( curr = *prev ) != NULL ) {
      FILELIST* underlying = theIUnderFile(curr);
      HQASSERT(theIPDFContextID(curr) != pdfContextId,
               "PDF FILELIST object found on Postscript file list.");

      if ( underlying != NULL && theIPDFContextID(underlying) == pdfContextId
           && (purged == NULL || underlying == purged) ) {
        /* This filter overlays a PDF filter; close it. */
        if (isIOpenFile(curr)) {
          if (isIOutputFile(curr))
            (void)(*theIMyFlushFile(curr))(curr);
          (void)(*theIMyCloseFile(curr))(curr, CLOSE_FORCE);
        }
        theIUnderFile(curr) = NULL;
        /* Will be unlinked by restore/GC. */
      }
      else {
        prev = & curr->next ;
      }
    }
  }
}


/* fileio_finalize - finalize a FILELIST: close it and unlink it
 *
 * Compare this to fileio_restore.
 */

void fileio_finalize(FILELIST *obj)
{
  FILELIST *curr;
  FILELIST **prev;
  FILELIST **next = &gvm_filefilters;
  Bool found = FALSE;

  if ( isIOpenFile( obj )) {
    if ( isIOutputFile( obj ))
      (void)(*theIMyFlushFile( obj ))( obj );
    (void)(*theIMyCloseFile( obj ))( obj, CLOSE_FORCE );
  }

  for ( prev = &lvm_filefilters ; prev ; prev = next, next = NULL ) {
    while (( curr = *prev ) != NULL ) {
      if ( curr == obj ) {
        *prev = curr->next; /* unlink */
        found = TRUE;

      } else if (theIUnderFile(curr) == obj) {
        /* Prevent underlying file pointer dangling too [368160] */
        theIUnderFile(curr) = &closed_sentinel;
      }
      prev = &curr->next;
    }
  }
  HQASSERT( found, "Couldn't unlink finalized file" );
}

/*---------------------------------------------------------------------------*/
static Bool fileio_set_systemparam(corecontext_t *context,
                                   uint16 name, OBJECT *theo)
{
  FILEIOPARAMS *fileioparams = context->fileioparams ;

  HQASSERT((theo && name < NAMES_COUNTED) ||
           (!theo && name == NAMES_COUNTED),
           "name and parameter object inconsistent") ;

  switch ( name ) {
  case NAME_LowMemRSDPurgeToDisk:
    fileioparams->LowMemRSDPurgeToDisk = (int8)oBool(*theo) ;
    break ;
  }

  return TRUE ;
}

static Bool fileio_get_systemparam(corecontext_t *context,
                                   uint16 name, OBJECT *result)
{
  FILEIOPARAMS *fileioparams = context->fileioparams ;

  HQASSERT(result, "No object for systemparam result") ;

  switch ( name ) {
  case NAME_LowMemRSDPurgeToDisk:
    object_store_bool(result, fileioparams->LowMemRSDPurgeToDisk) ;
    break;
  }

  return TRUE ;
}

/*---------------------------------------------------------------------------*/

#if defined(DEBUG_BUILD)
#include "debugging.h"
#include "monitor.h"

static void debug_print_fileflags(uint32 flags)
{
  uint32 i ;

  static char *flagnames[] = { /* 32 flags, low bit to high bit */
    "EOF",
    "READ",
    "WRITE",
    "OPEN",
    "STD",
    "EDIT",
    "REAL",
    "FILTER",
    "LB",
    "IOERROR",
    "TIMEOUT",
    "REWINDABLE",
    "FILLED",
    "GOTCR",
    "SKIPLF",
    "BASE",
    "RSD",
    "DELIMIT",
    "EXPAND",
    "THRESHOLD",
    "CLOSING",
    "CST",
    "0x400000",
    "0x800000",
    "0x1000000",
    "0x2000000",
    "0x4000000",
    "0x8000000",
    "0x10000000",
    "0x20000000",
    "0x40000000",
    "FGC"
  } ;

  for ( i = 0 ; flags ; ++i ) {
    if ( (flags & (1u << i)) != 0 ) {
      monitorf((uint8 *)flagnames[i]) ;
      if ( (flags &= ~(1u << i)) != 0 )
        monitorf((uint8 *)",") ;
    }
  }
}

void debug_print_file(FILELIST *flptr)
{
  FILELIST *uflptr ;
  uint32 uflcount = 0 ;

  monitorf((uint8 *)"FILELIST %p (%.*s)\n", flptr, flptr->len, flptr->clist) ;
  monitorf((uint8 *)" Flags ") ;
  debug_print_fileflags(theIFlags(flptr)) ;
  monitorf((uint8 *)"\n") ;
  monitorf((uint8 *)" Filter id %d\n", theIFilterId(flptr)) ;
  if ( theIDeviceList(flptr) != NULL ) {
    monitorf((uint8 *)" Device %s\n", theIDeviceList(flptr)->name) ;
    monitorf((uint8 *)" Descriptor %d\n", theIDescriptor(flptr)) ;
  } else
    monitorf((uint8 *)" Filter private %p\n", theIFilterPrivate(flptr)) ;
  monitorf((uint8 *)" Filter state ") ;
  switch ( theIFilterState(flptr) ) {
  case FILTER_INIT_STATE:
    monitorf((uint8 *)"FILTER_INIT_STATE") ;
    break ;
  case FILTER_EMPTY_STATE:
    monitorf((uint8 *)"FILTER_EMPTY_STATE") ;
    break ;
  case FILTER_LASTCHAR_STATE:
    monitorf((uint8 *)"FILTER_LASTCHAR_STATE") ;
    break ;
  case FILTER_EOF_STATE:
    monitorf((uint8 *)"FILTER_EOF_STATE") ;
    break ;
  case FILTER_ERR_STATE:
    monitorf((uint8 *)"FILTER_ERR_STATE") ;
    break ;
  default:
    monitorf((uint8 *)"Unknown (%d)", theIFilterState(flptr)) ;
    break ;
  }
  monitorf((uint8 *)"\n") ;
  monitorf((uint8 *)" Buffer %x, ptr %x (diff %d)\n",
           theIBuffer(flptr), theIPtr(flptr), theIPtr(flptr) - theIBuffer(flptr)) ;
  monitorf((uint8 *)" Buffer size %d, count %d, readsize %d\n",
           theIBufferSize(flptr), theICount(flptr), theIReadSize(flptr)) ;
  monitorf((uint8 *)" Line %d\n", theIFileLineNo(flptr)) ;
  monitorf((uint8 *)" Save level %d, %s\n",
           NUMBERSAVES(theISaveLevel(flptr)),
           (theISaveLevel(flptr) & GLOBMASK) == ISGLOBAL ? "global" : "local") ;
  monitorf((uint8 *)" PDF context id %d\n", theIPDFContextID(flptr)) ;

  if ( isIFilter(flptr) ) {
    monitorf((uint8 *)(isprint(theILastChar(flptr)) ?
                       " Last char '%c'\n" : " Last char 0x%x\n"),
             theILastChar(flptr)) ;
    debug_print_object_indented(&theIParamDict(flptr), " Filter params ", "\n", NULL) ;
  }

  while ( (uflptr = theIUnderFile(flptr)) != NULL ) {
    uint32 i ;

    for ( i = 0 ; i < uflcount ; ++i )
      monitorf((uint8 *)" ") ;
    monitorf((uint8 *)" Underlying filter id %d\n", theIUnderFilterId(flptr)) ;

    for ( i = 0 ; i < uflcount ; ++i )
      monitorf((uint8 *)" ") ;
    monitorf((uint8 *)" Underlying file %p (%.*s) id %d\n",
             uflptr, uflptr->len, uflptr->clist, theIFilterId(uflptr)) ;

    if ( theIFilterId(uflptr) != theIUnderFilterId(flptr) )
      break ; /* underlying closed */

    for ( i = 0 ; i < uflcount ; ++i )
      monitorf((uint8 *)" ") ;
    monitorf((uint8 *)" Flags ") ;

    debug_print_fileflags(theIFlags(uflptr)) ;
    monitorf((uint8 *)"\n") ;

    ++uflcount ;
    flptr = uflptr ;
  }
}

void debug_print_filelist(FILELIST *flptr)
{
  while ( flptr ) {
    debug_print_file(flptr) ;
    flptr = flptr->next ;
  }
}

void debug_print_psfiles(void)
{
  monitorf((uint8 *)"Local filelists\n") ;
  debug_print_filelist(lvm_filefilters) ;

  monitorf((uint8 *)"Global filelists\n") ;
  debug_print_filelist(gvm_filefilters) ;
}
#endif

/*
Log stripped */
