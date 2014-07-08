/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:ucase.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode case changing.
 *
 * The Unicode case functions perform case changing on Unicode UTF-8 or
 * UTF-16 sequences.
 */

#include "hqunicode.h"
#include "hqmemcpy.h"
#include "uprivate.h"
#include "std.h" /* For HQASSERT, HQFAIL */

#ifdef HQNlibicu
#include "unicode/uchar.h"
#include "unicode/ustring.h"
#endif

/* Case change a UTF-8 sequence */
int utf8_case(utf8_buffer *input, utf8_buffer *output, int casefold)
{
  /* Implement case conversion by converting to UTF-16, calling utf16_case,
     and converting back. */
#define CASE_BUFFER_SIZE 512
  UTF16 in_data[CASE_BUFFER_SIZE], case_data[CASE_BUFFER_SIZE] ;
  utf16_buffer u16_in, u16_out ;
  uint32 limit ;
  int result ;

  HQASSERT(casefold == UTF_CASE_UPPER ||
           casefold == UTF_CASE_LOWER ||
           casefold == UTF_CASE_TITLE ||
           casefold == UTF_CASE_FOLDED,
           "Unicode case folding invalid") ;
  HQASSERT(input != NULL, "No UTF-8 input") ;
  HQASSERT(input->codeunits != NULL, "No UTF-8 input code units") ;
  HQASSERT(output != NULL, "No UTF-8 output") ;
  HQASSERT(output->codeunits != NULL, "No UTF-8 sequence buffer string") ;
  HQASSERT(utf8_validate(input->codeunits,
                         input->codeunits + input->unitlength,
                         NULL, NULL, NULL) == 0,
           "Input string to case change is not valid UTF-8") ;

  /* The worst case expansion from UTF-16 to UTF-8 is 2:3. We will restrict
     the amount of UTF-16 output we generate from the case conversion
     to that, so that we know we can convert it all to UTF-8 at the end.
     Since case mapping is quite close to 1:1, we restrict the conversion
     buffer too. */
  limit = output->unitlength * 2u / 3u ;
  if ( limit > CASE_BUFFER_SIZE )
    limit = CASE_BUFFER_SIZE ;

  u16_in.codeunits = in_data ;
  u16_in.unitlength = limit ;

  result = utf8_to_utf16(input, &u16_in) ;
  switch ( result ) {
  case UTF_CONVERT_OK:
  case UTF_CONVERT_OUTPUT_EXHAUSTED:
    break ;
  default: /* Error return */
    return result ;
  }

  u16_out.codeunits = case_data ;
  u16_out.unitlength = limit ;

  result = utf16_case(&u16_in, &u16_out, casefold) ;
  switch ( result ) {
  case UTF_CONVERT_OK:
  case UTF_CONVERT_OUTPUT_EXHAUSTED:
    break ;
  default: /* Failure */
    return result ;
  }

  result = utf16_to_utf8(&u16_out, output) ;
  switch ( result ) {
  default:
    HQFAIL("Failure to convert back to UTF-8 after case conversion") ;
    /*@fallthrough@*/
  case UTF_CONVERT_NOT_AVAILABLE:
    return UTF_CONVERT_NOT_AVAILABLE ;
  case UTF_CONVERT_OK:
    /* Detect if we did not use all of the data converted to UTF-16, and
       backup enough characters. */
    for ( limit = u16_in.unitlength ; limit > 0 ; --limit ) {
      UTF16 code = *u16_in.codeunits++ ;

      if ( !UTF16_IS_SURROGATE_LOW(code) ) {
        /* Back up one UTF-8 character */
        do {
          ++input->unitlength ;
        } while ( utf8_length[*--input->codeunits] == 0 ) ;
      }
    }

    if ( input->unitlength > 0 )
      return UTF_CONVERT_OUTPUT_EXHAUSTED ;

    return UTF_CONVERT_OK ;
  }
}

/* Case change a UTF-16 sequence */
int utf16_case(utf16_buffer *input, utf16_buffer *output, int casefold)
{
  HQASSERT(casefold == UTF_CASE_UPPER ||
           casefold == UTF_CASE_LOWER ||
           casefold == UTF_CASE_TITLE ||
           casefold == UTF_CASE_FOLDED,
           "Unicode case folding invalid") ;
  HQASSERT(input != NULL, "No UTF-16 input") ;
  HQASSERT(input->codeunits != NULL, "No UTF-16 input code units") ;
  HQASSERT(output != NULL, "No UTF-16 output") ;
  HQASSERT(output->codeunits != NULL, "No UTF-16 sequence buffer string") ;
  HQASSERT(utf16_validate(input->codeunits,
                          input->codeunits + input->unitlength,
                          NULL, NULL, NULL) == 0,
           "Input string to case change is not valid UTF-16") ;

#ifdef HQNlibicu
  {
    uint32 inlimit = input->unitlength ;

    if ( !unicode_icu_ready() )
      return UTF_CONVERT_NOT_AVAILABLE ;

    for (;;) {
      UErrorCode status = U_ZERO_ERROR ;
      int32 length ;

      switch ( casefold ) {
      case UTF_CASE_FOLDED:
        length = u_strFoldCase((UChar *)output->codeunits,
                               (int32)output->unitlength,
                               (const UChar *)input->codeunits,
                               (int32)inlimit,
                               U_FOLD_CASE_DEFAULT,
                               &status) ;
        break ;
      case UTF_CASE_UPPER:
        length = u_strToUpper((UChar *)output->codeunits,
                              (int32)output->unitlength,
                              (const UChar *)input->codeunits,
                              (int32)inlimit,
                              "", /* root locale */
                              &status) ;
        break ;
      case UTF_CASE_LOWER:
        length = u_strToLower((UChar *)output->codeunits,
                              (int32)output->unitlength,
                              (const UChar *)input->codeunits,
                              (int32)inlimit,
                              "", /* root locale */
                              &status) ;
        break ;
#if !UCONFIG_NO_BREAK_ITERATION
      case UTF_CASE_TITLE:
        length = u_strToTitle((UChar *)output->codeunits,
                              (int32)output->unitlength,
                              (const UChar *)input->codeunits,
                              (int32)inlimit,
                              NULL, /* standard titlecase break iterator */
                              "", /* root locale */
                              &status) ;
        break ;
#endif
      default:
        HQFAIL("Odd case folding") ;
        return UTF_CONVERT_NOT_AVAILABLE ;
      }

      switch ( status ) {
      case U_STRING_NOT_TERMINATED_WARNING:
      case U_ZERO_ERROR: /* Consumed all input */
        output->codeunits += length ;
        output->unitlength -= (uint32)length ;
        input->codeunits += inlimit ;
        input->unitlength -= inlimit ;
        if ( input->unitlength > 0 )
          return UTF_CONVERT_OUTPUT_EXHAUSTED ;
        return UTF_CONVERT_OK ;
      case U_BUFFER_OVERFLOW_ERROR:
        /* The buffer overflowed, but we have no indication of how much of
           the input was required to fill the buffer. Try again with a
           reduced limit. */
        HQASSERT((uint32)length > output->unitlength,
                 "Buffer overflow should not have happened") ;
        /* Multiply the input limit by the ratio of the overflow, on the
           assumption the expansion is approximately linear. */
        inlimit = inlimit * output->unitlength / (uint32)length ;
        /* Search backward to avoid ending with a first surrogate. */
        while ( inlimit > 0 &&
                UTF16_IS_SURROGATE_HIGH(input->codeunits[inlimit - 1]) ) {
          --inlimit ;
        }
        if ( inlimit == 0 ) /* The output buffer really is too small. */
          return UTF_CONVERT_OUTPUT_EXHAUSTED ; /* loop again */
        break ;
      default:
        return UTF_CONVERT_NOT_AVAILABLE ;
      }
    }
  }
#else
  UNUSED_PARAM(utf16_buffer *, input) ;
  UNUSED_PARAM(utf16_buffer *, output) ;
  UNUSED_PARAM(int, casefold) ;

  /* ICU was not available, or failed. We don't use towupper()/towlower()
     directly, because they are locale-sensitive. */
  HQTRACE((debug_unicode & DEBUG_UNICODE_NO_ICU) != 0,
          ("ICU not available for case modification")) ;

  return UTF_CONVERT_NOT_AVAILABLE ;
#endif
}

/*
Log stripped */
