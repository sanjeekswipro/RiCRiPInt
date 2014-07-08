/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:blitcolors.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blit color manipulation functions.
 */

#include "core.h"
#include "bitblts.h"
#include "color.h"
#include "dl_color.h"
#include "tables.h"
#include "hqbitvector.h"
#include "hqmemset.h"
#include "display.h" /* LateColorAttrib */
#include "swcmm.h" /* SW_CMM_INTENT_PERCEPTUAL */
#include "blitcolors.h"
#include "blitcolorh.h"
#include "pixelLabels.h"
#include "gu_chan.h"
#include "htrender.h" /* render_gethalftone */


static void blit_pack_expand8(blit_packed_t *packed, const blit_colormap_t *map) ;
static void blit_pack_expand16(blit_packed_t *packed, const blit_colormap_t *map) ;
static void blit_color_pack_mask(blit_color_t *color) ;
static void blit_color_expand_mask(blit_color_t *color) ;


/* Initialise a set of colors, and associate them with a colormap. */
void blit_color_init(blit_color_t *color, const blit_colormap_t *map)
{
  unsigned int bytes ;

  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color != NULL, "No blit color to initialise") ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  /* Make it easy to see what we've changed. */
  HqMemSet8((uint8 *)color, 0x55, sizeof(blit_color_t));
#endif

  color->map = map ;

#ifdef ASSERT_BUILD
  /* Mark all of the sub-structures as invalid. */
  color->valid = blit_color_invalid ;
#endif

  /* No screen has been prepared for quantisation. */
  color->quantised.spotno = SPOT_NO_INVALID ;

  /* Clear the packed space, so that padding space will have a consistent
     (zero) value. */
  bytes = map->expanded_bytes ;
  if ( bytes == 0 ) /* lcm of packed bits and sizeof(blit_t) is too large. */
    bytes = (map->packed_bits + 7) >> 3 ;
  HqMemZero(&color->packed.channels.bytes[0], bytes) ;

  NAME_OBJECT(color, BLIT_COLOR_NAME) ;
}


void blit_color_unpack(blit_color_t *color, const dl_color_t *dlc,
                       object_type_t label,
                       LateColorAttrib *lca,
                       Bool knockout, Bool selected,
                       Bool is_erase, Bool is_knockout)
{
  const blit_colormap_t *map ;
  bitvector_iterator_t iterator ;
  channel_index_t ncolors, nmaxblits, noverrides, nchannels ;
  COLORVALUE dcv ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  HQASSERT(is_erase == ((map->erase_color == NULL) &&
                        (map->knockout_color == NULL)),
           "No erase/knockout color");

  /* Extract DL color for constant colors, or set colorvalue out of band so
     it can be used to extract channel colors. */
  switch ( dlc_check_black_white(dlc) ) {
  default:
    HQFAIL("Invalid DL tint color") ;
    /*@fallthrough@*/
  case DLC_TINT_WHITE:
    dcv = COLORVALUE_ONE ;
    break ;
  case DLC_TINT_BLACK:
    dcv = COLORVALUE_ZERO ;
    break ;
  case DLC_TINT_OTHER:
    dcv = COLORVALUE_INVALID ;
    break ;
  }

  /* For sanity's sake, make sure the alpha, all and type channels have
     appropriate state set. */
  color->state[map->all_index] = color->state[map->type_index] =
    color->state[map->alpha_index] = blit_channel_missing;

  ncolors = nmaxblits = noverrides = nchannels = 0 ;

  for ( BITVECTOR_ITERATE_BITS(iterator, map->nchannels) ;
        BITVECTOR_ITERATE_BITS_MORE(iterator) ;
        BITVECTOR_ITERATE_BITS_NEXT(iterator) ) {
    COLORVALUE cv = dcv ;
    blit_channel_state_t state = blit_channel_missing ;

    if ( (map->rendered[iterator.element] & iterator.mask) == 0 ) {
      /* Channel is not rendered. Make the value transparent. */
      cv = COLORVALUE_TRANSPARENT ;
    } else if ( map->channel[iterator.bit].type == channel_is_color ) {
      COLORANTINDEX ci = map->channel[iterator.bit].ci ;

      HQASSERT(ci >= 0 || ci == COLORANTINDEX_ALL,
               "Colorant index not valid for color channel") ;

      /* Test if we have a colorant value for this channel. Note that the
         dlc_get_indexed_colorant call checks for either the colorant index,
         or colorant /All existing. */
      if ( cv != COLORVALUE_INVALID /* cv already extracted */ ||
           (dlc_get_indexed_colorant(dlc, ci, &cv) &&
            cv != COLORVALUE_TRANSPARENT) ) {
        /* We are either unpacking the erase color itself, or rendering
           an object normally. In either case, the colorvalue that
           we have extracted from the dlc is right. */
        state = blit_channel_present ;
        if ( ci != COLORANTINDEX_ALL &&
             dlc_colorant_is_overprinted(dlc, ci) ) {
          state |= blit_channel_maxblit ;
          ++nmaxblits ;
        }
        ++ncolors, ++nchannels ;
      } else if ( is_erase ) {
        /* This is the erase or knockout color that we're
           unpacking. We can't leave any rendered channels marked as
           overprinted, but there is no colorant, and no /All
           separation. Use solid or clear according to whether the
           first colorant is closest to solid or clear. */
        dl_color_iter_t dliter;

        HQTRACE(TRUE, ("Missing channel in erase color"));
        switch ( dlc_first_colorant(dlc, &dliter, &ci, &cv) ) {
        case DLC_ITER_COLORANT:
        case DLC_ITER_ALLSEP:
          HQASSERT(cv != COLORVALUE_TRANSPARENT,
                   "NYI: knockout color with first channel transparent") ;
          if ( cv >= COLORVALUE_HALF ) {
            cv = COLORVALUE_ONE ;
          } else {
            cv = COLORVALUE_ZERO ;
          }
          break ;
        case DLC_ITER_ALL01:
          HQFAIL("Erase color should have been handled through tint") ;
          break ;
        default:
          HQFAIL("Erase color has no colorants") ;
          break ;
        }

        state = blit_channel_present ;

        ++ncolors, ++nchannels ;
      } else if ( knockout ) {
        /* The channel didn't exist in the DL color, or was marked as
           an overprint using COLORVALUE_TRANSPARENT. We're knocking
           out this object, so we need to pick up the missing channel
           from the knockout color. If we're not generating a contone
           mask then this will be the same as the erase color. */
        cv = map->knockout_color->unpacked.channel[iterator.bit].cv ;
        state = map->knockout_color->state[iterator.bit] |
          blit_channel_override | blit_channel_knockout ;
        HQASSERT((state & blit_channel_present) != 0,
                 "Knockout color is missing colorant") ;

        if ( (state & blit_channel_maxblit) != 0 )
          ++nmaxblits ;

        ++noverrides, ++ncolors, ++nchannels ;
      } else {
        /* Colorant is not present, it isn't covered by an /All separation,
           it isn't knocking out, and it isn't required. Mark it as
           overprinted. */
        cv = COLORVALUE_TRANSPARENT ;
        state = blit_channel_missing ;
      }
    } else {
      /* This is the Alpha or type channel. They'll be filled in later. */
      HQASSERT((map->channel[iterator.bit].type == channel_is_type &&
                iterator.bit == map->type_index) ||
               (map->channel[iterator.bit].type == channel_is_alpha &&
                iterator.bit == map->alpha_index),
               "Channel should be alpha or type") ;
      cv = 0 ; /* Clear the colorant's storage */
      state = blit_channel_present ;

      ++nchannels ;
    }

    color->unpacked.channel[iterator.bit].cv = cv ;
    color->state[iterator.bit] = state ;
  }

  color->ncolors = ncolors ;
  color->nmaxblits = nmaxblits ;
  color->noverrides = noverrides ;
  color->nchannels = nchannels ;

  /* Reset the alpha and type channels after the loop, they may get blatted
     if their channels are missing. */
  color->alpha = color->unpacked.channel[map->alpha_index].cv
    = dlc_color_opacity(dlc);
  color->type = color->unpacked.channel[map->type_index].label = label ;
  color->rendering_intent =
    lca != NULL ? lca->renderingIntent
    : SW_CMM_INTENT_RELATIVE_COLORIMETRIC /* default for RLE */;

  /* If there are any maxblits, or any overprints, then this object will
     combine with other objects. Mark the type channel as a maxblit, so
     that it can be handled specially. */
  if ( nmaxblits > 0 || nchannels != map->nrendered ) {
    /** \todo ajcd 2008-10-15: should we also increase nmaxblits if the type
        channel is rendered? */
    color->state[map->type_index] |= blit_channel_maxblit ;
  }
#ifdef ASSERT_BUILD
  /* We've unpacked a color, but the quantisation and packing are now
     invalid. */
  color->valid = blit_color_unpacked ;
#endif

  /* Applying the rendering properties is done as a separate loop because
     most of the time it won't be needed, and the logic is sufficiently
     complex that it is desirable to keep it in one location. */
  if ( map->apply_properties && ncolors > 0 )
    blit_apply_render_properties(color, selected, is_erase);

  if ( is_erase ) { /* remember it */
    blit_colormap_t *modifiable_map = (blit_colormap_t *)map;
    modifiable_map->erase_color = color;
    /* The default value for the knockout color is the same as the
       erase - it may or may not be updated later. */
    modifiable_map->knockout_color = color;
  }

  if ( is_knockout ) { /* remember it */
    blit_colormap_t *modifiable_map = (blit_colormap_t *)map;
    modifiable_map->knockout_color = color;
  }
}


void blit_color_quantise(blit_color_t *color)
{
  const blit_colormap_t *map ;
  channel_index_t index ;
  blit_quantise_state_t state = 0;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_unpacked, "Blit color is not unpacked") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_quantised), "Blit color is already quantised") ;
#endif

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_present) != 0 ) {
      if ( map->channel[index].type == channel_is_color ) {
        COLORANTINDEX ci = map->channel[index].ci ;
        COLORVALUE cv = color->unpacked.channel[index].cv ;
        channel_output_t htmax = color->quantised.htmax[index] ;

        HQASSERT(ci == COLORANTINDEX_ALL || ci >= 0,
                 "Invalid colorant index for color channel") ;
        HQASSERT(htmax > 0,
                 "Halftone max value not set") ;

        if ( cv >= COLORVALUE_ONE ) {
          cv = htmax ; /* white */
        } else if ( cv > 0 ) {
          switch ( map->override_htmax ) {
          case 0:
            HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
                     "Screen not set; cannot quantise blit color") ;
            ht_applyTransform(color->quantised.spotno, color->quantised.type,
                              1, &ci, &cv, &cv, TRUE, htmax);
            break ;
          case 1:
            /* Monochrome output, white done above, so must be black. */
            HQASSERT(cv == COLORVALUE_ONE, "Midtone on a mask channel");
            cv = 0;
            break ;
          default:
            /* This is the same as CT_NORMAL, which is what
               ht_applyTransform() would do with it. */
            COLORVALUE_MULTIPLY(cv, map->override_htmax, cv) ;
            break ;
          }
        }

        /* Determine if quantised channel is all zero, all one, or neither. */
        if ( cv == 0 )
          state |= blit_quantise_min ;
        else if ( cv >= htmax )
          state |= blit_quantise_max ;
        else
          state |= blit_quantise_mid ;

        color->quantised.qcv[index] = (channel_output_t)cv ;
      } else if ( map->channel[index].type == channel_is_type ) {
        /* Map the quantised type through the type mapping array. */
        HQASSERT(color->type == color->unpacked.channel[map->type_index].label,
                 "Blit color type out of step with unpacked value");
        if ( map->type_lookup != NULL ) {
          color->quantised.qcv[map->type_index] = map->type_lookup[color->type];
        } else {
          color->quantised.qcv[map->type_index] = (channel_output_t)color->type;
        }
      } else {
        HQASSERT(map->channel[index].type == channel_is_alpha
                 && index == map->alpha_index,
                 "Blit channel is not alpha, type, or colorant");
        /* Quantise the alpha channel using a rounded multiplication,
           rather than the HT transforms. */
        HQASSERT(color->alpha == color->unpacked.channel[map->alpha_index].cv,
                 "Blit color alpha out of step with unpacked value") ;
        COLORVALUE_MULTIPLY(color->alpha,
                            color->quantised.htmax[map->alpha_index],
                            color->quantised.qcv[map->alpha_index]) ;
      }
    }
  }
  color->quantised.state = state ;

#ifdef ASSERT_BUILD
  color->valid |= blit_color_quantised ;
#endif
}


/* Ensure that the packed blit color based on a quantised color is valid. */
void blit_color_pack_generic8(blit_color_t *color)
{
  const blit_colormap_t *map ;
  channel_index_t index ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_present) != 0 ) {
      unsigned int offset = map->channel[index].bit_offset ;
      unsigned int size = map->channel[index].bit_size ;
      channel_output_t output_mask = (channel_output_t)~(-1 << size) ;
      /* Extract and map the quantised channel data. It's the job of the
         rasterstyle setup to ensure that the mapping of the quantisation
         fits in the packed data buffer. */
      channel_output_t output = (channel_output_t)(color->quantised.qcv[index] * map->channel[index].pack_mul + map->channel[index].pack_add) ;
      unsigned int shift, byteindex ;

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

      /* Pack channel high-bit first into slot. We pack it using byte-sized
         units, so we get a consistent output for low- and high-endian
         architectures. */
      shift = (offset & 7) + size ;
      byteindex = (offset >> 3) ;

      if ( shift > 8 ) {
        /* The output straddles two or more bytes */
        unsigned int remainder = shift - 8 ;

        color->packed.channels.bytes[byteindex] &= (uint8)~(output_mask >> remainder) ;
        color->packed.channels.bytes[byteindex++] |= (uint8)(output >> remainder) ;

        while ( remainder > 8 ) {
          remainder -= 8 ;
          shift -= 8 ;
          color->packed.channels.bytes[byteindex++] = (uint8)(output >> remainder) ;
        }

        /* Fall through to the final byte */
        shift = 16 - shift ;
      } else {
        /* The output fits in one byte. */
        shift = 8 - shift ;
      }

      /* The final byte of multi-byte, or sole byte of a single byte pack
         uses the output mask we computed earlier. */
      HQASSERT(shift >= 0 && shift < 8, "Shift out of range") ;
      color->packed.channels.bytes[byteindex] &= (uint8)~(output_mask << shift) ;
      color->packed.channels.bytes[byteindex] |= (uint8)(output << shift) ;
    }
  }

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif
}

/* Ensure that the packed blit color based on a quantised color is valid. */
void blit_color_pack_generic16(blit_color_t *color)
{
  const blit_colormap_t *map ;
  channel_index_t index ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;

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

  /* Expand if we can't guarantee there will be enough for one pack word, we
     need that as a minimum for blitting. */
  if ( map->packed_bits < 16 ) {
    blit_pack_expand16(&color->packed.channels, map) ;
#ifdef ASSERT_BUILD
    color->valid |= blit_color_expanded ;
#endif
  }
}

/** Expand an 8-bit packed color. */
void blit_color_expand_generic8(blit_color_t *color)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_packed, "Blit color is not packed") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the tone-N
     surface expands in some pack methods, and it's too hard to keep track. */
  HQASSERT(!(color->valid & blit_color_expanded), "Blit color is already expanded") ;
#endif

  /* Expand packed channel data to blit width multiple. */
  blit_pack_expand8(&color->packed.channels, color->map) ;

#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

/** Expand a 16-bit packed color. */
void blit_color_expand_generic16(blit_color_t *color)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_packed, "Blit color is not packed") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the tone-N
     surface expands in some pack methods, and it's too hard to keep track. */
  HQASSERT(!(color->valid & blit_color_expanded), "Blit color is already expanded") ;
#endif

  /* Expand packed channel data to blit width multiple. */
  blit_pack_expand16(&color->packed.channels, color->map) ;

#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}


/* Convert quantised colors back to unpacked colors. */
void blit_color_dequantise(blit_color_t *color)
{
  const blit_colormap_t *map ;
  channel_index_t index ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but trapping does a
     lot of unnecessary dequantisation. */
  HQASSERT(!(color->valid & blit_color_unpacked), "Blit color is already unpacked") ;
#endif
  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Cannot dequantise color if screen not installed") ;

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    HQASSERT(map->channel[index].type == channel_is_color,
             "Dequantising non-color");
    if ( (color->state[index] & blit_channel_present) != 0 ) {
      COLORVALUE_DIVIDE(color->quantised.qcv[index],
                        color->quantised.htmax[index],
                        color->unpacked.channel[index].cv) ;
    } else {
      color->unpacked.channel[index].cv = COLORVALUE_TRANSPARENT ;
    }
  }
#ifdef ASSERT_BUILD
  color->valid |= blit_color_unpacked ;
#endif
}


/* Ensure that the quantised color has the levels set for a particular
   screen. */
void blit_quantise_set_screen(blit_color_t *color, SPOTNO screen, HTTYPE type)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  if ( !ht_is_object_based_screen(screen) )
    type = HTTYPE_DEFAULT;

  if ( color->quantised.spotno != screen || color->quantised.type != type ) {
    const blit_colormap_t *map = color->map ;
    bitvector_iterator_t iterator ;

    VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

    for ( BITVECTOR_ITERATE_BITS(iterator, map->nchannels) ;
          BITVECTOR_ITERATE_BITS_MORE(iterator) ;
          BITVECTOR_ITERATE_BITS_NEXT(iterator) ) {
      /* Set the maximum for all rendered channels. The subset of rendered
         channels that are actually knocked out or maxblitted hasn't been
         decided yet, so we can't limit it to those yet. */
      if ( (map->rendered[iterator.element] & iterator.mask) != 0 ) {
        if ( map->override_htmax > 0 ) {
          /* If the color is already quantised, it must have the same depth,
             because the tone max cannot change once the blitmap is
             created. */
          HQASSERT(!(color->valid & blit_color_quantised) ||
                   color->quantised.htmax[iterator.bit] == map->override_htmax,
                   "Tone max changed after use") ;
          color->quantised.htmax[iterator.bit] = map->override_htmax ;
        } else if ( map->channel[iterator.bit].type == channel_is_color ) {
          COLORANTINDEX ci = map->channel[iterator.bit].ci ;
          HQASSERT(ci == COLORANTINDEX_ALL || ci >= 0,
                   "Invalid colorant index for color channel") ;
          color->quantised.htmax[iterator.bit] =
            ht_getClearScreen(screen, type, ci);

#ifdef ASSERT_BUILD
          /* We've changed the screen levels corresponding to colorants;
             invalidate all except unpacked, to make sure they are rebuilt. */
          color->valid &= blit_color_unpacked ;
#endif
        }
      }
    }

    /** \todo ajcd 2008-11-14: These should come from the rasterstyle
        channel data. */
    color->quantised.htmax[map->type_index] = map->type_htmax ;
    color->quantised.htmax[map->alpha_index] = COLORVALUE_ONE ;

    color->quantised.spotno = screen; color->quantised.type = type;
  }
}


blit_quantise_state_t blit_quantise_state(blit_color_t *color)
{
  blit_quantise_state_t state ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  HQASSERT(color->valid & blit_color_quantised, "Color not quantised") ;

  state = color->quantised.state ;
  if ( state == blit_quantise_unknown ) {
    const blit_colormap_t *map = color->map ;
    channel_index_t index ;

    VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

    for ( index = 0 ; index < map->nchannels ; ++index ) {
      if ( (color->state[index] & blit_channel_present) != 0 &&
           map->channel[index].type == channel_is_color ) {
        channel_output_t qcv = color->quantised.qcv[index] ;

        HQASSERT(color->quantised.htmax[index] > 0,
                 "Halftone max value not set") ;

        if ( qcv == 0 )
          state |= blit_quantise_min ;
        else if ( qcv >= color->quantised.htmax[index] )
          state |= blit_quantise_max ;
        else
          state |= blit_quantise_mid ;
      }
    }

    color->quantised.state = state ;
  }

  return state ;
}


/* Helper function for blit_apply_render_properties() */
void blit_channel_mark_absent(blit_color_t *color, channel_index_t index)
{
  const blit_colormap_t *map ;
  blit_channel_state_t state ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(index < map->nchannels, "Channel index out of range") ;

  state = color->state[index] ;
  if ( (state & blit_channel_present) != 0 ) {
    HQASSERT(color->nchannels > 0, "No channels left to remove") ;
    color->nchannels -= 1 ;

    if ( index == map->alpha_index ) {
      HQASSERT(map->channel[index].type == channel_is_alpha,
               "Alpha channel type or index wrong") ;
      color->alpha = color->unpacked.channel[index].cv = COLORVALUE_ONE ;
      HQASSERT(color->ncolors <= color->nchannels, "Too few channels for colors") ;
    } else {
      HQASSERT(map->channel[index].type == channel_is_color,
               "Should only be removing color channels or alpha") ;
      HQASSERT(color->ncolors > 0, "No colors left to remove") ;
      color->ncolors -= 1 ;

      if ( (state & blit_channel_override) != 0 ) {
        /* Note that we leave the override bit set in the channel state, in
           case the colorant is reintroduced. */
        HQASSERT(color->noverrides > 0, "No overrides left to remove") ;
        color->noverrides -= 1 ;
      }

      if ( (state & blit_channel_maxblit) != 0 ) {
        /* Note that we leave the maxblit bit set in the channel state, in
           case the colorant is reintroduced. */
        HQASSERT(color->nmaxblits > 0, "No maxblits left to remove") ;
        color->nmaxblits -= 1 ;
      }

      color->quantised.state = blit_quantise_unknown ;
      color->unpacked.channel[index].cv = COLORVALUE_TRANSPARENT ;
    }

    color->state[index] = state & ~blit_channel_present ;
  }
}


/* Helper function for blit_apply_render_properties() */
void blit_channel_mark_present(blit_color_t *color, channel_index_t index)
{
  const blit_colormap_t *map ;
  blit_channel_state_t state ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(index < map->nchannels, "Channel index out of range") ;

  state = color->state[index] ;
  if ( (state & blit_channel_present) == 0 ) {
    HQASSERT(color->nchannels < map->nchannels, "No channels left to add") ;
    color->nchannels += 1 ;

    if ( index == map->alpha_index ) {
      HQASSERT(map->channel[index].type == channel_is_alpha,
               "Alpha channel type or index wrong") ;
    } else {
      HQASSERT(map->channel[index].type == channel_is_color,
               "Should only be adding color channels or alpha") ;

      HQASSERT(color->ncolors < map->ncolors, "No colors left to add") ;
      color->ncolors += 1 ;

      if ( (state & blit_channel_override) != 0 ) {
        /* Note that we left the override bit set in the channel state, in
           case the colorant is reintroduced. */
        HQASSERT(color->noverrides < color->ncolors, "No overrides left to add") ;
        color->noverrides += 1 ;
      }

      if ( (state & blit_channel_maxblit) != 0 ) {
        /* Note that we left the maxblit bit set in the channel state, in
           case the colorant is reintroduced. */
        HQASSERT(color->nmaxblits < color->ncolors, "No maxblits left to add");
        color->nmaxblits += 1 ;
      }

      color->quantised.state = blit_quantise_unknown ;
    }

    color->state[index] = state | blit_channel_present ;
  }
}


void blit_apply_render_properties(blit_color_t *color,
                                  Bool selected, Bool erase)
{
  const blit_colormap_t *map ;
  bitvector_iterator_t iterator ;
  Bool render_only ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT((color->valid & blit_color_unpacked) ||
           (color->valid & blit_color_quantised),
           "Color is neither unpacked nor quantised") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->apply_properties, "Don't need to apply render properties") ;

  /* Allow rendering only for grouping objects which may contain many
     different colorants, for watermark and debug marks which should go on
     all channels, and for erase objects that should fill all channels. Note
     that these objects have their dispositions explicitly set, and so don't
     need to mask out the user label before testing. */
  render_only = (color->type == 0 || color->type == SW_PGB_MIXED_OBJECT);

  for ( BITVECTOR_ITERATE_BITS(iterator, map->nchannels) ;
        BITVECTOR_ITERATE_BITS_MORE(iterator) ;
        BITVECTOR_ITERATE_BITS_NEXT(iterator) ) {
    if ( (map->rendered[iterator.element] & iterator.mask) == 0 ) {
      /* Channel is not rendered, so it shouldn't be marked as present. */
      HQASSERT(color->state[iterator.bit] == blit_channel_missing,
               "Non-rendered channel should be missing") ;
    } else if ( map->channel[iterator.bit].type == channel_is_color ) {
      uint32 channel_properties = map->channel[iterator.bit].render_properties ;
      blit_channel_state_t state = color->state[iterator.bit] ;
      /* This is normally not a legal channel combination, however we test
         for either render property or mask property dependent on the
         channel properties, so this can act as a fallback for both
         cases if we force "rendering" for all channels. */
      uint32 properties = (RENDERING_PROPERTY_RENDER_ALL |
                           RENDERING_PROPERTY_MASK_ALL);

      /* If the color is for a pixel that need not be present and rendered
         in all non-omitted channels, then we combine the label's
         properties with the channel properties to work out what to do with
         this colorant. */
      if ( !render_only ) {
        properties = pixelLabelProperties(color->type, channel_properties) ;
        /* Assert that bit mask is mutually exclusive for this object. */
        HQASSERT(RENDERING_PROPERTY_HAS_MASK(properties) +
                 RENDERING_PROPERTY_HAS_RENDER(properties) +
                 RENDERING_PROPERTY_HAS_KNOCKOUT(properties) +
                 RENDERING_PROPERTY_IS_IGNORE(properties) == 1,
                 "More than one of mask, render, ignore, and knockout properties is true") ;
      }

      /* The channel can have Mask or Render present, but not both. The
         object properties say whether this object type will mask, knockout
         or overprint for a Mask channel, or render, knockout, or overprint
         for a non-Mask channel. We have to start by testing the channel
         properties, though, to work out how to interpret the object
         properties. */
      if ( RENDERING_PROPERTY_IS_IGNORE(properties) ) {
        HQASSERT(!erase, "Should not be overprinting in erase color") ;
        HQASSERT(!render_only,
                 "Should not be overprinting channel when forcing render") ;
        /* This object should be overprinted with respect to this channel. */
        blit_channel_mark_absent(color, iterator.bit) ;
      } else if ( RENDERING_PROPERTY_HAS_MASK(channel_properties) ) {
        /* This channel is a Mask, which means that we will force the
           colorvalues to zero or one. No intermediate values are allowed,
           and there is no color variation within self-colored objects such
           as images and shfills, so we mark the channel as overriding. */

        /* We should now be down to two options for the properties, since
           Render is disallowed for mask channels. */
        HQASSERT(RENDERING_PROPERTY_HAS_MASK(properties) +
                 RENDERING_PROPERTY_HAS_KNOCKOUT(properties) == 1,
                 "More than one of mask and knockout properties is true") ;

        if ( erase ) {
          /* We are unpacking the erase color. The erase color for a mask
             doesn't depend on the object properties or the color arg, just the
             selected flag. */
          HQASSERT((state & blit_channel_present) != 0,
                   "Erase color should have all colorants") ;
          if ( selected ) { /* black */
            color->unpacked.channel[iterator.bit].cv = COLORVALUE_ZERO;
            color->quantised.qcv[iterator.bit] = 0;
          } else {/* white */
            color->unpacked.channel[iterator.bit].cv = COLORVALUE_ONE;
            color->quantised.qcv[iterator.bit] =
              color->quantised.htmax[iterator.bit];
          }
          color->quantised.state = blit_quantise_unknown; /* masks don't care */
        } else if ( (state & (blit_channel_present|blit_channel_knockout)) == blit_channel_present ) {
          /* The object has this colorant, so it must either be black or
             white, depending on whether it's masked (and selected). */
          if ( selected && RENDERING_PROPERTY_HAS_MASK(properties) ) {
            /* black */
            color->unpacked.channel[iterator.bit].cv = COLORVALUE_ZERO ;
            color->quantised.qcv[iterator.bit] = 0 ;
          } else {
            /* white */
            color->unpacked.channel[iterator.bit].cv = COLORVALUE_ONE ;
            color->quantised.qcv[iterator.bit] =
              color->quantised.htmax[iterator.bit] ;
          }

          color->quantised.state = blit_quantise_unknown; /* masks don't care */

          if ( (state & blit_channel_override) == 0 ) {
            color->state[iterator.bit] |= blit_channel_override ;
            color->noverrides += 1 ;
          }
        } else if ( (state & (blit_channel_present|blit_channel_knockout)) == (blit_channel_present|blit_channel_knockout) ) {
          /* The colorant was not present, but the object was knocked out, so
             unpack filled in the erase color. This is even true where the
             caller (backdrop) is producing quantised colors, because the
             knockout and the colorants were set up in the original unpack. */
          HQASSERT((state & blit_channel_override) != 0,
                   "Knockout channel should be overriding") ;
        } else {
          /* The object can overprint with respect to this channel. */
          blit_channel_mark_absent(color, iterator.bit) ;
        }
        /* end of mask channel code */
      } else if ( (state & blit_channel_present) != 0 ) {
        /* We have a colorant value for this channel. */

        /* We should now be down to two options for the properties, since
           Mask is disallowed for rendering channels. */
        HQASSERT(RENDERING_PROPERTY_HAS_RENDER(properties) +
                 RENDERING_PROPERTY_HAS_KNOCKOUT(properties) == 1,
                 "More than one of render and knockout properties is true") ;

        if ( RENDERING_PROPERTY_HAS_KNOCKOUT(properties) ) {
          const blit_color_t *erase_color = color->map->erase_color;

          HQASSERT(!erase, "Can't be knocking out an erase");
          HQASSERT(erase_color != NULL, "No erase color for knockout");
          HQASSERT(erase_color->valid & blit_color_unpacked, "Erase color not unpacked yet") ;
          /* If color is already quantised, must have a quantised erase to
             use. If not, the quantised color set here will not be used. */
          HQASSERT((color->valid & blit_color_quantised) == 0
                   || (erase_color->valid & blit_color_quantised) != 0,
                   "Erase color not quantised yet") ;

          /* This object should knockout in this channel, and we have an
             erase color to take the channel value from. Knockouts override
             the natural color of this channel. */
          color->unpacked.channel[iterator.bit].cv =
            erase_color->unpacked.channel[iterator.bit].cv ;
          /** \todo The knockout might have been quantised with the wrong screen. */
          color->quantised.qcv[iterator.bit] =
            erase_color->quantised.qcv[iterator.bit] ;
          color->state[iterator.bit] =
            erase_color->state[iterator.bit] | blit_channel_knockout | blit_channel_override ;
          color->quantised.state = blit_quantise_unknown ;

          HQASSERT((color->state[iterator.bit] & blit_channel_present) != 0,
                   "Erase color was missing colorant") ;

          /* Is this a new override? */
          if ( (state & blit_channel_override) == 0 ) {
            HQASSERT(color->noverrides < color->ncolors,
                     "Too many override channels") ;
            color->noverrides += 1 ;
          }

          /* Did we change the maxblit state? */
          if ( (state & blit_channel_maxblit) != 0 ) {
            HQASSERT(color->nmaxblits > 0, "No maxblits left to remove") ;
            color->nmaxblits -= 1 ;
          }

          if ( (color->state[iterator.bit] & blit_channel_maxblit) != 0 ) {
            HQASSERT(color->nmaxblits < color->ncolors,
                     "Too many maxblit channels") ;
            color->nmaxblits += 1 ;
          }
        }
      } else {
        /* Colorant is not present, it was overprinted. */
        HQASSERT(state == blit_channel_missing,
                 "Overprinted channel should be missing") ;
      }
    } else {
      /* This is the Alpha or type channel. Alpha and type channels can only
         get render property. */
      HQASSERT((map->channel[iterator.bit].type == channel_is_type &&
                iterator.bit == map->type_index) ||
               (map->channel[iterator.bit].type == channel_is_alpha &&
                iterator.bit == map->alpha_index),
               "Channel should be alpha or type") ;
      HQASSERT(map->channel[iterator.bit].render_properties == RENDERING_PROPERTY_RENDER_ALL,
               "Alpha and type channels cannot be masked, knocked out or ignored") ;
    }
  }

#ifdef ASSERT_BUILD
  /* Packing and expansion are now invalid. */
  color->valid &= ~(blit_color_packed|blit_color_expanded) ;
#endif
}


Bool blit_map_mask_channel(const blit_colormap_t *map, channel_index_t index)
{
  HQASSERT(map != NULL, "No colormap to use");
  HQASSERT(index < map->nchannels, "Channel index out of range");
  return RENDERING_PROPERTY_HAS_MASK(map->channel[index].render_properties);
}


/* Build a colormap for a monochrome render (mask, clip, etc). */
void blit_colormap_mask(blit_colormap_t *map)
{
  HQASSERT(map != NULL, "No colormap to fill in") ;

  map->nchannels = 1 ;
  map->all_index = 0 ;   /* /All colorant is the only one */
  map->alpha_index = 1 ; /* No Alpha channel */
  map->type_index = 2 ;  /* No type channel */
  map->rendered[0] = 1 ; /* Bit mask for rendered channels */
  map->nrendered = 1 ;   /* One channel rendered */
  map->ncolors = 1 ;     /* It's a color channel, too */
  map->packed_bits = 1 ;
  map->expanded_bytes = sizeof(blit_t) ;
  map->override_htmax = 1 ;
  map->type_htmax = 255 ;
  map->type_lookup = NULL ;
  map->apply_properties = FALSE ; /* Don't need rendering properties */
  map->rasterstyle_id = 0;
  map->pack_quantised_color = blit_color_pack_mask ;
  map->expand_packed_color = blit_color_expand_mask ;
  map->overprint_mask = blit_overprint_mask_generic8 ;

  /* Set up a single channel for a 1-bit mask, mapping COLORANTINDEX_ALL to
     channel 0. Mask colours are never set from a DL colour, anyway. */
  map->channel[0].ci = COLORANTINDEX_ALL ;
  map->channel[0].pack_add = 0 ;
  map->channel[0].pack_mul = 1 ;
  map->channel[0].type = channel_is_color ;
  map->channel[0].bit_offset = 0 ;
  map->channel[0].bit_size = 1 ;
  map->channel[0].render_properties = RENDERING_PROPERTY_RENDER_ALL ;
  map->channel[0].colorant_info = NULL ;

  NAME_OBJECT(map, BLIT_MAP_NAME) ;
}


/* Set a mask color to black or white, including quantized and packed. */
void blit_color_mask(blit_color_t *color, Bool white)
{
  const blit_colormap_t *map ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->nchannels == 1 &&
           map->alpha_index != 0 &&
           map->type_index != 0 &&
           map->all_index == 0 &&
           map->nrendered == 1 &&
           map->rendered[0] == 1 &&
           map->packed_bits == 1 &&
           map->override_htmax == 1 &&
           map->channel[0].ci == COLORANTINDEX_ALL &&
           map->channel[0].pack_add == 0 &&
           map->channel[0].pack_mul == 1 &&
           map->channel[0].type == channel_is_color &&
           map->channel[0].bit_offset == 0 &&
           map->channel[0].bit_size == 1,
           "Color doesn't have a mask colormap") ;

  color->unpacked.channel[map->alpha_index].cv =
    color->quantised.qcv[map->alpha_index] = color->alpha = COLORVALUE_ONE ;
  color->unpacked.channel[map->type_index].cv =
    color->quantised.qcv[map->type_index] = color->type = 0 ;

  /* The mask only references one channel, don't bother looping over all
     of the channels. */
  color->state[0] = blit_channel_present ;

  color->ncolors = color->nchannels = 1 ;
  color->nmaxblits = color->noverrides = 0 ;
  color->quantised.htmax[0] = 1 ;

  if ( white ) { /* White */
    color->unpacked.channel[0].cv = COLORVALUE_ONE ;

    color->quantised.state = blit_quantise_max ;
    color->quantised.qcv[0] = 1 ;

    color->packed.channels.blits[0] = ALLONES ;
  } else { /* Black */
    color->unpacked.channel[0].cv = 0 ;

    color->quantised.state = blit_quantise_min ;
    color->quantised.qcv[0] = 0 ;

    color->packed.channels.blits[0] = 0 ;
  }

  /* This will never be used. */
  color->quantised.spotno = SPOT_NO_INVALID ;
#ifdef ASSERT_BUILD
  /* We've initialised all of the variants in one go. */
  color->valid = blit_color_unpacked|blit_color_quantised|blit_color_packed|blit_color_expanded ;
#endif
}


static void blit_color_pack_mask(blit_color_t *color)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 1 &&
           color->map->all_index == 0 &&
           color->map->alpha_index == 1 &&
           color->map->type_index == 2 &&
           color->map->rendered[0] == 1 &&
           color->map->nrendered == 1 &&
           color->map->ncolors == 1 &&
           color->map->packed_bits == 1 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           color->map->override_htmax == 1 &&
           color->map->type_htmax == 255 &&
           color->map->type_lookup == NULL &&
           !color->map->apply_properties,
           "Not a mask blit colormap in mask pack routine") ;
  color->packed.channels.blits[0] = (blit_t)(-(intptr_t)color->quantised.qcv[0]) ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif
  HQASSERT((color->packed.channels.blits[0] == 0 &&
            color->quantised.qcv[0] == 0) ||
           (color->packed.channels.blits[0] == ALLONES &&
            color->quantised.qcv[0] == 1),
           "Packed and quantised mask colors are inconsistent") ;
}

static void blit_color_expand_mask(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_expanded, "Blit color is not expanded") ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->map->nchannels == 1 &&
           color->map->all_index == 0 &&
           color->map->alpha_index == 1 &&
           color->map->type_index == 2 &&
           color->map->rendered[0] == 1 &&
           color->map->nrendered == 1 &&
           color->map->ncolors == 1 &&
           color->map->packed_bits == 1 &&
           color->map->expanded_bytes == sizeof(blit_t) &&
           color->map->override_htmax == 1 &&
           color->map->type_htmax == 255 &&
           color->map->type_lookup == NULL &&
           !color->map->apply_properties,
           "Not a mask blit colormap in mask expand routine") ;
  /* Pack routine did all of the work, just assert it's OK here. */
  HQASSERT((color->packed.channels.blits[0] == 0 &&
            color->quantised.qcv[0] == 0) ||
           (color->packed.channels.blits[0] == ALLONES &&
            color->quantised.qcv[0] == 1),
           "Packed and quantised mask colors are inconsistent") ;
}

/* Test if the color channels are overprinted. */
int blit_color_overprinted(const blit_color_t *color)
{
  const blit_colormap_t *map ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT((color->valid & blit_color_unpacked) ||
           (color->valid & blit_color_quantised),
           "Unpacked color is invalid; cannot test it") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  if ( color->ncolors == 0 )
    return BLT_OVP_ALL ;

  if ( color->ncolors == map->ncolors )
    return BLT_OVP_NONE ;

  return BLT_OVP_SOME ;
}

/* Test if any of the color channels are maxblitted. */
Bool blit_color_maxblitted(const blit_color_t *color)
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT((color->valid & blit_color_unpacked) ||
           (color->valid & blit_color_quantised),
           "Unpacked color is invalid; cannot test it") ;

  return color->nmaxblits != 0 ;
}


/* Return the colorant index to be used to select a halftone for the blit
   color. */
COLORANTINDEX blit_map_sole_index(const blit_colormap_t *map)
{
  COLORANTINDEX ci ;

  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  HQASSERT(map->nchannels == 1,
           "Should output exactly one channel for mono RLE/halftoning") ;
  HQASSERT(map->channel[BLIT_SOLE_CHANNEL].type == channel_is_color,
           "Output channel should be a color for mono RLE/halftoning") ;

  ci = map->channel[BLIT_SOLE_CHANNEL].ci;
  HQASSERT(ci == COLORANTINDEX_ALL || ci >= 0,
           "Output color channel index invalid for mono RLE/halftoning") ;
  if ( ci < 0 ) /* Got /All colorant - use default screen */
    ci = COLORANTINDEX_NONE;
  return ci;
}


/** Expand packed storage so that the data repeats sufficient times to make
    a multiple of blit_t size. This allows fast copy operations to be used
    for blitting.

    This function expands 8-bit packed colors.
 */
static void blit_pack_expand8(blit_packed_t *packed, const blit_colormap_t *map)
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
    uint8 *src = &packed->bytes[0];
    uint8 *dest = &packed->bytes[packed_bits >> 3];
    uint8 *limit = &packed->bytes[expanded_bytes];
    uint8 residual ;
    unsigned int shift = (packed_bits & 7) ;

    HQASSERT(limit > dest,
             "Destination and limit cannot be equal if LCM is different") ;

    /* The residual data is the overflow bits in the last byte of the packed
       size. */
    residual = CAST_UNSIGNED_TO_UINT8(*dest & ~(0xffu >> shift)) ;

    if ( packed_bits < 8 ) {
      /* Expand the packed size to at least a byte by doubling the bits used. */
      uint16 expanded = residual << 8 ;

      HQASSERT(src == dest, "Less than one byte should have src equals dest") ;
      do {
        expanded |= expanded >> packed_bits ;
        packed_bits <<= 1 ;
      } while ( packed_bits < 8 ) ;
      *dest++ = (uint8)(expanded >> 8) ;
      residual = (uint8)expanded ;
      shift = (packed_bits & 7) ;
    }

    if ( shift == 0 ) { /* Byte multiples are easy to expand */
      /* Deliberately copy upwards, overlapping src and dest. Don't use
         HqMemCpy(), it won't do the right thing. */
      do {
        *dest++ = *src++ ;
      } while ( dest < limit ) ;
    } else { /* Need to shift bits to replicate bytes. */
      do { /* Deliberately copy upwards overlapping src and dest. */
        *dest++ = CAST_UNSIGNED_TO_UINT8(residual | (*src >> shift)) ;
        residual = (uint8)(*src++ << (8 - shift)) ;
      } while ( dest < limit ) ;
    }
  }
}

/** Expand packed storage so that the data repeats sufficient times to make
    a multiple of blit_t size. This allows fast copy operations to be used
    for blitting. */
static void blit_pack_expand16(blit_packed_t *packed, const blit_colormap_t *map)
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

/* Create a packed output blit mask for the bitvector corresponding
   to a channel mask. */
void blit_overprint_mask_generic8(blit_packed_t *packed,
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
      unsigned int shift, byteindex ;

      /* Check that the channel has a suitable size */
      HQASSERT(size > 0, "Channel to be packed has no size") ;
      HQASSERT(size <= sizeof(channel_output_t) * 8,
               "Packed size larger than storage unit") ;

      /* Check that channel fits in space allocated */
      HQASSERT(offset + size <= sizeof(channel_output_t) * 8 * BLIT_MAX_CHANNELS,
               "Packing channel outside of allocated storage") ;

      /* Pack channel high-bit first into slot. We pack it using byte-sized
         units, so we get a consistent output for low- and high-endian
         architectures. */
      shift = (offset & 7) + size ;
      byteindex = (offset >> 3) ;

      if ( shift > 8 ) {
        /* The output straddles two or more bytes */
        unsigned int remainder = shift - 8 ;

        packed->bytes[byteindex++] |= (uint8)(output >> remainder) ;

        while ( remainder > 8 ) {
          remainder -= 8 ;
          shift -= 8 ;
          packed->bytes[byteindex++] = 0xFF;
        }

        /* Fall through to the final byte */
        shift = 16 - shift ;
      } else {
        /* The output fits in one byte. */
        shift = 8 - shift ;
      }

      /* The final byte of multi-byte, or sole byte of a single-byte pack
         uses the output mask we computed earlier. */
      HQASSERT(shift >= 0 && shift < 8, "Shift out of range") ;
      packed->bytes[byteindex] |= (uint8)(output << shift) ;
    }
  }

  blit_pack_expand8(packed, map) ;
}

/** Create a packed output blit mask for the bitvector corresponding
    to a channel mask. */
void blit_overprint_mask_generic16(blit_packed_t *packed,
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

      /* The final short of multi-short, or sole byte of a single-short pack
         uses the output mask we computed earlier. */
      HQASSERT(shift >= 0 && shift < 16, "Shift out of range") ;
      packed->shorts[shortindex] |= (uint16)(output << shift) ;
    }
  }

  if ( map->packed_bits < 16 )
    blit_pack_expand16(packed, map) ;
}


/* Generate the two mappings needed for backdrop/image expansion, one
   for the expander, the other for the pixel extractor. */
void blit_expand_mapping(blit_color_t *color,
                         COLORANTINDEX *colorants, size_t n_comps,
                         Bool overrides,
                         int expanded_to_plane[],
                         unsigned int *expanded_comps,
                         int blit_to_expanded[])
{
  const blit_colormap_t *map ;
  channel_index_t i ;
  unsigned int nexpanded = 0;
  Bool uses_all = FALSE ;
  blit_channel_state_t mask ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(expanded_to_plane != NULL, "Nowhere to put plane mappings") ;
  HQASSERT(blit_to_expanded != NULL, "Nowhere to put blit mappings") ;
  HQASSERT(expanded_comps != NULL, "Nowhere to put expanded size") ;

  /* Prepare the blit color in the same way that blit_color_unpack() does.
     We're going to stuff in values from the backdrop, but these values are
     already quantised. It's not clear whether we should always reverse the
     quantisation to generate a valid unpacked color, or whether we should
     just create a dummy unpacked color, or even leave it invalid and assume
     that the lower levels never need to look at it (this probably isn't a
     good assumption for trapping). */
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  /* This mask is used to test which channels are expanded. Normal
     images test whether the colorant is present and not overridden,
     whereas backdrops may have different render properties applied per
     block, so the override status applied through unpacking the DL
     color is meaningless for mask and knockout channels. */
  mask = blit_channel_present | (overrides ? blit_channel_override : 0);

  /** \todo ajcd 2008-09-15:
      Determine whether the output happens to map the packed format,
      regardless of whether channels are currently rendering. If so, we'll
      preserve the gaps in the channels in the expanders so we can set the
      packed value as well as the quantised value. */

  for ( i = 0 ; i < map->nchannels ; ++i ) {
    blit_to_expanded[i] = -1 ;

    /* Only expand data for channels that are present and are not overridden. */
    if ( (color->state[i] & mask) == blit_channel_present ) {
      /* Add mapping for blit_to_expanded, and make sure that is reflected
         in mapping for expanded_to_plane. */
      COLORANTINDEX ci = map->channel[i].ci ;

      /* We'll process the colorants mapping to /All in a second pass. */
      if ( ci == COLORANTINDEX_ALL ) {
        HQASSERT(map->channel[i].type == channel_is_color,
                 "Unrecognised channel type") ;
        uses_all = TRUE ;
      } else if ( map->channel[i].type != channel_is_type ) {
        int planeindex;

        HQASSERT(map->channel[i].type == channel_is_color ||
                 map->channel[i].type == channel_is_alpha,
                 "Unrecognised channel type") ;
        HQASSERT(ci == COLORANTINDEX_ALPHA || ci >= 0,
                 "Unrecognised colorant index") ;

        /* Find the index for this plane */
        for ( planeindex = (int)n_comps ; --planeindex >= 0 ; ) {
          if ( colorants[planeindex] == ci )
            break ;
        }

        if ( planeindex >= 0 ) {
          unsigned int iexpanded ;

          /* A matching plane was found. In future, it may be possible
             to have multiple blit channels with the same colorant but
             different render properties, so we'll check if we're
             already expanding this plane, and use the same data if
             possible. */
          for ( iexpanded = 0 ; iexpanded < nexpanded ; ++iexpanded ) {
            if ( expanded_to_plane[iexpanded] == planeindex ) {
              --nexpanded ; /* Compensate for increment below, we're re-using
                               a channel. */
              break ;
            }
          }

          expanded_to_plane[iexpanded] = planeindex ;
          blit_to_expanded[i] = iexpanded ;
          ++nexpanded ;
        } else if ( ci >= 0 ) {
          /* This channel should map to /All */
          HQASSERT(map->channel[i].type == channel_is_color,
                   "Unrecognised channel type") ;
          uses_all = TRUE ;
        }
      }
    }
  }

  if ( uses_all ) {
    int planeindex;

    for ( planeindex = (int)n_comps ; --planeindex >= 0 ; ) {
      if ( colorants[planeindex] == COLORANTINDEX_ALL )
        break ;
    }

    if ( planeindex >= 0 ) { /* The /All plane exists */
      for ( i = 0 ; i < map->nchannels ; ++i ) {
        /* Look for channels that weren't mapped last time, and see if they
           should map to /All. */
        if ( blit_to_expanded[i] < 0 &&
             (color->state[i] & mask) == blit_channel_present &&
             map->channel[i].type == channel_is_color )
          blit_to_expanded[i] = nexpanded ;
      }

      expanded_to_plane[nexpanded] = planeindex ;
      ++nexpanded ;
    }
  }
  HQASSERT((size_t)nexpanded <= n_comps,
           "Expanded data larger than space allocated for it") ;
  *expanded_comps = nexpanded ;
}


/* Log stripped */
