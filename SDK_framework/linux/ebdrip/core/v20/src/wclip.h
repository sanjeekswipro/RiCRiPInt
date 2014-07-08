/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:wclip.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal API for Rectangular Window clipping code.
 */

#ifndef __WCLIP_H__
#define __WCLIP_H__

/*
 * The API to the wclip algorithm
 */

/*
 * Test the supplied path to see if it contains any ordinates which are 
 * too big to be processed by later (integer based) fill code.
 */
Bool is_huge_path(PATHLIST *path);

/*
 * Test the supplied point to see if it is too big to be processed by
 * later (integer based) fill code.
 */
Bool is_huge_point(dcoord x, dcoord y);

/*
 * Clip the path supplied to remove any ordinates which are too big
 * to be processed, but ensure the resulting path will still produce
 * the same results when stroked/filled.
 */
Bool clip_huge_path(PATHLIST *path, PATHINFO *clippedpath, STROKE_PARAMS *sp);

#endif /* protection for multiple inclusion */

/* Log stripped */
