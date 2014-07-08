/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:toneblt8.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for 8-bit separations or band/frame interleaved output.
 */

#include "core.h"
#include "often.h"
#include "objnamer.h"

#include "render.h"    /* x_sep_position */
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "toneblt.h"
#include "toneblt8.h"
#include "pcl5Blit.h"
#include "pclPatternBlit.h"
#include "hqmemset.h"
#include "gu_chan.h"
#include "control.h"
#include "imexpand.h"
#include "imgblts.h"
#include "imageo.h"
#include "builtin.h"
#include "rlecache.h"
#include "interrupts.h"

static inline void tone8_pack_negative(blit_color_t *color) ;

/**
 * Set all the 8bit pixels in the given range to the given value
 */
static void bitfill8(render_blit_t *rb, dcoord y, register dcoord xs,
                     register dcoord xe)
{
  const blit_color_t *color = rb->color;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill8" );

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 8, "Packed color size incorrect") ;

  xe = xe - xs + 1 ; /* total bytes to fill */
  xs += rb->x_sep_position;
  HqMemSet8((uint8 *)rb->ylineaddr + xs, color->packed.channels.bytes[0], xe);
}

/* ---------------------------------------------------------------------- */
static void bitfillmax8(render_blit_t *rb,
                        dcoord y , register dcoord xs , register dcoord xe)
{
  register uint8 *byteptr ;
  register int32 v;
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillmax8" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 8, "Packed color size incorrect") ;

  xe = xe - xs + 1 ; /* total bytes to fill */
  xs += rb->x_sep_position ;
  v = color->packed.channels.bytes[0] ;
  byteptr = ( uint8 * )rb->ylineaddr + xs ;

  do {
    register int32 max32 ;
    INLINE_MAX32(max32, v, (int32)*byteptr) ;
    *byteptr++ = (uint8)max32 ;
  } while ((--xe)  != 0);
}



/* ---------------------------------------------------------------------- */
static void bitclipmax8(render_blit_t *rb,
                        dcoord y , register dcoord xs , register dcoord xe )
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipmax8" ) ;

  bitclipn(rb, y , xs , xe , bitfillmax8 ) ;
}

/* ---------------------------------------------------------------------- */
static void blkfillmax8(render_blit_t *rb, register dcoord ys,
                        register dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = theFormL(*rb_copy.outputform) ;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillmax8" ) ;

  do {
    bitfillmax8(&rb_copy, ys , xs , xe ) ;
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate) ;
  } while ( ++ys <= ye ) ;
}

/* ---------------------------------------------------------------------- */
static void blkclipmax8(render_blit_t *rb, register dcoord ys,
                        register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfillmax8) ;
}

/* ---------------------------------------------------------------------- */
static void bitclip8(render_blit_t *rb,
                     dcoord y , register dcoord xs , register dcoord xe )
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip8" ) ;

  bitclipn(rb, y , xs , xe , bitfill8 ) ;
}

/**
 * Fill a rectangular block with a given 8bit color value.
 *
 * This can be implemented just as repeated calls to the line
 * version bitfill8(). But for performance reasons it is more efficient
 * to effectively pull that inline and re-arrange things to avoid any
 * repeated calculations.
 */
static void blkfill8(render_blit_t *rb, register dcoord ys,
                     register dcoord ye, dcoord xs, dcoord xe)
{
  register int32 wupdate ;
  register uint8 value;
  register uint8 *ptr;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill8");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  HQASSERT((rb->color->valid & blit_color_packed) != 0,
           "Packed color not set for span") ;
  HQASSERT(rb->color->map->packed_bits == 8, "Packed color size incorrect") ;

  xe = xe - xs + 1; /* total bytes to fill */
  ptr = (uint8 *)rb->ylineaddr + xs + rb->x_sep_position;
  ye = ye - ys ; /* one less than the total lines to fill */

  value = rb->color->packed.channels.bytes[0];
  wupdate = rb->outputform->l;
  do {
    HqMemSet8(ptr, value, xe);
    ptr += wupdate;
  } while ( --ye >= 0 ) ;
}

/* ---------------------------------------------------------------------- */
static void blkclip8(render_blit_t *rb, register dcoord ys,
                     register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfill8) ;
}


static void bitfillom8(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  const blit_color_t *color = rb->color ;
  const render_info_t *p_ri = rb->p_ri ;
  uint8 *omptr ;
  uint8 label = CAST_UNSIGNED_TO_UINT8(color->quantised.qcv[color->map->type_index]) ;

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillom8");

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 8, "Packed color size incorrect") ;

  /* Write in the same position on omform as the on the actual raster */
  omptr = (uint8 *)BLIT_ADDRESS(p_ri->p_rs->forms->objectmapform.addr,
                                (uint8 *)rb->ylineaddr - (uint8 *)rb->outputform->addr);

  if ( rb->maxmode == BLT_MAX_NONE ) {
    HqMemSet8(omptr + xs + rb->x_sep_position, label, xe - xs + 1);
  } else {
    uint8 v = color->packed.channels.bytes[0] ;
    dcoord len = xe - xs + 1; /* total pixels to fill */
    uint8 *outptr = (uint8 *)rb->ylineaddr + xs + rb->x_sep_position ;

    do {
      if ( *outptr <= v ) /* if equal, prefer top object */
        *omptr = label;
      ++outptr; ++omptr;
    } while (--len != 0);
  }

  if (!p_ri->p_rs->page->output_object_map)
    DO_SPAN(rb, y, xs, xe);
}

static void bitclipom8(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipom8");

  bitclipn(rb, y, xs, xe, bitfillom8);
}

static void bitorom8(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  FORM *omform = &rb->p_ri->p_rs->forms->objectmapform;
  const blit_color_t *color = rb->color ;
  blit_t *ylineaddr;

  UNUSED_PARAM(dcoord, y);
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitorom8");

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 8, "Packed color size incorrect") ;

  /* Write in the same position on omform as the on the actual raster */
  ylineaddr = BLIT_ADDRESS(omform->addr,
                           (uint8 *)rb->ylineaddr
                           - (uint8 *)rb->outputform->addr);
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Maxblitting the object map");
  {
    uint8 *omptr;
    uint8 label = CAST_UNSIGNED_TO_UINT8(color->quantised.qcv[color->map->type_index]) ;
    dcoord len = xe - xs + 1; /* total bytes to fill */

    xs += rb->x_sep_position;
    omptr = ( uint8 * )ylineaddr + xs;
    do {
      *omptr |= label;
      omptr++;
    } while (--len != 0);
  }
  /* Don't dispatch to the base blit, this is only used for om output. */
}

static void bitcliporom8(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitcliporom8");

  bitclipn(rb, y, xs, xe, bitorom8);
}

#ifdef BLIT_CONTONE_8
/* Added code size for optimised images only if we're using this for an
   output surface. */

/* Use include templates to generate a blit function fully optimised for
   one negative channel; this covers the 8-bit CMYK band/frame interleaved
   case. */

/** \fn tone8_subtractive_1x8_forward
    Optimised pixel extracter for 1 channel, 8 bits, subtractive color
    spaces, forward expansion order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION tone8_subtractive_1x8_forward
#define EXPAND_BITS 8
#define BLIT_COLOR_PACK tone8_pack_negative /* Explicit pack call */
#include "imgpixels1f.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_subtractive_1x8_rows
    Optimised row fill function for 1 channel, 8 bits, subtractive color
    spaces, forward expansion, render by row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_subtractive_1x8_rows
#define PIXEL_FN(p_) tone8_subtractive_1x8_forward
#define BLOCK_FN blkfill8 /* Explicit block call */
#include "imgfillorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn orth_subtractive_1x8_rows
    Optimised image callback function for 8-bit images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION orth_subtractive_1x8_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) fill_subtractive_1x8_rows
#define NOT_HALFTONED
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_subtractive_1x8_cols
    Optimised row fill function for 1 channel, 8 bits, subtractive color
    spaces, forward expansion, render by column order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_subtractive_1x8_cols
#define PIXEL_FN(p_) tone8_subtractive_1x8_forward
#define BLOCK_FN blkfill8 /* Explicit block call */
#include "imgfillorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn orth_subtractive_1x8_cols
    Optimised image callback function for 8-bit orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION orth_subtractive_1x8_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) fill_subtractive_1x8_cols
#define NOT_HALFTONED
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rtfill_subtractive_1x8_forward
    Optimised row fill function for 1 channel, 8 bits, subtractive color
    spaces, forward row order, rotated tiles. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rtfill_subtractive_1x8_forward
#define PIXEL_FN(p_) tone8_subtractive_1x8_forward
#define RENDER_IMAGE_TILE CHAR_IMAGE_TILE
#include "imgfillrt.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rot_subtractive_1x8_forward
    Optimised image callback function for 8-bit rotated tiled images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rot_subtractive_1x8_forward
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) rtfill_subtractive_1x8_forward
#define NOT_HALFTONED
#include "imgbltrot.h"
#endif /* !DOXYGEN_SKIP */

/** \fn tone8_subtractive_1x8_backward
    Optimised pixel extracter for 1 channel, 8 bits, subtractive color
    spaces, backward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION tone8_subtractive_1x8_backward
#define EXPAND_BITS 8
#define BLIT_COLOR_PACK tone8_pack_negative /* Explicit pack call */
#include "imgpixels1b.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rtfill_subtractive_1x8_backward
    Optimised row fill function for 1 channel, 8 bits, subtractive color
    spaces, backward row order, rotated tiles. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rtfill_subtractive_1x8_backward
#define PIXEL_FN(p_) tone8_subtractive_1x8_backward
#define RENDER_IMAGE_TILE CHAR_IMAGE_TILE
#include "imgfillrt.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rot_subtractive_1x8_backward
    Optimised image callback function for 8-bit rotated tiled images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rot_subtractive_1x8_backward
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) rtfill_subtractive_1x8_backward
#define NOT_HALFTONED
#include "imgbltrot.h"
#endif /* !DOXYGEN_SKIP */

#endif /* BLIT_CONTONE_8 */

/** Optimised 8-bit tone fill for the most common cases. */
static void imageblt8(render_blit_t *rb, imgblt_params_t *params,
                      imgblt_callback_fn *callback,
                      Bool *result)
{
#ifdef BLIT_CONTONE_8
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
                              "Tone 8 image function called after other blits") ;

  /* Very specialised fully expanded blit stack for unclipped/rectclipped
     images. The combinations we probably want for this are forward/backward,
     subtractive/additive. */
  if ( params->type == IM_BLIT_IMAGE && !params->out16 &&
       params->one_color_channel && !params->on_the_fly &&
       !map->apply_properties && channel_is_negative8(map, 0) ) {
    if ( params->orthogonal ) {
      HQASSERT(!params->wflip && !params->hflip,
               "Tone8 surface should not be X or Y flipped") ;
      if ( rb->clipmode != BLT_CLP_COMPLEX ) {
        if ( params->geometry->wx != 0 ) {
          *result = orth_subtractive_1x8_rows(rb, params) ;
        } else {
          *result = orth_subtractive_1x8_cols(rb, params) ;
        }
        return ;
      }
    } else { /* rotated, 8-bit, one subtractive color channel image. */
      if ( params->tiles ) {
        if ( params->dcol < 0 ) {
          *result = rot_subtractive_1x8_backward(rb, params) ;
        } else {
          *result = rot_subtractive_1x8_forward(rb, params) ;
        }
        return ;
      }
    }
  }
#endif /* BLIT_CONTONE_8 */

  imagebltn(rb, params, callback, result) ;
}

/**
 * Set all the 8bit pixels in a given form to a given value
 */
static void areahalf8(render_blit_t *rb,  FORM *formptr )
{
  blit_color_t *color = rb->color ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->map->packed_bits == 8, "Packed color size incorrect") ;
  HQASSERT(color->map->expanded_bytes != 0, "Expanded color size incorrect") ;

  blit_color_expand(color) ;

  BlitSet(theFormA(*formptr), color->packed.channels.blits[0],
          theFormS(*formptr) >> BLIT_SHIFT_BYTES);
}

/** Render preparation function for toneblits packs current color. */
static surface_prepare_t render_prepare_8(surface_handle_t handle,
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

/** Blit color packing for 1 channel of 8 bit, positive. */
static void tone8_pack_positive(blit_color_t *color)
{
  blit_t v ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 1 &&
           color->map->rendered[0] == 1 &&
           color->map->nrendered == 1 &&
           color->map->packed_bits == 8 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           !color->map->apply_properties &&
           channel_is_8bit(color->map, 0) &&
           channel_is_positive(color->map, 0),
           "Not a 1x8 bit positive colormap") ;

  /* Quicker to re-pack and expand in one go. */
  HQASSERT(color->quantised.qcv[0] <= 255,
           "Quantised colorvalue doesn't fit in a byte") ;
  v = color->quantised.qcv[0] ;
  v |= v << 8 ;
  v |= v << 16 ;
#if BLIT_WIDTH_BYTES > 4
  v |= v << 32 ;
#endif
  color->packed.channels.blits[0] = v ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Blit color packing for 1 channel of 8 bit, negative. */
static inline void tone8_pack_negative(blit_color_t *color)
{
  blit_t v ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 1 &&
           color->map->rendered[0] == 1 &&
           color->map->nrendered == 1 &&
           color->map->packed_bits == 8 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           !color->map->apply_properties &&
           channel_is_8bit(color->map, 0) &&
           channel_is_negative8(color->map, 0),
           "Not a 1x8 bit negative colormap") ;

  /* Quicker to re-pack and expand in one go. */
  HQASSERT(color->quantised.qcv[0] <= 255,
           "Quantised colorvalue doesn't fit in a byte") ;
  v = color->quantised.qcv[0] ;
  v |= v << 8 ;
  v |= v << 16 ;
#if BLIT_WIDTH_BYTES > 4
  v |= v << 32 ;
#endif
  color->packed.channels.blits[0] = ~v ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Expanding the color is a no-op because the pack routine already did
    it. */
static void tone8_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_packed, "Blit color not packed") ;
  HQASSERT(color->valid & blit_color_expanded, "Blit color not expanded") ;
}

static void tone8_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->packed_bits == 8 && map->nchannels == 1 &&
           channel_is_8bit(map, 0),
           "Not a suitable 8-bit blit map") ;

  /* We have several options for 8-bit. We only use these if not applying
     properties, because properties may affect the rendered state of
     channels. */
  if ( !map->apply_properties && map->nrendered == 1 ) {
    /* One 8-bit rendered channel. Is it positive or negative? */
    if ( channel_is_positive(map, 0) ) { /* Additive */
      map->pack_quantised_color = tone8_pack_positive ;
    } else if ( channel_is_negative8(map, 0) ) { /* Subtractive */
      map->pack_quantised_color = tone8_pack_negative ;
    }
    map->expand_packed_color = tone8_color_expand ;
  }
}

/** Alternative InterleavingStyles for tone8 surface; i.e., not pixel
    interleaved. */
const static sw_datum tone8_interleave[] = {
  SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
  SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_BAND),
  SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_FRAME),
} ;

/** Pagedevice match for tone8 surface. */
const static sw_datum tone8_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"),
    SW_DATUM_ARRAY(&tone8_interleave[0], SW_DATA_ARRAY_LENGTH(tone8_interleave)),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("ValuesPerComponent"), SW_DATUM_INTEGER(256),
} ;

/** The 8-bit surface set. */
static surface_set_t tone8_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&tone8_dict[0],
                                 SW_DATA_DICT_LENGTH(tone8_dict))) ;

/** The 8-bit surface. */
static surface_t tone8 = SURFACE_INIT ;
static const surface_t *indexed[N_SURFACE_TYPES] ;

void init_toneblt_8(void)
{
  /* Base blits */
  tone8.baseblits[BLT_CLP_NONE].spanfn =
    tone8.baseblits[BLT_CLP_RECT].spanfn = bitfill8 ;
  tone8.baseblits[BLT_CLP_COMPLEX].spanfn = bitclip8 ;

  tone8.baseblits[BLT_CLP_NONE].blockfn =
    tone8.baseblits[BLT_CLP_RECT].blockfn = blkfill8 ;
  tone8.baseblits[BLT_CLP_COMPLEX].blockfn = blkclip8 ;

  tone8.baseblits[BLT_CLP_NONE].charfn =
    tone8.baseblits[BLT_CLP_RECT].charfn =
    tone8.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  tone8.baseblits[BLT_CLP_NONE].imagefn =
    tone8.baseblits[BLT_CLP_RECT].imagefn =
    tone8.baseblits[BLT_CLP_COMPLEX].imagefn = imageblt8 ;

  /* Object map on the side blits */
  tone8.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].spanfn =
    tone8.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].spanfn = bitfillom8 ;
  tone8.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].spanfn = bitclipom8 ;

  tone8.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].blockfn =
    tone8.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone8.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone8.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].charfn =
    tone8.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].charfn =
    tone8.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone8.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].imagefn =
    tone8.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].imagefn =
    tone8.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  tone8.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].spanfn =
    tone8.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].spanfn = bitorom8 ;
  tone8.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].spanfn = bitcliporom8 ;

  tone8.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].blockfn =
    tone8.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone8.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone8.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].charfn =
    tone8.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].charfn =
    tone8.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone8.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].imagefn =
    tone8.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].imagefn =
    tone8.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Max blits; no min blits for single-channel tone 8 */
  tone8.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    tone8.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn = bitfillmax8 ;
  tone8.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = bitclipmax8 ;

  tone8.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    tone8.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillmax8 ;
  tone8.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipmax8 ;

  tone8.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    tone8.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn =
    tone8.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone8.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    tone8.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    tone8.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  init_pcl_pattern_blit(&tone8) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&tone8) ;
  surface_pattern_builtin(&tone8) ;
  surface_gouraud_builtin_tone(&tone8) ;

  tone8.areafill = areahalf8 ;
  tone8.prepare = render_prepare_8 ;
  tone8.blit_colormap_optimise = tone8_blitmap_optimise ;

  tone8.n_rollover = 3 ;
  tone8.screened = FALSE ;

  builtin_clip_N_surface(&tone8, indexed) ;

  /* The surface we've just completed is part of a set implementing 8-bit
     contone output. Add it and all of the associated surfaces to the surface
     array for this set. */
  tone8_set.indexed = indexed ;
  tone8_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

  indexed[SURFACE_OUTPUT] = &tone8 ;

  /* trapping surfaces. */
  surface_set_trap_builtin(&tone8_set, indexed);

  surface_set_transparency_builtin(&tone8_set, &tone8, indexed) ;

#ifdef BLIT_CONTONE_8
  /* Now that we've filled in the tone8 surface set, hook it up so
     that it can be found. */
  surface_set_register(&tone8_set) ;
#endif
}

void surface_set_mht_ff_builtin(surface_set_t *set, const surface_t *indexed[])
{
  UNUSED_PARAM(surface_set_t *, set) ;
  HQASSERT(set, "No surface set to initialise") ;
  HQASSERT(set->indexed, "No surface array in set") ;
  HQASSERT(set->indexed == indexed, "Surface array inconsistent") ;
  HQASSERT(set->n_indexed > SURFACE_MHT_CONTONE_FF &&
           set->n_indexed > SURFACE_MHT_MASK &&
           set->n_indexed > SURFACE_OUTPUT,
           "Surface array too small") ;
  HQASSERT(set->indexed[SURFACE_OUTPUT] != NULL,
           "No output surface defined for surface set") ;
  HQASSERT(set->indexed[SURFACE_MHT_MASK] != NULL,
           "No MHT mask surface defined for surface set") ;
  HQASSERT(set->indexed[SURFACE_MHT_CONTONE_FF] == NULL ||
           set->indexed[SURFACE_MHT_CONTONE_FF] == &tone8,
           "MHT 8-bit surface already initialised") ;
  indexed[SURFACE_MHT_CONTONE_FF] = &tone8 ;
}

/* Log stripped */
