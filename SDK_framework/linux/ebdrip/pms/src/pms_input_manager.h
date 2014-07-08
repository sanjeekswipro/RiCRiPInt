/* Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_input_manager.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for input channel management functions.
 *
 */

#ifndef _PMS_INPUT_MANAGER_H_
#define _PMS_INPUT_MANAGER_H_

/* Module */
int PMS_IM_Initialize();
void PMS_IM_Finalize();

int PMS_IM_WaitForInput();
int PMS_IM_CloseActiveDataStream(void);
int PMS_IM_PeekActiveDataStream(unsigned char * buffer, int nBytesToRead);
int PMS_IM_ConsumeActiveDataStream(int nBytesToConsume);
int PMS_IM_WriteToActiveDataStream(unsigned char * pBuffer, int nBytesToWrite, int * pnBytesWritten);



#endif /* _PMS_INPUT_MANAGER_H_ */

