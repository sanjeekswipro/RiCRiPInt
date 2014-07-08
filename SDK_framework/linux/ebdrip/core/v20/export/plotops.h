/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:plotops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * header file for plotops.c declaring exported functions and variables
 */

#ifndef __PLOTOPS_H__
#define __PLOTOPS_H__

#include "displayt.h"
#include "graphict.h"

/* ----- External global variables ----- */

extern Bool degenerateClipping ;
extern int32 clipmapid ;

/* ----- Exported functions ----- */

/**
 * Inform the core that we are entering a text context; anything added to the
 * DL during a text context is marked as being text. Thus this method should be
 * called before adding any text to the DL.
 */
void textContextEnter(void);

/**
 * Leave a text context.
 */
void textContextExit(void);

Bool csetg(DL_STATE *page, int32 colorType, int32 options);
Bool setg(DL_STATE *page, int32 colorType, int32 options);

Bool setup_rect_clipping(DL_STATE *page, CLIPOBJECT **clipping, dbbox_t *bbox);
Bool setup_vignette_clipping(DL_STATE *page, DLREF *dlobj);

struct NFILLOBJECT ;

Bool augment_clipping(DL_STATE *page, LISTOBJECT *lobj,
                      struct NFILLOBJECT *nfillclip,
                      dbbox_t *bboxclip);

Bool getTransparencyState(TranState* ts,
                          DL_STATE *page,
                          Bool useStrokingAlpha,
                          TranAttrib** result);

Bool getLateColorState(GSTATE* gs,
                       DL_STATE* page,
                       int32 colorType,
                       LateColorAttrib** result);

Bool clipIsRectangular(CLIPOBJECT *clip);


#endif /* protection for multiple inclusion */


/* Log stripped */
