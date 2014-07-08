/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!src:compositers.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Compositing functions for the various blend modes and their variants.
 * In general these functions should not be called directly, rather
 * through a properly configured CCE object.
 *
 * \verbatim
 *
 * ****************************************************************************
 *
 * The following list defines the various blend modes, first in words,
 * then as a function of the backdrop and source colors. The function
 * is written in the form
 *   Blend(b,s) = some function of 'b' and 's'
 * where 'b' is the backdrop color and 's' is the source color.
 *
 * Normal blend mode :
 * -----------------
 * Selects the source color, ignoring the backdrop:
 *
 * Blend(b,s) = s
 *
 * Multiply blend mode :
 * -------------------
 * Multiplies the backdrop and source color values:
 *
 * Blend(b,s) = b*s
 *
 * The result color is always at least as dark as either of the two
 * constituent colors. Multiplying any color with black produces black;
 * multiplying with white leaves the original color unchanged. Painting
 * successive overlapping objects with a color other than black or white
 * produces progressively darker colors.
 *
 * Screen blend mode :
 * -----------------
 * Multiplies the complements of the backdrop and source color values,
 * then complements the result:
 *
 * Blend(b,s) = 1 - (1-b)*(1-s)
 *            = b + s - (b*s)
 *
 * The result color is always at least as light as either of the two
 * constituent colors. Screening any color with white produces white;
 * screening with black leaves the original color unchanged. The effect
 * is similar to projecting multiple photographic slides simultaneously
 * onto a single screen.
 *
 * Overlay blend mode :
 * ------------------
 * Multiplies or screens the colors, depending on the backdrop color value.
 * Source colors overlay the backdrop while preserving its highlights and
 * shadows. The backdrop color is not replaced but is mixed with the source
 * color to reflect the lightness or darkness of the backdrop.
 *
 * Blend(b,s) = HardLight(s,b)
 *
 * Darken blend mode :
 * -----------------
 * Selects the darker of the backdrop and source colors:
 *
 * Blend(b,s) = min(b,s)
 *
 * The backdrop is replaced with the source where the source is darker;
 * otherwise, it is left unchanged.
 *
 * Lighten blend mode :
 * ------------------
 * Selects the lighter of the backdrop and source colors:
 *
 * Blend(b,s) = max(b,s)
 *
 * The backdrop is replaced with the source where the source is lighter;
 * otherwise, it is left unchanged.
 *
 * ColorDodge blend mode :
 * ---------------------
 * Brightens the backdrop color to reflect the source color. Painting with
 * black produces no changes.
 *
 * Blend(b,s) = min(1,b/(1-s)) if s < 1
 *            = 1              if s = 1
 *
 * ColorBurn blend mode :
 * ---------------------
 * Darkens the backdrop color to reflect the source color. Painting with
 * white produces no change.
 *
 * Blend(b,s) = 1 - min(1,(1 - b)/s) if s > 0
 *            = 0              if s = 0
 *
 * HardLight blend mode :
 * --------------------
 * Multiplies or screens the colors, depending on the source color value.
 * The effect is similar to shining a harsh spotlight on the backdrop.
 *
 * Blend(b,s) = Multiply(b,2*s)   if s <= 0.5
 *            = Screen(b,2*s - 1) if s > 0.5
 *
 * SoftLight blend mode :
 * --------------------
 * Darkens or lightens the colors, depending on the source color value.
 * The effect is similar to shining a diffused spotlight on the backdrop.
 *
 * Blend(b,s) = b - (1 - 2*s)*b*(1-b)    if s <= 0.5
 *            = b + (2*s - 1)*(D(b) - b) if s >  0.5
 * where
 *   D(x) = (16*x-12)*x + 4)*x   if x <= 0.25
 *        = sqrt(x)              if x >  0.25
 *
 * Difference blend mode :
 * ---------------------
 * Subtracts the darker of the two constituent colors from the lighter color:
 *
 * Blend(b,s) = | b - s |
 *
 * Painting with white inverts the backdrop color; painting with black
 * produces no change.
 *
 * Exclusion blend mode :
 * ---------------------
 * Produces an effect similar to that of the Difference mode but lower in
 * contrast. Painting with white inverts the backdrop color; painting with
 * black produces no change.
 *
 * Blend(b,s) = b + s - 2*b*s
 *
 * ****************************************************************************
 * \endverbatim
 */

#include "core.h"
#include "blitcolort.h"
#include "compositers.h"

#include "compositeMacros.h"

/* --Public methods-- */

/* --'Normal' blend mode variants-- */

void cceNormal(uint32 count,
               const COLORVALUE *src, COLORVALUE srcAlpha,
               const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
               COLORVALUE* result)
{
  uint32 i;

  UNUSED_PARAM(COLORVALUE, bdAlpha) ;

  for (i = 0; i < count; i ++) {
    result[i] = (COLORVALUE)(Multiply(COLORVALUE_ONE - srcAlpha, bdPremult[i]) +
                             Multiply(src[i], srcAlpha));
  }
}

void cceNormalPreMult(uint32 count,
                      const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                      const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                      COLORVALUE* result)
{
  uint32 i;

  UNUSED_PARAM(COLORVALUE, bdAlpha) ;

  for (i = 0; i < count; i ++) {
    result[i] = (COLORVALUE)(Multiply(COLORVALUE_ONE - srcAlpha, bdPremult[i]) +
                             srcPremult[i]);
  }
}

/* --'Multiply' blend mode variants-- */

/**
 * The Multiply blend function; this isn't used within the compositers because
 * it simplifies significantly when substituted within the full transparency
 * calculation, but is used elsewhere.
 */
static COLORVALUE multiplyBlendFunction(COLORVALUE srcColor, COLORVALUE bdColor)
{
  return Multiply(srcColor, bdColor);
}

void cceMultiply(uint32 count,
                 const COLORVALUE *src, COLORVALUE srcAlpha,
                 const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                 COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    uint32 srcPremult = Multiply(src[i], srcAlpha);
    result[i] = (COLORVALUE)(Multiply(COLORVALUE_ONE - srcAlpha, bdPremult[i]) +
                             Multiply(COLORVALUE_ONE - bdAlpha, srcPremult) +
                             Multiply(srcPremult, bdPremult[i]));
  }
}

void cceMultiplyPreMult(uint32 count,
                        const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                        const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                        COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    result[i] = (COLORVALUE)(Multiply(COLORVALUE_ONE - srcAlpha, bdPremult[i]) +
                             Multiply(COLORVALUE_ONE - bdAlpha, srcPremult[i]) +
                             Multiply(srcPremult[i], bdPremult[i]));
  }
}

/* --'Screen' blend mode variants-- */

/**
 * The Screen blend function; this isn't used within the compositers because
 * it simplifies significantly when substituted within the full transparency
 * calculation, but is used elsewhere.
 */
static COLORVALUE screenBlendFunction(COLORVALUE srcColor, COLORVALUE bdColor)
{
  return (COLORVALUE)(bdColor + srcColor - Multiply(bdColor, srcColor));
}

void cceScreen(uint32 count,
               const COLORVALUE *src, COLORVALUE srcAlpha,
               const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
               COLORVALUE* result)
{
  uint32 i;

  UNUSED_PARAM(COLORVALUE, bdAlpha) ;

  for (i = 0; i < count; i ++) {
    uint32 srcPremult = Multiply(src[i], srcAlpha);
    result[i] = (COLORVALUE)(srcPremult + bdPremult[i] -
                             Multiply(srcPremult, bdPremult[i]));
  }
}

void cceScreenPreMult(uint32 count,
                      const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                      const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                      COLORVALUE* result)
{
  uint32 i;

  UNUSED_PARAM(COLORVALUE, bdAlpha) ;
  UNUSED_PARAM(COLORVALUE, srcAlpha) ;

  for (i = 0; i < count; i ++) {
    result[i] = (COLORVALUE)(srcPremult[i] + bdPremult[i] -
                             Multiply(srcPremult[i], bdPremult[i]));
  }
}

/* --'Overlay' blend mode variants-- */

static COLORVALUE overlay(COLORVALUE src, COLORVALUE srcPremult,
                          COLORVALUE srcAlpha,
                          COLORVALUE bdPremult, COLORVALUE bdAlpha)
{
  COLORVALUE bd = (COLORVALUE)Divide(bdPremult, bdAlpha);
  if (bd > COLORVALUE_HALF) {
    return (COLORVALUE)(bdPremult + srcPremult -
                        Multiply(srcAlpha, bdAlpha) +
                        Multiply(bdAlpha, srcPremult) +
                        Multiply(srcAlpha, bdPremult) -
                        (2 * Multiply(srcPremult, bdPremult)));
  }
  else {
    uint32 blendResult = 2 * Multiply(src, bd);
    return TranCalcFull(srcAlpha, bdAlpha, srcPremult, bdPremult, blendResult);
  }
}

void cceOverlay(uint32 count,
                const COLORVALUE *src, COLORVALUE srcAlpha,
                const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = overlay(src[i], srcPremult, srcAlpha, bdPremult[i], bdAlpha);
  }
}

void cceOverlayPreMult(uint32 count,
                       const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                       const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                       COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = overlay(src, srcPremult[i], srcAlpha, bdPremult[i], bdAlpha);
  }
}

/* --'SoftLight' blend mode variants-- */

static COLORVALUE blendSoftLight(COLORVALUE srcCv, COLORVALUE bdCv)
{
  float src = COLORVALUE_TO_USERVALUE(srcCv);
  float bd = COLORVALUE_TO_USERVALUE(bdCv);
  float result;

  if (src <= 0.5) {
    result = bd - (((1 - (2 * src)) * bd) * (1 - bd));
  }
  else {
    float d;
    if (bd <= 0.25) {
      d = ((((16 * bd) - 12) * bd) + 4) * bd;
    }
    else {
      d = (float)sqrt(bd);
    }

    result = bd + (((2 * src) - 1) * (d - bd));
  }
  return FLOAT_TO_COLORVALUE(result);
}

void cceSoftLight(uint32 count,
                  const COLORVALUE *src, COLORVALUE srcAlpha,
                  const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                  COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    COLORVALUE bd = Divide(bdPremult[i], bdAlpha);
    COLORVALUE blendResult = blendSoftLight(src[i], bd);
    result[i] = TranCalcFull(srcAlpha, bdAlpha, srcPremult, bdPremult[i],
                             blendResult);
  }
}

void cceSoftLightPreMult(uint32 count,
                         const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                         const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                         COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], bdAlpha);
    COLORVALUE bd = Divide(bdPremult[i], bdAlpha);
    COLORVALUE blendResult = blendSoftLight(src, bd);

    result[i] = TranCalcFull(srcAlpha, bdAlpha, srcPremult[i], bdPremult[i],
                             blendResult);
  }
}

/* --'HardLight' blend mode variants-- */

/* Blend function for HardLight. */
static COLORVALUE hardLightBlendFunction(COLORVALUE srcColor,
                                         COLORVALUE bdColor)
{
  if (srcColor <= COLORVALUE_HALF)
    return multiplyBlendFunction(srcColor * 2, bdColor);
  else
    return screenBlendFunction(srcColor * 2 - COLORVALUE_ONE, bdColor);
}

void cceHardLight(uint32 count,
                  const COLORVALUE *src, COLORVALUE srcAlpha,
                  const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                  COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    COLORVALUE bd = Divide(bdPremult[i], bdAlpha);
    result[i] = TranCalcFull(srcAlpha, bdAlpha, srcPremult, bdPremult[i],
                             hardLightBlendFunction(src[i], bd));
  }
}

void cceHardLightPreMult(uint32 count,
                         const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                         const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                         COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    COLORVALUE bd = Divide(bdPremult[i], bdAlpha);
    result[i] = TranCalcFull(srcAlpha, bdAlpha, srcPremult[i], bdPremult[i],
                             hardLightBlendFunction(src, bd));
  }
}

/* --'ColorDodge' blend mode variants-- */

static COLORVALUE colorDodge(COLORVALUE src, COLORVALUE srcPremult,
                             COLORVALUE srcAlpha,
                             COLORVALUE bdPremult, COLORVALUE bdAlpha)
{
  /* Note that it's ok to check the pre-multiplied value for zero at this
   * point - if either the color or the alpha is zero, the result of the
   * blend function term will be zero. */
  if (bdPremult == 0) {
    /* The blend function outputs zero (and thus the last product in the
     * transparency calculation disappears) */
    return TranCalcPart(srcAlpha, bdAlpha, srcPremult, bdPremult);
  }
  else {
    uint32 blendResult;
    uint32 bd = Divide(bdPremult, bdAlpha);
    uint32 omSrc = COLORVALUE_ONE - src;

    if (bd >= omSrc)
      blendResult = COLORVALUE_ONE;
    else
      blendResult = Divide(bd, omSrc);

    return TranCalcFull(srcAlpha, bdAlpha, srcPremult, bdPremult, blendResult);
  }
}

void cceColorDodge(uint32 count,
                   const COLORVALUE *src, COLORVALUE srcAlpha,
                   const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                   COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = colorDodge(src[i], srcPremult, srcAlpha, bdPremult[i], bdAlpha);
  }
}

void cceColorDodgePreMult(uint32 count,
                          const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                          const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                          COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = colorDodge(src, srcPremult[i], srcAlpha, bdPremult[i], bdAlpha);
  }
}

/* --'ColorBurn' blend mode variants-- */

static COLORVALUE colorBurn(COLORVALUE src, COLORVALUE srcPremult,
                            COLORVALUE srcAlpha,
                            COLORVALUE bdPremult, COLORVALUE bdAlpha)
{
  uint32 blendResult;
  uint32 bd = Divide(bdPremult, bdAlpha);

  if (bd == COLORVALUE_ONE) {
    blendResult = COLORVALUE_ONE;
  }
  else {
    int32 omBd = COLORVALUE_ONE - bd;
    if (omBd >= src)
      blendResult = 0;
    else
      blendResult = COLORVALUE_ONE - Divide(omBd, src);
  }

  return TranCalcFull(srcAlpha, bdAlpha, srcPremult, bdPremult, blendResult);
}

void cceColorBurn(uint32 count,
                  const COLORVALUE *src, COLORVALUE srcAlpha,
                  const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                  COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = colorBurn(src[i], srcPremult, srcAlpha, bdPremult[i], bdAlpha);
  }
}

void cceColorBurnPreMult(uint32 count,
                         const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                         const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                         COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = colorBurn(src, srcPremult[i], srcAlpha, bdPremult[i], bdAlpha);
  }
}

/* --'Darken' blend mode variants-- */

static COLORVALUE darken(COLORVALUE src, COLORVALUE srcPremult,
                         COLORVALUE srcAlpha,
                         COLORVALUE bdPremult, COLORVALUE bdAlpha)
{
  uint32 blendResult;
  uint32 bd = Divide(bdPremult, bdAlpha);

  if (bd <= src)
    blendResult = Multiply(bdPremult, srcAlpha);
  else
    blendResult = Multiply(srcPremult, bdAlpha);

  return (COLORVALUE)(TranCalcPart(srcAlpha, bdAlpha, srcPremult, bdPremult) +
                      blendResult);
}

void cceDarken(uint32 count,
               const COLORVALUE *src, COLORVALUE srcAlpha,
               const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
               COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = darken(src[i], srcPremult, srcAlpha, bdPremult[i], bdAlpha);
  }
}

void cceDarkenPreMult(uint32 count,
                      const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                      const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                      COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = darken(src, srcPremult[i], srcAlpha, bdPremult[i], bdAlpha);
  }
}

/* --'Lighten' blend mode variants-- */

static COLORVALUE lighten(COLORVALUE src, COLORVALUE srcPremult,
                          COLORVALUE srcAlpha,
                          COLORVALUE bdPremult, COLORVALUE bdAlpha)
{
  uint32 blendResult;
  uint32 bd = Divide(bdPremult, bdAlpha);

  if (bd > src)
    blendResult = Multiply(bdPremult, srcAlpha);
  else
    blendResult = Multiply(srcPremult, bdAlpha);

  return (COLORVALUE)(TranCalcPart(srcAlpha, bdAlpha, srcPremult, bdPremult) +
                      blendResult);
}

void cceLighten(uint32 count,
                const COLORVALUE *src, COLORVALUE srcAlpha,
                const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = lighten(src[i], srcPremult, srcAlpha, bdPremult[i], bdAlpha);
  }
}

void cceLightenPreMult(uint32 count,
                       const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                       const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                       COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = lighten(src, srcPremult[i], srcAlpha, bdPremult[i], bdAlpha);
  }
}

/* --'Difference' blend mode variants-- */

static COLORVALUE difference(COLORVALUE src, COLORVALUE srcPremult,
                             COLORVALUE srcAlpha,
                             COLORVALUE bdPremult, COLORVALUE bdAlpha)
{
  uint32 bd = Divide(bdPremult, bdAlpha);

  if (bd > src)
    return (COLORVALUE)(bdPremult + srcPremult -
                        (2 * Multiply(bdAlpha, srcPremult)));
  else
    return (COLORVALUE)(bdPremult + srcPremult -
                        (2 * Multiply(srcAlpha, bdPremult)));
}

void cceDifference(uint32 count,
                   const COLORVALUE *src, COLORVALUE srcAlpha,
                   const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                   COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = difference(src[i], srcPremult, srcAlpha, bdPremult[i], bdAlpha);
  }
}

void cceDifferencePreMult(uint32 count,
                          const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                          const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                          COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = difference(src, srcPremult[i], srcAlpha, bdPremult[i], bdAlpha);
  }
}

/* --'Exclusion' blend mode variants-- */

void cceExclusion(uint32 count,
                  const COLORVALUE *src, COLORVALUE srcAlpha,
                  const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                  COLORVALUE* result)
{
  uint32 i;

  UNUSED_PARAM(COLORVALUE, bdAlpha) ;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = (COLORVALUE)(srcPremult + bdPremult[i] -
                             (2 * Multiply(srcPremult, bdPremult[i])));
  }
}

void cceExclusionPreMult(uint32 count,
                         const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                         const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                         COLORVALUE* result)
{
  uint32 i;

  UNUSED_PARAM(COLORVALUE, bdAlpha) ;
  UNUSED_PARAM(COLORVALUE, srcAlpha) ;

  for (i = 0; i < count; i ++) {
    result[i] = (COLORVALUE)(srcPremult[i] + bdPremult[i] -
                             (2 * Multiply(srcPremult[i], bdPremult[i])));
  }
}

/* --'CompatibleOverprint' blend mode variants-- */
static COLORVALUE compatibleOverprint(COLORVALUE src, COLORVALUE srcPremult,
                                      COLORVALUE srcAlpha,
                                      COLORVALUE bdPremult, COLORVALUE bdAlpha,
                                      blit_channel_state_t op)
{
  if (op & blit_channel_maxblit) {
    /* Max-blt - choose the darkest color. Color values are additive. */
    COLORVALUE bd = Divide(bdPremult, bdAlpha);
    op = (src < bd) ? blit_channel_present : blit_channel_missing;
  }

  if (op == blit_channel_missing) {
    return (COLORVALUE)(Multiply(COLORVALUE_ONE - bdAlpha, srcPremult) +
                        bdPremult);
  }
  else {
    return (COLORVALUE)(Multiply(COLORVALUE_ONE - srcAlpha, bdPremult) +
                        srcPremult);
  }
}

void cceCompatibleOverprint(const blit_channel_state_t *opFlags,
                            uint32 count,
                            const COLORVALUE *src, COLORVALUE srcAlpha,
                            const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                            COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE srcPremult = Multiply(src[i], srcAlpha);
    result[i] = compatibleOverprint(src[i], srcPremult, srcAlpha, bdPremult[i],
                                    bdAlpha, opFlags[i]);
  }
}

void cceCompatibleOverprintPreMult(const blit_channel_state_t *opFlags,
                                   uint32 count,
                                   const COLORVALUE *srcPremult, COLORVALUE srcAlpha,
                                   const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                                   COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; i ++) {
    COLORVALUE src = Divide(srcPremult[i], srcAlpha);
    result[i] = compatibleOverprint(src, srcPremult[i], srcAlpha, bdPremult[i],
                                    bdAlpha, opFlags[i]);
  }
}

/* When there's no transparency involved, overprinting follows the standard
   opaque painting model. */
void cceOpaqueOverprint(const blit_channel_state_t *opFlags, uint32 count,
                        const COLORVALUE *src, const COLORVALUE *bd, COLORVALUE* result)
{
  uint32 i;

  for (i = 0; i < count; ++i) {
    if (opFlags[i] & blit_channel_present ) {
      if ( opFlags[i] & blit_channel_maxblit ) {
        /* Max-blt - choose the darkest color (color values are additive). */
        if (src[i] > bd[i])
          result[i] = bd[i];
        else
          result[i] = src[i];
      } else {
        result[i] = src[i]; /* Normal opaque paint. */
      }
    } else {
      result[i] = bd[i]; /* Overprint. */
    }
  }
}

/* Weighted average functions for color and alpha. These are used to handle
   the fractional shape part of the compositing formula. */
void cceWeightedAverage(uint32 count, const COLORVALUE *src,
                        const COLORVALUE *bd, COLORVALUE bdAlpha,
                        COLORVALUE shape, COLORVALUE *result)
{
  uint32 i;

  /* If bdAlpha is zero then bd is undefined, but the term drops out anyway. */
  if ( bdAlpha == COLORVALUE_ZERO ) {
    for ( i = 0; i < count; ++i ) {
      result[i] = Multiply(shape, src[i]);
    }
  } else {
    for ( i = 0; i < count; ++i ) {
      result[i] = Multiply(COLORVALUE_ONE - shape, bd[i]) +
                  Multiply(shape, src[i]);
    }
  }
}

void cceWeightedAverageAlpha(COLORVALUE src, COLORVALUE bd,
                             COLORVALUE shape, COLORVALUE *result)
{
  *result = Multiply(COLORVALUE_ONE - shape, bd) + Multiply(shape, src);
}

/* See header for doc. */
void cceRemoveBackdropContribution(uint32 count,
                                   const COLORVALUE *src, COLORVALUE srcAlpha,
                                   const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                                   COLORVALUE* result)
{
  /* src      - Cn
     srcAlpha - &gn
     bdPremult- Co * &o
     bd       - Co
     bdAlpha  - &o
     result   - C
     C = Cn + (Cn - Co)((&o/&gn) - &o) [PDF 1.4 7.3.7] */

  if (bdAlpha == COLORVALUE_ZERO || srcAlpha == COLORVALUE_ZERO || srcAlpha == COLORVALUE_ONE ) {
    /* RHS of equation reduced to zero.
       (note, 0/0 is defined as 0 in Adobe-land) */
    uint32 i;
    for (i = 0; i < count; ++i)
      result[i] = src[i];
  } else if ( bdAlpha == COLORVALUE_ONE ) {
    /* C = Cn + (Cn - C0) * (1 / &gn - 1)
         = Cn + (Cn - C0) / &gn - (Cn - C0)
         = C0 + (Cn - C0) / &gn
    */
    uint32 i ;
    for ( i = 0 ; i < count ; ++i ) {
      uint32 C0 = bdPremult[i] ; /* bd is the same as bdPremult because bdAlpha == 1 */
      uint32 Cn = src[i] ;
      uint32 C = C0 ;
      if ( Cn > C0 ) { /* C0 + (Cn - C0) / &gn */
        C += (Cn - C0) * COLORVALUE_ONE / srcAlpha ; /* 0 <= C <= COLORVALUE_ONE^2 + COLORVALUE_ONE */
        if ( C > COLORVALUE_ONE )
          C = COLORVALUE_ONE ;
      } else if ( C0 > Cn ) { /* C0 - (C0 - Cn) / &gn */
        Cn = (C0 - Cn) * COLORVALUE_ONE / srcAlpha ;
        if ( Cn > C ) /* Avoid an extra branch by subtracting C to zero */
          Cn = C ;
        C -= Cn ;
      } /* else C0 == Cn */
      HQASSERT(C <= COLORVALUE_ONE, "Reduced value out of range") ;
      result[i] = (COLORVALUE)C ;
    }
  } else {
    /* A little bit of algebra on the equations makes fixed point
       calculation more amenable, and faster too. */
    uint32 i ;
    for ( i = 0 ; i < count ; ++i ) {
      uint32 C0 = Divide(bdPremult[i], bdAlpha) ;
      uint32 Cn = src[i] ;
      uint32 C = Cn ;
      if ( Cn > C0 ) { /* Cn + (Cn - C0) * &o * (1 / &gn - 1) */
        Cn = (Cn - C0) * bdAlpha ; /* 0 <= Cn <= COLORVALUE_ONE^2 */
        Cn = Cn / srcAlpha - Cn / COLORVALUE_ONE ;
        C += Cn ;
        if ( C > COLORVALUE_ONE )
          C = COLORVALUE_ONE ;
      } else if ( C0 > Cn ) { /* Cn - (C0 - Cn) * &o * (1 / &gn - 1) */
        Cn = (C0 - Cn) * bdAlpha ; /* 0 <= Cn <= COLORVALUE_ONE^2 */
        Cn = Cn / srcAlpha - Cn / COLORVALUE_ONE ;
        if ( Cn > C ) /* Avoid an extra branch by subtracting C to zero */
          Cn = C ;
        C -= Cn ;
      } /* else C0 == Cn */
      HQASSERT(C <= COLORVALUE_ONE, "Reduced value out of range") ;
      result[i] = (COLORVALUE)C ;
    }
  }
}

/* Log stripped */
