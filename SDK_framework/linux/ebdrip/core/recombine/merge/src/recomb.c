/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:recomb.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine functions
 */

#include "core.h"
#include "recomb.h"

#include "swerrors.h"

#include "objects.h"
#include "display.h"
#include "stacks.h"
#include "dlstate.h"
#include "graphics.h"
#include "pathops.h"
#include "panalyze.h"
#include "vnobj.h"
#include "gstack.h"
#include "gu_chan.h"
#include "halftone.h"
#include "dl_store.h"
#include "control.h"
#include "dl_bres.h"
#include "rcbshfil.h"
#include "often.h"
#include "routedev.h"
#include "dl_image.h"
#include "imageo.h"
#include "dl_bbox.h"
#include "hdl.h"
#include "shadex.h"
#include "imexpand.h" /* im_expandmerge */
#include "imstore.h"

#include "rcbadjst.h"
#include "rcbcntrl.h"
#include "rcbcomp.h"
#include "rcbsplit.h"
#include "rcbvigko.h"
#include "rcbvmerg.h"
#include "rcbdl.h"

#if defined( ASSERT_BUILD )
Bool debug_recombine = FALSE;
Bool debug_vignette  = FALSE;
#endif

/**
 * For spot to spot/process vignettes/images may have either 1 (spot) or N
 * (process) colorants in common, where N must equal the number of process
 * separations we've seen. That's because the first spot or process color
 * is painted as per normal (which knocks out in all the other separations)
 * and the second spot or process color is overprinted.
 */
Bool rcb_valid_common_colorants(COLORANTINDEX *cis, int32 nci)
{
  if ( nci > 0 ) {
    if ( nci == 1 ) {
      COLORANTINDEX ci;
      RCBSEPARATION *rcbsep;
      ci = cis[0];
      for ( rcbsep = rcbn_iterate(NULL); rcbsep != NULL;
            rcbsep = rcbn_iterate(rcbsep) ) {
        if ( ci == rcbn_sepciPseudo(rcbsep) ) {
          if ( rcbn_sepisprocess(rcbsep) ) {
            if ( nci != rcbn_cprocessseps() )
              return FALSE;
          }
          break;
        }
      }
    }
    else if ( nci == rcbn_cprocessseps() ) {
      while ((--nci) >= 0 ) {
        COLORANTINDEX ci;
        RCBSEPARATION *rcbsep;
        ci = cis[nci];
        for ( rcbsep = rcbn_iterate( NULL ); rcbsep != NULL ;
              rcbsep = rcbn_iterate( rcbsep )) {
          if ( ci == rcbn_sepciPseudo( rcbsep )) {
            if ( ! rcbn_sepisprocess( rcbsep ))
              return FALSE;
            break;
          }
        }
      }
    }
    else
      return FALSE;
  }
  return TRUE;
}

static Bool rcb_merge_spot(DL_STATE *page,
                           LISTOBJECT *lobj_dst, LISTOBJECT *lobj_src,
                           COLORANTINDEX ci)
{
  int32          ispot;
  STATEOBJECT    *nstate = NULL;

  HQASSERT((lobj_dst != NULL) && (lobj_src != NULL), "NULL lobj");

  /** \todo: Do not merge spot when going to device gray and looking at
     black colorant. This optimisation needs to be checked as safe. */
#if 0
  if ( (int32)thegsDeviceColorSpace( gstate ) == SPACE_DeviceGray ) {
    ispot = 0;
    if ( color == BLACK_SEPARATION ) {
      spotno = lobj_src->objectstate->spotno;
    }
  }
  else
#endif
    ispot = ht_mergespotnoentry(lobj_dst->objectstate->spotno,
                                lobj_src->objectstate->spotno,
                                DISPOSITION_REPRO_TYPE(lobj_src->disposition),
                                ci, page->eraseno);

  /* Update dl object with new object state */
  if ( ispot > 0 ) {
    STATEOBJECT state;

    state        = *(lobj_dst->objectstate);
    state.spotno = ispot;

    nstate = (STATEOBJECT*)dlSSInsert(page->stores.state,
                                      &state.storeEntry, TRUE);
    if (nstate != NULL) {
      lobj_dst->objectstate = nstate;
    }
  }

  return ((ispot == 0) || (nstate != NULL));
}

Bool rcb_merge_spots(DL_STATE *page, LISTOBJECT* lobj_dst, LISTOBJECT* lobj_src)
{
  COLORANTINDEX     ci;

  HQASSERT( lobj_dst != NULL, "lobj_dst is null" );
  HQASSERT( lobj_src != NULL, "lobj_src is null" );

  /* Spot numbers are the same no need to merge them */
  if ( lobj_src->objectstate->spotno == lobj_dst->objectstate->spotno )
    return TRUE;

  ci = rcbn_presep_screen(NULL);
  return rcb_merge_spot(page, lobj_dst, lobj_src, ci);
}

/**
 * Calculates the rollover for each dl object. Rebuilds the dl objects
 * to follow the opaque ordering defined in the Z-band. This is only
 * required when the number of separations is greater than one.
 */
static Bool rcb_rebuild_dl_and_rollovers(DL_STATE *page)
{
  int32 band, last_band = page->sizefactdisplaylist;
  DLREF *dlobj;

  /* Reset the pointers in the DL back to the first records, and
   * don't have any objects following the erase.
   */
  for ( band = 0; band < last_band ; ++band ) {
    rcb_set_insertion(page, band, FALSE, rcb_dl_head(page, band));
  }

  /* Start after the erase object in the Z band and loop over them */

  for ( dlobj = dlref_next(rcb_dl_head(page, last_band)); dlobj;
        dlobj = dlref_next(dlobj) ) {
    LISTOBJECT *lobj = dlref_lobj(dlobj);
    DLREF *dl1, *dl2;
    CLIPOBJECT *clipstate;
    int32 y1, y2, b1, b2;
    Bool found;

    HQASSERT(lobj, "lobj NULL");
    HQASSERT(lobj->objectstate, "lobj->objectstate NULL");

    clipstate = lobj->objectstate->clipstate;

    HQASSERT(clipstate, "clipstate NULL");

    y1 = lobj->bbox.y1;
    y2 = lobj->bbox.y2;

    if ( theY1Clip(*clipstate) > y1 )
      y1 = theY1Clip(*clipstate);
    if ( theY2Clip(*clipstate) < y2 )
      y2 = theY2Clip(*clipstate);

    if (y1 > y2) /* Object outside page range */
      continue;

    RCB_BBOX_TO_BANDS(page, y1, y2, b1, b2, "rcb rebuild");

    /* Start looking at the objects starting from the endobject */

    found = FALSE;

    for ( band = b1; band <= b2 ; ++band ) {
      dl1 = rcb_dl_tails(page, FALSE)[band];
      dl2 = dlref_next(dl1);

      while ( dl2 && dlref_lobj(dl2) != lobj ) {
        dl1 = dl2;
        dl2 = dlref_next(dl2);
      }

      /* dl2 is non-null if this object has been found. */

      if ( dl2 ) {
        if ( dl1 != rcb_dl_tails(page, FALSE)[band] ) {
          /* Its somewhere in the list, pull out and place after end. */

          dlref_setnext(dl1, dlref_next(dl2));
          dlref_setnext(dl2, dlref_next(rcb_dl_tails(page, FALSE)[band]));
          dlref_setnext(rcb_dl_tails(page, FALSE)[band], dl2);
        }
        rcb_set_insertion(page, band, FALSE, dl2);
        found = TRUE;
      }
      else {
        /* If the object was found in a previous band, and not in this
         * one, then it can't be found in the rest of the DL.
         */
        if ( found )
          break;
      }

      SwOftenUnsafe();
    }
  }
  return TRUE;
}

/**
 * Updates the dl insertion points prior to adding a pseudo erase.
 * DL objects may otherwise appear after the imposition pointers
 * instead of before
 */
Bool rcb_resync_dlptrs(DL_STATE *page)
{
  int32 iband, lastband;
  DLREF *lbstartlink, *lbendlink;

  HQASSERT(rcbn_enabled(), "show be recombining");

  /* Dl objects may need be out of order, so reorder them from the last band */
  if ( !rcb_rebuild_dl_and_rollovers(page) )
    return FALSE;

  /* Now update the insertion point for each band,
   * this is important as dl objects before the insertion point
   * will be hidden by the pseudo erase */
  lastband  = page->sizefactdisplaylist;
  lbstartlink = rcb_dl_head(page, lastband);
  lbendlink = rcb_dl_tails(page, FALSE)[lastband];

  /* We need to consider all objects up to and including the one
   * refered to by the initial lbendlink */
  if ( lbendlink != NULL )
    lbendlink = dlref_next(lbendlink);
  else
    HQFAIL("lbendlink null");

  for ( iband = 0; iband < lastband ; ++ iband ) {
    DLREF *currlink, *lbnextlink, *newendlink;

    lbnextlink = lbstartlink;
    currlink = newendlink = rcb_dl_head(page, iband);

    /* Advance insertion point if subsequent object exists before or on
     * the insertion point of the last band
     */
    while ( lbnextlink != lbendlink && currlink != NULL ) {
      if ( dlref_lobj(lbnextlink) == dlref_lobj(currlink) ) {
        /* advance insertion point on this currlink */
        newendlink = currlink;
        currlink = dlref_next(currlink);
      }
      lbnextlink = dlref_next(lbnextlink);
    }
    /* Update the insertion pointer for this band */
    HQASSERT( newendlink != NULL, "newendlink null" );
    rcb_set_insertion(page, iband, FALSE, newendlink);
  }
  return TRUE;
}

#define RCB_INSERTPOINT_CHECK  0
#define RCB_INSERTPOINT_UPDATE 1

/**
 * Move the EndDisplayList pointers on to the merged node
 */
static Bool rcb_update_insertion_point(DL_STATE *page, LISTOBJECT *old_lobj,
                                       int32 b1, int32 b2, int32 bm,
                                       int32 last_band, int32 fuzzy,
                                       int32 action, Bool *fcheck)
{
  int32 band;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM(int32, fuzzy);
#endif

  HQASSERT(fcheck != NULL, "fcheck null");
  HQASSERT(old_lobj != NULL, "old_lobj null");

  *fcheck = TRUE;

  for ( band = b1; ( band >= b1 && band <= b2 ) || ( band == last_band );
      ( band == b2 ) ? band = last_band : band++ ) {
    DLREF *link_dlptr = rcb_dl_tails(page, FALSE)[band];

    HQASSERT(link_dlptr != NULL, "link_dlptr null");

    while ( link_dlptr != NULL && dlref_lobj(link_dlptr) != old_lobj )
      link_dlptr = dlref_next(link_dlptr);

    /* If link_dlptr is set then we found after the current insertion point */
    if ( link_dlptr != NULL ) {
      if ( action == RCB_INSERTPOINT_UPDATE )
        rcb_set_insertion(page, band, FALSE, link_dlptr);
    }
    else {
      if ( action == RCB_INSERTPOINT_CHECK ) {
        int32 fcanreorder;

        if ( !rcb_reorder_dl_objects_check(page, old_lobj, band, &fcanreorder) )
          return FALSE;
        if ( !fcanreorder ) {
          *fcheck = FALSE;
          return TRUE;
        }
      }
      else {
        int32 found;

        HQASSERT(action == RCB_INSERTPOINT_UPDATE, "wrong state");

        /* Object is before current insertion point, need to reorder dl list */
        if ( !rcb_reorder_dl_objects(page, old_lobj, band, &found) )
          return FALSE;

        HQASSERT(found || fuzzy, "failed to find dl object");

        /* If we've reordered the DL and moved more than 1 object, then
           we can't carry on with the merge (if we're still to find a
           MERGE_FUZZY). In this case, we need to search all through the
           DL again. So, force us to start again (if we've still a fuzzy
           match left) */
        if ( found && band == bm )
          *fcheck = FALSE;
      }
    }
  }
  return TRUE;
}

/**
 * Merge the spot and color components from the new into the old.
 */
static Bool rcb_merge_exact(DL_STATE *page,
                            LISTOBJECT *p_lobj_old, LISTOBJECT *p_lobj_new,
                            p_ncolor_t *pp_ncolor_new)
{
  return rcb_merge_spots(page, p_lobj_old, p_lobj_new) &&
         dl_merge(page->dlc_context, &p_lobj_old->p_ncolor, pp_ncolor_new);
}

/**
 * Update planes to include the colorants included by way of a fuzzy
 * match. These colorants will be removed from the final dl_color after
 * color adjustment (provided input and output colorspace is the same).
 * This behaviour is required to correctly display objects spread or
 * choked in a subset of colorants.
 */
static Bool rcb_merge_fuzzy(DL_STATE *page,
                            LISTOBJECT *p_lobj_old, LISTOBJECT *p_lobj_new,
                            p_ncolor_t *pp_ncolor_new)
{
  if ( p_lobj_old->attr.planes == NULL ) {
    if ( !dl_copy(page->dlc_context, &p_lobj_old->attr.planes, pp_ncolor_new) )
      return FALSE;
  }
  else {
    if ( !dl_merge_extra(page->dlc_context, &p_lobj_old->attr.planes,
                         pp_ncolor_new) )
      return FALSE;
  }

  if ( p_lobj_new->attr.planes == NULL ) {
    if ( !dl_copy(page->dlc_context, &p_lobj_new->attr.planes,
                  &p_lobj_old->p_ncolor) )
      return FALSE;
  }
  else {
    if ( !dl_merge_extra(page->dlc_context, &p_lobj_new->attr.planes,
                         &p_lobj_old->p_ncolor) )
      return FALSE;
  }
  if ( dl_common_colorants(p_lobj_new->attr.planes, *pp_ncolor_new) ) {
    dl_color_t        dlcolor;
    dl_color_iter_t   dlci;
    dlc_iter_result_t iter_res;
    COLORANTINDEX     ci;
    COLORVALUE        cv;

    dlc_from_dl_weak(p_lobj_new->attr.planes, &dlcolor);
    HQASSERT((DLC_TINT_OTHER == dlc_check_black_white(&dlcolor)),
             "dlcolor must be DLC_TINT_OTHER");

    iter_res = dlc_first_colorant(&dlcolor, &dlci, &ci, &cv);

    if ( !dl_remove_colorant(page->dlc_context, &p_lobj_new->attr.planes, ci) )
      return FALSE;
  }

  /* Merge the spots components between the objects. */
  if ( !rcb_merge_spots(page, p_lobj_old, p_lobj_new) ||
       !rcb_merge_spots(page, p_lobj_new, p_lobj_old))
    return FALSE;

  /* Merge the colors components between the objects. */
  if ( !dl_merge(page->dlc_context, &p_lobj_old->p_ncolor, pp_ncolor_new) )
    return FALSE;
  dl_release(page->dlc_context, &p_lobj_new->p_ncolor);
  return dl_copy(page->dlc_context, &p_lobj_new->p_ncolor,
                 &p_lobj_old->p_ncolor);
}

#define CLIPRECTS_EQUIVALENT( _cl1, _cl2, _epsilon ) \
( (abs(theX1Clip(*(_cl1)) - theX1Clip(*(_cl2))) <= (_epsilon)) && \
  (abs(theX2Clip(*(_cl1)) - theX2Clip(*(_cl2))) <= (_epsilon)) && \
  (abs(theY1Clip(*(_cl1)) - theY1Clip(*(_cl2))) <= (_epsilon)) && \
  (abs(theY2Clip(*(_cl1)) - theY2Clip(*(_cl2))) <= (_epsilon)) )

/**
 * Check that the object old_lobj (old_dlobj) is a valid candidate for
 * comparing with new_lobj. If so then make the comparison.
 */
rcb_merge_t rcb_CheckCandidate(DL_STATE *page,
                               LISTOBJECT *new_lobj, LISTOBJECT *old_lobj,
                               DLREF *old_dlobj, p_ncolor_t p_ncolor_new,
                               rcb_merge_t test_type,
                               COMPARE_LOBJS compare_lobjs,
                               int32 b1, int32 b2, int32 bm,
                               CLIPOBJECT **plast_same_clip_ptr)
{
  CLIPOBJECT *old_clip;
  CLIPOBJECT *new_clip;
  Bool fImageAndImage;
  Bool fOrderOK;
  rcb_merge_t ret;

  /* Filter out composite objects. */
  if ( (new_lobj == old_lobj) ||
       (( old_lobj->spflags & RENDER_RECOMBINE ) == 0 ) )
    return MERGE_NONE;

  /* Are they the same object, so far as we can tell? Try the clipping
   * rectangle, and then the type specific object comparator.
   */
  HQASSERT(old_lobj->objectstate, "No object state for old object");
  HQASSERT(new_lobj->objectstate, "No object state for new object");

  old_clip = old_lobj->objectstate->clipstate;
  new_clip = new_lobj->objectstate->clipstate;

  HQASSERT( old_clip && new_clip, "CLIPOBJECT missing!" );

  fImageAndImage = ( old_lobj->opcode == RENDER_image &&
                     new_lobj->opcode == RENDER_image );

  /*
       Images are restricted to one trap match only, so to reduce
       the possibility of trying to trap match more than once we
       allow for a one pixel rounding error in clip rect comparison
  */
  if ( !CLIPRECTS_EQUIVALENT(old_clip,new_clip, fImageAndImage ? 1 : 0) )
    return MERGE_NONE;

  /* Ignore dl objects with a none dl color. Must have no planes in common. */
  if (  dl_is_none( old_lobj->p_ncolor ) ||
        dl_common_colorants( old_lobj->p_ncolor, p_ncolor_new ) )
    return MERGE_NONE;

  /* now compare the object with the candidate */
  ret = (*compare_lobjs)( old_lobj, new_lobj, test_type );

  if ( ret == MERGE_DONE )
    return MERGE_DONE;
  if ( ret == MERGE_ERROR )
    return MERGE_ERROR;

  /* Either didn't find match, or this match not wanted. */
  if (( ret & test_type ) == MERGE_NONE )
    return MERGE_NONE;

  /* If the old_clip(on the DL) is not the same as the last
   * matched clip, then if the clips are not the same then
   * continue since the objects can not be the same.
   */
  if ( ( *plast_same_clip_ptr != old_clip ) && ( !same_clip_objects(old_clip,
          new_clip, FALSE, fImageAndImage ? 1 : 0 ) ) )
    return MERGE_NONE;

  /* Test if merging into old_lobj would cause the object to be moved
     passed other object with common planes */
  if ( ! rcb_update_insertion_point(page, dlref_lobj(old_dlobj), b1, b2, bm,
                                    page->sizefactdisplaylist,
                                    (ret & MERGE_FUZZY),
                                    RCB_INSERTPOINT_CHECK, & fOrderOK) )
    ret = MERGE_ERROR;
  if ( ! fOrderOK )
    ret = MERGE_NONE;

  if ( (test_type & MERGE_CHECK) != 0 )
    return MERGE_CHECK;

  if ( ret == MERGE_SPLIT )
    return MERGE_SPLIT;

  *plast_same_clip_ptr = ( fImageAndImage ? NULL : old_clip );

  return ret;
}

/**
 * Adjust start pointer so point to fuzzy matched traps before
 * exact matched object.
 */
static rcb_merge_t rcb_adjustFuzzyStart(DL_STATE *page,
                                        DLREF **p_start_dlptr,
                                        DLREF *end_dlobj,
                                        int32 fuzzy_match_seps,
                                        p_ncolor_t p_ncolor_new,
                                        COMPARE_LOBJS compare_lobjs,
                                        int32 b1, int32 b2, int32 bm,
                                        CLIPOBJECT **plast_same_clip_ptr)
{
  int32 i,fCandidate,ret;
  DLREF *ahead_dlptr, *start_dlptr = *p_start_dlptr;
  LISTOBJECT *ahead_lobj, *end_lobj;

  /* now move display list pointer to neighbours before end_dlobj */
  for ( i = 0, ahead_dlptr = start_dlptr;
        ( ahead_dlptr != end_dlobj ) &&
        (i < fuzzy_match_seps + 1);
        i++, ahead_dlptr = dlref_next(ahead_dlptr) )
    EMPTY_STATEMENT();

  while ( ahead_dlptr != end_dlobj ) {
    start_dlptr = dlref_next(start_dlptr);
    ahead_dlptr = dlref_next(ahead_dlptr);
  }

  /* no objects left to fuzzy matches*/
  if ( start_dlptr == end_dlobj )
    return MERGE_NONE;

  /* now thin down the list to fuzzy matchables
     valid candidates must be fuzzy matchable and so
     must all those after them till the end_dlobj
  */
  end_lobj = dlref_lobj(end_dlobj);

  for (fCandidate = FALSE; !fCandidate && (start_dlptr != end_dlobj);
       start_dlptr = dlref_next(start_dlptr) ) {
    fCandidate = TRUE;

    for (ahead_dlptr = start_dlptr; ahead_dlptr != end_dlobj;
         ahead_dlptr = dlref_next(ahead_dlptr) ) {
      ahead_lobj = dlref_lobj(ahead_dlptr);

      /*check for a valid object to compare */
      ret = rcb_CheckCandidate(page, ahead_lobj, end_lobj, end_dlobj, p_ncolor_new,
                               MERGE_FUZZY, compare_lobjs, b1, b2, bm,
                               plast_same_clip_ptr);
      /* stop looking if we get a dodgy answer */
      if ( ret == MERGE_ERROR )
        return ret;

      /* Either didn't find match, or this match not wanted. */
      if ( (ret & MERGE_FUZZY) == 0 ) {
        fCandidate = FALSE;
        break;
      }
    }
  }

  if ( start_dlptr == end_dlobj ) /* no objects left to fuzzy matches*/
    return MERGE_NONE;

  /* if we get here then the list, start_dlptr to end_dlobj,
   * contains all traps (less the end_dlobj)
   */
  *p_start_dlptr = start_dlptr;
  return MERGE_FUZZY;
}

/**
 * Is the node present here or nearby on the display list?
 * Well, it won't be if we are on the first separation, in which
 * case the next pointer of the display list object will be NULL,
 * so we dont need to make that a special case.
 */
static rcb_merge_t rcb_merge_dl_objects(DL_STATE *page, LISTOBJECT *lobj,
                                        p_ncolor_t *pp_ncolor_new,
                                        COMPARE_LOBJS compare_lobjs,
                                        rcb_merge_t test_type)
{
  int32 b1, b2, bm;
  int32 last_band;
  DLREF* exact_dlobj = NULL;
  Bool fTheRestAreTraps = FALSE;
  Bool fLastWasMatch;
  rcb_merge_t ret;

  int32 pass;
  int32 fOrderOK;
  int32 fuzzy_match_seps = rcbn_cseps();
  rcb_merge_t found_exact_match = MERGE_NONE;
  CLIPOBJECT *last_same_clip_ptr = NULL;  /* cache the last matching clip */

  HQASSERT(rcbn_enabled(), "should be recombining");
  HQASSERT(!rcbn_first_separation(), "first separation");

  HQASSERT(lobj != NULL, "null lobj");
  HQASSERT(lobj->bbox.y1 <= lobj->bbox.y2, "top and bottom in wrong order");

  HQASSERT((( lobj->spflags & RENDER_RECOMBINE ) != 0 ) ,
           "object to merge is not a recombine object" );

  last_band = page->sizefactdisplaylist;
  RCB_BBOX_TO_BANDS(page, lobj->bbox.y1, lobj->bbox.y2, b1, b2, "rcb merge");
  bm = ((lobj->bbox.y1 + lobj->bbox.y2) / 2) / page->sizefactdisplayband;
  if ( bm < 0 )
    bm = 0;
  else if ( bm > page->sizefactdisplaylist )
    bm  = page->sizefactdisplaylist;

  exact_dlobj = NULL;
  fTheRestAreTraps = FALSE;

  /* We have to look at the object starting from end up to the end of the
   * band. unfortunately if we're in the 3 sep or on, the object may be out
   * of order since they are in the thruid sep A then B, but on the first sep
   * B is imaged, and then on the second sep A is imaged. At this point the
   * order is backwards and a pass from the end ptr will not find the object
   * since it is before end. At that point, we have to start back at the
   * beginning and look up to the endptr. Once we find it, code later will
   * determine that it is out of order and will resplice  the DL to put
   * it back in order.
   */

  for ( pass = 0; pass < 2; ++pass ) {
    DLREF *old_dlobj, *start_dlptr, *end_dlptr;

    /* We start our search in the 'middle' band of the object, so that
     * we're most likely to find a fuzzy match.  If we start at top or
     * bottom, we could miss it by one band.
     */
    HQASSERT(rcb_dl_tails(page, FALSE)[bm], "somehow lost enddisplay dl");
    start_dlptr = rcb_dl_tails(page, TRUE)[bm];
    HQASSERT(start_dlptr, "somehow lost imposition dl");

    if ( pass == 0 ) {
      start_dlptr = rcb_dl_tails(page, FALSE)[bm];
      end_dlptr = NULL;
      exact_dlobj = NULL;
      fTheRestAreTraps = FALSE;
    }
    else {
      /* If we not at least in the 3rd plane then there is no way that
       * the objects can be out of order.
       */
      if ( !rcbn_order_important() &&
           (test_type & (MERGE_SPLIT | MERGE_CHECK)) == 0 )
        break;

      if ( exact_dlobj != NULL ) {
        if ( fuzzy_match_seps == 0 ) /* no more fuzzies to find */
          break;

        /* shorten the list to those items before the last exact match that
           ALL fuzzy match.
        */
        end_dlptr = exact_dlobj;

        /* Skip the erasepage; can never merge
          & doesn't have CLIPOBJECT (so assert below fires). */
        if ( start_dlptr == rcb_dl_head(page, bm) )
          start_dlptr = dlref_next(start_dlptr);

        ret = rcb_adjustFuzzyStart(page, &start_dlptr,
                                   end_dlptr,
                                   fuzzy_match_seps,
                                   *pp_ncolor_new,
                                   compare_lobjs,
                                   b1,b2,bm,
                                   &last_same_clip_ptr);

        if ( ret == MERGE_ERROR )
          return ret;

        HQASSERT((ret == MERGE_FUZZY)||(ret == MERGE_NONE),
                  "Bad result from rcb_adjustFuzzyStart");

        if (ret != MERGE_FUZZY)
          break;

        fTheRestAreTraps = TRUE;

      }
      else {
        end_dlptr = rcb_dl_tails(page, FALSE)[bm];

        /* Beginning of DL - already scanned it all. */
        if ( start_dlptr == end_dlptr )
          break;
      }
    }

    /* Skip the erasepage; can never merge
      & doesn't have CLIPOBJECT (so assert below fires). */
    if ( start_dlptr == rcb_dl_head(page, bm) )
      start_dlptr = dlref_next(start_dlptr);

    /* a flag to keep tabs on the last attempted match*/
    fLastWasMatch = TRUE;

    for ( old_dlobj = start_dlptr; (old_dlobj != end_dlptr);
          old_dlobj = dlref_next(old_dlobj) ) {
      LISTOBJECT *old_lobj;

      if ( exact_dlobj != NULL &&  /* already found exact match*/
           pass == 0 &&            /* still on 1st pass (looking for fuzzies */
           !fLastWasMatch )        /* last match failed */
        break;                     /* then move onto next pass */

      HQASSERT( old_dlobj, "somehow ran off the end" );

      old_lobj = dlref_lobj(old_dlobj);

      fLastWasMatch = FALSE;

      /* if we've established that all the objects till the end
         are traps then don't repeat the compare */
      if ( fTheRestAreTraps ) {
        ret = MERGE_FUZZY;
      } else {

        ret = rcb_CheckCandidate(page, lobj, old_lobj, old_dlobj,
                                 *pp_ncolor_new, test_type, compare_lobjs,
                                 b1, b2, bm, &last_same_clip_ptr);

        if ( ret == MERGE_NONE )
          continue;

        if ( ret == MERGE_DONE || ret == MERGE_ERROR )
          return ret;

        /* Either didn't find match, or this match not wanted. */
        if ( (ret & test_type) == 0 )
          continue;

        /* Special case of do we need to split up a vignette. */
        if ( ret == MERGE_SPLIT )
          return rcb_vignette_split(page, old_lobj, lobj);
      }

      fLastWasMatch = TRUE;

      if ( ret == MERGE_FUZZY ) {
        if ( lobj->opcode != RENDER_vignette && lobj->opcode != RENDER_shfill ) {

          found_exact_match |= MERGE_FUZZY;

          /* Try to fuzzy match all the dl objects from previous separations */
          fuzzy_match_seps -= dl_num_channels( old_lobj->p_ncolor );
          if ( old_lobj->attr.planes )
            fuzzy_match_seps += dl_num_channels(old_lobj->attr.planes);
          if ( fuzzy_match_seps <= 0 ) {
            test_type &= ~MERGE_FUZZY;
            test_type &= ~MERGE_EXACT;
          }

          /* move the EndDisplayList pointers on to the merged node */
          if ( ! rcb_update_insertion_point(page, dlref_lobj(old_dlobj),
                                            b1, b2, bm, last_band,
                                            (ret & MERGE_FUZZY),
                                            RCB_INSERTPOINT_UPDATE,
                                            &fOrderOK ))
            return MERGE_ERROR;

          if ( !rcb_merge_fuzzy(page, old_lobj, lobj, pp_ncolor_new) )
            return MERGE_ERROR;

          /* Had to reorder the dl so restart from the beginning */
          if ( ! fOrderOK ) {
            pass = -1;
            break;
          }

          if ( test_type )
            continue;

          /* Since the test's have been exhausted, then we're done with
           * this node. return true to tell the caller that it found an exact
           * match */
          return found_exact_match;
        }
      }

      /* we have found the object, so we want to merge the contents of
       * the new node into the old one, and throw the new one
       * away - but only after possibly finding the fuzzy match */
      found_exact_match |= MERGE_EXACT;

      /* Exact matches also need to reduce the number of fuzzy matches left
       * to do. */
      fuzzy_match_seps -= dl_num_channels( old_lobj->p_ncolor );
      if ( old_lobj->attr.planes )
        fuzzy_match_seps += dl_num_channels(old_lobj->attr.planes);
      if ( fuzzy_match_seps <= 0 )
        test_type &= ~MERGE_FUZZY;

      /* peel off the exact match but so the search can continue
       * for a fuzzy match */
      test_type &= ~MERGE_EXACT;

      /* if we have found an exact match and we are still looking for a
         fuzzy trap then remember where we found the exact match
      */
      if ( ( test_type & MERGE_FUZZY ) && ( exact_dlobj == NULL ) )
          exact_dlobj = old_dlobj;

      /* move the EndDisplayList pointers on to the merged node */
      if ( !rcb_update_insertion_point(page, dlref_lobj(old_dlobj), b1, b2, bm,
                                       last_band, (ret & MERGE_FUZZY),
                                       RCB_INSERTPOINT_UPDATE, &fOrderOK) )
        return MERGE_ERROR;

      if ( ! rcb_merge_exact(page, old_lobj, lobj, pp_ncolor_new) )
        return MERGE_ERROR;

      /* If the objects are images, then we need to merge their contents.
       * This is the only special case.
       */
      if ( lobj->opcode == RENDER_image ) {
        IMAGEOBJECT *src_image = lobj->dldata.image;
        IMAGEOBJECT *dst_image = old_lobj->dldata.image;
        ++theINPlanes( dst_image );
        if ( !im_storemerge(src_image->ims, dst_image->ims) ||
             !im_expandmerge(page, src_image->ime, dst_image->ime) )
          return MERGE_ERROR;
      }
      else if ( lobj->opcode == RENDER_vignette ) {
        /* Need to recurse on all sub-elements of the vignette. */
        VIGNETTEOBJECT *vigobj = lobj->dldata.vignette;
        LISTOBJECT *ret_lobj;
        int32 faddnewtodl;
        test_type &= ~MERGE_FUZZY; /* Always take out FUZZY for vignettes. */
        if ( ! rcbv_merge_vignettes( page, vigobj->compareinfo ,
                                     old_lobj, lobj, &ret_lobj, &faddnewtodl ))
          return MERGE_ERROR;
        HQASSERT( ret_lobj == old_lobj || faddnewtodl ,
              "ret_lobj and faddnewtodl out of sync" );
        /* Merged old into new, and old has been removed.
           Return MERGE_NONE to make new be added to the dl */
        if ( faddnewtodl )
          return MERGE_NONE;
      }
      else if ( lobj->opcode == RENDER_shfill ) {
        /* Recurse on sub-elements of shfills */

        test_type &= ~MERGE_FUZZY; /* Always take out FUZZY for shfills. */
        if ( !rcbs_merge_shfill(old_lobj, lobj) )
          return MERGE_ERROR;
      }

      /* Had to reorder the dl so restart from the beginning */
      if ( ! fOrderOK ) {
        pass = -1;
        break;
      }

      /* If this is the last match, or there is no fuzzy match then
       * return true */
      if ( test_type == MERGE_NONE )
        return found_exact_match;
    }
  }

  /* Failed to find aonther match for the object. The return is the boolean
   * found_exact_match which if false implies that the object is ont on the
   * DL yet (even if we found a fuzzy match for it). */
  return found_exact_match;
}

/**
 * Wrapper function around rcb_merge_dl_objects. Provides a handle on
 * the lobj's ncolor as the ncolor in the dl object may change with
 * merging.
 */
rcb_merge_t merge_dl_objects(DL_STATE *page,
                             LISTOBJECT *lobj, COMPARE_LOBJS compare_lobjs,
                             rcb_merge_t test_type)
{
  rcb_merge_t fMerge;
  p_ncolor_t     p_ncolor_new;
  VIGNETTEOBJECT *vigobj = NULL;
  rcbv_compare_t compareinfo;

  if ( !dl_copy(page->dlc_context, &p_ncolor_new, &lobj->p_ncolor) )
    return MERGE_ERROR;

  if ( lobj->opcode == RENDER_vignette ) {
    /* Setup the compare info, used to merge a successful match */
    vigobj = lobj->dldata.vignette;

    /* create some comparison work space */
    vigobj->compareinfo = & compareinfo;
    fMerge = rcb_merge_dl_objects(page, lobj, &p_ncolor_new,
                                  compare_lobjs, test_type);
    vigobj->compareinfo = NULL;
  } else {
    fMerge = rcb_merge_dl_objects(page, lobj, &p_ncolor_new,
                                  compare_lobjs, test_type);
  }

  dl_release(page->dlc_context, &p_ncolor_new);

  rcbn_object_merge_result(lobj->opcode, fMerge);

  return fMerge;
}

Bool rcb_merge_knockout(DL_STATE *page,
                        LISTOBJECT *knock_lobj, LISTOBJECT *vgimg_lobj,
                        int32 op, rcbv_compare_t *compareinfo ,
                        Bool fRemoveKnockout, LISTOBJECT **ret_lobj)
{
  HQASSERT( knock_lobj, "knock_lobj NULL" );
  HQASSERT( vgimg_lobj, "vgimg_lobj NULL" );

  /* If we're going to color correct the vgimg... */

  HQASSERT( ( knock_lobj->spflags & RENDER_RECOMBINE ) != 0 ,
      "knockout must be a recombine object" );
  HQASSERT( ( vgimg_lobj->spflags & RENDER_RECOMBINE ) != 0 ,
      "vignette/image must be a recombine object" );

  if ( !dl_merge_extra(page->dlc_context, &vgimg_lobj->p_ncolor,
                       &knock_lobj->p_ncolor) ||
       !rcb_merge_spots(page, vgimg_lobj, knock_lobj) )
    return FALSE;

  if ( op == RENDER_vignette ) {
    if ( knock_lobj->opcode == RENDER_vignette ) {
      int32 faddnewtodl;

      HQASSERT(ret_lobj != NULL, "ret_lobj null");
      HQASSERT(compareinfo != NULL, "compareinfo null");

      if ( ! rcbv_merge_vignettes( page, compareinfo,
                                   vgimg_lobj, knock_lobj ,
                                   ret_lobj, & faddnewtodl ))
        return FALSE;

      /* May need to add new vignette to dl if extended at both ends
       * (owing to overprint white objects at ends of vignettes) */
      if ( faddnewtodl ) {
        LISTOBJECT *new_lobj;
        VIGNETTEOBJECT *vigobj;

        new_lobj = *ret_lobj;
        HQASSERT(new_lobj != NULL, "new_lobj null");

        vigobj = new_lobj->dldata.vignette;
        HQASSERT(vigobj != NULL, "vigobj null");

        return add_listobject(page, new_lobj, NULL);
      }

      /* knockout vignette will have been freed and removed from the dl */
      return TRUE;

    } else {
      DLREF *vig_linkp;
      LISTOBJECT *vig_lobj;

      for ( vig_linkp = vig_dlhead(vgimg_lobj); vig_linkp;
          vig_linkp = dlref_next(vig_linkp) ) {
        vig_lobj = dlref_lobj(vig_linkp);
        /* All vignette links have same objectstate. */
        vig_lobj->objectstate = vgimg_lobj->objectstate;

        if ( !dl_merge_extra(page->dlc_context, &vig_lobj->p_ncolor,
                             &knock_lobj->p_ncolor) )
          return FALSE;
      }

      if ( ret_lobj != NULL )
        *ret_lobj = vgimg_lobj;
    }
  }

  /* Stop the k/o from happening by setting the dl color to none. */
  if ( fRemoveKnockout )
    dl_to_none(page->dlc_context, &knock_lobj->p_ncolor);

  return TRUE;
}

/**
 * rcb_isKnockout returns true iff all the color values are zero
 */
Bool rcb_isKnockout( LISTOBJECT *lobj )
{
  HQASSERT( lobj != NULL, "lobj null" );
  HQASSERT( ( lobj->spflags & RENDER_RECOMBINE ) != 0 ,
            "RENDER_RECOMBINE must be set" );

  if ( lobj->opcode == RENDER_void ||
       lobj->opcode == RENDER_erase )
    return FALSE;

  if ( lobj->opcode == RENDER_vignette ) {
    DLREF *dlptr;
    for ( dlptr = vig_dlhead(lobj); dlptr; dlptr = dlref_next(dlptr) )
      if ( ! rcb_isKnockout(dlref_lobj(dlptr)) )
        return FALSE;
  } else if ( lobj->opcode == RENDER_shfill ) {
    DLREF *dlptr;
    for ( dlptr = hdlOrderList(lobj->dldata.shade->hdl);
          dlptr; dlptr = dlref_next(dlptr) )
      if ( ! rcb_isKnockout(dlref_lobj(dlptr)) )
        return FALSE;
  } else {
    dl_color_t        dlc;
    dl_color_iter_t   dlci;
    dlc_iter_result_t iter_res;
    COLORVALUE        cv;
    COLORANTINDEX     ci;

    dlc_from_dl_weak( lobj->p_ncolor, & dlc );
    HQASSERT( dlc_check_black_white( & dlc ) == DLC_TINT_OTHER ,
              "Should only see DLC_TINT_OTHER in recombine dl colors" );

    for ( iter_res = dlc_first_colorant( & dlc, & dlci, & ci, & cv );
          iter_res == DLC_ITER_COLORANT;
          iter_res = dlc_next_colorant( & dlc, & dlci, & ci, & cv )) {
      if ( cv != COLORVALUE_PRESEP_WHITE )
        return FALSE;
    }
  }
  return TRUE;
}

/**
 * Object recombine assumes there's an erase object at the start of the DL for
 * many of the recombine DL manipulation functions.  Since the erase is now
 * outside the page group, in the containing HDL, we make a dummy erase that is
 * removed before compositing/rendering.
 */
Bool rcb_dl_start(DL_STATE *page)
{
  /* The properties of the dummy erase don't matter much.  It just needs to exist
     to avoid changing the recombine DL functions. */
  return adderasedisplay(page, FALSE);
}

/**
 * Object recombine assumes there's an erase object at the start of the DL for
 * many of the recombine DL manipulation functions.  Since the erase is now
 * outside the page group, in the containing HDL, we make a dummy erase that is
 * removed before compositing/rendering.
 */
static void remove_dummy_erase(DL_STATE *page)
{
  int32 b1, b2, band, last_band = page->sizefactdisplaylist;
  DLREF **head = rcb_dl_head_addr(page, last_band);
  DLREF **tails = rcb_dl_tails(page, FALSE);
  DLREF **snapshot = rcb_dl_tails(page, TRUE);
  LISTOBJECT *lobj;

  HQASSERT(*head != NULL, "Z-order band is empty");
  lobj = dlref_lobj(*head);
  HQASSERT(lobj != NULL && lobj->opcode == RENDER_erase,
           "Should have added a dummy erase");

  RCB_BBOX_TO_BANDS(page, lobj->bbox.y1, lobj->bbox.y2, b1, b2, "rcb dummy erase");
  HQASSERT(b1 >= 0 && b1 < last_band, "Invalid band");
  HQASSERT(b2 >= 0 && b2 < last_band, "Invalid band");

  for ( band = b1; ( band >= b1 && band <= b2 ) || ( band == last_band );
        ( band == b2 ) ? band = last_band : ++band ) {
    head = rcb_dl_head_addr(page, band);
    HQASSERT(*head != NULL && dlref_lobj(*head) == lobj,
             "Dummy erase not found in band");
    *head = dlref_next(*head);
    if ( dlref_lobj(tails[band]) == lobj )
      tails[band] = dlref_next(tails[band]);
    if ( dlref_lobj(snapshot[band]) == lobj )
      snapshot[band] = dlref_next(snapshot[band]);
  }
  /** \todo MJ free dummy erase and links */
}

/**
 * When the page is about to be rendered, BandTails can point
 * anywhere into the list since the last plane does not have to have
 * all of the objects in it. Set theIEndDisplayList to be the last object on
 * the page.
 */
static void recombineSetEndDisplayList(DL_STATE *page)
{
  int32 band;

  for ( band = 0; band <= page->sizefactdisplaylist; band++ ) {
    DLREF *dl = rcb_dl_tails(page, FALSE)[band];

    HQASSERT(dl, "band has NULL end pointer");
    while ( dlref_next(dl) != NULL )
      dl = dlref_next(dl);
    rcb_set_insertion(page, band, FALSE, dl);
  }
}

/**
 * This routine does all the work for finishing off the display list.
 */
Bool rcb_dl_finish(DL_STATE *page)
{
  /* The target HDL should be the base HDL; we can't partial paint while
     recombining. */
  page->targetHdl = page->currentHdl;

  /* For now skip most of the fix up code. It may be beneficial to do knockout
     merging of images etc to reduce the number of objects to * composite, but
     trap matched image planes would need to be padded in * the image expander
     (cf, im_rcb_adjustimage). */
  if ( rcbn_merge_required(RENDER_void) ) {
    /* Fixing vignette knockouts is still possible and useful. */
    if ( !rcbn_fixVignetteKnockouts(page) )
      return FALSE;
  }

  /* Set the end pointers to something useful. */
  recombineSetEndDisplayList(page);

  /* Remove dummy erase added to workaround recombine's dl manipulation
     functions assuming an erase at the start of the dl.  The erase is not in
     the page group, it's in the base hdl instead. */
  remove_dummy_erase(page);

  return TRUE;
}

void init_C_globals_recomb(void)
{
#if defined( ASSERT_BUILD )
  debug_recombine = FALSE;
  debug_vignette  = FALSE;
#endif
}

/* Log stripped */
