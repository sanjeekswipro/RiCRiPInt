/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __DLLUTILS_H__
#define __DLLUTILS_H__

/*
 * $HopeName: SWsecurity!src:dllutils.h(EBDSDK_P.1) $
 *
* Log stripped */


/* ----------------------- Macros ------------------------------------------ */

/* Obfuscation defines */
#define decodeLicence  updateDlog
#define encodeLicence  procNewValue
#define HqDecrypt   ReportRes


/* ----------------------- Functions --------------------------------------- */

extern uint32 decodeLicence( uint32 serialNo, uint8 * encodedLicence, uint32 encodedLength, uint8 * decodedLicence );
extern uint32 encodeLicence( uint32 serialNo, uint8 * decodedLicence, uint32 decodedLength, uint8 * encodedLicence );
extern void HqDecrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf);

#endif /* __DLLUTILS_H__ */

/* eof dllutils.h */
