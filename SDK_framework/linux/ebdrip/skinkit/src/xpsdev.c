/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*
 * $HopeName: SWskinkit!src:xpsdev.c(EBDSDK_P.1) $
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of an input device for obtaining XPS page parts
 * from an external source.
 */

#include <string.h>

#include "mem.h"
#include "memfs.h"
#include "swdevice.h"
#include "skindevs.h"     /* skindevices_last_error */
#include "hqstr.h"        /* strlen_int32 */
#include "ripthread.h"    /* RAM_Device_Type */
#include "file.h"         /* LONGESTFILENAME */
#include "sync.h"         /* PKCreateSemaphore, PKDestroySemaphore */
#include "streams.h"      /* HqnReadStream */
#include "xpsbuild.h"     /* buildXPSPackageInMemory */
#include "xpsdev.h"       /* xpsdevSetPackage, xpsdevClearPackage etc. */

#define XPSDEV_PARAM_TYPE   0
#define XPSDEV_PARAM_COUNT  1

/**
 * @brief Name of the single, read-only device parameter for this device.
 */
static uint8* szParamType =   (uint8*) "Type";

/**
 * @brief Constant value of the <code>/Type</code> device parameter. This allows
 * the RIP to classify this device as a storage medium.
 */
static uint8* szParamValueFileSystem = (uint8*) "FileSystem";

static void xpsdev_set_last_error(DEVICELIST *dev, int32 error);

static int32 RIPCALL xpsdev_ioerror( DEVICELIST *dev );

static int32 RIPCALL xpsdev_ioerror_false( DEVICELIST *dev );

static int32 RIPCALL xpsdev_noerror( DEVICELIST *dev );

static int32 RIPCALL xpsdev_init_device( DEVICELIST *dev );

static int32 RIPCALL xpsdev_dismount_device( DEVICELIST *dev );

static DEVICE_FILEDESCRIPTOR RIPCALL xpsdev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags );

static int32 RIPCALL xpsdev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );

static int32 RIPCALL xpsdev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );

static int32 RIPCALL xpsdev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );

static int32 RIPCALL xpsdev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags );

static int32 RIPCALL xpsdev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason );

static int32 RIPCALL xpsdev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff );

static void* RIPCALL xpsdev_start_file_list( DEVICELIST *dev, uint8 *pattern );

static int32 RIPCALL xpsdev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *patter, FILEENTRY *entry );

static int32 RIPCALL xpsdev_end_file_list( DEVICELIST *dev, void *handle );

static int32 RIPCALL xpsdev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 );

static int32 RIPCALL xpsdev_delete_file( DEVICELIST *dev, uint8 *filename );

static int32 RIPCALL xpsdev_set_param( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL xpsdev_start_param(DEVICELIST *dev);

static int32 RIPCALL xpsdev_get_param( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL xpsdev_status_device( DEVICELIST *dev, DEVSTAT *devstat );

static int32 RIPCALL xpsdev_spare( void );


static XPSPackageDescription *pgCurrentPackageDescription = NULL;

static XPSPackageStreamManager *pgCurrentStreamManager = NULL;

static int32 fgCurrentPackageIsComplete = TRUE;

/** @brief Encapsulates the state of a single instance of the XPS device. */
typedef struct _XPSDeviceState
{
  /** @brief Opaque pointer to the build context. */
  void     *pBuildContext;

  /** @brief Points to the root of the Memory File System for this XPS device. */
  MFSNODE  *pMFSRoot;

  /** @brief Semaphore to protect concurrent calls. */
  void     *pSema;

  /** @brief An optional semaphore used to synchronize access to files
      whose content may grow dynamically. */
  void     *pDataSema;

  /** @brief Index ticker for the stateful <code>start_param</code> and
   *  <code>get_param</code> interfaces. */
  int32     iParam;

  /** @brief The zero-based current document index, used when calling
       the package stream manager. */
  uint32    iDocument;

  /** @brief The zero-based current page index, used when calling the
       package stream manager. */
  uint32    iPage;

  /** @brief The caller-supplied description of the XPS package. */
  XPSPackageDescription    *pPackageDescription;

  /** @brief The caller-supplied package stream manager. */
  XPSPackageStreamManager  *pStreamManager;

  /** @brief The special URI mappings that can be used to determine the document
       and page contexts. */
  URIContextMappingsList    contextMappings;
} XPSDeviceState;

/**
 * @brief Enumeration to indicate the two types of file that can be open on
 *        this device: either an in-memory file, or a client-managed
 *        HqnReadStream.
 */
enum
{
  XPSDEV_RAMFile,
  XPSDEV_ExternalInputStream
};

/**
 * @brief Encapsulates a file descriptor for this device.
 *
 * <p>A "file" on the XPS device might be one of two things. It might be a pointer
 * to an in-memory file managed by the skin's Memory File System (MFS). In
 * this case, the underlying file descriptor is actually an MFSFILEDESC
 * structure. Alternatively, the "file" might be a pointer to an
 * HqnReadStream, which was supplied by calling OpenResourceStreamFn()
 * on the XPSPackageStreamManager associated with this device.
 *
 * <p>This structure just wraps a union of those two pointer types, with
 * an integer discriminator.
 */
typedef struct _XPSFILEDESC
{
  uint32        type; /* Either XPSDEC_RAMFile or XPSDEV_ExternalStream */
  union
  {
     MFSFILEDESC   *pMFSDesc;
     HqnReadStream *pReadStream;
  } u;
} XPSFILEDESC;

/** \brief The XPS input device type structure. */
DEVICETYPE XpsInput_Device_Type = {
  XPS_INPUT_DEVICE_TYPE,     /**< the device ID number */
  DEVICERELATIVE,            /**< flags to indicate specifics of device */
  sizeof( XPSDeviceState ),  /**< the size of the private data */
  0,                         /**< minimum ticks between tickle functions */
  NULL,                      /**< procedure to service the device */
  skindevices_last_error,    /**< return last error for this device */
  xpsdev_init_device,        /**< call to initialise device */
  xpsdev_open_file,          /**< call to open file on device */
  xpsdev_read_file,          /**< call to read data from file on device */
  xpsdev_write_file,         /**< call to write data to file on device */
  xpsdev_close_file,         /**< call to close file on device */
  xpsdev_close_file,         /**< call to abort action on the device */
  xpsdev_seek_file,          /**< call to seek file on device */
  xpsdev_bytes_file,         /**< call to get bytes avail on an open file */
  xpsdev_status_file,        /**< call to check status of file */
  xpsdev_start_file_list,    /**< call to start listing files */
  xpsdev_next_file,          /**< call to get next file in list */
  xpsdev_end_file_list,      /**< call to end listing */
  xpsdev_rename_file,        /**< rename file on the device */
  xpsdev_delete_file,        /**< remove file from device */
  xpsdev_set_param,          /**< call to set device parameter */
  xpsdev_start_param,        /**< call to start getting device parameters */
  xpsdev_get_param,          /**< call to get the next device parameter */
  xpsdev_status_device,      /**< call to get the status of the device */
  xpsdev_dismount_device,    /**< call to dismount the device */
  xpsdev_ioerror,            /**< call to return buffer size */
  NULL,                      /**< ioctl slot (optional) */
  xpsdev_spare,              /**< spare slot */
};

void xpsdevSetPackage
  ( XPSPackageDescription *pPD, XPSPackageStreamManager *pPSM, uint32 fComplete )
{
  pgCurrentStreamManager = pPSM;
  pgCurrentPackageDescription = pPD;
  fgCurrentPackageIsComplete = fComplete;
}

void xpsdevClearPackage( void )
{
  pgCurrentStreamManager = NULL;
  pgCurrentPackageDescription = NULL;
}

int32 xpsdevAddFixedDocument
  ( uint8 *pszDevice, XPSDocumentDescription *pDocument )
{
  DEVICELIST *dev = SwFindDevice( pszDevice );

  if ( dev != NULL )
  {
    XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
    if ( pState->pBuildContext != NULL )
    {
      int32 error = skindevices_last_error(dev);
      int32 buildResult = addFixedDocumentToXPSPackage
        (
          pState->pBuildContext,
          pDocument,
          &error
        );

      xpsdev_set_last_error(dev, error);

      PKSignalSemaphore( pState->pDataSema );

      return ( buildResult > -1 );
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    return FALSE;
  }
}

int32 xpsdevAddFixedPage
  ( uint8 *pszDevice, XPSPageDescription *pPage )
{
  DEVICELIST *dev = SwFindDevice( pszDevice );

  if ( dev != NULL )
  {
    XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
    if ( pState->pBuildContext != NULL )
    {
      int32 error = skindevices_last_error(dev);
      int32 buildResult = addFixedPageToXPSPackage
        (
          pState->pBuildContext,
          pPage,
          &error
        );

      xpsdev_set_last_error(dev, error);

      PKSignalSemaphore( pState->pDataSema );

      return (buildResult > -1);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    return FALSE;
  }
}

int32 xpsdevSetPackageComplete( uint8 *pszDevice )
{
  DEVICELIST *dev = SwFindDevice( pszDevice );

  if ( dev != NULL )
  {
    XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
    if ( pState->pBuildContext != NULL )
    {
      int32 error = skindevices_last_error(dev);

      commitXPSPackageInMemory( pState->pBuildContext, &error );

      xpsdev_set_last_error(dev, error);

      PKSignalSemaphore( pState->pDataSema );
      return TRUE;
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    return FALSE;
  }
}

static void xpsdev_set_last_error(DEVICELIST *dev, int32 error)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(error);
}

static int32 RIPCALL xpsdev_ioerror( DEVICELIST *dev )
{
  xpsdev_set_last_error(dev, DeviceIOError);
  return -1;
}

static int32 RIPCALL xpsdev_ioerror_false( DEVICELIST *dev )
{
  xpsdev_set_last_error(dev, DeviceIOError);
  return FALSE ;
}

static int32 RIPCALL xpsdev_noerror( DEVICELIST *dev )
{
  xpsdev_set_last_error(dev, DeviceNoError);
  return 0;
}

static int32 RIPCALL xpsdev_init_device( DEVICELIST *dev )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  void *pSema;
  int32 error = DeviceNoError;

  /* We rely on the RIP client having set the current stream manager and
     package description, since these will become part of the device
     state. The device cannot function if either of these is NULL. */
  if ( pgCurrentStreamManager == NULL || pgCurrentPackageDescription == NULL )
    return -1;

  pSema = PKCreateSemaphore( 1 );

  /* We can't protect against concurrent calls, so we can't proceed with
     initialization. */
  if ( pSema == NULL )
    return -1;

  /* If the current package is marked as incomplete, then create a second
     semaphore that can be used to wait for additional data in
     any of the incremental part streams. */
  if ( !fgCurrentPackageIsComplete )
  {
    pState->pDataSema = PKCreateSemaphore( 0 );
    if ( !pState->pDataSema )
    {
      PKDestroySemaphore( pSema ); /* Clean up partial state before failing. */
      return -1;
    }
  }
  else
  {
    pState->pDataSema = NULL;
  }

  pState->contextMappings.pFirst = NULL;
  pState->contextMappings.pLast = NULL;

  /* Create the in-memory description of the package parts. */
  pState->pBuildContext = buildXPSPackageInMemory( pgCurrentPackageDescription,
                                                   &(pState->contextMappings),
                                                   &error );
  xpsdev_set_last_error(dev, error);

  /* Abort initialization if this didn't work. The lastError should have been
     set appropriately. */
  if ( pState->pBuildContext == NULL )
  {
    PKDestroySemaphore( pSema );
    return -1;
  }

  /* If the package is complete, commit the build. */
  if ( fgCurrentPackageIsComplete ) {
    commitXPSPackageInMemory( pState->pBuildContext, &error );
    xpsdev_set_last_error(dev, error);
  }

  xpsdev_set_last_error(dev, DeviceNoError);
  pState->pMFSRoot = getXPSPackageRoot( pState->pBuildContext );
  pState->pSema = pSema;
  pState->iParam = 0;
  pState->iDocument = 0;
  pState->iPage = 0;
  pState->pPackageDescription = pgCurrentPackageDescription;
  pState->pStreamManager = pgCurrentStreamManager;
  return 0;
}

static int32 RIPCALL xpsdev_dismount_device( DEVICELIST *dev )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  freeXPSPackageInMemory( pState->pBuildContext, &(pState->contextMappings) );
  PKDestroySemaphore( pState->pSema );

  if ( pState->pDataSema != NULL )
    PKDestroySemaphore( pState->pDataSema );

  pState->pMFSRoot = NULL;

  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL xpsdev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  MFSFILEDESC *pMFSFile = NULL;
  HqnReadStream *pStream = NULL;
  XPSFILEDESC *pFile;
  DEVICE_FILEDESCRIPTOR result;
  uint32 resourceType = ResourceType_URI;
  URIContextMapping *pContextMapping = NULL;
  int32 error = DeviceNoError;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  /* See whether the part we are trying to open is one that determines
     any useful contextual information. */
  pContextMapping = lookupContextMapping( &(pState->contextMappings), filename );

  if ( pContextMapping != NULL )
  {
    pState->iDocument = pContextMapping->iDocument;
    pState->iPage = pContextMapping->iPage;
    resourceType = pContextMapping->resourceType;
  }

  PKWaitOnSemaphore( pState->pSema );

  if ( MFSOpen( pState->pMFSRoot, (char*) filename, openflags, &pMFSFile, &error ) )
  {
    xpsdev_set_last_error(dev, error);

    pFile = (XPSFILEDESC*) MemAlloc( sizeof( XPSFILEDESC ), TRUE, FALSE );
    if ( pFile == NULL )
    {
      MFSClose( pMFSFile );
      xpsdev_set_last_error(dev, DeviceVMError);
      result = -1;
    }
    else
    {
      pFile->type = XPSDEV_RAMFile;
      pFile->u.pMFSDesc = pMFSFile;
      result = VOIDPTR_TO_DEVICE_FILEDESCRIPTOR( pFile );
    }
  }
  else if ( openflags & SW_RDONLY )
  {
    XPSPackageStreamManager *pPSM = pState->pStreamManager;

    xpsdev_set_last_error(dev, error);

    pPSM->pfOpenResourceStream
      (
        pPSM->pPrivate,
        pState->iDocument,
        pState->iPage,
        resourceType,
        filename,
        &pStream
      );

    if ( pStream != NULL )
    {
      pFile = (XPSFILEDESC*) MemAlloc( sizeof( XPSFILEDESC ), TRUE, FALSE );
      if ( pFile == NULL )
      {
        pStream->pfClose( pStream->pPrivate );
        xpsdev_set_last_error(dev, DeviceVMError);
        result = -1;
      }
      else
      {
        pFile->type = XPSDEV_ExternalInputStream;
        pFile->u.pReadStream = pStream;
        result = VOIDPTR_TO_DEVICE_FILEDESCRIPTOR( pFile );
      }
    }
    else
    {
      /* Stream not found by the external manager. */
      xpsdev_set_last_error(dev, DeviceUndefined);
      result = -1;
    }
  }
  else
  {
    xpsdev_set_last_error(dev, DeviceUndefined);
    result = -1;
  }

  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL xpsdev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  XPSFILEDESC *pFD;
  int32 result;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );
  pFD = (XPSFILEDESC*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
  if ( pFD->type == XPSDEV_RAMFile )
  {
    MFSFILEDESC *pMFSFD = pFD->u.pMFSDesc;
    MFSFILE *pFile = MFSGetFile( pMFSFD );
    int32 error = DeviceNoError;

    /* If the RAM file is still being written, and there is a semaphore
       allowing us to wait for more data, then wait for >0 bytes to become
       available before reading. This allows extenal code to populate
       the package dynamically while it is being processed. */
    if ( ( pState->pDataSema != NULL ) && ( pFile->nWriters > 0 ) )
    {
      Hq32x2  avail = { 0, 0 };
      uint32  fWaitingForBytes = TRUE;
      while ( fWaitingForBytes )
      {
        if ( !MFSAvail( pMFSFD, &avail, SW_BYTES_AVAIL_REL ) )
          return xpsdev_ioerror( dev );

        if ( !Hq32x2IsZero( &avail ) )
          fWaitingForBytes = FALSE;

        /* Wait for external code to signal that more data might be
           available. */
        if ( fWaitingForBytes )
        {
          PKWaitOnSemaphore( pState->pDataSema );
        }
      }
    }
    result = MFSRead( pMFSFD, buff, len, &error );
    xpsdev_set_last_error(dev, error);
  }
  else
  {
    HqnReadStream *pStream = pFD->u.pReadStream;
    result = pStream->pfRead( pStream->pPrivate, buff, len );
  }

  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL xpsdev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  XPSFILEDESC *pFD;
  int32 result;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */
  
  PKWaitOnSemaphore( pState->pSema );
  pFD = (XPSFILEDESC*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
  if ( pFD->type == XPSDEV_RAMFile ) {
    int32 error = DeviceNoError;
    result = MFSWrite( pFD->u.pMFSDesc, buff, len, &error );
    xpsdev_set_last_error(dev, error);
  }
  else
  {
    result = -1;
    xpsdev_set_last_error(dev, DeviceIOError);
  }

  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL xpsdev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  int32 result;
  XPSFILEDESC *pFD;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );

  pFD = (XPSFILEDESC*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );

  if ( pFD->type == XPSDEV_RAMFile )
  {
    if ( !MFSClose( pFD->u.pMFSDesc ) )
      result = xpsdev_ioerror( dev );
    else
    {
      MemFree( pFD );
      result = 0;
    }
  }
  else
  {
    HqnReadStream *pStream = pFD->u.pReadStream;
    if ( pStream->pfClose( pStream->pPrivate ) < 0 )
    {
      result = xpsdev_ioerror( dev );
    }
    else
    {
      XPSPackageStreamManager *pPSM = pState->pStreamManager;
      pPSM->pfCloseResourceStream( pPSM->pPrivate, pStream );
      MemFree( pFD );
      result = 0;
    }
  }

  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL xpsdev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  XPSFILEDESC *pFD;
  int32 result;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */
  PKWaitOnSemaphore( pState->pSema );
  pFD = (XPSFILEDESC*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );

  if ( pFD->type == XPSDEV_RAMFile )
  {
    int32 error = DeviceNoError;
    result = MFSSeek( pFD->u.pMFSDesc, destination, flags, &error );
    xpsdev_set_last_error(dev, error);
  }
  else
  {
    int32 frompos;
    HqnReadStream *pStream = pFD->u.pReadStream;

    switch( flags )
    {
      case SW_SET:   frompos = STREAM_POSITION_START   ; break ;
      case SW_INCR:  frompos = STREAM_POSITION_CURRENT ; break ;
      case SW_XTND:  frompos = STREAM_POSITION_END     ; break ;
      default:
        return xpsdev_ioerror_false(dev);
    }

    result = pStream->pfSeek( pStream->pPrivate, destination, frompos );

    if ( !result )
    {
      result = xpsdev_ioerror_false( dev );
    }
  }

  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL xpsdev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  XPSFILEDESC *pFD;
  int32 result;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */
  PKWaitOnSemaphore( pState->pSema );
  pFD = (XPSFILEDESC*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );

  if ( pFD->type == XPSDEV_RAMFile )
  {
    result = MFSAvail( pFD->u.pMFSDesc, bytes, reason );
  }
  else
  {
    Hq32x2 current = { 0 };
    HqnReadStream *pStream = pFD->u.pReadStream;
    bytes->high = 0;
    bytes->low = 0;
    result = pStream->pfSeek( pStream->pPrivate, &current, STREAM_POSITION_CURRENT )
          && pStream->pfSeek( pStream->pPrivate, bytes, STREAM_POSITION_END )
          && pStream->pfSeek( pStream->pPrivate, &current, STREAM_POSITION_START );
  }

  PKSignalSemaphore( pState->pSema );

  if ( !result )
    return xpsdev_ioerror_false( dev );
  return TRUE;
}

static int32 RIPCALL xpsdev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff )
{
  DEVICE_FILEDESCRIPTOR fd = xpsdev_open_file( dev, filename, SW_RDONLY );
  if ( fd == -1 )
  {
    return -1;
  }
  else
  {
    HqU32x2 * pBytes = &(statbuff->bytes);
    xpsdev_bytes_file( dev, fd, (Hq32x2*) pBytes, SW_BYTES_TOTAL_ABS );
    xpsdev_close_file( dev, fd );
    return 0;
  }
}

static void* RIPCALL xpsdev_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  XPSDeviceState *pDevState = (XPSDeviceState*) dev->private_data;
  uint8 *pNameBuffer;
  MFSITERSTATE *pState = NULL;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */
  
  UNUSED_PARAM( uint8*, pattern ); /* Not needed here, because it's passed into each
                                      xpsdev_next_file call. */

  pNameBuffer = (uint8*) MemAlloc( LONGESTFILENAME + 1, TRUE, FALSE );

  if ( pNameBuffer == NULL )
  {
    xpsdev_set_last_error(dev, DeviceVMError);
    return NULL;
  }

  PKWaitOnSemaphore( pDevState->pSema );

  pState = MFSIterBegin( pDevState->pMFSRoot, pNameBuffer );

  PKSignalSemaphore( pDevState->pSema );

  return (void*) pState;
}

static int32 RIPCALL xpsdev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *pattern, FILEENTRY *entry )
{
  XPSDeviceState *pDevState = (XPSDeviceState*) dev->private_data;
  MFSITERSTATE *pState = (MFSITERSTATE*) (*handle);
  uint32 fLeadingSlash = FALSE;

  /* If the pattern is supplied with a leading slash, then also include
     one on the filenames, otherwise they won't match. */

  if ( (char) pattern[ 0 ] == '/' )
    fLeadingSlash = TRUE;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pDevState->pSema );

  while ( MFSIterNext( pState ) )
  {
    MFSIterName
      ( pState, (char*) pState->pPrivate, LONGESTFILENAME + 1, fLeadingSlash );

    if ( SwPatternMatch( pattern, (uint8*) pState->pPrivate ) )
    {
      entry->namelength = strlen_int32( (char*) pState->pPrivate );
      entry->name = (uint8*) pState->pPrivate;
      PKSignalSemaphore( pDevState->pSema );
      return FileNameMatch;
    }
  }

  PKSignalSemaphore( pDevState->pSema );

  return FileNameNoMatch ;
}

static int32 RIPCALL xpsdev_end_file_list( DEVICELIST *dev, void *handle )
{
  MFSITERSTATE *pState = (MFSITERSTATE*) handle;

  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  MemFree( pState->pPrivate );
  MFSIterEnd( pState );

  return 0;
}

static int32 RIPCALL xpsdev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 )
{
  UNUSED_PARAM( uint8*, file1 );
  UNUSED_PARAM( uint8*, file2 );
  return xpsdev_ioerror( dev );
}

static int32 RIPCALL xpsdev_delete_file( DEVICELIST *dev, uint8 *filename )
{
  UNUSED_PARAM( uint8*, filename );
  return xpsdev_ioerror( dev );
}

static int32 RIPCALL xpsdev_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  UNUSED_PARAM( DEVICEPARAM*, param ); /* No settable parameters of this device. */ 
  (void) xpsdev_noerror(dev) ; /* clear error flag first */
  return ParamIgnored;
}

static int32 RIPCALL xpsdev_start_param(DEVICELIST *dev)
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  (void) xpsdev_noerror(dev) ; /* clear error flag first */
  pState->iParam = 0;
  return XPSDEV_PARAM_COUNT ;
}

static int32 RIPCALL xpsdev_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  XPSDeviceState *pState = (XPSDeviceState*) dev->private_data;
  (void) xpsdev_noerror(dev) ; /* clear error flag first */

  if ( param->paramname == NULL )
  {
    /* The RIP is part way through asking for all parameters. */
    switch ( pState->iParam++ )
    {
      case XPSDEV_PARAM_TYPE:
        param->paramname = szParamType;
        param->paramnamelen = strlen_int32( (char*) szParamType );
        param->type = ParamString;
        param->paramval.strval = szParamValueFileSystem;
        param->strvallen = strlen_int32( (char*) szParamValueFileSystem );
        return ParamAccepted;

      default:
        /* Out of range. */
        return ParamIgnored;
    }
  }
  else if ( strncmp( (char*) param->paramname, (char*) szParamType,
                     CAST_SIGNED_TO_SIZET(param->paramnamelen) ) == 0 )
  {
    /* The RIP is asking for the "Type" parameter specifically. */
    param->type = ParamString;
    param->paramval.strval = szParamValueFileSystem;
    param->strvallen = strlen_int32( (char*) szParamValueFileSystem );
    return ParamAccepted;
  }
  else
  {
    /* The RIP is asking for a parameter not defined by this device. */
    return ParamIgnored;
  }
}

static int32 RIPCALL xpsdev_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  devstat->block_size = 1024;
  devstat->start = NULL;
  HqU32x2FromInt32( &(devstat->size), 0 );
  HqU32x2FromInt32( &(devstat->free), 0 );

  return xpsdev_noerror( dev );
}

static int32 RIPCALL xpsdev_spare( void )
{
  return 0;
}

