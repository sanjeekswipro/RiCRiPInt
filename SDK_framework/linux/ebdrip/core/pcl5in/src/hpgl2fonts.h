/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2fonts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Characters Group" category.
 */

#ifndef __HPGL2FONTS_H__
#define __HPGL2FONTS_H__

#include "pcl5context.h"
#include "fontselection.h"
#include "matrix.h"   /* OMATRIX */

/* Be sure to update default_HPGL2_character_info() if you change this structure. */
typedef struct HPGL2CharacterInfo {
  FontInfo font_info ;
  uint8 label_terminator ; /** The LB user-defined terminator. */
  uint8 label_terminator_mode ; /** Print terminator char, or not. */
  uint8 label_origin ;
  uint8 text_direction ;
  uint8 linefeed_direction ;
  uint8 transparent_data_mode ; /** How to handle CR, LF etc chars. */
  uint8 direction_mode ;
  uint8 char_size_mode ;
  uint8 scalable_bitmap ;
  uint8 fill_mode ;
  int32 edge_pen ;
  Bool edge_pen_specified ;
  HPGL2Real extra_space_width ;
  HPGL2Real extra_space_height ;
  HPGL2Real direction_run ;
  HPGL2Real direction_rise ;
  HPGL2Real char_size_width ;
  HPGL2Real char_size_height ;
  HPGL2Real stick_pen_width_factor ;
  HPGL2Real slant_angle ;
  Bool degenerate_matrices ;
  OMATRIX glyphs_matrix ;  /* bitmap * scale * slant * rotate */
  OMATRIX advance_matrix ;  /* scale * slant * rotate */
  OMATRIX lineend_matrix ; /* bitmap * scale * rotate */
  OMATRIX glyphs_inverse ;
  OMATRIX lineend_inverse ;
  HPGL2Real left_margin[2] ; /* For BS and HT handling */
  uint8 label_mode;
  uint16 label_row;
} HPGL2CharacterInfo;

/**
 * Initialise default character info.
 */
void default_HPGL2_character_info(HPGL2CharacterInfo* self);

/**
 * Set the HPGL2 and PS fonts.
 */
Bool hpgl2_set_font(PCL5Context *pcl5_ctxt, FontInfo *font_info);


/**
 * set default values for char info, depending on whether
 * context is DF or IN operation.
 */
void hpgl2_set_default_character_info(HPGL2CharacterInfo *char_info,
                                      Bool initialize);

/**
 * Synchronize the interpreter gstate with the HPGL2 Character info.
 */
void hpgl2_sync_character_info(HPGL2CharacterInfo *char_info, Bool initialize);

Bool hpgl2_glyph_bbox(PCL5Context *pcl5_ctxt, uint16 glyph, sbbox_t *bbox) ;

Bool hpgl2_draw_symbol(PCL5Context *pcl5_ctxt, uint16 glyph) ;

/**
 * Parse 1 or 2 byte characters from the PCL5 input, depending on
 * Label Mode and support for 2 byte characters.
 */
Bool hpgl2_read_label_character(PCL5Context *pcl5_ctxt, int32 *res);

/**
 * Indicate if a multibyte font is currently selected.
 */
Bool hpgl2_multi_byte_font(HPGL2CharacterInfo *char_info);

/**
 * Calculate the symbol mode character, accounting label mode and the
 * whether current font is wide.
 */
uint16 hpgl2_get_SM_char(PCL5Context *pcl5_ctxt);

Bool hpgl2op_AD(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_CF(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_CP(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_DI(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_DR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_DT(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_DV(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_ES(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_FI(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_FN(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_LB(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_LO(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SA(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SB(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SD(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SI(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SL(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SS(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_TD(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_LM(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
