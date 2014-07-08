/* Copyright (c) 2008-2012 Global Graphics Software Ltd. All Rights Reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_virtualfile.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*! \file
 *  \ingroup OIL
 *  \brief OIL This header file contains the OIL virtual file interface.
 *
 *  The interface described by this file provides a way for the OIL to 
 *  store a "virtual file" in RAM.
 */

#ifndef __OIL_VIRTUALFILE_H__
#define __OIL_VIRTUALFILE_H__

#include "skinkit.h"

int OIL_VirtFileOpen(char *pszFilename, int nOpenFlags);
int OIL_VirtFileClose(int nFileID);
int OIL_VirtFileDelete(char *pszFilename);
int OIL_VirtFileStatus(char *pszFilename, STAT *pStat);
int OIL_VirtFileRead(int nFileID, unsigned char *pBuff, int nLength);
int OIL_VirtFileWrite(int nFileID, unsigned char *pBuff, int nLength);
int OIL_VirtFileSeek(int nFileID, Hq32x2 *pDestination, int nWhence);
int OIL_VirtFileBytes(int nFileID, Hq32x2 *pBytes, int nReason);
int OIL_VirtFileSendToOutput(int nFileID);
int OIL_VirtFileCleanup();


#endif /* __OIL_VIRTUALFILE_H__ */
