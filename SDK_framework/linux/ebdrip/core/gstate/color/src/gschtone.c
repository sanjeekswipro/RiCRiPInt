/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gschtone.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface functions between colour and halftones.
 */

#include "core.h"

#include "constant.h"     /* EPSILON */
#include "control.h"      /* interpreter */
#include "dicthash.h"     /* internaldict */
#include "dictscan.h"     /* NAMETYPEMATCH */
#include "fileio.h"       /* FILELIST */
#include "fileops.h"      /* currfileCache */
#include "gcscan.h"       /* ps_scan_field */
#include "gstack.h"       /* gs_regeneratehalftoneinfo */
#include "gstate.h"       /* invalidate_gstate_screens */
#include "gu_chan.h"      /* guc_colorantIndexPossiblyNewName */
#include "gu_htm.h"       /* htm_IsModuleNameRegistered */
#include "gu_misc.h"      /* newhalftones */
#include "gs_spotfn.h"    /* findCSpotFunction */
#include "halftone.h"     /* ht_equivalentchspotid */
#include "hqmemcpy.h"     /* HqMemCpy */
#include "mmcompat.h"     /* mm_sac_alloc */
#include "mps.h"          /* mps_res_t */
#include "namedef_.h"     /* NAME_* */
#include "objects.h"      /* OBJECT */
#include "params.h"       /* SystemParams */
#include "pcmnames.h"     /* pcmCMYKNames */
#include "pixelLabels.h"  /* RENDERING_PROPERTY_RENDER_ALL* */
#include "rcbcntrl.h"     /* rcbn_presep_screen */
#include "routedev.h"     /* DEVICE_INVALID_CONTEXT */
#include "spdetect.h"     /* get_separation_name */
#include "swerrors.h"     /* RANGECHECK */
#include "dlstate.h"      /* page */

#include "gsccalibpriv.h" /* cc_note_uncalibrated_screens */
#include "gscdevcipriv.h" /* cc_samedevicecodehalftoneinfo */
#include "gschcms.h"      /* REPRO_N_TYPES */
#include "gscparamspriv.h"/* colorUserParams */
#include "gscxferpriv.h"  /* cc_sametransferhalftoneinfo */

#include "gschtonepriv.h"


#define HT_SEP 0
#define HT_OVR 1
#define HT_USE 2

/* For now, this is only used in the arrays of GS_HALFTONEinfo. */

typedef struct HTXFER {
  OBJECT          transferFn;
  NAMECACHE      *transferFnColorant;
} HTXFER;

struct GS_HALFTONEinfo {
  cc_counter_t    refCnt;

  OBJECT          halftonedict;

  SPOTNO          spotno;
  int32           halftoneoverride;
  uint8           regeneratescreen;
  uint8           screentype;
  uint8           pad1;
  uint8           pad2;
  int32           halftoneid;

  HALFTONE        halftones[4];

  uint32          nColorants[REPRO_N_TYPES+1];
  /* nColorants = 0 means type has not been specified in the dict */
  OBJECT          htDefaultTransfer[REPRO_N_TYPES+1];
  HTXFER         *htTransfer[REPRO_N_TYPES+1]/*[nColorants]*/;
};


#if defined( ASSERT_BUILD )
static void halftoneInfoAssertions(GS_HALFTONEinfo *pInfo);
#else
#define halftoneInfoAssertions(pInfo) EMPTY_STATEMENT()
#endif

static Bool cc_createhalftoneinfo( GS_HALFTONEinfo **halftoneInfo );
static Bool cc_updatehalftoneinfo( GS_HALFTONEinfo **halftoneInfo );

/* ---------------------------------------------------------------------- */

typedef struct sethalftone_params {
  OBJECT *htdict_main;      /* Original object passed into dosethalftone. */
  OBJECT *htdict_parent; /* Parent dict, if any */
  OBJECT *htdict_use ;      /* The object/dict/sub-dict to use, esp. when overriding. */

  Bool overridefreq;  /* If this screen's frequency can be overridden. */
  Bool overrideangle; /* If this screen's angle     can be overridden. */
  Bool inheritfreq;   /* If this screen's frequency can be inherited.  */
  Bool inheritangle;  /* If this screen's angle     can be inherited.  */

  Bool fSeparationsKeySetInType5 ; /* If we're controlling separations from the
                                      halftone dictionary (perhaps positioned).      */

  HTTYPE objectType; /* Object type for subscreens of type 195 screens */

  size_t nSubDictIndex; /* Index of the current subdict within a Type 5 */

  COLORANTINDEX colorantIndex ;         /* colorant of screen being considered */

  int32 nOriginalHalftoneType ;         /* Halftone type of screen being considered.    */
  NAMECACHE * nmOriginalHalftoneName ;  /* Dot shape name of screen being considered.   */
  NAMECACHE * nmOriginalColorKey ;      /* Color name of screen being considered.       */

  int32 nUseHalftoneType ;              /* Halftone type of screen being used.          */
  NAMECACHE * nmUseHalftoneName ;       /* Dot shape of screen being used.              */
  NAMECACHE * nmUseColorKey ;           /* Color name of screen being used.             */

  int32 nOverrideHalftoneType ;         /* Halftone type of screen being executed.      */
  NAMECACHE * nmOverrideHalftoneName ;  /* Dot shape of screen being executed.          */
  NAMECACHE * nmOverrideColorKey ;      /* Color name of screen being executed.         */

  NAMECACHE *nmAlternativeName;         /* The halftone name to look up the disk
                                           cache from, arising out of the Hqn
                                           extension type 199 halftones */
  NAMECACHE *nmAlternativeColor;        /* ditto, for colorant */
  HTTYPE cacheType;                     /* ditto, for type */

  int32 cAlternativeHalftoneRedirections; /* allows us to count the number of times
                                             we jump to an alternative dictionary via
                                             a type 199 halftone; and in particular
                                             limits us to a fixed depth so we don't
                                             go infinitely recursive and blow up the
                                             c stack */

  int32 encryptType ;                   /* If this halftone is encrypted (!=0) and what type */
  int32 encryptLength ;                 /* Encyption Length (req for HQX, EncryptTypes 1 and 2)  */

} SETHALFTONE_PARAMS ;

enum {
  HALFTONE_GROUP_INVALID = 0, /* zero means it's not a valid type */
  HALFTONE_GROUP_SPOTFN,      /* In-RIP screen using spot-function/frequency/angle */
  HALFTONE_GROUP_THRESHOLD,   /* In-RIP screen using threshold table */
  HALFTONE_GROUP_UMBRELLA,    /* Collection of other types, e.g. a type 5 */
  HALFTONE_GROUP_MODULAR      /* Implemented by external screening module */
} ;

typedef struct {
  int32 group ;            /* One HALFTONE_GROUP_xxxx value */
  int32 multiple_screens ; /* If non-zero is the step from one to another. */
} HALFTONE_INFO ;

/* Argument block structure used to pass data to the dictwalk_dosethalftone()
   callback passed to walk_dictionary()
*/
typedef struct dw_dosetht_params {
  SPOTNO spotno ;
  Bool checkargs ;
  SYSTEMVALUE *freqv ; /* non-null if to override dictionary value */
  SYSTEMVALUE *anglv ;
  GS_COLORinfo *colorInfo ;
  SETHALFTONE_PARAMS *htparams ;
  corecontext_t *context;
} DW_dosetht_Params ;

static Bool dosethalftone(  corecontext_t *context,
                            OBJECT *poKeyOfType5Subordinate,
                            OBJECT *thedict ,
                            int32 screentype ,
                            SPOTNO spotno ,
                            Bool checkargs ,
                            SYSTEMVALUE *freqv , /* non-null if to override dictionary value */
                            SYSTEMVALUE *anglv ,
                            GS_COLORinfo *colorInfo ,
                            SETHALFTONE_PARAMS *htparams );

static Bool dosethalftone5( corecontext_t *context,
                            OBJECT *theo ,
                            SPOTNO spotno ,
                            Bool checkargs ,
                            SYSTEMVALUE *freqv , /* non-null if to override dictionary value */
                            SYSTEMVALUE *anglv ,
                            GS_COLORinfo *colorInfo ,
                            SETHALFTONE_PARAMS *htparams ,
                            NAMETYPEMATCH **htmatch ) ;

static Bool dictwalk_dosethalftone5( OBJECT * poKeyOfType5Subordinate,
                                     OBJECT * poDict,
                                     void *argsBlockPtr ) ;

static Bool checksinglescreen(OBJECT *freqo, SYSTEMVALUE *freqv,
                              OBJECT *anglo, SYSTEMVALUE *anglv,
                              OBJECT *proco,
                              Bool allowdict,
                              int32 *dictcount);

static Bool setscreens_checkargs(STACK *stack , int32 screentype , int32 *sl ,
                                 SYSTEMVALUE freqv[ 4 ] ,
                                 SYSTEMVALUE anglv[ 4 ] ,
                                 OBJECT *proco[ 4 ] );

static Bool setscreens_execargs_dict(  corecontext_t *context,
                                       GS_COLORinfo *colorInfo,
                                       STACK *stack ,
                                       int32 screentype,
                                       int32 htfromsetscreen,
                                       int32 sl,
                                       int32 lspotid ,
                                       SYSTEMVALUE freqv[ 4 ] ,
                                       SYSTEMVALUE anglv[ 4 ] ,
                                       OBJECT *proco[ 4 ] );
static Bool clearTransferCache(GS_HALFTONEinfo *halftoneInfo);

static Bool dosethalftone199(OBJECT * poType199Dictionary,
                             OBJECT ** ppoSubstituteDictionary,
                             SETHALFTONE_PARAMS *htparams);


static int32 frequency_names[] = {
  NAME_RedFrequency, NAME_BlueFrequency,
  NAME_GreenFrequency, NAME_GrayFrequency };
static int32 angle_names[] = {
  NAME_RedAngle, NAME_BlueAngle,
  NAME_GreenAngle, NAME_GrayAngle };

static Bool halftoneoverride = FALSE;

/* IDs for pdfout gstate tracking. */
static int32 HalftoneId = 0 ;

/* ---------------------------------------------------------------------- */

#if defined( ASSERT_BUILD )
Bool debug_regeneration = FALSE ;
#endif

int32 spotid = 0 ;

/* ---------------------------------------------------------------------- */

void init_C_globals_gschtone(void)
{
  HalftoneId = 0 ;
  halftoneoverride = FALSE;
  spotid = 0 ;
#if defined( ASSERT_BUILD )
  debug_regeneration = FALSE ;
#endif
}

/*
 * Halftone Info Data Access Functions
 * ===================================
 */

static Bool cc_createhalftoneinfo( GS_HALFTONEinfo **halftoneInfo )
{
  size_t i;
  GS_HALFTONEinfo     *pInfo;

  pInfo = (GS_HALFTONEinfo *) mm_sac_alloc( mm_pool_color,
                                            sizeof(GS_HALFTONEinfo),
                                            MM_ALLOC_CLASS_NCOLOR );

  *halftoneInfo = pInfo;

  if (pInfo == NULL)
    return error_handler(VMERROR);

  pInfo->refCnt = 1;

  pInfo->halftonedict = onull; /* Struct copy to set slot properties */

  pInfo->spotno = SPOT_NO_INVALID;
  pInfo->halftoneoverride = 0;
  pInfo->regeneratescreen = FALSE;
  pInfo->screentype = ST_SETMISSING; /* nearest to an invalid value */

  pInfo->halftoneid = -1;

  for (i = 0; i < 4; i++)
    pInfo->halftones[i].spotfn = onull; /* Struct copy to set slot properties */

  for (i = 0 ; i <= REPRO_N_TYPES ; i++) {
    pInfo->nColorants[i] = 0;
    pInfo->htDefaultTransfer[i] = onull; /* Struct copy to set slot properties */
    pInfo->htTransfer[i] = NULL;
  }
  return TRUE;
}

static void freehalftoneinfo( GS_HALFTONEinfo *halftoneInfo )
{
  size_t type;

  for (type = 0 ; type <= REPRO_N_TYPES ; type++) {
    if (halftoneInfo->htTransfer[type] != NULL)
      mm_sac_free(mm_pool_color, halftoneInfo->htTransfer[type],
                  halftoneInfo->nColorants[type] * sizeof(HTXFER));
  }
  mm_sac_free(mm_pool_color, halftoneInfo, sizeof(GS_HALFTONEinfo));
}

void cc_destroyhalftoneinfo( GS_HALFTONEinfo **halftoneInfo )
{
  if ( *halftoneInfo != NULL ) {
    halftoneInfoAssertions(*halftoneInfo);
    CLINK_RELEASE(halftoneInfo, freehalftoneinfo);
  }
}

void cc_reservehalftoneinfo( GS_HALFTONEinfo *halftoneInfo )
{
  if ( halftoneInfo != NULL ) {
    halftoneInfoAssertions( halftoneInfo ) ;
    CLINK_RESERVE( halftoneInfo ) ;
  }
}


static Bool cc_copyhalftoneinfo( GS_HALFTONEinfo *halftoneInfo,
                                 GS_HALFTONEinfo **halftoneInfoCopy )
{
  GS_HALFTONEinfo *pInfoCopy;
  size_t type;

  halftoneInfoAssertions(halftoneInfo);

  pInfoCopy = (GS_HALFTONEinfo *) mm_sac_alloc( mm_pool_color,
                                                sizeof(GS_HALFTONEinfo),
                                                MM_ALLOC_CLASS_NCOLOR );
  if (pInfoCopy == NULL)
    return error_handler(VMERROR);

  HqMemCpy(pInfoCopy, halftoneInfo, sizeof(GS_HALFTONEinfo));
  pInfoCopy->refCnt = 1;

  for (type = 0 ; type <= REPRO_N_TYPES ; type++) {
    if (pInfoCopy->nColorants[type] > 0) {
      mm_size_t htxferSize = pInfoCopy->nColorants[type] * sizeof(HTXFER);
      pInfoCopy->htTransfer[type] =
        (HTXFER *) mm_sac_alloc(mm_pool_color,
                                htxferSize, MM_ALLOC_CLASS_NCOLOR);
      if ( pInfoCopy->htTransfer[type] == NULL ) {
        cc_destroyhalftoneinfo( &pInfoCopy );
        return error_handler( VMERROR );
      }

      HqMemCpy(pInfoCopy->htTransfer[type], halftoneInfo->htTransfer[type],
               htxferSize);
    }
  }

  *halftoneInfoCopy = pInfoCopy;
  return TRUE;
}


static Bool cc_updatehalftoneinfo( GS_HALFTONEinfo **halftoneInfo )
{
  if ( *halftoneInfo == NULL )
    return cc_createhalftoneinfo( halftoneInfo );

  halftoneInfoAssertions(*halftoneInfo);

  CLINK_UPDATE(GS_HALFTONEinfo, halftoneInfo,
               cc_copyhalftoneinfo, freehalftoneinfo);
  return TRUE;
}


Bool gsc_regeneratehalftoneinfo( GS_COLORinfo *colorInfo,
                                 SPOTNO newspotno, SPOTNO oldspotno )
{
  uint8 regenscreen;

  HQASSERT( colorInfo, "gs_regeneratehalftoneinfo: colorInfo is NULL" );
  /* Note that when this is called colorInfo has already been updated with the
     new halftone data. */
  HQASSERT( colorInfo->halftoneInfo->spotno == newspotno,
            "Colorinfo doesn't have the new screen." );

  /* If we're forced to regenerate the screen, or the spot numbers are
   * different and the spot number we're going to is not already in the
   * halftone cache, then we must actually regenerate
   */
  regenscreen = colorInfo->halftoneInfo->regeneratescreen;
  if ( newspotno == oldspotno && ! regenscreen )
    return TRUE;

  /* Must do this now, since redo doesn't know about oldspotno. */
  ht_change_non_purgable_screen( oldspotno, newspotno );
  HQTRACE(debug_regeneration, ("gstate change spotno: %d", newspotno));

  if ( regenscreen ||
       ! ht_checkifchentry( newspotno, HTTYPE_DEFAULT, COLORANTINDEX_UNKNOWN,
                            gsc_getHalftonePhaseX( colorInfo ),
                            gsc_getHalftonePhaseY( colorInfo ))) {
    if ( ! gsc_redo_setscreen( colorInfo ))
      return FALSE;
  }
  /* Following assignment is needed because a setscreen can mark the gstate
   * being restored as needing regenerating and this is then bodily copied
   * (above) into the gstate.
   */
  colorInfo->halftoneInfo->regeneratescreen = FALSE;
  return TRUE;
}


/** gsc_setdefaulthalftoneinfo sets the gstate containing colorInfo with the
   'default' halftoneinfo. */
Bool gsc_setdefaulthalftoneinfo( GS_COLORinfo *colorInfo,
                                 GS_COLORinfo *defaultColorInfo )
{
  SPOTNO oldspotno;
  SPOTNO newspotno;

  HQASSERT( colorInfo , "gsc_setdefaulthalftoneinfo: colorInfo NULL" );
  HQASSERT( defaultColorInfo , "gsc_setdefaulthalftoneinfo: defaultColorInfo NULL" );

  if ( defaultColorInfo == colorInfo )
    return TRUE; /* default halftone already established */

  oldspotno = gsc_getSpotno( colorInfo );
  newspotno = gsc_getSpotno( defaultColorInfo );

  cc_destroyhalftoneinfo( &colorInfo->halftoneInfo );
  colorInfo->halftoneInfo = defaultColorInfo->halftoneInfo;
  cc_reservehalftoneinfo( defaultColorInfo->halftoneInfo );

  if ( oldspotno != newspotno )
    gsc_markChainsInvalid(colorInfo);
  return gsc_regeneratehalftoneinfo( colorInfo, newspotno, oldspotno );
}


SPOTNO gsc_getSpotno(GS_COLORinfo *colorInfo)
{
  HQASSERT( colorInfo != NULL, "gsc_getSpotno: NULL pointer to colorInfo" ) ;
  HQASSERT( colorInfo->halftoneInfo != NULL, "gsc_getSpotno: NULL pointer to halftone info" ) ;

  return colorInfo->halftoneInfo->spotno ;
}


Bool gsc_setSpotno(GS_COLORinfo *colorInfo, SPOTNO spotno)
{
  HQASSERT( colorInfo != NULL, "gsc_setSpotno: NULL pointer to colorInfo" ) ;
  HQASSERT( colorInfo->halftoneInfo != NULL, "gsc_setSpotno: NULL pointer to halftone info" ) ;

  if ( colorInfo->halftoneInfo->spotno == spotno )
    return TRUE;
  if ( cc_updatehalftoneinfo(&colorInfo->halftoneInfo) ) {
    gsc_markChainsInvalid(colorInfo);
    colorInfo->halftoneInfo->spotno = spotno;
    return clearTransferCache(colorInfo->halftoneInfo);
    /* The transfer functions are now unavailable, but this should not
       matter as the callers will only build backend chains. */
  } else
    return FALSE;
}


Bool cc_halftonetransferinfo(GS_HALFTONEinfo    *halftoneinfo,
                             COLORANTINDEX      iColorant,
                             HTTYPE             reproType,
                             OBJECT             **halftonetransfer,
                             int32              *pHalftoneOffset,
                             GUCR_RASTERSTYLE   *rasterstyle)
{
  size_t i;
  COLORANTINDEX ci;
  HTTYPE useType;

  halftoneInfoAssertions(halftoneinfo);
  HQASSERT(halftonetransfer != NULL, "halftonetransfer is null");
  HQASSERT(pHalftoneOffset != NULL, "pHalftoneOffset is null");
  HQASSERT(reproType < REPRO_N_TYPES, "invalid reprotype");

  *halftonetransfer = NULL;
  *pHalftoneOffset = -1;

  /* If the colorant is present in the halftone dictionary, halftonetransfer
   * will be non-NULL. If the colorant is present, but the TransferFunction is
   * absent, halftonetransfer will point to a null object.
   */
  useType = halftoneinfo->nColorants[reproType] == 0 /* not in dict */
            ? HTTYPE_DEFAULT : reproType;
  if (iColorant == COLORANTINDEX_NONE) {
    *halftonetransfer = &halftoneinfo->htDefaultTransfer[useType];
    *pHalftoneOffset = 0; /* default is always zero */
  } else if (iColorant != COLORANTINDEX_UNKNOWN) {
    for (i = 0; i < halftoneinfo->nColorants[useType]; i++) {
      NAMECACHE *colorantName =
        halftoneinfo->htTransfer[useType][i].transferFnColorant;
      if (colorantName != NULL) {
        if (!guc_colorantIndexPossiblyNewName(rasterstyle, colorantName, &ci))
          return FALSE;
        if (iColorant == ci) {
          *halftonetransfer = &halftoneinfo->htTransfer[useType][i].transferFn;
          *pHalftoneOffset = (int32)i + 1; /* shunt up by one, zero is default */
          break;
        }
      }
    }
  }
  return TRUE;
}

Bool cc_arehalftoneobjectslocal(corecontext_t *corecontext,
                                GS_HALFTONEinfo *halftoneInfo )
{
  size_t i, type;

  if ( halftoneInfo == NULL )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&halftoneInfo->halftonedict, corecontext) )
    return TRUE ;
  for ( i = 0 ; i < 4 ; i++ ) {
    if ( illegalLocalIntoGlobal(&halftoneInfo->halftones[i].spotfn,
                                corecontext ) )
      return TRUE ;
  }
  for (type = 0 ; type <= REPRO_N_TYPES ; type++) {
    if ( illegalLocalIntoGlobal(&halftoneInfo->htDefaultTransfer[type],
                                corecontext) )
      return TRUE ;
    for ( i = 0 ; i < halftoneInfo->nColorants[type] ; i++ ) {
      if ( illegalLocalIntoGlobal(&halftoneInfo->htTransfer[type][i].transferFn,
                                  corecontext) )
        return TRUE ;
    }
  }
  return FALSE ;
}


/** scan the given halftone info.
 *
 * This should match gsc_arehalftoneobjectslocal, since both need look at
 * all the VM pointers. */
mps_res_t cc_scan_halftone( mps_ss_t ss, GS_HALFTONEinfo *halftoneInfo )
{
  mps_res_t res;
  size_t i, type;

  if ( halftoneInfo == NULL )
    return MPS_RES_OK;

  res = ps_scan_field( ss, &halftoneInfo->halftonedict );
  if ( res != MPS_RES_OK ) return res;
  for ( i = 0 ; i < 4 ; ++i ) {
    res = ps_scan_field( ss, &halftoneInfo->halftones[ i ].spotfn );
    if ( res != MPS_RES_OK ) return res;
  }
  for (type = 0 ; type <= REPRO_N_TYPES ; type++) {
    res = ps_scan_field( ss, &halftoneInfo->htDefaultTransfer[type] );
    if ( res != MPS_RES_OK ) return res;
    for ( i = 0 ; (int32)i < halftoneInfo->nColorants[type] ; i++ ) {
      res = ps_scan_field( ss, &halftoneInfo->htTransfer[type][i].transferFn );
      if ( res != MPS_RES_OK ) return res;
    }
  }
  return MPS_RES_OK;
}


#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the transfer info access functions.
 */
static void halftoneInfoAssertions(GS_HALFTONEinfo *pInfo)
{
  size_t type;

  HQASSERT(pInfo != NULL, "Missing halftone info");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");

  HQASSERT(pInfo->halftoneid >= 0 || pInfo->spotno == SPOT_NO_INVALID,
           "halftoneid not set");

  HQASSERT(HTTYPE_DEFAULT == REPRO_N_TYPES, "HTTYPE_DEFAULT wrong");
  for (type = 0 ; type <= REPRO_N_TYPES ; type++)
    HQASSERT((pInfo->nColorants[type] == 0 && pInfo->htTransfer[type] == NULL) ||
             (pInfo->nColorants[type] != 0 && pInfo->htTransfer[type] != NULL),
             "Invalid nColorants or htTransfer");
}
#endif


/* ---------------------------------------------------------------------- */
uint8 gsc_getscreentype( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo, "colorInfo parameter to gsc_getscreentype is NULL" ) ;
  HQASSERT( colorInfo->halftoneInfo, "halftoneInfo parameter to gsc_getscreentype is NULL" ) ;
  return colorInfo->halftoneInfo->screentype ;
}

HALFTONE *gsc_gethalftones( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo, "colorInfo parameter to gsc_gethalftones is NULL" ) ;
  HQASSERT( colorInfo->halftoneInfo, "halftoneInfo parameter to gsc_gethalftones is NULL" ) ;
  return &colorInfo->halftoneInfo->halftones[ 0 ] ;
}

OBJECT *gsc_gethalftonedict( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo, "colorInfo parameter to gsc_gethalftonedict is NULL" ) ;
  HQASSERT( colorInfo->halftoneInfo, "halftoneInfo parameter to gsc_gethalftonedict is NULL" ) ;
  return &colorInfo->halftoneInfo->halftonedict ;
}

int32 gsc_gethalftoneid( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo, "colorInfo parameter to gsc_gethalftoneid is NULL" ) ;
  HQASSERT( colorInfo->halftoneInfo, "halftoneInfo parameter to gsc_gethalftoneid is NULL" ) ;
  return colorInfo->halftoneInfo->halftoneid ;
}

int32 cc_gethalftoneid( GS_HALFTONEinfo  *halftoneInfo )
{
  HQASSERT( halftoneInfo, "halftoneInfo parameter to cc_gethalftoneid is NULL" ) ;
  return halftoneInfo->halftoneid ;
}


/* ---------------------------------------------------------------------- */
/* Extracts the spot function from a halftone dictionary given a color.   */
OBJECT *ht_extract_spotfunction( OBJECT *htdict , NAMECACHE *color )
{
  OBJECT *htsubd ;
  OBJECT *httype ;
  OBJECT *spotfunction = NULL ;

  httype = fast_extract_hash_name( htdict , NAME_HalftoneType ) ;
  if ( httype &&
       oType( *httype ) == OINTEGER ) {
    switch ( oInteger( *httype )) {
    case 5:
      oName( nnewobj ) = color ;
      htsubd = fast_extract_hash( htdict , & nnewobj ) ;
      if ( htsubd == NULL ) {
        switch ( theINameNumber( color )) {
        case NAME_Cyan:    color = system_names + NAME_Red     ; break ;
        case NAME_Red:     color = system_names + NAME_Cyan    ; break ;
        case NAME_Magenta: color = system_names + NAME_Green   ; break ;
        case NAME_Green:   color = system_names + NAME_Magenta ; break ;
        case NAME_Yellow:  color = system_names + NAME_Blue    ; break ;
        case NAME_Blue:    color = system_names + NAME_Yellow  ; break ;
        case NAME_Black:   color = system_names + NAME_Gray    ; break ;
        case NAME_Gray:    color = system_names + NAME_Black   ; break ;
        }
        oName( nnewobj ) = color ;
        htsubd = fast_extract_hash( htdict , & nnewobj ) ;
      }
      htdict = htsubd ;
      /* Fall through */
    case 1:
      if ( htdict &&
           oType( *htdict ) == ODICTIONARY ) {
        spotfunction = fast_extract_hash_name( htdict , NAME_SpotFunction ) ;
      }
      break ;
    case 2:
      switch ( theINameNumber( color )) {
      case NAME_Cyan:
      case NAME_Red:
        oName( nnewobj ) = system_names + NAME_RedSpotFunction ;
        break ;
      case NAME_Magenta:
      case NAME_Green:
        oName( nnewobj ) = system_names + NAME_GreenSpotFunction ;
        break ;
      case NAME_Yellow:
      case NAME_Blue:
        oName( nnewobj ) = system_names + NAME_BlueSpotFunction ;
        break ;
      default:
        HQFAIL( "This should not really have hapenned" ) ;
        /* Fall through */
      case NAME_Default:
      case NAME_Black:
      case NAME_Gray:
        oName( nnewobj ) = system_names + NAME_GraySpotFunction ;
        break ;
      }
      spotfunction = fast_extract_hash( htdict , & nnewobj ) ;
      break ;
    }
  }
  if ( spotfunction &&
     ( oType( *spotfunction ) == OARRAY ||
       oType( *spotfunction ) == OPACKEDARRAY ))
    return spotfunction ;
  return NULL ;
}


/* ---------------------------------------------------------------------- */
typedef struct spotfn_args {
  SPOTNO spotno ;
  NAMECACHE *colorname ;
  OBJECT *spotfunction ;
  COLORANTINDEX ci;
} SPOTFN_ARGS ;


/* Extracts the spot function from a gstate given a color or index.       */
static Bool extract_spotfunction_from_one_gstate(GSTATE *gs, void *arg)
{
  SPOTFN_ARGS *sf_args = ( SPOTFN_ARGS * ) arg ;

  HQASSERT( gs , "gs is null in extract_spotfunction_from_one_gstate" ) ;
  HQASSERT( sf_args ,
            "sf_args is null in extract_spotfunction_from_one_gstate" ) ;
  HQASSERT( sf_args->colorname ,
            "colorname is null in extract_spotfunction_from_one_gstate" ) ;

  if ( gsc_getSpotno(gs->colorInfo) == sf_args->spotno ) {

    if ( gs->colorInfo->halftoneInfo->screentype == ST_SETHALFTONE ) {
      sf_args->spotfunction =
        ht_extract_spotfunction( &gs->colorInfo->halftoneInfo->halftonedict,
                                 sf_args->colorname );
      if ( sf_args->spotfunction != NULL )
        return TRUE ;

    } else {

      HALFTONE *pHalftones = &gs->colorInfo->halftoneInfo->halftones[0];
      /* pHalftones is an array of 4 which by definition is C,M,Y and K,
         using K as the default and R, G, B and Gray as synonymous in
         those slots */
      int32 nElement;

      for (nElement = 0; nElement < 8; nElement++) {
        if (guc_colorantIndexReserved (gsc_getRS(gs->colorInfo),
                                       pcmAllNames[nElement]) == sf_args->ci) {
          if (nElement >= 4)
            nElement -= 4;
          break;
        }
      }
      if (nElement == 8)
        nElement = 3; /* Black in the halftone array, because we didn't find the
                         colorant among the simple ones */
      sf_args->spotfunction = &pHalftones[nElement].spotfn;
      return TRUE ;
    }
  }
  return FALSE ; /* Not found it. */
}


OBJECT *gs_extract_spotfunction( SPOTNO spotno, COLORANTINDEX ci,
                                 NAMECACHE *colorname )
{
  SPOTFN_ARGS sf_args ;

  HQASSERT( colorname ,
            "colorname is null in extract_gstate_spotfunction" ) ;
  HQASSERT( gstackptr == gstateptr->next ,
            "extract_gstate_spotfunction: graphics stack is corrupt" ) ;

  /* Pack the arguments into a structure so we can use the generic
   * gframe walk function. */
  sf_args.spotno = spotno ;
  sf_args.colorname = colorname ;
  sf_args.ci = ci ;
  sf_args.spotfunction = NULL ;

  if ( gs_forall(extract_spotfunction_from_one_gstate, &sf_args,
                 TRUE /*include gstate*/, FALSE /*not grframes*/) )
    return sf_args.spotfunction ;
  return NULL ;
}


/* ---------------------------------------------------------------------- */
/* Two utility functions for setscreen, setcolorscreen & sethalftone.     */
static Bool checksinglescreen(OBJECT *freqo, SYSTEMVALUE *freqv,
                              OBJECT *anglo, SYSTEMVALUE *anglv,
                              OBJECT *proco,
                              Bool allowdict,
                              int32 *dictcount)
{
  HQASSERT( freqo , "freqo NULL in checksinglescreen" ) ;
  HQASSERT( freqv , "freqv NULL in checksinglescreen" ) ;
  HQASSERT( anglo , "anglo NULL in checksinglescreen" ) ;
  HQASSERT( anglv , "anglv NULL in checksinglescreen" ) ;
  HQASSERT( proco , "proco NULL in checksinglescreen" ) ;
  HQASSERT( dictcount , "dictcount NULL in checksinglescreen" ) ;

  if ( !object_get_numeric(freqo, freqv) ||
       !object_get_numeric(anglo, anglv) )
    return FALSE ;

  switch ( oType( *proco )) {
  case ODICTIONARY :
    if (! allowdict)
      return error_handler (TYPECHECK);
    ++(*dictcount) ;
    return TRUE ;
  case OARRAY :
  case OSTRING :
  case OPACKEDARRAY :
  case OFILE :
    if (! oCanExec( *proco ))
      if ( ! object_access_override(proco) )
        return error_handler( INVALIDACCESS ) ;

    if ( oType( *proco ) == OFILE ) {
      FILELIST *flptr ;
      flptr = oFile( *proco ) ;
      currfileCache = NULL ;
      if ( ! isIInputFile( flptr ) || ! isIOpenFileFilter( proco , flptr ))
        return error_handler( IOERROR ) ;
    }

  case OOPERATOR :
    if ( ! oExecutable( *proco ))
      return error_handler( TYPECHECK ) ;
    break;
  case ONAME :
  case OINTEGER :
  case OREAL :
  case OINFINITY :
    break;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if (*freqv < EPSILON)     /* check frequency */
    return error_handler (RANGECHECK);

  /* angle adjustment moved to newhalftones */

  return TRUE ;
}

/* -----------------------------------------------------------------------------
 * Initialises a SETHALFTONE_PARAMS structure ready for work...
 */
#define HT_RESET_INHERITOVERRIDES( _htparams ) MACRO_START      \
  (_htparams)->overridefreq      = TRUE ; \
  (_htparams)->overrideangle     = TRUE ; \
  (_htparams)->inheritfreq       = TRUE ; \
  (_htparams)->inheritangle      = TRUE ; \
MACRO_END

static void dosetht_init_sht_params( OBJECT *theo ,
                                     NAMECACHE *htname ,
                                     SETHALFTONE_PARAMS *htparams )
{
  HQASSERT( htparams , "htparams NULL in dosetht_init_sht_params" ) ;

  htparams->htdict_main       = theo ;
  htparams->htdict_use        = theo ;
  htparams->htdict_parent = NULL;

  HT_RESET_INHERITOVERRIDES( htparams ) ;

  htparams->fSeparationsKeySetInType5 = FALSE ;

  htparams->objectType = HTTYPE_DEFAULT; /* use this, if none specified */
  htparams->colorantIndex  = COLORANTINDEX_UNKNOWN ;
  htparams->nSubDictIndex = 0;

  htparams->nOriginalHalftoneType  = -1 ;
  htparams->nmOriginalHalftoneName  = htname ;
  htparams->nmOriginalColorKey = NULL ;

  htparams->nUseHalftoneType  = -1 ;
  htparams->nmUseHalftoneName  = NULL ;
  htparams->nmUseColorKey = NULL ;

  htparams->nOverrideHalftoneType  = -1 ;
  htparams->nmOverrideHalftoneName  = NULL ;
  htparams->nmOverrideColorKey = NULL ;

  htparams->nmAlternativeName = NULL;
  htparams->nmAlternativeColor = NULL;
  htparams->cacheType = HTTYPE_ALL;
  htparams->cAlternativeHalftoneRedirections = 0;

  htparams->encryptType = 0;
  htparams->encryptLength = 0;
}

/**
 * This routine is responsible for setting one of the missing screens that
 * has been calculated as required by setup_implicit_screens. In the simple
 * case this routine can just duplicate an existing screen. There are two
 * other alternatives. The first is where we need to simply override angles.
 * For a threshold screen this obviously is a no-op. For a halftone we need
 * to redo the halftone. The second alternative is where we are overriding
 * a screen with a halftone dictionary. In that case we need to implement
 * essentially the same override code that is in dosethalftone, hence we
 * make use of that routine by calling it with a ST_... type of ST_SETMISSING.
 * The code tries to minimise effort by seeing if it can simply do a duplicate.
 */
static Bool setup_implicit_screen(  corecontext_t *context,
                                    SPOTNO spotno,
                                    COLORANTINDEX ciToSetup,
                                    SYSTEMVALUE frequency ,
                                    OBJECT *spotfunction ,
                                    SETHALFTONE_PARAMS * htparams,
                                    NAMECACHE * nmColorName ,
                                    COLORANTINDEX ciCopyFrom,
                                    GS_COLORinfo *colorInfo )
{
  HTTYPE objtype = htparams != NULL ? htparams->objectType : HTTYPE_DEFAULT;

  /* Do we already have a screen for the colorant index concerned? */
  if ( ! ht_checkifchentry( spotno, objtype, ciToSetup,
                            gsc_getHalftonePhaseX( colorInfo ),
                            gsc_getHalftonePhaseY( colorInfo ))) {
    /* No, we don't have a screen, so let's go make one based on
       ciCopyFrom, which really, really must exist, taking into account
       any overriding angles specified for the new screens */
    COLORANTINDEX ciToSetupOverride ;
    Bool fNewOverride ;
    Bool fOldOverride ;
    SYSTEMVALUE new_angle ;
    SYSTEMVALUE old_angle ;
    OBJECT    *overridedata ;
    NAMECACHE *overridename ;
    NAMECACHE * nmHalftoneName = NULL;

    if (htparams != NULL)
      nmHalftoneName = htparams->nmOriginalHalftoneName;

    ciToSetupOverride = ciToSetup ;
    if ( rcbn_intercepting() && ciToSetup == rcbn_presep_screen(NULL) )
      ciToSetupOverride = rcbn_likely_separation_colorant() ;

    if (! guc_overrideScreenAngle(colorInfo->deviceRS, ciToSetupOverride,
                                  & new_angle, & fNewOverride))
      return FALSE;

    if (! guc_overrideScreenAngle(colorInfo->deviceRS, ciCopyFrom,
                                  & old_angle, & fOldOverride))
      return FALSE;

    /* if there is no override and we are recombining, need to force
       the screen angle to default for process colorants */
    if ( rcbn_intercepting() && rcbn_use_default_screen_angle(ciToSetup) )
      fNewOverride = TRUE ;

    /* See if we need to do an override. */
    if ( getdotshapeoverride(context, & overridename , & overridedata ) == SCREEN_OVERRIDE_SETHALFTONE )
    {
      SYSTEMVALUE freqv[ 4 ] ;
      SYSTEMVALUE anglv[ 4 ] ;
      OBJECT lnnewobj = OBJECT_NOTVM_NOTHING ;

      SETHALFTONE_PARAMS implicitparams ;

      dosetht_init_sht_params( NULL , nmHalftoneName , & implicitparams ) ;
      implicitparams.objectType = objtype;

      freqv[ 0 ] = freqv[ 1 ] = freqv[ 2 ] = freqv[ 3 ] = frequency ;
      anglv[ 0 ] = anglv[ 1 ] = anglv[ 2 ] = anglv[ 3 ] = new_angle ;

      theTags( lnnewobj ) = ONAME ;
      oName( lnnewobj ) = nmColorName ;

      if ( ! dosethalftone( context, & lnnewobj, NULL, ST_SETMISSING, spotno,
                            FALSE, freqv, anglv, colorInfo, & implicitparams ))
        return FALSE ;

      if ( ht_checkifchentry( spotno, objtype, ciToSetup,
                              gsc_getHalftonePhaseX( colorInfo ) ,
                              gsc_getHalftonePhaseY( colorInfo )))
        return TRUE ;
    }

    if ( ! ht_isSpotFuncScreen( spotno, objtype, ciCopyFrom,
                                gsc_getHalftonePhaseX( colorInfo ),
                                gsc_getHalftonePhaseY( colorInfo )) ||
         ( ! fNewOverride ||
           ( fOldOverride && new_angle == old_angle ))) {
      if ( ! ht_duplicatechentry( spotno, objtype, ciToSetup, nmColorName,
                                  nmHalftoneName, spotno, objtype, ciCopyFrom,
                                  gsc_getHalftonePhaseX( colorInfo ) ,
                                  gsc_getHalftonePhaseY( colorInfo )))
        return FALSE ;
    } else {
      SYSTEMVALUE angle = ( SYSTEMVALUE )new_angle ;

      HQASSERT( frequency != 0.0 && spotfunction != NULL ,
                "setup_implicit_screen trying to change angle of a threshold/modular screen" ) ;

      HQASSERT( ! ht_checkifchentry( spotno, objtype, ciToSetup,
                                     gsc_getHalftonePhaseX( colorInfo ) ,
                                     gsc_getHalftonePhaseY( colorInfo )),
                                     "screen already exists" ) ;
      if ( ! newhalftones( context, & frequency ,
                           & angle ,
                           spotfunction ,
                           spotno, objtype, ciToSetup,
                           nmHalftoneName ,
                           nmColorName ,
                           htparams != NULL ? htparams->nmAlternativeName : NULL,
                           htparams != NULL ? htparams->nmAlternativeColor : NULL,
                           htparams != NULL ? htparams->cacheType : HTTYPE_ALL,
                           FALSE ,
                           FALSE ,
                           FALSE ,
                           FALSE ,
                           NULL ,
                           FALSE ,
                           TRUE ,
                           TRUE ,
                           TRUE ,
                           gsc_getHalftonePhaseX( colorInfo ) ,
                           gsc_getHalftonePhaseY( colorInfo ) ,
                           NULL , /* poAdjustScreen */
                           colorInfo ))
        return FALSE ;
      HQASSERT( ht_checkifchentry( spotno, objtype, ciToSetup,
                                   gsc_getHalftonePhaseX( colorInfo ) ,
                                   gsc_getHalftonePhaseY( colorInfo )) ,
                "newhalftones succeeded but halftone didn't get created" ) ;
    }
  }
  return TRUE ;
}

/**
 * This routine is responsible for calculating any missing screens required:
 * Having set up the screens in a multitude of different ways we must also
 * deal with screens which are only implied, such as the default for spot
 * colors when only setscreen or setcolorscreen is done, or the screen for a
 * specific spot color when the angle is explicitly specified by
 * DefaultScreenAngles. By definition, any spot colours must use the Black
 * screen (RB2, page 319 para 1), except that it is now possible to override
 * angles individually for each spot color, with DefaultScreenAngles, so we
 * have to scan spot colors and generate their screens. DefaultScreenAngles
 * is decomposed into graphics state entries for the process colors and
 * separation structure fields for the spot colors.
 */
static Bool setup_implicit_screens( corecontext_t *context,
                                    SYSTEMVALUE frequency ,
                                    OBJECT *spotfunction ,
                                    SPOTNO spotno ,
                                    Bool fFromHalftoneType5,
                                    SETHALFTONE_PARAMS * htparams,
                                    GS_COLORinfo *colorInfo )
{
  COLORANTINDEX ciCopyFrom ;
  GUCR_CHANNEL* hf;
  GUCR_COLORANT* hc;
  HTTYPE objtype = htparams != NULL ? htparams->objectType : HTTYPE_DEFAULT;
  DL_STATE *page = context->page;

  /* First deal with any remaining process color screens: either the angle isnt
     overridden for a screen, or is the same as the black override angle; in either
     case duplicate the black (providing it exists - the default if it does not. This
     doesn't catch every case (e.g. black not overridden but angle the same
     nevertheless), but this is much less likely so we'll catch the duplicate while
     generating the screen instead.  Note the duplication order (K)YMC; this is to
     make sure the list comes out as CMYK once built.

     For Type 5 halftones, go straight for Default (which must be present) */

  if (fFromHalftoneType5)
  {
    /* By definition, halftone type 5 use the Default screen for missing colorants */
    ciCopyFrom = COLORANTINDEX_NONE ;
    HQASSERT( ht_checkifchentry( spotno, objtype, ciCopyFrom,
                                 gsc_getHalftonePhaseX( colorInfo ) ,
                                 gsc_getHalftonePhaseY( colorInfo )) ,
              "Must have Default screen in a halftone type 5" ) ;
  }
  else
  {
    ciCopyFrom = guc_colorantIndexReserved (colorInfo->deviceRS, system_names + NAME_Black);

    /* If we've never heard of "Black", try "Gray", which is analogous */
    if (ciCopyFrom == COLORANTINDEX_UNKNOWN ||
        ! ht_checkifchentry( spotno, objtype, ciCopyFrom,
                             gsc_getHalftonePhaseX( colorInfo ) ,
                             gsc_getHalftonePhaseY( colorInfo )))
      ciCopyFrom = guc_colorantIndexReserved (colorInfo->deviceRS, system_names + NAME_Gray);

    /* If Gray is also unknown, or we don't have a screen for either
       Black or Gray, use Default instead */

    if ( ciCopyFrom == COLORANTINDEX_UNKNOWN ||
         ! ht_checkifchentry( spotno, objtype, ciCopyFrom,
                              gsc_getHalftonePhaseX( colorInfo ) ,
                              gsc_getHalftonePhaseY( colorInfo ))) {
      /* We must have Default */
      ciCopyFrom = COLORANTINDEX_NONE ;
      HQASSERT( ht_checkifchentry( spotno, objtype, ciCopyFrom,
                                   gsc_getHalftonePhaseX( colorInfo ) ,
                                   gsc_getHalftonePhaseY( colorInfo )) ,
                "neither a Black nor a Default screen found" ) ;
    }

    /* Make a Default from the Black if necessary */
    if (ciCopyFrom != COLORANTINDEX_NONE) {

      if ( ! setup_implicit_screen( context, spotno ,
                                    COLORANTINDEX_NONE ,
                                    frequency ,
                                    spotfunction ,
                                    htparams ,
                                    system_names + NAME_Default ,
                                    ciCopyFrom ,
                                    colorInfo ))
        return FALSE ;
    }
  }

  /* And make screens for any colorants which don't have them so far;
     this may see the same colorant more than once, but that's OK - we
     will quickly discover that it is already there. */
  for (hf = gucr_framesStart (colorInfo->deviceRS); gucr_framesMore (hf); gucr_framesNext (& hf)) {

    for (hc = gucr_colorantsStart (hf);
         gucr_colorantsMore (hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext (& hc)) {
      const GUCR_COLORANT_INFO *colorantInfo;
      COLORANTINDEX ciBasedOn, ciChain;

      if ( !gucr_colorantDescription(hc, &colorantInfo) )
        continue; /* blank colorant */

      ciBasedOn = ciCopyFrom;

      if (page->colorPageParams.halftoneColorantMapping) {
        ciChain = colorantInfo->colorantIndex;
        for (;;) {
          ciChain = guc_getInverseColorant (colorInfo->deviceRS, ciChain);
          if (ciChain == COLORANTINDEX_UNKNOWN)
            break;
          if (ht_checkifchentry( spotno, objtype, ciChain,
                                 gsc_getHalftonePhaseX( colorInfo ) ,
                                 gsc_getHalftonePhaseY( colorInfo ))) {
            ciBasedOn = ciChain;
            break;
          }
        }
      }

      if ( ! setup_implicit_screen( context, spotno ,
                                    colorantInfo->colorantIndex ,
                                    frequency ,
                                    spotfunction ,
                                    htparams ,
                                    colorantInfo->originalName != NULL ?
                                    colorantInfo->originalName :
                                    colorantInfo->name ,
                                    ciBasedOn ,
                                    colorInfo ))
        return FALSE ;
    }
  }

  if ( rcbn_intercepting()) {
    COLORANTINDEX ciActual ;

    ciActual = rcbn_likely_separation_colorant() ;

    if ( ciActual != COLORANTINDEX_NONE ) {
      /* Have some reasonable evidence of what the current separation might
         be. Build the special presep screen, take the angle override from
         the ci of our best guess at the current separation. If we do not
         have any idea what the current separation is we just use the
         default screen.
      */
      NAMECACHE    *nmPseudo ;
      COLORANTINDEX ciPseudo ;

      /* Get name and index for the current presep screen */
      ciPseudo = rcbn_presep_screen( & nmPseudo ) ;

      if ( ! setup_implicit_screen( context, spotno ,
                                    ciPseudo ,
                                    frequency ,
                                    spotfunction ,
                                    htparams ,
                                    nmPseudo ,
                                    ciCopyFrom ,
                                    colorInfo ))
        return FALSE ;
    }
  }
  return TRUE ;
}

/**
 * This routine is responsible for invoking a set[color]screen. It must first
 * of all check to see if any of the procedures are a pattern. If so, then
 * we install the pattern. If not, then we must consider if we've got a halftone
 * override and then call setscreens_execargs_dict(...).
 */
static Bool setscreens_execargs_proc( corecontext_t *context,
                                      GS_COLORinfo *colorInfo, STACK *stack ,
                                      int32 screentype, int32 sl,
                                      SPOTNO lspotid, Bool *ispattern,
                                      SYSTEMVALUE freqv[ 4 ] , SYSTEMVALUE anglv[ 4 ] ,
                                      OBJECT *proco[ 4 ] )
{
  Bool override_by_dict_param = FALSE;
  Bool override_by_key;
  Bool tellMeIfPatternScreen = FALSE;
  int32 nColorant, nColorants;
  int32 nScreen;
  COLORANTINDEX aMapping[4];
  NAMECACHE * anmColorName[4];
  DEVICESPACEID deviceSpaceId;

  /* Protect stack from recursive interpreter */
  npop( sl , stack ) ;

  /* override_by_key means that this screen is being overridden by the
     current screen using /Override. */
  override_by_key = colorInfo->halftoneInfo->halftoneoverride > 0;
  /* override_by_dict_param means that we are overriding this screen
     with a halftone dict from the system param. */
  if ( ! override_by_key ) {
    OBJECT    *overridedata ;
    NAMECACHE *overridename ;
    override_by_dict_param =
      getdotshapeoverride(context, & overridename , & overridedata )
      == SCREEN_OVERRIDE_SETHALFTONE;
  }

  /* If we are overriding angles then screens for each of the four colors of a
     setscreen are different, so effectively it turns into a setcolorscreen */

  guc_deviceColorSpace (colorInfo->deviceRS, & deviceSpaceId, & nColorants);
  if (deviceSpaceId != DEVICESPACE_N &&
      (! rcbn_intercepting() || ! rcbn_build_preseparated_screens())) {

    if (!guc_simpleDeviceColorSpaceMapping (colorInfo->deviceRS, deviceSpaceId,
                                            aMapping, nColorants))
      return FALSE;

    if ( screentype == ST_SETSCREEN ) {
      for (nColorant = 0; nColorant < nColorants; nColorant++) {
        SYSTEMVALUE angle; /* unused here after return from function */
        Bool fOverrideScreenAngle;
        if (! guc_overrideScreenAngle(colorInfo->deviceRS, aMapping[nColorant],
                                      & angle, & fOverrideScreenAngle))
          return FALSE;
        if (fOverrideScreenAngle) {
          screentype = ST_SETCOLORSCREEN;
          break;
        }
      }
    }

    nScreen = 0;

    /* Set up the names of the screen colorants */

    switch (deviceSpaceId) {
    case DEVICESPACE_Gray:
      anmColorName[0] = system_names + NAME_Gray;
      nScreen = 3;
      break;
    case DEVICESPACE_CMYK:
    case DEVICESPACE_CMY:
      anmColorName[0] = system_names + NAME_Cyan;
      anmColorName[1] = system_names + NAME_Magenta;
      anmColorName[2] = system_names + NAME_Yellow;
      anmColorName[3] = system_names + NAME_Black; /* overwritten below in CMY */
      break;
    case DEVICESPACE_RGB:
    case DEVICESPACE_RGBK:
      anmColorName[0] = system_names + NAME_Red;
      anmColorName[1] = system_names + NAME_Green;
      anmColorName[2] = system_names + NAME_Blue;
      anmColorName[3] = system_names + NAME_Black; /* overwritten below in RGB */
      break;
    case DEVICESPACE_Lab:
      HQFAIL ("Lab not implemented here yet");
      break;
    default:
      HQFAIL ("unrecognised device color space");
      break;
    }

    /* Set up defaults from the fourth screen for three-colorant color
       spaces (we could do similarly for single colorant space, but we
       get a default created from it anyway by setup_implicit_screens */
    switch (deviceSpaceId) {
    case DEVICESPACE_RGB:
    case DEVICESPACE_CMY:
      anmColorName[3] = system_names + NAME_Default;
      aMapping[3] = COLORANTINDEX_NONE;
      nColorants = 4; /* OK, so we lie, but there are four screens */
      break;
    default:
      break;
    }

  } else {

    /* DeviceN - all we do here is set the Default and inherit from it when setting
       implicit screens */
    nColorants = 1;
    nScreen = 3;
    aMapping[0] = COLORANTINDEX_NONE;
    anmColorName[0] = system_names + NAME_Default;
  }

  for (nColorant = 0; nColorant < nColorants; nColorant++, nScreen++) {

    HQASSERT( ! ht_checkifchentry( lspotid, HTTYPE_DEFAULT, aMapping[nColorant],
                                   gsc_getHalftonePhaseX( colorInfo ) ,
                                   gsc_getHalftonePhaseY( colorInfo )) ,
                               "screen already exists" ) ;
    if ( ! newhalftones( context, freqv + nScreen ,
                         anglv + nScreen ,
                         proco[ nScreen ] ,
                         lspotid, HTTYPE_DEFAULT, aMapping[nColorant],
                         NULL ,
                         anmColorName[nColorant],
                         NULL, NULL, HTTYPE_ALL,
                         FALSE ,
                         FALSE ,
                         FALSE ,
                         FALSE ,
                         ( override_by_key || override_by_dict_param ) ?
                           ( & tellMeIfPatternScreen ) : NULL,
                         TRUE ,
                         TRUE ,
                         TRUE ,
                         TRUE ,
                         gsc_getHalftonePhaseX( colorInfo ) ,
                         gsc_getHalftonePhaseY( colorInfo ) ,
                         NULL, /* poAdjustScreen */
                         colorInfo ))
      return FALSE ;

    HQASSERT( (override_by_key || override_by_dict_param) ||
              ht_checkifchentry( lspotid, HTTYPE_DEFAULT, aMapping[nColorant],
                                 gsc_getHalftonePhaseX( colorInfo ) ,
                                 gsc_getHalftonePhaseY( colorInfo )) ,
              "newhalftones succeeded but halftone didn't get created" ) ;

    if ( (override_by_key || override_by_dict_param) && tellMeIfPatternScreen ) {
      /* At least one screen is a pattern screen */
      (* ispattern) = TRUE ;
      override_by_key = FALSE;
      override_by_dict_param = FALSE;
    }
  }

  /* Is this screen being overridden (implying it wasn't a pattern)? */
  halftoneoverride = FALSE;
  if ( override_by_key || override_by_dict_param ) {
    HQASSERT( ! tellMeIfPatternScreen , "should have dealt with pattern case" ) ;
    /* Getting here means that no pattern screens around - so ignore screen */

    if ( override_by_dict_param ) {
      (void)purgehalftones( lspotid, TRUE ); /* Start with a clean sheet */
      return setscreens_execargs_dict( context, colorInfo, stack, screentype, TRUE, sl,
                                       lspotid , freqv , anglv , proco ) ;
    }
    halftoneoverride = TRUE;
    return TRUE ;
  }

  if ( ! setup_implicit_screens( context, freqv[ 3 ] , proco[ 3 ] ,
                                 lspotid , FALSE , NULL , colorInfo ))
    return FALSE ;

  /* Finally, it is not a halftone dictionary, so say so in the gstate. */
  theTags( colorInfo->halftoneInfo->halftonedict ) = ONULL ;
  return TRUE ;
}

/**
 * This routine is responsible for invoking a sethalftone, or a setscreen
 * that contained a halftone dictionary. It does a dummy run of dosethalftone
 * to first of all check the arguments, and then does the real run.
 */
static Bool setscreens_execargs_dict(  corecontext_t *context,
                                       GS_COLORinfo *colorInfo,
                                       STACK *stack ,
                                       int32 screentype,
                                       Bool htfromsetscreen,
                                       int32 sl,
                                       int32 lspotid ,
                                       SYSTEMVALUE freqv[ 4 ] ,
                                       SYSTEMVALUE anglv[ 4 ] ,
                                       OBJECT *proco[ 4 ] )
{
  OBJECT *theo = proco[ 3 ] ;

  if ( screentype == ST_SETHALFTONE ) {
    HQASSERT( oType( *theo ) == ODICTIONARY, "should only have dict type" ) ;
    if ( oType( *theo ) == ODICTIONARY ) {
      OBJECT *thed = oDict( *theo ) ;
      if ( ! oCanRead( *thed ) && ! object_access_override(thed) )
        return error_handler( INVALIDACCESS ) ;
    }
  }

  /* Check args in dictionary. */
  halftoneoverride = FALSE ;

  if ( htfromsetscreen ) {
    if ( ! dosethalftone( context, NULL, theo, screentype, 0, TRUE , freqv, anglv, colorInfo, NULL ))
      return FALSE ;
  }
  else {
    if ( ! dosethalftone( context, NULL, theo, screentype, 0, TRUE , NULL, NULL, colorInfo, NULL ))
      return FALSE ;
  }

  /* Protect stack from recursive interpreter */
  if ( screentype == ST_SETHALFTONE )
    npop( sl , stack ) ;

  /* See if halftone is being overridden by a previous one. */
  if ( halftoneoverride )
    return TRUE ;

  /* Check if any screen exists (with the right phase) */
  HQASSERT( !ht_checkifchentry( lspotid, HTTYPE_DEFAULT, COLORANTINDEX_UNKNOWN,
                                gsc_getHalftonePhaseX( colorInfo ),
                                gsc_getHalftonePhaseY( colorInfo )) ,
            "screen already exists" ) ;

  if ( htfromsetscreen ) {
    if ( ! dosethalftone( context, NULL, theo, screentype, lspotid, FALSE, freqv, anglv, colorInfo, NULL ))
      return FALSE ;
  }
  else {
    if ( ! dosethalftone( context, NULL, theo, screentype, lspotid, FALSE, NULL, NULL, colorInfo, NULL ))
      return FALSE ;
  }

  /* Note: I am assuming here that a Default screen will always exist if
     any screen of a set has been installed.*/
  HQASSERT( ht_checkifchentry( lspotid, HTTYPE_DEFAULT, COLORANTINDEX_NONE,
                               gsc_getHalftonePhaseX( colorInfo ),
                               gsc_getHalftonePhaseY( colorInfo )) ,
                            "dosethalftone succeeded but halftone(s) didn't get created" ) ;

  if ( screentype == ST_SETHALFTONE ) {
    Copy( & colorInfo->halftoneInfo->halftonedict , theo ) ;
  } else {
    /* Finally, it is not a halftone dictionary, so say so in the gstate. */
    theTags( colorInfo->halftoneInfo->halftonedict ) = ONULL ;
  }

  return TRUE ;
}

/**
 * This routine is responsible for invoking either setscreen, setcolorscreen
 * or sethalftone. It therefore needs to decide which of these it's doing,
 * and then do it. It's complicated by the fact that a dictionary argument
 * to set[color]screen actually means do a sethalftone. The screen is installed
 * into the gstate. If an error occurs then any partial screens installed are
 * removed and the spot number reclaimed (if possible). The arguments on the
 * operand stack are passed over to the temporary stack before invoking any
 * callbacks.
 */

Bool gsc_setscreens( GS_COLORinfo *colorInfo, STACK *stack , int32 screentype )
{
  corecontext_t *context = get_core_context_interp();
  DL_STATE *page = context->page;
  int32 sl = 0;
  SPOTNO newspotid;
  Bool htfromsetscreen ;
  OBJECT *theo ;
  SYSTEMVALUE freqv[ 4 ] ;
  SYSTEMVALUE anglv[ 4 ] ;
  OBJECT     *proco[ 4 ] ;
  Bool       result = FALSE;
  GS_HALFTONEinfo *oldHalftoneInfo;

  if ( ! setscreens_checkargs( stack , screentype , & sl , freqv , anglv , proco ))
    return FALSE ;

#ifdef PS2_PDFOUT
  /* ifdef out as a reminder to revisit when implementing PDF out */
  if ( ! flush_vignette( VD_Default ))
    return FALSE ; /* Y for pdfout sake */
#endif

  /* See if we got a setscreen that should have been a sethalftone. */
  htfromsetscreen = FALSE ;
  theo = proco[ 3 ] ;
  if ( oType( *theo ) == ODICTIONARY ) {
    if ( screentype != ST_SETHALFTONE ) {
      screentype = ST_SETHALFTONE ;
      htfromsetscreen = TRUE ;
    }
  }

  oldHalftoneInfo = colorInfo->halftoneInfo;
  cc_reservehalftoneinfo( oldHalftoneInfo );

  if ( ! cc_updatehalftoneinfo( &colorInfo->halftoneInfo ) ) {
    cc_destroyhalftoneinfo( &oldHalftoneInfo ); /* as cc_updatehalftoneinfo didn't */
    return FALSE;
  }
  if ( ! cc_invalidateColorChains( colorInfo, TRUE ))
    goto failure;

  /* Clear the transfer cache, it will be set during screen installation.
   * NB. Although this modifies the gstate before argument checking, the gstate
   * will be restored to its original state upon failure. It will also be
   * restored if the gstate halftone has a /Override.
   */
  if (!clearTransferCache(colorInfo->halftoneInfo))
    goto failure;

  newspotid = ++spotid ; /* Allocate this up front; reclaim if error. */

  if ( screentype == ST_SETHALFTONE ) {
    result = setscreens_execargs_dict( context, colorInfo, stack , screentype , htfromsetscreen, sl,
                                       newspotid , freqv , anglv , proco ) ;
  }
  else {
    /* ST_SETSCREEN or ST_SETCOLORSCREEN */
    int32 i ;
    Bool ispattern = FALSE;

    result = setscreens_execargs_proc( context, colorInfo, stack, screentype, sl,
                                       newspotid , & ispattern , freqv , anglv , proco ) ;

    if ( result && ! halftoneoverride ) {
      /* If screen was a pattern, store new ST_... type. */
      if ( ispattern ) {
        if ( screentype == ST_SETCOLORSCREEN )
          screentype = ST_SETCOLORPATTERN ;
        else
          screentype = ST_SETPATTERN ;
      }

      for ( i = 0 ; i < 4 ; ++i ) {
        colorInfo->halftoneInfo->halftones[ i ].freq = ( USERVALUE )freqv[ i ] ;
        colorInfo->halftoneInfo->halftones[ i ].angl = ( USERVALUE )anglv[ i ] ;
        Copy( &colorInfo->halftoneInfo->halftones[ i ].spotfn , proco[ i ] ) ;
      }
    }
  }

  if ( result && ! halftoneoverride ) { /* New screen successfully installed */
    SPOTNO eqlspotid;
    SPOTNO oldspotid = colorInfo->halftoneInfo->spotno;

    colorInfo->halftoneInfo->screentype = ( uint8 )screentype ;

    if ( (eqlspotid = ht_equivalentchspotid(newspotid, oldspotid)) > 0 ) {
      Bool purged;

      /* Found an existing screen to match the new one - get rid of new one. */
      purged = purgehalftones(newspotid, TRUE);
      if ( purged && newspotid == spotid )
        /* New spotid was last one used - reclaim it to be used again. */
        --spotid;
      /* Use the matching screen instead of the new screen */
      newspotid = eqlspotid;
    }

    if ( newspotid != oldspotid ) {
      ht_change_non_purgable_screen(oldspotid, newspotid);
      HQTRACE(debug_regeneration, ("gstate new spotno: %d", newspotid));
      cc_note_uncalibrated_screens(colorInfo, page, newspotid);
      colorInfo->halftoneInfo->spotno = newspotid;
      gsc_markChainsInvalid(colorInfo);
    }

    colorInfo->halftoneInfo->halftoneid = ++HalftoneId ;

    /* Get rid of the saved info, don't delete the save chains here.
     */
    cc_destroyhalftoneinfo( &oldHalftoneInfo );

    /* Update equivalent colorants for current blend space target. */
    result = guc_updateEquivalentColorants(gsc_getTargetRS(colorInfo),
                                           COLORANTINDEX_ALL);
  }
  else {
    Bool purged;

    /* Try and reclaim the last spotid so we can keep spotids as small
       numbers (if possible). */
    purged = purgehalftones( newspotid, TRUE );
    if ( purged && newspotid == spotid )
      --spotid ;

    /* Reinstate the saved info. */
    cc_destroyhalftoneinfo( &colorInfo->halftoneInfo );
    colorInfo->halftoneInfo = oldHalftoneInfo;
  }

  npop( sl , & temporarystack ) ;
  return result ;

 failure:
  /* Reinstate the saved info. */
  cc_destroyhalftoneinfo( &colorInfo->halftoneInfo );
  colorInfo->halftoneInfo = oldHalftoneInfo;
  return result;
}


/* ---------------------------------------------------------------------- */
Bool gsc_currentscreens( corecontext_t *context, GS_COLORinfo *colorInfo,
                         STACK *stack, int32 i , int32 j )
{
  OBJECT *o1 , *o2 , *theo ;
  GS_HALFTONEinfo *halftoneInfo = colorInfo->halftoneInfo;
  COLOR_PAGE_PARAMS *colorPageParams = &context->page->colorPageParams;

  HQASSERT(i < 4, "i is bigger than 3");
  HQASSERT(j < 4, "j is bigger than 3");

  /* Is required to push 0 60 halftone dictionary in the event of there being
   * a halftone dictionary active. However, this really doesn't work in
   * practice, so we go to some trouble to extract appropriate frequencies
   * and angles from the halftone dictionary (or subdictionary).
   */

  if ( halftoneInfo->screentype != ST_SETHALFTONE ) {
    while ( i <= j ) {
      if ( ! stack_push_real( halftoneInfo->halftones[ i ].freq, stack) ||
           ! stack_push_real( halftoneInfo->halftones[ i ].angl, stack) ||
           ! push( &halftoneInfo->halftones[ i ].spotfn, stack ))
        return FALSE ;
      ++i ;
    }

  } else {
    OBJECT i1 = OBJECT_NOTVM_NOTHING , i2 = OBJECT_NOTVM_NOTHING ;

    theTags( i1 ) = OREAL;
    oReal( i1 ) = 60.0f ;
    theTags( i2 ) = OREAL;
    oReal( i2 ) = 0.0f ;

    theo = fast_extract_hash_name(&halftoneInfo->halftonedict,
                                  NAME_HalftoneType);
    switch (oInteger(*theo)) {

    case 1:
      o1 = fast_extract_hash_name(&halftoneInfo->halftonedict,
                                  NAME_Frequency) ;
      if ( o1 == NULL )
        o1 = &i1 ;
      o2 = fast_extract_hash_name(&halftoneInfo->halftonedict,
                                  NAME_Angle) ;
      if ( o2 == NULL )
        o2 = &i2 ;
      while ( i <= j ) {
        if (! push3(o1, o2,
                    &halftoneInfo->halftonedict,
                    stack))
          return FALSE;
        ++i ;
      }
      break;

    case 2:
      o1 = &i1 ;
      o2 = &i2 ;
      while ( i <= j ) {
        if ( colorPageParams->useAllSetScreen ) {
          o1 = fast_extract_hash_name(&halftoneInfo->halftonedict,
                                      frequency_names[i]) ;
          if ( o1 == NULL )
            o1 = &i1 ;
          o2 = fast_extract_hash_name(&halftoneInfo->halftonedict,
                                      angle_names[i]) ;
          if ( o2 == NULL )
            o2 = &i2 ;
        }
        if (! push3(o1, o2,
                    &halftoneInfo->halftonedict,
                    stack))
          return FALSE;
        ++i ;
      }
      break;

    default:
      HQFAIL("currentscreen can't handle current halftone type!");
      /*@fallthrough@*/
    case 3: case 4: case 6: case 10: case 16: case 100:
    case 195: /** \todo Should probably construct dicts for each colorant. */
      o1 = &i1 ;
      o2 = &i2 ;
      while ( i <= j ) {
        if (! push3(o1, o2,
                    &halftoneInfo->halftonedict,
                    stack))
          return FALSE;
        ++i ;
      }
      break;

    case 5:
      o1 = &i1 ;
      o2 = &i2 ;
      while ( i <= j ) {
        if ( colorPageParams->useAllSetScreen ) {

          oName(nnewobj) = pcmCMYKNames[i] ;
          theo = fast_extract_hash(&halftoneInfo->halftonedict,
                                   &nnewobj);
          if (! theo || oType( *theo ) != ODICTIONARY) {
            oName(nnewobj) = pcmRGBGyNames[i] ;
            theo = fast_extract_hash(&halftoneInfo->halftonedict,
                                     &nnewobj);
            if (! theo || oType( *theo ) != ODICTIONARY) {
              theo = fast_extract_hash_name(&halftoneInfo->halftonedict,
                                            NAME_Default);
            }
          }

          o1 = o2 = NULL ;
          if (theo && oType( *theo ) == ODICTIONARY) {
            o1 = fast_extract_hash_name(theo, NAME_Frequency);
            o2 = fast_extract_hash_name(theo, NAME_Angle);
          }

          if ( ! o1 )
            o1 = &i1 ;
          if ( ! o2 )
            o2 = &i2 ;
        }
        if (! push3(o1, o2,
                    &halftoneInfo->halftonedict,
                    stack))
          return FALSE;
        ++i ;
      }
      break;
    }
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Halftone dictionary matches, by type of dictionary.  See htmatches(). */

#define HT_MATCH_SEPARATION 0 /* separation control */
#define HT_MATCH_GENERAL    1 /* general control */
#define HT_MATCH_TRANSFER   2 /* transfer function */
#define HT_MATCH_BASIC      3 /* general (either threshold or spot function) */
#define HT_MATCH_EXTRA      4 /* halftone type specific */

#define HT_MAX_MATCHES      5

/* extensions to support separation control: */
static NAMETYPEMATCH htmatch_separation[] = {
/* 0 */ { NAME_Positions | OOPTIONAL,             2, { OARRAY,OPACKEDARRAY }},
/* 1 */ { NAME_Background | OOPTIONAL,            1, { OBOOLEAN }},
/* 2 */ { NAME_DCS | OOPTIONAL,                   1, { ODICTIONARY }},
         DUMMY_END_MATCH
};

/* extensions to support general control: */
static NAMETYPEMATCH htmatch_general[] = {
  { NAME_Override | OOPTIONAL,              1, { OINTEGER }},
  { NAME_OverrideFrequency | OOPTIONAL,     1, { OBOOLEAN }},
  { NAME_OverrideAngle | OOPTIONAL,         1, { OBOOLEAN }},
  { NAME_InheritFrequency | OOPTIONAL,      1, { OBOOLEAN }},
  { NAME_InheritAngle | OOPTIONAL,          1, { OBOOLEAN }},
  DUMMY_END_MATCH
};

/** Constants for slots in a dictmatch for \c htmatch_general. */
enum { mg_Override, mg_OverrideFrequency, mg_OverrideAngle,
       mg_InheritFrequency, mg_InheritAngle };


static NAMETYPEMATCH htmatch_transfer[] = {
/* 0 */  { NAME_TransferFunction | OOPTIONAL,      3, { OARRAY, OPACKEDARRAY, OFILE }},
         DUMMY_END_MATCH
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type1 Halftone. */
static NAMETYPEMATCH htmatch1_basic[] = {
/* 0 */  { NAME_Frequency,                         2, { OINTEGER, OREAL }},
/* 1 */  { NAME_Angle,                             2, { OINTEGER, OREAL }},
/* 2 */  { NAME_SpotFunction,                      0  },
/* 3 */  { NAME_DoubleScreens | OOPTIONAL,         1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH htmatch1_extra[] = {
/* 0 */  { NAME_ActualFrequency | OOPTIONAL,       2, { OINTEGER, OREAL }},
/* 1 */  { NAME_ActualAngle | OOPTIONAL,           2, { OINTEGER, OREAL }},
/* 2 */  { NAME_AccurateScreens | OOPTIONAL,       1, { OBOOLEAN }},
/* 3 */  { NAME_ScreenChanges | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch1[ 5 ] = {
  htmatch_separation ,
  htmatch_general ,
  htmatch_transfer ,
  htmatch1_basic ,
  htmatch1_extra ,
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type2 Halftone. */
static NAMETYPEMATCH htmatch2_basic[] = {
/* 0 */  { NAME_RedFrequency,                      2, { OINTEGER, OREAL }},
/* 1 */  { NAME_RedAngle,                          2, { OINTEGER, OREAL }},
/* 2 */  { NAME_RedSpotFunction,                   0  },
/* 3 */  { NAME_GreenFrequency,                    2, { OINTEGER, OREAL }},
/* 4 */  { NAME_GreenAngle,                        2, { OINTEGER, OREAL }},
/* 5 */  { NAME_GreenSpotFunction,                 0  },
/* 6 */  { NAME_BlueFrequency,                     2, { OINTEGER, OREAL }},
/* 7 */  { NAME_BlueAngle,                         2, { OINTEGER, OREAL }},
/* 8 */  { NAME_BlueSpotFunction,                  0  },
/* 9 */  { NAME_GrayFrequency,                     2, { OINTEGER, OREAL }},
/*10 */  { NAME_GrayAngle,                         2, { OINTEGER, OREAL }},
/*11 */  { NAME_GraySpotFunction,                  0  },
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch2[ 5 ] = {
  NULL ,
  htmatch_general ,
  NULL ,
  htmatch2_basic ,
  NULL
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type3 Halftone. */
static NAMETYPEMATCH htmatch3_basic[] = {
/* 0 */  { NAME_Width,                             1, { OINTEGER }},
/* 1 */  { NAME_Height,                            1, { OINTEGER }},
/* 2 */  { NAME_Thresholds,                        1, { OSTRING }},
/* 3 */  { NAME_Depth | OOPTIONAL,                 1, { OINTEGER }},
/* 4 */  { NAME_Invert | OOPTIONAL,                1, { OBOOLEAN }},
/* 5 */  { NAME_ScreenExtraGrays | OOPTIONAL,      1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch3[ 5 ] = {
  htmatch_separation ,
  htmatch_general ,
  htmatch_transfer ,
  htmatch3_basic ,
  NULL
};

enum { mb_threshold_Width, mb_threshold_Height, mb_threshold_Thresholds,
       /* those three must be first because they are shared with type 4 */
       mb_threshold_Depth, mb_threshold_Invert, mb_threshold_ScreenExtraGrays,
       mb_threshold_Limit };


/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type4 Halftone. */
static NAMETYPEMATCH htmatch4_basic[] = {
/* 0 */  { NAME_RedWidth,                          1, { OINTEGER }},
/* 1 */  { NAME_RedHeight,                         1, { OINTEGER }},
/* 2 */  { NAME_RedThresholds,                     2, { OSTRING , OFILE }},
/* 3 */  { NAME_GreenWidth,                        1, { OINTEGER }},
/* 4 */  { NAME_GreenHeight,                       1, { OINTEGER }},
/* 5 */  { NAME_GreenThresholds,                   2, { OSTRING , OFILE }},
/* 6 */  { NAME_BlueWidth,                         1, { OINTEGER }},
/* 7 */  { NAME_BlueHeight,                        1, { OINTEGER }},
/* 8 */  { NAME_BlueThresholds,                    2, { OSTRING , OFILE }},
/* 9 */  { NAME_GrayWidth,                         1, { OINTEGER }},
/*10 */  { NAME_GrayHeight,                        1, { OINTEGER }},
/*11 */  { NAME_GrayThresholds,                    2, { OSTRING , OFILE }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch4[ 5 ] = {
  NULL ,
  htmatch_general ,
  NULL ,
  htmatch4_basic ,
  NULL
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type5 Halftone. */
static NAMETYPEMATCH htmatch5_basic[] = {
/* 0 */  { NAME_Default,                           1, { ODICTIONARY }},
/* 1 */  { NAME_Separations | OOPTIONAL,           1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch5[ 5 ] = {
  NULL ,
  htmatch_general ,
  NULL ,
  htmatch5_basic ,
  NULL
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type6 Halftone. */
static NAMETYPEMATCH htmatch6_basic[] = {
/* 0 */  { NAME_Width,                             1, { OINTEGER }},
/* 1 */  { NAME_Height,                            1, { OINTEGER }},
/* 2 */  { NAME_Thresholds,                        1, { OFILE }},
/* 3 */  { NAME_Depth | OOPTIONAL,                 1, { OINTEGER }},
/* 4 */  { NAME_Invert | OOPTIONAL,                1, { OBOOLEAN }},
/* 5 */  { NAME_ScreenExtraGrays | OOPTIONAL,      1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch6[ 5 ] = {
  htmatch_separation ,
  htmatch_general ,
  htmatch_transfer ,
  htmatch6_basic ,
  NULL
};


/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type10 Halftone. */
static NAMETYPEMATCH htmatch10_basic[] = {
/* 0 */  { NAME_Xsquare,                           1, { OINTEGER }},
/* 1 */  { NAME_Ysquare,                           1, { OINTEGER }},
/* 2 */  { NAME_Thresholds,                        2, { OSTRING, OFILE }},
/* 3 */  { NAME_Depth | OOPTIONAL,                 1, { OINTEGER }},
/* 4 */  { NAME_Invert | OOPTIONAL,                1, { OBOOLEAN }},
/* 5 */  { NAME_ScreenExtraGrays | OOPTIONAL,      1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch10[ 5 ] = {
  htmatch_separation ,
  htmatch_general ,
  htmatch_transfer ,
  htmatch10_basic ,
  NULL
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type16 Halftone. */
static NAMETYPEMATCH htmatch16_basic[] = {
/* 0 */  { NAME_Width,                             1, { OINTEGER }},
/* 1 */  { NAME_Height,                            1, { OINTEGER }},
/* 2 */  { NAME_Thresholds,                        1, { OFILE }},
/* 3 */  { NAME_Depth | OOPTIONAL,                 1, { OINTEGER }},
/* 4 */  { NAME_Invert | OOPTIONAL,                1, { OBOOLEAN }},
/* 5 */  { NAME_ScreenExtraGrays | OOPTIONAL,      1, { OBOOLEAN }},
/* 6 */  { NAME_Width2  | OOPTIONAL,               1, { OINTEGER }},
/* 7 */  { NAME_Height2 | OOPTIONAL,               1, { OINTEGER }},
/* 8 */  { NAME_LimitScreenLevels | OOPTIONAL,     1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch16[ 5 ] = {
  htmatch_separation ,
  htmatch_general ,
  htmatch_transfer ,
  htmatch16_basic ,
  NULL
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type100 Halftone. */
static NAMETYPEMATCH htmatch100_basic[] = {
/* 0 */  { NAME_HalftoneModule,                    2, { ONAME, OSTRING }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch100[ 5 ] = {
  htmatch_separation ,
  htmatch_general ,
  htmatch_transfer ,
  htmatch100_basic ,
  NULL ,
};

/* ---------------------------------------------------------------------- */
/* Matching dictionaries for Type 195 Halftone. */
static NAMETYPEMATCH htmatch195_basic[] = {
/* 0 */  { NAME_Default,                           1, { ODICTIONARY }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH *htmatch195[ 5 ] = {
  NULL ,
  htmatch_general ,
  NULL ,
  htmatch195_basic ,
  NULL
};

/* ---------------------------------------------------------------------- */
/* Because we now use HalftoneType 100, we no longer employ simple tables
 * alone to associate the type number with its matches list.
 * Instead, we use an inline procedure taking the type as its argument.
 * This maintains a table-like look and feel, the function call resembling
 * a subscripted access, without risking out of bounds subscription.
 */

static inline NAMETYPEMATCH **htmatches( int32 httype )
{
  /* We hide the name scope of this static table to ensure access
   * is only via this function.
   */
  static NAMETYPEMATCH **htmatches_hidden[] = {
    NULL ,
    htmatch1 ,
    htmatch2 ,
    htmatch3,
    htmatch4 ,
    htmatch5 ,
    htmatch6 ,
    NULL ,
    NULL ,
    NULL ,
    htmatch10 ,
    NULL ,
    NULL ,
    NULL ,
    NULL ,
    NULL ,
    htmatch16
  } ;
  NAMETYPEMATCH **thematch = NULL ;

  if ( httype <= 16 )
    thematch = htmatches_hidden[ httype ];
  else if ( 100 == httype )
    thematch = htmatch100;
  else if ( 195 == httype )
    thematch = htmatch195;
  HQASSERT( NULL != thematch, "Illegal halftone type in htmatches()" ) ;
  return thematch ;
}


/* -----------------------------------------------------------------------------
 * The following structures (and the NAMETYPEMATCH tables above) are used to
 * table drive various aspects of sethalftone. For example one can check if
 * a Type1 halftone contains multiple screens or not by looking at the
 * multiple_screens field for type 1, e.g., htinfo( 1 )->multiple_screens.
 * As with htmatches(), above, we now use an inline procedure instead of
 * direct access to the tables, to allow for HalftoneType 100.
 */

static inline const HALFTONE_INFO *htinfo( int32 httype )
{
  /* We hide the name scope of this static table to ensure access
   * is only via this function.
   */
  static HALFTONE_INFO htinfos_hidden[] = {
    /* Type0  */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type1  */ { HALFTONE_GROUP_SPOTFN,     0 /* multiple_screens step */ } ,
    /* Type2  */ { HALFTONE_GROUP_SPOTFN,     3 } ,
    /* Type3  */ { HALFTONE_GROUP_THRESHOLD,  0 } ,
    /* Type4  */ { HALFTONE_GROUP_THRESHOLD,  3 } ,
    /* Type5  */ { HALFTONE_GROUP_UMBRELLA,   0 } ,
    /* Type6  */ { HALFTONE_GROUP_THRESHOLD,  0 } ,
    /* Type7  */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type8  */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type9  */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type10 */ { HALFTONE_GROUP_THRESHOLD,  0 } ,
    /* Type11 */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type12 */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type13 */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type14 */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type15 */ { HALFTONE_GROUP_INVALID,    0 } , /* not used */
    /* Type16 */ { HALFTONE_GROUP_THRESHOLD,  0 }
  } ;
  static HALFTONE_INFO htinfo100 = { HALFTONE_GROUP_MODULAR, 0 } ;
  static HALFTONE_INFO htinfo195 = { HALFTONE_GROUP_UMBRELLA, 0 };
  HALFTONE_INFO *theinfo = NULL;

  if ( httype <= 16 )
    theinfo = &htinfos_hidden[ httype ];
  else if ( 100 == httype )
    theinfo = &htinfo100;
  else if ( 195 == httype )
    theinfo = &htinfo195;
  HQASSERT( NULL != theinfo, "Out of range halftone type in htinfo()" ) ;
  HQASSERT( 0 != theinfo->group, "Invalid haftone type in htinfo()" ) ;
  return theinfo ;
}


/* ---------------------------------------------------------------------- */
static NAMETYPEMATCH half_match[] = {
  /* Use the enum below to index */
  { NAME_HalftoneType, 1, { OINTEGER }},
  { NAME_HalftoneName | OOPTIONAL, 2, { ONAME , OSTRING }},
  { NAME_Encrypt | OOPTIONAL, 1, { ODICTIONARY }},
  DUMMY_END_MATCH
};
enum { half_match_HalftoneType, half_match_HalftoneName, half_match_Encrypt } ;

static NAMETYPEMATCH encr_match[] = {
  /* Use the enum below to index */
  { NAME_EncryptType, 1, { OINTEGER }},
  { NAME_EncryptLength | OOPTIONAL, 1, { OINTEGER }},
  DUMMY_END_MATCH
};
enum { encr_match_EncryptType, encr_match_EncryptLength } ;



/**
 * This routine checks for and extracts the HalftoneType, HalftoneName and
 * Encrypt information from the given dictionary. The resultant values are
 * then placed in the SETHALFTONE_PARAMS structure, where any one of the
 * three 'original', 'override' and 'to-be-used' elements can be set. In
 * addition this routine can optionally validate that the dict is a valid
 * sub-dict of a Type 5.
 */
static Bool dosetht_get_htnameandtype( OBJECT *dict ,
                                       Bool subtype5 ,
                                       SETHALFTONE_PARAMS *htparams ,
                                       int field,
                                       int32 *type )
{
  int32 nHalftoneType ;

  HQASSERT( dict , "dict NULL in gethalftonenameandtype" ) ;
  HQASSERT( htparams , "htparams NULL in gethalftonenameandtype" ) ;

  if ( oType( *dict ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  /* dictmatch: {HalftoneType, HalftoneName, Encrypt} */
  if ( ! dictmatch( dict , half_match ))
    return FALSE ;

  /* HalftoneType */

  HQASSERT( half_match[half_match_HalftoneType].result , "HalftoneType required in halftone dictionary" ) ;
  nHalftoneType = oInteger(*half_match[half_match_HalftoneType].result) ;
  if ( ! (( nHalftoneType > 0 && nHalftoneType <= 6 ) ||
          ( nHalftoneType == 10 ) ||
          ( nHalftoneType == 16 ) ||
          ( nHalftoneType == 100) ||  /* Screening modules */
          ( nHalftoneType == 195 ) ||
          ( nHalftoneType == 199 )))
    return error_handler( RANGECHECK ) ;

  /* Check if we've recursed on a Type5 and the sub-type is illegal. */
  if ( subtype5 &&
       ( 5 == nHalftoneType ||
         (nHalftoneType != 199 && 0 != htinfo( nHalftoneType )->multiple_screens )))
    return error_handler( RANGECHECK ) ;

  if ( field == HT_SEP )
    htparams->nOriginalHalftoneType = nHalftoneType ;
  else if ( field == HT_OVR )
    htparams->nOverrideHalftoneType = nHalftoneType ;
  else {
    HQASSERT( field == HT_USE , "unknown field type" ) ;
    htparams->nUseHalftoneType = nHalftoneType ;
  }
  *type = nHalftoneType;

  /* HalftoneName */
  if ( half_match[half_match_HalftoneName].result != NULL ) {
    OBJECT *htobject = half_match[half_match_HalftoneName].result ;
    NAMECACHE *htname ;
    if ( oType( *htobject ) == ONAME )
      htname = oName( *htobject ) ;
    else {
      HQASSERT( oType( *htobject ) == OSTRING ,
                "This should have been a string" ) ;
      htname = cachename( oString( *htobject ) ,
                          theLen(*htobject)) ;
    }
    if ( field == HT_SEP )
      htparams->nmOriginalHalftoneName = htname ;
    else if ( field == HT_OVR )
      htparams->nmOverrideHalftoneName = htname ;
    else
      htparams->nmUseHalftoneName = htname ;
  }

  /* Encrypt */
  if ( half_match[half_match_Encrypt].result != NULL ) {
    int32 nType, nLength;

    if ( ! dictmatch(half_match[half_match_Encrypt].result, encr_match) )
      return FALSE ;

    nType = oInteger(*encr_match[encr_match_EncryptType].result) ;

    nLength = 0;
    if ( encr_match[encr_match_EncryptLength].result != NULL )
      nLength = oInteger(*encr_match[encr_match_EncryptLength].result) ;

    switch ( nType ) {
    case 0: /* No encryption, EncryptLength should no be set */
      if ( nLength != 0 )
        return error_handler( RANGECHECK ) ;
      break ;
    case 1: /* HQX encryption */
    case 2: /* Harlequin-private-HQX - for password-protected screens */
      if ( nLength <= 0 )
        return error_handler( RANGECHECK ) ;
      break ;
    default: /* Unknown encryption type */
      return error_handler( RANGECHECK ) ;
    }

    htparams->encryptType = nType;
    htparams->encryptLength = nLength;
  }
  return TRUE ;
}


/** If *pdict is a type 199, resolves all redirections and stores the
    name and type in the indicated set of fields in htparams. */
static Bool dosetht_resolve199( OBJECT **pdict, Bool subtype5,
                                SETHALFTONE_PARAMS *htparams, int field )
{
  int32 type;

  do {
    if ( !dosetht_get_htnameandtype( *pdict, subtype5, htparams, field, &type ))
      return FALSE;
    if ( type == 199 )
      if ( !dosethalftone199(*pdict, pdict, htparams) )
        return FALSE;
    /* Type 199s can refer to other type 199s, though there is a depth
       counter in htparams to prevent loops like this going infinite;
       otherwise keep going until we find a useful dictionary */
  } while ( type == 199 );
  return TRUE;
}


/** If *pdict is a type 195, replaces it with the correct subdict. */
static Bool dosetht_resolve195( OBJECT **pdict,
                                SETHALFTONE_PARAMS *htparams, int field )
{
  int32 type;

  if ( !dosetht_get_htnameandtype( *pdict, FALSE, htparams, field, &type ))
    return FALSE;
  if ( type == 195 ) {
    OBJECT *subdict;
    int16 objNameNo;

    switch (htparams->objectType) {
    case REPRO_TYPE_PICTURE:  objNameNo = NAME_Picture; break;
    case REPRO_TYPE_TEXT:     objNameNo = NAME_Text; break;
    case REPRO_TYPE_VIGNETTE: objNameNo = NAME_Vignette; break;
    case REPRO_TYPE_OTHER:    objNameNo = NAME_Linework; break;
    case HTTYPE_DEFAULT: objNameNo = NAME_Default; break;
    default: HQFAIL("Invalid object type"); objNameNo = NAME_Default;
    }
    subdict = fast_extract_hash_name( *pdict, objNameNo );
    if (subdict == NULL)
      subdict = fast_extract_hash_name( *pdict, NAME_Default );
    HQASSERT(subdict != NULL, "Can't find type subdict in override");
    if ( !dosetht_get_htnameandtype( subdict, FALSE, htparams, field, &type ))
      return FALSE;
    *pdict = subdict;
  }
  return TRUE;
}


/**
 * This routine checks for and extracts the override and inherit attributes
 * from a given dictionary and stores the result in the SETHALFTONE_PARAMS
 * structure. The dictmatch must have been done beforehand.
 */
static void dosetht_extract_general( SETHALFTONE_PARAMS *htparams ,
                                     NAMETYPEMATCH *match_general )
{
  HQASSERT( htparams , "htparams NULL in dosetht_extract_general" ) ;
  HQASSERT( match_general , "htmatch[ HT_MATCH_GENERAL ] must always exist" ) ;

  HQASSERT( match_general == htmatch_general , "unexpected match_general" ) ;

  if ( match_general[mg_OverrideFrequency].result != NULL ) {
    HQASSERT( oType( *match_general[mg_OverrideFrequency].result ) == OBOOLEAN,
      "OverrideFrequency should have been a boolean" ) ;
    htparams->overridefreq = oBool( *match_general[mg_OverrideFrequency].result );
  }

  if ( match_general[mg_OverrideAngle].result != NULL ) {
    HQASSERT( oType( *match_general[mg_OverrideAngle].result ) == OBOOLEAN,
      "OverrideAngle should have been a boolean" ) ;
    htparams->overrideangle = oBool( *match_general[mg_OverrideAngle].result );
  }

  if ( match_general[mg_InheritFrequency].result != NULL ) {
    HQASSERT( oType( *match_general[mg_InheritFrequency].result ) == OBOOLEAN,
      "InheritFrequency should have been a boolean" ) ;
    htparams->inheritfreq = oBool( *match_general[mg_InheritFrequency].result );
  }

  if ( match_general[mg_InheritAngle].result != NULL ) {
    HQASSERT( oType( *match_general[mg_InheritAngle].result ) == OBOOLEAN,
      "InheritAngle should have been a boolean" ) ;
    htparams->inheritangle = oBool( *match_general[mg_InheritAngle].result );
  }
}

/**
 * This routine checks for and extracts the DCS key. The dictmatch must have
 * been done beforehand. The presence means that we execute the /DCS entry in
 * internaldict, passing it the color of the halftone being installed.
 */
static Bool dosetht_exec_dcs( NAMETYPEMATCH *match_separation ,
                               OBJECT * poKeyOfType5Subordinate )
{
  HQASSERT( match_separation , "htmatch[ HT_MATCH_SEPARATION ] must always exist" ) ;
  HQASSERT( match_separation == htmatch_separation , "unexpected match_separation" ) ;

  if ( match_separation[ 2 ].result /* DCS */ != NULL ) {
    OBJECT *dcs ;
    HQASSERT( oType( *match_separation[ 2 ].result ) == ODICTIONARY ,
      "This should have been a dictionary" ) ;
    /* Execute the procedure /DCS in internaldict. */
    if (( dcs = fast_extract_hash_name(&internaldict , NAME_DCS)) != NULL ) {
      if ( poKeyOfType5Subordinate == NULL ) {
        oName( nnewobj ) = system_names + NAME_Default ;
        poKeyOfType5Subordinate = & nnewobj ;
      }
      HQASSERT( oType( *poKeyOfType5Subordinate ) == ONAME ,
                "Key in type 5 dictionary should have been a name" ) ;
      if ( ! push2( poKeyOfType5Subordinate ,
                    match_separation[ 2 ].result , & operandstack ) ||
           ! push(dcs, &executionstack) )
        return FALSE ;
      if ( !interpreter(1, NULL) )
        return FALSE;
    }
  }
  return TRUE ;
}

/** Destroys the current cache of halftone transfers and initialises a
 * new one before it's properly set up using dosetht_store_caldata. */
static Bool initTransferCache(GS_HALFTONEinfo *halftoneInfo,
                              HTTYPE objtype, size_t nColorants)
{
  size_t i;

  /* Destroy the existing htxfer cache and initialise a new cache */
  if ( halftoneInfo->htTransfer[objtype] != NULL )
    mm_sac_free(mm_pool_color,
                halftoneInfo->htTransfer[objtype],
                halftoneInfo->nColorants[objtype] * sizeof(HTXFER));
  halftoneInfo->htTransfer[objtype] = NULL;

  halftoneInfo->nColorants[objtype] = (uint32)nColorants;
  if ( nColorants > 0 ) {
    mm_size_t htxfersize = nColorants * sizeof( HTXFER );
    halftoneInfo->htTransfer[objtype] =
      (HTXFER *) mm_sac_alloc(mm_pool_color, htxfersize,
                              MM_ALLOC_CLASS_NCOLOR);
    if ( halftoneInfo->htTransfer[objtype] == NULL )
      return error_handler( VMERROR ) ;
  }

  theTags( halftoneInfo->htDefaultTransfer[objtype] ) = ONULL;
  for ( i = 0 ; i < nColorants ; ++i ) {
    object_store_null(object_slot_notvm(&halftoneInfo->htTransfer[objtype][i].transferFn)) ;
    halftoneInfo->htTransfer[objtype][i].transferFnColorant = NULL;
  }
  return TRUE;
}


static Bool clearTransferCache(GS_HALFTONEinfo *halftoneInfo)
{
  HTTYPE objtype;

  for ( objtype = 0 ; objtype <= REPRO_N_TYPES ; objtype++ )
    if ( !initTransferCache(halftoneInfo, objtype, 0) )
      return FALSE;
  return TRUE;
}

/**
 * This routine stores the calibration data for CMYK into the gstate that may
 * have been put into the halftone dictionary.
 */
static Bool dosetht_store_caldata( NAMETYPEMATCH **htmatch ,
                                    GS_COLORinfo *colorInfo ,
                                    SETHALFTONE_PARAMS *htparams,
                                    Bool subtype5 )
{
  size_t nTransfer;
  HTTYPE objtype = htparams->objectType;
  GS_HALFTONEinfo   *halftoneInfo = colorInfo->halftoneInfo;

  HQASSERT( htparams , "htparams NULL in dosetht_store_caldata" ) ;

  /* multiple screen halftone types don't support transfer-function */
  if ( 0 == htinfo( htparams->nOriginalHalftoneType )->multiple_screens ) {
    Bool isDefault =
      htparams->nmOriginalColorKey == system_names + NAME_Default;

    /* Cache the transfer function.
     * NB. If the TransferFunction is not present in the dictionary, this is
     * indicated by setting the cache entry to ONULL. [It is invalid for
     * a job to set a null value.]
     */
    NAMETYPEMATCH *match_transfer = htmatch[ HT_MATCH_TRANSFER ] ;
    HQASSERT( match_transfer , "should have got halftone transfer" ) ;
    HQASSERT( match_transfer == htmatch_transfer , "unexpected match_transfer" ) ;

    nTransfer = htparams->nSubDictIndex;
    HQASSERT( (subtype5
               && (nTransfer < halftoneInfo->nColorants[objtype] || isDefault))
              || (!subtype5 && nTransfer == 0),
              "too many TransferFunctions found");

    if ( match_transfer[ 0 ].result != NULL ) {

      HQASSERT( oType( *match_transfer[ 0 ].result ) == OARRAY ||
                oType( *match_transfer[ 0 ].result ) == OPACKEDARRAY ||
                oType( *match_transfer[ 0 ].result ) == OFILE ,
                "TransferFunction in halftone dictionary should have been a (packed) array" ) ;

      /* Validate the calibration array form of the transfer function here
       */
      if ( !oExecutable( *match_transfer[ 0 ].result ))
        if (!gsc_validateCalibrationArray(match_transfer[ 0 ].result))
          return FALSE;

      if (subtype5 && !isDefault)
        Copy( &halftoneInfo->htTransfer[objtype][nTransfer].transferFn,
              match_transfer[ 0 ].result );
      else
        Copy( &halftoneInfo->htDefaultTransfer[objtype],
              match_transfer[ 0 ].result );
    } else
      HQASSERT(oType((subtype5 && !isDefault)
                     ? halftoneInfo->htTransfer[objtype][nTransfer].transferFn
                     : halftoneInfo->htDefaultTransfer[objtype])
               == ONULL,
               "Unspecified halftone transfer should be ONULL");

    if (subtype5 && !isDefault)
      halftoneInfo->htTransfer[objtype][nTransfer].transferFnColorant  =
            htparams->nmOriginalColorKey;
  }

  return TRUE ;
}


/* ---------------------------------------------------------------------- */
enum { SH_TYPE_SPOTFN = 1, SH_TYPE_THRESH = 2, SH_TYPE_MODULAR = 3 } ;

typedef struct sh_dotshapeinfo {
  int32 sh_type ;
  union {
    struct {
      int32 width ;
      int32 height ;
      int32 depth ;
      int32 width2 ;
      int32 height2 ;
      Bool invert;
      OBJECT *thresh ;
    } sh_thresh ;
    struct {
      SYSTEMVALUE freq ;
      SYSTEMVALUE angl ;
      OBJECT *spotfn ;
    } sh_spotfn ;
    struct {
      OBJECT *poHTModule ;
    } sh_modular ;
  } sh_bits ;
} SH_DOTSHAPE ;


/**
 * This routine checks for any data that needs to be stored back into the
 * halftone dictionary. For now this is two things; firstly where the
 * halftone dictionary has requested the ActualFrequency & ActualAngle
 * values to be filled in and secondly where we need to fill in the
 * Frequency & Angle keys that may have been overridden by the arguments
 * to set[color]screen. Note that this is done for Type 2 halftones as
 * well as Type 1.
 */
static Bool dosetht_store_data( OBJECT *dict,
                                NAMETYPEMATCH *match1 ,
                                NAMETYPEMATCH *match1_extra ,
                                SETHALFTONE_PARAMS *htparams ,
                                SYSTEMVALUE *freqv , SYSTEMVALUE *anglv ,
                                SH_DOTSHAPE *sh_dotshape ,
                                COLOR_PAGE_PARAMS *colorPageParams )
{
  HQASSERT( match1 , "match1 NULL in dosetht_store_data" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht_store_data" ) ;
  HQASSERT( sh_dotshape , "sh_dotshape NULL in dosetht_store_data" ) ;

  /* For case of implicit_screens. */
  if ( dict == NULL )
    return TRUE ;

  if ( HALFTONE_GROUP_SPOTFN != htinfo( htparams->nOriginalHalftoneType )->group )
    return TRUE ;

  HQASSERT( sh_dotshape->sh_type == SH_TYPE_SPOTFN , "can't store freq/angl otherwise" ) ;
  HQASSERT( htparams->nOriginalHalftoneType == 1 ||
            htparams->nOriginalHalftoneType == 2 ,
            "don't know how to store back non-spot fn screen data" ) ;

  if ( match1_extra ) {
    HQASSERT( match1_extra == htmatch1_extra , "unexpected match1_extra" ) ;

    /* Frequency */
    if ( match1_extra[ 0 ].result != NULL ) {
      oName( nnewobj ) = theIMName( & match1_extra[ 0 ] ) ;
      oReal( rnewobj ) =
        ( USERVALUE )sh_dotshape->sh_bits.sh_spotfn.freq ;
      if ( ! fast_insert_hash( dict , & nnewobj , & rnewobj ))
        return FALSE ;
    }

    /* Angle */
    if ( match1_extra[ 1 ].result != NULL ) {
      oName( nnewobj ) = theIMName( & match1_extra[ 1 ] ) ;
      oReal( rnewobj ) =
        ( USERVALUE )sh_dotshape->sh_bits.sh_spotfn.angl ;
      if ( ! fast_insert_hash( dict , & nnewobj , & rnewobj ))
        return FALSE ;
    }
  }

  if ( colorPageParams->useAllSetScreen ) {
    if ( freqv ) {
      oName( nnewobj ) = theIMName( & match1[ 0 ] ) ;
      oReal( rnewobj ) =
        ( USERVALUE )sh_dotshape->sh_bits.sh_spotfn.freq ;
      if ( ! fast_insert_hash( dict , & nnewobj , & rnewobj ))
        return FALSE ;
    }
    if ( anglv ) {
      oName( nnewobj ) = theIMName( & match1[ 1 ] ) ;
      oReal( rnewobj ) =
        ( USERVALUE )sh_dotshape->sh_bits.sh_spotfn.angl ;
      if ( ! fast_insert_hash( dict , & nnewobj , & rnewobj ))
        return FALSE ;
    }
  }
  return TRUE ;
}


/**
 * This routine determines and extracts the override screen that is
 * going to be used.
 *
 * Having done that it then updates the "use..." fields in the SETHALFTONE_PARAMS
 * structure. The way the override screen is then used is that the match_basic
 * pointer is updated to point to that of the override halftone as opposed to
 * that of the original halftone.
 * The routine looks more complicated than it is due to how it extracts
 * complementary colors from a Type5 halftone dictionary (or then the
 * Default entry) if the screen being overridden does not exist.
 */
static Bool dosetht_get_overridescreen( NAMETYPEMATCH **htmatch ,
                                         NAMETYPEMATCH **match_basic ,
                                         OBJECT *poOverrideHalftoneDict ,
                                         GS_COLORinfo *colorInfo ,
                                         SETHALFTONE_PARAMS *htparams )
{
  NAMECACHE * nmColorKey;

  HQASSERT( htmatch , "htmatch NULL in dosetht_get_overridescreen" ) ;
  HQASSERT( match_basic , "match_basic NULL in dosetht_get_overridescreen" ) ;
  HQASSERT( poOverrideHalftoneDict , "poOverrideHalftoneDict NULL in dosetht_get_overridescreen" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in dosetht_get_overridescreen" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht_get_overridescreen" ) ;

  /* Get the overrides et al from the override dict. */
  {
    NAMETYPEMATCH *match_general = htmatch[ HT_MATCH_GENERAL ] ;

    HQASSERT( match_general , "htmatch[ HT_MATCH_GENERAL ] must always exist" ) ;
    HQASSERT( match_general == htmatch_general , "unexpected match_general" ) ;

    if ( ! dictmatch( poOverrideHalftoneDict , match_general ))
      return FALSE ;

    /* Reset overrides to defaults. */
    HT_RESET_INHERITOVERRIDES( htparams ) ;

    dosetht_extract_general( htparams, match_general );
  }

  if ( htparams->nOverrideHalftoneType == 5 ) {

    int32 nColorants ;
    DEVICESPACEID deviceSpace ;
    NAMECACHE *nmSepColor = get_separation_name(FALSE) ;
    OBJECT *poOverrideHalftoneType5SubordinateDict = NULL ;

    HQASSERT( htparams->nmOriginalColorKey != NULL , "should have set up halftone color" ) ;

    nmColorKey = htparams->nmOriginalColorKey ;
    guc_deviceColorSpace( colorInfo->deviceRS , & deviceSpace , & nColorants ) ;
    if ( interceptSeparation( nmColorKey, nmSepColor, deviceSpace ))
      nmColorKey = nmSepColor;

    /* is the same color we are looking at from the original dictionary also in the
       override dictionary? */
    oName( nnewobj ) = nmColorKey ;
    poOverrideHalftoneType5SubordinateDict = extract_hash( poOverrideHalftoneDict , & nnewobj ) ;

    /* if not found, try for a complementary color name */
    if ( poOverrideHalftoneType5SubordinateDict == NULL ) {
      switch (nmColorKey - system_names) {
      case NAME_Cyan:    nmColorKey = system_names + NAME_Red;     break;
      case NAME_Magenta: nmColorKey = system_names + NAME_Green;   break;
      case NAME_Yellow:  nmColorKey = system_names + NAME_Blue;    break;
      case NAME_Black:   nmColorKey = system_names + NAME_Gray;    break;
      case NAME_Red:     nmColorKey = system_names + NAME_Cyan;    break;
      case NAME_Green:   nmColorKey = system_names + NAME_Magenta; break;
      case NAME_Blue:    nmColorKey = system_names + NAME_Yellow;  break;
      case NAME_Gray:    nmColorKey = system_names + NAME_Black;   break;
      default:           nmColorKey = NULL;                        break;
      }

      if (nmColorKey != NULL) {
        oName( nnewobj ) = nmColorKey;
        poOverrideHalftoneType5SubordinateDict =
          extract_hash( poOverrideHalftoneDict , & nnewobj ) ;
      }
    }

    /* If that was not found, then use the Default entry. */
    if ( poOverrideHalftoneType5SubordinateDict == NULL ) {
      nmColorKey = oName( nnewobj ) = system_names + NAME_Default ;

      poOverrideHalftoneType5SubordinateDict =
        extract_hash( poOverrideHalftoneDict , & nnewobj ) ;
      if ( poOverrideHalftoneType5SubordinateDict == NULL )
        return error_handler( RANGECHECK ) ;
    }

    /* Given the override sub-dictionary, extract its screen data
       (threshold or spotfn) */

    /* Use the HalftoneName entry from the Type5 dictionary if the
     * sub-dictionary does not contain one. */
    htparams->nmUseHalftoneName = htparams->nmOverrideHalftoneName ;

    if ( !dosetht_resolve199( &poOverrideHalftoneType5SubordinateDict, TRUE,
                              htparams, HT_USE ))
      return FALSE;

    htparams->nmUseColorKey = nmColorKey ;

    /* Update match_basic to point to the override screen. */
    (*match_basic) = htmatches( htparams->nUseHalftoneType )[ HT_MATCH_BASIC ] ;

    if ( ! dictmatch( poOverrideHalftoneType5SubordinateDict , (*match_basic) ))
      return FALSE ;

    /* Re-get the override{freq,angle} params for override (since that's what we're using). */
    {
      NAMETYPEMATCH *match_general = htmatch[ HT_MATCH_GENERAL ] ;

      if ( ! dictmatch( poOverrideHalftoneType5SubordinateDict , match_general ))
        return FALSE ;
      dosetht_extract_general( htparams, match_general );
    }

    /* Remember the dict the override is using, esp. for modular screens. */
    htparams->htdict_use = poOverrideHalftoneType5SubordinateDict;

  } else {
    /* Overriding halftone dictionary is not a type 5, so the only choice about which
       screen overrides is when we have a type 2 or 4 or a setcolorscreen
       (multiple_screens) */
    DEVICESPACEID deviceSpace;
    int32 nScreen, nColorants; /* unused after the call to guc_deviceColorSpace */

    htparams->nUseHalftoneType = htparams->nOverrideHalftoneType ;
    htparams->nmUseHalftoneName = htparams->nmOverrideHalftoneName ;

    htparams->nmUseColorKey = system_names + NAME_Black ;
    guc_deviceColorSpace (colorInfo->deviceRS, & deviceSpace, & nColorants);
    if (deviceSpace == DEVICESPACE_Gray)
      htparams->nmUseColorKey = system_names + NAME_Gray ;

    nmColorKey = htparams->nmOriginalColorKey ;
    HQASSERT( nmColorKey , "This should never be NULL" ) ;
    nScreen = 3;

    if ( 0 != htinfo( htparams->nUseHalftoneType )->multiple_screens ) {
      NAMECACHE *nmSepColor = get_separation_name(FALSE) ;

      if ( interceptSeparation( nmColorKey, nmSepColor, deviceSpace ))
        nmColorKey = nmSepColor;

      switch (nmColorKey - system_names) {
      case NAME_Cyan:
      case NAME_Red:
        nScreen = 0;
        break;
      case NAME_Magenta:
      case NAME_Green:
        nScreen = 1;
        break;
      case NAME_Yellow:
      case NAME_Blue:
        nScreen = 2;
        break;
      case NAME_Black:
      case NAME_Gray:
        nScreen = 3;
        break;
      default:
        /* Go back to either Black or Gray as we had before. */
        nmColorKey = htparams->nmUseColorKey ;
        break;
      }
      htparams->nmUseColorKey = nmColorKey;
    }

    /* Update match_basic to point to the override screen. */
    (*match_basic) = htmatches( htparams->nUseHalftoneType )[ HT_MATCH_BASIC ] +
        nScreen * htinfo( htparams->nUseHalftoneType )->multiple_screens;

    if ( ! dictmatch( poOverrideHalftoneDict , (*match_basic) ))
      return FALSE ;

    /* Remember the dict the override is using, esp. for modular screens. */
    htparams->htdict_use = poOverrideHalftoneDict;
  }
  return TRUE ;
}

/**
 * This routine extracts and checks the screening parameters. Obviously we
 * only know about threshold screens or spot functions. The results are stored
 * in the SH_DOTSHAPE structure.
 * For a Spot Function screen (Type 1 or 2) we also have to consider if we
 * are inheriting the frequency or angle from the set[color]screen call.
 */
static Bool dosetht_check_screen( NAMETYPEMATCH *match_basic ,
                                  SETHALFTONE_PARAMS *htparams ,
                                  SYSTEMVALUE *freqv , SYSTEMVALUE *anglv ,
                                  SH_DOTSHAPE *sh_dotshape ,
                                  COLOR_PAGE_PARAMS *colorPageParams )
{
  HQASSERT( match_basic , "match_basic NULL in dosetht_check_screen" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht_check_screen" ) ;
  HQASSERT( sh_dotshape , "sh_dotshape NULL in dosetht_check_screen" ) ;

  HQASSERT( htparams->nUseHalftoneType != 5 , "Type 5 halftone encountered in dosetht_check_screen" ) ;

  if ( HALFTONE_GROUP_MODULAR == htinfo( htparams->nUseHalftoneType )->group )
  { /* A screening-module provided screen */
    HQASSERT( htparams->nUseHalftoneType == 100,
              "unknown halftone type for modular screen" ) ;
    HQASSERT( match_basic == htmatch100_basic,
              "unexpected match_basic for modular screen" ) ;
    HQASSERT( oType( *match_basic[ 0 ].result ) == ONAME ||
              oType( *match_basic[ 0 ].result ) == OSTRING ,
      "HalftoneModule in a type modular halftone dictionary should have been a name or a string" ) ;

    sh_dotshape->sh_bits.sh_modular.poHTModule = match_basic[ 0 ].result ;
    if ( ! htm_IsModuleNameRegistered( sh_dotshape->sh_bits.sh_modular.poHTModule ) )
      return error_handler( RANGECHECK );

    sh_dotshape->sh_type = SH_TYPE_MODULAR ;
  }
  else if ( HALFTONE_GROUP_THRESHOLD == htinfo( htparams->nUseHalftoneType )->group ) {
    /* Parameters for a single threshold. */
    int32 width, height, depth, depth_factor;
    OBJECT *thresh ;
    Bool invert;

    HQASSERT( htparams->nUseHalftoneType == 3 ||
              htparams->nUseHalftoneType == 4 ||
              htparams->nUseHalftoneType == 6 ||
              htparams->nUseHalftoneType == 10 ||
              htparams->nUseHalftoneType == 16,
              "unknown halftone type for threshold screen" ) ;
    HQASSERT( match_basic == htmatch3_basic ||
              match_basic == htmatch4_basic + 0 * htinfo( 4 )->multiple_screens ||
              match_basic == htmatch4_basic + 1 * htinfo( 4 )->multiple_screens ||
              match_basic == htmatch4_basic + 2 * htinfo( 4 )->multiple_screens ||
              match_basic == htmatch4_basic + 3 * htinfo( 4 )->multiple_screens ||
              match_basic == htmatch6_basic ||
              match_basic == htmatch10_basic ||
              match_basic == htmatch16_basic ,
              "unexpected match_basic" ) ;
    HQASSERT( oType( *match_basic[ mb_threshold_Width ].result ) == OINTEGER ,
      "Width in threshold halftone dictionary should have been an integer" ) ;
    HQASSERT( oType( *match_basic[ mb_threshold_Height ].result ) == OINTEGER ,
      "Height in threshold halftone dictionary should have been an integer" ) ;
    HQASSERT( oType( *match_basic[ mb_threshold_Thresholds ].result ) == OSTRING ||
              oType( *match_basic[ mb_threshold_Thresholds ].result ) == OFILE ,
      "Thresholds in a threshold halftone dictionary should have been a string or file" ) ;

    width  = oInteger( *match_basic[ mb_threshold_Width ].result );
    height = oInteger( *match_basic[ mb_threshold_Height ].result );
    if ( htparams->nUseHalftoneType != 4 /* type 4 doesn't support /Depth */
         && match_basic[ mb_threshold_Depth ].result != NULL )
      depth = oInteger( *match_basic[ mb_threshold_Depth ].result );
    else
      depth = 1;
    depth_factor = (1 << depth) - 1;
    if ( htparams->nUseHalftoneType != 4 /* type 4 doesn't support /Invert */
         && match_basic[ mb_threshold_Invert ].result != NULL )
      invert = oBool( *match_basic[ mb_threshold_Invert ].result );
    else
      invert = FALSE;
    thresh = match_basic[ mb_threshold_Thresholds ].result;
    if (( width  <= 0 ) ||
        ( height <= 0 ) ||
        (( oType( *thresh ) == OSTRING ) &&
         (( int32 )theLen(*thresh) > 0 ) &&
         ( htparams->nUseHalftoneType == 10 ?
           /* width is really Xsquare and height is Ysquare */
           (width * width + height * height) * depth_factor :
           width * height * depth_factor )
         != (int32)theLen(*thresh) ))
      return error_handler( RANGECHECK ) ;

    if ( htparams->nUseHalftoneType == 16 ) {
      OBJECT *width2_match = match_basic[ mb_threshold_Limit ].result;
      OBJECT *height2_match = match_basic[ mb_threshold_Limit+1 ].result;
      int32 width2  = 0;
      int32 height2 = 0;

      /* Must have both width2 and height2 defined, or neither of them */
      if ( width2_match ) {
        if ( height2_match ) {
          HQASSERT(oType(*width2_match) == OINTEGER,
                   "width2 should be an integer");
          HQASSERT(oType(*height2_match) == OINTEGER,
                   "height2 should be an integer");

          width2 = oInteger(*width2_match); height2 = oInteger(*height2_match);
          if ( (height2 <= 0) || (width2 <= 0) )
            return error_handler(RANGECHECK);

        } else
          return error_handler(UNDEFINED);  /* Have Width2 but not Height2 */
      } else {
        if ( height2_match )
          return error_handler(UNDEFINED); /* Have Height2 but not Width2 */
      }

      sh_dotshape->sh_bits.sh_thresh.width2  = width2;
      sh_dotshape->sh_bits.sh_thresh.height2 = height2;
    }

    sh_dotshape->sh_type = SH_TYPE_THRESH ;
    sh_dotshape->sh_bits.sh_thresh.width = width ;
    sh_dotshape->sh_bits.sh_thresh.height = height ;
    sh_dotshape->sh_bits.sh_thresh.depth = depth ;
    sh_dotshape->sh_bits.sh_thresh.invert = invert ;
    sh_dotshape->sh_bits.sh_thresh.thresh = thresh ;

  } else {

    /* Parameters for a single spot function. */
    int32 waste ;
    SYSTEMVALUE freq ;
    SYSTEMVALUE angl ;
    OBJECT *spotfn ;
    HQASSERT( HALFTONE_GROUP_SPOTFN == htinfo( htparams->nUseHalftoneType )->group,
              "unexpected halftone group - we only expect spot-function screens here") ;
    HQASSERT( htparams->nUseHalftoneType == 1 ||
              htparams->nUseHalftoneType == 2 ,
              "unknown halftone type for spot-function halftone dictionary" ) ;
    HQASSERT( match_basic == htmatch1_basic ||
              match_basic == htmatch2_basic + 0 * htinfo( 2 )->multiple_screens ||
              match_basic == htmatch2_basic + 1 * htinfo( 2 )->multiple_screens ||
              match_basic == htmatch2_basic + 2 * htinfo( 2 )->multiple_screens ||
              match_basic == htmatch2_basic + 3 * htinfo( 2 )->multiple_screens ,
              "unexpected match_basic" ) ;
    spotfn = match_basic[ 2 ].result ; /* SpotFunction */
    HQASSERT( oType( *match_basic[ 0 ].result ) == OINTEGER ||
              oType( *match_basic[ 0 ].result ) == OREAL ,
      "Frequency should have been an integer or real in halftone dictionary" ) ;
    HQASSERT( oType( *match_basic[ 1 ].result ) == OINTEGER ||
              oType( *match_basic[ 1 ].result ) == OREAL ,
      "Angle should have been an integer or real in halftone dictionary" ) ;

    /* For spot functions doesn't simply use the given dictionary keys: we need to
     * consider the case where we had a set[color]screen and want to inherit the
     * frequency or angle from there.
     */

    if ( ( htparams->inheritfreq ||
           htparams->inheritangle ||
           colorPageParams->useAllSetScreen ) &&
         ( freqv || anglv ))
    {
      int32 fa_index ;
      OBJECT freqo = OBJECT_NOTVM_NOTHING , anglo = OBJECT_NOTVM_NOTHING ;

      /* Inherit frequency and angle from set[color]screen. */
      switch (htparams->nmOriginalColorKey - system_names) {
      case NAME_Cyan:
      case NAME_Red:
        fa_index = 0;
        break;
      case NAME_Magenta:
      case NAME_Green:
        fa_index = 1;
        break;
      case NAME_Yellow:
      case NAME_Blue:
        fa_index = 2;
        break;
      case NAME_Black:
      case NAME_Gray:
        fa_index = 3;
        break;
      default:
        fa_index = 3;
        break;
      }

      if ( htparams->inheritfreq && freqv ) {
        oReal( rnewobj ) = ( USERVALUE )freqv[ fa_index ] ;
        Copy( & freqo  , & rnewobj ) ;
      } else {
        Copy( & freqo  , match_basic[ 0 ].result ) ;
      }

      if ( htparams->inheritangle && anglv ) {
        oReal( rnewobj ) = ( USERVALUE )anglv[ fa_index ] ;
        Copy( & anglo , & rnewobj ) ;
      } else {
        Copy( & anglo  , match_basic[ 1 ].result ) ;
      }

      if ( ! checksinglescreen(& freqo , & freq ,
                               & anglo , & angl ,
                               spotfn , FALSE , & waste ))
        return FALSE ;

    } else {

      /* Not overriding from set[color]screen */
      if ( ! checksinglescreen ( match_basic[ 0 ].result /* Frequency */, & freq ,
                                 match_basic[ 1 ].result /* Angle */,     & angl ,
                                 spotfn , FALSE , & waste ))
        return FALSE ;

    }

    sh_dotshape->sh_type = SH_TYPE_SPOTFN ;
    sh_dotshape->sh_bits.sh_spotfn.freq = freq ;
    sh_dotshape->sh_bits.sh_spotfn.angl = angl ;
    sh_dotshape->sh_bits.sh_spotfn.spotfn = spotfn ;
  }

  return TRUE ;
}

/**
 * This routine adds any separations as required by the halftone dictionary
 * onto the current list for this page. This routines is only ever called
 * with the original halftone and never with the override. It also never
 * does this with a multiple screen, although there is no real reason why.
 */
static Bool dosetht_add_separations( DL_STATE *page ,
                                     NAMETYPEMATCH **htmatch ,
                                     int32 * pfIgnoreColorant ,
                                     OBJECT * poKeyOfType5Subordinate ,
                                     GS_COLORinfo *colorInfo ,
                                     SETHALFTONE_PARAMS *htparams )
{
  COLORANTINDEX ci, ciNew;
  NAMECACHE * pnmKeyOfType5Subordinate;
  int32 nInstance;

  HQASSERT( htmatch , "htmatch NULL in dosetht_add_separations" ) ;
  HQASSERT( pfIgnoreColorant , "pfIgnoreColorant NULL in dosetht_add_separations" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in dosetht_add_separations" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht_add_separations" ) ;
  HQASSERT( htparams->nOriginalHalftoneType != 5 , "Type 5 halftones should never get here" ) ;

  * pfIgnoreColorant = TRUE ; /* Unless we decide otherwise --> get to end of routine. */

  if (htparams->fSeparationsKeySetInType5 && poKeyOfType5Subordinate != NULL) {

    HQASSERT (oType(*poKeyOfType5Subordinate) == ONAME,
              "poKeyOfType5Subordinate is not a name in dosetht_add_separations");

    pnmKeyOfType5Subordinate = oName (*poKeyOfType5Subordinate);
    ci = htparams->colorantIndex ;

    if (pnmKeyOfType5Subordinate != system_names + NAME_Default &&
        ci != COLORANTINDEX_UNKNOWN && ci != COLORANTINDEX_NONE &&
        ci != COLORANTINDEX_ALL) {
      NAMETYPEMATCH *match_separation = htmatch[HT_MATCH_SEPARATION];
      OBJECT * poBackground = match_separation[1].result;
      Bool fBackground;
      int32 specialHandling;
      USERVALUE neutralDensity;
      OBJECT * poPositions = match_separation[0].result;
      Bool fOverrideScreenAngle;
      USERVALUE screenAngle;

      HQASSERT( match_separation != NULL, "should have got separation control" ) ;
      HQASSERT( match_separation == htmatch_separation , "unexpected match_separation" ) ;
      HQASSERT (poPositions == NULL ||
                oType(*poPositions) == OARRAY ||
                oType(*poPositions) == OPACKEDARRAY ,
                "Positions should have been an array in halftone dictionary");

      fBackground = FALSE;
      if (poBackground != NULL)
        fBackground =  oBool (*poBackground);

      /* We removed the separation earlier on (though retained the numbers for them
         in reserve), so now is the time to reinstate it. */

      if (!guc_colorantIndexPossiblyNewSeparation (colorInfo->deviceRS,
                                                   pnmKeyOfType5Subordinate, &ciNew))
        return FALSE;
      HQASSERT (ciNew == ci,
        "mismatched colorant indexes (after guc_colorantIndexPossiblyNewSeparation) \
         in dosetht_add_separations");

      specialHandling = guc_colorantSpecialHandling (colorInfo->deviceRS,
                                                     pnmKeyOfType5Subordinate) ;
      neutralDensity = guc_colorantNeutralDensity (colorInfo->deviceRS,
                                                   pnmKeyOfType5Subordinate) ;
      if (! guc_colorantScreenAngle (colorInfo->deviceRS,
                                     pnmKeyOfType5Subordinate,
                                     & screenAngle,
                                     & fOverrideScreenAngle))
        return FALSE;

      /* If Positions is given, we use it, otherwise we add one instance in a new
         frame at the end. Note that we start with no colorants */
      if (poPositions == NULL) {

        /* If we don't say where to put the separation, it will come out in
           dictionary hashing order, and this is distinctly unhelpful. Instead order
           them by colorant index, so at least you get CMYK for example in that
           order. In some cases, this may lead to sheets with no channels attached,
           but that is OK, they'll just get skipped on rendering etc. */

        if (! guc_newFrame(colorInfo->deviceRS,
                           pnmKeyOfType5Subordinate,
                           0, 0, fBackground, FALSE,
                           RENDERING_PROPERTY_RENDER_ALL,
                           GUC_FRAMERELATION_AT, (int32) ci,
                           specialHandling,
                           neutralDensity,
                           screenAngle, fOverrideScreenAngle,
                           DOING_RUNLENGTH(page),
                           & ciNew))
          return FALSE;
        HQASSERT (ciNew == ci,
          "mismatched indexes (after guc_newFrame, no Positions) in dosetht_add_separations");
      } else {
        for (nInstance = 0; nInstance <theLen(*poPositions); nInstance++) {
          OBJECT * poThisPosition = & oArray (*poPositions) [nInstance];
          int32 nFrame;
          SYSTEMVALUE x, y;

          if (oType (*poThisPosition) != OARRAY &&
              oType (*poThisPosition) != OPACKEDARRAY)
            return error_handler (TYPECHECK);

          if (theLen(*poThisPosition) < 3)
            return error_handler (RANGECHECK);

          if (oType (oArray (*poThisPosition) [0]) == OINTEGER)
            x = oInteger (oArray(*poThisPosition) [0]);
          else if (oType (oArray (*poThisPosition) [0]) == OREAL)
            x = oReal (oArray (*poThisPosition) [0]);
          else
            return error_handler (TYPECHECK);

          if (oType (oArray (*poThisPosition) [1]) == OINTEGER)
            y = oInteger (oArray (*poThisPosition) [1]);
          else if (oType (oArray (*poThisPosition) [1]) == OREAL)
            y = oReal (oArray (*poThisPosition) [1]);
          else
            return error_handler (TYPECHECK);

          /* convert separation offsets to device space */
          x = x * page->xdpi / 72.0 + 0.5;
          if (x < 0.0) x -= 1.0;
          if (doing_mirrorprint) x = -x;

          y = y * page->ydpi / 72.0 + 0.5;
          if (y < 0.0) y -= 1.0;

          if (oType (oArray (*poThisPosition) [2]) == OINTEGER) {
            /* note this assumes numbering is zero based, which contradicts the
               extensions manual. Previously, numbering was relative, so unless the
               behaviour was to augment existing lists of separations, it wouldn't
               actually have mattered; but if you did augment, it would have had to
               have started at zero. This is, in any case, the safer choice */
            nFrame = oInteger (oArray (*poThisPosition) [2]);
          } else {
            return error_handler (TYPECHECK);
          }

          if (! guc_newFrame(colorInfo->deviceRS,
                             pnmKeyOfType5Subordinate,
                             (int32) x, (int32) y, fBackground, FALSE,
                             RENDERING_PROPERTY_RENDER_ALL,
                             GUC_FRAMERELATION_AT, nFrame,
                             specialHandling,
                             neutralDensity,
                             screenAngle, fOverrideScreenAngle,
                             DOING_RUNLENGTH(page),
                             & ciNew))
            return FALSE;
          HQASSERT (ciNew == ci,
            "mismatched indexes after guc_newFrame (with Positions) in dosetht_add_separations");
        }
      }
    }
  }

  /* Say we want to actually use this screen. */
  * pfIgnoreColorant = FALSE ;
  return TRUE ;
}

/**
 * This routine performs color detection on the given information. We only do it
 * when we get a setscreen. That's because we set default halftone information
 * that really tells us nothing. It's only when a job goes "freq ang proc setscreen"
 * or "currentscreen exch pop 15 exch setscreen" that we know something about the job.
 * We also now do it for Type 1 halftone dictionaries.
 */
static Bool dosetht_color_detection( GS_COLORinfo *colorInfo ,
                                      SETHALFTONE_PARAMS *htparams ,
                                      Bool subOfType5,
                                      SYSTEMVALUE *freqv , SYSTEMVALUE *anglv ,
                                      SH_DOTSHAPE *sh_dotshape )
{
  SYSTEMVALUE freq ;
  SYSTEMVALUE angl ;

  HQASSERT( colorInfo , "colorInfo NULL in dosetht_color_detection" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht_color_detection" ) ;

  if ( freqv && anglv ) {
    freq = freqv[ 3 ] ;
    angl = anglv[ 3 ] ;
  }
  else {
    if ( sh_dotshape &&
         sh_dotshape->sh_type == SH_TYPE_SPOTFN &&
         !subOfType5 &&
         htparams->nOriginalHalftoneType == 1 ) {
      freq = sh_dotshape->sh_bits.sh_spotfn.freq ;
      angl = sh_dotshape->sh_bits.sh_spotfn.angl ;
    }
    else
      return TRUE ;
  }

  HQASSERT( freq > 0 , "frequency should be > 0.0" ) ;

  /* Note that this probes any redefinition of setcmykcolor as well as
     examining the angle. */
  if ( htparams->nmOriginalColorKey == system_names + NAME_Black ||
       htparams->nmOriginalColorKey == system_names + NAME_Gray )
  {
    if ( ! detect_setscreen_separation( freq , angl, colorInfo ))
      return FALSE ;
  }

  return TRUE ;
}


/**
 * This routine executes the screening parameters previously obtained.
 */
static Bool dosetht_exec_screens( corecontext_t *context,
                                  NAMETYPEMATCH **htmatch ,
                                  NAMETYPEMATCH *match_basic ,
                                  SPOTNO spotno ,
                                  GS_COLORinfo *colorInfo ,
                                  SETHALFTONE_PARAMS *htparams ,
                                  SH_DOTSHAPE *sh_dotshape )
{
  HQASSERT( htmatch , "htmatch NULL in dosetht_exec_screens" ) ;
  HQASSERT( match_basic , "match_basic NULL in dosetht_exec_screens" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in dosetht_exec_screens" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht_exec_screens" ) ;
  HQASSERT( sh_dotshape , "sh_dotshape NULL in dosetht_exec_screens" ) ;

  HQASSERT( htparams->nOriginalHalftoneType != 5 , "Type 5 halftones should never get here" ) ;
  HQASSERT( htparams->nUseHalftoneType != 5 , "Type 5 halftones should never get here" ) ;

  HQTRACE( debug_regeneration,
   ("screen: type %d name %.*s color %.*s",
    htparams->nUseHalftoneType ,
    htparams->nmUseHalftoneName ? theINLen( htparams->nmUseHalftoneName ) : 0 ,
    htparams->nmUseHalftoneName ? theICList( htparams->nmUseHalftoneName ) : (uint8*)"" ,
    htparams->nmUseColorKey ? theINLen( htparams->nmUseColorKey ) : 0 ,
    htparams->nmUseColorKey ? theICList( htparams->nmUseColorKey ) : (uint8*)"" )) ;

  /* Add the screen. */
  HQASSERT( ! ht_checkifchentry( spotno, htparams->objectType,
                                 htparams->colorantIndex,
                                 gsc_getHalftonePhaseX( colorInfo ) ,
                                 gsc_getHalftonePhaseY( colorInfo )) ,
                                 "screen already exists" ) ;

  if ( HALFTONE_GROUP_MODULAR == htinfo( htparams->nUseHalftoneType )->group )
  {
    HQASSERT( sh_dotshape->sh_type == SH_TYPE_MODULAR ,
              "should have been a modular type" ) ;
   HQASSERT( htparams->htdict_use, "No halftone dict for modular halftones" );

    /* This is where we start setting up the ghost screen etc.
     * Note that any alternative name and color are not relevant to modular
     * screens, because regardless of whether they are aliased or not, we
     * don't save modular screens in the disk cache.
     */
    if (!newModularHalftone(spotno, htparams->objectType,
                            htparams->colorantIndex ,
                            sh_dotshape->sh_bits.sh_modular.poHTModule,
                            htparams->nmUseHalftoneName ,
                            htparams->nmUseColorKey ,
                            htparams->htdict_use ,
                            colorInfo ) )
      return FALSE ;
  }
  else if ( HALFTONE_GROUP_THRESHOLD == htinfo( htparams->nUseHalftoneType )->group ) {
    int32   width  = sh_dotshape->sh_bits.sh_thresh.width ;
    int32   height = sh_dotshape->sh_bits.sh_thresh.height ;
    int32   depth = sh_dotshape->sh_bits.sh_thresh.depth ;
    OBJECT *thresh = sh_dotshape->sh_bits.sh_thresh.thresh ;
    OBJECT  newfileo = OBJECT_NOTVM_NULL ;
    Bool    extragrays ;
    Bool    limitlevels = FALSE ;

    HQASSERT( htparams->nUseHalftoneType == 3 ||
              htparams->nUseHalftoneType == 4 ||
              htparams->nUseHalftoneType == 6 ||
              htparams->nUseHalftoneType == 10 ||
              htparams->nUseHalftoneType == 16,
              "unknown halftone type for threshold screen" ) ;
    HQASSERT( sh_dotshape->sh_type == SH_TYPE_THRESH ,
              "should have been a threshold type" ) ;

    /* For type 16 thresholds, we default to limiting the number
     * of levels unless /LimitScreenLevels is false in the ht dict.
     */
    if ( 16 == htparams->nUseHalftoneType )
      limitlevels = ( match_basic[ mb_threshold_Limit+2 ].result != NULL ?
                      oBool( *match_basic[ mb_threshold_Limit+2 ].result ) :
                      TRUE ) ;

    /* ScreenExtraGrays system param overrides a missing or false value in
     * the halftone dictionary, but in the case of type 16 thresholds we
     * only permit this when /LimitScreenLevels is false in the ht dict.
     * Likewise, we don't permit this for multi-threshold screens.
     * This is because we don't want to inadvertently disturb screens that
     * incorporate compensation curves in the thresholds.
     */
    if ( 0 == htinfo( htparams->nUseHalftoneType )->multiple_screens ) {
      extragrays = ( match_basic[ mb_threshold_ScreenExtraGrays ].result ?
                     oBool( *match_basic[ mb_threshold_ScreenExtraGrays ].result ) :
                     FALSE ) ;
      if ( !limitlevels && depth == 1 )
        extragrays |= context->systemparams->ScreenExtraGrays ;
    }
    else
      extragrays = FALSE ;

    switch ( htparams->nUseHalftoneType ) {

    case 10:
      if ( !newXYthresholds(context, width, height, depth, thresh,
                            sh_dotshape->sh_bits.sh_thresh.invert,
                            spotno, htparams->objectType, htparams->colorantIndex,
                            htparams->nmUseHalftoneName ,
                            htparams->nmUseColorKey ,
                            htparams->nmAlternativeName,
                            htparams->nmAlternativeColor,
                            htparams->cacheType,
                            TRUE ,
                            extragrays ,
                            &newfileo,
                            htparams->encryptType, htparams->encryptLength,
                            colorInfo) )
        return FALSE ;
      break;

    case 16:
      if ( !new16thresholds(context, width, height, depth, thresh,
                            sh_dotshape->sh_bits.sh_thresh.invert,
                            spotno, htparams->objectType, htparams->colorantIndex,
                            htparams->nmUseHalftoneName ,
                            htparams->nmUseColorKey ,
                            htparams->nmAlternativeName,
                            htparams->nmAlternativeColor,
                            htparams->cacheType,
                            TRUE ,
                            extragrays ,
                            limitlevels ,
                            &newfileo ,
                            sh_dotshape->sh_bits.sh_thresh.width2 ,
                            sh_dotshape->sh_bits.sh_thresh.height2,
                            htparams->encryptType, htparams->encryptLength,
                            colorInfo) )
        return FALSE ;
      break;

    default:
      if ( ! newthresholds( context, width, height, depth, thresh,
                            sh_dotshape->sh_bits.sh_thresh.invert,
                            spotno, htparams->objectType, htparams->colorantIndex,
                            htparams->nmUseHalftoneName ,
                            htparams->nmUseColorKey ,
                            htparams->nmAlternativeName,
                            htparams->nmAlternativeColor,
                            htparams->cacheType,
                            TRUE ,
                            extragrays ,
                            &newfileo,
                            htparams->encryptType, htparams->encryptLength,
                            colorInfo) )
        return FALSE ;
    }

    if ( oType(newfileo) != ONULL ) {
      /* File object has changed, need to try and poke it back into the
         dictionary that it came from */

      /* The field htparams->htdict_use should be just what we want nowadays.
         It points to the appropriate main-dict or type-5-sub-dict for either
         the original or the override halftone.
       */
      OBJECT *dict = htparams->htdict_use;

      HQASSERT(oType(newfileo) == OFILE,
               "newfileo should be a file");
      HQASSERT(dict, "no dict in dosetht_exec_screens?");

      HQASSERT(oType(*dict) == ODICTIONARY,
               "dosetht_exec_scr: expected a dictionary");

      oName( nnewobj ) = theIMName( &match_basic[ mb_threshold_Thresholds ]);
      if ( ! fast_insert_hash( dict , & nnewobj , & newfileo ))
        return FALSE ;
    }

  }
  else {
    /* not a threshold screen */

    SYSTEMVALUE freq = sh_dotshape->sh_bits.sh_spotfn.freq ;
    SYSTEMVALUE angl = sh_dotshape->sh_bits.sh_spotfn.angl ;
    OBJECT   *spotfn = sh_dotshape->sh_bits.sh_spotfn.spotfn ;
    NAMETYPEMATCH *match1_extra = htmatch[ HT_MATCH_EXTRA ] ;
    int32 doublescreens ;

    HQASSERT( sh_dotshape->sh_type == SH_TYPE_SPOTFN , "should have been a spotfunction type" ) ;
    HQASSERT( htparams->nUseHalftoneType == 1 ||
              htparams->nUseHalftoneType == 2 ,
              "unknown halftone type for threshold screen" ) ;

    if ( 0 == htinfo( htparams->nUseHalftoneType )->multiple_screens &&
         match_basic[ 3 ].result )
      doublescreens = oBool( *match_basic[ 3 ].result ) ;
    else
      doublescreens = FALSE ;

    if ( ! newhalftones( context, & freq , & angl ,
                         spotfn ,
                         spotno, htparams->objectType, htparams->colorantIndex,
                         htparams->nmUseHalftoneName ,
                         htparams->nmUseColorKey ,
                         htparams->nmAlternativeName,
                         htparams->nmAlternativeColor,
                         htparams->cacheType,
                         ( match1_extra && match1_extra[ 2 ].result ) ?
                           oBool( *match1_extra[ 2 ].result ) :
                           FALSE ,
                         match1_extra && match1_extra[ 2 ].result ,
                         doublescreens ,
                         match1_extra &&
                         ( match1_extra[ 0 ].result || match1_extra[ 1 ].result ) ,
                         NULL ,
                         FALSE ,
                         htparams->overridefreq ,
                         htparams->overrideangle ,
                         TRUE ,
                         gsc_getHalftonePhaseX( colorInfo ) ,
                         gsc_getHalftonePhaseY( colorInfo ) ,
                         match1_extra ? match1_extra[3].result : NULL, /* poAdjustScreen */
                         colorInfo ))
      return FALSE ;
  }

  HQASSERT( ht_checkifchentry( spotno, htparams->objectType,
                               htparams->colorantIndex,
                               gsc_getHalftonePhaseX( colorInfo ),
                               gsc_getHalftonePhaseY( colorInfo )),
            "newModularHalftone/newhalftones/newthresholds succeeded but halftone didn't get created" ) ;

  return TRUE ;
}


/**
 * This routine is responsible for checking the arguments on the operand
 * stack provided to the setscreen, setcolorscreen or sethalftone operator.
 * The arguments on the operand stack are popped off and pushed onto the
 * temporary stack and the number of them are returned.
 */
static Bool setscreens_checkargs( STACK *stack , int32 screentype , int32 *sl ,
                                  SYSTEMVALUE freqv[ 4 ] ,
                                  SYSTEMVALUE anglv[ 4 ] ,
                                  OBJECT *proco[ 4 ] )
{
  int32 lsl ;

  if ( screentype == ST_SETHALFTONE ) { /* Argument passed in to function. */
    OBJECT *theo ;
    lsl = 1 ;
    if ( isEmpty( *stack ))
      return error_handler( STACKUNDERFLOW ) ;

    theo = theITop( stack ) ;
    if ( oType( *theo ) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;

    if ( ! push( theo , & temporarystack ))
      return FALSE ;
    proco[ 0 ] = proco[ 1 ] = proco[ 2 ] = proco[ 3 ] = theTop( temporarystack ) ;
  }
  else {                              /* Arguments to be got off stack.  */
    int32 ts ;
    int32 i , n ;
    int32 dictcount ;
    n = 1 ;
    if ( screentype == ST_SETCOLORSCREEN )
      n = 4 ;
    lsl = 3 * n ;

    if ( theIStackSize( stack ) < lsl - 1 )
      return error_handler( STACKUNDERFLOW ) ;
    ts = 0 ;
    for ( i = lsl - 1 ; i >= 0 ; --i ) {
      OBJECT *theo = stackindex( i , stack ) ;
      if ( ! push( theo , & temporarystack )) {
        npop( ts , &temporarystack ) ;
        return FALSE ;
      }
      ++ts ;
    }

    dictcount = 0 ;
    for ( i = 0 ; i < n ; ++i ) {
      proco[ i ] = stackindex( lsl - 1 - 2 - 3 * i , & temporarystack ) ;
      if ( ! checksinglescreen( stackindex( lsl - 1 - 0 - 3 * i , & temporarystack ) , freqv + i ,
                                stackindex( lsl - 1 - 1 - 3 * i , & temporarystack ) , anglv + i ,
                                proco[ i ] ,
                                TRUE , & dictcount )) {
        npop( ts , & temporarystack ) ;
        return FALSE ;
      }
    }
    /* If top arg is NOT a dictionary, then NO dictionaries allowed at all. */
    if (( dictcount ) && ( dictcount != n )) {
      npop( ts , & temporarystack ) ;
      return error_handler( TYPECHECK ) ;
    }
    /* If from setscreen then duplicate frequency & angle into other slots. */
    if ( screentype == ST_SETSCREEN ) {
      freqv[ 1 ] = freqv[ 2 ] = freqv[ 3 ] = freqv[ 0 ] ;
      anglv[ 1 ] = anglv[ 2 ] = anglv[ 3 ] = anglv[ 0 ] ;
      proco[ 1 ] = proco[ 2 ] = proco[ 3 ] = proco[ 0 ] ;
    }
  }
  (*sl) = lsl ;
  return TRUE ;
}


/**
 * This routine is used by the dosethalftone5 routine to create any missing
 * screens which can be got by looking at complementary colors. This is what
 * Adobe do. By complementary colors we mean that if the output device
 * requires a Cyan screen but Cyan is missing and Red is not, then we use
 * the Red halftone instead.
 */
static Bool dosetht5_exec_complementaries( corecontext_t *context,
                                           OBJECT *theo ,
                                           SPOTNO spotno ,
                                           Bool checkargs ,
                                           GS_COLORinfo *colorInfo ,
                                           SETHALFTONE_PARAMS *htparams ,
                                           SYSTEMVALUE *freqv ,
                                           SYSTEMVALUE *anglv )
{
  int32 nColorant, nColorants ;
  DEVICESPACEID deviceSpace;
  COLORANTINDEX aMapping[4];
  OBJECT * poComplementaryHalftoneDict;

  HQASSERT( theo , "theo NULL in dosetht5_exec_complementaries" ) ;
  HQASSERT( colorInfo , "colorInfo NULL in dosetht5_exec_complementaries" ) ;
  HQASSERT( htparams , "htparams NULL in dosetht5_exec_complementaries" ) ;

  /* Look for complementary colors.  I.e., if going to DeviceGray but
   * didn't find a Gray, then look for Black.  Similarly, if going to
   * DeviceRGB and didn't find a Red, look for a Cyan, etc.
   */

  guc_deviceColorSpace (colorInfo->deviceRS, & deviceSpace, & nColorants);

  if (deviceSpace != DEVICESPACE_N) {
    /* If it turns out to be completely general, including DeviceN, this
       could always simply look up Red for Cyan, Cyan for Red and so on.
       Until we know that, only look up the complementaries if the
       colorant does not exist directly in the simple color space. */
    NAMECACHE** pnComplementaries = NULL;
    NAMECACHE** pnOriginals = NULL;

    HQASSERT (nColorants <= 4, "guc_deviceColorSpace gave me more than 4 simple colorants");
    if (!guc_simpleDeviceColorSpaceMapping(colorInfo->deviceRS, deviceSpace,
                                           aMapping, nColorants))
      return FALSE;

    switch (deviceSpace) {
    case DEVICESPACE_Gray:
      pnComplementaries = pcmKName;
      pnOriginals = pcmGyName;
      break;
    case DEVICESPACE_CMYK:
    case DEVICESPACE_CMY:
      pnComplementaries = pcmRGBGyNames;
      pnOriginals = pcmCMYKNames;
      break;
    case DEVICESPACE_RGB:
    case DEVICESPACE_RGBK:
      pnComplementaries = pcmCMYGyNames;
      pnOriginals = pcmRGBGyNames;
      break;
    default:
      break;
    }
    if (pnComplementaries != NULL) {
      for (nColorant = 0; nColorant < nColorants; nColorant++) {
        /* Is there a screen for the original colorant? */
        if (checkargs /* when checking, assume there isn't */
            || !ht_checkifchentry( spotno, htparams->objectType,
                                   aMapping[nColorant],
                                   gsc_getHalftonePhaseX(colorInfo),
                                   gsc_getHalftonePhaseY(colorInfo) )) {
          /* no screen for original colorant, so for Gray, try Black;
             for Cyan, try Red, etc. */
          oName (nnewobj) = pnComplementaries[nColorant];
          poComplementaryHalftoneDict = extract_hash( theo, & nnewobj );

          /* Was there a screen for the complement?  If so, process it for
             the original colorant */
          if (poComplementaryHalftoneDict != NULL) {
            OBJECT lnnewobj = OBJECT_NOTVM_NOTHING ;

            theTags(lnnewobj) = ONAME | LITERAL ;
            oName(lnnewobj) = pnOriginals[nColorant];
            htparams->nmUseColorKey = oName(lnnewobj) ;
            /* For example, in DeviceRGB add RED_SEPARATION screen using
               /Cyan dictionary entry. */
            htparams->htdict_use = poComplementaryHalftoneDict;
            if ( ! dosethalftone( context, & lnnewobj, poComplementaryHalftoneDict, ST_SETHALFTONE,
                                  spotno, checkargs, freqv, anglv, colorInfo , htparams ))
              return FALSE ;

            /* Consider the complementary entries to be extensions to
               the original dictionary */
            htparams->nSubDictIndex++ ;
          }
        }
      }
    }
  }
  return TRUE ;
}


static Bool ht_name_to_HTTYPE(HTTYPE *type_out, NAMECACHE *name)
{
  int16 objNameNo = theINameNumber(name);
  switch (objNameNo) {
  case NAME_Picture:  *type_out = REPRO_TYPE_PICTURE; break;
  case NAME_Text:     *type_out = REPRO_TYPE_TEXT; break;
  case NAME_Vignette: *type_out = REPRO_TYPE_VIGNETTE; break;
  case NAME_Linework: *type_out = REPRO_TYPE_OTHER; break;
  case NAME_Default:  *type_out = HTTYPE_DEFAULT; break;
  default: return error_handler(RANGECHECK);
  }
  return TRUE;
}


/**
 * This routine is used by the dosethalftone5 routine to call dosethalftone
 * for those sub-objects in the halftone dictionary that walk_dictionary found.
 * The parameter args is a structure containing the original data that was
 * passed to dosethalftone which the recursive call needs. [This is the only
 * way to get that data passed through the walk_dictionary call.]
 */
static Bool dictwalk_dosethalftone5( OBJECT * poKeyOfType5Subordinate,
                                      OBJECT * poDict,
                                      void *args )
{
  SETHALFTONE_PARAMS *htparams ;
  DW_dosetht_Params *params = (DW_dosetht_Params *)args ;
  NAMECACHE * pnmKeyOfType5Subordinate = NULL;

  /* Ignore non-colour keys. Note we only need to test for an ONAME
   * object since any OSTRING objects will have been converted to ONAME
   * objects when they were inserted into the halftone dictionary. */
  if ( oType( *poKeyOfType5Subordinate ) != ONAME )
    return TRUE;
  pnmKeyOfType5Subordinate = oName( *poKeyOfType5Subordinate );
  if ( pnmKeyOfType5Subordinate == system_names + NAME_Type
       || pnmKeyOfType5Subordinate == system_names + NAME_HalftoneType
       || pnmKeyOfType5Subordinate == system_names + NAME_HalftoneName
       || pnmKeyOfType5Subordinate == system_names + NAME_Override
       || pnmKeyOfType5Subordinate == system_names + NAME_OverrideAngle
       || pnmKeyOfType5Subordinate == system_names + NAME_OverrideFrequency
       || pnmKeyOfType5Subordinate == system_names + NAME_Separations
       /* Mapped is added as part of PDF input */
       || pnmKeyOfType5Subordinate == system_names + NAME_Mapped )
    return TRUE;

  htparams = params->htparams ;
  htparams->htdict_use = poDict ;

  if ( htparams->nOriginalHalftoneType == 195 ) {
    if (!ht_name_to_HTTYPE(&htparams->objectType, pnmKeyOfType5Subordinate))
      return FALSE;
    htparams->cacheType = htparams->objectType;
  } else {
    HQASSERT(htparams->nmAlternativeColor == NULL,
             "Nested type 5 halftones");
  }

  if (!dosethalftone( params->context,
                      htparams->nOriginalHalftoneType == 5
                      ? poKeyOfType5Subordinate : NULL,
                      poDict,
                      ST_SETHALFTONE,
                      params->spotno,
                      params->checkargs,
                      params->freqv,
                      params->anglv,
                      params->colorInfo ,
                      htparams ) )
    return FALSE;

  /* Increment a count of subdicts, so that ht xfer info can be inserted
   * into the correct place in its array. */
  if ( htparams->nOriginalHalftoneType == 5
       && pnmKeyOfType5Subordinate != system_names + NAME_Default )
    htparams->nSubDictIndex++;
  return TRUE;
}


/**
 * This routine is the handler for Type 5 & 195 halftone dictionaries.
 * It uses walk_dictionary to check all objects in the dictionary and recurses
 * into dosethalftone to 'execute' these.
 * Having done that, it checks for missing complementary colors and after that
 * finishes off by adding missing screens, using the Default entry.
 */
static Bool dosethalftone5( corecontext_t *context,
                            OBJECT *theo ,
                            SPOTNO spotno ,
                            Bool checkargs ,
                            SYSTEMVALUE *freqv , /* non-null if to override dictionary value */
                            SYSTEMVALUE *anglv ,
                            GS_COLORinfo *colorInfo ,
                            SETHALFTONE_PARAMS *htparams ,
                            NAMETYPEMATCH **htmatch )
{
  int32 loverride = 0 ;
  int32 gsloverride = 0 ;
  Bool result ;
  Bool isloverride = FALSE ;
  Bool isType5 = htparams->nOriginalHalftoneType == 5;
  NAMETYPEMATCH *match_basic ;
  NAMETYPEMATCH *match_general ;
  DW_dosetht_Params args ;
  OBJECT * poDefaultDictionary;
  OBJECT *parent = htparams->htdict_parent;

  htparams->nSubDictIndex = 0;

  match_general = htmatch[ HT_MATCH_GENERAL ] ;
  HQASSERT( match_general , "htmatch[ HT_MATCH_GENERAL ] must always exist" ) ;

  if ( match_general[mg_Override].result != NULL ) {
    isloverride = TRUE ;
    HQASSERT( oType( *match_general[mg_Override].result ) == OINTEGER,
      "Override should have been an integer in Type 5 halftone" ) ;
    loverride = oInteger( *match_general[mg_Override].result );
    /* When encounter an /Override key, don't inherit anything from setscreen. */
    freqv = anglv = NULL ;
  }
  gsloverride = colorInfo->halftoneInfo->halftoneoverride ;

  match_basic = htmatch[ HT_MATCH_BASIC ] ;
  HQASSERT( match_basic , "htmatch[ HT_MATCH_BASIC ] must always exist" ) ;

  /* Separations key (hqn extension) */
  if ( isType5 ) {
    HQASSERT( match_basic[ 1 ].result == NULL ||
              oType( *match_basic[ 1 ].result ) == OBOOLEAN ,
              "Separations should have been a boolean in type 5 halftone" ) ;
    htparams->fSeparationsKeySetInType5 = match_basic[ 1 ].result != NULL &&
                                          oBool( *match_basic[ 1 ].result ) ;
    if (!checkargs && htparams->fSeparationsKeySetInType5)
      guc_clearColorant (colorInfo->deviceRS, COLORANTINDEX_ALL);
  }
  /* Set up parameter block required by walk_dictionary so can pass
   * arguments required by dosethalftone back into it from the callback. */
  args.spotno = spotno ;
  args.checkargs = checkargs ;
  args.freqv = freqv ;
  args.anglv = anglv ;
  args.colorInfo = colorInfo ;
  args.htparams = htparams ;
  args.context = context;

  /* Go for all those subordinate dictionaries ... */
  htparams->htdict_parent = theo;
  colorInfo->halftoneInfo->halftoneoverride = 0 ; /* So we don't upset subsidaries. */
  result = walk_dictionary( theo , dictwalk_dosethalftone5 , ( void * )& args ) ;
  colorInfo->halftoneInfo->halftoneoverride = gsloverride ;
  htparams->htdict_parent = parent;
  if ( ! result )
    return FALSE ;

  /* Work out what we need to do if the halftone dict has a /Override key
   * and/or if the current halftone in the gstate does too.
   */
  if ( isloverride ) {
    if ( loverride < gsloverride ) {
      halftoneoverride = TRUE ;
      return TRUE ;
    }
  } else if ( gsloverride > 0 ) {
    halftoneoverride = TRUE ;
    return TRUE ;
  }

  if ( isType5 ) {
    /* Look for complementary colors.  I.e., if going to DeviceGray but
     * didn't find a Gray, then look for Black. Similarly if going to
     * DeviceRGB and didn't find a Red, look for a Cyan, etc... */
    htparams->htdict_parent = theo;
    colorInfo->halftoneInfo->halftoneoverride = 0 ; /* So we don't upset subsidaries. */
    result = dosetht5_exec_complementaries( context, theo, spotno, checkargs,
                                            colorInfo, htparams, freqv, anglv );
    colorInfo->halftoneInfo->halftoneoverride = gsloverride ;
    htparams->htdict_parent = parent;
    if ( ! result )
      return FALSE ;

    if (checkargs) {
      /* Initialise the transfer cache, now that we know how many
         colorants there are. */
      if (!initTransferCache(colorInfo->halftoneInfo,
                             htparams->objectType, htparams->nSubDictIndex))
        return FALSE;
    }
  }

  if ( checkargs )
    return TRUE;

  /* check if any screen of this set exists */
  HQASSERT( ht_checkifchentry( spotno, HTTYPE_DEFAULT, COLORANTINDEX_UNKNOWN,
                               gsc_getHalftonePhaseX( colorInfo ) ,
                               gsc_getHalftonePhaseY( colorInfo )) ,
            "dosethalftone succeeded but halftone(s) didn't get created" );

  if ( !isType5 ) { /* the rest is for type 5 only */
    if ( isloverride )
      colorInfo->halftoneInfo->halftoneoverride = loverride ;
    return TRUE;
  }

  /* Now duplicate the Default screen into the process screens (which we
   * need) which don't have one; we know it has a Default, because we
   * rangecheck'd above if there wasn't.
   */
  poDefaultDictionary = match_basic[0].result;
  if ( !dosetht_resolve199( &poDefaultDictionary, TRUE, htparams, HT_SEP ))
    return FALSE;

  if ( htparams->nOriginalHalftoneType == 1 ) {
    SYSTEMVALUE freq , angl ;

    /* Frequency key */
    if ( ! dictmatch( poDefaultDictionary , htmatch1_basic )) {
      /* We've done this once already in the recursive call to dosethalftone
       * so it should now work.
       */
      HQFAIL( "match on default screen failed second time round" ) ;
      return FALSE ;
    }

    /* The match checks the type. */
    HQASSERT( oType(*htmatch1_basic[ 0 ].result) == OINTEGER ||
              oType(*htmatch1_basic[ 0 ].result) == OREAL ,
              "Frequency in Default screen is not int or real" ) ;

    freq = object_numeric_value(htmatch1_basic[ 0 ].result) ;
    angl = object_numeric_value(htmatch1_basic[ 1 ].result) ;

    if ( ! setup_implicit_screens( context, freq , htmatch1_basic[ 2 ].result /* SpotFunction */,
                                   spotno , TRUE /* from type 5 */ , htparams , colorInfo ))
      return FALSE ;

  } else {

    HQASSERT( htparams->nOriginalHalftoneType == 3 ||
              htparams->nOriginalHalftoneType == 6 ||
              htparams->nOriginalHalftoneType == 10 ||
              htparams->nOriginalHalftoneType == 16 ||
              htparams->nOriginalHalftoneType == 100,
              "bad/unknown sub type in Default dictionary" ) ;

    /* Default is not a spotfn screen, so no need to worry about the
       frequency or angle */
    if ( ! setup_implicit_screens( context, 0.0 /* Frequency */, NULL /* SpotFunction */,
                                   spotno , TRUE /* from type 5 */ , htparams , colorInfo ))
      return FALSE ;
  }

  if ( isloverride )
    colorInfo->halftoneInfo->halftoneoverride = loverride ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

Bool dosethalftone199_find(OBJECT **po, OBJECT *dict,
                           NAMETYPEMATCH *match, size_t i)
{
  *po = extract_hash(dict, match[i].result);
  if ( *po == NULL )
    return detailf_error_handler(UNDEFINED,
             "Halftone redirect did not find %.*s %.*s in %s",
             theIMName(&match[i])->len, theIMName(&match[i])->clist,
             oName(*match[i].result)->len, oName(*match[i].result)->clist,
             i == 0 ? "screens" : "dictionary");
  if ( oType(**po) != ODICTIONARY )
    return detail_error_handler(TYPECHECK,
                                "Halftone redirect target is not a dictionary");
  return TRUE;
}


/** This function deals with halftone redirection: finding a substitute
   for a special type 199 halftone dictionary with a halftone dictionary
   from the same or a different type 5/195 halftone */

static Bool dosethalftone199(OBJECT * poType199Dictionary,
                             OBJECT ** ppoSubstituteDictionary,
                             SETHALFTONE_PARAMS *htparams)
{
  static NAMETYPEMATCH htmatch199[] = {
    /* 0 */  { NAME_Halftone | OOPTIONAL,    2, { ONAME, OSTRING }},
    /* 1 */  { NAME_HalftoneColor | OOPTIONAL, 2, { ONAME, OSTRING }},
    /* 2 */  { NAME_HalftoneObjectType | OOPTIONAL, 2, { ONAME, OSTRING }},
    DUMMY_END_MATCH
  };
  OBJECT * po, * poHalftoneType;

  po = htparams->htdict_parent; /* our parent, until we hear differently */

  /* guard against infinite recursion */
  htparams->cAlternativeHalftoneRedirections++;
  if (htparams->cAlternativeHalftoneRedirections > 16) /* just a suitably large
                                                          number, no particular
                                                          significance */
    return detail_error_handler(LIMITCHECK,
                                "Halftone redirect recursion is too deep.");

  if (! dictmatch (poType199Dictionary, htmatch199))
    return FALSE;

  /* redirect our attention to the alternative color/type and possibly
     alternative halftone name indicated by the dictionary */

  if (htmatch199[0].result != NULL) {
    /* we are being pointed at another halftone in switchscreens. It must be a
       type 5/195 halftone dictionary, not a spotfunction or single screen */
    OBJECT *tmp;

    tmp = fast_extract_hash_name(& systemdict, NAME_DollarPrinterdict);
    HQASSERT (tmp != NULL, "$printerdict not found in systemdict");
    HQASSERT (oType(*tmp) == ODICTIONARY, "$printerdict is not a dictionary");

    tmp = fast_extract_hash_name(tmp, NAME_switchscreens);
    HQASSERT (tmp != NULL, "switchscreens not found in $printerdict");
    HQASSERT (oType(*tmp) == ODICTIONARY, "switchscreens not a dictionary");

    if ( !dosethalftone199_find(&po, tmp, htmatch199, 0) )
      return FALSE;

    /* that's sufficient to find a color in that dictionary, but we should be
       more rigorous on checking it is a type 5/195 */
    poHalftoneType = fast_extract_hash_name(po, NAME_HalftoneType);
    HQASSERT(poHalftoneType != NULL,
             "Halftone redirect did not find HalftoneType in target halftone.");
    HQASSERT(oType(*poHalftoneType) == OINTEGER,
             "Halftone redirect HalftoneType is not an integer.");
    if ( oInteger(*poHalftoneType) != 5 && oInteger(*poHalftoneType) != 195 )
      return detail_error_handler(RANGECHECK,
                                  "Halftone redirect is not type 5 or 195");

    /* OK, let's use that in place of the dictionary we thought we were
       using, and proceed to look for the type & the color; otherwise
       we'll be using our original dictionary in looking for the type &
       the color. Note where we went to, for the purposes of picking up
       the screen cache. */
    htparams->nmAlternativeName =
      oType(*htmatch199[0].result) == ONAME ?
      oName(*htmatch199[0].result) :
      cachename(oString(*htmatch199[0].result),
                (uint32)theLen(*htmatch199[0].result));
    /* Other keys will be looked up relative to this new name, so reset. */
    htparams->cacheType = HTTYPE_ALL;
  }

  if ( htmatch199[2].result != NULL ) { /* look up type, if given */
    NAMECACHE *name;

    if ( !dosethalftone199_find(&po, po, htmatch199, 2) )
      return FALSE;
    name = oType(*htmatch199[2].result) == ONAME
           ? oName(*htmatch199[2].result)
           : cachename(oString(*htmatch199[2].result),
                       (uint32)theLen(*htmatch199[2].result));
    if (!ht_name_to_HTTYPE(&htparams->cacheType, name))
      return FALSE;
  }

  if ( htmatch199[1].result != NULL ) { /* look up color, if given */
    if ( !dosethalftone199_find(&po, po, htmatch199, 1) )
      return FALSE;
    htparams->nmAlternativeColor =
      oType(*htmatch199[1].result) == ONAME ?
      oName(*htmatch199[1].result) :
      cachename(oString(*htmatch199[1].result),
                (uint32)theLen(*htmatch199[1].result));
  }

  /* OK, we got it, tell the caller */
  * ppoSubstituteDictionary = po;
  return TRUE;
}


/**
 * This routine is the main body control for halftone dictionaries. Note
 * that type 5 & 195 halftones are dealt with in the dosethalftone5 routine
 * (and recurse back in).
 * This routine may be called due to a number of circumstances:
 *  (a) a sethalftone operator.
 *  (b) a set[color]screen operator with a halftone dict instead of a proc.
 *  (c) the equivalent of (a) or (b) due to a grestore (or equivalent) operator.
 *  (d) a sethalftone or set[color]screen operator when an override exists.
 *  (e) creating missing screens when a halftone dict override exists.
 */
static Bool dosethalftone( corecontext_t *context,
                           OBJECT *poKeyOfType5Subordinate,
                           OBJECT *thedict ,
                           int32 screentype ,
                           SPOTNO spotno ,
                           Bool checkargs ,
                           SYSTEMVALUE *freqv , /* non-null if to override dictionary value */
                           SYSTEMVALUE *anglv ,
                           GS_COLORinfo *colorInfo ,
                           SETHALFTONE_PARAMS *htparams )
{
  int32 cScreens, nScreen ;
  Bool overridescreen ;
  int32 i;
  OBJECT    * poOverrideHalftoneDict ;
  NAMECACHE * nmOverrideHalftoneName ;
  NAMECACHE * pnmKeyOfType5Subordinate = NULL;
  NAMETYPEMATCH **htmatch ;
  DL_STATE *page = context->page;
  COLOR_PAGE_PARAMS *colorPageParams = &page->colorPageParams;

  int32 loverride = 0 ;
  int32 gsloverride = 0 ;
  Bool isloverride = FALSE ;

  SETHALFTONE_PARAMS recparams ;

  /* Parameters for a single dot shape. */
  SH_DOTSHAPE sh_dotshape = { 0 } ;

  /* Do required setup for this call... */
  if ( htparams == NULL ) {
    htparams = ( & recparams ) ;
    dosetht_init_sht_params( thedict , NULL , htparams ) ;
  } else {
    /* Use a local copy on recursion so any changes during recursion don't upset parent. */
    recparams = (*htparams) ;
    htparams = & recparams ;
  }
  if ( poKeyOfType5Subordinate != NULL )
    pnmKeyOfType5Subordinate = oName( *poKeyOfType5Subordinate );

  if ( screentype == ST_SETHALFTONE ) {
    int32 httype;

    switch ( oType( *thedict )) {
    case ODICTIONARY:
      /* normal case */
      break;

    /* The following are extensions to support separation control
     * applying to type 5's with Separations on only.
     */
    case OSTRING: /* synonym */
    case ONAME:   /* synonym */
    case ONULL:   /* calculate but omit separation */
      if ( ! htparams->fSeparationsKeySetInType5 )
        return error_handler( TYPECHECK ) ;
      if ( checkargs )
        return TRUE ;

      HQASSERT( poKeyOfType5Subordinate != NULL ,
                "poKeyOfType5Subordinate should not be NULL" ) ;
      HQASSERT( pnmKeyOfType5Subordinate != NULL ,
                "pnmKeyOfType5Subordinate should not be NULL" ) ;

      if (oType( *thedict ) == OSTRING ||
          oType( *thedict ) == ONAME)
      {
        COLORANTINDEX dummy_ci;

        /* the key is a synonym for an existing name (well, it may not exist quite
           yet, because it may be in the same dictionary, but it will) */
        NAMECACHE * pnmSynonym;
        if (oType(*thedict) == ONAME)
          pnmSynonym = oName(*thedict);
        else
          pnmSynonym = cachename(oString(*thedict), theLen(*thedict));

        if (!guc_colorantSynonym(colorInfo->deviceRS, pnmKeyOfType5Subordinate,
                                 pnmSynonym, &dummy_ci))
          return FALSE;

      } else if (oType( *thedict ) == ONULL) {

        COLORANTINDEX ci, newCi;

        /* remove any references to the colorant as a separation to render, but
           retain it as a known separation (in PostScript terms, it is in
           SeparationColorNames but not SeparationOrder) */
        ci = guc_colorantIndex (colorInfo->deviceRS, pnmKeyOfType5Subordinate);
        if (ci != COLORANTINDEX_UNKNOWN)
          guc_clearColorant (colorInfo->deviceRS, ci);
        if (!guc_colorantIndexPossiblyNewSeparation (colorInfo->deviceRS,
                                                     pnmKeyOfType5Subordinate, &newCi))
          return FALSE;
        HQASSERT (newCi == ci || ci == COLORANTINDEX_UNKNOWN,
                  "new separation has different number from old one");

      }

      return TRUE;

    default:
      if ( poKeyOfType5Subordinate != NULL ) {
        HQASSERT( oType( *poKeyOfType5Subordinate ) == ONAME ,
                  "poKeyOfType5Subordinate not a name - already checked" ) ;
        if ( guc_colorantIndex (colorInfo->deviceRS,
                                oName( *poKeyOfType5Subordinate )) ==
             COLORANTINDEX_UNKNOWN)
          return TRUE ; /* not found */
        else
          return error_handler( TYPECHECK ) ;
      } else {
        return error_handler( TYPECHECK ) ;
      }
    }

    /* Check permission access of halftone dictionary. */
    if ( ! oCanRead( *oDict( *thedict )) &&
         ! object_access_override(oDict(*thedict)) )
      return error_handler( INVALIDACCESS ) ;

    if ( ! dosetht_get_htnameandtype( thedict, poKeyOfType5Subordinate != NULL,
                                      htparams, HT_SEP, &httype ))
      return FALSE ;

    if ( httype == 199 ) {
      OBJECT * poSubstituteDictionary;

      if (! dosethalftone199 (thedict, & poSubstituteDictionary, htparams))
        return FALSE;

      /* Note that all we have done is substitute one halftone dictionary for
         another, so poKeyOfType5Subordinate stays the same, as do the other
         parameters; only the subordinate dictionary itself changes in the call to
         dosethalftone */
      return dosethalftone(context, poKeyOfType5Subordinate, poSubstituteDictionary, screentype,
                           spotno, checkargs, freqv, anglv, colorInfo, htparams);
    }

    /* Perform all the required matches. */
    htmatch = htmatches( htparams->nOriginalHalftoneType ) ;
    for ( i = 0 ; i < HT_MAX_MATCHES ; ++i ) {
      NAMETYPEMATCH *match = htmatch[ i ] ;
      if ( match )
        if ( ! dictmatch( thedict , match ))
          return FALSE ;
    }

    {
      NAMETYPEMATCH *match_general = htmatch[ HT_MATCH_GENERAL ] ;
      dosetht_extract_general( htparams, match_general );
    }

    /* Deal with type 5 and 195 halftones separately, otherwise continue
       with a single halftone dictionary (possibly recursively out of a
       type 5/195) */
    if ( htparams->nOriginalHalftoneType == 5
         || htparams->nOriginalHalftoneType == 195 )
      return dosethalftone5( context, thedict , spotno , checkargs , freqv , anglv ,
                             colorInfo , htparams , htmatch ) ;

  } else {
    /* not ST_SETHALFTONE */

    HQASSERT( screentype == ST_SETSCREEN ||
              screentype == ST_SETCOLORSCREEN ||
              screentype == ST_SETMISSING ,
              "bad screentype" ) ;
    htparams->nOriginalHalftoneType = 1 ;
    if ( screentype == ST_SETCOLORSCREEN )
      htparams->nOriginalHalftoneType = 2 ;
    htmatch = htmatches( htparams->nOriginalHalftoneType ) ;

    /* Null all the set[color]screen bits. */
    for ( i = 0 ; i < HT_MAX_MATCHES ; ++i ) {
      NAMETYPEMATCH *match = htmatch[ i ] ;
      if ( match ) {
        while ( theISomeLeft( match )) {
          match[ 0 ].result = NULL ;
          ++match ;
        }
      }
    }
  }

  {
    NAMETYPEMATCH *match_general = htmatch[ HT_MATCH_GENERAL ] ;

    HQASSERT( match_general , "htmatch[ HT_MATCH_GENERAL ] must always exist" ) ;
    HQASSERT( match_general == htmatch_general , "unexpected match_general" ) ;

    if ( match_general[mg_Override].result != NULL ) {
      isloverride = TRUE ;
      HQASSERT( oType( *match_general[mg_Override].result ) == OINTEGER,
                "This should have been an integer" ) ;
      loverride = oInteger( *match_general[mg_Override].result );
      /* When encounter an /Override key, don't inherit anything from setscreen. */
      freqv = anglv = NULL ;
    }
    gsloverride = colorInfo->halftoneInfo->halftoneoverride ;
  }

  /* By now we have:
   *  (a) A HalftoneType for the halftone being done.
   *  (b) Possibly a HalftoneName for the halftone being done.
   *  (c) If a complementary Type5, then a Color name for the halftone used.
   */

  /* If we are overriding a sethalftone, then we need to use different screen. */
  overridescreen = FALSE ;
  if ( getdotshapeoverride(context, & nmOverrideHalftoneName , & poOverrideHalftoneDict ) ==
       SCREEN_OVERRIDE_SETHALFTONE ) {
    overridescreen = TRUE ;

    /* Use the name indicated by the override for the screen, unless
       that screen itself provides a name */
    htparams->nmOverrideHalftoneName = nmOverrideHalftoneName ;

    if ( !dosetht_resolve195( &poOverrideHalftoneDict, htparams, HT_OVR ))
      return FALSE;
    if ( !dosetht_resolve199( &poOverrideHalftoneDict, FALSE, htparams, HT_OVR ))
      return FALSE;
  }

  /* By now we have:
   *  (a) A HalftoneType for the halftone being done.
   *  (b) Possibly a HalftoneName for the halftone being done.
   *  (c) If a complementary Type5, then a Color name for the halftone used.
   *  (d) If overriding, then both a HalftoneType & HalftoneName for the override.
   */

  /* Deal with single screens (take multiple_screens - the 4 screens of a type 2, 4
     or setcolorscreen, having already excluded type 5's - in turn) */

  cScreens = 1;
  if ( 0 != htinfo( htparams->nOriginalHalftoneType )->multiple_screens )
    cScreens = 4;

  for ( nScreen  = 0 ; nScreen < cScreens; nScreen++ ) {
    SETHALFTONE_PARAMS mulparams ;
    NAMETYPEMATCH *match_basic = htmatch[ HT_MATCH_BASIC ] ;
    HQASSERT( match_basic , "htmatch[ HT_MATCH_BASIC ] must always exist" ) ;

    HQASSERT( htparams->nOriginalHalftoneType != 5 , "Type 5 halftones should never get here" ) ;
    if ( 0 != htinfo( htparams->nOriginalHalftoneType )->multiple_screens ) {

      /* Before doing anything else, take a local copy
         so we don't get disrupted by recursion */
      mulparams = recparams ;
      htparams = ( & mulparams ) ;

      /* Refer to the correct match dictionary for the particular entry of the
         multiple screen (e.g. RedFrequency etc). */
      match_basic += htinfo( htparams->nOriginalHalftoneType )->multiple_screens * nScreen ;

      /* Name the otherwise anonymous screen, choosing the CMYK system (rather than RGBGray) */
      htparams->nmOriginalColorKey = pcmCMYKNames[ nScreen ];

    } else if (poKeyOfType5Subordinate != NULL) {

      /* Since it was from a type 5 dictionary we know the name of the
         color which this screen is to be applied to */
      htparams->nmOriginalColorKey = oName (*poKeyOfType5Subordinate);

    } else {

      /* The anonymous screen is christened "Black", unless we are
         working in DeviceGray, when it is called "Gray" */
      DEVICESPACEID deviceSpace;
      int32 nColorants; /* unused after the call to guc_deviceColorSpace */

      htparams->nmOriginalColorKey = system_names + NAME_Black;
      guc_deviceColorSpace (colorInfo->deviceRS, & deviceSpace, & nColorants);
      if (deviceSpace == DEVICESPACE_Gray)
        htparams->nmOriginalColorKey = system_names + NAME_Gray;
    }

    /* Having taken a local copy of htparams for multiple screens where
       needed, find out the colorant index of the color (possibly named
       from an anonymous call, such as setscreen). If we don't know of
       this color, this call will reserve an index for it, in case we
       encounter it again in the future as a colorant to be output -
       dynamically added separations, for instance */
    if (!guc_colorantIndexPossiblyNewName (colorInfo->deviceRS,
                                           htparams->nmOriginalColorKey,
                                           &htparams->colorantIndex))
      return FALSE;

    /* By now we have:
     *  (a) A HalftoneType for the halftone being done.
     *  (b) Possibly a HalftoneName for the halftone being done.
     *  (c) If a complementary Type5, then a Color name for the halftone used.
     *  (d) If overriding, then both a HalftoneType & HalftoneName for the override.
     *  (e) If not a multiple screen, then a Color name for the halftone being done.
     *  (f) If a multiple screen, then a Color name for the halftone being done.
     */

    /* Three cases to consider:
     *  (A) sethalftone type screen with an override. Need to check original and do override.
     *  (B) sethalftone type screen with no override. Perhaps need to detect separation.
     *  (C) set[color]screen type screen with an override. Need to do only override.
     */

    if ( screentype == ST_SETHALFTONE ) {
      if ( overridescreen ) {
        /* Case (A) */

        /* We've done a set[color]screen with a halftone dictionary or a
         * sethalftone, and in either case with an override. In the case
         * of a set[color]screen, we want to color detect on the angle;
         * see below comment for Case (C). In the case of a sethalftone,
         * we want to color detect on the Angle entry. So, essentially
         * we want to color detect on the results of the Freq/Angle
         * returned by dosetht_check_screen.  */

        htparams->nUseHalftoneType  = htparams->nOriginalHalftoneType ;
        htparams->nmUseHalftoneName  = htparams->nmOriginalHalftoneName ;
        if ( htparams->nmUseColorKey == NULL )
          htparams->nmUseColorKey = htparams->nmOriginalColorKey ;

        if ( ! dosetht_check_screen( match_basic , htparams ,
                                     freqv , anglv , &sh_dotshape ,
                                     colorPageParams ))
          return FALSE ;

        if ( checkargs ) {

          if ( ! dosetht_color_detection( colorInfo, htparams,
                                          poKeyOfType5Subordinate != NULL,
                                          freqv, anglv, & sh_dotshape ))
            return FALSE ;

        } else {

          Bool fIgnoreColorant ;
          SYSTEMVALUE *lfreq = NULL , *langl = NULL ;
          SYSTEMVALUE lfreqv[ 4 ] , langlv[ 4 ] ;
          NAMETYPEMATCH *match1_extra ;

          if ( ! dosetht_add_separations( page , htmatch , & fIgnoreColorant ,
                                          poKeyOfType5Subordinate , colorInfo , htparams ))
            return FALSE ;

          match1_extra = htmatch[ HT_MATCH_EXTRA ] ;
          if ( ! dosetht_store_data( thedict,
                                     match_basic, match1_extra, htparams,
                                     freqv , anglv , &sh_dotshape,
                                     colorPageParams))
            return FALSE ;

          if ( fIgnoreColorant )
            return TRUE ;

          /* Then override with override screen. */
          if ( ! dosetht_get_overridescreen( htmatch , & match_basic , poOverrideHalftoneDict ,
                                             colorInfo , htparams ))
            return FALSE ;

          if ( freqv && anglv ) {
            lfreq =  freqv ;
            langl = anglv ;
          } else if ( sh_dotshape.sh_type == SH_TYPE_SPOTFN ) {
            SYSTEMVALUE freq = sh_dotshape.sh_bits.sh_spotfn.freq ;
            SYSTEMVALUE angl = sh_dotshape.sh_bits.sh_spotfn.angl ;
            for ( i = 0 ; i < 4 ; ++i ) {
              lfreqv[ i ] = freq ; langlv[ i ] = angl ;
            }
            lfreq = lfreqv ;
            langl = langlv ;
          }

          if ( ! dosetht_check_screen( match_basic , htparams ,
                                       lfreq , langl , &sh_dotshape ,
                                       colorPageParams))
            return FALSE ;

          if ( ! dosetht_exec_screens( context, htmatch , match_basic , spotno ,
                                       colorInfo , htparams , & sh_dotshape ))
            return FALSE ;

          if ( ! dosetht_store_caldata( htmatch , colorInfo , htparams,
                                        poKeyOfType5Subordinate != NULL ))
            return FALSE ;
        }

      } else {

        /* Case (B) */

        /* We've done a set[color]screen with a simple halftone dict
           (not type 5), or a sethalftone (with no override in either
           case).  For set[color]screen, we want to color detect on the
           angle; see below comment for Case (C). In the case of a
           sethalftone, we want to color detect on the sethalftone
           angle. So, essentially we want to color detect on the results
           of the Freq/Angle returned by dosetht_check_screen.  */

        if ( checkargs ) {

          htparams->nUseHalftoneType  = htparams->nOriginalHalftoneType ;
          htparams->nmUseHalftoneName  = htparams->nmOriginalHalftoneName ;
          if ( ! htparams->nmUseColorKey )
            htparams->nmUseColorKey = htparams->nmOriginalColorKey ;

        } else {

          Bool fDetectSeparation = FALSE ;
          { /* factor in separation detection */
            int32 nColorants ;
            DEVICESPACEID deviceSpace ;
            NAMECACHE *nmSepColor = get_separation_name(FALSE) ;
            NAMECACHE *nmColorKey = htparams->nmOriginalColorKey ;

            guc_deviceColorSpace( colorInfo->deviceRS , & deviceSpace , & nColorants ) ;

            if ( interceptSeparation( nmColorKey, nmSepColor, deviceSpace )) {
              OBJECT *htdict = htparams->htdict_main ;
              HQASSERT( htdict , "htdict_main unexpectedly NULL in htparams" ) ;

              fDetectSeparation = TRUE ;
              htparams->nmOverrideHalftoneName = NULL ;
              if ( ! dosetht_resolve199( &htdict, FALSE, htparams, HT_OVR ))
                return FALSE;

              if ( ! dosetht_get_overridescreen( htmatch , & match_basic , htdict ,
                                                 colorInfo , htparams ))
                return FALSE ;
            }
          }
          if ( ! fDetectSeparation ) {
            htparams->nUseHalftoneType  = htparams->nOriginalHalftoneType ;
            htparams->nmUseHalftoneName = htparams->nmOriginalHalftoneName ;
            if ( htparams->nmUseColorKey == NULL )
              htparams->nmUseColorKey = htparams->nmOriginalColorKey ;
          }
        }

        if ( ! dosetht_check_screen( match_basic , htparams ,
                                     freqv , anglv , &sh_dotshape ,
                                     colorPageParams ))
          return FALSE ;

        if ( checkargs ) {
          if ( ! dosetht_color_detection( colorInfo, htparams,
                                          poKeyOfType5Subordinate != NULL,
                                          freqv, anglv, & sh_dotshape ))
            return FALSE ;
        } else {

          Bool fIgnoreColorant ;
          NAMETYPEMATCH *match1_extra ;

          if ( ! dosetht_add_separations( page , htmatch , & fIgnoreColorant ,
                                          poKeyOfType5Subordinate , colorInfo , htparams ))
            return FALSE ;

          match1_extra = htmatch[ HT_MATCH_EXTRA ] ;
          if ( ! dosetht_store_data( thedict,
                                     match_basic, match1_extra, htparams,
                                     freqv , anglv , &sh_dotshape ,
                                     colorPageParams))
            return FALSE ;

          if ( fIgnoreColorant )
            return TRUE ;

          if ( ! dosetht_exec_screens( context, htmatch , match_basic , spotno ,
                                       colorInfo , htparams , & sh_dotshape ))
            return FALSE ;

          if ( ! dosetht_store_caldata( htmatch , colorInfo , htparams,
                                        poKeyOfType5Subordinate != NULL ))
            return FALSE ;
        }
      }

    } else {

      /* Case (C) */
      HQASSERT( overridescreen , "should always be overriding if not ST_SETHALFTONE" ) ;

      /* We are either doing a missing screen, or a set[color]screen. In
       * the case of the latter, we need to do color detection before
       * getting the override.  That's because the set[color]screen will
       * have come from something like "currentscreen exch pop 15 exch
       * setscreen" and the 15 tells us something. */
      if ( checkargs ) {
        if ( ! dosetht_color_detection( colorInfo , htparams ,
                                        poKeyOfType5Subordinate != NULL,
                                        freqv , anglv , NULL ))
          return FALSE ;

      } else {

        if ( ! dosetht_get_overridescreen( htmatch , & match_basic , poOverrideHalftoneDict ,
                                           colorInfo , htparams ))
          return FALSE ;

        if ( ! dosetht_check_screen( match_basic , htparams ,
                                     freqv , anglv , &sh_dotshape ,
                                     colorPageParams ))
          return FALSE ;

        if ( ! dosetht_exec_screens( context, htmatch , match_basic , spotno ,
                                     colorInfo , htparams , & sh_dotshape ))
          return FALSE ;
      }
    }
  }

  /* Having finished checking/installing all the screens, check if the Override will. */
  if ( isloverride ) {
    if ( loverride < gsloverride ) {
      halftoneoverride = TRUE ;
      return TRUE ;
    }
    colorInfo->halftoneInfo->halftoneoverride = loverride ;
  } else if ( gsloverride > 0 ) {
    halftoneoverride = TRUE ;
    return TRUE ;
  }

  /* Finished all checking, so can return if only checking args. */
  if ( checkargs )
    return TRUE ;

  /* Setup the missing implicit screens. */
  HQASSERT( htparams->nOriginalHalftoneType != 5 ,
            "halftone type 5's should be being dealt with in dosethalftone5" ) ;

  if ( poKeyOfType5Subordinate == NULL ) {

    /* The screen is not a subordinate of a type 5 (implicit screens
       for these are done once they have all been processed) */

    HQASSERT( sh_dotshape.sh_type == SH_TYPE_SPOTFN ||
              sh_dotshape.sh_type == SH_TYPE_THRESH ||
              sh_dotshape.sh_type == SH_TYPE_MODULAR ,
              "should have been a threshold, spotfunction or modular type" ) ;

    if ( sh_dotshape.sh_type == SH_TYPE_SPOTFN ) {
      if (! setup_implicit_screens( context, sh_dotshape.sh_bits.sh_spotfn.freq ,
                                    sh_dotshape.sh_bits.sh_spotfn.spotfn ,
                                    spotno , FALSE , htparams, colorInfo ))
         return FALSE ;
    } else {
      if (! setup_implicit_screens( context, 0.0 , NULL , spotno , FALSE , htparams , colorInfo ))
        return FALSE ;
    }
  }

  /* Deal with any DCS entries. */
  if ( 0 == htinfo( htparams->nOriginalHalftoneType )->multiple_screens ) {
    NAMETYPEMATCH *match_separation = htmatch[ HT_MATCH_SEPARATION ] ;
    if ( ! dosetht_exec_dcs( match_separation , poKeyOfType5Subordinate ))
      return FALSE ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
#define SETSCREENENTRIES      (  3+1)
#define SETCOLORSCREENENTRIES (4*3+1)

Bool gsc_currenthalftones ( GS_COLORinfo *colorInfo, STACK *stack )
{
  corecontext_t *context = get_core_context_interp();
  HALFTONE *halftones;
  int32   i;
  Bool    glmode ;
  Bool    result ;
  OBJECT *theo ;
  OBJECT  thed = OBJECT_NOTVM_NOTHING ;
  COLOR_PAGE_PARAMS *colorPageParams = &context->page->colorPageParams;

  /* If there isn't one, we are required to "fabricate" one according to the
   * red book 2 defintion of the operator.   Similar to Adobe, we generate a
   * Type1 halftone for setscreen, and a Type2 halftone for setcolorscreen.
   */
  switch ( colorInfo->halftoneInfo->screentype ) {
  case ST_SETHALFTONE :
    return push (&colorInfo->halftoneInfo->halftonedict, stack);

  case ST_SETSCREEN :
  case ST_SETPATTERN :
    theo = & colorInfo->halftoneInfo->halftones[ 3 ].spotfn ;

    /* Use global mode if in global mode and spot function is global */
    glmode = context->glallocmode && oGlobalValue(*theo);
    glmode = setglallocmode(context, glmode);
    result = ps_dictionary(&thed, SETSCREENENTRIES);
    setglallocmode(context, glmode);
    if ( ! result )
      return FALSE ;

    oName( nnewobj ) = system_names + NAME_HalftoneType ;
    oInteger( inewobj ) = 1 ;
    if ( ! fast_insert_hash( & thed , & nnewobj , & inewobj ))
      return FALSE ;

    oName( nnewobj ) = system_names + NAME_Frequency ;
    oReal( rnewobj ) = colorInfo->halftoneInfo->halftones[ 3 ].freq ;
    if ( ! fast_insert_hash( & thed , & nnewobj , & rnewobj ))
      return FALSE ;

    oName( nnewobj ) = system_names + NAME_Angle ;
    oReal( rnewobj ) = colorInfo->halftoneInfo->halftones[ 3 ].angl ;
    if ( ! fast_insert_hash( & thed , & nnewobj , & rnewobj ))
      return FALSE ;

    oName( nnewobj ) = system_names + NAME_SpotFunction ;
    if ( ! fast_insert_hash( & thed , & nnewobj , theo ))
      return FALSE ;

    if (colorPageParams->adobeCurrentHalftone) {
      /* you're not going to like this:
       *  what Adobe do as a side effect is to install this fabricated dictionary
       *  in the graphics state as the current screen.
       */
      if ( ! cc_updatehalftoneinfo( &colorInfo->halftoneInfo ) ||
           ! cc_invalidateColorChains( colorInfo, TRUE ))
        return FALSE ;
      Copy (& colorInfo->halftoneInfo->halftonedict, & thed);
      colorInfo->halftoneInfo->screentype = ST_SETHALFTONE;
      colorInfo->halftoneInfo->halftoneid = ++HalftoneId;
    }

    return push (&thed, stack);

  case ST_SETCOLORSCREEN :
  case ST_SETCOLORPATTERN :
    /* Use global mode if in global mode and all spot functions are global */
    halftones = colorInfo->halftoneInfo->halftones;
    glmode = context->glallocmode &&
      oGlobalValue(halftones[0].spotfn) &&
      oGlobalValue(halftones[1].spotfn) &&
      oGlobalValue(halftones[2].spotfn) &&
      oGlobalValue(halftones[3].spotfn);
    glmode = setglallocmode(context, glmode);
    result = ps_dictionary(&thed, SETCOLORSCREENENTRIES);
    setglallocmode(context, glmode);
    if ( ! result )
      return FALSE ;

    oName( nnewobj ) = system_names + NAME_HalftoneType ;
    oInteger( inewobj ) = 2 ;
    if ( ! fast_insert_hash( & thed , & nnewobj , & inewobj ))
      return FALSE ;

    for ( i = 0 ; i < 4 ; ++i ) {
      int32 stepsize = htinfo( 2 )->multiple_screens ;
      oName( nnewobj ) = theIMName( & htmatch2_basic[0+stepsize*i] ) ;
      oReal( rnewobj ) = colorInfo->halftoneInfo->halftones[ i ].freq ;
      if ( ! fast_insert_hash( & thed , & nnewobj , & rnewobj ))
        return FALSE ;

      oName( nnewobj ) = theIMName( & htmatch2_basic[1+stepsize*i] ) ;
      oReal( rnewobj ) = colorInfo->halftoneInfo->halftones[ i ].angl ;
      if ( ! fast_insert_hash( & thed , & nnewobj , & rnewobj ))
        return FALSE ;

      oName( nnewobj ) = theIMName( & htmatch2_basic[2+stepsize*i] ) ;
      theo = &colorInfo->halftoneInfo->halftones[ i ].spotfn ;
      if ( ! fast_insert_hash( & thed , & nnewobj , theo ))
        return FALSE ;
    }

    if (colorPageParams->adobeCurrentHalftone) {
      /* you're not going to like this:
       *  what Adobe do as a side effect is to install this fabricated dictionary
       *  in the graphics state as the current screen.
       */
      if ( ! cc_updatehalftoneinfo( &colorInfo->halftoneInfo ) ||
           ! cc_invalidateColorChains( colorInfo, TRUE ))
        return FALSE ;
      Copy (&colorInfo->halftoneInfo->halftonedict, & thed);
      colorInfo->halftoneInfo->screentype = ST_SETHALFTONE;
      colorInfo->halftoneInfo->halftoneid = ++HalftoneId;
    }

    return push (&thed, stack);

  default:
    HQFAIL ("unrecognized screen type in currenthalftone");
  }
  return FALSE;
}

Bool gsc_redo_setscreen( GS_COLORinfo *colorInfo )
{
  corecontext_t *context = get_core_context_interp();

  switch ( colorInfo->halftoneInfo->screentype ) {
  case ST_SETSCREEN:
  case ST_SETPATTERN:
    return (gsc_currentscreens( context, colorInfo, &operandstack, 3 , 3 ) &&
            gsc_setscreens( colorInfo, &operandstack, ST_SETSCREEN )) ;
  case ST_SETCOLORSCREEN:
  case ST_SETCOLORPATTERN:
    return (gsc_currentscreens( context, colorInfo, &operandstack, 0 , 3 )
            && gsc_setscreens(colorInfo, &operandstack, ST_SETCOLORSCREEN )) ;
  case ST_SETHALFTONE:
    return (gsc_currenthalftones( colorInfo, &operandstack ) &&
            gsc_setscreens( colorInfo, &operandstack , ST_SETHALFTONE )) ;
  default:
    HQFAIL( "unknown screen type in gsc_redo_setscreen" ) ;
    return error_handler( UNREGISTERED ) ;
  }
}

uint8 gsc_regeneratescreen( GS_COLORinfo *colorInfo )
{
  return colorInfo->halftoneInfo->regeneratescreen ;
}


void gsc_invalidate_one_gstate_screens( GS_COLORinfo *colorInfo , uint8 regenscreen )
{
  HQASSERT( colorInfo , "colorInfo is null in gsc_invalidate_one_gstate_screens" ) ;

  colorInfo->halftoneInfo->regeneratescreen = regenscreen ;
}

/* ---------------------------------------------------------------------- */
/** An operator to go from spot function (PostScript procedure) to spot
 function number (index into switchscreens array (name may change!)
 in printerdict, defined in edpdpss.pss). Pops its argument (the
 procedure) and returns true and a number if found, or just false if
 not found.
 */

Bool findspotfunctionnumber_(ps_context_t *pscontext)
{
  OBJECT *theo;
  int32 number;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW) ;

  theo = theTop(operandstack) ;
  switch (oType(*theo)) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler(TYPECHECK) ;
  }

  if (!oCanRead(*theo))
    if (!object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

  number = findCSpotFunction(theo) ;

  if (number < 0) {
    Copy(theo, &fnewobj);
    return TRUE;
  } else {
    theTags(*theo) = OINTEGER | LITERAL;
    oInteger(*theo) = number;
    return push(&tnewobj, &operandstack);
  }
}

/** An operator to go from spot function (PostScript procedure) to spot
 function name (index into switchscreens dictionary (number now redundant!)
 in printerdict, defined in edpdpss.pss). Pops its argument (the
 procedure) and returns true and a name if found, or just false if
 not found.
 */

Bool findspotfunctionname_(ps_context_t *pscontext)
{
  OBJECT *theo;
  NAMECACHE *name = NULL ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW) ;

  theo = theTop(operandstack) ;
  switch (oType(*theo)) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler(TYPECHECK) ;
  }

  if (!oCanRead(*theo))
    if (!object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

  name = findSpotFunctionName(theo) ;

  if (name) {
    theTags(*theo) = ONAME | LITERAL ;
    oName(*theo) = name;
    return push(&tnewobj, &operandstack);
  } else {
    Copy(theo, &fnewobj);
    return TRUE;
  }
}

/* Log stripped */
