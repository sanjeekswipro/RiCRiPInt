/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpath.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCLXL "path" related function
 */
#include <math.h>

#include "core.h"
#include "swcopyf.h"
#include "gu_cons.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlscan.h"
#include "pclxlcursor.h"
#include "pclxltags.h"

#include "graphics.h"
#include "gstack.h"
#include "gu_path.h"
#include "pathcons.h"
#include "clippath.h"
#include "namedef_.h"
#include "pclGstate.h"
#include "pclAttrib.h"

#ifdef DEBUG_BUILD
#include "pclxltest.h"
#endif

/*
 * Tag 0x80 SetPathToClip
 *
 * Sets the current path to be (a copy of) the current clip path
 */

Bool
pclxl_op_set_path_to_clip(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  int32 cliptype;

  /*
   * I am assuming that there will always be a current "clip" path
   * and that this will have been set up as part of the page set up
   * to be the entire area of the page.
   */

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS), ("SetPathToClip"));

  if ( !clippath_internal(get_core_context_interp(),
                          &thePathInfo(*gstateptr), &cliptype) ) {
    /** \todo Set up XL error! */
    return(FALSE);
  }

  pclxl_get_current_path_and_point(graphics_state);
  return(TRUE);
}

static Bool
pclxl_close_sub_path(PCLXL_GRAPHICS_STATE graphics_state)
{
  if ( !graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so any and all paths will also be "degenerate"
     * which in the core rip are represented as a NULL path (list)
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  if ( !graphics_state->current_path )
  {
    /*
     * We have been asked to close a sub-path when there isn't a current path.
     *
     * In Postscript this is a serious error
     * But in PCLXL a spurious CloseSubPath operator is an entirely benigh operation
     *
     * So we will produce a debug message
     * But then return TRUE anyway
     * We will actually use this as (yet another) opportunity
     * to resync our state with the current underlying Postscript RIP state
     */

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("There is no current cursor position but attempting to close a (sub-)path in PCLXL is a benign operation"));

    pclxl_get_current_path_and_point(graphics_state);

    return TRUE;
  }
  else if ( !path_close(CLOSEPATH, &thePathInfo(*gstateptr)) )
  {
    /*
     * We failed to call Postscript's "closepath"
     * A suitable error message has already been logged
     * So we simply return FALSE here
     */

    return FALSE;
  }
  else
  {
    /*
     * We have successfully closed the underlying Postscript path
     * So we must track this happy state within our own
     * PCLXL graphics state
     */

    pclxl_get_current_path_and_point(graphics_state);

    return TRUE;
  }
}

/*
 * Tag 0x84 CloseSubPath
 */

Bool
pclxl_op_close_sub_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS), ("CloseSubPath"));

  return(pclxl_close_sub_path(pclxl_context->graphics_state));
}

static Bool
pclxl_new_path(PCLXL_GRAPHICS_STATE graphics_state)
{
  /*
   * If there is a current path (or a current point?)
   * then we must call Postscript's "newpath" operator
   * And IFF successful we must also record that there is no current path
   * in our own PCLXL graphics state
   */

  if ( graphics_state->current_path && !gs_newpath() )
  {
    /*
     * We failed to call Postscript's "newpath"
     * A suitable error message has already been logged
     * So we simply return FALSE here
     */

    return FALSE;
  }
  else
  {
    /*
     * We have successfully removed any existing Postscript path
     * So we must track this happy state within our own
     * PCLXL graphics state
     */

    pclxl_get_current_path_and_point(graphics_state);

    return TRUE;
  }
}

/*
 * Tag 0x85 NewPath
 */

Bool
pclxl_op_new_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* If there is a current path (or a current point?) then we must call
   * Postscript's "newpath" operator And IFF successful we must also record that
   * there is no current path in our own PCLXL graphics state
   */

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS), ("NewPath"));

  return(pclxl_new_path(pclxl_context->graphics_state));
}

/*
 * pclxl_new_sub_path() is a subtle variant upon pclxl_[op_]new_path()
 * in that it does not necessarily result in a new path operation
 *
 * Instead, if there is already one or more paths
 * and therefore there is a current point
 * all it does is to "break" the path at this point
 * (i.e. end the previous sub-path *WITHOUT CLOSING IT*)
 *
 * It does this by forcing a "moveto" the current point
 * But it does this IFF there is a current point
 */

static Bool
pclxl_new_sub_path(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  if ( !graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so any and all paths will also be "degenerate"
     * which in the core rip are represented as a NULL path (list)
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  if ( graphics_state->current_point )
  {
    Bool moveto_result = pclxl_ps_moveto(pclxl_context,
                                         graphics_state->current_point_xy.x,
                                         graphics_state->current_point_xy.y);

    /*
     * There is still a current point
     * and it is (supposedly) exactly where it previously was
     *
     * The question is: Is there a current *path*?
     *
     * The safest thing to do is check with Postscript
     * to see what it "thinks"
     */

    pclxl_get_current_path_and_point(graphics_state);

    return moveto_result;
  }
  else
  {
    return TRUE; /* pclxl_new_path(graphics_state); */
  }
}

/*
 * pclxl_paint_path() does the bulk of the work behind pclxl_op_paint_path()
 * but it is also used behind pclxl_op_rectangle(), pclxl_op_round_rectangle()
 * and other filled-shape operators
 */

static Bool
pclxl_paint_path(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  if ( !finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return FALSE;
  }

  if ( !graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so anything that we would attempt to draw
     * will leave no marks on the paper
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  /* No need to draw anything for the destination rop */
  /** \todo we should we never draw with ROP_D */
  if (graphics_state->ROP3  == PCL_ROP_D) {
    pclxl_get_current_path_and_point(graphics_state);
    return TRUE ;
  }

  if ( !graphics_state->current_path )
  {
    /*
     * There is no current path
     * Presumably because there have been no path drawing operations
     * since either the beginning of the page or since the last
     * NewPath operator
     */

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("There is no current cursor position but attempting to paint a path in PCLXL is a benign operation"));

    pclxl_get_current_path_and_point(graphics_state);

    return TRUE;
  }

  if ( (graphics_state->fill_details.brush_source.color_array_len > 0) ) {
    /* If this fill looks like it can be optimised by the pattern blitter, but
     * is opaque (which the pattern blitter cannot handle), convert it to a
     * transparent pattern by first drawing the fill in white. */
    if ( (graphics_state->ROP3 == PCL_ROP_TSo || graphics_state->ROP3 == PCL_ROP_T) &&
         graphics_state->fill_details.brush_source.pattern_enabled &&
         graphics_state->source_tx_mode == PCLXL_eOpaque &&
         graphics_state->paint_tx_mode == PCLXL_eOpaque ) {
      setPclXLPattern(NULL, 0, NULL, NULL, NULL, NULL);
      pclxl_ps_set_shade(1);
      if (! setPclForegroundSource(pclxl_context->corecontext->page,
                                   PCL_DL_COLOR_IS_FOREGROUND))
        return FALSE;

      if (! pclxl_ps_fill(pclxl_context))
        return FALSE;

      setPclPatternTransparent(TRUE);
    }

    if (!pclxl_ps_set_color(pclxl_context,
                            &graphics_state->fill_details.brush_source, FALSE /* For an image? */) ||
        !pclxl_ps_set_rop3(pclxl_context, graphics_state->ROP3, FALSE) ||
        !pclxl_ps_fill(pclxl_context)) {
      /*
       * There was a current path and a brush source
       * but we failed to fill the area defined by the path
       *
       * A suitable error (or was it just a warning?) has been logged.
       * The question is: Do we fail this operation completely?
       * Or do we attempt to continue?
       */

      return FALSE;
    }

    /* Restore the state since we may have messed with it above. */
    pclxl_pcl_grestore(pclxl_context, graphics_state);
  }

  if ( (graphics_state->line_style.pen_source.color_array_len > 0) &&
       ((!pclxl_ps_set_color(pclxl_context,
                             &graphics_state->line_style.pen_source, FALSE /* For an image? */)) ||
        (!pclxl_ps_set_rop3(pclxl_context, graphics_state->ROP3, TRUE)) ||
        (!pclxl_ps_set_line_width(graphics_state->line_style.pen_width)) ||
        (!pclxl_ps_set_miter_limit(pclxl_context,
                                   graphics_state->line_style.miter_limit)) ||
        (!pclxl_ps_stroke(pclxl_context))) )
  {
    /*
     * We have failed to stroke the path outline
     * An error (or warning) has been logged
     * But should we fail here or attempt to continue?
     */

    return FALSE;
  }
  else
  {
    /*
     * We have successfully filled the current path (area)
     * and stroked the path outline
     */

    pclxl_get_current_path_and_point(graphics_state);

    return TRUE;
  }

  /*NOTREACHED*/
}

#ifdef DEBUG_BUILD

static uint8*
pclxl_debug_color_string(PCLXL_COLOR_DETAILS color_details,
                         uint8*              color_string_buf,
                         uint32              color_string_len,
                         uint8*              pen_or_brush)
{
  switch ( color_details->color_array_len )
  {
  case 0:

    (void) swncopyf(color_string_buf,
                    color_string_len,
                    (uint8*) "Null%s",
                    pen_or_brush);

    break;

  case 1:

    (void) swncopyf(color_string_buf,
                    color_string_len,
                    (uint8*) "Gray [ %f ]",
                    color_details->color_array[PCLXL_GRAY_CHANNEL]);

    break;

  case 3:

    (void) swncopyf(color_string_buf,
                    color_string_len,
                    (uint8*) "RGB [ %f, %f, %f ]",
                    color_details->color_array[PCLXL_RED_CHANNEL],
                    color_details->color_array[PCLXL_GREEN_CHANNEL],
                    color_details->color_array[PCLXL_BLUE_CHANNEL]);

    break;

  case 4:

    (void) swncopyf(color_string_buf,
                    color_string_len,
                    (uint8*) "CMYK [ %f, %f, %f, %f ]",
                    color_details->color_array[PCLXL_CYAN_CHANNEL],
                    color_details->color_array[PCLXL_MAGENTA_CHANNEL],
                    color_details->color_array[PCLXL_YELLOW_CHANNEL],
                    color_details->color_array[PCLXL_BLACK_CHANNEL]);

    break;

  default:

    (void) swncopyf(color_string_buf,
                    color_string_len,
                    (uint8*) "UnknownColorSpace [ array_length = %d ]",
                    color_details->color_array_len);

    break;
  }

  return color_string_buf;
}
#endif

/*
 * Tag 0x86 PaintPath
 */

Bool
pclxl_op_paint_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

#ifdef DEBUG_BUILD
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  uint8 brush_color_debug_string[64];
  uint8 pen_color_debug_string[64];
#endif

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("PaintPath(BrushSource = %s, PenSource = %s, PenWidth = %f)",
               pclxl_debug_color_string(&graphics_state->fill_details.brush_source,
                                        brush_color_debug_string,
                                        sizeof(brush_color_debug_string),
                                        (uint8*) "Brush"),
               pclxl_debug_color_string(&graphics_state->line_style.pen_source,
                                        pen_color_debug_string,
                                        sizeof(pen_color_debug_string),
                                        (uint8*) "Pen"),
               graphics_state->line_style.pen_width));

  return pclxl_paint_path(pclxl_context);
}

/*
 * pclxl_quadrant_and_proportion() takes an angle (in radians)
 * (typically as calculated using atan2())
 * and converts it into an enumeration value
 * that indicates whether the angle represents
 * which quadrant the angle points into (NE, NW, SW, SE)
 *
 * This value is then used to determine how many
 * full elliptical arc quadrants to draw
 * and how many partial elliptical arcs to draw
 *
 * The quadrant value is typically used to index an array
 * or used in a for-loop to move in either a clockwise
 * or counter-clockwise direction.
 */

enum
{
  PCLXL_NE_QUADRANT = 0,
  PCLXL_NW_QUADRANT = 1,
  PCLXL_SW_QUADRANT = 2,
  PCLXL_SE_QUADRANT = 3
};

typedef uint8 PCLXL_QUADRANT;

#ifndef PI
#define PI  (3.14159265359)
#endif

#ifdef DEBUG_BUILD
#define PCLXL_RADIANS_TO_DEGREES(RAD) ((int32) ((RAD) / (2 * PI) * 360.0))
#define PCLXL_PROPORTION_TO_PERCENTAGE(P) ((int32) ((P) * 100))
#define PCLXL_PRINTABLE_ARC_DIRECTION(AD) ((AD) == PCLXL_eClockWise ? "Clockwise" : "CounterClockwise")
#endif

static Bool
pclxl_quadrant_and_proportion(PCLXL_SysVal       radians,
                              PCLXL_ArcDirection arc_direction,
                              Bool               is_start_point,
                              PCLXL_QUADRANT*    p_quadrant,
                              PCLXL_SysVal*      p_proportion)
{
  uint8 quadrant = 0;  /* Like a compass needle
                        * but only points NE, NW, SW or SE
                        */

  /*
   * Because PCLXL works from a top-left-corner origin
   * with y-axis increasing down the page
   * But I find it most comprehensible to view "North" as being up the page
   * We are going to flip the <radians> value
   */

  PCLXL_SysVal inverted_radians = (- radians);

  while ( inverted_radians < 0.0 ) inverted_radians += (2 * PI);

  for ( quadrant = 0 ;
        inverted_radians > (PI / 2) ;
        inverted_radians -= (PI / 2), quadrant = ((quadrant + 1) % 4) );

  if ( inverted_radians == 0.0 )
  {
    if ( arc_direction == PCLXL_eCounterClockWise )
    {
      *p_quadrant = quadrant;

      *p_proportion = 0.0;
    }
    else if ( is_start_point )
    {
      *p_quadrant = ((quadrant + 3) % 4);

      *p_proportion = 1.0;
    }
    else
    {
      *p_quadrant = quadrant;

      *p_proportion = 1.0;
    }
  }
  else if ( inverted_radians == (PI / 2) )
  {
    if ( arc_direction == PCLXL_eClockWise )
    {
      *p_quadrant = quadrant;

      *p_proportion = 0.0;
    }
    else if ( is_start_point )
    {
      *p_quadrant = ((quadrant + 1) % 4);

      *p_proportion = 1.0;
    }
    else
    {
      *p_quadrant = quadrant;

      *p_proportion = 1.0;
    }
  }
  else
  {
    if ( arc_direction == PCLXL_eCounterClockWise )
    {
      *p_quadrant = quadrant;

      *p_proportion = (inverted_radians / (PI / 2));
    }
    else
    {
      *p_quadrant = quadrant;

      *p_proportion = (((PI / 2) - inverted_radians) / (PI / 2));

    }
  }

  PCLXL_DEBUG(PCLXL_DEBUG_ARC_PATH,
              ("pclxl_quadrant_and_proportion(%d (degrees)) is in quadrant %d, proportion %d%%%%",
               PCLXL_RADIANS_TO_DEGREES(radians),
               *p_quadrant,
               PCLXL_PROPORTION_TO_PERCENTAGE(*p_proportion)));

  return TRUE;
}

/*
 * pclxl_mid_point() takes two points (start and end of a line)
 * and a "proportion" and it calculates a third point that
 * is that proportion along the line linking the start to the end point.
 *
 * It returns this control point into the caller supplied location
 * but also returns this location as its return value
 * so that it can directly be used as the input to
 * second and third order calculations
 */

static PCLXL_SysVal_XY*
pclxl_mid_point(PCLXL_SysVal_XY* start_point,
                PCLXL_SysVal_XY* end_point,
                PCLXL_SysVal     proportion,
                PCLXL_SysVal_XY* mid_point)
{
  PCLXL_SysVal dx = (end_point->x - start_point->x);
  PCLXL_SysVal dy = (end_point->y - start_point->y);

  mid_point->x = (start_point->x + (dx * proportion));
  mid_point->y = (start_point->y + (dy * proportion));

  return mid_point;
}

/*
 * pclxl_trim_bezier() is passed an existing bezier curve
 * as defined by a start point, an end point and two "control" points
 *
 * It is passed a "proportion" which is a real number
 * which is assumed to be *between* 0.0 and 1.0
 *
 * It performs calculations to derive two new control points
 * and a new end point such that the resultant bezier curve
 * defined by (original) start_point, new_control_point_1,
 * new_control_point_2 and new_end_point represents the initial <proportion>
 * of the original bezier curve
 */

static PCLXL_SysVal_XY*
pclxl_trim_bezier(PCLXL_SysVal_XY* start_point,
                   PCLXL_SysVal_XY* control_point_1,
                   PCLXL_SysVal_XY* control_point_2,
                   PCLXL_SysVal_XY* end_point,
                   PCLXL_SysVal     trim_proportion,
                   PCLXL_SysVal_XY* new_control_point_1,
                   PCLXL_SysVal_XY* new_control_point_2,
                   PCLXL_SysVal_XY* new_end_point)
{
  PCLXL_SysVal_XY a,b,c,d,e,f;

  HQASSERT(((trim_proportion >= 0.0) && (trim_proportion <= 1.0)),
           "Bezier Trim \"Proportion\" must be between 0.0 to 1.0 (inclusive)");

  (void) pclxl_mid_point(start_point,
                         control_point_1,
                         trim_proportion,
                         &a);

  (void) pclxl_mid_point(control_point_1,
                         control_point_2,
                         trim_proportion,
                         &b);

  (void) pclxl_mid_point(control_point_2,
                         end_point,
                         trim_proportion,
                         &c);

  (void) pclxl_mid_point(&a,
                         &b,
                         trim_proportion,
                         &d);

  (void) pclxl_mid_point(&b,
                         &c,
                         trim_proportion,
                         &e);

  (void) pclxl_mid_point(&d,
                         &e,
                         trim_proportion,
                         &f);

  *new_control_point_1 = a;

  *new_control_point_2 = d;

  *new_end_point = f;

  return new_end_point;
}

/*
 * CIRCLE_FACTOR (also sometimes referred to as "Kappa")
 * is a constant that is used to derive "optimal" "control points"
 * along the intersecting tangents to the beginning and end points
 * of a Bezier curve that approximates a quadrant of an ellipse
 * that is used to draw an arc path and to
 * define a dual-radii rounded corner of a round corner rectangle
 */

#define CIRCLE_FACTOR ( 4.0 * ( sqrt( 2.0 ) - 1.0 ) / 3.0 )

/*
 * pclxl_arc_quadrant_path() takes all the parameters necessary
 * to draw one (partial) quadrant of an ellipse
 *
 * It does a moveto/lineto the start position if necessary
 * and then calculates the first-order control points
 * to draw a full quadrant of an ellipse across to the quadrant end point
 *
 * If the actual start and end points are not at the start
 * and/or end points of the full ellipse
 * as indicated by non-zero and/or non-1.0 proportions
 * then it calculates the second and third-order control points
 * needed to draw the part of the quadrant needed
 */

static Bool
pclxl_arc_quadrant_path(PCLXL_CONTEXT pclxl_context,
                        PCLXL_SysVal_XY* quadrant_start_point,
                        PCLXL_SysVal_XY* arc_start_point,
                        PCLXL_SysVal     arc_start_proportion,
                        PCLXL_SysVal_XY* quadrant_corner,
                        PCLXL_SysVal_XY* arc_end_point,
                        PCLXL_SysVal     arc_end_proportion,
                        PCLXL_SysVal_XY* quadrant_end_point)
{
  PCLXL_SysVal_XY control_point_1;
  PCLXL_SysVal_XY control_point_2;

  HQASSERT(((arc_start_proportion >= 0.0) && (arc_start_proportion <= 1.0)),
           "Elliptical Arc (Quadrant) Start \"Proportion\" must be between 0.0 to 1.0 (inclusive)");

  HQASSERT(((arc_end_proportion >= 0.0) && (arc_end_proportion <= 1.0)),
           "Elliptical Arc (Quadrant) End \"Proportion\" must be between 0.0 to 1.0 (inclusive)");

  /*
   * The first thing to do is calculate the "first-order" control points
   * which basically control the bezier curve
   * from the start of the quadrant
   * to the end of the quadrant
   */

  (void) pclxl_mid_point(quadrant_start_point,
                         quadrant_corner,
                         CIRCLE_FACTOR,
                         &control_point_1);

  (void) pclxl_mid_point(quadrant_end_point,
                         quadrant_corner,
                         CIRCLE_FACTOR,
                         &control_point_2);

  /*
   * Then we "moveto" the arc
   * start point that we have been given
   */

  if ( !pclxl_context->graphics_state->current_point )
  {
    if ( !pclxl_moveto(pclxl_context, arc_start_point->x, arc_start_point->y) )
    {
      return FALSE;
    }
  }
  else if ( !pclxl_moveif(pclxl_context,
                          arc_start_point->x,
                          arc_start_point->y) )
  {
    return FALSE;
  }

  /*
   * Now, the question is: Are we drawing the full quadrant? Because if so,
   * then control_point_1 and control_point2 are already correctly set
   * But if not, then we have to sub-divide the full quadrant bezier appropriately
   */

  if ( (arc_start_proportion > 0.0) &&
       (arc_end_proportion < 1.0) )
  {
    /*
     * Ok, we need to recalculate the control points
     * to define a sub-section of the full quadrant curve
     * that "trims" *both* ends of the curve
     */

    PCLXL_SysVal reversed_arc_start_proportion = (1.0 - arc_start_proportion);
    PCLXL_SysVal reduced_arc_end_proportion = ((arc_end_proportion - arc_start_proportion) / (1.0 - arc_start_proportion));

    PCLXL_SysVal_XY recalculated_start_point;
    PCLXL_SysVal_XY recalculated_end_point;

    (void) pclxl_trim_bezier(quadrant_end_point,
                             &control_point_2,
                             &control_point_1,
                             quadrant_start_point,
                             reversed_arc_start_proportion,
                             &control_point_2,
                             &control_point_1,
                             &recalculated_start_point);

    (void) pclxl_trim_bezier(&recalculated_start_point,
                             &control_point_1,
                             &control_point_2,
                             quadrant_end_point,
                             reduced_arc_end_proportion,
                             &control_point_1,
                             &control_point_2,
                             &recalculated_end_point);
  }
  else if ( arc_start_proportion > 0.0 )
  {
    /*
     * We need to trim the start (only)
     * of the full quadrant curve
     */

    PCLXL_SysVal reversed_arc_start_proportion = (1.0 - arc_start_proportion);

    PCLXL_SysVal_XY recalculated_start_point;

    (void) pclxl_trim_bezier(quadrant_end_point,
                             &control_point_2,
                             &control_point_1,
                             quadrant_start_point,
                             reversed_arc_start_proportion,
                             &control_point_2,
                             &control_point_1,
                             &recalculated_start_point);
  }
  else if ( arc_end_proportion < 1.0 )
  {
    PCLXL_SysVal_XY recalculated_end_point;

    (void) pclxl_trim_bezier(quadrant_start_point,
                             &control_point_1,
                             &control_point_2,
                             quadrant_end_point,
                             arc_end_proportion,
                             &control_point_1,
                             &control_point_2,
                             &recalculated_end_point);
  }

  /*
   * Now we can draw a curve to the end point
   * using the two control points that we have calculated
   */

  if ( !pclxl_ps_curveto(pclxl_context,
                         control_point_1.x,
                         control_point_1.y,
                         control_point_2.x,
                         control_point_2.y,
                         arc_end_point->x,
                         arc_end_point->y) )
  {
    return FALSE;
  }
  else
  {
    pclxl_get_current_path_and_point(pclxl_context->graphics_state);
    return TRUE;
  }
}

enum
{
  PCLXL_ARC_TYPE_ELLIPSE = 0,
  PCLXL_ARC_TYPE_ARC,
  PCLXL_ARC_TYPE_CHORD,
  PCLXL_ARC_TYPE_PIE
};

static Bool
pclxl_arc_path(PCLXL_CONTEXT      pclxl_context,
               PCLXL_SysVal_Box*  bbox,
               PCLXL_SysVal_XY*   start_reference_point,
               PCLXL_SysVal_XY*   end_reference_point,
               PCLXL_ArcDirection arc_direction,
               uint8              arc_type)
{
  /**
   * In order to draw the (elliptical) arc path
   * given a bounding box, a pair of start and end reference points
   * and an arc direction (clockwise or counter-clockwise)
   *
   * We are going to divide the bounding box into for quadrants
   * labelled thus:
   *
   *
   *  (x1,y1) "tl"                 "ct"                 "tr"
   *           +--------------------+--------------------+
   *           |                    |                    |
   *           |                    |                    |
   *           |         NW         |        NE          |
   *           |                    |                    |
   *           |                    |"c"                 |
   *      "cl" +--------------------+--------------------+ "cr"
   *           |                    |                    |
   *           |                    |                    |
   *           |         SW         |        SE          |
   *           |                    |                    |
   *           |                    |                    |
   *           +--------------------+--------------------+
   *          "bl"                 "cb"                 "br" (x2,y2)
   *
   *
   * We are then going work out which quadrants are involved in the
   * requested arc (as defined by the start and end reference points
   * which may or may not be inside the bounding box but must not
   * be coincident with "c")
   *
   * Then we are going to draw (in the correct order)
   * the quadrants (full or partial) to effect the arc
   */
  PCLXL_SysVal_XY tl;
  PCLXL_SysVal_XY bl;
  PCLXL_SysVal_XY tr;
  PCLXL_SysVal_XY br;

  PCLXL_SysVal_XY c;

  PCLXL_SysVal_XY ct;
  PCLXL_SysVal_XY cl;
  PCLXL_SysVal_XY cb;
  PCLXL_SysVal_XY cr;

  PCLXL_SysVal x_radius;
  PCLXL_SysVal y_radius;

  PCLXL_SysVal yx_ratio;
  PCLXL_SysVal sp_theta;
  PCLXL_SysVal ep_theta;

  PCLXL_SysVal_XY sp;
  PCLXL_SysVal_XY ep;

  uint8 sp_quadrant;
  uint8 ep_quadrant;

  PCLXL_SysVal sp_proportion;
  PCLXL_SysVal ep_proportion;

  PCLXL_ArcDirection opposite_arc_direction =
      (arc_direction == PCLXL_eClockWise ? PCLXL_eCounterClockWise : PCLXL_eClockWise);

  /*
   * In order to make the subsequent code
   * as compact as possible
   * we want to be able to access the corners of the bounding box
   * using a quadrant index (NE = 0, NW = 1, SW = 2, SE = 3)
   */

  PCLXL_SysVal_XY* bbox_corners[4];

  /*
   * And we want to be able to access the mid-points of the edges
   * of the bounding box using a combination of a quadrant index
   * and the arc direction (PCLXL_eClockWise = 0, PCLXL_eCounterClockWise = 1)
   */

  PCLXL_SysVal_XY* bbox_edge_mid_points[4][2];

  bbox_corners[PCLXL_NE_QUADRANT] = &tr;
  bbox_corners[PCLXL_NW_QUADRANT] = &tl;
  bbox_corners[PCLXL_SW_QUADRANT] = &bl;
  bbox_corners[PCLXL_SE_QUADRANT] = &br;

  bbox_edge_mid_points[PCLXL_NE_QUADRANT][PCLXL_eClockWise]        = &cr;
  bbox_edge_mid_points[PCLXL_NE_QUADRANT][PCLXL_eCounterClockWise] = &ct;
  bbox_edge_mid_points[PCLXL_NW_QUADRANT][PCLXL_eClockWise]        = &ct;
  bbox_edge_mid_points[PCLXL_NW_QUADRANT][PCLXL_eCounterClockWise] = &cl;
  bbox_edge_mid_points[PCLXL_SW_QUADRANT][PCLXL_eClockWise]        = &cl;
  bbox_edge_mid_points[PCLXL_SW_QUADRANT][PCLXL_eCounterClockWise] = &cb;
  bbox_edge_mid_points[PCLXL_SE_QUADRANT][PCLXL_eClockWise]        = &cb;
  bbox_edge_mid_points[PCLXL_SE_QUADRANT][PCLXL_eCounterClockWise] = &cr;

  tl.x = bbox->x1;
  tl.y = bbox->y1;

  br.x = bbox->x2;
  br.y = bbox->y2;

  bl.x = tl.x;
  bl.y = br.y;

  tr.x = br.x;
  tr.y = tl.y;

  c.x = ((tl.x + br.x) / 2);
  c.y = ((tl.y + br.y) / 2);

  cr.x = br.x;
  cr.y = c.y;

  ct.x = c.x;
  ct.y = tl.y;

  cl.x = tl.x;
  cl.y = c.y;

  cb.x = c.x;
  cb.y = br.y;

  x_radius = (cr.x - c.x);

  y_radius = (cb.y - c.y);

  /*
   * If either (or both) of the bounding box dimensions
   * (and thus the radii) are zero, then we have to cope with
   * the following special cases:
   *
   * 1) If we have been asked to draw an ArcPath, Chord or ChordPath
   * (or Pie/PiePath ?) then we draw nothing.
   *
   *
   * 2) If *both* of the bounding box dimensions are zero
   * then we also draw nothing for ellipses
   */

  if ( ((arc_type != PCLXL_ARC_TYPE_ELLIPSE) &&
        ((x_radius == 0.0) || (y_radius == 0.0))) ||
       ((arc_type == PCLXL_ARC_TYPE_ELLIPSE) &&
        (x_radius == 0.0) &&
        (y_radius == 0.0)) )
  {
    return TRUE;
  }

  /*
   * Before we perform any more calculations we need to check that neither the
   * start point or end point coincide with the central point
   *
   * As, otherwise, some of the following calculations would result in
   * division-by-zero errors.
   *
   * Note that pclxl_ellipse_path() applies a fix to its start and end points
   * such that neither are ever coincident with the centre point
   * that we have just calculated/derived from the bounding box
   */

  if ( ((start_reference_point->x == c.x) && (start_reference_point->y == c.y)) ||
       ((end_reference_point->x == c.x) && (end_reference_point->y == c.y)) )
  {
    /*
     * Unfortunately one of the reference points coincides with the centre of
     * the bounding box
     *
     * Therefore we cannot project a "ray" from the centre of the bounding box
     * through this reference point
     *
     * And therefore there is no way to calculate the start point and/or end
     * point of the elliptical arc
     */

    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Start reference point (%f,%f) and/or end reference point (%f,%f) is coincident with centre point (%f,%f)",
                         start_reference_point->x, start_reference_point->y,
                         end_reference_point->x, end_reference_point->y,
                         c.x, c.y));
    return FALSE;
  }

  /*
   * We also have a problem if either of the radii is zero
   * because we are then unable to determine the start and end point angles
   * and thus the start and end points
   */

  if ( (x_radius == 0.0) || (y_radius == 0.0) )
  {
    /*
     * As a simple frig we will set both start and end points to be 180 degrees
     * because this is the default start/end point of ellipses anyway
     */
    sp_theta =  ep_theta = PI;

    yx_ratio = 0;
  }
  else
  {
    yx_ratio = y_radius / x_radius;

    /*
     * Note that we "stretch" the <y> coordinate as if to make the bounding box
     * square and thus the ellipse into a circle
     *
     * And then calculate these these virtual angles (remembering that PCLXL's
     * "y-axis" increases down the page)
     */

    sp_theta = atan2(((start_reference_point->y - c.y) / yx_ratio), (start_reference_point->x - c.x));

    ep_theta = atan2(((end_reference_point->y - c.y) / yx_ratio), (end_reference_point->x - c.x));
  }

  /*
   * We then calculate the x and y intersects for the lines (a.k.a. rays)
   * projected from the centre through the start and end reference points with
   * this stretched ellipse/circle
   *
   * And then "unstretch" the resultant y coordinate to make it elliptical again
   */

  sp.x = (c.x + (x_radius * cos(sp_theta)));

  sp.y = (c.y + (yx_ratio * x_radius * sin(sp_theta)));

  ep.x = (c.x + (x_radius * cos(ep_theta)));

  ep.y = (c.y + (yx_ratio * x_radius * sin(ep_theta)));

  /*
   * We can also convert these two angles into "quadrant" enumeration values So
   * that we know if both ends of the arc are in the same quadrant and/or how
   * many quadrants must be drawn
   */

  (void) pclxl_quadrant_and_proportion(sp_theta,
                                       arc_direction,
                                       TRUE,
                                       &sp_quadrant,
                                       &sp_proportion);

  (void) pclxl_quadrant_and_proportion(ep_theta,
                                       arc_direction,
                                       FALSE,
                                       &ep_quadrant,
                                       &ep_proportion);


  /*
   * We now have the coordinates for the corners of the bounding box, and the
   * centres of each edge of the bounding box which collectively allow us to
   * define the bezier curves for the full quadrants (if any) that are included
   * in the arc the center of the bounding box
   *
   * We also have a pair of theoretical polar angles (in radians) for the start
   * and end points (calculated as if the ellipse were stretched to be
   * circular).
   *
   * This allows us to calculate the portion (i.e. fraction of PI/2 radians) of
   * the beziers for the quadrants where the and start and end points of the arc
   * are.  Note that if these are both in the same quadrant then this adds an
   * extra complication
   *
   * We also have the start and end-point coordinates to be used for the initial
   * "moveto" and final "curveto"
   *
   * So we now have to work out which partial quadrants to draw and which full
   * quadrants to draw (in the correct order and direction) We should be able to
   * use the "theta" angles for this too
   */

#ifdef DEBUG_BUILD
  {
    /* use temporary variables here for the VxWorks compiler which
     * cannot cope with "complex" floating point expressions
     */
    int32 spx = (int32) (sp.x - c.x);
    int32 spy = (int32) (sp.y - c.y);
    int32 sptheta = PCLXL_RADIANS_TO_DEGREES(sp_theta);
    int32 spprop  = PCLXL_PROPORTION_TO_PERCENTAGE(sp_proportion);
    int32 epx = (int32) (ep.x - c.x);
    int32 epy = (int32) (ep.y - c.y);
    int32 eptheta = PCLXL_RADIANS_TO_DEGREES(ep_theta);
    int32 epprop  = PCLXL_PROPORTION_TO_PERCENTAGE(ep_proportion);

    PCLXL_DEBUG(PCLXL_DEBUG_ARC_PATH,
                ("\
Start Point (relative to centre) = (%d,%d)\n\
Start Point Theta                = %d (quadrant %d, proportion %d%%%%, direction = %s)\n\
End Point (relative to centre)   = (%d,%d)\n\
End Point Theta                  = %d (quadrant %d, proportion %d%%%%, direction = %s)",
                 spx,
                 spy,
                 sptheta,
                 sp_quadrant,
                 spprop,
                 PCLXL_PRINTABLE_ARC_DIRECTION(arc_direction),
                 epx,
                 epy,
                 eptheta,
                 ep_quadrant,
                 epprop,
                 PCLXL_PRINTABLE_ARC_DIRECTION(arc_direction)));
  }

  if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_ARC_PATH)
  {
    (void) pclxl_debug_elliptical_arc(pclxl_context,
                                      &tl,
                                      &bl,
                                      &br,
                                      &tr,
                                      &c,
                                      &cr,
                                      &ct,
                                      &cl,
                                      &cb,
                                      start_reference_point,
                                      end_reference_point,
                                      &sp,
                                      &ep);
  }
#endif

  if ( !pclxl_context->graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so any and all paths will also be "degenerate"
     * which in the core rip are represented as a NULL path (list)
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  /*
   * If the arc is for a pie then we need to first draw a line from the
   * centre position to the arc's start point (a pie will already have
   * done a move to centre).  Starting a pie from the centre point, followed
   * by a line to the arc start point, is crucial to match line dashing
   * with the HP printers.  Note, the spec doesn't appear to actually
   * state where on the pie the path should start.
   */

  if ( (arc_type == PCLXL_ARC_TYPE_PIE) &&
       (!pclxl_ps_lineto(pclxl_context, sp.x, sp.y)) )
  {
    return FALSE;
  }

  if ( (ep_quadrant == sp_quadrant) &&
       (((arc_direction == PCLXL_eCounterClockWise) &&
         (ep_theta < sp_theta)
        ) ||
        ((arc_direction == PCLXL_eClockWise) &&
         (ep_theta > sp_theta)
        )
       )
     )
  {
    /*
     * We have encountered the special case of an arc being drawn entirely
     * within a single segment I.e. both start and end points lie within the
     * same quadrant and either: we are drawing a clockwise arc and the end
     * point is more clockwise than the start point or we are drawing a
     * counter-clockwise arc and the end point is more counter-clockwise than
     * the start point
     */

    return pclxl_arc_quadrant_path(pclxl_context,
                                   bbox_edge_mid_points[sp_quadrant][opposite_arc_direction],
                                   &sp,
                                   sp_proportion,
                                   bbox_corners[sp_quadrant],
                                   &ep,
                                   ep_proportion,
                                   bbox_edge_mid_points[ep_quadrant][arc_direction]);
  }
  else
  {
    /*
     * We have encountered the general case of an elliptical arc
     * that must pass through more than one quadrant
     */

    uint8 quad_incr = (arc_direction == PCLXL_eCounterClockWise ? 1 : 3);
    uint8 quadrant;
    uint8 quadrants_drawn;

    for ( quadrant = sp_quadrant, quadrants_drawn = 0 ;
          quadrants_drawn < 5 ; /* Note that we will break out of the loop
                                 * a) if an error occurs
                                 * b) we succeed with the last segment
                                 * So quadrants_drawn being >= 5 represents
                                 * a serious failure of this loop
                                 */
          quadrant = ((quadrant + quad_incr) % 4) )
    {
      if ( (quadrants_drawn == 0) && (quadrant == sp_quadrant) )
      {
        /*
         * Ok, this is the first segment of the arc
         * So we are probably only drawing part of the full quadrant's arc
         * starting at the start point and finishing at the edge of this quadrant
         */

        if ( !pclxl_arc_quadrant_path(pclxl_context,
                                      bbox_edge_mid_points[quadrant][opposite_arc_direction],
                                      &sp,
                                      sp_proportion,
                                      bbox_corners[quadrant],
                                      bbox_edge_mid_points[quadrant][arc_direction],
                                      1.0,
                                      bbox_edge_mid_points[quadrant][arc_direction]) )
        {
          /*
           * Oops, something has gone wrong while drawing this segment.
           *
           * A suitable error has been logged So we simply break out of this
           * loop and indeed the whole function
           */

          return FALSE;
        }
        else
        {
          /*
           * Just in case the end of the arc falls into the same quadrant as the
           * start of the arc we must guard against getting stuck in this
           * start-of-arc branch and thus stuck in a loop
           */

          quadrants_drawn++;
        }
      }
      else if ( quadrant == ep_quadrant )
      {
        if ( !pclxl_arc_quadrant_path(pclxl_context,
                                      bbox_edge_mid_points[quadrant][opposite_arc_direction],
                                      bbox_edge_mid_points[quadrant][opposite_arc_direction],
                                      0.0,
                                      bbox_corners[quadrant],
                                      &ep,
                                      ep_proportion,
                                      bbox_edge_mid_points[quadrant][arc_direction]) )
        {
          /*
           * We have failed to draw the last segment of the arc.
           * Again we simply break out of the loop/return out of the function.
           */

          return FALSE;
        }
        else
        {
          /*
           * We have successfully drawn the last segment of the arc.
           * So we have successfully completed the task
           */

          quadrants_drawn++;

          return TRUE;
        }
      }
      else
      {
        if ( !pclxl_arc_quadrant_path(pclxl_context,
                                      bbox_edge_mid_points[quadrant][opposite_arc_direction],
                                      bbox_edge_mid_points[quadrant][opposite_arc_direction],
                                      0.0,
                                      bbox_corners[quadrant],
                                      bbox_edge_mid_points[quadrant][arc_direction],
                                      1.0,
                                      bbox_edge_mid_points[quadrant][arc_direction]) )
        {
          /*
           * We have failed to draw an intermediate
           * (full) quadrant of the arc
           */

          return FALSE;
        }
        else
        {
          quadrants_drawn++;
        }
      }
    }
  }

  HQFAIL("We should not have reached this point in the code");

  return FALSE;
}

/*
 * Tag 0x91 ArcPath is passed a bounding box, a start point and an end point
 * and an optional arc direction
 */

Bool
pclxl_op_arc_path(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[5] = {
#define ARCPATH_BOUNDING_BOX    (0)
    {PCLXL_AT_BoundingBox | PCLXL_ATTR_REQUIRED},
#define ARCPATH_START_POINT     (1)
    {PCLXL_AT_StartPoint | PCLXL_ATTR_REQUIRED},
#define ARCPATH_END_POINT       (2)
    {PCLXL_AT_EndPoint | PCLXL_ATTR_REQUIRED},
#define ARCPATH_ARC_DIRECTION   (3)
    {PCLXL_AT_ArcDirection},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION arc_direction_values[] = {
    PCLXL_eClockWise,
    PCLXL_eCounterClockWise,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY start_reference_point;
  PCLXL_SysVal_XY end_reference_point;
  PCLXL_ArcDirection arc_direction;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(match[ARCPATH_BOUNDING_BOX].result, &bounding_box);
  /* StartPoint */
  pclxl_attr_get_real_xy(match[ARCPATH_START_POINT].result, &start_reference_point);
  /* EndPoint */
  pclxl_attr_get_real_xy(match[ARCPATH_END_POINT].result, &end_reference_point);
  /* ArcDirection */
  arc_direction = PCLXL_eCounterClockWise;
  if ( match[ARCPATH_ARC_DIRECTION].result ) {
    if ( !pclxl_attr_match_enumeration(match[ARCPATH_ARC_DIRECTION].result, arc_direction_values,
                                       &arc_direction, pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS | PCLXL_DEBUG_ARC_PATH),
              ("ArcPath(BoundingBox = (%f,%f,%f,%f), StartPoint = (%f,%f), EndPoint = (%f,%f), ArcDirection = %s)",
               bounding_box.x1, bounding_box.y1,
               bounding_box.x2, bounding_box.y2,
               start_reference_point.x, start_reference_point.y,
               end_reference_point.x, end_reference_point.y,
               PCLXL_PRINTABLE_ARC_DIRECTION(arc_direction)));

  return(pclxl_new_sub_path(pclxl_context) &&
         pclxl_arc_path(pclxl_context, &bounding_box,
                        &start_reference_point, &end_reference_point,
                        arc_direction, PCLXL_ARC_TYPE_ARC));
}

static Bool
pclxl_embedded_beziers(PCLXL_PARSER_CONTEXT parser_context,
                       uint32               number_of_points,
                       PCLXL_DataTypeSimple point_data_type,
                       Bool                 absolute)
{
  /*
   * Ok we are expecting a number of "points" which are each in the specified
   * data type
   *
   * We are expecting them to be supplied in the embedded data which immediately
   * follows the PCLXL operator tag which we have just read
   *
   * We are expecting the number of points to be a multiple of 3 because each
   * bezier requires 3 (additional) points which are the control points 1 and 2
   * and an end point
   */
  PCLXL_EMBEDDED_READER embedded_reader;
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  static uint8 point_data_type_sizes[] = { 1, 1, 2,  2 };
  uint32 expected_data_length = number_of_points*2*point_data_type_sizes[point_data_type];

  if ( !parser_context->data_source_open ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_DATA_SOURCE_NOT_OPEN,
                        ("Failed to read embedded bezier"));
    return FALSE;
  }

  if ( !pclxl_stream_embedded_init(pclxl_context,
                                   pclxl_parser_current_stream(parser_context),
                                   parser_context->data_source_big_endian,
                                   &embedded_reader) ) {
    return(FALSE);
  }
  if ( expected_data_length != pclxl_embedded_length(&embedded_reader) ) {
    /*
     * There is insufficient embedded data to represent the expected number (and
     * size) of points
     */
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_DATA_LENGTH,
                        ("The actual (embedded) data length (%d bytes) does not match the expected data length of %d bezier point triplets each consisting of 3 x 2 x %d byte%s => total %d byte%s",
                         pclxl_embedded_length(&embedded_reader), number_of_points,
                         point_data_type_sizes[point_data_type],
                         (point_data_type_sizes[point_data_type] > 1 ? "s" : ""),
                         expected_data_length,
                         (expected_data_length > 1 ? "s" : "")));
    return FALSE;
  }
  if ( (number_of_points % 3) != 0 ) {
    /*
     * We are expecting the number of points to be a multiple of 3 because each
     * bezier curve requires 3 additional points which represent control points
     * 1 and 2 and the next end point
     */
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_ILLEGAL_DATA_LENGTH,
                        ("Embedded data points for bezier must be a multiple of 3"));
    return FALSE;

  }
  number_of_points /= 3;
  while ( number_of_points-- > 0 ) {
    SYSTEMVALUE args[6];
    int32 points[6];  /* 0,1 = cp1, 2,3 = cp2, 4,5 = ep */

    if ( !pclxl_embedded_read_data(&embedded_reader, point_data_type, points, 6) ) {
      if ( pclxl_embedded_insufficient(&embedded_reader) ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_DATA,
                            ("Failed to read embedded point"));
      }
      return FALSE;
    }
    args[0] = points[0];
    args[1] = points[1];
    args[2] = points[2];
    args[3] = points[3];
    args[4] = points[4];
    args[5] = points[5];

#ifdef DEBUG_BUILD
    if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_ARC_PATH) {
      PCLXL_SysVal_XY cp1, cp2, ep;

      cp1.x = args[0]; cp1.y = args[1];
      cp2.x = args[2]; cp2.y = args[3];
      ep.x = args[4]; ep.y = args[5];
      pclxl_debug_bezier_curve(pclxl_context,
                               &pclxl_context->graphics_state->current_point_xy,
                               &cp1, &cp2, &ep);
    }
#endif
    if ( pclxl_context->graphics_state->ctm_is_invertible &&
         !gs_curveto(absolute, TRUE, args, &thePathInfo(*gstateptr)) )
    {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                          ("Failed to rcurveto(cp1_x = %f, cp1_y = %f, cp2_x = %f, cp2_y = %f, curveto_x = %f, curveto_y = %f)",
                           points[0], points[1], points[2], points[3], points[4], points[5]));
      return(FALSE);
    }
  }

  return(pclxl_embedded_flush(&embedded_reader));
}

/*
 * pclxl_bezier_path() handles either a single attribute-supplied bezier path
 * or a collection of embedded data-define beziers
 * on behalf of both pclxl_op_bezier_path() and pclxl_op_bezier_rel_path()
 * as they both accept the same attribute collections.
 */

static Bool
pclxl_bezier_path(PCLXL_PARSER_CONTEXT parser_context,
                  Bool                 absolute)
{
  static PCLXL_ATTR_MATCH match[6] = {
#define BEZIERPATH_NUMBER_OF_POINTS (0)
    {PCLXL_AT_NumberOfPoints},
#define BEZIERPATH_POINT_TYPE       (1)
    {PCLXL_AT_PointType},
#define BEZIERPATH_CONTROL_POINT_1  (2)
    {PCLXL_AT_ControlPoint1},
#define BEZIERPATH_CONTROL_POINT_2  (3)
    {PCLXL_AT_ControlPoint2},
#define BEZIERPATH_END_POINT        (4)
    {PCLXL_AT_EndPoint},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_point_types[] = {
    PCLXL_eUByte,
    PCLXL_eByte,
    PCLXL_eUInt16,
    PCLXL_eInt16,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY control_point_1;
  PCLXL_SysVal_XY control_point_2;
  PCLXL_SysVal_XY end_point;
  PCLXL_DataTypeSimple point_data_type;
  SYSTEMVALUE args[6];
  int32 number_of_points;

#ifdef DEBUG_BUILD
  uint8* operator_name = pclxl_get_tag_string(pclxl_stream_op_tag(pclxl_parser_current_stream(parser_context)));
#endif /* DEBUG_BUILD */

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( match[BEZIERPATH_NUMBER_OF_POINTS].result || match[BEZIERPATH_POINT_TYPE].result ) {
    /* Embedded control and end points */
    if ( match[BEZIERPATH_CONTROL_POINT_1].result ||
         match[BEZIERPATH_CONTROL_POINT_1].result ||
         match[BEZIERPATH_END_POINT].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Need NumberOfPoints and PointType or control points"));
      return(FALSE);
    }
    if ( !match[BEZIERPATH_NUMBER_OF_POINTS].result ||
         !match[BEZIERPATH_POINT_TYPE].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Need both NumberOfPoints and PointType"));
      return(FALSE);
    }

    /* NumberOfPoints */
    number_of_points = pclxl_attr_get_int(match[BEZIERPATH_NUMBER_OF_POINTS].result);
    /* PointType */
    if ( !pclxl_attr_match_enumeration(match[BEZIERPATH_POINT_TYPE].result, allowed_point_types,
                                       &point_data_type, pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    if ( graphics_state->ctm_is_invertible && !graphics_state->current_point ) {
      /* There is no current cursor position so we cannot draw a bezier curve from
       * it to anywhere
       */
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_CURRENT_CURSOR_UNDEFINED,
                          ("There is no current cursor position. So %s cannot draw any bezier curve or bezier curves",
                           operator_name));
      return(FALSE);
    }

    /* We have been told about a number of points of a specific type that appear
     * as some embedded data immediately following this operator tag
     */
    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS | PCLXL_DEBUG_EMBEDDED_DATA),
                ("%s(NumberOfPoints = %d, PointType = %d)",
                 operator_name, number_of_points, point_data_type));

    if ( !pclxl_moveif(pclxl_context, graphics_state->current_point_xy.x, graphics_state->current_point_xy.y) ) {
      return(FALSE);
    }
    if ( !pclxl_embedded_beziers(parser_context, number_of_points, point_data_type, absolute) ) {
      return(FALSE);
    }

  } else { /* Explicit control points and end point */
    if ( !(match[BEZIERPATH_CONTROL_POINT_1].result &&
           match[BEZIERPATH_CONTROL_POINT_2].result &&
           match[BEZIERPATH_END_POINT].result) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Need both control points and end point"));
      return(FALSE);
    }

    if ( graphics_state->ctm_is_invertible && !graphics_state->current_point ) {
      /* There is no current cursor position so we cannot draw a bezier curve from
       * it to anywhere
       */
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_CURRENT_CURSOR_UNDEFINED,
                          ("There is no current cursor position. So %s cannot draw any bezier curve or bezier curves",
                           operator_name));
      return(FALSE);
    }

    /* ControlPoint1 */
    pclxl_attr_get_real_xy(match[BEZIERPATH_CONTROL_POINT_1].result, &control_point_1);
    /* ControlPoint2 */
    pclxl_attr_get_real_xy(match[BEZIERPATH_CONTROL_POINT_2].result, &control_point_2);
    /* EndPoint */
    pclxl_attr_get_real_xy(match[BEZIERPATH_END_POINT].result, &end_point);

    /* We have been supplied with a simple end point So we draw a line (using the
     * current pen source and width) from the current position/existing path end
     * point to this new end point
     */
    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
                ("%s(ControlPoint1.X = %f, ControlPoint1.Y = %f, ControlPoint2.X = %f, ControlPoint2.Y = %f, EndPoint.X = %f, EndPoint.Y = %f)",
                 operator_name, /* BezierPath or BezierRelPath */
                 control_point_1.x, control_point_1.y,
                 control_point_2.x, control_point_2.y,
                 end_point.x, end_point.y));

    if ( !pclxl_moveif(pclxl_context, graphics_state->current_point_xy.x, graphics_state->current_point_xy.y) ) {
      return(FALSE);
    }
#ifdef DEBUG_BUILD
    if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_ARC_PATH) {
      pclxl_debug_bezier_curve(pclxl_context, &graphics_state->current_point_xy,
                               &control_point_1, &control_point_2, &end_point);
    }
#endif
    args[0] = control_point_1.x;
    args[1] = control_point_1.y;
    args[2] = control_point_2.x;
    args[3] = control_point_2.y;
    args[4] = end_point.x;
    args[5] = end_point.y;

    if ( graphics_state->ctm_is_invertible &&
         !gs_curveto(absolute, TRUE, args, &thePathInfo(*gstateptr)) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                          ("Failed to %scurveto(cp1_x = %f, cp1_y = %f, cp2_x = %f, cp2_y = %f, curveto_x = %f, curveto_y = %f)",
                           (absolute ? "" : "r"),
                           control_point_1.x, control_point_1.y,
                           control_point_2.x, control_point_2.y,
                           end_point.x, end_point.y));
      return(FALSE);
    }
  }

  pclxl_get_current_path_and_point(graphics_state);
  return(TRUE);
}

/*
 * Tag 0x93 BezierPath either takes number of points and a point type
 * and the operator is then followed by that number of points
 * (which must be a multiple of 3)
 * Or it accepts two control points and an end point)
 */

Bool
pclxl_op_bezier_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return(pclxl_bezier_path(parser_context, TRUE));
}

/*
 * Tag 0x95 BezierRelPath
 */

Bool
pclxl_op_bezier_rel_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return(pclxl_bezier_path(parser_context, FALSE));
}

static
PCLXL_ATTR_MATCH chord_pie_match[4] = {
#define CHORDPIE_BOUNDING_BOX   (0)
  {PCLXL_AT_BoundingBox | PCLXL_ATTR_REQUIRED},
#define CHORDPIE_START_POINT    (1)
  {PCLXL_AT_StartPoint | PCLXL_ATTR_REQUIRED},
#define CHORDPIE_END_POINT      (2)
  {PCLXL_AT_EndPoint | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};

static Bool
pclxl_chord_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY start_reference_point;
  PCLXL_SysVal_XY end_reference_point;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, chord_pie_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(chord_pie_match[CHORDPIE_BOUNDING_BOX].result, &bounding_box);
  /* StartPoint */
  pclxl_attr_get_real_xy(chord_pie_match[CHORDPIE_START_POINT].result, &start_reference_point);
  /* EndPoint */
  pclxl_attr_get_real_xy(chord_pie_match[CHORDPIE_END_POINT].result, &end_reference_point);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS | PCLXL_DEBUG_ARC_PATH),
              ("%s(BoundingBox = (%f,%f,%f,%f), StartPoint = (%f,%f), EndPoint = (%f,%f))",
               pclxl_get_tag_string(pclxl_stream_op_tag(pclxl_parser_current_stream(parser_context))),
               bounding_box.x1, bounding_box.y1, bounding_box.x2, bounding_box.y2,
               start_reference_point.x, start_reference_point.y,
               end_reference_point.x, end_reference_point.y));

  return(pclxl_new_sub_path(pclxl_context) &&
         pclxl_arc_path(pclxl_context, &bounding_box,
                        &start_reference_point, &end_reference_point,
                        PCLXL_eCounterClockWise, PCLXL_ARC_TYPE_CHORD) &&
         pclxl_close_sub_path(pclxl_context->graphics_state) &&
         pclxl_moveto(pclxl_context, bounding_box.x1, bounding_box.y1));
}


/*
 * Tag 0x97 ChordPath
 */

Bool
pclxl_op_chord_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return(pclxl_chord_path(parser_context));
}

static Bool
pclxl_save_current_point(PCLXL_GRAPHICS_STATE graphics_state,
                         PCLXL_SysVal_XY*     saved_point_xy)
{
  *saved_point_xy = graphics_state->current_point_xy;

  return TRUE;
}

static Bool
pclxl_restore_current_point(PCLXL_CONTEXT pclxl_context,
                            PCLXL_SysVal_XY* saved_point_xy)
{
  return pclxl_moveto(pclxl_context, saved_point_xy->x, saved_point_xy->y);
}

/*
 * Tag 0x96 Chord
 */

Bool
pclxl_op_chord(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY saved_point;

  return(pclxl_new_path(graphics_state) &&
         pclxl_chord_path(parser_context) &&
         pclxl_paint_path(pclxl_context) &&
         pclxl_save_current_point(graphics_state, &saved_point) &&
         pclxl_new_path(graphics_state) &&
         pclxl_restore_current_point(pclxl_context, &saved_point));
}

static
PCLXL_ATTR_MATCH bbox_match[2] = {
#define BBOX_BOUNDING_BOX   (0)
  {PCLXL_AT_BoundingBox | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};

static Bool
pclxl_ellipse_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY start_and_end_point;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, bbox_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(bbox_match[BBOX_BOUNDING_BOX].result, &bounding_box);

  /* HP printers start ellipses at the 9 o'clock position.  This is crucial to
   * match line dashing.  Note, the spec doesn't appear to actually state where
   * on the ellipse the path should start.  Note that we apply a tiny fix/fiddle
   * to the start and end point such that it is guaranteed not to coincide with
   * the centre point calculated by pclxl_arc_path() even for zero width (or
   * height) bounding box dimensions */
  start_and_end_point.x = (bounding_box.x1 < bounding_box.x2 ? (bounding_box.x1 - 1) : (bounding_box.x2 - 1));
  start_and_end_point.y = (bounding_box.y1 + bounding_box.y2) * 0.5;

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS | PCLXL_DEBUG_ARC_PATH),
              ("%s(BoundingBox = (%f,%f,%f,%f))",
               pclxl_get_tag_string(pclxl_stream_op_tag(pclxl_parser_current_stream(parser_context))),
               bounding_box.x1, bounding_box.y1,
               bounding_box.x2, bounding_box.y2));

  return(pclxl_new_sub_path(pclxl_context) &&
         pclxl_arc_path(pclxl_context, &bounding_box,
                        &start_and_end_point, &start_and_end_point,
                        PCLXL_eCounterClockWise, PCLXL_ARC_TYPE_ELLIPSE) &&
         pclxl_close_sub_path(graphics_state) &&
         pclxl_moveto(pclxl_context, bounding_box.x1, bounding_box.y1));
}

/*
 * Tag 0x99 EllipsePath
 */

Bool
pclxl_op_ellipse_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return(pclxl_ellipse_path(parser_context));
}

/*
 * Tag 0x98 Ellipse
 */

Bool
pclxl_op_ellipse(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY saved_point;

  return(pclxl_new_path(graphics_state) &&
         pclxl_ellipse_path(parser_context) &&
         pclxl_paint_path(pclxl_context) &&
         pclxl_save_current_point(graphics_state, &saved_point) &&
         pclxl_new_path(graphics_state) &&
         pclxl_restore_current_point(pclxl_context, &saved_point));
}

static Bool
pclxl_pie_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY start_reference_point;
  PCLXL_SysVal_XY end_reference_point;
  PCLXL_SysVal_XY centre_point;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, chord_pie_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(chord_pie_match[CHORDPIE_BOUNDING_BOX].result, &bounding_box);
  /* StartPoint */
  pclxl_attr_get_real_xy(chord_pie_match[CHORDPIE_START_POINT].result, &start_reference_point);
  /* EndPoint */
  pclxl_attr_get_real_xy(chord_pie_match[CHORDPIE_END_POINT].result, &end_reference_point);

  centre_point.x = ((bounding_box.x1 + bounding_box.x2) / 2.0);
  centre_point.y = ((bounding_box.y1 + bounding_box.y2) / 2.0);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS | PCLXL_DEBUG_ARC_PATH),
              ("%s(BoundingBox = (%f,%f,%f,%f), StartPoint = (%f,%f), EndPoint = (%f,%f))",
               pclxl_get_tag_string(pclxl_stream_op_tag(pclxl_parser_current_stream(parser_context))),
               bounding_box.x1, bounding_box.y1,
               bounding_box.x2, bounding_box.y2,
               start_reference_point.x, start_reference_point.y,
               end_reference_point.x, end_reference_point.y));

  /* Note that because we *start with* a "moveto" a Pie[Path] is always the
   * start of a new sub-path
   */
  return(pclxl_moveto(pclxl_context, centre_point.x, centre_point.y) &&
         pclxl_arc_path(pclxl_context, &bounding_box,
                        &start_reference_point, &end_reference_point,
                        PCLXL_eCounterClockWise, PCLXL_ARC_TYPE_PIE) &&
         pclxl_close_sub_path(graphics_state) &&
         pclxl_moveto(pclxl_context, bounding_box.x1, bounding_box.y1));
}

/*
 * Tag 0x9f PiePath
 */

Bool
pclxl_op_pie_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return(pclxl_pie_path(parser_context));
}

/*
 * Tag 0x9e Pie
 */

Bool
pclxl_op_pie(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = parser_context->pclxl_context->graphics_state;
  PCLXL_SysVal_XY saved_point;

  return (
           pclxl_new_path(graphics_state) &&
           pclxl_pie_path(parser_context) &&
           pclxl_paint_path(parser_context->pclxl_context) &&
           pclxl_save_current_point(graphics_state, &saved_point) &&
           pclxl_new_path(graphics_state) &&
           pclxl_restore_current_point(parser_context->pclxl_context, &saved_point)
         );
}


/*
 * There are at least two PCLXL operators which expect the same
 * "multiAttributeList" and handle them in the same way.  These are specifically
 * LinePath (0x9b) and LineRelPath (0x9d)
 */

static Bool
pclxl_embedded_points(PCLXL_PARSER_CONTEXT   parser_context,
                      uint32                 number_of_points,
                      PCLXL_DataTypeSimple   point_data_type,
                      Bool                   absolute)
{
  /*
   * Ok we are expecting a number of "points" which are each in the specified
   * data type and we are expecting them to be supplied in the embedded data
   * which immediately follows the PCLXL operator tag which we have just read
   */
  PCLXL_EMBEDDED_READER embedded_reader;
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  static uint8 point_data_type_sizes[] = { 1, 1, 2,  2 };
  uint32 expected_data_length = number_of_points * 2 * point_data_type_sizes[point_data_type];

  if ( !parser_context->data_source_open ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_DATA_SOURCE_NOT_OPEN,
                        ("Failed to read embedded point"));
    return FALSE;
  }

  if ( !pclxl_stream_embedded_init(pclxl_context,
                                   pclxl_parser_current_stream(parser_context),
                                   parser_context->data_source_big_endian,
                                   &embedded_reader) ) {
    return(FALSE);
  }
  if ( expected_data_length < pclxl_embedded_length(&embedded_reader) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_DATA_SOURCE_EXCESS_DATA,
                        ("Embedded data length (%d bytes) greater than expected data length (%d bytes)",
                         pclxl_embedded_length(&embedded_reader), expected_data_length));
    return(FALSE);
  }

  if ( pclxl_context->graphics_state->ctm_is_invertible ) {
    while ( number_of_points-- > 0 ) {
      int32 point[2];
      SYSTEMVALUE args[2];

      if ( !pclxl_embedded_read_data(&embedded_reader, point_data_type, point, 2) ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_DATA,
                            ("Failed to read embedded point"));
        return FALSE;
      }

      /* Convert to fp values for the core */
      args[0] = point[0];
      args[1] = point[1];
      if ( !gs_lineto(absolute, TRUE, args, &thePathInfo(*gstateptr)) )
      {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                            ("Failed to lineto(x = %f, y = %f)",
                             point[0],
                             point[1]));

        return FALSE;
      }
      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                  ("PS: %slineto(x = %f, y = %f)", (absolute ? "" : "r"), args[0], args[1]));
    }
  }

  return(pclxl_embedded_flush(&embedded_reader));
}

/*
 * There are at least two PCLXL operators which expect the same
 * "multiAttributeList" and handle them in the same way.  These are specifically
 * LinePath (0x9b) and LineRelPath (0x9d)
 */

static Bool
pclxl_line_path(PCLXL_PARSER_CONTEXT   parser_context,
                Bool                   absolute)
{
  static PCLXL_ATTR_MATCH match[4] = {
#define LINEPATH_NUMBER_OF_POINTS   (0)
    {PCLXL_AT_NumberOfPoints},
#define LINEPATH_POINT_TYPE         (1)
    {PCLXL_AT_PointType},
#define LINEPATH_END_POINT          (2)
    {PCLXL_AT_EndPoint},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_point_types[] = {
    PCLXL_eUByte,
    PCLXL_eByte,
    PCLXL_eUInt16,
    PCLXL_eInt16,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY end_point;
  PCLXL_DataTypeSimple point_data_type;
  SYSTEMVALUE args[2];
  int32 number_of_points;

#ifdef DEBUG_BUILD
  uint8* operator_name = pclxl_get_tag_string(pclxl_stream_op_tag(pclxl_parser_current_stream(parser_context)));
#endif /* DEBUG_BUILD */

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( match[LINEPATH_NUMBER_OF_POINTS].result ||
       match[LINEPATH_POINT_TYPE].result ) {
    /* Got embedded points */
    if ( match[LINEPATH_END_POINT].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got EndPoint as well as NumberOfPoints and/or PointType"));
      return(FALSE);
    }

    if ( !match[LINEPATH_NUMBER_OF_POINTS].result || !match[LINEPATH_POINT_TYPE].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Missing NumberOfPoints or PointType"));
      return(FALSE);
    }

    /* NumberOfPoints */
    number_of_points = pclxl_attr_get_int(match[LINEPATH_NUMBER_OF_POINTS].result);
    /* PointType */
    if ( !pclxl_attr_match_enumeration(match[LINEPATH_POINT_TYPE].result, allowed_point_types,
                                       &point_data_type, pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    if ( graphics_state->ctm_is_invertible && !graphics_state->current_point ) {
      /* There is no current cursor position so we cannot draw a line from it to
       * anywhere
       */
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_CURRENT_CURSOR_UNDEFINED,
                          ("There is no current cursor position. So %s cannot draw any line or lines",
                           operator_name));
      return(FALSE);
    }

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS | PCLXL_DEBUG_EMBEDDED_DATA),
                ("%s(NumberOfPoints = %d, PointType = %d)",
                 operator_name, number_of_points, point_data_type));

    if ( !pclxl_embedded_points(parser_context, number_of_points, point_data_type, absolute) ) {
      return(FALSE);
    }

  } else { /* Got a single point since must have matched at least 1 attribute */
    if ( match[LINEPATH_NUMBER_OF_POINTS].result || match[LINEPATH_POINT_TYPE].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got EndPoint and embedded points"));
      return(FALSE);
    }

    /* EndPoint */
    pclxl_attr_get_real_xy(match[LINEPATH_END_POINT].result, &end_point);

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
                ("%s(EndPoint.X = %f, EndPoint.Y = %f)",
                 operator_name, end_point.x, end_point.y));

    if ( graphics_state->ctm_is_invertible && !graphics_state->current_point ) {
      /* There is no current cursor position so we cannot draw a line from it to
       * anywhere
       */
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_CURRENT_CURSOR_UNDEFINED,
                          ("There is no current cursor position. So %s cannot draw any line or lines",
                           operator_name));
      return(FALSE);
    }

    args[0] = end_point.x;
    args[1] = end_point.y;

    if ( graphics_state->ctm_is_invertible &&
         !gs_lineto(absolute, TRUE, args, &thePathInfo(*gstateptr)) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                          ("Failed to lineto(x = %f, y = %f)", end_point.x, end_point.y));
      return(FALSE);
    }
  }

  pclxl_get_current_path_and_point(graphics_state);
  return(TRUE);
}

/*
 * Tag 0x9b LinePath
 */

Bool
pclxl_op_line_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return pclxl_line_path(parser_context, TRUE);
}

/*
 * Tag 0x9d LineRelPath
 */

Bool
pclxl_op_line_rel_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return pclxl_line_path(parser_context, FALSE);
}

/*
 * pclxl_rectangle_path is used by both of
 * pclxl_op_rectangle() and pclxl_rectangle_path()
 * to "draw" (i.e. define/describe) a (square-cornered) rectangle.
 *
 * According to the PCLXL "spec" rectangles (square or round-cornered)
 * and rectangle paths are always "closed" (sub) paths.
 * But both pclxl_rectangle_path() and pclxl_round_rectangle_path()
 * explicitly do nothing to any existing path
 * (i.e. they do *not* do an implicit NewPath
 * and thus they simply append a new closed sub-path)
 *
 * pclxl_rectangle_path() accepts a simple bounding box
 * specified as a pair of coordinates representing top-left-corner
 * and botton-right-corner.
 *
 * The rectangle is drawn from the top-left-corner
 * in an counter-clockwise (a.k.a. anti-clockwise) direction
 * through the bottom-right-corner coordinate
 * and back to the top-left-corner coordinate.
 * And then the path is closed and the current position is set to
 * the end of this sub-path
 */

static Bool
pclxl_rectangle_path(PCLXL_CONTEXT pclxl_context, PCLXL_SysVal_Box* bbox)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY tl;
  PCLXL_SysVal_XY bl;
  PCLXL_SysVal_XY tr;
  PCLXL_SysVal_XY br;

  if ( !graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so any and all paths will also be "degenerate"
     * which in the core rip are represented as a NULL path (list)
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  /*
   * For our convenience let's convert the bounding box
   * into four separate coordinates
   */

  tl.x = bbox->x1;
  tl.y = bbox->y1;

  br.x = bbox->x2;
  br.y = bbox->y2;

  bl.x = tl.x;
  bl.y = br.y;

  tr.x = br.x;
  tr.y = tl.y;

  if (
       /* move to start point of rectangle */
       (pclxl_moveto(pclxl_context, tl.x, tl.y)) &&
       /* line to bottom left corner */
       (pclxl_ps_lineto(pclxl_context, bl.x, bl.y)) &&
       /* line to bottom right corner */
       (pclxl_ps_lineto(pclxl_context, br.x, br.y)) &&
       /* line to top right corner */
       (pclxl_ps_lineto(pclxl_context, tr.x, tr.y)) &&
       /* line (back) to top left corner */
       (pclxl_ps_lineto(pclxl_context, tl.x, tl.y)) &&
       /*
        * According to the PCLXL "spec" rectangle paths are closed (sub) paths
        * So we now have an interesting choice:
        * Do we explicitly draw this last line segment?
        * Or do we just close the sub-path and let this complete the line segment?
        */
       (pclxl_close_sub_path(graphics_state))
     )
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/*
 * Tag 0xa0 Rectangle
 */

Bool
pclxl_op_rectangle(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY saved_point;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, bbox_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(bbox_match[BBOX_BOUNDING_BOX].result, &bounding_box);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("Rectange(BoundingBox = (%f,%f,%f,%f))",
               bounding_box.x1, bounding_box.y1, bounding_box.x2, bounding_box.y2));

  return(pclxl_new_path(graphics_state) &&
         pclxl_rectangle_path(pclxl_context, &bounding_box) &&
         pclxl_paint_path(pclxl_context) &&
         pclxl_save_current_point(graphics_state, &saved_point) &&
         pclxl_new_path(graphics_state) &&
         pclxl_restore_current_point(pclxl_context, &saved_point));
}

/*
 * Tag 0xa1 RectanglePath
 */

Bool
pclxl_op_rectangle_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_Box bounding_box;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, bbox_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(bbox_match[BBOX_BOUNDING_BOX].result, &bounding_box);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("RectangePath(BoundingBox = (%f,%f,%f,%f))",
               bounding_box.x1, bounding_box.y1, bounding_box.x2, bounding_box.y2));

  return(pclxl_new_sub_path(pclxl_context) &&
         pclxl_rectangle_path(pclxl_context, &bounding_box));
}

/*
 * In order to draw a round cornered rectangle
 * where the round corners are defined/drawn as quadrants of an ellipse
 * (i.e. drawn using bezier curves) we need a pair of "control points"
 *
 * The following functions are each given a single "corner" point
 * and a pair of radii from which they each calculate
 * one of 4 different control points
 * (which are essentially "due east", "due south", "due west" and
 * "due north" of the "corner" point respectively)
 *
 * Each function returns a boolean which indicates whether
 * a valid control point has been derived
 * (i.e. it is not coincident with the corner point).
 *
 * Only if *both* required control points are valid/non-coincident with
 * the corner point can we draw a curve.
 * Otherwise we must draw a straight line segment.
 */

static void
pclxl_east_control_point(PCLXL_SysVal_XY* point,
                         PCLXL_SysVal_XY* radii,
                         PCLXL_SysVal_XY* control_point)
{
  HQASSERT(radii->x, "Cannot calculate round [cornered] rectangle control point using a zero radius");

  control_point->x = (point->x + (radii->x * (1.0 - CIRCLE_FACTOR)));

  control_point->y = point->y;
}

static void
pclxl_west_control_point(PCLXL_SysVal_XY* point,
                         PCLXL_SysVal_XY* radii,
                         PCLXL_SysVal_XY* control_point)
{
  HQASSERT(radii->x, "Cannot calculate round [cornered] rectangle control point using a zero radius");

  control_point->x = (point->x - (radii->x * (1.0 - CIRCLE_FACTOR)));

  control_point->y = point->y;
}

static void
pclxl_north_control_point(PCLXL_SysVal_XY* point,
                          PCLXL_SysVal_XY* radii,
                          PCLXL_SysVal_XY* control_point)
{
  HQASSERT(radii->y, "Cannot calculate round [cornered] rectangle control point using a zero radius");

  control_point->x = point->x;

  control_point->y = (point->y - (radii->y * (1.0 - CIRCLE_FACTOR)));
}

static void
pclxl_south_control_point(PCLXL_SysVal_XY* point,
                          PCLXL_SysVal_XY* radii,
                          PCLXL_SysVal_XY* control_point)
{
  HQASSERT(radii->y, "Cannot calculate round [cornered] rectangle control point using a zero radius");

  control_point->x = point->x;

  control_point->y = (point->y + (radii->y * (1.0 - CIRCLE_FACTOR)));
}

static Bool
pclxl_round_rectangle_path(PCLXL_CONTEXT pclxl_context,
                           PCLXL_SysVal_Box* bbox,
                           PCLXL_SysVal_XY* diameters,
                           PCLXL_SysVal_XY* path_end_point)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY cp1;
  PCLXL_SysVal_XY cp2;

  PCLXL_SysVal_XY tl;
  PCLXL_SysVal_XY bl;
  PCLXL_SysVal_XY tr;
  PCLXL_SysVal_XY br;

  PCLXL_SysVal_XY radii;

  /*
   * For our convenience let's convert the bounding box
   * into four separate coordinates
   */

  tl.x = bbox->x1;
  tl.y = bbox->y1;

  br.x = bbox->x2;
  br.y = bbox->y2;

  /*
   * Let's "normalize" the top left and bottom right points
   * such that bottom right is indeed below and to the right of
   * the supposed top left
   */

  if ( br.x < tl.x ) { PCLXL_SysVal x = tl.x; tl.x = br.x; br.x = x; }
  if ( br.y < tl.y ) { PCLXL_SysVal y = tl.y; tl.y = br.y; br.y = y; }

  if ( (diameters->x < 0.0) ||
       (diameters->x > (br.x - tl.x)) ||
       (diameters->y < 0.0) ||
       (diameters->y > (br.y - tl.y)) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                               ("Round corner diameters %f and %f must be in the range 0.0 to half of bounding box width %f and height %f",
                                diameters->x,
                                diameters->y,
                                (br.x - tl.x),
                                (br.y - tl.y)));

    return FALSE;
  }

  /*
   * Note that we always return the first bounding box coordinate as the final path end point,
   * even if this coordinate is not actually at the top-left
   */

  path_end_point->x = bbox->x1;
  path_end_point->y = bbox->y1;

  if ( !graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so any and all paths will also be "degenerate"
     * which in the core rip are represented as a NULL path (list)
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  if ( (diameters->x == 0) || (diameters->y == 0) )
  {
    /*
     * One (or both) of the round corner diameters is zero
     * This means that we won't actually be drawing a *round* corner
     * So we can short-circuit the computation of corner control points
     * and draw a square-cornered rectangle instead
     */

    return pclxl_rectangle_path(pclxl_context, bbox);
  }

  /*
   * From this point onwards we *know* that we are definitely going to
   * draw non-degenerate rounded corners between the straight line edge segments.
   *
   * So we can go ahead and determine bottom-left and top-right coordinates,
   * radii and thus control points and then go ahead and draw the line and arc segments
   * that form the round [cornered] rectangle.
   */

  bl.x = tl.x;
  bl.y = br.y;

  tr.x = br.x;
  tr.y = tl.y;

  radii.x = (diameters->x / 2);
  radii.y = (diameters->y / 2);

  if ( !pclxl_moveto(pclxl_context, (tl.x), tl.y + radii.y) )
    return FALSE;

  pclxl_north_control_point(&bl, &radii, &cp1);
  pclxl_east_control_point(&bl, &radii, &cp2);

  /*
   * Yes, we have both control points
   * so we can attempt to draw a round (curved) corner
   */

  if ( !pclxl_ps_lineto(pclxl_context, tl.x, (bl.y - radii.y)) ||
       !pclxl_ps_curveto(pclxl_context, cp1.x, cp1.y, cp2.x, cp2.y,
                         (bl.x + radii.x), bl.y) )
    return FALSE;

  pclxl_west_control_point(&br, &radii, &cp1);
  pclxl_north_control_point(&br, &radii, &cp2);

  /*
   * Yes, we have both control points
   * so we can attempt to draw a round (curved) corner
   */

  if ( !pclxl_ps_lineto(pclxl_context, (br.x - radii.x), br.y) ||
       !pclxl_ps_curveto(pclxl_context, cp1.x, cp1.y, cp2.x, cp2.y,
                         br.x, (br.y - radii.y)) )
    return FALSE;

  pclxl_south_control_point(&tr, &radii, &cp1);
  pclxl_west_control_point(&tr, &radii, &cp2);

  /*
   * Yes, we have both control points
   * so we can attempt to draw a round (curved) corner
   */

  if ( !pclxl_ps_lineto(pclxl_context, tr.x, (tr.y + radii.y)) ||
       !pclxl_ps_curveto(pclxl_context, cp1.x, cp1.y, cp2.x, cp2.y,
                         (tr.x - radii.x), tr.y) )
    return FALSE;

  pclxl_east_control_point(&tl, &radii, &cp1);
  pclxl_south_control_point(&tl, &radii, &cp2);

  /*
   * Yes, we have both control points
   * so we can attempt to draw a round (curved) corner
   */

  if ( !pclxl_ps_lineto(pclxl_context, (tl.x + radii.x), tl.y) ||
       !pclxl_ps_curveto(pclxl_context, cp1.x, cp1.y, cp2.x, cp2.y,
                         tl.x, (tl.y + radii.y)) )
    return FALSE;

  /*
   * According to the PCLXL "spec" rectangle paths are closed (sub) paths
   */

  if ( !pclxl_close_sub_path(graphics_state) )
    return FALSE;


  /*
   * Well we appear to have successfully drawn all
   * the various segments of this round (cornered) rectangle
   *
   * So we return true so that the caller knows this too
   * (and can therefore log the successful PCLXL operator)
   */

  return TRUE;
}

/*
 * Tag 0xa2 RoundRectangle
 */

static
PCLXL_ATTR_MATCH round_rect_match[3] = {
#define ROUNDRECT_BOUNDING_BOX      (0)
  {PCLXL_AT_BoundingBox | PCLXL_ATTR_REQUIRED},
#define ROUNDRECT_ELLIPSE_DIMENSION (1)
  {PCLXL_AT_EllipseDimension | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};
Bool
pclxl_op_round_rectangle(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY ellipse_dimensions;
  PCLXL_SysVal_XY saved_point;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, round_rect_match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(round_rect_match[ROUNDRECT_BOUNDING_BOX].result, &bounding_box);
  /* EllipseDimension */
  pclxl_attr_get_real_xy(round_rect_match[ROUNDRECT_ELLIPSE_DIMENSION].result, &ellipse_dimensions);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("RoundRectange(BoundingBox = (%f,%f,%f,%f), EllipseDimension = (%f,%f))",
               bounding_box.x1, bounding_box.y1, bounding_box.x2, bounding_box.y2,
               ellipse_dimensions.x, ellipse_dimensions.y));

  return(pclxl_new_path(graphics_state) &&
         pclxl_round_rectangle_path(pclxl_context, &bounding_box,
                                    &ellipse_dimensions, &saved_point) &&
         pclxl_paint_path(pclxl_context) &&
         pclxl_new_path(graphics_state) &&
         pclxl_restore_current_point(pclxl_context, &saved_point));
}

/*
 * Tag 0xa3 RoundRectanglePath
 */

Bool
pclxl_op_round_rectangle_path(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_Box bounding_box;
  PCLXL_SysVal_XY ellipse_dimensions;
  PCLXL_SysVal_XY end_point;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, round_rect_match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* BoundingBox */
  pclxl_attr_get_real_box(round_rect_match[ROUNDRECT_BOUNDING_BOX].result, &bounding_box);
  /* EllipseDimension */
  pclxl_attr_get_real_xy(round_rect_match[ROUNDRECT_ELLIPSE_DIMENSION].result, &ellipse_dimensions);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("RoundRectangePath(BoundingBox = (%f,%f,%f,%f), EllipseDimension = (%f,%f))",
               bounding_box.x1, bounding_box.y1, bounding_box.x2, bounding_box.y2,
               ellipse_dimensions.x, ellipse_dimensions.y));

  return(pclxl_new_sub_path(pclxl_context) &&
         pclxl_round_rectangle_path(pclxl_context, &bounding_box,
                                    &ellipse_dimensions, &end_point) &&
         pclxl_restore_current_point(pclxl_context, &end_point));
}

/*
 * Tag 0x68
 */

Bool
pclxl_op_set_clip_rectangle(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[3] = {
#define SETCLIPREGION_CLIP_REGION (0)
    {PCLXL_AT_ClipRegion | PCLXL_ATTR_REQUIRED},
#define SETCLIPREGION_BOUNDING_BOX (1)
    {PCLXL_AT_BoundingBox | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_clip_region_values[] = {
    PCLXL_eInterior,
    PCLXL_eExterior,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ClipRegion clip_region;
  PCLXL_SysVal_Box bounding_box;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ClipRegion */
  if ( !pclxl_attr_match_enumeration(match[SETCLIPREGION_CLIP_REGION].result,
                                     allowed_clip_region_values, &clip_region,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  /* BoundingBox */
  pclxl_attr_get_real_box(match[SETCLIPREGION_BOUNDING_BOX].result, &bounding_box);

  /* pre-conditions */
  if ( (clip_region == PCLXL_eExterior) &&
       (graphics_state->clip_mode != PCLXL_eEvenOdd) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_CLIP_MODE_MISMATCH,
                        (("Exterior clip region must not use NZ clip rule")));
    return(FALSE);
  }


  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("SetClipRectangle(BoundingBox = (%f,%f,%f,%f))",
               bounding_box.x1,
               bounding_box.y1,
               bounding_box.x2,
               bounding_box.y2));

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("SetClipRectangle(ClipRegion = (%s))",
               clip_region == PCLXL_eExterior ? "Exterior" : "Interior"));

  /* Postconditions:
   * current path is destroyed.
   * clip path is now result of intersection of the clip path on function
   * entry with the rectangle.
   */

  /* We can re-use the path drawing operations as paths for clipping are
   * to be constructred in same manner as paths to be drawn.
   */
  return(pclxl_new_path(graphics_state) &&
         /* PCL XL Compatibility. The reference printer appears to
          * treat the SetClipRectangle as a "replace" operation on
          * the current clip path, rather than the "intersect" op
          * operation specified in the PCL XL documentation.
          */
         gs_ctop() && gs_cpush() &&
         pclxl_rectangle_path(pclxl_context, &bounding_box) &&
         pclxl_ps_clip(pclxl_context, clip_region == PCLXL_eExterior,
                       graphics_state->clip_mode == PCLXL_eEvenOdd) &&
         pclxl_new_path(graphics_state));
}

/*
 * Tag 0x62
 */

static
PCLXL_ATTR_MATCH clip_region_match[2] = {
#define CLIPREGION_CLIP_REGION  (0)
  {PCLXL_AT_ClipRegion | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};

Bool
pclxl_op_set_clip_replace(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ENUMERATION allowed_clip_region_values[] = {
    PCLXL_eInterior,
    PCLXL_eExterior,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ClipRegion clip_region;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, clip_region_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ClipRegion */
  if ( !pclxl_attr_match_enumeration(clip_region_match[CLIPREGION_CLIP_REGION].result,
                                     allowed_clip_region_values, &clip_region,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* pre-conditions */
  if ( (clip_region == PCLXL_eExterior) &&
       (graphics_state->clip_mode != PCLXL_eEvenOdd) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_CLIP_MODE_MISMATCH,
                        (("Exterior clip region must not use NZ clip rule")));
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("SetClipReplace"));

  /* clip is capable of dealing with an empty clip path, so it
   * is not an error here to have an empty clip path.
   * As we are replacing the clip path, we need to do re-initialize
   * the clip path back to be the current page clip
   * then clip against the current path. */

  /* Postconditions:
   * current path is destroyed.
   * clip path is now result of intersection of the clip path on function
   * entry with the current path.
   */

  return ( gs_ctop() &&
           gs_cpush() &&
           pclxl_ps_clip(pclxl_context, clip_region == PCLXL_eExterior,
                         graphics_state->clip_mode == PCLXL_eEvenOdd) &&
           pclxl_new_path(graphics_state));
}

/*
 * Tag 0x67
 */

Bool
pclxl_op_set_clip_intersect(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ENUMERATION allowed_clip_region_values[] = {
    PCLXL_eInterior,
    PCLXL_eExterior,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ClipRegion clip_region;

  if ( !pclxl_attr_set_match(parser_context->attr_set, clip_region_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ClipRegion */
  if ( !pclxl_attr_match_enumeration(clip_region_match[CLIPREGION_CLIP_REGION].result,
                                     allowed_clip_region_values, &clip_region,
                                     parser_context->pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* pre-conditions */
  if ( (clip_region == PCLXL_eExterior) &&
       (graphics_state->clip_mode != PCLXL_eEvenOdd) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        (("Exterior clip region must not use NZ clip rule")));
    return FALSE;
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS),
              ("SetClipIntersect"));

  /*
   * clip is capable of dealing with an empty clip path, so it
   * is not an error here to have an empty clip path.
   *
   * Postconditions:
   * current path is destroyed.
   * clip path is now result of intersection of the clip path on function
   * entry with the current path.
   *
   * Compatibility Issue: It appears that our reference printers
   * do not support intersecting an existing clip path with the
   * *exterior* of a new path.
   * Instead it appears that this exterior path *replaces* the existing
   * clip path
   */

  if ( (clip_region == PCLXL_eExterior) &&
       !(gs_ctop() && gs_cpush()) ) {
    /** \todo Add XL error */
    return(FALSE);
  }

  return(pclxl_ps_clip(pclxl_context,
                       clip_region == PCLXL_eExterior,
                       graphics_state->clip_mode == PCLXL_eEvenOdd) &&
         pclxl_new_path(graphics_state));
}

Bool
pclxl_set_clip_to_page(PCLXL_GRAPHICS_STATE graphics_state)
{
  /* assumption is that the gstate always has a clip path of some form. */
  return (gs_ctop() &&
          gs_cpush() &&
          pclxl_new_path(graphics_state)
         );
}

/*
 * Tag 0x69
 */

Bool
pclxl_op_set_clip_to_page(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_PATHS), ("SetClipToPage"));

  return pclxl_set_clip_to_page(pclxl_context->graphics_state);
}

/*
 * Tag 0x7f
 */

Bool
pclxl_op_set_clip_mode(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCLIPMODE_CLIP_MODE   (0)
    {PCLXL_AT_ClipMode | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_clip_mode_values[] = {
    PCLXL_eNonZeroWinding,
    PCLXL_eEvenOdd,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ClipMode clip_mode;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ClipMode */
  if ( !pclxl_attr_match_enumeration(match[SETCLIPMODE_CLIP_MODE].result,
                                     allowed_clip_mode_values, &clip_mode,
                                     pclxl_context, PCLXL_SS_STATE) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetClipMode %d", clip_mode));

  graphics_state->clip_mode = clip_mode;
  return(TRUE);
}

/*
 * Tag 0xb6
 */

/* the manangement of state required for PCL XL scan lines is incorporated
 * into the gstate of the interpreter.
 */
Bool
pclxl_op_begin_scan(PCLXL_PARSER_CONTEXT parser_context)
{
  SYSTEMVALUE x = 0, y = 1;
  SYSTEMVALUE pen_width;
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  /* we need to side-effect the linecap, and linewidth. We can apply a gsave
   * at this point to preserve other parts of the gstate. The current path
   * will be empty when EndScan is called, and the required operator sequence
   * only allows BeginScanRel operators between the BeginScan and EndScan,
   * thus there is no means of changing anything other than the current point
   * in the gsate (set explicitly via EndScan).
   */
  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
      ("BeginScan"));

  /* calculate if scan line is more than one device pixel high. Scan lines
   * are drawn along the y axis.  If line is not a device pixel high, draw
   * 0 width lines.
   */
  /** \todo would stroke adjustment be applicable to this? */

  MATRIX_TRANSFORM_DXY(x, y, x, y, &theIgsPageCTM( gstateptr));
  if ( (x * x) + ( y * y) > 1.0 )
    pen_width = 1;
  else
    pen_width = 0;

  /* Postconditions:
   *   The gstate ( not the PCLXL graphics state structure ) is updated with:
   *   linewidth is 1 user unit, or 0 user units for very narrow lines.
   *   linecaps are butt caps.
   */
  /** \todo
   *  It is not specified whether the scan lines use
   *  the BrushSource or PenSource for their color/pattern etc.
   *  But I have now empirically determined that it must use the PenSource
   */

  /* Need to be clear what we are sideffecting here. The PS gstate and the
   * PCLXL graphics state are sepearate but connected. Don't mix up
   * calls to change PS gstate with calls to change PCLXL graphics state.
   * We'll set up the gstate for drawing the scan lines and leave the
   * PCLXL gstate unchanged.
   * I expect error handler to deal with any grestores required to re-establish
   * the PCLXL graphics state.
   */

  return ( gs_gpush(GST_GSAVE) &&
           pclxl_ps_set_line_width(pen_width) &&
           pclxl_ps_set_color(pclxl_context,
                              &graphics_state->line_style.pen_source, FALSE /* For an image? */) &&
           pclxl_new_path(graphics_state) &&
           pclxl_ps_set_line_cap(graphics_state, PCLXL_eButtCap) );
}

/*
 * Tag 0xb8
 */

Bool
pclxl_op_end_scan(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  ps_context_t *pscontext ;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
      ("EndScan"));

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  /*
   * Undo the changes we made to current color, pen width and line endings/caps.
   * Postcondition:
   * Clear current path.
   * Note that we *must* update *both* the Postscript context
   * (done by a grestore_() of the private-to-scan-line-operation)
   * and also clear any notion of a path from the PCLXL graphics state too
   */
  return (grestore_(pscontext) &&
          pclxl_new_path(pclxl_context->graphics_state));
}

/* construct and stroke the path for a single scan line, reading from the
 * data source specified.
 * the y_coord for the scan line defines a boundary, not a center line, but
 * otherwise scan lines can be seen as strokes.
 */
static
Bool pclxl_scan_line(
  PCLXL_PARSER_CONTEXT    parser_context,
  PCLXL_EMBEDDED_READER*  p_embedded_reader)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  int32 start_point[2];
  PCLXL_SysVal x_start;
  PCLXL_SysVal y_start;
  PCLXL_SysVal x_end;
  int32 x_pair_count;
  int32 x_pair_data_type;

  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return(FALSE);
  }

  /* read X,Y start. */
  if ( !pclxl_embedded_read_data(p_embedded_reader, XL_DATATYPE_UINT16, start_point, 2) ) {
    if ( pclxl_embedded_insufficient(p_embedded_reader) ) {
      goto insufficient_data;
    }
    return(FALSE);
  }

  /* Scan line y coord defines an edge, not a center line. Offset by 0.5 to
   * position scan line correctly.
   */
  x_start = start_point[0];
  y_start = start_point[1] + 0.5;

  /* read number of x pairs. */
  if ( !pclxl_embedded_read_data(p_embedded_reader, XL_DATATYPE_UINT16, &x_pair_count, 1) ) {
    if ( pclxl_embedded_insufficient(p_embedded_reader) ) {
      goto insufficient_data;
    }
    return(FALSE);
  }
  if ( x_pair_count == 0 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_SCANLINE, PCLXL_ILLEGAL_DATA_VALUE,
                        ("Illegal number of scan line x-pairs."));
    return FALSE;
  }

  /* read data type for x pairs. */
  if ( !pclxl_embedded_read_data(p_embedded_reader, XL_DATATYPE_UBYTE, &x_pair_data_type, 1) ) {
    if ( pclxl_embedded_insufficient(p_embedded_reader) ) {
      goto insufficient_data;
    }
    return(FALSE);
  }
  if ( x_pair_data_type != PCLXL_eUByte && x_pair_data_type != PCLXL_eUInt16 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_SCANLINE, PCLXL_ILLEGAL_DATA_VALUE,
                        ("Illegal datatype for scan line x pairs."));
    return FALSE;
  }

  x_end = x_start;

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS|PCLXL_DEBUG_PATHS|PCLXL_DEBUG_EMBEDDED_DATA,
              ("ScanLine begin"));

  if (! pclxl_ps_set_color(pclxl_context,
                           &graphics_state->fill_details.brush_source, FALSE /* For an image? */) ||
      ! pclxl_ps_set_rop3(pclxl_context, graphics_state->ROP3, TRUE)) {
    return FALSE;
  }

  /* read each pair, and plot line defined. */
  do {
    int32 x_data[2];

    if ( !pclxl_embedded_read_data(p_embedded_reader, x_pair_data_type, x_data, 2) ) {
      if ( pclxl_embedded_insufficient(p_embedded_reader) ) {
        goto insufficient_data;
      }
      return(FALSE);
    }
    x_start += x_data[0];
    x_end += (x_data[0] + x_data[1]);

    if ( graphics_state->ctm_is_invertible &&
         (!pclxl_ps_moveto(pclxl_context, x_start, y_start) ||
          !pclxl_ps_lineto(pclxl_context, x_end, y_start)) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                          ("Failed to draw scan line %d %d to %d %d", x_start, y_start, x_end, y_start) );
      return FALSE;
    }

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS|PCLXL_DEBUG_PATHS |PCLXL_DEBUG_EMBEDDED_DATA,
                ("ScanLineSegment( (%f,%f) to (%f,%f)",x_start, y_start, x_end, y_start));

    x_start = x_end;

  } while ( --x_pair_count > 0 );

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS|PCLXL_DEBUG_PATHS|PCLXL_DEBUG_EMBEDDED_DATA,
              ("ScanLine end"));

  return (pclxl_ps_stroke(pclxl_context) &&
          pclxl_new_path(pclxl_context->graphics_state));

insufficient_data:
  PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_SCANLINE, PCLXL_MISSING_DATA,
                      ("Insufficient data"));
  return FALSE;
}


static Bool
pclxl_scan_line_block(PCLXL_PARSER_CONTEXT   parser_context,
                      uint32                 number_of_scan_lines)
{
  PCLXL_EMBEDDED_READER embedded_reader;
  int32  start_point_data_type;

  if ( !parser_context->data_source_open ) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_SCANLINE, PCLXL_DATA_SOURCE_NOT_OPEN,
                        ("Failed to read scan line"));
    return FALSE;
  }

  if ( !pclxl_stream_embedded_init(parser_context->pclxl_context,
                                   pclxl_parser_current_stream(parser_context),
                                   parser_context->data_source_big_endian,
                                   &embedded_reader) ) {
    return(FALSE);
  }
  if ( pclxl_embedded_length(&embedded_reader) == 0 ) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_SCANLINE, PCLXL_ILLEGAL_DATA_LENGTH,
                        ("No embedded data"));
    return(FALSE);
  }

  if ( !pclxl_embedded_read_data(&embedded_reader, XL_DATATYPE_UBYTE, &start_point_data_type, 1) ) {
    return(FALSE);
  }

  if ( start_point_data_type != XL_DATATYPE_SINT16 ) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_DATA_VALUE,
                        ("Scan line start points must be specified with SINT16 type"));
    return(FALSE);
  }

  do {
    if ( !pclxl_scan_line(parser_context, &embedded_reader) ) {
      return(FALSE);
    }
  } while ( --number_of_scan_lines > 0 );

  return(pclxl_embedded_flush(&embedded_reader));
}

/*
 * Tag 0xb9
 */

Bool
pclxl_op_scan_line_rel(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SCANLINEREL_NUMBER_OF_SCAN_LINES  (0)
    {PCLXL_AT_NumberOfScanLines},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  int32 number_of_scan_lines;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* NumberOfScanLines */
  number_of_scan_lines = 1;
  if ( match[SCANLINEREL_NUMBER_OF_SCAN_LINES].result ) {
    number_of_scan_lines = pclxl_attr_get_int(match[SCANLINEREL_NUMBER_OF_SCAN_LINES].result);

    if ( number_of_scan_lines == 0 ) {
      /* Cannot be more than 65535 since attribute data type is 16-bit */
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_SCANLINE, PCLXL_ILLEGAL_DATA_VALUE,
                          ("Illegal number of scan lines."));
      return(FALSE);
    }
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("ScanLineRel"));

  return(pclxl_scan_line_block(parser_context, number_of_scan_lines));

}

/******************************************************************************
* Log stripped */
