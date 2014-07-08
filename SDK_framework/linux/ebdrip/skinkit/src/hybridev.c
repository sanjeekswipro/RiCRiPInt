/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*
 * $HopeName: SWskinkit!src:hybridev.c(EBDSDK_P.1) $
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of hybrid device intended for a read-only SW folder.
 * on disk. In this case, the SW folder can be shared by more than one RIP.
 *
 * Please note the following restrictions
 * 1) The disk file was assumed read-only. Any attempts to modify it would be
 * done in ram. This implies that the change will not be persistent, ie, lost
 * after each RIP session.
 * 2) Any attempts to delete or rename a disk file is disallowed.
 */

#include <string.h>
#include <stdio.h>

#include "hqstr.h"
#include "mem.h"
#include "memfs.h"
#include "swdevice.h"
#include "skindevs.h"
#include "ripthread.h" /* HYBRID_DEVICE_TYPE */
#include "file.h"      /* LONGESTFILENAME */
#include "sync.h"      /* PKCreateSemaphore, PKDestroySemaphore */
#include "skinkit.h"   /* SwLeJobStart */

#define HYBRIDEV_PARAM_TYPE   0
#define HYBRIDEV_PARAM_PREFIX 1
#define HYBRIDEV_PARAM_COUNT  2

/**
 * \brief externally defined device types of the sub-devices of this device.
 */
extern DEVICETYPE RAM_Device_Type, Fs_Device_Type ;

/**
 * \brief Name of the device parameter for this device.
 */
static uint8* szParamType =   (uint8*) "Type";
static uint8* szParamPrefix = (uint8*) "Prefix";

/**
 * \brief Constant value of the <code>/Type</code> device parameter. This allows
 * the RIP to classify this device as a storage medium.
 */
static uint8* szParamValueFileSystem = (uint8*) "FileSystem";

/**
 * \brief For file descriptors of this device.
 */
typedef struct HybridFile
{
  DEVICE_FILEDESCRIPTOR hybridfd;   /**< File descriptor on HybriDev */
  DEVICELIST       *dev;       /**< Which device the file in on */
  DEVICE_FILEDESCRIPTOR fd;         /**< FD on that device */
} HybridFile;

/** \brief Encapsulates the state of a single instance of this device. */
typedef struct _HybriDeviceState
{
  /** \brief The RAM sub-device. */
  DEVICELIST * ramdev ;

  /** \brief The disk sub-file device */
  DEVICELIST * diskdev ;

  /** \brief Relative path to root. */
  uint8     prefix[ LONGESTFILENAME + 1 ];

  /** \brief Semaphore to protect concurrent calls. */
  void     *pSema;

  /** \brief Index ticker for the stateful <code>start_param</code> and
   *  <code>get_param</code> interfaces. */
  int32     iParam;
} HybriDeviceState;

/** \brief Iteration state of this device. */
typedef struct _HybridDeviceIterate
{
  int32        iDev ;       /**< current sub-device index: 0: ram 1: disk */
  DEVICELIST * subDevs[2];  /**< pointing to the sub-devices. */
  void       * handle ;     /**< Iteration handle of the current sub-device */
} HybridDeviceIterate ;

/* Device API */
static int32 RIPCALL hybridev_ioerror( DEVICELIST *dev );
static int32 RIPCALL hybridev_noerror( DEVICELIST *dev );
static int32 RIPCALL hybridev_init_device( DEVICELIST *dev );
static int32 RIPCALL hybridev_dismount_device( DEVICELIST *dev );
static DEVICE_FILEDESCRIPTOR RIPCALL hybridev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags );
static int32 RIPCALL hybridev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );
static int32 RIPCALL hybridev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );
static int32 RIPCALL hybridev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL hybridev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags );
static int32 RIPCALL hybridev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason );
static int32 RIPCALL hybridev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff );
static void* RIPCALL hybridev_start_file_list( DEVICELIST *dev, uint8 *pattern );
static int32 RIPCALL hybridev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *patter, FILEENTRY *entry );
static int32 RIPCALL hybridev_end_file_list( DEVICELIST *dev, void *handle );
static int32 RIPCALL hybridev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 );
static int32 RIPCALL hybridev_delete_file( DEVICELIST *dev, uint8 *filename );
static int32 RIPCALL hybridev_set_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL hybridev_start_param(DEVICELIST *dev);
static int32 RIPCALL hybridev_get_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL hybridev_status_device( DEVICELIST *dev, DEVSTAT *devstat );
static int32 RIPCALL hybridev_ioctl
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, int32 opcode, intptr_t arg );
static int32 RIPCALL hybridev_spare( void );

/** \brief The RAM input device type structure. */
DEVICETYPE Hybrid_Device_Type = {
#ifdef USE_HYBRID_SW_FOLDER
  OS_DEVICE_TYPE,              /**< the device ID number */
#else
  HYBRID_DEVICE_TYPE,          /**< the device ID number */
#endif
  DEVICERELATIVE,              /**< flags to indicate specifics of device */
  CAST_SIZET_TO_INT32(sizeof( HybriDeviceState )), /**< the size of the private data */
  0,                           /**< minimum ticks between tickle functions */
  NULL,                        /**< procedure to service the device */
  skindevices_last_error,      /**< return last error for this device */
  hybridev_init_device,        /**< call to initialise device */
  hybridev_open_file,          /**< call to open file on device */
  hybridev_read_file,          /**< call to read data from file on device */
  hybridev_write_file,         /**< call to write data to file on device */
  hybridev_close_file,         /**< call to close file on device */
  hybridev_close_file,         /**< call to abort action on the device */
  hybridev_seek_file,          /**< call to seek file on device */
  hybridev_bytes_file,         /**< call to get bytes avail on an open file */
  hybridev_status_file,        /**< call to check status of file */
  hybridev_start_file_list,    /**< call to start listing files */
  hybridev_next_file,          /**< call to get next file in list */
  hybridev_end_file_list,      /**< call to end listing */
  hybridev_rename_file,        /**< rename file on the device */
  hybridev_delete_file,        /**< remove file from device */
  hybridev_set_param,          /**< call to set device parameter */
  hybridev_start_param,        /**< call to start getting device parameters */
  hybridev_get_param,          /**< call to get the next device parameter */
  hybridev_status_device,      /**< call to get the status of the device */
  hybridev_dismount_device,    /**< call to dismount the device */
  hybridev_ioerror,            /**< call to return buffer size */
  hybridev_ioctl,              /**< ioctl slot (optional) */
  hybridev_spare,              /**< spare slot */
};

/* Declaration of other local functions */
static int32 mountSubDevice(DEVICELIST *dev, DEVICELIST ** pSub, char * subname,  DEVICETYPE * subdevtype );
static void dismountSubDevice(DEVICELIST ** pSub);
static DEVICE_FILEDESCRIPTOR openFileAndRegister(DEVICELIST *dev, uint8 *filename, int32 openflags );
static int32 loadFileFromDiskToRam( DEVICELIST *dev, uint8 *filename);

static void hybridev_set_last_error(DEVICELIST *dev, int32 error)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(error);
}

static int32 RIPCALL hybridev_ioerror( DEVICELIST *dev )
{
  hybridev_set_last_error(dev, DeviceIOError);
  return -1;
}

static int32 RIPCALL hybridev_noerror( DEVICELIST *dev )
{
  hybridev_set_last_error(dev, DeviceNoError);
  return 0;
}

/** \brief Mount ram or disk sub-device for the hybrid device.  Note that
 * the mount is different from the standard "mount" in that the device is
 * not added to the global device list. It remains local to this device and
 * cannot be found by name outside.
 */
static int32 mountSubDevice(DEVICELIST *dev, DEVICELIST ** pSub, char * subname,  DEVICETYPE * subdevtype )
{
  char abzDevName[256];

  /* Mount the ram sub-device */
  sprintf(abzDevName, "%s-%s", (char*) theIDevName(dev), subname);
  *pSub = (DEVICELIST *) MemAlloc (sizeof(DEVICELIST) + strlen_uint32(abzDevName) + 1, TRUE, FALSE);
  if (*pSub == NULL)
  {
    hybridev_set_last_error(dev, DeviceVMError);
    return FALSE;
  }
  (void) strncpy ((char*)(*pSub) + sizeof(DEVICELIST), abzDevName, strlen(abzDevName));
  theIDevName( *pSub ) = (uint8*)(*pSub) + sizeof(DEVICELIST);
  theIDevType( *pSub ) = subdevtype ;
  theIDeviceFlags( *pSub ) = theIDevTypeFlags(subdevtype) ;

  /* Allocate private storage, if necessary */
  if ( theIDevTypePrivSize( subdevtype ) > 0 )
  {
    theIPrivate(*pSub) = (uint8 *) MemAlloc (theIDevTypePrivSize(subdevtype), TRUE, FALSE);
    if (theIPrivate(*pSub) == NULL)
    {
      hybridev_set_last_error(dev, DeviceVMError);
      return FALSE;
    }
  }

  if ( (*theIDevInit(*pSub))(*pSub) < 0 )
  {
    hybridev_set_last_error(dev, (*theILastErr(*pSub))(*pSub)) ;
    return FALSE ;
  }

  return TRUE ;
}

/** \brief Dismount ram or disk sub-device for the hybrid device.  */
static void dismountSubDevice(DEVICELIST ** pSub)
{
  if (*pSub)
  {
    (*theIDevDismount(*pSub))(*pSub);

    if ( theIPrivate(*pSub) != NULL )
      MemFree(theIPrivate(*pSub));

    MemFree(*pSub);
    *pSub = NULL;
  }
}

static int32 RIPCALL hybridev_init_device( DEVICELIST *dev )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;

  (void) hybridev_noerror(dev) ; /* set to no error */

  pState->iParam = 0;
  pState->prefix[0] = '\0';
  pState->ramdev = pState->diskdev = NULL;

  if ( (pState->pSema = PKCreateSemaphore( 1 )) == NULL ||
       !mountSubDevice(dev, &pState->ramdev, "ram", &RAM_Device_Type) ||
       !mountSubDevice(dev, &pState->diskdev, "disk", &Fs_Device_Type) )
  {
    /* Dismount clears the error, so restore the error afterwards. */
    int32 last_error = skindevices_last_error(dev);
    (void) hybridev_dismount_device( dev );
    hybridev_set_last_error(dev, last_error);
    return -1;
  }
  return 0;
}

static int32 RIPCALL hybridev_dismount_device( DEVICELIST *dev )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  if (pState->pSema)
  {
    PKDestroySemaphore( pState->pSema );
    pState->pSema = NULL ;
  }
  dismountSubDevice(&pState->ramdev);
  dismountSubDevice(&pState->diskdev);
  return 0;
}

/**
 * \brief Open the file on sub-device, and register that in this device's
 * state.
 */

static DEVICE_FILEDESCRIPTOR openFileAndRegister(DEVICELIST *dev, uint8 *filename,
                      int32 openflags )
{
  DEVICE_FILEDESCRIPTOR fd = 0;
  HybridFile * phf = MemAlloc(sizeof(HybridFile), TRUE, FALSE);

  if (phf == NULL || ((fd = (*theIOpenFile(dev))(dev, filename, openflags)) == -1))
  {
    if (phf != NULL)
      MemFree(phf );
    if (fd == -1)
      hybridev_set_last_error(dev, (*theILastErr( dev ))( dev )) ;
    else
      hybridev_set_last_error(dev, DeviceVMError);
    return -1;
  }

  phf->hybridfd = VOIDPTR_TO_DEVICE_FILEDESCRIPTOR(phf);
  phf->dev = dev;
  phf->fd = fd;
  return phf->hybridfd;
}

/**
 * \brief Loading the file from disk to ram. This is called when a disk
 * file is opened for write. We disallow disk files to be modified. If
 * such a request arises, we copy the file to ram and the modification
 * is carried out on the ram copy.
 */
static int32 loadFileFromDiskToRam( DEVICELIST *dev, uint8 *filename)
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  DEVICE_FILEDESCRIPTOR ramid = -1, diskid = -1;
  int32 result = 0, len;
  uint8 buff[2048];

  if ((ramid = (*theIOpenFile(pState->ramdev))(pState->ramdev, filename,
                 SW_WRONLY | SW_CREAT | SW_TRUNC)) == -1)
  {
    /* Cannot create this file in RAM */
    result = -1;
    hybridev_set_last_error(dev, (*theILastErr( pState->ramdev ))( pState->ramdev )) ;
  }
  else if ((diskid = (*theIOpenFile(pState->diskdev))(pState->diskdev, filename, SW_RDONLY)) == -1)
  {
    /* Cannot read this file from disk */
    result = -1;
    hybridev_set_last_error(dev, (*theILastErr( pState->diskdev ))( pState->diskdev )) ;
  }
  if (result != -1)
  {
    /* Copy the file from disk to ram */
    while ((len = ( *theIReadFile( pState->diskdev))(pState->diskdev, diskid, buff, sizeof(buff))) > 0 )
    {
      result = (*theIWriteFile(pState->ramdev))(pState->ramdev,ramid, buff, len);
      if (result == -1)
      {
        hybridev_set_last_error(dev, (*theILastErr( pState->ramdev ))( pState->ramdev )) ;
        break ;
      }
    }
  }
  if (ramid != -1)
  {
    result = ( *theICloseFile( pState->ramdev))(pState->ramdev, ramid);
    ramid = -1;
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pState->ramdev ))( pState->ramdev )) ;
  }
  if (diskid != -1)
  {
    result = ( *theICloseFile( pState->diskdev))(pState->diskdev, diskid);
    diskid = -1;
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pState->diskdev ))( pState->diskdev )) ;
  }
  return result ;
}

static DEVICE_FILEDESCRIPTOR RIPCALL hybridev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  DEVICE_FILEDESCRIPTOR result;
  STAT stat;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );

  /* If file is in memory, open from there */
  if ((*theIStatusFile(pState->ramdev))(pState->ramdev, filename, &stat) != -1)
    result = openFileAndRegister(pState->ramdev, filename, openflags);
  else if ((*theIStatusFile(pState->diskdev))(pState->diskdev, filename, &stat) != -1)
  {
    /* If the file is on disk, we open through disk device. But we only allow
     * read only access if the file is on disk.
     */
    if (openflags & SW_RDONLY)
    {
      result = openFileAndRegister(pState->diskdev, filename, openflags);
    }
    else
    {
      /* If an attempt is makde to a disk file, we load the file from disk to
       * memory, and write on the copy.
       */
      if ((result = loadFileFromDiskToRam(dev, filename)) != -1)
        result = openFileAndRegister(pState->ramdev, filename, openflags);
    }
  }
  else
  {
    /* File is not anywhere, can be new - create them in ram. */
    result = openFileAndRegister(pState->ramdev, filename, openflags);
  }

  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL hybridev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridFile * pFD;
  int32 result;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor);

  if (pFD == NULL)
  {
    /* Invalid file descriptor */
    result = -1;
    hybridev_set_last_error(dev, DeviceIOError) ;
  }
  else
  {
    result = ( *theIReadFile( pFD->dev))(pFD->dev, pFD->fd, buff, len);
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pFD->dev ))( pFD->dev )) ;
  }
  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL hybridev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridFile *pFD;
  int32 result;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor);
  if (pFD == NULL)
  {
    /* Invalid file descriptor */
    result = -1;
    hybridev_set_last_error(dev, DeviceIOError) ;
  }
  else
  {
    result = (*theIWriteFile(pFD->dev))(pFD->dev, pFD->fd, buff, len);
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pFD->dev ))( pFD->dev )) ;
  }
  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL hybridev_ioctl
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, int32 opcode, intptr_t arg )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridFile *pFD;
  int32 result;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor);
  if (pFD == NULL)
  {
    /* Invalid file descriptor */
    result = -1;
    hybridev_set_last_error(dev, DeviceIOError) ;
  }
  else if ( theIIoctl( pFD->dev ) == NULL )
  {
    /* No-op, because the underlying device has no ioctl function. */
    result = 0;
  }
  else
  {
    result = (*theIIoctl(pFD->dev))(pFD->dev, pFD->fd, opcode, arg);
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pFD->dev ))( pFD->dev )) ;
  }
  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL hybridev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  int32 result;
  HybridFile *pFD;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );

  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor);
  if (pFD == NULL)
  {
    /* Invalid file descriptor */
    result = -1;
    hybridev_set_last_error(dev, DeviceIOError) ;
  }
  else
  {
    result = ( *theICloseFile( pFD->dev))(pFD->dev, pFD->fd);
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pFD->dev ))( pFD->dev )) ;
    else
    {
       MemFree(pFD);
       result = 0;
    }
  }
  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL hybridev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridFile *pFD;
  int32 result;

  (void) hybridev_noerror(dev) ; /* clear error flag first */
  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor);
  if (pFD == NULL)
  {
    /* Invalid file descriptor */
    result = FALSE;
    hybridev_set_last_error(dev, DeviceIOError) ;
  }
  else
  {
    result = ( *theISeekFile( pFD->dev))(pFD->dev, pFD->fd, destination, flags);
    if (result == FALSE)
      hybridev_set_last_error(dev, (*theILastErr( pFD->dev ))( pFD->dev )) ;
  }
  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL hybridev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridFile *pFD;
  int32 result;

  (void) hybridev_noerror(dev) ; /* clear error flag first */
  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(descriptor);

  if (pFD == NULL)
  {
    /* Invalid file descriptor */
    result = FALSE;
    hybridev_set_last_error(dev, DeviceIOError) ;
  }
  else
  {
    result = ( *theIBytesFile( pFD->dev))(pFD->dev, pFD->fd, bytes, reason);
    if (result == FALSE)
      hybridev_set_last_error(dev, (*theILastErr( pFD->dev ))( pFD->dev ));
  }
  PKSignalSemaphore( pState->pSema );

  return TRUE;
}

static int32 RIPCALL hybridev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  int32 result;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );

  /* Try ram first */
  result = (*theIStatusFile(pState->ramdev))(pState->ramdev, filename, statbuff);
  /* Now try disk if not in ram */
  if (result == -1)
    result = (*theIStatusFile(pState->diskdev))(pState->diskdev, filename, statbuff);

  PKSignalSemaphore( pState->pSema );
  return result ;
}

static void* RIPCALL hybridev_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridDeviceIterate * pIter = NULL;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  if ((pIter = (HybridDeviceIterate *) MemAlloc( sizeof(HybridDeviceIterate), TRUE, FALSE )) == NULL)
  {
    hybridev_set_last_error(dev, DeviceVMError);
    return NULL;
  }
  pIter->subDevs[0] = pState->ramdev;
  pIter->subDevs[1] = pState->diskdev;

  PKWaitOnSemaphore( pState->pSema );

  pIter->handle = (*theIStartList(pIter->subDevs[pIter->iDev]))(pIter->subDevs[pIter->iDev], pattern);
  if (pIter->handle == NULL)
  {
    pIter->iDev ++ ;
    pIter->handle = (*theIStartList(pIter->subDevs[pIter->iDev]))(pIter->subDevs[pIter->iDev], pattern);
    if (pIter->handle == NULL)
    {
      MemFree(pIter);
      hybridev_set_last_error(dev, (*theILastErr( pIter->subDevs[pIter->iDev] ))( pIter->subDevs[pIter->iDev] ));
      pIter = NULL;
    }
  }

  PKSignalSemaphore( pState->pSema );
  return (void*) pIter;
}

static int32 RIPCALL hybridev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *pattern, FILEENTRY *entry )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  HybridDeviceIterate * pIter = (HybridDeviceIterate *) (*handle);
  int32 result = FileNameNoMatch ;

  PKWaitOnSemaphore( pState->pSema );

  result = (*theINextList(pIter->subDevs[pIter->iDev]))(pIter->subDevs[pIter->iDev],
            &(pIter->handle), pattern, entry);
  if (result != FileNameMatch && (pIter->iDev == 0))
  {
    void * hNext = NULL ;
    /* No more file on ram device, try disk one */
    hNext = (*theIStartList(pIter->subDevs[1]))(pIter->subDevs[1], pattern);
    if (hNext != NULL)
    {
      /* Switch to the disk device */
      (void) (*theIEndList(pIter->subDevs[0]))(pIter->subDevs[0], pIter->handle);
      pIter->iDev = 1;
      pIter->handle = hNext ;
      result = (*theINextList(pIter->subDevs[1]))(pIter->subDevs[1], &(pIter->handle), pattern, entry);
    }
  }

  if (pIter->iDev == 1)
  {
    STAT stat;
    /* Make sure that the file is not in RAM as well. */
    while (result == FileNameMatch &&
          (*theIStatusFile(pState->ramdev))(pState->ramdev, entry->name, &stat) != -1 )
    {
      result = (*theINextList(pIter->subDevs[1]))(pIter->subDevs[1], &(pIter->handle), pattern, entry);
    }
  }

  if (result != FileNameMatch)
    hybridev_set_last_error(dev, (*theILastErr( pIter->subDevs[pIter->iDev] ))( pIter->subDevs[pIter->iDev] ));

  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL hybridev_end_file_list( DEVICELIST *dev, void *handle )
{
  HybridDeviceIterate * pIter = (HybridDeviceIterate *) handle;
  int32 result = 0;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  if (pIter != NULL)
  {
    result = (*theIEndList(pIter->subDevs[pIter->iDev]))(pIter->subDevs[pIter->iDev], pIter->handle);
    MemFree( pIter );
  }
  return result;
}

static int32 RIPCALL hybridev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  int32 result = 0;
  STAT  stat;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  /* We disallow disk file (or ram file which was a copy of the disk file)
   * from being renamed. Moreover, the new name must not be used on disk.
   */
  PKWaitOnSemaphore( pState->pSema );
  if ((*theIStatusFile(pState->diskdev))(pState->diskdev, (uint8*)file1, &stat) == -1 &&
       (*theIStatusFile(pState->diskdev))(pState->diskdev, (uint8*)file2, &stat) == -1)
  {
    result = (*theIRenameFile(pState->ramdev))(pState->ramdev, file1, file2);
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pState->ramdev ))( pState->ramdev )) ;
  }
  else
  {
    result = -1;
    hybridev_set_last_error(dev, DeviceInvalidAccess) ;
  }
  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL hybridev_delete_file( DEVICELIST *dev, uint8 *filename )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  int32 result = 0;
  STAT  stat;

  (void) hybridev_noerror(dev) ; /* clear error flag first */

  /* We disallow disk file (or ram file which was a copy of the disk file)
   * from being deleted.
   */
  PKWaitOnSemaphore( pState->pSema );
  if ((*theIStatusFile(pState->diskdev))(pState->diskdev, (uint8*)filename, &stat) == -1)
  {
    result = (*theIDeleteFile(pState->ramdev))(pState->ramdev, filename);
    if (result == -1)
      hybridev_set_last_error(dev, (*theILastErr( pState->ramdev ))( pState->ramdev )) ;
  }
  else
  {
    result = -1;
    hybridev_set_last_error(dev, DeviceInvalidAccess) ;
  }
  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL hybridev_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  int32 length = strlen_int32 ( (char *) szParamPrefix );
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;

  (void) hybridev_noerror(dev) ; /* clear error flag first */
  if ( param->paramnamelen == length &&
       strncmp((char *)param->paramname,
               (char *)szParamPrefix, CAST_SIGNED_TO_SIZET(length)) == 0)
  {
    /* otherwise change file prefix if it is valid: */
    if ( param->type != ParamString ) {
      hybridev_set_last_error(dev, DeviceIOError) ;
      return ParamTypeCheck;
    }
    if ( param->strvallen > LONGESTFILENAME ) {
      hybridev_set_last_error(dev, DeviceIOError) ;
      return ParamRangeCheck;
    }

    if (param->strvallen > 0 )
    {
      int32 result = ParamAccepted;
      memcpy(pState->prefix, param->paramval.strval, CAST_SIGNED_TO_SIZET(param->strvallen));
      pState->prefix[ param->strvallen ] = '\0';
      /* pass on the prefix to child devices */
      result = (*theISetParam(pState->ramdev))(pState->ramdev, param);
      if (result == ParamAccepted)
        result = (*theISetParam(pState->diskdev))(pState->diskdev, param);
      return result ;
    }
  }
  else
  {
    hybridev_set_last_error(dev, DeviceNoError) ;
    return ParamIgnored;
  }

  return ParamAccepted;
}

static int32 RIPCALL hybridev_start_param(DEVICELIST *dev)
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  (void) hybridev_noerror(dev) ; /* clear error flag first */
  pState->iParam = 0;
  return HYBRIDEV_PARAM_COUNT ;
}

static int32 RIPCALL hybridev_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  HybriDeviceState *pState = (HybriDeviceState*) dev->private_data;
  (void) hybridev_noerror(dev) ; /* clear error flag first */

  if ( param->paramname == NULL )
  {
    /* The RIP is part way through asking for all parameters. */
    switch ( pState->iParam++ )
    {
      case HYBRIDEV_PARAM_TYPE:
        param->paramname = szParamType;
        param->paramnamelen = strlen_int32( (char*) szParamType );
        param->type = ParamString;
        param->paramval.strval = szParamValueFileSystem;
        param->strvallen = strlen_int32( (char*) szParamValueFileSystem );
        return ParamAccepted;

      case HYBRIDEV_PARAM_PREFIX:
        param->paramname = szParamPrefix;
        param->paramnamelen = strlen_int32( (char*) szParamPrefix );
        param->type = ParamString;
        param->paramval.strval = pState->prefix;
        param->strvallen = strlen_int32( (char*) pState->prefix );
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
  else if ( strncmp( (char*) param->paramname, (char*) szParamPrefix,
                     CAST_SIGNED_TO_SIZET(param->paramnamelen) ) == 0 )
  {
    /* The RIP is asking for the "Prefix" parameter specifically. */
    param->type = ParamString;
    param->paramval.strval = pState->prefix;
    param->strvallen = strlen_int32( (char*) pState->prefix );
    return ParamAccepted;
  }
  else
  {
    /* The RIP is asking for a parameter not defined by this device. */
    return ParamIgnored;
  }
}

static int32 RIPCALL hybridev_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  devstat->block_size = 1024;
  devstat->start = NULL;
  HqU32x2FromInt32( &(devstat->size), 0 );
  HqU32x2FromInt32( &(devstat->free), 0 );

  return hybridev_noerror( dev );
}

static int32 RIPCALL hybridev_spare( void )
{
  return 0;
}

