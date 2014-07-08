/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:region.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The page is notionally split up into regions.  Regions are the same
 * rectangular size, apart from the last row and column which may be smaller.
 * Each region can be composited or direct rendered and this decision is made
 * based on the DL objects that intersect each region.
 *
 * A region is marked for compositing if it contains transparency or if it
 * overprints another object.  Since compositing is expensive the code tries
 * avoid marking regions for compositing.  For overprinted objects which paint
 * in virtual colorants (i.e. spots which will eventually be converted to
 * process) the background is tracked to determine if anything non-white has
 * been painted (on a per-colorant basis).  This optimisation was particularly
 * beneficial for some preseparated jobs.
 *
 * Softmask images are prevalent in photobook jobs and often large areas of the
 * image are completely transparent and do not need compositing.
 */

#ifndef __REGION_H__
#define __REGION_H__

#include "display.h"
#include "bitGrid.h"
#include "gs_color.h"
#include "surfacet.h"

struct CompositeContext;

typedef int32 DLRegionIterator;

/** Used to initialise a DLRegionIterator. */
enum { DL_REGION_ITERATOR_INIT = -1 };

/**
 * Create a region map and mark it according to the DL.  If LCM is enabled then
 * overprinted process colorants may need compositing for correct results.
 */
Bool dlregion_mark(
    /*@notnull@*/ /*@in@*/ DL_STATE *page);

Bool dlregion_iterator(
    /*@in@*/ /*@notnull@*/           DL_STATE *page,
    /*@in@*/ /*@null@*/              surface_handle_t handle,
    /*@in@*/ /*@notnull@*/           struct CompositeContext *context,
    /*@in@*/ /*@notnull@*/           dbbox_t *region,
                                     int32 currentbackdrops,
                                     int32 regionTypes,
    /*@in@*/ /*@out@*/ /*@notnull@*/ DLRegionIterator *iterator,
    /*@out@*/ /*@notnull@*/          dbbox_t *bounds,
    /*@out@*/ /*@notnull@*/          Bool *backdropRender);

Bool lobj_fullycompositing(
    /*@notnull@*/ /*@in@*/ LISTOBJECT *lobj,
    /*@notnull@*/ /*@in@*/ Group *group,
    /*@notnull@*/ /*@in@*/ HDL *hdl);

Bool lobj_maybecompositing(
    /*@notnull@*/ /*@in@*/ LISTOBJECT *lobj,
    /*@notnull@*/ /*@in@*/ Group *group,
                           Bool maxblts_supported);

#if defined(DEBUG_BUILD)
void dlregion_clip(DL_STATE *page, dbbox_t *bbox, int32 left, int32 right);
#endif

void set_region_size(DL_STATE *page);

#endif /* __REGION_H__ */

/* Log stripped */
