/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscsmplk.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS3: Simple device space conversion link functions for converting between
 * rgb, rgbk, cmyk, cmy gray and Lab. This includes BlackGeneration and
 * UnderColorRemoval logic.
 */

#include "core.h"
#include "hqmemcpy.h"           /* HqMemCpy */
#include "swerrors.h"           /* TYPECHECK */
#include "mps.h"                /* mps_res_t */
#include "gcscan.h"             /* ps_scan_field */

#include "control.h"            /* interpreter - *** Remove this *** */
#include "fileio.h"             /* FILELIST */
#include "functns.h"            /* fn_evaluate */
#include "namedef_.h"           /* NAME_pop */
#include "render.h"             /* inputpage */
#include "routedev.h"           /* DEVICE_INVALID_CONTEXT */
#include "spdetect.h"           /* detect_setcolor_separation */
#include "stacks.h"             /* operandstack - *** Remove this *** */

#include "gs_callps.h"          /* callpscache functions */
#include "gs_colorpriv.h"       /* CLINK */
#include "gscdevcipriv.h"       /* OP_CMYK_OVERPRINT_MASK */
#include "gscparamspriv.h"      /* colorPageParams */

#include "gscsmplkpriv.h"       /* extern's */
#include "pscalc.h"
#include "monitor.h"

/*============================================================================
 *
 * rgbtocmyk definitions.
 */

/* We need to identify all the unique characteristics that identify a CLINK.
 * The CLINK type, colorspace, colorant set are orthogonal to these items.
 * The device color space & colorants are defined as fixed.
 * The following define the number of id slots needed by the various CLINK
 * types in this file.  See individual create routines for the actual values
 * used in the IDs.
 */
#define CLID_SIZEnoslotsneeded  (0)
#define CLID_SIZErgbtocmyk      (3)

/* The initial value for the uniqueId of a UCR or BG procedure. We just bump
 * the Id whenever we see a new one without checking if it's one we've seen
 * before in general. But we do special case 2 sets of UCR & BG procedures for
 * optimisations.
 */
#define UCRBG_NOOP     (-1)
#define UCRBG_MAX      ( 0)
#define UCRBG_MIN      ( 1)
#define UCRBG_INITIAL  ( 2)

typedef struct GS_PROC {
  OBJECT      psObject;
  CALLPSCACHE *cpsc;
} GS_PROC;

struct GS_RGBtoCMYKinfo {
  cc_counter_t refCnt;
  size_t       structSize;
  GS_PROC      ucr;
  GS_PROC      bg;
};

struct CLINKRGBtoCMYKinfo {
  GS_RGBtoCMYKinfo    *rgbtocmykInfo;
  Bool                preserveBlack ;
};

static size_t rgbtocmykStructSize(void);

static Bool cc_creatergbtocmykinfo( GS_RGBtoCMYKinfo **rgbtocmykInfo ) ;

#if defined( ASSERT_BUILD )
static void rgbtocmykInfoAssertions(GS_RGBtoCMYKinfo *pInfo) ;
#else
#define rgbtocmykInfoAssertions(pInfo) EMPTY_STATEMENT()
#endif

/*============================================================================
 *
 * customconversion common creator.
 */

static size_t customconversionStructSize( void )
{
  return sizeof(CLINKcustomconversion);
}

static Bool customconversion_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;
  int32 indims ;
  int32 ondims ;
  int32 oldStackSize;
  OBJECT cv = OBJECT_NOTVM_NOTHING;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues is NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "custom conversion input");

  indims = pLink->n_iColorants ;
  ondims = pLink->p.customconversion->n_oColorants ;

  if ( pLink->p.customconversion->pscalc_func ) {

    if ( pscalc_exec(pLink->p.customconversion->pscalc_func, indims, ondims,
        pLink->iColorValues, oColorValues) != PSCALC_noerr )
      return FALSE;
  } else {
    for ( i = 0 ; i < indims ; ++i ) {
      object_store_real( &cv, pLink->iColorValues[ i ] ) ;
      if ( ! push( &cv , &operandstack ))
        return FALSE ;
    }

    oldStackSize = theStackSize(operandstack);

    /* @@JJ PROTECT against gsave/grestore abuse */
    if ( !push(&pLink->p.customconversion->customprocedure, &executionstack) )
      return FALSE;
    if ( ! interpreter( 1 , NULL ))
      return FALSE;

    if ( theStackSize( operandstack ) < ondims - 1 )
      return error_handler( STACKUNDERFLOW ) ;

    HQASSERT(theStackSize(operandstack) == oldStackSize + ondims - indims,
             "Unexpected stack size after running custom conversions");

    for ( i = 0 ; i < ondims ; ++i ) {
      USERVALUE colorValue ;
      OBJECT *theo ;

      theo = stackindex( i , & operandstack ) ;
      if ( !object_get_real(theo, &colorValue) )
        return FALSE ;

      NARROW_01( colorValue ) ;
      oColorValues[ ( ondims - 1 ) - i ] = colorValue ;
    }
    npop ( ondims , & operandstack ) ;
  }
  /* Clear the cmyk overprintProcess flags */
  pLink->overprintProcess &= ~OP_CMYK_OVERPRINT_MASK ;

  return TRUE ;
}

/*============================================================================
 *
 * cmyktogray link access functions.
 */

struct CLINKCMYKtoGrayinfo {
  Bool    fCompositing;
};

static Bool cmyktogray_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool cmyktogray_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKcmyktogray_functions =
{
  cc_common_destroy,
  cmyktogray_invokeSingle,
  NULL /* cmyktogray_invokeBlock */,
  NULL
};

CLINK *cc_cmyktogray_create(Bool fCompositing)
{
  CLINK *pLink;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_DeviceCMYK,
                           SPACE_DeviceGray,
                           CL_TYPEcmyktogray,
                           sizeof(CLINKCMYKtoGrayinfo),
                           &CLINKcmyktogray_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEcmyktogray,
                         sizeof(CLINKCMYKtoGrayinfo),
                         &CLINKcmyktogray_functions );

  pLink->p.cmyktogray = (CLINKCMYKtoGrayinfo *)
                        ((uint8 *)pLink + cc_commonStructSize(pLink)) ;
  pLink->p.cmyktogray->fCompositing = fCompositing;

  return pLink;
}

static Bool cmyktogray_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;
  USERVALUE c = pLink->iColorValues[ 0 ] ;
  USERVALUE m = pLink->iColorValues[ 1 ] ;
  USERVALUE y = pLink->iColorValues[ 2 ] ;
  USERVALUE k = pLink->iColorValues[ 3 ] ;
  USERVALUE t ;
  Bool fCompositing = pLink->p.cmyktogray->fCompositing;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "cmyktogray input");


  /* Don't do separation detection at the back end, and only for output colors */
  if (!fCompositing && new_color_detected && ( c != m || m != y || y != 0.0 )) {
    if (!detect_setcolor_separation())
      return FALSE;
  }

  t = 0.30f * c +
      0.59f * m +
      0.11f * y +
              k ;
  t = 1.0f - t ;
  /* In case of rounding (overflow) errors. */
  if ( t < 0.0f )
    t = 0.0f ;
  oColorValues[ 0 ] = t ;

  /* Clear the cmyk overprintProcess flags */
  pLink->overprintProcess &= ~OP_CMYK_OVERPRINT_MASK ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cmyktogray_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * cmykton link access functions.
 */

static void customconversion_destroy(CLINK *pLink)
{
  if ( pLink->p.customconversion->pscalc_func ) {
    pscalc_destroy(pLink->p.customconversion->pscalc_func);
    pLink->p.customconversion->pscalc_func = NULL;
  }
  cc_common_destroy(pLink);
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_cmykton_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKcmykton_functions =
{
  customconversion_destroy,
  customconversion_invokeSingle,
  NULL /* cc_cmykton_invokeBlock */,
  NULL
};

CLINK *cc_cmykton_create( OBJECT customProcedure,
                          int32 n_oColorants )
{
  CLINK *pLink;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_DeviceCMYK,
                           SPACE_DeviceN,
                           CL_TYPEcmykton,
                           customconversionStructSize(),
                           &CLINKcmykton_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL) {
    pLink->p.customconversion = (CLINKcustomconversion *)((uint8 *)pLink +
        cc_commonStructSize(pLink));
    Copy(object_slot_notvm(&pLink->p.customconversion->customprocedure),
         &customProcedure) ;
    pLink->p.customconversion->pscalc_func = pscalc_create(&customProcedure);
    pLink->p.customconversion->n_oColorants = n_oColorants ;
    cc_commonAssertions( pLink,
                         CL_TYPEcmykton,
                         customconversionStructSize(),
                         &CLINKcmykton_functions );
  }

  return pLink;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_cmykton_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * cmyktorgb link access functions.
 */

static Bool cmyktorgb_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool cmyktorgb_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKcmyktorgb_functions =
{
  cc_common_destroy,
  cmyktorgb_invokeSingle,
  NULL /* cmyktorgb_invokeBlock */,
  NULL
};

CLINK *cc_cmyktorgb_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_DeviceCMYK,
                           SPACE_DeviceRGB,
                           CL_TYPEcmyktorgb,
                           0,
                           &CLINKcmyktorgb_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEcmyktorgb,
                         0,
                         &CLINKcmyktorgb_functions );

  return pLink;
}

static Bool cmyktorgb_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;
  USERVALUE k = pLink->iColorValues[ 3 ] ;
  USERVALUE c = pLink->iColorValues[ 0 ] + k ;
  USERVALUE m = pLink->iColorValues[ 1 ] + k ;
  USERVALUE y = pLink->iColorValues[ 2 ] + k ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "cmyktorgb input");

  oColorValues[ 0 ] = 1.0f - min( 1.0f , c ) ;
  oColorValues[ 1 ] = 1.0f - min( 1.0f , m ) ;
  oColorValues[ 2 ] = 1.0f - min( 1.0f , y ) ;

  /* Clear the cmyk overprintProcess flags */
  pLink->overprintProcess &= ~OP_CMYK_OVERPRINT_MASK ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cmyktorgb_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * cmyktorgbk link access functions.
 */

static Bool cmyktorgbk_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool cmyktorgbk_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKcmyktorgbk_functions =
{
  cc_common_destroy,
  cmyktorgbk_invokeSingle,
  NULL /* cmyktorgbk_invokeBlock */,
  NULL
};

CLINK *cc_cmyktorgbk_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_DeviceCMYK,
                           SPACE_DeviceRGBK,
                           CL_TYPEcmyktorgbk,
                           0,
                           &CLINKcmyktorgbk_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions(pLink,
                        CL_TYPEcmyktorgbk,
                        0,
                        &CLINKcmyktorgbk_functions);

  return pLink;
}

static Bool cmyktorgbk_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "cmyktorgbk input");

  oColorValues[ 0 ] = 1.0f - pLink->iColorValues[ 0 ] ;
  oColorValues[ 1 ] = 1.0f - pLink->iColorValues[ 1 ] ;
  oColorValues[ 2 ] = 1.0f - pLink->iColorValues[ 2 ] ;
  oColorValues[ 3 ] = 1.0f - pLink->iColorValues[ 3 ] ;

  /* Clear the cmyk overprintProcess flags */
  pLink->overprintProcess &= ~OP_CMYK_OVERPRINT_MASK ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cmyktorgbk_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * cmyktocmy link access functions.
 */

static Bool cmyktocmy_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool cmyktocmy_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKcmyktocmy_functions =
{
  cc_common_destroy,
  cmyktocmy_invokeSingle,
  NULL /* cmyktocmy_invokeBlock */,
  NULL
};

CLINK *cc_cmyktocmy_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_DeviceCMYK,
                           SPACE_DeviceCMY,
                           CL_TYPEcmyktocmy,
                           0,
                           &CLINKcmyktocmy_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEcmyktocmy,
                         0,
                         &CLINKcmyktocmy_functions);

  return pLink;
}

static Bool cmyktocmy_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;
  USERVALUE k = pLink->iColorValues[ 3 ] ;
  USERVALUE c = pLink->iColorValues[ 0 ] + k ;
  USERVALUE m = pLink->iColorValues[ 1 ] + k ;
  USERVALUE y = pLink->iColorValues[ 2 ] + k ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "cmyktocmy input");

  oColorValues[ 0 ] = min( 1.0f , c ) ;
  oColorValues[ 1 ] = min( 1.0f , m ) ;
  oColorValues[ 2 ] = min( 1.0f , y ) ;

  /* Clear the cmyk overprintProcess flags */
  pLink->overprintProcess &= ~OP_CMYK_OVERPRINT_MASK ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cmyktocmy_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * cmyktolab link access functions.
 */

static Bool cmyktolab_invokeSingle( CLINK *pLink, USERVALUE *oColorValues );
#ifdef INVOKEBLOCK_NYI
static Bool cmyktolab_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKcmyktolab_functions =
{
  cc_common_destroy,
  cmyktolab_invokeSingle,
  NULL /* cmyktolab_invokeBlock */,
  NULL
};

CLINK *cc_cmyktolab_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(4,
                           NULL,
                           SPACE_DeviceCMYK,
                           SPACE_Lab,
                           CL_TYPEcmyktolab,
                           0,
                           &CLINKcmyktolab_functions,
                           COLCACHE_NYI);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEcmyktolab,
                         0,
                         &CLINKcmyktolab_functions);

  return pLink;
}

static Bool cmyktolab_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;

  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( USERVALUE *, oColorValues ) ;

  HQFAIL( "cmyktolab not yet supported" ) ;

  /* Need to add CLID calculation support if this is implemented */

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "cmyktolab input");

  /* Clear the cmyk overprintProcess flags */
  pLink->overprintProcess &= ~OP_CMYK_OVERPRINT_MASK ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cmyktolab_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * rgbtocmyk link access functions.
 */

static Bool rgbtocmyk_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool rgbtocmyk_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t cc_rgbtocmyk_scan( mps_ss_t ss, CLINK *pLink );

static CLINKfunctions CLINKrgbtocmyk_functions =
{
  cc_common_destroy,
  rgbtocmyk_invokeSingle,
  NULL /* rgbtocmyk_invokeBlock */,
  cc_rgbtocmyk_scan
};

CLINK *cc_rgbtocmyk_create(GS_RGBtoCMYKinfo   *rgbtocmykInfo,
                           Bool               preserveBlack,
                           COLOR_PAGE_PARAMS  *colorPageParams)
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceRGB,
                           SPACE_DeviceCMYK,
                           CL_TYPErgbtocmyk,
                           rgbtocmykStructSize(),
                           &CLINKrgbtocmyk_functions,
                           CLID_SIZErgbtocmyk);

  if (pLink != NULL) {
    CLID *idslot = pLink->idslot ;

    pLink->p.rgbtocmyk = (CLINKRGBtoCMYKinfo *)((uint8 *)pLink +
                                                cc_commonStructSize(pLink));
    pLink->p.rgbtocmyk->rgbtocmykInfo = rgbtocmykInfo ;
    pLink->p.rgbtocmyk->preserveBlack = preserveBlack ;

    /* Populate CLID slots:
       0 for TableBasedColor sysparam & convertRGBBlack
       1 for uid of ucr proc
       2 for uid of bg proc
       NOTE: The code now ensures the uniqueID fields are
       set to zero if the procedure is empty.
       */

    idslot[0] = (CLID) ((colorPageParams->tableBasedColor ? 0x1 : 0x0) |
                        (preserveBlack                    ? 0x2 : 0x0));
    idslot[1] = (CLID)id_callpscache(rgbtocmykInfo->ucr.cpsc);
    idslot[2] = (CLID)id_callpscache(rgbtocmykInfo->bg.cpsc);

    cc_commonAssertions( pLink,
                         CL_TYPErgbtocmyk,
                         rgbtocmykStructSize(),
                         &CLINKrgbtocmyk_functions );
  }

  return pLink;
}

static Bool rgbtocmyk_invokeSingle( CLINK *pLink , USERVALUE *oColorValues )
{
  /* Algorithm as per red book 2 page 305: */

  int32 i ;
  USERVALUE c , m , y , k ;
  CLINKRGBtoCMYKinfo *rgbtocmyk ;
  GS_RGBtoCMYKinfo *rgbtocmykInfo ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "rgbtocmyk input");

  c = 1.0f - pLink->iColorValues[ 0 ] ;
  m = 1.0f - pLink->iColorValues[ 1 ] ;
  y = 1.0f - pLink->iColorValues[ 2 ] ;

  /* k for now, is the smallest of these */
  k = c ;
  if ( k > m )
    k = m;
  if ( k > y )
    k = y ;

  rgbtocmyk = pLink->p.rgbtocmyk ;
  HQASSERT(rgbtocmyk != NULL, "somehow didn't set up rgbtocmykInfo");
  rgbtocmykInfo = rgbtocmyk->rgbtocmykInfo ;
  HQASSERT(rgbtocmykInfo!= NULL, "somehow didn't set up rgbtocmykInfo");

  /* Optimise case for empty UCR and BG procedures.
   * If we are converting RGB Black to CMYK Black then treat as UCRBG_MAX.
   * NB. If k == 1.0 then we must have an RGB Black.
   */
  if ((id_callpscache(rgbtocmykInfo->ucr.cpsc) == UCRBG_MAX &&
       id_callpscache(rgbtocmykInfo->bg.cpsc) == UCRBG_MAX) ||
      (k == 1.0f && rgbtocmyk->preserveBlack)) {
    oColorValues[ 0 ] = c - k ;
    oColorValues[ 1 ] = m - k ;
    oColorValues[ 2 ] = y - k ;
    oColorValues[ 3 ] = k ;
  }
  /* Optimise case for {pop 0} UCR and BG procedures. */
  else if (id_callpscache(rgbtocmykInfo->ucr.cpsc) == UCRBG_MIN &&
           id_callpscache(rgbtocmykInfo->bg.cpsc) == UCRBG_MIN) {
    oColorValues[ 0 ] = c ;
    oColorValues[ 1 ] = m;
    oColorValues[ 2 ] = y ;
    oColorValues[ 3 ] = 0 ;
  }
  else {
    int32 result  = TRUE;
    USERVALUE dUCR = 0.0;
    USERVALUE dBG = 0.0 ;

    cc_reservergbtocmykinfo(rgbtocmykInfo);

    lookup_callpscache(rgbtocmykInfo->ucr.cpsc, k, &dUCR);
    lookup_callpscache(rgbtocmykInfo->bg.cpsc, k, &dBG);

    cc_destroyrgbtocmykinfo(&rgbtocmykInfo);

    if ( !result )
      return FALSE;

    if ( dUCR != 0.0f ) {
      c = c - dUCR ; NARROW_01( c ) ;
      m = m - dUCR ; NARROW_01( m ) ;
      y = y - dUCR ; NARROW_01( y ) ;
    }
    oColorValues[ 0 ] = c ;
    oColorValues[ 1 ] = m ;
    oColorValues[ 2 ] = y ;
    oColorValues[ 3 ] = dBG ;
  }
  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool rgbtocmyk_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */


/* cc_rgbtocmyk_scan - scan the rgbtocmyk section of a CLINK */
static mps_res_t cc_rgbtocmyk_scan( mps_ss_t ss, CLINK *pLink )
{
  return cc_scan_rgbtocmyk( ss, pLink->p.rgbtocmyk->rgbtocmykInfo );
}

static size_t rgbtocmykStructSize(void)
{
  return sizeof(CLINKRGBtoCMYKinfo);
}

Bool cc_rgbtocmykiscomplex( CLINK *pLink )
{
  GS_RGBtoCMYKinfo *rgbtocmykInfo ;

  cc_commonAssertions( pLink,
                       CL_TYPErgbtocmyk,
                       rgbtocmykStructSize(),
                       &CLINKrgbtocmyk_functions );

  rgbtocmykInfo = pLink->p.rgbtocmyk->rgbtocmykInfo ;
  HQASSERT(rgbtocmykInfo != NULL , "Somehow lost CLINKRGBtoCMYKinfo");

  switch ( id_callpscache(rgbtocmykInfo->ucr.cpsc)) {
  case UCRBG_MAX:
  case UCRBG_MIN:
    break;
  default:
    return TRUE;
  }
  switch ( id_callpscache(rgbtocmykInfo->bg.cpsc)) {
  case UCRBG_MAX:
  case UCRBG_MIN:
    break;
  default:
    return TRUE;
  }

  return FALSE;
}

/*============================================================================
 *
 * rgbtogray link access functions.
 */

struct CLINKRGBtoGrayinfo {
  Bool    fCompositing;
};

static Bool rgbtogray_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool rgbtogray_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKrgbtogray_functions =
{
  cc_common_destroy,
  rgbtogray_invokeSingle,
  NULL /* rgbtogray_invokeBlock */,
  NULL
};

CLINK *cc_rgbtogray_create(Bool fCompositing)
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceRGB,
                           SPACE_DeviceGray,
                           CL_TYPErgbtogray,
                           sizeof(CLINKRGBtoGrayinfo),
                           &CLINKrgbtogray_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPErgbtogray,
                         sizeof(CLINKRGBtoGrayinfo),
                         &CLINKrgbtogray_functions );

  pLink->p.rgbtogray = (CLINKRGBtoGrayinfo *)
                       ((uint8 *)pLink + cc_commonStructSize(pLink)) ;
  pLink->p.rgbtogray->fCompositing = fCompositing;

  return pLink;
}

static Bool rgbtogray_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;
  USERVALUE r = pLink->iColorValues[ 0 ] ;
  USERVALUE g = pLink->iColorValues[ 1 ] ;
  USERVALUE b = pLink->iColorValues[ 2 ] ;
  USERVALUE t ;
  Bool fCompositing = pLink->p.rgbtogray->fCompositing;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "rgbtogray input");

  /* Don't do separation detection at the back end, and only for output colors */
  if (!fCompositing && new_color_detected && ( r != g || g != b )) {
    if (!detect_setcolor_separation())
      return FALSE;
  }

  t = 0.30f * r +
      0.59f * g +
      0.11f * b ;
  oColorValues[ 0 ] = t ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool rgbtogray_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * rgbton link access functions.
 */

#ifdef INVOKEBLOCK_NYI
static Bool cc_rgbton_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKrgbton_functions =
{
  customconversion_destroy,
  customconversion_invokeSingle,
  NULL /* cc_rgbton_invokeBlock */,
  NULL
};

CLINK *cc_rgbton_create( OBJECT customProcedure,
                         int32 n_oColorants )
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceRGB,
                           SPACE_DeviceN,
                           CL_TYPErgbton,
                           customconversionStructSize(),
                           &CLINKrgbton_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL) {
    pLink->p.customconversion = (CLINKcustomconversion *)((uint8 *)pLink +
        cc_commonStructSize(pLink));
    Copy(object_slot_notvm(&pLink->p.customconversion->customprocedure),
         &customProcedure) ;
    pLink->p.customconversion->pscalc_func = pscalc_create(&customProcedure);
    pLink->p.customconversion->n_oColorants = n_oColorants ;
    cc_commonAssertions( pLink,
                         CL_TYPErgbton,
                         customconversionStructSize(),
                         &CLINKrgbton_functions );
  }

  return pLink;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_rgbton_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * rgbtorgbk link access functions.
 */

static Bool rgbtorgbk_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool rgbtorgbk_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKrgbtorgbk_functions =
{
  cc_common_destroy,
  rgbtorgbk_invokeSingle,
  NULL /* rgbtorgbk_invokeBlock */,
  NULL
};

CLINK *cc_rgbtorgbk_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceRGB,
                           SPACE_DeviceRGBK,
                           CL_TYPErgbtorgbk,
                           0,
                           &CLINKrgbtorgbk_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPErgbtorgbk,
                         0,
                         &CLINKrgbtorgbk_functions );

  return pLink;
}

static Bool rgbtorgbk_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "rgbtorgbk input");

  oColorValues[ 0 ] = pLink->iColorValues[ 0 ] ;
  oColorValues[ 1 ] = pLink->iColorValues[ 1 ] ;
  oColorValues[ 2 ] = pLink->iColorValues[ 2 ] ;
  oColorValues[ 3 ] = 1.0f ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool rgbtorgbk_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * rgbtocmy link access functions.
 */

static Bool rgborcmy_invert_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool rgbtocmy_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKrgborcmy_invert_functions =
{
  cc_common_destroy,
  rgborcmy_invert_invokeSingle,
  NULL /* rgborcmy_invert_invokeBlock */,
  NULL
};

CLINK *cc_rgbtocmy_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceRGB,
                           SPACE_DeviceCMY,
                           CL_TYPErgborcmy_invert,
                           0,
                           &CLINKrgborcmy_invert_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPErgborcmy_invert,
                         0,
                         &CLINKrgborcmy_invert_functions );

  return pLink;
}

static Bool rgborcmy_invert_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "rgborcmy_invert input");

  oColorValues[ 0 ] = 1.0f - pLink->iColorValues[ 0 ] ;
  oColorValues[ 1 ] = 1.0f - pLink->iColorValues[ 1 ] ;
  oColorValues[ 2 ] = 1.0f - pLink->iColorValues[ 2 ] ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool rgborcmy_invert_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * rgbtolab link access functions.
 */

static Bool rgbtolab_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool rgbtolab_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKrgbtolab_functions =
{
  cc_common_destroy,
  rgbtolab_invokeSingle,
  NULL /* rgbtolab_invokeBlock */,
  NULL
};

CLINK *cc_rgbtolab_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceRGB,
                           SPACE_Lab,
                           CL_TYPErgbtolab,
                           0,
                           &CLINKrgbtolab_functions,
                           COLCACHE_NYI);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPErgbtolab,
                         0,
                         &CLINKrgbtolab_functions );

  return pLink;
}

static Bool rgbtolab_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  int32 i ;

  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( USERVALUE *, oColorValues ) ;

  HQFAIL( "rgbtolab not yet supported" ) ;

  /* Need to add CLID calculation support if this is implemented */

  for ( i = 0 ; i < pLink->n_iColorants ; i++ )
    COLOR_01_ASSERT(pLink->iColorValues[ i ], "rgbtolab input");

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool rgbtolab_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * cmytorgb link access functions, (see also rgbtocmy link access functions).
 */
CLINK *cc_cmytorgb_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(3,
                           NULL,
                           SPACE_DeviceCMY,
                           SPACE_DeviceRGB,
                           CL_TYPErgborcmy_invert,
                           0,
                           &CLINKrgborcmy_invert_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPErgborcmy_invert,
                         0,
                         &CLINKrgborcmy_invert_functions );

  return pLink;
}

/*============================================================================
 *
 * graytocmyk link access functions.
 */

static Bool graytocmyk_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool graytocmyk_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKgraytocmyk_functions =
{
  cc_common_destroy,
  graytocmyk_invokeSingle,
  NULL /* graytocmyk_invokeBlock */,
  NULL
};

CLINK *cc_graytocmyk_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_DeviceGray,
                           SPACE_DeviceCMYK,
                           CL_TYPEgraytocmyk,
                           0,
                           &CLINKgraytocmyk_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEgraytocmyk,
                         0,
                         &CLINKgraytocmyk_functions );

  return pLink;
}

static Bool graytocmyk_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  COLOR_01_ASSERT(pLink->iColorValues[ 0 ], "graytocmyk input");

  oColorValues[ 0 ] = 0.0f ;
  oColorValues[ 1 ] = 0.0f ;
  oColorValues[ 2 ] = 0.0f ;
  oColorValues[ 3 ] = 1.0f - pLink->iColorValues[ 0 ] ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool graytocmyk_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * graytorgb link access functions.
 */

static Bool graytorgb_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool graytorgb_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKgraytorgb_functions =
{
  cc_common_destroy,
  graytorgb_invokeSingle,
  NULL /* graytorgb_invokeBlock */,
  NULL
};

CLINK *cc_graytorgb_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_DeviceGray,
                           SPACE_DeviceRGB,
                           CL_TYPEgraytorgb,
                           0,
                           &CLINKgraytorgb_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEgraytorgb,
                         0,
                         &CLINKgraytorgb_functions );

  return pLink;
}

static Bool graytorgb_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  USERVALUE g = pLink->iColorValues[ 0 ] ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  COLOR_01_ASSERT(pLink->iColorValues[ 0 ], "graytorgb input");

  oColorValues[ 0 ] = g ;
  oColorValues[ 1 ] = g ;
  oColorValues[ 2 ] = g ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool graytorgb_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * grayton link access functions.
 */

#ifdef INVOKEBLOCK_NYI
static Bool cc_grayton_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKgrayton_functions =
{
  customconversion_destroy,
  customconversion_invokeSingle,
  NULL /* cc_grayton_invokeBlock */,
  NULL
};

CLINK *cc_grayton_create( OBJECT customProcedure,
                          int32 n_oColorants )
{
  CLINK *pLink;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_DeviceGray,
                           SPACE_DeviceN,
                           CL_TYPEgrayton,
                           customconversionStructSize(),
                           &CLINKgrayton_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL) {
    pLink->p.customconversion = (CLINKcustomconversion *)((uint8 *)pLink +
        cc_commonStructSize(pLink));
    Copy(object_slot_notvm(&pLink->p.customconversion->customprocedure),
         &customProcedure) ;
    pLink->p.customconversion->pscalc_func = pscalc_create(&customProcedure);
    pLink->p.customconversion->n_oColorants = n_oColorants ;
    cc_commonAssertions( pLink,
                         CL_TYPEgrayton,
                         customconversionStructSize(),
                         &CLINKgrayton_functions );
  }

  return pLink;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_grayton_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * graytok link access functions.
 */

static Bool graytok_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool graytok_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKgraytok_functions =
{
  cc_common_destroy,
  graytok_invokeSingle,
  NULL /* graytok_invokeBlock */,
  NULL
};

CLINK *cc_graytok_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_DeviceGray,
                           SPACE_DeviceK,
                           CL_TYPEgraytok,
                           0,
                           &CLINKgraytok_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEgraytok,
                         0,
                         &CLINKgraytok_functions );

  return pLink;
}

static Bool graytok_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  COLOR_01_ASSERT(pLink->iColorValues[ 0 ], "graytok input");

  oColorValues[ 0 ] = 1.0f - pLink->iColorValues[ 0 ] ;

  return TRUE ;
}


#ifdef INVOKEBLOCK_NYI
static Bool graytok_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * graytocmy link access functions.
 */

static Bool graytocmy_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool graytocmy_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKgraytocmy_functions =
{
  cc_common_destroy,
  graytocmy_invokeSingle,
  NULL /* graytocmy_invokeBlock */,
  NULL
};

CLINK *cc_graytocmy_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_DeviceGray,
                           SPACE_DeviceCMY,
                           CL_TYPEgraytocmy,
                           0,
                           &CLINKgraytocmy_functions,
                           CLID_SIZEnoslotsneeded);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEgraytocmy,
                         0,
                         &CLINKgraytocmy_functions );

  return pLink;
}

static Bool graytocmy_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  USERVALUE g = pLink->iColorValues[ 0 ] ;
  USERVALUE t = 1.0f - g ;

  HQASSERT(pLink, "pLink NULL");
  HQASSERT(oColorValues, "oColorValues NULL");

  COLOR_01_ASSERT(pLink->iColorValues[ 0 ], "graytocmy input");

  oColorValues[ 0 ] = t ;
  oColorValues[ 1 ] = t ;
  oColorValues[ 2 ] = t ;

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool graytocmy_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/*============================================================================
 *
 * graytolab link access functions.
 */

static Bool graytolab_invokeSingle( CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool graytolab_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */

static CLINKfunctions CLINKgraytolab_functions =
{
  cc_common_destroy,
  graytolab_invokeSingle,
  NULL /* graytolab_invokeBlock */,
  NULL
};

CLINK *cc_graytolab_create( void )
{
  CLINK *pLink;

  pLink = cc_common_create(1,
                           NULL,
                           SPACE_DeviceGray,
                           SPACE_Lab,
                           CL_TYPEgraytolab,
                           0,
                           &CLINKgraytolab_functions,
                           COLCACHE_NYI);

  if (pLink != NULL)
    cc_commonAssertions( pLink,
                         CL_TYPEgraytolab,
                         0,
                         &CLINKgraytolab_functions );

  return pLink;
}

static Bool graytolab_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( USERVALUE *, oColorValues ) ;

  HQFAIL( "graytolab not yet supported" ) ;

  /* Need to add CLID calculation support if this is implemented */

  COLOR_01_ASSERT(pLink->iColorValues[ 0 ], "graytolab input");

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool graytolab_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK *, pLink ) ;
  UNUSED_PARAM( CLINKblock *, pBlock );
  HQFAIL( "Unsupported color transform link function" );
  return TRUE ;
}
#endif /* INVOKEBLOCK_NYI */

/* ---------------------------------------------------------------------- */

/*
 * rgbtocmyk Info Data Access Functions
 * ================================
 */

static Bool cc_creatergbtocmykinfo( GS_RGBtoCMYKinfo **rgbtocmykInfo )
{
  GS_RGBtoCMYKinfo  *pInfo ;
  size_t            structSize ;

  structSize = sizeof( GS_RGBtoCMYKinfo ) ;

  pInfo = ( GS_RGBtoCMYKinfo * ) mm_sac_alloc( mm_pool_color ,
                                               structSize ,
                                               MM_ALLOC_CLASS_NCOLOR ) ;
  *rgbtocmykInfo = pInfo ;
  if ( pInfo == NULL )
    return error_handler( VMERROR ) ;

  pInfo->refCnt = 1 ;
  pInfo->structSize = structSize ;

  pInfo->ucr.psObject = onull ; /* set slot properties */
  pInfo->bg.psObject = onull ; /* set slot properties */

  pInfo->ucr.cpsc = NULL;
  pInfo->bg .cpsc = NULL;

  rgbtocmykInfoAssertions( pInfo ) ;

  return TRUE ;
}

static Bool copyrgbtocmykinfo( GS_RGBtoCMYKinfo *rgbtocmykInfo,
                               GS_RGBtoCMYKinfo **rgbtocmykInfoCopy )
{
  GS_RGBtoCMYKinfo *pInfoCopy;

  pInfoCopy = mm_sac_alloc(mm_pool_color,
                           rgbtocmykInfo->structSize,
                           MM_ALLOC_CLASS_NCOLOR);
  if ( pInfoCopy == NULL )
    return error_handler(VMERROR);

  *rgbtocmykInfoCopy = pInfoCopy;
  HqMemCpy(pInfoCopy, rgbtocmykInfo, rgbtocmykInfo->structSize);
  pInfoCopy->refCnt = 1;

  /* Now have another structure pointing to both of these. */
  reserve_callpscache(pInfoCopy->ucr.cpsc);
  reserve_callpscache(pInfoCopy->bg .cpsc);
  return TRUE;
}

static void freergbtocmykinfo( GS_RGBtoCMYKinfo *rgbtocmykInfo )
{
  if ( rgbtocmykInfo->ucr.cpsc )
    destroy_callpscache(&rgbtocmykInfo->ucr.cpsc);
  if ( rgbtocmykInfo->bg .cpsc )
    destroy_callpscache(&rgbtocmykInfo->bg.cpsc);

  mm_sac_free( mm_pool_color, rgbtocmykInfo, rgbtocmykInfo->structSize ) ;
}

static Bool updatergbtocmykinfo( GS_RGBtoCMYKinfo **rgbtocmykInfo )
{
  CLINK_UPDATE(GS_RGBtoCMYKinfo, rgbtocmykInfo,
               copyrgbtocmykinfo, freergbtocmykinfo);
  return TRUE;
}

void cc_destroyrgbtocmykinfo( GS_RGBtoCMYKinfo **rgbtocmykInfo )
{
  if ( *rgbtocmykInfo != NULL ) {
    rgbtocmykInfoAssertions(*rgbtocmykInfo);
    CLINK_RELEASE(rgbtocmykInfo, freergbtocmykinfo);
  }
}

void cc_reservergbtocmykinfo( GS_RGBtoCMYKinfo *rgbtocmykInfo )
{
  if ( rgbtocmykInfo != NULL ) {
    rgbtocmykInfoAssertions( rgbtocmykInfo ) ;
    CLINK_RESERVE( rgbtocmykInfo ) ;
  }
}

Bool cc_arergbtocmykobjectslocal(corecontext_t *corecontext,
                                 GS_RGBtoCMYKinfo *rgbtocmykInfo )
{
  if ( rgbtocmykInfo == NULL )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&rgbtocmykInfo->ucr.psObject, corecontext) )
    return TRUE ;
  if ( illegalLocalIntoGlobal(&rgbtocmykInfo->bg.psObject, corecontext) )
    return TRUE ;

  return FALSE ;
}

/* cc_scan_rgbtocmyk - scan RGBtoCMYKinfo
 *
 * This should match cc_arergbtocmykobjectslocal, since both need look at
 * all the VM pointers. */
mps_res_t cc_scan_rgbtocmyk( mps_ss_t ss, GS_RGBtoCMYKinfo *rgbtocmykInfo )
{
  mps_res_t res;

  if ( rgbtocmykInfo == NULL )
    return MPS_RES_OK;

  res = ps_scan_field( ss, &rgbtocmykInfo->ucr.psObject );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &rgbtocmykInfo->bg.psObject );
  return res;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the rgbtocmyk info access functions.
 */
static void rgbtocmykInfoAssertions(GS_RGBtoCMYKinfo *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == sizeof(GS_RGBtoCMYKinfo),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

OBJECT *gsc_getblackgenerationobject( GS_COLORinfo *colorInfo )
{
  HQASSERT(colorInfo, "colorInfo is NULL");
  HQASSERT(colorInfo->rgbtocmykInfo, "rgbtocmykInfo is NULL");
  return &colorInfo->rgbtocmykInfo->bg.psObject ;
}

int32 gsc_getblackgenerationid( GS_COLORinfo *colorInfo )
{
  HQASSERT(colorInfo, "colorInfo is NULL");
  HQASSERT(colorInfo->rgbtocmykInfo, "rgbtocmykInfo is NULL");
  return id_callpscache(colorInfo->rgbtocmykInfo->bg.cpsc);
}

OBJECT *gsc_getundercolorremovalobject( GS_COLORinfo *colorInfo )
{
  HQASSERT(colorInfo, "colorInfo is NULL");
  HQASSERT(colorInfo->rgbtocmykInfo, "rgbtocmykInfo is NULL");
  return & colorInfo->rgbtocmykInfo->ucr.psObject ;
}

int32 gsc_getundercolorremovalid( GS_COLORinfo *colorInfo )
{
  HQASSERT(colorInfo, "colorInfo is NULL");
  HQASSERT(colorInfo->rgbtocmykInfo, "rgbtocmykInfo is NULL");
  return id_callpscache(colorInfo->rgbtocmykInfo->ucr.cpsc);
}

static Bool get_ucrbg_proc(STACK              *stack,
                           COLOR_PAGE_PARAMS  *colorPageParams,
                           OBJECT             **pobj,
                           int32              *uniqueID)
{
  OBJECT *orig_obj = *pobj, *theo;
  FILELIST *flptr;

  *uniqueID = UCRBG_NOOP; /* Default to doing nothing */

  if ( DEVICE_INVALID_CONTEXT() )
    return error_handler(UNDEFINED);

  if ( isEmpty(*stack) )
    return error_handler(STACKUNDERFLOW);

  theo = theTop(*stack);
  switch ( oType(*theo) ) {
    case ODICTIONARY:
      break;
    case OFILE:
      {
        flptr = oFile(*theo);
        if ( !isIInputFile(flptr) || !isIOpenFileFilter(theo, flptr) ||
             !isIRewindable(flptr) )
          return error_handler(IOERROR);
      }
      /* Drop through. */
    case OARRAY:
    case OPACKEDARRAY:
      if ( ! oExecutable(*theo))
        return error_handler(TYPECHECK);
      if ( !oCanExec(*theo) && !object_access_override(theo) )
        return error_handler(INVALIDACCESS);
      break;
    default:
      return error_handler(TYPECHECK);
  }

  if ( colorPageParams->ignoreSetBlackGeneration )
    return TRUE;
  if ( OBJECTS_IDENTICAL(*orig_obj,*theo))
    return TRUE;

  /* Create with uniqueID of UCRBG_MAX if we have an empty proc.
   * Create with uniqueID of UCRBG_MIN if we have a proc of {pop 0} as often
   * used by default in gui rips. Else just normal default processing.
   */
  *uniqueID = UCRBG_INITIAL;
  if ( oType(*theo) != OFILE && theLen(*theo) == 0 )
    *uniqueID = UCRBG_MAX;
  else if (oType(*theo) == OARRAY && theLen(*theo) == 2 ) {
    OBJECT *olist = oArray(*theo);
    if ( oType(olist[0]) == OOPERATOR &&
           theINameNumber(theIOpName(oOp(olist[0]))) == NAME_pop &&
           oType(olist[1]) == OINTEGER && oInteger(olist[1]) == 0 )
      *uniqueID = UCRBG_MIN;
  }
  *pobj = theo;
  return TRUE;
}

static Bool do_setbgucr(GS_COLORinfo      *colorInfo,
                        STACK             *stack,
                        int32             fnType,
                        COLOR_PAGE_PARAMS *colorPageParams)
{
  GS_RGBtoCMYKinfo *rgbtocmykInfo = colorInfo->rgbtocmykInfo;
  static int32 iBlackGenerationId = UCRBG_INITIAL;
  static int32 iUnderColorRemovalId = UCRBG_INITIAL;
  CALLPSCACHE *gscnew;
  OBJECT *psobj;
  int32 uniqueID;
  GS_PROC *gsp;

  HQASSERT(stack, "stack NULL");

  if ( rgbtocmykInfo == NULL ) {
    if ( !cc_creatergbtocmykinfo(&colorInfo->rgbtocmykInfo) )
      return FALSE;
    rgbtocmykInfo = colorInfo->rgbtocmykInfo;
  }
  gsp = (fnType == FN_BLACK_GEN) ? &rgbtocmykInfo->bg : &rgbtocmykInfo->ucr;
  psobj = &gsp->psObject;

  if ( !get_ucrbg_proc(stack, colorPageParams, &psobj, &uniqueID) )
    return FALSE;

  if ( uniqueID == UCRBG_NOOP ) {
    pop(stack);
    return TRUE;
  }
  else if ( uniqueID == UCRBG_INITIAL ) {
    if ( fnType == FN_BLACK_GEN )
      uniqueID = ++iBlackGenerationId;
    else /* fnType == FN_UNDER_COLOR_REM */
      uniqueID = ++iUnderColorRemovalId;
  }
  if ( (gscnew = create_callpscache(fnType, 1, uniqueID, NULL, psobj)) == NULL )
    return FALSE;
  pop(stack);

  /* Finally duplicate the rgbtocmykInfo if its in use */
  if ( !updatergbtocmykinfo(&colorInfo->rgbtocmykInfo) ) {
    destroy_callpscache(&gscnew);
    return FALSE;
  }

  /* Refresh these variables after the update. */
  rgbtocmykInfo = colorInfo->rgbtocmykInfo;
  gsp = (fnType == FN_BLACK_GEN) ? &rgbtocmykInfo->bg : &rgbtocmykInfo->ucr;

  /* kill old entry and over-write with new */
  if ( gsp->cpsc )
    destroy_callpscache(&gsp->cpsc);
  gsp->cpsc = gscnew;
  Copy(&gsp->psObject, psobj);

  if ( !cc_invalidateColorChains(colorInfo, TRUE) )
    return FALSE;

  return TRUE;
}

Bool gsc_setblackgeneration(corecontext_t *corecontext,
                            GS_COLORinfo  *colorInfo,
                            STACK         *pstack)
{
  return do_setbgucr(colorInfo, pstack, FN_BLACK_GEN,
                     &corecontext->page->colorPageParams);
}

Bool gsc_setundercolorremoval(corecontext_t *corecontext,
                              GS_COLORinfo  *colorInfo,
                              STACK         *pstack)
{
  return do_setbgucr(colorInfo, pstack, FN_UNDER_COL_REM,
                     &corecontext->page->colorPageParams);
}

/* Log stripped */
