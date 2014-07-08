/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gschead.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain head management.
 */

#include "core.h"

#include "constant.h"           /* EPSILON */
#include "dicthash.h"           /* internaldict */
#include "dictscan.h"           /* dictmatch */
#include "dlstate.h"            /* page->color */
#include "gcscan.h"             /* ps_scan_field */
#include "gu_chan.h"            /* guc_deviceColorSpace */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "mmcompat.h"           /* mm_free_with_header */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* oType */
#include "mps.h"                /* mps_res_t */
#include "params.h"             /* USERPARAMS */
#include "pattern.h"            /* check_pia_valid */
#include "rcbcntrl.h"           /* rcbn_enabled */
#include "swerrors.h"           /* RANGECHECK */

#include "gs_cachepriv.h"       /* COC_RESERVE */
#include "gs_chaincachepriv.h"  /* cc_addChainCacheEntry */
#include "gs_colorpriv.h"       /* cc_destroySingleChain */
#include "gs_tablepriv.h"       /* cc_reserveTomsTable */
#include "gsccmmpriv.h"         /* cc_getCMMRange */
#include "gscdevcipriv.h"       /* cc_destroyblockoverprints */
#include "gschcms.h"            /* gsc_setRequiredReproType */
#include "gsccie.h"             /* cc_getCieBasedABCRange */
#include "gsctable.h"           /* cc_getCieTableRange */
#include "gscpdfpriv.h"         /* cc_getLabRange */
#include "gscindex.h"           /* cc_getindexedhival */
#include "gscicc.h"             /* cc_ICCBased_Range */
#include "gsc_icc.h"            /* cc_get_iccbased_dimension */
#include "gscfastrgbconvpriv.h" /* cc_fastrgb2gray_freelut */

#include "gscheadpriv.h"        /* externs */


static int16 space_names[] = {
  0,                            /* dummy */
  NAME_CIETableA,
  NAME_CIETableABC,
  NAME_CIETableABCD,
  NAME_CIEBasedA,
  NAME_CIEBasedABC,
  NAME_CIEBasedDEF,
  NAME_CIEBasedDEFG,
  NAME_DeviceGray,
  NAME_DeviceRGB,
  NAME_DeviceCMY,
  NAME_DeviceCMYK,
  NAME_Pattern,
  NAME_Indexed,
  NAME_Separation,
  NAME_DeviceN,
  NAME_Lab,
  NAME_CalGray,
  NAME_CalRGB,
  NAME_ICCBased,
  NAME_CMM,
  NAME_Preseparation
};

static int8 space_arraysize[] = {
  0,                            /* dummy */
  3,                            /* CIETableA */
  3,                            /* CIETableABC */
  3,                            /* CIETableABCD */
  2,                            /* CIEBasedA */
  2,                            /* CIEBasedABC */
  2,                            /* CIEBasedDEF */
  2,                            /* CIEBasedDEFG */
  1,                            /* DeviceGray */
  1,                            /* DeviceRGB */
  1,                            /* DeviceCMY */
  1,                            /* DeviceCMYK */
  1 /* or 2 */,                 /* Pattern */
  4,                            /* Indexed */
  4,                            /* Separation */
  4,                            /* DeviceN */
  2,                            /* Lab */
  2,                            /* CalGray */
  2,                            /* CalRGB */
  2,                            /* ICCBased */
  4,                            /* CMM */
  2                             /* Preseparation */
};

static int32 space_channels[] = {
  0,                            /* dummy */
  1,                            /* CIETableA */
  3,                            /* CIETableABC */
  4,                            /* CIETableABCD */
  1,                            /* CIEBasedA */
  3,                            /* CIEBasedABC */
  3,                            /* CIEBasedDEF */
  4,                            /* CIEBasedDEFG */
  1,                            /* DeviceGray */
  3,                            /* DeviceRGB */
  3,                            /* DeviceCMY */
  4,                            /* DeviceCMYK */
  1 /* or 2 */,                 /* Pattern */
  1,                            /* Indexed */
  1,                            /* Separation */
  0,                            /* DeviceN */
  3,                            /* Lab */
  1,                            /* CalGray */
  3,                            /* CalRGB */
  0,                            /* ICCBased */
  0,                            /* CMM */
  1                             /* Preseparation */
};

static Bool init_colorValues(GS_COLORinfo *colorInfo , int32 colorType);
static Bool spaceIsJustBlack( GS_COLORinfo  *colorInfo,
                              OBJECT        *colorspace,
                              Bool          *isJustBlack ) ;

static Bool cc_setPresepColorIsGray( GS_COLORinfo *colorInfo, int32 colorType,
                                     Bool fPresepColorIsGray );
static Bool cc_setCompositing( GS_COLORinfo *colorInfo, int32 colorType,
                               Bool fCompositing );


static void hsv_to_rgb( USERVALUE hsv[ 3 ] , USERVALUE rgb[ 3 ] ) ;

/* ------------------------------------------------------------------------ */

/* Returns the COLORSPACE_ID associated * with the colorspace array (theo) */
Bool gsc_getcolorspacetype(OBJECT        *theo,
                           COLORSPACE_ID *colorspaceID)
{
  COLORSPACE_ID space;
  int32 length = 0;
  NAMECACHE *name ;

  switch ( oType(*theo)) {
  case ONAME:
    name = oName(*theo) ;
    for ( space = 1 ; space < SPACE_Preseparation ; ++space ) {
      if ( name == & system_names[ space_names[ space ]])
        break ;
    }
    if ( space >= SPACE_Preseparation )
      return error_handler( UNDEFINED ) ;
    if ( space_arraysize[ space ] != 1 )
      return error_handler( RANGECHECK ) ;
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    if (theLen(*theo) == 0)
      return error_handler( RANGECHECK ) ;
    length = theLen(*theo) ;
    theo = oArray(*theo) ;
    if ( oType(*theo) != ONAME)
      return error_handler( TYPECHECK ) ;
    name = oName(*theo) ;

    for ( space = 1 ; space < SPACE_Preseparation ; ++space ) {
      if ( name == &system_names[ space_names[ space ]])
        break ;
    }
    /* Note: it is possible that level2 may not restrict the length of the
     * array; trailing elements might be ignored, in which case the test
     * below would be a less-than rather than not-equals.
     */
    if ( space >= SPACE_Preseparation )
      return error_handler( UNDEFINED ) ;
    if ( length != space_arraysize[ space ] ) {
      /* Special case: patterns can have 1 or 2 elements. */
      if ( space == SPACE_Pattern && length == 2 )
        break ;
      /* And another: DeviceN spaces can have an optional attributes
       * dictionary at the end, at least in PDF 1.3.
       */
      if ( space == SPACE_DeviceN && length == 5 )
        break ;
      /* ICCBased could have 2 or 3 elements by this stage,
       * either from PS via a Hqn extension or from our PDF or XPS
       * handling.
      */
      if ( space == SPACE_ICCBased && length == 3 )
        break ;
      return error_handler( RANGECHECK ) ;
    }
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  *colorspaceID = space;

  return TRUE;
}

/* Returns the COLORSPACE_ID and the number of channels (dimension) associated
 * with the colorspace array (theo). Note that in the case of a colored pattern
 * space the dimension is zero and for an uncolored pattern is the dimension of
 * the underlying space.
 */
Bool gsc_getcolorspacesizeandtype(GS_COLORinfo  *colorInfo,
                                  OBJECT        *theo,
                                  COLORSPACE_ID *colorspaceID,
                                  int32         *dimension)
{
  COLORSPACE_ID basespace ;
  int32 length = 0;
  OBJECT *arrayobj = NULL;
  int ncols;

  if (!gsc_getcolorspacetype(theo, colorspaceID))
    return FALSE;

  if (oType(*theo) == OARRAY || oType(*theo) == OPACKEDARRAY) {
    arrayobj = theo;
    theo = oArray(*theo);
    length = theLen(*arrayobj) ;
  }

  switch (*colorspaceID) {
  case SPACE_DeviceN:
    /* We need to inspect the array of colornames. */
    ++theo ;
    if ( oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY )
      return error_handler( TYPECHECK ) ;
    ncols = theLen(*theo) ;
    if ( ncols == 0 )
      return error_handler( RANGECHECK ) ;
    *dimension = ncols;
    break;
  case SPACE_Pattern:
    if ( length == 2 ) {
      /* We need to find the dimension of the underlying space */
      ++theo;
      if (!gsc_getcolorspacesizeandtype(colorInfo, theo, &basespace, &ncols))
        return FALSE;
      if ( basespace == SPACE_Pattern )
        return error_handler( TYPECHECK ) ;
      *dimension = ncols ;
    } else {
      *dimension = 0;
    }
    break;
  case SPACE_ICCBased:
    return gsc_get_iccbased_dimension(colorInfo, arrayobj, dimension);
  case SPACE_CMM:
    /* We need to find the dimension of the underlying input space */
    theo += 2;
    if (!gsc_getcolorspacesizeandtype(colorInfo, theo, &basespace, &ncols))
      return FALSE;
    if ( basespace == SPACE_Pattern || length == 0 )
      return error_handler( TYPECHECK ) ;
    *dimension = ncols ;
    break;
  default:
    *dimension = space_channels[ *colorspaceID ];
    break;
  }

  return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Returns the COLORSPACE_ID and the number of channels (dimension) associated
 * with the base colorspace associated with the pattern colorspace array (theo).
 * If the pattern colorspace specified does not have an associated basespace the
 * function returns a dimension of zero and SPACE_notset.
 */
Bool cc_getpatternbasespace( GS_COLORinfo   *colorInfo,
                             OBJECT         *theo,
                             COLORSPACE_ID  *colorspaceID,
                             int32          *dimension )
{
  switch ( oType(*theo) ) {
  case ONAME:
  case ONULL: /* allowed when colorspace is set directly */
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    switch (theLen(*theo)) {
    case 1:
      break;
    case 2:
      /* We need to find the dimension of the underlying space */
      return gsc_getcolorspacesizeandtype(colorInfo, &oArray(*theo)[1],
                                          colorspaceID, dimension ) ;
    default:
      HQFAIL( "getpatternbasespace was called with bad colorspace" );
    }
    break ;
  default:
    HQFAIL( "getpatternbasespace was called with bad colorspace");
  }

  /* pattern colorspace doesn't have a basespace */
  *colorspaceID = SPACE_notset ;
  *dimension = 0;

  return TRUE;
}

/* ---------------------------------------------------------------------- */

#if defined( ASSERT_BUILD )
static void chainhead_assertions(GS_CHAINinfo *chain)
{
  HQASSERT(chain != NULL, "chain NULL");

  HQASSERT(chain->refCnt > 0, "zero or negative refCnt");
  HQASSERT(chain->refCnt < 0x100000, "rather large refCnt");
  HQASSERT(( (intptr_t) chain & 1) == 0, "odd address");

  HQASSERT(chain->structSize == sizeof(GS_CHAINinfo) +
           chain->n_iColorants * sizeof(USERVALUE),
           "structure size not correct");
  HQASSERT(chain->n_iColorants >= 0, "number of input channels not set");
  HQASSERT(chain->iColorValues == NULL ||
           chain->iColorValues == (USERVALUE*)(chain + 1),
           "iColorValues not set");
  HQASSERT(chain->chainStyle == CHAINSTYLE_COMPLETE ||
           chain->chainStyle == CHAINSTYLE_DUMMY_FINAL_LINK,
           "chainStyle is invalid");
}

static void chainContext_assertions(GS_CHAIN_CONTEXT *chainContext)
{
  HQASSERT(chainContext != NULL, "chainContext NULL");

  HQASSERT(chainContext->refCnt > 0, "zero or negative refCnt");
  HQASSERT(chainContext->refCnt < 0x100000, "rather large refCnt");
  HQASSERT(((intptr_t) chainContext & 1) == 0, "odd address");
}
#else
#define chainhead_assertions(_chain)
#define chainContext_assertions(_context)
#endif

/* ---------------------------------------------------------------------- */

Bool cc_createChainHead( GS_CHAINinfo   **colorChain,
                         COLORSPACE_ID  iColorSpace,
                         int32          colorspaceDimension,
                         OBJECT         *colorspace )
{
  int32           i;
  GS_CHAINinfo    *chainHead = NULL;
  size_t          structSize;

  HQASSERT(colorChain != NULL, "colorChain NULL");
  HQASSERT(*colorChain == NULL, "*colorChain not NULL, memory leak");

  structSize = sizeof(GS_CHAINinfo) + colorspaceDimension * sizeof(USERVALUE);

  chainHead = mm_sac_alloc(mm_pool_color,
                           structSize,
                           MM_ALLOC_CLASS_NCOLOR);
  *colorChain = chainHead;
  if (chainHead == NULL)
    return error_handler( VMERROR );

  if ( colorspaceDimension != 0 )
    chainHead->iColorValues = (USERVALUE *) (chainHead + 1); /* 1st byte after the pLink struct */
  else
    chainHead->iColorValues = NULL ;

  chainHead->refCnt = 1;
  chainHead->structSize = structSize;
  chainHead->iColorSpace = iColorSpace;
  chainHead->chainStyle = CHAINSTYLE_COMPLETE ;
  chainHead->overprintProcess = 0u;
  chainHead->fPresepColorIsGray = FALSE;
  chainHead->fSoftMaskLuminosityChain = FALSE;
  chainHead->fCompositing = FALSE;
  chainHead->patternPaintType = NO_PATTERN;
  chainHead->inBlackType = BLACK_TYPE_NONE;
  chainHead->prevIndependentChannels = TRUE;

  Copy(object_slot_notvm(&chainHead->colorspace), colorspace);
  chainHead->n_iColorants = colorspaceDimension;
  chainHead->pattern = onull; /* Struct copy to set slot properties */
  chainHead->chainColorModel = REPRO_COLOR_MODEL_INVALID;

  chainHead->saveLevel = get_core_context()->savelevel;

  for (i = 0; i < colorspaceDimension; i++) {
    /* Initialise now, precise values will be set later */
    chainHead->iColorValues[i] = 0.0f;
  }

  chainHead->context = NULL;

  chainhead_assertions(*colorChain);

  return TRUE;
}

/* ---------------------------------------------------------------------- */

Bool cc_initChains(GS_COLORinfo *colorInfo)
{
  GS_CHAINinfo *colorChain;
  int32 nChain;
  OBJECT theo = OBJECT_NOTVM_NULL;

  for (nChain = 0; nChain < GSC_N_COLOR_TYPES; nChain++) {
    colorChain = NULL;
    if ( ! cc_createChainHead( &colorChain, SPACE_notset, 1, &theo )) {
      return FAILURE(FALSE);
    }
    colorInfo->chainInfo[ nChain ] = colorChain;
    colorInfo->constructionDepth[ nChain ] = 0;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */

static Bool cc_copyChain( GS_CHAINinfo *oldChainHead,
                          GS_CHAINinfo **newChainHead )
{
  GS_CHAINinfo    *chainHead;

  chainhead_assertions(oldChainHead);

  chainHead = mm_sac_alloc(mm_pool_color,
                           oldChainHead->structSize,
                           MM_ALLOC_CLASS_NCOLOR );
  if (chainHead == NULL)
    return error_handler( VMERROR );

  *newChainHead = chainHead;
  HqMemCpy(chainHead, oldChainHead, oldChainHead->structSize);
  chainHead->iColorValues = (USERVALUE *) (chainHead + 1); /* 1st byte after the pLink struct */

  chainHead->refCnt = 1;

  /* Since we now have a new copy of the chain head, refering to the same
   * context, we need a new claim on the context.
   */
  if ( chainHead->context != NULL ) {
    CLINK_RESERVE( chainHead->context ) ;
  }

  return TRUE;
}

static void freeChain( GS_CHAINinfo *chainHead )
{
  cc_destroyChainContext(&chainHead->context);

  mm_sac_free(mm_pool_color, chainHead, chainHead->structSize);
}

Bool cc_updateChain( GS_CHAINinfo **chainHead )
{
  chainhead_assertions(*chainHead);

  CLINK_UPDATE(GS_CHAINinfo, chainHead, cc_copyChain, freeChain);
  return TRUE;
}

void cc_destroyChain( GS_CHAINinfo **chainHead )
{
  HQASSERT(chainHead != NULL && *chainHead != NULL, "chainHead invalid");
  chainhead_assertions(*chainHead);

  CLINK_RELEASE(chainHead, freeChain);
}

/* ---------------------------------------------------------------------- */
static Bool cc_createChainContext(GS_CHAIN_CONTEXT **newContext,
                                  GS_COLORinfo     *colorInfo)
{
  GS_CHAIN_CONTEXT *chainContext;
  int i;

  HQASSERT(newContext != NULL, "newContext NULL");
  HQASSERT(*newContext == NULL, "newContext should be NULL");

  chainContext = mm_sac_alloc(mm_pool_color,
                              sizeof(GS_CHAIN_CONTEXT),
                              MM_ALLOC_CLASS_NCOLOR);
  if (chainContext == NULL)
    return error_handler( VMERROR );

  chainContext->refCnt = 1;
  chainContext->pnext = NULL;
  chainContext->pSimpleLink = NULL;
  chainContext->pCurrentCMYK = NULL;
  chainContext->pCurrentRGB = NULL;
  chainContext->pCurrentGray = NULL;

  chainContext->pCache = NULL;
  chainContext->devCodeLut = NULL;
  chainContext->tomsTable = NULL;
  chainContext->cacheFlags = colorInfo->params.enableColorCache;

  chainContext->blockTmpArray = NULL ;
  chainContext->blockTmpArraySize = 0 ;

  for (i = 0; i < MAX_CSA_LENGTH; i++) {
    chainContext->finalDevicenCSAmain[i] = onothing;  /* Struct copy to set slot properties */
    chainContext->finalDevicenCSAsmp[i] = onothing;   /* Struct copy to set slot properties */
  }
  chainContext->illegalTintTransform = onull;  /* Struct copy to set slot properties */

  chainContext->blockOverprint = NULL ;

  chainContext->fIntercepting = FALSE;
  chainContext->fApplyMaxBlts = FALSE;
  chainContext->blackPosition = -1;

  *newContext = chainContext;

  chainContext_assertions(chainContext);

  return TRUE;
}

Bool cc_createChainContextIfNecessary(GS_CHAIN_CONTEXT **newContext,
                                      GS_COLORinfo     *colorInfo)
{
  HQASSERT(newContext != NULL, "newContext NULL");

  if (*newContext == NULL) {
    if (!cc_createChainContext(newContext, colorInfo))
      return FALSE;
  }

  return TRUE;
}

void cc_invalidateTransformChains(GS_CHAIN_CONTEXT *chainContext)
{
  if (chainContext != NULL) {
    if (chainContext->pCurrentCMYK != NULL) {
      cc_metrics_increment_destructs();
      cc_destroyLinks(chainContext->pCurrentCMYK);
      chainContext->pCurrentCMYK = NULL;
    }
    if (chainContext->pCurrentRGB != NULL) {
      cc_metrics_increment_destructs();
      cc_destroyLinks(chainContext->pCurrentRGB);
      chainContext->pCurrentRGB = NULL;
    }
    if (chainContext->pCurrentGray != NULL) {
      cc_metrics_increment_destructs();
      cc_destroyLinks(chainContext->pCurrentGray);
      chainContext->pCurrentGray = NULL;
    }
  }
}

static void freeChainContext(GS_CHAIN_CONTEXT *chainContext)
{
  if (chainContext != NULL) {
    if (chainContext->pnext != NULL) {
      cc_metrics_increment_destructs();
      cc_destroyLinks(chainContext->pnext);
      chainContext->pnext = NULL;
    }
    if (chainContext->pSimpleLink != NULL) {
      cc_metrics_increment_destructs();
      cc_destroyLinks(chainContext->pSimpleLink);
      chainContext->pSimpleLink = NULL;
    }
    cc_invalidateTransformChains(chainContext);
    if (chainContext->pCache != NULL) {
      coc_release(chainContext->pCache);
      chainContext->pCache = NULL;
    }
    if (chainContext->devCodeLut != NULL) {
      cc_fastrgb2gray_freelut(chainContext->devCodeLut);
      chainContext->devCodeLut = NULL;
    }
    if (chainContext->tomsTable != NULL) {
      cc_destroyTomsTable(chainContext->tomsTable);
      chainContext->tomsTable = NULL;
    }
    if (chainContext->blockTmpArray != NULL) {
      mm_free(mm_pool_color,
              chainContext->blockTmpArray,
              chainContext->blockTmpArraySize);
      chainContext->blockTmpArray = NULL;
      chainContext->blockTmpArraySize = 0;
    }
    cc_destroyblockoverprints(&chainContext->blockOverprint);

    if (oType(chainContext->finalDevicenCSAmain[1]) == OARRAY) {
      int i;
      mm_free_with_header(mm_pool_color, oArray(chainContext->finalDevicenCSAmain[1]));
      for (i = 0; i < MAX_CSA_LENGTH; i++)
        chainContext->finalDevicenCSAmain[i] = onothing;  /* Struct copy to set slot properties */
    }
    if (oType(chainContext->finalDevicenCSAsmp[1]) == OARRAY) {
      int i;
      mm_free_with_header(mm_pool_color, oArray(chainContext->finalDevicenCSAsmp[1]));
      for (i = 0; i < MAX_CSA_LENGTH; i++)
        chainContext->finalDevicenCSAsmp[i] = onothing;  /* Struct copy to set slot properties */
    }
  }
  mm_sac_free(mm_pool_color, chainContext, sizeof(GS_CHAIN_CONTEXT));
}

void cc_destroyChainContext(GS_CHAIN_CONTEXT **oldContext)
{
  if ( *oldContext != NULL )
    CLINK_RELEASE(oldContext, freeChainContext);
}

/* ---------------------------------------------------------------------- */
Bool cc_arechainobjectslocal(corecontext_t *corecontext,
                             GS_COLORinfo *colorInfo, int32 colorType )
{
  GS_CHAINinfo *chainInfo;

  chainInfo = colorInfo->chainInfo[ colorType ] ;
  if ( chainInfo == NULL )
    return FALSE ;

  /* This routine only cares about if the top level OBJECT is in
   * local or global memory. Since this is stored in the color chain
   * head object that's all we need check.
   */
  if ( illegalLocalIntoGlobal(&chainInfo->colorspace, corecontext) )
    return TRUE ;

  if ( (colorType == GSC_FILL || colorType == GSC_STROKE) &&
       illegalLocalIntoGlobal(&chainInfo->pattern, corecontext) )
    return TRUE ;

  return FALSE ;
}

/* ---------------------------------------------------------------------- */
/* gsc_scan_chain - scan the given chain
 *
 * This should match gsc_arechainobjectslocal, since both need look at
 * all the VM pointers. */
mps_res_t cc_scan_chain( mps_ss_t ss, GS_COLORinfo *colorInfo, int32 colorType )
{
  mps_res_t res;
  GS_CHAINinfo *chainInfo;

  chainInfo = colorInfo->chainInfo[ colorType ];
  if ( chainInfo == NULL )
    return MPS_RES_OK;

  res = ps_scan_field( ss, &chainInfo->colorspace );
  if ( res != MPS_RES_OK ) return res;
  if ( colorType == GSC_FILL || colorType == GSC_STROKE )
    res = ps_scan_field( ss, &chainInfo->pattern );
  return res;
}

/* ---------------------------------------------------------------------- */

Bool cc_sameColorSpaceObject(GS_COLORinfo   *colorInfo,
                             OBJECT         *colorSpace_1,
                             COLORSPACE_ID  colorspaceId_1,
                             OBJECT         *colorSpace_2,
                             COLORSPACE_ID  colorspaceId_2)
{
  Bool fSameColorSpace = FALSE;
  ICC_PROFILE_INFO *icc_1;
  ICC_PROFILE_INFO *icc_2;
  int32 dummyDims;
  COLORSPACE_ID dummyDS;
  COLORSPACE_ID dummyPCS;

  if ( colorspaceId_1 == colorspaceId_2 ) {
    switch ( colorspaceId_1 ) {
    case SPACE_DeviceGray:
    case SPACE_DeviceRGB:
    case SPACE_DeviceCMY:
    case SPACE_DeviceCMYK:
      fSameColorSpace = TRUE ;
      break;
    case SPACE_DeviceN:
    case SPACE_Separation:
    case SPACE_Indexed:
      /* Since PS jobs often poke changes into color spaces, especially jobs
       * from older applications poking directly into Separation CSAs,
       * i.e. clients must be sure that it is safe in that color space objects
       * have not been unexpectedly changed by the job under its feet.
       */
     if (compare_objects(colorSpace_1, colorSpace_2))
        fSameColorSpace = TRUE ;
      break;
    case SPACE_ICCBased:
      /* It is necessary to treat ICCBased specially because PDF code makes the
       * optional dictionary 3rd element unsafe because it can disappear under
       * the cache's feet when it is the base space of a Pattern, Indexed,
       * Separation, or DeviceN. Just the file in the 2nd element is sufficient.
       */
      HQASSERT(oType(*colorSpace_1) == OARRAY && oType(*colorSpace_2) == OARRAY &&
               theLen(*colorSpace_1) >= 2 && theLen(*colorSpace_2) >= 2,
               "Corrupted ICCBased color space");

      if (!cc_get_iccbased_profile_info(colorInfo, colorSpace_1, &icc_1,
                                        &dummyDims, &dummyDS, &dummyPCS) ||
          !cc_get_iccbased_profile_info(colorInfo, colorSpace_2, &icc_2,
                                        &dummyDims, &dummyDS, &dummyPCS)) {
        /* Both ICCBased color spaces will have been processed before now */
        HQFAIL("Corrupted ICCBased space");
        return fSameColorSpace;
      }

      fSameColorSpace = (icc_1 == icc_2);
      break;
    default:
      /* Other color spaces could potentially be compared, but is probably not
       * worth the effort because of their complexity and unlikeliness of a
       * positive answer being useful to a client.
       */
      break;
    }
  }

  return fSameColorSpace;
}

Bool gsc_sameColorSpaceObject(GS_COLORinfo *colorInfo,
                              OBJECT       *colorSpace_1,
                              OBJECT       *colorSpace_2)
{
  COLORSPACE_ID colorspaceId_1;
  COLORSPACE_ID colorspaceId_2;

  if (!gsc_getcolorspacetype(colorSpace_1, &colorspaceId_1) ||
      !gsc_getcolorspacetype(colorSpace_2, &colorspaceId_2))
    return FALSE;

  return cc_sameColorSpaceObject(colorInfo,
                                 colorSpace_1, colorspaceId_1,
                                 colorSpace_2, colorspaceId_2);
}

/* ---------------------------------------------------------------------- */
Bool gsc_setcolorspace( GS_COLORinfo *colorInfo, STACK *stack, int32 colorType )
{
  int32 stacksize ;
  OBJECT *theo ;
  COLORSPACE_ID colorspace_id;
  int32 colorspacedimension;

  COLORTYPE_ASSERT(colorType, "gsc_setcolorspace") ;

  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = stackindex( 0 , stack ) ;

  /* Will need to determine whether the colorspace has changed and what its dimension is.
   */
  if ( ! gsc_getcolorspacesizeandtype( colorInfo, theo,
                                       &colorspace_id, &colorspacedimension ) )
    return FALSE;

  if ( ! cc_updateChainForNewColorSpace( colorInfo ,
                                         colorType ,
                                         colorspace_id ,
                                         colorspacedimension ,
                                         theo ,
                                         CHAINSTYLE_COMPLETE,
                                         FALSE /* fCompositing */))
    return FALSE ;

  pop( stack ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Also see export version gsc_setcolorspacedirect. */
static Bool setcolorspacedirect( GS_COLORinfo   *colorInfo ,
                                 int32          colorType ,
                                 COLORSPACE_ID  colorspace_id ,
                                 Bool           fCompositing)
{
  int32 colorspacedimension ;
  OBJECT theo = OBJECT_NOTVM_NULL ;

  COLORTYPE_ASSERT(colorType, "setcolorspacedirect") ;

  switch ( colorspace_id ) {
  case SPACE_DeviceGray:
    colorspacedimension = 1;
    break;
  case SPACE_Lab:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
    colorspacedimension = 3;
    break;
  case SPACE_DeviceCMYK:
    colorspacedimension = 4;
    break;
  default:
    HQFAIL( "setcolorspacedirect called with unsupported colorspace" );
    return error_handler( UNREGISTERED ) ;
  }

  return cc_updateChainForNewColorSpace( colorInfo ,
                                         colorType ,
                                         colorspace_id ,
                                         colorspacedimension ,
                                         &theo ,
                                         CHAINSTYLE_COMPLETE,
                                         fCompositing) ;
}

/* ---------------------------------------------------------------------- */
Bool gsc_setcolorspacedirect( GS_COLORinfo  *colorInfo ,
                              int32         colorType ,
                              COLORSPACE_ID colorspace_id )
{
  return setcolorspacedirect(colorInfo, colorType, colorspace_id,
                             FALSE /* fCompositing */);
}

/* ---------------------------------------------------------------------- */
Bool gsc_setcolorspacedirectforcompositing( GS_COLORinfo  *colorInfo,
                                            int32         colorType,
                                            COLORSPACE_ID colorspace_id)
{
  return setcolorspacedirect(colorInfo, colorType, colorspace_id,
                             TRUE /* fCompositing */);
}

/* ---------------------------------------------------------------------- */
Bool gsc_setcustomcolorspacedirect( GS_COLORinfo  *colorInfo ,
                                    int32         colorType ,
                                    OBJECT        *colorSpaceObj,
                                    Bool          fCompositing )
{
  COLORSPACE_ID colorspace_id;
  int32 colorspacedimension;

  COLORTYPE_ASSERT(colorType, "gsc_setcustomcolorspacedirect") ;

  if (!gsc_getcolorspacesizeandtype(colorInfo, colorSpaceObj,
                                    &colorspace_id,
                                    &colorspacedimension ))
    HQASSERT(colorspace_id == SPACE_DeviceN || colorspace_id == SPACE_Separation,
             "gsc_setcustomcolorspacedirect: called with unsupported colorspace");

  return cc_updateChainForNewColorSpace( colorInfo ,
                                         colorType ,
                                         colorspace_id ,
                                         colorspacedimension ,
                                         colorSpaceObj ,
                                         CHAINSTYLE_COMPLETE,
                                         fCompositing) ;
}

/* -------------------------------------------------------------------------- */
Bool cc_updateChainForNewColorSpace(GS_COLORinfo  *colorInfo ,
                                    int32         colorType ,
                                    COLORSPACE_ID colorspace_id ,
                                    int32         colorspacedimension ,
                                    OBJECT        *theo ,
                                    CHAINSTYLE    chainStyle,
                                    Bool          fCompositing)
{
  GS_CHAINinfo *newColorChain ;
  GS_CHAINinfo *oldColorChain = NULL;
  Bool mustTidyGstate = FALSE;
  Bool status;
  GS_CHAINinfo *colorChain;
  Bool constructNewChain;

  HQASSERT( colorInfo != NULL , "colorInfo NULL in cc_updateChainForNewColorSpace" ) ;
  HQASSERT( theo != NULL , "theo NULL in cc_updateChainForNewColorSpace" ) ;

  /* During setcolorspace, it is only safe to directly reuse the current color
   * chain for device spaces. Although it should be safe to compare the CSA for
   * other color spaces, it isn't always possible because some applications
   * directly poke new objects into the current CSA before calling setcolorspace.
   * We rely on the color chain cache for performance for non-device spaces.
   */
  switch (colorspace_id) {
  case SPACE_notset:
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceGray:
    constructNewChain = (colorspace_id != colorInfo->chainInfo[colorType]->iColorSpace);
    break;
  default:
    constructNewChain = TRUE;
    break;
  }

  if ( !constructNewChain ) {
    /* If the old chain doesn't have the required values of chainStyle and
     * fCompositing, then force the lazy building of a new chain because these
     * properties affect chain construction.
     * We don't need to test fPresepColorIsGray here since it is dealt with
     * later. Nor do we test volatile data that is set when the chain
     * is rebuilt.
     * Note that we don't want to force the building of a chain because the
     * client may not want to (esp. for patterns).
     */
    if (cc_updateChain( &colorInfo->chainInfo[ colorType ] )) {
      GS_CHAINinfo *updatedColorChain = colorInfo->chainInfo[ colorType ] ;

      HQASSERT( updatedColorChain != NULL ,
                "updatedColorChain NULL in cc_updateChainForNewColorSpace" ) ;

      if (!cc_createChainContextIfNecessary(&updatedColorChain->context,
                                            colorInfo))
        return FALSE;

      if (updatedColorChain->chainStyle != chainStyle ) {
        if (!cc_invalidateChain(colorInfo, colorType, FALSE))
          return FALSE;
        updatedColorChain->chainStyle = chainStyle ;
      }
      if (! cc_setCompositing( colorInfo, colorType, fCompositing ))
        return FALSE;
    }
    else
      return FALSE;
  }
  else {
    oldColorChain = colorInfo->chainInfo[ colorType ] ;

    if (!cc_addChainCacheEntry(colorInfo, colorType, FALSE, TRUE))
      return FALSE;

    /* We are now going to create a new colorchain which, if
     * successful, will replace the existing one.
     */
    newColorChain = NULL;
    if ( ! cc_createChainHead( &newColorChain,
                               colorspace_id,
                               colorspacedimension,
                               theo ))
      return FALSE ;

    if (!cc_createChainContextIfNecessary(&newColorChain->context, colorInfo))
      return FALSE;

    newColorChain->chainStyle = chainStyle;
    newColorChain->fCompositing = fCompositing;

    /* Now (possibly temporarily) put newColorChain in the gstate. We have a
     * few more things to put into the gstate, then we'll build the chain and
     * finalise the gstate.
     */
    colorInfo->chainInfo[ colorType ] = newColorChain ;
    mustTidyGstate = TRUE;
  }

  HQASSERT(CLINK_OWNER(colorInfo->chainInfo[colorType]), "Can't change shared chain head");

  colorChain = colorInfo->chainInfo[ colorType ];

  colorChain->overprintProcess = 0u ;

  status = cc_findColorModel(colorInfo,
                             gsc_getcolorspace(colorInfo, colorType),
                             gsc_getcolorspaceobject(colorInfo, colorType),
                             fCompositing,
                             &colorChain->chainColorModel) &&
           cc_setColorModel(colorInfo, colorType, colorChain->chainColorModel) &&
           init_colorValues(colorInfo, colorType);

  /* Then destroy the old or new chain depending on success.
   * NB. The gstate may not contain newColorChain after construction.
   */
  if (mustTidyGstate) {
    if ( status && gsc_constructChain( colorInfo, colorType )) {
      cc_destroyChain( &oldColorChain ) ;
    }
    else {
      /* We are normally destroying newColorChain but this may not be the case if
       * an error occurs in a recursive callout during chain construction */
      cc_destroyChain( &colorInfo->chainInfo[ colorType ] ) ;
      colorInfo->chainInfo[ colorType ] = oldColorChain ;
      (void) gsc_setRequiredReproType( colorInfo, colorType, REPRO_TYPE_OTHER ) ;
      return FALSE ;
    }
  }

  return status;
}

/* -------------------------------------------------------------------------- */
static Bool init_colorValues( GS_COLORinfo *colorInfo , int32 colorType )
{
  int32         i;
  SYSTEMVALUE   range[2];
  COLORSPACE_ID colorSpace;
  GS_CHAINinfo *colorChain;
  Bool          fPresepColorIsGray;

  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT(colorChain != NULL, "colorChain NULL") ;

  colorSpace = colorChain->iColorSpace;

  /* Get the precise color initialisation right on a colorspace basis */

  switch (colorSpace) {
  case SPACE_DeviceGray:
  case SPACE_CalGray:
    HQASSERT( colorChain->n_iColorants == 1 , "wrong number of colorants" ) ;
    colorChain->iColorValues[0] = 0.0f;
    fPresepColorIsGray = FALSE ;
    if ( rcbn_intercepting() )
      fPresepColorIsGray = TRUE ;
    if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
      return FALSE;
    break;
  case SPACE_DeviceRGB:
  case SPACE_CalRGB:
    HQASSERT( colorChain->n_iColorants == 3 , "wrong number of colorants" ) ;
    colorChain->iColorValues[0] = 0.0f;
    colorChain->iColorValues[1] = 0.0f;
    colorChain->iColorValues[2] = 0.0f;
    fPresepColorIsGray = FALSE ;
    if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) )
      fPresepColorIsGray = IS_GRAY_RGB( colorChain->iColorValues ) ;
    if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
      return FALSE;
    break;
  case SPACE_DeviceCMY:
    HQASSERT( colorChain->n_iColorants == 3 , "wrong number of colorants" ) ;
    colorChain->iColorValues[0] = 0.0f;
    colorChain->iColorValues[1] = 0.0f;
    colorChain->iColorValues[2] = 0.0f;
    colorChain->fPresepColorIsGray = FALSE ;
    break;
  case SPACE_DeviceCMYK:
    HQASSERT( colorChain->n_iColorants == 4 , "wrong number of colorants" ) ;
    colorChain->iColorValues[0] = 0.0f;
    colorChain->iColorValues[1] = 0.0f;
    colorChain->iColorValues[2] = 0.0f;
    colorChain->iColorValues[3] = 1.0f;
    fPresepColorIsGray = FALSE ;
    if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) )
      fPresepColorIsGray = IS_BLACKONLY_CMYK( colorChain->iColorValues ) ;
    if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
      return FALSE;
    break;
  case SPACE_Separation:
    HQASSERT( colorChain->n_iColorants == 1 , "wrong number of colorants" ) ;
    colorChain->iColorValues[0] = 1.0f;
    fPresepColorIsGray = FALSE ;
    if ( rcbn_intercepting() ) {
      if ( ! spaceIsJustBlack( colorInfo,
                               &colorChain->colorspace ,
                               &fPresepColorIsGray ))
        return FALSE ;
    }
    if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
      return FALSE;
    break;
  case SPACE_DeviceN:
    for (i = 0; i < colorChain->n_iColorants; i++)
      colorChain->iColorValues[i] = 1.0f;
    fPresepColorIsGray = FALSE ;
    if ( rcbn_intercepting() ) {
      if ( ! spaceIsJustBlack( colorInfo,
                               &colorChain->colorspace,
                               &fPresepColorIsGray ))
        return FALSE ;
    }
    if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
      return FALSE;
    break;
  case SPACE_CMM:
    HQFAIL("CMM colorspace NYI");
    break;
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
  case SPACE_Lab:
  case SPACE_ICCBased:
    {
      CLINK  *cieLink;

      /* Since we will need range from the colorchain we must first of all
       * construct the colorchain.
       */
      if ( ! gsc_constructChain( colorInfo, colorType ))
        return FALSE ;
      colorChain = colorInfo->chainInfo[ colorType ] ;
      HQASSERT(colorChain != NULL && colorChain->context != NULL, "colorChain NULL");

      cieLink = colorChain->context->pnext;
      HQASSERT(cieLink != NULL, "cieLink NULL");

      for (i = 0; i < colorChain->n_iColorants; i++) {
        /* Use 0 or the closest value allowed by the range */
        colorChain->iColorValues[i] = 0.0f ;
        cc_getCieRange(cieLink, i, range);
        if (range[0] > 0)
          colorChain->iColorValues[i] = (USERVALUE) range[0];
        else
        if (range[1] < 0)
          colorChain->iColorValues[i] = (USERVALUE) range[1];
      }
    }
    colorChain->fPresepColorIsGray = FALSE;
    break;
  case SPACE_Indexed:
    HQASSERT( colorChain->n_iColorants == 1 , "wrong number of colorants" ) ;
    colorChain->iColorValues[0] = 0.0f; /* aka Index 0 */
    colorChain->fPresepColorIsGray = FALSE;
    break;
  case SPACE_Pattern:
    theTags(colorChain->pattern) = ONULL ;
    colorChain->patternPaintType = NO_PATTERN ; /* unknown as yet */
    break ;
  default:
    for (i = 0; i < colorChain->n_iColorants; i++)
      colorChain->iColorValues[i] = 0.0f;
    colorChain->fPresepColorIsGray = FALSE;
    break;
  }
  return TRUE ;
}

COLORSPACE_ID gsc_getcolorspace( GS_COLORinfo *colorInfo, int32 colorType )
{
  HQASSERT( colorInfo , "colorInfo NULL in gsc_getcolorspace" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getcolorspace") ;
  HQASSERT( colorInfo->chainInfo[ colorType ] , "GS_CHAINinfo NULL in gsc_getcolorspace" ) ;

  return colorInfo->chainInfo[ colorType ]->iColorSpace ;
}

static Bool getcolorspace( OBJECT *colorSpace, COLORSPACE_ID *piColorSpace )
{
  HQASSERT( colorSpace , "colorSpace NULL" ) ;

  if (!gsc_getcolorspacetype(colorSpace,
                             piColorSpace))
    return FALSE ;

  return TRUE ;
}

Bool gsc_getbasecolorspace( GS_COLORinfo *colorInfo, int32 colorType , COLORSPACE_ID *piColorSpace )
{
  OBJECT *baseColorSpace;

  HQASSERT( colorInfo , "colorInfo NULL" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getbasecolorspace") ;

  baseColorSpace = gsc_getbasecolorspaceobject(colorInfo, colorType);

  if (!getcolorspace(baseColorSpace, piColorSpace ))
    return FALSE ;

  return TRUE ;
}

OBJECT *gsc_getcolorspaceobject( GS_COLORinfo *colorInfo , int32 colorType )
{
  HQASSERT( colorInfo , "colorInfo NULL in gsc_getcolorspaceobject" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getcolorspaceobject") ;
  HQASSERT( colorInfo->chainInfo[ colorType ] , "GS_CHAINinfo NULL in gsc_getcolorspaceobject" ) ;

  return &colorInfo->chainInfo[ colorType ]->colorspace ;
}

/* For use in indexed colorspaces - return the object for the base colorspace.
 * This could be a simple name or a composite object depending on the base space.
 */
OBJECT *cc_getbasecolorspaceobject( OBJECT *colorSpace )
{
  HQASSERT( colorSpace , "colorSpace NULL" ) ;

  HQASSERT(oType(*colorSpace) == OARRAY &&
           oName(oArray(*colorSpace)[0]) == &system_names[NAME_Indexed] &&
           theLen(*colorSpace) == space_arraysize[SPACE_Indexed],
           "gsc_getbasecolorspaceobject called when colorspace wasn't SPACE_Indexed" ) ;

  /* The second element in an indexed colorspace array is the base colorspace.
   * This could be a simple name (e.g. DeviceRGB), or could be a composite
   * object (e.g. when the base space is DeviceN).
   */
  return & oArray(*colorSpace)[1];
}

OBJECT *gsc_getbasecolorspaceobject( GS_COLORinfo *colorInfo , int32 colorType )
{
  OBJECT* colorSpace;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_getbasecolorspaceobject" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getbasecolorspaceobject") ;

  colorSpace = gsc_getcolorspaceobject( colorInfo , colorType ) ;

  return cc_getbasecolorspaceobject(colorSpace);
}

/* ---------------------------------------------------------------------- */
/* Given a colorspaceid we return a PS array describing that color space
 * by looking up the colorspace name in internaldict
 * ie. SPACE_DeviceGray => NAME_DeviceGray => [/DeviceGray]
 */
Bool gsc_getInternalColorSpace( COLORSPACE_ID colorSpaceId, OBJECT **colorSpaceObj )
{
  OBJECT *theo ;

  HQASSERT( colorSpaceObj, "colorSpaceObj parameter to getcolorspace is NULL" ) ;
  *colorSpaceObj = NULL;

  /* There isn't really a useful colorspace in force when in the PaintProc
   * for an uncolored pattern but Adobe returns DeviceGray and so do we.
   */
  if ( colorSpaceId == SPACE_PatternMask )
    colorSpaceId = SPACE_DeviceGray ;

  theo = fast_extract_hash_name(&internaldict, space_names[colorSpaceId]) ;
  if ( ! theo )
    return error_handler( UNREGISTERED ) ;

  *colorSpaceObj = theo ;
  return TRUE ;
}

Bool gsc_currentcolorspace( GS_COLORinfo *colorInfo, int32 colorType, OBJECT *colorSpace )
{
  OBJECT *theo ;
  GS_CHAINinfo *colorChain;

  HQASSERT( colorInfo, "colorInfo parameter to gsc_currentcolorspace is NULL" ) ;
  HQASSERT( colorSpace, "colorSpace parameter to gsc_currentcolorspace is NULL" ) ;

  colorChain = colorInfo->chainInfo[colorType] ;
  HQASSERT( colorChain, "colorChain parameter to gsc_currentcolorspace is NULL" ) ;

  if ( oType(colorChain->colorspace) != OARRAY &&
       oType(colorChain->colorspace) != OPACKEDARRAY ) {
    /* Assume that the structure [/DeviceGray] of /DeviceGray etc in
     * internaldict is correct.
     */
    if ( ! gsc_getInternalColorSpace( colorChain->iColorSpace , & theo ))
      return FALSE ;
  }
  else
    theo = &colorChain->colorspace;

  Copy( colorSpace, theo ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool gsc_setgray( GS_COLORinfo *colorInfo, STACK *stack, int32 colorType )
{
  int32 stacksize ;

  OBJECT *theo ;
  USERVALUE gray[ 1 ] ;
  GS_CHAINinfo *chainInfo ;

  COLORTYPE_ASSERT(colorType, "gsc_setgray") ;

  /* Expect one number in the range 0..1: limits to that range. */
  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = stackindex( 0 , stack ) ;
  if ( !object_get_real(theo, &gray[0]) )
    return FALSE ;

  NARROW_01( gray[ 0 ] ) ;

  chainInfo = colorInfo->chainInfo[ colorType ] ;
  if ( chainInfo->iColorSpace != SPACE_DeviceGray ) {
    if ( ! gsc_setcolorspacedirect( colorInfo , colorType , SPACE_DeviceGray ))
      return FALSE ;
  }

  if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
    return FALSE ;
  chainInfo = colorInfo->chainInfo[ colorType ] ;

  chainInfo->iColorValues[ 0 ] = gray[ 0 ];
  if (! cc_setCompositing( colorInfo, colorType, FALSE ))
    return FALSE;

  pop( stack ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool gsc_sethsbcolor( GS_COLORinfo *colorInfo, STACK *stack, int32 colorType )
{
  int32 i ;
  int32 stacksize ;
  USERVALUE rgb[ 3 ] = {0,0,0} ;
  USERVALUE hsv[ 3 ] ;
  GS_CHAINinfo *chainInfo ;
  Bool fPresepColorIsGray = FALSE ;

  COLORTYPE_ASSERT(colorType, "gsc_sethsbcolor") ;

  /* Expect three numbers in the range 0..1: limits to that range. */
  stacksize = theStackSize( operandstack ) ;
  if ( stacksize < 2 )
    return error_handler( STACKUNDERFLOW ) ;
  for ( i = 0 ; i < 3 ; i++ ) {
    OBJECT *theo ;
    USERVALUE arg ;

    theo = stackindex( i , stack ) ;
    if ( !object_get_real(theo, &arg) )
      return FALSE ;

    hsv[ 2 - i ] = arg ;
  }

  hsv_to_rgb( hsv , rgb ) ;

  if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) )
    fPresepColorIsGray = IS_GRAY_RGB( rgb ) ;

  chainInfo = colorInfo->chainInfo[ colorType ] ;
  if ( chainInfo->iColorSpace != SPACE_DeviceRGB ) {
    if ( ! gsc_setcolorspacedirect( colorInfo , colorType , SPACE_DeviceRGB ))
       return FALSE ;
  }

  if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
    return FALSE ;
  chainInfo = colorInfo->chainInfo[ colorType ] ;

  chainInfo->iColorValues[ 0 ] = rgb[ 0 ];
  chainInfo->iColorValues[ 1 ] = rgb[ 1 ];
  chainInfo->iColorValues[ 2 ] = rgb[ 2 ];

  if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
    return FALSE;
  if (! cc_setCompositing( colorInfo, colorType, FALSE ))
    return FALSE;

  npop( 3 , & operandstack ) ;

  return TRUE ;
}

/*------------------------------------------------------------------------*/
Bool gsc_setrgbcolor( GS_COLORinfo *colorInfo, STACK *stack, int32 colorType )
{
  int32 i ;
  int32 stacksize ;
  USERVALUE rgb[ 3 ] ;
  GS_CHAINinfo *chainInfo ;
  Bool fPresepColorIsGray = FALSE ;

  COLORTYPE_ASSERT(colorType, "gsc_setrgbcolor") ;

  /* Expect three numbers in the range 0..1: limits to that range. */
  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  for ( i = 0 ; i < 3 ; i++ ) {
    OBJECT *theo ;
    USERVALUE arg ;

    theo = stackindex( i , stack ) ;
    if ( !object_get_real(theo, &arg) )
      return FALSE ;

    NARROW_01( arg ) ;
    rgb[ 2 - i ] = arg ;
  }

  if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) )
    fPresepColorIsGray = IS_GRAY_RGB( rgb ) ;

  chainInfo = colorInfo->chainInfo[ colorType ] ;
  if ( chainInfo->iColorSpace != SPACE_DeviceRGB ) {
    if ( ! gsc_setcolorspacedirect( colorInfo , colorType , SPACE_DeviceRGB ))
       return FALSE ;
  }

  if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
    return FALSE ;
  chainInfo = colorInfo->chainInfo[ colorType ] ;

  chainInfo->iColorValues[ 0 ] = rgb[ 0 ];
  chainInfo->iColorValues[ 1 ] = rgb[ 1 ];
  chainInfo->iColorValues[ 2 ] = rgb[ 2 ];

  if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
    return FALSE;
  if (! cc_setCompositing( colorInfo, colorType, FALSE ))
    return FALSE;

  npop( 3 , stack ) ;

  return TRUE ;
}

/*------------------------------------------------------------------------*/
Bool gsc_setcmykcolor( GS_COLORinfo *colorInfo, STACK *stack, int32 colorType )
{
  int32 i ;
  int32 stacksize ;
  USERVALUE cmyk[ 4 ] ;
  uint8 overprintProcess ;
  GS_CHAINinfo *chainInfo ;
  Bool fPresepColorIsGray = FALSE ;
  Bool implicitOverprinting ;

  COLORTYPE_ASSERT(colorType, "gsc_setcmykcolor") ;

  /* Expect four numbers in the range 0..1: limits to that range. */
  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 3 )
    return error_handler( STACKUNDERFLOW ) ;

  implicitOverprinting = gsc_getoverprintmode(colorInfo) ;
  overprintProcess = 0 ;

  for ( i = 0 ; i < 4 ; i++ ) {
    OBJECT *theo ;
    USERVALUE arg ;

    theo = stackindex( i , stack ) ;
    if ( !object_get_real(theo, &arg) )
      return FALSE ;

    if ( !implicitOverprinting && (arg == -1.0f) )
      overprintProcess |= 1 << (3 - i) ;
    NARROW_01( arg ) ;
    cmyk[ 3 - i ] = arg ;
  }

  if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) )
    fPresepColorIsGray = IS_BLACKONLY_CMYK( cmyk ) ;

  chainInfo = colorInfo->chainInfo[ colorType ] ;
  if ( chainInfo->iColorSpace != SPACE_DeviceCMYK ) {
    if ( ! gsc_setcolorspacedirect( colorInfo , colorType , SPACE_DeviceCMYK ))
      return FALSE ;
  }

  if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
    return FALSE ;
  chainInfo = colorInfo->chainInfo[ colorType ] ;

  chainInfo->iColorValues[ 0 ] = cmyk[ 0 ];
  chainInfo->iColorValues[ 1 ] = cmyk[ 1 ];
  chainInfo->iColorValues[ 2 ] = cmyk[ 2 ];
  chainInfo->iColorValues[ 3 ] = cmyk[ 3 ];

  if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
    return FALSE;
  if (! cc_setCompositing( colorInfo, colorType, FALSE ))
    return FALSE;
  chainInfo->overprintProcess = overprintProcess ;

  npop( 4 , stack ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Utility function which inspects a colorspace for either /Separation Black
 * or /DeviceN [ Black ] so we can special-case these for recombine.
 */

static Bool spaceIsJustBlack( GS_COLORinfo  *colorInfo,
                              OBJECT        *colorspace,
                              Bool          *isJustBlack )
{
  COLORSPACE_ID colorspace_id ;
  int32 colorspacedimension ;
  OBJECT *theo = NULL ;

  HQASSERT( colorspace , "colorspace NULL." ) ;
  HQASSERT( isJustBlack , "isJustBlack NULL." ) ;

  *isJustBlack = FALSE ;

  /* This can happen with CHAINSTYLE_HALFTONEONLY chains. */

  if ( oType(*colorspace) == ONULL )
    return TRUE ;

  if ( ! gsc_getcolorspacesizeandtype( colorInfo, colorspace,
                                       &colorspace_id,
                                       &colorspacedimension ))
    return FALSE ;

  if ( colorspacedimension == 1 ) {
    switch ( colorspace_id ) {
      case SPACE_DeviceN:
        theo = oArray(*colorspace) + 1 ;
        if ( (oType(*theo) == OARRAY ||
              oType(*theo) == OPACKEDARRAY) &&
             theLen(*theo) == 1 ) {
          theo = oArray(*theo) ;
        }
        else
          theo = NULL ;
        break ;

      case SPACE_Separation:
        theo = oArray(*colorspace) + 1 ;
        break ;

      default :
        break ;
    }
  }

  /* By now theo is either NULL (nothing to check) or a pointer to an
   * object which we'll now check to see if it says 'Black'.
   */

  if ( theo ) {
    NAMECACHE *ncache = NULL ;

    if ( oType(*theo) == OSTRING )
      ncache = cachename(oString(*theo),theLen(*theo)) ;
    else if ( oType(*theo) == ONAME )
      ncache = oName(*theo) ;

    if ( ncache ) {
      if ( theINameNumber( ncache ) == NAME_Black ) {
        *isJustBlack = TRUE ;
      } else if ( theINLen( ncache ) == 0 ) {
        /* Cope with a bug in InDesign which produces zero length names when
           outputting to a composite device that can handle separation spaces.
           If the alternate space is DeviceGray then assume a separation name
           of Black was intended. */
        colorspace = oArray(*colorspace) + 2 ;

        if ( oType(*colorspace) == ONULL )
          return TRUE ;

        if ( ! gsc_getcolorspacesizeandtype( colorInfo, colorspace,
                                             &colorspace_id,
                                             &colorspacedimension ))
          return FALSE ;

        if ( colorspacedimension  == 1 && colorspace_id == SPACE_DeviceGray )
          *isJustBlack = TRUE ;
      }
    }
  }

  return TRUE ;
}

static NAMETYPEMATCH genericpatterndictmatch[] = {
  /* Use the enum below to index these. */
  { NAME_PatternType,                1, { OINTEGER }},
  { NAME_XUID | OOPTIONAL,           1, { OARRAY }},
  { NAME_PaintType | OOPTIONAL,      1, { OINTEGER }},
  { NAME_Implementation | OOPTIONAL, 0 },
  DUMMY_END_MATCH
};
enum {
  gpdmatch_PatternType,
  gpdmatch_XUID,
  gpdmatch_PaintType,
  gpdmatch_Implementation
} ;

/* ---------------------------------------------------------------------- */

Bool gsc_setcolor( GS_COLORinfo *colorInfo, STACK *stack , int32 colorType )
{
  /* See red book 2 page 497 and section 4.8:
   * All we do here is check and store the color value; the actual conversion
   * is performed when we do a paint operation.
   */
  int32 i ;
  int32 j ;
  int32 ndims ;
  int32 stacksize ;
  SYSTEMVALUE indexedrange[2];
  OBJECT *theo ;
  USERVALUE arg ;
  Bool status ;
  GS_CHAINinfo *colorChain ;

  COLORSPACE_ID colorspace_id;

  COLORTYPE_ASSERT(colorType, "gsc_setcolor") ;

  /* Update the relevant chain head structure */
  colorChain = colorInfo->chainInfo[ colorType ] ;

  colorspace_id = colorChain->iColorSpace ;
  switch ( colorspace_id ) {
  case SPACE_DeviceGray:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMYK:
  case SPACE_Separation:
  case SPACE_DeviceN:
    ndims = colorChain->n_iColorants ;
    stacksize = theIStackSize( stack ) ;
    if ( stacksize < (ndims - 1) )
      return error_handler( STACKUNDERFLOW ) ;
    if ( ! cc_updateChain( &colorInfo->chainInfo[ colorType ] ))
      return FALSE ;
    colorChain = colorInfo->chainInfo[ colorType ] ;

    /* Validate the color values */
    for ( i = 0 ; i < ndims ; i++ ) {
      theo = stackindex( i , stack ) ;
      if ( !object_get_real(theo, &arg) )
        return FALSE ;
    }
    /* And then set them */
    for ( i = 0 ; i < ndims ; i++ ) {
      theo = stackindex( i , stack ) ;
      (void) object_get_real(theo, &arg) ;

      NARROW_01( arg ) ;
      colorChain->iColorValues[ ndims - i - 1 ] = arg ;
    }

    npop( ndims , stack ) ;

    if ( colorspace_id == SPACE_DeviceCMYK ||
         colorspace_id == SPACE_DeviceRGB ) {
      Bool fPresepColorIsGray = FALSE ;
      if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) ) {
        if ( colorspace_id == SPACE_DeviceCMYK )
          fPresepColorIsGray = IS_BLACKONLY_CMYK( colorChain->iColorValues );
        else
          fPresepColorIsGray = IS_GRAY_RGB( colorChain->iColorValues );
      }

      if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
        return FALSE;
    }
    else if ( colorspace_id == SPACE_Separation ||
              colorspace_id == SPACE_DeviceN ) {
      Bool fPresepColorIsGray = FALSE ;
      if ( rcbn_intercepting() ) {
        if ( ! spaceIsJustBlack( colorInfo,
                                 &colorChain->colorspace ,
                                 &fPresepColorIsGray ))
          return FALSE ;
      }

      if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
        return FALSE;
    }

    break;

  case SPACE_Indexed:
    stacksize = theIStackSize( stack ) ;
    if ( stacksize < 0 )
      return error_handler( STACKUNDERFLOW ) ;

    theo = stackindex( 0 , stack ) ;
    if ( !object_get_real(theo, &arg) )
      return FALSE ;

    if ( !cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
      return FALSE ;
    colorChain = colorInfo->chainInfo[ colorType ] ;
    if ( ! gsc_range( colorInfo , colorType , 0 , indexedrange ))
      return FALSE ;
    HQASSERT( colorChain == colorInfo->chainInfo[ colorType ] ,
              "gsc_range should not invalidate colorChain" ) ;
    if ( arg < 0.0f )
      arg = 0.0f ;
    else if ( arg > indexedrange[1] ) {
      arg = ( USERVALUE )( indexedrange[1] ) ;
    }
    /* truncate: */
    arg = ( USERVALUE )(( int32 )arg ) ;

    colorChain->iColorValues[ 0 ] = arg ;

    pop( stack ) ;
    break;

  case SPACE_CMM:
    HQFAIL("CMM colorspace NYI");
    break;
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
  case SPACE_Lab:
  case SPACE_CalGray:
  case SPACE_CalRGB:
  case SPACE_ICCBased:
    ndims = colorChain->n_iColorants ;
    stacksize = theIStackSize( stack ) ;
    if ( stacksize < (ndims - 1) )
      return error_handler( STACKUNDERFLOW ) ;
    if ( ! cc_updateChain( &colorInfo->chainInfo[ colorType ] ))
      return FALSE;
    colorChain = colorInfo->chainInfo[ colorType ] ;

    /* Validate the color values */
    for ( i = 0; i < ndims; i++ ) {
      OBJECT *theo ;
      USERVALUE arg ;

      theo = stackindex( i , stack ) ;
      if ( !object_get_real(theo, &arg) )
        return FALSE ;
    }
    /* And then set them */
    for ( i = 0, j = ndims - 1 ; i < ndims ; i++, j-- ) {
      OBJECT *theo ;
      USERVALUE arg ;
      SYSTEMVALUE range[2];

      theo = stackindex( i , stack ) ;
      (void) object_get_real(theo, &arg) ;

      if (!gsc_range(colorInfo, colorType, j, range)) {
        HQASSERT(i == 0, "gsc_range shouldn't leave gstate in undefined state");
        return FALSE;
      }
      if ( arg < range[0] )
        arg = (USERVALUE)range[0];
      else if ( arg > range[1] )
        arg = (USERVALUE)range[1];
      colorChain->iColorValues[ j ] = arg ;
    }

    npop( ndims , stack ) ;
    break;

  case SPACE_Pattern:
    {
      OBJECT patterno = OBJECT_NOTVM_NOTHING , *theo ;

      /* Expect a pattern dictionary, and, if it is paint type 2, a color
       * in the base color space for the pattern, i.e. setcolor with the
       * base color type and the rest of the stack should be OK.
       */
      stacksize = theIStackSize( stack ) ;
      if ( stacksize < 0 )
        return error_handler( STACKUNDERFLOW ) ;

      theo = stackindex( 0 , stack ) ;
      Copy( & patterno , theo ) ;

      if ( oType(*theo) != ODICTIONARY )
        return error_handler( TYPECHECK ) ;

      /* Do some analysis of the pattern dictionary here */
      if ( ! dictmatch( theo , genericpatterndictmatch ))
        return error_handler( TYPECHECK ) ;

      if ( oAccess(*oDict(*theo)) != READ_ONLY ) /* makepattern makes the pattern dict read-only */
        return error_handler( TYPECHECK ) ;

      /* And check on the Implementation entry... */
      theo = genericpatterndictmatch[gpdmatch_Implementation].result ;
      if ( ! theo || ! check_pia_valid( theo ))
        return error_handler( TYPECHECK ) ;

      if ( !cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
        return FALSE ;
      colorChain = colorInfo->chainInfo[ colorType ] ;
      if (! cc_invalidateChain( colorInfo , colorType , FALSE ))
        return FALSE;
      HQASSERT( colorChain == colorInfo->chainInfo[ colorType ] ,
                "gsc_invalidateChain should not invalidate colorChain" ) ;

      pop( stack ) ;
      /* If we now get any errors we've got to restore that pop. */

      colorChain->patternPaintType = NO_PATTERN ;

      /* The PatternType object - already present and typechecked. */
      theo = genericpatterndictmatch[gpdmatch_PatternType].result ;

      switch (oInteger(*theo)) {

      case 1: /* PatternType 1: replicated pattern cell */
      case 101: /* Synonym for PatternType 1 which allows non-tiled cells */
      case 102: /* Like PatternType 1, but untiled and specifying a knockout
                   pattern group (for gradients). */

        /* The PaintType object must be present. */
        if ( (theo = genericpatterndictmatch[gpdmatch_PaintType].result) == NULL ) {
          (void)push( & patterno , stack ) ;
          return error_handler(TYPECHECK) ;
        }

        switch (oInteger(*theo)) {

        case 1:
          /* A COLORED pattern - does not require an underlying
           * color space already set up.  No extra params needed.
           * Red and White Book says "underlying...is ignored when
           * using colored patterns" so there is no checking to
           * do here.
           */
          colorChain->patternPaintType = COLOURED_PATTERN ;
          break ;

        case 2:
          /* An UNCOLORED pattern - requires an underlying
           * color space already plus parameters supplied here.
           * so set the colorspace of the head link to the colorspace
           * of the basespace and call gsc_setcolor.
           */
          if ( !cc_getpatternbasespace( colorInfo,
                                        &colorChain->colorspace,
                                        &colorspace_id,
                                        &ndims ) ) {
            (void)push( &patterno , stack ) ;
            return FALSE ;
          }

          if ( colorspace_id == SPACE_notset ) {
            (void)push( & patterno , stack ) ;
            return error_handler( RANGECHECK ) ;
          }

          /* Set the pattern paint type before potentially creating the rest
             of the chain; the gs_setcolor below may do this as a
             side-effect. */
          colorChain->patternPaintType = UNCOLOURED_PATTERN ;

          /* Lie about the color space and call setcolor recursively. */
          { OBJECT colorspace ;

            HQASSERT( colorChain->iColorSpace == SPACE_Pattern ,
                      "Pattern color space should already be set" ) ;
            colorspace = colorChain->colorspace ;

            colorChain->iColorSpace = colorspace_id ;
            colorChain->colorspace = oArray(colorspace)[ 1 ] ;

            /* This sets the colour in the chain head */
            status = gsc_setcolor( colorInfo, stack , colorType ) ;

            colorChain = colorInfo->chainInfo[ colorType ] ;
            colorChain->iColorSpace = SPACE_Pattern ;
            colorChain->colorspace = colorspace ;
          }

          if ( ! status ) {
            colorChain->patternPaintType = NO_PATTERN ;
            (void)push( & patterno , stack ) ;
            if ( newerror == UNDEFINED ||
                 newerror == UNREGISTERED ||
                 newerror == TYPECHECK )
              return error_handler( RANGECHECK ) ; /* is required */
            return FALSE ;
          }

          break ;
        default:
          (void)push( & patterno , stack ) ;
          return error_handler( UNDEFINED ) ;
        }
        break;

      case 2: /* PatternType 2: smooth shading pattern */
        colorChain->patternPaintType = COLOURED_PATTERN ;
        break;

      default:
        (void)push( & patterno , stack ) ;
        return error_handler( UNDEFINED ) ;
      }

      /* PS2_TO_PS3_COLORCACHE NOP */

      /* And save away the pattern */
      Copy( & colorChain->pattern , & patterno ) ;

      break;
    }

  default:
    HQFAIL( "unrecognized color space" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  return TRUE ;
}

Bool gsc_setcolordirect( GS_COLORinfo *colorInfo, int32 colorType, USERVALUE *values )
{
  /* See red book 2 page 497 and section 4.8:
   * All we do here is check and store the color value; the actual conversion
   * is performed when we do a paint operation.
   */
  int32 i ;
  int32 ndims ;
  int32 colorspace_id;
  SYSTEMVALUE indexedrange[2];
  USERVALUE arg;
  GS_CHAINinfo *chainInfo ;

  COLORTYPE_ASSERT(colorType, "gsc_setcolordirect") ;

  /* Update the relevant chain head structure */
  chainInfo = colorInfo->chainInfo[ colorType ] ;
  colorspace_id = chainInfo->iColorSpace;
  switch ( colorspace_id ) {
  case SPACE_DeviceGray:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceCMYK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_TrapDeviceN:
    ndims = chainInfo->n_iColorants ;
    if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
      return FALSE;
    chainInfo = colorInfo->chainInfo[ colorType ] ;
    for ( i = 0 ; i < ndims ; i++ ) {
      arg = values[ i ] ;
      NARROW_01( arg ) ;
      chainInfo->iColorValues[ i ] = arg ;
    }

    if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) ) {
      if ( colorspace_id == SPACE_DeviceCMYK ||
           colorspace_id == SPACE_DeviceRGB ) {
        Bool fPresepColorIsGray = FALSE ;
        if ( rcbn_intercepting() && (colorType == GSC_FILL || colorType == GSC_STROKE) ) {
          if ( colorspace_id == SPACE_DeviceCMYK )
            fPresepColorIsGray = IS_BLACKONLY_CMYK( chainInfo->iColorValues );
          else
            fPresepColorIsGray = IS_GRAY_RGB( chainInfo->iColorValues );
        }

        if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
          return FALSE;
      }
      else if ( colorspace_id == SPACE_Separation ||
                colorspace_id == SPACE_DeviceN ) {
        Bool fPresepColorIsGray = FALSE ;

        if ( rcbn_intercepting() ) {
          if ( ! spaceIsJustBlack( colorInfo,
                                   &chainInfo->colorspace ,
                                   &fPresepColorIsGray ))
            return FALSE ;
        }

        if (! cc_setPresepColorIsGray( colorInfo, colorType, fPresepColorIsGray ))
          return FALSE;
      }
    }

    break;

  case SPACE_Indexed:
    if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
      return FALSE ;
    chainInfo = colorInfo->chainInfo[ colorType ] ;
    if ( ! gsc_range( colorInfo , colorType , 0 , indexedrange ))
      return FALSE ;
    arg = values[ 0 ] ;
    if ( arg < 0.0f )
      arg = 0.0f ;
    else if ( arg > indexedrange[ 1 ] )
      arg = ( USERVALUE )( indexedrange[ 1 ] ) ;
    /* truncate: */
    arg = ( USERVALUE )(( int32 )arg ) ;

    chainInfo->iColorValues[ 0 ] = arg ;
    break;

  case SPACE_CMM:
    HQFAIL("CMM colorspace NYI");
    break;
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
  case SPACE_Lab:
  case SPACE_CalGray:
  case SPACE_CalRGB:
  case SPACE_ICCBased:
    ndims = chainInfo->n_iColorants ;
    if ( ! cc_updateChain( & colorInfo->chainInfo[ colorType ] ))
      return FALSE ;
    chainInfo = colorInfo->chainInfo[ colorType ] ;

    /* Set the color values, no need to validate them as for gsc_setcolor() */
    for ( i = 0 ; i < ndims ; i++ ) {
      USERVALUE arg = values[i];
      SYSTEMVALUE range[2];

      if (!gsc_range(colorInfo, colorType, i, range)) {
        HQASSERT(i == 0, "gsc_range shouldn't leave gstate in undefined state");
        return FALSE;
      }
      if ( arg < range[0] )
        arg = (USERVALUE)range[0];
      if ( arg > range[1] )
        arg = (USERVALUE)range[1];
      chainInfo->iColorValues[ i ] = arg ;
    }

    break;

  case SPACE_Pattern:
  case SPACE_PatternMask:
    /* Can't set colors directly in Pattern colorspace */
    HQFAIL ( "gsc_setcolordirect called with Pattern colorspace" );
    return error_handler(UNREGISTERED) ;

  default:
    HQFAIL( "unrecognized color space" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool gsc_currentcolor(GS_COLORinfo *colorInfo, STACK *pstack, int32 colorType)
{
  /* Returns the appropriate number of numbers from the current input color
   * except that for patterns it is a bit more complicated.
   */
  int32 i ;
  GS_CHAINinfo *chainInfo;
  COLORSPACE_ID space ;
  int32 dimension;
  OBJECT cv = OBJECT_NOTVM_NOTHING;

  chainInfo = colorInfo->chainInfo[colorType];
  space = chainInfo->iColorSpace ;

  switch (space) {
  default:
    for ( i = 0 ; i < chainInfo->n_iColorants ; ++i ) {
      object_store_real(&cv, chainInfo->iColorValues[ i ]) ;
      if ( ! push(&cv, pstack) )
        return FALSE ;
    }
    return TRUE ;
  case SPACE_Indexed:
    object_store_integer(&cv, (int32)chainInfo->iColorValues[ 0 ]) ;
    return push(&cv, pstack) ;
  case SPACE_Pattern:
    /* Pattern: if there's an underlying space we push the color coordinates
       for that first, then we push the dictionary */
    if ( !cc_getpatternbasespace( colorInfo, &chainInfo->colorspace,
                                  &space, &dimension ) )
      HQFAIL ("Pattern base space changed unexpectedly");

    /* To start with the OBJECT will be a NULL. */
    if ( space != SPACE_notset && oType(chainInfo->pattern) == ODICTIONARY ) {
      OBJECT *theo = fast_extract_hash_name(&chainInfo->pattern,
                                            NAME_PaintType) ;
      if ( theo != NULL && oInteger(*theo) == 2 )
        for ( i = 0 ; i < chainInfo->n_iColorants ; ++i ) {
          object_store_real(&cv, chainInfo->iColorValues[ i ]) ;
          if ( ! push(&cv, pstack) )
            return FALSE ;
        }
    }

    /* now push the pattern dictionary */
    return push( & chainInfo->pattern , pstack ) ;
  case SPACE_PatternMask:
    object_store_real(&cv, 0.0f) ;
    return push(&cv, pstack) ;
  }
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
Bool gsc_setpattern( GS_COLORinfo *colorInfo, STACK *pstack, int32 colorType )
{
  /* Essentially does what setcolor does, but with some additional changing
   * of the color spaces under some circumstances - see red book 2 page 513.
   */

  OBJECT *olist ;
  OBJECT  array = OBJECT_NOTVM_NOTHING ;
  OBJECT  colorspace = OBJECT_NOTVM_NOTHING ;

  if ( isEmpty( *pstack ))
    return error_handler( STACKUNDERFLOW ) ;

  if ( colorInfo->chainInfo[ colorType ]->iColorSpace != SPACE_Pattern ) {
    /* Make sure the current color space is represented as an array first
     * of all so that currentcolorspace gives [/Pattern [...]].
     */
    if (!gsc_currentcolorspace( colorInfo, GSC_FILL, &colorspace))
      return FALSE ;
    HQASSERT(oType(colorspace) == OARRAY, "Colorspace should be an array" ) ;

    /* Create an array representing the new color space */
    olist = ( OBJECT * )get_omemory( 2 ) ;
    if ( ! olist )
      return error_handler(VMERROR) ;
    theTags( array ) = OARRAY | LITERAL | UNLIMITED ;
    theLen( array ) = 2 ;
    oArray( array ) = olist ;

    object_store_name(&olist[0], NAME_Pattern, LITERAL) ;
    OCopy(olist[1], colorspace) ;

    /* and setcolorspace on that array */
    if ( ! push( & array , & operandstack ) ) {
      return FALSE ;
    }
    if ( ! gsc_setcolorspace( colorInfo, &operandstack, colorType ) )
      return FALSE ;
  }

  /* And do the setcolor with the rest of the parameters in Pattern space. */
  return gsc_setcolor( colorInfo, pstack, colorType );
}

Bool gsc_getpattern( GS_COLORinfo *colorInfo, int32 colorType, OBJECT **pattern )
{
  GS_CHAINinfo*  chainInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");

  chainInfo = colorInfo->chainInfo[colorType];
  HQASSERT(chainInfo != NULL, "chainInfo NULL");

  if (chainInfo == NULL)
    return error_handler(UNDEFINED);

  HQASSERT(chainInfo->iColorSpace == SPACE_Pattern,
           "Expected Pattern color space");

  *pattern = &colorInfo->chainInfo[colorType]->pattern;

  return TRUE;
}

/* Note: there is no currentpattern operator */

/* ---------------------------------------------------------------------- */
Bool gsc_setpatternmaskdirect(GS_COLORinfo *colorInfo,
                              int32 colorType,
                              OBJECT *pattern,
                              USERVALUE *iColorValues)
{
  GS_CHAINinfo *colorChain ;
  int32 colorspacedimension, result ;
  COLORSPACE_ID colorspace_id ;
  OBJECT colorspace = OBJECT_NOTVM_NOTHING ;

  COLORTYPE_ASSERT(colorType, "gsc_setpatternmaskdirect") ;
  HQASSERT(pattern, "No pattern object") ;
  HQASSERT(iColorValues != NULL, "No underlying colour parameters") ;

  if ( !gsc_getcolorspacesizeandtype(colorInfo, pattern,
                                     &colorspace_id, &colorspacedimension) )
    return FALSE;

  HQASSERT(colorspace_id == SPACE_Pattern,
           "Object is not a pattern colourspace") ;
  HQASSERT(colorspacedimension > 0, "No underlying colours in base space") ;

  /* Set the colour space to SPACE_PatternMask, similar to SPACE_Pattern. This
     is copied from gsc_setcolorspace. */
  if ( ! cc_updateChainForNewColorSpace( colorInfo ,
                                         colorType ,
                                         SPACE_PatternMask ,
                                         colorspacedimension ,
                                         pattern ,
                                         CHAINSTYLE_COMPLETE,
                                         FALSE /* fCompositing */))
    return FALSE ;

  /* Set the underlying colour; copied from gsc_setcolor. */
  if (! cc_invalidateChain( colorInfo , colorType , FALSE ))
    return FALSE;

  /* May have updated colour chain head, so re-cache it. */
  colorChain = colorInfo->chainInfo[ colorType ];
  HQASSERT(colorChain != NULL, "colorChain NULL");

  /* SPACE_PatternMask is used for the contents of uncoloured pattern cells.
     These are not patterned themselves, which is what patternPaintType
     indicates. */
  colorChain->patternPaintType = NO_PATTERN ;

  /* An UNCOLORED pattern - requires an underlying
   * color space already plus parameters supplied here.
   * so set the colorspace of the head link to the colorspace
   * of the basespace and call gsc_setcolor.
   */
  if ( !cc_getpatternbasespace(colorInfo,
                               &colorChain->colorspace,
                               &colorspace_id,
                               &colorspacedimension) )
    return FALSE ;

  HQASSERT(colorspace_id != SPACE_notset,
           "No underlying colour space for uncoloured pattern") ;

  /* Lie about the color space and call setcolor recursively. */
  Copy(&colorspace, &colorChain->colorspace) ;

  colorChain->iColorSpace = colorspace_id ;
  Copy(&colorChain->colorspace, &oArray(colorspace)[1]) ;

  /* This sets the colour in the chain head */
  result = gsc_setcolordirect(colorInfo, colorType, iColorValues) ;

  colorChain = colorInfo->chainInfo[ colorType ] ;
  colorChain->iColorSpace = SPACE_PatternMask ;
  Copy(&colorChain->colorspace, &colorspace) ;

  Copy(&colorChain->pattern, pattern) ;

  return result ;
}

/* ---------------------------------------------------------------------- */
void gsc_initgray( GS_COLORinfo *colorInfo )
{
  ( void )gsc_setcolorspacedirect( colorInfo, GSC_STROKE, SPACE_DeviceGray);
  ( void )gsc_setcolorspacedirect( colorInfo, GSC_FILL, SPACE_DeviceGray);
}

/* ---------------------------------------------------------------------- */
int32 gsc_dimensions( GS_COLORinfo *colorInfo, int32 colorType )
{
  GS_CHAINinfo * colorChain;

  HQASSERT( colorInfo , "colorInfo NULL in gsc_dimensions" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_dimensions") ;

  colorChain = colorInfo->chainInfo[colorType];
  HQASSERT( colorChain != NULL, "gsc_dimensions: hit NULL color chain");

  return colorChain->n_iColorants;
}

/* ---------------------------------------------------------------------- */
void gsc_getcolorvalues( GS_COLORinfo *colorInfo , int32 colorType ,
                         USERVALUE **pColorValues , int32 *pnDimensions )
{
  GS_CHAINinfo *colorChain ;

  HQASSERT( colorInfo , "NULL colorInfo passed to gsc_getcolorvalues" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getcolorvalues") ;
  HQASSERT( pColorValues != NULL , "NULL pColorValues passed to gsc_getcolorvalues" ) ;
  HQASSERT( pnDimensions != NULL , "NULL pnDimensions passed to gsc_getcolorvalues" ) ;

  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT( colorChain != NULL , "NULL colorChain in gsc_getcolorvalues" ) ;

  *pnDimensions = colorChain->n_iColorants ;
  *pColorValues = colorChain->iColorValues ;
}

Bool gsc_setoverprintprocess( GS_COLORinfo *colorInfo , int32 colorType,
                              uint8 overprintProcess )
{
  GS_CHAINinfo *colorChain ;

  HQASSERT( colorInfo , "NULL colorInfo passed to gsc_getoverprintprocess" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getoverprintprocess") ;

  if (colorInfo->chainInfo[colorType]->overprintProcess != overprintProcess) {
    if (cc_updateChain(&colorInfo->chainInfo[colorType]) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorChain = colorInfo->chainInfo[ colorType ] ;
      HQASSERT( colorChain != NULL , "NULL colorChain in gsc_getoverprintprocess" ) ;

      colorChain->overprintProcess = overprintProcess;
    }
    else
      return FALSE;
  }

  return TRUE;
}

uint8 gsc_getoverprintprocess( GS_COLORinfo *colorInfo , int32 colorType )
{
  GS_CHAINinfo *colorChain ;

  HQASSERT( colorInfo , "NULL colorInfo passed to gsc_getoverprintprocess" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getoverprintprocess") ;

  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT( colorChain != NULL , "NULL colorChain in gsc_getoverprintprocess" ) ;

  return colorChain->overprintProcess ;
}

/* ---------------------------------------------------------------------- */

/** Switches a color chain's presep mode. The existing chain may be recycled
 * via the ChainCache.
 */
static Bool cc_setPresepColorIsGray( GS_COLORinfo *colorInfo, int32 colorType,
                                     Bool fPresepColorIsGray )
{
  GS_CHAINinfo *colorChain ;

  HQASSERT( colorInfo , "NULL colorInfo passed to cc_setPresepColorIsGray" ) ;
  COLORTYPE_ASSERT(colorType, "cc_setPresepColorIsGray") ;

  if (colorInfo->chainInfo[colorType]->fPresepColorIsGray != fPresepColorIsGray) {
    if (cc_updateChain(&colorInfo->chainInfo[colorType]) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorChain = colorInfo->chainInfo[ colorType ] ;
      HQASSERT( colorChain != NULL , "NULL colorChain in cc_setPresepColorIsGray" ) ;

      colorChain->fPresepColorIsGray = fPresepColorIsGray ;
    }
    else
      return FALSE;
  }

  return TRUE;
}

/** Switches a color chain's compositing mode. The existing chain may be recycled
 * via the ChainCache.
 * If compositing is turned off, black preservation must be initialised because
 * the color cache uses it as a key.
 */
static Bool cc_setCompositing( GS_COLORinfo *colorInfo, int32 colorType,
                               Bool fCompositing )
{
  GS_CHAINinfo *colorChain ;

  HQASSERT( colorInfo , "NULL colorInfo passed to cc_setCompositing" ) ;
  COLORTYPE_ASSERT(colorType, "cc_setCompositing") ;

  if (colorInfo->chainInfo[colorType]->fCompositing != fCompositing) {
    if (cc_updateChain(&colorInfo->chainInfo[colorType]) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorChain = colorInfo->chainInfo[ colorType ] ;
      HQASSERT( colorChain != NULL , "NULL colorChain in cc_setCompositing" ) ;

      colorChain->fCompositing = fCompositing ;
    }
    else
      return FALSE ;

    if (!colorChain->fCompositing) {
      /* When not compositing, the blackType must be initialised to BLACK_TYPE_NONE
       * as a constant value because the color cache uses it as a key. When
       * compositing the value will be pre-set in compositing code.
       */
      if (!gsc_setBlackType(colorInfo, colorType, BLACK_TYPE_NONE))
        return FALSE;
    }
  }

  return TRUE;
}

/**
 * Is this chain built for compositing (i.e. is it a backend chain)?
 */
Bool gsc_fCompositing(GS_COLORinfo *colorInfo, int32 colorType)
{
  HQASSERT(colorInfo, "NULL colorInfo" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_fCompositing") ;
  return colorInfo->chainInfo[colorType]->fCompositing;
}

/* ---------------------------------------------------------------------- */

/* Getter + setter of blackType.
 */
static void validBlackType(GSC_BLACK_TYPE blackType)
{
  switch (blackType) {
  case BLACK_TYPE_100_PC:
  case BLACK_TYPE_TINT:
  case BLACK_TYPE_ZERO:
  case BLACK_TYPE_NONE:
  case BLACK_TYPE_MODIFIED:
    break;
  case BLACK_TYPE_UNKNOWN:
  default:
    HQFAIL("Invalid black type");
    break;
  }
}

/** Switches a color chain's black preservation mode. The existing chain may be
 * recycled via the ChainCache.
 * If the relevent chain is a front end chain, black preservation must always
 * be in the initialised state because the color cache uses it as a key. Back
 * end chains will be switched as necessary.
 */
Bool gsc_setBlackType(GS_COLORinfo *colorInfo, int32 colorType, GSC_BLACK_TYPE blackType)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[colorType];

  HQASSERT( colorInfo , "colorInfo NULL" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_setBlackType") ;
  validBlackType(blackType);

  if (colorInfo->chainInfo[colorType]->inBlackType != blackType) {
    if (cc_updateChain(&colorInfo->chainInfo[colorType]) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorChain = colorInfo->chainInfo[ colorType ] ;
      HQASSERT( colorChain != NULL , "NULL colorChain in cc_setCompositing" ) ;

      colorChain->inBlackType = blackType;
    }
    else
      return FALSE;
  }

  return TRUE;
}

/* ------------------------------------------------------------------------ */

/** Switches a color chain's independent channels mode. The existing chain may
 * be recycled via the ChainCache.
 */
Bool gsc_setPrevIndependentChannels(GS_COLORinfo *colorInfo, int32 colorType,
                                    Bool prevIndependentChannels)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[colorType];

  HQASSERT(colorInfo , "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_setPrevIndependentChannels");

  if (colorChain->prevIndependentChannels != prevIndependentChannels) {
    if (cc_updateChain(&colorInfo->chainInfo[colorType]) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorChain = colorInfo->chainInfo[ colorType ] ;
      HQASSERT( colorChain != NULL , "colorChain NULL" ) ;

      colorChain->prevIndependentChannels = prevIndependentChannels;
    }
    else
      return FALSE ;
  }

  return TRUE;
}

Bool gsc_getPrevIndependentChannels(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[colorType];

  HQASSERT(colorInfo, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_getColorModel");

  HQASSERT(colorChain->prevIndependentChannels == TRUE ||
           colorChain->prevIndependentChannels == FALSE,
           "Invalid prevIndependentChannels");

  return colorChain->prevIndependentChannels;
}

/* ---------------------------------------------------------------------- */

/* Switches a color chain into luminosity mode. The existing chain may be recycled
 * via the ChainCache.
 * This is used for transparency soft masks where alpha is derived from
 * luminosity. The luminosity values are obtained from device color spaces by
 * converting to device gray and from device independent color spaces
 * by converting to XYZ and extracting the Y component.
 */

Bool gsc_setLuminosityChain(GS_COLORinfo *colorInfo, int32 colorType, Bool on)
{
  GS_CHAINinfo* colorChain;
  HQASSERT(colorInfo != NULL, "colorInfo is null");
  HQASSERT(colorType >= 0 && colorType < GSC_N_COLOR_TYPES,
           "colorType is out of range");

  colorChain = colorInfo->chainInfo[colorType];
  if (colorChain->fSoftMaskLuminosityChain != on) {
    if (cc_updateChain(&colorInfo->chainInfo[colorType]) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorChain = colorInfo->chainInfo[colorType];

      colorChain->fSoftMaskLuminosityChain = on;
    }
    else
      return FALSE;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* Getter of fSoftMaskLuminosityChain.
 */
Bool gsc_getfSoftMaskLuminosityChain(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[colorType];

  HQASSERT( colorInfo , "NULL colorInfo passed to gsc_getfSoftMaskLuminosityChain" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_getfSoftMaskLuminosityChain") ;

  return colorChain->fSoftMaskLuminosityChain;
}

/* ---------------------------------------------------------------------- */
/* Given an indexed color space (typically the GSC_SHFILL element in
   chainInfo in a graphics state, and specifically not
   GSC_SHFILL_INDEXED_BASE), gsc_baseColor returns a pointer to
   *pnDimensions input color values in *ppColor, that number also
   being set. This is the same number that would be returned by
   gsc_dimensions on the equivalent GSC_SHFILL_INDEXED_BASE chain.

   If the colorchain does not exist it will be created and the first
   link will be invoked. This means that this function can be used
   without invoking the complete chain. The function returns TRUE
   if the chain has been successfully invoked, FALSE otherwise.

   Note: the memory for the color values remains the property of the
   color chain. Do not try to free it; also note that it will be
   overwritten on next invocation, so make sure you keep a copy if you
   need to keep hold of it.
 */

Bool gsc_baseColor( GS_COLORinfo *colorInfo, int32 colorType, USERVALUE ** ppColor, int32 * pnDimensions )
{
  GS_CHAINinfo * colorChain ;
  CLINK *pIndexedLink ;

  HQASSERT( colorInfo , "NULL colorInfo passed to gsc_getcolorvalues" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_baseColor") ;
  HQASSERT (ppColor != NULL, "NULL ppColor passed to gsc_baseColor");
  HQASSERT (pnDimensions != NULL, "NULL pnDimensions passed to gsc_baseColor");

  if ( ! gsc_constructChain( colorInfo, colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[colorType];
  HQASSERT (colorChain != NULL, "NULL colorChain passed to gsc_baseColor");
  HQASSERT( colorChain->context->pnext , "gsc_baseColor did not construct next link as expected" ) ;
  HQASSERT (colorChain->iColorSpace == SPACE_Indexed,
                      "gsc_baseColor called when colorspace wasn't SPACE_Indexed");

  pIndexedLink = colorChain->context->pnext ;

  /* Copy the color from the head link into the first link of the chain */
  pIndexedLink->iColorValues[ 0 ] = colorChain->iColorValues[ 0 ] ;

  if ( !(pIndexedLink->functions->invokeSingle)( pIndexedLink, pIndexedLink->pnext->iColorValues ) )
    return FALSE ;

  * pnDimensions = pIndexedLink->pnext->n_iColorants;
  * ppColor = pIndexedLink->pnext->iColorValues;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* gsc_range writes the minimum and maximum values that component "index"
   of the input color space indexed from colorType in colorInfo may accept
   into prRange[0] and prRange[1].
 */

Bool gsc_range( GS_COLORinfo *colorInfo, int32 colorType, int32 index, SYSTEMVALUE range[2] )
{
  GS_CHAINinfo * colorChain;
  COLORSPACE_ID colorSpace;

  HQASSERT( colorInfo != NULL, "gsc_range: colorInfo parameter is NULL");
  colorChain = colorInfo->chainInfo[colorType];
  HQASSERT( colorChain != NULL, "gsc_range: hit NULL color chain");
  HQASSERT (index < colorChain->n_iColorants,
            "gsc_range: index out of bounds for number of color channels.");

  colorSpace = colorChain->iColorSpace;

  switch (colorSpace) {
  case SPACE_DeviceGray:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceCMYK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_CalGray:
  case SPACE_CalRGB:
    range[0] = 0.0;
    range[1] = 1.0;
    break;
  case SPACE_CMM:
    HQFAIL("CMM colorspace NYI");
    break;
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
  case SPACE_Lab:
  case SPACE_ICCBased:
    /* Since we will need range from the colorchain we must first of all
     * construct the colorchain.
     */
    if ( ! gsc_constructChain( colorInfo, colorType ))
      return FALSE ;
    colorChain = colorInfo->chainInfo[ colorType ] ;
    HQASSERT( colorChain != NULL , "colorChain NULL when not expected" ) ;
    HQASSERT( colorChain->context->pnext != NULL, "invalid colorChain given to gsc_range" ) ;
    cc_getCieRange(colorChain->context->pnext, index, range);
    break;
  case SPACE_Indexed:
    /* Since we will need range from the colorchain we must first of all
     * construct the colorchain.
     */
    if ( ! gsc_constructChain( colorInfo, colorType ))
      return FALSE ;
    colorChain = colorInfo->chainInfo[ colorType ] ;
    HQASSERT( colorChain , "colorChain NULL when not expected" ) ;
    HQASSERT( colorChain->context->pnext != NULL, "invalid colorChain given to gsc_range" ) ;
    range[0] = 0.0;
    range[1] = cc_getindexedhival( colorChain->context->pnext );
    break;
  case SPACE_Pattern:
    HQFAIL( "cannot get range of pattern color space" ) ;
  default:
    HQFAIL( "unrecognized color space in gsc_range" ) ;
    range[0] = 0.0;
    range[1] = 1.0;
    break;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool gsc_baseRange(GS_COLORinfo *colorInfo, int32 colorType, int32 index,
                   SYSTEMVALUE range[2])
{
  GS_CHAINinfo* colorChain;
  CLINK *pIndexedLink ;

  HQASSERT(colorInfo != NULL, "gsc_baseRange: colorInfo parameter is NULL");

  colorChain = colorInfo->chainInfo[colorType];
  HQASSERT(colorChain != NULL, "gsc_baseRange: hit NULL color chain");
  HQASSERT(colorChain->iColorSpace == SPACE_Indexed,
           "gsc_baseRange: Not an indexed space");

  if (colorChain->context == NULL) {
    HQFAIL("chainContext NULL");
    return FALSE;
  }

  pIndexedLink = colorChain->context->pnext;
  HQASSERT(pIndexedLink != NULL, "pIndexedLink is null");
  HQASSERT(pIndexedLink->pnext != NULL, "pIndexedLink->pnext is null");
  HQASSERT(index < pIndexedLink->pnext->n_iColorants,
           "gsc_baseRange: index out of bounds for number of color channels");

  switch (pIndexedLink->pnext->iColorSpace)
  {
    case SPACE_Pattern:
    case SPACE_Indexed:
    case SPACE_CMM:
      return error_handler(RANGECHECK);

    case SPACE_DeviceGray:
    case SPACE_DeviceRGB:
    case SPACE_DeviceCMY:
    case SPACE_DeviceCMYK:
    case SPACE_Separation:
    case SPACE_DeviceN:
    case SPACE_CalGray:
    case SPACE_CalRGB:
      range[0] = 0.0;
      range[1] = 1.0;
      break;

    default:
      cc_getCieRange(pIndexedLink->pnext, index, range);
      break;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool gsc_getcurrentcolorspacerange(GS_COLORinfo *colorInfo, STACK *stack)
{
  OBJECT *theo;
  OBJECT arrayo = OBJECT_NOTVM_NOTHING;
  int32 i, ncomps;
  SYSTEMVALUE range[2];

  theo = theTop(*stack);

  if (oType(*theo) != OINTEGER)
    return error_handler(TYPECHECK);

  ncomps = oInteger(*theo);
  pop(&operandstack);

  if (!ps_array(&arrayo, 2*ncomps))
    return FALSE;

  for (i=0; i < ncomps; i++) {
    if (!gsc_range(colorInfo, GSC_FILL ,i ,range))
      return FALSE;

    object_store_real(&oArray(arrayo)[2*i], (USERVALUE) range[0]);
    object_store_real(&oArray(arrayo)[(2*i)+1], (USERVALUE) range[1]);
  }

  return push(&arrayo, &operandstack);
}
/* ---------------------------------------------------------------------- */

/* Not intended for export outside the color module
 */
void cc_getCieRange( CLINK *pLink, int32 index, SYSTEMVALUE range[2] )
{
  HQASSERT( pLink , "pLink NULL in cc_getCieRange" ) ;
  HQASSERT( range , "range NULL in cc_getCieRange" ) ;

  switch (pLink->linkType) {
  case CL_TYPEblackevaluate:
  case CL_TYPEblackremove:
    /* Recurse into the next link in the chain */
    cc_getCieRange(pLink->pnext, index, range);
    break;
  case CL_TYPEciebaseda:
    cc_getCieBasedABCRange(pLink->p.ciebaseda, index, range);
    break;
  case CL_TYPEciebasedabc:
    cc_getCieBasedABCRange(pLink->p.ciebasedabc, index, range);
    break;
  case CL_TYPEciebaseddef:
    cc_getCieBasedDEFRange(pLink->p.ciebaseddef, index, range);
    break;
  case CL_TYPEciebaseddefg:
    cc_getCieBasedDEFGRange(pLink->p.ciebaseddefg, index, range);
    break;
  case CL_TYPEcietablea:
    cc_getCieTableRange(pLink->p.cietablea, index, range);
    break;
  case CL_TYPEcietableabc:
    cc_getCieTableRange(pLink->p.cietableabc, index, range);
    break;
  case CL_TYPEcietableabcd:
    cc_getCieTableRange(pLink->p.cietableabcd, index, range);
    break;
  case CL_TYPElab:
    cc_getLabRange(pLink->p.lab, index, range);
    break;
  case CL_TYPEiccbased:
    cc_getICCBasedRange(pLink->p.iccbased, index, range);
    break;
  case CL_TYPEcalgray:
  case CL_TYPEcalrgb:
    range[ 0 ] = 0.0 ;
    range[ 1 ] = 1.0 ;
    break;
  case CL_TYPEcmmxform:
    /* Calls cc_getCieRange on the first link of the transform */
    cc_getCMMRange(pLink, index, range);
    break;
  case CL_TYPEalternatecmm:
  case CL_TYPEcustomcmm:
    /** \todo JJ. Hack these two for now
    cc_getAlternateCMMRange(pLink, index, range); ****/
    range[ 0 ] = 0.0 ;
    range[ 1 ] = 1.0 ;
    break;

  default:
    /* These can result from interception of device independent spaces */
    switch (pLink->iColorSpace) {
    default:
      HQFAIL("Invalid linkType for getCieRange");
      /* DROP THROUGH just in case */
    case SPACE_DeviceGray:
    case SPACE_DeviceRGB:
    case SPACE_DeviceCMY:
    case SPACE_DeviceCMYK:
    case SPACE_Separation:
    case SPACE_DeviceN:
      range[ 0 ] = 0.0 ;
      range[ 1 ] = 1.0 ;
      break;
    }
  }

  HQASSERT(range[0] < range[1], "upper range is less than lower");
  HQTRACE(range[0] <= -300, ("Very low minimum range value"));
  HQTRACE(range[1] >=  300, ("Very high maximum range value"));
}

/* ---------------------------------------------------------------------- */
static void hsv_to_rgb( USERVALUE hsv[ 3 ] , USERVALUE rgb[ 3 ] )
{
  /* Originally taken from page 616 of Newman and Sproull */
  int32 i ;
  USERVALUE  h , s , v , f , p , q , t ;

  HQASSERT( hsv , "hsv NULL in hsv_to_rgb" ) ;
  HQASSERT( rgb , "rgb NULL in hsv_to_rgb" ) ;

  h = hsv[ 0 ] ; NARROW_01( h ) ;
  s = hsv[ 1 ] ; NARROW_01( s ) ;
  v = hsv[ 2 ] ; NARROW_01( v ) ;

  if ( s < ( USERVALUE )EPSILON ) {
    rgb[ 0 ] = rgb[ 1 ] = rgb[ 2 ] = v ;
  }
  else {
    if ( fabs(1.0f - h) < ( USERVALUE )EPSILON )
      h = 0.0f ;
    else
      h *= 6.0f ;

    i = ( int32 )h ; /* 0..5 */
    f = h - ( USERVALUE )i ; /* remainder */
    p = v * ( 1.0f - s ) ;
    q = v * ( 1.0f - s * f ) ;
    t = v * ( 1.0f - s * ( 1.0f - f )) ;
    NARROW_01( v ) ;
    NARROW_01( t ) ;
    NARROW_01( p ) ;
    NARROW_01( q ) ;
    switch ( i ) {
    case 0: rgb[ 0 ] = v ; rgb[ 1 ] = t ; rgb[ 2 ] = p ; break ;
    case 1: rgb[ 0 ] = q ; rgb[ 1 ] = v ; rgb[ 2 ] = p ; break ;
    case 2: rgb[ 0 ] = p ; rgb[ 1 ] = v ; rgb[ 2 ] = t ; break ;
    case 3: rgb[ 0 ] = p ; rgb[ 1 ] = q ; rgb[ 2 ] = v ; break ;
    case 4: rgb[ 0 ] = t ; rgb[ 1 ] = p ; rgb[ 2 ] = v ; break ;
    case 5: rgb[ 0 ] = v ; rgb[ 1 ] = p ; rgb[ 2 ] = q ; break ;
    default:
      HQFAIL( "hsv_to_rgb: Unhandled case reached." ) ;
    }
  }
}

/* ---------------------------------------------------------------------- */
Bool gsc_rgb_to_hsv( USERVALUE rgb[ 3 ] , USERVALUE hsv[ 3 ] )
{
  USERVALUE maxi , mini , diff , r , g , b ;

  HQASSERT( rgb , "rgb NULL in rgb_to_hsv" ) ;
  HQASSERT( hsv , "hsv NULL in rgb_to_hsv" ) ;

  r = rgb[ 0 ] ;
  g = rgb[ 1 ] ;
  b = rgb[ 2 ] ;

  maxi = r > g ? ( r > b ? r : ( g > b ? g : b )) : ( g > b ? g : b ) ;
  mini = r < g ? ( r < b ? r : ( g < b ? g : b )) : ( g < b ? g : b ) ;
  diff = maxi - mini ;
  hsv[ 2 ] = maxi ;
  hsv[ 1 ] = 0.0f ;
  if ( maxi > ( USERVALUE )EPSILON )
    hsv[ 1 ] = diff / maxi ;
  if ( hsv[ 1 ] < ( USERVALUE )EPSILON )
    hsv[ 0 ] = 0.0f ;
  else {
    USERVALUE rc = ( maxi - r ) / diff ;
    USERVALUE gc = ( maxi - g ) / diff ;
    USERVALUE bc = ( maxi - b ) / diff ;
    if ( r == maxi )
      hsv[ 0 ] = ( bc - gc ) / 6.0f ;
    else if ( g == maxi )
      hsv[ 0 ] = ( 2.0f + rc - bc ) / 6.0f ;
    else /* b == maxi */
      hsv[ 0 ] = ( 4.0f + gc - rc ) / 6.0f ;
    if ( hsv[ 0 ] < 0.0f )
      hsv[ 0 ] += 1.0f ;
  }
  return TRUE ;
}

/* eof */

/* Log stripped */
