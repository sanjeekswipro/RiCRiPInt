/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pictureframe.h(EBDSDK_P.1) $
 * $Id: src:pictureframe.h,v 1.18.6.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PICTUREFRAME_H__
#define __PICTUREFRAME_H__ (1)

#include "pcl5context.h"
#include "cursorpos.h"
#include "hpgl2scan.h"

/* Be sure to update default_picture_frame_info() if you change this structure. */
typedef struct PictureFrameInfo {
  CursorPosition anchor_point;
  /* Defined wrt to default orientationc (print direction 0).
   * Not necessarily same as PCL cursor position.
   */

  PCL5Real default_width;
  PCL5Real default_height;
  PCL5Real plot_horizontal_size;
  PCL5Real plot_vertical_size;
} PictureFrameInfo;

PictureFrameInfo* get_picture_frame_info(PCL5Context *pcl5_ctxt);

/**
 * Initialise default picture frame info.
 */
void default_picture_frame_info(PictureFrameInfo* self);

Bool set_hpgl2_ctm(PCL5Context *pcl5_ctxt);

Bool set_hpgl2_rotation_ctm(PCL5Context *pcl5_ctxt);

Bool set_hpgl2_scaled_ctm(PCL5Context *pcl5_ctxt);

void set_hpgl2_plot_width(PCL5Context *pcl5ctxt, HPGL2Real width);

void set_hpgl2_plot_height(PCL5Context *pcl5ctxt, HPGL2Real height);

/* functions that reset HPGL2 values to defaults must only be called
 * when the MPE has well defined page size, length, margins.
 */
void default_picture_frame_dimensions(PCL5Context *pcl5ctxt);

void default_picture_frame_position(PCL5Context *pcl5_ctxt);

void default_plot_size(PCL5Context *pcl5_ctxt);

/**
 * The PCL ctm changes as the PCL margins are changed. Calling this function
 * will allow the anchor point for the picture frame to remain in a constant
 * physical location.
 * old_ctm is the old page orientation matrix for PCL.
 * new_ctm is the new page orientation matrix for PCL.
 * This function should not be called from inside an HPGL context.
 * Picture frame anchor point is specified wrt the page orientation, not
 * the print direction.
 */
void handle_ctm_change_picture_frame(PCL5Context *pcl5_ctxt,
                                     OMATRIX*old_ctm,
                                     OMATRIX*new_ctm);

/** Register the fact that we need to sync gstate on next entry to HPGL */
void set_hpgl2_sync_required(void);

/*------------------- PCL operator callbacks. ----------------------*/

Bool pcl5op_star_c_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_Y(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_K(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_star_c_L(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_percent_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_percent_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
