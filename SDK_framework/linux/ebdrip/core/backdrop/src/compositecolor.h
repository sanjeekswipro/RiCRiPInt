/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:compositecolor.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __COMPOSITECOLOR_H__
#define __COMPOSITECOLOR_H__

#include "blitcolorh.h"

void bd_sourceColorBackdropSetup(CompositeContext *context,
                                 const Backdrop *backdrop,
                                 const Backdrop *sourceBackdrop);

Bool bd_sourceColorBlit(CompositeContext *context, const Backdrop *backdrop,
                        blit_color_t *sourceColor);

void bd_sourceColorBackdrop(CompositeContext *context, const Backdrop *backdrop);

void bd_sourceColorComplete(CompositeContext *context, const Backdrop *backdrop);

#if defined( ASSERT_BUILD )
void bd_checkColor(COLORVALUE *color, COLORVALUE alpha, uint32 nComps,
                   Bool premult);
#else
#define bd_checkColor(color, alpha, nComps, premult)
#endif /* ASSERT_BUILD */

#endif /* protection for multiple inclusion */

/* Log stripped */
