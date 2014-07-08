/* ============================================================================
 * $HopeName: HQNencrypt!src:hqpki.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * TODO: plug memory management and fileio. Stage #2 after checkin to keep
 * sanity.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Implementation of private key handling.
 */

/* NOTE: need OpenSSL 0.9.7x */
#include "openssl/bio.h"
#include "openssl/err.h"
#include "openssl/pkcs7.h"
#include "openssl/pem.h"
#include "openssl/pkcs12.h"

#include "std.h"
#include "hqmemcpy.h"

#include "hqpki.h"

struct PKI_detail {
  EVP_PKEY *pkey ;
  X509 *cert ;
} ;

static HqBool initialised = FALSE ;

struct PKI_context {
  PKI_callbacks cb ;
} ;

HqBool pki_init(
      PKI_context **pki_context,
      PKI_callbacks *cb)
{
  HQASSERT(pki_context != NULL, "pki_context is NULL") ;
  HQASSERT(cb != NULL, "cb is NULL") ;
  HQASSERT(cb->f_malloc != NULL, "malloc callback must be specified") ;
  HQASSERT(cb->f_free != NULL, "free callback must be specified") ;
  HQASSERT(cb->f_open != NULL, "open callback must be specified") ;
  HQASSERT(cb->f_read != NULL, "read callback must be specified") ;
  HQASSERT(cb->f_close != NULL, "close callback must be specified") ;

  OpenSSL_add_all_algorithms() ;

  *pki_context = NULL ;

  if ((*pki_context = cb->f_malloc(sizeof(PKI_context))) == NULL)
    return FALSE ;

  (*pki_context)->cb = *cb ;

  initialised = TRUE ;

  return TRUE ;
}

void pki_terminate(
      PKI_context **pki_context)
{
  HQASSERT(pki_context != NULL, "pki_context is NULL") ;
  HQASSERT(*pki_context != NULL, "pki_context pointer is NULL") ;
  HQASSERT(initialised, "PKI subsystem not initialised") ;
  initialised = FALSE ;

  (*pki_context)->cb.f_free(*pki_context) ;

  *pki_context = NULL ;
}

void pki_unload_private(
      PKI_context *pki_context,
      PKI_detail **pki_detail)
{
  HQASSERT(pki_detail != NULL, "pki_detail is NULL") ;
  HQASSERT(*pki_detail != NULL, "pki_detail pointer is NULL") ;
  HQASSERT((*pki_detail)->cert != NULL, "pki_detail cert is NULL") ;
  HQASSERT((*pki_detail)->pkey != NULL, "pki_detail pkey is NULL") ;

  X509_free((*pki_detail)->cert) ;
  EVP_PKEY_free((*pki_detail)->pkey) ;

  if (pki_context != NULL)
    pki_context->cb.f_free(*pki_detail) ;

  *pki_detail = NULL ;

  return ;
}

/* Load the private key as well as the certificate. */
HqBool pki_load_private(
      PKI_context *pki_context,
      uint8 *pfxfilename,
      uint32 pfxfilename_len,
      uint8 *password,
      uint32 passwordlen,
      PKI_detail **pki_detail)
{
  BIO *pfx ;
  int i, j ; /* Declared as int so we don't get a type mismatch with
                OpenSSL types. */
  PKCS12 *p12 = NULL ;
  STACK_OF (PKCS7) * asafes ;
  STACK_OF (PKCS12_SAFEBAG) * bags ;
  PKCS12_SAFEBAG *bag ;
  int bagnid ;
  PKCS7 *p7 ;
  PKCS8_PRIV_KEY_INFO *p8 ;
  struct PKI_detail *new_pki_detail ;
  EVP_PKEY *pkey = NULL ;
  X509 *cert = NULL ;

  HQASSERT(initialised, "PKI subsystem not initialised") ;
  HQASSERT(pki_context != NULL, "pki_context is NULL" );
  HQASSERT(pfxfilename != NULL, "pfxfilename is NULL") ;
  HQASSERT(pfxfilename_len > 0, "pfxfilename_len is <= zero") ;
  HQASSERT(pki_detail != NULL, "pki_detail is NULL") ;

  *pki_detail = NULL ;

  if (pfxfilename == NULL)
    return FALSE ;

  /* NOTE: Although the OpenSSL interface appears to have pluggable
     BIO's, it doesn't. We need to load the private key into memory
     (its small) and then use a memory based BIO to parse the private
     key from a memory buffer. */
  {
#define BUF_CHUNK 4096
#define MAX_CHUNKS 100

    PKI_file_handle *f_handle ;
    int32 status, buf_len, free_space = BUF_CHUNK ;

    uint32 num_chunks = 1,
           whole_file_len = 0 ;
    uint8 *buf, *temp, *upto,
           *whole_file = pki_context->cb.f_malloc(BUF_CHUNK) ;

    if (whole_file == NULL)
      return FALSE ;

    upto = whole_file ;

    if (! pki_context->cb.f_open(&f_handle, pfxfilename, pfxfilename_len))
      return FALSE ;

    for (;;) {
      status = pki_context->cb.f_read(f_handle, BUF_CHUNK, &buf, &buf_len) ;
      if ( status > 0) {

        /* append buf to whole_file */
        for (;;) {
          if (buf_len <= free_space) {
            HqMemCpy( upto, buf, buf_len ) ;
            upto += buf_len ;
            whole_file_len += buf_len ;
            free_space -= buf_len ;
            break ;
          } else { /* we have overflowed available buffer space -
                      allocate another chunk */

            num_chunks++ ;

            /* Give a reasonable limit for private key file
               sizes. This ought to be more than enough. */
            if (num_chunks > MAX_CHUNKS) {
              pki_context->cb.f_free( whole_file ) ;
              pki_context->cb.f_close(&f_handle) ;
              return FALSE ;
            }

            temp = pki_context->cb.f_malloc(BUF_CHUNK * num_chunks) ;
            if (temp == NULL) {
              pki_context->cb.f_free( whole_file ) ;
              pki_context->cb.f_close(&f_handle) ;
              return FALSE ;
            }
            HqMemCpy(temp, whole_file, whole_file_len ) ;
            upto = temp + whole_file_len ;
            pki_context->cb.f_free( whole_file ) ;
            whole_file = temp ;
            free_space = num_chunks * BUF_CHUNK - whole_file_len ;
          }
        } /* end of allocation loop */

      } else if (status == 0) { /* no more bytes left */
        break ;
      } else if (status < 0) { /* I/O error condition */
        pki_context->cb.f_free( whole_file ) ;
        pki_context->cb.f_close(&f_handle) ;
        return FALSE ;
      }
    }
    pki_context->cb.f_close(&f_handle) ;

    /* the PKCS 7 object */
    if ((pfx = BIO_new_mem_buf(whole_file, whole_file_len)) == NULL) {
      pki_context->cb.f_free( whole_file ) ;
      return FALSE ;
    }

    /* get the PKCS#12 object */
    p12 = d2i_PKCS12_bio(pfx, NULL) ;

    pki_context->cb.f_free( whole_file ) ;
  }

  if (p12 == NULL) {
    BIO_free (pfx);
    return FALSE ;
  }

  if ((asafes = PKCS12_unpack_authsafes (p12)) == NULL) {
    BIO_free (pfx);
    return FALSE ;
  }

  for (i=0; i < sk_PKCS7_num (asafes); i++) {
    p7 = sk_PKCS7_value (asafes, i) ;
    bagnid = OBJ_obj2nid (p7->type) ;
    if (bagnid == NID_pkcs7_data) {
      bags = PKCS12_unpack_p7data (p7) ;
    } else if (bagnid == NID_pkcs7_encrypted) {
      bags = PKCS12_unpack_p7encdata (p7, (char *) password, passwordlen) ;
    } else
      continue;

    if (bags == NULL) {
      BIO_free (pfx) ;
      return FALSE ;
    }

    for (j=0; j < sk_PKCS12_SAFEBAG_num (bags); j++) {
      bag = sk_PKCS12_SAFEBAG_value (bags, j) ;
      switch (M_PKCS12_bag_type(bag)) {
        case NID_keyBag:
          if (pkey != NULL)
            break;
          p8 = bag->value.keybag ;
          if ((pkey = EVP_PKCS82PKEY(p8)) == NULL) {
            BIO_free(pfx);
            sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free) ;
            sk_PKCS7_pop_free(asafes, PKCS7_free) ;
            return FALSE ;
          }
          break;

        case NID_pkcs8ShroudedKeyBag:
          if (pkey != NULL)
            break;
          if ((p8 = PKCS12_decrypt_skey(bag, (char *) password, passwordlen)) == NULL) {
            BIO_free(pfx) ;
            sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free) ;
            sk_PKCS7_pop_free(asafes, PKCS7_free) ;
            return FALSE ;
          }
          if ((pkey = EVP_PKCS82PKEY(p8)) == NULL) {
            PKCS8_PRIV_KEY_INFO_free(p8) ;
            BIO_free(pfx) ;
            sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free) ;
            sk_PKCS7_pop_free(asafes, PKCS7_free) ;
            return FALSE ;
          }
          PKCS8_PRIV_KEY_INFO_free(p8) ;
          break;

        case NID_certBag:
          if (cert != NULL)
            break;
          cert = PKCS12_certbag2x509(bag) ;
          if (cert == NULL) {
            BIO_free (pfx);
            sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
            sk_PKCS7_pop_free(asafes, PKCS7_free);
            return FALSE ;
          }
          break;

        default:
          break;
      }
    }
    sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free) ;
    if (cert != NULL && pkey != NULL)
      break;
  }
  sk_PKCS7_pop_free(asafes, PKCS7_free);
  BIO_free(pfx);

  if (cert == NULL || pkey == NULL)
    return FALSE ;

  if ((new_pki_detail = pki_context->cb.f_malloc(sizeof(struct PKI_detail))) == NULL)
    return FALSE ;

  new_pki_detail->cert = cert ;
  new_pki_detail->pkey = pkey ;

  *pki_detail = new_pki_detail ;
  return TRUE ;
}

HqBool pki_decrypt_enveloped_data(
      PKI_context *pki_context,
      PKI_detail *pki_detail,
      uint8 *envelope,
      uint32 envelope_len,
      uint8 **decrypted,
      uint32 *out_length,
      HqBool *matched)
{
  PKCS7 *p7 = NULL ;
  BIO *out = NULL ;
  int flags = PKCS7_DETACHED ;
  BIO *mem ;
  char *output ;

  HQASSERT(initialised, "PKI subsystem not initialised") ;
  HQASSERT(pki_context != NULL, "pki_context is NULL" );
  HQASSERT(pki_detail != NULL, "pki_detail is NULL") ;
  HQASSERT(envelope != NULL, "envelope is NULL") ;
  HQASSERT(envelope_len > 0, "envelope_len is not > zero") ;

  /* the PKCS 7 object */
  if ((mem = BIO_new_mem_buf(envelope, envelope_len)) == NULL)
    return FALSE ;

  p7 = d2i_PKCS7_bio(mem, NULL) ;
  BIO_free(mem) ;
  if (p7 == NULL)
    return FALSE ;

  if ((out = BIO_new(BIO_s_mem())) == NULL) {
    PKCS7_free(p7) ;
    return FALSE ;
  }

  if (! PKCS7_decrypt(p7, pki_detail->pkey, pki_detail->cert, out, flags)) {
    BIO_free(out) ;
    /* typical error: the private key is not matched the recipient */
    *matched = 0 ;
    PKCS7_free(p7) ;
    /* return TRUE so that we can carry on to detect another recipient string if there is any more */
    return TRUE ;
  }

  *out_length = BIO_get_mem_data(out, &output) ;

  /* TODO: remove this length check */
  if (*out_length != 24) {
    BIO_free(out) ;
    return FALSE ;
  }

  /* make a copy of the decrypted data */
  *decrypted = (uint8 *)pki_context->cb.f_malloc(*out_length) ;
  if ((*decrypted) == NULL) {
    BIO_free (out);
    return FALSE ;
  }
  HqMemCpy(*decrypted, output, *out_length) ;

  BIO_free(out) ;

  /* it is done */
  *matched = 1;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
