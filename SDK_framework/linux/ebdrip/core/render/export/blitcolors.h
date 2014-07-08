/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:blitcolors.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Render-time representations of colors.
 */

#ifndef __BLITCOLORS_H__
#define __BLITCOLORS_H__ 1

#include "blitcolort.h"
#include "bitbltt.h"
#include "hqbitvector.h"
#include "objnamer.h"

struct GUCR_COLORANT_INFO ; /* from SWv20 */

/** This definition exists to prevent nasty constants from appearing
    throughout the code, and to make the places where the assumption that
    non-pixel interleaved code can have only one valid channel visible.*/
#define BLIT_SOLE_CHANNEL 0

#define BLIT_MAP_NAME "Blit channel map"

struct blit_colormap_t {
  channel_index_t nchannels ;   /**< 0 <= nchannels <= BLIT_MAX_CHANNELS */
  channel_index_t all_index ;   /**< nchannels <= all_index < BLIT_MAX_CHANNELS if no /All */
  channel_index_t alpha_index ; /**< nchannels <= alpha_index < BLIT_MAX_CHANNELS if no alpha */
  channel_index_t type_index ;  /**< nchannels <= type_index < BLIT_MAX_CHANNELS if no type */
  bitvector_t(rendered, BLIT_MAX_CHANNELS) ; /**< Channels to be rendered. */
  channel_index_t nrendered ;   /**< Number of channels to be rendered. */
  channel_index_t ncolors ;     /**< Number of color channels to be rendered. */
  unsigned int packed_bits ;    /**< Total bits when packed, including padding. */
  unsigned int expanded_bytes ; /**< Total bytes when expanded, or zero if no expansion. */
  blit_color_t *erase_color;    /**< The erase color. */
  blit_color_t *knockout_color; /**< The knockout color. */
  uint32 rasterstyle_id; /**< The ID of the rasterstyle this was built from. */
  channel_output_t override_htmax; /**< Override for max quantised level. 0 means no override. */
  /* override_htmax is a little hacky. It exists for use with mask maps and for
     the contone rendering for modular halftones. It's also currently used for
     all of the contone rasterstyles, so we don't call ht_getClear() for
     them. However, when we start allowing different contone channel depths,
     we'll need an array read from the rasterstyle for those. */
  channel_output_t type_htmax ; /**< Max quantised level for type channel. */
  channel_output_t *type_lookup ; /**< Lookup table for type, NULL if none. */
  Bool apply_properties ;       /**< Render properties need applying. */
  /** Method to pack a quantised color. This method pointer must be set. */
  void (*pack_quantised_color)(blit_color_t *color) ;
  /** Method to expand a packed color. This method pointer must be set. */
  void (*expand_packed_color)(blit_color_t *color) ;
  /** Method to generate and expand an overprint mask. This method pointer
      must be set. */


  /* Move OBJECT_NAME_MEMBER into the middle of the struct so that we can
   * make the chanel array at the end variable size.
   */
  OBJECT_NAME_MEMBER

  void (*overprint_mask)(blit_packed_t *packed, const blit_color_t *color,
                         blit_channel_state_t mask,
                         blit_channel_state_t state) ;
  uint32 alloced_channels; /**< Number of channenls alloced in struct below */
  struct { /* Channel->color lookup */
    /** Colorant index. COLORANTINDEX_UNKNOWN if not rendered,
        COLORANTINDEX_NONE for type/alpha, COLORANTINDEX_ALL for the /All
        channel. */
    COLORANTINDEX ci ;
    channel_output_t pack_add ; /**< Packed value = quantised * pack_mul + pack_add  */
    int16 pack_mul ;            /**< Packed value = quantised * pack_mul + pack_add  */
    enum {
      channel_is_color,         /**< Channel represents real colorant. */
      channel_is_alpha,         /**< Channel represents alpha value. */
      channel_is_type,          /**< Channel represents object type. */
      channel_is_special        /**< Channel represents special colorant. */
    } type ; /* special from trap stuff */
    unsigned int bit_offset ;   /**< Packing bit offset; strictly increasing. */
    unsigned int bit_size ;     /**< Packing size. bit_size > 0, bit_size+bit_offset <= next channel offset */
    uint32 render_properties ;  /**< How to render objects in this channel. */
    const struct GUCR_COLORANT_INFO *colorant_info ; /**< Colorant info, or NULL if mask map, alpha, or type. */
  } channel[BLIT_MAX_CHANNELS] /* Actually 'alloced_channels' variable size */ ;
} ;

/* Predicates for channel assertions and tests in optimised blit color
   packing. */
#define channel_is_8bit(map_, ch_) \
  ((map_)->channel[ch_].bit_size == 8 && \
   (map_)->channel[ch_].bit_offset == 8 * (ch_))
#define channel_is_16bit(map_, ch_) \
  ((map_)->channel[ch_].bit_size == 16 && \
   (map_)->channel[ch_].bit_offset == 16 * (ch_))
#define channel_is_positive(map_, ch_) \
  ((map_)->channel[ch_].pack_add == 0 && \
   (map_)->channel[ch_].pack_mul == 1)
#define channel_is_negative8(map_, ch_) \
  ((map_)->channel[ch_].pack_add == 255 && \
   (map_)->channel[ch_].pack_mul == -1)
#define channel_is_negative16(map_, ch_) \
  ((map_)->channel[ch_].pack_add == COLORVALUE_MAX && \
   (map_)->channel[ch_].pack_mul == -1)

/** Different ways of looking at packed output data: */
union blit_packed_t {
  /** Max BLIT_MAX_CHANNELS channels * 16-bit blit represented as bytes */
  uint8 bytes[BLIT_MAX_CHANNELS * sizeof(channel_output_t)] ;
  /** Max BLIT_MAX_CHANNELS channels * 16-bit blit represented as short */
  uint16 shorts[BLIT_MAX_CHANNELS * sizeof(channel_output_t) / sizeof(uint16)] ;
  /** Max BLIT_MAX_CHANNELS channels * 16-bit blit represented as word */
  uint32 words[BLIT_MAX_CHANNELS * sizeof(channel_output_t) / sizeof(uint32)] ;
  /** Max BLIT_MAX_CHANNELS channels * 16-bit blit represented as blit */
  blit_t blits[BLIT_MAX_CHANNELS * sizeof(channel_output_t) / sizeof(blit_t)] ;
} ;

#define BLIT_COLOR_NAME "Combined blit color"

struct blit_color_t {
  const blit_colormap_t *map ;  /**< Color channel mapping for this output phase. */
  COLORVALUE alpha ;      /**< Original alpha for this blit. */
  object_type_t type ;    /**< Unmapped object type for this blit. */
  uint8 rendering_intent; /**< Rendering intent for this blit. */
  channel_index_t ncolors ; /**< Number of non-omitted color channels. */
  channel_index_t nmaxblits ; /**< Number of non-omitted maxblitted color channels. */
  channel_index_t noverrides ; /**< Number of non-omitted knockout color channels. */
  channel_index_t nchannels ; /**< Number of non-omitted color, alpha, and type channels. */
  blit_channel_state_t state[BLIT_MAX_CHANNELS] ; /**< Channel state. */

  /** Color channel data unpacked from DL color. */
  struct {
    union {
      COLORVALUE cv ;    /**< Traditional 16-bit colorvalue (transitional). */
      uint8 label ;      /**< Object type. */
    } channel[BLIT_MAX_CHANNELS] ;
  } unpacked ;

  /** Color channel data quantised to HT/tone levels. */
  struct {
    SPOTNO spotno ; /**< The screen used for quantisation. */
    HTTYPE type; /**< The object type used for quantisation. */
    blit_quantise_state_t state ; /**< Color quantisation state. */
    channel_output_t qcv[BLIT_MAX_CHANNELS] ; /**< Quantised color value. */
    channel_output_t htmax[BLIT_MAX_CHANNELS] ; /**< Max level for this screen. */
  } quantised ;

  /** Color channel data packed into output bytes. */
  struct {
    blit_packed_t channels ; /**< Packed data for output data. */
  } packed ;

#ifdef ASSERT_BUILD
  /** Bitmask for color validity flags. These are only used to assert that
      the color is in the right state. */
  enum {
    blit_color_invalid = 0,
    blit_color_unpacked = 1,
    blit_color_quantised = 2,
    blit_color_packed = 4,
    blit_color_expanded = 8
  } valid ;
#endif

  OBJECT_NAME_MEMBER
} ;


#endif /* protection for multiple inclusion */

/* Log stripped */
