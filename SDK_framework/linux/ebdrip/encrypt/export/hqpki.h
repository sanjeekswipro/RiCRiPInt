#ifndef __HQPKI_H__
#define __HQPKI_H__
/* ============================================================================
 * $HopeName: HQNencrypt!export:hqpki.h(EBDSDK_P.1) $
 * $Id: export:hqpki.h,v 1.3.11.1.1.1 2013/12/19 11:24:15 anon Exp $
 * 
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Interface to HQN PKI.
 */

typedef struct PKI_context PKI_context ;

typedef struct PKI_detail PKI_detail ;

typedef struct PKI_file_handle PKI_file_handle ;

typedef HqBool (*PKI_open_file)(
     PKI_file_handle **file_handle,
     uint8 *filename,
     uint32 filename_len) ;

typedef void (*PKI_close_file)(
     PKI_file_handle **file_handle) ;

typedef int32 (*PKI_read_file)(
     PKI_file_handle *file_handle,
     int32 bytes_wanted,
     uint8 **buffer,
     int32 *buf_len) ;

typedef void * (*PKI_malloc)(
      size_t size) ;

typedef void (*PKI_free)(
      void *memPtr) ;

typedef struct PKI_callbacks {
  PKI_malloc f_malloc ;
  PKI_free f_free ;
  PKI_open_file f_open ;
  PKI_read_file f_read ;
  PKI_close_file f_close ;

  /* Am unable to plug OpenSSL memory management at this stage. */
} PKI_callbacks ;

extern HqBool pki_load_private(
      PKI_context *pki_context,
      uint8 *pfxfilename,
      uint32 pfxfilename_len,
      uint8 *password,
      uint32 passwordlen,
      PKI_detail **pki_detail) ;

extern void pki_unload_private(
      PKI_context *pki_context,
      PKI_detail **pki_detail) ;

extern HqBool pki_decrypt_enveloped_data(
      PKI_context *pki_context,
      PKI_detail *pki_detail,
      uint8 *envelope,
      uint32 envelope_len,
      uint8 **decrypted,
      uint32 *out_length,
      HqBool *matched) ;

HqBool pki_init(
      PKI_context **ctxt,
      PKI_callbacks *cb) ;

void pki_terminate(
      PKI_context **ctxt) ;

/* ============================================================================
* Log stripped */
#endif
