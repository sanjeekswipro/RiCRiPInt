/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:toneblt32.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for contone output.
 */

#include "core.h"
#include "objnamer.h"

#include "render.h"    /* x_sep_position */
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "imageo.h"
#include "imgblts.h"
#include "toneblt.h"
#include "toneblt32.h"
#include "pclPatternBlit.h"
#include "hqmemset.h"
#include "gu_chan.h"
#include "builtin.h"
#include "control.h"
#include "interrupts.h"

/** Optimised slices for CMYK 8bpp knockout, overprint blits. */
static blitclip_slice_t slices32[2] = {
  BLITCLIP_SLICE_INIT /*knockout*/, BLITCLIP_SLICE_INIT /*overprint*/,
} ;

/* ---------------------------------------------------------------------- */
/* Max/min blit calculations are performed for 4 bytes together in parallel.
   We can do this by negating the CMYK value, and determining if adding it to
   the existing value will carry. Since ~x = -x-1, negating the four-byte
   value cmyk (written as four bytes [c,m,y,k]) gives:

     ~cmyk = [-c-1,-m-1,-y-1,-k-1]

   Only the bottom 8 bits of the negated values are stored, there is an
   implicit 9th sign bit for each value, which is set for all of the bytes.
   If we add all of these bytes individually to the existing values
   [C,M,Y,K], we get:

     [C-c-1,M-m-1,Y-y-1,K-k-1]

   If the C addition carries into the 9th bit, then:

     C-c-1 >= 0

   and therefore:

     C >= c + 1

   and so:

     C > c

   (Similarly for m/M, y/Y, and k/K.) We can determine if the additions
   carry into the 9th bit by looking at the three sources of carries. These
   are:

     ((~c & 0x7f) + (C & 0x7f)) & 0x80

     (~c & 0x80)

     (C & 0x80)

   If two or more of these are non-zero, the addition would carry into the
   9th bit. We convert this carry into a mask, which will be used to
   determine which of the existing or blitted values to use.
*/
static void bitfill32max(render_blit_t *rb,
                         dcoord y, register dcoord xs, register dcoord xe)
{
  register uint32 cmyk, cmyk7, cmyk8, *u32ptr, maxblit_mask ;
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill32max") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 32, "Packed color size incorrect") ;
  HQASSERT(rb->maxmode != BLT_MAX_NONE, "Should be maxblitting") ;

  xe = xe - xs + 1 ; /* total pixels (long words) to fill */
  xs += rb->x_sep_position ;

  /* Color state cannot be maxblit only. */
  HQASSERT((color->state[0] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (color->state[1] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (color->state[2] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (color->state[3] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit,
           "Color state is maxblit only") ;

  /* The maxblit mask is a mask giving the non-maxblitted channels. */
  maxblit_mask = rb->p_ri->p_rs->cs.maxblit_mask->words[0] ;

  cmyk = color->packed.channels.words[0] ;
  if ( rb->opmode == BLT_OVP_SOME ) {
    uint32 mask = rb->p_ri->p_rs->cs.overprint_mask->words[0] ;
    /* Overprinted channels are masked out, leaving their values at zero.
       This means the maximum value must be the existing byte. We also need
       to modify the maxblit channel mask, so that fully overprinted channels
       are treated as maxblits with this zero value. */
    cmyk &= mask ;
    maxblit_mask &= mask ;
  }

  /* Now invert the maxblit mask to give the channels that are to be
     maxblitted, rather than not maxblitted. We're going to simplify the loop
     by always performing the max operation. We use this mask when loading
     the existing colors, so the channels that should knockout will be
     compared against zero. The max operation will therefore automatically
     select the new color. */
  maxblit_mask = ~maxblit_mask ;

  cmyk7 = ~cmyk & 0x7f7f7f7fu ;
  cmyk8 = (~cmyk >> 7) & 0x1010101u ; /* top bits moved to bit 0 */

  u32ptr = (uint32 *)rb->ylineaddr + xs ;
  do {
    uint32 old = *u32ptr & maxblit_mask ; /* Channels to maxblit. */
    uint32 mask = (old & 0x7f7f7f7fu) + cmyk7 ;
    mask = (mask >> 7) & 0x1010101u ;
    mask += (old >> 7) & 0x1010101u ;
    mask += cmyk8 ;
    mask = (mask >> 1) & 0x1010101u ;
    mask *= 0xffu ;

    /* Original bytes selected by mask are greater than new value. */
    *u32ptr = (*u32ptr & mask) | (cmyk & ~mask) ;
    ++u32ptr ;
  } while ( --xe > 0 ) ;
}

static void bitclip32max(render_blit_t *rb,
                         dcoord y, dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip32max") ;

  bitclipn(rb, y , xs , xe , bitfill32max) ;
}

/* ---------------------------------------------------------------------- */
static void bitfill32min(render_blit_t *rb,
                         dcoord y , register dcoord xs, register dcoord xe )
{
  register uint32 cmyk, cmyk7, cmyk8, *u32ptr, maxblit_mask ;
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill32min") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 32, "Packed color size incorrect") ;
  HQASSERT(rb->maxmode != BLT_MAX_NONE, "Should be maxblitting") ;

  xe = xe - xs + 1 ; /* total pixels (long words) to fill */
  xs += rb->x_sep_position ;

  /* Color state cannot be maxblit only. */
  HQASSERT((color->state[0] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (color->state[1] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (color->state[2] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (color->state[3] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit,
           "Color state is maxblit only") ;

  /* The maxblit mask is a mask giving the non-maxblitted channels. */
  maxblit_mask = rb->p_ri->p_rs->cs.maxblit_mask->words[0] ;

  cmyk = color->packed.channels.words[0] ;
  if ( rb->opmode == BLT_OVP_SOME ) {
    uint32 mask = rb->p_ri->p_rs->cs.overprint_mask->words[0] ;
    /* Overprinted channels are set to 0xff. This means the minimum value
       must be the existing byte. We also need to modify the maxblit channel
       mask, so that fully overprinted channels are treated as maxblits with
       this maximum value. */
    cmyk |= ~mask ;
    maxblit_mask &= mask ;
  }

  /* Leave the maxblit mask as the channels that will not be maxblitted. This
     will be OR'ed into the existing color when it's loaded, so the min
     operation should automatically select the new color for those
     channels. */

  cmyk7 = ~cmyk & 0x7f7f7f7fu ;
  cmyk8 = (~cmyk >> 7) & 0x1010101u ; /* top bits moved to bit 0 */

  u32ptr = (uint32 *)rb->ylineaddr + xs ;
  do {
    uint32 old = *u32ptr | maxblit_mask ; /* Channels to maxblit. */
    uint32 mask = (old & 0x7f7f7f7fu) + cmyk7 ;
    mask = (mask >> 7) & 0x1010101u ;
    mask += (old >> 7) & 0x1010101u ;
    mask += cmyk8 ;
    mask = (mask >> 1) & 0x1010101u ;
    mask *= 0xffu ;

    /* Original bytes selected by mask are greater than new value. */
    *u32ptr = (*u32ptr & ~mask) | (cmyk & mask) ;
    ++u32ptr ;
  } while ( --xe > 0 ) ;
}

static void bitclip32min(render_blit_t *rb,
                         dcoord y, dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip32min" ) ;

  bitclipn(rb, y , xs , xe , bitfill32min) ;
}

/* ---------------------------------------------------------------------- */
/**
 * Overprint all the 32bit pixels in a given range
 */
static void bitfill32overprint(render_blit_t *rb, dcoord y,
                               register dcoord xs, register dcoord xe)
{
  register uint32 cmyk, *u32ptr, overprint_mask ;
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill32overprint") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 32, "Packed color size incorrect") ;
  HQASSERT(rb->opmode == BLT_OVP_SOME, "Should be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  xe = xe - xs + 1 ; /* total pixels (long words) to fill */
  xs += rb->x_sep_position ;

  overprint_mask = rb->p_ri->p_rs->cs.overprint_mask->words[0] ;
  cmyk = color->packed.channels.words[0] & overprint_mask ;
  overprint_mask = ~overprint_mask ; /* Mask for existing pixels */

  u32ptr = (uint32 *)rb->ylineaddr + xs ;
  do {
    *u32ptr = (*u32ptr & overprint_mask) | cmyk ;
    ++u32ptr ;
  } while ( --xe > 0 ) ;
}

static void bitclip32overprint(render_blit_t *rb,
                               dcoord y, register dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip32overprint") ;

  bitclipn(rb, y, xs, xe, bitfill32overprint) ;
}

/**
 * Set all the 32bit pixels in a given range to the given value
 */
static void bitfill32knockout(render_blit_t *rb,
                              dcoord y, register dcoord xs, register dcoord xe)
{
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y) ;

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill32knockout") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 32, "Packed color size incorrect") ;
  HQASSERT(rb->opmode == BLT_OVP_NONE, "Should not be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  xe = xe - xs + 1 ; /* total pixels (long words) to fill */
  xs += rb->x_sep_position ;

  HqMemSet32((uint32 *)rb->ylineaddr + xs, color->packed.channels.words[0], xe);
}

static void bitclip32knockout(render_blit_t *rb,
                              dcoord y, register dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip32knockout") ;

  bitclipn(rb, y , xs , xe , bitfill32knockout) ;
}

/** Self-modifying blits for 32-bit tone span fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void bitfill32(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &slices32[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->spanfn)(rb, y, xs, xe) ;
}

/* ---------------------------------------------------------------------- */
/**
 * Overprint a rectangular block with a given 32bit color value.
 */
static void blkfill32overprint(render_blit_t *rb, register dcoord ys,
                               register dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = rb_copy.outputform->l;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill32overprint") ;
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  do {
    bitfill32overprint(&rb_copy, ys , xs , xe);
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
  } while ( ++ys <= ye ) ;
}

static void blkclip32overprint(render_blit_t *rb, register dcoord ys,
                               register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfill32overprint);
}

/**
 * Fill a rectangular block with a given 32bit color value.
 *
 * This can be implemented just as repeated calls to the line
 * version bitfill32(). But for performance reasons it is more efficient
 * to effectively pull that inline and re-arrange things to avoid any
 * repeated calculations.
 */
static inline void blkfill32knockout(render_blit_t *rb, register dcoord ys,
                                     register dcoord ye, dcoord xs, dcoord xe)
{
  register uint32 cmyk, *u32ptr ;
  register int32 wupdate = rb->outputform->l;
  const blit_color_t *color = rb->color;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill32knockout") ;
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 32, "Packed color size incorrect") ;
  HQASSERT(rb->opmode == BLT_OVP_NONE, "Should not be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  xe = xe - xs + 1 ; /* total pixels (long words) to fill */
  xs += rb->x_sep_position ;

  cmyk = color->packed.channels.words[0];
  u32ptr = (uint32 *)rb->ylineaddr + xs ;
  do {
    HqMemSet32(u32ptr, cmyk, xe) ;
    u32ptr = (uint32 *)BLIT_ADDRESS(u32ptr, wupdate) ;
  } while ( ++ys <= ye ) ;
}

static void blkclip32knockout(render_blit_t *rb, register dcoord ys,
                              register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfill32knockout);
}

/** Self-modifying blits for 32-bit tone block fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void blkfill32(render_blit_t *rb, register dcoord ys,
                      register dcoord ye, dcoord xs, dcoord xe)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &slices32[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->blockfn)(rb, ys, ye, xs, xe) ;
}

/* ---------------------------------------------------------------------- */

/** Optimised pixel extracter for 4 channels, 8 bits, subtractive color
    spaces, forward row order. */
static inline void tone32_subtractive_4x8_forward(blit_color_t *color,
                                                  const void **buffer,
                                                  int32 *npixels,
                                                  unsigned int nexpanded,
                                                  const int blit_to_expanded[])
{
  const uint32 *current ;
  uint32 value ;
  int32 remaining ;

  UNUSED_PARAM(unsigned int, nexpanded) ;
  UNUSED_PARAM(const int *, blit_to_expanded) ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 4 &&
           color->map->rendered[0] == 15 &&
           color->map->nrendered == 4 &&
           color->map->packed_bits == 32 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           !color->map->apply_properties &&
           color->map->alpha_index >= 4 &&
           channel_is_8bit(color->map, 0) &&
           channel_is_negative8(color->map, 0) &&
           channel_is_8bit(color->map, 1) &&
           channel_is_negative8(color->map, 1) &&
           channel_is_8bit(color->map, 2) &&
           channel_is_negative8(color->map, 2) &&
           channel_is_8bit(color->map, 3) &&
           channel_is_negative8(color->map, 3),
           "Not a 4x8 bit negative colormap") ;
  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Quantised screen not set for expander color") ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(nexpanded == 4 &&
           blit_to_expanded[0] == 0 && blit_to_expanded[1] == 1 &&
           blit_to_expanded[2] == 2 && blit_to_expanded[3] == 3,
           "Should be expanding 4x8 pixels") ;

  current = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  /* Quicker to re-pack than to test. */
  color->packed.channels.words[0] = value = *current ;
#if BLIT_WIDTH_BYTES > 4
  color->packed.channels.words[1] = color->packed.channels.words[0] ;
#endif
  color->packed.channels.blits[0] = ~color->packed.channels.blits[0] ;

#ifdef ASSERT_BUILD
  /* Note that quantised color is not valid. */
  color->valid &= ~blit_color_quantised ;
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif

  remaining = *npixels ;
  do {
    ++current ;
    --remaining ;
  } while ( remaining != 0 && *current == value ) ;

  *buffer = current ;
  *npixels -= remaining ;
}

/** \fn fill_subtractive_4x8_rows
    Optimised row fill function for 4 channels, 8 bits, subtractive color
    spaces, forward expansion, render by row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_subtractive_4x8_rows
#define PIXEL_FN(p_) tone32_subtractive_4x8_forward
#define BLOCK_FN blkfill32knockout /* Explicit block call */
#include "imgfillorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn orth_subtractive_4x8_rows
    Optimised image callback function for 4x8-bit orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION orth_subtractive_4x8_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) fill_subtractive_4x8_rows
#define NOT_HALFTONED
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_subtractive_4x8_cols
    Optimised row fill function for 4 channels, 8 bits, subtractive color
    spaces, forward expansion, render by column order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_subtractive_4x8_cols
#define PIXEL_FN(p_) tone32_subtractive_4x8_forward
#define BLOCK_FN blkfill32knockout /* Explicit block call */
#include "imgfillorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn orth_subtractive_4x8_cols
    Optimised image callback function for 4x8-bit orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION orth_subtractive_4x8_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) fill_subtractive_4x8_cols
#define NOT_HALFTONED
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** Optimised 32-bit tone fill for the most common cases. */
static void imageblt32knockout(render_blit_t *rb, imgblt_params_t *params,
                               imgblt_callback_fn *callback,
                               Bool *result)
{
  /* Added code size for optimised images only if we're using this for an
     output surface. */
  const blit_colormap_t *map ;

  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  VERIFY_OBJECT(rb->color, BLIT_COLOR_NAME) ;
  map = rb->color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  ASSERT_BASE_OR_FORWARD_ONLY(rb, imagefn, &next_imgblt,
                              "Tone 32 image function called after other blits") ;

  /* Very specialised fully expanded blit stack for unclipped/rectclipped
     images. The combinations we probably want for this are forward/backward,
     subtractive/additive. */
  if ( params->type == IM_BLIT_IMAGE && !params->out16 &&
       params->converted_comps == 4 && !params->on_the_fly &&
       !map->apply_properties &&
       params->blit_to_expanded[0] == 0 && params->blit_to_expanded[1] == 1 &&
       params->blit_to_expanded[2] == 2 && params->blit_to_expanded[3] == 3 &&
       map->nchannels == 4 && map->alpha_index >= 4 && map->nrendered == 4 &&
       channel_is_negative8(map, 0) && channel_is_negative8(map, 1) &&
       channel_is_negative8(map, 2) && channel_is_negative8(map, 3) ) {
    if ( params->orthogonal ) {
      if ( rb->clipmode != BLT_CLP_COMPLEX ) {
        HQASSERT(!params->wflip && !params->hflip,
                 "Tone32 surface should not be X or Y flipped") ;
        if ( params->geometry->wx != 0 ) {
          *result = orth_subtractive_4x8_rows(rb, params) ;
        } else {
          *result = orth_subtractive_4x8_cols(rb, params) ;
        }
        return ;
      }
    }
    /* else rotated image optimisations omitted until proven necessary. */
  }

  imagebltn(rb, params, callback, result) ;
}

/** Image blit selector to pick suitable optimised 32-bit tone fill for
    knockout images. */
static void imageblt32(render_blit_t *rb, imgblt_params_t *params,
                       imgblt_callback_fn *callback,
                       Bool *result)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &slices32[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->imagefn)(rb, params, callback, result) ;
}

/* ---------------------------------------------------------------------- */
/*
 * Set all the 32bit pixels in a given region to a given value
 */
static void areahalf32(render_blit_t *rb, FORM *formptr)
{
  blit_color_t *color = rb->color ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->map->packed_bits == 32, "Packed color size incorrect") ;
  HQASSERT(color->map->expanded_bytes != 0, "Expanded color size incorrect");

  blit_color_expand(color) ;

  BlitSet(theFormA(*formptr), color->packed.channels.blits[0],
          theFormS(*formptr) >> BLIT_SHIFT_BYTES);
}

/* ---------------------------------------------------------------------- */

/** Render preparation function for toneblits packs current color. */
static surface_prepare_t render_prepare_32(surface_handle_t handle,
                                           render_info_t *p_ri)
{
  blit_color_t *color ;

  UNUSED_PARAM(surface_handle_t, handle) ;

  HQASSERT(p_ri, "No render info") ;

  color = p_ri->rb.color ;
  blit_color_quantise(color) ;
  blit_color_pack(color) ;

  return SURFACE_PREPARE_OK ;
}

/** Blit color packing for 4 channels of 8 bits, negative. */
static void tone32_pack_negative(blit_color_t *color)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 4 &&
           color->map->rendered[0] == 15 &&
           color->map->nrendered == 4 &&
           color->map->packed_bits == 32 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           !color->map->apply_properties &&
           channel_is_8bit(color->map, 0) &&
           channel_is_negative8(color->map, 0) &&
           channel_is_8bit(color->map, 1) &&
           channel_is_negative8(color->map, 1) &&
           channel_is_8bit(color->map, 2) &&
           channel_is_negative8(color->map, 2) &&
           channel_is_8bit(color->map, 3) &&
           channel_is_negative8(color->map, 3),
           "Not a 4x8 bit negative colormap") ;

  HQASSERT(color->quantised.qcv[0] <= 255 ||
           (color->state[0] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;
  HQASSERT(color->quantised.qcv[1] <= 255 ||
           (color->state[1] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;
  HQASSERT(color->quantised.qcv[2] <= 255 ||
           (color->state[2] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;
  HQASSERT(color->quantised.qcv[3] <= 255 ||
           (color->state[3] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;

  /* Quicker to re-pack than to test. */
  color->packed.channels.bytes[0] = (uint8)color->quantised.qcv[0] ;
  color->packed.channels.bytes[1] = (uint8)color->quantised.qcv[1] ;
  color->packed.channels.bytes[2] = (uint8)color->quantised.qcv[2] ;
  color->packed.channels.bytes[3] = (uint8)color->quantised.qcv[3] ;
#if BLIT_WIDTH_BYTES > 4
  color->packed.channels.words[1] = color->packed.channels.words[0] ;
#endif
  color->packed.channels.blits[0] = ~color->packed.channels.blits[0] ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Expanding the color is a no-op because the pack routine already did
    it. */
static void tone32_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_packed, "Blit color not packed") ;
  HQASSERT(color->valid & blit_color_expanded, "Blit color not expanded") ;
}

static void tone32_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->packed_bits == 32 && map->nchannels == 4 &&
           channel_is_8bit(map, 0) && channel_is_8bit(map, 1) &&
           channel_is_8bit(map, 2) && channel_is_8bit(map, 3),
           "Not a suitable 32-bit blit map") ;

  /* Cannot use optimise pack routine if properties are being used, because
     the presence of channels can be changed. */
  if ( !map->apply_properties && map->nrendered == 4 ) {
    if ( channel_is_negative8(map, 0) && channel_is_negative8(map, 1) &&
         channel_is_negative8(map, 2) && channel_is_negative8(map, 3) ) {
      map->pack_quantised_color = tone32_pack_negative ;
      map->expand_packed_color = tone32_color_expand ;
    }
  }
}

/** Pagedevice /Private dictionary for tone32 surface selection. */
const static sw_datum tone32_private[] = {
  /* Comparing integer to array auto-coerces to the length. */
  SW_DATUM_STRING("ColorChannels"), SW_DATUM_INTEGER(4),
} ;

/** Alternative PackingUnitRequests for tone32 surface. */
const static sw_datum tone32_packing[] = {
  SW_DATUM_INTEGER(0), /* Not defined */
  SW_DATUM_INTEGER(8),
#ifdef highbytefirst
 /* High byte first is the same order for any packing depth. */
  SW_DATUM_INTEGER(16),
  SW_DATUM_INTEGER(32),
#if BLIT_WIDTH_BYTES > 4
  SW_DATUM_INTEGER(64),
#endif
#endif
} ;

/** Pagedevice dictionary for tone32 surface selection. */
const static sw_datum tone32_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"), SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_PIXEL),
  SW_DATUM_STRING("PackingUnitRequest"),
    SW_DATUM_ARRAY(&tone32_packing[0], SW_DATA_ARRAY_LENGTH(tone32_packing)),
  /** \todo ajcd 2009-09-23: We can only use Private /ColorChannels as a
     selection key for this because pixel-interleaved currently cannot add
     new channels. Therefore, gucr_framesChannelsTotal(gucr_framesStart()) is
     always its original value of 4, as initialised from ColorChannels. */
  SW_DATUM_STRING("Private"),
    SW_DATUM_DICT(&tone32_private[0], SW_DATA_DICT_LENGTH(tone32_private)),
#if 0
  /** \todo ajcd 2009-09-23: ProcessColorModel is not used, it wasn't part
      of the original selection criteria. There may be inversions between
      CMYK and RGBK, the consumer will have to set that up. */
  SW_DATUM_STRING("ProcessColorModel"), SW_DATUM_STRING("DeviceCMYK"),
#endif
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(256),
} ;

/** CMYK 8bpc pixel interleaved surface optimisation. */
static surface_set_t tone32_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&tone32_dict[0],
                                 SW_DATA_DICT_LENGTH(tone32_dict))) ;

/** The CMYK8 surface description. */
static surface_t tone32 = SURFACE_INIT ;
static const surface_t *indexed[N_SURFACE_TYPES] ;

void init_toneblt_32(void)
{
  /* Knockout optimised slice. */
  slices32[0][BLT_CLP_NONE].spanfn =
    slices32[0][BLT_CLP_RECT].spanfn = bitfill32knockout ;
  slices32[0][BLT_CLP_COMPLEX].spanfn = bitclip32knockout ;

  slices32[0][BLT_CLP_NONE].blockfn =
    slices32[0][BLT_CLP_RECT].blockfn = blkfill32knockout ;
  slices32[0][BLT_CLP_COMPLEX].blockfn = blkclip32knockout ;

  slices32[0][BLT_CLP_NONE].charfn =
    slices32[0][BLT_CLP_RECT].charfn =
    slices32[0][BLT_CLP_COMPLEX].charfn = charbltn ;

  slices32[0][BLT_CLP_NONE].imagefn =
    slices32[0][BLT_CLP_RECT].imagefn =
    slices32[0][BLT_CLP_COMPLEX].imagefn = imageblt32knockout ;

  /* Overprint optimised slice. */
  slices32[1][BLT_CLP_NONE].spanfn =
    slices32[1][BLT_CLP_RECT].spanfn = bitfill32overprint ;
  slices32[1][BLT_CLP_COMPLEX].spanfn = bitclip32overprint ;

  slices32[1][BLT_CLP_NONE].blockfn =
    slices32[1][BLT_CLP_RECT].blockfn = blkfill32overprint ;
  slices32[1][BLT_CLP_COMPLEX].blockfn = blkclip32overprint ;

  slices32[1][BLT_CLP_NONE].charfn =
    slices32[1][BLT_CLP_RECT].charfn =
    slices32[1][BLT_CLP_COMPLEX].charfn = charbltn ;

  slices32[1][BLT_CLP_NONE].imagefn =
    slices32[1][BLT_CLP_RECT].imagefn =
    slices32[1][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Base blits use self-modifying blits */
  tone32.baseblits[BLT_CLP_NONE].spanfn =
    tone32.baseblits[BLT_CLP_RECT].spanfn =
    tone32.baseblits[BLT_CLP_COMPLEX].spanfn = bitfill32 ;

  tone32.baseblits[BLT_CLP_NONE].blockfn =
    tone32.baseblits[BLT_CLP_RECT].blockfn =
    tone32.baseblits[BLT_CLP_COMPLEX].blockfn = blkfill32 ;

  tone32.baseblits[BLT_CLP_NONE].charfn =
    tone32.baseblits[BLT_CLP_RECT].charfn =
    tone32.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  tone32.baseblits[BLT_CLP_NONE].imagefn =
    tone32.baseblits[BLT_CLP_RECT].imagefn =
    tone32.baseblits[BLT_CLP_COMPLEX].imagefn = imageblt32 ;

  /* No object map on the side blits for tone32 */

  /* Max blits */
  tone32.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    tone32.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn = bitfill32max ;
  tone32.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = bitclip32max ;

  tone32.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    tone32.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone32.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone32.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    tone32.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn =
    tone32.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone32.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    tone32.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    tone32.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  tone32.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].spanfn =
    tone32.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].spanfn = bitfill32min ;
  tone32.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].spanfn = bitclip32min ;

  tone32.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].blockfn =
    tone32.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone32.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone32.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].charfn =
    tone32.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].charfn =
    tone32.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone32.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].imagefn =
    tone32.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].imagefn =
    tone32.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No ROP blits for tone32 */

  init_pcl_pattern_blit(&tone32) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&tone32) ;
  surface_pattern_builtin(&tone32) ;
  surface_gouraud_builtin_tone_multi(&tone32) ;

  tone32.areafill = areahalf32 ;
  tone32.prepare = render_prepare_32 ;
  tone32.blit_colormap_optimise = tone32_blitmap_optimise ;

  tone32.n_rollover = 3 ;
  tone32.screened = FALSE ;

  builtin_clip_N_surface(&tone32, indexed) ;

  /* The surface we've just completed is part of a set implementing 8 bpc
     CMYK output. Add it and all of the associated surfaces to the surface
     array for this set. */
  tone32_set.indexed = indexed ;
  tone32_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

  indexed[SURFACE_OUTPUT] = &tone32 ;

  /* Add trapping surfaces. */
  surface_set_trap_builtin(&tone32_set, indexed);

  surface_set_transparency_builtin(&tone32_set, &tone32, indexed) ;

  /* Now that we've filled in the tone32 surface description, hook it up so
     that it can be found. */
  surface_set_register(&tone32_set) ;
}

/* Log stripped */
