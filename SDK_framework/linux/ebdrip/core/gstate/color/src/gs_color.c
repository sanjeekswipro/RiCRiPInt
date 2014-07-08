/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gs_color.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to set up colour chains.
 */

#include "core.h"

#include "blitcolort.h"         /* BLIT_MAX_COLOR_CHANNELS */
#include "ciepsfns.h"           /* CIE_PROCTABLE_SIZE - depends on graphics.h for CIECallBack */
#include "control.h"            /* interpreter */
#include "dicthash.h"           /* fast_sys_extract_hash */
#include "dictops.h"            /* walk_dictionary */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "dlstate.h"            /* page->color */
#include "gcscan.h"             /* ps_scan_field */
#include "gu_chan.h"            /* guc_colorantIndex */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "mmcompat.h"           /* mm_alloc_with_header */
#include "monitor.h"            /* monitorf */
#include "metrics.h"            /* sw_metrics_open_group */
#include "mm_core.h"            /* NUMBERSAVES */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* oType */
#include "objmatch.h"           /* OBJECT_MATCH */
#include "pattern.h"            /* UNCOLOURED_PATTERN */
#include "rcbcntrl.h"           /* rcbn_intercepting */
#include "spdetect.h"           /* enable_separation_detection */
#include "swerrors.h"           /* RANGECHECK */

#include "gs_cachepriv.h"       /* coc_init */
#include "gs_chaincachepriv.h"  /* cc_countChainsinChainCache */
#include "gs_tablepriv.h"       /* cc_createTomsTable */
#include "gscblackevaluate.h"   /* cc_blackevaluate_create */
#include "gscblackremove.h"     /* cc_blackreplace_create */
#include "gscblackreplace.h"    /* cc_blackreplace_create */
#include "gsccalibpriv.h"       /* cc_arecalibrationobjectslocal */
#include "gsccie.h"             /* cc_createciebaseddefginfo */
#include "gsccmmpriv.h"         /* cc_cmmxform_create, cc_getFinalXYZLink */
#include "gscalternatecmmpriv.h"/* cc_createcmminfo */
#include "gsccrdpriv.h"         /* cc_arecrdobjectslocal */
#include "gscdevcipriv.h"       /* cc_devicecode_create */
#include "gscdevcn.h"           /* cc_interceptdevicen_create */
#include "gschcmspriv.h"        /* cc_getCMYKInterceptInfo */
#include "gscheadpriv.h"        /* GS_CHAINinfo */
#include "gschtonepriv.h"       /* cc_destroyhalftoneinfo */
#include "gschtone.h"           /* gsc_getSpotno */
#include "gscicc.h"             /* cc_get_iccbased_profile_info */
#include "gscindex.h"           /* cc_indexed_create */
#include "gscinitpriv.h"        /* cc_colorInfo_init */
#include "gscluminositypriv.h"  /* cc_luminosity_create */
#include "gscparamspriv.h"      /* colorUserParams */
#include "gscpdfpriv.h"         /* cc_createcalrgbinfo */
#include "gscphotoinkpriv.h"    /* guc_photoink_colorant */
#include "gscpresp.h"           /* cc_preseparation_create */
#include "gscsmplkpriv.h"       /* cc_reservergbtocmykinfo */
#include "gscsmpxformpriv.h"    /* cc_csaGetSimpleTransform */
#include "gsctable.h"           /* cc_cietableabcd_create */
#include "gsctintpriv.h"        /* cc_tinttransform_create */
#include "gscxferpriv.h"        /* cc_destroytransferinfo */
#include "gscfastrgbconvpriv.h" /* cc_fastrgb2gray_create */

#include "gs_colorpriv.h"


/* The maximum recursion level of color chains that results from, e.g. DeviceN
 * spaces used as tint transforms or alternate spaces. */
#define MAX_CONSTRUCTION_DEPTH    (16)

static Bool cc_invokeChainTransform(GS_COLORinfo   *colorInfo,
                                    int32          colorType,
                                    USERVALUE      *iColorValues,
                                    int32          nColors,
                                    OBJECT         *colorSpaceObj,
                                    COLORSPACE_ID  iColorSpace,
                                    COLORSPACE_ID  oColorSpace,
                                    Bool           forCurrentCMYKColor,
                                    CLINK          **pFirstLink,
                                    USERVALUE      *oColorValues);

static void destroyTransformInfoItem(GS_CONSTRUCT_CONTEXT *context, int32 transformIdx);
static void destroyTransformInfoList(GS_CONSTRUCT_CONTEXT *context);

static Bool cc_constructLuminosityChain(GS_CONSTRUCT_CONTEXT  *context);

static Bool cc_constructPreseparationChain(GS_COLORinfo         *colorInfo,
                                           int32                colorType,
                                           GS_CONSTRUCT_CONTEXT *context,
                                           GS_CHAINinfo         *colorChain,
                                           CLINK                **pHeadLink,
                                           OBJECT               *IndexedBasePSColorSpace);

static Bool cc_constructDeviceChain(GS_COLORinfo          *colorInfo,
                                    GUCR_RASTERSTYLE      *hRasterStyle,
                                    int32                 colorType,
                                    GS_CHAINinfo          *colorChain,
                                    CLINK                 **pHeadLink,
                                    GS_CONSTRUCT_CONTEXT  *context,
                                    OBJECT                *IndexedBasePSColorSpace,
                                    DEVICECODE_TYPE       deviceCodeType);

static Bool cc_constructNonInterceptChain(GS_COLORinfo          *colorInfo,
                                          GUCR_RASTERSTYLE      *hRasterStyle,
                                          int32                 colorType,
                                          GS_CHAINinfo          *colorChain,
                                          CLINK                 **pHeadLink,
                                          GS_CONSTRUCT_CONTEXT  *context);

static Bool createFinalDeviceNColorSpace(GS_CHAIN_CONTEXT *chainContext,
                                         GUCR_RASTERSTYLE *hRasterStyle,
                                         COLORANTINDEX **pColorantIndexArray,
                                         int32 *colorantIndexArraySize,
                                         int32 nDeviceColorants,
                                         Bool fNonInterceptingChain,
                                         OBJECT *PSColorSpace);

static Bool dummyInvokeSingle(CLINK *pLink, USERVALUE *oColorValues);

static Bool cc_hasIndependentChannels(GS_CHAINinfo *colorChain,
                                      Bool exactChannelMatch,
                                      Bool chainIsComplete);
static void cc_initEraseColorRoot(void);

static Bool extendColorantIndexArray ( COLORANTINDEX **pColorantIndexArray,
                                       int32 *pColorantIndexArraySize,
                                       int32 newColorantIndexArraySize ) ;

static Bool getallseparationcolorants( GUCR_RASTERSTYLE *hRasterStyle,
                                       COLORANTINDEX **pColorantIndexArray,
                                       int32 *pColorantIndexArraySize,
                                       int32 *pcolorspacedimension ) ;

/* --------------------------------------------------------------------------- */

/* The functions for a dummy final link that is sometimes used to store final
 * color values and sometimes as a passthrough.
 */
static CLINKfunctions dummyFunctions = {cc_common_destroy, dummyInvokeSingle, NULL, NULL};

COLOR_STATE *frontEndColorState;

#define EPSILON   (0.00001)

/* --------------------------------------------------------------------------- */

#ifdef METRICS_BUILD

/* #define DEBUG_COLOR */

static struct gs_color_metrics {
  int nChainRequests;
  int nChainConstructions;
  int nChainCacheHits;
  int nChainDestructions;
  int nPrevChainCnt;
  int nLinkCreates;
  int nLinkDestroys;
  int nPrevLinkCnt;
} gs_color_metrics ;

#define METRICS_INCREMENT(x)  ++(gs_color_metrics.x)

void cc_metrics_increment_constructs(void)
{
  METRICS_INCREMENT(nChainConstructions);
}

void cc_metrics_increment_destructs(void)
{
  METRICS_INCREMENT(nChainDestructions);
}

#ifdef ASSERT_BUILD

static int countLinksInChain(CLINK *pLink)
{
  int linkCount = 0;

  while (pLink != NULL) {
    linkCount++;
    switch (pLink->linkType) {
    case CL_TYPEdevicecode:
      linkCount += cc_countLinksInDeviceCode(pLink);
      break;
    case CL_TYPEcmmxform:
      linkCount += cc_countLinksInCMMXform(pLink);
      break;
    case CL_TYPEinterceptdevicen:
      linkCount += cc_countLinksInDeviceN(pLink);
      break;
    }
    pLink = pLink->pnext;
  }

  return linkCount;
}

static int countSubChains(CLINK *pLink)
{
  int chainCount = 0;

  while (pLink != NULL) {
    switch (pLink->linkType) {
    case CL_TYPEinterceptdevicen:
      chainCount += cc_countSubChainsInDeviceN(pLink);
      break;
    }
    pLink = pLink->pnext;
  }

  return chainCount;
}

void cc_addCountsForOneChain(CLINK *pLink,
                             int *chainCount, int *linkCount)
{
  if (pLink != NULL) {
    *chainCount += 1;
    *linkCount += countLinksInChain(pLink);
    *chainCount += countSubChains(pLink);
  }
}

void cc_addCountsForOneContext(GS_CHAINinfo *chain,
                               double *floatChainCount, double *floatLinkCount)
{
  int chainCount = 0;
  int linkCount = 0;

  if (chain->context != NULL) {
    cc_addCountsForOneChain(chain->context->pnext, &chainCount, &linkCount);
    cc_addCountsForOneChain(chain->context->pSimpleLink, &chainCount, &linkCount);
    cc_addCountsForOneChain(chain->context->pCurrentCMYK, &chainCount, &linkCount);
    cc_addCountsForOneChain(chain->context->pCurrentRGB, &chainCount, &linkCount);
    cc_addCountsForOneChain(chain->context->pCurrentGray, &chainCount, &linkCount);

    *floatChainCount += (double) chainCount / chain->refCnt / chain->context->refCnt;
    *floatLinkCount += (double) linkCount / chain->refCnt / chain->context->refCnt;
  }
}

static void countAllColorChains(int *chainCount, int *linkCount)
{
  int32 i;
  double floatChainCount = 0.0;
  double floatLinkCount = 0.0;
  GS_COLORinfo *colorInfo;
  double cacheChainCount = 0.0;
  double cacheLinkCount = 0.0;

  /* Count the chains in all colorInfo structs, but taking account of chain
   * sharing by only counting a fraction of a chain when either the chainHead or
   * the chain itself is shared. Thus we may get a fractional number, which is
   * ok because it will be added to the fractional contribution from the
   * ChainCache. The total should be a whole number.
   */
  for ( colorInfo = frontEndColorState->colorInfoList.colorInfoHead;
        colorInfo != NULL; colorInfo = colorInfo->next ) {
    for (i = 0; i < GSC_N_COLOR_TYPES; i++) {
      GS_CHAINinfo *chain = colorInfo->chainInfo[i];
      cc_addCountsForOneContext(chain, &floatChainCount, &floatLinkCount);
    }
  }

  /* And add the chains referenced by the ChainCache, which is also modulated
   * for chain sharing.
   */
  cc_countChainsinChainCache(&cacheChainCount, &cacheLinkCount);
  floatChainCount += cacheChainCount;
  floatLinkCount += cacheLinkCount;

  *chainCount = (int32) (floatChainCount + 0.5);
  /** \todo Backend colorInfos are now added to page->colorInfoList and are
      therefore not counted towards the metrics. This also means the asserts
      ifdef'd out under COUNT_BACKEND_COLORINFOS would fire if compiled in.
      Metrics needs to be made pipeline safe, 65117. */
#ifdef COUNT_BACKEND_COLORINFOS
  HQASSERT(fabs(floatChainCount - *chainCount) <= EPSILON,
           "Missing a fraction of a color chain");
#endif

  *linkCount = (int32) (floatLinkCount + 0.5);
#ifdef COUNT_BACKEND_COLORINFOS
  HQASSERT(fabs(floatLinkCount - *linkCount) <= EPSILON,
           "Missing a fraction of a color link");
#endif
}

#else  /* !ASSERT_BUILD */

#define countAllColorChains(_chainCount, _linkCount)  \
MACRO_START \
  *(_chainCount) = 0;  \
  *(_linkCount) = 0;   \
MACRO_END


#endif /* !ASSERT_BUILD */


static Bool gs_color_metrics_update(sw_metrics_group *metrics)
{
  int currentChainCount;
  int currentLinkCount;

  countAllColorChains(&currentChainCount, &currentLinkCount);

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Color")) )
    return FALSE ;

  SW_METRIC_INTEGER("ChainConstructions", gs_color_metrics.nChainConstructions) ;
  SW_METRIC_INTEGER("ChainRequests", gs_color_metrics.nChainRequests) ;
  SW_METRIC_INTEGER("ChainCacheHits", gs_color_metrics.nChainCacheHits) ;
  SW_METRIC_INTEGER("ChainDestructions", gs_color_metrics.nChainDestructions) ;

#ifdef COUNT_BACKEND_COLORINFOS
  HQASSERT(gs_color_metrics.nPrevChainCnt + gs_color_metrics.nChainConstructions -
                  gs_color_metrics.nChainDestructions == currentChainCount,
           "Unbalanced color chain construction/destruction");
  HQASSERT(gs_color_metrics.nPrevLinkCnt + gs_color_metrics.nLinkCreates -
                  gs_color_metrics.nLinkDestroys == currentLinkCount,
           "Unbalanced color link create/destroys");
#endif

  gs_color_metrics.nPrevChainCnt = currentChainCount;
  gs_color_metrics.nPrevLinkCnt = currentLinkCount;

#ifdef DEBUG_COLOR
  {
    sw_metric *maxCachedChains_metric;
    sw_datum maxCachedChains;

    maxCachedChains_metric = sw_metric_get(metrics, "MaxCachedChains", strlen("MaxCachedChains"));
    maxCachedChains = sw_metric_get_value(maxCachedChains_metric);
    monitorf((uint8 *) "Chain %d requests, %d cacheHits, %d constructions, %d max_cached\n",
              gs_color_metrics.nChainRequests, gs_color_metrics.nChainCacheHits,
              gs_color_metrics.nChainConstructions,
              maxCachedChains.value.integer);
  }
#endif

  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void gs_color_metrics_reset(int reason)
{
  struct gs_color_metrics init = { 0 } ;

  gs_color_metrics = init ;

  if (reason != SW_METRICS_RESET_BOOT)
    countAllColorChains(&gs_color_metrics.nPrevChainCnt, &gs_color_metrics.nPrevLinkCnt);
}

static sw_metrics_callbacks gs_color_metrics_hook = {
  gs_color_metrics_update,
  gs_color_metrics_reset,
  NULL
} ;

#else     /* !METRICS_BUILD */

#define METRICS_INCREMENT(x)  EMPTY_STATEMENT()

#endif    /* !METRICS_BUILD */

/* --------------------------------------------------------------------------- */

static Bool colorspaceNameToIndex(GUCR_RASTERSTYLE *hRasterStyle,
                                  OBJECT           *colorantName,
                                  Bool             allowAutoSeparation,
                                  Bool             f_do_nci,
                                  COLORANTINDEX    *pcolorant,
                                  OBJECT           *excludedSeparations,
                                  Bool             *colorantMatchesDevice)
{
  COLORSPACE_ID calibrationColorSpace ;
  COLORANTINDEX colorant ;
  NAMECACHE *ntemp ;

  HQASSERT( colorantName != NULL, "colorantName is NULL" ) ;
  HQASSERT( pcolorant != NULL, "pcolorant is NULL" ) ;
  HQASSERT( colorantMatchesDevice != NULL, "colorantMatchesDevice is NULL" ) ;

  *colorantMatchesDevice = FALSE ;
  *pcolorant = COLORANTINDEX_UNKNOWN;

  if ( oType ( *colorantName ) == OSTRING ) {
    ntemp = cachename( oString( *colorantName ), theLen(*colorantName)) ;
    if ( ntemp == NULL )
      return FALSE;
  } else {
    HQASSERT(oType ( *colorantName ) == ONAME, "colorant must be a String or Name" ) ;
    ntemp = oName( *colorantName ) ;
  }

  if ( theINLen( ntemp ) == 0 ) {
    /* Cope with a bug in InDesign which produces zero length names when
     * outputting to a composite device that can handle separation spaces.
     */
    return TRUE;
  }

  if (oType(*excludedSeparations) == ODICTIONARY) {
     OBJECT es = OBJECT_NOTVM_NOTHING;

    /* If the colorant names match a dummy name that the application intended
     * to use to force the alternate space, then honour that intent.
     */
    object_store_namecache(&es, ntemp, LITERAL);
    if (fast_extract_hash(excludedSeparations, &es)) {
      *pcolorant = COLORANTINDEX_NONE ;
      return TRUE;
    }
  }

  /* In the normal case, only match if the colorant is fully fledged */
  colorant = guc_colorantIndex(hRasterStyle, ntemp);

  if (colorant == COLORANTINDEX_UNKNOWN && allowAutoSeparation) {
    HQASSERT(IS_INTERPRETER(), "All colorants should be known in back end");
    if (!guc_addAutomaticSeparation(hRasterStyle, ntemp, f_do_nci))
      return FALSE ;

    colorant = guc_colorantIndex(hRasterStyle, ntemp);
  }

  *colorantMatchesDevice = (colorant != COLORANTINDEX_UNKNOWN);

  guc_calibrationColorSpace( hRasterStyle, &calibrationColorSpace ) ;
  if (calibrationColorSpace != SPACE_notset) {
    /* The photoink case, only match calibration colorants, which must be a
     * reserved colorant (but /None is also allowed and /All will be replaced later).
     */
    if ((colorant != COLORANTINDEX_NONE) &&
        (colorant != COLORANTINDEX_ALL)) {
      if (colorant != COLORANTINDEX_UNKNOWN) {
        /* Force the tint transform */
        HQTRACE( TRUE, ("A native photoink separation has been set - using tint transform") );
        *colorantMatchesDevice = FALSE ;
      }
      else {
        colorant = guc_colorantIndexReserved(hRasterStyle, ntemp);
        *colorantMatchesDevice = guc_photoink_colorant(hRasterStyle, colorant);
      }
    }
  }

  if ( colorant == COLORANTINDEX_UNKNOWN ) {
    if (!guc_colorantIndexPossiblyNewName(hRasterStyle, ntemp, &colorant))
      return FALSE;
    HQASSERT( colorant != COLORANTINDEX_UNKNOWN ,
              "Didn't expect to get COLORANTINDEX_UNKNOWN(1)" ) ;
  }

  *pcolorant = colorant ;
  return TRUE ;
}

Bool cc_colorspaceNamesToIndex(GUCR_RASTERSTYLE   *hRasterStyle,
                               OBJECT             *PSColorSpace,
                               Bool               allowAutoSeparation,
                               Bool               f_do_nci,
                               COLORANTINDEX      *pcolorants,
                               int32              n_colorants,
                               OBJECT             *excludedSeparations,
                               Bool               *allColorantsMatch)
{
  OBJECT *theo ;

  *allColorantsMatch = TRUE;

  HQASSERT( PSColorSpace != NULL, "PSColorSpace is NULL" ) ;
  HQASSERT( pcolorants != NULL, "pcolorants is NULL" ) ;
  HQASSERT( allColorantsMatch != NULL, "allColorantsMatch is NULL" ) ;

  HQASSERT(( oType( *PSColorSpace ) == OARRAY ||
             oType( *PSColorSpace ) == OPACKEDARRAY ) &&
             ( theLen(*PSColorSpace) == 4 || theLen(*PSColorSpace) == 5 ),
            "PSColorSpace is not an array object of length 4 or 5" ) ;
  theo = oArray( *PSColorSpace ) ;

  HQASSERT( oType( *theo ) == ONAME &&
            ( oName( *theo ) == system_names + NAME_Separation ||
              oName( *theo ) == system_names + NAME_DeviceN ),
            "PSColorSpace must be /Separation or /DeviceN" ) ;
  theo ++ ;

  /* Colorant objects of array type are valid for DeviceN, but invalid for a
   * /Separation color space.
   */
  if (oName(oArray(*PSColorSpace)[0]) == system_names + NAME_Separation &&
      (oType(oArray(*PSColorSpace)[1]) == OARRAY ||
       oType(oArray(*PSColorSpace)[1]) == OPACKEDARRAY))
    return error_handler(TYPECHECK);

  return cc_namesToIndex(hRasterStyle,
                         theo,
                         allowAutoSeparation,
                         f_do_nci,
                         pcolorants,
                         n_colorants,
                         excludedSeparations,
                         allColorantsMatch);
}

Bool cc_namesToIndex(GUCR_RASTERSTYLE   *hRasterStyle,
                     OBJECT             *namesObject,
                     Bool               allowAutoSeparation,
                     Bool               f_do_nci,
                     COLORANTINDEX      *pcolorants,
                     int32              n_colorants,
                     OBJECT             *excludedSeparations,
                     Bool               *allColorantsMatch)
{
  int i;
  OBJECT *theo = namesObject;

  switch ( oType ( *theo ) ) {

  case OSTRING:
  case ONAME:
    HQASSERT( n_colorants == 1, "Wrong number of colorants");
    if (!colorspaceNameToIndex( hRasterStyle,
                                theo,
                                allowAutoSeparation,
                                f_do_nci,
                                &pcolorants[0],
                                excludedSeparations,
                                allColorantsMatch ))
      return FALSE;
    break;

  case OARRAY:
  case OPACKEDARRAY:
    HQASSERT( n_colorants == theLen(*theo), "Wrong number of colorants");
    theo = oArray( *theo ) ;

    for ( i = 0 ; i < n_colorants ; i++ ) {
      Bool colorantMatchesDevice;

      switch ( oType ( *theo ) ) {
      case OSTRING:
      case ONAME:
        if (!colorspaceNameToIndex( hRasterStyle,
                                    theo,
                                    allowAutoSeparation,
                                    f_do_nci,
                                    &pcolorants[i],
                                    excludedSeparations,
                                    &colorantMatchesDevice ))
          return FALSE;

        /* /All is not supported for DeviceN */
        if (!colorantMatchesDevice || pcolorants[ i ] == COLORANTINDEX_ALL)
          *allColorantsMatch = FALSE ;
        break;

      default:
        return error_handler(TYPECHECK);
      }
      theo++ ;
    }
    break;

  default:
    return error_handler(TYPECHECK);
  }

  return TRUE;
}

Bool gsc_colorspaceNamesToIndex(GUCR_RASTERSTYLE   *hRasterStyle,
                                OBJECT             *PSColorSpace,
                                Bool               allowAutoSeparation,
                                Bool               f_do_nci,
                                COLORANTINDEX      *pcolorants,
                                int32              n_colorants,
                                GS_COLORinfo       *colorInfo,
                                Bool               *allColorantsMatch)
{
  return cc_colorspaceNamesToIndex(hRasterStyle, PSColorSpace,
                                   allowAutoSeparation, f_do_nci, pcolorants,
                                   n_colorants, &colorInfo->params.excludedSeparations,
                                   allColorantsMatch);
}

#ifdef ASSERT_BUILD
static void check_colorInfoHead(GS_COLORinfoList *list)
{
  int32 i = 0;
  GS_COLORinfo *info;
  for (info = list->colorInfoHead; info != NULL; info = info->next)
    i++;
  HQASSERT(i == list->nColorInfos, "Corrupt colorInfo list");
}
#else
#define check_colorInfoHead(_list)
#endif

void gsc_freecolorinfo(GS_COLORinfo *colorInfo)
{
  GS_COLORinfoList *list = &colorInfo->colorState->colorInfoList;
  int32 nChain;

  for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
    /* The sole purpose of the cc_addChainCacheEntry here is to hand the chain
     * over to the chainCache if possible. If this fails, the worst that can
     * happen is that we'll lose some performance. Anyway, we must ignore the
     * error because changing the signature of this function would be wrist
     * slittingly bad.
     * Set notIfCacheOwner to TRUE because there is no point caching the chain
     * if we are about to destroy the cache.
     */
    (void) cc_addChainCacheEntry(colorInfo, nChain, TRUE, TRUE);
    cc_destroyChain( & colorInfo->chainInfo[nChain] ) ;
    cc_destroyChainCache( colorInfo, nChain );
  }
  cc_destroycrdinfo( & colorInfo->crdInfo );
  cc_destroyrgbtocmykinfo( & colorInfo->rgbtocmykInfo );
  cc_destroytransferinfo( & colorInfo->transferInfo );
  cc_destroycalibrationinfo( & colorInfo->calibrationInfo );
  cc_destroyhalftoneinfo( & colorInfo->halftoneInfo );
  cc_destroyhcmsinfo( & colorInfo->hcmsInfo );
  cc_destroydevicecodeinfo( & colorInfo->devicecodeInfo );

  guc_discardRasterStyle( & colorInfo->deviceRS );
  guc_discardRasterStyle( & colorInfo->targetRS );

  if (colorInfo == list->colorInfoHead)
    list->colorInfoHead = list->colorInfoHead->next;
  else {
    GS_COLORinfo *prevInfo = list->colorInfoHead;
    while (prevInfo->next != colorInfo)
      prevInfo = prevInfo->next;
    prevInfo->next = prevInfo->next->next;
  }
  list->nColorInfos--;
  check_colorInfoHead(list);
}

void gsc_copycolorinfo(GS_COLORinfo *dst, GS_COLORinfo *src)
{
  GS_COLORinfoList *list = &src->colorState->colorInfoList;
  int32 nChain;

  /* Invalidate the chains for the src colorInfo where appropriate. Doing this
   * here means that the sequence:
   *   gsave .. invokeChain .. grestore .. gsave .. invokeChain .. grestore
   * allows the reuse the first chain in the second invoke. If we didn't do this
   * the chains will be invalidated when the value of fInvalidateColorChains is
   * TRUE in the parent gstate because the second invoke will inherit that value.
   */
  if ( src->fInvalidateColorChains ) {
     /* If cc_invalidateColorChains fails, the worst that can happen is that
      * we'll lose some performance. Anyway, we must otherwise ignore the error
      * because changing the signature of this function would be wrist
      * slittingly bad.
      */
    if (cc_invalidateColorChains(src, TRUE))
      src->fInvalidateColorChains = FALSE ;
  }

  HqMemCpy( dst, src, sizeof( GS_COLORinfo )) ;

  /* Since we're sharing the color chains and info, we need to reserve our
   * claim on them.
   */
  for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
    if ( dst->chainInfo[ nChain ] != NULL )
      CLINK_RESERVE( dst->chainInfo[ nChain ] ) ;

    cc_reserveChainCache( dst->chainCache[ nChain ] ) ;
  }

  cc_reservecrdinfo( dst->crdInfo ) ;
  cc_reservergbtocmykinfo( dst->rgbtocmykInfo ) ;
  cc_reservetransferinfo( dst->transferInfo ) ;
  cc_reservecalibrationinfo( dst->calibrationInfo ) ;
  cc_reservehalftoneinfo( dst->halftoneInfo ) ;
  cc_reservehcmsinfo( dst->hcmsInfo ) ;
  cc_reservedevicecodeinfo( dst->devicecodeInfo ) ;

  guc_reserveRasterStyle( dst->deviceRS ) ;
  guc_reserveRasterStyle( dst->targetRS ) ;

  dst->next = list->colorInfoHead;
  list->colorInfoHead = dst;
  list->nColorInfos++;
  check_colorInfoHead(list);
}

Bool gsc_copycolorinfo_withstate(GS_COLORinfo *dst, GS_COLORinfo *src,
                                 COLOR_STATE *colorState)
{
  GS_COLORinfoList *list = &colorState->colorInfoList;

  if ( colorState == NULL || colorState == src->colorState ) {
    gsc_copycolorinfo(dst, src);
    return TRUE;
  }

  /* Changing colorState means the chains and the chain cache need a full
     reset. */

  cc_colorInfo_init(dst);

  dst->colorState = colorState;
  dst->next = list->colorInfoHead;
  list->colorInfoHead = dst;
  list->nColorInfos++;
  check_colorInfoHead(list);

  if ( !cc_initChains(dst) ||
       !cc_initChainCache(dst) )
    return FALSE;

  dst->deviceRS = src->deviceRS ;
  dst->targetRS = src->targetRS ;

  guc_reserveRasterStyle( dst->deviceRS ) ;
  guc_reserveRasterStyle( dst->targetRS ) ;

  dst->crdInfo = src->crdInfo ;
  dst->rgbtocmykInfo = src->rgbtocmykInfo ;
  dst->transferInfo = src->transferInfo ;
  dst->calibrationInfo = src->calibrationInfo ;
  dst->halftoneInfo = src->halftoneInfo ;
  dst->hcmsInfo = src->hcmsInfo ;
  dst->devicecodeInfo = src->devicecodeInfo ;

  cc_reservecrdinfo( dst->crdInfo ) ;
  cc_reservergbtocmykinfo( dst->rgbtocmykInfo ) ;
  cc_reservetransferinfo( dst->transferInfo ) ;
  cc_reservecalibrationinfo( dst->calibrationInfo ) ;
  cc_reservehalftoneinfo( dst->halftoneInfo ) ;
  cc_reservehcmsinfo( dst->hcmsInfo ) ;
  cc_reservedevicecodeinfo( dst->devicecodeInfo ) ;

  dst->params = src->params ;
  dst->gstate = src->gstate ;
  /* Some params are for the front end only */
  dst->params.excludedSeparations = onull ;  /* Struct copy for slot properties */
  dst->params.adobeProcessSeparations = FALSE ;

  /* A special for named color tables that cache tint transforms, and thus
   * calling the interpreter in the back end. These tables have already been
   * built up in the back end colorState as part of building color chains during
   * preconvert_new(), but they must now be owned by the back end colorState
   * otherwise the interpreter will change them under it's feet. This is
   * necessary because the design allows named color data to be shared by as
   * many colorInfo's as possible.
   */
  if (!cc_updateNamedColorantStore(dst))
    return FALSE;

  return TRUE;
}

Bool gsc_areobjectsglobal(corecontext_t *context, GS_COLORinfo *colorInfo )
{
  int32 nChain;

  for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
    if ( cc_arechainobjectslocal(context, colorInfo, nChain ) )
      return FALSE ;
  }

  if ( cc_arecrdobjectslocal(context, colorInfo->crdInfo ) )
    return FALSE ;
  if ( cc_arergbtocmykobjectslocal(context, colorInfo->rgbtocmykInfo ) )
    return FALSE ;
  if ( cc_aretransferobjectslocal(context, colorInfo->transferInfo ) )
    return FALSE ;
  if ( cc_arecalibrationobjectslocal(context, colorInfo->calibrationInfo ) )
    return FALSE ;
  if ( cc_arehalftoneobjectslocal(context, colorInfo->halftoneInfo ) )
    return FALSE ;
  if ( cc_arehcmsobjectslocal(context, colorInfo->hcmsInfo ) )
    return FALSE ;

  return TRUE ;
}


/* gsc_scan - scanning function for gstate color info
 *
 * This should match gsc_areobjectsglobal, since both need look at all the VM
 * pointers. */
mps_res_t gsc_scan( mps_ss_t ss, GS_COLORinfo *colorInfo )
{
  int32 nChain;
  mps_res_t res;

  for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
    GS_CHAIN_CONTEXT *chainContext;

    if (colorInfo->chainInfo[nChain] == NULL)
      continue;

    chainContext = colorInfo->chainInfo[nChain]->context;

    res = cc_scan_chain( ss, colorInfo, nChain );
    if ( res != MPS_RES_OK )
      return res;
    if (chainContext != NULL) {
      if (chainContext->pnext != NULL) {
        res = cc_scan( ss, chainContext->pnext );
        if ( res != MPS_RES_OK )
          return res;
      }
      if (chainContext->pSimpleLink != NULL) {
        res = cc_scan( ss, chainContext->pSimpleLink );
        if ( res != MPS_RES_OK )
          return res;
      }
      if (chainContext->pCurrentCMYK != NULL) {
        res = cc_scan( ss, chainContext->pCurrentCMYK );
        if ( res != MPS_RES_OK )
          return res;
      }
      if (chainContext->pCurrentRGB != NULL) {
        res = cc_scan( ss, chainContext->pCurrentRGB );
        if ( res != MPS_RES_OK )
          return res;
      }
      if (chainContext->pCurrentGray != NULL) {
        res = cc_scan( ss, chainContext->pCurrentGray );
        if ( res != MPS_RES_OK )
          return res;
      }
    }
  }

  res = cc_scan_crd( ss, colorInfo->crdInfo );
  if ( res != MPS_RES_OK ) return res;
  res = cc_scan_rgbtocmyk( ss, colorInfo->rgbtocmykInfo );
  if ( res != MPS_RES_OK ) return res;
  res = cc_scan_transfer( ss, colorInfo->transferInfo );
  if ( res != MPS_RES_OK ) return res;
  res = cc_scan_calibration( ss, colorInfo->calibrationInfo );
  if ( res != MPS_RES_OK ) return res;
  res = cc_scan_halftone( ss, colorInfo->halftoneInfo );
  if ( res != MPS_RES_OK ) return res;
  res = cc_scan_hcms( ss, colorInfo->hcmsInfo );
  return res;
}

/* Initialise the color information; should only be called once,
 * from dostart(); failures are terminal.
 */
Bool gsc_swstart( GS_COLORinfo *colorInfo )
{
  if ( !gsc_colorStateCreate(&frontEndColorState) )
    return FALSE;

  colorInfo->colorState = frontEndColorState;

  frontEndColorState->colorInfoList.colorInfoHead = colorInfo;
  HQASSERT(frontEndColorState->colorInfoList.nColorInfos == 0, "Corrupt colorInfo list");
  frontEndColorState->colorInfoList.nColorInfos++;

  cc_initEraseColorRoot();

  return TRUE;
}


/* gsc_finish - finish color information */
void gsc_finish( GS_COLORinfo *colorInfo )
{
  UNUSED_PARAM( GS_COLORinfo *, colorInfo );

  gsc_params_finish() ;
  cc_iccbased_finish();
  gsc_colorStateDestroy(&frontEndColorState);
}

/** COLOR_STATE contains references to the top level color structures including
    the spacecache, the color cache, the color chain cache and the colorInfo
    list.  gsc_colorStateCreate is called once per swstart for the frontend and
    for each page in the backend. */
Bool gsc_colorStateCreate(COLOR_STATE **colorStateRef)
{
  COLOR_STATE *colorState, init = {0};

  HQASSERT(*colorStateRef == NULL, "Color state already exists");

  colorState = mm_alloc(mm_pool_color, sizeof(COLOR_STATE),
                        MM_ALLOC_CLASS_NCOLOR);
  if ( colorState == NULL )
    return error_handler(VMERROR);

  *colorState = init;

  if ( !coc_start(&colorState->cocState) ||
       !cc_startChainCache(&colorState->chainCacheState) ||
       !gsc_spacecache_init(&colorState->spacecache) ||
       !gsc_startICCCache(colorState) ||
       !cc_fastrgb2gray_create(&colorState->fastrgb2grayState) ||
       !cc_fastrgb2cmyk_create(&colorState->fastrgb2cmykState) ||
       !cc_tintStateCreate(&colorState->tintState) ) {
    gsc_colorStateDestroy(&colorState);
    return FALSE;
  }

  cc_initInterceptId(&colorState->hcmsInterceptId);

  *colorStateRef = colorState;
  return TRUE;
}

void gsc_colorStateDestroy(COLOR_STATE **colorStateRef)
{
  if ( *colorStateRef != NULL ) {
    COLOR_STATE *colorState = *colorStateRef;

    cc_tintStateDestroy(&colorState->tintState);
    cc_fastrgb2gray_destroy(&colorState->fastrgb2grayState);
    cc_fastrgb2cmyk_destroy(&colorState->fastrgb2cmykState);
    gsc_spacecache_destroy(&colorState->spacecache,
                           colorState->chainCacheState,
                           &colorState->colorInfoList);
    cc_stopChainCache(&colorState->chainCacheState);
    coc_finish(&colorState->cocState);
    gsc_finishICCCache(colorState);
    HQASSERT(colorState->tomsTables == NULL,
             "Freeing colorstate with TomsTables still active");

    mm_free(mm_pool_color, colorState, sizeof(COLOR_STATE));
    *colorStateRef = NULL;
  }
}

/** Transfer relevant color data from the front end to the back end */
Bool gsc_colorStateTransfer(DL_STATE    *page,
                            COLOR_STATE *srcColorState,
                            COLOR_STATE *dstColorState)
{
  UNUSED_PARAM(COLOR_STATE *, srcColorState);
  UNUSED_PARAM(COLOR_STATE *, dstColorState);

  /* ICC state */
  if (!gsc_ICCCacheTransfer(page))
    return FALSE;

  gsc_spacecache_destroy(&srcColorState->spacecache,
                         srcColorState->chainCacheState,
                         &srcColorState->colorInfoList);

  return TRUE;
}

Bool gsc_colorStateStart(void)
{
  HQASSERT(IS_INTERPRETER(), "Starting color state in back end");

  if ( frontEndColorState->spacecache == NULL )
    if ( !gsc_spacecache_init(&frontEndColorState->spacecache) )
      return FALSE;
  return TRUE;
}

void gsc_colorStateFinish(Bool changing_page)
{
  GS_COLORinfo *colorInfo;

  HQASSERT(IS_INTERPRETER(), "Finishing color state in back end");

  for ( colorInfo = frontEndColorState->colorInfoList.colorInfoHead;
        colorInfo != NULL; colorInfo = colorInfo->next ) {
    /* The target rasterstyles may be allocated from dl pool memory and when
       that pool is destroyed we need to reset all the colorInfos' targets to
       something. This is to cope with the "gsave beginpage grestore"
       idiom. */
    gsc_setTargetRS(colorInfo, gsc_getRS(colorInfo));

    /* Some color chain links contain DL_STATE references. Invalidate all
       front-end chains if we're changing the inputpage. */
    if ( changing_page )
      (void)cc_invalidateColorChains(colorInfo, TRUE) ;
  }

  /* Cached chains may reference colorspaces created by spacecache,
     so this needs to match the cc_invalidateColorChains call above. */
  if ( changing_page )
    gsc_spacecache_destroy(&frontEndColorState->spacecache,
                           frontEndColorState->chainCacheState,
                           &frontEndColorState->colorInfoList);
  coc_reset(frontEndColorState->cocState, FALSE);
}

void gsc_colorStatePartialReset(void)
{
  HQASSERT(IS_INTERPRETER(), "Partial reset of color state in back end");

  /* Reset the ColorCache, releasing dl_colors as we go.
     This isn't strictly necessary since we expect this cache to be empty
     at this point - since we've had to do a partial paint, and therefore
     it shouldn't actually do anything useful. */
  coc_reset(frontEndColorState->cocState, TRUE);
}

Bool gsc_initgraphics( GS_COLORinfo *colorInfo )
{
  if ( ! gsc_setcolorspacedirect( colorInfo, GSC_FILL, SPACE_DeviceGray ) ||
       ! gsc_setcolorspacedirect( colorInfo, GSC_STROKE, SPACE_DeviceGray ))
    return FALSE ;

  theTags( colorInfo->chainInfo[ GSC_FILL ]->pattern ) = ONULL ;
  theTags( colorInfo->chainInfo[ GSC_STROKE ]->pattern ) = ONULL ;

  return TRUE ;
}

Bool gsc_chainCanBeInvoked( GS_COLORinfo *colorInfo , int32 colorType )
{
  GS_CHAINinfo *colorChain ;

  colorChain = colorInfo->chainInfo[ colorType ];

  if ( colorChain == NULL ||
       colorChain->iColorSpace == SPACE_notset )
    return FALSE ;
  else
    return TRUE ;
}


/* Helper routine for gsc_invokeChainSingle
 * to actually invoke single on each CLINK
 */
static Bool cc_iterateChainSingle(CLINK          *pLink,
                                  GSC_BLACK_TYPE *blackType)
{
  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(blackType != NULL, "blackType NULL");

  while ( pLink->pnext != NULL ) {
    if ( ! ( pLink->functions->invokeSingle )( pLink, pLink->pnext->iColorValues ))
      return FALSE ;
    pLink->pnext->overprintProcess = pLink->overprintProcess ;
    pLink->pnext->blackType = pLink->blackType;
    pLink->pnext->blackValueToPreserve = pLink->blackValueToPreserve;

    pLink = pLink->pnext ;
  }
  if (!(pLink->functions->invokeSingle)(pLink, NULL))
    return FALSE;

  *blackType = pLink->blackType;
  return TRUE;
}

/* gsc_invokeChainSingle constructs the colorchain if none exists
 * and calls the invoke function for each link in turn.
 */
Bool gsc_invokeChainSingle( GS_COLORinfo *colorInfo ,
                            int32 colorType )
{
  GUCR_RASTERSTYLE *hRasterStyle;
  CLINK *pLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;
  GSC_BLACK_TYPE outBlackType;
  uint32 hashKey = 0;
  Bool useCache;
  int32 i;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_invokeChainSingle" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_invokeChainSingle");

  hRasterStyle = gsc_getTargetRS(colorInfo);

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");
  HQASSERT(colorChain->fCompositing || colorChain->inBlackType == BLACK_TYPE_NONE,
           "blackType should be NONE when interpreting");

  pLink = chainContext->pnext;
  HQASSERT(pLink != NULL, "pLink null");

  /* If the color values are in the cache then use it and bail out */
  useCache = (chainContext->cacheFlags & GSC_ENABLE_COLOR_CACHE) != 0;
  if (useCache) {
    Bool cacheHit = FALSE;

    if ( chainContext->pCache == NULL ) {
      /* No cache pointer, cache not explicitly disabled for this chain, so go
         create it */
      if ( coc_head_create(colorInfo->colorState->cocState, colorChain) )
        cacheHit = coc_lookup(colorChain, &hashKey);
    } else {  /* Have a cache, invoke it */
      cacheHit = coc_lookup(colorChain, &hashKey);
    }

    if ( cacheHit )
      return TRUE;
  }

  /* Copy the salient dynamic data to the first link. That's the overprint
   * and color values. */
  pLink->overprintProcess = colorChain->overprintProcess ;
  for (i = 0; i < pLink->n_iColorants; ++i)
    pLink->iColorValues[i] = colorChain->iColorValues[i];
  pLink->blackType = colorChain->inBlackType;
  HQASSERT(pLink->blackValueToPreserve == 0.0f, "blackValueToPreserve != 0.0");

  /* The invokes for each CLINK may call out to PS, which could (if someone
   * is being mean, or stoopid) execute operators which cause the color
   * chain to be invalidated there and then (rather than deferred - using the
   * gMustResetColorChains global). We catch this by putting our claim on the
   * CLINKs, so that they don't get destroyed during the invoke).
   */
  CLINK_RESERVE(chainContext);
  if (!cc_iterateChainSingle(pLink, &outBlackType)) {
    cc_destroyChainContext(&chainContext);
    return FALSE ;
  }

  dl_set_currentblacktype(CoreContext.page->dlc_context, outBlackType);

  /* If we are intercepting, we may introduce color into a colorant where a simple
     conversion would not. This is fine, so far as reproducing the color is
     concerned, but gives us a problem with overprints, since those are rather
     color space dependent. So we need to know which channels would have been
     overprinted (typically because of an implied overprint where there is a zero
     operand to setcmykcolor, but also implied black overprints, and explicit
     process overprints too). To do this we need to build a simple chain (if we
     haven't already done it, but it is a lazy construction since we don't know
     until we have an actual color that needs it whether we need to do this or
     not); once we've done it we keep the chain though.

     The end result is the color from the main chain will be merged with the
     overprint info from this simple chain and marked to do maxblitting if
     appropriate.
   */

  if (chainContext->fApplyMaxBlts) {
    GSC_BLACK_TYPE dummyBlackType;

    /* Lazily create a simple chain without any color management with the
     * exception of black preservation. Invoke it to gather the overprint
     * info and merge with the original dl_color.
     */
    if (chainContext->pSimpleLink == NULL) {
      COLORSPACE_ID calibrationColorSpace ;
      DEVICESPACEID RealDeviceSpace ;
      int32 nDeviceColorants ;

      guc_deviceColorSpace( hRasterStyle, &RealDeviceSpace, &nDeviceColorants ) ;
      guc_calibrationColorSpace( hRasterStyle, &calibrationColorSpace ) ;

      if ( ! cc_constructChain( colorInfo ,
                                hRasterStyle ,
                                colorType ,
                                TRUE , /* a simple chain, not allowing intercepts, and ... */
                                colorChain ,
                                &chainContext->pSimpleLink , /* ... not the ordinary head link! */
                                &colorChain->colorspace ,
                                colorChain->iColorSpace ,
                                colorChain->n_iColorants ,
                                RealDeviceSpace ,
                                nDeviceColorants ,
                                calibrationColorSpace ,
                                FALSE)) {
          /* oops */
          cc_destroyChainContext(&chainContext);
          return FALSE;
        }
    }

    /* Copy the salient dynamic data to the simple link. That's the overprint
     * and color values. We use the color values that have had black removed,
     * i.e. the values from the first link.
     */
    chainContext->pSimpleLink->overprintProcess = colorChain->overprintProcess ;
    for (i = 0; i < pLink->n_iColorants; ++i)
      chainContext->pSimpleLink->iColorValues[i] = chainContext->pnext->iColorValues[i];
    chainContext->pSimpleLink->blackType = chainContext->pnext->blackType;
    chainContext->pSimpleLink->blackValueToPreserve = chainContext->pnext->blackValueToPreserve;

    if (!cc_iterateChainSingle(chainContext->pSimpleLink, &dummyBlackType)) {
      cc_destroyChainContext(&chainContext);
      return FALSE ;
    }
  }

  /* And finally put the resulting dl_color into the cache if it's active. Don't
     insert if PS callback invalidated chain, making colorChain->context null. */
  if (useCache) {
    if ( chainContext == colorChain->context && chainContext->pCache != NULL )
      coc_insert(colorChain, hashKey);
  }

  /* We've now finished with this chain, for the purposes of this invoke
     (matches CLINK_RESERVE just before first cc_iterateChainSingle()). */
  cc_destroyChainContext(&chainContext);

  return TRUE ;
}

Bool gsc_getNSeparationColorants( GS_COLORinfo *colorInfo , int32 colorType ,
                                  int32 *nColorants , COLORANTINDEX **oColorants )
{
  CLINK *pLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *context;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_getNSeparationColorants" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getNSeparationColorants");
  HQASSERT( oColorants , "oColorants NULL in gsc_getNSeparationColorants" ) ;

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  context = colorChain->context;
  HQASSERT(context != NULL, "colorChain->context NULL");
  HQASSERT(colorChain->fCompositing || colorChain->inBlackType == BLACK_TYPE_NONE,
           "blackType should be NONE when interpreting");

  HQASSERT( colorChain->iColorSpace == SPACE_DeviceN ||
            colorChain->iColorSpace == SPACE_Separation ,
            "Should only be called with SPACE_DeviceN/SPACE_Separation" ) ;

  pLink = context->pnext ;
  HQASSERT( pLink != NULL, "pLink NULL" ) ;

  while ( pLink ) {
    if ( pLink->linkType == CL_TYPEblackevaluate ||
         pLink->linkType == CL_TYPEtinttransform ||
         pLink->linkType == CL_TYPEallseptinttransform ||
         pLink->linkType == CL_TYPEtransfer ||
         pLink->linkType == CL_TYPEdevicecode ||
         pLink->linkType == CL_TYPEinterceptdevicen) {
      HQASSERT( pLink->iColorants != NULL , "Only the above CLINKs should have colorants" ) ;
      *nColorants = pLink->n_iColorants ;
      *oColorants = pLink->iColorants ;
      return TRUE ;
    }
    HQASSERT( pLink->iColorants == NULL , "All other CLINKs should not have colorants" ) ;
    pLink = pLink->pnext ;
  }
  HQFAIL( "Somehow didn't find the CL_TYPEtinttransform/CL_TYPEtransfer link" ) ;
  return FALSE ;
}

/* For indexed colorspaces - how many colors are specified in this space, and
 * how many colorants are there per color?
 *
 * This function asserts that the specified colorspace is indexed.
 */
Bool gsc_getBaseColorListDetails( GS_COLORinfo *colorInfo ,
                                  int32 colorType ,
                                  int32* colorCount ,
                                  int32* colorantCount )
{
  USERVALUE * color ;
  SYSTEMVALUE indexRange[ 2 ] ;
  GS_CHAINinfo * colorChain ;

  COLORTYPE_ASSERT(colorType, "gsc_getBaseColorListDetails");

  colorChain = colorInfo->chainInfo[ colorType ] ;

  HQASSERT( colorChain->iColorSpace == SPACE_Indexed ,
            "gsc_getBaseColorListDetails - colorspace is not indexed" );

  /*Get the color range (so we can calculate the valid index range)*/
  if ( gsc_range( colorInfo , colorType , 0 , indexRange ) != TRUE )
    return FALSE ;

  /* Plus one, because we can access 0 - hival inclusive */
  colorCount[ 0 ] = (( int32 ) indexRange[ 1 ] ) + 1 ;

  /* Get a base color, just so we can get the number of colorants in the base
  space */
  if ( gsc_baseColor( colorInfo , colorType , &color , colorantCount ) != TRUE )
    return FALSE ;

  return TRUE ;
}

/* For indexed colorspaces - get the list of colors (specified in the base
 * color space) each index maps to.
 * The passed USERVALUE array should be allocated/deallocated by the caller,
 * and be large enough to hold (expectedColorCount) * (expectedColorantCount)
 * USERVALUE's.
 * If the color / colorant count is different from those expected, the
 * function will exit, returning FALSE.
 *
 * This function asserts that the specified colorspace is indexed.
 */
Bool gsc_getBaseColorList( GS_COLORinfo *colorInfo ,
                           int32 colorType ,
                           int32 expectedColorCount ,
                           int32 expectedColorantCount ,
                           USERVALUE *targetList )
{
  int32 i ;
  int32 colorCount = 0;
  int32 colorantCount = 0;
  int32 colorantIndex ;
  USERVALUE * color ;
  USERVALUE oldValue ;
  GS_CHAINinfo * colorChain ;

  HQASSERT( colorInfo , "gsc_getBaseColorList - passed colorInfo is NULL" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getBaseColorList");
  HQASSERT( targetList != NULL , "gsc_getBaseColorList - passed targetList is NULL" );

  /* The following block of code was basically copied from gsc_setcolor, from
  the case for indexed colorspaces */
  if ( cc_updateChain( & colorInfo->chainInfo[ colorType ] ) != TRUE )
    return FALSE ;

  /* Get the colorchain and assert that it is an indexed color space */
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT( colorChain->iColorSpace == SPACE_Indexed ,
            "gsc_getBaseColorList - colorspace is not indexed" ) ;

  /* Store the old color value so we can restore it at the end */
  oldValue = colorChain->iColorValues[ 0 ] ;

  /* Get the number of colors specified in the base space. Call this function
  specifically to ensure that we are producing the same number of colors as
  the caller will have allocated */
  if ( gsc_getBaseColorListDetails( colorInfo , colorType , &colorCount ,
                                    &colorantCount ) != TRUE )
    return FALSE ;
  if (( colorCount != expectedColorCount ) || ( colorantCount != expectedColorantCount ))
    return FALSE ;

  /* Iterate over each valid index value, set it as the current color, then
  get the base color */
  for ( i = 0 ; i < colorCount ; i ++ ) {
    /* Set the current color */
    colorChain->iColorValues[ 0 ] = ( USERVALUE ) i ;

    /* Get the color in the base color space */
    if ( !gsc_baseColor( colorInfo , colorType , &color , &colorantCount ) )
      return FALSE ;

    if ( colorantCount != expectedColorantCount )
      return FALSE ;

    /* Copy color into output list */
    for ( colorantIndex = 0 ; colorantIndex < colorantCount ; colorantIndex ++ )
      targetList[ colorantIndex ] = color[ colorantIndex ] ;

    targetList += colorantCount ;
  }

  colorChain->iColorValues[ 0 ] = oldValue ; /* Restore the old color */
  return TRUE ;
}

/* gsc_getChainOutputColors is used to transform the current color to
 * the output colorspace. The color it gives is after overprint reduction,
 * transfer, calibration et al but has not been quantized to device codes.
 */
Bool gsc_getChainOutputColors( GS_COLORinfo *colorInfo, int32 colorType,
                               COLORVALUE **oColorValues,
                               COLORANTINDEX **oColorants,
                               int32 *nColors,
                               Bool *fOverprinting )
{
  Bool result ;
  GS_CHAINinfo *colorChain = NULL;
  CLINK *pLink ;
  static COLORVALUE presepColorValue;

  HQASSERT( colorInfo , "colorInfo is NULL in gsc_getChainOutputColors" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getChainOutputColors");
  HQASSERT( oColorValues, "oColorValues is NULL in gsc_getChainOutputColors" ) ;
  HQASSERT( oColorants, "oColorants is NULL in gsc_getChainOutputColors" ) ;
  HQASSERT( nColors, "nColors is NULL in gsc_getChainOutputColors" ) ;
  HQASSERT( fOverprinting, "fOverprinting is NULL in gsc_getChainOutputColors" ) ;

  /* Need to disable color cache to get output colors from
   * device code link
   */
  disable_separation_detection() ;

  /* we must disable the color cache for this operation because we are going to pick
     up values direct from the chain, rather than just the resulting color stored in
     the cache. In order to do that we need to make sure we don't rebuild the chain,
     because caching is controlled from the head node */

  result = gsc_constructChain( colorInfo, colorType );

  if (result) {
    int32 savedCacheFlags;
    GS_CHAIN_CONTEXT *chainContext;

    colorChain = colorInfo->chainInfo[ colorType ] ;
    HQASSERT(colorChain != NULL, "colorChain NULL");

    chainContext = colorChain->context;
    HQASSERT(chainContext != NULL, "colorChain->context NULL");
    HQASSERT(chainContext->pnext, "context->pnext NULL" ) ;

    /* Prevent the invalidation of the chain until we have finished with it.
     * That is, due to the same bizarre reasons as gsc_invokeChainSingle
     * protects itself.
     */
    CLINK_RESERVE(chainContext) ;

    savedCacheFlags = chainContext->cacheFlags;
    chainContext->cacheFlags = chainContext->cacheFlags & ~GSC_ENABLE_COLOR_CACHE;

    result = gsc_invokeChainSingle( colorInfo, colorType ) ;

    chainContext->cacheFlags = savedCacheFlags;

    if (result) {
      /* Walk the color chain until we hit the last link */
      pLink = chainContext->pnext ;
      HQASSERT( pLink != NULL, "pLink NULL" ) ;

      while ( pLink->pnext )
        pLink = pLink->pnext ;

      /* Now get the color information from the final clink */
      switch (pLink->linkType) {
      case CL_TYPEdevicecode:
        cc_deviceGetOutputColors( pLink,
                                  oColorValues, oColorants, nColors, fOverprinting ) ;
        break ;
      case CL_TYPEpresep:
        HQASSERT(pLink->n_iColorants == 1, "Too many colorants for presep") ;
        *nColors       = pLink->n_iColorants ;
        *oColorants    = pLink->iColorants ;
        /* We report presep colors as DeviceGray, so invert the values */
        presepColorValue = FLOAT_TO_COLORVALUE(1.0f - pLink->iColorValues[0]) ;
        *oColorValues  = &presepColorValue ;
        *fOverprinting = gsc_getoverprint( colorInfo, colorType ) ;
        break ;
      default:
        HQFAIL("Invalid link type at tail of color chain") ;
        break ;
      }
    }

    /* We've now finished with this chain, for the purposes of this function */
    cc_destroyChainContext(&chainContext);
  }

  enable_separation_detection() ;

  return result ;
}

/* -------------------------------------------------------------------------- */

Bool gsc_invokeChainTransform(GS_COLORinfo *colorInfo,
                              int32 colorType,
                              COLORSPACE_ID oColorSpace,
                              Bool forCurrentCMYKColor,
                              USERVALUE *oColorValues)
{
  int32 i;
  CLINK **pFirstLink;
  int32 nDeviceColorants;
  GS_CHAINinfo *colorChain;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_invokeChainTransform");
  HQASSERT(oColorValues != NULL, "oColorValues NULL");

  colorChain = colorInfo->chainInfo[colorType];
  HQASSERT(colorChain != NULL, "colorChain NULL");
  if (!cc_createChainContextIfNecessary(&colorChain->context, colorInfo))
    return FALSE;
  chainContext = colorChain->context;

  switch (oColorSpace) {
  case SPACE_DeviceCMYK:
    pFirstLink = &colorChain->context->pCurrentCMYK;
    nDeviceColorants = 4;
    break;
  case SPACE_DeviceRGB:
    pFirstLink = &colorChain->context->pCurrentRGB;
    nDeviceColorants = 3;
    break;
  case SPACE_DeviceGray:
    pFirstLink = &colorChain->context->pCurrentGray;
    nDeviceColorants = 1;
    break;
  default:
    HQFAIL("Uncatered for oColorSpace");
    return error_handler(UNREGISTERED);
  }

  /* A few early returns for obvious cases */
  if (colorChain->iColorSpace == oColorSpace) {
    /* Matching input and output just returns the input colors */
    for (i = 0; i < nDeviceColorants; i++)
      oColorValues[i] = colorChain->iColorValues[i];
    return TRUE;
  }
  else if (colorChain->iColorSpace == SPACE_Pattern ||
           colorChain->iColorSpace == SPACE_PatternMask) {
    /* These return default values */
    for (i = 0; i < nDeviceColorants; i++)
      oColorValues[i] = 0.0;
    if (oColorSpace == SPACE_DeviceCMYK)
      oColorValues[3] = 1.0;
    return TRUE;
  }

  return cc_invokeChainTransform(colorInfo, colorType,
                                 colorChain->iColorValues,
                                 colorChain->n_iColorants,
                                 &colorChain->colorspace,
                                 colorChain->iColorSpace,
                                 oColorSpace,
                                 forCurrentCMYKColor,
                                 pFirstLink, oColorValues);
}

static Bool cc_invokeChainTransform(GS_COLORinfo   *colorInfo,
                                    int32          colorType,
                                    USERVALUE      *iColorValues,
                                    int32          nColors,
                                    OBJECT         *colorSpaceObj,
                                    COLORSPACE_ID  iColorSpace,
                                    COLORSPACE_ID  oColorSpace,
                                    Bool           forCurrentCMYKColor,
                                    CLINK          **pFirstLink,
                                    USERVALUE      *oColorValues)
{
  int32 i;
  Bool result = TRUE;
  Bool inputSpaceOK = FALSE;
  CLINK *pLink;
  DEVICESPACEID deviceSpace;
  int32 nDeviceColorants;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "cc_invokeChainTransform");
  HQASSERT(pFirstLink != NULL, "pFirstLink NULL");
  HQASSERT(oColorValues != NULL, "oColorValues NULL");

  switch (oColorSpace) {
  case SPACE_DeviceCMYK:
    deviceSpace = DEVICESPACE_CMYK;
    nDeviceColorants = 4;
    break;
  case SPACE_DeviceRGB:
    deviceSpace = DEVICESPACE_RGB;
    nDeviceColorants = 3;
    break;
  case SPACE_DeviceGray:
    deviceSpace = DEVICESPACE_Gray;
    nDeviceColorants = 1;
    break;
  default:
    HQFAIL("Uncatered for oColorSpace");
    return error_handler(UNREGISTERED);
  }

  /* Now the non-obvious cases for which construction of a currentcolor chain,
   * with no color management or interceptions, is necessary. That is, except
   * we do want named color interception for all the non-currentxxxxcolor
   * uses at present.
   */
  if (*pFirstLink == NULL) {
    if (!cc_constructChain(colorInfo,
                           colorInfo->targetRS,
                           colorType,
                           FALSE,
                           NULL,
                           pFirstLink,
                           colorSpaceObj,
                           iColorSpace,
                           nColors,
                           deviceSpace,
                           nDeviceColorants,
                           SPACE_notset,
                           !forCurrentCMYKColor))   /* named color interception */
      return FALSE;
  }

  /* This function is used by currentcmykcolor and friends, and also elsewhere
   * in the rip. For currentxxxxcolor, we need to bail out with default values
   * if so deemed by the RB. For other uses, we want to know the converted
   * color regardless.
   */
  if (forCurrentCMYKColor) {
    pLink = *pFirstLink;
    HQASSERT(pLink != NULL, "pLink NULL");

    while (pLink->pnext != NULL && !inputSpaceOK) {
      switch (pLink->iColorSpace) {
      case SPACE_DeviceCMYK:
      case SPACE_DeviceRGB:
      case SPACE_DeviceGray:
        /* Carry on and invoke the chain */
        inputSpaceOK = TRUE;
        break;
      case SPACE_Separation:
      case SPACE_DeviceN:
      case SPACE_Indexed:
        /* Look at the alternate or base space and see if that's a device space */
        break;
      default:
        /* Unacceptable spaces should return zero for all components */
        for (i = 0; i < nDeviceColorants; i++)
          oColorValues[i] = 0.0;
        if (oColorSpace == SPACE_DeviceCMYK)
          oColorValues[3] = 1.0;
        return TRUE;
      }

      pLink = pLink->pnext;
    }
  }

  /* The invokes for each CLINK may call out to PS, which could (if someone
   * is being mean, or stoopid) execute operators which cause the color
   * chain to be invalidated there and then (rather than deferred - using the
   * gMustResetColorChains global). We catch this by putting our claim on the
   * CLINKs, so that they don't get destroyed during the invoke).
   */
  chainContext = colorInfo->chainInfo[colorType]->context;
  CLINK_RESERVE(chainContext);

  disable_separation_detection();

  pLink = *pFirstLink;
  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pLink->n_iColorants == nColors, "Inconsistent number of colorants");

  for (i = 0; i < nColors; i++)
    pLink->iColorValues[i] = iColorValues[i];

  while (pLink->pnext != NULL) {
    result = (pLink->functions->invokeSingle)(pLink, pLink->pnext->iColorValues);
    if (!result)
      break;

    pLink = pLink->pnext;
  }

  if (result) {
    /* There is no need to invoke the last link, but it does contain the color
     * values expected by currentcmykcolor & friends after invoking the chain.
     */
    HQASSERT(pLink->linkType == CL_TYPEdummyfinallink,
             "Expected a dummy final link for transform chain");
    HQASSERT(nDeviceColorants == pLink->n_iColorants,
             "Unexpected number of colorants");
    for (i = 0; i < pLink->n_iColorants; i++)
      oColorValues[i] = pLink->iColorValues[i];
  }

  enable_separation_detection();

  cc_destroyChainContext(&chainContext);

  return result;
}

/* Helper routine for gsc_invokeChainBlock
 * to actually invoke block on each CLINK
 */
static Bool cc_iterateChainBlock(CLINK      *pFirstLink,
                                 CLINKblock *pBlock)
{
  CLINK *pLastLink = pFirstLink ;
  USERVALUE *piColorValues = pBlock->iColorValues ;
  uint8 *poverprintProcess = pBlock->overprintProcess ;
  USERVALUE *blackValueToPreserve = pBlock->blackValueToPreserve;
  GSC_BLACK_TYPE *blackType = pBlock->blackType;
  int32 nColor;
  USERVALUE *saved_iColorValues = pBlock->iColorValues;
  Bool success;

  if ( pFirstLink->pnext != NULL ) {

    /* Use tmpColorValues as the output of the penultimate link and input to final link. */
    USERVALUE *colorValues = pBlock->tmpColorValues ;
    pBlock->iColorValues = pBlock->tmpColorValues ;

    /* Iterate through the chain for each color value */
    for (nColor = 0; nColor < pBlock->nColors; nColor++) {
      int32 i ;
      CLINK *pThisLink = pFirstLink ;
      CLINK *pNextLink = pThisLink->pnext ;

      /* Copy the color from the head link into the first link of the chain */
      for (i = 0; i < pThisLink->n_iColorants; ++i)
        pThisLink->iColorValues[i] = piColorValues[i];
      pThisLink->blackType = pBlock->origBlackType;
      HQASSERT(pThisLink->blackValueToPreserve == 0.0f, "blackValueToPreserve != 0.0");

      /* Initialise the pLink's cmyk overprint flags to off */
      pThisLink->overprintProcess = 0;

      /* Invoke all CLINKs apart from the last two. */
      while ( pNextLink->pnext != NULL ) {

        if ( ! (pThisLink->functions->invokeSingle)( pThisLink , pNextLink->iColorValues ))
          return FALSE ;

        pNextLink->overprintProcess = pThisLink->overprintProcess ;
        pNextLink->blackValueToPreserve = pThisLink->blackValueToPreserve;
        pNextLink->blackType = pThisLink->blackType;

        pThisLink = pNextLink ;
        pNextLink = pNextLink->pnext ;
      }

      /* Invoke the penultimate CLINK - placing the output values into the block */
      if ( ! (pThisLink->functions->invokeSingle)( pThisLink , colorValues ))
        return FALSE ;

      *poverprintProcess++ = pThisLink->overprintProcess ;
      *blackValueToPreserve++ = pThisLink->blackValueToPreserve ;
      *blackType++ = pThisLink->blackType ;

      pLastLink = pNextLink ;
      piColorValues += pFirstLink->n_iColorants ;
      colorValues += pLastLink->n_iColorants ;
    }
  }
  else {
    for (nColor = 0; nColor < pBlock->nColors; nColor++)
      poverprintProcess[nColor] = 0;
  }

  /* Invoke the Block routine for the last CLINK (the convert to device code one. */
  success = (pLastLink->functions->invokeBlock)( pLastLink , pBlock ) ;

  pBlock->iColorValues = saved_iColorValues;

  return success;
}

/* Specialist analogue of gsc_invokeChainBlock for the fast rgb to gray method.
 * The rgb to gray has already been done using luts by the client, so this
 * function only needs to invoke the devicecode link.
 */
Bool invokeDevicecodeBlock( CLINK *pFirstLink ,
                            USERVALUE *piColorValues ,
                            COLORVALUE *poColorValues ,
                            int32 nColors )
{
  CLINK *pLastLink ;
  CLINKblock cBlock ;
  int32 i;

  HQASSERT( pFirstLink != NULL && pFirstLink->linkType == CL_TYPErgbtogray,
            "Expected rgbtogray link" ) ;

  pLastLink = pFirstLink->pnext ;
  if ( pLastLink != NULL && pLastLink->linkType != CL_TYPEdevicecode ) {
    HQFAIL("Expected devicecode link");
    return FALSE;
  }

  cBlock.nColors = nColors ;
  cBlock.iColorValues = piColorValues ;
  cBlock.tmpColorValues = NULL ;
  cBlock.deviceCodes = poColorValues ;
  cBlock.blockOverprint = NULL ;

  for (i = 0; i < nColors; i++) {
    /* This is not necessary because overprintProcess is only used for 4 components,
     * but we'll keep it to make the data clean.
     */
    cBlock.overprintProcess[i] = 0;
    /* cBlock.blackValueToPreserve[i] = 0; Not necessary */
  }

  /* Invoke the Block routine for the last CLINK (the convert to device code one. */
  return (pLastLink->functions->invokeBlock)( pLastLink , & cBlock ) ;
}

Bool gsc_invokeChainBlock( GS_COLORinfo *colorInfo ,
                           int32 colorType ,
                           USERVALUE *piColorValues ,
                           COLORVALUE *poColorValues ,
                           int32 nColors )
{
  GUCR_RASTERSTYLE *hRasterStyle;
  CLINK *pFirstLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;
  CLINKblock cBlock ;

  HQASSERT( colorInfo , "colorInfo NULL" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_invokeChainBlock");
  HQASSERT( piColorValues , "piColorValues NULL" ) ;
  HQASSERT( poColorValues , "poColorValues NULL" ) ;
  HQASSERT( nColors > 0 , "nColors should be > 0" ) ;
  HQASSERT( nColors <= GSC_BLOCK_MAXCOLORS, "nColors too big") ;

  hRasterStyle = gsc_getTargetRS(colorInfo);

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  pFirstLink = chainContext->pnext ;
  HQASSERT(pFirstLink != NULL, "pFirstLink NULL");

  /* The invokes for each CLINK may call out to PS, which could (if someone
   * is being mean, or stoopid) execute operators which cause the color
   * chain to be invalidated there and then (rather than deferred - using the
   * gMustResetColorChains global). We catch this by putting our claim on the
   * CLINKs, so that they don't get destroyed during the invoke).
   */
  CLINK_RESERVE(chainContext);

  if (chainContext->blockTmpArraySize == 0) {
    int32 nColorants;
    if (!cc_maxChainColorants(colorChain, &nColorants)) {
      cc_destroyChainContext(&chainContext);
      return FALSE;
    }
    chainContext->blockTmpArraySize = nColorants * GSC_BLOCK_MAXCOLORS * sizeof(USERVALUE);
    chainContext->blockTmpArray = mm_alloc(mm_pool_color,
                                           chainContext->blockTmpArraySize,
                                           MM_ALLOC_CLASS_NCOLOR);
    if (chainContext->blockTmpArray == NULL) {
      chainContext->blockTmpArraySize = 0;
      cc_destroyChainContext(&chainContext);
      return error_handler(VMERROR);
    }
  }

  cBlock.nColors = nColors ;
  cBlock.iColorValues = piColorValues ;
  cBlock.tmpColorValues = chainContext->blockTmpArray ;
  cBlock.deviceCodes = poColorValues ;
  cBlock.colorChain = colorChain;
  cBlock.blockOverprint = chainContext->blockOverprint ;

  /* Initialise the pLink's cmyk overprint flags to off */
  pFirstLink->overprintProcess = 0 ;

  /* Initialise the blackType to that of the colorChain.
   * This is only of relevance for backdrop chains where we might be preserving
   * black. NB. When the blackType changes in the backdrop, the colorChain is
   * always rebuilt.
   */
  cBlock.origBlackType = colorChain->inBlackType;

  /* For SHFILL chains, we may need to work out the MAXBLT overprint flags and
     channels required for the block. Note that unlike the single invoke, the
     nonintercept invoke happens first for blocks. This is so that we can use
     the devicecode buffer as temporary storage without overwriting the
     devicecodes we wanted. Since the overprint information is accumulated in
     a separate mask and applied later, we do not need to know the actual
     devicecodes that will be produced at this stage.
   */
  if (chainContext->fApplyMaxBlts &&
      cc_isblockoverprinting(chainContext->blockOverprint)) {
    /* then we might be overprinting. */

    /* Lazily create a simple chain without any color management with the
     * exception of black preservation. Invoke it to gather the overprint
     * info within cBlock.
     */
    if (chainContext->pSimpleLink == NULL) {
      COLORSPACE_ID calibrationColorSpace ;
      DEVICESPACEID RealDeviceSpace ;
      int32 nDeviceColorants ;

      HQASSERT((void *)piColorValues != (void *)poColorValues,
               "Input and output buffers for gsc_invokeChainBlock are the same") ;
      guc_deviceColorSpace( hRasterStyle, &RealDeviceSpace, &nDeviceColorants ) ;
      guc_calibrationColorSpace( hRasterStyle, &calibrationColorSpace ) ;

      if ( ! cc_constructChain( colorInfo ,
                                hRasterStyle ,
                                colorType ,
                                TRUE , /* a simple chain, not allowing intercepts, and ... */
                                colorChain ,
                                &chainContext->pSimpleLink , /* ... not the ordinary head link! */
                                &colorChain->colorspace ,
                                colorChain->iColorSpace ,
                                colorChain->n_iColorants ,
                                RealDeviceSpace ,
                                nDeviceColorants ,
                                calibrationColorSpace ,
                                FALSE)) {
        /* oops */
        cc_destroyChainContext(&chainContext);
        return FALSE;
      }
    }

    /* Evaluate the simple chain over the block to get the overprint info */
    if (!cc_iterateChainBlock(chainContext->pSimpleLink, &cBlock)) {
      cc_destroyChainContext(&chainContext);
      return FALSE ;
    }
  }

  /* Now iterate the block, calling the device code convert routine */
  if (!cc_iterateChainBlock(pFirstLink, &cBlock)) {
    cc_destroyChainContext(&chainContext);
    return FALSE ;
  }

  /* We've now finished with this chain, for the purposes of this invoke */
  cc_destroyChainContext(&chainContext);

  return TRUE ;
}

/* Converts the color for 'nColors' values via Tom's Tables.
 */
Bool gsc_invokeChainBlockViaTable(GS_COLORinfo *colorInfo,
                                  int32 colorType,
                                  int32 *piColorValues,
                                  COLORVALUE *poColorValues,
                                  int32 nColors)
{
  GS_CHAINinfo *colorChain;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT( colorInfo , "colorInfo NULL" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_invokeChainBlock");
  HQASSERT( piColorValues , "piColorValues NULL" ) ;
  HQASSERT( poColorValues , "poColorValues NULL" ) ;
  HQASSERT( nColors > 0 , "nColors should be > 0" ) ;
  HQASSERT( nColors <= GSC_BLOCK_MAXCOLORS, "nColors too big") ;
  /* We could have GSC_SHFILL with PoorShading. */
  HQASSERT(colorType == GSC_IMAGE || colorType == GSC_SHFILL || colorType == GSC_BACKDROP,
           "Tom's Tables should only be called for images");

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  if (chainContext->tomsTable == NULL) {
    chainContext->tomsTable = cc_createTomsTable(colorInfo, colorType);
    if (chainContext->tomsTable == NULL)
      return FALSE;
  }

  if (!cc_invokeTomsTable(colorInfo, colorType,
                          piColorValues, poColorValues, nColors,
                          chainContext->tomsTable))
    return FALSE;

  return TRUE;
}

Bool gsc_populateHalftoneCache(GS_COLORinfo *colorInfo , int32 colorType ,
                               COLORVALUE *poColorValues ,
                               int32 nColors )
{
  CLINK *pFirstLink ;
  CLINK *pLastLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;
  CLINKblock cBlock ;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_populateHalftoneCache" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_populateHalftoneCache");
  HQASSERT( poColorValues , "poColorValues NULL in gsc_populateHalftoneCache" ) ;
  HQASSERT( nColors > 0 , "nColors should be > 0 in gsc_populateHalftoneCache" ) ;

  if ( ! gsc_constructChain( colorInfo, colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  /* if we don't have a devicecode link at the tail of the chain, we don't have
   * a normal chain and thus can't update the halftone cache.
   */
  if (colorChain->fSoftMaskLuminosityChain)
    return TRUE;

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  pFirstLink = colorChain->context->pnext ;
  HQASSERT( pFirstLink, "pFirstLink NULL" ) ;
  pLastLink = pFirstLink ;

  cBlock.nColors = nColors ;
  cBlock.iColorValues = NULL ;
  cBlock.deviceCodes = poColorValues ;
  if ( pFirstLink->pnext != NULL ) {
    CLINK *pThisLink = pFirstLink ;
    CLINK *pNextLink = pThisLink->pnext ;

    /* Find the last link */
    while ( pNextLink->pnext != NULL ) {
      pThisLink = pNextLink ;
      pNextLink = pThisLink->pnext ;
    }
    pLastLink = pNextLink ;
  }
  /* Invoke the populate routine for the last CLINK (the convert to device code one. */
  return cc_devicecode_populate( pLastLink, &cBlock ) ;
}


/* Tests whether a CIE colorspace is a device independent form of a simple
 * device space.
 */
static Bool CIEActingAsSimpleSpace(TRANSFORM_LINK_INFO *pInfo)
{
  int32         index;
  SYSTEMVALUE   range[2];

  HQASSERT(pInfo != NULL, "pInfo is NULL in CIEActingAsSimpleSpace");
  HQASSERT(ColorspaceIsCIEBased(pInfo->inputColorSpaceId), "colorspace is not CIE in CIEActingAsSimpleSpace");

  /* The test currently returns TRUE if the range of all components is 0->1.
   * It may be that we will need more in the future, eg. we do not distinguish
   * between CIEBasedABC spaces that are expressing an underlying DeviceRGB or
   * some arbitrary DeviceN space. The answer is important for pre-applying
   * transfers because a) one is additive and the other subtractive and b) the
   * colorant order must be correct for the transfers. Currently, we assume a
   * simple mapping onto DeviceCMYK, DeviceRGB or DeviceGray.
   */
  /** \todo JJ  for (index = 0; index < pLink->n_iColorants; index++) { */
  /**** Doing this hack properly (sic) means knowing how many components.
   **** Eventually, we won't need to know because we'll just ask the colorspace if
   **** it's a calbrated device space ******/
  for (index = 0; index < 1; index++) {
    switch (pInfo->inputColorSpaceId) {
    case SPACE_CIEBasedA:
      cc_getCieBasedABCRange(pInfo->u.ciebaseda, index, range);
      break;
    case SPACE_CIEBasedABC:
      cc_getCieBasedABCRange(pInfo->u.ciebasedabc, index, range);
      break;
    case SPACE_CIEBasedDEF:
      cc_getCieBasedDEFRange(pInfo->u.ciebaseddef, index, range);
      break;
    case SPACE_CIEBasedDEFG:
      cc_getCieBasedDEFGRange(pInfo->u.ciebaseddefg, index, range);
      break;
    case SPACE_CIETableA:
      cc_getCieTableRange(pInfo->u.cietablea, index, range);
      break;
    case SPACE_CIETableABC:
      cc_getCieTableRange(pInfo->u.cietableabc, index, range);
      break;
    case SPACE_CIETableABCD:
      cc_getCieTableRange(pInfo->u.cietableabcd, index, range);
      break;
    case SPACE_Lab:
      cc_getLabRange(pInfo->u.lab, index, range);
      break;
    case SPACE_ICCBased:
      cc_getICCInfoRange(pInfo->u.icc, index, range);
      break;
    case SPACE_CalGray:
    case SPACE_CalRGB:
      return TRUE;
    case SPACE_CMM:
    default:
      HQFAIL("Unexpected colorspace");
      return FALSE;
    }

    if (range[0] != 0.0 || range[1] != 1.0)
      return FALSE;
  }

  return TRUE;
}

/* Adds a link to the colorchain.
 */
static CLINK **cc_addLink( CLINK **pThisLink, CLINK *pNextLink )
{
  HQASSERT(pThisLink != NULL, "pThisLink NULL");
  HQASSERT(pNextLink != NULL, "pNextLink NULL");

  *pThisLink = pNextLink ;
  return & pNextLink->pnext ;
}


static Bool applyIntercept(COLORSPACE_ID            colorspaceId,
                           GS_COLORinfo             *colorInfo,
                           int32                    colorType,
                           GUCR_RASTERSTYLE         *hRasterStyle,
                           GS_CONSTRUCT_CONTEXT     *context,
                           GS_CHAINinfo             *colorChain,
                           COLORANTINDEX            *pColorantIndexArray,
                           TRANSFORM_LINK_INFO      *currentReproSpaceInfo,
                           COLORSPACE_ID            *inIntercept)
{
  HQASSERT(colorChain != NULL, "colorChain NULL");

  *inIntercept = SPACE_notset;

  /* This must be set regardless of whether the colorSpaceId matches the device
   * space, otherwise PDF/X and emulation setups won't work correctly. If it
   * turns out there is no color management applied, the client will turn
   * fIntercepting off.
   */
  colorChain->context->fIntercepting = TRUE;

  *currentReproSpaceInfo = cc_getInterceptInfo(colorInfo, colorType,
                                               colorspaceId,
                                               context->fColorManage,
                                               colorChain->fCompositing,
                                               cc_incompleteChainIsSimple(colorChain));
  if (currentReproSpaceInfo->inputColorSpaceId == colorspaceId) {
    context->chainColorSpace = colorspaceId;
    return TRUE;
  }

  /* According to the PS/PDF specs, transfers should only be applied as one of
   * the last stages in opaque jobs. And only in the final conversion to the
   * output device with transparency.
   * But we apply them here when intercepting device color because we've found
   * fewer consistency problems with non-color managed setups. The same
   * consistency is required for opaque jobs sent through backdrop render; since
   * we don't know whether the job is transparent or not, we apply the transfers
   * to emulate what would happen without backdrop render.
   * (Transparent PDF jobs rarely set transfers for exactly the reason that they
   * aren't predictable, i.e. they're never used in real world transparency jobs
   * so it doesn't really matter that it's not as per spec.)
   */
  if ( context->nTransferLinks == 0 ) {
    CLINK *pNextLink;

    context->nTransferLinks++;
    pNextLink = cc_transfer_create(context->colorspacedimension,
                                   pColorantIndexArray,
                                   colorspaceId,
                                   gsc_getRequiredReproType(colorInfo,
                                                            colorType),
                                   context->jobColorSpaceIsGray,
                                   TRUE,      /* isFirstTransferLink */
                                   FALSE,     /* isLastTransferLink */
                                   context->applyJobPolarity,
                                   colorInfo->transferInfo,
                                   colorInfo->halftoneInfo,
                                   hRasterStyle,
                                   context->colorPageParams->forcePositive,
                                   &context->page->colorPageParams);
    if (pNextLink == NULL)
      return FALSE;
    context->pThisLink = cc_addLink( context->pThisLink, pNextLink ) ;
  }

  if (currentReproSpaceInfo->u.shared == NULL) {
    context->PSColorSpace = cc_getIntercept(colorInfo, colorType,
                                            colorspaceId,
                                            context->fColorManage,
                                            colorChain->fCompositing,
                                            cc_incompleteChainIsSimple(colorChain));
    context->chainColorSpace = currentReproSpaceInfo->inputColorSpaceId;
    HQASSERT(currentReproSpaceInfo->inputColorSpaceId == SPACE_Separation ||
             currentReproSpaceInfo->inputColorSpaceId == SPACE_DeviceN,
             "Intercept doesn't have an info struct");

    *inIntercept = colorspaceId;
  }

  return TRUE;
}

static Bool lookForOverride(TRANSFORM_LINK_INFO     *currentReproSpaceInfo,
                            GS_COLORinfo            *colorInfo,
                            int32                   colorType,
                            GS_CHAINinfo            *colorChain,
                            GS_CONSTRUCT_CONTEXT    *context,
                            Bool                    forceDeviceSpace,
                            Bool                    *overridden)
{
  int32 potentialOverrideDimensions = 0;
  COLORSPACE_ID potentialOverrideSpaceId = SPACE_notset;
  COLORSPACE_ID dummy_pcsSpaceId;
  Bool validProfile = TRUE;

  *overridden = FALSE;

  /* Look for a possible device space override for the current color space.
   * NB. The original device independent override work was based on Photoshop,
   *     when 26890 is done, we can remove that restriction.
   */
  switch (context->chainColorSpace) {
  case SPACE_CIEBasedDEFG:
    if (!colorInfo->params.photoshopInput ||
        !CIEActingAsSimpleSpace(currentReproSpaceInfo))
      break;
    /* This could be a photoshop CMYK masquerading as a DEFG. Note that we don't ask
       "is it a reasonable DEFG to replace with raw" because all Photoshop DEFG's
       are interpreted as CMYK when the intercept is on */
    potentialOverrideSpaceId = SPACE_DeviceCMYK;
    potentialOverrideDimensions = 4;
    break;

  case SPACE_CIEBasedABC:
    if (!cc_get_isPhotoshopRGB(currentReproSpaceInfo->u.ciebasedabc))
      break;
    /* FALL THROUGH */
  case SPACE_CIEBasedDEF:
    if (!colorInfo->params.photoshopInput ||
        !CIEActingAsSimpleSpace(currentReproSpaceInfo))
      break;
    /* FALL THROUGH */
  case SPACE_CalRGB:
    potentialOverrideSpaceId = SPACE_DeviceRGB;
    potentialOverrideDimensions = 3;
    break;

  case SPACE_CIEBasedA:
    if (!colorInfo->params.photoshopInput ||
        !CIEActingAsSimpleSpace(currentReproSpaceInfo))
      break;
    /* This could be a photoshop Gray masquerading as a CIEBasedA. Note that
       we don't ask "is it a reasonable A to replace with raw" because all
       Photoshop A's are interpreted as Gray when the intercept is on */
    /* FALL THROUGH */
  case SPACE_CalGray:
    potentialOverrideSpaceId = SPACE_DeviceGray;
    potentialOverrideDimensions = 1;
    break;

  case SPACE_ICCBased:
    /* In this query, we allow invalid profiles to return data on the chance
     * that the profile will be overridden. Otherwise it will be dealt with
     * below.
     */
    if (!cc_get_icc_details(currentReproSpaceInfo->u.icc,
                            FALSE,
                            &potentialOverrideDimensions,
                            &potentialOverrideSpaceId,
                            &dummy_pcsSpaceId))
      return FALSE;

    /* If we have an invalid ICC profile, we may be able to handle it by
     * forcing the use of the nearest device space, otherwise we have to abort.
     */
    if (!cc_valid_icc_profile(currentReproSpaceInfo->u.icc)) {
      validProfile = FALSE;
      if (!context->page->colorPageParams.abortOnBadICCProfile) {
        error_clear();
        forceDeviceSpace = TRUE;
      }
      else
        return FALSE;
    }
  }

  if (potentialOverrideSpaceId != SPACE_notset || forceDeviceSpace) {
    switch (potentialOverrideSpaceId) {
    case SPACE_DeviceCMYK:
    case SPACE_DeviceRGB:
    case SPACE_DeviceGray:
      if (forceDeviceSpace ||
          cc_getColorSpaceOverride(colorInfo->hcmsInfo,
                                   potentialOverrideSpaceId,
                                   colorType)) {
        context->chainColorSpace = potentialOverrideSpaceId;
        context->colorspacedimension = potentialOverrideDimensions;
        *overridden = TRUE;
        return TRUE;
      }

    default:
      if (forceDeviceSpace) {
        /* If we get here the ICC profile couldn't have had a simple device
         * alternate space. So, we'll carry on using the profile instead, if
         * it's valid.
         */
        HQASSERT(context->chainColorSpace == SPACE_ICCBased, "Expected an ICC colorspace");
        return validProfile;
      }
    }
  }


  /* We have to deal with DeviceN ICC profiles and with 100pc Black
   * preservation for ICCBased spaces.
   */
  if (colorChain != NULL &&
      context->chainColorSpace == SPACE_ICCBased) {
    int32 dimensions;
    COLORSPACE_ID colorSpaceId;
    COLORSPACE_ID pcsSpaceId;

    if (!cc_get_icc_details(currentReproSpaceInfo->u.icc,
                            TRUE,
                            &dimensions,
                            &colorSpaceId,
                            &pcsSpaceId))
      return FALSE;

    if (colorSpaceId == SPACE_DeviceN &&
        context->forceIccToDeviceN &&
        colorInfo->constructionDepth[colorType] == 1) {
      /* On entry, PSColorSpace should be of the form [/ICCBased OFILE dict].
       * On exit, PSColorSpace is DeviceN (with the profile as the alternative
       * space) if the profile contains a colorant tag. This will be used to
       * have a chance at rendering the colorants directly rather than using
       * the profile. PSColorSpace will be NULL otherwise, when the profile
       * will be used to convert to the PCS.
       * We only do this once, the constructionDepth test stops recursion.
       */
      if (!cc_get_icc_DeviceN(currentReproSpaceInfo->u.icc,
                             context->PSColorSpace,
                             &context->PSColorSpace))
        return FALSE;

      if (context->PSColorSpace != NULL) {
        context->chainColorSpace = colorSpaceId;
        context->colorspacedimension = dimensions;
        context->forceIccToDeviceN = FALSE;
        *overridden = TRUE;
        return TRUE;
      }
    }
  }

  return TRUE;
}


static Bool createTransformInfoList(GS_COLORinfo            *colorInfo,
                                    int32                   colorType,
                                    GUCR_RASTERSTYLE        *hRasterStyle,
                                    GS_CHAINinfo            *colorChain,
                                    Bool                    fPresepToGray,
                                    GS_CONSTRUCT_CONTEXT    *context)
{
  REPRO_ITERATOR *reproIterator = NULL;
  TRANSFORM_LINK_INFO blendInfo;
  Bool createdNewBlendInfo = FALSE;
  TRANSFORM_LINK_INFO currentReproSpaceInfo;
  TRANSFORM_LINK_INFO *prevLinkInfo = NULL;
  Bool status;
  int32 transformIdx;
  Bool reserveExistingInfo = TRUE;
  Bool overridden;
  COLORANTINDEX *pColorantIndexArray = context->pColorantIndexArray;

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++)
    context->transformList[transformIdx] = NULL;

  cc_initTransformInfo(&blendInfo);
  cc_initTransformInfo(&currentReproSpaceInfo);

  switch (context->chainColorSpace) {
  case SPACE_InterceptCMYK:
    if (!applyIntercept(SPACE_DeviceCMYK, colorInfo, colorType,
                        hRasterStyle, context, colorChain, pColorantIndexArray,
                        &currentReproSpaceInfo, &context->inIntercept))
      return FALSE;

    if (context->inIntercept == SPACE_DeviceCMYK) {
      if (guc_processMapped(colorInfo->deviceRS)) {
        /* Don't do max blitting or 100% color preservation if doing process substitution */
        colorChain->context->fIntercepting = FALSE;
      }
      return TRUE;
    }
    break;

  case SPACE_InterceptRGB:
    if (!applyIntercept(SPACE_DeviceRGB, colorInfo, colorType,
                        hRasterStyle, context, colorChain, pColorantIndexArray,
                        &currentReproSpaceInfo, &context->inIntercept))
      return FALSE;

    if (context->inIntercept == SPACE_DeviceRGB) {
      CLINK *pNextLink;

      /* Invert the colors when intercepting to DeviceN because we are
       * moving from an additive to a subtractive colorspace.
       * NB. Strictly, the output space here should be a DeviceN
       * colorspace, but it's not worth creating a new simple link type for
       * this so we'll just make use of rgbtocmy.
       */
      pNextLink = cc_rgbtocmy_create() ;
      if (pNextLink == NULL)
        return FALSE;
      context->pThisLink = cc_addLink( context->pThisLink, pNextLink ) ;

      return TRUE;
    }
    break;

  case SPACE_InterceptGray:
    if (!applyIntercept(SPACE_DeviceGray, colorInfo, colorType,
                        hRasterStyle, context, colorChain, pColorantIndexArray,
                        &currentReproSpaceInfo, &context->inIntercept))
      return FALSE;

    if (context->inIntercept == SPACE_DeviceGray) {
      CLINK *pNextLink;

      /* Invert the colors when intercepting to a Separation because we are
       * moving from an additive to a subtractive colorspace.
       * NB. Strictly, the output space here should be a Separation
       * colorspace, but it's not worth creating a new simple link type for
       * this so we'll just make use of graytok.
       */
      pNextLink = cc_graytok_create() ;
      if (pNextLink == NULL)
        return FALSE;
      context->pThisLink = cc_addLink( context->pThisLink, pNextLink ) ;

      return TRUE;
    }
    break;

  default:
    /* If there's no intercept, we are here for a CIE or ICC colorspace */
    if (!cc_createTransformInfo(colorInfo,
                                &currentReproSpaceInfo,
                                context->PSColorSpace))
      return FALSE;

    /* This is the one case of a new info struct that is only refered to from
     * this function and so doesn't need reserving. */
    reserveExistingInfo = FALSE;

    /* Check for DeviceGray pretending to be CIEBasedA or CalGray when recombining.
     * If this happens switch to DeviceGray to allow recombine interception.
     */
    if (fPresepToGray &&
        rcbn_intercepting() && ! rcbn_composite_page()) {
      HQASSERT(colorChain != NULL, "colorChain NULL");

      switch (context->chainColorSpace) {
      case SPACE_CIEBasedA:
      case SPACE_CalGray:
        if (CIEActingAsSimpleSpace(&currentReproSpaceInfo)) {
          context->chainColorSpace = SPACE_DeviceGray;
          HQASSERT(context->colorspacedimension == 1, "Expected 1 component");
          cc_destroyTransformInfo(&currentReproSpaceInfo);
          return TRUE;
        }
        break;
      }
    }

    /* We should now allow a device independent space to be overridden with the
     * nearest device space. We don't replace with the actual intercept here,
     * the device space may then be intercepted as though it were the input space
     * in another loop of chain construction.
     */
    if (!lookForOverride(&currentReproSpaceInfo, colorInfo, colorType,
                         colorChain, context, FALSE,
                         &overridden)) {
      cc_destroyTransformInfo(&currentReproSpaceInfo);
      return FALSE;
    }
    if (overridden) {
      cc_destroyTransformInfo(&currentReproSpaceInfo);

      /* We are here because we've overridden a CMYK ICC profile for the color
       * space and the first profile in setreproduction. This potentially means
       * that implicit overprinting will take place as though it were a
       * DeviceCMYK color space.
       */
      if (context->chainColorSpace == SPACE_DeviceCMYK)
        context->matchingICCprofiles = TRUE;

      return TRUE;
    }

    break; /* default */
  }

  /* We now need to set the rendering intent for the first profile in a transform.
   * This will normally match the intent chosen by setreproduction for the first
   * transform, but need not. Different intents may be applied to input and output
   * profiles when configured expertly.
   */
  currentReproSpaceInfo.intent = cc_sourceRenderingIntent(colorInfo, colorType);

  /* Set the BPC value. There should be only one value per reproduction dict, but
   * our implementation requires the same value for all profiles in a transform.
   */
  currentReproSpaceInfo.blackPointComp = cc_sourceBlackPointComp(colorInfo);

#define return DO_NOT_RETURN__GO_TO_cleanup_instead!

  status = TRUE;

  if (context->fColorManage &&
      cc_getrenderingintent(colorInfo) != SW_CMM_INTENT_NONE) {
    if (!cc_reproductionIteratorInit(colorInfo, colorType,
                                     &currentReproSpaceInfo, &reproIterator)) {
      status = FALSE;
      goto cleanup;
    }
  }
  else if (guc_backdropRasterStyle(hRasterStyle)) {
    COLORSPACE_ID tmp;
    COLORSPACE_ID blendSpaceId;
    OBJECT blendColorSpace = OBJECT_NOTVM_NOTHING ;

    /* Get the blend space. This could come from either the target raster style
     * or the override. Because blend spaces are used for both input and output
     * color conversions, preference is given to invertibility. Otherwise the
     * override is used. If the selected space is a device space (marked as not
     * invertible, we'll use the PS/PDF rules for conversion. Jobs tend not to
     * use non-invertible blend spaces where such an inverse conversion will
     * be required.
     */
    guc_deviceToColorSpaceId(hRasterStyle, &blendSpaceId);
    guc_colorSpace(hRasterStyle, &blendColorSpace);
    if (!cc_createTransformInfo(colorInfo,
                                &blendInfo,
                                &blendColorSpace)) {
      status = FALSE;
      goto cleanup;
    }
    createdNewBlendInfo = TRUE;

    if (context->fPaintingSoftMask &&
        !context->fSoftMaskLuminosityChain &&
        blendInfo.inputColorSpaceId == blendSpaceId) {
      /* We are processing a soft mask and the blend space is a device space.
       * The best thing to do is to ignore device independent color spaces and
       * convert input colors to the nearest device space; and thence through
       * device space conversions to the blend space. This is to maintain
       * compatibility with Acrobat.
       * The above isn't applicable to the soft mask luminosity chains in the
       * soft mask's parent group because we need XYZ values.
       * NB. Converting via an input profile would mean it's unlikely we'd see
       * a solid black or clear white in the soft mask which will usually be
       * undesirable.
       * NB. Resetting chainColorSpace is necessary for the back end when the
       * blend space is handled via an associated profile intercept.
       */
      context->chainColorSpace = currentReproSpaceInfo.inputColorSpaceId;
      status = lookForOverride(&currentReproSpaceInfo, colorInfo, colorType,
                               colorChain, context, TRUE,
                               &overridden);
      if (!status)
        goto cleanup;
      if (overridden)
        goto cleanup;     /* Successful */
      else {
        /* It is possible to get here with Lab objects, we'll just continue
         * with the original color space without overriding.
         */
       }
    }

    /* If we are overriding the blend space, replace it now */
    if (cc_getColorSpaceOverride(colorInfo->hcmsInfo,
                                 blendSpaceId,
                                 colorType) ||
        !cc_isInvertible(&blendInfo)) {
      cc_destroyTransformInfo(&blendInfo);
      createdNewBlendInfo = FALSE;
      blendInfo = cc_getBlendInfo(colorInfo, colorType, blendSpaceId);
    }

    /* Invert the input & output because blendInfo was created as an input space
     * but we are using it as an output space.
     */
    tmp = blendInfo.inputColorSpaceId;
    blendInfo.inputColorSpaceId = blendInfo.outputColorSpaceId;
    blendInfo.outputColorSpaceId = tmp;

    /* Set the intent & BPC for the blending transforms. For consistency, these
     * should be the same as the intent & BPC for the first profile in an output
     * transform.
     */
    blendInfo.intent = cc_sourceRenderingIntent(colorInfo, colorType);
    blendInfo.blackPointComp = cc_sourceBlackPointComp(colorInfo);
  }

  context->allowIntercept = FALSE;
  context->forceIccToDeviceN = FALSE;
  context->matchingICCprofiles = FALSE;

  transformIdx = 0;

  /* Loop around device independent colour cases. Then go back to main chain */
  while (status && currentReproSpaceInfo.inputColorSpaceId != SPACE_notset) {
    TRANSFORM_LINK_INFO *linkInfo;
    Bool getNextRepro = FALSE;
    Bool startNewTransform;

    /* Commit the current link info into the linked list */
    linkInfo = mm_alloc(mm_pool_color,
                        sizeof (TRANSFORM_LINK_INFO),
                        MM_ALLOC_CLASS_NCOLOR);
    if (linkInfo == NULL) {
      status = error_handler(VMERROR);
      break;
    }
    *linkInfo = currentReproSpaceInfo;    /* A structure copy */

    /* Retain the first info struct of (possibly multiple) transforms as they are processed */
    if (context->transformList[transformIdx] == NULL)
      context->transformList[transformIdx] = linkInfo;

    if (reserveExistingInfo)
      cc_reserveTransformInfo(linkInfo);
    /* Only the first info struct in the list might be new and not need reserving */
    reserveExistingInfo = TRUE;

    if (prevLinkInfo != NULL)
      prevLinkInfo->next = linkInfo;
    prevLinkInfo = linkInfo;
    linkInfo = NULL;

    switch (currentReproSpaceInfo.inputColorSpaceId) {

    case SPACE_CIEBasedDEFG:
    case SPACE_CIETableABCD:
    case SPACE_CIEBasedDEF:
    case SPACE_CIETableABC:
    case SPACE_CIETableA:
      /* A structure copy */
      currentReproSpaceInfo = *cc_followOnTransformLinkInfo(&currentReproSpaceInfo);
      /* To avoid asserts when building chain */
      currentReproSpaceInfo.blackPointComp = prevLinkInfo->blackPointComp;

      /* We need to cope with potential DeviceN/Separation follow-on colorspaces
       * for CIETableA(BC(D)) within a reproduction dictionary. The simplest is
       * to not allow it and error. */
      if (currentReproSpaceInfo.inputColorSpaceId == SPACE_Separation ||
          currentReproSpaceInfo.inputColorSpaceId == SPACE_DeviceN) {
        status = detail_error_handler(CONFIGURATIONERROR,
                                      "Illegal use of devicelink CIETable colorspace.");
      }
      break;

    default:
      getNextRepro = TRUE;
      break;
    }

    /* This section obtains the next color operation to be used for constructing
     * a color chain, when required, from an iterator that walks over the
     * structure created by setreproduction.
     */
    if (getNextRepro) {
      if (reproIterator != NULL) {
        cc_reproductionIteratorNext(reproIterator,
                                    &currentReproSpaceInfo,
                                    &startNewTransform);

        /* Start a new transform for emulation's, devicelink's & InputColorSpace's */
        if (startNewTransform) {
          prevLinkInfo = NULL;
          transformIdx++;
          if (context->fNonInterceptingChain)
            /* If we're in a non-intercepting chain, we could be attempting to
             * work out the max blitting of an ICCBased chain. For that purpose,
             * only process the first level of the reproduction dictionary.
             */
            break;
          if (transformIdx >= MAX_NEXTDEVICE_DICTS)
            status = detail_error_handler(LIMITCHECK, "Nested CMM transform level exceeded.");
        }
      }
      else {
        Bool useCRD = context->fColorManage && !colorChain->fCompositing;

        switch (currentReproSpaceInfo.outputColorSpaceId) {
        case SPACE_CIEXYZ:
        case SPACE_CIELab:
        case SPACE_ICCXYZ:
        case SPACE_ICCLab:
        case SPACE_HqnPCS:

          if (context->fSoftMaskLuminosityChain) {
            /* Creating a soft mask luminosity chain.  Finish the chain at
               XYZ space and add a luminosity CLINK (instead of devicecode)
               to extract the 'Y' component for use as the alpha channel. */
            cc_initTransformInfo(&currentReproSpaceInfo);
            currentReproSpaceInfo.inputColorSpaceId = SPACE_SoftMaskXYZ;
            context->finalDeviceSpace = TRUE;
          }
          else if (blendInfo.inputColorSpaceId != SPACE_notset) {
            currentReproSpaceInfo = blendInfo;
          }
          else if (!useCRD) {
            COLORSPACE_ID aimSpaceId;

            /* At the back end, we use the default profiles in preference to
             * CRDs, partly to avoid callouts to the interpreter, and partly
             * because the single CRD is usually not a good choice. At least
             * there is one default profile per device space.
             */
            guc_deviceToColorSpaceId(hRasterStyle, &aimSpaceId);
            currentReproSpaceInfo = cc_getDefaultInfo(colorInfo, aimSpaceId);
            currentReproSpaceInfo.inputColorSpaceId = currentReproSpaceInfo.outputColorSpaceId;
          }
          else {
            /* At the front end, use the CRD for compatibility with Genoa, etc.
             */
            cc_initTransformInfo(&currentReproSpaceInfo);
            currentReproSpaceInfo.inputColorSpaceId = SPACE_CIEXYZ;
            currentReproSpaceInfo.u.crd = colorInfo->crdInfo;
          }
          /* The outputColorSpaceId value isn't important here, but does allow
           * termination of this loop.
           */
          currentReproSpaceInfo.outputColorSpaceId = SPACE_notset;
          /* To avoid asserts when building chain */
          currentReproSpaceInfo.blackPointComp = prevLinkInfo->blackPointComp;
          break;

        default:
          cc_initTransformInfo(&currentReproSpaceInfo);
          break;
        }
      }
    }
  }

  /* Cull list of noop transforms */
  if (status) {
    int32 skip = 0;
    int32 potentialNextDimensions = 0;
    COLORSPACE_ID potentialNextSpaceId = SPACE_notset;
    COLORSPACE_ID dummy_pcsSpaceId;

    for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++) {
      TRANSFORM_LINK_INFO *currentInfo;
      int32 nProfilesInTransform = 0;
      Bool pureICC = TRUE;

      /* Work out if the transform is purely ICC and therefore appropriate for
       * culling.
       */
      for (currentInfo = context->transformList[transformIdx];
           currentInfo != NULL;
           currentInfo = currentInfo->next) {

        nProfilesInTransform++;

        if (currentInfo->inputColorSpaceId == SPACE_ICCBased ||
            currentInfo->inputColorSpaceId == SPACE_ICCXYZ ||
            currentInfo->inputColorSpaceId == SPACE_ICCLab) {
          EMPTY_STATEMENT();
        }
        else {
          pureICC = FALSE;
          break;
        }

        if (!cc_get_icc_details(currentInfo->u.icc,
                                TRUE,
                                &potentialNextDimensions,
                                &potentialNextSpaceId,
                                &dummy_pcsSpaceId)) {
          status = FALSE;
          break;
        }
      }

      if (!status)
        break;

      /* If we can optimise out a 2 profile transform with identical transforms
       * do so.
       * Similarly for 1 element transforms that result from explicitly setting
       * InputColorSpace to a device space to act as a 'pass-through' for that
       * reproduction type/color space.
       */
      if (pureICC &&
          nProfilesInTransform == 2 &&
          context->transformList[transformIdx]->u.icc ==
                      context->transformList[transformIdx]->next->u.icc) {
        skip++;
        destroyTransformInfoItem(context, transformIdx);
        context->transformList[transformIdx] = NULL;

        /* We are here because we have matching CMYK ICC profiles for the color
         * space and the first profile in setreproduction. The color space for
         * the chain will be either ICCbased or DeviceCMYK (with interception).
         * For ICCBased, this potentially means that implicit overprinting may
         * take place as though it were a DeviceCMYK color space.
         * For DeviceCMYK, extra settings here don't matter.
         */
        if (transformIdx == 0 && colorChain != NULL &&
            potentialNextSpaceId == SPACE_DeviceCMYK) {
          colorChain->context->fIntercepting = TRUE;
          context->matchingICCprofiles = TRUE;
        }
      }
      else if (nProfilesInTransform == 1 &&
               context->transformList[transformIdx]->outputColorSpaceId == SPACE_notset) {
        switch (context->transformList[transformIdx]->inputColorSpaceId) {
        case SPACE_DeviceCMYK: potentialNextDimensions = 4; break;
        case SPACE_DeviceRGB : potentialNextDimensions = 3; break;
        case SPACE_DeviceGray: potentialNextDimensions = 1; break;
        default: HQFAIL("Inconsistent use of device spaces in transform list"); break;
        }
        skip++;
        potentialNextSpaceId = currentInfo->inputColorSpaceId;
        destroyTransformInfoItem(context, transformIdx);
        context->transformList[transformIdx] = NULL;
      }
      else if (skip > 0) {
        context->transformList[transformIdx - skip] = context->transformList[transformIdx];
        context->transformList[transformIdx] = NULL;
      }
    }

    /* If we culled all transforms then avoid an infinite loop */
    if (context->transformList[0] == NULL) {
      context->chainColorSpace = potentialNextSpaceId;
      HQASSERT(context->colorspacedimension == potentialNextDimensions,
               "Dimensions should remain unchanged 'cos it must be the same profile");
    }
  }

cleanup:
  /* We've done the device independent cases for now.
   * Tidy up and go back to main chain construction.
   */
  if (reproIterator != NULL)
    cc_reproductionIteratorFinish(&reproIterator);

  if (createdNewBlendInfo)
    cc_destroyTransformInfo(&blendInfo);

  if (!status) {
    if (!reserveExistingInfo)
      cc_destroyTransformInfo(&currentReproSpaceInfo);

    destroyTransformInfoList(context);
  }

#undef return

  return status;
}

static void destroyTransformInfoItem(GS_CONSTRUCT_CONTEXT *context, int32 transformIdx)
{
  TRANSFORM_LINK_INFO *linkInfo;
  TRANSFORM_LINK_INFO *nextLinkInfo;
  linkInfo = context->transformList[transformIdx];

  while (linkInfo != NULL) {
    nextLinkInfo = linkInfo->next;

    cc_destroyTransformInfo(linkInfo);

    mm_free(mm_pool_color, linkInfo, sizeof (TRANSFORM_LINK_INFO));

    linkInfo = nextLinkInfo;
  }
}

static void destroyTransformInfoList(GS_CONSTRUCT_CONTEXT *context)
{
  int32 transformIdx;

  for (transformIdx = 0; transformIdx < MAX_NEXTDEVICE_DICTS; transformIdx++)
    destroyTransformInfoItem(context, transformIdx);
}

/* doDeviceN handles the Separation & DeviceN cases of chain construction.
 * This is complicated by having several cases to consider.
 *
 * 1. The basic case from the PS/PDF specs is for a Separation/DeviceN where all
 *    colorants are either renderable or not. If they are we can render them
 *    without modification, otherwise we use the alternate space and tint
 *    transform. This case can be configured using the AdobeProcessSeparations
 *    userparam and not settting named color interception. We don't set the
 *    userparam by default because users expect something more sophisticated.
 *
 * 2. Named color interception is done via named color databases which is handled
 *    in the tinttransform link. This is a documented Hqn extension widely used
 *    with color management to give a replacement alternate space & tint transform.
 *
 * 3. There is special handling of the All Separation where some devices will
 *    want to divert All to only CMYK or just K to prevent ink saturation.
 *
 * 4. All other cases are diverted through the interceptdevicen link which will
 *    work out the best way of colour managing the colorant set. It is typical
 *    for that link to create sub-chains that recursively call back into here.
 *    We prevent further recursion by testing for constructionDepth.
 *
 * 5. An auxilliary purpose of this link is to assist in deriving equivalent CMYK
 *    and sRGB values for named colorants. This is done with a sub-chain hanging
 *    off the chain head. For this 'transform' sub-chain, the 'colorChain' is NULL.
 */
static Bool doDeviceN(GS_COLORinfo              *colorInfo,
                      GUCR_RASTERSTYLE          *hRasterStyle,
                      int32                     colorType,
                      GS_CONSTRUCT_CONTEXT      *context,
                      GS_CHAINinfo              *colorChain,
                      Bool                      allowAutoSeparation)
{
  int32 i;
  CLINK *pNextLink;
  COLORSPACE_ID calibrationColorSpace;
  Bool useDeviceNIntercept = TRUE;

  HQASSERT(context->chainColorSpace == SPACE_Separation ||
           context->chainColorSpace == SPACE_DeviceN,
           "Expected a Separation/DeviceN colorspace");
  HQASSERT(theLen(*context->PSColorSpace) == 4 || theLen(*context->PSColorSpace) == 5,
           "Unexpected length of DeviceN colour space");

  if (context->inIntercept == SPACE_notset) {
    if (!cc_colorspaceNamesToIndex(hRasterStyle,
                                   context->PSColorSpace,
                                   allowAutoSeparation,
                                   TRUE,
                                   context->pColorantIndexArray,
                                   context->colorspacedimension,
                                   &colorInfo->params.excludedSeparations,
                                   &context->finalDeviceSpace))
      return FALSE;
  }
  else {
    HQASSERT(context->inIntercept == SPACE_DeviceCMYK ||
             context->inIntercept == SPACE_DeviceRGB ||
             context->inIntercept == SPACE_DeviceGray,
             "Unexpected intercept space when forcing a tint transform");
  }

  /* We're going to apply a number of rules to decide whether it is appropriate
   * to use the interceptdevicen link for handling this set of colorants. Also
   * decide whether to derive equivalent colors.
   */
  if (colorChain == NULL || context->inIntercept != SPACE_notset) {
    /* finalDeviceSpace is now true if all colorant names match an output channel.
     * However when building the transform sub-chain we dont want this to be TRUE.
     */
    context->finalDeviceSpace = FALSE;
    useDeviceNIntercept = FALSE;
    context->inIntercept = SPACE_notset;
  }
  else {
    Bool deriveEquivalents = TRUE;
    Bool usePSTintTransform = TRUE;
    Bool internalICCDeviceN;
    Bool allNone = TRUE;
    OBJECT *alternateSpaceObj = &oArray(*context->PSColorSpace)[2];
    COLORSPACE_ID alternateColorSpaceId;
    int32 nColorants;

    /* If this DeviceN was constructed by our code (standard ICCBased is of
     * length 2), then any None colorants are likely to be from XPS where a spot
     * is often represented as an ICC profile with 3 colorants, but 2 of the
     * colorants are /None.
     */
    if ( !gsc_getcolorspacesizeandtype(colorInfo, alternateSpaceObj,
                                       &alternateColorSpaceId, &nColorants))
      return FALSE;

    /* This is the limit of the number of colorants allowed in one color space.
     */
    if (nColorants >= BLIT_MAX_COLOR_CHANNELS)
      return detail_error_handler(LIMITCHECK, "Colorant limit exceeded.");

    internalICCDeviceN = alternateColorSpaceId == SPACE_ICCBased &&
                         theLen(*alternateSpaceObj) == 3;

    /* Decide whether to allow the use of the tint transform in deriving
     * equivalent RGB & CMYK values for individual colorants within a DeviceN.
     */
    for (i = 0; i < context->colorspacedimension; i++) {
      /* A DeviceN [All] always forces the use of the alternate space */
      if (context->pColorantIndexArray[i] == COLORANTINDEX_ALL) {
        HQASSERT(context->chainColorSpace == SPACE_Separation ||
                 !context->finalDeviceSpace, "finalDeviceSpace TRUE");
        deriveEquivalents = FALSE;
        useDeviceNIntercept = FALSE;
        break;
      }
      /* If there are None colorants in the DeviceN set, then we can't usually
       * rely on the tint transform to give us good CMYK equivalent values. But
       * we can where an ICCBased alternate space was constructed by our code.
       */
      if (context->pColorantIndexArray[i] == COLORANTINDEX_NONE) {
        if (!internalICCDeviceN)
          usePSTintTransform = FALSE;
      }
      else
        allNone = FALSE;
    }

    /* If we have a simple transform, there is no point deriving equivalents
     * from the tint transform because the equivalents must already exist when
     * the simple transform was created.
     */
    if (cc_csaGetSimpleTransform(context->PSColorSpace) != NULL)
      deriveEquivalents = FALSE;

    /* We have to process Separation None or DeviceN [None None ..]  using the
     * simple method.
     */
    if (allNone) {
      deriveEquivalents = FALSE;
      useDeviceNIntercept = FALSE;
    }

    /* If we're recursively constructing a chain, we want to use the simple
     * methods to avoid infinite recursion, and because the DeviceN interception
     * will be happening in the top level chain.
     */
    if (colorInfo->constructionDepth[colorType] > 1) {
      deriveEquivalents = FALSE;
      useDeviceNIntercept = FALSE;
    }

    /* Soft mask chains require that no clever interception is done, and that
     * the tint transform is used if the input colour space contains spots. This
     * is ensured by the asserts, but we must allow cc_interceptdevicen_create()
     * to be called for R/G/B separations to be correctly converted via the tint
     * transform.
     */
    if (context->fPaintingSoftMask) {
      HQASSERT(!context->allowNamedColorIntercept && !allowAutoSeparation,
               "Inconsistent soft mask color context");
      deriveEquivalents = FALSE;
    }

    /* Derive the sRGB & CMYK color values for new colorants - sRGB is used for
     * roam for separated output; CMYK is used to derive 'simple' tint transforms
     * for mixtures of spots when backdrop rendering which my be used in
     * compositing if the interceptdevicen link cannot be used. The CMYK values
     * are valid in the relevant hRasterStyle and all values are separately
     * evaluated in each raster style in which the colorant occurs. In practice,
     * only the bottommost raster style will actually invoke simple tint
     * transforms because the colorants are usually renderable in backdrops.
     * If usePSTintTransform is FALSE, the equivalent values will ONLY be derived
     * from named colour databases. If TRUE, and a colorant wasnt't found in a
     * database, then the original tint transform may be used.
     * If no CMYK equivalent is available for a non-renderable colorant, we will
     * avoid using the colorant in the display list by converting colors through
     * the tint transform.
     */
    if (deriveEquivalents) {
      if (!guc_setEquivalentColors(colorInfo, hRasterStyle,
                                   colorType, context->chainColorSpace,
                                   context->colorspacedimension,
                                   context->pColorantIndexArray,
                                   context->PSColorSpace,
                                   usePSTintTransform))
        return FALSE;
    }
  }

  guc_calibrationColorSpace( hRasterStyle, &calibrationColorSpace ) ;

  /* The All Separation case */
  if (context->finalDeviceSpace &&
      context->colorspacedimension == 1 &&
      context->pColorantIndexArray[0] == COLORANTINDEX_ALL) {
    int32 convertAllSeparation;

    HQASSERT(colorInfo->params.convertAllSeparation == GSC_CONVERTALLSEPARATION_ALL ||
             colorInfo->params.convertAllSeparation == GSC_CONVERTALLSEPARATION_BLACK ||
             colorInfo->params.convertAllSeparation == GSC_CONVERTALLSEPARATION_CMYK,
             "Unexpected value of convertAllSeparation");

    /* In the rather special case of the /All separation we need to remap
       the colorspace onto a DeviceN colorspace which includes all of the
       device colorants and the /All 'colorant'. However, if the graphics
       state indicates the convertAllSeparation Hqn extension, we should
       instead only do this to the Black separation or possibly to CMYK.
       Another special case is where we are printing to a PhotoInk device,
       here there is no point in converting the All separation to
       a DeviceN colorspace because we cannot calibrate in the final
       device space (which is the whole point). */

    if ((colorInfo->params.convertAllSeparation == GSC_CONVERTALLSEPARATION_ALL) &&
        (calibrationColorSpace == SPACE_notset)) {
      if (!getallseparationcolorants( hRasterStyle,
                                      &context->pColorantIndexArray,
                                      &context->colorantIndexArraySize,
                                      &context->colorspacedimension ))
        return FALSE;

      /* pColorantIndexArray should now be in order, e.g. [COLORANTINDEX_ALL, 0, 1, 2 ....]
       * We'll use COLORANTINDEX_ALL as single input colorant, and the
       * contents of pColorantIndexArray as the output colorants.
       */

      context->chainColorSpace = SPACE_DeviceN;
      convertAllSeparation = GSC_CONVERTALLSEPARATION_ALL;
    }
    else {
      if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                             context->pColorantIndexArray, 4))
        return FALSE;
      context->colorspacedimension = 4;
      context->finalDeviceSpace = FALSE ;
      context->chainColorSpace = SPACE_DeviceCMYK;
      convertAllSeparation = colorInfo->params.convertAllSeparation;

      context->PSColorSpace = NULL; /* dont care about this going into device spaces */
      context->allowNamedColorIntercept = FALSE ;
      context->allowIntercept = FALSE;
    }

    pNextLink = cc_allseptinttransform_create( 1,
                                               context->pColorantIndexArray,
                                               context->chainColorSpace,
                                               context->colorspacedimension,
                                               convertAllSeparation) ;
    if (pNextLink == NULL)
      return FALSE;

    context->pThisLink = cc_addLink( context->pThisLink, pNextLink );
  }

  else {
    /* Not Separation All */

    Bool applyTintTransform = TRUE;

    if (context->allowIntercept && useDeviceNIntercept) {
      Bool intercepted;
      Bool renderable;
      OBJECT *priorPSColorSpace = context->PSColorSpace;

      HQASSERT(colorChain != NULL, "colorChain NULL");

      /* Create link to handle color managed process and
       * spots together - returns updated PSColorSpace with
       * any additional process colorants needed */
      if (!cc_interceptdevicen_create(colorInfo,
                                      colorType,
                                      hRasterStyle,
                                      context->colorspacedimension,
                                      context->pColorantIndexArray,
                                      context->aimDeviceSpace,
                                      context->nAimDeviceColorants,
                                      colorChain,
                                      context->fColorManage,
                                      priorPSColorSpace,
                                      &pNextLink,
                                      &context->PSColorSpace,
                                      &intercepted,
                                      &renderable,
                                      &applyTintTransform))
        return FALSE;

      if (applyTintTransform) {
        /* If applyTintTransform is true, the DeviceN space wasn't intercepted,
         * probably because the color space was deemed simple enough for a normal
         * tint transform link.
         * Usually, the colorant set isn't renderable, but we set finalDeviceSpace
         * to cope with Separation Red in RGB where we want to use the tint
         * transform to get the color space treated correctly as subtractive.
         */
        HQASSERT(pNextLink == NULL, "Expected pNextLink NULL");
        context->finalDeviceSpace = FALSE;
      }
      else if (pNextLink != NULL) {
        /* The DeviceN space was intercepted */

        HQASSERT(!applyTintTransform, "Expected applyTintTransform FALSE");

        if (context->allowBlackPreservation &&
            !context->doneBlackRemove) {
          CLINK *blackremoveLink;
          USERVALUE blackTintThreshold;

          /* Now we know we're adding an interceptdevicen link, we need a removeblack
           * link added before it to apply black preservation. The removeblack link
           * needs to use the colorspace prior to the interceptdevicn.
           */
          blackTintThreshold = cc_getBlackTintIntercept(colorInfo->hcmsInfo, colorType);

          context->doneBlackRemove = TRUE;
          if (!cc_blackremove_create(colorChain->context,
                                     priorPSColorSpace,
                                     context->chainColorSpace,
                                     context->colorspacedimension,
                                     context->f100pcBlackRelevant,
                                     context->fBlackTintLuminance,
                                     context->fBlackTintRelevant,
                                     blackTintThreshold,
                                     colorChain->chainColorModel,
                                     hRasterStyle,
                                     &colorInfo->params.excludedSeparations,
                                     &blackremoveLink)) {
            /* Destroy the not yet linked interceptdevicen link */
            pNextLink->functions->destroy(pNextLink);
            return FALSE;
          }
          if (pNextLink != NULL)
            context->pThisLink = cc_addLink(context->pThisLink, blackremoveLink);
        }

        /* Now link in the interceptdevicen link */
        context->pThisLink = cc_addLink(context->pThisLink, pNextLink);

        /* Get dimension of new space */
        if ( !gsc_getcolorspacesizeandtype(colorInfo, context->PSColorSpace,
                                           &context->chainColorSpace,
                                           &context->colorspacedimension) )
          return FALSE;
        if (context->colorspacedimension > context->colorantIndexArraySize) {
          if (!extendColorantIndexArray(&context->pColorantIndexArray,
                                        &context->colorantIndexArraySize,
                                        context->colorspacedimension + STACK_EXTCIA_SIZE))
            return FALSE;
        }
        HQASSERT(context->chainColorSpace == SPACE_DeviceCMYK ||
                 context->chainColorSpace == SPACE_DeviceRGB ||
                 context->chainColorSpace == SPACE_DeviceGray ||
                 context->chainColorSpace == SPACE_DeviceN,
                 "Expected a device colorspace");

        /* If any component of the interceptdevicen link has colour managed
         * overprints then the main chain will need to apply maxblitting, so
         * transfer overprint information to the main chain.
         */
        if (intercepted)
          colorChain->context->fIntercepting = TRUE;
        if (renderable)
          context->transformedSpotType = DC_TRANSFORMEDSPOT_INTERCEPT;
        else
          context->transformedSpotType = DC_TRANSFORMEDSPOT_NORMAL;

        /* We don't want any added process colorants to be
         * intercepted or we could end up in a loop
         */
        context->allowNamedColorIntercept = FALSE;

        /* We've done color management in the interceptdevicen link */
        context->allowIntercept = FALSE;

        /* We have to go around chain construction loop once more to finish
         * things off, esp. getting the new colorantId list.
         */
        context->finalDeviceSpace = FALSE;
      }
    }

    /* The simple case of using an alternate space and tint transform. Possibly
     * using named color interception.
     */
    if (applyTintTransform && !context->finalDeviceSpace) {
      Bool fCompositing;

      if (context->allowBlackPreservation &&
          !context->doneBlackRemove) {
        USERVALUE blackTintThreshold;

        /* DeviceN spaces may contain Black, so requires black preservation.
         */
        blackTintThreshold = cc_getBlackTintIntercept(colorInfo->hcmsInfo, colorType);

        context->doneBlackRemove = TRUE;
        if (!cc_blackremove_create(colorChain->context,
                                   context->PSColorSpace,
                                   context->chainColorSpace,
                                   context->colorspacedimension,
                                   context->f100pcBlackRelevant,
                                   context->fBlackTintLuminance,
                                   context->fBlackTintRelevant,
                                   blackTintThreshold,
                                   colorChain->chainColorModel,
                                   hRasterStyle,
                                   &colorInfo->params.excludedSeparations,
                                   &pNextLink)) {
          return FALSE;
        }
        if (pNextLink != NULL)
          context->pThisLink = cc_addLink(context->pThisLink, pNextLink);
      }

      /* Don't do separation detection at the back end */
      fCompositing = colorChain != NULL ? colorChain->fCompositing : TRUE;

      pNextLink = cc_tinttransform_create( context->colorspacedimension,
                                           context->pColorantIndexArray,
                                           context->chainColorSpace,
                                           colorInfo,
                                           context->PSColorSpace,
                                           fCompositing,
                                           &context->PSColorSpace,
                                           context->allowNamedColorIntercept,
                                           &context->allowIntercept);
      if (pNextLink == NULL)
        return FALSE;
      context->pThisLink = cc_addLink( context->pThisLink, pNextLink ) ;

      /* Get dimension of alternate space which may be a DeviceN */
      if (!gsc_getcolorspacesizeandtype(colorInfo, context->PSColorSpace,
                                        &context->chainColorSpace,
                                        &context->colorspacedimension))
        return FALSE;
      if (context->colorspacedimension > context->colorantIndexArraySize) {
        if (!extendColorantIndexArray(&context->pColorantIndexArray,
                                      &context->colorantIndexArraySize,
                                      context->colorspacedimension + STACK_EXTCIA_SIZE))
          return FALSE;
      }

      context->transformedSpotType = DC_TRANSFORMEDSPOT_NORMAL ;

      context->allowNamedColorIntercept = FALSE;
    }
  }

  return TRUE;
}

static void initConstructContext(GS_CONSTRUCT_CONTEXT *context)
{
  int i;
  corecontext_t *coreContext = &CoreContext;

  for (i = 0; i < MAX_NEXTDEVICE_DICTS; i++)
    context->transformList[i] = NULL;
  context->currentInfo = NULL;
  context->forceIccToDeviceN = TRUE;
  context->pThisLink = NULL;
  context->PSColorSpace = NULL;
  context->chainColorSpace = SPACE_notset;
  context->finalDeviceSpace = FALSE;
  context->colorspacedimension = 0;
  context->fColorManage = FALSE;
  context->fPaintingSoftMask = FALSE;
  context->fSoftMaskLuminosityChain = FALSE;
  context->fNonInterceptingChain = FALSE ;
  context->allowIntercept = FALSE ;
  context->nTransferLinks = 0 ;
  context->applyJobPolarity = FALSE ;
  context->aimDeviceSpace = 0;
  context->nAimDeviceColorants = 0;
  context->jobColorSpaceIsGray = FALSE;
  context->pColorantIndexArray = context->sColorantIndexArray;
  context->colorantIndexArraySize = STACK_EXTCIA_SIZE ;
  context->allowNamedColorIntercept = FALSE;
  context->inIntercept = SPACE_notset;
  context->transformedSpotType = DC_TRANSFORMEDSPOT_ILLEGAL;
  context->allowBlackPreservation = FALSE;
  context->doneBlackEvaluate = FALSE;
  context->doneBlackRemove = FALSE;
  context->f100pcBlackRelevant = FALSE;
  context->fBlackTintRelevant = FALSE;
  context->fBlackTintLuminance = FALSE;
  context->blackevaluateLink = NULL;
  context->page = coreContext->page;
  context->colorPageParams = &coreContext->page->colorPageParams;
  context->matchingICCprofiles = FALSE;
}

/* cc_constructChain is used to construct a color processing chain for the
 * type of object specified by colorType (GSC_FILL, GSC_STROKE etc).

 * Pointers to head links for each colorType are held in the graphics state.

 * These pointers can point to the same head link but only head links with a
 * single reference may be modified (this restricts modifications to chains
 * which were created by the current gstate). This means that jobs which
 * execute gsave followed by grestore with no changes to the current color
 * or colorspace between will not create unneccessary copies of colorchains
 * or other structures. A NULL pointer indicates that the head link doesn't
 * exist.

 * Head links in turn can share colorchains but only colorchains which have
 * a single reference may be modified (this restricts modifications to
 * chains which were created by the current gstate). This means that
 * the common sequence setcolorspace gsave ... setcolor grestore is also
 * inexpensive. A NULL pointer indicates that the colorchain doesn't exist.

 * The result of this is that gsc_constructChain will only construct a
 * colorchain if a valid chain doesn't already exist. That is if the head
 * link contains a NULL pnext pointer. Note that if the head link is from
 * another gstate it must be updated before the chain is constructed.
 */
Bool cc_constructChain(GS_COLORinfo     *colorInfo,
                       GUCR_RASTERSTYLE *hRasterStyle,
                       int32            colorType,
                       Bool             fNonInterceptingChain,
                       GS_CHAINinfo     *colorChain,
                       CLINK            **pHeadLink,
                       OBJECT           *PSColorSpace,
                       COLORSPACE_ID    chainColorSpace,
                       int32            colorspacedimension,
                       DEVICESPACEID    RealDeviceSpace,
                       int32            nDeviceColorants,
                       COLORSPACE_ID    calibrationColorSpace,
                       Bool             forceNamedIntereptionForTransforms)
{
  Bool result = FALSE;
  int32 i;
  Bool allowAutoSeparation ;
  Bool fPresepLink;
  Bool fPresepToGray;
  Bool fPresepColorIsGray;
  CLINK *pNextLink;
  OBJECT customProcedure = OBJECT_NOTVM_NOTHING ;
  OBJECT *IndexedBasePSColorSpace = NULL ;
  uint8 saved_fIntercepting;
  int32 iterationsDone = 0;
  GS_CHAIN_CONTEXT *chainContext = NULL;
  Bool deferredGrayIntercept = FALSE;

  OBJECT manufacturedPSColorSpace = OBJECT_NOTVM_NOTHING;
  OBJECT patternScreenColorSpace = OBJECT_NOTVM_NOTHING;
  OBJECT patternScreenArray[4] = {OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
                                  OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING};

  GS_CONSTRUCT_CONTEXT context;

  initConstructContext(&context);
  context.PSColorSpace = PSColorSpace;
  context.chainColorSpace = chainColorSpace;
  context.colorspacedimension = colorspacedimension;
  context.aimDeviceSpace = RealDeviceSpace;
  context.nAimDeviceColorants = nDeviceColorants;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(pHeadLink != NULL, "pHeadLink NULL");
  HQASSERT(*pHeadLink == NULL, "*pHeadLink not NULL, memory leak");

  if (context.colorspacedimension > context.colorantIndexArraySize) {
    if (!extendColorantIndexArray(&context.pColorantIndexArray,
                                  &context.colorantIndexArraySize,
                                  context.colorspacedimension + STACK_EXTCIA_SIZE))
      return FALSE;
  }

  if (calibrationColorSpace != SPACE_notset) {
    /* If a calibration space is specified, then go to that, (only
     * applies to DeviceN PhotoInk devices).
     */
    switch (calibrationColorSpace) {
    case SPACE_DeviceGray:
      context.aimDeviceSpace = DEVICESPACE_Gray;
      context.nAimDeviceColorants = 1;
      break;
    case SPACE_DeviceRGB:
      context.aimDeviceSpace = DEVICESPACE_RGB;
      context.nAimDeviceColorants = 3;
      break;
    case SPACE_DeviceCMYK:
      context.aimDeviceSpace = DEVICESPACE_CMYK;
      context.nAimDeviceColorants = 4;
      break;
    default:
      HQFAIL("Invalid calibration space");
      break;
    }
  }

  if ( colorChain != NULL ) {
    /* Normal case */

    chainContext = colorChain->context;

    /* Allow color management interception:
     * - on main chains only (not transform or simple chains).
     * - for both interpreting and compositing - these will often be simplified
     *   because profiles match between blend spaces.
     * - not when deriving a soft mask.
     */
    context.fPaintingSoftMask = cc_getPaintingSoftMask(colorInfo);
    context.fSoftMaskLuminosityChain = colorChain->fSoftMaskLuminosityChain;
    context.fColorManage = !guc_backdropRasterStyle(gsc_getTargetRS(colorInfo));
    context.fNonInterceptingChain = fNonInterceptingChain;
    context.allowIntercept = !fNonInterceptingChain;
    context.allowNamedColorIntercept = context.allowIntercept &&
                                       !context.fPaintingSoftMask;

    HQASSERT(!context.fSoftMaskLuminosityChain ||
             (colorChain->fCompositing && context.fPaintingSoftMask),
             "Unexpected soft mask luminosity chain in cc_constructChain");

    if (!fNonInterceptingChain)
      chainContext->fIntercepting = FALSE;

    context.transformedSpotType = DC_TRANSFORMEDSPOT_NONE;
    fPresepToGray = TRUE ;
    fPresepColorIsGray = colorChain->fPresepColorIsGray;
    context.jobColorSpaceIsGray = (colorChain->iColorSpace == SPACE_DeviceGray);

    /* In front end chains, we evaluate whether a color will affect black
     * preservation but use one chain for all blackType's. In back end chains we
     * have unique chains for each blackType which we already know the type of.
     */
    context.allowBlackPreservation =
            (colorInfo->constructionDepth[colorType] == 0) &&
            !context.fPaintingSoftMask &&
            !colorChain->fPresepColorIsGray;
    context.f100pcBlackRelevant =
            context.allowBlackPreservation &&
            !cc_getBlackIntercept(colorInfo->hcmsInfo, colorType);
    context.fBlackTintRelevant =
            context.allowBlackPreservation &&
            cc_getBlackTintIntercept(colorInfo->hcmsInfo, colorType) < 1.0f;
    context.fBlackTintLuminance = cc_getBlackTintLuminance(colorInfo->hcmsInfo);
    if (colorChain->fCompositing) {
      context.f100pcBlackRelevant =
              context.f100pcBlackRelevant &&
              (colorChain->inBlackType == BLACK_TYPE_100_PC ||
               colorChain->inBlackType == BLACK_TYPE_MODIFIED);
      context.fBlackTintRelevant =
              context.fBlackTintRelevant &&
              (colorChain->inBlackType == BLACK_TYPE_TINT ||
               colorChain->inBlackType == BLACK_TYPE_MODIFIED);
    }
  }
  else {
    /* Building a transform chain. Only device spaces are acceptable */
    fPresepToGray = FALSE ;
    fPresepColorIsGray = FALSE ;
    context.jobColorSpaceIsGray = -1 ;    /* An invalid value, will be trapped in transfer code */
    context.forceIccToDeviceN = FALSE;

    if (forceNamedIntereptionForTransforms)
      context.allowNamedColorIntercept = TRUE ;
  }

  /* Allow auto-separation when color converting chains in the front end - it is
     too late to add any spots, they should have all been added in the front-end
     chain. Also for soft masks - we want to derive the soft mask purely from
     the job, i.e. with no override data from the rip environment. */
  allowAutoSeparation = colorChain != NULL && !colorChain->fCompositing &&
                        !context.fPaintingSoftMask;

  context.pThisLink = pHeadLink ;

  /* Transfer functions are applied in the frontend and therefore we don't want
     to apply them again when doing recombine adjustment or compositing.
     Setting nTransferLinks to one means one set of transfers has already been
     applied. */
  context.nTransferLinks = gsc_transfersPreapplied(colorInfo) ? 1 : 0;

  /* For the transfer link, should JobNegative and ForcePositive be applied? */
  context.applyJobPolarity = ! guc_backdropRasterStyle(hRasterStyle);

  /* Prevent the use of the ChainCache for side chains constructed as part of
   * the main chain.
   */
  colorInfo->constructionDepth[colorType]++;
  if (colorInfo->constructionDepth[colorType] > MAX_CONSTRUCTION_DEPTH)
    return detail_error_handler(LIMITCHECK, "Color space recursion limit exceeded.");

#define return DO_NOT_RETURN__GO_TO_cleanup_instead!

  fPresepLink = FALSE ;

  do {
    /* Do a quick check on whether the color space is recursive */
#define MAX_CHAIN_ITERATIONS      (100)
    iterationsDone++;
    if (iterationsDone > MAX_CHAIN_ITERATIONS) {
      (void) detail_error_handler(LIMITCHECK, "Color space is recursive.");
      goto cleanup;
    }

    /* Normally, on the first loop, or the second loop for Indexed spaces (it's
     * easiest to deal with the base space), PCL CMY spaces (it's  easiest to
     * deal with RGB after inversion), and Patterns (we need to wait for a
     * possible base space), evaluate the blackType to determine if black should
     * be preserved for this color space and color values.
     */
    if (context.allowBlackPreservation &&
        !context.doneBlackEvaluate &&
        context.chainColorSpace != SPACE_Indexed &&
        context.chainColorSpace != SPACE_DeviceCMY &&
        context.chainColorSpace != SPACE_Pattern &&
        context.chainColorSpace != SPACE_PatternMask) {
      HQASSERT(!context.doneBlackRemove,
               "Inconsistent black preservation state");

      context.doneBlackEvaluate = TRUE;
      if (!cc_blackevaluate_create(context.PSColorSpace,
                                   context.chainColorSpace,
                                   context.colorspacedimension,
                                   context.f100pcBlackRelevant,
                                   context.fBlackTintRelevant,
                                   colorChain->chainColorModel,
                                   hRasterStyle,
                                   colorChain->fCompositing,
                                   colorChain->inBlackType,
                                   &colorInfo->params.excludedSeparations,
                                   &pNextLink,
                                   &chainContext->blackPosition))
        goto cleanup;
      if (pNextLink != NULL) {
        context.pThisLink = cc_addLink(context.pThisLink, pNextLink);
        context.blackevaluateLink = pNextLink;
      }
      else
        context.allowBlackPreservation = FALSE;
    }

    /* Process the current state, potentially adding a new link */
    switch ( context.chainColorSpace ) {

    case SPACE_CIEBasedDEFG:
    case SPACE_CIETableABCD:
    case SPACE_InterceptCMYK:
    case SPACE_CIEBasedABC:
    case SPACE_CalRGB:
    case SPACE_Lab:
    case SPACE_CIEBasedDEF:
    case SPACE_CIETableABC:
    case SPACE_InterceptRGB:
    case SPACE_CIEBasedA:
    case SPACE_CalGray:
    case SPACE_CIETableA:
    case SPACE_InterceptGray:
    case SPACE_ICCBased:
      saved_fIntercepting = (uint8) ((chainContext != NULL) && chainContext->fIntercepting);

      if (!createTransformInfoList(colorInfo, colorType, hRasterStyle, colorChain,
                                   fPresepToGray,
                                   &context))
        goto cleanup;

      if (context.transformList[0] != NULL) {
        /* Color management is subject to black preservation, we now know that
         * color management is being applied, so create the blackremove link.
         */
        if (context.allowBlackPreservation &&
            !context.doneBlackRemove) {
          USERVALUE blackTintThreshold;
          blackTintThreshold = cc_getBlackTintIntercept(colorInfo->hcmsInfo, colorType);

          context.doneBlackRemove = TRUE;
          if (!cc_blackremove_create(chainContext,
                                     context.PSColorSpace,
                                     context.chainColorSpace,
                                     context.colorspacedimension,
                                     context.f100pcBlackRelevant,
                                     context.fBlackTintRelevant,
                                     context.fBlackTintLuminance,
                                     blackTintThreshold,
                                     colorChain->chainColorModel,
                                     hRasterStyle,
                                     &colorInfo->params.excludedSeparations,
                                     &pNextLink)) {
            destroyTransformInfoList(&context);
            goto cleanup;
          }
          if (pNextLink != NULL)
            context.pThisLink = cc_addLink(context.pThisLink, pNextLink);
        }

        /* The color management link */
        pNextLink = cc_cmmxform_create(colorInfo,
                                       context.transformList,
                                       context.aimDeviceSpace,
                                       cc_getrenderingintent(colorInfo),
                                       cc_getAlternateCMM(colorInfo->hcmsInfo),
                                       cc_getwcsCMM(colorInfo->hcmsInfo),
                                       context.pColorantIndexArray,
                                       &context.chainColorSpace,
                                       &context.colorspacedimension,
                                       &context.PSColorSpace);
        if (pNextLink == NULL) {
          destroyTransformInfoList(&context);
          goto cleanup;
        }
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
      }
      else {
        HQASSERT(!ColorspaceIsCIEBased(context.chainColorSpace), "Infinite loop imminent");

        /* It turned out that we aren't intercepting after all, so restore the flag. */
        if (chainContext != NULL)
          chainContext->fIntercepting = saved_fIntercepting;
      }

      /* When recombining, a CIEBasedA will be converted to DeviceGray ready for
       * recombine interception - a transform won't have been created for that case.
       * Other cases will not be allowed to be recombine intercepted.
       */
      if (context.transformList[0] != NULL)
        fPresepToGray = FALSE ;
      fPresepColorIsGray = FALSE ;

      destroyTransformInfoList(&context);

      break;

    case SPACE_CIEXYZ:
    case SPACE_CIELab:
    case SPACE_ICCXYZ:
    case SPACE_ICCLab:
    case SPACE_HqnPCS:
    case SPACE_HqnProfile:
      HQFAIL("Shouldn't see XYZ/Lab here");
      goto cleanup;

    case SPACE_Pattern:
      /* Can be a colored (Type1 or Type2) pattern or an uncolored pattern.
       * If this is a colored pattern there is nothing more to do. If we
       * have an uncolored pattern we need to check that the base colorspace
       * is valid and the best way to do this is to construct the colorchain.
       */
      if ( ! cc_getpatternbasespace( colorInfo, context.PSColorSpace,
                                     &context.chainColorSpace,
                                     &context.colorspacedimension ) )
        HQFAIL( "Pattern base space changed unexpectedly" );

      HQASSERT( colorChain != NULL, "colorChain NULL" ) ;

      if ( context.chainColorSpace != SPACE_notset &&
           colorChain->patternPaintType == UNCOLOURED_PATTERN ) {
        /* we should continue to build the chain. This chain needs to be
         * constructed to check that the underlying space is valid.
         */
        context.PSColorSpace = &oArray(*context.PSColorSpace)[1] ;
      }
      else {
        context.PSColorSpace = NULL ;
        context.chainColorSpace = SPACE_Pattern;
        context.colorspacedimension = 0;

        context.finalDeviceSpace = TRUE ;
      }
      break;

    case SPACE_PatternMask:
      /* Contents of an uncoloured pattern. The base space is always
         required, the best way to check that the base colorspace is valid
         is to construct the color chain. */
      if ( ! cc_getpatternbasespace( colorInfo,
                                     context.PSColorSpace, &context.chainColorSpace,
                                     &context.colorspacedimension ) )
        HQFAIL( "Pattern base space changed unexpectedly" );

      HQASSERT(colorChain != NULL, "colorChain NULL") ;
      HQASSERT(context.chainColorSpace != SPACE_notset,
               "Base colourspace not set for pattern cell") ;

      /* we should continue to build the chain. This chain needs to be
       * constructed to check that the underlying space is valid.
       */
      context.PSColorSpace = &oArray(*context.PSColorSpace)[1] ;
      break;

    case SPACE_Indexed:
      pNextLink = cc_indexed_create( colorInfo,
                                     context.PSColorSpace,
                                     hRasterStyle,
                                     &context.PSColorSpace ) ;
      if (pNextLink == NULL)
        goto cleanup;
      context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

      if (!gsc_getcolorspacesizeandtype( colorInfo,
                                         context.PSColorSpace, &context.chainColorSpace,
                                         &context.colorspacedimension))
        goto cleanup;
      IndexedBasePSColorSpace = context.PSColorSpace;

      break;

    case SPACE_Separation:
    case SPACE_DeviceN:
      /* Conditions      a. Colorants match device colorants
       *                 b. Colorants don't all match device colorants
       * ColorLink       a. CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 b. CL_TYPEtinttransfer built from PSColorspace
       * Resulting Space a. ColorCache
       *                 b. From colorspace array
       * State changes   a. chaincolorspace, TransferCurvesApplied
       *                 b. chaincolorspace, PSColorspace
       */
      if ( fPresepColorIsGray &&
           rcbn_intercepting() &&
           ! rcbn_composite_page()) {
        HQASSERT( fPresepToGray , "fPresepColorIsGray is TRUE but fPresepToGray is FALSE" ) ;
        HQASSERT( context.colorspacedimension == 1 , "Should be single colorant space" ) ;

        /* I know the link's name is 'gray to k' but really it should just be called
         * 'flip a single colorant' or somesuch - actually what we're doing here is
         * K to gray.
         */
        pNextLink = cc_graytok_create() ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

        context.chainColorSpace = SPACE_DeviceGray ;
      }
      else {
        if (!doDeviceN(colorInfo,
                       hRasterStyle,
                       colorType,
                       &context,
                       colorChain,
                       allowAutoSeparation))
          goto cleanup;

        fPresepToGray = FALSE ;
        fPresepColorIsGray = FALSE ;
      }

      /* Allowable once, but just don't do it again. */
      allowAutoSeparation = FALSE ;
      break;

    case SPACE_FinalDeviceN:
      /* In this case we need only fill in the colorant index array with values
       * from 0 to nDeviceColorants-1 as device colorants are always in order.
       */
      for ( i = 0 ; i < nDeviceColorants ; i++ )
        context.pColorantIndexArray[ i ] = i ;
      context.finalDeviceSpace = TRUE ;
      context.chainColorSpace = SPACE_DeviceN ;
      break ;

    case SPACE_DeviceCMYK:
      /* Conditions      a. interceptcolorspacecmyk == NULL or allowintercept(F), REAL_DEVICE_CMYK
       *                 b. interceptcolorspacecmyk == NULL or allowintercept(F), REAL_DEVICE_RGB
       *                 c. interceptcolorspacecmyk == NULL or allowintercept(F), REAL_DEVICE_Gray
       *                 d. interceptcolorspacecmyk == NULL or allowintercept(F), REAL_DEVICE_N
       *                 e. interceptcolorspacecmyk not NULL, allowintercept(T)
       *                 f. interceptcolorspacecmyk == NULL, allowintercept(F)
       * ColorLinks      a. CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 b. CL_TYPEcmyktorgb, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 c. CL_TYPEcmyktogray, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 d. CL_TYPEcmykton, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 e. CL_TYPEtransfer
       *                 f. None
       * Resulting Space a. ColorCache
       *                 b. ColorCache
       *                 c. ColorCache
       *                 d. ColorCache
       *                 e. SPACE_InterceptCMYK
       * State changes   a. chaincolorspace
       *                 b. chaincolorspace
       *                 c. chaincolorspace
       *                 d. chaincolorspace
       *                 e. chaincolorspace
       *                 f. chaincolorspace, aimDeviceSpace
       */

      if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                             context.pColorantIndexArray, 4))
        goto cleanup;
      context.PSColorSpace = NULL ;

      if ( fPresepColorIsGray &&
           rcbn_intercepting() &&
           ! rcbn_composite_page()) {
        HQASSERT(colorType == GSC_FILL || colorType == GSC_STROKE,
                 "fPresepColorIsGray expected a FILL or STROKE colorType");
        HQASSERT(fPresepToGray, "fPresepColorIsGray is TRUE but fPresepToGray is FALSE");

        /* Can convert to gray before doing transfer functions, since more efficient
         * and also gives same result.
         */
        pNextLink = cc_cmyktogray_create(colorChain->fCompositing) ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

        context.chainColorSpace = SPACE_DeviceGray ;
        context.colorspacedimension = 1 ;
      }
      else {
        fPresepToGray = FALSE ;
        fPresepColorIsGray = FALSE ;

        if (context.allowIntercept &&
            (cc_getIntercept(colorInfo, colorType,
                             SPACE_DeviceCMYK,
                             context.fColorManage,
                             colorChain->fCompositing,
                             cc_incompleteChainIsSimple(colorChain)) != NULL)) {
          pNextLink = NULL ;

          context.chainColorSpace = SPACE_InterceptCMYK ;
        }
        else if (context.fSoftMaskLuminosityChain) {
          context.finalDeviceSpace = TRUE ;
          pNextLink = NULL ;
        }
        else {
          switch ( context.aimDeviceSpace ) {

          case DEVICESPACE_CMYK:
            context.finalDeviceSpace = TRUE ;
            pNextLink = NULL ;
            break ;

          case DEVICESPACE_RGBK:
            pNextLink = cc_cmyktorgbk_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceRGBK ;
            context.colorspacedimension = 4 ;
            break ;

          case DEVICESPACE_RGB:
            pNextLink = cc_cmyktorgb_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceRGB ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_CMY:
            pNextLink = cc_cmyktocmy_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceCMY ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_Lab:
            pNextLink = cc_cmyktolab_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_Lab ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_Gray:
            if (colorChain != NULL)
              pNextLink = cc_cmyktogray_create(colorChain->fCompositing) ;
            else
              /* Don't do separation detection for transform chains */
              pNextLink = cc_cmyktogray_create(TRUE) ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceGray ;
            context.colorspacedimension = 1 ;
            break ;

          case DEVICESPACE_N:
            pNextLink = NULL ;
            if (context.nTransferLinks == 0) {
              /* Consider custom transforms to be an intercept and pre-apply
               * transfer functions
               */
              context.nTransferLinks++;
              pNextLink =
                cc_transfer_create(context.colorspacedimension,
                                   context.pColorantIndexArray,
                                   context.chainColorSpace,
                                   gsc_getRequiredReproType(colorInfo,
                                                            colorType),
                                   context.jobColorSpaceIsGray,
                                   TRUE,      /* isFirstTransferLink */
                                   FALSE,     /* isLastTransferLink */
                                   context.applyJobPolarity,
                                   colorInfo->transferInfo,
                                   colorInfo->halftoneInfo,
                                   hRasterStyle,
                                   context.colorPageParams->forcePositive,
                                   &context.page->colorPageParams);
              if (pNextLink == NULL)
                goto cleanup;
              context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            }

            guc_CustomConversion ( hRasterStyle, DEVICESPACE_CMYK, &customProcedure ) ;
            pNextLink = cc_cmykton_create(customProcedure, nDeviceColorants) ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

            context.chainColorSpace = SPACE_FinalDeviceN ;
            context.colorspacedimension = nDeviceColorants ;
            /* PSColorSpace has to be manufactured for HDLT */
            context.PSColorSpace = &manufacturedPSColorSpace;
            if (!createFinalDeviceNColorSpace(chainContext, hRasterStyle,
                                              &context.pColorantIndexArray,
                                              &context.colorantIndexArraySize,
                                              nDeviceColorants,
                                              fNonInterceptingChain,
                                              context.PSColorSpace))
              goto cleanup;
            break ;

          default:
            /* must be an error */
            pNextLink = NULL ;
            HQFAIL("Unknown device colorspace in cc_constructChain");
            goto cleanup;
          }
        }
      }

      context.allowIntercept = FALSE;
      break;

    case SPACE_DeviceRGB:
      /* Conditions      a. interceptcolorspacergb == NULL or allowintercept(F), RealDeviceRGB
       *                 b. interceptcolorspacergb == NULL or allowintercept(F), RealDeviceCMYK
       *                 c. interceptcolorspacergb == NULL or allowintercept(F), RealDeviceGray
       *                 d. interceptcolorspacergb == NULL or allowintercept(F), RealDeviceN
       *                 e. interceptcolorspacergb is not NULL, allowintercept(T)
       * ColorLink       a. CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 b. CL_TYPErgbtocmyk, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 c. CL_TYPErgbtogray, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 d. CL_TYPErgbton, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 e. CL_TYPEtransfer
       * Resulting Space a. ColorCache
       *                 b. ColorCache
       *                 c. ColorCache
       *                 d. ColorCache
       *                 e. InterceptRGB
       * State changes   a. chaincolorspace
       *                 b. chaincolorspace
       *                 c. chaincolorspace
       *                 d. chaincolorspace
       *                 e. chaincolorspace
       */
      if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGB,
                                             context.pColorantIndexArray, 3))
        goto cleanup;
      context.PSColorSpace = NULL ;

      if ( fPresepColorIsGray &&
           rcbn_intercepting() &&
           ! rcbn_composite_page()) {
        HQASSERT(colorType == GSC_FILL || colorType == GSC_STROKE,
                 "fPresepColorIsGray expected a FILL or STROKE colorType");
        HQASSERT(fPresepToGray, "fPresepColorIsGray is TRUE but fPresepToGray is FALSE");

        /* Can convert to gray before doing transfer functions, since more efficient
         * and also gives same result.
         */
        pNextLink = cc_rgbtogray_create(colorChain->fCompositing) ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

        context.chainColorSpace = SPACE_DeviceGray ;
        context.colorspacedimension = 1 ;
      }
      else {
        fPresepToGray = FALSE ;
        fPresepColorIsGray = FALSE ;

        if (context.allowIntercept &&
            (cc_getIntercept(colorInfo, colorType,
                             SPACE_DeviceRGB,
                             context.fColorManage,
                             colorChain->fCompositing,
                             cc_incompleteChainIsSimple(colorChain)) != NULL)) {
          pNextLink = NULL ;

          context.chainColorSpace = SPACE_InterceptRGB ;
        }
        else if (context.fSoftMaskLuminosityChain) {
          context.finalDeviceSpace = TRUE ;
          pNextLink = NULL ;
        }
        else {
          Bool preserveBlack;

          switch ( context.aimDeviceSpace ) {

          case DEVICESPACE_CMYK:
            /* Conversion of RGB black to CMYK black is subject to black
             * preservation.
             */
            if (context.allowBlackPreservation &&
                !context.doneBlackRemove) {
              USERVALUE blackTintThreshold;
              blackTintThreshold = cc_getBlackTintIntercept(colorInfo->hcmsInfo, colorType);

              context.doneBlackRemove = TRUE;
              if (!cc_blackremove_create(chainContext,
                                         context.PSColorSpace,
                                         context.chainColorSpace,
                                         context.colorspacedimension,
                                         context.f100pcBlackRelevant,
                                         context.fBlackTintRelevant,
                                         context.fBlackTintLuminance,
                                         blackTintThreshold,
                                         colorChain->chainColorModel,
                                         hRasterStyle,
                                         &colorInfo->params.excludedSeparations,
                                         &pNextLink))
                goto cleanup;
              if (pNextLink != NULL)
                context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            }

            /* The RGB to CMYK conversion currently requires additional black
             * preservation methods.
             * ConvertRGBBlack is a legacy black preservation method that will
             * be deprecated in due course.
             * We also need to convert RGB black to CMYK black to get correct
             * overprinting for RGB objects when OverprintBlack is on and black
             * preservation isn't on.
             */
            preserveBlack = ((colorType == GSC_FILL || colorType == GSC_STROKE) &&
                             cc_getConvertRGBBlack(colorInfo->hcmsInfo, colorType)) ||
                            (!context.f100pcBlackRelevant &&
                             colorChain != NULL &&
                             colorChain->chainColorModel == REPRO_COLOR_MODEL_RGB &&
                             cc_isoverprintblackpossible(colorInfo, colorType,
                                                         colorChain->fCompositing));

            pNextLink = cc_rgbtocmyk_create(colorInfo->rgbtocmykInfo,
                                            preserveBlack,
                                            context.colorPageParams);
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceCMYK ;
            context.colorspacedimension = 4 ;
            break ;

          case DEVICESPACE_RGBK:
            pNextLink = cc_rgbtorgbk_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceRGBK ;
            context.colorspacedimension = 4 ;
            break ;

          case DEVICESPACE_RGB:
            context.finalDeviceSpace = TRUE ;
            pNextLink = NULL ;
            break ;

          case DEVICESPACE_CMY:
            pNextLink = cc_rgbtocmy_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceCMY ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_Lab:
            pNextLink = cc_rgbtolab_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_Lab ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_Gray:
            if (colorChain != NULL)
              pNextLink = cc_rgbtogray_create(colorChain->fCompositing) ;
            else
              /* Don't do separation detection for transform chains */
              pNextLink = cc_rgbtogray_create(TRUE) ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceGray ;
            context.colorspacedimension = 1 ;
            break ;

          case DEVICESPACE_N:
            pNextLink = NULL ;
            if (context.nTransferLinks == 0) {
              /* Consider custom transforms to be an intercept and pre-apply
               * transfer functions
               */
              context.nTransferLinks++;
              pNextLink =
                cc_transfer_create(context.colorspacedimension,
                                   context.pColorantIndexArray,
                                   context.chainColorSpace,
                                   gsc_getRequiredReproType(colorInfo,
                                                            colorType),
                                   context.jobColorSpaceIsGray,
                                   TRUE,      /* isFirstTransferLink */
                                   FALSE,     /* isLastTransferLink */
                                   context.applyJobPolarity,
                                   colorInfo->transferInfo,
                                   colorInfo->halftoneInfo,
                                   hRasterStyle,
                                   context.colorPageParams->forcePositive,
                                   &context.page->colorPageParams) ;
              if (pNextLink == NULL)
                goto cleanup;
              context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            }

            guc_CustomConversion ( hRasterStyle, DEVICESPACE_RGB, &customProcedure ) ;
            pNextLink = cc_rgbton_create(customProcedure, nDeviceColorants) ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

            context.chainColorSpace = SPACE_FinalDeviceN ;
            context.colorspacedimension = nDeviceColorants ;
            /* PSColorSpace has to be manufactured for HDLT */
            context.PSColorSpace = &manufacturedPSColorSpace;
            if (!createFinalDeviceNColorSpace(chainContext, hRasterStyle,
                                              &context.pColorantIndexArray,
                                              &context.colorantIndexArraySize,
                                              nDeviceColorants,
                                              fNonInterceptingChain,
                                              context.PSColorSpace))
              goto cleanup;
            break ;

          default:
            /* must be an error */
            pNextLink = NULL ;
            HQFAIL("Unknown device colorspace in cc_constructChain");
            goto cleanup;
          }
        }
      }

      context.allowIntercept = FALSE;
      break;

    case SPACE_DeviceGray:
      /* Conditions      a. DeviceGray, InterceptGray == NULL or allowintercept(F), RealDeviceGray
       *                 b. DeviceGray, InterceptGray == NULL or allowintercept(F), RealDeviceCMYK
       *                 c. DeviceGray, InterceptGray == NULL or allowintercept(F), RealDeviceRGB
       *                 d. DeviceGray, InterceptGray == NULL or allowintercept(F), RealDeviceN
       *                 e. DeviceGray, InterceptGray not null, allowintercept(T)
       * ColorLink       a. CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 b. CL_TYPEgraytocmyk, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 c. CL_TYPEgraytorgb, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 d. CL_TYPEgrayton, CL_TYPEtransfer, CL_TYPEcalibration, CL_TYPEhalftone
       *                 e. CL_TYPEtransfer
       * Resulting Space a. ColorCache
       *                 b. ColorCache
       *                 c. ColorCache
       *                 d. ColorCache
       *                 e. InterceptGray
       * State Changes   a. chaincolorspace
       *                 b. chaincolorspace
       *                 c. chaincolorspace
       *                 d. chaincolorspace
       *                 e. chaincolorspace
       */
      if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_Gray,
                                             context.pColorantIndexArray, 1))
        goto cleanup;
      context.PSColorSpace = NULL ;

      if ( colorChain != NULL &&
           colorChain->iColorSpace != SPACE_Pattern &&
           fTreatScreenAsPattern(colorChain->iColorSpace,
                                 colorType,
                                 colorInfo->deviceRS,
                                 gsc_getSpotno(colorInfo))) {
        fPresepToGray = FALSE ;
        fPresepColorIsGray = FALSE ;

        /* In the rather special case of patternscreens being rendered to, eg. contone
         * devices we send the pattern through an All separation which has to be set
         * up here, viz:
         *        [/Separation /All /DeviceGray {}]
         * we will also ignore transfers, calibration per colorant by the logic of
         * updateHTCacheForPatternContone using iColorValues and not transformed values.
         * Because DeviceN is inverse to DeviceGray we have to flip into DeviceK first.
         * NB. Using stack variables (patternScreenColorSpace, patternScreenArray)
         *     to hold the colorspace objects is safe because these are not needed
         *     outside chaing generation. In particular, not referenced by the chain.
         */
        pNextLink = cc_graytok_create() ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

        context.chainColorSpace = SPACE_DeviceK ;
        context.PSColorSpace = &patternScreenColorSpace;

        theTags(*context.PSColorSpace) = OARRAY | LITERAL;
        theLen(*context.PSColorSpace) = 4;
        oArray(*context.PSColorSpace) = patternScreenArray;

        theTags(patternScreenArray[0]) = ONAME | LITERAL;
        oName(patternScreenArray[0]) = system_names + NAME_Separation;

        theTags(patternScreenArray[1]) = ONAME | LITERAL;
        oName(patternScreenArray[1]) = system_names + NAME_All;

        theTags(patternScreenArray[2]) = ONAME | LITERAL;
        oName(patternScreenArray[2]) = system_names + NAME_DeviceGray;

        theTags(patternScreenArray[3]) = OARRAY | CANEXEC | EXECUTABLE;
        theLen(patternScreenArray[3]) = 0;
        oString(patternScreenArray[3]) = NULL;

        context.chainColorSpace = SPACE_Separation;
      }
      else if ( fPresepToGray &&
                rcbn_intercepting() && ! rcbn_composite_page()) {
        fPresepToGray = FALSE ;

        /* We are intercepting for recombine and have not converted to gray from presep already */

        HQASSERT(fPresepColorIsGray ||
                 /* Hack for Indexed because fPresepColorIsGray isn't set for
                  * Indexed DeviceGray. Fixing that is more effort than it's worth */
                 IndexedBasePSColorSpace != NULL,
                 "fPresepColorIsGray should be true");
        HQASSERT(context.nTransferLinks == 0, "Unexpected value for nTransferLinks in recombine");
        context.nTransferLinks++;
        pNextLink = cc_transfer_create(context.colorspacedimension,
                                       context.pColorantIndexArray,
                                       context.chainColorSpace,
                                       gsc_getRequiredReproType(colorInfo,
                                                                colorType),
                                       context.jobColorSpaceIsGray,
                                       TRUE,      /* isFirstTransferLink */
                                       FALSE,     /* isLastTransferLink */
                                       context.applyJobPolarity,
                                       colorInfo->transferInfo,
                                       colorInfo->halftoneInfo,
                                       hRasterStyle,
                                       context.colorPageParams->forcePositive,
                                       &context.page->colorPageParams) ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

        /* Flag that we want the recombine chain link on the end */
        fPresepLink = TRUE;

        context.finalDeviceSpace = TRUE;
      }
      else {
        fPresepToGray = FALSE ;
        fPresepColorIsGray = FALSE ;

        /* Gray intercepts are a bit trickier than the others. If a gray intercept
         * exists we use it. But if it's null the behaviour depends on the aim
         * space. If the aim space is:
         * - gray, we don't intercept.
         * - rgb, we use the rgb intercept, but first we go around the loop again.
         * - cmyk, or anything else, use the cmyk intercept.
         */
        if (context.allowIntercept &&
            cc_getIntercept(colorInfo, colorType,
                            SPACE_DeviceGray,
                            context.fColorManage,
                            colorChain->fCompositing,
                            cc_incompleteChainIsSimple(colorChain)) != NULL) {
          pNextLink = NULL ;

          context.chainColorSpace = SPACE_InterceptGray ;
        }
        else if (context.allowIntercept &&
                 cc_getIntercept(colorInfo, colorType,
                                 SPACE_DeviceCMYK,
                                 context.fColorManage,
                                 colorChain->fCompositing,
                                 cc_incompleteChainIsSimple(colorChain)) != NULL) {
          /* Allow gray to use the cmyk interception as the next choice.
           */
          pNextLink = cc_graytocmyk_create() ;
          if (pNextLink == NULL)
            goto cleanup;
          context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

          context.chainColorSpace = SPACE_DeviceCMYK ;
          context.colorspacedimension = 4 ;
          deferredGrayIntercept = TRUE;
        }
        else if (context.fSoftMaskLuminosityChain) {
          context.finalDeviceSpace = TRUE ;
          pNextLink = NULL ;
        }
        else {
          switch ( context.aimDeviceSpace ) {

          case DEVICESPACE_CMYK:
            /* Use the gray to k chain for images to avoid bloating the image
             * store and big slowdowns. Convert non-image objects to cmyk because
             * they potentially require a knock-out in cmy with OverprintGray OR
             * OverprintProcess set to FALSE. Also convert to cmyk when we don't
             * have a bona fide color chain (as in a cmyk transform branch).
             */
            if (colorChain != NULL &&
                (colorType == GSC_IMAGE || colorType == GSC_BACKDROP) &&
                 gsc_getoverprintgrayimages(colorInfo)) {
              pNextLink = cc_graytok_create() ;
              context.chainColorSpace = SPACE_DeviceK ;
              context.colorspacedimension = 1 ;
            }
            else {
              pNextLink = cc_graytocmyk_create() ;
              context.chainColorSpace = SPACE_DeviceCMYK ;
              context.colorspacedimension = 4 ;
            }
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            break ;

          case DEVICESPACE_RGBK:
            /* Gray is in the same sense as the K in RGBK, so go straight to
             * output, but first hack the one colorant index to K.
             */
            if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGBK,
                                                   context.pColorantIndexArray, 4))
            context.pColorantIndexArray[0] = context.pColorantIndexArray[3];
            context.chainColorSpace = SPACE_DeviceK ;
            context.colorspacedimension = 1 ;
            break ;

          case DEVICESPACE_RGB:
            pNextLink = cc_graytorgb_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceRGB ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_CMY:
            pNextLink = cc_graytocmy_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_DeviceCMY ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_Lab:
            pNextLink = cc_graytolab_create() ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            context.chainColorSpace = SPACE_Lab ;
            context.colorspacedimension = 3 ;
            break ;

          case DEVICESPACE_Gray:
            context.finalDeviceSpace = TRUE ;
            pNextLink = NULL ;
            break ;

          case DEVICESPACE_N:
            pNextLink = NULL ;
            if (context.nTransferLinks == 0) {
              /* Consider custom transforms to be an intercept and pre-apply
               * transfer functions
               */
              context.nTransferLinks++;
              pNextLink =
                cc_transfer_create(context.colorspacedimension,
                                   context.pColorantIndexArray,
                                   context.chainColorSpace,
                                   gsc_getRequiredReproType(colorInfo,
                                                            colorType),
                                   context.jobColorSpaceIsGray,
                                   TRUE,      /* isFirstTransferLink */
                                   FALSE,     /* isLastTransferLink */
                                   context.applyJobPolarity,
                                   colorInfo->transferInfo,
                                   colorInfo->halftoneInfo,
                                   hRasterStyle,
                                   context.colorPageParams->forcePositive,
                                   &context.page->colorPageParams) ;
              if (pNextLink == NULL)
                goto cleanup;
              context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
            }

            guc_CustomConversion ( hRasterStyle, DEVICESPACE_Gray, &customProcedure ) ;
            pNextLink = cc_grayton_create(customProcedure, nDeviceColorants) ;
            if (pNextLink == NULL)
              goto cleanup;
            context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;

            context.chainColorSpace = SPACE_FinalDeviceN ;
            context.colorspacedimension = nDeviceColorants ;
            /* PSColorSpace has to be manufactured for HDLT */
            context.PSColorSpace = &manufacturedPSColorSpace;
            if (!createFinalDeviceNColorSpace(chainContext, hRasterStyle,
                                              &context.pColorantIndexArray,
                                              &context.colorantIndexArraySize,
                                              nDeviceColorants,
                                              fNonInterceptingChain,
                                              context.PSColorSpace))
              goto cleanup;
            break ;

          default:
            /* must be an error */
            pNextLink = NULL ;
            HQFAIL("Unknown device colorspace in cc_constructChain");
            goto cleanup;
          }
        }
      }

      if (!deferredGrayIntercept)
        context.allowIntercept = FALSE;
      break;

    case SPACE_DeviceRGBK:
      HQASSERT( context.aimDeviceSpace == DEVICESPACE_RGBK, "Unsupported colorspace") ;
      if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_RGBK,
                                             context.pColorantIndexArray, 4))
        goto cleanup;
      context.finalDeviceSpace = TRUE ;
      context.PSColorSpace = NULL ;
      break;

    case SPACE_DeviceCMY:
      if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMY,
                                             context.pColorantIndexArray, 3))
        goto cleanup;
      context.PSColorSpace = NULL ;
      fPresepToGray = FALSE ;
      fPresepColorIsGray = FALSE ;

      switch ( context.aimDeviceSpace ) {
      case DEVICESPACE_CMY:
        context.finalDeviceSpace = TRUE ;
        pNextLink = NULL ;
        break ;

      default:
        /* PCL can have a DeviceCMY input space, which we invert to RGB */
        pNextLink = cc_cmytorgb_create() ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
        context.chainColorSpace = SPACE_DeviceRGB ;
        context.colorspacedimension = 3 ;
        break ;
     }
     break;

    case SPACE_DeviceK:
      context.pColorantIndexArray[ 0 ] =
        guc_colorantIndexReserved( hRasterStyle , system_names + NAME_Black ) ;

      switch (context.aimDeviceSpace) {
      case DEVICESPACE_CMYK:
        break;
      case DEVICESPACE_RGBK:
        /* Coerse the graytok link to mean 'ktogray', because K in RGBK is additive */
        pNextLink = cc_graytok_create() ;
        if (pNextLink == NULL)
          goto cleanup;
        context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
        break;
      default:
        /* must be an error */
        pNextLink = NULL ;
        HQFAIL("Unknown device colorspace in cc_constructChain");
        goto cleanup;
      }
      context.finalDeviceSpace = TRUE;
      context.PSColorSpace = NULL ;
      pNextLink = NULL;
      break;

    default:
      /* an unknown colorspace must be an error */
      HQFAIL("Unsupported colorspace in cc_constructChain");
      goto cleanup;
    } /* switch (context.chainColorSpace) */

    if ( context.colorantIndexArraySize < context.colorspacedimension ) {
      if ( context.colorantIndexArraySize > STACK_EXTCIA_SIZE )
        mm_free( mm_pool_color,
                 context.pColorantIndexArray,
                 context.colorantIndexArraySize * sizeof(COLORANTINDEX) );
      context.colorantIndexArraySize = context.colorspacedimension ;
      context.pColorantIndexArray = mm_alloc(mm_pool_color,
                                             context.colorantIndexArraySize * sizeof(COLORANTINDEX),
                                             MM_ALLOC_CLASS_NCOLOR );
      if ( context.pColorantIndexArray == NULL ) {
        ( void ) error_handler(VMERROR);
        goto cleanup;
      }
    }

    HQASSERT(context.pThisLink != NULL, "context.pThisLink NULL");
  } while ( ! context.finalDeviceSpace ) ;

  HQASSERT(context.currentInfo == NULL, "currentInfo.u.shared should be NULL by now") ;


  /* Immediately before the end of the chain, we can replace the black that was
   * removed earlier as part of black preservation.
   */
  if (context.allowBlackPreservation &&
      context.doneBlackRemove) {
    if (!cc_blackreplace_create(context.colorspacedimension,
                                context.pColorantIndexArray,
                                context.chainColorSpace,
                                context.f100pcBlackRelevant,
                                context.fBlackTintRelevant,
                                hRasterStyle,
                                &pNextLink)) {
      destroyTransformInfoList(&context);
      goto cleanup;
    }
    if (pNextLink != NULL)
      context.pThisLink = cc_addLink( context.pThisLink, pNextLink ) ;
  }


  if (context.fSoftMaskLuminosityChain) {
    if (!cc_constructLuminosityChain(&context))
      goto cleanup;
  }
  else if ( fPresepLink ) {
    HQASSERT(! fNonInterceptingChain,
              "non-intercepting chains not implemented yet for preseparation");
    if (!cc_constructPreseparationChain(colorInfo,
                                        colorType,
                                        &context,
                                        colorChain,
                                        pHeadLink,
                                        IndexedBasePSColorSpace))
      goto cleanup;
  }
  else if (! fNonInterceptingChain) {
    DEVICECODE_TYPE deviceCodeType;

    if (context.chainColorSpace == SPACE_Pattern) {
      deviceCodeType = DC_TYPE_halftone_only;
    }
    else if (colorChain != NULL && colorChain->fCompositing) {
      if (guc_backdropRasterStyle(hRasterStyle)) {
        /* Converting from a blend space to a blend space,
           no transfers or calibration to be applied */
        deviceCodeType = DC_TYPE_none;
        context.nTransferLinks = 0;
      } else {
        /* Converting from final blend space to device space */
        deviceCodeType = DC_TYPE_calibration_only;
        context.nTransferLinks = 0;
      }
    } else {
      if (guc_backdropRasterStyle(hRasterStyle))
        /* Interpreting an object and converting to current
           blend space - apply transfers but not calibration */
        deviceCodeType = DC_TYPE_transfer_only;
      else
        /* Normal, non-transparency case - apply transfer and calibration */
        deviceCodeType = DC_TYPE_normal;
    }

    if (!cc_constructDeviceChain(colorInfo,
                                 hRasterStyle,
                                 colorType,
                                 colorChain,
                                 pHeadLink,
                                 &context,
                                 IndexedBasePSColorSpace,
                                 deviceCodeType)) {
      goto cleanup;
    }
  } else {
    /* a non-intercepting device chain differs from an ordinary one in that the link
       causes a merge of the final device color with the current color, accumulating
       into it information about what would have overprinted had the chain been
       non-intercepting */
    chainContext->fApplyMaxBlts = TRUE;

    if (!cc_constructNonInterceptChain(colorInfo,
                                       hRasterStyle,
                                       colorType,
                                       colorChain,
                                       pHeadLink,
                                       &context)) {
      goto cleanup;
    }
  }

  result = TRUE;

cleanup:

  if ( !result ) {
    if ( *pHeadLink != NULL ) {
      cc_destroyLinks( *pHeadLink );
      *pHeadLink = NULL ;
    }
  }

  if ( context.pColorantIndexArray != NULL &&
       context.colorantIndexArraySize > STACK_EXTCIA_SIZE )
    mm_free( mm_pool_color,
             context.pColorantIndexArray,
             context.colorantIndexArraySize * sizeof(COLORANTINDEX) );

  /* Reenable the ChainCache */
  colorInfo->constructionDepth[colorType]--;

  if (*pHeadLink != NULL)
    cc_metrics_increment_constructs();

#undef return

  return result;
}


static Bool cc_constructLuminosityChain(GS_CONSTRUCT_CONTEXT  *context)
{
  CLINK *pNextLink ;

  pNextLink = cc_luminosity_create(context);
  if (pNextLink == NULL)
    return FALSE;
  (void) cc_addLink(context->pThisLink, pNextLink);

  return TRUE;
}

static Bool cc_constructShfillBaseChain(GS_COLORinfo *colorInfo,
                                        int32 colorType,
                                        COLORSPACE_ID chaincolorspace,
                                        int32 colorspacedimension,
                                        OBJECT *IndexedBasePSColorSpace)
{
  /* If we set up an indexed space for shaded fills we need to set up a chain
   * whose colorspace is the base space as interpolation of color values is
   * done in the base space.
   */
  if ( colorType == GSC_SHFILL ) {
    OBJECT theo = OBJECT_NOTVM_NOTHING ;

    if ( IndexedBasePSColorSpace != NULL ) {
      if ( ! gsc_getcolorspacesizeandtype( colorInfo,
                                           IndexedBasePSColorSpace,
                                           &chaincolorspace,
                                           &colorspacedimension ))
        return FALSE;
    }
    else {
      chaincolorspace = SPACE_notset;
      colorspacedimension = 0;
      theTags(theo) = ONULL;
      IndexedBasePSColorSpace = &theo;
    }

    if ( ! cc_updateChainForNewColorSpace( colorInfo,
                                           GSC_SHFILL_INDEXED_BASE,
                                           chaincolorspace,
                                           colorspacedimension,
                                           IndexedBasePSColorSpace,
                                           CHAINSTYLE_COMPLETE,
                                           FALSE /* fCompositing */))
      return FALSE;
  }

  return TRUE;
}

/* Apply the final preseparation transforms if the chain is valid
 * at this point.
 */
static Bool cc_constructPreseparationChain(GS_COLORinfo         *colorInfo,
                                           int32                colorType,
                                           GS_CONSTRUCT_CONTEXT *context,
                                           GS_CHAINinfo         *colorChain,
                                           CLINK                **pHeadLink,
                                           OBJECT               *IndexedBasePSColorSpace)
{
  CLINK *pNextLink ;
  COLORSPACE_ID chaincolorspace = SPACE_DeviceGray;
  int32 colorspacedimension = 1;

  pNextLink = cc_preseparation_create(colorInfo, colorType, context,
                                      colorChain, *pHeadLink );
  if (pNextLink == NULL)
    return FALSE;
  (void) cc_addLink(context->pThisLink, pNextLink);

  if (!cc_constructShfillBaseChain(colorInfo, colorType,
                                   chaincolorspace, colorspacedimension,
                                   IndexedBasePSColorSpace))
    return FALSE;

  return TRUE;
}

/* Apply the final device space transforms (transfer, calib and halftone)
 * if the chain is valid at this point. Create just the halftone transform
 * if halftoneOnly is TRUE - this will be the case for chains used to
 * insert traps returned by the trapper.
 * BEWARE: Before modifying the following code, see the comment in
 * gsc_hasIndependentChannels.
 */
static Bool cc_constructDeviceChain(GS_COLORinfo          *colorInfo,
                                    GUCR_RASTERSTYLE      *hRasterStyle,
                                    int32                 colorType,
                                    GS_CHAINinfo          *colorChain,
                                    CLINK                 **pHeadLink,
                                    GS_CONSTRUCT_CONTEXT  *context,
                                    OBJECT                *IndexedBasePSColorSpace,
                                    DEVICECODE_TYPE       deviceCodeType)
{
  CLINK *pNextLink ;
  GS_CHAIN_CONTEXT *chainContext = NULL;

  if (colorChain != NULL)
    chainContext = colorChain->context;

  if (colorChain != NULL &&
      colorChain->chainStyle == CHAINSTYLE_COMPLETE) {

    HQASSERT(chainContext != NULL, "chainContex NULL");

    HQASSERT(context->nTransferLinks == 0 ||
             context->nTransferLinks == 1 , "Should only add 1 pre-transfer at most");
    context->nTransferLinks++;

    pNextLink = cc_devicecode_create(colorInfo,
                                     hRasterStyle,
                                     colorType,
                                     context,
                                     colorChain->iColorSpace,
                                     chainContext->fIntercepting,
                                     colorChain->fCompositing,
                                     *pHeadLink,
                                     deviceCodeType,
                                     colorChain->patternPaintType,
                                     colorChain->chainColorModel,
                                     chainContext->blackPosition,
                                     &chainContext->illegalTintTransform,
                                     &chainContext->fApplyMaxBlts);
  }
  else {
    HQASSERT(colorChain == NULL ||
             colorChain->chainStyle == CHAINSTYLE_DUMMY_FINAL_LINK,
             "Inconsistent chainStyle");

    /* Create a dummy link that will be invoked under some circumstances, such as
     * at the end of a side chain when overriding an ICCBased space, but which
     * often doesn't get invoked.
     */
    pNextLink = cc_common_create(context->colorspacedimension,
                                 context->pColorantIndexArray,
                                 context->chainColorSpace, context->chainColorSpace,
                                 CL_TYPEdummyfinallink, 0, &dummyFunctions, 0);
  }
  if (pNextLink == NULL)
    return FALSE;
  (void) cc_addLink(context->pThisLink, pNextLink);

  if (!cc_constructShfillBaseChain(colorInfo, colorType,
                                   context->chainColorSpace,
                                   context->colorspacedimension,
                                   IndexedBasePSColorSpace))
    return FALSE;

  return TRUE;
}

/* A simple pass through invoke routine for the terminating link of side chains
 * which often doesn't get invoked, but when it does it doesn't alter values.
 */
static Bool dummyInvokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i;
  cc_commonAssertions(pLink, CL_TYPEnotset, 0, &dummyFunctions);

  for (i = 0; i < pLink->n_iColorants; i++)
    oColorValues[i] = pLink->iColorValues[i];
  return TRUE;
}

static Bool cc_constructNonInterceptChain(GS_COLORinfo          *colorInfo,
                                          GUCR_RASTERSTYLE      *hRasterStyle,
                                          int32                 colorType,
                                          GS_CHAINinfo          *colorChain,
                                          CLINK                 **pHeadLink,
                                          GS_CONSTRUCT_CONTEXT  *context)
{
  CLINK *pNextLink;
  GS_CHAIN_CONTEXT *chainContext;
  CLINK *devicecodeLink;

  chainContext = colorChain->context;
  HQASSERT( chainContext != NULL, "chainContext NULL" );

  /* Find the devicecode link of the main chain */
  HQASSERT(chainContext->pnext != NULL, "Expected a color chain");
  devicecodeLink = chainContext->pnext;
  while ( devicecodeLink->pnext != NULL )
    devicecodeLink = devicecodeLink->pnext;
  HQASSERT(devicecodeLink->linkType == CL_TYPEdevicecode,
           "Expected a devicecode link");

  pNextLink = cc_nonintercept_create( colorInfo,
                                      hRasterStyle,
                                      colorType,
                                      context,
                                      colorChain->iColorSpace,
                                      colorInfo->devicecodeInfo,
                                      *pHeadLink,
                                      devicecodeLink,
                                      colorChain->chainColorModel,
                                      chainContext->blackPosition);
  if (pNextLink == NULL)
    return FALSE;
  (void) cc_addLink(context->pThisLink, pNextLink);

  return TRUE;
}

/* Helper function for gsc_constructChain(). Should not be called directly.
 * Builds the normal color chain into the gstate based on the head link
 * defined by the parameters.
 */
static Bool doConstructChain( GS_COLORinfo *colorInfo,
                              int32 colorType )
{
  COLORSPACE_ID calibrationColorSpace ;
  COLORSPACE_ID chaincolorspace ;
  GS_CHAINinfo *colorChain;
  GS_CHAIN_CONTEXT *chainContext;
  DEVICESPACEID RealDeviceSpace ;
  int32 colorspacedimension ;
  int32 nDeviceColorants ;
  OBJECT *PSColorSpace ;
  CLINK **pHeadLink;
  GUCR_RASTERSTYLE *hRasterStyle = gsc_getTargetRS(colorInfo);

  colorChain = colorInfo->chainInfo[ colorType ];
  HQASSERT( colorChain != NULL, "colorChain NULL" ) ;
  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  /* The refCnt will be 2 because gsc_constructChain puts an extra claim on */
  HQASSERT( colorChain->refCnt == 2, "Not owner of this colorChain" ) ;
  HQASSERT( !colorInfo->fInvalidateColorChains, "Called when chains need resetting" ) ;

  pHeadLink  = &colorChain->context->pnext;
  HQASSERT(*pHeadLink == NULL, "colorChain body is unexpectedly present") ;

  chaincolorspace = colorChain->iColorSpace ;
  colorspacedimension = colorChain->n_iColorants ;
  PSColorSpace = &colorChain->colorspace ;

  /* If there isn't a suitable ready-made chain, proceed to make one */
  guc_deviceColorSpace( hRasterStyle, &RealDeviceSpace, &nDeviceColorants ) ;
  guc_calibrationColorSpace( hRasterStyle, &calibrationColorSpace ) ;
  HQASSERT( chaincolorspace != SPACE_notset,
            "Chain can't be constructed as colorspace is not set" ) ;

  HQASSERT(colorChain->chainStyle == CHAINSTYLE_COMPLETE,
           "Inconsistent chainStyle");
  if ( ! cc_constructChain( colorInfo ,
                            hRasterStyle ,
                            colorType ,
                            FALSE ,
                            colorChain ,
                            pHeadLink ,
                            PSColorSpace ,
                            chaincolorspace ,
                            colorspacedimension ,
                            RealDeviceSpace ,
                            nDeviceColorants ,
                            calibrationColorSpace ,
                            FALSE ))
    return FALSE ;

  return TRUE ;
}

/* Builds the normal color chain into the gstate based on the head link
 * defined by the parameters.
 */
Bool gsc_constructChain( GS_COLORinfo *colorInfo, int32 colorType )
{
  GS_CHAINinfo        *colorChain ;
  GS_CHAINinfo        *cachedColorChain ;
  Bool                result ;
  Bool                dummy;

  /* This is a good central point, called from almost everywhere that uses a
   * color chain. Invalidate the chains for this colorInfo where appropriate.
   */
  if ( colorInfo->fInvalidateColorChains ) {
    if (!cc_invalidateColorChains(colorInfo, TRUE))
      return FALSE;
    colorInfo->fInvalidateColorChains = FALSE ;
  }

  colorChain = colorInfo->chainInfo[ colorType ];
  HQASSERT(colorChain != NULL, "colorChain NULL");

  if ( colorChain->context != NULL && colorChain->context->pnext != NULL)
    return TRUE ; /* a valid colorchain exists */

  METRICS_INCREMENT(nChainRequests);

  if (!cc_findChainCacheEntry(colorInfo, colorType, FALSE,
                              &cachedColorChain, NULL, &dummy))
    return FALSE;

  if ( cachedColorChain != NULL ) {
    HQASSERT(cachedColorChain->context != NULL &&
             cachedColorChain->context->pnext != NULL,
             "An empty chain in the cache");

    /* This chain already exists in the ChainCache. Copy it across to the chain
     * in hand, along with associated caches.
     * NB. We don't copy pCurrentCMYK & friends because they may rely on PS VM
     * which isn't necessarily safe to invoke in a cached chain. And they are so
     * rarely used that they aren't worth keeping.
     */
    cc_destroyChainContext(&colorChain->context);
    colorChain->context = cachedColorChain->context;
    CLINK_RESERVE(colorChain->context);

    METRICS_INCREMENT(nChainCacheHits);
    return TRUE ;
  }

  if (!cc_updateChain(&colorInfo->chainInfo[colorType]) ||
      !cc_createChainContextIfNecessary(&colorInfo->chainInfo[colorType]->context,
                                        colorInfo)) {
    (void) cc_invalidateChain(colorInfo, colorType, TRUE);
    return FALSE;
  }
  colorChain = colorInfo->chainInfo[ colorType ] ;

  /* Now build the chain.
   * To guard against the possibility of a chain being invalidated (or even
   * destroyed) during the construction as a side effect of executing postscript
   * in named color lookup procedures, we will protect the chain.
   */
  CLINK_RESERVE(colorChain);

  result = doConstructChain( colorInfo, colorType ) ;

  /* After constuction, colorChain may not be in the gstate due to the above so
   * put it back in. If this is the case any changes made to the head link, eg. via
   * setcolorspace and friends will be lost (but such postscript is very silly).
   * If the named color lookup was sensible, ie. it didn't contain colorspace or
   * painting operators, then the color chain in the gstate will be invalid.
   * NB. If colorChain is still in the gstate, the result will be just a
   * release of the chain.
   */
  cc_destroyChain(&colorInfo->chainInfo[colorType]);
  colorInfo->chainInfo[colorType] = colorChain;

  if (result) {
    if (!cc_addChainCacheEntry(colorInfo, colorType, FALSE, FALSE))
      result = FALSE;
  }


  if (!result) {
    HQASSERT(CLINK_OWNER(colorChain), "not the colorChain owner") ;
    ( void )cc_invalidateChain( colorInfo, colorType, TRUE ) ;
  }

  return result ;
}

Bool gsc_getChainOutputColorSpace( GS_COLORinfo *colorInfo, int32 colorType,
                                   COLORSPACE_ID *colorSpaceId,
                                   OBJECT **colorSpaceObj )
{
  GS_CHAINinfo  *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;
  CLINK         *pLink ;

  HQASSERT( colorInfo , "colorInfo is NULL in gsc_getChainOutputColorSpace" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getChainOutputColorSpace");
  HQASSERT( colorSpaceId,  "cc_getChainOutputColorSpace: colorSpaceId is NULL" ) ;
  HQASSERT( colorSpaceObj, "cc_getChainOutputColorSpace: colorSpaceObj is NULL" ) ;

  /* We need to produce a PS object which describes the colorspace that we are
   * using for output (ie a name corresponding to the real device space, such
   * as /DeviceCMYK, or for a /Separation (or /DeviceN) with the device can
   * handle, the PS object (array) which specifies that color space.
   * This is stored in the final clink () by cc_constructDeviceChain.
   */
  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  pLink = chainContext->pnext ;
  HQASSERT(pLink != NULL, "pLink NULL");

  while (pLink->pnext != NULL)
    pLink = pLink->pnext ;

  *colorSpaceObj = NULL;
  *colorSpaceId  = pLink->oColorSpace;

  if (pLink->linkType == CL_TYPEdevicecode) {
    cc_deviceGetOutputColorSpace(pLink, colorSpaceObj);
    switch (pLink->oColorSpace) {
    case SPACE_DeviceRGB:
    case SPACE_DeviceCMYK:
    case SPACE_DeviceGray:
    case SPACE_DeviceCMY:
    case SPACE_DeviceRGBK:
    case SPACE_DeviceK:
    case SPACE_Pattern:
      /* For these cases, colorSpaceObj will be obtained from internaldict later */
      HQASSERT( *colorSpaceObj == NULL,
                "cc_getChainOutputColorSpace: colorSpaceObj in clink != NULL") ;
      break ;
    default:
      HQASSERT( *colorSpaceObj != NULL,
                "cc_getChainOutputColorSpace: colorSpaceObj in clink == NULL"
                "for non-simple output colorspace" ) ;
      break ;
    }
  }
  else if (pLink->linkType == CL_TYPEpresep) {
    /* For this case, colorSpaceObj will be obtained from internaldict later */
    HQASSERT(pLink->oColorSpace == SPACE_Recombination,
             "cc_getChainOutputColorSpace: Expected SPACE_Recombination") ;
    /* A hack for recombine, just make it look like DeviceGray (color values must
     * be inverted elsewhere
     */
    *colorSpaceId  = SPACE_DeviceGray ;
  }
  else
    HQFAIL("Invalid link type in cc_getChainOutputColorSpace") ;

  return TRUE ;
}

/**
  * Does this chain involve mixing the colorant values. It doesn't if all links
  * in the chain have simple 1to1 mappings, or else have simple 1toN mappings.
  * The final link must be a devicecode link.
  * An exact channel match of input to output colorants is required for image
  * code where optimised code requires an exact match. Other cases don't.
  */
Bool gsc_hasIndependentChannels(GS_COLORinfo *colorInfo, int32 colorType,
                                Bool exactChannelMatch,
                                Bool *independent)
{
  GS_CHAINinfo *colorChain;

  HQASSERT( colorInfo , "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_hasIndependentChannels");
  HQASSERT(independent, "independent NULL");

  if (!gsc_constructChain(colorInfo, colorType))
    return FALSE;
  colorChain = colorInfo->chainInfo[colorType];

  *independent = cc_hasIndependentChannels(colorChain, exactChannelMatch, TRUE);

  return TRUE;
}

/**
 * Does this incomplete chain have independent channels. The same rules as for
 * gsc_hasIndependentChannels() are applied.
 */
Bool cc_incompleteChainIsSimple(GS_CHAINinfo *colorChain)
{
  return cc_hasIndependentChannels(colorChain, FALSE, FALSE);
}


static Bool cc_hasIndependentChannels(GS_CHAINinfo *colorChain,
                                      Bool exactChannelMatch,
                                      Bool chainIsComplete)
{
  CLINK *pLink;
  Bool independent;
  GS_CHAIN_CONTEXT *chainContext;

  UNUSED_PARAM(Bool, chainIsComplete);
  HQASSERT( colorChain , "colorChain NULL" ) ;

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "chainContext NULL");

  /* A color chain is deemed to have independent channels if there is no color
   * conversion going on. This can be tested by checking to see if there are
   * any channel mixing links in the chain. And the last link must be a
   * devicecode link.
   */

  pLink = chainContext->pnext ;
  if (pLink == NULL) {
    /* If we have been called whilst the chain is still being constructed then we
     * define the chain to have independent channels for the purpose of color
     * interception.
     */
    HQASSERT(!chainIsComplete || pLink != NULL, "pLink NULL");
    return TRUE;
  }

  while (pLink->linkType == CL_TYPEindexed ||
         pLink->linkType == CL_TYPEgraytorgb ||
         pLink->linkType == CL_TYPEgraytocmyk ||
         pLink->linkType == CL_TYPEgraytok ||
         pLink->linkType == CL_TYPErgborcmy_invert ||
         pLink->linkType == CL_TYPEtransfer ||
         pLink->linkType == CL_TYPEallseptinttransform ||
         pLink->linkType == CL_TYPEblackevaluate ||
         pLink->linkType == CL_TYPEblackremove ||
         pLink->linkType == CL_TYPEblackreplace) {
    /* Ignore these links. They are either simple 1to1 conversions or else a
     * pass the same single value to n channels which isn't colorant mixing.
     */
    pLink = pLink->pnext ;
    if (pLink == NULL) {
      /* Ditto to above comment for incomplete chains */
      HQASSERT(!chainIsComplete || pLink != NULL, "pLink NULL");
      return TRUE;
    }
  }

  if (pLink->linkType == CL_TYPEdevicecode) {
    int32 nColorants ;
    COLORANTINDEX *piColorants ;
    COLORVALUE *dummyColorValues ;
    Bool dummyOverprinting ;

    if (exactChannelMatch) {
      cc_deviceGetOutputColors( pLink,
                                &dummyColorValues, &piColorants,
                                &nColorants, &dummyOverprinting ) ;

      /* Normally, this will be true. But for photoinks it won't be, nor for
       * DeviceN colorspaces containing duplicates or /None that were culled
       * in the device code link. Returning true here would completely confuse
       * the image expansion code.
       */
      if (nColorants == pLink->n_iColorants) {
        int32 i;
        independent = TRUE ;
        for (i = 0; i < nColorants; i++)
          if (pLink->iColorants[i] != piColorants[i])
            independent = FALSE ;
      }
      else
        independent = FALSE ;
    }
    else
      independent = TRUE ;
  }
  else
    independent = FALSE ;

  return independent;
}

/* Returns the number of  input colorants to the final link of a chain. It's needed
 * to derive the memory required for buffers used in gsc_invokeChainBlock(). */
Bool cc_maxChainColorants(GS_CHAINinfo *colorChain, int32 *pnColorants)
{
  CLINK *pLink ;
  COLORVALUE *dummyColorValues ;
  COLORANTINDEX *dummyColorants ;
  Bool dummyOverprinting ;
  int32 nColorants ;

  HQASSERT(colorChain != NULL, "colorChain NULL" ) ;
  HQASSERT(pnColorants != NULL, "pnColorants NULL" ) ;
  HQASSERT(colorChain->context != NULL, "colorChain->context NULL" ) ;
  HQASSERT(colorChain->context->pnext != NULL, "pLink NULL" ) ;

  /* For the moment, skip over all links except for the last, the invokeBlock
   * functions just iteratively call the invokeSingle functions at the moment so
   * there is no point using these links.
   */
  pLink = colorChain->context->pnext ;
  while ( pLink->pnext != NULL )
    pLink = pLink->pnext ;

  /* Now get the number of components from the final clink, the max of input
   * and output.
   */
  switch (pLink->linkType) {
  case CL_TYPEdevicecode:
    cc_deviceGetOutputColors( pLink,
                              &dummyColorValues, &dummyColorants,
                              &nColorants, &dummyOverprinting ) ;
    if (nColorants < pLink->n_iColorants)
      nColorants = pLink->n_iColorants ;
    *pnColorants = nColorants ;
    break ;
  case CL_TYPEpresep:
    HQASSERT(pLink->n_iColorants == 1, "Too many colorants for presep") ;
    *pnColorants = pLink->n_iColorants;
    break ;
  case CL_TYPEluminosity:
    *pnColorants = pLink->n_iColorants;
    break ;
  default:
    HQFAIL("Invalid link type at tail of color chain") ;
    return FALSE;
  }

  return TRUE;
}

Bool gsc_getDeviceColorColorants( GS_COLORinfo *colorInfo , int32 colorType ,
                                  int32 *pnColorants ,
                                  COLORANTINDEX **piColorants )
{
  CLINK *pLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;
  COLORVALUE *dummyColorValues ;
  Bool dummyOverprinting ;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_getDeviceColorColorants" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getDeviceColorColorants");
  HQASSERT( pnColorants , "pnColorants NULL in gsc_getDeviceColorColorants" ) ;
  HQASSERT( piColorants , "piColorants NULL in gsc_getDeviceColorColorants" ) ;

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  /* Find the end of the chain; that's where the halftone info lies. */
  pLink = chainContext->pnext ;
  while ( pLink->pnext != NULL )
    pLink = pLink->pnext ;

  /* Now get the color information from the final clink */
  switch (pLink->linkType) {
  case CL_TYPEdevicecode:
    cc_deviceGetOutputColors( pLink,
                              &dummyColorValues, piColorants,
                              pnColorants, &dummyOverprinting ) ;
    break ;
  case CL_TYPEpresep:
    HQASSERT(pLink->n_iColorants == 1, "Too many colorants for presep") ;
    *pnColorants = pLink->n_iColorants ;
    *piColorants = pLink->iColorants ;
    break ;
  case CL_TYPEluminosity:
    *pnColorants = cc_luminosity_ncomps();
    *piColorants = cc_luminosity_colorants();
    break ;
  default:
    HQFAIL("Invalid link type at tail of color chain") ;
    break ;
  }

  return TRUE ;
}

Bool gsc_updateHTCacheForShfillDecomposition(GS_COLORinfo *colorInfo, int32 colorType)
{
  CLINK* pLink;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT(colorInfo, "colorInfo is null");
  COLORTYPE_ASSERT(colorType, "gsc_updateHTCache");

  if (! gsc_constructChain(colorInfo, colorType))
    return FALSE;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  pLink = chainContext->pnext;
  while (pLink->pnext) {
    pLink = pLink->pnext;
  }

  switch (pLink->linkType) {
  case CL_TYPEdevicecode:
    cc_deviceUpdateHTCacheForShfillDecomposition(pLink);
    break;

  case CL_TYPEpresep:
    /* Should never try to shfill decomposition with a presep chain
       (when recombine interception is on, shfill patches are created instead). */
    HQFAIL("Should not call gsc_updateHTCache with a recombine interception on");
    return error_handler(UNREGISTERED);

  case CL_TYPEluminosity:
    /* A no-op, no need to update halftone cache. */
    break;

  default:
    HQFAIL("Invalid link type at the tail of color chain");
    return error_handler(UNREGISTERED);
  }

  return TRUE;
}

Bool gsc_isPreseparationChain( GS_COLORinfo *colorInfo, int32 colorType , Bool *preseparation )
{
  CLINK *pLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_isPreseparationChain" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_isPreseparationChain");
  HQASSERT( preseparation , "preseparation NULL in gsc_isPreseparationChain" ) ;

  /* A color chain is deemed to produce preseparation output if the last
   * CLINK is a CL_TYPEpresep.
   */
  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  pLink = chainContext->pnext ;
  HQASSERT( pLink , "pLink NULL" ) ;

  while ( pLink->pnext != NULL )
    pLink = pLink->pnext ;

  *preseparation = ( pLink->linkType == CL_TYPEpresep ) ;
  return TRUE ;
}

Bool gsc_colorChainIsComplex( GS_COLORinfo *colorInfo, int32 colorType ,
                              Bool *iscomplex, Bool *fast_rgb_gray_candidate,
                              Bool *fast_rgb_cmyk_candidate)
{
  CLINK *pLink ;
  GS_CHAINinfo *colorChain ;
  GS_CHAIN_CONTEXT *chainContext;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_colorChainIsComplex" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_colorChainIsComplex");
  HQASSERT( iscomplex , "iscomplex NULL in gsc_colorChainIsComplex" ) ;

  /* A color chain is deemed to be complex if it involves anything too time consuming.
   * That mainly means other than the simle rgb->cmy, cmyk->gray, etc... color spaces.
   */
  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL");

  chainContext = colorChain->context;
  HQASSERT(chainContext != NULL, "colorChain->context NULL");

  /* Presume the chain is complex until proven it's not */
  *iscomplex = TRUE ;

  pLink = chainContext->pnext ;
  HQASSERT( pLink , "pLink NULL when not expected" ) ;

  /* Simple RGB to Gray chains can be optimised using LUTs, when color
   * converting images.
   */
  if (fast_rgb_gray_candidate != NULL)
    *fast_rgb_gray_candidate = ( pLink->linkType == CL_TYPErgbtogray
                               && pLink->pnext != NULL
                               && pLink->pnext->linkType == CL_TYPEdevicecode);

  /* Simple RGB to CMYK chains could presumably also be optimised using LUTs
   * when color converting images, (or even by doing the maths directly).
  */
  if (fast_rgb_cmyk_candidate != NULL)
    *fast_rgb_cmyk_candidate = (pLink->linkType == CL_TYPErgbtocmyk &&
                                pLink->pnext != NULL &&
                                pLink->pnext->linkType == CL_TYPEdevicecode);

  /* Walk over the links in the chain until we find a complex link, or the end
   * of the chain.
   */
  for (; pLink != NULL; pLink = pLink->pnext) {
    Bool skipLink = FALSE;

    switch (pLink->linkType) {
    case CL_TYPEtinttransform:
      /* Ignore a simple tint transform (typically a recombine one running in C). */
      skipLink = !cc_tinttransformiscomplex(pLink);
      break;

    case CL_TYPEtransfer:
    case CL_TYPEallseptinttransform:
    case CL_TYPErgbtorgbk:
    case CL_TYPErgborcmy_invert:
    case CL_TYPEcmyktogray:
    case CL_TYPEcmyktorgbk:
    case CL_TYPEcmyktorgb:
    case CL_TYPEcmyktocmy:
    case CL_TYPEgraytocmyk:
    case CL_TYPEgraytorgbk:
    case CL_TYPEgraytocmy:
    case CL_TYPEgraytorgb:
    case CL_TYPEgraytok:
    case CL_TYPEblackevaluate:
    case CL_TYPEblackremove:
    case CL_TYPEblackreplace:
      /* Ignore all these links because they all have simple invoke functions. */
      skipLink = TRUE;
      break ;

    case CL_TYPEdevicecode:
    case CL_TYPEluminosity:
    case CL_TYPEpresep:
      HQASSERT(pLink->pnext == NULL, "Link should be the last in the chain");
      *iscomplex = FALSE ;
      break ;

    default:
      break ;
    }

    if (!skipLink)
      break;
  }

  return TRUE ;
}

void gsc_setConvertAllSeparation(GS_COLORinfo *colorInfo, int convertAllSeparation)
{
  HQASSERT(colorInfo != NULL, "colorInfo null");
  if (colorInfo->params.convertAllSeparation != convertAllSeparation) {
    (void)cc_invalidateColorChains(colorInfo, TRUE);
    colorInfo->params.convertAllSeparation = convertAllSeparation;
  }
}

/** gsc_generationNumber:
   returns the generation number of the current color space. The number is
   bumped up (wraps back to zero) whenever *any* of the color chains change
   color space. Enables, very cheaply, one to determine if the color space
   has changed using the color cache and CLIDs. [Added for PDF Out]
 */
Bool gsc_generationNumber(GS_COLORinfo *colorInfo, int32 colorType,
                          uint32* pGeneratioNumber)
{
  GS_CHAINinfo* pchain;

  HQASSERT(colorInfo != NULL, "colorInfo null");
  HQASSERT(colorType >= 0 && colorType < GSC_N_COLOR_TYPES,
           "gsc_generationNumber: colorType out of range");

  if (! gsc_constructChain(colorInfo, colorType))
    return FALSE;

  pchain = colorInfo->chainInfo[colorType];
  HQASSERT(pchain != NULL, "colorChain null");

  if (! coc_generationNumber(colorInfo->colorState->cocState, pchain,
                             pGeneratioNumber))
    return error_handler(UNDEFINED);

  return TRUE;
}

void cc_destroyLinks(CLINK *pLink)
{
  HQASSERT(pLink != NULL, "pLink NULL");

  while (pLink) {
    CLINK *next = pLink->pnext;

    /* This frees up all the memory associated with the clink. */
    (pLink->functions->destroy)(pLink);
    pLink = next ;
  }
}

Bool cc_invalidateChain( GS_COLORinfo *colorInfo,
                         int32 colorType,
                         Bool destroyChainList )
{
  GS_CHAINinfo  *colorChain ;
  Bool  status = TRUE;

  HQASSERT(colorInfo != NULL, "cc_invalidateChain: colorInfo null");
  HQASSERT(colorInfo->chainInfo[colorType], "color chain NULL");

  if (destroyChainList) {
    if (!cc_invalidateChainCache(colorInfo, colorType))
      status = FALSE;
  }
  else {
    if (!cc_addChainCacheEntry(colorInfo, colorType, FALSE, TRUE))
      status = FALSE;
  }

  if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
    status = FALSE;
  colorChain = colorInfo->chainInfo[ colorType ] ;

  /* If 'status' is FALSE here, we've probably hit a vmerror, but we're going to
   * continue anyway. The worst consequence of this is that we'll be invalidating
   * a color chain for more than one gstate. This will make no difference to any
   * output because the chain will be regenerated (if there's some memory around
   * at that point).
   *
   * The reason we're going to continue is that one indirect client,
   * gsc_setTargetRS(), needs to remain a void function to avoid much headache
   * in its clients.
   */

  /* Now get rid of the real chains */
  cc_destroyChainContext(&colorChain->context);

  return status;
}

Bool cc_invalidateColorChains(GS_COLORinfo *colorInfo, Bool destroyChainList)
{
  Bool ok = TRUE ;
  int32 nChain;

  HQASSERT(colorInfo != NULL, "cc_invalidateColorChains: colorInfo null");

  for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
    ok = cc_invalidateChain(colorInfo, nChain, destroyChainList) && ok ;
  }

  return ok ;
}

void gsc_markChainsInvalid(GS_COLORinfo *colorInfo)
{
  /* Context flag to allow lazy flushing of all colorchains, and to prevent
   * store_separation from clobbering a chain in the middle of invokation
   */
  colorInfo->fInvalidateColorChains = TRUE;
}

/* ---------------------------------------------------------------------- */

int32 gsc_colorInfoSize()
{
  return sizeof (GS_COLORinfo);
}

/* ---------------------------------------------------------------------- */

void gsc_setOpaque(GS_COLORinfo *colorInfo,
                   Bool opaqueNonStroke, Bool opaqueStroke)
{
  if (colorInfo->gstate.opaqueNonStroke == opaqueNonStroke &&
      colorInfo->gstate.opaqueStroke == opaqueStroke)
    return;

  /* Even in the event of failure, cc_invalidateColorChains() still guarantees
   * that the invalidation has happened. Changing the signature of gsc_updateOpaque()
   * gets complex in the clients.
   */
  (void)cc_invalidateColorChains(colorInfo, FALSE);

  colorInfo->gstate.opaqueNonStroke = opaqueNonStroke;
  colorInfo->gstate.opaqueStroke = opaqueStroke;
}

Bool cc_getOpaque(GS_COLORinfo *colorInfo, TsStrokeSelector type)
{
  if (type == TsStroke)
    return colorInfo->gstate.opaqueStroke;
  else if (type == TsNonStroke)
    return colorInfo->gstate.opaqueNonStroke;

  HQFAIL("Invalid stroke param");
  return TRUE;
}

#if defined( ASSERT_BUILD )
void gsc_getOpaque(GS_COLORinfo *colorInfo,
                   Bool *opaqueNonStroke, Bool *opaqueStroke)
{
  *opaqueNonStroke = colorInfo->gstate.opaqueNonStroke;
  *opaqueStroke = colorInfo->gstate.opaqueStroke;
}
#endif

/* ---------------------------------------------------------------------- */

GUCR_RASTERSTYLE *gsc_getRS(const GS_COLORinfo *colorInfo)
{
  return colorInfo->deviceRS;
}

void gsc_replaceRasterStyle(GS_COLORinfo *colorInfo, GUCR_RASTERSTYLE *rasterStyle)
{
  /* The color info structure takes over ownership of rasterStyle and so
   * we do not need to do a reserve (a ref count increment).
   */

  /* Even in the event of failure, cc_invalidateColorChains() still guarantees
   * that the invalidation has happened. Changing the signature of gsc_replaceRasterStyle()
   * gets complex in the clients. As things are, the worst that can happen is that
   * we lose the performance of reusing chains in the ChainCache.
   */
  (void)cc_invalidateColorChains(colorInfo, FALSE);

  if (colorInfo->deviceRS != NULL)
    guc_discardRasterStyle(&colorInfo->deviceRS);

  colorInfo->deviceRS = rasterStyle;

  /* Must also replace the targetRS with rasterStyle since the underlying
     rasterstyle has just changed.  This requires a ref count increment. */
  guc_reserveRasterStyle(rasterStyle);
  if (colorInfo->targetRS != NULL)
    guc_discardRasterStyle(&colorInfo->targetRS);
  colorInfo->targetRS = rasterStyle;
}

void gsc_setDeviceRS(GS_COLORinfo *colorInfo, GUCR_RASTERSTYLE *deviceRS)
{
  HQASSERT(colorInfo->deviceRS != NULL && deviceRS != NULL,
           "Expected a device rasterstyle");

  if (deviceRS == colorInfo->deviceRS)
    return;

  /* Even in the event of failure, cc_invalidateColorChains() still guarantees
   * that the invalidation has happened. Changing the signature of gsc_setDeviceRS()
   * gets complex in the clients. As things are, the worst that can happen is that
   * we lose the performance of reusing chains in the ChainCache.
   */
  (void)cc_invalidateColorChains(colorInfo, FALSE);

  guc_reserveRasterStyle(deviceRS);
  guc_discardRasterStyle(&colorInfo->deviceRS);
  colorInfo->deviceRS = deviceRS;
}

GUCR_RASTERSTYLE *gsc_getTargetRS(GS_COLORinfo *colorInfo)
{
  return colorInfo->targetRS;
}

void gsc_setTargetRS(GS_COLORinfo *colorInfo, GUCR_RASTERSTYLE *targetRS)
{
  HQASSERT(colorInfo->targetRS != NULL && targetRS != NULL,
           "Expected a target rasterstyle");

  if (targetRS == colorInfo->targetRS)
    return;

  /* Even in the event of failure, cc_invalidateColorChains() still guarantees
   * that the invalidation has happened. Changing the signature of gsc_setTargetRS()
   * gets complex in the clients. As things are, the worst that can happen is that
   * we lose the performance of reusing chains in the ChainCache.
   */
  (void)cc_invalidateColorChains(colorInfo, FALSE);

  guc_reserveRasterStyle(targetRS);
  guc_discardRasterStyle(&colorInfo->targetRS);
  colorInfo->targetRS = targetRS;
}

/* ---------------------------------------------------------------------- */

CLINK *cc_common_create(int32              nColorants,
                        COLORANTINDEX      *pColorants,
                        COLORSPACE_ID      iColorSpace,
                        COLORSPACE_ID      oColorSpace,
                        int32              linkType,
                        size_t             structSize,
                        CLINKfunctions     *functions,
                        int32              nIds)
{
  int32   i;
  CLINK   *pLink;
  size_t  commonStructSize;
#ifdef PLATFORM_IS_64BIT
  uint8   pad_bytes = 8;
#endif

  commonStructSize = sizeof(CLINK);

  if ( nIds > 0 )
    commonStructSize += nIds * sizeof(CLID);

  commonStructSize += nColorants * sizeof(USERVALUE);

  if ( pColorants )
    commonStructSize += nColorants * sizeof(COLORANTINDEX);

#ifdef PLATFORM_IS_64BIT
    /* Add bytes up to next 8 byte boundary so that any private data following
     * this which requires 8 byte aligment will be ok on 64-bit platforms. */
    /* \todo Sort this all out properly with multi_hetero or similar allocator */
    pad_bytes = 8 - (commonStructSize % 8);

  if ( pad_bytes < 8 )
    commonStructSize += pad_bytes;
#endif

  structSize += commonStructSize;

  pLink = mm_sac_alloc(mm_pool_color,
                       structSize,
                       MM_ALLOC_CLASS_NCOLOR );

  if (pLink == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Correct alignment for 64-bit platforms relies on the following being
   * ordered in decreasing order of alignment requirement.
   */
  pLink->idcount = nIds;
  pLink->idslot = NULL;

  if ( nIds > 0 )
    pLink->idslot = (CLID *) (pLink + 1);

  pLink->iColorValues = (nIds > 0) ? (USERVALUE *) (pLink->idslot + nIds)
                                   : (USERVALUE *) (pLink + 1);

  pLink->iColorants = NULL;
  if (nColorants > 0 && pColorants != NULL)
    pLink->iColorants = (COLORANTINDEX *) (pLink->iColorValues + nColorants);

#ifdef PLATFORM_IS_64BIT
  if ( pad_bytes < 8) {
    if ( pLink->iColorants != NULL)
      bzero((char *) (pLink->iColorants + nColorants), pad_bytes);
    else
      bzero((char *) (pLink->iColorValues + nColorants), pad_bytes);
  }
#endif

  pLink->structSize = structSize;
  pLink->linkType = linkType;
  pLink->pnext = NULL;
  pLink->functions = functions;
  pLink->n_iColorants = nColorants;

  for (i = 0; i < nColorants; i++)
    pLink->iColorValues[i] = 0.0f;

  if ( pColorants ) {
    for (i = 0; i < nColorants; i++)
      pLink->iColorants[i] = pColorants[i];
  }

  pLink->iColorSpace = iColorSpace;
  pLink->oColorSpace = oColorSpace;

  pLink->overprintProcess = 0u;
  pLink->blackType = BLACK_TYPE_UNKNOWN;
  pLink->blackValueToPreserve = 0.0f;

  pLink->p.shared = NULL;

  METRICS_INCREMENT(nLinkCreates);

  return pLink;
}

void cc_common_destroy(CLINK *pLink)
{
  HQASSERT(pLink != NULL, "pLink not set");

  mm_sac_free(mm_pool_color, pLink, pLink->structSize);
  METRICS_INCREMENT(nLinkDestroys);
}

size_t cc_commonStructSize(CLINK *pLink)
{
  size_t structSize = 0;
  structSize += sizeof(CLINK);
  if ( pLink->idslot )
    structSize += pLink->idcount * sizeof(CLID);
  structSize += pLink->n_iColorants * sizeof(USERVALUE);
  if ( pLink->iColorants )
    structSize += pLink->n_iColorants * sizeof(COLORANTINDEX) ;

#ifdef PLATFORM_IS_64BIT
  {
    /* Add bytes up to next 8 byte boundary so that any private data following
     * this which requires 8 byte aligment will be ok on 64-bit platforms. */
    /* \todo Sort this all out properly with multi_hetero or similar allocator */
    uint8 pad_bytes = 8 - (structSize % 8);

    if ( pad_bytes < 8 )
      structSize += pad_bytes;
  }
#endif

  return structSize ;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
void cc_commonAssertions(CLINK          *pLink,
                         int32          linkType,
                         size_t         structSize,
                         CLINKfunctions *functions)
{
  HQASSERT(pLink != NULL, "pLink not set");

  structSize += cc_commonStructSize(pLink);

  HQASSERT(pLink->linkType == linkType, "link type not set"); \
  HQASSERT(pLink->structSize == structSize, "structure size not correct");
  HQASSERT(pLink->functions == functions, "function structure not set"); \
  HQASSERT(pLink->n_iColorants >= 0, "number of input colorants not set");

  HQASSERT(pLink->idslot == NULL ? pLink->idcount <= 0 : pLink->idcount > 0,
           "id info in inconsistent state");
  HQASSERT(pLink->idslot == NULL || pLink->idslot == (CLID *) (pLink + 1),
           "clids not set");

  HQASSERT((pLink->idslot != NULL && pLink->iColorValues == (USERVALUE *) (pLink->idslot + pLink->idcount)) ||
           (pLink->idslot == NULL && pLink->iColorValues == (USERVALUE *) (pLink + 1)),
           "iColorValues not set");

  HQASSERT(pLink->iColorants == NULL ||
           pLink->iColorants == (COLORANTINDEX *) (pLink->iColorValues + pLink->n_iColorants),
          "iColorValues not set");

  HQASSERT(pLink->iColorSpace != SPACE_notset ||
           pLink->n_iColorants == 0, "input colorspace not set");
  HQASSERT(pLink->oColorSpace != SPACE_notset ||
           pLink->n_iColorants == 0 ||
           linkType == CL_TYPEcrd, "output colorspace not set");
}
#endif


mps_res_t cc_scan( mps_ss_t ss, CLINK *pLink )
{
  mps_res_t res = MPS_RES_OK;

  if ( pLink->pnext != NULL) {
    res = cc_scan( ss, pLink->pnext );
    if ( res != MPS_RES_OK )
      return res;
  }
  if ( pLink->functions->scan != NULL )
    res = (pLink->functions->scan)( ss, pLink );
  return res;
}


/* ---------------------------------------------------------------------- */
Bool cc_create_xuids( OBJECT *poUniqueID ,
                      OBJECT *poXUID ,
                      int32 *pnXUIDs ,
                      int32 **ppXUIDs )
{
  int32 nXUIDs ;
  int32 *pXUIDs ;
  OBJECT *theo ;

  HQASSERT( pnXUIDs , "pnXUIDs NULL in cc_create_xuids" ) ;
  HQASSERT( ppXUIDs , "ppXUIDs NULL in cc_create_xuids" ) ;

  nXUIDs = 0 ;
  pXUIDs = NULL ;

  /* XUID is taken in preference to UniqueID. */
  theo = poXUID ; /* XUID */
  if ( theo ) {
    int32 len = theLen(*theo) ;
    HQASSERT( oType( *theo ) == OARRAY ||
              oType( *theo ) == OPACKEDARRAY ,
              "XUID should be an OARRAY/OPACKEDARRAY" ) ;
    if ( len != 0 ) {
      int32 i ;
      OBJECT *olist ;
      pXUIDs = mm_sac_alloc(mm_pool_color ,
                            ( len + 1 ) * sizeof( int32 ) ,
                            MM_ALLOC_CLASS_NCOLOR ) ;
      if ( pXUIDs == NULL )
        return error_handler( VMERROR ) ;
      nXUIDs = len + 1 ;

      olist = oArray( *theo ) ;
      pXUIDs[ 0 ] = 2 ;
      for ( i = 1 ; i <= len ; ++i ) {
        if ( oType( *olist ) != OINTEGER )
          return error_handler( TYPECHECK ) ;
        pXUIDs[ i ] = oInteger( *olist ) ;
        ++olist ;
      }
    }
  }
  else {
    theo = poUniqueID ; /* UniqueID */
    if ( theo ) {
      HQASSERT( oType( *theo ) == OINTEGER ,
                "UniqueID should be an OINTEGER" ) ;
      pXUIDs = mm_sac_alloc(mm_pool_color ,
                            2 * sizeof( int32 ) ,
                            MM_ALLOC_CLASS_NCOLOR ) ;
      if ( pXUIDs == NULL )
        return error_handler( VMERROR ) ;
      nXUIDs = 2 ;
      pXUIDs[ 0 ] = 1 ;
      pXUIDs[ 1 ] = oInteger( *theo ) ;
    }
  }

  /* If XUID/UniqueID don't exist then generate a unique number. */
  if ( nXUIDs == 0 ) {
    static int32 XUIDUniqueID = 0 ;
    pXUIDs = mm_sac_alloc(mm_pool_color ,
                          2 * sizeof( int32 ) ,
                          MM_ALLOC_CLASS_NCOLOR ) ;
    if ( pXUIDs == NULL )
      return error_handler( VMERROR ) ;
    nXUIDs = 2 ;
    pXUIDs[ 0 ] = 0 ;
    pXUIDs[ 1 ] = ++XUIDUniqueID ;
  }

  (*pnXUIDs) = nXUIDs ;
  (*ppXUIDs) = pXUIDs ;

  return TRUE ;
}

void cc_destroy_xuids( int32 *pnXUIDs , int32 **ppXUIDs )
{
  int32 nXUIDs ;
  int32 *pXUIDs ;

  HQASSERT( pnXUIDs , "pnXUIDs NULL in cc_destroy_xuids" ) ;
  HQASSERT( ppXUIDs , "ppXUIDs NULL in cc_destroy_xuids" ) ;

  nXUIDs = (*pnXUIDs) ;
  pXUIDs = (*ppXUIDs) ;

  if ( pXUIDs != NULL ) {
    mm_sac_free( mm_pool_color ,
                 ( mm_addr_t )pXUIDs ,
                 ( mm_size_t )nXUIDs * sizeof( int32 )) ;

    (*pnXUIDs) = 0 ;
    (*ppXUIDs) = NULL ;
  }
  else
    HQASSERT( nXUIDs == 0 , "pXUIDs NULL but nXUIDs not 0" ) ;
}

/* ---------------------------------------------------------------------- */

static Bool extendColorantIndexArray ( COLORANTINDEX **pColorantIndexArray,
                                       int32 *pColorantIndexArraySize,
                                       int32 newColorantIndexArraySize )
{
  COLORANTINDEX *pColorantIndexArrayTemp;
  int32 i;

  HQASSERT(newColorantIndexArraySize > STACK_EXTCIA_SIZE, "newColorantIndexArraySize is too small");

  pColorantIndexArrayTemp = mm_alloc(mm_pool_color,
                                     newColorantIndexArraySize * sizeof(COLORANTINDEX),
                                     MM_ALLOC_CLASS_NCOLOR);

  if ( pColorantIndexArrayTemp == NULL )
    return error_handler(VMERROR) ;

  for ( i = 0 ; i < *pColorantIndexArraySize ; i++ )
    pColorantIndexArrayTemp[ i ] = (*pColorantIndexArray) [ i ] ;

  if ( *pColorantIndexArraySize > STACK_EXTCIA_SIZE )
    mm_free(mm_pool_color,
            *pColorantIndexArray,
            (*pColorantIndexArraySize) * sizeof(COLORANTINDEX) );

  *pColorantIndexArray = pColorantIndexArrayTemp ;
  *pColorantIndexArraySize = newColorantIndexArraySize ;

  return TRUE ;
}

Bool cc_extractP( OBJECT into[], int32 n, OBJECT *theo )
{
  int32 i;
  int32 type;

  if (theLen(*theo) != n)
    return error_handler( RANGECHECK ) ;

  theo = oArray( *theo ) ;
  for ( i = 0 ; i < n ; ++i ) {
    type = oType( *theo ) ;
    if (( type != OARRAY && type != OPACKEDARRAY ) ||
        ( ! oExecutable( *theo )))
      return error_handler( TYPECHECK ) ;
    if ( ! oCanExec( *theo ) &
         ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
    Copy( &into[ i ] , theo ) ;
    ++theo ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* findCIEProcedure returns a pointer to a C function which implements an
   encode/decode/transform procedure. It returns NULL if the procedure
   couldn't be found, in which case the PostScript procedure is called
   instead. */
static OBJECT *switchcieprocs = NULL ;

CIECallBack cc_findCIEProcedure(OBJECT *theo)
{
  if ( ! switchcieprocs ) {
    OBJECT *printerdict ;
    OBJECT dpd = OBJECT_NOTVM_NOTHING;

    object_store_name(&dpd, NAME_DollarPrinterdict, LITERAL);
    if ( (printerdict = fast_sys_extract_hash( &dpd) ) != NULL) {
      switchcieprocs = fast_extract_hash_name(printerdict, NAME_switchcieprocs) ;
    }
  }

  if ( switchcieprocs ) {
    if ( theTags(*theo) & EXECUTABLE ) {
      OBJECT_MATCH oms ;

      oms.obj = theo ;
      oms.key = NULL ;

      (void)walk_dictionary(switchcieprocs, wd_match_obj, &oms) ;

      if ( oms.key && oType(*(oms.key)) == OINTEGER ) {
        int32 index = oInteger(*oms.key) ;

        if ( index >= 0 && index < CIE_PROCTABLE_SIZE ) {
          return cieproctable[index] ;
        }
      }
    }
  }

  /* couldn't find switchcieprocs dictionary, or couldn't find proc, or
     it had the wrong type of key, or the key was out of value. */
  return NULL;
}

/* ---------------------------------------------------------------------- */

static Bool createFinalDeviceNColorSpace(GS_CHAIN_CONTEXT *chainContext,
                                         GUCR_RASTERSTYLE *hRasterStyle,
                                         COLORANTINDEX **pColorantIndexArray,
                                         int32 *pColorantIndexArraySize,
                                         int32 nDeviceColorants,
                                         Bool fNonInterceptingChain,
                                         OBJECT *PSColorSpace)
{
  GUCR_CHANNEL *hf ;
  GUCR_COLORANT *hc ;
  OBJECT *olist ;
  OBJECT *colorantArray;
  int32 i;
  OBJECT *finalDevicenCSA;

  HQASSERT( hRasterStyle != NULL, "Invalid hRasterStyle");
  HQASSERT( pColorantIndexArray != NULL, "Invalid pColorantIndexArray");
  HQASSERT( pColorantIndexArraySize != NULL, "Invalid pColorantIndexArraySize");
  HQASSERT( PSColorSpace != NULL, "Invalid PSColorSpace");
  HQASSERT( nDeviceColorants >= 0, "Invalid nDeviceColorants");

  if (fNonInterceptingChain)
    finalDevicenCSA = chainContext->finalDevicenCSAsmp;
  else
    finalDevicenCSA = chainContext->finalDevicenCSAmain;

  HQASSERT( oType(finalDevicenCSA[1]) == ONOTHING,
            "Colorant array already exists in manufacturedCSA");

  /* Associate the colorspace array of 4 objects and colorant list */
  theTags(*PSColorSpace) = OARRAY | LITERAL;
  theLen(*PSColorSpace) = 4;
  oArray(*PSColorSpace) = finalDevicenCSA;

  olist = oArray(*PSColorSpace);

  /* The colorspace type, always DeviceN */
  object_store_name(&olist[0], NAME_DeviceN, LITERAL) ;

  /* The colorant array, allocate here but populate below */
  colorantArray = mm_alloc_with_header(mm_pool_color,
                                       nDeviceColorants* sizeof(OBJECT),
                                       MM_ALLOC_CLASS_NCOLOR);
  if (colorantArray == NULL)
    return error_handler(VMERROR);
  theTags(olist[1]) = OARRAY | LITERAL;
  theLen(olist[1]) = (uint16) nDeviceColorants;
  oArray(olist[1]) = colorantArray;

  /* We should never go through the alternative space/tint transform.
   * Neither do we have the information to be able to create a sensible
   * tint transform (we cannot analytically invert custom transforms).
   * We'll force an error with an illegal tint transform in the event that we
   * have to use it.
   */
  object_store_name(&olist[2], NAME_DeviceGray, LITERAL);

  theTags(olist[3]) = OARRAY | EXECUTABLE;
  theLen(olist[3]) = 1;
  oArray(olist[3]) = &chainContext->illegalTintTransform;

  /* The colorant's will always be directly renderable by the way we are
   * extracting them. Obtaining this list of colorants is the main purpose
   * of this function. These should be placed into the colorants array, the
   * second element of the colorspace.
   */
  if ( nDeviceColorants > *pColorantIndexArraySize ) {
    if ( ! extendColorantIndexArray( pColorantIndexArray,
                                     pColorantIndexArraySize,
                                     nDeviceColorants + STACK_EXTCIA_SIZE ) )
      return FALSE ;
  }
  /* Initialise colorant index array to the None colorant */
  for ( i = 0 ; i < *pColorantIndexArraySize ; i++ )
    (*pColorantIndexArray)[ i ] = COLORANTINDEX_NONE ;

  for (hf = gucr_framesStart(hRasterStyle); gucr_framesMore(hf); gucr_framesNext(&hf)) {
    for (hc = gucr_colorantsStart(hf);
         gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(&hc)) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(hc, &colorantInfo) &&
           colorantInfo->colorantIndex < nDeviceColorants ) {
        COLORANTINDEX ci = colorantInfo->colorantIndex ;

        object_store_namecache(&colorantArray[ci], colorantInfo->name, LITERAL);

        (*pColorantIndexArray)[ci] = ci;
      }
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static Bool getallseparationcolorants( GUCR_RASTERSTYLE *hRasterStyle,
                                       COLORANTINDEX **pColorantIndexArray,
                                       int32 *pColorantIndexArraySize,
                                       int32 *pcolorspacedimension )
{
  GUCR_CHANNEL* hf ;
  GUCR_COLORANT* hc ;
  COLORANTINDEX ColorantIndexTemp ;

  int32 i, j;
  int32 nColorantsFound;
  HQASSERT( *pColorantIndexArraySize >= 1, "Invalid ColorantIndexArray");

  /* Insert COLORANTINDEX_ALL at the start of the array */
  (*pColorantIndexArray) [ 0 ] = COLORANTINDEX_ALL;
  nColorantsFound = 1;

  /* Work through the list of colorants. Extend pColorantIndexArray by amounts of
   * STACK_EXTCIA_SIZE as necessary until the whole list has been evaluated
   */
  for (hf = gucr_framesStart (hRasterStyle);
       gucr_framesMore (hf);
       gucr_framesNext (& hf)) {
    for (hc = gucr_colorantsStart(hf);
         gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(&hc)) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(hc, &colorantInfo) ) {
        ++nColorantsFound;
        if ( nColorantsFound > *pColorantIndexArraySize ) {
          if ( ! extendColorantIndexArray ( pColorantIndexArray,
                                            pColorantIndexArraySize,
                                            (*pColorantIndexArraySize) + STACK_EXTCIA_SIZE ) )
            return FALSE ;
        }

        (*pColorantIndexArray) [ nColorantsFound - 1 ] = colorantInfo->colorantIndex ;
      }
    }
  }

  /* Sort the colorants array */
  for ( i = 0 ; i < nColorantsFound ; i++ ) {
    for ( j = i ; j < nColorantsFound ; j++ ) {
      if ( (*pColorantIndexArray) [ j ] < (*pColorantIndexArray) [ i ] ) {
        ColorantIndexTemp = (*pColorantIndexArray) [ j ] ;
        (*pColorantIndexArray) [ j ] = (*pColorantIndexArray) [ i ] ;
        (*pColorantIndexArray) [ i ] = ColorantIndexTemp ;
      }
    }
  }

  /* Prune the array to remove duplicates */
  for ( i = 0, j = 0 ; i < nColorantsFound ; i++ ) {
    if ( (*pColorantIndexArray) [ i ] != (*pColorantIndexArray) [ j ] ) {
      j++;
      (*pColorantIndexArray) [ j ] = (*pColorantIndexArray) [ i ] ;
    }
  }
  nColorantsFound = j+1 ;

  /* Update the colorspacedimension */
  *pcolorspacedimension = nColorantsFound ;
  return TRUE ;
}

Bool gsc_convertRGBtoCMYK( GS_COLORinfo *colorInfo,
                           USERVALUE rgb[ 3 ] , USERVALUE cmyk[ 4 ] )
{
  Bool result ;
  CLINK *pclink ;
  USERVALUE *piColorValues ;

  HQASSERT(colorInfo != NULL, "gsc_convertRGBtoCMYK: colorInfo null");
  HQASSERT(rgb  , "rgb  NULL in gsc_convertRGBtoCMYK" ) ;
  HQASSERT(cmyk , "cmyk NULL in gsc_convertRGBtoCMYK" ) ;

  /* This rgb to cmyk conversion doesn't need to handle direct conversion of 0 0 0 rgb
   * to 0 0 0 1 cmyk. Indeed, it shouldn't do that for vignette's which is one client.
   */
  pclink = cc_rgbtocmyk_create(colorInfo->rgbtocmykInfo, FALSE,
                               &CoreContext.page->colorPageParams);
  if ( pclink == NULL )
    return FALSE ;

  piColorValues = pclink->iColorValues ;
  piColorValues[ 0 ] = rgb[ 0 ] ;
  piColorValues[ 1 ] = rgb[ 1 ] ;
  piColorValues[ 2 ] = rgb[ 2 ] ;
  disable_separation_detection() ;
  result = (pclink->functions->invokeSingle)( pclink , cmyk ) ;
  enable_separation_detection() ;
  (pclink->functions->destroy)( pclink ) ;
  return result ;
}

/* gsc_promoteColor is used to promote DeviceGray or DeviceRGB color to
 * DeviceRGB or DeviceCMYK, matching the behaviour in the vignette
 * detection code for vignettes which span multiple (simple) colorspaces.
 * HDLT uses this to unify the colorspace for a vignette, in the same
 * way as the vignette detection code.
 *
 * Notes:
 *  This routine allocates memory for the new color but does not attempt
 *  to free the original color, and on success sets colorValue, nColors
 *  and oldSpace appropriately for the promoted colorspace.
 */
Bool gsc_promoteColor( GS_COLORinfo *colorInfo,
                       USERVALUE **colorValue, int32 *nColors,
                       COLORSPACE_ID *oldSpace, COLORSPACE_ID newSpace )
{
  USERVALUE *newColor ;
  int32 n_newColors ;

  HQASSERT(colorInfo != NULL, "gsc_promoteColor: colorInfo null");
  HQASSERT( colorValue && *colorValue,
            "gsc_promoteColor: colorValue is invalid" ) ;
  HQASSERT( nColors, "gsc_promoteColor: nColors is NULL" ) ;
  HQASSERT( oldSpace, "gsc_promoteColor: oldSpace is NULL" ) ;

  HQASSERT( *oldSpace == SPACE_DeviceGray || *oldSpace == SPACE_DeviceRGB,
            "gsc_promoteColor: oldSpace must be DeviceGray or DeviceRGB" ) ;
  HQASSERT( newSpace == SPACE_DeviceRGB || newSpace == SPACE_DeviceCMYK,
            "gsc_promoteColor: newSpace must be DeviceRGB or DeviceCMYK" ) ;
  HQASSERT( *oldSpace != newSpace, "gsc_promoteColor: nothing to do" ) ;

  n_newColors = ( newSpace == SPACE_DeviceCMYK ? 4 : 3 ) ;
  newColor = mm_alloc(mm_pool_color,
                      sizeof( USERVALUE ) * n_newColors,
                      MM_ALLOC_CLASS_NCOLOR ) ;
  if ( newColor != NULL )
    return error_handler( VMERROR ) ;

  if ( newSpace == SPACE_DeviceCMYK ) {
    if ( *oldSpace == SPACE_DeviceGray ) {
      /* Go via RGB */
      newColor[ 0 ] = newColor[ 1 ] = newColor[ 2 ] = *colorValue[ 0 ] ;
    }
    if ( ! gsc_convertRGBtoCMYK( colorInfo, *colorValue, newColor )) {
      mm_free( mm_pool_color, newColor, sizeof( USERVALUE ) * n_newColors ) ;
      return FALSE ;
    }
  }
  else { /* newSpace == SPACE_DeviceRGB => oldSpace == SPACE_DeviceGray */
    newColor[ 0 ] = newColor[ 1 ] = newColor[ 2 ] = *colorValue[ 0 ] ;
  }

  *colorValue = newColor ;
  *nColors    = n_newColors ;
  *oldSpace   = newSpace ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

/* Purge the NamedColorant cache to the saveLevel from all colorInfo structures.
 * If the saveLevel is set to a negative number, then all cache entries will be
 * purged.
 */
void cc_restoreNamedColorantCache(int32 saveLevel, GS_COLORinfoList *colorInfoList)
{
  GS_COLORinfo *colorInfo;

  for ( colorInfo = colorInfoList->colorInfoHead;
        colorInfo != NULL; colorInfo = colorInfo->next ) {
    cc_purgeNamedColorantStore(colorInfo, saveLevel);
  }
}

/* ---------------------------------------------------------------------- */

/** \defgroup EraseColorInfo Erase colorInfo for the page.
 * \ingroup color
 *
 * This group of functions deals with storing the colorInfo for erase page
 * which is required for obtaining the erase color. A global erase colorInfo
 * is maintained for the front end, but it is subject to save/restore because
 * of recombine shenanigans. So, we're maintaining a stack of colorInfo's to
 * mirror the potential save level, although most values will be NULL at any
 * one time.
 */
/** \{ */

static mps_res_t MPS_CALL cc_savedEraseScan(mps_ss_t ss, void *p, size_t s);
static GS_COLORinfo savedEraseColorInfo[MAXSAVELEVELS];
static Bool savedEraseColorValid[MAXSAVELEVELS];
static mps_root_t savedEraseColorRoot;

/** \todo ajcd 2011-03-08: When the pagedevice target structure is created,
    the erase color info may live in it. */
static GS_COLORinfo *erase_color_info ;

GS_COLORinfo *gsc_eraseColorInfo(void)
{
  HQASSERT(erase_color_info != NULL, "No erase color info setup") ;
  return erase_color_info ;
}

/** Store the erase colorInfo we'll need for final erase color computation in
 * the page.
 */
Bool gsc_loadEraseColorInfo(GS_COLORinfo *colorInfo)
{
  /* It's possible that erase_color_info is already populated, in which case
   * we'll reuse it, otherwise create a new colorInfo.
   */
  if (erase_color_info != NULL)
    gsc_freecolorinfo(erase_color_info);
  else {
    erase_color_info = mm_alloc(mm_pool_color, gsc_colorInfoSize(),
                                MM_ALLOC_CLASS_GSTATE);
    if (erase_color_info == NULL)
      return error_handler(VMERROR);
  }
  gsc_copycolorinfo(erase_color_info, colorInfo);

  /* Must use device RS. */
  gsc_setTargetRS(erase_color_info, gsc_getRS(erase_color_info));

  /* There is no point keeping hold of chains and caches in this one purpose
   * colorInfo since we'll only build just the one chain using it.
   */
  return cc_invalidateColorChains(erase_color_info, TRUE) ;
}

/** Push the current erase_color_info onto the stack of savedEraseColorInfo.
 */
static void cc_saveEraseColor(int32 saveLevel)
{
  int level = NUMBERSAVES(saveLevel);

  HQASSERT(level < MAXSAVELEVELS, "save level too high");

  if (erase_color_info != NULL) {
    gsc_copycolorinfo(&savedEraseColorInfo[level], erase_color_info);
    savedEraseColorValid[level] = TRUE;
  }
}

/** Restore the erase_color_info to its value at the last save. Do it by popping
 * off the stack of savedEraseColorInfo.
 */
static Bool cc_restoreEraseColor(int32 saveLevel)
{
  int i;
  int level = NUMBERSAVES(saveLevel);

  HQASSERT(level < MAXSAVELEVELS, "save level too high");

  /* Free any existing erase_color_info, but as an optimisation, retain the
   * allocation if we are going to immediately reuse it.
   */
  if (erase_color_info != NULL) {
    gsc_freecolorinfo(erase_color_info);
    if (!savedEraseColorValid[level]) {
      mm_free(mm_pool_color, erase_color_info, gsc_colorInfoSize());
      erase_color_info = NULL;
    }
  }
  /* Pop the top colorInfo from the stack into erase_color_info, allocating
   * a new colorInfo if necessary.
   */
  if (savedEraseColorValid[level]) {
    if (erase_color_info == NULL) {
      erase_color_info = mm_alloc(mm_pool_color, gsc_colorInfoSize(),
                                  MM_ALLOC_CLASS_GSTATE);
      if (erase_color_info == NULL)
        return error_handler(VMERROR);
    }
    gsc_copycolorinfo(erase_color_info, &savedEraseColorInfo[level]);
  }

  /* Ensure that all save levels above are freed and initialised */
  for (i = level; i < MAXSAVELEVELS; i++) {
    if (savedEraseColorValid[i]) {
      gsc_freecolorinfo(&savedEraseColorInfo[i]);
      cc_colorInfo_init(&savedEraseColorInfo[i]);
      savedEraseColorValid[i] = FALSE;
    }
  }

  return TRUE;
}

/** GC callback for savedEraseColorInfo stack.
 */
static mps_res_t MPS_CALL cc_savedEraseScan(mps_ss_t ss, void *p, size_t s)
{
  int i;
  mps_res_t res = MPS_RES_OK;

  UNUSED_PARAM(void *, p);
  UNUSED_PARAM(size_t, s);
  HQASSERT(p == savedEraseColorInfo, "Invalid savedEraseColorInfo base");

  for (i = 0; i < MAXSAVELEVELS; i++) {
    if (savedEraseColorValid[i]) {
      res = gsc_scan(ss, &savedEraseColorInfo[i]);
      if (res != MPS_RES_OK) return res;
    }
  }

  /* Also scan the erase colorInfo in the current page. It is unusual to scan
   * objects outside of their structures, but DL_STATE doesn't have a scanner
   * of it's own and this is almost the right place functionally speaking.
   */
  /** \todo ajcd 2011-03-07: This shouldn't be in DL_STATE at all: */
  if (erase_color_info != NULL) {
    res = gsc_scan(ss, erase_color_info);
    if (res != MPS_RES_OK) return res;
  }

  return res;
}

/** Create GC root for the savedEraseColorInfo stack.
 */
static void cc_initEraseColorRoot(void)
{
  mps_res_t res;

  res = mps_root_create(&savedEraseColorRoot, mm_arena, mps_rank_exact(),
                        0, cc_savedEraseScan, &savedEraseColorInfo, 0);
  if (res != MPS_RES_OK) {
    (void)dispatch_SwExit( 1, "Setting Up savedEraseColorRoot for GC.");
    return;
  }
}

/** \} */

/* ---------------------------------------------------------------------- */

/** Callback for save operations.
 */
void gsc_save(int32 saveLevel)
{
  cc_saveEraseColor(saveLevel);
}

/* Prior to a PS restore, special actions have to occur to remove the possibility
 * of refering to objects that have been restored away from PS VM, a PDF context,
 * or whatever else that is subject to save/restore.
 * This function will be called at the higher save level when all objects are
 * still present in memory.
 */
Bool gsc_restore(int32 saveLevel)
{
  GS_COLORinfo *colorInfo;

  HQASSERT(IS_INTERPRETER(), "Restoring color data in back end");

  for ( colorInfo = frontEndColorState->colorInfoList.colorInfoHead;
        colorInfo != NULL; colorInfo = colorInfo->next ) {
    int32 nChain;

    for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
      if (colorInfo->chainInfo[nChain]->context != NULL &&
          colorInfo->chainInfo[nChain]->saveLevel > saveLevel)
        /* The sole purpose of invalidating chains is to pass the caches associated
         * with the color chains over to the chainCache for future reuse - the
         * color chains themselves should already exist in the ChainCache. This
         * is normally done as part of grestore, but restore is more complicated
         * because it also prevents chains being put back into the ChainCache at
         * a later point in the restore process with potentially dangling pointers
         * to freed memory.
         */
        (void) cc_invalidateChain(colorInfo, nChain, FALSE);
    }
  }

  cc_restoreNamedColorantCache(saveLevel, &frontEndColorState->colorInfoList);
  cc_chainCacheRestore(saveLevel);

  cc_purgeICCProfileInfo(frontEndColorState, saveLevel);

  return cc_restoreEraseColor(saveLevel);
}

/* ---------------------------------------------------------------------- */

#ifdef ASSERT_BUILD

/* 'refCnt' is asserted for feasibility as used in various color structures.
 * The most likely reason for an assert is that an attempt is made to use a
 * structure after it has been freed.
 */
void cc_checkRefcnt(cc_counter_t *refCnt)
{
  HQASSERT(*refCnt > 0, "zero or negative refcnt");
  HQASSERT(*refCnt < 0x1000000, "Suspicious color structure");
  HQASSERT(((intptr_t) refCnt & 1) == 0, "odd address");
}

#endif    /* ASSERT_BUILD */

/* --------------------------------------------------------------------------- */

/** File runtime initialisation */
void init_C_globals_gs_color(void)
{
  int i;

  /*****************************************************************************

    Globals are only allowed for frontend color transforms. If an item needs to
    be used for both frontend and backend transforms then it should be put into
    COLOR_STATE.

  *****************************************************************************/

  switchcieprocs = NULL ;
  erase_color_info = NULL ;
  frontEndColorState = NULL;

  for (i = 0; i < MAXSAVELEVELS; i++) {
    cc_colorInfo_init(&savedEraseColorInfo[i]);
    savedEraseColorValid[i] = FALSE;
  }

#ifdef METRICS_BUILD
  gs_color_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&gs_color_metrics_hook) ;
#endif
}

Bool gsc_use_fast_rgb2cmyk(
  GS_COLORinfo  *color_info)
{
  HQASSERT(color_info != NULL, "NULL color info pointer");

  return (color_info->params.useFastRGBToCMYK);
}

/* Log stripped */
