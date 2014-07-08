/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_socket.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for socket support functions.
 *
 */

#ifndef _PMS_SOCKET_H_
#define _PMS_SOCKET_H_

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef void* PMS_SOCKET_HANDLE ;

/* Module */
int Socket_Initialize();
int Socket_Finalize();
int Socket_GetHostAddress(char *pszAddress, int cbSize);

/* Socket - Server and Client */
int Socket_Close(PMS_SOCKET_HANDLE hPMSSocket);
int Socket_Send(PMS_SOCKET_HANDLE hPMSSocket, unsigned char *pBuffer,
                int nBytesToSend, int *pnBytesRead);
int Socket_Receive(PMS_SOCKET_HANDLE hPMSSocket, unsigned char *pBuffer,
                   int nBytesToRead, int *pnBytesRead);

/* Socket - Server only */
PMS_SOCKET_HANDLE Socket_InitServer(int nPort);
int Socket_Listen(PMS_SOCKET_HANDLE hPMSSocket);

/* Socket - Client only */
PMS_SOCKET_HANDLE Socket_InitConnect(unsigned char *pszAddress, int nPort);
int Socket_OpenInDataStream(PMS_SOCKET_HANDLE hPMSSocket);
int Socket_PeekInDataStream(PMS_SOCKET_HANDLE hPMSSocket, unsigned char * buffer, int nBytesToRead);
int Socket_ConsumeInDataStream(PMS_SOCKET_HANDLE hPMSSocket, int nBytesToConsume);
int Socket_CloseInDataStream(PMS_SOCKET_HANDLE hPMSSocket);


#endif /* _PMS_SOCKET_H_ */

