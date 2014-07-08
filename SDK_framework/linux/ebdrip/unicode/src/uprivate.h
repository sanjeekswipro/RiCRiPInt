/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:uprivate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode manipulation library.
 *
 * The Unicode manipulation functions provide rudimentary facilities to convert
 * between Unicode transformation representations, to compare normalised Unicode
 * sequences, and to iterate over Unicode sequences.
 */

#ifndef _UPRIVATE_H_
#define _UPRIVATE_H_

/** \brief Start of UTF-16 high surrogate range */
#define UTF16_SURROGATE_HIGH 0xd800

/** \brief Start of UTF-16 low surrogate range */
#define UTF16_SURROGATE_LOW  0xdc00

/** \brief Number of bits represented in surrogates */
#define UTF16_SURROGATE_BITS 10

/** \brief Mask for bits represented in surrogates */
#define UTF16_SURROGATE_MASK ((1 << UTF16_SURROGATE_BITS) - 1)

/** \brief Quick test to determine if a UTF-16 code unit is a surrogate */
#define UTF16_IS_SURROGATE(u) (((u) & 0xf800) == 0xd800)

/** \brief Quick test to determine if a UTF-16 code unit is a first
    surrogate. */
#define UTF16_IS_SURROGATE_HIGH(u) (((u) & 0xfc00) == 0xd800)

/** \brief Quick test to determine if a UTF-16 code unit is a second
    surrogate. */
#define UTF16_IS_SURROGATE_LOW(u) (((u) & 0xfc00) == 0xdc00)

/** \brief Length table for UTF-8 conversion

    Table indexed by a start byte, yielding the number of bytes in the UTF-8
    encoding it introduces. Zero indicates an invalid UTF-8 start byte.
 */
extern uint8 utf8_length[256] ;

/** \brief Minimum value table for UTF-8 conversion

    This is used to check the valid ranges for UTF-8 representations, to
    ensure the "shortest encoding" condition is met. Note that the Unicode
    4.0 standard does not phrase the UTF-8 requirement as "shortest encoding"
    anymore, but the syntax it provides performs the same function. This
    library simplifies the parsing by reading all forms, and then checking the
    validity. This table is indexed by the number of bytes in the UTF-8
    representation.
 */
extern UTF32 utf8_minimum[5] ;

/** \brief Mask table for UTF-8 conversion

    Masks to remove the unwanted bits from the start byte of UTF-8
    representations. This table is indexed by the number of bytes in the UTF-8
    representation.
 */
extern UTF8 utf8_mask[5] ;

/** \brief Mark table for UTF-8 conversion.

    Values ORed into first byte to indicate the number of bytes in a UTF-8
    code point.
 */
extern UTF8 utf8_marker[5] ;

/** \brief Unicode memory handler and allocation. */
extern unicode_memory_t unicode_mem_handle ;

#define unicode_alloc(size) \
  ((unicode_mem_handle.alloc_fn)(unicode_mem_handle.context, (size)))
#define unicode_realloc(mem, size) \
  ((unicode_mem_handle.realloc_fn)(unicode_mem_handle.context, (mem), (size)))
#define unicode_free(mem) \
  ((unicode_mem_handle.free_fn)(unicode_mem_handle.context, (mem)))

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
/* \brief Debug flag to turn on tracing of Unicode/ICU problems. */
extern int debug_unicode ;

/* \brief Bit set for use with debug_unicode. */
enum {
  DEBUG_UNICODE_NO_ICU = 1
} ;
#endif

#ifdef HQNlibicu
/* This include is needed to get default value of UCONFIG_NO_NORMALIZATION. */
#include "unicode/unorm.h"

#if !UCONFIG_NO_NORMALIZATION
/* \brief Lookup table from HQNc-unicode normal forms to ICU normal forms. */
extern UNormalizationMode icu_normal_form[] ;
#endif /* !UCONFIG_NO_NORMALIZATION */

HqBool unicode_icu_ready(void) ;
#endif /* HQNlibicu */

/*
Log stripped */
#endif /* protection from multiple inclusion */
