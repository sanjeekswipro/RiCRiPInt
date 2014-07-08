/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxltest.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains various wrappers around Postscript APIs
 * provided by the Core RIP including:
 *
 *  save()
 *  restore()
 *  run_ps_string()
 */

#include <stdlib.h>     /* for time(), rand() and srand() */

#include "core.h"

#ifdef DEBUG_BUILD

#include "namedef_.h"
#include "swcopyf.h"
#include "display.h"
#include "miscops.h"

#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlparsercontext.h"
#include "pclxlerrors.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlfont.h"

/*
 * x_inch(), y_inch(), x_mm() and y_mm()
 * are all convenience functions that calculate
 * the real-world sizes into PCLXL current-job "user" coordinates.
 *
 * This then allows the subsequent test (content)
 * to be expressed in these real-world units (if desired)
 * and thus drawn in a way that is "resolution-independent"
 */

static PCLXL_SysVal
x_inch(PCLXL_CONTEXT pclxl_context, PCLXL_SysVal inches)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  return (inches *
          pclxl_ps_units_per_pclxl_uom(PCLXL_eInch) *
          ((non_gs_state->current_media_details.orientation % 2) ?
           non_gs_state->units_per_measure.res_y :
           non_gs_state->units_per_measure.res_x) /
          graphics_state->page_scale.x /
          pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit));
}

static PCLXL_SysVal
y_inch(PCLXL_CONTEXT pclxl_context, PCLXL_SysVal inches)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  return (inches *
          pclxl_ps_units_per_pclxl_uom(PCLXL_eInch) *
          non_gs_state->units_per_measure.res_y /
          ((non_gs_state->current_media_details.orientation % 2) ?
           non_gs_state->units_per_measure.res_x :
           non_gs_state->units_per_measure.res_y) /
          graphics_state->page_scale.y /
          pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit));
}

static PCLXL_SysVal
x_mm(PCLXL_CONTEXT pclxl_context, PCLXL_SysVal mm)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  return (mm *
          pclxl_ps_units_per_pclxl_uom(PCLXL_eMillimeter) *
          ((non_gs_state->current_media_details.orientation % 2) ?
           non_gs_state->units_per_measure.res_y :
           non_gs_state->units_per_measure.res_x) /
          graphics_state->page_scale.x /
          pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit));
}

static PCLXL_SysVal
y_mm(PCLXL_CONTEXT pclxl_context, PCLXL_SysVal mm)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  return (mm *
          pclxl_ps_units_per_pclxl_uom(PCLXL_eMillimeter) *
          ((non_gs_state->current_media_details.orientation % 2) ?
           non_gs_state->units_per_measure.res_x :
           non_gs_state->units_per_measure.res_y) /
          graphics_state->page_scale.y /
          pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit));
}

static PCLXL_SysVal
random_0_to_1(PCLXL_CONTEXT pclxl_context)
{
  static unsigned int seed = 0;

  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  if ( non_gs_state->page_number != seed ) srand(seed = non_gs_state->page_number);

  return ((PCLXL_SysVal) rand() / (PCLXL_SysVal) RAND_MAX);
}

Bool
pclxl_dot(PCLXL_CONTEXT pclxl_context,
          PCLXL_SysVal x,
          PCLXL_SysVal y,
          uint8 label_position,
          uint8* label_text)
{
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

  static PCLXL_Int32 label_rotations[5] = { 0, 0, 90, 180, 270 };
  PCLXL_SysVal dot_radius = x_mm(pclxl_context, 0.5);
  PCLXL_SysVal label_y_offset = x_mm(pclxl_context, 1.0);

  uint8  ps_string[512];

  (void) pclxl_set_default_font(pclxl_context,
                                (uint8*) "Courier       Bd",
                                16,
                                10,
                                config_params->default_symbol_set,
                                FALSE);

  /*
   * A label_position of 0 means "no label"
   * 1 = along the positive X axis
   * 2 = along the negative (in PCLXL terms) Y axis
   * 3 = along the negative X axis
   * 4 = along the positive (in PCLXL terms) Y axis
   */

  label_position = (label_position % 5);

  if ( label_position )
  {
    if ( label_text )
    {
      (void) swncopyf(ps_string,
                      sizeof(ps_string),
                      (uint8*)
" gsave "
" 0 setgray "
" newpath "
" %f %f %f 0 360 arc closepath fill "
" %f %f moveto "
" %d rotate "
" 0 %f rmoveto "
" (\\(%s\\)) show "
" grestore ",
                      x, y, dot_radius,
                      x, y,
                      label_rotations[label_position],
                      label_y_offset,
                      label_text);
    }
    else
    {
      (void) swncopyf(ps_string,
                      sizeof(ps_string),
                      (uint8*)
" gsave "
" 0 setgray "
" newpath "
" %f %f %f 0 360 arc closepath fill "
" %f %f moveto "
" %d rotate "
" 0 %f rmoveto "
" (\\(%d,%d\\)) show "
" grestore ",
                      x, y, dot_radius,
                      x, y,
                      label_rotations[label_position],
                      label_y_offset,
                      (int32) x, (int32) y);
    }
  }
  else
  {
    (void) swncopyf(ps_string,
                    sizeof(ps_string),
                    (uint8*)
" gsave "
" 0 setgray "
" newpath "
" %f %f %f 0 360 arc closepath fill "
" grestore ",
                    x, y, dot_radius);
  }

  return pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));
}

Bool
pclxl_debug_origin(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

  PCLXL_SysVal line_width = x_mm(pclxl_context, 0.2);
  PCLXL_SysVal axis_len = x_mm(pclxl_context, 8);
  PCLXL_SysVal circle_radius = x_mm(pclxl_context, 3.5);
  PCLXL_SysVal arrow_head_len = x_mm(pclxl_context, 1.5);
  PCLXL_SysVal label_x_offset = (axis_len / 6);
  PCLXL_SysVal label_y_offset = x_mm(pclxl_context, 1.2);

  uint8 ps_string[1024];

  (void) pclxl_set_default_font(pclxl_context,
                                (uint8*) "Courier       Bd",
                                16,
                                12,
                                config_params->default_symbol_set,
                                FALSE);

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " gsave "
                  " 1 setgray "
                  " %f %f %f %f rectfill "

                  " 0 setgray "
                  " %f setlinewidth "

                  " %f 0 moveto %f 0 lineto stroke "
                  " 0 %f moveto 0 %f lineto stroke "

                  " 0 0 %f 0 360 arc closepath stroke "

                  " %f %f moveto %f %f lineto %f %f lineto stroke "

                  " %f %f moveto %f %f lineto %f %f lineto stroke "

                  " %f %f moveto (+X) show "

                  " 90 rotate "

                  " %f %f moveto (+Y) show "

                  " grestore ",

                  (- axis_len), (- axis_len),
                  (2 * axis_len), (2 * axis_len),

                  line_width,

                  (- axis_len), axis_len,
                  (- axis_len), axis_len,

                  circle_radius,

                  (axis_len - arrow_head_len), arrow_head_len,
                  axis_len, 0.0,
                  (axis_len - arrow_head_len), (- arrow_head_len),

                  (- arrow_head_len), (axis_len - arrow_head_len),
                  0.0, axis_len,
                  arrow_head_len, (axis_len - arrow_head_len),

                  label_x_offset, label_y_offset,

                  label_x_offset, label_y_offset);

  return pclxl_ps_run_ps_string(pclxl_context, ps_string,
                                strlen((char*) ps_string));
}

Bool
pclxl_hello_world(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_SysVal x = x_inch(pclxl_context, 1.0);
  PCLXL_SysVal y = y_inch(pclxl_context, 3.0);
  uint8* hello_world = (uint8*) "Hello World";

  uint8 ps_string[64];

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " %f %f moveto (%s) show ",
                  x, y, hello_world);

  return pclxl_ps_run_ps_string(pclxl_context, ps_string,
                                strlen((char*) ps_string));
}

/*
 * pclxl_test_page_1() draws
 * (using Postscript directly)
 * a trivial test page that contains some text,
 * an outline shape and a filled shape
 * with a border around these that follows the dimensions and orientation
 * of the current page.
 *
 * It was mostly used to check the PCLXL page orientation CTM implementation
 */

Bool
pclxl_test_page_1(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

  PCLXL_SysVal page_width = graphics_state->page_width;
  PCLXL_SysVal page_height = graphics_state->page_height;

  PCLXL_SysVal line_width = x_mm(pclxl_context, 2);
  PCLXL_SysVal margin = x_mm(pclxl_context, 5);

  PCLXL_SysVal text_x = x_inch(pclxl_context, 1);
  PCLXL_SysVal text_y = y_inch(pclxl_context, 1);
  uint8  page_number_and_orientation[128];
  uint8*  orientation_strings[] =
  {
    (uint8*) "Portrait",
    (uint8*) "Landscape",
    (uint8*) "Reverse Portrait",
    (uint8*) "Reverse Landscape"
  };

  PCLXL_SysVal triangle_x = x_inch(pclxl_context, 1);
  PCLXL_SysVal triangle_y = y_inch(pclxl_context, 2);
  PCLXL_SysVal triangle_width = x_mm(pclxl_context, 40);
  PCLXL_SysVal triangle_height = y_mm(pclxl_context, 30);

  PCLXL_SysVal circle_cx = x_inch(pclxl_context, 2);
  PCLXL_SysVal circle_cy = y_inch(pclxl_context, 5);
  PCLXL_SysVal circle_radius = x_inch(pclxl_context, 1);

  uint8 ps_string[1024];

  (void) pclxl_dot(pclxl_context, 0.0, 0.0, 4, NULL);

  (void) pclxl_dot(pclxl_context, page_width, page_height, 1, NULL);

  (void) pclxl_set_default_font(pclxl_context,
                                (uint8*) "Courier         ",
                                16,
                                24,
                                config_params->default_symbol_set,
                                FALSE);

  (void) swncopyf(page_number_and_orientation,
                  sizeof(page_number_and_orientation),
                  (uint8*) "Page %d \\(%s\\)",
                  non_gs_state->page_number,
                  orientation_strings[non_gs_state->current_media_details.orientation]);

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*)
/* Draw a black border just inside the edge of the page */
" %f setlinewidth "
" 0 setgray "
" %f %f moveto "
" %f %f lineto "
" %f %f lineto "
" %f %f lineto "
" %f %f lineto "
" closepath stroke "
/* Draw a green 3/4/5 triangle outline 2" down the page */
" 0 1 0 setrgbcolor "
" %f %f moveto "
" %f %f lineto "
" %f %f lineto "
" closepath stroke "
/* Draw a filled random colour circle 5" down the page */
" %f %f %f setrgbcolor "
" %f %f moveto "
" %f %f %f 0 360 arc closepath fill "
/* Draw a blue "Page Number <n> (<Orientation>)" 1" down the page */
" 0 0 1 setrgbcolor "
" %f %f moveto "
" (%s) show ",
                  line_width,
                  margin, margin,
                  margin, (page_height - margin),
                  (page_width - margin), (page_height - margin),
                  (page_width - margin), margin,
                  margin, margin,

                  triangle_x, triangle_y,
                  triangle_x, (triangle_y + triangle_height),
                  (triangle_x + triangle_width), (triangle_y + triangle_height),

                  random_0_to_1(pclxl_context), random_0_to_1(pclxl_context), random_0_to_1(pclxl_context),
                  circle_cx, circle_cy,
                  circle_cx, circle_cy, circle_radius,

                  text_x, text_y,
                  page_number_and_orientation);

  return pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));
}

/*
 * pclxl_mm_graph_paper() draws a simple millimetre grid
 * across the entire page from top-left (0,0)
 * to bottom right (<page_width>, <page_height>)
 * with thin green lines each millimeter in each direction
 * and thicker lines each centimetre
 *
 * This was principly used during the cursor positioning
 * and line/path drawing testing
 */

Bool
pclxl_mm_graph_paper(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  Bool landscape = (non_gs_state->current_media_details.orientation % 2);
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal mm_x = (landscape ? y_mm(pclxl_context, 1) : x_mm(pclxl_context, 1));
  PCLXL_SysVal mm_y = (landscape ? x_mm(pclxl_context, 1) : y_mm(pclxl_context, 1));

  PCLXL_SysVal mm_x_line_width = mm_x / 50;
  PCLXL_SysVal mm_y_line_width = mm_y / 50;

  uint8* mm_line_colour = (uint8*) " 0 1 0 setrgbcolor ";

  PCLXL_SysVal cm_x = mm_x * 10;
  PCLXL_SysVal cm_y = mm_y * 10;

  PCLXL_SysVal cm_x_line_width = mm_x / 20;
  PCLXL_SysVal cm_y_line_width = mm_y / 20;

  uint8* cm_line_colour = (uint8*) " 0 1 0 setrgbcolor ";
  /* uint8* cm_line_colour = (uint8*) " 1 0 0 setrgbcolor "; */

  PCLXL_SysVal x;

  PCLXL_SysVal y;

  uint8 ps_string[512];

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*)
" %s "
" %f setlinewidth",
                  mm_line_colour,
                  mm_y_line_width
                 );

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));

  for ( y = 0.0 ;
        y < graphics_state->page_height ;
        y += mm_y )
  {
    (void) swncopyf(ps_string,
                    sizeof(ps_string),
                    (uint8*)
" 0 %f moveto "
" %f %f lineto "
" closepath stroke ",
                    y, graphics_state->page_width, y
                   );

    (void) pclxl_ps_run_ps_string(pclxl_context,
                                  ps_string,
                                  strlen((char*) ps_string));
  }

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*)
" %f setlinewidth",
                  mm_x_line_width
                 );

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));
  for ( x = 0.0 ;
        x < graphics_state->page_width ;
        x += mm_x )
  {
    (void) swncopyf(ps_string,
                    sizeof(ps_string),
                    (uint8*)
" %f 0 moveto "
" %f %f lineto "
" closepath stroke ",
                    x, x, graphics_state->page_height
                   );

    (void) pclxl_ps_run_ps_string(pclxl_context,
                                  ps_string,
                                  strlen((char*) ps_string));
  }

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*)
" %s "
" %f setlinewidth",
                  cm_line_colour,
                  cm_y_line_width
                 );

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));

  for ( y = 0.0 ;
        y < graphics_state->page_height ;
        y += cm_y )
  {
    (void) swncopyf(ps_string,
                    sizeof(ps_string),
                    (uint8*)
" 0 %f moveto "
" %f %f lineto "
" closepath stroke ",
                    y, graphics_state->page_width, y
                   );

    (void) pclxl_ps_run_ps_string(pclxl_context,
                                  ps_string,
                                  strlen((char*) ps_string));
  }

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*)
" %f setlinewidth",
                  cm_x_line_width
                 );

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));
  for ( x = 0.0 ;
        x < graphics_state->page_width ;
        x += cm_x )
  {
    (void) swncopyf(ps_string,
                    sizeof(ps_string),
                    (uint8*)
" %f 0 moveto "
" %f %f lineto "
" closepath stroke ",
                    x, x, graphics_state->page_height
                   );

    (void) pclxl_ps_run_ps_string(pclxl_context,
                                  ps_string,
                                  strlen((char*) ps_string));
  }

  return TRUE;
}

Bool
pclxl_debug_elliptical_arc(PCLXL_CONTEXT pclxl_context,
                           PCLXL_SysVal_XY* tl,
                           PCLXL_SysVal_XY* bl,
                           PCLXL_SysVal_XY* br,
                           PCLXL_SysVal_XY* tr,
                           PCLXL_SysVal_XY* c,
                           PCLXL_SysVal_XY* cr,
                           PCLXL_SysVal_XY* ct,
                           PCLXL_SysVal_XY* cl,
                           PCLXL_SysVal_XY* cb,
                           PCLXL_SysVal_XY* p3,
                           PCLXL_SysVal_XY* p4,
                           PCLXL_SysVal_XY* sp,
                           PCLXL_SysVal_XY* ep)
{
  uint8 ps_string[256];

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " 0.5 setgray %f setlinewidth ",
                  x_mm(pclxl_context, 1.0));

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " %f %f moveto %f %f lineto %f %f lineto %f %f lineto closepath ",
                  tl->x, tl->y,
                  bl->x, bl->y,
                  br->x, br->y,
                  tr->x, tr->y);

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " %f %f moveto %f %f lineto %f %f moveto %f %f lineto ",
                  c->x, c->y,
                  p3->x, p3->y,
                  c->x, c->y,
                  p4->x, p4->y);

  (void) pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));

  (void) pclxl_dot(pclxl_context, tl->x, tl->y, 1, NULL);
  (void) pclxl_dot(pclxl_context, bl->x, bl->y, 3, NULL);
  (void) pclxl_dot(pclxl_context, br->x, br->y, 4, NULL);
  (void) pclxl_dot(pclxl_context, tr->x, tr->y, 2, NULL);
  (void) pclxl_dot(pclxl_context, c->x,  c->y,  2, NULL);
  (void) pclxl_dot(pclxl_context, cr->x, cr->y, 2, NULL);
  (void) pclxl_dot(pclxl_context, ct->x, ct->y, 2, NULL);
  (void) pclxl_dot(pclxl_context, cl->x, cl->y, 1, NULL);
  (void) pclxl_dot(pclxl_context, cb->x, cb->y, 4, NULL);
  (void) pclxl_dot(pclxl_context, p3->x, p3->y, 1, NULL);
  (void) pclxl_dot(pclxl_context, p4->x, p4->y, 1, NULL);
  (void) pclxl_dot(pclxl_context, sp->x, sp->y, 1, NULL);
  (void) pclxl_dot(pclxl_context, ep->x, ep->y, 1, NULL);

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " newpath %f %f moveto ",
                  c->x,
                  c->y);

  return pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                strlen((char*) ps_string));
}

void
pclxl_debug_bezier_curve(PCLXL_CONTEXT pclxl_context,
                         PCLXL_SysVal_XY* sp,
                         PCLXL_SysVal_XY* cp1,
                         PCLXL_SysVal_XY* cp2,
                         PCLXL_SysVal_XY* ep)
{
  (void) pclxl_dot(pclxl_context, sp->x,  sp->y,  2, (uint8*) "sp");
  (void) pclxl_dot(pclxl_context, cp1->x, cp1->y, 2, (uint8*) "cp1");
  (void) pclxl_dot(pclxl_context, cp2->x, cp2->y, 2, (uint8*) "cp2");
  (void) pclxl_dot(pclxl_context, ep->x,  ep->y,  2, (uint8*) "ep");
}

#else /* DEBUG_BUILD */

Bool
pclxl_test()
{
  return FALSE;
}

#endif /* DEBUG_BUILD */

/******************************************************************************
* Log stripped */
