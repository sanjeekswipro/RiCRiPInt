#ifndef __HQSHA1_H__
#define __HQSHA1_H__
/* ============================================================================
 * $HopeName: HQNencrypt!export:hqsha1.h(EBDSDK_P.1) $
 * $Id: export:hqsha1.h,v 1.3.11.1.1.1 2013/12/19 11:24:15 anon Exp $
 * 
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Interface to HQN SHA1 encryption/decryption.
 */

/* You will notice that the functions and types are *identical* to the
   OpenSSL interface. This avoids needing glue code to the OpenSSL
   backend. Currently there is no need for ScriptWorks to have
   different security handlers. Also, the OpenSSL interface could
   be backed by a different implementation. */

#define SHA_LONG uint32
#define SHA_LBLOCK 16
#define SHA_CBLOCK (SHA_LBLOCK*4)  /* SHA treats input data as a
                                    * contiguous array of 32 bit
                                    * wide big-endian values. */
#define SHA_LAST_BLOCK  (SHA_CBLOCK-8)
#define SHA_DIGEST_LENGTH 20

/* Purely for sizeof() */
typedef struct SHAstate_st
        {
        SHA_LONG h0,h1,h2,h3,h4;
        SHA_LONG Nl,Nh;
        SHA_LONG data[SHA_LBLOCK];
        int num;
        } SHA_CTX;

int32 SHA1_Init(SHA_CTX *c) ;

int32 SHA1_Update(SHA_CTX *c, const void *data, uint32 len) ;

int32 SHA1_Final(uint8 *md, SHA_CTX *c) ;

uint8 *SHA1(const uint8 *d, uint32 n, uint8 *md) ;

void SHA1_Transform(SHA_CTX *c, const uint8 *data) ;

/* ============================================================================
* Log stripped */
#endif
