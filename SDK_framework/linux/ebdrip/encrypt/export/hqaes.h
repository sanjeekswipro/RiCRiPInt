#ifndef __HQAES_H__
#define __HQAES_H__
/* ============================================================================
 * $HopeName: HQNencrypt!export:hqaes.h(EBDSDK_P.1) $
 * $Id: export:hqaes.h,v 1.4.11.1.1.1 2013/12/19 11:24:15 anon Exp $
 * 
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Interface to HQN AES encryption/decryption.
 */

/* You will notice that the functions and types are *identical* to the
   OpenSSL interface. This avoids needing glue code to the OpenSSL
   backend. Currently there is no need for ScriptWorks to have
   different security handlers. Also, the OpenSSL interface could
   be backed by a different implementation. */

#define AES_ENCRYPT	1
#define AES_DECRYPT	0

#define AES_MAXNR 14

/* Purely for sizeof() */
typedef struct aes_key_st {
    unsigned long rd_key[4 *(AES_MAXNR + 1)];
    int rounds;
} AES_KEY;

int32 AES_set_encrypt_key(const uint8 *userKey, const int32 bits,
      AES_KEY *key);

int32 AES_set_decrypt_key(const uint8 *userKey, const int32 bits,
      AES_KEY *key);

void AES_encrypt(const uint8 *in, uint8 *out,
      const AES_KEY *key);

void AES_decrypt(const uint8 *in, uint8 *out,
      const AES_KEY *key);

void AES_cbc_encrypt(const uint8 *in, uint8 *out,
	const unsigned long length, const AES_KEY *key,
	uint8 *ivec, uint32 enc);

/* ============================================================================
* Log stripped */
#endif
