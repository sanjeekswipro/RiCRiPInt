/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:cursorpos.h(EBDSDK_P.1) $
 * $Id: src:cursorpos.h,v 1.35.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __CURSORPOS_H__
#define __CURSORPOS_H__

#include "pcl5context.h"
#include "matrix.h"

#define GL2_PLOTTER_UNITS_TO_INTERNAL ((HPGL2Real)7200.0 / (HPGL2Real)1016.0)
#define INTERNAL_UNIT_TO_PLOTTER_UNIT ((HPGL2Real)1016 / (HPGL2Real)7200)

#define MAX_CURSOR_STACK_DEPTH 20

#define HALF_INCH 3600 /* PCL Internal Units */

typedef struct CursorPosition {
  PCL5Real x;
  PCL5Real y;
} CursorPosition;

typedef struct CursorPositionStack {
  uint32         depth;
  CursorPosition stack[MAX_CURSOR_STACK_DEPTH];
} CursorPositionStack;


/* Where is the cursor relative to the right margin? */
enum {
  NOT_SET = -1,
  BEFORE = 0,
  ON = 1,
  BEYOND = 2
};


Bool cursor_explicitly_set(PCL5Context *pcl5_ctxt) ;

void mark_cursor_explicitly_set(PCL5Context *pcl5_ctxt) ;

void get_cursor_position(PCL5Context *pcl5_ctxt, PCL5Real *x_pos, PCL5Real *y_pos) ;

void set_cursor_position(PCL5Context *pcl5_ctxt, PCL5Real x_pos, PCL5Real y_pos);

void set_cursor_x_absolute(PCL5Context *pcl5_ctxt, PCL5Real x) ;

void move_cursor_x_relative(PCL5Context *pcl5_ctxt, PCL5Real x) ;

void move_cursor_y_relative(PCL5Context *pcl5_ctxt, PCL5Real y) ;

Bool char_fits_before_right_margin(PCL5Context *pcl5_ctxt, int32 char_width) ;

Bool cursor_is_between_vertical_margins(PCL5Context *pcl5_ctxt) ;

/* Is the cursor BEFORE, ON or BEYOND the right margin? */
int32 cursor_position_relative_to_right_margin(PCL5Context *pcl5_ctxt) ;

Bool cursor_is_at_right_page_edge(PCL5Context *pcl5_ctxt) ;

void move_cursor_to_right_margin(PCL5Context *pcl5_ctxt) ;

void move_cursor_to_left_margin(PCL5Context *pcl5_ctxt) ;

void move_cursor_to_logical_page(PCL5Context *pcl5_ctxt, CursorPosition *cursor) ;

void set_default_cursor_x_position(PCL5Context *pcl5_ctxt) ;

void set_default_cursor_y_position(PCL5Context *pcl5_ctxt) ;

void set_default_cursor_position(PCL5Context *pcl5_ctxt) ;

Bool move_cursor_column_right(PCL5Context *pcl5_ctxt, int32 num_columns) ;

Bool set_ps_cursor_position(PCL5Context *pcl5_ctxt) ;

void clear_cursor_position_stack(PCL5Context *pcl5_ctxt) ;

CursorPosition* get_cursor(PCL5Context *pcl5_ctxt);

void transform_cursor(CursorPosition *cursor,
                      OMATRIX *orig_ctm,
                      OMATRIX *new_ctm) ;

void transform_current_cursor(PCL5Context *pcl5_ctxt,
                              OMATRIX *orig_ctm,
                              OMATRIX *new_ctm) ;

void transform_cursor_stack(PCL5Context *pcl5_ctxt,
                            OMATRIX *orig_ctm,
                            OMATRIX *new_ctm) ;

void transform_cursors(PCL5Context *pcl5_ctxt,
                       OMATRIX *orig_ctm,
                       OMATRIX *new_ctm) ;

Bool pcl5op_CR_raw(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_LF_raw(PCL5Context *pcl5_ctxt) ;

void position_cursor_for_next_character(PCL5Context *pcl5_ctxt, int32 char_width) ;

void position_cursor_to_centre_overstrike(PCL5Context *pcl5_ctxt, int32 char_width) ;

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

Bool pcl5op_ampersand_a_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_a_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_p_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_control_CR(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_control_BS(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_control_HT(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_ampersand_a_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_a_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_p_Y(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_equals(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_control_LF(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_control_FF(PCL5Context *pcl5_ctxt) ;

Bool pcl5op_ampersand_k_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_f_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */

#endif
