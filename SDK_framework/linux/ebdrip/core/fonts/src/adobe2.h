/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:adobe2.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for Type 2 (CFF) charstring decoding
 */

#ifndef __ADOBE2_H__
#define __ADOBE2_H__

#include "fontt.h"
#include "chbuild.h"
#include "objecth.h"

Bool decode_adobe2_outline(corecontext_t *context,
                           charstring_methods_t *charfns,
                           OBJECT *stringo,
                           charstring_build_t *buildfns) ;

/* Log stripped */
#endif /* Protection from multiple inclusion */
