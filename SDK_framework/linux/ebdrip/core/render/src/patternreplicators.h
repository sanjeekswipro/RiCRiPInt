/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:patternreplicators.h(EBDSDK_P.1) $
 * $Id: src:patternreplicators.h,v 1.6.1.1.1.1 2013/12/19 11:25:06 anon Exp $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Pattern replication blitters
 */

#ifndef __PATTERNREPLICATORS_H__
#define __PATTERNREPLICATORS_H__

#include "surface.h"
#include "patternrender.h"

#ifdef DEBUG_BUILD
extern int32 debug_pattern_first ;
extern int32 debug_pattern_last ;
#endif

void pattern_replicators_builtin(surface_t *surface) ;

Bool patterninttiling(pattern_tracker_t *tracker,
                      pattern_render_callback callback, void *data);

Bool patternrealtiling(pattern_tracker_t *tracker,
                       pattern_render_callback callback, void *data);

/* Log stripped */

#endif /* protection for multiple inclusion */
