/** \file
 * \ingroup halftone
 *
 * $HopeName: SWv20!export:color.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface containing color operations that depend on spot number.
 */

#ifndef __HTCOLOR_H__
#define __HTCOLOR_H__ 1

#include "graphict.h" /* GUCR_RASTERSTYLE */
#include "gschcms.h" /* REPRO_TYPE_ */


/*
 * Tint state typedef - for added compiler checking
 */
typedef int HT_TINT_STATE;

/*
 * Color tint states, a solid, clear, or some shade in between
 */
enum HT_TINT_STATE { HT_TINT_SOLID, HT_TINT_CLEAR, HT_TINT_SHADE };

/**
 * Find color state for colorant
 */
HT_TINT_STATE ht_getColorState(
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci,
  COLORVALUE        color,
  GUCR_RASTERSTYLE *hRasterStyle);

/** Direct test for color being clear. */
#define ht_colorIsClear(s, t, i, c, rs) (ht_getColorState(s, t, i, c, rs) == HT_TINT_CLEAR)

/** Direct test for color being solid. */
#define ht_colorIsSolid(s, t, i, c, rs) (ht_getColorState(s, t, i, c, rs) == HT_TINT_SOLID)


/** Get the clear value for the given screen (or raster/colorant if contone). */
COLORVALUE ht_getClear(
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci,
  GUCR_RASTERSTYLE *hRasterStyle);

/** Get the clear value for the given screen (assuming screened output). */
COLORVALUE ht_getClearScreen(
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci);


/**
 * Detect if one or more colorants use more than 256 levels
 */
Bool ht_is16bitLevels(
  SPOTNO            spotno,
  HTTYPE            type,
  size_t            count,
  COLORANTINDEX     ci[],
  GUCR_RASTERSTYLE *hRasterStyle);


/**
 * Structure for use as communication between ht_setupTransforms() and
 * ht_doTransforms(). It is defined here because clients are expected to
 * allocate an array of them, clients should not attempt to acess fields
 * within the structure.
 */
typedef struct HT_TRANSFORM_INFO {
  int8           transform;
  COLORVALUE     colorClear;
} HT_TRANSFORM_INFO;


/**
 * Take a set of colors and colorants and apply the transform from \c COLORVALUE
 * to halftone levels or contone color range.
 *
 * This is the 'simple' version, see \c ht_setupTransforms for the fast one.
 */
void ht_applyTransform(
  SPOTNO              spotno,
  HTTYPE              type,
  size_t              count,
  const COLORANTINDEX ci[],
  const COLORVALUE    icolors[],
  COLORVALUE          ocolors[],
  Bool                halftoning,
  int32               levels);


/**
 * This function derives the transform info required for converting color
 * values to device codes. This is stored in transformInfo[] which is an array
 * of the same size as aci[] which will be passed as a param to
 * ht_doTransforms().
 */
void ht_setupTransforms(
  SPOTNO              spotno,
  HTTYPE              type,
  size_t              count,
  const COLORANTINDEX ci[],
  GUCR_RASTERSTYLE    *hRasterStyle,
  HT_TRANSFORM_INFO   transformInfo[]);

/** Same as \c ht_setupTransforms, but not using a rasterstyle. */
void ht_setupTransformInfos(
  SPOTNO              spotno,
  HTTYPE              type,
  size_t              ncomps,
  const COLORANTINDEX aci[],
  Bool                halftoning,
  int32               levels,
  HT_TRANSFORM_INFO   transformInfo[]);


/**
 * This function is an optimised version of ht_applyTransform(). It requires
 * the transformInfo[] which is obtained from ht_setupTransforms().
 * The transformInfo[] must be pertinent to the required spotno and the set of
 * colorants.
 */
void ht_doTransforms(
  size_t                  count,
  const COLORVALUE        icolors[],
  const HT_TRANSFORM_INFO transformInfo[],
  COLORVALUE              ocolors[]);

/** Do nsamples ht_doTransforms() all in one go to help with performance.
 */
void ht_doTransformsMultiAll(
  size_t                  ncomps,
  const COLORVALUE        icolors[],
  const HT_TRANSFORM_INFO transformInfo[],
  COLORVALUE              ocolors[],
  size_t                  nsamples);

/**
 * Similar to ht_doTransforms, but optimised to handle multiple transforms on
 * the same colorant index.  A stride argument for icolors and ocolors is used
 * between each transform.  This function does not handle COLORVALUE_TRANSPARENT
 * and therefore if that value is possible ht_doTransforms should be used
 * instead.
 */
void ht_doTransformsMulti(
  size_t                  count,
  const COLORVALUE        icolors[],
  unsigned int            istride,
  const HT_TRANSFORM_INFO transformInfo,
  COLORVALUE              ocolors[],
  unsigned int            ostride);

/**
 * As ht_doTransformsMulti, but input color values are 8 bits.
 */
void ht_doTransformsMulti8(
  size_t                  count,
  const uint8             icolors[],
  unsigned int            istride,
  const HT_TRANSFORM_INFO transformInfo,
  COLORVALUE              ocolors[],
  unsigned int            ostride);

#endif /* protection for multiple inclusion */

/* Log stripped */
