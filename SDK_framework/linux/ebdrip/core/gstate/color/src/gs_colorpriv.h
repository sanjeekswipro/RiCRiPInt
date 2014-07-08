/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gs_colorpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain private data
 */

#ifndef __GS_COLORPRIV_H__
#define __GS_COLORPRIV_H__


#include "ciepsfns.h"          /* CIECallBack */
#include "graphict.h"          /* GSTATE */
#include "gu_chan.h"           /* DEVICESPACEID */
#include "objects.h"           /* OBJECT */
#include "tranState.h"         /* TsStrokeSelector */

#include "refcnt.h"            /* cc_counter_t */
#include "gscdevcipriv.h"      /* TRANSFORMEDSPOT */
#include "gschcms.h"           /* REPRO_COLOR_MODEL */
#include "gschead.h"           /* GSC_BLACK_KIND */
#include "gscparamspriv.h"     /* COLOR_SYSTEM_PARAMS */
#include "gscsmpxformpriv.h"   /* SPACECACHE */
#include "gs_cachepriv.h"      /* gCoc_t */
#include "gs_chaincachepriv.h" /* GS_CHAIN_CACHE_STATE */
#include "gscfastrgbconvpriv.h" /* GS_FASTRGB2GRAY_STATE */
#include "gs_table.h"          /* GSC_TABLE */
#include "gsctintpriv.h"       /* GS_TINT_STATE */
#include "gs_color.h"
#include "pscalc.h"

/** \todo JJ A temporary hack to convert all sac allocations to normal allocations.
 * The sac allocations aren't pipeline safe, so they either need to be made safe
 * or replaced.
 */
#undef mm_sac_alloc
#undef mm_sac_free
#define mm_sac_alloc    mm_alloc
#define mm_sac_free     mm_free


/* This is the level of recursion that we allow for NextDevice in setreproduction.
 * It is a large number purely to avoid having to allocate memory when automatic
 * memory is more convenient. Noone will ever want to go above a recursion of 4.
 */
#define MAX_NEXTDEVICE_DICTS    (20)

/* Colorant index arrays are dynamically allocated if they're bigger than this. */
#define STACK_EXTCIA_SIZE   (8)

/* ---------------------------------------------------------------- */

struct GSTATE_MIRROR {
  Bool                      opaqueNonStroke;
  Bool                      opaqueStroke;
  int32                     halftonePhaseX;
  int32                     halftonePhaseY;
  USERVALUE                 screenRotate;
};

struct GS_COLORinfo {
  GUCR_RASTERSTYLE          *deviceRS;
  GUCR_RASTERSTYLE          *targetRS; /* Either the device RS or a backdrop
                                        * RS.  This RS is used as the target
                                        * for a color chain invocation.
                                        * Resulting colorants pertain to the
                                        * targetRS. */

  GS_CHAINinfo              *chainInfo[GSC_N_COLOR_TYPES];
  int32                     constructionDepth[GSC_N_COLOR_TYPES];
  GS_CHAIN_CACHE            *chainCache[GSC_N_COLOR_TYPES];

  /* The gstate attributes */
  GS_CRDinfo                *crdInfo;
  GS_RGBtoCMYKinfo          *rgbtocmykInfo;
  GS_TRANSFERinfo           *transferInfo;
  GS_CALIBRATIONinfo        *calibrationInfo;
  GS_HALFTONEinfo           *halftoneInfo;
  GS_HCMSinfo               *hcmsInfo;
  GS_DEVICECODEinfo         *devicecodeInfo;

  Bool                      fInvalidateColorChains;

  /* A struct of params that are used as working copies from other sources. In
   * all cases they need to be referenced in the back-end so they cannot live
   * in UserParams etc. In some cases the values are temporarily changed within
   * the current gstate.
   */
  struct {
    /* A pagedevice key that is used internally as a gstate attribute */
    int                     convertAllSeparation;

    /* Userparams that require storing in the gstate for use in the back-end */
    int                     enableColorCache;
    OBJECT                  excludedSeparations;
    Bool                    photoshopInput;
    Bool                    adobeProcessSeparations;
    Bool                    useFastRGBToCMYK;
    int                     rgbToCMYKMethod;
  } params;

  /* Copies of environment data used at chain creation */
  GSTATE_MIRROR             gstate;

  /* COLOR_STATE contains references to the top level color structures including
     the spacecache, the color cache, the color chain cache and the colorInfo
     list. */
  COLOR_STATE               *colorState;

  GS_COLORinfo              *next;
} ;

struct GS_CONSTRUCT_CONTEXT {
  TRANSFORM_LINK_INFO *transformList[MAX_NEXTDEVICE_DICTS];
  TRANSFORM_LINK_INFO *currentInfo;
  Bool                forceIccToDeviceN;
  CLINK               **pThisLink;
  OBJECT              *PSColorSpace;
  COLORSPACE_ID       chainColorSpace;
  Bool                finalDeviceSpace;
  int32               colorspacedimension;
  Bool                fColorManage;
  Bool                fPaintingSoftMask;
  Bool                fSoftMaskLuminosityChain;
  Bool                fNonInterceptingChain;
  Bool                allowIntercept;
  int32               nTransferLinks;
  Bool                applyJobPolarity;
  DEVICESPACEID       aimDeviceSpace;
  int32               nAimDeviceColorants;
  Bool                jobColorSpaceIsGray;
  COLORANTINDEX       sColorantIndexArray[STACK_EXTCIA_SIZE];
  COLORANTINDEX       *pColorantIndexArray;
  int32               colorantIndexArraySize;
  Bool                allowNamedColorIntercept;
  COLORSPACE_ID       inIntercept;
  TRANSFORMEDSPOT     transformedSpotType;
  Bool                allowBlackPreservation;
  Bool                doneBlackEvaluate;
  Bool                doneBlackRemove;
  Bool                f100pcBlackRelevant;
  Bool                fBlackTintRelevant;
  Bool                fBlackTintLuminance;
  CLINK               *blackevaluateLink;
  DL_STATE            *page;
  COLOR_PAGE_PARAMS   *colorPageParams;
  Bool                matchingICCprofiles;
};

struct GS_COLORinfoList {
  GS_COLORinfo *colorInfoHead;
  int32 nColorInfos;
};

struct COLOR_STATE {
  /* Color cache for invoke single, gs_cache.c */
  COC_STATE *cocState;

  /* Color chain cache, gs_chaincache.c */
  GS_CHAIN_CACHE_STATE *chainCacheState;

  /* Tom's Tables color caches for invoke block, gs_table.c */
  GSC_TOMSTABLES *tomsTables;

 /* Cache of simple colorspace transforms, gscsmpxform.c */
  SPACECACHE *spacecache;

  /* A list of all colorInfo structures for this color state. Used in restore to
     make sure all color chains and the ChainCache are correctly restored. */
  GS_COLORinfoList colorInfoList;

/* A list of all ICC profile structures in this color state, gscicc.c */
  ICC_PROFILE_INFO_CACHE *ICC_cacheHead;

  /* Fast RGB to gray conversion, gscfastrgb2gray.c */
  GS_FASTRGB2GRAY_STATE *fastrgb2grayState;

  /* Fast RGB to cmyk conversion, gscfastrgb2gray.c */
  GS_FASTRGB2CMYK_STATE *fastrgb2cmykState;

  /* Linked list of dciluts, gscdevci.c */
  DCILUT *dciluts;

  /* gschcms.c */
  uint32 hcmsInterceptId;

  /* Tint transform state, gsctint.c */
  GS_TINT_STATE *tintState;
};

/* Values for the linkType */
enum { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  CL_TYPEnotset,
  CL_TYPEcietablea, CL_TYPEcietableabc, CL_TYPEcietableabcd,
  CL_TYPEciebaseda, CL_TYPEciebasedabc, CL_TYPEciebaseddef, CL_TYPEciebaseddefg,
  CL_TYPElab, CL_TYPEcalgray, CL_TYPEcalrgb, CL_TYPEiccbased,
  CL_TYPEindexed,
  CL_TYPEtinttransform, CL_TYPEallseptinttransform,
  CL_TYPEcustomconversion,
  CL_TYPEcrd,
  CL_TYPEchannelmap,
  CL_TYPErgbtogray,   CL_TYPErgbtocmyk,   CL_TYPErgbton, CL_TYPErgbtorgbk, CL_TYPErgborcmy_invert, CL_TYPErgbtolab,
  CL_TYPEcmyktogray,  CL_TYPEcmyktorgb,   CL_TYPEcmykton, CL_TYPEcmyktorgbk, CL_TYPEcmyktocmy, CL_TYPEcmyktolab,
  CL_TYPEgraytorgb,   CL_TYPEgraytocmyk,  CL_TYPEgrayton, CL_TYPEgraytok, CL_TYPEgraytorgbk, CL_TYPEgraytocmy, CL_TYPEgraytolab,
  CL_TYPEpresep, CL_TYPEdevicecode,   CL_TYPEnonintercept,
  CL_TYPEtransfer,
  CL_TYPEcalibration,
  CL_TYPEluminosity,
  CL_TYPEinterceptdevicen,
  CL_TYPEcmmxform,
  CL_TYPEalternatecmm,
  CL_TYPEcustomcmm,
  CL_TYPEiccbasedoutput,
  CL_TYPEneutralmapping,
  CL_TYPEblackevaluate,
  CL_TYPEblackremove,
  CL_TYPEblackreplace,
  CL_TYPEdummyfinallink
};


/*************************************************************************
 *
 * The following are the structures which can exist only as part of Color
 * Links.
 *
 *************************************************************************/

typedef struct CLINKcustomconversion {
  int32         n_oColorants;
  OBJECT        customprocedure;
  struct PSCALC_OBJ *pscalc_func;
} CLINKcustomconversion;

struct CLINKblock {
  int32             nColors ;
  USERVALUE         *iColorValues ;
  USERVALUE         *tmpColorValues ;
  COLORVALUE        *deviceCodes ;
  uint8             overprintProcess[GSC_BLOCK_MAXCOLORS] ;
  USERVALUE         blackValueToPreserve[GSC_BLOCK_MAXCOLORS] ;
  GSC_BLACK_TYPE    blackType[GSC_BLOCK_MAXCOLORS];
  GSC_BLACK_TYPE    origBlackType;
  GS_CHAINinfo      *colorChain;
  GS_BLOCKoverprint *blockOverprint ;
};

typedef struct CLINKfunctions {
  void          (*destroy)     ( CLINK *clink );
  int32         (*invokeSingle)( CLINK *clink, USERVALUE *icolor );
  int32         (*invokeBlock) ( CLINK *clink, CLINKblock *cblock );
  mps_res_t     (*scan)        ( mps_ss_t ss, CLINK *clink );
} CLINKfunctions;

union CLINKprivate {
  void                     *shared;
  CLINKciebaseda           *ciebaseda;
  CLINKciebasedabc         *ciebasedabc;
  CLINKciebaseddef         *ciebaseddef;
  CLINKciebaseddefg        *ciebaseddefg;
  CLINKcietablea           *cietablea;
  CLINKcietableabc         *cietableabc;
  CLINKcietableabcd        *cietableabcd;
  CLINKlab                 *lab;
  CLINKcalrgbg             *calrgbg;
  CLINKiccbased            *iccbased;
  CLINKcrd                 *crd;
  CLINKindexed             *indexed;
  CLINKtinttransform       *tinttransform;
  CLINKintercepttransform  *intercepttransform;
  CLINKallseptinttransform *allseptinttransform;
  CLINKcustomconversion    *customconversion;
  CLINKCMYKtoGrayinfo      *cmyktogray;
  CLINKRGBtoCMYKinfo       *rgbtocmyk;
  CLINKRGBtoGrayinfo       *rgbtogray;
  CLINKDEVICECODEinfo      *devicecode;
  CLINKNONINTERCEPTinfo    *nonintercept;
  CLINKTRANSFERinfo        *transfer;
  CLINKCALIBRATIONinfo     *calibration;
  CLINKPRESEPARATIONinfo   *preseparation;
  CLINKLUMINOSITYinfo      *luminosity;
  CLINKinterceptdevicen    *interceptdevicen;
  CLINKcmmxform            *cmmxform;
  CLINKalternatecmm        *alternatecmm;
  CLINKcustomcmm           *customcmm;
  CLINKneutralmapping      *neutralmapping;
  CLINKblackevaluate       *blackevaluate;
  CLINKblackremove         *blackremove;
  CLINKblackreplace        *blackreplace;
};

struct CLINK { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  size_t            structSize;
  CLINK             *pnext;
  int32             n_iColorants;
  USERVALUE         *iColorValues;  /* [n_iColorants] */
  COLORSPACE_ID     iColorSpace;
  COLORSPACE_ID     oColorSpace;
  uint8             overprintProcess;
  GSC_BLACK_TYPE    blackType;
  USERVALUE         blackValueToPreserve;
  int32             linkType;
  CLINKfunctions    *functions;
  COLORANTINDEX     *iColorants;    /* [n_iColorants] */

  int32             idcount;
  CLID              *idslot;
  CLINKprivate      p;
};

/* Magic values for idcount */
#define COLCACHE_DISABLE (-1)
#define COLCACHE_NYI (-2)

/* ---------------------------------------------------------------- */

/* Defines for XYZ */
#define CC_CIEXYZ_X              0
#define CC_CIEXYZ_Y              1
#define CC_CIEXYZ_Z              2
#define NUMBER_XYZ_COMPONENTS    3

typedef SYSTEMVALUE     XYZVALUE[NUMBER_XYZ_COMPONENTS];

/* ---------------------------------------------------------------- */

/* Multiplies a 3 dimensional color with a matrix for cie colorspaces and crd's
 */
#define MATRIX_MULTIPLY(_color, _matrix) MACRO_START  \
  register SYSTEMVALUE *m = (_matrix) ;               \
  register SYSTEMVALUE c0 = _color[0], c1 = _color[1], c2 = _color[2] ; \
  _color[0] = c0 * m[0] + c1 * m[3] + c2 * m[6] ;     \
  _color[1] = c0 * m[1] + c1 * m[4] + c2 * m[7] ;     \
  _color[2] = c0 * m[2] + c1 * m[5] + c2 * m[8] ;     \
MACRO_END

/* ---------------------------------------------------------------- */

#define IS_BLACKONLY_CMYK(cv) (cv[0] == 0.0f && cv[1] == 0.0f && cv[2] == 0.0f)

#define IS_GRAY_RGB(_cv) ((_cv)[0] == (_cv)[1] && (_cv)[1] == (_cv)[2])

/* ---------------------------------------------------------------- */

Bool gsc_swstart( GS_COLORinfo *colorInfo );

/* Common color link processing functions */

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
void cc_addCountsForOneChain(CLINK *pLink,
                             int *chainCount, int *linkCount);
void cc_addCountsForOneContext(GS_CHAINinfo *chain,
                               double *chainCount, double *linkCount);
#endif    /* ASSERT_BUILD */

void cc_metrics_increment_constructs(void);
void cc_metrics_increment_destructs(void);

#else     /* !METRICS_BUILD */

#define cc_metrics_increment_constructs() EMPTY_STATEMENT()
#define cc_metrics_increment_destructs() EMPTY_STATEMENT()

#endif    /* METRICS_BUILD */

CLINK *cc_common_create(int32              nColorants,
                        COLORANTINDEX      *pColorants,
                        COLORSPACE_ID      icolorSpace,
                        COLORSPACE_ID      ocolorSpace,
                        int32              linkType,
                        size_t             structSize,
                        CLINKfunctions     *functions,
                        int32              nIds);

void cc_common_destroy(CLINK *pLink);

size_t cc_commonStructSize(CLINK *pLink);

#if defined( ASSERT_BUILD )
void cc_commonAssertions(CLINK          *pLink,
                         int32          linkType,
                         size_t         structSize,
                         CLINKfunctions *functions);
#else
#define cc_commonAssertions(pLink, linkType, structSize, functions) \
  EMPTY_STATEMENT()
#endif

Bool cc_colorspaceNamesToIndex(GUCR_RASTERSTYLE   *hRasterStyle,
                               OBJECT             *PSColorSpace,
                               Bool               allowAutoSeparation,
                               Bool               f_do_nci,
                               COLORANTINDEX      *pcolorants,
                               int32              n_colorants,
                               OBJECT             *excludedSeparations,
                               Bool               *allColorantsMatch);
Bool cc_namesToIndex(GUCR_RASTERSTYLE   *hRasterStyle,
                     OBJECT             *namesObject,
                     Bool               allowAutoSeparation,
                     Bool               f_do_nci,
                     COLORANTINDEX      *pcolorants,
                     int32              n_colorants,
                     OBJECT             *excludedSeparations,
                     Bool               *allColorantsMatch);

/* @@JJ Currently required for photoink in gu_chan.c. Would like to eliminate that */
mps_res_t cc_scan( mps_ss_t ss, CLINK *pLink );

Bool cc_create_xuids( OBJECT *poUniqueID ,
                      OBJECT *poXUID ,
                      int32 *pnXUIDs ,
                      int32 **ppXUIDs ) ;
void cc_destroy_xuids( int32 *pnXUIDs , int32 **ppXUIDs ) ;

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
                       Bool             forceNamedIntereptionForTransforms);

Bool cc_incompleteChainIsSimple(GS_CHAINinfo *colorChain);

Bool cc_maxChainColorants(GS_CHAINinfo *colorChain, int32 *pnColorants);

void cc_destroyLinks( CLINK *clink );

Bool cc_invalidateChain(GS_COLORinfo *colorInfo,
                        int32 colorType,
                        Bool destroyChainList);
Bool cc_invalidateColorChains(GS_COLORinfo *colorInfo, Bool destroyChainList);

Bool cc_getOpaque(GS_COLORinfo *colorInfo, TsStrokeSelector type);

void cc_restoreNamedColorantCache(int32 saveLevel, GS_COLORinfoList *colorInfoList);

Bool invokeDevicecodeBlock( CLINK *pFirstLink ,
                            USERVALUE *piColorValues ,
                            COLORVALUE *poColorValues ,
                            int32 nColors );

/* ---------------------------------------------------------------- */

/* Postscript callout accessors and C optimisers */

#define CALL_C(_func, _colorValue, _extra) MACRO_START      \
  _colorValue = (*(_func))(_colorValue, (void *)(_extra));  \
MACRO_END

Bool cc_extractP( OBJECT into[], int32 n, OBJECT *theo );

CIECallBack cc_findCIEProcedure(OBJECT *theo);

/* Log stripped */

#endif /* __GS_COLORPRIV_H__ */
