/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_filesys.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for PMS file system.
 *
 */

#include "pms_filesys.h"

#include "pms_malloc.h"
#include <string.h>


/* Use PJL error numbers to ease error reporting */
enum {
  eNoError                       = 0,

  eGeneralFileSystemError        = 32000,
  eVolumeNotAvailable            = 32001,
  eDiskFull                      = 32002,
  eFileNotFound                  = 32003,
  eNoFreeFileDescriptors         = 32004,
  eInvalidNoOfBytes              = 32005,
  eFileAlreadyExists             = 32006,
  eIllegalName                   = 32007,
  eCannotDeleteRoot              = 32008,
  eFileOpOnDirectory             = 32009,
  eDirectoryOpOnFile             = 32010,
  eNotSameVolume                 = 32011,
  eReadOnly                      = 32012,
  eDirectoryFull                 = 32013,
  eDirectoryNotEmpty             = 32014,
  eBadDisk                       = 32015,
  eNoLable                       = 32016,
  eInvalidParameter              = 32017,
  eNoContiguousSpace             = 32018,
  eCannotChangeRoot              = 32019,
  eFileDescriptorObselete        = 32020,
  eDeleted                       = 32021,
  eNoBlockDevice                 = 32022,
  eBadSeek                       = 32023,
  eInternalError                 = 32024,
  eWriteOnly                     = 32025,
  eWriteProtected                = 32026,
  eNoFilename                    = 32027,

  eEndOfDirectory                = 32051,
  eNoFileSystem                  = 32052,
  eNoMemory                      = 32053,
  eVolumeNameOutOfRange          = 32054,
  eBadFS                         = 32055,
  eHardwareFailure               = 32056,
  eAccessDenied                  = 32064
};


/* Memory pool to use for allocations */
#define PMS_MemoryPoolFileSys PMS_MemoryPoolPMS


struct PMS_FS_Entry;
typedef struct PMS_FS_Entry PMS_FS_Entry;

typedef struct PMS_FS_Dir
{
  int            cEntries;
  PMS_FS_Entry * pEntries;

} PMS_FS_Dir;

typedef struct PMS_FS_File
{
  int             openFlags;
  int             cbSize;
  int             position;
  unsigned char * contents;

} PMS_FS_File;

struct PMS_FS_Entry
{
  PMS_FS_Entry * pNext;
  PMS_FS_Entry * pPrev;

  PMS_FS_Entry * pParent;

  unsigned char name[ PMS_FSENTRY_NAMELEN + 1 ];

  PMS_eFileType type;     /* ePMS_FS_Dir / ePMS_FS_File */

  union
  {
    PMS_FS_Dir d;
    PMS_FS_File f;
  } u;

};


typedef struct PMS_Volume
{
  int index;

  char * pLocation;
  char * pLabel;

  int cbCapacity;
  int cbFree;

  PMS_FS_Entry root;

} PMS_Volume;

/* One tiny disk, so that it's easy to fill it up, and one rather larger disk */
static PMS_Volume gVolumes[] =
{
  {
    0,                                          /* index */
    "Location 0",                               /* pLocation */
    "Label 0",                                  /* pLabel */
    4 * 1024 * 1024,                            /* cbCapacity */
    4 * 1024 * 1024,                            /* cbFree */
    { NULL, NULL, NULL, "root", ePMS_FS_Dir }   /* root */
  },
  {
    1,                                          /* index */
    "Location 1",                               /* pLocation */
    "Label 1",                                  /* pLabel */
    4 * 1024,                                   /* cbCapacity */
    4 * 1024,                                   /* cbFree */
    { NULL, NULL, NULL, "root", ePMS_FS_Dir }   /* root */
  }
};
#define PMS_N_VOLUMES ( sizeof( gVolumes ) / sizeof( gVolumes[ 0 ] ) )

static int gDisklock = FALSE;

static unsigned char gElement[ PMS_FSENTRY_NAMELEN + 1 ];


/**
 * \brief Find volume for entry
 */
static PMS_Volume * VolumeForEntry( PMS_FS_Entry * pEntry )
{
  int i;

  while( pEntry->pParent != NULL )
  {
    pEntry = pEntry->pParent;
  }

  for( i = 0; i < PMS_N_VOLUMES; i++ )
  {
    if( &gVolumes[ i ].root == pEntry )
    {
      return &gVolumes[ i ];
    }
  }

  return NULL;
} 


/**
 * \brief Allocate memory from file system
 */
static void * FSMalloc( PMS_Volume * pVolume, int cbSize )
{
  void * p = NULL;

  if( pVolume->cbFree >= cbSize )
  {
    p = OSMalloc( cbSize, PMS_MemoryPoolFileSys );

    if( p != NULL )
    {
      pVolume->cbFree -= cbSize;
    }
  }

  return p;
} 


/**
 * \brief Free memory from file system
 */
static void FSFree( PMS_Volume * pVolume, void * p, int cbSize )
{
  OSFree( p, PMS_MemoryPoolFileSys );
  pVolume->cbFree += cbSize;
} 


/**
 * \brief Parse volume part of filename.
 *
 * Copies volume part of pzName into gElement, e.g. "0:".  Does not include
 * any following backslashes.
 * If ppNext is not NULL sets *ppNext to point to first non-backslash character
 * following volume name.
 */
static unsigned char * ParseVolume( unsigned char * pzName, unsigned char ** ppNext )
{
  unsigned char * p = pzName;
  unsigned char * v = gElement;

  while( *p >= '0' && *p <= '9' )
  {
    *v++ = *p++;
  }

  if( *p == ':' )
  {
    *v++ = *p++;
    *v = '\0';
  }
  else
  {
    gElement[ 0 ] = '\0';
  }

  if( *p != '\\' && *p != '\0' )
  {
    gElement[ 0 ] = '\0';
  }

  while( *p == '\\' )
  {
    p++;
  }

  if( ppNext )
  {
    *ppNext = p;
  }

  return gElement;
}


/**
 * \brief Parse path element of filename.
 *
 * Copies path element, i.e. text between backslashes, from pzName into gElement.
 * If ppNext is not NULL sets *ppNext to point to first non-backslash character
 * following path element, i.e. to start of next element.
 */
static unsigned char * ParseElement( unsigned char * pzName, unsigned char ** ppNext )
{
  unsigned char * p = pzName;
  unsigned char * e = gElement;

  while( *p == '\\' )
  {
    p++;
  }

  while( *p != '\0' && *p != '\\' )
  {
    *e++ = *p++;
  }
  *e = '\0';

  while( *p == '\\' )
  {
    p++;
  }

  if( ppNext )
  {
    *ppNext = p;
  }

  return gElement;
}


/**
 * \brief Splits filename into parent and leafname.
 */
static void SplitName( unsigned char * pzName, unsigned char * pParent, unsigned char * pLeafname )
{
  unsigned char * p = pzName;
  unsigned char * e;

  e = ParseVolume( p, &p );
  strcpy( (char *) pParent, (char *) e );

  if( *p == '\0' )
  {
    /* Just a volume name */
    *pLeafname = '\0';
  }
  else
  {
    int fLeafname = FALSE;

    while( !fLeafname )
    {
      e = ParseElement( p, &p );

      if( *p == '\0' )
      {
        strcpy( (char *) pLeafname, (char *) e );
        fLeafname = TRUE;
      }
      else
      {
        strcat( (char *) pParent, "\\" );
        strcat( (char *) pParent, (char *) e );
      }
    }
  }
}


/**
 * \brief Finds PMS_Volume structure for pzVolume.
 *
 * If successful sets *ppVolume to point to structure.
 * If ppNext is not NULL sets *ppNext to point to first non-backslash character
 * following volume name.
 * Returns one of the error enumeration values.
 */
static int FindVolume( unsigned char * pzVolume, PMS_Volume ** ppVolume, unsigned char ** ppEnd )
{
  int eError = eNoError;

  unsigned char * p;

  p = ParseVolume( pzVolume, ppEnd );

  if( p[ 0 ] != '\0' )
  {
    int nVolume = 0;

    while( *p != ':' )
    {
      nVolume *= 10;
      nVolume += *p - '0';
      p++;
    }

    if( nVolume < PMS_N_VOLUMES )
    {
      *ppVolume = &gVolumes[ nVolume ];
    }
    else
    {
      eError = eVolumeNameOutOfRange;
    }
  }
  else
  {
    eError = eIllegalName;
  }

  return eError;
}
  

/**
 * \brief Finds named entry in a directory.
 *
 * If successful returns pointer to entry, otherwise NULL if name does
 * not match any entry.
 */
static PMS_FS_Entry * FindDirEntry( PMS_FS_Dir * pDir, unsigned char * pzName )
{
  PMS_FS_Entry * pEntry = pDir->pEntries;

  while( pEntry != NULL )
  {
    int cmp = strcmp( (const char *) pzName, (const char *) pEntry->name );

    if( cmp == 0 )
    {
      /* Found entry */
      break;
    }
    else if( cmp < 0 )
    {
      /* Gone past entry */
      pEntry = NULL;
    }
    else
    {
      pEntry = pEntry->pNext;
    }
  }

  return pEntry;
}


/**
 * \brief Creates new named entry in a directory.
 *
 * If successful returns pointer to the new entry, otherwise NULL if memory
 * cannot be allocated for entry.
 */
static PMS_FS_Entry * AddDirEntry( PMS_FS_Entry * pDirEntry, unsigned char * pzName )
{
  PMS_FS_Entry * pNewEntry = FSMalloc( VolumeForEntry( pDirEntry ), sizeof(PMS_FS_Entry) );

  if( pNewEntry != NULL )
  {
    PMS_FS_Dir * pDir = &pDirEntry->u.d;

    pNewEntry->pNext = NULL;
    pNewEntry->pPrev = NULL;
    pNewEntry->pParent = pDirEntry;
    strcpy( (char *) pNewEntry->name, (char *) pzName );

    if( pDir->cEntries == 0 )
    {
      /* No entries, so new one is first in list */
      pDir->pEntries = pNewEntry;
    }
    else
    {
      /* Link into list of entries in alphabetical order */
      PMS_FS_Entry * pNext = pDir->pEntries;
      PMS_FS_Entry * pPrev = NULL;

      while( pNext != NULL )
      {
        if( strcmp( (const char *) pNewEntry->name, (const char *) pNext->name ) < 0 )
        {
          /* Insert new entry after pPrev, before pNext */
          break;
        }

        pPrev = pNext;
        pNext = pNext->pNext;
      }

      if( pPrev != NULL )
      {
        pPrev->pNext = pNewEntry;
      }
      else
      {
        pDir->pEntries = pNewEntry;
      }

      if( pNext != NULL )
      {
        pNext->pPrev = pNewEntry;
      }

      pNewEntry->pNext = pNext;
      pNewEntry->pPrev = pPrev;
    }

    pDir->cEntries++;
  }

  return pNewEntry;
}
  

/**
 * \brief Creates new named directory in a directory.
 *
 * If successful returns pointer to the new entry, otherwise NULL if memory
 * cannot be allocated for entry.
 */
static PMS_FS_Entry * CreateDir( PMS_FS_Entry * pDir, unsigned char * pzName )
{
  PMS_FS_Entry * pNewEntry = AddDirEntry( pDir, pzName );

  if( pNewEntry != NULL )
  {
    pNewEntry->type = ePMS_FS_Dir;
    pNewEntry->u.d.cEntries = 0;
    pNewEntry->u.d.pEntries = NULL;
  }

  return pNewEntry;
}


/**
 * \brief Creates new named file in a directory.
 *
 * If successful returns pointer to the new entry, otherwise NULL if memory
 * cannot be allocated for entry.
 */
static PMS_FS_Entry * CreateFile( PMS_FS_Entry * pDir, unsigned char * pzName )
{
  PMS_FS_Entry * pNewEntry = AddDirEntry( pDir, pzName);

  if( pNewEntry != NULL )
  {
    pNewEntry->type = ePMS_FS_File;
    pNewEntry->u.f.openFlags = 0;
    pNewEntry->u.f.cbSize = 0;
    pNewEntry->u.f.position = 0;
    pNewEntry->u.f.contents = NULL;
  }

  return pNewEntry;
}


/**
 * \brief Remove entry from its parent directory.
 *
 * Does not free memory for entry.
 */
static void RemoveEntryFromParentDir( PMS_FS_Entry * pEntry )
{
  PMS_FS_Dir * pDir = &pEntry->pParent->u.d;

  if( pEntry->pPrev == NULL )
  {
    /* Removing first entry in list */
    pDir->pEntries = pEntry->pNext;
  }
  else
  {
    pEntry->pPrev->pNext = pEntry->pNext;
  }

  if( pEntry->pNext != NULL )
  {
    pEntry->pNext->pPrev = pEntry->pPrev;
  }

  pDir->cEntries--;
}


/**
 * \brief Locate entry from path name.
 *
 * If successful sets *ppEntry to point to entry.  Additionally if *ppVolume
 * is not NULL it is set to volume for entry.
 * Returns one of the error enumeration values.
 */
static int FindEntry( unsigned char * pzPath, PMS_Volume ** ppVolume, PMS_FS_Entry ** ppEntry )
{
  int eError = eNoError;

  PMS_FS_Entry  * pEntry = NULL;
  PMS_Volume    * pVolume;
  unsigned char * pName;

  eError = FindVolume( pzPath, &pVolume, &pName );

  if( eError == eNoError )
  {
    if( *pName == '\0' )
    {
      /* pzName was just a volume name so return top-level dir of volume */
      pEntry = &pVolume->root;
    }
    else
    {
      PMS_FS_Dir    * pDir = &pVolume->root.u.d;
      unsigned char * pElement;

      do
      {
        pElement = ParseElement( pName, &pName );
        pEntry = FindDirEntry( pDir, pElement );

        if( pEntry == NULL )
        {
          /* Entry not found */
          eError = eFileNotFound;
          break;
        }

        if( *pName == '\0' )
        {
          /* End of name, so entry found */
          break;
        }

        if( pEntry->type == ePMS_FS_Dir )
        {
          pDir = &pEntry->u.d;
        }
        else
        {
          /* Have reached file before reaching end of name */
          eError = eIllegalName;
          pEntry = NULL;
        }

      } while( pEntry != NULL );
    }
  }

  if( eError == eNoError )
  {
    *ppEntry = pEntry;

    if( ppVolume != NULL )
    {
      *ppVolume = pVolume;
    }
  }

  return eError;
}


/**
 * \brief Free memory holding file contents.
 *
 * Returns one of the error enumeration values.
 */
static int FreeFileContent( PMS_FS_Entry * pFileEntry )
{
  PMS_FS_File * pFile = &pFileEntry->u.f;

  if( pFile->contents != NULL )
  {
    FSFree( VolumeForEntry( pFileEntry ), pFile->contents, pFile->cbSize );

    pFile->cbSize = 0;
    pFile->position = 0;
    pFile->contents = NULL;
  }

  return eNoError;
}


/**
 * \brief Recursively delete directory and all its contents.
 *
 * Returns one of the error enumeration values.
 */
static int DeleteDir( PMS_FS_Dir * pDir )
{
  int eError = eNoError;

  PMS_FS_Entry * pEntry = pDir->pEntries;
  PMS_FS_Entry * pNext;

  while( pEntry != NULL && eError == eNoError )
  {
    pNext = pEntry->pNext;

    if( pEntry->type == ePMS_FS_Dir )
    {
      eError = DeleteDir( &pEntry->u.d );
    }
    else
    {
      eError = FreeFileContent( pEntry );
    }

    if( eError == eNoError )
    {
      FSFree( VolumeForEntry( pEntry ), pEntry, sizeof( PMS_FS_Entry ) );
      pDir->cEntries--;

      pEntry = pNext;
    }
    else
    {
      pDir->pEntries = pEntry;
    }
  }

  if( eError == eNoError )
  {
    pDir->pEntries = NULL;
  }

  return eError;
}

/**
 * \brief Reports details of an entry.
 *
 * Fills in details for entry in *pStat.
 */
static void GetEntryDetails( PMS_FS_Entry * pEntry, PMS_TyStat * pStat )
{
  memcpy( pStat->aName, pEntry->name, PMS_FSENTRY_NAMELEN + 1 );
  pStat->type = pEntry->type;

  if( pEntry->type == ePMS_FS_File )
  {
    pStat->cbSize = (unsigned long) pEntry->u.f.cbSize;
  }
}


/**
 * \brief Initialise the file system.
 */
void PMS_FS_InitFS(void)
{
}


/**
 * \brief Shutdown the file system.
 */
void PMS_FS_ShutdownFS(void)
{
  int i;

  for( i = 0; i < PMS_N_VOLUMES; i++ )
  {
    DeleteDir( &gVolumes[ i ].root.u.d );
  }
}


/**
 * \brief Initialise a volume, deleting all its contents.
 *
 * Returns one of the error enumeration values.
 */
int PMS_FS_InitVolume( unsigned char * pzVolume )
{
  int eError = eNoError;

  PMS_Volume * pVolume;

  eError = FindVolume( pzVolume, &pVolume, NULL );

  if( eError == eNoError )
  {
    if( pVolume->index == 0 && gDisklock )
    {
      eError = eAccessDenied;
    }
    else
    {
      /* Delete all entries in root directory of volume */
      eError = DeleteDir( &pVolume->root.u.d );
    }
  }

  return eError;
}


/**
 * \brief Create a directory.
 *
 * Returns one of the error enumeration values.
 */
int PMS_FS_MakeDir( unsigned char * pzPath )
{
  int eError = eNoError;

  unsigned char aParent[ PMS_FSENTRY_PATHLEN ];
  unsigned char aLeafname[ PMS_FSENTRY_NAMELEN ];
  PMS_Volume   * pVolume;
  PMS_FS_Entry * pParent;

  SplitName( pzPath, aParent, aLeafname );

  eError = FindEntry( aParent, &pVolume, &pParent );

  if( eError == eNoError )
  {
    if( pVolume->index == 0 && gDisklock )
    {
      eError = eAccessDenied;
    }
    else if( pParent->type == ePMS_FS_File )
    {
      /* Parent is a file */
      eError = eIllegalName;
    }
    else if( FindDirEntry( &pParent->u.d, aLeafname ) != NULL )
    {
      /* Entry already exists */
      eError = eFileAlreadyExists;
    }
    else
    {
      if( CreateDir( pParent, aLeafname ) == NULL )
      {
        eError = eDiskFull;
      }
      else
      {
        /* Success */
      }
    }
  }

  return eError;
}


/**
 * \brief Open a file.
 *
 * If successful sets *pHandle to a value which be passd to subsequent
 * calls of PMS_FS_Read, PMS_FS_Write, PMS_FS_Close, PMS_FS_Seek.
 * Returns one of the error enumeration values.
 */
int PMS_FS_Open( unsigned char * pzPath, int flags, void ** pHandle )
{
  int eError = eNoError;

  unsigned char aParent[ PMS_FSENTRY_PATHLEN ];
  unsigned char aLeafname[ PMS_FSENTRY_NAMELEN ];
  PMS_Volume   * pVolume;
  PMS_FS_Entry * pParent;

  SplitName( pzPath, aParent, aLeafname );

  eError = FindEntry( aParent, &pVolume, &pParent );

  if( eError == eNoError )
  {
    if( pVolume->index == 0 && gDisklock && (flags & PMS_FS_WRITE) == PMS_FS_WRITE )
    {
      eError = eAccessDenied;
    }
    else if( pParent->type == ePMS_FS_File )
    {
      /* Parent is a file */
      eError = eIllegalName;
    }
    else
    {
      PMS_FS_Entry * pEntry = FindDirEntry( &pParent->u.d, aLeafname );

      if( pEntry == NULL )
      {
        if( (flags & PMS_FS_CREAT) == PMS_FS_CREAT )
        {
          /* Create missing file */
          pEntry = CreateFile( pParent, aLeafname );

          if( pEntry == NULL )
          {
            eError = eDiskFull;
          }
        }
        else
        {
          /* Non-existent */
          eError = eFileNotFound;
        }
      }
      else if( pEntry->type == ePMS_FS_Dir )
      {
        /* Trying to open a directory */
        eError = eFileOpOnDirectory;
      }
      else if( pEntry->u.f.openFlags != 0 )
      {
        /* Already open */
        eError = eGeneralFileSystemError;
      }
      else
      {
        if( (flags & PMS_FS_TRUNC) == PMS_FS_TRUNC )
        {
          /* Delete existing content */
          FreeFileContent( pEntry );
        }
        else
        {
          /* Append to existing content */
          pEntry->u.f.position = pEntry->u.f.cbSize;
        }
      }

      if( eError == eNoError )
      {
        pEntry->u.f.openFlags = flags;
        *pHandle = pEntry;
      }
    }
  }

  return eError;
}


/**
 * \brief Read data from a file.
 *
 * If successful sets *pcbRead to the number of bytes read.
 * Returns one of the error enumeration values.
 */
int PMS_FS_Read( void * handle, unsigned char * buffer, int bytes, int * pcbRead )
{
  int eError = eNoError;

  PMS_FS_Entry * pEntry = (PMS_FS_Entry *) handle;

  if( ( pEntry->u.f.openFlags & PMS_FS_READ ) == 0 )
  {
    /* Not open for reading */
    eError = eGeneralFileSystemError;
  }
  else
  {
    if( bytes > pEntry->u.f.cbSize - pEntry->u.f.position )
    {
      bytes = pEntry->u.f.cbSize - pEntry->u.f.position;
    }

    if( bytes > 0 )
    {
      memcpy( buffer, pEntry->u.f.contents + pEntry->u.f.position, bytes );

      pEntry->u.f.position += bytes;
      *pcbRead = bytes;
    }
    else
    {
      *pcbRead = 0;
    }
  }

  return eError;
}


/**
 * \brief Writes data to a file.
 *
 * If successful sets *pcbWritten to the number of bytes written.
 * Returns one of the error enumeration values.
 */
int PMS_FS_Write( void * handle, unsigned char * buffer, int bytes, int * pcbWritten )
{
  int eError = eNoError;

  PMS_FS_Entry * pEntry = (PMS_FS_Entry *) handle;

  if( ( pEntry->u.f.openFlags & PMS_FS_WRITE ) == 0 )
  {
    /* Not open for writing */
    eError = eGeneralFileSystemError;
  }
  else
  {
    if( ( pEntry->u.f.openFlags & PMS_FS_APPEND ) == PMS_FS_APPEND )
    {
      pEntry->u.f.position = pEntry->u.f.cbSize;
    }

    if( pEntry->u.f.position + bytes > pEntry->u.f.cbSize )
    {
      /* Extending file */
      PMS_Volume    * pVolume = VolumeForEntry( pEntry );
      unsigned char * pNewContent = FSMalloc( pVolume, pEntry->u.f.position + bytes );

      if( pNewContent != NULL )
      {
        if( pEntry->u.f.contents )
        {
          if( pEntry->u.f.position > 0 )
          {
            memcpy( pNewContent, pEntry->u.f.contents, pEntry->u.f.position );
          }

          FSFree( pVolume, pEntry->u.f.contents, pEntry->u.f.cbSize );
        }

        memcpy( pNewContent + pEntry->u.f.position, buffer, bytes );

        pEntry->u.f.contents = pNewContent;
        pEntry->u.f.cbSize = pEntry->u.f.position + bytes;
        pEntry->u.f.position = pEntry->u.f.cbSize;
        *pcbWritten = bytes;
      }
      else
      {
        eError = eDiskFull;
      }
    }
    else
    {
      /* Overwriting existing file contents */
      memcpy( pEntry->u.f.contents + pEntry->u.f.position, buffer, bytes );

      pEntry->u.f.position += bytes;
      *pcbWritten = bytes;
    }
  }

  return eError;
}


/**
 * \brief Closes a file.
 *
 * Returns one of the error enumeration values.
 */
int PMS_FS_Close( void * handle )
{
  PMS_FS_Entry * pEntry = (PMS_FS_Entry *) handle;

  pEntry->u.f.openFlags = 0;

  return eNoError;
}


/**
 * \brief Seeks to a position in a file.
 *
 * Returns one of the error enumeration values.
 */
int PMS_FS_Seek(void * handle, int offset, int whence)
{
  int eError = eNoError;

  PMS_FS_Entry * pEntry = (PMS_FS_Entry *) handle;

  if( ( pEntry->u.f.openFlags & ( PMS_FS_READ | PMS_FS_WRITE ) ) == 0 )
  {
    /* Not open for reading or writing */
    eError = eGeneralFileSystemError;
  }
  else
  {
    if( whence == PMS_FS_SEEK_SET )
    {
      pEntry->u.f.position = offset;
    }
    else if( whence == PMS_FS_SEEK_CUR )
    {
      pEntry->u.f.position += offset;
    }
    else if( whence == PMS_FS_SEEK_END )
    {
      pEntry->u.f.position = pEntry->u.f.cbSize + offset;
    }
    else
    {
      eError = eGeneralFileSystemError;
    }

    if( pEntry->u.f.position > pEntry->u.f.cbSize )
    {
      pEntry->u.f.position = pEntry->u.f.cbSize;
    }
    else if( pEntry->u.f.position < 0 )
    {
      pEntry->u.f.position = 0;
    }
  }

  return eError;
}


/**
 * \brief Deletes a file or empty directory.
 *
 * Returns one of the error enumeration values.
 */
int PMS_FS_Delete( unsigned char * pzPath )
{
  int eError = eNoError;

  PMS_Volume   * pVolume;
  PMS_FS_Entry * pEntry;

  eError = FindEntry( pzPath, &pVolume, &pEntry );

  if( eError == eNoError )
  {
    if( pVolume->index == 0 && gDisklock )
    {
      eError = eAccessDenied;
    }
    else if( pEntry->type == ePMS_FS_File )
    {
      FreeFileContent( pEntry );
      RemoveEntryFromParentDir( pEntry );
      FSFree( VolumeForEntry( pEntry ), pEntry, sizeof( PMS_FS_Entry ) );
    }
    else
    {
      if( pEntry->pParent == NULL )
      {
        /* Trying to delete volume */
        eError = eCannotDeleteRoot;
      }
      else if( pEntry->u.d.cEntries == 0 )
      {
        RemoveEntryFromParentDir( pEntry );
        FSFree( VolumeForEntry( pEntry ), pEntry, sizeof( PMS_FS_Entry ) );
      }
      else
      {
        /* Directory not empty */
        eError = eDirectoryNotEmpty;
      }
    }
  }

  return eError;  
}


/**
 * \brief Reports details of an entry in a directory.
 *
 * If successful fills in details for entry in *pStat.
 * Returns one of the error enumeration values.
 */
int PMS_FS_DirEntryStat(unsigned char * pzDir, int nEntry, PMS_TyStat * pStat)
{
  int eError = eNoError;

  PMS_FS_Entry * pDir;

  eError = FindEntry( pzDir, NULL, &pDir );

  if( eError == eNoError )
  {
    if( pDir->type != ePMS_FS_Dir )
    {
      /* pzDir isn't a directory */
      eError = eDirectoryOpOnFile;
    }
    else if( nEntry > pDir->u.d.cEntries )
    {
      /* entry out of range */
      pStat->type = ePMS_FS_None;
    }
    else
    {
      PMS_FS_Entry * pEntry = pDir->u.d.pEntries;
      int            i = 1;

      while( i++ < nEntry )
      {
        pEntry = pEntry->pNext;
      }

      GetEntryDetails( pEntry, pStat );
    }
  }

  return eError;
}


/**
 * \brief Reports details of an entry.
 *
 * If successful fills in details for entry in *pStat.
 * Returns one of the error enumeration values.
 */
int PMS_FS_Stat( unsigned char * pzPath, PMS_TyStat * pStat )
{
  int eError = eNoError;

  PMS_FS_Entry * pEntry;

  eError = FindEntry( pzPath, NULL, &pEntry );

  if( eError == eNoError )
  {
    GetEntryDetails( pEntry, pStat );
  }

  return eError;
}


/**
 * \brief Reports details of a file system volume.
 *
 * If *pnVolumes is not NULL fills in *pnVolumes with number of volumes.
 * If iVolume is in range and *pFileSystemInfo is not NULL fills in *pFileSystemInfo
 * with details of specified volume.
 * Returns one of the error enumeration values.
 */
int PMS_FS_FileSystemInfo(int iVolume, int * pnVolumes, PMS_TyFileSystem * pFileSystemInfo)
{
  int eError = eNoError;

  if( pnVolumes != NULL )
  {
    *pnVolumes = PMS_N_VOLUMES;
  }

  if( pFileSystemInfo != NULL )
  {
    if( iVolume >= 0 && iVolume < PMS_N_VOLUMES )
    {
      pFileSystemInfo->cbCapacity = gVolumes[ iVolume ].cbCapacity;
      pFileSystemInfo->cbFree = gVolumes[ iVolume ].cbFree;
      pFileSystemInfo->pLocation = gVolumes[ iVolume ].pLocation;
      pFileSystemInfo->pLabel = gVolumes[ iVolume ].pLabel;
    }
    else
    {
      eError = eVolumeNameOutOfRange;
    }
  }

  return eError;
}


/**
 * \brief Get file system disk lock status.
 */
int PMS_FS_GetDisklock(void)
{
  return gDisklock;
}

/**
 * \brief Set file system disk lock status.
 */
void PMS_FS_SetDisklock(int fLocked)
{
  gDisklock = fLocked;
}


