/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscblackreplace.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Black preservation: Third (Black replace) phase.
 *
 * The blackType has previously been established, the black component has been
 * removed, the replacement black value has been established, and color
 * management has occurred on the remaining color values.
 * This blackreplace link now has the job of re-instating the preserved black
 * value.
 */

#include "core.h"

#include "namedef_.h"           /* NAME_* */

#include "gs_colorpriv.h"       /* CLINK */
#include "gscblackremove.h"     /* cc_getRgbPositionsInList */

#include "gscblackreplace.h"    /* externs */


#define CLID_SIZEblackreplace   (2)

static void blackreplace_destroy(CLINK *pLink);

static Bool blackreplace_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);

static void blackreplaceUpdatePtrs(CLINK *pLink);

#if defined( ASSERT_BUILD )
static void blackreplaceAssertions(CLINK *pLink);
#else
#define blackreplaceAssertions(pLink)     EMPTY_STATEMENT()
#endif


static void addBlackBackIn(GSC_BLACK_TYPE      blackType,
                           USERVALUE           blackValueToPreserve,
                           CLINKblackreplace   *blackreplace,
                           CLINK               *pLink,
                           USERVALUE           *colorValues);

static Bool cc_getRgbPositionsInOutputList(int32            nColorants,
                                           COLORANTINDEX    *iColorants,
                                           GUCR_RASTERSTYLE *hRasterStyle,
                                           int32            *rgbPositions);


struct CLINKblackreplace {
  Bool              f100pcBlackRelevant;
  Bool              fBlackTintRelevant;
  int32             blackPosition;
  int32             rgbPositions[3];
  Bool              additive;
};


static CLINKfunctions CLINKblackreplace_functions =
{
    blackreplace_destroy ,
    blackreplace_invokeSingle ,
    NULL /* cc_blackreplace_invokeBlock */,
    NULL /* blackreplace_scan */
} ;



/*
 * Black Replace Data Access Functions
 * ===================================
 */

Bool cc_blackreplace_create(int32             nColorants,
                            COLORANTINDEX     *colorants,
                            COLORSPACE_ID     colorSpace,
                            Bool              f100pcBlackRelevant,
                            Bool              fBlackTintRelevant,
                            GUCR_RASTERSTYLE  *hRasterStyle,
                            CLINK             **blackLink)
{
  CLINK *pLink ;
  CLINKblackreplace *blackreplace;
  int32 rgb;

  HQASSERT(colorants != NULL, "colorants NULL");
  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");

  pLink = cc_common_create(nColorants,
                           colorants,
                           colorSpace,
                           colorSpace,
                           CL_TYPEblackreplace,
                           sizeof (CLINKblackreplace),
                           &CLINKblackreplace_functions,
                           CLID_SIZEblackreplace);
  if (pLink == NULL)
    return FALSE;

  blackreplaceUpdatePtrs(pLink);
  blackreplace = pLink->p.blackreplace;

  blackreplace->blackPosition = -1;
  for (rgb = 0; rgb < 3; rgb++)
    blackreplace->rgbPositions[rgb] = -1;

  if (!cc_getBlackPositionInOutputList(pLink->n_iColorants, pLink->iColorants,
                                       hRasterStyle,
                                       &blackreplace->blackPosition))
    return FALSE;
  if (!cc_getRgbPositionsInOutputList(pLink->n_iColorants, pLink->iColorants,
                                      hRasterStyle,
                                      blackreplace->rgbPositions))
    return FALSE;

  /* We only allow 100% black preservation for linework & text. If the source
   * color was a backdrop then we control the application from the backdrop code.
   */
  blackreplace->f100pcBlackRelevant = f100pcBlackRelevant;
  blackreplace->fBlackTintRelevant  = fBlackTintRelevant;
  blackreplace->additive = DeviceColorspaceIsAdditive(pLink->iColorSpace);

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   */
  { CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEblackreplace , "Didn't create as requested" ) ;
    idslot[0] = f100pcBlackRelevant;
    idslot[1] = fBlackTintRelevant;
  }

  *blackLink = pLink;

  blackreplaceAssertions( pLink ) ;

  return TRUE;
}


static void blackreplace_destroy( CLINK *pLink )
{
  blackreplaceAssertions( pLink ) ;

  cc_common_destroy( pLink ) ;
}

/* Replace the black component with a previously established value.
 */
static Bool blackreplace_invokeSingle( CLINK *pLink, USERVALUE *oColorValues )
{
  int32 i;
  CLINKblackreplace *blackreplace ;

  blackreplaceAssertions( pLink ) ;
  HQASSERT( oColorValues != NULL , "oColorValues == NULL" ) ;

  blackreplace = pLink->p.blackreplace ;


  /* Pass through the color values to the next link. They will be modified by
   * having black replaced if appropriate.
   */
  for (i = 0; i < pLink->n_iColorants; i++)
    oColorValues[i] = pLink->iColorValues[i];

  /* If black was removed at the front end of the chain, add it back in now
   * after color management of the black-less color.
   */
  if (pLink->blackType == BLACK_TYPE_100_PC || pLink->blackType == BLACK_TYPE_TINT) {
    addBlackBackIn(pLink->blackType, pLink->blackValueToPreserve,
                   blackreplace, pLink,
                   oColorValues);
  }
  else {
    /* This value is used to propagate black preservation when overprinting in
     * the backdrop. */
    if (blackreplace->f100pcBlackRelevant || blackreplace->fBlackTintRelevant) {
      if (blackreplace->blackPosition >= 0 &&
          pLink->iColorValues[blackreplace->blackPosition] == 0.0f)
        pLink->blackType = BLACK_TYPE_ZERO;
    }
  }

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_blackreplace_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  CLINKblackreplace *blackreplace ;

  blackreplaceAssertions( pLink ) ;
  HQASSERT( oColorValues != NULL , "oColorValues == NULL" ) ;

  blackreplace = pLink->p.blackreplace ;

  /* If we previously set the blackType flag, then we now want to replace
   * either the black component, or all the components of the color with black.
   */
  if (blackreplace->f100pcBlackRelevant || blackreplace->fBlackTintRelevant) {
    if (pBlock->blackType[nColor] == BLACK_TYPE_100_PC ||
        pBlock->blackType[nColor] == BLACK_TYPE_TINT) {
      addBlackBackIn(blackType[nColor], pBlock->blackValueToPreserve[nColor],
                     blackreplace, pLink,
                     colorValues);
    }
  }

  return TRUE ;
}
#endif


static void blackreplaceUpdatePtrs(CLINK *pLink)
{
  pLink->p.blackremove = (CLINKblackremove *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void blackreplaceAssertions( CLINK *pLink )
{
  cc_commonAssertions( pLink ,
                       CL_TYPEblackreplace,
                       sizeof (CLINKblackreplace) ,
                       &CLINKblackreplace_functions ) ;

  HQASSERT( pLink->p.blackreplace == ( CLINKblackreplace * ) (( uint8 * )pLink + cc_commonStructSize( pLink )) ,
            "blackreplace data not set" ) ;

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceGray:
  case SPACE_DeviceK:
  case SPACE_Separation:
  case SPACE_DeviceN:
    break ;
  default:
    HQFAIL( "Bad input color space" ) ;
    break ;
  }
}
#endif

/* ---------------------------------------------------------------------- */

/* Replace the black component of the color values with 'blackValueToPreserve'.
 * We are relying on the original color to be 100% black or a black tint.
 * Since the black was removed at the start of the invoke, if the result
 * of the color management is a non-white, we preserve that in the non-black
 * channels to, a) produce reasomable results should a black overprint a
 * colored object, b) to avoid 'sub-white' pixels appearing when white is
 * color managed to non-white.
 */
static void addBlackBackIn(GSC_BLACK_TYPE      blackType,
                           USERVALUE           blackValueToPreserve,
                           CLINKblackreplace   *blackreplace,
                           CLINK               *pLink,
                           USERVALUE           *colorValues)
{
  UNUSED_PARAM(CLINK *, pLink);

  if (blackType == BLACK_TYPE_100_PC) {
    HQASSERT(blackreplace->f100pcBlackRelevant, "Inconsistent f100pcBlackRelevant");
    HQASSERT(blackValueToPreserve == 1.0f, "Inconsistent black value");
  }
  else
    HQASSERT(blackType == BLACK_TYPE_TINT && blackreplace->fBlackTintRelevant,
             "Inconsistent fBlackTintRelevant");

  if (blackreplace->blackPosition >= 0) {
    HQASSERT(pLink->iColorSpace != SPACE_DeviceRGB,
              "RGB space seems to have a black colorant");
    HQASSERT(blackreplace->blackPosition < pLink->n_iColorants, "blackPosition out of range");

    if (blackreplace->additive) {
      HQASSERT(pLink->iColorSpace == SPACE_DeviceGray ||
               pLink->iColorSpace == SPACE_DeviceRGBK,
               "Unexpected additive space with a black colorant");
      colorValues[blackreplace->blackPosition] = (1.0f - blackValueToPreserve);
    }
    else
      colorValues[blackreplace->blackPosition] = blackValueToPreserve;
  }
  else if (blackreplace->rgbPositions[0] >= 0) {
    int32 i;
    if (blackreplace->additive) {
      HQASSERT(pLink->iColorSpace == SPACE_DeviceRGB,
               "Unexpected additive space with a black colorant");
      for (i = 0; i < 3; i++) {
        HQASSERT(blackreplace->rgbPositions[i] >= 0, "-ve RGB position");
        colorValues[blackreplace->rgbPositions[i]] = (1 - blackValueToPreserve);
      }
    }
    else {
      HQASSERT(pLink->iColorSpace == SPACE_DeviceN,
               "Unexpected additive space with a black colorant");
      for (i = 0; i < 3; i++) {
        HQASSERT(blackreplace->rgbPositions[i] >= 0, "-ve RGB position");
        colorValues[blackreplace->rgbPositions[i]] = blackValueToPreserve;
      }
    }
  }
  else
    HQFAIL("Black preservation used on a device without a black colorant");
}

/* Obtain the position of a black colorant in the iColorants array. This is used
 * only after all color conversions except for photoink splitting. At this point
 * all colorants are renderable except for photoink devices.
 * If there is a black colorant then return TRUE, otherwise FALSE.
 */
Bool cc_getBlackPositionInOutputList(int32            nColorants,
                                     COLORANTINDEX    *iColorants,
                                     GUCR_RASTERSTYLE *hRasterStyle,
                                     int32            *blackPosition)
{
  COLORANTINDEX iBlack;
  int32 i;

  HQASSERT(*blackPosition == -1, "blackPosition should be uninitialised");

  if (!cc_getBlackPositionInList(nColorants, iColorants,
                                 hRasterStyle, blackPosition))
    return FALSE;

  if (*blackPosition == -1) {
    /* NB. This is the same method that cc_blackPresentInOutput() uses for DeviceN
     * output.
     */
    iBlack = guc_getBlackColorantIndex(hRasterStyle);

    for (i = 0; i < nColorants; ++i) {
      if (iColorants[i] == iBlack) {
        *blackPosition = i;
        break;
      }
    }
  }

  return TRUE;
}

/* This function is just a trampoline, but goes in parallel with
 * cc_getBlackPositionInOutputList().
 */
static Bool cc_getRgbPositionsInOutputList(int32            nColorants,
                                           COLORANTINDEX    *iColorants,
                                           GUCR_RASTERSTYLE *hRasterStyle,
                                           int32            *rgbPositions)
{
  return cc_getRgbPositionsInList(nColorants, iColorants,
                                  hRasterStyle, rgbPositions);
}

/* Determine whether black preservation can be handled by the current raster
 * style - is there a black colorant or RGB triplet? If there is then black can
 * be removed from the input and re-instated after color management.
 */
Bool cc_blackPresentInOutput(GUCR_RASTERSTYLE *hRasterStyle, Bool for100pcBlack)
{
  DEVICESPACEID targetPCM;
  int32 dummyN;
  COLORSPACE_ID calibrationColorModel;

  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");

  guc_deviceColorSpace(hRasterStyle, &targetPCM, &dummyN);

  switch (targetPCM) {
  case DEVICESPACE_CMYK:
  case DEVICESPACE_RGBK:
    return TRUE;

  /* Preserving black tints for these devices would give bad output in most cases */
  case DEVICESPACE_Gray:
  case DEVICESPACE_RGB:
  case DEVICESPACE_CMY:
    return for100pcBlack;

  case DEVICESPACE_Lab:
  default:
    return FALSE;

  case DEVICESPACE_N:
    guc_calibrationColorSpace(hRasterStyle, &calibrationColorModel);
    switch (calibrationColorModel) {
    case SPACE_notset:
      /* Non-photoink case, does the output have a black colorant? */
      return guc_getBlackColorantIndex(hRasterStyle) >= 0;

    /* Photoink cases to treat the same as device space output */
    case SPACE_DeviceCMYK:
      return TRUE;
    case SPACE_DeviceRGB:
    case SPACE_DeviceGray:
      return for100pcBlack;

    /* Photoink case to treat as though no black colorant exists */
    case SPACE_DeviceN:
    default:
      return FALSE;
    }
  }
}

/*
* Log stripped */
