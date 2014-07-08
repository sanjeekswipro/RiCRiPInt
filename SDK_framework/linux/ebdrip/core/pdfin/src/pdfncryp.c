/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfncryp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PDF decryption.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "swerrors.h"
#include "swcopyf.h"
#include "swctype.h"
#include "swdevice.h"
#include "swoften.h"
#include "objects.h"

#include "mm.h"
#include "mmcompat.h"
#include "fileioh.h"

/* Hash, decryption and PKI routines. */
#include "hqmd5.h"
#include "hqrc4.h"
#include "hqsha1.h"
#include "hqaes.h"
#include "hqpki.h"

#include "monitor.h"
#include "namedef_.h"

#include "scanner.h"
#include "chartype.h"
#include "matrix.h"
#include "graphics.h"
#include "dictops.h"

#include "swpdf.h"
#include "pdfcntxt.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"
#include "pdfdefs.h"

#include "swpdfin.h"
#include "pdfin.h"
#include "pdfncryp.h"
#include "pdfattrs.h"
#include "pdfclip.h"
#include "pdfencod.h"
#include "pdfexec.h"
#include "pdffont.h"
#include "pdfops.h"
#include "pdfscan.h"
#include "pdfsgs.h"
#include "pdftxt.h"


#define PADDED_PASSWORD_LEN (32)
#define KEY_STEM_LEN (16)

/* Public keys are generated as 20 bytes */
#define MAX_KEY_RAW 20

typedef struct encryption_crypt_filter
{
  /* Name of this filter */
  uint8 *cf_name ;
  uint32 cf_name_len ;

  /* None, V2 or AESV2 */
  uint32 cf_cfm ;

  /* The crypt filter key length */
  uint32 cf_keyrawbytes ;

  Bool cf_encrypt_metadata ;
} encryption_crypt_filter ;

struct pdf_crypto_info {
  /* Encryption handler version. /V*/
  int32 version ;

  /* Encryption algorithm revision. /R */
  int32 revision ;

  /* Do we have the password and hence the decryption key?  This gets
     set to TRUE when we have found a password which works. */
  Bool found_valid_password ;

  /* If we match a owner password, then we always allow printing. */
  Bool matched_owner_password ;

  /* Other encryption dictionary entries. /O and /U */
  uint8 *owner, *user, *fileid ;
  int32 owner_len, user_len, fileid_len ;
  int32 permission ; /* /P */

  /* Encrypted user supplied password for comparison against
     owner/user. */
  uint8 *password ;
  int32 passwordlen ;

  /* RAW decryption key. We may not use all of the decryption key
     bytes - depending on what the PDF specification. */
  uint32 keyrawbytes ;
  uint8 keyraw[ MAX_KEY_RAW ] ;

  /* Number of bytes from the rawkey which ought to be used for
     decryption. /Length - if used. */
  uint32 keybytes ;

  /* Security handler type - /Filter */
  int32 sc_handler ;

  /* Sub-filter name - /SubFilter */
  int32 sub_filter ;

  /* Is the metadata encrypted - /EncryptMetadata */
  Bool encrypt_metadata ;

  /* All private key details. We load all private key files before
     processing the PDF job, decrypting the private key and
     certificate. */
  uint32 num_pki_detail ;
  PKI_detail **pki_detail ; /* array of pointers */

  /* All crypt filters apart from the standard one - 'Identical'. Have
     not seen any PDF files which have more than one such filter so
     far. */
  uint32 num_crypt_filters ;
  encryption_crypt_filter *cf_filter ;

  /* The default crypt filter for streams. Points into cf_filter
     array. */
  encryption_crypt_filter *default_stm ;

  /* The default crypt filter for strings. Points into cf_filter
     array. */
  encryption_crypt_filter *default_str ;

  /* The default crypt filter for embedded file streams. Points into
     cd_filter array. */
  encryption_crypt_filter *default_eff ;

  /* This is used by the PDF string decryption code. We need to know
     when we are reading the infodict as we may or may not have the
     metadata information encrypted. */
  Bool reading_metadata ;

  /* When loading private keys, we need to initialise the PKI
     subsystem which in turn has its own context. */
  PKI_context *pki_context ;
} ;

/* sc_handler values */
#define PCI_ET_unknown          0
#define PCI_ET_Standard         1
#define PCI_ET_Adobe_PubSec     2

/* PCI_ET_Adobe_PubSec is a temp value filled with the values below */
#define PCI_ET_adbe_pkcs7_s3    3
#define PCI_ET_adbe_pkcs7_s4    4
#define PCI_ET_adbe_pkcs7_s5    5

/* Crypt Filter names */
#define CF_ET_AESV2            1 /* AES */
#define CF_ET_V2               2 /* RC4 */
#define CF_ET_None             3 /* Not encrypted */

#define U_FIELD_LEN (PADDED_PASSWORD_LEN)
#define O_FIELD_LEN (PADDED_PASSWORD_LEN)

/* /P */
#define PRINT_PERMISSIONS_MASK 4

#define PWMAXSIZE           32

/* As per the PDF 1.6 specification */
#define HASHLOOPCOUNT_REV3  50
#define CRYPTLOOPCOUNT_REV3 19

/*
 * String used to pad passwords out to 32 bytes and to check the result
 * of decrypting the user password.
 * These magic values are documented in the PDF Reference Manual (page 78
 * of the PDF Reference, 3rd Edition, version 1.4)
 */
static uint8 padstring[] =
  {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41,
    0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80,
    0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A,
  };

/* ============================================================================
 * PKI pluggable interfaces.
 */
static void * pki_malloc(
      size_t size)
{
  void *alloc;

  alloc = mm_alloc_with_header(mm_pool_temp, (mm_size_t)size,
                               MM_ALLOC_CLASS_PKI) ;
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

static void pki_free(
      void *memPtr)
{
  mm_free_with_header(mm_pool_temp, memPtr) ;
}

struct PKI_file_handle {
  OBJECT ofile ;
} ;

static Bool pki_open(
     PKI_file_handle **file_handle,
     uint8 *filename,
     uint32 filename_len)
{
  OBJECT ostr_filename = OBJECT_NOTVM_NOTHING;

  theTags(ostr_filename) = OSTRING|UNLIMITED|LITERAL ;
  theLen(ostr_filename) = CAST_TO_UINT16(filename_len) ;
  oString(ostr_filename) = filename ;

  if ((*file_handle = mm_alloc( mm_pool_temp, sizeof(PKI_file_handle),
                                MM_ALLOC_CLASS_PKI)) == NULL)
    return error_handler(VMERROR) ;

  if (! file_open(&ostr_filename, SW_RDONLY, READ_FLAG, FALSE, 0, object_slot_notvm(&((*file_handle)->ofile)))) {
    mm_free( mm_pool_temp, *file_handle, sizeof(PKI_file_handle)) ;
    *file_handle = NULL ;
    return FALSE ;
  }

  return TRUE ;
}

static int32 pki_read(
     PKI_file_handle *file_handle,
     int32 bytes_wanted,
     uint8 **buffer,
     int32 *buf_len)
{
  FILELIST *flptr = oFile(file_handle->ofile) ;

  if (! GetFileBuff(flptr, bytes_wanted, buffer, buf_len)) {
    if (isIIOError(flptr)) {
      return -1 ;
    } else {
      *buf_len = 0 ;
      return 0 ;
    }
  } else {
    return 1 ;
  }
}

static void pki_close(
      PKI_file_handle **file_handle)
{
  (void) file_close(&(*file_handle)->ofile) ;
  mm_free( mm_pool_temp, *file_handle, sizeof(PKI_file_handle)) ;
  *file_handle = NULL ;
}

/* ============================================================================
 * Functions which actually do the decryption while processing PDF files.
 */

Bool pdf_decrypt_string(
      PDFXCONTEXT *pdfxc,
      uint8 *string_in,
      uint8 *string_out,
      int32 len,
      int32 *newlen,
      uint32 objnum,
      uint32 objgen)
{
  PDF_CRYPTO_INFO *crypt_info ;
  uint8  md5in[ 25 ], md5out[ MD5_DIGEST_LENGTH ] ;
  int32 local_length, decrypted_string_len = len ;
  uint32 cf_cfm ;
  RC4_KEY rc4key ;
  AES_KEY aeskey ;

  *newlen = 0 ;

  /* The first n bytes for md5 are the constant for the whole file.
   * These are already present in pdfxc->rc4_key.
   */

  HQASSERT( pdfxc != NULL, "pdfxc is NULL" ) ;
  crypt_info = pdfxc->crypt_info ;
  HQASSERT( crypt_info != NULL , "crypt_info is NULL" ) ;

  /* SEE NOTE IN HEADER OF THIS FILE */
#if 0
  if (crypt_info->reading_metadata && ! crypt_info->encrypt_metadata) {
    HqMemCpy( string_out, string_in, len ) ;
    return TRUE ;
  }
#endif

  /* default */
  cf_cfm = CF_ET_V2 ;
  local_length = crypt_info->keybytes ;

  if (crypt_info->version == 4) {
    if (crypt_info->default_str != NULL) {
      cf_cfm = crypt_info->default_str->cf_cfm ;
      local_length = crypt_info->default_str->cf_keyrawbytes ;
    } else {
      /* Identity filter. */
      HqMemCpy(string_out, string_in, len) ;
      *newlen = decrypted_string_len ;
      return TRUE ;
    }
  }

  /* PDF 1.6 Algorithm 3.1 step 2 */

  /* Use the lowest order 3 bytes of the object number & the lowest
   * order 2 bytes of the generation number as the 2nd 5 bytes for md5
   */

  HqMemCpy(md5in, crypt_info->keyraw, local_length) ;

  md5in[ local_length ]     = ( uint8 )( objnum >>  0 & 0xff ) ;
  md5in[ local_length + 1 ] = ( uint8 )( objnum >>  8 & 0xff ) ;
  md5in[ local_length + 2 ] = ( uint8 )( objnum >> 16 & 0xff ) ;
  md5in[ local_length + 3 ] = ( uint8 )( objgen >>  0 & 0xff ) ;
  md5in[ local_length + 4 ] = ( uint8 )( objgen >>  8 & 0xff ) ;

  /* See PDF 1.6 errata */
  if (cf_cfm == CF_ET_AESV2) {
    md5in[ local_length + 5 ] = (uint8)'s' ;
    md5in[ local_length + 6 ] = (uint8)'A' ;
    md5in[ local_length + 7 ] = (uint8)'l' ;
    md5in[ local_length + 8 ] = (uint8)'T' ;
    local_length += 9 ;
  } else {
    local_length += 5 ;
  }

  /* PDF 1.6 Algorithm 3.1 step 3 */
  /* calculate decryption key. */
  (void)MD5(md5in, local_length, md5out) ;

  /* PDF 1.6 Algorithm 3.1 step 4 */
  if ( local_length > 16 )
    local_length = 16;

  switch (cf_cfm) {
    case CF_ET_AESV2:
      if (len % 16 != 0) /* for in the real world */
        return detail_error_handler(UNDEFINED,
                                    "AES string length is not multiple of 16.") ;

      if (len < 32) /* MUST have iv and at least one further block */
        return detail_error_handler(UNDEFINED,
                                    "AES string length is less that 32.") ;


      {
        uint32 padlen = 0 ;
        uint8 *iv = string_in ;

        /* First 16 bytes in AES encrypted stream/string is the iv */
        string_in += 16 ;
        len -= 16 ;
        decrypted_string_len -= 16 ;

        if (AES_set_decrypt_key(md5out, local_length * 8, &aeskey) < 0)
          return detail_error_handler(UNDEFINED, "AES decryption error") ;

        /* AES_cbc_encrypt() loops around the 16 byte CBC blocks */
        AES_cbc_encrypt(string_in, string_out, len, &aeskey, iv, AES_DECRYPT) ;

        /* The final length is the length - the value of the last byte
           which contains the pad length. See PDF 1.6 errata as per
           RFC 2898. */
        padlen = (uint32)string_out[len - 1] ; /* extra variable for sanity */
        decrypted_string_len -= padlen ;
      }
      break ;

    case CF_ET_V2:
      RC4_set_key (&rc4key, local_length, md5out) ;
      RC4 (&rc4key, len, string_in, string_out) ;
      break ;

    case CF_ET_None:
      break ;

    default:
      break ;
  }

  *newlen = decrypted_string_len ;
  return TRUE ;
}

/**
 * Check for the presence of the Crypt filter.
 *
 * \param cryptFilter Set to TRUE if a Crypt filter is found.
 * \return FALSE on error.
 */
static Bool checkForCryptFilter(OBJECT* filters,
                                OBJECT* decodeParams,
                                Bool* cryptFilter) {
  UNUSED_PARAM(OBJECT*, decodeParams);

  if (filters == NULL)
    return TRUE;

  if (oType(*filters) == OARRAY) {
    int32 i;
    int32 totalFilters = theLen(*filters);
    OBJECT* name = oArray(*filters);

    HQASSERT(decodeParams == NULL || theLen(*filters) == theLen(*decodeParams),
             "Filter and decode parameter array lengths differ.");
    for (i = 0; i < totalFilters; i ++) {
      HQASSERT(oType(*name) == ONAME, "Only names are allowed in a filter list.");

      if (oNameNumber(*name) == NAME_Crypt) {
        *cryptFilter = TRUE;
        break;
      }
    }
  }
  else {
    HQASSERT(oType(*filters) == ONAME, "'filters' should be a name or an array.");
    if (oNameNumber(*filters) == NAME_Crypt)
      *cryptFilter = TRUE;
  }

  return TRUE;
}

/* To decrypt PDF streams in ScriptWorks, we insert a decryption filter
 * on the stream for that stream object. If is_stream is FALSE, we assume
 * its an embedded file which is being decrypted.
 *
 * The filter and decodeparams are passed to this function so that we can check
 * if there is a /Crypt filter specification for the stream, which means that
 * the default should not be used.
 */
Bool pdf_insert_decrypt_filter(
      PDFCONTEXT *pdfc,
      OBJECT *pdfobj,
      OBJECT *filter,
      OBJECT *decodeparams,
      uint32 objnum,
      uint32 objgen,
      Bool is_stream)
{
  PDFXCONTEXT *pdfxc ;
  PDF_CRYPTO_INFO *crypt_info ;
  uint8  md5in[ 25 ], md5out[ MD5_DIGEST_LENGTH ] ;
  uint32 local_length;
  uint32 cf_cfm ;
  OBJECT name = OBJECT_NOTVM_NOTHING ;
  OBJECT decrypt_params = OBJECT_NOTVM_NOTHING ;
  OBJECT decrypt_key = OBJECT_NOTVM_NOTHING ; /* For AES or RC4 */

  /* The first n bytes for md5 are the constant for the whole file.
   * These are already present in pdfxc->rc4_key.
   */

  HQASSERT( pdfc != NULL, "pdfc is NULL" ) ;
  HQASSERT( pdfobj != NULL, "pdfobj is NULL" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  crypt_info = pdfxc->crypt_info ;
  HQASSERT(crypt_info != NULL, "crypt_info is NULL") ;

  /* default decryption if decryption is being used  - basically
     the default document decryption */
  cf_cfm = CF_ET_V2 ;
  local_length = crypt_info->keybytes ;

  if (crypt_info->version == 4) {
    Bool cryptFilter = FALSE;

    /* If the stream filters include a Crypt filter, don't apply the document
    decryption filter automatically. */
    if (! checkForCryptFilter(filter, decodeparams, &cryptFilter))
      return FALSE;
    if (cryptFilter)
      return TRUE;

    if (is_stream) {
      if (crypt_info->default_stm != NULL) {
        cf_cfm = crypt_info->default_stm->cf_cfm ;
        local_length = crypt_info->default_stm->cf_keyrawbytes ;
      } else {
        /* Identity filter. */
        return TRUE ;
      }
    } else { /* Its an embedded file. */
      if (crypt_info->default_eff != NULL) {
        cf_cfm = crypt_info->default_eff->cf_cfm ;
        local_length = crypt_info->default_eff->cf_keyrawbytes ;
        /* If eff default is not specified, use stream default. */
      } else if (crypt_info->default_stm != NULL) {
        cf_cfm = crypt_info->default_stm->cf_cfm ;
        local_length = crypt_info->default_stm->cf_keyrawbytes ;
      } else {
        /* Identity filter. */
        return TRUE ;
      }
    }
  }

  if (! pdf_create_dictionary(pdfc, 1, &decrypt_params))
    return FALSE ;
  /* freed when the context ends and the memory pool is torn down */

  /* Calculate the key for the decryption. */

  /* PDF 1.6 Algorithm 3.1 step 2 */

  /* Use the lowest order 3 bytes of the object number & the lowest
   * order 2 bytes of the generation number as the 2nd 5 bytes for md5
   */
  HqMemCpy(md5in, crypt_info->keyraw, local_length) ;

  md5in[ local_length ]     = ( uint8 )( objnum >>  0 & 0xff ) ;
  md5in[ local_length + 1 ] = ( uint8 )( objnum >>  8 & 0xff ) ;
  md5in[ local_length + 2 ] = ( uint8 )( objnum >> 16 & 0xff ) ;
  md5in[ local_length + 3 ] = ( uint8 )( objgen >>  0 & 0xff ) ;
  md5in[ local_length + 4 ] = ( uint8 )( objgen >>  8 & 0xff ) ;

  /* See PDF 1.6 errata */
  if (cf_cfm == CF_ET_AESV2) {
    md5in[ local_length + 5 ] = (uint8)'s' ;
    md5in[ local_length + 6 ] = (uint8)'A' ;
    md5in[ local_length + 7 ] = (uint8)'l' ;
    md5in[ local_length + 8 ] = (uint8)'T' ;
    local_length += 9 ;
  } else {
    local_length += 5 ;
  }

  /* PDF 1.6 Algorithm 3.1 step 3 */
  /* calculate decryption key. */
  (void)MD5(md5in, local_length, md5out) ;

  /* PDF 1.6 Algorithm 3.1 step 4 */
  if ( local_length > 16 )
    local_length = 16;

  /* We now have the decryption key so create the appropriate
     decrypt filter with this key. */

  if (! pdf_create_string(pdfc, local_length, &decrypt_key))
    return FALSE ;

  HqMemCpy(oString(decrypt_key), md5out, local_length) ;

  switch (cf_cfm) {
    case CF_ET_AESV2:
      /* insert AES decryption filter. */
      object_store_name(&name, NAME_AESKey, LITERAL) ;
      if (! pdf_fast_insert_hash(pdfc, &decrypt_params, &name, &decrypt_key))
        return FALSE ;

      object_store_name(&name, NAME_AESDecode, LITERAL) ;
      if (! pdf_createfilter(pdfc, pdfobj, &name, &decrypt_params, TRUE))
        return FALSE ;

      break ;

    case CF_ET_V2:
      /* insert RC4 decryption filter. */
      object_store_name(&name, NAME_RC4Key, LITERAL) ;
      if (! pdf_fast_insert_hash(pdfc, &decrypt_params, &name, &decrypt_key))
        return FALSE ;

      object_store_name(&name, NAME_RC4Decode, LITERAL) ;
      if (! pdf_createfilter(pdfc, pdfobj, &name, &decrypt_params, TRUE))
        return FALSE ;

      break ;

    case CF_ET_None:
      break ;

    default:
      break ;
  }

  return TRUE ;
}

void set_reading_metadata(
    PDFCONTEXT *pdfc,
    Bool are_we)
{
  PDFXCONTEXT *pdfxc ;
  PDF_CRYPTO_INFO *crypt_info ;

  HQASSERT( pdfc != NULL, "pdfc is NULL" ) ;
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  crypt_info = pdfxc->crypt_info ;

  if (crypt_info != NULL)
    crypt_info->reading_metadata = are_we ;
}

/* ============================================================================
 * Password checking functions as per the PDF specifications.
 */

/* make_encryption_key() uses the 'standard' algorithm, 3.2 on page 69 of the
 * PDF 1.3 Reference Manual (June 2000 revision) to create a hash object which
 * contains the key to be used to initialise an RC4 filter for decrypting
 * PDF files.
 *
 * The algorithm used for 128 bit decryption is algorithm 3.2 as described
 * on page 16 of the PDF14Deltas document, June 2001 revision.
 *
 * Compute an encryption key using the provided password :
 *
 *   1) Pad the password to 32 bytes using the defined padding string
 *      If it already exceeds 32 bytes, truncate it.
 *
 *   2) MD5 hash the padded password.
 *
 *   3) Hash the /O encryption dictionary entry as well.
 *
 *   4) Treat the /P entry (permissions) as a 4-byte uint32
 *      and hash these LOW BYTE FIRST.
 *
 *   5) Hash the first element of the File ID string
 *
 *   For Revision 3, additionally perform :
 *
 *     6) Loop an additional 50 times, taking the result of the hash
 *        function as the input to a new MD5 hash.
 *
 *   The first N bytes are the encryption key.
 */
static void make_encryption_key(
      PDF_CRYPTO_INFO *pci,
      uint8 *pw,
      int32 pwlen)
{
  MD5_CTX Hash;
  int32   permission;
  uint8   permtemp[4];

  HQASSERT( pci , "pci is NULL" ) ;

  /* Compute an encryption key using the password. */
  (void) MD5_Init( &Hash );

  /* PDF 1.6 Algorithm 3.2 step 1 & 2 */
  (void) MD5_Update( &Hash, pw , pwlen );
  (void) MD5_Update( &Hash, padstring, PWMAXSIZE - pwlen );

  /* PDF 1.6 Algorithm 3.2 step 3 */
  (void) MD5_Update( &Hash, pci->owner, pci->owner_len );

  /* PDF 1.6 Algorithm 3.2 step 4 */
  permission = pci->permission;
  permtemp[0] = (uint8)(  permission       & 0xff);
  permtemp[1] = (uint8)( (permission >> 8) & 0xff);
  permtemp[2] = (uint8)((permission >> 16) & 0xff);
  permtemp[3] = (uint8)((permission >> 24) & 0xff);

  (void) MD5_Update( &Hash, permtemp, 4);

  /* PDF 1.6 Algorithm 3.2 step 5 */
  (void) MD5_Update( &Hash, pci->fileid, pci->fileid_len );

  /* PDF 1.6 Algorithm 3.2 step 9 */
  pci->keyrawbytes = 5 ;

  /* For Revision 3 (128 bit) we need to loop 50 times taking the Hash
     value and passing it through MD5 again.... */
  if ( pci->revision >= 3 ) {
    int32 HashLoop;
    /* PDF 1.6 Algorithm 3.2 step 6 - If document metadata is not
     * being encrypted, pass 4 bytes with the value 0xFFFFFFFF to the
     * MD5 hash function. */
    if (! pci->encrypt_metadata) {
      /* reuse permtemp here */
      permtemp[0] = permtemp[1] = permtemp[2] = permtemp[3] = 0xff ;
      (void) MD5_Update( &Hash, permtemp, 4);
    }

    HQASSERT(pci->keybytes <= 16,
             "keybytes is not less than or equal to 16") ;

    HQASSERT(pci->keybytes == 16, "keybytes is not equal to 16") ;

    /* PDF 1.6 Algorithm 3.2 step 8 */
    for ( HashLoop = 0; HashLoop < HASHLOOPCOUNT_REV3; HashLoop++ ) {
      uint8 digest[ 16 ];

      (void) MD5_Final( digest, &Hash );
      (void) MD5_Init( &Hash );
      (void) MD5_Update( &Hash, digest, pci->keybytes );
    }

    /* PDF 1.6 Algorithm 3.2 step 9 */
    pci->keyrawbytes = pci->keybytes ;
  }

  /* PDF 1.6 Algorithm 3.2 step 7 */
  (void) MD5_Final( pci->keyraw, &Hash );
}

/* check_user_password() validates a user password against the
 * 'Standard' PDF encryption filter.
 *
 * Algorithm 3.5 (page 17 of the June PDFF14DFeltas document)
 *
 * 1. Compute an encryption key from the password
 *
 * 2. Decrypt the /U value of the encryption dictionary
 *    using RC4, and the encryption key above.
 *
 * For Revision 2, compare this to the fixed padding string.
 *
 * For Revision 3 perform steps 3 and 4.
 *
 * 3. Revision 3, do the following 19 times :
 *    3.1 Generate a new RC4 key by taking each
 *        byte of the key generated in step 1 and XOR
 *        each byte with the single byte of the loop counter (19 to 1)
 *    3.2 Take the output from the previous RC4 and *encrypt*
 *        it using the encryption key above.
 *
 * 4. Compute the first 4 steps of algorithm 3.5 :
 *    4.1 Create an encyrption key from the password
 *    4.2 MD5 Hash the key
 *    4.3 MD5 Hash element 1 of the File ID.
 *    4.4 Using the key from step 1 generate an RC4
 *      and encrypt the hashed data.
 *
 * However, the above doesn't actually work. The algorithm which does work :
 *
 *    4.1 Take the fixed padding string
 *    4.2 MD5 Hash the padding string
 *    4.3 MD5 Hash the first string of the File ID
 *
 * Compare the results of step 3 & 4.
 */

Bool check_user_password(
      PDF_CRYPTO_INFO *pci,
      uint8 *pw,
      int32 pwlen) {
  /* overkill currently by 16 bytes, in some uses, but good uniformity */
  uint8 temp[ PADDED_PASSWORD_LEN ] ;
  uint8 temp2[ PADDED_PASSWORD_LEN ] ;
  uint8 Step1Copy[ PADDED_PASSWORD_LEN ] ;
  RC4_KEY rc4key ;

  MD5_CTX Hash ;
  int32   i, j ;
  uint32  len ;

  HQASSERT( pci , "pci NULL in check_user_password" ) ;

  make_encryption_key(pci, pw, pwlen) ;

  len = pci->user_len ;

  if ( len != 0 ) {
    if ( len > PADDED_PASSWORD_LEN ) {
      HQFAIL( "pci->user_len > PADDED_PASSWORD_LEN" ) ;
      return FALSE ;
    }
    RC4_set_key (&rc4key, pci->keybytes, pci->keyraw) ;
    RC4 (&rc4key, len, pci->user, temp);
  }
  switch ( pci->revision ) {
    case 2:
    if ( HqMemCmp( temp, len, padstring, len) == 0 )
      return TRUE;
    break;

    case 3:
    case 4:
    {
      int32 local_rawbytes = pci->keyrawbytes;

      if ( local_rawbytes >= PADDED_PASSWORD_LEN ) {
        HQFAIL( "pci->keyrawbytes >= PADDED_PASSWORD_LEN" );
        return FALSE ;
      }

      for ( j = CRYPTLOOPCOUNT_REV3; j > 0; j-- ) {
        /* 3.1 Generate new RC4 key by XOring the original. */
        HqMemCpy( Step1Copy, pci->keyraw, local_rawbytes);
        for ( i = 0; i < local_rawbytes; i++ )
          Step1Copy[i] ^= j;

        RC4_set_key (&rc4key, local_rawbytes, Step1Copy) ;
        RC4 (&rc4key, len, temp, temp2);

        HqMemCpy ( temp, temp2, len );
      }
    }

    (void) MD5_Init( &Hash );

    /* 4.1 MD5 Hash The fixed padding string. */
    (void) MD5_Update( &Hash, padstring, sizeof ( padstring ) );

    /* 4.2 MD5 Hash element 1 of the File ID. */
    (void) MD5_Update( &Hash, pci->fileid, pci->fileid_len );

    (void) MD5_Final( temp2, &Hash );

    /* Compare the results of step 3 (in temp) and step 4 (in temp2) */
    if ( len > 16 )
      len = 16;

    if ( HqMemCmp( temp, len, temp2, 16 ) == 0 ) /* was len now 16 */
      return TRUE;

    break;
  }

  return FALSE;
}

/* check_owner_password() validates an owner password against the
 * 'Standard' PDF encryption filter.
 *
 * Note, once again the Adobe documentation is wrong. In this case clearly
 * wrong, since the documented algorithm is not the inverse of the documented
 * algorithm for calculating the /O key.
 *
 * The documented algorithm (3.7, page 19 of the June PDF14Deltas document) :
 *
 * 1) Compute an encryption key from the supplied password string
 * 2) Decrypt the value of the /O key in the encryption dictioanry.
 * 3) Repeat 19 times, take the previous rsult of the RC4 decryption
 *      (on first pass, use the /O value), and create an RC4
 *    encryption key by XOR'ing each byte of the key from above with the
 *    loop counter. RC4 decrypt the previous result.
 * 4) The final result should be the padded user string, so check it.
 *
 *
 * And here's the algorithm which actually works :
 *
 * 1) Compute an encryption key using steps 1 - 3 of the algorithm for
 *     computing the /O value
 *
 * 2) Decrypt the /O encryption entry.  For revision 3, repeat 19 times,
 *    take the previous rsult of the RC4 decryption, and create an RC4
 *    encryption key by XOR'ing each byte of the key from above with the
 *    loop counter. RC4 decrypt the previous result.
 *
 * 3) The final result should be the padded user string, so check it.
 */

static Bool check_owner_password(
      PDF_CRYPTO_INFO * pci)
{
  /* overkill currently by 16 bytes, in some uses, but good uniformity */
  uint8 hashtemp[ PADDED_PASSWORD_LEN ];
  uint8 temp[ PADDED_PASSWORD_LEN ];
  uint8 temp2[ PADDED_PASSWORD_LEN ];
  uint8 Step1Copy[ PADDED_PASSWORD_LEN ];

  MD5_CTX Hash;
  int32   i,j;
  uint32  len;
  uint8   digest[ 16 ];
  RC4_KEY rc4key ;

  HQASSERT( pci , "pci is NULL" ) ;

  len = pci->passwordlen;
  if ( len > PWMAXSIZE )
    len = PWMAXSIZE;

  /* PDF 1.6 - Algorithm 3.3 step 1 and 2 */
  (void) MD5_Init( &Hash );
  (void) MD5_Update( &Hash, pci->password, len );
  (void) MD5_Update( &Hash, padstring , PWMAXSIZE - len );

  len = pci->owner_len;
  if ( len > PADDED_PASSWORD_LEN ){
    HQFAIL( "pci->owner_len > PADDED_PASSWORD_LEN" );
    return FALSE ;
  }

  switch ( pci->revision ) {
  case 2:
    (void) MD5_Final( digest, &Hash );
    RC4_set_key (&rc4key, 5, digest) ;
    RC4 (&rc4key, len, pci->owner, temp );
    break;

  case 3:
  case 4:
    /* PDF 1.6 - Algorithm 3.3 step 3 */
    for ( j = 0; j < HASHLOOPCOUNT_REV3; j++ ) {
      (void) MD5_Final( digest, &Hash );
      (void) MD5_Init( &Hash );
      (void) MD5_Update( &Hash, digest, sizeof ( digest ) );
    }

    (void) MD5_Final( hashtemp, &Hash );
    {
      uint32  loop = 0;
      while ( loop < len ) {
        temp[ loop ] = pci->owner[ loop ];
        loop++;
      }
    }

    for ( j = CRYPTLOOPCOUNT_REV3; j >= 0; j-- ) {
      /*
       * 3.1 Generate new RC4 key by XOring the original
       */
      HqMemCpy( Step1Copy, hashtemp, 16 );
      for ( i = 0; i < 16; i++ )
        Step1Copy[ i ] ^= j;

      RC4_set_key (&rc4key, 16, Step1Copy) ;
      RC4 (&rc4key, len, temp, temp2 );

      HqMemCpy( temp, temp2, len );
    }

    break;
  }
  return check_user_password( pci, temp, len );
}

static Bool pki_load_private_key_files(
      PDF_CRYPTO_INFO *pci,
      OBJECT *private_key_files,
      OBJECT *private_key_passwords)
{
  uint32 i , len ;
  OBJECT *olist_filenames, *olist_passwords;

  HQASSERT( pci , "pci is NULL" ) ;
  HQASSERT( pci->num_pki_detail == 0 , "pci num_pki_detail is not zero" ) ;
  HQASSERT( pci->pki_detail == NULL , "pci pki_detail is not NULL" ) ;
  HQASSERT( ! pci->found_valid_password, "pci->found_valid_password already TRUE" ) ;

  if ( private_key_files == NULL )
    return detail_error_handler(UNDEFINED,
                                "No private keys specified to decrypt file.") ;

  if ( private_key_passwords == NULL )
    return detail_error_handler(UNDEFINED,
                                "No private key passwords specified to decrypt private keys.") ;

  /* check the types are the same */
  if (oType(*private_key_files) != oType(*private_key_passwords))
    return detail_error_handler(UNDEFINED,
                                "Private key files is not the same as private key passwords.") ;

  /* if we have arrays, check that the length is the same */
  if (oType(*private_key_files) == OARRAY || oType(*private_key_files) == OPACKEDARRAY)
    if (theLen(*private_key_files) != theLen(*private_key_passwords))
      return detail_error_handler(UNDEFINED,
                                  "Private key files/passwords array is not the same length.") ;

  /* check that the array is not empty */
  if (theLen(*private_key_files) == 0)
    return detail_error_handler(UNDEFINED,
                                "No private key files specified to decrypt file.") ;

  switch (oType(*private_key_files)) {
  case OSTRING:
    if ((pci->pki_detail = mm_alloc( mm_pool_temp, sizeof(PKI_detail *),
                                     MM_ALLOC_CLASS_PDF_CRYPTO_INFO)) == NULL)
      return error_handler( VMERROR ) ;
    pci->num_pki_detail = 1 ;

    if (! pki_load_private(pci->pki_context,
                           oString(*private_key_files), theLen(*private_key_files),
                           oString(*private_key_passwords), theLen(*private_key_passwords),
                           &(pci->pki_detail[0]))) {
      return detail_error_handler(UNDEFINED,
                                  "Unable to load private keys.") ;
    }

    return TRUE ;
  case OARRAY:
  case OPACKEDARRAY:
    olist_filenames = oArray( *private_key_files ) ;
    olist_passwords = oArray( *private_key_passwords ) ;
    len = theLen(*private_key_files) ;

    HQASSERT(len > 0, "len is not > zero") ;

    if ((pci->pki_detail = mm_alloc( mm_pool_temp, sizeof(PKI_detail *) * len,
                                     MM_ALLOC_CLASS_PDF_CRYPTO_INFO)) == NULL)
      return error_handler( VMERROR ) ;
    pci->num_pki_detail = len ;

    /* initialise so we can de-allocate on error */
    for ( i = 0 ; i < len ; i++ ) {
      pci->pki_detail[i] = NULL ;
    }

    for ( i = 0 ; i < len ; i++ ) {
      /* check that we have an array of strings */
      if ( oType( olist_filenames[i] ) != OSTRING ||
           oType( olist_passwords[i] ) != OSTRING) {
        return error_handler( TYPECHECK ) ;
      }

      if (! pki_load_private(pci->pki_context,
                             oString(olist_filenames[i]), theLen(olist_filenames[i]),
                             oString(olist_passwords[i]), theLen(olist_passwords[i]),
                             &(pci->pki_detail[i]))) {

        return detail_error_handler(UNDEFINED,
                                    "Unable to load private keys.") ;
      }
    }
    return TRUE;
  default:
    return detail_error_handler(TYPECHECK,
                                "Password object is neither an array nor a string.") ;
  }
  /*NOTREACHED*/
}

/* ============================================================================
 * Functions to check and enumerate over the list of passwords.
 */
static void pdf_check_owner_password(
      PDF_CRYPTO_INFO *pci,
      uint8 *password,
      int32 len)
{
  HQASSERT( pci , "pci is NULL" ) ;
  HQASSERT( len >= 0 , "len is negative" ) ;

  pci->password = password ;
  pci->passwordlen = len ;

  if ( check_owner_password( pci ) )
    pci->found_valid_password = TRUE ;
}

static void pdf_check_user_password(
      PDF_CRYPTO_INFO *pci,
      uint8 *password,
      int32 len)
{
  HQASSERT( pci , "pci is NULL" ) ;
  HQASSERT( len >= 0 , "len less than zero" ) ;

  if ( check_user_password( pci, password, len ) )
    pci->found_valid_password = TRUE ;
}

static Bool pdf_check_x_passwords(
      PDF_CRYPTO_INFO *pci,
      void (*password_checking_function)( PDF_CRYPTO_INFO *pci,
      uint8 *password, int32 len ),
      OBJECT *pwlist)
{
  int i , len ;
  OBJECT *olist;

  HQASSERT( pci , "pci is NULL" ) ;
  HQASSERT( password_checking_function, "password_checking_function is NULL" ) ;
  HQASSERT( ! pci->found_valid_password, "pci->found_valid_password already TRUE" ) ;

  if ( pwlist == NULL )
    return TRUE;

  switch ( oType(*pwlist)) {
    case OSTRING:
      (*password_checking_function)( pci , oString( *pwlist ) , theLen(*pwlist));
      return TRUE ;
    case OARRAY:
    case OPACKEDARRAY:
      olist = oArray( *pwlist );
      len = theLen(*pwlist);
      for ( i = 0 ; i < len ; i++ ) {
        /* Could do recursive check here, but it isn't guaranteed to halt. */
        if ( oType( olist[i] ) != OSTRING ) {
          return detail_error_handler(TYPECHECK,
                                      "Non-string type in password array.") ;
        }
        (*password_checking_function)( pci , oString( olist[i] ) , theLen( olist[i] )) ;
        if ( pci->found_valid_password ) {
          return TRUE;
        }
      }
      return TRUE;
    default:
      return detail_error_handler(TYPECHECK,
                                  "Password object is neither an array nor a string.") ;
  }
  /*NOTREACHED*/
}

static Bool pdf_check_owner_passwords(
      PDF_CRYPTO_INFO *pci,
      OBJECT *pwlist)
{
  HQASSERT( pci , "pci is NULL" ) ;
  return pdf_check_x_passwords( pci, pdf_check_owner_password, pwlist );
}

static Bool pdf_check_user_passwords(
      PDF_CRYPTO_INFO *pci,
      OBJECT *pwlist)
{
  HQASSERT( pci , "pci is NULL" ) ;
  return pdf_check_x_passwords( pci, pdf_check_user_password, pwlist );
}

static void pki_generate_key(
      uint8 *decrypted_envelope,
      uint32 decrypted_envelope_len,
      OBJECT *recipients,
      Bool encrypt_metadata,
      uint8 *keyraw,
      uint32 *keyrawbytes)
{
  SHA_CTX Hash ;
  OBJECT *olist ;
  uint32 len, i ;
  uint8 *recipient_pkcs7 ;
  uint32 recipient_pkcs7_len ;
  uint8 metadata_special[4] ;

  UNUSED_PARAM(uint32, decrypted_envelope_len);

  HQASSERT(decrypted_envelope_len == 24, "decrypted_envelope_len not equal to 24") ;
  HQASSERT(recipients != NULL, "recipients is NULL") ;
  HQASSERT(oType(*recipients) == OARRAY, "recipients is no an array") ;

  HQASSERT(keyraw != NULL, "keyraw is NULL") ;
  HQASSERT(*keyrawbytes >= 20, "keyrawbytes less than 20") ;

  (void) SHA1_Init(&Hash) ;

  /* See PDF 1.6 specification for Public Key Encryption Algorithms */

  /* 1) passing the 20 bytes seed */
  (void) SHA1_Update(&Hash, decrypted_envelope, 20) ;

  /* 2) The bytes of each item in the Recipients array of PKCS#7
     objects, in the order in which they appear in the array. */
  olist = oArray( *recipients );
  len = theLen(*recipients);

  for (i=0; i < len; i++) {
    /* May seem like a waste to extract into variables, but I need
       some sort of sanity. */
    recipient_pkcs7_len = theLen( olist[i] ) ;
    recipient_pkcs7 = (uint8*)oString( olist[i] ) ;
    (void) SHA1_Update(&Hash, recipient_pkcs7, recipient_pkcs7_len) ;
  }

  /* 3) passing 0xFFFFFFFF if metadata is left as plaintext */
  if (! encrypt_metadata) {

    metadata_special[0] = metadata_special[1] =
          metadata_special[2] = metadata_special[3] = 0xff;

    (void) SHA1_Update(&Hash, metadata_special, 4) ;
  }

  *keyrawbytes = 20;

  (void) SHA1_Final(keyraw, &Hash) ;
}

/* ============================================================================
 * Functions to read encryption dictionary and try a bunch of passwords in an
 * attempt to discover the decryption key.
 */

enum { CFD_Type = 0, CFD_CFM, CFD_Length, CFD_AuthEvent, CFD_Recipients,
      CFD_EncryptMetadata, CFD_max } ;

static NAMETYPEMATCH crypt_filter_dict[CFD_max + 1] = {
  { NAME_Type      | OOPTIONAL       , 2, { ONAME, OINDIRECT }},
  { NAME_CFM       | OOPTIONAL       , 2, { ONAME, OINDIRECT }},
  { NAME_Length    | OOPTIONAL       , 3, { OREAL, OINTEGER, OINDIRECT }},
  { NAME_AuthEvent | OOPTIONAL       , 2, { ONAME, OINDIRECT }},
  { NAME_Recipients      | OOPTIONAL , 3, { OARRAY, OSTRING, OINDIRECT }},
  { NAME_EncryptMetadata | OOPTIONAL , 2, { OBOOLEAN, OINDIRECT }},

  DUMMY_END_MATCH
} ;

/* This structure is purely so that I can pass multiple structure
   pointers into the dictionary walk callback. */
struct crypt_callback_ctx {
  /* Used for dictionary walk - allow negative values for assert within
   * crypt_strider(), hence int32 rather than uint32 */
  int32 counter ;
  PDFCONTEXT *pdfc ;
  PDF_CRYPTO_INFO *pci ;
} ;

Bool process_recipients(
      PDF_CRYPTO_INFO *pci,
      OBJECT *recipients)
{
  OBJECT *olist ;
  uint32 len, i, j ;
  uint32 envelope_len ;
  uint8 *envelope ;
  uint8 *decrypted_envelope = NULL ;
  uint32 decrypted_envelope_len = 0 ;
  Bool matched = FALSE ;

  if ( recipients == NULL )
    return detail_error_handler(TYPECHECK,
                                "No recipients in recipient list.") ;

  if (oType(*recipients) != OARRAY)
    return detail_error_handler(TYPECHECK,
                                "Recipient field is not an array.") ;

  olist = oArray( *recipients );
  len = theLen(*recipients);

  /* Look for first recipient enveloped data we can decrypt within
     this job by trying all SW registered private keys. */

  /* for each private key */
  for (j=0; j < pci->num_pki_detail; j++) {
    /* for each recipient */
    for (i=0; i < len; i++) {
      if ( oType( olist[i] ) != OSTRING )
        return detail_error_handler(TYPECHECK,
                                    "Recipient is not a string.") ;

      envelope_len = theLen(olist[i]) ;
      envelope = (uint8*)oString(olist[i]) ;

      /* Did an error occur while trying to unpack the recipient entry */
      if (! pki_decrypt_enveloped_data(pci->pki_context,
                                       pci->pki_detail[j], envelope,
                                       envelope_len, &decrypted_envelope,
                                       &decrypted_envelope_len, &matched))
        return detail_error_handler(UNDEFINED,
              "Error occured while attempting to decrypt envelope data.") ;

      if (matched) /* if we found a match */
        break ;
    }
    if (matched) /* if we found a match */
      break ;
  }

  /* We only need to de-allocate the decrypted_envelope if a match was
     successful. */
  if (! matched)
    return detail_error_handler( UNDEFINED,
                                 "Unable to find private key for any recipient.") ;

  /** \todo The content should contain 20 bytes seed and 4 bytes
     permission information.

     The spec also says: Permissions not present when PKCS#7
     object is referenced from Crypt filter decode parameter
     dictionary which implys the recipients array can be defined in
     the /Crypt filters. However, we have not seen any of this kind
     of PDF file yet. */
  if (decrypted_envelope_len != 24) {
    pki_free(decrypted_envelope) ;
    return detail_error_handler( UNDEFINED,
                                 "Enveloped data length is not equal to 24." ) ;
  }

  pci->permission = (decrypted_envelope[20] << 24) |
                    (decrypted_envelope[21] << 16) |
                    (decrypted_envelope[22] << 8)  |
                     decrypted_envelope[23] ;

  pci->keyrawbytes = MAX_KEY_RAW ;

  /* calculate the key */
  pki_generate_key(decrypted_envelope, decrypted_envelope_len, recipients,
                   pci->encrypt_metadata, pci->keyraw, &(pci->keyrawbytes)) ;

  pki_free(decrypted_envelope) ;

  return TRUE ;
}


Bool crypt_strider(
      OBJECT *key,
      OBJECT *value,
      void *data)
{
  struct crypt_callback_ctx *temp = data ;
  PDFCONTEXT *pdfc ;
  PDF_CRYPTO_INFO *pci ;
  int32 crypt_counter ;

  HQASSERT(temp != NULL, "temp is NULL") ;

  /* Setup what we need. */
  pdfc = temp->pdfc ;
  pci = temp->pci ;
  crypt_counter = --(temp->counter) ;
  temp = NULL ; /* Don't use any more - so clear it. */

  HQASSERT(crypt_counter >= 0, "crypt_counter is less than zero") ;

  if ( oType(*key) != ONAME ||
       oType(*value) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  if ( ! pdf_dictmatch( pdfc, value, crypt_filter_dict ) )
    return FALSE ;

  /* Get and store the crypt filter name. */
  {
    NAMECACHE *crypt_name_namecache ;
    /* Because these are in the name cache, they last the lifetime of the
       PDF job. */
    crypt_name_namecache = oName( *key ) ;
    pci->cf_filter[crypt_counter].cf_name = theICList( crypt_name_namecache ) ;
    pci->cf_filter[crypt_counter].cf_name_len = theINLen( crypt_name_namecache ) ;
  }

  /* Get /Type - if it exists  - we do nothing with this - just check its value. */
  if ( crypt_filter_dict[ CFD_Type ].result != NULL ) {
    OBJECT    *filter_type = crypt_filter_dict[ CFD_Type ].result ;
    NAMECACHE *filter_type_namecache ;
    int32     check_length ;
    uint8     *check_string ;

    filter_type_namecache = oName( *filter_type ) ;
    check_string = theICList( filter_type_namecache ) ;
    check_length = theINLen( filter_type_namecache ) ;

    if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("CryptFilter") ) != 0 ) {
      return error_handler(RANGECHECK) ;
    }
  }

  /* Get /CFM - if it exists */
  if ( crypt_filter_dict[ CFD_CFM ].result != NULL ) {
    OBJECT    *filter_cfm = crypt_filter_dict[ CFD_CFM ].result ;
    NAMECACHE *filter_cfm_namecache ;
    int32     check_length ;
    uint8     *check_string ;

    filter_cfm_namecache = oName( *filter_cfm ) ;
    check_string = theICList( filter_cfm_namecache ) ;
    check_length = theINLen( filter_cfm_namecache ) ;

    if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("None") ) == 0 ) {
      pci->cf_filter[crypt_counter].cf_cfm = CF_ET_None ;
    } else if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("V2") ) == 0 ) {
      pci->cf_filter[crypt_counter].cf_cfm = CF_ET_V2 ;
    } else if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("AESV2") ) == 0 ) {
      pci->cf_filter[crypt_counter].cf_cfm = CF_ET_AESV2 ;
    } else {
      return detail_error_handler( RANGECHECK,
                                   "Crypt filter has invalid CFM." ) ;
    }
  } else {
    pci->cf_filter[crypt_counter].cf_cfm = CF_ET_None ; /* default if not specified */
  }

  /* Get /AuthEvent - if it exists - we do nothing with this - just check its value. */
  if ( crypt_filter_dict[ CFD_AuthEvent ].result != NULL ) {
    OBJECT    *filter_authevent = crypt_filter_dict[ CFD_AuthEvent ].result ;
    NAMECACHE *filter_authevent_namecache ;
    int32     check_length ;
    uint8     *check_string ;

    filter_authevent_namecache = oName( *filter_authevent ) ;
    check_string = theICList( filter_authevent_namecache ) ;
    check_length = theINLen( filter_authevent_namecache ) ;

    if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("DocOpen") ) == 0 ) {
    } else if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("EFOpen") ) == 0 ) {
    } else {
      return detail_error_handler( RANGECHECK,
                                   "Crypt filter has invalid AuthEvent." ) ;
    }
  }

  /* NOTE: PDF file generated by Acrobat 6 has /Length to be 16, which is not
   * per the spec, the PDF1.5 spec says (page 107, Table 3.22):
   *
   * Length integer (Optional) When the value of CFM is V2, this entry is used
   * to indicate the bit length of the decryption key. It must be a multiple of
   * 8 in the range of 40 to 128. Default value: 128.
   *
   * The /Length key defined in /CF dictionary is now ignored.
   */

  /* Get /Length - if it exists */
  {
    uint32 keylen ;
    if ( crypt_filter_dict[ CFD_Length ].result != 0  )
      keylen = oInteger( *(crypt_filter_dict[ CFD_Length ].result) ) ;
    else
      keylen = 16 ; /* default */

    /* Key length seems to be coming through as a byte count which is
       not what the spec. says! */
#if 0
    if ((keylen & 7) || keylen < 40 || keylen > 128)
      return detail_error_handler( RANGECHECK,
                                   "Crypt filter Length is out of range." ) ;
#endif
    /* Max we can handle is 128 bit. We don't know if the length is
       expressed as a byte or bit count yet. */

    /* If more 16 bytes assume its a bit count so convert it to a byte
       count. */
    if (keylen > 16)
      keylen >>= 3 ;

    /* We are limiting to 128 bits. */
    if (keylen > 16)
      keylen = 16;

#if 0
    if (keylen < 5)
      return detail_error_handler( RANGECHECK,
                                   "Crypt filter Length is out of range." ) ;
#endif

    pci->cf_filter[crypt_counter].cf_keyrawbytes = keylen ;
  }

  if (pci->sc_handler != PCI_ET_Standard) {
    /* Additional public key encryption fields - PDF 1.6 table 3.24 */

    /* Get /Recipients - required */
    if ( crypt_filter_dict[ CFD_Recipients ].result != NULL ) {

      /* process_recipients raises PS error. */
      if (! process_recipients(pci, crypt_filter_dict[ CFD_Recipients ].result))
        return FALSE ;

    } else {
      return detail_error_handler( TYPECHECK,
                                   "Missing /Recipients field." ) ;
    }

    pci->cf_filter[crypt_counter].cf_encrypt_metadata = TRUE ;
    if (crypt_filter_dict[ CFD_EncryptMetadata ].result != NULL)
      pci->cf_filter[crypt_counter].cf_encrypt_metadata = oBool( *(crypt_filter_dict[ CFD_EncryptMetadata ].result) ) ;
  }

  return TRUE ;
}

static void pdf_end_decryption_internal(
      PDF_CRYPTO_INFO **pci)
{
  PDF_CRYPTO_INFO *crypt_info ;
  uint32 i ;

  HQASSERT(pci != NULL, "pci is NULL") ;

  crypt_info = *pci ;

  if (crypt_info == NULL)
    return ;

  if (crypt_info->num_pki_detail > 0) {
    for (i=0; i < crypt_info->num_pki_detail; i++) {
      pki_unload_private(crypt_info->pki_context,
                         &(crypt_info->pki_detail[i])) ;
    }
    mm_free(mm_pool_temp, crypt_info->pki_detail,
            sizeof(PKI_detail *) * crypt_info->num_pki_detail) ;
  }

  /* tear down PKI if it was used */
  if (crypt_info->pki_context != NULL) {
    pki_terminate(&(crypt_info->pki_context)) ;
    HQASSERT(crypt_info->pki_context == NULL,
             "crypt_info pki_context is not NULL") ;
  }

  if (crypt_info->num_crypt_filters > 0) {
    mm_free(mm_pool_temp, crypt_info->cf_filter,
            sizeof(encryption_crypt_filter) * crypt_info->num_crypt_filters) ;
  }

  mm_free(mm_pool_temp, crypt_info, sizeof(PDF_CRYPTO_INFO)) ;
  *pci = NULL ;
}

void pdf_end_decryption(
      PDFCONTEXT *pdfc)
{
  PDFXCONTEXT *pdfxc ;
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  pdf_end_decryption_internal(&(pdfxc->crypt_info)) ;
}

enum { EED_Filter = 0, EED_SubFilter, EED_V, EED_Length,
       EED_CF, EED_StmF, EED_StrF, EED_EFF, EED_R, EED_O, EED_U,
       EED_P, EED_EncryptMetadata, EED_Recipients, EED_Max  } ;

/*
 From the PDF1.6 spec page 94
"Unlike strings within the body of the document, those in the encryption dictionary
must be direct objects. The contents of the encryption dictionary are not encrypted
by the usual methods (the algorithm specified by the V entry). Security
handlers are responsible for encrypting any data in the encryption dictionary that
they need to protect."
*/

static NAMETYPEMATCH encryption_dict[EED_Max + 1] = {
  { NAME_Filter ,                     2, { ONAME,OINDIRECT }},
  { NAME_SubFilter | OOPTIONAL ,      2, { ONAME,OINDIRECT }},

  { NAME_V | OOPTIONAL ,              3, { OREAL,OINTEGER,OINDIRECT }},
  { NAME_Length | OOPTIONAL ,         3, { OREAL,OINTEGER,OINDIRECT }},

  /* Crypt filters */
  { NAME_CF | OOPTIONAL ,             2, { ODICTIONARY,OINDIRECT }},
  { NAME_StmF | OOPTIONAL ,           2, { ONAME,OINDIRECT }},
  { NAME_StrF | OOPTIONAL ,           2, { ONAME,OINDIRECT }},
  { NAME_EFF | OOPTIONAL ,            2, { ONAME,OINDIRECT }},

  /* Standard encryption fields. */
  { NAME_R | OOPTIONAL,               3, { OREAL,OINTEGER,OINDIRECT }},
  { NAME_O | OOPTIONAL,               1, { OSTRING }},
  { NAME_U | OOPTIONAL,               1, { OSTRING }},
  { NAME_P | OOPTIONAL,               2, { OINTEGER,OINDIRECT }},

  { NAME_EncryptMetadata | OOPTIONAL, 2, { OBOOLEAN,OINDIRECT }},

  /* Public key encryption fields. */
  { NAME_Recipients | OOPTIONAL , 2, { OARRAY,OINDIRECT }},

  DUMMY_END_MATCH
} ;

static Bool pdf_begin_decryption_internal(
      PDFCONTEXT *pdfc,
      OBJECT *encryption,
      OBJECT *file_id,
      PDF_CRYPTO_INFO **new_pci)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *string ;
  int32 permissions ;
  OBJECT *file_id_first_part ;
  PDF_CRYPTO_INFO *pci ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pdfc != NULL, "pdfc is NULL" ) ;
  HQASSERT( pdfxc != NULL, "pdfxc is NULL" ) ;
  HQASSERT( encryption, "encryption is NULL" ) ;
  HQASSERT( new_pci != NULL, "new_pci is NULL" ) ;

  if (pdfxc->crypt_info) {
    *new_pci = pdfxc->crypt_info;
    return TRUE;
  }
  *new_pci = NULL ;

  /* Figure out what's in the encryption dictionary. */
  if (oType(*encryption) == OINDIRECT) {
     OBJECT *otmp;
     Hq32x2 file_pos;
     FILELIST *flptr = pdfxc->flptr ;
     DEVICELIST *dev = theIDeviceList(flptr) ;

     if ( (*theIMyFilePos(flptr))(flptr, &file_pos) == EOF )
       return FALSE ;

     if (! pdf_lookupxref(pdfc, & otmp, oXRefID( *encryption ),
                          theGen(*encryption), FALSE))
       return FALSE ;

     if ((*theIMyResetFile(flptr))(flptr) == EOF)
       return FALSE ;
     if (! (*theISeekFile(dev))(dev, theIDescriptor(flptr),
                                &file_pos, SW_SET))
       return FALSE ;

     if (otmp == NULL)
       return detail_error_handler(UNDEFINEDRESOURCE ,
                                   "Cross reference to encryption dictionary failed.") ;

     encryption = otmp;
  }

  /* Is it a dictionary? */
  if (oType( *encryption ) != ODICTIONARY)
    return detail_error_handler(TYPECHECK,
         "Encryption dictionary is incorrect type.") ;

  /* Look up the entries we're expecting */
  if (! pdf_dictmatch(pdfc, encryption, encryption_dict))
    return FALSE ;

  /* Check the ID key (from the Trailer dict) */
  if (file_id == NULL)
    return detail_error_handler(TYPECHECK,
                                "Encrypted file has no ID.") ;

  HQASSERT( oType( *file_id ) == OARRAY || oType( *file_id ) == OPACKEDARRAY ,
      "file_id not array or packed array in pdf_check_passwords" ) ;

  if ( theLen(*file_id) != 2 )
    return detail_error_handler(RANGECHECK,
                                "Encryption file ID doesn't have two components." ) ;

  file_id_first_part = oArray( *file_id ) /* + 0 to get first element */ ;
  if ( oType( *file_id_first_part ) != OSTRING )
    return detail_error_handler(TYPECHECK,
                                "Encryption file ID first element is not a string." ) ;

  if ((pci = mm_alloc( mm_pool_temp, sizeof(PDF_CRYPTO_INFO),
                       MM_ALLOC_CLASS_PDF_CRYPTO_INFO)) == NULL)
    return error_handler( VMERROR ) ;

  /* defaults */
  HqMemZero((uint8 *)pci, sizeof(PDF_CRYPTO_INFO)) ; /* init struct */
  pci->encrypt_metadata = TRUE ;

  /* Get /Filter */
  {
    OBJECT    *filter_name = encryption_dict[ EED_Filter ].result ;
    NAMECACHE *filter_namecache ;
    int32     check_length ;
    uint8     *check_string ;

    pci->sc_handler = PCI_ET_unknown ;

    if ( filter_name == NULL )
      return detail_error_handler(TYPECHECK,
                                  "Encryption type is not a name." ) ;

    filter_namecache = oName( *filter_name ) ;
    check_string = theICList( filter_namecache ) ;
    check_length = theINLen( filter_namecache ) ;

    if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("Standard") ) == 0 ) {
      pci->sc_handler = PCI_ET_Standard ;
    } else if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("Adobe.PubSec") ) == 0 ) {
      pci->sc_handler = PCI_ET_Adobe_PubSec ;
    } else {
      return detail_error_handler(TYPECHECK,
                                  "Encryption type is not supported." ) ;
    }
  }

  /* Get /SubFilter - if it exists */
  {
    OBJECT    *subfilter_name = encryption_dict[ EED_SubFilter ].result;
    NAMECACHE *subfilter_namecache ;
    int32     check_length ;
    uint8     *check_string ;

    pci->sub_filter = PCI_ET_unknown ;

    if (subfilter_name != NULL) {
      subfilter_namecache = oName( *subfilter_name ) ;
      check_string = theICList( subfilter_namecache ) ;
      check_length = theINLen( subfilter_namecache ) ;

      if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("adbe.pkcs7.s3") ) == 0 ) {
        pci->sub_filter = PCI_ET_adbe_pkcs7_s3;
      } else if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("adbe.pkcs7.s4") ) == 0 ) {
        pci->sub_filter = PCI_ET_adbe_pkcs7_s4;
      } else if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("adbe.pkcs7.s5") ) == 0 ) {
        pci->sub_filter = PCI_ET_adbe_pkcs7_s5;
      } else {
        return detail_error_handler(TYPECHECK,
                                    "Encryption type is not supported." ) ;
      }
    }
  }

  pci->version = 0; /*default value*/

  /* Get /V field  - if it exists */
  if ( encryption_dict[ EED_V ].result != 0  )
    pci->version = oInteger( *(encryption_dict[ EED_V ].result) ) ;

  switch (pci->version) {
    case 1:
      /* Version 1 = RC4 with fixed 40 bit key length */
      pci->keybytes = 5;
      break;
    case 2:
      /* introduced in PDF1.5 for crypt filter */
      /* the PDF 1.5 spec says the /Length is for /V = 2,3 only, if /V is 4, the /Length
       * should be defined in the crypt filter dictionary, however, the /Length defined
       * there appears not following the spec (in some cases, it is 16, which is not in
       * the range of 40-128, probabaly it is in byte not in bit as spec clearly states.)
       * However, the /Length sounds always defined here, let us try to use this /Length.
       */
      /** \todo FIXME: this may need to be revisited for a newer Acrobat.
       */
    case 4:
      /* Version 2 = RC4 with variable key length */

      /* Get /Length - option - V2 & V3*/
      {
        uint32 keylen ;
        if ( encryption_dict[ EED_Length ].result != 0  )
          keylen = oInteger( *(encryption_dict[ EED_Length ].result) ) ;
        else
          keylen = 40 ;

        if ((keylen & 7) || keylen < 40 || keylen > 128)
          return error_handler( RANGECHECK ) ;

        keylen >>= 3 ;

        pci->keybytes = keylen ;
      }
      break;

    case 3:
      /* Version 3 is undocumented due to US trade restrictions */
      return detail_error_handler(UNDEFINED,
                                  "US restricted encryption is not supported." ) ;

    default:
      return detail_error_handler(UNDEFINED,
                                  "Unknown encryption version." ) ;
  }

  /* Get passwords or load private keys before continueing */
  if (pci->sc_handler == PCI_ET_Standard) {

    /*  Get /R field - required */
    if (encryption_dict[ EED_R ].result != NULL) {
      pci->revision = oInteger( *(encryption_dict[ EED_R ].result) ) ;
    } else {
      return detail_error_handler(TYPECHECK,
                                  "Missing /R field." ) ;
    }

    if (pci->revision < 2 || pci->revision > 4) {
      return error_handler( RANGECHECK ) ;
    }

    /* Get /O field - required */
    string = encryption_dict[ EED_O ].result ;
    if ( string != NULL) {
      if ( theLen(*string) != O_FIELD_LEN ) {
        return detail_error_handler(RANGECHECK,
                                    "Encryption dictionary O field is not 32 bytes long." ) ;
      }
      pci->owner = oString( *string ) ;
      pci->owner_len = O_FIELD_LEN;
    } else {
      return detail_error_handler(TYPECHECK,
                                  "Missing /O field." ) ;
    }

    /* Get /U field - required */
    string = encryption_dict[ EED_U ].result ;
    if ( string != NULL) {
      if ( theLen(*string) != U_FIELD_LEN ) {
        return detail_error_handler(RANGECHECK,
                                    "Encryption dictionary U field is not 32 bytes long." ) ;
      }
      pci->user = oString( *string ) ;
      pci->user_len = U_FIELD_LEN;
    } else {
      return detail_error_handler(TYPECHECK,
                                  "Missing /U field." ) ;
    }

    /* Get /P field - required */
    if (encryption_dict[ EED_P ].result != NULL) {
      permissions = oInteger( *(encryption_dict[ EED_P ].result) ) ;
      pci->permission = permissions ;
    } else {
      return detail_error_handler(TYPECHECK,
                                  "Missing /P field." ) ;
    }

  } else { /* PUBLIC ENCRYPTION */
    /* If we have public keys, load the private keys before processing
       the crypt filters. */

    {
      PKI_callbacks cb = { pki_malloc, pki_free, pki_open, pki_read, pki_close } ;
      if (! pki_init(&(pci->pki_context), &cb)) /* Need PKI processing capability */
        return FALSE ;

      HQASSERT(pci->pki_context != NULL, "pci pki_context is NULL") ;
    }

    if (! pki_load_private_key_files(pci, ixc->private_key_files_global,
                                     ixc->private_key_passwords_global)) {
      if (! pki_load_private_key_files(pci, ixc->private_key_files_local,
                                       ixc->private_key_passwords_local)) {
        return FALSE ;
      }
    }
  }

  /* do we have crypt filter? */
  if (pci->version == 4) {

    /* Get /CF field - optional, V4 only */
    if ( encryption_dict[ EED_CF ].result != NULL ) {
      struct crypt_callback_ctx cb_ctx ;
      OBJECT *crypt_filters = encryption_dict[ EED_CF ].result ;
      int32 dict_size ;

      HQASSERT(oType(*crypt_filters) == ODICTIONARY,
               "Dictmatch fail on crypt filter dictionary.") ;

      getDictLength( dict_size, crypt_filters ) ;
      pci->num_crypt_filters = dict_size ;

      pci->cf_filter = mm_alloc( mm_pool_temp ,
                                sizeof(encryption_crypt_filter) * dict_size,
                                MM_ALLOC_CLASS_PDF_CRYPT_FILTERS ) ;
      if (pci->cf_filter == NULL)
        return error_handler(VMERROR) ;

      /* Setup callback context for walking the crypt filter dictionary. */
      cb_ctx.counter = pci->num_crypt_filters ;
      cb_ctx.pci = pci ;
      cb_ctx.pdfc = pdfc ;

      /* enumerate over the crypt filter entries - strider callback
         sets error if one exists */
      if (! walk_dictionary( crypt_filters,
                             /* Strider in Lord Of The Rings walked a lot. */
                             crypt_strider,
                             &cb_ctx ) )
        return FALSE ;
    }

    /* for password security handler, the PDF generated by Acrobat 6 has
     * /EncryptMetadata defined in the encrytion dictionary, unlike the
     * public-key security handler, in which the /EncryptMetadata is
     * defined in the crypt filter dictionary and is per the spec.
     *
     * Note: there is no mention of having /EncryptMetadata in password
     * security handler but it does exist in the PDF file encrypted by
     * Acrobat 6 if you choose leaving metadata as plain text.
     */

    /* Get /EncryptMetadata - if it exists */
    if (encryption_dict[ EED_EncryptMetadata ].result != NULL)
      pci->encrypt_metadata = oBool( *(encryption_dict[ EED_EncryptMetadata ].result) ) ;

    /* Get /StmF field - optional, V4 only */
    {
      OBJECT    *stmf_name = encryption_dict[ EED_StmF ].result;
      NAMECACHE *stmf_namecache ;
      int32     check_length ;
      uint8     *check_string ;

      pci->default_stm = NULL ; /* NULL means Identity */

      if (stmf_name != NULL) {
        stmf_namecache = oName( *stmf_name ) ;
        check_string = theICList( stmf_namecache ) ;
        check_length = theINLen( stmf_namecache ) ;

        if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("Identity") ) != 0 ) {
          /* Go through crypt dictionary looking for this name. */
          uint32 i ;
          Bool found = FALSE ;
          for (i=0; i< pci->num_crypt_filters; i++) {
            if ( HqMemCmp( check_string , check_length , pci->cf_filter[i].cf_name, pci->cf_filter[i].cf_name_len ) == 0 ) {
              pci->default_stm = &(pci->cf_filter[i]) ;
              found = TRUE ;
            }
          }
          if (! found)
            return error_handler( RANGECHECK ) ;
        }
      }
    }

    /* Get /StrF field - optional, V4 only  */
    {
      OBJECT    *strf_name = encryption_dict[ EED_StrF ].result;
      NAMECACHE *strf_namecache ;
      int32     check_length ;
      uint8     *check_string ;

      pci->default_str = NULL ; /* NULL means Identity */

      if (strf_name != NULL) {
        strf_namecache = oName( *strf_name ) ;
        check_string = theICList( strf_namecache ) ;
        check_length = theINLen( strf_namecache ) ;

        if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("Identity") ) != 0 ) {
          /* Go through crypt dictionary looking for this name. */
          uint32 i ;
          Bool found  = FALSE ;
          for (i=0; i< pci->num_crypt_filters; i++) {
            if ( HqMemCmp( check_string , check_length , pci->cf_filter[i].cf_name, pci->cf_filter[i].cf_name_len ) == 0 ) {
              pci->default_str = &(pci->cf_filter[i]) ;
              found = TRUE ;
            }
          }
          if (! found)
            return error_handler( RANGECHECK ) ;
        }
      }
    }

    /* Get /EFF field - optional, V4 only  */
    {
      OBJECT    *eff_name = encryption_dict[ EED_EFF ].result;
      NAMECACHE *eff_namecache ;
      int32     check_length ;
      uint8     *check_string ;

      pci->default_eff = NULL ; /* NULL means Identity */

      if (eff_name != NULL) {
        eff_namecache = oName( *eff_name ) ;
        check_string = theICList( eff_namecache ) ;
        check_length = theINLen( eff_namecache ) ;

        if ( HqMemCmp( check_string , check_length , NAME_AND_LENGTH("Identity") ) != 0 ) {
          /* Go through crypt dictionary looking for this name. */
          uint32 i ;
          Bool found  = FALSE ;
          for (i=0; i < pci->num_crypt_filters; i++) {
            if ( HqMemCmp( check_string , check_length , pci->cf_filter[i].cf_name, pci->cf_filter[i].cf_name_len ) == 0 ) {
              pci->default_eff = &(pci->cf_filter[i]) ;
              found = TRUE ;
            }
          }
          if (! found)
            return error_handler( RANGECHECK ) ;
        }
      }
    }
  } /* End if version 4 */

  /* now file ID */
  pci->fileid = oString(*file_id_first_part) ;
  pci->fileid_len = theLen(*file_id_first_part) ;

  /* ==========================================================================
   * PASSWORD BASED DECRYPTION.
   * ==========================================================================
   */
  if (pci->sc_handler == PCI_ET_Standard) {
    pci->found_valid_password = FALSE ;

    {
      Bool search_ok = TRUE ;
      pci->matched_owner_password = FALSE ;

      /* Explicitly check empty owner password. */
      pdf_check_owner_password( pci , (uint8 *) "", 0 ) ;
      if (pci->found_valid_password)
        pci->matched_owner_password = TRUE ;

      if ( ! pci->found_valid_password && search_ok )
        search_ok = pdf_check_owner_passwords( pci , ixc->ownerpasswords_global );
      if (pci->found_valid_password)
        pci->matched_owner_password = TRUE ;

      if ( ! pci->found_valid_password && search_ok )
        search_ok = pdf_check_owner_passwords( pci , ixc->ownerpasswords_local );
      if (pci->found_valid_password)
        pci->matched_owner_password = TRUE ;

      /* Explicitly check empty user password. */
      if ( ! pci->found_valid_password && search_ok )
        pdf_check_user_password( pci , (uint8 *) "", 0 ) ;

      if ( ! pci->found_valid_password && search_ok )
        search_ok = pdf_check_user_passwords( pci , ixc->userpasswords_global );

      if ( ! pci->found_valid_password && search_ok )
        search_ok = pdf_check_user_passwords( pci , ixc->userpasswords_local );

      if ( ! search_ok )
        return error_handler( UNDEFINED ) ;
    }

    /* All passwords fail. */
    if ( ! pci->found_valid_password )
      return detail_error_handler(INVALIDACCESS,
                                  "Unable to find valid password to decrypt file." );

  /* ==========================================================================
   *  PUBLIC KEY DECRYPTION.
   * ==========================================================================
   */
  } else {
    /* Additional public key encryption fields - PDF 1.6 table 3.21 */
    if (pci->sub_filter == PCI_ET_adbe_pkcs7_s3 ||
        pci->sub_filter == PCI_ET_adbe_pkcs7_s4) {

      if (encryption_dict[ EED_Recipients ].result == NULL)
        return error_handler( TYPECHECK ) ;

      /* process_recipients raises error */
      if (! process_recipients(pci, encryption_dict[ EED_Recipients ].result))
        return FALSE ;

    } else {
      /* Recipients will have been set on crypt filters. Nothing to
         do. */
    }
  }

  /* If we did NOT match an owner password and the print permission
     flag is off, then do not allow the printing of this document. */
  if ( ! pci->matched_owner_password &&
       (pci->permission & PRINT_PERMISSIONS_MASK) == 0) {
#if defined( DEBUG_BUILD )
    if ( ixc->honor_print_permission_flag ) {
#endif
      return detail_error_handler(INVALIDACCESS,
                                  "Printing is not permitted for this file." );
#if defined( DEBUG_BUILD )
    } else {
      monitorf( (uint8 *) "======================================================================================\n" );
      monitorf( (uint8 *) "Warning: Ignoring print permissions flag.  This file will NOT print on the release RIP\n" );
      monitorf( (uint8 *) "======================================================================================\n" );
    }
#endif
  }

  *new_pci = pci ;
  pdfxc->crypt_info = pci ;

  return TRUE ;
}

Bool pdf_begin_decryption(
      PDFCONTEXT *pdfc,
      OBJECT *encryption,
      OBJECT *file_id)
{
  PDF_CRYPTO_INFO *new_pci ;
  Bool status ;

  PDF_CHECK_MC( pdfc ) ;

  status = pdf_begin_decryption_internal(pdfc, encryption, file_id, &new_pci) ;

  if (! status)
    pdf_end_decryption_internal(&new_pci) ;

  return status ;
}

/* ============================================================================
* Log stripped */
