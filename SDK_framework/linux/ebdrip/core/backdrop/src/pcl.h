/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:pcl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL using backdrop to handle PCL ROPs that involve destination.
 */

#ifndef __BACKDROPPCL_H__
#define __BACKDROPPCL_H__

#include "pclAttrib.h"

typedef struct PclBDState PclBDState;

Bool pcl_stateNew(uint32 inCompsMax, mm_cost_t cost, PclBDState **newPcl);
void pcl_stateFree(PclBDState **freePcl);
void pcl_runInfo(PclBDState *pcl, PclAttrib *attrib);
Bool pcl_hasAttrib(PclBDState *pcl);
Bool pcl_patternActive(PclBDState *pcl);
void pcl_sourceColor(CompositeContext *context, Bool *opaque);
void pcl_setPattern(PclBDState *pcl, dcoord spanX, dcoord spanY);
uint32 pcl_loadRun(PclBDState *pcl, uint32 xi, uint32 runLen);
Bool pcl_compositeColor(CompositeContext *context, const Backdrop *backdrop);

#endif /* protection for multiple inclusion */

/* Log stripped */
