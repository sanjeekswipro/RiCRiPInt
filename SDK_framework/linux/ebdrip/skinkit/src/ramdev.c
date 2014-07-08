/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*
 * $HopeName: SWskinkit!src:ramdev.c(EBDSDK_P.1) $
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of an input/output device tied to in-memory data.
 *
 * Useful for backending the os device for embedded systems for which
 * there is no disk, or to help avoid multiple RIP implementations
 * writing caches to a single shared installation.
 */

#include <string.h>
#include <stdio.h>

#include "hqstr.h"
#include "mem.h"
#include "memfs.h"
#include "swdevice.h"
#include "skindevs.h"
#include "ripthread.h" /* RAM_Device_Type */
#include "file.h"      /* LONGESTFILENAME */
#include "sync.h"      /* PKCreateSemaphore, PKDestroySemaphore */

#define RAMDEV_PARAM_TYPE   0
#define RAMDEV_PARAM_PREFIX 1
#define RAMDEV_PARAM_COUNT  2

/**
 * @brief Name of the single, read-only device parameter for this device.
 */
static uint8* szParamType =   (uint8*) "Type";
static uint8* szParamPrefix = (uint8*) "Prefix";

/**
 * @brief Constant value of the <code>/Type</code> device parameter. This allows
 * the RIP to classify this device as a storage medium.
 */
static uint8* szParamValueFileSystem = (uint8*) "FileSystem";
#ifdef USE_RAM_SW_FOLDER
extern MFSNODE *pSWRam ;
#else
#if defined(USE_HYBRID_SW_FOLDER) || defined(USE_ZIP_HYBRID_SW_FOLDER)
/* Created in init_device */
MFSDIR rootDir = { 0, NULL };

/** The root node has a name ("root"), but this is not actually a part of the
 * pathname for any file in this tree, because all paths are expressed
 * relative to the root node.
 */
MFSNODE hybridRoot = { MFS_Directory, FALSE, FALSE, "root", NULL, &rootDir };
#else
/**
 * FOR DEMONSTRATION PURPOSES
 * The following declarations describe a simple filesystem which could serve as
 * the initial state for the RAM device.
 *
 * The demonstration tree contains the single file
 * ps/simple.ps, which contains PostScript code to output the string
 * "Hello, World". This could be executed, for example, with the
 * fragment:-
 *
 *     (%ram%/ps/simple.ps) run
 *
 * The single demonstration file is read-only. However, the directories are
 * writable, so it is possible to expand the device with new files.
 */

uint8 demoData[] =
{ '(', 'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '!', ')', ' ', '=' };

MFSFILE demoFile =
{ sizeof( demoData ), demoData, 0, 0, FALSE, sizeof( demoData ), FALSE, 0,
  FALSE, NULL, FALSE };

MFSNODE demoFileNode = { MFS_File, TRUE, FALSE, "simple.ps", &demoFile, NULL };
MFSNODE *demoEntries[] = { &demoFileNode };
MFSDIR demoDir = { 1, demoEntries, FALSE };
MFSNODE demoDirNode = { MFS_Directory, FALSE, FALSE, "ps", NULL, &demoDir };

MFSNODE *rootEntries[] = { &demoDirNode };
MFSDIR rootDir = { 1, rootEntries, FALSE };

/** The root node has a name ("root"), but this is not actually a part of the
 * pathname for any file in this tree, because all paths are expressed
 * relative to the root node.
 */
MFSNODE demoRoot = { MFS_Directory, FALSE, FALSE, "root", NULL, &rootDir };
#endif
#endif

/**
 * \brief This is the root of the RAM device's virtual filesystem.
 *
 * The current implementation of the RAM device permits for only a single
 * filesystem root. However, like the file device, the RAM device supports
 * prefixes. Therefore, even with a single root, it is possible for multiple
 * instances of the RAM device to exist independantly of each other, using
 * prefixes to navigate down to a specified node under the master root.
 *
 * This variable is initialized to \c NULL, which allows the RAM device
 * to detect when its first instance is being mounted. This is the point
 * at which it chooses the master root node, whose pointer is assigned
 * to this variable. When subsequent instances are mounted, the same root
 * pointer is re-used. See \c ramdev_init_device().
 *
 * The master root pointer is decided based on the configuration of the
 * RIP skin library. If the SW folder is on disk, the role of the RAM
 * device is minor: it just becomes another example device. In this case,
 * it is initialized to the \c demoRoot. If the SW folder is compiled into
 * memory, the RAM device takes on the role of the RIP's primary
 * resource storage device (the "os" device). In this case, the root node
 * is \c pSWRam, which is the root of the imported SW folder data. Finally,
 * if the SW folder is used in "hybrid" mode (where the SW folder exists
 * on disk, but is read-only), the RAM device is initialized to an empty
 * root, which becomes populated as the SW folder is modified, since this
 * is not permitted on disk.
 */
static MFSNODE * pMFSRoot = NULL;

static int32 RIPCALL ramdev_ioerror( DEVICELIST *dev );

static int32 RIPCALL ramdev_ioerror_false( DEVICELIST *dev );

static int32 RIPCALL ramdev_noerror( DEVICELIST *dev );

static int32 RIPCALL ramdev_init_device( DEVICELIST *dev );

static int32 RIPCALL ramdev_dismount_device( DEVICELIST *dev );

static DEVICE_FILEDESCRIPTOR RIPCALL ramdev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags );

static int32 RIPCALL ramdev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );

static int32 RIPCALL ramdev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len );

static int32 RIPCALL ramdev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );

static int32 RIPCALL ramdev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags );

static int32 RIPCALL ramdev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason );

static int32 RIPCALL ramdev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff );

static void* RIPCALL ramdev_start_file_list( DEVICELIST *dev, uint8 *pattern );

static int32 RIPCALL ramdev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *patter, FILEENTRY *entry );

static int32 RIPCALL ramdev_end_file_list( DEVICELIST *dev, void *handle );

static int32 RIPCALL ramdev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 );

static int32 RIPCALL ramdev_delete_file( DEVICELIST *dev, uint8 *filename );

static int32 RIPCALL ramdev_set_param( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL ramdev_start_param(DEVICELIST *dev);

static int32 RIPCALL ramdev_get_param( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL ramdev_status_device( DEVICELIST *dev, DEVSTAT *devstat );

static int32 RIPCALL ramdev_ioctl
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, int32 opcode, intptr_t arg );

static int32 RIPCALL ramdev_spare( void );

/** @brief Encapsulates the state of a single instance of the RAM device. */
typedef struct _RAMDeviceState
{
  /** @brief Points to the root of the Memory File System for this RAM device. */
  MFSNODE  *pMFSRoot;

  /** @brief Indicates whether \c pMFSRoot should be released when the device
      is dismounted. */
  uint32    fReleaseRootOnDismount;

  /** @brief Relative path to root. */
  uint8     prefix[ LONGESTFILENAME + 1 ];

  /** @brief Semaphore to protect concurrent calls. */
  void     *pSema;

  /** @brief Index ticker for the stateful <code>start_param</code> and
   *  <code>get_param</code> interfaces. */
  int32     iParam;
} RAMDeviceState;

/** \brief The RAM input device type structure. */
DEVICETYPE RAM_Device_Type = {
#if defined(USE_RAM_SW_FOLDER) || defined(USE_ZIP_HYBRID_SW_FOLDER)
  OS_DEVICE_TYPE,            /**< the device ID number */
#else
  RAM_DEVICE_TYPE,            /**< the device ID number */
#endif
  DEVICERELATIVE,            /**< flags to indicate specifics of device */
  CAST_SIZET_TO_INT32(sizeof(RAMDeviceState)), /**< the size of the private data */
  0,                         /**< minimum ticks between tickle functions */
  NULL,                      /**< procedure to service the device */
  skindevices_last_error,    /**< return last error for this device */
  ramdev_init_device,        /**< call to initialise device */
  ramdev_open_file,          /**< call to open file on device */
  ramdev_read_file,          /**< call to read data from file on device */
  ramdev_write_file,         /**< call to write data to file on device */
  ramdev_close_file,         /**< call to close file on device */
  ramdev_close_file,         /**< call to abort action on the device */
  ramdev_seek_file,          /**< call to seek file on device */
  ramdev_bytes_file,         /**< call to get bytes avail on an open file */
  ramdev_status_file,        /**< call to check status of file */
  ramdev_start_file_list,    /**< call to start listing files */
  ramdev_next_file,          /**< call to get next file in list */
  ramdev_end_file_list,      /**< call to end listing */
  ramdev_rename_file,        /**< rename file on the device */
  ramdev_delete_file,        /**< remove file from device */
  ramdev_set_param,          /**< call to set device parameter */
  ramdev_start_param,        /**< call to start getting device parameters */
  ramdev_get_param,          /**< call to get the next device parameter */
  ramdev_status_device,      /**< call to get the status of the device */
  ramdev_dismount_device,    /**< call to dismount the device */
  ramdev_ioerror,            /**< call to return buffer size */
  ramdev_ioctl,              /**< ioctl slot (optional) */
  ramdev_spare,              /**< spare slot */
};

static void ramdev_set_lasterror(DEVICELIST *dev, int32 error)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(error);
}

static int32 RIPCALL ramdev_ioerror( DEVICELIST *dev )
{
  ramdev_set_lasterror(dev, DeviceIOError);
  return -1;
}

static int32 RIPCALL ramdev_ioerror_false( DEVICELIST *dev )
{
  ramdev_set_lasterror(dev, DeviceIOError);
  return FALSE ;
}

static int32 RIPCALL ramdev_noerror( DEVICELIST *dev )
{
  ramdev_set_lasterror(dev, DeviceNoError);
  return 0;
}

static int32 RIPCALL ramdev_init_device( DEVICELIST *dev )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;

  (void) ramdev_noerror(dev) ; 
  
  pState->iParam = 0;
  pState->prefix[0] = '\0';
  
  /* If pMFSRoot is NULL, then we are mounting the first instance of
     the RAM device, so we need to choose a root node. This depends on
     how access to the SW folder has been configured. */
  if (pMFSRoot == NULL)
  {
#ifdef USE_RAM_SW_FOLDER
    /* The root of the RAM device is the root of the in-memory SW folder,
       created by the Import Tool. */
    pMFSRoot = pSWRam;
#else
#if defined(USE_HYBRID_SW_FOLDER) || defined(USE_ZIP_HYBRID_SW_FOLDER)
    /* In this case, the RAM device manages all write access to the SW
       folder, while the regular file device manages read access. Set
       the root to an initially empty directory. */
    pMFSRoot = &hybridRoot ;
#else
    /* The RAM device has no special role here, because the SW folder is on
       disk, and is managed wholly by the file device. Just mount a simple
       demo tree. */
    pMFSRoot = &demoRoot;
#endif
#endif

    /* In all the above cases, pMFSRoot is assigned from statically-defined
       (or imported) data. Mutations to this structure will use RIP-managed
       memory, which becomes invalid when the RIP shuts down. Therefore,
       make a copy of the tree so that modifications do not occur in situ.
       File data itself need not be copied here. Only the tree structure
       needs to be copied. */
    pMFSRoot = MFSCopyTree( pMFSRoot, FALSE );

    if ( pMFSRoot == NULL )
    {
      /* Abort initialization if we couldn't take a safe copy. */
      ramdev_set_lasterror(dev, DeviceVMError);
      return -1;
    }

    /* We have done MFSCopyTree to make a new tree, so record that
       the tree has to be freed when the device is dismounted. This will
       cause ramdev_dismount_device() to make the corresponding
       MFSReleaseRoot(). */
    pState->fReleaseRootOnDismount = TRUE;
  }
  else
  {
    /* We have mounted this ramdev against an existing filesystem tree,
       so don't free it on the dismount. */
    pState->fReleaseRootOnDismount = FALSE;
  }

  pState->pMFSRoot = pMFSRoot;

  if ( (pState->pSema = PKCreateSemaphore( 1 )) == NULL )
  {
    (void) ramdev_dismount_device( dev );
    ramdev_set_lasterror(dev, DeviceVMError);
    return -1;
  }

  return 0;
}

static int32 RIPCALL ramdev_dismount_device( DEVICELIST *dev )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

  if ( pState->fReleaseRootOnDismount )
    MFSReleaseRoot( pState->pMFSRoot );

  if (pState->pSema)
  {
    PKDestroySemaphore( pState->pSema );
    pState->pSema = NULL ;
  }

  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL ramdev_open_file
  ( DEVICELIST *dev, uint8 *filename, int32 openflags )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSFILEDESC *pDescriptor;
  DEVICE_FILEDESCRIPTOR result;
  uint8 *pNameBuffer = NULL;
  int32 error = DeviceNoError;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

  if (pState->prefix[0])
  {
    pNameBuffer = (uint8*) MemAlloc( strlen_uint32((char*) pState->prefix) + 
                                     strlen_uint32((char*) filename) + 2, TRUE, FALSE ); 
    if (pNameBuffer == NULL)
    {
      ramdev_set_lasterror(dev, DeviceVMError);
      return -1;
    }
    strcpy( (char*)pNameBuffer, (char*)pState->prefix );
    if (pState->prefix[strlen((char*) pState->prefix)-1] != '/')
      strcat( (char*)pNameBuffer, "/" );
    strcat( (char*)pNameBuffer, (char*)filename);
  }
  else
    pNameBuffer = filename ;

  PKWaitOnSemaphore( pState->pSema );

  if ( !MFSOpen( pState->pMFSRoot, (char*) pNameBuffer, openflags, &pDescriptor, &error ) )
  {
    result = -1;
  }
  else
    result = VOIDPTR_TO_DEVICE_FILEDESCRIPTOR(pDescriptor);

  ramdev_set_lasterror(dev, error);

  PKSignalSemaphore( pState->pSema );

  if (pState->prefix[0] && pNameBuffer) 
    MemFree(pNameBuffer);

  return result;
}

static int32 RIPCALL ramdev_read_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSFILEDESC *pFD;
  int32 result;
  int32 error = DeviceNoError;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
  result = MFSRead( pFD, buff, len, &error );
  ramdev_set_lasterror(dev, error);
  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL ramdev_write_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSFILEDESC *pFD;
  int32 result;
  int32 error = DeviceNoError;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  
  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
  result = MFSWrite( pFD, buff, len, &error );
  ramdev_set_lasterror(dev, error);
  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL ramdev_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  int32 result;
  MFSFILEDESC *pFD;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

  PKWaitOnSemaphore( pState->pSema );

  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );

  if ( !MFSClose( pFD ) )
    result = ramdev_ioerror( dev );
  else
  {
    result = 0;
  }

  PKSignalSemaphore( pState->pSema );

  return result;
}

static int32 RIPCALL ramdev_seek_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destination, int32 flags )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSFILEDESC *pFD;
  int32 result;
  int32 error = DeviceNoError;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
  result = MFSSeek( pFD, destination, flags, &error );
  ramdev_set_lasterror(dev, error);
  PKSignalSemaphore( pState->pSema );
  return result;
}

static int32 RIPCALL ramdev_bytes_file
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSFILEDESC *pFD;
  int32 result;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  PKWaitOnSemaphore( pState->pSema );
  pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
  result = MFSAvail( pFD, bytes, reason );
  PKSignalSemaphore( pState->pSema );

  if ( !result )
    return ramdev_ioerror_false( dev );
  return TRUE;
}

static int32 RIPCALL ramdev_status_file
  ( DEVICELIST *dev, uint8 *filename, STAT *statbuff )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSNODE *pParent;
  uint32 i;
  int32 result;
  MFSNODE *pNode;
  uint8 *pNameBuffer = NULL;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  if (pState->prefix[0])
  {
    pNameBuffer = (uint8*) MemAlloc( strlen_uint32((char*) pState->prefix) + 
                                     strlen_uint32((char*) filename) + 2, TRUE, FALSE ); 
    if (pNameBuffer == NULL)
    {
      ramdev_set_lasterror(dev, DeviceVMError);
      return -1;
    }
    strcpy( (char*)pNameBuffer, (char*)pState->prefix );
    if (pState->prefix[strlen((char*) pState->prefix)-1] != '/')
      strcat( (char*)pNameBuffer, "/" );
    strcat( (char*)pNameBuffer, (char*)filename);
  }
  else
    pNameBuffer = filename ;

  PKWaitOnSemaphore( pState->pSema );
  pNode = MFSFindRelative( pState->pMFSRoot, (char*) pNameBuffer, &pParent, &i );
  PKSignalSemaphore( pState->pSema );

  /* These are not supported by the MFS API. */
  statbuff->referenced = 0;
  statbuff->modified = 0;
  statbuff->created = 0;

  if ( pNode == NULL || pNode->type != MFS_File )
  {
    HqU32x2FromUint32( &(statbuff->bytes), 0 );
    result = -1;
  }
  else
  {
    HqU32x2FromUint32( &(statbuff->bytes), pNode->pFile->cbSize );
    result = 0 ;
  }
  
  if (pState->prefix[0] && pNameBuffer) 
    MemFree(pNameBuffer);
  return result;
}

static void* RIPCALL ramdev_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  RAMDeviceState *pDevState = (RAMDeviceState*) dev->private_data;
  uint8 *pNameBuffer;
  MFSITERSTATE *pState = NULL;
  MFSNODE *pRoot, *pParent;
  uint32 i;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  
  UNUSED_PARAM( uint8 *, pattern ); /* Not needed here, because it's passed into each
                                       ramdev_next_file call. */

  PKWaitOnSemaphore( pDevState->pSema );

  if (pDevState->prefix[0])
    pRoot = MFSFindRelative( pDevState->pMFSRoot, (char*) pDevState->prefix, &pParent, &i );
  else
    pRoot = pDevState->pMFSRoot;

  if (pRoot && pRoot->type == MFS_Directory)
  {
    pNameBuffer = (uint8*) MemAlloc( LONGESTFILENAME + 1, TRUE, FALSE );

    if (pNameBuffer != NULL)
    {
      pState = MFSIterBegin( pRoot, pNameBuffer );

      if (pState == NULL)
      {
        ramdev_set_lasterror(dev, DeviceVMError);
        MemFree( pNameBuffer );
      }
    }
    else
    {
      ramdev_set_lasterror(dev, DeviceVMError);
    }
  }

  PKSignalSemaphore( pDevState->pSema );

  return (void*) pState;
}

static int32 RIPCALL ramdev_next_file
  ( DEVICELIST *dev, void **handle, uint8 *pattern, FILEENTRY *entry )
{
  RAMDeviceState *pDevState = (RAMDeviceState*) dev->private_data;
  MFSITERSTATE *pState = (MFSITERSTATE*) (*handle);
  uint32 fLeadingSlash = FALSE;

  /* If the pattern is supplied with a leading slash, then also include
     one on the filenames, otherwise they won't match. */

  if ( (char) pattern[ 0 ] == '/' )
    fLeadingSlash = TRUE;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

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

static int32 RIPCALL ramdev_end_file_list( DEVICELIST *dev, void *handle )
{
  MFSITERSTATE *pState = (MFSITERSTATE*) handle;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

  MemFree( pState->pPrivate );
  MFSIterEnd( pState );

  return 0;
}

static int32 RIPCALL ramdev_rename_file
  ( DEVICELIST *dev, uint8 *file1, uint8 *file2 )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  int32 result;
  MFSNODE *pRoot, *pParent;
  uint32 i;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  if (pState->prefix[0])
    pRoot = MFSFindRelative( pState->pMFSRoot, (char*) pState->prefix, &pParent, &i );
  else
    pRoot = pState->pMFSRoot;

  if (pRoot )
  {
    int32 error = DeviceNoError;
    PKWaitOnSemaphore( pState->pSema );
    result = MFSRename( pRoot, (char*) file1, (char*) file2, &error );
    ramdev_set_lasterror(dev, error);
    PKSignalSemaphore( pState->pSema );
  }
  else
  {
    ramdev_set_lasterror(dev, DeviceUndefined) ;
    result = -1;
  }
  return result;
}

static int32 RIPCALL ramdev_delete_file( DEVICELIST *dev, uint8 *filename )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  int32 result;
  MFSNODE *pRoot, *pParent;
  uint32 i;

  (void) ramdev_noerror(dev) ; /* clear error flag first */
  if (pState->prefix[0])
    pRoot = MFSFindRelative( pState->pMFSRoot, (char*) pState->prefix, &pParent, &i );
  else
    pRoot = pState->pMFSRoot;

  if (pRoot )
  {
    int32 error = DeviceNoError;
    PKWaitOnSemaphore( pState->pSema );
    result = MFSDelete( pRoot, (char*) filename, &error );
    ramdev_set_lasterror(dev, error);
    PKSignalSemaphore( pState->pSema );
  }
  else
  {
    ramdev_set_lasterror(dev, DeviceUndefined) ;
    result = -1;
  }
  return result;
}

static int32 RIPCALL ramdev_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  int32 length = strlen_int32 ( (char *) szParamPrefix );
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
 
  (void) ramdev_noerror(dev) ; /* clear error flag first */
  if ( param->paramnamelen == length &&
       strncmp((char *)param->paramname,
               (char *)szParamPrefix, CAST_SIGNED_TO_SIZET(length)) == 0)
  {
    /* otherwise change file prefix if it is valid: */
    if ( param->type != ParamString ) {
      ramdev_set_lasterror(dev, DeviceIOError) ;
      return ParamTypeCheck;
    }
    if ( param->strvallen > LONGESTFILENAME ) {
      ramdev_set_lasterror(dev, DeviceIOError) ;
      return ParamRangeCheck;
    }

    if (param->strvallen > 0 )
    {
      memcpy(pState->prefix, param->paramval.strval, CAST_SIGNED_TO_SIZET(param->strvallen));
      pState->prefix[ param->strvallen ] = '\0';
    }
  } 
  else
  {
    ramdev_set_lasterror(dev, DeviceNoError) ;
    return ParamIgnored;
  }

  return ParamAccepted;
}

static int32 RIPCALL ramdev_start_param(DEVICELIST *dev)
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  (void) ramdev_noerror(dev) ; /* clear error flag first */
  pState->iParam = 0;
  return RAMDEV_PARAM_COUNT ;
}

static int32 RIPCALL ramdev_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  (void) ramdev_noerror(dev) ; /* clear error flag first */

  if ( param->paramname == NULL )
  {
    /* The RIP is part way through asking for all parameters. */
    switch ( pState->iParam++ )
    {
      case RAMDEV_PARAM_TYPE:
        param->paramname = szParamType;
        param->paramnamelen = strlen_int32( (char*) szParamType );
        param->type = ParamString;
        param->paramval.strval = szParamValueFileSystem;
        param->strvallen = strlen_int32( (char*) szParamValueFileSystem );
        return ParamAccepted;

      case RAMDEV_PARAM_PREFIX:
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

static int32 RIPCALL ramdev_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  uint32 cbRAM = 0;
  uint32 cbROM = 0;
  uint32 cbTotal = 0;

  /* Ask MFS to sum the memory usage for the root node of the tree. */
  MFSMemUsage( pState->pMFSRoot, &cbROM, &cbRAM );
  cbTotal = cbRAM + cbROM;

  devstat->block_size = 1024;
  devstat->start = NULL;
  HqU32x2FromUint32( &(devstat->size), cbTotal );

  /* Currently no way to determine free memory for RAM device. */
  HqU32x2FromInt32( &(devstat->free), 0 );

  return ramdev_noerror( dev );
}

static int32 RIPCALL ramdev_ioctl
  ( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, int32 opcode, intptr_t arg )
{
  RAMDeviceState *pState = (RAMDeviceState*) dev->private_data;
  MFSFILEDESC *pFD;
  int32 result;

  (void) ramdev_noerror(dev) ; /* clear error flag first */

  if ( opcode == DeviceIOCtl_SetCompressionMode )
  {
    PKWaitOnSemaphore( pState->pSema );
    pFD = DEVICE_FILEDESCRIPTOR_TO_VOIDPTR( descriptor );
    if ( MFSSetCompression( pFD, arg == IOCTL_COMPRESSION_ON ) )
    {
      result = 0;
    }
    else
    {
      ramdev_set_lasterror(dev, DeviceIOError);
      result = -1;
    }

    PKSignalSemaphore( pState->pSema );
  }
  else
  {
    result = 0;
  }

  return result;
}


static int32 RIPCALL ramdev_spare( void )
{
  return 0;
}

void init_C_globals_ramdev(void)
{
#if defined(USE_HYBRID_SW_FOLDER) || defined(USE_ZIP_HYBRID_SW_FOLDER)
  rootDir.nEntries = 0;
  rootDir.entries = NULL;
  rootDir.fDynamicList = FALSE;
#endif

  pMFSRoot = NULL ;
}

