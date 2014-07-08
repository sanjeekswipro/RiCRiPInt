/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:cursorpos.c(EBDSDK_P.1) $
 * $Id: src:cursorpos.c,v 1.60.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Cursor Positioning" category.
 *
 * Horizontal Cursor Positioning
 *
 * Columns                     ESC & a # C
 * Decipoints                  ESC & a # H
 * PCL Units                   ESC * p # X
 *
 * Control Codes
 *
 * Carriage Return             CR
 * Space                       SP
 * Backspace                   BS
 * Horizontal Tab              HT
 *
 * Vertical Cursor Positioning
 *
 * Rows                        ESC & a # R
 * Decipoints                  ESC & a # V
 * PCL Units                   ESC * p # Y
 * Half Line-Feed              ESC =
 *
 * Control Codes
 *
 * Line-Feed                   LF
 * Form-Feed                   FF
 * Line Termination            ESC & k # G
 * Push/Pop Cursor Position    ESC & f # S
 */

#include "core.h"
#include "cursorpos.h"
#include "pcl5fonts.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "jobcontrol.h"
#include "pagecontrol.h"
#include "misc.h"

#include "fileio.h"
#include "graphics.h"
#include "gstate.h"
#include "gu_cons.h"
#include "monitor.h"


/* Has the cursor already been explicitly set ? */
Bool cursor_explicitly_set(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  return p_state->cursor_explicitly_set ;
}


void mark_cursor_explicitly_set(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  p_state->cursor_explicitly_set = TRUE ;
}


CursorPosition* get_cursor(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  cursor = &print_state->cursor ;

  return cursor ;
}


/* Move cursor y value to within the logical page bounds */
void move_cursor_y_to_logical_page(PCL5Context *pcl5_ctxt,
                                   CursorPosition *cursor)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  if (cursor->y < - (PCL5Real) page_info->top_margin)
    cursor->y = - (PCL5Real) page_info->top_margin ;

  else if (cursor->y > page_info->max_text_length)
    cursor->y = page_info->max_text_length ;

  underline_callback(pcl5_ctxt, FALSE) ;
}


/* Move cursor x value to within the logical page bounds */
void move_cursor_x_to_logical_page(PCL5Context *pcl5_ctxt,
                                   CursorPosition *cursor)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  if (cursor->x < 0)
    cursor->x = 0 ;

  else if (cursor->x > page_info->page_width)
    cursor->x = page_info->page_width ;

  underline_callback(pcl5_ctxt, FALSE) ;
}


/* Check whether cursor x or y values are outside the logical page,
 * as can happen e.g. to values popped off the cursor position stack
 * if the page orientation has changed, and if so move them to the
 * logical page bounds.
 */
void move_cursor_to_logical_page(PCL5Context *pcl5_ctxt,
                                 CursorPosition *cursor)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  if (cursor->x < 0)
    cursor->x = 0 ;

  else if (cursor->x > page_info->page_width)
    cursor->x = page_info->page_width ;

  if (cursor->y < - (PCL5Real) page_info->top_margin)
    cursor->y = - (PCL5Real) page_info->top_margin ;

  else if (cursor->y > page_info->max_text_length)
    cursor->y = page_info->max_text_length ;

  underline_callback(pcl5_ctxt, FALSE) ;
}


void get_cursor_position(PCL5Context *pcl5_ctxt, PCL5Real *x_pos, PCL5Real *y_pos)
{
  CursorPosition *cursor ;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  cursor = get_cursor(pcl5_ctxt) ;

  *x_pos = cursor->x ;
  *y_pos = cursor->y ;
}


/* N.B. This will limit the position to the logical page.  It is up to the
 * caller to ask for the final position if required.
 */
void set_cursor_position(PCL5Context *pcl5_ctxt, PCL5Real x_pos, PCL5Real y_pos)
{
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  cursor = get_cursor(pcl5_ctxt) ;

  cursor->x = x_pos ;
  cursor->y = y_pos ;

  move_cursor_to_logical_page(pcl5_ctxt, cursor) ;
}


/* N.B. This will limit the x position to the logical page.  It is up to the
 * caller to ask for the final position if required.
 */
void move_cursor_x_relative(PCL5Context *pcl5_ctxt, PCL5Real x)
{
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

#if defined (DEBUG_BUILD)
  if (x != 0)
    HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;
#endif /* DEBUG_BUILD */

  cursor = get_cursor(pcl5_ctxt) ;
  cursor->x += x ;

  move_cursor_x_to_logical_page(pcl5_ctxt, cursor) ;
}


/* N.B. This will limit the x position to the logical page.  It is up to the
 * caller to ask for the final position if required.
 */
void set_cursor_x_absolute(PCL5Context *pcl5_ctxt, PCL5Real x)
{
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  cursor = get_cursor(pcl5_ctxt) ;
  cursor->x = x ;

  move_cursor_x_to_logical_page(pcl5_ctxt, cursor) ;
}


/* N.B. This will limit the y position to the logical page.  It is up to the
 * caller to ask for the final position if required.
 */
void move_cursor_y_relative(PCL5Context *pcl5_ctxt, PCL5Real y)
{
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

#if defined (DEBUG_BUILD)
  if (y != 0)
    HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;
#endif /* DEBUG_BUILD */

  cursor = get_cursor(pcl5_ctxt) ;
  cursor->y += y ;

  move_cursor_y_to_logical_page(pcl5_ctxt, cursor) ;
}


/* N.B. This will limit the y position to the logical page.  It is up to the
 * caller to ask for the final position if required.
 */
void set_cursor_y_absolute(PCL5Context *pcl5_ctxt, PCL5Real y)
{
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  cursor = get_cursor(pcl5_ctxt) ;
  cursor->y = y ;

  move_cursor_y_to_logical_page(pcl5_ctxt, cursor) ;
}


/* N.B. It is up to the caller to keep the cursor within the text area,
 *      i.e. to the left of the right margin, if required.
 *
 *      This will limit the x position to the logical page.  It is up
 *      to the caller to ask for the final position if required.
 */
Bool move_cursor_column_right(PCL5Context *pcl5_ctxt, int32 num_columns)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Ensure HMI is valid */
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
      return FALSE ;
  }

  move_cursor_x_relative(pcl5_ctxt, page_info->hmi * num_columns) ;

  return TRUE ;
}


/* This figures out whether a character of the width provided will fit before
 * the right margin.  It works regardless of where the cursor is currently
 * located relative to the right margin.
 * If the width provided is -1, it will use the HMI instead.
 */
Bool char_fits_before_right_margin(PCL5Context *pcl5_ctxt, int32 char_width)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (char_width == -1) {
    /* Ensure HMI is valid */
    if (page_info->hmi == INVALID_HMI) {
      if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
        return FALSE ;
    }
    char_width = page_info->hmi ;
  }

  if (x + char_width > page_info->right_margin)
    return FALSE ;

  return TRUE ;
}


/* This figures out whether a character of the width provided will fit
 * before the right edge of the logical page.
 * If the width provided is -1, it will use the HMI instead.
 */
Bool char_fits_before_right_page_edge(PCL5Context *pcl5_ctxt, int32 char_width)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (char_width == -1) {
    /* Ensure HMI is valid */
    if (page_info->hmi == INVALID_HMI) {
      if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
        return FALSE ;
    }
    char_width = page_info->hmi ;
  }

  /* N.B. Assume by analogy with char_fits_before_right_margin that on the
   *      right edge of the logical page counts as fitting.
   */
  if (x + char_width > page_info->page_width)
    return FALSE ;

  return TRUE ;
}


/* Bool indicates whether the cursor is between (or on) the left
 * and right margins.
 */
Bool cursor_is_between_vertical_margins(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;
  Bool inside = FALSE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (x >= page_info->left_margin &&
      x <= page_info->right_margin)
    inside = TRUE ;

  return inside ;
}


/* Bool indicates whether the cursor is on or beyond the right
 * margin.
 */
Bool cursor_is_on_or_beyond_right_margin(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;
  Bool beyond = FALSE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (x >= page_info->right_margin)
    beyond = TRUE ;

  return beyond ;
}


/* This function is needed because when clipping as opposed to wrapping is the
 * selected mode, the cursor being ON the right margin causes character
 * clipping whereas the character would be printed if the cursor were either
 * BEFORE or BEYOND the margin.
 */

/** \todo If we don't end up using all integer cursor movements we may need to
 * check here for values within half a PCL internal unit to count as ON the
 * margin.
 */
/* Is the cursor BEFORE, ON or BEYOND the right margin? */
int32 cursor_position_relative_to_right_margin(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;
  int32 rel_position = BEFORE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (x == page_info->right_margin)
    rel_position = ON ;
  else if (x > page_info->right_margin)
    rel_position = BEYOND ;

  return rel_position ;
}


/* Returns TRUE if the cursor is at the right hand edge
 * of the logical page.
 */
Bool cursor_is_at_right_page_edge(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;
  Bool at_edge = FALSE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  get_cursor_position(pcl5_ctxt, &x, &y) ;
  HQASSERT(x <= page_info->page_width, "Cursor is off page") ;

  if (x == page_info->page_width)
    at_edge = TRUE ;

  return at_edge ;
}


void move_cursor_to_right_margin(PCL5Context *pcl5_ctxt)
{
  CursorPosition *cursor ;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  cursor = get_cursor(pcl5_ctxt) ;

  cursor->x = page_info->right_margin ;
  underline_callback(pcl5_ctxt, FALSE) ;
}


void move_cursor_to_left_margin(PCL5Context *pcl5_ctxt)
{
  CursorPosition *cursor ;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  cursor = get_cursor(pcl5_ctxt) ;
  cursor->x = page_info->left_margin ;

  underline_callback(pcl5_ctxt, TRUE) ;
}


void set_default_cursor_x_position(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Default cursor position is at left margin, and 0.75 * VMI below top
   * margin.  It appears this is the case even if this is below the
   * bottom margin.
   */

  /** \todo N.B. It appears that if this is below the page, it is allowed to be
   * below the page, i.e. it is not limited to the logical page.  Certain
   * relative cursor movements from here, e.g. by row or column are also not
   * limited to the logical page.  Needs more investigation.
   */

  set_cursor_x_absolute(pcl5_ctxt, page_info->left_margin) ;
  underline_callback(pcl5_ctxt, FALSE) ;
}


void set_default_cursor_y_position(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Default cursor position is at left margin, and 0.75 * VMI below top
   * margin.  It appears this is the case even if this is below the
   * bottom margin.
   */

  /** \todo N.B. It appears that if this is below the page, it is allowed to be
   * below the page, i.e. it is not limited to the logical page.  Certain
   * relative cursor movements from here, e.g. by row or column are also not
   * limited to the logical page.  Needs more investigation.
   */

  set_cursor_y_absolute(pcl5_ctxt, (0.75f * page_info->vmi)) ;
  underline_callback(pcl5_ctxt, FALSE) ;
}


void set_default_cursor_position(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  set_default_cursor_x_position(pcl5_ctxt) ;
  set_default_cursor_y_position(pcl5_ctxt) ;
}


/* N.B. It is up to the caller to keep the cursor within the text area,
 *      i.e. above the bottom margin, if required.
 *
 *      This will limit the x position to the logical page.  It is up
 *      to the caller to ask for the final position if required.
 */
void move_cursor_one_line_down(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  move_cursor_y_relative(pcl5_ctxt, page_info->vmi) ;
}


/* N.B. It is up to the caller to keep the cursor within the text area,
 *      i.e. above the bottom margin, if required.
 *
 *      This will limit the x position to the logical page.  It is up
 *      to the caller to ask for the final position if required.
 */
void move_cursor_half_line_down(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  move_cursor_y_relative(pcl5_ctxt, page_info->vmi / 2.0f) ;
}


Bool set_ps_cursor_position(PCL5Context *pcl5_ctxt)
{
  CursorPosition *cursor ;
  SYSTEMVALUE pos[2] ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  cursor = get_cursor(pcl5_ctxt) ;

#if defined(DEBUG_BUILD)
  {
    PageCtrlInfo *page_info ;
    TextInfo *text_info ;
    page_info = get_page_ctrl_info(pcl5_ctxt) ;
    HQASSERT(page_info != NULL, "page_info is NULL") ;
    text_info = get_text_info(pcl5_ctxt) ;
    HQASSERT(text_info != NULL, "text_info is NULL") ;

    /* N.B. If we are doing an overstrike character, the cursor can be just
     *      to the left of the logical page.  (Actually only if it will be
     *      centred, but we can't check for that conveniently here).
     */
    HQASSERT((cursor->x >= 0 || text_info->overstrike_pending) &&
             cursor->x <= page_info->page_width &&
             cursor->y >= - (PCL5Real) page_info->top_margin &&
             cursor->y <= page_info->max_text_length,
             "Cursor is outside logical page") ;
  }
#endif /* DEBUG_BUILD */

  pos[0] = (SYSTEMVALUE) cursor->x;
  pos[1] = (SYSTEMVALUE) cursor->y;

  if (! gs_moveto(TRUE, pos, &thePathInfo( *gstateptr )))
    return FALSE;

  return TRUE ;
}

/* ============================================================================
 * Cursor coordinate transformation functions
 * ============================================================================
 */

/* Update the given cursor coordinates to keep the same physical position
 * given the new CTM and the inverse of the original CTM.
 *
 * N.B. This does not move the cursor to the logical page, since this
 * may not be required following an orientation change and should not
 * be necessary for a print direction change.
 */
void transform_cursor(CursorPosition *cursor,
                      OMATRIX *orig_ctm,
                      OMATRIX *new_ctm)
{
  PCL5Real new_x, new_y ;
  OMATRIX matrix, inverse_new_ctm ;

  HQASSERT(cursor != NULL, "cursor is NULL" ) ;
  HQASSERT(orig_ctm != NULL, "orig_ctm is NULL" ) ;
  HQASSERT(new_ctm != NULL, "new_ctm is NULL" ) ;

  matrix_inverse(new_ctm, &inverse_new_ctm);
  matrix_mult(orig_ctm, &inverse_new_ctm, &matrix) ;
  MATRIX_TRANSFORM_XY((cursor->x), (cursor->y), new_x, new_y, &matrix) ;

  cursor->x = new_x ;
  cursor->y = new_y ;
}


/* Update the current cursor coordinates to keep the same physical position
 * given the new CTM and the inverse of the original CTM.
 *
 * N.B. This moves the cursor to the logical page, which should generally
 * be right for the current cursor.  If this is not desired, use
 * transform_cursor instead.
 */
void transform_current_cursor(PCL5Context *pcl5_ctxt,
                              OMATRIX *orig_ctm,
                              OMATRIX *new_ctm)
{
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  HQASSERT(orig_ctm != NULL, "orig_ctm is NULL" ) ;
  HQASSERT(new_ctm != NULL, "new_ctm is NULL" ) ;

  /* N.B. This uses transform_cursor rather than e.g. set_cursor_position
   * to avoid the cursor_has_moved_callback.
   */
  cursor = get_cursor(pcl5_ctxt) ;
  transform_cursor(cursor, orig_ctm, new_ctm) ;
  move_cursor_to_logical_page(pcl5_ctxt, cursor) ;
}

/* ============================================================================
 * Cursor Position Stack manipulation functions, (including coordinate
 * transformation function(s)).
 * ============================================================================
 */

 void push_cursor_position(PCL5Context *pcl5_ctxt)
{
  struct PCL5PrintState *p_state;
  CursorPositionStack *p_stack;
  CursorPosition *p_cursor;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");
  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "state pointer is NULL");

  p_stack = &p_state->cursor_stack;
  p_cursor = &p_state->cursor;

  if (p_stack->depth < MAX_CURSOR_STACK_DEPTH)
  {
    p_stack->stack[p_stack->depth] = *p_cursor;
    p_stack->depth++;
  }
}


void pop_cursor_position(PCL5Context *pcl5_ctxt)
{
  struct PCL5PrintState *p_state;
  CursorPositionStack *p_stack;
  CursorPosition *p_cursor;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");
  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "PCL5PrintState is NULL");

  p_stack = &p_state->cursor_stack;
  p_cursor = &p_state->cursor;

  if (p_stack->depth >= 1)
  {
    *p_cursor = p_stack->stack[p_stack->depth - 1];
    p_stack->depth--;

    /* If the cursor position is outside the logical page move it in */
    move_cursor_to_logical_page(pcl5_ctxt, p_cursor);
  }
}


void clear_cursor_position_stack(PCL5Context *pcl5_ctxt)
{
  struct PCL5PrintState *p_state;
  CursorPositionStack *p_stack;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");
  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "PCL5PrintState is NULL");

  p_stack = &p_state->cursor_stack;

  p_stack->depth = 0;
}


/* Update cursor stack to keep same physical positions.
 * The coordinates are stored in terms of the current printdirection,
 * meaning they should only need to be transformed for printdirection
 * changes or for changes affecting the top margin position.
 *
 * N.B. This does not move them to the logical page.
 */
/** \todo Think about registration change and moving to logical page. */
void transform_cursor_stack(PCL5Context *pcl5_ctxt,
                            OMATRIX *orig_ctm,
                            OMATRIX *new_ctm)
{
  PCL5PrintState *print_state ;
  CursorPositionStack *cursor_stack ;
  uint32 i ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  HQASSERT(orig_ctm != NULL, "orig_ctm is NULL" ) ;
  HQASSERT(new_ctm != NULL, "new_ctm is NULL" ) ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL" ) ;

  cursor_stack = &(print_state->cursor_stack) ;

  for (i = 0; i < cursor_stack->depth; i++) {
    transform_cursor(&(cursor_stack->stack[i]), orig_ctm, new_ctm);
  }
}

/* ==========================================================================*/
/* Update all cursors in PCL5PrintState to keep the same physical positions
 * when the CTM changes, e.g. due to printdirection or top margin change.
 *
 * N.B. This also moves the current cursor to the logical page.
 */
/** \todo Review situation regarding moving cursors to the logical page */
void transform_cursors(PCL5Context *pcl5_ctxt,
                       OMATRIX *orig_ctm,
                       OMATRIX *new_ctm)
{
  PCL5PrintState *print_state ;
  PCL5FontState *font_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  HQASSERT(orig_ctm != NULL, "orig_ctm is NULL" ) ;
  HQASSERT(new_ctm != NULL, "new_ctm is NULL" ) ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL" ) ;
  font_state = &(print_state->font_state) ;

  /* N.B. Since transforming the current cursor may (currently) result in the
   *      underline callback being called, (due to moving the cursor to the
   *      logical page), it is safest to transform the underline positions
   *      first, to ensure the underline callback is not called with the
   *      relevant state only partly set up.
   */
  transform_cursor(&(font_state->start_underline_position), orig_ctm, new_ctm) ;
  transform_cursor(&(font_state->end_underline_position), orig_ctm, new_ctm) ;
  transform_current_cursor(pcl5_ctxt, orig_ctm, new_ctm) ;
  transform_cursor_stack(pcl5_ctxt, orig_ctm, new_ctm) ;
}

/* ==========================================================================*/

/* Issue a CR without doing line termination command processing. */
Bool pcl5op_CR_raw(PCL5Context *pcl5_ctxt)
{
  mark_cursor_explicitly_set(pcl5_ctxt) ;
  move_cursor_to_left_margin(pcl5_ctxt) ;
  return TRUE ;
}

/* Issue a LF without doing line termination command processing. */
Bool pcl5op_LF_raw(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y, excess ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;
  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (page_info->perforation_skip) {

    /* N.B. This holds even if the cursor is already below the
     * bottom margin, (or indeed above the top margin).
     */
    if (y + page_info->vmi > page_info->text_length) {
      /* This will maintain the current horizontal cursor position */
      return throw_page(pcl5_ctxt, TRUE, TRUE) ;
    }
  }
  else if (y + page_info->vmi > page_info->max_text_length) {
    excess = (y + page_info->vmi) - page_info->max_text_length ;

    /* This will maintain the current horizontal cursor position */
    throw_page(pcl5_ctxt, TRUE, TRUE) ;

    /* Set the cursor to the 'excess' down from the top of the page.
     * N.B. This does not appear to be documented.
     */
    set_cursor_position(pcl5_ctxt, x, excess - page_info->top_margin) ;
    return TRUE ;
  }

  move_cursor_one_line_down(pcl5_ctxt) ;

  return TRUE ;
}

/* ==========================================================================*/
/* Positions the cursor ready for the next text character when line wrap
 * is in operation.
 * N.B. This can result in a page throw and hence a PS restore.
 */
void position_cursor_for_next_character(PCL5Context *pcl5_ctxt, int32 char_width)
{
#if defined(DEBUG_BUILD)
  {
    TroubleShootingInfo *trouble_info ;

    trouble_info = get_trouble_shooting_info(pcl5_ctxt) ;
    HQASSERT(trouble_info != NULL, "trouble_info is Null") ;
    HQASSERT(trouble_info->line_wrap, "Expected line wrap") ;
  }
#endif /* DEBUG_BUILD */

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(char_width >= 0 || char_width == -1,
           "Unexpected char_width") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  if ((cursor_is_between_vertical_margins(pcl5_ctxt) &&
       ! char_fits_before_right_margin(pcl5_ctxt, char_width)) ||
      (cursor_is_on_or_beyond_right_margin(pcl5_ctxt) &&
       ! char_fits_before_right_page_edge(pcl5_ctxt, char_width))) {

    /* Do a CR-LF before printing the next character */
    pcl5op_CR_raw(pcl5_ctxt) ;
    pcl5op_LF_raw(pcl5_ctxt) ;

    /** \todo What if the character still doesn't fit before the right margin
     *  i.e. it is wider than the text area.  Should it be printed?
     *  Should the cursor be moved to the right margin?
     */
  }
}

/* Positions the cursor ready to print an overstrike character, (following one
 * or more backspaces).  The overstrike character is supposed to be centred on
 * the same place as a character of the last printed character width would be.
 */
void position_cursor_to_centre_overstrike(PCL5Context *pcl5_ctxt, int32 char_width)
{
  TextInfo *text_info ;
  PCL5Real x_dist ;
  CursorPosition *cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(char_width >= 0, "Unexpected char_width") ;
  HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;

  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is Null") ;
  HQASSERT(text_info->last_char_width != -1,
           "Invalid value for last_char_width") ;

  x_dist = (text_info->last_char_width - char_width) / 2 ;

  /* N.B. We cannot use move_cursor_x_relative here, as that restricts the
   *      cursor position to the logical page, whereas this can go to the
   *      left of the left edge of the logical page.  (The circumstances in
   *      which this is called should preclude a value over the right edge
   *      of the logical page, (see pcl5fonts.c)).
   */
  cursor = get_cursor(pcl5_ctxt) ;
  cursor->x += x_dist ;
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/** \todo It is possible that all PCL cursor positions should be integer (in
 * terms of PCL internal units), and therefore so should all PCL cursor moves,
 * (including moves whose size is given by a character size).  The fact that
 * e.g. clipping behaviour is different depending on whether the cursor is
 * BEFORE, ON, or BEYOND the right margin, together with integer margin
 * positions, and HMI and VMI tends to suggest this.  It has however not been
 * tested.
 *
 * If it is the case then all other PCL cursor movements may need to snap to a
 * grid, including e.g. when exiting HPGL2.
 *
 * Apart from the 6 cursor movement commands below, this would also affect
 * any commands here which use a fraction of the VMI, i.e. half line feed,
 * and anything resulting in a default cursor position being set.
 */


/* Horizontal Positioning (Columns) */
/* N.B. It has been established that the value is real, and is being limited to
 *      something like the range assumed below.
 */
Bool pcl5op_ampersand_a_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_sign != NOSIGN || value.real >= 0, "Unexpected value") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_round_to_4d(value.real) ;

  /* Ensure HMI is valid */
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
      return FALSE ;
  }

  /* Convert to PCL Internal Units */
  if (explicit_sign == NOSIGN) {
    set_cursor_x_absolute(pcl5_ctxt, page_info->hmi * value.real ) ;
  } else {
    move_cursor_x_relative(pcl5_ctxt, page_info->hmi * value.real ) ;

    /* Underline is always drawn when there is a positive movement so
       we best draw what we have so far when we move in the negative
       direction. */
    if (value.real < 0) {
      underline_callback(pcl5_ctxt, TRUE) ;
    }
  }

  return TRUE ;
}


/* Horizontal Positioning (Decipoints) */
/* N.B. It has been established that the command is not ignored for large
 *      positive or negative values.  Assume by analogy with other cursor
 *      positioning commands that it is limited to the range -32767 to
 *      32767, (but this has not been tested).
 */
Bool pcl5op_ampersand_a_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_sign != NOSIGN || value.real >= 0, "Unexpected value") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_round_to_2d(value.real) ;

  /* Convert to PCL Internal Units */
  if (explicit_sign == NOSIGN) {
    set_cursor_x_absolute(pcl5_ctxt, 10 * value.real) ;
  } else {
    move_cursor_x_relative(pcl5_ctxt, 10 * value.real) ;

    /* Underline is always drawn when there is a positive movement so
       we best draw what we have so far when we move in the negative
       direction. */
    if (value.real < 0) {
      underline_callback(pcl5_ctxt, TRUE) ;
    }
  }

  return TRUE ;
}


/* Horizontal Positioning (PCL Units) */
/* N.B. It has been established that the value is integer, and is being limited to
 *      something like the range assumed below.
 */
Bool pcl5op_star_p_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 pcl_units ;
  PageCtrlInfo* page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_sign != NOSIGN || value.integer >= 0, "Unexpected value") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  /* Limit values to -32767 to 32767 */
  pcl_units = (int32) pcl5_limit_to_range(value.integer, -32767, 32767) ;

  /* Convert to PCL Internal Units */
  if (explicit_sign == NOSIGN) {
    set_cursor_x_absolute(pcl5_ctxt, pcl_unit_to_internal(page_info, pcl_units)) ;
  } else {
    move_cursor_x_relative(pcl5_ctxt, pcl_unit_to_internal(page_info, pcl_units)) ;

    /* Underline is always drawn when there is a positive movement so
       we best draw what we have so far when we move in the negative
       direction. */
    if (value.integer < 0) {
      underline_callback(pcl5_ctxt, TRUE) ;
    }
  }

  return TRUE ;
}


/*  Carriage Return
 *  See the function pcl5op_ampersand_k_G() in this file for
 *  documentation on how the line termination command is
 *  interpreted.
 */
Bool pcl5op_control_CR(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  switch (page_info->line_termination) {
  case 0:
  case 2:
    if (! pcl5op_CR_raw(pcl5_ctxt))
      return FALSE ;
    break ;
  case 1:
  case 3:
    if (! pcl5op_CR_raw(pcl5_ctxt))
      return FALSE ;
    if (! pcl5op_LF_raw(pcl5_ctxt))
      return FALSE ;
    break ;
  default:
    HQFAIL("Line termination command has become corrupt!") ;
    break ;
  }
  return TRUE ;
}


/* Backspace */
Bool pcl5op_control_BS(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;
  FontInfo *font_info ;
  TextInfo *text_info ;
  PageCtrlInfo *page_info ;
  PCL5Real distance ;
  PCL5Real x, y ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;
  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  if (! underline_callback(pcl5_ctxt, TRUE))
    return FALSE ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;
  get_cursor_position(pcl5_ctxt, &x, &y) ;

  /* Ensure the HMI is valid */
  /* N.B.   This also ensures we have a valid active font. */
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
      return FALSE ;
  }

  /* A BS in a proportional font sets the overstrike_pending flag, which
   * helps determine e.g. whether to centre the next character, (the
   * overstrike character), and how much to move the cursor afterwards.
   * A BS in a fixed space font resets the overstrike_pending flag.
   *
   * Also, the distance we expect to backspace is different for fixed and
   * proportional fonts.
   */
  if (is_fixed_spacing(get_font(font_info, font_info->active_font))) {
    text_info->overstrike_pending = FALSE ; 
    distance = page_info->hmi ;
  }
  else {
    text_info->overstrike_pending = TRUE ;
    distance = text_info->last_char_width ;
  }

  HQASSERT(distance >= 0, "negative distance");

  /* If the BS would result in the left margin being crossed, just go back to
   * the left margin.  If the cursor is already left of the left margin, BS
   * appears to operate normally.  The following will automatically keep it on
   * the logical page, and this has been assumed to be correct.
   *
   * The PCL5 Tech ref says that if the cursor is beyond the right margin, it
   * should be moved 'just to the left of' the right margin, (presumably to
   * allow the next character to be printed rather than clipped).  However,
   * experience with the printer seems to suggest that no special right margin
   * handling is needed here, regardless of whether the backspace starts from
   * the right margin, crosses the right margin, or takes place entirely beyond
   * the right margin.
   */
  if ((x >= page_info->left_margin) &&
      (x - distance <= page_info->left_margin))
    move_cursor_to_left_margin(pcl5_ctxt) ;
  else
    move_cursor_x_relative(pcl5_ctxt, - distance) ;

  /* Although the underline details have been reset (because of the
     draw underline), we need to change the underline start position
     to *after* the BS has been executed. Yuk! */
  {
    PCL5Real x_pos, y_pos ;
    get_cursor_position(pcl5_ctxt, &x_pos, &y_pos) ;

    p_state->font_state.start_underline_position.x = x_pos ;
    p_state->font_state.start_underline_position.y = y_pos ;
    p_state->font_state.end_underline_position.x = x_pos ;
    p_state->font_state.end_underline_position.y = y_pos ;
  }

  return TRUE ;
}


/* Horizontal Tab
 * Move the cursor to the next tab stop on the current line.
 * Tab stops are at the left margin and every 8th column between there
 * and the right edge of the logical page.  If the new position
 * crosses the right margin it is set to the right margin.
 */
Bool pcl5op_control_HT(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y, new_x, stop_width ;
  PCL5Integer stop = 0 ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  if (page_info->hmi == 0)
    return TRUE ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;
  get_cursor_position(pcl5_ctxt, &x, &y) ;

  /* The first tab stop is at the left margin */
  if (x < page_info->left_margin)
    move_cursor_to_left_margin(pcl5_ctxt) ;
  else {

    /* Ensure HMI is valid */
    if (page_info->hmi == INVALID_HMI) {
      if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
        return FALSE ;
    }

    /* Find the stop before the cursor and move to the next one */
    stop_width = 8 * page_info->hmi ;
    stop = (PCL5Integer) ((x - page_info->left_margin) / stop_width) ;

    new_x = page_info->left_margin + (stop_width * (stop + 1)) ;

    /* If the tab would move the cursor across the right margin,
     * (where starting on the right margin counts as in the text
     * area), set the cursor position to the right margin.
     */
    if ((x <= page_info->right_margin) && (new_x > page_info->right_margin))
      move_cursor_to_right_margin(pcl5_ctxt) ;
    else
      set_cursor_x_absolute(pcl5_ctxt, new_x) ;
  }

  return TRUE ;
}


/* Vertical Positioning (Rows) */
/* N.B. Assume by analogy with other cursor positioning commands that this
 *      is limited to the range stated in the Tech Ref, but this has not been
 *      tested.
 */
Bool pcl5op_ampersand_a_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_sign != NOSIGN || value.real >= 0, "Unexpected value") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  /* Limit values to -32767 to 32767 */
  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_round_to_4d(value.real) ;

  /* Convert to PCL Internal Units */
  if (explicit_sign == NOSIGN)
    /* N.B. Zero appears to mean the first print line, i.e. (0.75 * VMI) */
    set_cursor_y_absolute(pcl5_ctxt, page_info->vmi * (value.real + 0.75f)) ;
  else
    move_cursor_y_relative(pcl5_ctxt, page_info->vmi * value.real ) ;

  return TRUE ;
}


/* Vertical Positioning (Decipoints) */
/* N.B. Assume by analogy with other cursor positioning commands that this
 *      is limited to the range stated in the Tech Ref, but this has not
 *      been tested.
 */
Bool pcl5op_ampersand_a_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_sign != NOSIGN || value.real >= 0, "Unexpected value") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  /* Limit values to -32767 to 32767 */
  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_round_to_1d(value.real) ;

  /* Convert to PCL Internal Units */
  if (explicit_sign == NOSIGN)
    set_cursor_y_absolute(pcl5_ctxt, 10 * value.real) ;
  else
    move_cursor_y_relative(pcl5_ctxt, 10 * value.real) ;

  return TRUE ;
}


/* Vertical Postioning (PCL Units) */
/* N.B. It has been established that the value is integer, and is being limited to
 *      something like the range stated in the Tech Ref.
 */
Bool pcl5op_star_p_Y(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 pcl_units ;
  PageCtrlInfo* page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_sign != NOSIGN || value.integer >= 0, "Unexpected value") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  /* Limit values to -32767 to 32767 */
  pcl_units = (int32) pcl5_limit_to_range(value.integer, -32767, 32767) ;

  /* Convert to PCL Internal Units */
  if (explicit_sign == NOSIGN)
    set_cursor_y_absolute(pcl5_ctxt, pcl_unit_to_internal(page_info, pcl_units)) ;
  else
    move_cursor_y_relative(pcl5_ctxt, pcl_unit_to_internal(page_info, pcl_units)) ;

  return TRUE ;
}


/* ESC-= */
/* Ignore explicit_sign and value as they will NOT be set for two
 * character escape sequences.
 */
Bool pcl5op_equals(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y, excess ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;
  get_cursor_position(pcl5_ctxt, &x, &y) ;

  if (page_info->perforation_skip) {

    /* N.B. This holds even if the cursor is already below the bottom margin.
     * Assume by analogy with line feed that it also holds if the cursor
     * starts above the top margin.
     */
    if (y + (page_info->vmi / 2.0f) > page_info->text_length) {
      /* This will maintain the current horizontal cursor position */
      return throw_page(pcl5_ctxt, TRUE, TRUE) ;
    }
  }
  else if (y + (page_info->vmi / 2.0f) > page_info->max_text_length) {
    excess = (y + (page_info->vmi / 2.0f)) - page_info->max_text_length ;

    /* This will maintain the current horizontal cursor position */
    throw_page(pcl5_ctxt, TRUE, TRUE) ;

    /* Set the cursor to the 'excess' down from the top of the page.
     * N.B. This does not appear to be documented.
     */
    set_cursor_position(pcl5_ctxt, x, excess - page_info->top_margin) ;
    return TRUE ;
  }

  move_cursor_half_line_down(pcl5_ctxt) ;

  return TRUE ;
}


/* Line Feed
 * See the function pcl5op_ampersand_k_G() in this file for
 * documentation on how the line termination command is
 * interpreted.
 */
Bool pcl5op_control_LF(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  switch (page_info->line_termination) {
  case 2:
  case 3:
    if (! pcl5op_CR_raw(pcl5_ctxt))
      return FALSE ;
    /* dropthrough */
  case 0:
  case 1:
    if (! pcl5op_LF_raw(pcl5_ctxt))
      return FALSE ;
    break ;
  default:
    HQFAIL("Line termination command has become corrupt!") ;
    break ;
  }
  return TRUE ;
}


/* Form Feed
 * See the function pcl5op_ampersand_k_G() in this file for
 * documentation on how the line termination command is
 * interpreted.
 */
Bool pcl5op_control_FF(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  /* I'm going to assume that if FF is to be interpreted as CR FF, the
     horizontal position is not kept on the next page but rather reset
     to the left margin position. */

  switch (page_info->line_termination) {
  case 2:
  case 3:
    if (! pcl5op_CR_raw(pcl5_ctxt))
      return FALSE ;
    /* dropthrough */
  case 0:
  case 1:
    /* This will maintain the current horizontal cursor position */
    if (! throw_page(pcl5_ctxt, TRUE, TRUE))
      return FALSE ;
    break ;
  default:
    HQFAIL("Line termination command has become corrupt!") ;
    break ;
  }
  return TRUE ;
}


/* Line termination command. ESC & k # G

   # =
   0 - CR=CR; LF=LF; FF=FF
   1 - CR=CR-LF; LF=LF; FF=FF
   2 - CR=CR; LF=CR-LF; FF=CR-FF
   3 - CR=CR-LF; LF=CR-LF; FF=CR-FF
   Default = 0, Range = 0-3 */
Bool pcl5op_ampersand_k_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;
  uint32 termination_command = (uint32)value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  switch (termination_command) {
  case 0:
  case 1:
  case 2:
  case 3:
    page_info->line_termination = termination_command ;
    break ;
  default:
    /* Ignore invalid values. */
    break ;
  }

  return TRUE ;
}


/* Push or Pop Cursor */
Bool pcl5op_ampersand_f_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (value.integer != 0 && value.integer != 1)
    return TRUE ;

  mark_cursor_explicitly_set(pcl5_ctxt) ;

  if (value.integer == 0)
    push_cursor_position(pcl5_ctxt) ;
  else
    pop_cursor_position(pcl5_ctxt) ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
