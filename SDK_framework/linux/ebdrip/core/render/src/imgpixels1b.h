/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:imgpixels1b.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the definition of the 1xN backward image pixel extractor.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro FUNCTION expands to the function name to be defined.
 *
 * The macro EXPAND_BITS expands to the width (8 or 16) of the output.
 *
 * The macro BLIT_COLOR_PACK(color) is used to pack the blit color.
 *
 * The function parameters are:
 *
 *   color            - The render_blit_t state pointer.
 *   buffer           - A pointer to the expansion buffer. The pointer will be
 *                      updated to the next pixel to be read from the expander.
 *   npixels          - A pointer to the maximum number of pixels to read. The
 *                      pointer will be updated to the number of pixels
 *                      extracted.
 *   nexpanded        - The number of channels expanded. This parameter is
 *                      unused in this function.
 *   blit_to_expanded - A mapping of the blit channels to expander buffer
 *                      channels.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

/** The expansion unit is an unsigned int of the appropriate width. */
#define expand_t SUFFIX2(uint,EXPAND_BITS)

/** \brief Add the expansion unit width to the end of a token name

    We need a three-level macro expansion to guarantee expansion of the
    preprocessing tokens we're concatenating. expand_t introduces the
    EXPAND_BITS qualifier on names, but this is a macro which we want expanded
    itself. The SUFFIX2 level guarantees that its arguments are fully
    expanded (C99, 6.10.3.1) before expanding SUFFIX3, which performs
    the token concatenation. */
#define SUFFIX2(x_,y_) SUFFIX3(x_,y_)
#define SUFFIX3(x_,y_) x_ ## y_

static forceinline void FUNCTION(blit_color_t *color,
                                 const void **buffer,
                                 int32 *npixels,
                                 unsigned int nexpanded,
                                 const int blit_to_expanded[])
{
  const expand_t *current ;
  expand_t value ;
  int32 remaining ;

  UNUSED_PARAM(unsigned int, nexpanded) ;
  UNUSED_PARAM(const int *, blit_to_expanded) ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(can_expand_pixels_1xN(color, nexpanded, blit_to_expanded),
           "Should not be expanding 1xN pixels") ;
  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Quantised screen not set for expander color") ;

  current = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  /* Expand this color into the blit entry. See im_expand_color_8/16 for
     details; this is an inlined optimised version taking into account the
     channel and map restrictions asserted on entry. */
  color->quantised.qcv[0] = value = *current ;
  HQASSERT(color->quantised.qcv[0] <= color->quantised.htmax[0],
           "Expanded color is not quantised to valid range") ;
  if ( value == 0 )
    color->quantised.state = blit_quantise_min ;
  else if ( value >= color->quantised.htmax[0] )
    color->quantised.state = blit_quantise_max ;
  else
    color->quantised.state = blit_quantise_mid ;

#ifdef ASSERT_BUILD
  /* Force re-packing of blit color. */
  color->valid &= ~blit_color_packed ;
  color->valid |= blit_color_quantised ;
#endif
  BLIT_COLOR_PACK(color) ;

  remaining = *npixels ;
  do {
    --current ;
    --remaining ;
  } while ( remaining != 0 && *current == value ) ;

  *buffer = current ;
  *npixels -= remaining ;
}

#undef FUNCTION
#undef SUFFIX2
#undef SUFFIX3
#undef EXPAND_BITS
#undef BLIT_COLOR_PACK
#undef expand_t

/* Log stripped */
