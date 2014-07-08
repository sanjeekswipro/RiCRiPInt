/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsccmm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Alternate CMM module.
 */

#include "core.h"

#include "namedef_.h"           /* NAME_* */
#include "params.h"             /* USERPARAMS */
#include "swerrors.h"           /* error_clear */

#include "gs_colorpriv.h"       /* XYZVALUE */
#include "gsccie.h"             /* cc_ciebaseddefg_create */
#include "gsccrdpriv.h"         /* cc_crd_create */
#include "gscheadpriv.h"        /* GS_CHAINinfo */
#include "gscicc.h"             /* cc_iccbased_create */
#include "gscalternatecmmpriv.h"/* cc_cmm_create */
#include "gscpdfpriv.h"         /* cc_calrgb_create */
#include "gscsmplkpriv.h"       /* cc_rgbtocmy_create */
#include "gsctable.h"           /* cc_cietableabcd_create */
#include "gscxferpriv.h"        /* cc_transfer_create */

#include "gsccmmpriv.h"         /* externs */


#define CC_CIELab_L             (0)
#define CC_CIELab_a             (1)
#define CC_CIELab_b             (2)
#define NUMBER_LAB_COMPONENTS   (3)

typedef SYSTEMVALUE     LabVALUE[NUMBER_LAB_COMPONENTS];


/* The private structure required for the cmmxform CLINK */
struct CLINKcmmxform {
  CLINK               *cmmTransform[MAX_NEXTDEVICE_DICTS];
};

/* The context structure required whilst creating the cmmxform CLINK */
typedef struct CREATEcmmxform {
  CLINK               *cmmTransform[MAX_NEXTDEVICE_DICTS];

  COLOR_STATE         *colorState;

  Bool                tryAlternateCMM[MAX_NEXTDEVICE_DICTS];

  CLINK               **currentTransform;
  CLINK               **currentLink;

  TRANSFORM_LINK_INFO *currentInfo;
  uint8               currentICCTable;
  Bool                currentUseRelativeWhite;
  Bool                currentBlackPointComp;

  DEVICESPACEID       aimDeviceSpace;

  /* These are used to pass values from an input link to an output link */
  XYZVALUE            sourceWhitePoint;
  XYZVALUE            sourceRelativeWhitePoint;
  XYZVALUE            sourceBlackPoint;
  XYZVALUE            destWhitePoint;
  XYZVALUE            destRelativeWhitePoint;
  XYZVALUE            destBlackPoint;
  XYZVALUE            zeroXYZ;
  XYZVALUE            oneXYZ;
} CREATEcmmxform;


static void destroyTransforms(CLINK **cmmTransforms);
static void  cmmxform_destroy(CLINK *pLink);
static Bool cmmxform_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool cmmxform_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t cmmxform_scan( mps_ss_t ss, CLINK *pLink );

static uint32 cmmxformStructSize(void);
static void cmmxformUpdatePtrs(CLINK *pLink);

#if defined( ASSERT_BUILD )
static void cmmxformAssertions(CLINK *pLink);
#else
#define cmmxformAssertions(_pLink) EMPTY_STATEMENT()
#endif


static CLINK **cc_addLink(CLINK **pThisLink, CLINK *pNextLink);

static Bool transformCIEBasedDEFG(CREATEcmmxform *cmmxform,
                                  COLORSPACE_ID  *chainColorSpace,
                                  int32          *colorspacedimension);
static Bool transformCIETableABCD(CREATEcmmxform *cmmxform,
                                  COLORSPACE_ID  *chainColorSpace,
                                  int32          *colorspacedimension);
static Bool transformCIEBasedABC(CREATEcmmxform *cmmxform,
                                 COLORSPACE_ID  *chainColorSpace,
                                 int32          *colorspacedimension);
static Bool transformCalRGB(CREATEcmmxform *cmmxform,
                            COLORSPACE_ID  *chainColorSpace,
                            int32          *colorspacedimension);
static Bool transformLab(CREATEcmmxform *cmmxform,
                         COLORSPACE_ID  *chainColorSpace,
                         int32          *colorspacedimension);
static Bool transformCIEBasedDEF(CREATEcmmxform *cmmxform,
                                 COLORSPACE_ID  *chainColorSpace,
                                 int32          *colorspacedimension);
static Bool transformCIETableABC(CREATEcmmxform *cmmxform,
                                 COLORSPACE_ID  *chainColorSpace,
                                 int32          *colorspacedimension);
static Bool transformCIEBasedA(CREATEcmmxform *cmmxform,
                               COLORSPACE_ID  *chainColorSpace,
                               int32          *colorspacedimension);
static Bool transformCalGray(CREATEcmmxform *cmmxform,
                             COLORSPACE_ID  *chainColorSpace,
                             int32          *colorspacedimension);
static Bool transformCIETableA(CREATEcmmxform *cmmxform,
                               COLORSPACE_ID  *chainColorSpace,
                               int32          *colorspacedimension);
static Bool transformICCBased(CREATEcmmxform *cmmxform,
                              COLORSPACE_ID  *chainColorSpace,
                              int32          *colorspacedimension,
                              OBJECT         **outputPSColorSpace);
static Bool transformCIEXYZ(CREATEcmmxform *cmmxform,
                            COLORSPACE_ID  *chainColorSpace,
                            int32          *colorspacedimension,
                            OBJECT         **outputPSColorSpace);
static Bool transformPCS(CREATEcmmxform *cmmxform,
                         COLORSPACE_ID  *chainColorSpace,
                         int32          *colorspacedimension,
                         OBJECT         **outputPSColorSpace);
static Bool transformSoftMask(CREATEcmmxform *cmmxform,
                              COLORSPACE_ID  *chainColorSpace,
                              int32          *colorspacedimension,
                              OBJECT         **outputPSColorSpace);
static Bool transformCMM(CREATEcmmxform *cmmxform,
                         COLORSPACE_ID  *chainColorSpace,
                         int32          *colorspacedimension,
                         OBJECT         **outputPSColorSpace);


CLINK *cc_neutralmapping_create(COLORSPACE_ID  srcPCSId,
                                COLORSPACE_ID  destPCSId,
                                Bool           relativeWhite,
                                Bool           relativeBlack,
                                XYZVALUE       srcWhitePoint,
                                XYZVALUE       srcRelativeWhitePoint,
                                XYZVALUE       srcBlackPoint,
                                XYZVALUE       destWhitePoint,
                                XYZVALUE       destRelativeWhitePoint,
                                XYZVALUE       destBlackPoint);

static Bool estimateSourceBlackPoint(CLINK       *sourceLinks,
                                     XYZVALUE    sourceRelativeWhitePoint,
                                     XYZVALUE    sourceBlackPoint);
static Bool estimateDestBlackPoint(CLINK       *destLink,
                                   XYZVALUE    destRelativeWhitePoint,
                                   XYZVALUE    destBlackPoint);

static Bool cc_hqnprofileCSA_create(CREATEcmmxform *cmmxform,
                                    COLORSPACE_ID  *chainColorSpace,
                                    int32          *colorspacedimension);
static CLINK *cc_hqnprofileCRD_create(HQN_PROFILE_INFO  *pInfo,
                                      uint8             desiredIntent,
                                      XYZVALUE          **destWhitePoint,
                                      XYZVALUE          **destBlackPoint,
                                      XYZVALUE          **destRelativeWhitePoint,
                                      DEVICESPACEID     aimDeviceSpace,
                                      COLORSPACE_ID     *oColorSpace,
                                      int32             *dimensions,
                                      OBJECT            **nextColorSpaceObject);
static void copyXYZ(XYZVALUE destValue, XYZVALUE srcValue);

static CLINKfunctions CLINKcmmxform_functions =
{
  cmmxform_destroy,
  cmmxform_invokeSingle,
  NULL /* cmmxform_invokeBlock */,
  cmmxform_scan
};

/* ---------------------------------------------------------------------- */

/* createTransform adds links to the chain for device independent colour processing.

 * This is a work in progress that will ultimately allow transforms consisting of
 * these sub-chains to be processed separately and allow easier communication
 * between the links in a transform.
 */
CLINK *cc_cmmxform_create(GS_COLORinfo          *colorInfo,
                          TRANSFORM_LINK_INFO   *transformList[MAX_NEXTDEVICE_DICTS],
                          DEVICESPACEID         aimDeviceSpace,
                          uint8                 currentRenderingIntent,
                          sw_cmm_instance       *alternateCMM,
                          sw_cmm_instance       *wcsCMM,
                          COLORANTINDEX         *pColorantIndexArray,
                          COLORSPACE_ID         *chainColorSpace,
                          int32                 *colorspacedimension,
                          OBJECT                **outputPSColorSpace)
{
  CLINK *pLink;
  CREATEcmmxform tmp_cmmxform;
  Bool status = TRUE;
  int32 transformIdx = 0;
  COLORSPACE_ID init_chainColorSpace = *chainColorSpace;
  int32 init_colorspacedimension = *colorspacedimension;
  int32 nXUIDs = 0;
  int32 is_scRGB = 0;
  int32 is_conversion = 0;
  int32 i;

  /** \todo @@JJ This test needs augmenting and putting in the right place but will
   * do for now */
  if (alternateCMM != NULL) {
    if (!cc_cmmSupportInputProfiles(alternateCMM) ||
        !cc_cmmSupportOutputProfiles(alternateCMM)) {
      alternateCMM = NULL;
    }
  }

  /** \todo MJ This is deliberately over-simple because cc_cmmxform_create needs
     reworking to handle multiple alternate CMMs, and therefore trying to do
     anything fancier here isn't worth it.  In any case wcsCMM will always be
     null apart from to those actually working on WCS. */
  if ( wcsCMM ) {
    alternateCMM = wcsCMM ;
  }

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    tmp_cmmxform.cmmTransform[transformIdx] = NULL;
    tmp_cmmxform.tryAlternateCMM[transformIdx] = (alternateCMM != NULL);
  }

  tmp_cmmxform.colorState = colorInfo->colorState;

  tmp_cmmxform.currentTransform = NULL;
  tmp_cmmxform.currentLink = NULL;
  tmp_cmmxform.currentInfo = NULL;
  tmp_cmmxform.currentICCTable = 0;
  tmp_cmmxform.currentUseRelativeWhite = FALSE;
  tmp_cmmxform.currentBlackPointComp = FALSE;

  tmp_cmmxform.aimDeviceSpace = aimDeviceSpace;

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++) {
    tmp_cmmxform.sourceWhitePoint[i] = 0.0;
    tmp_cmmxform.sourceRelativeWhitePoint[i] = 0.0;
    tmp_cmmxform.sourceBlackPoint[i] = 0.0;
    tmp_cmmxform.destWhitePoint[i] = 0.0;
    tmp_cmmxform.destRelativeWhitePoint[i] = 0.0;
    tmp_cmmxform.destBlackPoint[i] = 0.0;
    tmp_cmmxform.zeroXYZ[i] = 0.0;
    tmp_cmmxform.oneXYZ[i] = 1.0;
  }

  /* Loop around device independent colour cases. Then go back to main chain */

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    if (transformList[transformIdx] == NULL)
      break;

    tmp_cmmxform.currentTransform = &tmp_cmmxform.cmmTransform[transformIdx];

    /* A first pass over the transform to set the default rendering intent
     * appropriately for the current context.
     */
    for (tmp_cmmxform.currentInfo = transformList[transformIdx];
         tmp_cmmxform.currentInfo != NULL;
         tmp_cmmxform.currentInfo = tmp_cmmxform.currentInfo->next) {
      if (tmp_cmmxform.currentInfo->intent == SW_CMM_INTENT_DEFAULT) {
        tmp_cmmxform.currentInfo->intent = currentRenderingIntent;
      }
    }

    /* Use the alternateCMM if it's possible */
    if (alternateCMM != NULL) {

      /* Work out if the transform is purely ICC and therefore appropriate for
       * the alternate CMM. */
      for (tmp_cmmxform.currentInfo = transformList[transformIdx];
           tmp_cmmxform.currentInfo != NULL;
           tmp_cmmxform.currentInfo = tmp_cmmxform.currentInfo->next) {

        switch (tmp_cmmxform.currentInfo->inputColorSpaceId) {
        case SPACE_ICCBased:
        case SPACE_ICCXYZ:
          /* Don't use the alternateCMM with our own matrix only scRGB profile
           * and check before using it with a colorspace conversion profile.
           */
          if (!cc_get_icc_is_scRGB(tmp_cmmxform.currentInfo->u.icc, &is_scRGB) ||
              !cc_get_icc_is_conversion_profile( tmp_cmmxform.currentInfo->u.icc, &is_conversion))
            return NULL;
          else
            if (is_scRGB ||
                (is_conversion && !cc_cmmSupportColorspaceProfiles(alternateCMM)))
              tmp_cmmxform.tryAlternateCMM[transformIdx] = FALSE;
          break;

        case SPACE_ICCLab:
          /* Check before using a colorspace conversion profile */
          if (!cc_get_icc_is_conversion_profile( tmp_cmmxform.currentInfo->u.icc, &is_conversion))
            return NULL;
          else if (is_conversion && !cc_cmmSupportColorspaceProfiles(alternateCMM))
            tmp_cmmxform.tryAlternateCMM[transformIdx] = FALSE;
          break;

        case SPACE_CMM:
          /** \todo JJ. THIS SHOULD also go via alternateCMM route at this level. But this
           * currently happens within the link and must be brought up here soon */
        default:
          tmp_cmmxform.tryAlternateCMM[transformIdx] = FALSE;
          break;
        }
      }

      /* Next, attempt to create a transform from the profiles via the alternate CMM */
      if (tmp_cmmxform.tryAlternateCMM[transformIdx]) {
        error_context_t *errcontext = CoreContext.error;
        int32 old_err = FALSE;

        tmp_cmmxform.currentLink = &tmp_cmmxform.cmmTransform[transformIdx];

        /* Ignore any errors and try again with the built-in CMM */
        if (error_signalled_context(errcontext))
          error_save_context(errcontext, &old_err);

        pLink = cc_alternatecmm_create(transformList[transformIdx],
                                       alternateCMM,
                                       chainColorSpace,
                                       colorspacedimension);
        if (pLink == NULL) {
          /** \todo JJ. Conditional on retry */

          tmp_cmmxform.tryAlternateCMM[transformIdx] = FALSE;
        }
        else {
          tmp_cmmxform.currentLink = cc_addLink(tmp_cmmxform.currentLink, pLink);
          nXUIDs += pLink->idcount;
        }

        /* We're going to retry with the built-in CMM regardless of the
           alternate CMM error. */
        if (old_err)
          error_restore_context(errcontext, old_err);
        else
          error_clear_context(errcontext);
      }
    }

    /* If we cannot use the alternate CMM, use the built-in CMM (as if we'd
     * really want to do anything else) :-) */
    if (!tmp_cmmxform.tryAlternateCMM[transformIdx]) {

      /* Arbitrate the appropriate ICC table and relative/absolute intent. This
       * will only have an effect for ICC tables, but there may be an ICC link
       * somewhere in the transform.
       * The rule we will use is that if any intent is an absolute intent we'll
       * use absolute intents.
       * Check black point compensation - there should be just one value per
       * reproduction dictionary,
       */
      tmp_cmmxform.currentUseRelativeWhite = TRUE;
      tmp_cmmxform.currentBlackPointComp = transformList[transformIdx]->blackPointComp;

      for (tmp_cmmxform.currentInfo = transformList[transformIdx];
           tmp_cmmxform.currentInfo != NULL;
           tmp_cmmxform.currentInfo = tmp_cmmxform.currentInfo->next) {

        switch (tmp_cmmxform.currentInfo->intent) {
        case SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC:
        case SW_CMM_INTENT_ABSOLUTE_PERCEPTUAL:
        case SW_CMM_INTENT_ABSOLUTE_SATURATION:
          tmp_cmmxform.currentUseRelativeWhite = FALSE;
          break;
        case SW_CMM_INTENT_PERCEPTUAL:
        case SW_CMM_INTENT_RELATIVE_COLORIMETRIC:
        case SW_CMM_INTENT_SATURATION:
        case SW_CMM_INTENT_NONE:
          break;
        default:
          HQFAIL("Invalid intent");
        }

        HQASSERT(tmp_cmmxform.currentInfo->blackPointComp == tmp_cmmxform.currentBlackPointComp,
                 "Inconsistent black point compensation within a transform");
      }

      /* Arbitrate black point compensation. Turn BPC off if we aren't using
       * a relative intent.
       */
      if (!tmp_cmmxform.currentUseRelativeWhite)
        tmp_cmmxform.currentBlackPointComp = FALSE;

      tmp_cmmxform.currentLink = &tmp_cmmxform.cmmTransform[transformIdx];

      for (tmp_cmmxform.currentInfo = transformList[transformIdx];
           tmp_cmmxform.currentInfo != NULL;
           tmp_cmmxform.currentInfo = tmp_cmmxform.currentInfo->next) {


        switch (tmp_cmmxform.currentInfo->intent) {
        case SW_CMM_INTENT_RELATIVE_COLORIMETRIC:
        case SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC:
        case SW_CMM_INTENT_NONE:
          tmp_cmmxform.currentICCTable = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
          break;
        case SW_CMM_INTENT_PERCEPTUAL:
        case SW_CMM_INTENT_ABSOLUTE_PERCEPTUAL:
          tmp_cmmxform.currentICCTable = SW_CMM_INTENT_PERCEPTUAL;
          break;
        case SW_CMM_INTENT_SATURATION:
        case SW_CMM_INTENT_ABSOLUTE_SATURATION:
          tmp_cmmxform.currentICCTable = SW_CMM_INTENT_SATURATION;
          break;
        }

        /* Set this to catch any attempt to use it again. We should now be using
         * currentICCTable and currentUseRelativeWhite.
         */
        tmp_cmmxform.currentInfo->intent = SW_CMM_INTENT_DEFAULT;

        switch (tmp_cmmxform.currentInfo->inputColorSpaceId) {

        case SPACE_CIEBasedDEFG:
          status = transformCIEBasedDEFG(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CIETableABCD:
          status = transformCIETableABCD(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CIEBasedDEF:
          status = transformCIEBasedDEF(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CIETableABC:
          status = transformCIETableABC(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CIEBasedABC:
          status = transformCIEBasedABC(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CalRGB:
          status = transformCalRGB(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_Lab:
          status = transformLab(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CIETableA:
          status = transformCIETableA(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CIEBasedA:
          status = transformCIEBasedA(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CalGray:
          status = transformCalGray(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_ICCBased:
          status = transformICCBased(&tmp_cmmxform, chainColorSpace, colorspacedimension, outputPSColorSpace);
          break;

        case SPACE_HqnProfile:
          status = cc_hqnprofileCSA_create(&tmp_cmmxform, chainColorSpace, colorspacedimension);
          break;

        case SPACE_CMM:
          status = transformCMM(&tmp_cmmxform, chainColorSpace, colorspacedimension, outputPSColorSpace);
          break;

        case SPACE_CIEXYZ:
        case SPACE_CIELab:
          status = transformCIEXYZ(&tmp_cmmxform, chainColorSpace, colorspacedimension, outputPSColorSpace);
          break;

        case SPACE_ICCXYZ:
        case SPACE_ICCLab:
        case SPACE_HqnPCS:
          status = transformPCS(&tmp_cmmxform, chainColorSpace, colorspacedimension, outputPSColorSpace);
          break;

        case SPACE_SoftMaskXYZ:
          status = transformSoftMask(&tmp_cmmxform, chainColorSpace, colorspacedimension, outputPSColorSpace);
          break;

        case SPACE_DeviceCMYK:
        case SPACE_DeviceRGB:
        case SPACE_DeviceGray:
        case SPACE_DeviceN:
        case SPACE_Separation:
          /* These can result from using devicelink's as intercept spaces */
          break;

        default:
          HQFAIL("Unxepected colorspace");
          (void) error_handler(UNDEFINED);
          status = FALSE;
          break;
        }

        if (!status) {
          destroyTransforms(tmp_cmmxform.cmmTransform);
          return NULL;
        }
      }

      for (tmp_cmmxform.currentLink = &tmp_cmmxform.cmmTransform[transformIdx];
           *tmp_cmmxform.currentLink != NULL;
           tmp_cmmxform.currentLink = &(*tmp_cmmxform.currentLink)->pnext) {

        nXUIDs += (*tmp_cmmxform.currentLink)->idcount;
      }
    }
  }

  switch (init_chainColorSpace) {
  case SPACE_InterceptCMYK:
  case SPACE_InterceptRGB:
  case SPACE_InterceptGray:
    /* pColorantIndexArray is unchanged from the device space */
    break;
  default:
    pColorantIndexArray = NULL;
    break;
  }

  pLink = cc_common_create(init_colorspacedimension,
                           pColorantIndexArray,
                           init_chainColorSpace,
                           *chainColorSpace,
                           CL_TYPEcmmxform,
                           cmmxformStructSize(),
                           &CLINKcmmxform_functions,
                           nXUIDs);
  if (pLink == NULL) {
    destroyTransforms(tmp_cmmxform.cmmTransform);
    return NULL;
  }

  cmmxformUpdatePtrs(pLink);

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    int32 i;
    CLINK *currentLink = tmp_cmmxform.cmmTransform[transformIdx];

    /* Copy pLinks */
    pLink->p.cmmxform->cmmTransform[transformIdx] = currentLink;

    /* Copy nXUIDs in reverse order to make the assert easier */
    while (currentLink != NULL) {
      for (i = 0; i < currentLink->idcount; i++) {
        pLink->idslot[--nXUIDs] = currentLink->idslot[i];
      }

      currentLink = currentLink->pnext;
    }
  }
  HQASSERT(nXUIDs == 0, "nXUIDs should be 0");

  return pLink;
}

static void destroyTransforms(CLINK **cmmTransforms)
{
  int32 transformIdx;

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    CLINK *currentLink = cmmTransforms[transformIdx];

    if (currentLink == NULL)
      break;
    cc_destroyLinks(currentLink);
  }
}

static void cmmxform_destroy(CLINK *pLink)
{
  CLINKcmmxform  *cmmxform = pLink->p.cmmxform;

  cmmxformAssertions(pLink);

  destroyTransforms(cmmxform->cmmTransform);
  cc_common_destroy(pLink);
}

static Bool cmmxform_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 transformIdx;
  CLINKcmmxform  *cmmxform = pLink->p.cmmxform;

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    CLINK *currentLink = cmmxform->cmmTransform[transformIdx];
    CLINK *nextTransform = NULL;
    USERVALUE *currentOutput;

    if (currentLink == NULL)
      break;

    if (transformIdx < MAX_NEXTDEVICE_DICTS - 1)
      nextTransform = cmmxform->cmmTransform[transformIdx + 1];

    if (transformIdx == 0) {
      int32 i;
      for (i = 0; i < pLink->n_iColorants; i++)
        currentLink->iColorValues[i] = pLink->iColorValues[i] ;
    }

    if (nextTransform == NULL)
      currentOutput = oColorValues;
    else
      currentOutput = nextTransform->iColorValues;

    while (currentLink->pnext != NULL) {
      if (! (currentLink->functions->invokeSingle)(currentLink, currentLink->pnext->iColorValues))
        return FALSE ;

      currentLink = currentLink->pnext ;
    }
    if (! (currentLink->functions->invokeSingle)(currentLink, currentOutput))
      return FALSE;
  }

  return TRUE;
}


#ifdef INVOKEBLOCK_NYI
static int32 cmmxform_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  cmmxformAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */

/* cmm_scan - Scan all sub-chains */
static mps_res_t cmmxform_scan( mps_ss_t ss, CLINK *pLink )
{
  int32 transformIdx;
  mps_res_t res = MPS_RES_OK;
  CLINKcmmxform  *cmmxform = pLink->p.cmmxform;

  cmmxformAssertions(pLink);

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    if (cmmxform->cmmTransform[transformIdx] == NULL)
      break;
    res = cc_scan(ss, cmmxform->cmmTransform[transformIdx]);
    if (res != MPS_RES_OK)
      break;
  }

  return res;
}

static uint32 cmmxformStructSize(void)
{
  return sizeof (CLINKcmmxform);
}

static void cmmxformUpdatePtrs(CLINK *pLink)
{
  pLink->p.cmmxform = (CLINKcmmxform *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void cmmxformAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEcmmxform,
                      cmmxformStructSize(),
                      &CLINKcmmxform_functions);

  HQASSERT(pLink->p.cmmxform == (CLINKcmmxform *)((uint8 *) pLink + cc_commonStructSize(pLink)),
           "cmmxform data not set");

  /** \todo JJ. More assertions. */
}
#endif

void cc_getCMMRange(CLINK *pLink, int32 index, SYSTEMVALUE range[2])
{
  CLINKcmmxform  *cmmxform = pLink->p.cmmxform;
  CLINK *firstLink = cmmxform->cmmTransform[0];

  HQASSERT(firstLink != NULL, "Must be at least one link in transform");

  cc_getCieRange(firstLink, index, range);
}

/* Assumes that a chain containing pLink has been invoked.
 * The function returns the final link that accepts an XYZ value.
 * Useful for clients that need to know the XYZ equivalent of the output.
 */
CLINK *cc_getFinalXYZLink(CLINK *pLink)
{
  int32 transformIdx;
  CLINK *finalXYZLink;
  CLINKcmmxform *cmmxform;

  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pLink->linkType == CL_TYPEcmmxform, "pLink should be a cmmxform");

  cmmxform = pLink->p.cmmxform;
  HQASSERT(pLink != NULL, "pLink NULL");

  finalXYZLink = NULL;

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    CLINK *currentLink = cmmxform->cmmTransform[transformIdx];

    if (currentLink == NULL) {
      HQASSERT(transformIdx != 0, "Must be at least one link in transform");
      break;
    }

    while (currentLink != NULL) {
      switch (currentLink->iColorSpace) {
      case SPACE_CIEXYZ:
      case SPACE_CIELab:
      case SPACE_ICCXYZ:
      case SPACE_ICCLab:
      case SPACE_HqnPCS:
        finalXYZLink = currentLink;
        break;
      case SPACE_HqnProfile:
        HQFAIL("Shouldn't have an HqnProfile link");
        break;
      }
      currentLink = currentLink->pnext ;
    }
  }

  /* Double check. The finalXYZLink can only exist for particular link types */
  if (finalXYZLink != NULL) {
    switch (finalXYZLink->linkType) {
    case CL_TYPEcrd:
    case CL_TYPEiccbasedoutput:
      break;
    default:
      finalXYZLink = NULL;
      break;
    }
  }

  return finalXYZLink;
}

/* ---------------------------------------------------------------------- */

/* Adds a link to the colorchain.
 */
static CLINK **cc_addLink( CLINK **pThisLink, CLINK *pNextLink )
{
  HQASSERT(pThisLink != NULL, "pThisLink NULL");
  HQASSERT(pNextLink != NULL, "pNextLink NULL");

  *pThisLink = pNextLink ;
  return & pNextLink->pnext ;
}


static Bool transformCIEBasedDEFG(CREATEcmmxform *cmmxform,
                                  COLORSPACE_ID  *chainColorSpace,
                                  int32          *colorspacedimension)
{
  CLINK *pNextLink;

  pNextLink = cc_ciebaseddefg_create(cmmxform->currentInfo->u.ciebaseddefg);
  if (pNextLink == NULL)
    return FALSE;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  HQASSERT(cmmxform->currentInfo->next == NULL ||
           cmmxform->currentInfo->next->inputColorSpaceId == SPACE_CIEBasedABC,
           "inconsistent inputColorSpaceId");

  *chainColorSpace = SPACE_CIEBasedABC ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformCIETableABCD(CREATEcmmxform *cmmxform,
                                  COLORSPACE_ID  *chainColorSpace,
                                  int32          *colorspacedimension)
{
  CLINK *pNextLink;

  pNextLink = cc_cietableabcd_create(cmmxform->currentInfo->u.cietableabcd,
                                     chainColorSpace,
                                     colorspacedimension);
  if (pNextLink == NULL)
    return FALSE;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  HQASSERT(cmmxform->currentInfo->next == NULL ||
           cmmxform->currentInfo->next->inputColorSpaceId == *chainColorSpace,
           "inconsistent inputColorSpaceId");

  return TRUE;
}


static Bool transformCIEBasedABC(CREATEcmmxform *cmmxform,
                                 COLORSPACE_ID  *chainColorSpace,
                                 int32          *colorspacedimension)
{
  CLINK *pNextLink;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;

  pNextLink = cc_ciebasedabc_create(cmmxform->currentInfo->u.ciebasedabc,
                                    &WP,
                                    &BP,
                                    &RWP,
                                    &dummyRBP);
  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->sourceWhitePoint, *WP);
  copyXYZ(cmmxform->sourceBlackPoint, *BP);
  copyXYZ(cmmxform->sourceRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  *chainColorSpace = SPACE_CIEXYZ ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformCalRGB(CREATEcmmxform *cmmxform,
                            COLORSPACE_ID  *chainColorSpace,
                            int32          *colorspacedimension)
{
  CLINK *pNextLink;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;

  pNextLink = cc_calrgb_create(cmmxform->currentInfo->u.calrgb,
                               &WP,
                               &BP,
                               &RWP,
                               &dummyRBP);
  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->sourceWhitePoint, *WP);
  copyXYZ(cmmxform->sourceBlackPoint, *BP);
  copyXYZ(cmmxform->sourceRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  *chainColorSpace = SPACE_CIEXYZ ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformLab(CREATEcmmxform *cmmxform,
                         COLORSPACE_ID  *chainColorSpace,
                         int32          *colorspacedimension)
{
  CLINK *pNextLink;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;

  pNextLink = cc_lab_create(cmmxform->currentInfo->u.lab,
                            &WP,
                            &BP,
                            &RWP,
                            &dummyRBP );
  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->sourceWhitePoint, *WP);
  copyXYZ(cmmxform->sourceBlackPoint, *BP);
  copyXYZ(cmmxform->sourceRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  *chainColorSpace = SPACE_CIEXYZ ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformCIEBasedDEF(CREATEcmmxform *cmmxform,
                                 COLORSPACE_ID  *chainColorSpace,
                                 int32          *colorspacedimension)
{
  CLINK *pNextLink;

  pNextLink = cc_ciebaseddef_create(cmmxform->currentInfo->u.ciebaseddef);
  if (pNextLink == NULL)
    return FALSE;;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  HQASSERT(cmmxform->currentInfo->next == NULL ||
           cmmxform->currentInfo->next->inputColorSpaceId == SPACE_CIEBasedABC,
           "inconsistent inputColorSpaceId");

  *chainColorSpace = SPACE_CIEBasedABC ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformCIETableABC(CREATEcmmxform *cmmxform,
                                 COLORSPACE_ID  *chainColorSpace,
                                 int32          *colorspacedimension)
{
  CLINK *pNextLink;

  pNextLink = cc_cietableabc_create(cmmxform->currentInfo->u.cietableabc,
                                    chainColorSpace,
                                    colorspacedimension);
  if (pNextLink == NULL)
    return FALSE;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  HQASSERT(cmmxform->currentInfo->next == NULL ||
           cmmxform->currentInfo->next->inputColorSpaceId == *chainColorSpace,
           "inconsistent inputColorSpaceId");

  return TRUE;
}

static Bool transformCIEBasedA(CREATEcmmxform *cmmxform,
                               COLORSPACE_ID  *chainColorSpace,
                               int32          *colorspacedimension)
{
  CLINK *pNextLink;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;

  pNextLink = cc_ciebaseda_create(cmmxform->currentInfo->u.ciebaseda,
                                  &WP,
                                  &BP,
                                  &RWP,
                                  &dummyRBP);
  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->sourceWhitePoint, *WP);
  copyXYZ(cmmxform->sourceBlackPoint, *BP);
  copyXYZ(cmmxform->sourceRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  *chainColorSpace = SPACE_CIEXYZ ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformCalGray(CREATEcmmxform *cmmxform,
                             COLORSPACE_ID  *chainColorSpace,
                             int32          *colorspacedimension)
{
  CLINK *pNextLink;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;

  pNextLink = cc_calgray_create(cmmxform->currentInfo->u.calgray,
                                &WP,
                                &BP,
                                &RWP,
                                &dummyRBP);
  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->sourceWhitePoint, *WP);
  copyXYZ(cmmxform->sourceBlackPoint, *BP);
  copyXYZ(cmmxform->sourceRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  *chainColorSpace = SPACE_CIEXYZ ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS ;

  return TRUE;
}

static Bool transformCIETableA(CREATEcmmxform *cmmxform,
                               COLORSPACE_ID  *chainColorSpace,
                               int32          *colorspacedimension)
{
  CLINK *pNextLink;

  pNextLink = cc_cietablea_create(cmmxform->currentInfo->u.cietablea,
                                  chainColorSpace,
                                  colorspacedimension);
  if (pNextLink == NULL)
    return FALSE;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");
  HQASSERT(cmmxform->currentInfo->next == NULL ||
           cmmxform->currentInfo->next->inputColorSpaceId == *chainColorSpace,
           "inconsistent inputColorSpaceId");

  return TRUE;
}

static Bool transformICCBased(CREATEcmmxform *cmmxform,
                              COLORSPACE_ID  *chainColorSpace,
                              int32          *colorspacedimension,
                              OBJECT         **outputPSColorSpace)
{
  CLINK *pNextLink;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;

  if ( !cc_iccbased_create( cmmxform->currentInfo->u.icc,
                            cmmxform->colorState,
                            cmmxform->currentICCTable,
                            &pNextLink,
                            chainColorSpace,
                            colorspacedimension,
                            outputPSColorSpace,
                            &WP,
                            &BP,
                            &RWP,
                            &dummyRBP ) )
    return FALSE;

  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->sourceWhitePoint, *WP);
  copyXYZ(cmmxform->sourceBlackPoint, *BP);
  copyXYZ(cmmxform->sourceRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");

  return TRUE;
}

static Bool transformCIEXYZ(CREATEcmmxform *cmmxform,
                            COLORSPACE_ID  *chainColorSpace,
                            int32          *colorspacedimension,
                            OBJECT         **outputPSColorSpace)
{
  CLINK *pNextLink;
  XYZVALUE whitePoint;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;

  /* For relative rendering intents, we will use the relative white points This
   * isn't Red Book, but when a relative white point isn't provided by the CSA
   * the result is the same. It is important for ICCBased spaces where a media
   * white point may be in the ICC profile and for which we don't want any ink
   * laid down for white.
   */
  if (cmmxform->currentUseRelativeWhite)
    copyXYZ(whitePoint, cmmxform->sourceRelativeWhitePoint);
  else
    copyXYZ(whitePoint, cmmxform->sourceWhitePoint);

  pNextLink = cc_crd_create( cmmxform->currentInfo->u.crd,
                             whitePoint,
                             cmmxform->sourceBlackPoint,
                             &WP,
                             &BP,
                             &RWP,
                             cmmxform->aimDeviceSpace,
                             TRUE,
                             outputPSColorSpace,
                             chainColorSpace,
                             colorspacedimension ) ;
  if (pNextLink == NULL)
    return FALSE;

  copyXYZ(cmmxform->destWhitePoint, *WP);
  copyXYZ(cmmxform->destBlackPoint, *BP);
  copyXYZ(cmmxform->destRelativeWhitePoint, *RWP);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");

  return TRUE;
}

static Bool transformPCS(CREATEcmmxform *cmmxform,
                         COLORSPACE_ID  *chainColorSpace,
                         int32          *colorspacedimension,
                         OBJECT         **outputPSColorSpace)
{
  CLINK *pNextLink;
  CLINK *neutralmappingLink;
  COLORSPACE_ID srcPCSId = *chainColorSpace;
  COLORSPACE_ID destPCSId;
  XYZVALUE *WP;
  XYZVALUE *BP;
  XYZVALUE *RWP;
  XYZVALUE *dummyRBP;
  CLINK *tmpLink;

  switch (cmmxform->currentInfo->inputColorSpaceId) {
  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
    if (!cc_outputtransform_create(cmmxform->currentInfo->u.icc,
                                   cmmxform->colorState,
                                   cmmxform->currentICCTable,
                                   outputPSColorSpace,
                                   &pNextLink,
                                   chainColorSpace,
                                   colorspacedimension,
                                   &WP,
                                   &BP,
                                   &RWP,
                                   &dummyRBP))
      return FALSE;
    if (pNextLink == NULL)
      return FALSE;
    break;
  case SPACE_HqnPCS:
    pNextLink = cc_hqnprofileCRD_create(cmmxform->currentInfo->u.hqnprofile,
                                        cmmxform->currentICCTable,
                                        &WP,
                                        &BP,
                                        &RWP,
                                        cmmxform->aimDeviceSpace,
                                        chainColorSpace,
                                        colorspacedimension,
                                        outputPSColorSpace);
    if (pNextLink == NULL)
      return FALSE;
    break;
  default:
    HQFAIL("Unexpected connection space");
    return error_handler(UNDEFINED);
  }

  copyXYZ(cmmxform->destWhitePoint, *WP);
  copyXYZ(cmmxform->destBlackPoint, *BP);
  copyXYZ(cmmxform->destRelativeWhitePoint, *RWP);

  destPCSId = pNextLink->iColorSpace;

  if (cmmxform->currentBlackPointComp) {
    if (!estimateSourceBlackPoint(*cmmxform->currentTransform,
                                  cmmxform->sourceRelativeWhitePoint,
                                  cmmxform->sourceBlackPoint) ||
        !estimateDestBlackPoint(pNextLink,
                                cmmxform->destRelativeWhitePoint,
                                cmmxform->destBlackPoint)) {
      /* If we didn't find the black points, whether due to absence of suitable
       * data or from some error, we carry on anyway without BPC.
       */
      error_clear();
      cmmxform->currentBlackPointComp = FALSE;
    }
  }
  if (!cmmxform->currentBlackPointComp) {
    /* Initialise black points because we don't want to use them */
    copyXYZ(cmmxform->sourceBlackPoint, cmmxform->zeroXYZ);
    copyXYZ(cmmxform->destBlackPoint, cmmxform->zeroXYZ);
  }

  neutralmappingLink = cc_neutralmapping_create(srcPCSId,
                                                destPCSId,
                                                cmmxform->currentUseRelativeWhite,
                                                cmmxform->currentBlackPointComp,
                                                cmmxform->sourceWhitePoint,
                                                cmmxform->sourceRelativeWhitePoint,
                                                cmmxform->sourceBlackPoint,
                                                cmmxform->destWhitePoint,
                                                cmmxform->destRelativeWhitePoint,
                                                cmmxform->destBlackPoint);
  if (neutralmappingLink == NULL) {
    (*pNextLink->functions->destroy)(pNextLink);
    return FALSE;
  }

  /** \todo JJ. Hack for abstract profiles implemented via Alternate CMM - 62820.
   * Walk over the current transform and insert the neutralmapping link after
   * the input profile.
   * We really need to insert a neutralmapping link on both sides of an abstract
   * profile, but for the current hack we'll just put one on the input side.
   */
  tmpLink = *cmmxform->currentTransform;
  while (&tmpLink->pnext != cmmxform->currentLink)
    tmpLink = tmpLink->pnext;
  if (tmpLink->linkType == CL_TYPEcustomcmm) {
    CLINK **pSourceLink = &(*cmmxform->currentTransform)->pnext;
    CLINK *cmmLink = *pSourceLink;

    pSourceLink = cc_addLink(pSourceLink, neutralmappingLink);
    pSourceLink = cc_addLink(pSourceLink, cmmLink);
  }
  else
    cmmxform->currentLink = cc_addLink(cmmxform->currentLink, neutralmappingLink);

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");

  return TRUE;
}

static Bool transformSoftMask(CREATEcmmxform *cmmxform,
                              COLORSPACE_ID  *chainColorSpace,
                              int32          *colorspacedimension,
                              OBJECT         **outputPSColorSpace)
{
  CLINK *neutralmappingLink;
  COLORSPACE_ID srcPCSId = *chainColorSpace;
  COLORSPACE_ID destPCSId = SPACE_ICCXYZ;

  HQASSERT(cmmxform->cmmTransform[0] != NULL && cmmxform->cmmTransform[0]->pnext == NULL,
           "A soft mask cmmxform should have only one link here");
  HQASSERT(cmmxform->cmmTransform[1] == NULL,
           "Soft mask conversions are limited to one color transform");

  /* Force relative intents to get a range of 0->1 in the soft mask */
  cmmxform->currentUseRelativeWhite = TRUE;
  cmmxform->currentBlackPointComp = TRUE;

  if (!estimateSourceBlackPoint(*cmmxform->currentTransform,
                                cmmxform->sourceRelativeWhitePoint,
                                cmmxform->sourceBlackPoint)) {
    /* If we didn't find the black points, whether due to absence of suitable
     * data or from some error, we carry on anyway without BPC.
     */
    error_clear();
    cmmxform->currentBlackPointComp = FALSE;
    copyXYZ(cmmxform->sourceBlackPoint, cmmxform->zeroXYZ);
  }
  copyXYZ(cmmxform->destWhitePoint, cmmxform->oneXYZ);
  copyXYZ(cmmxform->destBlackPoint, cmmxform->zeroXYZ);
  copyXYZ(cmmxform->destRelativeWhitePoint, cmmxform->oneXYZ);

  neutralmappingLink = cc_neutralmapping_create(srcPCSId,
                                                destPCSId,
                                                cmmxform->currentUseRelativeWhite,
                                                cmmxform->currentBlackPointComp,
                                                cmmxform->sourceWhitePoint,
                                                cmmxform->sourceRelativeWhitePoint,
                                                cmmxform->sourceBlackPoint,
                                                cmmxform->destWhitePoint,
                                                cmmxform->destRelativeWhitePoint,
                                                cmmxform->destBlackPoint);
  if (neutralmappingLink == NULL)
    return FALSE;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, neutralmappingLink);

  *chainColorSpace = SPACE_CIEXYZ;
  *colorspacedimension = NUMBER_XYZ_COMPONENTS;
  *outputPSColorSpace = NULL;

  return TRUE;
}

static Bool transformCMM(CREATEcmmxform *cmmxform,
                         COLORSPACE_ID  *chainColorSpace,
                         int32          *colorspacedimension,
                         OBJECT         **outputPSColorSpace)
{
  CLINK *pNextLink;

  pNextLink = cc_customcmm_create(cmmxform->currentInfo->u.customcmm,
                                  chainColorSpace,
                                  colorspacedimension,
                                  outputPSColorSpace);
  if (pNextLink == NULL)
    return FALSE;

  cmmxform->currentLink = cc_addLink(cmmxform->currentLink, pNextLink);

  HQASSERT(cmmxform->currentLink == &pNextLink->pnext, "unexpected color link");

  return TRUE;
}

/* ------------------------------------------------------------------------ */

/* The neutral mapping clink. This handles white and black point mapping from
 * the source and destination color links.
 */

#include "matrix.h"             /* matrix_inverse_3x3 */

#define CLID_SIZEneutralmapping   (2)

struct CLINKneutralmapping {
  Bool          relativeWhite;
  Bool          relativeBlack;
  XYZVALUE      sourceWP;
  XYZVALUE      sourceBP;
  XYZVALUE      sourceRelativeWP;
  XYZVALUE      outputWP;
  XYZVALUE      outputBP;
  XYZVALUE      outputRelativeWP;

  XYZVALUE      sourceAdaptedWP;
  XYZVALUE      sourceAdaptedRelativeWP;
  XYZVALUE      sourceAdaptedBP;

  double        catMatrix[9];
  double        catMatrixInv[9];
};

/* The linear CAT matrix, aka. "Wrong Von Kries" is the one that's been used by
 * the Hqn ICC processing for a long time. It's not going to stay, but it's here
 * to avoid breaking too much stuff for the moment.
 */
static double linearCATmatrix[9] = { 1.0,  0.0,  0.0,
                                     0.0,  1.0,  0.0,
                                     0.0,  0.0,  1.0};

/* The CAT matrix present in the default CRD. Thiis is also here for
 * compatibility testing with other resources in due course.
 */
static double defaultCATmatrix[9] = { 0.40024, -0.22630,  0.0,
                                      0.70760,  1.16532,  0.0,
                                     -0.08081,  0.04570,  0.91822};

static double bradfordCATmatrix[9] = { 0.8951,  -0.7502,  0.0389,
                                       0.2664,  1.7135,  -0.0685,
                                      -0.1614,  0.0367,   1.0296};

static void neutralmapping_destroy(CLINK *pLink);
Bool neutralmapping_invokeSingle(CLINK *pLink, USERVALUE *icolor);
#ifdef INVOKEBLOCK_NYI
static int32 neutralmapping_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t neutralmapping_scan( mps_ss_t ss, CLINK *pLink );

static uint32 neutralmappingStructSize(void);
static void neutralmappingUpdatePtrs(CLINK *pLink);

#if defined( ASSERT_BUILD )
static void neutralmappingAssertions(CLINK *pLink);
#else
#define neutralmappingAssertions(_pLink) EMPTY_STATEMENT()
#endif

static void doCAT(XYZVALUE cie, CLINKneutralmapping *neutralmapping,
                  XYZVALUE sourceWhite, XYZVALUE destWhite);

static double fLab(double labRatio);


static CLINKfunctions CLINKneutralmapping_functions =
{
  neutralmapping_destroy,
  neutralmapping_invokeSingle,
  NULL /* alternatecmm_invokeBlock */,
  neutralmapping_scan
};



CLINK *cc_neutralmapping_create(COLORSPACE_ID  srcPCSId,
                                COLORSPACE_ID  destPCSId,
                                Bool           relativeWhite,
                                Bool           relativeBlack,
                                XYZVALUE       srcWhitePoint,
                                XYZVALUE       srcRelativeWhitePoint,
                                XYZVALUE       srcBlackPoint,
                                XYZVALUE       destWhitePoint,
                                XYZVALUE       destRelativeWhitePoint,
                                XYZVALUE       destBlackPoint)
{
  int32               i;
  CLINK               *pLink;
  CLINKneutralmapping *neutralmapping;
  double              *catMatrix;
  int32               cat;

  pLink = cc_common_create(NUMBER_XYZ_COMPONENTS,
                           NULL,
                           srcPCSId,
                           destPCSId,
                           CL_TYPEneutralmapping,
                           neutralmappingStructSize(),
                           &CLINKneutralmapping_functions,
                           CLID_SIZEneutralmapping);
  if ( pLink == NULL )
    return NULL;

  neutralmappingUpdatePtrs(pLink);

  neutralmapping = pLink->p.neutralmapping;

  neutralmapping->relativeWhite = relativeWhite;
  neutralmapping->relativeBlack = relativeBlack;

  cat = 1;
  switch (cat) {
  default:
  case 0:
    catMatrix = linearCATmatrix;
    break;
  case 1:
    catMatrix = defaultCATmatrix;
    break;
  case 2:
    catMatrix = bradfordCATmatrix;
    break;
  }

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++) {
    if (srcWhitePoint[i] <= 0.0) {
      HQFAIL("White point is unexpectedly zero");
      cc_common_destroy(pLink);
      (void) error_handler(RANGECHECK);
      return NULL;
    }

    neutralmapping->sourceWP[i] = srcWhitePoint[i];
    neutralmapping->sourceRelativeWP[i] = srcRelativeWhitePoint[i];
    neutralmapping->sourceBP[i] = srcBlackPoint[i];
    neutralmapping->outputWP[i] = destWhitePoint[i];
    neutralmapping->outputRelativeWP[i] = destRelativeWhitePoint[i];
    neutralmapping->outputBP[i] = destBlackPoint[i];

    neutralmapping->sourceAdaptedWP[i] = neutralmapping->sourceWP[i];
    neutralmapping->sourceAdaptedRelativeWP[i] = neutralmapping->sourceRelativeWP[i];
    neutralmapping->sourceAdaptedBP[i] = neutralmapping->sourceBP[i];
  }

  /* Sets up the operands that will be passed to the PQR procedure */
  for (i = 0; i < 9; i++)
    neutralmapping->catMatrix[i] = catMatrix[i];

  if (!matrix_inverse_3x3(neutralmapping->catMatrix, neutralmapping->catMatrixInv)) {
    cc_common_destroy(pLink);
    (void) error_handler(LIMITCHECK);
    return NULL;
  }

  doCAT(neutralmapping->sourceAdaptedWP, neutralmapping,
        neutralmapping->sourceWP, neutralmapping->outputWP);
  doCAT(neutralmapping->sourceAdaptedRelativeWP, neutralmapping,
        neutralmapping->sourceWP, neutralmapping->outputWP);
  doCAT(neutralmapping->sourceAdaptedBP, neutralmapping,
        neutralmapping->sourceWP, neutralmapping->outputWP);

  /* Only relativeWhite & relativeBlack are needed because other items are used
   * in other links.
   */
  pLink->idslot[0] = relativeWhite;
  pLink->idslot[1] = relativeBlack;

  neutralmappingAssertions(pLink);

  return pLink;
}

static void neutralmapping_destroy(CLINK *pLink)
{
  neutralmappingAssertions(pLink);

  cc_common_destroy(pLink);
}

Bool neutralmapping_invokeSingle(CLINK *pLink, USERVALUE *icolor)
{
  int32                 i;
  XYZVALUE              cie;
  CLINKneutralmapping   *neutralmapping = pLink->p.neutralmapping;

  neutralmappingAssertions(pLink);

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    cie[i] = pLink->iColorValues[i];

  doCAT(cie, neutralmapping,
        neutralmapping->sourceWP, neutralmapping->outputWP);

  /* aka Black Point Compensation - BPC */
  if (neutralmapping->relativeBlack) {
    double Ysrc;
    double Ydst;
    double scale;
    double offset;
    SYSTEMVALUE sourceScaleFactor;
    SYSTEMVALUE outputScaleFactor;

    /* Scale XYZ to convert from CIEXYZ to the ICC XYZ */
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
      cie[i] *= (neutralmapping->sourceAdaptedWP[i] / neutralmapping->sourceAdaptedRelativeWP[i]);

    /* This is basically a simple adjustment of XYZ values based on scaling
     * the source range onto the output range. First, we have to ensure that
     * all elements used in this process have been chromatically adapted to
     * the output and adjusted to the output's relative white point.
     */

    /* Convert from the source XYZ to flat XYZ space with (1, 1, 1) white point.
     * NB. The current XYZ is CIE XYZ adapted to the destination relative WP.
     */
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
      cie[i] /= neutralmapping->sourceAdaptedWP[i];

    /* Work out the scaling factors are */
    sourceScaleFactor = neutralmapping->sourceAdaptedWP[CC_CIEXYZ_Y] / neutralmapping->sourceAdaptedRelativeWP[CC_CIEXYZ_Y];
    outputScaleFactor = neutralmapping->outputWP[CC_CIEXYZ_Y] / neutralmapping->outputRelativeWP[CC_CIEXYZ_Y];
    Ysrc = neutralmapping->sourceAdaptedBP[CC_CIEXYZ_Y] * sourceScaleFactor;
    Ydst = neutralmapping->outputBP[CC_CIEXYZ_Y] * outputScaleFactor;

    scale = (1 - Ydst) / (1 - Ysrc);
    offset = (1 - scale);

    /* Apply the scaling */
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
      cie[i] = (cie[i] * scale) + offset;

    /* Convert the flat XYZ space with (1, 1, 1) white point to the output ICC XYZ.
     * NB. The current XYZ is CIE XYZ adapted to the destination relative WP.
     */
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
      cie[i] *= neutralmapping->outputWP[i];

    /* Scale XYZ to convert from ICC XYZ to CIEXYZ */
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
      cie[i] *= (neutralmapping->outputRelativeWP[i] / neutralmapping->outputWP[i]);
  }

  else if (neutralmapping->relativeWhite) {
    /* Scale XYZ to normalise white points from the adapted RelativeWhitePoint
     * of the src to that of the dst.
     */
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
      cie[i] *= (neutralmapping->outputRelativeWP[i] / neutralmapping->sourceAdaptedRelativeWP[i]);
  }

  for (i = 0; i < pLink->n_iColorants; i++)
    icolor[i] = (USERVALUE) cie[i];

  return TRUE;
}


#ifdef INVOKEBLOCK_NYI
static int32 neutralmapping_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  neutralmappingAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */

/* neutralmapping_scan - Not currently necessary */
static mps_res_t neutralmapping_scan( mps_ss_t ss, CLINK *pLink )
{
  UNUSED_PARAM(mps_ss_t, ss);
  UNUSED_PARAM(CLINK *, pLink);

  return MPS_RES_OK;
}

static uint32 neutralmappingStructSize(void)
{
  return sizeof (CLINKneutralmapping);
}

static void neutralmappingUpdatePtrs(CLINK *pLink)
{
  pLink->p.neutralmapping = (CLINKneutralmapping *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void neutralmappingAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEneutralmapping,
                      neutralmappingStructSize(),
                      &CLINKneutralmapping_functions);

  switch (pLink->iColorSpace)
  {
  case SPACE_CIEXYZ:
  case SPACE_CIELab:
  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
    break;
  default:
    HQFAIL("Bad input color space");
    break;
  }
}
#endif


/* Executes the PQR procedure */
static void doCAT(XYZVALUE cie, CLINKneutralmapping *neutralmapping,
                  XYZVALUE sourceWhite, XYZVALUE destWhite)
{
  int32 i;

  MATRIX_MULTIPLY(cie, neutralmapping->catMatrix);

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    cie[i] *= (destWhite[i] / sourceWhite[i]);

  /* now by the inverse CAT matrix */
  MATRIX_MULTIPLY(cie, neutralmapping->catMatrixInv);
}

/* ------------------------------------------------------------------------ */

static double fXYZ(double xyzRatio)
{
  if (xyzRatio > 0.008856)
    return pow(xyzRatio, 1.0/3.0);
  else
    return 841.0/108.0 * xyzRatio + 16.0/116.0;
}

static double fLab(double labRatio)
{
  if (labRatio >= 6.0/29.0)
    return pow(labRatio, 3.0);
  else
    return (labRatio - 4.0/29.0) * 108.0/841.0;
}

static void CIEXYZtoICCLab(XYZVALUE xyz, XYZVALUE white, LabVALUE lab)
{
  double fX = fXYZ(xyz[CC_CIEXYZ_X] / white[CC_CIEXYZ_X]);
  double fY = fXYZ(xyz[CC_CIEXYZ_Y] / white[CC_CIEXYZ_Y]);
  double fZ = fXYZ(xyz[CC_CIEXYZ_Z] / white[CC_CIEXYZ_X]);

  lab[CC_CIELab_L] = 116.0 * fY - 16.0;
  lab[CC_CIELab_a] = 500.0 * (fX - fY);
  lab[CC_CIELab_b] = 200.0 * (fY - fZ);
}

static void ICCLabtoCIEXYZ(LabVALUE lab, XYZVALUE white, XYZVALUE xyz)
{
  double M = (lab[CC_CIELab_L] + 16.0)/116.0;
  double L = M + lab[CC_CIELab_a] /500.0;
  double N = M - lab[CC_CIELab_b] /200.0;

  xyz[CC_CIEXYZ_X] = fLab(L) * white[CC_CIEXYZ_X];
  xyz[CC_CIEXYZ_Y] = fLab(M) * white[CC_CIEXYZ_Y];
  xyz[CC_CIEXYZ_Z] = fLab(N) * white[CC_CIEXYZ_Z];
}

static Bool evaluateBlackPoint(CLINK *outputLink,
                               CLINK *inputLink,
                               COLORSPACE_ID deviceSpace,
                               XYZVALUE white,
                               XYZVALUE outputXYZ)
{
  int32     i;
  USERVALUE oXYZfloat[NUMBER_XYZ_COMPONENTS];
  LabVALUE  oLab;

  if (outputLink != NULL) {
    for (i = 0; i < outputLink->n_iColorants; i++)
      outputLink->iColorValues[i] = 0.0;
    if (!outputLink->functions->invokeSingle(outputLink, inputLink->iColorValues))
      return FALSE;
  }
  else {
    /* No output table - so have to use only the input table */
    switch (deviceSpace) {
    case SPACE_DeviceRGB:
    case SPACE_DeviceGray:
      for (i = 0; i < inputLink->n_iColorants; i++)
        inputLink->iColorValues[i] = 0.0;
      break;
    default:
      for (i = 0; i < inputLink->n_iColorants; i++)
        inputLink->iColorValues[i] = 1.0;
      break;
    }
  }

  /* Take an initial guess */
  if (!inputLink->functions->invokeSingle(inputLink, oXYZfloat))
    return FALSE;

  /* Force a/b values to 0 for CMYK profiles. Also limit the max value of L of the
   * black point estimate to 50.
   * At the moment, all colors returned from an ICC link are in CIEXYZ regardless
   * of the colorspace indicated by the link. In due course, this will change, but
   * for now, it's always CIEXYZ.
   */
  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    outputXYZ[i] = oXYZfloat[i];

  CIEXYZtoICCLab(outputXYZ, white, oLab);
  if (deviceSpace == SPACE_DeviceCMYK) {
    oLab[CC_CIELab_a] = 0;
    oLab[CC_CIELab_b] = 0;
  }
  if (oLab[CC_CIELab_L] > 50)
    oLab[CC_CIELab_L] = 50;
  ICCLabtoCIEXYZ(oLab, white, outputXYZ);

  return TRUE;
}

/** \todo JJ. Don't include for now because few profiles require it. */
#if 0
#define NUMBER_BPC_PROBES 33
#define BPC_PROBE_FRACT   0.5

static Bool roundTripEvaluate()
{
  /* Probe the gray scale to get a better luminance estimate */
  if (inputCRDLink && sourceLinks->pnext != NULL) {
    XYZVALUE bpc[NUMBER_BPC_PROBES];

    for (i = 0; i < NUMBER_BPC_PROBES; i++) {
      int32 j;
      for (j = 0; j < inputCRDLink->n_iColorants; j++)
        inputCRDLink->iColorValues[j] = sourceWhitePoint[j] * i / (NUMBER_BPC_PROBES - 1) * BPC_PROBE_FRACT;

      if (!inputCRDLink->functions->invokeSingle(inputCRDLink, sourceLinks->iColorValues))
        goto tidy;
      if (!sourceLinks->functions->invokeSingle(sourceLinks, sourceLinks->pnext->iColorValues) ||
          !sourceLinks->pnext->functions->invokeSingle(sourceLinks->pnext, oXYZ))
        goto tidy;

      bpc[i][CC_CIEXYZ_Y] = oXYZ[CC_CIEXYZ_Y];
      bpc[i][CC_CIEXYZ_X] = oXYZ[CC_CIEXYZ_X] / oXYZ[CC_CIEXYZ_Y];
      bpc[i][CC_CIEXYZ_Z] = oXYZ[CC_CIEXYZ_Z] / oXYZ[CC_CIEXYZ_Y];
    }
  }
}
#endif



/** \todo HACK ALERT
 * This is required to get at the internals of CLINKiccbased. Find a better way.
 */
#include "icmini.h"

/* Do Black Point Compensation if relevant data is available */
static Bool estimateSourceBlackPoint(CLINK       *sourceLinks,
                                     XYZVALUE    sourceRelativeWhitePoint,
                                     XYZVALUE    sourceBlackPoint)
{
  CLINK           *inputCRDLink = NULL;
  COLORSPACE_ID   deviceSpace;
  Bool            result = FALSE;

  /* Do Source BP estimation here */


  /* Get the RelativeColorimetric table from an ICC profile if present */
  if (sourceLinks->linkType == CL_TYPEiccbased) {
    int32           dummyDims;
    COLORSPACE_ID   dummyPCSSpace;
    COLORSPACE_ID   dummyColorSpaceId;
    OBJECT          *dummyPSColorSpace;
    XYZVALUE        *dummyDestWP;
    XYZVALUE        *dummyDestBP;
    XYZVALUE        *dummyDestRWP;
    XYZVALUE        *dummyDestRBP;

    if (!cc_get_icc_details(sourceLinks->p.iccbased->profile,
                            TRUE,
                            &dummyDims,
                            &deviceSpace,
                            &dummyPCSSpace))
      return FALSE;

    if (deviceSpace == SPACE_DeviceCMYK) {
      if (!cc_outputtransform_create(sourceLinks->p.iccbased->profile,
                                     NULL,
                                     SW_CMM_INTENT_PERCEPTUAL,
                                     &dummyPSColorSpace,
                                     &inputCRDLink,
                                     &dummyColorSpaceId,
                                     &dummyDims,
                                     &dummyDestWP,
                                     &dummyDestBP,
                                     &dummyDestRWP,
                                     &dummyDestRBP))
        goto tidy;
    }
  }
  else {
    /* We'll use the black point from the original colorspace */
    return TRUE;
  }

  if (!evaluateBlackPoint(inputCRDLink, sourceLinks, deviceSpace,
                          sourceRelativeWhitePoint, sourceBlackPoint))
    goto tidy;

  result = TRUE;

tidy:
  /* Free the links that might have been created */
  if (inputCRDLink != NULL)
    inputCRDLink->functions->destroy(inputCRDLink);

  return result;
}

static Bool estimateDestBlackPoint(CLINK       *destLink,
                                   XYZVALUE    destRelativeWhitePoint,
                                   XYZVALUE    destBlackPoint)
{
  CLINK           *outputCSALink = NULL;
  COLORSPACE_ID   deviceSpace;
  Bool            result = FALSE;

  HQASSERT(destLink->n_iColorants == NUMBER_XYZ_COMPONENTS, "Unexpected destLink n_iColorants");

  /* Get the RelativeColorimetric table from an ICC profile if present */
  if (destLink->linkType == CL_TYPEiccbasedoutput) {
    int32           dummyDims;
    COLORSPACE_ID   dummyPCSSpace;
    COLORSPACE_ID   dummyColorSpaceId;
    OBJECT          *dummyPSColorSpace;
    XYZVALUE        *dummySourceWP;
    XYZVALUE        *dummySourceBP;
    XYZVALUE        *dummySourceRWP;
    XYZVALUE        *dummySourceRBP;

    if (!cc_get_icc_details(destLink->p.iccbased->profile,
                            TRUE,
                            &dummyDims,
                            &deviceSpace,
                            &dummyPCSSpace))
      return FALSE;

    if ( !cc_iccbased_create( destLink->p.iccbased->profile,
                              NULL,
                              destLink->p.iccbased->intent,
                              &outputCSALink,
                              &dummyColorSpaceId,
                              &dummyDims,
                              &dummyPSColorSpace,
                              &dummySourceWP,
                              &dummySourceBP,
                              &dummySourceRWP,
                              &dummySourceRBP ) )
      return FALSE;

    if (outputCSALink == NULL)
      return FALSE;
  }
  else {
    /* We're not going to do BPC until more work is done */
    return FALSE;
  }

/** \todo JJ. If (profile type is not LUT based Gray/RGB/CMYK) */
  if (!estimateSourceBlackPoint(outputCSALink, destRelativeWhitePoint, destBlackPoint))
    goto tidy;

#if 0
  if (!evaluateBlackPoint(destLink, outputCSALink, deviceSpace,
                          *destRelativeWhitePoint, *destBlackPoint))
    goto tidy;
#endif
#if 0
  else {
    /* Take an initial guess */
    if (destLink->p.iccbased->intent == SW_CMM_INTENT_RELATIVE_COLORIMETRIC) {
      if (!estimateSourceBlackPoint(outputCSALink, destRelativeWhitePoint, initalBlackPoint))
        goto tidy:
    }
    else {
      for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
        initalBlackPoint[i] = 0.0;
    }
  }
#endif

  result = TRUE;

tidy:
  /* Free the links that might have been created */
  if (outputCSALink != NULL)
    outputCSALink->functions->destroy(outputCSALink);

  return result;
}

/* ------------------------------------------------------------------------ */

/* The hqnprofile color link. This handles profiles constructed from colorspaces
 * and crd's. It's intended to allow ad hoc collections of resources to be put
 * together and used in the manner of an ICC profile. Obviously, the entire set
 * of resources should normally be derived from the same characterisation data
 * for the set to make sense, but different collections can be put together
 * easily in postscript rather than being forced to use a fixed set within
 * an ICC profile.
 */

/* The maximum number of input and output tables in the profile. Chosen to match
 * an ICC profile for ease of selecting rendering intents.
 */
#define N_HQN_TABLES    (3)

struct HQN_PROFILE_INFO {
  cc_counter_t        refCnt;
  int8                n_device_colors; /* the number of device components */
  COLORSPACE_ID       devicespace;     /* the input colourspace */

  TRANSFORM_LINK_INFO dev2pcs[N_HQN_TABLES];    /* input links for the 3 intents */
  GS_CRDinfo          *pcs2dev[N_HQN_TABLES];   /* output crd's for the 3 intents */
};


/* The main chain has found a transform link info with an Hqn profile. This will
 * contain a pointer to another transform link info for the actual colorspace
 * placed in the profile. A color chain link will be created for one link, or a
 * sequence of links depending on the colorspace. The colorspace is required to
 * convert colors to one of the connection spaces, explicitly not a device space.
 */
static Bool cc_hqnprofileCSA_create(CREATEcmmxform *cmmxform,
                                    COLORSPACE_ID  *chainColorSpace,
                                    int32          *colorspacedimension)
{
  Bool status = FALSE;
  HQN_PROFILE_INFO *pInfo;
  TRANSFORM_LINK_INFO *origCurrentInfo;
  uint8 desiredIntent;
  OBJECT *outputPSColorSpace;

  HQASSERT(cmmxform->currentInfo->inputColorSpaceId == SPACE_HqnProfile,
           "Unexpected colorspace");

  pInfo = cmmxform->currentInfo->u.hqnprofile;
  desiredIntent = cmmxform->currentICCTable;

  /* Attempt to find the table for the desired intent */
  if (pInfo->dev2pcs[desiredIntent].u.shared == NULL) {
    /* Desired table not present, pick first one found */
    if (pInfo->dev2pcs[SW_CMM_INTENT_RELATIVE_COLORIMETRIC].u.shared != NULL)
      desiredIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    else if (pInfo->dev2pcs[SW_CMM_INTENT_PERCEPTUAL].u.shared != NULL)
      desiredIntent = SW_CMM_INTENT_PERCEPTUAL;
    else if (pInfo->dev2pcs[SW_CMM_INTENT_SATURATION].u.shared != NULL)
      desiredIntent = SW_CMM_INTENT_SATURATION;
    else {
      HQFAIL("Expected to find a CSA in Hqn profile");
      return FALSE;
    }
  }

  /* Trampoline from this HqnProfile info to the CSA that was provided within the
   * profile. This allows us to reuse the transformxxxx() functions, but we'll
   * need to reset the currentInfo when we're done so the main loop can continue
   * from the connection space.
   */
  origCurrentInfo = cmmxform->currentInfo;
  cmmxform->currentInfo = &pInfo->dev2pcs[desiredIntent];

  /* Create CSA Link from the value of the dev2pcs field, which should already
   * have been validated as a CSA.
   */
  while (cmmxform->currentInfo != NULL) {
    switch (cmmxform->currentInfo->inputColorSpaceId) {
    case SPACE_CIEBasedDEFG:
      status = transformCIEBasedDEFG(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CIETableABCD:
      status = transformCIETableABCD(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CIEBasedDEF:
      status = transformCIEBasedDEF(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CIETableABC:
      status = transformCIETableABC(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CIEBasedABC:
      status = transformCIEBasedABC(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CalRGB:
      status = transformCalRGB(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_Lab:
      status = transformLab(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CIETableA:
      status = transformCIETableA(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CIEBasedA:
      status = transformCIEBasedA(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_CalGray:
      status = transformCalGray(cmmxform, chainColorSpace, colorspacedimension);
      break;

    case SPACE_ICCBased:
      status = transformICCBased(cmmxform, chainColorSpace, colorspacedimension, &outputPSColorSpace);
      HQASSERT(outputPSColorSpace == NULL, "Unexpected outputPSColorSpace value");
      break;

    default:
      status = detail_error_handler(RANGECHECK, "Invalid colorspace type in Hqn profile");
    }

    if (!status)
      break;

    /* Some spaces will have an implicit follow-on color link, so create it here */
    cmmxform->currentInfo = cc_followOnTransformLinkInfo(cmmxform->currentInfo);
  }

  /* Reset the currentInfo for the main chain creation */
  cmmxform->currentInfo = origCurrentInfo;

  return status;
}

/* The main chain has found a transform link info with an Hqn profile used as a
 * rendering. This will contain a pointer to a CRD, so create the color link.
 */
static CLINK *cc_hqnprofileCRD_create(HQN_PROFILE_INFO  *pInfo,
                                      uint8             desiredIntent,
                                      XYZVALUE          **destWhitePoint,
                                      XYZVALUE          **destBlackPoint,
                                      XYZVALUE          **destRelativeWhitePoint,
                                      DEVICESPACEID     aimDeviceSpace,
                                      COLORSPACE_ID     *oColorSpace,
                                      int32             *dimensions,
                                      OBJECT            **nextColorSpaceObject)
{
  CLINK  *pLink;
  XYZVALUE dummySourceWhitePoint = {0.01, 0.01, 0.01};  /* Use dumb values */
  XYZVALUE dummySourceBlackPoint = {0.01, 0.01, 0.01};

  /* Attempt to find the table for the desired intent */
  if (pInfo->pcs2dev[desiredIntent] == NULL) {
    /* Desired table not present, pick first one found */
    if (pInfo->pcs2dev[SW_CMM_INTENT_RELATIVE_COLORIMETRIC] != NULL)
      desiredIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    else if (pInfo->pcs2dev[SW_CMM_INTENT_PERCEPTUAL] != NULL)
      desiredIntent = SW_CMM_INTENT_PERCEPTUAL;
    else if (pInfo->pcs2dev[SW_CMM_INTENT_SATURATION] != NULL)
      desiredIntent = SW_CMM_INTENT_SATURATION;
    else {
      HQFAIL("Expected to find a CRD in Hqn profile");
      return NULL;
    }
  }

  /* Create CRD Link from the value of the pcs2dev field, which should already
   * have been validated as a CRD.
   */
  pLink = cc_crd_create(pInfo->pcs2dev[desiredIntent],
                        dummySourceWhitePoint,
                        dummySourceBlackPoint,
                        destWhitePoint,
                        destBlackPoint,
                        destRelativeWhitePoint,
                        aimDeviceSpace,
                        FALSE,
                        nextColorSpaceObject,
                        oColorSpace,
                        dimensions);
  if ( pLink == NULL )
    return NULL;

  return pLink;
}

/* ------------------------------------------------------------------------ */

/* Create an Hqn profile structure. It's a simple dictionary that contains a
 * set of colorspaces for input, and a set of CRD's for output. Up to 3 items
 * are allowed for input and output, which are keyed by the same names as used
 * for the tables in an ICC profile.
 */
Bool cc_hqnprofile_createInfo(GS_COLORinfo      *colorInfo,
                              OBJECT            *profileObj,
                              HQN_PROFILE_INFO  **profileInfo,
                              int32             *validDimensions,
                              COLORSPACE_ID     *validDeviceSpace)
{
  int32 i;
  Bool intentPresent = FALSE;
  HQN_PROFILE_INFO  *pInfo;

  enum {
    profile_type, profile_input, profile_output, profile_n_entries
  };
  static NAMETYPEMATCH profile_match[profile_n_entries + 1] = {
    { NAME_ProfileType,               1, { OINTEGER }},
    { NAME_Input | OOPTIONAL,         1, { ODICTIONARY }},
    { NAME_Output,                    1, { ODICTIONARY }},
    DUMMY_END_MATCH
  };

  enum {
    intent_perceptual, intent_relativeColorimetric, intent_saturation, intent_n_entries
  };
  static NAMETYPEMATCH output_match[N_HQN_TABLES + 1] = {
    { NAME_Perceptual | OOPTIONAL,            1, { ODICTIONARY }},
    { NAME_RelativeColorimetric | OOPTIONAL,  1, { ODICTIONARY }},
    { NAME_Saturation | OOPTIONAL,            1, { ODICTIONARY }},
    DUMMY_END_MATCH
  };
  static NAMETYPEMATCH input_match[N_HQN_TABLES + 1] = {
    { NAME_Perceptual | OOPTIONAL,            1, { OARRAY }},
    { NAME_RelativeColorimetric | OOPTIONAL,  1, { OARRAY }},
    { NAME_Saturation | OOPTIONAL,            1, { OARRAY }},
    DUMMY_END_MATCH
  };

  /* The rendering intent enum values are assumed to be specific values that
   * match the ICC values for convenience */
  HQASSERT((int)SW_CMM_INTENT_PERCEPTUAL == (int)intent_perceptual &&
           (int)SW_CMM_INTENT_RELATIVE_COLORIMETRIC == (int)intent_relativeColorimetric &&
           (int)SW_CMM_INTENT_SATURATION == (int)intent_saturation &&
           N_HQN_TABLES == intent_n_entries,
           "Rendering intent enums dont match expected values");

  HQASSERT(oType(*profileObj) == ODICTIONARY, "Expected a dictionary");

  *profileInfo = NULL;

  if (!dictmatch(profileObj, profile_match))
    return FALSE;

  if (oInteger(*profile_match[profile_type].result) != 1)
    return detail_error_handler(RANGECHECK, "ProfileType isn't 1");

  pInfo = mm_sac_alloc(mm_pool_color,
                       sizeof(HQN_PROFILE_INFO),
                       MM_ALLOC_CLASS_NCOLOR);
  if (pInfo == NULL)
    return error_handler(VMERROR);

  pInfo->refCnt = 1;
  for (i = 0; i < N_HQN_TABLES; i++) {
    cc_initTransformInfo(&pInfo->dev2pcs[i]);
    pInfo->pcs2dev[i] = NULL;
  }

  /* Process the required Output tables */
  if (!dictmatch(profile_match[profile_output].result, output_match)) {
    cc_destroyhqnprofileinfo(&pInfo);
    return FALSE;
  }

  for (i = 0; i < N_HQN_TABLES; i++) {
    /* These crd's must be Type 1, so colorInfo can be set to NULL without effect
     * because it isn't used. Which is handy because we don't have access to it.
     */
    if (output_match[i].result != NULL) {
      int32 dimensions;
      COLORSPACE_ID colorSpaceId;

      if (!cc_setcolorrendering(NULL, &pInfo->pcs2dev[i],
                                output_match[i].result)) {
        cc_destroyhqnprofileinfo(&pInfo);
        return FALSE;
      }

      cc_crd_details(pInfo->pcs2dev[i], &dimensions, &colorSpaceId);

      /* Verify that all CRD's have the same device space and the same number
       * of colorants. We aren't going to the trouble of verifying that all
       * colorant names match, but we ought to. However, it's unlikely to be
       * used in a DeviceN mode so we won't bother for now.
       */
      if (*validDeviceSpace == SPACE_notset) {
        HQASSERT(*validDimensions == 0, "validDimensions already set");
        *validDeviceSpace = colorSpaceId;
        *validDimensions = dimensions;
      }
      else if (*validDeviceSpace != colorSpaceId || *validDimensions != dimensions) {
        cc_destroyhqnprofileinfo(&pInfo);
        return detail_error_handler(CONFIGURATIONERROR,
                                    "Hqn profile contains incompatible CRDs");
      }

      intentPresent = TRUE;
    }
  }

  if (!intentPresent) {
    cc_destroyhqnprofileinfo(&pInfo);
    return detail_error_handler(CONFIGURATIONERROR,
                                "Hqn profile doesn't contain a rendering");
  }

  /* Process the optional Input tables */
  if (profile_match[profile_input].result != NULL) {
    if (!dictmatch(profile_match[profile_input].result, input_match)) {
      cc_destroyhqnprofileinfo(&pInfo);
      return FALSE;
    }

    for (i = 0; i < N_HQN_TABLES; i++) {
      if (input_match[i].result != NULL) {
        int32 dimensions;
        COLORSPACE_ID colorSpaceId;

        /* The colorspace must be a CIE space, other restrictions applied below */
        if (!gsc_getcolorspacesizeandtype(colorInfo,
                                          input_match[i].result,
                                          &colorSpaceId, &dimensions) ||
            !ColorspaceIsCIEBased(colorSpaceId)) {
          cc_destroyhqnprofileinfo(&pInfo);
          return FALSE;
        }

        if (!cc_createTransformInfo(colorInfo,
                                    &pInfo->dev2pcs[i],
                                    input_match[i].result)) {
          cc_destroyhqnprofileinfo(&pInfo);
          return FALSE;
        }

        /* The colorspace isn't allowed to be a devicelink */
        if (cc_isDevicelink(&pInfo->dev2pcs[i])) {
          cc_destroyhqnprofileinfo(&pInfo);
          return detail_error_handler(CONFIGURATIONERROR,
                                      "Hqn profile contains devicelink color space");
        }

        /* Verify that all CSA's have the same number of colorants. We don't
         * bother verifying that the device space is the same because for we'd
         * need to extend the colorModel work for CIEBased & CIETable spaces.
         */
        if (*validDeviceSpace == SPACE_notset) {
          HQASSERT(*validDimensions == 0, "validDimensions already set");
          *validDeviceSpace = colorSpaceId;
          *validDimensions = dimensions;
        }
        else if (*validDimensions != dimensions) {
          cc_destroyhqnprofileinfo(&pInfo);
          return detail_error_handler(CONFIGURATIONERROR,
                                      "Hqn profile contains incompatible color spaces");
        }
      }
    }
  }

  *profileInfo = pInfo;

  return TRUE;
}

static void freehqnprofileinfo(HQN_PROFILE_INFO  *pInfo)
{
  int32 i;

  for (i = 0; i < N_HQN_TABLES; i++) {
    if (pInfo->dev2pcs[i].u.shared != NULL)
      cc_destroyTransformInfo(&pInfo->dev2pcs[i]);
    if (pInfo->pcs2dev[i] != NULL)
      cc_destroycrdinfo(&pInfo->pcs2dev[i]);
  }

  mm_sac_free(mm_pool_color, pInfo, sizeof(HQN_PROFILE_INFO));
}

void cc_destroyhqnprofileinfo(HQN_PROFILE_INFO  **pInfo)
{
  CLINK_RELEASE(pInfo, freehqnprofileinfo);
}

void cc_reservehqnprofileinfo(HQN_PROFILE_INFO  *pInfo)
{
  CLINK_RESERVE(pInfo);
}

/* ------------------------------------------------------------------------ */

void cc_initTransformInfo(TRANSFORM_LINK_INFO *transformInfo)
{
  transformInfo->inputColorSpaceId = SPACE_notset;
  transformInfo->outputColorSpaceId = SPACE_notset;
  transformInfo->u.shared = NULL;
  transformInfo->intent = SW_CMM_INTENT_DEFAULT;
  transformInfo->blackPointComp = FALSE;;
  transformInfo->next = NULL;
}

/* Identify minimal information about a potential colour link prior to creating
 * a colour chain. Making use of this minimal information allows us to hone a
 * colour chain, including splitting more complex chains up into sub-chains
 * inside the cmm colour link.
 */
Bool cc_createTransformInfo(GS_COLORinfo        *colorInfo,
                            TRANSFORM_LINK_INFO *pInfo,
                            OBJECT              *PSColorSpace)
{
  Bool status;
  int32 dimension;
  COLORSPACE_ID deviceSpace;
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;

  HQASSERT(pInfo->u.shared == NULL, "Info unitialised");

  if (!gsc_getcolorspacesizeandtype(colorInfo, PSColorSpace,
                                    &pInfo->inputColorSpaceId, &dimension))
    return FALSE;

  pInfo->outputColorSpaceId = SPACE_CIEXYZ;

  switch (pInfo->inputColorSpaceId) {

  case SPACE_CIEBasedDEFG:
    status = cc_createciebaseddefginfo(&pInfo->u.ciebaseddefg,
                                       &pInfo->outputColorSpaceId,
                                       PSColorSpace);
    HQASSERT(!status || pInfo->outputColorSpaceId == SPACE_CIEBasedABC,
             "Expected CIEBasedABC");
    break;

  case SPACE_CIETableABCD:
    status = cc_createcietableabcdinfo(&pInfo->u.cietableabcd,
                                       &pInfo->outputColorSpaceId,
                                       PSColorSpace,
                                       colorInfo);
    break;

  case SPACE_CIEBasedDEF:
    status = cc_createciebaseddefinfo(&pInfo->u.ciebaseddef,
                                      &pInfo->outputColorSpaceId,
                                      PSColorSpace);
    HQASSERT(!status || pInfo->outputColorSpaceId == SPACE_CIEBasedABC,
             "Expected CIEBasedABC");
    break;

  case SPACE_CIETableABC:
    status = cc_createcietableabcinfo(&pInfo->u.cietableabc,
                                      &pInfo->outputColorSpaceId,
                                      PSColorSpace,
                                      colorInfo);
    break;

  case SPACE_CIEBasedABC:
    status = cc_createciebasedabcinfo(&pInfo->u.ciebasedabc, PSColorSpace);
    break;

  case SPACE_CalRGB:
    status = cc_createcalrgbinfo(&pInfo->u.calrgb, PSColorSpace);
    break;

  case SPACE_Lab:
    status = cc_createlabinfo(&pInfo->u.lab, PSColorSpace);
    break;

  case SPACE_CIETableA:
    status = cc_createcietableainfo(&pInfo->u.cietablea,
                                    &pInfo->outputColorSpaceId,
                                    PSColorSpace,
                                    colorInfo);
    break;

  case SPACE_CIEBasedA:
    status = cc_createciebasedainfo(&pInfo->u.ciebaseda, PSColorSpace);
    break;

  case SPACE_CalGray:
    status = cc_createcalgrayinfo(&pInfo->u.calgray, PSColorSpace);
    break;

  case SPACE_ICCBased:
    status = cc_get_iccbased_profile_info(colorInfo,
                                          PSColorSpace,
                                          &pInfo->u.icc,
                                          &dimension,
                                          &deviceSpace,
                                          &pInfo->outputColorSpaceId);

    break;

  case SPACE_CMM:
    if (hcmsInfo == NULL) {
      HQFAIL("Expected non-NULL hcmsInfo for CMM colorspace");
      status = FALSE;
    }
    else
      status = cc_createcustomcmminfo(&pInfo->u.customcmm,
                                      &pInfo->outputColorSpaceId,
                                      colorInfo,
                                      PSColorSpace,
                                      cc_getAlternateCMM(hcmsInfo));
    break;

  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceGray:
  case SPACE_DeviceN:
  case SPACE_Separation:
    /* These can result from using devicelink's as intercept spaces or from
     * explicity setting intercepts or InputColorSpace to device spaces.
     */
    pInfo->outputColorSpaceId = SPACE_notset;
    status = TRUE;
    break;

  case SPACE_CIEXYZ:
  case SPACE_CIELab:
  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
  case SPACE_HqnPCS:
  case SPACE_HqnProfile:
  case SPACE_SoftMaskXYZ:
  default:
    HQFAIL("Unexpected colorspace id");
    status = error_handler(UNDEFINED);
    break;
  }

  return status;
}

void cc_destroyTransformInfo(TRANSFORM_LINK_INFO *pInfo)
{
  switch (pInfo->inputColorSpaceId) {

  case SPACE_CIEBasedDEFG:
    cc_destroyciebaseddefginfo(&pInfo->u.ciebaseddefg);
    break;

  case SPACE_CIETableABCD:
    cc_destroycietableabcdinfo(&pInfo->u.cietableabcd);
    break;

  case SPACE_CIEBasedDEF:
    cc_destroyciebaseddefinfo(&pInfo->u.ciebaseddef);
    break;

  case SPACE_CIETableABC:
    cc_destroycietableabcinfo(&pInfo->u.cietableabc);
    break;

  case SPACE_CIEBasedABC:
    cc_destroyciebasedabcinfo(&pInfo->u.ciebasedabc);
    break;

  case SPACE_CalRGB:
    cc_destroycalrgbinfo(&pInfo->u.calrgb);
    break;

  case SPACE_Lab:
    cc_destroylabinfo(&pInfo->u.lab);
    break;

  case SPACE_CIETableA:
    cc_destroycietableainfo(&pInfo->u.cietablea);
    break;

  case SPACE_CIEBasedA:
    cc_destroyciebasedainfo(&pInfo->u.ciebaseda);
    break;

  case SPACE_CalGray:
    cc_destroycalgrayinfo(&pInfo->u.calgray);
    break;

  case SPACE_ICCBased:
    /* Don't need to reserve or destroy ICC info's because they exist in their own cache */
    break;

  case SPACE_CMM:
    cc_destroycustomcmminfo(&pInfo->u.customcmm);
    break;

  case SPACE_CIEXYZ:
  case SPACE_CIELab:
    cc_destroycrdinfo(&pInfo->u.crd);
    break;

  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
    /* Don't need to reserve or destroy ICC info's because they exist in their own cache */
    break;

  case SPACE_HqnPCS:
  case SPACE_HqnProfile:
    cc_destroyhqnprofileinfo(&pInfo->u.hqnprofile);
    break;

  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceGray:
  case SPACE_DeviceN:
  case SPACE_Separation:
    /* These can result from using devicelink's as intercept spaces */
    break;

  case SPACE_SoftMaskXYZ:
    /* Falls out from soft mask luminosity chains */
    break;

  case SPACE_notset:
    /* Initialised state - it's easiest to ignore here */
    break;

  default:
    HQFAIL("Unexpected colorspace id");
    break;
  }

  cc_initTransformInfo(pInfo);

  return;
}


void cc_reserveTransformInfo(TRANSFORM_LINK_INFO *pInfo)
{
  switch (pInfo->inputColorSpaceId) {

  case SPACE_CIEBasedDEFG:
    cc_reserveciebaseddefginfo(pInfo->u.ciebaseddefg);
    break;

  case SPACE_CIETableABCD:
    cc_reservecietableabcdinfo(pInfo->u.cietableabcd);
    break;

  case SPACE_CIEBasedDEF:
    cc_reserveciebaseddefinfo(pInfo->u.ciebaseddef);
    break;

  case SPACE_CIETableABC:
    cc_reservecietableabcinfo(pInfo->u.cietableabc);
    break;

  case SPACE_CIEBasedABC:
    cc_reserveciebasedabcinfo(pInfo->u.ciebasedabc);
    break;

  case SPACE_CalRGB:
    cc_reservecalrgbinfo(pInfo->u.calrgb);
    break;

  case SPACE_Lab:
    cc_reservelabinfo(pInfo->u.lab);
    break;

  case SPACE_CIETableA:
    cc_reservecietableainfo(pInfo->u.cietablea);
    break;

  case SPACE_CIEBasedA:
    cc_reserveciebasedainfo(pInfo->u.ciebaseda);
    break;

  case SPACE_CalGray:
    cc_reservecalgrayinfo(pInfo->u.calgray);
    break;

  case SPACE_ICCBased:
    /* Don't need to reserve or destroy ICC info's because they exist in their own cache */
    break;

  case SPACE_CMM:
    cc_reservecustomcmminfo(pInfo->u.customcmm);
    break;

  case SPACE_CIEXYZ:
  case SPACE_CIELab:
    cc_reservecrdinfo(pInfo->u.crd);
    break;

  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
    /* Don't need to reserve or destroy ICC info's because they exist in their own cache */
    break;

  case SPACE_HqnPCS:
  case SPACE_HqnProfile:
    cc_reservehqnprofileinfo(pInfo->u.hqnprofile);
    break;

  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceGray:
  case SPACE_DeviceN:
  case SPACE_Separation:
    /* These can result from using devicelink's as intercept spaces */
    break;

  case SPACE_SoftMaskXYZ:
    /* Falls out from soft mask luminosity chains */
    break;

  case SPACE_notset:
    /* Initialised state - it's easiest to ignore here */
    break;

  default:
    HQFAIL("Unexpected colorspace id");
    break;
   }

  return;
}

Bool cc_sameColorSpaceFromTransformInfo(TRANSFORM_LINK_INFO *info1,
                                        TRANSFORM_LINK_INFO *info2)
{
  /* A hacky way of asserting that the tranform info structures are least valid */
  cc_reserveTransformInfo(info1);
  cc_reserveTransformInfo(info2);
  cc_destroyTransformInfo(info1);
  cc_destroyTransformInfo(info2);

  return info1->inputColorSpaceId == info2->inputColorSpaceId &&
         info1->outputColorSpaceId == info2->outputColorSpaceId &&
         info1->u.shared == info2->u.shared;
}

mm_size_t cc_sizeofTransformInfo()
{
  return sizeof (struct TRANSFORM_LINK_INFO);
}

/* ------------------------------------------------------------------------ */

/* Some colorspaces will have an implicit follow-on color link, return it here */
TRANSFORM_LINK_INFO *cc_followOnTransformLinkInfo(TRANSFORM_LINK_INFO *linkInfo)
{
  switch (linkInfo->inputColorSpaceId) {
  case SPACE_CIEBasedDEFG:
    return cc_nextCIEBasedDEFGInfo(linkInfo->u.ciebaseddefg);
  case SPACE_CIETableABCD:
    return cc_nextCIETableABCDInfo(linkInfo->u.cietableabcd);
  case SPACE_CIEBasedDEF:
    return cc_nextCIEBasedDEFInfo(linkInfo->u.ciebaseddef);
  case SPACE_CIETableABC:
    return cc_nextCIETableABCInfo(linkInfo->u.cietableabc);
  case SPACE_CIEBasedA:
    return cc_nextCIETableAInfo(linkInfo->u.cietablea);
  default:
    return NULL;
  }
}

static void copyXYZ(XYZVALUE destValue, XYZVALUE srcValue)
{
  int i;

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    destValue[i] = srcValue[i];
}


#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
int cc_countLinksInCMMXform(CLINK *pLink)
{
  int32 transformIdx;
  CLINKcmmxform  *cmmxform = pLink->p.cmmxform;
  int dummyChainCount = 0;
  int linkCount = 0;

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
    CLINK *currentLink = cmmxform->cmmTransform[transformIdx];
    cc_addCountsForOneChain(currentLink, &dummyChainCount, &linkCount);
  }

  return linkCount;
}
#endif    /* ASSERT_BUILD */
#endif    /* METRICS_BUILD */


/* Log stripped */

