/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!src:fileiops.c(EBDSDK_P.1) $
 * $Id: src:fileiops.c,v 1.120.1.2.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * File operations at PostScript level; opening, closing, reading, writing.
 */

#include "core.h"
#include "swdevice.h"
#include "swoften.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "devices.h"
#include "devparam.h"
#include "monitor.h"

#include "fileio.h"
#include "fileimpl.h"

/* PaulG 14July1997 - split apart file_internal from file_ so that it
   could be used directly from the external stream code (PDF) */

Bool file_open(OBJECT * fnameobject , int32 openflags ,
               int32 psflags , int32 appendmode ,
               int32 baseflag , OBJECT * result )
{
  corecontext_t *context = get_core_context();
  FILELIST *flptr = NULL ;
  register FILELIST *basefile, *nextfile ;
  register DEVICELIST *dev ;
  DEVICE_FILEDESCRIPTOR d = 0;
  uint8 *device_name , *file_name ;
  uint8 * fname, *fstr ;
  int32 fnamelen, lbflag ;
  Hq32x2  fileOffset;
  device_iterator_t iter ;

  flptr = NULL;     /* init to keep compiler quiet */

  HQASSERT(oType(*fnameobject) == OSTRING, "Filename is not a string") ;

  if (!context->is_interpreter) {
    monitorf((uint8 *) "**** Aborting due to file creation whilst rendering\n");
    return error_handler(UNDEFINED);
  }

  fname = oString(*fnameobject) ;

  fnamelen = theILen( fnameobject ) ;
  if ( fnamelen >= LONGESTFILENAME )
    return detailf_error_handler( LIMITCHECK , "Filename too long (%.*s)" ,
                                  fnamelen , fname ) ;

  switch ( parse_filename(fname, fnamelen, &device_name, &file_name) ) {
  case PARSEERROR:
    return detailf_error_handler( RANGECHECK ,
                                  "Unable to parse filename (%.*s)" ,
                                  fnamelen , fname ) ;
  case JUSTDEVICE :
    if (( dev = find_device( device_name )) != NULL ) {
      if (( ! isDeviceEnabled( dev )) ||
          ( isDeviceRelative( dev ))) /* device must have filename extension */
        return detailf_error_handler( UNDEFINEDFILENAME ,
                                      "Device disabled or not relative (%s)" ,
                                      device_name ) ;

      /* Adding info to the error dict is unlikely to fail, and even
         if it does it's more important to get the bare error message
         logged, so this is a judicious cast away of a return
         value. */
      ( void )object_error_info( &onull , fnameobject ) ;

      if (( d = (*theIOpenFile( dev ))( dev , NULL , openflags )) < 0 )
        return device_error_handler( dev ) ;

      ( void )object_error_info( &onull , &onull ) ;
    } else {
      return file_standard_open(device_name, strlen_int32((char *)device_name),
                                openflags, result) ;
    }

    break ;
  case JUSTFILE :
    /* search through the devices until one succeeds */
    for ( dev = device_first(&iter, DEVICEENABLED|DEVICERELATIVE) ;
          dev ;
          dev = device_next(&iter) ) {
      if ( AllowSearch(context->devicesparams, dev) ) {
        ( void )object_error_info( &onull , fnameobject ) ;
        if (( d = (*theIOpenFile( dev ))( dev , file_name , openflags )) < 0 ) {
          int32 lasterr = (*theILastErr( dev ))( dev ) ;
          if (lasterr != DeviceUndefined)
            return device_error_handler( dev ) ;
        }
        ( void )object_error_info( &onull , &onull ) ;
        if ( d >= 0 )
          break;
      }
    }
    if ( ! dev )
      return detailf_error_handler( UNDEFINEDFILENAME ,
                                    "Not found on any device (%.*s)" ,
                                    fnamelen , fname ) ;

    if ( appendmode ) {
      /* seek to the end of the file */
      Hq32x2FromUint32(&fileOffset, 0u);
      ( void )object_error_info( &onull , fnameobject ) ;
      if (! (*theISeekFile(dev))(dev, d, &fileOffset, SW_XTND))
        return device_error_handler( dev ) ;
      ( void )object_error_info( &onull , &onull ) ;
    }
    break ;

  case DEVICEANDFILE :
    /* Fully specified name */
    if ( NULL == ( dev = find_device( device_name )))
      return detailf_error_handler( UNDEFINEDFILENAME ,
                                    "Could not find device (%s)" ,
                                    device_name ) ;

    if (( ! isDeviceEnabled( dev )) || ( ! isDeviceRelative( dev )))
      return detailf_error_handler( UNDEFINEDFILENAME ,
                                    "Device disabled or not relative (%s)" ,
                                    device_name ) ;

    ( void )object_error_info( &onull , fnameobject ) ;
    if (( d = (*theIOpenFile( dev ))( dev , file_name , openflags )) < 0 )
      return device_error_handler(dev);
    ( void )object_error_info( &onull , &onull ) ;

    if ( appendmode ) {
      /* seek to the end of the file */
      Hq32x2FromUint32(&fileOffset, 0u);
      ( void )object_error_info( &onull , fnameobject ) ;
      if (! (*theISeekFile(dev))(dev, d, &fileOffset, SW_XTND))
        return device_error_handler( dev ) ;
      ( void )object_error_info( &onull , &onull ) ;
    }
    break ;

  case NODEVICEORFILE :
    return detailf_error_handler( UNDEFINEDFILENAME ,
                                  "No device or file found (%.*s)" ,
                                  fnamelen , fname ) ;

  default:
    HQFAIL("unknown return value from parse_filename" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  HQASSERT(dev, "No device for file") ;
  HQASSERT(d >= 0, "Descriptor not opened") ;

  /* It's a real file, so allocate new FILELIST struct */
  nextfile = context->glallocmode ? gvm_filefilters : lvm_filefilters ;
  lbflag = isDeviceLineBuff( dev ) ? LB_FLAG : 0 ;

  /* Find a base file to re-use */
  basefile = NULL ;
  if ( baseflag == BASE_FILE_FLAG ) {
    for ( basefile = nextfile ; basefile ; basefile = basefile->next ) {
      if ( isIBaseFile(basefile) &&
           context->savelevel == theISaveLevel(basefile) &&
           ! isIOpenFile(basefile))
        break ;
    }
  }

  fnamelen = strlen_int32( (char *)file_name ) ;
  if ( basefile ) {
    /* This means it's a base flag (&) and we're re-using a FILELIST structure */

    /* Check reuse of linked FILELIST and prevent dangling pointers [368160].
       FileDispose does not need an error version because it is a void and
       does nothing when there is no buffer. */
    {
      static FILELIST closed_sentinel = {
        tag_FILELIST, 0, 15, (uint8*)"%closedbasefile",
        0, 0, BASE_FILE_FLAG, NULL,
        NULL, 0, NULL, 0, 0,
        FileError,                            /* fillbuff */
        FileFlushBufError,                    /* flushbuff */
        FileInitError,                        /* initfile */
        FileCloseError,                       /* closefile */
        FileDispose,                          /* disposefile */
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
      } ;
      FILELIST * check = lvm_filefilters ;
      int i ;

      for (i = 0 ; i < 2 ; check = gvm_filefilters, ++i) {
        while (check) {
          if (theIUnderFile(check) == basefile)
            theIUnderFile(check) = &closed_sentinel ;
          check = check->next ;
        }
      }
    }

    flptr = basefile ;
    fstr  = theICList( basefile ) ;

    nextfile = basefile->next ;
  } else {
    if ( baseflag ) {
      /* This means it's a base flag (&) and we've got to allocate a new */
      /* FILELIST structure. */
      flptr = (FILELIST *)mm_ps_alloc_typed( mm_pool_ps_typed,
                                             sizeof(FILELIST) + LONGESTFILENAME );
    } else {
      flptr = (FILELIST *)mm_ps_alloc_typed( mm_pool_ps_typed,
                                             sizeof(FILELIST) + fnamelen + 1 ) ;
    }
    if ( flptr == NULL ) {
      /** \todo TODO FIXME Close the file! */
      return error_handler(VMERROR) ;
    }
    fstr = (uint8 *)(flptr + 1) ;
  }
  (void)strcpy( (char *)fstr , (char *)file_name ) ;

  init_filelist_struct(flptr , fstr , fnamelen ,
                       psflags | OPEN_FLAG | REALFILE_FLAG | lbflag | baseflag,
                       d ,
                       NULL , 0 ,
                       psflags & READ_FLAG ? FileFillBuff : NULL ,
                       psflags & WRITE_FLAG ? FileFlushBuff : NULL ,
                       FileInit , FileClose , FileDispose ,
                       psflags == WRITE_FLAG ? NULL : FileBytes ,
                       FileReset , FilePos , FileSetPos , FileFlushFile ,
                       FileEncodeError ,
                       FileDecodeError ,
                       FileLastError ,
                       0 , dev ,
                       NULL , nextfile ) ;

  if ( basefile == NULL ) {
    if ( context->glallocmode )
      gvm_filefilters = flptr ;
    else
      lvm_filefilters = flptr ;
  }

  /* call the file's init routine */
  if ( ! ( *theIMyInitFile( flptr ))( flptr , NULL , NULL ))
    return ( *theIFileLastError( flptr ))( flptr ) ;

  /* Setup the file object */
  file_store_object(result, flptr, LITERAL) ;

  return TRUE ;
}


/* PaulG 14July1997 - split closefile_internal away from closefile_
   so that it could be used directly from the PDF external streams
   routines. */

Bool file_close(register OBJECT *theofile)
{
  register FILELIST *flptr ;

  if (oType(*theofile) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*theofile) ;

  if ( isIOpenFileFilter( theofile , flptr )) {
    if ( isIOutputFile( flptr ))
      if ((*theIMyFlushFile( flptr))( flptr ) == EOF )
        return (*theIFileLastError( flptr ))( flptr ) ;

    if ((*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) == EOF )
      return error_handler( IOERROR ) ;

    /* Rewindable filters do not clear the open flag in their close
       routines. Force it to be cleared now. */
    ClearIOpenFlag( flptr ) ;
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Test if file is seekable */
Bool file_seekable( FILELIST *flptr )
{
  HQASSERT( flptr != NULL , "flptr is null" ) ;
  HQASSERT( isIOpenFile( flptr ) , "flptr has been closed" ) ;

  if ( ! isIInputFile( flptr ) || isIOutputFile( flptr ))
    return FALSE ;

  /* The only time a filter can be seekable is when the filter is
     ResusableStreamDecode. Rewindable filters (from PDF) are
     seekable only to a file position of zero (and are excluded). */

  if ( isIFilter( flptr ) && isIRSDFilter( flptr ))
    return TRUE ;

  /* Now only accept a real file on a seekable device. */

  if ( theIDeviceList( flptr ) != NULL ) {
    DEVICELIST *dev ;
    DEVICE_FILEDESCRIPTOR d ;
    Hq32x2      ext ;

    HQASSERT( theIUnderFile( flptr ) == NULL , "real file has an under file" ) ;

    dev = theIDeviceList( flptr ) ;
    d = theIDescriptor( flptr ) ;

    /* Try to find the file extent; will succeed if seekable. */
    return (*theIBytesFile( dev ))( dev , d , & ext , SW_BYTES_TOTAL_ABS ) ;
  }
  return FALSE ;
}

/* Return stat information for file if it exists. */
Bool file_stat(
/*@in@*/ /*@notnull@*/
  struct OBJECT*  fobject,
/*@out@*/ /*@notnull@*/
  Bool*           exists,
/*@out@*/ /*@notnull@*/
  STAT*           stat)
{
  int32       len;
  uint8*      device_name;
  uint8*      file_name;
  DEVICELIST* dev = NULL;
  device_iterator_t iter;

  HQASSERT((fobject != NULL),
           "file_stat: NULL file object pointer");
  HQASSERT((oType(*fobject) == OSTRING),
           "file_stat: file object is not a string");
  HQASSERT((exists != NULL),
           "file_stat: NULL pointer to returned exists");

  /* Guts ruthlessly pulled from body of status_() as of 2007-07-19 */
  len = theILen(fobject);
  if ( len >= LONGESTFILENAME ) {
    return(error_handler(LIMITCHECK));
  }

  /* Assume file does not exist */
  *exists = FALSE;

  switch ( parse_filename(oString(*fobject), len, &device_name, &file_name) ) {
  case DEVICEANDFILE :
    dev = find_device(device_name);
    if ( (dev == NULL) ||
         (!isDeviceRelative(dev) && !isDeviceEnabled(dev)) ) {
      break;
    }
    if ( (*theIStatusFile(dev))(dev, file_name, stat) == 0 ) {
      *exists = TRUE;
    }
    break;

  case JUSTFILE :
    {
      corecontext_t *context = get_core_context();
      for ( dev = device_first(&iter, DEVICEENABLED|DEVICERELATIVE);
            dev != NULL;
            dev = device_next(&iter) ) {
        if ( AllowSearch(context->devicesparams, dev) ) {
          if ( (*theIStatusFile(dev))(dev, file_name, stat) == 0 ) {
            *exists = TRUE;
            break;
          }
        }
      }
    }
    break;

  case NODEVICEORFILE:
  case JUSTDEVICE:
    break;

  default:
    return(error_handler(LIMITCHECK));
  }

  return TRUE;

} /* file_stat */

int32 file_read(FILELIST *flptr, uint8 *buffer,
                int32 bytes_wanted, int32 *bytes_read)
{
  int32 dummy_read ;

  HQASSERT(flptr, "No file to read from") ;
  HQASSERT(isIInputFile(flptr), "file read called on non-readable file") ;

  /* Remove test every time bytes_read is accessed by pointing at dummy. */
  if ( bytes_read == NULL )
    bytes_read = &dummy_read ;

  *bytes_read = 0 ;

  if ( !isIInputFile(flptr) ) {
    (void)error_handler(INVALIDACCESS) ;
    return 0 ;
  }

  HQASSERT(EOF != TRUE && EOF != FALSE,
           "Value of EOF should not match TRUE or FALSE") ;

  while ( bytes_wanted > 0 ) {
    uint8 *src ;
    int32 count ;

    HQASSERT(buffer, "No buffer to read file into") ;
    if ( !GetFileBuff(flptr, bytes_wanted, &src, &count) ) {
      if ( isIIOError(flptr) ) /* Aborted because of error */
        return 0 ;
      return EOF ;
    }

    HqMemCpy(buffer, src, count) ;

    buffer += count ;
    bytes_wanted -= count ;
    *bytes_read += count ;
  }

  return 1 ;
}


int32 file_skip(
  FILELIST* flptr,
  int32     bytes_to_skip,
  int32*    bytes_skipped)
{
  uint8* src;
  int32  count;
  int32  dummy_skipped;

  HQASSERT(flptr != NULL, "No file to skip") ;
  HQASSERT(bytes_to_skip > 0, "Invalid skip count") ;
  HQASSERT(isIInputFile(flptr), "file skip called on non-readable file") ;

  /* Remove test every time bytes_skipped is accessed by pointing at dummy. */
  if ( bytes_skipped == NULL ) {
    bytes_skipped = &dummy_skipped;
  }

  *bytes_skipped = 0;

  if ( !isIInputFile(flptr) ) {
    (void)error_handler(INVALIDACCESS);
    return(0);
  }

  HQASSERT(EOF != TRUE && EOF != FALSE,
           "Value of EOF should not match TRUE or FALSE") ;

  while ( bytes_to_skip > 0 ) {
    if ( !GetFileBuff(flptr, bytes_to_skip, &src, &count) ) {
      if ( isIIOError(flptr) )  { /* Aborted because of error */
        return(0);
      }
      return(EOF);
    }

    bytes_to_skip -= count;
    *bytes_skipped += count;
  }

  return(1);

} /* file_skip */


Bool file_write(FILELIST *flptr, const uint8 *buffer, int32 bytes_ready)
{
  HQASSERT(flptr, "No file to write to") ;
  HQASSERT(isIOutputFile(flptr), "file write called on non-writable file") ;

  if ( !isIOutputFile(flptr) )
    return error_handler(INVALIDACCESS) ;

  while ( bytes_ready > 0 ) {
    uint8 *dest ;
    int32 count ;

    HQASSERT(buffer, "No buffer to write to file") ;
    if ( !PutFileBuff(flptr, bytes_ready, &dest, &count) )
      return FALSE ;

    HqMemCpy(dest, buffer, count) ;

    buffer += count ;
    bytes_ready -= count ;
  }

  return TRUE ;
}

/* See prototype for usage. */
DEVICELIST* get_device(FILELIST* flptr)
{
  while (flptr->underlying_file != NULL) {
    flptr = flptr->underlying_file;
  }
  return flptr->device;
}

/* Log stripped */
