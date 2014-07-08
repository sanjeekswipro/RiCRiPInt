/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!export:compositers.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header file for compositers.c
 */

#ifndef __COMPOSITERS_H__
#define __COMPOSITERS_H__

#include "cce.h"
#include "blitcolort.h"

/* --Public methods-- */

/* Blend function arguments. */
#define CCE_ARGS uint32 count, \
                 const COLORVALUE *src, \
                 COLORVALUE srcAlpha, \
                 const COLORVALUE *bd, \
                 COLORVALUE bdAlpha, \
                 COLORVALUE *result

/* Separable color compositers. */
void cceNormal(CCE_ARGS);
void cceNormalPreMult(CCE_ARGS);
void cceMultiply(CCE_ARGS);
void cceMultiplyPreMult(CCE_ARGS);
void cceScreen(CCE_ARGS);
void cceScreenPreMult(CCE_ARGS);
void cceOverlay(CCE_ARGS);
void cceOverlayPreMult(CCE_ARGS);
void cceSoftLight(CCE_ARGS);
void cceSoftLightPreMult(CCE_ARGS);
void cceHardLight(CCE_ARGS);
void cceHardLightPreMult(CCE_ARGS);
void cceColorDodge(CCE_ARGS);
void cceColorDodgePreMult(CCE_ARGS);
void cceColorBurn(CCE_ARGS);
void cceColorBurnPreMult(CCE_ARGS);
void cceDarken(CCE_ARGS);
void cceDarkenPreMult(CCE_ARGS);
void cceLighten(CCE_ARGS);
void cceLightenPreMult(CCE_ARGS);
void cceDifference(CCE_ARGS);
void cceDifferencePreMult(CCE_ARGS);
void cceExclusion(CCE_ARGS);
void cceExclusionPreMult(CCE_ARGS);

/* Non-separable color compositers. */
void cceHue(CCE_ARGS);
void cceHuePreMult(CCE_ARGS);
void cceSaturation(CCE_ARGS);
void cceSaturationPreMult(CCE_ARGS);
void cceColor(CCE_ARGS);
void cceColorPreMult(CCE_ARGS);
void cceLuminosity(CCE_ARGS);
void cceLuminosityPreMult(CCE_ARGS);


/* Special compositers for overprinting. */

/**
 * When there's no transparency involved, overprinting follows the standard
 * opaque painting model.
 */
void cceOpaqueOverprint(const blit_channel_state_t *opFlags, uint32 count,
                        const COLORVALUE *src, const COLORVALUE *bd, COLORVALUE* result);

/* When the source or backdrop are non-opaque. */
void cceCompatibleOverprint(const blit_channel_state_t *opFlags,
                            uint32 count,
                            const COLORVALUE *src, COLORVALUE srcAlpha,
                            const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                            COLORVALUE* result);

void cceCompatibleOverprintPreMult(const blit_channel_state_t *opFlags,
                                   uint32 count,
                                   const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                                   const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                                   COLORVALUE* result);

/* Alpha compositer. */
void cceCompositeAlpha(COLORVALUE alpha1,
                       COLORVALUE alpha2,
                       COLORVALUE* result);

/* Separate/premultiplied alpha converters. */
void cceMultiplyAlpha(uint32 count,
                      const COLORVALUE *src,
                      COLORVALUE alpha,
                      COLORVALUE* result);

void cceDivideAlpha(uint32 count,
                    const COLORVALUE *src,
                    COLORVALUE alpha,
                    COLORVALUE* result);

/* Weighted average functions for color and alpha. These are used to handle
   the fractional shape part of the compositing formula.

   result[i] = (1 - shape) * bd[i] + shape * src[i]
 */
void cceWeightedAverage(uint32 count, const COLORVALUE *src,
                        const COLORVALUE *bd, COLORVALUE bdAlpha,
                        COLORVALUE shape, COLORVALUE *result);

void cceWeightedAverageAlpha(COLORVALUE src, COLORVALUE bd,
                             COLORVALUE shape, COLORVALUE *result);

/**
 * For removing the contribution of the group backdrop from the computed
 * results; to ensure when a NON-ISOLATED group is itself composited into
 * another group, the backdrop's contribution is included only once.
 * For isloated groups this operation simplifies to a no-op.
 * Note, assumes that source color is NOT PREMULTIPLIED and
 * background color IS PREMULTIPLIED.
 */
void cceRemoveBackdropContribution(uint32 count,
                                   const COLORVALUE *src, COLORVALUE srcAlpha,
                                   const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                                   COLORVALUE* result);
/* --Description--

Interfaces to all compositers supported by the CCE.
In general, these functions are not be called directly, but instead called
through the interfaces provided in a CCE object instance.

When calling these methods, it is possible for the result list to
be the same as one of the source lists, for example:

  cceNormal(cce->state, 4, src, srcAlpha, bg, bgAlpha, bg);
*/

#endif

/* Log stripped */
