/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:swzipreaddev.c(EBDSDK_P.1) $
 * SW ZIP device
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of the SW ZIP read device type.
 *
 * If an SW ZIP read device is added to the device list which is
 * passed to SwStart, the core will attempt to read SW content from a
 * ZIP file called BootFile.bin. This ZIP file ought to contain the contents
 * of a typical SW folder. Note that the SW files added to the ZIP
 * file must not have SW as a prefix in their path.
 *
 * SW ZIP read devices are only ever read from. The core will only
 * ever mount a single instance of an SW ZIP read device.
 *
 * When the core successfully opens the BootFile.bin file, the core
 * continues to use the os device for writing files. This means that
 * client code of the core can set the os device to an arbitrary empty
 * directory and then install a SW ZIP read device which points to a
 * shared BootFile.bin file. If the os device picks a unique directory name,
 * multiple RIP's can be executed all sharing a single BootFile.bin.
 */

#include "std.h"
#include "hqstr.h"
#include "devutils.h"
#include "ripthread.h"
#include "file.h"
#include "paths.h"
#include "mem.h"
#include "sync.h"    /* PKCreateSemaphore, PKDestroySemaphore */
#include "skindevs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/*
  The Harlequin RIP exported .h files: these are part of the product.
*/
#include "swdevice.h"
#include "swoften.h"
#include "swzip.h"

/* ---------------------------------------------------------------------- */
/*
  Miscellaneous items:
*/

#define PREFIX_MAX_LENGTH LONGESTFILENAME     /* the maximum length of the Prefix string */

/* It is useful to hang on to a pointer the SW ZIP read device */
DEVICELIST * theswzipdevice = NULL;

/**
 * @brief  Structure to hold file-specific state.
 */
typedef struct ZIPFileState
{
  FileDesc* pDescriptor;
  int32 short_read;
} ZIPFileState;

/**
 * @brief  Structure to hold device-specific state.
 */
typedef struct ZIPDeviceState
{
  uint8 psSWZIPDir[ PREFIX_MAX_LENGTH ];

  /** @brief Semaphore to protect concurrent calls. */
  void *pSema;
} ZIPDeviceState;

/* This buffer holds an explicitly-provided path to the SW ZIP folder,
 * as stored by KSetSWZIPDir(). If this string is zero-size, then no
 * explicit SWZIP folder path has been specified, and the skin should
 * default to the per-platform search rules.
 */
static uint8 swzipDirOverride[ LONGESTFILENAME ] = "";

/* ----------------------- Helper functions ------------------------------ */

/** \brief
   Allocate a ZIPFileState object. Should be released with releaseFileState().
*/
static ZIPFileState* createFileState ()
{
  ZIPFileState* pState = MemAlloc (sizeof (ZIPFileState), TRUE, FALSE);

  if (pState)
  {
    /* Initialise content */
    pState->pDescriptor = NULL;
    pState->short_read = 0;
  }

  return pState;
}

/** \brief
   Release a FileState object.
 */
static void releaseFileState (ZIPFileState* pState)
{
  if (pState)
    MemFree (pState);
}

static void RIPCALL swzip_set_last_error( DEVICELIST *dev, int32 nError )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(nError);
}

/**
 * @brief Record an explicit path to the folder where BootFile.bin is kept,
 * overriding any default search rules.
 *
 * @param pSWZIPDir Pointer to a null-terminated path, which may not
 * exceed LONGESTFILENAME in length (inclusive of the terminator), and
 * which must end with a platform directory separator.
 *
 * @return TRUE on success; FALSE otherwise.
 */
int32 KSetSWZIPDir( uint8 *pSWZIPDir )
{
  int32 pathLen = strlen_int32( (char*) pSWZIPDir );

  if ( pathLen < 1 || pathLen >= LONGESTFILENAME )
  {
    return FALSE;
  }
  else if ( pSWZIPDir[ pathLen - 1 ] != DIRECTORY_SEPARATOR )
  {
    return FALSE;
  }
  else
  {
    strcpy( (char*) swzipDirOverride, (char*) pSWZIPDir );
    return TRUE;
  }
}

/* ---------------------------------------------------------------------- */
/* \brief The following routines are simple stub routines for cases
   where the only functionality required is to set the appropriate
   error condition for the SW ZIP read device type.

   Take care which is used in which circumstances.
*/

static int32 RIPCALL swzip_ioerror ( DEVICELIST *dev )
{
  swzip_set_last_error ( dev, DeviceIOError );
  return -1;
}

static int32 RIPCALL swzip_spare ( void )
{
  return 0;
}

static int32 RIPCALL swzip_write_file(DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                      uint8 *buff, int32 len )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8*, buff);
  UNUSED_PARAM(int32, len);

  swzip_set_last_error ( dev, DeviceIOError );

  return -1 ;
}

static void* RIPCALL swzip_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  UNUSED_PARAM( uint8*, pattern );

  /* Ensure that next_file and end_file_list never get called. We
     emulate no files being found. */
  swzip_set_last_error ( dev, DeviceNoError );
  return NULL ;
}

static int32 RIPCALL swzip_next_file( DEVICELIST *dev, void **handle,
                                      uint8 *pattern, FILEENTRY *entry )
{
  UNUSED_PARAM( void**, handle );
  UNUSED_PARAM( uint8*, pattern );
  UNUSED_PARAM( FILEENTRY*, entry );

  /* For the swzip device, this should never be called. If it is,
     raise an error. */
  swzip_set_last_error ( dev, DeviceIOError );
  return -1;
}

static int32 RIPCALL swzip_end_file_list( DEVICELIST *dev, void * handle )
{
  UNUSED_PARAM( void*, handle );

  /* For the swzip device, this should never be called. If it is,
     raise an error. */
  swzip_set_last_error ( dev, DeviceIOError );
  return -1;
}

static int32 RIPCALL swzip_rename_file( DEVICELIST *dev,
                                        uint8 *file1, uint8 *file2 )
{
  UNUSED_PARAM( uint8*, file1 );
  UNUSED_PARAM( uint8*, file2 );

  /* Renaming on swzip devices is not allowed. */
  swzip_set_last_error ( dev, DeviceInvalidAccess );
  return -1 ;
}

static int32 RIPCALL swzip_delete_file( DEVICELIST *dev, uint8 *filename )
{
  UNUSED_PARAM( uint8*, filename );

  /* Deleting on swzip devices is not allowed. */
  swzip_set_last_error ( dev, DeviceInvalidAccess );
  return -1 ;
}

static int32 RIPCALL swzip_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  UNUSED_PARAM( DEVICEPARAM*, param );

  swzip_set_last_error ( dev, DeviceNoError );
  return ParamIgnored;
}

static int32 RIPCALL swzip_start_param( DEVICELIST *dev )
{
  swzip_set_last_error ( dev, DeviceNoError );
  return 0; /* number of device specific parameters: none */
}

static int32 RIPCALL swzip_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  UNUSED_PARAM( DEVICEPARAM*, param );

  swzip_set_last_error ( dev, DeviceNoError );
  return ParamIgnored ;
}

/* ---------------------------------------------------------------------- */
/* \brief The following routines are what need to be implemented for
   the SW ZIP read device. For full details on device callbacks, refer
   to the comments in filedev.c */

static int32 RIPCALL swzip_device_init( DEVICELIST *dev )
{
  uint8  platformSWDir[ LONGESTFILENAME ];
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;

  void *pSema = PKCreateSemaphore( 1 );
  if ( pSema == NULL )
    return -1;

  if ( theswzipdevice == NULL ) {
    /* The %swzip% device is unique in that it is created by the core
       rip. It is guaranteed to be initialised before any other device
       if an SW device type is supplied. Therefore dev must refer to
       the %swzip% device if 'theswzipdevice' is null (its statically
       initialised value). We assume its prefix (see discussion at the
       head of the file) is "SWZIP/". */
    theswzipdevice = dev;
  } else {
    /* We only ever allow one SW ZIP device to be mounted. */
    swzip_set_last_error ( dev, DeviceInvalidAccess );
    return -1; /* failure */
  }

  pDeviceState->psSWZIPDir[0] = '\0';
  pDeviceState->pSema = pSema;

  /* Form prefix from directory containing app + 'SW/' */
  if ( swzipDirOverride[ 0 ] != '\0' )
  {
    /* An explicit path to the SW folder has been provided, so use it. */
    PlatformFilenameToPS( pDeviceState->psSWZIPDir, swzipDirOverride );
  }
  else
  {
    /* Use per-platform search rules to find the SW folder. */
    PKSWDir( platformSWDir );
    PlatformFilenameToPS( pDeviceState->psSWZIPDir, platformSWDir );
  }

  swzip_set_last_error ( dev, DeviceNoError );
  return 0; /* success */
}

static int32 RIPCALL swzip_dismount_device( DEVICELIST *dev )
{
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;

  PKDestroySemaphore( pDeviceState->pSema );

  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL swzip_open_file( DEVICELIST *dev,
                                      uint8 *filename, int32 openflags )
{
  int32 pkError;
  DEVICE_FILEDESCRIPTOR result;
  uint8 name[ LONGESTFILENAME ];
  ZIPFileState* pFileState;
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;

  /* Create file-specific state */
  pFileState = createFileState ();
  if (! pFileState)
  {
    swzip_set_last_error( dev, DeviceVMError );
    return -1;
  }

  /* Create the complete filename by concatenating the /Prefix for the
   * current device and the provided filename.
   */
  name[0] = 0;
  PSPrefixAndFilenameToPlatform(name, pDeviceState->psSWZIPDir, filename );

  pFileState->pDescriptor = PKOpenFile(name, openflags, &pkError);
  if (! pFileState->pDescriptor)
  {
    swzip_set_last_error( dev, KMapPlatformError(pkError) );
    releaseFileState (pFileState);
    return -1;
  }

  result = VOIDPTR_TO_DEVICE_FILEDESCRIPTOR(pFileState);

  swzip_set_last_error( dev, DeviceNoError );
  return result;
}

static int32 RIPCALL swzip_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                           uint8 *buff, int32 len )
{
  int32 result;
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;
  ZIPFileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor) ;
  if (! pFileState)
  {
    swzip_set_last_error( dev, DeviceInvalidAccess );
    result = -1;
  }
  else
  {
    int32 pkError;

    if (pFileState->short_read > 0)
    {
      /* set by ioctl call just before read */
      len = pFileState->short_read;
      pFileState->short_read = 0;
    }

    result = PKReadFile(pFileState->pDescriptor, buff, len, &pkError);
    if (result < 0)
      swzip_set_last_error( dev, KMapPlatformError(pkError) );
    else
      swzip_set_last_error( dev, DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}

static int32 RIPCALL swzip_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  int32 result;
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;
  ZIPFileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor) ;
  if (! pFileState)
  {
    swzip_set_last_error( dev, DeviceInvalidAccess );
    result = -1;
  }
  else
  {
    int32 pkError;

    result = PKCloseFile(pFileState->pDescriptor, &pkError);
    if (result < 0)
    {
      swzip_set_last_error( dev, KMapPlatformError(pkError) );
    }
    else
    {
      swzip_set_last_error( dev, DeviceNoError );
      releaseFileState( pFileState );
    }
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}

static int32 RIPCALL swzip_seek_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 * destination, int32 flags )
{
  int32 result = FALSE;
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;
  ZIPFileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor) ;
  if (! pFileState)
  {
    swzip_set_last_error( dev, DeviceInvalidAccess );
  }
  else
  {
    int32 pkError;

    result = PKSeekFile(pFileState->pDescriptor, destination, flags, &pkError);
    if (! result)
      swzip_set_last_error( dev, KMapPlatformError(pkError) );
    else
      swzip_set_last_error( dev, DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}

static int32 RIPCALL swzip_bytes_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * bytes, int32 reason )
{
  int32 result = FALSE;
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;
  ZIPFileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor) ;
  if (! pFileState)
  {
    swzip_set_last_error( dev, DeviceInvalidAccess );
  }
  else
  {
    int32 pkError;

    result = PKBytesFile(pFileState->pDescriptor, bytes, reason, &pkError);
    if ( !result )
      swzip_set_last_error( dev, KMapPlatformError(pkError) );
    else
      swzip_set_last_error( dev, DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}

static int32 RIPCALL swzip_status_file( DEVICELIST *dev, uint8 *filename,
                                        STAT *statbuff )
{
  int32 pkError;
  int32 result;
  uint8 fname[ LONGESTFILENAME ];
  ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;

  fname[0] = 0;
  PSPrefixAndFilenameToPlatform(fname, pDeviceState->psSWZIPDir, filename);

  result = PKStatusFile(fname, statbuff, &pkError);
  if (result < 0)
    swzip_set_last_error ( dev, KMapPlatformError (pkError));
  else
    swzip_set_last_error ( dev, DeviceNoError );

  return result;
}

static int32 RIPCALL swzip_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  swzip_set_last_error ( dev, DeviceNoError );
  devstat->size.low = 0;
  devstat->size.high = 0;
  devstat->free.low = 0;
  devstat->free.high = 0;
  devstat->block_size = 1024; /* fake it - you may wish to actually look
                               * this up using statvfs().
                               */
  devstat->start = NULL;
  return 0;
}

static int32 RIPCALL swzip_ioctl( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor, int32 opcode, intptr_t arg )
{
  int32 result = 0;
  int32 nError = DeviceNoError;

  if (opcode == DeviceIOCtl_ShortRead)
  {
    ZIPDeviceState* pDeviceState = (ZIPDeviceState*) dev->private_data;
    ZIPFileState* pFileState;

    PKWaitOnSemaphore (pDeviceState->pSema);

    pFileState = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor) ;
    if (! pFileState)
    {
      nError = DeviceInvalidAccess;
      result = -1;
    }
    else
    {
      pFileState->short_read = CAST_INTPTRT_TO_INT32 (arg);
      result = 0;
    }

    PKSignalSemaphore (pDeviceState->pSema);
  }

  swzip_set_last_error( dev, nError );
  return result;
}

/* ---------------------------------------------------------------------- */

DEVICETYPE SwZip_Device_Type = {
  SWZIPREAD_DEVICE_TYPE,  /* the device type number */
  DEVICERELATIVE | DEVICENOSEARCH | DEVICEUNDISMOUNTABLE,
                          /* device characteristics flags */
  CAST_SIZET_TO_INT32(sizeof (ZIPDeviceState)), /* the size of the private data */
  0,                      /* tickle function control: n/a */
  NULL,                   /* procedure to service the device */
  skindevices_last_error,    /* return last error for this device */
  swzip_device_init,         /* call to initialise device */
  swzip_open_file,           /* call to open file on device */
  swzip_read_file,           /* call to read data from file on device */
  swzip_write_file,          /* call to write data to file on device */
  swzip_close_file,          /* call to close file on device */
  swzip_close_file,          /* call to abort: same as close */
  swzip_seek_file,           /* call to seek file on device */
  swzip_bytes_file,          /* call to get bytes avail on an open file */
  swzip_status_file,         /* call to check status of file */
  swzip_start_file_list,     /* call to start listing files */
  swzip_next_file,           /* call to get next file in list */
  swzip_end_file_list,       /* call to end listing */
  swzip_rename_file,         /* call to rename file on the device */
  swzip_delete_file,         /* call to remove file from device */
  swzip_set_param,           /* call to set device parameter */
  swzip_start_param,         /* call to start getting device parameters */
  swzip_get_param,           /* call to get the next device parameter */
  swzip_status_device,       /* call to get the status of the device */
  swzip_dismount_device,     /* call to dismount the device */
  swzip_ioerror,             /* call to determine buffer size (dummy) */
  swzip_ioctl,               /* ioctl slot, to hint about caching, etc. */
  swzip_spare                /* spare slot */
};

void init_C_globals_swzipreaddev(void)
{
  theswzipdevice = NULL ;
  swzipDirOverride[0] = '\0' ;
}

