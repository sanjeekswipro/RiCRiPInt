/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gschcms.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphics-state HCMS implementation
 */

#include "core.h"

#include "blitcolort.h"         /* BLIT_MAX_COLOR_CHANNELS */
#include "control.h"            /* interpreter, aninterrupt */
#include "dicthash.h"           /* insert_hash */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "dictops.h"            /* enddictmark_ */
#include "dlstate.h"            /* inputpage */
#include "fileio.h"             /* SLIST (needed by fileops.h) */
#include "fileops.h"            /* closefile_ */
#include "gcscan.h"             /* ps_scan_field */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "miscops.h"            /* run_ps_string */
#include "mm.h"                 /* mm_sac_alloc */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* OBJECT */
#include "stackops.h"           /* STACK_POSITIONS */
#include "swerrors.h"           /* error_handler */

#include "swcmm.h"              /* sw_cmm_instance */
#include "gs_cache.h"           /* GSC_ENABLE_NAMED_COLOR_CACHE */
#include "gs_colorpriv.h"       /* GS_HCMSinfo */
#include "gscalternatecmmpriv.h"/* cc_findAlternateCMM */
#include "gscblackevaluate.h"   /* cc_redoBlackRelevant */
#include "gsccie.h"             /* cc_reserveciebaseddefginfo */
#include "gsccmmpriv.h"         /* TRANSFORM_LINK_INFO */
#include "gsccrdpriv.h"         /* cc_reservecrdinfo */
#include "gscheadpriv.h"        /* gsc_getcolorspacesizeandtype */
#include "gscicc.h"             /* cc_get_iccbased_profile_info */
#include "gscparamspriv.h"      /* colorUserParams */
#include "gscsmpxformpriv.h"    /* cc_csaGetSimpleTransform */
#include "gsctable.h"           /* cc_reservecietableabcdinfo */
#include "gschcmspriv.h"        /* externs */
#include "wcscmm.h"             /* WCS CMM */


/* The ChainCache is the cache of color chains which can be reused for the benefit
 * of performance. The ChainCache keys off of hcmInfo->interceptId which is
 * generally bumped when a new intercept is seen because it's too complex to work
 * out if the dictionaries we now have are the same as ones we've seen before.
 * However, there are 2 well-known cases where we can set the same interceptId and
 * they are with all intercepts at default values, and ditto with overrides set.
 * They will commonly be used in cases such as soft masks where intercepts must be
 * disabled because colour management shouldn't be used to generate a soft mask.
 * We have some belt and braces protection for overflowing a 32 bit integer.
 */
enum {
  INTERCEPT_ID_DEFAULT                  = 0,
  INTERCEPT_ID_NONE_WITH_OVERRIDES      = 1,
  INTERCEPT_ID_NONE_WITHOUT_OVERRIDES   = 2,
  INTERCEPT_ID_INITIAL                  = 3,
  INTERCEPT_ID_MAX                      = 0xFFFFFFFF
};


/* BlackTint values can be either boolean or real. We store the values as real
 * for convenience, so when a boolean is supplied it is converted to one of
 * these real values.
 * The Black key is handled the same to allow some code reuse, but only accepts
 * boolean values.
 */
#define BLACK_TRUE    (2.0f)    /* A value more than 1 */
#define BLACK_FALSE   (-1.0f)   /* A value less than 0 */

typedef struct INTERCEPTinfo {
  OBJECT                colorSpaceObject;
  TRANSFORM_LINK_INFO   info;
} INTERCEPTinfo;

/** The data pertinent to one entry in the cache of named colorants held within
 * INTERCEPT_NAMEDCOLinfo. For each NamesObject, we hold the alternate space and
 * tint transform returned by the named color databases, if it was found in the
 * databases. If it wasn't found, we can still cache that fact and avoid repeated
 * lookups in the databases.
 * We identify the NamesObject with the set of colorant names in the object. If it
 * is a DeviceN space, there may be more than one colorant. If the colorant is a
 * string we'll convert it to a name. We do this conversion to a set of names
 * because a) storing the original object isn't safe because some jobs poke into
 * the PS string before calling setcolorspace on it again, b) names are immutable
 * during the whole job and can be reliably compared.
 * The store is a store that holds all NamesObject's encountered in the job so
 * far. In the front end, it is only an optimisation, but in the back end it is
 * required to be complete to prevent call outs to the interpreter (which isn't
 * allowed when DL pipelining).
 * We also need to store the saveLevel at which the entry was inserted to allow
 * a safe purge method on restore, since the lifetime of a named color intercept
 * could span many nested save/restores. NB. Since the lifetime of a NamesObject
 * cache may exceed that of a raster style, it is inappropriate to key the
 * NamesObject cache with COLORANT_ID values, so that's another reason to use
 * names.
 */
typedef struct NAMES_OBJECT_STORE NAMES_OBJECT_STORE;
struct NAMES_OBJECT_STORE {
  Bool                present;
  NAMECACHE           *name;
  OBJECT              alternateSpace;
  OBJECT              tintTransform;
  int32               tintTransformId;
  int32               saveLevel;
  NAMES_OBJECT_STORE  *next;
};

/** The structure holding all data about the named color intercepts.
 * It includes a store of colorants and relevant data found in the
 * current named color databases. This is only an optimisation at the front end,
 * but it is required to be complete at the hand-off to the back end because
 * callouts to the interpreter aren't allowed in the back end.
 *
 * New entries can be inserted in any gstate context by virtue of calling
 * setcolorspace with a new colorant, but must also be available to all gstates
 * that share the named color intercept. Therefore, we share this data among
 * gstates with a reference count.
 */
typedef struct INTERCEPT_NAMEDCOLinfo {
  cc_counter_t        refCnt;
  OBJECT              namedColorObject;
  int32               numStoredNamesObjects;
  NAMES_OBJECT_STORE  *namesObjectStore;
} INTERCEPT_NAMEDCOLinfo;

typedef struct ABSTRACT_PROFILE_EXTENSION {
  cc_counter_t        refCnt;
  int32               extendLen;
  TRANSFORM_LINK_INFO extend[1];
} ABSTRACT_PROFILE_EXTENSION;

typedef struct PROFILEinfo {
  TRANSFORM_LINK_INFO         p;
  ABSTRACT_PROFILE_EXTENSION  *extension;
} PROFILEinfo;


typedef struct REPRODUCTIONinfo REPRODUCTIONinfo;
struct REPRODUCTIONinfo {
  cc_counter_t          refCnt;
  Bool                  validReproduction;
  Bool                  firstInSequence;
  COLORSPACE_ID         inputDeviceSpaceId;
  int32                 inputDimensions;
  COLORSPACE_ID         outputDeviceSpaceId;
  int32                 outputDimensions;

  PROFILEinfo           objectProfile[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];
  Bool                  fullProfileData[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];

  int32                 iccIntentMappingNames[SW_CMM_N_ICC_RENDERING_INTENTS];
  int32                 objectIntentNames[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];
  int32                 sourceIntentName;

  Bool                  blackPointComp;

  REPRODUCTIONinfo      *nextDevice;

  TRANSFORM_LINK_INFO   inputColorSpaceInfo[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];
  Bool                  inputColorSpaceIsDeviceLink[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];
  Bool                  inputColorSpaceIsRequired[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];

  INTERCEPTinfo         nullIntercept;
  INTERCEPTinfo         reproProfileAsIntercept;
};

struct GS_HCMSinfo {
  cc_counter_t                refCnt;
  size_t                      structSize;

  /* This group should really be put into an intercept structure */
  uint32                      interceptId;
  OBJECT                      interceptCMYKObj;
  OBJECT                      interceptRGBObj;
  OBJECT                      interceptGrayObj;
  INTERCEPTinfo               interceptCMYK[REPRO_N_TYPES];
  INTERCEPTinfo               interceptRGB[REPRO_N_TYPES];
  INTERCEPTinfo               interceptGray[REPRO_N_TYPES];
  OBJECT                      blendCMYKObj;
  OBJECT                      blendRGBObj;
  OBJECT                      blendGrayObj;
  INTERCEPTinfo               blendCMYK[REPRO_N_TYPES];
  INTERCEPTinfo               blendRGB[REPRO_N_TYPES];
  INTERCEPTinfo               blendGray[REPRO_N_TYPES];
  OBJECT                      sourceCMYKObj;
  OBJECT                      sourceRGBObj;
  OBJECT                      sourceGrayObj;
  INTERCEPTinfo               sourceCMYK[REPRO_N_TYPES];
  INTERCEPTinfo               sourceRGB[REPRO_N_TYPES];
  INTERCEPTinfo               sourceGray[REPRO_N_TYPES];
  OBJECT                      overrideCMYKObj;
  OBJECT                      overrideRGBObj;
  OBJECT                      overrideGrayObj;
  Bool                        overrideCMYK[REPRO_N_TYPES];
  Bool                        overrideRGB[REPRO_N_TYPES];
  Bool                        overrideGray[REPRO_N_TYPES];
  INTERCEPT_NAMEDCOLinfo      *interceptNamedColors;
  Bool                        multipleNamedColors;
  Bool                        overprintPreview;
  Bool                        useBlendSpaceForOutput;
  OBJECT                      interceptBlackObj;
  OBJECT                      interceptBlackTintObj;
  USERVALUE                   interceptBlack[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];
  USERVALUE                   interceptBlackTint[REPRO_N_TYPES][REPRO_N_COLOR_MODELS];
  Bool                        blackTintLuminance;
  Bool                        convertRGBBlack;
  Bool                        useReproductionAsIntercept;


  /* Default profiles for color rendering in the back end.
   */
  INTERCEPTinfo               defaultCMYK;
  INTERCEPTinfo               defaultRGB;
  INTERCEPTinfo               defaultGray;

  /* A group of 'associated' profiles that should be considered part of the
   * original colorspace. These might come from a blend space in the job, but
   * which can't be used directly because an object might be a Separation or
   * DeviceN. The usage is very similar to an intercept for these cases, but it
   * comes from the job rather than the setup. Crucially, the lifetime of an
   * instance is likely to be less than an intercept, so it must be tracked
   * separately from the point of view of the ChainCache.
   */
  INTERCEPTinfo               associatedCMYK;
  INTERCEPTinfo               associatedRGB;
  INTERCEPTinfo               associatedGray;

  /* To indicate we are currently painting a soft mask */
  Bool                        paintingSoftMask;

  /* To control whether we honour ICC profiles within a soft mask.
   * Acrobat X always uses ICC alternate spaces, ignoring the profile. So we
   * have to match that as default behaviour.
   */
  Bool                        poorSoftMask;

  /* An alternate CMM configured from the pagedevice */
  sw_cmm_instance             *alternateCMM;

  sw_cmm_instance             *wcsCMM;

  uint8                       renderingIntent;
  /* Emulate Acrobat's out of spec. behaviour for transparency groups */
  Bool                        adobeRenderingIntent;

  OBJECT                      reproductionObject;
  REPRODUCTIONinfo            reproductionInfo;
  uint32                      reproductionTransformDepth;

  uint8                       treatOneBitImagesAs;
  uint8                       treatSingleRowImagesAs;

  uint8                       requiredReproType[GSC_N_COLOR_TYPES];
  REPRO_COLOR_MODEL           colorModel[GSC_N_COLOR_TYPES];

  /* For optimising loops over multiple color chains */
  Bool                        usingObjectBasedColor;

  /* Some controls for trace messages to limit the number of each type. Doing
   * it this way doesn't limit them to one per job, but is easy and local.
   */
  int32                       trace_devicelinkMismatch;
  int32                       trace_unknownIntent;
};


static int32 renderingIntentNames[GSC_N_RENDERING_INTENTS];
static int32 reproTypeNames[REPRO_N_TYPES];
static int32 reproColorClassNames[REPRO_N_COLOR_MODELS];
static uint8 defaultReproTypes[GSC_N_COLOR_TYPES];
static int reproTypePriority[REPRO_N_TYPES];


static Bool cc_createhcmsinfo( GS_COLORinfo *colorInfo, GS_HCMSinfo **hcmsInfo ) ;

static Bool cc_updatehcmsinfo( GS_COLORinfo *colorInfo, GS_HCMSinfo **hcmsInfo );

static Bool cc_copyhcmsinfo( GS_HCMSinfo *hcmsInfo,
                             GS_HCMSinfo **hcmsInfoCopy );

static void initInterceptInfo( INTERCEPTinfo *pInfo );
static void destroyInterceptInfo( INTERCEPTinfo *cmykInfo );
static void reserveInterceptInfo( INTERCEPTinfo *cmykInfo );

static mps_res_t cc_scanNamedColors(mps_ss_t ss,
                                    INTERCEPT_NAMEDCOLinfo *namedColorInfo);

static void destroyReproductionInfo( REPRODUCTIONinfo  *reproductionInfo );
static void reserveReproductionInfo( REPRODUCTIONinfo  *reproductionInfo );

static PROFILEinfo initProfileList();
static Bool allocProfileList(PROFILEinfo *prof, int32 listLen);
static void destroyProfileList(PROFILEinfo *prof);
static void reserveProfileList(PROFILEinfo *prof);


#if defined( ASSERT_BUILD )
static void interceptInfoAssertions(GS_HCMSinfo *pInfo);
#else
#define interceptInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static uint32 interceptInfoStructSize(void);

static Bool doInterceptDict(OBJECT *interceptDict, GS_COLORinfo *colorInfo);

static Bool interceptDeviceSpace(GS_COLORinfo   *colorInfo,
                                 OBJECT         *interceptObj,
                                 OBJECT         *pInfoObj,
                                 INTERCEPTinfo  *pInfo,
                                 int32          expectedDimension);

static Bool interceptOneColorSpace(GS_COLORinfo   *colorInfo,
                                   OBJECT         *interceptObj,
                                   INTERCEPTinfo  *pInfo,
                                   int32          expectedDimension);

static Bool interceptOverride(OBJECT  *overrideObj,
                              OBJECT  *pInfoObj,
                              Bool    pInfo[REPRO_N_TYPES]);

static Bool createNamedColorInfo(INTERCEPT_NAMEDCOLinfo **namedColorInfo);
static void destroyNamedColorInfo(INTERCEPT_NAMEDCOLinfo **pInfo);
static Bool interceptNamedColors(GS_HCMSinfo  *hcmsInfo,
                                 OBJECT       *namedColorObj);

static void purgeNamedColorantStore(INTERCEPT_NAMEDCOLinfo *namedColorInfo,
                                    int32                  saveLevel);

static Bool interceptBlack(OBJECT     *blackObj,
                           OBJECT     *pInfoObj,
                           USERVALUE  pInfo[REPRO_N_TYPES][REPRO_N_COLOR_MODELS]);

static Bool resetIntercepts(GS_HCMSinfo *hcmsInfo, GS_COLORinfo *colorInfo);

static void initReproductionInfo(REPRODUCTIONinfo *reproductionInfo);

static Bool doReproductionDict(OBJECT           *reproDict,
                               REPRODUCTIONinfo *reproductionInfo,
                               GS_COLORinfo     *colorInfo);

static Bool doProfile(OBJECT            *profileDictObj,
                      REPRODUCTIONinfo  *reproductionInfo,
                      GS_COLORinfo      *colorInfo);
static Bool extractProfileList(OBJECT        *profileObj,
                               GS_COLORinfo  *colorInfo,
                               PROFILEinfo   *linkInfo,
                               COLORSPACE_ID *validDeviceSpace,
                               int32         *dimensions);
static Bool doIntentMappings(OBJECT            *intentsObj,
                             REPRODUCTIONinfo  *reproductionInfo);
static Bool extractIntent(OBJECT *intentObj,
                         int32   *intentName);

static Bool doInputColorSpace(OBJECT            *inputObj,
                              REPRODUCTIONinfo  *reproductionInfo,
                              GS_COLORinfo      *colorInfo);
static Bool extractInputColorSpace(OBJECT               *inputObj,
                                   TRANSFORM_LINK_INFO  *linkInfo,
                                   REPRODUCTIONinfo     *reproductionInfo,
                                   GS_COLORinfo         *colorInfo);

static Bool validIntent(int32 intentNameNumber);
static void convertIntent(int32 intentNameNumber, uint8 *convertedIntent);

static int32 nOutputChannels(TRANSFORM_LINK_INFO *linkInfo);

static void updateUsingObjectBasedColor(GS_HCMSinfo *hcmsInfo);

static Bool checkExcessKeys(OBJECT *inputDict, NAMETYPEMATCH *checkDict);
static Bool checkExcessKeys2(OBJECT *inputDict,
                            NAMETYPEMATCH *checkDict1, NAMETYPEMATCH *checkDict2);

/* ---------------------------------------------------------------------- */

/* Template dictmatch structures that will be reused several times with different
 * allow object types.
 */

/* NB. Ensure that the real repro types are at the front because a loop will
 * iterate over them from 0 to REPRO_N_TYPES.
 */
enum {
  reproType_Picture, reproType_Text, reproType_Vignette, reproType_Other,
  reproType_Default, reproType_n_entries
};
static NAMETYPEMATCH reproType_matchTemplate[reproType_n_entries + 1] = {
  { NAME_Picture | OOPTIONAL,           0, {ONOTHING}},
  { NAME_Text | OOPTIONAL,              0, {ONOTHING}},
  { NAME_Vignette | OOPTIONAL,          0, {ONOTHING}},
  { NAME_Other | OOPTIONAL,             0, {ONOTHING}},
  { NAME_Default | OOPTIONAL,           0, {ONOTHING}},
  DUMMY_END_MATCH
};

/* NB. Ensure that the real color models are at the front because a loop will
 * iterate over them from 0 to REPRO_N_COLOR_MODELS.
 */
enum {
  colorModel_CMYK, colorModel_RGB, colorModel_Gray, colorModel_NamedColor,
  colorModel_CIE, colorModel_Default, colorModel_n_entries
};
static NAMETYPEMATCH colorModel_matchTemplate[colorModel_n_entries + 1] = {
  { NAME_CMYK | OOPTIONAL,              0, {ONOTHING}},
  { NAME_RGB | OOPTIONAL,               0, {ONOTHING}},
  { NAME_Gray | OOPTIONAL,              0, {ONOTHING}},
  { NAME_NamedColor | OOPTIONAL,        0, {ONOTHING}},
  { NAME_CIE | OOPTIONAL,               0, {ONOTHING}},
  { NAME_Default | OOPTIONAL,           0, {ONOTHING}},
  DUMMY_END_MATCH
};

/* ---------------------------------------------------------------------- */

static Bool cc_createhcmsinfo( GS_COLORinfo *colorInfo, GS_HCMSinfo **hcmsInfo )
{
  int32               i;
  GS_HCMSinfo         *pInfo;
  size_t              structSize;

  structSize = interceptInfoStructSize();

  pInfo = mm_sac_alloc(mm_pool_color,
                       structSize,
                       MM_ALLOC_CLASS_NCOLOR);

  *hcmsInfo = pInfo;

  if (pInfo == NULL)
    return error_handler(VMERROR);

  pInfo->refCnt = 1;
  pInfo->structSize = structSize;

  /* These must be initialised outside of resetIntercepts because an attempt would
   * be made to free a non-existant structure.
   */
  pInfo->interceptCMYKObj =
    pInfo->interceptRGBObj =
    pInfo->interceptGrayObj =
    pInfo->blendCMYKObj =
    pInfo->blendRGBObj =
    pInfo->blendGrayObj =
    pInfo->sourceCMYKObj =
    pInfo->sourceRGBObj =
    pInfo->sourceGrayObj =
    pInfo->overrideCMYKObj =
    pInfo->overrideRGBObj =
    pInfo->overrideGrayObj =
    pInfo->interceptBlackObj =
    pInfo->interceptBlackTintObj = onull;       /* Struct copy to set slot properties */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    initInterceptInfo(&pInfo->interceptCMYK[i]);
    initInterceptInfo(&pInfo->interceptRGB[i]);
    initInterceptInfo(&pInfo->interceptGray[i]);
    initInterceptInfo(&pInfo->blendCMYK[i]);
    initInterceptInfo(&pInfo->blendRGB[i]);
    initInterceptInfo(&pInfo->blendGray[i]);
    initInterceptInfo(&pInfo->sourceCMYK[i]);
    initInterceptInfo(&pInfo->sourceRGB[i]);
    initInterceptInfo(&pInfo->sourceGray[i]);
  }

  if (!createNamedColorInfo(&pInfo->interceptNamedColors)) {
    mm_sac_free(mm_pool_color, pInfo, pInfo->structSize);
    return error_handler(VMERROR);
  }

  if (!resetIntercepts(pInfo, colorInfo)) {
    destroyNamedColorInfo(&pInfo->interceptNamedColors);
    mm_sac_free(mm_pool_color, pInfo, pInfo->structSize);
    return FALSE;
  }

  /* These must be initialised outside of resetIntercepts because, although they
   * are intercepts, they are intended to persist after a null setinterceptcolorspace,
   * as they are used in place of CRDs in some cases.
   */
  initInterceptInfo(&pInfo->defaultCMYK);
  initInterceptInfo(&pInfo->defaultRGB);
  initInterceptInfo(&pInfo->defaultGray);

  /* Ditto for associated profiles */
  initInterceptInfo(&pInfo->associatedCMYK);
  initInterceptInfo(&pInfo->associatedRGB);
  initInterceptInfo(&pInfo->associatedGray);

  pInfo->overprintPreview = FALSE;

  pInfo->paintingSoftMask = FALSE;
  pInfo->poorSoftMask = DEFAULT_POOR_SOFT_MASK;

  pInfo->alternateCMM = NULL;

  {
    OBJECT wcsstr = OBJECT_NOTVM_STRING("HQN_CMM_WCS") ;
    pInfo->wcsCMM = cc_findAlternateCMM(&wcsstr) ;
  }

  pInfo->renderingIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
  pInfo->adobeRenderingIntent = FALSE;

  pInfo->reproductionObject = onull ; /* Struct copy to set slot properties */
  initReproductionInfo(&pInfo->reproductionInfo);
  pInfo->reproductionTransformDepth = 0;

  pInfo->treatOneBitImagesAs = REPRO_TYPE_OTHER;
  pInfo->treatSingleRowImagesAs = REPRO_TYPE_VIGNETTE;

  for (i = 0; i < GSC_N_COLOR_TYPES; i++) {
    pInfo->requiredReproType[i] = defaultReproTypes[i];
    pInfo->colorModel[i] = REPRO_COLOR_MODEL_GRAY;
  }

  pInfo->usingObjectBasedColor = FALSE;

#ifdef ASSERT_BUILD
  pInfo->trace_devicelinkMismatch = 0;
  pInfo->trace_unknownIntent = 0;
#endif

  interceptInfoAssertions(pInfo);

  return TRUE;
}

static void freehcmsinfo( GS_HCMSinfo *hcmsInfo )
{
  int32           i;

  interceptInfoAssertions(hcmsInfo);

  /* Remove our claims on these */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    destroyInterceptInfo(&hcmsInfo->interceptCMYK[i]);
    destroyInterceptInfo(&hcmsInfo->interceptRGB[i]);
    destroyInterceptInfo(&hcmsInfo->interceptGray[i]);
    destroyInterceptInfo(&hcmsInfo->blendCMYK[i]);
    destroyInterceptInfo(&hcmsInfo->blendRGB[i]);
    destroyInterceptInfo(&hcmsInfo->blendGray[i]);
    destroyInterceptInfo(&hcmsInfo->sourceCMYK[i]);
    destroyInterceptInfo(&hcmsInfo->sourceRGB[i]);
    destroyInterceptInfo(&hcmsInfo->sourceGray[i]);
  }
  destroyNamedColorInfo(&hcmsInfo->interceptNamedColors);

  destroyInterceptInfo(&hcmsInfo->defaultCMYK);
  destroyInterceptInfo(&hcmsInfo->defaultRGB);
  destroyInterceptInfo(&hcmsInfo->defaultGray);

  destroyInterceptInfo(&hcmsInfo->associatedCMYK);
  destroyInterceptInfo(&hcmsInfo->associatedRGB);
  destroyInterceptInfo(&hcmsInfo->associatedGray);

  destroyReproductionInfo(&hcmsInfo->reproductionInfo);

  mm_sac_free(mm_pool_color, hcmsInfo, hcmsInfo->structSize);
}

void cc_destroyhcmsinfo( GS_HCMSinfo **hcmsInfo )
{
  if ( *hcmsInfo != NULL ) {
    interceptInfoAssertions( *hcmsInfo ) ;
    CLINK_RELEASE( hcmsInfo, freehcmsinfo ) ;
  }
}

void cc_reservehcmsinfo( GS_HCMSinfo *hcmsInfo )
{
  if ( hcmsInfo != NULL ) {
    interceptInfoAssertions( hcmsInfo ) ;
    CLINK_RESERVE( hcmsInfo ) ;
  }
}

static Bool cc_updatehcmsinfo( GS_COLORinfo *colorInfo, GS_HCMSinfo **hcmsInfo )
{
  if ( *hcmsInfo == NULL )
    return cc_createhcmsinfo( colorInfo, hcmsInfo );

  interceptInfoAssertions(*hcmsInfo);

  CLINK_UPDATE(GS_HCMSinfo, hcmsInfo, cc_copyhcmsinfo, freehcmsinfo);
  return TRUE;
}

static Bool cc_copyhcmsinfo( GS_HCMSinfo *hcmsInfo,
                             GS_HCMSinfo **hcmsInfoCopy )
{
  /* When copying an intercept structure for internal use only, we only
   * carry out a shallow copy because the various intercepts are essentially
   * independent of each other when set via setinterceptcolorspace et al.
   */
  int32         i;
  GS_HCMSinfo   *pInfoCopy;

  interceptInfoAssertions(hcmsInfo);

  pInfoCopy = mm_sac_alloc(mm_pool_color,
                           hcmsInfo->structSize,
                           MM_ALLOC_CLASS_NCOLOR );

  if (pInfoCopy == NULL)
    return error_handler(VMERROR);

  *hcmsInfoCopy = pInfoCopy;
  HqMemCpy(pInfoCopy, hcmsInfo, hcmsInfo->structSize);

  pInfoCopy->refCnt = 1;

  /* Update the reference counts for structures hanging off the hcms structure
   */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    reserveInterceptInfo(&pInfoCopy->interceptCMYK[i]);
    reserveInterceptInfo(&pInfoCopy->interceptRGB[i]);
    reserveInterceptInfo(&pInfoCopy->interceptGray[i]);
    reserveInterceptInfo(&pInfoCopy->blendCMYK[i]);
    reserveInterceptInfo(&pInfoCopy->blendRGB[i]);
    reserveInterceptInfo(&pInfoCopy->blendGray[i]);
    reserveInterceptInfo(&pInfoCopy->sourceCMYK[i]);
    reserveInterceptInfo(&pInfoCopy->sourceRGB[i]);
    reserveInterceptInfo(&pInfoCopy->sourceGray[i]);
  }
  CLINK_RESERVE(pInfoCopy->interceptNamedColors);

  reserveInterceptInfo(&pInfoCopy->defaultCMYK);
  reserveInterceptInfo(&pInfoCopy->defaultRGB);
  reserveInterceptInfo(&pInfoCopy->defaultGray);

  reserveInterceptInfo(&pInfoCopy->associatedCMYK);
  reserveInterceptInfo(&pInfoCopy->associatedRGB);
  reserveInterceptInfo(&pInfoCopy->associatedGray);

  reserveReproductionInfo(&pInfoCopy->reproductionInfo);

  return TRUE;
}

Bool cc_arehcmsobjectslocal(corecontext_t *corecontext, GS_HCMSinfo *hcmsInfo )
{
  if ( hcmsInfo == NULL )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&hcmsInfo->interceptCMYKObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->interceptRGBObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->interceptGrayObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->blendCMYKObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->blendRGBObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->blendGrayObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->sourceCMYKObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->sourceRGBObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->sourceGrayObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->overrideCMYKObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->overrideRGBObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->overrideGrayObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->interceptNamedColors->namedColorObject, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->interceptBlackObj, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->interceptBlackTintObj, corecontext) )
    return TRUE ;

  if ( illegalLocalIntoGlobal(&hcmsInfo->defaultCMYK.colorSpaceObject, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->defaultRGB.colorSpaceObject, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->defaultGray.colorSpaceObject, corecontext) )
    return TRUE ;

  if ( illegalLocalIntoGlobal(&hcmsInfo->associatedCMYK.colorSpaceObject, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->associatedRGB.colorSpaceObject, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&hcmsInfo->associatedGray.colorSpaceObject, corecontext) )
    return TRUE ;

  if ( illegalLocalIntoGlobal(&hcmsInfo->reproductionObject, corecontext) )
    return TRUE ;

  return FALSE ;
}


/* cc_scan_hcms - scan GS_HCMSinfo
 *
 * This should match cc_arehcmsobjectslocal, since both need look at
 * all the VM pointers. */
mps_res_t cc_scan_hcms( mps_ss_t ss, GS_HCMSinfo *hcmsInfo )
{
  mps_res_t res;

  if ( hcmsInfo == NULL )
    return MPS_RES_OK;

  res = ps_scan_field( ss, &hcmsInfo->interceptCMYKObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->interceptRGBObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->interceptGrayObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->blendCMYKObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->blendRGBObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->blendGrayObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->sourceCMYKObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->sourceRGBObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->sourceGrayObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->overrideCMYKObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->overrideRGBObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->overrideGrayObj );
  if ( res != MPS_RES_OK ) return res;
  res = cc_scanNamedColors( ss, hcmsInfo->interceptNamedColors );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->interceptBlackObj );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->interceptBlackTintObj );
  if ( res != MPS_RES_OK ) return res;

  res = ps_scan_field( ss, &hcmsInfo->defaultCMYK.colorSpaceObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->defaultRGB.colorSpaceObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->defaultGray.colorSpaceObject );
  if ( res != MPS_RES_OK ) return res;

  res = ps_scan_field( ss, &hcmsInfo->associatedCMYK.colorSpaceObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->associatedRGB.colorSpaceObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &hcmsInfo->associatedGray.colorSpaceObject );
  if ( res != MPS_RES_OK ) return res;

  res = ps_scan_field( ss, &hcmsInfo->reproductionObject );

  return res;
}


static void initInterceptInfo( INTERCEPTinfo *pInfo )
{
  pInfo->colorSpaceObject = onull ; /* Struct copy to set slot properties */
  cc_initTransformInfo(&pInfo->info);
}

static void destroyInterceptInfo( INTERCEPTinfo *pInfo )
{
  pInfo->colorSpaceObject = onull ; /* Struct copy to set slot properties */
  cc_destroyTransformInfo(&pInfo->info);
}

static void reserveInterceptInfo(INTERCEPTinfo *pInfo)
{
  cc_reserveTransformInfo(&pInfo->info);
}


static void destroyReproductionInfo( REPRODUCTIONinfo  *reproductionInfo )
{
  int32 i;
  int32 j;

  HQASSERT(reproductionInfo != NULL, "reproductionInfo NULL");

  if (reproductionInfo->nextDevice != NULL) {
    HQASSERT(!reproductionInfo->nextDevice->firstInSequence,
             "firstInSequence shouldn't be TRUE");
    CLINK_RELEASE(&reproductionInfo->nextDevice, destroyReproductionInfo);
  }

  /* Destroy objectProfile */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++)
      destroyProfileList(&reproductionInfo->objectProfile[i][j]);
  }

  /* Destroy inputColorSpaceInfo */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++)
      cc_destroyTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
  }

  /* The firstInSequence is destroyed as part of hcmsInfo structure */
  if (!reproductionInfo->firstInSequence) {
    mm_sac_free(mm_pool_color, reproductionInfo, sizeof(REPRODUCTIONinfo));
  }
}

static void reserveReproductionInfo( REPRODUCTIONinfo  *reproductionInfo )
{
  int32 i;
  int32 j;

  HQASSERT(reproductionInfo->firstInSequence, "Expected first dict in sequence");

  /* Reserve the rest of the sequence of reproduction dictionaries. NB. The first
   * dictionary is contained by GS_HCMSinfo.
   */
  if (reproductionInfo->nextDevice != NULL)
    CLINK_RESERVE(reproductionInfo->nextDevice);

  /* Reserve objectProfile */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++)
      reserveProfileList(&reproductionInfo->objectProfile[i][j]);
  }

  /* Reserve inputColorSpaceInfo */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++)
      cc_reserveTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
  }
}

static PROFILEinfo initProfileList()
{
  PROFILEinfo prof;
  cc_initTransformInfo(&prof.p);
  prof.extension = NULL;

  return prof;
}

static Bool allocProfileList(PROFILEinfo *prof, int32 listLen)
{
  int32 i;
  int32 extendLen = listLen - 1;

  if (listLen > 1) {
    /* The allocation is 1 slot less than listLen, one slot is already in prof->p,
     * and note one slot is already in the struct hack for prof->e. */
    prof->extension = mm_alloc(mm_pool_color,
                               sizeof(ABSTRACT_PROFILE_EXTENSION) +
                               (extendLen - 1) * sizeof(TRANSFORM_LINK_INFO),
                               MM_ALLOC_CLASS_NCOLOR);
    if (prof->extension == NULL)
      return error_handler(VMERROR);

    prof->extension->refCnt = 1;
    prof->extension->extendLen = extendLen;
    prof->p.next = &prof->extension->extend[0];
    for (i = 0; i < extendLen; i++) {
      cc_initTransformInfo(&prof->extension->extend[i]);

      if (i != extendLen - 1)
        prof->extension->extend[i].next = &prof->extension->extend[i + 1];
    }
  }

  return TRUE;
}

static void freeExtension(ABSTRACT_PROFILE_EXTENSION *extension)
{
  int32 i;

  for (i = 0; i < extension->extendLen; i++)
    cc_destroyTransformInfo(&extension->extend[i]);

  mm_free(mm_pool_color, extension, sizeof(ABSTRACT_PROFILE_EXTENSION) +
          (extension->extendLen - 1) * sizeof(TRANSFORM_LINK_INFO));
}

static void destroyProfileList(PROFILEinfo *prof)
{
  cc_destroyTransformInfo(&prof->p);

  if (prof->extension != NULL)
    CLINK_RELEASE(&prof->extension, freeExtension);
}

static void reserveProfileList(PROFILEinfo *prof)
{
  cc_reserveTransformInfo(&prof->p);

  if (prof->extension != NULL)
    CLINK_RESERVE(prof->extension);
}


static uint32 interceptInfoStructSize(void)
{
  return sizeof(GS_HCMSinfo);         /* intercept info */
}

#if defined( ASSERT_BUILD )
static void interceptInfoAssertions(GS_HCMSinfo *pInfo)
{
  REPRODUCTIONinfo *reproductionInfo;

  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == interceptInfoStructSize(),
           "structure size not correct");

  /** \todo @@JJ WIP - Complete the asserts for PROFILEinfo structures */

  reproductionInfo = &pInfo->reproductionInfo;
  for (;;) {
    reproductionInfo = reproductionInfo->nextDevice;
    if (reproductionInfo == NULL)
      break;
  }
}
#endif

/* ---------------------------------------------------------------------- */

Bool gsc_getOverprintPreview(GS_COLORinfo *colorInfo)
{
  return colorInfo->hcmsInfo->overprintPreview;
}

/* ---------------------------------------------------------------------- */

static Bool isInvertible(COLORSPACE_ID colorSpaceId,
                         TRANSFORM_LINK_INFO_UNION linkData)
{
  Bool invertible;
  Bool inputPresent;
  Bool outputPresent;
  Bool devicelinkPresent;

  switch (colorSpaceId) {
  case SPACE_ICCBased:
    if (!cc_icc_availableModes(linkData.icc,
                               &inputPresent, &outputPresent, &devicelinkPresent))
      return FALSE;

    invertible = inputPresent && outputPresent;
    break;

  case SPACE_CalRGB:
  case SPACE_CalGray:
    HQFAIL("A CalRGB/Gray blend space which should be treated as a device space");
    invertible = FALSE;
    break;

  default:
    invertible = FALSE;
    break;
  }

  return invertible;
}

Bool cc_isInvertible(TRANSFORM_LINK_INFO *linkInfo)
{
  if (linkInfo == NULL)
    return FALSE;

  return isInvertible(linkInfo->inputColorSpaceId, linkInfo->u);
}

Bool gsc_isInvertible(GS_COLORinfo *colorInfo, OBJECT *colorSpace)
{
  Bool invertible;
  COLORSPACE_ID colorSpaceId;
  TRANSFORM_LINK_INFO_UNION linkData;
  int32 dummyN;
  COLORSPACE_ID dummyId;

  if (!gsc_getcolorspacetype(colorSpace, &colorSpaceId))
    return FALSE;

  switch (colorSpaceId) {
  case SPACE_ICCBased:
    if ( !cc_get_iccbased_profile_info(colorInfo, colorSpace, &linkData.icc,
                                       &dummyN, &dummyId, &dummyId))
      return FALSE;

    invertible = isInvertible(colorSpaceId, linkData);
    break;

  case SPACE_CalRGB:
  case SPACE_CalGray:
    HQFAIL("A CalRGB/Gray blend space which should be treated as a device space");
    invertible = FALSE;
    break;

  default:
    invertible = FALSE;
    break;
  }

  return invertible;
}

/* ---------------------------------------------------------------------- */

/* In some configurations it is desirable to not set a Devicexxxx intercept,
 * but we might still want to colour manage the final output conversion using
 * the Blendxxxx intercept instead. One example is in a workflow that wishes to
 * avoid colour managing CMYK, but does wish to colour manage other objects.
 * However, we need some knowledge of the color chain prior to the virtual
 * device, specifically whether colorants were mixed within that chain. Only
 * use the Blendxxxx intercept if there was colorant mixing.
 */
static Bool mayUseBlendSpaceForOutput(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[colorType];
  GS_HCMSinfo   *hcmsInfo;

  HQASSERT(colorInfo, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_getColorModel");

  hcmsInfo = colorInfo->hcmsInfo;
  HQASSERT(hcmsInfo != NULL, "hcmsInfo NULL");

  HQASSERT(BOOL_IS_VALID(colorChain->prevIndependentChannels),
           "Invalid prevIndependentChannels");

  return hcmsInfo->useBlendSpaceForOutput &&
         !colorChain->prevIndependentChannels &&
         !guc_backdropRasterStyle(colorInfo->targetRS);
}

static INTERCEPTinfo *outputAsIntercept(GS_COLORinfo  *colorInfo,
                                        COLORSPACE_ID interceptSpaceId,
                                        uint8         reproType,
                                        Bool          chainIsColorManaged,
                                        Bool          fCompositing,
                                        int           dimensions)
{
  OBJECT *internalSpace;
  GS_HCMSinfo   *hcmsInfo;
  INTERCEPTinfo *reproProfileInfo;
  COLORSPACE_ID tmp;
  REPRO_COLOR_MODEL colorModel;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  hcmsInfo = colorInfo->hcmsInfo;
  HQASSERT(hcmsInfo != NULL, "hcmsInfo NULL");

  if (!gsc_getInternalColorSpace(interceptSpaceId, &internalSpace)) {
    HQFAIL("Should have got a device colorspace");
    return NULL;
  }

  if (!cc_findColorModel(colorInfo, interceptSpaceId, internalSpace,
                         fCompositing, &colorModel)) {
    HQFAIL("Should have got a device intercept");
    return NULL;
  }

  reproProfileInfo = &hcmsInfo->reproductionInfo.reproProfileAsIntercept;
  Copy(&reproProfileInfo->colorSpaceObject, internalSpace);
  reproProfileInfo->info = hcmsInfo->reproductionInfo.objectProfile[reproType][colorModel].p;

  /* BlendInfo requires device space on input, while objectProfile is other
   * way, so swap them.
   */
  tmp = reproProfileInfo->info.inputColorSpaceId;
  reproProfileInfo->info.inputColorSpaceId = reproProfileInfo->info.outputColorSpaceId;
  reproProfileInfo->info.outputColorSpaceId = tmp;

  if (chainIsColorManaged) {
    /* Color managed chain. Without an emulation workflow, the color model
     * already matches the output so no further action is required. But with
     * an emulation workflow we have to apply the conversions in the NextDevice.
     * We do this by returning an intercept to a device space, e.g. DeviceCMYK
     * intercepting to DeviceCMYK. This will be enough to create a valid
     * INTERCEPTinfo stucture and trick the rest of the reproduction machinery
     * into continuing to process the nested NextDevice dictionaries.
     */
    INTERCEPTinfo *nullInterceptInfo = &hcmsInfo->reproductionInfo.nullIntercept;
    initInterceptInfo(nullInterceptInfo);

    if (!interceptOneColorSpace(colorInfo,
                                internalSpace,
                                nullInterceptInfo,
                                dimensions))
      return NULL;

    return nullInterceptInfo;
  }
  else {
    /* Non-color managed chain, so treat the output profile as a blend space
     * intercept. The selection of the profile is made from one of the set of
     * 'objectProfile' but is made pragmatically. We have to do something
     * sensible in order to have good blending with mixed color models.
     */
    return reproProfileInfo;
  }
}

static INTERCEPTinfo *findIntercept(GS_COLORinfo    *colorInfo,
                                    int32           colorType,
                                    COLORSPACE_ID   interceptSpaceId,
                                    uint8           reproType,
                                    Bool            chainIsColorManaged,
                                    Bool            fCompositing,
                                    Bool            isOutput,
                                    Bool            independentChannels)
{
  INTERCEPTinfo *interceptInfo;
  INTERCEPTinfo *blendInfo;
  INTERCEPTinfo *sourceInfo;
  INTERCEPTinfo *associatedInfo;
  INTERCEPTinfo *reproProfileInfo = NULL;
  Bool          interceptOverride;
  int           dimensions;
  GS_HCMSinfo   *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");
  HQASSERT(!chainIsColorManaged || !isOutput,
           "Shouldn't be intercepting to the output device");

  hcmsInfo = colorInfo->hcmsInfo;
  HQASSERT(hcmsInfo != NULL, "hcmsInfo NULL");

  /* Don't apply color management with a None rendering intent */
  if (cc_getrenderingintent(colorInfo) == SW_CMM_INTENT_NONE)
    return NULL;

  switch (interceptSpaceId) {
  case SPACE_DeviceCMYK:
    interceptInfo = &hcmsInfo->interceptCMYK[reproType];
    blendInfo = &hcmsInfo->blendCMYK[reproType];
    sourceInfo = &hcmsInfo->sourceCMYK[reproType];
    associatedInfo = &hcmsInfo->associatedCMYK;
    interceptOverride = hcmsInfo->overrideCMYK[reproType];
    dimensions = 4;
    break;
  case SPACE_DeviceRGB:
    interceptInfo = &hcmsInfo->interceptRGB[reproType];
    blendInfo = &hcmsInfo->blendRGB[reproType];
    sourceInfo = &hcmsInfo->sourceRGB[reproType];
    associatedInfo = &hcmsInfo->associatedRGB;
    interceptOverride = hcmsInfo->overrideRGB[reproType];
    dimensions = 3;
    break;
  case SPACE_DeviceGray:
    interceptInfo = &hcmsInfo->interceptGray[reproType];
    blendInfo = &hcmsInfo->blendGray[reproType];
    sourceInfo = &hcmsInfo->sourceGray[reproType];
    associatedInfo = &hcmsInfo->associatedGray;
    interceptOverride = hcmsInfo->overrideGray[reproType];
    dimensions = 1;
    break;
  default:
    HQFAIL("Invalid interceptSpaceId");
    return NULL;
  }

  if (interceptSpaceId == hcmsInfo->reproductionInfo.outputDeviceSpaceId) {
    reproProfileInfo = outputAsIntercept(colorInfo, interceptSpaceId, reproType,
                                         chainIsColorManaged, fCompositing,
                                         dimensions);
  }

  HQASSERT((interceptInfo->info.inputColorSpaceId != SPACE_notset) ^
           (oType(interceptInfo->colorSpaceObject) == ONULL),
           "Inconsistent interceptInfo");
  HQASSERT((blendInfo->info.inputColorSpaceId != SPACE_notset) ^
           (oType(blendInfo->colorSpaceObject) == ONULL),
           "Inconsistent blendInfo");
  HQASSERT((sourceInfo->info.inputColorSpaceId != SPACE_notset) ^
           (oType(sourceInfo->colorSpaceObject) == ONULL),
           "Inconsistent sourceInfo");
  HQASSERT((associatedInfo->info.inputColorSpaceId != SPACE_notset) ^
           (oType(associatedInfo->colorSpaceObject) == ONULL),
           "Inconsistent associatedInfo");

  if (!chainIsColorManaged) {
    if (interceptInfo->info.inputColorSpaceId != SPACE_notset) {
      /* If we end up using Devicexxx as a default for Blendxxx, then we're not
       * allowed to use devicelink's.
       */
      if (!cc_isInvertible(&interceptInfo->info))
        interceptInfo = NULL;
    }

    /* Set default values if Blendxxxx and/or Sourcexxxx aren't explicitly set.
     */
    if (blendInfo->info.inputColorSpaceId == SPACE_notset)
      blendInfo = interceptInfo;
    if (sourceInfo->info.inputColorSpaceId == SPACE_notset)
      sourceInfo = blendInfo;

    /* We're not color managing this chain, so use the Blendxxxx in preference
     * to the Devicexxxx intercept. The Sourcexxxx is used for the conversion
     * of an object into the first blend space, while Blendxxxx is used for
     * conversion between blend spaces.
     */
    if (fCompositing)
      interceptInfo = blendInfo;
    else
      interceptInfo = sourceInfo;
  }
  else {
    COLORTYPE_ASSERT(colorType, "findIntercept");

    /* The chain is color managed, we may or may not have a device intercept, but do have
     * a blend space. Use that in some rare configurations when backdrop rendering, and
     * the chain so far has independent channels.
     */
    if (mayUseBlendSpaceForOutput(colorInfo, colorType) &&
        blendInfo->info.inputColorSpaceId != SPACE_notset &&
        independentChannels &&
        fCompositing &&
        !isOutput) {
      interceptInfo = blendInfo;
    }
  }

  /* If we're overriding then ignore the associated profile, which comes from
   * job, and use other intercepts instead, if any.
   */
  if (interceptOverride)
    associatedInfo = NULL;

  /* This intercept is for an output profile, so we can't use an associated
   * profile which is only for the front end of a chain.
   */
  if (isOutput)
    associatedInfo = NULL;

  /* We are painting a soft mask, and we have to override ICC profiles with
   * device space alternates.
   */
  if (hcmsInfo->paintingSoftMask && hcmsInfo->poorSoftMask)
    associatedInfo = NULL;

  /* Now arbitrate between the associated profile and the intercept */
  if (associatedInfo != NULL &&
      associatedInfo->info.inputColorSpaceId != SPACE_notset)
    return associatedInfo;
  else if (interceptInfo != NULL &&
           interceptInfo->info.inputColorSpaceId != SPACE_notset)
    return interceptInfo;

  /* There is no explicit intercept. We may be able to inherit the intercept
   * profile from the reproduction dictionary, IFF the color model matches the
   * first reproduction dictionary.
   * NB. if chainIsColorManaged, then reproProfileInfo is the nullInterceptInfo
   *     to enable the color machinery to process emulation workflows.
   */
  if (hcmsInfo->useReproductionAsIntercept &&
      reproProfileInfo != NULL) {
    if (chainIsColorManaged ||
        cc_isInvertible(&reproProfileInfo->info))
      return reproProfileInfo;
  }

  return NULL;
}

OBJECT *cc_getIntercept(GS_COLORinfo    *colorInfo,
                        int32           colorType,
                        COLORSPACE_ID   interceptSpaceId,
                        Bool            chainIsColorManaged,
                        Bool            fCompositing,
                        Bool            independentChannels)
{
  INTERCEPTinfo *interceptInfo;
  uint8  reproType;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "cc_getIntercept");

  reproType = colorInfo->hcmsInfo->requiredReproType[colorType];
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  interceptInfo = findIntercept(colorInfo, colorType, interceptSpaceId, reproType,
                                chainIsColorManaged, fCompositing, FALSE,
                                independentChannels);

  if (interceptInfo == NULL)
    return NULL;
  else {
    HQASSERT(oType(interceptInfo->colorSpaceObject) != ONULL, "null colorspace");
    return &interceptInfo->colorSpaceObject;
  }
}

TRANSFORM_LINK_INFO cc_getInterceptInfo(GS_COLORinfo   *colorInfo,
                                        int32          colorType,
                                        COLORSPACE_ID  interceptSpaceId,
                                        Bool           chainIsColorManaged,
                                        Bool           fCompositing,
                                        Bool           independentChannels)
{
  TRANSFORM_LINK_INFO linkInfo;
  INTERCEPTinfo *interceptInfo;
  uint8 reproType;

  HQASSERT(colorInfo != NULL, "hcmsInfo NULL");
  COLORTYPE_ASSERT(colorType, "cc_getInterceptInfo");

  reproType = colorInfo->hcmsInfo->requiredReproType[colorType];
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  interceptInfo = findIntercept(colorInfo, colorType, interceptSpaceId, reproType,
                                chainIsColorManaged, fCompositing, FALSE,
                                independentChannels);

  if (interceptInfo == NULL)
    cc_initTransformInfo(&linkInfo);
  else
    linkInfo = interceptInfo->info;

  return linkInfo;
}

/* ---------------------------------------------------------------------- */

OBJECT *gsc_getBlend(GS_COLORinfo   *colorInfo,
                     COLORSPACE_ID  blendSpaceId,
                     uint8          reproType)
{
  INTERCEPTinfo *blendInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  blendInfo = findIntercept(colorInfo, GSC_ILLEGAL, blendSpaceId, reproType,
                            FALSE, TRUE, TRUE, TRUE);

  if (blendInfo == NULL)
    return NULL;
  else {
    HQASSERT(oType(blendInfo->colorSpaceObject) != ONULL, "null colorspace");
    return &blendInfo->colorSpaceObject;
  }
}

TRANSFORM_LINK_INFO cc_getBlendInfo(GS_COLORinfo   *colorInfo,
                                    int32          colorType,
                                    COLORSPACE_ID  blendSpaceId)
{
  TRANSFORM_LINK_INFO linkInfo;
  INTERCEPTinfo *blendInfo;
  uint8 reproType;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "cc_getBlendInfo");

  reproType = colorInfo->hcmsInfo->requiredReproType[colorType];
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  blendInfo = findIntercept(colorInfo, GSC_ILLEGAL, blendSpaceId, reproType,
                            FALSE, TRUE, TRUE, TRUE);

  if (blendInfo == NULL)
    cc_initTransformInfo(&linkInfo);
  else
    linkInfo = blendInfo->info;

  return linkInfo;
}

TRANSFORM_LINK_INFO cc_getDefaultInfo(GS_COLORinfo   *colorInfo,
                                      COLORSPACE_ID  blendSpaceId)
{
  INTERCEPTinfo *defaultInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");

  switch (blendSpaceId){
  case SPACE_DeviceCMYK:
  case SPACE_DeviceN:
    defaultInfo = &colorInfo->hcmsInfo->defaultCMYK;
    break;
  case SPACE_DeviceRGB:
    defaultInfo = &colorInfo->hcmsInfo->defaultRGB;
    break;
  case SPACE_DeviceGray:
    defaultInfo = &colorInfo->hcmsInfo->defaultGray;
    break;
  default:
    HQFAIL("Unexpected blendSpaceId");
    defaultInfo = NULL;
  }

  HQASSERT(defaultInfo->info.inputColorSpaceId != SPACE_notset &&
           oType(defaultInfo->colorSpaceObject) != ONULL,
           "Uninitialised default profile");

  return defaultInfo->info;
}

/* Find out if we have an effective blend space, (or at least an
 * intercept space that can be used as such).
 */
Bool gsc_getBlendInfoSet(GS_COLORinfo *colorInfo,
                         COLORSPACE_ID blendSpaceId)
{
  return (gsc_getBlend(colorInfo, blendSpaceId, REPRO_TYPE_OTHER) != NULL);
}

/* ---------------------------------------------------------------------- */

Bool cc_getColorSpaceOverride(GS_HCMSinfo    *hcmsInfo,
                              COLORSPACE_ID  colorSpaceId,
                              int32          colorType)
{
  Bool value;
  uint8 reproType;

  HQASSERT(hcmsInfo != NULL, "hcmsInfo should not be NULL");
  COLORTYPE_ASSERT(colorType, "cc_getColorSpaceOverride");

  reproType = hcmsInfo->requiredReproType[colorType];

  switch (colorSpaceId) {
  case SPACE_DeviceCMYK:
    value = hcmsInfo->overrideCMYK[reproType];
    break;
  case SPACE_DeviceRGB:
    value = hcmsInfo->overrideRGB[reproType];
    break;
  case SPACE_DeviceGray:
    value = hcmsInfo->overrideGray[reproType];
    break;
  default:
    HQFAIL("Invalid colorSpaceId");
    value = FALSE;
    break;
  }

  return value;
}

/* This function is a hack that allows the tiff code to use setinterceptcolorspace
 * to set, say, a CMYK profile for a DeviceN image. It should be disposed of
 * when 29787 is done because we should then be able to set the colorspace directly
 * and use the override in the color module the same as everything else.
 */
Bool gsc_getColorSpaceOverride(GS_COLORinfo *colorInfo,
                               COLORSPACE_ID colorSpaceId)
{
  HQASSERT(colorInfo != NULL, "colorInfo NULL");

  return cc_getColorSpaceOverride(colorInfo->hcmsInfo,
                                  colorSpaceId,
                                  GSC_IMAGE);
}

OBJECT *gsc_getNamedColorIntercept(GS_COLORinfo *colorInfo)
{
  GS_HCMSinfo *hcmsInfo;
  OBJECT *theo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  hcmsInfo = colorInfo->hcmsInfo;
  HQASSERT(hcmsInfo != NULL, "hcmsInfo should not be NULL");

  /* Don't apply color management with a None rendering intent */
  if (cc_getrenderingintent(colorInfo) == SW_CMM_INTENT_NONE)
    return NULL;

  theo = &hcmsInfo->interceptNamedColors->namedColorObject;
  if ( oType(*theo) == ONULL || theLen(*theo) == 0 )
    return NULL;
  else
    return theo;
}

Bool cc_getBlackIntercept(GS_HCMSinfo *hcmsInfo, int32 colorType)
{
  uint8  reproType;
  REPRO_COLOR_MODEL colorModel;

  HQASSERT(hcmsInfo != NULL, "hcmsInfo should not be NULL");
  COLORTYPE_ASSERT(colorType, "cc_getBlackIntercept");

  reproType = hcmsInfo->requiredReproType[colorType];
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  colorModel = hcmsInfo->colorModel[colorType];
  HQASSERT(colorModel < REPRO_N_COLOR_MODELS, "Invalid colorModel");

  /* 100% black preservation is only supported for these reproType's */
  if (reproType != REPRO_TYPE_TEXT &&
      reproType != REPRO_TYPE_OTHER)
    return TRUE;

  /* 1 bit images could be marked as linework. We don't preserve black on these
   * because it's proved too difficult to get this right.
   */
  if (colorType == GSC_IMAGE)
    return TRUE;

  if (hcmsInfo->interceptBlack[reproType][colorModel] == BLACK_TRUE)
    return TRUE;
  else {
    HQASSERT(hcmsInfo->interceptBlack[reproType][colorModel] == BLACK_FALSE,
             "Unexpected black value");
    return FALSE;
  }
}

USERVALUE cc_getBlackTintIntercept(GS_HCMSinfo *hcmsInfo, int32 colorType)
{
  uint8  reproType;
  REPRO_COLOR_MODEL colorModel;

  HQASSERT(hcmsInfo != NULL, "hcmsInfo should not be NULL");
  COLORTYPE_ASSERT(colorType, "cc_getBlackTintIntercept");

  reproType = hcmsInfo->requiredReproType[colorType];
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  colorModel = hcmsInfo->colorModel[colorType];
  HQASSERT(colorModel < REPRO_N_COLOR_MODELS, "Invalid colorModel");

  /* Black tint preservation is only supported for these reproType's */
  /** \todo JJ - This should work with shadings + images, but haven't worked out how. */
  if (reproType != REPRO_TYPE_TEXT &&
      reproType != REPRO_TYPE_OTHER)
    return BLACK_TRUE;

  /* 1 bit images could be marked as linework. We don't preserve black on these
   * because it's proved too difficult to get this right.
   */
  if (colorType == GSC_IMAGE)
    return BLACK_TRUE;

  return hcmsInfo->interceptBlackTint[reproType][colorModel];
}

Bool cc_getBlackTintLuminance(GS_HCMSinfo *hcmsInfo)
{
  if (hcmsInfo == NULL)
    return FALSE;

  return hcmsInfo->blackTintLuminance;
}

Bool cc_getConvertRGBBlack(GS_HCMSinfo *hcmsInfo, int32 colorType)
{
  uint8  reproType;
  REPRO_COLOR_MODEL colorModel;

  if (hcmsInfo == NULL)
    return FALSE;

  HQASSERT(hcmsInfo != NULL, "hcmsInfo should not be NULL");
  COLORTYPE_ASSERT(colorType, "cc_getBlackIntercept");

  reproType = hcmsInfo->requiredReproType[colorType];
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  colorModel = hcmsInfo->colorModel[colorType];
  HQASSERT(colorModel < REPRO_N_COLOR_MODELS, "Invalid colorModel");

  /* ConvertRGBBlack is only supported for these reproType's */
  if (reproType != REPRO_TYPE_TEXT &&
      reproType != REPRO_TYPE_OTHER)
    return FALSE;

  /* ConvertRGBBlack is only supported for RGB */
  if (colorModel != REPRO_COLOR_MODEL_RGB)
    return FALSE;

  return hcmsInfo->convertRGBBlack;
}

Bool cc_getMultipleNamedColors(GS_HCMSinfo *hcmsInfo)
{
  if (hcmsInfo == NULL)
    return FALSE;

  return hcmsInfo->multipleNamedColors;
}

/* ---------------------------------------------------------------------- */

/* Implements the setinterceptcolorspace operator.
 * If a key does not appear in any particular call to setinterceptcolorspace
 * then it is left unchanged.
 */
Bool gsc_setinterceptcolorspace(GS_COLORinfo *colorInfo, OBJECT *icsDict)
{
  GS_HCMSinfo     *oldHcmsInfo;
  Bool            result = FALSE;

  /* We accept a dictionary, whose keys will be merged with the existing data.
   * Or a null which (re)sets all intercepts to default values.
   */
  if (oType(*icsDict) != ODICTIONARY && oType(*icsDict) != ONULL)
    return error_handler( TYPECHECK ) ;

  oldHcmsInfo = colorInfo->hcmsInfo;
  cc_reservehcmsinfo( oldHcmsInfo );

  /* If 'icsDict' is a null object, we reset all the intercepts to default values
   * in which case we can preserve color chains in the ChainCache. If we have
   * a new intercept dictionary it will be too difficult to check that all
   * sub-dictionaries are the same as ones we've seen before so we'll tell the
   * ChainCache to flush itself from the current gstate.
   * We'll know that intercepts are different because 'interceptId' will say so.
   */
  if (oType(*icsDict) == ONULL) {
    if (cc_updatehcmsinfo( colorInfo, &colorInfo->hcmsInfo ) &&
        cc_invalidateColorChains( colorInfo, FALSE )) {
      result = resetIntercepts(colorInfo->hcmsInfo, colorInfo);
    }
  }
  else {
    if (cc_updatehcmsinfo( colorInfo, &colorInfo->hcmsInfo ) &&
        cc_invalidateColorChains( colorInfo, TRUE )) {
      HQASSERT(oType(*icsDict) == ODICTIONARY, "icsDict isn't a dict");
      result = doInterceptDict(icsDict, colorInfo);
    }
  }

  if (result) {
    /* Get rid of the saved info */
    cc_destroyhcmsinfo(&oldHcmsInfo);

    updateUsingObjectBasedColor(colorInfo->hcmsInfo);
  }
  else {
    /* Reinstate the saved info */
    cc_destroyhcmsinfo(&colorInfo->hcmsInfo);
    colorInfo->hcmsInfo = oldHcmsInfo;
  }

  return result;
}

/* ---------------------------------------------------------------------- */

enum {
  intercept_DeviceCMYK, intercept_DeviceRGB, intercept_DeviceGray,
  intercept_BlendCMYK, intercept_BlendRGB, intercept_BlendGray,
  intercept_SourceCMYK, intercept_SourceRGB, intercept_SourceGray,
  intercept_OverrideCMYK, intercept_OverrideRGB, intercept_OverrideGray,
  intercept_NamedColor, intercept_MultipleNamedColors,
  intercept_OverprintPreview, intercept_UseBlendSpaceForOutput,
  intercept_Black, intercept_BlackTint,
  intercept_BlackTintLuminance, intercept_ConvertRGBBlack,
  intercept_DefaultCMYK, intercept_DefaultRGB, intercept_DefaultGray,
  intercept_n_entries
};
static NAMETYPEMATCH intercept_match[intercept_n_entries + 1] = {
  { NAME_DeviceCMYK | OOPTIONAL,              4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_DeviceRGB | OOPTIONAL,               4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_DeviceGray | OOPTIONAL,              4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_BlendCMYK | OOPTIONAL,               4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_BlendRGB | OOPTIONAL,                4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_BlendGray | OOPTIONAL,               4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_SourceCMYK | OOPTIONAL,              4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_SourceRGB | OOPTIONAL,               4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_SourceGray | OOPTIONAL,              4, { ODICTIONARY, OARRAY, ONAME, ONULL }},
  { NAME_OverrideCMYK | OOPTIONAL,            2, { ODICTIONARY, OBOOLEAN }},
  { NAME_OverrideRGB | OOPTIONAL,             2, { ODICTIONARY, OBOOLEAN }},
  { NAME_OverrideGray | OOPTIONAL,            2, { ODICTIONARY, OBOOLEAN }},
  { NAME_NamedColor | OOPTIONAL,              2, { OARRAY, ONULL }},
  { NAME_MultipleNamedColors | OOPTIONAL,     1, { OBOOLEAN }},
  { NAME_OverprintPreview | OOPTIONAL,        1, { OBOOLEAN }},
  { NAME_UseBlendSpaceForOutput | OOPTIONAL,  1, { OBOOLEAN }},
  { NAME_Black | OOPTIONAL,                   2, { ODICTIONARY, OBOOLEAN }},
  { NAME_BlackTint | OOPTIONAL,               3, { ODICTIONARY, OBOOLEAN, OREAL }},
  { NAME_BlackTintLuminance | OOPTIONAL,      1, { OBOOLEAN }},
  { NAME_ConvertRGBBlack | OOPTIONAL,         1, { OBOOLEAN }},
  { NAME_DefaultCMYK | OOPTIONAL,             3, { OARRAY }},
  { NAME_DefaultRGB | OOPTIONAL,              3, { OARRAY }},
  { NAME_DefaultGray | OOPTIONAL,             3, { OARRAY }},
  DUMMY_END_MATCH
};

static Bool doInterceptDict(OBJECT *interceptDict, GS_COLORinfo *colorInfo)
{
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;
  OBJECT *theo;

  HQASSERT(CLINK_OWNER(hcmsInfo), "Can't change shared hcmsInfo");

  if (!checkExcessKeys(interceptDict, intercept_match))
    return FALSE;

  if (!dictmatch(interceptDict, intercept_match))
    return FALSE;

  /* We have a new set of intercepts which mustn't get confused in the ChainCache */
  hcmsInfo->interceptId = colorInfo->colorState->hcmsInterceptId++;
  if (hcmsInfo->interceptId == INTERCEPT_ID_MAX)
    hcmsInfo->interceptId = INTERCEPT_ID_INITIAL;

  theo = intercept_match[intercept_DeviceCMYK].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->interceptCMYKObj,
                              hcmsInfo->interceptCMYK,
                              4))
      return FALSE;
  }

  theo = intercept_match[intercept_DeviceRGB].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->interceptRGBObj,
                              hcmsInfo->interceptRGB,
                              3))
      return FALSE;
  }

  theo = intercept_match[intercept_DeviceGray].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->interceptGrayObj,
                              hcmsInfo->interceptGray,
                              1))
      return FALSE;
  }

  theo = intercept_match[intercept_BlendCMYK].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->blendCMYKObj,
                              hcmsInfo->blendCMYK,
                              4))
      return FALSE;
  }

  theo = intercept_match[intercept_BlendRGB].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->blendRGBObj,
                              hcmsInfo->blendRGB,
                              3))
      return FALSE;
  }

  theo = intercept_match[intercept_BlendGray].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->blendGrayObj,
                              hcmsInfo->blendGray,
                              1))
      return FALSE;
  }

  theo = intercept_match[intercept_SourceCMYK].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->sourceCMYKObj,
                              hcmsInfo->sourceCMYK,
                              4))
      return FALSE;
  }

  theo = intercept_match[intercept_SourceRGB].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->sourceRGBObj,
                              hcmsInfo->sourceRGB,
                              3))
      return FALSE;
  }

  theo = intercept_match[intercept_SourceGray].result;
  if (theo != NULL) {
    if (!interceptDeviceSpace(colorInfo, theo,
                              &hcmsInfo->sourceGrayObj,
                              hcmsInfo->sourceGray,
                              1))
      return FALSE;
  }

  theo = intercept_match[intercept_OverrideCMYK].result;
  if (theo != NULL) {
    if (!interceptOverride(theo, &hcmsInfo->overrideCMYKObj, hcmsInfo->overrideCMYK))
      return FALSE;
  }

  theo = intercept_match[intercept_OverrideRGB].result;
  if (theo != NULL) {
    if (!interceptOverride(theo, &hcmsInfo->overrideRGBObj, hcmsInfo->overrideRGB))
      return FALSE;
  }

  theo = intercept_match[intercept_OverrideGray].result;
  if (theo != NULL) {
    if (!interceptOverride(theo, &hcmsInfo->overrideGrayObj, hcmsInfo->overrideGray))
      return FALSE;
  }

  theo = intercept_match[intercept_NamedColor].result;
  if (theo != NULL) {
    if (!interceptNamedColors(hcmsInfo, theo))
      return FALSE;
  }

  theo = intercept_match[intercept_MultipleNamedColors].result;
  if (theo != NULL) {
    hcmsInfo->multipleNamedColors = oBool( *theo );
  }

  theo = intercept_match[intercept_OverprintPreview].result;
  if (theo != NULL) {
    hcmsInfo->overprintPreview = oBool( *theo );
  }

  theo = intercept_match[intercept_UseBlendSpaceForOutput].result;
  if (theo != NULL) {
    hcmsInfo->useBlendSpaceForOutput = oBool( *theo );
  }

  theo = intercept_match[intercept_Black].result;
  if (theo != NULL) {
    if (!interceptBlack(theo, &hcmsInfo->interceptBlackObj, hcmsInfo->interceptBlack))
      return FALSE;
  }

  theo = intercept_match[intercept_BlackTint].result;
  if (theo != NULL) {
    if (!interceptBlack(theo, &hcmsInfo->interceptBlackTintObj, hcmsInfo->interceptBlackTint))
      return FALSE;
  }

  theo = intercept_match[intercept_BlackTintLuminance].result;
  if (theo != NULL) {
    hcmsInfo->blackTintLuminance = oBool( *theo ) ;
  }

  theo = intercept_match[intercept_ConvertRGBBlack].result;
  if (theo != NULL) {
    hcmsInfo->convertRGBBlack = oBool( *theo ) ;
  }


  /* NB. This set of default rendering spaces must use an ICC profile. */
  theo = intercept_match[intercept_DefaultCMYK].result;
  if (theo != NULL) {
    if (!interceptOneColorSpace(colorInfo, theo,
                                &hcmsInfo->defaultCMYK,
                                4))
      return FALSE;
  }
  theo = intercept_match[intercept_DefaultRGB].result;
  if (theo != NULL) {
    if (!interceptOneColorSpace(colorInfo, theo,
                                &hcmsInfo->defaultRGB,
                                3))
      return FALSE;
  }
  theo = intercept_match[intercept_DefaultGray].result;
  if (theo != NULL) {
    if (!interceptOneColorSpace(colorInfo, theo,
                                &hcmsInfo->defaultGray,
                                1))
      return FALSE;
  }

  return TRUE ;
}

/* Handler for [Intercept | Blend][CMYK | RGB | Gray] keys of setinterceptcolorspace.
 * We expect one of two forms for the value. One is a valid colorspace array
 * which will be used for all intercepts of that device space.
 * The other is a dictionary object containing optional keys of Default, Picture,
 * Text, Vignette, and Other. This allows intercepts to be sub-classed by reproType.
 * At the end of the process, we will have an INTERCEPTinfo associated with
 * each of the 5 reproduction types.
 */
static Bool interceptDeviceSpace(GS_COLORinfo   *colorInfo,
                                 OBJECT         *interceptObj,
                                 OBJECT         *pInfoObj,
                                 INTERCEPTinfo  *pInfo,
                                 int32          expectedDimension)
{
  int32 i;

  /* Copy the top level object for currentinterceptcolorspace */
  Copy(pInfoObj, interceptObj);

  if (oType(*interceptObj) != ODICTIONARY) {
    /* Intercept of DeviceCMYK/RGB/Gray isn't a dictionary, so the intercept will
     * be the same for all reproType's.
     */
    INTERCEPTinfo defaultIntercept;
    initInterceptInfo(&defaultIntercept);

    if (!interceptOneColorSpace(colorInfo, interceptObj,
                                &defaultIntercept,
                                expectedDimension))
      return FALSE;

    /* Initialise all profiles with the default */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      destroyInterceptInfo(&pInfo[i]);
      pInfo[i] = defaultIntercept;
      reserveInterceptInfo(&defaultIntercept);
    }
    /* And pop off one reserve level so the first one doesn't count twice */
    destroyInterceptInfo(&defaultIntercept);
  }
  else {
    NAMETYPEMATCH reproType_match[reproType_n_entries + 1];

    /* Copy reproType_match from reproType_matchTemplate to allow specialisation */
    HqMemCpy(reproType_match, reproType_matchTemplate, sizeof(reproType_matchTemplate));
    for (i = 0; i < reproType_n_entries; i++) {
      reproType_match[i].count = 2;
      reproType_match[i].match[0] = OARRAY;
      reproType_match[i].match[1] = ONAME;
    }

    if (!checkExcessKeys(interceptObj, reproType_match))
      return FALSE;

    if (!dictmatch(interceptObj, reproType_match))
      return FALSE;

    /* Extract the Default, if present, and initialise all values to it */
    if (reproType_match[reproType_Default].result != NULL) {
      INTERCEPTinfo defaultIntercept;
      initInterceptInfo(&defaultIntercept);

      if (!interceptOneColorSpace(colorInfo,
                                  reproType_match[reproType_Default].result,
                                  &defaultIntercept,
                                  expectedDimension))
        return FALSE;

      /* Initialise all profiles with the default */
      for (i = 0; i < REPRO_N_TYPES; i++) {
        destroyInterceptInfo(&pInfo[i]);
        pInfo[i] = defaultIntercept;
        reserveInterceptInfo(&defaultIntercept);
      }
      /* And pop off one reserve level so the first one doesn't count twice */
      destroyInterceptInfo(&defaultIntercept);
    }

    /* Loop over the reproduction types, which are the remaining valid keys, and
     * extract the values.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      if (reproType_match[i].result != NULL) {
        if (!interceptOneColorSpace(colorInfo,
                                    reproType_match[i].result,
                                    &pInfo[i],
                                    expectedDimension))
          return FALSE;
      }
    }
  }

  return TRUE;
}

/* Handler for individual entries within the InputColorSpace dictionary.
 * The interceptObj should be null or a valid color space.
 */
static Bool interceptOneColorSpace(GS_COLORinfo   *colorInfo,
                                   OBJECT         *interceptObj,
                                   INTERCEPTinfo  *pInfo,
                                   int32          expectedDimension)
{
  COLORSPACE_ID colorspaceID;
  int32 dimension;

  if (oType(*interceptObj) == ONULL) {
    colorspaceID = SPACE_notset;
    dimension = 0;
  }
  else {
    if (!gsc_getcolorspacesizeandtype(colorInfo, interceptObj,
                                      &colorspaceID, &dimension))
      return FALSE ;    /* Stop further iterations. */
  }

  if ((dimension != expectedDimension) && (colorspaceID != SPACE_notset)) {
    return detail_error_handler(RANGECHECK,
                                "Incorrect number of components in intercept space");
  }

  /* Remove any intercept data and create new structures if necessary.
   */
  destroyInterceptInfo(pInfo);

  Copy(&pInfo->colorSpaceObject, interceptObj);

  if (colorspaceID != SPACE_notset) {
    if (!cc_createTransformInfo(colorInfo,
                                &pInfo->info,
                                interceptObj))
      return FALSE ;
  }
  else
    initInterceptInfo(pInfo);

  return TRUE;
}

/* Handler for OverrideCMYK /RGB /Gray keys of setinterceptcolorspace.
 * We expect one of two forms for the value. One is a boolean which will be
 * used for all reproTypes.
 * The other is a dictionary object containing optional keys of Default, Picture,
 * Text, Vignette, and Other. This allows 'pInfo' to be sub-classed by reproType.
 * At the end of the process, we will have a boolean value associated with each
 * of the 4 reproduction types.
 */
static Bool interceptOverride(OBJECT  *overrideObj,
                              OBJECT  *pInfoObj,
                              Bool    pInfo[REPRO_N_TYPES])
{
  int32 i;

  /* Copy the top level object for currentinterceptcolorspace */
  Copy(pInfoObj, overrideObj);

  if (oType(*overrideObj) != ODICTIONARY) {
    HQASSERT(oType(*overrideObj) == OBOOLEAN, "Expected a boolean object");

    /* Override of DeviceCMYK/RGB/Gray is a boolean, so the override will
     * be the same for all reproType's.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      pInfo[i] = oBool(*overrideObj);
    }
  }
  else {
    NAMETYPEMATCH reproType_match[reproType_n_entries + 1];

    /* Copy reproType_match from reproType_matchTemplate to allow specialisation */
    HqMemCpy(reproType_match, reproType_matchTemplate, sizeof(reproType_matchTemplate));
    for (i = 0; i < reproType_n_entries; i++) {
      reproType_match[i].count = 1;
      reproType_match[i].match[0] = OBOOLEAN;
    }

    if (!checkExcessKeys(overrideObj, reproType_match))
      return FALSE;

    /* Extract the Default, if present, and initialise all values to it */
    if (!dictmatch(overrideObj, reproType_match))
      return FALSE;

    if (reproType_match[reproType_Default].result != NULL) {
      for (i = 0; i < REPRO_N_TYPES; i++) {
        pInfo[i] = oBool(*reproType_match[reproType_Default].result);
      }
    }

    /* Loop over the reproduction types, which are the remaining valid keys, and
     * extract the values.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      if (reproType_match[i].result != NULL)
        pInfo[i] = oBool(*reproType_match[i].result);
    }
  }

  return TRUE;
}

static Bool createNamedColorInfo(INTERCEPT_NAMEDCOLinfo **namedColorInfo)
{
  INTERCEPT_NAMEDCOLinfo *pInfo;

  pInfo = mm_sac_alloc(mm_pool_color,
                       sizeof (INTERCEPT_NAMEDCOLinfo),
                       MM_ALLOC_CLASS_NCOLOR);
  if (pInfo == NULL)
    return error_handler(VMERROR);

  pInfo->refCnt = 1;
  pInfo->namedColorObject = onull;      /* Struct copy to set slot properties */
  pInfo->numStoredNamesObjects = 0;
  pInfo->namesObjectStore = NULL;

  *namedColorInfo = pInfo;

  return TRUE;
}

static void freeNamedColorInfo(INTERCEPT_NAMEDCOLinfo *pInfo)
{
  /* Purge all named color entries in the store */
  purgeNamedColorantStore(pInfo, -1);
  mm_sac_free(mm_pool_color, pInfo, sizeof(INTERCEPT_NAMEDCOLinfo));
}

static void destroyNamedColorInfo(INTERCEPT_NAMEDCOLinfo **pInfo)
{
  if (*pInfo != NULL)
    CLINK_RELEASE(pInfo, freeNamedColorInfo);
}

/** Replace a namedColorInfo with a newly initialised structure */
static Bool replaceNamedColorInfo(INTERCEPT_NAMEDCOLinfo **namedColorInfo)
{
  INTERCEPT_NAMEDCOLinfo *pInfo = NULL;

  if (!createNamedColorInfo(&pInfo))
    return FALSE;

  destroyNamedColorInfo(namedColorInfo);
  *namedColorInfo = pInfo;

  return TRUE;
}

/* Handler for NamedColor key of setinterceptcolorspace */
static Bool interceptNamedColors(GS_HCMSinfo  *hcmsInfo,
                                 OBJECT       *namedColorObj)
{
  STACK_POSITIONS stackPositions;

  /* Do the error checking */
  if (oType(*namedColorObj) == OARRAY) {
    OBJECT *one_name = oArray(*namedColorObj);
    int32 n = theLen(*namedColorObj);
    int32 i;

    for (i = 0; i < n; i++) {
      if ( oType(one_name[i]) != ONAME && oType(one_name[i]) != OSTRING )
        return error_handler(TYPECHECK);

      /* Force the early loading of all named color resources to avoid reloading
       * them potentially multiple times, and to avoid an interpreter call in
       * the back end (not allowed).
       */
      saveStackPositions(&stackPositions);
      if (!push(&one_name[i], &operandstack) ||
          !run_ps_string((uint8 *) "/NamedColor findresource pop")) {
        (void) restoreStackPositions(&stackPositions, FALSE);
        error_clear();
        return errorinfo_error_handler(UNDEFINEDRESOURCE, &one_name[i], &onull);
      }
    }
  }
  else if (oType(*namedColorObj) != ONULL)
    return error_handler(TYPECHECK);

  /* NB. Unlike most other color structures, this one isn't updated, but replaced,
   * that's due to difficulties around reference counting namesObjectStore, and
   * the fact that it should be initialised when the namedColorObject changes.
   * We only want to copy the namedColorObj.
   */
  if (!replaceNamedColorInfo(&hcmsInfo->interceptNamedColors))
    return FALSE;
  Copy(&hcmsInfo->interceptNamedColors->namedColorObject, namedColorObj);

  return TRUE;
}

static mps_res_t cc_scanNamedColors(mps_ss_t ss,
                                    INTERCEPT_NAMEDCOLinfo *namedColorInfo)
{
  mps_res_t res;
  NAMES_OBJECT_STORE *storeEntry;

  res = ps_scan_field(ss, &namedColorInfo->namedColorObject);
  if ( res != MPS_RES_OK )
    return res;

  for (storeEntry = namedColorInfo->namesObjectStore;
       storeEntry != NULL;
       storeEntry = storeEntry->next) {
    res = ps_scan_field(ss, &storeEntry->alternateSpace);
    if ( res != MPS_RES_OK )
      return res;
    res = ps_scan_field(ss, &storeEntry->tintTransform);
    if ( res != MPS_RES_OK )
      return res;
  }

  return res;
}

/** Obtains the number of name store entries for colorants in the namesObject.
 * This function was factored out of getNames() because one client needs to
 * allocate a names array.
 */
int cc_getNumNames(OBJECT *namesObject)
{
  int numNames;

  if (oType(*namesObject) == ONAME)
    numNames = 1;
  else if (oType(*namesObject) == OSTRING)
    numNames = 1;
  else {
    HQASSERT(oType(*namesObject) == OPACKEDARRAY ||
             oType(*namesObject) == OARRAY,
             "Invalid type in named color intercept");
    numNames = theLen(*namesObject);
  }

  return numNames;
}

/** Obtains the set of name store entries for colorants in the namesObject. This
 * will identify the namesObject when used for lookup or insertion in the store.
 * names is assumed to be an array of numNames, which is populated with the names
 * from namesObject. numNames must be consistent with namesObject.
 */
void cc_getNames(OBJECT *namesObject, NAMECACHE **names, int numNames)
{
  int i;

  if (oType(*namesObject) == ONAME) {
    HQASSERT(numNames == 1, "Inconsistent numNames");
    names[0] = oName(*namesObject);
  }
  else if (oType(*namesObject) == OSTRING) {
    HQASSERT(numNames == 1, "Inconsistent numNames");
    names[0] = cachename(oString(*namesObject), theLen(*namesObject));
  }
  else {
    HQASSERT(oType(*namesObject) == OPACKEDARRAY ||
             oType(*namesObject) == OARRAY,
             "Invalid type in named color intercept");
    HQASSERT(numNames == theLen(*namesObject), "Inconsistent numNames");

    /* We should have aborted DeviceN spaces with too many colorants */
    HQASSERT(numNames <= BLIT_MAX_COLOR_CHANNELS, "numNames too large");

    for (i = 0; i < numNames; i++) {
      OBJECT *colorant = &oArray(*namesObject)[i];

      if (oType(*colorant) == OSTRING)
        names[i] = cachename(oString(*colorant), (uint32) theLen(*colorant));
      else {
        HQASSERT(oType(*colorant) == ONAME, "colorant must be a String or Name");
        names[i] = oName(*colorant);
      }
    }
  }
}

/** Looks up a namesObject in a store of tint transforms and alternate spaces
 * found in the named color databases for the current gstate, including whether
 * a given colorant was not found. The returned data is the alternateSpace,
 * tintTransform & tintTransformId. The latter is for the benefit of the color
 * cache which will be able to match the current color chain with that of a color
 * cache we already know about.
 * Only single colorants may be stored.
 * See comment against cc_insertNamedColorantStore() about the lifetime of the
 * store.
 * Returns TRUE if the namesObject is found, FALSE otherwise.
 */
Bool cc_lookupNamedColorantStore(GS_COLORinfo *colorInfo,
                                 OBJECT       *namesObject,
                                 OBJECT       **storedAlternateSpace,
                                 OBJECT       **storedTintTransform,
                                 int32        *storedTintTransformId)
{
  NAMECACHE *name;
  int32 numNames;
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;
  NAMES_OBJECT_STORE *storeEntry;

  /* Global control of whether to use this store which is normally only for
   * testing. But we cannot call out to the interpreter in the back end so we
   * must use the store then regardless of the param.
   */
  if ((colorInfo->params.enableColorCache & GSC_ENABLE_NAMED_COLOR_CACHE) == 0 &&
      colorInfo->colorState == frontEndColorState)
    return FALSE;

  HQASSERT(oType(*namesObject) == ONAME ||
           oType(*namesObject) == OSTRING ||
           oType(*namesObject) == OPACKEDARRAY ||
           oType(*namesObject) == OARRAY,
           "Invalid type in named color intercept");

  /* Get the colorant name from this namesObject. And bail out if there isn't
   * exactly one colorant.
   */
  numNames = cc_getNumNames(namesObject);
  if (numNames != 1)
    return FALSE;
  cc_getNames(namesObject, &name, numNames);

  for (storeEntry = hcmsInfo->interceptNamedColors->namesObjectStore;
       storeEntry != NULL;
       storeEntry = storeEntry->next) {

    /* Success if the colorant from namesObject matches this store entry */
    if (name == storeEntry->name) {
      if (storeEntry->present) {
        *storedAlternateSpace = &storeEntry->alternateSpace;
        *storedTintTransform = &storeEntry->tintTransform;
        *storedTintTransformId = storeEntry->tintTransformId;
      }
      else {
        *storedAlternateSpace = NULL;
        *storedTintTransform = NULL;
        *storedTintTransformId = 0;
      }

      return TRUE;
    }
  }

  return FALSE;
}

/** Inserts a namesObject and associated data about the alternate space and
 * tint transform into the namedColorant store. That includes whether the colorant
 * wasn't found in the databases in the current gstate.
 * Only single colorants may be stored.
 * If the colorant was found in the databases, the alternateSpace and tintTransform
 * should point to the objects returned by the database. If the colorant wasn't
 * found, both of these should be NULL.
 * The lifetime of the store is the lifetime of the setinterceptcolorspace, but
 * within a store, the lifetime of individual entries is limited by the save
 * level at which they were inserted. That's because all entries normally contain
 * references to PS VM because the color chain creation calls out to PS as part
 * of invoking the databases.
 * An entry may be inserted into a namedColorInfo that is referenced by several
 * hcmsInfo's and thus several colorInfo's, which breaks a strict memory
 * ownership model. It is this way to maximise the sharing of data amongst
 * hcmsInfo's relevant to the same setinterceptcolorspace. However, all entries
 * at a particular saveLevel belong to an hcmsInfo at the same saveLevel which
 * helps to make this model safe.
 * Returns FALSE on error, TRUE otherwise.
 */
Bool cc_insertNamedColorantStore(GS_COLORinfo *colorInfo,
                                 OBJECT       *namesObject,
                                 OBJECT       *alternateSpace,
                                 OBJECT       *tintTransform,
                                 int32        tintTransformId)
{
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;
  NAMES_OBJECT_STORE *storeEntry;
  int numNames = 1;

  HQASSERT(oType(*namesObject) == ONAME ||
           oType(*namesObject) == OSTRING ||
           oType(*namesObject) == OPACKEDARRAY ||
           oType(*namesObject) == OARRAY,
           "Invalid type in named color intercept");

  /* Bail out if there isn't exactly one colorant */
  numNames = cc_getNumNames(namesObject);
  if (numNames != 1)
    return FALSE;

  /* Global control of whether to use this store which is normally only for
   * testing. But we cannot call out to the interpreter in the back end so we
   * must use the store then regardless of the param.
   */
  if ((colorInfo->params.enableColorCache & GSC_ENABLE_NAMED_COLOR_CACHE) == 0 &&
      colorInfo->colorState == frontEndColorState)
    return TRUE;

  storeEntry = mm_alloc(mm_pool_color,
                        sizeof(NAMES_OBJECT_STORE),
                        MM_ALLOC_CLASS_NCOLOR);
  if (storeEntry == NULL)
    return error_handler(VMERROR);
  (void) object_slot_notvm(&storeEntry->alternateSpace);
  (void) object_slot_notvm(&storeEntry->tintTransform);

  /* Store the colorant name from this namesObject in the next store entry */
  cc_getNames(namesObject, &storeEntry->name, numNames);

  if (alternateSpace != NULL) {
    HQASSERT(tintTransform != NULL, "tinttransform NULL");
    storeEntry->present = TRUE;
    Copy(&storeEntry->alternateSpace, alternateSpace);
    Copy(&storeEntry->tintTransform, tintTransform);
    storeEntry->tintTransformId = tintTransformId;
  }
  else {
    HQASSERT(tintTransform == NULL, "tinttransform non-NULL");
    storeEntry->present = FALSE;
    object_store_null(&storeEntry->alternateSpace);
    object_store_null(&storeEntry->tintTransform);
    storeEntry->tintTransformId = 0;
  }
  storeEntry->saveLevel = get_core_context_interp()->savelevel;

  /* Insert the new entry at the head of the list */
  storeEntry->next = hcmsInfo->interceptNamedColors->namesObjectStore;
  hcmsInfo->interceptNamedColors->namesObjectStore = storeEntry;

  hcmsInfo->interceptNamedColors->numStoredNamesObjects++;

  return TRUE;
}

/** Purges entries in the colorantName store that were created at the saveLevel
 * or above. The entries in the list should be in order of entry, i.e. in
 * saveLevel order.
 * This has been factored out of cc_purgeNamedColorantStore() to allow destroying
 * of all NAMES_OBJECT_STORE entries, if saveLevel is -1.
 * See comment against cc_insertNamedColorantStore() about the lifetime of the
 * store.
 */
static void purgeNamedColorantStore(INTERCEPT_NAMEDCOLinfo *namedColorInfo,
                                    int32                  saveLevel)
{
  int i;
  NAMES_OBJECT_STORE *storeEntry;
  NAMES_OBJECT_STORE *nextEntry;

  for (storeEntry = namedColorInfo->namesObjectStore;
       storeEntry != NULL && storeEntry->saveLevel > saveLevel;
       storeEntry = nextEntry) {
    nextEntry = storeEntry->next;
    mm_free(mm_pool_color, storeEntry, sizeof(NAMES_OBJECT_STORE));
    namedColorInfo->namesObjectStore = nextEntry;
    namedColorInfo->numStoredNamesObjects--;
  }

  /* Higher save level entries should be found at the front of the list. So
   * remaining entries should be from lower save levels.
   */
  i = namedColorInfo->numStoredNamesObjects;
  for (; storeEntry != NULL; storeEntry = storeEntry->next) {
    if (storeEntry->saveLevel > saveLevel)
      HQFAIL("Entries with out of order save levels in named colorant store");
    i--;
  }
  HQASSERT(i == 0, "Inconsistent namesObjectStore");
}

/** Purges entries in the colorantName store that were created at the saveLevel
 * or above. The entries in the list should be in order of entry, i.e. in
 * saveLevel order.
 */
void cc_purgeNamedColorantStore(GS_COLORinfo *colorInfo, int32 saveLevel)
{
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;

  if (hcmsInfo != NULL)
    purgeNamedColorantStore(hcmsInfo->interceptNamedColors, saveLevel);
}

/** Safely prepares the named color data for use in the back end. This is
 * required at the hand off to the back end because the tint transform tables
 * must be consistent with the colorState. Thus, the named color data is made
 * unique to the colorInfo to ensure changes don't affect other colorInfos, and
 * we won't copy the colorant store into the new pInfo because there would be
 * reference counting issues with the store.
 * After this, the named colorant store will be rebuilt as a side effect of
 * building color chains during the hand-over.
 */
Bool cc_updateNamedColorantStore(GS_COLORinfo *colorInfo)
{
  OBJECT oldObj = OBJECT_NOTVM_NOTHING;

  Copy(&oldObj, &colorInfo->hcmsInfo->interceptNamedColors->namedColorObject);

  if (!cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) ||
       !replaceNamedColorInfo(&colorInfo->hcmsInfo->interceptNamedColors))
    return FALSE;

  Copy(&colorInfo->hcmsInfo->interceptNamedColors->namedColorObject, &oldObj);

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Deals with black values that could be boolean or a threshold value. If it's
 * boolean, it is converted to a threshold value that is outside the valid range
 * of 0-1 to force all black values to be either preserved or not.
 */
static Bool extractBlack(OBJECT *blackObj, USERVALUE *blackValue)
{
  if (oType(*blackObj) == OBOOLEAN) {
    if (oBool(*blackObj))
      *blackValue = BLACK_TRUE;
    else
      *blackValue = BLACK_FALSE;
  }
  else {
    USERVALUE value;

    HQASSERT(oType(*blackObj) == OREAL, "Expected an OREAL for black value");
    value = oReal(*blackObj);
    if (value < 0.0f || value > 1.0f)
      return detail_error_handler(RANGECHECK, "Invalid Black or BlackTint value");

    *blackValue = oReal(*blackObj);
  }

  return TRUE;
}

/* Handler for Black and BlackTint keys of setinterceptcolorspace.
 * We expect one of two forms for the value. One is a boolean which will be
 * used for all reproTypes.
 * The other is a dictionary object containing optional keys of Default, Picture,
 * Text, Vignette, and Other. This allows 'pInfo' to be sub-classed by reproType.
 * Each reproType could be a assigned a boolean value, or a dictionary that allows
 * further specialisation by colorModel.
 * At the end of the process, we will have a boolean value associated with each
 * of the 20 reproTypes/colorModel combinatations.
 * Some examples:
 *   /Black false
 *   /Black << /Text false >>
 *   /Black << /Default false  /Text true >>
 *   /Black << /Text << /Default false  /CMYK true >> >>
 *   /Black << /Other false  /Text << /Default true  /CMYK false >> >>
 *   /Black << /Default << /Default false  /CMYK true >>  /Text << /Default true  /CMYK false >> >>
 *   /Black << /Default << /CMYK false >> >>
 */
static Bool interceptBlack(OBJECT     *blackObj,
                           OBJECT     *pInfoObj,
                           USERVALUE  pInfo[REPRO_N_TYPES][REPRO_N_COLOR_MODELS])
{
  int32 i;
  int32 j;

  /* Copy the top level object for currentinterceptcolorspace */
  Copy(pInfoObj, blackObj);

  if (oType(*blackObj) != ODICTIONARY) {
    HQASSERT(oType(*blackObj) == OBOOLEAN || oType(*blackObj) == OREAL,
             "Expected a boolean or real object");

    /* Override of DeviceCMYK/RGB/Gray is a boolean, so the override will
     * be the same for all reproType's.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        if (!extractBlack(blackObj, &pInfo[i][j]))
          return FALSE;
      }
    }
  }
  else {
    NAMETYPEMATCH reproType_match[reproType_n_entries + 1];
    NAMETYPEMATCH colorModel_match[colorModel_n_entries + 1];

    /* Copy reproType_match from reproType_matchTemplate to allow specialisation */
    HqMemCpy(reproType_match, reproType_matchTemplate, sizeof(reproType_matchTemplate));
    for (i = 0; i < reproType_n_entries; i++) {
      reproType_match[i].count = 3;
      reproType_match[i].match[0] = ODICTIONARY;
      reproType_match[i].match[1] = OBOOLEAN;
      reproType_match[i].match[2] = OREAL;
    }

    /* Copy colorModel_match from colorModel_matchTemplate to allow specialisation */
    HqMemCpy(colorModel_match, colorModel_matchTemplate, sizeof(colorModel_matchTemplate));
    for (i = 0; i < colorModel_n_entries; i++) {
      colorModel_match[i].count = 2;
      colorModel_match[i].match[0] = OBOOLEAN;
      colorModel_match[i].match[1] = OREAL;
    }

    if (!checkExcessKeys(blackObj, reproType_match))
      return FALSE;

    /* Extract the Default, if present, and initialise as appropriate */
    if (!dictmatch(blackObj, reproType_match))
      return FALSE;

    if (reproType_match[reproType_Default].result != NULL) {
      if (oType(*reproType_match[reproType_Default].result) != ODICTIONARY) {
        /* A default, initialise all values to it */
        HQASSERT(oType(*reproType_match[reproType_Default].result) == OBOOLEAN ||
                 oType(*reproType_match[reproType_Default].result) == OREAL,
                 "Expected a boolean or real object");

        for (i = 0; i < REPRO_N_TYPES; i++) {
          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            if (!extractBlack(reproType_match[reproType_Default].result,
                              &pInfo[i][j]))
              return FALSE;
          }
        }
      }
      else {
        /* A Default with a dict of color model entries */
        if (!checkExcessKeys(reproType_match[reproType_Default].result, colorModel_match))
          return FALSE;

        if (!dictmatch(reproType_match[reproType_Default].result, colorModel_match))
          return FALSE;

        /* If there is a Default color model, initialise all values to it */
        if (colorModel_match[colorModel_Default].result != NULL) {
          for (i = 0; i < REPRO_N_TYPES; i++) {
            for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
              if (!extractBlack(colorModel_match[colorModel_Default].result,
                                &pInfo[i][j]))
                return FALSE;
            }
          }
        }

        /* If there is a value for individual color models in the Default dict,
         * set the values for appropriate reproType/colorModel combinations.
         */
        for (i = 0; i < REPRO_N_TYPES; i++) {
          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            if (colorModel_match[j].result != NULL) {
              if (!extractBlack(colorModel_match[j].result,
                                &pInfo[i][j]))
                return FALSE;
            }
          }
        }
      }
    }

    /* Loop over the reproduction types, which are the remaining valid keys, and
     * extract the values.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      if (reproType_match[i].result != NULL) {
        if (oType(*reproType_match[i].result) != ODICTIONARY) {
          /* A reproType entry with a default, initialise values to it */
          HQASSERT(oType(*reproType_match[i].result) == OBOOLEAN ||
                   oType(*reproType_match[i].result) == OREAL,
                   "Expected a boolean or real object");

          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            if (!extractBlack(reproType_match[i].result,
                              &pInfo[i][j]))
              return FALSE;
          }
        }
        else {
          /* A reproType entry with a dict of color model entries */
          if (!checkExcessKeys(reproType_match[i].result, colorModel_match))
            return FALSE;

          if (!dictmatch(reproType_match[i].result, colorModel_match))
            return FALSE;

          /* Initialise values for this reproType from the default colorModel */
          if (colorModel_match[colorModel_Default].result != NULL) {
            for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
              if (!extractBlack(colorModel_match[colorModel_Default].result,
                                &pInfo[i][j]))
                return FALSE;
            }
          }

          /* Now set the values for each reproType/colorModel combination */
          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            if (colorModel_match[j].result != NULL) {
              if (!extractBlack(colorModel_match[j].result,
                                &pInfo[i][j]))
                return FALSE;
            }
          }
        }
      }
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Initialises the intercept fields of the structure */
static Bool resetIntercepts(GS_HCMSinfo *hcmsInfo, GS_COLORinfo *colorInfo)
{
  HQASSERT(CLINK_OWNER(colorInfo->hcmsInfo), "Can't change shared hcmsInfo");

  hcmsInfo->interceptId = INTERCEPT_ID_DEFAULT;

  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->interceptCMYKObj,
                            hcmsInfo->interceptCMYK,
                            4))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->interceptRGBObj,
                            hcmsInfo->interceptRGB,
                            3))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->interceptGrayObj,
                            hcmsInfo->interceptGray,
                            1))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->blendCMYKObj,
                            hcmsInfo->blendCMYK,
                            4))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->blendRGBObj,
                            hcmsInfo->blendRGB,
                            3))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->blendGrayObj,
                            hcmsInfo->blendGray,
                            1))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->sourceCMYKObj,
                            hcmsInfo->sourceCMYK,
                            4))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->sourceRGBObj,
                            hcmsInfo->sourceRGB,
                            3))
    return FALSE;
  if (!interceptDeviceSpace(colorInfo, &onull,
                            &hcmsInfo->sourceGrayObj,
                            hcmsInfo->sourceGray,
                            1))
    return FALSE;
  if (!interceptOverride(&fnewobj, &hcmsInfo->overrideCMYKObj, hcmsInfo->overrideCMYK))
    return FALSE;
  if (!interceptOverride(&fnewobj, &hcmsInfo->overrideRGBObj, hcmsInfo->overrideRGB))
    return FALSE;
  if (!interceptOverride(&fnewobj, &hcmsInfo->overrideGrayObj, hcmsInfo->overrideGray))
    return FALSE;
  if (!interceptNamedColors(hcmsInfo, &onull))
    return FALSE;
  hcmsInfo->multipleNamedColors = TRUE;
  hcmsInfo->useBlendSpaceForOutput = FALSE;
  if (!interceptBlack(&tnewobj, &hcmsInfo->interceptBlackObj, hcmsInfo->interceptBlack))
    return FALSE;
  if (!interceptBlack(&tnewobj, &hcmsInfo->interceptBlackTintObj, hcmsInfo->interceptBlackTint))
    return FALSE;
  hcmsInfo->blackTintLuminance = TRUE;
  hcmsInfo->convertRGBBlack = FALSE;
  hcmsInfo->useReproductionAsIntercept = TRUE;

  /* For the absence of doubt, don't reset defaultCMYK, defaultRGB, or defaultGray
   * because we want these to apply to things like PDF soft masks as the last
   * gasp method of rendering colour to a device space.
   */

  return TRUE;
}

/* ---------------------------------------------------------------------- */

Bool gsc_setPoorSoftMask(GS_COLORinfo *colorInfo, Bool poorSoftMask)
{
  GS_HCMSinfo *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
      cc_invalidateColorChains(colorInfo, FALSE)) {
    hcmsInfo = colorInfo->hcmsInfo;

    hcmsInfo->poorSoftMask = poorSoftMask;
  }
  else
    return FALSE;

  return TRUE;
}

/* Initialises the intercept fields of the structure and sets the overrides,
 * if desired. The original purpose of this is for transparency soft masks
 * where the mask should be derived without colour managing the source data
 * to get predictable masks. Sometimes we want to set overrides and sometimes
 * not, depending on whether a luminosity should be derived from device
 * independent colour spaces.
 */
Bool gsc_configPaintingSoftMask(GS_COLORinfo *colorInfo, Bool setOverride)
{
  GS_HCMSinfo *hcmsInfo;
  int32 i;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  /* Don't purge the ChainCache because the intercepts are changing to a well
   * known state.
   */
  if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
      cc_invalidateColorChains(colorInfo, FALSE)) {
    hcmsInfo = colorInfo->hcmsInfo;

    if (!resetIntercepts(colorInfo->hcmsInfo, colorInfo))
      return FALSE;

    /* This prevents the use of a profile from the reproduction dictionary as an
     * intercept. This is required to get predictable soft masks in transparency.
     */
    hcmsInfo->useReproductionAsIntercept = FALSE;

    hcmsInfo->paintingSoftMask = TRUE;

    /* Acrobat X always uses ICC alternate spaces, ignoring the profile. So we
     * have to match that as default behaviour.
     */
    if (hcmsInfo->poorSoftMask)
      setOverride = TRUE;

    if (setOverride) {
      for (i = 0; i < REPRO_N_TYPES; i++) {
        hcmsInfo->overrideCMYK[i] = TRUE;
        hcmsInfo->overrideRGB[i]  = TRUE;
        hcmsInfo->overrideGray[i] = TRUE;
      }

      hcmsInfo->interceptId = INTERCEPT_ID_NONE_WITH_OVERRIDES;
    }
    else
      hcmsInfo->interceptId = INTERCEPT_ID_NONE_WITHOUT_OVERRIDES;
  }
  else
    return FALSE;

  /* Acrobat doesn't convert process Separation/DeviceN CSAs through profiles
   * because it considers them only for overprints. We should do the same for
   * soft masks.
   * NB. We'll rely on a group save context to restore the value.
   */
  colorInfo->params.adobeProcessSeparations = FALSE;

  /* Turn off overprinting for the soft mask's group. This is not part of
   * the spec, but is done to avoid unexpected interaction with the
   * background color for luminosity soft masks (alpha soft masks are not
   * affected by the overprint setting at all); imagine a CMYK group, whose
   * default background color is black - if overprint is true it's all too
   * easy to lose objects as the black background shows through, and results
   * in a mask alpha of zero.
   */
  if (!gsc_disableOverprint(colorInfo))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* Check the consistency of paintingSoftMask and interceptId. It is important
 * for the color chain cache.
 */
static void checkInterceptId(GS_HCMSinfo *hcmsInfo)
{
  switch (hcmsInfo->interceptId) {
  case INTERCEPT_ID_NONE_WITH_OVERRIDES:
  case INTERCEPT_ID_NONE_WITHOUT_OVERRIDES:
    HQASSERT(hcmsInfo->paintingSoftMask, "Should be painting a soft mask");
    break;
  default:
    HQASSERT(!hcmsInfo->paintingSoftMask, "Shouldn't be painting a soft mask");
  }
}

void cc_initInterceptId(uint32 *id)
{
  *id = INTERCEPT_ID_INITIAL;
};

uint32 cc_getInterceptId(GS_HCMSinfo *hcmsInfo)
{
  HQASSERT(hcmsInfo != NULL, "hcmsInfo NULL");
  checkInterceptId(hcmsInfo);

  return hcmsInfo->interceptId;
}

/* ---------------------------------------------------------------------- */
/* manufactures a dictionary containing all the spaces stored by
 * setinterceptcolorspace */
Bool gsc_getinterceptcolorspace(GS_COLORinfo *colorInfo, STACK *stack)
{
#define DICT_SIZE (13)

  GS_HCMSinfo       *hcmsInfo;
  OBJECT            dict = OBJECT_NOTVM_NOTHING;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  hcmsInfo = colorInfo->hcmsInfo;

  if (!ps_dictionary(&dict, DICT_SIZE) )
    return FALSE;

  if (!fast_insert_hash_name(&dict, NAME_DeviceCMYK, &hcmsInfo->interceptCMYKObj) ||
      !fast_insert_hash_name(&dict, NAME_DeviceRGB, &hcmsInfo->interceptRGBObj) ||
      !fast_insert_hash_name(&dict, NAME_DeviceGray, &hcmsInfo->interceptGrayObj) ||
      !fast_insert_hash_name(&dict, NAME_BlendCMYK, &hcmsInfo->blendCMYKObj) ||
      !fast_insert_hash_name(&dict, NAME_BlendRGB, &hcmsInfo->blendRGBObj) ||
      !fast_insert_hash_name(&dict, NAME_BlendGray, &hcmsInfo->blendGrayObj) ||
      !fast_insert_hash_name(&dict, NAME_SourceCMYK, &hcmsInfo->sourceCMYKObj) ||
      !fast_insert_hash_name(&dict, NAME_SourceRGB, &hcmsInfo->sourceRGBObj) ||
      !fast_insert_hash_name(&dict, NAME_SourceGray, &hcmsInfo->sourceGrayObj) ||
      !fast_insert_hash_name(&dict, NAME_OverrideCMYK, &hcmsInfo->overrideCMYKObj) ||
      !fast_insert_hash_name(&dict, NAME_OverrideRGB, &hcmsInfo->overrideRGBObj) ||
      !fast_insert_hash_name(&dict, NAME_OverrideGray, &hcmsInfo->overrideGrayObj) ||
      !fast_insert_hash_name(&dict, NAME_NamedColor, &hcmsInfo->interceptNamedColors->namedColorObject) ||
      !fast_insert_hash_name(&dict, NAME_MultipleNamedColors, hcmsInfo->multipleNamedColors ? &tnewobj : &fnewobj) ||
      !fast_insert_hash_name(&dict, NAME_OverprintPreview, hcmsInfo->overprintPreview ? &tnewobj : &fnewobj) ||
      !fast_insert_hash_name(&dict, NAME_UseBlendSpaceForOutput, hcmsInfo->useBlendSpaceForOutput ? &tnewobj : &fnewobj) ||
      !fast_insert_hash_name(&dict, NAME_Black, &hcmsInfo->interceptBlackObj) ||
      !fast_insert_hash_name(&dict, NAME_BlackTint, &hcmsInfo->interceptBlackTintObj) ||
      !fast_insert_hash_name(&dict, NAME_BlackTintLuminance, hcmsInfo->blackTintLuminance ? &tnewobj : &fnewobj) ||
      !fast_insert_hash_name(&dict, NAME_ConvertRGBBlack, hcmsInfo->convertRGBBlack ? &tnewobj : &fnewobj) ||
      !fast_insert_hash_name(&dict, NAME_DefaultCMYK, &hcmsInfo->defaultCMYK.colorSpaceObject) ||
      !fast_insert_hash_name(&dict, NAME_DefaultRGB, &hcmsInfo->defaultRGB.colorSpaceObject) ||
      !fast_insert_hash_name(&dict, NAME_DefaultGray, &hcmsInfo->defaultGray.colorSpaceObject))
    return FALSE ;

  if (!push(&dict, stack))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */

Bool gsc_setAssociatedProfile(GS_COLORinfo   *colorInfo,
                              COLORSPACE_ID  colorSpaceId,
                              OBJECT         *profile)
{
  INTERCEPTinfo *oldProfile;
  INTERCEPTinfo newProfile;
  GS_HCMSinfo   *hcmsInfo = colorInfo->hcmsInfo;

  initInterceptInfo(&newProfile);

  /* Convert the new associated profile to an INTERCEPTinfo */
  switch (colorSpaceId) {
  case SPACE_DeviceCMYK:
    oldProfile = &hcmsInfo->associatedCMYK;
    if (!interceptOneColorSpace(colorInfo, profile, &newProfile, 4))
      return FALSE;
    break;

  case SPACE_DeviceRGB:
    oldProfile = &hcmsInfo->associatedRGB;
    if (!interceptOneColorSpace(colorInfo, profile, &newProfile, 3))
      return FALSE;
    break;

  case SPACE_DeviceGray:
    oldProfile = &hcmsInfo->associatedGray;
    if (!interceptOneColorSpace(colorInfo, profile, &newProfile, 1))
      return FALSE;
    break;

  default:
    HQFAIL("Invalid colorspaceId");
    return FALSE;
  }

  /* Test if new != old, only use a few relevant fields */
  if (oldProfile->info.inputColorSpaceId != newProfile.info.inputColorSpaceId ||
      oldProfile->info.outputColorSpaceId != newProfile.info.outputColorSpaceId ||
      oldProfile->info.u.shared != newProfile.info.u.shared) {

    if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
        cc_invalidateColorChains(colorInfo, FALSE)) {
      switch (colorSpaceId) {
      case SPACE_DeviceCMYK:
        colorInfo->hcmsInfo->associatedCMYK = newProfile;   /* Struct copy to set slot properties */
        break;
      case SPACE_DeviceRGB:
        colorInfo->hcmsInfo->associatedRGB = newProfile;    /* Struct copy to set slot properties */
        break;
      case SPACE_DeviceGray:
        colorInfo->hcmsInfo->associatedGray = newProfile;   /* Struct copy to set slot properties */
        break;
      }
    }
    else
      return FALSE;
  }

  return TRUE;
}

TRANSFORM_LINK_INFO cc_getAssociatedProfile(GS_COLORinfo      *colorInfo,
                                            REPRO_COLOR_MODEL colorModel)
{
  TRANSFORM_LINK_INFO *profile;
  TRANSFORM_LINK_INFO nullInfo;

  HQASSERT(colorModel < REPRO_N_COLOR_MODELS,
           "Invalid colorModel");

  switch (colorModel) {
  case REPRO_COLOR_MODEL_CMYK:
    profile = &colorInfo->hcmsInfo->associatedCMYK.info;
    break;
  case REPRO_COLOR_MODEL_RGB:
    profile = &colorInfo->hcmsInfo->associatedRGB.info;
    break;
  case REPRO_COLOR_MODEL_GRAY:
    profile = &colorInfo->hcmsInfo->associatedGray.info;
    break;
  default:
    /* Not an error */
    cc_initTransformInfo(&nullInfo);
    profile = &nullInfo;
    break;
  }

  return *profile;
}

/* ---------------------------------------------------------------------- */

Bool cc_getPaintingSoftMask(GS_COLORinfo *colorInfo)
{
  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  checkInterceptId(colorInfo->hcmsInfo);

  return colorInfo->hcmsInfo->paintingSoftMask;
}

/* ---------------------------------------------------------------------- */

/* Should only be called during setpagedevice and stores the alternate CMM
 * installed into the pagedevice.
 */
Bool gsc_setAlternateCMM(GS_COLORinfo *colorInfo, OBJECT *cmmName)
{
  sw_cmm_instance *alternateCMM;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(oType(*cmmName) == OSTRING || oType(*cmmName) == ONULL,
           "Wrong type of cmmName object");

  if (oType(*cmmName) == ONULL)
    alternateCMM = NULL;
  else {
    alternateCMM = cc_findAlternateCMM(cmmName);
    if (alternateCMM == NULL)
      return detail_error_handler(CONFIGURATIONERROR, "Unknown Alternate CMM");
  }

  if (colorInfo->hcmsInfo == NULL ||
      colorInfo->hcmsInfo->alternateCMM != alternateCMM) {
    if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
        cc_invalidateColorChains(colorInfo, TRUE)) {
      colorInfo->hcmsInfo->alternateCMM = alternateCMM;
    }
    else
      return FALSE;
  }

  return TRUE;
}

sw_cmm_instance *cc_getAlternateCMM(GS_HCMSinfo *hcmsInfo)
{
  HQASSERT(hcmsInfo, "hcmsInfo NULL");

  return hcmsInfo->alternateCMM;
}

/* ---------------------------------------------------------------------- */

sw_cmm_instance *cc_getwcsCMM(GS_HCMSinfo *hcmsInfo)
{
  HQASSERT(hcmsInfo, "hcmsInfo NULL");

  return hcmsInfo->wcsCMM;
}

/* ---------------------------------------------------------------------- */

Bool gsc_setrenderingintent(GS_COLORinfo *colorInfo, OBJECT *riName)
{
  NAMECACHE         *newIntentName;
  uint8             intent;

  if (colorInfo == NULL)
    return error_handler(UNDEFINED);

  if (oType(*riName) != ONAME)
    return error_handler(TYPECHECK);

  newIntentName = oName(*riName);

  for (intent = 0; intent < SW_CMM_N_ICC_RENDERING_INTENTS; intent++) {
    if (oNameNumber(*riName) == renderingIntentNames[intent])
      break;
  }
  if (oNameNumber(*riName) == renderingIntentNames[SW_CMM_INTENT_NONE])
    intent = SW_CMM_INTENT_NONE;
  else if (intent == SW_CMM_N_ICC_RENDERING_INTENTS) {
    /* The job has supplied a bad rendering intent. Acrobat ignores it, although
     * we normally wouldn't ignore such an error. In this case, we will ignore
     * error because the effect of rendering intent is normally subtle.
     */
    HQTRACE(colorInfo->hcmsInfo->trace_unknownIntent++ == 0,
            ("Invalid ICC rendering intent - '%.*s'",
             newIntentName->len, newIntentName->clist));
    return TRUE;
  }

  /* Success. Update the gstate with the new information
   */
  if (colorInfo->hcmsInfo->renderingIntent != intent) {
    if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
        cc_invalidateColorChains(colorInfo, FALSE))
      colorInfo->hcmsInfo->renderingIntent = intent;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getrenderingintent(GS_COLORinfo *colorInfo, STACK *stack)
{
  uint8 currentIntent = cc_getrenderingintent(colorInfo);
  OBJECT ri = OBJECT_NOTVM_NOTHING;

  object_store_name(&ri, renderingIntentNames[currentIntent], LITERAL);

  return push(&ri, stack);
}

NAMECACHE *gsc_convertIntentToName(uint8 renderingIntent)
{
  HQASSERT(renderingIntent < SW_CMM_N_ICC_RENDERING_INTENTS ||
           renderingIntent == SW_CMM_INTENT_NONE,
           "Invalid renderingIntent");

  return system_names + renderingIntentNames[renderingIntent];
}

uint8 cc_getrenderingintent(GS_COLORinfo *colorInfo)
{
  GS_HCMSinfo  *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  hcmsInfo = colorInfo->hcmsInfo;

  HQASSERT(hcmsInfo->renderingIntent < SW_CMM_N_ICC_RENDERING_INTENTS ||
           hcmsInfo->renderingIntent == SW_CMM_INTENT_NONE,
           "Invalid renderingIntent");

  return hcmsInfo->renderingIntent;
}

uint8 gsc_getICCrenderingintent(GS_COLORinfo *colorInfo)
{
  return cc_getrenderingintent(colorInfo);
}

Bool gsc_setAdobeRenderingIntent(GS_COLORinfo *colorInfo, Bool adobeRenderingIntent)
{
  if (colorInfo == NULL)
    return error_handler(UNDEFINED);

  if (colorInfo->hcmsInfo->adobeRenderingIntent != adobeRenderingIntent) {
    if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
        cc_invalidateColorChains(colorInfo, TRUE))
      colorInfo->hcmsInfo->adobeRenderingIntent = adobeRenderingIntent;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getAdobeRenderingIntent(GS_COLORinfo *colorInfo)
{
  GS_HCMSinfo  *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  hcmsInfo = colorInfo->hcmsInfo;

  return hcmsInfo->adobeRenderingIntent;
}

/* ---------------------------------------------------------------------- */

/* Handler for the setreproduction operator */
Bool gsc_setreproduction(GS_COLORinfo *colorInfo, OBJECT *reproDict)
{
  GS_HCMSinfo     *oldHcmsInfo;
  Bool            result = FALSE;

  if (oType(*reproDict) != ODICTIONARY)
    return error_handler( TYPECHECK ) ;

  oldHcmsInfo = colorInfo->hcmsInfo;
  cc_reservehcmsinfo( oldHcmsInfo );

  if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
      cc_invalidateColorChains(colorInfo, TRUE)) {

    /* Initialise the reproductionInfo, taking care to free off resources
     * currently in the gstate.
     */
    destroyReproductionInfo(&colorInfo->hcmsInfo->reproductionInfo);
    initReproductionInfo(&colorInfo->hcmsInfo->reproductionInfo);

    OCopy(colorInfo->hcmsInfo->reproductionObject, *reproDict);
    colorInfo->hcmsInfo->reproductionTransformDepth = 0;

    /* And do the work */
    result = doReproductionDict(reproDict,
                                &colorInfo->hcmsInfo->reproductionInfo,
                                colorInfo);

    /* set the dict to read only */
    SET_DICT_ACCESS (&colorInfo->hcmsInfo->reproductionObject, READ_ONLY);
  }

  if (result) {
    /* Get rid of the saved info */
    cc_destroyhcmsinfo(&oldHcmsInfo);

    updateUsingObjectBasedColor(colorInfo->hcmsInfo);
  }
  else {
    /* Reinstate the saved info */
    cc_destroyhcmsinfo(&colorInfo->hcmsInfo);
    colorInfo->hcmsInfo = oldHcmsInfo;
  }

  return result;
}

/* ------------------------------------------------------------------------ */

/* Initialise all the entries in a reproductionInfo structure */
static void initReproductionInfo(REPRODUCTIONinfo *reproductionInfo)
{
  uint32 i;
  uint32 j;

  reproductionInfo->refCnt = 1;
  reproductionInfo->validReproduction = FALSE;
  reproductionInfo->firstInSequence = TRUE;
  reproductionInfo->inputDeviceSpaceId = SPACE_notset;
  reproductionInfo->inputDimensions = 0;
  reproductionInfo->outputDeviceSpaceId = SPACE_notset;
  reproductionInfo->outputDimensions = 0;

  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
      reproductionInfo->objectProfile[i][j] = initProfileList();
      reproductionInfo->fullProfileData[i][j] = FALSE;
    }
  }

  for (i = 0; i < SW_CMM_N_ICC_RENDERING_INTENTS; i++)
    reproductionInfo->iccIntentMappingNames[i] = renderingIntentNames[i];

  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++)
      reproductionInfo->objectIntentNames[i][j] = NAME_Default;
  }

  reproductionInfo->sourceIntentName = NAME_Default;

  reproductionInfo->blackPointComp = FALSE;
  reproductionInfo->nextDevice = NULL;

  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
      cc_initTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
      reproductionInfo->inputColorSpaceIsDeviceLink[i][j] = FALSE;
      reproductionInfo->inputColorSpaceIsRequired[i][j] = FALSE;
    }
  }

  initInterceptInfo(&reproductionInfo->nullIntercept);
  initInterceptInfo(&reproductionInfo->reproProfileAsIntercept);
}

/* ------------------------------------------------------------------------ */

enum {
  rep_Profile, rep_IntentMappings, rep_SourceIntent, rep_BlackPointCompensation,
  rep_NextDevice, rep_InputColorSpace, rep_n_entries
};
static NAMETYPEMATCH gRreproduction_match[rep_n_entries + 1] = {
  { NAME_Profile | OOPTIONAL,                 2, { ODICTIONARY, ONULL }},
  { NAME_IntentMappings | OOPTIONAL,          3, { ODICTIONARY, ONAME, ONULL }},
  { NAME_SourceIntent | OOPTIONAL,            2, { ONAME, ONULL }},
  { NAME_BlackPointCompensation | OOPTIONAL,  1, { OBOOLEAN }},
  { NAME_NextDevice | OOPTIONAL,              2, { ODICTIONARY, ONULL }},
  { NAME_InputColorSpace | OOPTIONAL,         3, { ODICTIONARY, OARRAY, ONULL }},
  DUMMY_END_MATCH
};

/* Process a reproduction dictionary and place data into a REPRODUCTIONinfo
 * structure. This is a potentially recursive function because the structures
 * can be nested via the NextDevice key.
 */
static Bool doReproductionDict(OBJECT           *reproDict,
                               REPRODUCTIONinfo *reproductionInfo,
                               GS_COLORinfo     *colorInfo)
{
  OBJECT *theo;
  NAMETYPEMATCH reproduction_match[rep_n_entries + 1];
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;
  int32 i;
  int32 j;

  /* Copy gRreproduction_match into local data to allow recursive calls */
  HqMemCpy(reproduction_match, gRreproduction_match, sizeof(gRreproduction_match));

  if (!checkExcessKeys(reproDict, reproduction_match))
    return FALSE;

  if (!dictmatch(reproDict, reproduction_match))
    return FALSE;

  if (reproduction_match[rep_Profile].result != NULL ||
      reproduction_match[rep_NextDevice].result != NULL ||
      reproduction_match[rep_InputColorSpace].result != NULL) {
    reproductionInfo->validReproduction = TRUE;
  }

  hcmsInfo->reproductionTransformDepth++;
  if (hcmsInfo->reproductionTransformDepth > MAX_NEXTDEVICE_DICTS)
    return detail_error_handler(CONFIGURATIONERROR,
                                "Max depth of NextDevice dictionaries exceeded");

  theo = reproduction_match[rep_Profile].result;
  if (theo != NULL) {
    if (oType(*theo) != ONULL) {
      if (!doProfile(theo, reproductionInfo, colorInfo))
        return FALSE;
    }
  }

  theo = reproduction_match[rep_IntentMappings].result;
  if (theo != NULL) {
    if (oType(*theo) != ONULL) {
      if (!doIntentMappings(theo, reproductionInfo))
        return FALSE;
    }
  }

  theo = reproduction_match[rep_SourceIntent].result;
  if (theo != NULL) {
    if (oType(*theo) != ONULL) {
      int32 intentNameNumber = oNameNumber(*reproduction_match[rep_SourceIntent].result);
      if (!validIntent(intentNameNumber))
        return error_handler(RANGECHECK);;

      reproductionInfo->sourceIntentName = intentNameNumber;
    }
  }

  theo = reproduction_match[rep_BlackPointCompensation].result;
  if (theo != NULL)
    reproductionInfo->blackPointComp = oBool(*theo);

  theo = reproduction_match[rep_InputColorSpace].result;
  if (theo != NULL) {
    if (oType(*theo) != ONULL) {
      if (!doInputColorSpace(theo, reproductionInfo, colorInfo))
        return FALSE;
    }
  }
  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
      if (reproductionInfo->inputColorSpaceInfo[i][j].inputColorSpaceId == SPACE_notset &&
          reproductionInfo->inputColorSpaceIsRequired[i][j]) {
        /* We don't have profiles for all reproTypes in the previous reproduction
         * dictionary and there was a devicelink => we don't have enough info to
         * process the input colors to this reproduction dictionary.
         */
        return detail_error_handler(CONFIGURATIONERROR,
                                    "Missing InputColorSpace");
      }
    }
  }

  /* The NextDevice handling must go after Profile & InputColorSpace because the
   * verifications require that the NextDevice knows the colorspace and/or the
   * number of channels.
   */
  theo = reproduction_match[rep_NextDevice].result;
  if (theo != NULL) {
    if (oType(*theo) == ODICTIONARY) {
      int32 i;
      int32 j;

      reproductionInfo->nextDevice = mm_sac_alloc(mm_pool_color,
                                                  sizeof(REPRODUCTIONinfo),
                                                  MM_ALLOC_CLASS_NCOLOR);
      if (reproductionInfo->nextDevice == NULL)
        return error_handler(VMERROR);

      initReproductionInfo(reproductionInfo->nextDevice);
      reproductionInfo->nextDevice->firstInSequence = FALSE;
      reproductionInfo->nextDevice->inputDeviceSpaceId = reproductionInfo->outputDeviceSpaceId;
      reproductionInfo->nextDevice->inputDimensions = reproductionInfo->outputDimensions;

      /* At this point, we can validate whether we have enough data to convert color
       * in the NextDevice.
       */
      for (i = 0; i < REPRO_N_TYPES; i++) {
        for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
          if (!reproductionInfo->fullProfileData[i][j] &&
              !reproductionInfo->inputColorSpaceIsDeviceLink[i][j])
            reproductionInfo->nextDevice->inputColorSpaceIsRequired[i][j] = TRUE;
        }
      }

      if (!doReproductionDict(theo,
                              reproductionInfo->nextDevice,
                              colorInfo))
        return FALSE;
    }
  }

  return TRUE;
}

/* ------------------------------------------------------------------------ */

/* Handler for the Profile key of setreproduction.
 * We expect a dictionary object containing optional keys of Default, Picture,
 * Text, Vignette, and Other. Each of the reproduction types will contain a
 * dictionary of profiles keyed against the color models of Default, CMYK, RGB,
 * Gray, and NamedColor.
 * At the end of the process, we will have a profile associated with each of
 * the 20 combinations of reproduction type and color model.
 *
 * An example of a Profile dictionary:
 * <<
 *    /Default  <profile> or [<profile1> <profile2> ...]
 *    /Picture <<
 *      /Default  <profile> or [<profile1> <profile2> ...]
 *      /CMYK     <profile> or [<profile1> <profile2> ...]
 *    >>
 * >>
 * where
 * - <profile> is a filestream for ICC profiles and a dictionary for Hqn profiles.
 * - all reproTypes's are allowed as top levels keys and all colorModels are
 *   allowed within each sub-dictionary.
 */
static Bool doProfile(OBJECT            *profileDictObj,
                      REPRODUCTIONinfo  *reproductionInfo,
                      GS_COLORinfo      *colorInfo)
{
  int32 i;
  int32 j;
  NAMETYPEMATCH reproType_match[reproType_n_entries + 1];
  NAMETYPEMATCH colorModel_match[colorModel_n_entries + 1];

  HQASSERT(oType(*profileDictObj) == ODICTIONARY, "Expected a dictionary");

  /* Copy reproType_match from reproType_matchTemplate to allow specialisation.
   * The Default case is different because it requires a profile, while the
   * object specific keys require a dictionary for the color models.
   */
  HqMemCpy(reproType_match, reproType_matchTemplate, sizeof(reproType_matchTemplate));
  for (i = 0; i < REPRO_N_TYPES; i++) {
    reproType_match[i].count = 2;
    reproType_match[i].match[0] = ODICTIONARY;
    reproType_match[i].match[1] = ONULL;
  }
  reproType_match[reproType_Default].count = 4;
  reproType_match[reproType_Default].match[0] = OFILE;
  reproType_match[reproType_Default].match[1] = ODICTIONARY;
  reproType_match[reproType_Default].match[2] = OARRAY;
  reproType_match[reproType_Default].match[3] = ONULL;

  /* Copy colorModel_match from colorModel_matchTemplate to allow specialisation */
  HqMemCpy(colorModel_match, colorModel_matchTemplate, sizeof(colorModel_matchTemplate));
  for (i = 0; i < colorModel_n_entries; i++) {
    colorModel_match[i].count = 4;
    colorModel_match[i].match[0] = OFILE;
    colorModel_match[i].match[1] = ODICTIONARY;
    colorModel_match[i].match[2] = OARRAY;
    colorModel_match[i].match[3] = ONULL;
  }


  if (!checkExcessKeys(profileDictObj, reproType_match))
    return FALSE;

  if (!dictmatch(profileDictObj, reproType_match))
    return FALSE;

  /* Extract the Default, if present, and initialise all values to it */
  if (reproType_match[reproType_Default].result != NULL) {
    PROFILEinfo defaultProfile = initProfileList();

    if (!extractProfileList(reproType_match[reproType_Default].result,
                            colorInfo,
                            &defaultProfile,
                            &reproductionInfo->outputDeviceSpaceId,
                            &reproductionInfo->outputDimensions))
      return FALSE;

    /* Initialise all profiles with the default */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        destroyProfileList(&reproductionInfo->objectProfile[i][j]);
        reproductionInfo->objectProfile[i][j] = defaultProfile;
        reserveProfileList(&defaultProfile);
      }
    }
    /* And pop off one reserve level so the first one doesn't count twice */
    destroyProfileList(&defaultProfile);
  }

  /* Loop over the reproduction types, which are the remaining valid keys, and
   * extract the values for the default and color model specific keys.
   */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    if (reproType_match[i].result != NULL) {
      if (oType(*reproType_match[i].result) == ODICTIONARY) {
        if (!checkExcessKeys(reproType_match[i].result, colorModel_match))
          return FALSE;

        if (!dictmatch(reproType_match[i].result, colorModel_match))
          return FALSE;

        /* Initialise profiles for this reproType from the default colorModel */
        if (colorModel_match[colorModel_Default].result != NULL) {
          PROFILEinfo defaultProfile = initProfileList();

          if (!extractProfileList(colorModel_match[colorModel_Default].result,
                                  colorInfo,
                                  &defaultProfile,
                                  &reproductionInfo->outputDeviceSpaceId,
                                  &reproductionInfo->outputDimensions))
            return FALSE;
          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            destroyProfileList(&reproductionInfo->objectProfile[i][j]);
            reproductionInfo->objectProfile[i][j] = defaultProfile;
            reserveProfileList(&defaultProfile);
          }
          /* And pop off one reserve level so the first one doesn't count twice */
          destroyProfileList(&defaultProfile);
        }

        /* Now set the values for each reproType/colorModel combination.
         */
        for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
          if (colorModel_match[j].result != NULL) {
            destroyProfileList(&reproductionInfo->objectProfile[i][j]);
            if (!extractProfileList(colorModel_match[j].result,
                                    colorInfo,
                                    &reproductionInfo->objectProfile[i][j],
                                    &reproductionInfo->outputDeviceSpaceId,
                                    &reproductionInfo->outputDimensions))
              return FALSE;
          }
        }
      }
      else
        HQASSERT(oType(*reproType_match[i].result) == ONULL,
                 "Expected null object");
    }
  }

  /* Determine whether we have a full set of profile data for the purpose of
   * verification later.
   */
  for (i = 0; i < REPRO_N_TYPES; i++) {
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
      reproductionInfo->fullProfileData[i][j] = TRUE;
      if (reproductionInfo->objectProfile[i][j].p.inputColorSpaceId == SPACE_notset)
        reproductionInfo->fullProfileData[i][j] = FALSE;
    }
  }

  return TRUE;
}

/* Handler for individual entries within the Profile dictionary.
 * The profileObj should be null, an ICC profile stream, an array of profiles
 * to handle a sequence including an abstract profile, or a (in a form TBD),
 * Hqn profile.
 */
static Bool extractProfile(OBJECT               *profileObj,
                           GS_COLORinfo         *colorInfo,
                           Bool                 lastInList,
                           TRANSFORM_LINK_INFO  *linkInfo,
                           COLORSPACE_ID        *devicespace,
                           int32                *dimensions)
{
  /* If the profile is explicitly declared as null, we'll ignore it for now */
  if (oType(*profileObj) == ONULL)
    return TRUE;

  if (oType(*profileObj) == OFILE) {
    /* Must be an ICC Profile */

    ICC_PROFILE_INFO *pInfo = NULL;
    COLORSPACE_ID pcsspace;
    Bool dummyInputTable;
    Bool dummyDevicelinkTable;
    Bool outputTablePresent;

    /* Fetch or make an icc_profile_info_cache element containing header info */
    if (!cc_get_icc_output_profile_info(colorInfo,
                                        profileObj,
                                        &pInfo,
                                        dimensions,
                                        devicespace,
                                        &pcsspace))
      return FALSE;
    if (pInfo == NULL)
      return FALSE;

    if (!cc_icc_availableModes(pInfo,
                               &dummyInputTable,
                               &outputTablePresent,
                               &dummyDevicelinkTable))
      return FALSE;

    if (lastInList) {
      /* Check there is a usable output table for the final, or only, profile.
       * We allow, e.g. Lab colorspace conversion profiles, in other slots of a
       * profile array.
       */
      if (!outputTablePresent)
        return detail_error_handler(CONFIGURATIONERROR,
                                    "No output table in output profile");
    }

    /* Verify that we don't have a devicelink */
    if (pcsspace != SPACE_ICCLab && pcsspace != SPACE_ICCXYZ)
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Invalid use of devicelink profile");

    linkInfo->inputColorSpaceId = pcsspace;
    linkInfo->outputColorSpaceId = SPACE_ICCBased;
    linkInfo->u.icc = pInfo;
  }
  else if (oType(*profileObj) == ODICTIONARY) {
    /* Must be an Hqn Profile */

    HQN_PROFILE_INFO  *pInfo;

    *dimensions = 0;
    *devicespace = SPACE_notset;
    if (!cc_hqnprofile_createInfo(colorInfo, profileObj, &pInfo, dimensions, devicespace))
      return FALSE;

    linkInfo->inputColorSpaceId = SPACE_HqnPCS;
    linkInfo->outputColorSpaceId = SPACE_HqnProfile;
    linkInfo->u.hqnprofile = pInfo;
  }
  else if (oType(*profileObj) == OARRAY) {
    /* CMM color spaces are allowed, with conditions */

    int32 inputDimension;
    COLORSPACE_ID colorSpaceId;

    if (!cc_createTransformInfo(colorInfo,
                                linkInfo,
                                profileObj))
      return FALSE;

    if ( !gsc_getcolorspacesizeandtype(colorInfo, profileObj,
                                       &colorSpaceId, &inputDimension) )
      return FALSE;

    if (colorSpaceId != SPACE_CMM)
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Color space used as output profile");

    /* We only verify the number of components and not the device space by design */
    if (inputDimension != NUMBER_XYZ_COMPONENTS)
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Custom CMM color space used as output profile must be XYZ");

    *dimensions = cc_customcmm_nOutputChannels(linkInfo->u.customcmm);

    /** \todo JJ - instead, verify that this out dim matches the next in dim.
     * This isn't the right place for this test. Although useful if the CMM color
     * space is acting as an abstract profile, it doesn't work if it is acting
     * as a device link.
    if (*dimensions != NUMBER_XYZ_COMPONENTS)
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Custom CMM color space used as output profile must be XYZ");
     ****/
  }
  else {
    HQFAIL("Expected a file, dictionary, or an array");
    return FALSE;
  }

  return TRUE ;
}

static Bool extractProfileList(OBJECT        *profileObj,
                               GS_COLORinfo  *colorInfo,
                               PROFILEinfo   *prof,
                               COLORSPACE_ID *validDeviceSpace,
                               int32         *validDimensions)
{
  int32 dimensions = 0;
  COLORSPACE_ID devicespace = SPACE_notset;
  GS_HCMSinfo *hcmsInfo = colorInfo->hcmsInfo;

  /* If the profile is explicitly declared as null, we'll ignore it for now */
  if (oType(*profileObj) == ONULL)
    return TRUE;

  /* Care needed with arrays. These could be either a color space to be treated
   * as a single profile, or an array of profiles for handling abstract profiles.
   * There should be no confusion between these 2 cases.
   */
  if (oType(*profileObj) == OARRAY && oType(oArray(*profileObj)[0]) != ONAME) {
    /* For handling arrays of profiles. This would normally be because the
     * configuration contained an abstract profile, or to be more precise, a
     * profile that converts from PCS to PCS. It is expected that the final
     * profile in the sequence will have a device space. */
    int32 i;
    int32 len = theLen(*profileObj);
    TRANSFORM_LINK_INFO *linkInfo = &prof->p;

    /* If necessary, allocate an extension to the normal profile slot. */
    if (!allocProfileList(prof, len))
      return FALSE;

    /* Extract the list of profiles from the array */
    profileObj = oArray(*profileObj);
    for (i = 0; i < len; i++) {
      if (!extractProfile(&profileObj[i], colorInfo, i == len - 1,
                          linkInfo, &devicespace, &dimensions))
        return FALSE;
      /** \todo JJ. Verify that a sequence terminates at the FIRST device space */
      linkInfo = linkInfo->next;
    }

    /* Prevent findIntercept using a NextDevice as the intercept when an abstract
     * profile is present. That means that the NextDevice dict won't be applied
     * for objects in an untagged and non-intercepted device space.
     */
    if (len > 1)
      hcmsInfo->useReproductionAsIntercept = FALSE;
  }
  else {
    /* Must be a singleton Profile */
    if (!extractProfile(profileObj, colorInfo, TRUE,
                        &prof->p, &devicespace, &dimensions))
      return FALSE;
  }

  /* Verify that all profiles have the same device space */
  if (*validDeviceSpace == SPACE_notset) {
    HQASSERT(*validDimensions == 0, "validDimensions already set");
    *validDeviceSpace = devicespace;
    *validDimensions = dimensions;
  }
  else if (*validDeviceSpace != devicespace || *validDimensions != dimensions)
    return detail_error_handler(CONFIGURATIONERROR,
                                "Multiple profiles with different device space's");
  HQASSERT(*validDimensions != 0, "validDimensions not set");

  return TRUE ;
}

/* ------------------------------------------------------------------------ */

/* Handler for the IntentMappings key of setreproduction.
 * We expect a dictionary object containing optional keys of RelativeColorimetric,
 * Perceptual, Saturation, AbsoluteColorimetric, Picture, Text, Vignette, and
 * Other, i.e. each of the ICC rendering intents, and each of the reproduction
 * types. Each of the reproduction types will contain a dictionary of intents
 * keyed against the color models of Default, CMYK, RGB, Gray, and NamedColor.
 *
 * At the end of the process, we will have an overide intent associated with
 * each of the 4 ICC intents and a further override for the 20 combinations of
 * reproduction type and color model. When present, the reproType override takes
 * precedence. With no overrides, the original intent from the job or setup
 * will be used.
 *
 * An example of an IntentMappings dictionary:
 * <<
 *    /Perceptual            /Perceptual
 *    /RelativeColorimetric  /Perceptual
 *    /Saturation            /Perceptual
 *    /AbsoluteColorimetric  /Perceptual
 *    /Picture <<
 *      /Default  RelativeColorimetric
 *      /CMYK     Perceptual
 *    >>
 * >>
 * where all reproTypes's are allowed as top levels keys and all colorModels are
 * allowed within each sub-dictionary.
 */
static Bool doIntentMappings(OBJECT            *intentsObj,
                             REPRODUCTIONinfo  *reproductionInfo)
{
  int32 i;
  int32 j;

  if (oType(*intentsObj) == ONAME) {
    /* The IntentMapping is a name. This means that all ICC and reproType overrides
     * will be set to the same value.
     */
    for (i = 0; i < SW_CMM_N_ICC_RENDERING_INTENTS; i++)
      reproductionInfo->iccIntentMappingNames[i] = oNameNumber(*intentsObj);

    for (i = 0; i < REPRO_N_TYPES; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        reproductionInfo->objectIntentNames[i][j] = oNameNumber(*intentsObj);
      }
    }
  }
  else {
    NAMETYPEMATCH reproType_match[reproType_n_entries + 1];
    NAMETYPEMATCH colorModel_match[colorModel_n_entries + 1];

    enum {intent_perceptual, intent_rel_colorimetric, intent_saturation,
          intent_abs_colorimetric, intent_n_entries
    };
    static NAMETYPEMATCH intentsICC_match[SW_CMM_N_ICC_RENDERING_INTENTS + 1] = {
      { NAME_Perceptual | OOPTIONAL,            2, { ONAME, ONULL }},
      { NAME_RelativeColorimetric | OOPTIONAL,  2, { ONAME, ONULL }},
      { NAME_Saturation | OOPTIONAL,            2, { ONAME, ONULL }},
      { NAME_AbsoluteColorimetric | OOPTIONAL,  2, { ONAME, ONULL }},
      DUMMY_END_MATCH
    };

    HQASSERT(oType(*intentsObj) == ODICTIONARY, "Expected a dictionary");

    /* Copy reproType_match from reproType_matchTemplate to allow specialisation */
    HqMemCpy(reproType_match, reproType_matchTemplate, sizeof(reproType_matchTemplate));
    for (i = 0; i < reproType_n_entries; i++) {
      reproType_match[i].count = 2;
      reproType_match[i].match[0] = ODICTIONARY;
      reproType_match[i].match[1] = ONULL;
    }

    /* Copy colorModel_match from colorModel_matchTemplate to allow specialisation */
    HqMemCpy(colorModel_match, colorModel_matchTemplate, sizeof(colorModel_matchTemplate));
    for (i = 0; i < colorModel_n_entries; i++) {
      colorModel_match[i].count = 2;
      colorModel_match[i].match[0] = ONAME;
      colorModel_match[i].match[1] = ONULL;
    }

    if (!checkExcessKeys2(intentsObj, intentsICC_match, reproType_match))
      return FALSE;


    /* Extract the current ICC intent, if present, and process it's value */
    if (!dictmatch(intentsObj, intentsICC_match))
      return FALSE;

    if (intentsICC_match[intent_perceptual].result != NULL) {
      if (!extractIntent(intentsICC_match[intent_perceptual].result,
                         &reproductionInfo->iccIntentMappingNames[SW_CMM_INTENT_PERCEPTUAL]))
        return FALSE;
    }
    if (intentsICC_match[intent_rel_colorimetric].result != NULL) {
      if (!extractIntent(intentsICC_match[intent_rel_colorimetric].result,
                         &reproductionInfo->iccIntentMappingNames[SW_CMM_INTENT_RELATIVE_COLORIMETRIC]))
        return FALSE;
    }
    if (intentsICC_match[intent_saturation].result != NULL) {
      if (!extractIntent(intentsICC_match[intent_saturation].result,
                         &reproductionInfo->iccIntentMappingNames[SW_CMM_INTENT_SATURATION]))
        return FALSE;
    }
    if (intentsICC_match[intent_abs_colorimetric].result != NULL) {
      if (!extractIntent(intentsICC_match[intent_abs_colorimetric].result,
                         &reproductionInfo->iccIntentMappingNames[SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC]))
        return FALSE;
    }


    if (!dictmatch(intentsObj, reproType_match))
      return FALSE;

    /* Loop over the reproduction types, which are the remaining valid keys, and
     * extract the values for default and color model specific keys.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      if (reproType_match[i].result != NULL) {
        if (oType(*reproType_match[i].result) == ODICTIONARY) {
          if (!checkExcessKeys(reproType_match[i].result, colorModel_match))
            return FALSE;

          if (!dictmatch(reproType_match[i].result, colorModel_match))
            return FALSE;

          /* Initialise intents for this reproType from the default colorModel */
          if (colorModel_match[colorModel_Default].result != NULL) {
            int32 defaultIntentName = NAME_Default;

            if (!extractIntent(colorModel_match[colorModel_Default].result,
                               &defaultIntentName))
              return FALSE;

            for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
              reproductionInfo->objectIntentNames[i][j] = defaultIntentName;
            }
          }

          /* Now set the intents for each colorModel */
          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            if (colorModel_match[j].result != NULL) {
              if (!extractIntent(colorModel_match[j].result,
                                 &reproductionInfo->objectIntentNames[i][j]))
                return FALSE;
            }
          }
        }
        else
          HQASSERT(oType(*reproType_match[i].result) == ONULL,
                   "Expected null object");
      }
    }
  }

  return TRUE;
}

/* Handler for individual entries within the IntentMappings dictionary.
 * The intentObj should be null or the name of a rendering intent. If it's null
 * then it's equivalent to using the default value.
 */
static Bool extractIntent(OBJECT *intentObj,
                         int32   *intentName)
{
  int32 intentNameNumber;

  /* If the intent is explicitly declared as null, it means only use the intent
   * mappings and ignore the reproType overrides.
   */
  if (oType(*intentObj) == ONULL) {
    *intentName = NAME_Default;
    return TRUE;
  }

  HQASSERT(oType(*intentObj) == ONAME,
           "Expected a name");

  intentNameNumber = oNameNumber(*intentObj);

  if (!validIntent(intentNameNumber))
    return error_handler(RANGECHECK);

  *intentName = intentNameNumber;

  return TRUE;
}

/* ------------------------------------------------------------------------ */

/* Handler for the InputColorSpace key of setreproduction.
 * We expect one of two forms for the value. One is a valid colorspace array
 * (except for Separation or DeviceN) which will be used for all conversions in
 * within the NextDevice stage.
 * The other form is a dictionary object containing optional keys of Default,
 * Picture, Text, Vignette, and Other. Each of the reproduction types contains a
 * dictionary of colorspace arrays keyed against the color models of Default,
 * CMYK, RGB, Gray, and NamedColor.
 * At the end of the process, we will have a TRANSFORM_LINK_INFO associated with
 * each of the 20 combinations of reproduction type and color model.
 *
 * An example of an InputColorSpace dictionary:
 * <<
 *    /Default  [ /ICCBased <filestream1> ]
 *    /Picture <<
 *      /Default  [ /ICCBased <filestream2> ]
 *      /CMYK     /DeviceCMYK
 *    >>
 *    /Text <<
 *      /Default  [ /CMM (ConvertToBlack) .... ]
 *    >>
 * >>
 * where all reproTypes's are allowed as top levels keys and all colorModels are
 * allowed within each sub-dictionary.
 */
static Bool doInputColorSpace(OBJECT            *inputObj,
                              REPRODUCTIONinfo  *reproductionInfo,
                              GS_COLORinfo      *colorInfo)
{
  int32 i;
  int32 j;

  if (reproductionInfo->firstInSequence)
    return detail_error_handler(CONFIGURATIONERROR,
                                "InputColorSpace is only allowed in NextDevice dictionaries");


  if (oType(*inputObj) == OARRAY) {
    TRANSFORM_LINK_INFO defaultInput;
    cc_initTransformInfo(&defaultInput);

    if (!extractInputColorSpace(inputObj,
                                &defaultInput,
                                reproductionInfo,
                                colorInfo))
      return FALSE;

    /* Initialise all profiles with the default */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        cc_destroyTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
        reproductionInfo->inputColorSpaceInfo[i][j] = defaultInput;
        cc_reserveTransformInfo(&defaultInput);

        reproductionInfo->inputColorSpaceIsDeviceLink[i][j] = cc_isDevicelink(&defaultInput);
      }
    }
    /* And pop off one reserve level so the first one doesn't count twice */
    cc_destroyTransformInfo(&defaultInput);
  }
  else if (oType(*inputObj) == ODICTIONARY) {
    NAMETYPEMATCH reproType_match[reproType_n_entries + 1];
    NAMETYPEMATCH colorModel_match[colorModel_n_entries + 1];

    /* Copy reproType_match from reproType_matchTemplate to allow specialisation.
     * The Default case is different because it requires a color space, while the
     * object specific keys require a dictionary for the color models.
     */
    HqMemCpy(reproType_match, reproType_matchTemplate, sizeof(reproType_matchTemplate));
    for (i = 0; i < reproType_n_entries; i++) {
      reproType_match[i].count = 2;
      reproType_match[i].match[0] = ODICTIONARY;
      reproType_match[i].match[1] = ONULL;
    }
    reproType_match[reproType_Default].count = 3;
    reproType_match[reproType_Default].match[0] = ONAME;
    reproType_match[reproType_Default].match[1] = OARRAY;
    reproType_match[reproType_Default].match[2] = ONULL;

    /* Copy colorModel_match from colorModel_matchTemplate to allow specialisation */
    HqMemCpy(colorModel_match, colorModel_matchTemplate, sizeof(colorModel_matchTemplate));
    for (i = 0; i < colorModel_n_entries; i++) {
      colorModel_match[i].count = 3;
      colorModel_match[i].match[0] = ONAME;
      colorModel_match[i].match[1] = OARRAY;
      colorModel_match[i].match[2] = ONULL;
    }


    if (!checkExcessKeys(inputObj, reproType_match))
      return FALSE;

    if (!dictmatch(inputObj, reproType_match))
      return FALSE;

    /* Extract the Default, if present, and initialise all values to it */
    if (reproType_match[reproType_Default].result != NULL) {
      TRANSFORM_LINK_INFO defaultInput;
      cc_initTransformInfo(&defaultInput);

      if (!extractInputColorSpace(reproType_match[reproType_Default].result,
                                  &defaultInput,
                                  reproductionInfo,
                                  colorInfo))
        return FALSE;

      /* Initialise all profiles with the default */
      for (i = 0; i < REPRO_N_TYPES; i++) {
        for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
          cc_destroyTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
          reproductionInfo->inputColorSpaceInfo[i][j] = defaultInput;
          cc_reserveTransformInfo(&defaultInput);

          reproductionInfo->inputColorSpaceIsDeviceLink[i][j] = cc_isDevicelink(&defaultInput);
        }
      }
      /* And pop off one reserve level so the first one doesn't count twice */
      cc_destroyTransformInfo(&defaultInput);
    }

    /* Loop over the reproduction types, which are the remaining valid keys, and
     * extract the values for default and color model specific keys.
     */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      if (reproType_match[i].result != NULL) {
        /* If the value of the reproduction type key is a dictionary, then we look
         * within it for color model keys.
         */
        if (oType(*reproType_match[i].result) == ODICTIONARY) {
          /* Ensure there are no stray keys in reproType specific dict */
          if (!checkExcessKeys(reproType_match[i].result, colorModel_match))
            return FALSE;

          /* Extract the values of all color model keys */
          if (!dictmatch(reproType_match[i].result, colorModel_match))
            return FALSE;

          /* Initialise profiles for this reproType from the default colorModel */
          if (colorModel_match[colorModel_Default].result != NULL) {
            TRANSFORM_LINK_INFO defaultInput;
            cc_initTransformInfo(&defaultInput);

            if (!extractInputColorSpace(colorModel_match[colorModel_Default].result,
                                        &defaultInput,
                                        reproductionInfo,
                                        colorInfo))
              return FALSE;
            for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
              cc_destroyTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
              reproductionInfo->inputColorSpaceInfo[i][j] = defaultInput;
              cc_reserveTransformInfo(&defaultInput);

              reproductionInfo->inputColorSpaceIsDeviceLink[i][j] = cc_isDevicelink(&defaultInput);
            }
            /* And pop off one reserve level so the first one doesn't count twice */
            cc_destroyTransformInfo(&defaultInput);
          }

          /* Now set the values for each reproType/colorModel combination.
           */
          for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
            if (colorModel_match[j].result != NULL) {
              cc_destroyTransformInfo(&reproductionInfo->inputColorSpaceInfo[i][j]);
              if (!extractInputColorSpace(colorModel_match[j].result,
                                          &reproductionInfo->inputColorSpaceInfo[i][j],
                                          reproductionInfo,
                                          colorInfo))
                return FALSE;

              reproductionInfo->inputColorSpaceIsDeviceLink[i][j] =
                      cc_isDevicelink(&reproductionInfo->inputColorSpaceInfo[i][j]);
            }
          }
        }
        else
          HQASSERT(oType(*reproType_match[i].result) == ONULL,
                   "Expected null object");
      }
    }
  }
  else
    HQASSERT(oType(*inputObj) == ONULL, "Expected null object");

  return TRUE;
}

/* Handler for individual entries within the InputColorSpace dictionary.
 * The inputObj should be null or a valid color space, but not Separation or
 * DeviceN.
 */
static Bool extractInputColorSpace(OBJECT               *inputObj,
                                   TRANSFORM_LINK_INFO  *linkInfo,
                                   REPRODUCTIONinfo     *reproductionInfo,
                                   GS_COLORinfo         *colorInfo)
{
  int32 inputDimension;
  int32 outputDimension;
  COLORSPACE_ID colorSpaceId;

  /* If the inputColorSpace is explicitly declared as null, we'll ignore it */
  if (oType(*inputObj) == ONULL)
    return TRUE;

  HQASSERT(oType(*inputObj) == ONAME || oType(*inputObj) == OARRAY,
           "Expected a name or an array");

  if (!cc_createTransformInfo(colorInfo,
                              linkInfo,
                              inputObj))
    return FALSE;

  if ( !gsc_getcolorspacesizeandtype(colorInfo, inputObj,
                                     &colorSpaceId, &inputDimension) )
    return FALSE;

  /* InputColorSpace isn't allowed to be a Separation or DeviceN because we don't
   * currently have an info structure for them, only a CLINK which we can't use
   * here. NB. A devicelink profile can provide equivalent functionality.
   */
  if (colorSpaceId == SPACE_Separation || colorSpaceId == SPACE_DeviceN)
    return detail_error_handler(CONFIGURATIONERROR,
                                "InputColorSpace is a Separation or DeviceN");

  /* We only verify the number of components and not the device space by design */
  if (inputDimension != reproductionInfo->inputDimensions)
    return detail_error_handler(CONFIGURATIONERROR,
                                "InputColorSpace has an inconsistent number of channels");

  /* If we have a devicelink, we must do some more work */
  if (cc_isDevicelink(linkInfo)) {
    /* Get the number of output channels */
    outputDimension = nOutputChannels(linkInfo);

    /* If it's a devicelink, verify it's the same as the profile device space
     * for the sake of consistency, even though a Profile key will be ignored
     * in this reproduction dictionary.
     */
    if (reproductionInfo->outputDimensions == 0)
      reproductionInfo->outputDimensions = outputDimension;
    else if (reproductionInfo->outputDimensions != outputDimension)
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Multiple profiles with different device space's");
  }

  return TRUE;
}

/* ------------------------------------------------------------------------ */

/* Intended for PDF/X-3 & 4, this will use a profile provided by OutputIntents
 * functionality of PDF as the basis of color management by inserting it into
 * the reproduction dictionary.
 * If there isn't any current color management, this means fabricating a single
 * reproduction dictionary and installing it.
 * If there is current color management, it usually means creating an emulation
 * color managment workflow.
 * If we are color managing, and also using overrides, we'll make use of the
 * appropriate intercept profiles in the nested reproduction dict. Without
 * overrides, we'll ignore any intercepts and simply use the OutputIntents
 * profile as would be normal for an emulation workflow.
 */
Bool gsc_addOutputIntent(GS_COLORinfo *colorInfo, OBJECT *profileObj)
{
  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");
  HQASSERT(oType(*profileObj) == OFILE, "Expected a file");

  if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
      cc_invalidateColorChains(colorInfo, TRUE)) {
    REPRODUCTIONinfo tmpReproductionInfo;
    REPRODUCTIONinfo *nextDevice;
    PROFILEinfo defaultProfile = initProfileList();
    int32 i;
    int32 j;
    int32 tmpIntent;
    INTERCEPTinfo *interceptInfos;
    Bool *overrides;

    initReproductionInfo(&tmpReproductionInfo);

    if (!extractProfileList(profileObj,
                            colorInfo,
                            &defaultProfile,
                            &tmpReproductionInfo.outputDeviceSpaceId,
                            &tmpReproductionInfo.outputDimensions))
      return FALSE;

    /* Initialise all profiles with the default */
    for (i = 0; i < REPRO_N_TYPES; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        tmpReproductionInfo.objectProfile[i][j] = defaultProfile;
      }
    }

    /* Inherit the BPC setting whilst interpreting the job */
    tmpReproductionInfo.blackPointComp = colorInfo->hcmsInfo->reproductionInfo.blackPointComp;

    nextDevice = mm_sac_alloc(mm_pool_color,
                              sizeof(REPRODUCTIONinfo),
                              MM_ALLOC_CLASS_NCOLOR);
    if (nextDevice == NULL)
      return error_handler(VMERROR);

    /* A structure copy */
    tmpReproductionInfo.nextDevice = nextDevice;
    *tmpReproductionInfo.nextDevice = colorInfo->hcmsInfo->reproductionInfo;

    nextDevice->firstInSequence = FALSE;
    nextDevice->inputDeviceSpaceId = tmpReproductionInfo.outputDeviceSpaceId;
    nextDevice->inputDimensions = tmpReproductionInfo.outputDimensions;

    /* If overriding color management, we're going to inherit intercept
     * profiles from the setup, for use in managing color after conversion to
     * the OutputIntents to the device. We'll do this by passing the current
     * device intercept profiles to the nested reproduction dict via the
     * InputColorSpace. This will allow devicelink profiles to be used in
     * conjunction with PDF/X.
     * If we're not overriding, or there isn't an intercept, we'll treat the
     * OutputIntents as an emulation profile.
     */
    switch (nextDevice->inputDeviceSpaceId) {
    case SPACE_DeviceCMYK:
      interceptInfos = colorInfo->hcmsInfo->interceptCMYK;
      overrides = colorInfo->hcmsInfo->overrideCMYK;
      break;
    case SPACE_DeviceRGB:
      interceptInfos = colorInfo->hcmsInfo->interceptRGB;
      overrides = colorInfo->hcmsInfo->overrideRGB;
      break;
    case SPACE_DeviceGray:
      interceptInfos = colorInfo->hcmsInfo->interceptGray;
      overrides = colorInfo->hcmsInfo->overrideGray;
      break;
    default:
      /* In the exceptional cases where we get a DeviceN OutputIntents profile
       * we'll just treat it as an emulation profile.
       */
      interceptInfos = NULL;
      overrides = NULL;
      break;
    }

    if (interceptInfos != NULL) {
      for (i = 0; i < REPRO_N_TYPES; i++) {
        HQASSERT(nextDevice->inputColorSpaceInfo[i]->inputColorSpaceId == SPACE_notset &&
                 nextDevice->inputColorSpaceInfo[i]->u.shared == NULL,
                 "InputColorSpace isn't allowed for first reproduction dict");
        for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
          if (overrides[i] &&
              interceptInfos[i].info.inputColorSpaceId != SPACE_notset) {
            /* If we are overriding, use the intercept profile if one is set */
            nextDevice->inputColorSpaceInfo[i][j] = interceptInfos[i].info;
            cc_reserveTransformInfo(&nextDevice->inputColorSpaceInfo[i][j]);
          }
          else if (!nextDevice->validReproduction) {
            /* We haven't set an output profile via setreproduction, and we
             * aren't overriding and thus setting InputColorSpace to an intercept
             * profile. In this situation, we'll set this InputColorSpace to
             * the inputDeviceSpaceId because that will cause color management
             * to ignore this stage - simply leaving it at SPACE_notset will mean
             * using the OutputIntents as an emulation and the default output
             * profile (Bad). NB. We cannot simply drop the NextDevice dict
             * because an InputColorSpace for some other object type/color model
             * might be set.
             */
            nextDevice->inputColorSpaceInfo[i][j].inputColorSpaceId =
                      nextDevice->inputDeviceSpaceId;
          }
        }
      }
    }

    /** \todo @@JJ This sets and emulation intent to RelativeColorimetic. This
     * should be controllable in some way.
     */
    tmpIntent = NAME_RelativeColorimetric;
    for (i = 0; i < REPRO_N_TYPES; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        nextDevice->objectIntentNames[i][j] = tmpIntent;
      }
    }
    tmpReproductionInfo.validReproduction = TRUE;

    /* A structure copy */
    colorInfo->hcmsInfo->reproductionInfo = tmpReproductionInfo;
  }
  else
    return FALSE;

  return TRUE;
}

/* Intended for PDF/X-3 & 4. The jobs for which this applies will have self
 * contained color management assuming a virtual device characterised by the
 * OutputIntents profile. Whilst converting colors to the OutputIntents, we
 * shouldn't be honouring any device intercepts from the setup because that goes
 * against PDF/X. However, we should be honouring black and named color
 * intercepts, so this function preserves those settings.
 * NB. Device color intercepts are applied when converting colors from the
 *     Outputintents to the real device. That is what gsc_addOutputIntent() does.
 */
Bool gsc_pdfxResetIntercepts(GS_COLORinfo *colorInfo)
{
  GS_HCMSinfo *hcmsInfo;
  OBJECT namedColor = OBJECT_NOTVM_NOTHING;
  OBJECT black = OBJECT_NOTVM_NOTHING;
  OBJECT blackTint = OBJECT_NOTVM_NOTHING;
  Bool overprintPreview = colorInfo->hcmsInfo->overprintPreview;

  /* Preserve the black and named color intercepts */
  Copy(&namedColor, &colorInfo->hcmsInfo->interceptNamedColors->namedColorObject);
  Copy(&black, &colorInfo->hcmsInfo->interceptBlackObj);
  Copy(&blackTint, &colorInfo->hcmsInfo->interceptBlackTintObj);

  /* Initialise all intercepts */
  if (!gsc_setinterceptcolorspace(colorInfo, &onull))
    return FALSE;

  /* Restore the black and named color intercepts */
  hcmsInfo = colorInfo->hcmsInfo;
  if (!interceptNamedColors(hcmsInfo, &namedColor) ||
      !interceptBlack(&black, &hcmsInfo->interceptBlackObj, hcmsInfo->interceptBlack) ||
      !interceptBlack(&blackTint, &hcmsInfo->interceptBlackTintObj, hcmsInfo->interceptBlackTint))
    return FALSE;

  colorInfo->hcmsInfo->overprintPreview = overprintPreview;

  return TRUE;
}

/* ------------------------------------------------------------------------ */

Bool gsc_getreproduction(GS_COLORinfo *colorInfo, STACK *stack)
{
  GS_HCMSinfo  *hcmsInfo;

  HQASSERT(colorInfo->hcmsInfo != NULL, "colorInfo == NULL");

  hcmsInfo = colorInfo->hcmsInfo;

  return push(&hcmsInfo->reproductionObject, stack);
}

/* ------------------------------------------------------------------------ */

struct REPRO_ITERATOR {
  REPRODUCTIONinfo      *currentReproDict;
  TRANSFORM_LINK_INFO   currentProfile;
  Bool                  currentProfileIsInput;
  uint8                 reproObjectType;
  uint8                 origRenderingIntent;
  uint8                 currentRenderingIntent;
  REPRO_COLOR_MODEL     colorModel;
  Bool                  currentBlackPointComp;
  TRANSFORM_LINK_INFO   defaultCrdInfo;
};

void iteratorInit(REPRO_ITERATOR *iterator,
                  GS_COLORinfo   *colorInfo,
                  int32          colorType)
{
  REPRODUCTIONinfo *reproductionInfo;

  HQASSERT(iterator != NULL, "iterator NULL");
  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");
  COLORTYPE_ASSERT(colorType, "iteratorInit") ;
  HQASSERT(cc_getrenderingintent(colorInfo) < SW_CMM_N_ICC_RENDERING_INTENTS,
           "Invalid rendering intent");

  reproductionInfo = &colorInfo->hcmsInfo->reproductionInfo;

  iterator->currentReproDict = reproductionInfo;
  cc_initTransformInfo(&iterator->currentProfile);
  iterator->currentProfileIsInput = FALSE;

  iterator->reproObjectType = colorInfo->hcmsInfo->requiredReproType[colorType];
  iterator->origRenderingIntent = colorInfo->hcmsInfo->renderingIntent;
  iterator->currentRenderingIntent = iterator->origRenderingIntent;
  iterator->currentBlackPointComp = reproductionInfo->blackPointComp;
  iterator->colorModel = colorInfo->hcmsInfo->colorModel[colorType];

  cc_initTransformInfo(&iterator->defaultCrdInfo);
  iterator->defaultCrdInfo.inputColorSpaceId = SPACE_CIEXYZ;
  /* Deliberately not set because we don't know. It shouldn't matter because
   * we're about to return to the main chain construction.
   */
  iterator->defaultCrdInfo.outputColorSpaceId = SPACE_notset;
  iterator->defaultCrdInfo.u.crd = colorInfo->crdInfo;
}

Bool cc_reproductionIteratorInit(GS_COLORinfo         *colorInfo,
                                 int32                colorType,
                                 TRANSFORM_LINK_INFO  *currentReproSpaceInfo,
                                 REPRO_ITERATOR       **reproIterator)
{
  REPRODUCTIONinfo *reproductionInfo = &colorInfo->hcmsInfo->reproductionInfo;
  REPRO_ITERATOR *iterator;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "cc_reproductionIteratorInit") ;
  HQASSERT(currentReproSpaceInfo != NULL, "colorInfo NULL");
  HQASSERT(reproIterator != NULL, "reproIterator NULL");

  *reproIterator = NULL;

  /* If setreproduction hasn't been called then return TRUE with no iterator */
  if (!reproductionInfo->validReproduction)
    return TRUE;

  iterator = mm_alloc(mm_pool_color,
                      sizeof (REPRO_ITERATOR),
                      MM_ALLOC_CLASS_NCOLOR);
  if (iterator == NULL)
    return error_handler(VMERROR);

  iteratorInit(iterator, colorInfo, colorType);

  /* If the currentColorSpaceId is a device space, then the previous profile is
   * either a devicelink profile or a raw device space, so we must ignore the
   * top level reproduction dictionary and move on to the next one. This is a
   * hack that treats the intercept devicelink the same as a devicelink
   * InputColorSpace in a lower level reproduction dict.
   */
  if (cc_isDevicelink(currentReproSpaceInfo)) {
    iterator->currentProfile =
        iterator->currentReproDict->objectProfile[iterator->reproObjectType][iterator->colorModel].p;
    iterator->currentProfileIsInput = TRUE;

    /* A hack that bails out of further colour management if we have a devicelink
     * that has a different number of components to the first profile in the
     * reproduction dictionary.
     */
    if (nOutputChannels(currentReproSpaceInfo) != reproductionInfo->outputDimensions) {
      /** \todo JJ 2011-07-14. 65246, should be cleverer about this. We need to
       * cope with a devicelink that maps onto the output profile with
       * additional components that are present in the output raster style.
       * Commented out for now.
       */
#if 0
      HQTRACE(currentReproSpaceInfo->outputColorSpaceId != SPACE_notset &&
              colorInfo->hcmsInfo->trace_devicelinkMismatch++ == 0,
             ("Input devicelink profile doesn't match colour setup - not using setreproduction"));
      iterator->currentReproDict = NULL;
#endif
    }
  }

  *reproIterator = iterator;

  return TRUE;
}

void cc_reproductionIteratorNext(REPRO_ITERATOR       *iterator,
                                 TRANSFORM_LINK_INFO  *nextReproSpaceInfo,
                                 Bool                 *startNewTransform)
{
  Bool nextDevice = FALSE;
  Bool terminateIteration = FALSE;
  REPRODUCTIONinfo *currentReproDict = iterator->currentReproDict;
  uint8 reproObjectType = iterator->reproObjectType;
  REPRO_COLOR_MODEL colorModel = iterator->colorModel;

  HQASSERT(iterator != NULL, "iterator NULL");

  *startNewTransform = FALSE;

  if (currentReproDict != NULL) {

    if (iterator->currentProfile.inputColorSpaceId == SPACE_notset || iterator->currentProfileIsInput) {

      /* If currentProfile is an InputColorSpace that is a devicelink, then
       * ignore any Profile that might exist in currentReproDict and move on to
       * the next one.
       */
      if (iterator->currentProfile.inputColorSpaceId != SPACE_notset && iterator->currentProfileIsInput) {
        if (cc_isDevicelink(&iterator->currentProfile))
          nextDevice = TRUE;
      }

      if (!nextDevice) {
        int32 objectIntentName = currentReproDict->objectIntentNames[reproObjectType][colorModel];

        /* Obtain the appropriate profile as determined from the hierarchy defined
         * in setreproduction.
         * Follow sequences which normally include abstract profiles, if they exist.
         */
        if (iterator->currentProfile.next != NULL)
          iterator->currentProfile = *iterator->currentProfile.next;
        else
          iterator->currentProfile =
                currentReproDict->objectProfile[reproObjectType][colorModel].p;

        /* Obtain the appropriate intent as determined from the hierarchy defined
         * in setreproduction for the reproTypes, or from iccIntentMappingNames.
         */
        if (objectIntentName == NAME_Default)
          convertIntent(currentReproDict->iccIntentMappingNames[iterator->origRenderingIntent],
                        &iterator->currentRenderingIntent);
        else
          convertIntent(objectIntentName, &iterator->currentRenderingIntent);

        /* If there isn't a profile configured for this object, then use the crd,
         * but terminate colour management at this point.
         */
        if (iterator->currentProfile.inputColorSpaceId == SPACE_notset) {
          iterator->currentProfile = iterator->defaultCrdInfo;
          terminateIteration = TRUE;
        }
      }
      iterator->currentProfileIsInput = FALSE;
    }
    else {
      HQASSERT(!iterator->currentProfileIsInput, "currentProfileIsInput TRUE");

      /* Follow sequences which normally include follow-on color spaces, if they exist. */
      if (iterator->currentProfile.next != NULL)
        iterator->currentProfile = *iterator->currentProfile.next;
      else
        nextDevice = TRUE;
    }

    if (nextDevice) {
      iterator->currentReproDict = currentReproDict->nextDevice;
      currentReproDict = iterator->currentReproDict;

      if (currentReproDict != NULL) {
        int32 objectIntentName;

        /* We're on to the NextDevice. The next profile will normally be the same
         * as the output profile from the previous device, but may be overridden
         * by an InputColorSpace
         */
        if (currentReproDict->inputColorSpaceInfo[reproObjectType][colorModel].inputColorSpaceId != SPACE_notset) {
          *startNewTransform = TRUE;
          iterator->currentProfile =
                currentReproDict->inputColorSpaceInfo[reproObjectType][colorModel];

          objectIntentName = currentReproDict->sourceIntentName;
        }
        else {
          COLORSPACE_ID tmp = iterator->currentProfile.inputColorSpaceId;

          /* The currentProfile remains the same, but we use the input tables
           * and swop the input/output indicators. */
          iterator->currentProfile.inputColorSpaceId = iterator->currentProfile.outputColorSpaceId;
          iterator->currentProfile.outputColorSpaceId = tmp;

          objectIntentName = currentReproDict->objectIntentNames[reproObjectType][colorModel];

          /** \todo @@JJ THIS MUST BE REMOVED, but can't be until the alternate CMM is improved */
          *startNewTransform = TRUE;
        }

        /* Obtain the appropriate intent as determined from the hierarchy defined
         * in setreproduction for the reproTypes, or from iccIntentMappingNames.
         */
        if (objectIntentName == NAME_Default)
          convertIntent(currentReproDict->iccIntentMappingNames[iterator->origRenderingIntent],
                        &iterator->currentRenderingIntent);
        else
          convertIntent(objectIntentName, &iterator->currentRenderingIntent);
      }
      iterator->currentProfileIsInput = TRUE;
    }
  }

  if (currentReproDict != NULL) {
    /* A structure copy */
    *nextReproSpaceInfo = iterator->currentProfile;
    /* But set the rendering intent & BPC appropriate for the current object */
    nextReproSpaceInfo->intent = iterator->currentRenderingIntent;
    nextReproSpaceInfo->blackPointComp = currentReproDict->blackPointComp;
  }
  else {
    cc_initTransformInfo(nextReproSpaceInfo);
  }

  if (terminateIteration)
    iterator->currentReproDict = NULL;
}

void cc_reproductionIteratorFinish(REPRO_ITERATOR **reproIterator)
{
  REPRO_ITERATOR *iterator;

  HQASSERT(reproIterator != NULL, "reproIterator NULL");
  iterator = *reproIterator;

  mm_free(mm_pool_color, iterator, sizeof (REPRO_ITERATOR));
  *reproIterator = NULL;
}

/* Return the initial rendering intent to allow the chain construction to set
 * the intent for the first profile in a transform. This is only used in the
 * blend spaces + first ReproDict.
 */
uint8 cc_sourceRenderingIntent(GS_COLORinfo   *colorInfo,
                               int32          colorType)
{
  TRANSFORM_LINK_INFO  reproSpaceInfo;
  REPRO_ITERATOR iterator;
  uint8 sourceIntent;
  Bool dummy;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "cc_reproductionIteratorInit") ;

  /* Special case to avoid calling setreproduction code */
  if (cc_getrenderingintent(colorInfo) == SW_CMM_INTENT_NONE)
    return SW_CMM_INTENT_NONE;

  iteratorInit(&iterator, colorInfo, colorType);
  cc_reproductionIteratorNext(&iterator, &reproSpaceInfo, &dummy);

  sourceIntent = reproSpaceInfo.intent;

  /* If the value is Default then the first profile will end up using the same
   * intent as the next profile.
   */
  if (colorInfo->hcmsInfo->reproductionInfo.sourceIntentName != NAME_Default) {
    convertIntent(iterator.currentReproDict->sourceIntentName,
                  &sourceIntent);
  }

  return sourceIntent;
}

/* Return the BPC value from the first reproduction dict to allow the chain
 * construction to set the value for the first profile in a transform. Our
 * implementation requires that all profiles in a transform be marked if BPC
 * is going to be applied.
 */
Bool cc_sourceBlackPointComp(GS_COLORinfo   *colorInfo)
{
  return colorInfo->hcmsInfo->reproductionInfo.blackPointComp;
}

/* ------------------------------------------------------------------------ */

static Bool validIntent(int32 intentNameNumber)
{
  switch (intentNameNumber) {
  case NAME_Perceptual:
  case NAME_RelativeColorimetric:
  case NAME_Saturation:
  case NAME_AbsoluteColorimetric:
  case NAME_AbsolutePerceptual:
  case NAME_AbsoluteSaturation:
    return TRUE;
  default:
    return FALSE;
  }

  /* NOT REACHED */
}

static void convertIntent(int32 intentNameNumber, uint8 *convertedIntent)
{
  switch (intentNameNumber) {
  case NAME_Perceptual:
    *convertedIntent = SW_CMM_INTENT_PERCEPTUAL;
    break;
  case NAME_RelativeColorimetric:
    *convertedIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    break;
  case NAME_Saturation:
    *convertedIntent = SW_CMM_INTENT_SATURATION;
    break;
  case NAME_AbsoluteColorimetric:
    *convertedIntent = SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC;
    break;
  case NAME_AbsolutePerceptual:
    *convertedIntent = SW_CMM_INTENT_ABSOLUTE_PERCEPTUAL;
    break;
  case NAME_AbsoluteSaturation:
    *convertedIntent = SW_CMM_INTENT_ABSOLUTE_SATURATION;
    break;
  default:
    HQFAIL("Unexpected intent");
    *convertedIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    break;
  }
}

/* ------------------------------------------------------------------------ */

static void convertReproTypeNameNumber(int32 reproTypeName, uint8 *reproType)
{
  switch (reproTypeName) {
  case NAME_Picture:
    *reproType = REPRO_TYPE_PICTURE;
    break;
  case NAME_Vignette:
    *reproType = REPRO_TYPE_VIGNETTE;
    break;
  case NAME_Text:
    *reproType = REPRO_TYPE_TEXT;
    break;
  case NAME_Other:
    *reproType = REPRO_TYPE_OTHER;
    break;
  default:
    *reproType = REPRO_TYPE_ILLEGAL;
    break;
  }
}

enum {
  misc_TreatOneBitImagesAs, misc_TreatSingleRowImagesAs,
  misc_n_entries
};
static NAMETYPEMATCH misc_match[misc_n_entries + 1] = {
  { NAME_TreatOneBitImagesAs | OOPTIONAL,     1, { ONAME }},
  { NAME_TreatSingleRowImagesAs | OOPTIONAL,  1, { ONAME }},
  DUMMY_END_MATCH
};

/* This controls how some, important, corner cases get handled.
 * By default, 1-bit images are assumed to be pre-screened, so they get handled
 * as linework. It is possible that they were originally 8-bit images that got
 * optimised upstream because all the pixels were full or clear, in which case
 * we want them handled as a Picture.
 * Single row images are handled as Vignettes by default because graphic arts
 * often implemented vignettes as images before shaded fills were widely used.
 * Pages that have come from the Microsoft driver often have images broken up
 * into smaller images just a few, or even one, pixel high. We want these to be
 * handled as Picture.
 */
Bool gsc_setmiscobjectmappings(GS_COLORinfo *colorInfo, OBJECT *objectDict)
{
  GS_HCMSinfo     *hcmsInfo;
  OBJECT          *theo;

  HQASSERT(colorInfo != NULL, "colorInfo == NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo == NULL");

  if (oType(*objectDict) != ODICTIONARY)
    return error_handler( TYPECHECK ) ;

  if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
      cc_invalidateColorChains(colorInfo, TRUE)) {
    hcmsInfo = colorInfo->hcmsInfo;

    if (!checkExcessKeys(objectDict, misc_match))
      return FALSE;

    if (!dictmatch(objectDict, misc_match))
      return FALSE;

    theo = misc_match[misc_TreatOneBitImagesAs].result;
    if (theo != NULL) {
      uint8 reproType;
      convertReproTypeNameNumber(oNameNumber(*theo), &reproType);
      if (reproType == REPRO_TYPE_ILLEGAL)
        return error_handler(RANGECHECK);

      hcmsInfo->treatOneBitImagesAs = reproType;
    }

    theo = misc_match[misc_TreatSingleRowImagesAs].result;
    if (theo != NULL) {
      uint8 reproType;
      convertReproTypeNameNumber(oNameNumber(*theo), &reproType);
      if (reproType == REPRO_TYPE_ILLEGAL)
        return error_handler(RANGECHECK);

      hcmsInfo->treatSingleRowImagesAs = reproType;
    }
  }
  else
    return FALSE;

  return TRUE;
}

Bool gsc_getmiscobjectmappings(GS_COLORinfo *colorInfo, STACK *stack)
{
  UNUSED_PARAM(GS_COLORinfo *, colorInfo);
  UNUSED_PARAM(STACK *, stack);

  return detail_error_handler(RANGECHECK, "Not yet implemented");
}

uint8 gsc_getTreatOneBitImagesAs(GS_COLORinfo *colorInfo)
{
  HQASSERT(colorInfo != NULL, "colorInfo == NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo == NULL");

  return colorInfo->hcmsInfo->treatOneBitImagesAs;
}

uint8 gsc_getTreatSingleRowImagesAs(GS_COLORinfo *colorInfo)
{
  HQASSERT(colorInfo != NULL, "colorInfo == NULL");
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo == NULL");

  return colorInfo->hcmsInfo->treatSingleRowImagesAs;
}

/* ------------------------------------------------------------------------ */

int gsc_reproTypePriority(uint8 reproType)
{
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  return reproTypePriority[reproType];
}

Bool gsc_setRequiredReproType(GS_COLORinfo *colorInfo, int32 colorType,
                              uint8 reproType)
{
  GS_HCMSinfo  *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_setRequiredReproType") ;
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");

  hcmsInfo = colorInfo->hcmsInfo;

  /* Avoid creating a new info structure if the intent hasn't changed
   */
  if (hcmsInfo->requiredReproType[colorType] == reproType)
    return TRUE;

  if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
      cc_invalidateChain(colorInfo, colorType, FALSE)) {
    hcmsInfo = colorInfo->hcmsInfo;

    hcmsInfo->requiredReproType[colorType] = reproType;
  }
  else
    return FALSE;

  return TRUE;
}

uint8 gsc_getRequiredReproType(const GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_HCMSinfo  *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_getRequiredReproType") ;
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  hcmsInfo = colorInfo->hcmsInfo;

  return hcmsInfo->requiredReproType[colorType];
}

Bool gsc_resetRequiredReproType(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_HCMSinfo  *hcmsInfo;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_resetRequiredReproType") ;
  HQASSERT(colorInfo->hcmsInfo != NULL, "hcmsInfo NULL");

  hcmsInfo = colorInfo->hcmsInfo;

  /* Reset the requiredReproType for fills and stroke chains only because:
   * - images, shfill and backdrop chains set the reproType for every object.
   * - images will get their intent changed in the invokeBlock calls if the
   *   reproType is changed here.
   * This fact makes this function very ticklish, but I don't see any cleaner
   * way of resetting reproType for Text objects in particular.
   */
  switch (colorType) {
  case GSC_FILL:
  case GSC_STROKE:
    if (!gsc_setRequiredReproType(colorInfo, colorType, defaultReproTypes[colorType]))
      return FALSE;
  }

  return TRUE;
}

NAMECACHE *gsc_getReproTypeName(int32 reproType)
{
  HQASSERT(reproType < REPRO_N_TYPES, "Invalid reproType");
  HQASSERT(reproType >= 0, "Invalid intent");

  return &system_names[reproTypeNames[reproType]] ;
}

/* ------------------------------------------------------------------------ */

Bool cc_findColorModel(GS_COLORinfo       *colorInfo,
                       COLORSPACE_ID      iColorSpaceId,
                       OBJECT             *colorSpace,
                       Bool               fCompositing,
                       REPRO_COLOR_MODEL  *colorModel)
{
  ICC_PROFILE_INFO        *dummyICCInfo;
  int32                   dummyDimension;
  COLORSPACE_ID           dummyPCSSpaceId;
  COLORSPACE_ID           potentialDeviceSpaceId;
  GSC_SIMPLE_TRANSFORM    *simpleTransform;

  HQASSERT(colorSpace, "colorSpace NULL");
  HQASSERT(colorModel, "colorModel NULL");

  *colorModel = REPRO_COLOR_MODEL_INVALID;

  switch (iColorSpaceId) {
  case SPACE_DeviceCMYK:
    *colorModel = REPRO_COLOR_MODEL_CMYK;
    break;
  case SPACE_DeviceCMY:
  case SPACE_DeviceRGB:
  case SPACE_CalRGB:
    /* N.B. Currently only PCL uses DeviceCMY as an input space, and it
     * is immediately converted to DeviceRGB. */
    *colorModel = REPRO_COLOR_MODEL_RGB;
    break;
  case SPACE_DeviceGray:
  case SPACE_CalGray:
    *colorModel = REPRO_COLOR_MODEL_GRAY;
    break;
  case SPACE_Separation:
  case SPACE_DeviceN:
    *colorModel = REPRO_COLOR_MODEL_NAMED_COLOR;

    /* Color models of device + spots are treated specially by black preservation
     * when compositing.
     */
    simpleTransform = cc_csaGetSimpleTransform(colorSpace);
    if (simpleTransform != NULL && fCompositing) {
      switch (cc_spacecache_PCMofInputRS(simpleTransform)) {
      case DEVICESPACE_CMYK:
        *colorModel = REPRO_COLOR_MODEL_CMYK_WITH_SPOTS;
        break;
      case DEVICESPACE_RGB:
        *colorModel = REPRO_COLOR_MODEL_RGB_WITH_SPOTS;
        break;
      case DEVICESPACE_Gray:
        *colorModel = REPRO_COLOR_MODEL_GRAY_WITH_SPOTS;
        break;
      default:
        HQFAIL("Expected a device space PCM");
        break;
      }
    }
    break;
  }

  /* If we've found the equivalent space, there's no need to continue. Indeed,
   * the colorSpace may well be invalid for simple device spaces.
   */
  if (*colorModel != REPRO_COLOR_MODEL_INVALID)
    return TRUE;

  /* It can happen during startup that we have neither a colorSpaceId nor a
   * colorSpace. At other times, if we don't have a colorSpace we should have
   * had a valid colorSpaceId that we dealt with above.
   */
  if (oType(*colorSpace) == ONULL) {
    HQASSERT(iColorSpaceId == SPACE_notset || iColorSpaceId == SPACE_TrapDeviceN,
             "Unexpected colorSpaceId");
    *colorModel = REPRO_COLOR_MODEL_GRAY;
    return TRUE;
  }


  switch (iColorSpaceId) {
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
    /** \todo @@JJ 26890. We need to determine if these are calibrated device spaces */
    *colorModel = REPRO_COLOR_MODEL_CIE;
    break;
  case SPACE_Lab:
    *colorModel = REPRO_COLOR_MODEL_CIE;
    break;

  case SPACE_ICCBased:
    if (!cc_get_iccbased_profile_info(colorInfo,
                                      colorSpace,
                                      &dummyICCInfo,
                                      &dummyDimension,
                                      &potentialDeviceSpaceId,
                                      &dummyPCSSpaceId))
      return FALSE;

    switch (potentialDeviceSpaceId) {
    case SPACE_DeviceCMYK:
      *colorModel = REPRO_COLOR_MODEL_CMYK;
      break;
    case SPACE_DeviceRGB:
      *colorModel = REPRO_COLOR_MODEL_RGB;
      break;
    case SPACE_DeviceGray:
      *colorModel = REPRO_COLOR_MODEL_GRAY;
      break;
    case SPACE_DeviceN:
      *colorModel = REPRO_COLOR_MODEL_NAMED_COLOR;
      break;
    default:
      *colorModel = REPRO_COLOR_MODEL_CIE;
      break;
    }
    break;

  case SPACE_Indexed:
    colorSpace = cc_getbasecolorspaceobject(colorSpace);
    if (!gsc_getcolorspacesizeandtype(colorInfo, colorSpace,
                                      &iColorSpaceId, &dummyDimension))
      return FALSE;
    if (!cc_findColorModel(colorInfo,
                           iColorSpaceId,
                           colorSpace,
                           fCompositing,
                           colorModel))
      return FALSE;
    break;

  case SPACE_PatternMask:
  case SPACE_Pattern:
    if (!cc_getpatternbasespace(colorInfo, colorSpace,
                                &iColorSpaceId, &dummyDimension))
      return FALSE;
    if (iColorSpaceId != SPACE_notset) {
      /* Uncolored pattern spaces use their base space for the color model */
      colorSpace = &oArray(*colorSpace)[1];
      if (!cc_findColorModel(colorInfo,
                             iColorSpaceId,
                             colorSpace,
                             fCompositing,
                             colorModel))
        return FALSE;
    }
    else {
      /* Top level colored pattern objects aren't color managed */
      *colorModel = REPRO_COLOR_MODEL_PATTERN;
    }
    break;

  case SPACE_CMM:
    HQFAIL("CMM colorspace NYI");
    *colorModel = REPRO_COLOR_MODEL_INVALID;
    break;
  default:
    HQFAIL( "unrecognized color space" ) ;
    *colorModel = REPRO_COLOR_MODEL_INVALID;
    break;
  }

  HQASSERT(*colorModel != REPRO_COLOR_MODEL_INVALID, "Invalid colorModel");

  return TRUE;
}

Bool gsc_setColorModel(GS_COLORinfo *colorInfo, int32 colorType,
                       REPRO_COLOR_MODEL colorModel)
{
  HQASSERT(colorInfo, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_setColorModel") ;
  HQASSERT(colorModel < REPRO_N_COLOR_MODELS,
           "Invalid colorModel");

  if (colorInfo->hcmsInfo->colorModel[colorType] != colorModel) {
    if (cc_updatehcmsinfo(colorInfo, &colorInfo->hcmsInfo) &&
        cc_invalidateChain(colorInfo, colorType, FALSE)) {
      colorInfo->hcmsInfo->colorModel[colorType] = colorModel;
    }
    else
      return FALSE;
  }

  return TRUE;
}

Bool cc_setColorModel(GS_COLORinfo *colorInfo, int32 colorType,
                      REPRO_COLOR_MODEL chainColorModel)
{
  REPRO_COLOR_MODEL colorModel;

  HQASSERT(colorInfo, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_setColorModel") ;
  HQASSERT(chainColorModel < REPRO_N_COLOR_MODELS_WITH_SPOTS,
           "Invalid colorModel");

  switch (chainColorModel) {
  case REPRO_COLOR_MODEL_CMYK_WITH_SPOTS:
  case REPRO_COLOR_MODEL_RGB_WITH_SPOTS:
  case REPRO_COLOR_MODEL_GRAY_WITH_SPOTS:
    colorModel = REPRO_COLOR_MODEL_NAMED_COLOR;
    break;
  case REPRO_COLOR_MODEL_PATTERN:
    colorModel = REPRO_COLOR_MODEL_GRAY;
    break;
  default:
    colorModel = chainColorModel;
    break;
  }

  HQASSERT(colorModel < REPRO_N_COLOR_MODELS,
           "Invalid colorModel");

  if (!gsc_setColorModel(colorInfo, colorType, colorModel))
    return FALSE;

  return TRUE;
}

REPRO_COLOR_MODEL gsc_getColorModel(GS_COLORinfo *colorInfo, int32 colorType)
{
  HQASSERT(colorInfo, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_getColorModel");

  HQASSERT(colorInfo->hcmsInfo->colorModel[colorType] < REPRO_N_COLOR_MODELS,
           "Invalid colorModel");

  return colorInfo->hcmsInfo->colorModel[colorType];
}

Bool gsc_colorModel(GS_COLORinfo      *colorInfo,
                    OBJECT            *colorSpace,
                    REPRO_COLOR_MODEL *colorModel)
{
  COLORSPACE_ID colorSpaceId;

  HQASSERT(colorSpace != NULL, "colorSpace NULL");
  HQASSERT(colorModel != NULL, "colorModel NULL");

  if (! gsc_getcolorspacetype(colorSpace, &colorSpaceId))
    return FALSE;

  /* This function is only used when interpreting, not compositing */
  if (!cc_findColorModel(colorInfo, colorSpaceId, colorSpace, FALSE, colorModel))
    return FALSE;

  HQASSERT(*colorModel < REPRO_N_COLOR_MODELS, "Invalid colorModel");

  return TRUE;
}

/* ------------------------------------------------------------------------ */

Bool cc_isDevicelink(TRANSFORM_LINK_INFO *linkInfo)
{
  TRANSFORM_LINK_INFO localInfo = *linkInfo;
  Bool terminateLoop = FALSE;

  /* In concept, we can see if the linkInfo leads to a connection space. There
   * is the complication that our CIETablexxx spaces could have a follow-on
   * to a connection space, or it could be a devicelink. Or it could follow-on
   * to another CIETablexxx space or an ICC profile (albeit these are unlikely).
   */
  do {
    switch (localInfo.inputColorSpaceId) {
    case SPACE_CIEBasedDEFG:
      localInfo = *cc_nextCIEBasedDEFGInfo(localInfo.u.ciebaseddefg);
      break;
    case SPACE_CIETableABCD:
      localInfo = *cc_nextCIETableABCDInfo(localInfo.u.cietableabcd);
      break;
    case SPACE_CIEBasedDEF:
      localInfo = *cc_nextCIEBasedDEFInfo(localInfo.u.ciebaseddef);
      break;
    case SPACE_CIETableABC:
      localInfo = *cc_nextCIETableABCInfo(localInfo.u.cietableabc);
      break;
    case SPACE_CIETableA:
      localInfo = *cc_nextCIETableAInfo(localInfo.u.cietablea);
      break;
    default:
      terminateLoop = TRUE;
      break;
    }
  } while (!terminateLoop);

  switch (localInfo.outputColorSpaceId) {
  case SPACE_CIEXYZ:
  case SPACE_CIELab:
  case SPACE_ICCXYZ:
  case SPACE_ICCLab:
  case SPACE_HqnPCS:
    return FALSE;
  default:
    return TRUE;
  }
}

static int32 nOutputChannels(TRANSFORM_LINK_INFO *linkInfo)
{
  int32 outputDimension;

  switch (linkInfo->inputColorSpaceId) {
  case SPACE_ICCBased:
    outputDimension = cc_iccbased_nOutputChannels(linkInfo->u.icc);
    break;
  case SPACE_CMM:
    outputDimension = cc_customcmm_nOutputChannels(linkInfo->u.customcmm);
    break;
  case SPACE_CIETableABCD:
    outputDimension = cc_cietableabcd_nOutputChannels(linkInfo->u.cietableabcd);
    break;
  case SPACE_CIETableABC:
    outputDimension = cc_cietableabc_nOutputChannels(linkInfo->u.cietableabc);
    break;
  case SPACE_CIETableA:
    outputDimension = cc_cietablea_nOutputChannels(linkInfo->u.cietablea);
    break;

  /* These can result from setting InputColorSpace to a device space */
  case SPACE_DeviceCMYK:
    outputDimension = 4;
    break;
  case SPACE_DeviceRGB:
    outputDimension = 3;
    break;
  case SPACE_DeviceGray:
    outputDimension = 1;
    break;
    break;

  default:
    HQFAIL("Unexpected type of InputColorSpace");
    outputDimension = 0;
  }

  return outputDimension;
}

/* ---------------------------------------------------------------------- */

/* Does this gstate use object based color management of profiles? It's useful
 * to know to enable an optimisation in gsc_ICCCacheTransfer().
 * Walk over the significant object based attributes and determine if any
 * have more than one value.
 */
static void updateUsingObjectBasedColor(GS_HCMSinfo *hcmsInfo)
{
  int i;
  int j;
  REPRODUCTIONinfo *reproInfo;

  hcmsInfo->usingObjectBasedColor = FALSE;

  for (i = 0; i < REPRO_N_TYPES && !hcmsInfo->usingObjectBasedColor; i++) {
    if (hcmsInfo->interceptCMYK[i].info.u.shared != hcmsInfo->interceptCMYK[0].info.u.shared ||
        hcmsInfo->interceptRGB[i].info.u.shared  != hcmsInfo->interceptRGB[0].info.u.shared  ||
        hcmsInfo->interceptGray[i].info.u.shared != hcmsInfo->interceptGray[0].info.u.shared ||
        hcmsInfo->blendCMYK[i].info.u.shared != hcmsInfo->blendCMYK[0].info.u.shared ||
        hcmsInfo->blendRGB[i].info.u.shared  != hcmsInfo->blendRGB[0].info.u.shared  ||
        hcmsInfo->blendGray[i].info.u.shared != hcmsInfo->blendGray[0].info.u.shared ||
        hcmsInfo->sourceCMYK[i].info.u.shared != hcmsInfo->sourceCMYK[0].info.u.shared ||
        hcmsInfo->sourceRGB[i].info.u.shared  != hcmsInfo->sourceRGB[0].info.u.shared  ||
        hcmsInfo->sourceGray[i].info.u.shared != hcmsInfo->sourceGray[0].info.u.shared ||
        hcmsInfo->overrideCMYK[i] != hcmsInfo->overrideCMYK[0] ||
        hcmsInfo->overrideRGB[i]  != hcmsInfo->overrideRGB[0]  ||
        hcmsInfo->overrideGray[i] != hcmsInfo->overrideGray[0]) {
      hcmsInfo->usingObjectBasedColor = TRUE;
      break;
    }

    /* Black tint preservation needs special attention because we require a
     * non-standard ICC table to reverse map black-luminance.
     */
    for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
      if (hcmsInfo->interceptBlackTint[i][j] != hcmsInfo->interceptBlackTint[0][0]) {
        hcmsInfo->usingObjectBasedColor = TRUE;
        break;
      }
    }
  }

  for (reproInfo = &hcmsInfo->reproductionInfo;
       reproInfo != NULL && !hcmsInfo->usingObjectBasedColor;
       reproInfo = reproInfo->nextDevice) {
    for (i = 0; i < REPRO_N_TYPES && !hcmsInfo->usingObjectBasedColor; i++) {
      for (j = 0; j < REPRO_N_COLOR_MODELS; j++) {
        if (reproInfo->objectProfile[i][j].p.u.shared != reproInfo->objectProfile[0][0].p.u.shared ||
            reproInfo->objectProfile[i][j].extension  != reproInfo->objectProfile[0][0].extension  ||
            reproInfo->objectIntentNames[i][j]  != reproInfo->objectIntentNames[0][0]  ||
            reproInfo->inputColorSpaceInfo[i][j].u.shared != reproInfo->inputColorSpaceInfo[0][0].u.shared) {
          hcmsInfo->usingObjectBasedColor = TRUE;
          break;
        }
      }
    }
  }
}

Bool cc_usingObjectBasedColor(GS_COLORinfo *colorInfo)
{
  HQASSERT(colorInfo, "colorInfo NULL");

  return colorInfo->hcmsInfo->usingObjectBasedColor;
}

#define MAX_CHECK_DICTS (2)

typedef struct {
  int32 numDicts;
  NAMETYPEMATCH *dicts[MAX_CHECK_DICTS];
} CHECK_DICTS;

static Bool dictwalk_excesskeys(OBJECT *key,
                                OBJECT *value,
                                void   *args )
{
  CHECK_DICTS *checkDicts = args;
  NAMETYPEMATCH *allowedNames;
  int32 i;

  for (i = 0; i < checkDicts->numDicts; i++) {
    allowedNames = checkDicts->dicts[i];
    while (theISomeLeft(allowedNames)) {
      if (oType(*key) == ONAME &&
          oNameNumber(*key) == (allowedNames->name & ~OOPTIONAL))
        return TRUE;

      allowedNames++;
    }
  }

  return errorinfo_error_handler(CONFIGURATIONERROR, key, value);
}

static Bool checkExcessKeys(OBJECT *inputDict, NAMETYPEMATCH *checkDict)
{
  CHECK_DICTS checkDicts;

  checkDicts.numDicts = 1;
  checkDicts.dicts[0] = checkDict;

  /* Ensure there are no stray keys in dict */
  if (!walk_dictionary(inputDict, dictwalk_excesskeys, (void *) &checkDicts))
    return FALSE;

  return TRUE;
}

static Bool checkExcessKeys2(OBJECT *inputDict,
                             NAMETYPEMATCH *checkDict1, NAMETYPEMATCH *checkDict2)
{
  CHECK_DICTS checkDicts;

  checkDicts.numDicts = 2;
  checkDicts.dicts[0] = checkDict1;
  checkDicts.dicts[1] = checkDict2;

  /* Ensure there are no stray keys in dict */
  if (!walk_dictionary(inputDict, dictwalk_excesskeys, (void *) &checkDicts))
    return FALSE;

  return TRUE;
}

/** File runtime initialisation */
void init_C_globals_gschcms(void)
{
  /*****************************************************************************

    Globals are only allowed for frontend color transforms. If an item needs to
    be used for both frontend and backend transforms then it should be put into
    COLOR_STATE.

  *****************************************************************************/

  /* Initialise constant static data here
   */
  renderingIntentNames[SW_CMM_INTENT_PERCEPTUAL]            = NAME_Perceptual;
  renderingIntentNames[SW_CMM_INTENT_RELATIVE_COLORIMETRIC] = NAME_RelativeColorimetric;
  renderingIntentNames[SW_CMM_INTENT_SATURATION]            = NAME_Saturation;
  renderingIntentNames[SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC] = NAME_AbsoluteColorimetric;
  renderingIntentNames[SW_CMM_INTENT_ABSOLUTE_PERCEPTUAL]   = NAME_undefined;
  renderingIntentNames[SW_CMM_INTENT_ABSOLUTE_SATURATION]   = NAME_undefined;
  renderingIntentNames[SW_CMM_INTENT_NONE]                  = NAME_None;
  HQASSERT(GSC_N_RENDERING_INTENTS == 7, "Number of reproTypes has changed");

  reproTypeNames[REPRO_TYPE_PICTURE]     = NAME_Picture;
  reproTypeNames[REPRO_TYPE_TEXT]        = NAME_Text;
  reproTypeNames[REPRO_TYPE_VIGNETTE]    = NAME_Vignette;
  reproTypeNames[REPRO_TYPE_OTHER]       = NAME_Other;
  HQASSERT(REPRO_N_TYPES == 4, "Number of reproTypes has changed");

  reproColorClassNames[REPRO_COLOR_MODEL_CMYK]         = NAME_CMYK;
  reproColorClassNames[REPRO_COLOR_MODEL_RGB]          = NAME_RGB;
  reproColorClassNames[REPRO_COLOR_MODEL_GRAY]         = NAME_Gray;
  reproColorClassNames[REPRO_COLOR_MODEL_NAMED_COLOR]  = NAME_NamedColor;
  reproColorClassNames[REPRO_COLOR_MODEL_CIE]          = NAME_CIE;
  HQASSERT(REPRO_N_COLOR_MODELS == 5, "Number of colorModels has changed");

  defaultReproTypes[GSC_FILL]                 = REPRO_TYPE_OTHER;
  defaultReproTypes[GSC_STROKE]               = REPRO_TYPE_OTHER;
  defaultReproTypes[GSC_IMAGE]                = REPRO_TYPE_OTHER;
  defaultReproTypes[GSC_SHFILL]               = REPRO_TYPE_VIGNETTE;
  defaultReproTypes[GSC_SHFILL_INDEXED_BASE]  = REPRO_TYPE_VIGNETTE;
  defaultReproTypes[GSC_VIGNETTE]             = REPRO_TYPE_VIGNETTE;
  defaultReproTypes[GSC_BACKDROP]             = REPRO_TYPE_OTHER;
  HQASSERT(GSC_N_COLOR_TYPES == 7, "Number of colorTypes has changed");

  /* The hierarchy of object types when objects are composited */
  reproTypePriority[REPRO_TYPE_TEXT] = 4;
  reproTypePriority[REPRO_TYPE_OTHER] = 3;
  reproTypePriority[REPRO_TYPE_VIGNETTE] = 2;
  reproTypePriority[REPRO_TYPE_PICTURE] = 1;
  HQASSERT(REPRO_N_TYPES == 4, "Number of reproTypes has changed");
}


/* eof */

/* Log stripped */
