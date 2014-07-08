/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:halftoneblts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone bit blitting routines and structures.
 */

#ifndef __HALFTONEBLTS_H__
#define __HALFTONEBLTS_H__ 1

#include "surface.h"
#include "htrender.h"
#include "bitbltt.h"

extern blitclip_slice_t blitsliceh[NHALFTONETYPES] ;
extern blitclip_slice_t nbit_blit_sliceh[NHALFTONETYPES];

extern blitclip_slice_t ht_xor_slice ;
extern blitclip_slice_t ht_xornot_slice ;
extern blitclip_slice_t ht_or_slice ;
extern blitclip_slice_t ht_and_slice ;

void init_halftone1_span(surface_t *surface) ;
void init_halftonen_span(surface_t *surface) ;

void imagebltht(render_blit_t *rb, imgblt_params_t *params,
                imgblt_callback_fn *callback,
                Bool *result) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
