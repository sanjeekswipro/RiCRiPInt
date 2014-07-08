/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:intersectblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Builtin implementation of self-intersecting blits.
 *
 * Self-intersecting blits are used as an optimisation to remove overlaps in
 * transparent shaded fills, for flattening the Z-order of rollovers, and
 * will be used for text knockout mode.
 */

#include "core.h"
#include "surface.h"
#include "bitblts.h"
#include "bitblth.h"
#include "clipblts.h"
#include "spanlist.h"
#include "toneblt.h"
#include "render.h"
#include "blitcolors.h"
#include "blitcolorh.h"

/* Special self-intersecting reverse-painter's algorithm blit function */
static void intersectclipspan(render_blit_t *rb,
                              register dcoord y , register dcoord xs , dcoord xe)
{
  FORM *intersectingform ;
  render_blit_t rb_intersect = *rb ;
  dcoord x_sep_position = rb->x_sep_position ;

  GET_BLIT_DATA(rb->blits, INTERSECT_BLIT_INDEX, intersectingform) ;

  rb_intersect.outputform = intersectingform ;
  rb_intersect.depth_shift = 0; /* intersectingform is 1-bit */
  intersectingform->hoff = rb->outputform->hoff ;
  rb_intersect.ylineaddr = BLIT_ADDRESS(intersectingform->addr,
                                        intersectingform->l * (y - intersectingform->hoff - rb->y_sep_position));

  HQASSERT(rb_intersect.ylineaddr >= intersectingform->addr &&
           rb_intersect.ylineaddr < BLIT_ADDRESS(intersectingform->addr, intersectingform->size),
           "Intersecting form line address outside of form") ;

  /* Filter the span through the intersectingform, using next_span to call
     down the blit stack to the underlying spans. Then knockout the span just
     painted from intersectingform to avoid blitting over the same area
     again. */
  if ( intersectingform->type == FORMTYPE_BANDRLEENCODED ) {
    spanlist_t *spans = (spanlist_t *)rb_intersect.ylineaddr ;

    spanlist_intersecting(spans, next_span, NULL, rb,
                          y, xs, xe, x_sep_position) ;

    if ( !spanlist_delete(spans, xs + x_sep_position, xe + x_sep_position) ) {
      if ( !spanlist_merge(spans) ) {
        const render_state_t *p_rs = rb->p_ri->p_rs ;
        /* Convert to bitmap if failed to reduce space used. Abuse halftonebase
           for the temporary workspace because we may be in a clip line, and
           clippingbase may be in use. */
        bandrleencoded_to_bandbitmap(intersectingform,
                                     p_rs->forms->halftonebase,
                                     p_rs->cs.bandlimits.x1,
                                     p_rs->cs.bandlimits.x2) ;
      }
    }
  } else {
    HQASSERT(intersectingform->type == FORMTYPE_BANDBITMAP,
             "Intersecting form is neither RLE spanlist nor bitmap") ;
    bitmap_intersecting(rb_intersect.ylineaddr, next_span, NULL, rb,
                        y, xs, xe, x_sep_position) ;
    bitfill0(&rb_intersect, y, xs, xe) ;
  }
}

/* Special self-intersection clip functions for self-intersecting shfills.
   These blit the block or span normally, then knock the span or block out of
   the clip mask so it won't get overwritten. */
static void intersectclipblock(render_blit_t *rb,
                               dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  FORM *intersectingform ;
  render_blit_t rb_copy = *rb ;
  render_blit_t rb_intersect = *rb ;
  dcoord x_sep_position = rb_intersect.x_sep_position ;

  GET_BLIT_DATA(rb->blits, INTERSECT_BLIT_INDEX, intersectingform) ;

  rb_intersect.outputform = intersectingform ;
  rb_intersect.depth_shift = 0; /* intersectingform is 1-bit */
  intersectingform->hoff = rb->outputform->hoff ;
  rb_intersect.ylineaddr = BLIT_ADDRESS(intersectingform->addr,
                                        intersectingform->l * (ys - intersectingform->hoff - rb->y_sep_position));

  HQASSERT(rb_intersect.ylineaddr >= intersectingform->addr &&
           rb_intersect.ylineaddr < BLIT_ADDRESS(intersectingform->addr, intersectingform->size),
           "Intersecting form line address outside of form") ;

  /* Don't call intersectclipspan when filtering */
  BLOCK_USE_NEXT_BLITS(rb_copy.blits) ;

  if ( intersectingform->type == FORMTYPE_BANDRLEENCODED ) {
    /* We may bail out of using spanlists for the intersectingform if they
       become too complex. Do as much as we can using spanlists, if we
       bail out do the remainder as bitmaps. */
    while ( ys <= ye ) {
      spanlist_t *spans = (spanlist_t *)rb_intersect.ylineaddr ;

      /* Filter the span through the intersectingform, using next_span to call
         down the blit stack to the underlying spans. */
      spanlist_intersecting(spans, next_span, NULL, &rb_copy,
                            ys, xs, xe, x_sep_position) ;

      /* Increment line and Y now, they aren't used by the rest of the loop.
         If the band is converted to bitmap, we want to note that this line
         has already been done. */
      rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, rb_copy.outputform->l) ;
      rb_copy.ymaskaddr = BLIT_ADDRESS(rb_copy.ymaskaddr, rb_copy.clipform->l) ;
      rb_intersect.ylineaddr = BLIT_ADDRESS(rb_intersect.ylineaddr, intersectingform->l) ;
      ++ys ;

      /* Knock out the span from the interseting form. */
      if ( !spanlist_delete(spans, xs + x_sep_position, xe + x_sep_position) ) {
        if ( !spanlist_merge(spans) ) {
          const render_state_t *p_rs = rb_copy.p_ri->p_rs ;
          /* Convert to bitmap if failed to reduce space used. Abuse
             halftonebase for the temporary workspace because we may be in a
             clip line, and clippingbase may be in use. */
          bandrleencoded_to_bandbitmap(intersectingform,
                                       p_rs->forms->halftonebase,
                                       p_rs->cs.bandlimits.x1,
                                       p_rs->cs.bandlimits.x2) ;
          /* The rest of the band must be implemented as bitmap */
          break ;
        }
      }
    }
  }

  while ( ys <= ye ) {
    HQASSERT(intersectingform->type == FORMTYPE_BANDBITMAP,
             "Intersecting form is neither RLE spanlist nor bitmap") ;

    /* Filter the span through the intersectingform, using next_span to call
       down the blit stack to the underlying spans. */
    bitmap_intersecting(rb_intersect.ylineaddr, next_span, NULL, &rb_copy,
                        ys, xs, xe, x_sep_position) ;

    /* Knock out the span from the intersecting form. */
    bitfill0(&rb_intersect, ys, xs, xe) ;

    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, rb_copy.outputform->l) ;
    rb_copy.ymaskaddr = BLIT_ADDRESS(rb_copy.ymaskaddr, rb_copy.clipform->l) ;
    rb_intersect.ylineaddr = BLIT_ADDRESS(rb_intersect.ylineaddr, intersectingform->l) ;
    ++ys ;
  }
}

void surface_intersect_builtin(surface_t *surface)
{
  surface->intersectblits.spanfn = intersectclipspan ;
  surface->intersectblits.blockfn = intersectclipblock ;
  surface->intersectblits.charfn = charbltn ;
  surface->intersectblits.imagefn = imagebltn ;
}

/* Log stripped */
