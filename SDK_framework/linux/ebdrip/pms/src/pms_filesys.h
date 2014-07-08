/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_filesys.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for PMS file system.
 *
 */

#ifndef __PMS_FILESYS_H__
#define __PMS_FILESYS_H__

#include "pms.h"

/* function declarations */
extern void PMS_FS_InitFS(void);
extern void PMS_FS_ShutdownFS(void);

extern int PMS_FS_InitVolume(unsigned char * pzVolume);
extern int PMS_FS_MakeDir(unsigned char * pzPath);
extern int PMS_FS_Open(unsigned char * pzPath, int flags, void ** pHandle);
extern int PMS_FS_Read(void * handle, unsigned char * buffer, int bytes, int * pcbRead );
extern int PMS_FS_Write(void * handle, unsigned char * buffer, int bytes, int * pcbWritten );
extern int PMS_FS_Close(void * handle);
extern int PMS_FS_Seek(void * handle, int offset, int whence);
extern int PMS_FS_Delete(unsigned char * pzName);
extern int PMS_FS_DirEntryStat(unsigned char * pzDir, int nEntry, PMS_TyStat * pStat);
extern int PMS_FS_Stat(unsigned char * pzName, PMS_TyStat * pStat);
extern int PMS_FS_FileSystemInfo(int iVolume, int * pnVolumes, PMS_TyFileSystem * pFileSystemInfo);
extern int PMS_FS_GetDisklock(void);
extern void PMS_FS_SetDisklock(int fLocked); 



#endif /* __PMS_FILESYS_H__ */
