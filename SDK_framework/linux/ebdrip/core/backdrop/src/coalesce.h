/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:coalesce.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Coalesce spans back into blocks to allow the line repeat optimisation to
 * be applied.
 */

#ifndef __COALESCE_H__
#define __COALESCE_H__

#include "blitcolorh.h"

typedef struct SpanCoalesce SpanCoalesce;

Bool bd_coalesceNew(int32 xsize, mm_cost_t cost, SpanCoalesce **newCoalesce);
void bd_coalesceFree(SpanCoalesce **freeCoalesce);
void bd_coalesceInit(SpanCoalesce *coalesce);
void bd_coalesceFlush(CompositeContext *context);
void bd_coalesceSpan(CompositeContext *context,
                     const Backdrop *backdrop, blit_color_t *sourceColor,
                     dcoord y, dcoord x1, dcoord runLen);

#endif /* protection for multiple inclusion */

/* Log stripped */
