/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscpdf.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fully fledged support for color spaces specific to pdf.
 */

#include "core.h"

#include "objects.h"
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "mm.h"                 /* mm_sac_alloc */
#include "namedef_.h"           /* NAME_* */
#include "swerrors.h"           /* TYPECHECK */
#include "hqmemset.h"

#include "gscpdfpriv.h"         /* extern's */


/* ---------------------------------------------------------------------- */

struct CLINKlab {
  cc_counter_t  refCnt;
  int32         linkType;
  size_t        structSize;
  Bool          colorspaceIsValid;

  SYSTEMVALUE   matrix [9];

  SYSTEMVALUE   whitepoint [3];
  SYSTEMVALUE   blackpoint [3];

  SYSTEMVALUE   relativewhitepoint [3];
  SYSTEMVALUE   relativeblackpoint [3];

  SYSTEMVALUE   range[6];

  /* XUID/UniqueID. */
  int32          nXUIDs ;
  int32         *pXUIDs ;
};

static void  lab_destroy(CLINK *pLink);
static Bool lab_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
static size_t labStructSize(void);
static void labUpdatePtrs(CLINK *pLink, CLINKlab *labInfo);

#if defined( ASSERT_BUILD )
static void labAssertions(CLINK *pLink);
#else
#define labAssertions(_pLink) EMPTY_STATEMENT()
#endif

static size_t labInfoStructSize(void);

#if defined( ASSERT_BUILD )
static void labInfoAssertions(CLINKlab *pInfo);
#else
#define labInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static Bool extract_lab( CLINKlab      *pInfo,
                         OBJECT        *colorSpaceObject );


static CLINKfunctions CLINKlab_functions =
{
  lab_destroy,
  lab_invokeSingle,
  NULL /* lab_invokeBlock */,
  NULL
};

/* ---------------------------------------------------------------------- */

/*
 * Lab Link Data Access Functions
 * ==============================
 */
CLINK *cc_lab_create(CLINKlab           *labInfo,
                     XYZVALUE           **sourceWhitePoint,
                     XYZVALUE           **sourceBlackPoint,
                     XYZVALUE           **sourceRelativeWhitePoint,
                     XYZVALUE           **sourceRelativeBlackPoint)
{
  int32   nXUIDs;
  CLINK   *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPElab (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */
  HQASSERT(labInfo != NULL, "labInfo NULL");

  nXUIDs = labInfo->nXUIDs ;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_Lab,
                           SPACE_CIEXYZ,
                           CL_TYPElab,
                           labStructSize(),
                           &CLINKlab_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  labUpdatePtrs(pLink, labInfo);
  cc_reservelabinfo(labInfo);

  *sourceWhitePoint = &labInfo->whitepoint;
  *sourceBlackPoint = &labInfo->blackpoint;
  *sourceRelativeWhitePoint = &labInfo->relativewhitepoint;
  *sourceRelativeBlackPoint = &labInfo->relativeblackpoint;

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = labInfo->pXUIDs[nXUIDs];

  labAssertions(pLink);

  return pLink;
}

static void lab_destroy(CLINK *pLink)
{
  labAssertions(pLink);

  cc_destroylabinfo(&pLink->p.lab);
  cc_common_destroy(pLink);
}

static Bool lab_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  SYSTEMVALUE   cie[3];
  int32         i;
  int32         ondims = 3;
  CLINKlab      *pInfo = pLink->p.lab;

  labAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  for ( i = 0 ; i < ondims ; ++i )
    cie[i] = pLink->iColorValues[i];

  NARROW3(cie, pInfo->range);

  /* Convert the Lab values to XYZ using standard formulae */
  cie[0] = (cie[0] + 16.0) / 116.0 ;
  cie[1] = cie[1] / 500.0 ;
  cie[2] = cie[2] / 200.0 ;

  MATRIX_MULTIPLY (cie, pInfo->matrix);

  for ( i = 0 ; i < ondims ; ++i ) {
    SYSTEMVALUE ftmp = cie[i] ;
    if ( ftmp >= 6.0 / 29.0 )
      ftmp = ftmp * ftmp * ftmp ;
    else {
      ftmp -= 4.0 / 29.0 ;
      ftmp *= 108.0 / 841.0 ;
    }

    cie[i] = ftmp * pInfo->whitepoint[i];
  }

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    oColorValues[i] = (USERVALUE) cie[i];

  return TRUE ;
}

void cc_getLabRange( CLINKlab *labInfo,
                     int32 index,
                     SYSTEMVALUE range[2] )
{
  labInfoAssertions(labInfo);
  HQASSERT(index < 3, "Invalid channel index");

  range[0] = labInfo->range[index * 2];
  range[1] = labInfo->range[index * 2 + 1];
}

USERVALUE gsc_LabDefaultRange(int32 index)
{
  switch (index) {
  case 0: return    0;
  case 1: return  100;
  case 2: return -100;
  case 3: return  100;
  case 4: return -100;
  case 5: return  100;
  default:
    HQFAIL("Invalid index for gsc_LabDefaultRange");
    return 100;
  }
  /* NOT REACHED */
}


static size_t labStructSize(void)
{
  return 0;
}

static void labUpdatePtrs(CLINK           *pLink,
                          CLINKlab        *labInfo)
{
  pLink->p.lab = labInfo;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void labAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPElab,
                      labStructSize(),
                      &CLINKlab_functions);

  if (pLink->p.lab != NULL) {
    labInfoAssertions(pLink->p.lab);
    HQASSERT(pLink->iColorSpace == SPACE_Lab, "Bad input color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Lab Info Common Data Access Functions
 * =====================================
 */
Bool cc_createlabinfo( CLINKlab   **labInfo,
                       OBJECT     *colorSpaceObject )
{
  CLINKlab       *pInfo;
  size_t          structSize;

  structSize = labInfoStructSize();

  pInfo = (CLINKlab *) mm_sac_alloc( mm_pool_color,
                                     structSize,
                                     MM_ALLOC_CLASS_NCOLOR );

  *labInfo = pInfo;

  if (pInfo == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)pInfo, (int)structSize);

  pInfo->refCnt = 1;
  pInfo->linkType = CL_TYPElab;
  pInfo->structSize = structSize;
  pInfo->colorspaceIsValid = FALSE;

  pInfo->matrix[0] = pInfo->matrix[1] = pInfo->matrix[2] = pInfo->matrix[3] = 1 ;
  pInfo->matrix[4] = pInfo->matrix[5] = pInfo->matrix[6] = pInfo->matrix[7] = 0 ;
  pInfo->matrix[8] = -1 ;

  pInfo->nXUIDs = 0;
  pInfo->pXUIDs = NULL;

  if (!extract_lab(pInfo, colorSpaceObject)) {
    cc_destroylabinfo(labInfo);
    return FALSE;
  }

  pInfo->colorspaceIsValid = TRUE;

  labInfoAssertions(pInfo);

  return TRUE;
}

static void freelabinfo( CLINKlab *labInfo )
{
  cc_destroy_xuids( & labInfo->nXUIDs , & labInfo->pXUIDs ) ;

  mm_sac_free(mm_pool_color, labInfo, labInfo->structSize);
}

void cc_destroylabinfo( CLINKlab **labInfo )
{
  if ( *labInfo != NULL ) {
    labInfoAssertions(*labInfo);
    CLINK_RELEASE(labInfo, freelabinfo);
  }
}

void cc_reservelabinfo( CLINKlab  *labInfo )
{
  if ( labInfo != NULL ) {
    labInfoAssertions( labInfo ) ;
    CLINK_RESERVE( labInfo ) ;
  }
}

static size_t labInfoStructSize(void)
{
  return sizeof(CLINKlab);
}

#if defined( ASSERT_BUILD )
static void labInfoAssertions(CLINKlab  *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->linkType == CL_TYPElab, "linkType not set");
  HQASSERT(pInfo->structSize == labInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

/* ---------------------------------------------------------------------- */

static NAMETYPEMATCH lab_dictmatch[] = {
/* 0 */ { NAME_WhitePoint,                     2, { OARRAY, OPACKEDARRAY }},
/* 1 */ { NAME_BlackPoint | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},
/* 2 */ { NAME_Range      | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},

/* 3 */ { NAME_RelativeWhitePoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
/* 4 */ { NAME_RelativeBlackPoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
/* 5 */ { NAME_UniqueID | OOPTIONAL,           1, { OINTEGER }},
/* 6 */ { NAME_XUID     | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }},
         DUMMY_END_MATCH
  } ;

static Bool extract_lab( CLINKlab  *pInfo,
                         OBJECT    *colorSpaceObject )
{
  int32           i ;
  OBJECT          *theo ;

  theo = oArray( *colorSpaceObject ) ;

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           oName(*theo) == &system_names[NAME_Lab],
           "extract_lab_should be called only for NAME_Lab");

  /* Move on to the dictionary */
  theo++;
  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , lab_dictmatch ))
    return FALSE ;

  theo = lab_dictmatch[ 0 ].result ; /* WhitePoint: required */
  if (!object_get_numeric_array(theo, pInfo->whitepoint, 3))
    return FALSE;
  if ((pInfo->whitepoint[0] == 0) ||
      (pInfo->whitepoint[1] == 0) ||
      (pInfo->whitepoint[2] == 0))
    return error_handler(RANGECHECK);

  theo = lab_dictmatch[ 1 ].result ; /* BlackPoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->blackpoint, 3))
      return FALSE;
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->blackpoint[i] = 0.0;
  }

  theo = lab_dictmatch[ 2 ].result ; /* Range */
  if ( theo != NULL ) {
    /* The array only gives the range for a/b components. The range of L is fixed */
    for (i = 0; i < 2; i++)
      pInfo->range[i] = gsc_LabDefaultRange(i) ;

    if (!object_get_numeric_array(theo, &pInfo->range[2], 4))
      return FALSE;

    for (i = 2; i < 6; i++) {
      /* Check that start and end of the range are the right way around */
      if (( i & 1 ) &&        /* check at odd numbers */
          ( pInfo->range[ i - 1 ] >= pInfo->range[ i ]))
        return error_handler( RANGECHECK ) ;
    }
  }
  else {
    /* Defaults */
    for (i = 0; i < 6; i++)
      pInfo->range[i] = gsc_LabDefaultRange(i) ;
  }

  theo = lab_dictmatch[ 3 ].result ; /* RelativeWhitePoint */
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

  theo = lab_dictmatch[ 4 ].result ; /* RelativeBlackPoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->relativeblackpoint, 3))
      return FALSE;
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->relativeblackpoint[i] = pInfo->blackpoint[i];
  }

  return cc_create_xuids( lab_dictmatch[ 5 ].result ,
                          lab_dictmatch[ 6 ].result ,
                          & pInfo->nXUIDs ,
                          & pInfo->pXUIDs ) ;
}

/* ---------------------------------------------------------------------- */

struct CLINKcalrgbg {
  cc_counter_t  refCnt;
  int32         linkType;
  size_t        structSize;
  Bool          colorspaceIsValid;

  int32         dimensions;

  int32         gammaPresent;
  int32         matrixPresent;

  SYSTEMVALUE   whitepoint [3];
  SYSTEMVALUE   blackpoint [3];

  SYSTEMVALUE   relativewhitepoint [3];
  SYSTEMVALUE   relativeblackpoint [3];

  SYSTEMVALUE   gamma[3];
  SYSTEMVALUE   matrix[9];

  /* XUID/UniqueID. */
  int32          nXUIDs ;
  int32         *pXUIDs ;
};

static void  calgray_destroy(CLINK *pLink);
static Bool calgray_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
static size_t calgrayStructSize(void);
static void calgrayUpdatePtrs(CLINK *pLink, CLINKcalrgbg *calrgbgInfo);
#if defined( ASSERT_BUILD )
static void calgrayAssertions(CLINK *pLink);
#else
#define calgrayAssertions(_pLink) EMPTY_STATEMENT()
#endif

static void  calrgb_destroy(CLINK *pLink);
static Bool calrgb_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
static size_t calrgbStructSize(void);
static void calrgbUpdatePtrs(CLINK *pLink, CLINKcalrgbg *calrgbgInfo);
#if defined( ASSERT_BUILD )
static void calrgbAssertions(CLINK *pLink);
#else
#define calrgbAssertions(_pLink) EMPTY_STATEMENT()
#endif

static Bool createcalrgbginfo( CLINKcalrgbg  **calrgbInfo,
                               OBJECT        *colorSpaceObject,
                               int32         linkType );
static void destroycalrgbginfo( CLINKcalrgbg  **calrgbInfo );
static void reservecalrgbginfo( CLINKcalrgbg  *calrgbInfo );
static size_t calrgbgInfoStructSize(void);

#if defined( ASSERT_BUILD )
static void calrgbgInfoAssertions(CLINKcalrgbg *pInfo);
#else
#define calrgbgInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static Bool extract_calrgbg( CLINKcalrgbg  *pInfo,
                             OBJECT        *colorSpaceObject );



static CLINKfunctions CLINKcalgray_functions =
{
  calgray_destroy,
  calgray_invokeSingle,
  NULL /* calgray_invokeBlock */,
  NULL
};

static CLINKfunctions CLINKcalrgb_functions =
{
  calrgb_destroy,
  calrgb_invokeSingle,
  NULL /* calrgb_invokeBlock */,
  NULL
};

/* ---------------------------------------------------------------------- */

/*
 * CalGray Link Data Access Functions
 * ==================================
 */
CLINK *cc_calgray_create(CLINKcalrgbg       *calgrayInfo,
                         XYZVALUE           **sourceWhitePoint,
                         XYZVALUE           **sourceBlackPoint,
                         XYZVALUE           **sourceRelativeWhitePoint,
                         XYZVALUE           **sourceRelativeBlackPoint)
{
  int32   nXUIDs;
  CLINK   *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEcalgray (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */
  HQASSERT(calgrayInfo != NULL, "calgrayInfo NULL");

  nXUIDs = calgrayInfo->nXUIDs ;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_CalGray,
                           SPACE_CIEXYZ,
                           CL_TYPEcalgray,
                           calgrayStructSize(),
                           &CLINKcalgray_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  calgrayUpdatePtrs(pLink, calgrayInfo);
  cc_reservecalgrayinfo(calgrayInfo);

  *sourceWhitePoint = &calgrayInfo->whitepoint;
  *sourceBlackPoint = &calgrayInfo->blackpoint;
  *sourceRelativeWhitePoint = &calgrayInfo->relativewhitepoint;
  *sourceRelativeBlackPoint = &calgrayInfo->relativeblackpoint;

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = calgrayInfo->pXUIDs[nXUIDs];

  calgrayAssertions(pLink);

  return pLink;
}

static void calgray_destroy(CLINK *pLink)
{
  calgrayAssertions(pLink);

  cc_destroycalgrayinfo(&pLink->p.calrgbg);
  cc_common_destroy(pLink);
}

static Bool calgray_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32         i;
  SYSTEMVALUE   cie[3];
  SYSTEMVALUE   ftmp;
  CLINKcalrgbg  *pInfo = pLink->p.calrgbg;

  calgrayAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  ftmp = pLink->iColorValues[0];
  NARROW_01(ftmp);

  if ( pInfo->gammaPresent )
    ftmp = pow(ftmp, pInfo->gamma[0]);

  cie[0] = ftmp * pInfo->whitepoint[0];
  cie[1] = ftmp * pInfo->whitepoint[1];
  cie[2] = ftmp * pInfo->whitepoint[2];

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    oColorValues[i] = (USERVALUE) cie[i];

  return TRUE ;
}

static size_t calgrayStructSize(void)
{
  return 0;
}

static void calgrayUpdatePtrs(CLINK           *pLink,
                              CLINKcalrgbg    *calgrayInfo)
{
  pLink->p.calrgbg = calgrayInfo;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void calgrayAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEcalgray,
                      calgrayStructSize(),
                      &CLINKcalgray_functions);

  if (pLink->p.calrgbg != NULL) {
    calrgbgInfoAssertions(pLink->p.calrgbg);
    HQASSERT(pLink->iColorSpace == SPACE_CalGray, "Bad input color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * CalRGB Link Data Access Functions
 * =================================
 */
CLINK *cc_calrgb_create(CLINKcalrgbg       *calrgbInfo,
                        XYZVALUE           **sourceWhitePoint,
                        XYZVALUE           **sourceBlackPoint,
                        XYZVALUE           **sourceRelativeWhitePoint,
                        XYZVALUE           **sourceRelativeBlackPoint)
{
  int32   nXUIDs;
  CLINK   *pLink;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEcalrgb (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */
  HQASSERT(calrgbInfo != NULL, "calrgbInfo NULL");

  nXUIDs = calrgbInfo->nXUIDs ;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_CalRGB,
                           SPACE_CIEXYZ,
                           CL_TYPEcalrgb,
                           calrgbStructSize(),
                           &CLINKcalrgb_functions,
                           nXUIDs);
  if ( pLink == NULL )
    return NULL ;

  calrgbUpdatePtrs(pLink, calrgbInfo);
  cc_reservecalrgbinfo(pLink->p.calrgbg);

  *sourceWhitePoint = &calrgbInfo->whitepoint;
  *sourceBlackPoint = &calrgbInfo->blackpoint;
  *sourceRelativeWhitePoint = &calrgbInfo->relativewhitepoint;
  *sourceRelativeBlackPoint = &calrgbInfo->relativeblackpoint;

  while ((--nXUIDs) >= 0 )
    pLink->idslot[nXUIDs] = calrgbInfo->pXUIDs[nXUIDs];

  calrgbAssertions(pLink);

  return pLink;
}

static void calrgb_destroy(CLINK *pLink)
{
  calrgbAssertions(pLink);

  cc_destroycalrgbinfo(&pLink->p.calrgbg);
  cc_common_destroy(pLink);
}

static Bool calrgb_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  SYSTEMVALUE   cie[3];
  int32         i;
  CLINKcalrgbg  *pInfo = pLink->p.calrgbg;

  calrgbAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  for ( i = 0 ; i < pInfo->dimensions ; ++i ) {
    cie[i] = pLink->iColorValues[i];
    NARROW_01(cie[i]);

    if ( pInfo->gammaPresent )
      cie[i] = pow(cie[i], pInfo->gamma[i]);
  }

  if ( pInfo->matrixPresent )
    MATRIX_MULTIPLY (cie, pInfo->matrix);

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++)
    oColorValues[i] = (USERVALUE) cie[i];

  return TRUE ;
}


static size_t calrgbStructSize(void)
{
  return 0;
}

static void calrgbUpdatePtrs(CLINK           *pLink,
                             CLINKcalrgbg    *calrgbInfo)
{
  pLink->p.calrgbg = calrgbInfo;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void calrgbAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEcalrgb,
                      calrgbStructSize(),
                      &CLINKcalrgb_functions);

  if (pLink->p.calrgbg != NULL) {
    calrgbgInfoAssertions(pLink->p.calrgbg);
    HQASSERT(pLink->iColorSpace == SPACE_CalRGB, "Bad input color space");
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * CalGray Info Data Access Functions
 * ==================================
 */

Bool cc_createcalgrayinfo( CLINKcalrgbg  **calgrayInfo,
                           OBJECT        *colorSpaceObject )
{
  return createcalrgbginfo(calgrayInfo,
                           colorSpaceObject,
                           CL_TYPEcalgray );
}

void cc_destroycalgrayinfo( CLINKcalrgbg **calgrayInfo )
{
  HQASSERT((*calgrayInfo)->linkType == CL_TYPEcalgray, "linkType not set");
  destroycalrgbginfo(calgrayInfo);
}

void cc_reservecalgrayinfo( CLINKcalrgbg *calgrayInfo )
{
  HQASSERT(calgrayInfo->linkType == CL_TYPEcalgray, "linkType not set");
  reservecalrgbginfo(calgrayInfo);
}

/* ---------------------------------------------------------------------- */

/*
 * CalRGB Info Data Access Functions
 * =================================
 */

Bool cc_createcalrgbinfo( CLINKcalrgbg  **calrgbInfo,
                          OBJECT        *colorSpaceObject )
{
  return createcalrgbginfo(calrgbInfo,
                           colorSpaceObject,
                           CL_TYPEcalrgb );
}

void cc_destroycalrgbinfo( CLINKcalrgbg **calrgbInfo )
{
  HQASSERT((*calrgbInfo)->linkType == CL_TYPEcalrgb, "linkType not set");
  destroycalrgbginfo(calrgbInfo);
}

void cc_reservecalrgbinfo( CLINKcalrgbg *calrgbInfo )
{
  HQASSERT(calrgbInfo->linkType == CL_TYPEcalrgb, "linkType not set");
  reservecalrgbginfo(calrgbInfo);
}

/* ---------------------------------------------------------------------- */

/*
 * CalGray + CalRGB Info Common Data Access Functions
 * ==================================================
 */
static Bool createcalrgbginfo( CLINKcalrgbg  **calrgbgInfo,
                               OBJECT        *colorSpaceObject,
                               int32         linkType )
{
  CLINKcalrgbg   *pInfo;
  size_t         structSize;

  structSize = calrgbgInfoStructSize();

  pInfo = (CLINKcalrgbg *) mm_sac_alloc( mm_pool_color,
                                         structSize,
                                         MM_ALLOC_CLASS_NCOLOR );

  *calrgbgInfo = pInfo;

  if (pInfo == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)pInfo, (int)structSize);

  pInfo->refCnt = 1;
  pInfo->linkType = linkType;
  pInfo->structSize = structSize;
  pInfo->colorspaceIsValid = FALSE;

  if (pInfo->linkType == CL_TYPEcalgray)
    pInfo->dimensions = 1;
  else
    pInfo->dimensions = 3;

  pInfo->gammaPresent = FALSE;
  pInfo->matrixPresent = FALSE;

  pInfo->nXUIDs = 0;
  pInfo->pXUIDs = NULL;

  if (!extract_calrgbg(pInfo, colorSpaceObject)) {
    destroycalrgbginfo(calrgbgInfo);
    return FALSE;
  }

  pInfo->colorspaceIsValid = TRUE;

  calrgbgInfoAssertions(pInfo);

  return TRUE;
}

static void freecalrgbginfo( CLINKcalrgbg *calrgbgInfo )
{
  cc_destroy_xuids( & calrgbgInfo->nXUIDs , & calrgbgInfo->pXUIDs ) ;

  mm_sac_free(mm_pool_color, calrgbgInfo, calrgbgInfo->structSize);
}

void destroycalrgbginfo( CLINKcalrgbg **calrgbgInfo )
{
  if ( *calrgbgInfo != NULL ) {
    calrgbgInfoAssertions(*calrgbgInfo);
    CLINK_RELEASE(calrgbgInfo, freecalrgbginfo);
  }
}

void reservecalrgbginfo( CLINKcalrgbg *calrgbgInfo )
{
  if ( calrgbgInfo != NULL ) {
    calrgbgInfoAssertions( calrgbgInfo ) ;
    CLINK_RESERVE( calrgbgInfo ) ;
  }
}

static size_t calrgbgInfoStructSize(void)
{
  return sizeof(CLINKcalrgbg);
}

#if defined( ASSERT_BUILD )
static void calrgbgInfoAssertions(CLINKcalrgbg *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->linkType == CL_TYPEcalgray ||
           pInfo->linkType == CL_TYPEcalrgb, "linkType not set");
  HQASSERT(pInfo->structSize == calrgbgInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

/* ---------------------------------------------------------------------- */

static NAMETYPEMATCH calrgb_dictmatch[] = {
/* 0 */ { NAME_WhitePoint,                     2, { OARRAY, OPACKEDARRAY }},
/* 1 */ { NAME_BlackPoint | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},
/* 2 */ { NAME_Gamma      | OOPTIONAL,         4, { OARRAY, OPACKEDARRAY, OINTEGER, OREAL }},
/* 3 */ { NAME_Matrix     | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},

/* 4 */ { NAME_RelativeWhitePoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
/* 5 */ { NAME_RelativeBlackPoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
/* 6 */ { NAME_UniqueID | OOPTIONAL,           1, { OINTEGER }},
/* 7 */ { NAME_XUID     | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }},
         DUMMY_END_MATCH
  } ;

static Bool extract_calrgbg( CLINKcalrgbg *pInfo,
                             OBJECT       *colorSpaceObject )
{
  int32           i ;
  OBJECT          *theo ;

  theo = oArray( *colorSpaceObject ) ;

  /* We start off with theo pointing to the Name at the start of the colorspace
   */
  HQASSERT(oType(*theo) == ONAME &&
           (oName(*theo) == &system_names[NAME_CalRGB] ||
            oName(*theo) == &system_names[NAME_CalGray]),
           "extract_calrgbg should be called only for NAME_CalGray or NAME_CalRGB");

  /* Move on to the dictionary */
  theo++;
  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! dictmatch( theo , calrgb_dictmatch ))
    return FALSE ;

  theo = calrgb_dictmatch[ 0 ].result ; /* WhitePoint: required */
  if (!object_get_numeric_array(theo, pInfo->whitepoint, 3))
    return FALSE;

  if ((pInfo->whitepoint[0] == 0) ||
      (pInfo->whitepoint[1] == 0) ||
      (pInfo->whitepoint[2] == 0))
    return error_handler(RANGECHECK);

  theo = calrgb_dictmatch[ 1 ].result ; /* BlackPoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->blackpoint, 3))
      return FALSE;
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->blackpoint[i] = 0.0;
  }

  theo = calrgb_dictmatch[ 2 ].result ; /* Gamma */
  if ( theo != NULL ) {
    if (pInfo->linkType == CL_TYPEcalgray) {
      switch ( oType(*theo)) {
      case OINTEGER:
        pInfo->gamma[0] = oInteger( *theo );
        break;
      case OREAL:
        pInfo->gamma[0] = oReal( *theo );
        break;
      default:
        return error_handler( TYPECHECK );
      }
      pInfo->gamma[1] = 0;
      pInfo->gamma[2] = 0;
      if (pInfo->gamma[0] != 1)
        pInfo->gammaPresent = TRUE;
    }
    else {
      if (!object_get_numeric_array(theo, pInfo->gamma, pInfo->dimensions))
        return FALSE;

      if (pInfo->gamma[0] != 1 ||
          pInfo->gamma[1] != 1 ||
          pInfo->gamma[2] != 1)
        pInfo->gammaPresent = TRUE;
    }
  }

  theo = calrgb_dictmatch[ 3 ].result ; /* Matrix */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->matrix, 9))
      return FALSE;
    pInfo->matrixPresent = TRUE;
  }

  theo = calrgb_dictmatch[ 4 ].result ; /* RelativeWhitePoint */
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

  theo = calrgb_dictmatch[ 5 ].result ; /* RelativeBlackPoint */
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, pInfo->relativeblackpoint, 3))
      return FALSE;
  }
  else {
    for (i = 0; i < 3; i++)
      pInfo->relativeblackpoint[i] = pInfo->blackpoint[i];
  }

  return cc_create_xuids( calrgb_dictmatch[ 6 ].result ,
                          calrgb_dictmatch[ 7 ].result ,
                          & pInfo->nXUIDs ,
                          & pInfo->pXUIDs ) ;
}

/* Log stripped */
