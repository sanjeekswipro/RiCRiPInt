/* Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_input_manager.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Input Interface Management.
 *
 */


#include "pms.h"
#include "pms_input_manager.h"
#include "pms_malloc.h"   /* OSMalloc */
#include "pms_platform.h" /* PMS_Delay */
#include "oil_entry.h"    /* Probe flush */

#ifdef PMS_INPUT_IS_FILE
#include "pms_file_in.h"
#endif
#ifdef PMS_SUPPORT_SOCKET
#include "pms_input_socket.h"
#include "pms_socket.h"
#endif
#ifdef PMS_HOT_FOLDER_SUPPORT
#include "pms_input_hotfolder.h"
#endif

/* \brief Input module API */
struct input_module {
  int (*pfnInitDataStream)(void);
  int (*pfnOpenDataStream)(void);
  int (*pfnCloseDataStream)(void);
  int (*pfnPeekDataStream)(unsigned char * buffer, int nBytesToRead);
  int (*pfnConsumeDataStream)(int nBytesToConsume);
  int (*pfnWriteDataStream)(unsigned char * pBuffer, int nBytesToWrite, int * pnBytesWritten);
};

/* \brief Input module node used for linked list */
struct input_modules {
  struct input_module tInput;
  struct input_modules *pNext;
} ;

struct input_modules * l_ptModules = NULL;     /*< List of input modules */
struct input_modules * l_ptModulesTail = NULL; /*< Last module node in the linked list */
struct input_modules * l_ptActive = NULL;      /*< Active connection */

/**
 * \brief Add an input module node to the linked list of input modules.
 *
 */
int Add_Input_Module( int (*pfnInitDataStream)(void),
                      int (*pfnOpenDataStream)(void),
                      int (*pfnCloseDataStream)(void),
                      int (*pfnPeekDataStream)(unsigned char * buffer, int nBytesToRead),
                      int (*pfnConsumeDataStream)(int nBytesToConsume),
                      int (*pfnWriteDataStream)(unsigned char * pBuffer, int nBytesToWrite, int * pnBytesWritten)
    ) 
{
  struct input_modules *pModuleNode;

  PMS_ASSERT(pfnOpenDataStream, ("Add_Input_Module: pfnOpenDataStream must be defined for all modules\n"));

  pModuleNode = OSMalloc(sizeof(struct input_modules), PMS_MemoryPoolPMS);
  if(!pModuleNode) {
    return FALSE;
  }
  pModuleNode->pNext = NULL;
  pModuleNode->tInput.pfnInitDataStream = pfnInitDataStream;
  pModuleNode->tInput.pfnOpenDataStream = pfnOpenDataStream;
  pModuleNode->tInput.pfnCloseDataStream = pfnCloseDataStream;
  pModuleNode->tInput.pfnPeekDataStream = pfnPeekDataStream;
  pModuleNode->tInput.pfnConsumeDataStream = pfnConsumeDataStream;
  pModuleNode->tInput.pfnWriteDataStream = pfnWriteDataStream;

  if(l_ptModulesTail == NULL) {
    l_ptModules = pModuleNode;
    l_ptModulesTail = pModuleNode;
  } else {
    l_ptModulesTail->pNext = pModuleNode;
    l_ptModulesTail = pModuleNode;
  }
  return TRUE;
}


/**
 * \brief Initialise the input interfaces.
 *
 * Prepares the input subsystem to use use selected default interface.\n
 */
int PMS_IM_Initialize()
{
  struct input_modules *pMods;
  int nResult;

#ifdef PMS_HOT_FOLDER_SUPPORT
    /* if hot folder input is enabled a path will be set */
  if (g_pPMSHotFolderPath != NULL )
  {
    HotFolder_Initialize(); /* Initialize subsystem */

    nResult = Add_Input_Module( HotFolder_InitDataStream,
                                HotFolder_OpenDataStream,
                                HotFolder_CloseDataStream,
                                HotFolder_PeekDataStream,
                                HotFolder_ConsumeDataStream,
                                NULL );
    if(!nResult) {
      return FALSE;
    }
  }
#endif

#ifdef PMS_INPUT_IS_FILE
  /* File_Initialize()... not required */

  nResult = Add_Input_Module( File_InitDataStream,
                              File_OpenDataStream,
                              File_CloseDataStream,
                              File_PeekDataStream,
                              File_ConsumeDataStream,
                              NULL ); /* NULL= Send to output to console */
  if(!nResult) {
    return FALSE;
  }
#endif

#ifdef PMS_SUPPORT_SOCKET

  /* if socket input is enable the port will be nonzero */
  if (g_SocketInPort != 0 )
  {
    Socket_Initialize(); /* Initialize subsystem (OS calls) */

    nResult = Input_Socket_Initialize(); /* A socket input channel (create a server socket) */
    if(!nResult) {
      return FALSE;
    }

    nResult = Add_Input_Module( Input_Socket_InitDataStream,
                                Input_Socket_OpenDataStream,
                                Input_Socket_CloseDataStream,
                                Input_Socket_PeekDataStream,
                                Input_Socket_ConsumeDataStream,
                                Input_Socket_WriteDataStream );
    if(!nResult) {
      return FALSE;
    }
  }
#endif

  for(pMods = l_ptModules; pMods; pMods = pMods->pNext) {
    /* initialize streams */
    if(pMods->tInput.pfnInitDataStream)
    {
      (*pMods->tInput.pfnInitDataStream)();
    }
  }

  return 1;
}

/**
 * \brief Cleanup all input modules.
 */
void PMS_IM_Finalize(){
  struct input_modules *pMods;
  struct input_modules *pModsNext;

#ifdef PMS_INPUT_IS_FILE
  /* File_Finalize() ... not implemented as it's not required */
#endif

#ifdef PMS_SUPPORT_SOCKET
  Input_Socket_Finalize();
  Socket_Finalize();
#endif

#ifdef PMS_HOT_FOLDER_SUPPORT
  HotFolder_Finalize();
#endif

  PMS_ASSERT(l_ptActive==NULL, ("PMS_IM_Finalize: Still got an active data stream\n"));

  for(pMods = l_ptModules; pMods; pMods = pModsNext) {
    /* initialize streams */
    pModsNext = pMods->pNext;
    OSFree(pMods, PMS_MemoryPoolPMS);
  }
  l_ptModules = NULL;
  l_ptModulesTail = l_ptModules;
}

/**
 * \brief Block until an input module has a connection.
 *
 * \todo Currently there is no priority override setting on input modules, so in thoery if a job in the hot
 *       folder might not get a chance to run if there are many pending socket connections available.
 *
*/
int PMS_IM_WaitForInput() 
{
  struct input_modules *pMods;
  int bWaitingForJob = 1;
  int nResult;

  /* If there are no input modules then return */
  if(!l_ptModules) {
    return FALSE;
  }

  while(bWaitingForJob) {
    bWaitingForJob = 0; /* Assume all modules are only tried to open once... file on command line etc.
                           If socket, or hot folder or any other input that waits for a job then it will get set again. */
                           
    for(pMods = l_ptModules; pMods; pMods = pMods->pNext) {
      /* Get a job */
      if(pMods->tInput.pfnOpenDataStream) {
        nResult = (*pMods->tInput.pfnOpenDataStream)();
        if(nResult > 0) {
          l_ptActive = pMods; /* l_ptActive is the connected module until PMS_CloseDataStream */
          return TRUE;
        } else if(nResult < 0) {
          /* Don't try this module again... used for files on command, could be used for a once only folder (sd card or similar) */
          pMods->tInput.pfnOpenDataStream = NULL; /* \todo This could be a bit tidier */
        } else {
          bWaitingForJob = 1; /* Try again in a bit */
        }
      }
    }
    PMS_Delay(200); /* /todo Is this delay reasonable? */
  }

  return FALSE;
}

/**
 * \brief Close an active connection.
 */
int PMS_IM_CloseActiveDataStream(void)
{
  int nRetVal = TRUE;
  OIL_ProbeLogFlush();

  if(l_ptActive) {
    if(l_ptActive->tInput.pfnCloseDataStream)
    {
      nRetVal = (*l_ptActive->tInput.pfnCloseDataStream)();
    }
    l_ptActive = NULL;
  }
  return nRetVal;
}

/**
 * \brief Get some data from the active connection.
 */
int PMS_IM_PeekActiveDataStream(unsigned char * buffer, int nBytesToRead) 
{
  if(l_ptActive && l_ptActive->tInput.pfnPeekDataStream) {
    return l_ptActive->tInput.pfnPeekDataStream(buffer, nBytesToRead);
  }

  return 0;
}

/**
 * \brief Consume some data from the active connection.
 */
int PMS_IM_ConsumeActiveDataStream(int nBytesToConsume)
{
  if(l_ptActive && l_ptActive->tInput.pfnConsumeDataStream) {
    return l_ptActive->tInput.pfnConsumeDataStream(nBytesToConsume);
  }

  return 0;
}

/**
 * \brief Output data to the active connection.
 *
 * Input modules optionally provide a write function. 
 */
int PMS_IM_WriteToActiveDataStream(unsigned char * pBuffer, int nBytesToWrite, int * pnBytesWritten)
{
  if(l_ptActive && l_ptActive->tInput.pfnWriteDataStream) {
    return l_ptActive->tInput.pfnWriteDataStream(pBuffer, nBytesToWrite, pnBytesWritten);
  } else {
    /* Intentionally throw away the data as there either isn't an write function or there's no valid connection */
    *pnBytesWritten = nBytesToWrite;
  }

  return 0;
}

