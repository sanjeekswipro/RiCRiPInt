/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:chbuild.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Type 1/2 charstring building interface. Charstring interpreters call out
 * through this interface to add line segments and hints to characters.
 */

#ifndef __CHBUILD_H__
#define __CHBUILD_H__

#include "fontt.h"
#include "graphict.h"
#include "objecth.h"

/* A floating point type with enough resolution to represent coordinates in
   the character. "float" should be sufficient for Type 1 and Type 2
   characters; Type 1 characters may contain integers up to 4 bytes, but they
   must be divided to be in the range +/-32000 before use as a coordinate.
   Type 2 can represent up to 16 bit integers and fixed point 16.16 values.
   However, we'll use double so we don't have to cast back and forth from the
   Type 1/2 stack values (which are represented using double). */
typedef double ch_float ;

struct charstring_build_t {
  /* private data, passed back to all chbuild routines by charstring
     decoders. */
  void *data ;

  /* To pass outline and hints back to the character outline code. */
  Bool (*initchar)( void *data ) ;

  Bool (*setbearing)( void *data, ch_float xbear , ch_float ybear ) ;
  Bool (*setwidth)( void *data, ch_float xwidth , ch_float ywidth ) ;

  Bool (*hstem)(void *data, ch_float y1, ch_float y2, Bool tedge, Bool bedge, int32 index) ;
  Bool (*vstem)(void *data, ch_float y1, ch_float y2, Bool tedge, Bool bedge, int32 index) ;
  Bool (*hintmask)( void *data, int32 index, Bool activate ) ;
  Bool (*cntrmask)( void *data, int32 index, uint32 group ) ;

  Bool (*flex)( void *data, ch_float curve_a[6], ch_float curve_b[6],
                ch_float depth, ch_float thresh, Bool hflex) ;

  Bool (*dotsection)( void *data ) ;

  /* Changing hints is similar to deactivating hints, however the indices are
     re-set so that they cannot be re-used. */
  Bool (*change)( void *data ) ;

  Bool (*moveto)( void *data, ch_float x , ch_float y ) ;
  Bool (*lineto)( void *data, ch_float x , ch_float y ) ;
  Bool (*curveto)( void *data, ch_float curve[6] ) ;
  Bool (*closepath)( void *data ) ;

  /* Finish building a character. endchar is ALWAYS called if initchar
     succeeds, regardless of whether the character could be built. */
  Bool (*endchar)( void *data, Bool result ) ;
} ;

/* Typedef for a converter function that decodes a string with the help of
   charstring methods, using a char builder interface to create a path and
   store the character width. */
typedef Bool (*charstring_decode_fn)(corecontext_t *context,
                                     charstring_methods_t *t1fns,
                                     OBJECT *stringo,
                                     charstring_build_t *buildfns) ;

Bool ch_build_path(corecontext_t *context,
                   charstring_methods_t *t1fns,
                   OBJECT *stringo,
                   charstring_decode_fn decoder,
                   charstring_build_t *buildnew,
                   charstring_build_t *buildtop,
                   PATHINFO *path, ch_float *xwidth, ch_float *ywidth) ;

/* Log stripped */
#endif /* protection for multiple inclusion */
