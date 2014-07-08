/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_file_in.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for PMS file input.
 *
 */

#ifndef __PMS_FILE_IN_H__
#define __PMS_FILE_IN_H__

/* globals */
/*! \brief Holds name of the job(full path).*/
extern char szJobFilename[256];

/* function declarations */
int File_InitDataStream(void);
int File_OpenDataStream(void);
int File_CloseDataStream(void);
int File_PeekDataStream(unsigned char * buffer, int nBytesToRead);
int File_ConsumeDataStream(int nBytesToConsume);
int File_ReadDataStream(unsigned char * buffer, int nBytesToRead);
int File_Open( char * pzPath, char * flags, void ** pHandle );
int File_Read(unsigned char * buffer, int nBytesToRead, void * handle);
int File_Seek(void * handle, long *pPosition, int nWhence);
int File_Bytes(void * handle);
int File_Close(void * handle);


#endif /* __PMS_FILE_IN_H__ */
