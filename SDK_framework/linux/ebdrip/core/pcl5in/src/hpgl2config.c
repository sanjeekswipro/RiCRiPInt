/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2config.c(EBDSDK_P.1) $
 * $Id: src:hpgl2config.c,v 1.59.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Configuration Group" category.
 * (PG and RP are unsupported)
 *
 *   CO    % Comment
 *   DF    % Default Values
 *   IN    % Initialize
 *   IP    % Input P1 and P2
 *   IR    % Input Relative P1 and P2
 *   IW    % Input Window
 *   RO    % Rotate Coordinate System
 *   SC    % Scale
 */

#include "core.h"
#include "hpgl2config.h"
#include "pcl5context_private.h"
#include "hpgl2vector.h" /* hpgl2_moveto */
#include "hpgl2scan.h"
#include "pictureframe.h"
#include "pcl5ctm.h"
#include "hpgl2linefill.h"
#include "hpgl2polygon.h"
#include "hpgl2fonts.h"
#include "printmodel.h"
#include "pcl5color.h"

#include "fileio.h"
#include "objects.h"
#include "graphics.h" /* set line width */
#include "gstack.h" /* gstateptr */
#include "pathcons.h" /* gs_currentpoint */
#include "clipops.h"  /* gs_initclip */
#include "gu_rect.h"
#include "gu_cons.h" /* gs_moveto */
#include "gu_ctm.h"
#include "pclGstate.h"

static void get_picture_frame_extent(PCL5Context *pcl5_ctxt,
                                     HPGL2Point *extent);

/* See header for doc. */
void default_HPGL2_config_info(HPGL2ConfigInfo* self)
{
  self->scale_enabled = FALSE;
  self->scale_mode = HPGL2_SCALE_ANISOTROPIC;
  self->rotation = 0;

  SETXY(self->window.ll, 0, 0);
  SETXY(self->window.ur, 0, 0);
  self->window.soft_clip_type = HPGL2_NO_SOFT_CLIP;

  SETXY(self->scale_points.p1, 0, 0);
  SETXY(self->scale_points.p2, 0, 0);

  {
    HPGL2ScalingIsoParams params = {0};
    self->scaling_parameters.isotropic = params;
  }
}

/* --- clip state manipulation. --- */

/* Handling of clipping is simple. Two rectangular regions are required. One
 * for the picture frame itself, and an optional one for soft clip window.
 * The clip path stack of the interpreter will be used to implement the
 * clipping. On entry to HPGL2, the current PCL clip path is clipsaved, the
 * picture frame clip is defined and then clipsaved.
 * To handle soft clip, on each definition / update of the softclip window,
 * restore the original picture frame clip (and clipsave it), then intersect
 * new soft clip window with current clip.
 * There is no need to save the intersection of the picture frame clip and the
 * softclip.
 *
 * The HPGL2 interpreter uses currentpoint in the gstate to track pen position.
 *
 */

/** set up the initial picture frame clipping limits. Call this function
 * after any save or gsave done on entry to HPGL2 mode, but before changing
 * the CTM from PCL internal units coord space as the clip is specified in
 * PCL internal units.
 *
 * It leaves two clip paths on the clipstack; PCL current clip path, and the
 * picture frame clip. The picture frame clip is also the current clippath.
 * Any current path in the gstate will be lost on invoking this function.
 *
 * Any current path in the gstate is cleared, and the caller is obliged to
 * set the current point as required. The current point in the gsatte can
 * be recreated from the pen position or the PCL cursor.
 *
 * The assumption is that the HPGL2 clip path is intersected with the
 * existing PCL clip path. If the HPGL2 clip path exists independently, then
 * an init clip will be required after the first gs_cpush.
 */
Bool set_hpgl2_picture_frame_clip(PCL5Context *pcl5_ctxt)
{
  PictureFrameInfo *picture_frame = NULL;
  RECTANGLE rect;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  picture_frame = get_picture_frame_info(pcl5_ctxt);

  rect.x = (SYSTEMVALUE)picture_frame->anchor_point.x;
  rect.y = (SYSTEMVALUE)picture_frame->anchor_point.y;
  rect.w = (SYSTEMVALUE)picture_frame->default_width;
  rect.h = (SYSTEMVALUE)picture_frame->default_height;

  /* The HPGL picture frame is set in print-direction 0. */
  if (! pcl_setctm(ctm_orientation(get_pcl5_ctms(pcl5_ctxt)), FALSE))
    return FALSE;

  return ( gs_cpush()
           && cliprectangles(&rect, 1)
           && gs_cpush() );
}


/** Synchronise the gstate clippath with the current HPGL2 state.
 */
Bool set_hpgl2_soft_clip(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = NULL;
  RECTANGLE rect;
  HPGL2Integer clip_type;
  SYSTEMVALUE curr[2];

  HQASSERT(pcl5_ctxt != NULL, "PCLContext is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  clip_type = config_info->window.soft_clip_type;

  HQASSERT(clip_type == HPGL2_NO_SOFT_CLIP
          || clip_type == HPGL2_SCALABLE_SOFT_CLIP
          || clip_type == HPGL2_FIXED_SOFT_CLIP,
          "Bad soft clip type");

  /* A clip might be specified in a different coord space to that which
   * exists now. E.g. we might be in scaled mode, but if the clip window
   * was specified in absolute coords it must not move with any rescaling.
   */

  if ( clip_type == HPGL2_SCALABLE_SOFT_CLIP ) {
    HPGL2Point ll, ur;

    ll = config_info->window.ll;
    ur = config_info->window.ur;

    if ( !job_point_to_plotter_point(pcl5_ctxt, &ll, &ll, FALSE)
         || !job_point_to_plotter_point(pcl5_ctxt, &ur, &ur, FALSE) )
      return FALSE;

    rect.x = (SYSTEMVALUE)ll.x;
    rect.y = (SYSTEMVALUE)ll.y;
    rect.w = (SYSTEMVALUE)(ur.x - ll.x);
    rect.h = (SYSTEMVALUE)(ur.y - ll.y);

  }
  else if (clip_type == HPGL2_FIXED_SOFT_CLIP) {
    HPGL2Point ll, ur;
    /* (re)-establish the clip in plotter units. */

    ll = config_info->window.ll;
    ur = config_info->window.ur;

    rect.x = (SYSTEMVALUE)ll.x;
    rect.y = (SYSTEMVALUE)ll.y;
    rect.w = (SYSTEMVALUE)(ur.x - ll.x);
    rect.h = (SYSTEMVALUE)(ur.y - ll.y);
  }

  /* if no soft clip, then just restore the picture frame clip. Note that
   * clipping to rectangles clears the current path. The current point
   * existing at entry to the function needs to be preserved.
   */
  return (gs_currentpoint(&gstateptr->thepath , &curr[0], &curr[1])
          && gs_ctop()
          && gs_cpush() /* ensure the picture frame clip is always saved. */
          && ( ( clip_type == HPGL2_NO_SOFT_CLIP)
                || cliprectangles(&rect, 1) )
          && gs_moveto(TRUE, curr, &gstateptr->thepath) );
}

/* Fix a scalable soft clip window to its equivalent plotter coordinates,
 * as defined by the current scaling parameters.
 */
Bool fix_soft_clip(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);

  if ( config_info->window.soft_clip_type == HPGL2_SCALABLE_SOFT_CLIP ) {

    HQASSERT(config_info->scale_enabled,
             "Scalable soft clip window found while in non-scaled mode");

    if ( !job_point_to_plotter_point(pcl5_ctxt,
                                &config_info->window.ll,
                                &config_info->window.ll,
                                FALSE) )
      goto fail;

    if ( !job_point_to_plotter_point(pcl5_ctxt,
                                &config_info->window.ur,
                                &config_info->window.ur,
                                FALSE) )
      goto fail;

    config_info->window.soft_clip_type = HPGL2_FIXED_SOFT_CLIP;
  }

  return TRUE;

fail:
  return FALSE;
}

void hpgl2_default_clip_window(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);

  config_info->window.soft_clip_type = HPGL2_NO_SOFT_CLIP;

  return;
}

/*
 * Call this function to restore the PCL clip path on exit from HPGL2.
 *
 * clip stack will contain :
 *  pcl clip path
 *  pciture frame clip path
 */
Bool restore_pcl_clip(PCL5Context *pcl5_ctxt)
{
  /* inefficient in that is restores the picture frame clip redundantly
   * but there is no simple "clip pop" operation(?)
   */
  UNUSED_PARAM(PCL5Context *, pcl5_ctxt);
  return ( gs_ctop()  && gs_ctop() );
}

/* --- Config state helper functions. --- */

/* The pen position for HPGL2 is established on entry to HPGL
 * mode each time. Changes to the picture frame or PCL page
 * size can side effect the pen position, but the pen position
 * should only be moved to when HPGL2 is entered.
 * changes to HPGL state as a consequence of PCL operations
 * should *not* side effect the interpreter gstate until the
 * HPGL mode is entered.
 */

void hpgl2_handle_reset_pen_position(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;

  print_state->initial_pen_position.x = 0 ;
  print_state->initial_pen_position.y = 0 ;

  /* presume that carriage return point will
     reset at same time as the pen position. */
  print_state->Carriage_Return_point.x = 0;
  print_state->Carriage_Return_point.x = 0;
}

/* define a central point to organize the state changes
 * required when another operator has altered the user
 * space CTM. SC, IP, IR are all capable of altering the
 * definition of user coord system. Such a change might
 * imply other parts of the HPGL2 state be changed.
 * - soft clip window.
 */
Bool handle_user_ctm_change(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);
  Bool res = TRUE;

  if ( config_info->scale_enabled)
    res = set_hpgl2_soft_clip(pcl5_ctxt);

  return res;
}

/* API to default particular parts of HPGL2 state on particular PCL
 * operations. The default HPGL2 state will depend on various current
 * MPE values, e.g. page size.
 *
 * These functions do not change the gstate. If the HPGL2 base coordinate
 * space has chnaged, due to eg. picture frame change, this will be
 * dealt with on entry to HPGL2 mode.
 */

/* Printer reset :
 * - call IN operator.
 * - default picture frame size.
 * - default picture frame anchor point.
 * - default HPGL2 plot size.
 * - default page orientation.
 *
 * Page size must be valid in the MPE when this is called.
 */

void hpgl2_handle_PCL_printer_reset(PCL5Context *pcl5_ctxt)
{
  /* don't want to call the hpgl2op_IN if it parses data from the
   * input stream.
   */
  hpgl2_DF_or_IN_set_default_state(pcl5_ctxt, TRUE); /* IN */
  default_picture_frame_dimensions(pcl5_ctxt); /* frame size */
  default_picture_frame_position(pcl5_ctxt); /* frame anchor */
  default_plot_size(pcl5_ctxt); /* plot size */
  return;
}

/* Change to page size, length or orientation.
 * - default picture frame size.
 * - default picture frame anchor point.
 * - default HPGL2 plot size.
 * - default scaling points.
 * - default soft-clip.
 * - clear polygon buffer.
 * - reset pen position.
 */
void hpgl2_handle_PCL_page_change(PCL5Context *pcl5_ctxt)
{
  default_picture_frame_dimensions(pcl5_ctxt); /* frame size */
  default_picture_frame_position(pcl5_ctxt); /* frame anchor */
  default_plot_size(pcl5_ctxt); /* plot size */
  (void)hpgl2_default_scale_points(pcl5_ctxt); /* scaling points. */
  hpgl2_default_clip_window(pcl5_ctxt); /* soft clip  */
  (void)hpgl2_force_exit_polygon_mode(pcl5_ctxt); /* polygon buffer */
  hpgl2_handle_reset_pen_position(pcl5_ctxt);
  return;
}

/* Redefining the picture frame.
 * - default scaling points
 * - reset soft-clip.
 * - clear polygon buffer.
 * - reset pen position.
 */
void hpgl2_handle_PCL_picture_frame_redefine(PCL5Context *pcl5_ctxt)
{
  (void)hpgl2_default_scale_points(pcl5_ctxt); /* scale points */
  hpgl2_default_clip_window(pcl5_ctxt); /* soft clip  */
  (void)hpgl2_force_exit_polygon_mode(pcl5_ctxt); /* polygon buffer */
  hpgl2_handle_reset_pen_position(pcl5_ctxt);
  return;
}

/* Setting the picture frame anchor points.
 * - default scaling points.
 * - reset soft-clip.
 * - clear polygon buffer.
 * - reset pen position.
 */
void hgpl2_handle_PCL_picture_frame_anchor_change(PCL5Context *pcl5_ctxt)
{
  hpgl2_default_scale_points(pcl5_ctxt); /* scale points */
  hpgl2_default_clip_window(pcl5_ctxt); /* soft clip  */
  (void)hpgl2_force_exit_polygon_mode(pcl5_ctxt); /* polygon buffer */
  hpgl2_handle_reset_pen_position(pcl5_ctxt);
  return;
}

/* Setting HPGL2 plot size.
 * - change picture scaling factor.
 */
void hpgl2_handle_PCL_plot_size_change(PCL5Context *pcl5_ctxt)
{
  UNUSED_PARAM(PCL5Context *, pcl5_ctxt);
  return;
}

/* Set the control flag for the scaling, and update the HPGL2 to device
 * CTM to reflect scaling.
 * This function will eagerly update the gstate CTM.
 */
void set_hpgl2_scaling(PCL5Context *pcl5_ctxt, Bool enable)
{
  HPGL2ConfigInfo *config_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  /* config_info->scale_enabled = enable; */

  /* changes to scaling affect whether or not we need to account for
   * plot size scaling. This means changes to scale state require recalculation
   * of the picture frame CTM.
   */

  if ( config_info->scale_enabled != enable ) {
    Bool res = TRUE;
    config_info->scale_enabled  = enable;
    res = set_hpgl2_ctm(pcl5_ctxt);
    HQASSERT( res, "Failed to set HPGL ctm on scaling state change");
  }

  return;
}

Bool hpgl2_is_rotated(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info;
  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);

  return ( config_info->rotation != 0 );
}

Bool hpgl2_is_scaled(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info;
  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);

  return ( config_info->scale_enabled );
}

/* relative pen sizes need to know the distance p1 to p2. Distance is
 * in PLOTTER units. */
HPGL2Real get_p1_p2_distance(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = NULL;
  HPGL2Real x1,y1,x2,y2;

  config_info = get_hpgl2_config_info(pcl5_ctxt);

  /* p1 and p2 are in terms of PCL internal units. */

  x1 = config_info->scale_points.p1.x;
  y1 = config_info->scale_points.p1.y;
  x2 = config_info->scale_points.p2.x;
  y2 = config_info->scale_points.p2.y;

  return sqrt( (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2) );
}

/*  The effect of scaling and rotation of the HPGL2 coord space are
 *  maintained as a separate CTM. This is manually applied to the
 *  co-ordinates that specify the paths and polygons to be drawn.
 *  An anisotropic transform should not be applied to the gstate CTM
 *  as it will distort e.g. line caps.
 *  The CTM is dependent on both SC and IP(IR) commands' parameters
 *  so is maintained separately.
 */
void hpgl2_update_scaled_ctm(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = NULL;
  PictureFrameInfo *picture_frame_info = NULL;
  HPGL2Ctms *hpgl2_ctms = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  HQASSERT(config_info != NULL, "HPGL2ConfigInfo is NULL");

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(picture_frame_info != NULL, "Picture frame info is NULL");

  hpgl2_ctms = get_hpgl2_ctms(pcl5_ctxt);

  /* the scaled matrix does NOT includes the underlying HPGL2 to
   * PCL internal units (default orientation) space.
   */

  /* calculation of scaled ctm using the current state. */

  switch ( config_info->scale_mode) {
    case HPGL2_SCALE_ANISOTROPIC:
    {
      HPGL2ScalingAnisoParams *aniso_params;
      HPGL2Real x1, x2, y1, y2,
                user_xd, user_yd,
                scale_factor_x, scale_factor_y,
                scale_point_xd, scale_point_yd;
      OMATRIX *scaled_ctm;

      aniso_params = &config_info->scaling_parameters.anisotropic;
      x1 = aniso_params->x1;
      x2 = aniso_params->x2;
      y1 = aniso_params->y1;
      y2 = aniso_params->y2;

      /* scale relates user units to default / plotter units. */
      scale_point_xd =
        config_info->scale_points.p1.x - config_info->scale_points.p2.x;
      scale_point_yd =
        config_info->scale_points.p1.y - config_info->scale_points.p2.y;

      user_xd =  x1 - x2;
      user_yd =  y1 - y2;

      scale_factor_x = scale_point_xd / user_xd ;
      scale_factor_y = scale_point_yd / user_yd ;

      /* scale points imply a translation both a translation and
       * scaling for the user space CTM wrt the plotter units
       * We know where p1 is in terms of the unscaled system, so can find where
       * origin of scaled space is in default space.
       */

      scaled_ctm = &hpgl2_ctms->picture_frame_scaling_ctm;

      scaled_ctm->matrix[0][0] = scale_factor_x;
      scaled_ctm->matrix[0][1] = 0.0;
      scaled_ctm->matrix[1][0] = 0.0;
      scaled_ctm->matrix[1][1] = scale_factor_y;
      scaled_ctm->matrix[2][0] = config_info->scale_points.p1.x
                                  - (x1 * scale_factor_x);
      scaled_ctm->matrix[2][1] = config_info->scale_points.p1.y
                                  - (y1 * scale_factor_y);

      MATRIX_SET_OPT_BOTH(scaled_ctm);

     break;
    }

    case HPGL2_SCALE_ISOTROPIC:
    {
      OMATRIX *scaled_ctm = NULL;
      HPGL2ScalingIsoParams *iso_params;
      HPGL2Real user_xd, user_yd,
                scale_factor_x, scale_factor_y,
                scale_point_xd, scale_point_yd,
                abs_unit_x, abs_unit_y,
                x1,x2,y1,y2,
                left, bottom;

      /* isotropic scaling requires that the user scaling be applied
       * to both X and Y axis - the user unit size should be the same
       * in both axes.
       * Need the smaller of the scales because we are required to ensure that
       * all of the specified Xmin,Xmax and Ymin,Ymax ranges are representable.
       * It is only the magnitude of the scale that changes, not its sign.
       * The documentation implies that only one of the axes can have its
       * scale changed.
       */

      iso_params = &config_info->scaling_parameters.isotropic;

      HQASSERT(iso_params->bottom >= 0.0 && iso_params->bottom <= 100.0,
              "Bad bottom parameter" );
      HQASSERT(iso_params->left >= 0.0 && iso_params->left <= 100.0,
              "Bad top parameter" );

      scaled_ctm = &hpgl2_ctms->picture_frame_scaling_ctm;

      scale_point_xd =
        config_info->scale_points.p1.x - config_info->scale_points.p2.x;
      scale_point_yd =
        config_info->scale_points.p1.y - config_info->scale_points.p2.y;

      left = iso_params->left;
      bottom = iso_params->bottom;
      x1 = iso_params->x1;
      x2 = iso_params->x2;
      y1 = iso_params->y1;
      y2 = iso_params->y2;
      user_yd = y1 - y2;  /* user unit distance covered by Ymin,Ymax */
      user_xd = x1 - x2;

      /* scales before iso adjustment. */
      scale_factor_y = scale_point_yd / user_yd;
      scale_factor_x = scale_point_xd / user_xd;

      /* sizes of user units per scale. */
      abs_unit_x = fabs(scale_factor_x);
      abs_unit_y = fabs(scale_factor_y);

      if ( abs_unit_x != abs_unit_y ) {

        if ( abs_unit_x < abs_unit_y )  {
          HPGL2Real required_new_units;
          HPGL2Real new_scale;

          /* Need to work out where P1 ( and implicitly P2 ) coincide with
           * user units in terms of the new scale applied to Y axis. To do
           * this, we need to know how many extra user units we must add to
           * cover the Y component of the distance P1P2, deriving a Ymin' and
           * Ymax' value that can be used to derive the scaled ctm. These
           * addition user units are added one either side of the Ymin, Ymax
           * range, distributed according to the left parameter ( which
           * determines percentage of the extra units added to the "bottom"
           * of the range.)
           * By experiment we see that the notion of bottom is always
           * associated to the P1 point, even if P1y > P2y or Ymin > Ymax
           * ( i.e. the image is flipped ). As P1 scale point is always
           * associated with the Ymin point in user space, the adjustment
           * is always carried out wrt Ymin.
           *
           * The addition of the extra user units is to expand on the
           * Ymin,Ymax range, therefore the operation performed depends
           * on whether Ymin > Ymax.
           */

          required_new_units = ( fabs(scale_point_yd) / abs_unit_x )
                                - fabs(user_yd);

          /* preserve original sign of scale in this axis */
          new_scale = scale_factor_y < 0.0 ? -abs_unit_x : abs_unit_x ;

          HQASSERT( ( scale_factor_y < 0.0 && new_scale < 0.0 )
                    || ( scale_factor_y >= 0.0 && new_scale >= 0.0),
                    "Mismatched scales in y axis");

          required_new_units = (bottom / 100.0) * required_new_units;
          if ( y1 < y2 )
            y1 -= required_new_units;
          else
            y1 += required_new_units;

          scale_factor_y = new_scale;

        } else  {
          HPGL2Real required_new_units;
          HPGL2Real new_scale;

          /* As above, but Xmin changes, and additional user units are
           * distributed through X axis according the left parameter.
           * Again the "left" is associated to the scale point P1.
           */
          required_new_units = ( fabs(scale_point_xd) / abs_unit_y )
                                - fabs(user_xd);

          new_scale = scale_factor_x < 0.0 ? -abs_unit_y : abs_unit_y ;

          HQASSERT( ( scale_factor_x < 0.0 && new_scale < 0.0 )
                    || ( scale_factor_x >= 0.0 && new_scale >= 0.0),
                    "Mismatched scales in y axis");

          required_new_units = (left / 100.0) * required_new_units;
          if ( x1 < x2 )
            x1 -= required_new_units;
          else
            x1 += required_new_units;

          scale_factor_x = new_scale;
        }
      }

      scaled_ctm->matrix[0][0] = scale_factor_x;
      scaled_ctm->matrix[0][1] = 0.0;
      scaled_ctm->matrix[1][0] = 0.0;
      scaled_ctm->matrix[1][1] = scale_factor_y;

      /* now offset for the (iso adjusted) scale point in plotter space. */
      scaled_ctm->matrix[2][0] = config_info->scale_points.p1.x -
                          (x1 * scale_factor_x);
      scaled_ctm->matrix[2][1] = config_info->scale_points.p1.y -
                          (y1 * scale_factor_y);

      MATRIX_SET_OPT_BOTH(scaled_ctm);

      break;
    }

    case HPGL2_SCALE_POINT_FACTOR:
    {
      HPGL2ScalingPointScaleParams *point_scale_params;
      HPGL2Real scaled_origin_x, scaled_origin_y,
                scale_x, scale_y,
                x1, y1;
      OMATRIX *scaled_ctm;

      point_scale_params = &config_info->scaling_parameters.point_scale;

      x1 = point_scale_params->x1;
      y1 = point_scale_params->y1;
      scale_x = point_scale_params->scale_x;
      scale_y = point_scale_params->scale_y;

      scaled_origin_x = config_info->scale_points.p1.x - (x1 * scale_x);
      scaled_origin_y = config_info->scale_points.p1.y - (y1 * scale_y);

      /* build transform from scaled space, through default HPGL2 space.
       * Same as above.
       */
      scaled_ctm = &hpgl2_ctms->picture_frame_scaling_ctm;

      scaled_ctm->matrix[0][0] = scale_x;
      scaled_ctm->matrix[0][1] = 0.0;
      scaled_ctm->matrix[1][0] = 0.0;
      scaled_ctm->matrix[1][1] = scale_y;
      scaled_ctm->matrix[2][0] = scaled_origin_x;
      scaled_ctm->matrix[2][1] = scaled_origin_y;

      MATRIX_SET_OPT_BOTH(scaled_ctm);

      break;

    }

    default:
      HQFAIL("Unknown HPGL2 scale mode");
      break;
  }

  /* cache the inverse of the scaled matrix. */
  matrix_inverse(&hpgl2_ctms->picture_frame_scaling_ctm,
                 &hpgl2_ctms->inverse_picture_frame_scaling_ctm);

  /** \todo matrix_assertions required here. */

  return;
}

/* Calculate how far the picture frame extends in terms of the working
 * HPGL2 plotter coord system. This coord system may be rotated.
 * The height and width of the picture frame are store wrt the PCL world.
 * As the picture frame does not rotate with the HPGL coordiate system, we
 * need to account here for the current rotation to represent the position
 * of the frame in the current HPGL coordinate system.
 */
void get_picture_frame_extent(PCL5Context *pcl5_ctxt, HPGL2Point *extent)
{
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);
  PictureFrameInfo *picture_frame = get_picture_frame_info(pcl5_ctxt);

  HQASSERT(config_info != NULL, "Config_info is NULL");
  HQASSERT(picture_frame != NULL, "picture_frame is NULL");

  switch ( config_info->rotation ) {
    case 0: /* fall through */
    case 180:
      extent->x = (HPGL2Real)picture_frame->default_width
                   * INTERNAL_UNIT_TO_PLOTTER_UNIT;
      extent->y = (HPGL2Real)picture_frame->default_height
                   * INTERNAL_UNIT_TO_PLOTTER_UNIT;
      break;

    case 90: /* fall through */
    case 270:
      extent->x = (HPGL2Real)picture_frame->default_height
                   * INTERNAL_UNIT_TO_PLOTTER_UNIT;
      extent->y = (HPGL2Real)picture_frame->default_width
                   * INTERNAL_UNIT_TO_PLOTTER_UNIT;
      break;

    default:
      HQFAIL("Strange rotation for HPGL2");
      extent->x = (HPGL2Real)picture_frame->default_width
                   * INTERNAL_UNIT_TO_PLOTTER_UNIT;
      extent->y = (HPGL2Real)picture_frame->default_height
                   * INTERNAL_UNIT_TO_PLOTTER_UNIT;
      break;
  }
}

/* set P1 and P2 according to the size of the picture frame and the
 * rotation. The scale points are set to the extent of the picture frame.
 * The scale points are set in the plotter unit space, so reflect rotation
 * of HPGL space.
 */
void hpgl2_default_scale_points(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *configInfo = NULL;
  HPGL2Point origin, extent;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  configInfo = get_hpgl2_config_info(pcl5_ctxt);
  HQASSERT(configInfo != NULL, "HPGL2ConfigInfo is NULL");

  origin.x = (HPGL2Real)0.0;
  origin.y = (HPGL2Real)0.0;
  get_picture_frame_extent(pcl5_ctxt, &extent);
  configInfo->scale_points.p1.x = origin.x;
  configInfo->scale_points.p1.y = origin.y;
  configInfo->scale_points.p2.x = extent.x;
  configInfo->scale_points.p2.y = extent.y;
}

void hpgl2_default_rotation(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  HQASSERT(config_info != NULL, "HPGL2ConfigInfo is NULL");

  config_info->rotation = 0;
}

/**
 * Set the merge control setting to its default; i.e. off. This simply sets the
 * PCL rop to 252.
 */
static void hpgl2_default_merge_control(PCL5Context *pcl5_ctxt)
{
  set_current_rop(pcl5_ctxt, PCL_ROP_TSo);
}

void hpgl2_set_default_config_info(PCL5Context *pcl5_ctxt, Bool initialize)
{
    /* Input window. */
  hpgl2_default_clip_window(pcl5_ctxt);

    /* Scale mode : off by default */
  set_hpgl2_scaling(pcl5_ctxt, FALSE);

  hpgl2_default_merge_control(pcl5_ctxt);

  if ( initialize ) {
    hpgl2_default_rotation(pcl5_ctxt);
    (void)hpgl2_default_scale_points(pcl5_ctxt);
  }
}

void hpgl2_sync_config_info(PCL5Context *pcl5_ctxt, Bool init)
{
  UNUSED_PARAM(Bool, init);

  (void)set_hpgl2_soft_clip(pcl5_ctxt);
}


/* --- HPGL2 operators --- */

Bool hpgl2op_CO(PCL5Context *pcl5_ctxt)
{
  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  return ( hpgl2_scan_comment(pcl5_ctxt) >= 0 );
}

/* The (equivalent of ) the IN operator can be invoked due to a printer reset, and thus
 * it will run when the gstate reflects the PCL world, and not the
 * HPGL2 world. Such an invocation of IN ( and any other operators
 * that IN invokes ) cannot rely on, nor alter, the gstate.
 *
 * HPGL2 state management is a 2 phase process. Changes are accumulated in
 * the various HPGL2 state structures, and then explicity synced to the
 * gstate when the interpreter is in HPGL mode.
 * This sync occurs en-masse when entering HPGL mode, and after an IN or DF
 * operator. This allows PCL ops to side effect the HPGL state.
 * Incremental changes to the state (from HPGL2 operators) are sync'ed
 * immediately.
 */

void hpgl2_sync_state(PCL5Context *pcl5_ctxt, Bool initialize)
{
  /* ctm has to be set before e.g. pen position is established. */
  set_hpgl2_ctm(pcl5_ctxt);

  hpgl2_sync_print_state(get_hpgl2_print_state(pcl5_ctxt), initialize);
  hpgl2_sync_config_info(pcl5_ctxt, initialize);
  hpgl2_sync_linefill_info(pcl5_ctxt, initialize);
  hpgl2_sync_vector_info(get_hpgl2_vector_info(pcl5_ctxt), initialize);
  hpgl2_sync_character_info(get_hpgl2_character_info(pcl5_ctxt), initialize);
  hpgl2_sync_technical_info(get_hpgl2_technical_info(pcl5_ctxt), initialize);
}

/* PCL operators might cause a reset to default values of aspects
 * of the HPGL2 state. */
void hpgl2_DF_or_IN_set_default_state(PCL5Context *pcl5_ctxt, Bool init)
{
  HPGL2ConfigInfo *configInfo = get_hpgl2_config_info(pcl5_ctxt);
  HPGL2Point default_anchor_corner;

  /* Print state: Polygon buffer, carriage return point, pen position */
  hpgl2_set_default_print_state(get_hpgl2_print_state(pcl5_ctxt), init) ;

  /* Line type, line attributes, pen width, pen width units, symbol mode,
   * fill type, screened vectors, raster fills.
   *
   * PCL COMPATIBILITY The HP4700 seems to use the P1 scale point to default
   * the anchor corner (it doesn't appear to matter if P1 > P2).
   */
  default_anchor_corner.x = configInfo->scale_points.p1.x;
  default_anchor_corner.y = configInfo->scale_points.p1.y;

  hpgl2_set_default_linefill_info(get_hpgl2_line_fill_info(pcl5_ctxt), init,
                                  &default_anchor_corner);

  /* Force clear of all raster fill definitions. */
  pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.hpgl2_user, FALSE) ;

  /* plot mode, pen state */
  hpgl2_set_default_vector_info(get_hpgl2_vector_info(pcl5_ctxt), init);

  /* scale points, input window, rotation, merge control */
  hpgl2_set_default_config_info(pcl5_ctxt, init);

  /* label terminator, terminator mode */
  hpgl2_set_default_character_info(get_hpgl2_character_info(pcl5_ctxt), init);

  /* color range */
  hpgl2_set_default_palette_extension_info(
                                  get_hpgl2_palette_extension(pcl5_ctxt),
                                  init);

  /* merge contro and pixel placement */
  hpgl2_set_default_technical_info(get_hpgl2_technical_info(pcl5_ctxt), init);

  /* clear polygon mode */
  (void)hpgl2_force_exit_polygon_mode(pcl5_ctxt);

  return;
}

Bool hpgl2op_DF(PCL5Context *pcl5_ctxt)
{
  uint8 terminator;
  HPGL2PrintState* print_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL" );

  print_state = get_hpgl2_print_state(pcl5_ctxt);
  HQASSERT(print_state != NULL, "print state is NULL");

  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  hpgl2_DF_or_IN_set_default_state(pcl5_ctxt, FALSE);
  hpgl2_sync_state(pcl5_ctxt, FALSE);
  return TRUE ;
}

Bool hpgl2op_IN(PCL5Context *pcl5_ctxt)
{
  uint8 terminator;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL" );

  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  hpgl2_DF_or_IN_set_default_state(pcl5_ctxt, TRUE);
  if ( pcl5_ctxt->pcl5c_enabled )
    create_hpgl2_palette(pcl5_ctxt, 3);
  else {
    hpgl2_set_pen_width(pcl5_ctxt,
                        HPGL2_ALL_PENS, /* linefill_info->selected_pen, */
                        hpgl2_get_default_pen_width(HPGL2_PEN_WIDTH_METRIC));
  }
  hpgl2_sync_state(pcl5_ctxt, TRUE);

  /* Carriage return point updated in the default state setting. */

  return TRUE ;
}

Bool hpgl2op_IP(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *configInfo = NULL;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2Integer p1_x, p1_y, p2_x, p2_y;
  uint8 terminator;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  configInfo = get_hpgl2_config_info(pcl5_ctxt);
  HQASSERT(configInfo != NULL, "HPGL2ConfigInfo is NULL");

  hpgl2_linetype_clear(linefill_info) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {

    hpgl2_default_scale_points(pcl5_ctxt);
    /* if the scale points are altered while scaling, the user space changes. */
    if ( hpgl2_is_scaled(pcl5_ctxt) )
    {
      hpgl2_update_scaled_ctm(pcl5_ctxt);
      handle_user_ctm_change(pcl5_ctxt);
    }
    return TRUE;
  }

  if ( hpgl2_scan_integer(pcl5_ctxt, &p1_x ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_integer(pcl5_ctxt, &p1_y ) <= 0 ) {
    /* syntax error - what to do? */
    return TRUE;
  }


  /* Check for dodgy values. */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {
    /* track mode. */
    HPGL2Real x_diff, y_diff;

    x_diff = configInfo->scale_points.p2.x - configInfo->scale_points.p1.x ;
    y_diff = configInfo->scale_points.p2.y - configInfo->scale_points.p1.y ;
    /* tracking mode - X and Y distances between P1 and P2
     * remain constant. Fractional plotter units might be introduced.
     */

    configInfo->scale_points.p1.x = (HPGL2Real) p1_x ;
    configInfo->scale_points.p1.y = (HPGL2Real) p1_y ;
    configInfo->scale_points.p2.x = (HPGL2Real) p1_x + x_diff ;
    configInfo->scale_points.p2.y = (HPGL2Real) p1_y + y_diff ;
  }
  else {

    if ( hpgl2_scan_separator(pcl5_ctxt) <= 0
        || hpgl2_scan_integer(pcl5_ctxt, &p2_x ) <= 0
        || hpgl2_scan_separator(pcl5_ctxt) <= 0
        || hpgl2_scan_integer(pcl5_ctxt, &p2_y ) <= 0 ) {
     /* syntax error. */
      return TRUE;
    }

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

    if ( hpgl2_is_scaled(pcl5_ctxt) )
    {
      p1_x = (HPGL2Integer) p1_x;
      p2_x = (HPGL2Integer) p2_x;
      p1_y = (HPGL2Integer) p1_y;
      p2_y = (HPGL2Integer) p2_y;

    }
    else
    {
      /* nominal plotter units in the job need to be scaled by plot
       * size scaling. */
      p1_x = (HPGL2Integer) (p1_x * horizontal_scale_factor(pcl5_ctxt));
      p2_x = (HPGL2Integer) ( p2_x * horizontal_scale_factor(pcl5_ctxt));
      p1_y = (HPGL2Integer) ( p1_y * vertical_scale_factor(pcl5_ctxt));
      p2_y = (HPGL2Integer) ( p2_y * vertical_scale_factor(pcl5_ctxt));
    }

    /* Cannot specify coincident scaling points. Spec requires P2 be incremented
     * if the points are coincident. To avoid problems with scaling with small
     * numbers, we wont insist on absolute equality.
     */
    if ( COORD_EQUAL_WITH_EPSILON(p1_x, p2_x) )
      ++p2_x;

    if ( COORD_EQUAL_WITH_EPSILON(p1_y, p2_y) )
      ++p2_y;

    configInfo->scale_points.p1.x =  p1_x ;
    configInfo->scale_points.p1.y = (HPGL2Real) p1_y ;
    configInfo->scale_points.p2.x = (HPGL2Real) p2_x ;
    configInfo->scale_points.p2.y = (HPGL2Real) p2_y ;

  }

  /* if the IP points are altered while scaling,
   * the user space changes.
   */
  if ( hpgl2_is_scaled(pcl5_ctxt) )
  {
    hpgl2_update_scaled_ctm(pcl5_ctxt);
    handle_user_ctm_change(pcl5_ctxt);
  }

  return TRUE ;
}

Bool hpgl2op_IR(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *configInfo = NULL;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  PictureFrameInfo *picture_frame_info = NULL;
  HPGL2Real x1_percent, x2_percent, y1_percent, y2_percent;
  HPGL2Real p1_x, p1_y, p2_x, p2_y;
  uint8 terminator;
  HPGL2Point extent;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  configInfo = get_hpgl2_config_info(pcl5_ctxt);
  HQASSERT(configInfo != NULL, "HPGL2ConfigInfo is NULL");

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(picture_frame_info != NULL, "Picture frame is NULL");

  hpgl2_linetype_clear(linefill_info) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {

    hpgl2_default_scale_points(pcl5_ctxt);
    /* if the scale points are altered while scaling, the user space changes. */
    if ( hpgl2_is_scaled(pcl5_ctxt) )
    {
      hpgl2_update_scaled_ctm(pcl5_ctxt);
      handle_user_ctm_change(pcl5_ctxt);
    }
    return TRUE;
  }

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &x1_percent ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_clamped_real(pcl5_ctxt, &y1_percent ) <= 0 ) {
    return TRUE;
  }

  x1_percent = clamp_hpgl2real(x1_percent, (HPGL2Real)0.0, (HPGL2Real)100.0);
  y1_percent = clamp_hpgl2real(y1_percent, (HPGL2Real)0.0, (HPGL2Real)100.0);

  x1_percent /= (HPGL2Real)100.0;
  y1_percent /= (HPGL2Real)100.0;


  /* scaling points set as percentage of the picture frame sizes.
   * picture_frame_extent is reported in plotter units.
   */
  get_picture_frame_extent(pcl5_ctxt, &extent);
  p1_x = extent.x * x1_percent;
  p1_y = extent.y * y1_percent;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {
    /* track mode. */
    HPGL2Real x_diff, y_diff;

    x_diff = configInfo->scale_points.p2.x - configInfo->scale_points.p1.x ;
    y_diff = configInfo->scale_points.p2.y - configInfo->scale_points.p1.y ;
    /* tracking mode - X and Y distances between P1 and P2
     * remain constant. Fractional plotter units might be introduced.
     */

    configInfo->scale_points.p1.x =  p1_x ;
    configInfo->scale_points.p1.y =  p1_y ;
    configInfo->scale_points.p2.x =  p1_x + x_diff ;
    configInfo->scale_points.p2.y =  p1_y + y_diff ;
  }
  else {

    if ( hpgl2_scan_separator(pcl5_ctxt) <= 0
        || hpgl2_scan_clamped_real(pcl5_ctxt, &x2_percent ) <= 0
        || hpgl2_scan_separator(pcl5_ctxt) <= 0
        || hpgl2_scan_clamped_real(pcl5_ctxt, &y2_percent ) <= 0 ) {
      /* syntax error. */
      return TRUE;
    }
    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
    x2_percent = clamp_hpgl2real(x2_percent, (HPGL2Real)0.0, (HPGL2Real)100.0);
    y2_percent = clamp_hpgl2real(y2_percent, (HPGL2Real)0.0, (HPGL2Real)100.0);

    x2_percent /= (HPGL2Real)100.0;
    y2_percent /= (HPGL2Real)100.0;

    p2_x = extent.x * x2_percent;
    p2_y = extent.y * y2_percent;

    /* To avoid problems with scaling with coordinates very close together
     * we wont insist on absolute equality. here */

    if ( COORD_EQUAL_WITH_EPSILON(p2_x, p1_x) )
      p2_x++;

    if ( COORD_EQUAL_WITH_EPSILON(p2_y, p1_y) )
      p2_y++;

    configInfo->scale_points.p1.x = (HPGL2Real) p1_x ;
    configInfo->scale_points.p1.y = (HPGL2Real) p1_y ;
    configInfo->scale_points.p2.x = (HPGL2Real) p2_x ;
    configInfo->scale_points.p2.y = (HPGL2Real) p2_y ;

  }

  /* if the IP points are altered while scaling,
   * the user space changes.
   */
  if ( configInfo->scale_enabled ) {
    hpgl2_update_scaled_ctm(pcl5_ctxt);
    handle_user_ctm_change(pcl5_ctxt);
  }

  return TRUE ;
}

Bool hpgl2op_IW(PCL5Context *pcl5_ctxt)
{
  uint8 terminator;
  HPGL2Point  ll, ur;
  HPGL2ConfigInfo *config_info;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  hpgl2_linetype_clear(linefill_info) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Default the clip window. */

    hpgl2_default_clip_window(pcl5_ctxt);
    set_hpgl2_soft_clip(pcl5_ctxt);

    return TRUE;
  }

  if ( hpgl2_scan_point(pcl5_ctxt, &ll) > 0
      && hpgl2_scan_separator(pcl5_ctxt) > 0
      && hpgl2_scan_point(pcl5_ctxt, &ur) > 0
      && hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {

    /* Handling of clip windows depends on whether the clip window
     * was defined in terms of plotter or user units. Experimentation
     * suggests that if defined in terms of user units, the window
     * is re-interpreted on each IP/IR/SC that follows i.e. the window
     * definition retains its specified values and those are applied to
     * the new user coordinate space.
     * However a SC; command will fix the clip window in its position.
     */

    config_info = get_hpgl2_config_info(pcl5_ctxt);

    config_info->window.ll.x = ll.x;
    config_info->window.ll.y = ll.y;
    config_info->window.ur.x = ur.x;
    config_info->window.ur.y = ur.y;

    /* A clip window defined when using scaling can be moved as user
     * space is altered by IP/IR/SC.
     */

    if ( config_info->scale_enabled ) {
      /* Soft clip must be defined in  user units, and these will be
       * reinterpreted as user space changes.
       */
      config_info->window.soft_clip_type = HPGL2_SCALABLE_SOFT_CLIP;
    }
    else
      config_info->window.soft_clip_type = HPGL2_FIXED_SOFT_CLIP;

    set_hpgl2_soft_clip(pcl5_ctxt);
  }

  return TRUE ;
}

Bool hpgl2op_RO(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);
  HPGL2Integer rotation = 0;
  uint8 terminator;
  HPGL2Ctms *ctms = get_hpgl2_ctms(pcl5_ctxt);
  OMATRIX work_matrix;
  HPGL2Point out;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &rotation) > 0 ) {

    if ( rotation != 0
         && rotation != 90
         && rotation != 180
         && rotation != 270 ) {
      (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator) ;
      return TRUE; /* illegal parameter. */
    }
  }

  /* According to the ref printer (HP4250), the pattern anchor corner
   * does not rotate with the picture frame, but remains in the same
   * physical location. The calculation below is clunky, but we have
   * not got the rotation as a separate plotter unit to plotter unit
   * transform of the plotter space.
   */
  /** \todo Rotations might be best held separatley from the picture
   * frame to PCL internal unit space transform. Possibly we need
   * some sort of event mechanism to handle dependencies on such changes.
   */

  /* get absolute position of the anchor point in terms of internal units
   * including rotations. */
  work_matrix = ctms->working_picture_frame_ctm;
  MATRIX_TRANSFORM_XY( linefill_info->anchor_corner.x,
                       linefill_info->anchor_corner.y,
                       out.x,
                       out.y,
                       &work_matrix );

  config_info->rotation = rotation;

  /* update CTMs for the plotter coord space.  Scale points retain their
   * coordinates. Pen position is left in same physical position. */
  set_hpgl2_ctm(pcl5_ctxt);

  /* Locate the old anchor point in terms of the new plotter coord space. */
  work_matrix = ctms->inverse_working_picture_frame_ctm;
  MATRIX_TRANSFORM_XY( out.x,
                       out.y,
                       linefill_info->anchor_corner.x,
                       linefill_info->anchor_corner.y,
                       &work_matrix );

  /* reset the softclip window to account for changes in rotation. */
  set_hpgl2_soft_clip(pcl5_ctxt);

  /* Update the carriage return point to the current pen location. */
  (void)gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) ;

  hpgl2_linetype_clear(linefill_info) ;

  return TRUE ;
}

Bool hpgl2op_SC(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = NULL;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  PictureFrameInfo *picture_frame_info = NULL;
  HPGL2Real x1, y1, x2, y2,
            left = (HPGL2Real)50.0, bottom = (HPGL2Real)50.0 ;
  HPGL2Integer mode = 0; /* default is non-iso scaling. */
  uint8 terminator;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  HQASSERT(config_info != NULL, "HPGL2ConfigInfo is NULL");

  hpgl2_linetype_clear(linefill_info) ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* SC;
     * default to plotter unit scaling
     * Don't need to re-do the clip. Gstate holds clip in terms of
     * plotter units.
     * Some HPGL state parameters must be fixed to their plotter
     * unit equivalents.
     * - input window.
     * - hatch fill line spacing.
     */

    if ( ! fix_soft_clip(pcl5_ctxt)
        || !fix_all_hatch_spacing(pcl5_ctxt)
       )
      return FALSE;
    set_hpgl2_scaling(pcl5_ctxt, FALSE);
    return TRUE;
  }

  /* x1, y2 will represent scale factors for point-scale mode. */
  if ( hpgl2_scan_real(pcl5_ctxt, &x1 ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &x2 ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &y1 ) <= 0
      || hpgl2_scan_separator(pcl5_ctxt) <= 0
      || hpgl2_scan_real(pcl5_ctxt, &y2 ) <= 0 ) {
    /* syntax error */
    return TRUE;
  }

  if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
    if (hpgl2_scan_integer(pcl5_ctxt, &mode) > 0 ) {
      mode = clamp_hpgl2int(mode, 0, 2);
      if ( mode == HPGL2_SCALE_ISOTROPIC ) {
        /* isotropic mode has optional pair of percentages. */
        if ( hpgl2_scan_separator(pcl5_ctxt) > 0 ) {
          if ( hpgl2_scan_real(pcl5_ctxt, &left) <= 0
               || hpgl2_scan_separator(pcl5_ctxt) <= 0
               || hpgl2_scan_real(pcl5_ctxt, &bottom) <= 0 )
            /* both percentages must be specified. */
            return TRUE;
          else {

            left = clamp_hpgl2real(left, 0.0, 100.0);
            bottom = clamp_hpgl2real(bottom, 0.0, 100.0);
          }
        }
      }
      else {
        /* non-isotropic mode. */
      }
    }
    else {
      /* syntax error - dodgy separator? */
      return TRUE;
    }
  }

  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  /* derive the scaling CTM. */
  picture_frame_info = get_picture_frame_info(pcl5_ctxt);

  if ( mode == HPGL2_SCALE_ANISOTROPIC ) {
    HPGL2ScalingAnisoParams *aniso_params = NULL;

    /* not allowed to have Xmax == Xmin, nor Ymax == Ymin */
    if ( VAL_EQUAL_WITH_EPSILON(x1,x2)
        || VAL_EQUAL_WITH_EPSILON(y1, y2))
      return TRUE;

    /* record the scaling parameters as part of config state, as they
     * might need to be reapplied.
     */
    config_info->scale_mode = HPGL2_SCALE_ANISOTROPIC;
    aniso_params = &config_info->scaling_parameters.anisotropic;

    aniso_params->x1 = x1;
    aniso_params->x2 = x2;
    aniso_params->y1 = y1;
    aniso_params->y2 = y2;

    hpgl2_update_scaled_ctm(pcl5_ctxt);

  }
  else if ( mode == HPGL2_SCALE_ISOTROPIC ) {
    HPGL2ScalingIsoParams *iso_params = NULL;

    /* not allowed to have Xmax == Xmin, nor Ymax == Ymin */
    if ( VAL_EQUAL_WITH_EPSILON(x1,x2)
        || VAL_EQUAL_WITH_EPSILON(y1, y2))
      return TRUE;

    /* record the scaling parameters as part of config state. */
    config_info->scale_mode = HPGL2_SCALE_ISOTROPIC;
    iso_params = &config_info->scaling_parameters.isotropic;

    iso_params->x1 = x1;
    iso_params->x2 = x2;
    iso_params->y1 = y1;
    iso_params->y2 = y2;
    iso_params->left = left;
    iso_params->bottom = bottom;

    hpgl2_update_scaled_ctm(pcl5_ctxt);

  }
  else { /* mode is point scale. */
    HPGL2ScalingPointScaleParams *point_scale_params = NULL;

    /* record the scaling parameters. */
    config_info->scale_mode = HPGL2_SCALE_POINT_FACTOR;
    point_scale_params = &config_info->scaling_parameters.point_scale;
    point_scale_params->x1 = x1;
    point_scale_params->y1 = y1;
    point_scale_params->scale_x = x2;
    point_scale_params->scale_y = y2;

    /* Bail on non-sensible scale factors. */
    if ( VAL_EQUAL_WITH_EPSILON(point_scale_params->scale_x,0.0)
        || VAL_EQUAL_WITH_EPSILON(point_scale_params->scale_y,0.0))
      return TRUE;

    hpgl2_update_scaled_ctm(pcl5_ctxt);
  }

  set_hpgl2_scaling(pcl5_ctxt, TRUE);

  /* update gstate to account for the scaling. That may
   * mean updating the clip region, which can change
   * with scaling. The clip region in the gstate is in
   * terms of plotter units, and scaling changes the
   * relation of plotter units and user units.
   */
  return handle_user_ctm_change(pcl5_ctxt);
}

HPGL2Real horizontal_scale_factor(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);
  PictureFrameInfo *picture_frame = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(!config_info->scale_enabled
          || config_info->scale_mode == HPGL2_SCALE_POINT_FACTOR,
      "Plot size scaling factor does not apply in (an)isotropic scale mode");

  /* The notion of "horizontal" applies to the page description not the
   * output so depends on the rotation of the HPGL coord system.
   */
  if ( config_info->rotation == 0 || config_info->rotation == 180 )
    return  picture_frame->plot_horizontal_size > 0 ?
            picture_frame->default_width / picture_frame->plot_horizontal_size :
            1 ;
  else
    return  picture_frame->plot_vertical_size > 0 ?
            picture_frame->default_height / picture_frame->plot_vertical_size :
            1 ;
}

HPGL2Real vertical_scale_factor(PCL5Context *pcl5_ctxt)
{
  HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt);
  PictureFrameInfo *picture_frame = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(!config_info->scale_enabled
          || config_info->scale_mode == HPGL2_SCALE_POINT_FACTOR,
      "Plot size scaling factor does not apply in (an)isotropic scale mode");

  /* The notion of "vertical" applies to the page description not the
   * output so depends on the rotation of the HPGL coord system.
   */
  if ( config_info->rotation == 0 || config_info->rotation == 180 )
    return  picture_frame->plot_vertical_size > 0 ?
            picture_frame->default_height / picture_frame->plot_vertical_size :
            1 ;
  else
    return  picture_frame->plot_horizontal_size > 0 ?
            picture_frame->default_width / picture_frame->plot_horizontal_size :
            1 ;
}

/* ============================================================================
* Log stripped */
