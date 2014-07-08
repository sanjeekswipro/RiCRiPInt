/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscpresp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements pre-separation space color link.
 */

#include "core.h"
#include "gscpresp.h"

#include "caching.h"        /* PENTIUM_CACHE_LOAD */
#include "display.h"        /* RENDER_RECOMBINE */
#include "dl_color.h"       /* FLOAT_TO_COLORVALUE */
#include "group.h"          /* groupMustComposite */
#include "gu_chan.h"        /* guc_getColorantMapping */
#include "halftone.h"       /* ht_setUsedDeviceRS */
#include "pattern.h"        /* NO_PATTERN */
#include "rcbcntrl.h"       /* rcbn_current_colorant */
#include "render.h"         /* inputpage */
#include "spdetect.h"       /* setscreen_separation */
#include "htrender.h"       /* ht_is_object_based_screen */

#include "gs_colorpriv.h"   /* CLINK */
#include "gscdevcipriv.h"   /* gsc_getSpotno */
#include "gscheadpriv.h"    /* GS_CHAINinfo */
#include "gschtonepriv.h"   /* cc_scan_halftone */
#include "gscparamspriv.h"  /* colorPageParams */


struct CLINKPRESEPARATIONinfo
{
  Bool                fUpdateHTCache ;
  Bool                fPattern ;
  GUCR_RASTERSTYLE    *hRasterStyle ;
  SPOTNO              spotno ;
  HTTYPE              httype;
  Bool                setoverprint ;
  Bool                overprintsEnabled;
  CLINK               *pHeadLink ;
  DL_STATE            *page ;
} ;

/* ---------------------------------------------------------------------- */

static void  preseparation_destroy( CLINK *pLink ) ;
static Bool preseparation_invokeSingle( CLINK *pLink , USERVALUE *dummy_oColorValue ) ;
static Bool preseparation_invokeBlock(  CLINK *pLink , CLINKblock *pBlock ) ;

static uint32 presepStructSize( void ) ;
static void   presepUpdatePtrs( CLINK *pLink ) ;

#if defined( ASSERT_BUILD )
static void presepAssertions( CLINK *pLink ) ;
#else
#define presepAssertions( pLink ) EMPTY_STATEMENT()
#endif

static Bool presepCheckSetScreen( CLINK *pLink ) ;

static void updateHTCacheForRecombine( CLINK *pLink ) ;

static CLINKfunctions CLINKpreseparation_functions =
{
  preseparation_destroy ,
  preseparation_invokeSingle ,
  preseparation_invokeBlock ,
  NULL /* scan */
} ;

CLINK *cc_preseparation_create(GS_COLORinfo         *colorInfo,
                               int32                colorType,
                               GS_CONSTRUCT_CONTEXT *context,
                               GS_CHAINinfo         *colorChain,
                               CLINK                *pHeadLink)
{
  COLORANTINDEX ci ;
  CLINK *pLink ;
  CLINKPRESEPARATIONinfo *preseparation ;
  COLOR_PAGE_PARAMS *colorPageParams;

  colorPageParams = context->colorPageParams;

#define CLID_SIZEpreseparation 4

  /* Get the current colorant index for the recombination separation */
  ci = rcbn_current_colorant() ;

  /* Try and allocate actual link */
  pLink = cc_common_create( 1 ,
                            & ci ,
                            SPACE_Preseparation ,
                            SPACE_Recombination ,
                            CL_TYPEpresep ,
                            presepStructSize() ,
                            & CLINKpreseparation_functions ,
                            CLID_SIZEpreseparation ) ;
  if ( pLink == NULL )
    return NULL ;

  presepUpdatePtrs( pLink ) ;

  preseparation = pLink->p.preseparation ;
  HQASSERT( preseparation != NULL , "cc_preseparation_create: preseparation NULL" ) ;

  /* Flag need to update ht cache. */
  preseparation->fUpdateHTCache = TRUE ;

  preseparation->fPattern = ( colorChain->patternPaintType !=
                              NO_PATTERN ) ;

  preseparation->hRasterStyle = gsc_getRS(colorInfo) ;
  HQASSERT(!guc_backdropRasterStyle(preseparation->hRasterStyle),
           "Expected device rasterstyle") ;

  /* Transparent objects should use the page's default screen */
  if ( cc_getOpaque(colorInfo,
                    (colorType == GSC_STROKE ? TsStroke : TsNonStroke))
       && (context->page->currentGroup == NULL
           || !groupMustComposite(context->page->currentGroup)) )
    preseparation->spotno = gsc_getSpotno(colorInfo) ;
  else
    preseparation->spotno = context->page->default_spot_no;
  preseparation->httype =
    ht_is_object_based_screen(preseparation->spotno)
    ? gsc_getRequiredReproType(colorInfo, colorType)
    : HTTYPE_DEFAULT;

  preseparation->setoverprint = gsc_getoverprint(colorInfo, colorType) ;
  preseparation->overprintsEnabled = cc_overprintsEnabled(colorInfo, colorPageParams);

  /* Point pHeadLink at the first link in this chain for use in the determination
   * of overprints etc. But use the the base space of an Indexed colorspace.
   * NB. This is only used for transparent linework images.
   */
  if ( pHeadLink == NULL )
    preseparation->pHeadLink = pLink ;
  else if ( pHeadLink->iColorSpace == SPACE_Indexed )
    preseparation->pHeadLink = pHeadLink->pnext ? pHeadLink->pnext : pLink ;
  else
    preseparation->pHeadLink = pHeadLink ;

  /* This page context will be used by dl_colors from invoke functions */
  preseparation->page = context->page;

  /* CLIDs identify all the unique characteristics of a CLINK and are used for
     color caching. */
  { CLID *idslot = pLink->idslot ; /* [CLID_SIZEpreseparation] */

    idslot[ 0 ] = (CLID)preseparation->spotno;
    idslot[ 1 ] = ci;
    idslot[ 2 ] = (preseparation->overprintsEnabled ? 0x0001 : 0x0000) |
                  (preseparation->setoverprint      ? 0x0002 : 0x0000);
    idslot[ 3 ] = (CLID)preseparation->httype;
  }

  presepAssertions( pLink ) ;

  return pLink ;
}

static void preseparation_destroy( CLINK *pLink )
{
  presepAssertions( pLink ) ;

  cc_common_destroy( pLink ) ;
}

static Bool preseparation_invokeSingle( CLINK *pLink , USERVALUE *dummy_oColorValue )
{
  COLORVALUE cv ;
  USERVALUE colorValue ;
  CLINKPRESEPARATIONinfo *preseparation ;
  dlc_context_t *dlc_context ;
  dl_color_t *dlc_current ;

  UNUSED_PARAM( USERVALUE * , dummy_oColorValue ) ;

  HQASSERT( dummy_oColorValue == NULL , "dummy_oColorValue != NULL" ) ;

  presepAssertions( pLink ) ;
  preseparation = pLink->p.preseparation ;

  if ( new_screen_detected ) {
    if ( !presepCheckSetScreen( pLink ) ) {
      return FALSE;
    }
  }

  /* Stage 1; go calculate overprints. */

  /* Does not exist in recombine. */

  colorValue = pLink->iColorValues[ 0 ] ;
  COLOR_01_ASSERT( colorValue , "preseparation input" ) ;

  /* Stage 2; simply convert fixed point pseudo Device Codes. */
  cv = FLOAT_TO_COLORVALUE( colorValue ) ;

  /* Stage 3; populate the halftone cache with the results. */
  dlc_context = preseparation->page->dlc_context ;
  dl_set_currentspflags(dlc_context, RENDER_RECOMBINE) ;
  if ( preseparation->fPattern )
    dl_set_currentspflags(dlc_context,
                          dl_currentspflags(dlc_context) | RENDER_PATTERN) ;
  if ( preseparation->fUpdateHTCache )
    updateHTCacheForRecombine( pLink ) ;

  /* Stage 4; reduce the resultant colors based on overprints. We must pass
   * sorted arrays of colorants and colorvalues to dlc_alloc_fillin. The
   * colorants have been pre-sorted but the colorvalues must be re-arranged
   * now.
   */

  /* Does not exist in recombine. */

  /* Stage 5; create a dl color object. */
  dlc_current = dlc_currentcolor(dlc_context) ;
  dlc_release(dlc_context, dlc_current) ;

  /* Set/clear knockout flag in case object turns out to be composite. The
   * overprint info is carried with overprintProcess.
   */
  if ( !preseparation->overprintsEnabled || !preseparation->setoverprint )
    dl_set_currentspflags(dlc_context,
                          dl_currentspflags(dlc_context) | RENDER_KNOCKOUT) ;

  return dlc_alloc_fillin(dlc_context,
                          1, pLink->iColorants, &cv, dlc_current) ;
}

static Bool preseparation_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  CLINKPRESEPARATIONinfo   *preseparation;
  int32                    nColors ;
  USERVALUE                *colorValues;
  COLORVALUE               *deviceCodes;
  uint8                    *overprintProcess;

  presepAssertions( pLink ) ;
  preseparation = pLink->p.preseparation ;

  /* Stage 2; simply convert fixed point pseudo Device Codes. */
  nColors = pBlock->nColors ;
  colorValues = pBlock->iColorValues ;
  deviceCodes = pBlock->deviceCodes ;
  overprintProcess = pBlock->overprintProcess;

  HQASSERT(colorValues != NULL, "colorValues is NULL");
  HQASSERT(deviceCodes != NULL, "deviceCodes is NULL");
  HQASSERT(overprintProcess != NULL, "overprintProcess is NULL");

  while ( nColors >= 8 ) {
    PENTIUM_CACHE_LOAD( deviceCodes + 7 ) ;
    deviceCodes[ 0 ] = FLOAT_TO_COLORVALUE( colorValues[ 0 ] ) ;
    deviceCodes[ 1 ] = FLOAT_TO_COLORVALUE( colorValues[ 1 ] ) ;
    deviceCodes[ 2 ] = FLOAT_TO_COLORVALUE( colorValues[ 2 ] ) ;
    deviceCodes[ 3 ] = FLOAT_TO_COLORVALUE( colorValues[ 3 ] ) ;
    deviceCodes[ 4 ] = FLOAT_TO_COLORVALUE( colorValues[ 4 ] ) ;
    deviceCodes[ 5 ] = FLOAT_TO_COLORVALUE( colorValues[ 5 ] ) ;
    deviceCodes[ 6 ] = FLOAT_TO_COLORVALUE( colorValues[ 6 ] ) ;
    deviceCodes[ 7 ] = FLOAT_TO_COLORVALUE( colorValues[ 7 ] ) ;
    colorValues += 8 ;
    deviceCodes += 8 ;
    nColors -= 8 ;
  }
  while ( nColors >= 1 ) {
    deviceCodes[ 0 ] = FLOAT_TO_COLORVALUE( colorValues[ 0 ] ) ;
    colorValues += 1 ;
    deviceCodes += 1 ;
    nColors -= 1 ;
  }

  /* Stage 3; populate the halftone cache with the results. */
  if ( preseparation->fUpdateHTCache )
    updateHTCacheForRecombine( pLink ) ;

  return TRUE ;
}

static uint32 presepStructSize( void )
{
  return sizeof( CLINKPRESEPARATIONinfo ) ;
}

static void presepUpdatePtrs( CLINK *pLink )
{
  pLink->p.preseparation = ( CLINKPRESEPARATIONinfo * )
    (( uint8 * )pLink + cc_commonStructSize( pLink )) ;
}

#if defined( ASSERT_BUILD )
static void presepAssertions( CLINK *pLink )
{
  cc_commonAssertions( pLink ,
                       CL_TYPEpresep ,
                       presepStructSize() ,
                       & CLINKpreseparation_functions ) ;
}
#endif

/* ---------------------------------------------------------------------- */
static Bool presepCheckSetScreen( CLINK *pLink )
{
  HQASSERT( pLink , "pLink NULL in deviceCheckSetScreen" ) ;
  HQASSERT( new_screen_detected , "new_screen_detected assumed to be TRUE" ) ;

  /* Check for a non-b/w object that could have come from a pre-separated job.
   * The input color space must therefore be DeviceGray. Since this is the
   * pre separation node we know we're DeviceGray, or a Gray derived from CMYK.
   * Should also ignore pattern screens, since these don't tell us anything.
   * [Note that patterns virtually never get here due to small frequency
   *  screen removal in spdetect.c.]
   * Since this is again a pre separation node this can't be true.
   */
  if ( pLink->iColorValues[ 0 ] > 0.0f &&
       pLink->iColorValues[ 0 ] < 1.0f ) {
    if ( !setscreen_separation( TRUE ) ) {
      return FALSE;
    }
  }
  else {
    if ( !setscreen_separation( FALSE ) ) {
      return FALSE;
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static void updateHTCacheForRecombine( CLINK *pLink )
{
  CLINKPRESEPARATIONinfo *preseparation ;

  presepAssertions( pLink ) ;

  preseparation = pLink->p.preseparation ;

  /* It is non-trivial to determine the final colorant(s) that will be rendered
     (this presep colorant is mapped to the separation colorant, then
     color-converted in the backend chain), and therefore it is safest to
     preserve the halftone in all colorants in the device rasterstyle. */
  ht_setUsedDeviceRS(preseparation->page->eraseno,
                     preseparation->spotno, preseparation->httype,
                     preseparation->hRasterStyle) ;

  preseparation->fUpdateHTCache = FALSE ;
}

/* Log stripped */
