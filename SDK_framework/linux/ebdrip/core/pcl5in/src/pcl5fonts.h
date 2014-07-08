/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5fonts.h(EBDSDK_P.1) $
 * $Id: src:pcl5fonts.h,v 1.50.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PCL5FONTS_H__
#define __PCL5FONTS_H__ (1)

#include "fcache.h"           /* char_selector_t */
#include "pcl5context.h"
#include "cursorpos.h"
#include "fontselection.h"

#include "eventapi.h"

/** \todo This is a first guess at how many pending chars to try to accumulate
 * before flushing them to the display list.  It will need to become adaptive.
 */
#define NUM_CHARS_PER_DL_ENTRY 32

#define MIN_CHARS_PER_DL_ENTRY 1
#define MAX_CHARS_PER_DL_ENTRY 256

/* FontState items which do not form part of the MPE */
typedef struct PCL5FontState {
  int32 underline_mode ;
  int32 orig_wmode ;
  int32 orig_vmode ;
  int32 DL_slots ;   /* How many more chars to ask for next time */
  Bool within_text_run ;
  Bool ctm_changed ; /* Since the last text run */
  Bool text_path_changed ; /* Never in the whole job - \todo this is a HACK */
#ifdef RQ64403
  Bool changed_text_mode ;
  Bool text_run_restart_required ;
#endif
  CursorPosition start_underline_position ;
  CursorPosition end_underline_position ;
  Bool within_underline_callback ;
  PCL5Real max_underline_dist ;
  PCL5Real max_underline_thickness ;
  Bool delete_font_ev_handler_error ;
} PCL5FontState ;

/**
 * Initialise PCL5FontState
 */
void set_default_font_state(PCL5FontState *font_state) ;

/* Be sure to update default_text_info() if you change this structure. */
/* N.B. The vertical text path and text parsing method are listed in the
 * PCL5 Quick Reference as part of the PageControl section of the MPE.
 * However, they seem more naturally related to fonts.
 */
/** \todo We may want to make the PCL5 FontInfo part of this structure */
typedef struct TextInfo {
  uint32       text_parsing_method ; /* 1 or 2 byte parsing ? */
  Bool         vertical_text_path ;
  Bool         overstrike_pending ;  /* Had BS in proportional font since last text char? */
  int32        last_char_width ;
} TextInfo ;

/**
 * Initialise TextInfo
 */
void default_text_info(TextInfo* self) ;

/**
 * Get hold of the TextInfo
 */
TextInfo* get_text_info(PCL5Context *pcl5_ctxt) ;

/**
 * PFIN event handler
 */
sw_event_result HQNCALL delete_font(void *context, sw_event* ev) ;

/**
 * Callback for when a glyph does not exist.
 */
Bool pcl5_font_notdef(char_selector_t*  selector, int32 type,
                      int32 charCount, FVECTOR *advance,
                      void *data) ;

/* Overwrite default PCL5 font settings from PJL */
Bool pcl5fonts_apply_pjl_changes(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params, Bool *font_info_provided, int32 *font_number, int32 *font_source) ;

/* Set the default symbolset */
Bool set_default_symbolset(void) ;

/* Set up the default PCL5 fonts */
Bool default_pcl5fonts(PCL5Context *pcl5_ctxt, int32 font_number, int32 font_source, Bool info_provided) ;

#ifdef RQ64403
/* Flag that any new text run needs to be setup before the text run
   starts. */
Bool pcl5_set_text_run_restart_required(PCL5Context *pcl5_ctxt, Bool force) ;
#endif
/* About to start a text run. */
Bool pcl5_start_text_run(PCL5Context *pcl5_ctxt, FontSelInfo **font_sel_info,
                         FontInfo **font_info, Font **font) ;

/* Ending a text run. */
Bool pcl5_end_text_run(PCL5Context *pcl5_ctxt, Bool force) ;

/* Flush any DL text run */
Bool pcl5_flush_text_run(PCL5Context *pcl5_ctxt, int32 slots) ;

/* Text character callback which is always called inbetween
   pcl5_start_text_run() and pcl5_end_text_run() . */
Bool pcl5_text_character(PCL5Context *pcl5_ctxt, FontInfo *font_info, Font **font, uint16 ch) ;

/* Used when looking ahead for a character continuation block. */
Bool implicit_end_character_data(PCL5Context *pcl5_ctxt) ;

/**
 * Get hold of the current PCL font info.
 */
FontInfo* pcl5_get_font_info(PCL5Context *pcl5_ctxt) ;

FontMgtInfo* pcl5_get_font_management(PCL5Context*  pcl5_ctxt) ;

/* Handle text underlining. */
Bool underline_callback(PCL5Context *pcl5_ctxt, Bool force_draw) ;

Bool reset_underline_details(PCL5Context *pcl5_ctxt) ;

/**
 * Is wide_ch the first byte of a two byte character?
 * N.B. Assumes 2-byte character support is enabled.
 */
Bool is_first_byte_of_two_byte_char(PCL5Context *pcl5_ctxt, uint16 wide_ch) ;

/**
 * Grab a byte from the PCL5Context filestream and add it to wide_ch
 * as the second byte of a 2 byte character.
 * It is assumed that on entry, the value of wide_ch is equal to the
 * value of the first byte.
 * Indicate whether EOF was reached.
 */
void add_second_byte_to_two_byte_char(PCL5Context *pcl5_ctxt,
                                      uint16 *wide_ch,
                                      Bool *eof_reached) ;

enum {
  FONTCTRL_DELETE_ALL = 0,
  FONTCTRL_DELETE_ALLTEMP,
  FONTCTRL_DELETE_FONTID,
  FONTCTRL_DELETE_FONTID_CHARCODE,
  FONTCTRL_MAKETEMP_FONTID,
  FONTCTRL_MAKEPERM_FONTID,
  FONTCTRL_CURRENTTEMP_FONTID
};

extern
Bool do_symset_control(
  PCL5Context*  pcl5_ctxt,
  int32         command);

enum {
  SYMSETCTRL_DELETE_ALL = 0,
  SYMSETCTRL_DELETE_ALLTEMP,
  SYMSETCTRL_DELETE_SSID,
  SYMSETCTRL_MAKETEMP_SSID = 4,
  SYMSETCTRL_MAKEPERM_SSID
};

extern
Bool do_font_control(
  PCL5Context*  pcl5_ctxt,
  int32         command);

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */
Bool pcl5op_star_c_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_F(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* This operator is not really a font operator but declaring in this
   header is as good as anywhere. */
Bool pcl5op_ampersand_n_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_f_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_E(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_control_SI(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_control_SO(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_left_paren_symbolset(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_s_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_left_paren_at(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_symbolset(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_s_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_right_paren_at(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_p_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_d_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_d_at(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_k_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_c_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_t_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
