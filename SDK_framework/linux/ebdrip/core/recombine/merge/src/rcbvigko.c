/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbvigko.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine vignette knock-outs
 */

#include "core.h"
#include "rcbvigko.h"

#include "display.h"
#include "vnobj.h"
#include "dl_color.h"
#include "dl_store.h"
#include "gs_color.h"
#include "dlstate.h"
#include "often.h"
#include "stacks.h"
#include "params.h"

#include "recomb.h"
#include "rcbcntrl.h"
#include "rcbcomp.h"
#include "rcbsplit.h"
#include "rcbdl.h"
#include "rcbtrap.h"

#if defined( ASSERT_BUILD )
static Bool debug_fix_knockouts = FALSE;
#endif

rcb_merge_t rcbn_compare_vignette_knockout(LISTOBJECT *vlobj, LISTOBJECT *klobj)
{
  NFILLOBJECT *nfillk;
  NFILLOBJECT *nfillt;
  VIGNETTEOBJECT *vobj;

  HQASSERT(vlobj != NULL, "vlobj NULL");
  HQASSERT(klobj != NULL, "klobj NULL");
  HQASSERT(vlobj->opcode == RENDER_vignette, "vlobj should be a vignette");
  HQASSERT(klobj->opcode == RENDER_fill, "klobj should be a rectfill");

  nfillk = klobj->dldata.nfill;
  HQASSERT(nfillk != NULL, "nfillk should not be NULL");

  vobj = vlobj->dldata.vignette;
  HQASSERT(vobj != NULL, "vobj should not be NULL");

  /* Compare normal outline. */
  nfillt = vobj->outlines.nfillm;
  HQASSERT(nfillt != NULL, "nfillt should not be NULL");
  if ( same_nfill_objects( nfillk, nfillt ))
    return MERGE_EXACT;
  if ( nfillk->rcbtrap != NULL && nfillt->rcbtrap != NULL )
    if ( rcbt_comparetrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE, TRUE) ) {
      if ( rcbt_compareexacttrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE) )
        return MERGE_EXACT;
      return MERGE_FUZZY;
    }

  /* Compare special outline (mid circle of doughnuts). */
  nfillt = vobj->outlines.nfills;
  if ( nfillt != NULL ) {
    if ( same_nfill_objects(nfillk, nfillt) )
      return MERGE_EXACT;
    if ( nfillk->rcbtrap != NULL && nfillt->rcbtrap != NULL )
      if ( rcbt_comparetrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE, TRUE ) ) {
        if ( rcbt_compareexacttrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE) )
          return MERGE_EXACT;
        return MERGE_FUZZY;
      }
  }

  /* Compare overprinted extended (at head) outline. */
  nfillt = vobj->outlines.nfillh;
  if ( nfillt != NULL ) {
    if ( same_nfill_objects(nfillk, nfillt) )
      return MERGE_EXACT;
    if ( nfillk->rcbtrap != NULL && nfillt->rcbtrap != NULL )
      if ( rcbt_comparetrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE, TRUE) ) {
        if ( rcbt_compareexacttrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE) )
          return MERGE_EXACT;
        return MERGE_FUZZY;
      }
  }

  /* Compare overprinted extended (at tail) outline. */
  nfillt = vobj->outlines.nfillt;
  if ( nfillt != NULL ) {
    if ( same_nfill_objects(nfillk, nfillt) )
      return MERGE_EXACT;
    if ( nfillk->rcbtrap != NULL && nfillt->rcbtrap != NULL )
      if ( rcbt_comparetrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE, TRUE ) ) {
        if ( rcbt_compareexacttrap(nfillk->rcbtrap, nfillt->rcbtrap, TRUE) )
          return MERGE_EXACT;
        return MERGE_FUZZY;
      }
  }
  return MERGE_NONE;
}

static Bool testVignetteKnockout(DL_STATE *page,
                                 LISTOBJECT *vig_lobj, LISTOBJECT *ko_lobj,
                                 Bool *restitchokay,
                                 rcbv_compare_t *compareinfo,
                                 DLREF **ppfirsttrap, DLREF **pplasttrap)
{
  uint8 ko_opcode;
  int32 nci;
  COLORANTINDEX cis[ 4 ];
  VIGNETTEOBJECT *vig_vobj, *ko_vobj;
  DLREF *tlink;

  HQASSERT(vig_lobj != NULL, "vig_lobj null");
  HQASSERT(ko_lobj != NULL, "ko_lobj null");
  HQASSERT(compareinfo != NULL, "compareinfo null");

  ko_opcode = ko_lobj->opcode;
  HQASSERT(vig_lobj->opcode == RENDER_vignette, "vig_lobj not a vignette");

  (*restitchokay) = TRUE;
  compareinfo->matchtype = MERGE_NONE;

  if ( ( vig_lobj == ko_lobj ) ||
       ( ko_opcode != RENDER_fill && ko_opcode != RENDER_vignette ) ||
       ( dl_is_none( ko_lobj->p_ncolor )) ||
       ( ( ko_lobj->spflags & RENDER_RECOMBINE ) == 0 ))
    return TRUE;

  /* Ignore knockout that is a vignette if already had look at it. */
  if ( ko_lobj->opcode == RENDER_vignette &&
       ( ko_lobj->marker & MARKER_VN_FIXKNOCKOUT ) == 0 )
    return TRUE;

  vig_vobj = vig_lobj->dldata.vignette;
  ko_vobj = (( ko_opcode == RENDER_vignette )
      ? ko_lobj->dldata.vignette : NULL );

  HQASSERT(vig_vobj != NULL &&
      ( ko_opcode != RENDER_vignette || ko_vobj != NULL ),
      "Must have a vignette object for a vignette dl object" );

  /* Knockouts as vignettes must have a flat tint */
  if ( ko_vobj != NULL && ko_vobj->colormonotonic != VDC_Neutral )
    return TRUE;

  /* For spot to spot/process vignettes may have either 1 (spot) or N (process)
   * colorants in common, where N must equal the number of process separations
   * we've seen. That's because the first spot or process color is painted as
   * per normal (which kocks out in all the other separations) and the second
   * spot or process color is overprinted.
   */
  if ( !dl_upto_4_common_colorants( ko_lobj->p_ncolor,
                                     vig_lobj->p_ncolor, cis, &nci ))
    return TRUE;

  /* Allow a single spot or all process colorants in common.
     This occurs owing to the manner some apps simulate DeviceN */
  if ( nci > 0 && !rcb_valid_common_colorants( cis, nci ))
    return TRUE;

  HQASSERT(ko_lobj->objectstate != NULL, "objectstate null");
  HQASSERT(ko_lobj->objectstate->clipstate != NULL, "clipstate null" );

  if ( !same_clip_objects( vig_lobj->objectstate->clipstate,
        ko_lobj->objectstate->clipstate, TRUE, 0))
    return TRUE;

  switch ( ko_opcode ) {
  case RENDER_fill:
    compareinfo->matchtype =
      rcbn_compare_vignette_knockout( vig_lobj, ko_lobj );
    break;

  case RENDER_vignette: {
    compareinfo->matchtype =
      rcbv_compare_vignettes( compareinfo, vig_lobj, ko_lobj, 0, TRUE );
    break;
  }
  default:
    HQFAIL( "ko_opcode must be Rect|Fill|Vignette" );
    return FALSE;
  }
  HQASSERT(compareinfo->matchtype != MERGE_ERROR, "unexpected error" );
  if ( compareinfo->matchtype == MERGE_NONE )
    return TRUE;

  /* check common colorants with other traps */
  for ( tlink = *pplasttrap; tlink; tlink = dlref_next(tlink) ) {
    if (dlref_lobj(tlink) == ko_lobj) {
      /* KO is AFTER image */
      if ( !dl_upto_4_common_colorants( ko_lobj->p_ncolor,
                                     dlref_lobj(*ppfirsttrap)->p_ncolor, cis,
                                     &nci )) {
        compareinfo->matchtype = MERGE_NONE;
        return TRUE;
      }

      if ( nci > 0 && !rcb_valid_common_colorants( cis, nci )) {
        compareinfo->matchtype = MERGE_NONE;
        return TRUE;
      }

      break;
    }
  }

  if (!tlink) {
    if ( !dl_upto_4_common_colorants( ko_lobj->p_ncolor,
                                     dlref_lobj(*pplasttrap)->p_ncolor,
                                     cis, &nci )) {
      compareinfo->matchtype = MERGE_NONE;
      return TRUE;
    }

    if ( nci > 0 && !rcb_valid_common_colorants( cis, nci )) {
      compareinfo->matchtype = MERGE_NONE;
      return TRUE;
    }

    if (ko_lobj->opcode == RENDER_fill) {
      if ( !dl_upto_4_common_colorants( ko_lobj->p_ncolor,
                                     dlref_lobj(*ppfirsttrap)->p_ncolor,
                                     cis, &nci )) {
        compareinfo->matchtype = MERGE_NONE;
        return TRUE;
      }

      if ( nci > 0 && !rcb_valid_common_colorants( cis, nci )) {
        compareinfo->matchtype = MERGE_NONE;
        return TRUE;
      }
    }
  }

  /* Check making the vignette and ko adjacent does not change the z-order */
  if ( !rcb_restitch_check(page, vig_lobj, ko_lobj, vig_lobj->bbox.y1,
                           vig_lobj->bbox.y2, restitchokay) )
    return FALSE;

  if ( !(*restitchokay) )
    compareinfo->matchtype = MERGE_NONE;

  return TRUE;
}

static Bool testVignette( LISTOBJECT *vig_lobj )
{
  VIGNETTEOBJECT *vig_vobj;

  HQASSERT(vig_lobj != NULL, "vig_lobj null");

  /* Ignore objects that aren't vignettes. */
  if ( vig_lobj->opcode != RENDER_vignette )
    return FALSE;

  /* Ignore vignettes that we've already had a look at. */
  if (( vig_lobj->marker & MARKER_VN_FIXKNOCKOUT ) == 0 )
    return FALSE;

  /* Test on MARKER_VN_FIXKNOCKOUT ensures that color is not none */
  HQASSERT(!dl_is_none( vig_lobj->p_ncolor ), "color should not be none");

  if ( ( vig_lobj->spflags & RENDER_RECOMBINE ) == 0 )
    return FALSE;

  vig_vobj = vig_lobj->dldata.vignette;
  HQASSERT(vig_vobj != NULL, "vig_vobj null");

  /* Ignore vignettes that are actually knockouts,
     look for the complementary vignette instead */
  if ( vig_vobj->colormonotonic == VDC_Neutral )
    return FALSE;

  /* Ignore vignettes that have been fully recombined in all planes. */
  if ( dl_num_channels( vig_lobj->p_ncolor ) == rcbn_cseps())
    return FALSE;

  /* For safety; this should always be non-NULL. */
  return ( vig_vobj->outlines.nfillm != NULL );
}

static Bool fixVignetteKnockout(DL_STATE *page, LISTOBJECT *vig_lobj,
                                LISTOBJECT *ko_lobj, VIGNETTEOBJECT *vig_vobj,
                                rcbv_compare_t *compareinfo, Bool *pfReset,
                                DLREF *ko_link, DLREF **ppfirsttrap,
                                DLREF **pplasttrap)

{
  DLREF *tlink;
  LISTOBJECT *ret_lobj;

  /* Found knockout so can be confident that it really is a vignette */
  HQTRACE( debug_vignette && vig_vobj->confidence == VDC_Low,
      ( "promoted vignette to VDC_High" ));
  vig_vobj->confidence = VDC_High;

  HQTRACE( debug_fix_knockouts, ( "fixed vignette which needed knockout" ));

  /* We wish to move the KO so it is next to the vignette and the potential
     group of objects merged with it.
     Also merge the color from the k/o into the vignette so it
     can be included in color correction.
     If the KO is AFTER the group then restitch it to just BEFORE
     otherwise restitch it to just before the last item in the group.
   */
  for ( tlink = *pplasttrap; tlink; tlink = dlref_next(tlink) ) {
    if (tlink == ko_link) {
      /* KO is AFTER image */
      if ( !rcb_restitch_update(page, dlref_lobj(*ppfirsttrap), ko_lobj,
                                vig_lobj->bbox.y1, vig_lobj->bbox.y2) )
        return FALSE;

      /* if there is only the vignette in the group and the KO is not a fill
       * then extend the group to include it (n.b. fill will have color set
       * to none later) */
      if ( ko_lobj->opcode != RENDER_fill && *ppfirsttrap == *pplasttrap )
        *ppfirsttrap = ko_link;
      break;
    }
  }

  if (!tlink) {
    /* KO is BEFORE the vignette objects group.We must move the KO to just
     * before the LAST item in the group in order to shove unwanted objects
     * between down the list past the group. If the KO is a fill it will
     * eventually have color none and so doesn't want to remain in the middle
     * of the group. So in that case we bump it up to just before the first
     * item in the group and ignore it.
     */
    if ( !rcb_restitch_update(page, dlref_lobj(*pplasttrap), ko_lobj,
                              vig_lobj->bbox.y1, vig_lobj->bbox.y2) )
      return FALSE;

    if (ko_lobj->opcode == RENDER_fill) {
      if ( !rcb_restitch_update(page, dlref_lobj(*ppfirsttrap), ko_lobj,
                                vig_lobj->bbox.y1, vig_lobj->bbox.y2) )
        return FALSE;
    } else {
      if (*ppfirsttrap == *pplasttrap)
        *ppfirsttrap = ko_link;
    }
  }

  if ( !rcb_merge_knockout( page, ko_lobj, vig_lobj,
                            RENDER_vignette, compareinfo,
                            TRUE /* remove knockout */,
                            &ret_lobj ))
    return FALSE;

  HQASSERT((ret_lobj->marker && MARKER_VN_FIXKNOCKOUT) != 0, "corrupt marker");

  if ( dl_num_channels( ret_lobj->p_ncolor ) == rcbn_cseps())
    /* Clear the marker bit to stop searching for knockouts */
    ret_lobj->marker &= ~MARKER_VN_FIXKNOCKOUT;

  if ( ret_lobj == ko_lobj )
    /* Must reset the fix vignette knockout process */
    *pfReset = TRUE;

  return TRUE;
}

static Bool findVignetteKnockout( DL_STATE *page,
                                  DLREF *vig_link,
                                  Bool *pfMergedKO,
                                  Bool *pfReset,
                                  DLREF **ppfirsttrap,
                                  DLREF **pplasttrap )
{
  LISTOBJECT *vig_lobj;
  VIGNETTEOBJECT *vig_vobj;
  rcbv_compare_t fuzzycompareinfo;
  int32 fuzzy_opcode = RENDER_void;
  int32 pass;
  LISTOBJECT *fuzzy_ko_lobj = NULL;
  DLREF *fuzzy_ko_link = NULL;

  HQASSERT(vig_link != NULL, "vig_link null");
  HQASSERT(pfMergedKO != NULL, "pfMergedKO mull");
  HQASSERT(pfReset != NULL, "pfReset mull");

  *pfMergedKO = FALSE;

  vig_lobj = dlref_lobj(vig_link);
  HQASSERT(vig_lobj != NULL, "vig_lobj null");
  HQASSERT(vig_lobj->opcode == RENDER_vignette, "vig_lobj not a vignette" );
  HQASSERT((vig_lobj->marker & MARKER_VN_FIXKNOCKOUT ) != 0,
      "fixknockout marker not set");

  vig_vobj = vig_lobj->dldata.vignette;
  HQASSERT(vig_vobj != NULL, "vig_vobj null");

  HQTRACE( debug_fix_knockouts, ( "found vignette which needs knockout" ));

  *pfReset = FALSE;

  /* fuzzy_ko_link is used to hold onto a fuzzy match in case we do
     not subsequently find an exact match */
  fuzzy_ko_link = NULL;

  /* Now search for a suitable knockout object (may not exist) */
  for ( pass = 0; pass < 2 ; ++pass ) {
    DLREF *start_dlptr, *end_dlptr, *ko_link;

    if ( pass == 0 ) {
      start_dlptr = vig_link;
      end_dlptr   = NULL;
    } else {
      start_dlptr =
        rcb_dl_head(page, page->sizefactdisplaylist /* z-order band */);
      end_dlptr   = vig_link;
    }

    for ( ko_link = start_dlptr; ko_link != end_dlptr;
          ko_link = dlref_next(ko_link) ) {
      LISTOBJECT *ko_lobj;
      rcbv_compare_t compareinfo;
      int32 restitchokay = FALSE;

      SwOftenUnsafe();

      ko_lobj = dlref_lobj(ko_link);

      if ( !testVignetteKnockout(page, vig_lobj, ko_lobj, &restitchokay,
                                 &compareinfo, ppfirsttrap, pplasttrap) )
        return FALSE;

      if ( compareinfo.matchtype != MERGE_NONE ) {
        if ( compareinfo.matchtype & MERGE_FUZZY ) {
          /* Choose a vignette ko over a fill ko */
          if ( fuzzy_ko_link == NULL ||
               (fuzzy_opcode != RENDER_vignette &&
                ko_lobj->opcode == RENDER_vignette) ) {
            fuzzy_ko_lobj = ko_lobj;
            fuzzy_ko_link = ko_link;
            fuzzycompareinfo = compareinfo;
            fuzzy_opcode = ko_lobj->opcode;

            /* ko can only come before the vignette on the first pass if the
             * ko is the first dl object; ko always comes before the vignette
             * on the second pass
             */
            *pfReset = ( pass == 1 || dlref_next(ko_link) == vig_link );
          }
          /* Found a fuzzy match but keep looking
           * in case find an exact match instead */
          continue;
        }

        HQASSERT(compareinfo.matchtype & MERGE_EXACT,
            "expected to find an exact match");

        /* This affects the rewind position */
        *pfReset = ( pass == 1 || dlref_next(ko_link) == vig_link );

        *pfMergedKO = TRUE;

        return fixVignetteKnockout( page, vig_lobj, ko_lobj,
                                    vig_vobj, &compareinfo, pfReset,
                                    ko_link, ppfirsttrap, pplasttrap );
      }
      else if ( pass == 0 && !restitchokay ) {
        /* if the vignette and ko were merged then restitching the dl to make
         * the objects adjacent would cause a change in the z-order;
         * any further objects on the first pass would also change the z-order,
         * therefore can abandon the first pass - this is not the case for
         * the second pass
         */
         break;
      }
    }
  }

  if ( fuzzy_ko_link ) {
    /* Use the first fuzzy matched set of knockouts */
    *pfMergedKO = TRUE;
    return fixVignetteKnockout( page, vig_lobj, fuzzy_ko_lobj,
                                vig_vobj, &fuzzycompareinfo, pfReset,
                                fuzzy_ko_link, ppfirsttrap, pplasttrap );
  }

  return TRUE;
}

Bool rcbn_fixVignetteKnockouts(DL_STATE *page)
{
  DLREF *vig_link, *previous_link;
  DLREF *firsttrap, *lasttrap;

  /* previous_link points to the dl object before the current candidate
     vignette dl object. If a vignette has had knockouts merged AND the
     knockouts ocurred AFTER the vignette, then can restart from the dl
     object after previous_link instead of going right back to the
     beginning of the display list */
  previous_link = vig_link =
    rcb_dl_head(page, page->sizefactdisplaylist /* z-order band */);

#if defined( ASSERT_BUILD )
  {
    LISTOBJECT *erase_lobj = dlref_lobj(vig_link);
    HQASSERT(erase_lobj != NULL && erase_lobj->opcode == RENDER_erase,
        "First dl object is not an erase" );
  }
#endif

  /* use group markers for keeping an eye on groups of merged objects
     these should all end up adjacent to each other */
  firsttrap = lasttrap = NULL;

  vig_link = dlref_next(vig_link); /* skip the erase */

  while ( vig_link != NULL ) {
    LISTOBJECT *vig_lobj;
    int32 rewind = FALSE;
    int32 reset = FALSE;

    SwOftenUnsafe();

    HQASSERT(dlref_next(previous_link) == vig_link, "bad previous_link" );

    vig_lobj = dlref_lobj(vig_link);

    if ( testVignette( vig_lobj )) {
      DLREF * tlink;
      int32 mergedKO;

      /* if current vignette isn't in the object group
         then make a new group from just it */
      for ( tlink = firsttrap;tlink && tlink != lasttrap;
            tlink = dlref_next(tlink) ) {
        if (vig_link == tlink)
          break;
      }

      if ( tlink == lasttrap && lasttrap != vig_link )
        firsttrap = lasttrap = vig_link;

      /* The knockout for this vignette is likely to occur immediately
       * before or after the vignette; therefore start searching for the
       * ko from the dl object before the vignette */
      if ( !findVignetteKnockout(page, vig_link, &mergedKO, &reset,
                                 &firsttrap, &lasttrap) )
        return FALSE;

      if ( mergedKO ) {
        /* dl has changed; rewind to where we know it has not changed */
        rewind = TRUE;
      }
      else { /* Did not find knockout for this vignette. */
        VIGNETTEOBJECT *vig_vobj;

        vig_vobj = vig_lobj->dldata.vignette;
        HQASSERT(vig_vobj != NULL, "vig_vobj null");
        if ( vig_vobj->confidence == VDC_High ) {
          /* No knockouts and not splitting,
             clear the flag to stop looking for knockouts again */
          vig_lobj->marker &= ~MARKER_VN_FIXKNOCKOUT;
        }
      }
    }

    /* splitup low confidence vignettes */
    if ( vig_lobj->opcode == RENDER_vignette ) {
      VIGNETTEOBJECT *vig_vobj;

      vig_vobj = vig_lobj->dldata.vignette;
      HQASSERT(vig_vobj != NULL, "vig_vobj null");

      if ( vig_vobj->confidence == VDC_Low ) {
        HQTRACE(debug_vignette, ( "SPLIT vignette : %d", vig_len(vig_vobj)));
        HQASSERT(vig_len(vig_vobj) < UserParams.VignetteMinFills,
            "number of actual DL objects should be less than this" );

        if ( rcb_vignette_split_inline(page, vig_lobj, FALSE) == MERGE_ERROR ) {
          /* For safety; make sure only 1 object on DL */
          rcb_vignette_split_remove(page, vig_lobj);
          return FALSE;
        }

        rcb_vignette_split_free(page, vig_lobj);

        /* dl has changed; rewind to where we know it has not changed */
        rewind = TRUE;
      }
    }

    /* Update vignette and previous link pointers */
    if ( rewind ) {
      /* Found a ko for a vignette; rewind only as far back as necessary */
      if ( reset )
        vig_link = previous_link =
          rcb_dl_head(page, page->sizefactdisplaylist /* z-order band */);
      else
        vig_link = previous_link;
    }
    else {
      /* Did nothing this time around; advance previous_link */
      previous_link = vig_link;
    }
    vig_link = dlref_next(vig_link);
  }

  return TRUE;
}

void init_C_globals_rcbvigko(void)
{
#if defined( ASSERT_BUILD )
  debug_fix_knockouts = FALSE;
#endif
}

/* Log stripped */
