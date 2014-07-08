/* $HopeName: HQNencrypt!export:hqcrypt.h(EBDSDK_P.1) $ */
/*
 * File:           hqcrypt.h
 * Compound:       HQNencrypt
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 * Description:
 *   Simple data encryption/decryption utility routines.
 *   Clients include:
 *   ScriptWorks SWhqmuldev plugin and encrypt utility.
 *
* Log stripped */

#ifndef __HQCRYPT_H__
#define __HQCRYPT_H__

/* define a couple of mappings to obfuscate the Encryption/Decryption */
/* function entry points */

#define HqEncrypt          HqTempLabel1
#define HqDecrypt          HqTempLabel2
#define HQN_ENCRYPT_LEAD   "HQE\001"

extern void HqEncrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf);
extern void HqDecrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf);

#endif /* !__HQCRYPT_H__ */

/* EOF hqcrypt.h */
