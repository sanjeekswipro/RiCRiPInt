/* $HopeName: HQNencrypt!src:oemcrypt.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
Log stripped */

/*
  This file is used to build cryptctl which is sent out to ScriptWorks OEM's
  who wish to encrypt their own control files to select which devices their 
  customers can use. Encryption may be considered too strong a word for what
  is done here!
*/

#include "std.h"
#include "oemcrypt.h"


void OemEncrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf)
{
  int32 i;
  uint8 b;
  uint32 key;

  key = *pKey;
  for (i = 0; i < nBytes; i++)
  {
    b = *pInBuf++;
	  /* encrypt the byte */
    b = b ^ (uint8)key ;
	  *pOutBuf++ = b;
	  /* step the key for the next encryption */
    key++;   
  }
  /* return the key for subsequent encryption calls */
  *pKey = key;
}


void OemDecrypt(uint32 *pKey, int32 nBytes, uint8 *pInBuf, uint8 *pOutBuf)
{
  int32 i;
  uint8 b;
  uint32 key;

  key = *pKey;
  for (i = 0; i < nBytes; i++)
  {
    b = *pInBuf++;
	  /* encrypt the byte */
    b = b ^ (uint8)key ;
	  *pOutBuf++ = b;
	  /* step the key for the next encryption */
    key++;   
  }
  /* return the key for subsequent encryption calls */
  *pKey = key;
}
