/* $HopeName: HQNencrypt!export:oemcrypt.h(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
Log stripped */

#ifndef __OEMCRYPT_H__
#define __OEMCRYPT_H__

/* define a couple of mappings to obfuscate the Encryption/Decryption */
/* function entry points */

#define OemEncrypt	HqTempLabel3
#define OemDecrypt	HqTempLabel4

extern void OemEncrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf);
extern void OemDecrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf);

#endif /* __OEMCRYPT_H__ */

