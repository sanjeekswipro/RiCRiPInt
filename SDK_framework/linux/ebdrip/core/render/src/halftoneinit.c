/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:halftoneinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone surface definitions.
 */

#include "core.h"
#include "surface.h"
#include "bitblts.h"
#include "bitblth.h" /* blkfillspan */
#include "toneblt.h" /* charbltn */
#include "blttables.h"
#include "halftoneblts.h"
#include "halftoneblks.h"
#include "halftonechar.h"
#include "halftoneimg.h"
#include "render.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "pcl5Blit.h"
#include "gu_chan.h"
#include "builtin.h"
#include "renderfn.h"
/* Not keen on exposing all of these here, they should be accessed through
   getter functions on the render_info_t: */
#include "pclAttribTypes.h"
#include "pclAttrib.h"
#include "pclPatternBlit.h"
#include "display.h"

struct surface_band_t {
  dbbox_t xor_bbox ; /**< BBox covered by DTx, DSx, Dn. */
  const blit_slice_t *slice0 ; /** Depth-specific slice to use to clear bits. */
  const blit_slice_t *slice1 ; /** Depth-specific slice to use to set bits. */
  Bool flip ;        /**< Should this channel be flipped for XOR idioms? */

  OBJECT_NAME_MEMBER
} ;

#ifdef ASSERT_BUILD
Bool debug_xor_tracking = FALSE ;
#endif

#define HALFTONE_TRACKER_NAME "Halftone tracker"

/** Band localiser for halftone. We use this function to track a small amount
    of ROP state for each band. */
static Bool halftone_band_render(surface_handle_t *handle,
                                 render_band_callback_fn *callback,
                                 render_band_callback_t *data,
                                 Bool multibit)
{
  surface_band_t tracker = { 0 } ;
  Bool result ;

  if ( multibit ) {
    tracker.slice0 = &nbit_blit_slice0[0] ;
    tracker.slice1 = &nbit_blit_slice1[0] ;
  } else {
    tracker.slice0 = &blitslice0[0] ;
    tracker.slice1 = &blitslice1[0] ;
  }
  NAME_OBJECT(&tracker, HALFTONE_TRACKER_NAME) ;
  handle->band = &tracker ;

  /* Actually do the band render callback */
  result = (*callback)(data) ;

  UNNAME_OBJECT(&tracker) ;

  return result ;
}

static Bool halftone_band_assign(surface_handle_t handle, render_state_t *rs)
{
  surface_band_t *tracker = handle.band ;
  const GUCR_COLORANT_INFO *info ;

  VERIFY_OBJECT(tracker, HALFTONE_TRACKER_NAME) ;

  bbox_clear(&tracker->xor_bbox) ;

  /* When compensating for subtractive rendering in XOR idioms, only flip
     the black channel. */
  if ( gucr_colorantDescription(rs->cs.hc, &info) ) {
    tracker->flip = (info->colorantIndex == rs->page->deviceBlackIndex) ;
  }

  return TRUE ;
}

/* PCL drivers often use XOR/ROP0/XOR idioms to clip an image to a shape.
   When these idioms are not detected early, we render them using direct ROPs
   and the bitfillXOR layer.
   When we're doing that, we have to flip ROP 0/255 to render all bits
   clear/set in halftone (subtractive) space, to keep the XOR region from
   inverting. Unfortunately, some of the time the driver will render part of
   the ROP 0 shapes outside of the bbox of the XOR objects. In this case, this
   ROP slice can be used to partition the block or span into sections inside
   and outside the XOR region, and only flip those parts inside. */
static void bitrop01(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  surface_band_t *tracker ;
  dbbox_t *bbox ;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, tracker) ;
  VERIFY_OBJECT(tracker, HALFTONE_TRACKER_NAME) ;
  bbox = &tracker->xor_bbox ;

  if ( y >= bbox->y1 && y <= bbox->y2 && xe >= bbox->x1 && xs <= bbox->x2 ) {
    if ( xs < bbox->x1 ) {
      tracker->slice1[rb->clipmode].spanfn(rb, y, xs, bbox->x1 - 1) ;
      xs = bbox->x1 ;
    }
    if ( xe > bbox->x2 ) {
      tracker->slice1[rb->clipmode].spanfn(rb, y, bbox->x2 + 1, xe) ;
      xe = bbox->x2 ;
    }
    tracker->slice0[rb->clipmode].spanfn(rb, y, xs, xe) ;
  } else {
    tracker->slice1[rb->clipmode].spanfn(rb, y, xs, xe) ;
  }
}

static void blkrop01(render_blit_t *rb,
                     dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  surface_band_t *tracker ;
  dbbox_t *bbox ;
  dcoord ey ;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, tracker) ;
  VERIFY_OBJECT(tracker, HALFTONE_TRACKER_NAME) ;
  bbox = &tracker->xor_bbox ;

  if ( ys < bbox->y1 ) { /* Horizontal slice above XOR region */
    INLINE_MIN32(ey, ye, bbox->y1 - 1) ;
    tracker->slice1[rb->clipmode].blockfn(rb, ys, ey, xs, xe) ;
    ys = bbox->y1 ;
  }

  if ( ye >= bbox->y1 ) {
    if ( ys <= bbox->y2 ) {
      /* Do the slices left, inside, and right of the ROP region. */
      dcoord ex, x1 = xs ;
      INLINE_MIN32(ey, ye, bbox->y2) ;

      if ( x1 < bbox->x1 ) { /* Vertical slice left of the XOR region */
        INLINE_MIN32(ex, xe, bbox->x1 - 1) ;
        tracker->slice1[rb->clipmode].blockfn(rb, ys, ey, x1, ex) ;
        x1 = bbox->x1 ;
      }

      if ( xe >= bbox->x1 ) {
        if ( x1 <= bbox->x2 ) { /* Inside the XOR region */
          INLINE_MIN32(ex, xe, bbox->x2) ;
          tracker->slice0[rb->clipmode].blockfn(rb, ys, ey, x1, ex) ;
          x1 = bbox->x2 + 1 ;
        }

        if ( xe > bbox->x2 ) { /* Vertical slice right of the XOR region */
          tracker->slice1[rb->clipmode].blockfn(rb, ys, ey, x1, xe) ;
        }
      }

      ys = bbox->y2 + 1 ;
    }

    if ( ye > bbox->y2 ) { /* Horizontal slice below XOR region */
      tracker->slice1[rb->clipmode].blockfn(rb, ys, ye, xs, xe) ;
    }
  }
}

static blitclip_slice_t rop01_overlap_slice = {
  { bitrop01, blkrop01, invalid_snfill, charbltn, imagebltht },
  { bitrop01, blkrop01, invalid_snfill, charbltn, imagebltht },
  { bitrop01, blkrop01, invalid_snfill, charbltn, imagebltht },
} ;

static void bitrop10(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  surface_band_t *tracker ;
  dbbox_t *bbox ;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, tracker) ;
  VERIFY_OBJECT(tracker, HALFTONE_TRACKER_NAME) ;
  bbox = &tracker->xor_bbox ;

  if ( y >= bbox->y1 && y <= bbox->y2 && xe >= bbox->x1 && xs <= bbox->x2 ) {
    if ( xs < bbox->x1 ) {
      tracker->slice0[rb->clipmode].spanfn(rb, y, xs, bbox->x1 - 1) ;
      xs = bbox->x1 ;
    }
    if ( xe > bbox->x2 ) {
      tracker->slice0[rb->clipmode].spanfn(rb, y, bbox->x2 + 1, xe) ;
      xe = bbox->x2 ;
    }
    tracker->slice1[rb->clipmode].spanfn(rb, y, xs, xe) ;
  } else {
    tracker->slice0[rb->clipmode].spanfn(rb, y, xs, xe) ;
  }
}

static void blkrop10(render_blit_t *rb,
                     dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  surface_band_t *tracker ;
  dbbox_t *bbox ;
  dcoord ey ;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, tracker) ;
  VERIFY_OBJECT(tracker, HALFTONE_TRACKER_NAME) ;
  bbox = &tracker->xor_bbox ;

  if ( ys < bbox->y1 ) { /* Horizontal slice above XOR region */
    INLINE_MIN32(ey, ye, bbox->y1 - 1) ;
    tracker->slice0[rb->clipmode].blockfn(rb, ys, ey, xs, xe) ;
    ys = bbox->y1 ;
  }

  if ( ye >= bbox->y1 ) {
    if ( ys <= bbox->y2 ) {
      /* Do the slices left, inside, and right of the ROP region. */
      dcoord ex, x1 = xs ;
      INLINE_MIN32(ey, ye, bbox->y2) ;

      if ( x1 < bbox->x1 ) { /* Vertical slice left of the XOR region */
        INLINE_MIN32(ex, xe, bbox->x1 - 1) ;
        tracker->slice0[rb->clipmode].blockfn(rb, ys, ey, x1, ex) ;
        x1 = bbox->x1 ;
      }

      if ( xe >= bbox->x1 ) {
        if ( x1 <= bbox->x2 ) { /* Inside the XOR region */
          INLINE_MIN32(ex, xe, bbox->x2) ;
          tracker->slice1[rb->clipmode].blockfn(rb, ys, ey, x1, ex) ;
          x1 = bbox->x2 + 1 ;
        }

        if ( xe > bbox->x2 ) { /* Vertical slice right of the XOR region */
          tracker->slice0[rb->clipmode].blockfn(rb, ys, ey, x1, xe) ;
        }
      }

      ys = bbox->y2 + 1 ;
    }

    if ( ye > bbox->y2 ) { /* Horizontal slice below XOR region */
      tracker->slice0[rb->clipmode].blockfn(rb, ys, ye, xs, xe) ;
    }
  }
}

static blitclip_slice_t rop10_overlap_slice = {
  { bitrop10, blkrop10, invalid_snfill, charbltn, imagebltht },
  { bitrop10, blkrop10, invalid_snfill, charbltn, imagebltht },
  { bitrop10, blkrop10, invalid_snfill, charbltn, imagebltht },
} ;

static surface_prepare_t pcl_blit_optimise(surface_band_t *tracker,
                                           render_info_t *p_ri,
                                           PclAttrib *attrib)
{
  const DL_STATE *page = p_ri->p_rs->page ;
  /* When doing ROP_DIRECT, compensate for halftone imaging being
     subtractive in XOR/black/XOR idioms:
     Value 0: no compensation
     Value 1: render black insides as bitvalue 0 instead of 1
     Value 2: render XORs inverted as XOR NOT (assumes XORs are paired). */
# define ROP_XOR 2

  VERIFY_OBJECT(tracker, HALFTONE_TRACKER_NAME) ;

  /* By this time, we've checked that it's safe to use any ROPs that are
     presented to this function, so we can assert if we see any that we
     don't implement. See halftone_rop_check() for the filter. */

  if ( page->pcl5eModeEnabled ) {
    uint8 ropmode;

    HQASSERT(p_ri->rb.depth_shift == 0, /* halftone1 only */
             "PCL mono blitter used in invalid setup.");

    /** \todo ajcd 2013-07-03: determine when we can re-use the pattern
        blits, and how to optimise out the PCL5e blit stages that duplicate
        it. */

    /* The PCL5 ROP blit tests use the quantised color state to determine if
       the source color is white. The ROPs are only used for PCL5e, which is
       monochrome, and images are a single channel. */
    if (! pcl5_mono_blitter_required(p_ri))
      return SURFACE_PREPARE_OK ;

    pcl5_ropmode(&p_ri->rb, &ropmode);

    HQASSERT(p_ri->surface->ropblits[ropmode].spanfn != NULL,
             "Required blitter not supported.");
    SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
              &p_ri->surface->ropblits[ropmode],
              &p_ri->surface->ropblits[ropmode],
              &p_ri->surface->ropblits[ropmode]);
    SET_BLIT_DATA(p_ri->rb.blits, ROP_BLIT_INDEX, attrib);
  } else {
    blit_color_t *color = p_ri->rb.color ;
    dbbox_t bbox ;

    switch ( attrib->rop ) {
    case PCL_ROP_BLACK: /* 0x00 : Force black ROP layer */
      bbox_intersection(&p_ri->lobj->bbox, &p_ri->clip, &bbox) ;
      if ( ROP_XOR == 1 && tracker->flip &&
           attrib->xorstate == PCL_XOR_INSIDE &&
           bbox_intersects(&tracker->xor_bbox, &bbox) ) {
        if ( bbox_contains(&tracker->xor_bbox, &bbox) ) {
          SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                    &tracker->slice0[BLT_CLP_NONE],
                    &tracker->slice0[BLT_CLP_RECT],
                    &tracker->slice0[BLT_CLP_COMPLEX]);
        } else {
          SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                    &rop01_overlap_slice[BLT_CLP_NONE],
                    &rop01_overlap_slice[BLT_CLP_RECT],
                    &rop01_overlap_slice[BLT_CLP_COMPLEX]);
          SET_BLIT_DATA(p_ri->rb.blits, ROP_BLIT_INDEX, tracker);
          HQTRACE(debug_xor_tracking,
                  ("Object opcode:%d ROP:%d bbox [%d %d %d %d] exceeds XOR bbox [%d %d %d %d]",
                   p_ri->lobj->opcode, attrib->rop,
                   p_ri->lobj->bbox.x1, p_ri->lobj->bbox.y1,
                   p_ri->lobj->bbox.x2, p_ri->lobj->bbox.y2,
                   tracker->xor_bbox.x1, tracker->xor_bbox.y1,
                   tracker->xor_bbox.x2, tracker->xor_bbox.y2)) ;
        }
      } else if ( ROP_XOR == 2 && !tracker->flip ) {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &tracker->slice0[BLT_CLP_NONE],
                  &tracker->slice0[BLT_CLP_RECT],
                  &tracker->slice0[BLT_CLP_COMPLEX]);
      } else {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &tracker->slice1[BLT_CLP_NONE],
                  &tracker->slice1[BLT_CLP_RECT],
                  &tracker->slice1[BLT_CLP_COMPLEX]);
      }
      break ;
    case PCL_ROP_WHITE: /* 0xff : Force white ROP layer */
      HQASSERT(attrib->xorstate == PCL_XOR_OUTSIDE,
               "ROP 255 inside XOR section") ;
      bbox_intersection(&p_ri->lobj->bbox, &p_ri->clip, &bbox) ;
      if ( ROP_XOR == 1 && tracker->flip &&
           attrib->xorstate == PCL_XOR_INSIDE &&
           bbox_intersects(&tracker->xor_bbox, &bbox) ) {
        if ( bbox_contains(&tracker->xor_bbox, &bbox) ) {
          SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                    &tracker->slice1[BLT_CLP_NONE],
                    &tracker->slice1[BLT_CLP_RECT],
                    &tracker->slice1[BLT_CLP_COMPLEX]);
        } else {
          SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                    &rop10_overlap_slice[BLT_CLP_NONE],
                    &rop10_overlap_slice[BLT_CLP_RECT],
                    &rop10_overlap_slice[BLT_CLP_COMPLEX]);
          SET_BLIT_DATA(p_ri->rb.blits, ROP_BLIT_INDEX, tracker);
          HQTRACE(debug_xor_tracking,
                  ("Object opcode:%d ROP:%d bbox [%d %d %d %d] exceeds XOR bbox [%d %d %d %d]",
                   p_ri->lobj->opcode, attrib->rop,
                   p_ri->lobj->bbox.x1, p_ri->lobj->bbox.y1,
                   p_ri->lobj->bbox.x2, p_ri->lobj->bbox.y2,
                   tracker->xor_bbox.x1, tracker->xor_bbox.y1,
                   tracker->xor_bbox.x2, tracker->xor_bbox.y2)) ;
        }
      } else {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &tracker->slice0[BLT_CLP_NONE],
                  &tracker->slice0[BLT_CLP_RECT],
                  &tracker->slice0[BLT_CLP_COMPLEX]);
      }
      break ;
    case PCL_ROP_D:     /* 0xaa : Ignore blits */
      return SURFACE_PREPARE_SKIP ;
    case PCL_ROP_S:     /* 0xcc */
      break ; /* Straight to underlying blits */
    case PCL_ROP_T:     /* 0xf0 */
      if ( ROP_XOR == 1 && tracker->flip &&
           attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND &&
           attrib->xorstate == PCL_XOR_INSIDE ) {
        /* Assume that everything inside an XOR section is black. */
        bbox_intersection(&p_ri->lobj->bbox, &p_ri->clip, &bbox) ;
        if ( bbox_intersects(&tracker->xor_bbox, &bbox) ) {
          if ( bbox_contains(&tracker->xor_bbox, &bbox) ) {
            color->quantised.qcv[BLIT_SOLE_CHANNEL] =
              color->quantised.htmax[BLIT_SOLE_CHANNEL] ;
            color->quantised.state = blit_quantise_max ;
          } else {
            SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                      &rop01_overlap_slice[BLT_CLP_NONE],
                      &rop01_overlap_slice[BLT_CLP_RECT],
                      &rop01_overlap_slice[BLT_CLP_COMPLEX]);
            SET_BLIT_DATA(p_ri->rb.blits, ROP_BLIT_INDEX, tracker);
            HQTRACE(debug_xor_tracking,
                    ("Object opcode:%d ROP:%d bbox [%d %d %d %d] exceeds XOR bbox [%d %d %d %d]",
                     p_ri->lobj->opcode, attrib->rop,
                     p_ri->lobj->bbox.x1, p_ri->lobj->bbox.y1,
                     p_ri->lobj->bbox.x2, p_ri->lobj->bbox.y2,
                     tracker->xor_bbox.x1, tracker->xor_bbox.y1,
                     tracker->xor_bbox.x2, tracker->xor_bbox.y2)) ;
          }
        }
      }
      break ; /* Straight to underlying blits */
    case PCL_ROP_Dn:    /* 0x55 */
      /* Force quantised color to min, so we render 1s and XOR with them. */
      HQASSERT(attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND,
               "DL color is not foreground") ;
      color->quantised.qcv[BLIT_SOLE_CHANNEL] = 0 ;
      color->quantised.state = blit_quantise_min ;
      /*@fallthrough@*/
    case PCL_ROP_DTx:   /* 0x5a */
    case PCL_ROP_DSx:   /* 0x66 */
      if ( ROP_XOR == 1 ) {
        if ( attrib->xorstate == PCL_XOR_STARTING ) { /* Augment xor bbox. */
          bbox_union(&tracker->xor_bbox, &p_ri->lobj->bbox, &tracker->xor_bbox) ;
        } else if ( attrib->xorstate == PCL_XOR_ENDING ) {
          bbox_clear(&tracker->xor_bbox) ;
        }
      }

      /* If we're XORring with a constant blit 0, skip the object. */
      if ( attrib->rop == PCL_ROP_DTx &&
           attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND &&
           blit_quantise_state(color) == ((ROP_XOR == 2 &&
                                           tracker->flip)
                                          ? blit_quantise_min
                                          : blit_quantise_max) )
        return SURFACE_PREPARE_SKIP ;

      if ( ROP_XOR == 2 && tracker->flip ) {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &ht_xornot_slice[BLT_CLP_NONE],
                  &ht_xornot_slice[BLT_CLP_RECT],
                  &ht_xornot_slice[BLT_CLP_COMPLEX]);
      } else {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &ht_xor_slice[BLT_CLP_NONE],
                  &ht_xor_slice[BLT_CLP_RECT],
                  &ht_xor_slice[BLT_CLP_COMPLEX]);
      }
      break ;
    case PCL_ROP_DTa:   /* 0xa0 : T_BLACK->black, T_WHITE->D, else AND layer */
      if ( attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
        switch ( blit_quantise_state(color) ) {
        case blit_quantise_min:
          /* If we're ANDing with constant blit 1, skip the object. */
          return SURFACE_PREPARE_SKIP ;
        case blit_quantise_max:
          /* If we're ANDing with constant blit 0, render 0. */
          SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                    &tracker->slice0[BLT_CLP_NONE],
                    &tracker->slice0[BLT_CLP_RECT],
                    &tracker->slice0[BLT_CLP_COMPLEX]);
          return SURFACE_PREPARE_OK ;
        }
      }
      /*@fallthrough@*/
    case PCL_ROP_DSa:   /* 0x88  : AND ROP layer */
      if ( tracker->flip ) {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &ht_or_slice[BLT_CLP_NONE],
                  &ht_or_slice[BLT_CLP_RECT],
                  &ht_or_slice[BLT_CLP_COMPLEX]);
      } else {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &ht_and_slice[BLT_CLP_NONE],
                  &ht_and_slice[BLT_CLP_RECT],
                  &ht_and_slice[BLT_CLP_COMPLEX]);
      }
      break ;
    case PCL_ROP_DTo:   /* 0xfa : T_BLACK->D, T_WHITE->white, else OR layer */
      if ( attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
        switch ( blit_quantise_state(color) ) {
        case blit_quantise_min:
          /* If we're ORing with constant blit 1, render 1. */
          SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                    &tracker->slice1[BLT_CLP_NONE],
                    &tracker->slice1[BLT_CLP_RECT],
                    &tracker->slice1[BLT_CLP_COMPLEX]);
          return SURFACE_PREPARE_OK ;
        case blit_quantise_max:
          /* If we're ORring with constant blit 0, skip the object. */
          return SURFACE_PREPARE_SKIP ;
        }
      }
      /*@fallthrough@*/
    case PCL_ROP_DSo:   /* 0xee : OR ROP layer */
      if ( tracker->flip ) {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &ht_and_slice[BLT_CLP_NONE],
                  &ht_and_slice[BLT_CLP_RECT],
                  &ht_and_slice[BLT_CLP_COMPLEX]);
      } else {
        SET_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX,
                  &ht_or_slice[BLT_CLP_NONE],
                  &ht_or_slice[BLT_CLP_RECT],
                  &ht_or_slice[BLT_CLP_COMPLEX]);
      }
      break ;
    default:
      HQFAIL("Unexpected ROP in halftone setup") ;
      break ;
    }
  }

  return SURFACE_PREPARE_OK ;
}

/** Render preparation function for halftone quantises current color. */
static surface_prepare_t render_prepare_halftone_common(surface_handle_t handle,
                                                        render_info_t *p_ri)
{
  PclAttrib *pclAttrib ;

  UNUSED_PARAM(surface_handle_t, handle) ;

  HQASSERT(p_ri, "No render info") ;

  blit_color_quantise(p_ri->rb.color) ;

  if ( (pclAttrib = pcl_attrib_from_ri(p_ri)) != NULL )
    return pcl_blit_optimise(handle.band, p_ri, pclAttrib) ;

  return SURFACE_PREPARE_OK;
}

/** Blit color packing no-op for halftone rasterstyles. This allows the
    blit color pack routine to be called unconditionally. */
static void halftone_color_pack(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif
}

/** We don't need to expand because we're halftoning. */
static void halftone_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

static void halftone_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  map->pack_quantised_color = halftone_color_pack ;
  map->expand_packed_color = halftone_color_expand ;
}

/** Check for objects which do not use foreground, and see if their ROPs
    are reducible to cases we can handle. */
static Bool halftone_rop_check(surface_page_t *handle,
                               const DL_STATE *page,
                               const dl_color_t *color,
                               PclAttrib *attrib)
{
  /* The colour of texture spans. */
  enum { T_BLACK, T_WHITE, T_OTHER } tcolor = T_OTHER ;

  UNUSED_PARAM(surface_page_t *, handle) ;

  if ( page->pcl5eModeEnabled ) { /* PCL 5e implements all blits directly. */
    attrib->backdrop = FALSE ;
    attrib->patternBlit = FALSE ;
    return TRUE ;
  }

  /* We've done very aggressive optimisation of the ROP in getPclAttrib(), to
     take into account the known source and texture. We can do a few more
     cases using bitwise combinations of OR/AND/XOR. We need to know what the
     base blit is setup to do, to determine if we can use the bits written as
     one operand of the OR/AND/XOR layer.

     The DL color the function is handed is in the original PCL virtual
     colorspace. We're called from DL object addition (getPclAttrib(), during
     setg()) and from PCL region marking (which is itself called from
     init_dl_render(), before preconversion). The PCL virtual colorspace is
     usually RGB. This function hasn't been checked for operation in CMYK.

     The halftone blits are always subtractive; filling with 1s puts ink on
     the page. The optimisations available are predicated on what
     preconversion will do to the RGB colours, and then how these will get
     translated to halftone bit patterns.
  */

  /* If there is a pattern we can't simplify using the blit layer, bail out.
     Patterns may still be rendered directly, but multiple colors may be set
     by the pattern blit layer, so we cannot simplify the ROP. */
  if ( attrib->patternColors != PCL_PATTERN_NONE ) {
    if ( !attrib->patternTransparent )
      return TRUE ;

    if ( attrib->patternColors != PCL_PATTERN_BLACK_AND_WHITE &&
         attrib->patternColors != PCL_PATTERN_OTHER_AND_WHITE )
      return TRUE ;
  }

  if (attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND) {
    HQASSERT(!attrib->sourceTransparent, "Shouldn't have transparent source") ;
    HQASSERT((attrib->rop & 0x33) == ((attrib->rop >> 2) & 0x33),
             "ROP doesn't have source optimised") ;

    /* Blit color will be texture. Source color is black, and already
       optimised out of the ROP. Find out the foreground color. Use the
       virtualBlackIndex because this is called during interpretation. */
    if ( dlc_is_white(color) ) {
      tcolor = T_WHITE ;
    } else if ( dlc_is_black(color, page->virtualBlackIndex) ) {
      tcolor = T_BLACK ;
    }

    /* This switch corresponds to the switch in pcl_blit_optimise() that
       selects the underlying optimisation. These are the cases that we can
       handle by letting the DL colour render to the baseblits, optionally
       installing an XOR/OR/AND or other ROP blit layer to capture and modify
       the result. The only cases that can appear are those with the ROP
       source optimised (all combinations of nibbles 0x0, 0x5, 0xa, 0xf). */
    switch ( attrib->rop ) {
    case PCL_ROP_BLACK: /* 0x00 : Force black ROP layer */
    case PCL_ROP_WHITE: /* 0xff : Force white ROP layer */
      /* We can handle these cases directly. */
      break ;
    case 0x05:          /* DTon : T_BLACK->not D, T_WHITE->force black */
    case 0x0a:          /* DTna : T_BLACK->D, T_WHITE->force black */
    case 0x0f:          /* Tn   : T_BLACK->force white, T_WHITE->force black */
    case 0x50:          /* TDna : T_BLACK->force black, T_WHITE->not D */
    case 0x5f:          /* DTan : T_BLACK->force white, T_WHITE->not D */
    case 0xa5:          /* TDxn : T_BLACK->not D, T_WHITE->D */
    case 0xaf:          /* DTno : T_BLACK->force white, T_WHITE->D */
    case 0xf5:          /* TDno : T_BLACK->not D, T_WHITE->force white */
      /* Can't handle these cases because the texture is not black or white.
         These should already be set for transparency. */
      HQASSERT(attrib->patternColors != PCL_PATTERN_NONE ||
               tcolor == T_OTHER,
               "ROP inconsistent with texture") ;
      HQASSERT(attrib->backdrop, "ROP not setup for compositing") ;
      return TRUE ;
    case PCL_ROP_Dn:    /* 0x55 : Force quantised color to 1 and XOR. */
      break ;
    case PCL_ROP_D:     /* 0xaa : no-op */
      HQASSERT(dlc_is_none(color), "Color isn't none for ROP D") ;
      break ;
    case PCL_ROP_DTx:   /* 0x5a : T_BLACK->D, else use XOR layer */
    case PCL_ROP_DTa:   /* 0xa0 : T_BLACK->black, T_WHITE->D, else AND layer */
    case PCL_ROP_T:     /* 0xf0 : Underlying blits. */
    case PCL_ROP_DTo:   /* 0xfa : T_BLACK->D, T_WHITE->white, else OR layer */
      break ;
    default:
      HQFAIL("ROP has source white") ;
      return TRUE ;
    }
  } else {
    /* Foreground in the PCL attrib implies that either the object is an image,
       with source colour coming from the image pixels, or that the object is
       the inverse spans for a char when the source is not transparent.
       Blit color will be the source color. */
    HQASSERT(attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB,
             "No foreground source") ;

    /* We can't handle transparent sources using this route, they need full
       compositing. */
    if ( attrib->sourceTransparent ) {
      HQASSERT(attrib->backdrop, "ROP not setup for compositing") ;
      return TRUE ;
    }

#if 0
    /** \todo ajcd 2013-07-16: Is this right at all? There were some non-char
        other objects that had DL white and FG in attrib. */
    /* Is this white spans from a character? */
    if ( dlc_is_white(color) ) {
      HQASSERT(!attrib->sourceTransparent,
               "Source white transparent object with FG in attrib") ;
      HQASSERT((attrib->rop & 0x33) == ((attrib->rop >> 2) & 0x33),
               "ROP doesn't have source optimised") ;
      /** \todo ajcd 2013-07-10: Optimise source white case */
      scolor = S_WHITE ;
    }
#endif

    if ( foregroundIsWhite(page, attrib) ) {
      HQASSERT((attrib->rop & 0x0f) == ((attrib->rop >> 4) & 0x0f),
               "ROP doesn't have texture optimised") ;
      tcolor = T_WHITE ;
    } else if ( foregroundIsBlack(page, attrib) ) {
      HQASSERT((attrib->rop & 0x0f) == ((attrib->rop >> 4) & 0x0f),
               "ROP doesn't have texture optimised") ;
      tcolor = T_BLACK ;
    }

    /* This switch corresponds to the switch in pcl_blit_optimise() that
       selects the underlying optimisation. These are the cases that we can
       handle by letting the DL colour render to the baseblits, optionally
       installing an XOR/OR/AND or other ROP blit layer to capture and modify
       the result. */
    switch ( attrib->rop ) {
      DEVICESPACEID dspace ;
    case PCL_ROP_BLACK: /* 0x00 : Force black ROP layer */
    case PCL_ROP_WHITE: /* 0xff : Force white ROP layer */
    case PCL_ROP_DSx:   /* 0x66 : XOR ROP layer to invert source */
    case PCL_ROP_S:     /* 0xcc : Filter through pattern layer */
    case PCL_ROP_D:     /* 0xaa : no-op */
      break ;
    case PCL_ROP_DSa:   /* 0x88 : AND ROP layer */
    case PCL_ROP_DSo:   /* 0xee : OR ROP layer */
      /** \todo ajcd 2013-07-29: These don't work properly for multi-channel
          output, because black images don't mask other channels. */
      guc_deviceColorSpace(page->hr, &dspace, NULL) ;
      /** \todo ajcd 2013-07-29: Should this be restricted to
          PCL_PATTERN_NONE? The pattern is part of the texture, which isn't
          used by DSa or DSo, so hopefully that case will be optimised out
          already. */
      if ( dspace == DEVICESPACE_Gray )
        break ;
    case 0x11:          /* DSon : NYI handle with ROP layer */
    case 0x22:          /* DSna : NYI handle with ROP layer */
    case 0x33:          /* Sn   : NYI handle with ROP layer */
    case 0x44:          /* SDna : NYI handle with ROP layer */
    case 0x55:          /* Dn   : NYI handle with ROP layer */
    case 0x77:          /* DSan : NYI handle with ROP layer */
    case 0x99:          /* DSxn : NYI handle with ROP layer */
    case 0xbb:          /* DSno : NYI handle with ROP layer */
    case 0xdd:          /* SDno : NYI handle with ROP layer */
      HQASSERT(attrib->backdrop, "ROP not setup for compositing") ;
      return TRUE ;
    default:
      /* Can't handle other texture colours */
      return TRUE ;
    }
  }

  attrib->backdrop = FALSE ;
  if ( attrib->patternColors != PCL_PATTERN_NONE )
    attrib->patternBlit = TRUE ;

  return TRUE ;
}

#ifdef BLIT_HALFTONE_1

static surface_prepare_t render_prepare_halftone_1(surface_handle_t handle,
                                                   render_info_t *p_ri)
{
  p_ri->rb.depth_shift = 0 ;
  return render_prepare_halftone_common(handle, p_ri) ;
}

static Bool halftone_band_render_1(surface_handle_t *handle,
                                   const render_state_t *rs,
                                   render_band_callback_fn *callback,
                                   render_band_callback_t *data,
                                   surface_bandpass_t *bandpass)
{
  UNUSED_PARAM(surface_bandpass_t *, bandpass) ;
  UNUSED_PARAM(const render_state_t *, rs) ;

  return halftone_band_render(handle, callback, data, FALSE /*multibit*/) ;
}

void init_halftone_1(void)
{
  /* Alternative InterleavingStyles for halftone1 surface; i.e., not pixel
     interleaved. */
  const static sw_datum halftone1_interleave[] = {
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_BAND),
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_FRAME),
  } ;

  /* Pagedevice match for halftone1 surface. */
  const static sw_datum halftone1_dict[] = {
    SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(TRUE),
    SW_DATUM_STRING("InterleavingStyle"),
    SW_DATUM_ARRAY(&halftone1_interleave[0], SW_DATA_ARRAY_LENGTH(halftone1_interleave)),
    SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
    SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(2),
  } ;

  static surface_set_t halftone1_set =
    SURFACE_SET_INIT(SW_DATUM_DICT(&halftone1_dict[0],
                                   SW_DATA_DICT_LENGTH(halftone1_dict))) ;
  static const surface_t *indexed[N_SURFACE_TYPES] ;
  static surface_t halftone1 = SURFACE_INIT ;

  /* Base blits, max blits */
  init_halftone1_span(&halftone1) ;
  init_halftone1_block(&halftone1) ;
  init_halftone1_char(&halftone1) ;
  init_halftone1_image(&halftone1) ;

  /* ROP blits */
  init_halftone1_rops(&halftone1) ;

  /* PCL black and white pattern masking */
  init_pcl_pattern_blit(&halftone1) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&halftone1) ;
  surface_pattern_builtin(&halftone1) ;
  surface_gouraud_builtin_screened(&halftone1) ;

  halftone1.prepare = render_prepare_halftone_1 ;
  halftone1.assign = halftone_band_assign ;
  halftone1.blit_colormap_optimise = halftone_blitmap_optimise ;
  halftone1.n_rollover = 6 ;
  halftone1.screened = TRUE ;
  halftone1.render_order = SURFACE_ORDER_IMAGEROW|SURFACE_ORDER_COPYDOT;

  builtin_clip_1_surface(&halftone1, indexed) ;

  /* The surface we've just completed is part of a set implementing halftone
     output. Add it and all of the associated surfaces to the surface
     array for this set. */
  halftone1_set.indexed = indexed ;
  halftone1_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

  indexed[SURFACE_OUTPUT] = &halftone1 ;
#ifdef BLIT_HALFTONE_MODULAR
  surface_set_mht_mask_builtin(&halftone1_set, indexed) ;
  surface_set_mht_ff_builtin(&halftone1_set, indexed) ;
  surface_set_mht_ff00_builtin(&halftone1_set, indexed) ;
#endif
  surface_set_trap_builtin(&halftone1_set, indexed);
  surface_set_transparency_builtin(&halftone1_set, &halftone1, indexed) ;
  halftone1_set.rop_support = halftone_rop_check ;
  halftone1_set.packing_unit_bits = BLIT_WIDTH_BITS ;
  halftone1_set.band_localiser = &halftone_band_render_1 ;

  /* Now that we've filled in the RLE surface description, hook it up so
     that it can be found. */
  surface_set_register(&halftone1_set) ;
}
#endif


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
static Bool halftone_band_render_n(surface_handle_t *handle,
                                   const render_state_t *rs,
                                   render_band_callback_fn *callback,
                                   render_band_callback_t *data,
                                   surface_bandpass_t *bandpass)
{
  UNUSED_PARAM(surface_bandpass_t *, bandpass) ;
  UNUSED_PARAM(const render_state_t *, rs) ;

  return halftone_band_render(handle, callback, data, TRUE /*multibit*/) ;
}

static void init_halftone_n(surface_set_t *halftonen_set, surface_t *halftonen,
                            const surface_t *indexed[])
{
  /* Base blits, max blits */
  init_halftonen_span(halftonen);
  init_halftonen_block(halftonen);
  init_halftonen_char(halftonen);
  init_halftonen_image(halftonen);

  /* PCL black and white pattern masking */
  init_pcl_pattern_blit(halftonen) ;

  /* Builtins for intersect, pattern, gouraud, and imageclip */
  surface_intersect_builtin(halftonen);
  surface_pattern_builtin(halftonen);
  surface_gouraud_builtin_screened(halftonen);

  halftonen->blit_colormap_optimise = halftone_blitmap_optimise ;
  halftonen->n_rollover = 6;
  halftonen->screened = TRUE;

  builtin_clip_N_surface(halftonen, indexed) ;

  HQASSERT(halftonen_set->indexed == indexed, "Surface array inconsistent") ;
  HQASSERT(halftonen_set->n_indexed > SURFACE_OUTPUT, "Surface array too small") ;
  indexed[SURFACE_OUTPUT] = halftonen ;

#ifdef BLIT_HALFTONE_MODULAR
  surface_set_mht_mask_builtin(halftonen_set, indexed) ;
  surface_set_mht_ff_builtin(halftonen_set, indexed) ;
  surface_set_mht_ff00_builtin(halftonen_set, indexed) ;
#endif
  surface_set_trap_builtin(halftonen_set, indexed);
  surface_set_transparency_builtin(halftonen_set, halftonen, indexed) ;
  halftonen_set->rop_support = halftone_rop_check ;
  halftonen_set->packing_unit_bits = BLIT_WIDTH_BITS ;
  halftonen_set->band_localiser = &halftone_band_render_n ;
}
#endif

#ifdef BLIT_HALFTONE_2
/** Render preparation function for halftone quantises current color. */
static surface_prepare_t render_prepare_halftone_2(surface_handle_t handle,
                                                   render_info_t *p_ri)
{
  p_ri->rb.depth_shift = 1 ;
  return render_prepare_halftone_common(handle, p_ri) ;
}

void init_halftone_2(void)
{
  /* Alternative InterleavingStyles for halftone2 surface; i.e., not pixel
     interleaved. */
  const static sw_datum halftone2_interleave[] = {
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_BAND),
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_FRAME),
  } ;

  /* Pagedevice match for halftone2 surface. */
  const static sw_datum halftone2_dict[] = {
    SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(TRUE),
    SW_DATUM_STRING("InterleavingStyle"),
    SW_DATUM_ARRAY(&halftone2_interleave[0], SW_DATA_ARRAY_LENGTH(halftone2_interleave)),
    SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
    SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(4),
  } ;

  static surface_set_t halftone2_set =
    SURFACE_SET_INIT(SW_DATUM_DICT(&halftone2_dict[0],
                                   SW_DATA_DICT_LENGTH(halftone2_dict))) ;
  static surface_t halftone2 = SURFACE_INIT;
  static const surface_t *indexed2[N_SURFACE_TYPES] ;

  halftone2_set.indexed = indexed2 ;
  halftone2_set.n_indexed = NUM_ARRAY_ITEMS(indexed2) ;
  halftone2.prepare = render_prepare_halftone_2 ;
  halftone2.assign = halftone_band_assign ;

  init_halftone_n(&halftone2_set, &halftone2, indexed2);

  /* Now that we've filled in the surface description, hook it up so
     that it can be found. */
  surface_set_register(&halftone2_set) ;
}
#endif


#ifdef BLIT_HALFTONE_4
static surface_prepare_t render_prepare_halftone_4(surface_handle_t handle,
                                                   render_info_t *p_ri)
{
  p_ri->rb.depth_shift = 2 ;
  return render_prepare_halftone_common(handle, p_ri) ;
}

void init_halftone_4(void)
{
  /* Alternative InterleavingStyles for halftone4 surface; i.e., not pixel
     interleaved. */
  const static sw_datum halftone4_interleave[] = {
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_BAND),
    SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_FRAME),
  } ;

  /* Pagedevice match for halftone4 surface. */
  const static sw_datum halftone4_dict[] = {
    SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(TRUE),
    SW_DATUM_STRING("InterleavingStyle"),
    SW_DATUM_ARRAY(&halftone4_interleave[0], SW_DATA_ARRAY_LENGTH(halftone4_interleave)),
    SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
    SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(16),
  } ;

  static surface_set_t halftone4_set =
    SURFACE_SET_INIT(SW_DATUM_DICT(&halftone4_dict[0],
                                   SW_DATA_DICT_LENGTH(halftone4_dict))) ;
  static surface_t halftone4 = SURFACE_INIT;
  static const surface_t *indexed4[N_SURFACE_TYPES] ;

  halftone4_set.indexed = indexed4 ;
  halftone4_set.n_indexed = NUM_ARRAY_ITEMS(indexed4) ;
  halftone4.prepare = render_prepare_halftone_4 ;
  halftone4.assign = halftone_band_assign ;

  init_halftone_n(&halftone4_set, &halftone4, indexed4);

  /* Now that we've filled in the surface description, hook it up so
     that it can be found. */
  surface_set_register(&halftone4_set) ;
}
#endif

/* Log stripped */
