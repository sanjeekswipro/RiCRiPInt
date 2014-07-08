/* Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_input_socket.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for input channel management functions.
 *
 */

#ifndef _PMS_INPUT_SOCKET_H_
#define _PMS_INPUT_SOCKET_H_

/* Module */
int Input_Socket_Initialize();
void Input_Socket_Finalize();

int Input_Socket_InitDataStream(void);
int Input_Socket_OpenDataStream(void);
int Input_Socket_CloseDataStream(void);
int Input_Socket_PeekDataStream(unsigned char * buffer, int nBytesToRead);
int Input_Socket_ConsumeDataStream(int nBytesToConsume);
int Input_Socket_WriteDataStream(unsigned char *pBuffer, int nBytesToWrite, int *pnBytesWritten);


#endif /* _PMS_INPUT_SOCKET_H_ */

