/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscdevci.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphic-state device color interface
 */

#include "core.h"

#include "color.h"              /* ht_setupTransforms */
#include "display.h"            /* RENDER_KNOCKOUT */
#include "dlstate.h"            /* DLSTATE */
#include "gcscan.h"             /* ps_scan_field */
#include "gu_chan.h"            /* guc_backdropRasterStyle */
#include "gu_htm.h"             /* htm_GetFirstHalftoneRef */
#include "halftone.h"           /* ht_setUsed */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "hqmemset.h"           /* HqMemSet16 */
#include "mmcompat.h"           /* mm_alloc_with_header */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* OBJECT */
#include "pattern.h"            /* fTreatScreenAsPattern */
#include "rcbadjst.h"           /* rcba_doing_color_adjust */
#include "spdetect.h"           /* new_color_detected */
#include "swerrors.h"           /* detail_error_handler */
#include "tranState.h"          /* TsStroke */
#include "trap.h"               /* isTrappingActive */
#include "uvms.h"               /* UVS */
#include "htrender.h"           /* ht_is_object_based_screen */
#include "group.h"              /* groupMustComposite */

#include "gscblackevaluate.h"   /* cc_blackInput */
#include "gscblackreplace.h"    /* cc_getBlackPositionInOutputList */
#include "gsccalibpriv.h"       /* CLID_SIZEcalibration */
#include "gschcmspriv.h"        /* cc_getBlackIntercept */
#include "gscheadpriv.h"        /* GS_CHAINinfo */
#include "gschtonepriv.h"       /* cc_scan_halftone */
#include "gscparamspriv.h"      /* COLOR_PAGE_PARAMS */
#include "gscphotoinkpriv.h"    /* guc_interpolatePhotoinkTransform */
#include "gscxferpriv.h"        /* cc_transfer_create */
#include "gscdevcipriv.h"
#include "gs_callps.h"

/* These are defs for a cache that sits above the dciluts, the dcilut_cache,
 * which short-circuits a lot of processing and is important for performance.
 * Typically, for 8 bit data, there will be a very high hit rate. For 16 bit
 * data the hit rate may be low, but in this module we don't know where the
 * data came from so we can't selectively enable this cache.
 */
#define CACHEHASHBITS       (10)
#define CACHEHASHSIZE       (1 << CACHEHASHBITS)

typedef struct DCILUT_CACHE {
  COLORVALUE *input;                      /* [CACHEHASHSIZE] */
  COLORVALUE (*output)[CACHEHASHSIZE];    /* [nMappedColorants][CACHEHASHSIZE] */
} DCILUT_CACHE;

/* Useful debug data for measuring the performance of the dcilut_cache */
#ifdef DEBUG_BUILD
#define MAX_DEBUG_STATS_COLORANTS (12)

#define DEBUG_INCREMENT(x_) (x_)++
#define DEBUG_DECREMENT(x_) (x_)--
#define DEBUG_INCREMENT_ARRAY(a_, i_)  MACRO_START \
  if ((i_) < MAX_DEBUG_STATS_COLORANTS)  \
    DEBUG_INCREMENT((a_)[(i_)]);         \
MACRO_END
#else
#define DEBUG_INCREMENT(x_)
#define DEBUG_DECREMENT(x_)
#define DEBUG_INCREMENT_ARRAY(a_, i_)
#endif

/* The dcilut maps an input color onto m output colors. Normally m is 1,
 * but for photoink devices m is >= 1. The tables map uniformly spaced
 * input colors onto m arrays of output colors with color values evaluated
 * by interpolating between the mapped values.
 *
 * The input to the dcilut is a COLORVALUE that is assumed to be an integer
 * with a minimum of 0x0000 and a maximum of 0xFF00. And that these values are
 * linearly scaled from FP values with a range of 0.0 to 1.0.
 * With this assumption, we can use the top byte of a COLORVALUE as an index into
 * an 8 bit lut and the bottom byte as an 8 bit fractional part.
 * If ever this changes then the details of the dcilut and the dcilut cache will
 * have to accomodate the new reality.
 */
#define DCILUT_SIZE           (0x100)
#define DCILUT_CODE_notset    (0xFFFFu)

#if COLORVALUE_MAX != 0xFF00
  DCILUT_assumptions_about_COLORVALUE_have_been_broken!;
#endif

#define MAX_DEBUG_STATS_COLORANTS (12)

struct DCILUT {
  cc_counter_t  refCnt ;
  /* int32      nPopulated ; */
  uint32        nMappedColorants ;          /* Array sizes dependent on PhotoInk mappings */

  DCILUT      **headref ;
  DCILUT       *next ;
  DCILUT       *prev ;

  CLID          idhashXCD ;
  CLID          idslotX[ 3 ] ;              /* CL_TYPEtransfer */
  CLID          idslotC[ 3 ] ;              /* CL_TYPEcalibration */
  CLID          idslotD[ 4 ] ;              /* gsc_getSpotno/ImmediateRepro/ContoneMask */

  COLORVALUE    dciCodes[DCILUT_SIZE] ;

  /* Cache to provide faster conversion than interpolating the dciluts directly.
   */
  DCILUT_CACHE  *normalCache;
} ;

typedef Bool (LOOKUP_COLOR_FN)(CLINK        *pLink,
                               USERVALUE    *colorValues,
                               Bool         useCache,
                               COLORVALUE   *oColorValues);

/* A structure for folding info common to both devicecode and nonintercept links */
typedef struct {
  int32            colorType ;
  int32            blackPosition ;
  Bool             jobColorSpaceIsGray;
  Bool             setoverprint;
  Bool             overprintMode;
  Bool             overprintBlack;
  Bool             overprintWhite;
  Bool             fTransformedSpot;
  int32            *colorantsInSpotlist ;   /* [n_iColorants] */
  Bool             additive ;
  Bool             allowImplicit ;
  uint32           *nMappedColorants ;      /* [n_iColorants] */
  uint32           *overprintMask ;
  GUCR_RASTERSTYLE *hRasterStyle ;
  int32            nDeviceColorants ;
  Bool             fCompositing ;
  CLINK            *pHeadLink ;
  int32            headLinkBlackPos ;
  REPRO_COLOR_MODEL chainColorModel ;
  Bool             tableBasedColor;
  Bool             overprintsEnabled;
} OVERPRINTinfo ;

struct CLINKDEVICECODEinfo {
  /* Overprinting phase. */
  GS_DEVICECODEinfo  *devicecodeInfo;
  uint32             *overprintMask ;
  uint8              overprintFlags ;
  int32              colorType ;
  int32              nDeviceColorants ;
  int32              nSortedColorants ;
  int32              nReducedColorants ;
  int32              blackPosition ;
  Bool               allowImplicit ;
  uint8              patternPaintType ;
  CLINK              *pHeadLink ;      /* For the input color values */

  Bool               fTransformedSpot; /* if a spot has been transformed to its
                                          alternative space, and it has been
                                          overprinted, the device colorants are
                                          all max bit blt overprinted */

  int32              *colorantsInSpotlist ; /* [n_iColorants] */
  COLORSPACE_ID      jobColorSpace ;
  Bool               tableBasedColor;
  Bool               overprintsEnabled;
  Bool               immediateRepro;
  Bool               forcePositive;
  Bool               fApplyMaxBlts; /* if this chain is using max blitting, we
                                       don't want to reduce the colorant set, but
                                       instead rely on overprint bits recorded
                                       separately with the color */
  OVERPRINTinfo      overprintInfo ;    /* A folded copy of some of the above */


  /* An integer representation of the float input values. It scales values
   * of 0-1 to 0-0xFF00. See above comments next to COLORVALUE_SCALE_FACTOR.
   */
  COLORVALUE         *cFF00;                /* [n_iColorants] */

  /* Produce oColorValues phase */
  DEVICECODE_TYPE    deviceCodeType ;
  CLINK              **transferLinks ;      /* [n_iColorants] */
  CLINK              **calibrationLinks ;   /* [n_iColorants] */
  struct {
    Bool cached;
    union {
      struct CALLPSCACHE *cpsc;
      OBJECT psfunc;
    } func;
  } transfer;
  const GUCR_PHOTOINK_INFO *photoinkInfo ;
  struct { /* ContoneMask remaps clear devicecode values to a replacement value. */
    int              value;                  /** Original page device value. */
    COLORVALUE       threshold, replacement; /** Threshold/replacement values to
                                                 avoid work in the invokes. */
  } contoneMask;
  GS_HALFTONEinfo    *halftoneInfo ;
  SPOTNO             spotno ;
  HTTYPE             httype;
  uint8              fBgFlags ;
  GUCR_RASTERSTYLE   *hRasterStyle ;
  GUCR_RASTERSTYLE   *hDeviceRasterStyle ;

  /* For mapping input colorants to photoink output colorants */
  uint32             *nMappedColorants ;    /* [n_iColorants] */
  COLORANTINDEX      **colorantMaps ;       /* [n_iColorants] */

  /* The function that convert input color to device codes */
  LOOKUP_COLOR_FN    *lookupColor ;

  /* The result of the transform through transfer/calibration/photoink.
   * oColorants is the same as 'pLink->iColorants' except for photoinks when it is
   * the array of colorants produced from the photoink transform stage.
   */
  COLORVALUE         *oColorValues ;        /* [nDeviceColorants] */
  COLORANTINDEX      *oColorants ;          /* [nDeviceColorants] */
  COLORVALUE         *tmpColorValues ;      /* [nDeviceColorants] */

  /* May need to quantise output values to ht device codes for the
   * updateHTCacheFunctions.
   */
  HT_TRANSFORM_INFO  *sorted_htTransformInfo; /* [nDeviceColorants] */

  /* These are for sorting oColorValues into the numerical order required for
   * the dl_color. */
  int32              *sortingIndexes ;      /* [nDeviceColorants] */
  COLORANTINDEX      *sortedColorants ;     /* [nDeviceColorants] */
  COLORVALUE         *sortedColorValues ;   /* [nDeviceColorants] */
  /* This flag indicate a direct mapping of input to output colorants which
   * allows things to be short-circuited. */
  Bool               simpleColorantOrder ;

  /* These are the result of overprint reduction when colour values are zero. */
  COLORANTINDEX      *reducedColorants ;    /* [nDeviceColorants] */
  COLORVALUE         *reducedColorValues ;  /* [nDeviceColorants] */

 /* Converts iColorValues to a device code output. It requires iColorValues
  * to be sorted according to sortedColorants. */
  void               (*updateHTCacheFunction)( CLINK *pLink , int32 nColors ,
                                               COLORVALUE *iColorValues,
                                               COLORVALUE *oColorValues ) ;

  /* Interpolation table used for all of above. */
  DCILUT             **dciluts ;            /* [n_iColorants] */
  /* These are used for faster accesses to the arrays in 'dciluts' */
  Bool               useDCILUTnormalCache;
  COLORVALUE         **cacheInput;          /* [n_iColorants] */
  COLORVALUE         **cacheOutput;         /* [nDeviceColorants] */
  uint16             *cacheHash;            /* [n_iColorants] */

  /* The color space object of the DeviceN/Separation output colorants (for HDLT) */
  OBJECT             colorSpaceObj;
  OBJECT             devicenCSA[MAX_CSA_LENGTH];

  /* The page containing the dl_colors produced by this chain */
  DL_STATE           *page;

  /* Compositing phase. */
  Bool               fCompositing;

#ifdef DEBUG_BUILD
  /* Useful debug data for measuring the performance of the dcilut_cache */
  int32              cacheHits;
  int32              cacheMisses;
  int32              channelHits[MAX_DEBUG_STATS_COLORANTS];
  int32              channelMisses[MAX_DEBUG_STATS_COLORANTS];
#endif
} ;

struct GS_DEVICECODEinfo {
  cc_counter_t      refCnt ;
  size_t            structSize ;

  /* The overprint values for GSC_FILL & GSC_STROKE */
  Bool              overprinting[2];

  /* This is used for temporarily disabling overprints in this gstate. It works
   * in tandem with the Overprint systemparam.
   */
  Bool              overprintsEnabled;

  /* The value actually used, which could be derived from the OverprintProcess
   * userparam or from the pdf OPM or ps setoverprintmode as modified by the
   * IgnoreOverprintMode userparam.
   */
  Bool              overprintMode;

  /* The value of the parameter last passed to setoverprintmode. It is retained
   * soley for use by currentoverprintmode.
   */
  Bool              currentOverprintMode;

  /* The gstate equivalent of overprint userparams. The userparams have no effect
   * after the values have been copied into here.
   */
  Bool              overprintBlack;
  Bool              overprintGray;
  Bool              overprintGrayImages;
  Bool              overprintWhite;
  Bool              ignoreOverprintMode;
  Bool              transformedSpotOverprint;
  Bool              overprintICCBased;
} ;

struct GS_BLOCKoverprint {
  size_t structSize ;         /* Size of this structure */
  uint32 *overprintMask ;     /* Union of block colour overprint masks */
  int32 *sortingIndexes ;
  COLORANTINDEX *sortedColorants ;
  COLORANTINDEX *overprintedColorants ;
  int32 n_Colorants ;
  uint8 overprintAll ;        /* Union of overprint flags */
  uint8 overprintUseMask ;    /* Has mask been set? */
  uint8 overprintOpCode ;     /* Opcode used to apply overprint flags */
} ;

/* ---------------------------------------------------------------------- */
/* Macros for handling the overprint mask. */

/* Gives the number of bytes/words for _nColorants bit flags (word-aligned). */
/* overprintMask created, but not used, for image color chains for simplicity. */
#define OVERPRINTMASK_BYTESIZE( _nColorants ) ((((_nColorants) + 31) & ~31) >> 3)
#define OVERPRINTMASK_WORDSIZE( _nColorants )  (((_nColorants) + 31) >> 5)

/* Sets all the bit flags to knockout (ie paint every colorant). */
#define OVERPRINTMASK_KNOCKOUT 0u
#define OVERPRINTMASK_OVERPRINT (uint32)-1

/* Sets all the bit flags to initial value. Mask expression ignores bits which
   are spare in final word. Mask expression works for nColorants > 32. */
#define SET_OVERPRINTMASK(_overprintMask, _nColorants, _initial) MACRO_START \
  int32 _nWords_ = OVERPRINTMASK_WORDSIZE((_nColorants)) ; \
  uint32 _value_ = (_initial) & ((uint32)-1 << ((32 - (_nColorants)) & 31)) ; \
  while ((_nWords_) > 0) { \
    _nWords_ -= 1 ; \
    (_overprintMask)[(_nWords_)] = _value_ ; \
    _value_ = (_initial) ; \
  } \
MACRO_END

/* Merge two overprint masks. Flag is set to FALSE if there are no overprints
   left in mask, TRUE if there are. */
#define INTERSECT_OVERPRINTMASK(_targetMask, _sourceMask, _nColorants, _flag) MACRO_START \
  int32 _nWords_ = OVERPRINTMASK_WORDSIZE((_nColorants)) ; \
  register uint32 _any_ = 0 ; \
  while ((_nWords_) > 0) { \
    _nWords_ -= 1 ; \
    (_targetMask)[(_nWords_)] &= (_sourceMask)[(_nWords_)] ; \
    _any_ |= (_targetMask)[(_nWords_)] ; \
  } \
  (_flag) = (uint8)(_any_ != 0 ) ; \
MACRO_END

#define OVERPRINT_COLORANT( _overprintMask , _index ) \
  (_overprintMask)[((_index) >> 5)] |= (0x80000000u >> ((_index) & 31)) ;

#define KNOCKOUT_COLORANT( _overprintMask , _index ) \
  (_overprintMask)[((_index) >> 5)] &= ~(0x80000000u >> ((_index) & 31)) ;

#define PAINT_COLORANT( _overprintMask , _index ) \
  (((_overprintMask)[((_index) >> 5)] & (0x80000000u >> ((_index) & 31))) == 0 )

/* ---------------------------------------------------------------------- */

static void  devicecode_destroy( CLINK *pLink ) ;
static LOOKUP_COLOR_FN dci_lookup_colorSlow;
static LOOKUP_COLOR_FN dci_lookup_colorFast;
static Bool dci_invoke(CLINK       *pLink,
                       COLORVALUE  *cFF00,
                       int32       channel,
                       COLORVALUE  *oDeviceCodes);
static Bool dci_invokexfercal(CLINKDEVICECODEinfo *devicecode ,
                              int32               i ,
                              USERVALUE           ival ,
                              COLORVALUE          *oval );
static Bool devicecode_invokeSingle( CLINK *pLink , USERVALUE *dummy_oColorValue ) ;
static Bool devicecode_invokeBlock(  CLINK *pLink , CLINKblock *pBlock ) ;
static mps_res_t devicecode_scan( mps_ss_t ss, CLINK *pLink );

static size_t devicecodeStructSize( int32 nColorants , int32 nDeviceColorants ) ;
static void   devicecodeUpdatePtrs( CLINK *pLink , int32 nColorants , int32 nDeviceColorants ) ;

#if defined( ASSERT_BUILD )
static void devicecodeAssertions( CLINK *pLink ) ;
#else
#define devicecodeAssertions( pLink ) EMPTY_STATEMENT()
#endif

static Bool createdevicecodeinfo( GS_DEVICECODEinfo   **devicecodeInfo );
static Bool updatedevicecodeinfo( GS_DEVICECODEinfo   **devicecodeInfo );
static Bool copydevicecodeinfo( GS_DEVICECODEinfo *devicecodeInfo ,
                                GS_DEVICECODEinfo **devicecodeInfoCopy );
#if defined( ASSERT_BUILD )
static void devicecodeInfoAssertions(GS_DEVICECODEinfo *pInfo);
#else
#define devicecodeInfoAssertions( pInfo ) EMPTY_STATEMENT()
#endif

static Bool internaliseDeviceNColorSpace(OBJECT           *dstColorSpace,
                                         OBJECT           *srcColorSpace,
                                         OBJECT           *dstDevicenCSA,
                                         GS_COLORinfo     *colorInfo,
                                         OBJECT           *illegalTintTransform);
static Bool deviceCheckSetColor( CLINK *pLink ) ;
static Bool deviceCheckSetScreen( CLINKDEVICECODEinfo * devicecode );

static void set_updateHTCacheFunction( CLINKDEVICECODEinfo *devicecode,
                                       int32               nColorants,
                                       GUCR_RASTERSTYLE    *hRasterStyle );
static Bool set_clidsize(int32 nColorants);
static void updateHTCacheForHalftoneBackdropRender( CLINK *pLink ,
                                                    int32 nColors ,
                                                    COLORVALUE *iColorValues,
                                                    COLORVALUE *oColorValues );
static void updateHTCacheForHalftone( CLINK *pLink , int32 nColors ,
                                      COLORVALUE *iColorValues,
                                      COLORVALUE *oColorValues ) ;
static void updateHTCacheForHalftoneTrapping( CLINK *pLink , int32 nColors ,
                                              COLORVALUE *iColorValues,
                                              COLORVALUE *oColorValues ) ;
static void updateHTCacheForHalftoneShfill( CLINK *pLink , int32 nColors ,
                                            COLORVALUE *iColorValues,
                                            COLORVALUE *oColorValues ) ;
static void updateHTCacheForContone( CLINK *pLink , int32 nColors ,
                                     COLORVALUE *iColorValues ,
                                     COLORVALUE *oColorValues ) ;
static void updateHTCacheForContoneTrapping( CLINK *pLink , int32 nColors ,
                                             COLORVALUE *iColorValues ,
                                             COLORVALUE *oColorValues ) ;
static void updateHTCacheForPatternContone( CLINK *pLink , int32 nColors ,
                                            COLORVALUE *iColorValues ,
                                            COLORVALUE *oColorValues ) ;
static void updateHTCacheForNothing( CLINK *pLink , int32 nColors ,
                                     COLORVALUE *iColorValues ,
                                     COLORVALUE *oColorValues ) ;

static DCILUT *dcilut_create( DCILUT **headref,
                              CLID    idslotX[ 3 ],
                              CLID    idslotC[ 3 ],
                              CLID    idslotD[ 4 ],
                              uint32  nMappedColorants );
static void dcilut_free( DCILUT *dcilut ) ;

static Bool background_colorants( int32            nColorants ,
                                  COLORANTINDEX    iColorants[] ,
                                  uint8            *fBgFlags ,
                                  GUCR_RASTERSTYLE *hRasterStyle ) ;
static void sort_colorants( int32         nColorants ,
                            COLORANTINDEX iColorants[] ,
                            int32         *pnColorants ,
                            COLORANTINDEX oColorants[] ,
                            int32         oi_mapping[] ,
                            int16         duplicateColorants ,
                            Bool          *simpleMapping ) ;

static Bool op_allow_implicit( int32              colorType,
                               COLORSPACE_ID      jobColorSpace ,
                               CLINK              *pHeadLink,
                               Bool               matchingICCprofiles,
                               GS_DEVICECODEinfo  *devicecodeInfo );
static Bool op_colorant_in_spotlist( COLORANTINDEX    ci,
                                     CLINK            *pheadlink,
                                     GUCR_RASTERSTYLE *hRasterStyle ) ;
static Bool op_decide_overprints(CLINK          *pLink,
                                 USERVALUE      *colorValues,
                                 uint8          overprintProcess,
                                 OVERPRINTinfo  *overprintInfo,
                                 Bool           *implicitOverprinting) ;

static Bool cc_isoverprintpossible(CLINKDEVICECODEinfo *devicecode) ;

static Bool getoverprint( GS_DEVICECODEinfo *devicecodeInfo, int32 colorType ) ;

static Bool get_nDeviceColorants(int32               nColorants ,
                                 COLORANTINDEX       *colorants ,
                                 GUCR_RASTERSTYLE    *hRasterStyle );

static CLINKfunctions CLINKdevicecode_functions =
{
    devicecode_destroy ,
    devicecode_invokeSingle ,
    devicecode_invokeBlock ,
    devicecode_scan
};

/* ---------------------------------------------------------------------- */
/*   Non-intercept functions and structures... */

struct CLINKNONINTERCEPTinfo
{
  uint32             *overprintMask ;
  uint8              overprintFlags ;
  int32              colorType ;
  int32              nDeviceColorants ;
  int32              nSortedColorants ;
  int32              n_overprintedColorants ;
  int32              blackPosition ;
  Bool               allowImplicit ;
  Bool               fTransformedSpot ;
  int32              *colorantsInSpotlist ;     /* [n_iColorants] */
  CLINK              *pHeadLink ;
  CLINK              *devicecodeLink ;
  Bool               mayAvoidMaxBlitting ;
  uint32             *nMappedColorants ;        /* [n_iColorants] */
  COLORANTINDEX      *oColorants ;              /* [nDeviceColorants] */
  int32              *sortingIndexes ;          /* [nDeviceColorants] */
  COLORANTINDEX      *sortedColorants ;         /* [nDeviceColorants] */
  COLORANTINDEX      *replaceColorants ;        /* [nDeviceColorants] */
  COLORVALUE         *replaceColorValues ;      /* [nDeviceColorants] */
  COLORANTINDEX      *overprintedColorants ;    /* [nDeviceColorants] */
  OVERPRINTinfo      overprintInfo ;            /* A folded copy of some of the above */
  DL_STATE           *page;
} ;

static void  cc_nonintercept_destroy( CLINK *pLink ) ;
static Bool cc_nonintercept_invokeSingle( CLINK *pLink , USERVALUE *dummy_oColorValue ) ;
static Bool cc_nonintercept_invokeBlock(  CLINK *pLink , CLINKblock *pBlock ) ;
static mps_res_t cc_nonintercept_scan( mps_ss_t ss, CLINK *pLink );

static size_t noninterceptStructSize( int32 nColorants, int32 nDeviceColorants ) ;
static void   noninterceptUpdatePtrs( CLINK *pLink , int32 nColorants, int32 nDeviceColorants ) ;

#if defined( ASSERT_BUILD )
static void noninterceptAssertions( CLINK *pLink ) ;
#else
#define noninterceptAssertions( pLink ) EMPTY_STATEMENT()
#endif

static CLINKfunctions CLINKnonintercept_functions =
{
    cc_nonintercept_destroy ,
    cc_nonintercept_invokeSingle ,
    cc_nonintercept_invokeBlock ,
    cc_nonintercept_scan
};

/* ---------------------------------------------------------------------- */
CLINK *cc_devicecode_create(GS_COLORinfo          *colorInfo,
                            GUCR_RASTERSTYLE      *hRasterStyle,
                            int32                 colorType,
                            GS_CONSTRUCT_CONTEXT  *context,
                            COLORSPACE_ID         jobColorSpace,
                            Bool                  fIntercepting,
                            Bool                  fCompositing,
                            CLINK                 *pHeadLink,
                            DEVICECODE_TYPE       deviceCodeType,
                            uint8                 patternPaintType,
                            REPRO_COLOR_MODEL     chainColorModel,
                            int32                 headLinkBlackPos,
                            OBJECT                *illegalTintTransform,
                            Bool                  *fApplyMaxBlts)
{
  int32               i;
  uint32              j;
  int32               dev_i;
  CLINK               *pLink;
  CLINKDEVICECODEinfo *devicecode;
  CLID                idslotD[ 4 ];
  CLID                idslotCX[ 3 ];
  int32               nColorants;
  int32               nDeviceColorants;
  COLORANTINDEX       *colorants;
  COLORSPACE_ID       colorSpace;
  Bool                jobColorSpaceIsGray;
  Bool                isFirstTransferLink;
  TRANSFORMEDSPOT     transformedSpotType;
  OBJECT              *PSColorSpace;
  OVERPRINTinfo       *overprintInfo;
  int32               realDeviceTarget;
  const GUCR_PHOTOINK_INFO  *photoinkInfo;

  COLOR_PAGE_PARAMS   *colorPageParams;

  colorPageParams = &context->page->colorPageParams;

  HQASSERT(patternPaintType == NO_PATTERN ||
           patternPaintType == COLOURED_PATTERN ||
           patternPaintType == UNCOLOURED_PATTERN,
           "Paint type for device code link wrong") ;

  realDeviceTarget = ! guc_backdropRasterStyle(hRasterStyle);

  nColorants = context->colorspacedimension;
  colorants = context->pColorantIndexArray;
  nDeviceColorants = get_nDeviceColorants(nColorants, colorants, hRasterStyle);
  colorSpace = context->chainColorSpace;
  jobColorSpaceIsGray = context->jobColorSpaceIsGray;
  isFirstTransferLink = (context->nTransferLinks == 1);
  transformedSpotType = context->transformedSpotType;
  PSColorSpace = context->PSColorSpace;

  if (jobColorSpaceIsGray) {
    /* Make device independent color look like DeviceGray when appropriate for
     * overprints etc.
     */
    HQASSERT(jobColorSpace == SPACE_CalGray  || jobColorSpace == SPACE_CIEBasedA ||
             jobColorSpace == SPACE_ICCBased || jobColorSpace == SPACE_DeviceGray,
             "jobColorSpaceIsGray is inconsistent");
    HQASSERT((pHeadLink != NULL && pHeadLink->n_iColorants == 1) ||
             (pHeadLink == NULL && nColorants == 1),
             "invalid number of colorants with jobColorSpaceIsGray");
    jobColorSpace = SPACE_DeviceGray ;
  }

  photoinkInfo = guc_photoinkInfo(hRasterStyle);

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEdevicecode (looking at invokeSingle) we have:
   * a) CL_TYPEtransfer (3x32 bits (oring in the sub types))
   * b) CL_TYPEcalibration (2x32 bits (oring in the sub types))
   * c) gsc_getSpotno (32 bits)
   * +
   * d) application of SystemParams->ImmediateRepro (aka ht_applyTransform) (1 bit)
   * e) application of SystemParams->TableBasedColor (1 bit)
   * f) application of Background Separation (1 bit)
   * g) application of SystemParams->Overprint (1 bit)
   * h) application of colorType (2 bits)
   * i) application of overprintMode (1 bit)
   * j) application of overprintBlack (1 bit)
   * k) application of overprintGray (1 bit)
   * l) application of overprintGrayImages (1 bit)
   * m) application of overprintWhite (1 bit)
   * n) application of setoverprint (1 bit)
   * +
   * o) handle to raster style (32 bits)

   * The following are also used in the invoke routines, but are not counted
   * here because they are explicitly included in the color cache entries along
   * with the color values. This is to avoid lots of unnecessary chain
   * reconstructions.
   * 1) application of Cyan overprintProcess bit flag (1 bit)
   * 2) application of Magenta overprintProcess bit flag (1 bit)
   * 3) application of Yellow overprintProcess bit flag (1 bit)
   * 4) application of Black overprintProcess bit flag (1 bit)
   * =
   * 9 slots.
   */
#define CLID_SIZEdisabled       (COLCACHE_DISABLE)
#define CLID_SIZEdevicecode     (12)
  pLink = cc_common_create( nColorants ,
                            colorants ,
                            colorSpace ,
                            colorSpace ,
                            CL_TYPEdevicecode ,
                            devicecodeStructSize( nColorants, nDeviceColorants ) ,
                            & CLINKdevicecode_functions ,
                            set_clidsize( nColorants )) ;
  if ( pLink == NULL )
    return NULL ;

  devicecodeUpdatePtrs( pLink , nColorants , nDeviceColorants ) ;
  devicecode = pLink->p.devicecode ;

  /* Initialise enough to make it safe to call devicecode_destroy() */
  for (i = 0 ; i < nColorants ; ++i) {
    devicecode->dciluts[i] = NULL;
    devicecode->transferLinks[i] = NULL;
    devicecode->calibrationLinks[i] = NULL;
  }
  for (i = 0; i < MAX_CSA_LENGTH; i++)
    devicecode->devicenCSA[i] = onothing;  /* Struct copy to set slot properties */
  devicecode->transfer.cached = FALSE;
  devicecode->transfer.func.cpsc = NULL;


  devicecode->nDeviceColorants = nDeviceColorants ;
  devicecode->hRasterStyle = hRasterStyle;
  devicecode->hDeviceRasterStyle = gsc_getRS(colorInfo);
  HQASSERT(!guc_backdropRasterStyle(devicecode->hDeviceRasterStyle),
           "Expected device rasterstyle");
  devicecode->devicecodeInfo = colorInfo->devicecodeInfo;

  devicecode->patternPaintType = patternPaintType;

  /* Transparent objects should use the page's default screen */
  if ( cc_getOpaque(colorInfo,
                    (colorType == GSC_STROKE ? TsStroke : TsNonStroke))
       && (context->page->currentGroup == NULL
           || !groupMustComposite(context->page->currentGroup)) )
    devicecode->spotno = gsc_getSpotno(colorInfo) ;
  else
    devicecode->spotno = context->page->default_spot_no;

  devicecode->httype =
    ht_is_object_based_screen(devicecode->spotno)
    ? gsc_getRequiredReproType(colorInfo, colorType)
    : HTTYPE_DEFAULT;

  /* This page context will be used by dl_colors from invoke functions */
  devicecode->page = context->page;

  devicecode->fCompositing = fCompositing;

#ifdef DEBUG_BUILD
  /* Useful debug data for measuring the performance of the dcilut_cache */
  devicecode->cacheHits = 0;
  devicecode->cacheMisses = 0;
  for ( i = 0; i < MAX_DEBUG_STATS_COLORANTS; ++i ) {
    devicecode->channelHits[i] = 0;
    devicecode->channelMisses[i] = 0;
  }
#endif

  devicecode->tableBasedColor = colorPageParams->tableBasedColor;
  devicecode->overprintsEnabled = cc_overprintsEnabled(colorInfo, colorPageParams);
  devicecode->immediateRepro = colorPageParams->immediateRepro;
  devicecode->forcePositive = colorPageParams->forcePositive;

  /* Assign the mappings to translate photoink input colorants to the outputs */
  if (photoinkInfo != NULL) {
    COLORANTINDEX *cis ;
    for ( i = 0, dev_i = 0 ; i < nColorants ; ++i ) {
      HQASSERT(colorants[i] != COLORANTINDEX_ALL,
               "/All sep should have been converted for photoink") ;
      if (colorants[i] == COLORANTINDEX_NONE) {
        devicecode->colorantMaps[i] = NULL;
        devicecode->oColorants[dev_i++] = colorants[i] ;
        devicecode->nMappedColorants[i] = 1 ;
      }
      else {
        cis = guc_getColorantMapping( hRasterStyle, colorants[i] ) ;
        HQASSERT(cis != NULL, "Colorant mapping must exist for photoink") ;
        devicecode->nMappedColorants[i] = 0;
        devicecode->colorantMaps[i] = cis;
        while (*cis != COLORANTINDEX_UNKNOWN) {
          devicecode->oColorants[dev_i++] = *cis++ ;
          devicecode->nMappedColorants[i]++ ;
        }
      }
    }
    HQASSERT(dev_i == nDeviceColorants, "colorant count error") ;
  }
  else {
    for ( i = 0 ; i < nColorants ; ++i ) {
      devicecode->nMappedColorants[i] = 1 ;
      devicecode->colorantMaps[i] = NULL;
      devicecode->oColorants[i] = colorants[i] ;
    }
  }

  /* Colorants have to be sorted by colorant index in the dl_color. We use the
   * sortedColorants array for those ci's, and do the sorting after both the
   * dci_invoke (which doesn't change the colorant order) and the photoink
   * conversions (which do change the order). This leads to a slightly tricky
   * method of retaining performance for simple cases, where 'simpleColorantOrder'
   * is used to bypass appropriate steps in the color conversions.
   */
  if ( nColorants > 0 )
    sort_colorants( nDeviceColorants ,
                    devicecode->oColorants ,
                    &devicecode->nSortedColorants ,
                    devicecode->sortedColorants ,
                    devicecode->sortingIndexes ,
                    context->page->colorPageParams.duplicateColorants ,
                    &devicecode->simpleColorantOrder) ;
  else {
    devicecode->nSortedColorants = 0 ;
    devicecode->simpleColorantOrder = FALSE ;
  }
  if (photoinkInfo != NULL)
    devicecode->simpleColorantOrder = FALSE;

  if (devicecode->nSortedColorants > 0) {
    ht_setupTransforms(devicecode->spotno, devicecode->httype,
                       devicecode->nSortedColorants,
                       devicecode->sortedColorants,
                       hRasterStyle,
                       devicecode->sorted_htTransformInfo);
  }

  if ( ! background_colorants( devicecode->nSortedColorants ,
                               devicecode->sortedColorants ,
                               & devicecode->fBgFlags ,
                               hRasterStyle )) {
    devicecode_destroy(pLink) ;
    return NULL ;
  }

  /* Point pHeadLink at the first link in this chain for use in the determination
   * of overprints etc. But use the the base space of an Indexed colorspace.
   */
  if ( pHeadLink == NULL )
    devicecode->pHeadLink = pLink ;
  else if ( pHeadLink->iColorSpace == SPACE_Indexed )
    devicecode->pHeadLink = pHeadLink->pnext ? pHeadLink->pnext : pLink ;
  else
    devicecode->pHeadLink = pHeadLink ;

  for ( i = 0 ; i < nColorants ; ++i ) {
    devicecode->transferLinks[ i ] = NULL ;
    devicecode->calibrationLinks[ i ] = NULL ;

    devicecode->dciluts[ i ] = NULL ;
  }

  devicecode->transfer.cached = colorPageParams->transfer.cached;
  if ( devicecode->transfer.cached ) {
    devicecode->transfer.func.cpsc = colorPageParams->transfer.func.cpsc;
    if ( devicecode->transfer.func.cpsc )
      reserve_callpscache(devicecode->transfer.func.cpsc);
  } else {
    devicecode->transfer.func.psfunc = colorPageParams->transfer.func.psfunc;
  }

  /* Check for ContoneMask and set up threshold and replacement values. */
  if ( colorPageParams->contoneMask != 0 && nDeviceColorants > 0 &&
       !guc_backdropRasterStyle(hRasterStyle) &&
       !gucr_halftoning(hRasterStyle) ) {
    float clear = (float)ht_getClear(devicecode->spotno, devicecode->httype,
                                     devicecode->oColorants[0], hRasterStyle);

    if ( colorPageParams->contoneMask < 0 ||
         colorPageParams->contoneMask > clear ) {
      (void)detail_error_handler(RANGECHECK,
        "ContoneMask out of range for this configuration");
      devicecode_destroy(pLink);
      return NULL;
    }

    /* Values are rounded to device codes and therefore a value within half a
       device code is treated as 'zero'.  At this point values are additive. */
    devicecode->contoneMask.value = colorPageParams->contoneMask;
    devicecode->contoneMask.threshold =
      FLOAT_TO_COLORVALUE((clear - 0.5f) / clear);
    devicecode->contoneMask.replacement =
      FLOAT_TO_COLORVALUE(1.0f - ((float)devicecode->contoneMask.value / clear));
  } else {
    devicecode->contoneMask.value = 0;
    devicecode->contoneMask.threshold =
      devicecode->contoneMask.replacement = COLORVALUE_INVALID;
  }

  /* Use one of the lut caches for non-pattern chains, if available. NB. We could
   * use both in the same chain, but it hasn't been observed to make a difference,
   * and we'd need 2 lots of devicecode->cacheInput/Output arrays if we did.
   */
  if (devicecode->patternPaintType == NO_PATTERN)
    devicecode->useDCILUTnormalCache = TRUE;
  else
    devicecode->useDCILUTnormalCache = FALSE;

  /* Choose the appropriate funcion for color converting inputs to device codes */
  if (devicecode->simpleColorantOrder && devicecode->tableBasedColor)
    devicecode->lookupColor = dci_lookup_colorFast;
  else
    devicecode->lookupColor = dci_lookup_colorSlow;

  /* Create the sub clinks for transfer & calibration. */
  idslotD[ 0 ] = (CLID) devicecode->spotno ;
  idslotD[ 2 ] = (CLID) (devicecode->immediateRepro ? 0x01 : 0x00) ;
  idslotD[ 3 ] = (CLID) devicecode->contoneMask.value;

  for ( i = 0, dev_i = 0 ; i < nColorants ; ++i ) {
    CLINK *pXLink = NULL;
    CLINK *pCLink = NULL;
    CLID* transferIdSlot, *calibrationIdSlot;
    Bool applyTransfer = FALSE;
    Bool applyDummyTransfer = FALSE;
    Bool applyCalibration = FALSE;
    Bool applyJobPolarity = (! fCompositing);

    idslotCX[ 0 ] = idslotCX[ 1 ] = idslotCX[ 2 ] = 0xFFFFFFFF ;
    idslotD[ 1 ] = (CLID) colorants[ i ] ;

    transferIdSlot = idslotCX;
    calibrationIdSlot = idslotCX;

    switch (deviceCodeType) {
    case DC_TYPE_none:
      applyJobPolarity = FALSE;
      applyDummyTransfer = TRUE;
      break;
    default:
      HQFAIL("Unrecognised device code c-link type");
      /*FALLTHRU*/
    case DC_TYPE_normal:
      HQASSERT(realDeviceTarget, "Expected a real raster style");
      applyTransfer = TRUE;
      applyCalibration = TRUE;
      break;
    case DC_TYPE_halftone_only:
      applyDummyTransfer = TRUE;
      break;
    case DC_TYPE_transfer_only:
      applyTransfer = TRUE;
      break;
    case DC_TYPE_calibration_only:
      HQASSERT(realDeviceTarget, "Expected a real raster style");
      applyDummyTransfer = TRUE;
      applyCalibration = TRUE;
      break;
    }

    HQASSERT(! applyTransfer || ! applyDummyTransfer,
             "Both applyTransfer and applyDummyTransfer are set");

    if (applyTransfer) {
      pXLink = cc_transfer_create(1, & colorants[i],
                                  colorSpace,
                                  gsc_getRequiredReproType(colorInfo, colorType),
                                  jobColorSpaceIsGray,
                                  isFirstTransferLink,
                                  TRUE /* isLastTransferLink */,
                                  applyJobPolarity,
                                  colorInfo->transferInfo,
                                  colorInfo->halftoneInfo,
                                  hRasterStyle,
                                  devicecode->forcePositive,
                                  &context->page->colorPageParams);
      devicecode->transferLinks[i] = pXLink;
      if (pXLink == NULL) {
        devicecode_destroy(pLink);
        return NULL;
      }
      transferIdSlot = pXLink->idslot;
    }

    if (applyDummyTransfer) {
      pXLink = cc_dummy_transfer_create(1, & colorants[i],
                                        colorSpace,
                                        isFirstTransferLink,
                                        TRUE /* isLastTransferLink */,
                                        ! fCompositing, /* applyJobPolarity */
                                        colorPageParams->negativeJob);
      devicecode->transferLinks[i] = pXLink;
      if (pXLink == NULL) {
        devicecode_destroy(pLink);
        return NULL;
      }
      transferIdSlot = pXLink->idslot;
      HQASSERT(pXLink->idcount == 3, "No. of xfer ID slots changed");
    }

    if (applyCalibration) {
      pCLink = cc_calibration_create(1, & colorants[i],
                                     colorSpace,
                                     colorInfo->calibrationInfo,
                                     hRasterStyle,
                                     fCompositing);
      devicecode->calibrationLinks[i] = pCLink;
      if (pCLink == NULL) {
        devicecode_destroy(pLink);
        return NULL;
      }
      calibrationIdSlot = pCLink->idslot;
      HQASSERT(pCLink->idcount == 2, "No. of calibration ID slots changed");
    }

    devicecode->dciluts[i] =
      dcilut_create(&colorInfo->colorState->dciluts,
                    transferIdSlot, calibrationIdSlot, idslotD,
                    devicecode->nMappedColorants[i]);
    if (devicecode->dciluts[i] == NULL) {
      devicecode_destroy(pLink);
      return NULL;
    }

    /* Can't use a fast lut cache to bypass most device code processing if not available */
    if (devicecode->dciluts[i]->normalCache == NULL)
      devicecode->useDCILUTnormalCache = FALSE;
  }

  /* Assign pointers to one of the dcilut caches which can improve performance
   * for many cases */
  for ( i = 0, dev_i = 0 ; i < nColorants ; ++i ) {
    if (devicecode->useDCILUTnormalCache) {
      devicecode->cacheInput[i] = devicecode->dciluts[i]->normalCache->input;
      for (j = 0; j < devicecode->dciluts[i]->nMappedColorants; j++) {
        devicecode->cacheOutput[dev_i] = devicecode->dciluts[i]->normalCache->output[j];
        dev_i++;
      }
    }
    else {
      devicecode->cacheInput[i] = NULL;
      for (j = 0; j < devicecode->dciluts[i]->nMappedColorants; j++) {
        devicecode->cacheOutput[dev_i] = NULL;
        dev_i++;
      }
    }
  }

  devicecode->colorType = colorType ;
  devicecode->nReducedColorants = 0 ;

  devicecode->blackPosition = -1;
  if (!cc_getBlackPositionInOutputList(nColorants, colorants, hRasterStyle,
                                       &devicecode->blackPosition)) {
    devicecode_destroy(pLink);
    return NULL;
  }

  devicecode->allowImplicit = op_allow_implicit( colorType,
                                                 jobColorSpace,
                                                 devicecode->pHeadLink,
                                                 context->matchingICCprofiles,
                                                 devicecode->devicecodeInfo ) ;
  devicecode->deviceCodeType = deviceCodeType ;
  devicecode->photoinkInfo = photoinkInfo ;
  devicecode->halftoneInfo = colorInfo->halftoneInfo ;
  devicecode->jobColorSpace = jobColorSpace ;

  /* transformedSpotType is set when a chain involves using the alternative space
   * of a Separation/DeviceN. This is only relevant if specified in the job
   * rather than a by-product of interception etc.
   */
  HQASSERT(transformedSpotType == DC_TRANSFORMEDSPOT_NONE ||
           transformedSpotType == DC_TRANSFORMEDSPOT_NORMAL ||
           transformedSpotType == DC_TRANSFORMEDSPOT_INTERCEPT,
           "Unexpected value for transformedSpotType");
  devicecode->fTransformedSpot = transformedSpotType != DC_TRANSFORMEDSPOT_NONE &&
                                 (transformedSpotType == DC_TRANSFORMEDSPOT_INTERCEPT ||
                                  devicecode->devicecodeInfo->transformedSpotOverprint) &&
                                 (devicecode->pHeadLink->iColorSpace == SPACE_Separation ||
                                  devicecode->pHeadLink->iColorSpace == SPACE_DeviceN);

  for ( i = 0 ; i < nColorants ; ++i )
    if (devicecode->fTransformedSpot)
      devicecode->colorantsInSpotlist[i] = op_colorant_in_spotlist( pLink->iColorants[ i ] ,
                                                                    devicecode->pHeadLink ,
                                                                    hRasterStyle ) ;
    else
      /* colorantsInSpotlist will not be accessed */
      devicecode->colorantsInSpotlist[i] = -1 ;

  devicecode->fApplyMaxBlts = fIntercepting && !fCompositing &&
                              cc_isoverprintpossible(devicecode);

  /* Initialise the output color space */
  devicecode->colorSpaceObj = onull;  /* Struct copy to set slot properties */
  for (i = 0; i < MAX_CSA_LENGTH; i++)
    devicecode->devicenCSA[i] = onothing;  /* Struct copy to set slot properties */

  /* PSColorSpace is allowed to be NULL except for DeviceN/Separations */
  if (PSColorSpace != NULL) {
    if (!internaliseDeviceNColorSpace(&devicecode->colorSpaceObj, PSColorSpace,
                                      devicecode->devicenCSA,
                                      colorInfo, illegalTintTransform)) {
      devicecode_destroy(pLink);
      return NULL;
    }
  }

  /* page context can influence choice of functions and must be set
   * up before calling set_updateHTCacheFunction. */
  set_updateHTCacheFunction(devicecode, nColorants, hRasterStyle) ;

  /* Copy relevant info into the overprint structure */
  overprintInfo = &devicecode->overprintInfo ;
  overprintInfo->colorType           = devicecode->colorType ;
  overprintInfo->blackPosition       = devicecode->blackPosition ;
  overprintInfo->jobColorSpaceIsGray = jobColorSpaceIsGray ;
  overprintInfo->setoverprint        = getoverprint(devicecode->devicecodeInfo, colorType) ;
  overprintInfo->overprintMode       = devicecode->devicecodeInfo->overprintMode ;
  overprintInfo->overprintBlack      = devicecode->devicecodeInfo->overprintBlack ;
  overprintInfo->overprintWhite      = devicecode->devicecodeInfo->overprintWhite ;
  overprintInfo->fTransformedSpot    = devicecode->fTransformedSpot ;
  overprintInfo->colorantsInSpotlist = devicecode->colorantsInSpotlist ;
  overprintInfo->additive            = DeviceColorspaceIsAdditive(pLink->iColorSpace) ;
  overprintInfo->allowImplicit       = devicecode->allowImplicit ;
  overprintInfo->nMappedColorants    = devicecode->nMappedColorants ;
  overprintInfo->overprintMask       = devicecode->overprintMask ;
  overprintInfo->hRasterStyle        = hRasterStyle ;
  overprintInfo->nDeviceColorants    = nDeviceColorants ;
  overprintInfo->fCompositing        = devicecode->fCompositing ;
  overprintInfo->pHeadLink           = devicecode->pHeadLink ;
  overprintInfo->headLinkBlackPos    = headLinkBlackPos ;
  overprintInfo->chainColorModel     = chainColorModel ;
  overprintInfo->tableBasedColor     = devicecode->tableBasedColor ;
  overprintInfo->overprintsEnabled   = devicecode->overprintsEnabled ;

  /* Now populate the CLID slots: */
  { CLID *idslot = pLink->idslot ;
    if ( idslot ) {
      if (devicecode->transferLinks[0] == NULL &&
          devicecode->calibrationLinks[0] == NULL) {
        HQASSERT(CLID_SIZEdummytransfer == 0, "fix the dummy transfer CLID slots");
        idslot[ 0 ] = idslot[ 1 ] = idslot[ 2 ] = 0xFFFFFFFF ;
        idslot[ 3 ] = idslot[ 4 ] = idslot[ 5 ] = 0xFFFFFFFF ;
      }
      else {
        idslot[ 0 ] = idslot[ 1 ] = idslot[ 2 ] = 0 ;
        idslot[ 3 ] = idslot[ 4 ] = 0 ;
        for ( i = 0 ; i < nColorants ; ++i ) {
          CLINK *pXLink = devicecode->transferLinks[ i ] ;
          CLINK *pCLink = devicecode->calibrationLinks[ i ] ;
          if (pXLink != NULL && pXLink->idslot != NULL) {
            HQASSERT( CLID_SIZEtransfer == 3 , "fix the transfer CLID slots" ) ;
            HQASSERT( idslot[ 0 ] == 0 || pXLink->idslot[ 0 ] == 0 ||
                      idslot[ 0 ] == pXLink->idslot[ 0 ] , "idslot[ 0 ] out of phase" ) ;
            HQASSERT( idslot[ 1 ] == 0 || pXLink->idslot[ 1 ] == 0 ||
                      idslot[ 1 ] == pXLink->idslot[ 1 ] , "idslot[ 1 ] out of phase" ) ;
            HQASSERT( idslot[ 2 ] == 0 || pXLink->idslot[ 2 ] == 0 ||
                      idslot[ 2 ] == pXLink->idslot[ 2 ] , "idslot[ 2 ] out of phase" ) ;
            idslot[ 0 ] |= pXLink->idslot[ 0 ] ;
            idslot[ 1 ] |= pXLink->idslot[ 1 ] ;
            idslot[ 2 ] |= pXLink->idslot[ 2 ] ;
          }
          if (pCLink != NULL && pCLink->idslot != NULL) {
            HQASSERT( CLID_SIZEcalibration == 2 , "fix the calibration CLID slots" ) ;
            HQASSERT( idslot[ 3 ] == 0 || pCLink->idslot[ 0 ] == 0 ||
                      idslot[ 3 ] == pCLink->idslot[ 0 ] , "idslot[ 3 ] out of phase" ) ;
            HQASSERT( idslot[ 4 ] == 0 || pCLink->idslot[ 1 ] == 0 ||
                      idslot[ 4 ] == pCLink->idslot[ 1 ] , "idslot[ 4 ] out of phase" ) ;
            idslot[ 3 ] |= pCLink->idslot[ 0 ] ;
            idslot[ 4 ] |= pCLink->idslot[ 1 ] ;
          }
        }
        idslot[ 5 ] = (CLID) colorPageParams->transfer.id;
      }
      idslot[ 6 ] = (CLID) devicecode->spotno ;
      idslot[ 7 ] = (CLID) ((( devicecode->immediateRepro                                    ) ? 0x0001 : 0x0000 ) |
                            (( devicecode->tableBasedColor                       ) ? 0x0002 : 0x0000 ) |
                            (( devicecode->fBgFlags & RENDER_BACKGROUND          ) ? 0x0004 : 0x0000 ) |
                            (( devicecode->overprintsEnabled                     ) ? 0x0008 : 0x0000 ) |
                            (( colorType != GSC_IMAGE                            ) ? 0x0010 : 0x0000 ) |
                            (( colorType != GSC_SHFILL_INDEXED_BASE &&
                               colorType != GSC_SHFILL                           ) ? 0x0020 : 0x0000 ) |
                            (( colorType != GSC_VIGNETTE                         ) ? 0x0040 : 0x0000 ) |
                            (( colorType != GSC_BACKDROP                         ) ? 0x0080 : 0x0000 ) |
                            (( devicecode->devicecodeInfo->overprintMode         ) ? 0x0100 : 0x0000 ) |
                            (( devicecode->devicecodeInfo->overprintBlack        ) ? 0x0200 : 0x0000 ) |
                            (( devicecode->devicecodeInfo->overprintGray         ) ? 0x0800 : 0x0000 ) |
                            (( devicecode->devicecodeInfo->overprintGrayImages   ) ? 0x1000 : 0x0000 ) |
                            (( devicecode->devicecodeInfo->overprintWhite        ) ? 0x2000 : 0x0000 ) |
                            (( devicecode->allowImplicit                         ) ? 0x4000 : 0x0000 ) |
                            (( devicecode->fTransformedSpot                      ) ? 0x8000 : 0x0000 ) |
                            (( devicecode->overprintInfo.setoverprint            ) ? 0x10000 : 0x0000 ) |
                            (( devicecode->fCompositing                          ) ? 0x20000 : 0x0000 ) |
                            (( realDeviceTarget                                  ) ? 0x40000 : 0x0000 ));
      idslot[ 8 ] = (CLID) guc_rasterstyleId( hRasterStyle );
      idslot[ 9 ] = (CLID) guc_rasterstyleId( devicecode->hDeviceRasterStyle );
      idslot[10 ] = (CLID) devicecode->httype;
      idslot[11 ] = (CLID) devicecode->contoneMask.value;
    }
  }

  devicecodeAssertions( pLink ) ;

  *fApplyMaxBlts = devicecode->fApplyMaxBlts;

  return pLink;
}

/* ---------------------------------------------------------------------- */
static void devicecode_destroy( CLINK *pLink )
{
  int32 i ;
  int32 nColorants ;
  CLINKDEVICECODEinfo *devicecode ;

  devicecodeAssertions( pLink ) ;

  nColorants = pLink->n_iColorants ;
  devicecode = pLink->p.devicecode ;
  for ( i = 0 ; i < nColorants ; ++i ) {
    CLINK *pLink ;
    if ( devicecode->dciluts[ i ] )
      CLINK_RELEASE(&devicecode->dciluts[ i ], dcilut_free);
    pLink = devicecode->transferLinks[ i ] ;
    if ( pLink )
      (pLink->functions->destroy)( pLink ) ;
    pLink = devicecode->calibrationLinks[ i ] ;
    if ( pLink )
      (pLink->functions->destroy)( pLink ) ;
  }

  if (oType(devicecode->devicenCSA[1]) == OARRAY) {
    HQASSERT(oType(devicecode->devicenCSA[0]) == ONAME &&
             oName(devicecode->devicenCSA[0]) == system_names + NAME_DeviceN,
             "Corrupt output DeviceN CSA");
    mm_free_with_header(mm_pool_color, oArray(devicecode->devicenCSA[1]));
  }
  if ( devicecode->transfer.cached && devicecode->transfer.func.cpsc )
    destroy_callpscache(&devicecode->transfer.func.cpsc);

  cc_common_destroy( pLink ) ;
}

/* ---------------------------------------------------------------------- */

/* A macro to do fast float to int conversion (with rounding) on Windows. Its
 * main purpose is to avoid the slow ftol conversion on Windows.  It assumes
 * input values are 0.0-1.0 range.
 * DO NOT MESS WITH THIS BLOCK UNLESS YOU HAVE DONE PERMORMANCE TESTING. The
 * performance of the conversion is extremely sensitive.
 */
#ifdef WIN32
/* Shift the mantissa into the second 16bit word of the double.
   1 << 20, plus 0.5 for rounding. */
#define MANTISSA_SHIFT      (1048576.5)
#define MAX_VALUES_PER_LOOP         (4)

#define N_FLOAT_TO_COLORVALUE(float_, cv_, n_) MACRO_START              \
  int32 n = (n_);                                                       \
  int32 done ;                                                          \
  float *fArray = (float_);                                             \
  float scale = COLORVALUE_ONE;                                         \
  uint16 *iArray = (cv_);                                               \
  double tmp[MAX_VALUES_PER_LOOP];                                      \
                                                                        \
  HQASSERT(scale <= MAXUINT16, "N_FLOAT_TO_COLORVALUE optimisation fails") ; \
  do {                                                                  \
    HQASSERT(n >= 1, "num colorants too small");                        \
                                                                        \
    if ( n == 1 ) {                                                     \
      tmp[0] = fArray[0] * scale + MANTISSA_SHIFT ;                     \
      (iArray)[0] = (uint16)(((uint32 *)&tmp[0])[1]) ;                  \
      HQASSERT((iArray)[0] == (uint16)(fArray[0] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      done = 1 ;                                                        \
    } else if ( n == 2 ) {                                              \
      tmp[1] = fArray[1] * scale + MANTISSA_SHIFT ;                     \
      tmp[0] = fArray[0] * scale + MANTISSA_SHIFT ;                     \
      (iArray)[1] = (uint16)(((uint32 *)&tmp[1])[1]) ;                  \
      (iArray)[0] = (uint16)(((uint32 *)&tmp[0])[1]) ;                  \
      HQASSERT((iArray)[1] == (uint16)(fArray[1] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      HQASSERT((iArray)[0] == (uint16)(fArray[0] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      done = 2 ;                                                        \
    } else if ( n == 3 ) {                                              \
      tmp[2] = fArray[2] * scale + MANTISSA_SHIFT ;                     \
      tmp[1] = fArray[1] * scale + MANTISSA_SHIFT ;                     \
      tmp[0] = fArray[0] * scale + MANTISSA_SHIFT ;                     \
      (iArray)[2] = (uint16)(((uint32 *)&tmp[2])[1]) ;                  \
      (iArray)[1] = (uint16)(((uint32 *)&tmp[1])[1]) ;                  \
      (iArray)[0] = (uint16)(((uint32 *)&tmp[0])[1]) ;                  \
      HQASSERT((iArray)[2] == (uint16)(fArray[2] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      HQASSERT((iArray)[1] == (uint16)(fArray[1] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      HQASSERT((iArray)[0] == (uint16)(fArray[0] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      done = 3 ;                                                        \
    } else { /* 4 or more, but only deal with 4. */                     \
      tmp[3] = fArray[3] * scale + MANTISSA_SHIFT ;                     \
      tmp[2] = fArray[2] * scale + MANTISSA_SHIFT ;                     \
      tmp[1] = fArray[1] * scale + MANTISSA_SHIFT ;                     \
      tmp[0] = fArray[0] * scale + MANTISSA_SHIFT ;                     \
      (iArray)[3] = (uint16)(((uint32 *)&tmp[3])[1]) ;                  \
      (iArray)[2] = (uint16)(((uint32 *)&tmp[2])[1]) ;                  \
      (iArray)[1] = (uint16)(((uint32 *)&tmp[1])[1]) ;                  \
      (iArray)[0] = (uint16)(((uint32 *)&tmp[0])[1]) ;                  \
      HQASSERT((iArray)[3] == (uint16)(fArray[3] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      HQASSERT((iArray)[2] == (uint16)(fArray[2] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      HQASSERT((iArray)[1] == (uint16)(fArray[1] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      HQASSERT((iArray)[0] == (uint16)(fArray[0] * scale + 0.5f), "Fast float to CV conversion failed") ; \
      done = 4 ;                                                        \
    }                                                                   \
                                                                        \
    n -= done;                                                          \
    fArray += done;                                                     \
    iArray += done;                                                     \
  } while (n > 0);                                                      \
  MACRO_END
#else
#define N_FLOAT_TO_COLORVALUE(float_, cv_, n_) MACRO_START              \
  int32 i;                                                              \
  for ( i = 0 ; i < (n_) ; ++i ) {                                      \
    (cv_)[i] = FLOAT_TO_COLORVALUE((float_)[i]);                        \
  }                                                                     \
MACRO_END

#endif

static Bool dci_lookup_colorSlow(CLINK        *pLink,
                                 USERVALUE    *iColorValues,
                                 Bool         useCache,
                                 COLORVALUE   *oColorValues)
{
  int32               i;
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  COLORVALUE          *cFF00 = devicecode->cFF00;
  uint16              *cacheHash = devicecode->cacheHash;
  Bool                cacheHit;
  Bool                simpleColorantOrder = devicecode->simpleColorantOrder;

  HQASSERT(pLink->n_iColorants > 0, "number of colorants is too small") ;
  HQASSERT(!useCache || !devicecode->simpleColorantOrder ||
           !devicecode->tableBasedColor,
           "Using the slow color lookup but should be using the fast method");

  for ( i = 0 ; i < pLink->n_iColorants ; ++i )
    COLOR_01_ASSERT( iColorValues[i], "device code input" ) ;

  /* To avoid pain on Windows with slow ftol conversions, we will convert the input
   * colour values once and do all remaining conversions in fixed point arithmetic
   * wherever possible.
   */
  N_FLOAT_TO_COLORVALUE(iColorValues, cFF00, pLink->n_iColorants) ;

  cacheHit = useCache;

  if (useCache) {
    for (i = 0; i < pLink->n_iColorants; i++) {
      uint16 hash;

      /* DO NOT MESS WITH THIS BLOCK UNLESS YOU HAVE DONE PERMORMANCE TESTING.
       * Performance is extremely delicate and will suffer if anything is changed,
       * at least on Windows with Intel CoreDuo processor.
       */
      hash = cFF00[i] >> (8 * sizeof(cFF00[0]) - CACHEHASHBITS);

      if (cFF00[i] == devicecode->cacheInput[i][hash]) {
        DEBUG_INCREMENT_ARRAY(devicecode->channelHits, i);
      }
      else {
        DEBUG_INCREMENT_ARRAY(devicecode->channelMisses, i);
        cacheHit = FALSE ;
      }

      cacheHash[i] = hash;
    }

    if (cacheHit) {
      DEBUG_INCREMENT(devicecode->cacheHits);

      if (simpleColorantOrder) {
        /* We have simpleInputColorantOrder and a cacheHit. This allows a quick
         * bail out. */
        for (i = 0; i < pLink->n_iColorants; i++) {
          oColorValues[i] = devicecode->cacheOutput[i][cacheHash[i]];
          HQASSERT(oColorValues[i] <= COLORVALUE_MAX, "Not a colorvalue");
        }

        return TRUE;
      }
    }
    else
      DEBUG_INCREMENT(devicecode->cacheMisses);
  }

  if (cacheHit) {
    /* We've got a cache hit, but not with simpleColorantOrder, so we must
     * continue to to do the sorting below.
     */
    if (devicecode->photoinkInfo == NULL) {
      for (i = 0; i < pLink->n_iColorants; i++)
        devicecode->oColorValues[i] = devicecode->cacheOutput[i][cacheHash[i]];
    }
    else {
      uint32 dev_i = 0;
      for (i = 0 ; i < pLink->n_iColorants; i++) {
        uint32 j;
        for (j = 0; j < devicecode->dciluts[i]->nMappedColorants; j++) {
          devicecode->oColorValues[dev_i] = devicecode->cacheOutput[dev_i][cacheHash[i]];
          dev_i++;
        }
      }
    }
  }
  else {

    if (useCache) {
      for (i = 0; i < pLink->n_iColorants; i++)
        devicecode->cacheInput[i][cacheHash[i]] = cFF00[i];
    }

    if (devicecode->tableBasedColor) {
      for ( i = 0 ; i < pLink->n_iColorants ; i++ ) {
        if ( !dci_invoke(pLink, cFF00, i, devicecode->oColorValues))
          return FALSE ;
      }
    }
    else {
      for ( i = 0 ; i < pLink->n_iColorants ; i++ ) {
        if (!dci_invokexfercal(devicecode, i, iColorValues[i],
                               &devicecode->oColorValues[i]))
          return FALSE ;
      }
    }

    if (devicecode->photoinkInfo != NULL) {
      COLORVALUE *oCV = devicecode->oColorValues;

      /* devicecode->oColorValues are the output of dci_invoke. We wish for the
       * output of the photoink interpolate to be in the same array, hence the
       * copy into tmpColorValues here.
       */
      for (i = 0; i < pLink->n_iColorants; i++)
        devicecode->tmpColorValues[i] = devicecode->oColorValues[i];

      /* Do the conversion of data from the standard colorants to the photoink colorants */
      for ( i = 0; i < pLink->n_iColorants; i++ ) {
        if ( devicecode->colorantMaps[i] != NULL ) {    /* NULL for None */
          /* Apply the cached custom transforms for all mapped output colorants */
          if (! guc_interpolatePhotoinkTransform(devicecode->photoinkInfo,
                                                 devicecode->colorantMaps[i],
                                                 devicecode->nMappedColorants[i],
                                                 devicecode->tmpColorValues[i],
                                                 oCV ) )
            return FALSE ;

          oCV += devicecode->nMappedColorants[i];
        }
      }
    }
    else
      HQASSERT(pLink->n_iColorants == devicecode->nDeviceColorants,
               "Photoink assumption broken");

    /* Check for ContoneMask and remap any color values that will be clear after
       quantisation to device codes. */
    if ( devicecode->contoneMask.value != 0 ) {
      for ( i = 0; i < devicecode->nDeviceColorants; ++i ) {
        if ( devicecode->oColorValues[i] >= devicecode->contoneMask.threshold )
          devicecode->oColorValues[i] = devicecode->contoneMask.replacement;
      }
    }

    /* Now put the output values into the dcilut_cache */
    if (useCache) {
      if (devicecode->photoinkInfo == NULL) {
        for (i = 0; i < pLink->n_iColorants; i++)
          devicecode->cacheOutput[i][cacheHash[i]] = devicecode->oColorValues[i];
      }
      else {
        uint32 dev_i = 0;
        for (i = 0; i < pLink->n_iColorants; i++) {
          uint32 j;
          for (j = 0; j < devicecode->dciluts[i]->nMappedColorants; j++) {
            devicecode->cacheOutput[dev_i][cacheHash[i]] = devicecode->oColorValues[dev_i];
            dev_i++;
          }
        }
      }
    }
  }

  /* devicecode->oColorValues must now be sorted according to sortingIndexes and
   * copied into oColorValues.
   */
  if (simpleColorantOrder) {
    for (i = 0; i < devicecode->nDeviceColorants; i++) {
      oColorValues[i] = devicecode->oColorValues[i];
      HQASSERT(oColorValues[i] <= COLORVALUE_MAX, "Not a colorvalue") ;
    }
  }
  else {
    int32     nSorted ;

    /* Now rearrange the devicecodes according to the order of the sorted
     * colorants because this matches the order returned by deviceGetOutputColors.
     */
    for ( i = nSorted = 0 ; i < devicecode->nDeviceColorants ; ++i ) {
      int32 nOriginalIndex = devicecode->sortingIndexes[ i ] ;

      if (nOriginalIndex >= 0) {
        /* note: negative sorting indexes arise if we have the same named colorant more
           than once in a DeviceN color space; only one of them actually takes effect,
           hence the omission */
        oColorValues[nSorted] = devicecode->oColorValues[nOriginalIndex] ;
        HQASSERT(oColorValues[nSorted] <= COLORVALUE_MAX, "Not a colorvalue") ;

        nSorted++;
      }
    }
    HQASSERT(nSorted == devicecode->nSortedColorants, "nSorted is out of step") ;
  }

  return TRUE;
}

static Bool dci_lookup_colorFast(CLINK        *pLink,
                                 USERVALUE    *iColorValues,
                                 Bool         useCache,
                                 COLORVALUE   *oColorValues)
{
  int32               i;
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  COLORVALUE          *cFF00 = devicecode->cFF00;

  UNUSED_PARAM(Bool, useCache);

  HQASSERT(pLink->n_iColorants > 0, "number of colorants is too small") ;
  HQASSERT(useCache, "Fast color lookup requires a lut cache");
  HQASSERT(devicecode->simpleColorantOrder,
           "Fast color lookup requires a simple colorant order");
  HQASSERT(devicecode->photoinkInfo == NULL,
           "Can't use fast color lookup for photoink");
  HQASSERT(devicecode->tableBasedColor,
           "devicecode->tableBasedColor isn't true");

  for ( i = 0 ; i < pLink->n_iColorants ; ++i )
    COLOR_01_ASSERT( iColorValues[i], "device code input" ) ;

  /* To avoid pain on Windows with slow ftol conversions, we will convert the input
   * colour values once and do all remaining conversions in fixed point arithmetic
   * wherever possible.
   */
  N_FLOAT_TO_COLORVALUE(iColorValues, cFF00, pLink->n_iColorants) ;

  for (i = 0; i < pLink->n_iColorants; i++) {
    uint16 hash;

    hash = cFF00[i] >> (8 * sizeof(cFF00[0]) - CACHEHASHBITS);

    /* DO NOT MESS WITH THIS BLOCK UNLESS YOU HAVE DONE PERMORMANCE TESTING.
     * Performance is extremely delicate and will suffer if anything is changed,
     * at least on Windows with Intel CoreDuo processor.
     */

    if (cFF00[i] == devicecode->cacheInput[i][hash]) {
      DEBUG_INCREMENT_ARRAY(devicecode->channelHits, i);

      oColorValues[i] = devicecode->cacheOutput[i][hash];
    }
    else {
      DEBUG_INCREMENT_ARRAY(devicecode->channelMisses, i);

      if (!dci_invoke(pLink, cFF00, i, oColorValues))
        return FALSE ;

      /* Check for ContoneMask and remap any color values that will be clear after
         quantisation to device codes. */
      if ( devicecode->contoneMask.value != 0 &&
           oColorValues[i] >= devicecode->contoneMask.threshold )
        oColorValues[i] = devicecode->contoneMask.replacement;

      devicecode->cacheInput[i][hash] = cFF00[i];
      devicecode->cacheOutput[i][hash] = oColorValues[i];
    }

    HQASSERT(oColorValues[i] <= COLORVALUE_MAX, "Not a colorvalue") ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool dci_invokexfercal( CLINKDEVICECODEinfo *devicecode ,
                               int32               channel ,
                               USERVALUE           ival ,
                               COLORVALUE          *oval )
{
  if (devicecode->transferLinks[channel] != NULL) {
    CLINK* pLinkXfer = devicecode->transferLinks[channel];
    pLinkXfer->iColorValues[0] = ival;
    if (! (pLinkXfer->functions->invokeSingle)(pLinkXfer, & ival))
      return FALSE;
  }

  if (devicecode->calibrationLinks[channel] != NULL) {
    CLINK* pLinkCal = devicecode->calibrationLinks[channel];
    pLinkCal->iColorValues[0] = ival;
    if (! (pLinkCal->functions->invokeSingle)(pLinkCal, & ival))
      return FALSE;

    /* Now deal with any user transfer function */
    if ( devicecode->transfer.cached ) {
      /* PS procedure must have been pre-cached to avoid interpreter calls
         during rendering */
      if ( devicecode->transfer.func.cpsc ) {
        lookup_callpscache(devicecode->transfer.func.cpsc, ival, &ival);
      }
      /* else a zero-length func, which is a no-op */
    } else {
      /* literal PS array used as lookup - this is OK at render time */
      OBJECT *xfer = &devicecode->transfer.func.psfunc;
      HQASSERT(theLen(*xfer) > 0 && !oExecutable(*xfer),
               "Unexpected user param xfer");
      if ( !cc_applyCalibrationInterpolation(ival, &ival, xfer) )
        return FALSE;
    }
  }

  /* Halftone quantisation is now done in the back end,
     unless we are using an image chain. */
  *oval = FLOAT_TO_COLORVALUE(ival);

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static Bool dci_invoke(CLINK       *pLink,
                       COLORVALUE  *cFF00,
                       int32       channel,
                       COLORVALUE  *oDeviceCodes)
{
  CLINKDEVICECODEinfo  *devicecode = pLink->p.devicecode ;
  DCILUT               **dciluts = devicecode->dciluts ;
  int32                c255i;
  int32                fraction;
  int32 lo;
  int32 c255i_lo;
  DCILUT *dcilut;

  HQASSERT( dciluts != NULL , "got some colorants but dciluts NULL" ) ;

  fraction = cFF00[channel] & 0xFF;
  c255i = cFF00[channel] >> 8;
  c255i_lo = c255i;

  dcilut = dciluts[ channel ] ;
  HQASSERT( dcilut , "got some colorants but dcilut NULL" ) ;
  lo = dcilut->dciCodes[ c255i_lo ] ;
  if ( lo == DCILUT_CODE_notset ) {
    /* The value isn't in the table yet, so go compute & populate it. */
    if (! dci_invokexfercal( devicecode ,
                             channel ,
                             c255i_lo / 255.0f ,
                             &dcilut->dciCodes[ c255i_lo ] ))
      return FALSE ;
    lo = dcilut->dciCodes[ c255i_lo ] ;
  }

  /* If fraction is zero, we're at a grid-point, and don't need the next value
   * up. This also protects against trying to address off the end of the table.
   */
  if ( fraction == 0 ) {
    oDeviceCodes[ channel ] = CAST_TO_COLORVALUE(lo) ;
  }
  else {
    int32 hi ;
    int32 c255i_hi = c255i + 1 ;
    HQASSERT( c255i_hi < 256 , "somehow got out of range" ) ;

    /* Get the value out of the table */
    hi = dcilut->dciCodes[ c255i_hi ] ;
    if ( hi == DCILUT_CODE_notset ) {
      /* The value isn't in the table yet, so go compute & populate it. */
      if (! dci_invokexfercal( devicecode ,
                               channel ,
                               c255i_hi / 255.0f ,
                               &dcilut->dciCodes[ c255i_hi ] ))
        return FALSE ;
      hi = dcilut->dciCodes[ c255i_hi ] ;
    }
    /* Scale the numbers for the fraction bits */
    hi = hi * fraction;
    lo = lo * ( 256 - fraction) ;
    /* Finally compute the values */
    oDeviceCodes[ channel ] = CAST_TO_COLORVALUE(( lo + hi + 128 ) >> 8) ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool devicecode_invokeSingle( CLINK *pLink , USERVALUE *dummy_oColorValue )
{
  int32 i;
  int32 nSorted ;
  int32 nReduced = 0;
  uint8 flags ;
  int32 nColorants ;
  int32 nDeviceColorants ;
  CLINKDEVICECODEinfo *devicecode ;
  Bool doOverprintReduction ;
  Bool useCache ;
  LOOKUP_COLOR_FN *lookupColor ;
  dlc_context_t *dlc_context ;
  dl_color_t *dlc_current ;

  UNUSED_PARAM( USERVALUE * , dummy_oColorValue ) ;

  HQASSERT( dummy_oColorValue == NULL , "dummy_oColorValue != NULL" ) ;

  devicecodeAssertions( pLink ) ;
  devicecode = pLink->p.devicecode ;

  dlc_context = devicecode->page->dlc_context ;
  dlc_current = dlc_currentcolor(dlc_context) ;

  /* Don't do separation detection at the back end */
  if (!devicecode->fCompositing) {
    if ( new_color_detected ) {
      if (!deviceCheckSetColor(pLink))
        return FALSE;
    }
    if ( new_screen_detected ) {
      if ( !deviceCheckSetScreen (devicecode) ) {
        return FALSE;
      }
    }
  }

  nColorants = pLink->n_iColorants ;
  nDeviceColorants = devicecode->nDeviceColorants ;

  /* useCache could be either value. If it's false, lookupColor needs to be the
   * slow version because the fast version requires it to be true.
   */
  useCache = devicecode->useDCILUTnormalCache;
  if (useCache)
    lookupColor = devicecode->lookupColor;
  else
    lookupColor = dci_lookup_colorSlow;

  for ( i = 0 ; i < nColorants ; ++i )
    COLOR_01_ASSERT( pLink->iColorValues[i], "device code input" ) ;

  /* Calculate overprints. */
  flags = devicecode->fBgFlags ;
  if ( op_decide_overprints( pLink,
                             pLink->iColorValues ,
                             pLink->overprintProcess ,
                             &devicecode->overprintInfo ,
                             &doOverprintReduction ))
    flags &= ~RENDER_KNOCKOUT ;

  /* don't reduce colorants if we're intercepting - leave overprinting of explicit
     colors to the nonintercept link below */
  doOverprintReduction = ( doOverprintReduction && !devicecode->fApplyMaxBlts ) ;

  /* Use device code interpolator to produce Device Codes. */
  if (pLink->n_iColorants > 0 &&
      !(*lookupColor)(pLink,
                      pLink->iColorValues,
                      useCache,
                      devicecode->sortedColorValues))
    return FALSE ;

  /* Populate the halftone cache with the results. NB. The use of tmpColorValues
   * is there to potentially hold quantised values internal to some of the
   * updateHTCache* functions.
   */
  /* May be updated in updateHTCacheForPatternContone */
  dl_set_currentspflags(dlc_context, flags) ;
  (*devicecode->updateHTCacheFunction)( pLink , 1 ,
                                        devicecode->sortedColorValues,
                                        devicecode->tmpColorValues ) ;
  if ( devicecode->patternPaintType != NO_PATTERN )
    dl_set_currentspflags(dlc_context, RENDER_PATTERN) ;

  /* Reduce the resultant colors based on overprints. We must pass
   * sorted arrays of colorants and colorvalues to dlc_alloc_fillin. The
   * colorants have been pre-sorted but the colorvalues must be re-arranged
   * now.
   */
  devicecode->nReducedColorants = 0 ; /* 0 if ! doOverprintReduction, meaning unused */
  if ( doOverprintReduction ) {
    for ( i = nSorted = nReduced = 0 ; i < nDeviceColorants ; ++i ) {
      int32 nOriginalIndex = devicecode->sortingIndexes[ i ] ;

      if (nOriginalIndex >= 0) {
        if ( PAINT_COLORANT( devicecode->overprintMask , nOriginalIndex )) {
          devicecode->reducedColorants[nReduced]   = devicecode->sortedColorants[nSorted] ;
          devicecode->reducedColorValues[nReduced] = devicecode->sortedColorValues[nSorted] ;
          nReduced++;
        } else if ( !devicecode->fTransformedSpot  &&
                    ( devicecode->colorType == GSC_SHFILL ||
                      devicecode->colorType == GSC_SHFILL_INDEXED_BASE ||
                      devicecode->colorType == GSC_VIGNETTE )) {
          /* Do not reduce overprints for shfills & vignettes as these
             are required to have a common set of colorants.  Instead, the
             overprint is indicated by use of COLORVALUE_TRANSPARENT. (If
             there's an intercept then we'll use max-blts and
             doOverprintReduction will be false.) */
          devicecode->sortedColorValues[ nSorted ] = COLORVALUE_TRANSPARENT ;

          /* Can now switch off overprint reduction - overprinted colorants are set to
             COLORVALUE_TRANSPARENT instead. */
          doOverprintReduction = FALSE ;
        }
        nSorted++;
      }
    }
    HQASSERT(nSorted == devicecode->nSortedColorants, "nSorted is out of step") ;

    if ( doOverprintReduction && nReduced == 0 ) {
      /* overprinting everything, send to none sep to remove object. */
      devicecode->reducedColorants [ 0 ] = COLORANTINDEX_NONE ;
      nReduced = 1 ;
    }
    devicecode->nReducedColorants = nReduced ;
  }
  else
    nSorted = devicecode->nSortedColorants;


  devicecode->overprintFlags = flags ;

  /* Create a dl color object. */

  dlc_release(dlc_context, dlc_current) ;

  {
    COLORANTINDEX *sortedColorants = devicecode->sortedColorants ;
    COLORANTINDEX *reducedColorants = devicecode->reducedColorants ;
    COLORVALUE *sortedColorValues = devicecode->sortedColorValues ;
    COLORVALUE *reducedColorValues = devicecode->reducedColorValues ;

    if ( pLink->iColorSpace == SPACE_Pattern &&
         devicecode->patternPaintType == NO_PATTERN ) {
      dlc_get_none(dlc_context, dlc_current) ;
    } else if ( pLink->iColorSpace == SPACE_Pattern &&
                devicecode->patternPaintType != UNCOLOURED_PATTERN ) {
      dlc_get_black(dlc_context, dlc_current) ;
    } else if ( nSorted == 0 ) {
      dlc_get_black(dlc_context, dlc_current) ;
    } else if ( doOverprintReduction ) {
      if ( devicecode->fTransformedSpot ) {
        COLORANTINDEX * overprintColorants;
        int32 nOverprinted;

        HQASSERT(!devicecode->fApplyMaxBlts,
                 "Color managed chains should maxblit via nonintercept chains");

        /* produce a color which, despite overprinting, contains all the colorants, but
           is then augmented with information about which are overprinted. This is so
           that we can produce a consistent set of overprints for all the colors in a
           shaded fill or vignette, and not overprint a single anomalous zero in the
           middle of an overprinted vignette */

        HQASSERT( nSorted >= 1 , "devicecode_invokeSingle: nSorted < 1" ) ;

        if ( !dlc_alloc_fillin(dlc_context,
                               nSorted,
                               sortedColorants,
                               sortedColorValues,
                               dlc_current) )
          return FALSE;

        /* Note: if we are intercepting, we don't get here. Overprint information is
           instead constructed later on via the alternative simple chain generated off
           the head link. */
        if ( devicecode->reducedColorants [ 0 ] == COLORANTINDEX_NONE ) {
          /* Overprinting all the colorants */
          overprintColorants = sortedColorants;
          nOverprinted = nSorted;
        }
        else {
          /* We need the set of overprinted colorants here, not the reduced ones, but
             fortunately reducedColorants is big enough for both, so we have the memory
             in which to compute that set */
          for (i = nReduced = 0; i < nSorted; i++) {
            if (nReduced >= devicecode->nReducedColorants ||
                devicecode->reducedColorants [nReduced] != devicecode->sortedColorants [i])
              devicecode->reducedColorants [devicecode->nReducedColorants + i - nReduced] =
                devicecode->sortedColorants [i];
            else
              nReduced++;
          }
          HQASSERT(nReduced == devicecode->nReducedColorants, "nReduced != nReducedColorants");

          overprintColorants = devicecode->reducedColorants + devicecode->nReducedColorants;
          nOverprinted = nSorted - nReduced;
        }

        if ( !dlc_apply_overprints(dlc_context,
                                   DLC_MAXBLT_OVERPRINTS,
                                   DLC_INTERSECT_OP,
                                   nOverprinted,
                                   overprintColorants,
                                   dlc_current) )
          return FALSE;

      } else {

        /* the simple overprinting case - produce a color which contains only references to the
           reduced colorants and their values */

        if ( !dlc_alloc_fillin(dlc_context,
                               nReduced,
                               reducedColorants,
                               reducedColorValues,
                               dlc_current) )
          return FALSE;
      }

    } else {
      /* the more usual case - produce a color which contains only references to the
         sorted colorants and their values */

      if ( !dlc_alloc_fillin(dlc_context,
                             nSorted,
                             sortedColorants,
                             sortedColorValues,
                             dlc_current) )
        return FALSE;
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool devicecode_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  int32     nColorants ;
  int32     nDeviceColorants ;
  int32     nSortedColorants ;
  DCILUT    **dciluts ;
  CLINKDEVICECODEinfo *devicecode ;
  GS_BLOCKoverprint *blockOverprint ;

  int32     nColor = 0 ;
  USERVALUE *colorValues ;
  COLORVALUE *deviceCodes ;
  uint8     *overprintProcess ;
  Bool      doOverprintReduction = FALSE ;
  Bool      useCache ;
  LOOKUP_COLOR_FN *lookupColor ;

  devicecodeAssertions( pLink ) ;
  devicecode = pLink->p.devicecode ;

  /* Use device code interpolator to produce Device Codes. */
  dciluts = devicecode->dciluts ;
  nColorants = pLink->n_iColorants ;
  nDeviceColorants = devicecode->nDeviceColorants ;
  nSortedColorants = devicecode->nSortedColorants ;
  HQASSERT(nColorants <= nDeviceColorants,
           "photoink output incompatible with input");

  colorValues = pBlock->iColorValues;
  deviceCodes = pBlock->deviceCodes;
  overprintProcess = pBlock->overprintProcess;

  HQASSERT(colorValues != NULL, "colorValues is NULL");
  HQASSERT(deviceCodes != NULL, "deviceCodes is NULL");
  HQASSERT(overprintProcess != NULL, "overprintProcess is NULL");
  HQASSERT(pBlock->nColors <= GSC_BLOCK_MAXCOLORS, "nColors too big");
  /* N.B. Use of PoorShading can result in a GSC_SHFILL type */
  HQASSERT(devicecode->colorType == GSC_IMAGE ||
           devicecode->colorType == GSC_SHFILL ||
           devicecode->colorType == GSC_BACKDROP,
           "Unexpected chain type in device invoke block");

  blockOverprint = pBlock->blockOverprint ;

  useCache = devicecode->useDCILUTnormalCache;

  /* useCache could be either value. If it's false, lookupColor needs to be the
   * slow version because the fast version requires it to be true.
   */
  if (useCache)
    lookupColor = devicecode->lookupColor;
  else
    lookupColor = dci_lookup_colorSlow;


  for (nColor = 0; nColor < pBlock->nColors; nColor++) {
    if ( blockOverprint ) {
      if ( blockOverprint->overprintAll ) {

        /* The result of this call to op_decide_overprints is used to determine
           whether all of the colours in a block are overprinted, even if the
           nonintercept chain is called to set the actual overprint masks. We
           can't really short-circuit this much, because we know that the
           systemparam and some other overprint flags are true or we wouldn't
           be here in the first place. */
        if ( op_decide_overprints(pLink ,
                                  colorValues ,
                                  overprintProcess[0],
                                  &devicecode->overprintInfo ,
                                  & doOverprintReduction ) ) {
          /* Overprinting, so leave overprintAll flag set */
          /* Only calculate overprint intersection if applying maxblts. If
             so, we wait until the nonintercept block invoke is
             called to calculate overprints. */
          if ( doOverprintReduction && !devicecode->fApplyMaxBlts ) {
            /* This sets blockOverprint->overprintAll to FALSE if there are no
               more overprints, and TRUE if there are, which is OK, because we
               wouldn't be here if it wasn't TRUE beforehand. */
            INTERSECT_OVERPRINTMASK(blockOverprint->overprintMask,
                                    devicecode->overprintMask,
                                    nDeviceColorants,
                                    blockOverprint->overprintAll) ;
            blockOverprint->overprintUseMask = TRUE ;
          }
        } else /* not overprinting, clear overprintAll flag */
          blockOverprint->overprintAll = FALSE ;
      }
    }

    if (pLink->n_iColorants > 0 &&
        !(*lookupColor)(pLink,
                        colorValues,
                        useCache,
                        deviceCodes))
      return FALSE ;

    colorValues += nColorants ;
    deviceCodes += nSortedColorants ;
    overprintProcess++ ;
  }

  /* Populate the halftone cache with the results. */
  deviceCodes = pBlock->deviceCodes ;
  (*devicecode->updateHTCacheFunction)( pLink, pBlock->nColors, NULL, deviceCodes ) ;

  return TRUE ;
}


/* ---------------------------------------------------------------------- */
/* devicecode_scan - scan the device code section of a CLINK */
static mps_res_t devicecode_scan( mps_ss_t ss, CLINK *pLink )
{
  int32 i;
  mps_res_t res;

  /* Don't scan pLink->p.devicecode->pHeadLink because it will lead to an
   * infinite loop
   */

  for (i = 0; i < pLink->n_iColorants; i++) {
    if ( pLink->p.devicecode->transferLinks[i] != NULL ) {
      res = cc_scan( ss, pLink->p.devicecode->transferLinks[i] );
      if ( res != MPS_RES_OK )
        return res;
    }
    if ( pLink->p.devicecode->calibrationLinks[i] != NULL ) {
      res = cc_scan( ss, pLink->p.devicecode->calibrationLinks[i] );
      if ( res != MPS_RES_OK )
        return res;
    }
  }
  /* Dont scan cc_scan_halftone() because it's already done in gscscan() */

  res = ps_scan_field( ss, &pLink->p.devicecode->colorSpaceObj);
  return res;
}


/* ---------------------------------------------------------------------- */
Bool cc_devicecode_populate( CLINK *pLink , CLINKblock *pBlock )
{
  CLINKDEVICECODEinfo *devicecode ;

  int32       nColors ;
  COLORVALUE  *deviceCodes ;

  devicecodeAssertions( pLink ) ;
  devicecode = pLink->p.devicecode ;

  /* Populate the halftone cache with the results. */
  nColors = pBlock->nColors ;
  deviceCodes = pBlock->deviceCodes ;
  (*devicecode->updateHTCacheFunction)( pLink , nColors , NULL , deviceCodes ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static size_t devicecodeStructSize( int32 nColorants, int32 nDeviceColorants )
{
  return sizeof( CLINKDEVICECODEinfo ) +
         OVERPRINTMASK_BYTESIZE(nDeviceColorants) +       /* overprintMask */
         nColorants       * sizeof( int32 ) +             /* colorantsInSpotlist */
         nColorants       * sizeof( CLINK * ) +           /* transferLinks */
         nColorants       * sizeof( CLINK * ) +           /* calibrationLinks */
         nColorants       * sizeof( uint32 ) +            /* nMappedColorants */
         nColorants       * sizeof( COLORANTINDEX * ) +   /* colorantMaps */
         nDeviceColorants * sizeof( COLORVALUE ) +        /* oColorValues */
         nDeviceColorants * sizeof( COLORANTINDEX ) +     /* oColorants */
         nDeviceColorants * sizeof( COLORVALUE ) +        /* tmpColorValues */
         nDeviceColorants * sizeof( HT_TRANSFORM_INFO ) + /* sorted_htTransformInfo */
         nDeviceColorants * sizeof( int32 ) +             /* sortingIndexes */
         nDeviceColorants * sizeof( COLORANTINDEX ) +     /* sortedColorants */
         nDeviceColorants * sizeof( COLORVALUE ) +        /* sortedColorValues */
         nDeviceColorants * sizeof( COLORANTINDEX ) +     /* reducedColorants */
         nDeviceColorants * sizeof( COLORVALUE ) +        /* reducedColorValues */
         nColorants       * sizeof( DCILUT * ) +          /* dciluts */
         nColorants       * sizeof( COLORVALUE * ) +      /* cacheInput */
         nDeviceColorants * sizeof( COLORVALUE * ) +      /* cacheOutput */
         nColorants       * sizeof( COLORVALUE ) +        /* cFF00 */
         nColorants       * sizeof( uint16 );             /* cacheHash */
}

/* ---------------------------------------------------------------------- */
static void devicecodeUpdatePtrs( CLINK *pLink , int32 nColorants , int32 nDeviceColorants )
{
  CLINKDEVICECODEinfo *devicecode ;

  HQASSERT( pLink , "pLink NULL in devicecodeUpdatePtrs" ) ;

  pLink->p.devicecode = ( CLINKDEVICECODEinfo * )
    (( uint8 * )pLink + cc_commonStructSize( pLink )) ;

  devicecode = pLink->p.devicecode ;

  if ( nColorants > 0 ) {
    /* Allocate with the biggest things first to avoid alignment problems */
    devicecode->overprintMask         = ( uint32 * )( devicecode + 1 ) ;
    devicecode->colorantsInSpotlist   = ( int32 * )( devicecode->overprintMask + OVERPRINTMASK_WORDSIZE( nDeviceColorants ) ) ;
    devicecode->transferLinks         = ( CLINK ** )( devicecode->colorantsInSpotlist + nColorants ) ;
    devicecode->calibrationLinks      = ( CLINK ** )( devicecode->transferLinks + nColorants ) ;
    devicecode->nMappedColorants      = ( uint32 * )( devicecode->calibrationLinks + nColorants ) ;
    devicecode->colorantMaps          = ( COLORANTINDEX ** )( devicecode->nMappedColorants + nColorants ) ;
    devicecode->oColorants            = ( COLORANTINDEX * )( devicecode->colorantMaps + nColorants ) ;
    devicecode->sorted_htTransformInfo= ( HT_TRANSFORM_INFO * )( devicecode->oColorants + nDeviceColorants ) ;
    devicecode->sortingIndexes        = ( int32 * )( devicecode->sorted_htTransformInfo + nDeviceColorants ) ;
    devicecode->sortedColorants       = ( COLORANTINDEX * )( devicecode->sortingIndexes + nDeviceColorants ) ;
    devicecode->reducedColorants      = ( COLORANTINDEX * )( devicecode->sortedColorants + nDeviceColorants ) ;
    devicecode->dciluts               = ( DCILUT ** )( devicecode->reducedColorants + nDeviceColorants ) ;
    devicecode->cacheInput            = ( COLORVALUE ** )( devicecode->dciluts + nColorants ) ;
    devicecode->cacheOutput           = ( COLORVALUE ** )( devicecode->cacheInput + nColorants ) ;
    devicecode->oColorValues          = ( COLORVALUE * )( devicecode->cacheOutput + nDeviceColorants ) ;
    devicecode->tmpColorValues        = ( COLORVALUE * )( devicecode->oColorValues + nDeviceColorants ) ;
    devicecode->sortedColorValues     = ( COLORVALUE * )( devicecode->tmpColorValues + nDeviceColorants ) ;
    devicecode->reducedColorValues    = ( COLORVALUE * )( devicecode->sortedColorValues + nDeviceColorants ) ;
    devicecode->cFF00                 = ( COLORVALUE * )( devicecode->reducedColorValues + nDeviceColorants ) ;
    devicecode->cacheHash             = ( COLORVALUE * )( devicecode->cFF00 + nColorants ) ;
  }
  else {
    devicecode->overprintMask         = NULL ;
    devicecode->colorantsInSpotlist   = NULL ;
    devicecode->transferLinks         = NULL ;
    devicecode->calibrationLinks      = NULL ;
    devicecode->nMappedColorants      = NULL ;
    devicecode->colorantMaps          = NULL ;
    devicecode->oColorValues          = NULL ;
    devicecode->oColorants            = NULL ;
    devicecode->tmpColorValues        = NULL ;
    devicecode->sorted_htTransformInfo= NULL ;
    devicecode->sortingIndexes        = NULL ;
    devicecode->sortedColorants       = NULL ;
    devicecode->sortedColorValues     = NULL ;
    devicecode->reducedColorants      = NULL ;
    devicecode->reducedColorValues    = NULL ;
    devicecode->dciluts               = NULL ;
    devicecode->cacheInput            = NULL ;
    devicecode->cacheOutput           = NULL ;
    devicecode->cFF00                 = NULL ;
    devicecode->cacheHash             = NULL ;
  }
}

/* ---------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )
static void devicecodeAssertions( CLINK *pLink )
{
  CLINKDEVICECODEinfo *devicecode ;
  int32               nColorants ;
  int32               nDeviceColorants ;
  int32               i ;
  int32               tmp ;

  devicecode = pLink->p.devicecode ;
  nColorants = pLink->n_iColorants ;
  nDeviceColorants = devicecode->nDeviceColorants ;

  cc_commonAssertions( pLink ,
                       CL_TYPEdevicecode ,
                       devicecodeStructSize( nColorants, nDeviceColorants ) ,
                       & CLINKdevicecode_functions ) ;

  if ( nColorants > 0 ) {
    HQASSERT(devicecode->overprintMask          == ( uint32 * )( devicecode + 1 ) ,
             "overprintMask not set" ) ;
    HQASSERT(devicecode->colorantsInSpotlist    == ( int32 * )( devicecode->overprintMask + OVERPRINTMASK_WORDSIZE( nDeviceColorants ) ) ,
             "colorantsInSpotlist not set" ) ;
    HQASSERT(devicecode->transferLinks          == ( CLINK ** )( devicecode->colorantsInSpotlist + nColorants ) ,
             "transferLinks not set" ) ;
    HQASSERT(devicecode->calibrationLinks       == ( CLINK ** )( devicecode->transferLinks + nColorants ) ,
             "calibrationLinks not set" ) ;
    HQASSERT(devicecode->nMappedColorants       == ( uint32 * )( devicecode->calibrationLinks + nColorants ) ,
             "nMappedColorants not set" ) ;
    HQASSERT(devicecode->colorantMaps           == ( COLORANTINDEX ** )( devicecode->nMappedColorants + nColorants ) ,
             "colorantMaps not set" ) ;
    HQASSERT(devicecode->oColorants             == ( COLORANTINDEX * )( devicecode->colorantMaps + nColorants ) ,
             "oColorants not set" ) ;
    HQASSERT(devicecode->sorted_htTransformInfo == ( HT_TRANSFORM_INFO * )( devicecode->oColorants + nDeviceColorants ) ,
             "sorted_htTransformInfo not set" ) ;
    HQASSERT(devicecode->sortingIndexes         == ( int32 * )( devicecode->sorted_htTransformInfo + nDeviceColorants ) ,
             "sortingIndexes not set" ) ;
    HQASSERT(devicecode->sortedColorants        == ( COLORANTINDEX * )( devicecode->sortingIndexes + nDeviceColorants ) ,
             "sortedColorants not set" ) ;
    HQASSERT(devicecode->reducedColorants       == ( COLORANTINDEX * )( devicecode->sortedColorants + nDeviceColorants ) ,
             "reducedColorants not set" ) ;
    HQASSERT(devicecode->dciluts                == ( DCILUT ** )( devicecode->reducedColorants + nDeviceColorants ) ,
             "dciluts not set" ) ;
    HQASSERT(devicecode->cacheInput             == ( COLORVALUE ** )( devicecode->dciluts + nColorants ) ,
             "cacheInput not set" ) ;
    HQASSERT(devicecode->cacheOutput            == ( COLORVALUE ** )( devicecode->cacheInput + nColorants ) ,
             "cacheOutput not set" ) ;
    HQASSERT(devicecode->oColorValues           == ( COLORVALUE * )( devicecode->cacheOutput + nDeviceColorants ) ,
             "oColorValues not set" ) ;
    HQASSERT(devicecode->tmpColorValues         == ( COLORVALUE * )( devicecode->oColorValues + nDeviceColorants ) ,
             "tmpColorValues not set" ) ;
    HQASSERT(devicecode->sortedColorValues      == ( COLORVALUE * )( devicecode->tmpColorValues + nDeviceColorants ) ,
             "sortedColorValues not set" ) ;
    HQASSERT(devicecode->reducedColorValues     == ( COLORVALUE * )( devicecode->sortedColorValues + nDeviceColorants ) ,
             "reducedColorValues not set" ) ;
    HQASSERT(devicecode->cFF00                  == ( COLORVALUE * )( devicecode->reducedColorValues + nDeviceColorants ) ,
             "cFF00 not set" ) ;
    HQASSERT(devicecode->cacheHash              == ( COLORVALUE * )( devicecode->cFF00 + nColorants ) ,
             "cacheHash not set" ) ;

    tmp = 0 ;
    for (i = 0; i < nColorants; i++)
      tmp += devicecode->nMappedColorants[i] ;
    HQASSERT(nDeviceColorants == tmp, "inconsistent colorant mappings") ;
  }
  else {
    HQASSERT( devicecode->overprintMask == NULL ,
              "overprintMask should be null" ) ;
    HQASSERT( devicecode->dciluts == NULL ,
              "dciluts should be null" ) ;
  }

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceGray:
  case SPACE_DeviceK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_Pattern:     /* allowed for colored patterns */
  case SPACE_PatternMask: /* and for painting the pattern mask for uncolored patterns */
  case SPACE_TrapDeviceN: /* special for trapping */
    break;
  default:
    HQFAIL( "Bad input color space" ) ;
    break ;
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Devicecode Info Data Access Functions
 * =====================================
 */

static Bool createdevicecodeinfo( GS_DEVICECODEinfo   **devicecodeInfo )
{
  GS_DEVICECODEinfo *pInfo ;
  size_t            structSize ;

  structSize = sizeof( GS_DEVICECODEinfo ) ;

  pInfo = mm_sac_alloc( mm_pool_color, structSize, MM_ALLOC_CLASS_NCOLOR ) ;
  *devicecodeInfo = pInfo ;
  if ( pInfo == NULL )
    return error_handler( VMERROR ) ;

  pInfo->refCnt = 1 ;
  pInfo->structSize = structSize ;

  pInfo->overprinting[GSC_FILL] = FALSE ;
  pInfo->overprinting[GSC_STROKE] = FALSE ;

  pInfo->overprintsEnabled = TRUE;
  pInfo->overprintMode = INITIAL_OVERPRINT_MODE ;
  pInfo->currentOverprintMode = INITIAL_OVERPRINT_MODE ;
  pInfo->overprintBlack = INITIAL_OVERPRINT_BLACK ;
  pInfo->overprintGray = INITIAL_OVERPRINT_GRAY ;
  pInfo->overprintGrayImages = INITIAL_OVERPRINT_GRAYIMAGES ;
  pInfo->overprintWhite = INITIAL_OVERPRINT_WHITE ;
  pInfo->ignoreOverprintMode = INITIAL_IGNORE_OVERPRINT_MODE ;
  pInfo->transformedSpotOverprint = INITIAL_TRANSFORMED_SPOT_OVERPRINT ;
  pInfo->overprintICCBased = INITIAL_OVERPRINT_ICCBASED ;

  devicecodeInfoAssertions( pInfo ) ;

  return TRUE ;
}

Bool cc_samedevicecodeinfo(void * pvGS_devicecodeinfo, CLINK * pLink)
{
  HQASSERT (pvGS_devicecodeinfo != NULL,
            "pvGS_devicecodeinfo is NULL in gsc_samedevicecodeinfo");
  devicecodeAssertions (pLink);

  return (GS_DEVICECODEinfo *) pvGS_devicecodeinfo == pLink->p.devicecode->devicecodeInfo;
}

static void freedevicecodeinfo( GS_DEVICECODEinfo *devicecodeInfo )
{
  mm_sac_free(mm_pool_color, devicecodeInfo, devicecodeInfo->structSize);
}

void cc_destroydevicecodeinfo( GS_DEVICECODEinfo   **devicecodeInfo )
{
  if ( *devicecodeInfo != NULL ) {
    devicecodeInfoAssertions(*devicecodeInfo);
    CLINK_RELEASE(devicecodeInfo, freedevicecodeinfo);
  }
}

void cc_reservedevicecodeinfo( GS_DEVICECODEinfo *devicecodeInfo )
{
  if ( devicecodeInfo != NULL ) {
    devicecodeInfoAssertions( devicecodeInfo ) ;
    CLINK_RESERVE( devicecodeInfo ) ;
  }
}

static Bool updatedevicecodeinfo( GS_DEVICECODEinfo   **devicecodeInfo )
{
  if ( *devicecodeInfo == NULL )
    return createdevicecodeinfo( devicecodeInfo ) ;

  devicecodeInfoAssertions( *devicecodeInfo ) ;

  CLINK_UPDATE(GS_DEVICECODEinfo, devicecodeInfo,
               copydevicecodeinfo, freedevicecodeinfo);
  return TRUE ;
}

static Bool copydevicecodeinfo( GS_DEVICECODEinfo *devicecodeInfo ,
                                GS_DEVICECODEinfo **devicecodeInfoCopy )
{
  GS_DEVICECODEinfo *pInfoCopy ;

  devicecodeInfoAssertions( devicecodeInfo ) ;

  pInfoCopy = mm_sac_alloc( mm_pool_color,
                            devicecodeInfo->structSize ,
                            MM_ALLOC_CLASS_NCOLOR ) ;
  if ( pInfoCopy == NULL )
    return error_handler( VMERROR ) ;

  *devicecodeInfoCopy = pInfoCopy ;
  HqMemCpy( pInfoCopy , devicecodeInfo , devicecodeInfo->structSize ) ;

  pInfoCopy->refCnt = 1 ;

  return TRUE ;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the devicecode info access functions.
 */
static void devicecodeInfoAssertions(GS_DEVICECODEinfo *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == sizeof(GS_DEVICECODEinfo),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

/* ---------------------------------------------------------------------- */
void cc_deviceGetOutputColors( CLINK *pLink,
                               COLORVALUE **oColorValues,
                               COLORANTINDEX **oColorants,
                               int32 *nColors,
                               Bool *fOverprinting )
{
  CLINKDEVICECODEinfo *devicecode ;

  HQASSERT( pLink, "pLink is NULL in deviceGetOutputColors" ) ;
  HQASSERT( pLink->linkType == CL_TYPEdevicecode, "pLink is wrong type in deviceGetOutputColors" ) ;

  /* If overprint actually reduced then return the reduced (and sorted)
   * color information. Otherwise return the unsorted color values -
   * since this info is used by guc_outputColorSpaceMapping, and in the
   * common case (eg CMYK device) that fn won't need to build a mapping
   * array. Do not return the reduced overprint for shfill chains; these
   * call the colour evaluation many times, and the reduced overprints for
   * one sample will not, in general, be valid for the whole shfill. Overprint
   * unification and reduction is performed on the final shfill DL colours at
   * the end of shfill creation.
   */
  devicecode = pLink->p.devicecode ;
  if ( devicecode->nReducedColorants > 0 &&
       devicecode->colorType != GSC_SHFILL &&
       devicecode->colorType != GSC_SHFILL_INDEXED_BASE &&
       devicecode->colorType != GSC_VIGNETTE ) {
    *nColors      = devicecode->nReducedColorants ;
    *oColorants   = devicecode->reducedColorants ;
    *oColorValues = devicecode->reducedColorValues ;
  }
  else {
    *nColors      = devicecode->nSortedColorants;
    *oColorants   = devicecode->sortedColorants;
    *oColorValues = devicecode->sortedColorValues ;
  }
  *fOverprinting  = ( devicecode->overprintFlags & RENDER_KNOCKOUT ) == 0;
}

/* ---------------------------------------------------------------------- */
/** Return a color space to HDLT to encode the final set of colorants sent to
 * the output device. If the final output colorants match a standard device space,
 * this color space is NULL. But if it requires a DeviceN color space, we pass
 * back the internalised color space we made earlier, which encodes the list of
 * output colorants.
 */
void cc_deviceGetOutputColorSpace( CLINK *pLink, OBJECT **colorSpaceObj )
{
  CLINKDEVICECODEinfo *devicecode ;

  HQASSERT( pLink, "pLink is NULL in deviceGetOutputColorSpace" ) ;
  HQASSERT( pLink->linkType == CL_TYPEdevicecode, "pLink is wrong type in deviceGetOutputColorSpace" ) ;

  /* Return the colorspace object for the output DeviceN/Separation colorspace
   * for the benefit of HDLT.
   */
  devicecode = pLink->p.devicecode ;

  if (oType(devicecode->colorSpaceObj) == ONULL)
    *colorSpaceObj = NULL ;
  else
    *colorSpaceObj = &devicecode->colorSpaceObj ;
}

/** Internalise a final DeviceN color space for returning to HDLT if the
 * InputColorSpace option is false. It will be a DeviceN space containing the
 * list of output colorants, which is the most important feature of this color
 * space. We are required to internalise this space because our color chain
 * caching can re-use a chain below its original save level where the original PS
 * color space may be out of scope.
 * NB. The tint transform will not be valid, and an error will result from
 * executing it.
 */
static Bool internaliseDeviceNColorSpace(OBJECT           *dstColorSpace,
                                         OBJECT           *srcColorSpace,
                                         OBJECT           *dstDevicenCSA,
                                         GS_COLORinfo     *colorInfo,
                                         OBJECT           *illegalTintTransform)
{
  OBJECT *colorantArray;
  int numColorants;
  COLORSPACE_ID colorSpaceId;
  int32 i;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(illegalTintTransform != NULL, "illegalTintTransform NULL");
  HQASSERT(dstColorSpace != NULL, "dstColorSpace NULL");
  HQASSERT(dstDevicenCSA != NULL, "dstDevicenCSA NULL");
  HQASSERT(srcColorSpace != NULL, "srcColorSpace NULL");

  if ( !gsc_getcolorspacesizeandtype(colorInfo, srcColorSpace,
                                     &colorSpaceId, &numColorants))
    return FALSE;

  HQASSERT(colorSpaceId == SPACE_Separation || colorSpaceId == SPACE_DeviceN,
           "Unexpected color space id");

  /* Associate the colorspace array of 4 objects and colorant list */
  theTags(*dstColorSpace) = OARRAY | LITERAL | UNLIMITED;
  theLen(*dstColorSpace) = 4;
  oArray(*dstColorSpace) = dstDevicenCSA;

  /* The colorspace type, always DeviceN */
  object_store_name(&dstDevicenCSA[0], NAME_DeviceN, LITERAL);

  /* The colorant array, allocate here but populate below */
  colorantArray = mm_alloc_with_header(mm_pool_color,
                                       numColorants * sizeof(OBJECT),
                                       MM_ALLOC_CLASS_NCOLOR);
  if (colorantArray == NULL)
    return error_handler(VMERROR);
  theTags(dstDevicenCSA[1]) = OARRAY | LITERAL | UNLIMITED;
  theLen(dstDevicenCSA[1]) = (uint16) numColorants;
  oArray(dstDevicenCSA[1]) = colorantArray;

  /* Copy the colorant array */
  if (colorSpaceId == SPACE_Separation) {
    Copy(&colorantArray[0], &oArray(*srcColorSpace)[1]);
  }
  else {
    for ( i = 0; i < numColorants; i++ )
      Copy(&colorantArray[i], &oArray(oArray(*srcColorSpace)[1])[i]);
  }

  /* We should never go through the alternative space/tint transform.
   * Neither do we have the information to be able to create a sensible
   * tint transform (we cannot analytically invert custom transforms).
   * We'll force an error with an illegal tint transform in the event that we
   * have to use it.
   */
  object_store_name(&dstDevicenCSA[2], NAME_DeviceGray, LITERAL);

  theTags(dstDevicenCSA[3]) = OARRAY | EXECUTABLE | UNLIMITED;
  theLen(dstDevicenCSA[3]) = 1;
  oArray(dstDevicenCSA[3]) = illegalTintTransform;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool deviceCheckSetColor( CLINK *pLink )
{
  HQASSERT( pLink , "pLink NULL in deviceCheckSetColor" ) ;
  HQASSERT( pLink->linkType == CL_TYPEdevicecode, "pLink is wrong type in deviceCheckSetColor" ) ;
  HQASSERT( new_color_detected , "new_color_detected assumed to be TRUE" ) ;

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
    { USERVALUE cr = pLink->iColorValues[ 0 ] ;
      USERVALUE mg = pLink->iColorValues[ 1 ] ;
      USERVALUE yb = pLink->iColorValues[ 2 ] ;
      if ( cr != mg || mg != yb || yb != 0.0 ) {
        if (!detect_setcolor_separation())
          return FALSE;
      }
    }
    break ;
  case SPACE_DeviceCMY:
  case SPACE_DeviceRGB:
    { USERVALUE cr = pLink->iColorValues[ 0 ] ;
      USERVALUE mg = pLink->iColorValues[ 1 ] ;
      USERVALUE yb = pLink->iColorValues[ 2 ] ;
      if ( cr != mg || mg != yb ) {
        if (!detect_setcolor_separation())
          return FALSE;
      }
    }
    break ;
  case SPACE_DeviceN:
  case SPACE_Separation:
    { CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode;
      /* Ignore a single colorant of none or black. */
      if ( devicecode->nSortedColorants != 1 ||
           (devicecode->sortedColorants[ 0 ] != COLORANTINDEX_NONE &&
            devicecode->blackPosition == -1) ) {
        if (!detect_setcolor_separation())
          return FALSE;
      }
    }
    break ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool deviceCheckSetScreen(CLINKDEVICECODEinfo * devicecode)
{
  HQASSERT( devicecode , "devicecode NULL in deviceCheckSetScreen" ) ;
  HQASSERT( devicecode->pHeadLink , "pHeadLink NULL in deviceCheckSetScreen" ) ;
  HQASSERT( new_screen_detected , "new_screen_detected assumed to be TRUE" ) ;

  /* Check for a non-b/w object that could have come from a pre-separated job.
   * The input color space must therefore be DeviceGray.
   * Should also ignore pattern screens, since these don't tell us anything.
   * [Note that patterns virtually never get here due to small frequency
   *  screen removal in spdetect.c.]
   * Don't need to have the same code in the invokeBlock routine since images
   * are tested once and for all in the image code.
   */
  if (devicecode->jobColorSpace == SPACE_DeviceGray &&
      ! fTreatScreenAsPattern(devicecode->jobColorSpace,
                              devicecode->colorType,
                              devicecode->hRasterStyle,
                              devicecode->spotno))
      return (setscreen_separation( devicecode->pHeadLink->iColorValues[ 0 ] > 0.0f &&
                                    devicecode->pHeadLink->iColorValues[ 0 ] < 1.0f ) ) ;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool background_colorants( int32            nColorants ,
                                  COLORANTINDEX    iColorants[] ,
                                  uint8            *fBgFlags ,
                                  GUCR_RASTERSTYLE *hRasterStyle )
{
  Bool fBackground = FALSE ;

  if ( guc_colorantAnyBackgroundSeparations( hRasterStyle )) {
    int32 i ;
    for ( i = 0 ; i < nColorants ; ++i ) {
      if ( iColorants[ i ] == COLORANTINDEX_ALL ) {
        fBackground = FALSE ;
        break ; /* Anything in the all separation is deemed not background. */
      }
      if ( guc_colorantIsBackgroundSeparation( hRasterStyle , iColorants[ i ])) {
        if ( ! fBackground && i != 0 )
          return detail_error_handler( RANGECHECK ,
            "Mixture of background and non-background colors is not allowed.") ;
        fBackground = TRUE ;
      }
      else if ( fBackground ) {
        /* Inconsistent background properties. */
        return detail_error_handler( RANGECHECK ,
          "Mixture of background and non-background colors is not allowed.") ;
      }
    }
  }

  (*fBgFlags) = ( fBackground ? RENDER_BACKGROUND : RENDER_KNOCKOUT ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* To sort colorant indexes into ascending index order along with a
 * mapping array from output to input colorant indexes.
 *
 * NB. COLORANTINDEX_ALL will be at the front since it is negative!
 */
static void sort_colorants( int32         nColorants ,
                            COLORANTINDEX iColorants[] ,
                            int32         *pnColorants ,
                            COLORANTINDEX oColorants[] ,
                            int32         oi_mapping[] ,
                            int16         duplicateColorants ,
                            Bool          *simpleMapping )
{
  int32 i ;
  int32 j ;

  HQASSERT( nColorants >= 1 , "zero length array!" ) ;
  HQASSERT( iColorants != NULL , "iColorants array not set" ) ;
  HQASSERT( oColorants != NULL , "oColorants array not set" ) ;
  HQASSERT( oi_mapping != NULL , "oi_mapping array not set" ) ;

  /* Copy the input to the output array */
  for ( i = 0 ; i < nColorants ; ++i ) {
    oi_mapping[ i ] = i ;
    oColorants[ i ] = iColorants[ i ] ;
  }

  /* Sort the output array into colorant order */
  for ( i = nColorants - 1 ; i > 0 ; --i ) {
    Bool fSwapped = FALSE ;
    COLORANTINDEX cj , cj1 ;

    cj1 = oColorants[ 0 ] ;
    for ( j = 0 ; j < i ; ++j ) {
      cj  = cj1 ;
      cj1 = oColorants[ j + 1 ] ;
      if ( cj > cj1 ) {
        COLORANTINDEX t ;

        t = oi_mapping[ j ] ;
        oi_mapping[ j ] = oi_mapping[ j + 1 ] ;
        oi_mapping[ j + 1 ] = t ;

        oColorants[ j + 1 ] = cj ;
        oColorants[ j ] = cj1 ;
        cj1 = cj ;

        /* Flag we did a swap */
        fSwapped = TRUE ;
      }
    }

    if ( ! fSwapped )
      break ;
  }

  /* Remove any /None entries if the color has some non /None entries. */
  HQASSERT( COLORANTINDEX_NONE < COLORANTINDEX_ALL ,
            "COLORANTINDEX_ALL must come first" ) ;
  i = 0 ;
  if ( oColorants[ 0 ] == COLORANTINDEX_NONE &&
       oColorants[ nColorants - 1 ] != COLORANTINDEX_NONE ) {
    do {
      oi_mapping[ i++ ] = -1 ;
    } while ( oColorants[ i ] == COLORANTINDEX_NONE ) ;
  }

  /* Remove duplicate colorants (arising from duplicate colorant names in a
     DeviceN colorspace).  Pick either the first or the last depending on the
     userparam.  Adobe rips seem to choose the last duplicate, but some jobs
     require the first to be chosen for correct output. */
  j = 0 ;
  while ((++i) < nColorants ) {
    COLORANTINDEX cj = oColorants[ i - 1 ] ;
    if ( cj == oColorants[ i ] ) {
      if ( duplicateColorants == NAME_First )
        oi_mapping[ i ] = -1 ;
      else
        oi_mapping[ i - 1 ] = -1 ;
    }
    else
      oColorants[ j++ ] = cj ; /* This is most often assigning to itself
                                * (when there are no duplicates or no
                                * preceding duplicates) but testing for
                                * that won't be any faster.
                                */
  }
                               /* Store the last colorant. */
  oColorants[ j ] = oColorants[ i - 1 ] ;

  /* Store total number of unique colorants. */
  (*pnColorants) = j + 1 ;

  *simpleMapping = FALSE;
  if (nColorants == *pnColorants) {
    *simpleMapping = TRUE;
    for (i = 0; i < nColorants; i++) {
      if (iColorants[i] != oColorants[i]) {
        *simpleMapping = FALSE;
        break;
      }
    }
  }
}

/* ---------------------------------------------------------------------- */
static void set_updateHTCacheFunction( CLINKDEVICECODEinfo *devicecode,
                                       int32               nColorants,
                                       GUCR_RASTERSTYLE    *hRasterStyle )
{
  if ( fTreatScreenAsPattern(devicecode->jobColorSpace,
                             devicecode->colorType,
                             devicecode->hRasterStyle,
                             devicecode->spotno) )
    devicecode->updateHTCacheFunction = updateHTCacheForPatternContone ;

  else if ( devicecode->patternPaintType == COLOURED_PATTERN )
    /* aka painting in a colored pattern. Uncolored patterns are painted in the
     * current color and therefore need the halftone cache updating as normal.
     */
    devicecode->updateHTCacheFunction = updateHTCacheForNothing ;

  else if ( nColorants == 0 ) /* None colorant, or unset pattern space */
    devicecode->updateHTCacheFunction = updateHTCacheForNothing ;

  else if ( devicecode->fCompositing ) {
    if ( ! guc_backdropRasterStyle( hRasterStyle )
         && gucr_halftoning( hRasterStyle )
         && devicecode->colorType != GSC_IMAGE
         && devicecode->colorType != GSC_BACKDROP ) {
      HQASSERT( devicecode->colorType != GSC_SHFILL_INDEXED_BASE,
                "Didn't expect GSC_SHFILL_INDEXED_BASE");

      /* Producing final device colors in the backend and need to allocate
         forms.  Exception is when called by im/cv_colcvt (for image and
         backdrop) which do quantisation and halftone update (for per span
         spotno) themselves.  Chains of type GSC_VIGNETTE won't be treated the
         same as GSC_SHFILL because vignettes don't have extra levels added
         during rendering. */
      if ( devicecode->colorType == GSC_SHFILL )
        devicecode->updateHTCacheFunction = updateHTCacheForHalftoneShfill ;
      else
        devicecode->updateHTCacheFunction = updateHTCacheForHalftone ;
    } else
      devicecode->updateHTCacheFunction = updateHTCacheForNothing ;
  } else {
    if ( guc_backdropRasterStyle( hRasterStyle ) ) {
      /* Do not allocate forms yet, just stop the halftone being reclaimed. */

      devicecode->updateHTCacheFunction = updateHTCacheForHalftoneBackdropRender ;
    } else {
      if ( isTrappingActive(devicecode->page) ) {
        /* This test could be further refined using some tasks context, which
         * might further restrict whether these special cases are required.
         * Any color chains created after trapping begins creating traps
         * probably don't need these special cache update functions.
         */
        if ( gucr_halftoning( hRasterStyle ))
          devicecode->updateHTCacheFunction = updateHTCacheForHalftoneTrapping ;
        else
          devicecode->updateHTCacheFunction = updateHTCacheForContoneTrapping ;
      }
      else if ( gucr_halftoning( hRasterStyle ))
        devicecode->updateHTCacheFunction = updateHTCacheForHalftone ;
      else
        devicecode->updateHTCacheFunction = updateHTCacheForContone ;
    }
  }
}

/* ---------------------------------------------------------------------- */
static int32 set_clidsize( int32 nColorants )
{
  if ( nColorants == 0 ) /* aka painting into a mask */
    return CLID_SIZEdisabled ;
  else
    return CLID_SIZEdevicecode ;
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForHalftoneBackdropRender( CLINK *pLink ,
                                                    int32 nColors ,
                                                    COLORVALUE *iColorValues,
                                                    COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode;

  UNUSED_PARAM(int32, nColors);
  UNUSED_PARAM(COLORVALUE*, iColorValues);
  UNUSED_PARAM(COLORVALUE*, oColorValues);

  /* It is non-trivial to determine the final colorants that will be rendered
     when this color has gone through backend color conversion, and therefore it
     is safest to preserve the halftone in all colorants in the device
     rasterstyle. Forms are allocated later as part of the backend color
     conversion when the final colorants and values are known. */
  ht_setUsedDeviceRS(devicecode->page->eraseno,
                     devicecode->spotno, devicecode->httype,
                     devicecode->hDeviceRasterStyle) ;
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForHalftone( CLINK *pLink , int32 nColors ,
                                      COLORVALUE *iColorValues,
                                      COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  int32 i, nSortedColorants = devicecode->nSortedColorants ;
  SPOTNO spotno = devicecode->spotno ;
  HTTYPE httype = devicecode->httype;

  if ( !devicecode->fCompositing )
    /* A front-end chain ending in device space. Object may require
       compositing. */
    updateHTCacheForHalftoneBackdropRender(pLink, nColors,
                                           iColorValues, oColorValues);

  /* Quantisation and form allocation are handled outside of the color chain for
     invoke block. */
  if ( iColorValues == NULL )
    return;

  HQASSERT(nColors == 1, "Can't quantise more than one color for update") ;

  /* Need to quantise to halftone levels before attempting to allocate
     forms */
  ht_doTransforms(nSortedColorants,
                  iColorValues,
                  devicecode->sorted_htTransformInfo,
                  oColorValues) ;

  for ( i = 0 ; i < nSortedColorants ; ++i ) {
    GUCR_COLORANT *colorant;
    const GUCR_COLORANT_INFO *info;

    /* Don't allocate form if colorant definitely won't be rendered. */
    gucr_colorantHandle(devicecode->hDeviceRasterStyle,
                        devicecode->sortedColorants[i], &colorant) ;
    if ( colorant != NULL &&
         gucr_colorantDescription(colorant, &info) ) {
      /* The return is true if the screen is fully populated with
       * forms. We're not using it to stop these updates so that the
       * halftone cache can change under our feet. */
      (void) ht_allocateForm( devicecode->page->eraseno, spotno, httype,
                              devicecode->sortedColorants[ i ] ,
                              nColors ,
                              oColorValues + i ,
                              nSortedColorants, NULL );
    }
  }
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForHalftoneTrapping( CLINK *pLink , int32 nColors ,
                                              COLORVALUE *iColorValues,
                                              COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  int32 i, nSortedColorants = devicecode->nSortedColorants ;
  SPOTNO spotno = devicecode->spotno ;
  HTTYPE httype = devicecode->httype;
  COLORVALUE white ;

  if ( !devicecode->fCompositing )
    /* A front-end chain ending in device space. Object may require
       compositing. */
    updateHTCacheForHalftoneBackdropRender(pLink, nColors,
                                           iColorValues, oColorValues);

  /* Quantisation and form allocation are handled outside of the color chain for
     invoke block. */
  if ( iColorValues == NULL )
    return;

  HQASSERT(nColors == 1, "Can't quantise more than one color for update") ;

  /* Need to quantise to halftone levels before attempting to allocate
     forms */
  ht_doTransforms(nSortedColorants,
                  iColorValues,
                  devicecode->sorted_htTransformInfo,
                  oColorValues) ;

  white = CAST_TO_COLORVALUE(gucr_valuesPerComponent(devicecode->hRasterStyle) - 1);

  for ( i = 0 ; i < nSortedColorants ; ++i ) {
    /* when trapping, always reserve the spot no even if we have only seen
     * solid colors so far. Trapping might introduce new values for this
     * spot */
    if ( oColorValues[ i ] == white || oColorValues[ i ] == 0 )
      ht_setUsed(devicecode->page->eraseno, spotno, httype,
                 devicecode->sortedColorants[ i ]) ;

    else {
      GUCR_COLORANT *colorant;
      const GUCR_COLORANT_INFO *info;

      /* Don't allocate form if colorant definitely won't be rendered. */
      gucr_colorantHandle(devicecode->hDeviceRasterStyle,
                          devicecode->sortedColorants[i], &colorant) ;
      if ( colorant != NULL &&
           gucr_colorantDescription(colorant, &info) ) {
        /* The return is true if the screen is fully populated with
         * forms. We're not using it to stop these updates so that the
         * halftone cache can change under our feet. */
        (void) ht_allocateForm( devicecode->page->eraseno, spotno, httype,
                                devicecode->sortedColorants[ i ] ,
                                nColors ,
                                oColorValues + i ,
                                nSortedColorants, NULL );
      }
    }
  }
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForHalftoneShfill( CLINK *pLink , int32 nColors ,
                                            COLORVALUE *iColorValues,
                                            COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  int32 i ;

  UNUSED_PARAM( int32 , nColors ) ;
  UNUSED_PARAM( COLORVALUE *, iColorValues ) ;
  UNUSED_PARAM( COLORVALUE *, oColorValues ) ;

  ht_defer_allocation() ;

  for ( i = 0 ; i < devicecode->nSortedColorants ; ++i )
    ht_set_all_levels_used(devicecode->page->eraseno,
                           devicecode->spotno, devicecode->httype,
                           devicecode->sortedColorants[i]);

  ht_resume_allocation(devicecode->spotno, TRUE) ;
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForContone( CLINK *pLink , int32 nColors ,
                                     COLORVALUE *iColorValues,
                                     COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  int32 i, nSortedColorants = devicecode->nSortedColorants ;
  SPOTNO spotno = devicecode->spotno ;
  HTTYPE httype = devicecode->httype;
  COLORVALUE white;

  if ( !devicecode->fCompositing )
    /* A front-end chain ending in device space. Object may require
       compositing. */
    updateHTCacheForHalftoneBackdropRender(pLink, nColors,
                                           iColorValues, oColorValues);

  /* Quantisation and form allocation are handled outside of the color chain for
     invoke block. */
  if ( iColorValues == NULL )
    return;

  HQASSERT(nColors == 1, "Can't quantise more than one color for update") ;

  /* Quantise so that halftone module gets consistent input */
  ht_doTransforms(nSortedColorants,
                  iColorValues,
                  devicecode->sorted_htTransformInfo,
                  oColorValues) ;

  white = CAST_TO_COLORVALUE(gucr_valuesPerComponent(devicecode->hRasterStyle) - 1);

  for ( i = 0 ; i < nSortedColorants ; ++i ) {
    /* The return is true if the screen is marked as used. We're ignoring it
     * because the halftone cache could change under our feet.
     */
    (void) ht_keep_screen(devicecode->page->eraseno, spotno, httype,
                          devicecode->sortedColorants[ i ],
                          nColors, oColorValues + i,
                          nSortedColorants, white);
  }
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForContoneTrapping( CLINK *pLink , int32 nColors ,
                                             COLORVALUE *iColorValues,
                                             COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  int32 i, nSortedColorants = devicecode->nSortedColorants ;
  SPOTNO spotno = devicecode->spotno ;
  HTTYPE httype = devicecode->httype;
  COLORVALUE white;

  if ( !devicecode->fCompositing )
    /* A front-end chain ending in device space. Object may require
       compositing. */
    updateHTCacheForHalftoneBackdropRender(pLink, nColors,
                                           iColorValues, oColorValues);

  /* Quantisation and form allocation are handled outside of the color chain for
     invoke block. */
  if ( iColorValues == NULL )
    return;

  HQASSERT(nColors == 1, "Can't quantise more than one color for update") ;

  /* Quantise so that halftone module gets consistent input */
  ht_doTransforms(nSortedColorants,
                  iColorValues,
                  devicecode->sorted_htTransformInfo,
                  oColorValues) ;

  white = CAST_TO_COLORVALUE(gucr_valuesPerComponent(devicecode->hRasterStyle) - 1);

  for ( i = 0 ; i < nSortedColorants ; ++i ) {
    /* when trapping, always reserve the spot no even if we have only seen
     * solid colors so far. Trapping might introduce new values for this
     * spot */
    if ( oColorValues[ i ] == white || oColorValues[ i ] == 0 )
      ht_setUsed(devicecode->page->eraseno, spotno, httype,
                 devicecode->sortedColorants[ i ]) ;

    else
      /* The return is true if the screen is marked as used. We're ignoring it
       * because the halftone cache could change under our feet.
       */
      (void) ht_keep_screen(devicecode->page->eraseno, spotno, httype,
                            devicecode->sortedColorants[ i ],
                            nColors, oColorValues + i,
                            nSortedColorants, white);
  }
}


/* ---------------------------------------------------------------------- */
static void updateHTCacheForPatternContone( CLINK *pLink , int32 nColors ,
                                            COLORVALUE *iColorValues,
                                            COLORVALUE *oColorValues )
{
  CLINKDEVICECODEinfo *devicecode = pLink->p.devicecode ;
  int32 i, nSortedColorants = devicecode->nSortedColorants ;
  SPOTNO spotno = devicecode->spotno ;
  int32 blackPosition = devicecode->blackPosition;
  int32 state ;

  UNUSED_PARAM( int32 , nColors ) ;
  UNUSED_PARAM( COLORVALUE *, oColorValues ) ;

  /* we should have filtered out images, shfills and backdrops used with
     what could be pattern screens, since fTreatScreenAsPattern returns
     false for those types of object */
  HQASSERT(devicecode->colorType == GSC_FILL || devicecode->colorType == GSC_STROKE,
           "Unexpected colorType");
  HQASSERT(iColorValues, "No unquantised colour in pattern screen update") ;
  HQASSERT( nColors == 1 , "Can only be TRUE with invokeSingle; hence 1 color" ) ;

  state = ht_getColorState(spotno, HTTYPE_DEFAULT,
                           devicecode->sortedColorants[0],
                           iColorValues[ 0 ], devicecode->hRasterStyle) ;
  if ( state == HT_TINT_SHADE ) {
    /* Is a pattern; simply set spflags and invokeSingle selects black. */
    HQASSERT( fTreatScreenAsPattern(devicecode->jobColorSpace,
                                    devicecode->colorType,
                                    devicecode->hRasterStyle,
                                    spotno),
              "No pattern state when using pattern screen" ) ;
    ht_setUsed(devicecode->page->eraseno,
               spotno, HTTYPE_DEFAULT, COLORANTINDEX_NONE);
    dl_set_currentspflags(devicecode->page->dlc_context, RENDER_PATTERN) ;
    for (i = 1; i < nSortedColorants; i++)
      iColorValues[i] = 0; /* for safety */
    return ;
  }

  /* Not a pattern; draw black or white, depending on input colour. White is
     easy, that's what we'd get anyway. But black is in the All channel which
     if we don't set RENDER_PATTERN as above will not be rendered (and if we
     do it renders the pattern, of course). So we need to put black into the
     black channel, if any, and if not I suggest into all channels. If there
     was more than one color (which there isn't - asserted above) we'd need
     to loop over all the colors doing this. Note that this modifies the
     input colour values, not the temporary output colour value scratch
     space. */
  iColorValues[0] = COLORVALUE_MAX ;

  for (i = 1; i < nSortedColorants; i++) {
    if ( state == HT_TINT_SOLID && (blackPosition < 0 || i == blackPosition))
      iColorValues [i] = 0 ;
    else
      iColorValues [i] = COLORVALUE_MAX ;
  }
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForNothing( CLINK *pLink , int32 nColors ,
                                     COLORVALUE *iColorValues,
                                     COLORVALUE *oColorValues )
{
  UNUSED_PARAM( CLINK * , pLink ) ;
  UNUSED_PARAM( int32 , nColors ) ;
  UNUSED_PARAM( COLORVALUE * , iColorValues ) ;
  UNUSED_PARAM( COLORVALUE * , oColorValues ) ;
}

/* ---------------------------------------------------------------------- */
/* Shfill decomposition creates colors by interpolation instead of color
   chain invocation and therefore requires a specialist routine to update
   the halftone cache. */
void cc_deviceUpdateHTCacheForShfillDecomposition(CLINK* pLink)
{
  CLINKDEVICECODEinfo* devicecode;

  devicecodeAssertions(pLink);
  devicecode = pLink->p.devicecode;

  if (guc_backdropRasterStyle(devicecode->hRasterStyle)) {
    updateHTCacheForHalftoneBackdropRender(pLink,
                                           0    /* anything, not used */,
                                           NULL /* anything, not used */,
                                           NULL /* anything, not used */);
  } else {
    int32 i, nSortedColorants = devicecode->nSortedColorants;
    Bool halftoning = gucr_halftoning(devicecode->hRasterStyle);

    for (i = 0; i < nSortedColorants; ++i)
      if ( halftoning )
        ht_set_all_levels_used(devicecode->page->eraseno,
                               devicecode->spotno, devicecode->httype,
                               devicecode->sortedColorants[i]);
      else
        ht_setUsed(devicecode->page->eraseno,
                   devicecode->spotno, devicecode->httype,
                   devicecode->sortedColorants[i]);
  }
}

/* ---------------------------------------------------------------------- */
static void dcilutremove( DCILUT *dcilut , DCILUT **dciluth )
{
  DCILUT *tdcilut = (*dciluth) ;

#if defined( ASSERT_BUILD )
  /* Check that dcilut to free is on chain. */
  { DCILUT *cdcilut = tdcilut ;
    while ( cdcilut != *dciluth &&
            cdcilut != tdcilut )
      cdcilut = cdcilut->next ;
    HQASSERT( cdcilut == *dciluth , "didn't find dciluth in list to remove" ) ;
  }
#endif

  dcilut->next->prev = dcilut->prev ;
  dcilut->prev->next = dcilut->next ;

  if ( tdcilut == dcilut ) {
    tdcilut = tdcilut->next ;
    if ( tdcilut == dcilut )
      tdcilut = NULL ;
    (*dciluth) = tdcilut ;
  }
}

/* ---------------------------------------------------------------------- */
static void dcilutaddhead( DCILUT *dcilut , DCILUT **headref )
{
  DCILUT *tdcilut = (*headref) ;
  if ( tdcilut == NULL ) {
    dcilut->next = dcilut ;
    dcilut->prev = dcilut ;
    (*headref) = dcilut ;
  }
  else {
    dcilut->next = tdcilut ;
    dcilut->prev = tdcilut->prev ;

    dcilut->next->prev = dcilut ;
    dcilut->prev->next = dcilut ;
    (*headref) = dcilut ;
  }
  dcilut->headref = headref ;
}

/* ---------------------------------------------------------------------- */
#define dcilutaddtail( _dcilut , _dciluth ) MACRO_START \
  DCILUT *_dcilut_ = (_dcilut) ; \
  DCILUT **_dciluth_ = (_dciluth) ; \
  dcilutaddhead( _dcilut_ , _dciluth_ ) ; \
  (*_dciluth_) = (*_dciluth_)->next ; \
MACRO_END

/* -------------------------------------------------------------------------- */
#define IDHASH( _idslotX , _idslotC , _idslotD ) \
   (_idslotX)[ 0 ] ^ (_idslotX)[ 1 ] ^ (_idslotX)[ 2 ] ^ \
   (_idslotC)[ 0 ] ^ (_idslotC)[ 1 ] ^ \
   (_idslotD)[ 0 ] ^ (_idslotD)[ 1 ] ^ (_idslotD)[ 2 ] ^ (_idslotD)[ 3 ]
#define IDMATCH( _dcilut , _idhashXCD , _idslotX , _idslotC , _idslotD ) \
  (_dcilut)->idhashXCD    == (_idhashXCD)    && \
  (_dcilut)->idslotX[ 0 ] == (_idslotX)[ 0 ] && \
  (_dcilut)->idslotX[ 1 ] == (_idslotX)[ 1 ] && \
  (_dcilut)->idslotX[ 2 ] == (_idslotX)[ 2 ] && \
  (_dcilut)->idslotC[ 0 ] == (_idslotC)[ 0 ] && \
  (_dcilut)->idslotC[ 1 ] == (_idslotC)[ 1 ] && \
  (_dcilut)->idslotD[ 0 ] == (_idslotD)[ 0 ] && \
  (_dcilut)->idslotD[ 1 ] == (_idslotD)[ 1 ] && \
  (_dcilut)->idslotD[ 2 ] == (_idslotD)[ 2 ] && \
  (_dcilut)->idslotD[ 3 ] == (_idslotD)[ 3 ]

#define IDCOPY( _dcilut , _idhashXCD , _idslotX , _idslotC , _idslotD ) \
  (_dcilut)->idhashXCD    = (_idhashXCD)    , \
  (_dcilut)->idslotX[ 0 ] = (_idslotX)[ 0 ] , \
  (_dcilut)->idslotX[ 1 ] = (_idslotX)[ 1 ] , \
  (_dcilut)->idslotX[ 2 ] = (_idslotX)[ 2 ] , \
  (_dcilut)->idslotC[ 0 ] = (_idslotC)[ 0 ] , \
  (_dcilut)->idslotC[ 1 ] = (_idslotC)[ 1 ] , \
  (_dcilut)->idslotD[ 0 ] = (_idslotD)[ 0 ] , \
  (_dcilut)->idslotD[ 1 ] = (_idslotD)[ 1 ] , \
  (_dcilut)->idslotD[ 2 ] = (_idslotD)[ 2 ] , \
  (_dcilut)->idslotD[ 3 ] = (_idslotD)[ 3 ]

  /* ---------------------------------------------------------------------- */
static DCILUT *dcilut_create( DCILUT **headref,
                              CLID  idslotX[ 3 ],
                              CLID  idslotC[ 2 ],
                              CLID  idslotD[ 4 ],
                              uint32 nMappedColorants )
{
  CLID   idhashXCD ;
  DCILUT *dcilut ;
  DCILUT *dciluth ;
  mm_size_t cacheSize ;

  idhashXCD = IDHASH( idslotX , idslotC , idslotD ) ;

  dciluth = *headref ;
  if ( dciluth ) {
    dcilut = dciluth ;
    do {
      if ( IDMATCH( dcilut , idhashXCD , idslotX , idslotC , idslotD )) {
        CLINK_RESERVE(dcilut) ;
        HQASSERT(dcilut->nMappedColorants == nMappedColorants, "dcilut hashes out of step") ;
        return dcilut ;
      }
      dcilut = dcilut->next ;
    } while ( dcilut != dciluth ) ;
  }

  /* We didn't find an existing dcilut that matches, so create a new one */
  dcilut = mm_sac_alloc( mm_pool_color ,
                         sizeof( DCILUT ) ,
                         MM_ALLOC_CLASS_DCI_LUT ) ;
  if ( dcilut == NULL ) {
    ( void )error_handler( VMERROR ) ;
    return NULL ;
  }

  cacheSize = sizeof (DCILUT_CACHE) +
              CACHEHASHSIZE * sizeof (COLORVALUE) +                      /* cache->input */
              nMappedColorants * CACHEHASHSIZE * sizeof (COLORVALUE);    /* cache->output */

  /* We can plough on without dcilut caches in low memory, so don't error
   * if either fails */
  dcilut->normalCache = mm_sac_alloc(mm_pool_color,
                                     cacheSize,
                                     MM_ALLOC_CLASS_DCI_LUT);

  if (dcilut->normalCache != NULL) {
    /* Be careful to avoid alignment problems */
    dcilut->normalCache->input = (COLORVALUE *) (dcilut->normalCache + 1) ;
    dcilut->normalCache->output =
      (COLORVALUE (*)[CACHEHASHSIZE]) (dcilut->normalCache->input + CACHEHASHSIZE) ;
  }

  /* dcilut->nPopulated = 0 ; */
  HqMemSet16((uint16 *)dcilut->dciCodes, (uint16)DCILUT_CODE_notset, DCILUT_SIZE) ;

  if (dcilut->normalCache != NULL) {
    uint32 i;
    HqMemSet16((uint16 *) dcilut->normalCache->input,
            (uint16) DCILUT_CODE_notset, CACHEHASHSIZE) ;
    for ( i = 0 ; i < nMappedColorants ; ++i ) {
      HqMemSet16((uint16 *) dcilut->normalCache->output[i],
              (uint16) COLORVALUE_INVALID, CACHEHASHSIZE) ;
    }
  }

  IDCOPY( dcilut , idhashXCD , idslotX , idslotC , idslotD ) ;

  dcilut->refCnt = 1 ;
  dcilut->nMappedColorants = nMappedColorants ;

  dcilutaddhead( dcilut , headref ) ;

  return dcilut ;
}

static void dcilut_destroy( DCILUT *dcilut )
{
  mm_size_t cacheSize ;
  uint32 nMappedColorants = dcilut->nMappedColorants;

  HQASSERT( dcilut , "dcilut NULL in dcilut_destroy" ) ;

  cacheSize = sizeof (DCILUT_CACHE) +
              CACHEHASHSIZE * sizeof (COLORVALUE) +                      /* cache->input */
              nMappedColorants * CACHEHASHSIZE * sizeof (COLORVALUE);    /* cache->output */

  if (dcilut->normalCache != NULL) {
    mm_sac_free( mm_pool_color ,
                 dcilut->normalCache,
                 cacheSize) ;
  }
  mm_sac_free( mm_pool_color ,
               dcilut,
               sizeof( DCILUT )) ;
}

/* ---------------------------------------------------------------------- */
static void dcilut_free( DCILUT *dcilut )
{
  HQASSERT( dcilut , "dcilut NULL in dcilut_free" ) ;
  dcilutremove( dcilut , dcilut->headref ) ;
  dcilut_destroy(dcilut);
}

/* ---------------------------------------------------------------------- */
/* NONINTERCEPT LINK TYPE FUNCTIONS.

   The nonintercept link type is an alternative to the devicecode link. It simply
   does the overprint determination part of the devicecode link, and then combines
   the overprint information with the existing color instead of making a new
   color. */
/* ---------------------------------------------------------------------- */
static void cc_nonintercept_destroy( CLINK *pLink )
{
  noninterceptAssertions( pLink ) ;
  cc_common_destroy( pLink ) ;
}

/* ---------------------------------------------------------------------- */
CLINK *cc_nonintercept_create(GS_COLORinfo          *colorInfo,
                              GUCR_RASTERSTYLE      *hRasterStyle,
                              int32                 colorType,
                              GS_CONSTRUCT_CONTEXT  *context,
                              COLORSPACE_ID         jobColorSpace,
                              GS_DEVICECODEinfo     *devicecodeInfo,
                              CLINK                 *pHeadLink,
                              CLINK                 *devicecodeLink,
                              REPRO_COLOR_MODEL     chainColorModel,
                              int32                 headLinkBlackPos)
{
  int32 i;
  CLINK *pLink ;
  CLINKNONINTERCEPTinfo *nonintercept ;
  int32 nColorants ;
  int32 nDeviceColorants ;
  COLORANTINDEX *colorants ;
  COLORSPACE_ID colorSpace ;
  Bool jobColorSpaceIsGray ;
  TRANSFORMEDSPOT transformedSpotType ;
  OVERPRINTinfo *overprintInfo ;
  const GUCR_PHOTOINK_INFO *photoinkInfo = guc_photoinkInfo(hRasterStyle);
  COLOR_PAGE_PARAMS *colorPageParams;

  UNUSED_PARAM(GS_COLORinfo *, colorInfo;)

  colorPageParams = &context->page->colorPageParams;

  nColorants = context->colorspacedimension;
  colorants = context->pColorantIndexArray;
  nDeviceColorants = get_nDeviceColorants(nColorants, colorants, hRasterStyle);
  colorSpace = context->chainColorSpace;
  jobColorSpaceIsGray = context->jobColorSpaceIsGray;
  transformedSpotType = context->transformedSpotType;

  if (jobColorSpaceIsGray) {
    /* Make device independent color look like DeviceGray when appropriate for
     * overprints etc.
     */
    HQASSERT(jobColorSpace == SPACE_CalGray  || jobColorSpace == SPACE_CIEBasedA ||
             jobColorSpace == SPACE_ICCBased || jobColorSpace == SPACE_DeviceGray,
             "jobColorSpaceIsGray is inconsistent");
    jobColorSpace = SPACE_DeviceGray ;
  }

  /* We are not concerned about caching here, since there is no color directly
     produced as a result of this link type */

  pLink = cc_common_create( nColorants ,
                            colorants ,
                            colorSpace ,
                            colorSpace ,
                            CL_TYPEnonintercept ,
                            noninterceptStructSize( nColorants , nDeviceColorants ) ,
                            & CLINKnonintercept_functions ,
                            COLCACHE_DISABLE) ;
  if ( pLink == NULL )
    return NULL ;

  noninterceptUpdatePtrs( pLink , nColorants , nDeviceColorants) ;

  nonintercept = pLink->p.nonintercept ;

  /* Assign the mappings to translate photoink input colorants to the outputs */
  if (photoinkInfo != NULL) {
    COLORANTINDEX *cis ;
    int32         dev_i ;
    for ( i = 0, dev_i = 0 ; i < nColorants ; ++i ) {
      HQASSERT(colorants[i] != COLORANTINDEX_ALL,
               "/All sep should have been converted for photoink") ;
      if (colorants[i] == COLORANTINDEX_NONE) {
        nonintercept->oColorants[dev_i++] = colorants[i] ;
        nonintercept->nMappedColorants[i]++ ;
      }
      else {
        cis = guc_getColorantMapping( hRasterStyle, colorants[i] ) ;
        HQASSERT(cis != NULL, "Colorant mapping must exist for photoink") ;
        nonintercept->nMappedColorants[i] = 0;
        while (*cis != COLORANTINDEX_UNKNOWN) {
          nonintercept->oColorants[dev_i++] = *cis++ ;
          nonintercept->nMappedColorants[i]++ ;
        }
      }
    }
    HQASSERT(dev_i == nDeviceColorants, "colorant count error") ;
  }
  else {
    for ( i = 0 ; i < nColorants ; ++i ) {
      nonintercept->nMappedColorants[i] = 1 ;
      nonintercept->oColorants[i] = colorants[i] ;
    }
  }

  if ( nDeviceColorants > 0 ) {
    Bool dummy;
    sort_colorants( nDeviceColorants ,
                    nonintercept->oColorants ,
                    & nonintercept->nSortedColorants ,
                    nonintercept->sortedColorants ,
                    nonintercept->sortingIndexes ,
                    context->page->colorPageParams.duplicateColorants ,
                    &dummy ) ;
  }
  else
    nonintercept->nSortedColorants = 0 ;

  /* Point pHeadLink at the first link in this chain, or the base space of
   * and Indexed colorspace */
  if ( pHeadLink == NULL )
    nonintercept->pHeadLink = pLink ;
  else if ( pHeadLink->iColorSpace == SPACE_Indexed )
    nonintercept->pHeadLink = pHeadLink->pnext ? pHeadLink->pnext : pLink ;
  else
    nonintercept->pHeadLink = pHeadLink ;

  /* We may avoid maxblitting and produce a more normal overprinted dl_color
   * if the non-color managed colorants are a subset. */
  nonintercept->devicecodeLink = devicecodeLink ;
  nonintercept->mayAvoidMaxBlitting = (colorType == GSC_FILL || colorType == GSC_STROKE) &&
                                      !DeviceColorspaceIsAdditive(pLink->iColorSpace) ;
  if ( nonintercept->mayAvoidMaxBlitting ) {
    int d;
    for (i = 0, d = 0; i < nonintercept->nSortedColorants; i++) {
      CLINKDEVICECODEinfo *devicecode = devicecodeLink->p.devicecode ;
      while (d < devicecode->nSortedColorants &&
             nonintercept->sortedColorants[i] != devicecode->sortedColorants[d])
        d++ ;
      if (d == devicecode->nSortedColorants) {
        nonintercept->mayAvoidMaxBlitting = FALSE ;
        break ;
      }
    }
  }

  nonintercept->colorType = colorType ;
  nonintercept->nDeviceColorants = nDeviceColorants ;

  nonintercept->blackPosition = -1;
  if (!cc_getBlackPositionInOutputList(nColorants, colorants, hRasterStyle,
                                       &nonintercept->blackPosition)) {
    cc_nonintercept_destroy(pLink);
    return NULL;
  }
  nonintercept->allowImplicit = op_allow_implicit( colorType,
                                                   jobColorSpace,
                                                   nonintercept->pHeadLink,
                                                   context->matchingICCprofiles,
                                                   devicecodeInfo ) ;

  /* TRANSFORMEDSPOT is set when a chain involves using the alternative space
   * of a Separation/DeviceN. This is only relevant if specified in the job
   * rather than a by-product of interception etc. */
  HQASSERT(transformedSpotType == DC_TRANSFORMEDSPOT_NONE ||
           transformedSpotType == DC_TRANSFORMEDSPOT_NORMAL,
           /* NB. DC_TRANSFORMEDSPOT_INTERCEPT not allowed for intercept chains */
           "Unexpected value for TRANSFORMEDSPOT");
  nonintercept->fTransformedSpot = transformedSpotType != DC_TRANSFORMEDSPOT_NONE &&
                                   devicecodeInfo->transformedSpotOverprint &&
                                   (nonintercept->pHeadLink->iColorSpace == SPACE_Separation ||
                                    nonintercept->pHeadLink->iColorSpace == SPACE_DeviceN);

  for ( i = 0 ; i < nColorants ; ++i )
    if (nonintercept->fTransformedSpot)
      nonintercept->colorantsInSpotlist[i] = op_colorant_in_spotlist( pLink->iColorants[ i ] ,
                                                                      nonintercept->pHeadLink ,
                                                                      hRasterStyle ) ;
    else
      /* colorantsInSpotlist will not be accessed */
      nonintercept->colorantsInSpotlist[i] = -1 ;

  /* Copy relevant info into the overprint structure */
  overprintInfo = &nonintercept->overprintInfo ;
  overprintInfo->colorType           = nonintercept->colorType ;
  overprintInfo->blackPosition       = nonintercept->blackPosition ;
  overprintInfo->jobColorSpaceIsGray = context->jobColorSpaceIsGray ;
  overprintInfo->setoverprint        = getoverprint(devicecodeInfo, colorType) ;
  overprintInfo->overprintMode       = devicecodeInfo->overprintMode ;
  overprintInfo->overprintBlack      = devicecodeInfo->overprintBlack ;
  overprintInfo->overprintWhite      = devicecodeInfo->overprintWhite ;
  overprintInfo->fTransformedSpot    = nonintercept->fTransformedSpot ;
  overprintInfo->colorantsInSpotlist = nonintercept->colorantsInSpotlist ;
  overprintInfo->additive            = DeviceColorspaceIsAdditive(pLink->iColorSpace) ;
  overprintInfo->allowImplicit       = nonintercept->allowImplicit ;
  overprintInfo->nMappedColorants    = nonintercept->nMappedColorants ;
  overprintInfo->overprintMask       = nonintercept->overprintMask ;
  overprintInfo->hRasterStyle        = hRasterStyle ;
  overprintInfo->nDeviceColorants    = nDeviceColorants ;
  /* This link is always called in the front-end */
  overprintInfo->fCompositing        = FALSE ;
  overprintInfo->pHeadLink           = nonintercept->pHeadLink ;
  overprintInfo->headLinkBlackPos    = headLinkBlackPos ;
  overprintInfo->chainColorModel     = chainColorModel ;
  overprintInfo->tableBasedColor     = colorPageParams->tableBasedColor ;
  overprintInfo->overprintsEnabled   = cc_overprintsEnabled(colorInfo, colorPageParams);
  HQASSERT(overprintInfo->overprintsEnabled,
           "Shouldn't be here when overprinting is disabled");

  /* This page context will be used by dl_colors from invoke functions */
  nonintercept->page = context->page;

  noninterceptAssertions( pLink ) ;

  return pLink;
}

/* ---------------------------------------------------------------------- */
static Bool cc_nonintercept_invokeSingle( CLINK *pLink , USERVALUE *dummy_oColorValue )
{
  /* This is basically a subset of the code for ordinary device codes which only
     deals with overprinting */
  int32 i , nOverprinted , nSorted;
  int32 nDeviceColorants ;
  CLINKNONINTERCEPTinfo *nonintercept ;
  int32 *sortingIndexes ;
  Bool fDoOverprintReduction ;
  dlc_context_t *dlc_context ;
  dl_color_t *dlc_current ;

  UNUSED_PARAM( USERVALUE * , dummy_oColorValue ) ;

  noninterceptAssertions( pLink ) ;
  nonintercept = pLink->p.nonintercept ;

  dlc_context = nonintercept->page->dlc_context ;
  dlc_current = dlc_currentcolor(dlc_context) ;

  if ( dlc_is_black(dlc_current, COLORANTINDEX_NONE) )
    return TRUE ;

  nDeviceColorants = nonintercept->nDeviceColorants ;

  /* calculate overprints, the main purpose of this function */
  if (!op_decide_overprints(pLink ,
                            pLink->iColorValues ,
                            pLink->overprintProcess ,
                            &nonintercept->overprintInfo ,
                            &fDoOverprintReduction)) {
    /* It turns out that overprinting isn't possible. Leave dlc_current
       unchanged. */
    return TRUE;
  }

  /* Solve a problem of HVD refusing to optimise jobs when maxblitting is used
   * on the DL.
   * Replace the dlc_current if the non-managed color is deemed close enough
   * to the managed color currently in dlc_current. Close enough means that
   * all input color values to this color link must be within a tolerance for
   * the same colorants in the devicecode link. Any extra colorants in the
   * devicecode link must be white within a tolerance.
   * The replacement dlc_current will be a normal overprint (i.e. excluding
   * the overprinted colorants rather than max blitting them). */
  if ( nonintercept->mayAvoidMaxBlitting ) {
    int d;
    int dSorted = 0;
    int iSorted = 0;
    int nReplace = 0;
    COLORANTINDEX *replaceColorants = nonintercept->replaceColorants;
    COLORVALUE *replaceColorValues = nonintercept->replaceColorValues;
    CLINK *devicecodeLink = nonintercept->devicecodeLink;
    CLINKDEVICECODEinfo *devicecode = devicecodeLink->p.devicecode ;

    /* Define a tolerance that happens to be good enough. */
#define TOLERANCE   (1.0f/4096)
    i = 0;
    for ( d = 0; d < devicecode->nDeviceColorants; d++ ) {
      int dIdx = devicecode->sortingIndexes[d];
      int iIdx;

      /* Skip over colorants from this link culled in sort_colorants() */
      while ( i < nonintercept->nDeviceColorants && nonintercept->sortingIndexes[i] < 0 )
        i++;
      iIdx = nonintercept->sortingIndexes[i];

      /* Skip over colorants from this link culled in sort_colorants() */
      if ( dIdx >= 0 ) {
        if ( iIdx >= 0 &&
             devicecode->sortedColorants[dSorted] == nonintercept->sortedColorants[iSorted] ) {
          if ( fabs(devicecodeLink->iColorValues[dIdx] - pLink->iColorValues[iIdx]) > TOLERANCE )
            break;
          if ( PAINT_COLORANT( nonintercept->overprintMask, iIdx )) {
            replaceColorants[nReplace] = devicecode->sortedColorants[dSorted];
            replaceColorValues[nReplace] = devicecode->sortedColorValues[dSorted];
            nReplace++;
          }
          i++;
          iSorted++;
        }
        else {
          if ( devicecodeLink->iColorValues[dIdx] > TOLERANCE )
            break;
        }
        dSorted++;
      }
    }

    if ( d == devicecode->nSortedColorants ) {
      /* The color managed and non-color managed colors are close enough.
       * Don't maxblit, just overprint with the colorant set from here, but with
       * the color values from the devicecode link */
      dlc_release(dlc_context, dlc_current);

      if ( nReplace == 0 ) {
        /* overprinting everything, send to none sep to remove object. */
        replaceColorants[0] = COLORANTINDEX_NONE;
        nReplace = 1 ;
      }
      return dlc_alloc_fillin(dlc_context,
                              nReplace,
                              replaceColorants,
                              replaceColorValues,
                              dlc_current);
    }
  }

  if (!fDoOverprintReduction) {
    return dlc_apply_overprints(nonintercept->page->dlc_context,
                                DLC_MAXBLT_KNOCKOUTS, DLC_INTERSECT_OP,
                                nonintercept->nSortedColorants,
                                nonintercept->sortedColorants,
                                dlc_current);
  }

  /* reduce the resultant colors based on overprints. We don't care about the color
     values, only the colorants remaining present after this */
  sortingIndexes = nonintercept->sortingIndexes ;
  for ( i = nOverprinted = nSorted = 0 ; i < nDeviceColorants ; ++i ) {
    int32 nOriginalIndex = sortingIndexes[ i ] ;
    if (nOriginalIndex >= 0) {
      /* note: negative sorting indexes arise if we have the same named colorant
         more than once in a DeviceN color space; only one of them actually takes
         effect, hence the omission */
      if ( ! PAINT_COLORANT( nonintercept->overprintMask , nOriginalIndex ))
        nonintercept->overprintedColorants [nOverprinted++] =
          nonintercept->sortedColorants [nSorted] ;
      nSorted++;
    }
  }
  nonintercept->n_overprintedColorants = nOverprinted ;

  /* merge newly derived overprinting information with existing dlc_current */
  return dlc_apply_overprints(nonintercept->page->dlc_context,
                              DLC_MAXBLT_OVERPRINTS, DLC_INTERSECT_OP,
                              nonintercept->n_overprintedColorants,
                              nonintercept->overprintedColorants,
                              dlc_current);
}

/* ---------------------------------------------------------------------- */
static Bool cc_nonintercept_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  int32 nColors ;
  int32 nColorants ;
  int32 nDeviceColorants ;
  CLINKNONINTERCEPTinfo *nonintercept ;
  GS_BLOCKoverprint *blockOverprint ;
  USERVALUE *colorValues ;
  uint8     *overprintProcess ;

  HQASSERT(pBlock, "cc_nonintercept_invokeBlock block args pointer NULL") ;
  HQASSERT(pBlock->iColorValues, "cc_nonintercept_invokeBlock colorValues input pointer NULL") ;
  HQASSERT(pBlock->nColors > 0, "cc_nonintercept_invokeBlock nColors too small") ;

  noninterceptAssertions( pLink ) ;
  nonintercept = pLink->p.nonintercept ;

  nColorants = pLink->n_iColorants ;
  nDeviceColorants = nonintercept->nDeviceColorants ;

  nColors = pBlock->nColors ;
  colorValues = pBlock->iColorValues ;
  overprintProcess = pBlock->overprintProcess;
  blockOverprint = pBlock->blockOverprint ;

  HQASSERT(blockOverprint, "blockOverprint should not be NULL in nonintercept invokeBlock") ;
  HQASSERT(nColors <= GSC_BLOCK_MAXCOLORS, "nColors too big");

  do {
    Bool doOverprintReduction ;

    (void)op_decide_overprints(pLink ,
                               colorValues ,
                               overprintProcess[0],
                               &nonintercept->overprintInfo ,
                               & doOverprintReduction) ;

    if ( !doOverprintReduction ) {
      int32 i ;

      /* Create inverse of sortedColorants set and intersect with mask. This
         is for transformed spot case, and performs the same function as the
         DLC_MAXBLT_KNOCKOUT opcode would have. */
      SET_OVERPRINTMASK(nonintercept->overprintMask, nDeviceColorants,
                        OVERPRINTMASK_OVERPRINT) ;
      for ( i = nonintercept->nSortedColorants ; --i >= 0 ; ) {
        KNOCKOUT_COLORANT(nonintercept->overprintMask,
                          nonintercept->sortedColorants[i]) ;
      }
    }

    /* This sets blockOverprint->overprintAll to FALSE if there are no
       more overprints, and TRUE if there are, which is OK, because we
       wouldn't be here if it wasn't TRUE beforehand. */
    INTERSECT_OVERPRINTMASK(blockOverprint->overprintMask,
                            nonintercept->overprintMask,
                            nDeviceColorants,
                            blockOverprint->overprintAll) ;

    if ( !blockOverprint->overprintAll )
      return TRUE ;

    colorValues += nColorants ;
    overprintProcess++ ;
  } while ( --nColors > 0 ) ;

  blockOverprint->overprintOpCode = DLC_MAXBLT_OVERPRINTS ;
  blockOverprint->overprintUseMask = TRUE ;

  return TRUE;
}


/* ---------------------------------------------------------------------- */
/* cc_nonintercept_scan - Not currently necessary */
static mps_res_t cc_nonintercept_scan( mps_ss_t ss, CLINK *pLink )
{
  UNUSED_PARAM(mps_ss_t, ss);
  UNUSED_PARAM(CLINK *, pLink);

  /* Don't scan pLink->p.nonintercept->pHeadLink because it will lead to an
   * infinite loop
   */

  return MPS_RES_OK;
}


/* ---------------------------------------------------------------------- */
static size_t noninterceptStructSize(int32 nColorants, int32 nDeviceColorants)
{
  return sizeof( CLINKNONINTERCEPTinfo ) +
         OVERPRINTMASK_BYTESIZE(nDeviceColorants) +
         nColorants       * sizeof( int32 ) +         /* colorantsInSpotlist */
         nColorants       * sizeof( uint32 ) +        /* nMappedColorants */
         nDeviceColorants * sizeof( COLORANTINDEX ) + /* oColorants */
         nDeviceColorants * sizeof( int32 ) +         /* sortingIndexes */
         nDeviceColorants * sizeof( COLORANTINDEX ) + /* sortedColorants */
         nDeviceColorants * sizeof( COLORANTINDEX ) + /* replaceColorants */
         nDeviceColorants * sizeof( COLORVALUE ) +    /* replaceColorValues */
         nDeviceColorants * sizeof( COLORANTINDEX );  /* overprintedColorants */
}

/* ---------------------------------------------------------------------- */
static void noninterceptUpdatePtrs( CLINK *pLink , int32 nColorants , int32 nDeviceColorants )
{
  CLINKNONINTERCEPTinfo * nonintercept;

  HQASSERT( pLink , "pLink NULL in noninterceptUpdatePtrs" ) ;

  nonintercept = ( CLINKNONINTERCEPTinfo * )(( uint8 * )pLink + cc_commonStructSize( pLink )) ;

  if ( nColorants > 0 ) {
    nonintercept->overprintMask       = ( uint32 * )( nonintercept + 1 ) ;
    nonintercept->colorantsInSpotlist = ( int32 * )( nonintercept->overprintMask + OVERPRINTMASK_WORDSIZE( nDeviceColorants ) ) ;
    nonintercept->nMappedColorants    = ( uint32 * )( nonintercept->colorantsInSpotlist + nColorants ) ;
    nonintercept->oColorants          = ( COLORANTINDEX * )( nonintercept->nMappedColorants + nColorants ) ;
    nonintercept->sortingIndexes      = ( int32 * )( nonintercept->oColorants + nDeviceColorants ) ;
    nonintercept->sortedColorants     = ( COLORANTINDEX * )( nonintercept->sortingIndexes + nDeviceColorants ) ;
    nonintercept->replaceColorants    = ( COLORANTINDEX * )( nonintercept->sortedColorants + nDeviceColorants ) ;
    nonintercept->replaceColorValues  = ( COLORVALUE * )( nonintercept->replaceColorants + nDeviceColorants ) ;
    nonintercept->overprintedColorants= ( COLORANTINDEX * )( nonintercept->replaceColorValues + nDeviceColorants ) ;
  }
  else {
    nonintercept->overprintMask       = NULL ;
    nonintercept->colorantsInSpotlist = NULL ;
    nonintercept->nMappedColorants    = NULL ;
    nonintercept->oColorants          = NULL ;
    nonintercept->sortingIndexes      = NULL ;
    nonintercept->sortedColorants     = NULL ;
    nonintercept->replaceColorants    = NULL ;
    nonintercept->replaceColorValues  = NULL ;
    nonintercept->overprintedColorants= NULL ;
  }

  pLink->p.nonintercept = nonintercept;
}

/* ---------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )
static void noninterceptAssertions( CLINK *pLink )
{
  CLINKNONINTERCEPTinfo *nonintercept ;
  int32                 nColorants ;
  int32                 nDeviceColorants ;
  int32                 i ;
  int32                 tmp ;

  nonintercept = pLink->p.nonintercept ;
  nColorants = pLink->n_iColorants ;
  nDeviceColorants = nonintercept->nDeviceColorants ;

  cc_commonAssertions( pLink ,
                       CL_TYPEnonintercept ,
                       noninterceptStructSize( nColorants, nDeviceColorants ) ,
                       & CLINKnonintercept_functions ) ;

  if ( nColorants > 0 ) {
    HQASSERT(nonintercept->overprintMask      ==
             ( uint32 * )( nonintercept + 1 ) ,
              "overprintMask not set" ) ;

    HQASSERT(nonintercept->colorantsInSpotlist  == ( int32 * )( nonintercept->overprintMask + OVERPRINTMASK_WORDSIZE( nDeviceColorants ) ) ,
             "colorantsInSpotlist not set" ) ;
    HQASSERT(nonintercept->nMappedColorants     == ( uint32 * )( nonintercept->colorantsInSpotlist + nColorants ) ,
             "nMappedColorants not set" ) ;
    HQASSERT(nonintercept->oColorants           == ( int32 * )( nonintercept->nMappedColorants + nColorants ) ,
             "oColorants not set" ) ;
    HQASSERT(nonintercept->sortingIndexes       == ( int32 * )( nonintercept->oColorants + nDeviceColorants ) ,
             "sortingIndexes not set" ) ;
    HQASSERT(nonintercept->sortedColorants      == ( COLORANTINDEX * )( nonintercept->sortingIndexes + nDeviceColorants ) ,
             "sortedColorants not set" ) ;
    HQASSERT(nonintercept->replaceColorants     == ( COLORANTINDEX * )( nonintercept->sortedColorants + nDeviceColorants ) ,
             "replaceColorants not set" ) ;
    HQASSERT(nonintercept->replaceColorValues   == ( COLORVALUE * )( nonintercept->replaceColorants + nDeviceColorants ) ,
             "replaceColorValues not set" ) ;
    HQASSERT(nonintercept->overprintedColorants == ( COLORANTINDEX * )( nonintercept->replaceColorValues + nDeviceColorants ) ,
             "overprintedColorants not set" ) ;

    tmp = 0 ;
    for (i = 0; i < nColorants; i++)
      tmp += nonintercept->nMappedColorants[i] ;
    HQASSERT(nDeviceColorants == tmp, "inconsistent colorant mappings") ;
  }
  else {
    HQASSERT( nonintercept->overprintMask == NULL ,
              "overprintMask should be null" ) ;
  }

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceGray:
  case SPACE_DeviceK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_Pattern: /* allowed for colored patterns */
  case SPACE_PatternMask: /* and for painting the pattern mask for uncolored patterns */
    break;
  default:
    HQFAIL( "Bad input color space" ) ;
    break ;
  }
}
#endif

/* ---------------------------------------------------------------------- */
/* Determine whether implicit overprinting is possibly allowed from the
 * static info during link creation. This will be augmented by the actual
 * color values during the invoke.
 * Note that the job colorspace may be different to the colorspace in
 * the head link (if the job and output colorspaces are DeviceGray,
 * and we are using a pattern screen then cc_constructChain maps the
 * job colorspace onto DeviceN (for All separation) - and its this
 * colorspace stored in the head (devicecode) clink.
 */
static Bool op_allow_implicit( int32              colorType,
                               COLORSPACE_ID      jobColorSpace,
                               CLINK              *pHeadLink,
                               Bool               matchingICCprofiles,
                               GS_DEVICECODEinfo  *devicecodeInfo )
{
  /* Dont implicitly overprint unless overprintMode is true */
  if ( ! devicecodeInfo->overprintMode )
    return FALSE ;

  if (colorType != GSC_FILL &&
      colorType != GSC_STROKE &&
      colorType != GSC_VIGNETTE)
    return FALSE;

  /* Patterns are either colored, and have no base, or uncolored, in
     which case the base colorspace is already in effect at this point */
  switch ( jobColorSpace ) {
  case SPACE_DeviceN:
  case SPACE_Separation:
  case SPACE_Indexed:
    {
      CLINK *pLink = pHeadLink ;

      for (;;) {
        switch ( pLink->iColorSpace ) {
          /* With spots, it is assumed the user has already chosen
             what is to be overprinted with the inclusion/exclusion of
             spots. */
        case SPACE_DeviceN:
        case SPACE_Separation:
          return FALSE ;

        case SPACE_Indexed:
          pLink = pLink->pnext ;
          HQASSERT( pLink, "op_allow_implicit: indexed colorspace with no base" ) ;
          break ; /* Consider the base colorspace */

        case SPACE_DeviceGray:
          return devicecodeInfo->overprintGray ;

        case SPACE_DeviceCMYK:
          return TRUE;

        case SPACE_ICCBased:
          /* Allow implicit overprinting of CMYK ICCBased spaces when the option is on */
          return devicecodeInfo->overprintICCBased &&
                 pHeadLink->n_iColorants == 4 &&
                 matchingICCprofiles;

        default:
          /* Any other colorspace, implicit overprints disallowed */
          return FALSE;
        }
      }
    }
    break ;

  case SPACE_DeviceGray:
    return devicecodeInfo->overprintGray;

  case SPACE_DeviceCMYK:
    return TRUE;

  case SPACE_ICCBased:
    /* Allow implicit overprinting of CMYK ICCBased spaces when the option is on */
    return devicecodeInfo->overprintICCBased &&
           pHeadLink->n_iColorants == 4 &&
           matchingICCprofiles;

  default:
    /* Any other colorspace, implicit overprints disallowed */
    return FALSE;
  }
}

/* ---------------------------------------------------------------------- */
static Bool op_colorant_in_spotlist( COLORANTINDEX    ci,
                                     CLINK            *pheadlink,
                                     GUCR_RASTERSTYLE *hRasterStyle )
{
  /* Determines whether colorant, one of the final colorants of the device, is also
     present in the original DeviceN or Separation space from which the final color
     was derived.

     This is so that we can decide whether to try to simulate a spot color converted
     to process by maxblting the colorants which were not present originally. If we
     had a PANTONE Separation space, for example, then we would maxblt all colorants
     by returning FALSE from here; but if we had Separation Cyan, we mustn't maxblt
     the Cyan or we could lose the shape altogether */

  int32 ncolorants , i ;
  COLORANTINDEX *spotcolorants ;

  HQASSERT( pheadlink != NULL , "op_colorant_in_spotlist: pheadlink null" ) ;
  HQASSERT( pheadlink->iColorSpace == SPACE_Separation ||
            pheadlink->iColorSpace == SPACE_DeviceN ,
            "Expect the head colorspace to be either a Separation or DeviceN" ) ;

  ncolorants = pheadlink->n_iColorants ;
  spotcolorants = pheadlink->iColorants ;

  for ( i = 0 ; i < ncolorants ; ++i )
    if ( ci == spotcolorants[ i ] )
      return TRUE ;

  /* While the colorant under consideration may not be in the list of colors in the
     original DeviceN, it may be there is a color which is the source of a photoink
     color (for example colorant is the index for 'Photo Cyan'), in which case we
     really ought to match it with 'Cyan' in the original color space for the
     purposes of this test. This is common, especially in recombine, where overprints
     are fabricated as DeviceN spaces */

  HQASSERT( ci != COLORANTINDEX_ALL &&
            ci != COLORANTINDEX_NONE &&
            ci != COLORANTINDEX_UNKNOWN ,
            "op_colorant_in_spotlist: ci either all, none or unknown" ) ;
  ci = guc_getInverseColorant( hRasterStyle , ci ) ;
  if ( ci != COLORANTINDEX_UNKNOWN ) {
    HQASSERT( ci != COLORANTINDEX_ALL &&
              ci != COLORANTINDEX_NONE ,
              "op_colorant_in_spotlist: ci either all or none" ) ;
    for ( i = 0 ; i < ncolorants ; ++i )
      if ( ci == spotcolorants[ i ] )
        return TRUE ;
  }

  /* Colorant not in original spot list */
  return FALSE ;
}

/* ---------------------------------------------------------------------- */
/* Erase color is 0.0 for subtractive colorspaces and 1.0 for additive
 * colorspaces at this point. Inverted jobs are forced positive and
 * then inverted at the end. Transfer functions and calibration have
 * yet to be applied.
 * For the benefit of photoink devices, the input colorants may be mapped
 * onto an output set of colorants with a 1 to m mapping. Hence there
 * are 2 levels of looping when setting the overprints.
 */
static Bool op_decide_overprints(CLINK         *pLink,
                                 USERVALUE     *colorValues,
                                 uint8         overprintProcess,
                                 OVERPRINTinfo *overprintInfo,
                                 Bool          *implicitOverprinting)
{
  int32 nColorants = pLink->n_iColorants;
  Bool forceOverprintGray;

  *implicitOverprinting = FALSE ;

  if ( overprintInfo->overprintsEnabled ) {
    int32 colorType = overprintInfo->colorType ;
    Bool overprinting = overprintInfo->setoverprint &&
                        (pLink->overprintProcess & OP_DISABLED) == 0 ;

    /* Do not do implicit or black overprinting for normal image color chains
     * or in a back-end chain - this overprinting is done in the front end.
     */
    if ( colorType != GSC_IMAGE && !overprintInfo->fCompositing ) {
      Bool overprintBlack = FALSE ;
      int32 blackPosition = overprintInfo->blackPosition ;
      USERVALUE opColorMax ;
      USERVALUE opColorMin ;

      if ( overprintInfo->additive ) {
        opColorMax = 0.0f ;
        opColorMin = 1.0f ;
      }
      else {
        opColorMax = 1.0f ;
        opColorMin = 0.0f ;
      }

      /* If an output black colorant exists, and the input color has a maximum
       * black component, then can do 100% black overprinting (if switched on).
       */
      if ( blackPosition >= 0 &&
           overprintInfo->overprintBlack &&
           (colorType == GSC_FILL || colorType == GSC_STROKE) ) {
        /* We only allow overprinting of pure black in the job.
         * As an optimisation, we can rely on black preservation to tell us that
         * an input color was pure black.
         */
        if (pLink->blackType == BLACK_TYPE_100_PC)
          overprintBlack = TRUE;
        else {
          GSC_BLACK_TYPE origBlackType;

          /* Discover if the original color was 100% black. The easiest way to
           * do that is to use a black preservation method.
           */
          origBlackType = cc_blackInput(TRUE,
                                        FALSE,
                                        overprintInfo->chainColorModel,
                                        overprintInfo->pHeadLink->iColorValues,
                                        overprintInfo->pHeadLink->n_iColorants,
                                        overprintInfo->headLinkBlackPos,
                                        NULL);

          overprintBlack = (origBlackType == BLACK_TYPE_100_PC);
        }
      }

      if ( overprinting || overprintBlack ) {
        int32   i ;
        uint32  pi_idx ;
        int32   cOutputColorant = 0 ;
        int32   overprintCount = 0 ;
        Bool    fTransformedSpot = overprintInfo->fTransformedSpot ;
        uint32  *overprintMask = overprintInfo->overprintMask ;
        uint32  *nMappedColorants = overprintInfo->nMappedColorants ;
        Bool    fAllMin ;

        SET_OVERPRINTMASK( overprintMask, nColorants, OVERPRINTMASK_KNOCKOUT ) ;

        fAllMin = TRUE ; /* All are min unless proven otherwise */

        if ( fTransformedSpot ) {
          /* Special case when a spot color has been transformed to its alternative
           * colorspace. In this case we max blt any colorants that were not in the
           * original list of spots. For example, a color with Cyan, Magenta and
           * Orange spots going to CMYK. The Cyan and Magenta must always paint
           * (not max blt) and the Yellow and Black must max blt.
           */

          int32   *colorantsInSpotlist = overprintInfo->colorantsInSpotlist ;

          for ( i = 0 ; i < nColorants ; ++i ) {

            if ( colorValues[ i ] != opColorMin )
              fAllMin = FALSE ;

            HQASSERT( colorantsInSpotlist[ i ] >= 0,
                     "colorantsInSpotlist not set up correctly" ) ;
            if ( ! colorantsInSpotlist[ i ] ) {
              for ( pi_idx = 0 ; pi_idx < nMappedColorants[i] ; ++pi_idx )
                OVERPRINT_COLORANT( overprintMask , cOutputColorant + pi_idx ) ;
              overprintCount += 1 ;
            }
            cOutputColorant += nMappedColorants[i] ;
          }
        }
        else {
          /* General overprinting case */
          Bool fSharedColorants ;
          Bool allowImplicit ;

          /* Implicit overprinting is allowed if both the original value of
           * allowImplicit (the tests on static data) and overprintProcess (the
           * test on dynamic data) allow it.
           */
          allowImplicit = overprintInfo->allowImplicit ;

          fSharedColorants = FALSE ;
          if ( pLink->iColorSpace == SPACE_DeviceN )
            if ( guc_getColorantMapping( overprintInfo->hRasterStyle , COLORANTINDEX_ALL ))
              fSharedColorants = TRUE ;

          for ( i = 0 ; i < nColorants ; ++i ) {
            /* In the following cases the colorant is overprinted:
             *  1. black overprinting
             *  2. overprinting true, allowImplicit true, color value 0.0f
             *  3. overprinting true, o/pProcess bit flag set, color value 0.0f
             */
            Bool fOpColorMin = colorValues[ i ] == opColorMin ;

            /* But qualify that if this is part of a common set of colorants, then
             * they must all be opColorMin
             */
            if ( fOpColorMin && fSharedColorants ) {
              /* Which set of colorants, if any, am I in? */
              COLORANTINDEX ci = pLink->iColorants[ i ] ;

              HQASSERT( ci != COLORANTINDEX_UNKNOWN ,
                        "op_decide_overprints: ci unknown" ) ;
              if ( ci != COLORANTINDEX_ALL && ci != COLORANTINDEX_NONE ) {
                ci = guc_getInverseColorant( overprintInfo->hRasterStyle , ci ) ;
                if ( ci != COLORANTINDEX_UNKNOWN ) {
                  COLORANTINDEX *cis ;
                  cis = guc_getColorantMapping( overprintInfo->hRasterStyle , ci ) ;
                  HQASSERT( cis != NULL , "Given inverse mapping should be colorants" ) ;
                  while (( ci = (*cis++)) != COLORANTINDEX_UNKNOWN ) {
                    int32 j ;
                    HQASSERT( ci != COLORANTINDEX_ALL &&
                              ci != COLORANTINDEX_NONE ,
                              "op_decide_overprints: ci either all or none" ) ;
                    for ( j = 0 ; j < nColorants ; ++j ) {
                      if ( ci == pLink->iColorants[ j ] &&
                           colorValues[ j ] != opColorMin ) {
                        fOpColorMin = FALSE ;
                        break ;
                      }
                    }
                    if ( ! fOpColorMin )
                      break ;
                  }
                }
              }
            }

            if ( ! fOpColorMin )
              fAllMin = FALSE ;

            if ( ( overprintBlack && ( i != blackPosition )) ||
                 ( overprinting && fOpColorMin &&
                   ( allowImplicit ||
                     ( ( nColorants == 4 ) && ( overprintProcess & ( 1 << i ))))) ) {
              for ( pi_idx = 0 ; pi_idx < nMappedColorants[i] ; ++pi_idx )
                OVERPRINT_COLORANT( overprintMask , cOutputColorant + pi_idx ) ;
              overprintCount += 1 ;
            }

            cOutputColorant += nMappedColorants[i] ;
          }
        }

        /* This is a special case for gray because gray and white overprinting
         * is ambiguous when we have 1 setgray and both OverprintWhite and
         * OverprintGray are FALSE. We obey OverprintWhite in preference. This is
         * equivalent to getting a return value of TRUE from op_allowImplicit when
         * for 1 setgray and overprintMode is true.
         */
        forceOverprintGray = overprintInfo->jobColorSpaceIsGray &&
                             overprintInfo->overprintMode;

        /* Behaviour when implicitly overprinting all given colorants is controlled
         * by overprintWhite. If FALSE, is to knock out all other colorants, ie. we
         * implicitly turn off overprinting.
         * NB. OverprintWhite is effectively always on for SHFILL chains and
         *     transparent linework images.
         */
        if ( (overprintCount == overprintInfo->nDeviceColorants || forceOverprintGray) &&
             fAllMin &&
             !overprintInfo->overprintWhite &&
             colorType != GSC_SHFILL &&
             colorType != GSC_SHFILL_INDEXED_BASE &&
             colorType != GSC_VIGNETTE )
          return FALSE ;
        else {
          *implicitOverprinting = ( overprintCount > 0 ) ;
          return TRUE ;
        }
        /* NOT REACHED */
      }
      else
        return FALSE ;
    }
    else
      return overprinting ;
  }
  else
    return FALSE ;
}

/* A utility function to help decide if shfills and color managed chains will
 * allow IMPLICIT or MAXBLIT overprinting. If so, the clients will do something
 * special (inefficient) to cope with the overprinting and setup the appropriate
 * maxblts.
 * NB. It is important that this function closely follows the rules in
 *     op_decide_overprints for the color managed chains. The consequence of not
 *     following that requirement is that maxblt overprinting may be applied
 *     inappropriately, eg. maxblt'ing of cmy into a gray linework.
 * NB2. For images, allowImplicit is always false. For Separation/DeviceN,
 *     fTransformedSpot is usually false unless all colorants are renderable.
 */
static Bool cc_isoverprintpossible(CLINKDEVICECODEinfo *devicecode)
{
  COLORSPACE_ID jobColorSpace;
  Bool currentoverprint;
  int32 colorType;

  HQASSERT(devicecode, "devicecode NULL") ;

  colorType = devicecode->colorType;
  jobColorSpace = devicecode->jobColorSpace;

  currentoverprint = getoverprint(devicecode->devicecodeInfo, colorType);

  if (devicecode->overprintsEnabled) {
    if (!devicecode->fCompositing) {
      if (devicecode->devicecodeInfo->overprintBlack &&
          (colorType == GSC_FILL || colorType == GSC_STROKE))
        return TRUE;
      else if (currentoverprint) {
        if (devicecode->fTransformedSpot ||
            devicecode->allowImplicit)
          /* NB. Max blitting should also depend on overprintProcess, but I'm
           * going to deliberately ignore it because a) it's from Level 1 and
           * hence very old, b) it's a dynamic test while all others can
           * be done in chain construction, c) max blitting will still work if
           * overprintmode is on (the default).
           */
          return TRUE;

        if (colorType == GSC_IMAGE &&
            devicecode->devicecodeInfo->overprintGrayImages &&
            jobColorSpace == SPACE_DeviceGray)
          return TRUE;
      }
    }
  }

  return FALSE;
}

Bool gsc_isoverprintpossible(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo *colorChain ;
  CLINK *pLink;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_setoverprint");
  HQASSERT(colorType == GSC_SHFILL, "gsc_isoverprintpossible is only used for shfills");

  colorChain = colorInfo->chainInfo[colorType];
  HQASSERT(colorChain != NULL, "colorchain NULL");
  HQASSERT(colorChain->context->pnext != NULL, "pLink NULL");

  pLink = colorChain->context->pnext;
  while (pLink->pnext != NULL)
    pLink = pLink->pnext;

  if (pLink->linkType == CL_TYPEpresep)
    return FALSE;

  HQASSERT(pLink->linkType == CL_TYPEdevicecode, "Invalid linkType");

  return cc_isoverprintpossible(pLink->p.devicecode);
}

Bool cc_isoverprintblackpossible(GS_COLORinfo *colorInfo, int32 colorType,
                                 Bool fCompositing)
{
  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  COLORTYPE_ASSERT(colorType, "gsc_setoverprint");

  if (colorInfo->devicecodeInfo->overprintsEnabled) {
    if (!fCompositing) {
      if (colorInfo->devicecodeInfo->overprintBlack &&
          (colorType == GSC_FILL || colorType == GSC_STROKE))
        return TRUE;
    }
  }

  return FALSE;
}

/* ---------------------------------------------------------------------- */
Bool gsc_setoverprint( GS_COLORinfo *colorInfo, int32 colorType, Bool overprint )
{
  int32 fillOrStroke;

  HQASSERT(colorInfo, "colorInfo NULL in gsc_setoverprint" ) ;
  COLORTYPE_ASSERT(colorType, "gsc_setoverprint");
  HQASSERT(overprint == 0 || overprint == 1, "Invalid overprint value");

  fillOrStroke = (colorType == GSC_STROKE ? GSC_STROKE : GSC_FILL) ;

  if (colorInfo->devicecodeInfo == NULL) {
    /* This call creates the initial devicecodeInfo at rip startup */
    if (!createdevicecodeinfo(&colorInfo->devicecodeInfo))
      return FALSE;
  }

  if ( colorInfo->devicecodeInfo->overprinting[fillOrStroke] != overprint ) {
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, FALSE ))
      colorInfo->devicecodeInfo->overprinting[fillOrStroke] = overprint ;
    else
      return FALSE;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool gsc_getoverprint( GS_COLORinfo *colorInfo, int32 colorType )
{
  return getoverprint(colorInfo->devicecodeInfo, colorType);
}

static Bool getoverprint( GS_DEVICECODEinfo *devicecodeInfo, int32 colorType )
{
  int32 fillOrStroke;

  HQASSERT(devicecodeInfo, "devicecodeInfo NULL" ) ;
  COLORTYPE_ASSERT(colorType, "getoverprint");

  fillOrStroke = (colorType == GSC_STROKE ? GSC_STROKE : GSC_FILL) ;

  return devicecodeInfo->overprinting[ fillOrStroke ] ;
}

/* ---------------------------------------------------------------------- */

/** Temporarily disable overprinting for this gstate. This works in a similar
 * way to the Overprint systemparam, but locally for this gstate.
 */
Bool gsc_disableOverprint(GS_COLORinfo *colorInfo)
{
  if (colorInfo->devicecodeInfo->overprintsEnabled) {
    /* This will invalidate the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains(colorInfo, FALSE))
      colorInfo->devicecodeInfo->overprintsEnabled = FALSE;
    else
      return FALSE;
  }

  return TRUE;
}

/** Revert the temporary disabling of overprinting for this gstate */
Bool gsc_enableOverprint(GS_COLORinfo *colorInfo)
{
  if (!colorInfo->devicecodeInfo->overprintsEnabled) {
    /* This will invalidate the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains(colorInfo, FALSE))
      colorInfo->devicecodeInfo->overprintsEnabled = TRUE;
    else
      return FALSE;
  }

  return TRUE;
}

Bool cc_overprintsEnabled(GS_COLORinfo *colorInfo, COLOR_PAGE_PARAMS *colorPageParams)
{
  return colorInfo->devicecodeInfo->overprintsEnabled &&
         colorPageParams->overprintsEnabled;
}

/* overprintmode handling for postscript and pdf
 * The complication with this stuff is the interaction with Hqn's OverprintProcess
 * userparam (which does much the same thing). The way it works is that the
 * userparam will set the overprintMode attribute for all gstates at that
 * save level and will therefore serve as a default value. Any calls to
 * setoverprintmode or usage of OPM will modify just the one gstate provided
 * the IgnoreOverprintMode userparam isn't true.
 * We also need to store the actual value last passed to setoverprintmode to
 * allow the correct return value in currentoverprintmode. This is the purpose
 * of gsc_s(g)etcurrentoverprintmode
 */

Bool gsc_setoverprintmode( GS_COLORinfo *colorInfo, Bool overprintMode )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( !colorInfo->devicecodeInfo->ignoreOverprintMode ) {
    if ( colorInfo->devicecodeInfo->overprintMode != overprintMode ) {
      /* OverprintMode is catered for in the ChainCache */
      if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
          cc_invalidateColorChains( colorInfo, FALSE ))
        colorInfo->devicecodeInfo->overprintMode = overprintMode ;
      else
        return FALSE;
    }
  }

  return TRUE;
}

/* Returns the applied value of OPM */
Bool gsc_getoverprintmode( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->overprintMode ;
}

Bool gsc_setcurrentoverprintmode( GS_COLORinfo *colorInfo, Bool overprintMode )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->currentOverprintMode != overprintMode ) {
    if (!updatedevicecodeinfo(&colorInfo->devicecodeInfo))
      return FALSE;
    colorInfo->devicecodeInfo->currentOverprintMode = overprintMode ;
  }

  return TRUE;
}

/* Returns the value of OPM set by the job */
Bool gsc_getcurrentoverprintmode( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->currentOverprintMode ;
}

Bool gsc_setoverprintblack( GS_COLORinfo *colorInfo, Bool overprintBlack )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->overprintBlack != overprintBlack ) {
    /* OverprintBlack is catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, FALSE ))
      colorInfo->devicecodeInfo->overprintBlack = overprintBlack ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getoverprintblack( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->overprintBlack ;
}

Bool gsc_setoverprintgray( GS_COLORinfo *colorInfo, Bool overprintGray )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->overprintGray != overprintGray ) {
    /* OverprintGray is NOT catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, TRUE ))
      colorInfo->devicecodeInfo->overprintGray = overprintGray ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getoverprintgray( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->overprintGray ;
}

Bool gsc_setoverprintgrayimages( GS_COLORinfo *colorInfo, Bool overprintGrayImages )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->overprintGrayImages != overprintGrayImages ) {
    /* OverprintGrayImages is NOT catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, TRUE ))
      colorInfo->devicecodeInfo->overprintGrayImages = overprintGrayImages ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getoverprintgrayimages( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->overprintGrayImages ;
}

Bool gsc_setoverprintwhite( GS_COLORinfo *colorInfo, Bool overprintWhite )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->overprintWhite != overprintWhite ) {
    /* OverprintWhite is NOT catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, TRUE ))
      colorInfo->devicecodeInfo->overprintWhite = overprintWhite ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getoverprintwhite( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->overprintWhite ;
}

Bool gsc_setignoreoverprintmode( GS_COLORinfo *colorInfo, Bool ignoreOverprintMode )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->ignoreOverprintMode != ignoreOverprintMode ) {
    /* IgnoreOverprintMode is NOT catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, TRUE ))
      colorInfo->devicecodeInfo->ignoreOverprintMode = ignoreOverprintMode ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getignoreoverprintmode( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->ignoreOverprintMode ;
}

Bool gsc_settransformedspotoverprint( GS_COLORinfo *colorInfo, Bool transformedSpotOverprint )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->transformedSpotOverprint != transformedSpotOverprint ) {
    /* TransformedSpotOverprint is NOT catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, TRUE ))
      colorInfo->devicecodeInfo->transformedSpotOverprint = transformedSpotOverprint ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_gettransformedspotoverprint( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->transformedSpotOverprint ;
}

Bool gsc_setoverprinticcbased( GS_COLORinfo *colorInfo, Bool overprintICCBased )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  if ( colorInfo->devicecodeInfo->overprintICCBased != overprintICCBased ) {
    /* OverprintICCBased is NOT catered for in the ChainCache */
    if (updatedevicecodeinfo(&colorInfo->devicecodeInfo) &&
        cc_invalidateColorChains( colorInfo, TRUE ))
      colorInfo->devicecodeInfo->overprintICCBased = overprintICCBased ;
    else
      return FALSE;
  }

  return TRUE;
}

Bool gsc_getoverprinticcbased( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo != NULL , "colorInfo is null" ) ;
  HQASSERT( colorInfo->devicecodeInfo != NULL , "devicecodeInfo is null" ) ;

  return colorInfo->devicecodeInfo->overprintICCBased ;
}

/* ---------------------------------------------------------------------- */
/* Block invocation overprinting. This is used by SHFILLs which are implemented
   using images. Before invoking the block routines, the image routine calls
   gsc_setblockoverprints. This attaches a block overprint structure to the
   color chain head. The block invoke routine accumulates overprint
   information into this structure, until gsc_clearblockoverprints is
   called to remove the information. gsc_applyblockoverprints may be called
   to apply the accumulated overprint information to a DL color. This takes
   the form of clearing the knockout flag, and applying shfill/maxblt
   information as necessary. The dl color is not reduced, since shfill
   overprints are ignored in rendering this is unnecessary. In the image case,
   the DL color to which the overprint information is applied is the color
   saved for use in the image's LISTOBJECT. */
void cc_destroyblockoverprints(GS_BLOCKoverprint **pBlockOverprint)
{
  if ( *pBlockOverprint != NULL ) {
    mm_free(mm_pool_color, (mm_addr_t)*pBlockOverprint,
            (mm_size_t)((*pBlockOverprint)->structSize)) ;
    *pBlockOverprint = NULL ;
  }
}

Bool cc_isblockoverprinting(GS_BLOCKoverprint *blockOverprint)
{
  return blockOverprint != NULL && blockOverprint->overprintAll ;
}

void gsc_clearblockoverprints(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[ colorType ] ;

  if ( colorChain != NULL && colorChain->context != NULL ) {
    cc_destroyblockoverprints(&colorChain->context->blockOverprint) ;
  }
}

Bool gsc_setblockoverprints(GS_COLORinfo *colorInfo, int32 colorType)
{
  GS_CHAINinfo *colorChain ;
  GS_BLOCKoverprint *blockOverprint ;
  CLINKDEVICECODEinfo *devicecode ;

  if ( !gsc_isoverprintpossible(colorInfo, colorType) )
    return TRUE ; /* No overprinting, don't waste time looking */

  if ( ! gsc_constructChain( colorInfo , colorType ))
    return FALSE ;
  colorChain = colorInfo->chainInfo[ colorType ] ;
  HQASSERT( colorChain != NULL , "colorChain NULL" ) ;
  HQASSERT( colorChain->context->pnext , "pLink NULL" ) ;

  blockOverprint = colorChain->context->blockOverprint ;
  if ( blockOverprint == NULL ) {
    CLINK *pLink ;
    size_t structSize ;
    int32 nDeviceColorants ;

    /* Find devicecode CLINK (last in chain) to get at colorant indices etc. */
    for ( pLink = colorChain->context->pnext ; pLink->pnext != NULL ; pLink = pLink->pnext ) ;
    devicecode = pLink->p.devicecode ;
    nDeviceColorants = devicecode->nDeviceColorants ;

    structSize = sizeof(GS_BLOCKoverprint) +
      OVERPRINTMASK_BYTESIZE(nDeviceColorants) +
      nDeviceColorants * sizeof(COLORANTINDEX) * 3 ;
    colorChain->context->blockOverprint = blockOverprint =
      mm_alloc(mm_pool_color, (mm_size_t)structSize, MM_ALLOC_CLASS_NCOLOR) ;

    if ( blockOverprint == NULL )
      return error_handler(VMERROR) ;

    blockOverprint->structSize = structSize ;
    blockOverprint->sortingIndexes = (int32 *)(blockOverprint + 1) ;
    blockOverprint->sortedColorants = blockOverprint->sortingIndexes + nDeviceColorants ;
    blockOverprint->overprintedColorants = blockOverprint->sortedColorants + nDeviceColorants ;
    blockOverprint->overprintMask = (uint32 *)(blockOverprint->overprintedColorants + nDeviceColorants) ;
    blockOverprint->n_Colorants = nDeviceColorants ;
    HQASSERT(devicecode->fTransformedSpot,
             "Block overprints are not from transformed spot") ;
    blockOverprint->overprintOpCode = DLC_MAXBLT_OVERPRINTS ;
    while ( --nDeviceColorants >= 0 ) {
      blockOverprint->sortingIndexes[nDeviceColorants] = devicecode->sortingIndexes[nDeviceColorants] ;
      blockOverprint->sortedColorants[nDeviceColorants] = devicecode->sortedColorants[nDeviceColorants] ;
    }
  }

  blockOverprint->overprintAll = TRUE ;
  blockOverprint->overprintUseMask = FALSE ;
  SET_OVERPRINTMASK(blockOverprint->overprintMask, blockOverprint->n_Colorants,
                    OVERPRINTMASK_OVERPRINT) ;

  return TRUE ;
}

/* Apply collected block overprints and overprint flags to a dl color. */
Bool gsc_applyblockoverprints(GS_COLORinfo *colorInfo, int32 colorType,
                              DL_STATE *page, dl_color_t *pdlc)
{
  GS_CHAINinfo *colorChain = colorInfo->chainInfo[ colorType ] ;
  GS_BLOCKoverprint *blockOverprint ;

  if ( colorChain != NULL && colorChain->context != NULL &&
       (blockOverprint = colorChain->context->blockOverprint) != NULL &&
       blockOverprint->overprintAll ) {
    HQASSERT(!colorInfo->fInvalidateColorChains,
             "fMustInvalidateColorChains should not be set here") ;

    dl_set_currentspflags(page->dlc_context,
                          dl_currentspflags(page->dlc_context) & ~RENDER_KNOCKOUT) ;

    /* If mask used, apply to pdlc */
    if ( blockOverprint->overprintUseMask ) {
      COLORANTINDEX *overprintColorants, *sortingIndexes, *sortedColorants ;
      int32 nOverprinted, nSorted, nDeviceColorants, i ;

      nDeviceColorants = blockOverprint->n_Colorants ;
      sortingIndexes = blockOverprint->sortingIndexes ;
      overprintColorants = blockOverprint->overprintedColorants ;
      sortedColorants = blockOverprint->sortedColorants ;

      for ( i = nSorted = nOverprinted = 0 ; i < nDeviceColorants ; ++i ) {
        int32 nOriginalIndex = sortingIndexes[ i ] ;

        if (nOriginalIndex >= 0) {
          /* note: negative sorting indexes arise if we have the same named
             colorant more than once in a DeviceN color space; only one of
             them actually takes effect, hence the omission */

          if ( !PAINT_COLORANT(blockOverprint->overprintMask, nOriginalIndex) ) {
            overprintColorants[nOverprinted++] = sortedColorants[nSorted] ;
          }

          nSorted++;
        }
      }

      if ( !dlc_apply_overprints(page->dlc_context,
                                 blockOverprint->overprintOpCode,
                                 DLC_REPLACE_OP,
                                 nOverprinted,
                                 overprintColorants,
                                 pdlc) )
        return FALSE;

      return TRUE ;
    }
  } ;

  /* There was no overprint information, so remove overprints from colour */
  return dlc_apply_overprints(page->dlc_context,
                              DLC_MAXBLT_OVERPRINTS,
                              DLC_REPLACE_OP,
                              0,
                              NULL,
                              pdlc) ;
}

/* ---------------------------------------------------------------------- */
static Bool get_nDeviceColorants(int32               nColorants ,
                                 COLORANTINDEX       *colorants ,
                                 GUCR_RASTERSTYLE    *hRasterStyle )
{
  if (guc_photoinkInfo(hRasterStyle) == NULL)
    return nColorants;
  else {
    int32 i ;
    int32 nDeviceColorants = 0 ;

    for ( i = 0 ; i < nColorants ; ++i ) {
      HQASSERT(colorants[i] != COLORANTINDEX_ALL,
               "/All sep should have been converted for photoink");
      if (colorants[i] == COLORANTINDEX_NONE)
        nDeviceColorants++ ;
      else {
        COLORANTINDEX *cis ;
        cis = guc_getColorantMapping( hRasterStyle, colorants[i] ) ;
        HQASSERT(cis != NULL, "Colorant mapping must exist for photoink") ;
        while (*cis++ != COLORANTINDEX_UNKNOWN)
          nDeviceColorants++ ;
      }
    }
    return nDeviceColorants ;
  }
}

/* ---------------------------------------------------------------------- */

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
int cc_countLinksInDeviceCode(CLINK *pLink)
{
  CLINKDEVICECODEinfo *devicecode;
  int nLinks = 0;
  int i;

  HQASSERT(pLink != NULL, "pLink NULL");
  devicecode = pLink->p.devicecode;

  for (i = 0; i < pLink->n_iColorants; i++) {
    if (devicecode->transferLinks[i] != NULL)
      nLinks++;
    if (devicecode->calibrationLinks[i] != NULL)
      nLinks++;
  }

  return nLinks;
}
#endif    /* ASSERT_BUILD */
#endif    /* METRICS_BUILD */


/* Log stripped */
