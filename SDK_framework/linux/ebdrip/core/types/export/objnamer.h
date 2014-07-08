/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:objnamer.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Allows objects to be named and verified.
 * Assumes std.h has been included.
 */

#ifndef __OBJECTNAMER_H__
#define __OBJECTNAMER_H__

#if defined( ASSERT_BUILD )

#define OBJECT_NAME_MEMBER uint32 objectNameMember;

/* Compute PJW hash of a variable-length string */
/*@notfunction@*/
#define OBJECT_NAMER_HASH(str_, hash_) MACRO_START                      \
  char *_str_ = (str_) ;                                                \
  uint32 _hash_ = 0, _bits_ = 0 ;                                       \
  while ( *_str_ != '\0' ) {                                            \
    _hash_ = (_hash_ << 4) + *_str_++ ;                                 \
    _bits_ = _hash_ & 0xf0000000u ;                                     \
    _hash_ ^= _bits_|(_bits_ >> 24) ;                                   \
  }                                                                     \
  (hash_) = _hash_ ;                                                    \
MACRO_END

/*@notfunction@*/
#define NAME_OBJECT(object, name) MACRO_START                           \
  HQASSERT((object) != NULL, "No object to name") ;                     \
  OBJECT_NAMER_HASH((name), (object)->objectNameMember) ;               \
MACRO_END

/*@notfunction@*/
#define UNNAME_OBJECT(object) MACRO_START                               \
  HQASSERT((object) != NULL, "No object to unname");                    \
  (object)->objectNameMember = 0;                                       \
MACRO_END

/*@notfunction@*/
#define VERIFY_OBJECT(object, name) MACRO_START                         \
  uint32 _namehash_ ;                                                   \
  OBJECT_NAMER_HASH((name), _namehash_) ;                               \
  HQASSERT((object) != NULL, "No object to verify");                    \
  HQASSERT((object)->objectNameMember == _namehash_, "Corrupt '" name "' object encountered"); \
MACRO_END

#else /* ! defined( ASSERT_BUILD ) */

#define OBJECT_NAME_MEMBER /* No field */
#define NAME_OBJECT(object, name)
#define UNNAME_OBJECT(object)
#define VERIFY_OBJECT(object, name)

#endif /* defined( ASSERT_BUILD ) */

/* --Description--

NOTE.1: For the UnnameObject macro to work, objects should be give non-zero
names.

This file provides methods for verifying that an object is of the expected
type.

By placing the OBJECT_NAME_MEMBER macro (WITHOUT a trailing semicolon) as
the last element of each object's structure definition, and using the
NAME_OBJECT (in constructors) and VERIFY_OBJECT (in methods), you are able
to check that you have a reasonably valid object pointer.

Object names are strings, but the macros compute a hash of the name which is
stored in the object name member.

Immediately prior to deallocating an object, use UNNAME_OBJECT - this sets
the object's name to zero. This will cause VERIFY_OBJECT to assert when a
method is passed a deallocated object (see NOTE.1).

All trace of the name verification system is removed when RELEASE_BUILD is
defined.
*/

#endif

/* Log stripped */
