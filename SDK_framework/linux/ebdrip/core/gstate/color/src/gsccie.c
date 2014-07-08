/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gsccie.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain links for cie based color spaces.
 */

#include "core.h"

#include "constant.h"           /* EPSILON */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "gcscan.h"             /* ps_scan_field */
#include "namedef_.h"           /* NAME_* */
#include "mm.h"                 /* mm_sac_alloc */
#include "mps.h"                /* mps_res_t */
#include "objects.h"            /* theOList */
#include "objstack.h"           /* push */
#include "params.h"             /* UserParams */
#include "swerrors.h"           /* TYPECHECK */
#include "hqmemset.h"

#include "gs_colorpriv.h"       /* CLINK */
#include "functns.h"
#include "gs_callps.h"
#include "gsccmmpriv.h"         /* cc_initTransformInfo */

#include "gsccie.h"

struct CLINKciebasedabc {
  cc_counter_t  refCnt;
  int32         linkType;
  size_t        structSize;
  Bool          isPhotoshopRGB;

  Bool          decodeabcPresent;
  Bool          matrixabcPresent;
  Bool          decodelmnPresent;
  Bool          matrixlmnPresent;

  Bool          convertLabtoXYZMethod;

  SYSTEMVALUE   rangeabc[6];
  OBJECT        decodeabc[3];
  CIECallBack   decodeabcfn[3];
  SYSTEMVALUE   matrixabc[9];
  CALLPSCACHE   *cpsc_decodeabc[3];

  SYSTEMVALUE   rangelmn[6];
  OBJECT        decodelmn[3];
  CIECallBack   decodelmnfn[3];
  SYSTEMVALUE   matrixlmn[9];
  CALLPSCACHE   *cpsc_decodelmn[3];
  Bool          use_cpsc;

  XYZVALUE      whitepoint;
  XYZVALUE      blackpoint;

  XYZVALUE      relativewhitepoint;
  XYZVALUE      relativeblackpoint;

  /* XUID/UniqueID. */
  int32         nXUIDs ;
  int32         *pXUIDs ;
};

struct CLINKciebaseddef {
  cc_counter_t  refCnt;
  int32         linkType;
  size_t        structSize;

  Bool          decodedefPresent;

  SYSTEMVALUE   rangedef[6];
  OBJECT        decodedef[3];
  CIECallBack   decodedeffn[3];
  CALLPSCACHE   *cpsc_decodedef[3];
  Bool          use_cpsc;

  SYSTEMVALUE   rangehij[6];
  OBJECT        deftable;

  SYSTEMVALUE   rangeabc[6];

  /* XUID/UniqueID. */
  int32         nXUIDs ;
  int32         *pXUIDs ;

  TRANSFORM_LINK_INFO *nextInfo;
};

struct CLINKciebaseddefg {
  cc_counter_t  refCnt;
  int32         linkType;
  size_t        structSize;

  Bool          decodedefgPresent;

  SYSTEMVALUE   rangedefg[8];
  OBJECT        decodedefg[4];
  CIECallBack   decodedefgfn[4];
  CALLPSCACHE   *cpsc_decodedefg[4];
  Bool          use_cpsc;

  SYSTEMVALUE   rangehijk[8];
  OBJECT        defgtable;

  SYSTEMVALUE   rangeabc[6];

  /* XUID/UniqueID. */
  int32         nXUIDs ;
  int32         *pXUIDs ;

  TRANSFORM_LINK_INFO *nextInfo;
};

/* Defines for convertLabtoXYZMethod */
#define CONVERT_Lab_WP            1
#define CONVERT_Lab_RELATIVE_WP   2
#define CONVERT_Lab_NONE          3

/* ---------------------------------------------------------------------- */

static void  ciebasedabc_destroy(CLINK *pLink);
static Bool ciebasedabc_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool ciebasedabc_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t ciebasedabc_scan( mps_ss_t ss, CLINK *pLink );
static size_t ciebasedabcStructSize(void);
static void ciebasedabcUpdatePtrs(CLINK *pLink, CLINKciebasedabc *ciebasedabcInfo);

static void  ciebaseddef_destroy(CLINK *pLink);
static Bool ciebaseddef_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool ciebaseddef_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t ciebaseddef_scan( mps_ss_t ss, CLINK *pLink );
static size_t ciebaseddefStructSize(void);
static void ciebaseddefUpdatePtrs(CLINK *pLink, CLINKciebaseddef *ciebaseddefInfo);

static void  ciebaseddefg_destroy(CLINK *pLink);
static Bool ciebaseddefg_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool ciebaseddefg_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t ciebaseddefg_scan( mps_ss_t ss, CLINK *pLink );
static size_t ciebaseddefgStructSize(void);
static void ciebaseddefgUpdatePtrs(CLINK *pLink, CLINKciebaseddefg *ciebaseddefgInfo);

#if defined( ASSERT_BUILD )
static void ciebasedabcAssertions(CLINK *pLink);
static void ciebaseddefAssertions(CLINK *pLink);
static void ciebaseddefgAssertions(CLINK *pLink);
#else
#define ciebasedabcAssertions(_pLink) EMPTY_STATEMENT()
#define ciebaseddefAssertions(_pLink) EMPTY_STATEMENT()
#define ciebaseddefgAssertions(_pLink) EMPTY_STATEMENT()
#endif

static Bool createciebasedabcinfo( CLINKciebasedabc   **ciebasedabcInfo,
                                   OBJECT             *colorSpaceObject,
                                   int32              linkType );
static void  destroyciebasedabcinfo( CLINKciebasedabc  **ciebasedabcInfo );

static void reserveciebasedinfo( CLINKciebasedabc *ciebasedabcInfo );
static size_t ciebasedabcInfoStructSize(void);
static size_t ciebaseddefInfoStructSize(void);
static size_t ciebaseddefgInfoStructSize(void);

#if defined( ASSERT_BUILD )
static void ciebasedabcInfoAssertions(CLINKciebasedabc *pInfo);
static void ciebaseddefInfoAssertions(CLINKciebaseddef *pInfo);
static void ciebaseddefgInfoAssertions(CLINKciebaseddefg *pInfo);
#else
#define ciebasedabcInfoAssertions(_pInfo) EMPTY_STATEMENT()
#define ciebaseddefInfoAssertions(_pInfo) EMPTY_STATEMENT()
#define ciebaseddefgInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static void convertLabtoXYZ( SYSTEMVALUE cie[3], SYSTEMVALUE pWhite[3]);
static Bool transformABCtoXYZ( CLINK *pLink, SYSTEMVALUE cie[3] );
static Bool transformDEFtoABC( CLINK *pLink, SYSTEMVALUE cie[3] );
static Bool transformDEFGtoABC( CLINK *pLink, SYSTEMVALUE cie[3] );

static void interpolatedefspace(CLINKciebaseddef *pInfo, SYSTEMVALUE cie[3]);
static void interpolatedefgspace(CLINKciebaseddefg *pInfo, SYSTEMVALUE cie[4]);

static Bool extract_ciebaseda( CLINKciebaseda    *pInfo,
                               OBJECT            *colorSpaceObject );
static Bool extract_ciebasedabc( CLINKciebasedabc  *pInfo,
                                 OBJECT            *colorSpaceObject );
static Bool extract_ciebasedabcTailEnd( CLINKciebasedabc  *pInfo,
                                        OBJECT            *theo );
static Bool extract_ciebaseddef( CLINKciebaseddef  *pInfo,
                                 OBJECT            *colorSpaceObject );
static Bool extract_ciebaseddefg( CLINKciebaseddefg  *pInfo,
                                  OBJECT             *colorSpaceObject );


static CLINKfunctions CLINKciebasedabc_functions =
{
  ciebasedabc_destroy,
  ciebasedabc_invokeSingle,
  NULL /* ciebasedabc_invokeBlock */,
  ciebasedabc_scan
};

static CLINKfunctions CLINKciebaseddef_functions =
{
  ciebaseddef_destroy,
  ciebaseddef_invokeSingle,
  NULL /* ciebaseddef_invokeBlock */,
  ciebaseddef_scan
};

static CLINKfunctions CLINKciebaseddefg_functions =
{
  ciebaseddefg_destroy,
  ciebaseddefg_invokeSingle,
  NULL /* ciebaseddefg_invokeBlock */,
  ciebaseddefg_scan
};

/* ---------------------------------------------------------------------- */

/*
 * Ciebaseda + Ciebasedabc Link Data Access Functions
 * ==================================================
 *
 * NB. There are different create link functions, but all other functions are
 *     common between CIEBasedA and CIEBasedABC.
 */
CLINK *cc_ciebaseda_create(CLINKciebaseda     *ciebasedaInfo,
                           XYZVALUE           **sourceWhitePoint,
                           XYZVALUE           **sourceBlackPoint,
                           XYZVALUE           **sourceRelativeWhitePoint,
                           XYZVALUE           **sourceRelativeBlackPoint)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEciebaseda (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(ciebasedaInfo != NULL, "ciebasedaInfo NULL");
  HQASSERT(sourceWhitePoint != NULL, "sourceWhitePoint NULL");
  HQASSERT(sourceBlackPoint != NULL, "sourceBlackPoint NULL");
  HQASSERT(sourceRelativeWhitePoint != NULL, "sourceRelativeWhitePoint NULL");
  HQASSERT(sourceRelativeBlackPoint != NULL, "sourceRelativeBlackPoint NULL");

  nXUIDs = ciebasedaInfo->nXUIDs ;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_CIEBasedA,
                           SPACE_CIEXYZ,
                           CL_TYPEciebaseda,
                           ciebasedabcStructSize(),
                           &CLINKciebasedabc_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  ciebasedabcUpdatePtrs(pLink, ciebasedaInfo);
  cc_reserveciebasedainfo(ciebasedaInfo);

  *sourceWhitePoint = &ciebasedaInfo->whitepoint;
  *sourceBlackPoint = &ciebasedaInfo->blackpoint;
  *sourceRelativeWhitePoint = &ciebasedaInfo->relativewhitepoint;
  *sourceRelativeBlackPoint = &ciebasedaInfo->relativeblackpoint;

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = ciebasedaInfo->pXUIDs[nXUIDs];

  ciebasedabcAssertions(pLink);

  return pLink;
}

CLINK *cc_ciebasedabc_create(CLINKciebasedabc   *ciebasedabcInfo,
                             XYZVALUE           **sourceWhitePoint,
                             XYZVALUE           **sourceBlackPoint,
                             XYZVALUE           **sourceRelativeWhitePoint,
                             XYZVALUE           **sourceRelativeBlackPoint)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEciebasedabc (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(ciebasedabcInfo != NULL, "ciebasedabcInfo NULL");
  HQASSERT(sourceWhitePoint != NULL, "sourceWhitePoint NULL");
  HQASSERT(sourceBlackPoint != NULL, "sourceBlackPoint NULL");
  HQASSERT(sourceRelativeWhitePoint != NULL, "sourceRelativeWhitePoint NULL");
  HQASSERT(sourceRelativeBlackPoint != NULL, "sourceRelativeBlackPoint NULL");

  nXUIDs = ciebasedabcInfo->nXUIDs ;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_CIEBasedABC,
                           SPACE_CIEXYZ,
                           CL_TYPEciebasedabc,
                           ciebasedabcStructSize(),
                           &CLINKciebasedabc_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  ciebasedabcUpdatePtrs(pLink, ciebasedabcInfo);
  cc_reserveciebasedabcinfo(ciebasedabcInfo);

  *sourceWhitePoint = &ciebasedabcInfo->whitepoint;
  *sourceBlackPoint = &ciebasedabcInfo->blackpoint;
  *sourceRelativeWhitePoint = &ciebasedabcInfo->relativewhitepoint;
  *sourceRelativeBlackPoint = &ciebasedabcInfo->relativeblackpoint;

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = ciebasedabcInfo->pXUIDs[nXUIDs];

  ciebasedabcAssertions(pLink);

  return pLink;
}

static void ciebasedabc_destroy(CLINK *pLink)
{
  ciebasedabcAssertions(pLink);

  destroyciebasedabcinfo(&pLink->p.ciebasedabc);

  cc_common_destroy(pLink);
}

static Bool ciebasedabc_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32                   i;
  CLINKciebasedabc        *ciebasedabcInfo;
  SYSTEMVALUE             cie[3];

  ciebasedabcAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");
  HQASSERT(pLink->pnext == NULL || pLink->pnext->n_iColorants == NUMBER_XYZ_COMPONENTS,
           "Invalid next link");

  ciebasedabcInfo   = pLink->p.ciebasedabc;

  if ( ! transformABCtoXYZ( pLink, cie ))
    return FALSE ;

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    oColorValues[i] = (USERVALUE) cie[i];

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool ciebasedabc_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  ciebasedabcAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */


/* ciebasedabc_scan - scan the ABC section of a CLINK */
static mps_res_t ciebasedabc_scan( mps_ss_t ss, CLINK *pLink )
{
  size_t i;
  mps_res_t res;

  if ( pLink->p.ciebasedabc->decodeabcPresent )
    for (i = 0; i < 3; i++) {
      res = ps_scan_field( ss, &pLink->p.ciebasedabc->decodeabc[i] );
      if ( res != MPS_RES_OK ) return res;
    }
  return MPS_RES_OK;
}


void cc_getCieBasedABCRange( CLINKciebasedabc *ciebasedabcInfo,
                             int32 index,
                             SYSTEMVALUE range[2] )
{
  ciebasedabcInfoAssertions(ciebasedabcInfo);
  HQASSERT(index < 3, "Invalid channel index");

  range[0] = ciebasedabcInfo->rangeabc[index * 2];
  range[1] = ciebasedabcInfo->rangeabc[index * 2 + 1];
}

void cc_getCieBasedABCWhitePoint( CLINK *pLink, XYZVALUE whitepoint)
{
  ciebasedabcAssertions(pLink);

  whitepoint[0] = pLink->p.ciebasedabc->whitepoint[0];
  whitepoint[1] = pLink->p.ciebasedabc->whitepoint[1];
  whitepoint[2] = pLink->p.ciebasedabc->whitepoint[2];
}

void cc_getCieBasedABCBlackPoint( CLINK *pLink, XYZVALUE blackpoint)
{
  ciebasedabcAssertions(pLink);

  blackpoint[0] = pLink->p.ciebasedabc->blackpoint[0];
  blackpoint[1] = pLink->p.ciebasedabc->blackpoint[1];
  blackpoint[2] = pLink->p.ciebasedabc->blackpoint[2];
}

static size_t ciebasedabcStructSize(void)
{
  return 0;
}

static void ciebasedabcUpdatePtrs(CLINK             *pLink,
                                  CLINKciebasedabc  *ciebasedabcInfo)
{
  pLink->p.ciebasedabc  = ciebasedabcInfo;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void ciebasedabcAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      pLink->linkType == CL_TYPEciebasedabc
                      ? CL_TYPEciebasedabc : CL_TYPEciebaseda,
                      ciebasedabcStructSize(),
                      &CLINKciebasedabc_functions);

  if (pLink->p.ciebasedabc != NULL) {
    ciebasedabcInfoAssertions(pLink->p.ciebasedabc);
    if (pLink->linkType == CL_TYPEciebaseda)
      HQASSERT(pLink->iColorSpace == SPACE_CIEBasedA, "Bad input color space");
    else
      HQASSERT(pLink->iColorSpace == SPACE_CIEBasedABC, "Bad input color space");
    HQASSERT(pLink->oColorSpace == SPACE_CIEXYZ, "Bad output color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Ciebaseddef Link Data Access Functions
 * ======================================
 */
CLINK *cc_ciebaseddef_create(CLINKciebaseddef   *ciebaseddefInfo)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEciebaseddef (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(ciebaseddefInfo != NULL, "ciebaseddefInfo NULL");

  nXUIDs = ciebaseddefInfo->nXUIDs ;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_CIEBasedDEF,
                           SPACE_CIEBasedABC,
                           CL_TYPEciebaseddef,
                           ciebaseddefStructSize(),
                           &CLINKciebaseddef_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  ciebaseddefUpdatePtrs(pLink, ciebaseddefInfo);
  cc_reserveciebaseddefinfo(ciebaseddefInfo);

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = ciebaseddefInfo->pXUIDs[nXUIDs];

  ciebaseddefAssertions(pLink);

  return pLink;
}

static void ciebaseddef_destroy(CLINK *pLink)
{
  ciebaseddefAssertions(pLink);

  cc_destroyciebaseddefinfo(&pLink->p.ciebaseddef);

  cc_common_destroy(pLink);
}

static Bool ciebaseddef_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  SYSTEMVALUE   cie[3];
  int32         i;

  ciebaseddefAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  if ( ! transformDEFtoABC( pLink, cie ))
    return FALSE ;

  for (i = 0; i < 3; i++)
    oColorValues[i] = (USERVALUE) cie[i];

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool ciebaseddef_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  ciebaseddefAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */


/* ciebaseddef_scan - scan the DEF section of a CLINK */
static mps_res_t ciebaseddef_scan( mps_ss_t ss, CLINK *pLink )
{
  size_t i;
  mps_res_t res;

  if ( pLink->p.ciebaseddef->decodedefPresent )
    for (i = 0; i < 3; i++) {
      res = ps_scan_field( ss, &pLink->p.ciebaseddef->decodedef[i] );
      if ( res != MPS_RES_OK ) return res;
    }
  return MPS_RES_OK;
}


void cc_getCieBasedDEFRange( CLINKciebaseddef *ciebaseddefInfo,
                             int32 index,
                             SYSTEMVALUE range[2] )
{
  ciebaseddefInfoAssertions(ciebaseddefInfo);
  HQASSERT(index < 3, "Invalid channel index");

  range[0] = ciebaseddefInfo->rangedef[index * 2];
  range[1] = ciebaseddefInfo->rangedef[index * 2 + 1];
}


static size_t ciebaseddefStructSize(void)
{
  return 0;
}

static void ciebaseddefUpdatePtrs(CLINK             *pLink,
                                  CLINKciebaseddef  *ciebaseddefInfo)
{
  pLink->p.ciebaseddef = ciebaseddefInfo;

  return;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void ciebaseddefAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEciebaseddef,
                      ciebaseddefStructSize(),
                      &CLINKciebaseddef_functions);


  if (pLink->p.ciebaseddef != NULL) {
    ciebaseddefInfoAssertions(pLink->p.ciebaseddef);

    HQASSERT(pLink->iColorSpace == SPACE_CIEBasedDEF, "Bad input color space");
    HQASSERT(pLink->oColorSpace == SPACE_CIEBasedABC, "Bad output color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Ciebaseddefg Link Data Access Functions
 * =======================================
 */
CLINK *cc_ciebaseddefg_create(CLINKciebaseddefg  *ciebaseddefgInfo)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEciebaseda (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(ciebaseddefgInfo != NULL, "ciebaseddefgInfo NULL");

  nXUIDs = ciebaseddefgInfo->nXUIDs ;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_CIEBasedDEFG,
                           SPACE_CIEBasedABC,
                           CL_TYPEciebaseddefg,
                           ciebaseddefgStructSize(),
                           &CLINKciebaseddefg_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  ciebaseddefgUpdatePtrs(pLink, ciebaseddefgInfo);
  cc_reserveciebaseddefginfo(ciebaseddefgInfo);

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = ciebaseddefgInfo->pXUIDs[nXUIDs];

  ciebaseddefgAssertions(pLink);

  return pLink;
}

static void ciebaseddefg_destroy(CLINK *pLink)
{
  ciebaseddefgAssertions(pLink);

  cc_destroyciebaseddefginfo(&pLink->p.ciebaseddefg);

  cc_common_destroy(pLink);
}

static Bool ciebaseddefg_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  SYSTEMVALUE   cie[3];
  int32         i;

  ciebaseddefgAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  if ( ! transformDEFGtoABC( pLink, cie ))
    return FALSE ;

  for (i = 0; i < 3; i++)
    oColorValues[i] = (USERVALUE) cie[i];

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool ciebaseddefg_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  ciebaseddefgAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */


/* ciebaseddefg_scan - scan the DEFG section of a CLINK */
static mps_res_t ciebaseddefg_scan( mps_ss_t ss, CLINK *pLink )
{
  size_t i;
  mps_res_t res;

  if ( pLink->p.ciebaseddefg->decodedefgPresent )
    for (i = 0; i < 3; i++) {
      res = ps_scan_field( ss, &pLink->p.ciebaseddefg->decodedefg[i] );
      if ( res != MPS_RES_OK ) return res;
    }
  return MPS_RES_OK;
}


void cc_getCieBasedDEFGRange( CLINKciebaseddefg *ciebaseddefgInfo,
                              int32 index,
                              SYSTEMVALUE range[2] )
{
  ciebaseddefgInfoAssertions(ciebaseddefgInfo);
  HQASSERT(index < 4, "Invalid channel index");

  range[0] = ciebaseddefgInfo->rangedefg[index * 2];
  range[1] = ciebaseddefgInfo->rangedefg[index * 2 + 1];
}


static size_t ciebaseddefgStructSize(void)
{
  return 0;
}

static void ciebaseddefgUpdatePtrs(CLINK              *pLink,
                                   CLINKciebaseddefg  *ciebaseddefgInfo)
{
  pLink->p.ciebaseddefg = ciebaseddefgInfo;
  return;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void ciebaseddefgAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEciebaseddefg,
                      ciebaseddefgStructSize(),
                      &CLINKciebaseddefg_functions);


  if (pLink->p.ciebaseddefg != NULL) {
    ciebaseddefgInfoAssertions(pLink->p.ciebaseddefg);

    HQASSERT(pLink->iColorSpace == SPACE_CIEBasedDEFG, "Bad input color space");
    HQASSERT(pLink->oColorSpace == SPACE_CIEBasedABC, "Bad output color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Ciebaseda Info Data Access Functions
 * ====================================
 */
Bool cc_createciebasedainfo( CLINKciebaseda     **ciebasedaInfo,
                             OBJECT             *colorSpaceObject )
{
  return createciebasedabcinfo(ciebasedaInfo,
                               colorSpaceObject,
                               CL_TYPEciebaseda );
}

void cc_destroyciebasedainfo( CLINKciebaseda **ciebasedaInfo )
{
  HQASSERT((*ciebasedaInfo)->linkType == CL_TYPEciebaseda, "linkType not set");

  /* Note: despite appearances to the contrary, CLINKciebaseda and CLINKciebasedabc
     are actually typedefs for the same structure type, hence it is possible to pass
     as CLINKciebaseda to destroyciebasedabcinfo which is expecting a
     CLINKciebasedabc pointer */

  destroyciebasedabcinfo(ciebasedaInfo);
}

void cc_reserveciebasedainfo( CLINKciebaseda *ciebasedaInfo )
{
  HQASSERT(ciebasedaInfo->linkType == CL_TYPEciebaseda, "linkType not set");
  reserveciebasedinfo(ciebasedaInfo);
}

/* ---------------------------------------------------------------------- */

/*
 * Ciebasedabc Info Data Access Functions
 * ======================================
 */

Bool cc_createciebasedabcinfo( CLINKciebasedabc   **ciebasedabcInfo,
                               OBJECT             *colorSpaceObject )
{
  return createciebasedabcinfo(ciebasedabcInfo,
                               colorSpaceObject,
                               CL_TYPEciebasedabc );
}

void cc_destroyciebasedabcinfo( CLINKciebasedabc **ciebasedabcInfo )
{
  HQASSERT((*ciebasedabcInfo)->linkType == CL_TYPEciebasedabc, "linkType not set");
  destroyciebasedabcinfo(ciebasedabcInfo);
}

void cc_reserveciebasedabcinfo( CLINKciebasedabc *ciebasedabcInfo )
{
  HQASSERT(ciebasedabcInfo->linkType == CL_TYPEciebasedabc, "linkType not set");
  reserveciebasedinfo(ciebasedabcInfo);
}

/* ---------------------------------------------------------------------- */

static CALLPSCACHE *cie_createcache(int32 fnType, OBJECT *psobj,
                                    SYSTEMVALUE *crd_range)
{
  CALLPSCACHE *cpsc;

  if ( (cpsc = create_callpscache(fnType, 1, 0, crd_range, psobj)) == NULL )
    return NULL;

  return cpsc;
}

/**
 * pre-cache the results of calling the various bits of PS
 * present in the CIE based ABC colorspace
 */
static Bool abc_cache_ps_calls(CLINKciebasedabc *pInfo)
{
  CALLPSCACHE *cpsc;
  OBJECT *theproc;
  int32 i;

  /* Call the DecodeABC procedures */
  if ( pInfo->decodeabcPresent ) {
    for (i = 0; i < 3; i++) {
      if ( pInfo->decodeabcfn[i] == NULL ) {

        theproc = pInfo->decodeabc + i;

        if ( (cpsc = cie_createcache(FN_CIE_TINT_TFM, theproc,
                                     &(pInfo->rangeabc[2*i]))) == NULL )
          return FALSE;
        pInfo->cpsc_decodeabc[i] = cpsc;
      }
    }
  }
  /* Call the DecodeLMN procedures */
  if ( pInfo->decodelmnPresent ) {
    for (i = 0; i < 3; i++) {
      if ( pInfo->decodelmnfn[i] == NULL ) {

        theproc = pInfo->decodelmn + i;

        if ( (cpsc = cie_createcache(FN_CIE_TINT_TFM, theproc,
                                     &(pInfo->rangelmn[2*i]))) == NULL )
          return FALSE;
        pInfo->cpsc_decodelmn[i] = cpsc;
      }
    }
  }
  return TRUE;
}

/**
 * pre-cache the results of calling the various bits of PS
 * present in the CIE based DEF colorspace
 */
static Bool def_cache_ps_calls(CLINKciebaseddef *pInfo)
{
  CALLPSCACHE *cpsc;
  OBJECT *theproc;
  int32 i;

  /* Call the DecodeDEF procedures */
  if ( pInfo->decodedefPresent ) {
    for (i = 0; i < 3; i++) {
      if ( pInfo->decodedeffn[i] == NULL ) {

        theproc = pInfo->decodedef + i;

        if ( (cpsc = cie_createcache(FN_CIE_TINT_TFM, theproc,
                                     &(pInfo->rangedef[2*i]))) == NULL )
          return FALSE;
        pInfo->cpsc_decodedef[i] = cpsc;
      }
    }
  }
  return TRUE;
}
/**
 * pre-cache the results of calling the various bits of PS
 * present in the CIE based DEFG colorspace
 */
static Bool defg_cache_ps_calls(CLINKciebaseddefg *pInfo)
{
  CALLPSCACHE *cpsc;
  OBJECT *theproc;
  int32 i;

  /* Call the DecodeDEFG procedures */
  if ( pInfo->decodedefgPresent ) {
    for (i = 0; i < 4; i++) {
      if ( pInfo->decodedefgfn[i] == NULL ) {

        theproc = pInfo->decodedefg + i;

        if ( (cpsc = cie_createcache(FN_CIE_TINT_TFM, theproc,
                                     &(pInfo->rangedefg[2*i]))) == NULL )
          return FALSE;
        pInfo->cpsc_decodedefg[i] = cpsc;
      }
    }
  }
  return TRUE;
}


/*
 * Ciebaseda + Ciebasedabc Info Common Data Access Functions
 * =========================================================
 */

static Bool createciebasedabcinfo( CLINKciebasedabc   **ciebasedabcInfo,
                                   OBJECT             *colorSpaceObject,
                                   int32              linkType )
{
  Bool                extractFailed;
  int32               i;
  CLINKciebasedabc    *pInfo;
  size_t               structSize;

  structSize = ciebasedabcInfoStructSize();

  pInfo = (CLINKciebasedabc *) mm_sac_alloc( mm_pool_color,
                                             structSize,
                                             MM_ALLOC_CLASS_NCOLOR );
  if (pInfo == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)pInfo, (int)structSize);

  pInfo->refCnt = 1;
  pInfo->linkType = linkType;
  pInfo->structSize = structSize;

  pInfo->decodeabcPresent = FALSE;
  pInfo->matrixabcPresent = FALSE;
  pInfo->decodelmnPresent = FALSE;
  pInfo->matrixlmnPresent = FALSE;

  pInfo->convertLabtoXYZMethod = CONVERT_Lab_NONE;

  /* Set default values for structure fields */
  for (i = 0; i < 3; i++) {
    pInfo->rangeabc[2*i]     = 0;
    pInfo->rangeabc[2*i + 1] = 1;
    pInfo->decodeabc[i] = onull; /* set slot properties */
    pInfo->decodeabcfn[i] = NULL;
    pInfo->matrixabc[3*i + i] = 1;
    pInfo->cpsc_decodeabc[i] = NULL;

    pInfo->rangelmn[2*i]     = 0;
    pInfo->rangelmn[2*i + 1] = 1;
    pInfo->decodelmn[i] = onull; /* set slot properties */
    pInfo->decodelmnfn[i] = NULL;
    pInfo->matrixlmn[3*i + i] = 1;
    pInfo->cpsc_decodelmn[i] = NULL;
  }

  pInfo->nXUIDs = 0;
  pInfo->pXUIDs = NULL;

  extractFailed = FALSE;
  if (linkType == CL_TYPEciebaseda) {
    if (!extract_ciebaseda(pInfo, colorSpaceObject))
      extractFailed = TRUE;
  }
  else {
    if (!extract_ciebasedabc(pInfo, colorSpaceObject))
      extractFailed = TRUE;
  }

  if (extractFailed) {
    destroyciebasedabcinfo(&pInfo);
    return FALSE;
  }

  /* The use of the cpsc is reserved for future use, such as lazy image reading. */
  pInfo->use_cpsc = FALSE;
  if (pInfo->use_cpsc) {
    /* If we need to use this CIEBased space outside of the original save level,
     * we need to cache the results of running the PS procedures. This can be
     * done with 1d tables and interpolation.
     * Will not be used if we're already using optimised C code for known idioms.
     */
    if ( !abc_cache_ps_calls(pInfo) )
      return FALSE;
  }

  ciebasedabcInfoAssertions(pInfo);

  *ciebasedabcInfo = pInfo;

  return TRUE;
}

static void freeciebasedabcinfo( CLINKciebasedabc  *ciebasedabcInfo )
{
  int32 i;

  cc_destroy_xuids( & ciebasedabcInfo->nXUIDs , & ciebasedabcInfo->pXUIDs ) ;

  for ( i = 0; i < 3; i++ ) {
    if ( ciebasedabcInfo->cpsc_decodeabc[i] != NULL )
      destroy_callpscache(&ciebasedabcInfo->cpsc_decodeabc[i]);
    if ( ciebasedabcInfo->cpsc_decodelmn[i] != NULL )
      destroy_callpscache(&ciebasedabcInfo->cpsc_decodelmn[i]);
  }
  mm_sac_free(mm_pool_color,
              (mm_addr_t) ciebasedabcInfo,
              (mm_size_t) ciebasedabcInfo->structSize);
}

static void destroyciebasedabcinfo( CLINKciebasedabc  **ciebasedabcInfo )
{
  if ( *ciebasedabcInfo != NULL ) {
    ciebasedabcInfoAssertions(*ciebasedabcInfo);
    CLINK_RELEASE(ciebasedabcInfo, freeciebasedabcinfo);
  }
}

static void reserveciebasedinfo( CLINKciebasedabc *ciebasedabcInfo )
{
  if ( ciebasedabcInfo != NULL ) {
    ciebasedabcInfoAssertions( ciebasedabcInfo ) ;
    CLINK_RESERVE( ciebasedabcInfo ) ;
  }
}

static size_t ciebasedabcInfoStructSize(void)
{
  return sizeof(CLINKciebasedabc);
}

#if defined( ASSERT_BUILD )
static void ciebasedabcInfoAssertions(CLINKciebasedabc *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->linkType == CL_TYPEciebaseda ||
           pInfo->linkType == CL_TYPEciebasedabc, "linkType not set");
  HQASSERT(pInfo->structSize == ciebasedabcInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif


Bool cc_get_isPhotoshopRGB(CLINKciebasedabc *ciebasedabcInfo)
{
  ciebasedabcInfoAssertions(ciebasedabcInfo);

  return ciebasedabcInfo->isPhotoshopRGB;
}

/* ---------------------------------------------------------------------- */

/*
 * Ciebaseddef Info Data Access Functions
 * ======================================
 */

Bool cc_createciebaseddefinfo( CLINKciebaseddef   **ciebaseddefInfo,
                               COLORSPACE_ID      *outputColorSpaceId,
                               OBJECT             *colorSpaceObject )
{
  int32               i;
  CLINKciebaseddef    *pInfo;
  size_t              structSize;

  structSize = ciebaseddefInfoStructSize();

  pInfo = (CLINKciebaseddef *) mm_sac_alloc( mm_pool_color,
                                             structSize,
                                             MM_ALLOC_CLASS_NCOLOR );
  if (pInfo == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)pInfo, (int)structSize);

  pInfo->refCnt = 1;
  pInfo->linkType = CL_TYPEciebaseddef;
  pInfo->structSize = structSize;

  pInfo->decodedefPresent = FALSE;

  /* Set default values for structure fields */
  for (i = 0; i < 3; i++) {
    pInfo->rangedef[2*i]     = 0;
    pInfo->rangedef[2*i + 1] = 1;
    pInfo->decodedef[i] = onull; /* set slot properties */
    pInfo->decodedeffn[i] = NULL;
    pInfo->cpsc_decodedef[i] = NULL;
    pInfo->rangehij[2*i]     = 0;
    pInfo->rangehij[2*i + 1] = 1;

    pInfo->rangeabc[2*i]     = 0;
    pInfo->rangeabc[2*i + 1] = 1;
  }
  pInfo->deftable = onull; /* set slot properties */

  pInfo->nXUIDs = 0;
  pInfo->pXUIDs = NULL;

  pInfo->nextInfo = NULL;

  if (!extract_ciebaseddef(pInfo, colorSpaceObject)) {
    cc_destroyciebaseddefinfo(&pInfo);
    return FALSE;
  }

  pInfo->nextInfo = mm_sac_alloc(mm_pool_color,
                                 cc_sizeofTransformInfo(),
                                 MM_ALLOC_CLASS_NCOLOR );
  if (pInfo->nextInfo == NULL) {
    cc_destroyciebaseddefinfo(&pInfo);
    return error_handler(VMERROR);
  }

  cc_initTransformInfo(pInfo->nextInfo);
  pInfo->nextInfo->inputColorSpaceId = SPACE_CIEBasedABC;
  pInfo->nextInfo->outputColorSpaceId = SPACE_CIEXYZ;

  if (!cc_createciebasedabcinfo(&pInfo->nextInfo->u.ciebasedabc, colorSpaceObject)) {
    cc_destroyciebaseddefinfo(&pInfo);
    return FALSE;
  }

  /* The use of the cpsc is reserved for future use, such as lazy image reading. */
  pInfo->use_cpsc = FALSE;
  if (pInfo->use_cpsc) {
    /* If we need to use this CIEBased space outside of the original save level,
     * we need to cache the results of running the PS procedures. This can be
     * done with 1d tables and interpolation.
     * Will not be used if we're already using optimised C code for known idioms.
     */
    if ( !def_cache_ps_calls(pInfo) )
      return FALSE;
  }

  ciebaseddefInfoAssertions(pInfo);

  *ciebaseddefInfo = pInfo;
  *outputColorSpaceId = pInfo->nextInfo->inputColorSpaceId;

  return TRUE;
}

static void freeciebaseddefinfo( CLINKciebaseddef *ciebaseddefInfo )
{
  int32 i;

  cc_destroy_xuids( & ciebaseddefInfo->nXUIDs , & ciebaseddefInfo->pXUIDs ) ;

  for ( i = 0; i < 3; i++ ) {
    if ( ciebaseddefInfo->cpsc_decodedef[i] != NULL )
      destroy_callpscache(&ciebaseddefInfo->cpsc_decodedef[i]);
  }
  if (ciebaseddefInfo->nextInfo != NULL) {
    if (ciebaseddefInfo->nextInfo->u.shared != NULL) {
      cc_destroyTransformInfo(ciebaseddefInfo->nextInfo);
    }
    mm_sac_free(mm_pool_color,
                ciebaseddefInfo->nextInfo,
                cc_sizeofTransformInfo());
  }

  mm_sac_free(mm_pool_color,
              (mm_addr_t) ciebaseddefInfo,
              (mm_size_t) ciebaseddefInfo->structSize);
}

void cc_destroyciebaseddefinfo( CLINKciebaseddef **ciebaseddefInfo )
{
  if ( *ciebaseddefInfo != NULL ) {
    ciebaseddefInfoAssertions(*ciebaseddefInfo);
    CLINK_RELEASE(ciebaseddefInfo, freeciebaseddefinfo);
  }
}

void cc_reserveciebaseddefinfo( CLINKciebaseddef *ciebaseddefInfo )
{
  if ( ciebaseddefInfo != NULL ) {
    ciebaseddefInfoAssertions( ciebaseddefInfo ) ;
    CLINK_RESERVE( ciebaseddefInfo ) ;
  }
}

TRANSFORM_LINK_INFO *cc_nextCIEBasedDEFInfo(CLINKciebaseddef *ciebaseddefInfo)
{
  ciebaseddefInfoAssertions( ciebaseddefInfo ) ;

  return ciebaseddefInfo->nextInfo;
}

static size_t ciebaseddefInfoStructSize(void)
{
  return sizeof(CLINKciebaseddef);
}

#if defined( ASSERT_BUILD )
static void ciebaseddefInfoAssertions(CLINKciebaseddef *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->linkType == CL_TYPEciebaseddef, "linkType not set");
  HQASSERT(pInfo->structSize == ciebaseddefInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Ciebaseddefg Info Data Access Functions
 * =======================================
 */

Bool cc_createciebaseddefginfo( CLINKciebaseddefg  **ciebaseddefgInfo,
                                COLORSPACE_ID      *outputColorSpaceId,
                                OBJECT             *colorSpaceObject )
{
  int32               i;
  CLINKciebaseddefg   *pInfo;
  size_t              structSize;

  structSize = ciebaseddefgInfoStructSize();

  pInfo = mm_sac_alloc( mm_pool_color,
                        structSize,
                        MM_ALLOC_CLASS_NCOLOR );

  if (pInfo == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)pInfo, (int)structSize);

  pInfo->refCnt = 1;
  pInfo->linkType = CL_TYPEciebaseddefg;
  pInfo->structSize = structSize;

  pInfo->decodedefgPresent = FALSE;

  /* Set default values for structure fields */
  for (i = 0; i < 4; i++) {
    pInfo->rangedefg[2*i]     = 0;
    pInfo->rangedefg[2*i + 1] = 1;
    pInfo->decodedefg[i] = onull; /* set slot properties */
    pInfo->decodedefgfn[i] = NULL;
    pInfo->cpsc_decodedefg[i] = NULL;

    pInfo->rangehijk[2*i]     = 0;
    pInfo->rangehijk[2*i + 1] = 1;
  }
  pInfo->defgtable = onull; /* set slot properties */

  for (i = 0; i < 3; i++) {
    pInfo->rangeabc[2*i]     = 0;
    pInfo->rangeabc[2*i + 1] = 1;
  }

  pInfo->nXUIDs = 0;
  pInfo->pXUIDs = NULL;

  pInfo->nextInfo = NULL;

  if (!extract_ciebaseddefg(pInfo, colorSpaceObject)) {
    cc_destroyciebaseddefginfo(&pInfo);
    return FALSE;
  }

  pInfo->nextInfo = mm_sac_alloc(mm_pool_color,
                                 cc_sizeofTransformInfo(),
                                 MM_ALLOC_CLASS_NCOLOR );
  if (pInfo->nextInfo == NULL) {
    cc_destroyciebaseddefginfo(&pInfo);
    return FALSE;
  }

  cc_initTransformInfo(pInfo->nextInfo);
  pInfo->nextInfo->inputColorSpaceId = SPACE_CIEBasedABC;
  pInfo->nextInfo->outputColorSpaceId = SPACE_CIEXYZ;

  if (!cc_createciebasedabcinfo(&pInfo->nextInfo->u.ciebasedabc, colorSpaceObject)) {
    cc_destroyciebaseddefginfo(&pInfo);
    return FALSE;
  }

  /* The use of the cpsc is reserved for future use, such as lazy image reading. */
  pInfo->use_cpsc = FALSE;
  if (pInfo->use_cpsc) {
    /* If we need to use this CIEBased space outside of the original save level,
     * we need to cache the results of running the PS procedures. This can be
     * done with 1d tables and interpolation.
     * Will not be used if we're already using optimised C code for known idioms.
     */
    if ( !defg_cache_ps_calls(pInfo) )
      return FALSE;
  }

  ciebaseddefgInfoAssertions(pInfo);

  *ciebaseddefgInfo = pInfo;
  *outputColorSpaceId = pInfo->nextInfo->inputColorSpaceId;

  return TRUE;
}

static void freeciebaseddefginfo( CLINKciebaseddefg  *ciebaseddefgInfo )
{
  int32 i;

  cc_destroy_xuids( & ciebaseddefgInfo->nXUIDs , & ciebaseddefgInfo->pXUIDs ) ;

  for ( i = 0; i < 4; i++ ) {
    if ( ciebaseddefgInfo->cpsc_decodedefg[i] != NULL )
      destroy_callpscache(&ciebaseddefgInfo->cpsc_decodedefg[i]);
  }
  if (ciebaseddefgInfo->nextInfo != NULL) {
    if (ciebaseddefgInfo->nextInfo->u.shared != NULL) {
      cc_destroyTransformInfo(ciebaseddefgInfo->nextInfo);
    }
    mm_sac_free(mm_pool_color,
                ciebaseddefgInfo->nextInfo,
                cc_sizeofTransformInfo());
  }

  mm_sac_free(mm_pool_color,
              (mm_addr_t) ciebaseddefgInfo,
              (mm_size_t) ciebaseddefgInfo->structSize);
}

void cc_destroyciebaseddefginfo( CLINKciebaseddefg  **ciebaseddefgInfo )
{
  if ( *ciebaseddefgInfo != NULL ) {
    ciebaseddefgInfoAssertions(*ciebaseddefgInfo);
    CLINK_RELEASE(ciebaseddefgInfo, freeciebaseddefginfo);
  }
}

void cc_reserveciebaseddefginfo( CLINKciebaseddefg *ciebaseddefgInfo )
{
  if ( ciebaseddefgInfo != NULL ) {
    ciebaseddefgInfoAssertions( ciebaseddefgInfo ) ;
    CLINK_RESERVE( ciebaseddefgInfo ) ;
  }
}

TRANSFORM_LINK_INFO *cc_nextCIEBasedDEFGInfo(CLINKciebaseddefg *ciebaseddefgInfo)
{
  ciebaseddefgInfoAssertions( ciebaseddefgInfo ) ;

  return ciebaseddefgInfo->nextInfo;
}

static size_t ciebaseddefgInfoStructSize(void)
{
  return sizeof(CLINKciebaseddefg);
}

#if defined( ASSERT_BUILD )
static void ciebaseddefgInfoAssertions(CLINKciebaseddefg *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->linkType == CL_TYPEciebaseddefg, "linkType not set");
  HQASSERT(pInfo->structSize == ciebaseddefgInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

/* ---------------------------------------------------------------------- */
static void convertLabtoXYZ(SYSTEMVALUE cie[3], XYZVALUE white)
{
/*
 * N.B. This is based on the version in SWcrdcore!src:collib.c, which in turn
 * says the equations invert those in following:
 *
 * Color Science: Concepts and Methods, Quantitative
 *                Data and Formulae, Second Edition, (pp166,167)
 *                Wyszecki and Stiles, Pub Wylie 1982.
 *                ISBN 0-471-02106-7
 *
 * Note that there is a change in the function for calculating lab and xyz
 * values at L* = 8, or L_SWITCH.  The ratio of Y to Yn at this point is
 * ~0.008856 or ALPHA_RATIO - it can be found by solving
 * L_SWITCH = 116*ALPHA_RATIO - 16.  The ratio coefficient for L* < L_SWITCH,
 * DELTA_RATIO is ~903.2962962963, and can be found by solving
 * L_SWITCH = DELTA_RATIO*ALPHA_RATIO.  The last value, GAMMA_RATIO falls out
 * as DELTA_RATIO/116.
 *
 * Using symbols for the values in the original equations allows simplification
 * of some expressions to hopefully maintain accuracy.
 */
#define L_SWITCH    (8.0)
#define ALPHA_RATIO (0.008856451679036)     /* Y ratio at which L* = 8 (= (24/116)^3) */
#define DELTA_RATIO (L_SWITCH/ALPHA_RATIO)  /* Which is ~903.2962962963 */
#define GAMMA_RATIO (DELTA_RATIO/116.0)     /* Also L_SWITCH/(116.0*ALPHA_RATIO))
                                               and is ~7.787037037037 */

/* Macros */
#define CUBE(x)     ((x)*(x)*(x))
#define LAB2XYZCALC(rInput, rRange) (((rInput) >  0.2068965517241) ? \
                                     ((rRange)*CUBE((rInput))) : \
                                     ((rRange)*((rInput) - (16.0/116.0))/GAMMA_RATIO))

  double  gXDivXn;
  double  gYDivYn;
  double  gZDivZn;

  HQASSERT(cie != NULL, "Null cie value in convertLabtoXYZ");
  HQASSERT(white != NULL, "Null white pointer in convertLabtoXYZ");

  gYDivYn = (cie[0] + 16.0)/116.0;
  gXDivXn = (cie[1]/500.0) + gYDivYn;
  gZDivZn = gYDivYn - (cie[2]/200.0);

  if (cie[0] > L_SWITCH) {
    cie[1] = white[1] * CUBE(gYDivYn);

  } else {
    cie[1] = white[1] * cie[0]/DELTA_RATIO;
  }

  cie[0] = LAB2XYZCALC(gXDivXn, white[0]);
  cie[2] = LAB2XYZCALC(gZDivZn, white[2]);

} /* Function CLIBLABToXYZ */


static Bool transformABCtoXYZ( CLINK *pLink, SYSTEMVALUE cie[3] )
{
  register int32    i;
  CLINKciebasedabc  *pInfo;

  pInfo = pLink->p.ciebasedabc;

  cie[0] = pLink->iColorValues[0];
  if (pInfo->linkType == CL_TYPEciebasedabc)
  {
    cie[1] = pLink->iColorValues[1];
    cie[2] = pLink->iColorValues[2];
  }
  else
  {
    cie[1] = 0;
    cie[2] = 0;
  }

  /* STEP 1 ON PAGE 297 */

  /* narrow to RangeABC */
  NARROW3 (cie, pInfo->rangeabc);

  /* call the DecodeABC procedures */
  if ( pInfo->decodeabcPresent ) {
    for (i = 0; i < pLink->n_iColorants; i++) {
      if ( pInfo->decodeabcfn[i] != NULL ) {
        CALL_C(pInfo->decodeabcfn[i], cie[i], NULL);
      } else if (pInfo->cpsc_decodeabc[i] != NULL) {
        USERVALUE uval = (USERVALUE)cie[i];
        lookup_callpscache(pInfo->cpsc_decodeabc[i], uval, &uval);
        cie[i] = (SYSTEMVALUE)uval;
      }
      else {
        if ( !call_psproc(&pInfo->decodeabc[i], cie[i], &cie[i], 1) )
          return FALSE;
      }
    }
  }

  /* multiply by MatrixABC */
  if ( pInfo->matrixabcPresent ) {
    MATRIX_MULTIPLY (cie, pInfo->matrixabc);
  }
  /* from here it's the same for CIEBasedABC and CIEBasedA */

  /* narrow to RangeLMN */
  NARROW3 (cie, pInfo->rangelmn);

  /* call the DecodeLMN procedures */
  if ( pInfo->decodelmnPresent ) {
    for (i = 0; i < 3; i++) {
      if ( pInfo->decodelmnfn[i] != NULL )
        CALL_C(pInfo->decodelmnfn[i], cie[i], NULL);
      else if (pInfo->cpsc_decodelmn[i] != NULL) {
        USERVALUE uval = (USERVALUE)cie[i];
        lookup_callpscache(pInfo->cpsc_decodelmn[i], uval, &uval);
        cie[i] = (SYSTEMVALUE)uval;
      }
      else {
        if ( !call_psproc(&pInfo->decodelmn[i], cie[i], &cie[i], 1) )
          return FALSE;
      }
    }
  }

  /* multiply by MatrixLMN */
  if ( pInfo->matrixlmnPresent ) {
    MATRIX_MULTIPLY (cie, pInfo->matrixlmn);
  }

  /* convert from Lab to XYZ if necessary */
  if (pInfo->convertLabtoXYZMethod == CONVERT_Lab_RELATIVE_WP)
    /* This is for ICC profiles which will convert media-relative Lab values
     * to absolute XYZ for the Postscript connection space.
     */
    convertLabtoXYZ(cie, pInfo->relativewhitepoint);
  else if (pInfo->convertLabtoXYZMethod == CONVERT_Lab_WP)
    convertLabtoXYZ(cie, pInfo->whitepoint);
  else
    HQASSERT(pInfo->convertLabtoXYZMethod == CONVERT_Lab_NONE, "Unexpected XYZ to Lab method");

  /* Da da! We now have the CIE 1931 XYZ values in cie[]
     Proceed to convert them to whichever color space is specified by the
     rendering dictionary */

  return TRUE;
}

static Bool transformDEFtoABC( CLINK *pLink, SYSTEMVALUE cie[3] )
{
  register int32    i;
  CLINKciebaseddef  *pInfo;
  SYSTEMVALUE       *rangeabc;

  pInfo = pLink->p.ciebaseddef;

  for (i = 0; i < 3; i++)
    cie[i] = pLink->iColorValues[i];

  /* Narrow to range DEF */
  NARROW3(cie, pInfo->rangedef);

  if ( pInfo->decodedefPresent ) {
    /* Apply DecodeDEF procedures */
    for ( i = 0; i < 3; i++ ) {
      if ( pInfo->decodedeffn[i] ) {
        CALL_C(pInfo->decodedeffn[i], cie[i], NULL);
      }
      else if (pInfo->cpsc_decodedef[i] != NULL) {
        USERVALUE uval = (USERVALUE)cie[i];
        lookup_callpscache(pInfo->cpsc_decodedef[i], uval, &uval);
        cie[i] = (SYSTEMVALUE)uval;
      }
      else {
        if ( !call_psproc(&pInfo->decodedef[i], cie[i], &cie[i], 1) )
          return FALSE;
      }
    }
  }

  /* Narrow to range HIJ */
  NARROW3(cie, pInfo->rangehij);

  /* Do 3D table interpolation */
  interpolatedefspace(pInfo, cie);

  /* Scale result to ABC range */
  rangeabc = pInfo->rangeabc;
  cie[0] = (cie[0]/255.0)*(rangeabc[1] - rangeabc[0]) + rangeabc[0];
  cie[1] = (cie[1]/255.0)*(rangeabc[3] - rangeabc[2]) + rangeabc[2];
  cie[2] = (cie[2]/255.0)*(rangeabc[5] - rangeabc[4]) + rangeabc[4];

  return TRUE;
}

static Bool transformDEFGtoABC( CLINK *pLink, SYSTEMVALUE cie[3] )
{
  register int32    i;
  CLINKciebaseddefg *pInfo;
  SYSTEMVALUE       *rangeabc;
  SYSTEMVALUE       temp[4];

  pInfo = pLink->p.ciebaseddefg;

  for (i = 0; i < 4; i++)
    temp[i] = pLink->iColorValues[i];

  /* Narrow to range DEFG */
  NARROW4(temp, pInfo->rangedefg);

  if ( pInfo->decodedefgPresent ) {
    /* Apply DecodeDEFG procedures */
    for ( i = 0; i < 4; i++ ) {
      if ( pInfo->decodedefgfn[i] ) {
        CALL_C(pInfo->decodedefgfn[i], temp[i], NULL);
      }
      else if (pInfo->cpsc_decodedefg[i] != NULL) {
        USERVALUE uval = (USERVALUE)temp[i];
        lookup_callpscache(pInfo->cpsc_decodedefg[i], uval, &uval);
        temp[i] = (SYSTEMVALUE)uval;
      }
      else {
        if ( !call_psproc(&pInfo->decodedefg[i], temp[i], &temp[i], 1) )
          return FALSE;
      }
    }
  }

  /* Narrow to range HIJK */
  NARROW4(temp, pInfo->rangehijk);

  /* Do 4D table interpolation */
  interpolatedefgspace(pInfo, temp);

  /* Scale result to ABC range */
  rangeabc = pInfo->rangeabc;
  cie[0] = (temp[0]/255.0)*(rangeabc[1] - rangeabc[0]) + rangeabc[0];
  cie[1] = (temp[1]/255.0)*(rangeabc[3] - rangeabc[2]) + rangeabc[2];
  cie[2] = (temp[2]/255.0)*(rangeabc[5] - rangeabc[4]) + rangeabc[4];

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static void interpolatedefspace(CLINKciebaseddef *pInfo, SYSTEMVALUE cie[3])
{
  int32         nh;
  int32         ni;
  int32         nj;
  int32         okh;
  int32         oki;
  int32         okj;
  int32         hoffset;
  int32         ioffset = 0;
  int32         ijoffset;
  int32         i;
  OBJECT*       thetable;
  OBJECT*       harray;
  OBJECT*       thearrays;
  register uint8* ijbyte;
  SYSTEMVALUE*  rangehij;
  SYSTEMVALUE   table[8];
  SYSTEMVALUE   fh;
  SYSTEMVALUE   fi;
  SYSTEMVALUE   fj;

  thetable = oArray(pInfo->deftable);

  nh = oInteger(thetable[0]);
  ni = oInteger(thetable[1]);
  nj = oInteger(thetable[2]);

  if ( (nh > 0) || (ni > 0) || (nj > 0) ) {
    rangehij = pInfo->rangehij;

    /* Compute (h,i,j) */
    cie[0] = ((cie[0] - rangehij[0])/(rangehij[1] - rangehij[0]))*(nh - 1);
    cie[1] = ((cie[1] - rangehij[2])/(rangehij[3] - rangehij[2]))*(ni - 1);
    cie[2] = ((cie[2] - rangehij[4])/(rangehij[5] - rangehij[4]))*(nj - 1);

    okh = cie[0] < (nh - 1);
    oki = cie[1] < (ni - 1);
    okj = cie[2] < (nj - 1);

    thearrays = oArray(thetable[3]);

    hoffset = (int32)cie[0];

    fh = cie[0] - (SYSTEMVALUE)((int32)cie[0]);
    fi = cie[1] - (SYSTEMVALUE)((int32)cie[1]);
    fj = cie[2] - (SYSTEMVALUE)((int32)cie[2]);

    switch ( oType(thearrays[0]) ) {
    case OARRAY:
    case OPACKEDARRAY: /* Array of arrays of 1D strings */
      ioffset = (int32)cie[1];
      ijoffset = 3*(int32)cie[2];

      for ( i = 0; i < 3; i++ ) {
        harray = oArray(thearrays[hoffset]);
        ijbyte = oString(harray[ioffset]) + ijoffset;

        table[0] = (SYSTEMVALUE)(*ijbyte);

        if ( okj ) {
          table[1] = (SYSTEMVALUE)(*(ijbyte + 3));

        } else {
          table[1] = table[0];
        }

        if ( oki ) {
          ijbyte = oString(harray[ioffset + 1]) + ijoffset;

          table[2] = (SYSTEMVALUE)(*ijbyte);

          if ( okj ) {
            table[3] = (SYSTEMVALUE)(*(ijbyte + 3));

          } else {
            table[3] = table[2];
          }

        } else {
          table[2] = table[0];
          table[3] = table[1];
        }

        if ( okh ) {
          harray = oArray(thearrays[hoffset + 1]);
          ijbyte = oString(harray[ioffset]) + ijoffset;

          table[4] = (SYSTEMVALUE)(*ijbyte);

          if ( okj ) {
            table[5] = (SYSTEMVALUE)(*(ijbyte + 3));

          } else {
            table[5] = table[4];
          }

          if ( oki ) {
            ijbyte = oString(harray[ioffset + 1]) + ijoffset;

            table[6] = (SYSTEMVALUE)(*(ijbyte));

            if ( okj ) {
              table[7] = (SYSTEMVALUE)(*(ijbyte + 3));

            } else {
              table[7] = table[6];
            }

          } else {
            table[6] = table[4];
            table[7] = table[5];
          }

        } else {
          table[4] = table[0];
          table[5] = table[1];
          table[6] = table[2];
          table[7] = table[3];
        }

        if ( fj > EPSILON ) {
          table[0] += fj*(table[1] - table[0]);
          table[2] += fj*(table[3] - table[2]);
          table[4] += fj*(table[5] - table[4]);
          table[6] += fj*(table[7] - table[6]);
        }

        if ( fi > EPSILON ) {
          table[0] += fi*(table[2] - table[0]);
          table[4] += fi*(table[6] - table[4]);
        }

        if ( fh > EPSILON ) {
          table[0] += fh*(table[4] - table[0]);
        }

        cie[i] = table[0];

        ijoffset++;
      }

      break;

    case OSTRING: /* Array of 2D strings */
      ijoffset = (3*((int32)cie[1]*nj + (int32)cie[2]));
      for ( i = 0; i < 3; i++ ) {
        ijbyte = oString(thearrays[hoffset]) + ijoffset;

        table[0] = (SYSTEMVALUE)(*ijbyte);

        if ( okj ) {
          table[1] = (SYSTEMVALUE)(*(ijbyte + 3));

        } else {
          table[1] = table[0];
        }

        if ( oki ) {
          table[2] = (SYSTEMVALUE)(*(ijbyte + nj*3));

          if ( okj ) {
            table[3] = (SYSTEMVALUE)(*(ijbyte + (nj + 1)*3));

          } else {
            table[3] = table[2];
          }

        } else {
          table[2] = table[0];
          table[3] = table[1];
        }

        if ( okh ) {
          ijbyte = oString(thearrays[hoffset + 1]) + ijoffset;

          table[4] = (SYSTEMVALUE)(*ijbyte);

          if ( okj ) {
            table[5] = (SYSTEMVALUE)(*(ijbyte + 3));

          } else {
            table[5] = table[4];
          }

          if ( oki ) {
            table[6] = (SYSTEMVALUE)(*(ijbyte + nj*3));

            if ( okj ) {
              table[7] = (SYSTEMVALUE)(*(ijbyte + (nj + 1)*3));

            } else {
              table[7] = table[6];
            }

          } else {
            table[6] = table[4];
            table[7] = table[5];
          }

        } else {
          table[4] = table[0];
          table[5] = table[1];
          table[6] = table[2];
          table[7] = table[3];
        }

        if ( fj > EPSILON ) {
          table[0] += fj*(table[1] - table[0]);
          table[2] += fj*(table[3] - table[2]);
          table[4] += fj*(table[5] - table[4]);
          table[6] += fj*(table[7] - table[6]);
        }

        if ( fi > EPSILON ) {
          table[0] += fi*(table[2] - table[0]);
          table[4] += fi*(table[6] - table[4]);
        }

        if ( fh > EPSILON ) {
          table[0] += fh*(table[4] - table[0]);
        }

        cie[i] = table[0];

        ijoffset++;
      }

      break;
    }
  }

  return;
}

/* ---------------------------------------------------------------------- */

static void interpolatedefgspace(CLINKciebaseddefg *pInfo, SYSTEMVALUE cie[4])
{
  int32         nh;
  int32         ni;
  int32         nj;
  int32         nk;
  int32         okh;
  int32         oki;
  int32         okj;
  int32         okk;
  int32         hoffset;
  int32         ioffset;
  int32         jkoffset;
  int32         i;
  OBJECT*       thetable;
  OBJECT*       thearrays;
  OBJECT*       harray;
  register uint8* jkbyte;
  SYSTEMVALUE*  rangehijk;
  SYSTEMVALUE   table[16];
  SYSTEMVALUE   fh;
  SYSTEMVALUE   fi;
  SYSTEMVALUE   fj;
  SYSTEMVALUE   fk;

  thetable = oArray(pInfo->defgtable);

  nh = oInteger(thetable[0]);
  ni = oInteger(thetable[1]);
  nj = oInteger(thetable[2]);
  nk = oInteger(thetable[3]);

  if ( (nh > 0) || (ni > 0) || (nj > 0) || (nk > 0) ) {
    rangehijk = pInfo->rangehijk;

    /* Compute (h,i,j,k) */
    cie[0] = ((cie[0] - rangehijk[0])/(rangehijk[1] - rangehijk[0]))*(nh - 1);
    cie[1] = ((cie[1] - rangehijk[2])/(rangehijk[3] - rangehijk[2]))*(ni - 1);
    cie[2] = ((cie[2] - rangehijk[4])/(rangehijk[5] - rangehijk[4]))*(nj - 1);
    cie[3] = ((cie[3] - rangehijk[6])/(rangehijk[7] - rangehijk[6]))*(nk - 1);

    okh = cie[0] < (nh - 1);
    oki = cie[1] < (ni - 1);
    okj = cie[2] < (nj - 1);
    okk = cie[3] < (nk - 1);

    thearrays = oArray(thetable[4]);

    hoffset   = (int32)cie[0];
    ioffset   = (int32)cie[1];

    fh = cie[0] - (SYSTEMVALUE)((int32)cie[0]);
    fi = cie[1] - (SYSTEMVALUE)((int32)cie[1]);
    fj = cie[2] - (SYSTEMVALUE)((int32)cie[2]);
    fk = cie[3] - (SYSTEMVALUE)((int32)cie[3]);

    /* Interpolation result is 3D only */
    for ( i = 0; i < 3; i++ ) {

      jkoffset = (3*((int32)cie[2]*nk + (int32)cie[3])) + i; /* offset into string for (a,b,c) */

      harray = oArray(thearrays[hoffset]);
      jkbyte = oString(harray[ioffset]) + jkoffset;

      table[0] = (SYSTEMVALUE)(*jkbyte);

      if ( okk ) {
        table[1] = (SYSTEMVALUE)(*(jkbyte + 3));

      } else {
        table[1] = table[0];
      }

      if ( okj ) {
        table[2] = (SYSTEMVALUE)(*(jkbyte + nk*3));

        if ( okk ) {
          table[3] = (SYSTEMVALUE)(*(jkbyte + (nk + 1)*3));

        } else {
          table[3] = table[2];
        }

      } else {
        table[2] = table[0];
        table[3] = table[1];
      }

      if ( oki ) {
        jkbyte = oString(harray[ioffset + 1]) + jkoffset;

        table[4] = (SYSTEMVALUE)(*jkbyte);

        if ( okk ) {
          table[5] = (SYSTEMVALUE)(*(jkbyte + 3));

        } else {
          table[5] = table[4];
        }

        if ( okj ) {
          table[6] = (SYSTEMVALUE)(*(jkbyte + nk*3));

          if ( okk ) {
            table[7] = (SYSTEMVALUE)(*(jkbyte + (nk + 1)*3));

          } else {
            table[7] = table[6];
          }

        } else {
          table[6] = table[4];
          table[7] = table[5];
        }

      } else {
        table[4] = table[0];
        table[5] = table[1];
        table[6] = table[2];
        table[7] = table[3];
      }

      if ( okh ) {
        harray = oArray(thearrays[hoffset + 1]);
        jkbyte = oString(harray[ioffset]) + jkoffset;

        table[8] = (SYSTEMVALUE)(*jkbyte);

        if ( okk ) {
          table[9] = (SYSTEMVALUE)(*(jkbyte + 3));

        } else {
          table[9] = table[8];
        }

        if ( okj ) {
          table[10] = (SYSTEMVALUE)(*(jkbyte + nk*3));

          if ( okk ) {
            table[11] = (SYSTEMVALUE)(*(jkbyte + (nk + 1)*3));

          } else {
            table[11] = table[10];
          }

        } else {
          table[10] = table[8];
          table[11] = table[9];
        }

        if ( oki ) {
          jkbyte = oString(harray[ioffset + 1]) + jkoffset;

          table[12] = (SYSTEMVALUE)(*jkbyte);

          if ( okk ) {
            table[13] = (SYSTEMVALUE)(*(jkbyte + 3));

          } else {
            table[13] = table[12];
          }

          if ( okj ) {

            table[14] = (SYSTEMVALUE)(*(jkbyte + nk*3));

            if ( okk ) {
              table[15] = (SYSTEMVALUE)(*(jkbyte + (nk + 1)*3));

            } else {
              table[15] = table[14];
            }

          } else {
            table[14] = table[12];
            table[15] = table[13];
          }

        } else {
          table[12] = table[8];
          table[13] = table[9];
          table[14] = table[10];
          table[15] = table[11];
        }

      } else {
        table[8] = table[0];
        table[9] = table[1];
        table[10] = table[2];
        table[11] = table[3];
        table[12] = table[4];
        table[13] = table[5];
        table[14] = table[6];
        table[15] = table[7];
      }

      if ( fk > EPSILON ) {
        table[0]  += fk*(table[1] - table[0]);
        table[2]  += fk*(table[3] - table[2]);
        table[4]  += fk*(table[5] - table[4]);
        table[6]  += fk*(table[7] - table[6]);
        table[8]  += fk*(table[9] - table[8]);
        table[10] += fk*(table[11] - table[10]);
        table[12] += fk*(table[13] - table[12]);
        table[14] += fk*(table[15] - table[14]);
      }

      if ( fj > EPSILON ) {
        table[0]  += fj*(table[2] - table[0]);
        table[4]  += fj*(table[6] - table[4]);
        table[8]  += fj*(table[10] - table[8]);
        table[12] += fj*(table[14] - table[12]);
      }

      if ( fi > EPSILON ) {
        table[0]  += fi*(table[4] - table[0]);
        table[8]  += fi*(table[12] - table[8]);
      }

      if ( fh > EPSILON ) {
        table[0]  += fh*(table[8] - table[0]);
      }

      cie[i] = table[0];
    }
  }

  /* Clear 4th colour component */
  cie[3] = (SYSTEMVALUE)0.0;

  return;
}

/* ---------------------------------------------------------------------- */
static Bool extract_ciebaseda( CLINKciebaseda    *pInfo,
                               OBJECT            *colorSpaceObject )
{
  /* theo is a pointer to the first object in the array
   * e.g. [ /CIEBasedA <<...>> ]
   * We know the length of the array is correct;
   * the dictionary contains all the conversion code fro cie color spaces;
   * the default color is 0 unless that is not in range, in which case
   *   the "closest" color is chosen;
   */

  static NAMETYPEMATCH thematch[] = {
    { NAME_RangeA | OOPTIONAL,             2, { OARRAY, OPACKEDARRAY }}, /* 0 */
    { NAME_DecodeA | OOPTIONAL,            2,                            /* 1 */
      { OARRAY | EXECUTABLE, OPACKEDARRAY | EXECUTABLE }},
    { NAME_MatrixA | OOPTIONAL,            2, { OARRAY, OPACKEDARRAY }}, /* 2 */
    DUMMY_END_MATCH
  };

  OBJECT    *theo;

  theo = oArray(*colorSpaceObject) ;

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           (oName(*theo) == &system_names[NAME_CIEBasedA]),
           "extract_ciebaseda should be called only for NAME_CIEBasedA");

  ++theo ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , thematch ))
    return FALSE ;

  /* PS2_TO_PS3_COLORCACHE NOP */

  theo = thematch[ 0 ].result ; /* RangeA */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangeabc, 2))
      return FALSE;
  }

  theo = thematch[ 1 ].result ; /* DecodeA */
  if ( theo != NULL ) {
    pInfo->decodeabc[0] = *theo;
    pInfo->decodeabcPresent = TRUE;
    pInfo->decodeabcfn[0] = cc_findCIEProcedure(&pInfo->decodeabc[0]);
  }

  theo = thematch[ 2 ].result ; /* MatrixA */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->matrixabc, 3))
      return FALSE;
    pInfo->matrixabcPresent = TRUE;
  }

  return extract_ciebasedabcTailEnd( pInfo , colorSpaceObject ) ;
}

/* ---------------------------------------------------------------------- */
static Bool extract_ciebasedabc( CLINKciebasedabc  *pInfo,
                                 OBJECT            *colorSpaceObject )
{
  /* theo is a pointer to the first object in the array
   * e.g. [ /CIEBasedABC <<...>> ]
   * We know the length of the array is correct;
   * the dictionary contains all the conversion code for cie color spaces;
   * the default color is 0,0,0 unless that is not in range, in which case
   *   the "closest" color is chosen;
   */

  static NAMETYPEMATCH thematch[] = {
    { NAME_RangeABC | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }}, /* 0 */
    { NAME_DecodeABC | OOPTIONAL,          2,
      { OARRAY | EXECUTABLE, OPACKEDARRAY | EXECUTABLE }},               /* 1 */
    { NAME_MatrixABC | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }}, /* 2 */
    DUMMY_END_MATCH
  } ;

  int32     i;
  OBJECT    *theo;

  theo = oArray( *colorSpaceObject ) ;

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           ((oName(*theo) == &system_names[NAME_CIEBasedABC]) ||
            (oName(*theo) == &system_names[NAME_CIEBasedDEF]) ||
            (oName(*theo) == &system_names[NAME_CIEBasedDEFG])),
           "extract_ciebasedabc should be called only for NAME_CIEBasedABC(DEF)(DEFG)");

  ++theo ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , thematch ))
    return FALSE ;

  /* PS2_TO_PS3_COLORCACHE NOP */

  theo = thematch[ 0 ].result ; /* RangeABC */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangeabc, 6))
      return FALSE;
  }

  theo = thematch[ 1 ].result ; /* DecodeABC */
  if ( theo != NULL ) {
    if (!cc_extractP( pInfo->decodeabc , 3 , theo ))
      return FALSE;
    pInfo->decodeabcPresent = TRUE;

    for (i = 0; i < 3; i++)
      pInfo->decodeabcfn[i] = cc_findCIEProcedure(&pInfo->decodeabc[i]);

    /* True if there's no procedure, ie. just a matrix, false otherwise. */
    pInfo->isPhotoshopRGB =
     ( uint8 )(( theLen( pInfo->decodeabc[ 0 ]) == 0 ) &&
               ( theLen( pInfo->decodeabc[ 1 ]) == 0 ) &&
               ( theLen( pInfo->decodeabc[ 2 ]) == 0 )) ;
  }
  else {
    pInfo->isPhotoshopRGB = TRUE;

  }

  theo = thematch[ 2 ].result ; /* MatrixABC */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->matrixabc, 9))
      return FALSE;
    pInfo->matrixabcPresent = TRUE;
  }

  if (!extract_ciebasedabcTailEnd(pInfo, colorSpaceObject))
    return FALSE;

  pInfo->isPhotoshopRGB = pInfo->isPhotoshopRGB && pInfo->matrixlmnPresent;
    /* A photoshop RGB must have MatrixLMN present. A case where we were previously
       thinking it was Photoshop RGB was as the Indexed spaced alternate, which is
       actually XYZ */

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool extract_ciebasedabcTailEnd( CLINKciebasedabc  *pInfo,
                                        OBJECT            *colorSpaceObject )
{
  static NAMETYPEMATCH thematch[] = {
    { NAME_RangeLMN | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }}, /* 0 */
    { NAME_DecodeLMN | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }}, /* 1 */
    { NAME_MatrixLMN | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }}, /* 2 */
    { NAME_WhitePoint,                     2, { OARRAY, OPACKEDARRAY }}, /* 3 */
    { NAME_BlackPoint | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }}, /* 4 */
    { NAME_RelativeWhitePoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }}, /* 5 */
    { NAME_RelativeBlackPoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }}, /* 6 */
    { NAME_ConvertLabtoXYZ | OOPTIONAL,    2, { ONAME, OBOOLEAN }},      /* 7 */
    { NAME_UniqueID | OOPTIONAL,           1, { OINTEGER }},             /* 8 */
    { NAME_XUID     | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }}, /* 9 */

    DUMMY_END_MATCH
  } ;

  int32     i;
  OBJECT    *theo;

  theo = oArray(*colorSpaceObject) ;

  ++theo ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , thematch ))
    return FALSE ;

  theo = thematch[ 0 ].result ; /* RangeLMN */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangelmn, 6))
      return FALSE;
  }

  theo = thematch[ 1 ].result ; /* DecodeLMN */
  if ( theo != NULL ) {
    if (!cc_extractP( pInfo->decodelmn, 3 , theo ))
      return FALSE;
    pInfo->decodelmnPresent = TRUE;

    for (i = 0; i < 3; i++)
      pInfo->decodelmnfn[i] = cc_findCIEProcedure(&pInfo->decodelmn[i]);
  }

  theo = thematch[ 2 ].result ; /* MatrixLMN */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->matrixlmn, 9))
      return FALSE;
    pInfo->matrixlmnPresent = TRUE;
  }

  theo = thematch[ 3 ].result ; /* WhitePoint: required */
  if (!object_get_numeric_array(theo, pInfo->whitepoint, 3))
    return FALSE;
  if ((pInfo->whitepoint[0] == 0) ||
      (pInfo->whitepoint[1] == 0) ||
      (pInfo->whitepoint[2] == 0))
    return error_handler(RANGECHECK);

  theo = thematch[ 4 ].result ; /* BlackPoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->blackpoint, 3))
      return FALSE;
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->blackpoint[i] = 0.0;
  }

  theo = thematch[ 5 ].result ; /* RelativeWhitePoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->relativewhitepoint, 3))
      return FALSE;
    if ((pInfo->relativewhitepoint[0] == 0) ||
        (pInfo->relativewhitepoint[1] == 0) ||
        (pInfo->relativewhitepoint[2] == 0))
      return error_handler(RANGECHECK);
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->relativewhitepoint[i] = pInfo->whitepoint[i];
  }

  theo = thematch[ 6 ].result ; /* RelativeBlackPoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->relativeblackpoint, 3))
      return FALSE;
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->relativeblackpoint[i] = pInfo->blackpoint[i];
  }

  theo = thematch[ 7 ].result ; /* ConvertLabtoXYZ */
  if ( theo != NULL ) {
    if (oType(*theo) == ONAME) {
      if (oNameNumber(*theo) == NAME_RelativeWhitePoint)
        pInfo->convertLabtoXYZMethod = CONVERT_Lab_RELATIVE_WP;
      else if (oNameNumber(*theo) == NAME_WhitePoint)
        pInfo->convertLabtoXYZMethod = CONVERT_Lab_WP;
      else
        return error_handler(RANGECHECK);
    }
    else if (oBool(*theo)) {
      /* This is a legacy method that we have to support */
      pInfo->convertLabtoXYZMethod = CONVERT_Lab_RELATIVE_WP;
    }
  }

  return cc_create_xuids( thematch[ 8 ].result ,
                          thematch[ 9 ].result ,
                          & pInfo->nXUIDs ,
                          & pInfo->pXUIDs );
}

/* ---------------------------------------------------------------------- */
static Bool extract_ciebaseddef( CLINKciebaseddef  *pInfo,
                                 OBJECT            *colorSpaceObject )
{
  static NAMETYPEMATCH thematchdef[] = {
    { NAME_RangeDEF | OOPTIONAL,  2, { OARRAY, OPACKEDARRAY }}, /* 0 */
    { NAME_DecodeDEF | OOPTIONAL, 2,
      { OARRAY | EXECUTABLE, OPACKEDARRAY | EXECUTABLE }},      /* 1 */
    { NAME_RangeHIJ | OOPTIONAL,  2, { OARRAY, OPACKEDARRAY }}, /* 2 */
    { NAME_Table,                 2, { OARRAY, OPACKEDARRAY }}, /* 3 */
    { NAME_RangeABC | OOPTIONAL,  2, { OARRAY, OPACKEDARRAY }}, /* 4 */

    { NAME_UniqueID | OOPTIONAL,  1, { OINTEGER }},             /* 5 */
    { NAME_XUID     | OOPTIONAL,  2, { OARRAY, OPACKEDARRAY }}, /* 6 */

    DUMMY_END_MATCH
  };

  int32     i;
  int32     sizes[ 3 ] ;
  int32     tableform ;
  OBJECT    *theo;

  theo = oArray( *colorSpaceObject ) ;

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           (oName(*theo) == &system_names[NAME_CIEBasedDEF]),
           "extract_ciebaseddef should be called only for NAME_CIEBasedDEF");

  ++theo ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , thematchdef ))
    return FALSE ;

  theo = thematchdef[ 0 ].result ; /* RangeDEF */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangedef, 6))
      return FALSE;
  }

  theo = thematchdef[ 1 ].result ; /* DecodeDEF */
  if ( theo != NULL ) {
    if (!cc_extractP( pInfo->decodedef , 3 , theo ))
      return FALSE;
    pInfo->decodedefPresent = TRUE;

    for (i = 0; i < 3; i++)
      pInfo->decodedeffn[i] = cc_findCIEProcedure(&pInfo->decodedef[i]);
  }

  theo = thematchdef[ 2 ].result ; /* RangeHIJ */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangehij, 6))
      return FALSE;
  }

  theo = thematchdef[ 3 ].result; /* Table - based on CRD table handling code */
  Copy(&pInfo->deftable, theo);
  if (( int32 )theLen(*theo) != 4 )
    return error_handler( RANGECHECK ) ;

  theo = oArray( *theo ) ;

  /* Pick up the table dimensions. */
  for ( i = 0 ; i < 3 ; ++i ) {
    if ( oType( theo[i] ) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    sizes[ i ] = oInteger( theo[i] ) ;
  }

  /* Check that we have a readable (packed) array with right number of arrays. */
  if ( oType( theo[3] ) != OARRAY && oType( theo[3] ) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead( theo[3] ))
    return error_handler( INVALIDACCESS ) ;
  if (( int32 )theLen( theo[ 3 ] ) != sizes[ 0 ] )
    return error_handler( RANGECHECK ) ;

  /* Base table interpretation on first entry in array. */
  tableform = 0 ;

  /* Pick up array of strings. */
  theo = oArray( theo[3] ) ;
  for ( i = 0 ; i < sizes[ 0 ] ; ++i ) {
    int32 j ;
    OBJECT *subo ;

    if ( tableform == 0 ) {
      /* Decide what form the 3D table is in */
      switch ( oType( theo[i] )) {
      case OARRAY:
      case OPACKEDARRAY:
        tableform = 1 ; break ;
      case OSTRING:
        tableform = 2 ; break ;
      default:
        return error_handler( TYPECHECK ) ;
      }
    }

    /* Check that contents of array of right type for table form. */
    switch ( tableform ) {
    case 1:
      if ( oType( theo[i] ) != OARRAY && oType( theo[i] ) != OPACKEDARRAY )
        return error_handler( TYPECHECK ) ;
      if ( ! oCanRead( theo[i] ))
        return error_handler( INVALIDACCESS ) ;
      if (( int32 )theLen( theo[ i ] ) != sizes[ 1 ] )
        return error_handler( RANGECHECK ) ;

      subo = oArray( theo[i] ) ;
      for ( j = 0 ; j < sizes[ 1 ] ; ++j ) {
        if ( oType( subo[j] ) != OSTRING )
          return error_handler( TYPECHECK ) ;
        if ( ! oCanRead( subo[j] ))
          return error_handler( INVALIDACCESS ) ;

        if (( int32 )theLen( subo[ j ] ) != ( 3 * sizes[ 2 ] ))
          return error_handler( RANGECHECK ) ;
      }
      break ;
    case 2:
      if ( oType( theo[i] ) != OSTRING )
        return error_handler( TYPECHECK ) ;
      if ( ! oCanRead( theo[i] ))
        return error_handler( INVALIDACCESS ) ;

      if (( int32 )theLen( theo[ i ] ) != ( 3 * sizes[ 1 ] * sizes[ 2 ] ))
        return error_handler( RANGECHECK ) ;
      break ;
    }
  }

  theo = thematchdef[ 4 ].result ; /* RangeABC */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangeabc, 6))
      return FALSE;
  }

  return cc_create_xuids( thematchdef[ 5 ].result ,
                          thematchdef[ 6 ].result ,
                          & pInfo->nXUIDs ,
                          & pInfo->pXUIDs ) ;
}

/* ---------------------------------------------------------------------- */
static Bool extract_ciebaseddefg( CLINKciebaseddefg  *pInfo,
                                  OBJECT             *colorSpaceObject )
{
  static NAMETYPEMATCH thematchdefg[] = {
    { NAME_RangeDEFG | OOPTIONAL,  2, { OARRAY, OPACKEDARRAY }}, /* 0 */
    { NAME_DecodeDEFG | OOPTIONAL, 2,
      { OARRAY | EXECUTABLE, OPACKEDARRAY | EXECUTABLE }},       /* 1 */
    { NAME_RangeHIJK | OOPTIONAL,  2, { OARRAY, OPACKEDARRAY }}, /* 2 */
    { NAME_Table,                  2, { OARRAY, OPACKEDARRAY }}, /* 3 */
    { NAME_RangeABC | OOPTIONAL,   2, { OARRAY, OPACKEDARRAY }}, /* 4 */

    { NAME_UniqueID | OOPTIONAL,   1, { OINTEGER }},             /* 5 */
    { NAME_XUID     | OOPTIONAL,   2, { OARRAY, OPACKEDARRAY }}, /* 6 */

    DUMMY_END_MATCH
  };

  int32     i;
  int32     sizes[ 4 ] ;
  int32     tableform ;
  OBJECT    *theo;

  theo = oArray(*colorSpaceObject);

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           (oName(*theo) == &system_names[NAME_CIEBasedDEFG]),
           "extract_ciebaseddefg should be called only for NAME_CIEBasedDEFG");

  ++theo ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , thematchdefg ))
    return FALSE ;

  theo = thematchdefg[ 0 ].result ; /* RangeDEFG */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangedefg, 8))
      return FALSE;
  }

  theo = thematchdefg[ 1 ].result ; /* DecodeDEG */
  if ( theo != NULL ) {
    if (!cc_extractP( pInfo->decodedefg , 4 , theo ))
      return FALSE;
    pInfo->decodedefgPresent = TRUE;

    for (i = 0; i < 4; i++)
      pInfo->decodedefgfn[i] = cc_findCIEProcedure(&pInfo->decodedefg[i]);
  }

  theo = thematchdefg[ 2 ].result ; /* RangeHIJK */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangehijk, 8))
      return FALSE;
  }

  theo = thematchdefg[ 3 ].result; /* Table - based on CRD table handling code */
  Copy(&pInfo->defgtable, theo);
  if (( int32 )theLen(*theo) != 5 )
    return error_handler( RANGECHECK ) ;

  theo = oArray(*theo) ;

  /* Pick up the table dimensions. */
  for ( i = 0 ; i < 4 ; ++i ) {
    if ( oType( theo[i] ) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    sizes[ i ] = oInteger( theo[i] ) ;
  }

  /* Check that we have a readable (packed) array with right number of arrays. */
  if ( oType( theo[4] ) != OARRAY && oType( theo[4] ) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead( theo[4] ))
    return error_handler( INVALIDACCESS ) ;
  if (( int32 )theLen( theo[4] ) != sizes[ 0 ] )
    return error_handler( RANGECHECK ) ;

  /* Base table interpretation on first entry in array. */
  tableform = 0 ;

  /* Pick up array of strings. */
  theo = oArray(theo[4]) ;
  for ( i = 0 ; i < sizes[ 0 ] ; ++i ) {
    int32 j ;
    OBJECT *subo ;

    /* Check that we have readable arrays of strings of correct length. */
    if ( oType( theo[i] ) != OARRAY && oType( theo[i] ) != OPACKEDARRAY )
      return error_handler( TYPECHECK ) ;
    if ( ! oCanRead( theo[i] ))
      return error_handler( INVALIDACCESS ) ;
    if (( int32 )theLen( theo[ i ] ) != sizes[ 1 ] )
      return error_handler( RANGECHECK ) ;

    subo = oArray( theo[i] ) ;
    for ( j = 0 ; j < sizes[ 1 ] ; ++j ) {
      if ( oType( subo[j] ) != OSTRING )
        return error_handler( TYPECHECK ) ;
      if ( ! oCanRead( theo[i] ))
        return error_handler( INVALIDACCESS ) ;
      if (( int32 )theLen( subo[j] ) != ( 3 * sizes[ 2 ] * sizes[ 3 ] ))
        return error_handler( RANGECHECK ) ;
    }
  }

  theo = thematchdefg[ 4 ].result ; /* RangeABC */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->rangeabc , 6))
      return FALSE;
  }

  return cc_create_xuids( thematchdefg[ 5 ].result ,
                          thematchdefg[ 6 ].result ,
                          & pInfo->nXUIDs ,
                          & pInfo->pXUIDs ) ;
}

/* eof */

/* Log stripped */
