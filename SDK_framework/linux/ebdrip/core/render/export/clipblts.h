/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!export:clipblts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Clip blitting routines and structures.
 */

#ifndef __CLIPBLTS_H__
#define __CLIPBLTS_H__ 1

#include "bitbltt.h" /* basic typedefs */
#include "objnamer.h" /* OBJECT_NAME_MEMBER */

/** Clear an RLE clip form. */
Bool rleclip_initform(FORM *form) ;

/** Convert a form from RLE clip encoding (a series of spanlists) to a bitmap,
    restricting to a vertical section of the form for efficiency.

    \param theform A form to convert from RLE clip to bitmap.

    \param tmp_mem A spare block of memory at least as long as one line of
    the form (theform->l bytes long).

    \param x1 The left limit of the area to convert. This is typically set to
    the clip limit, to avoid wasting time clearing parts of the form that are
    not going to be used. The area touched may be rounded down to a \c blit_t
    boundary.

    \param x2 The right limit of the area to convert. This is typically set to
    the clip limit, to avoid wasting time clearing parts of the form that are
    not going to be used. The area touched may be rounded up to a \c blit_t
    boundary.
 */
void bandrleencoded_to_bandbitmap(FORM *theform, blit_t *tmp_mem,
                                  dcoord x1, dcoord x2);


#endif /* protection for multiple inclusion */

/* Log stripped */
