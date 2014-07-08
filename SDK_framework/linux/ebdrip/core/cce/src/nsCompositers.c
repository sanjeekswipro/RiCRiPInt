/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!src:nsCompositers.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Non-separable blend mode compositers.
 */

#include "core.h"
#include "compositers.h"
#include "compositeMacros.h"

/* Calculate min and max together to reduce testing */
#define TriMinMax(a_, b_, c_, min_, max_) MACRO_START \
  if ( (a_) > (b_) ) { \
    if ( (b_) > (c_) ) { \
      (max_) = (a_) ; \
      (min_) = (c_) ; \
    } else { \
      (min_) = (b_) ; \
      if ( (a_) > (c_) ) \
        (max_) = (a_) ; \
      else \
        (max_) = (c_) ; \
    } \
  } else if ( (b_) < (c_) ) { \
    (max_) = (c_) ; \
    (min_) = (a_) ; \
  } else { \
    (max_) = (b_) ; \
    if ( (a_) < (c_) ) \
      (min_) = (a_) ; \
    else \
      (min_) = (c_) ; \
  } \
MACRO_END

/**
 * Adobe defined function.
 */
static float lum(float* c)
{
  return (c[0] * 0.3f) + (c[1] * 0.59f) + (c[2] * 0.11f);
}

/**
 * Anything smaller than this is so close to zero that it is likely
 * to blow-up the CCE maths.
 */
#define CCE_EPSILON (0.00001)

/**
 * Adobe defined function.
 */
static void clipColor(float* c)
{
  float l = lum(c);
  float min, max, divisor;
  int32 i;

  TriMinMax(c[0], c[1], c[2], min, max);
  /*
   * l = weighted-average(r,g,b)
   * min = min(r,g,b)
   * max = max(r,g,b)
   *
   * So we should have
   *   min <= l <= max
   *
   * But floating pointing inaccuracies could upset this relationship. So use
   * "divisor > CCE_EPSILON" test. This will catch both the case of "l == min"
   * where we could get a division by zero error, and "l < min" caused by
   * floating-point error.
   */

  if (min < 0.0) {
    divisor = l - min;
    if ( divisor > CCE_EPSILON ) {
      for (i = 0; i < 3; i ++) {
        c[i] = l + (((c[i] - l) * l) / divisor);
      }
    } else {
      c[0] = c[1] = c[2] = 0.0;
    }
  }

  if (max > 1.0) {
    divisor = max - l;
    if ( divisor > CCE_EPSILON ) {
      for (i = 0; i < 3; i ++) {
        c[i] = l + (((c[i] - l) * (1 - l)) / divisor);
      }
    } else {
      c[0] = c[1] = c[2] = 1.0;
    }
  }
}

/**
 * Adobe defined function.
 */
static float sat(float* c)
{
  float min, max;

  TriMinMax(c[0], c[1], c[2], min, max);
  return max - min;
}

/**
 * Adobe defined function. Note that c and result may be equal.
 */
static void setLum(float* c, float l, float* result)
{
  float d = l - lum(c);
  result[0] = c[0] + d;
  result[1] = c[1] + d;
  result[2] = c[2] + d;
  clipColor(result);
}

/**
 * Adobe defined function. Note that c and result may be equal.
 */
static void setSat(float* c, float s, float* result)
{
  int32 m[3];

  /* Get the min, mid and max color value indices into m[0], m[1] and m[2]
   * respectively. */
  if (c[0] < c[1]) {
    m[0] = 0;
    m[2] = 1;
  }
  else {
    m[0] = 1;
    m[2] = 0;
  }

  if (c[2] > c[m[2]]) {
    m[1] = m[2];
    m[2] = 2;
  }
  else if (c[2] < c[m[0]]) {
    m[1] = m[0];
    m[0] = 2;
  }
  else {
    m[1] = 2;
  }

  /* Now the main body of Adobe SetSat.
   * This threshold isn't in the spec, but it is what Acrobat X does.
   */
#define NEUTRAL_THRESHOLD   (0.5f/255.0f)
#define TAPER_LIMIT         (1.25f/255.0f)
  if (c[m[2]] - c[m[0]] > NEUTRAL_THRESHOLD) {
    result[m[1]] = ((c[m[1]] - c[m[0]]) * s) / (c[m[2]] - c[m[0]]);
    result[m[2]] = s;

    /* Taper the result to zero for near neutral colours. This isn't in the spec
     * but is needed to pass the Altona 2 test job.
     */
    if (c[m[2]] - c[m[0]] < TAPER_LIMIT) {
        result[m[1]] *= (c[m[2]] - c[m[0]] - NEUTRAL_THRESHOLD) / (TAPER_LIMIT - NEUTRAL_THRESHOLD);
        result[m[2]] *= (c[m[2]] - c[m[0]] - NEUTRAL_THRESHOLD) / (TAPER_LIMIT - NEUTRAL_THRESHOLD);
    }
  }
  else {
    result[m[1]] = result[m[2]] = 0;
  }

  result[m[0]] = 0;
}

static void hueBlend(float* src, float* bd, float* result)
{
  setSat(src, sat(bd), result);
  setLum(result, lum(bd), result);
}

static void saturationBlend(float* src, float* bd, float* result)
{
  setSat(bd, sat(src), result);
  setLum(result, lum(bd), result);
}

static void colorBlend(float* src, float* bd, float* result)
{
  setLum(src, lum(bd), result);
}

static void luminosityBlend(float* src, float* bd, float* result)
{
  setLum(bd, lum(src), result);
}

/**
 * If the blend space is gray, only the luminosity blend mode would have any
 * effect, and that would be a straight copy, thus we handle gray non-separable
 * blends specially.
 */
static void grayBlend(const COLORVALUE *src,
                      const COLORVALUE *bd,
                      COLORVALUE* result,
                      CCEBlendMode mode)
{
  switch (mode) {
  case CCEModeHue:
  case CCEModeSaturation:
  case CCEModeColor:
    /* Luminosity comes from backdrop. */
    result[0] = bd[0];
    return;

  case CCEModeLuminosity:
    /* Luminosity comes from source. */
    result[0] = src[0];
    return;
  }
}

/**
 * Perform the blend calculation.
 *
 * @param count One of 1, 3, or 4, indicating Gray, RGB or CMYK respectively.
 */
static void nonSeparableBlend(uint32 count,
                              const COLORVALUE *src,
                              const COLORVALUE *bd,
                              COLORVALUE* result,
                              CCEBlendMode mode)
{
  float srcRgb[3];
  float bdRgb[3];
  float resultRgb[4];
  uint32 i;
  Bool cmyk = FALSE;

  switch (count) {
  default:
    HQFAIL("Unsupported number of colorants.");
    for (i = 0; i < count; i ++) {
      result[i] = src[i];
    }
    return;

  case 1:
    grayBlend(src, bd, result, mode);
    return;

  case 4:
    cmyk = TRUE;
    /* Note that all color values are additive, so no need to invert CMY.
     * Fall-through */
  case 3:
    for (i = 0; i < 3; i ++) {
      srcRgb[i] = COLORVALUE_TO_USERVALUE(src[i]);
      bdRgb[i] = COLORVALUE_TO_USERVALUE(bd[i]);
    }
    break;
  }

  switch (mode) {
  case CCEModeHue:
    hueBlend(srcRgb, bdRgb, resultRgb);
    if (cmyk) {
      result[3] = bd[3];
    }
    break;

  case CCEModeSaturation:
    saturationBlend(srcRgb, bdRgb, resultRgb);
    if (cmyk) {
      result[3] = bd[3];
    }
    break;

  case CCEModeColor:
    colorBlend(srcRgb, bdRgb, resultRgb);
    if (cmyk) {
      result[3] = bd[3];
    }
    break;

  case CCEModeLuminosity:
    luminosityBlend(srcRgb, bdRgb, resultRgb);
    if (cmyk) {
      result[3] = src[3];
    }
    break;

  default:
    HQFAIL("nonSeparableBlend - mode not a non-separable blend mode.");
    break;
  }

  for (i = 0; i < 3; i ++) {
    result[i] = FLOAT_TO_COLORVALUE(resultRgb[i]);
  }
}

/**
 * Wrapper function for non-separable blend modes, where the source color
 * IS NOT premultiplied by alpha.
 */
static void nonSeparable(uint32 count,
                         const COLORVALUE *src, COLORVALUE srcAlpha,
                         const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                         COLORVALUE* result, CCEBlendMode mode)
{
  uint32 i;
  COLORVALUE bd[4], temp[4];

  HQASSERT(count <= 4, "Invalid number of colorants for non-separable blend.");

  /* Writing the result straight into the same array as bdPremult
     and therefore we need to buffer bdPremult for TranCalcFull. */
  if ( bdPremult == result ) {
    for ( i = 0; i < count; ++i ) {
      temp[i] = bdPremult[i];
    }
    bdPremult = temp;
  }

  /* Remove the alpha from the backdrop (only it is premultiplied). */
  cceDivideAlpha(count, bdPremult, bdAlpha, bd);

  /* Perform the blend. */
  nonSeparableBlend(count, src, bd, result, mode);

  /* Composite the colors to get the result (right now 'result' only contains
  the result of the blend function). Note that TranCalcFull() requires both
  source and backdrop colors to be premultiplied by their alpha. */
  for (i = 0; i < count; i ++) {
    uint32 srcPremult = Multiply(src[i], srcAlpha);
    result[i] = TranCalcFull(srcAlpha, bdAlpha, srcPremult, bdPremult[i],
                             result[i]);
  }
}

/**
 * Wrapper function for non-separable blend modes, where the source color
 * IS premultiplied by alpha.
 */
static void nonSeparablePreMult(uint32 count,
                                const COLORVALUE *srcPremult,
                                COLORVALUE srcAlpha,
                                const COLORVALUE *bdPremult,
                                COLORVALUE bdAlpha,
                                COLORVALUE* result, CCEBlendMode mode)
{
  uint32 i;
  COLORVALUE src[4], bd[4];

  HQASSERT(count <= 4, "Invalid number of colorants for non-separable blend.");

  /* Remove the alpha. */
  cceDivideAlpha(count, srcPremult, srcAlpha, src);
  cceDivideAlpha(count, bdPremult, bdAlpha, bd);

  /* Perform the blend. */
  nonSeparableBlend(count, src, bd, result, mode);

  /* Composite the colors to get the result (right now 'result' only contains
  the result of the blend function). */
  for (i = 0; i < count; i ++) {
    result[i] = TranCalcFull(srcAlpha, bdAlpha, srcPremult[i], bdPremult[i],
                             result[i]);
  }
}

/* --'Hue' blend mode variants-- */

void cceHue(CCE_ARGS)
{
  nonSeparable(count, src, srcAlpha, bd, bdAlpha, result, CCEModeHue);
}

void cceHuePreMult(CCE_ARGS)
{
  nonSeparablePreMult(count, src, srcAlpha, bd, bdAlpha, result,
                      CCEModeHue);
}

/* --'Saturation' blend mode variants-- */

void cceSaturation(CCE_ARGS)
{
  nonSeparable(count, src, srcAlpha, bd, bdAlpha, result,
               CCEModeSaturation);
}

void cceSaturationPreMult(CCE_ARGS)
{
  nonSeparablePreMult(count, src, srcAlpha, bd, bdAlpha, result,
                      CCEModeSaturation);
}

/* --'Color' blend mode variants-- */

void cceColor(CCE_ARGS)
{
  nonSeparable(count, src, srcAlpha, bd, bdAlpha, result, CCEModeColor);
}

void cceColorPreMult(CCE_ARGS)
{
  nonSeparablePreMult(count, src, srcAlpha, bd, bdAlpha, result,
                      CCEModeColor);
}

/* --'Luminosity' blend mode variants-- */

void cceLuminosity(CCE_ARGS)
{
  nonSeparable(count, src, srcAlpha, bd, bdAlpha, result,
               CCEModeLuminosity);
}

void cceLuminosityPreMult(CCE_ARGS)
{
  nonSeparablePreMult(count, src, srcAlpha, bd, bdAlpha, result,
                      CCEModeLuminosity);
}

/* Log stripped */
