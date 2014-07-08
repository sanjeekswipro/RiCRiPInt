/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:t1hint.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for Type 1 hinting
 */

#ifndef __T1HINT_H__
#define __T1HINT_H__


#include "fontt.h"
#include "graphict.h"
#include "chbuild.h"
#include "objecth.h"

/* Build a character path and return the width of a Type 1 character, with
   hinting. The decoder function translates the string object to calls onto
   the top charstring builder; since this may not be the top builder in the
   chain, it is given a specific place to put its own build methods. */
Bool t1hint_build_path(corecontext_t *context,
                       charstring_methods_t *methods,
                       OBJECT *stringo, charstring_decode_fn decoder,
                       charstring_build_t *buildnew,
                       charstring_build_t *buildtop,
                       PATHINFO *ch_path, ch_float *xwidth, ch_float *ywidth) ;

#if defined(DEBUG_BUILD)
void t1hint_debug_init(void) ;
void t1hint_method(int32 method) ;
#endif

/*
Log stripped */
#endif /* protection for multiple inclusion */
