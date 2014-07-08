/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:plotops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Preparation of devices to accept DL objects.
 */

#include "core.h"
#include "plotops.h"

#include "basemap.h"            /* free_basemap_semaphore */
#include "bitblts.h"            /* blit_t */
#include "bitblth.h"
#include "blttables.h"
#include "clipblts.h"           /* blit_clip_data_t */
#include "clipops.h"            /* clippingiscomplex */
#include "control.h"            /* handleLowMemory */
#include "display.h"            /* cclip_bbox */
#include "dl_bres.h"            /* make_nfill */
#include "dl_free.h"            /* free_fill */
#include "dl_store.h"           /* DlSSEntry */
#include "dlstate.h"            /* inputpage */
#include "fonts.h"              /* charcontext_t */
#include "genhook.h"            /* runHooks */
#include "graphict.h"           /* GSTATE */
#include "groupPrivate.h"       /* castHdlToGroup */
#include "group.h" /* groupMustComposite */
#include "gs_tag.h"             /* make_gstagstructureobject */
#include "gschead.h"            /* gsc_getcolorspace */
#include "gschtone.h"           /* gsc_getSpotno */
#include "gstate.h"             /* gsTranState */
#include "gu_path.h"            /* fl_setflat */
#include "hdlPrivate.h"         /* hdlTarget */
#include "jobmetrics.h"         /* JobStats */
#include "ndisplay.h"           /* NFILLOBJECT */
#include "pattern.h"            /* patterncheck */
#include "params.h"             /* UserParams */
#include "patternshape.h"       /* patternshape_lookup */
#include "pclAttrib.h"          /* getPclAttrib */
#include "psvm.h"               /* workingsave */
#include "rcbcntrl.h"           /* rcbn_enabled */
#include "rcbtrap.h"            /* rcbt_addtrap */
#include "routedev.h"           /* CURRENT_DEVICE */
#include "scanconv.h"           /* scanconvert_band */
#include "spdetect.h"           /* detect_setcmykcolor_separation */
#include "stackops.h"           /* saveStackPositions */
#include "surface.h"            /* dl_surface_used */
#include "system.h"             /* get_cliprec */
#include "swerrors.h"           /* error_handler*/
#include "tranState.h"          /* TsStroke */
#include "cce.h"                /* CCEModeNormal */

Bool degenerateClipping ;
int32 clipmapid = -1;

/* Text context level. Although this is statically initialised, it doesn't need
 * a function to reinitialise it after a rip restart because it should always
 * have a consistent value. */
static uint32 textContextLevel = 0;

/* ----- Static function declarations ----- */
static Bool fillclip(DL_STATE *page, render_blit_t *rb, CLIPRECORD *topcomplex) ;
static uint32 newclipmap(render_info_t *ri) ;
static Bool prepare_dl_clipping(DL_STATE *page,
                                CLIPRECORD *cliprec,
                                CLIPOBJECT * pExistingContext,
                                Bool imposition,
                                Bool fWithRecombine,
                                CLIPOBJECT **clipptr);
static Bool setup_dl_clipping(DL_STATE *page, CLIPOBJECT **clipptr) ;

static Bool getSoftMaskState(SoftMask* mask, DlSSSet* stores,
                             SoftMaskAttrib** result);

void init_C_globals_plotops(void)
{
  clipmapid = -1;
}

Bool setuserlabel_(ps_context_t *pscontext)
{
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW );
  theo = theTop( operandstack ) ;
  if ( oType( *theo ) != OBOOLEAN )
    return error_handler( TYPECHECK );
  gstateptr->user_label = (uint8)oBool( *theo );
  pop( & operandstack ) ;
  return TRUE;
}

/* See header for doc. */
void textContextEnter(void)
{
  HQASSERT(textContextLevel < MAXUINT32, "Overflowing textContextLevel");
  textContextLevel ++;
}

/* See header for doc. */
void textContextExit(void)
{
  HQASSERT(textContextLevel > 0, "textContextLevel has become negative; "
           "there must be an unmatched enter/exitContext()");
  textContextLevel --;
}


/* ----------------------------------------------------------------------------
   function:            .......(..)        author:              Andrew Cave
   creation date:       12-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   Output preparation routines.

---------------------------------------------------------------------------- */
Bool csetg(DL_STATE *page, int32 colorType, int32 options)
{
  CLIPRECORD *c_rec ;
  GSTATE *gs ;
  charcontext_t *charcontext ;
  render_blit_t *rb ;
  const surface_t *surface ;

  UNUSED_PARAM( int32 , colorType ) ;
  UNUSED_PARAM( int32 , options ) ;

  charcontext = char_current_context() ;
  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext->cptr, "No character cache") ;

  rb = charcontext->rb ;
  HQASSERT(RENDER_BLIT_CONSISTENT(rb),
           "Character caching state not consistent") ;

  gs = gstateptr ;

  /* Cast away the constness, as there's no direct access to the clip ATM.
     Note that charcontext->rb is not necessarily the same as
     &charcontext->rs.ri.rb, so we can't just change the
     charcontext->rs.ri.clip bbox. If a Type 3 character uses a recursive
     character, the inner character may be cached directly into the parent
     character's form. */
  ((render_info_t*)rb->p_ri)->clip = cclip_bbox = thegsPageClip(*gs).bounds ;

  degenerateClipping = FALSE ;
  if ( clippingisdegenerate(gs) ) {
    degenerateClipping = TRUE ;
    return TRUE ;
  }

  rb->clipmode = BLT_CLP_RECT ;
  surface = rb->p_ri->surface ;
  HQASSERT(surface, "No surface") ;

  RESET_BLITS(rb->blits,
              &surface->baseblits[BLT_CLP_NONE],
              &surface->baseblits[BLT_CLP_RECT],
              &surface->baseblits[BLT_CLP_COMPLEX]) ;

  c_rec = clippingiscomplex( gs , TRUE ) ;
  if ( c_rec ) {
    if ( theClipNo(*c_rec) != clipmapid ) {
      if ( !fillclip(page, rb, c_rec) )
        return FALSE ;
    }
    if ( clippingisdegenerate(gs) ) {
      degenerateClipping = TRUE ;
      return TRUE ;
    }
    rb->clipmode = BLT_CLP_COMPLEX ;
  } else
    clipmapid = -1 ;    /* No complex clipping */

  return TRUE ;
}


static Bool setg_test_free_mem(size_t amount, DL_STATE *page)
{
  size_t i, free_dl = 0;

  for ( i = 0; i < N_DL_POOLS; i++ )
    free_dl += mm_pool_free_size(page->dlpools[i]);
  return free_dl + mm_no_pool_size(TRUE) > amount;
}


Bool setg(DL_STATE *page, int32 colorType, int32 options)
{
  corecontext_t *context = get_core_context_interp();
  int32 patternid = INVALID_PATTERN_ID, parent_patternid = INVALID_PATTERN_ID ;
  int32 patterntype = -1 ;
  size_t retryFree = 512 * 1024, retryLastFree = 0 ;
  STATEOBJECT newstate, *dlstate ;
  int saved_dl_safe_recursion = dl_safe_recursion;

  if (colorType != GSC_UNDEFINED)
    COLORTYPE_ASSERT(colorType, "setg");

  HQASSERT(colorType != GSC_UNDEFINED ||
           (options & DEVICE_SETG_GROUP) != 0,
           "GSC_UNDEFINED only allowed for groups") ;

  /* Setup a default newstate; spotno may be overriden later. */
  newstate = stateobject_new(page->default_spot_no) ;

  if ( (options & DEVICE_SETG_GROUP) == 0 ) {
    /* Now call any procedures we should call on the first graphics object;
       note that this may potentially change the graphics state, or indeed
       call setg recursively. */
    static Bool fRecursive = FALSE;
    Bool fResult;

    if (! fRecursive) {
      fRecursive = TRUE;
      fResult = runHooks(&thegsDevicePageDict(*gstateptr), GENHOOK_StartPainting);
      fRecursive = FALSE;
      setHookStatus(GENHOOK_StartPainting, FALSE);
      if (! fResult)
        return FALSE;
    }

    if ( theICMYKDetected( workingsave ))
      if ( theICMYKDetect( workingsave ))
        if ( gsc_getcolorspace( gstateptr->colorInfo , colorType ) == SPACE_DeviceGray )
          if ( ! detect_setcmykcolor_separation(gstateptr->colorInfo))
            return FALSE ;

    if ( colorType != GSC_UNDEFINED ) {
      /* Set object's rendering intent. This is overridden for vignettes, and
         ignored for shfills, which give the Background and body
         of the shfill different intents. */
      if ( colorType != GSC_SHFILL &&
           colorType != GSC_SHFILL_INDEXED_BASE &&
           colorType != GSC_VIGNETTE ) {
        uint8 currentReproType;

        /* This is a BIG hack to get the text context right. It can be done
         * properly when the target work arrives.
         */
        if (textContextLevel > 0) {
          currentReproType = REPRO_TYPE_TEXT;
          if (!gsc_setRequiredReproType( gstateptr->colorInfo ,
                                         colorType ,
                                         currentReproType ))
            return FALSE;
        } else
          currentReproType = gsc_getRequiredReproType(gstateptr->colorInfo, colorType);

        DISPOSITION_STORE(dl_currentdisposition, currentReproType, colorType,
                          gstateptr->user_label ? DISPOSITION_FLAG_USER : 0);
      }
    } else /* HDLs have mixed dispositions, no use for USER flag. */
      DISPOSITION_STORE(dl_currentdisposition,
                        REPRO_DISPOSITION_MIXED, colorType, 0);
  } else { /* Groups have mixed dispositions, no use for USER flag. */
    DISPOSITION_STORE(dl_currentdisposition,
                      REPRO_DISPOSITION_MIXED, colorType, 0);
  }

  degenerateClipping = FALSE ;
#ifdef DEBUG_BUILD
  if ( debug_dl_skipsetg() )
    degenerateClipping = TRUE ;
#endif

  /* Here starts the creation of the DL state for this object. Partial paints
     are suppressed during the construction, so that the state is not destroyed,
     but if memory runs out, low-memory handling is performed which may partial
     paint, after which the state is recreated. After the state is created,
     partial paints are prevented until the operator that called DEVICE_SETG()
     is complete. */
  for (;;) {
    STACK_POSITIONS stackPositions;
    Bool errorFound = FALSE;

    ++dl_safe_recursion;
    saveStackPositions(&stackPositions);

    /* Find the appropriate HDL to add object for this device type and ID to */
    page->targetHdl = hdlTarget(page->currentHdl, gstateptr) ;

    if ( ! setup_dl_clipping(page, &newstate.clipstate))
      return FALSE ;

    if ( newstate.clipstate ) {
      cclip_bbox = newstate.clipstate->bounds ;
    } else {
      degenerateClipping = TRUE ;
      cclip_bbox = gstateptr->thePAGEinfo.theclip.bounds ;
    }

    if ( oType(gstateptr->theGSTAGinfo.dict) == ODICTIONARY ) {
      HQASSERT( gstateptr->theGSTAGinfo.data ,
        "Tags dictionary present but there's nowhere to put the data" ) ;
      if ( gstateptr->theGSTAGinfo.structure == NULL )  {
        if ( !make_gstagstructureobject(page, &gstateptr->theGSTAGinfo.dict,
                                        &gstateptr->theGSTAGinfo.structure) ) {
          /* We *could* be out of DL memory, it could be nasty tricks,
           * but one likely explanation is a bug in my code - paulc */
          HQFAIL( "Nasty tricks can cause this but a bug is more likely" ) ;
          return FALSE ;
        }
        if ( gstateptr->theGSTAGinfo.data[0] != gstateptr->theGSTAGinfo.structure->alloc_words ) {
          HQFAIL( "Nasty tricks can cause this but a bug is more likely" ) ;
          /* Get gstate into a consistent state before failing */
          theTags( gstateptr->theGSTAGinfo.dict ) = ONULL | LITERAL ; /* see null_ */
          gstateptr->theGSTAGinfo.structure = NULL ;
          mm_free( mm_pool_temp, gstateptr->theGSTAGinfo.data,
            gstateptr->theGSTAGinfo.data[0] * sizeof(gstateptr->theGSTAGinfo.data[0])) ;
          gstateptr->theGSTAGinfo.data = NULL ;
          return error_handler( CONFIGURATIONERROR ) ;
        }
      }
#if defined( ASSERT_BUILD )
    } else {
      HQASSERT( gstateptr->theGSTAGinfo.structure == NULL,
        "tstructure should have been set to NULL if dict is null." ) ;
#endif
    }

    /* Use a do - while loop as an error catcher */
    errorFound = TRUE;
    do {
      if ( !getTransparencyState(&gstateptr->tranState, page,
                                 colorType == GSC_STROKE,
                                 &newstate.tranAttrib) )
        break;

      if ( colorType != GSC_UNDEFINED ) {
        HQASSERT(!error_signalled_context(context->error),
                 "Should not be invoking colour chains in error condition") ;

        /* When invoking the color chains, some operators may cause the gstate
           to change. Therefore, we must not directly reference the gstate
           variables after this point. Instead, refer to the variables that have
           been nicely cached above here. We won't invoke the colour chain if
           colour is not required for this call (eg via gs_pseudo_erasepage,
           where dlc_currentcolor is already set). */
        if ( !is_pseudo_erasepage() &&
             !gsc_invokeChainSingle(gstateptr->colorInfo, colorType) )
          break;

        /* Get the late color management state - depends on gsc_invokeChainSingle. */
        if ( !getLateColorState(gstateptr, page, colorType,
                                &newstate.lateColorAttrib) )
          break;

        /* Pattern state should not be set up for groups, or the
           render loop will composite the pattern data twice.
           Must be called after gsc_invokeChainSingle.
           NB. This will likely call setg recursively. */
        if ( (options & DEVICE_SETG_GROUP) == 0 &&
             !patterncheck(page, gstateptr, colorType, &patternid,
                           &parent_patternid, &patterntype, newstate.tranAttrib) )
          break;
      }

      if ( !getPclAttrib(page, colorType == GSC_IMAGE,
                         (options & DEVICE_SETG_RETRY) == 0 &&
                         dl_safe_recursion <= 1 &&
                         retryLastFree == 0 /* first time for this object */,
                         &newstate.pclAttrib) )
        break;

      errorFound = FALSE;
    } while (FALSE);    /* End of error catcher */

    /* Retry after VMERROR.  Don't use newerror since it gets
     * cleared if the error was inside a recursive interpreter call
     * (patterncheck calls createPatternDL which calls interpreter)
     */
    if ( errorFound ) {
      if ( error_latest_context(context->error) == VMERROR ) {
        int32     actionNumber = 0 ;
        Bool free_test = FALSE;

        error_clear_context(context->error);
        /* Loop calling handleLowMemory until freed as much as we want or none */
        for (;;) {
          dl_safe_recursion = saved_dl_safe_recursion ;

          HQTRACE( debug_lowmemory,
                   ( "CALL(handleLowMemory): setg with action %d", actionNumber )) ;
          actionNumber = handleLowMemory( actionNumber, TRY_NORMAL_METHODS, NULL ) ;
          if ( actionNumber < 0 )
            return FALSE;
          if ( actionNumber == 0 ) /* Couldn't free up anything more */
            break ;
          free_test = setg_test_free_mem(retryFree, page);
          if ( free_test )
            break;
        }

        /* Retry if we have at least as much free as we tried to free last time */
        if ( retryLastFree != 0 )
          free_test = setg_test_free_mem(retryLastFree, page);
        if ( retryLastFree == 0 || free_test ) {
          retryLastFree = retryFree ;
          retryFree += retryFree ; /* Try to free twice as much next time */

          if ( !restoreStackPositions(&stackPositions, FALSE) )
            return FAILURE(FALSE);  /* stack underflow */
          continue ; /* restart outer loop */
        }
        return error_handler(VMERROR); /* Give up */
      }
      return FALSE; /* Some error has already been raised */
    }

    break ; /* Successful - end outer loop */
  }

  /* Continue preventing partial paints until the end of this operator, unless
     the operator is a group start. This is reset to the previous value by the
     interpreter loop. */
  if ( (options & DEVICE_SETG_GROUP) != 0 )
    dl_safe_recursion = saved_dl_safe_recursion;

  /* Now that we've got the colour, after possible extra loops for error
   * handling, we'll reset the reproType for fill & stroke chains. The other
   * chain types are always initialised prior to use.
   * NB. There is no central point where this can be done at the start of
   * each object.
   */
  if ( colorType != GSC_UNDEFINED ) {
    if (!gsc_resetRequiredReproType(gstateptr->colorInfo, colorType))
      return FALSE;
  }

  if ( patternid != INVALID_PATTERN_ID &&
       (dl_currentspflags(page->dlc_context) & RENDER_PATTERN) != 0 ) {
    Bool overprinting[2] = {FALSE, FALSE};

    HQASSERT(patterntype != -1, "Should have a valid patterntype by now");
    if (context->userparams->PatternOverprintOverride && patterntype == 2) {
      overprinting[GSC_FILL] = gsc_getoverprint(gstateptr->colorInfo, GSC_FILL);
      overprinting[GSC_STROKE] = gsc_getoverprint(gstateptr->colorInfo, GSC_STROKE);
    }

    /* We might have a pattern screen which is actually drawn in black or
       white, in which case it isn't to be treated as a pattern screen at
       all. This should have already been checked in
       updateHTCacheForPatternContone. */
    /** \todo @@@ TODO FIXME ajcd 2002-12-30: We shouldn't really be using
       currentGroup here, because a setgstate may have altered the target
       HDL. However, the group stack doesn't currently recognise this, and
       a pattern group will be opened with currentGroup is its parent, so
       we need to match that. We need to think about using
       groupTop(inputpage->targetHdl) for all currentGroup references, and
       allowing a tree rather than stack structure to the groups. */
    newstate.patternstate = patternObjectLookup(page->stores.pattern,
                                                patternid, parent_patternid,
                                                pageBaseMatrixId,
                                                page->currentGroup,
                                                dlc_currentcolor(page->dlc_context),
                                                overprinting,
                                                newstate.tranAttrib ) ;
    HQASSERT(newstate.patternstate,
             "Pattern found but no corresponding state created") ;
  }

  /* Pattern shapes vary independently of pattern objects; a single pattern
     object may have multiple pattern shapes.  Look at the state from the last
     setg to see if that pattern shape can be continued). */
  if ( !patternshape_lookup(page, page->currentdlstate, &newstate) )
    return FALSE ;

  /* Transparent objects should use the page's default screen, set up in
     newstate above; Override opaque objects to current gstate screen. */
  if ( tsOpaque(gsTranState(gstateptr),
                (colorType == GSC_STROKE ? TsStroke : TsNonStroke),
                gstateptr->colorInfo)
       && (page->currentGroup == NULL
           || !groupMustComposite(page->currentGroup)) )
    newstate.spotno = gsc_getSpotno(gstateptr->colorInfo);

  newstate.gstagstructure = gstateptr->theGSTAGinfo.structure ;

#ifdef METRICS_BUILD
  /* Only update the setg count when we get here; we're only interested in how
  many times we query the state cache. */
  dl_metrics()->store.setgCount++;
#endif

  /* In English:
   * If ( there is no current dlstate set up -OR-
   *      the clip in the gstate ISNT the same as the one in the dlstate -OR-
   *      what we want for the pattern ISNT the same as the dlstate -OR-
   *      the spotno in the gstate ISNT the same as the dlstate -OR-
   *      gs tags in the gstate ISNT the same as the dlstate -OR-
   *      soft mask in the gstate ISNT the same as the dlstate )
   * then make a new dlstate record.
   */
  dlstate = page->currentdlstate ;
  if ( ! dlstate ||
       newstate.clipstate != dlstate->clipstate ||
       newstate.patternstate != dlstate->patternstate ||
       newstate.patternshape != dlstate->patternshape ||
       newstate.spotno != dlstate->spotno ||
       newstate.gstagstructure != dlstate->gstagstructure ||
       newstate.tranAttrib != dlstate->tranAttrib ||
       newstate.lateColorAttrib != dlstate->lateColorAttrib ||
       newstate.pclAttrib != dlstate->pclAttrib ) {

    /* Set new DL state */
    page->currentdlstate =
      (STATEOBJECT*)dlSSInsert( page->stores.state, &newstate.storeEntry, TRUE ) ;
    if ( page->currentdlstate == NULL )
      return FALSE ;
  }

  return TRUE ;
}


Bool clipIsRectangular(CLIPOBJECT *clip)
{
  HQASSERT(clip != NULL, "No clip");
  do {
    if (clip->fill != NULL)
      return FALSE;
  } while ( (clip = clip->context) != NULL );
  return TRUE;
}


/** Create and link the DL structures for the clips happening to the current
   object. Essentially, this means chasing down the cliprecord structure for
   the current gstate (including imposition clips), creating CLIPOBJECTs and
   filling in the bress structure, bbox, fill rules and other information,
   until such point as we find a cliprecord for which this has already been
   done, and linking them together with the top clip pointing down the chain.
   The clipping form (or forms) state is tracked at render time, and
   regenerated using pointers to these CLIPOBJECTs to determine how much has
   been done. */
static Bool prepare_dl_clipping(DL_STATE *page, CLIPRECORD *cliprec,
                                CLIPOBJECT * pExistingContext,
                                Bool imposition,
                                Bool fWithRecombine,
                                CLIPOBJECT **clipptr)
{
  CLIPRECORD *previous ;
  CLIPOBJECT *context = NULL ;
  NFILLOBJECT *nfill = NULL ;
  uint8 cliptype = 0 ;
  int32 device = CURRENT_DEVICE() ;
  uint16 ncomplex = 0 ;

  int32 x1c , y1c , x2c , y2c ;

  HQASSERT(cliprec, "No current clip to dl_clipping") ;

  if ( (theClipType(*cliprec) & CLIPISDEGN) != 0 ) {
    *clipptr = NULL ;
    return TRUE ; /* Nothing to do if degenerate */
  }

  *clipptr = clipObjectLookup(page->stores.clip, cliprec->clipno,
                              pageBaseMatrixId) ;
  if ( *clipptr != NULL )
    return TRUE ; /* We've done it all already */

  HQASSERT(device != DEVICE_NULL, "null device should have degenerate clip") ;

  /* Use recursion to ensure that all previous clip objects have been set up */
  previous = cliprec->next ;
  if ( previous == NULL && ! imposition &&
       device != DEVICE_PATTERN1 && device != DEVICE_PATTERN2 &&
       pExistingContext == NULL ) {
    /* Build imposition clip list */
    previous = theClipRecord(impositionclipping) ;
    imposition = TRUE ;
  }

  x1c = theX1Clip(*cliprec) ;
  y1c = theY1Clip(*cliprec) ;
  x2c = theX2Clip(*cliprec) ;
  y2c = theY2Clip(*cliprec) ;
  if ( previous != NULL ) {     /* Check back down chain */
    /* Create clip context for previous record. */
    if ( ! prepare_dl_clipping(page, previous, pExistingContext,
                               imposition, fWithRecombine, &context) )
      return FALSE ;

    /* Check if noted as degenerate, and propagate to this record if so */
    if ( (theClipType(*previous) & CLIPISDEGN) != 0 ) {
      theClipType(*cliprec) |= CLIPISDEGN ;
      return TRUE ;
    }

    HQASSERT(context,
             "dl_clipping failed to initialise previous clip object") ;

    ncomplex = context->ncomplex ;

    if ( x1c < theX1Clip(*previous))
      theX1Clip(*cliprec) = x1c = theX1Clip(*previous) ;
    if ( y1c < theY1Clip(*previous))
      theY1Clip(*cliprec) = y1c = theY1Clip(*previous) ;

    if ( x2c > theX2Clip(*previous))
      theX2Clip(*cliprec) = x2c = theX2Clip(*previous) ;
    if ( y2c > theY2Clip(*previous))
      theY2Clip(*cliprec) = y2c = theY2Clip(*previous) ;

    if ( x1c > x2c || y1c > y2c ) {
      theClipType(*cliprec) |= CLIPISDEGN ;
      return TRUE ;
    }
  } else if (pExistingContext != NULL) {
    /* In most circumstances, we'll have the CLIPRECORD chain to work from,
       but in the case of adjusting the clipping during the postprocessing
       pass of recombine we don't have that luxury so if we're changing the
       clipping (as we do for overprinted vignette objects) we need to refer
       to the existing CLIPOBJECT from the dl object's state */
    context = pExistingContext;
    ncomplex = context->ncomplex ;

    HQASSERT (x1c >= theX1Clip(*context) &&
              y1c >= theY1Clip(*context) &&
              x2c <= theX2Clip(*context) &&
              y2c <= theY2Clip(*context),
              "cliprec's clipping is not a subset of context's");
  }

  /* We need the clip boundaries before flattening the path, in case
     parts of it are clipped out. */
  bbox_store(&cclip_bbox, x1c, y1c, x2c, y2c) ;
  HQASSERT(bbox_is_normalised(&cclip_bbox), "clip out of order(pre)") ;

  /* Rectangle has no fill, skip to clip object creation */
  if ( (theClipType(*cliprec) & CLIPISRECT) == 0 ) {
    USERVALUE flat ;
    dbbox_t bbox = { 0 } ;

    /* First of all flatten path. */
    flat = theClipFlat(*cliprec) ;

    /* flat is negative if the fill is already prepared and present in the
       clip record. This will only happen when the clip is artificial -
       provided as a side effect of vignette detection and applied to prevent
       problems with overprinting and color management */
    if (flat <  0.0) {
      nfill = cliprec->u.nfill;
    } else {
      if ( flat == 0.0f ) {
        flat = theFlatness(theLineStyle(*gstateptr)) ;
        HQASSERT(flat >= 0.0f, "Gstate flatness negative") ;
        theClipFlat(*cliprec) = flat ;
      }
      fl_setflat( flat ) ;

      if ( !make_nfill(page, thePath(theClipPath(*cliprec)), NFILL_ISCLIP,
                       &nfill) )
        return FALSE ;
    }

    /* If pExistingContext is not NULL, we are supplying a ready made fill,
       and the globals which control the fill are not valid. */
    if ( nfill == NULL ) {
      theClipType(*cliprec) |= CLIPISDEGN ;
      return TRUE ;
    } else {
      Bool clipped;

    /* Do BB calculation and degenerate detection a' la addnbressfill.
       Note that the memory used by the nfill may be wasted, but we can't
       just chuck it away in case it was shared with another clip. */

      bbox_nfill(nfill, &cclip_bbox, &bbox, &clipped);
      if ( clipped ) {
        if ( flat >= 0.0f ) /* Did we just create that fill above? */
          free_fill(nfill, page) ;
        theClipType(*cliprec) |= CLIPISDEGN ;
        return TRUE ;
      }
    }

    cliptype = theClipType(*cliprec) ;
    ncomplex += 1 ;     /* Add this fill to number of complex clips */

    /* Add nfill to nfill cache */
    {
      NFILLCACHE nfillcache ;
      NFILLCACHE* cached ;

      nfillcache.nfill = nfill ;

      cached = (NFILLCACHE*)dlSSInsert( page->stores.nfill,
                                        &nfillcache.storeEntry, TRUE );
      if (cached == NULL) {
        return error_handler( VMERROR ) ;
      }
      nfill = cached->nfill;

      /* Dispose of this fill if we found a different one in the cache */
      if ( nfill != nfillcache.nfill ) {
        /* Free our copy of this fill if used for clipping solely.
           A vignette may use the nfill as a fill and a clip */
        if ( pExistingContext == NULL )
          free_fill( nfillcache.nfill , page ) ;
      } else if (fWithRecombine) {
        /* Else add recombine fuzzy trap info (if required). */
        PATHINFO *tmppath = &theClipPath(*cliprec) ;
        (void)path_bbox(tmppath, NULL, BBOX_IGNORE_ALL|BBOX_SAVE) ;

        if ( !rcbt_addtrap(page->dlpools, nfill, tmppath, FALSE /* fDonut */, NULL) )
          return FALSE ;
      }
    }

    /* Reduce the clip area to exactly the area touched by the path. */
    if ( (cliptype & CLIPINVERT) == 0 ) {
      theX1Clip(*cliprec) = x1c = bbox.x1 ;
      theY1Clip(*cliprec) = y1c = bbox.y1 ;
      theX2Clip(*cliprec) = x2c = bbox.x2 ;
      theY2Clip(*cliprec) = y2c = bbox.y2 ;

      HQASSERT( x1c <= x2c , "x clip out of order(pst)" ) ;
      HQASSERT( y1c <= y2c , "y clip out of order(pst)" ) ;
    }

    if ( !dl_reserve_band(page, RESERVED_BAND_CLIPPING) )
      return FALSE ;

    if ( device == DEVICE_PATTERN1 || device == DEVICE_PATTERN2 ) {
      if ( ncomplex > 1 &&     /* Fourth band for nested pattern clipping */
           !dl_reserve_band(page, RESERVED_BAND_PATTERN_CLIP) )
        return FALSE ;
    }
  }

  /* We need to create a CLIPOBJECT for the clip, and fill in the relevant
     info */
  {
    CLIPOBJECT theclip;

    theX1Clip(theclip) = x1c ;
    theY1Clip(theclip) = y1c ;
    theX2Clip(theclip) = x2c ;
    theY2Clip(theclip) = y2c ;
    theclip.context = context ;
    theclip.fill = nfill ;
    theclip.rule = cliptype ;
    theclip.ncomplex = ncomplex ;
    theclip.clipno = cliprec->clipno ;
    theclip.pagebasematrixid = pageBaseMatrixId ;

    /* Insert clip object into clip cache. */
    *clipptr = (CLIPOBJECT*)dlSSInsert(page->stores.clip,
                                       &theclip.storeEntry, TRUE) ;
    if ( *clipptr == NULL ) {
      return error_handler(VMERROR) ;
    }
  }

  return TRUE ;
}

/**
 * Set up a CLIPOBJECT from the given bbox, rather than using the current gstate
 * clipping.
 */
Bool setup_rect_clipping(DL_STATE *page, CLIPOBJECT **clipping, dbbox_t *bbox)
{
  CLIPOBJECT clipobj;

  bbox_load(bbox, theX1Clip(clipobj), theY1Clip(clipobj),
            theX2Clip(clipobj), theY2Clip(clipobj));
  clipobj.context = NULL;
  clipobj.fill = NULL;
  clipobj.rule = (uint8)(ISCLIP | CLIPISRECT);
  clipobj.ncomplex = 0;
  clipobj.clipno = ++clipid;
  clipobj.pagebasematrixid = pageBaseMatrixId;

  /* Insert clip object into clip cache. */
  *clipping = (CLIPOBJECT*)dlSSInsert(page->stores.clip,
                                      &clipobj.storeEntry, TRUE);
  return *clipping != NULL;
}

/* ---------------------------------------------------------------------- */
/** setup_dl_clipping prepares the clip nodes for adding objects to the DL. It
   calls prepare_dl_clipping to recurse down the gstate clip record
   structures, filling in the cliprects and connecting them together on the
   way back up. These cliprects are used at render time when regenerating the
   clipping form. */
static Bool setup_dl_clipping(DL_STATE *page, CLIPOBJECT **clipptr)
{
  return prepare_dl_clipping(page, theClipRecord(thegsPageClip(*gstateptr)),
                             NULL, FALSE, rcbn_enabled(), clipptr) ;
}

/* ---------------------------------------------------------------------- */
/** setup_vignette_clipping is similar to setup_dl_clipping, except the clip
   record is fabricated from an existing fill (an element of a vignette)
   rather than coming from the clip path in the graphics state. Each element
   of a vignette can be clipped with respect to its following neigbouring
   elements without changing the visual effect with the normal opaque
   painting model, but if we don't do this when maxblts are on, then outer
   parts of radial and other enclosed vignettes show through the interior
   parts. */

Bool setup_vignette_clipping(DL_STATE *page, DLREF * dlobj)
{
  Bool fResult = TRUE;
  CLIPRECORD * pClipRec, * pPreviousClipRec;
  LISTOBJECT * lobj, * previousLobj;
  CLIPOBJECT *clipobj ;

  HQASSERT(dlobj != NULL, "empty vignette chain");

  previousLobj = dlref_lobj(dlobj);
  pPreviousClipRec = NULL;
  pClipRec = NULL;

#define return DO_NOT_RETURN__break_FROM_LOOP_INSTEAD!
  for (dlobj = dlref_next(dlobj); dlobj != NULL; dlobj = dlref_next(dlobj)) {
    lobj = dlref_lobj(dlobj);

    pClipRec = get_cliprec(mm_pool_temp);
    fResult = pClipRec != NULL;
    if (! fResult)
      break;

    /* populate the clipping record */

    switch ( previousLobj->opcode ) {
    case RENDER_rect: {
      /* Because we have to do inverted clipping, not ordinary clipping, we
         can't simply use the rectangles like an orthogonal rectclip.
         Therefore we have to convert the rectangle to a path and use that
         instead. The path will be converted to a NFILLOBJECT in the usual
         way */
      int32 x1, y1, x2, y2;

      x1 = previousLobj->bbox.x1;
      y1 = previousLobj->bbox.y1;
      x2 = previousLobj->bbox.x2;
      y2 = previousLobj->bbox.y2;

      path_fill_four(x1, y1, x1, y2, x2, y2, x2, y1) ;

      pClipRec->cliptype = (uint8) (ISCLIP | CLIPINVERT | NZFILL_TYPE);
      pClipRec->u.clippath = i4cpath; /* struct copy */
      pClipRec->clipflat = 0.0; /* use gstate flatness, not that it matters */
      break;
    }

    case RENDER_fill:
      /* for fills, we can just use the original nfill object directly from
       * the previous ordinary fill */

      pClipRec->cliptype = (uint8) (ISCLIP | CLIPINVERT |
                                    (previousLobj->dldata.nfill->type & CLIPRULE));
      pClipRec->u.nfill = previousLobj->dldata.nfill;
      pClipRec->clipflat = -1.0; /* indicating that the union in the
                                    structure is occupied by a nfill rather
                                    than a clippath */
      break;

    case RENDER_quad: {
      /* Build an NFILL, and copy it. */
      NFILLOBJECT quadfill;
      NBRESS quadthreads[4];
      NFILLOBJECT *nfill;

      quad_to_nfill(previousLobj, &quadfill, &quadthreads[0]);

      if ( (nfill = nfill_copy(page, &quadfill)) == NULL )
        fResult = FALSE;
      else {
        pClipRec->cliptype = (uint8)(ISCLIP | CLIPINVERT |
                                     (nfill->type & CLIPRULE));
        pClipRec->u.nfill = nfill;
        pClipRec->clipflat = -1.0; /* union is an nfill */
      }
      break;
    }

    default:
      HQFAIL("Vignette element not rect/fill/quad") ;
      fResult = FALSE;
      break;
    }

    if ( !fResult )
      break;

    /* cclip_bbox is revised by prepare_dl_clipping */
    HQASSERT(lobj->objectstate->clipstate != NULL,
             "NULL clipstate encountered in setup_vignette_clipping");
    pClipRec->bounds = lobj->objectstate->clipstate->bounds ;

    pClipRec->refcount = 1;
    pClipRec->clipno = ++clipid;

    pClipRec->next = pPreviousClipRec;

    fResult = prepare_dl_clipping(page, pClipRec, lobj->objectstate->clipstate,
                                  FALSE, FALSE, &clipobj);
    if (! fResult)
      break;

    /* make a new object state to carry this new clip state (pattern and
       screen are unchanged form the original), and replace the original */
    {
      STATEOBJECT state = * (lobj->objectstate);

      HQASSERT((pClipRec->cliptype & CLIPISDEGN) == 0,
               "degenerate clipping encountered in setup_vignette_clipping");
      if ((pClipRec->cliptype & CLIPISDEGN) != 0) {
        /* cope with the unusual case of having a degenerate clipping, which
           for inverse clipping means it should be treated as not be there at
           all */
        HQASSERT(clipobj == NULL, "degenerate clip, but clipobj not NULL");
        free_cliprec(pClipRec, mm_pool_temp);
        pClipRec = pPreviousClipRec;
        if (pClipRec == NULL)
          state.clipstate = lobj->objectstate->clipstate;
        else {
          state.clipstate = clipObjectLookup(page->stores.clip,
                                             pClipRec->clipno,
                                             pageBaseMatrixId) ;
          HQASSERT(state.clipstate != NULL,
                   "No clip object for previous clip record") ;
        }
      } else {
        /* the normal case */
        state.clipstate = clipobj;
      }

      lobj->objectstate = (STATEOBJECT*)dlSSInsert(page->stores.state,
                                                   &state.storeEntry, TRUE);
    }
    if (lobj->objectstate == NULL) {
      fResult = FALSE;
      break;
    }

    /* and go on to the next vignette element */

    pPreviousClipRec = pClipRec;
    previousLobj = lobj;
  }

  /* discard the chain of clip records we built up */

  while (pClipRec != NULL) {
    pPreviousClipRec = pClipRec->next;
    free_cliprec (pClipRec, mm_pool_temp);
    pClipRec = pPreviousClipRec;
  }

#undef return
  return fResult;
}

/* Augment the clipping in lobj with nfillclip. */
Bool augment_clipping(DL_STATE *page, LISTOBJECT *lobj, NFILLOBJECT *nfillclip,
                      dbbox_t *bboxclip)
{
  Bool result = FALSE;
  CLIPRECORD * pClipRec;
  CLIPOBJECT *clipobj ;
  dbbox_t bbox ;

  pClipRec = get_cliprec(mm_pool_temp);
  if ( !pClipRec )
    return FALSE;

#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  /* populate the clipping record */
  pClipRec->cliptype = (uint8)(ISCLIP |
                               (nfillclip == NULL
                                ? CLIPISRECT
                                : (nfillclip->type & CLIPRULE))) ;
  pClipRec->u.nfill = nfillclip;
  pClipRec->clipflat = -1.0; /* indicating that the union in the structure is
                                occupied by a nfill rather than a clippath */

  /* We need to compensate for clip_to_bounds() shrinking the clip box when
     tesselating scan conversion. */
  bbox = *bboxclip ;
  if ( page->ScanConversion == SC_RULE_TESSELATE ) {
    if ( bbox.x2 < page->page_w - 1 )
      bbox.x2 += 1 ;
    if ( bbox.y2 < page->page_h - 1 )
      bbox.y2 += 1 ;
  }

  /* cclip_bbox is revised by prepare_dl_clipping */
  HQASSERT(lobj->objectstate->clipstate != NULL,
           "NULL clipstate encountered in augment_clipping");
  bbox_intersection(&lobj->objectstate->clipstate->bounds, &bbox,
                    &pClipRec->bounds /* destination */);

  pClipRec->refcount = 1;
  pClipRec->clipno = ++clipid;

  pClipRec->next = NULL;

  if ( !prepare_dl_clipping(page, pClipRec, lobj->objectstate->clipstate,
                            FALSE, FALSE, &clipobj) )
    goto cleanup;

  /* make a new object state to carry this new clip state (pattern and
     screen are unchanged form the original), and replace the original */
  {
    STATEOBJECT state = * (lobj->objectstate);

    HQASSERT((pClipRec->cliptype & CLIPISDEGN) == 0,
             "degenerate clipping encountered in augment_clipping");
    if ((pClipRec->cliptype & CLIPISDEGN) != 0) {
      /* cope with the unusual case of having a degenerate clipping, which
         for inverse clipping means it should be treated as not be there at
         all */
      HQASSERT(clipobj == NULL, "degenerate clip, but clipobj not NULL");
      free_cliprec(pClipRec, mm_pool_temp);
      state.clipstate = lobj->objectstate->clipstate;
      HQASSERT(state.clipstate != NULL, "No clip object for clip record") ;
    } else {
      /* the normal case */
      state.clipstate = clipobj;
    }

    lobj->objectstate = (STATEOBJECT*)dlSSInsert(page->stores.state,
                                                 &state.storeEntry, TRUE);
    if ( !lobj->objectstate )
      goto cleanup;
  }

  result = TRUE;
 cleanup:
  /* discard the clip record */
  if ( pClipRec )
    free_cliprec(pClipRec, mm_pool_temp);

#undef return
  return result;
}

/** Construct a \c CLIPOBJECT chain on the stack representing the \c
    CLIPRECORD chain, and call \c regenerate_clipping() when at the bottom of
    the chain. */
static Bool fillclip_recurse(DL_STATE *page, render_info_t *p_ri,
                             CLIPRECORD *rec, int32 clipmapid,
                             CLIPOBJECT *clipobject, CLIPOBJECT *topclip)
{
  HQASSERT(clipobject != NULL, "No current clip object") ;
  HQASSERT(topclip != NULL, "No top clip object") ;

  for (;;) {
    Bool result ;

    if ( rec == NULL || rec->clipno == clipmapid ) {
      render_tracker_t *renderTracker = p_ri->p_rs->cs.renderTracker ;
      uint32 sema = 0 ;

      HQASSERT(clipobject != topclip,
               "Should have had at least one complex clip record") ;

      /* We reached the record representing the current state of the
         front-end clipping. The previous clip record links to clipobject, so
         we need to make it usable. We'll turn it into a rectangular clip the
         same size as the top clip, and put the clipmapid into it, so it's
         used as a stop record by regenerate_clipping(), but doesn't
         contribute to the filled mask. */
      clipobject->bounds = topclip->bounds ;
      clipobject->context = NULL ;
      clipobject->fill = NULL ;
      clipobject->ncomplex = 0 ;
      clipobject->rule = CLIPISRECT ;
      clipobject->clipno = clipmapid ;
      clipobject->pagebasematrixid = PAGEBASEID_INVALID ;

      if ( rec == NULL ) {
        /* Ran out of clips because we reached the end of the CLIPRECORD
           chain. The clipping form we are going to use was inserted in the
           character's render state by render_state_mask, but it has no form
           memory associated with it. If this is the first clip, allocate a
           clip map from the basemap. clipmapid is used to determine if the
           basemap data is damaged, and needs repairing. */
        renderTracker->clipping = CLIPPING_firstcomplex ;
        renderTracker->clippingformstate = NULL ;

        if ( (sema = newclipmap(p_ri)) == 0 )
          return error_handler(VMERROR) ;
      } else {
        renderTracker->clipping = CLIPPING_complex ;
        renderTracker->clippingformstate = clipobject ;
      }

      result = regenerate_clipping(p_ri, topclip) ;

      if ( sema != 0 )
        free_basemap_semaphore(sema) ;

      return result ;
    } else if ( (theClipType(*rec) & CLIPISRECT) == 0 ) {
      /* Construct a clip object for the complex clip record. */
      CLIPOBJECT next_clipobject ;
      USERVALUE flat ;

      /* Don't bother shrinking the bounds to the path. */
      clipobject->bounds = rec->bounds ;
      clipobject->context = &next_clipobject ;
      clipobject->rule = rec->cliptype ;
      clipobject->clipno = rec->clipno ;
      clipobject->ncomplex = 1 ;
      clipobject->pagebasematrixid = PAGEBASEID_INVALID ;

      flat = theClipFlat(*rec) ;
      if ( flat == 0.0f )
        flat = theFlatness(theLineStyle(*gstateptr)) ;
      fl_setflat(flat) ;

      /* Not going to DL, don't allocate extra nodes */
      if ( !make_nfill(page, thePath(theClipPath(*rec)), NFILL_ISCLIP,
                       &clipobject->fill) )
        return FALSE ;

      result = fillclip_recurse(page, p_ri, rec->next,
                                clipmapid, &next_clipobject, topclip) ;

      free_fill(clipobject->fill, page) ;

      return result ;
    } else {
      /* Rect clip; skip this record, an overriding complex will be further
         resticted. */
      rec = rec->next ;
    }
  }
}

/** Do the fills on the clipping bit map for all the current  clipping paths,
   using the appropriate rule as indicated in the clip record.  (The fill is
   with respect to their intersections). */
static Bool fillclip(DL_STATE *page, render_blit_t *rb, CLIPRECORD *topcomplex)
{
  Bool result = FALSE ;
  render_state_t clip_rs ;
  render_tracker_t renderTracker ;
  const clip_surface_t *clip_surface ;

  HQASSERT(CURRENT_DEVICE() == DEVICE_CHAR, "fillclip not in csetg") ;

  HQASSERT(topcomplex, "No cliprecord in fillclip") ;
  HQASSERT((theClipType(*topcomplex) & CLIPISRECT) == 0,
           "Top cliprecord not complex in fillclip") ;

  /* We need the blit state to be consistent (the blit pointer refers to the
     render info's blit sub-structure) in order for the render state copy to
     be correct. */
  HQASSERT(RENDER_BLIT_CONSISTENT(rb) &&
           RENDER_BLIT_COPY_INFO(rb) &&
           RENDER_INFO_COPY_STATE(rb->p_ri),
           "Character caching state not consistent") ;
  RS_COPY_FROM_RS(&clip_rs, rb->p_ri->p_rs) ;

  /* Extract the clip surface */
  clip_surface = clip_rs.ri.surface->clip_surface ;

  clip_rs.cs.renderTracker = &renderTracker ;
  render_tracker_init(&renderTracker) ;

  if ( clip_surface->context_begin == NULL ||
       (*clip_surface->context_begin)(clip_rs.surface_handle, &clip_rs.ri) ) {
    CLIPOBJECT top_clip ;

    result = fillclip_recurse(page, &clip_rs.ri, topcomplex, clipmapid,
                              &top_clip, &top_clip) ;

    /** \todo Call surface for rect clip over topcomplex? */

    /** \todo Shouldn't the clipped content be inside the context? */

    if ( clip_surface->context_end )
      (*clip_surface->context_end)(clip_rs.surface_handle, &clip_rs.ri, FALSE) ;

    /* update the status of the current clipmap */
    clipmapid = theClipNo(*topcomplex) ;
  }

  return result ;
}

/** This function obtains & establishes a new front-end clipping map. */
static uint32 newclipmap(render_info_t *ri)
{
  FORM *clipmapform ;
  void *map ;
  uint32 size, sema ;
  render_forms_t *forms ;

  HQASSERT(RENDER_INFO_CONSISTENT(ri), "Render info not self-consistent") ;
  HQASSERT(ri->rb.outputform, "No output form") ;

  forms = ri->p_rs->forms ;
  HQASSERT(forms, "Nowhere to put working lines") ;

  /* Make the clipping form the same size and layour as the outputform, but
     set the address to the basemap memory. */
  forms->clippingform = *ri->rb.outputform ;
  ri->rb.clipform = clipmapform = &forms->clippingform ;

  ri->rb.ymaskaddr = NULL ;

  sema = get_basemap_semaphore(&map, &size) ;
  theFormA(*clipmapform) = map ;
  theFormT(*clipmapform) = FORMTYPE_BANDBITMAP ;

  forms->clippingbase = BLIT_ADDRESS(map, theFormS(*clipmapform)) ;
  /* Halftonebase is abused by spanlist clipping when converting to bitmap */
  forms->halftonebase = BLIT_ADDRESS(forms->clippingbase, theFormL(*clipmapform)) ;

  HQASSERT((uint32)(theFormS(*clipmapform) + 2 * theFormL(*clipmapform)) <= size,
           "Not enough space in basemap for clipform and two spare lines") ;

  return sema ;
}

/** Get the DL's representation of the current soft mask settings.
*/
static Bool getSoftMaskState(SoftMask* mask, DlSSSet* stores,
                             SoftMaskAttrib** result)
{
  SoftMaskAttrib maskAttrib;

  HQASSERT(mask != NULL && stores != NULL && result != NULL,
           "getSoftMaskState - parameters cannot be NULL");

  /* Initialise result in case of early return. */
  *result = NULL;

  if (mask->type == EmptySoftMask)
    return TRUE;    /* Note that this is not an error. */

  /* Initialize the SoftMaskAttrib structure and get a stored instance. */
  maskAttrib.type = mask->type;
  maskAttrib.group = NULL ;

  /* Find the HDL stored for this softmask */
  if ( mask->groupId != HDL_ID_INVALID ) {
    HDL *maskhdl = hdlStoreLookup(stores->hdl, mask->groupId);

    if ( maskhdl == NULL ) {
      HQFAIL("No softmask in store for group ID") ;
      return FALSE ;
    }

    maskAttrib.group = hdlGroup(maskhdl);
    HQASSERT(maskAttrib.group != NULL, "Softmask ID is not for a group") ;
  }

  /* Get the stored version. */
  *result = (SoftMaskAttrib*)dlSSInsert(stores->softMask,
                                        &maskAttrib.storeEntry, TRUE);

  /* The store insert returning NULL means there was an error. */
  if (*result == NULL)
    return FALSE;

  return TRUE;
}

/** Get the DL's representation of the current transparency settings.
*/
Bool getTransparencyState(TranState* ts,
                          DL_STATE *page,
                          Bool useStrokingAlpha,
                          TranAttrib** result)
{
  USERVALUE alpha;
  TranAttrib tranAttrib;
  Bool alphaIsShape;
  uint32 blendMode = ts->blendMode;

  HQASSERT(ts != NULL && page != NULL, "parameters cannot be NULL");

  *result = NULL ;

  /* Initialize the TranAttrib structure and get a stored instance. */
  /* Note that the TranAttrib structure keeps small values and bools in small
     containers to save space. */
  alphaIsShape = ts->alphaIsShape;

  /* Drop AIS alphas outside knockout groups. */
  if ( alphaIsShape &&
       !groupGetAttrs(page->currentGroup)->knockoutDescendant )
    /* The alpha-is-shape part of the compositing formula cancels out. */
    alphaIsShape = FALSE;

  if ( !getSoftMaskState(&ts->softMask, &page->stores, &tranAttrib.softMask) )
    return FALSE;

  /* May need to override a non-separable blend mode if the blend space is a
     non-standard device space. */
  HQASSERT(page->currentGroup != NULL || ts->blendMode == CCEModeNormal,
           "Page group missing for non-normal blend mode");
  if ( page->currentGroup != NULL )
    blendMode = groupOverrideBlendMode(page->currentGroup, ts->blendMode);

  /* Convert the alpha to a fixed point representation. */
  alpha = tsConstantAlpha(ts, useStrokingAlpha);
  tranAttrib.alpha = FLOAT_TO_COLORVALUE(alpha);
  tranAttrib.blendMode = CAST_UNSIGNED_TO_UINT8(blendMode);
  tranAttrib.alphaIsShape = (uint8)alphaIsShape;

  *result = (TranAttrib*)dlSSInsert(page->stores.transparency,
                                    &tranAttrib.storeEntry, TRUE);
  return *result != NULL;
}

/** Get the DL's representation of the current late color management settings.
*/
Bool getLateColorState(GSTATE* gs,
                       DL_STATE* page,
                       int32 colorType,
                       LateColorAttrib** result)
{
  HQASSERT(gs != NULL && page != NULL,
           "getLateColorState - parameters cannot be NULL");

  *result = NULL ;

  if ( colorType != GSC_UNDEFINED ) {
    LateColorAttrib lateColorAttrib;
    Bool independentChannels;

    /* Record the object's original color space and overprint mode; if the job
       turns out to be composite the original space and overprint mode value is
       needed to ensure correct overprinting and color management following
       recombine interception.
       NB. The renderingIntent and origColorModel are set for all groups, but
           only the values for group objects are used in compositing. */
    lateColorAttrib.origColorModel = gsc_getColorModel(gs->colorInfo, colorType);
    lateColorAttrib.overprintMode = CAST_SIGNED_TO_UINT8(gsc_getoverprintmode(gs->colorInfo));
    if ( gsc_getAdobeRenderingIntent(gs->colorInfo) )
      lateColorAttrib.renderingIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    else
      lateColorAttrib.renderingIntent = gsc_getICCrenderingintent(gs->colorInfo);
    lateColorAttrib.blackType = dl_currentblacktype(page->dlc_context);
    if (!gsc_hasIndependentChannels(gs->colorInfo, colorType, FALSE,
                                    &independentChannels))
      return FALSE;
    lateColorAttrib.independentChannels = CAST_UNSIGNED_TO_UINT8(independentChannels);
#ifdef METRICS_BUILD
    {
      OBJECT colorSpace;
      lateColorAttrib.is_icc = FALSE ;

      if (gsc_currentcolorspace(gs->colorInfo, colorType, &colorSpace)) {
        lateColorAttrib.is_icc = (uint8)dl_transparency_is_icc_colorspace(&colorSpace) ;
      }
    }
#endif
    *result = (LateColorAttrib*)dlSSInsert(page->stores.latecolor,
                                           &lateColorAttrib.storeEntry, TRUE);

    if ( *result == NULL )
      return FALSE ;
  }

  return TRUE ;
}

/*
Log stripped */
