/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:unormalise.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode normalisation.
 *
 * The Unicode normalisation functions perform normalisation on Unicode UTF-8
 * or UTF-16 sequences.
 */

#include "hqunicode.h"
#include "hqmemcpy.h"
#include "uprivate.h"
#include "std.h" /* For HQASSERT, HQFAIL */

#ifdef HQNlibicu
#include "unicode/uclean.h"
#include "unicode/unorm.h"
#include "unicode/uiter.h"

#if !UCONFIG_NO_NORMALIZATION
/* \brief ICU normal forms corresponding to HQNc-unicode normal form enum. */
UNormalizationMode icu_normal_form[] = {
  UNORM_NONE,  /* UTF_NORMAL_FORM_NONE */
  UNORM_NFC,   /* UTF_NORMAL_FORM_C */
  UNORM_NFKC,  /* UTF_NORMAL_FORM_KC */
  UNORM_NFD,   /* UTF_NORMAL_FORM_D */
  UNORM_NFKD,  /* UTF_NORMAL_FORM_KD */
} ;
#endif /* !UCONFIG_NO_NORMALIZATION */
#endif /* HQNlibicu */

/* Normalise a UTF-8 sequence */
int utf8_normalize(utf8_buffer *input, utf8_buffer *output, int normalform)
{
  UNUSED_PARAM(utf8_buffer *, input) ;
  UNUSED_PARAM(utf8_buffer *, output) ;
  UNUSED_PARAM(int, normalform) ;

  HQASSERT(normalform == UTF_NORMAL_FORM_C ||
           normalform == UTF_NORMAL_FORM_KC ||
           normalform == UTF_NORMAL_FORM_D ||
           normalform == UTF_NORMAL_FORM_KD,
           "Only Unicode normal forms C, KC, D and KD are supported") ;
  HQASSERT(input != NULL, "No UTF-8 input") ;
  HQASSERT(input->codeunits != NULL, "No UTF-8 input code units") ;
  HQASSERT(output != NULL, "No UTF-8 output") ;
  HQASSERT(output->codeunits != NULL, "No UTF-8 sequence buffer string") ;
  HQASSERT(utf8_validate(input->codeunits,
                         input->codeunits + input->unitlength,
                         NULL, NULL, NULL) == 0,
           "Input string to normalisation is not valid UTF-8") ;

#if defined(HQNlibicu) && !UCONFIG_NO_NORMALIZATION
  /* See the comments in utf16_normalise about the ICU normalisation
     interface. The algorithm used for UTF-8 is the same, except that we have
     to convert to and from UTF-8 beforehand. The conversion from UTF-8 to
     the intermediate UTF-16 buffer is performed by an ICU iterator. */
#define NORMALISE_BUFFER_SIZE 512
  {
    UErrorCode status = U_ZERO_ERROR ;
    UNormalizationMode mode = icu_normal_form[normalform] ;
    HqBool donesome = FALSE ;
    int32 inlimit = (int32)input->unitlength ;
    int32 outlimit = NORMALISE_BUFFER_SIZE ;
    UTF16 normalise_buffer[NORMALISE_BUFFER_SIZE] ;

    HQASSERT((int32)input->unitlength >= 0,
             "UTF-8 normalisation input string is too large") ;
    HQASSERT((int32)output->unitlength >= 0,
             "UTF-8 normalisation output buffer is too large") ;

    if ( !unicode_icu_ready() )
      return UTF_CONVERT_NOT_AVAILABLE ;

    while ( input->unitlength > 0 ) {
      UCharIterator iterator ;
      int32 length ;

      /* Set up UChar iterator for UTF-8 buffer. */
      uiter_setUTF8(&iterator,
                    (const char *)input->codeunits, inlimit) ;

      /* Normalise into a temporary buffer */
      length = unorm_next(&iterator,
                          (UChar *)normalise_buffer, outlimit,
                          mode,
                          0, /*options*/
                          (UBool)TRUE, /*doNormalize*/
                          NULL, /*neededToNormalize*/
                          &status) ;

      switch ( status ) {
        utf8_buffer outcopy ;
        utf16_buffer normalise ;
      case U_STRING_NOT_TERMINATED_WARNING:
        status = U_ZERO_ERROR ;
        /*@fallthrough@*/
      case U_ZERO_ERROR:
        /* See comment below about examining the iterator state. If we would
           consume the whole input string, and we have already processed some
           output, we will flush that output to the client just in case the
           next input buffer contains a continuation of the normalisable
           text. */
        if ( donesome && (uint32)iterator.index == input->unitlength )
          return UTF_CONVERT_INPUT_EXHAUSTED ;

        /* We have to convert back to UTF-8. This may overflow the output
           buffer, so we save the output in case we need to back off. */
        outcopy = *output ;
        normalise.codeunits = normalise_buffer ;
        normalise.unitlength = (uint32)length ;
        if ( utf16_to_utf8(&normalise, output) == UTF_CONVERT_OK ) {
          /* Naughty, naughty. We peek inside the internal implementation of
             ICU's string iterator to find out how much of the input string was
             consumed by a successful normalisation. The alternative is writing
             our own iterator which duplicates the ICU iterator just so we can
             expose the amount consumed. This is a flaw in the ICU iteration
             interface. */
          input->codeunits += iterator.index ;
          input->unitlength -= (uint32)iterator.index ;
          /* Reset the input limit to the remaining input, and output limit to
             the temporary buffer size. */
          inlimit = (int32)input->unitlength ;
          outlimit = NORMALISE_BUFFER_SIZE ;
          donesome = TRUE ;
          break ;
        }

        /* Overflowed output buffer during UTF16 to UTF8 conversion. We need
           to limit the amount of output, rather than input. The output limit
           will then cause an input limit. */
        *output = outcopy ; /* Restore previous output position. */
        /* Set the UTF-16 limit to the maximum size that can be guaranteed to
           fit in the UTF-8 output. */
        outlimit = output->unitlength * 2 / 3 ;
        /*@fallthrough@*/
      case U_BUFFER_OVERFLOW_ERROR:
        HQASSERT(length > outlimit, "Buffer overflow should not have happened") ;
        /* Regrettably, the neededToNormalize flag is not valid when this
           error occurs, so we cannot tell if we had a run of normal form
           characters, or overflowed on a non-normal run. This would tell us
           whether we can safely break up the input to try a smaller amount.
           Instead, we will assume the output buffer is long enough to hold a
           single normalisable output sequence; if we have output such a
           sequence, we return an output overflow to the client to allow it
           to flush the buffer. If not, we try to split the input at a sane
           place, assuming it is in normal form. */
        if ( donesome )
          return UTF_CONVERT_OUTPUT_EXHAUSTED ;
        /* Multiply the input limit by the ratio of the overflow, on the
           assumption the expansion is approximately linear. */
        inlimit = inlimit * outlimit / length ;
        /* Search backward to avoid ending with continuation character. */
        while ( inlimit > 0 &&
                utf8_length[input->codeunits[inlimit]] == 0 ) {
          --inlimit ;
        }
        if ( inlimit == 0 ) /* The output buffer really is too small. */
          return UTF_CONVERT_NOT_AVAILABLE ;
        break ;
      case U_TRUNCATED_CHAR_FOUND:
      case U_ILLEGAL_CHAR_FOUND:
        /* Surprisingly, the ICU normaliser and iterator code does not return
           these errors. Incomplete characters in the input are likely to
           cause incorrect operation or crashes, hence the assert that the
           input is validated above. Sigh. */
        return UTF_CONVERT_INVALID_CODEPOINT ;
      default:
        return UTF_CONVERT_NOT_AVAILABLE ;
      }
    }

    return UTF_CONVERT_OK ;
  }
#else /* UCONFIG_NO_NORMALIZATION */
  {
    uint32 length = output->unitlength ;

    if ( length > input->unitlength )
      length = input->unitlength ;

    /* Copy input to output without normalisation, but complain about it if
       conversion was required. */
    HqMemCpy(output->codeunits, input->codeunits, length) ;

    input->codeunits += length ;
    input->unitlength -= length ;
    output->codeunits += length ;
    output->unitlength -= length ;

    HQTRACE((debug_unicode & DEBUG_UNICODE_NO_ICU) != 0 &&
             normalform != UTF_NORMAL_FORM_NONE,
            ("ICU not available for unicode normalisation in utf8_normalise()")) ;

    if ( length < input->unitlength )
      return UTF_CONVERT_OUTPUT_EXHAUSTED ;

    if ( normalform == UTF_NORMAL_FORM_NONE )
      return UTF_CONVERT_OK ;

    return UTF_CONVERT_NOT_AVAILABLE ;
  }
#endif /* UCONFIG_NO_NORMALIZATION */
}

/* Normalise a UTF-16 sequence */
int utf16_normalize(utf16_buffer *input, utf16_buffer *output, int normalform)
{
  HQASSERT(normalform == UTF_NORMAL_FORM_C ||
           normalform == UTF_NORMAL_FORM_KC ||
           normalform == UTF_NORMAL_FORM_D ||
           normalform == UTF_NORMAL_FORM_KD,
           "Only Unicode normal forms C, KC, D and KD are supported") ;
  HQASSERT(input != NULL, "No UTF-16 input") ;
  HQASSERT(input->codeunits != NULL, "No UTF-16 input code units") ;
  HQASSERT(output != NULL, "No UTF-16 output") ;
  HQASSERT(output->codeunits != NULL, "No UTF-16 sequence buffer string") ;
  HQASSERT(utf16_validate(input->codeunits,
                          input->codeunits + input->unitlength,
                          NULL, NULL, NULL) == 0,
           "Input string to normalisation is not valid UTF-16") ;

#if defined(HQNlibicu) && !UCONFIG_NO_NORMALIZATION
  /* The normalisation interface that ICU provides is a piece of rubbish. It
     does not facilitate incremental normalisation, because if the output
     buffer overflows, it does not keep the input iterator in step with the
     last output character, and it leaves the output buffer in an undefined
     state (not guaranteeing the output up to the overflow). Instead, it
     moves the input iterator to the start of the next normalisation
     boundary. It also uses a baroque iterator interface requiring UTF-16
     representation rather than UTF-32, resulting in some nasty problems when
     iterating over UTF-8 characters that use surrogates when represented in
     UTF-16. There really is no reason that a well-designed normaliser should
     need to use dynamic memory allocation.

     The interface presented here is incremental, so we do need to keep the
     iterators in step. This is done through a back off and retry system; if
     the entire buffer cannot be normalised in one go, we split up the
     original buffer into chunks. If the output buffer is of a sufficient
     size to hold any single normalised input unit, then this should succeed,
     because of the way ICU searches for normalisable boundaries. */
  {
    UErrorCode status = U_ZERO_ERROR ;
    UNormalizationMode mode = icu_normal_form[normalform] ;
    HqBool donesome = FALSE ;
    int32 inlimit = (int32)input->unitlength ;

    HQASSERT((int32)input->unitlength >= 0,
             "UTF-16 normalisation input string is too large") ;
    HQASSERT((int32)output->unitlength >= 0,
             "UTF-16 normalisation output buffer is too large") ;

    if ( !unicode_icu_ready() )
      return UTF_CONVERT_NOT_AVAILABLE ;

    while ( input->unitlength > 0 ) {
      UCharIterator iterator ;
      int32 length ;

      /* Set up UChar iterator for UTF-16 buffer. */
      uiter_setString(&iterator,
                      (const UChar *)input->codeunits, inlimit) ;

      length = unorm_next(&iterator,
                          (UChar *)output->codeunits,
                          (int32_t)output->unitlength,
                          mode,
                          0, /*options*/
                          (UBool)TRUE, /*doNormalize*/
                          NULL, /*neededToNormalize*/
                          &status) ;

      switch ( status ) {
      case U_STRING_NOT_TERMINATED_WARNING:
        status = U_ZERO_ERROR ;
        /*@fallthrough@*/
      case U_ZERO_ERROR:
        /* See comment below about examining the iterator state. If we would
           consume the whole input string, and we have already processed some
           output, we will flush that output to the client just in case the
           next input buffer contains a continuation of the normalisable
           text. */
        if ( donesome && (uint32)iterator.index == input->unitlength )
          return UTF_CONVERT_INPUT_EXHAUSTED ;
        output->codeunits += length ;
        output->unitlength -= (uint32)length ;
        /* Naughty, naughty. We peek inside the internal implementation of
           ICU's string iterator to find out how much of the input string was
           consumed by a successful normalisation. The alternative is writing
           our own iterator which duplicates the ICU iterator just so we can
           expose the amount consumed. This is a flaw in the ICU iteration
           interface. */
        input->codeunits += iterator.index ;
        input->unitlength -= (uint32)iterator.index ;
        /* Reset the input limit to the remaining input */
        inlimit = (int32)input->unitlength ;
        donesome = TRUE ;
        break ;
      case U_BUFFER_OVERFLOW_ERROR:
        HQASSERT((uint32)length > output->unitlength,
                 "Buffer overflow should not have happened") ;
        /* Regrettably, the neededToNormalize flag is not valid when this
           error occurs, so we cannot tell if we had a run of normal form
           characters, or overflowed on a non-normal run. This would tell us
           whether we can safely break up the input to try a smaller amount.
           Instead, we will assume the output buffer is long enough to hold a
           single normalisable output sequence; if we have output such a
           sequence, we return an output overflow to the client to allow it
           to flush the buffer. If not, we try to split the input at a sane
           place, assuming it is in normal form. */
        if ( donesome )
          return UTF_CONVERT_OUTPUT_EXHAUSTED ;
        /* Multiply the input limit by the ratio of the overflow, on the
           assumption the expansion is approximately linear. */
        inlimit = inlimit * (int32)output->unitlength / length ;
        /* Search backward to avoid ending with a first surrogate. */
        while ( inlimit > 0 &&
                UTF16_IS_SURROGATE_HIGH(input->codeunits[inlimit - 1]) ) {
          --inlimit ;
        }
        if ( inlimit == 0 ) /* The output buffer really is too small. */
          return UTF_CONVERT_NOT_AVAILABLE ;
        break ;
      case U_TRUNCATED_CHAR_FOUND:
      case U_ILLEGAL_CHAR_FOUND:
        /* Surprisingly, the ICU normaliser and iterator code does not return
           these errors. Incomplete characters in the input are likely to
           cause incorrect operation or crashes, hence the assert that the
           input is validated above. Sigh. */
        return UTF_CONVERT_INVALID_CODEPOINT ;
      default:
        return UTF_CONVERT_NOT_AVAILABLE ;
      }
    }

    return UTF_CONVERT_OK ;
  }
#else /* UCONFIG_NO_NORMALIZATION */
  {
    uint32 length = output->unitlength ;

    if ( length > input->unitlength )
      length = input->unitlength ;

    /* Copy input to output without normalisation, but complain about it if
       conversion was required. */
    HqMemCpy(output->codeunits, input->codeunits, length * sizeof(UTF16)) ;

    input->codeunits += length ;
    input->unitlength -= length ;
    output->codeunits += length ;
    output->unitlength -= length ;

    HQTRACE((debug_unicode & DEBUG_UNICODE_NO_ICU) != 0 &&
            normalform != UTF_NORMAL_FORM_NONE,
            ("ICU not available for unicode normalisation in utf16_normalise()")) ;

    if ( length < input->unitlength )
      return UTF_CONVERT_OUTPUT_EXHAUSTED ;

    if ( normalform == UTF_NORMAL_FORM_NONE )
      return UTF_CONVERT_OK ;

    return UTF_CONVERT_NOT_AVAILABLE ;
  }
#endif /* UCONFIG_NO_NORMALIZATION */
}

/*
Log stripped */
