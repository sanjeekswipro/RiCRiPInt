/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:uproperties.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode properties.
 *
 * The Unicode properties functions allow you to query the properties
 * on UTF32 characters.
 */

#include "hqunicode.h"
#include "uprivate.h"
#include "std.h" /* For HQASSERT, HQFAIL */

#ifdef HQNlibicu
#include "unicode/uchar.h"

HqBool unicode_has_binary_property(UTF32 c,
                                   UTF32_Property which,
                                   HqBool *error_occured )
{
  HQASSERT(error_occured != NULL, "error_occured is NULL") ;
  HQASSERT((int)UCHAR_BINARY_START == (int)UTF32_BINARY_START,
           "icu and hqn enumerations appear to be out of synch") ;
  HQASSERT((int)UCHAR_STRING_LIMIT == (int)UTF32_STRING_LIMIT,
           "icu and hqn enumerations appear to be out of synch") ;

  *error_occured = FALSE ;

  if ( !unicode_icu_ready() ) {
    *error_occured = TRUE ;
    return FALSE ;
  }

  return (HqBool)u_hasBinaryProperty((UChar32)c, (UProperty)which) ;
}

int8 unicode_char_type(UTF32 c,
                       HqBool *error_occured )
{
  HQASSERT(error_occured != NULL, "error_occured is NULL") ;
  HQASSERT((int)U_GENERAL_OTHER_TYPES == (int)UTF32_GENERAL_OTHER_TYPES,
           "icu and hqn enumerations appear to be out of synch") ;
  HQASSERT((int)U_CHAR_CATEGORY_COUNT == (int)UTF32_CHAR_CATEGORY_COUNT,
           "icu and hqn enumerations appear to be out of synch") ;

  *error_occured = FALSE ;

  if ( !unicode_icu_ready() ) {
    *error_occured = TRUE ;
    return U_UNASSIGNED ;
  }

  return (int8)u_charType((UChar32)c) ;
}

#else /* !HQNlibicu */

HqBool unicode_has_binary_property(UTF32 c, UTF32_Property which,
                                   HqBool *error_occured )
{
  HQASSERT(error_occured != NULL, "error_occured is NULL") ;
  UNUSED_PARAM(UTF32, c) ;
  UNUSED_PARAM(UTF32_Property, which) ;
  *error_occured = TRUE ;
  HQFAIL("Cannot determine Unicode character property without ICU") ;
  return FALSE ;
}

int8 unicode_char_type(UTF32 c,
                       HqBool *error_occured )
{
  HQASSERT(error_occured != NULL, "error_occured is NULL") ;
  UNUSED_PARAM(UTF32, c) ;
  *error_occured = TRUE ;
  HQFAIL("Cannot determine Unicode character type without ICU") ;
  return 0 ;
}

#endif /* !HQNlibicu */

/* Log stripped */
