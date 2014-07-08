/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:toneblt24.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for 24-bit (RGB) contone output.
 */

#include "core.h"
#include "objnamer.h"

#include "render.h"    /* x_sep_position */
#include "surface.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "imageo.h"
#include "imgblts.h"
#include "toneblt.h"
#include "toneblt24.h"
#include "pclPatternBlit.h"
#include "hqmemset.h"
#include "hqbitops.h"
#include "gu_chan.h"
#include "builtin.h"
#include "control.h"
#include "interrupts.h"

/** Optimised slice for RGB 8bpp knockout, overprint blits. */
static blitclip_slice_t slices24[2] = {
  BLITCLIP_SLICE_INIT /*knockout*/, BLITCLIP_SLICE_INIT /*overprint*/,
} ;

/* ---------------------------------------------------------------------- */

#ifdef highbytefirst
/* These shift macros are used to align blit_t masks and words with the
   output raster. They should be interpreted as shifting left or right in the
   output raster. We can perform use blit_t words to render the 24 bit blits
   because the RGB triples are always byte aligned, so there are never any
   incorrect carries over byte boundaries. */
#define TONESHIFTLEFT(x_, y_) ((x_) << (y_))
#define TONESHIFTRIGHT(x_, y_) ((x_) >> (y_))

#define R_SHIFT 0  /* Right shift for original RGBR to get R byte */
#define G_SHIFT 16 /* Right shift for original RGBR to get G byte */
#define B_SHIFT 8  /* Right shift for original RGBR to get B byte */

/* Cycle a 32-bit word representation of RGB through RGBR, GBRG, BRGB. Note
   this is NOT a word rotate. It's very deliberately phrased as two
   statements to duplicate the top and bottom bytes. */
#define RGB32_ROTATE(rgb_) MACRO_START \
  rgb_ = rgb_ << 8 ; \
  rgb_ |= rgb_ >> 24 ; \
MACRO_END

#else /* !highbytefirst */
/* These shift macros are used to align blit_t masks and words with the
   output raster. They should be interpreted as shifting left or right in the
   output raster. We can perform use blit_t words to render the 24 bit blits
   because the RGB triples are always byte aligned, so there are never any
   incorrect carries over byte boundaries. */
#define TONESHIFTLEFT(x_, y_) ((x_) >> (y_))
#define TONESHIFTRIGHT(x_, y_) ((x_) << (y_))

#define R_SHIFT 0  /* Right shift for original RGBR to get R byte */
#define G_SHIFT 8 /* Right shift for original RGBR to get G byte */
#define B_SHIFT 16  /* Right shift for original RGBR to get B byte */

/* Cycle a 32-bit word representation of RGB through RGBR, GBRG, BRGB. Note
   this is NOT a word rotate. It's very deliberately phrased as two
   statements to duplicate the top and bottom bytes. */
#define RGB32_ROTATE(rgb_) MACRO_START \
  rgb_ = rgb_ >> 8 ; \
  rgb_ |= rgb_ << 24 ; \
MACRO_END

#endif /* !highbytefirst */

/** Initialise representation of RGBR in a 32-bit unsigned word. This is used
    for fast blitting. */
#define RGB32_INIT(rgb_, color_) MACRO_START \
  const blit_color_t *_color_ = (color_) ; \
  uint32 _rgb_ = ((_color_->packed.channels.bytes[0] << 24) | \
                  (_color_->packed.channels.bytes[1] << G_SHIFT) | \
                  (_color_->packed.channels.bytes[2] << B_SHIFT) | \
                  (_color_->packed.channels.bytes[0] << 0)) ; \
  rgb_ = _rgb_ ; \
MACRO_END

/** A blit_t value with 0x01 for every byte. */
#define BLIT_BYTES_0x01 MASK_BYTES_1(blit_t)

/** A blit_t value with 0x7f for every byte. */
#define BLIT_BYTES_0x7f (BLIT_BYTES_0x01 * 0x7f)

/** An mask for when no overprint mask is present in maxblitting. It
    simplifies the code by avoiding tests in the inner loops. */
static const blit_t noopmask_base[3] = { ALLONES, ALLONES, ALLONES } ;

/* Max/min blit calculations are performed for 4 bytes together in parallel.
   We can do this by negating the RGB value, and determining if adding it to
   the existing value will carry. Since ~x = -x-1, negating the four-byte
   value rgbr (written as four bytes [r,g,b,r]) gives:

     ~rgbr = [-r-1,-g-1,-b-1,-r-1]

   Only the bottom 8 bits of the negated values are stored, there is an
   implicit 9th sign bit for each value, which is set for all of the bytes.
   If we add all of these bytes individually to the existing values
   [R,G,B,R], we get:

     [R-r-1,B-b-1,G-g-1,R-r-1]

   If the R addition carries into the 9th bit, then:

     R-r-1 >= 0

   and therefore:

     R >= r + 1

   and so:

     R > r

   (Similarly for g/G, b/B, and r/R.) We can determine if the additions
   carry into the 9th bit by looking at the three sources of carries. These
   are:

     ((~r & 0x7f) + (R & 0x7f)) & 0x80

     (~r & 0x80)

     (R & 0x80)

   If two or more of these are non-zero, the addition would carry into the
   9th bit. We convert this carry into a mask, which will be used to
   determine which of the existing or blitted values to use.
*/

static void bitfill24max(render_blit_t *rb,
                         dcoord y, register dcoord xs, register dcoord xe)
{
  register blit_t *addr ;
  const blit_t *packed, *opmask, *mbmask ;
  const blit_t *packed_base, *opmask_base, *mbmask_base ;
  int32 wpacked ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill24max") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->color->map->packed_bits == 24, "Packed color size incorrect") ;
  HQASSERT(rb->maxmode != BLT_MAX_NONE, "Should be maxblitting") ;
  HQASSERT(rb->color->map->expanded_bytes == sizeof(blit_t) * 3,
           "Expanded data size is not as expected") ;

  /* Color state cannot be maxblit only. */
  HQASSERT((rb->color->state[0] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (rb->color->state[1] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (rb->color->state[2] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit,
           "Color state is maxblit only") ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= 3 ; /* convert to bytes to fill */

  xs += rb->x_sep_position ;

  /* Since we know the packed color data is expanded, we will start the color
     selection at the phase that matches the output raster location. This
     means we don't need to shift the packed color data into place, only mask
     the start and end of the blit line. The expanded size for RGB is
     sizeof(blit_t) * 3 because 3 is relatively prime to sizeof(blit_t).
     There are sizeof(blit_t) cycles of 3 pixels each, so the start cycle
     can be worked out from the start pixel position, and then adjusted to a
     blit_t offset. */
  wpacked = (xs & BLIT_MASK_BYTES) * 3 ; /* Bytes into packed color */
  packed_base = &rb->color->packed.channels.blits[0] ;
  packed = packed_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  mbmask_base = &rb->p_ri->p_rs->cs.maxblit_mask->blits[0] ;
  mbmask = mbmask_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  opmask_base = rb->opmode == BLT_OVP_SOME
    ? &rb->p_ri->p_rs->cs.overprint_mask->blits[0]
    : &noopmask_base[0] ;
  opmask = opmask_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  wpacked = sizeof(blit_t) * 3 - wpacked ; /* Bytes left in packed color */

  xs *= 3 ; /* Convert to byte offset */
  addr = BLIT_ADDRESS(rb->ylineaddr, xs & ~BLIT_MASK_BYTES) ;
  xs &= BLIT_MASK_BYTES ; /* Byte shift of starting pixel */

  /* Partial left-span to align to blit_t boundary. */
  if ( xs != 0 ) {
    blit_t mask = TONESHIFTRIGHT(ALLONES, xs * 8) ;
    blit_t vvvv, old ;

    xs = BLIT_WIDTH_BYTES - xs ; /* bytes filled */
    HQASSERT(wpacked >= xs, "Not enough color bytes left") ;

    wpacked -= xs ; /* bytes left in packed color */
    xe -= xs ;      /* bytes left to do after this word */
    if ( xe < 0 ) /* Fill is entirely within one blit_t */
      mask &= TONESHIFTLEFT(ALLONES, -xe * 8) ;

    /* Overprinted channels are masked out, leaving their values at zero.
       This means the maximum value must be the existing byte. */
    mask &= *opmask ;
    vvvv = *packed & mask ;

    /* We also need to modify the maxblit channel mask, so that fully
       overprinted channels are treated as maxblits with this zero value. We
       then invert the maxblit mask to give the channels that are to be
       maxblitted, rather than not maxblitted. We're going to simplify the
       loop by always performing the max operation. We use this mask when
       loading the existing colors, so the channels that should knockout will
       be compared against zero. The max operation will therefore
       automatically select the new color. */
    mask = ~(mask & *mbmask) ;
    old = *addr & mask ; /* Channels to maxblit */

    mask = (~vvvv & BLIT_BYTES_0x7f) + (old & BLIT_BYTES_0x7f) ;
    mask = (mask >> 7) & BLIT_BYTES_0x01 ;
    mask += (old >> 7) & BLIT_BYTES_0x01 ;
    mask += (~vvvv >> 7) & BLIT_BYTES_0x01 ;
    mask = (mask >> 1) & BLIT_BYTES_0x01 ; /* Accumulate carries */
    mask *= 0xffu ; /* Convert to byte mask */

    /* Original bytes selected by mask are greater than new value. */
    *addr = (*addr & mask) | (vvvv & ~mask) ;
    ++addr ; ++packed ; ++opmask ; ++mbmask ;
  }

  HQASSERT((wpacked & BLIT_MASK_BYTES) == 0,
           "Should have a whole number of blit_t words left") ;

  if ( xe > 0 ) {
    /* Complete words in middle. We've now aligned the output address with a
       blit_t boundary and the packed color buffer, we can transfer full
       blit_t words directly. */
    while ( (xe -= BLIT_WIDTH_BYTES) >= 0 ) {
      blit_t mask, vvvv, old ;

      if ( wpacked == 0 ) {
        wpacked = sizeof(blit_t) * 3 ;
        packed = packed_base ;
        opmask = opmask_base ;
        mbmask = mbmask_base ;
      }

      wpacked -= BLIT_WIDTH_BYTES ;

      mask = *opmask ;
      vvvv = *packed & mask ;

      mask = ~(mask & *mbmask) ;
      old = *addr & mask ; /* Channels to maxblit */

      mask = (~vvvv & BLIT_BYTES_0x7f) + (old & BLIT_BYTES_0x7f) ;
      mask = (mask >> 7) & BLIT_BYTES_0x01 ;
      mask += (old >> 7) & BLIT_BYTES_0x01 ;
      mask += (~vvvv >> 7) & BLIT_BYTES_0x01 ;
      mask = (mask >> 1) & BLIT_BYTES_0x01 ; /* Accumulate carries */
      mask *= 0xffu ; /* Convert to byte mask */

      /* Original bytes selected by mask are greater than new value. */
      *addr = (*addr & mask) | (vvvv & ~mask) ;
      ++addr ; ++packed ; ++opmask ; ++mbmask ;
    }

    /* Partial right span. */
    if ( -xe < BLIT_WIDTH_BYTES ) {
      blit_t mask = TONESHIFTLEFT(ALLONES, -xe * 8) ;
      blit_t vvvv, old ;

      if ( wpacked == 0 ) {
        packed = packed_base ;
        opmask = opmask_base ;
        mbmask = mbmask_base ;
      }

      mask &= *opmask ;
      vvvv = *packed & mask ;

      mask = ~(mask & *mbmask) ;
      old = *addr & mask ; /* Channels to maxblit */

      mask = (~vvvv & BLIT_BYTES_0x7f) + (old & BLIT_BYTES_0x7f) ;
      mask = (mask >> 7) & BLIT_BYTES_0x01 ;
      mask += (old >> 7) & BLIT_BYTES_0x01 ;
      mask += (~vvvv >> 7) & BLIT_BYTES_0x01 ;
      mask = (mask >> 1) & BLIT_BYTES_0x01 ; /* Accumulate carries */
      mask *= 0xffu ; /* Convert to byte mask */

      /* Original bytes selected by mask are greater than new value. */
      *addr = (*addr & mask) | (vvvv & ~mask) ;
    }
  }
}

static void bitclip24max(render_blit_t *rb,
                         dcoord y, dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip24max") ;

  bitclipn(rb, y , xs , xe , bitfill24max) ;
}

/* ---------------------------------------------------------------------- */
static void bitfill24min(render_blit_t *rb,
                         dcoord y, register dcoord xs, register dcoord xe)
{
  register blit_t *addr ;
  const blit_t *packed, *opmask, *mbmask ;
  const blit_t *packed_base, *opmask_base, *mbmask_base ;
  int32 wpacked ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill24min") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_expanded, "Color not expanded") ;
  HQASSERT(rb->color->map->packed_bits == 24, "Packed color size incorrect") ;
  HQASSERT(rb->maxmode != BLT_MAX_NONE, "Should be maxblitting") ;
  HQASSERT(rb->color->map->expanded_bytes == sizeof(blit_t) * 3,
           "Expanded data size is not as expected") ;

  /* Color state cannot be maxblit only. */
  HQASSERT((rb->color->state[0] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (rb->color->state[1] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit ||
           (rb->color->state[2] & (blit_channel_present|blit_channel_maxblit)) != blit_channel_maxblit,
           "Color state is maxblit only") ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= 3 ; /* convert to bytes to fill */

  xs += rb->x_sep_position ;

  /* Since we know the packed color data is expanded, we will start the color
     selection at the phase that matches the output raster location. This
     means we don't need to shift the packed color data into place, only mask
     the start and end of the blit line. The expanded size for RGB is
     sizeof(blit_t) * 3 because 3 is relatively prime to sizeof(blit_t).
     There are sizeof(blit_t) cycles of 3 pixels each, so the start cycle
     can be worked out from the start pixel position, and then adjusted to a
     blit_t offset. */
  wpacked = (xs & BLIT_MASK_BYTES) * 3 ; /* Bytes into packed color */
  packed_base = &rb->color->packed.channels.blits[0] ;
  packed = packed_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  mbmask_base = &rb->p_ri->p_rs->cs.maxblit_mask->blits[0] ;
  mbmask = mbmask_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  opmask_base = rb->opmode == BLT_OVP_SOME
    ? &rb->p_ri->p_rs->cs.overprint_mask->blits[0]
    : &noopmask_base[0] ;
  opmask = opmask_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  wpacked = sizeof(blit_t) * 3 - wpacked ; /* Bytes left in packed color */

  xs *= 3 ; /* Convert to byte offset */
  addr = BLIT_ADDRESS(rb->ylineaddr, xs & ~BLIT_MASK_BYTES) ;
  xs &= BLIT_MASK_BYTES ; /* Byte shift of starting pixel */

  /* Partial left-span to align to blit_t boundary. */
  if ( xs != 0 ) {
    blit_t mask = TONESHIFTRIGHT(ALLONES, xs * 8) ;
    blit_t vvvv, old ;

    xs = BLIT_WIDTH_BYTES - xs ; /* bytes filled */
    HQASSERT(wpacked >= xs, "Not enough color bytes left") ;

    wpacked -= xs ; /* bytes left in packed color */
    xe -= xs ;      /* bytes left to do after this word */
    if ( xe < 0 ) /* Fill is entirely within one blit_t */
      mask &= TONESHIFTLEFT(ALLONES, -xe * 8) ;

    /* Overprinted channels are set to 0xff. This means the minimum value
       must be the existing byte. */
    mask &= *opmask ;
    vvvv = *packed | ~mask ;

    /* We also need to modify the maxblit channel mask, so that fully
       overprinted channels are treated as maxblits with this maximum value.
       We leave the maxblit mask as the channels that will not be maxblitted.
       This will be OR'ed into the existing color when it's loaded, so the
       min operation should automatically select the new color for those
       channels. */
    mask &= *mbmask ;
    old = *addr | mask ; /* Channels to maxblit */

    mask = (~vvvv & BLIT_BYTES_0x7f) + (old & BLIT_BYTES_0x7f) ;
    mask = (mask >> 7) & BLIT_BYTES_0x01 ;
    mask += (old >> 7) & BLIT_BYTES_0x01 ;
    mask += (~vvvv >> 7) & BLIT_BYTES_0x01 ;
    mask = (mask >> 1) & BLIT_BYTES_0x01 ; /* Accumulate carries */
    mask *= 0xffu ; /* Convert to byte mask */

    /* Original bytes selected by mask are greater than new value. */
    *addr = (*addr & ~mask) | (vvvv & mask) ;
    ++addr ; ++packed ; ++opmask ; ++mbmask ;
  }

  HQASSERT((wpacked & BLIT_MASK_BYTES) == 0,
           "Should have a whole number of blit_t words left") ;

  if ( xe > 0 ) {
    /* Complete words in middle. We've now aligned the output address with a
       blit_t boundary and the packed color buffer, we can transfer full
       blit_t words directly. */
    while ( (xe -= BLIT_WIDTH_BYTES) >= 0 ) {
      blit_t mask, vvvv, old ;

      if ( wpacked == 0 ) {
        wpacked = sizeof(blit_t) * 3 ;
        packed = packed_base ;
        opmask = opmask_base ;
        mbmask = mbmask_base ;
      }

      wpacked -= BLIT_WIDTH_BYTES ;

      mask = *opmask ;
      vvvv = *packed | ~mask ;

      mask &= *mbmask ;
      old = *addr | mask ; /* Channels to maxblit */

      mask = (~vvvv & BLIT_BYTES_0x7f) + (old & BLIT_BYTES_0x7f) ;
      mask = (mask >> 7) & BLIT_BYTES_0x01 ;
      mask += (old >> 7) & BLIT_BYTES_0x01 ;
      mask += (~vvvv >> 7) & BLIT_BYTES_0x01 ;
      mask = (mask >> 1) & BLIT_BYTES_0x01 ; /* Accumulate carries */
      mask *= 0xffu ; /* Convert to byte mask */

      /* Original bytes selected by mask are greater than new value. */
      *addr = (*addr & ~mask) | (vvvv & mask) ;
      ++addr ; ++packed ; ++opmask ; ++mbmask ;
    }

    /* Partial right span. */
    if ( -xe < BLIT_WIDTH_BYTES ) {
      blit_t mask = TONESHIFTLEFT(ALLONES, -xe * 8) ;
      blit_t vvvv, old ;

      if ( wpacked == 0 ) {
        packed = packed_base ;
        opmask = opmask_base ;
        mbmask = mbmask_base ;
      }

      mask &= *opmask ;
      vvvv = *packed | ~mask ;

      mask &= *mbmask ;
      old = *addr | mask ; /* Channels to maxblit */

      mask = (~vvvv & BLIT_BYTES_0x7f) + (old & BLIT_BYTES_0x7f) ;
      mask = (mask >> 7) & BLIT_BYTES_0x01 ;
      mask += (old >> 7) & BLIT_BYTES_0x01 ;
      mask += (~vvvv >> 7) & BLIT_BYTES_0x01 ;
      mask = (mask >> 1) & BLIT_BYTES_0x01 ; /* Accumulate carries */
      mask *= 0xffu ; /* Convert to byte mask */

      /* Original bytes selected by mask are greater than new value. */
      *addr = (*addr & ~mask) | (vvvv & mask) ;
    }
  }
}

static void bitclip24min(render_blit_t *rb,
                         dcoord y, dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip24min" ) ;

  bitclipn(rb, y , xs , xe , bitfill24min) ;
}

/* ---------------------------------------------------------------------- */

static void bitfill24overprint(render_blit_t *rb, dcoord y,
                               register dcoord xs, register dcoord xe)
{
  register blit_t *addr ;
  const blit_t *packed, *opmask ;
  const blit_t *packed_base, *opmask_base ;
  int32 wpacked ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill24overprint") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_expanded, "Color not expanded") ;
  HQASSERT(rb->color->map->packed_bits == 24, "Packed color size incorrect") ;
  HQASSERT(rb->opmode == BLT_OVP_SOME, "Should be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;
  HQASSERT(rb->color->map->expanded_bytes == sizeof(blit_t) * 3,
           "Expanded data size is not as expected") ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= 3 ; /* convert to bytes to fill */

  xs += rb->x_sep_position ;

  /* Since we know the packed color data is expanded, we will start the color
     selection at the phase that matches the output raster location. This
     means we don't need to shift the packed color data into place, only mask
     the start and end of the blit line. The expanded size for RGB is
     sizeof(blit_t) * 3 because 3 is relatively prime to sizeof(blit_t).
     There are sizeof(blit_t) cycles of 3 pixels each, so the start cycle
     can be worked out from the start pixel position, and then adjusted to a
     blit_t offset. */
  wpacked = (xs & BLIT_MASK_BYTES) * 3 ; /* Bytes into packed color */
  packed_base = &rb->color->packed.channels.blits[0] ;
  packed = packed_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  opmask_base = &rb->p_ri->p_rs->cs.overprint_mask->blits[0] ;
  opmask = opmask_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  wpacked = sizeof(blit_t) * 3 - wpacked ; /* Bytes left in packed color */

  xs *= 3 ; /* Convert to byte offset */
  addr = BLIT_ADDRESS(rb->ylineaddr, xs & ~BLIT_MASK_BYTES) ;
  xs &= BLIT_MASK_BYTES ; /* Byte shift of starting pixel */

  /* Partial left-span to align to blit_t boundary. */
  if ( xs != 0 ) {
    blit_t mask = TONESHIFTRIGHT(ALLONES, xs * 8) & *opmask ;

    xs = BLIT_WIDTH_BYTES - xs ; /* bytes filled */
    HQASSERT(wpacked >= xs, "Not enough color bytes left") ;

    xe -= xs ;      /* bytes left to do after this word */
    wpacked -= xs ; /* bytes left in packed color */
    if ( xe < 0 ) { /* Fill is entirely within one blit_t */
      mask &= TONESHIFTLEFT(ALLONES, -xe * 8) ;
      *addr = (*addr & ~mask) | (*packed & mask) ;
      return ;
    }

    *addr = (*addr & ~mask) | (*packed & mask) ;
    ++addr ; ++packed ; ++opmask ;
  }

  HQASSERT((wpacked & BLIT_MASK_BYTES) == 0,
           "Should have a whole number of blit_t words left") ;

  if ( xe > 0 ) {
    /* Complete words in middle. We've now aligned the output address with a
       blit_t boundary and the packed color buffer, we can transfer full
       blit_t words directly. */
    while ( (xe -= BLIT_WIDTH_BYTES) >= 0 ) {
      blit_t mask ;

      if ( wpacked == 0 ) {
        wpacked = sizeof(blit_t) * 3 ;
        packed = packed_base ;
        opmask = opmask_base ;
      }

      wpacked -= BLIT_WIDTH_BYTES ;
      mask = *opmask++ ;
      *addr = (*addr & ~mask) | (*packed & mask) ;
      ++addr ; ++packed ;
    }

    /* Partial right span. */
    if ( -xe < BLIT_WIDTH_BYTES ) {
      blit_t mask = TONESHIFTLEFT(ALLONES, -xe * 8) ;

      if ( wpacked == 0 ) {
        packed = packed_base ;
        opmask = opmask_base ;
      }

      mask &= *opmask ;
      *addr = (*addr & ~mask) | (*packed & mask) ;
    }
  }
}

static void bitclip24overprint(render_blit_t *rb,
                               dcoord y, register dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip32overprint") ;

  bitclipn(rb, y, xs, xe, bitfill24overprint) ;
}

static void bitfill24knockout(render_blit_t *rb,
                              dcoord y, dcoord xs, register dcoord xe)
{
  register blit_t *addr ;
  register const blit_t *packed ;
  const blit_t *packed_base ;
  int32 wpacked ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill24knockout") ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_expanded, "Color not expanded") ;
  HQASSERT(rb->color->map->packed_bits == 24, "Packed color size incorrect") ;
  HQASSERT(rb->opmode == BLT_OVP_NONE, "Should not be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;
  HQASSERT(rb->color->map->expanded_bytes == sizeof(blit_t) * 3,
           "Expanded data size is not as expected") ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= 3 ; /* convert to bytes to fill */

  xs += rb->x_sep_position ;

  /* Since we know the packed color data is expanded, we will start the color
     selection at the phase that matches the output raster location. This
     means we don't need to shift the packed color data into place, only mask
     the start and end of the blit line. The expanded size for RGB is
     sizeof(blit_t) * 3 because 3 is relatively prime to sizeof(blit_t).
     There are sizeof(blit_t) cycles of 3 pixels each, so the start cycle
     can be worked out from the start pixel position, and then adjusted to a
     blit_t offset. */
  wpacked = (xs & BLIT_MASK_BYTES) * 3 ; /* Bytes into packed color */
  packed_base = &rb->color->packed.channels.blits[0] ;
  packed = packed_base + (wpacked >> BLIT_SHIFT_BYTES) ;
  wpacked = sizeof(blit_t) * 3 - wpacked ; /* Bytes left in packed color */

  xs *= 3 ; /* Convert to byte offset */
  addr = BLIT_ADDRESS(rb->ylineaddr, xs & ~BLIT_MASK_BYTES) ;
  xs &= BLIT_MASK_BYTES ; /* Byte shift of starting pixel */

  /* Partial left-span to align to blit_t boundary. */
  if ( xs != 0 ) {
    blit_t mask = TONESHIFTRIGHT(ALLONES, xs * 8) ;

    xs = BLIT_WIDTH_BYTES - xs ; /* bytes filled */
    HQASSERT(wpacked >= xs, "Not enough color bytes left") ;

    xe -= xs ;      /* bytes left to do after this word */
    wpacked -= xs ; /* bytes left in packed color */
    if ( xe < 0 ) { /* Fill is entirely within one blit_t */
      mask &= TONESHIFTLEFT(ALLONES, -xe * 8) ;
      *addr = (*addr & ~mask) | (*packed & mask) ;
      return ;
    }

    *addr = (*addr & ~mask) | (*packed & mask) ;
    ++addr ; ++packed ;
  }

  HQASSERT((wpacked & BLIT_MASK_BYTES) == 0,
           "Should have a whole number of blit_t words left") ;

  if ( xe > 0 ) {
    /* Complete words in middle. We've now aligned the output address with a
       blit_t boundary and the packed color buffer, we can transfer full
       blit_t words directly. */
    while ( (xe -= BLIT_WIDTH_BYTES) >= 0 ) {
      if ( wpacked == 0 ) {
        wpacked = sizeof(blit_t) * 3 ;
        packed = packed_base ;
      }

      wpacked -= BLIT_WIDTH_BYTES ;
      *addr++ = *packed++ ;
    }

    /* Partial right span. */
    if ( -xe < BLIT_WIDTH_BYTES ) {
      blit_t mask = TONESHIFTLEFT(ALLONES, -xe * 8) ;

      if ( wpacked == 0 )
        packed = packed_base ;

      *addr = (*addr & ~mask) | (*packed & mask) ;
    }
  }
}

static void bitclip24knockout(render_blit_t *rb,
                              dcoord y, register dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip24knockout") ;

  bitclipn(rb, y , xs , xe , bitfill24knockout) ;
}

/** Self-modifying blits for 24-bit tone span fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void bitfill24(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &slices24[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->spanfn)(rb, y, xs, xe) ;
}

/* ---------------------------------------------------------------------- */
static void blkfill24overprint(render_blit_t *rb, register dcoord ys,
                               register dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = rb_copy.outputform->l;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill24overprint") ;
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  do {
    bitfill24overprint(&rb_copy, ys , xs , xe);
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
  } while ( ++ys <= ye ) ;
}

static void blkclip24overprint(render_blit_t *rb, register dcoord ys,
                               register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfill24overprint);
}

static void blkfill24knockout(render_blit_t *rb, register dcoord  ys,
                              register dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = theFormL(*rb_copy.outputform) ;

  HQASSERT(rb->opmode == BLT_OVP_NONE, "Should not be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  do {
    bitfill24knockout(&rb_copy, ys , xs , xe );
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
  } while ( ++ys <= ye ) ;
}

static void blkclip24knockout(render_blit_t *rb, register dcoord ys,
                              register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfill24knockout);
}

/** Self-modifying blits for 24-bit tone block fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void blkfill24(render_blit_t *rb, register dcoord  ys,
                      register dcoord  ye, dcoord xs, dcoord xe)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &slices24[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->blockfn)(rb, ys, ye, xs, xe) ;
}

/* ---------------------------------------------------------------------- */

/** Special block fill for images, optimised better for short runs. */
static inline void blkfill24image(render_blit_t *rb, register dcoord ys,
                                  register dcoord ye, dcoord xs, dcoord xe)
{
  uint8 *bytestart ;
  register int32 wupdate = theFormL(*rb->outputform) ;
  uint32 rgbr ;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill24image") ;
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  HQASSERT(rb->color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->color->map->packed_bits == 24, "Packed color size incorrect") ;
  HQASSERT(rb->opmode == BLT_OVP_NONE, "Should not be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= 3 ; /* bytes to fill */

  xs += rb->x_sep_position ;
  xs *= 3 ; /* byte offset of start pixel */
  bytestart = (uint8 *)rb->ylineaddr + xs ;

  RGB32_INIT(rgbr, rb->color) ;

  do {
    uint8 *byteaddr = bytestart, *byteend = bytestart + xe ;
    register uint32 *wordaddr = (uint32 *)((intptr_t)byteaddr & ~(intptr_t)3) ;
    register uint32 *wordend = (uint32 *)((intptr_t)byteend & ~(intptr_t)3) ;
    register uint32 rgb = rgbr ;

    /* Fill out the odd bytes of the first word, and set up RGB variable for
       the subsequent words. */
    switch ( (uintptr_t)byteaddr & 3 ) {
    case 1: /* xRGB in first word, want RGBR afterwards. */
      byteaddr[2] = (uint8)(rgbr >> B_SHIFT) ;
      RGB32_ROTATE(rgb) ; /* Falling through rotates three times. */
      /*@fallthrough@*/
    case 2: /* xxRG in first word, want BRGB afterwards. */
      byteaddr[1] = (uint8)(rgbr >> G_SHIFT) ;
      RGB32_ROTATE(rgb) ;
      /*@fallthrough@*/
    case 3: /* xxxR in first word, want GBRG afterwards. */
      byteaddr[0] = (uint8)(rgbr >> R_SHIFT) ;
      RGB32_ROTATE(rgb) ;
      ++wordaddr ; /* We've reached the first word boundary. */
      /*@fallthrough@*/
    case 0:
      break ;
    }

    while ( wordaddr < wordend ) {
      *wordaddr++ = rgb ;
      RGB32_ROTATE(rgb) ;
    }

    byteaddr = (uint8 *)wordaddr ;

    /* Fill out the odd bytes of the last word. */
    switch ( (uintptr_t)byteend & 3 ) {
    case 3:
      *byteaddr++ = (uint8)(rgbr >> R_SHIFT) ;
      /*@fallthrough@*/
    case 2:
      *byteaddr++ = (uint8)(rgbr >> G_SHIFT) ;
      /*@fallthrough@*/
    case 1:
      *byteaddr++ = (uint8)(rgbr >> B_SHIFT) ;
      /*@fallthrough@*/
    case 0:
      break ;
    }

    HQASSERT(byteaddr == byteend, "Didn't fill whole span") ;

    bytestart += wupdate ;
  } while ( ++ys <= ye ) ;
}

/** Optimised pixel extracter for 3 channels, 8 bits, additive color
    spaces, forward row order. */
static inline void tone24_additive_3x8_forward(blit_color_t *color,
                                               const void **buffer,
                                               int32 *npixels,
                                               unsigned int nexpanded,
                                               const int blit_to_expanded[])
{
  register const uint8 *current ;
  register int32 remaining ;

  UNUSED_PARAM(unsigned int, nexpanded) ;
  UNUSED_PARAM(const int *, blit_to_expanded) ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 3 &&
           color->map->rendered[0] == 7 &&
           color->map->nrendered == 3 &&
           color->map->packed_bits == 24 &&
           color->map->expanded_bytes == sizeof(blit_t) * 3 &&
           !color->map->apply_properties &&
           color->map->alpha_index >= 3 &&
           channel_is_8bit(color->map, 0) &&
           channel_is_positive(color->map, 0) &&
           channel_is_8bit(color->map, 1) &&
           channel_is_positive(color->map, 1) &&
           channel_is_8bit(color->map, 2) &&
           channel_is_positive(color->map, 2),
           "Not a 3x8 bit positive colormap") ;
  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Quantised screen not set for expander color") ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(nexpanded == 3 &&
           blit_to_expanded[0] == 0 && blit_to_expanded[1] == 1 &&
           blit_to_expanded[2] == 2, "Should be expanding 3x8 pixels") ;

  current = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  color->packed.channels.bytes[0] = current[0] ;
  color->packed.channels.bytes[1] = current[1] ;
  color->packed.channels.bytes[2] = current[2] ;

#ifdef ASSERT_BUILD
  /* We neither quantise nor expand the color. */
  color->valid &= ~(blit_color_quantised|blit_color_expanded) ;
  color->valid |= blit_color_packed ;
#endif

  /* Determine how many pixels were the same by raw byte comparison, then
     adjust the number of pixels based on the number of similar bytes. */
  remaining = 3 * *npixels - 3 ;

  /* This loop should use as few registers as possible for performance; on
     x86 processors, this makes a noticeable difference. */
  while ( remaining != 0 && current[0] == current[3] ) {
    ++current, --remaining ;
  }

  *npixels -= (remaining + 2) / 3 ;
  *buffer = (const uint8 *)*buffer + *npixels * 3 ;
}

/** \fn fill_additive_3x8_rows
    Optimised row fill function for 4 channels, 8 bits, additive color
    spaces, forward expansion, render by row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_additive_3x8_rows
#define PIXEL_FN(p_) tone24_additive_3x8_forward
#define BLOCK_FN blkfill24image /* Explicit block call */
#include "imgfillorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn orth_additive_3x8_rows
    Optimised image callback function for 3x8-bit orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION orth_additive_3x8_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) fill_additive_3x8_rows
#define NOT_HALFTONED
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_additive_3x8_cols
    Optimised row fill function for 4 channels, 8 bits, additive color
    spaces, forward expansion, render by column order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_additive_3x8_cols
#define PIXEL_FN(p_) tone24_additive_3x8_forward
#define BLOCK_FN blkfill24image /* Explicit block call */
#include "imgfillorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn orth_additive_3x8_cols
    Optimised image callback function for 3x8-bit orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION orth_additive_3x8_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) fill_additive_3x8_cols
#define NOT_HALFTONED
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** Optimised 24-bit tone fill for the most common cases. */
static void imageblt24knockout(render_blit_t *rb, imgblt_params_t *params,
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
                              "Tone 24 image function called after other blits") ;

  /* Very specialised fully expanded blit stack for unclipped/rectclipped
     images. The combinations we probably want for this are forward/backward,
     subtractive/additive. */
  if ( params->type == IM_BLIT_IMAGE && !params->out16 &&
       params->converted_comps == 3 && !params->on_the_fly &&
       !map->apply_properties &&
       params->blit_to_expanded[0] == 0 && params->blit_to_expanded[1] == 1 &&
       params->blit_to_expanded[2] == 2 &&
       map->nchannels == 3 && map->alpha_index >= 3 && map->nrendered == 3 &&
       channel_is_positive(map, 0) && channel_is_8bit(map, 0) &&
       channel_is_positive(map, 1) && channel_is_8bit(map, 1) &&
       channel_is_positive(map, 2) && channel_is_8bit(map, 2) ) {
    if ( params->orthogonal ) {
      if ( rb->clipmode != BLT_CLP_COMPLEX ) {
        HQASSERT(!params->wflip && !params->hflip,
                 "Tone24 surface should not be X or Y flipped") ;
        if ( params->geometry->wx != 0 ) {
          *result = orth_additive_3x8_rows(rb, params) ;
        } else {
          *result = orth_additive_3x8_cols(rb, params) ;
        }
        return ;
      }
    }
    /* else rotated image optimisations omitted until proven necessary. */
  }

  imagebltn(rb, params, callback, result) ;
}

/** Image blit selector to pick suitable optimised 24-bit tone fill for
    knockout images. */
static void imageblt24(render_blit_t *rb, imgblt_params_t *params,
                       imgblt_callback_fn *callback,
                       Bool *result)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &slices24[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->imagefn)(rb, params, callback, result) ;
}

/* ---------------------------------------------------------------------- */
static void areahalf24(render_blit_t *rb,  register FORM *formptr )
{
  const blit_color_t *color = rb->color ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_expanded, "Color not expanded") ;
  HQASSERT(color->map->packed_bits == 24, "Packed color size incorrect") ;
  HQASSERT(color->map->expanded_bytes != 0, "Expanded color size incorrect") ;

  if (color->packed.channels.bytes[0] == color->packed.channels.bytes[1] &&
      color->packed.channels.bytes[1] == color->packed.channels.bytes[2] ) {
    /* The usual case - clear to white or black - use a more
       efficient routine */
    HQASSERT(color->packed.channels.bytes[3] == color->packed.channels.bytes[0] &&
             (color->valid & blit_color_expanded),
             "Blit color is not expanded") ;
    HqMemSet32((uint32 *)theFormA(*formptr),
               color->packed.channels.words[0],
               theFormS(*formptr) / sizeof(int32)) ;
  } else {
    /*  do it the long winded way - this may leave odd bytes at the end
        of each scanline, but these will have to be stripped out anyway
        because a partial pixel is pretty meaningless */
    render_blit_t rb_copy = *rb ;

    rb_copy.outputform = formptr;
    rb_copy.ylineaddr = theFormA(*formptr);
    rb_copy.x_sep_position = 0;

    blkfill24knockout(&rb_copy, theFormHOff(*formptr),
                      theFormHOff(*formptr) + theFormRH(*formptr) - 1,
                      0, theFormW(*formptr) - 1);
  }
}

/* ---------------------------------------------------------------------- */

/** Render preparation function for toneblits packs current color. */
static surface_prepare_t render_prepare_24(surface_handle_t handle,
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

/** Blit color packing for 3 channels of 8 bits, positive. */
static void tone24_pack_positive(blit_color_t *color)
{
  register uint8 r, g, b ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 3 &&
           color->map->rendered[0] == 7 &&
           color->map->nrendered == 3 &&
           color->map->packed_bits == 24 &&
           color->map->expanded_bytes == sizeof(blit_t) * 3 &&
           !color->map->apply_properties &&
           channel_is_8bit(color->map, 0) &&
           channel_is_positive(color->map, 0) &&
           channel_is_8bit(color->map, 1) &&
           channel_is_positive(color->map, 1) &&
           channel_is_8bit(color->map, 2) &&
           channel_is_positive(color->map, 2),
           "Not a 3x8 bit positive colormap") ;

  HQASSERT(color->quantised.qcv[0] <= 255 ||
           (color->state[0] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;
  HQASSERT(color->quantised.qcv[1] <= 255 ||
           (color->state[1] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;
  HQASSERT(color->quantised.qcv[2] <= 255 ||
           (color->state[2] & blit_channel_present) == 0,
           "Quantised colorvalue doesn't fit in a byte") ;

  r = (uint8)color->quantised.qcv[0] ;
  g = (uint8)color->quantised.qcv[1] ;
  b = (uint8)color->quantised.qcv[2] ;

  /* Just store directly into expanded locations. */
  color->packed.channels.bytes[0] = color->packed.channels.bytes[3] =
    color->packed.channels.bytes[6] = color->packed.channels.bytes[9] = r ;
  color->packed.channels.bytes[1] = color->packed.channels.bytes[4] =
    color->packed.channels.bytes[7] = color->packed.channels.bytes[10] = g ;
  color->packed.channels.bytes[2] = color->packed.channels.bytes[5] =
    color->packed.channels.bytes[8] = color->packed.channels.bytes[11] = b ;

#if BLIT_WIDTH_BYTES > 4
  color->packed.channels.words[3] = color->packed.channels.words[0] ;
  color->packed.channels.words[4] = color->packed.channels.words[1] ;
  color->packed.channels.words[5] = color->packed.channels.words[2] ;
#endif

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Blit color packing for 3 channels of 8 bits, any other mapping. 24-bit
    always needs expansion. */
static void tone24_pack_other(blit_color_t *color)
{
  blit_color_pack_generic8(color) ;

  HQASSERT(color->valid & blit_color_packed, "Blit color is not packed") ;
  HQASSERT(color->map->packed_bits == 24, "Packed size unexpected") ;
  HQASSERT(color->map->expanded_bytes == sizeof(blit_t) * 3,
           "Expanded size unexpected") ;

  color->packed.channels.bytes[3] = color->packed.channels.bytes[6] =
    color->packed.channels.bytes[9] = color->packed.channels.bytes[0] ;
  color->packed.channels.bytes[4] = color->packed.channels.bytes[7] =
    color->packed.channels.bytes[10] = color->packed.channels.bytes[1] ;
  color->packed.channels.bytes[5] = color->packed.channels.bytes[8] =
    color->packed.channels.bytes[11] = color->packed.channels.bytes[2] ;

#if BLIT_WIDTH_BYTES > 4
  color->packed.channels.words[3] = color->packed.channels.words[0] ;
  color->packed.channels.words[4] = color->packed.channels.words[1] ;
  color->packed.channels.words[5] = color->packed.channels.words[2] ;
#endif

#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

/** Expanding the color is a no-op because the pack routine already did
    it. */
static void tone24_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_packed, "Blit color not packed") ;
  HQASSERT(color->valid & blit_color_expanded, "Blit color not expanded") ;
}

static void tone24_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->packed_bits == 24 && map->nchannels == 3 &&
           channel_is_8bit(map, 0) && channel_is_8bit(map, 1) &&
           channel_is_8bit(map, 2),
           "Not a suitable 24-bit blit map") ;

  map->pack_quantised_color = tone24_pack_other ;
  map->expand_packed_color = tone24_color_expand ;

  /* Cannot use optimised pack routine if properties are being used, because
     the presence of channels can be changed. */
  if ( !map->apply_properties && map->nrendered == 3 ) {
    if ( channel_is_positive(map, 0) && channel_is_positive(map, 1) &&
         channel_is_positive(map, 2) ) {
      map->pack_quantised_color = tone24_pack_positive ;
    }
  }
}

/** Pagedevice /Private dictionary for tone24 surface selection. */
const static sw_datum tone24_private[] = {
  /* Comparing integer to array auto-coerces to the length. */
  SW_DATUM_STRING("ColorChannels"), SW_DATUM_INTEGER(3),
} ;

/** Alternative PackingUnitRequests for tone24 surface. */
const static sw_datum tone24_packing[] = {
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

/** Pagedevice dictionary for tone24 surface selection. */
const static sw_datum tone24_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"), SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_PIXEL),
  SW_DATUM_STRING("PackingUnitRequest"),
    SW_DATUM_ARRAY(&tone24_packing[0], SW_DATA_ARRAY_LENGTH(tone24_packing)),
  /** \todo ajcd 2009-09-23: We can only use Private /ColorChannels as a
     selection key for this because pixel-interleaved currently cannot add
     new channels. Therefore, gucr_framesChannelsTotal(gucr_framesStart()) is
     always its original value of 3, as initialised from ColorChannels. */
  SW_DATUM_STRING("Private"),
    SW_DATUM_DICT(&tone24_private[0], SW_DATA_DICT_LENGTH(tone24_private)),
#if 0
  /** \todo ajcd 2009-09-23: This should probably be used, but wasn't part
      of the original selection criteria. There are inbuilt assumptions about
      min/max blitting that make this only suitable for RGB. */
  SW_DATUM_STRING("ProcessColorModel"), SW_DATUM_STRING("DeviceRGB"),
#endif
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(256),
} ;

/** RGB 8bpc pixel interleaved surface optimisation. */
static surface_set_t tone24_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&tone24_dict[0],
                                 SW_DATA_DICT_LENGTH(tone24_dict))) ;

/** The RGB8 surface description. */
static surface_t tone24 = SURFACE_INIT ;
static const surface_t *indexed[N_SURFACE_TYPES] ;

void init_toneblt_24(void)
{
  /* Knockout optimised slice. */
  slices24[0][BLT_CLP_NONE].spanfn =
    slices24[0][BLT_CLP_RECT].spanfn = bitfill24knockout ;
  slices24[0][BLT_CLP_COMPLEX].spanfn = bitclip24knockout ;

  slices24[0][BLT_CLP_NONE].blockfn =
    slices24[0][BLT_CLP_RECT].blockfn = blkfill24knockout ;
  slices24[0][BLT_CLP_COMPLEX].blockfn = blkclip24knockout ;

  slices24[0][BLT_CLP_NONE].charfn =
    slices24[0][BLT_CLP_RECT].charfn =
    slices24[0][BLT_CLP_COMPLEX].charfn = charbltn ;

  slices24[0][BLT_CLP_NONE].imagefn =
    slices24[0][BLT_CLP_RECT].imagefn =
    slices24[0][BLT_CLP_COMPLEX].imagefn = imageblt24knockout ;

  /* Overprint optimised slice. */
  slices24[1][BLT_CLP_NONE].spanfn =
    slices24[1][BLT_CLP_RECT].spanfn = bitfill24overprint ;
  slices24[1][BLT_CLP_COMPLEX].spanfn = bitclip24overprint ;

  slices24[1][BLT_CLP_NONE].blockfn =
    slices24[1][BLT_CLP_RECT].blockfn = blkfill24overprint ;
  slices24[1][BLT_CLP_COMPLEX].blockfn = blkclip24overprint ;

  slices24[1][BLT_CLP_NONE].charfn =
    slices24[1][BLT_CLP_RECT].charfn =
    slices24[1][BLT_CLP_COMPLEX].charfn = charbltn ;

  slices24[1][BLT_CLP_NONE].imagefn =
    slices24[1][BLT_CLP_RECT].imagefn =
    slices24[1][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Base blits */
  tone24.baseblits[BLT_CLP_NONE].spanfn =
    tone24.baseblits[BLT_CLP_RECT].spanfn =
    tone24.baseblits[BLT_CLP_COMPLEX].spanfn = bitfill24 ;

  tone24.baseblits[BLT_CLP_NONE].blockfn =
    tone24.baseblits[BLT_CLP_RECT].blockfn =
    tone24.baseblits[BLT_CLP_COMPLEX].blockfn = blkfill24 ;

  tone24.baseblits[BLT_CLP_NONE].charfn =
    tone24.baseblits[BLT_CLP_RECT].charfn =
    tone24.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  tone24.baseblits[BLT_CLP_NONE].imagefn =
    tone24.baseblits[BLT_CLP_RECT].imagefn =
    tone24.baseblits[BLT_CLP_COMPLEX].imagefn = imageblt24 ;

  /* No object map on the side blits for tone24 */

  /* Max blits */
  tone24.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    tone24.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn = bitfill24max ;
  tone24.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = bitclip24max ;

  tone24.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    tone24.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone24.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone24.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    tone24.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn =
    tone24.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone24.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    tone24.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    tone24.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  tone24.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].spanfn =
    tone24.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].spanfn = bitfill24min ;
  tone24.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].spanfn = bitclip24min ;

  tone24.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].blockfn =
    tone24.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone24.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone24.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].charfn =
    tone24.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].charfn =
    tone24.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone24.maxblits[BLT_MAX_MIN][BLT_CLP_NONE].imagefn =
    tone24.maxblits[BLT_MAX_MIN][BLT_CLP_RECT].imagefn =
    tone24.maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No ROP blits for tone24 */

  init_pcl_pattern_blit(&tone24) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&tone24) ;
  surface_pattern_builtin(&tone24) ;
  surface_gouraud_builtin_tone_multi(&tone24) ;

  tone24.areafill = areahalf24 ;
  tone24.prepare = render_prepare_24 ;
  tone24.blit_colormap_optimise = tone24_blitmap_optimise ;

  tone24.n_rollover = 3 ;
  tone24.screened = FALSE ;

  builtin_clip_N_surface(&tone24, indexed) ;

  /* The surface we've just completed is part of a set implementing 8 bpc
     RGB output. Add it and all of the associated surfaces to the surface
     array for this set. */
  tone24_set.indexed = indexed ;
  tone24_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

  indexed[SURFACE_OUTPUT] = &tone24 ;

  /* Add trapping surfaces. */
  surface_set_trap_builtin(&tone24_set, indexed);

  surface_set_transparency_builtin(&tone24_set, &tone24, indexed) ;

  /* Now that we've filled in the tone24 surface description, hook it up so
     that it can be found. */
  surface_set_register(&tone24_set) ;
}

/* Log stripped */
