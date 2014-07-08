/* Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_input_hotfolder.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for hot folder input.
 *
 */

#ifndef _PMS_INPUT_HOTFOLDER_H_
#define _PMS_INPUT_HOTFOLDER_H_

/* Module */

void HotFolder_Initialize(void);
void HotFolder_Finalize(void);

int HotFolder_InitDataStream(void);
int HotFolder_OpenDataStream(void);
int HotFolder_PeekDataStream(unsigned char * buffer, int nBytesToRead);
int HotFolder_ConsumeDataStream(int nBytesToConsume);
int HotFolder_WriteDataStream(unsigned char *pBuffer, int nBytesToWrite, int *pnBytesWritten);
int HotFolder_CloseDataStream(void);



#endif /* _PMS_INPUT_HOTFOLDER_H_ */

