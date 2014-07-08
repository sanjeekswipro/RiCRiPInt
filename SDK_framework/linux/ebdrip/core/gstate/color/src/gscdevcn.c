/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscdevcn.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support for PS deviceN Colors
 */

#include "core.h"
#include "gscdevcn.h"

#include "dlstate.h"            /* inputpage */
#include "gu_chan.h"            /* guc_colorantIndex */
#include "mm.h"                 /* mm_alloc */
#include "mm_core.h"            /* MM_ALLOC_CLASS_NCOLOR */
#include "namedef_.h"           /* NAME_DeviceGray */
#include "objects.h"            /* OBJECT outputSpaceObj */
#include "swerrors.h"           /* VMERROR */

#include "gs_colorpriv.h"       /* CLINK */
#include "gs_cache.h"           /* GSC_ENABLE_INTERCEPT_DEVICEN_CACHE */
#include "gs_cachepriv.h"       /* COC_FLAG_DISABLED */
#include "gschcmspriv.h"        /* cc_getMultipleNamedColors */
#include "gscheadpriv.h"        /* GS_CHAINinfo */
#include "gscparamspriv.h"      /* colorUserParams */
#include "gscphotoinkpriv.h"    /* guc_photoink_colorant */
#include "gscsmpxformpriv.h"    /* cc_csaGetSimpleTransform */
#include "gsctintpriv.h"        /* cc_nextTintTransformId */


/* Define specific slots for chains in these spaces */
enum {
  CMYK_CHAIN,
  RGB_CHAIN,
  GRAY_CHAIN,
  TRANSFORMED_SPOT_CHAIN,
  N_SPECIAL_CHAINS
};

/* Define an unreasonable color value to recognise as uninitialised. This helps
 * avoid awkwardness in initialising subtractive & additive values differently.
 */
#define UNITIALISED_COLOR_VALUE (-1000.0)

struct CLINKinterceptdevicen {
  /* The chainContext as required for volatile data in the invoke function */
  GS_CHAIN_CONTEXT *chainContext;

  /* A reference to the color cache. */
  COC_STATE *cocState;

  /* The positions of cmyk, rgb, and gray colorants within the DeviceN set.
   * Element 0 in cmykPositions gives the position of Cyan in the array so that
   * we know how to shuffle the colorants into cmyk order required for a cmyk
   * chain. Similarly for RGB and Gray.
   */
  int32 cmykPositions[4];
  int32 rgbPositions[3];
  int32 grayPositions[1];

  /* Are any of the cmyk, rgb, or gray colorants present in this DeviceN set */
  Bool cmykPresent;
  Bool rgbPresent;
  Bool grayPresent;

  /* An array of nColorants saying whether this colorant is renderable */
  Bool *isRenderable;

  /* An array of nColorants saying whether this colorant is intercepted via a
   * named color database */
  Bool *isIntercepted;

  /* An array of nColorants saying whether this colorant is neither a process,
   * renderable, or intercepted spot, so use the tint transform */
  Bool *isTransformedSpot;

  /* If we don't require any sub-chains, then we don't need this chain link */
  Bool allRenderable;

  /* Convenience bools telling whether any spot is renderable, intercepted
   * or transformed */
  Bool renderablePresent;
  Bool interceptedPresent;
  Bool transformedSpotPresent;

  /* Was the DeviceN created by our simple transform mechanism */
  GSC_SIMPLE_TRANSFORM *simpleTransform;
  Bool isSimpleTransform;

  /* Was one of the cmyk, rgb, or gray chains intercepted. If so, we'll need to
   * tell the main chain construction to set it's overprint control appropriately. */
  Bool subChainIntercepted;

  /* An array of nColorants alternate space objects. The individual CSAs are
   * allocated separately. */
  OBJECT **subChainCSAs;

  /* An array of nColorants tint transform objects. The individual tint transforms
   * are assigned only for intercepted colorants, i.e. taken from a named color
   * database. */
  OBJECT *tintTransforms;

  /* An array of nColorants for the colorant index of this colorant in the _device_
   * raster style. This helps determine whether the colorant is renderable. */
  COLORANTINDEX *deviceColorantIndices;

  /* The number of sub-chains that we have to take account of. We have to allow
   * for each colorant to be interceptable in it's own chain. Plus possible
   * chains for intercepting cmyk, rgb, gray. And one for the remaining transformed
   * spots. Not all of these chains can be used at any time, but it's convenient
   * to have a specific slot for each possible chain. */
  int32 nChains;

  /* An array of nChains of color chains. These are the chains that will be
   * invoked and the results merged. */
  GS_CHAINinfo **subChains;

  /* An array of nChains indicating whether the color values in final link of
   * each sub-chain is flipped before merging into the final output. The values
   * must be flipped if the final link of a sub-chain is additive because we have
   * to merge the values from each sub-chain in subtractive space. */
  Bool *subChainSubtractive;

  /* If the output space is additive, we have to flip the color values from the
   * subtractive space required for merging sub-chains. */
  Bool outputSubtractive;

  /* The total number of colorants in the output of the set of sub-chains,
   * including duplicates. This is required for the colorantMap. */
  int32 nMaxOutputColorants;

  /* An map from the nMaxOutputColorants color values from all sub-chains to
   * an output value slot. It makes it quick to map an output value from a
   * sub-chain to the merged output chain. The order of the map is the same as
   * the order of the values as they are encountered in the fixed order of
   * sub-chain processing. */
  int32 *colorantMap;

  /* A array of nMaxOutputColorants indices that contain the unique set of
   * colorants in the final links of all sub-chains. This array will be only
   * partially populated with nTotalOutputColorants values, but allocating
   * the full size is the easiest way. */
  COLORANTINDEX *uniqueColorants;

  /* The total number of unique colorants in the output of the set of
   * sub-chains, including duplicates. */
  int32 nTotalOutputColorants;

  /* A array of nMaxOutputColorants color values that contain the white points
   * of each colorant in each sub-chain as seen at the input of the last link.
   * The values are subtractive even if the sub-chain they belong to end in an
   * additive link */
  USERVALUE *whitePointsUnique;

  /* A array of nTotalOutputColorants color values that contain the maximum white
   * points of each unique final colorant across all sub-chains. This allows us
   * to initialise the values of each unique final colorant with its maximum
   * white point, whilst not carrying a non-zero white point from multiple
   * sub-chains into the output. If we were to do so, the result would look too
   * dark.
   * The values are subtractive even if the sub-chain they belong to end in an
   * additive link */
  USERVALUE *whitePointsMax;

  /* The output color space resulting from the merging of all sub-chains. This
   * includes an array of nTotalOutputColorants OBJECTS for the colorant list
   * in a DeviceN space, and an illegal tint transform that is never run because
   * it will cause an error if it is.
   * NB. The outputSpaceArray and illegalTintTransform are allocated to avoid
   * problems when copying this structure because other fields refer to them. */
  OBJECT outputSpaceObj;
  OBJECT *outputSpaceArray;       /* 4 */
  OBJECT *outputColorants;
  OBJECT *illegalTintTransform;   /* 1 */
};

static void interceptdevicen_destroy(CLINK *pLink);
static void destroyInternal(CLINKinterceptdevicen *interceptdevicen,
                            int32                 n_iColorants);
static Bool interceptdevicen_invokeSingle(CLINK       *pLink,
                                          USERVALUE   *oColorValues);
static mps_res_t interceptdevicen_scan(mps_ss_t ss,
                                       CLINK    *pLink);
static uint32 interceptdevicenStructSize(void);
static void interceptdevicenUpdatePtrs(CLINK                 *pLink,
                                       CLINKinterceptdevicen *newDevicen);
#if defined( ASSERT_BUILD )
static void interceptdevicenAssertions(CLINK *pLink);
#else
#define interceptdevicenAssertions(pLink)  EMPTY_STATEMENT()
#endif

static Bool init_interceptdevicen(CLINKinterceptdevicen *devicen,
                                  int32                 nColorants);

static Bool decideProcessChains(CLINKinterceptdevicen *devicen,
                                GS_COLORinfo          *colorInfo,
                                int32                 colorType,
                                GUCR_RASTERSTYLE      *hRasterStyle,
                                GS_CHAINinfo          *colorChain,
                                Bool                  fColorManage,
                                int32                 nColorants,
                                COLORANTINDEX         *colorants,
                                DEVICESPACEID         aimDeviceSpace,
                                int32                 nDeviceColorants,
                                Bool                  *useCMYKChain,
                                Bool                  *useRGBChain,
                                Bool                  *useGrayChain);

static Bool decideSeparationAttributes(CLINKinterceptdevicen *devicen,
                                       GS_COLORinfo          *colorInfo,
                                       GUCR_RASTERSTYLE      *hRasterStyle,
                                       Bool                  useCMYKChain,
                                       Bool                  useRGBChain,
                                       Bool                  useGrayChain,
                                       int32                 nColorants,
                                       COLORANTINDEX         *colorants,
                                       OBJECT                *colorNames);

static Bool buildSubChains(CLINKinterceptdevicen *devicen,
                           GS_COLORinfo          *colorInfo,
                           int32                 colorType,
                           GUCR_RASTERSTYLE      *hRasterStyle,
                           OBJECT                *PSColorSpace,
                           Bool                  fCompositing,
                           Bool                  useCMYKChain,
                           Bool                  useRGBChain,
                           Bool                  useGrayChain,
                           int32                 nColorants,
                           OBJECT                *colorNames,
                           DEVICESPACEID         aimDeviceSpace,
                           int32                 nDeviceColorants);

static Bool mergeColorsPrepare(CLINKinterceptdevicen *devicen,
                               GUCR_RASTERSTYLE      *hRasterStyle,
                               int32                 nColorants,
                               COLORANTINDEX         *colorants);

static Bool whitePointAdjPrepare(CLINK *pLink);


static CLINKfunctions interceptdevicen_functions = {
  interceptdevicen_destroy,
  interceptdevicen_invokeSingle,
  NULL,
  interceptdevicen_scan
};


/** cc_interceptdevicen_create handles most of the Separation & DeviceN cases of
 * chain construction. This is complicated by having several cases to consider.
 *
 * 1. The basic case from the PS/PDF specs is for a Separation/DeviceN where all
 *    colorants are either renderable or not. If they are we can render them
 *    without modification, otherwise we use the alternate space and tint
 *    transform. This case can be configured using the AdobeProcessSeparations
 *    userparam and by not settting named color interception. However, we don't
 *    set the userparam by default because users expect something more
 *    sophisticated.
 *
 * 2. Named color interception is done via named color databases which is handled
 *    in the tinttransform link. This is a documented Hqn extension widely used
 *    with color management to give a replacement alternate space & tint transform.
 *
 * 3. Process separations in color managed setups are expected to use CMYK color
 *    management with overprints, i.e. without backdrop rendering, using maxblit
 *    emulations for the unused process colorants; while BDR handles overprints
 *    in the backdrop; e.g. DeviceN [/Cyan /Magenta].
 *
 * 4. Mixtures of renderable and non-renderable colorants. The specs are clear that
 *    such color spaces should use the alternate space, but some users expect
 *    renderable colorants to go the their own separations where possible, which
 *    it is if we have a usable tint transform for the non-renderable colorants.
 *
 * 5. With backdrop rendering, we manufacture DeviceN color spaces for backdrops
 *    containing named colors, typically using simple tint transforms created
 *    from gscsmpxform.c. This will be used for the backdrop rendered portions
 *    of the page. However, for the direct rendered regions we try to create
 *    Separation/DeviceN spaces with only the colorants from the objects touching
 *    a pixel. That means there could be boundary artifacts from inconsistencies
 *    because Separation spaces in direct regions might well be intercepted using
 *    named color interception as per our extension, while the backdrop rendered
 *    regions won't.
 *
 * 6. To cope with points 3, 4, 5, we use this link for interceptdevicen which
 *    will break a Separation/DeviceN down into the components of CMYK, RGB, Gray,
 *    renderable spots, intercepted spots, and other spots. Each group will have
 *    color chains applied independently of each other and the results of each
 *    merged.
 *    The result is that process separations are color managed, renderable
 *    colorants are rendered where possible, artifacts in backdrop rendering are
 *    eliminated because color management of named colors is consistent between
 *    backdrop and direct rendered regions.
 *
 * 7. Control over whether we do the extra work to separate renderable colorants
 *    from non-renderable colorants is achieved with the MultipleNamedColors
 *    interceptcolorspace key. If the value is false, we only render colorants
 *    if all colorants in a DeviceN set are renderable, otherwise we use the
 *    tint transform.
 *
 * 8. Red, Green, and Blue separations are a special case, in that when they
 *    appear in a job we always treat them as a spot which is handled in the
 *    normal way of either using it as a spot in non-RGB setups that allow
 *    spots; or by using the tint transform otherwise.
 *    We also create internal DeviceN spaces containing Red, Green, and Blue
 *    when backdrop rendering from an RGB source backdrop. These are recognised
 *    and handled as RGB which is subject to RGB interception.
 */
Bool cc_interceptdevicen_create(GS_COLORinfo      *colorInfo,
                                int32             colorType,
                                GUCR_RASTERSTYLE  *hRasterStyle,
                                int32             nColorants,
                                COLORANTINDEX     *colorants,
                                DEVICESPACEID     aimDeviceSpace,
                                int32             nDeviceColorants,
                                GS_CHAINinfo      *colorChain,
                                Bool              fColorManage,
                                OBJECT            *PSColorSpace,
                                CLINK             **ppLink,
                                OBJECT            **oColorSpace,
                                Bool              *intercepted,
                                Bool              *renderable,
                                Bool              *applyTintTransform)
{
  int32 i;
  int32 chain;
  CLINK *pLink;
  CLINKinterceptdevicen tmp = {0};
  CLINKinterceptdevicen* devicen;
  Bool useCMYKChain;
  Bool useRGBChain;
  Bool useGrayChain;
  Bool dummyRenderableOnOutput;

  OBJECT *colorNames;
  int32 nTotalIds = 0;

  HQASSERT(colorInfo != NULL, "NULL colorInfo pointer");
  HQASSERT(hRasterStyle != NULL, "NULL hRasterStyle pointer");
  HQASSERT(nColorants > 0, "no colorants when expected");
  HQASSERT(colorants != NULL, "NULL colorants");
  HQASSERT(oColorSpace != NULL, "NULL oColorSpace");
  HQASSERT(intercepted != NULL, "NULL intercepted");
  HQASSERT(renderable != NULL, "NULL renderable");
  HQASSERT(applyTintTransform != NULL, "NULL applyTintTransform");

  *ppLink = NULL;
  *oColorSpace = PSColorSpace;
  *intercepted = FALSE;
  *renderable = FALSE;
  *applyTintTransform = FALSE;

  if (!init_interceptdevicen(&tmp, nColorants)) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  tmp.chainContext = colorChain->context;
  tmp.cocState = colorInfo->colorState->cocState;

  tmp.simpleTransform = cc_csaGetSimpleTransform(PSColorSpace);
  tmp.isSimpleTransform = (tmp.simpleTransform != NULL);

  /* Obtain the colorNames array from PSColorSpace */
  if (oType(oArray(*PSColorSpace)[1]) == OARRAY ||
      oType(oArray(*PSColorSpace)[1]) == OPACKEDARRAY) {
    colorNames = oArray(oArray(*PSColorSpace)[1]);
    if (nColorants != theLen(oArray(*PSColorSpace)[1])) {
      HQFAIL("Unexpected mismatch in number of colorants");
      destroyInternal(&tmp, nColorants);
      return error_handler(UNREGISTERED);
    }
  }
  else {
    if (nColorants != 1) {
      HQFAIL("Unexpected mismatch in number of colorants");
      destroyInternal(&tmp, nColorants);
      return error_handler(UNREGISTERED);
    }
    colorNames = &oArray(*PSColorSpace)[1];
    HQASSERT((oType(*colorNames) == ONAME || oType(*colorNames) == OSTRING),
             "Expected a single component colorant");
  }

  /* Establish whether the colorants are renderable on the output device OR if
   * CMYK equivalent values exist. Merely being a fully fledged colorant in a
   * backdrop raster style isn't enough to give reliable output.
   *
   * Get the colorant indices for the output raster style to determine if
   * they are renderable on the output device. We will then apply rules and
   * replace some with COLORANTINDEX_UNKNOWN if we don't think it's suitable
   * for interception. At the end of this, deviceColorantIndices is used to
   * test for renderable colorants.
   */
  if (!cc_colorspaceNamesToIndex(colorInfo->deviceRS,
                                 PSColorSpace,
                                 FALSE,
                                 FALSE,
                                 tmp.deviceColorantIndices,
                                 nColorants,
                                 &colorInfo->params.excludedSeparations,
                                 &dummyRenderableOnOutput)) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  if (guc_backdropRasterStyle(hRasterStyle)) {
    /* Do the non-renderable colorants have a good CMYK equivalent in the
     * target raster style. If any don't, use the normal tint transform.
     */
    for (i = 0; i < nColorants; i++) {
      /* NB. The All sep will force the standard tint transform, while the None
       * sep will not.
       */
      if (tmp.deviceColorantIndices[i] == COLORANTINDEX_NONE)
        tmp.deviceColorantIndices[i] = COLORANTINDEX_UNKNOWN;
      else if (tmp.deviceColorantIndices[i] == COLORANTINDEX_UNKNOWN)
        HQASSERT(tmp.deviceColorantIndices[i] == colorants[i], "Colorant mismatch");
      else {
        EQUIVCOLOR *equiv;

        HQASSERT(tmp.deviceColorantIndices[i] != COLORANTINDEX_ALL,
                 "Didn't expect an All separation");
        if (guc_getColorantName(colorInfo->deviceRS,
                                tmp.deviceColorantIndices[i]) == NULL) {
          if (guc_getCMYKEquivalents(hRasterStyle, colorants[i],
                                     &equiv, NULL) == NULL) {
            tmp.deviceColorantIndices[i] = COLORANTINDEX_UNKNOWN;
          }
        }
      }
    }
  }
  else {
    for (i = 0; i < nColorants; i++)
      HQASSERT(tmp.deviceColorantIndices[i] == colorants[i], "Colorant mismatch");
  }


  /* Decide whether we should use the CMYK, RGB, or Gray chain. Or none. */
  if (!decideProcessChains(&tmp, colorInfo, colorType,
                           hRasterStyle, colorChain, fColorManage,
                           nColorants, colorants,
                           aimDeviceSpace, nDeviceColorants,
                           &useCMYKChain, &useRGBChain, &useGrayChain)) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  /* Decide whether colorants are renderable, interceptable. */
  if (!decideSeparationAttributes(&tmp, colorInfo, hRasterStyle,
                                  useCMYKChain, useRGBChain, useGrayChain,
                                  nColorants, colorants, colorNames)) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  /* All colorants are treated as renderable spots => no sub-chains will be built
   * so no need for this chain link. Return to chain construction to continue.
   */
  if (tmp.allRenderable) {
    destroyInternal(&tmp, nColorants);
    /* Don't set applyTintTransform TRUE for this case */
    return TRUE;
  }

  /* Only one colorant and it's in a named color database OR only one sub-chain
   * handles all input colorants, it's more efficient to use a tint transform
   * link instead.
   */
  if (nColorants == 1 && tmp.interceptedPresent) {
    destroyInternal(&tmp, nColorants);
    *applyTintTransform = TRUE;
    return TRUE;
  }

  /* For the moment, if there are spots requiring the original tint transform,
   * we're going to use the normal tint transform to do the conversion because
   * the output can be bad in some circumstances, except for simple transforms
   * where we must already have CMYK equivalent values for each of the spots.
   * We will do the same unless all colorants are renderable on the output
   * device unless MultipleNamedColors is true. NB. if there is just one colorant
   * that is subject to interception by the tint transform link in the sub-chain.
   * This will be achieved by returning to the main chain construction and build
   * a tinttransform link there.
   */
  if ((tmp.transformedSpotPresent && !tmp.isSimpleTransform) ||
      ((tmp.transformedSpotPresent || tmp.interceptedPresent) &&
        !cc_getMultipleNamedColors(colorInfo->hcmsInfo))) {
    destroyInternal(&tmp, nColorants);
    *applyTintTransform = TRUE;
    return TRUE;
  }

  /* Build the color sub-chains as required for one process space, the
   * interceptable colorants, and the others in one chain. NB. Renderable spots
   * don't need a chain.
   */
  if (!buildSubChains(&tmp, colorInfo, colorType,
                      hRasterStyle, PSColorSpace,
                      colorChain->fCompositing,
                      useCMYKChain, useRGBChain, useGrayChain,
                      nColorants, colorNames,
                      aimDeviceSpace, nDeviceColorants)) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  /* Derive the set of colorants needed for the output space.
   * Create a DeviceN space for use when invoking the chain.
   */
  if (!mergeColorsPrepare(&tmp, hRasterStyle, nColorants, colorants)) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  /* Establish the number of CLID's that are needed for the set of sub-chains.
   * This is the sum of all clid's in all sub-chains except the last link in
   * each chain because we aren't going to invoke that link as part of this
   * interceptdevicen link.
   * The set of clid's must also include the set of input colorants.
   */
  for (chain = 0; chain < tmp.nChains; chain++) {
    if (tmp.subChains[chain] != NULL) {
      CLINK *subChainLink;

      /* Add the number of clid's from all links except the last */
      subChainLink = tmp.subChains[chain]->context->pnext;
      while (subChainLink->pnext != NULL) {
        nTotalIds += subChainLink->idcount;
        subChainLink = subChainLink->pnext;
      }
    }
  }
  nTotalIds += nColorants;


  /* Get link structure */
  pLink = cc_common_create(nColorants,
                           colorants,
                           SPACE_DeviceN,
                           SPACE_DeviceN,
                           CL_TYPEinterceptdevicen,
                           interceptdevicenStructSize(),
                           &interceptdevicen_functions,
                           nTotalIds);
  if (pLink == NULL) {
    destroyInternal(&tmp, nColorants);
    return FALSE;
  }

  interceptdevicenUpdatePtrs(pLink, &tmp);

  devicen = pLink->p.interceptdevicen;

  /* Now that the link has been created, it is possible to establish the white
   * point values by invoking it which is necessary for the real invoke calls.
   */
  if (!whitePointAdjPrepare(pLink)) {
    interceptdevicen_destroy(pLink);
    return FALSE;
  }

  /* Populate the XUID slots with the values from all sub-chains along with
   * the original colorant list.
   */
  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      CLINK *subChainLink;

      subChainLink = devicen->subChains[chain]->context->pnext;

      /* Copy nXUIDs in reverse order to make the assert easier */
      while (subChainLink->pnext != NULL) {
        for (i = 0; i < subChainLink->idcount; i++) {
          pLink->idslot[--nTotalIds] = subChainLink->idslot[i];
        }

        subChainLink = subChainLink->pnext;
      }
    }
  }
  /* Now include the colorants */
  for (i = 0; i < nColorants; i++)
    pLink->idslot[--nTotalIds] = (CLID)colorants[i];
  HQASSERT(nTotalIds == 0, "nTotalIds should be 0");


  *ppLink = pLink;

  *oColorSpace = &devicen->outputSpaceObj;

  *intercepted = devicen->subChainIntercepted;

  *renderable = !devicen->interceptedPresent &&
                !devicen->transformedSpotPresent;

  return TRUE;
}

static void interceptdevicen_destroy(CLINK* pLink)
{
  interceptdevicenAssertions(pLink);

  destroyInternal(pLink->p.interceptdevicen, pLink->n_iColorants);

  cc_common_destroy(pLink);
}

static void destroyInternal(CLINKinterceptdevicen *devicen,
                            int32                 n_iColorants)
{
  int32 chain;

  HQASSERT(devicen != NULL, "devicen NULL");

  if (devicen->isRenderable != NULL)
    mm_free(mm_pool_color, devicen->isRenderable, n_iColorants * sizeof(Bool));
  if (devicen->isIntercepted != NULL)
    mm_free(mm_pool_color, devicen->isIntercepted, n_iColorants * sizeof(Bool));
  if (devicen->isTransformedSpot != NULL)
    mm_free(mm_pool_color, devicen->isTransformedSpot, n_iColorants * sizeof(Bool));

  if (devicen->subChainCSAs != NULL) {
    for (chain = 0; chain < n_iColorants; chain++) {
      if (devicen->subChainCSAs[chain] != NULL)
        mm_free(mm_pool_color, devicen->subChainCSAs[chain], 5 * sizeof(OBJECT));
    }
    mm_free(mm_pool_color, devicen->subChainCSAs, n_iColorants * sizeof(OBJECT *));
  }
  if (devicen->tintTransforms != NULL)
    mm_free(mm_pool_color, devicen->tintTransforms, n_iColorants * sizeof(OBJECT));
  if (devicen->deviceColorantIndices != NULL)
    mm_free(mm_pool_color, devicen->deviceColorantIndices, n_iColorants * sizeof(COLORANTINDEX));

  if (devicen->subChains != NULL) {
    for (chain = 0; chain < devicen->nChains; chain++) {
      if (devicen->subChains[chain] != NULL)
        cc_destroyChain(&devicen->subChains[chain]);
    }
    mm_free(mm_pool_color, devicen->subChains, devicen->nChains * sizeof(CLINK *));
  }

  if (devicen->subChainSubtractive != NULL)
    mm_free(mm_pool_color, devicen->subChainSubtractive, devicen->nChains * sizeof(Bool));
  if (devicen->colorantMap != NULL)
    mm_free(mm_pool_color, devicen->colorantMap, devicen->nMaxOutputColorants * sizeof(Bool));
  if (devicen->uniqueColorants != NULL)
    mm_free(mm_pool_color, devicen->uniqueColorants, devicen->nMaxOutputColorants * sizeof(COLORANTINDEX));
  if (devicen->whitePointsUnique != NULL)
    mm_free(mm_pool_color, devicen->whitePointsUnique, devicen->nMaxOutputColorants * sizeof(USERVALUE));
  if (devicen->whitePointsMax != NULL)
    mm_free(mm_pool_color, devicen->whitePointsMax, devicen->nTotalOutputColorants * sizeof(USERVALUE));
  if (devicen->outputColorants != NULL)
    mm_free(mm_pool_color, devicen->outputColorants, devicen->nTotalOutputColorants * sizeof(OBJECT));

  if (devicen->outputSpaceArray != NULL)
    mm_free(mm_pool_color, devicen->outputSpaceArray, 4 * sizeof(OBJECT));
  if (devicen->illegalTintTransform != NULL)
    mm_free(mm_pool_color, devicen->illegalTintTransform, sizeof(OBJECT));
}

/* interceptdevicen_invokeSingle() - body is based on
 * cc_iterateChainSingle() except it skips the last link, the device code link,
 * as we need colorvalues when merging color managed process with the original
 * spot values.
 */
static Bool interceptdevicen_invokeSingle(CLINK      *pLink,
                                          USERVALUE  *oColorValues)
{
  int32 i;
  int32 chain;
  int32 positionInSet;
  CLINKinterceptdevicen *devicen;
  Bool useDeviceNCache;

  HQASSERT((oColorValues != NULL), "oColorValues NULL");
  interceptdevicenAssertions(pLink);

  /* Invoke the intercept link hanging off using settings for current link */
  devicen = pLink->p.interceptdevicen;

  useDeviceNCache = (devicen->chainContext->cacheFlags & GSC_ENABLE_INTERCEPT_DEVICEN_CACHE) != 0 &&
                    (devicen->chainContext->cacheFlags & GSC_ENABLE_COLOR_CACHE) != 0;

  for (chain = 0; chain < devicen->nChains; chain++) {

    if (devicen->subChains[chain] != NULL) {
      GS_CHAINinfo *subChain = devicen->subChains[chain];
      GS_CHAIN_CONTEXT *subChainCtxt = subChain->context;
      uint32 hashKey = 0;
      Bool cacheHit = FALSE;
      Bool useCache;
      int32 j;

      /* Copy input color values to start of sub-chain */
      switch (chain - pLink->n_iColorants) {
      case CMYK_CHAIN:
        for (j = 0; j < 4; j++) {
          if (devicen->cmykPositions[j] != IDEVN_POSN_UNKNOWN)
            subChain->iColorValues[j] = pLink->iColorValues[devicen->cmykPositions[j]];
          else
            subChain->iColorValues[j] = 0.0;
        }
        break;
      case RGB_CHAIN:
        for (j = 0; j < 3; j++) {
          if (devicen->rgbPositions[j] != IDEVN_POSN_UNKNOWN)
            /* Invert value because we're using DeviceN colors in as RGB (additive) */
            subChain->iColorValues[j] = 1.0f - pLink->iColorValues[devicen->rgbPositions[j]];
          else
            subChain->iColorValues[j] = 1.0;
        }
        break;
      case GRAY_CHAIN:
        HQASSERT(devicen->grayPositions[0] != IDEVN_POSN_UNKNOWN,
                "Invalid Gray colorant");
        /* Invert value because we're using DeviceN colors as Gray (additive) */
        subChain->iColorValues[0] = 1.0f - pLink->iColorValues[devicen->grayPositions[0]];
        break;
      case TRANSFORMED_SPOT_CHAIN:
        for (j = 0; j < pLink->n_iColorants; j++) {
          if (devicen->isTransformedSpot[j])
            subChain->iColorValues[j] = pLink->iColorValues[j];
          else
            subChain->iColorValues[j] = 0.0;
        }
        break;

      default:
        HQASSERT(chain < pLink->n_iColorants, "Unexpected number of color chains");
        HQASSERT(subChain->n_iColorants == 1, "Unexpected number of colorants");
        subChain->iColorValues[0] = pLink->iColorValues[chain];
        break;
      }

      useCache = useDeviceNCache &&
                 (subChainCtxt->cacheFlags & GSC_ENABLE_COLOR_CACHE) != 0;

      if (useCache) {
        if ( subChainCtxt->pCache == NULL ) {
          /* No cache pointer, cache not explicitly disabled for this chain, so go
             create it */
          if ( coc_head_create(devicen->cocState, subChain) )
            cacheHit = coc_lookup(subChain, &hashKey);
        } else {  /* Have a cache, invoke it */
          cacheHit = coc_lookup(subChain, &hashKey);
        }
      }

      /* Iterate the color chain up to penultimate link (i.e. one before device!) */
      if (!cacheHit) {
        CLINK *subChainLink = subChainCtxt->pnext;
        Bool result = TRUE;

        for (j = 0; j < subChain->n_iColorants; j++)
          subChainLink->iColorValues[j] = subChain->iColorValues[j];

        CLINK_RESERVE(subChain);

        subChainLink->overprintProcess = pLink->overprintProcess;
        while (subChainLink->pnext != NULL) {
          if (!(subChainLink->functions->invokeSingle)(subChainLink, subChainLink->pnext->iColorValues)) {
            result = FALSE;
            break;
          }
          subChainLink->pnext->overprintProcess = subChainLink->overprintProcess;
          subChainLink = subChainLink->pnext;
        }

        HQASSERT(!result || subChainLink->linkType == CL_TYPEdummyfinallink,
                 "Expected a dummy final link");

        if (!result) {
          cc_destroyChain(&subChain);
          return FALSE;
        }

        /* And populate the color cache */
        if (useCache) {
          if (subChainCtxt->pCache != NULL)
            coc_insert(subChain, hashKey);
        }

        cc_destroyChain(&subChain);
      }
    }
  }

  /* We have the converted colour values for all sub-chains. Now we merge
   * the values for each of the output colorants.
   */
  for (i = 0; i < devicen->nTotalOutputColorants; i++)
    oColorValues[i] = 0.0;
  positionInSet = 0;
  for (chain = 0; chain < pLink->n_iColorants; chain++) {
    if (devicen->isRenderable[chain]) {
      HQASSERT(devicen->subChains[chain] == NULL, "subChain non-NULL");
      /* The input is DeviceN, so subtractive. The output could be additive, so
       * flipping is required */
      if (devicen->subChainSubtractive[chain])
        oColorValues[devicen->colorantMap[positionInSet++]] = pLink->iColorValues[chain];
      else
        oColorValues[devicen->colorantMap[positionInSet++]] = 1.0f - pLink->iColorValues[chain];
    }
  }
  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      CLINK *subChainLink;

      subChainLink = devicen->subChains[chain]->context->pnext;
      while (subChainLink->pnext != NULL)
        subChainLink = subChainLink->pnext;

      /* Now get the color information from the final clink */
      HQASSERT(subChainLink->linkType == CL_TYPEdummyfinallink,
               "Expected a dummy final link");

      /* Flip the color values for additive colors because we have to merge the
       * colors from the sub-chains in a subtractive space.
       * Colour values are merged by multiplying values. This has the effect of
       * making every merge darker than the original values. And if only one
       * value is present then that is passed to the result.
       * Account is taken of the white point in this summation so that any non-
       * zero ink value for white isn't put in the result at this stage. We need
       * this to ensure that white is only put into the result once.
       */
      if (devicen->subChainSubtractive[chain]) {
        for (i = 0; i < subChainLink->n_iColorants; i++) {
          int32 outputPos = devicen->colorantMap[positionInSet];
          USERVALUE currentValue = oColorValues[outputPos];
          USERVALUE currentWhite = devicen->whitePointsUnique[positionInSet];
          currentValue += (1.0f - currentValue) *
                          (subChainLink->iColorValues[i] - currentWhite);
          oColorValues[devicen->colorantMap[positionInSet]] = currentValue;
          positionInSet++;
        }
      }
      else {
        for (i = 0; i < subChainLink->n_iColorants; i++) {
          int32 outputPos = devicen->colorantMap[positionInSet];
          USERVALUE currentValue = oColorValues[outputPos];
          USERVALUE currentWhite = devicen->whitePointsUnique[positionInSet];
          currentValue += (1.0f - currentValue) *
                          (1.0f - currentWhite - subChainLink->iColorValues[i]);
          oColorValues[devicen->colorantMap[positionInSet]] = currentValue;
          positionInSet++;
        }
      }
    }
  }
  HQASSERT(positionInSet == devicen->nMaxOutputColorants, "Inconsistent count");

  /* The white color value must now be added in just once because we discounted
   * it in the sub-chains. And the output values flipped if necessary.
   */
  if (devicen->outputSubtractive) {
    for (i = 0; i < devicen->nTotalOutputColorants; i++) {
      HQASSERT(devicen->whitePointsMax[i] != UNITIALISED_COLOR_VALUE,
               "Uninitialised color");
      oColorValues[i] += devicen->whitePointsMax[i];
      NARROW_01(oColorValues[i]);
    }
  }
  else {
    for (i = 0; i < devicen->nTotalOutputColorants; i++) {
      HQASSERT(devicen->whitePointsMax[i] != UNITIALISED_COLOR_VALUE,
               "Uninitialised color");
      oColorValues[i] = 1.0f - oColorValues[i] + devicen->whitePointsMax[i];
      NARROW_01(oColorValues[i]);
    }
  }

  return TRUE ;
}

#if 0
/* Commented out to suppress compiler warning */
static Bool cc_interceptdevicen_invokeBlock(CLINK       *pLink,
                                            CLINKblock  *p_block)
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  return TRUE ;
}
#endif /* 0 */

static mps_res_t interceptdevicen_scan(mps_ss_t  ss,
                                       CLINK     *pLink)
{
  CLINKinterceptdevicen *devicen;
  mps_res_t res = MPS_RES_OK;
  int32 chain;

  HQASSERT((pLink != NULL),
           "interceptdevicen_scan: NULL clink pointer");

  /* Scan the color management side chain only */
  devicen = pLink->p.interceptdevicen;

  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      res = cc_scan(ss, pLink->p.interceptdevicen->subChains[chain]->context->pnext);
      if (res != MPS_RES_OK)
        return res;
    }
  }

  return res;
}

static uint32 interceptdevicenStructSize(void)
{
  return sizeof(CLINKinterceptdevicen);
}

static void interceptdevicenUpdatePtrs(CLINK                 *pLink,
                                       CLINKinterceptdevicen *newDevicen)
{
  CLINKinterceptdevicen* devicen;

  pLink->p.interceptdevicen = (CLINKinterceptdevicen *)((uint8*) pLink + cc_commonStructSize(pLink));

  /* Hook up link specific structure */
  devicen = pLink->p.interceptdevicen;

  /* Copy local structure into gstate */
  *devicen = *newDevicen;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void interceptdevicenAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEinterceptdevicen,
                      interceptdevicenStructSize(),
                      &interceptdevicen_functions);

  HQASSERT(pLink->p.interceptdevicen == (CLINKinterceptdevicen *) ((uint8 *) pLink + cc_commonStructSize(pLink)),
           "transfer data not set");
}

#endif    /* ASSERT_BUILD */


/* Initialise the interceptdevicen structure */
static Bool init_interceptdevicen(CLINKinterceptdevicen *devicen,
                                  int32                 nColorants)
{
  int32 i;

  HQASSERT(devicen != NULL, "devicen NULL");

  devicen->nChains = nColorants + N_SPECIAL_CHAINS;

  devicen->isRenderable = mm_alloc(mm_pool_color,
                                   nColorants * sizeof(Bool),
                                   MM_ALLOC_CLASS_NCOLOR);
  devicen->isIntercepted = mm_alloc(mm_pool_color,
                                    nColorants * sizeof(Bool),
                                    MM_ALLOC_CLASS_NCOLOR);
  devicen->isTransformedSpot = mm_alloc(mm_pool_color,
                                        nColorants * sizeof(Bool),
                                        MM_ALLOC_CLASS_NCOLOR);
  devicen->subChainCSAs = mm_alloc(mm_pool_color,
                                   nColorants * sizeof(OBJECT *),
                                   MM_ALLOC_CLASS_NCOLOR);
  devicen->tintTransforms = mm_alloc(mm_pool_color,
                                     nColorants * sizeof(OBJECT),
                                     MM_ALLOC_CLASS_NCOLOR);
  devicen->deviceColorantIndices = mm_alloc(mm_pool_color,
                                            nColorants * sizeof(COLORANTINDEX),
                                            MM_ALLOC_CLASS_NCOLOR);
  devicen->subChains = mm_alloc(mm_pool_color,
                                devicen->nChains * sizeof(GS_CHAINinfo *),
                                MM_ALLOC_CLASS_NCOLOR);
  devicen->subChainSubtractive = mm_alloc(mm_pool_color,
                                          devicen->nChains * sizeof(Bool),
                                          MM_ALLOC_CLASS_NCOLOR);
  devicen->outputSpaceArray = mm_alloc(mm_pool_color,
                                       4 * sizeof(OBJECT),
                                       MM_ALLOC_CLASS_NCOLOR);
  devicen->illegalTintTransform = mm_alloc(mm_pool_color,
                                           sizeof(OBJECT),
                                           MM_ALLOC_CLASS_NCOLOR);

  /* These 2 allocations are special because their structures contain further
   * allocations so they have to be initialised immediately to prevent problems
   * in tidy up if an allocation fails.
   */
  if (devicen->subChainCSAs != NULL) {
    for (i = 0; i < nColorants; i++)
      devicen->subChainCSAs[i] = NULL;
  }
  if (devicen->subChains != NULL) {
    for (i = 0; i < devicen->nChains; i++)
      devicen->subChains[i] = NULL;
  }

  if (devicen->isRenderable == NULL ||
      devicen->isIntercepted == NULL ||
      devicen->isTransformedSpot == NULL ||
      devicen->subChainCSAs == NULL ||
      devicen->tintTransforms == NULL ||
      devicen->deviceColorantIndices == NULL ||
      devicen->subChains == NULL ||
      devicen->subChainSubtractive == NULL ||
      devicen->outputSpaceArray == NULL ||
      devicen->illegalTintTransform == NULL) {
    return error_handler(VMERROR);
  }

  devicen->chainContext = NULL;
  devicen->cocState = NULL;

  devicen->cmykPositions[0] = devicen->cmykPositions[1] =
    devicen->cmykPositions[2] = devicen->cmykPositions[3] = IDEVN_POSN_UNKNOWN;
  devicen->rgbPositions[0] = devicen->rgbPositions[1] =
    devicen->rgbPositions[2] = IDEVN_POSN_UNKNOWN;
  devicen->grayPositions[0] = IDEVN_POSN_UNKNOWN;

  devicen->cmykPresent = FALSE;
  devicen->rgbPresent = FALSE;
  devicen->grayPresent = FALSE;

  for (i = 0; i < nColorants; i++) {
    devicen->isRenderable[i] = FALSE;
    devicen->isIntercepted[i] = FALSE;
    devicen->isTransformedSpot[i] = FALSE;
  }

  devicen->allRenderable = FALSE;
  devicen->renderablePresent = FALSE;
  devicen->interceptedPresent = FALSE;
  devicen->transformedSpotPresent = FALSE;
  devicen->simpleTransform = NULL;
  devicen->isSimpleTransform = FALSE;
  devicen->subChainIntercepted = FALSE;

  for (i = 0; i < nColorants; i++) {
    /* subChainCSAs initialised above */
    devicen->tintTransforms[i] = onull;     /* Struct copy to set slot properties */
    devicen->deviceColorantIndices[i] = COLORANTINDEX_UNKNOWN;
  }

  /* nChains initialised above */

  for (i = 0; i < devicen->nChains; i++) {
    /* subChains initialised above */
    devicen->subChainSubtractive[i] = TRUE;
  }
  devicen->outputSubtractive = TRUE;

  devicen->nMaxOutputColorants = 0;
  devicen->colorantMap = NULL;
  devicen->uniqueColorants = NULL;
  devicen->nTotalOutputColorants = 0;

  devicen->whitePointsUnique = NULL;
  devicen->whitePointsMax = NULL;

  devicen->outputSpaceObj = onull;          /* Struct copy to set slot properties */
  for (i = 0; i < 4; i++)
    devicen->outputSpaceArray[i] = onull;   /* Struct copy to set slot properties */
  devicen->outputColorants = NULL;

  devicen->illegalTintTransform[0] = onull;    /* Struct copy to set slot properties */

  return TRUE;
}

/* Decide whether to use the cmyk, rgb, or gray color chains for interceptiion.
 * This is returned in useCMYKChain, useRGBChain, and useGrayChain.
 */
static Bool decideProcessChains(CLINKinterceptdevicen *devicen,
                                GS_COLORinfo          *colorInfo,
                                int32                 colorType,
                                GUCR_RASTERSTYLE      *hRasterStyle,
                                GS_CHAINinfo          *colorChain,
                                Bool                  fColorManage,
                                int32                 nColorants,
                                COLORANTINDEX         *colorants,
                                DEVICESPACEID         aimDeviceSpace,
                                int32                 nDeviceColorants,
                                Bool                  *useCMYKChain,
                                Bool                  *useRGBChain,
                                Bool                  *useGrayChain)
{
  int32 i;
  COLORANTINDEX cmykIndices[4];
  COLORANTINDEX rgbIndices[3];
  COLORANTINDEX grayIndices[1];
  Bool adobeProcessSeparations;

  HQASSERT(devicen != NULL, "devicen NULL");
  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");
  HQASSERT(colorChain != NULL, "colorChain NULL");
  HQASSERT(colorants != NULL, "PSColorSpace NULL");
  HQASSERT(useCMYKChain != NULL, "useCMYKChain NULL");
  HQASSERT(useRGBChain != NULL, "useRGBChain NULL");
  HQASSERT(useGrayChain != NULL, "useGrayChain NULL");

  /* Work out the number and order of process colorants and their mapping. We
   * only allow one process space to be handled. If the colorant list contains
   * more than one device space we'll handle the others as though they were
   * normal spots, e.g. [C M Y K Red Green Blue] will be treated as CMYK + Red
   * Green and Blue spots, not as CMYK + RGB.
   */
  if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                         cmykIndices, 4) ||
      !guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGB,
                                         rgbIndices, 3) ||
      !guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_Gray,
                                         grayIndices, 1)) {
    return FALSE;
  }

  for (i = 0; i < nColorants; i++) {
    int32 j;
    for (j = 0; j < 4; j++) {
      if (colorants[i] == cmykIndices[j]) {
        devicen->cmykPositions[j] = i;
        devicen->cmykPresent = TRUE;
        break;
      }
    }
  }
  for (i = 0; i < nColorants; i++) {
    int32 j;
    for (j = 0; j < 3; j++) {
      if (colorants[i] == rgbIndices[j]) {
        devicen->rgbPositions[j] = i;
        devicen->rgbPresent = TRUE;
        break;
      }
    }
  }
  for (i = 0; i < nColorants; i++) {
    if (colorants[i] == grayIndices[0]) {
      devicen->grayPositions[0] = i;
      devicen->grayPresent = TRUE;
    }
  }

  *useCMYKChain = FALSE;
  *useRGBChain = FALSE;
  *useGrayChain = FALSE;

  /* AdobeProcessSeparations is interpretation only. NB. fCompositing can be
   * set in the front end.
   */
  adobeProcessSeparations = colorInfo->params.adobeProcessSeparations &&
                            !colorChain->fCompositing;

  /* For CMYK, there is no point diverting through a separate chain, unless
   * we are color managing. In fact, doing so would be wrong for subsets of CMYK
   * because we'd lose any overprints, unless max blitting.
   */
  if (devicen->cmykPresent) {
    *useCMYKChain = !adobeProcessSeparations &&
                    (cc_getIntercept(colorInfo, colorType,
                                     SPACE_DeviceCMYK,
                                     fColorManage,
                                     colorChain->fCompositing,
                                     cc_incompleteChainIsSimple(colorChain)) != NULL);
  }

  /* RGB is different in that R, G, or B separations in a job should always be
   * treated as an unrenderable spot regardless of whether the PCM is RGB. This
   * is because all such devices we have dealt with so far are really CMY
   * devices. Therefore, we want the color to be transformed as though the
   * device really were CMY which means using the tint transform.
   * OTOH. If R, G, or B, isn't renderable, treat as ordiinary spots that will
   * be also be converted using the tint transform.
   *
   * Likewise, R, G, and B separations in a transparency job should be converted
   * via the alternate space regardless of whether the blend space is RGB.
   *
   * R, G, and B separations can also be used alongside C, M, Y, and K. We always
   * treat the R, G, B separations as normal spots in this case even for simple
   * transforms.
   *
   * The other major case is converting color from a backdrop. Such color spaces
   * are manufactured by our code using a simple transform as the tint transform.
   * Color spaces from a DeviceRGB blend space may be DeviceN containing R, G,
   * and B, plus any spots. The RGB set for these backdrops should be treated
   * as DeviceRGB with any spots in their own chains. It's also possible that
   * we'll only get a subset of RGB with overprinted objects due to a reduction
   * in transparency preconvert code. We'll treat subsets as DeviceRGB.
   */
  if (devicen->rgbPresent) {
    if (!devicen->cmykPresent &&
        devicen->isSimpleTransform &&
        cc_spacecache_PCMofInputRS(devicen->simpleTransform) == DEVICESPACE_RGB) {
      *useRGBChain = TRUE;
    }
    else {
      DEVICESPACEID deviceSpaceId;
      int32 dummyNColorants;

      guc_deviceColorSpace(hRasterStyle, &deviceSpaceId, &dummyNColorants);

      /* We've got R, G, or B separation with an RGB PCM. Mark it as non-renderable
       * so that it will be treated as a normal spot.
       */
      if (deviceSpaceId == DEVICESPACE_RGB || deviceSpaceId == DEVICESPACE_RGBK) {
        for (i = 0; i < 3; i++) {
          if (devicen->rgbPositions[i] != IDEVN_POSN_UNKNOWN)
            devicen->deviceColorantIndices[devicen->rgbPositions[i]] = COLORANTINDEX_UNKNOWN;
        }
      }
    }
  }

  /* For Gray, there is no point diverting through a separate chain, unless
   * we are color managing.
   */
  if (devicen->grayPresent && !devicen->cmykPresent && !devicen->rgbPresent) {
    *useGrayChain = !adobeProcessSeparations &&
                    (cc_getIntercept(colorInfo, colorType,
                                     SPACE_DeviceGray,
                                     fColorManage,
                                     colorChain->fCompositing,
                                     cc_incompleteChainIsSimple(colorChain)) != NULL);
  }

  /* We need to get overprints correct for a CMYK subset, e.g. with a DeviceN of
   * C + M, which turns out to not need to be colour converted, then we don't
   * want to use the CMYK chain because that will knockout of Y + K as well which
   * is undesirable. The only exception to this is if max blitting is in force
   * which only happens when intercepting CMYK without late color management, so
   * determine if that is the case.
   */
  if (*useCMYKChain) {
    GS_CHAINinfo *tmpColorChain = NULL;
    GS_CHAIN_CONTEXT *tmpChainCtxt = NULL;
    OBJECT colorSpace = OBJECT_NOTVM_NULL;
    COLORSPACE_ID colorSpaceId = SPACE_DeviceCMYK;
    int32 nInputColorants = 4;

    /* Construct the chain for CMYK in the same way that the CMYK sub-chain
     * will be built, and setting the same chain attributes.
     */
    if (!cc_createChainHead(&tmpColorChain, colorSpaceId,
                            nInputColorants, &colorSpace) ||
        !cc_createChainContextIfNecessary(&tmpColorChain->context, colorInfo))
      return FALSE;

    tmpColorChain->chainColorModel = REPRO_COLOR_MODEL_CMYK;
    tmpColorChain->fCompositing = colorChain->fCompositing;
    tmpColorChain->chainStyle = CHAINSTYLE_DUMMY_FINAL_LINK;

    tmpChainCtxt = tmpColorChain->context;

    if (!cc_constructChain(colorInfo, hRasterStyle, colorType,
                           FALSE, tmpColorChain,
                           &tmpChainCtxt->pnext,
                           &colorSpace,
                           colorSpaceId, nInputColorants,
                           aimDeviceSpace, nDeviceColorants,
                           SPACE_notset,
                           FALSE)) {
      return FALSE;
    }

    if (!tmpChainCtxt->fIntercepting)
      *useCMYKChain = FALSE;

    cc_destroyChain(&tmpColorChain);
  }

  return TRUE;
}

/* Decide whether colorants are renderable or interceptable by setting isRenderable,
 * isIntercepted, and isTransformedSpot appropriately.
 */
static Bool decideSeparationAttributes(CLINKinterceptdevicen *devicen,
                                       GS_COLORinfo          *colorInfo,
                                       GUCR_RASTERSTYLE      *hRasterStyle,
                                       Bool                  useCMYKChain,
                                       Bool                  useRGBChain,
                                       Bool                  useGrayChain,
                                       int32                 nColorants,
                                       COLORANTINDEX         *colorants,
                                       OBJECT                *colorNames)
{
  int32 i;
  int32 nRenderable = 0;
  int32 nTransformedSpots = 0;
  int32 nNone = 0;
  Bool renderable;
  COLORSPACE_ID calibrationColorSpace;

  HQASSERT(devicen != NULL, "devicen NULL");
  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");
  HQASSERT(colorants != NULL, "PSColorSpace NULL");
  HQASSERT(colorNames != NULL, "colorNames NULL");

  /* For the remaining spot colorants, establish which are renderable,
   * interceptable, and the others.
   */
  for (i = 0; i < nColorants; i++) {
    NAMECACHE *ntemp;

    if (colorants[i] == COLORANTINDEX_NONE) {
      nNone++;
      continue;
    }

    if (useCMYKChain) {
      if (devicen->cmykPositions[0] == i || devicen->cmykPositions[1] == i ||
          devicen->cmykPositions[2] == i || devicen->cmykPositions[3] == i)
        continue;
    }

    if (useRGBChain) {
      if (devicen->rgbPositions[0] == i || devicen->rgbPositions[1] == i ||
          devicen->rgbPositions[2] == i)
        continue;
    }

    if (useGrayChain) {
      if (devicen->grayPositions[0] == i)
        continue;
    }

    if (oType(colorNames[i]) == OSTRING) {
      ntemp = cachename(oString(colorNames[i]), theLen(colorNames[i]));
      if ( ntemp == NULL )
        return FALSE;
    } else {
      HQASSERT(oType(colorNames[i]) == ONAME, "colorant must be a String or Name");
      ntemp = oName(colorNames[i]);
    }

    renderable = FALSE;
    guc_calibrationColorSpace(hRasterStyle, &calibrationColorSpace);
    if (calibrationColorSpace != SPACE_notset) {
      if (guc_photoink_colorant(hRasterStyle,
                                guc_colorantIndexReserved(hRasterStyle, ntemp)))
        renderable = TRUE;
    }
    else if (guc_colorantIndex(hRasterStyle, ntemp) != COLORANTINDEX_UNKNOWN &&
             devicen->deviceColorantIndices[i] != COLORANTINDEX_UNKNOWN) {
      renderable = TRUE;
    }

    if (renderable) {
      devicen->isRenderable[i] = TRUE;
      devicen->renderablePresent = TRUE;
      nRenderable++;
    }
    else {
      OBJECT alternativeSpace = OBJECT_NOTVM_NOTHING;
      int32 tintTransformId =
        cc_nextTintTransformId(colorInfo->colorState->tintState);

      /** \todo JJ We need to get snooty about which databases we'll accept
       * here. We'll want to recalculate the equivalent colours for each new
       * raster style, so we need to make sure the alternativeSpace is a match
       * for the output raster style.
       */
      if (!gsc_invokeNamedColorIntercept(colorInfo,
                                         &colorNames[i],
                                         &devicen->isIntercepted[i],
                                         TRUE,
                                         &devicen->tintTransforms[i],
                                         &alternativeSpace,
                                         &tintTransformId)) {
        return FALSE;
      }

      if (devicen->isIntercepted[i])
        devicen->interceptedPresent = TRUE;
      else{
        devicen->isTransformedSpot[i] = TRUE;
        devicen->transformedSpotPresent = TRUE;
        nTransformedSpots++;
      }
    }
  }

  /* Allow an optimisation if all colorants are renderable spots or transformed spots */
  devicen->allRenderable = (nRenderable + nNone == nColorants);

  return TRUE;
}

/* Build the color chains as required for one process space, the interceptable
 * colorants, and the others in one chain. NB. Renderable spots don't need a
 * chain.
 */
static Bool buildSubChains(CLINKinterceptdevicen *devicen,
                           GS_COLORinfo          *colorInfo,
                           int32                 colorType,
                           GUCR_RASTERSTYLE      *hRasterStyle,
                           OBJECT                *PSColorSpace,
                           Bool                  fCompositing,
                           Bool                  useCMYKChain,
                           Bool                  useRGBChain,
                           Bool                  useGrayChain,
                           int32                 nColorants,
                           OBJECT                *colorNames,
                           DEVICESPACEID         aimDeviceSpace,
                           int32                 nDeviceColorants)
{
  int32 chain;
  OBJECT nullObj = onull; /* Struct copy to set slot properties */

  HQASSERT(devicen != NULL, "devicen NULL");
  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");
  HQASSERT(PSColorSpace != NULL, "PSColorSpace NULL");
  HQASSERT(colorNames != NULL, "colorNames NULL");

  for (chain = 0; chain < devicen->nChains; chain++) {
    Bool constructChain = FALSE;
    OBJECT *colorSpace = NULL;
    COLORSPACE_ID colorSpaceId = SPACE_notset;
    int32 nInputColorants = 0;
    REPRO_COLOR_MODEL chainColorModel = REPRO_COLOR_MODEL_INVALID;

    switch (chain - nColorants) {
    case CMYK_CHAIN:
      if (useCMYKChain) {
        colorSpace = &nullObj;
        colorSpaceId = SPACE_DeviceCMYK;
        nInputColorants = 4;
        chainColorModel = REPRO_COLOR_MODEL_CMYK;
        constructChain = TRUE;
      }
      break;
    case RGB_CHAIN:
      if (useRGBChain) {
        colorSpace = &nullObj;
        colorSpaceId = SPACE_DeviceRGB;
        nInputColorants = 3;
        chainColorModel = REPRO_COLOR_MODEL_RGB;
        constructChain = TRUE;
      }
      break;
    case GRAY_CHAIN:
      if (useGrayChain) {
        colorSpace = &nullObj;
        colorSpaceId = SPACE_DeviceGray;
        nInputColorants = 1;
        chainColorModel = REPRO_COLOR_MODEL_GRAY;
        constructChain = TRUE;
      }
      break;
    case TRANSFORMED_SPOT_CHAIN:
      if (devicen->transformedSpotPresent) {
        colorSpace = PSColorSpace;
        colorSpaceId = SPACE_DeviceN;
        nInputColorants = nColorants;
        chainColorModel = REPRO_COLOR_MODEL_NAMED_COLOR;
        constructChain = TRUE;
      }
      break;

    default:
      HQASSERT(chain < nColorants, "Unexpected number of color chains");

      if (devicen->isRenderable[chain]) {
        devicen->nMaxOutputColorants++;
      }
      else if (devicen->isIntercepted[chain]) {

        OBJECT *separationSpace;

        /* Allocate a block to hold both the CSA and the object referring to it. */
        devicen->subChainCSAs[chain] = mm_alloc(mm_pool_color,
                                                5 * sizeof(OBJECT),
                                                MM_ALLOC_CLASS_NCOLOR);
        if (devicen->subChainCSAs[chain] == NULL)
          return error_handler(VMERROR);

        /* Manufacture the CSA for the chain with this colorant, placing into
         * the pre-allocated array of objects.
         * Assign the array object to the first object, and the array itself to
         * the remaining 4 objects.
         */
        colorSpace = &devicen->subChainCSAs[chain][0];
        separationSpace = &devicen->subChainCSAs[chain][1];

        /* Manufacture the CSA for this separation */
        (void) object_slot_notvm(colorSpace);
        theTags(*colorSpace) = OARRAY | LITERAL | UNLIMITED;
        theLen(*colorSpace) = 4;
        oArray(*colorSpace) = separationSpace;

        object_store_name(object_slot_notvm(&separationSpace[0]), NAME_Separation, LITERAL);
        Copy(object_slot_notvm(&separationSpace[1]), &colorNames[chain]);
        object_store_name(object_slot_notvm(&separationSpace[2]), NAME_DeviceGray, LITERAL);

        /* Make the tint transform be an illegal procedure that errors when used.
         * We intend that the tint transform is never used.
         */
        (void) object_slot_notvm(&separationSpace[3]);
        theTags(separationSpace[3]) = OARRAY | EXECUTABLE | EXECUTE_ONLY;
        theLen(separationSpace[3]) = 1;
        oArray(separationSpace[3]) = devicen->illegalTintTransform;

        colorSpaceId = SPACE_Separation;
        nInputColorants = 1;
        chainColorModel = REPRO_COLOR_MODEL_NAMED_COLOR;
        constructChain = TRUE;
      }
      break;
    }

    if (constructChain) {
      GS_CHAINinfo *subChain;
      GS_CHAIN_CONTEXT *subChainCtxt;
      CLINK *subChainLink;

      /* Construct the chain for this colorant. We'll construct a new chain head
       * using the new color space. We won't allow interceptdevicen links to be
       * created in this chain (because of a check on constructionDepth), which
       * will be complete to the output device.
       */
      if (!cc_createChainHead(&devicen->subChains[chain], colorSpaceId,
                              nInputColorants, colorSpace) ||
          !cc_createChainContextIfNecessary(&devicen->subChains[chain]->context,
                                            colorInfo))
        return FALSE;

      /* Distinguish from normal chains. The associated caches are different so
       * they aren't interchangeable. Also inherit the other essential attributes.
       */
      subChain = devicen->subChains[chain];
      subChainCtxt = devicen->subChains[chain]->context;

      subChain->chainStyle = CHAINSTYLE_DUMMY_FINAL_LINK;
      subChain->chainColorModel = chainColorModel;
      subChain->fCompositing = fCompositing;

      if (!cc_constructChain(colorInfo, hRasterStyle, colorType,
                             FALSE, devicen->subChains[chain],
                             &subChainCtxt->pnext,
                             colorSpace,
                             colorSpaceId, nInputColorants,
                             aimDeviceSpace, nDeviceColorants,
                             SPACE_notset,
                             FALSE)) {
        return FALSE;
      }

      /* Is the final link additive or subtractive. It matters when it comes to
       * mixing the color values into the next link after the interceptdevicen.
       */
      subChainLink = subChainCtxt->pnext;
      while (subChainLink->pnext != NULL)
        subChainLink = subChainLink->pnext;

      HQASSERT(subChainLink->linkType == CL_TYPEdummyfinallink,
               "Expected a dummy final link");

      devicen->subChainSubtractive[chain] = ColorspaceIsSubtractive(subChainLink->iColorSpace);

      if (subChainCtxt->fIntercepting)
        devicen->subChainIntercepted = TRUE;

      devicen->nMaxOutputColorants += subChainLink->n_iColorants;
      HQASSERT(subChainLink->n_iColorants > 0, "Expected some colorants");
    }
  }
  HQASSERT(devicen->nMaxOutputColorants > 0, "Expected at least one sub-chain");

  return TRUE;
}

/* Derive the set of colorants needed for the output space.
 * Create a DeviceN space for use when invoking the chain.
 */
static Bool mergeColorsPrepare(CLINKinterceptdevicen *devicen,
                               GUCR_RASTERSTYLE      *hRasterStyle,
                               int32                 nColorants,
                               COLORANTINDEX         *colorants)
{
  int32 i;
  int32 chain;
  int32 positionInSet = 0;
  COLORANTINDEX cmykIndices[4];
  COLORANTINDEX rgbIndices[3];
  COLORANTINDEX grayIndices[1];
  Bool outputSubtractive = TRUE;

  HQASSERT(devicen != NULL, "devicen NULL");
  HQASSERT(hRasterStyle != NULL, "hRasterStyle NULL");
  HQASSERT(colorants != NULL, "colorants NULL");

  HQASSERT(devicen->nMaxOutputColorants > 0, "Expected some output colorants");

  /* Resolve the overall list of colorants provided to all the devicecode links
   * in the sub-chains to a list of unique colorants from which we can derive
   * manufactured DeviceN colorspace. Start with the maximum possible number of
   * colorants.
   * The total number of id slots can also be obtained with the same walk of the
   * sub-chains.
   */
  devicen->colorantMap = mm_alloc(mm_pool_color,
                                  devicen->nMaxOutputColorants * sizeof(int32),
                                  MM_ALLOC_CLASS_NCOLOR);
  devicen->uniqueColorants = mm_alloc(mm_pool_color,
                                      devicen->nMaxOutputColorants * sizeof(COLORANTINDEX),
                                      MM_ALLOC_CLASS_NCOLOR);
  devicen->whitePointsUnique = mm_alloc(mm_pool_color,
                                        devicen->nMaxOutputColorants * sizeof(USERVALUE),
                                        MM_ALLOC_CLASS_NCOLOR);
  if (devicen->colorantMap == NULL ||
      devicen->uniqueColorants == NULL ||
      devicen->whitePointsUnique == NULL)
    return error_handler(VMERROR);

  /* Initialise values */
  for (i = 0; i < devicen->nMaxOutputColorants; i++) {
    devicen->colorantMap[i] = 0;
    devicen->uniqueColorants[i] = COLORANTINDEX_UNKNOWN;
    devicen->whitePointsUnique[i] = UNITIALISED_COLOR_VALUE;
  }

  /* Walk over the sub-chains looking for the set of unique colorants in the
   * final, output, links from all sub-chains.
   */
  for (chain = 0; chain < nColorants; chain++) {
    if (devicen->isRenderable[chain]) {
      HQASSERT(devicen->subChains[chain] == NULL, "subChain non-NULL");

      /* We won't bother matching this renderable colorant with others because
       * there is a facility in the devicecode link to resolve duplicates.
       */
      devicen->colorantMap[positionInSet++] = devicen->nTotalOutputColorants;
      devicen->uniqueColorants[devicen->nTotalOutputColorants++] = colorants[chain];
    }
  }
  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      int32 j;
      CLINK *subChainLink;

      subChainLink = devicen->subChains[chain]->context->pnext;
      while (subChainLink->pnext != NULL)
        subChainLink = subChainLink->pnext;

      /* Now get the color information from the final clink */
      HQASSERT(subChainLink->linkType == CL_TYPEdummyfinallink,
               "Expected a dummy final link");

      for (i = 0; i < subChainLink->n_iColorants; i++) {
        for (j = 0; j < devicen->nTotalOutputColorants; j++) {
          if (subChainLink->iColorants[i] == devicen->uniqueColorants[j])
            break;
        }
        devicen->colorantMap[positionInSet++] = j;
        if (j == devicen->nTotalOutputColorants)
          devicen->uniqueColorants[devicen->nTotalOutputColorants++] = subChainLink->iColorants[i];
      }
    }
  }
  HQASSERT(positionInSet == devicen->nMaxOutputColorants, "Inconsistent count");
  HQASSERT(devicen->nTotalOutputColorants > 0, "Expected some output colorants");

  /* Final allocations now we know the value of nTotalOutputColorants */
  devicen->outputColorants = mm_alloc(mm_pool_color,
                                      devicen->nTotalOutputColorants * sizeof(OBJECT),
                                      MM_ALLOC_CLASS_NCOLOR);
  devicen->whitePointsMax = mm_alloc(mm_pool_color,
                                     devicen->nTotalOutputColorants * sizeof(USERVALUE),
                                     MM_ALLOC_CLASS_NCOLOR);
  if (devicen->outputColorants == NULL || devicen->whitePointsMax == NULL)
    return error_handler(VMERROR);

  /* Initialise values */
  for (i = 0; i < devicen->nTotalOutputColorants; i++) {
    devicen->outputColorants[i] = onothing;    /* Struct copy to set slot properties */
    devicen->whitePointsMax[i] = UNITIALISED_COLOR_VALUE;
  }


  /* Need these device space mappings below */
  if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                         cmykIndices, 4) ||
      !guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGB,
                                         rgbIndices, 3) ||
      !guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_Gray,
                                         grayIndices, 1)) {
    return FALSE;
  }

  /* Create the output colour space. Normally this is a DeviceN that we'll have
   * to manufacture, but optimise for the common device spaces where possible.
   */
  if (devicen->nTotalOutputColorants == 4 &&
      devicen->uniqueColorants[0] == cmykIndices[0] &&
      devicen->uniqueColorants[1] == cmykIndices[1] &&
      devicen->uniqueColorants[2] == cmykIndices[2] &&
      devicen->uniqueColorants[3] == cmykIndices[3]) {
    object_store_name(object_slot_notvm(&devicen->outputSpaceObj), NAME_DeviceCMYK, LITERAL);
  }
  else if (devicen->nTotalOutputColorants == 3 &&
      devicen->uniqueColorants[0] == rgbIndices[0] &&
      devicen->uniqueColorants[1] == rgbIndices[1] &&
      devicen->uniqueColorants[2] == rgbIndices[2]) {
    object_store_name(object_slot_notvm(&devicen->outputSpaceObj), NAME_DeviceRGB, LITERAL);
    outputSubtractive = FALSE;
  }
  else if (devicen->nTotalOutputColorants == 1 &&
      devicen->uniqueColorants[0] == grayIndices[0]) {
    object_store_name(object_slot_notvm(&devicen->outputSpaceObj), NAME_DeviceGray, LITERAL);
    outputSubtractive = FALSE;
  }
  else {
    /* Manufacture outputSpaceObj */
    (void) object_slot_notvm(&devicen->outputSpaceObj);
    theTags(devicen->outputSpaceObj) = OARRAY | LITERAL | UNLIMITED;
    theLen(devicen->outputSpaceObj) = 4;
    oArray(devicen->outputSpaceObj) = devicen->outputSpaceArray;

    object_store_name(object_slot_notvm(&devicen->outputSpaceArray[0]), NAME_DeviceN, LITERAL);

    (void) object_slot_notvm(&devicen->outputSpaceArray[1]);
    theTags(devicen->outputSpaceArray[1]) = OARRAY | LITERAL | UNLIMITED;
    theLen(devicen->outputSpaceArray[1]) = (uint16) devicen->nTotalOutputColorants;
    oArray(devicen->outputSpaceArray[1]) = devicen->outputColorants;

    object_store_name(object_slot_notvm(&devicen->outputSpaceArray[2]), NAME_DeviceGray, LITERAL);

    /* Make the tint transform be an illegal procedure that errors when used. The
     * intention being that it is never used.
     */
    (void) object_slot_notvm(&devicen->outputSpaceArray[3]);
    theTags(devicen->outputSpaceArray[3]) = OARRAY | EXECUTABLE | EXECUTE_ONLY;
    theLen(devicen->outputSpaceArray[3]) = 1;
    oArray(devicen->outputSpaceArray[3]) = devicen->illegalTintTransform;


    for (i = 0; i < devicen->nTotalOutputColorants; i++) {
      NAMECACHE *colorant;

      if (devicen->uniqueColorants[i] == COLORANTINDEX_NONE)
        colorant = system_names + NAME_None;
      else {
        colorant = guc_getColorantName(hRasterStyle,
                                       devicen->uniqueColorants[i]);
        /* NB. There is no requirement that all PCM colorants can be output */
        if (colorant == NULL)
          colorant = system_names + NAME_None;
      }

      devicen->outputColorants[i] = onothing;    /* Struct copy to set slot properties */
      theTags(devicen->outputColorants[i]) = ONAME | LITERAL;
      oName(devicen->outputColorants[i]) = colorant;
    }
  }

  /* If the output space is additive, we need to flip the output color values
   * because we assume the output space will be DeviceN unless it's changed here.
   */
   devicen->outputSubtractive = outputSubtractive;

  return TRUE;
}

/* Evaluate the white points of each sub-chain so that account can be taken of
 * the values in invokeSingle with the intention that we won't add the white
 * point for the same colorant multiple times. If we do the result will be too
 * dark.
 * Put the results in whitePointsUnique and whitePointsMax.
 */
static Bool whitePointAdjPrepare(CLINK *pLink)
{
  int32 i;
  int32 chain;
  int32 positionInSet = 0;
  CLINKinterceptdevicen *devicen;

  HQASSERT(pLink != NULL, "pLink NULL");

  devicen = pLink->p.interceptdevicen;
  HQASSERT(devicen != NULL, "devicen NULL");

  /* Initialise values to White for the DeviceN link inputs */
  for (i = 0; i < pLink->n_iColorants; i++)
    pLink->iColorValues[i] = 0.0;

  /* Initialise values to White for the sub-chain outputs which are always
   * subtractive even if the sub-chain concerned ends in additive.
   */
  for (i = 0; i < devicen->nMaxOutputColorants; i++)
    devicen->whitePointsUnique[i] = 0.0;

  /* Evaluate the white value through all sub-chains by invoking the link with
   * the white values. The iColorValues have previously been initialised to 0.0,
   * so the input values in the final link of each sub-chain are the white
   * point values of that sub-chain.
   * As something of a hack, whitePointsMax will be used as a scratch area for
   * the output values of the link, but is not otherwise used yet.
   */
  if (!pLink->functions->invokeSingle(pLink, devicen->whitePointsMax))
    return FALSE;

  /* Re-initialise whitePointsMax after it's use as a scratch */
  for (i = 0; i < devicen->nTotalOutputColorants; i++)
    devicen->whitePointsMax[i] = UNITIALISED_COLOR_VALUE;

  /* Step through the sub-chains and extract the white point values and place
   * into whitePointsUnique.
   */
  for (chain = 0; chain < pLink->n_iColorants; chain++) {
    if (devicen->isRenderable[chain]) {
      int32 outputPos = devicen->colorantMap[positionInSet];

      HQASSERT(devicen->subChains[chain] == NULL, "subChain non-NULL");

      /* There is no sub-chain, and we don't merge renderable color values so we
       * just need to keep track of the slot to write to.
       */
      devicen->whitePointsMax[outputPos] = 0.0;
      positionInSet++;
    }
  }
  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      CLINK *subChainLink;

      subChainLink = devicen->subChains[chain]->context->pnext;
      while (subChainLink->pnext != NULL)
        subChainLink = subChainLink->pnext;

      /* Now get the color information from the final clink */
      HQASSERT(subChainLink->linkType == CL_TYPEdummyfinallink,
               "Expected a dummy final link");

      for (i = 0; i < subChainLink->n_iColorants; i++) {
        USERVALUE currentValue = subChainLink->iColorValues[i];
        int32 outputPos = devicen->colorantMap[positionInSet];

        /* The white values are always subractive by our definition */
        if (!devicen->subChainSubtractive[chain])
          currentValue = 1.0f - currentValue;

        devicen->whitePointsUnique[positionInSet] = currentValue;

        if (devicen->whitePointsMax[outputPos] == UNITIALISED_COLOR_VALUE)
          devicen->whitePointsMax[outputPos] = currentValue;
        else if (currentValue > devicen->whitePointsMax[outputPos])
          devicen->whitePointsMax[outputPos] = currentValue;

        positionInSet++;
      }
    }
  }
  HQASSERT(positionInSet == devicen->nMaxOutputColorants, "Inconsistent count");

  return TRUE;
}

/* Is this DeviceN space suitable for the ChainCache. It is if it doesn't rely
 * on PS VM.
 */
Bool cc_devicenCachable(CLINK *pLink)
{
  CLINKinterceptdevicen *devicen;

  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pLink->linkType == CL_TYPEinterceptdevicen, "Expected an interceptdevicen link");

  devicen = pLink->p.interceptdevicen;

  if (devicen->interceptedPresent)
    return FALSE;

  if (devicen->transformedSpotPresent && !devicen->isSimpleTransform)
    return FALSE;

  return TRUE;
}

GSC_SIMPLE_TRANSFORM *cc_devicenSimpleTransform(CLINK *pLink)
{
  CLINKinterceptdevicen *devicen ;

  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pLink->linkType == CL_TYPEinterceptdevicen, "Expected an interceptdevicen link");

  devicen = pLink->p.interceptdevicen ;
  HQASSERT(devicen != NULL , "Somehow lost CLINKinterceptdevicen" ) ;

  return devicen->simpleTransform;
}

/* The function returns the sub-chain for the process space that corresponds to
 * the color model.
 * Useful for the black preservation functionality.
 */
GS_CHAINinfo *cc_getCMMChain(CLINK             *xformContainerLink,
                             REPRO_COLOR_MODEL colorModel)
{
  int32 chain = -1;
  int32 cmykChain;
  int32 rgbChain;
  int32 grayChain;
  int32 nProcessChains = 0;
  CLINKinterceptdevicen *devicen;

  HQASSERT(xformContainerLink != NULL, "xformContainerLink NULL");
  HQASSERT(xformContainerLink->linkType == CL_TYPEinterceptdevicen,
           "Expected a devicn intercept link");

  devicen = xformContainerLink->p.interceptdevicen;

  cmykChain = xformContainerLink->n_iColorants + CMYK_CHAIN;
  rgbChain = xformContainerLink->n_iColorants + RGB_CHAIN;
  grayChain = xformContainerLink->n_iColorants + GRAY_CHAIN;

  if (devicen->subChains[cmykChain] != NULL) {
    chain = cmykChain;
    nProcessChains++;
  }
  if (devicen->subChains[rgbChain] != NULL) {
    chain = rgbChain;
    nProcessChains++;
  }
  if (devicen->subChains[grayChain] != NULL) {
    chain = grayChain;
    nProcessChains++;
  }

  HQASSERT(nProcessChains <= 1, "Too many process chains");

  switch (colorModel) {
    case REPRO_COLOR_MODEL_CMYK:
    case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
    case REPRO_COLOR_MODEL_NAMED_COLOR:
      HQASSERT(chain == -1 || chain == cmykChain, "cmyk color model should have cmyk chain");
      break;
    case REPRO_COLOR_MODEL_RGB:
    case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
      HQASSERT(chain == -1 || chain == rgbChain, "rgb color model should have rgb chain");
      break;
    case REPRO_COLOR_MODEL_GRAY:
    case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
      HQASSERT(chain == -1 || chain == grayChain, "gray color model should have gray chain");
      break;
    default:
      HQFAIL("Unexpected color model");
      return NULL;
  }

  if (chain == -1)
    return NULL;
  else
    return devicen->subChains[chain];
}

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
int cc_countLinksInDeviceN(CLINK *pLink)
{
  int chain;
  CLINKinterceptdevicen  *devicen = pLink->p.interceptdevicen;
  int dummyChainCount = 0;
  int linkCount = 0;

  devicen = pLink->p.interceptdevicen;

  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      GS_CHAINinfo *subChain = devicen->subChains[chain];
      CLINK *pLink = subChain->context->pnext;
      cc_addCountsForOneChain(pLink, &dummyChainCount, &linkCount);
    }
  }

  return linkCount;
}

int cc_countSubChainsInDeviceN(CLINK *pLink)
{
  int chain;
  CLINKinterceptdevicen  *devicen = pLink->p.interceptdevicen;
  int chainCount = 0;
  int dummyLinkCount = 0;

  devicen = pLink->p.interceptdevicen;

  for (chain = 0; chain < devicen->nChains; chain++) {
    if (devicen->subChains[chain] != NULL) {
      GS_CHAINinfo *subChain = devicen->subChains[chain];
      CLINK *pLink = subChain->context->pnext;
      cc_addCountsForOneChain(pLink, &chainCount, &dummyLinkCount);
    }
  }

  return chainCount;
}
#endif    /* ASSERT_BUILD */
#endif    /* METRICS_BUILD */


/* Log stripped */
