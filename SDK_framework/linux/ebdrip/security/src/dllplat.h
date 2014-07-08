/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __DLLPLAT_H__
#define __DLLPLAT_H__

/*
 * $HopeName: SWsecurity!src:dllplat.h(EBDSDK_P.1) $
 *
* Log stripped */

/* ----------------------- Macros ------------------------------------------ */

/* Obfuscation defines */
#define GetDayNo          writeLogFile
#define WriteLicence      accessInfo
#define ReadLicence       incCounters
#define FindSecurityTag   cacheTest
#define CreateSecurityTag HCMSInitPackage

/* ----------------------- Functions --------------------------------------- */

extern uint16 GetDayNo();

extern int32  WriteLicence( uint8 * buffer, int32 count );
extern int32  ReadLicence( uint8 * buffer, int32 count );

/* Creates a security tag file
 * Returns TRUE <-> success
 */
extern int32  CreateSecurityTag (void);

/* Checks that the security tag file exists
 * Returns TRUE <-> success
 */
extern int32  FindSecurityTag (void);

#endif /* __DLLPLAT_H__ */

/* eof dllplat.h */
