/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2vector.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Vector Group" category.
 *
 *   AA    % Arc Absolute
 *   AR    % Arc Relative
 *   AT    % Absolute Arc Three Point
 *   BR    % Bezier Relative
 *   BZ    % Bezier Absolute
 *   CI    % Circle
 *   PA    % Plot Absolute
 *   PD    % Pen Down
 *   PE    % Polyline Encoded
 *   PR    % Plot Relative
 *   PU    % Pen Up
 *   RT    % Relative Arc Three Point
 */

#include "core.h"
#include "hpgl2vector.h"

#include "pcl5context_private.h"
#include "hpgl2scan.h"
#include "hpgl2config.h"
#include "hpgl2polygon.h"
#include "polygon_buffer_impl.h"
#include "printmodel.h"
#include "hpgl2linefill.h"
#include "hpgl2state.h"
#include "pcl5color.h" /* set_shade */
#include "pcl5scan.h"

#include "objects.h"
#include "swcopyf.h"
#include "miscops.h" /* run_ps_string, but only temporarily. */
#include "graphics.h"
#include "pathcons.h" /* gs_currentpoint */
#include "gu_cons.h"
#include "gstack.h" /* gstateptr */
#include "ascii.h"
#include "fileio.h"
#include "swerrors.h"
#include "control.h"
#include "pathops.h" /* STROKE_PARAMS */
#include "objstack.h"
#include "stacks.h"
#include "gu_path.h"
#include "constant.h"
#include "gu_fills.h"
#include "gschead.h" /* gsc_setgray */
#include "tables.h"


/* See header for doc. */
void default_HPGL2_vector_info(HPGL2VectorInfo* self)
{
  self->plot_mode = HPGL2_PLOT_ABSOLUTE;
  self->pen_state = HPGL2_PEN_UP;
}

/* Setting default state for DF or IN operation */
void hpgl2_set_default_vector_info(HPGL2VectorInfo *vector_info, Bool initialize)
{
  vector_info->plot_mode = HPGL2_PLOT_ABSOLUTE;
  if ( initialize ) {
    vector_info->pen_state = HPGL2_PEN_UP;
  }
}

/* synchronise the gstate of interpreter with the HPGL2 vector state. */
void hpgl2_sync_vector_info(HPGL2VectorInfo *vector_info, Bool initialize)
{
  UNUSED_PARAM(HPGL2VectorInfo*, vector_info);
  UNUSED_PARAM(Bool, initialize);

  /* Any vector info related changes to gstate should be done here. */

  return;
}

/* ---------- helper functions. ------------- */

/* map a user point to plotter point according to the scaling
 * attributes of the hpgl2 state.
 * out parameter may reference same HPGL2Point structure as the in parameter.
 * The relative parameter indicates if the input point is to be considered
 * a relative point or an absolute point. The output point is relative if
 * the input point is relative, and vice versa.
 *
 * user_point_to_plotter_point will apply plot size scaling to cordinates
 * processed when HPGL is not using user coordinates.
 */
Bool job_point_to_plotter_point(PCL5Context *pcl5_ctxt,
                                 HPGL2Point *in,
                                 HPGL2Point *out,
                                 Bool relative)
{
  HPGL2ConfigInfo *config_info = NULL;
  HPGL2Ctms *ctms = NULL;
  HPGL2Real x,y;

  HQASSERT(pcl5_ctxt != NULL, "PCL5context is NULL.");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  ctms = get_hpgl2_ctms(pcl5_ctxt);

  x = in->x;
  y = in->y;

  if ( config_info->scale_enabled ) {


    if ( relative )
      MATRIX_TRANSFORM_DXY(x, y, out->x, out->y,
                           &ctms->picture_frame_scaling_ctm);
    else
    {
      MATRIX_TRANSFORM_XY(x, y, out->x, out->y,
                          &ctms->picture_frame_scaling_ctm);
    }

    /* PCL COMPATIBILITY : on HP4650 reference printer, point factor scaling
     * applies to the point factor scale mode.
     */
    if ( config_info->scale_mode ==  HPGL2_SCALE_POINT_FACTOR )
    {
      out->x = out->x * horizontal_scale_factor(pcl5_ctxt);
      out->y = out->y * vertical_scale_factor(pcl5_ctxt);
    }
  }
  else
  {
    out->x = x * horizontal_scale_factor(pcl5_ctxt);
    out->y = y * vertical_scale_factor(pcl5_ctxt);
  }

  return TRUE;
}

/* transforms plotter coordinates into user coordinates. If HPGL is not scaled,
 * this function inverts the plot size scaling.
 */
Bool plotter_point_to_job_point(PCL5Context *pcl5_ctxt,
                                 HPGL2Point *in,
                                 HPGL2Point *out,
                                 Bool relative)
{
  HPGL2ConfigInfo *config_info = NULL;
  HPGL2Ctms  *ctms = NULL;
  HPGL2Real x,y;

  HQASSERT(pcl5_ctxt != NULL, "PCL5context is NULL.");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  ctms = get_hpgl2_ctms(pcl5_ctxt);

  x = in->x;
  y = in->y;

  if ( config_info->scale_enabled ) {

    /* PCL COMPATIBILITY : on HP4650 reference printer, point factor scaling
     * applies to the point factor scale mode.
     */
    if ( config_info->scale_mode ==  HPGL2_SCALE_POINT_FACTOR )
    {
      x = x / horizontal_scale_factor(pcl5_ctxt);
      y = y / vertical_scale_factor(pcl5_ctxt);
    }

    if ( relative )
      MATRIX_TRANSFORM_DXY(x, y, out->x, out->y,
                          &ctms->inverse_picture_frame_scaling_ctm);
    else
      MATRIX_TRANSFORM_XY(x, y, out->x, out->y,
                          &ctms->inverse_picture_frame_scaling_ctm);

  }
  else
  {
    out->x = x / horizontal_scale_factor(pcl5_ctxt);
    out->y = y / vertical_scale_factor(pcl5_ctxt);
  }

  return TRUE;
}

static Bool track_symbol_point(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2Point point ;
  HPGL2PointList *item ;

  UNUSED_PARAM(HPGL2LineFillInfo*, linefill_info) ;
  HQASSERT(linefill_info->symbol_mode_char != NUL, "No symbol mode glyph set; shouldn't be tracking points") ;

  if ( !gs_currentpoint(&gstateptr->thepath, &point.x, &point.y) )
    return FALSE ;

  item  = mm_alloc(mm_pcl_pool, sizeof(HPGL2PointList),
                   MM_ALLOC_CLASS_PCL_CONTEXT) ;
  if ( !item )
    return error_handler(VMERROR) ;

  item->point = point ;
  item->next = print_state->SM_points ;
  print_state->SM_points = item ;

  return TRUE ;
}

static Bool draw_symbol_points(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2PointList *item ;

  if ( linefill_info->symbol_mode_char == NUL )
    return TRUE ;

  for ( item = print_state->SM_points ; item ; item = item->next ) {
    HPGL2Real coords[2] ;

    coords[0] = item->point.x ;
    coords[1] = item->point.y ;

    if ( !gs_moveto(TRUE, coords, &gstateptr->thepath) )
      return FALSE ;

    /* Centres the symbol on the current point. */
    if ( !hpgl2_draw_symbol(pcl5_ctxt, hpgl2_get_SM_char(pcl5_ctxt)) )
      return TRUE ;
  }

  return TRUE ;
}

static void free_symbol_points(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;

  while ( print_state->SM_points ) {
    HPGL2PointList *item = print_state->SM_points ;
    print_state->SM_points = item->next ;
    mm_free(mm_pcl_pool, item, sizeof(HPGL2PointList)) ;
  }
}

/** Do a move in the HPGL2 plotter space (as defined by the
 *  gstate CTM. Any scaling must have been converted to plotter
 *  coords before calling this function.
 */
Bool hpgl2_moveto(PCL5Context *pcl5_ctxt, HPGL2Point *point)
{
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt) ;
  Bool absolute = (vector_info->plot_mode == HPGL2_PLOT_ABSOLUTE) ;
  Bool result;

  if ( hpgl2_in_polygon_mode(pcl5_ctxt) ) {
    result = hpgl2_polygon_moveto(get_hpgl2_polygon_buffer(pcl5_ctxt),
                                point, absolute);

    HQASSERT(result, "hpgl2_moveto failed for polygon buffer.");
  }
  else {
    HPGL2Real coords[2] ;

    coords[0] = point->x ;
    coords[1] = point->y ;
    result = gs_moveto(absolute, coords, &gstateptr->thepath);

    HQASSERT(result, "hpgl2_moveto failed.");
  }

  return result;

}

/** Draw a line in the HPGL2 plotter space (as defined by the
 *  gstate CTM. Any scaling must have been converted to plotter
 *  coords before calling this function.
 */
Bool hpgl2_lineto(
  PCL5Context*  pcl5_ctxt,
  HPGL2Point*   point)
{
  HPGL2PrintState *print_state;
  HPGL2LineFillInfo *linefill_info;
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  Bool absolute = (vector_info->plot_mode == HPGL2_PLOT_ABSOLUTE);
  Bool pen_down = (vector_info->pen_state == HPGL2_PEN_DOWN);
  Bool pen_selected = pen_is_selected(pcl5_ctxt);
  HPGL2Real coords[2];

  /* If we are in polygon mode, delegate lineto handling to the polygon buffer
   * implementation. There is no symbol mode processing for the polygon buffer.
   */
  if ( hpgl2_in_polygon_mode(pcl5_ctxt) ) {
    return hpgl2_polygon_plot(get_hpgl2_polygon_buffer(pcl5_ctxt),
                              absolute, pen_down, pen_selected, point);
  }

  /* Polygon buffer is a path, concurrently maintained. */
  coords[0] = point->x ;
  coords[1] = point->y ;

  if ( pen_selected && pen_down ) {
    if ( !gs_lineto(absolute, TRUE, coords, &gstateptr->thepath) )
      return FALSE ;

    print_state = get_hpgl2_print_state(pcl5_ctxt) ;
    print_state->path_to_draw = TRUE ;

  } else { /* vector_info->pen_state == HPGL2_PEN_UP  || pen not selected. */
    if ( !gs_moveto(absolute, coords, &gstateptr->thepath) )
      return FALSE ;
  }

  /* Track the current pen position now and draw the symbol later. */
  if ( pen_selected ) {
    linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
    if ( (linefill_info->symbol_mode_char != NUL) && !track_symbol_point(pcl5_ctxt) ) {
      return FALSE ;
    }
  }

  return TRUE ;
}

/* See header for doc. */
Bool hpgl2_stroke_internal(STROKE_PARAMS *params, STROKE_OPTIONS options)
{
  Bool result;
  uint8 savedRop = getPclRop();

  /* There are four special rops which are honored; all others use the
   * default. */
  switch (savedRop) {
    default:
      setPclRop(PCL_ROP_TSo);
      break;

    case PCL_ROP_BLACK:
    case PCL_ROP_D:
    case PCL_ROP_DTo:
    case PCL_ROP_WHITE:
      /* These rops are honored; do nothing. */
      break;
  }

  result = dostroke(params, GSC_FILL, options);

  setPclRop(savedRop);

  return result;
}

Bool hpgl2_stroke(PCL5Context *pcl5_ctxt,
                  Bool update_linetype_residue,
                  Bool clear_path,
                  Bool apply_thin_line_override)
{
  STROKE_PARAMS params ;
  PATHINFO *path_info = &gstateptr->thepath;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);

  /* The PCL pattern in the core affects all the rendering, including HPGL2
   * vectors and fills. The appropriate pattern for stroked lines, with solid
   * colors is the solid foreground pattern, which supports transparency.
   * Sync the pen colors here, as the gstate current color could be affected
   * by the use of fills.
   */
  hpgl2_sync_pen_color(pcl5_ctxt, FALSE);

  set_gstate_stroke(&params, path_info, NULL, FALSE) ;

   /* if dashing is degenerate, params.dashcurrent will not be initialzed,
   * and garbage values can cause FP exception when cast from double to float.
   * The code that judges dash to be degenerate should clear dashcurrent.
   * But, easier for moment to set it here.
   */
  params.dashcurrent = 0.0;

  if ( 1 <= linefill_info->line_type.type && linefill_info->line_type.type <= 8 ) {
    params.linestyle.dashoffset += (USERVALUE)linefill_info->line_type.residue ;
  }

  /* If the first and last points coincide do a closepath to make a corner.
     Skip this if we're just doing dots at each coord, otherwise we'll be
     missing the first and last (external) line caps. */

  /* Since path_close will close the last sub-path of the path, the test on
   * whether to apply it should be based on the last sub-path.
   * Do not close a subpath that consists of a single moveto if the pen is
   * up. This avoids drawing a dot for a single PU move. If pen is down,
   * then we do want to draw a dot and so need to close the path
   */

  if ( linefill_info->line_type.type != 0 &&
       fabs(path_info->lastline->point.x -
            path_info->lastpath->subpath->point.x) < EPSILON &&
       fabs(path_info->lastline->point.y -
            path_info->lastpath->subpath->point.y) < EPSILON &&
       ( path_info->lastpath->subpath->next != NULL
         ||  vector_info->pen_state == HPGL2_PEN_DOWN ) ) {
    if ( !path_close(CLOSEPATH, path_info) )
      return FALSE ;
  }

  /* Use butt cap and no join for lines 0.35mm or less.
     PCL COMPATIBILITY This override is disabled for the HP4700 for
     hatching lines. */
  if ( apply_thin_line_override )
    hpgl2_override_thin_line_attributes(pcl5_ctxt, &params.linestyle) ;

  if ( !hpgl2_stroke_internal(&params, STROKE_NORMAL) )
    return FALSE ;

  /* Line type residue is not updated in CI, EA, EP, ER, EW, FPP, PM, RA, RR, WG ops. */

  if ( update_linetype_residue &&
       1 <= linefill_info->line_type.type && linefill_info->line_type.type <= 8 ) {
    linefill_info->line_type.residue = params.dashcurrent ;
  }

  if ( clear_path )
    return gs_newpath() ;
  else
    return TRUE;
}

Bool hpgl2_curveto(PCL5Context *pcl5_ctxt, HPGL2Point *p1,
                   HPGL2Point *p2, HPGL2Point * p3)
{
  SYSTEMVALUE args[6];
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  Bool absolute = (vector_info->plot_mode == HPGL2_PLOT_ABSOLUTE );
  Bool stroked = (vector_info->pen_state == HPGL2_PEN_DOWN);
  Bool pen_selected = pen_is_selected(pcl5_ctxt);

  HQASSERT( vector_info->pen_state == HPGL2_PEN_DOWN
            || vector_info->pen_state == HPGL2_PEN_UP,
            "Illegal pen state" );

  if ( hpgl2_in_polygon_mode(pcl5_ctxt) )
    return hpgl2_polygon_curveto(get_hpgl2_polygon_buffer(pcl5_ctxt),
                                 p1, p2, p3, absolute, stroked, pen_selected);
  else {
    if ( pen_selected ) {
      args[0] = p1->x; args[1] = p1->y;
      args[2] = p2->x; args[3] = p2->y;
      args[4] = p3->x; args[5] = p3->y;
      return gs_curveto( absolute, stroked, args, &gstateptr->thepath);
    }
    else {
      args[0] = p3->x; args[1] = p3->y;
      return gs_moveto( absolute, args, &gstateptr->thepath);
    }
  }
}

Bool hpgl2_closepath(PCL5Context *pcl5_ctxt)
{

  if ( hpgl2_in_polygon_mode(pcl5_ctxt) ) {
    HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
    Bool pen_down = ( vector_info->pen_state == HPGL2_PEN_DOWN );
    return hpgl2_polygon_closepath(get_hpgl2_polygon_buffer(pcl5_ctxt),
                                    pen_down);
  }
  else {
    path_close(CLOSEPATH, &gstateptr->thepath);
    return TRUE;
  }
}

/* call this immediately after drawing a path. Assumes current
 * point marks end of the path. Clears symbols, update the
 * carriage return point to the end of the path just drawn.
 */
static void finalize_plot_path(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;

  print_state->path_to_draw = FALSE ;
  free_symbol_points(pcl5_ctxt) ;

  /* Update the carriage return point to the current pen location.
   * HPGL2 should always have a current point */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    HQFAIL("No current point found when updating carriage return point");

  return;
}

static Bool draw_path(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  SYSTEMVALUE coords[2];

  /* PM will look after itself in terms of drawing, via EP type operations */
  if ( ! hpgl2_in_polygon_mode(pcl5_ctxt) ) {

    /* save currentpoint, which marks end point of the path. */
    if ( !gs_currentpoint(&gstateptr->thepath, &coords[0], &coords[1]) ) {
      HQFAIL("No current point found when finalizing path.");
      return FALSE;
    }

    if ( print_state->path_to_draw ) {
      /* Don't draw hairlines for zero-length dashes at the end of the last line. */
      if ( gstateptr->thepath.lastline->type == LINETO )
        gstateptr->thepath.flags |= PATHINFO_IGNORE_ZERO_LEN_DASH ;

      if ( !hpgl2_stroke(pcl5_ctxt, TRUE, TRUE, TRUE) )
        return FALSE;

      /* Reinstate the pen position. */
      if ( !gs_moveto(TRUE, coords, &gstateptr->thepath) ) {
        HQFAIL("Cannot restore currentpoint after hpgl2_stroke");
        return FALSE;
      }
    }

    /* Draw at symbol at each of the coords in the path.
       There can be symbols, even if there is no path to draw. */
    if ( linefill_info->symbol_mode_char != NUL ) {
      if ( !draw_symbol_points(pcl5_ctxt) )
        return FALSE;
    }

    /* Reset current point after symbols.
     * HPGL must always has a currentpoint. */
    if ( !gs_moveto(TRUE, coords, &gstateptr->thepath) ) {
      HQFAIL("Cannot restore current point after handling symbols");
      return FALSE;
    }
  }
  return TRUE ;
}

/* returns the number of points plotted.  */
static int32 plot_points(PCL5Context *pcl5_ctxt)
{
  Bool result = FALSE ;
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  HPGL2Point point ;
  Bool relative_points = ( vector_info->plot_mode == HPGL2_PLOT_RELATIVE );
  int32 pointcount = 0;

  /* Read in the list of points and handle according to current
     pen up/down, absolute/relative status. */
  while ( hpgl2_scan_point(pcl5_ctxt, &point) > 0 ) {

    /* move from job units to plotter units. Plot size scaling is performed
     * through this transform. */
    if ( !job_point_to_plotter_point(pcl5_ctxt, &point, &point,
                                  relative_points) )
      goto cleanup;

    /* store only plotter coords. */

    if ( !hpgl2_lineto(pcl5_ctxt, &point) )
      goto cleanup ;

    pointcount++;

    if ( !hpgl2_scan_separator(pcl5_ctxt) )
      break ;
  }

  if ( !draw_path(pcl5_ctxt) )
    goto cleanup;

  result = TRUE ;

 cleanup :
  finalize_plot_path(pcl5_ctxt);
  return result ? pointcount : -1 ;
}

/* draw an arc with given centre, starting at the current pen position,
 * through specified angle, using specified chord angle. The radius
 * is implicitly defined by the point and the centre. The drawing
 * is done in terms of the current coords space, whether plotter or user.
 * Pen up/down, line attributes etc are all inherited from the current
 * state of the print enviroment.
 *
 * The arc can be drawn in relative or absolute coordinates depending on
 * the plot mode attribute.
 *
 * Arcs are flatten according to the chord angle parameter, and drawn according
 * with the usual hpgl line drawing primatives.
 *
 * Arcs do not support symbol mode, and that mode is explictly disabled in
 * this function.
 *
 *
 * At end ,current point is updated to be the last point on the arc.
 */
Bool draw_arc(PCL5Context *pcl5_ctxt, HPGL2Point *center, HPGL2Real chord_ang,
              HPGL2Real sweep, Bool closed, Bool stroked, Bool update_residue,
              uint8 chord_count_policy )
{
  Bool            result = FALSE,
                  relative;
  int32           chord_count;
  HPGL2Point      plot_point,
                  start,
                  circle_point;
  uint16          saved_SM_char;
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  HPGL2Real       abs_sweep = fabs(sweep);

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");
  HQASSERT(chord_ang >= 0.5 && chord_ang <= 180, "Invalid chord angle");


  relative = (vector_info->plot_mode == HPGL2_PLOT_RELATIVE);

  /* Find the start point - the current pen position - */
  if ( relative ) {
    start.x = 0;
    start.y = 0;
  }
  else {
    if ( !hpgl2_currentpoint( pcl5_ctxt, &start.x, &start.y ) )
    {
      HQFAIL("No current point");
      return FALSE;
    }
  }

  /* move from job units to plotter units. Plot size scaling is performed
   * through this transform. */
  if ( ! plotter_point_to_job_point(pcl5_ctxt,
                                         &start,
                                         &start,
                                         relative) ) {
    HQFAIL("Failed to map plotter point to user point");
    return FALSE;
  }

  saved_SM_char = line_info->symbol_mode_char;
  line_info->symbol_mode_char = NUL;    /* disable symbol mode for arcs. */

  /* ensure whole number of chords.
   * Essentially, one can increase or decrease the chord angle.
   *
   * PCL COMPATIBILITY
   * The HP4250 / HP4650 reference printers appear to operate a procedure
   * that will increase the chord angle slightly if that will produce a
   * whole number of chords around the sweep of the arc. Increasing the
   * angle only ever applies a small increase. Otherwise, the chord angle
   * is decreased so that it implies a whole number of chords around the
   * sweep.
   * Experiment suggests that for small numbers of chords, if the ratio
   * of increase in requested angle over the requested angle is < 1/119,
   * then angle is increased.
   * Wedges seem to have a slightly different rule to circles/arcs.
   * Also, the rules on rounding appears to change as the number of chords
   * increases. Have not been able to deduce the exact rule.
   * */

  /* Should have trapped dodgy chord angles and sweep angles prior to here,
   * but check just for safety.   */
  if ( chord_ang < 0.5 )
    return TRUE;

  if ( abs_sweep < EPSILON )
    return TRUE;

  /* Calculate nominal number of chords implied by chord angle. */
  chord_count = (int32) ( abs_sweep / chord_ang ) ;

  /* catch degenerate requests.
  * Need 3 chords at least for a circle */
  if ( chord_count == 0 ) {
    chord_count = 1;
    chord_ang = abs_sweep;
  }
  else if ( ( chord_count * chord_ang ) < abs_sweep )
  {
    HPGL2Real nominal_chord_angle = abs_sweep / chord_count;
    HPGL2Real angle_difference = nominal_chord_angle - chord_ang;

    HQASSERT( angle_difference >= 0, "Weird chord angle difference.");

#define HPGL2_WIDEN_METRIC ( 1.0/119 )

    switch ( chord_count_policy )
    {
      case HPGL2_CHORD_COUNT_ROUND_WEDGE:

        if ( (angle_difference / chord_ang) <= HPGL2_WIDEN_METRIC )
        {
          chord_ang = nominal_chord_angle;
        }
        else
        {
          ++chord_count;
          chord_ang = abs_sweep / chord_count;
        }

        break;

      default:
        HQFAIL("Unknown chord rounding policy"); /* FALLTHROUGH */
      case HPGL2_CHORD_COUNT_ROUND_ARC:
        if (angle_difference != 0.0) {
          ++chord_count;
          chord_ang = abs_sweep / chord_count;
        }
        break;

      case HPGL2_CHORD_COUNT_ROUND_CIRCLE:

        if ( (angle_difference / chord_ang) < HPGL2_WIDEN_METRIC )
        {
          chord_ang = nominal_chord_angle;
        }
        else
        {
          ++chord_count;
          chord_ang = abs_sweep / chord_count;
        }

        break;
    }

  }

  HQASSERT( chord_ang > 0, "Illegal chord angle");
  HQASSERT( chord_count >= 1, "Illegal chord count");

  /* chord angle to reflect drawing direction. */
  if ( sweep < 0.0 )
    chord_ang = -chord_ang;

  chord_ang *= DEG_TO_RAD;

  /* draw arc by rotating the circle_point around center point and translating
   * the rotated points by offset to center of arc. Draw chords between points
   */

  /* circle point is expressed relative to it center, regardless of the
   * plot mode. Arc drawn by rotating the circle point around center, then
   * translating point back wrt to center.
   */
  circle_point.x = start.x - center->x;
  circle_point.y = start.y - center->y;

  /* draw chords */
  while ( chord_count-- ) {
    HPGL2Real new_x, new_y;

    /* move to new point in user space circle */
    new_x = (cos(chord_ang) * circle_point.x) +
              (-sin(chord_ang) * circle_point.y);
    new_y = (sin(chord_ang) * circle_point.x) +
              (cos(chord_ang) * circle_point.y);

    if ( !relative ) {
      /* Plot the point wrt to circle center. */
      plot_point.x = new_x + center->x;
      plot_point.y = new_y + center->y;
    }
    else {
      /* plot difference from the last plot point. */
      plot_point.x = new_x - circle_point.x;
      plot_point.y = new_y - circle_point.y;
    }

    circle_point.x = new_x;
    circle_point.y = new_y;

    /* move from job units to plotter units. Plot size scaling is performed
     * through this transform. */
    if ( !job_point_to_plotter_point(pcl5_ctxt,
                                     &plot_point,
                                     &plot_point,
                                     relative) )
      return FALSE;

    hpgl2_lineto(pcl5_ctxt, &plot_point);
  }

  if ( closed && !hpgl2_closepath(pcl5_ctxt) )
    goto bail_out;

  if ( stroked ) {
    HPGL2Real coords[2];

    HQASSERT( ! hpgl2_in_polygon_mode(pcl5_ctxt),
      "Cannot stroke polygon buffer content while in polygon mode");

    if ( !gs_currentpoint(&gstateptr->thepath, &coords[0], &coords[1]) )
      goto bail_out;

    if ( !hpgl2_stroke(pcl5_ctxt, update_residue, TRUE, TRUE) )
      goto bail_out;

    /* Reinstate the current point. */
    if ( !gs_moveto(TRUE, coords, &gstateptr->thepath) )
      goto bail_out;

  }

  result = TRUE;

bail_out:
  line_info->symbol_mode_char = saved_SM_char;
  return result;
}

/* Calculate the angle between 2 vectors from a common origin. Non-reflex angle
 * between the points. This will calculate the non-reflexive angle between the
 * vectors. Result is in radians.
 *
 * Using the cross product to dervie the sine of the angle between the vectors.
 * The angle between vectors could be [-pi,pi] radians. As sine not unique in
 * this range, calculate the angle of u to midpoint vector between u and v -
 * which guarantees to be [-pi/2,pi/2] radians and hence have unique sine.
 * Actual angle is found by doubling the angle between u and the midpoint.
 *
 * If u and v are colinear but opposite, angle is chosen as +ve pi radians.
 *
 *
 */
static HPGL2Real angle_between_vectors(HPGL2Point *u, HPGL2Point*v)
{
  HPGL2Point mid_point;
  HPGL2Real cross_product, sine_theta;

  /* check for same point. */
  if ( COORD_EQUAL_WITH_EPSILON(u->x,v->x)
      && COORD_EQUAL_WITH_EPSILON(u->y,v->y) )
      return 0.0;

  /* check for 180 degree case. */
  if ( COORD_EQUAL_WITH_EPSILON( (u->x + v->x), 0.0 )
    && COORD_EQUAL_WITH_EPSILON( (u->y + v->y), 0.0 ) )
    return 180.0 * DEG_TO_RAD;

  mid_point.x = ( u->x + v->x ) ;
  mid_point.y = ( u->y + v->y ) ;

  cross_product = u->x * mid_point.y - u->y * mid_point.x ;

  /* cross_product = |u|* |mid_point| * sine theta */
  sine_theta = cross_product /
                ( sqrt(u->x * u->x + u->y * u->y) *
                  sqrt(mid_point.x * mid_point.x + mid_point.y * mid_point.y));

  return (2 * asin(sine_theta));
}

enum { GENERIC, START_INTER, INTER_END } ;
/* enumeration used in special case handling when deriving the center
 * of arc.
 */



/* draw a 3 point arc, using the points specified in the current user
 * coordinate space. Calculate center of arc, and sweep angle going
 * start-intermediate-end. Draw with usual arc drawing code.
 *
 * On exit, the current point is updated to the end point.
 * intermediate and end points must be specified in the current plot
 * mode. The start point for the arc is the current pen position (i.e. the
 * current point).
 */
static Bool draw_3_point_arc(PCL5Context *pcl5_ctxt, HPGL2Point *inter,
                              HPGL2Point *end, HPGL2Real chord_ang)
{
  int32 got_center_x = GENERIC, got_center_y = GENERIC;
  HPGL2Point center, local_start, local_inter, local_end;
  HPGL2Real sweep = 0.0,
            sweep_to_inter;
  Bool closed = FALSE,
       colinear = FALSE;
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  Bool relative_plot = (vector_info->plot_mode == HPGL2_PLOT_RELATIVE);
  Bool stroked = ! hpgl2_in_polygon_mode(pcl5_ctxt);

  local_inter = *inter;
  local_end = *end;

  /* shut up, compiler */
  center.x = 0;
  center.y = 0;

  /* the Pen Position (current point) will be the start of the arc. Want it
   * in current units. current point could be in polygon buffer, or normal
   * path depending on the drawing mode. */
  if ( !relative_plot ) {
    if ( ! hpgl2_currentpoint(pcl5_ctxt, &local_start.x, &local_start.y) ) {
      HQFAIL("No currentpoint in HPGL2");
      return FALSE;
    }

    /* plot size scaling is accounted for in this transform as well as user
     * units. */
    if ( !plotter_point_to_job_point(pcl5_ctxt, &local_start,
                                     &local_start, relative_plot) ) {
      HQFAIL("Cannot map plotter point to user point");
      return FALSE;
    }
  }
  else {
    local_start.x = 0.0;
    local_start.y = 0.0;
  }

  /* Various degenerate cases need handling.
   *
   * Specification says that 3 coincident points draws a single dot.
   *
   * If start and end points are equal, then arc is defined as a
   * circle with diameter defined by the intermediate point.
   *
   * If start, intermediate and end points are colinear, and the intermediate
   * point is between the start and end point, then a straight line
   * is drawn from start to finish.
   * This case applied if the intermediate point coincides with either the
   * start or finish.
   *
   * If the intermediate point is outside the start to end range. Then line
   * is drawn from start to edge of page and another from the opposite edge
   * to the end point. One of these lines will go through the intermediate
   * point.
   */

  if ( (COORD_EQUAL_WITH_EPSILON(local_start.x,local_inter.x)
          && COORD_EQUAL_WITH_EPSILON(local_start.y,local_inter.y))
      || (COORD_EQUAL_WITH_EPSILON(local_inter.x,local_end.x)
           && COORD_EQUAL_WITH_EPSILON(local_inter.y,local_end.y)) ) {
    colinear = TRUE;
    goto do_drawing;
    /* treat the linear cases as a sort of exception. */
  }

  if ( COORD_EQUAL_WITH_EPSILON(local_start.x,local_end.x)
       && COORD_EQUAL_WITH_EPSILON(local_start.y,local_end.y) ) {
    /* Circle case.  */
    center.x = ( local_start.x + local_inter.x ) / 2.0 ;
    center.y = ( local_start.y + local_inter.y ) / 2.0 ;
    sweep = 360.0;
    closed = TRUE;
    colinear = FALSE;
    /* points not treated as colinear. */
  }
  else {
    HPGL2Point  c_start,
                c_end,
                c_inter;
    Bool        complement_angle = FALSE;

    /** \todo Colinearity check to go here! */


    /* Now do some work. We calculate the center of the circle using the
     * equations of the chords. The center of the arc will be the intersection
     * of the radii that meet chords at right angles.
     */

    /* Horizontal or vertical chords immediately give one or other of
     * coordinate of the center. Also, need to avoid zeros and infinities
     * in the general calculation. Hence special handling.
     */

    if ( COORD_EQUAL_WITH_EPSILON(local_inter.y,local_start.y) ) {

      /* horizontal start_inter chord defines x coord of center. */
      center.x = ( local_inter.x + local_start.x ) / 2.0;
      got_center_x = START_INTER;

    }

    if (COORD_EQUAL_WITH_EPSILON(local_inter.x, local_start.x) ) {

      HQASSERT(got_center_x == GENERIC, "One chord cannot define center.");

      /* vertical start_inter chord defines y coord of center. */
      center.y = (local_inter.y + local_start.y) / 2.0;
      got_center_y = START_INTER;

    }

    if ( COORD_EQUAL_WITH_EPSILON(local_inter.y, local_end.y) ) {

      /* if the center x coord is already deduced, then 3 points must be
       * colinear.
       */
      if ( got_center_x != GENERIC ) {
        HQASSERT(got_center_x == START_INTER,"Chord state inconsistent");
        colinear = TRUE;
        goto do_drawing; /* treat colinearity as a sort of exception */
      }
      else {
        center.x = ( local_inter.x + local_end.x ) / 2.0;
        got_center_x = INTER_END;
      }
    }

    if ( COORD_EQUAL_WITH_EPSILON(local_inter.x, local_end.x) ) {

      /* if center y coord is already defined, then 3 points must be
       * colinear.
       */

      HQASSERT(got_center_x != INTER_END, "One chord cannot define center.");

      if ( got_center_y != GENERIC ) {
        HQASSERT(got_center_y == START_INTER,"Chord state inconsistent");
        colinear = TRUE;
        goto do_drawing; /* treat colinearity as a sort of exception */
      }
      else {
        center.y = ( local_inter.y + local_end.y ) / 2.0;
        got_center_y = INTER_END;

      }
    }

      /*  examine the gradients to determine center, and check linearity
       *  control points. */
    if ( got_center_x == GENERIC && got_center_y == GENERIC ) {

      HPGL2Real grad_start_inter,
                grad_inter_end,
                chord_x,
                chord_y,
                radius_gradient;

      /* OK, do it the hard way. Calculate the center x position, using the
       * equations for the radii that meet the chords at right angles. These
       * bisect the chords.
       */

      HQASSERT( local_inter.x != local_start.x,
                  "Chord special case handling failed");
      HQASSERT( local_inter.y != local_start.y,
                  "Chord special case handling failed");
      HQASSERT( local_inter.x != local_end.x,
                  "Chord special case handling failed");
      HQASSERT( local_inter.y != local_end.y,
                  "Chord special case handling failed");

      grad_start_inter = (local_inter.y - local_start.y)
                          / (local_inter.x - local_start.x) ;
      grad_inter_end = (local_end.y - local_inter.y)
                          / (local_end.x - local_inter.x) ;


      if ( VAL_EQUAL_WITH_EPSILON( grad_start_inter, grad_inter_end ) ) {
        colinear  = TRUE;
        goto do_drawing; /* treat colinearity as a sort of exception */
      }

       /* The radii intersect at center. Equating for center.y, we get...  */
      center.x = ( ( grad_start_inter * grad_inter_end * (local_start.y - local_end.y) )
                   + ( grad_inter_end * (local_start.x + local_inter.x) )
                   - ( grad_start_inter * (local_inter.x + local_end.x ) )
                 )
                 /
                 ( 2 * (grad_inter_end - grad_start_inter ) ) ;

      chord_x = ( local_inter.x + local_start.x ) / 2.0;
      chord_y = ( local_inter.y + local_start.y ) / 2.0;
      radius_gradient = -1 * (1.0 / grad_start_inter );

      center.y = (radius_gradient * ( center.x - chord_x )) + chord_y ;

    }
    else {
      HPGL2Real   chord_x,
                  chord_y,
                  radius_gradient;

      /* at least one of the center coords came from a special case of a
       * chord ( i.e. horizontal or vertical chord).
       * Each special case will come from a different chord.
       * If we need to, calculate missing coordinate. Note that a
       * radius meeting a chord at right angles will bisect the chord.
       *
       * Colinearity with these special cases will already have been
       * caught.
       */

      HQASSERT(got_center_x != got_center_y,
                "Cannot use single chord to find center");

      if ( got_center_x == GENERIC || got_center_y == GENERIC ) {
        /* got some calcultion to do ... */

        if ( got_center_x == START_INTER || got_center_y == START_INTER ) {
          /* ... with the INTER_END vector. Calculate eqn of radius through
           * center point of inter_end chord.
           */

          HQASSERT(!COORD_EQUAL_WITH_EPSILON( local_inter.x, local_end.x ),
                      "Chord special case handling failed");
          HQASSERT(!COORD_EQUAL_WITH_EPSILON( local_inter.y, local_end.y ),
                      "Chord special case handling failed");

          chord_x = ( local_inter.x + local_end.x ) / 2.0;
          chord_y = ( local_inter.y + local_end.y ) / 2.0;
          radius_gradient =  -1.0 * ( (local_end.x - local_inter.x)
                                      / (local_end.y - local_inter.y) );

        }
        else {
          /* ... with the START_END vector. Calculate eqn of radius through
           * center point of the start_end chord.
           */

          HQASSERT(!COORD_EQUAL_WITH_EPSILON( local_inter.x, local_start.x ),
                      "Chord special case handling failed");
          HQASSERT(!COORD_EQUAL_WITH_EPSILON( local_inter.y, local_start.y ),
                      "Chord special case handling failed");

          chord_x = ( local_start.x + local_inter.x ) / 2.0;
          chord_y = ( local_start.y + local_inter.y ) / 2.0;
          radius_gradient =  -1.0 * ( (local_inter.x - local_start.x)
                                      / (local_inter.y - local_start.y) );

          HQASSERT ( got_center_x == INTER_END || got_center_y == INTER_END,
             "Special case chords tracking gone awry" );

        }

        /* Now calculate the missing coord from equation of radius. */
        if ( got_center_x == GENERIC ) {
          center.x = ((center.y - chord_y) / radius_gradient) + chord_x ;
        } else {
          center.y = (radius_gradient * ( center.x - chord_x )) + chord_y ;
        }

      }
      /* else both coordinates where calculated from special case chords i.e.
       * chords where at right angles.
       */
    }

    /* Now have the center of arc. Calculate vectors to the start, inter-
     * mediate and end points raltive to the center.
     * Derive the angle between start and end vectors. From this angle
     * the sweep angle is calculated - depending on which path has to be
     * taken to go start to end, via intermediate point.
     */

    c_start = local_start;
    c_end = local_end;
    c_inter = local_inter;

    /* get vectors wrt to center of arc. */
    c_start.x -= center.x;
    c_start.y -= center.y;
    c_end.x -= center.x;
    c_end.y -= center.y;
    c_inter.x -= center.x;
    c_inter.y -= center.y;

    sweep = angle_between_vectors(&c_start, &c_end);
    sweep *= RAD_TO_DEG;
    sweep_to_inter = angle_between_vectors(&c_start, &c_inter);
    sweep_to_inter *= RAD_TO_DEG;

    if ( sweep >= 0 )
      complement_angle =  (sweep_to_inter < 0) || (sweep_to_inter > sweep) ;
    else
      complement_angle =  (sweep_to_inter >= 0) || (sweep_to_inter < sweep) ;

    if ( complement_angle ) {

      HQASSERT( fabs(sweep) <= 360, "Sweep angle weird");

      if ( sweep > 0.0 )
        sweep = -(360.0 - sweep);
      else
        sweep = 360.0 + sweep;
    }

  }

  /* If we detect colinear points at any stage, we'll bail out this point. */
do_drawing:

  if ( colinear ) {

    HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
    uint16 symbol_mode_char;
    Bool result = TRUE;


    /** \todo Still need to account for the two line cases, i.e inter point
     * not between the start and end. For time being, just do line start to end.
     */

    symbol_mode_char = line_info->symbol_mode_char;

    line_info->symbol_mode_char = NUL;

    /* lineto does not do scaling itself. */
    /* plot size scaling is accounted for in this transform as well as user
     * units. */
    if ( ! job_point_to_plotter_point(pcl5_ctxt, &local_end, &local_end,
                                        relative_plot) ) {
      HQFAIL("Failed to convert user point to plotter point");
      result = FALSE;
    }

    /* draw from the current point */
    if ( result ) {

      hpgl2_lineto(pcl5_ctxt, &local_end);

      if ( stroked ) {
        SYSTEMVALUE curr_point[2];
        Bool got_curr_point;
        /* If we are doing stroke, cannot be in polygon mode.
         * Cannot use hpgl2_moveto as that uses the abs/rel plot
         * mode of the pcl5_ctxt, so would fail for e.g. RT
         */

        HQASSERT( !hpgl2_in_polygon_mode(pcl5_ctxt),
                  "Cannot stroke path in polygon mode");

        got_curr_point = gs_currentpoint(&gstateptr->thepath,
                                         &curr_point[0], &curr_point[1] );

        HQASSERT( got_curr_point, "No current point in drawing three point arc");

        if ( got_curr_point ) {
          hpgl2_stroke(pcl5_ctxt, TRUE, TRUE, TRUE);
          /* preserve current point */
          gs_moveto(TRUE, curr_point, &gstateptr->thepath );
          result = TRUE;
        }
        else
          result = FALSE;
      }
    }

    line_info->symbol_mode_char = symbol_mode_char;
    return result;

  }
  else
    /* The current point will be updated by the curve drawing. */
    return draw_arc(pcl5_ctxt, &center, chord_ang, sweep,
                    closed, stroked, TRUE, HPGL2_CHORD_COUNT_ROUND_ARC);
}

/* Interpretation of current point depends on whether we are drawing to
 * polygon buffer or drawing directly.
 */
Bool hpgl2_currentpoint(PCL5Context *pcl5_ctxt,
                        SYSTEMVALUE *x, SYSTEMVALUE *y)
{
  if ( hpgl2_in_polygon_mode(pcl5_ctxt) )
    return hpgl2_polygon_current_point(get_hpgl2_polygon_buffer(pcl5_ctxt),
                                       x, y);
  else
    return gs_currentpoint(&gstateptr->thepath, x, y);
}

Bool hpgl2_setgray(USERVALUE gray_val)
{
  return set_shade(gray_val);
}

/* ----------- HP GL vector operations. -------------- */

Bool hpgl2op_AA(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2Real sweep, chord_ang = 5.0;
  HPGL2Point center;
  HPGL2VectorInfo *vector_info = NULL;
  uint8 terminator;
  uint8 curr_plot_mode;
  Bool stroked;

  HQASSERT(pcl5_ctxt != NULL, "PCL5 Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  stroked = ! hpgl2_in_polygon_mode(pcl5_ctxt);

  /* parse arguments: center coords, sweep, chordangle [optional] */

  if ( hpgl2_scan_point(pcl5_ctxt, &center ) <= 0
       || hpgl2_scan_separator(pcl5_ctxt) <= 0
       || hpgl2_scan_real(pcl5_ctxt, &sweep) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
    if ( hpgl2_scan_real(pcl5_ctxt, &chord_ang) <= 0 )
      return TRUE; /* syntax error */

    chord_ang = clamp_hpgl2real(chord_ang, (HPGL2Real)0.5, (HPGL2Real)180);
  }

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) < 0 )
    return TRUE; /* syntax error */

  if ( (curr_plot_mode = vector_info->plot_mode ) != HPGL2_PLOT_ABSOLUTE )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);

  /* draw our arc. */
  draw_arc(pcl5_ctxt, &center, chord_ang, sweep,
           FALSE, stroked, TRUE, HPGL2_CHORD_COUNT_ROUND_ARC );

  if ( curr_plot_mode != HPGL2_PLOT_ABSOLUTE )
    hpgl2_set_plot_mode(pcl5_ctxt, curr_plot_mode);

  /* Update the carriage return point to the current pen location. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  return TRUE ;
}

Bool hpgl2op_AR(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = NULL;
  HPGL2Real sweep, chord_ang = 5.0;
  HPGL2Point center;
  HPGL2VectorInfo *vector_info = NULL;
  uint8 terminator;
  uint8 plot_mode;
  Bool stroked;

  HQASSERT(pcl5_ctxt != NULL, "PCL5 Context is NULL");

  print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  stroked = ! hpgl2_in_polygon_mode(pcl5_ctxt);

  /* parse arguments: center coords, sweep, chordangle [optional] */

  if ( hpgl2_scan_point(pcl5_ctxt, &center ) <= 0
       || hpgl2_scan_separator(pcl5_ctxt) <= 0
       || hpgl2_scan_real(pcl5_ctxt, &sweep) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
    if ( hpgl2_scan_real(pcl5_ctxt, &chord_ang) <= 0 )
      return TRUE; /* syntax error */

    chord_ang = clamp_hpgl2real(chord_ang, (HPGL2Real)0.5, (HPGL2Real)180);
  }

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) < 0 )
    return TRUE; /* syntax error */

  if ( (plot_mode = vector_info->plot_mode ) != HPGL2_PLOT_RELATIVE )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE);

  /* draw our arc. */
  draw_arc(pcl5_ctxt, &center, chord_ang, sweep,
           FALSE, stroked, TRUE, HPGL2_CHORD_COUNT_ROUND_ARC );

  if ( plot_mode != HPGL2_PLOT_RELATIVE )
    hpgl2_set_plot_mode(pcl5_ctxt, plot_mode);

  /* Update the carriage return point to the current pen location. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  return TRUE ;
}

/* draw arc with the current pen, pen state, line type, line attributes */
Bool hpgl2op_AT(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = NULL;
  HPGL2Point inter,end;
  HPGL2Real chord_ang = 5.0;
  Bool result = FALSE;
  HPGL2VectorInfo *vector_info = NULL;
  uint8 curr_plot_mode;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  curr_plot_mode = vector_info->plot_mode;

  if ( hpgl2_scan_point(pcl5_ctxt, &inter) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_point(pcl5_ctxt, &end) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
    if ( hpgl2_scan_real(pcl5_ctxt, &chord_ang) <= 0 )
      return TRUE; /* syntax error */

    chord_ang = clamp_hpgl2real(chord_ang, (HPGL2Real)0.5, (HPGL2Real)180);
  }

  if (curr_plot_mode != HPGL2_PLOT_ABSOLUTE)
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);

  result = draw_3_point_arc(pcl5_ctxt, &inter, &end, chord_ang);

  if ( curr_plot_mode != HPGL2_PLOT_ABSOLUTE)
    hpgl2_set_plot_mode(pcl5_ctxt, curr_plot_mode);

  print_state = get_hpgl2_print_state(pcl5_ctxt) ;

  /* Update the carriage return point to the current pen location. There
   * should always be a current point. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  return result ;
}

static Bool internal_HPGL2_Bezier(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state;
  HPGL2VectorInfo *vector_info;
  SYSTEMVALUE curr_point[2];
  uint32 curve_count = 0;
  HPGL2Point p1, p2, p3;
  uint8 terminator;
  Bool relative;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  vector_info = get_hpgl2_vector_info(pcl5_ctxt) ;
  relative = vector_info->plot_mode == HPGL2_PLOT_RELATIVE;

  /* We must have a current point on entry - that is an
   * invariant of HPGL2 code.
   */

  /* add curve segments one at a time, as we parse the control points
   * from the hpgl2 source. Each Bezier is treated separately; a syntax error
   * for one does not remove any of the previously specified curves in the
   * command.
   */

  do {
    if ( hpgl2_scan_point(pcl5_ctxt, &p1) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_point(pcl5_ctxt, &p2) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_point(pcl5_ctxt, &p3) <= 0 ) {
      /* syntax error. Just carry on, stroke what we have. */
      break;
    }


    /* hpgl2_curveto, just like hpgl2_lineto etc. does not deal with scaled
     * points. We have to do that here.
     * plotsize scaling done via the user point transform.
     */
    {
      Bool conv_res;
      conv_res = job_point_to_plotter_point(pcl5_ctxt, &p1, &p1, relative)
              && job_point_to_plotter_point(pcl5_ctxt, &p2, &p2, relative)
              && job_point_to_plotter_point(pcl5_ctxt, &p3, &p3, relative);

      /* Conversion of user to plotter point should always succeed, but even
       * if it fails, should not stop consumming HPGL.*/
      HQASSERT(conv_res, "Failed to convert Bezier points");
      if ( !conv_res )
        return TRUE;
    }

    if ( !hpgl2_curveto(pcl5_ctxt, &p1, &p2, &p3) ) {
      /* Should always have a currentpoint on entry to the function, so the
       * curve should be drawable. If it isn't drawable, then error is
       * non-recoverable.
       *
       * On error, keep consumming HPGL2.
       *
       */
      return TRUE;
    }

    curve_count++;

  } while ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) ;

  /* eat the terminator, if it exists */
  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  if ( !hpgl2_in_polygon_mode(pcl5_ctxt) && curve_count ) {
    /* stroke the path that we were able to parse.
     * Don't stroke a bezier when drawing into the polygon buffer.
     */
    if ( !gs_currentpoint(&gstateptr->thepath,
                          &curr_point[0], &curr_point[1]) ) {
      HQFAIL("No current point after drawing Bezier!");
      return FALSE;
    }

    if ( !hpgl2_stroke(pcl5_ctxt, TRUE, TRUE, TRUE) ) {
      return FALSE;
    }

    /* restore the current point at end of the bezier */
    if ( ! gs_moveto(TRUE, curr_point, &gstateptr->thepath) ) {
      HQFAIL("Cannot set current point at end of Bezier");
      return FALSE;
    }

    print_state->Carriage_Return_point.x = curr_point[0];
    print_state->Carriage_Return_point.y = curr_point[1];
  }

  /* In future, in error cases, we could take some remedial action to
   * at least ensure that the current path is back to a single current
   * point (if not in polygon buffer).
   */

  return TRUE;
}

Bool hpgl2op_BR(PCL5Context *pcl5_ctxt)
{
  HPGL2VectorInfo *vector_info;
  uint8 curr_plot_mode;
  Bool result = FALSE;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  curr_plot_mode = vector_info->plot_mode;
  if ( (curr_plot_mode = vector_info->plot_mode) != HPGL2_PLOT_RELATIVE )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE);

  result = internal_HPGL2_Bezier(pcl5_ctxt);

  if ( curr_plot_mode != HPGL2_PLOT_RELATIVE )
    hpgl2_set_plot_mode(pcl5_ctxt, curr_plot_mode);

  return result;
}


Bool hpgl2op_BZ(PCL5Context *pcl5_ctxt)
{
  HPGL2VectorInfo *vector_info;
  uint8 curr_plot_mode;
  Bool result = FALSE;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  curr_plot_mode = vector_info->plot_mode;
  if ( (curr_plot_mode = vector_info->plot_mode) != HPGL2_PLOT_ABSOLUTE )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);

  result = internal_HPGL2_Bezier(pcl5_ctxt);

  if ( curr_plot_mode != HPGL2_PLOT_ABSOLUTE )
    hpgl2_set_plot_mode(pcl5_ctxt, curr_plot_mode);

  return result;
}

Bool hpgl2op_CI(PCL5Context *pcl5_ctxt)
{
  Bool            result = FALSE;
  HPGL2Real       radius,
                  chord_ang = 5;
  HPGL2VectorInfo *vector_info;
  HPGL2ConfigInfo *config_info;
  HPGL2Point      center,
                  current_point,
                  circle_point;
  int8            pen_state,
                  plot_mode;
  uint8           terminator;
  Bool            stroked;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  stroked = ! hpgl2_in_polygon_mode(pcl5_ctxt);

  /* Read radius and chord angle. */
  if ( hpgl2_scan_real(pcl5_ctxt, &radius) < 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
    if ( hpgl2_scan_real(pcl5_ctxt, &chord_ang) < 0 )
      return TRUE; /* syntax error */
  }

  chord_ang = clamp_hpgl2real(chord_ang, (HPGL2Real)0.5, (HPGL2Real)180);

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) < 0 )
    return TRUE; /* syntax error */

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  config_info = get_hpgl2_config_info(pcl5_ctxt);

  /* rotation won't affect the radius drawn. */

  /* Current assumption is that each HPGL2 drawing operator defines and
   * strokes its self contained path. There is always a current point in the
   * HPGL2 state, but no other currentpath information persists outside an
   * HPGL2 operator.
   */


  HQASSERT( vector_info->pen_state == HPGL2_PEN_UP
           || vector_info->pen_state == HPGL2_PEN_DOWN,
           "Illegal pen state" );

  /* Set up drawing environment.
   * Circle mode forces absolute coordinates. Implicit pen down. */
  if ( (plot_mode = vector_info->plot_mode) != HPGL2_PLOT_ABSOLUTE )
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE);

  if ( (pen_state = vector_info->pen_state) != HPGL2_PEN_DOWN )
    hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN);

  /* in polygon mode, a circle is a complete subpath in its own right. The
     closepath might affect where the circle is drawn. */
  if (hpgl2_in_polygon_mode(pcl5_ctxt) )
    hpgl2_close_polygon_buffer_subpath(pcl5_ctxt);

  /* CI can be invoked in polygon mode, or direct drawing mode. */
  if ( ! hpgl2_currentpoint(pcl5_ctxt, &center.x, &center.y) )
    goto bail_out;

  current_point = center ;

  /* plot size scaling and user units accounted for in same function. */
  plotter_point_to_job_point(pcl5_ctxt, &center, &center, FALSE);

  /* specify absolute point for the radius */
  circle_point = center;
  circle_point.x = center.x + radius;

  /* hpgl2_moveto etc. DO NOT deal with scaling themselves */
  /* plot size scaling is handled in the user point transform. */
  job_point_to_plotter_point(pcl5_ctxt,
                             &circle_point,
                             &circle_point,
                             FALSE);

  /* move to start point on the arc; absolute plotting is enforced. */
  if ( ! hpgl2_moveto(pcl5_ctxt, &circle_point) ) {
    HQFAIL("Cannot move to start point");
    goto bail_out;
  }

  /* circle is a complete subpath of its own. */
  if ( !draw_arc(pcl5_ctxt, &center, chord_ang, 360.0, TRUE,
                 stroked, FALSE, HPGL2_CHORD_COUNT_ROUND_CIRCLE) )
    goto bail_out;

  /* restore old currentpoint. Restore while we are plotting
     absolute coordinates. */
  if ( ! hpgl2_moveto(pcl5_ctxt, &current_point) )
    goto bail_out;

  if ( plot_mode != HPGL2_PLOT_ABSOLUTE )
    hpgl2_set_plot_mode(pcl5_ctxt, plot_mode);

  if ( pen_state != HPGL2_PEN_DOWN )
    hpgl2_set_pen_mode(pcl5_ctxt, pen_state);

  result = TRUE;

bail_out:

  /* In future, should we attempt to reset the current point in event of
   * an error? Or the plot mode etc.?  */
  return result;
}


Bool hpgl2op_PA(PCL5Context *pcl5_ctxt)
{
  hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_ABSOLUTE) ;
  hpgl2_set_lost_mode(pcl5_ctxt, FALSE);

  if ( plot_points(pcl5_ctxt) == -1 )
    return FALSE ;

  return TRUE ;
}

/* This function draws a dot for the PD; operator. It has to be separate
 * from hpgl2op_PD because the decision as to whether to draw a dot depends
 * on the operator subsquent to PD;
 */
/**
 * \todo @@@ TODO FIXME
 * The HPGL interpreter really should handle this via path construction and
 * flushing operations.
 */
Bool draw_PD_dot(PCL5Context *pcl5_ctxt)
{
  /* PCL COMPATIBILITY
   * PD; cannot be done with a single moveto without extending the
   * stroking code. A single point would only be rendered if the line
   * caps are round.
   * On the HP4700reference printer, this dot will NOT cause a symbol
   * character to be printed.
   *
   * The dot does not move the current point and should not introduce
   * any symbol characters. This is substantially different to the
   * normal HPGL line drawing that we can justify doing it all inline
   * here.
   * The dot is a one plotter unit long line.
   */
  HPGL2Point dot_point = {1.0, 0} ;
  uint16 saved_symbol_char ;
  int32 saved_line_type ;
  Bool result = TRUE ;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  SYSTEMVALUE orig_point[2];
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

  if (!print_state->dot_candidate) {
    HQFAIL("Drawing PD dot when there is no candidate position.");
    return FALSE ;
  }

  if (hpgl2_in_polygon_mode(pcl5_ctxt)) {
    HQFAIL("Must not call draw_PD_dot in polygon mode.");
    return FALSE;
  }

  if (!gs_currentpoint(&gstateptr->thepath,
                       &orig_point[0],
                       &orig_point[1])) {
    HQFAIL("No currentpoint") ;
    return FALSE ;
  }

    HQASSERT(print_state->SM_points == NULL,
              "Should not have any symbol points at PD;");

  /* In HPGL, the gstate is set up to reflect the plotter unit
   * space, including rotation. We can simply draw the line for
   * the dot in this space.
   */

  /* Temporarily disable the symbol processing. */
  saved_symbol_char = linefill_info->symbol_mode_char;
  linefill_info->symbol_mode_char = NUL;

  /* dots need to be done in solid line, but don't disturb the
   * line residue. */
  saved_line_type = linefill_info->line_type.type ;
  linefill_info->line_type.type = HPGL2_LINETYPE_SOLID ;

  if (vector_info->plot_mode == HPGL2_PLOT_ABSOLUTE) {
    dot_point.x += orig_point[0] ;
    dot_point.y += orig_point[1] ;
  }

  /* We can short cut some of the management functions of line
   * drawing as at this point, we know polygon mode is not active,
   * the line type is solid, the line dash should not be updated,
   * there is no symbol mode required, we need to restore the
   * current point that applied before the drawing of the single
   * plotter unit line and we need to preserve the line residue.
   */
  /**
   * \todo @@@ TODO Replace PD; handling with a stroker flag to allow
   * stroking of points without round line caps.
   */

  if (!hpgl2_lineto(pcl5_ctxt, &dot_point)
      || !hpgl2_stroke(pcl5_ctxt, FALSE, TRUE, TRUE))
    result = FALSE;

  /* restore the currentpoint before the 'dot' even if the dot
   * failed. */
  if ( !gs_moveto(TRUE, orig_point, &gstateptr->thepath) ) {
    HQFAIL("Cannot restore current point after PD;");
    result = FALSE ;
  }

  /* restore state */
  linefill_info->symbol_mode_char = saved_symbol_char ;
  linefill_info->line_type.type = saved_line_type ;
  return result;
}

Bool hpgl2op_PD(PCL5Context *pcl5_ctxt)
{
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  HPGL2Point curr_point;
  Bool pen_state_changed = FALSE ;

  curr_point.x = 0;
  curr_point.y = 0;

  pen_state_changed = vector_info->pen_state == HPGL2_PEN_UP ;

  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_DOWN) ;

  if ( hpgl2_in_lost_mode(pcl5_ctxt) ) {
    if (vector_info->plot_mode != HPGL2_PLOT_ABSOLUTE ) {
      hpgl2op_nullop(pcl5_ctxt);
      return TRUE;
    }
    else
      hpgl2_set_lost_mode(pcl5_ctxt, FALSE);
  }

  switch (plot_points(pcl5_ctxt)) {
    case -1:  /* error in plotting points */
      return FALSE;

    case 0:
      /* zero points plotted. This is due a PD; operator. Record that we
       * encountered a PD; - it may be expanded to a dot at the next operator
       */

      if ( !hpgl2_in_polygon_mode(pcl5_ctxt)
            && pen_state_changed
            && pen_is_selected(pcl5_ctxt) ) {
        HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);
        print_state->dot_candidate = TRUE ;
      }

      break;

    default:
      break;
  }

#if defined (ASSERT_BUILD)
  {
    HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

    HQASSERT(!print_state->path_to_draw,
            "Should not have a path to draw after PD operator");
  }
#endif  /* ASSERT_BUILD */

  return TRUE ;
}

/* -- polyline parsing issues. -- */


/* Array for byte classification checks when parsing polyline encoded data.
 * The valid digit char ranges don't fit complete bit ranges so can not easily
 * use bit twiddling for valid/invalid tests based on the current base.
 *
 * To simplify handling flags, the classification bit is set for the 7 and 8 bit
 * versions.
 */
#define PE_TERMINATOR         (0x01)    /* The normal HP-GL/2 ';' operator terminator. */
#define PE_FLAG               (0x02)    /* One of the PE flags - 7, <, =, and > */
#define PE_BASE32_DIGIT       (0x04)    /* Normal 7 bit value digit */
#define PE_BASE32_TERMINATOR  (0x08)    /* Terminating 7 bit value digit. */
#define PE_BASE64_DIGIT       (0x10)    /* Normal 8 bit value digit. */
#define PE_BASE64_TERMINATOR  (0x20)    /* Terminating 8 bit value digit. */

static
uint8 pe_chars[256] = {
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  PE_FLAG,        /* 0x37 - 7 */
  0,
  0,
  0,
  PE_TERMINATOR,  /* ; is not a flag but a normal HP-GL/2 command separator */
  PE_FLAG,        /* 0x3c - < */
  PE_FLAG,        /* 0x3d - = */
  PE_FLAG,        /* 0x3e - > */
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,  /* 63 */
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,  /* 95 */
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT,
  PE_BASE32_DIGIT|PE_BASE32_TERMINATOR|PE_BASE64_DIGIT, /* 126 */
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  PE_FLAG,    /* 0xb7 - 7 */
  0,
  0,
  0,
  0,          /* ; is not a flag so is not marked as such when top bit set */
  PE_FLAG,    /* 0xbc - < */
  PE_FLAG,    /* 0xbd - = */
  PE_FLAG,    /* 0xbe - > */
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,   /* 191 */
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,
  PE_BASE64_DIGIT|PE_BASE64_TERMINATOR,   /* 254 */
  0
};

#define isvalueterminator(c)      ((pe_chars[c]&(PE_FLAG|PE_TERMINATOR)) != 0)
#define isbasedigit(c, b)         ((pe_chars[c]&b) == b)
#define isterminatingdigit(c, b)  ((pe_chars[c]&b) == b)

/** Value parsing state. */
typedef struct PE_STATE {
  FILELIST* flptr;                  /**< PCL stream to parse. */
  int32     next_byte;              /**< Next byte to parse. */
  int32     base_digit;             /**< Mask for testing value digit for the current base. */
  int32     base_terminator;        /**< Mask for testing terminating value digit for the current base. */
  int32     base_shift;             /**< Bit shift per value digit for base, 5 or 6. */
  int32     base_mask;              /**< Mask to remove terminating digit flag. */
  int32     mask;                   /**< Mask for treating digits as 8 or 7 bit values. */
} PE_STATE;

static
void PE_skip_to_terminator(
  PE_STATE* pe_state,
  int32     ch)
{
  FILELIST* flptr;
  int32 mask;
  int32 base_terminator;

  mask = pe_state->mask;
  base_terminator = pe_state->base_terminator;
  flptr = pe_state->flptr;

  while ( (ch != EOF) &&
          !(isvalueterminator(ch) || isterminatingdigit((ch & mask), base_terminator)) ) {
    ch = Getc(flptr);
  }
  pe_state->next_byte = ch;

} /* PE_skip_to_terminator */

/* Parse a single number according to current base.
 *
 * Polyline encoded values are series of bytes that encode 5 or 6 bits of the
 * integer value in little endian order.  The last byte of a value includes an
 * offset to indicate that it is the last byte of data for the value.  The bits
 * are offset by 63 to make them printable characters and not overlap with PE
 * flag bytes (see below).
 *
 * Converting a byte to the next set of bits is simple:
 * 1. Remove the offset of 63 - shifts value to be 0 based.
 * 2. Remove termination bit by masking the low 5/6 bits.
 * 3. Shift into position for the final value - start at 0 and then shift by 5/6
 *    bits per byte.
 * 4. The final value is the generated value shifted right one bit with the sign
 *    based on the original bit 0.
 */
/* A value that cannot be encoded in a polyline */
#define PE_VALUE_LOST MININT32

static
Bool PE_parse_value(
  PE_STATE* pe_state,
  int32*    p_number)
{
  FILELIST* flptr;
  int32 ch;
  int32 base_digit;
  int32 base_terminator;
  int32 base_shift;
  int32 base_mask;
  int32 mask;
  int32 shift;
  int32 sign;
  uint32 accumulator;
  Bool last_digit;

  base_digit = pe_state->base_digit;
  base_terminator = pe_state->base_terminator;
  base_shift = pe_state->base_shift;
  base_mask = pe_state->base_mask;
  mask = pe_state->mask;
  flptr = pe_state->flptr;

  accumulator = 0;
  shift = 0;
  ch = pe_state->next_byte;
  do {
    /* Check we have a valid byte for the current base and if it is a
     * terminating byte.
     */
    if ( (ch == EOF) || !isbasedigit((ch & mask), base_digit)) {
      PE_skip_to_terminator(pe_state, ch);
      return(FALSE);
    }
    last_digit = isterminatingdigit((ch & mask), base_terminator);

    /* Add byte value to accumulator as long as there is space. */
    ch = (ch - 63)&base_mask;
    if ( highest_bit_set_in_byte[ch] > (32 - shift) ) {
      HQFAIL("PE value overflow - please report to core group");
      PE_skip_to_terminator(pe_state, ch);
      *p_number = PE_VALUE_LOST;
      return(TRUE);
    }
    accumulator |= (((uint32)ch) << shift);
    shift += base_shift;
    ch = Getc(flptr);
  } while ( !last_digit );

  /* Note last byte read */
  pe_state->next_byte = ch;

  /* Sign of the value is in the lsb of the accumulated value.  Extract it and
   * apply to value using branchless code.
   */
  sign = -(int32)(accumulator & 1);
  *p_number = ((accumulator >> 1) ^ sign) - sign;

  return(TRUE);

} /* PE_parse_value */

static inline
void PE_value_to_real(
  int32 number,
  int32 fraction_bits,
  HPGL2Real* real)
{
  /* Apply any fractional digits scaling */
  if ( fraction_bits == 0 ) {
    *real = (HPGL2Real)number;

  } else if ( fraction_bits < 0 ) {
    *real = (HPGL2Real)number * (1 << -fraction_bits);

  } else if ( fraction_bits > 0 ) {
    *real = (HPGL2Real)number / (1 << fraction_bits);
  }

} /* PE_value_to_real */


#define PARSE_OK    (0)
#define PARSE_ERROR (-1)
#define PARSE_END   (1)

static
Bool PE_parse(
  PCL5Context *pcl5_ctxt)
{
  PE_STATE  pe_state;
  FILELIST* flptr;
  HPGL2VectorInfo* vector_info;
  HPGL2PrintState* print_state;
  HPGL2Point coord;
  int32 fraction_bits;
  int32 plot_relative;
  int32 pen_down;
  int32 pen;
  int32 state;
  int32 x, y;
  Bool lost_point;

  /* Magic numbers used parsing numbers - should be setup in PE state and then
   * just plucked out here, one less test-branch-jump.
   * Digit mask is based on the initial initial subtraction of 63 then whether
   * the bit is set to indicate it was a terminating value.
   */
  pe_state.flptr = pcl5_ctxt->flptr;
  pe_state.base_digit = PE_BASE64_DIGIT;
  pe_state.base_terminator = PE_BASE64_TERMINATOR;
  pe_state.base_shift = 6;
  pe_state.base_mask = 0x3f;
  pe_state.mask = 0xff;

  fraction_bits = 0;

  /* Default PE drawing state. */
  plot_relative = TRUE;
  pen_down = TRUE;

  /* Pick up current lost mode */
  print_state = get_hpgl2_print_state(pcl5_ctxt);
  lost_point = print_state->lost;

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  flptr = pcl5_ctxt->flptr;

  pe_state.next_byte = pcl5_ctxt->last_char;
  state = PARSE_OK;
  while ( state == PARSE_OK )  {
    if ( pe_state.next_byte == EOF ) {
      state = PARSE_ERROR;
      break;
    }
    if ( pe_state.next_byte == ';' ) {
      /* Normal HP-GL/2 command terminator, not a flag. */
      if ( draw_path(pcl5_ctxt) ) {
        finalize_plot_path(pcl5_ctxt);
      }
      state = PARSE_END;
      break;
    }

    /* Flags are detected on low 7 bits only. */
    switch ( pe_state.next_byte & 0x7f ) {
    case '7': /* Switch to 7 bit mode */
      pe_state.base_digit = PE_BASE32_DIGIT;
      pe_state.base_terminator = PE_BASE32_TERMINATOR;
      pe_state.base_shift = 5;
      pe_state.base_mask = 0x1f;
      pe_state.mask = 0x7f;

      pe_state.next_byte = Getc(flptr);
      break;

    case ':': /* Select pen - no effect in polygon mode */
      pe_state.next_byte = Getc(flptr);
      if ( !PE_parse_value(&pe_state, &pen) ) {
        state = PARSE_ERROR;
        continue;
      }
      HQASSERT((pen != PE_VALUE_LOST),
               "pen number overflow");
      if ( !hpgl2_in_polygon_mode(pcl5_ctxt) ) {
        /* Changing pen implies ending the current line, flushing it to output. */
        if ( !draw_path(pcl5_ctxt) ) {
          state = PARSE_ERROR;
          continue;
        }
        finalize_plot_path(pcl5_ctxt);
        hpgl2_set_current_pen(pcl5_ctxt, pen, FALSE);
      }
      break;

    case '<': /* Pen up */
      pen_down = FALSE;
      vector_info->pen_state = HPGL2_PEN_UP;
      pe_state.next_byte = Getc(flptr);
      break;

    case '=': /* Absolute coordinates */
      plot_relative = FALSE;
      vector_info->plot_mode = HPGL2_PLOT_ABSOLUTE;
      lost_point = FALSE;
      print_state->lost = FALSE;
      pe_state.next_byte = Getc(flptr);
      break;

    case '>': /* Fractional data */
      pe_state.next_byte = Getc(flptr);
      if ( !PE_parse_value(&pe_state, &fraction_bits) ) {
        state = PARSE_ERROR;
        continue;
      }
      HQASSERT((abs(fraction_bits) <= 26),
               "fraction bit count too large");
      break;

    default:
      /* Parse X and Y values and scale for fractional part */
      x = y = 0;
      if ( !(PE_parse_value(&pe_state, &x) && PE_parse_value(&pe_state, &y)) ) {
        state = PARSE_ERROR;
        continue;
      }

      if ( !lost_point && (x != PE_VALUE_LOST && y != PE_VALUE_LOST) ) {
        PE_value_to_real(x, fraction_bits, &coord.x);
        PE_value_to_real(y, fraction_bits, &coord.y);
      } else {
        HQFAIL("Failed to parse coordinate pair data.");
        lost_point = TRUE;
        print_state->lost = TRUE;
      }

      /* hpgl2_lineto draws only in plotter coordinates so scale manually.
       * Treat failure to calculate plotter coords as being lost.
       */
      if ( !lost_point ) {
        /* plot size scaling and user units accounted for in same function. */
        if ( !job_point_to_plotter_point(pcl5_ctxt, &coord, &coord, plot_relative) ) {
          HQFAIL("Cannot calculate plotter coords");
          print_state->lost = TRUE;
          lost_point = TRUE;
        }
      }

      if ( !lost_point ) {
        /* In lost mode, don't changes the state of the pen.
         * Flag handlers will have set pen up, or absolute plot mode if
         * necessary. Otherwise, set default state here. This allows a pen up
         * on last point to persist after PE.
         */
        /**
         * \todo @@@ TODO : Is failure to draw to be considered as causing entry
         * to LOST mode.
         * \todo TODO - only set plot and pen mode if need to!
         */
        if ( plot_relative ) {
          vector_info->plot_mode = HPGL2_PLOT_RELATIVE;
        }
        if ( pen_down ) {
          vector_info->pen_state = HPGL2_PEN_DOWN;
        }
        if ( !hpgl2_lineto(pcl5_ctxt, &coord) ) {
          HQFAIL("Could not draw PE line");
          state = PARSE_ERROR;
          continue;
        }
      }
      /* Default PE drawing state. */
      plot_relative = TRUE;
      pen_down = TRUE;
      break;
    }
  }

  pcl5_ctxt->last_char = pe_state.next_byte;

  return(TRUE);

} /* PE_parse */

/* expect a stream of : (((flag | (flag value))* coord-pair)*
 * Each coord pair will be plotted pen down, relative unless the flag
 * indicates otherwise.
 */
Bool hpgl2op_PE(
  PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = NULL;
  HPGL2VectorInfo *vector_info = NULL;
  uint8 plot_mode;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  vector_info = get_hpgl2_vector_info(pcl5_ctxt) ;
  plot_mode = vector_info->plot_mode;

  hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE);

  PE_parse(pcl5_ctxt);

  /* restore initial plot mode. */
  hpgl2_set_plot_mode(pcl5_ctxt, plot_mode);

  /* Update the carriage return point to the current pen location. */
  return(gs_currentpoint(&gstateptr->thepath,
                         &print_state->Carriage_Return_point.x,
                         &print_state->Carriage_Return_point.y));
}

Bool hpgl2op_PR(PCL5Context *pcl5_ctxt)
{
  hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE) ;

  if ( plot_points(pcl5_ctxt) == -1)
    return FALSE ;

  return TRUE ;
}

Bool hpgl2op_PU(PCL5Context *pcl5_ctxt)
{
  HPGL2VectorInfo *vector_info = get_hpgl2_vector_info(pcl5_ctxt);

  hpgl2_set_pen_mode(pcl5_ctxt, HPGL2_PEN_UP) ;

  if ( hpgl2_in_lost_mode(pcl5_ctxt) ) {
    if (vector_info->plot_mode != HPGL2_PLOT_ABSOLUTE ) {
      return hpgl2op_nullop(pcl5_ctxt);
    }
    else
      hpgl2_set_lost_mode(pcl5_ctxt, FALSE);
  }

  if ( plot_points(pcl5_ctxt) == -1)
    return FALSE ;

  /* PCL COMPATIBILITY. The reference printer will reset residue on each
   * PU operation (not in polygon mode ).
   */
  if ( !hpgl2_in_polygon_mode(pcl5_ctxt) ) {
    HPGL2LineFillInfo * line_info = get_hpgl2_line_fill_info(pcl5_ctxt);

    line_info->line_type.residue = 0;
  }


  return TRUE ;
}

Bool hpgl2op_RT(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = NULL;
  HPGL2Point inter,end;
  HPGL2Real chord_ang = 5.0;
  Bool result = FALSE;
  HPGL2VectorInfo *vector_info = NULL;
  uint8 curr_plot_mode;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  curr_plot_mode = vector_info->plot_mode;

  if ( hpgl2_scan_point(pcl5_ctxt, &inter) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_point(pcl5_ctxt, &end) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
    if ( hpgl2_scan_real(pcl5_ctxt, &chord_ang) <= 0 )
      return TRUE; /* syntax error */

    chord_ang = clamp_hpgl2real(chord_ang, (HPGL2Real)0.5, (HPGL2Real)180);
  }

  if (curr_plot_mode != HPGL2_PLOT_RELATIVE)
    hpgl2_set_plot_mode(pcl5_ctxt, HPGL2_PLOT_RELATIVE);

  result = draw_3_point_arc(pcl5_ctxt, &inter, &end, chord_ang);

  if (curr_plot_mode != HPGL2_PLOT_RELATIVE)
    hpgl2_set_plot_mode(pcl5_ctxt, curr_plot_mode);

  print_state = get_hpgl2_print_state(pcl5_ctxt) ;

  /* Update the carriage return point to the current pen location. There
   * should always be a current point. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  return result ;
}

/* ============================================================================
* Log stripped */
