/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:blitcolorh.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Render-time representations of colors, functional interface.
 */

#ifndef __BLITCOLORH_H__
#define __BLITCOLORH_H__ 1

#include "blitcolort.h"

struct dl_color_t ;
struct LateColorAttrib;


/** \brief Initialise a set of colors, and associate them with a colormap.

    \param[out] color   The blit color to initialise
    \param[in] colormap The colormap with which the blit color will be
                        associated.
*/
void blit_color_init(/*@out@*/ /*@notnull@*/ blit_color_t *color,
                     /*@in@*/ /*@notnull@*/ const blit_colormap_t *colormap) ;

/** \brief Unpack a DL color into the unpacked blit color, marking the
    quantised and packed colors as invalid.

    \param[out] color   The blit color to unpack into.
    \param[in] dlc      A DL color to unpack
    \param label        The object label associated with this blit color. If
                        the label is 0, this is an erase color, and properties
                        will be ignored.
    \param lca          The late-color attributes for this blit color
                        (currently, only rendering intent is used).
    \param knockout     This color is being used for an object that is
                        knocking out other objects.
    \param selected     This is an object of interest for a mask channel.
                        This parameter is used as an extra filter, primarily
                        for modular halftoning, to select the objects for which
                        masks will be generated.
    \param erase        This color is the erase color.

    When finished, the field \c color->nchannels will be set to the number of
    channels set, \c color->ncolors will be set to the number of those that
    are real colorants, the bitvectors \c color->missing and \c
    color->maxblts will be set appropriately, the unpacked color channels
    will be filled in.

    The erase color will the stored in the blitmap as it is unpacked, so its
    lifetime must be longer. It must be the first color unpacked for any
    map. After that, knocked-out channels will be taken from it.
*/
void blit_color_unpack(/*@out@*/ /*@notnull@*/ blit_color_t *color,
                       /*@in@*/ /*@notnull@*/ const struct dl_color_t *dlc,
                       object_type_t label,
                       struct LateColorAttrib *lca,
                       Bool knockout, Bool selected,
                       Bool is_erase, Bool is_knockout);

/** \brief Ensure that the quantised color has the levels set for a particular
    screen.

    \param[in,out] color  The blit color to add screen information to.
    \param screen         The spot number to associate with this blit color.
    \param type           The halftone type to associate with this blit color.

    This function ensures that the maximum quantised level fields are set for
    all rendered channels. It must be called before quantising a blit color.
*/
void blit_quantise_set_screen(/*@out@*/ /*@notnull@*/ blit_color_t *color,
                              SPOTNO screen, HTTYPE type);

/** \brief Test if all or any of the quantised channels are
    zero/max/intermediate values.

    \param[in] color  The color to test.
    \return  One of the \c blit_quantise_* (not \c blit_quantise_unknown). */
blit_quantise_state_t blit_quantise_state(/*@in@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Ensure that the quantised blit color based on an unpacked color is
    valid.

    \param[in,out] color   The blit color to quantise.

    On exit, the quantised values are set for all channels present. The
    \c quantised.state indicates if the minimum, maximum, and other
    channel values occurred. This can be used to determine if
    optimised solid/clear functions or halftoning functions should be
    used when rendering the blit color. The quantised value of the type
    channel will be looked up in the type lookup array (if specified), but
    the top-level \c color->type field will be left reflecting the original
    pixel label.
*/
void blit_color_quantise(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Convert quantised colors back to unpacked colors.

    \param[in,out] color   The blit color to dequantise.

    The image expanders store quantised colors in the blit color, but don't
    normally convert that back to an unpacked color. This function converts
    them back, for cases that the unpacked color is needed.
*/
void blit_color_dequantise(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Pack the blit color based on a valid quantised color.

    \param[in,out] color   The blit color to pack.

    On exit, the packed channel data is filled in. The packed channel data is
    used for contone output, it stores the channels in the bit depths,
    interleaving and structure required for copying directly into the output
    rasters.

    This function packs colors using 8-bit packing units.
*/
void blit_color_pack_generic8(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Pack the blit color based on a valid quantised color.

    \param[in,out] color   The blit color to pack.

    On exit, the packed channel data is filled in. The packed channel data is
    used for contone output, it stores the channels in the bit depths,
    interleaving and structure required for copying directly into the output
    rasters.

    This function packs colors using 16-bit packing units.
*/
void blit_color_pack_generic16(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Macro to call the blit colormap-specific pack method. */
#define blit_color_pack(c_) MACRO_START \
  register blit_color_t *_c_ = (c_) ; \
  const blit_colormap_t *_m_ ; \
  VERIFY_OBJECT(_c_, BLIT_COLOR_NAME) ; \
  _m_ = _c_->map ; \
  VERIFY_OBJECT(_m_, BLIT_MAP_NAME) ; \
  (*_m_->pack_quantised_color)(_c_) ; \
MACRO_END

/** \brief Expand a packed blit color to a whole number of blit_t words.

    \param[in,out] color   The blit color to expand.

    On exit, the packed channel data is expanded (if possible) to a multiple
    of the \c blit_t size. If the color map has a non-zero \c expanded_bytes
    field, the data is replicated to fill a whole number of blit_t words.
    This is useful for writing efficient blitters.

    This function expands colors using 8-bit packing units.
*/
void blit_color_expand_generic8(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Expand a packed blit color to a whole number of blit_t words.

    \param[in,out] color   The blit color to expand.

    On exit, the packed channel data is expanded (if possible) to a multiple
    of the \c blit_t size. If the color map has a non-zero \c expanded_bytes
    field, the data is replicated to fill a whole number of blit_t words.
    This is useful for writing efficient blitters.

    This function expands colors using 16-bit packing units.
*/
void blit_color_expand_generic16(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color) ;

/** \brief Macro to call the blit colormap-specific expansion method. */
#define blit_color_expand(c_) MACRO_START \
  register blit_color_t *_c_ = (c_) ; \
  const blit_colormap_t *_m_ ; \
  VERIFY_OBJECT(_c_, BLIT_COLOR_NAME) ; \
  _m_ = _c_->map ; \
  VERIFY_OBJECT(_m_, BLIT_MAP_NAME) ; \
  (*_m_->expand_packed_color)(_c_) ; \
MACRO_END

/** \brief Remove an indexed channel from a color, adjusting the appropriate
    color properties.

    \param[in,out] color The blit color from which a channel will be removed.
    \param index         The index of the color that is to be absent.

    This function is used to assist application of render properties, and when
    expanding LW image stores. If the indexed channel is present in the
    color, it will be removed, leaving the knockout, override, and maxblit
    markers set in case the channel is reinstated.
*/
void blit_channel_mark_absent(/*@in@*/ /*@notnull@*/ blit_color_t *color,
                              channel_index_t index) ;

/** \brief Add an indexed channel to a color, adjusting the appropriate
    color properties.

    \param[in,out] color The blit color to which channel will be added.
    \param index         The index of the color that is to be present.

    This function is used to assist application of render properties, and when
    expanding LW image stores. If the indexed channel is absent in the
    color, it will be added, leaving the knockout, override, and maxblit
    markers set in case the channel is reinstated.
*/
void blit_channel_mark_present(/*@in@*/ /*@notnull@*/ blit_color_t *color,
                               channel_index_t index) ;

/** \brief Apply the channel rendering properties to an unpacked and/or
    quantised color.

    \param[in,out] color The blit color to which rendering properties will be
                         applied.
    \param selected     The color is being used for an object of interest.
                        This parameter is used as an extra filter, primarily
                        for modular halftoning, to select the objects for which
                        masks will be generated.
    \param erase        This color is the erase color.

    This function should be called if the blit color map's \c apply_properties
    field is set, to force application of properties for masking, ignoring,
    or knocking out channels. It can be applied to unpacked and quantised
    colors, and will reset the packed status of the color. */
void blit_apply_render_properties(/*@in@*/ /*@out@*/ /*@notnull@*/ blit_color_t *color,
                                  Bool selected, Bool erase);

/** \brief Create a packed output blit mask for the bitvector corresponding
    to a channel mask.

    \param[out] packed Packed channel data which will be set to contain zero
                       for all bits corresponding to overprinted channels, ones
                       for all bits corresponding to channels present.
    \param[in] color   A color whose state is matched to extract the mask data.
    \param mask        A channel state mask; this mask is used to extract a
                       set of bits from each channel's state. If the bits
                       extracted match the \a state parameter, the overprint
                       mask bits will be set for this channel.
    \param state       A channel state which the bits extracted through
                       the \a mask parameter are compared.

    The output from this function is a mask which is packed and then expanded
    in exactly the same format as the blit color's packed channel data. This
    mask can be used to mask the blit color when overprinting into raster
    buffers.

    This function expands colors using 8-bit packing units.
*/
void blit_overprint_mask_generic8(/*@out@*/ /*@notnull@*/ blit_packed_t *packed,
                                  /*@in@*/ /*@notnull@*/ const blit_color_t *color,
                                  blit_channel_state_t mask,
                                  blit_channel_state_t state) ;

/** \brief Create a packed output blit mask for the bitvector corresponding
    to a channel mask.

    \param[out] packed Packed channel data which will be set to contain zero
                       for all bits corresponding to overprinted channels, ones
                       for all bits corresponding to channels present.
    \param[in] color   A color whose state is matched to extract the mask data.
    \param mask        A channel state mask; this mask is used to extract a
                       set of bits from each channel's state. If the bits
                       extracted match the \a state parameter, the overprint
                       mask bits will be set for this channel.
    \param state       A channel state which the bits extracted through
                       the \a mask parameter are compared.

    The output from this function is a mask which is packed and then expanded
    in exactly the same format as the blit color's packed channel data. This
    mask can be used to mask the blit color when overprinting into raster
    buffers.

    This function expands colors using 16-bit packing units.
*/
void blit_overprint_mask_generic16(/*@out@*/ /*@notnull@*/ blit_packed_t *packed,
                                   /*@in@*/ /*@notnull@*/ const blit_color_t *color,
                                   blit_channel_state_t mask,
                                   blit_channel_state_t state) ;

/** \brief Macro to call the blit colormap-specific mask method. */
#define blit_overprint_mask(packed_, color_, mask_, state_) MACRO_START \
  register blit_color_t *_c_ = (color_) ; \
  const blit_colormap_t *_m_ ; \
  VERIFY_OBJECT(_c_, BLIT_COLOR_NAME) ; \
  _m_ = _c_->map ; \
  VERIFY_OBJECT(_m_, BLIT_MAP_NAME) ; \
  (*_m_->overprint_mask)(packed_, _c_, mask_, state_) ; \
MACRO_END


/** Test if the given channel is a mask channel.

    \param[in] map    The colormap to interrogate.
    \param[in] index  The index of the channel being queried.
 */
Bool blit_map_mask_channel(const blit_colormap_t *map, channel_index_t index);


/** \brief Build a colormap for a monochrome render (mask, clip, etc), with
    a single colorant.

    \param[out] map   The colormap to build.

    The colorant index used for mask colormaps is COLORANTINDEX_ALL, however
    it will not normally need to be used, because mask colors are usually set
    through \c blit_color_mask(). This colormap does NOT need to be
    destroyed, unlike those associated with rasterstyles.
*/
void blit_colormap_mask(/*@out@*/ /*@notnull@*/ blit_colormap_t *map) ;

/** \brief Set the values for a mask color.

    \param[in,out] color  The color to set.
    \param white          TRUE is the mask color should be set to white, FALSE
                          if it should be set to black.

    This function sets all of the unpacked, quantised, and packed data for a
    mask color. The color must have been associated with a mask colormap
    using \c blit_color_init().
*/
void blit_color_mask(/*@out@*/ /*@notnull@*/ blit_color_t *color, Bool white) ;

/** \brief Test if any or all of the color channels are overprinted.

    \param[in] color  The color to test.

    The return value is one of the BLT_OVP_* enumeration values. */
int blit_color_overprinted(/*@in@*/ /*@notnull@*/ const blit_color_t *color) ;

/** \brief Test if any of the color channels are maxblitted.

    \param[in] color  The color to test.
    \retval TRUE      Indicates that some channels require maxblits.
    \retval FALSE     No channels require maxblits.
 */
Bool blit_color_maxblitted(/*@in@*/ /*@notnull@*/ const blit_color_t *color) ;

/** \brief Return the colorant index for selecting the screen, when rendering a
    single channel at a time.

    \param[in] map  The color map associated with a render state.

    This can be used when not pixel interleaving, to get the colorant index
    of the sole color channel that is rendered for frame-, band-interleaved
    and separations. For an /All channel, COLORANTINDEX_NONE is returned.

    \todo This function is a stop-gap, until we work out what we want to do
    about pixel-interleaved halftoning. */
COLORANTINDEX blit_map_sole_index(/*@in@*/ /*@notnull@*/ const blit_colormap_t *map) ;

/** \brief Generate the two mappings needed for backdrop/image
     expansion, one for the expander, the other for the pixel
     extractor.

    \param[in] color     A blit color to which the color will be output.
    \param[in] colorants Array of the colorant indices in the input.
    \param[in] n_comps   Length of the \a colorants array.
    \param[in] overrides Whether to obey override flags in \a color.
    \param[out] expanded_to_plane Mapping from expansion buffer to input.
    \param[out] expanded_comps    Number of channels in expansion buffer.
    \param[out] blit_to_expanded  Mapping from blit color to expansion buffer.
*/
void blit_expand_mapping(blit_color_t *color,
                         COLORANTINDEX *colorants, size_t n_comps,
                         Bool overrides,
                         int expanded_to_plane[],
                         unsigned int *expanded_comps,
                         int blit_to_expanded[]);


#endif /* protection for multiple inclusion */

/* Log stripped */
