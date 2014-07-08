/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:ucompare.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode comparison.
 *
 * The Unicode comparison functions perform collation on Unicode UTF-8
 * or UTF-16 sequences.
 */

#include "std.h" /* For HQASSERT, HQFAIL */
#include "hqunicode.h"
#include "uprivate.h"
#include "hqmemcmp.h"

#ifdef HQNlibicu
/* This include is needed to get default value of UCONFIG_NO_NORMALIZATION. */
#include "unicode/unorm.h"
#endif

/* Compare a UTF-8 sequence. */
int utf8_compare(utf8_buffer *left, utf8_buffer *right,
                 int casefold, int normalform, int order)
{
  HQASSERT(casefold == UTF_CASE_UNFOLDED ||
           casefold == UTF_CASE_FOLDED,
           "Unicode case folding invalid") ;
  HQASSERT(normalform == UTF_NORMAL_FORM_NONE ||
           normalform == UTF_NORMAL_FORM_C ||
           normalform == UTF_NORMAL_FORM_D ||
           normalform == UTF_NORMAL_FORM_KC ||
           normalform == UTF_NORMAL_FORM_KD,
           "Invalid Unicode normalisation form") ;
  HQASSERT(order == UTF_ORDER_UNSPECIFIED ||
           order == UTF_ORDER_CODEPOINT,
           "Unicode ordering invalid") ;
  HQASSERT(left != NULL, "Missing UTF-8 comparee") ;
  HQASSERT(left->codeunits != NULL, "No UTF-8 comparee code units") ;
  HQASSERT(right != NULL, "Missing UTF-8 comparee") ;
  HQASSERT(right->codeunits != NULL, "No UTF-8 comparee code units") ;
  HQASSERT(utf8_validate(left->codeunits,
                         left->codeunits + left->unitlength,
                         NULL, NULL, NULL) == 0,
           "Input string to comparison is not valid UTF-8") ;
  HQASSERT(utf8_validate(right->codeunits,
                         right->codeunits + right->unitlength,
                         NULL, NULL, NULL) == 0,
           "Input string to comparison is not valid UTF-8") ;

#if defined(HQNlibicu) && !UCONFIG_NO_NORMALIZATION
  /* Convert the buffers to UTF16, so we can compare with unorm_compare(). */
  if ( unicode_icu_ready() ) {
    UErrorCode status = U_MEMORY_ALLOCATION_ERROR ;
    int32 result = 0 ;
    UTF16 *left_buffer, *right_buffer ;
    utf16_buffer left_u16, right_u16 ;
    uint32 options ;

    /* Normalisation options are shifted for the compare. */
    options = (icu_normal_form[normalform] << UNORM_COMPARE_NORM_OPTIONS_SHIFT) ;

    if ( casefold == UTF_CASE_FOLDED )
      options |= U_COMPARE_IGNORE_CASE ;

    if ( order == UTF_ORDER_CODEPOINT )
      options |= U_COMPARE_CODE_POINT_ORDER ;

    /* There just isn't any safe way of doing this incrementally with ICU; we
       could try to use unorm_next to find the next normalisation boundary,
       and to convert and compare those sections, but we run into problems
       when case folding alters the number of characters, and with finding
       the next character after a normalisation section to ensure that the
       comparison is correct. So, we punt and allocate buffers, convert UTF-8
       to UTF-16, and call unorm_compare(). */
    left_u16.unitlength = left->unitlength * 2 ; /* Worst case UTF-8 to UTF-16 */
    left_u16.codeunits = left_buffer =
      unicode_alloc(left_u16.unitlength * sizeof(UTF16)) ;

    if ( left_buffer != NULL ) {
      right_u16.unitlength = right->unitlength * 2 ; /* Worst case UTF-8 to UTF-16 */
      right_u16.codeunits = right_buffer =
        unicode_alloc(right_u16.unitlength * sizeof(UTF16)) ;

      if ( right_buffer != NULL ) {
        utf8_buffer left_u8 = *left, right_u8 = *right ;

        if ( utf8_to_utf16(&left_u8, &left_u16) == UTF_CONVERT_OK &&
             utf8_to_utf16(&right_u8, &right_u16) == UTF_CONVERT_OK ) {
          status = U_ZERO_ERROR ;
          result = unorm_compare((const UChar *)left_buffer,
                                 left_u16.codeunits - left_buffer,
                                 (const UChar *)right_buffer,
                                 right_u16.codeunits - right_buffer,
                                 options,
                                 &status) ;
        }
        unicode_free(right_buffer) ;
      }
      unicode_free(left_buffer) ;
    }

    if ( U_SUCCESS(status) )
      return (int)result ;

    /*@fallthrough@*/
  }
#else /* UCONFIG_NO_NORMALIZATION */
  UNUSED_PARAM(int, casefold) ;
  UNUSED_PARAM(int, normalform) ;
  UNUSED_PARAM(int, order) ;

  HQTRACE((debug_unicode & DEBUG_UNICODE_NO_ICU) != 0 &&
          (casefold != UTF_CASE_FOLDED ||
           normalform != UTF_NORMAL_FORM_NONE ||
           order != UTF_ORDER_UNSPECIFIED),
          ("ICU not available for comparison option in utf8_compare()")) ;

#endif /* UCONFIG_NO_NORMALIZATION */
  /* Either ICU is not available, or failed. Don't even try to do anything
     fancy, just provide a stable compare. */
  return (int)HqMemCmp((uint8 *)left->codeunits, (int32)left->unitlength,
                       (uint8 *)right->codeunits, (int32)right->unitlength) ;
}

/* Compare a UTF-16 sequence */
int utf16_compare(utf16_buffer *left, utf16_buffer *right,
                  int casefold, int normalform, int order)
{
  HQASSERT(casefold == UTF_CASE_UNFOLDED ||
           casefold == UTF_CASE_FOLDED,
           "Unicode case folding invalid") ;
  HQASSERT(normalform == UTF_NORMAL_FORM_NONE ||
           normalform == UTF_NORMAL_FORM_C ||
           normalform == UTF_NORMAL_FORM_D ||
           normalform == UTF_NORMAL_FORM_KC ||
           normalform == UTF_NORMAL_FORM_KD,
           "Invalid Unicode normalisation form") ;
  HQASSERT(order == UTF_ORDER_UNSPECIFIED ||
           order == UTF_ORDER_CODEPOINT,
           "Unicode ordering invalid") ;
  HQASSERT(left != NULL, "Missing UTF-16 comparee") ;
  HQASSERT(left->codeunits != NULL, "No UTF-16 comparee code units") ;
  HQASSERT(right != NULL, "Missing UTF-16 comparee") ;
  HQASSERT(right->codeunits != NULL, "No UTF-16 comparee code units") ;
  HQASSERT(utf16_validate(left->codeunits,
                          left->codeunits + left->unitlength,
                          NULL, NULL, NULL) == 0,
           "Input string to comparison is not valid UTF-16") ;
  HQASSERT(utf16_validate(right->codeunits,
                          right->codeunits + right->unitlength,
                          NULL, NULL, NULL) == 0,
           "Input string to comparison is not valid UTF-16") ;

#if defined(HQNlibicu) && !UCONFIG_NO_NORMALIZATION
  if ( unicode_icu_ready() ) {
    UErrorCode status = U_ZERO_ERROR ;
    int32 options, result ;

    /* Normalisation options are shifted for the compare. */
    options = (icu_normal_form[normalform] << UNORM_COMPARE_NORM_OPTIONS_SHIFT) ;

    if ( casefold == UTF_CASE_FOLDED )
      options |= U_COMPARE_IGNORE_CASE ;

    if ( order == UTF_ORDER_CODEPOINT )
      options |= U_COMPARE_CODE_POINT_ORDER ;

    result = unorm_compare((const UChar *)left->codeunits,
                           (int32)left->unitlength,
                           (const UChar *)right->codeunits,
                           (int32)right->unitlength,
                           options,
                           &status) ;
    if ( U_SUCCESS(status) )
      return (int)result ;

    /*@fallthrough@*/
  }
#else /* UCONFIG_NO_NORMALIZATION */
  UNUSED_PARAM(int, casefold) ;
  UNUSED_PARAM(int, normalform) ;
  UNUSED_PARAM(int, order) ;

  HQTRACE((debug_unicode & DEBUG_UNICODE_NO_ICU) != 0 &&
          (casefold != UTF_CASE_FOLDED ||
           normalform != UTF_NORMAL_FORM_NONE ||
           order != UTF_ORDER_UNSPECIFIED),
          ("ICU not available for comparison option in utf16_compare()")) ;
#endif /* UCONFIG_NO_NORMALIZATION */

  /* Either ICU is not available, or failed. Don't even try to do anything
     fancy, just provide a stable compare. */
  return (int)HqMemCmp((uint8 *)left->codeunits, (int32)left->unitlength,
                       (uint8 *)right->codeunits, (int32)right->unitlength) ;
}

/*
Log stripped */
