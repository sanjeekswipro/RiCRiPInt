/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbsplit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine vignette splitting
 */

#include "core.h"
#include "graphics.h"
#include "vnobj.h"
#include "dlstate.h"
#include "stacks.h"
#include "gstack.h"
#include "gu_chan.h"
#include "dl_free.h"
#include "routedev.h"
#include "display.h"
#include "dl_color.h"
#include "swerrors.h"
#include "hdl.h"
#include "recomb.h"
#include "rcbcntrl.h"
#include "rcbcomp.h"
#include "rcbsplit.h"
#include "rcbdl.h"

/**
 * Step foward n places the Display list chain
 */
static DLREF *dl_step_n(DLREF *dl, int32 n)
{
  while ( n-- > 0 )
    dl = dlref_next(dl);
  return dl;
}

static Bool rcb_restore_partialcolors(DL_STATE *page,
                                      LISTOBJECT *lobj, VIGNETTEOBJECT *vigobj)
{
  DLREF *link;
  LISTOBJECT *sublobj;

  HQASSERT(lobj != NULL, "lobj null");
  HQASSERT(vigobj != NULL, "vigobj null");
  HQASSERT(vigobj->partialcolors != NULL , "partialcolors null");

  link = vig_dlhead(lobj);
  HQASSERT(link != NULL, "link null");
  sublobj = dlref_lobj(link);
  HQASSERT(sublobj != NULL, "sublobj null");
  /* Look for a white object at the start */
  if ( ( sublobj->marker & MARKER_VN_WHITEOBJECT ) == 0 ) {
    link = dl_step_n(link, vig_len(vigobj) - 1 );
    HQASSERT(link != NULL, "link null");
    sublobj = dlref_lobj(link);
    HQASSERT(sublobj != NULL, "sublobj null");
    /* Look for a white object at the end */
    if ( ( sublobj->marker & MARKER_VN_WHITEOBJECT ) == 0 )
      return TRUE;    /* No white object at either end */
  }
  HQASSERT( ( sublobj->marker & MARKER_VN_WHITEOBJECT ) != 0 ,
            "Must be a whiteobject here!" );
  return dl_merge(page->dlc_context, &sublobj->p_ncolor, &vigobj->partialcolors);
}

int32 rcb_vignette_split_inline(DL_STATE *page,
                                LISTOBJECT *vlobj, Bool updateDL)
{
  VIGNETTEOBJECT *vigobj;
  DLRANGE dlrange;

  HQASSERT(vlobj, "lobj NULL");
  HQASSERT(vlobj->opcode == RENDER_vignette, "Need vignette object");
  vigobj = vlobj->dldata.vignette;
  HQASSERT(vigobj, "somehow lost VIGNETTEOBJECT");

  /* Fixup the dl colors when a white end has been merged thru the vignette */
  if ( vigobj->partialcolors != NULL ) {
    if ( !rcb_restore_partialcolors(page, vlobj, vigobj) )
      return MERGE_ERROR;
    dl_release(page->dlc_context, &vigobj->partialcolors); /* Finished with this now */
  }

  /* Iterate backwards over the sub-lobjs and insert them in turn immediately
     after vlobj.  This avoids having to mess with insertion ptrs. */
  hdlDlrangeBackwards(vigobj->vhdl, &dlrange);

  for ( dlrange_start(&dlrange); !dlrange_done(&dlrange); dlrange_next(&dlrange) ) {
    LISTOBJECT *lobj = dlrange_lobj(&dlrange);
    int32 band, b1, b2, last_band = page->sizefactdisplaylist, count;
    DLREF *link;

    RCB_BBOX_TO_BANDS(page, lobj->bbox.y1, lobj->bbox.y2, b1, b2, "vigsplit0");
    count = b2 - b1 + 2; /* b1 to b2 inclusively, plus last_band */

    if ( (link = alloc_n_dlrefs(count, page)) == NULL ) {
      (void)error_handler(VMERROR);
      return MERGE_ERROR;
    }

    /* The Last band must use the first link for HDL bands destroy to work. */
    for ( band = last_band; band == last_band || (band >= b1 && band <= b2);
          band == last_band ? band = b2 : --band ) {
      /* Insert this lobj immediately after vlobj. */
      DLREF *vlink = dlref_next(rcb_find_lobj_pre(page, band, vlobj));

      if ( vlink != NULL ) { /* found it */
        DLREF *next = dlref_next(link);

        dlref_assign(link, lobj);
        dlref_setnext(link, dlref_next(vlink));
        dlref_setnext(vlink, link);

        if ( updateDL )
          rcb_set_insertion(page, band, FALSE, link);

        link = next;
      }
    }
    HQASSERT(link == NULL, "Failed to add the vignette sub-lobj correctly");
  }

  rcb_vignette_split_remove(page, vlobj);

  return MERGE_SPLIT;
}

/**
 * Used to remove an object from the DL. Currently only used for vignettes.
 */
void rcb_vignette_split_remove(DL_STATE *page, LISTOBJECT *lobj)
{
  int32 b1, b2, last_band = page->sizefactdisplaylist;

  RCB_BBOX_TO_BANDS(page, lobj->bbox.y1, lobj->bbox.y2, b1, b2, "vig remove");

  rcb_remove_from_some_bands(page, lobj, b1, b2);
  rcb_remove_from_some_bands(page, lobj, last_band, last_band);
}

/**
 * Used to free a vignette top level after it's DL chain has been split
 * onto the main DL
 */
void rcb_vignette_split_free(DL_STATE *page, LISTOBJECT *lobj)
{
  DLREF *vdlobj;

  HQASSERT(lobj, "lobj NULL");
  HQASSERT(lobj->opcode == RENDER_vignette, "need vignette object");

  for ( vdlobj = vig_dlhead(lobj); vdlobj; vdlobj = dlref_next(vdlobj) )
    dlref_assign(vdlobj, NULL);
  free_dl_object( lobj, page );
}

/**
 * Used to clear any marker flag(s) from a LISTOBJECT.
 */
static void rcb_vignette_clear_flags( LISTOBJECT *lobj, int32 flag )
{
  HQASSERT(lobj, "lobj NULL");
  HQASSERT((flag & (~( MARKER_VN_VNCANDIDATE | MARKER_VN_WHITEOBJECT))) == 0,
            "asked to clear unknown flag");

  lobj->marker &= (~flag);
  if ( lobj->opcode == RENDER_vignette ) {
    DLREF *vdlobj;
    for ( vdlobj = vig_dlhead(lobj); vdlobj; vdlobj = dlref_next(vdlobj) ) {
      LISTOBJECT *vlobj = dlref_lobj(vdlobj);
      vlobj->marker &= (~flag);
    }
  }
}

/* We've just added new_lobj to the dl as it didn't match anything, but now
 * we've found out that in fact it matches old_lobj if we split one of the
 * two objects. That obviously implies that at least one of old_lobj or
 * new_lobj are vignettes.
 * What we therefore need to do is to take these objects out of the dl,
 * splitting them up if they are vignettes and add them back in (merging
 * as appropriate). This process itself may cause further splitting, in
 * which case we'll recurse on this routine.
 * Three cases to consider:
 * a) old_lobj is a fill/rectangle, new_lobj is a vignette.
 * b) old_lobj is a vignette, new_lobj is a fill/rectangle.
 * c) old_lobj is a vignette, new_lobj is a vignette.
 *
 * Case a) old_lobj is a fill/rectangle, new_lobj is a vignette.
 *  In this case we need to:
 *   remove new_lobj
 *   split  new_lobj
 *   add    new_lobj
 *
 * Case b) old_lobj is a vignette, new_lobj is a fill/rectangle.
 *  In this case we need to:
 *   split  old_lobj inline
 *   remove new_lobj
 *   add    new_lobj
 *
 * Case c) old_lobj is a vignette, new_lobj is a vignette.
 *  In this case we need to:
 *   split  old_lobj inline
 *   remove new_lobj
 *   split  new_lobj
 *   add    new_lobj
 */
int32 rcb_vignette_split(DL_STATE *page,
                         LISTOBJECT *old_lobj, LISTOBJECT *new_lobj)
{
  uint8 basefunc_old;
  uint8 basefunc_new;

  basefunc_old = old_lobj->opcode;
  basefunc_new = new_lobj->opcode;

  HQASSERT(( basefunc_old == RENDER_vignette &&
             ( basefunc_new == RENDER_vignette ||
               basefunc_new == RENDER_fill )) ||
           ( basefunc_new == RENDER_vignette &&
             ( basefunc_old == RENDER_vignette ||
               basefunc_old == RENDER_fill )) ,
           "got wrong object types.\n at least one "
           "should be vignette and other should be rect/fill/vignette" );

  /* split old_lobj inline */
  if ( basefunc_old == RENDER_vignette )
    if ( rcb_vignette_split_inline(page, old_lobj, TRUE) == MERGE_ERROR ) {
      /* For safety; make sure only 1 object on DL. */
      rcb_vignette_split_remove(page, old_lobj);
      return MERGE_ERROR;
    }

  /* white object marker flags are no longer applicable */
  rcb_vignette_clear_flags(old_lobj, MARKER_VN_WHITEOBJECT);
  rcb_vignette_clear_flags(new_lobj, MARKER_VN_WHITEOBJECT);

  /* remove new_lobj */
  rcb_vignette_split_remove(page, new_lobj);

  /* split/add new_lobj */
  if ( basefunc_new == RENDER_vignette ) {
    DLREF *vdlobj;
    for ( vdlobj = vig_dlhead(new_lobj); vdlobj; vdlobj = dlref_next(vdlobj) ) {
      Bool added;

      if ( !add_listobject(page, dlref_lobj(vdlobj), &added) )
        return MERGE_ERROR;
      if ( added ) {
        /* If we're on the 2nd, 3rd or 4th separation of the job and the
         * object didn't get merged, then there is the potential that we need
         * to either split an existing vignette on the dl, or, split the
         * vignette we were adding.
         */
        if ( rcbn_merge_required(dlref_lobj(vdlobj)->opcode) )
          if ( merge_dl_objects(page, dlref_lobj(vdlobj),
                                compare_vignette_splitter,
                                MERGE_SPLIT|MERGE_EXACT) == MERGE_ERROR )
            return MERGE_ERROR;
      }
    }
  } else {
    Bool added;

    if ( !add_listobject(page, new_lobj, &added) )
      return MERGE_ERROR;

    if ( added ) {
      /* If we're on the 2nd, 3rd or 4th separation of the job and the object
       * didn't get merged, then there is the potential that we need to either
       * split an existing vignette on the dl, or, split the vignette we were
       * adding.
       */
      if ( rcbn_merge_required(new_lobj->opcode) )
        if ( merge_dl_objects(page, new_lobj, compare_vignette_splitter,
                              MERGE_SPLIT|MERGE_EXACT) == MERGE_ERROR )
          return MERGE_ERROR;
    }
  }

  /* Free top level LISTOBJECT & miscellaneous vignette data, having stolen
   * chain.
   */
  if ( basefunc_old == RENDER_vignette )
    rcb_vignette_split_free(page, old_lobj);
  if ( basefunc_new == RENDER_vignette )
    rcb_vignette_split_free(page, new_lobj);

  /* Don't mind what we return here as long as it is not MERGE_ERROR. */
  return MERGE_SPLIT;
}

/* Log stripped */
