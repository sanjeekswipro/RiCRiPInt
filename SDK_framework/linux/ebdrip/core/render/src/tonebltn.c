/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:tonebltn.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for generic contone output.
 *
 * This surface is the unoptimised catch-all for tone blitting. It can handle
 * band/frame/pixel interleaved, any number of channels and bit depths. Max/min
 * blit will be very slow, but normal knockouts and overprints shouldn't be
 * too bad. Optimised override surfaces are recommended for commonly used
 * cases.
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
#include "toneblt.h"
#include "tonebltn.h"
#include "pclPatternBlit.h"
#include "hqmemset.h"
#include "gu_chan.h"
#include "builtin.h"
#include "hqbitvector.h"

/** Render preparation function for toneblits packs current color. */
static surface_prepare_t render_prepare_N(surface_handle_t handle,
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

static void areahalfN(render_blit_t *rb,  register FORM *formptr )
{
  blit_color_t *color = rb->color ;
  unsigned int expanded_blits ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;

  expanded_blits = color->map->expanded_bytes >> BLIT_SHIFT_BYTES ;
  if (expanded_blits != 0) {
    /* The usual case - we can use the expanded blits to copy larger chunks
       of data at a time. */
    blit_t *addr = theFormA(*formptr) ;
    int32 h = theFormRH(*formptr) ;

    blit_color_expand(color) ;

    do {
      int32 l = theFormL(*formptr) >> BLIT_SHIFT_BYTES ;
      const blit_t *packed_addr = &color->packed.channels.blits[0] ;
      unsigned int packed_l = expanded_blits ;
      do {
        *addr++ = *packed_addr++ ;
        if ( --packed_l == 0 ) {
          packed_addr = &color->packed.channels.blits[0] ;
          packed_l = expanded_blits ;
        }
      } while ( --l > 0 ) ;
    } while ( --h > 0 ) ;
  } else {
    /* Can't use the expanded blits, so do it the long winded way. */
    render_blit_t rb_copy = *rb ;

    rb_copy.outputform = formptr;
    rb_copy.ylineaddr = theFormA(*formptr);
    rb_copy.x_sep_position = 0;

    blkfillspan(&rb_copy, theFormHOff(*formptr),
                theFormHOff(*formptr) + theFormRH(*formptr) - 1,
                0, theFormW(*formptr) - 1);
  }
}

/* ---------------------------------------------------------------------- */

/* We always want an N-bit surface with 8-bit packing. */
#define PACK_WIDTH_BITS 8
#include "tonebltnimpl.h"

/** Pack and expand used if we can't guarantee there will be enough for one
    pack word. */
static void toneN_8_color_pack(blit_color_t *color)
{
  blit_color_pack_generic8(color) ;
  blit_color_expand(color) ;
}

static void toneN_8_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  if ( map->packed_bits < 8 )
    map->pack_quantised_color = toneN_8_color_pack ;
}

/** Alternative PackingUnitRequests for toneN8 surface. */
const static sw_datum toneN_8_packing[] = {
  SW_DATUM_INTEGER(0), /* Not defined, so default to this surface. */
  SW_DATUM_INTEGER(8),
#ifdef highbytefirst
 /* High byte first is the same order for any packing depth. */
  SW_DATUM_INTEGER(16),
  SW_DATUM_INTEGER(32),
#if BLIT_WIDTH_BYTES > 4
  SW_DATUM_INTEGER(64),
#endif /* BLIT_WIDTH_BYTES */
#endif /* highbytefirst */
#ifdef DEBUG_BUILD
  /* This value exists solely to support debugging. With it, the generic
     8-bit surface can be selected in configurations where the specialised 8,
     24, and 32-bpp surfaces would normally override it. Output of the
     different surface implementations can be compared to find bugs. */
  SW_DATUM_INTEGER(-1),
#endif
} ;

/** Pagedevice dictionary for generic toneN surface selection. */
const static sw_datum toneN_8_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("PackingUnitRequest"),
    SW_DATUM_ARRAY(&toneN_8_packing[0], SW_DATA_ARRAY_LENGTH(toneN_8_packing)),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
} ;

/** Generic pixel interleaved surface set. */
static surface_set_t toneN_8_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&toneN_8_dict[0],
                                 SW_DATA_DICT_LENGTH(toneN_8_dict))) ;

#ifndef highbytefirst
/* For lowbytefirst, we also want a 16-bit packing unit surface */
#define PACK_WIDTH_BITS 16
#include "tonebltnimpl.h"

/** Expand packed storage so that the data repeats sufficient times to make
    a multiple of blit_t size. This allows fast copy operations to be used
    for blitting. */
static void toneN_16_pack_expand(blit_packed_t *packed, const blit_colormap_t *map)
{
  unsigned int packed_bits, expanded_bytes ;

  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  packed_bits = map->packed_bits ;

  /* Don't need to do any expansion if there are no bits. */
  if ( packed_bits == 0 )
    return ;

  expanded_bytes = map->expanded_bytes ;

  /* There is nothing to do if the LCM is the same as the packed size. */
  if ( (expanded_bytes << 3) > packed_bits ) {
    uint16 *src = &packed->shorts[0];
    uint16 *dest = &packed->shorts[packed_bits >> 4];
    uint16 *limit = &packed->shorts[expanded_bytes];
    uint16 residual ;
    unsigned int shift = (packed_bits & 15) ;

    HQASSERT(limit > dest,
             "Destination and limit cannot be equal if LCM is different") ;

    /* The residual data is the overflow bits in the last short of the packed
       size. */
    residual = CAST_UNSIGNED_TO_UINT16(*dest & ~(0xffffu >> shift)) ;

    if ( packed_bits < 16 ) {
      /* Expand the packed size to at least a short by doubling the bits
         used. */
      uint32 expanded = residual << 16 ;

      HQASSERT(src == dest, "Less than one short should have src equals dest") ;
      do {
        expanded |= expanded >> packed_bits ;
        packed_bits <<= 1 ;
      } while ( packed_bits < 16 ) ;
      *dest++ = (uint16)(expanded >> 16) ;
      residual = (uint16)expanded ;
      shift = (packed_bits & 15) ;
    }

    if ( shift == 0 ) { /* Short multiples are easy to expand */
      /* Deliberately copy upwards, overlapping src and dest. Don't use
         HqMemCpy(), it won't do the right thing. */
      do {
        *dest++ = *src++ ;
      } while ( dest < limit ) ;
    } else { /* Need to shift bits to replicate shorts. */
      do { /* Deliberately copy upwards overlapping src and dest. */
        *dest++ = CAST_UNSIGNED_TO_UINT16(residual | (*src >> shift)) ;
        residual = (uint16)(*src++ << (16 - shift)) ;
      } while ( dest < limit ) ;
    }
  }
}


/* Ensure that the packed blit color based on a quantised color is valid. */
static void toneN_16_color_pack(blit_color_t *color)
{
  const blit_colormap_t *map ;
  channel_index_t index ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_present) != 0 ) {
      unsigned int offset = map->channel[index].bit_offset ;
      unsigned int size = map->channel[index].bit_size ;
      channel_output_t output_mask = (channel_output_t)~(-1 << size) ;
      /* Extract and map the quantised channel data. It's the job of the
         rasterstyle setup to ensure that the mapping of the quantisation
         fits in the packed data buffer. */
      channel_output_t output = (channel_output_t)(color->quantised.qcv[index] * map->channel[index].pack_mul + map->channel[index].pack_add) ;
      unsigned int shift, shortindex ;

      /* Check that the channel has a suitable size */
      HQASSERT(size > 0, "Channel to be packed has no size") ;
      HQASSERT(size <= sizeof(channel_output_t) * 8,
               "Packed size larger than storage unit") ;

      HQASSERT((color->quantised.qcv[index] & ~output_mask) == 0,
               "Quantised color overflows packed storage size") ;
      output &= output_mask ;

      /* Check that channel fits in space allocated */
      HQASSERT(offset + size <= sizeof(channel_output_t) * 8 * BLIT_MAX_CHANNELS,
               "Packing channel outside of allocated storage") ;

      /* Pack channel high-bit first into slot. We pack it using short-sized
         units, because that's what the user asked for. Ours is not to
         question why... */
      shift = (offset & 15) + size ;
      shortindex = (offset >> 4) ;

      if ( shift > 16 ) {
        /* The output straddles two or more shorts */
        unsigned int remainder = shift - 16 ;

        color->packed.channels.shorts[shortindex] &= (uint16)~(output_mask >> remainder) ;
        color->packed.channels.shorts[shortindex++] |= (uint16)(output >> remainder) ;

        /* Fall through to the final short */
        shift = 32 - shift ;
      } else {
        /* The output fits in one short. */
        shift = 16 - shift ;
      }

      /* The final short of multi-short, or sole short of a single short pack
         uses the output mask we computed earlier. */
      HQASSERT(shift >= 0 && shift < 16, "Shift out of range") ;
      color->packed.channels.shorts[shortindex] &= (uint16)~(output_mask << shift) ;
      color->packed.channels.shorts[shortindex] |= (uint16)(output << shift) ;
    }
  }

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif

  /* Expand if we can't guarantee there will be enough for one pack word. */
  if ( map->packed_bits < 16 )
    toneN_16_pack_expand(&color->packed.channels, map) ;
}

/** Expand a 16-bit packed color. */
static void toneN_16_color_expand(blit_color_t *color)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_packed, "Blit color is not packed") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_expanded), "Blit color is already expanded") ;
#endif

  /* Expand packed channel data to blit width multiple. */
  toneN_16_pack_expand(&color->packed.channels, color->map) ;

#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

/** Create a packed output blit mask for the bitvector corresponding
    to a channel mask. */
void toneN_16_overprint_mask(blit_packed_t *packed,
                             const blit_color_t *color,
                             blit_channel_state_t mask,
                             blit_channel_state_t state)
{
  const blit_colormap_t *map ;
  channel_index_t index ;
  unsigned int bytes ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  /* Clear the packed space, so that padding space will have a consistent
     (zero) value. */
  bytes = map->expanded_bytes ;
  if ( bytes == 0 ) /* lcm of packed bits and sizeof(blit_t) is too large. */
    bytes = (map->packed_bits + 7) >> 3 ;
  HqMemZero(&packed->bytes[0], bytes) ;

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & mask) == state ) {
      /* Set all bits in map->channel[index] packed values. */
      unsigned int offset = map->channel[index].bit_offset ;
      unsigned int size = map->channel[index].bit_size ;
      channel_output_t output = (channel_output_t)~(-1 << size) ;
      unsigned int shift, shortindex ;

      /* Check that the channel has a suitable size */
      HQASSERT(size > 0, "Channel to be packed has no size") ;
      HQASSERT(size <= sizeof(channel_output_t) * 8,
               "Packed size larger than storage unit") ;

      /* Check that channel fits in space allocated */
      HQASSERT(offset + size <= sizeof(channel_output_t) * 8 * BLIT_MAX_CHANNELS,
               "Packing channel outside of allocated storage") ;

      /* Pack channel high-bit first into slot. short-sized
         units, because that's what the user asked for. Ours is not to
         question why... */
      shift = (offset & 15) + size ;
      shortindex = (offset >> 4) ;

      if ( shift > 16 ) {
        /* The output straddles two or more shorts */
        unsigned int remainder = shift - 16 ;

        packed->shorts[shortindex++] |= (uint16)(output >> remainder) ;

        /* Fall through to the final short */
        shift = 32 - shift ;
      } else {
        /* The output fits in one short. */
        shift = 16 - shift ;
      }

      /* The final short of multi-short, or sole byte of a single short pack
         uses the output mask we computed earlier. */
      HQASSERT(shift >= 0 && shift < 16, "Shift out of range") ;
      packed->shorts[shortindex] |= (uint16)(output << shift) ;
    }
  }

  /* Expand if we can't guarantee there will be enough for one pack word. */
  if ( map->packed_bits < 16 )
    toneN_16_pack_expand(packed, map) ;
}

static void toneN_16_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  map->pack_quantised_color = toneN_16_color_pack ;
  map->expand_packed_color = toneN_16_color_expand ;
  map->overprint_mask = toneN_16_overprint_mask ;
}

/** Pagedevice dictionary for generic toneN 16-bit packed surface selection. */
const static sw_datum toneN_16_dict[] = {
  SW_DATUM_STRING("Halftone"), SW_DATUM_BOOLEAN(FALSE),
  SW_DATUM_STRING("PackingUnitRequest"), SW_DATUM_INTEGER(16),
  SW_DATUM_STRING("RunLength"), SW_DATUM_BOOLEAN(FALSE),
} ;

/** Generic pixel interleaved surface set, with 16-bit unit packing. */
static surface_set_t toneN_16_set =
  SURFACE_SET_INIT(SW_DATUM_DICT(&toneN_16_dict[0],
                                 SW_DATA_DICT_LENGTH(toneN_16_dict))) ;

#endif

void init_toneblt_N(void)
{
  init_toneblt_N8(&toneN_8_set, &toneN_8_blitmap_optimise) ;

#ifndef highbytefirst
  init_toneblt_N16(&toneN_16_set, &toneN_16_blitmap_optimise) ;
#endif
}

/* Log stripped */
