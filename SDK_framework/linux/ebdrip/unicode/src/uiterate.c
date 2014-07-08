/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:uiterate.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode manipulation library UTF iteration.
 *
 * The Unicode manipulation functions provide rudimentary facilities to convert
 * between Unicode transformation representations, to compare normalised Unicode
 * sequences, and to iterate over Unicode sequences. These routines provide
 * iterators for Unicode sequences.
 */

#include "hqunicode.h"
#include "uprivate.h"
#include "std.h" /* For HQASSERT, HQFAIL */

/* Initialise an iteration over a UTF-8 sequence */
void utf8_iterator_init(utf8_buffer *iterator, UTF8 *string, uint32 length)
{
  HQASSERT(iterator != NULL, "No UTF-8 iterator to initialise") ;
  HQASSERT((length == 0) || (string != NULL), "No UTF-8 string to initialise iterator with") ;

  iterator->codeunits = string ;
  iterator->unitlength = length ;
}

/* Extract next code point from a UTF-8 iterator, incrementing the
   iterator. Use this function only when the UTF-8 string is known to
   be valid; it will assert that the UTF-8 representation is correct.
 */
UTF32 utf8_iterator_get_next(utf8_buffer *iterator)
{
  UTF8 *units ;
  UTF8 byte ;
  uint32 remaining, length ;
  UTF32 unicode ;

  HQASSERT(iterator != NULL, "No UTF-8 iterator") ;
  if ( (remaining = iterator->unitlength) == 0 )
    return -1 ;

  units = iterator->codeunits ;
  HQASSERT(units != NULL, "No UTF-8 code units") ;

  byte = *units++ ;

  length = utf8_length[byte] ;
  HQASSERT(length != 0, "Invalid UTF-8 start byte") ;
  HQASSERT(length <= 4, "Invalid length in UTF-8 table") ;
  HQASSERT(length <= remaining, "Too few UTF-8 bytes") ;
  remaining -= length ;

  unicode = (byte & utf8_mask[length]) ;

  /* We use XOR 0x80 in to remove the high bit of the trailing bytes so that
     the compiler has fewer constants to manage; 0x80 is probably already
     loaded in a register because of the assert. */
  switch ( length ) {
  case 4:
    HQASSERT((*units & 0xc0) == 0x80, "Invalid UTF-8 trailing byte") ;
    unicode = (unicode << 6) | (*units++ ^ 0x80) ;
    /*@fallthrough@*/
  case 3:
    HQASSERT((*units & 0xc0) == 0x80, "Invalid UTF-8 trailing byte") ;
    unicode = (unicode << 6) | (*units++ ^ 0x80) ;
    /*@fallthrough@*/
  case 2:
    HQASSERT((*units & 0xc0) == 0x80, "Invalid UTF-8 trailing byte") ;
    unicode = (unicode << 6) | (*units++ ^ 0x80) ;
    /*@fallthrough@*/
  case 1:
    break ;
  }

  HQASSERT(unicode >= utf8_minimum[length],
           "Invalid UTF-8 encoding (not shortest form)") ;
  HQASSERT((unicode >= 0 && unicode <= 0xd7ff) ||
           (unicode >= 0xe000 && unicode <= 0x10ffff),
           "UTF8 represents invalid code point") ;

  iterator->codeunits = units ;
  iterator->unitlength = remaining ;

  return unicode ;
}

/* Initialise an iteration over a UTF-16 sequence */
void utf16_iterator_init(utf16_buffer *iterator, UTF16 *string, uint32 length)
{
  HQASSERT(iterator != NULL, "No UTF-16 iterator to initialise") ;
  HQASSERT((length == 0) || (string != NULL), "No UTF-16 string to initialise iterator with") ;

  iterator->codeunits = string ;
  iterator->unitlength = length ;
}

/* Extract next code point from a UTF-16 iterator, incrementing the
   iterator. Use this function only when the UTF-16 string is known to
   be valid; it will assert that the UTF-16 representation is correct. */
UTF32 utf16_iterator_get_next(utf16_buffer *iterator)
{
  UTF16 *units ;
  uint32 remaining ;
  UTF32 unicode ;

  HQASSERT(iterator != NULL, "No UTF-16 iterator") ;
  if ( (remaining = iterator->unitlength) == 0 )
    return -1 ;

  units = iterator->codeunits ;
  HQASSERT(units != NULL, "No UTF-16 code units") ;

  unicode = *units++ ;
  remaining -= 1 ;

  if ( UTF16_IS_SURROGATE(unicode) ) {
    HQASSERT(unicode >= UTF16_SURROGATE_HIGH &&
             unicode <= UTF16_SURROGATE_HIGH + UTF16_SURROGATE_MASK,
             "UTF16 first surrogate is not high") ;
    HQASSERT(remaining != 0, "Too few UTF-16 code units for surrogate pair") ;
    HQASSERT(*units >= UTF16_SURROGATE_LOW &&
             *units <= UTF16_SURROGATE_LOW + UTF16_SURROGATE_MASK,
             "UTF16 second surrogate is not low") ;

    unicode = ((unicode & UTF16_SURROGATE_MASK) << UTF16_SURROGATE_BITS) +
      (*units++ & UTF16_SURROGATE_MASK) + 0x10000 ;
    remaining -= 1 ;
  }

  HQASSERT((unicode >= 0 && unicode <= 0xd7ff) ||
           (unicode >= 0xe000 && unicode <= 0x10ffff),
           "UTF16 represents invalid code point") ;

  iterator->codeunits = units ;
  iterator->unitlength = remaining ;

  return unicode ;
}

/*
Log stripped */
