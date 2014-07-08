/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:groupPrivate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private (compound-local) interface for the Group object.
 */

#ifndef __GROUPPRIVATE_H__
#define __GROUPPRIVATE_H__

#include "displayt.h"           /* Group */
#include "imageo.h"             /* IMAGEOBJECT */

HDL *groupHdlDetach(/*@notnull@*/ /*@in@*/ Group *group) ;

void groupAnnounceShapedObj(/*@notnull@*/ /*@in@*/ Group *group);

Bool groupNewBackdrops(/*@notnull@*/ /*@in@*/ DL_STATE *page);

void groupDestroy(/*@notnull@*/ /*@in@*/ Group **groupPointer);

void groupSoftMaskAreaExpand(Group *group, dbbox_t *bbox) ;

/* Replace the existing colorInfo with the new one which may include
   changes following a setinterceptcolorspace in the BeginPage hook. */
Bool groupResetColorInfo(Group *group, GS_COLORinfo *newColorInfo,
                         COLOR_STATE *colorState);

Bool groupResetDeviceRS(Group *group);

void groupAbandon(/*@notnull@*/ /*@in@*/ Group *group);

Bool groupCleanupAfterPartialPaint(/*@notnull@*/ /*@in@*/ Group **groupPointer);
void groupAbandonAll(DL_STATE *page);


/** Check to see if we can remove the group. */
Bool groupEliminate(Group *group, TranAttrib *group_ta, Bool *eliminate);

Bool groupInsideSoftMask(Group *group);

#endif /* __GROUPPRIVATE_H__ */

/* Log stripped */
