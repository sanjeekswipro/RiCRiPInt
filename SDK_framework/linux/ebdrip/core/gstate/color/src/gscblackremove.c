/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscblackremove.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Black preservation: Second (Black remove) phase.
 *
 * The blackType for the current color values has already been established. The
 * process is to now remove the black from the color values prior to color
 * management. After that, the black will be replaced in the blackreplace link.
 * Doing black preservation this way means that the remaining colors will be
 * color managed, which means that if white is managed to non-white on output
 * then non-Black colorants don't knock out.
 * When compositing, colors that are being black preserved may also be a rich
 * black due to overprints from interpretation. Those overprints also tend to
 * be well preserved due to this method.
 *
 * For 100% black preservation, it requires 100% black on input, and always
 * preserves the 100% black along with a color managed white in other channels.
 *
 * For black tint preservation, we preserve the luminance of the black. This
 * method is somewhat more complex, but involves establishing the luminance
 * of the input black from the input profile. Determining the output black value
 * involves probing the output profile to determine the black value that gives
 * the same luminance.
 * If black tint preservation is being used in a color chain without profiles,
 * the input black value will be preserved in the output.
 * The output black value is established in this color link, and used when
 * replacing the black value in the blackreplace link.
 *
 * As a complication, black tint preservation may be optionally set to work only
 * on black values above a threshold value, e.g. 0.9.
 *
 * It is possible that this blackremove link will be created when black
 * preservation isn't possible due to a chicken and egg in chain construction,
 * e.g. BlackTint preservation in a chain with RGB color management. The important
 * thing here is that Black preservation is turned off in the invoke function.
 * OTOH, it's important that a blackType of TINT is passed through to the next
 * stage in the transparency stack if color management is applied there, but not
 * in the current chain. As an example, RGB objects in an RGB group, which will
 * be color managed to CMYK later in the next transparency stack.
 */

#include "core.h"

#include "namedef_.h"           /* NAME_* */
#include "swerrors.h"           /* VMERROR */

#include "gs_cache.h"           /* GSC_ENABLE_INTERCEPT_DEVICEN_CACHE */
#include "gs_colorpriv.h"       /* CLINK */
#include "gsccmmpriv.h"         /* cc_getFinalXYZLink */
#include "gscdevcn.h"           /* cc_getCMMChain */
#include "gschcmspriv.h"        /* REPRO_COLOR_MODEL_CMYK_WITH_SPOTS */
#include "gscheadpriv.h"        /* LUM_TABLE_SIZE */
#include "gscicc.h"             /* cc_createInverseICCLink */

#include "gscblackevaluate.h"   /* getBlackPosition */
#include "gscblackreplace.h"    /* cc_blackPresentInOutput */
#include "gscparamspriv.h"      /* colorUserParams */
#include "gscblackremove.h"     /* externs */



#define CLID_SIZEblackremove   (4)

static size_t blackremoveStructSize(int32 nColorants);

static void blackremoveUpdatePtrs(CLINK *pLink);

#if defined( ASSERT_BUILD )
static void blackremoveAssertions( CLINK *pLink ) ;
#else
#define blackremoveAssertions( pLink )     EMPTY_STATEMENT()
#endif

static void blackremove_destroy( CLINK *pLink );
static Bool blackremove_invokeSingle(CLINK *pLink , USERVALUE *oColorValues);

static Bool cc_removeBlack(USERVALUE         *iColorValues,
                           int32             n_iColorants,
                           REPRO_COLOR_MODEL chainColorModel,
                           int32             blackPosition,
                           int32             rgbPositions[3],
                           USERVALUE         threshold,
                           USERVALUE         *blackValueToPreserve);

static Bool cc_invokeBlackLuminance(CLINK     *pLink,
                                    USERVALUE *origColors,
                                    USERVALUE sourceBlackValue,
                                    USERVALUE *blackValueToPreserve);

static void cc_destroyBlackLuts(BLACK_PRESERVE *blackLuts);



struct BLACK_PRESERVE {
  CLINK             *finalXYZLink;
  USERVALUE         outputKtoY[LUM_TABLE_SIZE];
  USERVALUE         outputYtoK[LUM_TABLE_SIZE];
};



struct CLINKblackremove {
  GS_CHAIN_CONTEXT  *chainContext;
  Bool              f100pcBlackRelevant;
  Bool              fBlackTintRelevant;
  Bool              fBlackTintLuminance;
  USERVALUE         blackTintThreshold;
  int32             blackPosition;
  int32             rgbPositions[3];
  REPRO_COLOR_MODEL chainColorModel;
  BLACK_PRESERVE    *blackLuts;
  Bool              blackLutsDisabled;
  USERVALUE         tmp_iColorValues[1];            /* Struct Hack */
};


static CLINKfunctions CLINKblackremove_functions =
{
    blackremove_destroy ,
    blackremove_invokeSingle ,
    NULL /* cc_blackremove_invokeBlock */,
    NULL /* blackremove_scan */
} ;


/* Don't do black preservation for color spaces with more than MAX_BLACK_COLORANTS.
 * This somewhat arbitrary restriction is to avoid plumbing, but we'll never
 * fall foul of this.
 */
#define MAX_BLACK_COLORANTS (1024)


/*
 * Black Remove Data Access Functions
 * ==================================
 */
Bool cc_blackremove_create(GS_CHAIN_CONTEXT   *chainContext,
                           OBJECT             *colorSpace,
                           COLORSPACE_ID      colorSpaceId,
                           int32              nColorants,
                           Bool               f100pcBlackRelevant,
                           Bool               fBlackTintRelevant,
                           Bool               fBlackTintLuminance,
                           USERVALUE          blackTintThreshold,
                           REPRO_COLOR_MODEL  chainColorModel,
                           GUCR_RASTERSTYLE   *hRasterStyle,
                           OBJECT             *excludedSeparations,
                           CLINK              **blackLink)
{
  CLINK *pLink ;
  CLINKblackremove *blackremove;
  int32 iColorants[MAX_BLACK_COLORANTS];
  int32 *pColorants = iColorants;
  Bool dummyFinalDeviceSpace;

  /* Get colorant list for each case */
  switch (colorSpaceId) {
  case SPACE_DeviceCMYK:
  case SPACE_InterceptCMYK:
    if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                           pColorants, 4))
      return FALSE;
    break;
  case SPACE_DeviceRGB:
  case SPACE_InterceptRGB:
    if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGB,
                                           pColorants, 3))
      return FALSE;
    break;
  case SPACE_DeviceGray:
  case SPACE_InterceptGray:
    if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_Gray,
                                           pColorants, 1))
      return FALSE;
    break;
  case SPACE_Separation:
  case SPACE_DeviceN:
    if (!cc_colorspaceNamesToIndex(hRasterStyle,
                                   colorSpace,
                                   FALSE, /* allowAutoSeparation */
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
    pColorants = NULL;
    break;
  default:
    HQFAIL("Unexpected colorSpaceId");
    break;
  }

  pLink = cc_common_create(nColorants,
                           pColorants,
                           colorSpaceId,
                           colorSpaceId,
                           CL_TYPEblackremove,
                           blackremoveStructSize(nColorants),
                           &CLINKblackremove_functions,
                           CLID_SIZEblackremove);
  if ( pLink == NULL )
    return FALSE;

  blackremoveUpdatePtrs(pLink);

  blackremoveAssertions( pLink ) ;
  blackremove = pLink->p.blackremove;

  blackremove->chainContext = chainContext;

  if (!cc_getBlackPosition(colorSpace, colorSpaceId, nColorants,
                           hRasterStyle, chainColorModel, excludedSeparations,
                           &blackremove->blackPosition)) {
    pLink->functions->destroy(pLink);
    return FALSE;
  }
  if (!cc_getRGBPositions(colorSpace, nColorants,
                          hRasterStyle, chainColorModel, excludedSeparations,
                          blackremove->rgbPositions)) {
    pLink->functions->destroy(pLink);
    return FALSE;
  }

  blackremove->f100pcBlackRelevant = f100pcBlackRelevant &&
                                     cc_blackPresentInOutput(hRasterStyle, TRUE);
  blackremove->fBlackTintRelevant = fBlackTintRelevant &&
                                    cc_blackPresentInOutput(hRasterStyle, FALSE);
  blackremove->fBlackTintLuminance = fBlackTintLuminance;

  blackremove->blackTintThreshold = blackTintThreshold;
  blackremove->chainColorModel = chainColorModel;
  blackremove->blackLuts = NULL;
  blackremove->blackLutsDisabled  = FALSE;

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   */
  { CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEblackremove , "Didn't create as requested" ) ;
    idslot[0] = f100pcBlackRelevant;
    idslot[1] = fBlackTintRelevant;
    idslot[2] = (CLID) hRasterStyle;
    idslot[3] = (CLID) blackTintThreshold;
  }

  *blackLink = pLink;

  blackremoveAssertions(pLink);

  return TRUE;
}

static void blackremove_destroy( CLINK *pLink )
{
  CLINKblackremove *blackremove;

  blackremoveAssertions(pLink);
  blackremove = pLink->p.blackremove;

  if (blackremove->blackLuts != NULL)
    cc_destroyBlackLuts(blackremove->blackLuts);

  cc_common_destroy(pLink);
}

/* Remove the black component from the current color values and establish the
 * black value that will be later used in the blackreplace link in
 * 'blackValueToPreserve'. The preserved black value will only be different to
 * the intput value when preserving black luminance with the BlackTint method.
 */
static Bool blackremove_invokeSingle(CLINK *pLink , USERVALUE *oColorValues)
{
  int32 i;
  CLINKblackremove *blackremove;

  blackremoveAssertions(pLink);
  blackremove = pLink->p.blackremove;

  HQASSERT(pLink->pnext->linkType != CL_TYPEblackreplace,
           "Black remove followed by black replace");

  /* Pass through the color values to the next link. They will be modified by
   * having black removed from them if appropriate.
   */
  for (i = 0; i < pLink->n_iColorants; i++)
    oColorValues[i] = pLink->iColorValues[i];

  if ((blackremove->f100pcBlackRelevant && pLink->blackType == BLACK_TYPE_100_PC) ||
      (blackremove->fBlackTintRelevant && pLink->blackType == BLACK_TYPE_TINT)) {
    USERVALUE threshold;

    if (pLink->blackType == BLACK_TYPE_TINT)
      threshold = blackremove->blackTintThreshold;
    else
      threshold = -FLT_MAX;    /* no threshold */

    /* Save a copy of these original color values to derive the luminance. We
     * can't use oColorValues just yet because that gets overwritten in
     * cc_invokeBlackLuminance().
     */
    for (i = 0; i < pLink->n_iColorants; i++)
      blackremove->tmp_iColorValues[i] = pLink->iColorValues[i];

    /* The black is now removed from the output values */
    if (!cc_removeBlack(blackremove->tmp_iColorValues,
                        pLink->n_iColorants,
                        blackremove->chainColorModel,
                        blackremove->blackPosition,
                        blackremove->rgbPositions,
                        threshold,
                        &pLink->blackValueToPreserve))
      pLink->blackType = BLACK_TYPE_NONE;

    /* The current value of 'blackValueToPreserve' is the input black value. If
     * appropriate, this is modified to the output black that is equivalent in
     * luminance to the input black.
     * Either way, blackValueToPreserve will put back in the blackreplace link.
     */
    if (blackremove->fBlackTintLuminance && pLink->blackType == BLACK_TYPE_TINT) {
      if (!cc_invokeBlackLuminance(pLink,
                                   pLink->iColorValues,
                                   pLink->blackValueToPreserve,
                                   &pLink->blackValueToPreserve))
        return FALSE;
    }

    for (i = 0; i < pLink->n_iColorants; i++)
      oColorValues[i] = blackremove->tmp_iColorValues[i];
  }
  else
    /* It is possible for blackType to be set in a blackevaluate link when black
     * preservation isn't allowed, e.g. BlackTint preservation in a chain with
     * RGB color management, for which Black preservation should be turned off.
     */
    pLink->blackType = BLACK_TYPE_NONE;

  return TRUE;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_blackremove_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK * , pLink ) ;
  UNUSED_PARAM( CLINKblock * , pBlock ) ;

  blackremoveAssertions( pLink ) ;

  return TRUE ;
}
#endif

static size_t blackremoveStructSize(int32 nColorants)
{
  return sizeof(CLINKblackremove) +
         nColorants * sizeof (USERVALUE);       /* tmp_iColorValues */
}

static void blackremoveUpdatePtrs(CLINK *pLink)
{
  pLink->p.blackremove = (CLINKblackremove *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void blackremoveAssertions( CLINK *pLink )
{
  cc_commonAssertions( pLink ,
                       CL_TYPEblackremove,
                       blackremoveStructSize(pLink->n_iColorants),
                       &CLINKblackremove_functions ) ;

  HQASSERT( pLink->p.blackremove == ( CLINKblackremove * ) (( uint8 * )pLink + cc_commonStructSize( pLink )) ,
            "blackremove data not set" ) ;

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceRGB:
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


/* -------------------------------------------------------------------------- */

/* Remove the black or RGB triplet values and replace with white.
 *
 * Both the black removal and re-applying may be either a black/gray channel, or
 * an RGB triplet, with or without additional spot colorants. So some care is
 * needed.
 */
static Bool cc_removeBlack(USERVALUE         *iColorValues,
                           int32             n_iColorants,
                           REPRO_COLOR_MODEL chainColorModel,
                           int32             blackPosition,
                           int32             rgbPositions[3],
                           USERVALUE         threshold,
                           USERVALUE         *blackValueToPreserve)
{
  int32 i;

#define EPSILON   0.00001

  *blackValueToPreserve = 1.0f;

  if (blackPosition >= 0) {
    HQASSERT(rgbPositions[0] == -1 && rgbPositions[1] == -1 && rgbPositions[2] == -1,
             "RGB positions exist ");

    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_GRAY:
      if (n_iColorants == 1) {
        *blackValueToPreserve = 1.0f - iColorValues[blackPosition];
        if (*blackValueToPreserve < threshold)
          return FALSE;

        iColorValues[blackPosition] = 1.0f;
        break;
      }
      /* A hack for gray using the CMYK intercept */
      HQASSERT(n_iColorants == 4 && blackPosition == 3,
               "Expected gray coerced to CMYK");
      /* FALL THROUGH */
    case REPRO_COLOR_MODEL_CMYK:
    case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
    case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
    case REPRO_COLOR_MODEL_NAMED_COLOR:
      *blackValueToPreserve = iColorValues[blackPosition];
      if (*blackValueToPreserve < threshold)
        return FALSE;

      iColorValues[blackPosition] = 0.0f;
      break;

    default:
      HQFAIL("Unexpected color model when preserving black");
      break;
    }
  }
  else {
    HQASSERT(rgbPositions[0] >= 0 && rgbPositions[1] >= 0 && rgbPositions[2] >= 0,
             "No RGB positions");
    HQASSERT(blackPosition == -1, "Black position exists");

    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
      *blackValueToPreserve = iColorValues[rgbPositions[0]];
      if (*blackValueToPreserve < threshold)
        return FALSE;

      for (i = 0; i < 3; ++i) {
        HQASSERT(fabs(*blackValueToPreserve - iColorValues[rgbPositions[i]]) < EPSILON,
                 "Inconsistent black value");
        iColorValues[rgbPositions[i]] = 0.0f;
      }
      break;

    case REPRO_COLOR_MODEL_RGB:
      *blackValueToPreserve = 1.0f - iColorValues[rgbPositions[0]];
      if (*blackValueToPreserve < threshold)
        return FALSE;

      for (i = 0; i < n_iColorants; ++i) {
        HQASSERT(fabs(*blackValueToPreserve - (1.0f - iColorValues[i])) < EPSILON,
                 "Inconsistent black value");
        iColorValues[i] = 1.0f;
      }
      break;
    }
  }

  return TRUE;
}

/* Find the last link in a color chain that might contain an XYZ or Lab connection
 * space. This is required for luminance preservation when preserving black. If
 * we cannot find a connection space, e.g. there is no color management, or only
 * a device link profile, we'll return NULL. The client will probably attempt to
 * preserve the input black values instead.
 */
static CLINK *getFinalCMMLink(CLINK *pLink)
{
  CLINK *xformContainerLink = NULL;
  CLINKblackremove *blackremove = NULL;

  HQASSERT(pLink != NULL && pLink->pnext != NULL, "pLink NULL");

  if (pLink->linkType == CL_TYPEblackremove) {
    blackremoveAssertions(pLink);
    blackremove = pLink->p.blackremove;
  }

  for (; pLink != NULL; pLink = pLink->pnext) {
    if (pLink->linkType == CL_TYPEcmmxform || pLink->linkType == CL_TYPEinterceptdevicen)
      xformContainerLink = pLink;
  }

  if (xformContainerLink == NULL)
    return NULL;

  /* If there is any color processing after the final CMM link, we will treat it
   * as a device link because we cannot perform a reverse lookup through any
   * such link.
   */
  HQASSERT(xformContainerLink->pnext != NULL, "xformContainerLink->pnext NULL");
  if (xformContainerLink->pnext->linkType != CL_TYPEblackreplace &&
      xformContainerLink->pnext->linkType != CL_TYPEdummyfinallink)
    return NULL;

  /* If the color management is within an interceptdevicen link, we have to poke
   * a bit more to find the sub-chain for the colorModel in hand, and more to
   * find the final CMM link in that sub-chain.
   */
  if (xformContainerLink->linkType == CL_TYPEinterceptdevicen) {
    GS_CHAINinfo *xformContainerChain;
    if (blackremove == NULL) {
      HQFAIL("Shouldn't get nested interceptdevicen links");
      return NULL;
    }
    xformContainerChain = cc_getCMMChain(xformContainerLink, blackremove->chainColorModel);
    if (xformContainerChain == NULL)
      return NULL;
    xformContainerLink = getFinalCMMLink(xformContainerChain->context->pnext);
    if (xformContainerLink == NULL)
      return NULL;
  }

  return xformContainerLink;
}

/* Create a blackLuts structure for use in luminance preservation. Returning a null
 * pointer means that we can't use luminance preservation, but should revert to
 * simply preserving the input black value.
 */
static Bool cc_createBlackLuts(CLINK *pLink, BLACK_PRESERVE **pBlackLuts)
{
  int32 i;
  int32 kIdx;
  int32 lumIdx;
  int32 comp;
  BLACK_PRESERVE *blackLuts;
  CLINK *xformContainerLink;
  CLINK *finalXYZLink;
  CLINK *inverseOutLink;
  COLORSPACE_ID deviceSpace;
  int32 outputBlackPosition = -1;
  int32 outputRGBPositions[3] = {-1, -1, -1};

  HQASSERT(pBlackLuts != NULL && *pBlackLuts == NULL, "blackLuts not NULL");

  /* Find the last possible link in cmmxform after neutral mapping that can
   * accept XYZ values. */
  xformContainerLink = getFinalCMMLink(pLink);
  if (xformContainerLink == NULL)
    return TRUE;

  finalXYZLink = cc_getFinalXYZLink(xformContainerLink);
  if (finalXYZLink == NULL)
    return TRUE;
  HQASSERT(finalXYZLink->n_iColorants == NUMBER_XYZ_COMPONENTS,
           "Inconsistent ICC link");

  /* Create a link from the 'input' table of the final output profile. We're
   * going to use it to push black through it to get an estimate of the
   * luminance to black reverse mapping.
   */
  switch (finalXYZLink->iColorSpace) {
  case SPACE_CIEXYZ:
  case SPACE_CIELab:
    HQFAIL("Final XYZ link for CRD NYI");
    return TRUE;
  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
    inverseOutLink = cc_createInverseICCLink(finalXYZLink, &deviceSpace);
    if (inverseOutLink == NULL)
      return FALSE;
    break;
  case SPACE_HqnPCS:
    HQFAIL("Final XYZ link for HqnPCS NYI");
    return TRUE;
  default:
    return TRUE;
  }

  HQASSERT(inverseOutLink->oColorSpace == finalXYZLink->iColorSpace,
           "Inconsistent inverse link");

  /** Get the black position in the finalXYZLink.
   * \todo JJ. We're restricting luminance preservation to CMYK, RGB, and Gray
   * links. More work will be required for DeviceN output.
   */
  if (deviceSpace == SPACE_DeviceCMYK) {
    outputBlackPosition = 3;
  }
  else if (deviceSpace == SPACE_DeviceRGB) {
    outputRGBPositions[0] = 0;
    outputRGBPositions[1] = 1;
    outputRGBPositions[2] = 2;
  }
  else if (deviceSpace == SPACE_DeviceGray) {
    outputBlackPosition = 0;
  }
  else if (deviceSpace == SPACE_DeviceN) {
    /** \todo JJ WORK TO DO */
    HQFAIL("Preserving black luminance on DeviceN output, NYI");
    inverseOutLink->functions->destroy(inverseOutLink);
    return TRUE;
  }
  else {
    HQFAIL("Unexpected device space from black preservation link");
    inverseOutLink->functions->destroy(inverseOutLink);
    return TRUE;
  }

  /* Create the blackLuts structure. */

  blackLuts = mm_alloc(mm_pool_color, sizeof (BLACK_PRESERVE), MM_ALLOC_CLASS_NCOLOR);
  if (blackLuts == NULL) {
    inverseOutLink->functions->destroy(inverseOutLink);
    return FALSE;
  }

  blackLuts->finalXYZLink = finalXYZLink;
  for (i = 0; i < LUM_TABLE_SIZE; i++) {
    blackLuts->outputKtoY[i] = LUM_TABLE_notset;
    blackLuts->outputYtoK[i] = LUM_TABLE_notset;
  }


  /* Make the outputKtoY table from pushing, e.g. (0 0 0 k), or (k k k) values
   * through the input table of the final output profile and establising the XYZ
   * values. */
  for (comp = 0; comp < inverseOutLink->n_iColorants; comp++)
    inverseOutLink->iColorValues[comp] = 0;

  for (i = 0; i < LUM_TABLE_SIZE; i++) {
    USERVALUE oValues[NUMBER_XYZ_COMPONENTS];
    USERVALUE blackValue = (USERVALUE) i / (LUM_TABLE_SIZE - 1);

    if (outputBlackPosition >= 0) {
      inverseOutLink->iColorValues[outputBlackPosition] = blackValue;
    }
    else {
      int32 rgb;
      HQASSERT(outputRGBPositions[0] >= 0, "No black channel for luminance preservation");
      for (rgb = 0; rgb < 3; rgb++) {
        HQASSERT(outputRGBPositions[rgb] >= 0, "Inconsistent rbg positions");
        inverseOutLink->iColorValues[outputRGBPositions[rgb]] = 1.0f - blackValue;
      }
    }

    if (!(inverseOutLink->functions->invokeSingle)(inverseOutLink, oValues)) {
      inverseOutLink->functions->destroy(inverseOutLink);
      cc_destroyBlackLuts(blackLuts);
      return FALSE;
    }
    blackLuts->outputKtoY[i] = oValues[CC_CIEXYZ_Y];
  }

  /* Destroy the link from the input table of the final output profile */
  inverseOutLink->functions->destroy(inverseOutLink);

  /* Monotonicity check. Does it make reasonable sense to reverse lookup this table?
   * We'll accept any non-montonicity that gets past these tests on the assumption
   * that it's small.
   */
#define N_STEPS   (16)
  for (i = 0; i < N_STEPS; i++) {
    USERVALUE startLum = blackLuts->outputKtoY[i * (LUM_TABLE_SIZE - 1) / N_STEPS];
    USERVALUE endLum = blackLuts->outputKtoY[(i + 1) * (LUM_TABLE_SIZE - 1) / N_STEPS];
    if (startLum < endLum) {
      cc_destroyBlackLuts(blackLuts);
      return TRUE;
    }
  }
  if (blackLuts->outputKtoY[0] <= blackLuts->outputKtoY[LUM_TABLE_SIZE - 1]) {
    cc_destroyBlackLuts(blackLuts);
    return TRUE;
  }

  /* Convert from outputKtoY to outputYtoK
   * outputYtoK will be weakly monotonic. If outputKtoY isn't monotonic, there
   * will be some oddities in the conversion, but probably not too bad.
   */
  kIdx = LUM_TABLE_SIZE - 1;
  for (lumIdx = 0; lumIdx < LUM_TABLE_SIZE; lumIdx++) {
    USERVALUE lumValue = (USERVALUE) lumIdx / (LUM_TABLE_SIZE - 1);

    while (kIdx > 0) {
      if (lumValue < blackLuts->outputKtoY[kIdx - 1])
        break;

      kIdx--;
    }

    if (kIdx == (LUM_TABLE_SIZE - 1) && lumValue <= blackLuts->outputKtoY[kIdx]) {
      /* Use max K */
      blackLuts->outputYtoK[lumIdx] = 1.0f;
    }
    else if (kIdx == 0) {
      /* Use min K */
      blackLuts->outputYtoK[lumIdx] = 0.0f;
    }
    else if (blackLuts->outputKtoY[kIdx - 1] != blackLuts->outputKtoY[kIdx]) {
      blackLuts->outputYtoK[lumIdx] = kIdx -
                                        (lumValue - blackLuts->outputKtoY[kIdx]) /
                                        (blackLuts->outputKtoY[kIdx - 1] - blackLuts->outputKtoY[kIdx]);
      blackLuts->outputYtoK[lumIdx] /= (LUM_TABLE_SIZE - 1);
    }
  }

  *pBlackLuts = blackLuts;

  return TRUE;
}

static void cc_destroyBlackLuts(BLACK_PRESERVE *blackLuts)
{
  HQASSERT(blackLuts != NULL, "blackLuts NULL" );

  mm_free(mm_pool_color, blackLuts, sizeof (BLACK_PRESERVE));
}


/* If we can preserve luminance when doing black preservation, then decide what
 * the modified value of black should be. If we can't preserve luminance then
 * preserve the original black value instead.
 */
static Bool cc_invokeBlackLuminance(CLINK     *pLink,
                                    USERVALUE *origColors,
                                    USERVALUE sourceBlackValue,
                                    USERVALUE *blackValueToPreserve)
{
  int32 i;
  CLINKblackremove *blackremove;
  BLACK_PRESERVE *blackLuts;
  USERVALUE finalLuminance;
  Bool result = TRUE;
  int32 savedCacheFlags;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pLink->pnext != NULL, "pLink->pnext NULL");
  HQASSERT(blackValueToPreserve != NULL, "blackValueToPreserve NULL");

  blackremoveAssertions(pLink);
  blackremove = pLink->p.blackremove;

  /* A default setting in case we have to bail out because the chain isn't
   * appropriate for luminance preservation.
   */
  *blackValueToPreserve = sourceBlackValue;

  if (blackremove->blackLutsDisabled)
    /* Early bail-out because luminance preservation isn't necessary for this chain */
    return TRUE;

  if (blackremove->blackLuts == NULL) {
    if (!cc_createBlackLuts(pLink, &blackremove->blackLuts))
      return FALSE;
    if (blackremove->blackLuts == NULL) {
      blackremove->blackLutsDisabled = TRUE;
      return TRUE;
    }
  }

  blackLuts = blackremove->blackLuts;

  /* We're going to invoke the chain starting from the next link, without black
   * removal. The original color values passed in will be used.
   */
  HQASSERT(pLink->pnext != NULL, "pLink->pnext NULL");
  HQASSERT(pLink->n_iColorants == pLink->pnext->n_iColorants,
           "Mismatched number of colors");
  pLink = pLink->pnext;

  /* Squirrel the current color values and blackType in the first link to restore
   * at the end, and substitute the unmodified black values for the purpose of
   * working out the luminance.
   * NB. This means zeroing all other colorants which will be color managed in
   *     the main chain. For the additive cases, the the origColors will be replaced.
   */
  for (i = 0; i < pLink->n_iColorants; i++)
    pLink->iColorValues[i] = origColors[i];

  /* Invoke the chain with the current input black value. Then extract the XYZ
   * from the finalXYZLink.
   */
  savedCacheFlags = blackremove->chainContext->cacheFlags;
  blackremove->chainContext->cacheFlags &= ~GSC_ENABLE_INTERCEPT_DEVICEN_CACHE;
  chainContext = blackremove->chainContext;
  CLINK_RESERVE(chainContext);

  pLink->blackValueToPreserve = 0;
  pLink->blackType = BLACK_TYPE_NONE;

  while (pLink->pnext != NULL && pLink->linkType != CL_TYPEblackreplace) {
    if (!(pLink->functions->invokeSingle)(pLink, pLink->pnext->iColorValues)) {
      cc_destroyChainContext(&chainContext);
      result = FALSE;
      break;
    }
    pLink->pnext->blackType = pLink->blackType;
    pLink = pLink->pnext;
  }

  HQASSERT(!result || (pLink != NULL && pLink->linkType == CL_TYPEblackreplace),
           "Expected a blackreplace link");

  cc_destroyChainContext(&chainContext);
  blackremove->chainContext->cacheFlags = savedCacheFlags;

  if (!result)
    return FALSE;

  /* Determine the final output K value by pushing the Y from the finalXYZLink
   * through the outputYtoK and interpolating as appropriate.
   */
  finalLuminance = blackLuts->finalXYZLink->iColorValues[CC_CIEXYZ_Y];

  if (finalLuminance <= 0.0f)
    *blackValueToPreserve = 1.0f;
  else if (finalLuminance >= 1.0f)
    *blackValueToPreserve = 0.0f;
  else {
    USERVALUE scaledLum = finalLuminance * (LUM_TABLE_SIZE - 1);
    int32 idx = (int32) scaledLum;

    HQASSERT(idx >= 0 && idx < LUM_TABLE_SIZE - 1, "Black lut index out of range");
    *blackValueToPreserve = blackLuts->outputYtoK[idx] +
                             (scaledLum - idx) *
                             (blackLuts->outputYtoK[idx + 1] - blackLuts->outputYtoK[idx]);
  }

  return TRUE;
}

/* -------------------------------------------------------------------------- */

/* Don't do black preservation for color spaces with more than MAX_BLACK_COLORANTS.
 * This somewhat arbitrary restriction is to avoid plumbing, but we'll never
 * fall foul of this.
 */
#define MAX_BLACK_COLORANTS (1024)


/* Get the position of black in the iColorValues array. If iColorants is NULL,
 * we have a device space so use the fixed position of black or gray.
 * If there is a black colorant then return TRUE, otherwise FALSE.
 */
Bool cc_getBlackPosition(OBJECT            *colorspace,
                         COLORSPACE_ID     iColorSpace,
                         int32             n_iColorants,
                         GUCR_RASTERSTYLE  *hRasterStyle,
                         REPRO_COLOR_MODEL chainColorModel,
                         OBJECT            *excludedSeparations,
                         int32             *blackPosition)
{
  Bool dummyFinalDeviceSpace;
  int32 iColorants[MAX_BLACK_COLORANTS];

  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");
  HQASSERT(blackPosition != NULL, "blackPosition NULL");

  *blackPosition = -1;

  HQASSERT(iColorSpace != SPACE_Indexed,
           "Black removal should be deferred until the Indexed link");

  if (colorspace != NULL &&
      (oType(*colorspace) == OARRAY || oType(*colorspace) == OPACKEDARRAY) &&
      theLen(*colorspace) > 1 &&
      oType(oArray(*colorspace)[0]) == ONAME &&
      (oNameNumber(oArray(*colorspace)[0]) == NAME_Separation ||
       oNameNumber(oArray(*colorspace)[0]) == NAME_DeviceN)) {

    HQASSERT(n_iColorants > 0, "Expected at least one colorant");

    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_CMYK:
    case REPRO_COLOR_MODEL_RGB:
    case REPRO_COLOR_MODEL_GRAY:
    case REPRO_COLOR_MODEL_NAMED_COLOR:
    case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
    case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
    case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
      break;
    default:
      HQFAIL("Unexpected color model with a colorants array");
      break;
    }

    /* If there are too many colorants, don't do black presesvation but don't error */
    if (n_iColorants > MAX_BLACK_COLORANTS)
      return TRUE;

    /* Get the list of colorants in this color space. Then see if one of them
     * is black.
     */
    if (!cc_colorspaceNamesToIndex(hRasterStyle,
                                   colorspace,
                                   FALSE,
                                   TRUE,
                                   iColorants,
                                   n_iColorants,
                                   excludedSeparations,
                                   &dummyFinalDeviceSpace))
      return FALSE;

    if (!cc_getBlackPositionInList(n_iColorants, iColorants, hRasterStyle,
                                   blackPosition))
      return FALSE;
  }
  else {
    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_CMYK:
      HQASSERT(n_iColorants == 4, "CMYK should have 4 colorants");
      *blackPosition = 3;
      break;
    case REPRO_COLOR_MODEL_RGB:
      HQASSERT(n_iColorants == 3, "RGB should have 3 colorants");
      break;
    case REPRO_COLOR_MODEL_GRAY:
      if (iColorSpace == SPACE_InterceptCMYK || iColorSpace == SPACE_DeviceCMYK) {
        /* Hack for Gray coerced to CMYK */
        HQASSERT(n_iColorants == 4, "CMYK should have 4 colorants");
        *blackPosition = 3;
      }
      else {
        HQASSERT(n_iColorants == 1, "Gray should have 1 colorant");
        *blackPosition = 0;
      }
      break;
    case REPRO_COLOR_MODEL_NAMED_COLOR:
      HQASSERT(iColorSpace == SPACE_Pattern ||
               iColorSpace == SPACE_PatternMask ||
               iColorSpace == SPACE_ICCBased,
               "Color space with named colors should have colorants array");
      break;
    case REPRO_COLOR_MODEL_CIE:
    case REPRO_COLOR_MODEL_PATTERN:
      break;
    default:
      HQFAIL("Unexpected color model with no colorants array");
    }
  }

  return TRUE;
}

/* Obtain the position of a black colorant in the iColorants array.
 * This function expects to operate on a list of colorants from the color space,
 * not the output space, i.e. only test for the presence of Black and Gray.
 * Returns FALSE on error.
 */
Bool cc_getBlackPositionInList(int32            nColorants,
                               COLORANTINDEX    *iColorants,
                               GUCR_RASTERSTYLE *hRasterStyle,
                               int32            *blackPosition)
{
  COLORANTINDEX iBlack;
  COLORANTINDEX iGray;
  int32 i;

  HQASSERT(*blackPosition == -1, "blackPosition should be uninitialised");

  iBlack = guc_colorantIndexReserved(hRasterStyle ,
                                     system_names + NAME_Black);
  iGray = guc_colorantIndexReserved(hRasterStyle ,
                                    system_names + NAME_Gray);

  for (i = 0; i < nColorants; ++i) {
    if (iColorants[i] == iBlack) {
      *blackPosition = i;
      break;
    }
  }
  if (*blackPosition == -1) {
    for (i = 0; i < nColorants; ++i) {
      if (iColorants[i] == iGray) {
        *blackPosition = i;
        break;
      }
    }
  }

  return TRUE;
}


/* Get the positions of RGB in the iColorValues array. If iColorants is NULL,
 * we have a device space so use the fixed positions of DeviceRGB.
 * If there is an RGB triplet then return TRUE, otherwise FALSE.
 */
Bool cc_getRGBPositions(OBJECT            *colorspace,
                        int32             n_iColorants,
                        GUCR_RASTERSTYLE  *hRasterStyle,
                        REPRO_COLOR_MODEL chainColorModel,
                        OBJECT            *excludedSeparations,
                        int32             rgbPositions[3])
{
  int32 iColorants[MAX_BLACK_COLORANTS];
  int32 rgb;
  Bool dummyFinalDeviceSpace;

  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");
  HQASSERT(rgbPositions != NULL, "rgbPositions NULL");

  for (rgb = 0; rgb < 3; rgb++)
    rgbPositions[rgb] = -1;

  if (colorspace != NULL &&
      oType(*colorspace) == OARRAY && theLen(*colorspace) > 1 &&
      oType(oArray(*colorspace)[0]) == ONAME &&
      (oNameNumber(oArray(*colorspace)[0]) == NAME_Separation ||
       oNameNumber(oArray(*colorspace)[0]) == NAME_DeviceN)) {

    HQASSERT(n_iColorants > 0, "Expected at least one colorant");

    /* If there are too many colorants, don't do black presesvation but don't error */
    if (n_iColorants > MAX_BLACK_COLORANTS)
      return TRUE;

    /* Get the list of colorants in this color space. Then see if one of them
     * is black.
     */
    if (!cc_colorspaceNamesToIndex(hRasterStyle,
                                   colorspace,
                                   FALSE,
                                   TRUE,
                                   iColorants,
                                   n_iColorants,
                                   excludedSeparations,
                                   &dummyFinalDeviceSpace))
      return FALSE;

    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_RGB:
    case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
      if (!cc_getRgbPositionsInList(n_iColorants, iColorants, hRasterStyle,
                                    rgbPositions))
        return FALSE;
      break;
    }
  }
  else {
    switch (chainColorModel) {
    case REPRO_COLOR_MODEL_RGB:
      HQASSERT(n_iColorants == 3, "RGB should have 3 colorants");
      for (rgb = 0; rgb < 3; rgb++)
        rgbPositions[rgb] = rgb;
      break;
    }
  }

  return TRUE;
}

/* Obtain the position of RGB in the iColorants array.
 * If there is an RGB triplet then return TRUE, otherwise FALSE.
 */
Bool cc_getRgbPositionsInList(int32            nColorants,
                              COLORANTINDEX    *iColorants,
                              GUCR_RASTERSTYLE *hRasterStyle,
                              int32            *rgbPositions)
{
  COLORANTINDEX ciRgb[3];
  Bool allPresent = TRUE;
  Bool rgbPresent;
  int32 i;
  int32 rgb;

  if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGB,
                                         ciRgb, 3))
    return FALSE;

  for (rgb = 0; rgb < 3; rgb++)
    HQASSERT(rgbPositions[rgb] == -1, "rgbPositions should be uninitialised");

  rgbPresent = FALSE;
  for (i = 0; i < nColorants; i++) {
    for (rgb = 0 ; rgb < 3; ++rgb) {
      if (iColorants[i] == ciRgb[rgb]) {
        rgbPositions[rgb] = i;
        rgbPresent = TRUE;
        break;
      }
    }
  }

  /* We require the full set of RGB, if only a subset of RGB are present, we'll
   * treat as is none of them are.
   */
  if (rgbPresent) {
    for (rgb = 0; rgb < 3; rgb++) {
      if (rgbPositions[rgb] == -1) {
        allPresent = FALSE;
        break;
      }
    }
    if (!allPresent) {
      for (rgb = 0; rgb < 3; rgb++)
        rgbPositions[rgb] = -1;
    }
  }

  return TRUE;
}

/*
* Log stripped */
