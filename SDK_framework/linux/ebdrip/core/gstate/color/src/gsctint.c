/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsctint.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The tint transform color link.
 */

#include "core.h"

#include "control.h"            /* interpreter */
#include "chartype.h"           /* remap_bin_token_chars */
#include "fileio.h"             /* FILELIST */
#include "functns.h"            /* FN_TINT_TFM */
#include "gcscan.h"             /* ps_scan_field */
#include "graphics.h"           /* struct gstate */
#include "gstack.h"             /* gstateptr */
#include "gu_chan.h"            /* guc_getColorantMapping */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "mm.h"                 /* mm_alloc */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* oType */
#include "params.h"             /* UserParams */
#include "psvm.h"               /* workingsave */
#include "spdetect.h"           /* detect_setcolor_separation */
#include "stacks.h"             /* stack_push_real - *** Remove this *** */
#include "swerrors.h"           /* TYPECHECK */
#include "miscops.h"            /* bind_automatically */

#include "gs_callps.h"          /* CALLPSCACHE */
#include "gs_colorpriv.h"       /* CLINK */
#include "gscdevcipriv.h"       /* OP_DISABLED */
#include "gscdevcn.h"           /* IDEVN_POSN_UNKNOWN */
#include "gschcmspriv.h"        /* cc_getNamedColorIntercept */
#include "gscheadpriv.h"        /* GS_CHAINinfo */
#include "gscsmpxformpriv.h"    /* cc_csaGetSimpleTransform */

#include "gsctintpriv.h"

/* ---------------------------------------------------------------------- */

/* We need to identify all the unique characteristics that identify a CLINK.
 * The CLINK type, colorspace, colorant set are orthogonal to these items.
 * The device color space & colorants are defined as fixed.
 * The following define the number of id slots needed by the various CLINK
 * types in this file.
 */

#define CLID_SIZEtinttransform        (2)
#define CLID_SIZEallseptinttransform  (1)

#define CLID_TINT_SIMPLE_TRANSFORM    ((CLID) 0)
#define CLID_TINT_NONE                ((CLID) 1)
#define CLID_TINT_NORMAL              ((CLID) 2)
#define CLID_TINT_EMPTY_PROC          ((CLID) 3)

/*
 * Tint Transform
 * ==============
 */

struct CLINKtinttransform {
  GS_TINT_STATE *tintState;

  OBJECT      alternativespaceObject;
  OBJECT      tinttransformObject;
  int32       nOutputColorants;
  Bool        isCIEalternativespace;
  int32       tintTransformId;
  Bool        fIsEmptyProcedure;    /* determined at construction time from
                                       tinttransformObject above, to save
                                       analysing it repeatedly */
  GSC_SIMPLE_TRANSFORM *simpleTransform;

  Bool        fCompositing;
  Bool        fMappedColorants;
  CALLPSCACHE *cpsc;                /* cache of PS call results */
  uint32     *nMapped;              /* [n_iColorants] */
  int32      *sortedOrder;          /* [nOutputColorants] */
};


typedef struct CALLPS_TT_CACHE CALLPS_TT_CACHE;


struct GS_TINT_STATE {
  int32 TintTransformId;
  CALLPS_TT_CACHE *callps_tt_cache;
};


static void  tinttransform_destroy(CLINK *pLink);
static Bool tinttransform_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#if 0
static Bool tinttransform_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* 0 */
static mps_res_t tinttransform_scan( mps_ss_t ss, CLINK *pLink );

#if defined( ASSERT_BUILD )
static void tinttransformAssertions(CLINK *pLink);
#else
#define tinttransformAssertions(_pLink) EMPTY_STATEMENT()
#endif
static uint32 tinttransformStructSize(int32 nColorants, int32 nOutputColorants);
static void tinttransformUpdatePtrs(CLINK *pLink);

static Bool convert_to_alternate_colorspace(CLINK *pLink, USERVALUE *oColorValues);

static Bool determineAlternativeSpace(GS_COLORinfo *colorInfo,              /* I */
                                      OBJECT       *colorSpaceObject,       /* I */
                                      Bool         allowNamedColorIntercept,/* I */
                                      Bool         fCompositing,            /* I */
                                      OBJECT       *alternativespace,       /* O */
                                      OBJECT       *tinttransform,          /* O */
                                      int32        *tinttransformId,        /* O */
                                      Bool         *fgotIntercept);         /* O */


static CLINKfunctions CLINKtinttransform_functions =
{
    tinttransform_destroy,
    tinttransform_invokeSingle,
    NULL /* tinttransform_invokeBlock */,
    tinttransform_scan
};


/* ---------------------------------------------------------------------- */

/**
 * Static list of all the cached tint values for intercepted NamedColor PS
 * functions. These are created  early to avoid the need to call the
 * interpreter during compositing.
 */
struct CALLPS_TT_CACHE {
  cc_counter_t    refCnt;
  CALLPSCACHE     *cpsc;
  int32           id;
  CALLPS_TT_CACHE *next;
};


/**
 * Create a cache entry all the color values of the given tint function.
 * Called for tint functions generated by NamedColor interception, to avoid
 * PS interpreter calls during compositing.
 */
static Bool add_tint_callpscache(GS_TINT_STATE *tintState,
                                 CLINKtinttransform *tt)
{
  CALLPS_TT_CACHE *ttcache;

  /* Don't use this store in the front end because the color cache provides
   * enough caching for performance, and this store can be a CPU hog where only
   * a small number of colors are invoked in many chains.
   */
  if (tintState == frontEndColorState->tintState)
    return TRUE;

  for ( ttcache = tintState->callps_tt_cache; ttcache != NULL;
        ttcache = ttcache->next ) {
    if ( ttcache->id == tt->tintTransformId ) {
      CLINK_RESERVE(ttcache);
      tt->cpsc = ttcache->cpsc;
      return TRUE;
    }
  }
  ttcache = mm_alloc(mm_pool_color, sizeof(CALLPS_TT_CACHE),
                     MM_ALLOC_CLASS_NCOLOR);
  if ( ttcache == NULL )
    return error_handler(VMERROR);
  ttcache->refCnt = 1;
  ttcache->id = tt->tintTransformId;
  ttcache->cpsc = create_callpscache(FN_TINT_TFM, tt->nOutputColorants,
                                     ttcache->id, NULL,
                                     &(tt->tinttransformObject));
  if ( ttcache->cpsc == NULL ) {
    mm_free(mm_pool_color, ttcache, sizeof(CALLPS_TT_CACHE));
    return error_handler(VMERROR);
  }
  ttcache->next = tintState->callps_tt_cache;
  tintState->callps_tt_cache = ttcache;

  CLINK_RESERVE(ttcache);
  tt->cpsc = ttcache->cpsc;

  return TRUE;
}

static void rm_tint_callpscache(GS_TINT_STATE *tintState, int32 id)
{
  CALLPS_TT_CACHE *ttcache, *prev = NULL, *next;

  for ( ttcache = tintState->callps_tt_cache; ttcache != NULL;
        ttcache = next ) {
    next = ttcache->next;
    if ( ttcache->id == id ) {
      /** \todo not thread safe, but the list is per COLOR_STATE */
      if ( --(ttcache->refCnt) == 0 ) {
        destroy_callpscache(&ttcache->cpsc);
        if ( prev )
          prev->next = ttcache->next;
        else
          tintState->callps_tt_cache = ttcache->next;
        mm_free(mm_pool_color, ttcache, sizeof(CALLPS_TT_CACHE));
      }
    }
    prev = ttcache;
  }
}

/* ---------------------------------------------------------------------- */

Bool cc_tintStateCreate(GS_TINT_STATE **tintStateRef)
{
  GS_TINT_STATE *tintState;

  HQASSERT(*tintStateRef == NULL, "tintState already exists");

  tintState = mm_alloc(mm_pool_color, sizeof(GS_TINT_STATE),
                       MM_ALLOC_CLASS_NCOLOR);
  if ( tintState == NULL )
    return error_handler(VMERROR);

  tintState->TintTransformId = 0;
  tintState->callps_tt_cache = NULL;

  *tintStateRef = tintState;
  return TRUE;

}

void cc_tintStateDestroy(GS_TINT_STATE **tintStateRef)
{
  if ( *tintStateRef != NULL ) {
    GS_TINT_STATE *tintState = *tintStateRef;
    CALLPS_TT_CACHE *ttcache;
    CALLPS_TT_CACHE *prevcache;

    /* At the back end, the desire is that this cache becomes a store, so the
     * tintState will hold a refCnt, as well as the color chains that use it.
     */
    if (tintState != frontEndColorState->tintState) {
      for (ttcache = tintState->callps_tt_cache; ttcache != NULL; ttcache = prevcache) {
        prevcache = ttcache->next;
        rm_tint_callpscache(tintState, ttcache->id);
      }
    }

    HQASSERT(tintState->callps_tt_cache == NULL, "tintState memory leak");

    mm_free(mm_pool_color, tintState, sizeof(GS_TINT_STATE));
    *tintStateRef = NULL;
  }
}

/* ---------------------------------------------------------------------- */

/*
 * Tint Transform Link Data Access Functions
 * =========================================
 */

CLINK *cc_tinttransform_create(int32                nColorants,
                               COLORANTINDEX        *colorants,
                               COLORSPACE_ID        colorSpaceId,
                               GS_COLORinfo         *colorInfo,
                               OBJECT               *colorSpaceObject,
                               Bool                 fCompositing,
                               OBJECT               **alternativeSpaceObject,
                               Bool                 allowNamedColorIntercept,
                               Bool                 *pf_allowColorManagement)
{
  CLINK           *pLink ;
  int32           nOutputColorants;
  COLORSPACE_ID   tempAlternativespaceId;
  OBJECT          tempAlternativespaceObject = OBJECT_NOTVM_NOTHING;
  OBJECT          tempTinttransformObject = OBJECT_NOTVM_NOTHING;
  int32           tintTransformId = 0;
  Bool            fgotIntercept = FALSE;
  GSC_SIMPLE_TRANSFORM  *simpleTransform;

  HQASSERT(pf_allowColorManagement != NULL, "pf_allowColorManagement NULL");


  /* Discover if the tint transform is a simple tint tranform */
  simpleTransform = cc_csaGetSimpleTransform(colorSpaceObject);

  /* We need to know nOutputColorants before we create the link so the it's size
   * can be specified.
   */
  if (!determineAlternativeSpace(colorInfo,
                                 colorSpaceObject,
                                 allowNamedColorIntercept,
                                 fCompositing,
                                 &tempAlternativespaceObject,
                                 &tempTinttransformObject,
                                 &tintTransformId,
                                 &fgotIntercept))
    return NULL;

  if (!gsc_getcolorspacesizeandtype(colorInfo,
                                    &tempAlternativespaceObject,
                                    &tempAlternativespaceId,
                                    &nOutputColorants ))
    return NULL;

  if (fgotIntercept) {
    /* Don't install a simple tint transform if we're intercepting this space */
    simpleTransform = NULL;

    /* This is a hack to prevent DeviceN named color databases from being CMYK
     * color managed in another loop around chain construction. This could
     * happen if a DeviceN device had some of its colorants mapped onto CMYK.
     */
    switch (tempAlternativespaceId) {
    case SPACE_DeviceN:
    case SPACE_Separation:
      *pf_allowColorManagement = FALSE;
      break;
    }
  }

  pLink = cc_common_create(nColorants,
                           colorants,
                           colorSpaceId,
                           tempAlternativespaceId,
                           CL_TYPEtinttransform,
                           tinttransformStructSize(nColorants, nOutputColorants),
                           &CLINKtinttransform_functions,
                           CLID_SIZEtinttransform);

  if (pLink == NULL)
    return NULL;

  tinttransformUpdatePtrs(pLink);


  pLink->p.tinttransform->tintState = colorInfo->colorState->tintState;

  Copy(object_slot_notvm(&pLink->p.tinttransform->alternativespaceObject),
       &tempAlternativespaceObject);
  Copy(object_slot_notvm(&pLink->p.tinttransform->tinttransformObject),
       &tempTinttransformObject);

  pLink->p.tinttransform->nOutputColorants = nOutputColorants;

  if (ColorspaceIsCIEBased(tempAlternativespaceId))
    pLink->p.tinttransform->isCIEalternativespace = TRUE;
  else
    pLink->p.tinttransform->isCIEalternativespace = FALSE;

  pLink->p.tinttransform->tintTransformId = tintTransformId;

  pLink->p.tinttransform->fIsEmptyProcedure =
    (oType(tempTinttransformObject) == OARRAY ||
     oType(tempTinttransformObject) == OPACKEDARRAY) &&
    theLen (tempTinttransformObject) == 0;

  pLink->p.tinttransform->simpleTransform = simpleTransform;

  pLink->p.tinttransform->fCompositing = fCompositing;
  pLink->p.tinttransform->fMappedColorants = FALSE;

  *alternativeSpaceObject = &pLink->p.tinttransform->alternativespaceObject;

  pLink->p.tinttransform->cpsc = NULL;
  if ( fgotIntercept )
    if ( !add_tint_callpscache(pLink->p.tinttransform->tintState,
                               pLink->p.tinttransform) )
      return NULL;

  /* CLID calculation:
     Make special cases of a)simple tint transform, b) the None Sep,
     and c) empty procedures. In the normal case, we increment a variable on
     each create.
   */
  {
    CLID *idslot = pLink->idslot ;

    if ( pLink->p.tinttransform->simpleTransform != NULL ) {
      idslot[ 0 ] = CLID_TINT_SIMPLE_TRANSFORM;
      idslot[ 1 ] = (CLID)cc_spacecache_id(pLink->p.tinttransform->simpleTransform);
    }
    else if (( pLink->iColorants[ 0 ] == COLORANTINDEX_NONE ) &&
             ( pLink->n_iColorants == 1 )) {
      idslot[ 0 ] = CLID_TINT_NONE;
      idslot[ 1 ] = (CLID)0;
    }
    else if (pLink->p.tinttransform->fIsEmptyProcedure) {
      idslot[ 0 ] = CLID_TINT_EMPTY_PROC;
      idslot[ 1 ] = (CLID)0;
    }
    else {
      idslot[ 0 ] = CLID_TINT_NORMAL;
      idslot[ 1 ] = pLink->p.tinttransform->tintTransformId;
    }
  }

  tinttransformAssertions(pLink);

  return pLink;
}

static void tinttransform_destroy(CLINK *pLink)
{
  tinttransformAssertions(pLink);

  rm_tint_callpscache(pLink->p.tinttransform->tintState,
                      pLink->p.tinttransform->tintTransformId);
  cc_common_destroy(pLink);
}

static Bool tinttransform_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  tinttransformAssertions( pLink ) ;
  HQASSERT( oColorValues != NULL , "oColorValues == NULL" ) ;

  /* If at the front end, we consider a request to paint on any separation as a
   * color job.
   * If at the back end, we don't apply separation detection, it's not allowed.
   */
  if (!pLink->p.tinttransform->fCompositing && new_color_detected) {
    if (!detect_setcolor_separation())
      return FALSE;
  }
  return convert_to_alternate_colorspace( pLink , oColorValues ) ;
}

#if 0
/* commented out to suppress compiler warning: no longer used */
static Bool tinttransform_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  tinttransformAssertions(pLink);

  return TRUE;
}
#endif


/* tinttransform_scan - scan the tinttransform part of a CLINK */
static mps_res_t tinttransform_scan( mps_ss_t ss, CLINK *pLink )
{
  mps_res_t res;

  res = ps_scan_field( ss, &pLink->p.tinttransform->alternativespaceObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &pLink->p.tinttransform->tinttransformObject );
  return res;
}


static uint32 tinttransformStructSize(int32 nColorants, int32 nOutputColorants)
{
  return sizeof(CLINKtinttransform) +
         nColorants       * sizeof(int32) +             /* nMapped */
         nOutputColorants * sizeof(int32);              /* sortedOrder */
}

static void tinttransformUpdatePtrs(CLINK *pLink)
{
  pLink->p.tinttransform = (CLINKtinttransform *) ((uint8 *)pLink + cc_commonStructSize(pLink));
  pLink->p.tinttransform->nMapped     = (uint32 *)(pLink->p.tinttransform + 1);
  pLink->p.tinttransform->sortedOrder = (int32 *)(pLink->p.tinttransform->nMapped + pLink->n_iColorants);
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void tinttransformAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEtinttransform,
                      tinttransformStructSize(pLink->n_iColorants,
                                              pLink->p.tinttransform->nOutputColorants),
                      &CLINKtinttransform_functions);

  HQASSERT(pLink->p.tinttransform == (CLINKtinttransform *) ((uint8 *)pLink + cc_commonStructSize(pLink)),
           "tinttransform data not set");
  HQASSERT(pLink->p.tinttransform->nMapped      == (uint32 *)(pLink->p.tinttransform + 1) ,
           "nMapped not set" ) ;
  HQASSERT(pLink->p.tinttransform->sortedOrder  == (int32 *)(pLink->p.tinttransform->nMapped + pLink->n_iColorants) ,
           "sortedOrder not set" ) ;
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * All Separation Tint Transform
 * =============================
 */

struct CLINKallseptinttransform {
  int32           nOutputColorants;
  int32           convertAllSeparation;
};


static void allseptinttransform_destroy(CLINK *pLink);
static Bool allseptinttransform_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#if 0
static Bool allseptinttransform_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* 0 */

#if defined( ASSERT_BUILD )
static void allseptinttransformAssertions(CLINK *pLink);
#else
#define allseptinttransformAssertions(_pLink) EMPTY_STATEMENT()
#endif

static uint32 allseptinttransformStructSize(void);
static void allseptinttransformUpdatePtrs(CLINK *pLink);

static CLINKfunctions CLINKallseptinttransform_functions =
{
    allseptinttransform_destroy,
    allseptinttransform_invokeSingle,
    NULL /* allseptinttransform_invokeBlock */,
    NULL
};

/* ---------------------------------------------------------------------- */

/*
 * All Separation Tint Transform Link Data Access Functions
 * ========================================================
 */

CLINK *cc_allseptinttransform_create(int32              nColorants,
                                     COLORANTINDEX      *colorants,
                                     COLORSPACE_ID      colorSpaceId,
                                     int32              nOutputColorants,
                                     int32              convertAllSeparation)
{
  CLINK *pLink;

  pLink = cc_common_create(nColorants,
                           colorants,
                           SPACE_Separation,
                           colorSpaceId,
                           CL_TYPEallseptinttransform,
                           allseptinttransformStructSize(),
                           &CLINKallseptinttransform_functions,
                           CLID_SIZEallseptinttransform);

  if (pLink == NULL)
    return NULL;

  allseptinttransformUpdatePtrs(pLink);
  pLink->p.allseptinttransform->nOutputColorants = nOutputColorants;
  pLink->p.allseptinttransform->convertAllSeparation = convertAllSeparation;

  pLink->idslot[0] = convertAllSeparation;

  allseptinttransformAssertions(pLink);

  return pLink;
}

static void allseptinttransform_destroy(CLINK *pLink)
{
  allseptinttransformAssertions(pLink);

  cc_common_destroy(pLink);
}

static Bool allseptinttransform_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;
  int32 ondims ;
  USERVALUE colorValue ;
  CLINKallseptinttransform *allseptinttransform = pLink->p.allseptinttransform;

  allseptinttransformAssertions(pLink);
  HQASSERT( oColorValues != NULL, "oColorValues == NULL");
  HQASSERT( pLink->pnext->n_iColorants == allseptinttransform->nOutputColorants,
              "Wrong number of colrants for /All separation");

  colorValue = pLink->iColorValues[ 0 ] ;
  COLOR_01_ASSERT( colorValue , "all sep tint transform input" ) ;

  ondims = allseptinttransform->nOutputColorants ;
  for (i = 0; i < ondims; ++i)
    oColorValues[i] = colorValue;

  if (allseptinttransform->convertAllSeparation == GSC_CONVERTALLSEPARATION_BLACK) {
    HQASSERT(allseptinttransform->nOutputColorants == 4 &&
             pLink->oColorSpace == SPACE_DeviceCMYK,
             "Unexpected output space for GSC_CONVERTALLSEPARATION_BLACK");
    /* If converting to black only, the easiest way is to convert to CMYK but
     * zero the first 3 components. That avoids the need to manufacture a Separation
     * Black color space.
     */
    for (i = 0; i < 3; ++i)
      oColorValues[i] = 0.0f;
  }

  /* Always turn off overprints for the All Separation */
  pLink->overprintProcess |= OP_DISABLED;

  return TRUE ;
}

#if 0
/* commented out to suppress compiler warning: no longer used */
static Bool allseptinttransform_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  allseptinttransformAssertions(pLink);

  return TRUE;
}
#endif

static uint32 allseptinttransformStructSize(void)
{
  return sizeof(CLINKallseptinttransform);
}

static void allseptinttransformUpdatePtrs(CLINK *pLink)
{
  pLink->p.allseptinttransform = (CLINKallseptinttransform *) ((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void allseptinttransformAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEallseptinttransform,
                      allseptinttransformStructSize(),
                      &CLINKallseptinttransform_functions);
}
#endif

/**
 * We have what looks like an ordinary tint transform, so apply it
 */
static Bool tt_apply(CLINK *pLink, int32 ondims, USERVALUE *oColorValues)
{
  CLINKtinttransform *tinttransform = pLink->p.tinttransform;
  int32 saved_gId, i;

  HQASSERT(tinttransform->tintState == frontEndColorState->tintState,
           "Tint transform should only be applied in the front end");

  saved_gId = gstateptr->gId;

  for (i = 0; i < pLink->n_iColorants; i++)
    if ( !stack_push_real((SYSTEMVALUE )pLink->iColorValues[i], &operandstack))
      return FALSE;
  if ( !push( &tinttransform->tinttransformObject, &executionstack) )
    return FALSE;
  if ( !interpreter(1, NULL) )
    return FALSE;

  if ( !stack_get_reals(&operandstack, oColorValues, ondims) )
    return FALSE;
  npop(ondims, &operandstack);

  /* If we find this condition, we have to give up because of the likelihood
     of the colorInfo structure being wiped away from our feet. */
  if ( gstateptr->gId != saved_gId )
    return detail_error_handler(UNDEFINED, "Unsafe use of gsave/grestore.");

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static Bool convert_to_alternate_colorspace(CLINK *pLink, USERVALUE *oColorValues)
{
  /* There are two possibilities with separations: either we go from
   * the separation straight to the tint, applying the appropriate transfer
   * function provided by sethalftone type 5; or we convert to process colors.
   * The decision of which to do is determined as follows:
   * o  are we separating? this is determined by the Separations flag
   *    in the setpagedevice dictionary (page 236).
   * o  if we are, do we want just process colors or do we want spot colors?
   *    This is determined by the presence or not of a type 5 halftone
   *    dictionary and its contents.
   * o  if there is a type 5 halftone dictionary, is the color space among
   *    those listed for output as a spot color. If it is, keep it as a
   *    spot color; otherwise convert ot process.
   *
   */
  int32               i;
  intptr_t            ondims;
  CLINKtinttransform  *tinttransform;

  tinttransform = pLink->p.tinttransform;

  for ( i = 0 ; i < pLink->n_iColorants ; i++ ) {
    NARROW_01(pLink->iColorValues[ i ]);
  }

  ondims = tinttransform->nOutputColorants ;

  /* If we have a special custom C routine for this node, use this: */
  if ( tinttransform->simpleTransform ) {
    if ( ! cc_invokeSimpleTransform( tinttransform->simpleTransform , pLink ,
                                     oColorValues ))
      return FALSE ;
  }
  else if (( oType(tinttransform->tinttransformObject) ==
             OFILE ) ||
           ( oType(tinttransform->tinttransformObject) ==
             ODICTIONARY )) {
    int32 fnindex = FN_TINT_TFM ;

    if ( tinttransform->isCIEalternativespace )
      fnindex = FN_CIE_TINT_TFM ;

    if ( ! fn_evaluate( & tinttransform->tinttransformObject ,
                        pLink->iColorValues ,
                        oColorValues , fnindex , 0 ,
                        tinttransform->tintTransformId, FN_GEN_NA ,
                        ( void * ) ondims ))
      return FALSE ;
  }
  else if ( tinttransform->fIsEmptyProcedure ) {
    /* Normally, just copy the color values */

    if ( ondims == pLink->n_iColorants ) {
      for ( i = 0 ; i < ondims ; ++i )
        oColorValues[ i ] = pLink->iColorValues[i];
    }
    else {
      /* Regrettably, some applications, notably Illustrator 7, do not provide a valid
         tint transform when they are "certain" that the tint transform will not be
         used - but they are wrong! We use it for Roam color. In this case they provide
         an empty procedure, so we need to handle this case so it at least does not
         fall over.

         So, what values to choose? 0 is easiest, and is black for RGB and Gray, but
         we'd do better to set K=1 in CMYK. Less usual spaces would be brought into
         range later on if necessary. However, because they may provide a real tint
         transform later on, we would rather actually return an invalid result, but not
         one that proiduces an error. As it happens, we do already have such a color in
         CMYK - (.5, .5, .5, 0). This is because Quark probes with this value and it is
         excepted, so set it to that in CMYK. */

      if ( pLink->oColorSpace == SPACE_DeviceCMYK ) {
        HQASSERT( ondims == 4 ,
                  "CMYK doesn't have 4 components!" ) ;
        oColorValues[ 0 ] = oColorValues[ 1 ] = oColorValues[ 2 ] = 0.5f ;
        oColorValues[ 3 ] = 0.0f ;
      }
      else {
        for ( i = 0 ; i < ondims ; ++i )
          oColorValues[ i ] = 0.0f ;
      }
    }
  }
  else {
    if ( pLink->p.tinttransform->cpsc != NULL ) {
      /* Apply a cached back end tint transform */
      HQASSERT(pLink->n_iColorants == 1, "Tint transform store can only have 1 input");
      HQASSERT(tinttransform->tintState != frontEndColorState->tintState,
               "Tint transform store shouldn't be used in the front end");
      lookup_callpscache(pLink->p.tinttransform->cpsc, pLink->iColorValues[0],
                         oColorValues);
    }
    else {
      /* Apply an ordinary front end tint transform */
      if ( !tt_apply(pLink, CAST_INTPTRT_TO_INT32(ondims), oColorValues) )
        return FALSE;
    }

    if (pLink->p.tinttransform->fMappedColorants) {
      uint32      *nMapped = pLink->p.tinttransform->nMapped;
      int32       *sortedOrder = pLink->p.tinttransform->sortedOrder;
      uint32      j;

      for (i = 0; i < pLink->n_iColorants; i++) {
        /* Transfer the transparent color value to all mapped output colorants */
        if (pLink->iColorValues[i] == 0 && (pLink->overprintProcess & ( 1 << i ))) {
          for (j = 0; j < nMapped[i]; j++)
            oColorValues[sortedOrder[j]] = -1;
        }
        sortedOrder += nMapped[i];
      }
    }
  }

  if (!pLink->p.tinttransform->fMappedColorants) {
    switch ( pLink->oColorSpace ) {
      case SPACE_DeviceN:
      case SPACE_Separation:
      case SPACE_DeviceGray:
      case SPACE_DeviceCMYK:
      case SPACE_DeviceRGB:
        for ( i = 0 ; i < ondims ; ++i )
          NARROW_01( oColorValues[ i ] ) ;
        break ;

      default:
        break ;
    }
  }
  else {
    HQASSERT(pLink->oColorSpace == SPACE_DeviceN, "Unexpected colorspace");
    for ( i = 0 ; i < ondims ; ++i )
      if (oColorValues[ i ] != -1)
        NARROW_01( oColorValues[ i ] ) ;
  }

  return TRUE ;
}

/* Does the colorSpaceObject contain any CMYK process names */
static Bool containsPocessColorant(GUCR_RASTERSTYLE *hRasterStyle,
                                   OBJECT           *namesObject,
                                   Bool             *containsProcess)
{
  COLORANTINDEX cmykIds[4];
  COLORANTINDEX ci;
  int numNames = 1;
  int j;
  Bool dummyMatch;
  OBJECT exclSeps = OBJECT_NOTVM_NULL;

  *containsProcess = FALSE;

  if (!guc_simpleDeviceColorSpaceMapping(hRasterStyle, DEVICESPACE_CMYK,
                                         cmykIds, 4))
    return FALSE;

  /* Get the set of colorant names from this namesObject */
  HQASSERT(numNames == cc_getNumNames(namesObject), "Should have one colorant");

  if (!cc_namesToIndex(hRasterStyle, namesObject, FALSE, FALSE,
                       &ci, numNames, &exclSeps, &dummyMatch))
    return FALSE;

  for (j = 0; j < 4; j++) {
    if (ci == cmykIds[j]) {
      *containsProcess = TRUE;
      return TRUE;
    }
  }

  return TRUE;
}

/* Search the current named color databases for an intercept of the namesObject.
 * If an intercept exists for this colorant, replace the tinttransform and
 * alternativespace with those from the databases.
 * Data for the named colorants is stored in the named color cache. If the
 * current colorant was found in the databases, but wasn't found in the
 * cache, add a new entry to the cache using the value of tintTransformId
 * found on entry.
 * If the colorant wasn't found in the databases, nor in the cache, add a new
 * entry to the cache indicating that it isn't in the databases by using
 * a NULL value for alternativespace.
 * If the colorant was found in the cache, return the cache data for the
 * alternativespace, tinttransform, and tinttransformId.
 */
Bool gsc_invokeNamedColorIntercept(GS_COLORinfo *colorInfo,
                                   OBJECT       *namesObject,
                                   Bool         *fgotIntercept,
                                   Bool         f_return_results,
                                   OBJECT       *tinttransform,
                                   OBJECT       *alternativespace,
                                   int32        *tintTransformId)
{
  OBJECT *result;
  OBJECT *interceptNamedColors;
  int32 stacksize;
  OBJECT *cachedAlternateSpace;
  OBJECT *cachedTintTransform;
  int32 cachedTintTransformId;
  Bool containsProcess;
  int numNames;

  *fgotIntercept = FALSE;

  /* No intercept is not an error */
  interceptNamedColors = gsc_getNamedColorIntercept(colorInfo);
  if (interceptNamedColors == NULL)
    return TRUE;

  /* We can only intercept one colorant */
  numNames = cc_getNumNames(namesObject);
  if (numNames != 1)
    return TRUE;

  /* CMYK process names should never be intercepted, so short-circuit the call-out
   * to the interpreter. This step is required at the back end because the
   * interpreter call isn't allowed there.
   */
  if (!containsPocessColorant(colorInfo->targetRS, namesObject, &containsProcess))
    return FALSE;
  if (containsProcess)
    return TRUE;

  /* If we've already seen this namesObject in the cache, use it because it's
   * inefficient to repeatedly process it.
   */
  if (cc_lookupNamedColorantStore(colorInfo, namesObject,
                                  &cachedAlternateSpace, &cachedTintTransform,
                                  &cachedTintTransformId)) {
    if (cachedAlternateSpace != NULL) {
      HQASSERT(cachedTintTransform != NULL, "tinttransform NULL");
      HQASSERT(cachedTintTransformId != 0, "tinttransformId zero");

      *fgotIntercept = TRUE;

      if (f_return_results) {
        Copy(alternativespace, cachedAlternateSpace);
        Copy(tinttransform, cachedTintTransform);
        *tintTransformId = cachedTintTransformId;
      }
    }
    else {
      HQASSERT(cachedTintTransform == NULL, "tinttransform non-NULL");
      HQASSERT(cachedTintTransformId == 0, "tinttransformId zero expected");
    }

    return TRUE;
  }

  HQASSERT(IS_INTERPRETER(), "Named colors should already be cached in back end");

  /* We use operandstack stack here since calling PS world. */
  stacksize = theStackSize( operandstack ) ;

  /* Push the interception description onto the stack. */
  if ( ! push( interceptNamedColors , &operandstack ))
    return FALSE ;

  /* Push the separation name onto the stack. */
  if ( ! push( namesObject , &operandstack ))
    return FALSE ;

  /* See if it's recognised. */
  if ( !run_ps_string((uint8*)"/HqnNamedColor /ProcSet findresource /NClookupColor get exec") ) {
    HQFAIL( "Internal call of `/HqnNamedColor /ProcSet findresource /NClookupColor get exec' failed");
    return error_handler( UNDEFINED ) ;
  }

  /* this ought to have left a <colorspace>+<tinttransform>+true, or false,
     on the stack, but never trust nuffink */
  if ( theStackSize( operandstack ) <= stacksize ) {
    HQFAIL( "NClookupColor left nothing on stack" ) ;
    return error_handler( STACKUNDERFLOW ) ;
  }

  result = theTop( operandstack ) ;
  if ( oType(*result) != OBOOLEAN ) {
    HQFAIL( "NClookupColor didn't leave boolean on top of stack" ) ;
    return error_handler( TYPECHECK ) ;
  }

  if ( oBool(*result)) {
    OBJECT tmpAlternateSpace = OBJECT_NOTVM_NOTHING;
    OBJECT tmpTintTransform = OBJECT_NOTVM_NOTHING;

    if ( theStackSize( operandstack ) != stacksize + 3 ) {
      HQFAIL( "NClookupColor succeeded in finding colour but left wrong number of objects on stack" ) ;
      return error_handler( UNDEFINED ) ;
    }

    pop(&operandstack);     /* the boolean */

    result = theTop(operandstack);
    if ((oType(*result) != OARRAY) ||
        (oExecutable(*result) != EXECUTABLE)) {
      HQFAIL("NClookupColor didn't leave procedure for tinttransform on stack");
      return error_handler(TYPECHECK) ;
    }

    Copy(&tmpTintTransform, result);

    pop(&operandstack);     /* the tinttransform */

    result = theTop(operandstack);
    if ((oType(*result) != OARRAY)) {
      HQFAIL("NClookupColor didn't leave array for alternativespace on stack");
      return error_handler(TYPECHECK) ;
    }

    Copy(&tmpAlternateSpace, result);

    if ( f_return_results ) {
      Copy(alternativespace, &tmpAlternateSpace);
      Copy(tinttransform, &tmpTintTransform);
    }

    pop(&operandstack);     /* the colorspace */

    /* The colorant was found in the databases. It's inefficient to repeatedly
     * process them so cache it to avoid that overhead.
     */
    if (tintTransformId != NULL)
      if (!cc_insertNamedColorantStore(colorInfo, namesObject,
                                       &tmpAlternateSpace, &tmpTintTransform,
                                       *tintTransformId))
        return FALSE;

    *fgotIntercept = TRUE;
  } else {
    /* The colorant wasn't found in the databases. It's inefficient to repeatedly
     * process them so cache it to avoid that overhead.
     */
    if (!cc_insertNamedColorantStore(colorInfo, namesObject, NULL, NULL, 0))
      return FALSE;

    if ( theStackSize( operandstack ) != stacksize + 1 ) {
      HQFAIL( "NClookupColor failed to find colour and left wrong number of objects on stack" ) ;
      return error_handler( UNDEFINED ) ;
    }
    /* NClookupColor simply returned false */
    /* pop the false */
    pop( & operandstack ) ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

Bool cc_tinttransformiscomplex( CLINK *pLink )
{
  CLINKtinttransform *tinttransform ;

  tinttransformAssertions( pLink ) ;

  tinttransform = pLink->p.tinttransform ;
  HQASSERT( tinttransform != NULL , "Somehow lost CLINKtinttransform" ) ;

  return ( tinttransform->simpleTransform == NULL && !tinttransform->fIsEmptyProcedure ) ;
}

GSC_SIMPLE_TRANSFORM *cc_tintSimpleTransform(CLINK *pLink)
{
  CLINKtinttransform *tinttransform ;

  tinttransformAssertions( pLink ) ;

  tinttransform = pLink->p.tinttransform ;
  HQASSERT( tinttransform != NULL , "Somehow lost CLINKtinttransform" ) ;

  return tinttransform->simpleTransform;
}

/* ---------------------------------------------------------------------- */
static Bool determineAlternativeSpace(GS_COLORinfo *colorInfo,              /* I */
                                      OBJECT       *colorSpaceObject,       /* I */
                                      Bool         allowNamedColorIntercept,/* I */
                                      Bool         fCompositing,            /* I */
                                      OBJECT       *alternativespace,       /* O */
                                      OBJECT       *tinttransform,          /* O */
                                      int32        *tinttransformId,        /* O */
                                      Bool         *fgotIntercept)          /* O */
{
  /* colorSpaceObject is an array containing the colorspace in question,
   * e.g. [ /Separation (PANTONE 386) /DeviceCMYK { ... } ]
   * We know the length of the array is correct;
   * The separation name can be a string or name object;
   * The base color space is in the same format as the operand to
   *   setcolorspace;
   * The procedure should take a tint and produce the number of numbers
   *   appropriate to the dimension of the base color space;
   * It doesn't matter if we assign gstate (or at least, the colour component
   *   of it) and then return an error, because gstate will be restored in
   *   the event of an error.
   *
   * interceptNamedColors is an array containing the sequence of NamedColor
   *   database resources to search for the color, as resource names (strings
   *   are allowed, too)
   * alternativespace and tinttransform are determined from colorSpaceObject
   *   or the intercept space as appropriate.
   */
  COLORSPACE_ID   space ;
  int32           colorspacedimension ;
  FILELIST        *flptr ;
  OBJECT          namesObject = OBJECT_NOTVM_NOTHING;
  OBJECT          *theo;

  theo = oArray(*colorSpaceObject) ;

  theo ++ ;
  Copy(&namesObject, theo);

  /* Check that the name(s) object is a name or string or an array of the same */
  if (oType(namesObject) != ONAME &&
      oType(namesObject) != OSTRING &&
      oType(namesObject) != OPACKEDARRAY &&
      oType(namesObject) != OARRAY )
    return error_handler( TYPECHECK ) ;

  if (oType(namesObject) == OPACKEDARRAY ||
      oType(namesObject) == OARRAY ) {
    int32     i;
    OBJECT    *name = oArray(namesObject);

    if (theLen(namesObject) == 0)
      return error_handler( TYPECHECK );

    for (i = 0; i < theLen(namesObject); i++, name++)
      if (oType(*name) != ONAME &&
          oType(*name) != OSTRING )
        return error_handler( TYPECHECK ) ;

    /* Convert a DeviceN space with just one colorant to a Separation space
     * for the benefit of named color databases that do not support DeviceN.
     */
    if (theLen(namesObject) == 1)
      Copy(&namesObject, oArray(namesObject));
  }

  /* theo is the alternativespace object */
  theo++ ;
  Copy(alternativespace, theo);
  if (!gsc_getcolorspacesizeandtype( colorInfo, theo, &space, &colorspacedimension ))
    return FALSE;

  /* Is the base color space one of the allowed ones? */
  if ( space == SPACE_Pattern || space == SPACE_Indexed )
    return error_handler( RANGECHECK ) ;

  /* theo is the tinttransform object */
  theo++;
  Copy(tinttransform, theo);
  switch ( oType(*theo)) {
  case OARRAY :
  case OPACKEDARRAY :
    if ( ! oExecutable(*theo))
      return error_handler( TYPECHECK ) ;
    if ( ! oCanExec(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

    /* Harlequin extension: bind the tint transform.
     * NB. Tint transforms from the job shouldn't be called in the back end,
     *     they should be internalised because we can't invoke the interpreter.
     *     Simple tint transforms are used in the back end, but they don't need
     *     auto binding.
     */
    if (!fCompositing) {
      corecontext_t *context = get_core_context_interp();
      if ( context->userparams->AutomaticBinding ) {
        if ( ! bind_automatically(context, tinttransform ))
          return FALSE ;
      }
    }
    break ;
  case OFILE :
    flptr = oFile(*theo) ;
    HQASSERT( flptr , "flptr NULL in gs_determineseparationspace." ) ;
    if ( ! isIInputFile( flptr ) ||
         ! isIOpenFileFilter( theo , flptr ) ||
         ! isIRewindable( flptr ))
      return error_handler( IOERROR ) ;
    break;
  case ODICTIONARY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  *tinttransformId = cc_nextTintTransformId(colorInfo->colorState->tintState);

  if (allowNamedColorIntercept) {
    /* If an intercept exists for this colorant, replace the tinttransform,
     * alternativespace, and tinttransformId with those from the named color
     * databases, possibly including a cached value of tinttransformId.
     */
    if ( !gsc_invokeNamedColorIntercept(colorInfo, &namesObject,
                                        fgotIntercept, TRUE, tinttransform,
                                        alternativespace, tinttransformId) ) {
      return FALSE;
    }
  }

  return TRUE;
}

int32 cc_nextTintTransformId(GS_TINT_STATE *tintState)
{
  /* Bump the next Id value, but don't let it return 0 because that is treated
   * specially by cc_lookupNamedColorantCache().
   */
  if (++tintState->TintTransformId == 0)
    ++tintState->TintTransformId;

  return tintState->TintTransformId;
}

/* Log stripped */

/* eof */
