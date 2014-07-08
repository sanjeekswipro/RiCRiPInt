/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlhash.c(EBDSDK_P.1) $
 * $Id: src:xmlhash.c,v 1.5.10.1.1.1 2013/12/19 11:25:09 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PJW hash function.
 */

#include "core.h"

/* This is *almost* IDENTICAL to what resides in zlib so we can now think about
 * moving the hash function to another location.
 * I also notice that object naming uses something similiar, so we ought to
 * merge the 3 off them.
 */

/* Constants for PJW hash function */
#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

/**
 * \brief Compute a hash on a string.
 *
 * This an implementation of hashpjw without any branches in the loop.
 *
 * \param[in] p_string
 * Pointer to string to generate hash for.
 * \param[in] str_len
 * Length of string.
 *
 * \return
 * Unsigned 32-bit hash value for the string.
 */
uint32 xml_strhash(
/*@in@*/ /*@notnull@*/
  const uint8* p_string,
  uint32 str_len)
{
  uint32 hash = 0;
  uint32 bits = 0;

  HQASSERT((p_string != NULL), "p_string is NULL");

  while ( str_len-- > 0 ) {
    hash = (hash << PJW_SHIFT) + *p_string++;
    bits = hash&PJW_MASK;
    hash ^= bits|(bits >> PJW_RIGHT_SHIFT);
  }

  return(hash);
}

/* ============================================================================
* Log stripped */
