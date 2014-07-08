/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2polygon.h(EBDSDK_P.1) $
 * $Id: src:hpgl2polygon.h,v 1.10.6.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Polygon Group" category.
 */

#ifndef __HPGL2POLYGONE_H__
#define __HPGL2POLYGONE_H__

#include "pcl5context.h"
#include "hpgl2scan.h"

/* Be sure to update default_HPGL2_polygon_info() if you change this structure. */
typedef struct HPGL2PolygonInfo {
  Bool enabled;
  uint8 saved_pen_state;
  /* certain elements of state are preserved across polygon mode entry/exit.
   * For reference printer, this appears to be only pen state */
} HPGL2PolygonInfo;

struct HPGL2PrintState;
struct HPGL2PolygonBuffer;

/** Initialise default polygon info. */
void default_HPGL2_polygon_info(HPGL2PolygonInfo* self);

/**
 * Get opaque pointer representing the polygon buffer implementation.
 */
struct HPGL2PolygonBuffer *get_hpgl2_polygon_buffer(PCL5Context *pcl5_ctxt);

/** Create opaque polygon bufferr state. */
struct HPGL2PolygonBuffer* hpgl2_polygon_buffer_create(void);

/** Destroy opaque polygon buffer state. */
Bool hpgl2_polygon_buffer_destroy(struct HPGL2PolygonBuffer *buff);

/** Check if HPGL2 interpreter is in polygon mode.*/
Bool hpgl2_in_polygon_mode(PCL5Context *pcl5_ctxt);

/** force current subpath in polygon buffer to be closed. */
Bool hpgl2_close_polygon_buffer_subpath(PCL5Context *pcl5_ctxt);
/**
 * for hpgl2 interpreter out of polygon mode.
 * This API is used when e.g. PCL state change requires clearing
 * the polygon buffer of HPGL2.
 */
Bool hpgl2_force_exit_polygon_mode(PCL5Context *pcl5_ctxt);

/* -- HPGL2 operators -- */

/* (RQ is unsupported) */
Bool hpgl2op_EA(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_EP(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_ER(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_EW(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_FP(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PM(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_RA(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_RR(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_WG(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_RQ(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
