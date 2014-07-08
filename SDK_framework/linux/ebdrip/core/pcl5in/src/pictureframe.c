/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pictureframe.c(EBDSDK_P.1) $
 * $Id: src:pictureframe.c,v 1.53.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "picture frame" category.
 *
 * Picture Frame
 *
 * Picture Frame Horizontal Size   ESC * c # X
 * Picture Frame Vertical Size     ESC * c # Y
 * Set Picture Frame Anchor Point  ESC * c 0 T
 * HP-GL/2 Plot Horizontal Size    ESC * c # K
 * HP-GL/2 Plot Vertical Size      ESC * c # L
 * Enter HP-GL/2 Mode              ESC % # B
 * Enter PCL Mode                  ESC % # A
 */

/* The picture frame defines the base HPGL2 coordinate system. This is
 * specified in terms of plotter units. Upon entering the HPGL2 mode,
 * gstate CTM is set up as this base HPGL2 coordinate system i.e origin
 * at the  lower left of the picture frame, y axis increasing up the page,
 * and the units plotter unit size.
 *
 * The picture frame is defined with respect to the default orientation of
 * the PCL page. Print direction is not relevant.
 *
 * The HPGL2 processing mainly works in terms of plotter units.
 * Drawing of HPGL2 vectors, clipping and positioning of the HPGL2 pen etc. are
 * always done in this base coord system. There is no change to the
 * gstate CTM when HPGL user coord space (i.e. scaled coord space) is
 * used. Neither does rotation change the gstate CTM.
 * This ensures that e.g. line caps are not distorted by non-isotropice
 * user unit scaling. It also simplfies the handling of soft clip window.
 *
 * The HPGL2 MPE maintains various CTMS:
 * picture_frame_ctm maps the plotter unit based HPGL2 coord system onto the
 * PCL coordinate sytem that represents the page in its default orientation.
 * picture_frame_ctm is concatenated with the CTM defining the PCL page
 * in its default orientation to define the drawing environment for HPGL2.
 * The result of this concatenation is installed as the gstate CTM when
 * interpreting HPGL2.
 *
 * picture_frame_scaling_ctm : represents the transformation from HPGL2 user
 * coordinates to HPGL2 plotter unit coordinate space. It is applied manually
 * to coordinates in  the HPGL2 interpreter and is not included in the gstate
 * CTM.
 *
 * picture_frame_rotation_ctm : represents rotation of the HPGL2 coord space.
 * As with the scaling ctm, it is applied to HPGL2 coordinates directly and does
 * not affect the gstate CTM.
 *
 * Note the HPGL2 pen position does not account for any PCL print direction
 * specification other than 0.
 */

#include "core.h"
#include "timing.h"
#include "pictureframe.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "hpgl2scan.h"
#include "cursorpos.h"
#include "pagecontrol.h"
#include "hpgl2linefill.h"
#include "hpgl2config.h"
#include "hpgl2vector.h"

#include "fileio.h"
#include "monitor.h"
#include "graphics.h"
#include "gstack.h"
#include "gu_ctm.h"
#include "pathcons.h"
#include "miscops.h" /* run_ps_string, but only temporarily. */
#include "gu_cons.h"
#include "display.h"
#include "swerrors.h"


PictureFrameInfo* get_picture_frame_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  PCL5PrintEnvironment *mpe ;
  PictureFrameInfo *picture_frame_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  mpe = print_state->mpe ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  picture_frame_info = &(mpe->picture_frame) ;

  return picture_frame_info;
}

/* See header for doc. */
void default_picture_frame_info(PictureFrameInfo* self)
{
  SETXY(self->anchor_point, 0, 0);
  self->default_width = 0;
  self->default_height = 0;
  self->plot_horizontal_size = 0;
  self->plot_vertical_size = 0;
}

void default_picture_frame_position(PCL5Context *pcl5_ctxt)
{
  PictureFrameInfo *picture_frame_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);

  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  /** \todo Will have to account for print direction, and the margins of
   * the PCL page, if the anchor point is to be specified in terms of the
   * PCL cursor (the PCL cursor reflects print direction).
   */
  picture_frame_info->anchor_point.x = 0.0;
  picture_frame_info->anchor_point.y = 0.0;

  return;
}

/* set width to in terms of internal units. */
void set_picture_frame_width(PCL5Context *pcl5_ctxt, PCL5Real width)
{
  PictureFrameInfo *picture_frame = NULL;

  HQASSERT(width > 0 , "Bad picture frame width");
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame = get_picture_frame_info(pcl5_ctxt) ;
  HQASSERT(picture_frame != NULL, "Bad picture frame");

  picture_frame->default_width = width;
  return;
}

void set_picture_frame_height(PCL5Context *pcl5_ctxt, PCL5Real height)
{
  PictureFrameInfo *picture_frame = NULL;

  HQASSERT(height > 0 , "Bad picture frame width");
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame = get_picture_frame_info(pcl5_ctxt) ;
  HQASSERT(picture_frame != NULL, "Bad picture frame");

  picture_frame->default_height = height;
  return;
}

void set_default_picture_frame_height(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *curr_page_ctrl;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  curr_page_ctrl = get_page_ctrl_info(pcl5_ctxt) ;

  HQASSERT(curr_page_ctrl != NULL, "curr_page_ctrl is NULL") ;
  HQASSERT(curr_page_ctrl->text_length > 0, "-ve text_length");

  /* The default picture frame height needs to be set depending on
   * the default notions of text length, which is not necessarily the
   * current text length. The current text length depends on the
   * print direction, and the hpgl will need to use the default
   * value from the point of view of orientation ( i.e. print direction 0).
   */
  set_picture_frame_height(pcl5_ctxt, get_default_text_length(pcl5_ctxt) );

  return;
}

void set_default_picture_frame_width(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *curr_page_ctrl;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  curr_page_ctrl = get_page_ctrl_info(pcl5_ctxt) ;

  HQASSERT(curr_page_ctrl != NULL, "curr_page_ctrl is NULL") ;
  HQASSERT(curr_page_ctrl->page_width > 0, "-ve logical page width");

  set_picture_frame_width(pcl5_ctxt, curr_page_ctrl->page_width);

  return;
}

/* This function should only be called if there is a well defined
 * page size and margins in MPE on which to base the picture_frame size.
 */
void default_picture_frame_dimensions(PCL5Context *pcl5_ctxt)
{
  PictureFrameInfo *picture_frame_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);

  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  /* This is assuming that the coord system is translated to the
   * Top Margin, rather than the start of the logical page.
   * Note that the frame size can be set independently from the anchor
   * point.
   */
  /**
   * \todo watch out for margin handling at this point.
   */

  set_default_picture_frame_height(pcl5_ctxt);
  set_default_picture_frame_width(pcl5_ctxt);

  return;
}

void set_hpgl2_default_plot_width(PCL5Context *pcl5_ctxt)
{
  set_hpgl2_plot_width(pcl5_ctxt, (PCL5Real)0.0);
  return;
}

void set_hpgl2_default_plot_height(PCL5Context *pcl5_ctxt)
{
  set_hpgl2_plot_height(pcl5_ctxt, (PCL5Real)0.0);
  return;
}


void default_plot_size(PCL5Context *pcl5_ctxt)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  set_hpgl2_default_plot_height(pcl5_ctxt);
  set_hpgl2_default_plot_width(pcl5_ctxt);

  return;
}

/* width in internal units */
void set_hpgl2_plot_width(PCL5Context *pcl5_ctxt, PCL5Real width)
{
  PictureFrameInfo *picture_frame = NULL;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(width >= (PCL5Real)0.0, "Bad plot width");

  picture_frame = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(picture_frame != NULL, "Bad picture fame");

  picture_frame->plot_horizontal_size = width;
  return;
}

void set_hpgl2_plot_height(PCL5Context *pcl5_ctxt, PCL5Real height)
{
  PictureFrameInfo *picture_frame = NULL;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(height >= (PCL5Real)0.0, "Bad plot height");

  picture_frame = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(picture_frame != NULL, "Bad picture fame");

  picture_frame->plot_vertical_size = height;
  return;
}

/* Convert a PCL cursor position to an HPGL2 pen position.
 * The PCL cursor position must be specified wrt the page
 * default orientation (print direction 0), as the picture
 * frame is specified wrt the default orientation.
 */
void PCL_coords_as_hpgl2_pen(PCL5Context *pcl5_ctxt,
                             CursorPosition *pcl5_cursor,
                             HPGL2Point *hpgl2_point)
{
  HPGL2ConfigInfo *config_info;
  HPGL2Ctms *ctms;

  /* pcl5_cursor must represent cursor position in default orientation. */

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  ctms = get_hpgl2_ctms(pcl5_ctxt);
  MATRIX_TRANSFORM_XY(pcl5_cursor->x, pcl5_cursor->y,
                      hpgl2_point->x, hpgl2_point->y,
                      &ctms->inverse_working_picture_frame_ctm);

  return;
}

/* Convert an HPGL2 pen position to an equivalent PCL cursor position
 * wrt the picture frame. The PCL cursor position is specified wrt the
 * default orientation (print direction 0), as the picture frame is
 * defined wrt the default orientation.
 */
void hpgl2_coords_as_PCL_cursor(PCL5Context *pcl5_ctxt,
                                HPGL2Point *hpgl2_point,
                                CursorPosition *pcl5_cursor)
{
  HPGL2ConfigInfo *config_info;
  HPGL2Ctms *ctms;

  /* pcl5_cursor will represent cursor position in default orientation. */

  config_info = get_hpgl2_config_info(pcl5_ctxt);
  ctms = get_hpgl2_ctms(pcl5_ctxt);
  MATRIX_TRANSFORM_XY(hpgl2_point->x, hpgl2_point->y,
                      pcl5_cursor->x, pcl5_cursor->y,
                      &ctms->working_picture_frame_ctm);

  return;
}

void handle_ctm_change_picture_frame(PCL5Context *pcl5_ctxt,
                                     OMATRIX*old_orientation_ctm,
                                     OMATRIX*new_orientation_ctm)
{
  PictureFrameInfo *pictureframe_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "NULL PCL5Context");

  pictureframe_info = get_picture_frame_info(pcl5_ctxt);
  transform_cursor(&pictureframe_info->anchor_point,
                   old_orientation_ctm,
                   new_orientation_ctm);
}

/*------------------- PCL operator callbacks below. ----------------------*/

Bool pcl5op_star_c_X(PCL5Context *pcl5_ctxt, Bool explcit_positive, PCL5Numeric value)
{
  FILELIST *flptr ;
  PictureFrameInfo *picture_frame_info ;

  UNUSED_PARAM(Bool, explcit_positive) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  /* record height in PCL units. */

  if ( value.real < 0.0 )
    return TRUE;

  if ( value.real == 0.0 )
    set_default_picture_frame_width(pcl5_ctxt);
  else
   set_picture_frame_width(pcl5_ctxt, value.real * 10); /* decipoints to PCL units */

  hpgl2_handle_PCL_picture_frame_redefine(pcl5_ctxt);

  return TRUE ;
}

Bool pcl5op_star_c_Y(PCL5Context *pcl5_ctxt, Bool explcit_positive, PCL5Numeric value)
{
  FILELIST *flptr ;
  PictureFrameInfo *picture_frame_info ;

  UNUSED_PARAM(Bool, explcit_positive) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  /* record height in PCL units. */

  if ( value.real < 0.0 )
    return TRUE;

  if ( value.real == 0.0 )
    set_default_picture_frame_height(pcl5_ctxt);
  else
   set_picture_frame_height(pcl5_ctxt, value.real * 10); /* decipoints to PCL units */

  hpgl2_handle_PCL_picture_frame_redefine(pcl5_ctxt);

  return TRUE ;
}

Bool pcl5op_star_c_T(PCL5Context *pcl5_ctxt, Bool explcit_positive, PCL5Numeric value)
{
  FILELIST *flptr ;
  PictureFrameInfo *picture_frame = NULL;

  UNUSED_PARAM(Bool, explcit_positive) ;


  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  /* ignore command on a non-zero paramter. */
  if (value.real != 0.0)
    return TRUE;

  /* Copy the cursor position into the picture frame. */
  picture_frame = get_picture_frame_info(pcl5_ctxt);
  HQASSERT(picture_frame != NULL, "picture frame is NULL");

  /* the PCL cursor will reflect the print direction (when implemented),
   * so will require adjustment to print direction 0 position before
   * being uses as anchor point.
   */
  get_cursor_position(pcl5_ctxt,
                      &picture_frame->anchor_point.x,
                      &picture_frame->anchor_point.y);

  hgpl2_handle_PCL_picture_frame_anchor_change(pcl5_ctxt);
  return TRUE ;
}

Bool pcl5op_star_c_K(PCL5Context *pcl5_ctxt, Bool explcit_positive, PCL5Numeric value)
{
  PictureFrameInfo *picture_frame_info ;

  UNUSED_PARAM(Bool, explcit_positive) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  /* plot height parameter in inches, stored in PCL internal units. */

  /* ignore negative values. */
  if ( value.real < 0.0 )
    return TRUE;

  if ( value.real == 0.0 )
    set_hpgl2_default_plot_width(pcl5_ctxt);
  else
    set_hpgl2_plot_width(pcl5_ctxt, value.real * 7200 );

  return TRUE ;
}

Bool pcl5op_star_c_L(PCL5Context *pcl5_ctxt, Bool explcit_positive, PCL5Numeric value)
{
  PictureFrameInfo *picture_frame_info ;

  UNUSED_PARAM(Bool, explcit_positive) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  /* plot height parameter in inches, stored in PCL internal units. */

  /* ignore negative values. */
  if ( value.real < 0.0 )
    return TRUE;

  if ( value.real == 0.0 )
    set_hpgl2_default_plot_height(pcl5_ctxt);
  else
    set_hpgl2_plot_height(pcl5_ctxt, value.real * 7200 );

  return TRUE ;
}

/* Calculate the CTM defining the default HPGL2 plotter coord space as
 * defined by the picture frame onto the PCL page in its default orientation.
 */
Bool calculate_hpgl2_ctm(PCL5Context *pcl5_ctxt)
{
  PictureFrameInfo *picture_frame_info = NULL;
  OMATRIX *ctm = NULL;
  HPGL2Ctms *hpgl_ctms = NULL;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  picture_frame_info = get_picture_frame_info(pcl5_ctxt);
  hpgl_ctms = & ( pcl5_ctxt->print_state->mpe->hpgl2_ctms);

  HQASSERT( picture_frame_info != NULL, "picture_frame_info is NULL");

  ctm = &hpgl_ctms->default_picture_frame_ctm;

  ctm->matrix[0][0] = GL2_PLOTTER_UNITS_TO_INTERNAL;
  ctm->matrix[0][1] = 0.0;
  ctm->matrix[1][0] = 0.0;
  ctm->matrix[1][1] = -1.0 * GL2_PLOTTER_UNITS_TO_INTERNAL;
  ctm->matrix[2][0] = picture_frame_info->anchor_point.x;
  ctm->matrix[2][1] = picture_frame_info->anchor_point.y
                      + picture_frame_info->default_height;

  MATRIX_SET_OPT_BOTH(ctm);

  matrix_inverse(ctm, &hpgl_ctms->inverse_default_picture_frame_ctm);

  return TRUE;
}

/* Set up the gstate CTM for drawing HPGL2. Any applicable rotation is applied
 * at this point.
 * calculate_hpgl2_ctm should have been called prior to this function being
 * executed. set_hpgl2_ctm can be invoked after each rotation change.
 * The rotated ctm needs to be retain because it may be needed outside the HPGL2
 * execution context, e.g. in dealing with PCL %A operator.
 *
 * Note the HPGL CTM uses HPGL plotter units. If plot size scaling
 * is applied to a job, then the any nominal plotter units in the job
 * description must be transformed to actual plotter units for the interpreter
 * before being used in drawing operations, or for configuring the HPGL
 * interpreter.
 *
 */
Bool set_hpgl2_ctm(PCL5Context *pcl5_ctxt)
{
  PCL5Ctms *pcl5_ctms;
  HPGL2Ctms *hpgl_ctms;
  HPGL2ConfigInfo *config_info;
  PictureFrameInfo *picture_frame;
  OMATRIX *working = NULL;
  OMATRIX def_matrix;
  OMATRIX tmp;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* The HPGL2 gstate should be the current one, with the PCL5
   * gstate underneath.
   * todo Check whether a call from set_hpgl2_scaling could break
   * this.  (If so fix set_hpgl2_scaling or its callers).
   */
  HQASSERT(gstateptr->next->gType == GST_PCL5,
           "Unexpected gstate type when setting HPGL CTM");

  pcl5_ctms = get_pcl5_ctms(pcl5_ctxt);
  hpgl_ctms = get_hpgl2_ctms(pcl5_ctxt);
  config_info = get_hpgl2_config_info(pcl5_ctxt);
  picture_frame = get_picture_frame_info(pcl5_ctxt);
  working = &hpgl_ctms->working_picture_frame_ctm;
  def_matrix = hpgl_ctms->default_picture_frame_ctm;

  /* calculate rotations and adjust the gstate ctm accordingly.
   * rotations are wrt default orientation of HPGL2 coords.
   * CTM is mapping to the default HPGL plotter unit space, from the
   * rotated space. Have to account for fact that the picture frame
   * and PCL space have different orientations.
   */

  if ( config_info->rotation == 0 )
    *working = def_matrix;
  else {
    tmp = identity_matrix;

    /* do the rotate, then the translate. */
    switch ( config_info->rotation ) {

      case 90:
      tmp.matrix[0][0] = 0;
      tmp.matrix[0][1] = 1;
      tmp.matrix[1][0] = -1;
      tmp.matrix[1][1] = 0;
      break;

    case 180:
      tmp.matrix[0][0] = -1;
      tmp.matrix[0][1] = 0;
      tmp.matrix[1][0] = 0;
      tmp.matrix[1][1] = -1;
      break;

    case 270:
      tmp.matrix[0][0] = 0;
      tmp.matrix[0][1] = -1;
      tmp.matrix[1][0] = 1;
      tmp.matrix[1][1] = 0;
      break;

    default:
      HQFAIL("Bad HPGL2 rotation");
      break;
    }

    MATRIX_SET_OPT_BOTH(&tmp);
    matrix_mult(&tmp, &def_matrix, working);

    switch ( config_info->rotation ) {
      case 90:
        working->matrix[2][0] += picture_frame->default_width;
        break;
      case 180:
        working->matrix[2][0] += picture_frame->default_width;
        working->matrix[2][1] -= picture_frame->default_height;
        break;

      case 270:
        working->matrix[2][1] -= picture_frame->default_height;
        break;

      default: /* treat as 0 */
        HQFAIL("Bad HPGL2 rotation");
        break;
    }
  }

  MATRIX_SET_OPT_BOTH(working);
  matrix_inverse(working, &hpgl_ctms->inverse_working_picture_frame_ctm);
  matrix_mult(working, ctm_orientation(pcl5_ctms), &tmp);
  pcl_setctm(&tmp, FALSE);

  /* changes to the gstate CTM can affect the linewidth. Ensure that the
   * gstate reflects what is specified in the MPE.
   */
  /**
   * \todo : possibly we should sync more of HPGL2 state? Maybe sync all gstate
   * and HPGL2 state?
   */
  hpgl2_sync_linewidth(pcl5_ctxt);

  return TRUE;
}

/* Transform the passed cursor from printdirection zero to the current
 * printdirection, such that its physical position on the page remains the same.
 */
static void transform_orient_to_printdir(PCL5Context *pcl5_ctxt,
                                         CursorPosition *cursor)
{
  PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt) ;

  HQASSERT(cursor != NULL, "cursor is NULL" ) ;
  transform_cursor(cursor, ctm_orientation(ctms), ctm_current(ctms)) ;
}

/* Transform the passed cursor from the current printdirection to printdirection
 * zero, such that its physical position on the page remains the same.
 */
static void transform_printdir_to_orient(PCL5Context *pcl5_ctxt,
                                         CursorPosition *cursor)
{
  PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt) ;

  HQASSERT(cursor != NULL, "cursor is NULL" ) ;
  transform_cursor(cursor, ctm_current(ctms), ctm_orientation(ctms)) ;
}

static Bool hpgl2_sync_required = TRUE ;

void set_hpgl2_sync_required(void)
{
  hpgl2_sync_required = TRUE ;
}

/**
 * Enter HP-GL/2 mode.
 */
Bool pcl5op_percent_B(PCL5Context *pcl5_ctxt,
                      Bool explcit_positive, PCL5Numeric value)
{
  int32 res ;
  /* PCL5PrintEnvironment *mpe; */
  HPGL2TechnicalInfo *tech_info = get_hpgl2_technical_info(pcl5_ctxt);
  PrintModelInfo* print_info = get_print_model(pcl5_ctxt);
  HPGL2LineFillInfo* line_info = get_hpgl2_line_fill_info(pcl5_ctxt);

  UNUSED_PARAM(Bool, explcit_positive) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* We do this lazily now */
  if (! pcl5_flush_text_run(pcl5_ctxt, 1))
    return FALSE ;

  gc_safe_in_this_operator(); /* No PS objs here. */

  if (! pcl5_recording_a_macro) {
    Bool sync = TRUE ;

    /* Try this before the gexch for safety */
    if (! finishaddchardisplay(pcl5_ctxt->corecontext->page, 1))
      return FALSE ;
    if ( !gs_gexch(GST_HPGL, GST_PCL5)) {
      /* Create a new HPGL gstate (current) making the old one PCL5. */
      if (!gs_gpush(GST_PCL5))
        return FALSE ;
    } else {
      /* Swapped previous HPGL gstate back in, so nothing to set */
      sync = FALSE ;
    }

    if (sync) {
      /* PCL clip path needs to be preserved on entry to HPGL2.
       * Call this prior to changing the CTM, as picture frame clip
       * is specified in PCL internal units.
       */
      /* \todo Might PCL5 have changed its
       * clip meanwhile?  If so we should reapply the HPGL one.
       * Also maybe hpgl2_sync_config_info in that case?
       * There are also some PCL5 commands intended to change the size
       * that the picture frame clip should be set to on re-entry
       * to HPGL.  So we should note if one of these has happened
       * and if so set up the clip if not resynching everything.
       */
      if ( !set_hpgl2_picture_frame_clip(pcl5_ctxt) )
        return FALSE;

      /* Set up gstate to draw in HPGL2 coord system. */
      if (! calculate_hpgl2_ctm(pcl5_ctxt) || ! set_hpgl2_ctm(pcl5_ctxt) )
        return FALSE;
    }

    /* setting the picture frame clip will have destroyed any current
     * path. It is in an invariant for the HPGL2 interpreter that the
     * gstate interpreter will have a currentpoint, so the current point
     * must be established on entry to HPGL.
     *
     * Note that to establish the current point, we use  gs_moveto with
     * absolute coords. We do not need to call hpgl2_moveto as we do
     * not want do a relative, or a scaled moveto. The previous pen
     * position is held in aboslute terms, in plotter unints.
     * The PCL cursor position is held in terms of internal units.
     */

    /* HPGL2 CTM has been set up, so can use HPGL2 plotter unit
     * coordinates.
     */
    if ((int32)value.real % 2 == 1 ) {
      /* PCL cursor position becomes the HPGL2 pen position. */
      CursorPosition cursor;
      HPGL2Point hpgl2_pen;
      HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

      get_cursor_position(pcl5_ctxt, &cursor.x, &cursor.y);

      /* Translate the current cursor to coords in terms of
       * default orientation - as the picture frame is defined in
       * terms of default orientation.
       */
      transform_printdir_to_orient(pcl5_ctxt, &cursor);
      PCL_coords_as_hpgl2_pen(pcl5_ctxt, &cursor, &hpgl2_pen);
      print_state->initial_pen_position.x = hpgl2_pen.x;
      print_state->initial_pen_position.y = hpgl2_pen.y;
      /* change of pen position requires carriage return point be
       * updated.
       */
      print_state->Carriage_Return_point.x = hpgl2_pen.x;
      print_state->Carriage_Return_point.y = hpgl2_pen.y;

      /* Initialise the gstate currentpoint to the pen position
       * if we will not be doing it as part of hpgl2_sync_state.
       */
      if (!sync)
        hpgl2_sync_print_state(print_state, TRUE);
    }

    /* PCL pixel placement changes HPGL */
    tech_info->pixel_placement = print_info->pixel_placement;

    /* [65776] When going back into HPGL, if the user space is scaled, then
     * reinterpret the last set AC coords in terms of the current scale - even
     * if the current scale is not the same as was in force when the last AC
     * was done.
     */
    if ( ! hpgl2_redo_AC(pcl5_ctxt) )
      return FALSE;

    if (sync) {
      hpgl2_sync_state(pcl5_ctxt, TRUE);
      hpgl2_sync_required = FALSE;
    }
    else {
      /* The pclGstate is shared between PCL5 and HPGL so must be
       * correctly set up if we will not be synching the rest of
       * the HPGL state.
       * N.B. HPGL seems to start from the PCL5 rop and changes it
       * at various points, so nothing special needed for it here.
       */
      /* todo Revisit question of rops if end up using 2 separate
       * pclGstates.
       */
      /* \todo We may need to call
       * hpgl2_sync_pen_color or similar to correctly do
       * setPclPattern?  But if this is needed may better to
       * have two separate pclGstates.
       */
      hpgl2_sync_transparency(line_info);
      setPclForegroundSource(pcl5_ctxt->corecontext->page,
                             PCL_DL_COLOR_IS_FOREGROUND);
      hpgl2_sync_pixelplacement(tech_info);
    }
  }

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"---- enter HPGL/2 print mode\n") ;
  }
#endif /* DEBUG_BUILD */

  PROBE(SW_TRACE_INTERPRET_HPGL2, pcl5_ctxt, res = hpgl2_execops(pcl5_ctxt));

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"---- enter PCL5 print mode\n") ;
  }
#endif /* DEBUG_BUILD */

  if (! pcl5_recording_a_macro) {

    /* Record the current point ie. the HPGL2 pen position.
     * This may be needed when we re-enter the HPGL2 mode,
     * or for the Esc%A PCL operator.
     * We need to capture the pen position on exit from HPGL2
     * mode and this point is one where we can be sure the current
     * point coincides with pen position.
     */
    /**
     * \todo : this information could be captured at hpgl2_execops
     * but does it matter is we are recording macros?!?
     */
    HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

    gs_currentpoint(&gstateptr->thepath,
                    &print_state->initial_pen_position.x,
                    &print_state->initial_pen_position.y);


    /* Not sure why this is necessary, but it seems to be for J10p3 */
    /* Move this to before the gexch but still
     * do it regardless of the protovar value.
     */
    hpgl2_setgray(0.0);

    /* Try this before the gexch for safety */
    if (! finishaddchardisplay(pcl5_ctxt->corecontext->page, 1))
      return FALSE ;
    /* If we can't swap the gstates back again, something has gone wrong. */
    if (!gs_gexch(GST_PCL5, GST_HPGL))
      return error_handler(UNDEFINED) ;

    /* HPGL might have side effected the pclGstate. Put it back how PCL
     * likes it. */
    reinstate_pcl5_print_model(pcl5_ctxt);

    /* HPGL pixel placement changes PCL */
    set_pixel_placement(print_info, tech_info->pixel_placement);

    /* If we scanned the %A, Esc E or UEL command we need to call the operator now
       and return to PCL. */
    if ( res && pcl5_ctxt->end_of_hpgl2_op ) {
      res = (*pcl5_ctxt->end_of_hpgl2_op)(pcl5_ctxt, FALSE, pcl5_ctxt->end_of_hpgl2_arg) ;
    }
  }

  pcl5_ctxt->end_of_hpgl2_op = NULL ; /* clear the op regardless of 'res' state */

  return res ;
}

/**
 * Enter PCL5 mode.
 * The parameter might required that we update the PCL cursor to
 * reflect the last HPGL2 pen position. Exit from the HPGL2 mode
 * should store the last pen position.
 * Esc%#A - enter PCL mode.
 * # == 1,  PCL cursor position at location prior to entering
            HPGL2 mode.
 * # == 2,  PCL cursor position at current HPGL2 location.
 */
Bool pcl5op_percent_A(PCL5Context *pcl5_ctxt,
                      int32 explicit_sign, PCL5Numeric value)
{
  FILELIST *flptr ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  /* The command is only executed if we're ending HPGL, entering PCL.
     If we're already in PCL mode the command is ignored. */
  if ( !pcl5_ctxt->end_of_hpgl2_op )
    return TRUE ;

  /* The implementation of the HPGL2 parser means that the
   * pen position at end of HPGL2 interpretation was recorded
   * by pcl5op_percent_B.
   */

  if ( (int32)value.real % 2 == 1 ) {
    CursorPosition pcl5_cursor;
    HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

    hpgl2_coords_as_PCL_cursor(pcl5_ctxt,
                               &print_state->initial_pen_position,
                               &pcl5_cursor);
    /** \todo Rounding to nearest plotter unit? Fractional plotter units? */

    /* Place the PCL cursor at the current HPGL2 cursor position,
     * and let PCL know the cursor has been explicitly set.
     */
    transform_orient_to_printdir(pcl5_ctxt, &pcl5_cursor);
    set_cursor_position(pcl5_ctxt, pcl5_cursor.x, pcl5_cursor.y);
    mark_cursor_explicitly_set(pcl5_ctxt) ;
  }
  /* else, use the PCL cursor as it was prior to entering HPGL2. HPGL2
   * does not update the PCL cursor, so nothing to do here.
   */

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"---- enter PCL5 print mode - but already within PCL5 print mode\n") ;
  }
#endif /* DEBUG_BUILD */

  return TRUE ;
}

/* ============================================================================
* Log stripped */
