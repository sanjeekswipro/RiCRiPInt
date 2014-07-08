/** \file
 * \ingroup gstack
 *
 * $HopeName: SWv20!src:gstack.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphics stack manipulation.
 */

#include "core.h"
#include "coreinit.h"

#include "clipops.h"            /* imposition_update_rectangular_clipping */
#include "gcscan.h"             /* gs_scan */
#include "gschtone.h"           /* gsc_getSpotno,gsc_regeneratehalftoneinfo */
#include "gstate.h"             /* gs_storedashlist */
#include "gu_chan.h"            /* guc_updateRealAndVirtualColorants */
#include "gu_ctm.h"             /* NEWCTM_ALLCOMPONENTS */
#include "gu_path.h"            /* path_init */
#include "halftone.h"           /* ht_checkifchentry */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "idlom.h"              /* IDLOM_CLIP */
#include "namedef_.h"           /* NAME_* */
#include "params.h"             /* SystemParams */
#include "rcbcntrl.h"           /* rcbn_intercepting */
#include "routedev.h"           /* SET_DEVICE */
#include "system.h"             /* path_free_list */
#include "swerrors.h"           /* LIMITCHECK */
#include "matrix.h"             /* identity_matrix, MATRIX_COPY */
#include "vndetect.h"           /* flush_vignette */

#include "gstack.h"


GSTATE *gstateptr = NULL ;
GSTATE *gstackptr = NULL ;
GSTATE *grframes = NULL ;

static mps_root_t gstackRoot;
static mps_root_t grframeRoot;


int32   gstateId = 0 ;

static int32   gstackcount = 0 ;

static GSTATE *alloc_gstate( void ) ;


/** File runtime initialisation */
static void init_C_globals_gstack(void)
{
  gstateptr = NULL ;
  gstackptr = NULL ;
  grframes = NULL ;
  gstackRoot = NULL ;
  grframeRoot = NULL ;
  gstateId = 0 ;
  gstackcount = 0 ;
}

/** Garbage collection scanning function for GSTATE stacks. */
static mps_res_t MPS_CALL gs_scan_stack(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res = MPS_RES_OK;
  GSTATE *gs;
  size_t dummy;

  UNUSED_PARAM( size_t, s );
  HQASSERT(p != NULL, "Invalid gstate stack base");
  gs = *(GSTATE **)p;
  while ( gs != NULL && res == MPS_RES_OK ) {
    res = gs_scan( &dummy, ss, gs );
    gs = gs->next;
  }
  return res;
}


/* ---------------------------------------------------------------------------*/
/** Set up the first gstate. The remainder of the gstack set to empty. */
static Bool gstack_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (gstateptr = alloc_gstate()) == NULL )
    return FALSE ;

  gstateptr->gId   = ++gstateId ;
  gstateptr->gType = GST_CURRENT ;
  gstateptr->next  = gstackptr = NULL ;
  gstateptr->theHDLTinfo.next = NULL ;

  thegsDeviceStrokeAdjust(*gstateptr) = FALSE;

  thegsDevicePageDict(*gstateptr) = onull; /* Struct copy to set slot properties */
  gstateptr->thePDEVinfo.initcliprec = NULL ;
  thegsDistillID(*gstateptr) = 0 ;

  MATRIX_COPY( & theFontATMTRM( *gstateptr )  , & identity_matrix ) ;

  path_init(&thePathInfo(*gstateptr)) ;

  gstateptr->theGSTAGinfo.dict = onull ; /* Struct copy to set slot properties */
  gstateptr->theGSTAGinfo.structure = NULL ;
  gstateptr->theGSTAGinfo.data = NULL ;

  gstateptr->theFONTinfo.fontfns = &font_invalid_fns ;

  if ( mps_root_create(&gstackRoot, mm_arena, mps_rank_exact(),
                       0, gs_scan_stack, &gstateptr, 0) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  if ( mps_root_create(&grframeRoot, mm_arena, mps_rank_exact(),
                       0, gs_scan_stack, &grframes, 0) != MPS_RES_OK ) {
    mps_root_destroy(gstackRoot);
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}


static void gstack_finish( void )
{
  mps_root_destroy(grframeRoot);
  mps_root_destroy(gstackRoot);
}


void gs_updatePtrs(GSTATE *gs)
{
  HQASSERT(gs != NULL, "gs NULL");

  gs->colorInfo = (GS_COLORinfo *) (gs + 1);
}


/* ---------------------------------------------------------------------------*/
/** Allocate a new gstate.
 *
 * This should only be called for gstates to be created on the gstack, since we
 * maintain a count of the gstack size in case we wish to limit the number of
 * active gsaves.
 */
static GSTATE *alloc_gstate( void )
{
  GSTATE *gs ;

  gs = mm_alloc( mm_pool_temp, gstate_size(NULL), MM_ALLOC_CLASS_GSTATE );
  if (gs == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  HqMemZero(gs, gstate_size(NULL));
  gs->colorInfo = (GS_COLORinfo *) (gs + 1);
  gstackcount++;
  return gs;
}


/** Free a gstate.
 *
 * This should only be called for gstates to be removed from the gstack, since
 * we maintain a count of the gstack size in case we wish to limit the number of
 * active gsaves.
 */
void free_gstate( GSTATE *gs )
{
  HQASSERT( gs, "free_gstate: gs is NULL" ) ;
  gstackcount-- ;
  mm_free( mm_pool_temp , ( mm_addr_t )gs , gstate_size(NULL));
}


/* ---------------------------------------------------------------------------*/
/** Discards any dynamic allocations in this gstate because we're about
 * to copy into it, or free it.
 */
void gs_discardgstate( GSTATE *gs )
{
  HQASSERT( gs , "gs NULL in gs_freegstate" ) ;

  path_free_list(thePath(thePathInfo(*gs)), mm_pool_temp) ;

  gs_freecliprec(&theClipRecord(thegsPageClip(*gs))) ;
  gs_freecliprec(&gs->thePDEVinfo.initcliprec) ;
  gs_freeclippath(&theClipStack(thegsPageClip(*gs))) ;

  gsc_freecolorinfo( gs->colorInfo ) ;

  (void)gs_storedashlist( &gs->thestyle, NULL, 0 ) ;

  tsDiscard( & gs->tranState ) ;

  if ( gs->theGSTAGinfo.data ) {
    /* Element zero is size in words */
    mm_free( mm_pool_temp, gs->theGSTAGinfo.data,
             gs->theGSTAGinfo.data[0] * sizeof(gs->theGSTAGinfo.data[0]) ) ;
  }
}

/* ---------------------------------------------------------------------------*/
static Bool gs_cflush( CLIPPATH *clippath )
{
  CLIPPATH *pageclip = &thegsPageClip(*gstateptr) ;
  int32 oldclipno , newclipno ;

  HQASSERT( clippath , "clippath NULL in gs_gflush" ) ;

  oldclipno = theClipRecord(*pageclip) ?
    theClipNo(*theClipRecord(*pageclip)) : -1 ;
  newclipno = theClipRecord(*clippath) ?
    theClipNo(*theClipRecord(*clippath)) : -1 ;

  if ( oldclipno != newclipno )
    if ( ! flush_vignette( VD_GRestore ))
      return FALSE ;

  return TRUE ;
}

/* ---------------------------------------------------------------------------*/
/** \brief Save clipping details in case HDLT rejects a clip change.

    \param[in] installing    The clipping chain that is being installed in the
                             gstate.
    \param[in] stackifreject The clipping stack to put in the saved clipping
                             details.
    \param[in] clippath_old  Where to store the saved clipping details.

    \retval TRUE  If the clipping details were saved, and gs_hdlt_cliprestore()
                  should be called.
    \retval FALSE If the clipping details were not saved.

    The clipping stack stored in the old clip details is passed through to this
    function explicitly. When HDLT rejects a cliprestore, the top item from the
    clipping stack is still removed from the stack, even though the current
    clipping path is not changed. However, when a setgstate clip is rejected,
    the current clipping path and clip stack are preserved.

    \todo ajcd 2012-04-03: This seems like it could be a problem, if the
    device clip record for the current gstate is not the same as the new
    gstate's device clip record. */
static Bool gs_hdlt_clipchanges(/*@null@*/ /*@in@*/ CLIPRECORD *installing,
                                /*@null@*/ /*@in@*/ CLIPPATH *stackifreject,
                                /*@notnull@*/ /*@out@*/ CLIPPATH *clippath_old)
{
  CLIPPATH *pageclip = &thegsPageClip(*gstateptr) ;
  int32 oldclipno , newclipno ;

  HQASSERT(clippath_old, "No clippath to save state in" ) ;

  oldclipno = theClipRecord(*pageclip) ?
    theClipNo(*theClipRecord(*pageclip)) : -1 ;
  newclipno = installing ? theClipNo(*installing) : -1 ;

  if ( isHDLTEnabled(*gstateptr) && oldclipno != newclipno ) {
    *clippath_old = *pageclip ;
    theClipStack(*clippath_old) = stackifreject ;
    gs_reservecliprec(theClipRecord(*clippath_old)) ;
    gs_reserveclippath(theClipStack(*clippath_old)) ;
    return TRUE ;
  }
  return FALSE ;
}

/* ---------------------------------------------------------------------------*/
/** \brief Notify HDLT of clipping stack changes, and act on result.

    \param[in] clippath_old  The saved clipping details.
    \param     result        TRUE if there were no errors since
                             \c gs_hdlt_clipchanges, FALSE if there were
                             errors.

    \retval TRUE  If there was no error before calling this function, and the
                  HDLT callback added or discarded the clip change
                  successfully.
    \retval FALSE If an error before calling this function, or the HDLT
                  callback failed.

    This routine either keeps the new clipping details and frees the saved
    details, or re-instates the saved clipping details and frees the new
    details. If there were errors since the matching gs_hdlt_clipchanges()
    call, HDLT is not informed of the clip changes, and the saved clip
    details are freed (this is equivalent to the behaviour with HDLT turned
    off). */
static Bool gs_hdlt_cliprestore(/*@notnull@*/ /*@in@*/ CLIPPATH *clippath_old,
                                Bool result)
{
  if ( isHDLTEnabled(*gstateptr) && result ) {
    switch ( IDLOM_CLIP() ) {
    case NAME_false:              /* PS error in IDLOM callbacks */
      result = FALSE ;
      break ;
    case NAME_Discard:          /* Go back to old clip */
    /* Make sure that the old clip has correct device clip record for the
       current gstate. We may be called just after restoring a page
       device. */
      if ( clip_device_correct(clippath_old, gstateptr) ) {
        /* Free the clips in the gstate's clip, then transfer responsibility
           of the saved clippath's clip and stack to the gstate. */
        gs_freecliprec(&theClipRecord(thegsPageClip(*gstateptr))) ;
        gs_freeclippath(&theClipStack(thegsPageClip(*gstateptr))) ;

        thegsPageClip(*gstateptr) = *clippath_old ;

        theClipRecord(*clippath_old) = NULL ;
        theClipStack(*clippath_old) = NULL ;
      } else {
        result = FALSE ;
      }
      break ;
    case NAME_Add:
    default:
      break ;
    }
  }

  gs_freecliprec(&theClipRecord(*clippath_old)) ;
  gs_freeclippath(&theClipStack(*clippath_old)) ;

  return result ;
}

/* ---------------------------------------------------------------------------*/
/** Creates a copy of the current gstate on top of the gstack such that the copy
 * is the new current gstate.
 */
Bool gs_gpush( int32 gtype )
{
  GSTATE *gsnew ;
  corecontext_t *context = get_core_context_interp();
  uint8 regenscreen = FALSE ;

  HQASSERT( gstackptr == gstateptr->next, "gs_gpush: gstack is corrupt" ) ;

  if ( context->systemparams->MaxGsaves > 0 &&
       context->systemparams->MaxGsaves < gstackcount )
    return error_handler( LIMITCHECK ) ;

  if (( gsnew = alloc_gstate()) == NULL )
    return FALSE ;

  /* Fill the new gstate with the content of the old one */
  if ( ! gs_copygstate(gsnew, gstateptr, gtype, NULL) ) {
    gs_discardgstate( gsnew ) ;
    free_gstate( gsnew ) ;
    return FALSE ;
  }

  /* PLRM3 is ambiguous on the clip stack. On one hand, it says for gsave
     (PLRM3, p.603) "all elements of the graphics state are saved", and has
     the clipping path stack in the table of "Device-independent parameters
     of the graphics state" (PLRM3, p.179, table 4.1). On the other hand, the
     description of cliprestore says (PLRM3, p.543) "If there has been no
     clipsave operation since the most recent unmatched gsave, the
     cliprestore operator replaces the clipping path with the one that was in
     effect at the time of the gsave operation". That would imply that the
     clipping path stack is reset in the current gstate after a gsave. There
     isn't any way of distinguishing "gstate" from "gsave" until we get here,
     so we have to put up with counting and then freeing the clip stack. */
  gs_freeclippath(&theClipStack(thegsPageClip(*gsnew))) ;

  /* The new gstate becomes the current gstate */
  gsnew->gId    = ++gstateId ;
  gsnew->slevel = context->savelevel ;
  gsnew->saved  = FALSE ;
  gsnew->gType  = GST_CURRENT ;
  gsnew->next   = gstackptr = gstateptr ;
  gstateptr     = gsnew ;
  gstackptr->gType = gtype ;
  gstateptr->theHDLTinfo.next = &gstackptr->theHDLTinfo ;

  /* Regenerate screen always needs to be FALSE when gsave'ing. */
  gsc_invalidate_one_gstate_screens( gsnew->colorInfo , regenscreen ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------------*/
/** Removes the top gstate (below the current gstate) from the graphics
 * stack. This is used when setpagedevice succeeds, to discard the gstate we
 * saved in case of a setpagedevice failure. This is NOT the converse of
 * gs_push().
 */
void gs_gpop( void )
{
  GSTATE *gs ;

  HQASSERT( gstackptr == gstateptr->next, "gs_gpop: gstack is corrupt!" ) ;

  gs = gstackptr ;
  gstateptr->next = gstackptr = gs->next ;
  gstateptr->theHDLTinfo.next = &gstackptr->theHDLTinfo ;

  gs_discardgstate( gs ) ;
  free_gstate( gs ) ;
}

/* ---------------------------------------------------------------------------*/
/** \brief Exchange the current gstate with the top of the gstate stack.
           This is used around HPGL sections.

    \param from  The state we're currently in. This can't be checked because the
                 current state is always marked GST_CURRENT, but once exchanged
                 it will be marked with this type.

    \param to    The state we want to swap to. Must be on top of the gstack.

    \retval TRUE gstates were exchanged.
 */
Bool gs_gexch( int32 to, int32 from )
{
  HDLTinfo * hd ;
  GSTATE * gs ;

  HQASSERT( gstackptr == gstateptr->next, "gs_gexch: gstack is corrupt!" ) ;
  HQASSERT( gstateptr->gType == GST_CURRENT, "gs_gexch: gstateptr type!" ) ;

  gs = gstackptr ;
  if (!gs || gs->gType != to)
    return FALSE ;

  /* Limit swaps to same save level - we could relax this if the caller is
     careful to unswap again before any restore, but it's not necessary for
     now. */
  if (gs->slevel != gstateptr->slevel)
    return FALSE ;

  gstateptr->next = gs->next ;
  gstackptr = gstateptr ;
  gstateptr = gs ;
  gs->next  = gstackptr ;

  gstackptr->gType = from ;
  gstateptr->gType = GST_CURRENT ;

  /* Do we actually need to reorder this HDLT list too? */
  hd = gstateptr->theHDLTinfo.next ;
  gstateptr->theHDLTinfo.next = &gstackptr->theHDLTinfo ;
  gstackptr->theHDLTinfo.next = hd ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool gs_cpush( void )
{
  CLIPPATH *clippath ;
  GSTATE   *gs = gstateptr ;

  if ( (clippath = gs_newclippath(&thegsPageClip(*gs))) == NULL )
    return FALSE ;

  /* Link new entry under top. */
  theClipStack(thegsPageClip(*gs)) = clippath ;

  return TRUE ;
}

/* ---------------------------------------------------------------------------*/
Bool gs_ctop( void )
{
  CLIPPATH *clippath, *pageclip, newclip = {0} ;
  Bool result, hdlt_clipchanges ;
  CLIPPATH hdlt_oldclippath = {0} ;

  pageclip = &thegsPageClip(*gstateptr) ;
  clippath = theClipStack(*pageclip) ;
  if ( clippath != NULL ) {
    newclip = *clippath ;
  } else {
    /* PLRM3, p.543: If there is no clip stack, we take the clip path from
       the previous gstate, but not its clip stack. */
    newclip = thegsPageClip(*gstackptr) ; /* N.B. gstackptr, not gstateptr */
    theClipStack(newclip) = NULL ;
  }

  if ( ! gs_cflush(&newclip) )
    return FALSE ;

  /* Take immediate references to the new clip chain and stack. */
  gs_reservecliprec(theClipRecord(newclip)) ;
  gs_reserveclippath(theClipStack(newclip)) ;

  /* Ensure that the clip chain has the correct device clip record. If we're
     taking the clip path from a saved gstate, it may not share the same page
     device, so we need to re-build the clip record chain, inserting a new
     device clip record at the start of the chain. It is more likely than not
     that the gstate will share the same pagedevice. */
  if ( !clip_device_correct(&newclip, gstateptr) ) {
    gs_freecliprec(&theClipRecord(newclip)) ;
    gs_freeclippath(&theClipStack(newclip)) ;
    return FALSE ;
  }

  /* Save the old clip state in case HDLT rejects the new clip path, but
     ensure we'll drop the top element of the old clip stack regardless, by
     storing the new clip stack in the saved details. */
  hdlt_clipchanges = gs_hdlt_clipchanges(theClipRecord(newclip),
                                         theClipStack(newclip),
                                         &hdlt_oldclippath) ;

  /* Then lose the old references to gstate's clip chain and stack. */
  gs_freecliprec(&theClipRecord(*pageclip)) ;
  gs_freeclippath(&theClipStack(*pageclip)) ;

  /* Install the new clip chain and stack. We've transferred responsibility
     for the new clip's references to the gstate.  */
  *pageclip = newclip ;

  if ( doing_imposition &&
       thegsPageBaseID(*pageclip) != pageBaseMatrixId )
    imposition_update_rectangular_clipping(gstateptr) ;

  result = TRUE ;
  if ( hdlt_clipchanges ) {
    result = gs_hdlt_cliprestore(&hdlt_oldclippath, result) ;
  }

  return result ;
}

/* ---------------------------------------------------------------------------*/
/** Set the current gstate from gs_new. This will happen from a setgstate,
 * grestore, grestoreall, or similiar internal operations.
 *
 * If 'docopy' is true, then the contents of gs_new is copied into the
 * current gstate. Typically this will be the case for setgstate,
 * grestoreall, or save followed by grestore.
 *
 * If 'dopop' is true, then gs_new is in the graphics stack and gstates
 * above gs_new up to the current gstate (but not including, if we're
 * keeping a copy of gs_new - ie if 'docopy' is true) will be removed
 * and freed.
 *
 * Typical usage is:
 *   gs_setgstate(gs, type, TRUE, FALSE, FALSE, ...) for setting a gstate from
 *   a previously saved gstate (setgstate usage).
 *   gs_setgstate(gs, type, FALSE, TRUE, FALSE, ...) for a grestore.
 *   gs_setgstate(gs, type, TRUE, TRUE, TRUE, &dpd) for a grestoreall.
 */
Bool gs_setgstate(GSTATE *gs_new,
                  int32   gtype,
                  Bool    docopy,
                  Bool    dopop,
                  Bool    doendpage,
                  deactivate_pagedevice_t *dpd)
{
  Bool     result ;
  CLIPPATH *clippath ;
  deactivate_pagedevice_t dpd_query ;

  int32     hdlt_oldstate ;
  OBJECT   *hdlt_olddict ;
  Bool      hdlt_clipchanges ;
  CLIPPATH  hdlt_oldclippath = {0} ;

  int32 tospotno, fromspotno ;
  OMATRIX pagectm ;

  GSTATE *oldgrframes = grframes ;

  HQASSERT( gs_new, "gs_setgstate: gs_new is NULL" ) ;
  HQASSERT( gstateptr,  "gs_setgstate: gstack is really corrupt!" ) ;
  HQASSERT( gstackptr == gstateptr->next, "gs_setgstate: gstack is corrupt!" ) ;
  HQASSERT( docopy || dopop,
            "gs_setgstate: at least one of docopy and dopop should be set!" ) ;

  /* Required vignette flush if changing HDLT state, gs tags state. If clip
     state changes, flush might be deferred. */
  clippath = & thegsPageClip( *gs_new ) ;
  if ( oDict(gstateptr->theHDLTinfo.hooksDict) != oDict(gs_new->theHDLTinfo.hooksDict) ||
       oDict(gstateptr->theGSTAGinfo.dict) != oDict(gs_new->theGSTAGinfo.dict) ) {
    if ( ! flush_vignette( VD_Default )) /* Required flush */
      return FALSE ;
  } else if ( ! gs_cflush( clippath ) )
    return FALSE ;

#define return DO_NOT_RETURN__SET_result_TO_FALSE_INSTEAD!

  hdlt_oldstate    = theIdlomState( *gstateptr ) ;
  hdlt_olddict     = oDict(gstateptr->theHDLTinfo.hooksDict) ;
  /* If HDLT rejects the change to the new clip chain, then it is also
     rejecting changes to the clipping stack, and we will preserve the
     current gstate's clipping stack. */
  hdlt_clipchanges = gs_hdlt_clipchanges(theClipRecord(*clippath),
                                         theClipStack(thegsPageClip(*gstateptr)),
                                         &hdlt_oldclippath) ;

  /* We remove the (un)desired gstates from the gstack now in case we
   * are forced to partial paint during this operation - we put them
   * on grframes, for safe-keeping and patch up the remainder of the
   * gstack.
   */
  if ( dopop && gstackptr != gs_new ) {
    GSTATE *gs ;
    for ( gs = gstackptr ; gs->next != gs_new ; gs = gs->next ) {
      HQASSERT( gs->next, "gs_setgstate: gs_new not on gstack" ) ;
    }

    gs->next = grframes ;
    grframes = gstackptr ;
    gstateptr->next = gstackptr = gs_new ;
  }

  /* Check if we need to deactivate the device (but don't do it);
   * ignore any errors.
   * Note that if already deactivated (i.e. we already have results from a
   * previous call to deactivate_pagedevice) - as may be the case during a
   * restore - then don't do it again.
   */
  if ( dpd == NULL ) {
    dpd = & dpd_query ;
    deactivate_pagedevice( gstateptr , gs_new , NULL , dpd ) ;
  }
  if ( doendpage && dpd->action == PAGEDEVICE_REACTIVATING ) {
    /* conditions for calling EndPage procedure met. Note: this may
     * force a call to the showproc to render a page
     */
    ( void )do_pagedevice_deactivate( dpd ) ;
  }

  tospotno   = gsc_getSpotno( gstateptr->colorInfo ) ;
  fromspotno = gsc_getSpotno( gs_new->colorInfo ) ;

  HQASSERT(!docopy || dpd->action != PAGEDEVICE_DEFERRED_DEACTIVATE,
           "Should not be copying gstate while deactivating") ;

  if ( docopy ) {
    /* (*gstateptr) will remain the current gstate so we bodily copy the
       contents of gs_new into the current gstate - after clearing out
       anything already there. */
    HQASSERT(dpd->action == PAGEDEVICE_REACTIVATING ||
             !dpd->new_is_pagedevice ||
             !dpd->old_is_pagedevice ||
             (gsc_getRS(gstateptr->colorInfo) == gsc_getRS(gs_new->colorInfo)),
             "Rasterstyle is different but page device is not reactivating") ;
    /* We must reuse the original target raster style in the sequence -
        gsave...showpage...grestore when we have a virtual page group. This
        will be changed by the showpage, re-building its rasterstyle. */
    if ( dpd->action != PAGEDEVICE_REACTIVATING ) {
      gsc_setTargetRS(gs_new->colorInfo, gsc_getTargetRS(gstateptr->colorInfo)) ;
    }

    gs_discardgstate( gstateptr ) ;
    result = gs_copygstate(gstateptr, gs_new, gtype, dpd) ;

    /* Re-link the HDLT target chain. We only need to do this if we're
       replacing the contents of the gstateptr, otherwise the relationship
       between a target and its parent remains the same. */
    /** \todo @@@ TODO FIXME ajcd 2005-09-19: This is ugly, it won't work
        properly when grestoreall or gstate...setgstate is used within an
        HDLT target that isn't in the stack frame. */
    gstateptr->theHDLTinfo.next = &gstackptr->theHDLTinfo ;
  } else { /* !docopy */
    Bool keep_rs = FALSE ;

    /* gs_new will become the current gstate, so we need to reset things
     * in gs_new that aren't valid:
     *
     * This case is a typical grestore matched with a gsave
     */
    result = TRUE ;

    HQASSERT(gs_new != gstateptr, "New gstate cannot be current gstate") ;

    /* Must copy the deferred info. */
    if ( dpd->action == PAGEDEVICE_DEFERRED_DEACTIVATE ) {
      thegsDeviceBandId(*gs_new) = thegsDeviceBandId(*gstateptr) ;
      thegsPageBaseID(gs_new->thePDEVinfo) = thegsPageBaseID(gstateptr->thePDEVinfo) ;

      thegsDeviceW(*gs_new) = thegsDeviceW(*gstateptr);
      thegsDeviceH(*gs_new) = thegsDeviceH(*gstateptr);

      gs_reservecliprec(gstateptr->thePDEVinfo.initcliprec) ;
      gs_freecliprec(&gs_new->thePDEVinfo.initcliprec) ;
      gs_new->thePDEVinfo.initcliprec = gstateptr->thePDEVinfo.initcliprec ;

      MATRIX_COPY( & thegsDeviceCTM(*gs_new) ,
                   & thegsDeviceCTM(*gstateptr)) ;

      MATRIX_COPY( & thegsDevicePageCTM(*gs_new) ,
                   & thegsDevicePageCTM(*gstateptr)) ;

      gs_new->thePDEVinfo.scanconversion = gstateptr->thePDEVinfo.scanconversion;
      thegsDeviceStrokeAdjust(*gs_new) = thegsDeviceStrokeAdjust(*gstateptr) ;
      thegsSmoothness(*gs_new) = thegsSmoothness(*gstateptr);
      thegsDeviceScreenRotate(*gs_new) = thegsDeviceScreenRotate(*gstateptr) ;
      thegsDeviceHalftonePhaseX(*gs_new) = thegsDeviceHalftonePhaseX(*gstateptr) ;
      thegsDeviceHalftonePhaseY(*gs_new) = thegsDeviceHalftonePhaseY(*gstateptr) ;

      /* Take a mirror of these attributes and keep it in colorInfo for use when
       * there isn't a gstate available */
      gsc_setScreenRotate(gs_new->colorInfo, thegsDeviceScreenRotate(*gs_new));
      gsc_setHalftonePhase(gs_new->colorInfo,
                           thegsDeviceHalftonePhaseX(*gs_new),
                           thegsDeviceHalftonePhaseY(*gs_new));

      keep_rs = TRUE ;
    } else if ( dpd->action == PAGEDEVICE_NO_ACTION &&
                (gtype == GST_GSAVE || gtype == GST_SAVE) ) {
      keep_rs = TRUE ;
    }

    if ( keep_rs ) {
      /* Keep the target rasterstyle from the existing gstate unless this is a
         group restore, a page device reactivation, or if we are deferring
         deactivation. The reason we want to keep the existing rasterstyle is
         to handle the case of gsave...showpage...grestore when we have a
         virtual page group. The virtual page group will be changed by the
         showpage, re-building its rasterstyle. We only wish to change the
         target rasterstyle when restoring to a group gsave with the same
         pagedevice.
         Device rasterstyles will normally match except in the case of gsave...
         setpagedevice...grestore, when we need to retain the new one. So we
         need to copy both raster styles. */
      gsc_setDeviceRS(gs_new->colorInfo, gsc_getRS(gstateptr->colorInfo)) ;
      gsc_setTargetRS(gs_new->colorInfo, gsc_getTargetRS(gstateptr->colorInfo)) ;
    }

    /* If called from the pdf Q operator, preserve the path from the old gstate
     * by clearing out the path from gs_new and copying the old one. This is done
     * because pdf doesn't hold the path in the gstate and this is the easiest way
     * for us to combine postscript and pdf data structures.
     */
    if (gtype == GST_PDF) {
      PATHINFO *newpath = &thePathInfo(*gs_new) ;
      PATHINFO *oldpath = &thePathInfo(*gstateptr) ;

      path_free_list(thePath(*newpath), mm_pool_temp) ;
      path_init(newpath);
      if (!path_copy( newpath, oldpath, mm_pool_temp ))
        result = FALSE;
    }

    gs_discardgstate( gstateptr ) ;

    free_gstate( gstateptr ) ;

    gstateptr = gs_new ;
    gstateptr->gType = GST_CURRENT ;
    gstackptr = gstateptr->next ;
  }

  MATRIX_COPY(&pagectm, &thegsPageCTM(*gs_new)) ;

  /* Set the new device */
  if ( dpd->action != PAGEDEVICE_DEFERRED_DEACTIVATE ) {
    /* We've handled referencing the correct initclip record in the
       conditions above. We don't want to discard that work just because we
       want to reset the device_current_* function pointers. */
    CLIPRECORD *initcliprec = gs_new->thePDEVinfo.initcliprec ;
    gs_reservecliprec(initcliprec) ;
    SET_DEVICE(thegsDeviceType(*gs_new)) ;
    HQASSERT(gstateptr->thePDEVinfo.initcliprec == NULL,
             "Didn't destroy initclip record in SET_DEVICE") ;
    gstateptr->thePDEVinfo.initcliprec = initcliprec ;
  }

  if ( doing_imposition &&
       thegsPageBaseID(thegsPageClip(*gstateptr)) != pageBaseMatrixId )
    imposition_update_rectangular_clipping( gstateptr ) ;
  else if ( dpd->action == PAGEDEVICE_DEFERRED_DEACTIVATE )
    imposition_update_rectangular_clipping( gstateptr ) ;

  /* Now that we've copied all of the underlying structure of the gstate
   * we can set the things that may produce errors. These include the path,
   * the screen and the pagedevice.
   *
   * Note that if this fails we must end up with things in a consistent state.
   * Failure used to cause the device type to be set to a nulldevice as a
   * safety measure. But this could cause problems later on. So now we
   * explicitly mark it as failing pagdevice with the DEVICE_ERRBAND code.
   */
  if ( dpd->action == PAGEDEVICE_REACTIVATING ) {
    if ( result ) {
      /* /resetpagedevice calls /pagedevice, which increments the deviceid,
         but that's the wrong thing to do here. */
      int32 deviceid = thegsDeviceBandId(*gstateptr) ;
      /* When reactivating a previous pagedevice, we should preserve the
         clip from the gstate that contained the pagedevice. /resetpagedevice
         calls /pagedevice, which calls /initclip, destroying the clip. */
      CLIPPATH oldclip = thegsPageClip(*gstateptr) ;
      gs_reservecliprec(theClipRecord(oldclip)) ;

      result = (call_resetpagedevice() &&
                clip_device_correct(&oldclip, gstateptr)) ;

      if ( result ) {
        gs_freecliprec(&theClipRecord(thegsPageClip(*gstateptr))) ;
        /* Don't modify the clip stack, it shouldn't have been touched by
           resetpagedevice. */
        theClipStack(oldclip) = theClipStack(thegsPageClip(*gstateptr)) ;
        thegsPageClip(*gstateptr) = oldclip ;
      } else {
        gs_freecliprec(&theClipRecord(oldclip)) ;
      }

      thegsDeviceBandId(*gstateptr) = deviceid ;
    }

    if ( ! result )
      SET_DEVICE( DEVICE_ERRBAND ) ;
  }

  /* It is possible that a callback in a gsave...setgstate...grestore context
     could have changed colorant mappings. Update the rasterstyles to ensure
     real and virtual colorants are marked correctly. */
  guc_updateEquivalentColorants(gsc_getTargetRS(gstateptr->colorInfo),
                                COLORANTINDEX_ALL) ;

  result = result && gsc_regeneratehalftoneinfo(gstateptr->colorInfo,
                                                gsc_getSpotno(gstateptr->colorInfo),
                                                tospotno);

  /* Finally copy the CTM, because resetting the pagedevice will have
   * installed the default one.
   */
  newctmin = NEWCTM_ALLCOMPONENTS ;
  MATRIX_COPY(&thegsPageCTM(*gstateptr), &pagectm) ;

  if ( result ) {
    /* Check to see if the hdlt dictionary has changed */
    if ( isHDLTEnabled( *gstateptr )) {
      if ( hdlt_oldstate == HDLT_NORMAL &&
           hdlt_olddict == oDict(gstateptr->theHDLTinfo.hooksDict) )
        theIdlomState( *gstateptr ) = HDLT_NORMAL ;
      else
        theIdlomState( *gstateptr ) = HDLT_NEWDICT ;
    }
    else {
      /* If HDLT was previously enabled, but isn't now, then we check to
       * see if it has finished with its caches (if there are no other
       * enabled states.
       */
      if ( hdlt_oldstate != HDLT_DISABLED )
        idlom_maybeFlushAll() ;
    }
  }

  /* If the clipping has changed then HDLT may want to know about it */
  if ( hdlt_clipchanges ) {
    result = gs_hdlt_cliprestore(&hdlt_oldclippath, result) ;
  }

  if ( dopop ) {
    /* Clean up any gstates we took out of general circulation */
    while ( grframes != oldgrframes ) {
      GSTATE *gs = grframes->next ;
      gs_discardgstate( grframes ) ;
      free_gstate( grframes ) ;
      grframes = gs ;
    }
  }

#undef return
  return result ;
}


/** Copies the gstate.
 * The case where gs_dst is the current gstate requires more than a simple
 * copy since a new device may be set - this is done by gs_setgstate (which
 * should be the only caller where this is the case).
 *
 * The deactivate_pagedevice_t argument will be removed once the pagedevice
 * details are moved to a referenced structure.
 */
Bool gs_copygstate(GSTATE *gs_dst, GSTATE *gs_src, int32 gtype,
                   deactivate_pagedevice_t *dpd)
{
  PATHINFO *topath ;
  PATHINFO *frompath ;
  Bool success = TRUE;

  HQASSERT( gs_dst,  "gs_copygstate: gs_dst must be non-NULL" ) ;
  HQASSERT( gs_src,  "gs_copygstate: gs_src must be non-NULL" ) ;
  HQASSERT(dpd || gs_dst != gstateptr,
           "gs_copygstate: dpd must be non-NULL if copying to gstateptr") ;

  /* The following assert holds because:
     1) gs_copygstate() is only called with dpd non-NULL from gs_setgstate
        when docopy is TRUE.
     2) In all such cases, deactivate_pagedevice() is called with a NULL
        parameter for the pagedevice dictionary, and thus cannot set the
        deactivation options.
  */
  HQASSERT(!dpd ||
           (dpd->action != PAGEDEVICE_DEFERRED_DEACTIVATE &&
            dpd->action != PAGEDEVICE_FORCED_DEACTIVATE),
           "Cannot be deactivating pagedevice when copying to gstateptr") ;

  /* Copy the device information */
  Copy( & thegsDevicePageDict(*gs_dst),
        & thegsDevicePageDict(*gs_src)) ;

  if ( gs_dst != gstateptr ) {
    /* Not a setdevice */
    thegsDeviceType(*gs_dst) = thegsDeviceType(*gs_src) ;
    thegsDeviceHalftonePhaseX(*gs_dst) = thegsDeviceHalftonePhaseX(*gs_src) ;
    thegsDeviceHalftonePhaseY(*gs_dst) = thegsDeviceHalftonePhaseY(*gs_src) ;
    thegsDistillID(*gs_dst) = thegsDistillID(*gs_src) ;

    /* Take a mirror of these attributes and keep it in colorInfo for use when
     * there isn't a gstate available */
    gsc_setHalftonePhase(gs_dst->colorInfo,
                         thegsDeviceHalftonePhaseX(*gs_dst),
                         thegsDeviceHalftonePhaseY(*gs_dst));
  }
  else if ( dpd->action == PAGEDEVICE_REACTIVATING ) {
    /* Reset halftone phase for the device */
    thegsDeviceHalftonePhaseX(*gs_dst) = thegsDeviceHalftonePhaseX(*gs_src) ;
    thegsDeviceHalftonePhaseY(*gs_dst) = thegsDeviceHalftonePhaseY(*gs_src) ;

    /* Take a mirror of these attributes and keep it in colorInfo for use when
     * there isn't a gstate available */
    gsc_setHalftonePhase(gs_dst->colorInfo,
                         thegsDeviceHalftonePhaseX(*gs_dst),
                         thegsDeviceHalftonePhaseY(*gs_dst));
  }
  else {
   /* Need to do this because the grestore screen needs to inherit the
    * halftone phase that's current in the gstate (but the references
    * to it are always in the gstate being restored).
    */
    thegsDeviceHalftonePhaseX(*gs_src) = thegsDeviceHalftonePhaseX(*gs_dst) ;
    thegsDeviceHalftonePhaseY(*gs_src) = thegsDeviceHalftonePhaseY(*gs_dst) ;

    /* Take a mirror of these attributes and keep it in colorInfo for use when
     * there isn't a gstate available */
    gsc_setHalftonePhase(gs_src->colorInfo,
                         thegsDeviceHalftonePhaseX(*gs_src),
                         thegsDeviceHalftonePhaseY(*gs_src));
  }

  /* Always have to copy this since it might go out of scope. */
  Copy(&thegsDeviceShowProc(*gs_dst), &thegsDeviceShowProc(*gs_src));

  thegsDeviceBandId(*gs_dst) = thegsDeviceBandId(*gs_src) ;
  thegsPageBaseID(gs_dst->thePDEVinfo) = thegsPageBaseID(gs_src->thePDEVinfo) ;

  thegsDeviceW(*gs_dst) = thegsDeviceW(*gs_src);
  thegsDeviceH(*gs_dst) = thegsDeviceH(*gs_src);

  gs_reservecliprec(gs_src->thePDEVinfo.initcliprec) ;
  gs_dst->thePDEVinfo.initcliprec = gs_src->thePDEVinfo.initcliprec ;

  MATRIX_COPY( & thegsDeviceCTM(*gs_dst) ,
               & thegsDeviceCTM(*gs_src)) ;

  MATRIX_COPY( & thegsDevicePageCTM(*gs_dst) ,
               & thegsDevicePageCTM(*gs_src)) ;

  MATRIX_COPY( & thegsDeviceInversePageCTM(*gs_dst) ,
               & thegsDeviceInversePageCTM(*gs_src)) ;

  gs_dst->pa_eps = gs_src->pa_eps ;

  gs_dst->thePDEVinfo.scanconversion = gs_src->thePDEVinfo.scanconversion;
  thegsDeviceStrokeAdjust(*gs_dst) = thegsDeviceStrokeAdjust(*gs_src) ;
  thegsSmoothness(*gs_dst) = thegsSmoothness(*gs_src);
  thegsDeviceScreenRotate(*gs_dst) = thegsDeviceScreenRotate(*gs_src) ;

  /* Copy color information */
  gsc_copycolorinfo( gs_dst->colorInfo, gs_src->colorInfo ) ;

  /* Copy the line style information */
  HqMemCpy( & gs_dst->thestyle ,
            & gs_src->thestyle ,
            sizeof( LINESTYLE )) ;
  theDashListLen(gs_dst->thestyle) = 0 ;
  if ( !gs_storedashlist( &gs_dst->thestyle, theDashList(gs_src->thestyle),
                           theDashListLen(gs_src->thestyle) ) )
    success = FALSE ;

  /* Copy the current path.
   * The source gets to keep the original copy so that pointers into the
   * current path remain valid after gsave/grestore pairs - and we don't
   * come here to copy the path on the grestore.
   */
  topath   = &thePathInfo(*gs_dst) ;
  frompath = &thePathInfo(*gs_src) ;

  /* If it's a gs_gpush GST_SETCHARDEVICE, then zero the pointers in the
   * gstate. This is an optimisation which is used instead of doing a
   * newpath immediately after it.
   * Otherwise, gs_src gets a new copy of the path - this may be an empty path
   * (and if so, the copy won't fail).
   * If the copy does fail then we report a VMERROR - even if we're doing a
   * grestore which supposedly can't fail...
   */
  path_init( topath ) ;

  if (( gtype != GST_SETCHARDEVICE || gs_src != gstateptr ) &&
      ( ! path_copy_lazy( topath, frompath, mm_pool_temp ))) {
    path_init( topath ) ; /* ensure there aren't any dangling pointers */
    success = error_handler( VMERROR ) ;
  }

  /* Copy the font information */
  HqMemCpy( & gs_dst->theFONTinfo ,
            & gs_src->theFONTinfo ,
            sizeof( FONTinfo )) ;

  /* Copy the clipping paths */
  gs_reservecliprec(theClipRecord(thegsPageClip(*gs_src))) ;
  gs_reserveclippath(theClipStack(thegsPageClip(*gs_src))) ;

  /* Copy page information */
  HqMemCpy( & gs_dst->thePAGEinfo ,
            & gs_src->thePAGEinfo ,
            sizeof( PAGEinfo )) ;

  /* Copy HDLT information */
  HqMemCpy( & gs_dst->theHDLTinfo ,
            & gs_src->theHDLTinfo ,
            sizeof( HDLTinfo )) ;

  /* Copy trapping information */
  HqMemCpy( & gs_dst->theTRAPinfo ,
            & gs_src->theTRAPinfo ,
            sizeof( TRAPinfo )) ;

  /* Copy PDF information */
  HqMemCpy( & gs_dst->thePDFFinfo ,
            & gs_src->thePDFFinfo ,
            sizeof( PDFFinfo )) ;

  /* Copy gs tag information */
  HqMemCpy( & gs_dst->theGSTAGinfo ,
            & gs_src->theGSTAGinfo ,
            sizeof( GSTAGinfo )) ;

  /* But the data needs explicit copying */
  if ( gs_src->theGSTAGinfo.data != NULL ) {
    gs_dst->theGSTAGinfo.data = mm_alloc( mm_pool_temp,
      gs_src->theGSTAGinfo.data[0] * sizeof(gs_src->theGSTAGinfo.data[0]),
      MM_ALLOC_CLASS_GSTATE ) ;
    if ( gs_dst->theGSTAGinfo.data == NULL ) {
      success = error_handler( VMERROR ) ;
    } else {
      HqMemCpy( gs_dst->theGSTAGinfo.data , gs_src->theGSTAGinfo.data ,
                gs_src->theGSTAGinfo.data[0] * sizeof(gs_src->theGSTAGinfo.data[0])) ;
    }
  }

  /* Copy transparency information. */
  tsCopy( &gs_src->tranState , &gs_dst->tranState );

  /* Copy user label. */
  gs_dst->user_label = gs_src->user_label;

  return success ;
}

void gstack_C_globals(core_init_fns *fns)
{
  init_C_globals_gstack() ;

  fns->swstart = gstack_swstart ;
  fns->finish = gstack_finish ;
}

/*
Log stripped */
