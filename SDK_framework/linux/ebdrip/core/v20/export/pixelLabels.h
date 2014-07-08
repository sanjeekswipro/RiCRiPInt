/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!export:pixelLabels.h(EBDSDK_P.1) $
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The various pixel labels and rendering properties, and methods to
 * obtain them from repro types and dispositions
 *
 * The pixel labels encode the rendering property disposition of an
 * object.  They are also used for RLE object types (after a translation
 * which happens to be identity at the moment).  They are now defined as
 * SW_PGB_*_OBJECT in swpgb.h.  They are a flag set so that composited
 * and overprinted objects can exhibit more than one disposition.  The
 * bit position determines the priority when multiple dispositions are
 * present; the rendering properties of the highest bit for which a
 * disposition was explicitly set are preferred.
 */

#ifndef __PIXEL_LABELS_H__
#define __PIXEL_LABELS_H__

#include "swpgb.h"


/** An invalid value for pixel labels. */
#define SW_PGB_INVALID_OBJECT SW_PGB_COMPOSITED_OBJECT
  /* (SW_PGB_COMPOSITED_OBJECT cannot occur on its own) */


/** A pseudo-label for DL objects standing for a collection of objects.

  E.g., groups, backdrop images and mixed-colorant objects. */
#define SW_PGB_MIXED_OBJECT (SW_PGB_IMAGE_OBJECT | SW_PGB_VIGNETTE_OBJECT)
  /* This should not be able to arise through overprints, because images and
     vignettes (shfills) don't overprint each other. It doesn't have the
     composited bit set, so can't arise through transparency either. */


/* Rendering property bits. There are several bits for each of the repro
   types. The bits could be packed into less space since they are
   mutually exclusive, but it would be more trouble to test. The rendering
   properties mask for each colorant has 4 groups of 8 disposition bits each.
   The lowest 8 bits are used to indicate if the colorant disposition was
   explicitly set in addtoseparationorder.

   Bits for all repro types/dispositions for a specific
   property. Render and Mask are mutually exclusive within a separation (not
   for any technical reason, but because it doesn't make sense). */
#define RENDERING_PROPERTY_EXPLICIT_ALL   0x000000ffu
#define RENDERING_PROPERTY_RENDER_ALL     0x0000ff00u
#define RENDERING_PROPERTY_MASK_ALL       0x00ff0000u
#define RENDERING_PROPERTY_KNOCKOUT_ALL   0xff000000u
#define RENDERING_PROPERTY_IGNORE_ALL     0 /* Ignore if no bits set */

/* Rendering property macros; these test if any of the types has the property
   specified. The types mask, render, knockout and ignore are mutually
   exclusive for any individual disposition property. */
#define RENDERING_PROPERTY_HAS_MASK(_p) \
  (((_p) & RENDERING_PROPERTY_MASK_ALL) != 0)
#define RENDERING_PROPERTY_HAS_RENDER(_p) \
  (((_p) & RENDERING_PROPERTY_RENDER_ALL) != 0)
#define RENDERING_PROPERTY_HAS_KNOCKOUT(_p) \
  (((_p) & RENDERING_PROPERTY_KNOCKOUT_ALL) != 0)
#define RENDERING_PROPERTY_IS_IGNORE(_p) \
  (((_p) & ~RENDERING_PROPERTY_EXPLICIT_ALL) == 0)


/* Express a disposition for all properties. */
#define ALL_PROPERTIES(disp) \
  ((uint32)((disp) | ((disp) << 8) | ((disp) << 16) | ((disp) << 24)))

/* Masks for all properties of each disposition. */

#define RENDERING_PROPERTY_USER ALL_PROPERTIES(SW_PGB_USER_OBJECT)
#define RENDERING_PROPERTY_NAMEDCOLOR ALL_PROPERTIES(SW_PGB_NAMEDCOLOR_OBJECT)
#define RENDERING_PROPERTY_BLACK ALL_PROPERTIES(SW_PGB_BLACK_OBJECT)
#define RENDERING_PROPERTY_LW ALL_PROPERTIES(SW_PGB_LW_OBJECT)
#define RENDERING_PROPERTY_TEXT ALL_PROPERTIES(SW_PGB_TEXT_OBJECT)
#define RENDERING_PROPERTY_VIGNETTE ALL_PROPERTIES(SW_PGB_VIGNETTE_OBJECT)
#define RENDERING_PROPERTY_PICTURE ALL_PROPERTIES(SW_PGB_IMAGE_OBJECT)
#define RENDERING_PROPERTY_COMPOSITE ALL_PROPERTIES(SW_PGB_COMPOSITED_OBJECT)


/* Return a label corresponding to an object disposition. The object
   disposition includes reproType, colortype, and flags for LW and Text. */
uint8 pixelLabelFromDisposition(uint8 disposition) ;

/* Return a label for the given reproduction type. */
uint8 pixelLabelFromReproType(int32 reproType);

/* Return a halftone object type for the given pixel label. */
HTTYPE htTypeFromPixelLabel(uint8 label);

/* Return the top priority properties mask for a label, given a colorant's
   rendering properties. */
uint32 pixelLabelProperties(uint8 label, uint32 colorantProperties) ;

#endif

/* Log stripped */
