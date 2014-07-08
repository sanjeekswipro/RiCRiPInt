#ifndef __HQMD5_H__
#define __HQMD5_H__
/* ============================================================================
 * $HopeName: HQNencrypt!export:hqmd5.h(EBDSDK_P.1) $
 * $Id: export:hqmd5.h,v 1.3.11.1.1.1 2013/12/19 11:24:15 anon Exp $
 * 
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Interface to HQN MD5 hash algorithm.
 */

/* You will notice that the functions and types are *identical* to the
   OpenSSL interface. This avoids needing glue code to the OpenSSL
   backend. Currently there is no need for ScriptWorks to have
   different security handlers. Also, the OpenSSL interface could
   be backed by a different implementation. */

#define MD5_LONG uint32
#define MD5_CBLOCK 64
#define MD5_LBLOCK (MD5_CBLOCK/4)
#define MD5_DIGEST_LENGTH 16

/* Purely for sizeof() */
typedef struct MD5state_st
        {
        MD5_LONG A,B,C,D;
        MD5_LONG Nl,Nh;
        MD5_LONG data[MD5_LBLOCK];
        int num;
        } MD5_CTX;

uint32 MD5_Init(MD5_CTX *c) ;

uint32 MD5_Update(MD5_CTX *c, const void *data, uint32 len) ;

uint32 MD5_Final(uint8 *md, MD5_CTX *c) ;

uint8 *MD5(uint8 *d, uint32 n, uint8 *md) ;

void MD5_Transform(MD5_CTX *c, uint8 *b) ;

/* ============================================================================
* Log stripped */
#endif
