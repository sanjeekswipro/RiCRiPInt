/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscphotoink.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to set up colour chains.
 */

#include "core.h"

#include "debugging.h"          /* debug_get_colorspace */
#include "gu_chan.h"            /* guc_deviceColorSpace */
#include "monitor.h"            /* monitorf */
#include "swerrors.h"           /* VMERROR */

#include "gs_colorpriv.h"       /* CLINK */
#include "gscsmplkpriv.h"       /* cc_cmykton_create */


/*
 * Photoink Info Data
 * ==================
 */
#define MAX_PHOTOINK_INPUTS   (4)
#define LUTSIZE               (256)
#define LUTCODE_notset        (COLORVALUE_INVALID)

static uint32 guc_photoinkInfoStructSize(int32 nDeviceColorants) ;
static void guc_photoinkInfoUpdatePtrs( GUCR_PHOTOINK_INFO *pInfo,int32 nDeviceColorants ) ;

#if defined( ASSERT_BUILD )
static void guc_photoinkInfoAssertions(const GUCR_PHOTOINK_INFO *pInfo) ;
#else
#define guc_photoinkInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static Bool guc_findColorantMappings(CLINK              *pLink,
                                     GUCR_PHOTOINK_INFO *pInfo,
                                     GUCR_RASTERSTYLE   *pRasterStyle,
                                     COLORANTINDEX      *iColorants,
                                     int32              nDeviceColorants);


struct GUCR_PHOTOINK_INFO {
  cc_counter_t    refCnt;
  size_t          structSize;

  COLORSPACE_ID   calibrationColorSpace;

  int32           nColorants;
  int32           nDeviceColorants;

  uint32          nMappedColorants[MAX_PHOTOINK_INPUTS] ;
  COLORANTINDEX   iColorants[MAX_PHOTOINK_INPUTS] ;

  COLORANTINDEX   *oColorants;            /* nDeviceColorants */
  COLORANTINDEX   *uColorants;            /* nDeviceColorants */
  USERVALUE       *tmpColorValues;        /* nDeviceColorants */

  COLORVALUE      (*transforms)[LUTSIZE]; /* nDeviceColorants */
};


/* ---------------------------------------------------------------------- */

/*
 * Photoink Info Data Access Functions
 * ===================================
 */
static Bool guc_createphotoinkinfo(GUCR_PHOTOINK_INFO **photoinkInfo,
                                   COLORSPACE_ID      calibrationColorSpace,
                                   int32              nColorants,
                                   int32              nDeviceColorants)
{
  int32               i;
  GUCR_PHOTOINK_INFO  *pInfo;
  mm_size_t           structSize;

  HQASSERT(nColorants <= MAX_PHOTOINK_INPUTS, "nColorants is rather large") ;

  structSize = guc_photoinkInfoStructSize(nDeviceColorants);

  pInfo = (GUCR_PHOTOINK_INFO *) mm_sac_alloc(mm_pool_color,
                                              structSize,
                                              MM_ALLOC_CLASS_NCOLOR);

  *photoinkInfo = pInfo;

  if (pInfo == NULL)
    return error_handler(VMERROR);

  guc_photoinkInfoUpdatePtrs(pInfo, nDeviceColorants);

  pInfo->refCnt = 1;
  pInfo->structSize = CAST_SIZET_TO_UINT32(structSize);

  pInfo->calibrationColorSpace = calibrationColorSpace;

  pInfo->nColorants = nColorants;
  pInfo->nDeviceColorants = nDeviceColorants;

  for (i = 0; i < MAX_PHOTOINK_INPUTS; i++) {
    pInfo->nMappedColorants[i] = 0;
    pInfo->iColorants[i] = COLORANTINDEX_UNKNOWN;
  }

  for (i = 0; i < nDeviceColorants; i++) {
    pInfo->oColorants[i] = COLORANTINDEX_UNKNOWN;
    pInfo->uColorants[i] = COLORANTINDEX_UNKNOWN;
    pInfo->tmpColorValues[i] = 0.0;
  }

  for (i = 0; i < nDeviceColorants; i++) {
    int32   j ;
    for (j = 0; j < LUTSIZE; j++)
      pInfo->transforms[i][j] = LUTCODE_notset;   /* White in additive space which is
                                                   * where we are after transfers/calibration */
  }

  guc_photoinkInfoAssertions(pInfo);

  return TRUE;
}

static void freephotoinkinfo(GUCR_PHOTOINK_INFO *photoinkInfo)
{
  mm_sac_free(mm_pool_color, photoinkInfo, photoinkInfo->structSize);
}

void guc_destroyphotoinkinfo(GUCR_PHOTOINK_INFO **photoinkInfo)
{
  if ( *photoinkInfo != NULL ) {
    guc_photoinkInfoAssertions(*photoinkInfo);
    CLINK_RELEASE(photoinkInfo, freephotoinkinfo);
  }
}

static Bool singlePhotoinkTransform(const GUCR_PHOTOINK_INFO *photoinkInfo,
                                    CLINK              *pLink,
                                    int32              customtransformIdx,
                                    COLORANTINDEX      *colorantMap,
                                    uint32             nMappedColorants,
                                    USERVALUE          color,
                                    int32              c255i)
{
  uint32    j ;
  USERVALUE *tmpColorValues = photoinkInfo->tmpColorValues ;

  HQASSERT(photoinkInfo != NULL, "photoinkInfo is NULL") ;
  HQASSERT(customtransformIdx >= 0 && customtransformIdx < photoinkInfo->nColorants,
           "customtransformIdx out of range");
  HQASSERT(colorantMap != NULL, "colorantMap is NULL") ;

  HQASSERT(pLink->iColorValues[ customtransformIdx ] == 0.0f,
           "Input color value should be zero") ;

  /* For CMYK, we must flip the input value because we are currently in
   * additive space, ie. after final transfers/calibration */
  if (photoinkInfo->calibrationColorSpace == SPACE_DeviceCMYK)
    pLink->iColorValues[ customtransformIdx ] = 1.0f - color ;
  else
    pLink->iColorValues[ customtransformIdx ] = color ;
  if (! (pLink->functions->invokeSingle)( pLink , tmpColorValues ) )
    return FALSE ;

  /* Put it back to 0.0f. */
  pLink->iColorValues[ customtransformIdx ] = 0.0f ;

  for (j = 0; j < nMappedColorants; j++) {
    COLORANTINDEX   otherColorant = colorantMap[j];
    /* Always flip the output value because we are currently in
     * additive space and the custom transforms go to DeviceN (subtractive).
     */
    HQASSERT(photoinkInfo->transforms[ otherColorant ][ c255i ] == LUTCODE_notset,
             "Photoink transform already set");
    photoinkInfo->transforms[ otherColorant ][ c255i ] =
                        FLOAT_TO_COLORVALUE(1 - tmpColorValues[ otherColorant ]) ;
  }

  return TRUE;
}

static Bool populatePhotoinkTransform(GUCR_RASTERSTYLE *pRasterStyle,
                                      const GUCR_PHOTOINK_INFO *photoinkInfo,
                                      CLINK *pLink,
                                      COLORANTINDEX *iColorants)
{
  double lutsizef = 1.0 / (double)(LUTSIZE - 1) ;
  int32 i, nColorants = pLink->n_iColorants;

  for ( i = 0; i < nColorants; ++i ) {
    COLORANTINDEX *colorantMap;
    uint32 nMappedColorants;
    int32 c255i;

    colorantMap = guc_getColorantMapping(pRasterStyle, iColorants[i]);
    HQASSERT(colorantMap != NULL, "Colorant mapping must exist for photoink");

    nMappedColorants = photoinkInfo->nMappedColorants[i];

    for ( c255i = 0; c255i < LUTSIZE; ++c255i ) {
      if ( !singlePhotoinkTransform(photoinkInfo, pLink, i,
                                    colorantMap, nMappedColorants,
                                    (USERVALUE)(c255i * lutsizef), c255i) )
        return FALSE;
    }
  }

  return TRUE;
}

Bool guc_interpolatePhotoinkTransform(const GUCR_PHOTOINK_INFO  *photoinkInfo,
                                      COLORANTINDEX       *colorantMap,
                                      uint32              nMappedColorants,
                                      COLORVALUE          icolor,
                                      COLORVALUE          *ocolors)
{
  int32      c255i_lo;
  int32      fraction;
  uint32     i;
  COLORVALUE *transform;

  /* The icolor is separated into an index into the 8 bit lut and and 8 bit fraction */
  c255i_lo = icolor >> 8;
  fraction = icolor & 0xFF;

  HQASSERT(photoinkInfo != NULL, "photoinkInfo is NULL") ;
  HQASSERT(colorantMap != NULL, "colorantMap is NULL") ;
  HQASSERT(ocolors != NULL, "ocolors is NULL") ;

  /* Apply the cached custom transforms by interpolating between the 2 nearest grid points */

  for (i = 0; i < nMappedColorants; i++) {
    COLORANTINDEX   colorant = colorantMap[i];
    int32  lo;

    transform = photoinkInfo->transforms[colorant] ;

    lo = transform[c255i_lo];
    HQASSERT(lo != LUTCODE_notset, "Transform should already be set");

    /* If fraction is zero, we're at a grid-point, and don't need the next value
     * up. This also protects against trying to address off the end of the table.
     */
    if ( fraction == 0 ) {
      ocolors[i] = CAST_TO_COLORVALUE(lo);
    }
    else {
      int32  c255i_hi = c255i_lo + 1 ;
      int32  hi ;
      HQASSERT(c255i_hi < LUTSIZE, "color too large");

      hi = transform[c255i_hi];
      HQASSERT(hi != LUTCODE_notset, "Transform should already be set");

      /* Interpolate and scale the numbers for the fraction bits */
      hi = hi * fraction;
      lo = lo * ( 256 - fraction) ;
      /* Finally compute the values */
      ocolors[ i ] = CAST_TO_COLORVALUE((lo + hi + 128) >> 8);
    }
  }

  return TRUE ;
}

static uint32 guc_photoinkInfoStructSize(int32 nDeviceColorants)
{
  return sizeof(GUCR_PHOTOINK_INFO) +
         nDeviceColorants * sizeof(COLORANTINDEX) +      /* oColorants */
         nDeviceColorants * sizeof(COLORANTINDEX) +      /* uColorants */
         nDeviceColorants * sizeof(USERVALUE) +          /* oColorValues */
         nDeviceColorants * sizeof(USERVALUE) * LUTSIZE; /* transform arrays */
}

static void guc_photoinkInfoUpdatePtrs( GUCR_PHOTOINK_INFO *pInfo,int32 nDeviceColorants )
{
  HQASSERT( pInfo , "pInfo NULL in photoinkInfoUpdatePtrs" ) ;

  pInfo->oColorants     = ( COLORANTINDEX * )( pInfo + 1 ) ;
  pInfo->uColorants     = ( COLORANTINDEX * )( pInfo->oColorants + nDeviceColorants ) ;
  pInfo->tmpColorValues = ( USERVALUE * )( pInfo->uColorants + nDeviceColorants ) ;
  pInfo->transforms     = ( COLORVALUE (*)[LUTSIZE] )( pInfo->tmpColorValues + nDeviceColorants ) ;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the photoink info access functions.
 */
static void guc_photoinkInfoAssertions(const GUCR_PHOTOINK_INFO *pInfo)
{
  int32   nDeviceColorants = pInfo->nDeviceColorants ;

  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == guc_photoinkInfoStructSize(pInfo->nDeviceColorants),
           "structure size not correct");
  HQASSERT(pInfo->refCnt == 0 || pInfo->refCnt == 1, "refCnt should be 0 or 1");

  HQASSERT(pInfo->calibrationColorSpace == SPACE_DeviceGray ||
           pInfo->calibrationColorSpace == SPACE_DeviceRGB  ||
           pInfo->calibrationColorSpace == SPACE_DeviceCMYK,
           "calibrationColorSpace not set") ;

  HQASSERT(pInfo->oColorants      == ( COLORANTINDEX * )( pInfo + 1 ) ,
           "oColorants not set" ) ;
  HQASSERT(pInfo->uColorants      == ( COLORANTINDEX * )( pInfo->oColorants + nDeviceColorants ) ,
           "uColorants not set" ) ;
  HQASSERT(pInfo->tmpColorValues  == ( USERVALUE * )( pInfo->uColorants + nDeviceColorants ) ,
           "tmpColorValues not set" ) ;
  HQASSERT(pInfo->transforms      == ( COLORVALUE (*)[LUTSIZE] )( pInfo->tmpColorValues + nDeviceColorants ) ,
           "transforms not set" ) ;
}
#endif


/* -------------------------------------------------------------------------- */

/** Try probing a range of CMYK values to determine the colorant sets for these. */
Bool guc_findPhotoinkColorantIndices(GUCR_RASTERSTYLE   *pRasterStyle,
                                     GUCR_PHOTOINK_INFO **pPhotoinkInfo)
{
  OBJECT              customProcedure = OBJECT_NOTVM_NOTHING;
  CLINK               *pLink;
  GUCR_PHOTOINK_INFO  *pInfo;
  int32               nColorants;
  int32               nDeviceColorants;
  DEVICESPACEID       deviceSpaceId;
  COLORSPACE_ID       calibrationColorSpace ;
  DEVICESPACEID       calibrationDeviceSpace;
  CLINK               * (*customLinkCreator)(OBJECT customProcedure, int32 n_oColorants);
  COLORANTINDEX       iColorants[MAX_PHOTOINK_INPUTS];

  HQASSERT(pRasterStyle != NULL, "pRasterStyle NULL");
  HQASSERT(*pPhotoinkInfo == NULL, "Expected photoinkInfo == NULL");

  guc_deviceColorSpace(pRasterStyle, &deviceSpaceId, &nDeviceColorants);
  if (deviceSpaceId != DEVICESPACE_N) {
    return TRUE;
  }

  guc_calibrationColorSpace(pRasterStyle, &calibrationColorSpace);
  if (calibrationColorSpace == SPACE_notset) {
    return TRUE;
  }

  /* If we get here, we should have a PhotoInk device, but no guarantees, so
   * later on if we get non-unique mappings we'll deliberately fire an assert.
   */

  switch (calibrationColorSpace) {
  case SPACE_DeviceGray:
    nColorants = 1;
    calibrationDeviceSpace = DEVICESPACE_Gray;
    customLinkCreator = cc_grayton_create;
    break;
  case SPACE_DeviceRGB:
    nColorants = 3;
    calibrationDeviceSpace = DEVICESPACE_RGB;
    customLinkCreator = cc_rgbton_create;
    break;
  case SPACE_DeviceCMYK:
    nColorants = 4;
    calibrationDeviceSpace = DEVICESPACE_CMYK;
    customLinkCreator = cc_cmykton_create;
    break;
  default:
    HQFAIL("Invalid calibrationColorSpace");
    return FALSE;
  }

  if (!guc_simpleDeviceColorSpaceMapping(pRasterStyle, calibrationDeviceSpace,
                                         iColorants, nColorants))
    return FALSE;

  guc_CustomConversion(pRasterStyle, calibrationDeviceSpace, &customProcedure);

  if (!guc_createphotoinkinfo(&pInfo, calibrationColorSpace,
                              nColorants, nDeviceColorants))
    return FALSE ;

  pLink = customLinkCreator(customProcedure, nDeviceColorants);
  if ( pLink == NULL ) {
    guc_destroyphotoinkinfo(&pInfo);
    return FALSE ;
  }

  if ( !guc_findColorantMappings(pLink, pInfo, pRasterStyle,
                                 iColorants, nDeviceColorants) ||
       !populatePhotoinkTransform(pRasterStyle, pInfo, pLink, iColorants) ) {
    pLink->functions->destroy(pLink);
    guc_destroyphotoinkinfo(&pInfo);
    return FALSE;
  }

  *pPhotoinkInfo = pInfo;

  return TRUE ;
}

static Bool guc_findColorantMappings(CLINK              *pLink,
                                     GUCR_PHOTOINK_INFO *pInfo,
                                     GUCR_RASTERSTYLE   *pRasterStyle,
                                     COLORANTINDEX      *iColorants,
                                     int32              nDeviceColorants)
{
  int32             i;
  COLORANTINDEX     *oColorants = NULL;
  COLORANTINDEX     *uColorants = NULL;
  USERVALUE         *tmpColorValues = NULL;
  int32             nColorants;

  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pInfo != NULL, "pInfo NULL");
  HQASSERT(iColorants != NULL, "iColorants NULL");
  HQASSERT(pLink->n_iColorants <= MAX_PHOTOINK_INPUTS, "Invalid nColorants");

  nColorants = pLink->n_iColorants;

  for (i = 0; i < nColorants; i++)
    pLink->iColorValues[0] = 0;

  oColorants = pInfo->oColorants ;
  uColorants = pInfo->uColorants ;
  tmpColorValues = pInfo->tmpColorValues ;

  {
    int32 j, k, n;
    int32 nColorantsRemaining = nDeviceColorants ;

    for ( i = 0 ; i < nColorants ; ++i ) {
      COLORANTINDEX ci ;
      int32 inc ;

      for ( k = 0 ; k < nDeviceColorants ; ++k )
        oColorants[ k ] = COLORANTINDEX_UNKNOWN ;

      inc = LUTSIZE/32 ;
      for ( j = inc - 1 ; j < LUTSIZE ; j += inc ) {
        pLink->iColorValues[ i ] =
          ( j == LUTSIZE - 1 ? 1.0f : j / (float) (LUTSIZE - 1)) ;

        if (! (pLink->functions->invokeSingle)( pLink , tmpColorValues ) )
          return FALSE ;

        /* Analyze the results to collect the union of color mappings.
         */
        for ( k = 0 ; k < nDeviceColorants ; ++k ) {
          if ( tmpColorValues[ k ] != 0.0f )
            oColorants[ k ] = k ;
        }
      }
      /* Put it back to 0.0f. */
      pLink->iColorValues[ i ] = 0.0f ;

      /* Check the results of mapping one of CMYK to make sure there are no intersections. */
      n = 0 ;
      for ( k = 0 ; k < nDeviceColorants ; ++k ) {
        COLORANTINDEX oColorant = oColorants[ k ] ;
        if ( oColorant != COLORANTINDEX_UNKNOWN ) {
          COLORANTINDEX uColorant = uColorants[ k ] ;
          if ( uColorant != COLORANTINDEX_UNKNOWN ) {
            /* Need to undo any mappings we've set. */
            HQFAIL( "Got non unique mappings for a (supposedly) PhotoInk device; "
                    "Not sure what this means" ) ;
            while ((--i) >= 0 ) {
              ci = iColorants[i];
              HQASSERT( ci != COLORANTINDEX_ALL &&
                        ci != COLORANTINDEX_NONE &&
                        ci != COLORANTINDEX_UNKNOWN ,
                        "gsc_findCMYKColorantIndices: ci either all, none or unknown" ) ;
              (void)guc_setColorantMapping(pRasterStyle, ci, NULL, 0) ;
            }
            return FALSE ;
          }
          else
            uColorants[ k ] = oColorant ;

          /* Pack the colorants down to the bottom. Only the first n elements will be
           * used in guc_setColorantMapping, the rest ignored.
           */
          oColorants[ n++ ] = oColorant ;
        }
      }

      /* Now set these colorants. */
      if ( ! guc_setColorantMapping(pRasterStyle, iColorants[i], oColorants, n) )
        return FALSE ;

      pInfo->nMappedColorants[i] = (uint32) n;
      pInfo->iColorants[i] = iColorants[i];
      nColorantsRemaining -= n;
    }

    HQASSERT(nColorantsRemaining == 0, "custom transforms appear to give incorrect mappings") ;
  }

  return TRUE;
}

/** Is 'colorant' amongst the set of calibration colorants? */
Bool guc_photoink_colorant(const GUCR_RASTERSTYLE *pRasterStyle,
                           COLORANTINDEX          colorant)
{
  int32 i;
  const GUCR_PHOTOINK_INFO *photoinkInfo = guc_photoinkInfo(pRasterStyle);

  HQASSERT(photoinkInfo != NULL, "photoinkInfo == NULL");
  HQASSERT(photoinkInfo->refCnt == 1, "Expected a refCnt of 1");
  HQASSERT(!guc_backdropRasterStyle(pRasterStyle), "Expected device rasterstyle");

  if (photoinkInfo != NULL)
    guc_photoinkInfoAssertions(photoinkInfo);

  if (colorant == COLORANTINDEX_NONE)
    return TRUE;

  for (i = 0; i < photoinkInfo->nColorants; i++) {
    if (colorant == photoinkInfo->iColorants[i])
      return TRUE;
  }

  return FALSE;
}

#if defined(DEBUG_BUILD)

void debug_print_gucr_photoink(GUCR_PHOTOINK_INFO *photoink, int32 indent)
{
  char *spaces = "                                        " ;
  int32 i = strlen_int32(spaces) - indent ;

  if ( i < 0 )
    i = 0 ;

  spaces += i ;

  if ( photoink ) {
    int32 i ;
    monitorf((uint8 *)"<<\n") ;
    monitorf((uint8 *)"%s  References %d\n", spaces, photoink->refCnt) ;
    monitorf((uint8 *)"%s  Struct size %d\n", spaces, photoink->structSize) ;
    monitorf((uint8 *)"%s  Calibration space %s\n", spaces,
             debug_get_colorspace(photoink->calibrationColorSpace)) ;
    monitorf((uint8 *)"%s  # Colorants %d\n", spaces, photoink->nColorants) ;
    monitorf((uint8 *)"%s  # Device colorants %d\n", spaces, photoink->nDeviceColorants) ;
    monitorf((uint8 *)"%s  # Mapped Colorants [", spaces) ;
    for ( i = 0 ; i < photoink->nColorants ; ++i ) {
      monitorf((uint8 *)" %d", photoink->nMappedColorants[i]) ;
    }
    monitorf((uint8 *)" ]\n") ;
    monitorf((uint8 *)"%s  iColorants [", spaces) ;
    for ( i = 0 ; i < photoink->nColorants ; ++i ) {
      monitorf((uint8 *)" %d", photoink->iColorants[i]) ;
    }
    monitorf((uint8 *)" ]\n") ;
    monitorf((uint8 *)"%s  oColorants [", spaces) ;
    for ( i = 0 ; i < photoink->nDeviceColorants ; ++i ) {
      monitorf((uint8 *)" %d", photoink->oColorants[i]) ;
    }
    monitorf((uint8 *)" ]\n") ;
    monitorf((uint8 *)"%s  uColorants [", spaces) ;
    for ( i = 0 ; i < photoink->nDeviceColorants ; ++i ) {
      monitorf((uint8 *)" %d", photoink->uColorants[i]) ;
    }
    monitorf((uint8 *)" ]\n") ;
    monitorf((uint8 *)"%s  Colour values [", spaces) ;
    for ( i = 0 ; i < photoink->nDeviceColorants ; ++i ) {
      monitorf((uint8 *)" %f", photoink->tmpColorValues[i]) ;
    }
    monitorf((uint8 *)" ]\n") ;
    monitorf((uint8 *)"%s  Transforms NYP\n", spaces) ;
    monitorf((uint8 *)"%s>>\n", spaces) ;
  } else {
    monitorf((uint8 *)" none\n") ;
  }
}

#endif  /* DEBUG_BUILD */


/* Log stripped */
