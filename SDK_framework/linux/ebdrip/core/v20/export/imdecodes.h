/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:imdecodes.h(EBDSDK_P.1) $
 * $Id: export:imdecodes.h,v 1.5.2.1.1.1 2013/12/19 11:25:18 anon Exp $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image decodes arrays map image data to input color values.
 *
 * Image decodes are cached (only most recently used) for a particular
 * set of criteria (e.g. bits per component and number of components).
 * It's easy to add cache slots for more combinations if a particular
 * jobs demands it, just add to the enum and matching slot macro.
 *
 * Note, the cached entries are only freed when they are replaced by another
 * entry.
 */

#ifndef __IMDECODES_H__
#define __IMDECODES_H__

#include "images.h"

struct im_decodesobj_t;

enum {
  DECODESOBJ_SLOT_MASK,
  DECODESOBJ_SLOT_ALPHA,
  DECODESOBJ_SLOT_1BPP,
  DECODESOBJ_SLOT_8BPP_3NCOMPS,
  DECODESOBJ_SLOT_8BPP,
  DECODESOBJ_SLOT_DEFAULT,
  DECODESOBJ_NUM_SLOTS, /* Must be last! */
};

/* Decode arrays for mapping image values to input color values. */
Bool im_alloc_fdecodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata);

/* Decode arrays for mapping image values to input color values
   which are normalised for table conversion. */
Bool im_alloc_ndecodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata);

/* Decode arrays for mapping image values to input color values.  This is
   used for image preconversion when the image to be converted uses a LUT. */
Bool im_alloc_adjdecodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                         IMAGEOBJECT *imageobj, USERVALUE *colorvalues,
                         COLORANTINDEX *inputcolorants, Bool subtractive);

/* Frees fdecodes and ndecodes. */
void im_free_decodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata);

/* Reset the decodes cache after the dl pool has been destroyed. */
void im_reset_decodes(DL_STATE *page);

/* =============================================================================
* Log stripped */

#endif /* protection for multiple inclusion */
