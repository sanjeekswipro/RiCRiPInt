/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxloperators.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public declarations for all the operator handling functions
 * Note there are currently stub implementations of
 * all these functions in pclxloperators.c
 * I expect these stub implementations to be replaced by the real implementations
 * But this file can still contain their declarations.
 */

#ifndef __PCLXLOPERATORS_H__
#define __PCLXLOPERATORS_H__ 1

#include "pclxlparsercontext.h"

/**
 * \brief this structure contains basic details associated with
 * a PCLXL "operator" tag (of which there are currently 97).
 * These details include a name, a protocol version in which they were first introduced.
 */

typedef Bool PCLXL_OP_FN(PCLXL_PARSER_CONTEXT parser_context);

typedef PCLXL_OP_FN(*PCLXL_OP_FN_PTR);

/**
 * \brief given an operator byte-code tag
 * returns a string version for the XL tag byte value.
 * This basically hides the details storage and look-up mechanism
 */
extern
uint8* pclxl_get_tag_string(
  PCLXL_TAG op_tag);

/**
 * \brief given an operator byte-code tag
 * returns a string version for the XL tag byte value dependent on the type of
 * the error.
 * This basically hides the details storage and look-up mechanism
 */
extern
uint8* pclxl_error_get_tag_string(
  PCLXL_TAG op_tag,
  int32     error_code);

/**
 * \brief takes a PCLXL "operator" tag
 * (must be in the range 0x41 "BeginSession" and 0xbf "PassThrough"
 *
 * It looks up the associated operator details,
 * performs any validation which includes checking
 * that the stream protocol class version and
 * operator protocol class version are compatible
 * and then calls the handler function
 *
 * It also clears up (i.e. deletes the attributes from)
 * the attribute list after the operator handler has been called
 */

extern Bool
pclxl_handle_operator(PCLXL_TAG op_tag,
                      PCLXL_PARSER_CONTEXT parser_context);

/*
 * The following is the complete list of operator
 * handling functions.
 * Note that their implementation is spread across several implementation files
 *
 * the "pclxloperators.c" file basically contains the look-up table
 * and the pclxl_get_operator_details() and pclxl_handle_operator()
 * functions.
 */

extern PCLXL_OP_FN pclxl_op_begin_session;
extern PCLXL_OP_FN pclxl_op_end_session;
extern PCLXL_OP_FN pclxl_op_begin_page;
extern PCLXL_OP_FN pclxl_op_end_page;

extern PCLXL_OP_FN pclxl_op_vendor_unique;

extern PCLXL_OP_FN pclxl_op_comment;
extern PCLXL_OP_FN pclxl_op_open_data_source;
extern PCLXL_OP_FN pclxl_op_close_data_source;

extern PCLXL_OP_FN pclxl_op_begin_font_header;
extern PCLXL_OP_FN pclxl_op_read_font_header;
extern PCLXL_OP_FN pclxl_op_end_font_header;
extern PCLXL_OP_FN pclxl_op_begin_char;
extern PCLXL_OP_FN pclxl_op_read_char;
extern PCLXL_OP_FN pclxl_op_end_char;
extern PCLXL_OP_FN pclxl_op_remove_font;

extern PCLXL_OP_FN pclxl_op_set_char_attributes;
extern PCLXL_OP_FN pclxl_op_set_default_gs;
extern PCLXL_OP_FN pclxl_op_set_color_treatment;

extern PCLXL_OP_FN pclxl_op_begin_stream;
extern PCLXL_OP_FN pclxl_op_read_stream;
extern PCLXL_OP_FN pclxl_op_end_stream;
extern PCLXL_OP_FN pclxl_op_exec_stream;
extern PCLXL_OP_FN pclxl_op_remove_stream;
extern PCLXL_OP_FN pclxl_op_pop_gs;
extern PCLXL_OP_FN pclxl_op_push_gs;
extern PCLXL_OP_FN pclxl_op_set_clip_replace;
extern PCLXL_OP_FN pclxl_op_set_brush_source;
extern PCLXL_OP_FN pclxl_op_set_char_angle;
extern PCLXL_OP_FN pclxl_op_set_char_scale;
extern PCLXL_OP_FN pclxl_op_set_char_shear;
extern PCLXL_OP_FN pclxl_op_set_clip_intersect;
extern PCLXL_OP_FN pclxl_op_set_clip_rectangle;
extern PCLXL_OP_FN pclxl_op_set_clip_to_page;
extern PCLXL_OP_FN pclxl_op_set_color_space;
extern PCLXL_OP_FN pclxl_op_set_cursor;
extern PCLXL_OP_FN pclxl_op_set_cursor_rel;
extern PCLXL_OP_FN pclxl_op_set_halftone_method;
extern PCLXL_OP_FN pclxl_op_set_fill_mode;
extern PCLXL_OP_FN pclxl_op_set_font;
extern PCLXL_OP_FN pclxl_op_set_line_dash;
extern PCLXL_OP_FN pclxl_op_set_line_cap;
extern PCLXL_OP_FN pclxl_op_set_line_join;
extern PCLXL_OP_FN pclxl_op_set_miter_limit;
extern PCLXL_OP_FN pclxl_op_set_page_default_CTM;
extern PCLXL_OP_FN pclxl_op_set_page_origin;
extern PCLXL_OP_FN pclxl_op_set_page_rotation;
extern PCLXL_OP_FN pclxl_op_set_page_scale;
extern PCLXL_OP_FN pclxl_op_set_paint_tx_mode;
extern PCLXL_OP_FN pclxl_op_set_pen_source;
extern PCLXL_OP_FN pclxl_op_set_pen_width;
extern PCLXL_OP_FN pclxl_op_set_ROP;
extern PCLXL_OP_FN pclxl_op_set_source_tx_mode;
extern PCLXL_OP_FN pclxl_op_set_char_bold_value;
extern PCLXL_OP_FN pclxl_op_set_neutral_axis;
extern PCLXL_OP_FN pclxl_op_set_clip_mode;
extern PCLXL_OP_FN pclxl_op_set_path_to_clip;
extern PCLXL_OP_FN pclxl_op_set_char_sub_mode;
extern PCLXL_OP_FN pclxl_op_begin_user_defined_line_cap;
extern PCLXL_OP_FN pclxl_op_end_user_defined_line_cap;
extern PCLXL_OP_FN pclxl_op_close_sub_path;
extern PCLXL_OP_FN pclxl_op_new_path;
extern PCLXL_OP_FN pclxl_op_paint_path;

extern PCLXL_OP_FN pclxl_op_arc_path;
extern PCLXL_OP_FN pclxl_op_set_color_trapping;
extern PCLXL_OP_FN pclxl_op_set_adaptive_halftoning;
extern PCLXL_OP_FN pclxl_op_bezier_path;
extern PCLXL_OP_FN pclxl_op_bezier_rel_path;
extern PCLXL_OP_FN pclxl_op_chord;
extern PCLXL_OP_FN pclxl_op_chord_path;
extern PCLXL_OP_FN pclxl_op_ellipse;
extern PCLXL_OP_FN pclxl_op_ellipse_path;
extern PCLXL_OP_FN pclxl_op_line_path;
extern PCLXL_OP_FN pclxl_op_line_rel_path;
extern PCLXL_OP_FN pclxl_op_pie;
extern PCLXL_OP_FN pclxl_op_pie_path;
extern PCLXL_OP_FN pclxl_op_rectangle;
extern PCLXL_OP_FN pclxl_op_rectangle_path;
extern PCLXL_OP_FN pclxl_op_round_rectangle;
extern PCLXL_OP_FN pclxl_op_round_rectangle_path;

extern PCLXL_OP_FN pclxl_op_text;
extern PCLXL_OP_FN pclxl_op_text_path;

extern PCLXL_OP_FN pclxl_op_begin_image;
extern PCLXL_OP_FN pclxl_op_read_image;
extern PCLXL_OP_FN pclxl_op_end_image;
extern PCLXL_OP_FN pclxl_op_begin_rast_pattern;
extern PCLXL_OP_FN pclxl_op_read_rast_pattern;
extern PCLXL_OP_FN pclxl_op_end_rast_pattern;
extern PCLXL_OP_FN pclxl_op_begin_scan;

extern PCLXL_OP_FN pclxl_op_end_scan;
extern PCLXL_OP_FN pclxl_op_scan_line_rel;
extern PCLXL_OP_FN pclxl_op_pass_through;

/* This is required when PassThrough is in effect as we may have
   cached operator arguments. */

extern Bool
pclxl_pop_gs(PCLXL_CONTEXT pclxl_context,
             Bool          job_requested_pop_gs);

extern Bool
pclxl_push_gs(PCLXL_CONTEXT pclxl_context,
              uint8         postcript_save_restore_op);

extern Bool
pclxl_set_clip_to_page(PCLXL_GRAPHICS_STATE graphics_state);

extern Bool
pclxl_release_resources(PCLXL_CONTEXT pclxl_context,
                        Bool          include_per_page_resources);

#endif /* __PCLXLOPERATORS_H__ */

/******************************************************************************
* Log stripped */
