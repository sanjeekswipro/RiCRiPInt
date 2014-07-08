/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5ctm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Methods for PCL5 current transformation matrix handling.
 */
#include "core.h"
#include "pcl5ctm_private.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "gu_ctm.h"
#include "gstack.h"

/**
 * Initial orientation CTM is result of applying registration offsets
 * to base CTM.
 */
static void init_orientation(PCL5Ctms* self, JobCtrlInfo* job_control,
                             int32 duplex_page_number)
{
  PCL5Real left_reg, top_reg ;

  left_reg = job_control->left_registration ;
  top_reg = job_control->top_registration ;

  /* Reverse offset directions on the back of duplex sheets */
  if (job_control->duplex && (duplex_page_number % 2) == 0) {
    if (job_control->binding == LONG_EDGE)
      left_reg = - left_reg ;
    else
      top_reg = - top_reg ;  /* Short edge binding */
  }

  matrix_translate(&self->base, left_reg, top_reg, &self->orientation) ;
}


static void get_top_margin_for_printdirection_zero(PageCtrlInfo *page_control,
                                                   uint32 *top_margin)
{
  HQASSERT(page_control != NULL, "page_control is NULL") ;
  HQASSERT(top_margin != NULL, "top_margin is NULL" ) ;

  /* Ensure the value gets initialised */
  *top_margin = page_control->top_margin ;

  switch (page_control->print_direction) {
    case 0:
    /* Nothing more to do */
    break ;

  case 90:
    /* Right margin */
    *top_margin = page_control->page_width - page_control->right_margin ;
    break ;

  case 180:
    /* Bottom margin */
    *top_margin = page_control->max_text_length - page_control->text_length ;
    break ;

  case 270:
    /* Left margin */
    *top_margin = page_control->left_margin ;
    break ;

  default:
    HQFAIL("Unknown print direction") ;
    break ;
  }
}

/**
 * Calculate the logical page orientation CTM. Note that the print
 * direction matrix will be out of date once this method has run.
 */
static void ctm_calculate_orientation(PCL5Ctms* self,
                                      PageCtrlInfo* page_control,
                                      JobCtrlInfo* job_control,
                                      int32 duplex_page_number)
{
  PCL5Real x_translate, y_translate ;
  uint32 top_margin ;

  OMATRIX matrix_90 = {{{0, 1}, {-1, 0}, {0, 0}}, MATRIX_OPT_1001} ;
  OMATRIX matrix_270 = {{{0, -1}, {1, 0}, {0, 0}}, MATRIX_OPT_1001} ;

  /* The orientation CTM is the CTM for printdirection zero, and
   * depends on the size of the top margin for printdirection zero.
   * This may correspond to a different margin for the current
   * printdirection.
   */
  get_top_margin_for_printdirection_zero(page_control, &top_margin) ;

  /* Initialise and apply registration offsets. */
  init_orientation(self, job_control, duplex_page_number) ;

  /* N.B. Remaining calculations are relative to the initialised
   * orientation CTM which defines an origin at (x,y) relative to the
   * top left of the physical page, where (x,y) is (left registration
   * offset, top registration offset), with x axis increasing to the
   * right, and y axis increasing down the page.
   */

  switch (page_control->orientation) {
  case 0:
    x_translate = page_control->portrait_offset ;
    y_translate = top_margin ;
    matrix_translate(&self->orientation, x_translate, y_translate,
                     &self->orientation) ;
    break ;

  case 1:
    x_translate = top_margin ;
    y_translate = page_control->physical_page_length -
                  page_control->landscape_offset ;
    matrix_translate(&self->orientation, x_translate, y_translate,
                     &self->orientation) ;
    matrix_mult(&matrix_270, &self->orientation, &self->orientation) ;
    break ;

  case 2:
    x_translate = page_control->physical_page_width -
                  page_control->portrait_offset ;
    y_translate = page_control->physical_page_length - top_margin ;
    matrix_translate(&self->orientation, x_translate, y_translate,
                     &self->orientation) ;
    matrix_scale(&self->orientation, -1, -1, &self->orientation) ;
    break ;

  case 3:
    x_translate = page_control->physical_page_width - top_margin ;
    y_translate = page_control->landscape_offset ;
    matrix_translate(&self->orientation, x_translate, y_translate,
                     &self->orientation) ;
    matrix_mult(&matrix_90, &self->orientation, &self->orientation) ;
    break ;

  default:
    HQFAIL("Unknown orientation") ;
    break ;
  }
}

/**
 * Calculate the print direction CTM; this will first call
 * ctm_calculate_orientation() to ensure that the orientation matrix
 * (upon which the print direction matrix depends) is up-to-date.
 */
static void ctm_calculate_printdirection(PCL5Ctms* self,
                                         PageCtrlInfo* page_control,
                                         JobCtrlInfo* job_control,
                                         int32 duplex_page_number)
{
  PCL5Real x_translate, y_translate ;

  OMATRIX matrix_90 = {{{0, 1}, {-1, 0}, {0, 0}}, MATRIX_OPT_1001} ;
  OMATRIX matrix_270 = {{{0, -1}, {1, 0}, {0, 0}}, MATRIX_OPT_1001} ;

  ctm_calculate_orientation(self, page_control, job_control, duplex_page_number);

  /* Initialise the print direction CTM to the orientation CTM */
  self->print_direction = self->orientation ;

  /* Following are relative to the orientation CTM.  It may be useful
   * to know that the transformations through 0, 90, or 270 degrees
   * would be valid for any start point, so e.g. case 90 below would
   * also apply for a transformation from direction 180 to direction
   * 270.
   *
   * N.B. As the rotations are done first below the translations are
   * in terms of the new axis directions.
   */

  switch (page_control->print_direction) {
  case 0:
    break ;  /* nothing to do */

  case 90:
    x_translate = - (PCL5Real) page_control->right_margin ;
    y_translate = page_control->top_margin ;
    matrix_mult(&matrix_270, &self->print_direction, &self->print_direction) ;
    matrix_translate(&self->print_direction, x_translate, y_translate,
                     &self->print_direction) ;
    break ;

  case 180:
    x_translate = - (PCL5Real) page_control->page_width ;
    y_translate = - (PCL5Real) page_control->text_length ;
    matrix_scale(&self->print_direction, -1, -1, &self->print_direction) ;
    matrix_translate(&self->print_direction, x_translate, y_translate,
                     &self->print_direction) ;
    break ;

  case 270:
    x_translate = - (PCL5Real) page_control->left_margin ;
    y_translate = - (PCL5Real) page_control->max_text_length ;
    matrix_mult(&matrix_90, &self->print_direction, &self->print_direction) ;
    matrix_translate(&self->print_direction, x_translate, y_translate,
                     &self->print_direction) ;
    break ;

  default:
    HQFAIL("Unknown print direction") ;
  }
}


/**
 * Calculate the DPI of the output device.
 */
static void calculate_device_dpi(PCL5Ctms* self)
{
  double x = 7200, y = 0;

  /* Transform one inch to device space */
  MATRIX_TRANSFORM_DXY(x, y, x, y, &self->base);

  /* Round the result. */
  self->device_dpi = (uint32)(sqrt((x * x) + (y * y)) + 0.5);
}

/* See header for doc. */
void default_ctm(PCL5Ctms* self)
{
  self->base = identity_matrix;
  self->orientation = identity_matrix;
  self->print_direction = identity_matrix;
}

/* See header for doc. */
OMATRIX* ctm_current(PCL5Ctms* self)
{
  return &self->print_direction;
}

/* See header for doc. */
OMATRIX* ctm_orientation(PCL5Ctms* self)
{
  return &self->orientation;
}

/* See header for doc. */
OMATRIX* ctm_base(PCL5Ctms* self)
{
  return &self->base;
}


/* Add a half pixel offset to X and Y to compensate for the fact that our
 * snap to grid rules round down, (after adding the SC_PIXEL_ROUND),
 * whereas the reference printer appears to round to nearest pixel.
 */
static
void pcl_ctm_adjust(OMATRIX *out_ctm, OMATRIX *in_ctm)
{
  HQASSERT(in_ctm != NULL, "in_ctm is NULL") ;
  HQASSERT(out_ctm != NULL, "out_ctm is NULL") ;
  MATRIX_COPY(out_ctm, in_ctm);

  out_ctm->matrix[2][0] += 0.5f ;
  out_ctm->matrix[2][1] += 0.5f ;
}


/* Set an adjusted matrix as the CTM to allow for our snap to
 * grid rules being different from those of the reference printer.
 */
Bool pcl_setctm(OMATRIX* ctm, int32 apply_imposition)
{
  OMATRIX current_ps_ctm ;
  Bool status = TRUE ;
  OMATRIX matrix ;

  HQASSERT(ctm != NULL, "ctm is NULL") ;

  current_ps_ctm = thegsPageCTM(*gstateptr) ;
  pcl_ctm_adjust(&matrix, ctm) ;

  if (! MATRIX_EQ(&current_ps_ctm, &matrix))
    status = gs_setctm(&matrix, apply_imposition);

  return status ;
}


/* See header for doc. */
Bool ctm_install(PCL5Ctms *self)
{
  return pcl_setctm(&self->print_direction, FALSE);
}

/* See header for doc. */
uint32 ctm_get_device_dpi(PCL5Ctms *self)
{
  return self->device_dpi;
}

/* See header for doc. */
PCL5Ctms* get_pcl5_ctms(PCL5Context* context)
{
  HQASSERT(context != NULL && context->print_state != NULL &&
           context->print_state->mpe != NULL, "Bad context.");

  return &context->print_state->mpe->pcl5_ctms;
}

/* See header for doc. */
void ctm_set_base(PCL5Context* context, OMATRIX* base)
{
  PCL5Ctms* self = get_pcl5_ctms(context);

  HQASSERT(base != NULL, "Base matrix cannot be null.");

  MATRIX_COPY(&self->base, base);
  ctm_recalculate(context);
}

/* See header for doc. */
void ctm_recalculate(PCL5Context* context)
{
  PCL5Ctms* self = get_pcl5_ctms(context);
  PageCtrlInfo* page_control = get_page_ctrl_info(context);
  JobCtrlInfo* job_control = get_job_ctrl_info(context);

  ctm_calculate_printdirection(self, page_control, job_control,
                               context->print_state->duplex_page_number);
  calculate_device_dpi(self);

  {
    PCL5PrintState* print_state = context->print_state ;
    print_state->font_state.ctm_changed = TRUE ;
  }
}

/* See header for doc. */
IPOINT ctm_transform(PCL5Context* context, double x, double y)
{
  OMATRIX* ctm = ctm_current(get_pcl5_ctms(context));
  double tx, ty;
  IPOINT result;
  OMATRIX matrix;

  /* Get the reference point in device coordinates. */
  pcl_ctm_adjust(&matrix, ctm);
  MATRIX_TRANSFORM_XY(x, y, tx, ty, &matrix);

  SC_C2D_INT(result.x, tx);
  SC_C2D_INT(result.y, ty);

  return result;
}

/**
 * ctm_sanitise_default_matrix is a workaround to remove inaccuracies in the
 * device CTM which build up as a result of the way the CTM is constructed in
 * pagedev.pss and also the /Scaling [ 0.01 0.01 ] entry in oil_psconfig.c.  The
 * construction of the CTM involves casts from doubles to floats for the
 * arguments and also with the use of currentmatrix in pagedev.pss.  This
 * routine recognises that most of the time the matrix values will be resolution
 * / 7200 and therefore the values in the matrix can be snapped accordingly,
 * back to double accuracy.  Without this objects may misalign leading to output
 * problems.
 */
Bool ctm_sanitise_default_matrix(PCL5Context *context)
{
  Bool reset = FALSE;
  PCL5Ctms* ctms = get_pcl5_ctms(context);
  OMATRIX matrix;
  int32 i, j;

  MATRIX_COPY(&matrix, &ctms->base);

  for ( i = 0; i < 2; ++i ) {
    for ( j = 0; j < 2; ++j ) {
      SYSTEMVALUE tmp = matrix.matrix[i][j];

      if ( fabs(tmp) > EPSILON ) {
        SYSTEMVALUE dpi = fabs(tmp) * 7200;

        if ( fabs(ctms->device_dpi - dpi) < 0.0001 ) {
          matrix.matrix[i][j] = ctms->device_dpi / 7200.0;

          if ( tmp < 0 )
            matrix.matrix[i][j] = -matrix.matrix[i][j];

          reset = TRUE;
        }
      }
    }
  }

  if ( reset ) {
    ctm_set_base(context, &matrix);
  }

  return TRUE;
}


/**
 * Calculate media orientation from the base CTM, and store it in the
 * PCL5PrintState.  It is needed to get patterns the right way round,
 * and areafills the right size.  The base CTM must be set up before
 * this is called.
 *
 * N.B.   The media orientation values calculated here are consistent
 *        with those described by the LeadingEdge pagedevice parameter
 *        in Table 6.2 of PS Red Book v3.
 *
 * N.N.B. Although this may not be intuitively obvious, since e.g.
 *        text written in PCL orientation 1, (printdirection 0), has
 *        the same relationship to a media orientation 0, as text
 *        written in PCL orientation 0, (printdirection 0), does to
 *        media orientation 1, it is valid to add up PCL orientation
 *        and media orientation in order to describe the overall
 *        relationship between the PCL orientation and the device
 *        coordinate system.
 */
void set_media_orientation(PCL5Context *context)
{
  PCL5PrintState *print_state = context->print_state;
  PCL5Ctms* ctms = get_pcl5_ctms(context);
  double dx = 7200, dy = 7200;

  HQASSERT(print_state != NULL, "print_state is NULL");

  /* Transform one inch to device space */
  MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, &ctms->base);

  if (dx > 0)
    print_state->media_orientation = (dy > 0) ? 0 : 1;
  else
    print_state->media_orientation = (dy < 0) ? 2 : 3;
}


/**
 * Calculate device dpi and store it in the PCL5PrintState.
 * The base CTM must have been setup before this is called.
 * N.B. The media orientation should be irrelevant.
 */
void set_device_resolution(PCL5Context *context)
{
  PCL5PrintState *print_state = context->print_state;
  PCL5Ctms* ctms = get_pcl5_ctms(context);
  double dx = 7200, dy = 7200;

  /* Transform one inch to device space. */
  MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, &ctms->base);

  /* Round and store the result */
  print_state->hw_resolution[0] = (uint32) (sqrt(dx * dx) + 0.5);
  print_state->hw_resolution[1] = (uint32) (sqrt(dy * dy) + 0.5);
}


/* Log stripped */

