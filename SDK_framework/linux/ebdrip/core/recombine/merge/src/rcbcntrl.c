/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbcntrl.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Control routines for recombine.
 */

#include "core.h"
#include "coreinit.h"
#include "swcopyf.h"
#include "dlstate.h"
#include "swerrors.h"
#include "gstate.h"
#include "vndetect.h"
#include "swmemory.h"
#include "spdetect.h"
#include "gschtone.h"
#include "params.h"
#include "pcmnames.h"
#include "halftone.h"
#include "recomb.h"
#include "rcbcntrl.h"
#include "namedef_.h"
#include "gscequiv.h"
#include "monitor.h"
#include "rcbadjst.h"
#include "miscops.h"

struct RCBSEPARATION {
  NAMECACHE     *nmPseudo ;
  COLORANTINDEX  ciPseudo ;

  NAMECACHE     *nmActual ;
  COLORANTINDEX  ciActual ;

  COLORANTINDEX  ciScreen ;

  Bool           fprocess ;
  int32          nconfidence ;

  int32          nequivlevel;
  EQUIVCOLOR     cmykequiv;

  struct RCBSEPARATION *next ;
} ;

typedef struct RCBPAGE {
  Bool           fenabled ;

  int            intercepting ; /**< -1 means rcbn_beginpage() not called
                                      0 means intercepting objects
                                     >0 means intercepting currently disabled */

  int32          npseudo ;
  int32          ncopies ;

  Bool           foutputpage ;
  Bool           fdeactivate ;
  Bool           fseparation ;
  Bool           fcomposite ;

  Bool           flastwasseparation ;
  int32          crecombineobjects ;

  int32          nconfidence ;

  int32          cshowpage ;
  int32          cprocess ;
  int32          cspots ;

  int32          ipcm ;

  int32          nseps ;
  RCBSEPARATION *pseps ;

  COLORANTINDEX  ciAborted;

  GUCR_RASTERSTYLE* virtualRasterStyle;

  /* For use in deciding whether to do object merging. */
  Bool           tryMergeLW;
  float          numObjectsSeenInCurrentSep;
  float          numObjectsMergedInCurrentSep;

} RCBPAGE ;

/* ----------------------------------------------------------------------- */

typedef struct rcbpcm {
  int32         nColors ;
  COLORSPACE_ID iColorSpace ;
  NAMECACHE   **pPCMNames ;
} RCBPCM ;

RCBPCM gpcms[ 2 ] = {
  { 1 , SPACE_DeviceGray , pcmGyName } ,
  { 4 , SPACE_DeviceCMYK , pcmCMYKNames }
} ;

#define RCB_MAX_PCMS NUM_ARRAY_ITEMS(gpcms)

/* RCBN_MIN_HT_CONFIDENCE is the minimum confidence level that needs to be
   achieved before the actual colorant for the current separation is used
   in the halftones.  If the actual colorant changes, spotnos are patched
   by linking the new actual colorant with the old.  The threshold reduces
   the number of halftone links, but it needs to be low enough to include
   the screen angle separation method (in case of no plate color comments).
   The actual colorant index used when the confidence is below the thresold
   is the default colorant, COLORANTINDEX_NONE.
 */
#define RCBN_MIN_HT_CONFIDENCE RCBN_CONFIDENCE_LO

/* Prefix for pseudo colorant name. */
#define RCB_SEPNAME_LEADER "HqnRecombineSep"

/* ----------------------------------------------------------------------- */
static RCBPAGE grcb = { 0 } ;

#if defined( ASSERT_BUILD )
static Bool debug_rcbcntrl = FALSE ;
#endif

/* ----------------------------------------------------------------------- */
static Bool rcbn_genpseudo( RCBSEPARATION *rcbs ) ;
static Bool rcbn_new_separation( void ) ;
static void rcbn_analyze_pcm( void ) ;
static void rcbn_composite_pcm( void ) ;
static Bool rcbn_showpage( void ) ;


/* ----------------------------------------------------------------------- */
static void init_C_globals_rcbcntrl(void)
{
  RCBPAGE init = { 0 } ;

  grcb = init ;

#ifdef ASSERT_BUILD
  debug_rcbcntrl = FALSE ;
#endif
}

IMPORT_INIT_C_GLOBALS( rcbdl )
IMPORT_INIT_C_GLOBALS( rcbtrap )
IMPORT_INIT_C_GLOBALS( rcbvigko )
IMPORT_INIT_C_GLOBALS( recomb )

void rcbn_merge_C_globals(core_init_fns *fns)
{
  UNUSED_PARAM(core_init_fns *, fns) ;
  init_C_globals_rcbcntrl() ;
  init_C_globals_rcbdl() ;
  init_C_globals_rcbtrap() ;
  init_C_globals_rcbvigko() ;
  init_C_globals_recomb() ;
}

static void rcbn_setVirtualRasterStyle(void)
{
  grcb.virtualRasterStyle = gsc_getTargetRS(gstateptr->colorInfo);
  if (! guc_backdropRasterStyle(grcb.virtualRasterStyle))
    grcb.virtualRasterStyle = NULL;
}

/* ----------------------------------------------------------------------- */
/** This routine gets called from setpagedevice (and reset variant) when we
 * switch on recombine and so need to initialise things for it.
 */
Bool rcbn_start( void )
{
  if ( !gsc_getOverprintPreview(gstateptr->colorInfo) ) {
    /* Recombine requires OverprintPreview because the virtual device RS is used
       to represent all the separations in the job and it is necessary to
       composite overprints to recombine correctly. */
    OBJECT icsDict;

    if ( !run_ps_string((uint8*)"<< /OverprintPreview true >>") )
      return FALSE;

    if ( theStackSize(operandstack) < 0 )
      return error_handler(STACKUNDERFLOW) ;

    Copy(&icsDict, theTop(operandstack));
    pop(&operandstack);

    if ( !gsc_setinterceptcolorspace(gstateptr->colorInfo, &icsDict) )
      return FALSE;
  }

  rcbn_setVirtualRasterStyle() ;

  if ( ! rcbn_new_separation())
    return FALSE ;

  /* This is the only place that recombine gets enabled. */
  grcb.fenabled = TRUE ;

  return TRUE ;
}

/* ----------------------------------------------------------------------- */
/** This routine gets called after we've rendered a page and is meant to reset
 * things for the next page.
 */
Bool rcbn_reset( void )
{
  if ( !grcb.fenabled )
    return TRUE ;

  rcbn_term() ;
  return rcbn_start() ;
}

/* ---------------------------------------------------------------------- */
Bool rcbn_beginpage(DL_STATE *page)
{
  if ( !grcb.fenabled )
    return TRUE;

  HQASSERT(grcb.nseps == 1, "Expected exactly one separation now");
  HQASSERT(grcb.pseps != NULL, "Expected new separation to already exist");

  /* Re-initialise the recombine state to use the new virtual raster style. */
  rcbn_term();
  rcbn_setVirtualRasterStyle();

  if (! rcbn_new_separation())
    return FALSE;

  grcb.fenabled = TRUE;
  grcb.intercepting = -1; /* Don't enable recombine interception
                             until we have suitable blend space */

  HQASSERT((grcb.virtualRasterStyle != NULL) == (page->currentGroup != NULL),
           "rcbn_beginpage: virtualRasterStyle/currentGroup inconsistent");
  if ( page->currentGroup != NULL ) {
    COLORSPACE_ID blendspace;

    HQASSERT(groupGetUsage(page->currentGroup) == GroupPage,
             "Expected page group only");

    guc_deviceToColorSpaceId(grcb.virtualRasterStyle, &blendspace);
    if ( blendspace == SPACE_DeviceCMYK || blendspace == SPACE_DeviceGray ) {
      /* Now a page group is in place with a suitable blend space, enable
         recombine interception, prepare the recombine DL code, and apply
         the separation info from a PlateColor: comment + friends */
      grcb.intercepting = 0;
      return reset_separation_detection_on_sub_page(gstateptr->colorInfo) &&
             rcb_dl_start(page);
    }
  }
  return TRUE;
}

/* ----------------------------------------------------------------------- */
Bool rcbn_endpage(DL_STATE *page, Group *pageGroup,
                  Bool recombined, Bool savedOverprintBlack)
{
  /* Page group is closing; turn off intercepting until the next
     rcbn_beginpage. */
  grcb.intercepting = -1;

  /* Finish recombining the DL, including fixing up vignette KOs, shfill
     decomposition, fixing up intercepted objects in a composite page etc.  Only
     attempt recombine adjustment if outputting the page and there were
     recombine intercepted objects on the DL. */
  return (rcb_dl_finish(page) &&
          (!grcb.foutputpage || !recombined ||
           rcba_prepare_dl(pageGroup, savedOverprintBlack)));
}

/* ----------------------------------------------------------------------- */
/** This routine gets called from setpagedevice (and reset variant) when we
 * switch off recombine and so need to clear things down (free memory etc...).
 * All recombine separation tracking info et al gets cleared.
 */
void rcbn_term( void )
{
  RCBSEPARATION *rcbs ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    RCBSEPARATION *trcbs = rcbs ;
    rcbs = rcbs->next ;
    mm_free( mm_pool_temp , trcbs , sizeof( RCBSEPARATION )) ;
  }

  /* This is the only place that recombine gets disabled. */
  grcb.fenabled = FALSE ;
  grcb.intercepting = -1 ; /* No intercepting until rcbn_beginpage() */

  grcb.npseudo = 0 ;
  grcb.ncopies = 0 ;

  grcb.foutputpage = FALSE ;
  grcb.fdeactivate = FALSE ;
  grcb.fseparation = FALSE ;
  grcb.fcomposite  = FALSE ;
  grcb.crecombineobjects = 0 ;

  grcb.nconfidence = RCBN_CONFIDENCE_NONE ;

  grcb.cshowpage = 0 ;
  grcb.cprocess = 0 ;
  grcb.cspots = 0 ;

  grcb.ipcm = -1 ;

  grcb.nseps = 0 ;
  grcb.pseps = NULL ;

  grcb.virtualRasterStyle = NULL;

  grcb.ciAborted = COLORANTINDEX_NONE;

  grcb.tryMergeLW = TRUE;

  grcb.numObjectsSeenInCurrentSep = 0.0f;
  grcb.numObjectsMergedInCurrentSep = 0.0f;
}

/* ----------------------------------------------------------------------- */
void rcbn_quit(void)
{
  /* quit is being called.  Recombine usually automatically outputs the page on
     device deactivation but if the deactivation is done by quit then outputting
     the page is the last thing we want to try.  Outputting the page is very
     likely to not to succeed, causing quit to fail. */
  HQTRACE(debug_rcbcntrl, ("rcbn_quit: clearing recombine state"));
  rcbn_term();
}

/* ----------------------------------------------------------------------- */
/** This routines return TRUE if recombine is activated.
 */
Bool rcbn_enabled( void )
{
  /* Recombine is purely a frontend operation. If we're not in the interpreter
     then and don't look at the grcb global! */
  return grcb.fenabled && IS_INTERPRETER() ;
}

/* ----------------------------------------------------------------------- */
/** This routine returns TRUE if recombine is activate and we want to either:
 * a) intercept gray marks drawn on the page, or, b) intercept color detection
 * decisions.
 */
Bool rcbn_intercepting( void )
{
  /* Recombine interception is purely a frontend operation. If we're not in the
     interpreter then intercepting is off (and don't look at the grcb global!). */
  return grcb.fenabled && grcb.intercepting == 0 && IS_INTERPRETER() ;
}

/* ----------------------------------------------------------------------- */
/** This routine (in conjunction with the one below) is used to temporarily
 * enable/disable recombine interception. For example when drawing marks in
 * BeginPage/EndPage procedure, etc...
 */
void rcbn_enable_interception( GS_COLORinfo *colorInfo )
{
  if ( grcb.intercepting == -1 )
    return; /* This is a no-op, rcbn_beginpage() hasn't been called. */
  --grcb.intercepting ;
  HQASSERT( grcb.intercepting >= 0 , "grcb.intercepting gone -ve" ) ;
  if ( grcb.fenabled && grcb.intercepting == 0 ) {
    if (!gsc_redo_setscreen( colorInfo ))
      HQFAIL("gsc_redo_setscreen shouldn't fail");
    gsc_markChainsInvalid(colorInfo);
  }
}

/* ----------------------------------------------------------------------- */
/** This routine (in conjunction with the one above) is used to temporarily
 * enable/disable recombine interception. For example when drawing marks in
 * BeginPage/EndPage procedure, etc...
 */
void rcbn_disable_interception( GS_COLORinfo *colorInfo )
{
  if ( grcb.intercepting == -1 )
    return; /* This is a no-op, rcbn_beginpage() hasn't been called. */
  ++grcb.intercepting ;
  if ( grcb.fenabled && grcb.intercepting == 1 ) {
    if (!gsc_redo_setscreen( colorInfo ))
      HQFAIL("gsc_redo_setscreen shouldn't fail");
    gsc_markChainsInvalid(colorInfo);
  }
}

/* ----------------------------------------------------------------------- */
/** This routine generates a pseudo colorant index to use in recombine
   dl colors to identify a particular input separation. Note that pseudo
   colorants bear no relation to real (gu_chan) device colorant since the
   RasterStyle handle changes between separations so simply reserving a
   colorant can not guarantee a unique colorant for lifetime of the page.
   However, a reserved colorant, valid only for a single separation, is
   required to build the presep screen. At the end of the separation the
   presep screen is patched over a actual colorant screen, if appropriate.
 */
static Bool rcbn_genpseudo( RCBSEPARATION *rcbs )
{
  NAMECACHE     *nmPseudo ;
  COLORANTINDEX  ciPseudo ;

  uint8 strPseudo[] = RCB_SEPNAME_LEADER"12345678" ;

  ciPseudo = grcb.npseudo++ ;

  swcopyf( strPseudo + strlen(( char * )RCB_SEPNAME_LEADER ) ,
    ( uint8 * )"%0.8x" , ciPseudo ) ;

  nmPseudo = cachename( strPseudo , strlen_uint32(( char * )strPseudo )) ;
  if ( nmPseudo == NULL )
    return FALSE ;

  if (grcb.virtualRasterStyle != NULL) {
    if (!guc_colorantIndexPossiblyNewName(grcb.virtualRasterStyle,
                                          nmPseudo, &ciPseudo))
      return FALSE;
  }

  rcbs->nmPseudo = nmPseudo ;
  rcbs->ciPseudo = ciPseudo ;

  /* ciScreen is not the same as ciPseudo, its value comes from gu_chan as
     it must exist along side screens for real colorants. It is only valid
     during the life of the separation */
  return guc_colorantIndexPossiblyNewName(gsc_getRS(gstateptr->colorInfo) ,
                                          nmPseudo, &rcbs->ciScreen) ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to add a new recombine separation structure so that we
 * can track information about a possible new separation. Note that it is quite
 * possible to have a trailing one of these at the end of a recombine set of
 * separations which gets removed when we output the page.
 */
static Bool rcbn_new_separation( void )
{
  RCBSEPARATION *rcbs ;

  rcbs = mm_alloc( mm_pool_temp ,
                   sizeof( RCBSEPARATION ) ,
                   MM_ALLOC_CLASS_RCB_SEPINFO ) ;
  if ( rcbs == NULL )
    return error_handler( VMERROR ) ;

  rcbs->nmPseudo = NULL ;
  rcbs->ciPseudo = COLORANTINDEX_UNKNOWN ;
  rcbs->nmActual = NULL ;
  rcbs->ciActual = COLORANTINDEX_NONE ; /* default colorant */
  rcbs->ciScreen = COLORANTINDEX_UNKNOWN ;
  rcbs->fprocess = FALSE ;
  rcbs->nconfidence = RCBN_CONFIDENCE_NONE ;

  rcbs->nequivlevel = GSC_EQUIV_LVL_NONEKNOWN ;
  rcbs->cmykequiv[0] = rcbs->cmykequiv[1] = 0.0f ;
  rcbs->cmykequiv[2] = rcbs->cmykequiv[3] = 0.0f ;

  if ( ! rcbn_genpseudo( rcbs )) {
    mm_free( mm_pool_temp , rcbs , sizeof( RCBSEPARATION )) ;
    return FALSE ;
  }

  rcbs->next = grcb.pseps ;
  grcb.pseps = rcbs ;
  grcb.nseps += 1 ;

  grcb.numObjectsSeenInCurrentSep = 0.0f;
  grcb.numObjectsMergedInCurrentSep = 0.0f;

  /* Need to rebuild any color chains since the preseparation node now needs
   * to refer to a new pseudo colorant index.
   */
  gsc_markChainsInvalid(gstateptr->colorInfo);

  return TRUE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to test if we've already got a separation with the
 * given name. A duplicate then outputs the page.
 */
static Bool rcbn_repeat_separation( NAMECACHE *nmActual )
{
  RCBSEPARATION *rcbs ;

  rcbs = grcb.pseps ;
  HQASSERT( rcbs , "haven't got any recombine separations to check" ) ;
  rcbs = rcbs->next ;
  while ( rcbs ) {
    if ( nmActual == rcbs->nmActual ) {
      HQTRACE( debug_rcbcntrl ,
              ( "rcbn_repeat_separation: %.*s" , nmActual->len , nmActual->clist )) ;
      return TRUE ;
    }
    rcbs = rcbs->next ;
  }
  return FALSE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is called by the separation detection code when it has some
 * new information about the current page/separation as to what it is
 * (Monochromatic, Composite, Separation) and how likely it is to be correct.
 */
Bool rcbn_register_separation( NAMECACHE *sepname , int32 nconfidence )
{
  GUCR_RASTERSTYLE* rasterstyle;
  NAMECACHE     *nmActual ;
  COLORANTINDEX  ciActual ;
  RCBSEPARATION *rcbs ;

  nmActual = sepname ;
  HQASSERT( nmActual , "nmActual NULL in rcbn_register_separation" ) ;

  if ( nmActual == system_names + NAME_Composite )
    nmActual = system_names + NAME_Gray ;

  if ( nconfidence == RCBN_CONFIDENCE_MAX )
    if ( rcbn_repeat_separation( nmActual )) {
      if ( ! rcbn_showpage())
        return FALSE ;
    }

  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_register_separation: %.*s (%d)" , nmActual->len ,
      nmActual->clist , nconfidence )) ;

  rcbs = grcb.pseps ;
  HQASSERT( rcbs , "haven't got any recombine separations to update" ) ;

  if ( nconfidence < rcbs->nconfidence )
    return TRUE ;

  rasterstyle = grcb.virtualRasterStyle;
  if (rasterstyle == NULL)
    rasterstyle = gsc_getRS(gstateptr->colorInfo);

  if (!guc_colorantIndexPossiblyNewName(rasterstyle, nmActual, &ciActual))
    return FALSE;
  HQASSERT( ciActual >= 0 , "got unexpected colorant index for actual sep name" ) ;

  rcbs->nmActual = nmActual ;
  rcbs->ciActual = ciActual ;

  rcbs->nconfidence = nconfidence ;

  return TRUE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is called by all the page output routines (showpage, copypage)
 * when it knows that a page is going to be output.
 */
Bool rcbn_register_showpage( int32 ncopies )
{
  Bool fseparation ;
  RCBSEPARATION *rcbs ;

  HQASSERT( ncopies >= 0 , "ncopies should be >= 0" ) ;

  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_register_showpage: #copies == %d" , ncopies )) ;

  if ( ncopies == 0 ) {
    /* This situation happens when we get miscellaneous extra dummy pages
     * from stupid applications like Illustrator,...
     *
     * Bump up the current pseudo colorant index to forget about the pseudo
     * colorant used in this dummy separation.  This means any objects added or
     * channels merged will be ignored, because the recombine separation list no
     * longer retains any information about this pseudo colorant.
     */
    return rcbn_genpseudo(grcb.pseps) ;
  }

  grcb.flastwasseparation = FALSE;

  grcb.ncopies = ncopies ;
  grcb.cshowpage += 1 ;

  fseparation = sub_page_is_separations() ;
  if ( ! fseparation ) {
    /* Either composite or monochromatic. If we've got some pages already we might
     * as well take it as Black if we can as the only alternative is to abort.
     */
    if ( grcb.cshowpage > 1 ) {
      if ( ! sub_page_is_composite())
        fseparation = TRUE ;
    }
  }

  if ( fseparation ) {
    NAMECACHE *nmActual ;

    HQASSERT( grcb.cshowpage == 1 ||
              grcb.fseparation ,
              "Somehow got separation after non-separation" ) ;

    /* Check that we've got a known separation and not a duplicate one. */
    rcbs = grcb.pseps ;
    nmActual = rcbs->nmActual ;
    if ( nmActual == NULL )
      return detail_error_handler( UNDEFINEDRESULT ,
             "PlateColor is undetermined while recombining." ) ;

    while (( rcbs = rcbs->next ) != NULL ) {
      if ( nmActual == rcbs->nmActual )
        return detailf_error_handler( UNDEFINEDRESULT ,
                                      "PlateColor '%.*s' is duplicated while recombining." ,
                                      nmActual->len ,
                                      nmActual->clist ) ;
    }
    rcbs = grcb.pseps ;

    if (grcb.virtualRasterStyle) {
      if (! guc_addAutomaticSeparation(grcb.virtualRasterStyle,
                                       rcbs->nmActual, TRUE))
        return FALSE;

      /* ciActual was a reserved colorant and should be unchanged after
         promotion to a fully-fledged colorant. */
      HQASSERT(rcbs->ciActual == guc_colorantIndex(grcb.virtualRasterStyle,
                                                   rcbs->nmActual),
               "ciActual changed during promotion to fully-fledged colorant");
    }

    /* Try to find cmyk equivalents for the spot.  Equivs may not exist yet, if,
       for example, the job puts all CustomColor comments at the end of the job
       (in which case we'll have another go in rcbn_showpage). */
    if (!gsc_rcbequiv_lookup(gstateptr->colorInfo, nmActual,
                             rcbs->cmykequiv, &rcbs->nequivlevel))
      return FALSE;

    grcb.fseparation = TRUE ;
    grcb.flastwasseparation = TRUE;

    rcbs = grcb.pseps ;
    if ( rcbs->nconfidence > grcb.nconfidence )
      grcb.nconfidence = rcbs->nconfidence ;

    if ( ! grcb.fcomposite ) {
      GUCR_RASTERSTYLE* rasterstyle = (grcb.virtualRasterStyle
                                       ? grcb.virtualRasterStyle
                                       : gsc_getRS(gstateptr->colorInfo));
      RCBSEPARATION* rcbs = grcb.pseps;
      COLORANTINDEX* cimap;

      /* Map the screens built according to preseparated rules on to the
         final screens, now we know the job is actually preseparated. If the
         separation is not being output (no equivalent real colorant), then
         it is converted to process and uses the default process screens. */
      if (rcbs->ciActual != COLORANTINDEX_NONE &&
          guc_equivalentRealColorantIndex(rasterstyle, rcbs->ciActual, & cimap)) {
        do {
          GUCR_COLORANT* pColorant;
          const GUCR_COLORANT_INFO *colorantInfo;

          /* Lookup the equivalent real colorants in the real rasterstyle. */
          gucr_colorantHandle(gsc_getRS(gstateptr->colorInfo), *cimap, & pColorant);
          HQASSERT(pColorant, "pColorant must be in the real rasterstyle");

          if ( pColorant != NULL &&
               gucr_colorantDescription(pColorant, &colorantInfo) ) {
            /* ciScreen is the screen built according to presep rules. *cimap
             * is the existing screen, built to composite rules, which will
             * be replaced by ciScreen. */
            if (! ht_patchSpotnos(rcbs->ciScreen, *cimap, colorantInfo->name,
                                  FALSE) )
              return FALSE;
          }
          ++cimap;
        } while (*cimap != COLORANTINDEX_UNKNOWN);
        invalidate_gstate_screens();
      }
    }

    if ( grcb.nconfidence != RCBN_CONFIDENCE_MAX ) {
      if ( grcb.cshowpage == 4 ) {
        HQTRACE( debug_rcbcntrl ,
               ( "rcbn_register_showpage: force 4 pages due to low confidence" )) ;
        return rcbn_showpage() ;
      }
    }
  }
  else {

    if ( grcb.cshowpage > 1 )
      return detail_error_handler( UNDEFINEDRESULT ,
             "Composite page found after Separated page." ) ;

    if (!rcbn_register_separation( system_names + NAME_Gray , RCBN_CONFIDENCE_MAX ))
      return FALSE ;

    grcb.fcomposite = TRUE ;

    HQTRACE( debug_rcbcntrl ,
      ( "rcbn_register_showpage: grcb.fcomposite = TRUE" )) ;
    return rcbn_showpage() ;
  }

  /* Start a new separation. */
  return rcbn_new_separation() ;
}

/* ----------------------------------------------------------------------- */
/** This routine is called by the pagedevice deactivation code. It needs to
 * handle the output of any separations accumulated so far as well as
 * AutoShowPage.
 */
Bool rcbn_register_deactivate(Bool forced, Bool fautopage, int32 ncopies)
{
  grcb.fdeactivate = TRUE ;

  if ( ! forced ) {
    if ( grcb.cshowpage > 0 ) {
      HQTRACE( debug_rcbcntrl ,
        ( "rcbn_register_deactivate: Output == 1" )) ;

      return rcbn_showpage() ;
    }
    else if ( fautopage ) {
      HQTRACE( debug_rcbcntrl ,
              ( "rcbn_register_deactivate: fautopage" )) ;

      if ( ! rcbn_register_showpage( ncopies ))
        return FALSE ;

      /* Note the above call to rcbn_register_showpage may do a call to
       * rcbn_showpage for us (if the job is composite) so we need to
       * test if it's been done or if we need to do it (for the
       * non-composite cases; e.g. single separation).  */
      if ( grcb.cshowpage == 0 )
        return TRUE ;
      else
        return rcbn_showpage() ;
    }
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is the only route through which pages get output when recombine
 * is on.
 */
static Bool rcbn_showpage( void )
{
  int32 gid;
  Bool result;
  GS_COLORinfo *savedColorInfo;

  /* Must analyse separations to determine PCM to use. */
  grcb.cprocess = 0 ;
  grcb.cspots = 0 ;

  if ( grcb.nseps != grcb.cshowpage ) {
    RCBSEPARATION *rcbs = grcb.pseps ;
    grcb.ciAborted = rcbs->ciPseudo;
    grcb.pseps = rcbs->next ;
    grcb.nseps -= 1 ;
    mm_free( mm_pool_temp , rcbs , sizeof( RCBSEPARATION )) ;
  }

#if defined( ASSERT_BUILD )
  /* Check that we've not got any duplicates. */
  { RCBSEPARATION *brcbs ;
    brcbs = grcb.pseps ;
    while ( brcbs != NULL ) {
      RCBSEPARATION *trcbs ;
      trcbs = brcbs->next ;
      while ( trcbs != NULL ) {
        HQASSERT( brcbs->ciPseudo != trcbs->ciPseudo , "duplicate ciPseudo" ) ;
        HQASSERT( brcbs->nmPseudo != trcbs->nmPseudo , "duplicate nmPseudo" ) ;
        HQASSERT( brcbs->ciActual != trcbs->ciActual , "duplicate ciActual" ) ;
        HQASSERT( brcbs->nmActual != trcbs->nmActual , "duplicate nmActual" ) ;
        trcbs = trcbs->next ;
      }
      brcbs = brcbs->next ;
    }
  }
#endif

  if ( grcb.fseparation )
    rcbn_analyze_pcm() ;
  if ( grcb.fcomposite )
    rcbn_composite_pcm() ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if (! grcb.fcomposite) {
    RCBSEPARATION *rcbs ;
    for ( rcbs = grcb.pseps ; rcbs ; rcbs = rcbs->next ) {
      /* Have a second attempt at finding cmyk/rgb equivalents because it may be
         that CMYKCustomColors: comments only became available at the end of the
         page.
         We'll call the lookup unconditionally because:
         - the priority of any value we already have might be lower than
           CMYKCustomColors:.
         - even if the colorant is renderable, the RGB equivalent would be useful
           for roam. */
      if ( !gsc_rcbequiv_lookup(gstateptr->colorInfo, rcbs->nmActual,
                                rcbs->cmykequiv, &rcbs->nequivlevel) )
        return FALSE ;

      /* If the colorant is neither renderable nor has cmyk equivalent colors
         then we have to give up recombining. */
      if ( rcbs->nequivlevel == GSC_EQUIV_LVL_NONEKNOWN &&
           (guc_colorantIndex(gsc_getRS(gstateptr->colorInfo), rcbs->nmActual) ==
              COLORANTINDEX_UNKNOWN) ) {
        uint8* template = (uint8*)"PlateColor '%.*s' has no process "
                                  "equivalent when recombining"; /* NB no '.' */
        OBJECT nullobj = OBJECT_NOTVM_NULL, detailobj = OBJECT_NOTVM_NOTHING;

        /* length of string above minus format chars + 1 for terminator */
        if ( !ps_string(&detailobj, NULL,
                        strlen_int32((char *)template) - 4 + 1 + rcbs->nmActual->len ) )
          return error_handler( UNDEFINEDRESULT ) ;

        swcopyf(oString(detailobj), template, rcbs->nmActual->len , rcbs->nmActual->clist ) ;

        HQASSERT(theLen(detailobj) == (uint16)strlen((char*)oString(detailobj)) + 1,
                 "Bad error string length");
        oString(detailobj)[theLen(detailobj)-1] = '.';  /* '.' replaces null */

        /** \todo @@@ FIXME (JJ) If we're in the middle of a device deactivate it
           will be too late to notify the user of the error.  Instead, report
           the error manually (the job will still claim to have completed but
           there will be no output).  To fix this properly would require a
           reworking of execjob. */
        if (grcb.fdeactivate) {
          monitorf(UVM("PlateColor '%.*s' has no process equivalent when recombining.\n"),
                   rcbs->nmActual->len , rcbs->nmActual->clist);
          monitorf(UVS("%%%%[ Warning: Failed to recombine job; no output has been generated ]%%%%\n")) ;
        }

        return errorinfo_error_handler( UNDEFINEDRESULT ,
                                        &nullobj,
                                        &detailobj ) ;
      }

      /* Set the cmyk equivs in the virtual rasterstyle so to be accessible by
         the spacecache colorspace object in final conversion. */
      if (grcb.virtualRasterStyle) {
        if (! guc_setCMYKEquivalents(grcb.virtualRasterStyle,
                                     rcbs->ciActual, rcbs->cmykequiv))
          return FALSE;
      }
    }
  }

  /* We are about to throw a logical page in recombine. The thing that's special
   * about recombine is that the page is thrown at some point after the showpage
   * for the last contributing pre-separated page. Therefore, the job might have
   * validly changed the gstate before we get here, so we must restore the gstate
   * to what it was before the page.
   * The exception is the raster style which will be updated to contain new
   * colorant info for the next recommbine pseudo colorant. That must be preserved
   * after the rest of the gstate is restored.
   */
  if ( ! gs_gpush( GST_SHOWPAGE ))
    return FALSE ;
  gid = gstackptr->gId ;
  savedColorInfo = gstackptr->colorInfo;

  rcbn_disable_interception(gstateptr->colorInfo);
  disable_separation_detection();

  grcb.foutputpage = TRUE ;

  result = do_pagedevice_showpage( TRUE ) ;

  enable_separation_detection();
  /* Recombine interception will already have been re-enabled in the beginpage
     after the showpage. If result is false, just wait for the next beginpage */
  HQASSERT(!result || rcbn_intercepting(),
           "Recombine interception should already have been re-enabled");

  /** \todo JJ If we're in the middle of a device deactivate it will be too
     late to notify the user of the error.  Instead, report the error manually
     (the job will still claim to have completed but there will be no
     output).  To fix this properly would require a reworking of execjob. */
  if ( !result && grcb.fdeactivate )
    monitorf(UVS("%%%%[ Warning: Failed to recombine job; no output has been generated ]%%%%\n")) ;

  /* Restore the gstate to what it was but with the updated raster style. */
  if (result) {
    GUCR_RASTERSTYLE *newTargetRS;
    newTargetRS = gsc_getTargetRS(gstateptr->colorInfo);
    gsc_setTargetRS(savedColorInfo, newTargetRS);
  }
  if (!gs_cleargstates( gid , GST_SHOWPAGE , NULL ))
    result = FALSE;

  return result;
}

/* ----------------------------------------------------------------------- */
/** This routine returns the pseudo index of the current separation.
 */
COLORANTINDEX rcbn_current_colorant( void )
{
  RCBSEPARATION *rcbs = grcb.pseps ;

  HQASSERT( rcbs , "rcbs NULL in rcbn_current_colorant" ) ;

  return rcbs->ciPseudo ;
}

/* ----------------------------------------------------------------------- */
Bool rcbn_build_preseparated_screens( void )
{
  RCBSEPARATION *rcbs = grcb.pseps ;
  HQASSERT( rcbs != NULL , "rcbs is null" ) ;
  HQASSERT( rcbn_intercepting() , "must be recombine intercepting" ) ;
  return rcbs->nconfidence >= RCBN_CONFIDENCE_MED3 ;
}

/* ----------------------------------------------------------------------- */
/* This routine returns the pseudo name & index of the current separation.
 */
COLORANTINDEX rcbn_presep_screen( NAMECACHE **nmPseudo )
{
  RCBSEPARATION *rcbs = grcb.pseps ;

  HQASSERT( rcbs , "rcbs NULL in rcbn_current_colorant" ) ;

  if ( nmPseudo != NULL )
    *nmPseudo = rcbs->nmPseudo ;

  /* Cannot use ciPseudo since colorant must be known to the raster handle,
     the screen mixes with other screens which use real colorants. */
  return rcbs->ciScreen ;
}

/* ----------------------------------------------------------------------- */
/** This routine gives the colorant index of the current separation
   to be used in the halftone, or if we are not reasonably confident
   we just give the default back.
*/
COLORANTINDEX rcbn_likely_separation_colorant( void )
{
  RCBSEPARATION *rcbs = grcb.pseps ;
  COLORANTINDEX ciActual = COLORANTINDEX_NONE;

  HQASSERT( rcbs , "rcbs NULL in rcbn_current_actual_colorant" ) ;

  if (rcbs->nconfidence >= RCBN_MIN_HT_CONFIDENCE) {
    if (grcb.virtualRasterStyle) {
      /* Can't use equivalent real colorant index because the separation is
         not guaranteed to have been added (by guc_addAutomaticSeparation)
         until the end of the separation. */
      ciActual = guc_colorantIndexReserved(gsc_getRS(gstateptr->colorInfo), rcbs->nmActual);
      if (ciActual == COLORANTINDEX_UNKNOWN)
        ciActual = COLORANTINDEX_NONE;
    } else {
      ciActual = rcbs->ciActual;
    }
  }
  return ciActual;
}

/* ----------------------------------------------------------------------- */
Bool rcbn_use_default_screen_angle( COLORANTINDEX ci )
{
  HQASSERT( rcbn_intercepting() , "should be recombine intercepting" ) ;
  HQASSERT( grcb.pseps != NULL && grcb.nseps >= 1 ,
            "should have at least one rcbs thingy" ) ;
  if ( ci != COLORANTINDEX_NONE &&
       ci != grcb.pseps->ciScreen &&
       rcbn_build_preseparated_screens() ) {
    /* Make the process screens use default angles. If the current
       separation is a spot which cannot be produced we'll need to
       convert to process and should use the default process screens.
       If current separation is process we'll replace the appropriate
       process screen with the special presep screen at showpage time
       (required to help with ripping composite jobs with recombine). */
    GUCR_COLORANT* pColorant ;
    const GUCR_COLORANT_INFO *colorantInfo;

    gucr_colorantHandle( gsc_getRS(gstateptr->colorInfo) , ci , & pColorant ) ;

    /* null handle implies ci not a fully fledged colorant */
    if ( pColorant != NULL &&
         gucr_colorantDescription(pColorant, &colorantInfo) ) {
      if ( colorantInfo->colorantType == COLORANTTYPE_PROCESS )
        return TRUE ; /* make default */
    }
  }
  return FALSE ; /* do not make default */
}

/* ---------------------------------------------------------------------- */
COLORANTINDEX rcbn_aborted_colorant(void)
{
  HQASSERT(grcb.nseps >= 1, "aborted separation pseudo colorant invalid");
  return grcb.ciAborted;
}

/* ---------------------------------------------------------------------- */
/** This routine returns the CMYK equivalents of the current separation, as well
 * as the level of preference of determining said equivalents.
 */
void rcbn_current_equiv_details( int32 **level , EQUIVCOLOR **equivs )
{
  RCBSEPARATION *rcbs = grcb.pseps ;

  HQASSERT( rcbs , "rcbs NULL in rcbn_current_colorant" ) ;

  *level = &(rcbs->nequivlevel) ;
  *equivs = &(rcbs->cmykequiv) ;
}

/* ----------------------------------------------------------------------- */
/** This routine returns TRUE if the page being output is deemed to be composite.
 * It used to be a dynamic flag, but now is static (although still used in a few
 * files in dynamic manner).
 */
Bool rcbn_composite_page( void )
{
  return grcb.fcomposite ;
}

/* ----------------------------------------------------------------------- */
/** This routine returns TRUE if we are on the first separation, i.e. that the
 * number of showpages we've seen is 1 (or less).
 */
Bool rcbn_first_separation( void )
{
  return grcb.cshowpage < 1;
}

/* ----------------------------------------------------------------------- */
#define POSSIBLY_LINEWORK(_opcode) ((_opcode) == RENDER_fill || \
                                    (_opcode) == RENDER_mask)
/** Decide whether to try an object merge for the new DL object.  Obviously
   can do no merging on the first separation - have nothing to merge with.
   - RecombineObject == 0: object recombine completely disabled.
   - RecombineObject == 1: object recombine enabled for all objects, subject
                           to dynamic control for certain object types.
   - RecombineObject == 2: object recombine enabled for all objects. */
Bool rcbn_merge_required(int32 opcode)
{
  Bool tryMerge = grcb.cshowpage >= 1;

  if (tryMerge) {
    switch (UserParams.RecombineObject) {
    case 0:
      /* Disable object recombine completely. */
      tryMerge = FALSE;
      break;
    default:
      HQFAIL("Unrecognised value for RecombineObject user param");
      /* FALLTHRU */
    case 1:
      /* Decide whether it is worth continuing to try object merging (but only
         do this after the minimum number of objects threshold has been reached,
         below this the dynamic testing may not be reliable).  This is intended
         to catch linework-like jobs which are the pathalogical object merge
         case (many objects, few merges). */
      if (POSSIBLY_LINEWORK(opcode)) {
        if (grcb.tryMergeLW &&
            grcb.numObjectsSeenInCurrentSep >= 1 &&
            grcb.numObjectsSeenInCurrentSep > UserParams.RecombineObjectThreshold) {
          float proportionMerged;
          HQASSERT(grcb.numObjectsMergedInCurrentSep
                   <= grcb.numObjectsSeenInCurrentSep,
                   "cannot have merged more objects than objects seen");
          proportionMerged = (grcb.numObjectsMergedInCurrentSep
                              / grcb.numObjectsSeenInCurrentSep);
          if (proportionMerged < UserParams.RecombineObjectProportion) {
            grcb.tryMergeLW = FALSE;
#if defined (DEBUG_BUILD)
            monitorf((uint8*)"RECOMBINE DEBUG MESSAGE: Detected linework job, "
                             "disabling object recombine\n");
#endif
          }
        }
        tryMerge = grcb.tryMergeLW;
      }
      break;
    case 2:
      /* Always allow object recombine (when appropriate). */
      break;
    }
  }

  HQASSERT(! tryMerge || grcb.cshowpage >= 1, "Should not be trying to merge");

  return tryMerge;
}

/* ----------------------------------------------------------------------- */
void rcbn_object_merge_result(uint8 opcode, int32 merged)
{
  if (POSSIBLY_LINEWORK(opcode) && UserParams.RecombineObject == 1) {
    /* Possibly LW spans which do not object recombine well and therefore keep
       track of how well these objects are recombining.  Only count objects when
       RecombineObject is set to dynamic.  The RecombineObject param can
       deliberatly change in the course of the job (eg, around Shira LW comments
       object recombine is completely disabled and objects inside the comments
       are not counted). */
    ++(grcb.numObjectsSeenInCurrentSep);
    if (merged != MERGE_NONE)
      ++(grcb.numObjectsMergedInCurrentSep);
  }
}

/* ----------------------------------------------------------------------- */
/** This routine returns TRUE if we are on the third or subsequent separation.
 * It is used to determine if a reorder of the DL might be necessary due to
 * objects have got out of order.
 */
Bool rcbn_order_important( void )
{
  return grcb.cshowpage >= 2;
}

/* ----------------------------------------------------------------------- */
/** This routine returns the number of copies to be output of the recombined page.
 * We need to track this since some applications do 'clever' things like setting
 * hash-copies to 0 to supress dummy pages and we need to ignore those 0s.
 */
void rcbn_copies( int32 *ncopies )
{
  if ( grcb.foutputpage ) {
    if ( *ncopies != grcb.ncopies )
      HQTRACE( debug_rcbcntrl ,
      ( "rcbn_copies: #copies changed == %d, %d" , grcb.ncopies , *ncopies )) ;
    *ncopies = grcb.ncopies ;
  }
}

/* ----------------------------------------------------------------------- */
/** This routine is used to count the number of DL objects that are added to the
 * DL that are in recombine space (i.e. whose colorants are pseudo colorants and
 * whose colors are in our fixed 1.14 point format).
 */
void rcbn_add_recombine_object( void )
{
  HQASSERT( grcb.pseps != NULL , "grcb.pceps is null" ) ;
  ++grcb.crecombineobjects ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used at the end of color converting all recombine DL objects
 * to set the count of said DL objects back to zero.
 */
void rcbn_set_recombine_object( int32 cn )
{
  grcb.crecombineobjects = cn ;
}


/* ----------------------------------------------------------------------- */
/** This routine is used by pagedevice code to control if it can call the BeginPage
 * routine or not. BeginPage should only get called for the first page of a set
 * of recombine separations.
 */
Bool rcbn_do_beginpage( void )
{
  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_do_beginpage: BeginPage == %d" , grcb.cshowpage == 0 ? TRUE : FALSE )) ;

  return (grcb.cshowpage == 0) ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used by pagedevice code to control if it can call the EndPage
 * routine or not. EndPage should only get called for the last page of a set
 * of recombine separations.
 */
Bool rcbn_do_endpage( void )
{
  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_do_endpage: EndPage == %d" , grcb.foutputpage || grcb.fdeactivate )) ;

  return ( grcb.foutputpage || grcb.fdeactivate ) ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used by pagedevice code to control if it can actually
 * render a page or not. This is only activated through use of
 * rcbn_showpage, i.e., it is always the recombine control code that
 * controls outputting pages.  */
Bool rcbn_do_render( void )
{
  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_do_render: Output == %d" , grcb.foutputpage )) ;

  return grcb.foutputpage ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used by pagedevice code to control if it can deactivate a page
 * device or not. This is so that we can preserve recombine separations across
 * multiple page devices.
 */
Bool rcbn_do_deactivate( void )
{
  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_do_deactivate: Deactivate == %d" , grcb.cshowpage == 0 ? TRUE : FALSE )) ;
  /* If recombine has seen any showpages, then we can't deactivate yet */
  return ( grcb.cshowpage == 0 ) ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to analyze the entire list of recombine separation
 * structures to determine which ProcessColorModel (PCM) it should use. For now
 * the only two options are Gray (for Composite jobs) or CMYK (for Recombined
 * Separations). Later on we'll probably add Hexachrome but we'll never add RGB.
 */
static void rcbn_analyze_pcm( void )
{
  int32 i ;
  int32 ipcm ;
  RCBSEPARATION *rcbs ;
  int32 cspots[ RCB_MAX_PCMS ] ;
  int32 cprocess[ RCB_MAX_PCMS ] ;

  for ( i = 0 ; i < RCB_MAX_PCMS ; ++i )
    cspots[ i ] = cprocess[ i ] = 0 ;

  ipcm = -1 ;
  rcbs = grcb.pseps ;
  while ( rcbs ) {
    NAMECACHE *nmActual = rcbs->nmActual ;
    for ( i = 0 ; i < RCB_MAX_PCMS ; ++i ) {
      int32 j ;
      for ( j = 0 ; j < gpcms[ i ].nColors ; ++j ) {
        NAMECACHE *pPCMName = gpcms[ i ].pPCMNames[ j ] ;
        if ( nmActual == pPCMName ) {
          ++cprocess[ i ] ;
          if ( ipcm < 0 ||
               cprocess[ i ] > cprocess[ ipcm ] )
            ipcm = i ;
          break ;
        }
      }
      if ( j == gpcms[ i ].nColors )
        ++cspots[ i ] ;
    }
    rcbs = rcbs->next ;
  }

  if ( ipcm < 0 )
    ipcm = 0 ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    NAMECACHE *nmActual = rcbs->nmActual ;
    int32 j ;
    for ( j = 0 ; j < gpcms[ ipcm ].nColors ; ++j ) {
      NAMECACHE *pPCMName = gpcms[ ipcm ].pPCMNames[ j ] ;
      if ( nmActual == pPCMName ) {
        rcbs->fprocess = TRUE ;
        break ;
      }
    }
    rcbs = rcbs->next ;
  }

  HQTRACE( debug_rcbcntrl ,
    ( "rcbn_analyze_pcm: pcm == %d (spot = %d, process = %d)" ,
      ipcm , cspots[ ipcm ] , cprocess[ ipcm ] )) ;

  grcb.ipcm = ipcm ;
  grcb.cspots = cspots[ ipcm ] ;
  grcb.cprocess = cprocess[ ipcm ] ;

  HQASSERT( grcb.cshowpage == grcb.cspots + grcb.cprocess , "Sep counts out of sync" ) ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to set the ProcessColorModel for a Composite job. It
 * essentially sets it to Gray.
 */
static void rcbn_composite_pcm( void )
{
  RCBSEPARATION *rcbs ;

  rcbs = grcb.pseps ;
  rcbs->fprocess = TRUE ;

  HQASSERT( rcbs->next == NULL , "Should only be 1 composite page" ) ;

  grcb.ipcm = 0 ;
  grcb.cspots = 0 ;
  grcb.cprocess = 1 ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the color space id of the determined PCM.
 */
COLORSPACE_ID rcbn_icolorspace( void )
{
  HQASSERT( grcb.ipcm >= 0 , "Illegal PCM" ) ;

  return gpcms[ grcb.ipcm ].iColorSpace ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the number of colors of the determined PCM.
 */
int32 rcbn_ncolorspace( void )
{
  HQASSERT( grcb.ipcm >= 0 , "Illegal PCM" ) ;

  return gpcms[ grcb.ipcm ].nColors ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the color space name of the determined PCM.
 */
NAMECACHE **rcbn_nmcolorspace( void )
{
  HQASSERT( grcb.ipcm >= 0 , "Illegal PCM" ) ;

  return gpcms[ grcb.ipcm ].pPCMNames ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the number of recombine pages encountered.
 */
int32 rcbn_cseps( void )
{
  return grcb.cshowpage ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the number of spot color pages encountered.
 */
int32 rcbn_cspotseps( void )
{
  return grcb.cspots ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the number of process color pages
    encountered.
 */
int32 rcbn_cprocessseps( void )
{
  return grcb.cprocess ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the pseudo colorant index of a recombine
 * separation given its actual name. e.g. you ask what is the ci of "Cyan".
 * If the recombine separation doesn't exist it returns COLORANTINDEX_NONE.
 */
COLORANTINDEX rcbn_nm_ciPseudo( NAMECACHE *nmActual )
{
  RCBSEPARATION *rcbs ;

  HQASSERT( nmActual , "nmActual NULL in rcbn_nm_ciPseudo" ) ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    HQASSERT( rcbs->nmActual , "rcbs->nmActual somehow NULL" ) ;
    if ( nmActual == rcbs->nmActual )
      return rcbs->ciPseudo ;
    rcbs = rcbs->next ;
  }
  return COLORANTINDEX_NONE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the pseudo colorant index of a recombine
 * separation given its actual colorant index.
 * If the recombine separation doesn't exist it returns COLORANTINDEX_NONE.
 */
COLORANTINDEX rcbn_ciPseudo( COLORANTINDEX ciActual )
{
  RCBSEPARATION *rcbs ;

  HQASSERT( ciActual >= 0 , "ciActual < 0 in rcbn_ciPseudo" ) ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    if ( ciActual == rcbs->ciActual )
      return rcbs->ciPseudo ;
    rcbs = rcbs->next ;
  }
  return COLORANTINDEX_NONE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the real (gu_chan) device colorant index of
 * a recombine separation given its pseudo colorant index. If the recombine
 * separation doesn't exist it returns COLORANTINDEX_NONE.
 */
COLORANTINDEX rcbn_ciActual( COLORANTINDEX ciPseudo )
{
  RCBSEPARATION *rcbs ;

  HQASSERT( ciPseudo >= 0 , "ciPseudo < 0 in rcbn_ciActual" ) ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    if ( ciPseudo == rcbs->ciPseudo )
      return rcbs->ciActual ;
    rcbs = rcbs->next ;
  }
  return COLORANTINDEX_NONE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the real (gu_chan) device separation name of
 * a recombine separation given its pseudo colorant index. If the recombine
 * separation doesn't exist it returns NULL.
 */
NAMECACHE *rcbn_nmActual( COLORANTINDEX ciPseudo )
{
  RCBSEPARATION *rcbs ;

  HQASSERT( ciPseudo >= 0 , "ciPseudo < 0 in rcbn_nmActual" ) ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    if ( ciPseudo == rcbs->ciPseudo )
      return rcbs->nmActual ;
    rcbs = rcbs->next ;
  }
  return NULL ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the real name (e.g., "Cyan") of a
 * recombine separation and its CMYK process equivalents given its
 * actual colorant index.  */
NAMECACHE *rcbn_nmActual_and_equiv( GUCR_RASTERSTYLE* rasterStyle ,
                                    COLORANTINDEX ciActual ,
                                    EQUIVCOLOR **equiv,
                                    void *private_data )
{
  RCBSEPARATION *rcbs ;

  UNUSED_PARAM( GUCR_RASTERSTYLE*, rasterStyle ) ;
  UNUSED_PARAM( void*, private_data ) ;

  HQASSERT( ciActual >= 0 , "ciActual < 0 in rcbn_nmActual_" ) ;
  HQASSERT( equiv , "equiv null in rcbn_nmActual_" ) ;

  rcbs = grcb.pseps ;
  while ( rcbs ) {
    if ( ciActual == rcbs->ciActual ) {
      *equiv = &(rcbs->cmykequiv) ;
      return rcbs->nmActual ;
    }
    rcbs = rcbs->next ;
  }
  *equiv = NULL ;
  return NULL ;
}

/* ----------------------------------------------------------------------- */
Bool rcbn_mapping_create(COLORANTINDEX **mapping, int32 *map_length,
                         COLORANTINDEX ciCompositeGrayActual)
{
  COLORANTINDEX ci, ciMax ;
  COLORANTINDEX *pseudomap ;
  RCBSEPARATION *sep ;

  HQASSERT(mapping != NULL, "Nowhere to put new recombine mapping") ;
  HQASSERT(map_length != NULL, "Nowhere to put recombine mapping length") ;
  HQASSERT(grcb.pseps != NULL, "No recombine separation") ;

  ciMax = grcb.pseps->ciPseudo ;
  if ( (pseudomap = mm_alloc(mm_pool_temp, (ciMax + 1) * sizeof(COLORANTINDEX),
                             MM_ALLOC_CLASS_RCB_ADJUST)) == NULL )
    return error_handler(VMERROR) ;

  /* Initialise pseudo map to unknown. Dummy pseudo colorants will be
     removed. */
  for ( ci = 0 ; ci <= ciMax ; ++ci )
    pseudomap[ci] = COLORANTINDEX_UNKNOWN ;

  /* Map known pseudo channels to actual channels. */
  for ( sep = grcb.pseps ; sep ; sep = sep->next ) {
    HQASSERT(sep->ciPseudo <= ciMax,
             "Last pseudo index added wasn't highest index") ;
    pseudomap[sep->ciPseudo] = sep->ciActual ;
  }

  /* If a composite page was detected, and we have a real gray index for it,
     map the current detected pseudo index (the gray index) to the actual
     colorant for the separation. */
  if ( grcb.fcomposite ) {
    HQASSERT(ciCompositeGrayActual != COLORANTINDEX_UNKNOWN,
             "Recombine page does not have gray channel") ;
    pseudomap[ciMax] = ciCompositeGrayActual ;
  }

  *mapping = pseudomap ;
  *map_length = (int32)(ciMax + 1) ;

  return TRUE ;
}

void rcbn_mapping_free(COLORANTINDEX **mapping, int32 map_length)
{
  HQASSERT(mapping && *mapping, "No recombine mapping to free") ;
  HQASSERT(grcb.pseps != NULL, "No recombine separation") ;
  HQASSERT(map_length == grcb.pseps->ciPseudo + 1, "Mapping length doesn't match") ;
  mm_free(mm_pool_temp, *mapping, map_length * sizeof(COLORANTINDEX)) ;
  *mapping = NULL ;
}

/* ----------------------------------------------------------------------- */
Bool rcbn_is_pseudo_separation( const NAMECACHE *sepname )
{
  static const uint8 *leader = (uint8*)RCB_SEPNAME_LEADER ;
  uint32 i ;

  if ( !rcbn_intercepting() )
    return FALSE ;

  for ( i = 0 ; theINLen( sepname ) > i &&  leader[ i ] != '\0' ; ++i ) {
    if ( theICList( sepname )[ i ] != leader[ i ] )
      return FALSE ;
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to iterate over all the recombine separations.
 */
RCBSEPARATION *rcbn_iterate( RCBSEPARATION *prev )
{
  if ( prev == NULL )
    return grcb.pseps ;
  else
    return prev->next ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to determine if a recombine separation is a process one
 * or not (so by implication a spot color one).
 */
Bool rcbn_sepisprocess( RCBSEPARATION *rcbs )
{
  HQASSERT( rcbs , "rcbs NULL in rcbn_sepisprocess" ) ;
  return rcbs->fprocess ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the pseudo colorant index of a recombine
    separation.
 */
COLORANTINDEX rcbn_sepciPseudo( RCBSEPARATION *rcbs )
{
  HQASSERT( rcbs , "rcbs NULL in rcbn_sepciPseudo" ) ;
  return rcbs->ciPseudo ;
}

/* ----------------------------------------------------------------------- */
/** This routine is used to return the actual name of a recombine separation.
 */
NAMECACHE *rcbn_sepnmActual( RCBSEPARATION *rcbs )
{
  HQASSERT( rcbs , "rcbs NULL in rcbn_sepnmActual" ) ;
  /* Note: nmActual may be null, 'cos we might have a composite job */
  return rcbs->nmActual ;
}


/* ----------------------------------------------------------------------- */
/** return enum values for current page status:
   RCBN_COMPOSITE_PAGE - this page is certainly a composite page
   RCBN_UNKNOWN_PAGETYPE - unknown status
                           (probably still on the first page)
   RCBN_PROB_SEPARATION_PAGE - probably a separation
                               (e.g. received a plate color comment)
   RCBN_SEPARATION_PAGE - last page was certainly a separation and
                          this page is not yet determined
                          (i.e. not yet composite)
*/
enum {
  RCBN_COMPOSITE_PAGE = 0,
  RCBN_UNKNOWN_PAGETYPE,
  RCBN_PROB_SEPARATION_PAGE,
  RCBN_SEPARATION_PAGE,

  RCBN_MAX_PAGETYPES
};

Bool recombinestatus_(ps_context_t *pscontext)
{
  /* shove the current recombine GUI tick box status on the stack together with
     the composite image state (true if composite)
  */
  OBJECT Obj = OBJECT_NOTVM_NOTHING ;
  int32 status;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  object_store_bool(&Obj, grcb.fenabled) ;

  if (!push( & Obj , & operandstack ))      /*true if recombine enabled (i.e. on GUI)*/
    return error_handler(STACKUNDERFLOW);

  /* see comment above enum defs for meanings */
  status = RCBN_COMPOSITE_PAGE ;
  if ( !sub_page_is_composite() ) {
    if (grcb.flastwasseparation && (grcb.cshowpage > 0))
      status = RCBN_SEPARATION_PAGE ;
    else if (sub_page_is_separations())
      status = RCBN_PROB_SEPARATION_PAGE ;
    else
      status = RCBN_UNKNOWN_PAGETYPE;
  }

  object_store_integer(&Obj, status) ;

  if (!push( & Obj , & operandstack ))
    return error_handler(STACKUNDERFLOW);

  return TRUE ;
}

Bool recombineshowpage_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (grcb.cshowpage > 0) {
    HQTRACE(debug_rcbcntrl ,
            ("recombineshowpage_: explicit call to output page"));
    return rcbn_showpage();
  }

  return TRUE;
}

/*
* Log stripped */
