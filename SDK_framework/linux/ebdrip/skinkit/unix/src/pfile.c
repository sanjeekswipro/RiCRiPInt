/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!unix:src:pfile.c(EBDSDK_P.1) $
 * File related utility functions for Unix.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief File related utility functions for Unix.
 */

#ifdef linux
/* So as to be able to use open64, stat64, etc */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include "file.h"
#include "swoften.h"
#include "mem.h"
#include "skinkit.h"

#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>

#define DIRMODE  0777
#define FILEMODE 0666


#if defined linux && !defined(LIBuClibc)
/* Explicitly use the 64-bit types and APIs */
#define OFF_T off64_t
#define STATSTRUCT struct stat64
#define FSTATFN fstat64
#define LSEEKFN lseek64
#define OPENFN open64
#define STATFN stat64
#else
/* The normal types and APIs are 64-bit capable so just use them,
 * or there are no 64-bit APIs.
 */
#define OFF_T off_t
#define STATSTRUCT struct stat
#define FSTATFN fstat
#define LSEEKFN lseek
#define OPENFN open
#define STATFN stat
#endif


static int32 map_errno(int err);
static int make_subdirectories(char * filename);


struct FileDesc
{
  int fd;
};


static FileDesc* createFileDescriptorFromHandle( int fd )
{
  FileDesc* pDescriptor = (FileDesc*) MemAlloc (sizeof (FileDesc), FALSE, FALSE);

  if (pDescriptor)
    pDescriptor->fd = fd;

  return pDescriptor;
}


static void Hq32x2FromOff_t( Hq32x2 *p32x2, OFF_T offt )
{
  p32x2->low = (uint32) offt;
  /* Use two 16-bit shifts, in case 32 bit shift does nothing on this
     architecture. */
  p32x2->high = (int32)((offt >> 16) >> 16);
}


static HqBool Hq32x2ToOff_t( const Hq32x2 *p32x2, OFF_T * pofft )
{
  OFF_T result;

  /* Use two 16-bit shifts, in case 32 bit shift does nothing on this
     architecture. */
  result = p32x2->low | (((OFF_T)p32x2->high << 16) << 16);

  /* We can't use sizeof in #if to divine the size of OFF_T, so check that
     we did store the upper bits correctly, and fail if we couldn't. */
  if ( ((result >> 16) >> 16) != (OFF_T)p32x2->high )
    return FALSE;

  *pofft = result;
  return TRUE;
}


int32 PKAppDir(uint8 * pAppDir)
{
  return PKCurrDir( pAppDir );
}


int32 PKCurrDir(uint8 * pCurrDir)
{
  int length;

  /* Check arguments */
  if (!pCurrDir) return FALSE;

  /* How big is pCurrDir?  Let's hope it's at least LONGESTFILENAME!
   * Pass LONGESTFILENAME-1, since we might need to add a trailing '/'
   * character if there isn't one already. */
  if (!getcwd((char*)pCurrDir, LONGESTFILENAME-1))
  {
    PKRecordSystemError(errno, __LINE__, __FILE__, TRUE);
    return FALSE;
  }

  /* Add a trailing '/' if there isn't one there already */
  length = strlen((char*)pCurrDir);
  if (pCurrDir[length-1] != '/') {
    pCurrDir[length] = '/';
    pCurrDir[length+1] = '\0';
  }

  return TRUE;
}


int32 PKParseRoot(uint8 * pOutput, uint8 ** ppInput)
{
  /* Check arguments */
  if (!ppInput || !*ppInput) return FALSE;

  if (pOutput) *pOutput = '\0'; /* Output == "" */

  if (**ppInput != '/') return FALSE; /* Relative */

  while (**ppInput == '/') (*ppInput)++; /* Discard initial '/'s */
  return TRUE;
}


int32 PKBuildRoot(uint8 * pOutput, uint8 * pInput)
{
  /* Check arguments */
  if (!pOutput || !pInput || *pInput) return FALSE;

  pOutput[0] = '/'; pOutput[1] = '\0';
  return TRUE;
}


FileDesc* PKOpenFile(uint8 *filename, int32 openflags, int32 * pError)
{
  int flags, fd;
  int32 errcode = 0;

  /*
   * POSIXify the flags
   */

  /* Access specifier */
  switch (openflags & (SW_RDONLY | SW_WRONLY | SW_RDWR)) {
  case SW_RDONLY: flags = O_RDONLY; break;
  case SW_WRONLY: flags = O_WRONLY; break;
  case SW_RDWR:   flags = O_RDWR;   break;
  default: /* Either none or more than one of them */
    *pError = PKErrorParameter;
    return NULL;
  }

  if (openflags & SW_CREAT)  flags |= O_CREAT;
  if (openflags & SW_TRUNC)  flags |= O_TRUNC;
  if (openflags & SW_APPEND) flags |= O_APPEND;
  if (openflags & SW_EXCL)   flags |= O_EXCL;

  /* POSIX open */
  fd = OPENFN((char*)filename, flags, FILEMODE);

  if (fd >= 0)
  {
    FileDesc* pDescriptor = createFileDescriptorFromHandle (fd);
    if (! pDescriptor)
    {
      *pError = PKErrorNoMemory;
      close (fd);
    }
    return pDescriptor;
  }
  else
  {
    errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
  }

  /* If we are trying to create a file in a missing directory, make sure
   * the directory exists */
  if ( (flags & O_CREAT) && make_subdirectories((char*)filename) ) {
    fd = OPENFN((char*)filename, flags, FILEMODE); /* Try again */
    if (fd >= 0)
    {
      FileDesc* pDescriptor = createFileDescriptorFromHandle (fd);
      if (! pDescriptor)
      {
        *pError = PKErrorNoMemory;
        close (fd);
      }
      return pDescriptor;
    }
    else
    {
      errcode = errno ;
      PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    }
  }

  /* Map error code. */
  *pError = map_errno(errcode);
  return NULL;
}


int32 PKReadFile(FileDesc* pDescriptor, uint8 * buff, int32 len, int32 * pError)
{
  int32 bytes;

  HQASSERT(pDescriptor != NULL, "No file descriptor");
  for (;;)
  {
    bytes = read( pDescriptor->fd, buff, len );
    if ( bytes < 0 )
    {
      if ( errno == EINTR )
      {
      }
      else
      {
        int32 errcode = errno;
        PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
        *pError = map_errno(errcode);
        return -1;
      }
    }
    else
    {
      return bytes;
    }
  }
}


int32 PKWriteFile(FileDesc* pDescriptor, uint8 * buff, int32 len, int32 * pError)
{
  int32 bytes_left, bytes_written;

  HQASSERT(pDescriptor != NULL, "No file descriptor");
  bytes_left = len;
  while ( bytes_left )
  {
    bytes_written = write( pDescriptor->fd, (char *)buff,
                           (unsigned int) bytes_left );
    if ( bytes_written < 0)
    {
      if ( errno == EINTR )
      {
      }
      else
      {
        int32 errcode = errno ;
        PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
        *pError = map_errno(errcode);
        return -1;
      }
    }
    else
    {
      bytes_left -= bytes_written;
      buff += bytes_written;
    }
  }
  return len;
}


int32 PKCloseFile(FileDesc* pDescriptor, int32 * pError)
{
  int rv;
  HQASSERT(pDescriptor != NULL, "No file descriptor");

  rv = close(pDescriptor->fd);
  if (rv < 0)
  {
    int32 errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    *pError = map_errno(errcode);
  }
  else
    MemFree (pDescriptor);

  return rv;
}

int32 PKSeekFile(FileDesc* pDescriptor, Hq32x2 * destination, int32 flags,
                 int32 * pError)
{
  OFF_T code;
  OFF_T destn;

  HQASSERT(pDescriptor != NULL, "No file descriptor");

  switch (flags) {
    case SW_SET:  flags = SEEK_SET; break;
    case SW_INCR: flags = SEEK_CUR; break;
    case SW_XTND: flags = SEEK_END; break;
  }

  if (! Hq32x2ToOff_t(destination, &destn))
  {
    *pError = PKErrorNumericRange;
    return FALSE;
  }

  code = LSEEKFN(pDescriptor->fd, destn, flags);
  if (code < 0)
  {
    int32 errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    *pError = map_errno(errcode);
    return FALSE;
  }

  Hq32x2FromOff_t(destination, code);
  return TRUE;
}


int32 PKBytesFile(FileDesc* pDescriptor, Hq32x2 * bytes, int32 reason,
                  int32 * pError)
{
  /*
   * N.B. The same comment about max files sizes as above applies here.
   */
  int rv;
  OFF_T length;
  STATSTRUCT status;

  HQASSERT(pDescriptor != NULL, "No file descriptor");

  /* find file extent using fstat, retrying on EINTR */
  while ( (rv = FSTATFN(pDescriptor->fd, &status)) != 0 && errno == EINTR )
    /*NOSTATEMENT*/;

  if (rv == 0)
  {
    length = status.st_size;
    if (reason == SW_BYTES_AVAIL_REL)
    {
      OFF_T pos = LSEEKFN(pDescriptor->fd, 0, SEEK_CUR);
      if (pos < 0)
        length = 0; /* ? */
      else
        length -= pos;
    }
  }
  else
  {
    if (reason == SW_BYTES_AVAIL_REL)
      length = 0; /* ?!? "assume there are some bytes somewhere" */
    else {
      int32 errcode = errno ;
      PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
      *pError = map_errno(errcode);
      return FALSE;
    }
  }

  Hq32x2FromOff_t(bytes, length);
  return TRUE;
}


int32 PKStatusFile(uint8 * filename, STAT * statbuff, int32 * pError)
{
  STATSTRUCT status;
  Hq32x2     size;

  if (STATFN((char*)filename, &status) == -1)
  {
    int32 errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    *pError = map_errno(errcode);
    return -1;
  }

  Hq32x2FromOff_t( &size, status.st_size );
  HQASSERT(size.high >= 0 , "File negative size");

  statbuff->bytes.low  = size.low;
  statbuff->bytes.high = (uint32) size.high;
  statbuff->referenced = (uint32) status.st_atime;
  statbuff->created    = (uint32) status.st_mtime;
  return 0;
}


int32 PKDeleteFile(uint8 * filename, int32 * pError)
{
  int rv = unlink((char*)filename);
  if (rv < 0)
  {
    int32 errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    *pError = map_errno(errcode);
  }
  return rv;
}

/** \brief FindFileState */
typedef struct
{
  glob_t   g ;
  int32    iglob ;
} FindFileState;

void * PKFindFirstFile( uint8 * pszPattern, uint8 * pszEntryName, int32 * pError )
{
  FindFileState * pState;

  if (strlen((char *)pszPattern) >= LONGESTFILENAME)
  {
    *pError = PKErrorStringLength;
    return NULL;
  }

  pState = (FindFileState *)MemAlloc(sizeof(FindFileState), TRUE, FALSE);

  if (pState == NULL)
  {
    *pError = PKErrorNoMemory;
    return NULL;
  }

  pState->iglob = 0;
  if (glob((const char *)pszPattern, 0, NULL, &pState->g))
  {
    int32 errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    if (errcode == GLOB_NOSPACE)
      *pError = PKErrorNoMemory;
    else
      *pError = PKErrorOperationFailed;  /* generic failure */
    MemFree(pState);
    return NULL;
  }
  if (strlen(pState->g.gl_pathv[pState->iglob]) >= LONGESTFILENAME)
  {
    PKCloseFindFile((void *)pState, pError) ;
    *pError = PKErrorStringLength;
    return NULL;
  }
  strcpy((char*) pszEntryName, pState->g.gl_pathv[pState->iglob++]);
  return (void *)pState;
}

int32 PKFindNextFile(void * handle, uint8 * pszEntryName, int32 * pError)
{
  FindFileState * pState = (FindFileState *)handle;
  if (pState->iglob < pState->g.gl_pathc)
  {
    if (strlen(pState->g.gl_pathv[pState->iglob]) < LONGESTFILENAME)
    {
      strcpy((char*) pszEntryName, pState->g.gl_pathv[pState->iglob++]);
      return TRUE;
    }
    else
    {
      *pError = PKErrorStringLength;
      return FALSE;
    }
  }
  *pError = PKErrorNone;
  return FALSE;
}

int32 PKCloseFindFile(void * handle, int32 * pError)
{
  FindFileState * pState = (FindFileState *)handle;
  globfree(&pState->g);
  MemFree(pState);
  return TRUE;
}

/**
 * @brief Structure for holding the DIR pointer plus the name of the
 * directory. */
struct DirStruct {
  DIR  * pDIR;
  char * name;
};


void * PKDirOpen(uint8 * pszDirName, int32 * pError)
{
  struct DirStruct * dir = (struct DirStruct *) MemAlloc(sizeof(struct DirStruct), FALSE, FALSE);
  if (!dir) {
    *pError = PKErrorNoMemory;
    return NULL;
  }

  dir->name = (char *) MemAlloc(strlen((char*)pszDirName) + 1, FALSE, FALSE);
  if (!dir->name) {
    MemFree(dir);
    *pError = PKErrorNoMemory;
    return NULL;
  }
  strcpy(dir->name, (char*)pszDirName);

  dir->pDIR = opendir(dir->name);
  if (!dir->pDIR) {
    int32 errcode = errno ;
    PKRecordSystemError(errcode, __LINE__, __FILE__, TRUE);
    *pError = map_errno(errcode);
    MemFree(dir->name); MemFree(dir);
    return NULL;
  }

  return dir;
}

int32 PKDirNext(void * handle, uint8 * pszEntryName, int32 * fIsFolder,
                int32 * pError)
{
  struct DirStruct * dir = (struct DirStruct *)handle;
  struct dirent *dirent;

  errno = 0;
  while ( (dirent = readdir(dir->pDIR)) &&
	  ( (strcmp(dirent->d_name, ".") == 0) ||
	    (strcmp(dirent->d_name, "..") == 0) ) );
  if (!dirent) {
    *pError = map_errno(errno);
    return FALSE;
  }

  if (strlen(dirent->d_name) >= LONGESTFILENAME) {
    *pError = PKErrorStringLength;
    return FALSE;
  }

  if (fIsFolder) {
    char *fullname = (char *) MemAlloc(strlen(dir->name) + strlen(dirent->d_name) + 2, FALSE, FALSE);
    STATSTRUCT st;
    int rv;
    if (!fullname) {
      *pError = PKErrorNoMemory;
      return FALSE;
    }
    sprintf(fullname, "%s/%s", dir->name, dirent->d_name);
    rv = STATFN(fullname, &st);
    MemFree(fullname);
    if (rv) {
      *pError = map_errno(errno);
      return FALSE;
    }
    *fIsFolder = S_ISDIR(st.st_mode);
  }

  strcpy((char*)pszEntryName, dirent->d_name);
  return TRUE;
}

int32 PKDirClose(void * handle, int32 * pError)
{
  struct DirStruct * dir = (struct DirStruct *)handle;
  (void)pError; /* UNUSED */
  closedir(dir->pDIR);
  MemFree(dir->name); MemFree(dir);
  return TRUE;
}



/*
 * Implementation
 */

static int32 map_errno(int err)
{
  switch (err) {
  case 0:
    return PKErrorNone;
  case EACCES: case EPERM: case EROFS:
    return PKErrorAccessDenied;
  case EAGAIN: case EBUSY:
    return PKErrorInUse;
  case EDQUOT: case ENFILE: case ENOSPC:
    return PKErrorDiskFull;
  case EEXIST:
    return PKErrorAlreadyExists;
  case EFAULT: case EINVAL: case EBADF:
    return PKErrorParameter;
  case EIO:
    return PKErrorHardware;
  case EISDIR: case ENODEV: case ENXIO: case EDEADLK:
    return PKErrorOperationDenied;
  case ELOOP: case EMFILE: case EOPNOTSUPP: case ENOLCK:
    return PKErrorSoftwareLimit;
  case ENAMETOOLONG:
    return PKErrorStringLength;
  case ENOENT: case ETIMEDOUT:
    return PKErrorNonExistent;
  case ENOMEM:
#ifndef MACOSX
  case ENOSR:
#endif
    return PKErrorNoMemory;
  case ENOTDIR:
    return PKErrorStringValue;
  default:
    return PKErrorUnknown;
  }
}

static int make_subdirectories(char * filename)
{
  int dirs_made = FALSE;
  char *p, *lastchar;

  lastchar = filename + strlen(filename) - 1;

  /* Go back up the path, trying to make each directory until success */
  for (p = lastchar; p >= filename; p--) {
    if (*p == '/') {
      int rv;
      *p = '\0'; rv = mkdir(filename, DIRMODE); *p = '/';
      if (rv == 0) { dirs_made = TRUE; break; }
      if (errno == EEXIST) break;
      if (errno != ENOENT)
      {
        PKRecordSystemError(errno, __LINE__, __FILE__, TRUE);
        return FALSE; /* Error */
      }
    }
  }

  /* Now go back down it, making the ones we failed on */
  while ((p = strchr(p+1, '/')) != NULL) {
    int rv;
    *p = '\0'; rv = mkdir(filename, DIRMODE); *p = '/';
    if (rv)
    {
      PKRecordSystemError(errno, __LINE__, __FILE__, TRUE);
      return FALSE; /* Error! */
    }
    else dirs_made = TRUE;
  }

  return dirs_made;
}


int32 PKMakePSFilename(
  uint8*  pszFilename,
  uint8*  pszPSFilename)
{
  uint8   absFilename[LONGESTFILENAME];

  if ( *pszFilename != '/' ) {
    /* Got relative filename, stick current working directory on front */
    if ( !PKCurrDir(absFilename) ) {
      return(FALSE);
    }
    strcat((char*)absFilename, (char*)pszFilename);
    pszFilename = absFilename;
  }

  /* Generate PS filename */
  (void)sprintf((char*)pszPSFilename, "%%/%%%s", pszFilename);

  return(TRUE);
}

/* Record system errors through the monitor callback. */

void PKRecordSystemError(int32 errcode, int32 errline, const char * pErrfile, int32 fSysErr)
{
  int32 nLevel = KGetSystemErrorLevel();

  HQASSERT(nLevel >= 0 && nLevel < 3, "Invalid system error level");
  if (nLevel == 0 || errcode == 0)
    return ;
  /* Ignore file/path non exist error on  level 1 */
  if (nLevel == 1 && errcode == ENOENT)
    return ;

  if (fSysErr)
  {
    char * pStrErr = strerror(errcode);
    if (pStrErr)
    {
      SkinMonitorf( "System Error(%d) at line %d of file %s: %s.\n",
                    errcode, errline, pErrfile, pStrErr );
    }
    else
    {
      SkinMonitorf( "System Error(%d) at line %d of file %s.\n",
                    errcode, errline, pErrfile );
    }
  }
  else
  {
    SkinMonitorf( "Error(%d) at line %d of file %s.\n",
                  errcode, errline, pErrfile );
  }
}

extern int32 PKOSFontDir( uint8* pszFontDir )
{
  /* Not currently required on this platform */
  pszFontDir[0] = '\0';
  return FALSE;
}
