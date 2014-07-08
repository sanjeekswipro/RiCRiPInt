/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderbd.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for rendering a backdrop.
 */

#ifndef __RENDERBD_H__
#define __RENDERBD_H__

#include "surface.h"
#include "rendert.h"

Bool backdropblt_builtin(surface_handle_t handle,
                         render_blit_t *rb,
                         surface_backdrop_t group_handle,
                         surface_backdrop_t target_handle,
                         SPOTNO spotno,
                         HTTYPE objtype) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
