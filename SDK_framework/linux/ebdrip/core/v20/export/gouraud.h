/** \file
 * \ingroup gouraud
 *
 * $HopeName: SWv20!export:gouraud.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Front end decomposition functions for gouraud triangles.
 */

#ifndef __GOURAUD_H__
#define __GOURAUD_H__

struct SHADINGinfo ;
struct SHADINGvertex ;

/** \defgroup gouraud Gouraud shading
    \ingroup shfill */
/** \{ */

/* Routine to decompose gouraud-shaded triangle into linearly interpolatable
   colour sections, and add object to DL representing this */
Bool dodisplaygouraud(DL_STATE *page,
                      struct SHADINGvertex *v1, struct SHADINGvertex *v2,
                      struct SHADINGvertex *v3, struct SHADINGinfo *sinfo) ;

/* Decompose triangle with non-continuous function into continuous triangles */
Bool decompose_triangle(struct SHADINGvertex *v1, struct SHADINGvertex *v2,
                        struct SHADINGvertex *v3, struct SHADINGinfo *sinfo) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
