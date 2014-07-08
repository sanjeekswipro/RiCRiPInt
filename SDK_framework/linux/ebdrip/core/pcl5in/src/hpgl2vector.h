/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2vector.h(EBDSDK_P.1) $
 * $Id: src:hpgl2vector.h,v 1.25.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Vector Group" category.
 */

#ifndef __HPGL2VECTOR_H__
#define __HPGL2VECTOR_H__

#include "pcl5context.h"
#include "pathops.h"

/* Pen states. */
enum {
  HPGL2_PEN_UP,
  HPGL2_PEN_DOWN,
  HPGL2_INVALID_PEN_STATE = 99
} ;

/* plot modes */
enum {
  HPGL2_PLOT_ABSOLUTE,
  HPGL2_PLOT_RELATIVE
} ;

/* Be sure to update default_HPGL2_vector_info() if you change this structure. */
typedef struct HPGL2VectorInfo {
  uint8 plot_mode;
  uint8 pen_state;
} HPGL2VectorInfo;

/**
 * Initialise default vector info.
 */
void default_HPGL2_vector_info(HPGL2VectorInfo* self);

/** reset to default values, conditional on whether reset done in DF or IN
 *  context.*/
void hpgl2_set_default_vector_info(HPGL2VectorInfo *vector_info,
                                   Bool initialize);

/** synchronise the interpreter gstate with HPGL2 vector info */
void hpgl2_sync_vector_info(HPGL2VectorInfo *vector_info, Bool initialize);

/* --- HPGL2 operators --- */

Bool hpgl2op_AA(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_AR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_AT(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_BR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_BZ(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_CI(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PA(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PD(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PE(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PU(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_RT(PCL5Context *pcl5_ctxt) ;

/* --- help functions. --- */

struct HPGL2LineFillInfo ;
struct PATHINFO;

/**
 * Call out to the core to preform a stroke.
 */
Bool hpgl2_stroke_internal(STROKE_PARAMS *params, STROKE_OPTIONS options);

Bool hpgl2_stroke(PCL5Context *pcl5_ctxt,
                  Bool update_linetype_residue,
                  Bool new_path,
                  Bool apply_thin_line_override) ;

Bool hpgl2_moveto(PCL5Context *pcl5_ctxt, HPGL2Point *point) ;

Bool hpgl2_lineto(PCL5Context *pcl5_ctxt, HPGL2Point *point) ;

Bool hpgl2_closepath(PCL5Context *pcl5_ctxt);

Bool hpgl2_currentpoint(PCL5Context *pcl5_ctxt,
                        SYSTEMVALUE *x, SYSTEMVALUE *y);

/* HPGL jobs normally express coordinates in terms of plotter units, or
 * user units. User units are defined wrt the scale points; plotter units are
 * nominally absolute measures (1/1016 inch). However, if a job applies plot
 * sized scaling then its notion of a plotter unit is scaled.
 * The HPGL interpreter requires that all drawing be done in terms of real
 * plotter units, and so requires functions to map between the units in the
 * job ( whether user units or nominal plotter units ) and the real plotter
 * units used in the interpreter.
 */

/**
 * Turn a point specified in plotter units into a point represented in the
 * current coordinate space of the job. out parameter might represent user units
 * or nominal plotter units for the job, depending on whether the PCL5Context
 * indicates HPGL scaling is on. Nominal plotter units are acutual plotter
 * units scaled by the inverse of the plot size scaling factors.
 */
Bool plotter_point_to_job_point(PCL5Context *pcl5_ctxt,
                                 HPGL2Point *in,
                                 HPGL2Point *out,
                                 Bool relative) ;

/* Map user units / nominal plotter units to the real plotter units of the
 * output device. Accounts for plot sized scaling of coordinates given in the
 * job in terms of (nominal) plotter units.
 */
Bool job_point_to_plotter_point(PCL5Context *pcl5_ctxt,
                                 HPGL2Point *in,
                                 HPGL2Point *out,
                                 Bool relative) ;

enum {
  HPGL2_CHORD_COUNT_ROUND_ARC,
  HPGL2_CHORD_COUNT_ROUND_WEDGE,
  HPGL2_CHORD_COUNT_ROUND_CIRCLE
} ;

Bool draw_arc(PCL5Context *pcl5_ctxt, HPGL2Point *center, HPGL2Real chord_ang,
                     HPGL2Real sweep, Bool closed,
                     Bool stroked, Bool update_residue,
                     uint8 chord_count_policy);

Bool hpgl2_setgray(USERVALUE gray_val);

Bool draw_PD_dot(PCL5Context *pcl5_ctxt);

/* ============================================================================
* Log stripped */
#endif
