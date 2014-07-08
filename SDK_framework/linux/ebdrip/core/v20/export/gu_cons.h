/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:gu_cons.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphic-state path-building operators
 */

#ifndef __GU_CONS_H__
#define __GU_CONS_H__

#include "paths.h"

extern Bool gs_moveto(Bool absolute, SYSTEMVALUE args[2], PATHINFO *path);
extern Bool gs_lineto(Bool absolute, Bool stroked, SYSTEMVALUE args[2], PATHINFO *path);
extern Bool gs_curveto(Bool absolute, Bool stroked, SYSTEMVALUE args[6], PATHINFO *path);
extern Bool gs_quadraticcurveto(Bool absolute, Bool stroked, SYSTEMVALUE args[4], PATHINFO *path);
extern Bool gs_arcto(int32 flags, Bool stroked, register SYSTEMVALUE values[5], PATHINFO *path);
extern Bool gs_ellipticalarcto(Bool absolute, Bool stroked, Bool largearc, Bool sweepflag,
                               SYSTEMVALUE rx, SYSTEMVALUE ry, SYSTEMVALUE xrotation,
                               SYSTEMVALUE x2, SYSTEMVALUE y2, PATHINFO *path);

extern int32 checkbbox ;	/* rangecheck against gstate bounding box? */

#endif /* protection for multiple inclusion */

/* Log stripped */
