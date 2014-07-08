/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2polygon.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Polygon Group" category.
 * (RQ is unsupported)
 *
 *   EA    % Edge Rectangle Absolute
 *   EP    % Edge Polygon
 *   ER    % Edge Rectangle Relative
 *   EW    % Edge Wedge
 *   FP    % Fill Polygon
 *   PM    % Polygon Mode
 *   RA    % Fill Rectangle Absolute
 *   RR    % Fill Rectangle Relative
 *   WG    % Fill Wedge
 */

#include "core.h"
#include "hpgl2scan.h"
#include "hpgl2config.h"
#include "hpgl2vector.h"
#include "hpgl2linefill.h"
#include "hpgl2state.h"
#include "hpgl2polygon.h"
#include "polygon_buffer_impl.h"

#include "pcl5context_private.h"

#include "graphics.h"
#include "swerrors.h"

/* The polygon buffer is a separate path, maintained concurrently
 * with the gstate current path. hpgl2 drawing operators mirror
 * the conventional path construction operators, acting as proxies
 * for either the usual gs_* path construction operators, or as
 * special cases for the polygon buffer. This allows the HPGL
 * drawing code to ignore whether or not it draws into the polygon
 * buffer or draws "directly" to the page.
 * Both direct and polygon mode have specific state to manage.
 * Polygon mode needs to account for defining polygons with pen up.
 * "Direct" mode needs to account for symbol mode etc.
 * The intention is that there is sufficient encapsulation
 * to allow different approaches to polygon buffer implementation
 * to be swapped in if necessary.
 * Polygon mode has its own operators for :
 * lineto, moveto, curveto, closepath, stroke, fill, newpath.
 *
 * The implementation of the polygon buffer contained below
 * ultimately constructs a second PATHINFO for the polygon
 * buffer, very much like gs_* operators (with some modification).
 * This frees the polygon buffer from worrying about the memory
 * management etc, but does tie the implementation to using
 * the temporary pool etc.
 *
 * The polygon buffer itself is part of the print state.
 * The polygon status is part of the print environment.
 *
 */

/* --- polygon buffer management routines --- */

Bool hpgl2_polygon_buffer_destroy(struct HPGL2PolygonBuffer *buff)
{
  return polygon_buffer_impl_state_destroy(buff);
}

struct HPGL2PolygonBuffer* hpgl2_polygon_buffer_create(void)
{
  return polygon_buffer_impl_state_create();
}

struct HPGL2PolygonBuffer *get_hpgl2_polygon_buffer(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState * print_state = get_hpgl2_print_state(pcl5_ctxt);

  return print_state->polygon_buffer;
}

Bool hpgl2_in_polygon_mode(PCL5Context *pcl5_ctxt)
{
  HPGL2PolygonInfo *polygon_info = get_hpgl2_polygon_info(pcl5_ctxt);

  return polygon_info->enabled;
}

Bool hpgl2_set_polygon_mode(PCL5Context *pcl5_ctxt, Bool mode)
{
  HPGL2PolygonInfo *polygon_info = get_hpgl2_polygon_info(pcl5_ctxt);

  polygon_info->enabled = mode;
  return TRUE;
}

/* On entry to polygon mode, set the state appropriately, and add the
 * pen position to the polygon buffer as the first point of the polygon.
 * This should only be called when entering the polygon buffer on from
 * PM0, or its effective equivalent when going rectangles, wedges etc.
 */
static Bool hpgl2_init_polygon_buffer(PCL5Context* pcl5_ctxt,
                                      HPGL2Point *init_point)
{
  struct HPGL2PolygonBuffer *polygon_buffer =
                                    get_hpgl2_polygon_buffer(pcl5_ctxt);
  Bool result = TRUE;
  Bool pen_selected = pen_is_selected(pcl5_ctxt);

  HQASSERT(hpgl2_in_polygon_mode(pcl5_ctxt),
    "Attempt to init polygon buffer but not in polygon mode");

  hpgl2_empty_polygon_buffer(polygon_buffer);

  if ( init_point != NULL )
      result = hpgl2_polygon_plot(polygon_buffer, TRUE,
                                  FALSE, pen_selected, init_point);

  return result;
}

/* Experimentation with the HP4250 indicates that some parts of the HPGL
 * state should be preserved on entry to Polygon mode, and restored on exit.
 * The set of side effects is limited by operators applicable in PM and not
 * all mutable state is preserved across PM.
 * In the HP4250, the pen up/down state is preserved.
 */
Bool hpgl2_enter_polygon_mode(PCL5Context* pcl5_ctxt,
                              HPGL2Point *initial_point)
{
  HPGL2PolygonInfo *polygon_info = get_hpgl2_polygon_info(pcl5_ctxt);
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);

  HQASSERT( ! hpgl2_in_polygon_mode(pcl5_ctxt),
            "Already in polygon mode" );

  if  ( hpgl2_set_polygon_mode(pcl5_ctxt, TRUE)
      && hpgl2_init_polygon_buffer(pcl5_ctxt, initial_point)) {
    polygon_info->saved_pen_state = vector_info->pen_state;
    return TRUE;
  }

  return FALSE;
}

void hpgl2_exit_polygon_mode(PCL5Context* pcl5_ctxt)
{
  HPGL2PolygonInfo *polygon_info = get_hpgl2_polygon_info(pcl5_ctxt);

  HQASSERT( polygon_info->saved_pen_state == HPGL2_PEN_UP
            || polygon_info->saved_pen_state == HPGL2_PEN_DOWN,
            "Invalid pen state" );

  /* Restore such state as is preserved across PM */
  if ( hpgl2_set_polygon_mode(pcl5_ctxt, FALSE) ) {
    hpgl2_set_pen_mode(pcl5_ctxt, polygon_info->saved_pen_state);
  }

}

Bool hpgl2_force_exit_polygon_mode(PCL5Context *pcl5_ctxt)
{
  struct HPGL2PolygonBuffer *polygon_buffer =
                             get_hpgl2_polygon_buffer(pcl5_ctxt);

  (void)hpgl2_empty_polygon_buffer(polygon_buffer);
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);

  return TRUE;
}


/* --- path construction routines --- */

/* Draw a rectangle in the current coord space, using the current point as
 * one corner, and the specified parameter as the opposite corner.
 * Note that because of the operation of the polygon buffer, the path
 * must be recorded so we cannot use rectfill or rectstroke in this.
 * The path may have an explicit close added optionally.
 * The path is drawn in absolute coordinates.
 * the decision as to whether to stroke or fill the path is left for the
 * caller of the path constructor. It is the callers responsibilty to ensure
 * that a valid currentpoint is left, after the caller operates on the path.
 *
 * The drawing is done in plotter units, scaling is done by the caller.
 *
 * Pen up / pen down can affect the drawing, when in the polygon mode.
 *
 * Rectangles are not included in the commands that can be executed in the
 * polygon buffer mode.
 *
 * But, rectangles when drawn only affect the polygon buffer. Rectangles are
 * drawn into the polygon buffer.
 */
static Bool do_rect_path(PCL5Context *pcl5_ctxt, HPGL2Point *p1, Bool closed )
{
  HPGL2Point current_point, plot_point;
  HPGL2VectorInfo *line_info;
  struct HPGL2PolygonBuffer *polygon_buff = NULL;
  Bool result = TRUE;
  Bool absolute;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");
  HQASSERT(hpgl2_in_polygon_mode(pcl5_ctxt),
    "PCL rectangle should only be drawn to the polygon buffer.");

  line_info = get_hpgl2_vector_info(pcl5_ctxt);
  polygon_buff = get_hpgl2_polygon_buffer(pcl5_ctxt);
  absolute = ( line_info->plot_mode == HPGL2_PLOT_ABSOLUTE );

  if ( ! absolute )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);

  if ( ! hpgl2_polygon_current_point(polygon_buff,
                         &current_point.x, &current_point.y) ) {
      HQFAIL("No current point for rectangle path");
      result = FALSE; /* should always be a current point. */
      goto bail_out;
  }

  /* We are in polygon mode, draw rectangle using the
   * HPGL2 state.
   */
  /**
   * \todo We could address the polygon buffer directly here,
   * rectangles only ever drawn into the buffer.
   */
  plot_point = current_point;
  plot_point.x = p1->x;
  result &= hpgl2_lineto(pcl5_ctxt, &plot_point);
  plot_point.y = p1->y;
  result &= hpgl2_lineto(pcl5_ctxt, &plot_point);
  plot_point.x = current_point.x;
  result &= hpgl2_lineto(pcl5_ctxt, &plot_point);

  if ( closed )
    result &= hpgl2_close_polygon_buffer_subpath(pcl5_ctxt);
  else {
    plot_point.y = current_point.y;
    result &= hpgl2_lineto(pcl5_ctxt, &plot_point);
  }

bail_out:
  if ( ! absolute )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE);
  return result;
}

/* See header for doc. */
void default_HPGL2_polygon_info(HPGL2PolygonInfo* self)
{
  self->enabled = FALSE;
  self->saved_pen_state = HPGL2_INVALID_PEN_STATE; /* invalid value */
}

Bool hpgl2_close_polygon_buffer_subpath(PCL5Context *pcl5_ctxt)
{
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  Bool pen_down = ( vector_info->pen_state == HPGL2_PEN_DOWN );

  HQASSERT(vector_info->pen_state == HPGL2_PEN_DOWN
            || vector_info->pen_state == HPGL2_PEN_UP,
      "Invalid pen state");


  return hpgl2_polygon_closepath(get_hpgl2_polygon_buffer(pcl5_ctxt),
                                 pen_down);
}

/* --- HPGL2 operators --- */

Bool hpgl2op_EA(PCL5Context *pcl5_ctxt)
{
  HPGL2Point current_point, corner;
  Bool result = FALSE;
  uint8 terminator;
  HPGL2VectorInfo *vector_info;
  int8 pen_state;
  struct HPGL2PolygonBuffer *buffer = NULL;
  HPGL2LineFillInfo *linefill_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  buffer = get_hpgl2_polygon_buffer(pcl5_ctxt);
  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  pen_state = vector_info->pen_state;

  /* parse corner parameter. */
  if ( hpgl2_scan_point(pcl5_ctxt, &corner) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 )
    return TRUE; /* syntax error */

  /* get current pen position in plotter units; this will be the
     first point in the polygon buffer. */
  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

   /* implicit pen down must be done before entering polygon mode. */
  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);

  if ( ! hpgl2_enter_polygon_mode(pcl5_ctxt, &current_point) )
    goto bail_out;

  /* plot scale scaling now handled by user point transform. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &corner, &corner, FALSE) )
      goto bail_out; /* a matrix operation failed, something is very wrong. */

  /* add rectangular path, and stroke it, picks up current line width, joins */
  if ( ! do_rect_path(pcl5_ctxt, &corner, TRUE)
      || ! draw_polygon_buffer(pcl5_ctxt) )
    goto bail_out;

  result = TRUE ;

bail_out:
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  return result;
}

Bool hpgl2op_EP(PCL5Context *pcl5_ctxt)
{
  struct HPGL2PolygonBuffer *buffer;
  HPGL2LineFillInfo *linefill_info = NULL;
  Bool result = FALSE;
  uint16 symbol_char;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  buffer = get_hpgl2_polygon_buffer(pcl5_ctxt);
  symbol_char = linefill_info->symbol_mode_char;
  linefill_info->symbol_mode_char = NUL; /* disable symbole mode. */
  result = draw_polygon_buffer(pcl5_ctxt);
  linefill_info->symbol_mode_char = symbol_char;

  return result;
}

Bool hpgl2op_ER(PCL5Context *pcl5_ctxt)
{
  HPGL2Point current_point, corner;
  Bool result = FALSE;
  uint8 terminator;
  int8 pen_state;
  HPGL2VectorInfo *vector_info;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  pen_state = vector_info->pen_state;

  /* parse corner parameter. */
  if ( hpgl2_scan_point(pcl5_ctxt, &corner) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 )
    return TRUE; /* syntax error */

  /* from here on in, goto bail_out error. */

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

  /* implicit pen down */
  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);

  if ( ! hpgl2_enter_polygon_mode(pcl5_ctxt, &current_point) )
    goto bail_out;

  /* calculate the actual corner location. */
  /* plot size scaling is handled by the transformation. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &corner, &corner, TRUE) )
      goto bail_out; /* a matrix operation failed, something is very wrong. */

  /* do_rect_path works only on absolute coordinates. */
  corner.x += current_point.x;
  corner.y += current_point.y;

  /* add rectangular path, and stroke it, picks up current line width, joins.
   * Despite the setting of relative plot mode, the drawing the rectangle is
   * done in terms of absolute coordinates.
   * We draw in absolute coordinates, even for ER.
   */
  if ( ! do_rect_path(pcl5_ctxt, &corner, TRUE)
      || ! draw_polygon_buffer(pcl5_ctxt) )
    goto bail_out;

  result = TRUE;

bail_out:
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  return result;
}

Bool hpgl2op_EW(PCL5Context *pcl5_ctxt)
{
  HPGL2Point arc_start, current_point, center;
  HPGL2Real radius, start_angle, sweep_angle, chord_angle = 5.0;
  HPGL2VectorInfo *vector_info;
  HPGL2LineFillInfo *linefill_info;
  uint8 terminator;
  int8  plot_mode, pen_state;
  uint16 symbol_mode_char;
  Bool result = FALSE;
  struct HPGL2PolygonBuffer *buffer;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  buffer = get_hpgl2_polygon_buffer(pcl5_ctxt);

  /* Retain the current values of the state that this operator will change */
  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  pen_state = vector_info->pen_state;
  plot_mode = vector_info->plot_mode;
  symbol_mode_char = linefill_info->symbol_mode_char;

  /* parse radius, start_angle, sweep_angle, chord_angle (optional). */
  if ( hpgl2_scan_real(pcl5_ctxt, &radius ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &start_angle) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &sweep_angle) <= 0 )
    return TRUE; /* syntax error */

  if (hpgl2_scan_separator(pcl5_ctxt) > 0
      && hpgl2_scan_real(pcl5_ctxt, &chord_angle) <= 0 )
    return TRUE; /* syntax error */

  (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  sweep_angle = clamp_hpgl2real(sweep_angle, -360.0, 360);
  chord_angle = clamp_hpgl2real(chord_angle, 0.5, 180.0);

  /* start angle is taken modulo 360 */
  if ( start_angle > 360 || start_angle < -360 )
    start_angle = (HPGL2Real) fmod(start_angle, 360.0) ;

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

  /* implicit pen down etc. must be done before entering polygon mode. */
  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);
  hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);
  /** \todo is there an API to control symbol mode? */
  linefill_info->symbol_mode_char = NUL;

  if ( ! hpgl2_enter_polygon_mode(pcl5_ctxt, &current_point) )
    goto bail_out;

  /* The location of the 0 degree point depends on whether sign of the
   * radius. A negative radius places 0 degree line along the -VE x_axis.
   * We get same effect by keeping 0 degree line along the +VE x_axis, and
   * increaing the start angle by 180 degrees.
   */
  if ( radius < 0.0 ) {
    radius = -radius;
    start_angle += 180;
    if ( start_angle > 360 || start_angle < -360 )
      start_angle = (HPGL2Real) fmod(start_angle, 360.0) ;
  }

  start_angle *= DEG_TO_RAD;

  /* The draw_arc is capable of handling the scaling, but must have
   * center and point of circle specified as absolute points in the same
   * coord space.
   */

  /* plot size scaling now handled through the explicit coordinate transform,
   * as well as HPGL user coordinates. */
  if ( !plotter_point_to_job_point(pcl5_ctxt,
                                   &current_point,
                                   &center,
                                   FALSE) )
    goto bail_out;

  /* the point on the circle must have absolute coordinates, in the current
   * coord space.
   */
  arc_start = center;
  arc_start.x += radius * cos(start_angle);
  arc_start.y += radius * sin(start_angle);

  /* hpgl2lineto does not deal with scaling, so have to do it here.
   * Note this handles plot size scaling also. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &arc_start, &arc_start, FALSE) )
    goto bail_out;

  /* The radius is not drawn if the wedge is actually a circle. */
  if ( fabs(sweep_angle) == 360.0) {
    if ( ! hpgl2_moveto(pcl5_ctxt, &arc_start) )
      goto bail_out;
  }
  else {
    if ( ! hpgl2_lineto(pcl5_ctxt, &arc_start) )
      goto bail_out;
  }

  /* draw an arc, connecting the start point on the arc with the current
   * point, and closing the path. Unlike the rectangle drawing, the
   * drawing of the arc will account for the scaling.
   */
  if ( ! draw_arc(pcl5_ctxt, &center, chord_angle, sweep_angle,
                  TRUE, FALSE, FALSE, HPGL2_CHORD_COUNT_ROUND_WEDGE)
      || ! draw_polygon_buffer(pcl5_ctxt) )
    goto bail_out;

  result = TRUE ;

bail_out:
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);

  /* restore current point (pen position) and pen state.
   */
  /**
   * \todo Do we want to * try to recover this state in the event of an error?
   */
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  hpgl2_set_plot_mode(pcl5_ctxt, plot_mode);
  linefill_info->symbol_mode_char = symbol_mode_char;

  return result;
}

Bool hpgl2op_FP(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer fill_mode = 0;
  uint8 terminator;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &fill_mode) > 0 )
    fill_mode = clamp_hpgl2int(fill_mode, 0, 1);

  (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  return fill_polygon_buffer(pcl5_ctxt,
                            fill_mode == 0 ? EOFILL_TYPE : NZFILL_TYPE);
}

/* polygons are built up in the current path, then their associated path
 * can be saved on the PM2 flag. Want to avoid the use of gsave etc when
 * dealing with the path in the polygon buffer.
 *
 * for the moment, we will simply allow the path to be built up in the
 * current path.
 */
Bool hpgl2op_PM(PCL5Context *pcl5_ctxt)
{
  uint8 terminator;
  HPGL2Integer mode;
  HPGL2Point pen_position = {0,0};

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 )
    mode = 0;
  else {
    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &mode) <= 0 )
      /* syntx error */
      return TRUE;

    mode = clamp_hpgl2int(mode, 0, 2);
    /* eat the terminator if there is one. */
    (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  }

  if ( hpgl2_in_polygon_mode(pcl5_ctxt) ) {

    switch (mode) {
      case 0:
        /* From reference printer, PM0 while in polygon mode does nothing. */
        break;

      case 1:
        /* close the current sub-path in the polygon buffer. */
        (void)hpgl2_close_polygon_buffer_subpath(pcl5_ctxt);
        break;

      case 2:
        (void)hpgl2_close_polygon_buffer_subpath(pcl5_ctxt);
        hpgl2_exit_polygon_mode(pcl5_ctxt);
        break;
    }
  }
  else {

    switch (mode) {
      case 0:
        /* error in setting polygon mode should not force
        * hpgl parsing to quit. Reset to consistent state,
        * and carry on.
        * PM0 ignored if we are lost.
        * */
        if ( !hpgl2_in_lost_mode(pcl5_ctxt)
              && hpgl2_currentpoint(pcl5_ctxt,
                                &pen_position.x, &pen_position.y) ) {
          if ( !hpgl2_enter_polygon_mode(pcl5_ctxt, &pen_position) )
            hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);
        }

        break;

        /* ignore other commands if not in the polygon mode */
      case 1:
        break;

      case 2:
        break;
    }
  }

  return TRUE ;
}

Bool hpgl2op_RA(PCL5Context *pcl5_ctxt)
{
  HPGL2Point current_point, corner;
  Bool result = FALSE;
  HPGL2VectorInfo *vector_info;
  int8 pen_state;
  uint8 terminator;
  struct HPGL2PolygonBuffer *buffer;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  buffer = get_hpgl2_polygon_buffer(pcl5_ctxt);
  pen_state = vector_info->pen_state;

  /* parse corner parameter. */
  if ( hpgl2_scan_point(pcl5_ctxt, &corner) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 )
    return TRUE; /* syntax error */

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

  /* implicit pen down must be done before entering the polygon mode.  */
  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);

  if ( ! hpgl2_enter_polygon_mode(pcl5_ctxt, &current_point) )
    goto bail_out;

  /* plot size scaling and user units handled by transformation below. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &corner, &corner, FALSE) )
      goto bail_out; /* a matrix operation failed, something is very wrong. */

  /* add rectangular path, and fill it, picks up current line width, joins
   * Both fill types produce identical results for simple rectangles.*/
  if ( ! do_rect_path(pcl5_ctxt, &corner, TRUE)
      || ! fill_polygon_buffer(pcl5_ctxt,
                               EOFILL_TYPE) )
    goto bail_out;

  result = TRUE ;

bail_out:
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  return result;
}

Bool hpgl2op_RR(PCL5Context *pcl5_ctxt)
{
  HPGL2Point current_point, corner;
  Bool result = FALSE;
  uint8 terminator;
  HPGL2VectorInfo *vector_info;
  int8 pen_state;
  struct HPGL2PolygonBuffer *buffer;
  HPGL2LineFillInfo * linefill_info;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  buffer = get_hpgl2_polygon_buffer(pcl5_ctxt);
  pen_state = vector_info->pen_state;
  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  /* parse corner parameter. */
  if ( hpgl2_scan_point(pcl5_ctxt, &corner) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 )
    return TRUE; /* syntax error */

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

  /* implicit pen down must be done before entering polygon mode.*/
  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);

  if ( ! hpgl2_enter_polygon_mode(pcl5_ctxt, &current_point) )
    goto bail_out;

  /* plot size scaling and user units handled by transformation below. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &corner, &corner, TRUE) )
      goto bail_out; /* a matrix operation failed, something is very wrong. */

  /* do_rect_path works only in absolute coordinates. */
  corner.x += current_point.x;
  corner.y += current_point.y;

  /* add rectangular path, and fill it, picks up current line width, joins
   * Both fill types produce identical results for simple rectangles.*/
  if ( ! do_rect_path(pcl5_ctxt, &corner, TRUE)
      || ! fill_polygon_buffer(pcl5_ctxt, EOFILL_TYPE) )
    goto bail_out;

  result = TRUE ;

bail_out:
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  return result;
}

Bool hpgl2op_WG(PCL5Context *pcl5_ctxt)
{
  HPGL2Point arc_start, current_point, center;
  HPGL2Real radius, start_angle, sweep_angle, chord_angle = 5.0;
  HPGL2VectorInfo *vector_info;
  HPGL2LineFillInfo *linefill_info;
  uint8 terminator;
  int8  plot_mode, pen_state;
  uint16 symbol_mode_char;
  Bool result = FALSE;
  struct HPGL2PolygonBuffer *buffer = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  vector_info = get_hpgl2_vector_info(pcl5_ctxt);

  /* Retain the current values of the state that this operator will change */
  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  pen_state = vector_info->pen_state;
  plot_mode = vector_info->plot_mode;
  symbol_mode_char = linefill_info->symbol_mode_char;
  buffer = get_hpgl2_polygon_buffer(pcl5_ctxt);

  /* parse radius, start_angle, sweep_angle, chord_angle (optional). */
  if ( hpgl2_scan_real(pcl5_ctxt, &radius ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &start_angle) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &sweep_angle) <= 0 )
    return TRUE; /* syntax error */

  if (hpgl2_scan_separator(pcl5_ctxt) > 0
      && hpgl2_scan_real(pcl5_ctxt, &chord_angle) <= 0 )
    return TRUE; /* syntax error */

  (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  sweep_angle = clamp_hpgl2real(sweep_angle, -360.0, 360);
  chord_angle = clamp_hpgl2real(chord_angle, 0.5, 180.0);

  /* start angle is taken modulo 360 */
  if ( start_angle > 360 || start_angle < -360 )
    start_angle = (HPGL2Real) fmod(start_angle, 360.0) ;

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

  /* implicit pen down etc. must be done before entering polygon mode. */
  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);
  hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);
  /** \todo is there an API to control symbol mode? */
  linefill_info->symbol_mode_char = NUL;

  if ( ! hpgl2_enter_polygon_mode(pcl5_ctxt, &current_point) )
    goto bail_out;

  /* The location of the 0 degree point depends on whether sign of the
   * radius. A negative radius places 0 degree line along the -VE x_axis.
   * We get same effect by keeping 0 degree line along the +VE x_axis, and
   * increaing the start angle by 180 degrees.
   */
  if ( radius < 0.0 ) {
    radius = -radius;
    start_angle += 180;
    if ( start_angle > 360 || start_angle < -360 )
      start_angle = (HPGL2Real) fmod(start_angle, 360.0) ;
  }

  /* The draw_arc is capable of handling the scaling, but must have
   * center and point of circle specified as absolute points in the same
   * coord space.
   * The transformation will map account for plot size scaling if applicable. */
  if ( !plotter_point_to_job_point(pcl5_ctxt, &current_point, &center, FALSE) )
    goto bail_out;

  start_angle *= DEG_TO_RAD;

  /* the point on the circle must have absolute coordinates, in the current
   * coord space.
   */
  /**
   * \todo is this going to be accurate enough?
   */
  arc_start = center;
  arc_start.x += radius * cos(start_angle);
  arc_start.y += radius * sin(start_angle);

  /* hpgl2_moveto does not deal with scaling itself, so need to do
   * the move to in terms of plotter units. The transformation will map
   * account for plot size scaling if applicable. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &arc_start,
                                     &arc_start, FALSE))
      goto bail_out;

  /* The radius is not drawn if the wedge is actually a circle. */
  if ( fabs(sweep_angle) == 360.0) {
    if ( ! hpgl2_moveto(pcl5_ctxt, &arc_start) )
      goto bail_out;
  }
  else {
    if ( ! hpgl2_lineto(pcl5_ctxt, &arc_start) )
      goto bail_out;
  }

  /* draw an arc, connecting the start point on the arc with the current
   * point, and closing the path. Unlike the rectangle drawing, the
   * drawing of the arc will account for the scaling.
   */
  if ( ! draw_arc(pcl5_ctxt, &center, chord_angle, sweep_angle,
                  TRUE, FALSE, FALSE, HPGL2_CHORD_COUNT_ROUND_WEDGE)
      || ! fill_polygon_buffer(pcl5_ctxt, EOFILL_TYPE) )
    goto bail_out;

  result = TRUE ;

bail_out:
  (void)hpgl2_set_polygon_mode(pcl5_ctxt, FALSE);

  /* restore current point (pen position) and pen state. */
  /**
   * \todo Do we want to * try to recover this state in the event of an error?
   */
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  hpgl2_set_plot_mode(pcl5_ctxt, plot_mode);
  linefill_info->symbol_mode_char = symbol_mode_char;

  return result;
}


/* This is a new, general purpose rectangle drawing routine, that draws into
 * either the current gstate path, or the polygon buffer, depending on the
 * polygon mode.
 * Rectangle point is interpreted according to current plot mode.
 * Rectangle point is passed in plotter units.
 */
/**
 * \todo @@@ replace the use of do_rect_path in the polygon buffer with this
 * routine.
 */
Bool general_rect_path(PCL5Context *pcl5_ctxt, HPGL2Point *in_p1, Bool closed )
{
  HPGL2Point current_point, plot_point;
  HPGL2VectorInfo *vector_info;
  struct HPGL2PolygonBuffer *polygon_buff = NULL;
  Bool result = TRUE;
  Bool absolute;
  HPGL2Point p1 = *in_p1;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  polygon_buff = get_hpgl2_polygon_buffer(pcl5_ctxt);
  absolute = ( vector_info->plot_mode == HPGL2_PLOT_ABSOLUTE );

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) ) {
      HQFAIL("No current point for rectangle path");
      result = FALSE; /* should always be a current point. */
      goto bail_out;
  }

  /* point is converted into absolute coordinates if the current mode is
   * relative.
   */
  if ( ! absolute ) {
    p1.x += current_point.x;
    p1.y += current_point.y;
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);
  }

  plot_point = current_point;
  plot_point.x = p1.x;
  result &= hpgl2_lineto(pcl5_ctxt, &plot_point);
  plot_point.y = p1.y;
  result &= hpgl2_lineto(pcl5_ctxt, &plot_point);
  plot_point.x = current_point.x;
  result &= hpgl2_lineto(pcl5_ctxt, &plot_point);

  if ( closed )
    result &= hpgl2_closepath(pcl5_ctxt);
  else {
    plot_point.y = current_point.y;
    result &= hpgl2_lineto(pcl5_ctxt, &plot_point);
  }

bail_out:
  if ( ! absolute )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE);
  return result;
}

/* RQ is like RR, but does not affect the polygon buffer. */
Bool hpgl2op_RQ(PCL5Context *pcl5_ctxt)
{
  HPGL2Point current_point, corner;
  Bool result = FALSE;
  uint8 terminator;
  HPGL2VectorInfo *vector_info;
  int8 pen_state;
  HPGL2LineFillInfo * linefill_info;
  Bool absolute;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  pen_state = vector_info->pen_state;
  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  absolute = ( vector_info->plot_mode == HPGL2_PLOT_ABSOLUTE );

  /* parse corner parameter. */
  if ( hpgl2_scan_point(pcl5_ctxt, &corner) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 )
    return TRUE; /* syntax error */

  /* RQ does not affect the polygon buffer. */
  if ( hpgl2_in_polygon_mode(pcl5_ctxt) )
    return TRUE;

  if ( ! hpgl2_currentpoint(pcl5_ctxt, &current_point.x, &current_point.y) )
    goto bail_out;

  if ( ! absolute )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);

  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);

  /* plot size scaling handled via below transformation. */
  if ( ! job_point_to_plotter_point(pcl5_ctxt, &corner, &corner, TRUE) )
      goto bail_out; /* a matrix operation failed, something is very wrong. */

  corner.x += current_point.x;
  corner.y += current_point.y;

  /* add rectangular path, and fill it, picks up current line width, joins
   * Both fill types produce identical results for simple rectangles.*/
  if ( ! general_rect_path(pcl5_ctxt, &corner, TRUE)
      || ! hpgl2_fill(pcl5_ctxt, EOFILL_TYPE, TRUE ) )
    goto bail_out;

  hpgl2_moveto(pcl5_ctxt, &current_point );

bail_out:
  /* only return false if there is a VMERROR */
  result = ( newerror != VMERROR ) ;

  if ( ! absolute )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);
  hpgl2_set_pen_mode(pcl5_ctxt, pen_state);
  return result;
}

/* ============================================================================
* Log stripped */
