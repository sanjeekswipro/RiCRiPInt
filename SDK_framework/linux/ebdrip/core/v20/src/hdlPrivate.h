/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:hdlPrivate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private interface to HDL.

 * This file contains private (compound-local) method interfaces. These are
 * private because a) they are functionality not required by high-level clients,
 * and b) because they require knowledge of data structures that are private (or
 * should be private ;) to this compound.
 *
 * Note that export:hdl.h contains the introductory documentation for the HDL.
 */

#ifndef __HDLPRIVATE_H__
#define __HDLPRIVATE_H__

#include "dl_storet.h"
#include "displayt.h"
#include "dl_color.h"
#include "graphict.h"
#include "render.h"

#include "hdl.h" /* users expect this header to load hdl.h */

DlSSEntry *hdlStoreEntry(HDL *hdl);
Bool hdlAdd(HDL *hdl, LISTOBJECT *lobj);
Bool hdlMergeColorUpdate(HDL *hdl, LISTOBJECT *lobj);
void hdlSetGroup(HDL *hdl, Group *group);
void hdlSetTransparent(HDL *hdl);
Bool hdlRecombined(HDL *hdl);
Bool hdlOverprint(HDL *hdl);
Bool hdlSetSelfIntersect(HDL *hdl);
void hdlTakeBandTailSnapshot(HDL *hdl);
void hdlRestoreBandTails(HDL *hdl);
HDL *hdlBase(HDL *hdl);
uint32 hdlOffsetIntoPageDL(HDL *hdl);
Range hdlExtentOnPage(HDL *hdl);
void hdlBandRangeOnPage(DL_STATE *page, dbbox_t *bbox, Range *range);
Range hdlUsedBands(HDL *hdl);
Bool hdlCleanupAfterPartialPaint(HDL **hdlPointer);
HDL *hdlTarget(HDL *list, GSTATE *gs);
Bool hdlPrepareBanding(DL_STATE *page);
int32 hdlPurpose(HDL *hdl);
int hdlPurgeLevel(HDL *hdl);
void hdlSetPurgeLevel(HDL *hdl, int level);
LISTOBJECT *hdlSingleObject(HDL *hdl);

#endif

/* Log stripped */
