/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:gu_hsl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * HSL & other screen definition construction.
 */

#ifndef __GU_HSL_H__
#define __GU_HSL_H__

#include "objecth.h"  /* NAMECACHE */
#include "graphict.h" /* GS_COLORinfo */

struct CHALFTONE; /* from chalftone.h */


enum {
  SPOT_FUN_RAND = 101, /**< first-generation (4-color) HDS spot function */
  SPOT_FUN_RESPI = 102, /**< HMS spot function */
  SPOT_FUN_CHAIN = 103 /**< HCS spot function */
} ;


/* lookup table for 'C' spot functions */
typedef struct {
  int32 name ;
  int32 sfindex ;
} SFLOOKUP ;


typedef struct {
  Bool respifying;
  int32 subcell_factor;
  NAMECACHE *sfname ;
  int32 nweights;
  Bool grow_from_centre;
  SYSTEMVALUE growth_rate_fudge;
  SYSTEMVALUE dark_side;
  SYSTEMVALUE mid_range;
  SYSTEMVALUE light_side;
  SYSTEMVALUE gradient;
  SYSTEMVALUE light_weight, dark_weight;
} RESPI_PARAMS;


typedef struct {
  SYSTEMVALUE *centre_weights;
  SYSTEMVALUE *corner_weights;
  int32 last_nweights; /* number of els in centre, nels x 4 in corners */
} RESPI_WEIGHTS;


typedef struct {
  int32 AR1 , AR2 , AR3 , AR4 ;
  int32 DOTS ;
  int32 apparent_DOTS ;
} SCELL_GEOM;


Bool parse_HSL_parameters(corecontext_t *context,
                          GS_COLORinfo *colorInfo,
                          struct CHALFTONE *tmp_chalftone,
                          RESPI_PARAMS *respi_params,
                          RESPI_WEIGHTS *respi_weights,
                          NAMECACHE *sfcolor,
                          Bool maybeAPatternScreen,
                          NAMECACHE **underlying_spot_function_name ,
                          int32 *detail_name ,
                          int32 *detail_index ,
                          SYSTEMVALUE *freqv0adjust,
                          SFLOOKUP sflookup[],
                          int32 sflookup_entries) ;


/** State of encryption. */
enum {
  EN_DE_CRYPT_NEXT,
  ENCRYPT_FIRST,
  DECRYPT_FIRST
} ;


/** Length of password buffer in bytes. */
#define PASS_LENGTH (16)


/** Interface to symmetric function for encrypting halftone screen caches.

    \param buffer Buffer containing data to en/de-crypt. If \c kind is \c
      ENCRYPT_FIRST or \c DECRYPT_FIRST this buffer must be 16 bytes longer
      than the CHALFTONE structure stored in it.
    \param length The length of the data buffer to en/decrypt.
    \param pass A 16-byte buffer for the encryption password. If \c kind is
      \c ENCRYPT_FIRST or \c DECRYPT_FIRST, this password buffer will be
      initialised.
    \param id_number The ID number to use to encrypt the screens. This is
      either the dongle serial number, the customer number, or a generic
      number for all OEMs.
    \param kind One of the enumeration values \c ENCRYPT_FIRST, \c
      DECRYPT_FIRST or \c EN_DE_CRYPT_NEXT. The first call will generate
      a password, concatenate it with the CHALFTONE if encrypting, and then
      en/decrypt the data. Subsequent calls will encrypt or decrypt data
      buffers.
    \return FALSE if \c DECRYPT_FIRST failed to match the decrypted password
      against the generated password. TRUE if \c ENCRYPT_FIRST or \c
      EN_DE_CRYPT_NEXT were specified, or in the first decryption matched
      passwords.
*/
Bool encrypt_halftone(uint8 * buffer, int32 length,
                      uint8 pass[PASS_LENGTH], uint16 id_number, uint8 kind) ;


#endif /* protection for multiple inclusion */

/* Log stripped */
