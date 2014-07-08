/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gsctable.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Table based color spaces.
 */

#include "core.h"

#include "blitcolort.h"         /* BLIT_MAX_COLOR_CHANNELS */
#include "control.h"            /* error_handler */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "fileio.h"             /* isIOpenFileFilter */
#include "tables.h"             /* HighOrder2Bytes */
#include "gcscan.h"             /* ps_scan_field */
#include "hqxfonts.h"           /* hqxNonPSSetup */
#include "mmcompat.h"           /* mm_alloc_with_header */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_* */
#include "objecth.h"            /* OBJECT */
#include "swerrors.h"           /* TYPECHECK */
#include "hqmemset.h"

#include "gs_colorpriv.h"       /* CLINK */
#include "gsccmmpriv.h"         /* cc_initTransformInfo */
#include "gschead.h"            /* gsc_getcolorspacesizeandtype */

#include "gsctable.h"           /* extern's */


#define CIETABLE_MAX_INPUT_CHANNELS 4

#define MAX_VALUE_IN_NORMALIZED_TABLE ((SYSTEMVALUE)0x8000)

struct CLINKcietableabcd {
  cc_counter_t  refCnt;
  int32         linkType;
  size_t        structSize;

  uint32        colorspaceIsValid;

  uint8         matrixPresent;
  uint8         transferInputPresent;
  uint8         tablePresent;
  uint8         transferOutputPresent;

  OBJECT        underlyingColorSpaceObject;
  COLORSPACE_ID underlyingColorSpace;
  int32         dimensions;
  int32         underlyingDimensions;

  SYSTEMVALUE   rangeABCD[CIETABLE_MAX_INPUT_CHANNELS*2];

  SYSTEMVALUE   matrix[9];

  int32         transferinput_sizes[CIETABLE_MAX_INPUT_CHANNELS];
  SYSTEMVALUE   *transferinput[CIETABLE_MAX_INPUT_CHANNELS];
  SYSTEMVALUE   rangeEFGH[CIETABLE_MAX_INPUT_CHANNELS*2];

  int32         sizes[CIETABLE_MAX_INPUT_CHANNELS];
  OBJECT        file;
  uint16        *table_data;
  SYSTEMVALUE   *rangetable;
  SYSTEMVALUE   *bases, * ranges; /* alternate form of rangetable, for optimization */

  int32         *transferoutput_sizes;
  SYSTEMVALUE   **transferoutput;

  /* XUID/UniqueID. */
  int32          nXUIDs ;
  int32         *pXUIDs ;

  TRANSFORM_LINK_INFO *nextInfo;
};

/* ---------------------------------------------------------------------- */

static void  cietableabcd_destroy(CLINK *pLink);
static Bool cietableabcd_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool cietableabcd_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t cietableabcd_scan( mps_ss_t ss, CLINK *pLink );
static size_t cietableabcdStructSize(void);
static void cietableabcdUpdatePtrs(CLINK *pLink, CLINKcietableabcd *cietableabcdInfo);

#if defined( ASSERT_BUILD )
static void cietableabcdAssertions(CLINK *pLink);
#else
#define cietableabcdAssertions(_pLink) EMPTY_STATEMENT()
#endif

static Bool createcietableinfo( CLINKcietableabcd  **cietableInfo,
                                COLORSPACE_ID      *outputColorSpaceId,
                                OBJECT             *colorSpaceObject,
                                int32              linkType,
                                GS_COLORinfo       *colorInfo );
static void destroycietableinfo( CLINKcietableabcd  **cietableInfo );
static void reservecietableinfo( CLINKcietableabcd  *cietableInfo );
static size_t cietableInfoStructSize(void);

#if defined( ASSERT_BUILD )
static void cietableInfoAssertions(CLINKcietableabcd *pInfo);
#else
#define cietableInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static Bool transformTableABCD( CLINK *pLink, SYSTEMVALUE cie[] );

static Bool extract_cietableabcd( GS_COLORinfo      *colorInfo,
                                  CLINKcietableabcd *pInfo,
                                  OBJECT            *colorSpaceObject );
static Bool store_transfer_values(OBJECT       *theo,
                   int32        channels,
                   SYSTEMVALUE  **Transfers,
                   int32        *TransferSizes);
static uint16 *read_table_data(OBJECT       *fileObject,
                   int32        sizes[],
                   int32        dimensions,
                   int32        underlying_dimensions,
                   SYSTEMVALUE  *bases,
                               SYSTEMVALUE  *ranges);

#ifdef dump_table_data
static void verify_table_data(uint16       *table_dataPtr,
                              int32        dimensions,
                              int32        *table_dimensions,
                              int32        underlying_dimensions,
                              SYSTEMVALUE  *bases,
                              SYSTEMVALUE  * ranges);
#endif

static CLINKfunctions CLINKcietableabcd_functions =
{
  cietableabcd_destroy,
  cietableabcd_invokeSingle,
  NULL /* cietableabcd_invokeBlock */,
  cietableabcd_scan
};

/* ---------------------------------------------------------------------- */

/*
 * Cietablea + Cietableabc + Cietableabcd Link Data Access Functions
 * =================================================================
 *
 * NB. There are different create link functions, but all other functions are
 *     common between CIETableA, CIETableABC and CIETableABCD.
 */

CLINK *cc_cietablea_create(CLINKcietablea     *cietableaInfo,
                           COLORSPACE_ID      *outputColorSpaceId,
                           int32              *outputDimensions)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEcietablea (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(cietableaInfo != NULL, "cietableaInfo NULL");

  nXUIDs = cietableaInfo->nXUIDs ;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_CIETableA,
                           cietableaInfo->underlyingColorSpace,
                           CL_TYPEcietablea,
                           cietableabcdStructSize(),
                           &CLINKcietableabcd_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  cietableabcdUpdatePtrs(pLink, cietableaInfo);
  cc_reservecietableainfo(cietableaInfo);

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = cietableaInfo->pXUIDs[nXUIDs];

  cietableabcdAssertions(pLink);

  *outputColorSpaceId = cietableaInfo->underlyingColorSpace;
  *outputDimensions = cietableaInfo->underlyingDimensions;

  return pLink;
}

CLINK *cc_cietableabc_create(CLINKcietableabc   *cietableabcInfo,
                             COLORSPACE_ID      *outputColorSpaceId,
                             int32              *outputDimensions)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEcietableabc (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(cietableabcInfo != NULL, "cietableabcInfo NULL");

  nXUIDs = cietableabcInfo->nXUIDs ;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_CIETableABC,
                           cietableabcInfo->underlyingColorSpace,
                           CL_TYPEcietableabc,
                           cietableabcdStructSize(),
                           &CLINKcietableabcd_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  cietableabcdUpdatePtrs(pLink, cietableabcInfo);
  cc_reservecietableabcinfo(cietableabcInfo);

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = cietableabcInfo->pXUIDs[nXUIDs];

  cietableabcdAssertions(pLink);

  *outputColorSpaceId = cietableabcInfo->underlyingColorSpace;
  *outputDimensions = cietableabcInfo->underlyingDimensions;

  return pLink;
}

CLINK *cc_cietableabcd_create(CLINKcietableabcd  *cietableabcdInfo,
                              COLORSPACE_ID      *outputColorSpaceId,
                              int32              *outputDimensions)
{
  int32 nXUIDs;
  CLINK *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEcietableabcd (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT(cietableabcdInfo != NULL, "cietableabcdInfo NULL");

  nXUIDs = cietableabcdInfo->nXUIDs ;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_CIETableABCD,
                           cietableabcdInfo->underlyingColorSpace,
                           CL_TYPEcietableabcd,
                           cietableabcdStructSize(),
                           &CLINKcietableabcd_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  cietableabcdUpdatePtrs(pLink, cietableabcdInfo);
  cc_reservecietableabcdinfo(cietableabcdInfo);

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = cietableabcdInfo->pXUIDs[nXUIDs];

  cietableabcdAssertions(pLink);

  *outputColorSpaceId = cietableabcdInfo->underlyingColorSpace;
  *outputDimensions = cietableabcdInfo->underlyingDimensions;

  return pLink;
}

static void cietableabcd_destroy(CLINK *pLink)
{
  cietableabcdAssertions(pLink);

  destroycietableinfo(&pLink->p.cietableabcd);
  cc_common_destroy(pLink);
}

static Bool cietableabcd_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  CLINKcietableabcd *pInfo = pLink->p.cietableabcd;
  int32             i;
  int32             ondims;
  SYSTEMVALUE       tmp_oValues[BLIT_MAX_COLOR_CHANNELS];

  cietableabcdAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  if ( ! transformTableABCD(pLink, tmp_oValues))
    return FALSE ;

  ondims = pInfo->underlyingDimensions ;
  switch ( pLink->oColorSpace ) {
  case SPACE_DeviceN:
  case SPACE_Separation:
  case SPACE_DeviceGray:
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
    for ( i = 0 ; i < ondims ; ++i ) {
      oColorValues[ i ] = ( USERVALUE )tmp_oValues[ i ] ;
      NARROW_01( oColorValues[ i ] ) ;
    }
    break ;
  default:
    for ( i = 0 ; i < ondims ; ++i )
      oColorValues[ i ] = ( USERVALUE )tmp_oValues[ i ] ;
    break ;
  }

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cietableabcd_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  cietableabcdAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */


/* cietableabcd_scan - scan the ABCD table section of a CLINK */
static mps_res_t cietableabcd_scan( mps_ss_t ss, CLINK *pLink )
{
  mps_res_t res;

  res = ps_scan_field( ss, &pLink->p.cietableabcd->underlyingColorSpaceObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &pLink->p.cietableabcd->file );
  return res;
}


void cc_getCieTableRange( CLINKcietableabcd *cietableabcdInfo,
                          int32 index,
                          SYSTEMVALUE range[2] )
{
  cietableInfoAssertions(cietableabcdInfo);
  HQASSERT(index < cietableabcdInfo->dimensions, "Invalid channel index");

  range[0] = cietableabcdInfo->rangeABCD[index * 2];
  range[1] = cietableabcdInfo->rangeABCD[index * 2 + 1];
}


static size_t cietableabcdStructSize(void)
{
  return 0;
}

static void cietableabcdUpdatePtrs(CLINK              *pLink,
                                   CLINKcietableabcd  *cietableabcdInfo)
{
  pLink->p.cietableabcd = cietableabcdInfo;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void cietableabcdAssertions(CLINK *pLink)
{
  int32 linkType = CL_TYPEcietablea;

  if ((pLink->linkType == CL_TYPEcietableabcd) ||
      (pLink->linkType == CL_TYPEcietableabc)) {
    linkType = pLink->linkType;
  }

  cc_commonAssertions(pLink,
                      linkType,
                      cietableabcdStructSize(),
                      &CLINKcietableabcd_functions);

  if (pLink->p.cietableabcd != NULL) {
    cietableInfoAssertions(pLink->p.cietableabcd);
    HQASSERT(pLink->linkType == pLink->p.cietableabcd->linkType, "Bad linkType");
    if (pLink->linkType == CL_TYPEcietablea)
      HQASSERT(pLink->iColorSpace == SPACE_CIETableA, "Bad input color space");
    else if (pLink->linkType == CL_TYPEcietableabc)
      HQASSERT(pLink->iColorSpace == SPACE_CIETableABC, "Bad input color space");
    else
      HQASSERT(pLink->iColorSpace == SPACE_CIETableABCD, "Bad input color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Cietablea Info Data Access Functions
 * ======================================
 */
Bool cc_createcietableainfo( CLINKcietablea   **cietableaInfo,
                             COLORSPACE_ID    *outputColorSpaceId,
                             OBJECT           *colorSpaceObject,
                             GS_COLORinfo     *colorInfo )
{
  return createcietableinfo(cietableaInfo,
                            outputColorSpaceId,
                            colorSpaceObject,
                            CL_TYPEcietablea,
                            colorInfo );
}

void cc_destroycietableainfo( CLINKcietablea   **cietableaInfo )
{
  HQASSERT((*cietableaInfo)->linkType == CL_TYPEcietablea, "linkType not set");
  destroycietableinfo(cietableaInfo);
  return;
}

void cc_reservecietableainfo( CLINKcietablea   *cietableaInfo )
{
  HQASSERT(cietableaInfo->linkType == CL_TYPEcietablea, "linkType not set");
  reservecietableinfo(cietableaInfo);
}

TRANSFORM_LINK_INFO *cc_nextCIETableAInfo(CLINKcietablea  *cietableaInfo)
{
  cietableInfoAssertions(cietableaInfo);

  return cietableaInfo->nextInfo;
}

int32 cc_cietablea_nOutputChannels(CLINKcietablea  *cietableaInfo)
{
  cietableInfoAssertions(cietableaInfo);

  return cietableaInfo->underlyingDimensions;
}

/* ---------------------------------------------------------------------- */

/*
 * Cietableabc Info Data Access Functions
 * ======================================
 */
Bool cc_createcietableabcinfo( CLINKcietableabc   **cietableabcInfo,
                               COLORSPACE_ID      *outputColorSpaceId,
                               OBJECT             *colorSpaceObject,
                               GS_COLORinfo       *colorInfo )
{
  return createcietableinfo(cietableabcInfo,
                            outputColorSpaceId,
                            colorSpaceObject,
                            CL_TYPEcietableabc,
                            colorInfo );
}

void cc_destroycietableabcinfo( CLINKcietableabc   **cietableabcInfo )
{
  HQASSERT((*cietableabcInfo)->linkType == CL_TYPEcietableabc, "linkType not set");
  destroycietableinfo(cietableabcInfo);
  return;
}

void cc_reservecietableabcinfo( CLINKcietableabc   *cietableabcInfo )
{
  HQASSERT(cietableabcInfo->linkType == CL_TYPEcietableabc, "linkType not set");
  reservecietableinfo(cietableabcInfo);
}

TRANSFORM_LINK_INFO *cc_nextCIETableABCInfo(CLINKcietableabc  *cietableabcInfo)
{
  cietableInfoAssertions(cietableabcInfo);

  return cietableabcInfo->nextInfo;
}

int32 cc_cietableabc_nOutputChannels(CLINKcietableabc  *cietableabcInfo)
{
  cietableInfoAssertions(cietableabcInfo);

  return cietableabcInfo->underlyingDimensions;
}

/* ---------------------------------------------------------------------- */

/*
 * Cietableabcd Info Data Access Functions
 * =======================================
 */
Bool cc_createcietableabcdinfo( CLINKcietableabcd  **cietableabcdInfo,
                                COLORSPACE_ID      *outputColorSpaceId,
                                OBJECT             *colorSpaceObject,
                                GS_COLORinfo       *colorInfo )
{
  return createcietableinfo(cietableabcdInfo,
                            outputColorSpaceId,
                            colorSpaceObject,
                            CL_TYPEcietableabcd,
                            colorInfo );
}

void cc_destroycietableabcdinfo( CLINKcietableabcd  **cietableabcdInfo )
{
  HQASSERT((*cietableabcdInfo)->linkType == CL_TYPEcietableabcd, "linkType not set");
  destroycietableinfo(cietableabcdInfo);
}

void cc_reservecietableabcdinfo( CLINKcietableabcd *cietableabcdInfo )
{
  HQASSERT(cietableabcdInfo->linkType == CL_TYPEcietableabcd, "linkType not set");
  reservecietableinfo(cietableabcdInfo);
}

TRANSFORM_LINK_INFO *cc_nextCIETableABCDInfo(CLINKcietableabcd  *cietableabcdInfo)
{
  cietableInfoAssertions(cietableabcdInfo);

  return cietableabcdInfo->nextInfo;
}

int32 cc_cietableabcd_nOutputChannels(CLINKcietableabcd  *cietableabcdInfo)
{
  cietableInfoAssertions(cietableabcdInfo);

  return cietableabcdInfo->underlyingDimensions;
}

/* ---------------------------------------------------------------------- */

/*
 * Cietablea + Cietableabc + Cietableabcd Info Common Data Access Functions
 * ============================================================
 */
static Bool createcietableinfo( CLINKcietableabcd  **cietableInfo,
                                COLORSPACE_ID      *outputColorSpaceId,
                                OBJECT             *colorSpaceObject,
                                int32              linkType,
                                GS_COLORinfo       *colorInfo )
{
  int32               i;
  CLINKcietableabcd   *pInfo;
  size_t              structSize;

  structSize = cietableInfoStructSize();

  pInfo = mm_sac_alloc(mm_pool_color, structSize, MM_ALLOC_CLASS_NCOLOR);
  if (pInfo == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)pInfo, (int)structSize);

  pInfo->refCnt = 1;
  pInfo->linkType = linkType;
  pInfo->structSize = structSize;
  pInfo->colorspaceIsValid = FALSE;

  pInfo->matrixPresent = FALSE;
  pInfo->transferInputPresent = FALSE;
  pInfo->tablePresent = FALSE;
  pInfo->transferOutputPresent = FALSE;

  pInfo->underlyingColorSpaceObject = onull; /* copy to set slot properties */
  pInfo->underlyingColorSpace = SPACE_notset;
  pInfo->dimensions = 0;
  pInfo->underlyingDimensions = 0;

  /* Set default values for structure fields */
  for (i = 0; i < CIETABLE_MAX_INPUT_CHANNELS; i++) {
    pInfo->rangeABCD[2*i]     = 0;
    pInfo->rangeABCD[2*i + 1] = 1;

    pInfo->transferinput_sizes[i] = 0;
    pInfo->transferinput[i]       = NULL;
    pInfo->rangeEFGH[2*i]         = 0;
    pInfo->rangeEFGH[2*i + 1]     = 1;

    pInfo->sizes[i]             = 0;
  }

  pInfo->transferoutput_sizes  = NULL;
  pInfo->transferoutput = NULL;
  pInfo->rangetable = NULL;
  pInfo->bases = NULL;
  pInfo->ranges = NULL;

  for (i = 0; i < 3; i++)
    pInfo->matrix[3*i + i] = 1;

  pInfo->file  = onull; /* copy to set slot properties */
  pInfo->table_data     = NULL;

  pInfo->nXUIDs         = 0;
  pInfo->pXUIDs         = NULL;

  pInfo->nextInfo       = NULL;

  if (!extract_cietableabcd(colorInfo, pInfo, colorSpaceObject)) {
    destroycietableinfo(&pInfo);
    return FALSE;
  }

  pInfo->nextInfo = mm_sac_alloc(mm_pool_color,
                                 cc_sizeofTransformInfo(),
                                 MM_ALLOC_CLASS_NCOLOR );
  if (pInfo->nextInfo == NULL) {
    destroycietableinfo(&pInfo);
    return error_handler(VMERROR);
  }

  cc_initTransformInfo(pInfo->nextInfo);
  if (!cc_createTransformInfo(colorInfo, pInfo->nextInfo, &pInfo->underlyingColorSpaceObject)) {
    destroycietableinfo(&pInfo);
    return FALSE;
  }

  pInfo->colorspaceIsValid = TRUE;

  cietableInfoAssertions(pInfo);

  *cietableInfo = pInfo;
  *outputColorSpaceId = pInfo->nextInfo->inputColorSpaceId;

  return TRUE;
}

static void freecietableinfo( CLINKcietableabcd *cietableInfo )
{
  int32             i;

  /* De-allocate the input transfers */
  for (i = 0; i < CIETABLE_MAX_INPUT_CHANNELS; i++) {
    if (cietableInfo->transferinput[i] != NULL) {
      mm_free(mm_pool_color, cietableInfo->transferinput[i],
              cietableInfo->transferinput_sizes[i] * sizeof(SYSTEMVALUE));
    }
  }

  /* Deallocate the various output tables */
  if (cietableInfo->transferoutput != NULL) {
    if (cietableInfo->transferoutput_sizes != NULL) {
      for (i = 0; i < cietableInfo->underlyingDimensions; i++) {
        if (cietableInfo->transferoutput[i] != NULL) {
          mm_free(mm_pool_color, cietableInfo->transferoutput[i],
                  cietableInfo->transferoutput_sizes[i] * sizeof(SYSTEMVALUE));
        }
      }
    }
    mm_free (mm_pool_color, cietableInfo->transferoutput,
             cietableInfo->underlyingDimensions * sizeof (SYSTEMVALUE*));
  }
  if (cietableInfo->transferoutput_sizes != NULL)
    mm_free (mm_pool_color, cietableInfo->transferoutput_sizes,
             cietableInfo->underlyingDimensions * sizeof (int32));
  if (cietableInfo->rangetable != NULL)
    mm_free (mm_pool_color, cietableInfo->rangetable,
             cietableInfo->underlyingDimensions * 2 * sizeof (SYSTEMVALUE));
  if (cietableInfo->bases != NULL)
    mm_free (mm_pool_color, cietableInfo->bases,
             cietableInfo->underlyingDimensions * sizeof (SYSTEMVALUE));
  if (cietableInfo->ranges != NULL)
    mm_free (mm_pool_color, cietableInfo->ranges,
             cietableInfo->underlyingDimensions * sizeof (SYSTEMVALUE));

  /* De-allocate the table_data */
  if (cietableInfo->table_data != NULL)
    mm_free_with_header(mm_pool_color, cietableInfo->table_data);

  cc_destroy_xuids( & cietableInfo->nXUIDs , & cietableInfo->pXUIDs ) ;

  if (cietableInfo->nextInfo != NULL) {
    if (cietableInfo->nextInfo->u.shared != NULL) {
      cc_destroyTransformInfo(cietableInfo->nextInfo);
    }
    mm_sac_free(mm_pool_color,
                cietableInfo->nextInfo,
                cc_sizeofTransformInfo());
  }

  mm_sac_free(mm_pool_color,
              cietableInfo,
              cietableInfo->structSize);
}

static void destroycietableinfo( CLINKcietableabcd  **cietableInfo )
{
  if ( *cietableInfo != NULL ) {
    cietableInfoAssertions(*cietableInfo);
    CLINK_RELEASE(cietableInfo, freecietableinfo);
  }
}

static void reservecietableinfo( CLINKcietableabcd *cietableInfo )
{
  if ( cietableInfo != NULL ) {
    cietableInfoAssertions( cietableInfo ) ;
    CLINK_RESERVE( cietableInfo ) ;
  }
}

static size_t cietableInfoStructSize(void)
{
  return sizeof(CLINKcietableabcd);
}

#if defined( ASSERT_BUILD )
static void cietableInfoAssertions(CLINKcietableabcd *pInfo)
{
  int32   i;

  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->linkType == CL_TYPEcietablea ||
           pInfo->linkType == CL_TYPEcietableabc ||
           pInfo->linkType == CL_TYPEcietableabcd, "linkType not set");
  HQASSERT(pInfo->structSize == cietableInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
  HQASSERT(oType(pInfo->underlyingColorSpaceObject) != ONULL,
           "colorspace object is null");
  HQASSERT(pInfo->dimensions == 1 || pInfo->dimensions == 3 || pInfo->dimensions == 4,
           "Invalid number of dimensions");
  HQASSERT(pInfo->underlyingDimensions <= BLIT_MAX_COLOR_CHANNELS,
           "Too many output colorants");
  for (i = 0; i < CIETABLE_MAX_INPUT_CHANNELS; i++) {
    HQASSERT((pInfo->transferinput_sizes[i] == 0 && pInfo->transferinput[i] == NULL) ||
             (pInfo->transferinput_sizes[i] > 0 && pInfo->transferinput[i] != NULL),
             "Incompatible transfer input info");
  }
  if (pInfo->colorspaceIsValid) {
    HQASSERT(pInfo->transferoutput != NULL, "transferoutput is NULL");
    HQASSERT(pInfo->transferoutput_sizes != NULL, "transferoutput_sizes is NULL");
    HQASSERT(pInfo->rangetable != NULL, "rangetable is NULL");
    HQASSERT(pInfo->bases != NULL, "bases is NULL");
    HQASSERT(pInfo->ranges != NULL, "ranges is NULL");
    for (i = 0; i < pInfo->underlyingDimensions; i++) {
      HQASSERT((pInfo->transferoutput_sizes[i] == 0 && pInfo->transferoutput[i] == NULL) ||
               (pInfo->transferoutput_sizes[i] > 0 && pInfo->transferoutput[i] != NULL),
               "Incompatible transfer output info");
    }
    HQASSERT(!pInfo->tablePresent || oType(pInfo->file) != ONULL, "file object is null");
  }
}
#endif

/* ---------------------------------------------------------------------- */
#define tabref1(_a) (&table_data[(_a)*underlying_dimensions])

#define tabref3(_a,_b,_c) (&table_data[(_a)][(_b)][(_c)*underlying_dimensions])

#define tabref4(_a,_b,_c,_d) (&table_data[(_a)][(_b)][(_c)][(_d)*underlying_dimensions])


/* In the interpolation code below, I need a way to refer to corners
   of a [hyper]cube. I'll use letters for dimensions, with lc being the
   lower-valued entry and UC being the higher-valued one. Thus, the corners
   of a cube are abc abC aBc aBC Abc AbC ABc ABC and of a hypercube are
   abcd abcD abCd abCD aBcd aBcD aBCd aBCD Abcd AbcD AbCd AbCD ABcd ABcD ABCd ABCD
   The following macros are a convenient way to get all these variables set up.
 */
#define a which_cube_lower[0]
#define A which_cube_upper[0]
#define b which_cube_lower[1]
#define B which_cube_upper[1]
#define c which_cube_lower[2]
#define C which_cube_upper[2]
#define d which_cube_lower[3]
#define D which_cube_upper[3]


/* parts of interpolation */

#define interpolate_1d(_e1,_e2,_d12) ( ((SYSTEMVALUE)(_e1)) +                          \
                      (( ((SYSTEMVALUE)(_e2)) - ((SYSTEMVALUE)(_e1)) ) * \
                       ((SYSTEMVALUE)(_d12)) ))

#define destorageize(_a,_i) (((_a) * ranges[(_i)])+bases[(_i)])


static Bool transformTableABCD( CLINK *pLink, SYSTEMVALUE cie[] )
{
  CLINKcietableabcd   *pInfo = pLink->p.cietableabcd;
  int32               dimensions = pInfo->dimensions;
  int32               underlying_dimensions = pInfo->underlyingDimensions;
  int32               *sizes = pInfo->sizes;

  int32               *transferinput_sizes = pInfo->transferinput_sizes;
  SYSTEMVALUE         **transferinput = pInfo->transferinput;
  int32               *transferoutput_sizes = pInfo->transferoutput_sizes;
  SYSTEMVALUE         **transferoutput = pInfo->transferoutput;

  SYSTEMVALUE         *bases = pInfo->bases;
  SYSTEMVALUE         *ranges = pInfo->ranges;

  int32               which_cube_lower[4];
  int32               which_cube_upper[4];
  SYSTEMVALUE         into_cube[4];

  int32               i;


  for (i = 0; i < dimensions; i++)
    cie[i] = pLink->iColorValues[i];

  /* now start to transform the color */

  /* Apply the rangeABCD */
  if (dimensions == 1)
    NARROW1(cie, pInfo->rangeABCD);
  else if (dimensions == 3)
    NARROW3(cie, pInfo->rangeABCD);
  else
    NARROW4(cie, pInfo->rangeABCD);

  /* apply the matrix (ABC only) */
  if (pInfo->matrixPresent) {
    SYSTEMVALUE *MatrixABC = pInfo->matrix;
    SYSTEMVALUE range[6];

    SYSTEMVALUE ucolor0 = cie[0] ;
    SYSTEMVALUE ucolor1 = cie[1] ;
    SYSTEMVALUE ucolor2 = cie[2] ;

    cie[0] = ucolor0 * MatrixABC[0] +
             ucolor1 * MatrixABC[1] +
             ucolor2 * MatrixABC[2];
    cie[1] = ucolor0 * MatrixABC[3] +
             ucolor1 * MatrixABC[4] +
             ucolor2 * MatrixABC[5];
    cie[2] = ucolor0 * MatrixABC[6] +
             ucolor1 * MatrixABC[7] +
             ucolor2 * MatrixABC[8];

    HQASSERT(dimensions == 3,
         "MatrixABC only allowed for CIETableABC");

    /* re-trim the values -- if there is a matrix, we want to be able to
       assume range 0..1 in its output.
    */
    range[0] = range[2] = range[4] = 0.0;
    range[1] = range[3] = range[5] = 1.0;
    NARROW3(cie, range);
  }

  /* apply the input transfer */
  if (pInfo->transferInputPresent) {
    for (i = 0; i < dimensions; i++) {
      int32 transfer_size = transferinput_sizes[i];

      if (transfer_size != 0) {
        SYSTEMVALUE *transfer = transferinput[i];
        SYSTEMVALUE input_color;

        int32       index;
        SYSTEMVALUE lower;
        SYSTEMVALUE higher;

        /* scale to index the transfer array */
        if (pInfo->matrixPresent) {
          /* the range has been clipped to 0->1 by the matrix */
          input_color = cie[i] * (transfer_size - 1);
        }
        else {
          /* the RangeA(BC(D)) tells us what the input range is */
          SYSTEMVALUE range_base = pInfo->rangeABCD[i*2];
          SYSTEMVALUE range_limit = pInfo->rangeABCD[i*2+1] - range_base;
          input_color = (cie[i] - range_base) / range_limit * (transfer_size - 1);
        }

        /* get an index, then adjust the input color to be the gap from this index */
        index = (int32)input_color;
        input_color -= index;
        lower = transfer[index];

        index++; if (index == transfer_size) index--;
        higher = transfer[index];

        cie[i] = lower + (higher-lower)*input_color;

        HQASSERT(cie[i] >= pInfo->rangeEFGH[i*2], "Output of table too low");
        HQASSERT(cie[i] <= pInfo->rangeEFGH[i*2+1], "Output of table too high");
      }
    }
  }


  /* Apply the RangeEFGH */
  if (dimensions == 1)
    NARROW1(cie, pInfo->rangeEFGH);
  else if (dimensions == 3)
    NARROW3(cie, pInfo->rangeEFGH);
  else
    NARROW4(cie, pInfo->rangeEFGH);

  /* Now the lookup with interpolation
     The table dimensions are specified in sizes = pInfo->sizes;
   */
  if (pInfo->tablePresent) {
    /* Lazily populate the table data */
    if (pInfo->table_data == NULL) {
      pInfo->table_data = read_table_data(&pInfo->file,
                                          sizes,
                                          dimensions,
                                          underlying_dimensions,
                                          bases, ranges);

      if (pInfo->table_data == NULL)
        return FALSE;

#ifdef dump_table_data
      verify_table_data(pInfo->table_data,
                dimensions,
                sizes,
                underlying_dimensions,
                bases, ranges);
#endif
    }


    for (i = 0; i < dimensions; i++) {
      SYSTEMVALUE input_color;
      SYSTEMVALUE input_min;
      SYSTEMVALUE input_max;
      SYSTEMVALUE input_scaled;
      int32       index_limit;
      int32       index;

      input_color = cie[i];
      input_min = pInfo->rangeEFGH[2*i];
      input_max = pInfo->rangeEFGH[2*i+1];
      index_limit = sizes[i] - 1;

      /* scale to index the table */
      input_scaled = ((input_color - input_min) / (input_max - input_min)) * index_limit ;

      index = (int32)input_scaled;

      /* Now say which [huper]cube we're in, specifying it by its upper and
         lower cube boundaries, taking into account the upper edge effect.
      */
      which_cube_lower[i] = index;
      which_cube_upper[i] = index == index_limit ? index : (index+1);

      into_cube[i] = input_scaled - index;
    }

    switch (dimensions) {
    case 1:
      {
        uint16  *table_data = (uint16 *) pInfo->table_data;
        struct {
          uint16 *aa, *AA; /* N.B. Using aa and AA as 'a' and 'A' already defined */
        } corners1;

        corners1.aa = tabref1(a);
        corners1.AA = tabref1(A);

        for (i = 0; i < underlying_dimensions; i++) {
      /* Variables named with a letter repeated in lc then in UC
         represent a value produced by interpolating along the
         dimension represented by that letter. So aA means the
         result of interpolating in the dimension aA.
      */

      SYSTEMVALUE aA = interpolate_1d(corners1.aa[i], corners1.AA[i], into_cube[0]);

      cie[i] = destorageize(aA, i);
        }
      }
      break;

    case 3:
      {
        uint16  ***table_data = (uint16 ***) pInfo->table_data;
        struct {
          uint16 *abc,  *abC,  *aBc,  *aBC,  *Abc,  *AbC,  *ABc,  *ABC;
        } corners3;

        corners3.abc = tabref3(a, b, c);
        corners3.abC = tabref3(a, b, C);
        corners3.aBc = tabref3(a, B, c);
        corners3.aBC = tabref3(a, B, C);
        corners3.Abc = tabref3(A, b, c);
        corners3.AbC = tabref3(A, b, C);
        corners3.ABc = tabref3(A, B, c);
        corners3.ABC = tabref3(A, B, C);

        for (i = 0; i < underlying_dimensions; i++) {
      /* Variables named with a letter repeated in lc then in UC
         represent a value produced by interpolating along the
         dimension represented by that letter; a single letter
         in lc or UC represents a dimension in which the interpolation
         has not yet been done. So far example, aAbC means the result
         of interpolating in the dimension aA, with the dimension bB
         being at b and the dimension cC being at C.
      */

      SYSTEMVALUE aAbc = interpolate_1d(corners3.abc[i], corners3.Abc[i], into_cube[0]);
      SYSTEMVALUE aABc = interpolate_1d(corners3.aBc[i], corners3.ABc[i], into_cube[0]);
      SYSTEMVALUE aAbBc = interpolate_1d(aAbc, aABc, into_cube[1]);

      SYSTEMVALUE aAbC = interpolate_1d(corners3.abC[i], corners3.AbC[i], into_cube[0]);
      SYSTEMVALUE aABC = interpolate_1d(corners3.aBC[i], corners3.ABC[i], into_cube[0]);
      SYSTEMVALUE aAbBC = interpolate_1d(aAbC, aABC, into_cube[1]);

      SYSTEMVALUE aAbBcC = interpolate_1d(aAbBc, aAbBC, into_cube[2]);

      cie[i] = destorageize(aAbBcC, i);
        }
      }
      break;

    case 4:
      {
        uint16  ****table_data = (uint16 ****) pInfo->table_data;
        struct {
          uint16 *abcd, *abcD, *abCd, *abCD, *aBcd, *aBcD, *aBCd, *aBCD,
           *Abcd, *AbcD, *AbCd, *AbCD, *ABcd, *ABcD, *ABCd, *ABCD;
        } corners4;

        corners4.abcd = tabref4(a, b, c, d);
        corners4.abcD = tabref4(a, b, c, D);
        corners4.abCd = tabref4(a, b, C, d);
        corners4.abCD = tabref4(a, b, C, D);
        corners4.aBcd = tabref4(a, B, c, d);
        corners4.aBcD = tabref4(a, B, c, D);
        corners4.aBCd = tabref4(a, B, C, d);
        corners4.aBCD = tabref4(a, B, C, D);
        corners4.Abcd = tabref4(A, b, c, d);
        corners4.AbcD = tabref4(A, b, c, D);
        corners4.AbCd = tabref4(A, b, C, d);
        corners4.AbCD = tabref4(A, b, C, D);
        corners4.ABcd = tabref4(A, B, c, d);
        corners4.ABcD = tabref4(A, B, c, D);
        corners4.ABCd = tabref4(A, B, C, d);
        corners4.ABCD = tabref4(A, B, C, D);

        for (i = 0; i < underlying_dimensions; i++) {
      /* Variables named with a letter repeated in lc then in UC
         represent a value produced by interpolating along the
         dimension represented by that letter; a single letter
         in lc or UC represents a dimension in which the interpolation
         has not yet been done. So far example, aAbCD means the result
         of interpolating in the dimension aA, with the dimension bB
         being at b and the dimension cC being at C and dD at D.
      */

      SYSTEMVALUE aAbcd = interpolate_1d(corners4.abcd[i], corners4.Abcd[i], into_cube[0]);
      SYSTEMVALUE aABcd = interpolate_1d(corners4.aBcd[i], corners4.ABcd[i], into_cube[0]);
      SYSTEMVALUE aAbBcd = interpolate_1d(aAbcd, aABcd, into_cube[1]);

      SYSTEMVALUE aAbCd = interpolate_1d(corners4.abCd[i], corners4.AbCd[i], into_cube[0]);
      SYSTEMVALUE aABCd = interpolate_1d(corners4.aBCd[i], corners4.ABCd[i], into_cube[0]);
      SYSTEMVALUE aAbBCd = interpolate_1d(aAbCd, aABCd, into_cube[1]);

      SYSTEMVALUE aAbBcCd = interpolate_1d(aAbBcd, aAbBCd, into_cube[2]);

      SYSTEMVALUE aAbcD = interpolate_1d(corners4.abcD[i], corners4.AbcD[i], into_cube[0]);
      SYSTEMVALUE aABcD = interpolate_1d(corners4.aBcD[i], corners4.ABcD[i], into_cube[0]);
      SYSTEMVALUE aAbBcD = interpolate_1d(aAbcD, aABcD, into_cube[1]);

      SYSTEMVALUE aAbCD = interpolate_1d(corners4.abCD[i], corners4.AbCD[i], into_cube[0]);
      SYSTEMVALUE aABCD = interpolate_1d(corners4.aBCD[i], corners4.ABCD[i], into_cube[0]);
      SYSTEMVALUE aAbBCD = interpolate_1d(aAbCD, aABCD, into_cube[1]);

      SYSTEMVALUE aAbBcCD = interpolate_1d(aAbBcD, aAbBCD, into_cube[2]);

      SYSTEMVALUE aAbBcCdD = interpolate_1d(aAbBcCd, aAbBcCD, into_cube[3]);

      cie[i] = destorageize(aAbBcCdD, i);
        }
      }
      break;

    default:
      HQFAIL("wrong dimensionality for interpolation");
      return error_handler(UNREGISTERED);
    }
  }

  /* Apply the RangeTable.

     (note that this was previously incorrect, as it was applying the range according
     to the dimnensionality of the input, when it should be the output. In order to
     limit change, keep the 3 and 4d cases essentially the same as before.)
  */
  if (underlying_dimensions == 1) {
    NARROW1(cie, pInfo->rangetable);
  } else if (underlying_dimensions == 3) {
    NARROW3(cie, pInfo->rangetable);
  } else if (underlying_dimensions == 4) {
    NARROW4(cie, pInfo->rangetable);
  } else {
    for (i = 0; i < underlying_dimensions; i++) {
      if (cie[i] < pInfo->rangetable[2*i])
        cie[i] = pInfo->rangetable[2*i];
      else if (cie[i] > pInfo->rangetable[2*i+1])
        cie[i] = pInfo->rangetable[2*i+1];
    }
  }

  /* now the output transfer */
  if (pInfo->transferOutputPresent) {
    for (i = 0; i < underlying_dimensions; i++) {
      int32 transfer_size = transferoutput_sizes[i];
      if (transfer_size != 0) {
        SYSTEMVALUE input_color;
        SYSTEMVALUE *transfer = transferoutput[i];
        /* the RangeTable tells us what the range coming out of the table is */
        SYSTEMVALUE range_base = pInfo->rangetable[i*2];
        SYSTEMVALUE range_limit = pInfo->rangetable[i*2+1] - range_base;
        int32 index;
        SYSTEMVALUE lower;
        SYSTEMVALUE higher;

        /* scale to index the transfer array */
        input_color = (cie[i] - range_base) / range_limit * (transfer_size - 1);

        /* get an index, then adjust the input color to be the gap from this index */
        index = (int32)input_color;
        input_color -= index;
        lower = transfer[index];

        index++; if (index == transfer_size) index--;
        higher = transfer[index];

        cie[i] = lower + (higher-lower)*input_color;
      }
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static Bool extract_cietableabcd( GS_COLORinfo      *colorInfo,
                                  CLINKcietableabcd *pInfo,
                                  OBJECT            *colorSpaceObject )
{
  static NAMETYPEMATCH thetableabcdMatch[] = {
    /* normally everyone wants these, except when using this colorspace as a
     * transfer stage, in which case either TransferInput or TransferOutput
     * must be present.
     */
    { NAME_TableSize | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 0 */
    { NAME_Table     | OOPTIONAL,      1, { OFILE }},                /* 1 */

    /* anyone can have these */
    { NAME_TransferInput  | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }}, /* 2 */
    { NAME_RangeTable     | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }}, /* 3 */
    { NAME_TransferOutput | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }}, /* 4 */

    /* 4 can have these */
    { NAME_RangeABCD | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 5 */
    { NAME_RangeEFGH | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 6 */

    /* 3 can have these */
    { NAME_RangeABC  | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 7 */
    { NAME_RangeEFG  | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 8 */
    { NAME_MatrixABC | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 9 */

    /* 1 can have these */
    { NAME_RangeA    | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 10 */
    { NAME_RangeE    | OOPTIONAL,      2, { OARRAY, OPACKEDARRAY }}, /* 11 */

    /* anyone can have these */
    { NAME_UniqueID | OOPTIONAL,       1, { OINTEGER }},             /*12 */
    { NAME_XUID     | OOPTIONAL,       2, { OARRAY, OPACKEDARRAY }}, /*13 */

    DUMMY_END_MATCH
  } ;

  int32           i ;
  OBJECT          *table_params ;
  OBJECT          *theo ;

  theo = oArray(*colorSpaceObject) ;

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           ((oName(*theo) == &system_names[NAME_CIETableA]) ||
            (oName(*theo) == &system_names[NAME_CIETableABC]) ||
            (oName(*theo) == &system_names[NAME_CIETableABCD])),
           "extract_cietableabcd should be called only for NAME_CIETableA, "
           "NAME_CIETableABC or NAME_CIETableABCD");

  if (oName(*theo) == &system_names[NAME_CIETableA])
    pInfo->dimensions = 1;
  else if (oName(*theo) == &system_names[NAME_CIETableABC])
    pInfo->dimensions = 3;
  else
    pInfo->dimensions = 4;

  table_params = ++theo ;

  /* Look at the underlying color space */
  ++theo ;
  pInfo->underlyingColorSpaceObject = *theo;

  if ( ! gsc_getcolorspacesizeandtype( colorInfo, theo,
                                       &pInfo->underlyingColorSpace,
                                       &pInfo->underlyingDimensions ))
    return FALSE ;

  if ( pInfo->underlyingDimensions > BLIT_MAX_COLOR_CHANNELS )
    return detail_error_handler(CONFIGURATIONERROR,
                                "Too many colorants in CIETable color space");

  /* Got the underlying space, allocate output memory tables accordingly */

  pInfo->rangetable = mm_alloc(mm_pool_color,
                               pInfo->underlyingDimensions * 2 * sizeof (SYSTEMVALUE),
                               MM_ALLOC_CLASS_TRANSFERS);
  if (pInfo->rangetable == NULL)
    return error_handler(VMERROR);

  pInfo->bases = mm_alloc(mm_pool_color,
                          pInfo->underlyingDimensions * sizeof (SYSTEMVALUE),
                          MM_ALLOC_CLASS_TRANSFERS);
  if (pInfo->bases == NULL)
    return error_handler(VMERROR);

  pInfo->ranges = mm_alloc(mm_pool_color,
                           pInfo->underlyingDimensions * sizeof (SYSTEMVALUE),
                           MM_ALLOC_CLASS_TRANSFERS);
  if (pInfo->ranges == NULL)
    return error_handler(VMERROR);

  pInfo->transferoutput_sizes = mm_alloc(mm_pool_color,
                                         pInfo->underlyingDimensions * sizeof (int32),
                                         MM_ALLOC_CLASS_TRANSFERS);
  if (pInfo->transferoutput_sizes == NULL)
    return error_handler(VMERROR);

  pInfo->transferoutput = mm_alloc(mm_pool_color,
                                   pInfo->underlyingDimensions * sizeof (SYSTEMVALUE*),
                                   MM_ALLOC_CLASS_TRANSFERS);
  if (pInfo->transferoutput == NULL)
    return error_handler(VMERROR);

  /* initialize these (before they were arbitrary sized to accommodate DeviceN
     outputs, there were initialized on allocation of pInfo) */
  for (i = 0; i < pInfo->underlyingDimensions; i++) {
    pInfo->transferoutput[i] = NULL;
    pInfo->transferoutput_sizes[i] = 0;
    pInfo->rangetable[2 * i] = 0.0;
    pInfo->rangetable[2 * i + 1] = 1.0;
    pInfo->ranges[i] = 1.0/MAX_VALUE_IN_NORMALIZED_TABLE;
    pInfo->bases[i] = 0.0;
  }

  /* Now look at our own parameters */

  if ( oType(*table_params) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( table_params , thetableabcdMatch ))
    return FALSE ;

  /* One of Table, TransferInput or TransferOutput must be present, otherwise
   * there would be little to do.
   */
  if (thetableabcdMatch[ 1 ].result == NULL &&   /* Table */
      thetableabcdMatch[ 2 ].result == NULL &&   /* TransferInput */
      thetableabcdMatch[ 4 ].result == NULL)     /* TransferOutput */
    return error_handler(UNDEFINED);

  /* Check that the parameters are appropriate for 1, 3 or 4 channel tables,
   * transfer parameters to the appropriate variables,
   * and fill in the optional ones with defaults as needed.
   */
  theo = thetableabcdMatch[ 0 ].result ; /* TableSize */
  if (theo != NULL) {
    if ( theLen(*theo) != pInfo->dimensions )
      return error_handler( TYPECHECK ) ;

    theo = oArray(*theo);
    for ( i = 0 ; i < pInfo->dimensions ; ++i , ++theo ) {
      if ( oType(*theo) != OINTEGER )
        return error_handler( TYPECHECK ) ;
      pInfo->sizes[ i ] = ( uint8 ) oInteger(*theo) ;
      /* Must be at least 2 points on each side */
      if (pInfo->sizes[ i ] < 2)
        return error_handler( RANGECHECK ) ;
    }
  }

  theo = thetableabcdMatch[ 1 ].result ; /* Table */
  if (theo != NULL) {
    FILELIST *table_file = oFile(*theo) ;

    /* TableSize is required if Table exists */
    if (thetableabcdMatch[ 0 ].result == NULL )   /* TableSize */
      return error_handler(UNDEFINED);

    if ( ! isIOpenFileFilter( theo , table_file ))
      return error_handler( IOERROR ) ;

    pInfo->file = *theo;

    pInfo->tablePresent = TRUE ;
  }
  else {
    if (pInfo->dimensions != pInfo->underlyingDimensions)
      return error_handler (RANGECHECK);

    pInfo->tablePresent = FALSE ;
  }

  theo = thetableabcdMatch[ 2 ].result ; /* TransferInput */
  if (theo != NULL) {
    if ( ! store_transfer_values( theo ,
                                  pInfo->dimensions ,
                                  pInfo->transferinput ,
                                  pInfo->transferinput_sizes))
      return FALSE ;

    pInfo->transferInputPresent = TRUE ;
  }
  else {
    pInfo->transferInputPresent = FALSE ;
  }

  theo = thetableabcdMatch[ 3 ].result ; /* RangeTable */
  if ( theo != NULL ) {
    SYSTEMVALUE *RangeTable = pInfo->rangetable ;

    if ( theLen(*theo) != 2 * pInfo->underlyingDimensions )
      return error_handler( TYPECHECK ) ;

    theo = oArray(*theo);
    for ( i = 0 ; i < 2 * pInfo->underlyingDimensions ; ++i , ++theo ) {
      SYSTEMVALUE arg ;

      if ( !object_get_numeric(theo, &arg) )
        return FALSE ;

      RangeTable[ i ] = arg ;

      /* We store the elements in the table such that the most negative number is
         0x0000 and the most positive number is 0x8000
         (MAX_VALUE_IN_NORMALIZED_TABLE), so it is useful to have an alternative form
         of the range table such that we can easily convert in and out of the table
         to real numbers. Hence the two arrays bases and ranges and the magic number
         0x8000. */

      if ((i & 1) == 0)
        pInfo->bases[i/2] = arg;
      else
        pInfo->ranges[i/2] = (arg - pInfo->bases[i/2]) / MAX_VALUE_IN_NORMALIZED_TABLE;
    }
  }

  theo = thetableabcdMatch[ 4 ].result ; /* TransferOutput */
  if ( theo != NULL ) {
    if ( ! store_transfer_values( theo ,
                                  pInfo->underlyingDimensions ,
                                  pInfo->transferoutput ,
                                  pInfo->transferoutput_sizes))
      return FALSE ;

    pInfo->transferOutputPresent = TRUE ;
  }
  else {
    pInfo->transferOutputPresent = FALSE ;
  }

  switch ( pInfo->dimensions ) {
  case 1:
    if ( thetableabcdMatch[ 5 ].result != NULL || /* RangeABCD -- not allowed in 1-channel */
         thetableabcdMatch[ 6 ].result != NULL || /* RangeEFGH -- not allowed in 1-channel */
         thetableabcdMatch[ 7 ].result != NULL || /* RangeABC -- not allowed in 1-channel */
         thetableabcdMatch[ 8 ].result != NULL || /* RangeEFG -- not allowed in 1-channel */
         thetableabcdMatch[ 9 ].result != NULL )  /* MatrixABC -- not allowed in 1-channel */
      return error_handler( TYPECHECK ) ;

    /* CIETableA *never* has a matrix */
    pInfo->matrixPresent = FALSE ;

    /* Having disposed of the disallowed, now process allowed */
    theo = thetableabcdMatch[ 10 ].result ; /* RangeA */
    if ( theo != NULL ) {
      SYSTEMVALUE *Range = pInfo->rangeABCD ;

      if ( theLen(*theo) != 2 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 2 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        Range[ i ] = arg ;

        /* Check that start and end of the range are the right way around */
        if (( i & 1 ) &&        /* check at odd numbers */
            ( Range[ i - 1 ] >= Range[ i ]))
          return error_handler( RANGECHECK ) ;
      }
    }

    theo = thetableabcdMatch[ 11 ].result ; /* RangeE */
    if ( theo != NULL ) {
      SYSTEMVALUE *Range = pInfo->rangeEFGH ;

      if ( theLen(*theo) != 2 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 2 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        Range[ i ] = arg ;

        /* Check that start and end of the range are the right way around */
        if (( i & 1 ) &&        /* check at odd numbers */
            ( Range[ i - 1 ] >= Range[ i ]))
          return error_handler( RANGECHECK ) ;
      }
    }
    break ;

  case 3:
    if ( thetableabcdMatch[ 5 ].result != NULL ||  /* RangeABCD -- not allowed in 3-channel */
         thetableabcdMatch[ 6 ].result != NULL ||  /* RangeEFGH -- not allowed in 3-channel */
         thetableabcdMatch[ 10 ].result != NULL || /* RangeA    -- not allowed in 3-channel */
         thetableabcdMatch[ 11 ].result != NULL )  /* RangeE    -- not allowed in 3-channel */
      return error_handler( TYPECHECK ) ;

    /* Having disposed of the disallowed, now process allowed. */
    theo = thetableabcdMatch[ 7 ].result ; /* RangeABC */
    if ( theo != NULL ) {
      SYSTEMVALUE *Range = pInfo->rangeABCD ;

      if ( theLen(*theo) != 6 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 6 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        Range[ i ] = arg ;

        /* Check that start and end of the range are the right way around */
        if (( i & 1 ) &&        /* check at odd numbers */
            ( Range[ i - 1 ] >= Range[ i ]))
          return error_handler( RANGECHECK ) ;
      }
    }

    theo = thetableabcdMatch[ 8 ].result ; /* RangeEFG */
    if ( theo != NULL ) {
      SYSTEMVALUE *Range = pInfo->rangeEFGH ;

      if ( theLen(*theo) != 6 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 6 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        Range[ i ] = arg ;

        /* Check that start and end of the range are the right way around */
        if (( i & 1 ) &&        /* check at odd numbers */
            ( Range[ i - 1 ] >= Range[ i ]))
          return error_handler( RANGECHECK ) ;
      }
    }

    theo = thetableabcdMatch[ 9 ].result ; /* MatrixABC */
    if ( theo != NULL ) {
      SYSTEMVALUE *MatrixABC = pInfo->matrix ;

      if ( theLen(*theo) != 9 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 9 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        MatrixABC[ i ] = arg ;
      }
      pInfo->matrixPresent = TRUE ;
    }
    else {
      pInfo->matrixPresent = FALSE ;
    }
    break ;

  case 4:
    if ( thetableabcdMatch[ 7 ].result != NULL ||  /* RangeABC -- not allowed in 4-channel */
         thetableabcdMatch[ 8 ].result != NULL ||  /* RangeEFG -- not allowed in 4-channel */
         thetableabcdMatch[ 9 ].result != NULL ||  /* MatrixABC -- not allowed in 4-channel */
         thetableabcdMatch[ 10 ].result != NULL || /* RangeA -- not allowed in 4-channel */
         thetableabcdMatch[ 11 ].result != NULL )  /* RangeE -- not allowed in 4-channel */
      return error_handler( TYPECHECK ) ;

    /* CIETable4 *never* has a matrix */
    pInfo->matrixPresent = FALSE ;

    /* Having disposed of the disallowed, now process allowed */
    theo = thetableabcdMatch[ 5 ].result ; /* RangeABCD */
    if ( theo != NULL ) {
      SYSTEMVALUE *Range = pInfo->rangeABCD ;

      if ( theLen(*theo) != 8 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 8 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        Range[ i ] = arg ;

        /* Check that start and end of the range are the right way around */
        if (( i & 1 ) &&        /* check at odd numbers */
            ( Range[ i - 1 ] >= Range[ i ]))
          return error_handler( RANGECHECK ) ;
      }
    }

    theo = thetableabcdMatch[ 6 ].result ; /* RangeEFGH */
    if ( theo != NULL ) {
      SYSTEMVALUE *Range = pInfo->rangeEFGH ;

      if ( theLen(*theo) != 8 )
        return error_handler( TYPECHECK ) ;

      theo = oArray(*theo);
      for ( i = 0 ; i < 8 ; ++i , ++theo ) {
        SYSTEMVALUE arg ;

        if ( !object_get_numeric(theo, &arg) )
          return FALSE ;

        Range[ i ] = arg ;

        /* Check that start and end of the range are the right way around */
        if (( i & 1 ) &&        /* check at odd numbers */
            ( Range[ i - 1 ] >= Range[ i ]))
          return error_handler( RANGECHECK ) ;
      }
    }
    break ;
  }

  return cc_create_xuids( thetableabcdMatch[ 12 ].result ,
                          thetableabcdMatch[ 13 ].result ,
                          & pInfo->nXUIDs ,
                          & pInfo->pXUIDs ) ;
}

static Bool store_transfer_values(OBJECT       *theo,
                                   int32        channels,
                                   SYSTEMVALUE  **Transfers,
                                   int32        *TransferSizes)
{
  int32 i;

  HQASSERT(theo != NULL, "theo is NULL");

  if (theLen(*theo) != channels)
    return error_handler(TYPECHECK);

  theo = oArray(*theo);
  for (i = 0; i < channels; i++, theo++) {
    int32       len;
    OBJECT      *numbero;
    int32       j;
    SYSTEMVALUE *numberarray;

    if (oType(*theo) != OARRAY)
      return error_handler(TYPECHECK);

    len = theLen(*theo);
    numbero = oArray(*theo);

    numberarray = mm_alloc(mm_pool_color,
                           len * sizeof (SYSTEMVALUE),
                           MM_ALLOC_CLASS_TRANSFERS);

    if (numberarray == NULL)
      return error_handler(VMERROR);

    Transfers[i] = numberarray;
    TransferSizes[i] = len;

    for (j = 0; j < len; j++, numbero++) {
      if (!object_get_numeric(numbero, &numberarray[j]))
        return FALSE;
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* Format kinds (in both directions) */
#define IEEE_REAL 0
#define NATIVE_REAL 1
#define INT16 2
#define INT32 3

/* We want to storageize output values such that the most negative number is
   represented as 0x0000, and the most positive number as 0x8000
   (MAX_VALUE_IN_NORMALIZED_TABLE). Pre-computed base and range values help us to do
   this.  */

#define storageize_a(_a) ((uint16) (((_a) - base_a) / range_a))
#define storageize_b(_b) ((uint16) (((_b) - base_b) / range_b))
#define storageize_c(_c) ((uint16) (((_c) - base_c) / range_c))
#define storageize_d(_d) ((uint16) (((_d) - base_d) / range_d))

static uint16 *read_table_data(OBJECT       *fileObject,
                               int32        sizes[CIETABLE_MAX_INPUT_CHANNELS],
                               int32        dimensions,
                               int32        underlying_dimensions,
                               SYSTEMVALUE  *bases,
                               SYSTEMVALUE  * ranges)
{
  int32       alloc_size;
  TWOBYTES    buff2[4];
  FOURBYTES   buff4[4];
  int32       bytes_per_elt;
  int32       format;
  int32       format_kind;
  int32       format_scale = 0;
  int32       hiorder = TRUE;
  int32       n_elts;
  int32       n_ptrs;
  int32       n_ptrs_in_dimension;
  SYSTEMVALUE scale;
  uint16      *table_data;
  uint16      *table_ptr;
  uint16      **table_index_ptr;

  int32 i;

  /* already type-checked */
  FILELIST *fileptr = oFile(*fileObject);

  /* Work out the size of the table and of the index region
   */
  n_elts = 1;
  for (i = 0; i < dimensions; i++)
    n_elts *= sizes[i];

  /* eg. (A * (1 + (B * (1 + C)) for indexes into a four dimensional array
   */

  if ( dimensions == 1) {
    n_ptrs = 0;
  } else {
    n_ptrs = sizes[dimensions - 2];
    for (i = dimensions - 3; i >= 0; i--) {
      n_ptrs += 1;
      n_ptrs *= sizes[i];
    }
  }

  alloc_size = n_elts * underlying_dimensions * sizeof(uint16) +
               n_ptrs * sizeof(uint16*);

  table_data = mm_alloc_with_header(mm_pool_color, alloc_size, MM_ALLOC_CLASS_CIE_34);
  if (!table_data) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Generate an index region that allows the table to be referenced as a
   * multi-dimensional array.
   */
  table_index_ptr = (uint16 **) table_data;

  if ( dimensions > 1 ) {
    n_ptrs_in_dimension = 1;
    for (i = 0; i < dimensions - 1; i++) {
      int32   j;

      n_ptrs_in_dimension *= sizes[i];
      table_index_ptr[0] = (uint16 *) (&table_index_ptr[n_ptrs_in_dimension]);
      if (i == dimensions - 2)
        for (j = 1; j < n_ptrs_in_dimension; j++)
          table_index_ptr[j] = table_index_ptr[j - 1] + sizes[i + 1] * underlying_dimensions ;
      else
        for (j = 1; j < n_ptrs_in_dimension; j++)
          table_index_ptr[j] = (uint16 *) ((uint16 **)table_index_ptr[j - 1] + sizes[i + 1]);

      table_index_ptr = (uint16 **) table_index_ptr[0];
    }
  }

  /* Set table_ptr to where the table data will be written later */
  table_ptr = (uint16 *) table_index_ptr;

  /* We're now going to check to see if the file's encrypted, and if so
     we'll use alternate file i/o functions */

  if ( !hqxNonPSSetup( fileptr ) ) {
    int32  res;
    Hq32x2 filepos;
    /* If encryption detection worked then we're at the beginning of the encrypted data,
       with decryption i/o functions in place, otherwise we need to rewind the file */

    res = (*theIMyResetFile( fileptr ))( fileptr );
    Hq32x2FromInt32(&filepos, 0);
    res = (*theIMySetFilePos( fileptr ))( fileptr, &filepos );
    HQASSERT((res != EOF),
             "read_table_data: unexpected EOF");
  }

  /* Pick up what we need from the header of the array file -- see RB2 p109ff */

  /* First search for the HNA format key */

  while ( (format = Getc( fileptr )) != 149 ) {
    if (format == EOF) {
      mm_free_with_header(mm_pool_color, table_data);
      (void)error_handler(TYPECHECK); /* not a numberarray */
      return NULL;
    }
  }

  format = Getc( fileptr );
  if ((format == EOF) || (format > 177))  {
    mm_free_with_header(mm_pool_color, table_data);
    (void) error_handler(RANGECHECK);
    return NULL;
  }
  if (format >= 128) {
    format -= 128;
    hiorder = FALSE;
  }

  if (format == 48) {
    format_kind = IEEE_REAL;
  } else if (format == 49) {
      format_kind = NATIVE_REAL;
    } else if (format >= 32) {
    format_kind = INT16;
    format_scale = format - 32;
      } else {
    format_kind = INT32;
    format_scale = format;
      }

  scale = ((SYSTEMVALUE)1.0) / ((SYSTEMVALUE)(0x1 << (format_scale) ));

  /* junk the size bytes */
  (void)Getc( fileptr );
  (void)Getc( fileptr );

  if (underlying_dimensions == 3 || underlying_dimensions == 4 || underlying_dimensions == 1) {

    /* Do three or four output dimensions in parallel */

    SYSTEMVALUE
      base_a = 0.0, range_a = 1.0,
      base_b = 0.0, range_b = 1.0, /* shut up, compiler, since you can't tell */
      base_c = 0.0, range_c = 1.0, /* that I only use these if I give them  */
      base_d = 0.0, range_d = 1.0; /* values. */

    switch (underlying_dimensions) {
    case 4:
      range_d = ranges[3]; base_d  = bases[3]; /* and drop through */
    case 3:
      range_c = ranges[2]; base_c  = bases[2];
      range_b = ranges[1]; base_b  = bases[1]; /* and drop through */
    case 1:
      range_a = ranges[0]; base_a  = bases[0];
      break;
    }

    bytes_per_elt = underlying_dimensions * ((format_kind == INT16) ? 2 : 4);

    for (i = 0; i < n_elts; i++) {
      SYSTEMVALUE raw_a, raw_b, raw_c, raw_d;

      /* Read the required data into either buff2 or buff4 */
      {
        uint8 *buff = (format_kind == INT16) ? (uint8 *) buff2 : (uint8 *) buff4;

        if ( file_read( fileptr,  buff, bytes_per_elt, NULL ) <= 0 )  {
          mm_free_with_header(mm_pool_color, table_data);
          (void) error_handler(IOERROR);
          return NULL ;
        }
      }

      if (format_kind == INT16) {
        if ( hiorder ) {
          HighOrder2Bytes(asBytes(buff2[0]));
          HighOrder2Bytes(asBytes(buff2[1]));
          HighOrder2Bytes(asBytes(buff2[2]));
          HighOrder2Bytes(asBytes(buff2[3]));
        } else {
          LowOrder2Bytes(asBytes(buff2[0]));
          LowOrder2Bytes(asBytes(buff2[1]));
          LowOrder2Bytes(asBytes(buff2[2]));
          LowOrder2Bytes(asBytes(buff2[3]));
        }
      } else {
        if ( hiorder ) {
          HighOrder4Bytes(asBytes(buff4[0]));
          HighOrder4Bytes(asBytes(buff4[1]));
          HighOrder4Bytes(asBytes(buff4[2]));
          HighOrder4Bytes(asBytes(buff4[3]));
        } else {
          LowOrder4Bytes(asBytes(buff4[0]));
          LowOrder4Bytes(asBytes(buff4[1]));
          LowOrder4Bytes(asBytes(buff4[2]));
          LowOrder4Bytes(asBytes(buff4[3]));
        }
      }
      switch (format_kind) {
      case INT16:
        raw_a = asSignedShort(buff2[0]);
        raw_b = asSignedShort(buff2[1]);
        raw_c = asSignedShort(buff2[2]);
        raw_d = asSignedShort(buff2[3]);
        break;
      case INT32:
        raw_a = asSignedInt(buff4[0]);
        raw_b = asSignedInt(buff4[1]);
        raw_c = asSignedInt(buff4[2]);
        raw_d = asSignedInt(buff4[3]);
        break;
      case IEEE_REAL:
      case NATIVE_REAL:
        raw_a = asFloat(buff4[0]);
        raw_b = asFloat(buff4[1]);
        raw_c = asFloat(buff4[2]);
        raw_d = asFloat(buff4[3]);
        break;
      default:
        HQFAIL("Unknown format in number array");
        mm_free_with_header(mm_pool_color, table_data);
        (void) error_handler(UNREGISTERED);
        return NULL;
      }

      raw_a *= scale;
      *(table_ptr++) = storageize_a(raw_a);

      if (underlying_dimensions > 1) {
        raw_b *= scale;
        *(table_ptr++) = storageize_b(raw_b);

        raw_c *= scale;
        *(table_ptr++) = storageize_c(raw_c);

        if (underlying_dimensions == 4) {
          raw_d *= scale;
          *(table_ptr++) = storageize_d(raw_d);
        }
      }
    }

  } else {

    /* underlying dimensions arbitrary (not 1, 3 or 4) extension at SW_I2 */

    bytes_per_elt = ((format_kind == INT16) ? 2 : 4);

    for (i = 0; i < n_elts; i++) {

      int32 j;

      SYSTEMVALUE raw;

      for (j = 0; j < underlying_dimensions; j++) {
        /* Read the required data into either buff2 or buff4 */
        {
          uint8 *buff = (format_kind == INT16) ? (uint8 *) buff2 : (uint8 *) buff4;

          if ( file_read( fileptr, buff, bytes_per_elt, NULL ) <= 0 )  {
            mm_free_with_header(mm_pool_color, table_data);
            (void) error_handler(IOERROR);
            return NULL ;
          }
        }

        if (format_kind == INT16) {
          if ( hiorder ) {
            HighOrder2Bytes(asBytes(buff2[0]));
          } else {
            LowOrder2Bytes(asBytes(buff2[0]));
          }
        } else {
          if ( hiorder ) {
            HighOrder4Bytes(asBytes(buff4[0]));
          } else {
            LowOrder4Bytes(asBytes(buff4[0]));
          }
        }
        switch (format_kind) {
        case INT16:
          raw = asSignedShort(buff2[0]);
          break;
        case INT32:
          raw = asSignedInt(buff4[0]);
          break;
        case IEEE_REAL:
        case NATIVE_REAL:
          raw = asFloat(buff4[0]);
          break;
        default:
          HQFAIL("Unknown format in number array");
          mm_free_with_header(mm_pool_color, table_data);
          (void) error_handler(UNREGISTERED);
          return NULL;
        }

        /* store as per storageize macros in simple case */
        *(table_ptr++) = (uint16) ((raw*scale - bases[j]) / ranges[j]);
      }
    }

  }

  HQASSERT(table_ptr == (uint16*)((uint8*) table_data +
                                  sizeof(uint16) * n_elts * underlying_dimensions +
                                  sizeof(uint16*) * n_ptrs),
           "Table not written correctly");

  return table_data;
}

/* ---------------------------------------------------------------------- */

#ifdef dump_table_data

#define tab_elt(_ref,_dim) (destorageize((SYSTEMVALUE)(_ref[(_dim)]),(_dim)))

static void verify_table_data(uint16       *table_dataPtr,
                              int32        dimensions,
                              int32        *table_dimensions,
                              int32        underlying_dimensions,
                              SYSTEMVALUE  *bases,
                              SYSTEMVALUE  *ranges)
{
  int32
    i, max_i = table_dimensions[0] - 1,
    j, max_j = table_dimensions[1] - 1,
    k, max_k = table_dimensions[2] - 1,
    l, max_l = table_dimensions[3] - 1,
    m;

  switch (dimensions) {
  case 1:
    for (i = 0; i <= max_i; i++) {
          uint16  *table_data = (uint16 *) table_dataPtr;
          uint16 *ours = tabref1(i);
          monitorf((uint8*)"%3d \t",
                   (int32)(((SYSTEMVALUE)i) / ((SYSTEMVALUE)max_i) * 100));
          for (m = 0; m < underlying_dimensions; m++)
            monitorf((uint8*)"%g ", tab_elt (ours, m));
          monitorf ((uint8*)"\n");
        }
    break;
  case 3:
    for (i = 0; i <= max_i; i++)
      for (j = 0; j <= max_j; j++)
        for (k = 0; k <= max_k; k++) {
          uint16  ***table_data = (uint16 ***) table_dataPtr;
          uint16 *ours = tabref3(i,j,k);
          monitorf((uint8*)"%3d %3d %3d \t",
                   (int32)(((SYSTEMVALUE)i) / ((SYSTEMVALUE)max_i) * 100),
                   (int32)(((SYSTEMVALUE)j) / ((SYSTEMVALUE)max_j) * 100),
                   (int32)(((SYSTEMVALUE)k) / ((SYSTEMVALUE)max_k) * 100));
          for (m = 0; m < underlying_dimensions; m++)
            monitorf((uint8*)"%g ", tab_elt (ours, m));
          monitorf ((uint8*)"\n");
        }
    break;
  case 4:
    for (i = 0; i <= max_i; i++)
      for (j = 0; j <= max_j; j++)
        for (k = 0; k <= max_k; k++)
          for (l = 0; l <= max_l; l++) {
            uint16  ****table_data = (uint16 ****) table_dataPtr;
            uint16 *ours = tabref4(i,j,k,l);
            monitorf((uint8*)"%3d %3d %3d %3d \t",
                     (int32)(((SYSTEMVALUE)i) / ((SYSTEMVALUE)max_i) * 100),
                     (int32)(((SYSTEMVALUE)j) / ((SYSTEMVALUE)max_j) * 100),
                     (int32)(((SYSTEMVALUE)k) / ((SYSTEMVALUE)max_k) * 100),
                     (int32)(((SYSTEMVALUE)l) / ((SYSTEMVALUE)max_l) * 100));
          for (m = 0; m < underlying_dimensions; m++)
            monitorf((uint8*)"%g ", tab_elt (ours, m));
          monitorf ((uint8*)"\n");
          }
    break;
  default:
    HQFAIL("Bad number of dimensions on verifying table");
    break;
  }
}
#endif

/* eof */

/* Log stripped */
