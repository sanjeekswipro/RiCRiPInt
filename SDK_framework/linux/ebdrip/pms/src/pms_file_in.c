/* Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_file_in.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief File Input Simulator.
 *
 * These routines emulate the data streaming from input port (which is in this case - file)
 * Assumption: filenames are specified in command line parameter
 */

#include "pms_file_in.h"
#include "pms.h"
#include <stdio.h>
#include <string.h>
#include "pms_malloc.h"

/* Extern variable declarations */
/*! \brief Number of jobs to be processed. Value is set by commandline parser */
extern int nJobs;
/*! \brief Array of full paths of the jobs. Set by commandline parser */
extern char **aszJobNames; 

/* Static variable declarations */
/*! \brief command-line parameter index pointing to the job being processed. */
static int iFileIndex;
/*! \brief Number of jobs to be processed. */
static int nFiles;
/*! \brief Holds current job's name (full path). */
static char ** szFilenames;

/*! \brief Holds the File Descriptor of currently open file. */
static FILE* l_fileCurrent = NULL;

/* Forward Declarations */
static int GetNextFilename(char * pszFilename);

extern int g_cbReceiveBuffer; /* for file and socket input buffer */
extern int g_bStoreJobBeforeRip; /* store whole job before passing to rip */
unsigned char *l_pStoreBuffer = NULL; /* Store buffer. Only used if storing whole job before ripping */
int l_nStoreBufferUsed; /* The amount of valid data in the store buffer */
int l_nStoreBufferPos; /* Position in the store buffer */
char szJobFilename[256]; /* buffer to hold name of the job(full path) */

/**
 * \brief Initialize the File Data Stream.
 *
 * Parses the command-line parameter\n
 */
int File_InitDataStream(void)
{
  /* PMS_SHOW("File_InitDataStream()\n"); */

  nFiles = nJobs;
  szFilenames = aszJobNames;
  iFileIndex = 0;

  return TRUE;
}

/**
 * \brief Open the next file.
 *
 * Opens the file contaning next job data.\n
 */
int File_OpenDataStream(void)
{
  /* PMS_SHOW("File_OpenDataStream()\n"); */

  if(!GetNextFilename(szJobFilename))
    return -1; /* -1 means don't try opening this module again */

  if ( (l_fileCurrent = fopen((char *)szJobFilename, "rb") ) == NULL )
  {
    PMS_SHOW_ERROR(" ***ASSERT*** File_OpenDataStream: Failed to open file %s \n", szJobFilename);
    return 0; /* 0 means you may try this module again to try the next job */
  }

  if(g_tSystemInfo.nStoreJobBeforeRip)
  {
    int nRead;
    int nChunk = 16 * 1024;
    PMS_SHOW("Storing job before passing to rip...\n");
    l_pStoreBuffer = OSMalloc(g_tSystemInfo.cbReceiveBuffer,PMS_MemoryPoolMisc);
    if(!l_pStoreBuffer) {
      PMS_SHOW_ERROR(" ***ASSERT*** File_OpenDataStream: Failed to allocate buffer for storing job\n");
      fclose(l_fileCurrent);
      l_fileCurrent = NULL;
      return 0; /* 0 means you may try this module again to try the next job */
    }
    fseek(l_fileCurrent,0,SEEK_END);
    l_nStoreBufferUsed = ftell(l_fileCurrent);
    fseek(l_fileCurrent,0,SEEK_SET);

    if(l_nStoreBufferUsed > g_tSystemInfo.cbReceiveBuffer)
    {
      PMS_SHOW_ERROR(" ***ASSERT*** File_OpenDataStream: Failed to store job. Job size is larger than the memory buffer.\n");
      fclose(l_fileCurrent);
      l_fileCurrent = NULL;
      OSFree(l_pStoreBuffer, PMS_MemoryPoolMisc);
      l_pStoreBuffer = NULL;
      return 0; /* 0 means you may try this module again to try the next job */
    }

    l_nStoreBufferUsed = 0;
    l_nStoreBufferPos = 0;
    do
    {
      if((l_nStoreBufferUsed + nChunk) < g_tSystemInfo.cbReceiveBuffer)
      {
        nRead = (int)fread(l_pStoreBuffer + l_nStoreBufferUsed, 1, nChunk, l_fileCurrent);
      }
      else if(l_nStoreBufferUsed < g_tSystemInfo.cbReceiveBuffer)
      {
        nRead = (int)fread(l_pStoreBuffer + l_nStoreBufferUsed, 1, g_tSystemInfo.cbReceiveBuffer - l_nStoreBufferUsed, l_fileCurrent);
      }
      else
      {
        nRead = 0;
      }
      l_nStoreBufferUsed += nRead;
    } while (nRead>0);

    fclose(l_fileCurrent);
    l_fileCurrent = NULL;
    PMS_SHOW("Stored %d bytes.\n", l_nStoreBufferUsed);
  }

  return 1; /* 1 means we have a job ready to rip */
}

/**
 * \brief Close the file.
 *
 */
int File_CloseDataStream(void)
{
  /* File is already closed when the last byte was read. */

  if(l_pStoreBuffer)
  {
    OSFree(l_pStoreBuffer, PMS_MemoryPoolMisc);
    l_pStoreBuffer = NULL;
  }

  return TRUE;
}

/**
 * \brief Read from File Data Stream, but move the file position back to the start of the read.
 *
 * This routine reads the requested number of bytes from the file and puts them
 * in the buffer and return the file position back to the start of the read.\n
 * It is an implementation of PMS_PeekDataStream callback. \n
 */
int File_PeekDataStream(unsigned char * buffer, int nBytesToRead)
{
  int nbytes_read;

  /* PMS_SHOW("File_PeekDataStream(0x%p, %d)\n", buffer, nBytesToRead); */
  if(g_tSystemInfo.nStoreJobBeforeRip)
  {
    if((l_nStoreBufferPos + nBytesToRead) < l_nStoreBufferUsed)
    {
      nbytes_read = nBytesToRead;
    }
    else
    {
      nbytes_read = l_nStoreBufferUsed - l_nStoreBufferPos;
    }
    memcpy(buffer, l_pStoreBuffer + l_nStoreBufferPos, nbytes_read);
  }
  else
  {
    if(l_fileCurrent==NULL)
      return (0);

    nbytes_read = (int)fread(buffer, 1, nBytesToRead, l_fileCurrent);
    if(nbytes_read < 0)
    {
      PMS_SHOW_ERROR("Failed to read from file");
      fclose(l_fileCurrent);
      l_fileCurrent = NULL;
    }
    else if (nbytes_read == 0)
    {
      fclose(l_fileCurrent);
      l_fileCurrent = NULL;
    }
    else
    {
      fseek(l_fileCurrent, -nbytes_read, SEEK_CUR);
    }
  }

  return (nbytes_read);
}

/**
 * \brief Consume data from the file data stream.
 *
 * This moves the file position.\n
 *
 */
int File_ConsumeDataStream(int nBytesToConsume)
{
  /* PMS_SHOW("File_ConsumeDataStream(%d)\n", buffer, nBytesToConsume); */

  if(g_tSystemInfo.nStoreJobBeforeRip)
  {
    l_nStoreBufferPos += nBytesToConsume;
  }
  else
  {
    if(l_fileCurrent==NULL)
      return (0);

    fseek(l_fileCurrent, nBytesToConsume, SEEK_CUR);
  }

  return (nBytesToConsume);
}

/**
 * \brief Read and consume data from the file data stream.
 *
 * This moves the file position.\n
 *
 */
int File_ReadDataStream(unsigned char * buffer, int nBytesToRead)
{
  int nRead;

  nRead = File_PeekDataStream(buffer, nBytesToRead);
  File_ConsumeDataStream(nRead);

  return (nRead);
}

/**
 * \brief Open the specified file.
 *
 * This function is an interface to open a file on disk.
 * It sets the handle which can later be used to read, seek etc.
 * \param[in]   pzPath    character string specifying the file to open.
 * \param[in]   flags     character string specifying the file mode to be used.
 * \param[out]  pHandle   the passed address will point to the file handle on success.
 * \return      Returns TRUE if file was opened successfully, FALSE otherwise.
 */
int File_Open( char * pzPath, char * flags, void ** pHandle )
{
  if ( (*pHandle = fopen(pzPath, flags) ) == NULL )
  {
    PMS_SHOW_ERROR(" ***ASSERT*** File_Open: Failed to open file %s \n", pzPath);
    return FALSE;
  }
  return TRUE;
}

/**
 * \brief Closes the file associated with the specified handle.
 *
 * This function is an interface to close a file on disk.
 * \param[in]   pHandle  address pointing to the file handle.
 * \return      Returns TRUE on success, FALSE otherwise.
 */
int File_Close(void * handle)
{
  fclose(handle);
  return TRUE;
}

/**
 * \brief Read the file associated with the specified handle.
 *
 * This function is an interface to read a file on disk.
 * \param[out]  buffer   the requested bytes are read into the buffer pointed to by this address
 * \param[in]   nBytesToRead    number of bytes to read
 * \param[in]   pHandle   address pointing to the file handle.
 * \return      Returns number of bytes read on success, negative value otherwise.
 */
int File_Read(unsigned char * buffer, int nBytesToRead, void * handle)
{
  int nbytes_read;

  if(handle==NULL)
      return (0);

  nbytes_read = (int)fread(buffer, 1, nBytesToRead, handle);
  if(nbytes_read < 0)
  {
    PMS_SHOW_ERROR("Failed to read from file");
    fclose(handle);
    handle = NULL;
  }
  return (nbytes_read);
}

/**
 * \brief Seek the file associated with the specified handle.
 *
 * This function is an interface to seek a file on disk.
 * \param[in]   pHandle   address pointing to the file handle.
 * \param[in]   nBytesToSeek   number of bytes to seek
 * \param[in]   nWhence    SEEK_CUR - seek from current location, 
 *                         SEEK_SET - seek from start of file, 
 *                         SEEK_END - seek relative to end of file, 
 * \return      Returns negative integer on failure.
 */
int File_Seek(void * handle, long *pPosition, int nWhence)
{
  int result = -1;
  switch(nWhence)
  {
  case SEEK_CUR:
    result = fseek(handle, *pPosition, SEEK_CUR);
    break;
  case SEEK_SET:
    result = fseek(handle, *pPosition, SEEK_SET);
    break;
  case SEEK_END:
    result = fseek(handle, *pPosition, SEEK_END);
    break;
  default:
    break;
  }
  /* return the current postion as it is used by the RIP */
  if(result == 0)
    result = ftell(handle);
  if(result >= 0)
  {
    *pPosition = result;
    result = 0;
  }
  return (result);
}

/**
 * \brief Return file size in bytes of the file associated with the specified handle.
 *
 * This function is an interface for calculating file size of a file on disk.
 * \param[in]   pHandle   address pointing to the file handle.
 * \return      Returns file size in bytes
 */
int File_Bytes(void * handle)
{
  int t, lBytesInFile;
  if(handle==NULL)
    return (0);
  t = ftell(handle);
  fseek(handle, 0, SEEK_END);
  lBytesInFile = ftell(handle);
  fseek(handle, t, SEEK_SET);
  return lBytesInFile;
}

/**
 * \brief Get next job.
 *
 * Parse the command-line parameters and get the next job.\n
 */
static int GetNextFilename(char * pszFilename)
{
  if( iFileIndex < nFiles )
  {
    /* no check for directory separator - command line parameter must be platform specific */
    strcpy((char *)pszFilename, szFilenames[iFileIndex++] );
    return TRUE;
  }
  return FALSE;
}

