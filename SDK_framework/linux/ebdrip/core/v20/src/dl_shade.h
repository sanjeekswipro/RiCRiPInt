/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_shade.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list functions for smooth shading.
 */

#ifndef __DL_SHADE_H__
#define __DL_SHADE_H__

#include "displayt.h" /* GOURAUDOBJECT, HDL, DLREF, etc */
#include "dl_color.h" /* p_ncolor_t */

struct SHADINGinfo ;
struct GUCR_RASTERSTYLE ;

void gouraudbbox(GOURAUDOBJECT *gouraud, const dbbox_t *clip, dbbox_t *bbox) ;

Bool addgourauddisplay(DL_STATE *page, GOURAUDOBJECT *g);

/* Iterate over all colours in a Gouraud object */
Bool gouraud_iterate_dlcolors(LISTOBJECT *lobj,
                              Bool (*callbackfn)(p_ncolor_t *color, void *data),
                              void *data) ;


Bool shading_color_close_enough(struct SHADINGinfo *sinfo,
                                dl_color_t * pDlc_i,
                                dl_color_t * pDlc_v,
                                struct GUCR_RASTERSTYLE *hRasterStyle);

#endif /* protection for multiple inclusion */


/* Log stripped */
