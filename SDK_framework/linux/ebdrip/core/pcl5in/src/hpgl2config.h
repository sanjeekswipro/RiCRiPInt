/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2config.h(EBDSDK_P.1) $
 * $Id: src:hpgl2config.h,v 1.19.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Configuration Group" category.
 */

#ifndef __HPGL2CONFIG_H__
#define __HPGL2CONFIG_H__

#include "pcl5context.h"

/* scaling mode. */
enum {
  HPGL2_SCALE_ANISOTROPIC = 0,
  HPGL2_SCALE_ISOTROPIC,
  HPGL2_SCALE_POINT_FACTOR
} ;

typedef struct HPGL2ScalingAnisoParams {
  HPGL2Real x1,y1,x2,y2;
} HPGL2ScalingAnisoParams;

typedef struct HPGL2ScalingIsoParams {
  HPGL2Real x1,y1,x2,y2;
  HPGL2Real left, bottom;
} HPGL2ScalingIsoParams;

typedef struct HPGL2ScalingPointScaleParams {
  HPGL2Real x1, y1;
  HPGL2Real scale_x, scale_y;
} HPGL2ScalingPointScaleParams ;

typedef struct HPGL2PlotterPoint {
  HPGL2Real x;
  HPGL2Real y;
} HPGL2PlotterPoint;

/* Type of soft clip to apply.
 * Depends on how clip was defined, and not the current scaling mode.
 */
enum {
  HPGL2_NO_SOFT_CLIP,
  HPGL2_SCALABLE_SOFT_CLIP,
  HPGL2_FIXED_SOFT_CLIP
};

/* Be sure to update default_HPGL2_config_info() if you change this structure. */
typedef struct HPGL2ConfigInfo {

  Bool  scale_enabled;
  uint8 scale_mode; /* print state determines if this scaling is done*/
  HPGL2Integer rotation;

  struct {
    HPGL2Point ll;
    HPGL2Point ur;
    HPGL2Integer soft_clip_type;   /* soft clip enabled */
  } window;

  /* scale points and scaling parameters define the HPGL2 user unit
   * coord space. Alteration to either can define a new coord space.
   */
  struct {
    HPGL2PlotterPoint p1;
    HPGL2PlotterPoint p2;
  } scale_points;

  union {
    HPGL2ScalingAnisoParams anisotropic;
    HPGL2ScalingIsoParams isotropic;
    HPGL2ScalingPointScaleParams point_scale;
  } scaling_parameters;

} HPGL2ConfigInfo;

/**
 * Initialise default HPGL2 config info.
 */
void default_HPGL2_config_info(HPGL2ConfigInfo* self);

/** reset to default values, conditional on whether reset done in DF or IN context.*/
void hpgl2_DF_or_IN_set_default_state(PCL5Context *pcl5_ctxt, Bool init);

/** Sync all the HPGL2 state with gstate */
void hpgl2_sync_state(PCL5Context *pcl5_ctxt, Bool init);

/* clip state manipulation for HPGL2 */
Bool set_hpgl2_picture_frame_clip(PCL5Context *pcl5_ctxt);

Bool restore_pcl_clip(PCL5Context *pcl5_ctxt);

/* --- state helper functions. --- */
Bool hpgl2_is_rotated(PCL5Context *pcl5_ctxt);

/**
 * Returns true if the HPGL context is using user units for coordinates.
 * Returns false if the HPGL context is using (nominal) plotter units.
 */
Bool hpgl2_is_scaled(PCL5Context *pcl5_ctxt);

void hpgl2_update_scaled_ctm(PCL5Context *pcl5_ctxt);

void hpgl2_update_rotated_ctm(PCL5Context *pcl5_ctxt);

HPGL2Real get_p1_p2_distance(PCL5Context *pcl5_ctxt);

void hpgl2_default_scale_points(PCL5Context *pcl5_ctxt);

#define CLAMP_HPGL2_VALUE(v, l, h) (min(max(v, l), h))

#define clamp_hpgl2real(v, l, h)  CLAMP_HPGL2_VALUE((v), (l), (h))
#define clamp_hpgl2int(v, l, h)   CLAMP_HPGL2_VALUE((v), (l), (h))


/* handlers that link changes to PCL state to changes to HPGL2 state. */
void hpgl2_handle_PCL_printer_reset(PCL5Context *pcl5_ctxt);

void hpgl2_handle_PCL_page_change(PCL5Context *pcl5_ctxt);

void hpgl2_handle_PCL_picture_frame_redefine(PCL5Context *pcl5_ctxt);

void hgpl2_handle_PCL_picture_frame_anchor_change(PCL5Context *pcl5_ctxt);

void hpgl2_handle_PCL_plot_size_change(PCL5Context *pcl5_ctxt);

/* plot size scaling helper functions. */
HPGL2Real horizontal_scale_factor(PCL5Context *pcl5_ctxt);

HPGL2Real vertical_scale_factor(PCL5Context *pcl5_ctxt);

/* --- HPGL2 operators  --- */

/* (PG and RP are unsupported) */
Bool hpgl2op_CO(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_DF(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_IN(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_IP(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_IR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_IW(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_RO(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_SC(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
