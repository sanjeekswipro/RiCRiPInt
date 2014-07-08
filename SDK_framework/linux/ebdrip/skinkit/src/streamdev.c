/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:streamdev.c(EBDSDK_P.1) $
 */


/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of an input/output device tied to a stream reader.
 *
 * Useful for integrating into eg XPSDrv filter pipeline.
 */

#include <string.h>

#include "std.h"
#include "swdevice.h"
#include "skindevs.h"
#include "streamdev.h"
#include "ripthread.h"
#include "mem.h"

/* Maximum number of streams in the device */
#define MAXIMUM_STREAM_ENTRIES     10

#define STREAM_TYPE_READ           1
#define STREAM_TYPE_WRITE          2

#define INDEX_UNDEFINED            -1

/* conversions between a descriptor and stream entry index */
#define FD_FROM_INDEX(idx)        INT32_TO_DEVICE_FILEDESCRIPTOR((idx) + 1)
#define INDEX_FROM_FD(fd)         (DEVICE_FILEDESCRIPTOR_TO_INT32(fd) - 1)

typedef struct StreamEntry
{
  int32  fd;       /* descriptor, 0 if not opened */
  int32  type;     /* stream type: read or write */
  char * pFile;    /* file name */
  void * pStream;  /* read/write stream */
  int32  fInUse;   /* the entry is used */
} StreamEntry ;

/**
 * @brief  Structure to hold device-specific state.
 */
typedef struct StreamDeviceState
{
  StreamEntry * pAllStreams;
} StreamDeviceState;

static int32 getStreamEntryIdxbyName(DEVICELIST *dev, char * pFile)
{
  int32 i;
  StreamEntry * pEntries = ((StreamDeviceState*) dev->private_data)->pAllStreams;

  if (pFile == NULL || pEntries == NULL)
    return INDEX_UNDEFINED;

  for (i = 0; i  < MAXIMUM_STREAM_ENTRIES; i++)
    if ((pEntries[i].pFile != NULL) && ! strcmp(pFile, pEntries[i].pFile) && pEntries[i].fInUse)
      return i;
  return INDEX_UNDEFINED ;
}

static int32 registerStream(const char * devname, const char * pFile, void *pStrm, int32 type )
{
  int32 i;
  DEVICELIST * dev = SwFindDevice((uint8 *)devname);
  StreamEntry * pEntries ;

  if (dev == NULL || theIDevTypeNumber(theIDevType(dev)) != STREAM_DEVICE_TYPE ||
      pFile == NULL || pStrm == NULL)
    return FALSE;

  pEntries = ((StreamDeviceState*) dev->private_data)->pAllStreams;
  if (pEntries == NULL)
    return FALSE ;
  /* Find an empty slot */
  for (i = 0; i  < MAXIMUM_STREAM_ENTRIES; i++)
    if (! pEntries[i].fInUse)
      break;
  if (i == MAXIMUM_STREAM_ENTRIES)
    return FALSE;

  pEntries[i].fInUse = TRUE;
  pEntries[i].type = type;
  pEntries[i].pFile = ( char * ) pFile;
  pEntries[i].pStream = (void *) pStrm;
  return TRUE;
}

/* See header for doc */
int32 registerStreamReader(const char * devname, const char * pFile, HqnReadStream *pRS )
{
  return (registerStream(devname, pFile, (void *) pRS, STREAM_TYPE_READ));
}

/* See header for doc */
int32 registerStreamWriter(const char * devname, const char * pFile, HqnWriteStream *pWS )
{
  return (registerStream(devname, pFile, (void *) pWS, STREAM_TYPE_WRITE));
}

/* See header for doc */
void UnregisterStreamReaderWriter(const char * devname, const char * pFile)
{
  int32 i;
  DEVICELIST * dev = SwFindDevice((uint8 *)devname);

  if (dev == NULL || theIDevTypeNumber(theIDevType(dev)) != STREAM_DEVICE_TYPE || pFile == NULL )
    return ;

  i = getStreamEntryIdxbyName(dev, (char*) pFile);

  if (i != INDEX_UNDEFINED)
  {
    StreamEntry * pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[i]);
    pEntry->fInUse = FALSE;
    pEntry->fd = 0;
    pEntry->type = 0;
    pEntry->pFile = NULL;
    pEntry->pStream = NULL;
  }
}

static int32 RIPCALL streamdev_ioerror( DEVICELIST *dev );

static int32 streamdev_ioerror_false( DEVICELIST *dev );

static int32 streamdev_noerror( DEVICELIST *dev );

static void* RIPCALL streamdev_void( DEVICELIST *dev );

static int32 RIPCALL streamdev_init_device( DEVICELIST *dev );

static int32 RIPCALL streamdev_dismount_device( DEVICELIST *dev );

static DEVICE_FILEDESCRIPTOR RIPCALL streamdev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags );

static int32 RIPCALL streamdev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );

static int32 RIPCALL streamdev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );

static int32 RIPCALL streamdev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );

static int32 RIPCALL streamdev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags );

static int32 RIPCALL streamdev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason );

static int32 RIPCALL streamdev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff );

static void* RIPCALL streamdev_start_file_list( DEVICELIST *dev, uint8 *pattern );

static int32 RIPCALL streamdev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *patter, FILEENTRY *entry );

static int32 RIPCALL streamdev_end_file_list( DEVICELIST *dev, void *handle );

static int32 RIPCALL streamdev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 );

static int32 RIPCALL streamdev_delete_file( DEVICELIST *dev, uint8 *filename );

static int32 RIPCALL streamdev_set_param( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL streamdev_start_param(DEVICELIST *dev);

static int32 RIPCALL streamdev_get_param( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL streamdev_status_device( DEVICELIST *dev, DEVSTAT *devstat );

static int32 RIPCALL streamdev_spare( void );

/** \brief The pipeline input device type structure. */
DEVICETYPE Stream_Device_Type = {
  STREAM_DEVICE_TYPE,        /**< the device ID number */
  DEVICERELATIVE,            /**< flags to indicate specifics of device */
  CAST_SIZET_TO_INT32(sizeof (StreamDeviceState)), /**< the size of the private data */
  0,                         /**< minimum ticks between tickle functions */
  NULL,                      /**< procedure to service the device */ /* Tickle does nothing. */
  skindevices_last_error,    /**< return last error for this device */
  streamdev_init_device,     /**< call to initialise device */
  streamdev_open_file,       /**< call to open file on device */
  streamdev_read_file,       /**< call to read data from file on device */
  streamdev_write_file,      /**< call to write data to file on device */
  streamdev_close_file,      /**< call to close file on device */
  streamdev_close_file,      /**< call to abort action on the device */
  streamdev_seek_file,       /**< call to seek file on device */
  streamdev_bytes_file,      /**< call to get bytes avail on an open file */
  streamdev_status_file,     /**< call to check status of file */
  streamdev_start_file_list, /**< call to start listing files */
  streamdev_next_file,       /**< call to get next file in list */
  streamdev_end_file_list,   /**< call to end listing */
  streamdev_rename_file,     /**< rename file on the device */
  streamdev_delete_file,     /**< remove file from device */
  streamdev_set_param,       /**< call to set device parameter */
  streamdev_start_param,     /**< call to start getting device parameters */
  streamdev_get_param,       /**< call to get the next device parameter */
  streamdev_status_device,   /**< call to get the status of the device */
  streamdev_dismount_device, /**< call to dismount the device */
  streamdev_ioerror,         /**< call to return buffer size */
  NULL,                      /**< ioctl slot (optional) */
  streamdev_spare,           /**< spare slot */
};

static void streamdev_set_last_error( DEVICELIST *dev, int32 nError )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(nError);
}

static int32 RIPCALL streamdev_ioerror(DEVICELIST *dev)
{
  streamdev_set_last_error( dev, DeviceIOError );
  return -1;
}

static int32 streamdev_ioerror_false( DEVICELIST *dev )
{
  streamdev_set_last_error( dev, DeviceIOError );
  return FALSE ;
}

static int32 streamdev_noerror( DEVICELIST *dev )
{
  streamdev_set_last_error( dev, DeviceNoError );
  return 0;
}

static void* RIPCALL streamdev_void( DEVICELIST *dev )
{
  streamdev_set_last_error( dev, DeviceNoError );
  return (void*) NULL;
}

static int32 RIPCALL streamdev_init_device( DEVICELIST *dev )
{
  StreamDeviceState* pDeviceState = (StreamDeviceState*) dev->private_data;

  (void) streamdev_noerror(dev);

  pDeviceState->pAllStreams = (StreamEntry *) MemAlloc(
                sizeof(StreamEntry) * MAXIMUM_STREAM_ENTRIES, TRUE, FALSE );

  if ( pDeviceState->pAllStreams == NULL )
  {
    streamdev_set_last_error(dev, DeviceVMError);
    return -1;
  }
  return 0;
}

static int32 RIPCALL streamdev_dismount_device( DEVICELIST *dev )
{
  StreamDeviceState* pDeviceState = (StreamDeviceState*) dev->private_data;

  (void) streamdev_noerror(dev) ; /* clear error flag first */

  if (pDeviceState->pAllStreams)
    MemFree(pDeviceState->pAllStreams) ;

  return 0;
}

static int32 opened_for_input(StreamEntry * pEntry, int32 openflags )
{
  return ( ( openflags & SW_RDONLY ) &&
           ( pEntry->fd == 0 ) &&
           ( pEntry->type == STREAM_TYPE_READ ) &&
           ( pEntry->pStream != NULL ) );
}

static int32 opened_for_output(StreamEntry * pEntry, int32 openflags )
{
  return ( ( ( openflags & SW_WRONLY ) || ( openflags & SW_RDWR ) ) &&
           ( pEntry->fd == 0 ) &&
           ( pEntry->type == STREAM_TYPE_WRITE ) &&
           ( pEntry->pStream != NULL ) );
}

static DEVICE_FILEDESCRIPTOR RIPCALL streamdev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags )
{
  /* could be being opened for input or output */
  int32 idx = getStreamEntryIdxbyName(dev, (char*) filename);

  if ( idx != INDEX_UNDEFINED)
  {
    StreamEntry * pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[idx]);
    if (opened_for_input( pEntry, openflags ) || opened_for_output( pEntry, openflags ) )
    {
      streamdev_set_last_error( dev, DeviceNoError );
      pEntry->fd = FD_FROM_INDEX(idx);
      return pEntry->fd;
    }
  }
  streamdev_set_last_error( dev, DeviceUndefined );
  return -1;
}

static int32 RIPCALL streamdev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  int32 result, idx = INDEX_FROM_FD(descriptor);
  StreamEntry * pEntry ;
  HqnReadStream  * pReadStream ;

  if (idx < 0 || idx > MAXIMUM_STREAM_ENTRIES)
    return streamdev_ioerror(dev);

  pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[idx]);
  if ( pEntry->pStream == NULL || pEntry->fd == 0 || pEntry->fd != descriptor )
  {
    return streamdev_ioerror(dev);
  }

  if ( pEntry->type != STREAM_TYPE_READ )
  {
    streamdev_set_last_error( dev, DeviceInvalidAccess );
    return -1;
  }

  pReadStream = (HqnReadStream  *) pEntry->pStream ;
  result = pReadStream->pfRead( pReadStream->pPrivate, buff, len );

  if ( result < 0 )
  {
    return streamdev_ioerror(dev);
  }
  else
  {
    return result;
  }
}

static int32 RIPCALL streamdev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  int32 result, idx = INDEX_FROM_FD(descriptor);
  StreamEntry * pEntry ;
  HqnWriteStream  * pWriteStream ;

  if (idx < 0 || idx > MAXIMUM_STREAM_ENTRIES)
    return streamdev_ioerror(dev);

  pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[idx]);
  if ( pEntry->pStream == NULL || pEntry->fd == 0 || pEntry->fd != descriptor )
  {
    return streamdev_ioerror(dev);
  }

  if ( pEntry->type != STREAM_TYPE_WRITE )
  {
    streamdev_set_last_error( dev, DeviceInvalidAccess );
    return -1;
  }

  pWriteStream = (HqnWriteStream  *) pEntry->pStream ;
  result = pWriteStream->pfWrite( pWriteStream->pPrivate, buff, len );

  if ( result < 0 )
  {
    return streamdev_ioerror(dev);
  }
  else
  {
    return result;
  }
}

static int32 RIPCALL streamdev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  int32 idx = INDEX_FROM_FD(descriptor);
  StreamEntry * pEntry  ;

  if (idx < 0 || idx > MAXIMUM_STREAM_ENTRIES)
    return streamdev_ioerror(dev);

  pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[idx]);
  if ( pEntry->pStream == NULL || pEntry->fd == 0 || pEntry->fd != descriptor )
    return streamdev_ioerror(dev);

  else if (pEntry->type == STREAM_TYPE_READ)
  {
    HqnReadStream  * pReadStream = (HqnReadStream  *) pEntry->pStream;
    if ( pReadStream->pfClose( pReadStream->pPrivate ) < 0 )
    {
      streamdev_set_last_error( dev, DeviceIOError );
      return -1;
    }
    else
    {
      pEntry->fd = 0;
      streamdev_set_last_error( dev, DeviceNoError );
      return 0;
    }
  }
  else if ( pEntry->type == STREAM_TYPE_WRITE )
  {
    HqnWriteStream  * pWriteStream = (HqnWriteStream  *) pEntry->pStream;
    if ( pWriteStream->pfClose( pWriteStream->pPrivate ) < 0 )
    {
      streamdev_set_last_error( dev, DeviceIOError );
      return -1;
    }
    else
    {
      pEntry->fd = 0;
      streamdev_set_last_error( dev, DeviceNoError );
      return 0;
    }
  }
  else
  {
    return streamdev_ioerror(dev);
  }
}

static int32 RIPCALL streamdev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags )
{
  int32 result, frompos, idx = INDEX_FROM_FD(descriptor);
  StreamEntry * pEntry ;

  if (idx < 0 || idx > MAXIMUM_STREAM_ENTRIES)
    return streamdev_ioerror_false(dev);

  pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[idx]);
  if ( pEntry->pStream == NULL || pEntry->fd == 0 || pEntry->fd != descriptor )
  {
    return streamdev_ioerror_false(dev);
  }

  switch( flags )
  {
    case SW_SET:   frompos = STREAM_POSITION_START   ; break ;
    case SW_INCR:  frompos = STREAM_POSITION_CURRENT ; break ;
    case SW_XTND:  frompos = STREAM_POSITION_END     ; break ;
    default:
      return streamdev_ioerror_false(dev);
  }

  if ( pEntry->type == STREAM_TYPE_READ )
  {
    /* Delegate to the HqnStreamReader. */
    HqnReadStream  * pReadStream = (HqnReadStream  *) pEntry->pStream;
    result = pReadStream->pfSeek( pReadStream->pPrivate, destination, frompos );
  }
  else
  {
    HqnWriteStream  * pWriteStream = (HqnWriteStream  *) pEntry->pStream;
    result = pWriteStream->pfSeek( pWriteStream->pPrivate, destination, frompos );
  }

  if ( !result )
  {
    return streamdev_ioerror_false(dev);
  }
  else
  {
    return result;
  }
}

static int32 RIPCALL streamdev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason )
{
  int32 result, r, idx = INDEX_FROM_FD(descriptor);
  StreamEntry * pEntry ;

  if (idx < 0 || idx > MAXIMUM_STREAM_ENTRIES)
    return streamdev_ioerror_false(dev);

  pEntry = &(((StreamDeviceState*) dev->private_data)->pAllStreams[idx]);
  if ( pEntry->pStream == NULL || pEntry->fd == 0 || pEntry->fd != descriptor )
  {
    return streamdev_ioerror_false(dev);
  }

  switch( reason )
  {
    case SW_BYTES_AVAIL_REL:  r = STREAM_BYTES_AVAILABLE   ; break ;
    case SW_BYTES_TOTAL_ABS:  r = STREAM_BYTES_TOTAL ; break ;
    default:
      return streamdev_ioerror_false(dev);
  }

  if ( pEntry->type == STREAM_TYPE_READ )
  {
    /* Delegate to the HqnStreamReader. */
    HqnReadStream  * pReadStream = (HqnReadStream  *) pEntry->pStream;
    result = pReadStream->pfBytes( pReadStream->pPrivate, bytes, r );
  }
  else
  {
    HqnWriteStream  * pWriteStream = (HqnWriteStream  *) pEntry->pStream;
    result = pWriteStream->pfBytes( pWriteStream->pPrivate, bytes, r );
  }

  if ( !result )
  {
    return streamdev_ioerror_false(dev);
  }
  else
  {
    return result;
  }
}

static int32 RIPCALL streamdev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff )
{
  UNUSED_PARAM( uint8 *, filename );
  UNUSED_PARAM( STAT *, statbuff );

  return streamdev_ioerror( dev );
}

static void* RIPCALL streamdev_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  UNUSED_PARAM( uint8 *, pattern );
  return streamdev_void( dev );
}

static int32 RIPCALL streamdev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *pattern, FILEENTRY *entry )
{
  UNUSED_PARAM( DEVICELIST *, dev );
  UNUSED_PARAM( void **, handle );
  UNUSED_PARAM( uint8 *, pattern );
  UNUSED_PARAM( FILEENTRY *, entry );

  return FileNameNoMatch ;
}

static int32 RIPCALL streamdev_end_file_list( DEVICELIST *dev, void *handle )
{
  UNUSED_PARAM( void **, handle );

  return streamdev_noerror( dev );
}

static int32 RIPCALL streamdev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 )
{
  UNUSED_PARAM( uint8 *, file1 );
  UNUSED_PARAM( uint8 *, file2 );

  return streamdev_ioerror( dev );
}

static int32 RIPCALL streamdev_delete_file( DEVICELIST *dev, uint8 *filename )
{
  UNUSED_PARAM( uint8 *, filename );

  return streamdev_ioerror( dev );
}

static int32 RIPCALL streamdev_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  UNUSED_PARAM( DEVICELIST *, dev );
  UNUSED_PARAM( DEVICEPARAM *, param );

  return ParamIgnored;
}

static int32 RIPCALL streamdev_start_param(DEVICELIST *dev)
{
  streamdev_set_last_error( dev, DeviceNoError );
  return 0 ;
}

static int32 RIPCALL streamdev_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  UNUSED_PARAM( DEVICELIST *, dev );
  UNUSED_PARAM( DEVICEPARAM *, param );

  return ParamIgnored;
}

static int32 RIPCALL streamdev_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  UNUSED_PARAM( DEVSTAT *, devstat );

  return streamdev_ioerror( dev );
}

static int32 RIPCALL streamdev_spare( void )
{
  return 0;
}
