/* Copyright (C) 1999-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:filedev.c(EBDSDK_P.1) $
 * File system device
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of the file system device type.
 *
 * \section filedev_details Implementation of the file system device type.
 *
 * This is a largely complete, though simple, implementation.
 *
 * The most complex part is the implementation of the filenameforall
 * operator through the three relevant functions in the device.
 *
 * This is the device type which is assigned the device type number 0
 * - see ripthread.h and Programmer's Reference manual section 5.6 for
 * a discussion of device type numbers.
 *
 * A system which does not have a disk is still required to support a
 * device type 0. However, the semantics would be different: it would
 * only need to be readable, and supply known named files, such as
 * Sys/ExtraDevices (which is essential) and built-in fonts. It could
 * provide the byte stream for the read calls straight out of memory
 * (ROM perhaps) rather than accessing a disk.
 *
 * The operation of this implementation of the device type is that
 * each device of this type represents a portion of the file system,
 * e.g. on a PC the devices might be used to represent drives (%C%,
 * %D%, ...) and on a Macintosh they represent volumes. There will
 * always be a device %os% created implictly by the core rip, and this
 * will usually be a restricted part of the file system: Harlequin's
 * own products have a directory called "SW" by default which is what
 * %os% refers to.  Such a directory is supplied with the corerip
 * library, including example system files, many of which may be used
 * unchanged.
 *
 * This implementation uses one device parameter, "/Prefix". This
 * identifies where on the filing system the PostScript device is to
 * refer to. This is a string representing a native pathname on the
 * target file system. When PostScript refers to a file called
 * "%p%aa/bb/cc", we map this to a name in the file system. This
 * implementation does this by prefixing the name passed to the open
 * call ("aa/bb/cc") with the prefix for the device (which is assumed
 * to terminate with a '/'). The prefix is stored in the private data
 * field of the devicelist structure for each device.
 *
 * There is nothing special about the '/' in the PostScript name (see
 * PR 5.3), but we map it to the platform's directory separator. This
 * has two consequences: because the semantics of the name "aa/bb/cc"
 * are those of a flat file system, when a request is made to open the
 * file for writing, if we are to use the same name, we must create
 * any directories in the path in order to open the file.  The mapping
 * also means that we ignore the difference between files called
 * "aa/bb/cc/" and "aa/bb/cc". These are both valid PostScript names
 * and should refer to different files, but are mapped to the same
 * file in this implementation.  To be strictly correct this should be
 * handled by some more sophisticated name mapping.
 *
 * Some names will causes failures, e.g. names with > 31 chars on the
 * Mac.  To overcome this some name mapping mechanism would need to be
 * implemented.
 *
 * The prefix is obtained by the setdevparams PostScript operator, so
 * a useful piece of PostScript (to go in Sys/ExtraDevices, for
 * example) would be:
 *
 *     statusdict begin
 *     (%cd%) dup devmount pop
 *     <<
 *       /Password 0
 *       /DeviceType 0
 *       /Prefix (./)
 *       /Enable true
 *     >> setdevparams
 *     end
 *
 * (devmount is not listed in the PostScript Language Reference
 * Manual, but is found in all PostScript rips with file systems).
 *
 * However, there is no PostScript associated with mounting the %os%
 * device: it needs to exist in order for Sys/ExtraDevices to be
 * executed. This implementation assumes its prefix is always "SW/"
 * (i.e.  a directory called SW in the current working
 * directory). More general solutions include obtaining this from an
 * environment variable, configuration file or command line option.
 *
 * Though almost complete more general consideration might be given to the following:
 *
 * \li A complete implementation of the device_status function.
 * \li A comprehensive file name mapping scheme.
 * \li A more general mechanism for specifying and relocating the root
 * %os% device
 * \li A more general mechanism particularly for systems on which
 * multiple copies of the rip may be running at the same time:
 * especially to distribute files so that system files (including
 * fonts) are available as a central resource, but users do not
 * interfere with each other when, for example, writing files to the
 * file system.
 *
 */

#include "std.h"
#include "devutils.h"
#include "hqstr.h"
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

/* ---------------------------------------------------------------------- */
/*
  Miscellaneous items:
*/
#define PREFIX_MAX_LENGTH LONGESTFILENAME     /* the maximum length of the Prefix string */
#define FILEMODE          0666                /* files created with rw_rw_rw permisions */
#define DIRMODE           0777                /* ... and directories with rwxrwxrwx */

#ifndef S_ISDIR
#define S_ISDIR(_stmode_) ((_stmode_) & S_IFDIR)
#endif

#ifndef SEEK_SET
#define SEEK_SET L_SET
#define SEEK_CUR L_INCR
#define SEEK_END L_XTND
#endif


#define PS_DIRECTORY_SEPARATOR '/'
#define PS_FILENAME_ESCAPE     '\\'


typedef struct FilenameForAll
{
  struct FilenameForAll *  parent_list;

  uint8         has_dirs;
  uint8         doing_dirs;
  uint8         nondirbase;
  uint8         w2;
  int32         baselength; /* the length of the current path name */
  int32         rootlength; /* the length of the IRootPart of the current device */
  int32         prefixlength;
  void        * dir;         /* platform specific */
  uint8       * dirname;     /* needed to rewind dir handle */
  uint8         basename[ LONGESTFILENAME ];
} FilenameForAll;

/**
 * @brief  Structure to hold file-specific state.
 */
typedef struct FileState
{
  FileDesc* pDescriptor;
  int32 short_read;
} FileState;

/**
 * @brief  Structure to hold device-specific state.
 */
typedef struct FileDeviceState
{
  uint8 psSWDir[ LONGESTFILENAME ];

  /** @brief Semaphore to protect concurrent calls. */
  void *pSema;
} FileDeviceState;

static const uint8 * fs_prefix_parameter = (uint8 *) "Prefix";
static const uint8 * fs_type_parameter = (uint8 *) "Type";

/*
 * This buffer holds an explicitly-provided path to the SW folder, as stored
 * by KSetSWDir(). If this string is zero-size, then no explicit SW folder
 * path has been specified, and the skin should default to the per-platform
 * search rules.
 */
static uint8 swDirOverride[ LONGESTFILENAME ] = "";

/* ----------------------------------------------------------------------
  All the routines for the device types can be static within the file because
  they are only referenced via the device type structure.
*/
static int32 RIPCALL fs_device_init     ( DEVICELIST *dev );
static int32 RIPCALL fs_ioerror         ( DEVICELIST *dev );
static DEVICE_FILEDESCRIPTOR RIPCALL fs_open_file       ( DEVICELIST *dev,
                                                     uint8 *filename,
                                                     int32 openflags );
static int32 RIPCALL fs_read_file       ( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor,
                                  uint8 *buff,
                                  int32 len );
static int32 RIPCALL fs_write_file      ( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor,
                                  uint8 *buff,
                                  int32 len );
static int32 RIPCALL fs_close_file      ( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL fs_seek_file       ( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor,
                                  Hq32x2 *destination,
                                  int32 flags);
static int32 RIPCALL fs_bytes_file      ( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor,
                                  Hq32x2 * bytes,
                                  int32 reason );
static int32 RIPCALL fs_status_file     ( DEVICELIST *dev,
                                  uint8 *filename,
                                  STAT *statbuff );
static void* RIPCALL fs_start_file_list ( DEVICELIST *dev,
                                  uint8 *pattern);
static int32 RIPCALL fs_next_file       ( DEVICELIST *dev,
                                  void **handle,
                                  uint8 *pattern,
                                  FILEENTRY *entry);
static int32 RIPCALL fs_end_file_list   ( DEVICELIST *dev,
                                  void * handle);
static int32 RIPCALL fs_rename_file     ( DEVICELIST *dev,
                                  uint8 *file1,
                                  uint8 *file2);
static int32 RIPCALL fs_delete_file     ( DEVICELIST *dev,
                                  uint8 *filename );
static int32 RIPCALL fs_set_param       ( DEVICELIST *dev,
                                  DEVICEPARAM *param);
static int32 RIPCALL fs_start_param     ( DEVICELIST *dev );
static int32 RIPCALL fs_get_param       ( DEVICELIST *dev,
                                  DEVICEPARAM *param);
static int32 RIPCALL fs_status_device   ( DEVICELIST *dev,
                                  DEVSTAT *devstat);
static int32 RIPCALL fs_device_dismount ( DEVICELIST * dev );
static int32 RIPCALL fs_ioctl           ( DEVICELIST *dev,
                                  DEVICE_FILEDESCRIPTOR descriptor,
                                  int32 opcode,
                                  intptr_t arg );
static int32 RIPCALL fs_spare           ( void );


static int32 KInitFilenameForAll(FilenameForAll * ls, uint8 * pszDirName, int32 * pfIOError);
static void KAbandonFilenameForAll(FilenameForAll * ls);

static int32 PDFFilespecToPS( PDF_FILESPEC *pdffs );

/* ---------------------------------------------------------------------- */

/** \brief
  The device type structure for the file system devices. This is a
  specific example of the structure defined in PR5.8.

  Points to note:
  (a) of the function pointers included in this structure, only the tickle
      function may be a NULL pointer. The rest must be filled in with
      stub routines which at least set the last error flag appropriately.
      The dismount call does nothing else but indicates success with its
      return code; however the buffer size function indicates failure,
      which means that a fixed size buffer will be allocated see PR5.8.2
      and PR5.8.26.
  (b) the device is "relative" (it can support named files, unlike, for
      example, a serial line). This means that when an attempt is made to
      open a named file in PostScript unqualified by a device, devices of
      this type will each receive open requests until one does not report
      an "undefined" error. See PR5.8.2
  (c) the device is "writable": this means that devices of this type will
      be considered when trying to open a file for writing from PostScript.
*/

DEVICETYPE Fs_Device_Type = {
#ifdef USE_RAM_SW_FOLDER
  DISK_DEVICE_TYPE,       /* the device type number */
#else
#ifdef USE_HYBRID_SW_FOLDER
  DISK_DEVICE_TYPE,       /* the device type number */
#else
#ifdef USE_ZIP_HYBRID_SW_FOLDER
  DISK_DEVICE_TYPE,       /* the device type number */
#else
  OS_DEVICE_TYPE,         /* the device type number */
#endif
#endif
#endif
  DEVICERELATIVE | DEVICEWRITABLE,    /* device characteristics flags */
  CAST_SIZET_TO_INT32(sizeof (FileDeviceState)), /* the size of the private data */
  0,                      /* tickle function control: n/a */
  NULL,                   /* procedure to service the device */
  skindevices_last_error, /* return last error for this device */
  fs_device_init,         /* call to initialise device */
  fs_open_file,           /* call to open file on device */
  fs_read_file,           /* call to read data from file on device */
  fs_write_file,          /* call to write data to file on device */
  fs_close_file,          /* call to close file on device */
  fs_close_file,          /* call to abort: same as close */
  fs_seek_file,           /* call to seek file on device */
  fs_bytes_file,          /* call to get bytes avail on an open file */
  fs_status_file,         /* call to check status of file */
  fs_start_file_list,     /* call to start listing files */
  fs_next_file,           /* call to get next file in list */
  fs_end_file_list,       /* call to end listing */
  fs_rename_file,         /* call to rename file on the device */
  fs_delete_file,         /* call to remove file from device */
  fs_set_param,           /* call to set device parameter */
  fs_start_param,         /* call to start getting device parameters */
  fs_get_param,           /* call to get the next device parameter */
  fs_status_device,       /* call to get the status of the device */
  fs_device_dismount,     /* call to dismount the device (dummy) */
  fs_ioerror,             /* call to determine buffer size (dummy) */
  fs_ioctl,               /* ioctl slot, to hint about caching, etc. */
  fs_spare                /* spare slot */
};

/* ---------------------------------------------------------------------- */
/** \brief
   Allocate a FileState object. Should be released with releaseFileState().
*/
static FileState* createFileState ()
{
  FileState* pState = MemAlloc (sizeof (FileState), TRUE, FALSE);

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
static void releaseFileState (FileState* pState)
{
  if (pState)
    MemFree (pState);
}

/** \brief
   Initialise a FileDeviceState object. pDeviceState should point to preallocated
   memory.
*/
static int32 initialiseDeviceState (DEVICELIST *dev)
{
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  if (pDeviceState)
  {
    void* pSema;

    skindevices_set_last_error(DeviceNoError);

    pSema = PKCreateSemaphore (1);
    if (! pSema)
    {
      skindevices_set_last_error(DeviceIOError);
      return FALSE;
    }

    pDeviceState->psSWDir[0] = '\0';
    skindevices_set_last_error(DeviceNoError);
    pDeviceState->pSema = pSema;

    return TRUE;
  }

  return FALSE;
}

/** \brief
   Uninitialise a FileDeviceState object.
*/
static void uninitialiseDeviceState (FileDeviceState* pDeviceState)
{
  if (pDeviceState)
  {
    PKDestroySemaphore (pDeviceState->pSema);
  }
}

/* ---------------------------------------------------------------------- */
/** \brief
   File system device initialisation. See PR5.8.7. This is called for each
   device (note: not device type) when its type number is assigned by
   a call to setdevparams.
*/

static int32 RIPCALL fs_device_init( DEVICELIST *dev )
{
#ifndef USE_RAM_SW_FOLDER
  uint8  platformSWDir[ LONGESTFILENAME ] = { 0 } ;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
#endif

  if (! initialiseDeviceState (dev))
    return -1;

#ifndef USE_RAM_SW_FOLDER

  /* Get path to active SW folder */
  if (! KGetSWDir( platformSWDir ))
  {
    uninitialiseDeviceState (pDeviceState);
    return -1;
  }
  PlatformFilenameToPS( pDeviceState->psSWDir, platformSWDir );
#endif

  return 0; /* success */
}

/* ---------------------------------------------------------------------- */
/** \brief
   The following routines are simple stub routines for cases where the only
   functionality required is to set the appropriate error condition.
   Take care which is used in which circumstances.
*/

static int32 RIPCALL fs_ioerror ( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error( DeviceIOError );
  return -1;
}

static int32 RIPCALL fs_spare ( void )
{
  return 0;
}


/* ---------------------------------------------------------------------- */
/** \brief
   The file_open call for the file system device type. See PR 5.8.8.
*/

static DEVICE_FILEDESCRIPTOR RIPCALL fs_open_file( DEVICELIST *dev,
                                   uint8 *filename, int32 openflags )
{
  int32 pkError;
  DEVICE_FILEDESCRIPTOR result;
  uint8 name[ LONGESTFILENAME ];
  FileState* pFileState;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;

  /* Create file-specific state */
  pFileState = createFileState ();
  if (! pFileState)
  {
    skindevices_set_last_error( DeviceVMError );
    return -1;
  }

  /* Create the complete filename by concatenating the /Prefix for the
   * current device and the provided filename.
   */
  name[0] = 0;
  PSPrefixAndFilenameToPlatform(name, pDeviceState->psSWDir, filename );

  pFileState->pDescriptor = PKOpenFile(name, openflags, &pkError);
  if (! pFileState->pDescriptor)
  {
    skindevices_set_last_error( KMapPlatformError(pkError) );
    releaseFileState (pFileState);
    return -1;
  }

  result = VOIDPTR_TO_DEVICE_FILEDESCRIPTOR(pFileState) ;

  skindevices_set_last_error( DeviceNoError );
  return result;
}

/* ----------------------------------------------------------------------*/
/** \brief   The read_file routine for the file system device type. See PR 5.8.9.

   Read from file described by 'descriptor' which is on the device 'dev'
   into the buffer 'buff'.

   For abnormally large buffers (only when the device requests such sizes
   by the appropriate device type call), reading from the file system
   in chunks might be required, with SwOften calls between each.
*/

static int32 RIPCALL fs_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                   uint8 *buff, int32 len )
{
  int32 result;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  FileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = (FileState*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR (descriptor);
  if (! pFileState)
  {
    skindevices_set_last_error( DeviceInvalidAccess );
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
      skindevices_set_last_error( KMapPlatformError(pkError) );
    else
      skindevices_set_last_error( DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}


/* ---------------------------------------------------------------------- */
/** \brief   The write_file routine for the file system device type. See PR 5.8.10.

   Write to the file described by 'descriptor' which is on the device 'dev'
   from the buffer 'buff'.

   The comment for reading regarding SwOften also applies here.
*/

static int32 RIPCALL fs_write_file(DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                   uint8 *buff, int32 len )
{
  int32 result;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  FileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = (FileState*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR (descriptor);
  if (! pFileState)
  {
    skindevices_set_last_error( DeviceInvalidAccess );
    result = -1;
  }
  else
  {
    int32 pkError;

    result = PKWriteFile(pFileState->pDescriptor, buff, len, &pkError);
    if (result < 0)
      skindevices_set_last_error( KMapPlatformError(pkError) );
    else
      skindevices_set_last_error( DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}


/* ---------------------------------------------------------------------- */
/** \brief   The close_file routine for the file system device type. See PR 5.8.11
   and PR 5.8.12.

   The abort call also invokes this function, because an abnormal close
   is the same as a normal close for the file system device type.
*/

static int32 RIPCALL fs_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  int32 result;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  FileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = (FileState*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR (descriptor);
  if (! pFileState)
  {
    skindevices_set_last_error( DeviceInvalidAccess );
    result = -1;
  }
  else
  {
    int32 pkError;

    result = PKCloseFile(pFileState->pDescriptor, &pkError);
    if (result < 0)
    {
      skindevices_set_last_error( KMapPlatformError(pkError) );
    }
    else
    {
      skindevices_set_last_error( DeviceNoError );
      releaseFileState( pFileState );
    }
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}

/* ---------------------------------------------------------------------- */
/** \brief   The seek_file routine for the file system device type. See PR 5.8.13.
*/

static int32 RIPCALL fs_seek_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 * destination, int32 flags )
{
  int32 result = FALSE;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  FileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = (FileState*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR ( descriptor);
  if (! pFileState)
  {
    skindevices_set_last_error( DeviceInvalidAccess );
  }
  else
  {
    int32 pkError;

    result = PKSeekFile(pFileState->pDescriptor, destination, flags, &pkError);
    if (! result)
      skindevices_set_last_error( KMapPlatformError(pkError) );
    else
      skindevices_set_last_error( DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}


/* ---------------------------------------------------------------------- */
/** \brief   The bytes_file routine for the file system device type. See 5.8.14.

   reason SW_BYTES_AVAIL_REL - immediately available after current pos
   reason SW_BYTES_TOTAL_ABS - total extent of file in bytes
*/

static int32 RIPCALL fs_bytes_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * bytes, int32 reason )
{
  int32 result = FALSE;
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  FileState* pFileState;

  PKWaitOnSemaphore (pDeviceState->pSema);

  pFileState = (FileState*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR (descriptor);
  if (! pFileState)
  {
    skindevices_set_last_error( DeviceInvalidAccess );
  }
  else
  {
    int32 pkError;

    result = PKBytesFile(pFileState->pDescriptor, bytes, reason, &pkError);
    if ( !result )
      skindevices_set_last_error( KMapPlatformError(pkError) );
    else
      skindevices_set_last_error( DeviceNoError );
  }

  PKSignalSemaphore (pDeviceState->pSema);

  return result;
}


/* ---------------------------------------------------------------------- */
/** \brief   The status_file routine for the file system device type. See 5.8.15
*/

static int32 RIPCALL fs_status_file( DEVICELIST *dev, uint8 *filename,
                                     STAT *statbuff )
{
  int32 pkError;
  int32 result;
  uint8 fname[ LONGESTFILENAME ];
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;

  fname[0] = 0;
  PSPrefixAndFilenameToPlatform(fname, pDeviceState->psSWDir, filename);

  result = PKStatusFile(fname, statbuff, &pkError);
  if (result < 0)
    skindevices_set_last_error( KMapPlatformError(pkError) );
  else
    skindevices_set_last_error( DeviceNoError );

  return result;
}

/* ---------------------------------------------------------------------- */

/*  filenameforall: The PostScript operator is implemented with the three
   functions start_file_list, next_file and end_file_list, for which see
   PR 5.8.16 - 5.8.18.

   We maintain the state required between these calls in an instance of
   the FilenameForAll structure .

   A pattern of the form:
     abcd/ef*hi
   sets up the FilenameForAll structure with the following entries:
     basename =   SW/abcd/ef     (or whatever the prefix of the device is)
     rootlength = 3              (length of "SW/")
     baselength = 5              (length of "abcd/")
     prefixlength = 2            (length of "ef" )

   has_dirs is a flag to indicate if any directory entries where found in the
   current directories. When all the entries in the current dir has been
   enumerated, the directory is rewound, the flag doing_dirs set to TRUE, and
   sub-directories are examined in turn.
*/



/**
 * \brief * fs_start_file_list
 */
static void* RIPCALL fs_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  int32                 l, c;
  int32                 rootlen;
  int32                 baselength;
  int32                 fIOError;
  int32                 firstchar;
  uint8               * basename;
  uint8               * rootbase;
  uint8               * rootend;
  uint8                 nwpt[ LONGESTFILENAME ];
  FilenameForAll      * ls;

  /* allocate a new FilenameForAll structure */
  if ( ( ls = (FilenameForAll *)MemAlloc(sizeof(FilenameForAll), TRUE, FALSE)) == NULL)
  {
    skindevices_set_last_error( DeviceVMError );
    return NULL;
  }
  ls->dir = NULL;

  rootbase = theIRootPart( dev );
  rootend  = PSFilenameSkipToLeafname( rootbase );
  if ( rootbase == rootend )
    rootend = rootbase + strlen((char *)rootbase );

  ls->parent_list = NULL;
  ls->has_dirs = FALSE;
  ls->doing_dirs = FALSE;
  ls->nondirbase = (uint8)( rootend[ 0 ] != '\0' );
  if ( ls->nondirbase )
  {
    strcpy((char *)nwpt, (char *)rootend);
    strcat((char *)nwpt, (char *)pattern);
    pattern = nwpt;
  }
  basename = ls->basename;

  /* find longest unwild directory on front of file pattern */
  baselength = 0;
  for ( l = 0; (c = pattern[ l ]) != 0; ++l )
  {
    if (( c == '*' ) || ( c == '?' ))
      break;
    if ( c == PS_DIRECTORY_SEPARATOR )
      baselength = l + 1;
    else if ( c == PS_FILENAME_ESCAPE )
    {
      ++l;
      if ( pattern[ l ] == '\0' )
        break;
    }
  }
  ls->baselength = baselength;
  ls->prefixlength = l - baselength;

  rootlen = (int32)( rootend - rootbase );
  ls->rootlength = rootlen;
  if (( rootlen + l + 1 ) > LONGESTFILENAME )
  {
    MemFree(ls);
    skindevices_set_last_error( DeviceLimitCheck );
    return NULL;
  }

  memcpy(basename, rootbase, CAST_SIGNED_TO_SIZET(rootlen));
  memcpy(basename + rootlen, pattern, l);

  firstchar = basename[rootlen + baselength ];
  basename[rootlen + baselength] = 0;

  nwpt[0] = 0;
  PSPrefixAndFilenameToPlatform(nwpt, ls->basename, (uint8 *)"/");

  basename[rootlen + baselength] = (uint8)firstchar;

  if (! KInitFilenameForAll(ls, nwpt, &fIOError))
  {
    if (fIOError)
      skindevices_set_last_error( DeviceIOError );
    else
      skindevices_set_last_error( DeviceNoError );

    MemFree(ls);
    return NULL;
  }

  skindevices_set_last_error( DeviceNoError );
  return ( void * )ls;
}

/**
 * \brief fs_next_file
 */
static int32 RIPCALL fs_next_file( DEVICELIST *dev, void **handle,
                                   uint8 *pattern, FILEENTRY *entry )
{
  int32                 l;
  int32                 wasfolder;
  int32                 fname_length;
  uint8               * basename;
  uint8               * rootbase;
  uint8               * rootend = 0;
  FilenameForAll *      ls;
  FilenameForAll *      next_ls;
  FilenameForAll *      tls;
  uint8                 nextfilename[ LONGESTFILENAME ];
  uint8                 nwpt[ LONGESTFILENAME ];

  ls = ( FilenameForAll * )( * handle );

  if ( ls->nondirbase )
  {
    rootbase = theIRootPart( dev );
    rootend  = PSFilenameSkipToLeafname( rootbase );
    if ( rootbase == rootend )
      rootend = rootbase + strlen((char *)rootbase );
    strcpy((char *)nwpt, (char *)rootend);
    strcat((char *)nwpt, (char *)pattern);
    pattern = nwpt;
  }

  for (;;)
  {
    int32       pkError;

    if ( SwOften() < 0 )
    {
      skindevices_set_last_error( DeviceInterrupted );
      return FileNameError;
    }


    if (! PKDirNext( ls->dir, nextfilename, &wasfolder, &pkError))
    {
      if ( pkError != PKErrorNone )
      {
        skindevices_set_last_error( DeviceIOError );
        return FileNameError;
      }

      /* else assume we have come to the end of the current directory. */
      if ( ls->doing_dirs )
      {
        /* we have enumerated all the directories in this directory */
        if ( ls->parent_list )
        {
          tls = ls;
          ls = ls->parent_list;
          *handle = (void *) ls;
          KAbandonFilenameForAll( tls );
          continue;
        }
        else
        {
          /* we have finished the top level directory */
          skindevices_set_last_error( DeviceNoError );
          return FileNameNoMatch;
        }
      }
      else
      {
        /* If any dirs exist in this dir, we start from the top and list
         * each in turn.
         */
        if ( ls->has_dirs )
        {
          /* rewind to top of directory, and start again */
          if (! PKDirClose(ls->dir, &pkError))
          {
            skindevices_set_last_error( DeviceIOError );
            return FileNameError;
          }
          ls->dir = NULL;
          if ((ls->dir = PKDirOpen(ls->dirname, &pkError)) == NULL)
          {
            skindevices_set_last_error( DeviceIOError );
            return FileNameError;
          }
          ls->doing_dirs = TRUE;
          continue;
        }
        else
        {
          /* No sub directories in directory. Return back up to the
           * parent directory.
           */
          if ( ls->parent_list )
          {
            tls = ls;
            ls = ls->parent_list;
            *handle = (void *) ls;
            KAbandonFilenameForAll( tls );
          }
          else
          {
            /* there really is no more to do now */
            skindevices_set_last_error( DeviceNoError );
            return FileNameNoMatch;
          }
        }
      }
    }
    else
    {
      /* if we have a prefix part, check that they match */
      uint8      * ptb = nextfilename;
      uint8        psName[ LONGESTFILENAME ];

      PlatformPathElementToPS(psName, &ptb);
      fname_length = strlen_int32((char *)psName );

      if ( ls->prefixlength )
      {
        if ( fname_length < ls->prefixlength )
          continue;
        if ( strncmp((char *)psName,
                     (char*)(ls->basename + ls->rootlength + ls->baselength),
                     ls->prefixlength ) != 0)
          continue;
      }

      l = ls->rootlength + ls->baselength;
      memcpy(ls->basename + l, psName, CAST_SIGNED_TO_SIZET(fname_length) );
      ls->basename[ l + fname_length ] = '\0';

      if ( ls->doing_dirs )
      {
        /* are doing sub-directories now, so create new listing if entry
         * is directory
         */
        if ( wasfolder )
        {
          uint8  dirName[ LONGESTFILENAME ];;

          /* allocate a new FilenameForAll structure */
          if ( ( next_ls = (FilenameForAll *)MemAlloc(sizeof(FilenameForAll), TRUE, FALSE)) == NULL)
          {
            skindevices_set_last_error( DeviceVMError );
            return FileNameRangeCheck;
          }

          /* open this directory */
          PSPrefixAndFilenameToPlatform( dirName, ls->basename, (uint8 *)"/" );

          if ( ! KInitFilenameForAll( next_ls, dirName, NULL))
          {
            KAbandonFilenameForAll(next_ls);
            skindevices_set_last_error( DeviceIOError );
            return FileNameIOError;
          }

          next_ls->parent_list = ls;
          next_ls->has_dirs = FALSE;
          next_ls->doing_dirs = FALSE;
          basename = next_ls->basename;

          strncpy((char *)basename, (char *)ls->basename, LONGESTFILENAME );

          next_ls->baselength = ls->baselength + fname_length;
          next_ls->prefixlength = 0;
          next_ls->nondirbase = ls->nondirbase;
          next_ls->rootlength = ls->rootlength;
          basename[ next_ls->rootlength + next_ls->baselength ] = PS_DIRECTORY_SEPARATOR;
          next_ls->baselength++;
          ls = next_ls;
          *handle = ( void * )ls;
        }
      }
      else
      {                    /* we are still enumerating files */
        if ( wasfolder )
        {
          ls->has_dirs = TRUE;
        }
        else
        {
          /* we have a file entry in current dir, test if this file name
           * matches the pattern
           */
          if ( SwPatternMatch( pattern, ls->basename + ls->rootlength ))
          {
            /* at last, we can return a file name */
            theIDFileNameLen( entry ) = fname_length + ls->baselength
              - ( ls->nondirbase ? strlen_int32((char *)rootend ) : 0 );
            theIDFileName( entry ) = ls->basename + ls->rootlength
              + ( ls->nondirbase ? strlen((char *)rootend ) : 0 );

            theIDFileName( entry )[ theIDFileNameLen( entry ) ] = '\0';
            skindevices_set_last_error( DeviceNoError );
            return FileNameMatch;
          }
        }
      }
    }
  }
}

/**
 * \brief fs_end_file_list
 */
static int32 RIPCALL fs_end_file_list( DEVICELIST *dev, void * handle )
{
  FilenameForAll *      ls;
  FilenameForAll *      pls;

  UNUSED_PARAM(DEVICELIST *, dev);

  skindevices_set_last_error( DeviceNoError );

  ls = ( FilenameForAll * )handle;
  while ( ls ) {
    pls = ls->parent_list;

    KAbandonFilenameForAll( ls );
    ls = pls;
  }

  return 0;
}


/* ---------------------------------------------------------------------- */
/** \brief   The rename_file routine for the file system device type. See PR 5.8.19

*/

static int32 RIPCALL fs_rename_file( DEVICELIST *dev,
                                     uint8 *file1, uint8 *file2 )
{
  /* Note: We use rename() on Mac too for now because using File Manager
   * functions is 'too complicated' for what is intended to be an
   * example of how to use the RIP.
   */

  int32 code;
  uint8 buff1[ LONGESTFILENAME ];
  uint8 buff2[ LONGESTFILENAME ];
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  int32 nError = DeviceNoError;

  buff1[0] = buff2[0] = 0;
  PSPrefixAndFilenameToPlatform(buff1, pDeviceState->psSWDir, file1);
  PSPrefixAndFilenameToPlatform(buff2, pDeviceState->psSWDir, file2);

  code = rename((char *)buff1, (char *)buff2 );
  if (code < 0)
  {
    switch (errno)
    {
      case EACCES:
      case EEXIST:
      case EROFS:
        nError = DeviceInvalidAccess; break;
      case ENOENT:
      case ENOTDIR:
#ifndef MACINTOSH
      case ENAMETOOLONG:
#endif
        nError = DeviceUndefined; break;
      case ENFILE:
#ifdef UNIX
      case ENOSR:
#endif
        nError = DeviceLimitCheck; break;
      default:
        nError = DeviceIOError; break;
    }
  }

  skindevices_set_last_error( DeviceNoError );
  return code;
}


/* ---------------------------------------------------------------------- */
/** \brief   The delete_file routine for the file system device type.
*/

static int32 RIPCALL fs_delete_file( DEVICELIST *dev, uint8 *filename )
{
  uint8 fname[ LONGESTFILENAME ];
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
  int32 pkError;

  fname[0] = 0;
  PSPrefixAndFilenameToPlatform(fname, pDeviceState->psSWDir, filename);

  if (PKDeleteFile(fname, &pkError ) != 0)
  {
    skindevices_set_last_error( KMapPlatformError( pkError ));
    return -1;
  }

  skindevices_set_last_error( DeviceNoError );
  return 0;
}

/* ---------------------------------------------------------------------- */

/** \brief   The set_param routine for the file system device type. See PR 5.8.21.

   The only parameter that this implementation recognises is /Prefix, which
   must be a string, and it is used to define the prefix which is tacked
   on to the beginning of any filename requested by Harlequin RIP.

   For a more complex example of device parameter handling see the example
   pagebuffer device implementation.
*/

static int32 RIPCALL fs_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  int32 length;

  length = strlen_int32 ( (char *)fs_prefix_parameter );

  if ( param->paramnamelen == length &&
       strncmp((char *)param->paramname,
               (char *)fs_prefix_parameter, length) == 0)
  {
    FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;

    if ( dev == SwFindDevice((uint8 *)"os") ) {
      /* refuse to change the os prefix: this is a restriction you may
         choose not to implement: you could have a central bootstrap
         root device containing files like Sys/ExtraDevices which are
         essential to get started, and then transfer it elsewhere once
         that has run. Considerations here are whether the machine is
         likely to have more than one invocation of the rip running at
         once. */
      skindevices_set_last_error( DeviceIOError );
      return ParamConfigError;
    }

    /* otherwise change file prefix if it is valid: */
    if ( param->type != ParamString ) {
      skindevices_set_last_error( DeviceIOError );
      return ParamTypeCheck;
    }
    if ( param->strvallen > PREFIX_MAX_LENGTH ) {
      skindevices_set_last_error( DeviceIOError );
      return ParamRangeCheck;
    }

    if (param->strvallen > 0 && *param->paramval.strval == '%')
    {
      /* complete path is given */
      length = param->strvallen;
      memcpy(pDeviceState->psSWDir, param->paramval.strval, CAST_SIGNED_TO_SIZET(length));
    }
    else {
#ifndef USE_RAM_SW_FOLDER
      /* make the prefix relative to the os's prefix rather
         than the current directory */
      int32 l = strlen_int32( (char *)dev->private_data );
      memcpy(pDeviceState->psSWDir + l, param->paramval.strval, CAST_SIGNED_TO_SIZET(param->strvallen));
      length = l + param->strvallen;
#else
      /* if a disk device while SW (aka os device) in memory, must have root
       * in the prefix
       */
      skindevices_set_last_error( DeviceIOError );
      return ParamRangeCheck;
#endif
    }
    /* terminate the string */
    if ( length >= LONGESTFILENAME )
      length = LONGESTFILENAME - 1;
    pDeviceState->psSWDir[ length ] = '\0';
  } else {
    skindevices_set_last_error( DeviceNoError );
    return ParamIgnored;
  }

  skindevices_set_last_error( DeviceNoError );
  return ParamAccepted;
}

/* ---------------------------------------------------------------------- */

/** \brief   The start_param routine for the file system device type. See 5.8.22.

   The routine is called as part of the currentdevparams operator; the
   individual parameters are returned one at a time by subsequent calls to
   the get_param function. fs_param_count is used to maintain
   the state between calls to get_param.

   Also return the number of parameters recognized by this implementation
   of the file system device type.
*/

static int32 fs_param_count = 0;

static int32 RIPCALL fs_start_param( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error( DeviceNoError );
  fs_param_count = 0;
  return 2; /* number of device specific parameters: only two */
}

/* ---------------------------------------------------------------------- */

/** \brief   The get_param routine for the file system device type. See PR 5.8.23

   This routine serves two purposes: when the parameter name is NULL, it is
   to return the next device parameter - fs_param_count keeps
   track of how far through the list we are, though in this case there
   is only one - otherwise it should return the one called for by the name.

   There is a more complex example in the example implementation of the
   pagebuffer device type.
*/

static int32 RIPCALL fs_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;

  skindevices_set_last_error( DeviceNoError );

  if ( param->paramname == NULL ) {
    switch ( fs_param_count++ ) {
    case 0 :
      param->paramname = (uint8*) fs_prefix_parameter;
      param->paramnamelen = strlen_int32 ( (char *) fs_prefix_parameter );
      param->type = ParamString;
      param->paramval.strval = pDeviceState->psSWDir;
      param->strvallen = strlen_int32( (char *)pDeviceState->psSWDir );
      return ParamAccepted;
    case 1 :
      param->paramname = (uint8*) fs_type_parameter;
      param->paramnamelen = strlen_int32 ( (char *) fs_type_parameter );
      param->type = ParamString;
      param->paramval.strval = ( uint8* ) "FileSystem" ;
      param->strvallen = sizeof("FileSystem") - 1 ;
      return ParamAccepted;
    default:
      return ParamIgnored;
    }
  } else if ( strncmp ((char *)param->paramname, (char *)fs_prefix_parameter,
                       (size_t)param->paramnamelen ) == 0 )
  {
    param->type = ParamString;
    param->paramval.strval = pDeviceState->psSWDir;
    param->strvallen = strlen_int32 ( (char *)pDeviceState->psSWDir );
    return ParamAccepted;
  } else if ( strncmp ((char *)param->paramname, (char *)fs_type_parameter,
                       (size_t)param->paramnamelen ) == 0 )
  {
    param->type = ParamString;
    param->paramval.strval = ( uint8* ) "FileSystem" ;
    param->strvallen = sizeof("FileSystem") - 1 ;
    return ParamAccepted;
  } else {
    return ParamIgnored;
  }
}

/* ---------------------------------------------------------------------- */

/** \brief   The status_device routine for the file system device type.

   This is in contrast to status_file: it returns information about a device
   of this device type, rather than a file open on the device. The
   comprehensive implementation of this routine is likely to be complex. The
   example here simply gives no useful information.
*/

static int32 RIPCALL fs_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  skindevices_set_last_error( DeviceNoError );

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

/* ---------------------------------------------------------------------- */

/** \brief   The status_dismount routine for the file system device type.
*/

static int32 RIPCALL fs_device_dismount( DEVICELIST *dev )
{
  /* Uninitialise the state.  The memory itself is released by the RIP. */
  uninitialiseDeviceState ((FileDeviceState*) dev->private_data);
  return 0;
}

/* ---------------------------------------------------------------------- */

/** \brief   The ioctl routine for the file system device type.

   At the time of this writing, the only ioctl opcode is ..._ShortRead,
   which is used to indicate that, although Harlequin RIP will ask for an
   entire buffer's data in the read_file call, only a certain number of
   bytes will be used: read-ahead won't help.  Other opcodes may be added
   in the future.
*/

static int32 RIPCALL fs_ioctl( DEVICELIST *dev,
                               DEVICE_FILEDESCRIPTOR descriptor, int32 opcode, intptr_t arg )
{
  int32 result = 0;
  int32 nError = DeviceNoError;

  if (opcode == DeviceIOCtl_ShortRead)
  {
    FileDeviceState* pDeviceState = (FileDeviceState*) dev->private_data;
    FileState* pFileState;

    PKWaitOnSemaphore (pDeviceState->pSema);

    pFileState = (FileState*) DEVICE_FILEDESCRIPTOR_TO_VOIDPTR (descriptor);
    if (! pFileState)
    {
      nError = DeviceInvalidAccess;
      result = -1;
    }
    else
    {
      pFileState->short_read = CAST_INTPTRT_TO_INT32(arg);
      result = 0;
    }

    PKSignalSemaphore (pDeviceState->pSema);
  }
  else if (opcode == DeviceIOCtl_PDFFilenameToPS)
  {
    /* DeviceIOCtl_PDFFilenameToPS converts a PDF file spec to a PS
     * file name.
     */
    int32 call_result = PDFFilespecToPS((PDF_FILESPEC *) arg);
    if ( call_result !=  DeviceNoError ) {
      nError = call_result ;
      result = -1;
    }
  }

  skindevices_set_last_error( nError );
  return result;
}


/* ----------------------- Helper functions ------------------------------ */

/* Turn a PDF filespec filename into a full PS filename
   Will just make an attempt to construct a valid PS filename that
   will be accepted by the rip and useful to FindExternalFile
*/
static int32 PDFFilespecToPS( PDF_FILESPEC *pdffs )
{

  int32 result = DeviceNoError;
  char pdfname[ LONGESTFILENAME ] ;
  int32 psfname_len = 0 ;
  char * tmp = NULL ;

  if (pdffs->filename.len <= 0)
    return DeviceLimitCheck;

  /* Take a copy of the PDF name */
  strncpy( pdfname, (char *) pdffs->filename.clist, pdffs->filename.len ) ;
  pdfname[ pdffs->filename.len ] = '\0' ;

  tmp = strchr( pdfname, '\\' ) ;
  if ( tmp != NULL )
  {
    /*
     * There are backslashes in the path. We take this to be PC generated.
     * Turn the backslashes into forward slashes.
     */

    while ( tmp != NULL ) {
      tmp[0] = '/' ;
      tmp = strchr( tmp, '\\' ) ;
    }
  }
  else
  {
    tmp = strchr( pdfname, ':' ) ;
    if ( tmp != NULL )
    {
      /*
       * No backslashes, but there are colons. We take this to be Mac generated.
       * Replace colons with forward slashes.
       */
      while ( tmp != NULL ) {
        tmp[0] = '/' ;
        tmp = strchr( tmp, ':' ) ;
      }
    }
  }


  strcpy ( ( char * ) pdffs->buffer.clist, "%" ) ;
  psfname_len ++ ;

  strcat ( ( char * ) pdffs->buffer.clist, ( char * ) pdffs->current_device ) ;
  psfname_len += strlen_int32( ( char * ) pdffs->current_device ) ;

  strcat ( ( char * ) pdffs->buffer.clist, "%/" ) ;
  psfname_len += 2 ;

  /* pdffs->buffer.clist will hold the current_device length, but check if there is
   * enough space for the ps filename.
   */
  psfname_len += strlen_int32( pdfname ) ;
  if ( psfname_len > pdffs->buffer.len )
    return DeviceLimitCheck ;

  /* In the pdfname string, we replace individual chars but the overall length
   * of the string remains the same
   */
  strcat ( ( char * ) pdffs->buffer.clist, pdfname ) ;

  pdffs->buffer.len = ( int32 ) psfname_len ;

  return result ;
}

/**
 * \brief KInitFilenameForAll
 */
static int32 KInitFilenameForAll(FilenameForAll * ls, uint8 * pszDirName, int32 * pfIOError)
{
  int32 pkError;

  /* take a copy of the dir name */
  ls->dirname = (uint8 *)MemAlloc(strlen_uint32((char *)pszDirName) + 1, FALSE, FALSE);
  if (ls->dirname == NULL)
    return FALSE;
  strcpy((char *)ls->dirname, (char *)pszDirName);

  /* and get the platform-specific code to open the directory */
  ls->dir = PKDirOpen(pszDirName, &pkError);
  if (ls->dir == NULL)
  {
    if (pfIOError != NULL)
      *pfIOError = pkError != PKErrorNonExistent;
    MemFree(ls->dirname);
    return FALSE;
  }
  if (pfIOError != NULL)
    *pfIOError = FALSE;
  return TRUE;
}


/**
 * \brief KAbandonFilenameForAll
 */
static void KAbandonFilenameForAll(FilenameForAll * ls)
{
  int32 pkError;

  if (ls->dir != NULL)
  {
    PKDirClose(ls->dir, &pkError);
  }
  if (ls->dirname != NULL)
  {
    MemFree(ls->dirname);
  }
  MemFree(ls);
}

/**
 * @brief Record an explicit path to the SW folder, overriding any default search
 * rules.
 *
 * @param pSWDir Pointer to a null-terminated path, which may not exceed
 * LONGESTFILENAME in length (inclusive of the terminator), and which must end
 * with a directory separator.
 *
 * @return TRUE on success; FALSE otherwise.
 *
 * @note This function is not thread-safe, and so should be called prior to the
 * creation of any file device instances.
 */
int32 KSetSWDir( uint8 *pSWDir )
{
  int32 pathLen = strlen_int32( (char*) pSWDir );

  if ( pathLen < 1 || pathLen >= LONGESTFILENAME )
  {
    return FALSE;
  }
  else if ( pSWDir[ pathLen - 1 ] != DIRECTORY_SEPARATOR )
  {
    return FALSE;
  }
  else
  {
    strcpy( (char*) swDirOverride, (char*) pSWDir );
    return TRUE;
  }
}

/**
 * @brief Get an explicit path to the SW folder, taking into account any path
 * set using KSetSWDir().
 *
 * @param pSWDir Pointer to memory at least LONGESTFILENAME bytes in length,
 * which on output contains the SW folder path (terminated with a directory
 * separator).
 *
 * @return TRUE on success; FALSE otherwise.
 */
int32 KGetSWDir ( uint8* pSWDir )
{
  if (swDirOverride[0])
  {
    strcpy ((char*) pSWDir, (char*) swDirOverride);
    return TRUE;
  }

  return PKSWDir (pSWDir);
}

void init_C_globals_filedev(void)
{
  swDirOverride[0] = '\0' ;
}

/* end of filedev.c */
