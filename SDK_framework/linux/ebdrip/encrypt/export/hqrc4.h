#ifndef __HQRC4_H__
#define __HQRC4_H__
/* ============================================================================
 * $HopeName: HQNencrypt!export:hqrc4.h(EBDSDK_P.1) $
 * $Id: export:hqrc4.h,v 1.3.11.1.1.1 2013/12/19 11:24:15 anon Exp $
 * 
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Interface to HQN RC4 encryption/decryption.
 */

/* You will notice that the functions and types are *identical* to the
   OpenSSL interface. This avoids needing glue code to the OpenSSL
   backend. Currently there is no need for ScriptWorks to have
   different security handlers. Also, the OpenSSL interface could
   be backed by a different implementation. */

typedef struct rc4_key_st
	{
	uint32 x,y;
	uint32 data[256];
	} RC4_KEY;

void RC4_set_key(RC4_KEY *key, uint32 len, const uint8 *data) ;

void RC4(RC4_KEY *key, uint32 len, const uint8 *indata, uint8 *outdata) ;

/* ============================================================================
* Log stripped */
#endif
