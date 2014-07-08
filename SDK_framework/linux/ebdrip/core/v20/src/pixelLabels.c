/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!src:pixelLabels.c(EBDSDK_P.1) $
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Translating between pixel labels, repro types, dispositions and RLE
 * object types
 */

#include "core.h"
#include "tables.h"

#include "pixelLabels.h"

#include "swrle.h"
#include "swpgb.h" /* SW_PGB_*_OBJECT */
#include "graphics.h"
#include "gschcms.h"
#include "display.h"

/* Repro type mapping to turn the augmented repro types used for render
   properties into normal repro types. This table MUST match the order of
   repro types from gschcms.h, augmented with the extra types from
   display.h. */
const uint8 disposition_reproTypes[REPRO_N_DISPOSITIONS] = {
  REPRO_TYPE_PICTURE,
  REPRO_TYPE_TEXT,
  REPRO_TYPE_VIGNETTE,
  REPRO_TYPE_OTHER,
  REPRO_TYPE_OTHER,      /* ERASE */
  REPRO_TYPE_OTHER,      /* Forced debug marks, watermarks */
  REPRO_TYPE_PICTURE     /* RENDER Groups, backdrop images */
} ;


uint8 pixelLabelFromDisposition(uint8 disposition)
{
  /* Pixel labels corresponding to repro types. This table MUST match
     the order of repro types from gschcms.h, augmented with the extra
     types from display.h. */
  static const uint8 repro_labels[REPRO_N_DISPOSITIONS] = {
    SW_PGB_IMAGE_OBJECT,
    SW_PGB_TEXT_OBJECT,
    SW_PGB_VIGNETTE_OBJECT,
    SW_PGB_LW_OBJECT,   /* OTHER */
    0,                  /* ERASE */
    0,                  /* Forced debug marks, watermarks */
    SW_PGB_MIXED_OBJECT /* RENDER Groups, backdrop images */
  } ;
  uint8 reproType = DISPOSITION_REPRO_TYPE_UNMAPPED(disposition);

  HQASSERT(reproType < REPRO_N_DISPOSITIONS, "Not a valid repro type") ;
  HQASSERT(reproType < NUM_ARRAY_ITEMS(repro_labels), "Not enough repro labels") ;

  return repro_labels[reproType]
         | ((disposition & DISPOSITION_FLAG_USER) != 0 ? SW_PGB_USER_OBJECT : 0);
}


/* Return a pixel label for the given repro type.
*/
uint8 pixelLabelFromReproType(int32 reproType)
{
  switch (reproType) {
  case REPRO_TYPE_PICTURE:
    return SW_PGB_IMAGE_OBJECT;
  case REPRO_TYPE_TEXT:
    return SW_PGB_TEXT_OBJECT;
  case REPRO_TYPE_VIGNETTE:
    return SW_PGB_VIGNETTE_OBJECT;
  case REPRO_TYPE_OTHER:
    return SW_PGB_LW_OBJECT;
  case REPRO_TYPE_ILLEGAL:
  default:
    HQFAIL("Should not be getting disposition of an undefined color type") ;
    return 0;
  }
}

/* Return a halftone object type for the given pixel label.
*/
HTTYPE htTypeFromPixelLabel(uint8 label)
{
  /* Almost the reverse of pixelLabelFromReproType. */
  switch (label) {
  case SW_PGB_IMAGE_OBJECT:
    return REPRO_TYPE_PICTURE;
  case SW_PGB_TEXT_OBJECT:
    return REPRO_TYPE_TEXT;
  case SW_PGB_VIGNETTE_OBJECT:
    return REPRO_TYPE_VIGNETTE;
  case SW_PGB_LW_OBJECT:
    return REPRO_TYPE_OTHER;
  default: /* labels with multiple types use default */
    return HTTYPE_DEFAULT;
  }
}


/* ---------------------------------------------------------------------- */
uint32 pixelLabelProperties(uint8 label, uint32 colorantProperties)
{
  uint8 priority ; /* highest-priority label bits found */
  int8 highest_bit;
  static const uint32 bit_to_every_byte[] = {
    0x01010101, 0x02020202, 0x04040404, 0x08080808,
    0x10101010, 0x20202020, 0x40404040, 0x80808080
  };

  /* Select the highest-priority bits from the object's label. The
     highest label flag that was explicitly set in the colorant's rendering
     properties dictionary is used. If no flags were explicitly set, then the
     highest label flag that was implicitly set will be used. This allows
     /Text /Knockout to override a default /Render in a composited object, or
     /Picture /Mask to override /Text /Ignore for images in uncached Type 3
     characters. Backward compatibility is maintained, since DL objects will
     have at most one of repro type bits set. */
  priority = CAST_UNSIGNED_TO_UINT8(label & colorantProperties) ;
  if ( priority == 0 ) /* No explicit, look for implicit */
    priority = CAST_UNSIGNED_TO_UINT8(label & ~colorantProperties) ;

  HQASSERT(priority != 0, "Erase labels should have been removed already") ;
  highest_bit = highest_bit_set_in_byte[priority];
  HQASSERT(highest_bit >= 0, "Priority lookup got invalid value");
  HQASSERT(highest_bit < NUM_ARRAY_ITEMS(bit_to_every_byte),
           "Label out of property mask range") ;
  return (bit_to_every_byte[highest_bit] & colorantProperties);
}


/* Log stripped */
