/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:toneblt1.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for 1-bit surfaces.
 *
 * This is slightly different from the mask surface in that the
 * colour/quantisation state is taken into account.
 */

#include "core.h"
#include "surface.h"
#include "objnamer.h"
#include "render.h"    /* x_sep_position */
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "toneblt.h"
#include "gu_chan.h"
#include "imexpand.h"
#include "imageo.h"
#include "imgblts.h"
#include "builtin.h"

/** Self-modifying blits for 1-bit halftone span fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void bitfilltone1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for span") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No tone1 color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  default:
    HQFAIL("Should only be one color channel for tone1") ;
    /*@fallthrough@*/
  case blit_quantise_min:
    slice = &blitslice1[rb->clipmode] ; /* Black */
    break ;
  case blit_quantise_max:
    slice = &blitslice0[rb->clipmode] ; /* White */
    break ;
  }

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->spanfn)(rb, y, xs, xe) ;
}


/** Self-modifying blits for 1-bit tone block fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void blkfilltone1(render_blit_t *rb,
                         dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for block") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No tone1 color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  default:
    HQFAIL("Should only be one color channel for tone1") ;
    /*@fallthrough@*/
  case blit_quantise_min:
    slice = &blitslice1[rb->clipmode] ; /* Black */
    break ;
  case blit_quantise_max:
    slice = &blitslice0[rb->clipmode] ; /* White */
    break ;
  }

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->blockfn)(rb, ys, ye, xs, xe) ;
}

/** Self-modifying blits for 1-bit tone. This works out what the appropriate
    blit to call is, calls it, and also installs it in place of the current
    blit. */
static void charblttone1(render_blit_t *rb,
                         FORM *formptr, dcoord x, dcoord y)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for span") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No tone1 color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  default:
    HQFAIL("Should only be one color channel for tone 1") ;
    /*@fallthrough@*/
  case blit_quantise_min:
    slice = &blitslice1[rb->clipmode] ; /* Black */
    break ;
  case blit_quantise_max:
    slice = &blitslice0[rb->clipmode] ; /* White */
    break ;
  }

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->charfn)(rb, formptr, x, y) ;
}

static void imageblttone1(render_blit_t *rb, imgblt_params_t *params,
                          imgblt_callback_fn *callback,
                          Bool *result)
{
  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  HQASSERT(DOING_BASE_BLIT_ONLY(rb->blits),
           "Tone 1 image function called after other blits") ;
  HQASSERT(rb->p_ri->pattern_state == PATTERN_OFF,
           "Tone 1 image function called when patterning") ;
  HQASSERT(rb->depth_shift == 0,
           "Tone 1 image function should be 1-bit only") ;

  if ( params->type == IM_BLIT_MASK ) {
    /* Strictly black and white masks can use black/white optimisations. */
    switch ( blit_quantise_state(rb->color) ) {
    default:
      HQFAIL("Should only be one color channel for tone 1") ;
      /*@fallthrough@*/
    case blit_quantise_min:
      imageblt1(rb, params, callback, result) ;
      return ;
    case blit_quantise_max:
      imageblt0(rb, params, callback, result) ;
      return ;
    }
  } else if ( params->type == IM_BLIT_IMAGE &&
              /* All of the optimisations here are for orthogonal images. */
              params->orthogonal && params->one_color_channel ) {
    /* Can we do 1:1 optimisations? */
    if ( (theIOptimize(params->image) & IMAGE_OPTIMISE_1TO1) != 0 &&
         !params->on_the_fly &&
         im_expand1bit(rb->color, params->image->ime,
                       params->expanded_to_plane, params->expanded_comps,
                       params->blit_to_expanded) ) {
      imageblt1(rb, params, callback, result) ;
      return ;
    }
  }

  imagebltn(rb, params, callback, result) ;
}

/**
 * Set all the mask pixels in a given form to a given value
 */
static void areahalf1(render_blit_t *rb,  FORM *formptr)
{
  blit_color_t *color = rb->color ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for area") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should not have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  case blit_quantise_min:
    area1fill(formptr) ; /* Black */
    return ;
  default:
    HQFAIL("Should only be one color channel for halftoning") ;
    /*@fallthrough@*/
  case blit_quantise_max:
    area0fill(formptr) ; /* White */
    return ;
  }
}

/** Render preparation function for mask just quantises current color. */
static surface_prepare_t render_prepare_1(surface_handle_t handle,
                                          render_info_t *p_ri)
{
  UNUSED_PARAM(surface_handle_t, handle) ;

  HQASSERT(p_ri, "No render info") ;
  blit_color_quantise(p_ri->rb.color) ;
  p_ri->rb.depth_shift = 0 ;

  return SURFACE_PREPARE_OK ;
}

static void tone1_color_pack(blit_color_t *color)
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

/** Expanding the color is a no-op because the pack routine already did
    it. */
static void tone1_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

static void tone1_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  map->pack_quantised_color = tone1_color_pack ;
  map->expand_packed_color = tone1_color_expand ;
}

/** The tone 1-bit surface. */
static surface_t tone1 = SURFACE_INIT ;

void init_toneblt_1(void)
{
  /* Base blits */
  tone1.baseblits[BLT_CLP_NONE].spanfn =
    tone1.baseblits[BLT_CLP_RECT].spanfn =
    tone1.baseblits[BLT_CLP_COMPLEX].spanfn = bitfilltone1 ;

  tone1.baseblits[BLT_CLP_NONE].blockfn =
    tone1.baseblits[BLT_CLP_RECT].blockfn =
  tone1.baseblits[BLT_CLP_COMPLEX].blockfn = blkfilltone1 ;

  tone1.baseblits[BLT_CLP_NONE].charfn =
    tone1.baseblits[BLT_CLP_RECT].charfn =
    tone1.baseblits[BLT_CLP_COMPLEX].charfn = charblttone1 ;

  tone1.baseblits[BLT_CLP_NONE].imagefn =
    tone1.baseblits[BLT_CLP_RECT].imagefn =
    tone1.baseblits[BLT_CLP_COMPLEX].imagefn = imageblttone1 ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&tone1) ;
  surface_pattern_builtin(&tone1) ;
  surface_gouraud_builtin_tone(&tone1) ;

  /* Tone 1 is only used for MHT masks, so has no OM side blits, no max
     blits, no ROP blits, no PCL pattern blits, no surface set, no
     registration. */

  tone1.areafill = areahalf1 ;
  tone1.prepare = render_prepare_1 ;
  tone1.blit_colormap_optimise = tone1_blitmap_optimise ;
  tone1.backdropblit = backdropblt_builtin;

  tone1.n_rollover = 0 ; /* no rollovers */
  tone1.screened = FALSE ;
  tone1.render_order = SURFACE_ORDER_IMAGEROW|SURFACE_ORDER_COPYDOT;

  builtin_clip_1_surface(&tone1, NULL) ;
}

void surface_set_mht_mask_builtin(surface_set_t *set, const surface_t *indexed[])
{
  UNUSED_PARAM(surface_set_t *, set) ;
  HQASSERT(set, "No surface set to initialise") ;
  HQASSERT(set->indexed, "No surface array in set") ;
  HQASSERT(set->indexed == indexed, "Surface array inconsistent") ;
  HQASSERT(set->n_indexed > SURFACE_MHT_MASK, "Surface array too small") ;
  HQASSERT(set->indexed[SURFACE_OUTPUT] != NULL,
           "No output surface defined for surface set") ;
  HQASSERT(set->indexed[SURFACE_MHT_MASK] == NULL ||
           set->indexed[SURFACE_MHT_MASK] == &tone1,
           "MHT mask surface already initialised") ;
  indexed[SURFACE_MHT_MASK] = &tone1 ;
}


/* Log stripped */
