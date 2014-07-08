/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:uconvert.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode manipulation library UTF conversion.
 *
 * The Unicode manipulation functions provide rudimentary facilities to convert
 * between Unicode transformation representations, to compare normalised Unicode
 * sequences, and to iterate over Unicode sequences. These routines convert
 * between UTF forms.
 */

#include "std.h" /* For HQASSERT, HQFAIL */
#include "hqmemcpy.h"
#include "hqmemcmp.h"

#include "hqunicode.h"
#include "uprivate.h"

#ifdef HQNlibicu
#include "unicode/ucnv.h"
#endif

/* Convert a single Unicode code point to UTF-8 */
int unicode_to_utf8(UTF32 unicode, utf8_buffer *output)
{
  uint32 length = 1, remaining ;
  UTF8 *units ;

  HQASSERT((unicode >= 0 && unicode <= 0xd7ff) ||
           (unicode >= 0xe000 && unicode <= 0x10ffff),
           "Invalid Unicode code point") ;
  HQASSERT(output != NULL, "No UTF-8 sequence buffer") ;

  /* Most common case of 0-0x7f falls straight through */
  if ( unicode >= 0x80 ) {
    ++length ;
    if ( unicode >= 0x800 ) {
      ++length ;
      if ( unicode >= 0x10000 )
        ++length ;
    }
  }

  remaining = output->unitlength ;
  if ( remaining < length )
    return UTF_CONVERT_OUTPUT_EXHAUSTED ;

  units = output->codeunits ;
  HQASSERT(units != NULL, "No UTF-8 sequence buffer string") ;

  switch ( length ) {
  case 4:
    units[3] = (UTF8)((unicode & 0x3f) | 0x80) ;
    unicode >>= 6 ;
    /*@fallthrough@*/
  case 3:
    units[2] = (UTF8)((unicode & 0x3f) | 0x80) ;
    unicode >>= 6 ;
    /*@fallthrough@*/
  case 2:
    units[1] = (UTF8)((unicode & 0x3f) | 0x80) ;
    unicode >>= 6 ;
    /*@fallthrough@*/
  case 1:
    HQASSERT((unicode & utf8_mask[length]) == unicode,
             "UTF-8 first byte overflows bits available") ;
    units[0] = (UTF8)(unicode | utf8_marker[length]) ;
    break ;
  }

  output->codeunits = units + length ;
  output->unitlength = remaining - length ;

  return UTF_CONVERT_OK ;
}

/* Convert a single Unicode code point to UTF-16 */
int unicode_to_utf16(UTF32 unicode, utf16_buffer *output)
{
  HQASSERT((unicode >= 0 && unicode <= 0xd7ff) ||
           (unicode >= 0xe000 && unicode <= 0x10ffff),
           "Invalid Unicode code point") ;
  HQASSERT(output != NULL, "No UTF-16 sequence buffer") ;
  HQASSERT(output->codeunits != NULL, "No UTF-16 sequence buffer string") ;

  /* Convert the unicode value to UTF-16. This code uses a fall-through test
     for efficiency, relying on the low 16-bits staying the same when 0x10000
     is subtracted from the unicode value. The non-BMP case is rare. */
  unicode -= 0x10000 ;
  if ( unicode >= 0 ) { /* Character is not in BMP */
    if ( output->unitlength < 2 )
      return UTF_CONVERT_OUTPUT_EXHAUSTED ;
    *output->codeunits++ = (UTF16)((unicode >> UTF16_SURROGATE_BITS) +
                                   UTF16_SURROGATE_HIGH) ;
    output->unitlength -= 1 ;

    unicode = (unicode & UTF16_SURROGATE_MASK) + UTF16_SURROGATE_LOW ;
  }

  if ( output->unitlength == 0 )
    return UTF_CONVERT_OUTPUT_EXHAUSTED ;
  *output->codeunits++ = (UTF16)unicode ;
  output->unitlength -= 1 ;

  return UTF_CONVERT_OK ;
}

/* Convert a sequence of UTF-16 code units to a UTF-8 sequence */
int utf16_to_utf8(utf16_buffer *input, utf8_buffer *output)
{
  UTF16 *inunits ;
  uint32 remainin, remainout ;
  UTF8 *outunits ;

  HQASSERT(input != NULL, "No UTF-16 input") ;
  remainin = input->unitlength ;
  inunits = input->codeunits ;
  HQASSERT(inunits != NULL, "No UTF-16 code units") ;

  HQASSERT(output != NULL, "No UTF-8 output") ;
  remainout = output->unitlength ;
  outunits = output->codeunits ;
  HQASSERT(outunits != NULL, "No UTF-8 sequence buffer string") ;

  while ( remainin > 0 ) {
    UTF32 unicode = *inunits++ ;
    uint32 length ;

    remainin -= 1 ;

    if ( UTF16_IS_SURROGATE(unicode) ) {
      HQASSERT(unicode >= UTF16_SURROGATE_HIGH &&
               unicode <= UTF16_SURROGATE_LOW + UTF16_SURROGATE_MASK,
               "UTF16 surrogate test incorrect") ;
      if ( remainin == 0 )
        return UTF_CONVERT_INPUT_EXHAUSTED ;

      if ( unicode > UTF16_SURROGATE_HIGH + UTF16_SURROGATE_MASK ||
           *inunits < UTF16_SURROGATE_LOW ||
           *inunits > UTF16_SURROGATE_LOW + UTF16_SURROGATE_MASK )
        return UTF_CONVERT_INVALID_ENCODING ;

      unicode = ((unicode & UTF16_SURROGATE_MASK) << UTF16_SURROGATE_BITS) +
        (*inunits++ & UTF16_SURROGATE_MASK) + 0x10000 ;
      remainin -= 1 ;
    }

    /* If UTF16 surrogate pairs are correct, UTF16 by its nature cannot encode
       other invalid values. */
    HQASSERT((unicode >= 0 && unicode <= 0xd7ff) ||
             (unicode >= 0xe000 && unicode <= 0x10ffff),
             "UTF16 represents invalid code point") ;

    /* Output UTF-8 */
    length = 1 ;

    /* Most common case of 0-0x7f falls straight through */
    if ( unicode >= 0x80 ) {
      ++length ;
      if ( unicode >= 0x800 ) {
        ++length ;
        if ( unicode >= 0x10000 )
          ++length ;
      }
    }

    if ( remainout < length )
      return UTF_CONVERT_OUTPUT_EXHAUSTED ;

    switch ( length ) {
    case 4:
      outunits[3] = (UTF8)((unicode & 0x3f) | 0x80) ;
      unicode >>= 6 ;
      /*@fallthrough@*/
    case 3:
      outunits[2] = (UTF8)((unicode & 0x3f) | 0x80) ;
      unicode >>= 6 ;
      /*@fallthrough@*/
    case 2:
      outunits[1] = (UTF8)((unicode & 0x3f) | 0x80) ;
      unicode >>= 6 ;
      /*@fallthrough@*/
    case 1:
      HQASSERT((unicode & utf8_mask[length]) == unicode,
               "UTF-8 first byte overflows bits available") ;
      outunits[0] = (UTF8)(unicode | utf8_marker[length]) ;
      break ;
    }

    output->codeunits = (outunits += length) ;
    output->unitlength = (remainout -= length) ;

    input->codeunits = inunits ;
    input->unitlength = remainin ;
  }

  return UTF_CONVERT_OK ;
}

/* Convert a sequence of UTF-8 code units to a UTF-16 sequence */
int utf8_to_utf16(utf8_buffer *input, utf16_buffer *output)
{
  UTF8 *units ;
  uint32 remaining ;

  HQASSERT(input != NULL, "No UTF-8 input") ;
  remaining = input->unitlength ;
  units = input->codeunits ;
  HQASSERT(units != NULL, "No UTF-8 code units") ;

  HQASSERT(output != NULL, "No UTF-16 output") ;
  HQASSERT(output->codeunits != NULL, "No UTF-16 sequence buffer string") ;

  while ( remaining > 0 ) {
    UTF32 unicode, surrogate ;
    UTF8 byte = *units++ ;
    uint32 length = utf8_length[byte] ;

    HQASSERT(length <= 4, "Invalid length in UTF-8 table") ;

    if ( length == 0 )
      return UTF_CONVERT_INVALID_ENCODING ;

    if ( length > remaining )
      return UTF_CONVERT_INPUT_EXHAUSTED ;

    remaining -= length ;
    unicode = (byte & utf8_mask[length]) ;

  /* We use XOR 0x80 in to remove the high bit of the trailing bytes so that
     the compiler has fewer constants to manage; 0x80 is probably already
     loaded in a register because of the test. */
    switch ( length ) {
    case 4:
      if ( ((byte = *units++) & 0xc0) != 0x80 )
        return UTF_CONVERT_INVALID_ENCODING ;
      unicode = (unicode << 6) | (byte ^ 0x80) ;
      /*@fallthrough@*/
    case 3:
      if ( ((byte = *units++) & 0xc0) != 0x80 )
        return UTF_CONVERT_INVALID_ENCODING ;
      unicode = (unicode << 6) | (byte ^ 0x80) ;
      /*@fallthrough@*/
    case 2:
      if ( ((byte = *units++) & 0xc0) != 0x80 )
        return UTF_CONVERT_INVALID_ENCODING ;
      unicode = (unicode << 6) | (byte ^ 0x80) ;
      /*@fallthrough@*/
    case 1:
      break ;
    }

    HQASSERT(unicode >= 0, "Invalid Unicode value from UTF-8") ;
    if ( unicode > 0x10ffff ||
         (unicode >= 0xd800 && unicode <= 0xdfff) )
      return UTF_CONVERT_INVALID_CODEPOINT ;

    /* Now convert the unicode value to UTF-16. This code uses a fall-through
       test for efficiency, relying on the low 16-bits staying the same when
       0x10000 is subtracted from the unicode value. The non-BMP case is
       rare. */
    surrogate = unicode - 0x10000 ;
    if ( surrogate >= 0 ) { /* Character is not in BMP */
      if ( output->unitlength < 2 )
        return UTF_CONVERT_OUTPUT_EXHAUSTED ;
      *output->codeunits++ = (UTF16)((surrogate >> UTF16_SURROGATE_BITS) +
                                     UTF16_SURROGATE_HIGH) ;
      output->unitlength -= 1 ;

      surrogate = (surrogate & UTF16_SURROGATE_MASK) + UTF16_SURROGATE_LOW ;
    }

    if ( output->unitlength == 0 )
      return UTF_CONVERT_OUTPUT_EXHAUSTED ;
    *output->codeunits++ = (UTF16)surrogate ;
    output->unitlength -= 1 ;

    input->codeunits = units ;
    input->unitlength = remaining ;

    /* We may have just converted a character that was not in canonical UTF-8
       representation. Return with an error if this happened; this is after
       the buffer updates so that the client can choose to call the converter
       again if it wants to ignore this error. */
    if ( unicode < utf8_minimum[length] )
      return UTF_CONVERT_NOT_CANONICAL ;
  }

  return UTF_CONVERT_OK ;
}

/* Unicode stream converter APIs. */
#define PIVOT_SIZE 1024

struct unicode_convert_t {
#ifdef HQNlibicu
  UConverter *input, *output ;
  int bom, substitute ;
  UChar *pivotStart, *pivotEnd ;
  UChar pivot[PIVOT_SIZE] ;
#else /* !HQNlibicu */
  int dummy ;
#endif /* !HQNlibicu */
} ;

unicode_convert_t *unicode_convert_open(uint8 *inname, uint32 inlen,
                                        uint8 *outname, uint32 outlen,
                                        int bom,
                                        uint8 *subs, uint32 subslen)
{
#ifdef HQNlibicu
  unicode_convert_t *converter ;
  UErrorCode status = U_ZERO_ERROR ;
  char cname[UCNV_MAX_CONVERTER_NAME_LENGTH] ;

  if ( inlen >= UCNV_MAX_CONVERTER_NAME_LENGTH ||
       outlen >= UCNV_MAX_CONVERTER_NAME_LENGTH ||
       subslen >= 128 ||
       !unicode_icu_ready() )
    return NULL ;

  if ( (converter = unicode_alloc(sizeof(unicode_convert_t))) != NULL ) {
    UConverterFromUCallback action = UCNV_FROM_U_CALLBACK_STOP ;

    converter->input = converter->output = NULL ;
    converter->bom = bom ;
    converter->substitute = FALSE ;
    /* The first buffer, we leave a space at the start of the pivot in case
       we need to add a BOM. */
    converter->pivotStart = converter->pivotEnd = converter->pivot + 1 ;

    /* We rely on ICU's checking of status before operations to avoid
       damaging results. The status can be checked once at the end of all
       of the desired operations. */
    HqMemCpy(cname, outname, outlen) ;
    cname[outlen] = '\0' ;
    converter->output = ucnv_open(cname, &status) ;

    if ( subs != NULL ) {
      converter->substitute = TRUE ;

      if ( subslen != 0 ) {
        action = UCNV_FROM_U_CALLBACK_SUBSTITUTE ;
        ucnv_setSubstChars(converter->output,
                           (const char *)subs, (int8_t)subslen, &status) ;
      } else
        action = UCNV_FROM_U_CALLBACK_SKIP ;
    }

    ucnv_setFromUCallBack(converter->output,
                          action, NULL /*context*/,
                          NULL /*old action*/, NULL /*old context*/,
                          &status) ;

    /* The special converter name "Unicode" means any Unicode input
       format; we will auto-detect the input type. */
    if ( HqMemCmp(inname, inlen, (uint8 *)"Unicode", 7) != 0 ) {
      UConverterToUCallback action = UCNV_TO_U_CALLBACK_STOP ;

      if ( converter->substitute )
        action = UCNV_TO_U_CALLBACK_SUBSTITUTE ;

      HqMemCpy(cname, inname, inlen) ;
      cname[inlen] = '\0' ;
      converter->input = ucnv_open(cname, &status) ;
      ucnv_setToUCallBack(converter->input,
                          action, NULL /*context*/,
                          NULL /*old action*/, NULL /*old context*/,
                          &status) ;
    }

    if ( U_FAILURE(status) ) /* Clean up the failed converter */
      unicode_convert_close(&converter) ;
  }

  return converter ;
#else /* !HQNlibicu */
  UNUSED_PARAM(uint8 *, inname) ;
  UNUSED_PARAM(uint32, inlen) ;
  UNUSED_PARAM(uint8 *, outname) ;
  UNUSED_PARAM(uint32, outlen) ;
  UNUSED_PARAM(int, bom) ;
  UNUSED_PARAM(uint8 *, subs) ;
  UNUSED_PARAM(uint32, subslen) ;
  return NULL ;
#endif /* !HQNlibicu */
}

/* This routine converts between input and output encodings using an
   intermediate UTF-16 (UChar) buffer. We do not use ucnv_convertEx directly,
   even though this is very similar, because we want the option to strip or
   add a Byte Order Mark. */
int unicode_convert_buffer(unicode_convert_t *converter,
                           uint8 **input, int32 *inlen,
                           uint8 **output, int32 *outlen,
                           HqBool flush)
{
#ifdef HQNlibicu
  UErrorCode status = U_ZERO_ERROR ;
  uint8 *inlimit, *outlimit ;

  HQASSERT(converter, "No converter for unicode buffer") ;

  HQASSERT(input, "Nowhere to find input buffer") ;
  HQASSERT(*input, "No input buffer") ;
  HQASSERT(inlen, "Nowhere to find input length") ;

  HQASSERT(output, "Nowhere to find output buffer") ;
  HQASSERT(*output, "No output buffer") ;
  HQASSERT(outlen, "Nowhere to find output length") ;

  if ( converter->input == NULL ) {
    int32 sig_len ;
    const char *cname ;
    UConverterToUCallback action = UCNV_TO_U_CALLBACK_STOP ;

    /* We are auto-detecting Unicode input type, and we have not yet created
       a converter. */
    if ( (cname = ucnv_detectUnicodeSignature((const char *)*input, *inlen,
                                              &sig_len, &status)) == NULL ) {
      if ( *inlen >= 4 )
        return UTF_CONVERT_NOT_AVAILABLE ;
      else
        return UTF_CONVERT_INPUT_EXHAUSTED ; /* No signature found */
    }

    if ( converter->substitute )
      action = UCNV_TO_U_CALLBACK_SUBSTITUTE ;

    converter->input = ucnv_open(cname, &status) ;
    ucnv_setToUCallBack(converter->input,
                        action, NULL /*context*/,
                        NULL /*old action*/, NULL /*old context*/,
                        &status) ;
    if ( U_FAILURE(status) )
      return UTF_CONVERT_NOT_AVAILABLE ;

    /* Leave the BOM on input; some encodings (UTF-7, SCSU) are stateful,
       and require it. We will remove the BOM if necessary when preparing
       to output the converted pivot buffer. */
  }

  inlimit = *input + *inlen ;
  outlimit = *output + *outlen ;
  for (;;) {
    /* Flush codepoints left in the pivot buffer from the last time around. */
    if ( converter->pivotStart != converter->pivotEnd ) {
      switch ( converter->bom ) {
      case UCONVERT_BOM_ADD: /* Add BOM if it doesn't exist. */
        if ( converter->pivotStart[0] != 0xfeff )
          *--converter->pivotStart = 0xfeff ;
        converter->bom = UCONVERT_BOM_LEAVE ;
        break ;
      case UCONVERT_BOM_REMOVE: /* Remove BOM if it exists */
        if ( converter->pivotStart[0] == 0xfeff )
          converter->pivotStart += 1 ;
        converter->bom = UCONVERT_BOM_LEAVE ;
        break ;
      }

      ucnv_fromUnicode(converter->output,
                       (char **)output, (const char *)outlimit,
                       (const UChar **)&converter->pivotStart, converter->pivotEnd,
                       NULL,
                       (UBool)(flush && *input == inlimit),
                       &status) ;
      *outlen = CAST_PTRDIFFT_TO_INT32(outlimit - *output) ;
      if ( U_FAILURE(status) ) {
        if ( status == U_BUFFER_OVERFLOW_ERROR ) /* Target overflowed */
          return UTF_CONVERT_OUTPUT_EXHAUSTED ;

        /* Conversion error. Reset converter in case we try to continue. */
        ucnv_resetFromUnicode(converter->output) ;
        return UTF_CONVERT_INVALID_ENCODING ;
      }

      HQASSERT(converter->pivotStart == converter->pivotEnd,
               "Unicode output converter did not consume all input") ;

      /* Output succeeded; reset the pivot buffer. */
      converter->pivotStart = converter->pivotEnd = converter->pivot ;
    }

    /* Re-fill the pivot buffer. */
    ucnv_toUnicode(converter->input,
                   &converter->pivotEnd, converter->pivot + PIVOT_SIZE,
                   (const char **)input, (const char *)inlimit,
                   NULL, (UBool)flush, &status) ;
    *inlen = CAST_PTRDIFFT_TO_INT32(inlimit - *input) ;
    if ( status == U_BUFFER_OVERFLOW_ERROR ) {
      status = U_ZERO_ERROR ; /* Pivot overflow is not a problem. */
    } else if ( status == U_TRUNCATED_CHAR_FOUND ) {
      /* Input buffer is not complete. Back off the input part that caused an
         eror and allow a retry. */
#define MAX_BYTES_PER_CHAR 8 /* ucnv_getMaxCharSize would be better */
      char bytes[MAX_BYTES_PER_CHAR] ;
      int8 nbytes = MAX_BYTES_PER_CHAR ;

      status = U_ZERO_ERROR ;
      ucnv_getInvalidChars(converter->input, bytes, &nbytes, &status) ;
      if ( U_FAILURE(status) )
        return UTF_CONVERT_INVALID_ENCODING ;

      *input -= nbytes ;
      *inlen += nbytes ;

      ucnv_resetToUnicode(converter->input) ;
      return UTF_CONVERT_INPUT_EXHAUSTED ;
    } else if ( U_FAILURE(status) ) {
      /* Conversion error. This is error is unrecoverable. */
      return UTF_CONVERT_INVALID_ENCODING ;
    } else if ( converter->pivotStart == converter->pivotEnd ) {
      /* There was nothing read from the input. Either we finished OK, with
         no more to do, or there was unprocessed input left over. */
      if ( *inlen != 0 )
        return UTF_CONVERT_INPUT_EXHAUSTED ;

      return UTF_CONVERT_OK ;
    }
    /* else pivot buffer received some input; loop around to output it. */
  }
#else /* !HQNlibicu */
  UNUSED_PARAM(unicode_convert_t *, converter) ;
  UNUSED_PARAM(uint8 **, input) ;
  UNUSED_PARAM(int32 *, inlen) ;
  UNUSED_PARAM(uint8 **, output) ;
  UNUSED_PARAM(int32 *, outlen) ;
  UNUSED_PARAM(HqBool, flush) ;
  return UTF_CONVERT_NOT_AVAILABLE ;
#endif /* !HQNlibicu */
}

void unicode_convert_close(unicode_convert_t **converterp)
{
#ifdef HQNlibicu
  unicode_convert_t *converter ;

  HQASSERT(converterp, "Nowhere to find converter to close") ;

  converter = *converterp ;
  HQASSERT(converter, "No converter to close") ;

  if ( converter->input != NULL )
    ucnv_close(converter->input) ;
  if ( converter->output != NULL )
    ucnv_close(converter->output) ;

  unicode_free(converter) ;
  *converterp = NULL ;
#else /* !HQNlibicu */
  UNUSED_PARAM(unicode_convert_t **, converterp) ;
#endif /* !HQNlibicu */
}

/*
Log stripped */
