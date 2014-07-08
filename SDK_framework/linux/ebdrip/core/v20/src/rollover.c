/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:rollover.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for rollover (a.k.a. hidden) fills.
 */

#include "core.h"

#include "display.h"
#include "dlstate.h"
#include "dl_bres.h"
#include "hdlPrivate.h"
#include "group.h"
#include "pclAttrib.h"
#include "rollover.h"
#include "vnobj.h"
#include "surface.h"

/** A magic number, giving the number of pixels per line before we consider
    it worth starting a rollover. If we're not going to overwrite much stuff,
    then we don't bother marking rollovers. Ideally, this should reflect the
    cost over overwriting pixels, which will vary with the type of surface
    we're rendering to. If we're just overwriting a blit word or two, the
    cost of creating the intersection map, looking it up and filtering spans
    through it will outweigh the cost of overwriting.

    \todo This number is essentially a guess, more suitable for contone
    surfaces than halftone. In future, we could move the amount of overlap
    test into the renderer, and just use this test to determine that there is
    some possible overwriting.
*/
#define ROLLOVER_PIXELS_PER_LINE 10

/** Object types allowed for rollovers. Rollovers can now apply to any simple
    object type, so we allow char types to be considered. We could possibly
    include images and masks as well, but these will tend to generate lots of
    little spans that might complicate the intersection clipping map too
    much. */
static inline Bool rollover_opcode(LISTOBJECT *lobj)
{
  switch ( lobj->opcode ) {
    /* Quads excluded from rollovers because they're too small to be worth
       it. If they are unified with RENDER_rect or enlarged, consider
       them. */
  case RENDER_quad:
    if ( quad_is_line(lobj->dldata.quad) || quad_is_point(lobj->dldata.quad) )
      return FALSE ;
  case RENDER_fill:
  case RENDER_rect:
  case RENDER_char:
    return TRUE ;
  default:
    return FALSE ;
  }
}

/** Front-end Rollover ID, put into the rollover field of the DL object.
    Rollover objects have bit 1 (DL_ROLL_IT) set. */
static uintptr_t rollover_id = DL_ROLL_IT ;

static void dlobj_rollover_object(DL_STATE *page, LISTOBJECT *lobj,
                                  const DLREF *prev_lobj) ;

void dlobj_rollover(DL_STATE *page, LISTOBJECT *lobj, HDL *hdl)
{
  HQASSERT(lobj != NULL, "No LISTOBJECT to rollover") ;
  HQASSERT(hdl != NULL, "No HDL to add DL object to") ;

  /* Don't touch the rollover attribute if it's currently used for planes. */
  if ( (lobj->spflags & RENDER_RECOMBINE) == 0 ) {
    /* Clear the current object's rollover attribute. When we decide to
       perform a rollover, we set the previous and the current object's
       rollover ID. */
    lobj->attr.rollover = DL_NO_ROLL ;

    /* No more to do if we're not doing rollovers. */
    if ( page->fOmitHiddenFills ) {
      if ( lobj->opcode == RENDER_vignette ) {
        /* Vignettes get some more analysis, to determine if we can rollover
           the objects within the vignette. If all of the objects rollover,
           and share the same rollover ID, the whole vignette can
           rollover. */
        uintptr_t start_id = rollover_id ;
        DLREF *subdl = vig_dlhead(lobj);
        VIGNETTEOBJECT *vignette = lobj->dldata.vignette ;
        DLREF *prev ;

        HQASSERT(subdl && dlref_lobj(subdl), "Vignette with no objects") ;

        /* Initialise rollover to true if there are more than one objects.
           We'll reset it to false if the objects don't rollover. */
        vignette->rollover = (uint8)(dlref_next(subdl) != NULL) ;

        dlobj_rollover_object(page, dlref_lobj(subdl), NULL) ;

        for ( prev = subdl ; (subdl = dlref_next(prev)) != NULL ; prev = subdl ) {
          LISTOBJECT *subobj = dlref_lobj(subdl);

          HQASSERT(subobj, "No object attached to vignette link") ;

          dlobj_rollover_object(page, subobj, prev) ;

          /* If the object didn't get the same rollover ID as we started
             with, clear the whole vignette's rollover flag. Keep setting the
             object IDs, in case the sub-dl is rendered through the full
             render loop (which can take advantage of multiple sections). */
          if ( subobj->attr.rollover != start_id )
            vignette->rollover = FALSE ;
        }

        /* We never tested the first object in the vignette DL. We didn't
           need to, because we the only way the second one can have the
           start ID is if the first one was part of the same rollover. */
        HQASSERT(!vignette->rollover ||
                 dlref_lobj(vig_dlhead(lobj))->attr.rollover == start_id,
                 "First object didn't get expected rollover ID.") ;
      } else { /* Not a vignette object */
        dlobj_rollover_object(page, lobj, hdlOrderListLast(hdl)) ;
      }
    }
  }
}

static void dlobj_rollover_object(DL_STATE *page, LISTOBJECT *lobj,
                                  const DLREF *prev_link)
{
  HQASSERT(lobj != NULL, "No LISTOBJECT to rollover") ;

  /* Note if this object is a possible candidate for rolling over. Disallow
     pseudo erase pages, lines, non simple objects, patterned objects, some PCL5
     ROPped objects, objects in non-knockout groups. Tests for transparency are
     done at render time. */
  if ( (lobj->spflags & RENDER_PSEUDOERASE) == 0 &&
       rollover_opcode(lobj) &&
       lobj->objectstate->patternstate == NULL &&
       (lobj->objectstate->pclAttrib == NULL ||
        (lobj->objectstate->pclAttrib->dlPattern == NULL &&
         !pclROPRequiresDestination(lobj->objectstate->pclAttrib->rop))) ) {
    lobj->attr.rollover = DL_ROLL_POSSIBLE ;
  }

  /* Turn-off rollovers if NFILL is marked as a sparse object */
  if ( lobj->opcode == RENDER_fill ) {
    NFILLOBJECT *nfill = lobj->dldata.nfill;

    if ( nfill && (nfill->type & SPARSE_NFILL) )
      lobj->attr.rollover = DL_NO_ROLL;
  }

  /* Can't do anything if there is no previous object. */
  if ( prev_link != NULL ) {
    LISTOBJECT *prev_lobj = dlref_lobj((DLREF *)prev_link);

    HQASSERT(prev_lobj != NULL, "No LISTOBJECT for last Z order link") ;

    /* Don't do rollovers if this is not a candidate or the previous
       object was not a candidate, or if their Begin/Endpage status is
       different. */
    if ( lobj->attr.rollover != DL_NO_ROLL &&
         prev_lobj->attr.rollover != DL_NO_ROLL &&
         (lobj->spflags & (RENDER_BEGINPAGE|RENDER_ENDPAGE))
         == (prev_lobj->spflags & (RENDER_BEGINPAGE|RENDER_ENDPAGE)) &&
         /** \todo ajcd 2008-08-20: We currently also require that the
             clip state is the same, because the intersection blits will
             remove the unclipped object from the self-intersection map.
             This doesn't matter if the clip state is the same, because
             anything accidentally removed from outside of the clip area
             won't show anyway. */
         lobj->objectstate->clipstate == prev_lobj->objectstate->clipstate ) {
      dbbox_t overlap ;

      bbox_intersection(&lobj->bbox, &prev_lobj->bbox, &overlap) ;

      /* In English:

      - There must be some intersection between the objects.
      - If we're continuing a rollover, fully contained is sufficient
      - If we're continuing a rollover, fully containing is sufficient
      - Otherwise, there must minimum overlap X intersection and area

      The minimum X size and area overlap for the first rollover tries
      to model the cost of setting up the self-intersection form. Once
      a self-intersection form has been set up, we also accept objects
      contained within the previous object, or totally obscuring the
      previous object, regardless of their size.

      If we cannot reserve the self-intersecting band, then don't rollover
      either. It's an optimisation, so we can get by without doing it.
      */
      if ( !bbox_is_empty(&overlap) &&
           ((prev_lobj->attr.rollover != DL_NO_ROLL &&
             (bbox_equal(&overlap, &lobj->bbox) ||
              bbox_equal(&overlap, &prev_lobj->bbox))) ||
            (overlap.x2 - overlap.x1 >= ROLLOVER_PIXELS_PER_LINE &&
             /* The following area test is cross multiplied to reduce the
                chance of the numbers exceeding the dcoord range. It
                tests whether the intersection area is greater than the
                required pixel overlap times the band height. This is an
                attempt to reflect the cost of setting up and maintaining
                the intersection form for the whole band: */
             (overlap.x2 - overlap.x1) * ROLLOVER_PIXELS_PER_LINE
             > (overlap.y2 - overlap.y1) * page->band_lines)) ) {
        HQASSERT(rollover_id != DL_NO_ROLL,
                 "Rollover ID shouldn't be DL_NO_ROLL") ;
        HQASSERT((rollover_id & DL_ROLL_IT) != 0,
                 "Rollover ID doesn't have DL_ROLL_IT bit") ;
        if ( dl_reserve_band(page, RESERVED_BAND_SELF_INTERSECTING) )
          prev_lobj->attr.rollover = lobj->attr.rollover = rollover_id ;
      }
    }

    if ( (lobj->attr.rollover & DL_ROLL_IT) == 0 &&
         (prev_lobj->attr.rollover & DL_ROLL_IT) != 0 ) {
      /* Previous object was rolled over, but this one isn't. Bump the
         rollover ID, so that we don't merge the rollovers if this one
         turns out to be rolled over by the next object. We don't need
         to check for wraparound, because the storage for the rollover ID
         is a multiple of DL_ROLL_ID_INCR. */
      rollover_id += DL_ROLL_ID_INCR ;
      HQASSERT(rollover_id != DL_NO_ROLL,
               "Rollover ID shouldn't be DL_NO_ROLL") ;
      HQASSERT((rollover_id & DL_ROLL_IT) != 0,
               "Rollover ID doesn't have DL_ROLL_IT bit") ;
    }
  }
}

void init_C_globals_rollover(void)
{
  rollover_id = DL_ROLL_IT ;
}

/* Log stripped */
