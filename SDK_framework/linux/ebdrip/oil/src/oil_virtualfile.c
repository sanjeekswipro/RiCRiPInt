/* Copyright (c) 2008-2012 Global Graphics Software Ltd. All Rights Reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_virtualfile.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*! \file
 *  \ingroup OIL
 *  \brief This file contains the implementation of the OIL virtual file interface.
 *
 *  The functions implemented in this file provide a way for the OIL to store a
 *  file in RAM. This is referred to thoughout this source file as a "virtual file".
 */


#include "oil.h"
#include "oil_virtualfile.h"
#include "oil_malloc.h"
#include "mem.h"
#include "string.h"
#include "oil_interface_oil2pms.h"

extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;

/*! The memory used for the virtual file chunk is stored in the OIL job pool */
#define OIL_VIRTFILE_MEM_OIL_JOB 1

/*! The memory used for the virtual file chunk is stored in the RIP memory pool */
#define OIL_VIRTFILE_MEM_RIP     2

/* Debug the link list */
/* #define GG_DEBUG_LIST */

/**
 * \brief Virtual file chunk.
 */
struct TyVirtFileChunk
{
  unsigned char *pMemory;                /*!< The memory used for the virtual file chunk */
  unsigned int cbSize;                   /*!< Size of the virtual file chunk */
  unsigned int nMemoryPool;              /*!< Memory pool used for pMemory. OIL, RIP, or somewhere else */
  struct TyVirtFileChunk * pNextChunk;   /*!< Pointer to next virtual file chunk */
#ifdef GG_DEBUG_LIST
  unsigned int bWrittenTo; /* !< Debugging... has this chunk been written to? */
  unsigned int nID;        /* !< Debugging... ID? */
#endif
};

/**
 * \brief Virtual file structure.
 */
struct TyVirtFile { 
  char *pszFilename;                     /*!< Filename */
  int nFileID;                           /*!< ID for this virtual file */
  unsigned int uLength;                  /*!< Total length of the virtual file */
  struct TyVirtFileChunk *pFirstChunk;   /*!< Pointer to the first chunk of data */
  struct TyVirtFileChunk *pCurrentChunk; /*!< The current file pointer is within this chunk */
  unsigned int uCurrentChunkPos;         /*!< The current file pointer is this many bytes into the pCurrentChunk */
  struct TyVirtFile *pNextFile;          /*!< Pointer to the next virtual file */
};

struct TyVirtFile *l_pVirtFiles = NULL;  /*!< Head of the virtual file list list */
int l_nNextFileID = 0;                   /*!< Next file id to use */

/**
 * \brief Find a virtual file by a given ID number.
 *
 * \param[in] nFileID ID number of the virtual file to look for.
 *
 * \return    Pointer to virtual file structure if found, or NULL if the file is not found.
 */
static struct TyVirtFile *GetVirtFileFromID(int nFileID)
{
  struct TyVirtFile *pVirtFile;
  for( pVirtFile = l_pVirtFiles; pVirtFile; pVirtFile = pVirtFile->pNextFile )
  {
    if(pVirtFile->nFileID == nFileID)
    {
      return pVirtFile;
    }
  }
  return NULL;
}

#ifdef GG_DEBUG_LIST
/* DEBUG */
void DumpVirtFileList(int nFileID) 
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFileChunk *pVirtFileChunk;
  FILE *hFile = NULL;
  char szMsg[256];
  static int nFile=0;

  printf("+++ DumpVirtFileList()\n");
  nFile++;
  sprintf(szMsg, "DumpVirtFileList-%04d.bin", nFile);
  printf("Dumping data to file \"%s\"\n", szMsg);

#ifdef OIL_DUMP_DATA
  hFile=fopen(szMsg, "wb");
#endif
   
  pVirtFile = GetVirtFileFromID(nFileID);

  printf("pVirtFile First chunk %p\n"
         "  Current chunk %p\n"
         "  Current pos %u\n"
         "  Length %u\n",
    pVirtFile->pFirstChunk,
    pVirtFile->pCurrentChunk,
    pVirtFile->uCurrentChunkPos,
    pVirtFile->uLength);


  for(pVirtFileChunk = pVirtFile->pFirstChunk; 
      pVirtFileChunk;
      pVirtFileChunk = pVirtFileChunk->pNextChunk) {
    printf("pVirtFileChunk %p, %u\n"
           "  cbSize %u\n"
           "  memory data (8 bytes) %p\n"
           "  memory pool %u\n",
           pVirtFileChunk,
           pVirtFileChunk->nID,
           pVirtFileChunk->cbSize,
           (void*)pVirtFileChunk->pMemory,
           pVirtFileChunk->nMemoryPool
           );
    if(hFile) {
      sprintf(szMsg, "\r\n*** nID %d, size %d, addr=0x%p, data=0x%p, next=0x%p\r\n", 
        pVirtFileChunk->nID,
        pVirtFileChunk->cbSize,
        pVirtFileChunk,
        pVirtFileChunk->pMemory,
        pVirtFileChunk->pNextChunk
        );
      fwrite(szMsg, 1, strlen(szMsg), hFile);
      fwrite(pVirtFileChunk->pMemory, 1, pVirtFileChunk->cbSize, hFile);
    }
  }

  if(hFile) {
    fclose(hFile);
  }

  printf("---\n");
}
#endif

/**
 * \brief Find a virtual file by filename.
 *
 * \param[in] pszFilename Filename string of the virtual file to look for.
 *
 * \return    Pointer to virtual file structure if found, or NULL if the file is not found.
 */
static struct TyVirtFile *GetVirtFileFromFilename(char *pszFilename)
{
  struct TyVirtFile *pVirtFile;
  for( pVirtFile = l_pVirtFiles; pVirtFile; pVirtFile = pVirtFile->pNextFile )
  {
    if(strcmp(pVirtFile->pszFilename, pszFilename)==0)
    {
      return pVirtFile;
    }
  }
  return NULL;
}

/**
 * \brief Write a new virtual file chunk.
 *
 * \param[in]   pBuff Data to copy into the virtual file chunk. If NULL, then no data is copied, 
 *              but nLength bytes of memory is still allocated.
 *
 * \param[in]   nLength Length in bytes of the new virtual file chunk.
 *
 * \return    Pointer to the virtual file chunk if succesfully written, or NULL if the write fails.
 */
static struct TyVirtFileChunk * NewVirtFileChunk(unsigned char *pBuff, int nLength)
{
  struct TyVirtFileChunk * pVirtFileChunkNew;
#ifdef GG_DEBUG_LIST
  static int nID=0;
#endif

  pVirtFileChunkNew = OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(struct TyVirtFileChunk));
  if(!pVirtFileChunkNew)
  {
    HQFAILV(("NewVirtFileChunk: failed to allocate a new file chunk structure %d bytes", sizeof(struct TyVirtFileChunk)));
    return NULL;
  }

  /* Try OILMemoryPoolJob first, without blocking. If no memory immediately avaiable,
     then try RIR Memory. */
  pVirtFileChunkNew->pMemory = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, nLength);
  if(!pVirtFileChunkNew->pMemory)
  {
    pVirtFileChunkNew->pMemory = MemAlloc(nLength, FALSE, FALSE);
    if(!pVirtFileChunkNew->pMemory)
    {
      /* We could make a second attempt in OILMemoryPoolJob with OIL_MemBlock, just in
         case there are checked in pages waiting to be printed */

      HQFAILV(("NewVirtFileChunk: failed to allocate a new file chunk %d bytes", nLength));
      OIL_free(OILMemoryPoolJob, pVirtFileChunkNew);
      return NULL;
    }
    else
    {
      pVirtFileChunkNew->nMemoryPool = OIL_VIRTFILE_MEM_RIP;
    }
  }
  else
  {
    pVirtFileChunkNew->nMemoryPool = OIL_VIRTFILE_MEM_OIL_JOB;
  }

  pVirtFileChunkNew->pNextChunk = NULL;
  if(pBuff) {
    memcpy(pVirtFileChunkNew->pMemory, pBuff, nLength);
  }

  pVirtFileChunkNew->cbSize = nLength;
#ifdef GG_DEBUG_LIST
  pVirtFileChunkNew->nID = nID++;
#endif

  return pVirtFileChunkNew;
}

/**
 * \brief Allocate a new virtual file structure.
 *
 * \param[in] pszFilename Filename string for the new virtual file.
 *
 * \return    Pointer to the new virtual file structure if succesfully 
 *            created, or NULL if the allocation fails.
 */
static struct TyVirtFile * NewVirtFile(char *pszFilename)
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFile **ppVirtFileNew;
  
  ppVirtFileNew = &l_pVirtFiles;

  for( pVirtFile = l_pVirtFiles; pVirtFile; pVirtFile = pVirtFile->pNextFile ) 
  {
    ppVirtFileNew = &pVirtFile->pNextFile;
  }

  *ppVirtFileNew = OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(struct TyVirtFile));

  if(*ppVirtFileNew == NULL)
  {
    HQFAILV(("NewVirtFile: Failed to allocate a new virtual file structure %s.", pszFilename));
    return NULL;
  }
  memset(*ppVirtFileNew, 0x00, sizeof(struct TyVirtFile));

  (*ppVirtFileNew)->pszFilename = (char*)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, strlen(pszFilename)+1);

  if((*ppVirtFileNew)->pszFilename == NULL)
  {
    HQFAILV(("NewVirtFile: Failed to allocate filename string for virtual file %s.", pszFilename));
    OIL_free(OILMemoryPoolJob, *ppVirtFileNew);
    *ppVirtFileNew=NULL;
    return NULL;
  }

  strcpy((char*)(*ppVirtFileNew)->pszFilename, (char*)pszFilename);

  (*ppVirtFileNew)->nFileID = l_nNextFileID;
  l_nNextFileID++;

  return (*ppVirtFileNew);
}

/**
 * \brief Delete all the virtual file chunks in a virtual file.
 *
 * \param[in] pVirtFile Pointer to the virtual file structure that is to be emptied.
 *
 * \return Returns 1 if successful, or 0 if it fails.
 */
static int DeleteAllChunks(struct TyVirtFile *pVirtFile)
{
  struct TyVirtFileChunk *pFileChunk;
  struct TyVirtFileChunk *pFileChunkNext;

  HQASSERT(pVirtFile, "DeleteAllChunks: pVirtFile is NULL");

  for( pFileChunk = pVirtFile->pFirstChunk; pFileChunk; pFileChunk = pFileChunkNext )
  {
    pFileChunkNext = pFileChunk->pNextChunk;
    switch(pFileChunk->nMemoryPool)
    {
    case OIL_VIRTFILE_MEM_RIP:
      MemFree(pFileChunk->pMemory);
      break;
    case OIL_VIRTFILE_MEM_OIL_JOB:
      OIL_free(OILMemoryPoolJob, pFileChunk->pMemory);
      break;
    default:
      HQFAILV(("DeleteAllChunks: not expect memory pool %d", pFileChunk->nMemoryPool));
      return 0;
      break;
    }
    OIL_free(OILMemoryPoolJob, pFileChunk);
  }

  pVirtFile->pCurrentChunk = NULL;
  pVirtFile->pFirstChunk = NULL;
  pVirtFile->uCurrentChunkPos = 0;
  pVirtFile->uLength = 0;

  return 1;
}

/**
 * \brief Open a virtual file and, optionally, empty it.
 *
 * This function aims to return the file ID of a file with the specified filename.
 * If an empty file is required, SW_TRUNC or SW_CREAT should be set.  These flags
 * direct the function to open and truncate any existing file with the same filename,
 * or to create a file with the given file name if one does not already exist.
 * 
 * This function will first search for an existing virtual file with the given filename.
 * If no matching file is found but an empty file has been requested, a new file with 
 * the given file name will be created.
 * If a matching file is found, it is opened (and truncated, if required).
 * \param[in] pszFilename Filename of virtual file to open.
 *
 * \param[in] nOpenFlags Open flags. SW_CREAT or SW_TRUNC are used to create an empty virtual file. 
 *                   All other flags are ignored.
 *
 * \return Returns the file ID number if the file is successfully opened, or -1 if the call fails.
 */
int OIL_VirtFileOpen(char *pszFilename, int nOpenFlags)
{
  struct TyVirtFile *pVirtFile;
  
  pVirtFile = GetVirtFileFromFilename(pszFilename);

  /* Create and/or open with trunctation */
  if(nOpenFlags & (SW_CREAT | SW_TRUNC))
  {
    /* If file exists then remove its contents */
    if(pVirtFile)
    {
      DeleteAllChunks(pVirtFile);
    }
    /* Otherwise attempt to create it */
    else
    {
      pVirtFile = NewVirtFile(pszFilename);
    }
  }

  /* We should have a file structure by now, otherwise we return an error */
  if(!pVirtFile)
    return -1;

  /* Set file position to the beginning */
  pVirtFile->pCurrentChunk = pVirtFile->pFirstChunk;
  pVirtFile->uCurrentChunkPos = 0;

  return (pVirtFile->nFileID);
}

/**
 * \brief Close a virtual file.
 *
 * \param[in] nFileID Filename ID number of the virtual file to close.
 *
 * \return    Returns 0 if the file is successfully closed, or -1 if the call fails.
 */
int OIL_VirtFileClose(int nFileID)
{
  struct TyVirtFile *pVirtFile;

  pVirtFile = GetVirtFileFromID(nFileID);
  if(!pVirtFile)
    return -1;

  pVirtFile->pCurrentChunk = pVirtFile->pFirstChunk;
  pVirtFile->uCurrentChunkPos = 0;

  return 0;
}

/**
 * \brief Delete a virtual file.
 *
 * \param[in] pszFilename Filename of virtual file to delete.
 *
 * \return    Returns 0 if the file is successfully deleted, or -1 if the call fails.
 */
int OIL_VirtFileDelete(char *pszFilename)
{
  struct TyVirtFile *pVirtFileDelete;
  struct TyVirtFile *pVirtFile;
  struct TyVirtFile *pVirtFilePrev = NULL;

  GG_SHOW(GG_SHOW_VIRTFILE,"OIL_VirtFileDelete %s\n", pszFilename);

  pVirtFileDelete = GetVirtFileFromFilename(pszFilename);
  if(!pVirtFileDelete)
    return -1;

  for( pVirtFile = l_pVirtFiles; pVirtFile; pVirtFile = pVirtFile->pNextFile )
  {
    if(pVirtFile == pVirtFileDelete)
    {
      if(pVirtFilePrev)
      {
        pVirtFilePrev->pNextFile = pVirtFile->pNextFile;
      }
      else
      {
        if(pVirtFile->pNextFile) {
          l_pVirtFiles = pVirtFile->pNextFile;
        } else {
          l_nNextFileID = 0;
          l_pVirtFiles = NULL;
        }
      }

      DeleteAllChunks(pVirtFile);
      OIL_free(OILMemoryPoolJob, pVirtFile->pszFilename);
      OIL_free(OILMemoryPoolJob, pVirtFile);
      return 0;
    }
    pVirtFilePrev = pVirtFile;
  }

  return -1;
}

/**
 * \brief Read data from a virtual file into a buffer.
 *
 * \param[in]  nFileID Filename ID number of the virtual file to read.
 *
 * \param[out] pBuff Buffer to receive the data read from the file.
 *
 * \param[in]  nLength Length of the buffer.
 *
 * \return     Returns the number of bytes read, or -1 if the file is not found.
 */
int OIL_VirtFileRead(int nFileID, unsigned char *pBuff, int nLength)
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFileChunk *pVirtFileChunk;
  unsigned int uToRead;
  unsigned int uRemaining;
  unsigned char *pDst = pBuff;

  HQASSERT(pBuff, "OIL_VirtFileRead: pBuff is NULL");

  pVirtFile = GetVirtFileFromID(nFileID);

  /* Does it exist? */
  if(!pVirtFile)
    return -1;

  /* Is the file empty? */
  if(!pVirtFile->pCurrentChunk)
    return 0;
  /* At the end of file */
  if((pVirtFile->pCurrentChunk->pNextChunk == NULL) && (pVirtFile->pCurrentChunk->cbSize == pVirtFile->uCurrentChunkPos))
    return 0;
  /* If the requested buffer fits in the current chunk, then we
     simply copy the data and return early */
  if((pVirtFile->pCurrentChunk->cbSize - pVirtFile->uCurrentChunkPos) >= (unsigned int)nLength)
  {
    memcpy(pDst, pVirtFile->pCurrentChunk->pMemory + pVirtFile->uCurrentChunkPos, nLength);
    pVirtFile->uCurrentChunkPos+=nLength;
    return nLength;
  }

  /* otherwise we'll get the remaining data from the current chunk,
     and then copy the data out of the remain chunks in the loop below */
  uRemaining = (pVirtFile->pCurrentChunk->cbSize - pVirtFile->uCurrentChunkPos);
  if(uRemaining > 0)
  {
    memcpy(pDst, pVirtFile->pCurrentChunk->pMemory + pVirtFile->uCurrentChunkPos, uRemaining);
    pDst += uRemaining;
    pVirtFile->uCurrentChunkPos+=uRemaining;
  }
  uToRead = nLength - uRemaining;

  /* At the end of file */
  if((pVirtFile->pCurrentChunk->pNextChunk == NULL) && (pVirtFile->pCurrentChunk->cbSize == pVirtFile->uCurrentChunkPos))
    return (nLength - uToRead);

  pVirtFile->pCurrentChunk = pVirtFile->pCurrentChunk->pNextChunk;
  pVirtFile->uCurrentChunkPos = 0;

  for( pVirtFileChunk = pVirtFile->pCurrentChunk; (uToRead!=0) && pVirtFileChunk; pVirtFileChunk = pVirtFileChunk->pNextChunk)
  {
    /* the remaining request data fits in this chunk */
    if(uToRead <= pVirtFileChunk->cbSize)
    {
      memcpy(pDst, pVirtFileChunk->pMemory, uToRead);
      pVirtFile->pCurrentChunk = pVirtFileChunk;
      pVirtFile->uCurrentChunkPos = uToRead;
      return nLength;
    }
    else
    {
      memcpy(pDst, pVirtFileChunk->pMemory, pVirtFileChunk->cbSize);
      uToRead -= pVirtFileChunk->cbSize;
      pDst += pVirtFileChunk->cbSize;
    }
  }

  /* if we made it here, then the requested number of bytes to read 
     is more than the length of the virtual file */
  if(pVirtFileChunk)
  {
    pVirtFile->pCurrentChunk = pVirtFileChunk;
    pVirtFile->uCurrentChunkPos = 0;
  }

  return (nLength - uToRead);
}

/**
 * \brief Write from a buffer to a virtual file.
 *
 * \param[in] nFileID Filename ID number of the virtual file to recieve the data.
 *
 * \param[in] pBuff Buffer containing the data to write to the virtual file.
 *
 * \param[in] nLength Length of the buffer.
 *
 * \return    Returns the number of bytes written to the file, or -1 if the file does not exist.
 */
int OIL_VirtFileWrite(int nFileID, unsigned char *pBuff, int nLength)
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFileChunk *pVirtFileChunkNew;
  struct TyVirtFileChunk *pVirtFileChunk;
  unsigned char *pSrc;
  int nToWrite;
  int nWritten=0;

  pVirtFile = GetVirtFileFromID(nFileID);

  /* Does it exist? */
  if(!pVirtFile)
    return -1;

  /* is it the first ever chunk? */
  if(pVirtFile->pCurrentChunk==NULL)
  {
    pVirtFileChunkNew = NewVirtFileChunk(pBuff, nLength);
    if(!pVirtFileChunkNew)
      return 0;

    pVirtFile->pCurrentChunk = pVirtFileChunkNew;
    pVirtFile->pFirstChunk = pVirtFileChunkNew;
    pVirtFile->uCurrentChunkPos = nLength;
    pVirtFile->uLength = nLength;
    nWritten = nLength;
  }
  else
  {
    /* Are we currently at the end of the file? */
    if((pVirtFile->pCurrentChunk->pNextChunk == NULL) && (pVirtFile->uCurrentChunkPos == pVirtFile->pCurrentChunk->cbSize))
    {
      pVirtFileChunkNew = NewVirtFileChunk(pBuff, nLength);
      if(!pVirtFileChunkNew)
        return 0;

      pVirtFile->pCurrentChunk->pNextChunk = pVirtFileChunkNew;
      pVirtFile->pCurrentChunk = pVirtFileChunkNew;
      pVirtFile->uCurrentChunkPos = pVirtFileChunkNew->cbSize;
      pVirtFile->uLength += nLength;
      nWritten = nLength;
    }
    else
    {
      /* does the data fit into the current chunk? */
      if((pVirtFile->uCurrentChunkPos + nLength) <= pVirtFile->pCurrentChunk->cbSize)
      {
        memcpy(pVirtFile->pCurrentChunk->pMemory + pVirtFile->uCurrentChunkPos, pBuff, nLength);
        pVirtFile->uCurrentChunkPos += nLength;
        nWritten = nLength;
      }
      else
      {
        int nBytesToCopy;
        /* We need to write more data than fits in the current chunk.
           Fill the current chunk, and any next chunks until existing file is full.
           Then create a new chunk for remaining buffer. 
        */

        nToWrite = nLength;
        pSrc = pBuff;

        nBytesToCopy = pVirtFile->pCurrentChunk->cbSize - pVirtFile->uCurrentChunkPos;

        memcpy(pVirtFile->pCurrentChunk->pMemory, pSrc, nBytesToCopy);
        nToWrite -= nBytesToCopy;
        pSrc += nBytesToCopy;
        nWritten += nBytesToCopy;
        pVirtFile->uCurrentChunkPos = pVirtFile->pCurrentChunk->cbSize;

        for(pVirtFileChunk = pVirtFile->pCurrentChunk->pNextChunk; 
            pVirtFileChunk && (nToWrite > 0); 
            pVirtFileChunk=pVirtFileChunk->pNextChunk)
        {
          nBytesToCopy = (nToWrite <= (int)pVirtFileChunk->cbSize)? nToWrite : (int)pVirtFileChunk->cbSize;
          memcpy(pVirtFileChunk->pMemory, pSrc, nBytesToCopy);
          nWritten += nBytesToCopy;
          nToWrite -= nBytesToCopy;
          pSrc += nBytesToCopy;
          pVirtFile->pCurrentChunk = pVirtFileChunk;
          pVirtFile->uCurrentChunkPos = nBytesToCopy;
        }

        if(nToWrite>0)
        {
          /* recurse to append a new chunk */
          nWritten += OIL_VirtFileWrite(nFileID, pSrc, nToWrite);
        }
      }
    }
  }

  return nWritten;
}

/**
 * \brief Seek through a virtual file.
 *
 * \param[in] nFileID Filename ID number of the virtual file to seek.
 *
 * \param[in] pDestination Distance to seek, in bytes, from the specified starting point.
 *
 * \param[in] nWhence Starting point of the seek. Valid values are:
 * \arg       SW_SET  The start of the file.
 * \arg       SW_INCR The current position in the file.
 * \arg       SW_XTND The end of the file.  Not supported in this implementation.
 *
 * \return Returns 1 if the seek was successful, 0 if the seek failed, or -1 if the file does not exist.
 */
int OIL_VirtFileSeek(int nFileID, Hq32x2 *pDestination, int nWhence)
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFileChunk *pVirtFileChunk;
  unsigned int uDest;

  HQASSERT(pDestination, "OIL_VirtFileSeek: destination NULL");
  HQASSERT(pDestination->high == 0,
           "OIL_VirtFileSeek: only 32 bit seeks are supported");

  pVirtFile = GetVirtFileFromID(nFileID);

  /* Does it exist? */
  if(!pVirtFile)
    return -1;

  if ( !Hq32x2ToUint32( pDestination, &uDest ) )
    return 0;

  if(pVirtFile->pFirstChunk==NULL)
    return (uDest==0);

  switch(nWhence)
  {
  case SW_SET:
    GG_SHOW(GG_SHOW_VIRTFILE, "OIL_VirtFileSeek: SW_SET %u uLength=%d uCurrentChunkPos=%d cbSize=%d\n", 
      uDest, pVirtFile->uLength, pVirtFile->uCurrentChunkPos, pVirtFile->pCurrentChunk->cbSize);

    if(uDest==0)
    {
      pVirtFile->pCurrentChunk = pVirtFile->pFirstChunk;
      pVirtFile->uCurrentChunkPos = 0;
    }
    else
    { 
      for(pVirtFile->pCurrentChunk = pVirtFile->pFirstChunk; 
          pVirtFile->pCurrentChunk;
          pVirtFile->pCurrentChunk = pVirtFile->pCurrentChunk->pNextChunk) 
      { 
        if(uDest < pVirtFile->pCurrentChunk->cbSize) {
          pVirtFile->uCurrentChunkPos = uDest;
          uDest = 0;
          break;
        }
        uDest -= pVirtFile->pCurrentChunk->cbSize; 
        if(!pVirtFile->pCurrentChunk->pNextChunk) {
          pVirtFile->uCurrentChunkPos = pVirtFile->pCurrentChunk->cbSize;
          break;
        }
      }

      if(uDest > 0)
      {
        int nWritten;

        /* We've been asked to seek past the end of the file... maybe an error? */
        GG_SHOW(GG_SHOW_VIRTFILE, "OIL_VirtFileSeek: Seek %u bytes past the end of the file... maybe an error?\n", uDest);

        nWritten = OIL_VirtFileWrite(nFileID, NULL, uDest);
        if(nWritten != (int)uDest)
          return 0;
      }
    }

    break;

  case SW_INCR:
    GG_SHOW(GG_SHOW_VIRTFILE, "OIL_VirtFileSeek: SW_INCR %u uLength=%d uCurrentChunkPos=%d cbSize=%d\n", 
      uDest, pVirtFile->uLength, pVirtFile->uCurrentChunkPos, pVirtFile->pCurrentChunk->cbSize);

    if(uDest <= (pVirtFile->pCurrentChunk->cbSize - pVirtFile->uCurrentChunkPos))
    {
      pVirtFile->uCurrentChunkPos += uDest;
      break;
    }

    uDest -= (pVirtFile->pCurrentChunk->cbSize - pVirtFile->uCurrentChunkPos);

    for(pVirtFileChunk = pVirtFile->pCurrentChunk->pNextChunk; 
        pVirtFileChunk && (uDest > pVirtFileChunk->cbSize); 
        pVirtFileChunk = pVirtFileChunk->pNextChunk) 
    { 
      uDest -= pVirtFileChunk->cbSize; 
    }

    if(!pVirtFileChunk)
      return 0;

    pVirtFile->uCurrentChunkPos = uDest;
    pVirtFile->pCurrentChunk = pVirtFileChunk;
    break ;

  case SW_XTND:
    GG_SHOW(GG_SHOW_VIRTFILE, "SW_XTND: SW_INCR %u uLength=%d uCurrentChunkPos=%d cbSize=%d\n", 
      uDest, pVirtFile->uLength, pVirtFile->uCurrentChunkPos, pVirtFile->pCurrentChunk->cbSize);

    HQFAIL("OIL_VirtFileSeek: Seek method SW_XTND not implemented");
    return 0;
    break;

  default:
    HQFAILV(("OIL_VirtFileSeek: Unknown seek method %d", nWhence));
    return 0;
    break;
  }

  return 1;
}

/**
 * \brief Get the status of a virtual file.
 *
 * This function writes data pertaining to the specified file into a STAT structure.
 * However, this implementation only retrieves the file length, and not the created, last 
 * referenced or last modified times.
 * \param[in]  pszFilename Filename of the required virtual file.
 *
 * \param[out] pStat Pointer to STAT structure to receive status. Only the file length is retrieved.
 *
 * \return     Returns 1 if successful, or -1 if the call fails.
 */
int OIL_VirtFileStatus(char *pszFilename, STAT *pStat)
{
  struct TyVirtFile *pVirtFile;

  HQASSERT(pStat, "OIL_VirtFileStatus: pStat is NULL");

  pVirtFile = GetVirtFileFromFilename(pszFilename);
  if(!pVirtFile)
    return -1;

  HqU32x2FromUint32( &(pStat->bytes), pVirtFile->uLength ); 

  return 1;
}

/**
 * \brief Get either the total length or remaining bytes available in a virtual file.
 *
 * \param[in]  nFileID nFileID The ID number of the required virtual file.
 *
 * \param[out] pBytes  Pointer to an Hq32x2 structure to receive the requested data.
 *
 * \param[in]  nReason Indicates whether the function is to retrieve the total length of 
 *                     the file or the number of available bytes in the file.  Valid
 *                     values are:
 * \arg       SW_BYTES_AVAIL_REL   Retrieve the remaining available bytes in the file.
 * \arg       SW_BYTES_TOTAL_ABS   Retrieve the total length of the file
 *
 * \return Returns 1 if the query was successful, or -1 if the file was not found.
 */
int OIL_VirtFileBytes(int nFileID, Hq32x2 *pBytes, int nReason)
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFileChunk *pVirtFileChunk;
  unsigned int uBytes;

  pVirtFile = GetVirtFileFromID(nFileID);
  if(!pVirtFile)
    return -1;

  switch ( nReason )
  {
  case SW_BYTES_AVAIL_REL:

    /* No current chunk, then nothing left in file */
    if(!pVirtFile->pCurrentChunk)
    {
      Hq32x2FromUint32( pBytes, 0 );
      break;
    }

    /* No next chunk, so we'll return the space left in the current chunk */
    if(pVirtFile->pCurrentChunk->pNextChunk == NULL)
    {
      Hq32x2FromUint32( pBytes, pVirtFile->pCurrentChunk->cbSize - pVirtFile->uCurrentChunkPos );
      break;
    }

    /* Where is the current position from the first chunk? */
    uBytes = 0;
    for(pVirtFileChunk = pVirtFile->pFirstChunk;
      pVirtFileChunk; 
      pVirtFileChunk = pVirtFileChunk->pNextChunk)
    {
      if(pVirtFileChunk == pVirtFile->pCurrentChunk)
      {
        uBytes += pVirtFile->uCurrentChunkPos;
        break;
      }
      uBytes += pVirtFileChunk->cbSize;
    }
    /* Total length minus current position */
    Hq32x2FromUint32( pBytes, pVirtFile->uLength - uBytes );
    break;


  case SW_BYTES_TOTAL_ABS:
    Hq32x2FromUint32( pBytes, pVirtFile->uLength );
    break;

  default:
    HQFAILV(("OIL_VirtFileBytes: 0x%x, Unknown method %d", nFileID, nReason));
    return -1 ;
    break;
  }

  return 1;
}

/**
 * \brief Get either the total length or remaining bytes available in a virtual file.
 *
 * \param[in]  nFileID nFileID The ID number of the required virtual file.
 *
 * \return Returns 1 if the output was successful, or -1 if the file was not found.
 */
int OIL_VirtFileSendToOutput(int nFileID)
{
  struct TyVirtFile *pVirtFile;
  struct TyVirtFileChunk *pVirtFileChunk;
  int nWritten;
  PMS_TyBackChannelWriteFileOut tFile;

  pVirtFile = GetVirtFileFromID(nFileID);
  if(!pVirtFile)
    return -1;

  memset(&tFile, 0x00, sizeof(tFile));
  /* plus six to skip "Files\" */
  sprintf(tFile.szFilename, "%s", pVirtFile->pszFilename + 6);

  /* Output the complete file */
  for(pVirtFileChunk = pVirtFile->pFirstChunk;
      pVirtFileChunk;
      pVirtFileChunk = pVirtFileChunk->pNextChunk)
  {
    PMS_WriteDataStream(PMS_WRITE_FILE_OUT,
                        &tFile,
                        pVirtFileChunk->pMemory,
                        pVirtFileChunk->cbSize,
                        &nWritten);
  }

  return 1;
}

/**
 * \brief Free all memory allocate by this module.
 *
 * \return Returns 1.
 */
int OIL_VirtFileCleanup() {
  struct TyVirtFile *pVirtFile;
  struct TyVirtFile *pVirtFileNext;
  
  for( pVirtFile = l_pVirtFiles; pVirtFile; pVirtFile = pVirtFileNext )
  {
    pVirtFileNext = pVirtFile->pNextFile;
    GG_SHOW(GG_SHOW_VIRTFILE,"OIL_VirtFileCleanup delete %p, %s\n", pVirtFile, pVirtFile->pszFilename);
    OIL_VirtFileDelete(pVirtFile->pszFilename);
  }

  return 1;
}

