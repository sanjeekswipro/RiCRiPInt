/* Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_input_hotfolder.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Hot Folder module.
 *
 */

#ifdef WIN32
#include <windows.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#elif defined(UNIX) || defined(MACOSX)
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifndef MAX_PATH
#ifdef PATH_MAX
#define MAX_PATH PATH_MAX
#else
#define MAX_PATH 512
#endif
#endif
#elif defined(THREADX)

#else /* vxworks */
#include <vxWorks.h>
#include <sockLib.h>
#include <inetLib.h>
#include <taskLib.h>
#include <stdioLib.h>
#include <strLib.h>
#include <ioLib.h>
#include <fioLib.h>
#include <hostLib.h>
#endif /* VXWORKS */

#include <stdio.h>

#include "pms.h"
#include "pms_input_hotfolder.h"
#include "pms_malloc.h"
#include "pms_platform.h"
#include "oil_entry.h"
#include "pms_input_manager.h"
#ifdef PMS_INPUT_IS_FILE
#include "pms_file_in.h"
#endif
#include "pms_thread.h"


extern int g_cbReceiveBuffer; /* for file and socket input buffer */
extern int g_bStoreJobBeforeRip; /* store whole job before passing to rip */
extern char **aszJobNames;
extern int nJobs;

#ifdef PMS_HOT_FOLDER_SUPPORT
static char aszHotFolderFiles[2][MAX_PATH] = {"", ""};
static char *pHotFolderFiles[2];
static char FileBuffer[256];
static char *pFileBuffer;
void *pHotFolderSearchThread = NULL;
void *pHotFolderInputThread = NULL;
void *semHotFolderPeekHold;
static unsigned char bActiveFile;
static unsigned char bVirtualFile;
static char PJLStart[] = "\033%-12345X@PJL COMMENT Global Graphics\n";
static char PJLEnterLanguageImage[] = "@PJL ENTER LANGUAGE=IMAGE\n";
void HotFolder_Initialize(void);
void HotFolder_Search_Handler(void *args);

/**
 * \brief Initialise hot folder module.
 */
void HotFolder_Initialize(void)
{
  bActiveFile = FALSE;
}

/**
 * \brief Cleanup hot folder module.
 */
void HotFolder_Finalize(void)
{
}

/**
 * \brief Hot folder search.
 */
int HotFolder_Search()
{
  unsigned int filesize, filesize_last;
  char szDir[MAX_PATH];
  char szExt[6];
  void *hFind;
  FILE *pFile;
  int fd, i;
  char *p;
  struct stat buf;
  int bRescan = 0;

  strcpy(szDir, g_pPMSHotFolderPath);
  strcat(szDir, "\\*");
  do {
    hFind = PMS_Initiate_DirectorySearch(szDir);
    /* Find the first file in the directory */
    if(PMS_Run_DirectorySearch(g_pPMSHotFolderPath, hFind, (char*)&aszHotFolderFiles[0]))
    {
      /* There is a file, lets wait until its size is stable */
      PMS_Stop_DirectorySearch(hFind);
      bRescan = 0;
      filesize = 0;
      bVirtualFile = FALSE;
      do {
        filesize_last = filesize;
        /* wait 1 second to see if the file is complete */
        PMS_Delay(1000);
        pFile = fopen(aszHotFolderFiles[0], "r");
        if(!pFile) { /* probably because of an OS delayed remove or user removed file since the dir scan */
          filesize_last = filesize;
          bRescan = TRUE;
          break;
        } else {
          fd = fileno(pFile);
          fstat(fd, &buf);
          filesize = buf.st_size;
          fclose(pFile);
        }
      } while (filesize != filesize_last);

      if(bRescan) {
        continue; /* restart do loop to rescan directory */
      }

      /* now save the file info and open the file for input to the rip */
      bActiveFile = TRUE;

      /* check for a raw image file as we need to send PJL code */
      p = (char *)aszHotFolderFiles + strlen(aszHotFolderFiles[0]) ;
      i = 0;
      while(p--)
      {
        if(*p == '.'){
          strcpy(szExt, p);
          break;
        }
        if(++i >= (sizeof(szExt)-1)){
          strcpy(szExt, ".none");
          break;
        }
      }
      for(i = 0; i < (int)strlen(szExt);i++)
      {
        szExt[i] = (char)tolower(szExt[i]);
      }

      if((strcmp(szExt, ".tiff") == 0) || (strcmp(szExt, ".tif") == 0) ||
         (strcmp(szExt, ".jpeg") == 0) || (strcmp(szExt, ".jpg") == 0) ||
         (strcmp(szExt, ".jfif") == 0) || (strcmp(szExt, ".jif") == 0))
      {
        /* set up the PJL wrapper to define the file for ripping */
        sprintf(FileBuffer,"%s@PJL SET IMAGEFILE=\"%s\"\n%s", PJLStart, aszHotFolderFiles[0], PJLEnterLanguageImage);
        pFileBuffer = FileBuffer;
        bVirtualFile = TRUE;
        return TRUE;
      }
      else
      {
        /* set up file for file input */
        pHotFolderFiles[0] = (char*)&aszHotFolderFiles[0];
        pHotFolderFiles[1] = (char*)&aszHotFolderFiles[1];

        /* Reuse the file input method functions to process the file found */
        aszJobNames = (char**)&pHotFolderFiles;
        nJobs = 1;
        File_InitDataStream();
        File_OpenDataStream();
        return TRUE;
      }
    } else {
      /* No file to process */
      bRescan = FALSE;
    }
  } while (bRescan);

  /* No file to process this time */
  return FALSE;
}

/**
 * \brief Wrapper for init hot folder function
 *
 */
int HotFolder_InitDataStream(void)
{
  return 1;
}

/**
 * \brief Wrapper for open input hot folder function
 *
 */
int HotFolder_OpenDataStream(void)
{
  int nResult;

  /* Look for a job. */
  nResult = HotFolder_Search();

  if(nResult) {
    return 1; /* Got a job */
  } else {
    return 0; /* Try again in a bit */
  }
  /* return -1 would mean don't try again... You could use this as a once only folder, e.g. when inserting an sd card. */
}


/**
 * \brief Wrapper for peek input hot folder function
 *
 */
int HotFolder_PeekDataStream(unsigned char * buffer, int nBytesToRead)
{
  if(bVirtualFile) {
    /* Use the file wrapper job */
    /* \todo What if the filename contains multibyte chars... strcpy and strlen will probably be wrong */
    strcpy((char*)buffer, pFileBuffer);
    return ((int)strlen((char*)buffer));
  }
  else {
    return (File_PeekDataStream(buffer, nBytesToRead));
  }
}

/**
 * \brief Wrapper for consume input hot folder function
 *
 */
int HotFolder_ConsumeDataStream(int nBytesToConsume)
{
  int nResult;
  if(bVirtualFile) {
    pFileBuffer += nBytesToConsume;
    nResult = 1;
  } else {
    nResult = File_ConsumeDataStream(nBytesToConsume);
  }

  return nResult;
}

/**
 * \brief Wrapper for write to hot folder function
 *
 */
int HotFolder_WriteDataStream(unsigned char *pBuffer, int nBytesToWrite, int *pnBytesWritten)
{
  UNUSED_PARAM(unsigned char *, pBuffer);
  UNUSED_PARAM(int, nBytesToWrite);
  UNUSED_PARAM(int *, pnBytesWritten);
  return 1;
}

/**
 * \brief Wrapper for closing hot folder function
 *
 */
int HotFolder_CloseDataStream(void)
{
  int nResult = 0;
  if(bActiveFile && !bVirtualFile)
  {
    File_CloseDataStream();
    nResult = remove(aszHotFolderFiles[0]);
    bActiveFile = FALSE;
  }
  else if(bVirtualFile)
  {
    nResult = remove(aszHotFolderFiles[0]);
    bActiveFile = FALSE;
    bVirtualFile = FALSE;
  }
  if(nResult != 0) {
    PMS_SHOW_ERROR("Failed to remove file \"%s\" from hot folder.\n", aszHotFolderFiles[0]);
  }
  return 1;
}

#endif

