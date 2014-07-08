/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!export:blttables.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for surfaces and tables of blit functions.
 */

#ifndef __BLTTABLES_H__
#define __BLTTABLES_H__

#include "bitbltt.h" /* BITBLT_FUNCTION, et al */
#include "surface.h"

/** Blit slice with clipping optimisations suitable for rendering zeros. */
extern blitclip_slice_t blitslice0 ;

/** Blit slice with clipping optimisations suitable for rendering ones. */
extern blitclip_slice_t blitslice1 ;

/** Blit slice for rendering n-bit zeros, cf. blitslice0. */
extern blitclip_slice_t nbit_blit_slice0;

/** Blit slice for rendering n-bit ones, cf. blitslice1. */
extern blitclip_slice_t nbit_blit_slice1;

/** Blit slice with clipping optimisations suitable for rendering mask for
    Type 3 images. */
extern blitclip_slice_t maskimageslice ;

#endif /* protection for multiple inclusion */

/* Log stripped */
