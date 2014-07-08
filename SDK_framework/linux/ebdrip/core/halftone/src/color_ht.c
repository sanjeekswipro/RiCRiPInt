/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:color_ht.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color operations (color.h) that use the halftone cache.
 *
 * If we create a RIP without halftoning, there'll be a version of this
 * without cache access.
 */

#include "core.h"
#include "color.h"

#include "htpriv.h"
#include "hqassert.h"
#include "graphict.h" /* GUCR_RASTERSTYLE */
#include "gu_htm.h" /* MODHTONE_REF */
#include "gu_chan.h" /* gucr_valuesPerComponent */
#include "dlstate.h" /* xdpi */


/*
 * Function:    ht_getColorState
 *
 * Purpose:     Check for color being solid, clear, or a shade
 *
 * Returns:     HT_TINT_SOLID if color is 0, HT_TINT_CLEAR if color has its
 *              max value, else HT_TINT_SHADE
 *
 * Notes:       Could change to macro with test for trivial case and otherwise call.
 */
HT_TINT_STATE ht_getColorState(
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci,
  COLORVALUE        color,
  GUCR_RASTERSTYLE *hRasterStyle)
{
  HT_TINT_STATE     state;
  COLORVALUE        icolor ;
  HT_TRANSFORM_INFO transformInfo;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(HT_CI_VALID(ci), "invalid colorant index");

  /* Quantise to halftone levels to decide this colour's state. */
  ht_setupTransforms(spotno, type, 1, &ci, hRasterStyle, &transformInfo);
  ht_doTransforms(1, &color, &transformInfo, &icolor);

  /* Treat -ve colors as 0, to prevent renderer crashes */
  if ( icolor <= (COLORVALUE)0 ) {
    /* Trivial case - color is a solid */
    state = HT_TINT_SOLID;

  } else { /* Check for clear halftone */
    state = (icolor >= transformInfo.colorClear)
              ? HT_TINT_CLEAR
              : HT_TINT_SHADE;
  }
  return state ;
} /* Function ht_getColorState */


/* Get color for clear tint, when screening */
inline COLORVALUE ht_getClearScreen(
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci)
{
  const MODHTONE_REF *mhtone;

  mhtone = ht_getModularHalftoneRef(spotno, type, ci);
  if (mhtone != NULL)
    return htm_SrcBitDepth(mhtone) == 8 ? 255 : COLORVALUE_MAX;
  else {
    CHALFTONE* pch = ht_getch(spotno, type, ci);
    return CAST_TO_COLORVALUE((pch != NULL) ? ht_chNoTones(pch) : 1);
  }
}


/* Get color for clear tint */
COLORVALUE ht_getClear(
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci,
  GUCR_RASTERSTYLE *hRasterStyle)
{
  HQASSERT(spotno > 0, "ht_getClear: invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(HT_CI_VALID(ci), "invalid colorant index");

  if ( gucr_halftoning(hRasterStyle) )
    return ht_getClearScreen(spotno, type, ci);
  else
    return CAST_TO_COLORVALUE(gucr_valuesPerComponent( hRasterStyle ) - 1);
}


/*
 * Function:    ht_is16bitLevels
 *
 * Purpose:     To check for one or more colorants having more levels than
 *              can be indexed by a byte.
 *
 * Arguments:   spotno    - spot number
 *              count     - number of colorants to check
 *              ci        - array of colorant indexes
 *
 * Returns:     TRUE if any colorant has more than 256 levels, else FALSE
 */
Bool ht_is16bitLevels(
  SPOTNO            spotno,
  HTTYPE            type,
  size_t            count,
  COLORANTINDEX     ci[],
  GUCR_RASTERSTYLE *hRasterStyle)
{
  size_t i;
  Bool        f16bit = FALSE;
  COLORVALUE  clear;

  HQASSERT((spotno > 0),
           "ht_is16bitLevels: invalid spot number");
  HQASSERT((ci != NULL),
           "ht_is16bitLevels: NULL pointer to colorant indexes");

  for ( i = 0; (!f16bit) && (i < count); i++ ) {
    /* Loop over colorants finding finding max clear value */
    clear = ht_getClear(spotno, type, ci[i], hRasterStyle);
    f16bit = clear >= 256;
  }
  return f16bit ;
} /* Function ht_is16bitLevels */


/* Methods for converting from colorvalues to device codes */
enum {
  CT_INVALID = -1,
  CT_NORMAL = 0,
  CT_ADJUST,
  CT_PATTERN,
  CT_CONTONE,
  CT_DEGENERATE
} ;


/*
 * Determine whether to use CT_ADJUST or CT_NORMAL
 */
static inline Bool ht_use_CT_NORMAL(uint16 notones, Bool accurate)
{
  DL_STATE *page = CoreContext.page ;
  return (notones > 255 || accurate || page->xdpi >= 1000 || page->ydpi >= 1000)
         && notones > 2 ;
}


/*
 * Function:    ht_applyTransform
 *
 * Purpose:     To apply the ct transform for a set of colorants
 *
 * Arguments:   spotno      - spot number
 *              type        - object type
 *              ncomps      - number of colorants to transform
 *              aci         - array of colorant indexes
 *              icolors     - array of input color values
 *              ocolors     - array of output color values
 *              halftoning  - flag for halftoning
 *              levels      - if contone, number of levels
 *
 * Returns:     For each colorant applies the appropriate transform for the PS color
 *
 * Notes:       This function is now a simple trampoline onto Setup and Do
 *              functions which can be optionally used to optimise out
 *              unnecessary setup actions.
 */
void ht_applyTransform(
  SPOTNO              spotno,
  HTTYPE              type,
  size_t              ncomps,
  const COLORANTINDEX aci[],
  const COLORVALUE    icolors[],
  COLORVALUE          ocolors[],
  Bool                halftoning,
  int32               levels)
{
  size_t i;
  HT_TRANSFORM_INFO transformInfo;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(aci != NULL, "NULL pointer to array of colorant indexes");
  HQASSERT(icolors != NULL, "NULL pointer to input color values");
  HQASSERT(ocolors != NULL, "NULL pointer to output color values");

  for ( i = 0; i < ncomps; i++ ) {
    ht_setupTransformInfos(spotno, type, 1, &aci[i], halftoning, levels,
                           &transformInfo);
    ht_doTransforms(1, &icolors[i], &transformInfo, &ocolors[i]);
  }
} /* Function ht_applyTransform */


/* Obtain the 'transformInfo' for the set of colorants for later use in
 * ht_doTransforms().
 */
void ht_setupTransformInfos(
  SPOTNO              spotno,
  HTTYPE              type,
  size_t              ncomps,
  const COLORANTINDEX aci[],
  Bool                halftoning,
  int32               levels,
  HT_TRANSFORM_INFO   transformInfo[])
{
  size_t i;
  HT_TRANSFORM_INFO *cachedTransformInfo;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(ncomps >= 0, "-ve color ncomps");
  HQASSERT(aci != NULL, "NULL pointer to array of colorant indexes");
  HQASSERT(transformInfo != NULL, "NULL pointer to array of transformInfo");

  /* Get the cache of transform info for the last spot used. */
  cachedTransformInfo =
    ht_getCachedTransformInfo(spotno, type, halftoning, levels);

  for ( i = 0; i < ncomps; i++ ) {
    int8 transform = CT_DEGENERATE;
    COLORVALUE colorClear;
    COLORANTINDEX ci;

    HQASSERT(HT_CI_VALID(aci[i]),
             "ht_setupTransforms: invalid colorant index");

    ci = (aci[i] != COLORANTINDEX_ALL) ? aci[i] : COLORANTINDEX_NONE;

    /* If the last spot used cache contains useful info we'll use it. Otherwise
     * we'll derive the transform info the slow way.
     */
    if (ci <= HT_MAX_COLORANT_CACHE &&
        cachedTransformInfo[ci].transform != CT_INVALID) {
      HQASSERT(cachedTransformInfo[ci].colorClear != COLORVALUE_INVALID,
               "Invalid colorClear");

      transform = cachedTransformInfo[ci].transform;
      colorClear = cachedTransformInfo[ci].colorClear;
    }
    else {
      if ( !halftoning ) {
        colorClear = CAST_TO_COLORVALUE(levels - 1);
        /* special-case contone, as CT_NORMAL or faster equivalent */
        if (levels == 256)
          transform = CT_CONTONE;
        else
          transform = CT_NORMAL;
      } else {
        CHALFTONE *pch = ht_getch(spotno, type, ci);

        colorClear = ht_getClearScreen(spotno, type, ci);
        if (pch != NULL) {
          uint16 notones = ht_chNoTones(pch);

          if ( ht_chIsNormalThreshold(pch) ) {
            transform = CT_NORMAL;

          } else if ( levels > 255 ||
                      ht_use_CT_NORMAL(notones, ht_chAccurate(pch)) ) {
            transform = CT_NORMAL;

          } else if ( notones > 2) {
            transform = CT_ADJUST;

          } else if ( notones == 2 ) {
            if ( !ht_chHasFormclass(pch) ) { /* a pattern screen */
                /* Pattern screens are done in screened output using screening. */
              transform = CT_PATTERN;
            } else {
              transform = CT_NORMAL;
            }
          }
        } else {
          const MODHTONE_REF *mhtone = ht_getModularHalftoneRef(spotno, type, ci);

          if ( mhtone != NULL ) {
            /* modular, actually rendering to contone buffer */
            if (htm_SrcBitDepth(mhtone) == 8)
              transform = CT_CONTONE;
            else
              transform = CT_NORMAL;
          }
        }
      }

      /* Now that we've got updated info for the current colorant, enter it into
       * the last spot used cache.
       */
      if (ci <= HT_MAX_COLORANT_CACHE) {
        HQASSERT(cachedTransformInfo[ci].transform == CT_INVALID,
                 "Invalid transform");
        HQASSERT(cachedTransformInfo[ci].colorClear == COLORVALUE_INVALID,
                 "Invalid colorClear");
        cachedTransformInfo[ci].transform = transform;
        cachedTransformInfo[ci].colorClear = colorClear;
      }
    }

    transformInfo[i].transform = transform;
    transformInfo[i].colorClear = colorClear;
  }
}


void ht_setupTransforms(
  SPOTNO              spotno,
  HTTYPE              type,
  size_t              ncomps,
  const COLORANTINDEX aci[],
  GUCR_RASTERSTYLE    *hRasterStyle,
  HT_TRANSFORM_INFO   transformInfo[])
{
  ht_setupTransformInfos(spotno, type, ncomps, aci,
                         gucr_halftoning(hRasterStyle),
                         gucr_valuesPerComponent(hRasterStyle),
                         transformInfo);
}


void ht_doTransformsMultiAll(
  size_t ncomps,
  const COLORVALUE icolors[],
  const HT_TRANSFORM_INFO transformInfo[],
  COLORVALUE ocolors[],
  size_t nsamples)
{
  size_t i, j;
  COLORVALUE colorClear = transformInfo[0].colorClear;
  Bool same_t = TRUE, same_c = TRUE;

  HQASSERT(COLORVALUE_MAX == 0xff00,
           "Assumptions about 16-bit colour format have changed");
  /* See ht_doTransforms for detailed comments on the calculations. */

  /* Checks if all components have the same transform (happens for contone and
     many threshold screens), and optimizes that case. */
  for ( j = 1 ; j < ncomps ; j++ ) {
    if ( transformInfo[j].transform != transformInfo[0].transform ) {
      same_t = FALSE; break;
    }
    if ( transformInfo[j].colorClear != colorClear )
      same_c = FALSE;
  }

  if ( same_c && same_t ) {
    switch ( transformInfo[0].transform ) {
    case CT_CONTONE:
      for ( i = 0 ; i < ncomps*nsamples ; i++ ) {
        HQASSERT(icolors[i] != COLORVALUE_TRANSPARENT,
                 "Can't deal with COLORVALUE_TRANSPARENT");
        ocolors[i] = COLORVALUE_TO_UINT8(icolors[i]);
      }
      break;

    case CT_ADJUST:
      for ( i = 0 ; i < ncomps*nsamples ; i++ ) {
        HQASSERT(icolors[i] != COLORVALUE_TRANSPARENT,
                 "Can't deal with COLORVALUE_TRANSPARENT");
        COLORVALUE_MULTIPLY((icolors[i] + 0x80) & 0xff00,
                            colorClear, ocolors[i]);
      }
      break;

    case CT_NORMAL:
      if ( colorClear != 0xFF )
        for ( i = 0 ; i < ncomps*nsamples ; i++ ) {
          HQASSERT(icolors[i] != COLORVALUE_TRANSPARENT,
                   "Can't deal with COLORVALUE_TRANSPARENT");
          COLORVALUE_MULTIPLY(icolors[i], colorClear, ocolors[i]);
        }
      else
        /*   COLORVALUE_MULTIPLY(in, 0xFF, out)
         * simplifies to
         *   out = (in * 0xFF + 0x7f80) / 0xff00
         *   out = (in * 0xFF + 0x80*0xFF) / (0xFF*0x100)
         *   out = (in + 0x80) / 0x100
         */
        for ( i = 0 ; i < ncomps*nsamples ; i++ ) {
          HQASSERT(icolors[i] != COLORVALUE_TRANSPARENT,
                   "Can't deal with COLORVALUE_TRANSPARENT");
          ocolors[i] = (icolors[i] + 0x80) / 0x100;
        }
      break;

    case CT_PATTERN:
      /* As COLORVALUE_TRANSPARENT > COLORVALUE_MAX - COLORVALUE_HALF / 0xFF,
       * this handles COLORVALUE_TRANSPARENT implicitly. */
      for ( i = 0 ; i < ncomps*nsamples ; i++ )
        ocolors[i] = (icolors[i] < COLORVALUE_HALF / 0xFF
                      ? 0
                      : icolors[i] > COLORVALUE_MAX - COLORVALUE_HALF / 0xFF
                      ? 2
                      : 1);
      break;

    case CT_DEGENERATE:
      /* As COLORVALUE_TRANSPARENT >= COLORVALUE_HALF,
       * this handles COLORVALUE_TRANSPARENT implicitly. */
      for ( i = 0 ; i < ncomps*nsamples ; i++ )
        ocolors[i] = (icolors[i] < COLORVALUE_HALF ? 0 : colorClear);
      break;
    }
    return;
  } else if ( same_t && transformInfo[0].transform == CT_ADJUST ) {
    for ( i = 0, j = 0 ; i < ncomps*nsamples ; i++ ) {
      colorClear = transformInfo[j++].colorClear;
      if ( j == ncomps )
        j = 0;
      HQASSERT(icolors[i] != COLORVALUE_TRANSPARENT,
               "Can't deal with COLORVALUE_TRANSPARENT");
      COLORVALUE_MULTIPLY((icolors[i] + 0x80) & 0xff00,
                          colorClear, ocolors[i]);
    }
    return;
  }
  /* Fall through if can't be optimised and do it the old slow way. */
  for ( j = 0 ; j < nsamples ; ++j, icolors += ncomps, ocolors += ncomps )
    ht_doTransforms(ncomps, icolors, transformInfo, ocolors);
}


/* Convert 'icolors' from 'ocolors', i.e. from COLORVALUE to device codes,
 * using the 'transformInfo' obtained from ht_setupTransforms().
 */
void ht_doTransforms(
  size_t                  ncomps,
  const COLORVALUE        icolors[],
  const HT_TRANSFORM_INFO transformInfo[],
  COLORVALUE              ocolors[])
{
  size_t i;

  HQASSERT(ncomps >= 0, "-ve color ncomps");
  HQASSERT(icolors != NULL, "NULL pointer to input color values");
  HQASSERT(transformInfo != NULL, "NULL pointer to transformInfo values");
  HQASSERT(ocolors != NULL, "NULL pointer to output color values");

  for ( i = 0; i < ncomps; i++ ) {
    COLORVALUE icolor = icolors[i];
    int8 transform = transformInfo[i].transform;
    COLORVALUE colorClear = transformInfo[i].colorClear;

    /* Treat COLORVALUE_TRANSPARENT as white here, on the grounds that
       it will eventually either be removed or replaced with white
       after overprint unification. */
    if (icolor == COLORVALUE_TRANSPARENT) {
      ocolors[i] = colorClear;
      continue;
    }

    switch ( transform ) {
    case CT_CONTONE:
      HQASSERT(COLORVALUE_MAX == 0xff00,
               "Assumptions about 16-bit colour format have changed") ;
      /* Round to nearest multiple of 1/255 */
      ocolors[i] = COLORVALUE_TO_UINT8(icolor) ;
      break;

    case CT_ADJUST: {
      uint32 cv;
      HQASSERT(COLORVALUE_MAX == 0xff00,
               "Assumptions about 16-bit colour format have changed") ;
      /* Round to nearest multiple of 1/255.
       * The calculation of the colorvalue is supposed to do truncation (instead
       * of rounding with + 0.5) to be dot compatible with Adobe RIPs. */
      cv = (icolor + 0x80) & 0xff00 ;
      COLORVALUE_MULTIPLY(cv, colorClear, ocolors[i]) ;
      break;
    }

    case CT_NORMAL:
      COLORVALUE_MULTIPLY(icolor, colorClear, ocolors[i]) ;
      break;

    case CT_PATTERN:
      /* Threshold for white/black set at 0.5 / 255 */
      ocolors[i] = (icolor < COLORVALUE_HALF / 255
                    ? 0
                    : icolor > COLORVALUE_MAX - COLORVALUE_HALF / 255
                    ? 2
                    : 1);
      break;

    case CT_DEGENERATE:
      ocolors[i] = (icolor < COLORVALUE_HALF
                    ? 0
                    : colorClear);
      break;

    default:
      HQFAIL("ht_doTransforms: unknown CT transform found!");
      break;
    }
  }

} /* Function ht_doTransforms */


void ht_doTransformsMulti(
  size_t                  count,
  const COLORVALUE        icolors[],
  unsigned int            istride,
  const HT_TRANSFORM_INFO transformInfo,
  COLORVALUE              ocolors[],
  unsigned int            ostride)
{
  int8 transform = transformInfo.transform;
  COLORVALUE colorClear = transformInfo.colorClear;

  HQASSERT(count >= 0, "-ve color count");
  HQASSERT(icolors != NULL, "NULL pointer to input color values");
  HQASSERT(istride >= 1, "istride cannot be less than one");
  HQASSERT(ocolors != NULL, "NULL pointer to output color values");
  HQASSERT(ostride >= 1, "ostride cannot be less than one");

  switch ( transform ) {
  case CT_CONTONE:
    HQASSERT(COLORVALUE_MAX == 0xff00,
             "Assumptions about 16-bit colour format have changed") ;
    /* Round to nearest multiple of 1/255 */
    while ( count-- > 0 ) {
      HQASSERT(*icolors != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      *ocolors = COLORVALUE_TO_UINT8(*icolors) ;
      icolors += istride;
      ocolors += ostride;
    }
    break;

  case CT_ADJUST: {
    HQASSERT(COLORVALUE_MAX == 0xff00,
             "Assumptions about 16-bit colour format have changed") ;
    /* Round to nearest multiple of 1/255.
     * The calculation of the colorvalue is supposed to do truncation (instead
     * of rounding with + 0.5) to be dot compatible with Adobe RIPs. */
    while ( count-- > 0 ) {
      uint32 cv = (*icolors + 0x80) & 0xff00 ;
      HQASSERT(*icolors != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      COLORVALUE_MULTIPLY(cv, colorClear, *ocolors) ;
      icolors += istride;
      ocolors += ostride;
    }
    break;
  }

  case CT_NORMAL:
    while ( count-- > 0 ) {
      HQASSERT(*icolors != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      COLORVALUE_MULTIPLY(*icolors, colorClear, *ocolors) ;
      icolors += istride;
      ocolors += ostride;
    }
    break;

  case CT_PATTERN:
    /* Threshold for white/black set at 0.5 / 255 */
    while ( count-- > 0 ) {
      HQASSERT(*icolors != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      *ocolors = (*icolors < COLORVALUE_HALF / 255
                  ? 0
                  : *icolors > COLORVALUE_MAX - COLORVALUE_HALF / 255
                  ? 2
                  : 1);
      icolors += istride;
      ocolors += ostride;
    }
    break;

  case CT_DEGENERATE:
    while ( count-- > 0 ) {
      HQASSERT(*icolors != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      *ocolors = (*icolors < COLORVALUE_HALF
                  ? 0
                  : colorClear);
      icolors += istride;
      ocolors += ostride;
    }
    break;

  default:
    HQFAIL("unknown CT transform found!");
    break;
  }
}


void ht_doTransformsMulti8(
  size_t                  count,
  const uint8             icolors[],
  unsigned int            istride,
  const HT_TRANSFORM_INFO transformInfo,
  COLORVALUE              ocolors[],
  unsigned int            ostride)
{
  int8 transform = transformInfo.transform;
  COLORVALUE colorClear = transformInfo.colorClear;

  HQASSERT(count >= 0, "-ve color count");
  HQASSERT(icolors != NULL, "NULL pointer to input color values");
  HQASSERT(istride >= 1, "istride cannot be less than one");
  HQASSERT(ocolors != NULL, "NULL pointer to output color values");
  HQASSERT(ostride >= 1, "ostride cannot be less than one");

  switch ( transform ) {
  case CT_CONTONE:
    HQASSERT(COLORVALUE_MAX == 0xff00,
             "Assumptions about 16-bit colour format have changed") ;
    /* Round to nearest multiple of 1/255 */
    while ( count-- > 0 ) {
      COLORVALUE icolor = *icolors << 8;
      HQASSERT(icolor != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      *ocolors = COLORVALUE_TO_UINT8(icolor) ;
      icolors += istride;
      ocolors += ostride;
    }
    break;

  case CT_ADJUST: {
    HQASSERT(COLORVALUE_MAX == 0xff00,
             "Assumptions about 16-bit colour format have changed") ;
    /* Round to nearest multiple of 1/255.
     * The calculation of the colorvalue is supposed to do truncation (instead
     * of rounding with + 0.5) to be dot compatible with Adobe RIPs. */
    while ( count-- > 0 ) {
      COLORVALUE icolor = *icolors << 8;
      uint32 cv = (icolor + 0x80) & 0xff00 ;
      HQASSERT(icolor != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      COLORVALUE_MULTIPLY(cv, colorClear, *ocolors) ;
      icolors += istride;
      ocolors += ostride;
    }
    break;
  }

  case CT_NORMAL:
    while ( count-- > 0 ) {
      COLORVALUE icolor = *icolors << 8;
      HQASSERT(icolor != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      COLORVALUE_MULTIPLY(icolor, colorClear, *ocolors) ;
      icolors += istride;
      ocolors += ostride;
    }
    break;

  case CT_PATTERN:
    /* Threshold for white/black set at 0.5 / 255 */
    while ( count-- > 0 ) {
      COLORVALUE icolor = *icolors << 8;
      HQASSERT(icolor != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      *ocolors = (icolor < COLORVALUE_HALF / 255
                  ? 0
                  : icolor > COLORVALUE_MAX - COLORVALUE_HALF / 255
                  ? 2
                  : 1);
      icolors += istride;
      ocolors += ostride;
    }
    break;

  case CT_DEGENERATE:
    while ( count-- > 0 ) {
      COLORVALUE icolor = *icolors << 8;
      HQASSERT(icolor != COLORVALUE_TRANSPARENT, "Can't deal with COLORVALUE_TRANSPARENT");
      *ocolors = (icolor < COLORVALUE_HALF
                  ? 0
                  : colorClear);
      icolors += istride;
      ocolors += ostride;
    }
    break;

  default:
    HQFAIL("unknown CT transform found!");
    break;
  }
}


/* Helper function for ht_invalidate_all_current_halftones(). This is here
 * because CT_INVALID is defined local to this file.
 */
void ht_invalidate_transformInfo(HT_TRANSFORM_INFO *transformInfo)
{
  transformInfo->transform = CT_INVALID;
  transformInfo->colorClear = COLORVALUE_INVALID;
}


/* Log stripped */
