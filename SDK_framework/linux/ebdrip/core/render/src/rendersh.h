/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:rendersh.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported functions for smooth shading: Rendering side functions
 */

#ifndef __RENDERSH_H__
#define __RENDERSH_H__

#include "displayt.h" /* HDL */
#include "render.h" /* render_info_t */
#include "blitcolort.h" /* channel_index_t */

Bool render_gouraud(render_info_t* p_ri, Bool screened);

Bool render_shfill_all(render_info_t *p_ri, Bool screened,
                       HDL *hdl, channel_index_t nchannels, Bool transparency);

#endif /* protection for multiple inclusion */

/* Log stripped */
