/* Copyright (C) 2011, 2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_input_socket.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Input Socket.
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
#define MAX_PATH PATH_MAX
#elif defined(THREADX)
#include <rtipapi.h>

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
#endif

#include <stdio.h>

#include "pms.h"
#include "pms_malloc.h"
#include "pms_platform.h"
#include "oil_entry.h"
#include "pms_input_manager.h"
#ifdef PMS_INPUT_IS_FILE
#include "pms_file_in.h"
#endif
#include "pms_thread.h"
#ifdef PMS_SUPPORT_SOCKET
#include "pms_socket.h"
#endif

extern int g_cbReceiveBuffer; /* for file and socket input buffer */
extern int g_bStoreJobBeforeRip; /* store whole job before passing to rip */
extern char **aszJobNames;
extern int nJobs;

#ifdef PMS_SUPPORT_SOCKET

PMS_SOCKET_HANDLE l_hSocketInput=NULL;
int g_nThisHostPort;
#endif


/* Just initialize the module, not the socket subsystem */
int Input_Socket_Initialize() {
  /* Initialize server once as it sits way outside the job loops */
  if(!l_hSocketInput) {
    l_hSocketInput = Socket_InitServer(g_SocketInPort);
    if(!l_hSocketInput) {
      return FALSE;
    }
  }
  return TRUE;
}

/* Just finialize the module, not the socket subsystem */
int Input_Socket_Finalize() {
  if (g_SocketInPort != 0 )
  {
    /* Socket close actually frees the input buffer and pms structure
       that contains the socket handles */
    Socket_Close(l_hSocketInput);
    l_hSocketInput = NULL;
  }
  return TRUE;
}

/**
 * \brief Wrapper for init input socket function
 *
 */
int Input_Socket_InitDataStream(void)
{
  PMS_ASSERT(l_hSocketInput, ("Input socket handle is NULL\n"));


  return Socket_Listen(l_hSocketInput);
}

/**
 * \brief Wrapper for open input socket function
 *
 */
int Input_Socket_OpenDataStream(void)
{
  PMS_ASSERT(l_hSocketInput, ("Input socket handle is NULL\n"));
  return Socket_OpenInDataStream(l_hSocketInput);
}

/**
 * \brief Wrapper for closing input socket function
 *
 */
int Input_Socket_CloseDataStream(void)
{
  PMS_ASSERT(l_hSocketInput, ("Input socket handle is NULL\n"));

  return Socket_CloseInDataStream(l_hSocketInput);
}

/**
 * \brief Wrapper for peek input socket function
 *
 */
int Input_Socket_PeekDataStream(unsigned char * buffer, int nBytesToRead)
{
  PMS_ASSERT(l_hSocketInput, ("Input socket handle is NULL\n"));
  PMS_ASSERT(buffer, ("Input buffer is NULL\n"));
  return Socket_PeekInDataStream(l_hSocketInput, buffer, nBytesToRead);
}

/**
 * \brief Wrapper for consume input socket function
 *
 */
int Input_Socket_ConsumeDataStream(int nBytesToConsume)
{
  PMS_ASSERT(l_hSocketInput, ("Input socket handle is NULL\n"));
  return Socket_ConsumeInDataStream(l_hSocketInput, nBytesToConsume);
}

/**
 * \brief Wrapper for write to input socket function
 *
 */
int Input_Socket_WriteDataStream(unsigned char *pBuffer, int nBytesToWrite, int *pnBytesWritten)
{
  PMS_ASSERT(l_hSocketInput, ("Input socket handle is NULL\n"));
  return Socket_Send(l_hSocketInput, pBuffer, nBytesToWrite, pnBytesWritten);
}


