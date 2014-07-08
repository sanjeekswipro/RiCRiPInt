/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscblackevaluate.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Black preservation: First (Black evaluate) phase.
 *
 * This first phase of black preservation evaluates whether black should be
 * preserved for this color.
 * During interpretation, all color spaces are looked at to determine if a Black
 * colorant or an RGB triplet exists. A black colorant may be obtained from
 * DeviceGray, DeviceCMYK, Separation Black, DeviceN containing a Black colorant,
 * and ICCBased spaces equivalent to one of the named spaces. An RGB triplet is
 * only derived from DeviceRGB or an equivalent ICCBased space.
 * If black preservation is on, and a black colorant or RGB triplet is present,
 * the blackevaluate link will be created. Even if black preservation isn't
 * enabled, the position of the black colorant will be recorded because other
 * things, such as OverprintBlack handling, depend on knowing the position of the
 * black colorant in the input space.
 * During the invoke in interpretation, the blackType is determined to be one of
 * the values BLACK_TYPE_NONE, BLACK_TYPE_100_PC, BLACK_TYPE_TINT, BLACK_TYPE_ZERO
 * where the value depends on whether black preservation is on and whether the
 * kind is 100% black and/or black tint.
 *
 * During the compositing phase, any compositing will have turned black
 * preservation off. For opaque objects, the black preservation to be applied to
 * this object (direct rendering) or pixel (backdrop rendering) will have been
 * determined during interpretation because the blackType is inherited by each
 * transparency group in the stack of groups. If the blackType is BLACK_TYPE_NONE
 * we won't create the blackevaluate link, otherwise we will.
 * The set of allowed color spaces is bigger than for interpretation because
 * the transparency group may allow spots and we inherit the blackType from the
 * previous group in the transparency stack where possible.
 * The invoke when compositing will usually verify that black preservation is
 * appropriate. In one complicating case which currently only occurs in PCL, it's
 * possible that the color values were modified by code outside of the color
 * module. If so, the value of the blackType will be BLACK_TYPE_MODIFIED. The
 * invoke will work out the appropriate blackType for itself in that case.
 *
 * We only allow 100% black preservation for linework & text. The same is
 * currently true of black tint preservation, but that restriction may change
 * in the future since it should be possible to preserve black for shadings and
 * possibly also images.
 */

#include "core.h"

#include "namedef_.h"           /* NAME_* */
#include "params.h"             /* UserParams */
#include "swerrors.h"           /* VMERROR */

#include "gs_colorpriv.h"       /* CLINK */
#include "gschcmspriv.h"        /* REPRO_COLOR_MODEL_CMYK_WITH_SPOTS */
#include "gscsmplkpriv.h"       /* cc_grayton_create */

#include "gscblackremove.h"     /* cc_getBlackPosition */
#include "gscblackevaluate.h"   /* externs */



#define CLID_SIZEblackevaluate   (2)

static size_t blackevaluateStructSize(void);

static void blackevaluateUpdatePtrs(CLINK *pLink);

#if defined( ASSERT_BUILD )
static void blackevaluateAssertions( CLINK *pLink ) ;
#else
#define blackevaluateAssertions( pLink )     EMPTY_STATEMENT()
#endif

static void blackevaluate_destroy( CLINK *pLink );
static Bool blackevaluate_invokeSingle(CLINK *pLink , USERVALUE *oColorValues);

static GSC_BLACK_TYPE cc_checkBlackType(Bool              f100pcBlackRelevant,
                                        Bool              fBlackTintRelevant,
                                        REPRO_COLOR_MODEL chainColorModel,
                                        USERVALUE         *colorValues,
                                        int32             nColorValues,
                                        int32             blackPosition,
                                        int32             rgbPositions[3],
                                        GSC_BLACK_TYPE    origBlackType);


struct CLINKblackevaluate {
  Bool              f100pcBlackRelevant;
  Bool              fBlackTintRelevant;
  int32             blackPosition;
  int32             rgbPositions[3];
  REPRO_COLOR_MODEL chainColorModel;
  Bool              compositing;
};


static CLINKfunctions CLINKblackevaluate_functions =
{
    blackevaluate_destroy ,
    blackevaluate_invokeSingle ,
    NULL /* cc_blackevaluate_invokeBlock */,
    NULL /* blackevaluate_scan */
} ;


/* Don't do black preservation for color spaces with more than MAX_BLACK_COLORANTS.
 * This somewhat arbitrary restriction is to avoid plumbing, but we'll never
 * fall foul of this in practice.
 */
#define MAX_BLACK_COLORANTS (1024)


/*
 * Black Remove Data Access Functions
 * ==================================
 */

Bool cc_blackevaluate_create(OBJECT             *colorSpace,
                             COLORSPACE_ID      colorSpaceId,
                             int32              nColorants,
                             Bool               f100pcBlackRelevant,
                             Bool               fBlackTintRelevant,
                             REPRO_COLOR_MODEL  chainColorModel,
                             GUCR_RASTERSTYLE   *hRasterStyle,
                             Bool               compositing,
                             GSC_BLACK_TYPE     chainBlackType,
                             OBJECT             *excludedSeparations,
                             CLINK              **blackLink,
                             int32              *chainBlackPosition)
{
  CLINK *pLink ;
  CLINKblackevaluate *blackevaluate;
  Bool dummyFinalDeviceSpace;
  int32 iColorants[MAX_BLACK_COLORANTS];
  int32 *pColorants = iColorants;
  int32 blackPosition;
  int32 rgbPositions[3];

  *blackLink = NULL;
  *chainBlackPosition = -1;

  switch (chainColorModel) {
  case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
  case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
  case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
    HQASSERT(compositing, "Back end color model used in front end");
  }

  /* Get colorant list for each case */
  switch (colorSpaceId) {
  case SPACE_DeviceCMYK:
    if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                           pColorants, 4))
      return FALSE;
    break;
  case SPACE_DeviceRGB:
    if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGB,
                                           pColorants, 3))
      return FALSE;
  case SPACE_DeviceGray:
    if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_Gray,
                                           pColorants, 1))
      return FALSE;
    break;
  case SPACE_Separation:
  case SPACE_DeviceN:
    HQASSERT(colorSpace != NULL, "colorSpace NULL");

    /* If there are too many colorants, don't do black presesvation but don't error */
    if (nColorants > MAX_BLACK_COLORANTS)
      return FALSE;

    /* Get the list of colorants in this color space. Then see if one of them
     * is black.
     */
    if (!cc_colorspaceNamesToIndex(hRasterStyle,
                                   colorSpace,
                                   FALSE,
                                   TRUE,
                                   pColorants,
                                   nColorants,
                                   excludedSeparations,
                                   &dummyFinalDeviceSpace))
      return FALSE;
    break;
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableABCD:
  case SPACE_CIEBasedABC:
  case SPACE_CalRGB:
  case SPACE_Lab:
  case SPACE_CIEBasedDEF:
  case SPACE_CIETableABC:
  case SPACE_CIEBasedA:
  case SPACE_CalGray:
  case SPACE_CIETableA:
  case SPACE_ICCBased:
  case SPACE_Pattern:
  case SPACE_PatternMask:
    pColorants = NULL;
    break;
  default:
    HQFAIL("Unexpected colorSpaceId");
    break;
  }

  if (!cc_getBlackPosition(colorSpace, colorSpaceId, nColorants,
                           hRasterStyle, chainColorModel, excludedSeparations,
                           &blackPosition))
    return FALSE;
  if (!cc_getRGBPositions(colorSpace, nColorants,
                          hRasterStyle, chainColorModel, excludedSeparations,
                          rgbPositions))
    return FALSE;

  /* Check that there is a black channel to work with  */
  switch (chainColorModel) {
  case REPRO_COLOR_MODEL_CMYK:
  case REPRO_COLOR_MODEL_NAMED_COLOR:
  case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
  case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
    if (blackPosition < 0) {
      HQASSERT(blackPosition == -1, "Unexpected blackPosition value");
      return TRUE;
    }
    break;
  case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
    if (rgbPositions[0] < 0) {
      HQASSERT(rgbPositions[0] == -1 && rgbPositions[1] == -1 && rgbPositions[2] == -1,
               "Unexpected blackPosition value");
      return TRUE;
    }
    /* FALL THROUGH */
  case REPRO_COLOR_MODEL_RGB:
    HQASSERT(rgbPositions[0] >= 0 && rgbPositions[1] >= 0 && rgbPositions[2] >= 0,
             "RGB color chain with inconsistent RGB positions");
    break;
  case REPRO_COLOR_MODEL_GRAY:
    HQASSERT(blackPosition == 0, "100pc black without a black colorant");
    break;
  case REPRO_COLOR_MODEL_CIE:
  case REPRO_COLOR_MODEL_PATTERN:
    return TRUE;
  default:
    HQFAIL("Unexpected color model");
    return TRUE;
  }

  /* If black preservation isn't on, we won't evaluate or preserve it */
  if (!f100pcBlackRelevant && !fBlackTintRelevant) {
    /* The black position is still needed for OverprintBlack */
    *chainBlackPosition = blackPosition;
    return TRUE;
  }

  /* If this is a back end chain, we already know the blackType. If it isn't a
   * value that allows black preservation then we might as well bail out.
   */
  if (compositing &&
      chainBlackType != BLACK_TYPE_100_PC &&
      chainBlackType != BLACK_TYPE_TINT &&
      chainBlackType != BLACK_TYPE_MODIFIED)
    return TRUE;

  pLink = cc_common_create(nColorants,
                           pColorants,
                           colorSpaceId,
                           colorSpaceId,
                           CL_TYPEblackevaluate,
                           blackevaluateStructSize(),
                           &CLINKblackevaluate_functions,
                           CLID_SIZEblackevaluate);
  if (pLink == NULL)
    return FALSE;

  blackevaluateUpdatePtrs(pLink);

  blackevaluate = pLink->p.blackevaluate;

  blackevaluate->f100pcBlackRelevant = f100pcBlackRelevant;
  blackevaluate->fBlackTintRelevant = fBlackTintRelevant;
  blackevaluate->chainColorModel = chainColorModel;
  blackevaluate->blackPosition = blackPosition;
  blackevaluate->rgbPositions[0] = rgbPositions[0];
  blackevaluate->rgbPositions[1] = rgbPositions[1];
  blackevaluate->rgbPositions[2] = rgbPositions[2];
  blackevaluate->compositing = compositing;

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   */
  { CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEblackevaluate, "Didn't create as requested");
    idslot[0] = f100pcBlackRelevant;
    idslot[1] = fBlackTintRelevant;
  }

  *blackLink = pLink;
  *chainBlackPosition = blackevaluate->blackPosition;

  blackevaluateAssertions(pLink);

  return TRUE;
}


static void blackevaluate_destroy( CLINK *pLink )
{
  blackevaluateAssertions( pLink ) ;

  cc_common_destroy( pLink ) ;
}


static Bool blackevaluate_invokeSingle(CLINK *pLink , USERVALUE *oColorValues)
{
  int32 i;
  CLINKblackevaluate *blackevaluate;

  blackevaluateAssertions(pLink);
  blackevaluate = pLink->p.blackevaluate;

  /* For interpretation, evaluate the blackType of this color value.
   * For compositing, confirm that black preservation is allowed.
   */
  if (!blackevaluate->compositing)
    pLink->blackType = cc_blackInput(blackevaluate->f100pcBlackRelevant,
                                     blackevaluate->fBlackTintRelevant,
                                     blackevaluate->chainColorModel,
                                     pLink->iColorValues,
                                     pLink->n_iColorants,
                                     blackevaluate->blackPosition,
                                     blackevaluate->rgbPositions);
  else
    pLink->blackType = cc_checkBlackType(blackevaluate->f100pcBlackRelevant,
                                         blackevaluate->fBlackTintRelevant,
                                         blackevaluate->chainColorModel,
                                         pLink->iColorValues,
                                         pLink->n_iColorants,
                                         blackevaluate->blackPosition,
                                         blackevaluate->rgbPositions,
                                         pLink->blackType);

  /* Pass through the color values */
  for (i = 0; i < pLink->n_iColorants; i++)
    oColorValues[i] = pLink->iColorValues[i];

  return TRUE;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_blackevaluate_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK * , pLink ) ;
  UNUSED_PARAM( CLINKblock * , pBlock ) ;

  blackevaluateAssertions( pLink ) ;

  return TRUE ;
}
#endif

static size_t blackevaluateStructSize()
{
  return sizeof(CLINKblackevaluate);
}

static void blackevaluateUpdatePtrs(CLINK *pLink)
{
  pLink->p.blackevaluate = (CLINKblackevaluate *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void blackevaluateAssertions( CLINK *pLink )
{
  cc_commonAssertions( pLink ,
                       CL_TYPEblackevaluate,
                       blackevaluateStructSize(),
                       & CLINKblackevaluate_functions ) ;

  HQASSERT( pLink->p.blackevaluate == ( CLINKblackevaluate * ) (( uint8 * )pLink + cc_commonStructSize( pLink )) ,
            "blackevaluate data not set" ) ;

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceGray:
  case SPACE_DeviceK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_CalRGB:
  case SPACE_CalGray:
  case SPACE_ICCBased:
  case SPACE_InterceptCMYK:
  case SPACE_InterceptRGB:
  case SPACE_InterceptGray:
    break ;
  default:
    HQFAIL( "Bad input color space" ) ;
    break ;
  }
}
#endif


/* cc_blackInput is only used during interpretation. It determines whether
 * a color is 100% black or a black tint. It does this from the colorModel of
 * the chain and the color values. The color models that support black preservation
 * are Gray, CMYK, RGB, and NamedColor.
 */
GSC_BLACK_TYPE cc_blackInput(Bool              f100pcBlackRelevant,
                             Bool              fBlackTintRelevant,
                             REPRO_COLOR_MODEL colorModel,
                             USERVALUE         *pColorValues,
                             int32             nColorValues,
                             int32             blackPosition,
                             int32             rgbPositions[3])
{
  HQASSERT(colorModel < REPRO_N_COLOR_MODELS ||
           colorModel == REPRO_COLOR_MODEL_PATTERN,
           "Invalid color model" );
  HQASSERT(pColorValues != NULL || colorModel == REPRO_COLOR_MODEL_PATTERN,
           "Expected some color values" );

  if (f100pcBlackRelevant) {
    Bool f100pcBlack = FALSE;
    int32 i;

    if (pColorValues != NULL) {
      switch ( colorModel ) {
      case REPRO_COLOR_MODEL_CMYK:
      case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
      case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
      case REPRO_COLOR_MODEL_NAMED_COLOR:
        if (blackPosition >= 0) {
          HQASSERT(blackPosition < nColorValues, "blackPosition -ve");
          f100pcBlack = pColorValues[blackPosition] == 1.0f;
          for (i = 0; i < nColorValues; i++) {
            if (i != blackPosition && pColorValues[i] != 0.0f)
              f100pcBlack = FALSE;
          }
        }
        break;
      case REPRO_COLOR_MODEL_RGB:
        f100pcBlack = pColorValues[0] == 0.0f &&
                      pColorValues[1] == 0.0f &&
                      pColorValues[2] == 0.0;
        break;
      case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
        if (rgbPositions != NULL)
          f100pcBlack = pColorValues[rgbPositions[0]] == 1.0f &&
                        pColorValues[rgbPositions[1]] == 1.0f &&
                        pColorValues[rgbPositions[2]] == 1.0;
        break;
      case REPRO_COLOR_MODEL_GRAY:
        f100pcBlack = pColorValues[0] == 0.0f;
        break;
      }
    }

    if (f100pcBlack)
      return BLACK_TYPE_100_PC;
  }


  if (fBlackTintRelevant) {
    Bool fBlackTint = FALSE;
    int32 i;

    if (pColorValues != NULL) {
      switch ( colorModel ) {
      case REPRO_COLOR_MODEL_CMYK:
      case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
      case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
      case REPRO_COLOR_MODEL_NAMED_COLOR:
        if (blackPosition >= 0) {
          HQASSERT(blackPosition < nColorValues, "blackPosition -ve");
          fBlackTint = pColorValues[blackPosition] > 0.0f;
          for (i = 0; i < nColorValues; i++) {
            if (i != blackPosition && pColorValues[i] != 0.0f)
              fBlackTint = FALSE;
          }
        }
        break;
      case REPRO_COLOR_MODEL_RGB:
        fBlackTint = pColorValues[0] == pColorValues[1] &&
                     pColorValues[1] == pColorValues[2] &&
                     pColorValues[0] < 1.0f;
        break;
      case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
        HQFAIL("We should, but aren't, testing that non-RGB values are 0.0f");
        fBlackTint = pColorValues[rgbPositions[0]] == pColorValues[rgbPositions[1]] &&
                     pColorValues[rgbPositions[0]] == pColorValues[rgbPositions[2]] &&
                     pColorValues[rgbPositions[0]] > 0.0f;
        break;
      case REPRO_COLOR_MODEL_GRAY:
        fBlackTint = pColorValues[0] < 1.0f;
        break;
      }
    }

    if (fBlackTint)
      return BLACK_TYPE_TINT;
  }

  return BLACK_TYPE_NONE;
}

/* cc_checkBlackType will only be called when the fCompositing flag is set, i.e.
 * when converting colors in the transparency stack after interpretation. Most
 * of the function is checking state for asserts, but it is possible that
 * original black objects get assigned a different color, e.g. PCL idiom tests
 * do this, and these objects have their blackType reassessed here.
 */
static GSC_BLACK_TYPE cc_checkBlackType(Bool              f100pcBlackRelevant,
                                        Bool              fBlackTintRelevant,
                                        REPRO_COLOR_MODEL chainColorModel,
                                        USERVALUE         *colorValues,
                                        int32             nColorValues,
                                        int32             blackPosition,
                                        int32             rgbPositions[3],
                                        GSC_BLACK_TYPE    origBlackType)
{
  /* If black preservation isn't relevant to this chain, ignore. */
  if (!f100pcBlackRelevant && !fBlackTintRelevant)
    return BLACK_TYPE_NONE;

  /* We shouldn't be invoking this link if black preservation isn't active */
  switch (origBlackType) {
  case BLACK_TYPE_100_PC:
  case BLACK_TYPE_TINT:
  case BLACK_TYPE_MODIFIED:
    break;
  case BLACK_TYPE_ZERO:
  case BLACK_TYPE_NONE:
  case BLACK_TYPE_UNKNOWN:
  default:
    HQFAIL("Unexpected blackType");
    break;
  }

 /* Check that there is a black channel to work with */
  switch (chainColorModel) {
  case REPRO_COLOR_MODEL_CMYK:
  case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
  case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
    if (blackPosition < 0) {
      HQASSERT(blackPosition == -1, "Unexpected blackPosition value");
      HQASSERT(origBlackType == BLACK_TYPE_NONE || origBlackType == BLACK_TYPE_MODIFIED,
               "Unexpected blackType when black isn't present");
      return BLACK_TYPE_NONE;
    }
    break;
  case REPRO_COLOR_MODEL_RGB:
    HQASSERT(rgbPositions[0] == 0 && rgbPositions[1] == 1 && rgbPositions[2] == 2,
             "RGB color chain with inconsistent RGB positions");
    break;
  case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
    /* NB. No assert for rgbPositions because RGB won't be present for Sep with
     * RGB Virtual device */
    break;
  case REPRO_COLOR_MODEL_GRAY:
    HQASSERT(blackPosition >= 0, "100pc black without a black colorant");
    break;
  }

  /* Check that the black value is consistent with the blackType */
  if (origBlackType == BLACK_TYPE_100_PC) {
    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_CMYK:
    case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
      if (colorValues[blackPosition] != 1.0f)
        HQFAIL("blackType changed from 100pc");
      break;
    case REPRO_COLOR_MODEL_RGB:
      if (colorValues[0] != 0.0f ||
          colorValues[1] != 0.0f ||
          colorValues[2] != 0.0)
        HQFAIL("blackType changed from 100pc");
      break;
    case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
      if (colorValues[rgbPositions[0]] != 1.0f ||
          colorValues[rgbPositions[1]] != 1.0f ||
          colorValues[rgbPositions[2]] != 1.0)
        HQFAIL("blackType changed from 100pc");
      break;
    case REPRO_COLOR_MODEL_GRAY:
      if (colorValues[blackPosition] != 0.0f)
        HQFAIL("blackType changed from 100pc");
      break;
    case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
      if (colorValues[blackPosition] != 1.0f)
        HQFAIL("blackType changed from 100pc");
      break;
    }
  }
  else if (origBlackType == BLACK_TYPE_TINT) {
    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_RGB:
    case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
      if (colorValues[0] != colorValues[1] ||
          colorValues[0] != colorValues[2])
        HQFAIL("blackType changed from tint");
      break;
    }
  }

  /* If the black value was modified after an object was created, test again
   * for the blackType. This is currently only appropriate for colors modified
   * by PCL ropping. For this case, the colors are usually
   */
  if (origBlackType == BLACK_TYPE_MODIFIED) {
    return cc_blackInput(f100pcBlackRelevant,
                         fBlackTintRelevant,
                         chainColorModel,
                         colorValues,
                         nColorValues,
                         blackPosition,
                         rgbPositions);
  }
  else
    return origBlackType;
}


/* gsc_findBlackColorantIndex tries to find the canonical numerical index for
   the black colorant by first looking for 'Black' and then 'Gray', and if
   neither exist by probing the color chain with a range of values. The black
   colorant index, COLORANTINDEX_ALL, or COLORANTINDEX_NONE is stored in the
   RasterStyle. Use guc_getBlackColorantIndex to get the black colorant from
   the RasterStyle; this routine should only be called when setting up the
   raster style. This Black colorant is the one used by the renderer for
   separations. It is also used (by non-photoink devices) for 100% black
   preservation). */
Bool gsc_findBlackColorantIndex( GUCR_RASTERSTYLE* hRasterStyle )
{
  OBJECT        customProcedure = OBJECT_NOTVM_NOTHING ;
  CLINK         *pLink ;
  COLORANTINDEX ci ;
  COLORANTINDEX *cis ;
  USERVALUE     *oColorValues ;
  int32         i , nSamples ;

  int32         nDeviceColorants ;
  DEVICESPACEID deviceSpaceId ;
  COLORSPACE_ID calibrationColorSpace ;

  /* fall back state, meaning a single colorant does not exist that
     equates to black */
  guc_setBlackColorantIndex( hRasterStyle , COLORANTINDEX_NONE ) ;

  ci = guc_colorantIndex( hRasterStyle , system_names + NAME_Black ) ;
  HQASSERT( ci != COLORANTINDEX_ALL && ci != COLORANTINDEX_NONE ,
            "Got COLORANTINDEX_ALL/NONE for 'Black' in gsc_blackColorantindex" ) ;
  if ( ci != COLORANTINDEX_UNKNOWN ) {
    guc_setBlackColorantIndex( hRasterStyle , ci ) ;
    return TRUE ;
  }

  ci = guc_colorantIndex( hRasterStyle , system_names + NAME_Gray ) ;
  HQASSERT( ci != COLORANTINDEX_ALL && ci != COLORANTINDEX_NONE ,
            "Got COLORANTINDEX_ALL/NONE for 'Gray' in gsc_blackColorantindex");
  if ( ci != COLORANTINDEX_UNKNOWN ) {
    guc_setBlackColorantIndex( hRasterStyle , ci ) ;
    return TRUE ;
  }


  /* Failed to find a colorant named 'Black' or 'Gray'; If the PCM isn't DeviceN
   * then there is no Black.
   */
  guc_deviceColorSpace( hRasterStyle ,
                        & deviceSpaceId ,
                        & nDeviceColorants ) ;
  if ( deviceSpaceId == DEVICESPACE_RGB ) {
    /* RGB requires all colorants to be zero for black. */
    guc_setBlackColorantIndex(hRasterStyle, COLORANTINDEX_ALL) ;
    return TRUE ;
  } else if ( deviceSpaceId != DEVICESPACE_N )
    return TRUE ;

  /* PCM is DeviceN. See if we have a photoink device with a unique black channel.
   */
  guc_calibrationColorSpace( hRasterStyle, & calibrationColorSpace ) ;
  switch (calibrationColorSpace) {
  case SPACE_DeviceCMYK:
    ci = guc_colorantIndexReserved( hRasterStyle , system_names + NAME_Black ) ;
    cis = guc_getColorantMapping( hRasterStyle , ci );
    if (cis != NULL && cis[1] == COLORANTINDEX_UNKNOWN) {
      guc_setBlackColorantIndex( hRasterStyle , cis[0] ) ;
      return TRUE;
    }
    /* If we get here, there is a 1->m mapping of Black. Proceed to give the
     * opportunity for the gray custom conversion to determine which channel
     * separations should be rendered on.
     */
    break;
  case SPACE_DeviceGray:
    ci = guc_colorantIndexReserved( hRasterStyle , system_names + NAME_Gray ) ;
    cis = guc_getColorantMapping( hRasterStyle , ci );
    if (cis != NULL && cis[1] == COLORANTINDEX_UNKNOWN)
      guc_setBlackColorantIndex( hRasterStyle , cis[0] ) ;
    return TRUE;
  case SPACE_DeviceRGB:
    /* No black channel */
    return TRUE ;
  case SPACE_notset:
    /* Not a photoink device */
    break;
  default:
    HQFAIL("Unexpected calibration color space");
    return FALSE;
  }

  /* PCM is DeviceN. Try probing the custom conversion to determine if a unique
   * black colorant exists under a different name, eg 'HexK'.
   */
  guc_CustomConversion( hRasterStyle ,
                        DEVICESPACE_Gray ,
                        & customProcedure ) ;

  pLink = cc_grayton_create(customProcedure , nDeviceColorants) ;
  if ( pLink == NULL )
    return FALSE ;

  oColorValues = mm_sac_alloc(mm_pool_color,
                              (mm_size_t)nDeviceColorants * sizeof(USERVALUE),
                              MM_ALLOC_CLASS_NCOLOR) ;
  if (oColorValues == NULL) {
    pLink->functions->destroy( pLink ) ;
    return error_handler(VMERROR) ;
  }

  ci = COLORANTINDEX_UNKNOWN ;
  nSamples = 32 ;
  for ( i = 0 ; i < nSamples && ci != COLORANTINDEX_NONE ; ++i ) {
    int32 j ;
    /* set the gray value; the last sample is always 1.0 */
    pLink->iColorValues[ 0 ] =
      ( i == nSamples - 1 ? 1.0f : ((float)( i + 1 )) / ((float) nSamples )) ;
    if (! (pLink->functions->invokeSingle)( pLink , oColorValues ) ) {
      pLink->functions->destroy( pLink ) ;
      mm_sac_free(mm_pool_color, oColorValues,
                  (mm_size_t)nDeviceColorants * sizeof(USERVALUE)) ;
      return FALSE ;
    }

    /* look for a colorant with none zero value; must only be one */
    for ( j = 0 ; j < nDeviceColorants ; ++j ) {
      if ( oColorValues[ j ] != 0.0f ) {
        if ( ci == COLORANTINDEX_UNKNOWN )
          /* This is the way primary colorant indices are defined */
          ci = j ;
        else if ( ci != j ) {
          /* found another colorant or not consistent with previous sample */
          ci = COLORANTINDEX_NONE ;
          break ; /* out of colorant loop */
        }
      }
    }
    if ( ci == COLORANTINDEX_UNKNOWN )
      ci = COLORANTINDEX_NONE ;
  }
  HQASSERT( ci != COLORANTINDEX_UNKNOWN ,
            "gsc_blackColorantIndex: Trying to set black colorant unknown" ) ;
  guc_setBlackColorantIndex( hRasterStyle , ci ) ;

  pLink->functions->destroy( pLink ) ;

  mm_sac_free(mm_pool_color, oColorValues,
              (mm_size_t)nDeviceColorants * sizeof(USERVALUE)) ;

  return TRUE;
}

/*
* Log stripped */
