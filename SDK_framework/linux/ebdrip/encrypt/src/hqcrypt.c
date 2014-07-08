/* $HopeName: HQNencrypt!src:hqcrypt.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
* Log stripped */

#include "std.h"
HQASSERT_FILE();

#include "hqcrypt.h"

/* Constants to control encryption */

#define	E_ROTATE	31
#define	E_XOR		0x01030507u

#define STEP_KEY(key, b) MACRO_START \
    { \
        uint32 rotate_; \
	/* choose a rotate in the range 1..E_ROTATE inclusive */ \
	/* (this should avoid 0 and 32 since shifts are */ \
	/* implemented modulo 32 on some processors) */ \
	rotate_ = 1 + ((b) % E_ROTATE); \
	(key) = (((key) << rotate_) | ((key) >> (32-rotate_))) \
	        ^ (b) ^ E_XOR; \
    } \
MACRO_END

void HqEncrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf)
{
    int32 i;
    uint8 b;
    uint32 key;

/*
    HQASSERT(pKey != (uint32 *)NULL, "invalid key");
    HQASSERT(nBytes > 0 &&
             pInBuf != (uint8 *)NULL &&
             pOutBuf != (uint8 *)NULL, "invalid buffer");
*/

    key = *pKey;
    for (i = 0; i < nBytes; i++) {
    	b = *pInBuf++;
	/* encrypt the byte */
        b = (b ^ (uint8)key) + (uint8)(key >> 8);
	*pOutBuf++ = b;
	/* step the key for the next encryption */
	/* using the encrypted byte */
	STEP_KEY(key, b);
    }
    /* return the key for subsequent encryption calls */
    *pKey = key;
}

void HqDecrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf)
{
    int32 i;
    uint8 b;
    uint32 key;

/*
    HQASSERT(pKey != (uint32 *)NULL, "invalid key");
    HQASSERT(nBytes > 0 &&
             pInBuf != (uint8 *)NULL &&
             pOutBuf != (uint8 *)NULL, "invalid buffer");
*/

    key = *pKey;
    for (i = 0; i < nBytes; i++) {
    	b = *pInBuf++;
	/* decrypt the byte */
        *pOutBuf++ = (b - (uint8)(key >> 8)) ^ (uint8)key;
	/* step the key for the next decryption */
	/* using the encrypted byte */
	STEP_KEY(key, b);
    }
    /* return the key for subsequent decryption calls */
    *pKey = key;
}

/* EOF hqcrypt.c */

