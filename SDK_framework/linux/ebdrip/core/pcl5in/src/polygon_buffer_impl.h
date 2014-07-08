/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:polygon_buffer_impl.h(EBDSDK_P.1) $
 * Last $Date: 2013/12/19 11:25:01 $ by  $Author: anon $
 *
 * Copyright (C) 2001-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of polygon buffer, based on existing path construction
 * operators of the rip.
 * This module contains the functions that enable polygon paths to be
 * created, destroyed, stroked and filled.
 * It does not deal with the management of polygon mode, only the
 * provision of the means to construct and use polygon paths.
 */

/* The polygon buffer is a separate path, maintained concurrently
 * with the gstate current path. hpgl2 drawing operators mirror
 * the conventional path construction operators, acting as proxies
 * for either the usual gs_* path construction operators, or as
 * special cases for the polygon buffer. This allows the actual
 * drawing code to ignore whether or not it draws into the polygon
 * buffer or draws "directly" to the page.
 * Both direct and polygon mode have specific state to manage.
 * Polygon mode needs to account for defining polygons with pen up.
 * "Direct" mode needs to account for symbol mode etc.
 * The intention is that there is sufficient encapsulation
 * to allow different approaches to polygon buffer implementation
 * to be swapped in if necessary.
 * Polygon mode has its own operators for :
 * lineto, moveto, curveto, closepath, stroke, fill, newpath.
 *
 * The implementation of the polygon buffer contained below
 * ultimately constructs a second PATHINFO for the polygon
 * buffer, very much like gs_* operators (with some modification).
 * This frees the polygon buffer from worrying about the memory
 * management etc, but does tie the implementation to using
 * the temporary pool etc.
 *
 * When ordered to draw or fill a polygon, the polygon buffer
 * does so in the context of the current gstate.
 */

#ifndef __POLYGON_BUFFER_IMPL_H___
#define __POLYGON_BUFFER_IMPL_H___

#include "pcl5.h"
#include "pcl5types.h"
#include "pcl5context.h"

struct HPGL2PolygonBuffer;
struct HPGL2LineFillInfo;

/** Creation of the polygon buffer in the print state. */
struct HPGL2PolygonBuffer* polygon_buffer_impl_state_create(void);

/** Destructor for ploygon buffer state. */
Bool polygon_buffer_impl_state_destroy(struct HPGL2PolygonBuffer *buff);

/* --- Polygon buffer path construction API. --- */

/** remove any existing path in the buffer. */
Bool hpgl2_empty_polygon_buffer(struct HPGL2PolygonBuffer *polygon_buff);

/* --- path construction routines --- */

/** plot a new point in the buffer. */
Bool hpgl2_polygon_plot(struct HPGL2PolygonBuffer *polygon_buff,
                        Bool absolute, Bool pen_down, Bool pen_selected,
                        HPGL2Point *point);

/** report current point of the polygon buffer. */
Bool hpgl2_polygon_current_point(struct HPGL2PolygonBuffer *polygon_buff,
                                 SYSTEMVALUE *x, SYSTEMVALUE *y);

/** add moveto to polygon buffer */
Bool hpgl2_polygon_moveto(struct HPGL2PolygonBuffer *polygon_buff,
                          HPGL2Point *point, Bool absolute);

/** fill the polygon in the buffer */
Bool fill_polygon_buffer(PCL5Context *pcl5_ctxt, int32 fill_mode);

/** stroke the polygon in the buffer. */
Bool draw_polygon_buffer(PCL5Context *pcl5_ctxt);

/** Add cubic Bezier curve to the polygon buffer. */
Bool hpgl2_polygon_curveto(struct HPGL2PolygonBuffer *polygon_buff,
                           HPGL2Point *p1,HPGL2Point *p2, HPGL2Point * p3,
                           Bool absolute, Bool strokedi, Bool pen_selected);

/** Close the current path in the polygon buffer. */
Bool hpgl2_polygon_closepath(struct HPGL2PolygonBuffer *polygon_buff,
                              Bool pen_down);


/* Log stripped */

#endif
