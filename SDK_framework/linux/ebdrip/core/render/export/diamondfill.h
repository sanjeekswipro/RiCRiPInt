/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!export:diamondfill.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Function to scan convert image tile.
 */

#ifndef __DIAMONDFILL_H__
#define __DIAMONDFILL_H__

struct render_blit_t ; /* from render.h */

/** This function fills a diamond, given by a base coordinate pair and two
   vectors. The non-overlapping scan converter is used; this is used by the
   rotated image tile generation to create tiles which tesselate exactly. */
void diamond_fill(struct render_blit_t *rb,
                  dcoord bx, dcoord by, dcoord dx1, dcoord dy1,
                  dcoord dx2, dcoord dy2) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
