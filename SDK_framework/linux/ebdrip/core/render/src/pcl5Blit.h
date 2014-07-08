/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:pcl5Blit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL5 Blitters.
 */
#ifndef _pcl5blit_h_
#define _pcl5blit_h_

#include "surface.h"
#include "render.h"

void init_halftone1_rops(surface_t *surface) ;
void init_tone8_rops(surface_t *surface) ;

void pcl5_ropmode(render_blit_t *rb, uint8 *ropmode);

/**
 * Returns true if the mono PCL blitter is required to apply the passed
 * attributes.
 */
Bool pcl5_mono_blitter_required(render_info_t* p_ri);

#endif

/* Log stripped */

