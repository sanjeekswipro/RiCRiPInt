/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2misc.h(EBDSDK_P.1) $
 * $Id: src:hpgl2misc.h,v 1.8.6.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Misc" category.
 */

#ifndef __HPGL2MISC_H__
#define __HPGL2MISC_H__

#include "pcl5context.h"

#define HPGL2_DEFAULT_BITS_PER_INDEX (3)
#define HPGL2_DEFAULT_PEN_COUNT (1 << HPGL2_DEFAULT_BITS_PER_INDEX )


/* define 256 as largest palette size. This value must be a power of 2.
 * 256 is max palette size in PCL and having HPGL palette match that means
 * RF can be treated as colored patterns.
 */
#define HPGL2_MAX_PENS (256)

struct ColorPalette;

/* Be sure to update default_HPGL2_palette_extension_info()
 * if you change this structure. */
typedef struct HPGL2PaletteExtensionInfo {
  /* Color range values; black and white points, per color component. */
  HPGL2Real     CR_black[3];
  HPGL2Real     CR_white[3];
} HPGL2PaletteExtensionInfo;

/** Allocate a pen width table for the current palette */
HPGL2Real* alloc_pen_width_table( uint32 bits_per_index);

/** ensure current active palette has a pen width table, creating a new one
 * if pen width table not already existent. */
Bool check_pen_width_table(PCL5Context *pcl5_ctxt);

/** Helper function; copies pen width data from source to targert palette.
 * Copies only as much data from the source as will fit in the target.
 */
void hpgl2_copy_pen_width_table(struct ColorPalette *target,
                                struct ColorPalette *source);

Bool hpgl2_pen_width_tables_equal_size(struct ColorPalette *a,
                                       struct ColorPalette *b);

void hpgl2_default_pen_values(struct ColorPalette *palette, HPGL2Integer pen);

/* Palette extension default values. */
void default_HPGL2_palette_extension_info(
    HPGL2PaletteExtensionInfo *hpgl2_palette_extention);
/* Palette extension IN values. */
void hpgl2_set_default_palette_extension_info(
    HPGL2PaletteExtensionInfo *hpgl2_palette_extention, Bool init);

  /* (TL, XT and YT are unsupported) */
Bool hpgl2op_CR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_NP(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PC(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
