/** \file
 * \ingroup hpg2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2linefill.c(EBDSDK_P.1) $
 * $Id: src:hpgl2linefill.c,v 1.101.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Line and fill attributes Group" category.
 *
 *   AC    % Anchor Corner
 *   FT    % Fill Type
 *   LA    % Line Attributes
 *   LT    % Line Type
 *   PW    % Pen Width
 *   RF    % Raster Fill Definition
 *   SM    % Symbol Mode
 *   SP    % Select Pen
 *   SV    % Screened Vectors
 *   TR    % Transparency Mode
 *   UL    % User-defined Line Type
 *   WU    % Pen Width Unit Selection
 */

#include "core.h"
#include "hpgl2linefill.h"

#include "pcl5scan.h"
#include "pcl5context_private.h"
#include "hpgl2scan.h"
#include "hpgl2config.h"
#include "hpgl2vector.h"
#include "pclGstate.h"
#include "printmodel.h"
#include "pcl5ctm.h"
#include "pictureframe.h"
#include "pcl5color.h"
#include "hpgl2misc.h"
#include "resourcecache.h"

#include "objects.h"
#include "ascii.h"
#include "fileio.h"
#include "graphics.h" /* setting line width */
#include "gstack.h"
#include "gstate.h"
#include "pathcons.h" /* gs_currentpoint */
#include "constant.h" /* EPSILON */
#include "gu_cons.h"
#include "clipops.h"
#include "gu_path.h"
#include "gu_fills.h"
#include "gs_color.h"
#include "gschead.h"
#include "hqmemset.h"

static void hpgl2_sync_miterlimit(HPGL2LineFillInfo *linefill_info);
static void hpgl2_sync_lineend(HPGL2LineFillInfo *linefill_info) ;
static void hpgl2_sync_linejoin(HPGL2LineFillInfo *linefill_info) ;
static Bool hpgl2_sync_line_type(PCL5Context *pcl5_ctxt,
                                 HPGL2LineTypeInfo *line_type);
static void hpgl2_anchor_point_to_device_coord(PCL5Context *pcl5_ctxt,
                                               IPOINT *result);
static Bool hpgl2_pen_in_range(PCL5Context *pcl5_ctxt, HPGL2Integer pen);
static Bool hpgl2_pen_in_palette_range(ColorPalette *palette,
                                       HPGL2Integer pen);
/* Early drop rop D */
static HPGL2Integer hpgl2_map_pen_to_palette(ColorPalette *palette,
                                             HPGL2Integer pen);

#define HPGL2_DEFAULT_PEN_METRIC (0.35)
#define HPGL2_DEFAULT_PEN_RELATIVE (0.1)

/* See header for doc. */
void default_HPGL2_line_fill_info(HPGL2LineFillInfo* self)
{
  uint32 i ;
  self->line_type.type = HPGL2_LINETYPE_SOLID ;
  self->line_type.pattern_length = 4 ; /* 4% of distance between P1 and P2 */
  self->line_type.mode = 0 ; /* relative */
  self->line_type.residue = 0 ;
  for ( i = 0 ; i < 8 ; ++i ) {
    self->line_type.user_linetype[i][0] = 0 ;
  }
  self->previous_line_type_valid = FALSE ;
  self->previous_line_type.type = HPGL2_LINETYPE_SOLID ;
  self->previous_line_type.pattern_length = 4 ;
  self->previous_line_type.mode = 0 ;
  self->previous_line_type.residue = 0 ;
  /* PCL COMPATIBILITY
   * Not needed for HP4250/HP4650 reference printers.
  self->linetype99_point.x = 0 ;
  self->linetype99_point.y = 0 ;
   */
  self->line_end = HPGL2_LINE_END_BUTT;
  self->line_join = HPGL2_LINE_JOIN_MITRED;
  self->miter_limit = 5.0;
  self->pen_turret = 0;
  self->pen_width_selection_mode = HPGL2_PEN_WIDTH_METRIC;
  self->selected_pen = HPGL2_PEN_BLACK; /* ref printer does this. */
  self->symbol_mode_char = NUL;
  self->fill_params.fill_type = HPGL2_FILL_SOLID_1;

  /* DF and IN also side effect the retained fill type parameters. */
  self->fill_params.hatch.angle = 0.0;
  /* P1 and P2 will be reset to the corners of the picture frame, so
   * default spacing can be derived from picture frame. */
  self->fill_params.hatch.spacing_type = HPGL2_HATCH_SPACING_DEFAULT;
  self->fill_params.hatch.spacing = 0.0;
  /* NB no access to p1 p2 distance at the moment! */
  self->fill_params.cross_hatch.angle = 0.0;
  self->fill_params.cross_hatch.spacing_type = HPGL2_HATCH_SPACING_DEFAULT;
  self->fill_params.cross_hatch.spacing = 0.0;
  self->fill_params.shading.level = 50.0;
  self->fill_params.user.index = 1; /* solid */
  self->fill_params.user.pen_choice = 0;
  self->fill_params.pcl_hatch.type = 1;
  self->fill_params.pcl_user.pattern_id = 0;

  self->screened_vectors.fill_type = HPGL2_SV_SOLID;

  self->screened_vectors.shading.level = 50;
  self->screened_vectors.user.index = 1; /* solid */
  self->screened_vectors.user.pen_choice = 0; /* current pen */
  self->screened_vectors.pcl_hatch.type = 6;
  /* reference printer HP4250 defaults to hatch #6 for SV. */
  self->screened_vectors.pcl_user.pattern_id = 0;

  self->anchor_corner.x = 0;
  self->anchor_corner.y = 0;
  self->job_anchor_corner.x = self->job_anchor_corner.y = 0;
  self->transparency = 1;
}

/* --- helpers functions for state manipulation. --- */

/* Reset line fill state to default values. Assignments depend on
 * whether the reset is done in context of an IN or a DF operation.
 */
void hpgl2_set_default_linefill_info(HPGL2LineFillInfo *line_info, Bool initialize,
                                     HPGL2Point *default_anchor_corner)
{
  uint32 i ;
  line_info->line_type.type = HPGL2_LINETYPE_SOLID ;
  line_info->line_type.pattern_length = 4 ; /* 4% of distance between P1 and P2 */
  line_info->line_type.mode = 0 ; /* relative */
  line_info->line_type.residue = 0 ;
  for ( i = 0 ; i < 8 ; ++i ) {
    line_info->line_type.user_linetype[i][0] = 0 ;
  }
  line_info->previous_line_type_valid = FALSE ;
  line_info->previous_line_type.type = HPGL2_LINETYPE_SOLID ;
  line_info->previous_line_type.pattern_length = 4 ;
  line_info->previous_line_type.mode = 0 ;
  line_info->previous_line_type.residue = 0 ;
  /* PCL COMPATIBILITY
   * Not needed by HP4250/HP4650 reference printers.
  line_info->linetype99_point.x = 0 ;
  line_info->linetype99_point.y = 0 ;
   */
  line_info->line_end = HPGL2_LINE_END_BUTT;
  line_info->line_join = HPGL2_LINE_JOIN_MITRED;
  line_info->miter_limit = 5.0;
  line_info->pen_turret = 0;
  if ( initialize ) {
    /*
    line_info->black_pen_width = 0.35;
    line_info->white_pen_width = 0.35;
    */
    line_info->pen_width_selection_mode = HPGL2_PEN_WIDTH_METRIC;
    /*
     *  Experimentation with the reference printer suggests that
     *  IN operator *does not* affect the selected pen.
    */
  }
  line_info->symbol_mode_char = NUL;
  line_info->fill_params.fill_type = HPGL2_FILL_SOLID_1;

  /* DF and IN also side effect the retained fill type parameters. */
  line_info->fill_params.hatch.angle = 0.0;
  /* P1 and P2 will be reset to the corners of the picture frame, so
   * default spacing can be derived from picture frame. */
  line_info->fill_params.hatch.spacing_type = HPGL2_HATCH_SPACING_DEFAULT;
  line_info->fill_params.hatch.spacing = 0.0;
  line_info->fill_params.cross_hatch.angle = 0.0;
  line_info->fill_params.cross_hatch.spacing_type = HPGL2_HATCH_SPACING_DEFAULT;
  line_info->fill_params.cross_hatch.spacing = 0.0;
  /* NB no access to p1 p2 distance at the moment! */
  line_info->fill_params.shading.level = 50.0;
  line_info->fill_params.user.index = 1;
  line_info->fill_params.pcl_hatch.type = 1;
  line_info->fill_params.pcl_user.pattern_id = 0;

  line_info->screened_vectors.fill_type = HPGL2_SV_SOLID;
  /* Experiments with the HP4250 indicate that the printer
   * only resets the type of SV, not any of the saved
   * parameters.
   */

  if ( initialize ) {
    line_info->anchor_corner.x = 0;
    line_info->anchor_corner.y = 0;
    line_info->job_anchor_corner.x = line_info->job_anchor_corner.y = 0;
  } else {
    /* PCL COMPATIBILITY The HP4700 appears to reset the anchor point to
       scaling point P1 when executing DF.  The spec implies 0,0. */
    line_info->anchor_corner =
      line_info->job_anchor_corner = *default_anchor_corner;
  }

  line_info->transparency = 1;
}

/* Syncronise the interpreter gstate with the HPGL2 state. */
void hpgl2_sync_linefill_info(PCL5Context* pcl5_ctxt, Bool initialize)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  UNUSED_PARAM(Bool, initialize);

  hpgl2_sync_linewidth(pcl5_ctxt);
  hpgl2_sync_lineend(linefill_info) ;
  hpgl2_sync_linejoin(linefill_info) ;
  hpgl2_sync_miterlimit(linefill_info) ;
  hpgl2_sync_line_type(pcl5_ctxt, &linefill_info->line_type);
  hpgl2_sync_pen_color(pcl5_ctxt, FALSE);
  hpgl2_sync_transparency(linefill_info);
}

/** \todo : focus default size handling at this point? */
HPGL2Real hpgl2_get_palette_pen_width(ColorPalette *palette, HPGL2Integer pen)
{
  HQASSERT(palette != NULL, "NULL palette");

  if ( ! hpgl2_pen_in_palette_range( palette, pen ) ) {
    return 0;
  }
  else
    return palette->pen_widths[pen];
}

HPGL2Real hpgl2_get_pen_width(PCL5Context *pcl5_ctxt, HPGL2Integer pen)
{
  ColorPalette *palette = get_active_palette(pcl5_ctxt);

  HQASSERT(palette != NULL, "No Color palette");

  return hpgl2_get_palette_pen_width(palette, pen);
}

HPGL2Integer hpgl2_get_pen_width_mode(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;

  return line_info->pen_width_selection_mode ;
}

uint32 hpgl2_get_palette_pen_count(ColorPalette *palette)
{
  return ( 1 << palette->bits_per_index );
}

void hpgl2_set_palette_pen_width(ColorPalette *palette,
                                 HPGL2Integer pen,
                                 HPGL2Real pen_width)
{
  uint32 pen_count;

  HQASSERT(palette != NULL, "No color palette for pen widths");

  pen_count = hpgl2_get_palette_pen_count(palette);

  /* It is possible for an attempt to be made to set width
   * of a non-exstent pen. This is not a assert failure;
   * on the reference printer, an invalid pen can be maintained
   * in HPGL environment (as long as not used to draw). Where other
   * operators side effect and then attempt to restore pen state,
   * that might cause an attempt to set width of invalid pen.
   */
  if ( ( pen != HPGL2_ALL_PENS )
      && ! hpgl2_pen_in_palette_range(palette, pen) ) {
    return;
  }

  if ( pen == HPGL2_ALL_PENS )  {
    while (pen_count) {
      --pen_count;
      palette->pen_widths[pen_count] = pen_width;
    }
  }
  else
    palette->pen_widths[pen] = pen_width;
}

void hpgl2_set_pen_width(PCL5Context *pcl5_ctxt,
                         HPGL2Integer pen,
                         HPGL2Real pen_width)
{
  ColorPalette *palette = NULL;

  HQASSERT(pcl5_ctxt != NULL, "NULL PCL5Context");

  if ( pen == HPGL2_NO_PEN )
    return;

  palette = get_active_palette(pcl5_ctxt);

  HQASSERT(palette != NULL, "NULL palette");

  hpgl2_set_palette_pen_width(palette, pen, pen_width);
  /** \todo
   * Could avoid sync if the set pen is checked for equality with
   * the selected pen. */
  hpgl2_sync_linewidth(pcl5_ctxt) ;
}

void hpgl2_set_pen_width_mode(PCL5Context *pcl5_ctxt, HPGL2Integer pen_width_mode)
{
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;

  switch ( pen_width_mode ) {
  case HPGL2_PEN_WIDTH_METRIC :
  case HPGL2_PEN_WIDTH_RELATIVE :
    if ( line_info->pen_width_selection_mode != pen_width_mode ) {
      line_info->pen_width_selection_mode = pen_width_mode ;
      hpgl2_sync_linewidth(pcl5_ctxt) ;
    }
    break ;
  default :
    HQFAIL("Unexpected pen_width_mode value") ;
  }
}

/* From the HPGL2 specification : 40 plotter units to 1 millimter, and
 * 1016 plotter units to the inch, thus 1016/40 millimeters to the inch.
 */
#define HPGL2_MM_TO_INCH ((HPGL2Real)1016.0 / (HPGL2Real)40.0)
#define HPGL2_INCH_TO_MM ((HPGL2Real)40.0 / (HPGL2Real)1016.0)

HPGL2Real current_pen_width_to_plotter_units(PCL5Context *pcl5_ctxt)
{
  HPGL2Real pen_width;
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);

  if ( ! pen_is_selected(pcl5_ctxt) ) {
    HQFAIL("No pen selected");
    return 0;
  }

  pen_width = hpgl2_get_pen_width(pcl5_ctxt, line_info->selected_pen) ;
  HQASSERT(pen_width != -1, "Invalid pen width") ;

  if ( line_info->pen_width_selection_mode == HPGL2_PEN_WIDTH_METRIC ) {
    HPGL2Real plot_scale;

    pen_width = ( pen_width / HPGL2_MM_TO_INCH ) * 7200;  /* internal units.*/
    pen_width /= GL2_PLOTTER_UNITS_TO_INTERNAL;  /* width in plotter units. */

    if ( !config_info->scale_enabled
         || config_info->scale_mode == HPGL2_SCALE_POINT_FACTOR  ) {
      HPGL2Real h_plot_scale = horizontal_scale_factor(pcl5_ctxt);
      HPGL2Real v_plot_scale = vertical_scale_factor(pcl5_ctxt);

      /* Pen width needs to be scaled by the smallest of the plot sized scaing
       * ratios.
       */
      plot_scale = h_plot_scale <= v_plot_scale ? h_plot_scale : v_plot_scale ;
      if ( plot_scale != 1.0 )
        pen_width *= plot_scale;
    }

  } else {
    HQASSERT( line_info->pen_width_selection_mode
               == HPGL2_PEN_WIDTH_RELATIVE,
             "Bad pen width mode" );
    pen_width = ( pen_width / (HPGL2Real)100.0 ) *
                 get_p1_p2_distance(pcl5_ctxt); /* width in plotter units. */
  }

  HQASSERT( pen_width >= 0, "Negative pen width");

  return pen_width;
}

/* set gsate pen size to produce an specific absolute size.*/
void hpgl2_sync_linewidth(PCL5Context *pcl5_ctxt)
{
  HPGL2Real pen_width;
  USERVALUE gs_width;

  /* do nothing to gstate if there is no pen selected. */
  if ( ! pen_is_selected(pcl5_ctxt) )
    return;

  pen_width = current_pen_width_to_plotter_units(pcl5_ctxt);

  if ( pen_width < SMALLEST_REAL ) /* precision limit */
    gs_width = SMALLEST_REAL;
  else if ( pen_width > BIGGEST_REAL ) /* range limit */
    gs_width = BIGGEST_REAL;
  else
    gs_width = (USERVALUE)pen_width;

  gstateptr->thestyle.linewidth = gs_width;
}

void set_hpgl2_default_pen(PCL5Context *pcl5_ctxt)
{
  /* default pen is no pen. */
  HPGL2LineFillInfo *line_fill_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  line_fill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  line_fill_info->selected_pen = HPGL2_NO_PEN;
  return;
}

/* Selection of pen controls whether drawing is done. */
Bool pen_is_selected(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *line_fill_info = NULL;

  line_fill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  return (line_fill_info->selected_pen != HPGL2_NO_PEN);
}

HPGL2Real hpgl2_get_default_pen_width(HPGL2Integer mode)
{
  return mode == HPGL2_PEN_WIDTH_METRIC ?
                   HPGL2_DEFAULT_PEN_METRIC
                   : HPGL2_DEFAULT_PEN_RELATIVE ;
}

/* Early drop rop D */
void hpgl2_ensure_pen_in_palette_range(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *line_fill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  ColorPalette *palette = get_active_palette(pcl5_ctxt);

  HQASSERT( palette != NULL, "palette is NULL" );
  HQASSERT( line_fill_info->selected_pen >= 0
            || line_fill_info->selected_pen == HPGL2_NO_PEN,
            "Bad pen value");

  /* Behaviour of the reference printer indicates that an out-of-range
   * selected pen is mapped to in-range pen only if the pen is used.
   * Thus, SP6;NP4;NP8; leaves selected pen 6, but SP6;NP4;RR100,100;NP8
   * has selected pen 3 at the end.
   */
  if ( ! hpgl2_pen_in_palette_range(palette, line_fill_info->selected_pen ) ) {
    line_fill_info->selected_pen =
     hpgl2_map_pen_to_palette(palette, line_fill_info->selected_pen);
  }
}

static Bool hpgl2_sync_palette_color(PCL5Context *pcl5_ctxt,
                                     HPGL2Integer pen)
{
  if ( set_ps_color_from_palette_index(pcl5_ctxt, pen) ) {
    return setPclForegroundSource(pcl5_ctxt->corecontext->page,
                                  PCL_DL_COLOR_IS_FOREGROUND);
  }

  return FALSE;
}

static Bool hpgl2_pen_in_palette_range(ColorPalette *palette,
                                       HPGL2Integer pen)
{
  HQASSERT(palette != NULL, "palette is NULL");

  return ( (pen >= 0) && (pen < ( 1 << palette->bits_per_index ) ) );
}

static Bool hpgl2_pen_in_range(PCL5Context *pcl5_ctxt, HPGL2Integer pen)
{
  ColorPalette *palette = NULL;

  palette = get_active_palette(pcl5_ctxt);
  HQASSERT(palette != NULL, "palette is NULL");

  return hpgl2_pen_in_palette_range(palette, pen);
}

/* Apply the HPGL pen mapping algorithm for out of range pens. */
static HPGL2Integer hpgl2_map_pen_to_palette(ColorPalette *palette,
                                             HPGL2Integer pen)
{
  HPGL2Integer pen_count = 1 << palette->bits_per_index ;
  if ( pen >= pen_count )
    pen = ( ( pen - 1 ) % ( pen_count - 1 ) ) + 1 ;

  return pen;
}

/**
 * Set the current pattern in the core.
 *
 * @param palette The current palette; this is used for color patterns, but may
 * be null, in which case the pattern is treated as being black and white.
 */
static void set_pattern(pcl5_pattern* pattern,
                        uint32 deviceDpi,
                        uint32 angle,
                        IPOINT* origin,
                        Pcl5CachedPalette* palette)
{
  /* Check for optimised patterns; these have no data and can be replaced with
   * the default 'null' pattern. */
  if (pattern != NULL && pattern->data == NULL) {
    pattern = NULL;
  }

  setPcl5Pattern(pattern, deviceDpi, angle, origin, palette);
}

/* Lines drawn with a white pen are only seen when transparency is turned off.
 * Note the current PCL pattern is applied to all items rendered in PCL job
 * including stroked lines drawn in the current pen. Because of HPGL2
 * transparency, we cannot use the PCL white pattern, which would
 * always erase background.
 * Pen color might involve setting the current pattern.
 */
void hpgl2_sync_pen_color(PCL5Context *pcl5_ctxt, Bool force_solid)
{
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2ConfigInfo *config_info =  get_hpgl2_config_info(pcl5_ctxt);
  IPOINT anchor_point = { 0,0 };
  int32 rotation;
  uint32 pen;
  int32 device_res = ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt));

  hpgl2_ensure_pen_in_palette_range(pcl5_ctxt);

  pen = line_info->selected_pen;
  if ( pen != HPGL2_NO_PEN ) {
    uint8 fill_type = line_info->screened_vectors.fill_type;

    /* now the pattern */
    if ( force_solid ) {
        hpgl2_sync_palette_color(pcl5_ctxt, pen);
        set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
    }
    else {

      /* anchor point for patterns needs to be represented in terms of
       * device coordinates. */
      hpgl2_anchor_point_to_device_coord(pcl5_ctxt, &anchor_point);

      rotation = ( config_info->rotation
        + get_pattern_rotation(pcl5_ctxt, FALSE) ) % 360;

      /* screened vector info determines the pattern used to render strokes */
      switch (fill_type) {

        case HPGL2_SV_SOLID:
          hpgl2_sync_palette_color(pcl5_ctxt, pen);
          set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
          break;

        case HPGL2_SV_USER:
          {
            pcl5_pattern *RF_pattern = NULL;
            pcl5_resource_numeric_id index =
                      (pcl5_resource_numeric_id)line_info->screened_vectors.user.index;

            RF_pattern = pcl5_id_cache_get_pattern(
                                          pcl5_ctxt->resource_caches.hpgl2_user,
                                          index);

            if ( RF_pattern != NULL) {
              /* For screen vectors, the pattern is always treated as black and
               * white, unless it uses more than pens zero and one. */
              if ( RF_pattern->color && RF_pattern->highest_pen > 1) {
                if ( !update_cached_palette(pcl5_ctxt, TRUE) )
                  return;

                set_pattern(RF_pattern, device_res, rotation, &anchor_point,
                            &cached_palette);
              }
              else {
                /* uncolored pattern will be defined in terms of pens 0 and 1
                 * only. */
                if (line_info->screened_vectors.user.pen_choice == 1 )
                  hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
                else
                  hpgl2_sync_palette_color(pcl5_ctxt, 1);

                set_pattern(RF_pattern, device_res, rotation, &anchor_point, NULL);
              }
            }
            else {
              /* Default to solid fill, in current color. */
              hpgl2_sync_palette_color(pcl5_ctxt, pen);
              set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
            }
            break;
          }

        default:
          hpgl2_sync_palette_color(pcl5_ctxt, pen);
          set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
          break;

        case HPGL2_SV_PCL_CROSSHATCH:
          {
            pcl5_pattern *CH_pattern = NULL;
            pcl5_resource_numeric_id cross_hatch =
                  (pcl5_resource_numeric_id)line_info->screened_vectors.pcl_hatch.type;

            CH_pattern = pcl5_id_cache_get_pattern(
                pcl5_ctxt->resource_caches.cross_hatch,
                cross_hatch);

            hpgl2_sync_palette_color(pcl5_ctxt, pen);
            if ( CH_pattern != NULL )
             set_pattern(CH_pattern,
                         ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt)),
                         rotation, &anchor_point, NULL);
            else /* Default to solid fill. */
              set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);

            break;
          }

        case HPGL2_SV_PCL_USER:
          {
            pcl5_resource_numeric_id pattern_id =
              (pcl5_resource_numeric_id)line_info->screened_vectors.pcl_user.pattern_id;
            pcl5_pattern *user_pattern = NULL;

            user_pattern = pcl5_id_cache_get_pattern(
                                                pcl5_ctxt->resource_caches.user,
                                                pattern_id);

            hpgl2_sync_palette_color(pcl5_ctxt, pen);
            if ( user_pattern != NULL ) {

              if ( user_pattern->color ) {
                if ( !update_cached_palette(pcl5_ctxt, TRUE) )
                  return;

                set_pattern(user_pattern,
                            ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt)),
                            rotation, &anchor_point, &cached_palette);
              }
              else {
                hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
                set_pattern(user_pattern,
                            ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt)),
                            rotation, &anchor_point, NULL);
              }
            }
            else /* Default to solid fill. */
              set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);

            break;
          }

        case HPGL2_SV_SHADING:
          {
            pcl5_resource_numeric_id shade =
              (pcl5_resource_numeric_id)line_info->screened_vectors.shading.level;
            pcl5_pattern *shade_pattern = NULL;

            hpgl2_sync_palette_color(pcl5_ctxt, pen);
            if ( shade == 0 ) {
              /* This is not in the spec but has been observed and makes more
               * sense than applying the default black pattern. */
              set_pattern(&erasePattern,
                          ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt)),
                          0, &anchor_point, NULL);
            }
            else {

              shade_pattern = pcl5_id_cache_get_pattern(
                                            pcl5_ctxt->resource_caches.shading,
                                            shading_to_pattern_id( shade ));

              if ( shade_pattern != NULL )
               set_pattern(shade_pattern,
                           ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt)),
                           rotation, &anchor_point, NULL);
              else /* Default to solid fill. */
                set_current_pattern_with_id(pcl5_ctxt,
                                            0,
                                            PCL5_SOLID_FOREGROUND);

            }
            break;
          }
       }
    }
  }
}

static void hpgl2_anchor_point_to_device_coord(PCL5Context *pcl5_ctxt,
                                               IPOINT *result)
{
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2Ctms *all_ctms = get_hpgl2_ctms(pcl5_ctxt);
  OMATRIX* ctm = &(all_ctms->working_picture_frame_ctm);
  double tx, ty;

  /* Get the reference point in device coordinates. */
  MATRIX_TRANSFORM_XY(line_info->anchor_corner.x,
                      line_info->anchor_corner.y,
                      tx, ty,
                      ctm);

  /** \todo Is this still required? Lost mode?
  if ( tx > MAXINT32 )
    tx = MAXINT32;
  else if ( tx < MININT32 )
    tx = MININT32;

  if ( ty > MAXINT32 )
    ty = MAXINT32;
  else if ( ty < MININT32 )
    ty = MININT32;
*/

  /* PCL patterns have origin specified in terms of device
   * coordinates. */
  *result = ctm_transform(pcl5_ctxt, tx, ty);
}

/* Note that TR operator determines the behaviour of white areas in the HPGL2
 * and that includes the white areas of patterns. It also affects how the
 * RF "white pen" pixels are interpreted.
 * Set both pattern and source transparency to match output of 4250 reference
 * printer.
 */
void hpgl2_sync_transparency(HPGL2LineFillInfo *line_info)
{
  setPclSourceTransparent((Bool)line_info->transparency);
  setPclPatternTransparent((Bool)line_info->transparency);
}

void hpgl2_sync_fill_mode(PCL5Context *pcl5_ctxt, Bool force_pen_solid)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  IPOINT anchor_point;
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);
  HPGL2Integer rotation = config_info->rotation;
  pcl5_pattern *cache_entry = NULL;
  int32 device_res = ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt));

  hpgl2_anchor_point_to_device_coord(pcl5_ctxt, &anchor_point);

  rotation = ( rotation + get_pattern_rotation(pcl5_ctxt, FALSE) ) % 360;

  /* The fill "color" is derived from the current pen. The fill pattern
   * is then drawn with that pen. The fill will pick up the color from the
   * gstate set by the last pen selection.
   */
  switch (linefill_info->fill_params.fill_type) {

    case HPGL2_FILL_SOLID_1:
    case HPGL2_FILL_SOLID_2: /* both black */
      hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
      set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
      break;


   case HPGL2_FILL_HATCH:
   case HPGL2_FILL_CROSSHATCH:
      /* hatching uses the current pen parameters to do the fill. */
      hpgl2_sync_pen_color(pcl5_ctxt, force_pen_solid);
      break;

    case HPGL2_FILL_USER:
      {
        pcl5_resource_numeric_id index =
                        (pcl5_resource_numeric_id)linefill_info->fill_params.user.index;

        cache_entry =
          pcl5_id_cache_get_pattern(pcl5_ctxt->resource_caches.hpgl2_user,
                                    index);

        if ( cache_entry != NULL) {

          if ( cache_entry->color ) {
            if ( !update_cached_palette(pcl5_ctxt, TRUE) )
              return;

            set_pattern(cache_entry, device_res, rotation, &anchor_point,
                        &cached_palette);
          }
          else {
            /* patterns with 0,1 can be colored by the current pen or the
             * palette entry 1 depending on the pen choice flag.
             */
            if ( linefill_info->fill_params.user.pen_choice == 0 )
              hpgl2_sync_palette_color(pcl5_ctxt, 1);
            else
              hpgl2_sync_pen_color(pcl5_ctxt, TRUE);

            set_pattern(cache_entry, device_res, rotation, &anchor_point, NULL);
          }
        }
        else {
          /* Default to solid fill. */
          hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
          set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
        }

        break;
      }

    default:
      hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
      set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
      break;

    case HPGL2_FILL_PCL_CROSSHATCH:
      {
        pcl5_resource_numeric_id cross_hatch = (pcl5_resource_numeric_id)linefill_info->fill_params.pcl_hatch.type;

        cache_entry = pcl5_id_cache_get_pattern(
                                        pcl5_ctxt->resource_caches.cross_hatch,
                                        cross_hatch);

        if ( cache_entry != NULL ) {
          hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
          set_pattern(cache_entry, device_res, rotation, &anchor_point, NULL);
        }

        break;
      }

    case HPGL2_FILL_PCL_USER:
      {
        pcl5_resource_numeric_id pattern_id = (pcl5_resource_numeric_id)linefill_info->fill_params.pcl_user.pattern_id;

        cache_entry = pcl5_id_cache_get_pattern(pcl5_ctxt->resource_caches.user,
                                                pattern_id);
        if ( cache_entry != NULL ) {
          if ( cache_entry->color ) {
            if ( ! update_cached_palette(pcl5_ctxt, TRUE) )
              return;

            set_pattern(cache_entry, device_res, rotation, &anchor_point,
                        &cached_palette);
          }
          else {
            hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
            set_pattern(cache_entry, device_res, rotation, &anchor_point, NULL);
          }
        }

        break;
      }

    case HPGL2_FILL_SHADING:
      {
        pcl5_resource_numeric_id shade =
                    (pcl5_resource_numeric_id)linefill_info->fill_params.shading.level;

        if ( shade == 0 ) {
          /* This is not in the spec but has been observed and makes more sense
           * than applying the default black pattern. */
          set_pattern(&erasePattern, device_res, 0, &anchor_point, NULL);
        }
        else {
          cache_entry =
            pcl5_id_cache_get_pattern(pcl5_ctxt->resource_caches.shading,
                                      shading_to_pattern_id( shade ));

          hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
          set_pattern(cache_entry, device_res, rotation, &anchor_point, NULL);
        }

        break;
     }
  }
}

/* Map user unit hatch spacing to the equivalent plotter sizes. The spec
 * indicates that the spacing is specified in current units, and scales with
 * scale point changes. However, experiment suggests that if scaling is
 * tunred off, the hatch spacing is fixed into plotter space equivalent.
 */

static Bool fix_hatch_spacing(PCL5Context *pcl5_ctxt, HPGL2FillHatching *hatch)
{
  if (hatch->spacing_type == HPGL2_HATCH_SPACING_USER ) {
    HPGL2Point temp;

    HQASSERT(hpgl2_is_scaled(pcl5_ctxt),
             "Scalable hatch space found while in non-scaled mode");

    temp.x = hatch->spacing;
    temp.y = 0;

    /* nominal plotter units in the job may need plot size scaling. */
    if ( !job_point_to_plotter_point(pcl5_ctxt, &temp, &temp, TRUE) )
      return FALSE;

    hatch->spacing_type = HPGL2_HATCH_SPACING_PLOTTER;
    hatch->spacing = fabs(temp.x);
  }
  else if ( hatch->spacing == HPGL2_HATCH_SPACING_DEFAULT ) {
    hatch->spacing_type = HPGL2_HATCH_SPACING_PLOTTER;
    hatch->spacing = 0.01 * get_p1_p2_distance(pcl5_ctxt);
  }
  return TRUE;
}

Bool fix_all_hatch_spacing(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo * linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  return ( fix_hatch_spacing( pcl5_ctxt, &linefill_info->fill_params.hatch )
          && fix_hatch_spacing( pcl5_ctxt,
                                &linefill_info->fill_params.cross_hatch ) );
}

/* Hatch accross the rectangle defined by ur and ll corners, using current
 * hatch pattern. To simplify calculation of the locations of hatch lines to
 * be drawn, the box is rotated so hatch lines are "horizontal". The orthogonal
 * boundaries of the rotated box determine the length of lines drawn. Note that
 * we do not necessarily lines through the anchor point, unless the anchor
 * falls inside the box we are hatching.
 */
enum {
  HATCH_FAILED =0,
  HATCH_OK,
  HATCH_DEGENERATE
};

static uint32 do_hatch_box(PCL5Context *pcl5_ctxt,
                         HPGL2Point *ll,
                         HPGL2Point *ur)
{
  HPGL2LineFillInfo *line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2FillHatching *hatch_info = NULL;
  HPGL2Real step;
  HPGL2Real hatch_ang;
  Bool cross_hatch = FALSE;
  OMATRIX rotate,
          rotate_inverse;
  SYSTEMVALUE rot_ll[2], rot_lr[2], rot_ul[2], rot_ur[2],
              orig_rot_anchor[2], new_anchor[2],
              line_extent[2], adj_rot_anchor[2],
              step_delta[2], y_min, y_max, x_min, x_max;
  HPGL2Point  anchor;
  uint32 line_count;
  HPGL2Real pattern_length;

  HQASSERT(line_info->fill_params.fill_type == HPGL2_FILL_CROSSHATCH
            || line_info->fill_params.fill_type == HPGL2_FILL_HATCH,
            "do hatch called, but not in hatching mode");

  /* Observation of the HP4250 reference suggests residue needs to be reset. */
  line_info->line_type.residue = 0;

  /* Check degenerate path. Defer degenerate handling to the caller. */
  if ( ll->x > ur->x) {
    if (ll->x <= ( ur->x + EPSILON ) )
      return HATCH_OK;
  }
  else if (ur->x <= ( ll->x + EPSILON ) )
    return HATCH_OK;

  cross_hatch = line_info->fill_params.fill_type == HPGL2_FILL_CROSSHATCH;
  hatch_info = cross_hatch ? &line_info->fill_params.cross_hatch :
                             &line_info->fill_params.hatch;
  hatch_ang = hatch_info->angle;

  hatch_ang += DEG_TO_RAD * get_pattern_rotation(pcl5_ctxt, FALSE);

  if ( hatch_info->spacing_type == HPGL2_HATCH_SPACING_DEFAULT )
    step = 0.01 * get_p1_p2_distance(pcl5_ctxt);
  else if ( hatch_info->spacing_type == HPGL2_HATCH_SPACING_USER ) {
    HQASSERT(hpgl2_is_scaled(pcl5_ctxt),
      "Hatch spacing is user units, but HPGL2 state not scaled");

    if ( hpgl2_is_scaled(pcl5_ctxt) ) {
      HPGL2Point temp;

      temp.x = hatch_info->spacing;
      temp.y = 0;

      if ( ! job_point_to_plotter_point(pcl5_ctxt, &temp, &temp, TRUE) )
        return HATCH_FAILED;

      step = fabs(temp.x);
    }
    else
      return HATCH_FAILED;
  }
  else
    step = hatch_info->spacing;

  HQASSERT(step > 0, "Hatch step must be > 0");

  if ( step <= 0 )
    return HATCH_FAILED;

  /* Check degenerate case when the step is less than the width of a line.
   * This will mean that the area will be completely covered with the pen.
   * We won't hatch the bbox, but defer to the caller to determine the
   * appropriate action.
   */
  if ( current_pen_width_to_plotter_units(pcl5_ctxt) > step )
    return HATCH_DEGENERATE;

  /* rotate the bbox so that the vector indicating the direction of
   * hatch lines is at 0 degrees ( lines stroked in ascending x direction
   * of rotated coord system.
   */

  anchor = linefill_info->anchor_corner;

  rotate.matrix[0][0] = cos( -hatch_ang );
  rotate.matrix[0][1] = sin( -hatch_ang );
  rotate.matrix[1][0] = -sin( -hatch_ang );
  rotate.matrix[1][1] = cos( -hatch_ang );
  rotate.matrix[2][0] = 0;
  rotate.matrix[2][1] = 0;
  MATRIX_SET_OPT_BOTH(&rotate);

  rotate_inverse.matrix[0][0] = cos( hatch_ang );
  rotate_inverse.matrix[0][1] = sin( hatch_ang );
  rotate_inverse.matrix[1][0] = -sin( hatch_ang );
  rotate_inverse.matrix[1][1] = cos( hatch_ang );
  rotate_inverse.matrix[2][0] = 0;
  rotate_inverse.matrix[2][1] = 0;
  MATRIX_SET_OPT_BOTH(&rotate_inverse);

  /* calculate rotated corners of the bbox. */
  MATRIX_TRANSFORM_XY( ll->x, ll->y, rot_ll[0], rot_ll[1], &rotate);
  MATRIX_TRANSFORM_XY( ur->x, ur->y, rot_ur[0], rot_ur[1], &rotate);
  MATRIX_TRANSFORM_XY( ur->x, ll->y, rot_lr[0], rot_lr[1], &rotate);
  MATRIX_TRANSFORM_XY( ll->x, ur->y, rot_ul[0], rot_ul[1], &rotate);

  y_min = min ( min( rot_ll[1], rot_ur[1] ), min( rot_lr[1], rot_ul[1] ) );
  y_max = max ( max( rot_ll[1], rot_ur[1] ), max( rot_lr[1], rot_ul[1] ) );
  x_min = min ( min( rot_ll[0], rot_ur[0] ), min( rot_lr[0], rot_ul[0] ) );
  x_max = max ( max( rot_ll[0], rot_ur[0] ), max( rot_lr[0], rot_ul[0] ) );

  MATRIX_TRANSFORM_XY(anchor.x, anchor.y,
                      orig_rot_anchor[0], orig_rot_anchor[1],
                      &rotate);

  /* derive a new origin for the hatch pattern, such that new origin x coord is
   * the largest value less than the x_min of the bounding box that preserves
   * line pattern phase.
   * That is, the distance of the new anchor to the old anchor should be an
   * integer multiple of the pattern length.
   */
  pattern_length = 0;
  if (linefill_info->line_type.type != 0
      && linefill_info->line_type.type != HPGL2_LINETYPE_SOLID) {
    if ( linefill_info->line_type.mode == 0 ) {
      /* relative to p1 p2 */
       pattern_length = linefill_info->line_type.pattern_length
                          * 0.01 * get_p1_p2_distance(pcl5_ctxt);
    }
    else {
      /* Pattern length given in mm. */
      pattern_length = linefill_info->line_type.pattern_length * 40;
    }
    HQASSERT( pattern_length > 0, "Patterned line has pattern length of 0");
  }

  if ( orig_rot_anchor[0] > x_min ) {
    if ( pattern_length != 0 ) {
      HQASSERT(pattern_length > 0, "Negative pattern length");
      adj_rot_anchor[0] = (x_min + fabs( fmod( orig_rot_anchor[0] - x_min,
                                               pattern_length ) ) )
                          - pattern_length;
    }
    else
      adj_rot_anchor[0] = x_min;
  }
  else if ( orig_rot_anchor[0] < x_min ) {
    if (pattern_length != 0 ) {
      HQASSERT(pattern_length > 0, "Negative pattern length");
      adj_rot_anchor[0] = x_min - fabs( fmod( x_min - orig_rot_anchor[0],
                                              pattern_length ) );
    }
    else
      adj_rot_anchor[0] = x_min;
  }
  else
    adj_rot_anchor[0] = x_min;

  /* Hatch line length is integer of pattern length, plus 1/2 pattern length
   * which gives the required phase shift between successive line. The need
   * for the half phase shift determined by observation of the reference
   * printer (HP4250).
   */
  if ( pattern_length != 0 ) {
    x_max -= fmod( x_max - adj_rot_anchor[0], pattern_length );
    x_max =  x_max + pattern_length + ( pattern_length / 2 );
  }

 /* Similarly, move the transformed y coord of the anchor down the y axis
  * in steps of hatch line step until we are below the low y coord
  * calculated above. Because of phase requirements, move an even
  * number of lines from the origin.
  */
  if ( orig_rot_anchor[1] == y_min )
    adj_rot_anchor[1] = y_min;
  else if ( orig_rot_anchor[1] > y_min )
    adj_rot_anchor[1] = y_min - (2 * step)
                        + (fabs(fmod(orig_rot_anchor[1] - y_min, 2 * step)));
  else
    adj_rot_anchor[1] = y_min - fabs( fmod( y_min - orig_rot_anchor[1],
                                            (2*step) ) );

  /* calculate details of what to draw in plotter space. */
  MATRIX_TRANSFORM_DXY( ( x_max - adj_rot_anchor[0] ), 0,
                          line_extent[0], line_extent[1],
                          &rotate_inverse);

  MATRIX_TRANSFORM_DXY( 0, step,
                        step_delta[0], step_delta[1],
                        &rotate_inverse );

  /* calculate where the extended anchor point is located in plotter space. */
  MATRIX_TRANSFORM_XY( adj_rot_anchor[0], adj_rot_anchor[1],
                       new_anchor[0], new_anchor[1],
                       &rotate_inverse );

  line_count =  2 + (uint32)(fabs((y_max - adj_rot_anchor[1]) / step ) + 0.5);

  gs_moveto( TRUE, new_anchor, &gstateptr->thepath );

  /* Reset dashing from the dashoffset. */
  SET_LINELIST_CONT_DASH(gstateptr->thepath.lastline, FALSE);

  do {

    if ( ! gs_lineto( FALSE, TRUE, line_extent, &gstateptr->thepath) )
      goto hatch_error;
    new_anchor[0] += step_delta[0];
    new_anchor[1] += step_delta[1];
    if ( !gs_moveto( TRUE, new_anchor, &gstateptr->thepath) )
      goto hatch_error;

    /* Set the dashing continuation flag on the new sub-path
       (we don't want to reset dashing for each sub-path). */
    SET_LINELIST_CONT_DASH(gstateptr->thepath.lastline, TRUE);

  } while (--line_count);

  return HATCH_OK;

hatch_error:
  /* VMError or nocurrentpoint. */
  return HATCH_FAILED;
}

/*
 * A path is hatch filled by using the hatch filling the bounding box of the
 * path, and using the path itself as a clip on that bbox.
 * The path is filled with white color initially.
 * hpgl2_hatchfill_path will restore the current point of the path after the
 * hatching is done.
 */
Bool hpgl2_hatchfill_path(PCL5Context *pcl5_ctxt,
                          PATHINFO *the_path, int32 fill_rule, Bool do_copypath)
{
  HPGL2Point  ll,
              ur;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  SYSTEMVALUE curr_point[2],
              urx, ury, llx, lly;
  Bool res = TRUE;
  OMATRIX     inv_ctm;
  sbbox_t *buffer_bbox;
#if defined (DEBUG_BUILD)
  /* Set omit_hatch_clip to TRUE to see the full extent of hatching lines
    drawn by the rip. */
  static Bool omit_hatch_clip = FALSE;
#endif

  if ( the_path == NULL ) {
    HQFAIL("Null path to hatch fill");
    return TRUE;
  }

  /* can't hatch fill if no pen selected. */
  if ( ! pen_is_selected(pcl5_ctxt) )
    return TRUE;

  if ( linefill_info->fill_params.fill_type != HPGL2_FILL_CROSSHATCH
    && linefill_info->fill_params.fill_type != HPGL2_FILL_HATCH ) {
      HQFAIL("hatchfill invoked, but HPGL2 state is not doing hatched fills");
      return TRUE;
  }

  if ( !gs_currentpoint(&gstateptr->thepath,
                        &curr_point[0],
                        &curr_point[1] ) ) {
    HQFAIL("No current point when doing hatching");
    return FALSE;
  }

  buffer_bbox = path_bbox( the_path , NULL,
                           BBOX_SAVE|BBOX_LOAD|BBOX_IGNORE_ALL);
  if ( NULL == buffer_bbox ) {
    res = FALSE;
    goto hatch_done;
  }

  urx = buffer_bbox->x2;
  ury = buffer_bbox->y2;
  llx = buffer_bbox->x1;
  lly = buffer_bbox->y1;

  /* Check degenerate path.  */
  if ( llx > urx) {
    if (llx <= ( urx + EPSILON ) )
      return TRUE;
  }
  else if (urx <= ( llx + EPSILON ) )
    return TRUE;

  /* bbox given in device coordinates. Go back to current plotter
   * coords.
   */
  /**
   * \todo @@@ TODO be aware that plot size scaling will have to
   * apply at this point - plot size scaling does not get factored
   * into the device CTM.
   */

  if ( !matrix_inverse( &thegsPageCTM(*gstateptr), &inv_ctm )) {
    HQFAIL("Can't calculate CTM inverse");
    res = FALSE;
    goto hatch_done;
  }

  MATRIX_TRANSFORM_XY( urx, ury, urx, ury, &inv_ctm );
  MATRIX_TRANSFORM_XY( llx, lly, llx, lly, &inv_ctm );

  /* sort the bbox control points based on y coord. */
  if (ury < lly ) {
    ur.x = llx;
    ur.y = lly;
    ll.x = urx;
    ll.y = ury;
  }
  else {
    ur.x = urx;
    ur.y = ury;
    ll.x = llx;
    ll.y = lly;
  }

  hpgl2_setgray(1.0);
  /* This serves to paint white in the area to be hatched, for
   * handling of transparency.
   */
  set_current_pattern_with_id(pcl5_ctxt, 0 , PCL5_SOLID_FOREGROUND);
  res = dofill(the_path, fill_rule, GSC_FILL, FILL_NORMAL);
  hpgl2_sync_fill_mode(pcl5_ctxt, FALSE);
  if ( !res )
    goto hatch_done;

  if  ( gs_cpush() ) {
    uint32 hatch_box_result;

    if (
#if defined DEBUG_BUILD
      omit_hatch_clip ||
#endif /* DEBUG_BUILD */
      gs_addclip(fill_rule, the_path, do_copypath)) {
      /* Hatch fills always use the default ROP (found via experimentation). */
      uint8 savedRop = getPclRop();
      setPclRop(PCL_ROP_TSo);

      hatch_box_result = do_hatch_box(pcl5_ctxt, &ll, &ur);

      switch ( hatch_box_result )
      {
        case HATCH_OK:
          if ( linefill_info->fill_params.fill_type == HPGL2_FILL_CROSSHATCH ) {
            HPGL2Real ang = linefill_info->fill_params.cross_hatch.angle;

            /* Hatch same area, with same anchor but with lines at
             * 90 degrees to last. */
            linefill_info->fill_params.cross_hatch.angle =
                     DEG_TO_RAD * fmod ( (RAD_TO_DEG * ang) + 90, 360 );
            hatch_box_result = do_hatch_box(pcl5_ctxt, &ll, &ur);

            HQASSERT( hatch_box_result != HATCH_DEGENERATE,
                      "Cross hatching should not be degenerate");

            res = (hatch_box_result == HATCH_OK);
            linefill_info->fill_params.cross_hatch.angle = ang;
          }

          /* All the hatching must be done under a single stroke to ensure
             that there are no double-compositing artifacts if transparency
             is being applied. */
          res = res && hpgl2_stroke( pcl5_ctxt, TRUE, TRUE,
                                     FALSE /* ignore thin line override */ );
          break;

        case HATCH_DEGENERATE:
          /* fill with solid if hatch pattern is degenerate.
           * No need to worry about cross hatching. Same caveats as
           * the fill with white above.
           */
            hpgl2_sync_pen_color(pcl5_ctxt, TRUE);
            res = dofill(the_path, fill_rule, GSC_FILL, FILL_NORMAL);
            hpgl2_sync_fill_mode(pcl5_ctxt, FALSE);
            res &= gs_newpath();
            break;

        default:
          HQFAIL("Unknown result from do_hatch_box"); /* fall through */
          case HATCH_FAILED:
          res = FALSE;
          break;
      }

      if ( !gs_ctop() )
        res = FALSE;

      setPclRop(savedRop);
    }
  }
  else
    res = FALSE;

  if ( !gs_moveto(TRUE, curr_point, &gstateptr->thepath) ) {
    HQFAIL("Cannot restore current point when hatching");
    res = FALSE;
  }

hatch_done:
  return res;
}

/* Use butt cap and no join for lines 0.35mm or less. */
void hpgl2_override_thin_line_attributes(PCL5Context *pcl5_ctxt,
                                         LINESTYLE *linestyle)
{
  HPGL2Real pen_width = current_pen_width_to_plotter_units(pcl5_ctxt) ;

  /* Pen width is returned in plotter units so convert 0.35mm to PUs by
     multiplying by 40.  Adding an epsilon seems to be required for the
     correct result when exactly 0.35 is in the job. */
  if ( pen_width > (0.35 * 40 + EPSILON) )
    return ;

  linestyle->startlinecap = linestyle->endlinecap = linestyle->dashlinecap = BUTT_CAP ;
  linestyle->linejoin = NONE_JOIN ;
}

/* [65776] Re-interpret the last set AC co-ordinates in terms of the current
 * scaling of coordinates. This applies to both user coordinate systems or
 * any plot size scaling that is in force. */
Bool hpgl2_redo_AC(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  return job_point_to_plotter_point( pcl5_ctxt,
                                      &linefill_info->job_anchor_corner,
                                      &linefill_info->anchor_corner,
                                      FALSE ) ;
}

/* --- HPGL2 operators. --- */

Bool hpgl2op_AC(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = NULL;
  FILELIST *flptr;
  uint8 terminator;

  HQASSERT(pcl5_ctxt != NULL, "Null PCL5Context");

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  flptr = pcl5_ctxt->flptr;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator ) ) {

    linefill_info->anchor_corner.x = 0;
    linefill_info->anchor_corner.y = 0;

    linefill_info->job_anchor_corner.x = linefill_info->job_anchor_corner.y = 0;

    /* AC; returns anchor point to bottom left of the HPGL2
     * picture frame relative to the current coordinate system.
     */

    if ( hpgl2_is_scaled(pcl5_ctxt) )
      if ( ! job_point_to_plotter_point( pcl5_ctxt,
                                       &linefill_info->job_anchor_corner,
                                       &linefill_info->anchor_corner,
                                       FALSE ) )
        return TRUE;
  }
  else {
    HPGL2Point temp_anchor;

    if ( hpgl2_scan_real(pcl5_ctxt, &temp_anchor.x ) <= 0
        || hpgl2_scan_separator(pcl5_ctxt) <= 0
        || hpgl2_scan_real(pcl5_ctxt, &temp_anchor.y) <= 0 )
      return TRUE; /* syntax error */

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

    /* record AC in plotter units. It does not scale with user units. */
    /* Need to account for plot size scaling also, hence the transformation. */
    if ( ! job_point_to_plotter_point( pcl5_ctxt,
                                       &temp_anchor,
                                       &linefill_info->anchor_corner,
                                       FALSE ) )
      return TRUE;

    linefill_info->job_anchor_corner = temp_anchor;
  }

  hpgl2_linetype_clear(linefill_info) ;

  return TRUE ;
}

Bool hpgl2op_FT(PCL5Context *pcl5_ctxt)
 {
  HPGL2Integer fill_type;
  uint8 terminator;
  HPGL2LineFillInfo *line_info;
  HPGL2FillParams *fill_params;

  HPGL2Real dummy;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  fill_params = &line_info->fill_params;

  /* syntax: fill type, option1, option2 */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 )
    fill_type = 1;
  else {

    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &fill_type) <= 0 )
      return TRUE; /* syntax error */

    switch ( fill_type )
    {
      case HPGL2_FILL_SOLID_1: /* fall through */
      case HPGL2_FILL_SOLID_2: /* solid black */
      /* ignore options, if any. */
          (void)hpgl2_scan_separator(pcl5_ctxt);
          (void)hpgl2_scan_real(pcl5_ctxt, &dummy);
          (void)hpgl2_scan_separator(pcl5_ctxt);
          (void)hpgl2_scan_real(pcl5_ctxt, &dummy);
        break;

      case HPGL2_FILL_HATCH: /* fall through */
      case HPGL2_FILL_CROSSHATCH: /* HPGL2 hatching, cross hatching */
        {
          HPGL2FillHatching *hatch = NULL;
          HPGL2Real space, angle;

          if ( fill_type == HPGL2_FILL_HATCH )
           hatch = &fill_params->hatch;
          else
            hatch = &fill_params->cross_hatch;

          if ( hpgl2_scan_separator(pcl5_ctxt) > 0
               && hpgl2_scan_real(pcl5_ctxt, &space) > 0 ) {

            if ( space == 0.0 ) {
              /* default case for the spacing. No need to disturb any of the
               * previous state values.
               * We will leave HPGL2_HATCH_SPACING_DEFAULT to be calculated
               * when the fill is actually done, thus it will move with the
               * scale points. This follows the behaviour of the HP4250.
               */
              if ( hpgl2_is_scaled(pcl5_ctxt) ) {
                hatch->spacing = 0;
                hatch->spacing_type = HPGL2_HATCH_SPACING_DEFAULT;
              }
              else {
                hatch->spacing = get_p1_p2_distance(pcl5_ctxt) * 0.01;
                hatch->spacing_type = HPGL2_HATCH_SPACING_PLOTTER;
              }
            }
            else {

              if ( space < 0.0 )
                return TRUE; /* syntax error */

              /* record whether the spacing is recorded in current units
               * or plotter units.
               * If the spacing is given in terms of plotter units in the job,
               * plot size scaling must be applied.
               * The actual spacing recorded will be real plotter units.
               */
              if ( hpgl2_is_scaled(pcl5_ctxt) ) {
                hatch->spacing_type = HPGL2_HATCH_SPACING_USER;
                hatch->spacing = space;
              }
              else
              {
                hatch->spacing_type = HPGL2_HATCH_SPACING_PLOTTER;
                hatch->spacing = space * horizontal_scale_factor(pcl5_ctxt);
              }
            }

            /* angle. */
            if ( hpgl2_scan_separator(pcl5_ctxt) > 0
                && hpgl2_scan_real(pcl5_ctxt, &angle) > 0 ) {
              if ( angle < 0 )
                return TRUE; /* syntax error */
              angle = fmod( angle, 360.0);
              hatch->angle = DEG_TO_RAD * angle;
            }
          }
          else {
            /* The refence printer apparently allows plotter units
             * to be reinterpreted as user units.
             */
            if ( hatch->spacing_type != HPGL2_HATCH_SPACING_DEFAULT ) {
              HQASSERT( hatch->spacing > 0, "Illegal hatch spacing");
              hatch->spacing_type = hpgl2_is_scaled(pcl5_ctxt) ?
                                      HPGL2_HATCH_SPACING_USER :
                                      HPGL2_HATCH_SPACING_PLOTTER;
            }
          }
        }
        break;

      case HPGL2_FILL_SHADING: /* shading */
        {
          HPGL2FillShading *shading = &fill_params->shading;
          HPGL2Real level = 0;

          if ( hpgl2_scan_separator(pcl5_ctxt) > 0
               && hpgl2_scan_real(pcl5_ctxt, &level) > 0 ) {
            shading->level = level;
            /* ignore trailing options, if any. */
            (void)hpgl2_scan_separator(pcl5_ctxt);
            (void)hpgl2_scan_real(pcl5_ctxt, &dummy);

          }
          /* else, use previous shading state. */
        }
        break;

      case HPGL2_FILL_USER: /* HPGL2 user defined. */
        {
          HPGL2FillUser *user = &fill_params->user;
          HPGL2Real index = 0.0;
          HPGL2Real pen_option = 0.0;

          if ( hpgl2_scan_separator(pcl5_ctxt) > 0
              && hpgl2_scan_real(pcl5_ctxt, &index) > 0 ) {
            if ( index >= 1 && index <= 8 )
              user->index = (HPGL2Integer)index;
            /* else, just pass over out of range index. */

            if ( hpgl2_scan_separator(pcl5_ctxt)
               && hpgl2_scan_real(pcl5_ctxt, &pen_option) ) {
              if ( pen_option == 1.0f )
                user->pen_choice = 1;
              else if ( pen_option == 0.0f)
                user->pen_choice = 0;
              /* else, reuse existing option */
            }
          }
          /* else, use previous shading state. */
        }
        break;

      case HPGL2_FILL_PCL_CROSSHATCH: /* PCL cross hatch */
        {
          HPGL2FillPCLHatch *pcl_hatch = &fill_params->pcl_hatch;
          HPGL2Real pattern_type = 0.0;

          if ( hpgl2_scan_separator(pcl5_ctxt) > 0
              && hpgl2_scan_real(pcl5_ctxt, &pattern_type) > 0 ) {
            pcl_hatch->type = (HPGL2Integer)pattern_type;
            /* ignore trailing options, if any. */
            (void)hpgl2_scan_separator(pcl5_ctxt);
            (void)hpgl2_scan_real(pcl5_ctxt, &dummy);
          }
        }
        break;

      case HPGL2_FILL_PCL_USER: /* PCL user defined. */
        {
          HPGL2FillPCLUser *pcl_user = &fill_params->pcl_user;
          HPGL2Real pattern_id = 0;

          if ( hpgl2_scan_separator(pcl5_ctxt ) > 0
              && hpgl2_scan_real(pcl5_ctxt, &pattern_id) > 0 ) {
            pcl_user->pattern_id = (HPGL2Integer)pattern_id;
            /* ignore trailing options, if any. */
            (void)hpgl2_scan_separator(pcl5_ctxt);
            (void)hpgl2_scan_real(pcl5_ctxt, &dummy);
          }
        }
        break;

      default:
        return TRUE; /* syntax error */
      break;
    }

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  }

  fill_params->fill_type = CAST_SIGNED_TO_UINT8(fill_type);
  return TRUE ;
}

void hpgl2_linefill_handle_scale_off(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HPGL2FillHatching *hatch = &linefill_info->fill_params.hatch;

  HPGL2Point distance;
  /* When scaling is turned off, the spacing for hatched fills must be
   * translated into plotter units.
   * fixing the hatching distance is done in terms of the x axis. */

  if ( hatch->spacing_type == HPGL2_HATCH_SPACING_USER ) {
    distance.x = hatch->spacing;
    distance.y = 0.0;
    /* scaled ctm does not rotate so this should give the effect of
     * mapping user distance to plotter distance wrt x axis. */

    job_point_to_plotter_point(pcl5_ctxt, &distance, &distance, TRUE);
    hatch->spacing = distance.x;
    hatch->spacing_type = HPGL2_HATCH_SPACING_PLOTTER;
  }
}

static void hpgl2_sync_lineend(HPGL2LineFillInfo *linefill_info)
{
  switch ( linefill_info->line_end ) {
  case HPGL2_LINE_END_BUTT :
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = BUTT_CAP ;
    break ;
  case HPGL2_LINE_END_SQUARE :
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = SQUARE_CAP ;
    break ;
  case HPGL2_LINE_END_TRIANGLE :
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = TRIANGLE_CAP ;
    break ;
  case HPGL2_LINE_END_ROUND :
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = ROUND_CAP ;
    break ;
  default :
    HQFAIL("Unexpected HPGL2 line end") ;
  }
}

static void hpgl2_sync_linejoin(HPGL2LineFillInfo *linefill_info)
{
  switch ( linefill_info->line_join ) {
  case HPGL2_LINE_JOIN_MITRED :
    gstateptr->thestyle.linejoin = MITERCLIP_JOIN ;
    break ;
  case HPGL3_LINE_JOIN_MITRED_BEVELED :
    gstateptr->thestyle.linejoin = MITER_JOIN ;
    break ;
  case HPGL2_LINE_JOIN_TRIANGULAR :
    gstateptr->thestyle.linejoin = TRIANGLE_JOIN ;
    break ;
  case HPGL2_LINE_JOIN_ROUNDED :
    gstateptr->thestyle.linejoin = ROUND_JOIN ;
    break ;
  case HPGL2_LINE_JOIN_BEVELED :
    gstateptr->thestyle.linejoin = BEVEL_JOIN ;
    break ;
  case HPGL2_LINE_JOIN_NONE :
    gstateptr->thestyle.linejoin = NONE_JOIN ;
    break ;
  default :
    HQFAIL("Unexpected HPGL2 line join") ;
  }
}

static void hpgl2_sync_miterlimit(HPGL2LineFillInfo *linefill_info)
{
  gstateptr->thestyle.miterlimit = (USERVALUE)linefill_info->miter_limit ;
}

static Bool hpgl2_parse_lineattribute(PCL5Context *pcl5_ctxt, HPGL2LineFillInfo *linefill_info)
{
  int32 kind ;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &kind) <= 0 ||
       hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    return FALSE ;

  switch ( kind ) {
  case 1 : {
    int32 line_end ;

    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &line_end) <= 0 )
      return FALSE ;

    if ( line_end < 1 || line_end > 4 )
      return FALSE ;

    linefill_info->line_end = CAST_SIGNED_TO_UINT8(line_end) ;

    hpgl2_sync_lineend(linefill_info) ;
    break ;
  }
  case 2 : {
    int32 line_join ;

    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &line_join) <= 0 )
      return FALSE ;

    if ( line_join < 1 || line_join > 6 )
      return FALSE ;

    linefill_info->line_join = CAST_SIGNED_TO_UINT8(line_join) ;

    hpgl2_sync_linejoin(linefill_info) ;
    break ;
  }
  case 3 : {
    HPGL2Real miter_limit ;

    if ( hpgl2_scan_clamped_real(pcl5_ctxt, &miter_limit) <= 0 )
      return FALSE ;

    if ( miter_limit < 1 )
      miter_limit = 1 ;

    linefill_info->miter_limit = miter_limit ;

    hpgl2_sync_miterlimit(linefill_info) ;
    break ;
  }
  default :
    return FALSE ;
  }

  return TRUE ;
}

Bool hpgl2op_LA(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  uint8 terminator ;

  hpgl2_linetype_clear(linefill_info) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    linefill_info->line_end = HPGL2_LINE_END_BUTT ;
    hpgl2_sync_lineend(linefill_info) ;

    linefill_info->line_join = HPGL2_LINE_JOIN_MITRED ;
    hpgl2_sync_linejoin(linefill_info) ;

    linefill_info->miter_limit = 5 ;
    hpgl2_sync_miterlimit(linefill_info) ;
    return TRUE ;
  }

  if ( !hpgl2_parse_lineattribute(pcl5_ctxt, linefill_info) )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    return TRUE ;

  if ( !hpgl2_parse_lineattribute(pcl5_ctxt, linefill_info) )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    return TRUE ;

  if ( !hpgl2_parse_lineattribute(pcl5_ctxt, linefill_info) )
    return TRUE ;

  return TRUE ;
}

static Bool hpgl2_sync_line_type(PCL5Context *pcl5_ctxt, HPGL2LineTypeInfo *line_type)
{
  static HPGL2Real fixed_line_types[8][9] = {
    { 2, 0.0, 1.0 },
    { 2, 0.5, 0.5 },
    { 2, 0.7, 0.3 },
    { 4, 0.8, 0.1, 0.0, 0.1 },
    { 4, 0.7, 0.1, 0.1, 0.1 },
    { 6, 0.5, 0.1, 0.1, 0.1, 0.1, 0.1 },
    { 6, 0.7, 0.1, 0.0, 0.1, 0.0, 0.1 },
    { 8, 0.5, 0.1, 0.0, 0.1, 0.1, 0.1, 0.0, 0.1 }
  } ;
  static HPGL2Real adaptive_line_types[8][10] = {
    { 3, 0.0, 1.0, 0.0 },
    { 3, .25, 0.5, .25 },
    { 3, .35, 0.3, .35 },
    { 5, 0.4, 0.1, 0.0, 0.1, 0.4 },
    { 5, .35, 0.1, 0.1, 0.1, .35 },
    { 7, .25, 0.1, 0.1, 0.1, 0.1, 0.1, .25 },
    { 7, .35, 0.1, 0.0, 0.1, 0.0, 0.1, .35 },
    { 9, .25, 0.1, 0.0, 0.1, 0.1, 0.1, 0.0, 0.1, .25 }
  } ;
  HPGL2Real *dashlist = NULL, dashlistarray[20] ;
  HPGL2Real offset = 0 ;
  uint16 dashlistlen = 0 ;

  switch ( line_type->type ) {
  case 0 :
    /* Draw a dot at each x,y coord */
    dashlist = dashlistarray ;
    dashlist[0] = 0 ;
    dashlist[1] = 1 ;
    dashlistlen = 2 ;
    gstateptr->thestyle.dashmode = DASHMODE_PERCENTAGE ;
    break ;
  case HPGL2_LINETYPE_SOLID :
    /* Solid */
    dashlist = NULL ;
    dashlistlen = 0 ;
    gstateptr->thestyle.dashmode = DASHMODE_FIXED ;
    break ;
  case 8 : case 7 : case 6 : case 5 : case 4 : case 3 : case 2 : case 1 :
    /* Fixed line types */
    if ( line_type->user_linetype[line_type->type - 1][0] > 0 )
      dashlist = line_type->user_linetype[line_type->type - 1] ;
    else
      dashlist = fixed_line_types[line_type->type - 1] ;
    dashlistlen = (uint16)dashlist[0] ; /* first slot is length */
    ++dashlist ;
    gstateptr->thestyle.dashmode = DASHMODE_FIXED ;
    break ;
  case -1 : case -2 : case -3 : case -4 : case -5 : case -6 : case -7 : case -8 :
    /* Adaptive line types */
    if ( line_type->user_linetype[-line_type->type - 1][0] > 0 )
      dashlist = line_type->user_linetype[-line_type->type - 1] ;
    else
      dashlist = adaptive_line_types[-line_type->type - 1] ;
    dashlistlen = (uint16)dashlist[0] ; /* first slot is length */
    ++dashlist ;
    gstateptr->thestyle.dashmode = DASHMODE_ADAPTIVE ;
    break ;
  default :
    HQFAIL("Unexpected HPGL2 line type pattern") ;
    return TRUE ;
  }

  if ( line_type->type != 0 && line_type->type != HPGL2_LINETYPE_SOLID ) {
    HPGL2Real scaling ;
    int32 i ;

    if ( line_type->mode == 0 ) {
      /* Line type is relative to P1, P2 scaling. */
      scaling = line_type->pattern_length * 0.01 * get_p1_p2_distance(pcl5_ctxt) ;
    } else {
      /* Line type is absolute. */
      scaling = line_type->pattern_length * 40 ;
    }

    for ( i = 0 ; i < dashlistlen ; ++i ) {
      dashlistarray[i] = dashlist[i] * scaling ;
    }
    dashlist = dashlistarray ;

    if ( (dashlistlen & 1) != 0 ) {
      /* Convert odd-lengthed dashlists to even.
         This avoids lots of nasty special cases in the stroke code. */
      offset = dashlist[dashlistlen - 1] ;
      dashlist[0] += offset ;
      --dashlistlen ;
    }
  }

  gstateptr->thestyle.dashoffset = (USERVALUE)offset ;

  if ( !gs_storedashlist(&gstateptr->thestyle, dashlist, dashlistlen) )
    return FALSE ;

  return TRUE ;
}

void hpgl2_linetype_clear(HPGL2LineFillInfo *linefill_info)
{
#if 0
  /* PCL COMPATIBILITY ISSUE.
   * The HP4700 does not clear the previous line type, but does clear the
   * residue.
   */

  /* For the operators calling this function, plotting with a solid line type
     clears the previous line type and a subsequent LT99 has no effect. */
  if ( linefill_info->line_type.type == HPGL2_LINETYPE_SOLID )
    linefill_info->previous_line_type_valid = FALSE ;
#endif

  linefill_info->line_type.residue = 0 ;
}

Bool hpgl2op_LT(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2Integer type = linefill_info->line_type.type ;
  HPGL2Real pattern_length = linefill_info->line_type.pattern_length ;
  HPGL2Integer mode = linefill_info->line_type.mode ;
  uint8 terminator ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 )
    return LT_default(pcl5_ctxt);

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &type) <= 0 )
    return TRUE ;

  if ( type == 99 ) {
    /* PCL COMPATIBILITY
     * The PCL specification claims that LT99 requires current pen position
     * be same as when change to solid line type was done, in order for LT99
     * to apply. However, this is not what happens on the HP4650/HP4250
     * reference printers which ignore constraints on pen position.
    if ( fabs(current_point.x - linefill_info->linetype99_point.x) > EPSILON ||
         fabs(current_point.y - linefill_info->linetype99_point.y) > EPSILON )
      return TRUE ;

    */
    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
    /* PCL COMPATIBILITY
     * reference printers appear to force LT to be non-adaptive on LT99 */
    return restore_previous_linetype_internal(pcl5_ctxt, TRUE);
  }

  if ( type < -8 || type > 8 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    goto set_line_type ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &pattern_length) <= 0 )
    return TRUE ;

  if ( pattern_length < 0 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    goto set_line_type ;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &mode) <= 0 )
    return TRUE ;

  if ( mode != 0 && mode != 1 )
    return TRUE ;

  if ( mode == 0 && pattern_length > 100 )
    return TRUE ;

 set_line_type :
  return LT_internal(pcl5_ctxt, type, pattern_length, mode, 0);
}

static Bool set_line_type_internal(PCL5Context *pcl5_ctxt,
                 HPGL2Integer type,
                 HPGL2Real pattern_length,
                 HPGL2Integer mode,
                 HPGL2Real residue )
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  linefill_info->line_type.type = type ;
  linefill_info->line_type.pattern_length = pattern_length ;
  linefill_info->line_type.mode = mode ;
  linefill_info->line_type.residue = residue ;

  return hpgl2_sync_line_type(pcl5_ctxt, &linefill_info->line_type);
}

Bool LT_internal(PCL5Context *pcl5_ctxt,
                 HPGL2Integer type,
                 HPGL2Real pattern_length,
                 HPGL2Integer mode,
                 HPGL2Real residue)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  /* LT99 is not an actual line type to apply, but a request to reinstate
   * saved LT information. */
  HQASSERT(type != 99, "Incorrect line type");

  HQASSERT( type == HPGL2_LINETYPE_SOLID || (type <= 8 && type >= -8),
            "Invalid line type" );

  /* Experiment with the reference printer (HP4650/HP4250) suggests that
   * repeated use of the LT operator will not clear the saved line type data.
   * Only printer reset, IN or DF will invalid previously saved line type.
   * In essence, don't use solid line type as a valid previous line type.
   */
  if ( linefill_info->line_type.type != HPGL2_LINETYPE_SOLID ) {
    linefill_info->previous_line_type_valid = TRUE;
    linefill_info->previous_line_type = linefill_info->line_type; /* struct copy*/

    /* PCL COMPATIBILITY
     * Reference printer HP4250 does not record the residue, despite what
     * the PCL documentation says.
     */
    linefill_info->previous_line_type.residue = 0;

  }

  return set_line_type_internal(
         pcl5_ctxt,
         type,
         pattern_length,
         mode,
         residue );
}

Bool LT_default(PCL5Context *pcl5_ctxt)
{
  /* PCL COMPATIBILITY
   * Note that PCL specification constrians the use of LT99 in that LT99
   * only has an effect in pen position at time of LT99 operator is same as
   * pen position when solid LT was set. The HP4250/HP4650 reference printers
   * ignore this constraint. To follow the specification requires that the
   * current point at time of LT; operator be recorded.
    if ( !gs_currentpoint(&gstateptr->thepath,
                          &linefill_info->linetype99_point.x,
                          &linefill_info->linetype99_point.y) )
   */
  return LT_internal(pcl5_ctxt, HPGL2_LINETYPE_SOLID, 4, 0, 0);
}

Bool restore_previous_linetype_internal(PCL5Context *pcl5_ctxt,
                                        Bool force_non_adaptive)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);


  if ( linefill_info->line_type.type == HPGL2_LINETYPE_SOLID
      && linefill_info->previous_line_type_valid ) {
    int32 local_line_type = linefill_info->previous_line_type.type;

    /* PCL COMPATIBILITY
     * The reference printers ( HP 4250 / HP4700 ) appear to force adaptive
     * line types to their non-adaptive type when LT99 is used to restore
     * the line type.  */
    if ( force_non_adaptive && ( local_line_type < 0 ) )
      local_line_type = -local_line_type;

    /* PCL COMPATIBILITY
     * HP4250/HP4700 reference printers clear residue on restoring line type. */
    return set_line_type_internal(
             pcl5_ctxt,
             local_line_type,
             linefill_info->previous_line_type.pattern_length,
             linefill_info->previous_line_type.mode,
             linefill_info->previous_line_type.residue );
  }
  else
    return TRUE;
}

Bool hpgl2op_PW(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = NULL;
  HPGL2Real pen_width;
  HPGL2Integer pen;
  uint8 terminator;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* default mode */
    hpgl2_set_pen_width(pcl5_ctxt,
                        HPGL2_ALL_PENS, /* linefill_info->selected_pen, */
                        hpgl2_get_default_pen_width(
                          linefill_info->pen_width_selection_mode));
    return TRUE;
  }

  /* must have width if we're here */
  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &pen_width ) <= 0 )
    return TRUE; /* syntax error - ignore command. */

  /* check if pen specified. */
  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 ) {
    pen = HPGL2_ALL_PENS;
  }
  else if ( hpgl2_scan_integer(pcl5_ctxt, &pen) <= 0 ) {
    return TRUE; /* syntax error - ignore command. */
  }

  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  /* parameter value check */
  if ( pen_width >= (HPGL2Real)-32678.0 && pen_width <= (HPGL2Real)32677.0
      && ( pen == HPGL2_ALL_PENS || hpgl2_pen_in_range(pcl5_ctxt, pen) ) ) {
    hpgl2_set_pen_width(pcl5_ctxt, pen, pen_width);
  }

  hpgl2_linetype_clear(linefill_info) ;

  return TRUE ;
}

/* Architetural limits means pen indices must be in range 0 to 255, */
Bool hpgl2op_RF(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer index;
  uint8 terminator;
  Bool errored = FALSE;
  pcl5_pattern *new_entry = NULL;
  HPGL2LineFillInfo *linefill_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "Null PCL context");

  if ( pcl5_ctxt == NULL )
    return TRUE;

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {
    pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.hpgl2_user, FALSE) ;
    /* reset all patterns */
  }
  else {

    /* Some jobs (Genoa!) have bogus comma before the RF index. */
    (void)hpgl2_scan_separator(pcl5_ctxt);

    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &index) <= 0 )
      return TRUE; /* syntax error. */

    if ( index < 1 || index > 8 )
      return TRUE; /* syntax error. Only 8 patterns. */

    if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {
      pcl5_id_cache_remove(pcl5_ctxt->resource_caches.hpgl2_user, (pcl5_resource_numeric_id)index, FALSE) ;
      /* reset pattern at index. */

    }
    else {
      HPGL2Integer width;
      HPGL2Integer height;

      if (hpgl2_scan_separator(pcl5_ctxt) <= 0
          || hpgl2_scan_clamped_integer(pcl5_ctxt, &width) <= 0
          || hpgl2_scan_separator(pcl5_ctxt) <= 0
          || hpgl2_scan_clamped_integer(pcl5_ctxt, &height) <= 0 ) {
        /* syntax error - must have width and height together. */
        return TRUE;
      }
      else {
        uint8 acc;
        uint8* data = NULL;
        uint32 i,j,k;
        HPGL2Integer pen;
        pcl5_pattern RF_pattern;

        if ( width > 255 || width < 1 ) {
          HQFAIL("RF width out of range. ");
          return TRUE; /* error - can't define this pattern.
                          Parser will have to skip the data. */
        }

        if ( height > 255 || height < 1 ) {
          HQFAIL("RF height out of range. ");
          return TRUE; /* error - can't define this pattern.
                          Parser will have to skip the data. */
        }

        RF_pattern.detail.resource_type= SW_PCL5_PATTERN;
        RF_pattern.detail.numeric_id = (pcl5_resource_numeric_id)index;
        RF_pattern.detail.permanent = FALSE;
        RF_pattern.detail.private_data = NULL;
        RF_pattern.detail.PCL5FreePrivateData = NULL ;
        RF_pattern.detail.device = NULL;
        RF_pattern.height = height;
        RF_pattern.width = width;

        /* Pattern depth depends on whether there is color support.
         * As HPGL has no upfront declaration as to required pen depth for
         * raster fills, any pattern that could be colored has to start out
         * as 8 bit.
         */
        /**
         * \todo @@@ TODO colored pattern support in the core is restricted
         * to 8 bits.
         */
        RF_pattern.bits_per_pixel = pcl5_ctxt->pcl5c_enabled ? 8 : 1;
        RF_pattern.color = pcl5_ctxt->pcl5c_enabled;

        RF_pattern.x_dpi = RF_pattern.y_dpi =
                            ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt));
        RF_pattern.stride =
          (pcl5_id_cache_pattern_data_size(RF_pattern.width,
                                          RF_pattern.height,
                                          RF_pattern.bits_per_pixel)
          / RF_pattern.height);

        RF_pattern.data = NULL;

        if (! pcl5_id_cache_insert_pattern(
                pcl5_ctxt->resource_caches.hpgl2_user,
               (pcl5_resource_numeric_id)index, &RF_pattern, &new_entry))
          return FALSE; /* VMERROR */

        data = new_entry->data;

        /* default raster data to white */
        HqMemZero((uint8 *)data, new_entry->stride * new_entry->height);

        if ( new_entry->bits_per_pixel == 1 ) {
          /* Read pen indices as ints, but map them to bits and pack them
           * into the user pattern.
           */
          uint32 bits_over;
          uint32 full_units;
          uint32 bits_per_line;

          new_entry->highest_pen = 1;

          bits_per_line = (uint32)width;
          bits_over = bits_per_line % 8 ;
          full_units = bits_per_line / 8 ;

          for (k = 0; k < (uint32)height; k++) {
            for (j = 0; j < full_units; j++) {
              acc = 0;
              for (i = 0; i < 8; i++) {
                /* pack data into bytes. */
                if ( hpgl2_scan_separator(pcl5_ctxt) <= 0
                    || hpgl2_scan_integer(pcl5_ctxt, &pen) <= 0 ) {
                  /* syntax error. */
                  errored = TRUE;
                  goto bail_out;
                }
                else
                  acc = ( acc << 1 ) | ( pen == 0 ? 0 : 1 ) ;
              }
              *data++ = acc;
            }

            if ( bits_over ) {
              acc = 0;
              for (i = 0; i < bits_over; i++) {
                if ( hpgl2_scan_separator(pcl5_ctxt) <= 0
                    || hpgl2_scan_integer(pcl5_ctxt, &pen) <= 0 ) {
                    /* syntax error. */
                  errored = TRUE;
                  goto bail_out;
                }
                else
                  acc = ( acc << 1 ) | ( pen == 0 ? 0 : 1 ) ;
              }

              acc = (uint8) (acc << ( 8 - bits_over ));

              *data++ = acc;
            }
          }
        }
        else
        {
          new_entry->highest_pen = 1;
          for (k = 0; k < (uint32)height; k++) {
            for (j = 0; j < (uint32)width; j++) {
              if ( hpgl2_scan_separator(pcl5_ctxt) <= 0
                  || hpgl2_scan_integer(pcl5_ctxt, &pen) <= 0 ) {
                /* syntax error. */
                errored = TRUE;
                goto bail_out;
              }
              *data++ = CAST_SIGNED_TO_UINT8(pen);

              if (pen > new_entry->highest_pen) {
                new_entry->highest_pen = pen;
              }
            }
          }
        }
      }
      (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
    }
  }

  /* Check for solid-black pattern. */
  if (new_entry != NULL && pattern_is_black(new_entry)) {
    /* The pattern is all black and thus can be replaced with the default (NULL)
     * pattern. We indicate this by releasing the pattern raster data; we can't
     * simply remove the pattern from the cache as missing patterns have a
     * different behavior already (i.e. objects drawn in missing patterns are
     * not rendered). */
    pcl5_id_cache_release_pattern_data(pcl5_ctxt->resource_caches.hpgl2_user,
                                       new_entry->detail.numeric_id) ;
  }

bail_out:
  if ( errored && hpgl2_scan_terminator(pcl5_ctxt,  &terminator) > 0 ) {
    /* Encountering an early end of data is OK for defining a pattern. */
    /* fill the rest of the pattern with white. */
    errored = FALSE;
  }

  if ( errored ) {
    if ( new_entry != NULL ) {
      pcl5_id_cache_remove(pcl5_ctxt->resource_caches.hpgl2_user,  new_entry->detail.numeric_id, FALSE);
      new_entry = NULL;
    }
  }

  hpgl2_linetype_clear(linefill_info) ;

  return TRUE ;
}

Bool hpgl2op_SM(PCL5Context *pcl5_ctxt)
{
#if defined( ASSERT_BUILD )
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
#endif
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  int32 ch ;

  HQASSERT(!print_state->SM_points, "Should not have any SM points stored") ;

  if ( !pcl5_ctxt->config_params.two_byte_char_support_enabled ) {

    /* Always scan the next character; it's either a ';' or the symbol character.
     * The technical reference actually says that both are optional, but that is
     * pretty much impossible to parse, so we'll assume that the terminator is
     * not optional. */
    ch = pcl5_ctxt->last_char;
    if (ch == EOF)
      return TRUE ;

    /* Do we have a character of a semi-colon (code 59)? */
    if ( ch == 59 ) {
      linefill_info->symbol_mode_char = NUL ;
      pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
      return TRUE ;
    }

    if ( (33 <= ch && ch <= 58) || (60 <= ch && ch <= 126) || ch == 161 || ch == 254 ) {
      /* SM includes most printing chars, but excludes semi-colon. */
      linefill_info->symbol_mode_char = CAST_SIGNED_TO_UINT16(ch) ;
    } else {
      linefill_info->symbol_mode_char = NUL ;
    }
    pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
    return TRUE ;
  }
  else {
    int32 SM_ch;
    HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt);

    SM_ch = pcl5_ctxt->last_char;
    if (SM_ch == EOF)
      return TRUE;

    HQASSERT( SM_ch >= 0 && SM_ch <= 255, "Unusual SM character");

    if ( char_info->label_mode == 1 || char_info->label_mode == 3 ) {
      int32 ch2;
      /* always read 2 characters. To disable symbol mode in 16 bit mode
       * required SM<null>;
       */

      ch2 = Getc(pcl5_ctxt->flptr);
      if (ch2 == EOF) {
        pcl5_ctxt->last_char = ch2;
        return TRUE;
      }

      HQASSERT( ch2 >= 0 && ch2 <= 255, "Unusual SM character");

      SM_ch = ( ( SM_ch << 8 ) + (uint8)ch2 );
    }

    pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);

    /* disable SM */
    if ( SM_ch == 59 ) {
      linefill_info->symbol_mode_char = NUL;
      return TRUE;
    }

    /* The HP documentation on 2 byte support indicates a different range of
     * allowable characters to the PCL technical reference.
     */
    if ( SM_ch == 0 || SM_ch == 5 || SM_ch == 27 )
      linefill_info->symbol_mode_char = NUL;
    else
      linefill_info->symbol_mode_char = CAST_SIGNED_TO_UINT16(SM_ch);

    return TRUE;
  }
  /* NOTREEACHED */
}

void hpgl2_set_current_pen(PCL5Context *pcl5_ctxt, HPGL2Integer pen,
                           Bool force_solid)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  ColorPalette *palette = NULL;

  palette = get_active_palette(pcl5_ctxt);

  HQASSERT(pen >= 0 || pen == HPGL2_NO_PEN, "Bad pen value.");
  HQASSERT(palette != NULL, "Must have palette while in HPGL2");

  if ( pen == HPGL2_NO_PEN ) {
    set_hpgl2_default_pen(pcl5_ctxt);
    return;
  }

  /* ignore the selection of pen in polygon mode. PE might attempt to invoke
   * pen selection while in polygon mode. */
  if ( hpgl2_in_polygon_mode(pcl5_ctxt) )
    return;

  pen = hpgl2_map_pen_to_palette(palette, pen);

  linefill_info->selected_pen = pen;
  hpgl2_sync_linewidth(pcl5_ctxt);
  hpgl2_sync_pen_color(pcl5_ctxt, force_solid);
  /* PCL_COMPATIBILITY
   * HP4650 HP4250 printers do not appear to clear previous line info
   * when pen selection changes, despite what the PCL specification says.
   * Residue for current pen is cleared, in reference printers.
   hpgl2_linetype_clear(linefill_info) ;
   */

  linefill_info->line_type.residue = 0.0;
}

HPGL2Integer hpgl2_get_current_pen(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  return linefill_info->selected_pen ;
}

Bool hpgl2op_SP(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer pen;
  uint8 terminator;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* no parameters : do default */
    set_hpgl2_default_pen(pcl5_ctxt);
    return TRUE;
  }

  if ( hpgl2_scan_integer(pcl5_ctxt, &pen) > 0 ) {
    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
    if ( pen >= 0 )
      hpgl2_set_current_pen(pcl5_ctxt, pen, FALSE);
  }

  return TRUE ;
}

/* Much the same as the FT operator processing. */
Bool hpgl2op_SV(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer fill_type;
  uint8 terminator;
  HPGL2LineFillInfo *line_info;
  HPGL2SVParams *sv_params;

  HPGL2Integer dummy;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  sv_params = &line_info->screened_vectors;

  /* syntax: fill type, option1, option2 */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 )
    fill_type = HPGL2_SV_SOLID;
  else {

    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &fill_type) <= 0 )
      return TRUE; /* syntax error */

    switch ( fill_type )
    {
    case HPGL2_SV_SOLID: /* solid black */
      /* ignore options, if any. */
      (void)hpgl2_scan_separator(pcl5_ctxt);
      (void)hpgl2_scan_clamped_integer(pcl5_ctxt, &dummy);
      (void)hpgl2_scan_separator(pcl5_ctxt);
      (void)hpgl2_scan_clamped_integer(pcl5_ctxt, &dummy);
      break;

    case HPGL2_SV_SHADING: /* shading */
      {
        HPGL2FillShading *shading = &sv_params->shading;
        HPGL2Integer level = 0;

        if ( hpgl2_scan_separator(pcl5_ctxt) > 0
             && hpgl2_scan_clamped_integer(pcl5_ctxt, &level) > 0 ) {
          shading->level = level;
          /* ignore trailing options, if any. */
          (void)hpgl2_scan_separator(pcl5_ctxt);
          (void)hpgl2_scan_clamped_integer(pcl5_ctxt, &dummy);

        }
        /* else, use previous shading state. */
      }
      break;

    case HPGL2_SV_USER: /* HPGL2 user defined. */
      {
        HPGL2FillSVUser *user = &sv_params->user;
        HPGL2Integer index = 0;
        HPGL2Integer pen_choice = 1; /* Force black. */

        if ( hpgl2_scan_separator(pcl5_ctxt) > 0
             && hpgl2_scan_clamped_integer(pcl5_ctxt, &index) > 0 ) {

          if ( index >= 1 && index <= 8 )
            user->index = (HPGL2Integer)index;
          /* else, just pass over out of range inde & use previous state. */

          /* check for pen choice option */
          if ( hpgl2_scan_separator(pcl5_ctxt) > 0
               && hpgl2_scan_clamped_integer(pcl5_ctxt, &pen_choice ) > 0 ) {
            if ( pen_choice == 0 || pen_choice == 1 )
              user->pen_choice = pen_choice;
            /* pass over invalid options. */
          }

        }
        /* else, use previous shading state. */
      }
      break;

    case HPGL2_SV_PCL_CROSSHATCH: /* PCL cross hatch */
      {
        HPGL2FillPCLHatch *pcl_hatch = &sv_params->pcl_hatch;
        HPGL2Integer pattern_type = 0;

        if ( hpgl2_scan_separator(pcl5_ctxt) > 0
             && hpgl2_scan_clamped_integer(pcl5_ctxt,
                                           &pattern_type) > 0 ) {
          pcl_hatch->type = (HPGL2Integer)pattern_type;
          /* ignore trailing options, if any. */
          (void)hpgl2_scan_separator(pcl5_ctxt);
          (void)hpgl2_scan_clamped_integer(pcl5_ctxt, &dummy);
        }
      }
      break;

    case HPGL2_SV_PCL_USER: /* PCL user defined. */
      {
        HPGL2FillPCLUser *pcl_user = &sv_params->pcl_user;
        HPGL2Integer pattern_id = 0;

        if ( hpgl2_scan_separator(pcl5_ctxt) > 0
             && hpgl2_scan_clamped_integer(pcl5_ctxt,
                                           &pattern_id) > 0 ) {
          pcl_user->pattern_id = (HPGL2Integer)pattern_id;
          /* ignore trailing options, if any. */
          (void)hpgl2_scan_separator(pcl5_ctxt);
          (void)hpgl2_scan_clamped_integer(pcl5_ctxt, &dummy);
        }
      }
      break;

    default:
      return TRUE; /* syntax error */
      break;
    }

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  }

  sv_params->fill_type = CAST_SIGNED_TO_UINT8(fill_type);
  return TRUE ;
}

Bool hpgl2op_TR(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2Integer mode;
  uint8 terminator;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0)
    mode = 1;
  else {
    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &mode) <= 0 )
      return TRUE; /* syntax error */

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  }

  if ( mode == 0 || mode == 1 ) {
    linefill_info->transparency = CAST_SIGNED_TO_UINT8(mode);
    hpgl2_sync_transparency(linefill_info);
    hpgl2_linetype_clear(linefill_info) ;
  }

  return TRUE ;
}

Bool hpgl2op_UL(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  int32 linetype_index ;
  HPGL2Real linetype_list[20], *linetype_ptr, sum_of_gaps ;
  uint8 terminator ;
  int32 i ;

  hpgl2_linetype_clear(linefill_info) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Default all line types. */
    for ( i = 0 ; i < 8 ; ++i ) {
      linefill_info->line_type.user_linetype[i][0] = 0 ;
    }
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &linetype_index) <= 0 )
    return TRUE ; /* ignore bad command */

  if ( linetype_index < 0 )
    linetype_index = -linetype_index ;

  if ( linetype_index < 1 || linetype_index > 8 )
    return TRUE ; /* ignore bad command */

  sum_of_gaps = 0 ;

  for ( i = 0 ; i < 20 ; ++i ) {
    if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 ||
         hpgl2_scan_clamped_real(pcl5_ctxt, &linetype_list[i]) <= 0 )
      break ;

    if ( linetype_list[i] < 0 )
      return TRUE ; /* ignore bad command */

    sum_of_gaps += linetype_list[i] ;
  }

  if ( i == 0 ) {
    linefill_info->line_type.user_linetype[linetype_index-1][0] = 0 ;
    return TRUE ;
  }

  if ( sum_of_gaps == 0 ||
       hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 )
    return TRUE ; /* ignore bad command */

  /* Finally, after validation, store the user-defined line type. */
  linetype_ptr = linefill_info->line_type.user_linetype[linetype_index-1] ;
  linetype_ptr[0] = i ; /* put length in the first slot */
  ++linetype_ptr ;
  while ( --i >= 0 ) {
    linetype_ptr[i] = linetype_list[i] / sum_of_gaps ; /* convert to a percentage */
  }

  return TRUE ;
}

Bool hpgl2op_WU(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = NULL ;
  uint8 terminator;
  HPGL2Integer mode = HPGL2_PEN_WIDTH_METRIC;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0 ) {

    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &mode ) <= 0 )
      return TRUE; /* syntax error */

    /* just consume terminator. */
    (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator) ;
  }

  /* WU resets all pen widths */
  if (mode == HPGL2_PEN_WIDTH_METRIC || mode == HPGL2_PEN_WIDTH_RELATIVE) {
    linefill_info->pen_width_selection_mode = mode;
    hpgl2_set_pen_width(pcl5_ctxt,
                        HPGL2_ALL_PENS,
                        hpgl2_get_default_pen_width(mode) );
  }

  hpgl2_linetype_clear(linefill_info) ;

  return TRUE ;
}

Bool hpgl2_fill_path(PCL5Context *pcl5_ctxt,
                     PATHINFO *the_path,
                     int32 fill_mode)
{
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  Bool res = TRUE;

  HQASSERT(fill_mode == NZFILL_TYPE || fill_mode == EOFILL_TYPE,
           "Bad fill type");

  hpgl2_sync_fill_mode(pcl5_ctxt, FALSE);

  /* hatching fills require we draw actual lines rather than blit out
   * patterns. So hatching is a special case.
   * path to be filled will act as clip path over which hatching is to be
   * drawn.
   */
  if ( linefill_info->fill_params.fill_type == HPGL2_FILL_CROSSHATCH
       || linefill_info->fill_params.fill_type == HPGL2_FILL_HATCH ) {
    res = hpgl2_hatchfill_path(pcl5_ctxt, the_path, fill_mode, FALSE);
  }
  else {
    if ( !dofill(the_path, fill_mode, GSC_FILL, FILL_NORMAL) )
      res = FALSE ;
  }

  /* filling the polygon might disturb the PCL pattern associated to
   * the pen color. We assume that the pen settings are the "default"
   * and that fill operation should be "atomic" and not make persistent
   * alterations to parts of gstate associated to the pen.
   */
  hpgl2_sync_pen_color(pcl5_ctxt, FALSE);

  return TRUE;
}

/* Fill the current gstate path according to HPGL state. */
Bool hpgl2_fill(PCL5Context *pcl5_ctxt, int32 fill_mode, Bool newpath)
{
  PATHINFO *the_path = &(thePathInfo(*gstateptr)) ;

  if ( hpgl2_fill_path(pcl5_ctxt, the_path, fill_mode) )
  {
    if ( newpath )
      return gs_newpath() ;
    else
      return TRUE;
  }
  return FALSE;
}

/* ============================================================================
* Log stripped */
