/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:blitcolort.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Render time representations of colors.
 */

#ifndef __BLITCOLORT_H__
#define __BLITCOLORT_H__ 1

typedef unsigned int channel_index_t ;
typedef uint16 channel_output_t ;
typedef uint8 object_type_t ;

/** The maximum number of channels that can be simultaneously rendered. */
#define BLIT_MAX_CHANNELS 256

/** The maximum number of real colorants that can be simultaneously rendered,
    excluding alpha, /All and object type. */
#define BLIT_MAX_COLOR_CHANNELS (BLIT_MAX_CHANNELS - 3)

/** \brief Type for blit color map.

    The blit color map is derived from the output raster style. It defines the
    mapping from unpacked channels to quantised levels, and from quantised
    levels to packed bits. */
typedef struct blit_colormap_t blit_colormap_t ;

/** \brief Type for collected blit colors.

    The blit color collection is used by routines that derive one form of the
    blit color to another. It collects the unpacked, quantised, and packed
    colors together. */
typedef struct blit_color_t blit_color_t ;

/** \brief Type for packed blit channels.

    Packed channel data for blits and masks used for blits. */
typedef union blit_packed_t blit_packed_t ;

/** \brief Type for the state of blit channels. */
typedef uint8 blit_channel_state_t ;

/** \brief Bit flags for the possible states of blit color channels.

    Blit channel states are stored in variables of type \c
    blit_channel_state_t. These flags should be combined into those variables.
    If the channel has any content, \c blit_channel_present should be set. */
enum {
  blit_channel_missing = 0, /**< There is no value for this blit channel (it is
                               either overprinted or is not being rendered). */
  blit_channel_present = 1,  /**< The blit channel is not overprinted. */
  blit_channel_override = 2, /**< The blit channel's color is a constant
                                color, no variation is allowed within
                                self-colored objects (non-backdrop images and
                                shfills). */
  blit_channel_knockout = 4, /**< The blit channel's color is a knockout
                                derived from the erase color. */
  blit_channel_maxblit = 8   /**< The blit channel's color is a maxblit
                                overprint. */
} ;

/** \brief Type for the quantisation state of blit channels. */
typedef uint8 blit_quantise_state_t ;

/** \brief Bit flags for the quantisation state of the blit color.

    \c blit_color_quantise sets a flag in the quantised blit color indicating
    whether zero, max, and/or intermediate values are present in the blit
    color. This can be used for blit optimisations, and to determine if a
    halftone form should be selected. The blit quantisation state can also be
    lazily set by \c blit_quantise_state.
 */
enum {
  blit_quantise_unknown = 0, /**< Reset to this value for lazy evaluation. */
  blit_quantise_min = 1,     /**< Minimum channel value is present. */
  blit_quantise_max = 2,     /**< Maximum channel value is present. */
  blit_quantise_mid = 4      /**< Intermediate channel value is present. */
} ;

#endif /* protection for multiple inclusion */

/* Log stripped */
