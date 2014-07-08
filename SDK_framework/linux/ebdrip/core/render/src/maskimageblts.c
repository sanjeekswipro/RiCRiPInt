/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:maskimageblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bit blitting functions.
 */

#include "core.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "render.h"   /* render_blit_t */
#include "caching.h"
#include "surface.h"
#include "spanlist.h"
#include "toneblt.h" /* charbltn */
#include "clipblts.h"
#include "builtin.h"

static void bitfillmask(render_blit_t *rb,
                        register dcoord y , register dcoord xs , dcoord xe )
{
  spanlist_t *outputptr ;
  FORM *outputform ;

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillmask" ) ;

  outputform = rb->outputform ;

  if ( theFormT(*outputform) != FORMTYPE_BANDRLEENCODED ) {
    bitfill1(rb, y , xs , xe ) ;
    return ;
  }

  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  outputptr = (spanlist_t *)rb->ylineaddr ;
  if ( !spanlist_insert(outputptr, xs, xe) ) {
    /* Spanlist needs emptying. Try to merge first. */
    if ( !spanlist_merge(outputptr) ) {
      const dbbox_t *clip = &rb->p_ri->clip ;
      /* Convert to bitmap if failed to reduce space used. Abuse halftonebase
         for the temporary workspace because we may be in a clip line, and
         clippingbase may be in use. */
      bandrleencoded_to_bandbitmap(outputform,
                                   rb->p_ri->p_rs->forms->halftonebase,
                                   clip->x1 + rb->x_sep_position,
                                   clip->x2 + rb->x_sep_position) ;
    }
  }
}

static void bitclipmask(render_blit_t *rb,
                        register dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipmask" ) ;

  if ( theFormT(*rb->clipform) == FORMTYPE_BANDRLEENCODED ) {
    BITBLT_FUNCTION fillfunc ;

    if ( theFormT(*rb->outputform) == FORMTYPE_BANDRLEENCODED )
      fillfunc = bitfillmask ;
    else
      fillfunc = bitfill1 ;

    spanlist_intersecting((spanlist_t *)rb->ymaskaddr, fillfunc, NULL,
                          rb, y, xs, xe, rb->x_sep_position) ;
  } else {
    blit_slice_t *slice ;

    HQASSERT(theFormT(*rb->clipform) == FORMTYPE_BANDBITMAP,
             "clipform not RLE or bitmap encoded") ;

    if ( theFormT(*rb->outputform) == FORMTYPE_BANDRLEENCODED ) {
      const dbbox_t *clip = &rb->p_ri->clip ;
      /* Convert the output to bitmap if the clipping form is bitmap. We only
         need to convert the area inside the clip bounds. Abuse halftonebase
         for the temporary workspace because we may be in a clip line, and
         clippingbase may be in use. */
      bandrleencoded_to_bandbitmap(rb->outputform,
                                   rb->p_ri->p_rs->forms->halftonebase,
                                   clip->x1 + rb->x_sep_position,
                                   clip->x2 + rb->x_sep_position) ;
    }

    HQASSERT(theFormT(*rb->outputform) == FORMTYPE_BANDBITMAP,
             "outputform not bitmap encoded") ;

    /* Replace this blit in the stack with the appropriate specialised
       function. */
    slice = &blitslice1[rb->clipmode] ;
    SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

    (*slice->spanfn)(rb, y, xs, xe) ;
  }
}

/* ---------------------------------------------------------------------- */

static void blkfillmask(render_blit_t *rb,
                        register dcoord ys, register dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  int32 wupdate ;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillmask" ) ;

  wupdate = theFormL(*rb_copy.outputform) ;

  do {
    bitfillmask(&rb_copy, ys , xs , xe ) ;
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
  } while ( ++ys <= ye ) ;
}

static void blkclipmask(render_blit_t *rb,
                        register dcoord ys, register dcoord ye, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkclipmask" ) ;

  if ( theFormT(*rb->clipform) == FORMTYPE_BANDRLEENCODED ) {
    render_blit_t rb_copy = *rb ;
    BITBLT_FUNCTION fillfunc ;
    int32 wupdate = theFormL(*rb_copy.outputform) ;
    int32 wclipupdate = theFormL(*rb_copy.clipform) ;

    if ( theFormT(*rb_copy.outputform) == FORMTYPE_BANDRLEENCODED )
      fillfunc = bitfillmask ;
    else
      fillfunc = bitfill1 ;

    do {
      spanlist_intersecting((spanlist_t *)rb_copy.ymaskaddr, fillfunc, NULL,
                            &rb_copy, ys, xs, xe, rb_copy.x_sep_position) ;
      rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
      rb_copy.ymaskaddr = BLIT_ADDRESS(rb_copy.ymaskaddr, wclipupdate);
    } while ( ++ys <= ye ) ;
  } else {
    blit_slice_t *slice ;

    HQASSERT(theFormT(*rb->clipform) == FORMTYPE_BANDBITMAP,
            "clipform not RLE or bitmap encoded") ;

    if ( theFormT(*rb->outputform) == FORMTYPE_BANDRLEENCODED ) {
      const dbbox_t *clip = &rb->p_ri->clip ;
      /* Convert the output to bitmap if the clipping form is bitmap. We only
         need to convert the area inside the clip bounds. Abuse halftonebase
         for the temporary workspace because we may be in a clip line, and
         clippingbase may be in use. */
      bandrleencoded_to_bandbitmap(rb->outputform,
                                   rb->p_ri->p_rs->forms->halftonebase,
                                   clip->x1 + rb->x_sep_position,
                                   clip->x2 + rb->x_sep_position) ;
    }

    HQASSERT(theFormT(*rb->outputform) == FORMTYPE_BANDBITMAP,
            "outputform not bitmap encoded") ;

    /* Replace this blit in the stack with the appropriate specialised
       function. */
    slice = &blitslice1[rb->clipmode] ;
    SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

    (*slice->blockfn)(rb, ys, ye, xs, xe) ;
  }
}

/* ---------------------------------------------------------------------- */

static void init_mask_image(void)
{
  blitslice0[BLT_CLP_NONE].imagefn =
    blitslice0[BLT_CLP_RECT].imagefn =
    blitslice0[BLT_CLP_COMPLEX].imagefn = imageblt0 ;

  blitslice1[BLT_CLP_NONE].imagefn =
    blitslice1[BLT_CLP_RECT].imagefn =
    blitslice1[BLT_CLP_COMPLEX].imagefn = imageblt1 ;

  nbit_blit_slice0[BLT_CLP_NONE].imagefn =
    nbit_blit_slice0[BLT_CLP_RECT].imagefn =
    nbit_blit_slice0[BLT_CLP_COMPLEX].imagefn = imagebltn;

  nbit_blit_slice1[BLT_CLP_NONE].imagefn =
    nbit_blit_slice1[BLT_CLP_RECT].imagefn =
    nbit_blit_slice1[BLT_CLP_COMPLEX].imagefn = imagebltn;
}

/* ---------------------------------------------------------------------- */

blitclip_slice_t maskimageslice = {
  { /* Rect clipped */
    bitfillmask, blkfillmask, invalid_snfill, charbltn, imageblt1
  },
  { /* Complex clipped */
    bitclipmask, blkclipmask, invalid_snfill, charbltn, imageblt1
  },
  { /* Unclipped */
    bitfillmask, blkfillmask, invalid_snfill, charbltn, imageblt1
  }
} ;

static void areamask(render_blit_t *rb, FORM *formptr)
{
  UNUSED_PARAM(render_blit_t *, rb) ;
  area0fill(formptr) ;
}

/** Render preparation function for mask surfaces. */
static surface_prepare_t render_prepare_mask(surface_handle_t handle,
                                             render_info_t *p_ri)
{
  UNUSED_PARAM(surface_handle_t, handle) ;

  HQASSERT(p_ri, "No render info") ;
  VERIFY_OBJECT(p_ri->rb.color, BLIT_COLOR_NAME) ;
  HQASSERT(p_ri->rb.color->valid & blit_color_unpacked, "Blit color should be unpacked") ;
  HQASSERT(p_ri->rb.color->valid & blit_color_quantised, "Blit color should be pre-quantised") ;
  HQASSERT(p_ri->rb.color->valid & blit_color_packed, "Blit color should be pre-packed") ;
  HQASSERT(p_ri->rb.color->valid & blit_color_expanded, "Blit color should be pre-expanded") ;
  p_ri->rb.depth_shift = 0 ;

  return SURFACE_PREPARE_OK ;
}

/* ---------------------------------------------------------------------- */

surface_t patternshape_surface = SURFACE_INIT ;

surface_t mask_surface = SURFACE_INIT ;

surface_t mask_bitmap_surface = SURFACE_INIT ;

void init_mask_1(void)
{
  /* Base blits, max blits */
  init_mask_span() ;
  init_mask_block() ;
  init_mask_char() ;
  init_mask_image() ;

  /***********************************************************************/
  /* Normal mask surface can store the output as either spanlists or bitmap. */

  mask_surface.baseblits[BLT_CLP_NONE].spanfn =
    mask_surface.baseblits[BLT_CLP_RECT].spanfn = bitfillmask ;
  mask_surface.baseblits[BLT_CLP_COMPLEX].spanfn = bitclipmask ;

  mask_surface.baseblits[BLT_CLP_NONE].blockfn =
    mask_surface.baseblits[BLT_CLP_RECT].blockfn = blkfillmask ;
  mask_surface.baseblits[BLT_CLP_COMPLEX].blockfn = blkclipmask ;

  mask_surface.baseblits[BLT_CLP_NONE].charfn =
    mask_surface.baseblits[BLT_CLP_RECT].charfn =
    mask_surface.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  mask_surface.baseblits[BLT_CLP_NONE].imagefn =
    mask_surface.baseblits[BLT_CLP_RECT].imagefn =
    mask_surface.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Pattern replicators are required for patternshape generation and for
     modular halftone mask generation. */
  surface_pattern_builtin(&mask_surface) ;

  /* No ROPs, self-intersection, no gouraud, no imageclip for
     masks */

  mask_surface.areafill = areamask ;
  mask_surface.prepare = render_prepare_mask ;

  mask_surface.n_rollover = 0 ;   /* No rollovers */
  mask_surface.screened = FALSE ; /* There are no intermediate values */

  builtin_clip_N_surface(&mask_surface, NULL) ;

  /***********************************************************************/
  /* Bitmap mask surface can only store the output as bitmap. */

  mask_bitmap_surface.baseblits[BLT_CLP_NONE] = blitslice1[BLT_CLP_NONE] ;
  mask_bitmap_surface.baseblits[BLT_CLP_RECT] = blitslice1[BLT_CLP_RECT] ;
  mask_bitmap_surface.baseblits[BLT_CLP_COMPLEX] = blitslice1[BLT_CLP_COMPLEX] ;

  /* Pattern replicators are required for patternshape generation and for
     modular halftone mask generation. */
  surface_pattern_builtin(&mask_bitmap_surface) ;

  /* No ROPs, self-intersection, no gouraud, no imageclip for
     masks */

  mask_bitmap_surface.areafill = areamask ;
  mask_bitmap_surface.prepare = render_prepare_mask ;

  mask_bitmap_surface.n_rollover = 0 ;   /* No rollovers */
  mask_bitmap_surface.screened = FALSE ; /* There are no intermediate values */

  builtin_clip_1_surface(&mask_bitmap_surface, NULL) ;

  /***********************************************************************/
  /* Pattern shapes are stored using either spanlists or masks. */

  /* Currently, there is only one pattern shape surface, and it's similar
     to the mask surface. */
  patternshape_surface = mask_surface ;
}

/* Log stripped */
