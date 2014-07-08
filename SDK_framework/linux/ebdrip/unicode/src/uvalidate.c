/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:uvalidate.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode manipulation library UTF validation.
 *
 * The Unicode manipulation functions provide rudimentary facilities to convert
 * between Unicode transformation representations, to compare normalised Unicode
 * sequences, and to iterate over Unicode sequences. These routines validate
 * UTF sequences.
 */

#include "hqunicode.h"
#include "uprivate.h"
#include "std.h" /* For HQASSERT, HQFAIL */

/* Extract next code point from a raw UTF-8 string, validating the UTF-8
   string and updating the string pointer. This can be used to iterate
   over zero terminated strings. */
UTF32 utf8_validate_next(const UTF8 **string, const UTF8 *limit)
{
  const UTF8 *units ;
  UTF8 byte ;
  uint32 length ;
  UTF32 unicode, result = -1 ;

  HQASSERT(string != NULL, "No UTF-8 string") ;
  units = *string ;

  HQASSERT(limit == NULL || limit > units,
           "UTF-8 string limit is invalid") ;

  HQASSERT(units != NULL, "No UTF-8 code units") ;
  byte = *units++ ;

#define return DO NOT return!!! GO TO utf8_invalid INSTEAD!

  length = utf8_length[byte] ;
  HQASSERT(length <= 4, "Invalid length in UTF-8 table") ;
  if ( length == 0 )
    goto utf8_invalid ;

  unicode = (byte & utf8_mask[length]) ;

  /* We use XOR 0x80 in to remove the high bit of the trailing bytes so that
     the compiler has fewer constants to manage; 0x80 is probably already
     loaded in a register because of the test. We will consume more than the
     minimum number of bytes when presented with invalid non shortest form
     UTF-8 sequences; this is reasonable because the trailing bytes are not
     valid start bytes anyway, so resynchronisation will occur sooner. */
  switch ( length ) {
  case 4:
    if ( units == limit || ((byte = *units++) & 0xc0) != 0x80 )
      goto utf8_invalid ;
    unicode = (unicode << 6) | (byte ^ 0x80) ;
    /*@fallthrough@*/
  case 3:
    if ( units == limit || ((byte = *units++) & 0xc0) != 0x80 )
      goto utf8_invalid ;
    unicode = (unicode << 6) | (byte ^ 0x80) ;
    /*@fallthrough@*/
  case 2:
    if ( units == limit || ((byte = *units++) & 0xc0) != 0x80 )
      goto utf8_invalid ;
    unicode = (unicode << 6) | (byte ^ 0x80) ;
    /*@fallthrough@*/
  case 1:
    break ;
  }

  HQASSERT(unicode >= 0, "Invalid Unicode value from UTF-8") ;
  if ( unicode < utf8_minimum[length] ||
       unicode > 0x10ffff ||
       (unicode >= 0xd800 && unicode <= 0xdfff) )
    goto utf8_invalid ;

  result = unicode ;

 utf8_invalid:
  *string = units ;

#undef return
  return result ;
}

/* Get number of code units and codepoints in a UTF-8 string. */
uint32 utf8_validate(const UTF8 *string, const UTF8 *limit,
                     uint32 *utf8units,
                     uint32 *utf16units,
                     uint32 *codepoints)
{
  uint32 invalid = 0, nutf16units = 0, ncodepoints = 0 ;
  const UTF8 *units ;

  HQASSERT(string != NULL, "No UTF-8 string to validate") ;
  HQASSERT(limit == NULL || limit >= string,
           "UTF-8 string limit is invalid") ;

  for ( units = string ; units != limit ; ) {
    UTF32 unicode = utf8_validate_next(&units, limit) ;

    if ( unicode < 0 ) {
      ++invalid ;
      continue ;
    }

    if ( unicode == 0 && limit == NULL )
      break ; /* zero terminator found */

    ++ncodepoints ; /* It's a valid codepoint! */
    ++nutf16units ; /* Requires at least one UTF-16 codepoint */

    if ( unicode >= 0x10000 )
      ++nutf16units ; /* Surrogate pair will be required */
  }

  if ( utf8units != NULL )
    *utf8units = CAST_PTRDIFFT_TO_UINT32(units - string) ;

  if ( utf16units != NULL )
    *utf16units = nutf16units ;

  if ( codepoints != NULL )
    *codepoints = ncodepoints ;

  return invalid ;
}

/* Extract next code point from a raw UTF-16 string, validating the
   UTF-16 string and updating the string pointer. This can be used to
   iterate over zero terminated strings. */
UTF32 utf16_validate_next(const UTF16 **string, const UTF16 *limit)
{
  const UTF16 *units ;
  UTF32 unicode, result = -1 ;

  HQASSERT(string != NULL, "No UTF-16 string") ;
  units = *string ;

  HQASSERT(limit == NULL || limit > units,
           "UTF-16 string limit is invalid") ;

  HQASSERT(units != NULL, "No UTF-16 code units") ;
  unicode = *units++ ;

#define return DO NOT return!!! GO TO utf16_invalid INSTEAD!

  if ( UTF16_IS_SURROGATE(unicode) ) {
    UTF16 low ;

    HQASSERT(unicode >= UTF16_SURROGATE_HIGH &&
             unicode <= UTF16_SURROGATE_LOW + UTF16_SURROGATE_MASK,
             "UTF16 surrogate test incorrect") ;
    if ( units == limit ||
         unicode > UTF16_SURROGATE_HIGH + UTF16_SURROGATE_MASK )
      goto utf16_invalid ;

    low = *units++ ;
    if ( low < UTF16_SURROGATE_LOW ||
         low > UTF16_SURROGATE_LOW + UTF16_SURROGATE_MASK )
      goto utf16_invalid ;

    unicode = ((unicode & UTF16_SURROGATE_MASK) << UTF16_SURROGATE_BITS) +
      (low & UTF16_SURROGATE_MASK) + 0x10000 ;
  }

  /* If UTF16 surrogate pairs are correct, UTF16 by its nature cannot encode
     other invalid values. */
  HQASSERT((unicode >= 0 && unicode <= 0xd7ff) ||
           (unicode >= 0xe000 && unicode <= 0x10ffff),
           "UTF16 represents invalid code point") ;

  result = unicode ;

 utf16_invalid:
  *string = units ;

#undef return
  return result ;
}

/* Get number of code units and codepoints in a UTF-16 string. */
uint32 utf16_validate(const UTF16 *string, const UTF16 *limit,
                      uint32 *utf8units,
                      uint32 *utf16units,
                      uint32 *codepoints)
{
  uint32 invalid = 0, nutf8units = 0, ncodepoints = 0 ;
  const UTF16 *units ;

  HQASSERT(string != NULL, "No UTF-16 string to validate") ;
  HQASSERT(limit == NULL || limit >= string,
           "UTF-16 string limit is invalid") ;

  for ( units = string ; units != limit ; ) {
    UTF32 unicode = utf16_validate_next(&units, limit) ;

    if ( unicode < 0 ) {
      ++invalid ;
      continue ;
    }

    if ( unicode == 0 && limit == NULL )
      break ; /* zero terminator found */

    ++ncodepoints ; /* It's a valid codepoint! */
    ++nutf8units ;  /* Requires at least one UTF-8 codepoint */
    if ( unicode >= 0x80 ) {
      ++nutf8units ;
      if ( unicode >= 0x800 ) {
        ++nutf8units ;
        if ( unicode >= 0x10000 )
          ++nutf8units ;
      }
    }
  }

  if ( utf8units != NULL )
    *utf8units = nutf8units ;

  if ( utf16units != NULL )
    *utf16units = CAST_PTRDIFFT_TO_UINT32(units - string) ;

  if ( codepoints != NULL )
    *codepoints = ncodepoints ;

  return invalid ;
}

/* Determine if a UTF-8 string is prefixed with a BOM. */
HqBool utf8_has_bom(const UTF8 *string, const UTF8 *limit)
{
  const UTF8 *units ;
  UTF32 unicode ;

  HQASSERT(string != NULL, "No UTF-8 string") ;
  HQASSERT(limit == NULL || limit >= string,
           "UTF-8 string limit is invalid") ;

  units = string;
  unicode = utf8_validate_next(&units, limit) ;

  return ( unicode == UNICODE_BOM );
}

/*
Log stripped */
