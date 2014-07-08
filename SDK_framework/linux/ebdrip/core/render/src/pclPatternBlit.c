/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:pclPatternBlit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL simple pattern (i.e. patterns which only implement transparency) blitters.
 */
#include "core.h"
#include "pclPatternBlit.h"

#include "pclAttribTypes.h"
#include "pclGstate.h"
#include "pclAttrib.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "render.h"
#include "toneblt.h"
#include "display.h"
#include "blitcolors.h"
#include "blitcolorh.h"

/** Simplified blit color unpacker for PCL pattern. All channels should be
    the same as the object, the object type and rendering intent doesn't
    change, the colors all have the same number of channels. */
static inline void blit_color_pcl_pattern(blit_color_t *color,
                                          p_ncolor_t ncolor)
{
  const blit_colormap_t *map ;
  bitvector_iterator_t iterator ;
  COLORVALUE dcv ;
  dl_color_t dlc ;

  dlc_from_dl_weak(ncolor, &dlc) ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  /* Extract DL color for constant colors, or set colorvalue out of band so
     it can be used to extract channel colors. */
  switch ( dlc_check_black_white(&dlc) ) {
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

  for ( BITVECTOR_ITERATE_BITS(iterator, map->nchannels) ;
        BITVECTOR_ITERATE_BITS_MORE(iterator) ;
        BITVECTOR_ITERATE_BITS_NEXT(iterator) ) {
    if ( (map->rendered[iterator.element] & iterator.mask) != 0 ) {
      COLORVALUE cv = dcv ;
      COLORANTINDEX ci = map->channel[iterator.bit].ci ;

      HQASSERT(ci >= 0 || ci == COLORANTINDEX_ALL,
               "Colorant index not valid for color channel") ;

      /* Test if we have a colorant value for this channel. Note that the
         dlc_get_indexed_colorant call checks for either the colorant index,
         or colorant /All existing. */
      if ( cv != COLORVALUE_INVALID /* cv already extracted */ ||
           dlc_get_indexed_colorant(&dlc, ci, &cv) )
        color->unpacked.channel[iterator.bit].cv = cv ;
    }
  }

#ifdef ASSERT_BUILD
  /* We've unpacked a color, but the quantisation and packing are now
     invalid. */
  color->valid = blit_color_unpacked ;
#endif
  blit_color_quantise(color) ;
  blit_color_pack(color) ;
}

static inline uint32 wrap(uint32 coord, uint32 size)
{
  uint32 mask = size - 1 ;
  if ( (mask & size) == 0 ) /* size is a power of two */
    return coord & mask ;
  else
    return coord % size ;
}

/**
 * PCL pattern application blitter. This blitter does not implement the PCL
 * print model; it simply discards any spans which have a white pattern value.
 */
static void bitfillpcl_pattern(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  PclAttrib* attrib = rb->p_ri->lobj->objectstate->pclAttrib;
  PclDLPattern* pattern = attrib->dlPattern;
  PclDLPatternIterator iterator ;
  dcoord w = xe - xs + 1;
  void *blit_data ;
  p_ncolor_t transparent ;
  Bool unpack ;
  const surface_t *surface = rb->p_ri->surface ;

  HQASSERT(surface != NULL, "No output surface") ;
  HQASSERT(pattern->preconverted == PCL_PATTERN_PRECONVERT_DEVICE,
           "PCL DL pattern in not preconverted") ;

  /* We won't set the blit color to the pattern color if it is transparent,
     and has only one color. This allows us to use transparent black and
     white patterns as filters on images, where the blit color is set up
     in the image code. */
  unpack = ((attrib->patternColors != PCL_PATTERN_BLACK_AND_WHITE &&
             attrib->patternColors != PCL_PATTERN_OTHER_AND_WHITE) ||
            !attrib->patternTransparent) ;

  GET_BLIT_DATA(rb->blits, PCL_PATTERN_BLIT_INDEX, blit_data) ;
  transparent = blit_data ;

  pclDLPatternIteratorStart(&iterator, pattern, xs, y, w) ;
  for (;;) {
    if (iterator.color.ncolor != transparent) {
      if ( unpack ) {
        blit_color_pcl_pattern(rb->color, iterator.color.ncolor) ;
        /* Reset the blit slice for the base blit, because it may have
           self-modified to use specific functions for the tone value. */
        SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode,
                       &surface->baseblits[rb->clipmode]) ;
      }

      DO_SPAN(rb, y, xs, xs + iterator.cspan - 1);
    }

    xs += iterator.cspan ;
    w -= iterator.cspan ;
    if ( w <= 0 )
      break ;

    pclDLPatternIteratorNext(&iterator, w) ;
  }
}

static void blkfillpcl_pattern(render_blit_t *rb, dcoord ys, dcoord ye,
                               dcoord xs, dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  PclAttrib* attrib = rb_copy.p_ri->lobj->objectstate->pclAttrib;
  PclDLPattern* pattern = attrib->dlPattern;
  PclDLPatternIterator iterator ;
  void *blit_data ;
  p_ncolor_t transparent ;
  Bool unpack ;
  const surface_t *surface = rb_copy.p_ri->surface ;
  register int32 wupdate = theFormL(*rb_copy.outputform);

  HQASSERT(surface != NULL, "No output surface") ;
  HQASSERT(pattern->preconverted == PCL_PATTERN_PRECONVERT_DEVICE,
           "PCL DL pattern in not preconverted") ;

  /* We won't set the blit color to the pattern color if it is transparent,
     and has only one color. This allows us to use transparent black and
     white patterns as filters on images, where the blit color is set up
     in the image code. */
  unpack = ((attrib->patternColors != PCL_PATTERN_BLACK_AND_WHITE &&
             attrib->patternColors != PCL_PATTERN_OTHER_AND_WHITE) ||
            !attrib->patternTransparent) ;

  GET_BLIT_DATA(rb_copy.blits, PCL_PATTERN_BLIT_INDEX, blit_data) ;
  transparent = blit_data ;

  do {
    dcoord w = xe - xs + 1;
    dcoord x = xs ;

    pclDLPatternIteratorStart(&iterator, pattern, xs, ys, w) ;
    INLINE_MIN32(iterator.nlines, ye - ys + 1, iterator.nlines) ;

    for (;;) {
      if (iterator.color.ncolor != transparent) {
        if ( unpack ) {
          blit_color_pcl_pattern(rb_copy.color, iterator.color.ncolor) ;
          /* Reset the blit slice for the base blit, because it may have
             self-modified to use specific functions for the tone value. */
          SET_BLIT_SLICE(rb_copy.blits, BASE_BLIT_INDEX, rb_copy.clipmode,
                         &surface->baseblits[rb_copy.clipmode]) ;
        }

        DO_BLOCK(&rb_copy, ys, ys + iterator.nlines -  1,
                 x, x + iterator.cspan - 1);
      }

      x += iterator.cspan ;
      w -= iterator.cspan ;
      if ( w <= 0 )
        break ;

      pclDLPatternIteratorNext(&iterator, w) ;
    }

    ys += iterator.nlines ;
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr,
                                     wupdate * iterator.nlines);
  } while ( ys <= ye ) ;
}

/**
 * Blit initialisation.
 */
void init_pcl_pattern_blit(surface_t *surface)
{
  int32 clip;

  /* The pcl pattern blitter delegates clipping to the underlying span
   * function, thus there are no clipped versions of the blitter.
   * Block functions however require a cliped variant to setup the address
   * of the clip mask. */
  for (clip = 0; clip < BLT_CLP_N; clip ++) {
    surface->pclpatternblits[clip].spanfn = bitfillpcl_pattern;
    surface->pclpatternblits[clip].blockfn = blkfillpcl_pattern ;
    surface->pclpatternblits[clip].charfn = charbltn;
    surface->pclpatternblits[clip].imagefn = imagebltn;
  }
  surface->pclpatternblits[BLT_CLP_COMPLEX].blockfn = blkclipspan;
}

/* Log stripped */

