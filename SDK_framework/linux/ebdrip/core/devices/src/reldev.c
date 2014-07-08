/** \file
 * \ingroup filedevs
 *
 * $HopeName: COREdevices!src:reldev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The Relative Device type (17), implementation of which is internal to the rip
 */


#include "core.h"
#include "swdevice.h"
#include "swcopyf.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "devices.h"
#include "devs.h"
#include "absdev.h"
#include "reldev.h"

static int32 RIPCALL rel_init_device( DEVICELIST *dev );
static DEVICE_FILEDESCRIPTOR RIPCALL rel_open_file(DEVICELIST *dev,
                                                   uint8 *filename,
                                                   int32 openflags);
static int32 RIPCALL rel_read_file( DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8 *buff,
                                    int32 len );
static int32 RIPCALL rel_write_file( DEVICELIST *dev,
                                     DEVICE_FILEDESCRIPTOR descriptor,
                                     uint8 *buff,
                                     int32 len );
static int32 RIPCALL rel_close_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL rel_abort_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL rel_seek_file(DEVICELIST *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 * destn,
                                   int32 flags );
static int32 RIPCALL rel_bytes_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * bytes,
                                    int32 reason );
static int32 RIPCALL rel_status_file(DEVICELIST *dev,
                                     uint8 *filename,
                                     STAT *statbuff );
static void * RIPCALL rel_start_list( DEVICELIST *dev, uint8 *pattern );
static int32 RIPCALL rel_next_list(DEVICELIST *dev,
                                   void **handle,
                                   uint8 *pattern,
                                   FILEENTRY *entry );
static int32 RIPCALL rel_end_list( DEVICELIST *dev, void *handle );
static int32 RIPCALL rel_rename_file(DEVICELIST *dev,
                                     uint8 *file1,
                                     uint8 *file2);
static int32 RIPCALL rel_delete_file( DEVICELIST *dev, uint8 *filename );
static int32 RIPCALL rel_set_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL rel_start_param( DEVICELIST *dev );
static int32 RIPCALL rel_get_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL rel_status_device( DEVICELIST *dev, DEVSTAT *devstat);
static int32 RIPCALL rel_dismount( DEVICELIST *dev );
/* static int32 RIPCALL rel_buffer_size( DEVICELIST *dev ); */
static int32 RIPCALL rel_spare( void );

/* ----------------------------------------------------------------------
The Relative Device type (17), implementation of which is internal to the rip
---------------------------------------------------------------------- */

/* The relative device type:
 * can have multiple relative devices,
 * each of which can have multiple relative files,
 * each of which can have multiple streams open on it.
 *
 * For each relative device there is an absolute device type.
 * For each relative file there is an absolute device.
 */

typedef struct RelativeFile {
 struct RelativeFile    *pNext;         /* list of files on relative device */
 int32                  nStreams;       /* number of streams open on file*/
 DEVICELIST             *pAbsDevice;    /* underlying absolute device */
 uint8                  filename[1];    /* name of the file */
 /* ... filename follows directly on in memory */
} RelativeFile;

/* The list of relative streams is kept in descriptor order
 * to makes finding an unused descriptor easier.
 */

typedef struct RelativeStream {
 struct RelativeStream  *pNext;         /* list of streams on relative device*/
 DEVICE_FILEDESCRIPTOR  desc;           /* relative file descriptor */
 DEVICE_FILEDESCRIPTOR  absDesc;        /* underlying absolute descriptor */
 RelativeFile           *pFile;         /* corresponding relative file */
} RelativeStream;

/* Basic parameters supported by relative device */
#define REL_ABSDEVTYPE_PARAM    "AbsoluteDeviceType"
#define REL_ABSDEVTYPE_INDEX    0
#define REL_ABSDEVPREFIX_PARAM  "AbsoluteDevicePrefix"
#define REL_ABSDEVPREFIX_INDEX  1

#define REL_BASIC_PARAMS        2      /* should be number of above */

/* Parameters used for communication with underlying device */
#define REL_FILENAME_PARAM      "Filename"

typedef struct RelativeParam {
 struct RelativeParam  *pNext;
 DEVICEPARAM            p;
} RelativeParam;

typedef struct {
 RelativeStream *pStreams;      /* head of stream linked list */
 RelativeFile   *pFiles;        /* head of file list */
 RelativeParam  absDevType;     /* absolute device type - ParamInteger */
 RelativeParam  absDevPrefix;   /* absolute device prefix - ParamString */
} RelativeDevice;

#define REL_FIRST_PARAM( pRelDev ) pRelDev->absDevType
#define REL_LAST_BASIC_PARAM( pRelDev ) pRelDev->absDevPrefix
#define REL_EXTRA_PARAMS( pRelDev ) pRelDev->absDevPrefix.pNext

/* define following if you want relative device to be searchable.
 * There would need to be some way to disable this as the main intended use
 * ( the progress plugin ) wouldn't want it.
 */
#undef RELATIVE_DEVICE_SEARCHABLE

/* ------------------------------------------------------------------------ */
/* Lookup a relative file by name, non NULL <=> found */
static RelativeFile *relFindFile( RelativeDevice *pRelDev, uint8 *filename )
{
  RelativeFile   *pRelFile;

  /* loop over relative files */
  for ( pRelFile = pRelDev->pFiles;
        pRelFile != NULL;
        pRelFile = pRelFile->pNext ) {
    /* does the filename match ? */
    if ( strcmp( (char *) filename, (char *) pRelFile->filename ) == 0 )
      break;
  }

  return pRelFile;
}

/* ------------------------------------------------------------------------ */
/* Look up relative stream by descriptor, non NULL <=> found */
static RelativeStream *relFindStream( RelativeDevice *pRelDev,
                                      DEVICE_FILEDESCRIPTOR desc )
{
  RelativeStream *pRelStream;

  for ( pRelStream = pRelDev->pStreams;
        pRelStream != NULL && pRelStream->desc < desc;
        pRelStream = pRelStream->pNext )
      EMPTY_STATEMENT() ;

  if ( pRelStream == NULL || pRelStream->desc != desc ) {
    devices_set_last_error(DeviceIOError);
    return NULL;
  }

  return pRelStream;
}


/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_init_device( DEVICELIST *dev )
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeParam  *pRelParam;
  DEVICEPARAM    *pParam;

  devices_set_last_error(DeviceNoError);

  pRelDev->pStreams = NULL;
  pRelDev->pFiles = NULL;

  pRelParam = &REL_FIRST_PARAM(pRelDev);
  pParam = &pRelParam->p;
  pParam->paramname = (uint8 *) REL_ABSDEVTYPE_PARAM;
  pParam->paramnamelen = sizeof( REL_ABSDEVTYPE_PARAM ) - 1;
  pParam->type = ParamInteger;
  pParam->paramval.intval = -1;      /* mark unset */
  pRelParam->pNext = &pRelDev->absDevPrefix;

  pRelParam = pRelParam->pNext;
  pParam = &pRelParam->p;
  pParam->paramname = (uint8 *) REL_ABSDEVPREFIX_PARAM;
  pParam->paramnamelen = sizeof( REL_ABSDEVPREFIX_PARAM ) - 1;
  pParam->type = ParamString;
  if ( (pParam->paramval.strval = mm_alloc_with_header(mm_pool_temp, 1, MM_ALLOC_CLASS_REL_STRING)
        ) == NULL ) {
    devices_set_last_error(DeviceVMError);
    return -1;
  }
  pParam->strvallen = 0;
  pRelParam->pNext = NULL;       /* end of parameter list marker */

  return 0;
}


/* ---------------------------------------------------------------------- */
static DEVICE_FILEDESCRIPTOR RIPCALL rel_open_file(DEVICELIST *dev,
                                                   uint8 *filename,
                                                   int32 openflags)
{
  int32            did_alloc = FALSE;
  DEVICE_FILEDESCRIPTOR desc = 0;
  DEVICE_FILEDESCRIPTOR absDesc;
  RelativeDevice   *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeFile     *pRelFile = relFindFile( pRelDev, filename );
  RelativeStream   *pRelStream = NULL;
  RelativeStream   *pNextRelStream;
  RelativeStream   *pPrevRelStream;
  DEVICELIST       *pAbsDev = NULL;
  int32            mounted = FALSE;
  int32            fileAlloc = FALSE;
  uint8            name[LONGESTDEVICENAME];

  DEVICEPARAM      p;

  /* ........................................................................ */
  /* find first free descriptor, avoiding name clashes if creating new device */
  for ( ;; ) {
    uint8         szDesc[32];
    DEVICEPARAM   *pPrefixParam = &pRelDev->absDevPrefix.p;

    /* this works since streams are kept in descriptor order */
    for ( pPrevRelStream = NULL, pNextRelStream = pRelDev->pStreams;
          pNextRelStream != NULL && pNextRelStream->desc == desc;
          pPrevRelStream=pNextRelStream, pNextRelStream=pNextRelStream->pNext, desc++ )
      EMPTY_STATEMENT() ;

    /* if file (and hence underlying absolute device) already exists then done */
    if ( pRelFile != NULL )
      break;

    /* build default prefix if unset */
    if ( pPrefixParam->strvallen == 0 ) {
      (void)strcpy( (char *) name, "Abs" );
      (void)strncat((char *) name,
                    (char *) dev->name,
                    LONGESTDEVICENAME - sizeof("Abs")) ; /* NB sizeof("Abs") is 4 */
      name[LONGESTDEVICENAME-1] = 0;
    } else {
      (void)strncpy((char *) name,
                    (char *) pPrefixParam->paramval.strval,
                    (uint32) pPrefixParam->strvallen);
    }

    /* form under device name */
    swcopyf( szDesc, (uint8 *) "%d", desc );      /* descriptor as string */
    name[ LONGESTDEVICENAME-1 - strlen_uint32((char *)&szDesc) ] = 0;/*truncate prefix*/
    (void)strcat( (char *) name, (char *) szDesc );

    if ( find_device(name) == NULL )
    {
      /* no name clash */
      break;
    }

    desc++;
  }

  /* ........................................................................ */
  /* If file is not already open create file and underlying absolute device */
  if ( pRelFile != NULL ) {
    pAbsDev = pRelFile->pAbsDevice;       /* file already open */
  } else {
    /* check have set underlying device type */
    if ( theDevParamInteger(pRelDev->absDevType.p) == -1 ) {
      devices_set_last_error(DeviceIOError);
      return -1;
    }

    pAbsDev = find_device(name);
    if ( pAbsDev == NULL ) {
      pAbsDev = device_alloc(name, strlen((char*)name));
      if ( pAbsDev == NULL ) {
        devices_set_last_error(DeviceVMError);
        return -1;
      }
      did_alloc = TRUE;
    }

    if ( !device_connect(pAbsDev,
                         theDevParamInteger(pRelDev->absDevType.p),
                         (char *)theIDevName(pAbsDev), theIDeviceFlags(pAbsDev),
                         FALSE) ) {
      devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev)) ;
      if ( did_alloc ) {
        device_free(pAbsDev);
      }
      goto rel_open_file_xx_error ;
    }

    device_add(pAbsDev);
    mounted = TRUE;               /* from here on need to unwind on error */

    SetEnableDevice( pAbsDev );

    /* pass on extra params to underlying device */
    {
      RelativeParam        *pRelParam;

      for ( pRelParam = REL_EXTRA_PARAMS( pRelDev );
            pRelParam != NULL;
            pRelParam = pRelParam->pNext ) {
        int32        result;

        /* insist that parameters are accepted or ignored by underlying device */
        result = (*theISetParam( pAbsDev ))( pAbsDev, (void *) &pRelParam->p );
        if ( result != ParamAccepted && result != ParamIgnored )
          goto rel_open_file_io_error;
      }
    }

    /* pass on Filename parameter */
    {
      int32         result;

      p.paramname = (uint8 *) REL_FILENAME_PARAM;
      p.paramnamelen = sizeof(REL_FILENAME_PARAM) - 1;    /* -1 for terminator */
      p.type = ParamString;
      p.paramval.strval = filename;
      p.strvallen = strlen_int32((char *) filename);
      result = (*theISetParam( pAbsDev ))( pAbsDev , &p);

      if ( result != ParamAccepted && result != ParamIgnored )
        goto rel_open_file_io_error;
    }

    /* ........................................................................ */
    /* Do the things that might fail (allocs and open) before updating state.  */

    /* allocate file entry if new file */
    /* RelativeFile already has byte allocated for filename terminator */
    if ( (pRelFile = (RelativeFile *)mm_alloc_with_header(mm_pool_temp,
                                                          sizeof( RelativeFile ) + p.strvallen,
                                                          MM_ALLOC_CLASS_REL_FILE)) == NULL ) {
      devices_set_last_error(DeviceVMError);
      goto rel_open_file_xx_error;
    }
    fileAlloc = TRUE;
  }

  /* allocate stream entry */
  if ( ( pRelStream = (RelativeStream *) mm_alloc(mm_pool_temp,
                                                  sizeof( RelativeStream ),
                                                  MM_ALLOC_CLASS_REL_STREAM) ) == NULL ) {
    devices_set_last_error(DeviceVMError);
    goto rel_open_file_xx_error;
  }

  /* open underlying device */
  absDesc = (*theIOpenFile(pAbsDev))( pAbsDev, filename, openflags );
  if ( absDesc == -1 ) {
    devices_set_last_error((*theILastErr(pAbsDev))( pAbsDev ));
    goto rel_open_file_xx_error;

  rel_open_file_io_error:
    devices_set_last_error(DeviceIOError);
  rel_open_file_xx_error:
    if (pRelStream != NULL )
      mm_free(mm_pool_temp, pRelStream, sizeof( RelativeStream ) );
    if (fileAlloc)
      mm_free_with_header(mm_pool_temp, pRelFile );
    if (mounted)
      (void)device_dismount( name );
    return -1;
  }

  /* ------------------------------------------------------------------------ */
  /* if we get here we have succeeded, fill and link in structures */

  if ( fileAlloc ) {              /* fill and link in file if new */
    pRelFile->pNext = pRelDev->pFiles;
    pRelFile->nStreams = 1;
    pRelFile->pAbsDevice = pAbsDev;
    (void)strcpy( (char *) &pRelFile->filename, (char *) filename );
    pRelDev->pFiles = pRelFile;
  } else
    pRelFile->nStreams++;     /* otherwise just count new stream */

  /* fill and link in stream */
  pRelStream->pNext = pNextRelStream;
  pRelStream->desc = desc;
  pRelStream->absDesc = absDesc;
  pRelStream->pFile = pRelFile;
  if (pPrevRelStream == NULL) {
    pRelDev->pStreams = pRelStream;
  } else {
    pPrevRelStream->pNext = pRelStream;
  }

  /* make under device undismountable,
     so that it can't be taken away from beneath our feet. */
  SetUndismountableDevice( pAbsDev );

  return desc;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_read_file(DEVICELIST       *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   uint8            *buff,
                                   int32            len)
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeStream *pRelStream = relFindStream( pRelDev, descriptor );
  DEVICELIST     *pAbsDev;
  int32          result;

  if (pRelStream == NULL) {
    devices_set_last_error(DeviceIOError);
    return -1;
  }

  pAbsDev = pRelStream->pFile->pAbsDevice;
  result = (*theIReadFile(pAbsDev))(pAbsDev,
                                    pRelStream->absDesc,
                                    buff,
                                    len);
  if (result == -1)
    devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev));

  return result;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_write_file(DEVICELIST       *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8            *buff,
                                    int32            len)
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeStream *pRelStream = relFindStream( pRelDev, descriptor );
  DEVICELIST     *pAbsDev;
  int32          result;

  if (pRelStream == NULL) {
    devices_set_last_error(DeviceIOError);
    return -1;
  }

  pAbsDev = pRelStream->pFile->pAbsDevice;
  result = (*theIWriteFile(pAbsDev))(pAbsDev,
                                     pRelStream->absDesc,
                                     buff,
                                     len);
  if (result == -1)
    devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev));

  return result;
}

/* ---------------------------------------------------------------------- */
static int32 relCloseCommon(DEVICELIST *dev,
                            DEVICE_FILEDESCRIPTOR descriptor,
                            int32 abortFlag)
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeStream *pRelStream;
  RelativeStream *pPrevRelStream;
  RelativeFile   *pRelFile;
  DEVICELIST     *pAbsDev;
  int32          result;

  /* find stream (and previous) in list */
  for ( pPrevRelStream = NULL, pRelStream = pRelDev->pStreams;
        pRelStream != NULL && pRelStream->desc < descriptor;
        pPrevRelStream = pRelStream, pRelStream = pRelStream->pNext )
    EMPTY_STATEMENT() ;

  if (pRelStream == NULL || pRelStream->desc != descriptor ) {
    result = -1;
    goto relCloseCommonError;
  }

  pRelFile = pRelStream->pFile;
  pAbsDev = pRelFile->pAbsDevice;

  /* close file */
  result = abortFlag
    ? (*theIAbortFile(pAbsDev))( pAbsDev, pRelStream->absDesc )
    : (*theICloseFile(pAbsDev))( pAbsDev, pRelStream->absDesc );

  /* unlink and free stream node */
  if ( pPrevRelStream == NULL ) {
    pRelDev->pStreams = pRelStream->pNext;
  } else {
    pPrevRelStream->pNext = pRelStream->pNext;
  }
  mm_free(mm_pool_temp, pRelStream, sizeof( RelativeStream ) );

  /* unlink and free file node if last stream open on file */
  if ( --pRelFile->nStreams == 0 ) {
    RelativeFile  *pPrevRelFile = pRelDev->pFiles;

    if ( pPrevRelFile == pRelFile ) {       /* special case first in list */
      pRelDev->pFiles = pRelFile->pNext;
    } else {                              /* otherwise need to find previous */
      for (;;) {
        if (pPrevRelFile->pNext ==  pRelFile ) {
          pPrevRelFile->pNext = pRelFile->pNext;
          break;
        }
        pPrevRelFile = pPrevRelFile->pNext;
      }
    }
    mm_free_with_header(mm_pool_temp, pRelFile );
  }

  /* dismount underlying device */
  ClearUndismountableDevice( pAbsDev );
  if ( !device_dismount(pAbsDev->name) )
    result = -1;

  if ( result != 0 ) {
  relCloseCommonError:
    devices_set_last_error(DeviceIOError);
  }

  return result;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_close_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor)
{
 return relCloseCommon( dev, descriptor, FALSE );
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_abort_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor)
{
 return relCloseCommon( dev, descriptor, TRUE );
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_seek_file(DEVICELIST *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 * destn,
                                   int32 flags)
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeStream *pRelStream = relFindStream( pRelDev, descriptor );
  DEVICELIST     *pAbsDev;
  int32          result;

  if (pRelStream == NULL) {
    devices_set_last_error(DeviceIOError);
    return FALSE;
  }

  pAbsDev = pRelStream->pFile->pAbsDevice;

  result = (*theISeekFile(pAbsDev))(pAbsDev,
                                    pRelStream->absDesc,
                                    destn,
                                    flags);
  if (! result)
    devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev));

  return result;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_bytes_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * bytes,
                                    int32 reason )
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeStream *pRelStream = relFindStream( pRelDev, descriptor );
  DEVICELIST     *pAbsDev;
  int32          result;

  if (pRelStream == NULL) {
    devices_set_last_error(DeviceIOError);
    return FALSE;
  }

  pAbsDev = pRelStream->pFile->pAbsDevice;

  result = (*theIBytesFile(pAbsDev)) ( pAbsDev, pRelStream->absDesc, bytes, reason );
  if (! result)
    devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev));

  return result;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_status_file( DEVICELIST *dev,
                                      uint8 *filename,
                                      STAT *statbuff)
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeFile   *pRelFile = relFindFile( pRelDev, filename );
  DEVICELIST     *pAbsDev = NULL;
  int32          result;

  /* error if no match */
  if ( pRelFile == NULL ) {
    devices_set_last_error(DeviceIOError);
    return -1;
  }

  pAbsDev = pRelFile->pAbsDevice;
  result = (*theIStatusFile(pAbsDev))(pAbsDev,
                                      filename,
                                      statbuff);
  if (result == -1)
    devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev));

  return result;
}

/* ---------------------------------------------------------------------- */
static void * RIPCALL rel_start_list( DEVICELIST *dev, uint8 *pattern )
{
  UNUSED_PARAM(uint8 *, pattern);

#ifdef RELATIVE_DEVICE_SEARCHABLE
  {
    RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
    return (void *) &pRelDev->pFiles;
  }
#else
  UNUSED_PARAM(DEVICELIST *, dev);
  devices_set_last_error(DeviceIOError);
  return NULL;
#endif
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_next_list(DEVICELIST *dev,
                                   void **handle,
                                   uint8 *pattern,
                                   FILEENTRY *entry)
{
#ifdef RELATIVE_DEVICE_SEARCHABLE
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeFile *pppRelFile = (RelativeFile ***) handle;
  RelativeFile *pRelFile;

  for ( pRelFile = **pppRelFile;
        pRelFile != NULL;
        pRelFile = pRelFile->pNext ) {
    if ( SwPatternMatch( pattern, pRelFile->filename ) ) {
      entry->namelength = strlen_int32( pRelFile->filename );
      entry->name = pRelFile->filename;
      *pppRelFile = &pRelFile->pNext;
      return FileNameMatch ;
    }
  }
  return FileNameNoMatch;
#else
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  devices_set_last_error(DeviceIOError);
  return -1;
#endif
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_end_list( DEVICELIST *dev, void *handle )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void *, handle);

  return 0;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_rename_file(DEVICELIST *dev,
                                     uint8 *file1,
                                     uint8 *file2)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  /* This could be implemented to (delete file2 if it exists and,) set FileName
   * parameter for file1 to file2. */

  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_delete_file(DEVICELIST *dev, uint8 *filename)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);

  /* This could be implemented to be abort all streams on filename. */

  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeParam  *pRelParam;
  int32          paramIndex = 0;
  int32          paramAlloc = FALSE;
  int32          result;

  /* Filename - disallowed as reserved for passing filename to under device */
  if ( param->paramnamelen == sizeof(REL_FILENAME_PARAM) - 1 &&
       strncmp((char *)param->paramname,
               REL_FILENAME_PARAM,
               (uint32)param->paramnamelen) == 0 )
    return ParamIgnored;

  /* Match parameter name and check type if found */
  for ( pRelParam = &REL_FIRST_PARAM( pRelDev );
        pRelParam != NULL;
        paramIndex++, pRelParam = pRelParam->pNext ) {
    if ( param->paramnamelen == pRelParam->p.paramnamelen &&
         strncmp((char *) pRelParam->p.paramname,
                 (char *) param->paramname,
                 (uint32) param->paramnamelen) == 0 ) {
      if ( param->type != pRelParam->p.type )
        return ParamTypeCheck;
      break;
    }
  }

  /* Check and apply basic parameters here */
  switch (paramIndex) {
  case REL_ABSDEVTYPE_INDEX: {
    int32        nDeviceType = param->paramval.intval;
    DEVICETYPE   *pDevType = find_device_type( nDeviceType, FALSE );

    /* don't allow changing of underlying device type while streams open */
    if ( pRelDev->pStreams != NULL ) {
      devices_set_last_error(DeviceIOError);
      return ParamError;
    }
    if ( pDevType == NULL )
      return ParamConfigError;
    break;
  }
  case REL_ABSDEVPREFIX_INDEX:
    if ( param->strvallen >= LONGESTDEVICENAME )
      return ParamRangeCheck;
    break;
  }

  /* if parameter not seen before allocate a new parameter list entry */
  if ( pRelParam == NULL ) {
    pRelParam = (RelativeParam *) mm_alloc_with_header(mm_pool_temp,
                                                       sizeof( RelativeParam ),
                                                       MM_ALLOC_CLASS_REL_PARAM);
    if ( pRelParam == NULL )
      goto rel_set_param_vm_error;
    paramAlloc = TRUE;
  } else { /* else if string type free previous string value */
    if ( pRelParam->p.type == ParamString ) {
      mm_free_with_header(mm_pool_temp, pRelParam->p.paramval.strval );
    }
  }

  /* update entry in parameter list */
  HqMemCpy( &pRelParam->p , param , sizeof( DEVICEPARAM )) ;
  if ( param->type == ParamString ) {
    uint8 *pStr = mm_alloc_with_header(mm_pool_temp, param->strvallen,
                                       MM_ALLOC_CLASS_REL_STRING);

    if ( pStr == NULL) {
      if (paramAlloc)
        mm_free_with_header(mm_pool_temp, pRelParam );
      goto rel_set_param_vm_error;
    }
    HqMemCpy( pStr , param->paramval.strval , param->strvallen ) ;
    pRelParam->p.paramval.strval = pStr;
  }

  /* If new parameter add it to the end list to preserve ordering */
  if (paramAlloc) {
    RelativeParam *pLastRelParam;

    pRelParam->pNext = NULL;
    for ( pLastRelParam = &REL_LAST_BASIC_PARAM( pRelDev );
          pLastRelParam->pNext != NULL;
          pLastRelParam = pLastRelParam->pNext )
      EMPTY_STATEMENT() ;

    pLastRelParam->pNext = pRelParam;
  }

  /* pass non basic parameters on to under devices */
  result = ParamAccepted;
  if ( paramIndex >= REL_BASIC_PARAMS ) {
    RelativeFile  *pRelFile;

    for ( pRelFile = pRelDev->pFiles;
          pRelFile != NULL;
          pRelFile = pRelFile->pNext ) {
      int32        subResult;
      DEVICELIST   *pAbsDev = pRelFile->pAbsDevice;

      subResult = (*theISetParam( pAbsDev ))( pAbsDev , param );
      /*
       * Only note the last error from the underlying devices, since there is no
       * obvious prioritisation of errors, and they are likely to return the same
       * error anyway.
       */
      if ( subResult != ParamAccepted ) {
        if ( (result=subResult) == ParamError ) {
          devices_set_last_error((*theILastErr(pAbsDev))(pAbsDev));
        }
      }
    }
  }

  return result;

rel_set_param_vm_error:
  devices_set_last_error(DeviceVMError);
  return ParamError;
}

static RelativeParam    *p_rel_next_param;

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_start_param( DEVICELIST *dev )
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeParam  *pRelParam = &REL_FIRST_PARAM( pRelDev );
  int32          count = 0;

  p_rel_next_param = pRelParam;

  while ( pRelParam != NULL ) {
    count++;
    pRelParam = pRelParam->pNext ;
  }

  return count;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_get_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  DEVICEPARAM    *pParam;

  /* If getting named param set up state as for iterating over params */
  if ( param->paramname != NULL ) {
    for ( p_rel_next_param = &REL_FIRST_PARAM( pRelDev );
          p_rel_next_param != NULL;
          p_rel_next_param = p_rel_next_param->pNext ) {
      if ( param->paramnamelen == p_rel_next_param->p.paramnamelen &&
           strncmp((char *) param->paramname,
                   (char *) p_rel_next_param->p.paramname,
                   (uint32) param->paramnamelen) == 0 )
        break;
    }
  }

  if ( p_rel_next_param == NULL )
    return ParamIgnored;

  pParam = &p_rel_next_param->p;

  if ( param->paramname == NULL ) {
    param->paramname = pParam->paramname;
    param->paramnamelen = pParam->paramnamelen;
  }

  HqMemMove( &param->paramval , &pParam->paramval , sizeof( param->paramval )) ;

  if ( pParam->type == ParamString )
    param->strvallen = pParam->strvallen;

  return ParamAccepted;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devstat->block_size = 0;
  HqU32x2FromInt32(&devstat->size, 0);
  HqU32x2FromInt32(&devstat->free, 0);
  devstat->start = NULL;
  return 0;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_dismount( DEVICELIST *dev )
{
  RelativeDevice *pRelDev = (RelativeDevice *) (theIPrivate(dev));
  RelativeParam  *pRelParam;
  int32          result = 0;

  /* abort all open streams */
  while ( pRelDev->pStreams != NULL ) {
    if ( rel_abort_file( dev, pRelDev->pStreams->desc ) )
      result = -1;
  }

  /* free additional parameter records and strings hanging off them */
  pRelParam = REL_EXTRA_PARAMS( pRelDev );
  while ( pRelParam != NULL ) {
    RelativeParam *pNextRelParam = pRelParam->pNext;

    if ( pRelParam->p.type == ParamString ) {
      mm_free_with_header(mm_pool_temp, pRelParam->p.paramval.strval );
    }
    mm_free_with_header(mm_pool_temp, pRelParam );
    pRelParam = pNextRelParam;
  }
  REL_EXTRA_PARAMS( pRelDev ) = NULL;

  /* free the default underlying device prefix */
  mm_free_with_header(mm_pool_temp, pRelDev->absDevPrefix.p.paramval.strval );
  pRelDev->absDevPrefix.p.paramval.strval = NULL;

  return result;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL rel_spare( void )
{
  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
DEVICETYPE Rel_Device_Type = {
  RELATIVE_DEVICE_TYPE,           /* the device ID number */
  DEVICERELATIVE | DEVICEWRITABLE,/* flags to indicate specifics of device */
  sizeof(RelativeDevice),         /* the size of the private data */
  0,                              /* minimum ticks between tickle functions */
  NULL,                           /* procedure to service the device */
  devices_last_error,             /* return last error for this device */
  rel_init_device,                /* call to initialise device */
  rel_open_file,                  /* call to open file on device */
  rel_read_file,                  /* call to read data from file on device */
  rel_write_file,                 /* call to write data to file on device */
  rel_close_file,                 /* call to close file on device */
  rel_abort_file,                 /* call to abort action on the device */
  rel_seek_file,                  /* call to seek file on device */
  rel_bytes_file,                 /* call to get bytes avail on an open file */
  rel_status_file,                /* call to check status of file */
  rel_start_list,                 /* call to start listing files */
  rel_next_list,                  /* call to get next file in list */
  rel_end_list,                   /* call to end listing */
  rel_rename_file,                /* rename file on the device */
  rel_delete_file,                /* remove file from device */
  rel_set_param,                  /* call to set device parameter */
  rel_start_param,                /* call to start getting device parameters */
  rel_get_param,                  /* call to get the next device parameter */
  rel_status_device,              /* call to get the status of the device */
  rel_dismount,                   /* call to dismount the device */
  NULL,                           /* use default small buffer size */
  NULL,                         /* ignore ioctl calls */
  rel_spare                       /* spare slots */
};

void init_C_globals_reldev(void)
{
  p_rel_next_param = NULL ;
}

/* Log stripped */
