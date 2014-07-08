/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:toneblt16.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for 16-bit separations or band/frame interleaved output.
 */

#include "core.h"

#include "render.h"    /* x_sep_position */
#include "objnamer.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "toneblt.h"
#include "toneblt16.h"
#include "pclPatternBlit.h"
#include "hqmemset.h"
#include "hqbitops.h"
#include "gu_chan.h"
#include "builtin.h"


static void bitfillmax16(render_blit_t *rb,
                         dcoord y , register dcoord xs , register dcoord xe)
{
  register uint16 *dptr ;
  register int32 v;

  UNUSED_PARAM(dcoord, y);
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillmax16" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->color->map->packed_bits == 16, "Packed color size incorrect") ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xs += rb->x_sep_position ;
  v = rb->color->packed.channels.shorts[0] ;
  dptr = (uint16 *)rb->ylineaddr + xs ;

  do {
    register int32 max32 ;
    INLINE_MAX32(max32, v, (int32)*dptr) ;
    *dptr++ = (uint16)max32 ;
  } while ( --xe != 0 );
}


static void blkclipmax16(render_blit_t *rb, register dcoord ys,
                         register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfillmax16) ;
}


static void blkfillmax16(render_blit_t *rb, register dcoord ys,
                         register dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = theFormL(*rb_copy.outputform) ;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillmax16" ) ;

  do {
    bitfillmax16(&rb_copy, ys , xs , xe ) ;
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
  } while ( ++ys <= ye ) ;
}


static void bitclipmax16(render_blit_t *rb,
                         dcoord y , register dcoord xs , register dcoord xe )
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipmax16" ) ;

  bitclipn(rb, y, xs, xe, bitfillmax16);
}


/**
 * Set a span of 16bit wide pixels to the given value
 */
static void bitfill16(render_blit_t *rb, dcoord y, register dcoord xs,
                      register dcoord xe)
{
  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill16" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(rb->color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->color->map->packed_bits == 16, "Packed color size incorrect") ;

  xe = xe - xs + 1 ; /* total dots to fill */
  xs += rb->x_sep_position ;
  HqMemSet16((uint16 *)rb->ylineaddr + xs,
             rb->color->packed.channels.shorts[0], xe);
}

/**
 * Set a block of 16bit wide pixels to the given value
 */
static void blkfill16(render_blit_t *rb, register dcoord ys,
                      register dcoord ye, dcoord xs, dcoord xe)
{
  register int32 wupdate = rb->outputform->l;
  const blit_color_t *color = rb->color;
  register uint16 value;
  uint16 *ptr;

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill16");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap");

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 16, "Packed color size incorrect") ;

  xe = xe - xs + 1; /* total shorts to fill */
  xs += rb->x_sep_position;
  value = color->packed.channels.shorts[0];
  ptr = (uint16 *)rb->ylineaddr + xs;

  do {
    HqMemSet16(ptr, value, xe);
    ptr = (uint16 *)BLIT_ADDRESS(ptr, wupdate);
  } while ( ++ys <= ye ) ;
}

/**
 * Set all the 16bit pixels in the given form to a given value
 */
static void areahalf16(render_blit_t *rb, FORM *formptr )
{
  blit_color_t *color = rb->color ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_expanded, "Blit color not expanded") ;
  HQASSERT(color->map->packed_bits == 16, "Packed color size incorrect") ;
  HQASSERT(color->map->expanded_bytes != 0, "Expanded color size incorrect");

  BlitSet(theFormA(*formptr), color->packed.channels.blits[0],
          theFormS(*formptr) >> BLIT_SHIFT_BYTES);
}

static void bitclip16(render_blit_t *rb,
                      dcoord y , register dcoord xs , register dcoord xe )
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip16" ) ;

  bitclipn(rb, y, xs , xe , bitfill16);
}


static void blkclip16(render_blit_t *rb, register dcoord ys,
                      register dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn(rb, ys, ye, xs, xe, bitfill16) ;
}


static void bitfillom16(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  const blit_color_t *color = rb->color ;
  const render_info_t *p_ri = rb->p_ri ;
  uint16 *omptr ;

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillom16");

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 16, "Packed color size incorrect") ;

  /* Write in the same position on omform as the on the actual raster */
  omptr = (uint16 *)BLIT_ADDRESS(p_ri->p_rs->forms->objectmapform.addr,
                                 (uint8 *)rb->ylineaddr - (uint8 *)rb->outputform->addr);

  if ( rb->maxmode == BLT_MAX_NONE ) {
    uint16 v = color->quantised.qcv[color->map->type_index] ;
    HqMemSet16(omptr + xs + rb->x_sep_position, v, xe - xs + 1);
  } else {
    uint16 v = color->packed.channels.shorts[0] ;
    uint16 label = color->quantised.qcv[color->map->type_index] ;
    dcoord len = xe - xs + 1; /* total pixels to fill */
    uint16 *outptr = (uint16 *)rb->ylineaddr + xs + rb->x_sep_position ;

    do {
      if ( *outptr <= v )
        *omptr = label ; /* if equal, prefer top object */
      ++outptr; ++omptr;
    } while (--len != 0);
  }

  if (!p_ri->p_rs->page->output_object_map)
    DO_SPAN(rb, y, xs, xe);
}


static void bitorom16(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  FORM *omform = &rb->p_ri->p_rs->forms->objectmapform;
  const blit_color_t *color = rb->color ;
  blit_t *ylineaddr;

  UNUSED_PARAM(dcoord, y);
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitorom16");

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(color->map->packed_bits == 16, "Packed color size incorrect") ;

  /* Write in the same position on omform as the on the actual raster */
  ylineaddr = BLIT_ADDRESS(omform->addr,
                           (uint8 *)rb->ylineaddr
                           - (uint8 *)rb->outputform->addr);
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Maxblitting the object map");
  {
    uint16 *omptr;
    uint16 label = color->quantised.qcv[color->map->type_index] ;
    dcoord len = xe - xs + 1; /* total pixels to fill */

    xs += rb->x_sep_position;
    omptr = ( uint16 * )ylineaddr + xs;
    do {
      *omptr |= label;
      omptr++;
    } while (--len != 0);
  }
  /* Don't dispatch to the base blit, this is only used for om output. */
}

static void bitclipom16(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipom16");

  bitclipn(rb, y, xs, xe, bitfillom16);
}

static void bitcliporom16(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitcliporom16");

  bitclipn(rb, y, xs, xe, bitorom16);
}

/** Render preparation function for toneblits packs current color. */
static surface_prepare_t render_prepare_16(surface_handle_t handle,
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

/** Blit color packing for 1 channel of pseudo 16 bits, positive. */
static void tone16_pack_positive(blit_color_t *color)
{
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
           color->map->packed_bits == 16 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           !color->map->apply_properties &&
           channel_is_16bit(color->map, 0) &&
           channel_is_positive(color->map, 0),
           "Not a 1x16 bit positive colormap") ;

  /* Quicker to re-pack and expand at once. */
  HQASSERT(color->quantised.qcv[0] <= COLORVALUE_MAX,
           "Quantised colorvalue doesn't fit in a COLORVALUE") ;
  color->packed.channels.shorts[0] =
    color->packed.channels.shorts[1] = color->quantised.qcv[0] ;
#if BLIT_WIDTH_BYTES > 4
  color->packed.channels.words[1] = color->packed.channels.words[0] ;
#endif
#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Blit color packing for 1 channel of pseudo 16 bits, negative. */
static void tone16_pack_negative(blit_color_t *color)
{
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
           color->map->packed_bits == 16 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           !color->map->apply_properties &&
           channel_is_16bit(color->map, 0) &&
           channel_is_negative16(color->map, 0),
           "Not a 1x16 bit negative colormap") ;

  /* Quicker to re-pack and expand at once. */
  HQASSERT(color->quantised.qcv[0] <= COLORVALUE_MAX,
           "Quantised colorvalue doesn't fit in a COLORVALUE") ;

  color->packed.channels.shorts[0] = color->packed.channels.shorts[1] =
    COLORVALUE_MAX - color->quantised.qcv[0] ;
#if BLIT_WIDTH_BYTES > 4
  color->packed.channels.words[1] = color->packed.channels.words[0] ;
#endif
#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Blit color packing for 1 channel of pseudo 16 bits, with properties. */
static void tone16_pack_other(blit_color_t *color)
{
  const blit_colormap_t *map ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  HQASSERT(map->nchannels == 1 &&
           map->packed_bits == 16 &&
           map->expanded_bytes == sizeof(blit_t) &&
           channel_is_16bit(map, 0),
           "Not a 1x16 bit colormap") ;

  if ( (color->state[0] & blit_channel_present) != 0 ) {
    channel_output_t v ;

    v = (channel_output_t)(color->quantised.qcv[0] * map->channel[0].pack_mul + map->channel[0].pack_add) ;

    /* Quicker to re-pack and expand at once. */
    HQASSERT(v <= COLORVALUE_MAX,
             "Quantised colorvalue doesn't fit in a COLORVALUE") ;

    color->packed.channels.shorts[0] =
      color->packed.channels.shorts[1] = v ;
#if BLIT_WIDTH_BYTES > 4
    color->packed.channels.words[1] = color->packed.channels.words[0] ;
#endif
  }

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed|blit_color_expanded ;
#endif
}

/** Expanding the color is a no-op because the pack routine already did
    it. */
static void tone16_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_packed, "Blit color not packed") ;
  HQASSERT(color->valid & blit_color_expanded, "Blit color not expanded") ;
}

static void tone16_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->packed_bits == 16 && map->nchannels == 1 &&
           channel_is_16bit(map, 0),
           "Not a suitable 16-bit blit map") ;

  if ( map->apply_properties || map->nrendered == 0 ) {
    /* We need a special routine to pack the 16-bit if properties are in use,
       because we're not guaranteed to be packing high-bit first. */
    map->pack_quantised_color = tone16_pack_other ;
    map->expand_packed_color = tone16_color_expand ;
  } else {
    /* One 16-bit rendered channel. Is it positive or negative? */
    if ( channel_is_positive(map, 0) ) { /* Additive */
      map->pack_quantised_color = tone16_pack_positive ;
    } else if ( channel_is_negative16(map, 0) ) { /* Subtractive */
      map->pack_quantised_color = tone16_pack_negative ;
    }
    map->expand_packed_color = tone16_color_expand ;
  }
}

/** Alternative InterleavingStyles for tone16 surface; i.e., not pixel
    interleaved. */
const static sw_datum tone16_interleave[] = {
  SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
  SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_BAND),
  SW_DATUM_INTEGER(GUCR_INTERLEAVINGSTYLE_FRAME),
} ;

/** Alternative ValuesPerComponent for tone16 surface. */
const static sw_datum tone16_values[] = {
  SW_DATUM_INTEGER(65281), SW_DATUM_INTEGER(65536),
} ;

/** Alternative PackingUnitRequests for tone16 surface. */
const static sw_datum tone16_packing[] = {
  SW_DATUM_INTEGER(0), /* Not defined */
  SW_DATUM_INTEGER(16),
} ;

/** Pagedevice match for tone16 surface. */
const static sw_datum tone16_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("InterleavingStyle"),
    SW_DATUM_ARRAY(&tone16_interleave[0], SW_DATA_ARRAY_LENGTH(tone16_interleave)),
  SW_DATUM_STRING("PackingUnitRequest"),
    SW_DATUM_ARRAY(&tone16_packing[0], SW_DATA_ARRAY_LENGTH(tone16_packing)),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("ValuesPerComponent"),
    SW_DATUM_ARRAY(&tone16_values[0], SW_DATA_ARRAY_LENGTH(tone16_values)),
} ;

/** The pseudo 16-bit surface set. */
static surface_set_t tone16_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&tone16_dict[0],
                                 SW_DATA_DICT_LENGTH(tone16_dict))) ;

/** The pseudo 16-bit surface description. */
static surface_t tone16 = SURFACE_INIT ;

static const surface_t *indexed[N_SURFACE_TYPES] ;

void init_toneblt_16(void)
{
  /* Base blits */
  tone16.baseblits[BLT_CLP_NONE].spanfn =
    tone16.baseblits[BLT_CLP_RECT].spanfn = bitfill16 ;
  tone16.baseblits[BLT_CLP_COMPLEX].spanfn = bitclip16 ;

  tone16.baseblits[BLT_CLP_NONE].blockfn =
    tone16.baseblits[BLT_CLP_RECT].blockfn = blkfill16 ;
  tone16.baseblits[BLT_CLP_COMPLEX].blockfn = blkclip16 ;

  tone16.baseblits[BLT_CLP_NONE].charfn =
    tone16.baseblits[BLT_CLP_RECT].charfn =
    tone16.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  tone16.baseblits[BLT_CLP_NONE].imagefn =
    tone16.baseblits[BLT_CLP_RECT].imagefn =
    tone16.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Object map on the side blits */
  tone16.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].spanfn =
    tone16.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].spanfn = bitfillom16 ;
  tone16.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].spanfn = bitclipom16 ;

  tone16.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].blockfn =
    tone16.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone16.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone16.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].charfn =
    tone16.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].charfn =
    tone16.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone16.omblits[BLT_OM_REPLACE][BLT_CLP_NONE].imagefn =
    tone16.omblits[BLT_OM_REPLACE][BLT_CLP_RECT].imagefn =
    tone16.omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  tone16.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].spanfn =
    tone16.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].spanfn = bitorom16 ;
  tone16.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].spanfn = bitcliporom16 ;

  tone16.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].blockfn =
    tone16.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].blockfn = blkfillspan ;
  tone16.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  tone16.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].charfn =
    tone16.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].charfn =
    tone16.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone16.omblits[BLT_OM_COMBINE][BLT_CLP_NONE].imagefn =
    tone16.omblits[BLT_OM_COMBINE][BLT_CLP_RECT].imagefn =
    tone16.omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Max blits; no min blits for single-channel tone 16 */
  tone16.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    tone16.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn = bitfillmax16 ;
  tone16.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = bitclipmax16 ;

  tone16.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    tone16.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillmax16 ;
  tone16.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipmax16 ;

  tone16.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    tone16.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn =
    tone16.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charbltn ;

  tone16.maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    tone16.maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    tone16.maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No ROP blits for tone16 */

  init_pcl_pattern_blit(&tone16) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&tone16) ;
  surface_pattern_builtin(&tone16) ;
  surface_gouraud_builtin_tone(&tone16) ;

  tone16.areafill = areahalf16 ;
  tone16.prepare = render_prepare_16 ;
  tone16.blit_colormap_optimise = tone16_blitmap_optimise ;

  tone16.n_rollover = 3 ;
  tone16.screened = FALSE ;

  builtin_clip_N_surface(&tone16, indexed) ;

  /* The surface we've just completed is part of a set implementing pseudo
     16-bit contone output. Add it and all of the associated surfaces to the
     surface array for this set. */
  tone16_set.indexed = indexed ;
  tone16_set.n_indexed = NUM_ARRAY_ITEMS(indexed) ;

  indexed[SURFACE_OUTPUT] = &tone16 ;

  /* Add trapping surfaces. */
  surface_set_trap_builtin(&tone16_set, indexed);

  surface_set_transparency_builtin(&tone16_set, &tone16, indexed) ;

  tone16_set.packing_unit_bits = 16 ;

#ifdef BLIT_CONTONE_16
  /* Now that we've filled in the tone16 surface description, hook it up so
     that it can be found. */
  surface_set_register(&tone16_set) ;
#endif
}

void surface_set_mht_ff00_builtin(surface_set_t *set, const surface_t *indexed[])
{
  UNUSED_PARAM(surface_set_t *, set) ;
  HQASSERT(set, "No surface set to initialise") ;
  HQASSERT(set->indexed, "No surface array in set") ;
  HQASSERT(set->indexed == indexed, "Surface array inconsistent") ;
  HQASSERT(set->n_indexed > SURFACE_MHT_CONTONE_FF00 &&
           set->n_indexed > SURFACE_MHT_MASK &&
           set->n_indexed > SURFACE_OUTPUT,
           "Surface array too small") ;
  HQASSERT(set->indexed[SURFACE_OUTPUT] != NULL,
           "No output surface defined for surface set") ;
  HQASSERT(set->indexed[SURFACE_MHT_MASK] != NULL,
           "No MHT mask surface defined for surface set") ;
  HQASSERT(set->indexed[SURFACE_MHT_CONTONE_FF00] == NULL ||
           set->indexed[SURFACE_MHT_CONTONE_FF00] == &tone16,
           "MHT 16-bit surface already initialised") ;
  indexed[SURFACE_MHT_CONTONE_FF00] = &tone16 ;
}

/* Log stripped */
