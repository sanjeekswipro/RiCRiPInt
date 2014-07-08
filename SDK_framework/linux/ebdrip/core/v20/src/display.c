/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:display.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list management.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "mm.h"
#include "mmcompat.h"
#include "mm_core.h" /* gc_safe_in_this_operator */
#include "fonts.h"
#include "fontcache.h"
#include "monitor.h"
#include "namedef_.h"
#include "timing.h"
#include "taskh.h"

#include "display.h"
#include "dl_ref.h"
#include "bitblts.h"
#include "dl_store.h"
#include "images.h" /* TypeImageMask */
#include "imageo.h" /* IMAGEOBJECT */
#include "graphics.h"
#include "gu_chan.h"
#include "dlstate.h"
#include "params.h"
#include "plotops.h"
#include "dl_image.h"
#include "ndisplay.h"
#include "dl_bres.h"
#include "gstate.h"
#include "clipops.h"
#include "control.h"
#include "dl_bbox.h"
#include "dl_foral.h"
#include "imstore.h"
#include "dl_purge.h"
#include "shadecev.h"
#include "cvdecodes.h"
#include "routedev.h"
#include "fcache.h"

#include "pathops.h"
#include "idlom.h"

#include "toneblt.h"
#include "rollover.h"
#include "trap.h"   /* isTrapEnabled() */
#include "dl_free.h"
#include "vndetect.h"
#include "dl_color.h"

#include "gs_cache.h" /* coc_reset */
#include "gschtone.h"
#include "gschcms.h"
#include "gscdevci.h"
#include "gschead.h"
#include "halftone.h" /* ht_erasefreechentry */
#include "color.h"    /* ht_colorIsClear */

#include "rcbcntrl.h"
#include "spdetect.h" /* get_separation_name */
#include "renderom.h" /* init_separation_omission */

#include "gs_tag.h"
#include "hdlPrivate.h" /* hdlRestoreBandTails */
#include "groupPrivate.h"
#include "cce.h"
#include "patternshape.h"

#include "ripmulti.h" /* NUM_THREADS */
#include "ripdebug.h"
#include "debugging.h"
#include "miscops.h"
#include "swenv.h"

#include "irr.h"
#include "dl_shade.h"
#include "pclAttrib.h"
#include "pclGstate.h"
#include "backdrop.h"
#include "jobmetrics.h"
#include "spotlist.h"
#include "surface.h"
#include "region.h"
#include "pattern.h"
#include "objnamer.h"
#include "often.h"
#include "devices.h"
#include "swcopyf.h"
#include "imtiles.h"
#include "mlock.h"
#include "corejob.h"
#include "pgbproxy.h"
#include "forms.h"
#include "riptimeline.h"
#include "bandtable.h"
#include "lowmem.h"
#include "imexpand.h" /* im_expand_colorinfo_free et. al. */
#include "preconvert.h" /* preconvert_on_the_fly */

#include "monitori.h"  /* MON_TYPE_* for now */


uint8 dl_currentexflags;
uint8 dl_currentdisposition;

/**
 * Current clipping bounding box
 */
dbbox_t cclip_bbox ;

int dl_safe_recursion ;

int32 dl_pipeline_depth ;

/** The eraseno of the oldest page flushed by dl_pipeline_flush(). */
static dl_erase_nr dl_last_flush ;

/*--------------------------------------------------------------------------*/
/** The ring buffer of DL pages. There should be n+1 pages, where n is the
    number of DLs that can be in processing (interpretation and rendering
    combined) at any time. Whether there is actually a separate rendering DL
    is controlled by the pipeline depth parameter.

    The flushpage, outputpage, and inputpage are pointers into this array. In
    age, flushpage >= outputpage >= inputpage. flushpage and inputpage are
    only modified by the interpreter task. outputpage is modified by the
    erase task finaliser. */
static DL_STATE dl_pages[ NUM_DISPLAY_LISTS ] ;

/** \brief The oldest page that has not been totally flushed.

    The task group for a page is not released asynchronously by \c
    dl_erase_finalise(), because the interpreter needs to be able to test if
    a page has completed. This pointer is the oldest page whose task group
    has not been released. The task group is released by dl_pipeline_flush(),
    or dl_clear_page(). */
static DL_STATE *flushpage ;

/** \brief The current rendering page.

    When this catches up to inputpage, there are no asynchronous pages left
    in the rendering pipeline. */
static DL_STATE *outputpage ;

/** \brief The current input page.

    This points to the page currently under construction. dl_pipeline_flush()
    ensures that there is sufficient space that this will never catch up to
    flushpage.
*/
static DL_STATE *inputpage ;

static multi_mutex_t outputpage_mutex;
static multi_mutex_t inputpage_mutex;

DL_STATE *inputpage_lock(void)
{
  multi_mutex_lock(&inputpage_mutex) ;
  return inputpage ;
}

void inputpage_unlock(void)
{
  multi_mutex_unlock(&inputpage_mutex) ;
}

DL_STATE *outputpage_lock(void)
{
  multi_mutex_lock(&outputpage_mutex) ;
  return outputpage ;
}

void outputpage_unlock(void)
{
  multi_mutex_unlock(&outputpage_mutex) ;
}

/*--------------------------------------------------------------------------*/
#ifdef DEBUG_BUILD
/* For human-readable debug messages involving render opcodes. Please
   keep this up to date with changes to the enumeration in displayt.h. */
const char debug_opcode_names[N_RENDER_OPCODES][16] = {
  "void",
  "erase",
  "char",
  "rect",
  "quad",
  "fill",
  "mask",
  "image",
  "vignette",
  "gouraud",
  "shfill",
  "shfill_patch",
  "hdl",
  "group",
  "backdrop",
  "cell"
} ;

static int32 countlinks = 0;
static int32 countdlobj = 0;
static dbbox_t debug_focus = {MINDCOORD, MINDCOORD, MAXDCOORD, MAXDCOORD} ;
static int32 debug_dl_firstimage = 0 ;
static int32 debug_dl_lastimage = MAXINT32 ;
static int32 debug_dl_image = 0 ;
static int32 debug_dl_imageopt = 0 ;
static int32 debug_dl_firstsetg = 0 ;
static int32 debug_dl_lastsetg = MAXINT32 ;
static int32 debug_dl_setg = 0 ;
static int32 debug_dl_drop = 0 ;

Bool debug_dl_skipimage(int32 optimise)
{
  ++debug_dl_image ;
  return (debug_dl_image < debug_dl_firstimage ||
          debug_dl_image > debug_dl_lastimage ||
          ((optimise ^ (debug_dl_imageopt >> 16)) & debug_dl_imageopt) != 0) ;
}

Bool debug_dl_skipsetg(void)
{
  ++debug_dl_setg ;
  return (debug_dl_setg < debug_dl_firstsetg ||
          debug_dl_setg > debug_dl_lastsetg) ;
}

/**
 * Counts the number of objects and the number of scanline they occupy.
 * From this you can work out the average number of links per objects,
 * or the distance on average each object takes in device pixels.
 */
static void linkstats(DL_STATE *page, dbbox_t *bbox)
{
  int32 y1 = bbox->y1;
  int32 y2 = bbox->y2 +
             guc_getMaxOffsetIntoBand(gsc_getRS(gstateptr->colorInfo));
  int32 objects = 1;

  if ( y1 < 0 )
    y1 = 0 ;
  else if ( y1 >= page->page_h)
    y1 = page->page_h - 1 ;

  if ( y2 < 0 )
    y2 = 0 ;
  else if ( y2 >= page->page_h)
    y2 = page->page_h - 1 ;

  if ( y1 > y2 )
    return ;

  countdlobj += objects ;
  countlinks += ( y2 - y1 + 1 ) ;
}
#else /* !DEBUG_BUILD */
#define linkstats(x, y) EMPTY_STATEMENT()
#endif /* !DEBUG_BUILD */

/**
 * Initialise the given DL state object
 */
STATEOBJECT stateobject_new(int32 spotno)
{
  STATEOBJECT objectstate ;
  objectstate.storeEntry.next = NULL ;
  objectstate.clipstate = NULL ;
  objectstate.patternstate = NULL ;
  objectstate.patternshape = NULL ;
  objectstate.spotno = spotno ;
  objectstate.gstagstructure = NULL ;
  objectstate.tranAttrib = NULL ;
  objectstate.lateColorAttrib = NULL ;
  objectstate.pclAttrib = NULL ;
  return objectstate ;
}


/** Choose DL pool based on an allocation class. */
static inline int dl_choose_pool_index(uint32 allocclass)
{
  if ( allocclass == MM_ALLOC_CLASS_DLREF ||
       allocclass == MM_ALLOC_CLASS_LIST_OBJECT )
    return 1;
  else if ( allocclass == MM_ALLOC_CLASS_NFILL ||
            allocclass == MM_ALLOC_CLASS_GOURAUD )
    return 2;
  else if ( allocclass == MM_ALLOC_CLASS_IMAGE_EXPBUF )
    /* This pool is used for dl pool allocations that are highly likely to be
       freed in low memory.  Segregating them does wonders for reducing memory
       fragmentation in low-memory conditions. */
    return 3;
  else
    return 0;
}


/**
 * Internal function for allocation a DL object
 */
static Bool make_lobj(DL_STATE *page, LISTOBJECT **plobj,
                      int32 opcode, size_t tagb, dbbox_t *bbox)
{
  LISTOBJECT *lobj;
  size_t size = SIZE_ALIGN_UP_P2(sizeof(LISTOBJECT) + tagb, MM_DL_POOL_ALIGN);
  STATEOBJECT *dls = page->currentdlstate;
  void *p;

  HQASSERT(tagb % MM_DL_POOL_ALIGN == 0, "Can't do unaligned two-way alloc");
  HQASSERT(dl_choose_pool_index(MM_ALLOC_CLASS_DLREF)
           == dl_choose_pool_index(MM_ALLOC_CLASS_LIST_OBJECT),
           "Was relying on using the ap for DLREF pool");
  HQASSERT(IS_INTERPRETER(), "make_lobj called outside interpreter");

  *plobj = NULL;
  MM_AP_ALLOC(p, page->dl_ap, size);
  if ( p == NULL ) {
    /* Hacky way of getting low-memory handling. */
    p = dl_alloc(page->dlpools, size, MM_ALLOC_CLASS_LIST_OBJECT);
    if ( p == NULL )
      return error_handler(VMERROR);
  }
  lobj = (LISTOBJECT *)(((char *)p) + tagb);

  init_listobject(lobj, opcode, bbox);

  if ( !guc_backdropRasterStyle(gsc_getTargetRS(gstateptr->colorInfo)) )
    lobj->marker |= MARKER_DEVICECOLOR;

  if ( opcode == RENDER_erase ) {
    DISPOSITION_STORE(lobj->disposition, REPRO_DISPOSITION_ERASE, GSC_FILL, 0);
    if ( !dlc_to_lobj(page->dlc_context, lobj, &page->dlc_erase) )
      return FALSE;
  } else {
    lobj->spflags = CAST_UNSIGNED_TO_UINT8(dl_currentspflags(page->dlc_context)
                                           | dl_currentexflags);
    lobj->disposition = dl_currentdisposition;

    HQASSERT(dls, "No current DL state");
    lobj->objectstate = dls;
    if ( !theITrapIntent(gstateptr) )
      lobj->spflags |= RENDER_UNTRAPPED;

    /* Update dl obj with current color */
    if ( !dlc_to_lobj(page->dlc_context, lobj,
                      dlc_currentcolor(page->dlc_context)) )
      return FALSE;

    /* Check for transparency. */
    if ( stateobject_transparent(lobj->objectstate) )
      lobj->marker |= MARKER_TRANSPARENT | MARKER_OMNITRANSPARENT;

    /* If there is a pattern associated to this object, and the pattern
     * knocks out, then the patterned object can be marked as knock out too,
     * and this will prevent any compositing due to overprinting.
     */
    if ( dls->patternstate && !dls->patternstate->patternDoesOverprint )
      lobj->spflags |= RENDER_KNOCKOUT;
  }
  *plobj = lobj;
  return TRUE;
}

/**
 * Allocate a display list object.
 */
Bool make_listobject(DL_STATE *page, int32 opcode, dbbox_t *bbox,
                     LISTOBJECT **plobj)
{
  size_t tagb = 0;

  if ( opcode != RENDER_erase )
    tagb = TAG_BYTES(page->currentdlstate);

  if ( !make_lobj(page, plobj, opcode, tagb, bbox) )
    return FALSE;
  if ( tagb > 0 )
    HqMemCpy((uint8 *)(*plobj) - tagb, gstateptr->theGSTAGinfo.data + 1, tagb);
  return TRUE;
}

/**
 * Allocate a display list object but with the state copied from
 * another DL object
 */
Bool make_listobject_copy(DL_STATE *page, LISTOBJECT *source,
                          LISTOBJECT **plobj)
{
  size_t tagb = TAG_BYTES(source->objectstate);

  HQASSERT(source->opcode == RENDER_shfill ||
           source->opcode == RENDER_vignette ||
           source->opcode == RENDER_fill ||
           source->opcode == RENDER_quad ||
           source->opcode == RENDER_rect, "Unexpected listobject copy");

  if ( !make_lobj(page, plobj, source->opcode, tagb, &(source->bbox)) )
    return FALSE;

  **plobj = *source;

  if ( tagb > 0 )
    HqMemCpy((uint8 *)*plobj - tagb, (uint8 *)source - tagb, tagb);

  if ( !dl_copy(page->dlc_context, &(*plobj)->p_ncolor, &source->p_ncolor) )
    return FALSE ;
  if ( (source->spflags & RENDER_RECOMBINE) != 0 &&
        source->attr.planes != NULL )
    if ( !dl_copy(page->dlc_context, &(*plobj)->attr.planes,
                  &source->attr.planes) )
      return FALSE;

  return TRUE;
}

/**
 * Initialise the given listobject
 */
void init_listobject(LISTOBJECT *lobj, int32 opcode, dbbox_t *bbox)
{
  HQASSERT(lobj, "NULL listobject");

  lobj->objectstate = NULL;
  lobj->opcode = (uint8)opcode;
  lobj->marker = 0;
  lobj->spflags = RENDER_KNOCKOUT;
  lobj->disposition = 0;
  lobj->dldata.nfill = NULL;
  lobj->attr.planes = NULL;
  lobj->p_ncolor = NULL;
  if ( bbox )
    lobj->bbox = *bbox;
  else
    bbox_clear(&(lobj->bbox));
}

/**
 * Free the given DL object.
 */
void free_listobject(LISTOBJECT *lobj, DL_STATE *page)
{
  size_t tagb = TAG_BYTES(lobj->objectstate);

  dl_release(page->dlc_context, &lobj->p_ncolor);
  if ( (lobj->spflags & RENDER_RECOMBINE) )
    dl_release(page->dlc_context, &lobj->attr.planes);

  /* TWO_WAY_FREE */
  dl_free(page->dlpools, ((uint8 *)lobj) - tagb, sizeof(LISTOBJECT) + tagb,
          MM_ALLOC_CLASS_LIST_OBJECT);
}

/**
 * Add the given listobject to the DL via the appropriate vectored
 * DL addition function.
 */
Bool add_listobject(DL_STATE *page, LISTOBJECT *lobj, Bool *added)
{
  int32 status;

  HQASSERT(lobj, "Trying to add NULL object to DL");

  if ( added )
    *added = FALSE;

#ifdef DEBUG_BUILD
  if ( debug_dl_drop & (1 << lobj->opcode) ) {
    free_dl_object(lobj, page);
    return TRUE ;
  }
#endif

  if ( optional_content_on )
    status = (*device_current_addtodl)(page, lobj);
  else
    status = (*device_table_addtodl[DEVICE_NULL])(page, lobj);

  switch ( status ) {
  case DL_Merged:
  case DL_NotAdded:
    free_dl_object(lobj, page);
    return TRUE;
  case DL_Added:
    if ( added )
      *added = TRUE;
    return TRUE;
  case DL_Error:
    break;
  default:
    HQFAIL("unknown return from addtodl func");
  }
  return FALSE;
}

/**
 * Sets the data entries in the erase DL object.
 * If the erase color changes, the data entries need recalculating.
 */
static void dlerase_setdata(DL_STATE *page, GSTATE *gs, LISTOBJECT *lobj)
{
  Bool erasepgzero = FALSE;
  SPOTNO spot = lobj->objectstate->spotno;
  dl_color_iter_t dliter;
  dlc_iter_result_t result;
  COLORVALUE colval;
  COLORANTINDEX ci;
  dlc_tint_t tint;
  /* We need the following flag for RGB contone vs. everything else
    since for halftone output ht_colorIsClear means 0s, for non-RGB
    contone output the color gets inverted, so ht_colorIsClear which
    is really maxtones gets mapped to 0. */
  Bool fZeroIsClearOrWhite ;

  fZeroIsClearOrWhite = gucr_halftoning(gsc_getRS(gs->colorInfo));
  if ( !fZeroIsClearOrWhite) {
    int32 nColorants;
    DEVICESPACEID dsid;

    guc_deviceColorSpace(gsc_getRS(gs->colorInfo), &dsid, &nColorants);
    fZeroIsClearOrWhite = dsid != DEVICESPACE_RGB;
  }

  /* Get first colorant in dl color entry */
  result = dlc_first_colorant(&page->dlc_erase, &dliter, &ci, &colval);
  while ( result == DLC_ITER_COLORANT ) {
    /* Check single colorant entries */
    if ( fZeroIsClearOrWhite )
      erasepgzero = erasepgzero
        || !ht_colorIsClear(spot, REPRO_TYPE_OTHER /* linework */,
                            ci, colval, gsc_getRS(gs->colorInfo));
    else
      erasepgzero = erasepgzero
        || !ht_colorIsSolid(spot, REPRO_TYPE_OTHER /* linework */,
                            ci, colval, gsc_getRS(gs->colorInfo));
    /* Get next colorant in dl color entry */
    result = dlc_next_colorant(&page->dlc_erase, &dliter, &ci, &colval);
  }

  /* No more ordinary colorants, but could be special case */
  switch ( result ) {
    case DLC_ITER_ALLSEP:
      /* Need to check default all sep value as well */
      if ( fZeroIsClearOrWhite )
        erasepgzero = erasepgzero
          || !ht_colorIsClear(spot, REPRO_TYPE_OTHER /* linework */,
                              ci, colval, gsc_getRS(gs->colorInfo));
      else
        erasepgzero = erasepgzero
          || !ht_colorIsSolid(spot, REPRO_TYPE_OTHER /* linework */,
                              ci, colval, gsc_getRS(gs->colorInfo));
      break;

    case DLC_ITER_ALL01:
      /* No need to test for erasepgscreen as color is degenerate by
       * definition
       */
      tint = dlc_check_black_white(&page->dlc_erase);
      if ( fZeroIsClearOrWhite )
        erasepgzero = erasepgzero || (tint != DLC_TINT_WHITE);
      else
        erasepgzero = erasepgzero || (tint != DLC_TINT_BLACK);
      break;

    case DLC_ITER_COLORANT:
      HQFAIL("quit colorant search loop with colorant in hand");
      /*FALLTHROUGH */
    case DLC_ITER_NOMORE:
      break;

    case DLC_ITER_NONE:
      HQFAIL("found NONE separation for erase color");
      break;

    default:
      HQFAIL("unknown result from colorant search");
      break;
  }
  lobj->dldata.erase.with0   = (uint8)erasepgzero;
}


/**
 * Add an erase listobject to every band.
 */
Bool adderasedisplay(DL_STATE* page, Bool newPage)
{
  LISTOBJECT *lobj;
  STATEOBJECT thestate;
  dbbox_t bbox;

  /* May get called before DL is fully initalised, if so just return success. */
  if ( device_current_addtodl == NULL )
    return TRUE;

  bbox_store(&bbox, 0, 0, page->page_w - 1, page->page_h - 1);
  if ( !make_listobject(page, RENDER_erase, &bbox, &lobj) )
    return FALSE;

  /* The real state is only set up in update_erase. */
  thestate = stateobject_new(page->default_spot_no);
  lobj->objectstate = (STATEOBJECT*)dlSSInsert(page->stores.state,
                                               &thestate.storeEntry, TRUE);
  if (lobj->objectstate == NULL) {
    free_listobject(lobj, page);
    return FALSE;
  }
  /* When newPage is TRUE, we create a real erase, otherwise we need a
   * "read back a partially painted band" erase. */
  lobj->dldata.erase.newpage = (uint8)newPage;

  return add_listobject(page, lobj, NULL);
}


/* Install the erase colour for the current raster style in the erase obj */
Bool update_erase(DL_STATE *page)
{
  corecontext_t *context = get_core_context_interp();
  DLREF *erase_ref;
  LISTOBJECT* erase;
  Bool fSuccess = TRUE;
  LateColorAttrib *lateColorAttribs = NULL;

  /* Erase object is first in DL link list */
  erase_ref = dl_get_orderlist(page);
  HQASSERT(erase_ref, "No erase object");
  erase = dlref_lobj(erase_ref);

  /* This colorInfo contains the right screen dict, but an old spot. */
  if ( !gs_applyEraseColor(context, FALSE,
                           &page->dlc_erase, &lateColorAttribs, FALSE)
       || !gs_applyEraseColor(context, FALSE,
                              &page->dlc_knockout, &lateColorAttribs, TRUE)
       || !dlc_to_lobj(page->dlc_context, erase, &page->dlc_erase) )
    fSuccess = FALSE;

  /* Install page default screen into erase obj. */
  if ( fSuccess ) {
    STATEOBJECT new_state;

    new_state = stateobject_new(page->default_spot_no);
    /* Erases don't require clips, patterns, tags, transparency, but do need
       late color management. */
    new_state.lateColorAttrib =
      (LateColorAttrib*)dlSSInsert(page->stores.latecolor,
                                   &lateColorAttribs->storeEntry, TRUE);
    erase->objectstate = (STATEOBJECT*)dlSSInsert(page->stores.state,
                                                  &new_state.storeEntry, TRUE);
    if ( new_state.lateColorAttrib == NULL || erase->objectstate == NULL )
      fSuccess = FALSE;
  }
  if ( fSuccess )
    dlerase_setdata(page, gstateptr, erase);

  return fSuccess;
}


/**
 * Perform the clean-up (partial deallocation) required by a partial paint.
 * This is also called when erasepage appears in a nested group or HDL context,
 * to prevent destroying objects under construction.
 */
static void dl_free_partial(DL_STATE *page)
{
  HDL *baseHdl;

  HQASSERT(page != NULL, "page cannot be NULL");
  HQASSERT(page->currentHdl != NULL,  "baseHDL cannot be NULL");

  /* On rare occasions, we might have a partial paint at a point where
     the currentdlstate has been setup but it not yet referenced from
     any e.g. HDL object. Explicitly preserve the currentdlstate as we
     will probably need it, and it might be assumed elsewhere that once
     a setg is done, currentdlstate is valid. */
  if (page->currentdlstate != NULL)
    dlSSPreserve(page->stores.state, &page->currentdlstate->storeEntry, TRUE);

  /* We rebuild the spotlist during the preserve process. */
  spotlist_init(page, FALSE /*partial*/) ;

  /* Attempt to free-up as much as possible in the DL hierarchy. */
  baseHdl = hdlBase(page->currentHdl);
  if ( hdlCleanupAfterPartialPaint(&baseHdl) )
    HQFAIL("Should not have removed entire DL hierarchy");
  HQASSERT(baseHdl != NULL && page->currentHdl != NULL,
           "Open HDLs should not be destroyed");

  /* Free as much as possible in the various dlSS stores. This can only be done
     after all interested parties have had a chance to preserve the state
     objects they use. Some of the free calls rely on other bits of state
     being present, so the order of these calls is important. Ensure the state
     free is last as some of the others rely on that being present. */
  dlSSFree(page->stores.nfill);
  dlSSFree(page->stores.clip);
  dlSSFree(page->stores.gstag);
  dlSSFree(page->stores.pattern);
  dlSSFree(page->stores.patternshape);
  dlSSFree(page->stores.softMask);
  dlSSFree(page->stores.latecolor);
  dlSSFree(page->stores.transparency);
  dlSSFree(page->stores.hdl);
  dlSSFree(page->stores.pcl);
  dlSSFree(page->stores.state);

  /* Reset the color state for the partial paint. */
  gsc_colorStatePartialReset();

  /* Now remove anything in the dl cache that is unreferenced, to
     minimise dl pool fragmentation. */
  dcc_purge(page);
}

/**
 * Create each of the state stores.
 *
 * Note that the state stores are never explicitly destroyed; they are
 * destroyed implicitly when the display pool is cleared.
 */
static Bool createDLStores(DlSSSet* set, mm_pool_t *pools)
{
  uint32 sz;

  /*
   * DL Store sizes
   * See change 61385.
   *
   * Allow the size of the dl-store hash tables to be configured.
   * They are currently set to sizes for the worst case, and a much lower
   * value is OK in low-memory configurations. A size of 211 seems like more
   * than enough for those sorts of jobs.
   */
  if ( low_mem_configuration() )
    sz = 211;
  else
    sz = 30011;

  set->state = dlSSNew(pools, sz, set,
                       stateObjectCopy, stateObjectDelete,
                       stateObjectHash, stateObjectSame,
                       stateObjectPreserveChildren);

  set->nfill = dlSSNew(pools, 128, set,
                       nfillCacheCopy, nfillCacheDelete,
                       nfillCacheHash, nfillCacheSame,
                       NULL);

  if ( low_mem_configuration() )
    sz = 211;
  else
    sz = 7919;
  set->clip = dlSSNew(pools, sz, set,
                      clipObjectCopy, clipObjectDelete,
                      clipObjectHash, clipObjectSame,
                      clipObjectPreserveChildren);

  set->gstag = dlSSNew(pools, 16, set,
                       NULL, gsTagDelete,
                       gsTagHash, gsTagSame, NULL);

  set->pattern = dlSSNew(pools, 16, set,
                         patternObjectCopy, patternObjectDelete,
                         patternObjectHash, patternObjectSame,
                         patternObjectPreserveChildren);

  set->patternshape = dlSSNew(pools, 16, set,
                              patternshape_copy, patternshape_delete,
                              patternshape_hash, patternshape_same,
                              NULL);

  set->softMask = dlSSNew(pools, 64, set,
                          smAttribCopy, smAttribDelete,
                          smAttribHash, smAttribSame,
                          smAttribPreserveChildren);

  set->latecolor = dlSSNew(pools, 256, set,
                           lateColorAttribCopy, lateColorAttribDelete,
                           lateColorAttribHash, lateColorAttribSame,
                           lateColorAttribPreserveChildren);

  set->transparency = dlSSNew(pools, 256, set,
                              tranAttribCopy, tranAttribDelete,
                              tranAttribHash, tranAttribSame,
                              tranAttribPreserveChildren);

  set->hdl = dlSSNew(pools, 32, set,
                     hdlStoreCopy, hdlStoreDelete,
                     hdlStoreHash, hdlStoreSame,
                     hdlStorePreserveChildren);

  if ( low_mem_configuration() )
    sz = 211;
  else
    sz = 256;
  set->pcl = dlSSNew(pools, sz, set,
                     pclAttribCopy, pclAttribDelete,
                     pclAttribHash, pclAttribSame,
                     pclAttribPreserveChildren);


  /* Return TRUE if all stores were created successfully. */
  return (set->state != NULL &&
          set->nfill != NULL &&
          set->clip != NULL &&
          set->gstag != NULL &&
          set->pattern != NULL &&
          set->patternshape != NULL &&
          set->softMask != NULL &&
          set->latecolor != NULL &&
          set->transparency != NULL &&
          set->hdl != NULL &&
          set->pcl != NULL);
}

/**
 * Query the current virtual device color space name and id from the
 * inputpage.
 */
void dlVirtualDeviceSpace(const DL_STATE *page, int32 *name_id,
                          COLORSPACE_ID *space_id)
{
  if ( space_id != NULL )
    *space_id = page->virtualDeviceSpace;

  if ( name_id != NULL ) {
    switch ( page->virtualDeviceSpace ) {
    case SPACE_DeviceCMYK:
      *name_id = NAME_DeviceCMYK;
      break;
    case SPACE_DeviceRGB:
      *name_id = NAME_DeviceRGB;
      break;
    case SPACE_DeviceGray:
      *name_id = NAME_DeviceGray;
      break;
    default:
      HQFAIL("dlVirtualDeviceSpace - Unexpected virtual device space.");
      break;
    }
  }
}

/**
 * Allow partial painting providing we're in a page group and not a sub-group.
 *
 * Partial painting must be the last resort in case transparent or overprinted
 * objects follow.  In this case an error may have to be given to avoid
 * incorrect output.
 */
Bool dlAllowPartialPaint(const DL_STATE *page)
{
  HQASSERT(!page->opaqueOnly ||
           hdlPurpose(page->currentHdl) == HDL_BASE ||
           hdlPurpose(page->currentHdl) == HDL_PAGE,
           "Should not have nested Groups/HDLs for PCL");

  return ( page->currentGroup == NULL ||
           groupGetUsage(page->currentGroup) == GroupPage );
}


/**
 * Abandon all HDLs and subclasses in the HDL store.
 */
static Bool hdlAbandonStore(void *entry, void *data)
{
  HDL *hdl = entry;
  Group *group;

  UNUSED_PARAM(void *, data);

  group = hdlGroup(hdl);
  if ( group != NULL )
    groupAbandon(group);

  return TRUE;
}

/**
 * Take a snapshot of the band tails in the base HDL.
 */
static void dlTakeSnapshot(const DL_STATE *page)
{
  HDL *hdl;

  if ( page->currentGroup != NULL )
    hdl = groupHdl(groupBase(page->currentGroup));
  else
    hdl = dlPageHDL(page);
  HQASSERT(hdl != NULL, "No page HDL to take snapshot of");

  hdlTakeBandTailSnapshot(hdl);
}

/**
 * Restore the band tails in the base HDL using a previously taken snapshot.
 */
static void dlRestoreFromSnapshot(const DL_STATE *page)
{
  HDL *hdl;

  if ( page->currentGroup != NULL )
    hdl = groupHdl(groupBase(page->currentGroup));
  else
    hdl = dlPageHDL(page);
  HQASSERT(hdl != NULL, "No page HDL to restore snapshot of");

  hdlRestoreBandTails(hdl);
}

/* Create a color state for the back-end color transforms. */
static Bool dl_color_state_create(DL_STATE *page)
{
  HQASSERT(page->colorState == NULL, "Color state exists already");
  return gsc_colorStateCreate(&page->colorState);
}

void dl_color_state_destroy(DL_STATE *page)
{
  if ( page->colorState != NULL ) {
    /* Release the back-end colorInfo refs in the groups and imexpanders which
       are owned by the back-end colorState, and then destroy the colorState. */
    im_expand_colorinfo_free(page, TRUE /* on-the-fly conversion possible */);
    groupAbandonAll(page);
    gsc_colorStateDestroy(&page->colorState);
  }
}


static void dl_clear_pools(DL_STATE *page);

static void dl_free_genocide(DL_STATE *page)
{
  Bool first ;

  /* Destroy the color state for the back-end transforms. */
  dl_color_state_destroy(page);

  /* Destroy separation omission data. This is OK for partial paint with
     pool clearing, because it was all derived from the rasterstyle anyway.
     The current omit flags for the partial paint are preserved across
     the paint in the rasterstyle. */
  finish_separation_omission(page);

  /* Do whatever is required for images on an erase (or partial erase). */
  dl_image_finish(page);

  /* Cleanup the permanent memory in the HDL stack, and remove virtual
     raster style, if the last DL had a base group. */
  if ( page->stores.hdl )
    (void)dlSSForall(page->stores.hdl, hdlAbandonStore, NULL) ;

  /* Set the reserved bands bitmask to zero. Band flags will be or'ed in as
     needed for patterns, clipping, masked images, etc, and then the required
     number of bands will be allocated in rendering. */
  page->reserved_bands = 0 ;

  /* Clear the surfaces used mask */
  page->surfaces_used = 0 ;
  page->all_hdls = NULL;
  page->dlc_context = NULL;

  /* Just clear the erase color, the pool will be dropped at the end of this
     function. */
  dlc_clear(&page->dlc_erase) ;
  dlc_clear(&page->dlc_knockout) ;

  page->highest_sheet_number = -1 ;

  if ( page->erase_type != DL_ERASE_PARTIAL )
    irr_free(page) ;

  spotlist_init(page, TRUE /*not partial*/) ;

  page->currentdlstate = NULL ;
  page->currentHdl = NULL ;
  page->targetHdl = NULL ;
  page->currentGroup = NULL ;
  page->groupDepth = 0;

  page->force_deactivate = FALSE ;

  /* Tell the halftone system that this particular page is done. */
  ht_retire_dl(page->eraseno);

  /* If this was the outputpage, then we can assume that all previous pages
     have been flushed. This function is only called when we're about to bump
     the eraseno. */
  first = (page == outputpage_lock()) ; outputpage_unlock() ;
  if ( first )
    ht_flush_dl(page->eraseno) ;

  /* Clear the DL state store pointers */
  HqMemZero(&page->stores, sizeof(DlSSSet)) ;

  /* And finally, nuke the DL pool contents: */
  dl_clear_pools(page);
}

/** \brief Erase the DL immediately.

    \param page       The DL to erase.

    This is the internal function used to tear down the memory allocations
    for a display list, and reset it to a suitable state of constructing the
    next page (or continuing the current partial page, if appropriate).

    This function is called from the asynchronous DL finaliser used for pages
    in the render queue, and also for the synchronous DL finalisation (either
    dl_pipeline_flush() when flushing and erasing rendered pages, or
    dl_clear_page() from an erasepage or setpagedevice).

    \note Do \b not call this function elsewhere. Use dl_clear_page()
    instead. This function does not join the page's construction or render
    tasks. */
static void dl_erase(DL_STATE *page)
{
  OBJECT notvm_null = OBJECT_NOTVM_NULL ;
  int i;

  /** \todo ajcd 2011-03-09: We'd like to be able to asynchronously purge
      rotated image tile store, useless fonts, halftone forms and chptrs
      here, but it's currently too dangerous (it needs thread-safe data
      structures, or locking around the store iterations). These would be:

      purge_tcache()
      fontcache_purge_useless()
  */

  bd_sharedFree(&page->backdropShared);
  HQASSERT(page->regionMap == NULL,
           "Region map shared should not be set when clearing display list") ;

#ifdef DEBUG_BUILD
  if ( (debug_dlstore & DEBUG_DLSTORE_METRICS) != 0 ) {
    dlSSMetrics(page->stores.state, "stateobject", 2);
    dlSSMetrics(page->stores.nfill, "nfill", 1);
    dlSSMetrics(page->stores.clip, "clip", 2);
    dlSSMetrics(page->stores.gstag, "gstag", 1);
    dlSSMetrics(page->stores.pattern, "pattern", 1);
    dlSSMetrics(page->stores.patternshape, "patternshape", 1);
    dlSSMetrics(page->stores.softMask, "soft mask", 1);
    dlSSMetrics(page->stores.latecolor, "late color", 1);
    dlSSMetrics(page->stores.transparency, "transparency", 1);
    dlSSMetrics(page->stores.hdl, "HDL", 3);
    dlSSMetrics(page->stores.pcl, "PCL", 1);
  }
#endif

  /* If this page was introduced to the surface API, retire it properly. */
  if ( page->erase_type != DL_ERASE_BEGIN &&
       page->surfaces != NULL && page->surfaces->retire_dl != NULL ) {
    surface_handle_t handle ;
    Bool continues = (page->erase_type == DL_ERASE_PARTIAL ||
                      page->erase_type == DL_ERASE_PRESERVE) ;
    handle.page = page->sfc_page ;
    page->surfaces->retire_dl(&handle, page, continues) ;
    page->sfc_page = continues ? handle.page : NULL ;
  }

  /* We've now got rid of all previous tasks that reference this DL, so it's
     safe to dispose of the data. */
  switch ( page->erase_type ) {
  case DL_ERASE_ALL: /* showpage with separate input/outputpage. */
    HQASSERT(page->im_imagesleft == 0, "Page still constructing image");
    if ( page->rippedtodisk )
      erase_page_buffers(page);
    HQASSERT(page->highest_sheet_number < 0,
             "May have left pagebuffer files undeleted") ;

    dl_free_genocide(page) ;

    /* The rasterstyle was allocated from DL memory, which has now been
       wiped. */
    page->hr = NULL ;
    page->rippedsomethingsignificant = FALSE ;
    page->rippedtodisk = FALSE;
    page->highest_sheet_number = -1 ;

    /* Remove the IRR pgb device if set. */
    irr_pgb_remove(page);

    /* Reset the bounding box for the current page. This is not redundant;
       the BBox of the base HDL includes background separation objects, this
       does not. */
    bbox_clear(&page->page_bb) ;

    /* Reset the page group details. */
    page->page_group_details.knockout = DEFAULT_PAGE_GROUP_KNOCKOUT;
    page->page_group_details.colorspace = notvm_null;
    for (i = 0; i < NUM_CSA_SIZE; i++) {
      page->page_group_details.csa[i] = notvm_null;
    }

    /* This page is now done, so move on the current render page. Note that
       we don't change the current job's first_dl pointer, this is left until
       we flush the DL pipeline.

       Pages can be erased out of order if the pipeline is full when a
       cancellation is issued. */
    multi_mutex_lock(&outputpage_mutex) ;
    if ( page == outputpage ) {
      multi_mutex_lock(&inputpage_mutex) ;
      while ( outputpage != inputpage ) {
        if ( ++outputpage == &dl_pages[NUM_DISPLAY_LISTS] )
          outputpage = &dl_pages[0] ;
        if ( outputpage->erase_type != DL_ERASE_GONE )
          break ;
      }
      multi_mutex_unlock(&inputpage_mutex) ;
    } else { /* Out-of-order erase, just mark it as clear. */
      page->erase_type = DL_ERASE_GONE ;
    }
    multi_mutex_unlock(&outputpage_mutex) ;

    if ( page->render_resources != NULL ) /* Free all page resources. */
      resource_requirement_release(&page->render_resources) ;

    break ;
  case DL_ERASE_BEGIN: /* Failed to construct new page. */
  case DL_ERASE_GONE:  /* Page was already erased asynchronously. */
  case DL_ERASE_CLEAR: /* erasepage/setpagedevice/sync showpage. */
    HQASSERT(page->im_imagesleft == 0, "Page still constructing image");
    if ( page->rippedtodisk )
      erase_page_buffers(page);
    HQASSERT(page->highest_sheet_number < 0,
             "May have left pagebuffer files undeleted") ;

    page->rippedsomethingsignificant = FALSE ;
    page->rippedtodisk = FALSE;
    page->highest_sheet_number = -1 ;

    /* Reset the bounding box for the current page. This is not redundant;
       the BBox of the base HDL includes background separation objects, this
       does not. */
    bbox_clear(&page->page_bb) ;

    /* Reset the page group details. */
    page->page_group_details.knockout = DEFAULT_PAGE_GROUP_KNOCKOUT;
    page->page_group_details.colorspace = notvm_null;
    for (i = 0; i < NUM_CSA_SIZE; i++) {
      page->page_group_details.csa[i] = notvm_null;
    }

    /* Reset the ColorCache. Don't need to release dl_colors as these
       are freed as part of the pool. */
    gsc_colorStateFinish(FALSE);

    dl_free_genocide(page) ;

    if ( page->render_resources != NULL ) /* Free all page resources. */
      resource_requirement_release(&page->render_resources) ;

    break ;
  case DL_ERASE_PARTIAL: /* partial paint killing DL pools. */
    HQASSERT(page->im_imagesleft == 0, "Page still constructing image");

    /* Reset the ColorCache. Don't need to release dl_colors as these
       are freed as part of the pool. */
    gsc_colorStateFinish(FALSE);

    dl_free_genocide(page) ;

    if ( page->render_resources != NULL ) /* Free all page resources. */
      resource_requirement_release(&page->render_resources) ;

    break ;
  case DL_ERASE_PRESERVE: /* partial paint keeping DL pools. */
    /* If we have a vignette in progress, preserve its stateobjects.
       This *may* become unnecessary when the vignette switches over to
       using a HDL. Partial paint for backdrop rendering is incomplete,
       so the backdrop render test here may change. */
    /** \todo ajcd 2011-03-09: Yuck, this is really a front-end test. */
    if ( analyzing_vignette() )
      vn_preservestate(page);

    if (in_execform()) {
      /* Preserve form's partial hdl */
#if 0
      /* Simplest way to detect a pp while processing a form for now */
      monitorf((uint8*)"... while processing a form.\n");
#endif
      preserve_execform(page);
    }

    dl_free_partial(page);

    break ;
  case DL_ERASE_COPYPAGE: /* LL2 copypage and continue with same DL. */
    HQASSERT(page->im_imagesleft == 0, "Page still constructing image");
    if ( page->rippedtodisk )
      erase_page_buffers(page);
    HQASSERT(page->highest_sheet_number < 0,
             "May have left pagebuffer files undeleted") ;

    page->rippedsomethingsignificant = FALSE ;
    page->rippedtodisk = FALSE;
    page->highest_sheet_number = -1 ;

    break ;
  default:
    HQFAIL("Invalid erase type") ;
  }
}

/** \brief Asynchronously erase a display list.
 *
 * This is the finaliser for the join task used to asynchronously erase a
 * display list. The finaliser for this task does all the work, there is no
 * task function.
 */
static void dl_erase_finalise(corecontext_t *context, void *args)
{
  HQASSERT(context != NULL, "No core context") ;

  /* Do nothing if there are no args; this means that the task was cancelled
     before having join responsibility for the page group transferred to it.
     The task that tried to hand over is responsible for both joining and
     erasing the DL if that happens. */
  if ( args == NULL )
    return ;

  /* Join the page group, so nothing is referencing the DL objects. The args
     passed into the finaliser is the page group (containing the construction
     and rendering tasks for a page), not the erase group (containing just
     this finaliser function). The erase group will be joined and removed by
     dl_pipeline_flush(). */
  if ( !task_group_join(args, context->error) ) {
    /* Since this finaliser was tasked handed responsibility for the join, we
       know that its failure indicates an asynchronous render error, and not
       an erasepage or setpagedevice cancellation of a partially-constructed
       page. Propagate the error to the job group, cancelling all other pages
       in flight. */
    task_group_cancel(context->page->job->task_group,
                      context->error->old_error) ;
  }

  dl_erase(context->page) ;
}

/** \brief Flush any pages that may have been made invalid by a page change.
*/
static void dl_flushed_page(void)
{
  dl_erase_nr eraseno ;

  HQASSERT(IS_INTERPRETER(), "Page cache flush not from interpreter") ;

  /* Only flush the caches if we've moved the outputpage along. */
  eraseno = outputpage_lock()->eraseno ; outputpage_unlock() ;
  if ( eraseno != dl_last_flush ) {
    /** \todo ajcd 2011-03-07: We could move these purges to
        dl_erase() and get rid of this function if we used
        synchronised lists for the entries/hash buckets. */

    /* Purge image tiles that cannot be re-used. */
    purge_tcache(eraseno) ;

    /* purge fonts that can no longer be remapped in the font cache. */
    /** \todo ajcd 2011-03-07: We could move this to dl_erase() if
        we used synchronised lists for the hash buckets. */
    fontcache_purge_useless(eraseno);

    dl_last_flush = eraseno ;
  }
}

Bool dl_pending_erase(DL_STATE *page)
{
  task_t *erase_task = NULL ;
  task_group_t *erase_group = NULL ;
  corecontext_t *context = get_core_context_interp() ;
  error_context_t error = ERROR_CONTEXT_INIT, *olderror ;

  HQASSERT(page, "No display list") ;
  HQASSERT(IS_INTERPRETER(), "Only interpreter should modify DL pending task") ;
  HQASSERT(page->next_task != NULL, "DL still has no next task") ;
  HQASSERT(page->all_tasks, "DL has no task group") ;

  /* Suppress errors from this function and sub-functions. We're able to cope
     with failure in the caller. */
  olderror = context->error ;
  context->error = &error ;

  task_group_close(page->all_tasks) ;

  /* If doing an asynchronous page hand-off, create a group for the erase
     task, and hand over responsibility for joining the page group to it. */
  if ( task_group_create(&erase_group, TASK_GROUP_ERASE,
                         page->job->task_group, NULL) ) {
    task_group_ready(erase_group) ;
    if ( task_create(&erase_task, render_task_specialise, page,
                     NULL /*no worker*/, NULL /*args*/, &dl_erase_finalise,
                     erase_group, SW_TRACE_DL_ERASE) ) {
      if ( task_group_set_joiner(page->all_tasks, erase_task, page->all_tasks) ) {
        task_group_release(&page->all_tasks) ;
        page->all_tasks = erase_group ;
        /* If queueing the page, the erase task becomes this job's
           "previous" task, all subsequent pages will depend on it completing
           before they start. The erase task moves on outputpage, so we have
           to wait for it to complete, we cannot erase this page in parallel
           with rendering the next. */
        if ( page->job->previous != NULL )
          task_release(&page->job->previous) ;
        task_release(&page->next_task) ;
        task_ready(erase_task) ;
        task_group_close(erase_group) ;
        page->job->previous = erase_task ;
        /* The page's DL group is now the erase group. dl_pipeline_flush()
           uses this reference to ensure that all of the DL tasks are
           complete when it needs a new pipeline slot. */
        context->error = olderror ;
        return TRUE ;
      }
      task_release(&erase_task) ;
    }

    task_group_cancel(erase_group, newerror_context(context->error)) ;
    (void)task_group_join(erase_group, &error) ;
    task_group_release(&erase_group) ;
  }

  context->error = olderror ;

  return FALSE ;
}

/** Join and release the first unflushed page in the DL pipeline. */
static void join_and_release_flushpage(Bool *ok, Bool *no_messages)
{
  error_context_t error = ERROR_CONTEXT_INIT ;

  HQASSERT(IS_INTERPRETER(), "Cannot flush page from non-interpreter") ;
  HQASSERT(flushpage->all_tasks != NULL,
           "Erased page in DL pipeline doesn't have task group") ;

  /* If the page is asynchronous, it will have rendered but not flushed, so
     the erase finaliser itself has run, and will have propagated any error
     status to the erase group. We just need to join the erase group before
     releasing it.

     If the page is synchronous (i.e., flushpage == inputpage), then the
     task group is the page group, and we couldn't or didn't create an erase
     group.

     Catch any error raised by the asynchronous page render. The skin has
     already had a chance to catch render errors and indicate that they can
     be ignored, via the SWEVT_RENDER_ERROR event. */
  if ( !task_group_join(flushpage->all_tasks, &error) ) {
    /* We cancel the job group if we notice an error during asynchronous
       finalisation. However, it's also possible that this function is called
       for synchronous joins, when we don't have a separate erase task. So we
       propagate errors to the whole job here too; this should be a no-op if
       the job is already cancelled. */
    task_group_cancel(flushpage->job->task_group, error.old_error) ;

    /* Propagate the error to the interpreter context, but only if the failed
       page is in the same job. This will stop the interpreter from
       continuing to construct pages when an asynchronous render failed. */
    if ( flushpage->job == inputpage->job && *no_messages ) {
      *ok = error_handler(error.old_error) ;
    } else {
      /* Fake a PS error report if we can't call the PS handler [65744] */
      uint8 * err = (uint8*) "" ;
      size_t  len = 0 ;

      if (error.old_error) {
        NAMECACHE * name ;
        name = &system_names[NAME_dictfull + error.old_error - DICTFULL] ;
        err = theICList(name) ;
        len = theINLen(name) ;
      }

      /* NB hard-wiring "renderbands" here is a massive assumption that will
         not necessarily always be true. */
      emonitorf(flushpage->job->timeline, MON_CHANNEL_PROGRESS,
                /* ultimately the type will come from the error context... */
                MON_TYPE_ERROR + (error.old_error << 16),
                (uint8*)"%%%%[ Error: %.*s; OffendingCommand:"
                " renderbands; Info: page %u ]%%%%\n",
                len, err, flushpage->pageno) ;
      *no_messages = TRUE ;
    }
  }

  /* If an asynchronous erase task was successfully created,
     dl_pending_erase() transferred this reference to the job. Otherwise, we
     need to erase this page now. */
  if ( flushpage->next_task != NULL ) {
    dl_erase(flushpage) ;
    task_release(&flushpage->next_task) ;
  }

  task_group_release(&flushpage->all_tasks) ;

  /* Release the job from this page, but only if it's not the inputpage. The
     job object carried around in inputpage is the primary reference to the
     core job, we need it to remain valid. */
  if ( flushpage != inputpage )
    corejob_release(&flushpage->job) ;
}

Bool dl_pipeline_flush(int32 depth, Bool no_messages)
{
  /* If we've run out of pipeline slots, then we need to wait for jobs at the
     start of the rendering queue to flush. We need to lock outputpage while
     we check whether we're done, or the task group may be snatched out
     from beneath our feet. The DL_STATE array is arranged so that there is
     always a slot for the next page, so inputpage will never catch up to
     flushpage (however, flushpage and outputpage may catch up to inputpage,
     indicating there are no asynchronous pages in flight). */
  DL_STATE *renderpage ;
  Bool ok = TRUE ;

  HQASSERT(IS_INTERPRETER(), "Cannot flush DL pipeline from non-interpreter") ;
  HQASSERT(depth >= 0 && depth < NUM_DISPLAY_LISTS,
           "Invalid pipeline depth") ;

  renderpage = outputpage_lock() ;
  while ( inputpage - renderpage >= depth ||
          renderpage - inputpage >= NUM_DISPLAY_LISTS - depth ) {
    outputpage_unlock() ;

    /* We need at least one slot in the pipeline, otherwise we wouldn't have
       come into the loop. If the first page has already run asynchronously,
       the erase group join in this function won't change renderpage. If this
       page has not yet rendered, this will force it to render and to move on
       renderpage. If this was a synchronous page render forced by a depth
       of 0, then renderpage won't be moved on, but the loop will terminate
       because flushpage will equal inputpage below. */
    join_and_release_flushpage(&ok, &no_messages) ;

    renderpage = outputpage_lock() ;

    if ( flushpage == inputpage ) {
      HQASSERT(renderpage == inputpage, "Render page passed inputpage") ;
      break ; /* Terminate special case loop to flush inputpage too. */
    }

    /* Account for the page we flushed above. */
    if ( ++flushpage == &dl_pages[NUM_DISPLAY_LISTS] )
      flushpage = &dl_pages[0] ;
  }

  if ( renderpage == inputpage ) {
    /* There are no more asynchronous pages to render, so this DL slot will
       be the next slot used by rendering. Set it to allow the PGB proxy to
       flush straight through to the device. Deliberately ignore errors from
       the flush, most PGB param settings are in stopped contexts usually. */
    (void)pgbproxy_setflush(inputpage, TRUE) ;
  }

  outputpage_unlock() ;

  /* Need to catch up asynchronously rendered pages again (they may have
     completed after we joined and flushed the last batch), so we don't
     overrun flushpage with inputpage. */
  while ( flushpage != renderpage ) {
    join_and_release_flushpage(&ok, &no_messages) ;

    if ( ++flushpage == &dl_pages[NUM_DISPLAY_LISTS] )
      flushpage = &dl_pages[0] ;
  }

  /* We always follow the call to dl_pipeline_flush() with a call to
     dl_begin_page() for the new inputpage. Defer flushing any unsynchronised
     caches until we build the new page. */

  return ok ;
}

/** \brief Common front-end DL actions when handing off page into rendering or
    erasing.

    \param page        The DL page we're going to hand-off or destroy.
    \param erase_type  What's going to be done to the page.

    \retval TRUE     If the page was finalised for rendering properly.
    \retval FALSE    If the page was not prepared to render properly.

    This function should be called after dl_handoff_prepare() if preparing for
    asynchronous rendering.
*/
static Bool dl_changing_page(DL_STATE *page, dl_erase_t erase_type)
{
  HDL *hdl ;

  HQASSERT(IS_INTERPRETER(), "Change page not called by interpreter") ;

  switch ( erase_type ) {
  case DL_ERASE_ALL: /* showpage with separate input/outputpage. */
  case DL_ERASE_BEGIN: /* Failed to construct new page. */
  case DL_ERASE_GONE:  /* Page was already erased asynchronously. */
  case DL_ERASE_CLEAR:   /* erasepage/setpagedevice. */
    /* This is a genuine erase, so kill off any vignette in progress. This
       should remove the vignette candidate (if any) from the HDL stack. */
    /** \todo ajcd 2011-03-08: This may not be pipeline-safe. */
    abort_vignette(page);
    /*@fallthrough@*/
  case DL_ERASE_PARTIAL: /* partial paint killing DL pools. */
    HQASSERT(!analyzing_vignette(),
             "vignette candidate should not be present.");

    /** \todo ajcd 2011-03-08: This is a temporary loop, until targets
        (12145) are implemented. It's used to close off the DL probes when
        killing it off in one go. It's arguable whether it should be just
        in the DL_ERASE_ALL case, but since a new page will be constructed,
        building new HDLs, this seems right for now. */
    for ( hdl = page->currentHdl ; hdl != NULL ; hdl = hdlParent(hdl) ) {
      Group *group = hdlGroup(hdl);

      probe_end(SW_TRACE_DL_HDL, (intptr_t)hdl);
      if ( group != NULL )
        probe_end(SW_TRACE_DL_GROUP, (intptr_t)group);
    }

    /** \todo ajcd 2011-01-31: These should be target attributes, they
        shouldn't be directly in the DL_STATE. */
    page->targetHdl = page->currentHdl ;
    page->currentdlstate = NULL ;
    page->groupDepth = 0 ;

    /* We're setting up for a new DL, so we can't retain the current color. */
    if ( page->dlc_context != NULL )
      dlc_release(page->dlc_context, dlc_currentcolor(page->dlc_context));

    dlpurge_reset();

    /* Clear everything in all gstates (as of 18th March 2011 only gstags) */
    clear_gstate_dlpointers() ;

    /** \todo ajcd 2011-03-24: Can these be moved before the groupClose()
        above, into the DL_ERASE_ALL case?
     */
    if ( erase_type == DL_ERASE_ALL ) {
      /* Reset the ColorCache. Don't need to release dl_colors as these
         are freed as part of the pool. */
      gsc_colorStateFinish(TRUE);
    }

    /* After clearing currentGroup reset recombine whilst preserving any
       known separation state. */
    if ( !rcbn_reset() )
      return FALSE;

    break ;
  case DL_ERASE_PRESERVE: /* partial paint keeping DL pools. */
    /* Clear everything in all gstates (as of 18th March 2011 only gstags) */
    clear_gstate_dlpointers() ;

    /*@fallthrough@*/
  case DL_ERASE_COPYPAGE: /* LL2 copypage and continue with same DL. */
    break ;
  default:
    HQFAIL("Invalid erase type") ;
  }

  return TRUE ;
}

/* Prepare next page as inputpage, so we can render current page
   asynchronously. */
Bool dl_handoff_prepare(/*@notnull@*/ /*@in@*/ /*@out@*/ DL_STATE **nextpage,
                        dl_erase_t erase_type)
{
  HQASSERT(IS_INTERPRETER(), "DL handoff not in interpreter") ;
  HQASSERT(CoreContext.page == inputpage, "Not handing off inputpage") ;

  if ( erase_type == DL_ERASE_ALL ) {
    DL_STATE *next = inputpage ;
    HDL_LIST *hlist;

    if ( ++next == &dl_pages[NUM_DISPLAY_LISTS] )
      next = &dl_pages[0] ;

    /* Make a copy of the device rasterstyle which involves copying from temp
       pool/PSVM objects to page pool memory. This must to be done after
       trapPrepare (which may add separations) but before groupNewBackdrops
       (where more colorInfos are created). */
    next->hr = inputpage->hr ;
    if ( !guc_copyRasterStyle(inputpage->dlpools, next->hr, &inputpage->hr) )
      return FALSE;

    /* Reset all the references to the device RS in each group. */
    for ( hlist = inputpage->all_hdls; hlist != NULL; hlist = hlist->next ) {
      Group *group = hdlGroup(hlist->hdl);
      if ( group != NULL ) {
        if ( !groupResetDeviceRS(group) )
          return FALSE;
      }
    }

    omit_unpack_rasterstyle(inputpage);

    /* Prepare the next by copying the fields set by reset_pagedevice() into
       it. We don't need to do anything with the erase fields, they should have
       been reset when this page was last rendered, and we don't need to do
       anything with the begin page fields, because the immediately following
       dl_begin_page() will set them. */

    /* Note that we don't change the erase number, we'll let the new page
       construction do that for consistency with the synchronous DL render
       cases. */
    next->eraseno = inputpage->eraseno ;
    /* The next page starts back at the job level. */
    /** \todo ajcd 2011-07-23: Where do sub-jobs, RR scanning, etc., fit into
        this? Can we just go straight back to the job level, or do we need to
        stop at the PDF/XPS loops, etc? */
    next->timeline = inputpage->job->timeline ;
    next->forcepositive = inputpage->forcepositive ;
    next->irr.generating = inputpage->irr.generating ;
    next->pageno = inputpage->pageno ;
    next->sizedisplaylist = inputpage->sizedisplaylist ;
    next->sizedisplayfact = inputpage->sizedisplayfact ;
    next->sizefactdisplaylist = inputpage->sizefactdisplaylist ;
    next->sizefactdisplayband = inputpage->sizefactdisplayband ;
    next->page_w = inputpage->page_w ;
    next->page_h = inputpage->page_h ;
    next->xdpi = inputpage->xdpi ;
    next->ydpi = inputpage->ydpi ;
    next->band_l = inputpage->band_l ;
    next->band_l1 = inputpage->band_l1 ;
    next->band_lines = inputpage->band_lines ;
    next->scratch_band_size = inputpage->scratch_band_size;
    next->rle_flags = inputpage->rle_flags ;
    next->ScanConversion = inputpage->ScanConversion ;
    /** \todo ajcd 2011-03-16: Should the surface API have a reference count
        per page? */
    next->surfaces = inputpage->surfaces ;
    next->sfc_inst = inputpage->sfc_inst ;
    next->sfc_page = NULL ;
    next->virtualDeviceSpace = inputpage->virtualDeviceSpace ;
    next->virtualBlackIndex = inputpage->virtualBlackIndex ;
    next->deviceBlackIndex = inputpage->deviceBlackIndex ;
    next->fOmitHiddenFills = inputpage->fOmitHiddenFills ;
    next->framebuffer = inputpage->framebuffer ;
    next->job_number = inputpage->job_number ;
    next->trap_effort = inputpage->trap_effort ;
    /** \todo ajcd 2011-03-16: What should be done about the transferFunction
        OBJECT in these params? */
    next->colorPageParams = inputpage->colorPageParams ;
    next->imageParams = inputpage->imageParams ;
    next->job = corejob_acquire(inputpage->job) ;
    next->max_region_width = inputpage->max_region_width ;
    next->max_region_height = inputpage->max_region_height ;
    next->backdropAutoSeparations = inputpage->backdropAutoSeparations ;
    next->deviceROP = inputpage->deviceROP ;
    next->output_object_map = inputpage->output_object_map ;
    next->pclIdiomQueue.nparts = next->pclIdiomQueue.first = 0 ;
    next->pclIdiomQueue.last = NULL ;

    *nextpage = next ;
  } else { /* Not DL_ERASE_ALL, so re-using the current page */
    *nextpage = inputpage ;
  }

  /* Indicate that we're changing the current page. */
  if ( !dl_changing_page(inputpage, erase_type) ) {
    dl_handoff_commit(*nextpage, erase_type, FALSE) ;
    return FALSE ;
  }
  ht_handoff_dl(inputpage->eraseno, page_is_separations());
  return TRUE ;
}

/* Either commit a successful page handoff into the render pipeline, or undo
   a prepared inputpage after a failed handoff. */
void dl_handoff_commit(/*@notnull@*/ /*@in@*/ DL_STATE *nextpage,
                       dl_erase_t erase_type, Bool success)
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT(context->page == inputpage, "Not handing off inputpage") ;

  if ( success ) {
    multi_mutex_lock(&inputpage_mutex) ;
    inputpage->erase_type = erase_type ;
    inputpage->job->last_dl = nextpage ;
    /* dl pages form a ring buffer. The design is for the pgbproxy associated
     * with each one to be by default in the non-flushing state. This may not
     * be true if an interrupt occurred meaning the normal processing of the
     * DL did not occur. We therefore have to initialise the state to
     * non-flushing as we step around the ring buffer of DLs. No natural way
     * to do that in the pgbproxy code, so have to do it here when the
     * inputpage is stepped through the ring.
     */
    (void)pgbproxy_setflush(inputpage, FALSE);
    inputpage = context->page = nextpage ;
    multi_mutex_unlock(&inputpage_mutex) ;
  } else if ( erase_type == DL_ERASE_ALL ) {
    HQASSERT(nextpage != inputpage, "Asynchronous handoff uses inputpage") ;

    guc_discardRasterStyle(&inputpage->hr) ;
    inputpage->hr = nextpage->hr ;
    omit_unpack_rasterstyle(inputpage);

    corejob_release(&nextpage->job) ;
  }
}

/**
 * Populate any output resouces that will be needed but have not yet been
 * allocated.
 *
 * This is done on seeing the first marking object on a page, rather than when
 * the page is created. This ensures that a sequence of pagedevice calls,
 * which may cause allocation sizes to toggle back and forth, does not lead to
 * unnecessary fragmentation.
 */
static Bool populate_output_resources(const DL_STATE *page)
{
  requirement_node_t *bandvals;

  bandvals = requirement_node_find(page->render_resources,
                                   REQUIREMENTS_BAND_GROUP);
  HQASSERT(bandvals, "Lost BAND_GROUP requirements entry");
  /* Need one output band. */
  return requirement_node_setmin(bandvals, TASK_RESOURCE_BAND_OUT, 1);
}

Bool dl_begin_page(/*@notnull@*/ /*@in@*/ DL_STATE *page)
{
  HDL *hdl ;
  dl_erase_t erase_type ;
  Bool res = FALSE;
  Bool earlier_error;
  corecontext_t *context = get_core_context_interp();

  HQASSERT(IS_INTERPRETER(), "Begin page not called by interpreter") ;

  HQASSERT(page != NULL, "No page to reset") ;
  HQASSERT(page->next_task == NULL && page->all_tasks == NULL,
           "Didn't release previous page tasks") ;

  /* This is the erase_type that was used to destroy the DL. It determines
     what we need to setup in the new DL. */
  erase_type = page->erase_type ;

  /** \todo ajcd 2011-03-24: I don't like having these here, because they're
      actions to finish the previous page, rather than to build the new page.
      However, we can't remove auto separations in dl_changing_page(),
      because it's called before rendering a page, and we can't call
      guc_updateRealAndVirtualColorants() in dl_erase(), because it
      may perform allocation and returns a value. We don't currently have any
      function that's called after synchronous rendering. If we did, it would
      probably be called immediately before this function anyway. */
  if ( erase_type != DL_ERASE_COPYPAGE &&
       erase_type != DL_ERASE_PARTIAL &&
       erase_type != DL_ERASE_PRESERVE ) {
    guc_removeAutomaticSeparations(gsc_getRS(gstateptr->colorInfo)) ;
    if (!guc_updateEquivalentColorants(gsc_getTargetRS(gstateptr->colorInfo),
                                       COLORANTINDEX_ALL))
      return FALSE;
  }

#define return DO_NOT_return_goto_fail_INSTEAD!

  /* If not preserving individual DL objects, bump the erase number. */
  if ( erase_type != DL_ERASE_PRESERVE &&
       erase_type != DL_ERASE_COPYPAGE )
    ++page->eraseno ;

  probe_other(SW_TRACE_DL, SW_TRACETYPE_MARK, page->eraseno) ;

  page->erase_type = DL_ERASE_BEGIN ;

  /* Immediately flush any caches that have been invalidated. */
  dl_flushed_page() ;

  /* We always assume that the output surface will be used */
  dl_surface_used(page, SURFACE_OUTPUT) ;

  /* Default spot number must be setup before adding erase object */
  page->default_spot_no = gsc_getSpotno(gstateptr->colorInfo);
  ht_set_page_default(page->default_spot_no);

  /* Clear the PCL idiom queue. */
  page->pclIdiomQueue.nparts = page->pclIdiomQueue.first = 0 ;
  page->pclIdiomQueue.last = NULL ;

  switch ( erase_type ) {
    requirement_node_t *pagelimit, *bandvals, *pagevals;
  case DL_ERASE_ALL: /* Complete nuke of page */
    /*@fallthrough@*/
  case DL_ERASE_BEGIN: /* Failed to construct new page. */
  case DL_ERASE_GONE:  /* Page was already erased asynchronously. */
  case DL_ERASE_CLEAR: /* Erasepage/setpagedevice */
    HQASSERT(!page->rippedtodisk &&
             !page->rippedsomethingsignificant &&
             page->highest_sheet_number == -1 &&
             bbox_is_empty(&page->page_bb),
             "New page was not cleaned up correctly for DL_ERASE_CLEAR") ;

    /*@fallthrough@*/
  case DL_ERASE_PARTIAL: /* Partial paint destroying DL */
    HQASSERT(page->reserved_bands == 0 &&
             page->all_hdls == NULL &&
             page->im_list == NULL &&
             page->image_lut_list == NULL &&
             page->dlc_context == NULL &&
             page->omit_data == NULL &&
             page->spotlist == NULL &&
             page->irr.store == NULL &&
             page->render_resources == NULL &&
             page->currentHdl == NULL &&
             page->targetHdl == NULL &&
             page->currentGroup == NULL &&
             page->currentdlstate == NULL &&
             page->groupDepth == 0 &&
             page->regionMap == NULL &&
             page->im_imagesleft == 0 &&
             page->im_expbuf_shared == NULL &&
             page->im_shared == NULL,
             "New page was not cleaned up correctly for DL_ERASE_PARTIAL") ;

    /* Create resource requirements tree. */
    if ( (page->render_resources = resource_requirement_create()) == NULL )
      goto fail ;

    if (/* req->expr */
        requirement_node_create(page->render_resources,
                                REQUIREMENTS_ROOT,
                                REQUIREMENT_OP_LIMIT) == NULL ||
        /* req->expr->right */
        (pagelimit = requirement_node_create(page->render_resources,
                                             REQUIREMENTS_PAGELIMIT,
                                             REQUIREMENT_OP_VALUES)) == NULL ||
        /* req->expr->left */
        requirement_node_create(page->render_resources, REQUIREMENTS_CALCULATE,
                                REQUIREMENT_OP_SUM) == NULL ||
        /* req->expr->left->right */
        (pagevals = requirement_node_create(page->render_resources,
                                            REQUIREMENTS_RENDER_GROUP,
                                            REQUIREMENT_OP_VALUES)) == NULL ||
        /* req->expr->left->left */
        requirement_node_create(page->render_resources, REQUIREMENTS_CALCULATE,
                                REQUIREMENT_OP_MAX) == NULL ||
        /* req->expr->left->left->right */
        requirement_node_create(page->render_resources, REQUIREMENTS_TRAP_GROUP,
                                REQUIREMENT_OP_VALUES) == NULL ||
        /* req->expr->left->left->left */
        requirement_node_create(page->render_resources, REQUIREMENTS_CALCULATE,
                                REQUIREMENT_OP_SUM) == NULL ||
        /* req->expr->left->left->left->right */
        requirement_node_create(page->render_resources,
                                REQUIREMENTS_PAGEADD,
                                REQUIREMENT_OP_VALUES) == NULL ||
        /* req->expr->left->left->left->left */
        (bandvals = requirement_node_create(page->render_resources,
                                            REQUIREMENTS_BAND_GROUP,
                                            REQUIREMENT_OP_VALUES)) == NULL ) {
      resource_requirement_release(&page->render_resources) ;
      goto fail ;
    }

    /* Construct the initial band resource pools. */
    if ( !band_resource_pools(page) )
      goto fail ;

    /* Set maximum limits for band-related resources. Page limits should only
       be used for resources that can usefully cache data across bands (and
       therefore might benefit from having more instances than active
       threads), or for resources that may be detached and handed to other
       processes (so the RIP can allocate another instance and continue
       working while the resource is detached). */
    if ( /* May be detached for output: */
         !requirement_node_setmax(pagelimit, TASK_RESOURCE_BAND_OUT,
                                  (unsigned int)page->sizedisplaylist) ||
         /* May be detached for async MHT: */
         !requirement_node_setmax(pagelimit, TASK_RESOURCE_BAND_CT,
                                  (unsigned int)page->sizedisplaylist) ||
         /* May be cached across bands: */
         !requirement_node_setmax(pagelimit, TASK_RESOURCE_IMAGE_EXPANDER,
                                  (unsigned int)page->sizedisplaylist) )
      goto fail ;

    /* Populate some of the resources. We would like to make these all only
       if needed, but the back end isn't quite smart enough to fix them all
       on demand yet. */
    if ( /* We can use up to one band for each thread. */
         !requirement_node_simmax(bandvals,
                                  REQUIREMENT_SIMULTANEOUS_THREADS) ||
         /* Possibly a scratch band, one per page. */
         (page->scratch_band_size != 0 &&
          !requirement_node_setmin(pagevals, TASK_RESOURCE_BAND_SCRATCH, 1)) ||
         /* halftonebase, maxbltbase */
         /** \todo ajcd 2012-03-13: only if halftoning or RLE clipping or
             intersecting clips: (halftonebase) */
         /** \todo ajcd 2012-03-13: only if maxblitting: (maxbltbase) */
         !requirement_node_setmin(bandvals, TASK_RESOURCE_LINE_OUT, 2) ||
         /* clippingbase */
         /** \todo ajcd 2012-03-13: only if clipping: (clippingbase) */
         !requirement_node_setmin(bandvals, TASK_RESOURCE_LINE_1, 1) )
      goto fail ;

    /* Grab what little PCL state we need - is it PCL and 5e? */
    page->pcl5eModeEnabled = pcl5eModeIsEnabled() ;
    page->opaqueOnly = pclGstateIsEnabled() ;

    /* If a trapzone is created via pagedevice Install procedure, it
     * persists from page to page until a new pagedevice is installed. Thus we
     * might get a trapzone defined without an explicit settrapzone operation.
     * Account for the requirements of such default trapzones here.
     */
    if ( page->trapping_active && ! trapRequirements(page) )
      goto fail ;

    /* Create new state stores. Needed before opening the base HDL. */
    if ( !createDLStores(&page->stores, page->dlpools) ) {
      (void)error_handler(VMERROR);
      goto fail ;
    }

    /* Initialise the dl color context. Needed before opening the base HDL. */
    if ( !dlc_context_create(page) )
      goto fail;

    /* Init erase and knockout to white: the real values are computed
       at render start. */
    dlc_get_white(page->dlc_context, &page->dlc_erase);
    dlc_get_white(page->dlc_context, &page->dlc_knockout);

    /* Install a new base HDL. */
    if ( !hdlOpen(page, TRUE, HDL_BASE, &hdl) )
      goto fail;

    HQASSERT(hdl == page->currentHdl, "New HDL not pushed on stack") ;
    HQASSERT(page->currentHdl != NULL, "currentHdl is null") ;
    HQASSERT(page->currentHdl == page->targetHdl,
             "Initial HDL/Group should be target DL");
    HQASSERT(dlPageHDL(page) == page->currentHdl,
             "baseHdl should match currentHdl");

    if ( !gsc_colorStateStart() )
      goto fail;

    /* Add an erase to the base HDL, before the page group is created. */
    HQASSERT(dlPageHDL(page) == page->currentHdl, "currentHdl must be baseHdl");
    if ( !adderasedisplay(page, !page->rippedtodisk) )
      goto fail;

    if ( !ht_introduce_dl(page->eraseno, TRUE) )
      goto fail;

    if ( !start_separation_omission(page) )
      goto fail;

    if ( !dl_image_start(page) )
      goto fail;

    /* Create the color state for the back-end transforms. */
    if ( !dl_color_state_create(page) )
      goto fail;

    /** \todo ajcd 2011-03-08: RCBN start here? */

    /* Create a default page group which may be subsequently replaced by some
       other page group (for example, a PDF page group). */
    if ( !dlSetPageGroup(page, page->page_group_details.colorspace,
                         page->page_group_details.knockout) )
      goto fail;

    break ;
  case DL_ERASE_PRESERVE:
    if ( !ht_introduce_dl(page->eraseno, TRUE) )
      goto fail;
    break ;
  case DL_ERASE_COPYPAGE:
    HQASSERT(!page->rippedtodisk &&
             !page->rippedsomethingsignificant &&
             page->highest_sheet_number == -1,
             "New page was not cleaned up correctly for DL_ERASE_COPYPAGE") ;
    break ;
  default:
    HQFAIL("Invalid erase type") ;
  }

  set_region_size(page) ;
  page->band_lc = 0 ;
  page->backdropShared = NULL ;
  HQASSERT((page->band_lines % page->region_height) == 0 || page->sizedisplaylist == 1,
           "Region height must be an exact factor of band height, unless only one band");
  if ( !bd_sharedNew(page, page->page_w, page->page_h, page->region_height,
                     (page->band_lines + page->region_height - 1) / page->region_height,
                     max_simultaneous_tasks(), &page->backdropShared) )
    goto fail;

  /* Create a task group for the page containing a DL ready task. All tasks
     for the page (both interpretation and rendering) should be directly or
     indirectly in this group.

     The ready task acts as a collector for interpreter-time DL tasks, and a
     gate for render-time DL tasks. It is only made ready when the page is
     handed over for rendering, so it can be made a dependent by
     interpretation time tasks. It can also be replaced by other page group
     tasks, if tasks are needed before rendering should start, but after the
     DL is ready. In that case, the replacement task should be made dependent
     on the ready task and should be left at constructing.

     The cleanup actions are tricky here. We've constructed most of the
     page, if we leave it without the page task group and next task, then
     we're in a similar state to when the RIP booted up. If the failure is
     because the job's task group was cancelled, then there would be a series
     of cascading failures (erasepage and setpagedevice would both fail
     because they're unable to construct a clean page) until the server loop
     is reached, and a new job object is constructed. This is too tricky, so
     we allow this to succeed in that case. Note that interrupts and timeouts
     will not be lost in error_clear(). */
  earlier_error = error_signalled_context(context->error);
  if ( !task_group_create(&page->all_tasks, TASK_GROUP_PAGE,
                          page->job->task_group, NULL) ) {
    res = task_group_is_cancelled(page->job->task_group);
    if ( res && !earlier_error )
      error_clear_context(context->error);
    goto fail ;
  }
  /* We won't make the DL complete task ready until we're done interpreting
     the page. However, we do make the all tasks group ready, so asynchronous
     tasks can run inside it during interpretation. */
  task_group_ready(page->all_tasks) ;

  if ( !task_create(&page->next_task,
                    NULL /*specialiser*/, NULL /*specialiser_args*/,
                    NULL /*worker*/, NULL /*args*/, NULL /*cleanup*/,
                    page->all_tasks, SW_TRACE_DL_COMPLETE) ) {
    (void)task_group_join(page->all_tasks, NULL) ;
    task_group_release(&page->all_tasks) ;
    goto fail ;
  }

  /* Make the new page's DL_COMPLETE task depend on the previous page's
     completion, or the previous job's completion if this is the first of a
     job. We don't allow errors in task_depends() to propagate to the current
     context, because then a previous page render failure would prevent any
     subsequent DL being constructed. This causes setpagedevice to fail, and
     leaves the page device in a sufficiently messed up stage at the end of a
     job that we can't restore to the server loop correctly. */
  if ( page->job->previous != NULL ) {
    int32 saved_error = 0;
    Bool ok;

    if ( earlier_error )
      error_save_context(context->error, &saved_error);
    ok = task_depends(page->job->previous, page->next_task);
    if ( earlier_error )
      error_restore_context(context->error, saved_error);
    else if ( !ok )
      error_clear_context(context->error);
  }

#undef return
  /* Introduce the constructed DL to the surface API. This is the last thing
     done before resetting the erase type of the page, otherwise we'd have to
     retire the page from the surface during error cleanup. */
  if ( page->surfaces != NULL && page->surfaces->introduce_dl != NULL ) {
    surface_handle_t handle ;
    Bool continued = (erase_type == DL_ERASE_PARTIAL ||
                      erase_type == DL_ERASE_PRESERVE) ;
    if ( continued )
      handle.page = page->sfc_page ;
    else
      handle.instance = page->sfc_inst ;
    if ( !page->surfaces->introduce_dl(&handle, page, continued) )
      return FALSE ;
    page->sfc_page = handle.page ;
  }

  /* Page is now fully constructed and ready to use. */
  page->erase_type = DL_ERASE_CLEAR ;
  return TRUE ;

 fail:
  HQASSERT(page->erase_type == DL_ERASE_BEGIN,
           "Page not in failed construction state") ;
  /* If we previously retired the DL from the surface, but promised that it
     would be continued, we have to make it inform the surface that it will
     not be continued. The way we do that is to continue the page and then
     immediately retire it again. */
  if ( erase_type == DL_ERASE_PARTIAL || erase_type == DL_ERASE_PRESERVE ) {
    if ( page->surfaces != NULL ) {
      surface_handle_t handle ;
      handle.page = page->sfc_page ;
      if ( page->surfaces->introduce_dl != NULL &&
           !page->surfaces->introduce_dl(&handle, page, TRUE /*continued*/) )
        return FALSE ;
      if ( page->surfaces->retire_dl != NULL )
        page->surfaces->retire_dl(&handle, page, FALSE /*continues*/) ;
      page->sfc_page = NULL ;
    }
  }
  return res;
}


void dl_clear_page(DL_STATE *page)
{
  HQASSERT(IS_INTERPRETER(), "Page clear not called by interpreter") ;
  HQASSERT(page, "No page to clear") ;
  HQASSERT(page->erase_type == DL_ERASE_CLEAR ||
           page->erase_type == DL_ERASE_GONE ||
           page->erase_type == DL_ERASE_BEGIN,
           "Page has unexpected erase type") ;

  /* Could be in the middle of a show when a riptodisk is requested. But
     there's no need to return an error on failure, because we're about to
     nuke the DL that we would have added the object to. */
  (void)finishaddchardisplay(page, 1) ;

  /* Cancel the input page tasks, the DL is being destroyed. The task group
     might not exist if dl_begin_page() failed. We deliberately ignore any
     errors from joining the page group. */
  if ( page->all_tasks != NULL ) {
    task_group_cancel(page->all_tasks, INTERRUPT) ;
    (void)task_group_join(page->all_tasks, NULL) ;
    task_group_release(&page->all_tasks) ;
  }

  /* The page's next task should be in the group we've just cancelled, so we
     can release it without making it ready. The next task might not exist if
     dl_begin_page() failed. */
  if ( page->next_task != NULL )
    task_release(&page->next_task) ;

  /* Note that we're changing the front-end page. */
  /** \todo ajcd 2011-03-21: Errors shouldn't be allowed in this routine, if
      possible. However, it's worse trouble if we allowed dl_clear_page() to
      fail, so ignore the return value. */
  (void)dl_changing_page(page, page->erase_type) ;

  /* Delete everything on this page, right now. We're continuing this job
     with the same inputpage, so just clear out the DL pools rather than
     destroy job objects, etc. */
  dl_erase(page) ;
}

static Bool reset_targetRS(GSTATE *gs, void *targetRS)
{
  gsc_setTargetRS(gs->colorInfo, (GUCR_RASTERSTYLE*)targetRS);
  return TRUE;
}

/**
 * Close any previous page group and make a new one with the specified
 * attributes.  Closing the previous page group results in it being added to the
 * base HDL, either as a group or, if there's no transparency, as an HDL.  Empty
 * page groups are destroyed.  This function is called for each imposed page
 * which means each imposed page is put into a separate page group, all
 * contained in the base HDL.  The erase lobj is in the base HDL, before any
 * page groups.
 */
Bool dlSetPageGroup(DL_STATE *page, OBJECT colorspace, Bool knockout)
{
  Group *group;

  /** \todo ajcd 2011-03-23: Should this be both here and in
      dl_erase(), or should this function be split in two and the
      group destroy part called from there? */
  if ( page->currentGroup != NULL ) {
    HQASSERT(groupGetUsage(page->currentGroup) == GroupPage,
             "Expected only a page group to be open");
    if ( !groupClose(&page->currentGroup, TRUE) )
      return FALSE;
  }

  /* Create a default page group which may be subsequently replaced by some
     other page group (for example, a PDF page group), and may be eventually
     optimised to a simple HDL if possible. */
  if ( !groupOpen(page, colorspace, DEFAULT_PAGE_GROUP_ISOLATED, knockout,
                  TRUE /* banded */, NULL /* bgcolor */, NULL /* xferfn */,
                  NULL /* patternTA */, GroupPage, &group) )
    return FALSE;
  HQASSERT(group == page->currentGroup,
           "New group should match currentGroup");

  /* If being called after a partial paint, need to ensure all the gstates are
     updated to the new page group's blend space. */
  if ( !gs_forall(reset_targetRS, gsc_getTargetRS(gstateptr->colorInfo),
                  FALSE, FALSE) )
    return FALSE;

  /* Store the page group knockout flag and colorspace in case we need
     to make a new page group after a partial paint. */
  page->page_group_details.knockout = knockout;
  page->page_group_details.colorspace = onull;
  gsc_safeBackendColorSpace(&page->page_group_details.colorspace,
                            &colorspace, page->page_group_details.csa);

  /* Cache the black indices for the virtual device space and the real device
     space. We use the special COLORANTINDEX_ALL to indicate additive spaces
     for which black is all channels zero. These indexes can be passed
     directly to dlc_is_black() to determine whether a DL color built for the
     DL or converted to the device space is black. */
  page->virtualBlackIndex = guc_getBlackColorantIndex(gsc_getTargetRS(gstateptr->colorInfo)) ;
  page->deviceBlackIndex = guc_getBlackColorantIndex(page->hr) ;

  /* After the new page group is established, reset recombine for the new page
     and take a snapshot of the display list in its initial state for recombine
     or imposition. rcbn_beginpage() must be called before the snapshot to allow
     rcb_dl_start() to add the dummy erase first. */
  if ( !rcbn_beginpage(page) )
    return FALSE;

  dlTakeSnapshot(page);
  return TRUE;
}

/**
 * When we're imposing, or if the job does "clippath fill" to emulate erasepage
 * for an inverted transfer function, an "erasepage" node will get added to
 * the DL. In this case, if we're recombining, we need to advance the
 * dl pointers over this node so that anything that gets inserted on the DL
 * comes after it. If we don't do this, and the color of the fill is not
 * correctly detected, then we can end up inserting objects before it that
 * will get wiped out.
 * e.g. We add a Black "erasepage" because we don't know what color it is but
 * then continue with Cyan objects, because it really is the Cyan plate. When
 * we then get to the Black plate, if we have objects overprinting there (ie
 * objects that are only in the Black plate) they will get inserted before
 * this Black "erasepage" node which mean that they will get wiped out by it.
 */
Bool dlskip_pseudo_erasepage(DL_STATE *page)
{
  HQASSERT( rcbn_enabled() || doing_imposition ,
            "dlskip_pseudo_erasepage called when not imposition/recombining" ) ;

  if ( rcbn_enabled() && !rcb_resync_dlptrs(page) )
    return FALSE ;

  dlTakeSnapshot(page);
  return TRUE ;
}

/**
 * This routine is called on the second and subsequent pages of a
 * preseparated job. Instead of clearing the display list, it resets the end
 * vector to the beginning so that we rescan the display list.
 */
void dlreset_recombine(DL_STATE *page)
{
  HQASSERT( rcbn_enabled() ,
            "dlreset_recombine called when not recombining" ) ;

  dlRestoreFromSnapshot(page);
}

Bool dlreset_imposition(DL_STATE *page)
{
  HQASSERT( rcbn_enabled() || doing_imposition ,
            "dlreset_imposition called when not imposition/recombining" ) ;

  /* Next imposed page is placed in its own page group.
     Close previous page group and make a new one for this page. */
  return dlSetPageGroup(page, onull /* page group colorspace, default */,
                        DEFAULT_PAGE_GROUP_KNOCKOUT);
}

/*
 * SystemParams.DLBanding setings
 *     0  generate banded DL at object creation time
 *     1  generate banded DL at showpage time
 *     2  do not generate banded DL
 */

/**
 * Will the DL be accessed and modified randomly, only is it only ever
 * written to and then re-read sequentially at render time ?
 *
 * If there are multiple rendering threads then they all can access the
 * DL, and at the moment that counts as random access.
 * \todo BMJ 20-Nov-08 :  Support DL purging with multiple render threads
 */
Bool dlRandomAccess(void)
{
  corecontext_t *context;

  if (NUM_THREADS() > 1)
    return TRUE;

  context = get_core_context_interp() ;

  if ( context->systemparams->DLBanding == 0 )
    return TRUE;

  /* Recombine/vignettes/imposition all edit the DL on the fly */
  if ( doing_imposition || context->userparams->VignetteDetect ||
       isHDLTEnabled(*gstateptr) || rcbn_enabled() )
    return TRUE;

  return FALSE;
}

/**
 * Prepare the banding of the DL prior to showpage rendering
 */
Bool dlPrepareBanding(DL_STATE *page)
{
  Bool ok = TRUE;

  if ( get_core_context_interp()->systemparams->DLBanding == 1 )
    ok = hdlPrepareBanding(page);
  return ok;
}

Bool dl_pending_task(DL_STATE *page, task_t **task)
{
  Bool result = TRUE ;

  HQASSERT(page, "No display list") ;
  HQASSERT(task && *task, "No pending task") ;
  HQASSERT(IS_INTERPRETER(), "Only interpreter should modify DL pending task") ;

  if ( page->next_task != NULL ) {
    /* If task_depends fails, the new task is cancelled. However, we're going
       to leave it on the DL as the pending task, so its failure is
       propagated to the next task. */
    result = task_depends(page->next_task, *task) ;

    /* Release existing reference, after making ready */
    task_ready(page->next_task) ;
    task_release(&page->next_task) ;
  }

  /* Transfer reference to DL */
  page->next_task = *task ;
  *task = NULL ;

  return result ;
}

/**
 * Test to see if the current DL is empty or not.
 * If recombining or imposing just tests the current separation or imposed page.
 */
Bool displaylistisempty(DL_STATE *page)
{
  if ( rcbn_enabled() || doing_imposition ) {
    int32 zb = page->sizefactdisplaylist; /* Z-order band */
    HDL *hdl;

    if ( page->currentGroup != NULL )
      hdl = groupHdl(groupBase(page->currentGroup));
    else
      hdl = dlPageHDL(page);

    HQASSERT(hdl != NULL, "No page HDL to get band tails from");
    return hdlBandTails(hdl)[zb] == hdlBandTailSnapshot(hdl)[zb];
  } else
    return dlIsEmpty(page);
}

/**
 * Throw away the given DL object, i.e. add it to NO DL
 */
int32 addobjecttonodl(DL_STATE *page, LISTOBJECT *lobj)
{
  UNUSED_PARAM(LISTOBJECT *, lobj);
  UNUSED_PARAM(DL_STATE *, page);
  HQASSERT(!analyzing_vignette(), "shouldn't be analyzing a vignette");
  return DL_NotAdded;
}

/**
 * DL add error function - Error if called
 */
int32 addobjecttoerrordl(DL_STATE *page, LISTOBJECT *lobj)
{
  UNUSED_PARAM(LISTOBJECT *, lobj);
  UNUSED_PARAM(DL_STATE *, page);

  HQASSERT(!analyzing_vignette(), "shouldn't be analyzing a vignette");
  HQFAIL("addobjecttoerrordl called");
  return DL_Error;
}

#if defined( ASSERT_BUILD )
/**
 * Enable DL checking
 * Patch in debugger or private build for extra DL sanity testing
 */
static Bool do_check_hdl = FALSE;

/**
 * Check the validity of the given DL
 */
static void check_dl(DLREF *dl)
{
  LISTOBJECT *lobj;

  HQASSERT(dl, "DL corrupt; no objects") ;
  lobj = dlref_lobj(dl);
  HQASSERT(lobj, "DL corrupt; missing LISTOBJECT");
  HQASSERT(lobj->opcode == RENDER_erase, "DL corrupt; missing erase");

  dl = dlref_next(dl);
  while ( dl != NULL ) {
    lobj = dlref_lobj(dl);
    HQASSERT(lobj, "DL corrupt; missing LISTOBJECT");
    HQASSERT(lobj->opcode < N_RENDER_OPCODES, "DL corrupt; bad DL opcode");
    HQASSERT(lobj->objectstate, "DL corrupt; missing object state");
    HQASSERT(lobj->objectstate->clipstate, "DL corrupt; missing clip");
    dl = dlref_next(dl);
  }
}

#endif /* ASSERT_BUILD */

/**
 * Sanity check on the validity of a HDL.
 * Only used in assert builds, and disabled by default
 */
static void check_hdl(DL_STATE *page, HDL *hdl)
{
#if defined( ASSERT_BUILD )
  if ( do_check_hdl ) {
    int32 band;

    for ( band = 0; band < page->sizefactdisplaylist; band++ )
      check_dl(hdlBands(hdl)[band]);
    check_dl(hdlOrderList(hdl));
  }
#else
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(HDL *, hdl);
#endif
}

/**
 * Add the given object to the current DL (which may be a pattern DL).
 *
 * It appears from testing that if we do a setgstate to a valid pagedevice
 * whilst in a pattern, objects are added to the page device's DL. Using
 * the HDL target here ensures that objects are added to the right display
 * list.
 */
static int32 add2dl(DL_STATE *page, LISTOBJECT *lobj, Bool patdl)
{
  HDL *hdl = page->targetHdl;
  STATEOBJECT *state = lobj->objectstate;

  HQASSERT(!analyzing_vignette(), "shouldn't be analyzing a vignette");
  HQASSERT(lobj->opcode < N_RENDER_OPCODES, "Unknown object type added to DL");
  HQASSERT(lobj->opcode != RENDER_void, "Illegal object type added to DL");
  HQASSERT(state != NULL, "DL object has no state");
  HQASSERT(!bbox_is_empty(&(lobj->bbox)), "bbox is empty");
  HQASSERT(patdl ?
           (CURRENT_DEVICE() == DEVICE_PATTERN1 ||
            CURRENT_DEVICE() == DEVICE_PATTERN2) :
           dev_is_bandtype(CURRENT_DEVICE()),
           "wrong device function called");

  if ( hdl == NULL )
    return DL_NotAdded;

  if ( dl_is_none(lobj->p_ncolor) )
    return DL_NotAdded;

  if ( !dlAddingObject(page, lobj->opcode == RENDER_erase) )
    return FALSE;

  if ( !patdl ) {
#if defined(DEBUG_BUILD)
    /* If object is outside of DL focus area, ignore it. */
    if ( !bbox_intersects(&debug_focus, &lobj->bbox) )
      return DL_NotAdded ;
#endif

    if ( rcbn_enabled() && rcbn_merge_required(lobj->opcode) &&
         (lobj->spflags & RENDER_RECOMBINE) != 0 ) {
      int32 result;
      switch ( result = merge_dl_objects(page, lobj,
                                         rcb_comparefn(lobj->opcode),
                                         rcb_compareop(lobj->opcode)) ) {
      case MERGE_NONE:
      case MERGE_FUZZY:
        break;
      case MERGE_ERROR:
        return DL_Error;
      default:
        check_hdl(page, dlPageHDL(page));
        HQASSERT(result == MERGE_EXACT ||
                 result == ( MERGE_EXACT | MERGE_FUZZY ),
                 "unknown return type from dl merge");
        return DL_Merged;
      }
    }
    /*
     * Only modify the pages bounding box if this is not an erase object and
     * we are not doing a background separation or pseuso erasepage.
     */
    if ( lobj->opcode != RENDER_erase &&
         (lobj->spflags & (RENDER_BACKGROUND | RENDER_PSEUDOERASE)) == 0 )
      bbox_union(&page->page_bb, &lobj->bbox, &page->page_bb);

    if ( (lobj->spflags & RENDER_RECOMBINE) != 0 ) {
      /* Tell recombine we have added a recombine object to the dl */
      HQASSERT(rcbn_enabled(), "got recombine object when not recombining");
      rcbn_add_recombine_object();
    }
    linkstats(page, &lobj->bbox);
  }

  if ( lobj_maybecompositing(lobj, page->currentGroup, TRUE) ) {
    uint32 inCompsMax;

    /* If we've done a partial paint and now have an object that may require
       compositing then incorrect output is likely.  Instead of partial painting
       we should have thrown a vmerror, which we do now belatedly.  There's no
       equivalent test for PCL ropping, and possibly incorrect output is
       accepted for expediency. */
    if ( page->rippedtodisk )
      return error_handler(VMERROR), DL_Error;

    /* May be compositing the object so update backdrop resource provisioning. */
    gucr_colorantCount(gsc_getTargetRS(gstateptr->colorInfo), &inCompsMax);
    if ( !bd_resourceUpdate(page->backdropShared, inCompsMax,
                            groupNonIsolatedGroups(groupBase(page->currentGroup)),
                            page->groupDepth) )
      return DL_Error;
  }

  dlobj_rollover(page, lobj, hdl) ;

  if ( !hdlAdd(hdl, lobj) )
    return DL_Error;

  check_hdl(page, hdl);

  /* Add spot number and object type to list of known spots to help final
     backdrop render handle screen switching efficiently. */
  if ( !spotlist_add(page, state->spotno,
                     DISPOSITION_REPRO_TYPE(lobj->disposition)) )
    return DL_Error;

  /* Track the bboxes of the objects that refer to a softmask to
   * minimise the amount of softmask compositing.
   */
  if ( state->tranAttrib != NULL && state->tranAttrib->softMask != NULL )
    groupSoftMaskAreaExpand(state->tranAttrib->softMask->group, &(lobj->bbox));

  if ( state->tranAttrib != NULL && state->tranAttrib->alphaIsShape
       && (( state->tranAttrib->alpha != 0.0 &&  state->tranAttrib->alpha != 1.0 )
           || state->tranAttrib->softMask != NULL ))
    groupAnnounceShapedObj(page->currentGroup);

  probe_other(SW_TRACE_DL_OBJECT, SW_TRACETYPE_MARK, (intptr_t)lobj) ;

#if defined(DEBUG_BUILD) && defined(DEBUG_TRACK_SETG)
  lobj->setg_count = debug_dl_setg ;
#endif

  return DL_Added;
}


/**
 * Add the given object to the current pattern DL
 */
int32 addobjecttopatterndl(DL_STATE *page, LISTOBJECT *lobj)
{
  return add2dl(page, lobj, TRUE);
}

/**
 * Add the given object to the current HDL
 */
int32 addobjecttopagedl(DL_STATE *page, LISTOBJECT *lobj)
{
  int32 result = add2dl(page, lobj, FALSE);
  if ( result == DL_Added && lobj->objectstate != NULL &&
       lobj->objectstate->pclAttrib != NULL ) {
    pclIdiomAdd(page, hdlOrderListLast(page->targetHdl)) ;
  }
  return result ;
}

/**
 * Clip the bbox to the currently active clip bbox
 */
Bool clip2cclipbbox(dbbox_t *bbox)
{
  if ( degenerateClipping )
    return FALSE;

  HQASSERT(bbox_is_normalised(&cclip_bbox), "clip out of order");

  if ( !bbox_intersects(bbox, &cclip_bbox) )
    return FALSE ;

  bbox_intersection(bbox, &cclip_bbox, bbox);

  return TRUE ;
}

/*
 * CHARACTER HANDLING: finishaddchardisplay, addchardisplay :
 *
 * Adds characters to the display list.
 * Characeter entries on the display list are just normal dl objects but with
 * a variable size array of data triples referenced from the main object.
 * The show operator (and similar) set the number of characters we expect
 * (except kshow, where you can get out early), and we allow for that
 * number of characters in the display list node. On first entry, the node is
 * allocated and if there is more than one character we make a record of where
 * the node is.
 *
 * On second and subsequent times through, we fill in the new character in the
 * next data triple, bumping the count of number of slots used. However, if in
 * doing this we extend over additional bands, then we have to add extra link
 * objects; if that is expensive in memory, give up, waste the remaining memory
 * in the node that is allocated already, and allocate single character nodes
 * for the remainder of the string.
 *
 * All of this static state is stored in a single structure for clarity of use.
 */

/**
 * All of the text state maintained as the dl object is created incrementally
 */
typedef struct DL_TEXT_STATE {
  LISTOBJECT *dlobj;
  int16 nfree;
} DL_TEXT_STATE;

static DL_TEXT_STATE dlts = { NULL, 0 };

/**
 * Return the number of slots free in the current DL object
 */
int32 displaylistfreeslots(void)
{
  if (dlts.dlobj)
    return (int32) dlts.nfree ;
  return 0 ;
}

/**
 * Finish adding text to the DL
 */
Bool finishaddchardisplay(DL_STATE *page, int32 newlength)
{
  LISTOBJECT *lobj;

  /* If called inside a cached char context by HDLT or a recursive call, we
     don't want to flush any top-level char DL object that is under
     construction. If we did that, we'd end up dropping characters from the
     output, see Request 62979. */
  if ( CURRENT_DEVICE() == DEVICE_CHAR )
    return TRUE ;

  /* puts a genuinely complete character node onto the display list.
     Note: previously the node was put there on the first character and
     then appended to, but now we wait until we have a complete node
     before actually inserting it into the display list */

  if ( dlts.dlobj == NULL ) {
    if ( rcbn_intercepting() &&
         get_core_context_interp()->userparams->RecombineObject != 0 )
      /* Recombine character merging code can only merge one char at a time.
         If we are currently allowing object-merging then split the string up
         into individual char listobjects, otherwise more efficient not to. */
      dlts.nfree = 1;
    else
      dlts.nfree = (int16)newlength;
    return TRUE;
  }

  HQASSERT(!bbox_is_empty(&(dlts.dlobj->bbox)), "bbox empty");
  HQASSERT(dlts.dlobj->bbox.x1 >= 0 ||
           dlts.dlobj->bbox.y1 >= 0 ||
           dlts.dlobj->bbox.x2 < page->page_w ||
           dlts.dlobj->bbox.y2 < page->page_h,
           "bbox out of bounds");

  lobj = dlts.dlobj;
  dlts.dlobj = NULL;
  dlts.nfree = (int16)newlength;

  return add_listobject(page, lobj, NULL);
}

/**
 * Create a DL object for the given number of text glyphs
 */
static Bool make_charobj(DL_STATE *page, int16 nc, dbbox_t *bbox,
                         LISTOBJECT **plobj)
{
  DL_CHARS *text;
  int32 size;

  HQASSERT(nc > 0, "Create a text listobject with no chars");
  size = sizeof(DL_CHARS) + (nc-1)*sizeof(DL_GLYPH);
  text = (DL_CHARS *)dl_alloc(page->dlpools, size,
                              MM_ALLOC_CLASS_CHAR_OBJECT);
  if ( text == NULL )
    return FALSE;

  text->nalloc = (uint16)nc;
  text->nchars = 0;

  if ( !make_listobject(page, RENDER_char, bbox, plobj) ) {
    dl_free(page->dlpools, (uint8 *)text, size,
            MM_ALLOC_CLASS_CHAR_OBJECT);
    return FALSE;
  }

  (*plobj)->dldata.text = text;
  return TRUE;
}

/**
 * Add a text glyph to the DL
 */
Bool addchardisplay(DL_STATE *page, FORM *theform, int32 x1, int32 y1)
{
  dbbox_t bbox;
  DL_CHARS *dlchars;
  FORM *realform;

  /* what is the extent of the character? */
  realform = theform;
  if ( theFormT(*theform) == FORMTYPE_CHARCACHE ) {
    CHARCACHE *thechar;
    thechar = (CHARCACHE *)theform;
    realform = theForm(*thechar);
  }
  bbox_store(&bbox, x1, y1, x1 + realform->w - 1, y1 + realform->h - 1);

  /* General rectangular clipping on x and y.
     If a second or subsequent character is clipped out, we now have to
     add the already constructed (but not completely filled) node onto the
     display list (if one exists).
   */
  if ( !clip2cclipbbox(&bbox) )
    return finishaddchardisplay(page, 1);

  if ( dlts.dlobj == NULL ) {
    if ( dlts.nfree <= 0 ) {
      /* we know nothing about subsequent characters, so just allocate
         enough in the LISTOBJECT for one */
      dlts.dlobj = NULL;
      dlts.nfree = 1;
    }

    if ( !make_charobj(page, dlts.nfree, &bbox, &dlts.dlobj) )
      return FALSE;
  } else { /* Already got a DL object, just add to it */
    /* We are adding new characters. Enough space was (in theory) allowed for
     * by counting the number of characters in the show (or whatever) operator
     * when we first allocated the node.
     *
     * Used to check the y bounds of the added character and make complex
     * calculations to decide whether to start a new DL object or not. With
     * new DL memory structure, this is not worthwhile any more. So just update
     * the ybounds (in fact the whole bbox) for the text DL object.
     */
    HQASSERT(dlts.nfree > 0, "adding extra character when no space in node");

    bbox_union(&dlts.dlobj->bbox, &bbox, &dlts.dlobj->bbox);
  }

  dlchars = dlts.dlobj->dldata.text;
  dlchars->ch[dlchars->nchars].x    = x1;
  dlchars->ch[dlchars->nchars].y    = y1;
  dlchars->ch[dlchars->nchars].form = theform;
  dlchars->nchars++;

  if (--dlts.nfree > 0)
    return TRUE;
  else
    return finishaddchardisplay(page, 1);
}

/**
 * See if the rectangle object is patterned with a single untiled image,
 * in which case we can just use the image without the pattern construct.
 *
 * All images in XPS are supplied in the form of an ImageBrush, and as
 * a result appear inside patterns in the display list. This makes them
 * considerably slower to render. But in the majority of cases, the
 * images are actually untiled and the same size as the patterned rectangle,
 * so the patterm construct is a waste. Try and detect such cases,
 * and throw away the pattern to make rendering much faster.
 * Would be nice to stop XPS ImageBrush calls using patterns at all in this
 * case, but its very hard to work this out ahead of time. So it is easier
 * to catch the patterns after the event, and promote them to normal
 * non-patterned images in the DL.
 * Called from the rectangle DL building code, as this is where the pattern
 * has been fully defined, and we are trying to pour it through a rectangle.
 * Basically what we are testing for is a rectangle object that has an
 * associated pattern which is a single image the same size as the rectangle.
 *
 * \param[in]     lobj rectangle listobject under construction
 * \return        object to add to DL (either original rectangle or image
 *                extracted from the pattern)
 */
static LISTOBJECT *promote_image_in_pattern(DL_STATE *page, LISTOBJECT *lobj)
{
  HDL *patHdl = NULL;

  if ( lobj != NULL && lobj->objectstate != NULL )
    patHdl = patternHdl(lobj->objectstate->patternstate);

  /*
   * First make sure that rectangle has an associated pattern and HDL.
   * Also ignore rectangles with transparancy, as it makes the tests too
   * complex, and is not the typical case that is trying to be optimised.
   */
  if ( patHdl != NULL && (lobj->marker & MARKER_TRANSPARENT) == 0 ) {
    PATTERNOBJECT *ps = lobj->objectstate->patternstate;
    DLREF *dl = hdlOrderList(patHdl);

    /* Then make sure the pattern is untiled and a single image object */
    if ( ps->tilingtype == 0 && dl != NULL && dlref_next(dl) == NULL &&
         dlref_lobj(dl) && dlref_lobj(dl)->opcode == RENDER_image ) {
      IMAGEOBJECT *imageobj = dlref_lobj(dl)->dldata.image;
      dbbox_t *imbbox = &(imageobj->bbox);

      /* Next test is to ensure image is not rotated and
       * rectangle and image bounding boxes are the same.
       * Different code paths and different rounding mean that the
       * two bboxes often disagree by +/- 1 pixel, so we need to
       * leave a little leeway in the test, i.e allow image bbox to
       * be up to a pixel smaller.
       */
      if ( ( imageobj->optimize & IMAGE_OPTIMISE_ROTATED ) == 0 &&
             bbox_contains_epsilon(&(lobj->bbox),imbbox, 1, 1) ) {
        CLIPOBJECT *clip1 = lobj->objectstate->clipstate;
        CLIPOBJECT *clip2 = dlref_lobj(dl)->objectstate->clipstate;

        /*
         * Finally test to ensure object and pattern clipping have the same
         * effect.
         * Lots of complex cases are possible, with clipping being applied
         * outside or inside the pattern. But lets not worry about all the
         * obscure cases, just worry about the very typical one of
         * rectangular clipping equal to the size of the image. But clip
         * regions may actually be bigger than this, so just test for rect
         * clipping both outside and inside the pattern, and both big enough
         * to enclose the entire image (again with a 1 pixel leeway).
         */
        if ( clip1->fill == NULL && clip2->fill == NULL &&
             bbox_contains_epsilon(&(clip2->bounds),imbbox, 1, 1) &&
             bbox_contains_epsilon(&(clip1->bounds),imbbox, 1, 1) ) {
          /*
           * All conditions have been met, so the pattern construct
           * can be stripped off.  So free the original rectangle DL object
           * we had been building, and return the naked image DL object for
           * addition to the display list.
           */
          free_listobject(lobj, page);
          return dlref_lobj(dl);
        }
      }
    }
  }
  /* Fallen through if any of the tests have failed, so NOT a candiate
   * for jettisoning the pattern and just having a plain image.
   * So return the original rectanle object for addition to the DL.
   */
  return lobj;
}

/**
 * Adds a rectangle to the display list.
 *
 * when vignette detection is on, don't do an early return due to being
 * outside clip rect or with degenerate clipping, since we want to still
 * be able to detect a vignette that extends out of the clipping region.
 * Instead, go ahead and consider the rectangle (full extents thereof)
 * to be a vignette candidate, by calling addobjecttodl. Defer the clipping
 * to when the object is really being added to the display list, either in
 * its own right or as part of a vignette.
 */
Bool addrectdisplay(DL_STATE *page, dbbox_t *rect)
{
  LISTOBJECT *lobj ;
  dbbox_t clipbbox = *rect;

  if ( gstateptr->thePDEVinfo.scanconversion == SC_RULE_TESSELATE ) {
    clipbbox.x2--;
    clipbbox.y2--;
    if ( clipbbox.x2 < clipbbox.x1 || clipbbox.y2 < clipbbox.y1 )
      return TRUE;
  }

  if ( !clip2cclipbbox(&clipbbox) )
    return TRUE;

  /* this is a fill, so round off the last display node and start another */
  /* if it fails, either a DL merge error occured or get_listobject() */
  /* returned a null.  return FALSE in this case */
  if ( !finishaddchardisplay(page, 1) )
    return FALSE;

  if ( !make_listobject(page, RENDER_rect, &clipbbox, &lobj) )
    return FALSE ;

  lobj = promote_image_in_pattern(page, lobj);

  return add_listobject(page, lobj, NULL);
}

/**
 * Adds an image to the display list.
 */
Bool addimagedisplay(DL_STATE *page, IMAGEOBJECT *theimage, int32 imagetype)
{
  LISTOBJECT *lobj;
  int32 imageop = (imagetype == TypeImageMask) ? RENDER_mask : RENDER_image;

  /* Now set up display object. */
  if ( theimage->mask ) {
    if ( !dl_reserve_band(page, RESERVED_BAND_MASKED_IMAGE) )
      return FALSE ;
  }

  /* Get list object; later on, insert object into all relevant bands. */
  if ( !make_listobject(page, imageop, &theimage->bbox, &lobj) )
    return FALSE ;

  /* Images with an integrated alpha channel require compositing. */
  if ( (theimage->flags & IM_FLAG_COMPOSITE_ALPHA_CHANNEL) != 0 )
    /* Don't set MARKER_OMNITRANSPARENT, there's a possibility of
       setting the region map on a per-image-block basis. */
    lobj->marker |= MARKER_TRANSPARENT;

  /* Use generic data. */
  lobj->dldata.image = theimage;

  im_addextent(lobj, page->sizefactdisplaylist, page->sizefactdisplayband);

  /* Trim off blocks and data from the image that are known not be required */
  im_storetrim(theimage->ims, &theimage->imsbbox) ;
  return add_listobject(page, lobj, NULL);
}

/**
 * Returns the page's HDL
 */
HDL *dlPageHDL(const DL_STATE *page)
{
  HQASSERT(page, "No page to get HDL from") ;

  if ( page->currentHdl != NULL )
    return hdlBase(page->currentHdl) ;

  return NULL ;
}

/**
 * Returns true if the DL is empty. Erases are ignored (i.e. a dl containing
 * nothing but erases is considered empty).
 */
Bool dlIsEmpty(const DL_STATE *page)
{
  HDL *hdl = dlPageHDL(page) ;

  if ( hdl != NULL && !hdlIsEmpty(hdl) )
    return FALSE;

  if ( page->currentGroup != NULL ) {
    /** \todo MJ 08/06/10 The page group is still open and not yet on the DL.
        When it's closed it will be added, so better check it now. Is there a
        better way of handling this? */
    hdl = groupHdl(groupBase(page->currentGroup));
    if ( !hdlIsEmpty(hdl) )
      return FALSE;
  }

  return TRUE;
}

/**
 * Check for significant objects in this HDL.
 * Ignore erasepage, pseudoerasepage, beginpage and endpage objects.
 */
static Bool significantObjectsToRip(HDL *hdl)
{
  DLRANGE dlrange;

  if ( hdl == NULL )
    return FALSE;

  hdlDlrangeNoErase(hdl, &dlrange);

  for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
        dlrange_next(&dlrange) ) {
    LISTOBJECT *lobj = dlrange_lobj(&dlrange);

    HQASSERT(lobj, "lobj missing");
    HQASSERT(lobj->opcode != RENDER_erase, "Should have skipped erase");

    if ( (lobj->spflags & (RENDER_PSEUDOERASE | RENDER_BEGINPAGE |
                           RENDER_ENDPAGE)) != 0 )
      continue;

    /* Page group may already be on the DL from a previous partial paint.  In
       which case check significant objects have been added to it since. */
    if ( lobj->opcode == RENDER_group &&
         groupGetUsage(lobj->dldata.group) == GroupPage ) {
      if ( significantObjectsToRip(groupHdl(lobj->dldata.group)) )
        return TRUE;
    } else {
      return TRUE;
    }
  }

  return FALSE;
}


Bool dlSignificantObjectsToRip(const DL_STATE *page)
{
  /* Check for significant objects already on the DL. */
  if ( significantObjectsToRip(dlPageHDL(page)) )
    return TRUE;

  /* The page group isn't added to the DL until the start of rendering and
     therefore it may need to be checked separately.  If it's already on the DL
     it is checked in the first significantObjectsToRip call. */
  if ( page->currentGroup != NULL ) {
    HQASSERT(groupGetUsage(page->currentGroup) == GroupPage,
             "Should not be considering rendering within sub-groups");
    if ( significantObjectsToRip(groupHdl(page->currentGroup)) )
      return TRUE;
  }

  return FALSE; /* Nothing significant on DL. */
}


Bool bandSignificantObjectsToRip(const DL_STATE *page, int32 band)
{
  DLREF *dlref = dlref_next(dl_get_head(page, band));
  LISTOBJECT *lobj;

  if ( dlref == NULL )
    return FALSE;
  if ( dlref_next(dlref) != NULL )
    return TRUE;
  lobj = dlref_lobj(dlref);
  if ( lobj->opcode != RENDER_group )
    return TRUE;
  /* Just one object on the DL, and it's a group. */
  /* Going over the DL and checking all subgroups would not be worth it. */
  return hdlGetHead(groupHdl(lobj->dldata.group), band) != NULL;
}


/**
 * Called by the interpreter as each object is added to a DL.
 * Also called at the start of showpage.
 * Thus guaranteed to be called at least once before rendering starts.
 *
 * Used to track the creation of the first real mark on a page.
 * This can in turn trigger any lazy allocates that we do not want to do
 * until we are fairly sure that we will be outputting a page.
 */
Bool dlAddingObject(const DL_STATE *page, Bool isErase)
{
  static Bool seen_first_marking_object = FALSE;

  HQASSERT(IS_INTERPRETER(), "Render thread adding to DL");
  if ( isErase ) {
    seen_first_marking_object = FALSE;
  } else {
    if ( !seen_first_marking_object ) {
      /*
       * Can add any initialisation here that we want to occur once per
       * page, as early as possible, but only when we know we have some real
       * content. i.e. avoiding all those pages pages that may get created
       * and destroyed during pagedevice start-up or switch-over.
       */
      if ( !populate_output_resources(page) )
        return FALSE;
    }
    seen_first_marking_object = TRUE;
  }
  return TRUE;
}


/**
 * Bands in base HDL query.
 */
DLREF *dl_get_head(const DL_STATE *page, int32 bandi)
{
  DLREF *dl;

  dl = hdlGetHead(dlPageHDL(page), (uint32)bandi);
  HQASSERT(dl != NULL, "No dl for this band");
  return dl;
}


/**
 * Base HDL order list query.
 */
DLREF *dl_get_orderlist(const DL_STATE *page)
{
  HDL *hdl = dlPageHDL(page) ;

  HQASSERT(hdl != NULL, "No page HDL  from which to get order list");

  return hdlOrderList(hdl);
}

/**
 * Do the transparency attributes designate an opaque object?
 */
Bool tranAttribIsOpaque(TranAttrib *ta)
{
  return (ta == NULL ||
          (ta->blendMode == CCEModeNormal &&
           ta->alpha == COLORVALUE_ONE &&
           ta->softMask == NULL));
}


/** Are the transparency attributes the same? */
Bool tranAttribEqual(TranAttrib *ata, TranAttrib *bta)
{
  return ata == bta
    || (ata != NULL && bta != NULL && ata->alpha == bta->alpha
        && ata->alphaIsShape == bta->alphaIsShape
        && ata->blendMode == bta->blendMode && ata->softMask == bta->softMask);
}

Backdrop *tranAttribSoftMaskBackdrop(TranAttrib *ta)
{
  return ta->softMask != NULL ? groupGetBackdrop(ta->softMask->group) : NULL;
}


Bool stateobject_transparent(STATEOBJECT *objectstate)
{
  return ( (objectstate->patternstate != NULL &&
            objectstate->patternstate->backdrop) ||
           (objectstate->tranAttrib != NULL &&
            !tranAttribIsOpaque(objectstate->tranAttrib)) );
}


#if defined (DEBUG_BUILD)

#define MAX_HIT_CHARS 100

/**
 * Bundle of private info passed to the DL dumper callback
 */
typedef struct DUMP_DL_INFO {
  const DL_STATE *page;
  int32 level;
  uint32 last_depth, last_reason;
  char indent[100];
  Bool count_chars;
  struct {
    FORM *ch;
    int32 n;
  } chused[MAX_HIT_CHARS];
} DUMP_DL_INFO;

/**
 * Return a string showing meaning of given opcode
 */
static char *opcode_str(uint8 opcode)
{
  switch (opcode) {
  case RENDER_void:
    return "void";
  case RENDER_erase:
    return "erase";
  case RENDER_char:
    return "char";
  case RENDER_rect:
    return "rect";
  case RENDER_quad:
    return "quad";
  case RENDER_fill:
    return "fill";
  case RENDER_mask:
    return "mask";
  case RENDER_image:
    return "image";
  case RENDER_vignette:
    return "vignette";
  case RENDER_gouraud:
    return "gouraud";
  case RENDER_shfill:
    return "shfill";
  case RENDER_shfill_patch:
    return "shfill_patch";
  case RENDER_hdl:
    return "hdl";
  case RENDER_group:
    return "group";
  case RENDER_backdrop:
    return "backdrop";
  case RENDER_cell:
    return "cell";
  default:
    return "Unknown";
  }
}

/**
 * Return a string showing meaning of marker in symbolic form
 */
static char *marker_str(uint8 marker)
{
  static char markstr[10];
  int32 i = 0;

  if ( marker & MARKER_VN_VNCANDIDATE )
    markstr[i++] = 'V';
  if ( marker & MARKER_VN_WHITEOBJECT )
    markstr[i++] = 'W';
  if ( marker & MARKER_VN_FIXKNOCKOUT )
    markstr[i++] = 'K';
  if ( marker & MARKER_TRANSPARENT )
    markstr[i++] = 'T';
  if ( marker & MARKER_DEVICECOLOR )
    markstr[i++] = 'D';
  if ( marker & MARKER_DL_FORALL )
    markstr[i++] = 'F';
  if ( marker & MARKER_ONDISK )
    markstr[i++] = 'O';

  markstr[i] = '\0';

  return markstr;
}

/**
 * Return a string showing meaning of spflags in symbolic form
 */
static char *spflags_str(uint8 spflags)
{
  static char spflagsstr[10];
  int32 i = 0;

  if ( spflags & RENDER_UNTRAPPED )
    spflagsstr[i++] = 'U';
  if ( spflags & RENDER_RECOMBINE )
    spflagsstr[i++] = 'R';
  if ( spflags & RENDER_PSEUDOERASE )
    spflagsstr[i++] = 'e';
  if ( spflags & RENDER_KNOCKOUT )
    spflagsstr[i++] = 'K';
  if ( spflags & RENDER_BEGINPAGE )
    spflagsstr[i++] = 'B';
  if ( spflags & RENDER_ENDPAGE )
    spflagsstr[i++] = 'E';
  if ( spflags & RENDER_BACKGROUND )
    spflagsstr[i++] = 'B';
  if ( spflags & RENDER_PATTERN )
    spflagsstr[i++] = 'P';

  spflagsstr[i] = '\0';

  return spflagsstr;
}

/**
 * Debug printout of the given DL object disposition
 */
static char *disposition_str(uint8 disposition)
{
  static char dispstr[10];
  int32 i = 0;

  switch ( DISPOSITION_REPRO_TYPE_UNMAPPED(disposition) ) {
    case REPRO_TYPE_PICTURE:
      dispstr[i++] = 'P';
      break;
    case REPRO_TYPE_TEXT:
      dispstr[i++] = 'T';
      break;
    case REPRO_TYPE_VIGNETTE:
      dispstr[i++] = 'V';
      break;
    case REPRO_TYPE_OTHER:
      dispstr[i++] = 'O';
      break;
    case REPRO_DISPOSITION_ERASE:
      dispstr[i++] = 'E';
      break;
    case REPRO_DISPOSITION_RENDER:
      dispstr[i++] = 'R';
      break;
    default:
      dispstr[i++] = '?';
      break;
  }
  if ( disposition & DISPOSITION_FLAG_USER )
    dispstr[i++] = 'u';
  else
    dispstr[i++] = '-';

  switch ( DISPOSITION_COLORTYPE(disposition) ) {
    case GSC_FILL:
      dispstr[i++] = 'F';
      break;
    case GSC_STROKE:
      dispstr[i++] = 'S';
      break;
    case GSC_IMAGE:
      dispstr[i++] = 'I';
      break;
    case GSC_SHFILL:
      dispstr[i++] = 'H';
      break;
    case GSC_SHFILL_INDEXED_BASE:
      dispstr[i++] = 'h';
      break;
    case GSC_VIGNETTE:
      dispstr[i++] = 'V';
      break;
    case GSC_BACKDROP:
      dispstr[i++] = 'B';
      break;
    case GSC_N_COLOR_TYPES:
      dispstr[i++] = 'N';
      break;
    default:
      dispstr[i++] = '?';
      break;
  }

  dispstr[i] = '\0';

  return dispstr;
}

/**
 * Debug printout of the given STATEOBJECT
 */
static void dump_objstate(DUMP_DL_INFO *ddl, STATEOBJECT *dlstate)
{
  TranAttrib *ta = dlstate->tranAttrib;
  LateColorAttrib *lca = dlstate->lateColorAttrib;
  PclAttrib *pcl = dlstate->pclAttrib;

#define TF(tf_) ((tf_) ? 'T' : 'F')

  monitorf((uint8 *)"%s    (state @ %x : spot %d pat @ %x, tran @ %x",
           ddl->indent, dlstate, dlstate->spotno,
           dlstate->patternstate, ta);
  if ( ta )
    monitorf((uint8 *)" (%x:%u:%c:%x)",
             ta->alpha, ta->blendMode, TF(ta->alphaIsShape), ta->softMask);
  monitorf((uint8 *)", lca @ %x", lca);
  if ( lca )
    monitorf((uint8 *)" (%u:%u:%c:%u:%c)",
             lca->origColorModel, lca->renderingIntent, TF(lca->overprintMode),
             lca->blackType, TF(lca->independentChannels));
  monitorf((uint8 *)", pcl @ %x", pcl);
  if ( pcl )
    monitorf((uint8 *)" (%c:%c:%u:%u:%06x:%s:%s:%c:%c)",
             TF(pcl->sourceTransparent), TF(pcl->patternTransparent),
             pcl->rop, pcl->foregroundSource, pcl->foreground,
             debug_string_pclPatternColors(pcl->patternColors),
             debug_string_pclXORState(pcl->xorstate),
             TF(pcl->backdrop), TF(pcl->patternBlit));
  monitorf((uint8 *)")\n");

#undef TF
}

/**
 * Callback function to provide debug printout of gouraud colors
 */
static Bool dump_gour_func(p_ncolor_t *pp_ncolor, void *data)
{
  DUMP_DL_INFO *ddl = (DUMP_DL_INFO *)data;
  dl_color_t dlc;

  if ( !dlc_from_dl(ddl->page->dlc_context, pp_ncolor, &dlc) )
    return FALSE;
  monitorf((uint8 *)"%s    (", ddl->indent, pp_ncolor);
  if ( dlc.ce.prc )
    debug_print_ce(&(dlc.ce), FALSE);
  monitorf((uint8 *)")\n");
  dlc_release(ddl->page->dlc_context, &dlc);
  return TRUE;
}

/**
 * Debug printout of the opcode-specific bits of the given LISTOBJECT
 */
static void dump_opcode(LISTOBJECT *lobj, DUMP_DL_INFO *ddl)
{
  int32 i, j ;
  size_t bytes = sizeof(LISTOBJECT);

  switch ( lobj->opcode ) {
    case RENDER_void:
      break;
    case RENDER_erase:
      monitorf((uint8 *)"%s    %s %s\n", ddl->indent,
          lobj->dldata.erase.newpage ? "new page" : "partial paint",
          lobj->dldata.erase.with0   ? "zero erase" : "non-zero erase");
      break;
    case RENDER_char: {
      DL_CHARS *dlchars = lobj->dldata.text;

      bytes = sizeof(LISTOBJECT) + sizeof(DL_CHARS) +
              sizeof(DL_GLYPH) * (dlchars->nchars-1);
      for ( i = 0; i < dlchars->nchars; i++ ) {
        dcoord x   = dlchars->ch[i].x;
        dcoord y   = dlchars->ch[i].y;
        FORM *form = dlchars->ch[i].form;

        if ( form->type == FORMTYPE_CHARCACHE )
          form = ((CHARCACHE*)form)->thebmapForm;
        monitorf((uint8 *)"%s    glyph @ (%d, %d) %x", ddl->indent, x, y, form);
        monitorf((uint8 *)" %d*%d %d\n", form->w, form->h, form->size);

        if ( ddl->count_chars ) {
          for ( j = 0; j < MAX_HIT_CHARS; j++ ) {
            if ( ddl->chused[j].ch == NULL || ddl->chused[j].ch == form ) {
              ddl->chused[j].ch = form;
              ddl->chused[j].n++;
              break;
            }
          }
          if ( j == MAX_HIT_CHARS )
            ddl->count_chars = FALSE;
        }
      }
      break;
    }
    case RENDER_rect:
      monitorf((uint8 *)"%s    rect @ (%d, %d) : %d*%d\n", ddl->indent,
          lobj->bbox.x1, lobj->bbox.y1,
          lobj->bbox.x2 - lobj->bbox.x1,
          lobj->bbox.y2 - lobj->bbox.y1);

      break;
    case RENDER_quad:
      monitorf((uint8 *)"%s    ", ddl->indent);
      debug_print_quad(lobj->dldata.quad, &lobj->bbox) ;
      break;
    case RENDER_fill: {
      monitorf((uint8 *)"%s    ", ddl->indent);
      bytes += sizeof_nfill(lobj->dldata.nfill);
      debug_print_nfill(lobj->dldata.nfill, ddl->level > 2 ? 1 : 0);
      break;
    }
    case RENDER_mask:
    case RENDER_image: {
      IMAGEOBJECT *im = lobj->dldata.image;
      int32 bpp = im->ims ? im_storebpp(im->ims) : 0;
      int32 rw = im->imsbbox.x2 - im->imsbbox.x1 + 1 ;
      int32 rh = im->imsbbox.y2 - im->imsbbox.y1 + 1 ;

      monitorf((uint8 *)"%s    image @ %x %d*%d*%d (%d,%d %d,%d) -> %d*%d (%d,%d %d,%d)\n",
               ddl->indent, im, rw, rh, bpp,
               im->imsbbox.x1, im->imsbbox.y1, im->imsbbox.x2, im->imsbbox.y2,
               im->bbox.x2 - im->bbox.x1 + 1, im->bbox.y2 - im->bbox.y1 + 1,
               im->bbox.x1, im->bbox.y1, im->bbox.x2, im->bbox.y2);
      if ( im_expand_ims_alpha(im->ime) )
        monitorf((uint8 *)"%s      alpha channel present\n", ddl->indent);
      bytes += sizeof(IMAGEOBJECT);
      bytes += rw*rh*bpp/8;
      if ( im->mask ) {
        im = im->mask;
        bpp = im->ims ? im_storebpp(im->ims) : 0;
        rw = im->imsbbox.x2 - im->imsbbox.x1 + 1 ;
        rh = im->imsbbox.y2 - im->imsbbox.y1 + 1 ;

        monitorf((uint8 *)"%s      mask @ %x %d*%d*%d (%d,%d %d,%d) -> %d*%d (%d,%d %d,%d)\n",
                 ddl->indent, im, rw, rh, bpp,
                 im->imsbbox.x1, im->imsbbox.y1, im->imsbbox.x2, im->imsbbox.y2,
                 im->bbox.x2 - im->bbox.x1 + 1, im->bbox.y2 - im->bbox.y1 + 1,
                 im->bbox.x1, im->bbox.y1, im->bbox.x2, im->bbox.y2);
        bytes += sizeof(IMAGEOBJECT);
        bytes += rw*rh*bpp/8;
      }
      break;
    }
    case RENDER_gouraud: {
      GOURAUDOBJECT *g = lobj->dldata.gouraud;

      monitorf((uint8 *)"%s    coords (%d,%d) (%d,%d) (%d,%d) %d\n",
          ddl->indent, g->coords[0], g->coords[1], g->coords[2], g->coords[3],
          g->coords[4], g->coords[5], g->gsize);
      (void)gouraud_iterate_dlcolors(lobj, dump_gour_func, ddl);
      bytes += g->gsize;
    }
    case RENDER_shfill:
    case RENDER_vignette:
    case RENDER_shfill_patch:
    case RENDER_hdl:
    case RENDER_group:
    case RENDER_cell:
    case RENDER_backdrop:
    default:
      break;
  }
  monitorf((uint8 *)"%s    %d bytes\n", ddl->indent, (int32)bytes);
}

/**
 * Callback from dl forall to provide Debug printout of the
 * specified display list objects.
 *
 * Currently does not print out all the detailed information present,
 * and has a lot of redundant labels and data duplication.
 * Need to experiment to produce the optimal output with the easiest
 * to view content.
 */
static Bool dump_dl(DL_FORALL_INFO *info)
{
  DUMP_DL_INFO *ddl = (DUMP_DL_INFO *)info->data;
  LISTOBJECT *lobj = info->lobj;
  uint32 i;

  if ( info->depth * 2 + 1 >= sizeof(ddl->indent) ) {
    for ( i = 0; i < info->depth*2; i++)
      monitorf((uint8 *)" ");
    monitorf((uint8 *)"  %d-%s : ", lobj->opcode, opcode_str(lobj->opcode));
    return TRUE;
  }
  for ( i = 0; i < info->depth*2; i++)
    ddl->indent[i] = ' ';
  ddl->indent[i] = '\0';

  if ( ddl->last_depth < info->depth ) {
    if ( info->reason & DL_FORALL_PATTERN )
      monitorf((uint8 *)"%s  --pattern DL--\n", ddl->indent);
    else if ( info->reason & DL_FORALL_SOFTMASK )
      monitorf((uint8 *)"%s  --softmask DL--\n", ddl->indent);
    else
      monitorf((uint8 *)"%s  --sub DL--\n", ddl->indent);
  }
  ddl->last_depth = info->depth;
  ddl->last_reason = info->reason;

  monitorf((uint8 *)"%s  %d-%s : ", ddl->indent, lobj->opcode,
                                    opcode_str(lobj->opcode));
  if ( ddl->level > 1 ) {
    STATEOBJECT   *dlstate = lobj->objectstate;
    dl_color_t dlc;

#if defined(DEBUG_BUILD) && defined(DEBUG_TRACK_SETG)
    monitorf((uint8 *)"setg %d ", lobj->setg_count);
#endif

    monitorf((uint8 *)"%x(%s), %x(%s), %x(%s)",
        lobj->marker, marker_str(lobj->marker),
        lobj->spflags, spflags_str(lobj->spflags),
        lobj->disposition, disposition_str(lobj->disposition));
    monitorf((uint8 *)" 0x%08x [(%d,%d),(%d,%d)]\n", lobj->dldata.nfill,
        lobj->bbox.x1, lobj->bbox.y1,
        lobj->bbox.x2, lobj->bbox.y2);
    dump_objstate(ddl, dlstate);

    dlc_from_lobj_weak(lobj, &dlc);
    monitorf((uint8 *)"%s    (", ddl->indent);
    if ( dlc.ce.prc )
      debug_print_ce(&(dlc.ce), FALSE);
    else
      monitorf((uint8 *)"NULL color");
    monitorf((uint8 *)")\n");

    if ( (lobj->marker & MARKER_ONDISK) == 0 )
      dump_opcode(lobj, ddl);
  } else
    monitorf((uint8 *)"\n");
  return TRUE;
}

/**
 * Debug function to print out the contents of the given display list
 * in a nice format.
 *
 * \todo BMJ 04-Nov-08 :  only does orderlist, banded info might be nice too
 */
void debug_print_dl(const DL_STATE *page, int32 level)
{
  DLREF *dl;

  if ( page->currentHdl == NULL )
    return;
  dl = dl_get_orderlist(page);
  if ( level ) {
    DL_FORALL_INFO info;
    DUMP_DL_INFO ddl;
    int32 j;

    for ( j = 0 ; j < MAX_HIT_CHARS; j++ )
      ddl.chused[j].ch = NULL, ddl.chused[j].n = 0;
    ddl.count_chars = TRUE;
    ddl.page = page;
    ddl.level = level;
    ddl.last_depth = 0xff;

    monitorf((uint8*)"---- Display list @ 0x%08x ----\n", page);
    monitorf((uint8*)"  Pagesize = %d*%d, lines/band = %d bands/page = %d\n",
             page->page_w, page->page_h, page->band_lines,
             page->sizedisplaylist);
    info.page = (DL_STATE*)page;
    info.hdl  = page->currentHdl;
    info.data = &ddl;
    info.inflags = DL_FORALL_PATTERN|DL_FORALL_SOFTMASK|DL_FORALL_SHFILL|
                   DL_FORALL_GROUP|DL_FORALL_NONE;
    (void)dl_forall(&info, dump_dl);

    monitorf((uint8*)"  Total glyph usage\n");
    for ( j = 0; j < MAX_HIT_CHARS && ddl.chused[j].n > 0; j++ )
      monitorf((uint8*)"    form @ %x hits %d %d*%d %d bytes\n",
          ddl.chused[j].ch, ddl.chused[j].n,
          ddl.chused[j].ch->w, ddl.chused[j].ch->h, ddl.chused[j].ch->size);
    if ( j == MAX_HIT_CHARS )
      monitorf((uint8*)"    .....\n");

    monitorf((uint8*)"------------------------------------\n");
  }
}

#endif /* DEBUG_BUILD */

/**
 * Sums up memory used by DL.
 */
size_t dl_mem_used(DL_STATE *page)
{
  size_t i, used = 0;

  for ( i = 0; i < N_DL_POOLS; i++ )
    used += mm_pool_alloced_size(page->dlpools[i]);
  return used;
}


#ifdef METRICS_BUILD
static Bool dl_metrics_update(sw_metrics_group *metrics)
{
  size_t total_max_size = 0, all_max_frag = 0, max_size = 0, max_frag = 0;
  int32 max_objects = 0, total_max_objects = 0;
  int i, j ;

  for ( i = 0; i < NUM_DISPLAY_LISTS; ++i ) {
    /** \todo ajcd 2011-02-26: Lock render pages, they may be finalised while
        we're doing this. */
    for ( j = 0; j < N_DL_POOLS; ++j ) {
      if ( dl_pages[i].dlpools[j] != NULL ) {
        mm_debug_total_highest(dl_pages[i].dlpools[j],
                               &max_size, &max_objects, &max_frag);
        /* Adding the maximums is assuming they occurred at the same time,
           which might not be true, but is a good approximation.  The
           totals could overflow, but it's just debug output. */
        total_max_size += max_size; total_max_objects += max_objects;
        if ( all_max_frag < max_frag )
          all_max_frag = max_frag;
      }
    }
  }
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("DL")) )
    return FALSE;
  SW_METRIC_INTEGER("PeakPoolSize", (int32)total_max_size);
  SW_METRIC_INTEGER("PeakPoolObjects", total_max_objects);
  SW_METRIC_INTEGER("PeakPoolFragmentation", (int32)all_max_frag);
  sw_metrics_close_group(&metrics);
  sw_metrics_close_group(&metrics);
  return TRUE;
}

static void dl_metrics_reset(int reason)
{
  UNUSED_PARAM(int, reason);
}

static sw_metrics_callbacks dl_metrics_hook = {
  dl_metrics_update,
  dl_metrics_reset,
  NULL
};


#endif


#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
#define DL_DEBUG 1
#endif

#if DL_DEBUG

/** \todo ajcd 2011-02-26: This debugging doesn't take into account
    DL pipelining. */

int32 debug_dl;

struct {
  size_t n, max, bytes;
} all_dl_allocs[MM_ALLOC_CLASS_LIMIT];

void track_dl(size_t bytes, uint32 allocclass, Bool alloc)
{
  HQASSERT(allocclass < MM_ALLOC_CLASS_LIMIT, "Unknown DL alloc-class");
  if ( debug_dl & DEBUG_DL_TRACKDL ) {
    if ( alloc ) {
      all_dl_allocs[allocclass].n++;
      all_dl_allocs[allocclass].bytes += bytes;
      if ( all_dl_allocs[allocclass].max < bytes )
        all_dl_allocs[allocclass].max = bytes;
    } else {
      all_dl_allocs[allocclass].n--;
      all_dl_allocs[allocclass].bytes -= bytes;
    }
  }
}


static void clear_track_dl()
{
  int i;

  if ( debug_dl & DEBUG_DL_TRACKDL ) {
    for ( i = 0; i < MM_ALLOC_CLASS_LIMIT; i++ ) {
      all_dl_allocs[i].n = 0;
      all_dl_allocs[i].max = 0;
      all_dl_allocs[i].bytes = 0;
    }
  }
}

void report_track_dl(char *title, size_t size)
{
  DL_STATE *page = CoreContext.page;
  int i;
  size_t total = 0;

  if ( ( debug_dl & DEBUG_DL_TRACKDL ) &&
       all_dl_allocs[MM_ALLOC_CLASS_LIST_OBJECT].n > 1 ) { /* not blank page */
    monitorf((uint8 *)"Tracking DL Allocs '%s:%Pu'...\n", title, size);
    for ( i = 0; i < MM_ALLOC_CLASS_LIMIT; i++ ) {
      if ( all_dl_allocs[i].n != 0 ) {
        monitorf((uint8 *)"  DL alloc[%d] : %Pu %Pu %Pu\n", i,
                 all_dl_allocs[i].n, all_dl_allocs[i].bytes, all_dl_allocs[i].max);
        total += all_dl_allocs[i].bytes;
      }
    }
    monitorf((uint8 *)"  Total bytes %Pu\n", total);
    for ( i = 0; i < N_DL_POOLS; i++ ) {
      mm_pool_t pool = page->dlpools[i];

      if ( pool )
        monitorf((uint8*)"  Pool[%d] : %Pu/%Pu \n", i,
                 mm_pool_size(pool), mm_pool_free_size(pool));
      else
        monitorf((uint8*)"  Pool[%d] : ?/?/?\n", i);
    }
    if ( mm_pool_shading )
      monitorf((uint8*)"  SHADE_Pool : %Pu/%Pu \n",
               mm_pool_size(mm_pool_shading), mm_pool_free_size(mm_pool_shading));
    monitorf((uint8 *)"  Non DL %Pu/%Pu\n",
             mm_total_size(), mm_no_pool_size(TRUE));
    mm_pool_memstats();
  }
  clear_track_dl();
}


#else /* !DL_DEBUG */

#define clear_track_dl() EMPTY_STATEMENT()

#endif /* !DL_DEBUG */


static Bool dl_pool_swinit(struct SWSTART *params)
{
  int i, j;

  UNUSED_PARAM(struct SWSTART *, params) ;

  for ( i = 0; i < NUM_DISPLAY_LISTS; ++i ) {
    for ( j = 0; j < N_DL_POOLS; ++j ) { /* Create the dl pools */
      if ( j != dl_choose_pool_index(MM_ALLOC_CLASS_DLREF)
           ? mm_pool_create(&dl_pages[i].dlpools[j], DL_POOL_TYPE,
                             DL_POOL_PARAMS)
             != MM_SUCCESS
           : (mm_pool_create(&dl_pages[i].dlpools[j], DL_FAST_POOL_TYPE,
                             DL_POOL_PARAMS)
                != MM_SUCCESS
              || mm_ap_create(&dl_pages[i].dl_ap, dl_pages[i].dlpools[j])
                   != MM_SUCCESS) ) {
        do {
          while ( --j >= 0 ) {
            mm_pool_destroy(dl_pages[i].dlpools[j]);
            dl_pages[i].dlpools[j] = NULL;
          }
          j = N_DL_POOLS ;
        } while ( --i >= 0 ) ;
        return FALSE;
      }
    }

    /* Seed the initial inputpage from the timeline reference. Each handoff
       between pages sets the next page to the job timeline. */
    HQASSERT(core_tl_ref != SW_TL_REF_INVALID,
             "Core timeline not initialised") ;
    dl_pages[i].timeline = core_tl_ref ;
  }

  multi_mutex_init(&inputpage_mutex, INPUTPAGE_LOCK_INDEX, FALSE,
                   SW_TRACE_INPUT_PAGE_ACQUIRE, SW_TRACE_INPUT_PAGE_HOLD);
  multi_mutex_init(&outputpage_mutex, OUTPUTPAGE_LOCK_INDEX, FALSE,
                   SW_TRACE_OUTPUT_PAGE_ACQUIRE, SW_TRACE_OUTPUT_PAGE_HOLD);

  return TRUE ;
}


static Bool dl_pool_postboot(void)
{
  int i ;

  inputpage->hr = gsc_getRS(gstateptr->colorInfo) ;
  guc_reserveRasterStyle(inputpage->hr) ;

  /* Initialise the PGB proxy params for each DL page. */
  for ( i = 0; i < NUM_DISPLAY_LISTS; ++i )
    if ( !pgbproxy_init(&dl_pages[i], mm_pool_temp) )
      return FALSE ;
  /* Start with the interpreting page proxy talking to the real PGB device. */
  if ( !pgbproxy_setflush(inputpage, TRUE) )
    return FALSE ;

  /* Ensure that there is a core job tracker object. */
  if ( !corejob_create(inputpage, NULL) )
    return FALSE ;

  fndecodes_init();
  return TRUE ;
}


static void dl_pool_finish(void)
{
  int i, j;

  for ( i = 0; i < NUM_DISPLAY_LISTS; ++i ) {
    DL_STATE *page = &dl_pages[i] ;

    pgbproxy_finish(page) ;

    /** \todo ajcd 2011-03-21: Assert that everything's cleaned up
        properly by stop_interpreter(). */

    for ( j = 0; j < N_DL_POOLS; ++j ) {
      if ( page->dlpools[j] ) {
        if ( j == dl_choose_pool_index(MM_ALLOC_CLASS_DLREF) )
          mm_ap_destroy(page->dl_ap, page->dlpools[j]);
        mm_pool_destroy(page->dlpools[j]);
        page->dlpools[j] = NULL;
      }
    }
  }

  multi_mutex_finish(&outputpage_mutex);
  multi_mutex_finish(&inputpage_mutex);
  fndecodes_term();
}

void dl_pool_C_globals(core_init_fns *fns)
{
  fns->swinit = dl_pool_swinit ;
  fns->postboot = dl_pool_postboot ;
  fns->finish = dl_pool_finish ;
}

static Bool dl_misc_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

#ifdef DEBUG_BUILD
  register_ripvar(NAME_debug_dlstore, OINTEGER, &debug_dlstore) ;
  register_ripvar(NAME_debug_dl_x1, OINTEGER, &debug_focus.x1) ;
  register_ripvar(NAME_debug_dl_y1, OINTEGER, &debug_focus.y1) ;
  register_ripvar(NAME_debug_dl_x2, OINTEGER, &debug_focus.x2) ;
  register_ripvar(NAME_debug_dl_y2, OINTEGER, &debug_focus.y2) ;
  register_ripvar(NAME_debug_dl_firstimage, OINTEGER, &debug_dl_firstimage) ;
  register_ripvar(NAME_debug_dl_lastimage, OINTEGER, &debug_dl_lastimage) ;
  register_ripvar(NAME_debug_dl_imageopt, OINTEGER, &debug_dl_imageopt) ;
  register_ripvar(NAME_debug_dl_firstsetg, OINTEGER, &debug_dl_firstsetg) ;
  register_ripvar(NAME_debug_dl_lastsetg, OINTEGER, &debug_dl_lastsetg) ;
  register_ripvar(NAME_debug_dl_drop, OINTEGER, &debug_dl_drop) ;
  register_ripvar(NAME_debug_pcl_idiom, OINTEGER, &debug_pcl_idiom) ;
  register_ripvar(NAME_debug_pcl_idiom_control, OINTEGER, &debug_pcl_idiom_control) ;
  pgbproxy_debug_init() ;
#endif
#if DL_DEBUG
  register_ripvar(NAME_debug_dl, OINTEGER, &debug_dl);
#endif

  clear_track_dl();

  return TRUE ;
}

void dl_misc_C_globals(core_init_fns *fns)
{
  fns->swstart = dl_misc_swstart ;
}

/**
 * Clear the DL pools.
 */
static void dl_clear_pools(DL_STATE *page)
{
  size_t i;

  report_track_dl("clear pools", 0);
  for ( i = 0; i < N_DL_POOLS; i++ )
    mm_pool_clear(page->dlpools[i]);
}


/**
 * Choose DL pool from the full set passed, based on an allocation class.
 *
 * \param[in]  pools  An array of DL pools to choose from.
 * \param[in]  allocclass  The class of object being allocated.
 * \return The pool to use.
 */
mm_pool_t dl_choosepool(const mm_pool_t pools[N_DL_POOLS], uint32 allocclass)
{
  return pools[dl_choose_pool_index(allocclass)];
}


/**
 * Allocate the DL object of given size and type.
 */
void *dl_alloc(const mm_pool_t pools[N_DL_POOLS], size_t bytes,
               uint32 allocclass)
{
  void *ptr;

  HQASSERT(pools, "NULL pointer to DL memory pools");
  HQASSERT(bytes > 0, "zero bytes DL alloc");

  track_dl(bytes, allocclass, TRUE);
  ptr = mm_alloc(dl_choosepool(pools, allocclass), (mm_size_t)bytes,
                 (mm_alloc_class_t)allocclass);
  if ( ptr == NULL )
    report_track_dl("dl_alloc failure", bytes);
  return ptr;
}

/**
 * Free the DL object of given address, size and type.
 */
void dl_free(const mm_pool_t pools[N_DL_POOLS], void *addr, size_t size,
             uint32 allocclass)
{
  HQASSERT(pools, "NULL pointer to DL memory pools");
  HQASSERT(addr, "NULL DL memory free");

  track_dl(size, allocclass, FALSE);
  mm_free(dl_choosepool(pools, allocclass), (mm_addr_t)addr, (mm_size_t)size);
}

/**
 * Allocate the DL object of given size and type.
 * Note, allocations with a header are not tracked for debugging.
 */
void *dl_alloc_with_header(const mm_pool_t pools[N_DL_POOLS], size_t bytes,
                           uint32 allocclass)
{
  void *ptr;

  HQASSERT(pools, "NULL pointer to DL memory pools");
  HQASSERT(bytes > 0, "zero bytes DL alloc");

  ptr = mm_alloc_with_header(dl_choosepool(pools, allocclass), (mm_size_t)bytes,
                             (mm_alloc_class_t)allocclass);
  if ( ptr == NULL )
    report_track_dl("dl_alloc failure", bytes);
  return ptr;
}

/**
 * Free the DL object of given address and type.
 * Note, allocations with a header are not tracked for debugging.
 */
void dl_free_with_header(const mm_pool_t pools[N_DL_POOLS], void *addr,
                         uint32 allocclass)
{
  HQASSERT(pools, "NULL pointer to DL memory pools");
  HQASSERT(addr, "NULL DL memory free");

  mm_free_with_header(dl_choosepool(pools, allocclass), (mm_addr_t)addr);
}

/**
 * Step the DL iterator back one place.
 *
 * DLRANGE is an object representing a range of elements in the DL.
 * It also acts as an iterator object for stepping through the DL, either
 * fowards or backwards. The forwards implementation is quite straight forward.
 * But the backwards one is very basic and ony kicks in when the DL has been
 * purged to disk.
 *
 * Given we only have a single linked list, stepping backwards through it
 * is a pain. For a list of length 'n', if we search from the begiining each
 * time to find the element in front of the current one, total time spent will
 * be O(n^2). An alternative is to remember a position a distance sqrt(n) back
 * in the display list, and start the search from here each time. When we get
 * back to this starting point, move it back again by a distance sqrt(n) etc.
 * This will reduce total search time to O(n^1.5). This is the best we can
 * easily do without making the list doubly-linked, or having enough memory to
 * create a reversed copy, or to reverse it in situ (and make multi-threading
 * difficult).
 *
 * \todo BMJ 17-Feb-09 : Investigate benefits of a doubly linked list ?
 */
static void dlrange_stepback(DLRANGE *dlrange)
{
  if ( dlrange->current.dlref->inMemory || dlrange->current.index == 0 ) {
    DLREF *dl = dlrange->start.dlref;

    if ( dl == NULL || dl == dlrange->current.dlref )
      dlrange->current.dlref = NULL;
    else {
      if ( dlrange->current.dlref == dlrange->backN ) {
        int32 n = 0;
        DLREF *backN = dl;

        while ( dl && dl->next != dlrange->current.dlref ) {
          dl = dl->next;
          n++;
          if ( n >= dlrange->sqrt_len )
            backN = backN->next;
        }
        dlrange->backN = backN;
      }
      dl = dlrange->backN;

      while ( dl->next != dlrange->current.dlref )
        dl = dl->next;
      dlrange->current.dlref = dl;
      dlrange->current.index = dlrange->current.dlref->nobjs - 1;
    }
  } else
    dlrange->current.index--;
}

/**
 * Initialise the DLRANGE to empty
 */
void dlrange_init(DLRANGE *dlrange)
{
  dlrange->start.dlref   = NULL;
  dlrange->start.index   = 0;
  dlrange->end.dlref     = NULL;
  dlrange->end.index     = 0;
  dlrange->current.dlref = NULL;
  dlrange->current.index = 0;
  dlrange->forwards      = TRUE;
  dlrange->backN         = NULL;
  dlrange->sqrt_len      = 1;
  dlrange->common_render = FALSE;
  dlrange->read_lobj     = FALSE;
  dlrange->lobj.opcode   = RENDER_void;
  dlrange->writeBack     = FALSE;
}

void dlrange_start(DLRANGE *dlrange)
{
  HQASSERT(dlrange, "Corrupt NULL DL");
  dlrange->current = dlrange->start;
  dlrange->read_lobj   = FALSE;
  dlrange->lobj.opcode = RENDER_void;
  dlrange->writeBack   = FALSE;
  if ( !dlrange->forwards ) {
    int32 n = 0;

    while ( dlrange->current.dlref && dlrange->current.dlref->next !=
            dlrange->end.dlref ) {
      dlrange->current.dlref = dlrange->current.dlref->next;
      n++;
    }
    while ( dlrange->sqrt_len * dlrange->sqrt_len < n )
      dlrange->sqrt_len++;
    dlrange->backN = dlrange->current.dlref;
    dlrange->current.index = dlrange->current.dlref->nobjs - 1;
  }
}

Bool dlrange_done(DLRANGE *dlrange)
{
  HQASSERT(dlrange, "Corrupt NULL DL");
  if ( !dlrange->forwards )
    return ( dlrange->current.dlref == NULL );
  return ( dlrange->current.dlref == NULL ||
           (dlrange->current.dlref == dlrange->end.dlref &&
            dlrange->current.index == dlrange->end.index) );
}

void dlrange_next(DLRANGE *dlrange)
{
  HQASSERT(dlrange, "Corrupt NULL DL");
  HQASSERT(dlrange->current.dlref, "Stepping DL beyond end of list");
  if ( !dlrange->forwards )
    dlrange_stepback(dlrange);
  else {
    if ( dlrange->current.dlref->inMemory ) {
      dlrange->current.dlref = dlref_next(dlrange->current.dlref);
      dlrange->current.index = 0;
    } else {
      HQASSERT(dlrange->current.index < dlrange->current.dlref->nobjs,
               "Stepped off the end of dl object container on disk");
      if ( dlrange->writeBack ) {
        Bool ok;

        ok = dlref_rewrite(dlrange->current.dlref, dlrange->current.index,
                           &(dlrange->lobj));
        HQASSERT(ok, "Failed to write modified DL obj back to disk");
        dlrange->writeBack = FALSE;
      }
      dlrange->current.index++;
      if ( dlrange->current.index >= dlrange->current.dlref->nobjs ) {
        dlrange->current.dlref = dlrange->current.dlref->next;
        dlrange->current.index = 0;
      }
    }
  }
  dlrange->read_lobj = FALSE;
  dlrange->writeBack = FALSE;
}

LISTOBJECT *dlrange_lobj(DLRANGE *dlrange)
{
  HQASSERT(dlrange, "Corrupt NULL DL");
  HQASSERT(dlrange->current.dlref, "Accessing DL beyond end of list");

  if ( dlrange->current.dlref->inMemory )
    return dlref_lobj(dlrange->current.dlref);
  else {
    HQASSERT(dlrange->current.index < dlrange->current.dlref->nobjs,
             "Stepped off the end of dl object container on disk");
    if ( !dlrange->read_lobj ) {
      dlrange->read_lobj = TRUE;
      if ( !dlref_readfromdisk(dlrange->current.dlref, dlrange->current.index,
                               &(dlrange->lobj)) )
        return NULL;
    }
    return &(dlrange->lobj);
  }
}

/**
 * Save the current position in a DL iteration as the start location
 * for a future iterable range.
 */
void dlrange_setstart(DLRANGE *dlrange, DLRANGE *iter)
{
  dlrange->start = iter->current;
}

/**
 * Save the current position in a DL iteration as the end location
 * for a future iteratable range.
 */
void dlrange_setend(DLRANGE *dlrange, DLRANGE *iter)
{
  if ( iter )
    dlrange->end = iter->current;
  else
    dlrange->end.dlref = NULL;
}

/**
 * Function to render a single DL object.
 *
 * Object is supplied, but optionally without any objectstate.
 * In such cases, make it inherit the state from the erase object.
 */
Bool render_single_listobj(render_info_t *p_ri, LISTOBJECT *lobj)
{
  DLRANGE dlrange;
  DLREF dlref;

  if ( lobj->objectstate == NULL )
    lobj->objectstate = p_ri->p_rs->lobjErase->objectstate;

  dlref.inMemory = (uint16)TRUE;
  dlref.nobjs    = 1;
  dlref.dl.lobj  = lobj;
  dlref.next     = NULL;
  dlrange_init(&dlrange);
  dlrange.start.dlref = &dlref;

  return render_object_list_of_band(p_ri, &dlrange);
}

Bool dl_reserve_band(DL_STATE *page, uint8 bits)
{
  HQASSERT(IS_INTERPRETER(), "Reserving band too late") ;
  HQASSERT(page != NULL, "No page in which to reserve band") ;
  HQASSERT((bits & ~(RESERVED_BAND_CLIPPING |
                     RESERVED_BAND_PATTERN |
                     RESERVED_BAND_PATTERN_SHAPE |
                     RESERVED_BAND_PATTERN_CLIP |
                     RESERVED_BAND_MASKED_IMAGE |
                     RESERVED_BAND_SELF_INTERSECTING |
                     RESERVED_BAND_MODULAR_SCREEN |
                     RESERVED_BAND_MODULAR_MAP)) == 0,
           "Reserved band mask out of range") ;

  /* If we've already got all of the bits set already, then we're done. */
  if ( (page->reserved_bands & bits) != bits ) {
    uint8 reserved_bands = page->reserved_bands | bits ;
    requirement_node_t *expr ;

    expr = requirement_node_find(page->render_resources, REQUIREMENTS_BAND_GROUP) ;
    HQASSERT(expr != NULL, "No band group requirements") ;

    /* Remove existing bits from new mask */
    bits &= ~page->reserved_bands ;

    /* Are there new requests for mask bands? If so, make sure the minimum
       number required are pre-allocated. */
    if ( (bits & ~RESERVED_BAND_MODULAR_MAP) != 0 &&
         !requirement_node_maxmin(expr, TASK_RESOURCE_BAND_1,
                                  count_bits_set_in_byte[reserved_bands & ~RESERVED_BAND_MODULAR_MAP]) )
      return FALSE ;

    /* Are there new requests for modular screen tone bands? If so, make sure
       the minimum number required are pre-allocated. */
#define MODULAR_SCREEN_CTS (RESERVED_BAND_MODULAR_SCREEN | \
                            RESERVED_BAND_MODULAR_MAP)
    if ( (bits & MODULAR_SCREEN_CTS) != 0 &&
         !requirement_node_maxmin(expr, TASK_RESOURCE_BAND_CT,
                                  count_bits_set_in_byte[reserved_bands & MODULAR_SCREEN_CTS]) )
      return FALSE ;

    /** \todo ajcd 2012-03-12: Trapping possibly shouldn't do this. The use of
        the clipping band for trapping is an implementation artifact, rather
        than an architectural requirement. */
#define CLIP_SURFACE_BANDS (RESERVED_BAND_CLIPPING | \
                            RESERVED_BAND_SELF_INTERSECTING | \
                            RESERVED_BAND_MASKED_IMAGE | \
                            RESERVED_BAND_PATTERN_CLIP)
    if ( (bits & CLIP_SURFACE_BANDS) != 0 )
      dl_surface_used(page, SURFACE_CLIP) ;

    page->reserved_bands = reserved_bands ;

    /* Are there any new bits affecting trapping? */
    if ( (bits & ~MODULAR_SCREEN_CTS) != 0 ) {
      if ( isTrappingActive(page) ) {
        expr = requirement_node_find(page->render_resources,
                                     REQUIREMENTS_TRAP_GROUP) ;
        HQASSERT(expr != NULL, "No trap group requirements") ;
        if ( !requirement_node_maxmin(expr, TASK_RESOURCE_BAND_1,
                                      count_bits_set_in_byte[reserved_bands & ~MODULAR_SCREEN_CTS]) )
          return FALSE ;
      }
    }
  }

  return TRUE ;
}

/**
 * Initialise all the the DL static variables
 */
void init_C_globals_display(void)
{
  DL_STATE dl_page_init = {
    /* Data specific to this DL: */
    0,            /* eraseno */
    DL_ERASE_BEGIN, /* erase type */
    FALSE,        /* forcepositive */
    0 ,           /* reserved_bands */
    0 ,           /* surfaces used */
    SW_TL_REF_INVALID, /* Timeline */
    NULL,         /* HDL List */
    NULL,         /* topimage */
    NULL,         /* image_lut_list */
    {NULL},       /* image decodes array cache */
    {NULL, NULL}, /* cv_fdecodes arrays */
    {NULL, NULL}, /* cv_ndecodes arrays */
    0,            /* Size of the largest expansion buffer. */
    FALSE,        /* im_force_interleave */
    TRUE,         /* im_purge_allowed */
    SPOT_NO_INVALID, /* default spot no */
    NULL,         /* DL color context */
    {0},          /* dlc_erase */
    {0},          /* dlc_knockout */
    NULL,         /* Next DL task */
    NULL,         /* DL all tasks group */
    {FALSE, NULL, NULL}, /* Internal Retained Raster */
    {NULL, NULL, NULL, NULL}, /* dlpools, set on start-up */
    NULL,         /* dl_ap */
    {             /* 'stores' sub-structure */
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
    },
    FALSE,         /* trapping_active */
    NULL,          /* resource request for rendering */
    NULL,          /* colorState */

    /* Data common to all DLs for this page: */
    NULL,         /* omitdata */
    -1,           /* highest sheet number */
    NULL,         /* spotno list */
    {MAXINT32, MAXINT32, MININT32, MININT32}, /* page_bb */
    FALSE,        /* rippedtodisk */
    FALSE,        /* rippedsomethingsignificant */
    0,            /* page sequence within job. */

    /* Data common to all DLs for this pagedevice: */
    0,            /* sizedisplaylist */
    1,            /* sizedisplayfact */
    0,            /* sizefactdisplaylist */
    0,            /* sizefactdisplayband */
    0, 0,         /* page w, h */
    100.0, 100.0, /* xdpi, ydpi */
    0, 0,         /* band_l, band_l1 */
    0,            /* band_lines*/
    0,            /* scratch_band_size */
    0,            /* rle flags */
    SC_RULE_HARLEQUIN, /* ScanConversion */
    NULL,         /* raster handle */
    NULL,         /* surfaces */
    NULL,         /* surface instance handle */
    NULL,         /* surface page handle */
    SPACE_notset, /* virtualDeviceSpace */
    COLORANTINDEX_NONE, /* virtualBlackIndex */
    COLORANTINDEX_NONE, /* deviceBlackIndex */
    FALSE,        /* rollover fills */
    FALSE,        /* framebuffer output */
    1,            /* job_number */
    0,            /* trap_effort */
    { 0 },        /* color params */
    { 0x80000001, /* CompressImageParms - 1 and 32bit images by default */
      TRUE,       /* CompressImageSource */
      TRUE },     /* LowMemImagePurgeToDisk */

    /* Data common to all DLs for this job: */
    NULL,         /* job */
    FALSE,        /* pcl5eModeEnabled */
    FALSE,        /* opaqueOnly */

    /* Data that should be tracked in the front-end: */
    NULL,         /* Current HDL. */
    NULL,         /* Target HDL. */
    NULL,         /* Current Group. */
    { DEFAULT_PAGE_GROUP_KNOCKOUT,      /* Page group knockout */
      OBJECT_NOTVM_NULL, {OBJECT_NOTVM_NOTHING} /* Page group colorspace, csa */
    },            /* page_group_details */
    NULL,         /* current DL state */
    0,            /* Group nesting depth */
    0, 0,         /* max_region_width, max_region_height */
    FALSE,        /* force_deactivate */
    TRUE,         /* backdropAutoSeparations */
    TRUE,         /* device ROPs allowed */

    /* Data that should be tracked in the back-end: */
    0, 0,         /* region_width, region_height */
    0,            /* band_lc */
    NULL,         /* Backdrop shared state */
    NULL,         /* Region map. */
    0,            /* imagesleft semaphore */
    NULL,         /* Shared (largest) expansion buffer */
    NULL,         /* im_shared */
    0,            /* watermark_seed */
    NULL,         /* PGB device */

    /* Data that shouldn't be in any DL: */
    FALSE,        /* output object map */
    { { NULL, {0, 0, 0, 0}, 0, }, NULL, 0, 0}, /* pclIdiomQueue */
  };
  int32 i ;

  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i ) {
    dl_pages[i] = dl_page_init ;
  }
  flushpage = outputpage = inputpage = &dl_pages[0] ;

  dl_currentexflags = 0 ;
  dl_currentdisposition = 0 ;

  bbox_store(&cclip_bbox, 0, 0, 0, 0);
  dl_safe_recursion = 0;
  dl_pipeline_depth = 1 ;
  dl_last_flush = 0 ;

#if defined( DEBUG_BUILD )
  countlinks = 0 ;
  countdlobj = 0 ;

  bbox_store(&debug_focus, MINDCOORD, MINDCOORD, MAXDCOORD, MAXDCOORD) ;
#endif
#if DL_DEBUG
  debug_dl = 0;
#endif
  dlts.nfree = 0 ;
  dlts.dlobj = NULL ;
#ifdef METRICS_BUILD
  dl_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&dl_metrics_hook) ;
#endif
}

/*
Log stripped */
