/* Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_socket.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Socket Interface.
 *
 */

#if defined(WIN32)
#include <winsock.h>
#include <process.h>
#include <io.h>
#define socklen_t int

#elif defined(UNIX) || defined(MACOSX)
#ifdef linux
#define _BSD_SOURCE 1
#endif
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#elif defined(THREADX)
#include <rtipapi.h>

#elif defined(VXWORKS)
#include <vxWorks.h>
#include <sockLib.h>
#include <inetLib.h>
#include <taskLib.h>
#include <stdioLib.h>
#include <strLib.h>
#include <ioLib.h>
#include <fioLib.h>
#include <hostLib.h>

#else
#error Unknown platform
#endif

#include <stdio.h>

#include "pms.h"
#include "pms_malloc.h"
#include "pms_socket.h"
#include "pms_platform.h"

/* #define PMS_SOCKET_TRACE PMS_SHOW */
#define PMS_SOCKET_TRACE(x, ...)

#if defined(UNIX) || defined(MACOSX)
typedef int SOCKET;
#endif

#ifdef VXWORKS
typedef int SOCKET;
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#if defined(UNIX) || defined(MACOSX)
#define closesocket close
#endif

#ifdef VXWORKS
#define closesocket close
#endif

int sockopen(char *host, int port);
int sockinfo(int sock, char *info);
int sockclose(int sock);
void socklisten(int sock, void (*handler) (void *));
void sockerror(char *msg);

typedef struct tagPMSSocketInputBuffer{
  unsigned char *buffer;   /* buffer */
  int in;                  /* position in array to next data in */
  int out;                 /* position in array to to next data out */
  int nValidBytes;         /* number of bytes available to read */
  int nNoMoreData;         /* All data has been received... 
                              The last recv failed or returned no data
                              due to socket being closed. */
  int bEndOfJobReceived;   /* Client has closed the sending end. */
  void *pOpenInputThread;  /* Job connection thread */
  int bWaitingForAccept;   /* True if this socket is waiting for a connection */
} PMS_TySocketInputBuffer;

typedef struct tagPMSSocket{
  SOCKET hSocketData;      /* handle to the socket */
  SOCKET hSocketServer;    /* handle to the server socket */

  PMS_TySocketInputBuffer tSocketInput; 

  void * semOutput;        /* Semaphore for Socket_Send */

  struct tagPMSSocket * pNext;
} PMS_TySocket;

PMS_TySocket * l_pListSockets = NULL;

int l_nSocketRefCount = 0;
#ifdef WIN32
int l_bWSAInit = 0;
#endif

int g_nError=0;

extern int g_cbReceiveBuffer; /* for file and socket input buffer */
extern int g_bStoreJobBeforeRip; /* store whole job before passing to rip */

char g_szThisHostIPAddress[64]="";

void * l_pWaitForConnectionThread = NULL;



/**
 * \brief Initialise the socket module.
 *
 * Prepares the socket subsystem use sockets.\n
 */
int Socket_Initialize()
{
  PMS_SOCKET_TRACE("Socket_Initialize()\n");

  g_nError = 0;

#ifdef WIN32
  if(!l_bWSAInit)
  {
    WORD    wVersionRequired;
    WSADATA wsaData;

    wVersionRequired = MAKEWORD( 1, 1 );
    if( WSAStartup( wVersionRequired, &wsaData ) == 0 )
    {
      if( LOBYTE( wsaData.wVersion ) == 1 && HIBYTE( wsaData.wVersion ) == 1 )
      {
      }
      else
      {
        g_nError = WSAGetLastError();
        WSACleanup();
        return FALSE;
      }
    }
    else
    {
      g_nError = WSAGetLastError();
      return FALSE;
    }
    l_bWSAInit = 1;
  }
#endif

  Socket_GetHostAddress(&g_szThisHostIPAddress[0], sizeof(g_szThisHostIPAddress));

  return TRUE;
}

/**
 * \brief Finialize the socket module.
 *
 * Cleanup the socket subsystem.\n
 */
int Socket_Finalize()
{
  PMS_SOCKET_TRACE("Socket_Finalize()\n");
  PMS_ASSERT(l_nSocketRefCount==0, ("Socket_Finalize: There are %d sockets still open?\n", l_nSocketRefCount));

#ifdef WIN32
  if ( WSACleanup() != 0 ) 
  {
    g_nError = WSAGetLastError();
  }
#endif

  return g_nError;
}

static PMS_TySocket * AddSocket()
{
  PMS_TySocket * pNewSocket = NULL;
  PMS_TySocket * pSocket;
  PMS_SOCKET_TRACE("AddSocket()\n");

  pNewSocket = OSMalloc(sizeof(PMS_TySocket), PMS_MemoryPoolPMS);
  if(!pNewSocket)
    return NULL;
  memset(pNewSocket, 0x00, sizeof(PMS_TySocket));
  pNewSocket->hSocketData = INVALID_SOCKET;
  pNewSocket->hSocketServer = INVALID_SOCKET;

  pNewSocket->tSocketInput.buffer = OSMalloc(g_tSystemInfo.cbReceiveBuffer, PMS_MemoryPoolPMS);
  if(!pNewSocket->tSocketInput.buffer)
  {
    OSFree(pNewSocket, PMS_MemoryPoolPMS);
    return NULL;
  }

  pNewSocket->semOutput = PMS_CreateSemaphore(TRUE);

  if(l_pListSockets)
  {
    for(pSocket = l_pListSockets; pSocket->pNext; pSocket=pSocket->pNext);

    pSocket->pNext = pNewSocket;
  }
  else
  {
    l_pListSockets = pNewSocket;
  }
  
  return pNewSocket;
}

static int RemoveSocket(PMS_TySocket * pSocketToRemove)
{
  PMS_TySocket * pSocket;
  PMS_TySocket * pPrevSocket = NULL;
  PMS_SOCKET_TRACE("RemoveSocket()\n");

  for(pSocket = l_pListSockets; pSocket; pSocket=pSocket->pNext)
  {
    if(pSocket==pSocketToRemove)
    {
      if(pPrevSocket)
      {
        pPrevSocket->pNext = pSocket->pNext;
      }
      if(pSocket->tSocketInput.buffer)
      {
        OSFree(pSocket->tSocketInput.buffer, PMS_MemoryPoolPMS);
      }
      PMS_DestroySemaphore(pSocket->semOutput);
      OSFree(pSocket, PMS_MemoryPoolPMS);
      return TRUE;
    }
    pPrevSocket = pSocket;
  }

  return FALSE;
}

/* Socket - Server only */
/**
 * \brief Initialise the socket server.
 *
 */
PMS_SOCKET_HANDLE Socket_InitServer(int nPort)
{
  struct sockaddr_in addr;
/*  int setopt = 1; */
  PMS_TySocket *pNewSocket; 

  PMS_SOCKET_TRACE("Socket_InitServer()\n");

  pNewSocket = AddSocket();
  if(!pNewSocket)
  {
    g_nError = 0;
    return NULL;
  }

  /* Create socket */
  if(( pNewSocket->hSocketServer = socket( PF_INET, SOCK_STREAM, 0 )) != INVALID_SOCKET )
  {
    /* Bind to specified port */
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
#ifdef THREADX
    addr.sin_port = htons( nPort );
#else
    addr.sin_port = htons( (unsigned short) nPort );
#endif
    addr.sin_addr.s_addr = htonl( INADDR_ANY );
#ifdef VXWORKS
    addr.sin_len = sizeof(addr);
#endif

/*
if(setsockopt( pNewSocket->hSocketServer, SOL_SOCKET, SO_REUSEADDR, (char*)&setopt, sizeof(setopt)))
    {
#ifdef WIN32
      g_nError = WSAGetLastError() ;
#else
      g_nError = errno;
#endif
      closesocket( pNewSocket->hSocketServer );
      return NULL;
    }
*/

    if( bind( pNewSocket->hSocketServer, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 )
    {
#ifdef WIN32
      g_nError = WSAGetLastError() ;
#else
      g_nError = errno;
#endif
      closesocket( pNewSocket->hSocketServer );
      return NULL;
    }

#ifdef MACOSX
    {
      int flag = 1;

      if ( setsockopt( pNewSocket->hSocketServer, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag) ) != 0 )
      {
        g_nError = errno;
        closesocket( pNewSocket->hSocketServer );
        return NULL;
      }
    }
#endif
  }
  else
  {
#ifdef WIN32
    g_nError = WSAGetLastError() ;
#else
    g_nError = errno;
#endif
    return NULL;
  }

  l_nSocketRefCount++;

  return ((PMS_SOCKET_HANDLE)pNewSocket);
}


/**
 * \brief Create a listening server socket.
 *
 */
int Socket_WaitForConnection(PMS_SOCKET_HANDLE hPMSSocket)
{
  unsigned long arg;
  struct sockaddr sockaddr;
#if defined(VXWORKS) || defined(THREADX)
  int addrlen = sizeof(sockaddr);
#else
  socklen_t addrlen = sizeof(sockaddr);
#endif
  PMS_TySocket *pSocket = (PMS_TySocket *)hPMSSocket;

  PMS_SOCKET_TRACE("Socket_WaitForConnection()\n");
  PMS_ASSERT(pSocket, ("Socket_WaitForConnection: Socket context NULL\n"));
  PMS_ASSERT(pSocket->hSocketServer, ("Socket_WaitForConnection: Data socket NULL\n"));

  memset(&sockaddr, 0, sizeof(sockaddr));
  pSocket->hSocketData = accept(pSocket->hSocketServer, &sockaddr, &addrlen);

  if (pSocket->hSocketData == INVALID_SOCKET) 
  {
#ifdef WIN32
    g_nError = WSAGetLastError() ;
#else
    g_nError = errno;
#endif
    PMS_SHOW_ERROR("Socket accept failed %d\n", g_nError);
    return g_nError;
  }

  /* Switch the socket to non-blocking mode */
  arg = TRUE;  /* true for non-blocking */
#if defined(WIN32) || defined(THREADX)
  if (ioctlsocket( pSocket->hSocketData, FIONBIO, &arg) != 0)
#else
  if (ioctl( (int)pSocket->hSocketData, (int)FIONBIO, (int)&arg) != 0)
#endif
  {
#ifdef WIN32
    g_nError = WSAGetLastError() ;
#else
    g_nError = errno;
#endif
    PMS_SHOW_ERROR("Socket ioctlsocket failed %d\n", g_nError);
    PMS_EnterCriticalSection(g_csSocketInput);
    closesocket( pSocket->hSocketData );
    pSocket->hSocketData = INVALID_SOCKET;
    PMS_LeaveCriticalSection(g_csSocketInput);
    return -1;
  }

#ifdef THREADX
  {
    int iOptVal = 1;
    if (setsockopt(pSocket->hSocketData, SOL_SOCKET, SO_REUSESOCK, (RTP_PFCCHAR)&iOptVal, sizeof(int)) ) 
    {
      g_nError = errno;
      PMS_SHOW_ERROR("Socket setsockopt error %d.\n", g_nError) ;
      PMS_EnterCriticalSection(g_csSocketInput);
      closesocket(pSocket->hSocketData) ;
      pSocket->hSocketData = INVALID_SOCKET;
      PMS_LeaveCriticalSection(g_csSocketInput);
      return -1;
    }
  }
#endif
#ifdef MACOSX
  {
    int flag = 1;

    if ( setsockopt( pSocket->hSocketData, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag) ) != 0 )
    {
      g_nError = errno;
      PMS_SHOW_ERROR("Socket setsockopt failed %d\n", g_nError);
      PMS_EnterCriticalSection(g_csSocketInput);
      closesocket( pSocket->hSocketData );
      pSocket->hSocketData = INVALID_SOCKET;
      PMS_LeaveCriticalSection(g_csSocketInput);
      return -1;
    }
  }
#endif

  pSocket->tSocketInput.bWaitingForAccept = 0;

  return 0;
}

/**
 * \brief Wrapper function for wait for connection thread.
 *
 */
void Socket_WaitForConnectionWrapper(void * args)
{
  int nResult;
  PMS_SOCKET_TRACE("Socket_WaitForConnectionWrapper()\n");

  nResult = Socket_WaitForConnection((PMS_SOCKET_HANDLE)args);

  PMS_ASSERT(nResult==0, ("Socket_WaitForConnection failed %d\n", nResult));
}

/* Socket - Client only */
/**
 * \brief Create a client socket.
 *
 */
PMS_SOCKET_HANDLE Socket_InitConnect(unsigned char *pszAddress, int nPort)
{
  struct sockaddr_in addr;
  PMS_TySocket *pNewSocket;

  PMS_SOCKET_TRACE("Socket_InitConnect()\n");

  pNewSocket = AddSocket();
  if(!pNewSocket)
  {
    g_nError = 0;
    return NULL;
  }

  /* Create socket */
  if(( pNewSocket->hSocketData = socket( PF_INET, SOCK_STREAM, 0 )) != INVALID_SOCKET )
  {

    /* Bind to specified port and address */
    addr.sin_family = AF_INET;
#ifdef THREADX
    addr.sin_port = htons( nPort );
#else
    addr.sin_port = htons( (unsigned short) nPort );
#endif
    addr.sin_addr.s_addr = inet_addr( (char*) pszAddress );

    if ( connect( pNewSocket->hSocketData, (struct sockaddr *) &addr, sizeof(addr) ) != 0)
    {
#ifdef WIN32
      g_nError = WSAGetLastError() ;
#else
      g_nError = errno;
#endif
      PMS_EnterCriticalSection(g_csSocketInput);
      closesocket( pNewSocket->hSocketData );
      pNewSocket->hSocketData = INVALID_SOCKET;
      PMS_LeaveCriticalSection(g_csSocketInput);
    }
  }
  else
  {
#ifdef WIN32
    g_nError = WSAGetLastError() ;
#else
    g_nError = errno;
#endif
  }

  l_nSocketRefCount++;

  return ((PMS_SOCKET_HANDLE)pNewSocket);
}


/* Socket - Server and Client */
/**
 * \brief Close socket.
 *
 * Shutdown socket connection.\n
 * Server and client sockets.
 */
int Socket_Close(PMS_SOCKET_HANDLE hPMSSocket)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;

  PMS_SOCKET_TRACE("Socket_Close()\n");
  PMS_ASSERT(pSocket, ("Socket_Close: Socket context NULL\n"));
  PMS_ASSERT(pSocket->hSocketData, ("Socket_Close: Data socket NULL\n"));

  l_nSocketRefCount--;

  PMS_EnterCriticalSection(g_csSocketInput);
  if(pSocket->hSocketData != INVALID_SOCKET)
  {
    if( closesocket( pSocket->hSocketData ) == SOCKET_ERROR )
    {
#ifdef WIN32
      g_nError = WSAGetLastError() ;
#else
      g_nError = errno;
#endif
    }
    pSocket->hSocketData = INVALID_SOCKET;
  }

  if(pSocket->hSocketServer != INVALID_SOCKET)
  {
    if( closesocket( pSocket->hSocketServer ) == SOCKET_ERROR )
    {
#ifdef WIN32
      g_nError = WSAGetLastError() ;
#else
      g_nError = errno;
#endif
    }
    pSocket->hSocketServer = INVALID_SOCKET;
  }
  PMS_LeaveCriticalSection(g_csSocketInput);

  RemoveSocket(pSocket);

  return 0;
}

/**
 * \brief Send data on socket.
 *
 */
int Socket_Send(PMS_SOCKET_HANDLE hPMSSocket, unsigned char *pBuffer,
                int nBytesToSend, int *pnBytesSent)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;
  int nError;
  int nBytesSent = 0;
  int nTotalBytesSent = 0;
  int bTimedOut = 0;

  PMS_SOCKET_TRACE("Socket_Send()\n");
  PMS_ASSERT(pSocket, ("Socket_Send: Socket context NULL\n"));
  PMS_ASSERT(pSocket->hSocketData, ("Socket_Send: Data socket NULL\n"));
  PMS_ASSERT(pBuffer, ("Socket_Send: Send buffer is NULL\n"));

  /* if not bidirectional socket */
  if(!g_bBiDirectionalSocket)
  {
    *pnBytesSent = nBytesToSend;
    return 0;
  }
  if(pSocket->hSocketData == INVALID_SOCKET)
  {
    /* Socket is invalid... probably been closed, so no point in sending... just lie to caller instead */
    PMS_SOCKET_TRACE("Socket_Send()... socket invalid (closed) ignoring %d bytes to send.\n", nBytesToSend);
    *pnBytesSent = nBytesToSend;
    return 0;
  }

  if(!PMS_WaitOnSemaphore(pSocket->semOutput, 240 * 1000)) {
    PMS_FAIL("Failed to obtain semaphore in a timely fashion.\n");
    /* just carry on as if all was ok... send error handling will
       either recover are abort the job */
    bTimedOut = 1;
  }

  do{
    /* Some systems use SO_NOSIGPIPE instead of MSG_NOSIGNAL */
#if defined(MACOSX)
    nBytesSent = send( pSocket->hSocketData, (char *)pBuffer + nTotalBytesSent, nBytesToSend - nTotalBytesSent, 0 );
#elif defined(UNIX)
    nBytesSent = send( pSocket->hSocketData, (char *)pBuffer + nTotalBytesSent, nBytesToSend - nTotalBytesSent, MSG_NOSIGNAL );
#else
    nBytesSent = send( pSocket->hSocketData, (char *)pBuffer + nTotalBytesSent, nBytesToSend - nTotalBytesSent, 0 );
#endif

    if( nBytesSent == SOCKET_ERROR )
    {
#ifdef WIN32
      nError = WSAGetLastError() ;
#else
      nError = errno;
#endif
      switch(nError)
      {
#ifdef WIN32
      case WSAEWOULDBLOCK:
#else
      case EWOULDBLOCK:
#endif
#ifdef THREADX
      case 0:
#endif
        PMS_RelinquishTimeSlice();
        break;
      default:
        *pnBytesSent = nTotalBytesSent;
        PMS_SHOW_ERROR("Socket send error %d. Attempted to send %d bytes. Previously sent %d bytes\n", 
          nError, nBytesToSend - nTotalBytesSent, nTotalBytesSent);
        /* Increment the semaphore before we leave */
        if(!bTimedOut) {
          PMS_IncrementSemaphore(pSocket->semOutput);
        }
        return nError;
        break;
      }
    }
    else
    {
      nTotalBytesSent += nBytesSent;
    }
  } while(nTotalBytesSent < nBytesToSend);

  *pnBytesSent = nTotalBytesSent;

  /* Other threads can now call Socket_Send */
  if(!bTimedOut) {
    PMS_IncrementSemaphore(pSocket->semOutput);
  }

  return 0;
}

/**
 * \brief Receive data from socket.
 *
 */
int Socket_Receive(PMS_SOCKET_HANDLE hPMSSocket, unsigned char *pBuffer,
                   int nBytesToRead, int *pnBytesRead)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;
  int nError;
  int nResult;

  PMS_SOCKET_TRACE("Socket_Receive(), %d\n", nBytesToRead);
  PMS_ASSERT(pSocket, ("Socket_Receive: Socket context NULL\n"));
  PMS_ASSERT(pSocket->hSocketData, ("Socket_Receive: Data socket NULL\n"));
  PMS_ASSERT(pSocket->hSocketData!=INVALID_SOCKET, ("Socket_Receive: Data socket invalid\n"));
  PMS_ASSERT(pBuffer, ("Socket_Receive: Receive buffer is NULL\n"));

  nResult = recv( pSocket->hSocketData, (char *) pBuffer, nBytesToRead, 0 );
  if(nResult==SOCKET_ERROR)
  {
#ifdef WIN32
    nError = WSAGetLastError() ;
#else
    nError = errno;
#endif

    switch(nError)
    {
#ifdef WIN32
    case WSAEWOULDBLOCK:
#else
    case EWOULDBLOCK:
#endif
#ifdef THREADX
    case 0:
#endif
      *pnBytesRead = 0;
/*      PMS_SOCKET_TRACE("Socket_Receive()... %d, would block.\n", nError); */
      return 0;
      break;
#ifndef WIN32
     case ESHUTDOWN:
       nResult = 0;
       break;
#endif
    default:
      *pnBytesRead = 0;
      PMS_SHOW_ERROR("Socket receive error %d.\n", nError);
      return nError;
    }
  }

  *pnBytesRead = nResult;

  /* recv returned no bytes and no error... socket has been closed from client end. */
  if(*pnBytesRead == 0)
  {
    PMS_SOCKET_TRACE("Socket_Receive()... End of data input stream detected.\n");
    pSocket->tSocketInput.bEndOfJobReceived = 1;
  }

  return 0;
}

/**
 * \brief Get host server address.
 *
 * Prepares the socket subsystem to accecpt incoming data.\n
 */
int Socket_GetHostAddress(char *pszAddress, int cbSize)
{
#ifdef THREADX
  /* \todo Implement if needed */
#else
  char szHostName[260];
#ifdef VXWORKS
  int ipaddr;
#else
  struct hostent* pheThisHost;
#endif  /* VXWORKS */
#endif  /* THREADX */

  PMS_SOCKET_TRACE("Socket_GetHostAddress()\n");

  if(cbSize<14)
    return 1;

#ifdef THREADX
  /* \todo Get ip address of this machine if this call needs to be implemented */
  return 1;
#else /* not THREADX */
  pszAddress[0]='\0';
  if (gethostname(szHostName,sizeof(szHostName)-1)==0)
  {   
#ifdef VXWORKS
    ipaddr = hostGetByName(szHostName);
    return 0;
#else
    pheThisHost= gethostbyname(szHostName);
    if (pheThisHost)
    {
      sprintf(pszAddress, "%u.%u.%u.%u",
        pheThisHost->h_addr[0]&0xFF,
        pheThisHost->h_addr[1]&0xFF,
        pheThisHost->h_addr[2]&0xFF,
        pheThisHost->h_addr[3]&0xFF);
      return 0;
    }
#endif  /* VXWORKS */
  }
#endif  /* THREADX */
  return 1;
}


void Socket_Input_Handler(void *pParam)
{
  static int nTotalRecv = 0;
  int BytesRead=0, BytesToRead;
  int nResult = 0;
  PMS_SOCKET_HANDLE hPMSSocket = (PMS_SOCKET_HANDLE)pParam;
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;
  int bTryAgain = 1;

  PMS_SOCKET_TRACE("Socket_Input_Handler()\n");
  PMS_ASSERT(pSocket, ("Socket_Input_Handler: Socket not initialised\n"));

  do {
    PMS_EnterCriticalSection(g_csSocketInput);

    /* check there is space in the buffer to read into */
    if (pSocket->tSocketInput.nValidBytes < g_tSystemInfo.cbReceiveBuffer)
    {
      /* there is space, determine how much */
      if ((pSocket->tSocketInput.out > pSocket->tSocketInput.in))
      {
        /* no wrap around while filling */
        BytesToRead = pSocket->tSocketInput.out - pSocket->tSocketInput.in;
      }
      else
      {
        /* wrap around so fill to end of buffer */
        BytesToRead = g_tSystemInfo.cbReceiveBuffer - pSocket->tSocketInput.in ;
      }

      /* now read the data */
      BytesRead = 0;
      if(pSocket->tSocketInput.bEndOfJobReceived)
      {
        pSocket->tSocketInput.nNoMoreData = 1;
      }
      else
      {
        nResult = Socket_Receive(hPMSSocket, pSocket->tSocketInput.buffer + pSocket->tSocketInput.in, BytesToRead, &BytesRead);
        PMS_ASSERT(nResult==0, ("Socket_Input_Handler: Receive failed %d\n", nResult));

        if(nResult==0)
        {
          /* update pointer */
          pSocket->tSocketInput.in += BytesRead;

          pSocket->tSocketInput.nValidBytes += BytesRead;

          if(BytesRead)
          {
            nTotalRecv += BytesRead; 
          }

          /* if reached end of buffer point back to start */
          if (pSocket->tSocketInput.in == g_tSystemInfo.cbReceiveBuffer)
          {
            pSocket->tSocketInput.in = 0;
          }
        }
      }
    }
    else
    {
      /* buffer full, do not read */
    }

    if(nResult || pSocket->tSocketInput.nNoMoreData)
      bTryAgain = 0;
      
    PMS_LeaveCriticalSection(g_csSocketInput);
 
    PMS_RelinquishTimeSlice();

  } while (bTryAgain);

}

/**
 * \brief Listens for a socket connection.
 *
 * Listens on the port for a socket connection.
 */
int Socket_Listen(PMS_SOCKET_HANDLE hPMSSocket)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;
  PMS_SOCKET_TRACE("Socket_Listen()\n");

  if (listen(pSocket->hSocketServer, 5) == -1) 
  {
#ifdef WIN32
    g_nError = WSAGetLastError() ;
#else
    g_nError = errno;
#endif
    PMS_SHOW_ERROR("Socket listen failed %d\n", g_nError);
    return g_nError;
  }

  return TRUE;
}

/**
 * \brief Wait for a socket connection.
 *
 * Wait for an incoming socket connection.
 */
int Socket_OpenInDataStream(PMS_SOCKET_HANDLE hPMSSocket)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;

  PMS_SOCKET_TRACE("Socket_OpenInDataStream()\n");
  PMS_ASSERT(pSocket, ("Socket_OpenInDataStream: Socket not initialised\n"));
  PMS_ASSERT((pSocket->tSocketInput.in == pSocket->tSocketInput.out), ("Socket_OpenInDataStream: Socket buffer not empty from previous job\n"));

  if(!l_pWaitForConnectionThread) {
    /* Get ready for data on a new socket connection */
    pSocket->tSocketInput.bWaitingForAccept = 1;
    pSocket->tSocketInput.nNoMoreData = 0;
    pSocket->tSocketInput.bEndOfJobReceived = 0;
    pSocket->tSocketInput.in = 0;
    pSocket->tSocketInput.out = 0;
    pSocket->tSocketInput.nValidBytes = 0;
    pSocket->tSocketInput.pOpenInputThread = NULL;

    /* Kick off a seperate thread for the blocking accept call. This gives us
       the opportunity to cancel the accept call if we want to. */
    l_pWaitForConnectionThread = PMS_BeginThread(Socket_WaitForConnectionWrapper, 0, (void*)hPMSSocket);
  }

  if(pSocket->tSocketInput.bWaitingForAccept) {
    return 0; /* 0 means try again later */
  } /* otherwise we've got a connection */

  /* Wait for the thread that contained the blocking accept call to finish.
     Should be almost immediately.  */
  PMS_CloseThread(l_pWaitForConnectionThread, 1000);
  l_pWaitForConnectionThread = NULL;

  /* Kick off a seperate thread for receiving job data. */
  pSocket->tSocketInput.pOpenInputThread = PMS_BeginThread(Socket_Input_Handler, 0, (void*)hPMSSocket);
  if(!pSocket->tSocketInput.pOpenInputThread)
    return FALSE;

  /* if and only if we're storing the complete job before passing to the rip */
  if(g_tSystemInfo.nStoreJobBeforeRip)
  {
    PMS_TySocket *pSocket = ((PMS_TySocket*)hPMSSocket);
    PMS_SHOW("Storing job before passing to rip...\n");
    while( (pSocket->tSocketInput.nNoMoreData == 0) &&
           (pSocket->tSocketInput.nValidBytes < g_tSystemInfo.cbReceiveBuffer) )
    {
      PMS_RelinquishTimeSlice();
    }
    PMS_ASSERT(pSocket->tSocketInput.nNoMoreData == 1, ("Receive buffer filled before end of job. Increase receive job and try again.\n"));
    PMS_SHOW("Stored %d bytes.\n", pSocket->tSocketInput.nValidBytes);
  }

  return TRUE;
}

/**
 * \brief Close the input socket.
 *
 * Closes the socket that received the job.
 * Marks the receive buffer as empty.
 */
int Socket_CloseInDataStream(PMS_SOCKET_HANDLE hPMSSocket)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;

  PMS_SOCKET_TRACE("Socket_CloseInDataStream()\n");

  PMS_EnterCriticalSection(g_csSocketInput);
  closesocket( pSocket->hSocketData );
  pSocket->hSocketData = INVALID_SOCKET;
  PMS_LeaveCriticalSection(g_csSocketInput);

  /* The thread should close itself, but we will force it 
     closed if it doesn't close in a timely fashion. */
  PMS_CloseThread(pSocket->tSocketInput.pOpenInputThread, 5000);

  return TRUE;
}

/**
 * \brief Pass read data to PMS.
 *
 * This routine reads the requested number of bytes from the socket buffer and puts them
 * in the callers buffer.\n
 * However, it should keep the bytes for future peeks and only throw it away when consume data
 * is called.\n
 * It is an implementation of PMS_PeekDataStream callback. \n
 */
int Socket_PeekInDataStream(PMS_SOCKET_HANDLE hPMSSocket, unsigned char * buffer, int nBytesToRead)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;
  int nBytesRead = 0;
  int nValidBytes = 0;
  int bTryAgain = 1;

  PMS_ASSERT(pSocket, ("Socket_PeekInDataStream: Socket not initialised\n"));

  /* is there data to read */
  do
  {
    /* Don't let the receiving thread change the socket inpuut structure whilst
       it is in use here */
    PMS_EnterCriticalSection(g_csSocketInput);

    if (pSocket->tSocketInput.nValidBytes > 0)
    {
      /* does it wrap around */
      if (pSocket->tSocketInput.in == 0)
      {
        /* can read to end of buffer */
        nValidBytes = g_tSystemInfo.cbReceiveBuffer - pSocket->tSocketInput.out;
      }
      else if ((pSocket->tSocketInput.in > pSocket->tSocketInput.out))
      {
        /* no wrap around so read as much as possible */
        nValidBytes = pSocket->tSocketInput.nValidBytes;
      }
      else
      {
        /* wrap around so only read to end of buffer */
        nValidBytes = g_tSystemInfo.cbReceiveBuffer - pSocket->tSocketInput.out;
      }
      /* do not read more than has been requested */
      if (nValidBytes > nBytesToRead)
      {
        /* request is larger than available data - so cap it */
        nBytesRead = nBytesToRead;
      }
      else
      {
        nBytesRead = nValidBytes;
      }
      memcpy(buffer, (pSocket->tSocketInput.buffer)+pSocket->tSocketInput.out, nBytesRead);
    }

    if(nBytesRead || pSocket->tSocketInput.nNoMoreData)
      bTryAgain = 0;
      
    PMS_LeaveCriticalSection(g_csSocketInput);

    PMS_RelinquishTimeSlice();

    /* We block here until there is no more data or we have some data to return */
  } while (bTryAgain);

  PMS_SOCKET_TRACE("Socket_PeekInDataStream() returning %d bytes\n", nBytesRead);

  return (nBytesRead);
}

/**
 * \brief Pass read data to PMS.
 *
 * This routine reads the requested number of bytes from the socket buffer and puts them
 * in the callers buffer.\n
 * It is an implementation of PMS_ReadDataStream callback. \n
 */
int Socket_ConsumeInDataStream(PMS_SOCKET_HANDLE hPMSSocket, int nBytesToConsume)
{
  PMS_TySocket *pSocket = (PMS_TySocket*)hPMSSocket;
  UNUSED_PARAM((PMS_SOCKET_HANDLE), hPMSSocket);

  PMS_SOCKET_TRACE("Socket_ConsumeInDataStream(%d)\n", nBytesToConsume);

  /* Don't let the receiving thread change the socket input structure 
     whilst we need to use it */
  PMS_EnterCriticalSection(g_csSocketInput);

  pSocket->tSocketInput.out += nBytesToConsume;

  if (pSocket->tSocketInput.out >= g_tSystemInfo.cbReceiveBuffer)
  {
    pSocket->tSocketInput.out = pSocket->tSocketInput.out - g_tSystemInfo.cbReceiveBuffer;
  }

  /* If this assert ever fires, then it's possible that data was 
     already thrown away before it was consumed be this function. */
  PMS_ASSERT(pSocket->tSocketInput.nValidBytes >= nBytesToConsume,
    ("Trying to consume more bytes (%d bytes) than were stored in the buffer (%d bytes).",
    nBytesToConsume, pSocket->tSocketInput.nValidBytes ));

  pSocket->tSocketInput.nValidBytes -= nBytesToConsume;
  if(pSocket->tSocketInput.nValidBytes < 0)
  {
    pSocket->tSocketInput.nValidBytes = 0;
  }

  PMS_LeaveCriticalSection(g_csSocketInput);

  return (nBytesToConsume);
}

