/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbdl.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * For the reorder and restitch routines for recombine DL mangling.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"

#include "display.h"
#include "dl_color.h"
#include "dlstate.h"
#include "stacks.h"
#include "gstack.h"
#include "graphics.h"
#include "gu_chan.h"
#include "hdl.h"

#include "recomb.h"
#include "rcbcntrl.h"
#include "rcbdl.h"

#if defined( ASSERT_BUILD )
static LISTOBJECT *reorder_last_lobj_seen = NULL;
static LISTOBJECT *restitch_last_lobj_seen = NULL;
#endif

DLREF **rcb_dl_head_addr(DL_STATE *page, int32 bandi)
{
  HDL *hdl;
  DLREF **bands;

  HQASSERT(page->currentGroup != NULL &&
           groupGetUsage(groupBase(page->currentGroup)) == GroupPage,
           "Expected open page group");
  hdl = groupHdl(groupBase(page->currentGroup));

  bands = hdlBands(hdl);
  HQASSERT(bands != NULL, "No bands for recombine");
  if ( bands[bandi] == NULL ) {
    /* Use z-order band instead. */
    bandi = page->sizefactdisplaylist;
    HQASSERT(bands[bandi] != NULL, "No dl for this band");
  }
  return &bands[bandi];
}

DLREF **rcb_dl_tails(DL_STATE *page, Bool snapshot)
{
  HDL *hdl;
  DLREF **bands;

  HQASSERT(page->currentGroup != NULL &&
           groupGetUsage(groupBase(page->currentGroup)) == GroupPage,
           "Expected open page group");
  hdl = groupHdl(groupBase(page->currentGroup));

  if ( snapshot )
    bands = hdlBandTailSnapshot(hdl);
  else
    bands = hdlBandTails(hdl);
  HQASSERT(bands != NULL, "No band tails for recombine");
  return bands;                                      
}

void rcb_set_insertion(DL_STATE *page, int32 bandi, Bool snapshot, DLREF *dl)
{
  DLREF **bands = rcb_dl_tails(page, snapshot);

  bands[bandi] = dl;
}

/**
 * Shift DL objects about as the result of a recombine
 */
static Bool rcb_dl_shift(DL_STATE *page, DLREF *before, DLREF *insert,
                         dl_color_t *p_dlc1, dl_color_t *p_dlc2, Bool *pfcheck)
{
  DLREF *dlref = dlref_next(before);
  DLREF *end = insert;
  dl_color_t dlc_acc;
  Bool ffirstlobj = TRUE;

  /* Take a copy of the first color, may accummulate more colorants */
  if ( !dlc_copy(page->dlc_context, &dlc_acc, p_dlc1) )
    return FALSE;

  /* Now move dlref to immediately after insert. Any
     intermediate DL objects with colorants in common with those objects
     moved already (ie, the colorants in dlc_acc) will also be moved */
  while ( dlref != end ) {
    LISTOBJECT *p_list_object = dlref_lobj(dlref);
    dl_color_t dlc_curr_color;
    Bool fmove;

    if ( ffirstlobj ) {
      /* Always move the first one */
      dlc_clear(&dlc_curr_color);
      fmove = TRUE;
    } else {
      /* Move subsequent DL objects with colorants in common with those
         moved so far */
      if ( !dlc_from_dl(page->dlc_context, &p_list_object->p_ncolor, &dlc_curr_color)) {
        dlc_release(page->dlc_context, &dlc_acc);
        return FALSE;
      }
      fmove = ( (p_list_object->spflags & RENDER_RECOMBINE) != 0 &&
                !dlc_is_none(&dlc_curr_color) &&
                dlc_common_colorants(&dlc_curr_color, &dlc_acc) );
    }

    if ( fmove ) {
      /* Move the first one, or subsequent DL objects with colorants in
       * common with those moved so far
       */
      if ( pfcheck != NULL ) {
        /* Check mode: DL is not changed. Checks would not change Z-order */
        if ( !ffirstlobj && dlc_common_colorants(&dlc_curr_color, p_dlc2) ) {
          *pfcheck = FALSE;
          dlc_release(page->dlc_context, &dlc_curr_color);
          dlc_release(page->dlc_context, &dlc_acc);
          return TRUE;
        }
        /* Checking, so never move DL objects; shuffle on to the next */
        before = dlref;
        dlref = dlref_next(dlref);
      } else {
        /* Update mode: Assert the Z-order does not change and update DL */
        HQASSERT(ffirstlobj || !dlc_common_colorants(&dlc_curr_color, p_dlc2),
                 "Reordering/restitching DL objects but colors in the way "
                 "(Check variant of routine must not have been called)");

        /* Unhook the object from the list */
        dlref_setnext(before, dlref_next(dlref));

        /* Add the object in after the insert link */
        dlref_setnext(dlref, dlref_next(insert));
        dlref_setnext(insert, dlref);

        /* Move the pointers on */
        insert = dlref_next(insert);
        dlref = dlref_next(before);
      }

      /* Include any additional colorants arising from moving this object */
      if ( !ffirstlobj ) {
        if ( dlc_extra_colorants(&dlc_acc, &dlc_curr_color) &&
             !dlc_merge_extra(page->dlc_context, &dlc_acc, &dlc_curr_color) ) {
          dlc_release(page->dlc_context, &dlc_curr_color);
          dlc_release(page->dlc_context, &dlc_acc);
          return FALSE;
        }
      } else {
        /* dlc_acc is initialised to the first lobj color,
           so no need to look for additional colorants */
        ffirstlobj = FALSE;
      }
    } else { /* !fmove */
      /* No need to move the object, move on to the next object */
      before = dlref;
      dlref = dlref_next(dlref);
    }
    dlc_release(page->dlc_context, &dlc_curr_color);
  }
  dlc_release(page->dlc_context, &dlc_acc);
  return TRUE;
}

/**
 * Re-order DL objects as the result of a recombine
 */
static Bool rcb_reorder_objects(DL_STATE *page, LISTOBJECT *old_lobj,
                                int32 band, Bool *pffound, Bool *pfcheck)
{
  DLREF *dl = rcb_dl_tails(page, TRUE)[band];
  DLREF **tails = rcb_dl_tails(page, FALSE);
  DLREF *end_dlptr;
  dl_color_t dlc_start, dlc_end;

  HQTRACE((debug_recombine && (reorder_last_lobj_seen != old_lobj)),
          ("Reordering lobj 0x%X in the DL", old_lobj));
#if defined( ASSERT_BUILD )
  reorder_last_lobj_seen = old_lobj;
#endif

  if ( pfcheck != NULL )
    *pfcheck = TRUE; /* okay until proven otherwise */

  /* First find the object starting from the front. */

  HQASSERT(band >= 0 && band <= page->sizefactdisplaylist,
           "band index out of range");

  /* The object can NEVER be the start of the list since the erase is never
   * found to be missing. Ditto the end.
   */
  HQASSERT(dl && dlref_lobj(dl) != old_lobj, "Band dl corrupt : obj at start");
  while ( dlref_next(dl) != NULL && dlref_lobj(dlref_next(dl)) != old_lobj )
    dl = dlref_next(dl);

  /* The object may not be here if it was a vignette or a fuzzy match */
  if ( dlref_next(dl) == NULL ) {
    HQASSERT(dlref_lobj(dl) != old_lobj, "Band dl corrupt : obj at end");
    if ( pffound )
      *pffound = FALSE;
    return TRUE;
  }
  if ( pffound )
    *pffound = TRUE;

  end_dlptr = tails[band];
  HQASSERT(end_dlptr != NULL, "insertion point null");

  dlc_from_dl_weak(old_lobj->p_ncolor, &dlc_start);
  dlc_from_dl_weak(dlref_lobj(end_dlptr)->p_ncolor, &dlc_end);
  HQASSERT(!dlc_set_indexed_colorant(&dlc_start, rcbn_current_colorant()),
           "current sep is in dlc_start");

  if ( pfcheck == NULL ) /* Update mode: Move the insertion point */
    tails[band] = dlref_next(dl);

  /* Now loop over looking at objects and if they share in dlc_acc, move
   * them and update the pointers until we hit end
   */
  return rcb_dl_shift(page, dl, end_dlptr, &dlc_start, &dlc_end, pfcheck);
}

Bool rcb_reorder_dl_objects(DL_STATE *page, LISTOBJECT *old_lobj,
                            int32 band, Bool *found)
{
  HQASSERT(old_lobj != NULL, "old_lobj null");
  HQASSERT(found != NULL, "found null");
  return rcb_reorder_objects(page, old_lobj, band, found, NULL);
}

Bool rcb_reorder_dl_objects_check(DL_STATE *page, LISTOBJECT *old_lobj,
                                  int32 band, Bool *fcheck)
{
  HQASSERT(old_lobj != NULL, "old_lobj null");
  HQASSERT(fcheck != NULL, "fcheck null");
  return rcb_reorder_objects(page, old_lobj, band, NULL, fcheck);
}

/**
 * Search for the specified lobj in the given bands DL
 * return the predecessor link, to allow easy manipulation.
 *
 * Note DL must always start with an erase, which cannot be what we are
 * searching for, so a predecessor always exists
 *
 * Failure to find the object is indicated by the predecessors next
 * field being null.
 */
DLREF *rcb_find_lobj_pre(DL_STATE *page, int32 bandi, LISTOBJECT *lobj)
{
  DLREF *dl = rcb_dl_head(page, bandi);

  HQASSERT(dl && dlref_lobj(dl) && dlref_lobj(dl)->opcode == RENDER_erase &&
           dlref_lobj(dl) != lobj, "Invalid dl");

  while ( dlref_next(dl) && dlref_lobj(dlref_next(dl)) != lobj )
    dl = dlref_next(dl);
  return dl;
}

static Bool rcb_restitch_objects(DL_STATE *page,
                                 LISTOBJECT *p_lobj1, LISTOBJECT *p_lobj2,
                                 dl_color_t *p_dlc1, dl_color_t *p_dlc2,
                                 dcoord y1, dcoord y2, Bool *pfcheck)
{
  int32 band, b1, b2, last_band;

  HQTRACE((debug_recombine && (restitch_last_lobj_seen != p_lobj1)),
          ("Restitching lobjs 0x%X and 0x%X in the DL", p_lobj1, p_lobj2));
#if defined( ASSERT_BUILD )
  restitch_last_lobj_seen = p_lobj1;
#endif

  /* The knockout (knock_lobj) is trailing behind the vgimg (vgimg_lobj),
     and needs to be moved. restitch the DL so that the vgimg (and any
     trailing common color objects) end up after the knockout */

  last_band = page->sizefactdisplaylist;
  RCB_BBOX_TO_BANDS(page, y1, y2, b1, b2, "rcb restitch");

  for ( band = b1; ( band >= b1  && band <= b2 ) || ( band == last_band );
        band == b2 ? band = last_band : ++band ) {
    DLREF *dl = rcb_find_lobj_pre(page, band, p_lobj1);
    /* Find the link that points to the vignette/image listobject */

    if ( dlref_next(dl) != NULL ) { /* If the link was found, look for the ko */
      DLREF *ko = dlref_next(dlref_next(dl));

      /* Find the link that points to the knockout listobject */
      while ( ko != NULL && dlref_lobj(ko) != p_lobj2 )
        ko = dlref_next(ko);

      if ( ko != NULL ) {
        /* Found both the vignette/image link and the knockout link.
           Begin moving intervening objects to after the knockout if
           they have colorants in common with the vignette/image or
           objects moved already. */
        if ( !rcb_dl_shift(page, dl, ko, p_dlc1, p_dlc2, pfcheck) )
          return FALSE;
      }
    }
  }
  return TRUE;
}

static Bool rcb_restitch_setup_colors(DL_STATE *page,
                                      LISTOBJECT *p_lobj_iv,
                                      LISTOBJECT *p_lobj_ko,
                                      dl_color_t *p_dlc_iv,
                                      dl_color_t *p_dlc_ko)
{
  int32 nci;
  COLORANTINDEX cis[4];

  HQASSERT(dl_upto_4_common_colorants(p_lobj_ko->p_ncolor,
                                      p_lobj_iv->p_ncolor,
                                      cis, &nci),
           "Invalid number of common colorants");

  if ( !dlc_from_dl(page->dlc_context, &p_lobj_iv->p_ncolor, p_dlc_iv) )
    return FALSE;

  if ( !dlc_from_dl(page->dlc_context, &p_lobj_ko->p_ncolor, p_dlc_ko) ) {
    dlc_release(page->dlc_context, p_dlc_iv);
    return FALSE;
  }

  /* For spot to spot/process vignettes/images may have either 1 (spot) or N
   * (process) colorants in common, where N must equal the number of process
   * separations we've seen. That's because the first spot or process color
   * is painted as per normal (which knocks out in all the other separations)
   * and the second spot or process color is overprinted.
   */
  (void)dl_upto_4_common_colorants(p_lobj_ko->p_ncolor,
                                   p_lobj_iv->p_ncolor, cis, &nci);
  if ( nci > 0 ) {
    HQASSERT(rcb_valid_common_colorants(cis, nci), "Invalid common colorants");

    while ((--nci) >= 0) { /* Remove common colorants from the knockout */
      if ( !dlc_remove_colorant(page->dlc_context, p_dlc_ko, cis[nci]) ) {
        dlc_release(page->dlc_context, p_dlc_iv);
        dlc_release(page->dlc_context, p_dlc_ko);
        return FALSE;
      }
    }
  }

  return TRUE;
}

/**
 * Moves the image/vignette and knockout so they are adjacent, also moving
 * any objects inbetween that have common colors with the first object
 */
Bool rcb_restitch_update(DL_STATE *page,
                         LISTOBJECT *p_lobj_iv, LISTOBJECT *p_lobj_ko,
                         dcoord y1, dcoord y2)
{
  dl_color_t dlc_iv, dlc_ko;
  Bool fokay;

  if ( !rcb_restitch_setup_colors(page, p_lobj_iv, p_lobj_ko,
                                  &dlc_iv, &dlc_ko) )
    return FALSE;

  /* First move the knockout (and any inbetween DL objects with colorants
     in common with the knockout) to immediately after the vignette or
     image. Then move the vignette/image (and any inbetween DL objects
     with colorants in common with the vignette/image) to after the
     knockout. This ensures the vignette/image ends up after the knockout
     regardless of the order of the two DL objects at the start */
  fokay = rcb_restitch_objects(page, p_lobj_ko, p_lobj_iv, &dlc_ko, &dlc_iv,
                               y1, y2, NULL /* Update (not check) */) &&
          /* As the above resitch call but with the DL objects swapped around */
          rcb_restitch_objects(page, p_lobj_iv, p_lobj_ko, &dlc_iv, &dlc_ko,
                               y1, y2, NULL /* Update (not check) */);
  dlc_release(page->dlc_context, &dlc_iv);
  dlc_release(page->dlc_context, &dlc_ko);

  return fokay;
}

/**
 * Tests if moving the image/vignette and knockout so they are adjacent
 * can be done without changing the z-order
 */
Bool rcb_restitch_check(DL_STATE *page,
                        LISTOBJECT *p_lobj_iv, LISTOBJECT *p_lobj_ko,
                        dcoord y1, dcoord y2, Bool *fcheck)
{
  dl_color_t dlc_iv, dlc_ko;
  Bool fokay;

  *fcheck = TRUE;

#if defined( DEBUG_BUILD )
  {
    static int32 fSkipRestitchCheck = FALSE;
    if ( fSkipRestitchCheck )
      return TRUE;
  }
#endif

  if ( !rcb_restitch_setup_colors(page, p_lobj_iv, p_lobj_ko,
                                  &dlc_iv, &dlc_ko) )
    return FALSE;

  fokay = rcb_restitch_objects(page, p_lobj_ko, p_lobj_iv, &dlc_ko, &dlc_iv,
                               y1, y2, fcheck);
  if ( fokay && *fcheck )
    /* As the above restitch call but with the DL objects swapped around */
    fokay = rcb_restitch_objects(page, p_lobj_iv, p_lobj_ko, &dlc_iv, &dlc_ko,
                                 y1, y2, fcheck);
  dlc_release(page->dlc_context, &dlc_iv);
  dlc_release(page->dlc_context, &dlc_ko);

  return fokay;
}

/**
 * DL manipulation routines, used by vignette and shfill recombination
 */
void rcb_remove_from_some_bands(DL_STATE *page, LISTOBJECT *lobj,
                                int32 b1, int32 b2)
{
  int32 band;

  HQASSERT(lobj != NULL, "lobj null");

  HQASSERT(b1 >= 0 && b1 <= page->sizefactdisplaylist, "Invalid band");
  HQASSERT(b2 >= 0 && b2 <= page->sizefactdisplaylist, "Invalid band");
  HQASSERT((b1 == page->sizefactdisplaylist) ==
           (b2 == page->sizefactdisplaylist),
           "Either both bands should be last band or neither should be");

  for ( band = b1; band <= b2; band++ ) { /* Remove the object from each band */
    DLREF *dl;
    DLREF **tails = rcb_dl_tails(page, FALSE);

    dl = rcb_find_lobj_pre(page, band, lobj);
    HQASSERT(dlref_next(dl) != NULL, "Lost vignette in band");
    /* Unhook the vignette @ dl->next from this band */
    if ( dlref_next(dl) == tails[band] )
      tails[band] = dl;
    HQASSERT(dlref_next(dl) != rcb_dl_tails(page, TRUE)[band],
        "removing snapshot");
    dlref_setnext(dl, dlref_next(dlref_next(dl)));
  }
}

/**
 * This routine reassigns the bands in which LISTOBJECTS appear, since the
 * contents of the LISTOBJECTs will be swapped. The purpose is to make sure
 * that old_lobj has entries in all of its appropriate bands, and in none that
 * it does not appear in. If necessary, it can steal DLREF from
 * new_lobj, which if it does appear in the DL, will soon be removed.
 */
void rcb_reassign_swapped_bands(DL_STATE *page, LISTOBJECT *old_lobj,
                                LISTOBJECT *new_lobj)
{
  int32 ob1, ob2, nb1, nb2, band;

  HQASSERT(old_lobj != NULL, "Parameter old_lobj null");
  HQASSERT(new_lobj != NULL, "Parameter new_lobj null");

  HQASSERT(old_lobj != new_lobj, "Can't reassign same object!");

  /* Find all bands covered by these objects. They should intersect, or the
     routine is being used for something other than it was planned for. */
  RCB_BBOX_TO_BANDS(page, old_lobj->bbox.y1, old_lobj->bbox.y2, ob1, ob2, "rcbb");
  RCB_BBOX_TO_BANDS(page, new_lobj->bbox.y1, new_lobj->bbox.y2, nb1, nb2, "rcbb");

  if ( ob1 < nb1 ) /* old_lobj may still appear on these bands; remove it */
    rcb_remove_from_some_bands(page, old_lobj, ob1, nb1 - 1);
  if ( ob1 > nb1 ) { /* steal links from new_lobj for old_lobj */
    for ( band = nb1; band < ob1; ++band ) {
      DLREF *link = dlref_next(rcb_find_lobj_pre(page, band, new_lobj));
      HQASSERT(link, "Cannot find links for swapped object's new bands");
      dlref_assign(link, old_lobj);
    }
  }

  if ( ob2 > nb2 ) /* old_lobj may still appear on these bands; remove it */
    rcb_remove_from_some_bands(page, old_lobj, nb2 + 1, ob2);
  if ( ob2 < nb2 ) { /* steal links from new_lobj for old_lobj */
    for ( band = ob2 + 1; band <= nb2; ++band ) {
      DLREF *link = dlref_next(rcb_find_lobj_pre(page, band, new_lobj));
      HQASSERT(link, "Cannot find links for swapped object's new bands");
      dlref_assign(link, old_lobj);
    }
  }
}

#if defined( ASSERT_BUILD )
Bool rcb_assert_not_on_dl(DL_STATE *page, LISTOBJECT *lobj, dbbox_t *bbox)
{
  int32 b1, b2, band, last_band;

  HQASSERT(lobj != NULL, "Parameter lobj null");
  HQASSERT(bbox != NULL, "BBox null");

  RCB_BBOX_TO_BANDS(page, bbox->y1, bbox->y2, b1, b2, "rcb not on dl");

  last_band = page->sizefactdisplaylist;

  /*  Search for the objects in each band, and swap */
  for ( band = b1; band <= b2 || band == last_band;
        band == b2 ? band = last_band : ++band ) {
    DLREF *link = dlref_next(rcb_find_lobj_pre(page, band, lobj));
    if ( link )
      return FALSE;
  }
  /* Not found, assert will succeed */
  return TRUE;
}
#endif

void init_C_globals_rcbdl(void)
{
#if defined( ASSERT_BUILD )
  reorder_last_lobj_seen = NULL;
  restitch_last_lobj_seen = NULL;
#endif
}

/* Log stripped */
