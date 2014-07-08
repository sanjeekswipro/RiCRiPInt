/** \file
 * \ingroup render
 *
 * $HopeName$
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for rendering image masks into character device forms.
 */

#include "core.h"
#include "render.h"
#include "images.h" /* IMAGEDATA */
#include "imagedda.h"
#include "bitblts.h"
#include "bitblth.h"
#include "tables.h"
#include "ndisplay.h" /* SC_C2D_INT */

/** Determine whether to use DDAs or floating-point for device to source
    transforms. */
#define CHARS_USE_DDA 1

/**
 * Helper method for char_render_image(); return true if the indexed pixel
 * in 'data' is black.
 */
static inline Bool source_black(const uint8 *data, uint8 invert, int32 stride,
                                const ibbox_t *imsbbox, int32 x, int32 y)
{
  if ( x < imsbbox->x1 || x > imsbbox->x2 ||
       y < imsbbox->y1 || y > imsbbox->y2 )
    return FALSE ;

  return (((data[stride * y + (x >> 3)] ^ invert) & (128u >> (x & 7))) == 0) ;
}

static inline void copy_shift_forward(render_blit_t *rb,
                                      const uint8 *data, uint8 invert,
                                      dcoord x, dcoord w)
{
  blit_t *ylineaddr = rb->ylineaddr + (x >> BLIT_SHIFT_BITS) ;
  blit_t *ymaskaddr = rb->ymaskaddr + (x >> BLIT_SHIFT_BITS) ;
  blit_t bits = 0 ;
  int32 nbits = x & BLIT_MASK_BITS ; /* High bits used */

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipmode != BLT_CLP_COMPLEX ||
           rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(w > 0, "Image width too small") ;

#ifndef bitsgoright
  /** \todo ajcd 2014-04-15: This code assumes that the high bit position is
      leftmost. */
#  error bitsgoleft not implemented
#endif

  invert ^= 0xff ; /* Set bits that are 0. */
  do {
    uint8 byte = *data++ ^ invert ;
    int32 nbyte = min(w, 8) ;

    /* Shift data so high bit is in highest unused bit position. */
    bits |= ((blit_t)byte << (BLIT_WIDTH_BITS - 8)) >> nbits ;

    nbits += nbyte ;
    if ( nbits >= BLIT_WIDTH_BITS ) {
      if ( rb->clipmode == BLT_CLP_COMPLEX ) {
        HQASSERT(ymaskaddr >= theFormA(*rb->clipform) &&
                 ymaskaddr + 1 < BLIT_ADDRESS(theFormA(*rb->clipform),
                                              theFormH(*rb->clipform) * theFormL(*rb->clipform)),
                 "Out of range reading clip form") ;
        bits &= *ymaskaddr++ ;
      }
      HQASSERT(ylineaddr >= theFormA(*rb->outputform) &&
               ylineaddr + 1 < BLIT_ADDRESS(theFormA(*rb->outputform),
                                            theFormH(*rb->outputform) * theFormL(*rb->outputform)),
                 "Out of range writing character form") ;
      *ylineaddr++ |= bits ;
      nbits -= BLIT_WIDTH_BITS ;
      /* Construct the overflow blit word in two stages, so we take care of
         the case when there are no bits. First, shift the remaining bits to
         the top of the low byte, then shift the low byte to the top of the
         blit word. */
      bits = (blit_t)byte << (nbyte - nbits) ;
      bits <<= BLIT_WIDTH_BITS - 8 ;
    }

    w -= 8 ;
  } while ( w > 0 ) ;

  if ( nbits > 0 ) {
    bits &= ~(ALLONES >> nbits) ;
    if ( rb->clipmode == BLT_CLP_COMPLEX ) {
      HQASSERT(ymaskaddr >= theFormA(*rb->clipform) &&
               ymaskaddr + 1 < BLIT_ADDRESS(theFormA(*rb->clipform),
                                            theFormH(*rb->clipform) * theFormL(*rb->clipform)),
               "Out of range reading clip form") ;
      bits &= *ymaskaddr ;
    }
    HQASSERT(ylineaddr >= theFormA(*rb->outputform) &&
             ylineaddr + 1 < BLIT_ADDRESS(theFormA(*rb->outputform),
                                          theFormH(*rb->outputform) * theFormL(*rb->outputform)),
             "Out of range writing character form") ;
    *ylineaddr |= bits ;
  }
}

static inline void copy_shift_backward(render_blit_t *rb,
                                       const uint8 *data, uint8 invert,
                                       dcoord x, dcoord w)
{
  blit_t *ylineaddr = rb->ylineaddr + (x >> BLIT_SHIFT_BITS) ;
  blit_t *ymaskaddr = rb->ymaskaddr + (x >> BLIT_SHIFT_BITS) ;
  blit_t bits = 0 ;
  int32 nbits = (BLIT_MASK_BITS - x) & BLIT_MASK_BITS ; /* Low bits used */

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipmode != BLT_CLP_COMPLEX ||
           rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(w > 0, "Image width too small") ;

#ifndef bitsgoright
  /** \todo ajcd 2014-04-15: This code assumes that the high bit position is
      leftmost. */
#  error bitsgoleft not implemented
#endif

  invert ^= 0xff ; /* Set bits that are 0. */
  do {
    uint8 byte = reversed_bits_in_byte[*data++ ^ invert] ;
    int32 nbyte = min(w, 8) ;

    /* Shift data so low bit is in lowest unused bit position. */
    bits |= (blit_t)(byte >> (8 - nbyte)) << nbits ;

    nbits += nbyte ;
    if ( nbits >= BLIT_WIDTH_BITS ) {
      if ( rb->clipmode == BLT_CLP_COMPLEX ) {
        HQASSERT(ymaskaddr >= theFormA(*rb->clipform) &&
                 ymaskaddr + 1 < BLIT_ADDRESS(theFormA(*rb->clipform),
                                              theFormH(*rb->clipform) * theFormL(*rb->clipform)),
                 "Out of range reading clip form") ;
        bits &= *ymaskaddr-- ;
      }
      HQASSERT(ylineaddr >= theFormA(*rb->outputform) &&
               ylineaddr + 1 < BLIT_ADDRESS(theFormA(*rb->outputform),
                                            theFormH(*rb->outputform) * theFormL(*rb->outputform)),
                 "Out of range writing character form") ;
      *ylineaddr-- |= bits ;
      nbits -= BLIT_WIDTH_BITS ;
      /* The overflow blit word contains the high bits of the byte that were
         not used. This expression will be zero if there is no overflow. */
      bits = (blit_t)byte >> (8 - nbits) ;
    }

    w -= 8 ;
  } while ( w > 0 ) ;

  if ( nbits > 0 ) {
    bits &= ~(ALLONES << nbits) ;
    if ( rb->clipmode == BLT_CLP_COMPLEX ) {
      HQASSERT(ymaskaddr >= theFormA(*rb->clipform) &&
               ymaskaddr + 1 < BLIT_ADDRESS(theFormA(*rb->clipform),
                                            theFormH(*rb->clipform) * theFormL(*rb->clipform)),
               "Out of range reading clip form") ;
      bits &= *ymaskaddr ;
    }
    HQASSERT(ylineaddr >= theFormA(*rb->outputform) &&
             ylineaddr + 1 < BLIT_ADDRESS(theFormA(*rb->outputform),
                                          theFormH(*rb->outputform) * theFormL(*rb->outputform)),
             "Out of range writing character form") ;
    *ylineaddr |= bits ;
  }
}

void char_image_render(render_blit_t *rb, IMAGEDATA *imagedata,
                       const ibbox_t *imsbbox, uint8 *data, uint8 invert)
{
  dbbox_t dbbox ;
  dcoord x, y ;
  int32 stride ;
#ifdef CHARS_USE_DDA
  image_dda_basis_t basis ;
  image_dda_t uperx, vperx, upery, vpery ;
#else
  double uperx, vperx, upery, vpery ;
#endif
  const im_transform_t *geometry = &imagedata->geometry ;
  OMATRIX inverse ;
  int one_to_one = 0 ;

  /* imsbbox->dspace, does it intersect clip bounds? */

  bbox_store(&dbbox, 0, 0, rb->outputform->w - 1, rb->outputform->h - 1) ;
  bbox_intersection(&dbbox, &rb->p_ri->clip, &dbbox) ;
  if ( bbox_is_empty(&dbbox) )
    return ;

  if ( !matrix_inverse(&imagedata->opt_matrix, &inverse) )
    return ;

  stride = (geometry->w + 7) >> 3 ;

  /* Set up device space->image space DDAs. The basis is the character cache
     width and height multiplied together. This allows testing at any
     position. */
#ifdef CHARS_USE_DDA
  IMAGE_DDA_BASIS(&basis, rb->outputform->w, rb->outputform->h) ;
  IMAGE_DDA_INITIALISE_R(&uperx, basis, inverse.matrix[0][0]) ;
  IMAGE_DDA_INITIALISE_R(&vperx, basis, inverse.matrix[0][1]) ;
  IMAGE_DDA_INITIALISE_R(&upery, basis, inverse.matrix[1][0]) ;
  IMAGE_DDA_INITIALISE_R(&vpery, basis, inverse.matrix[1][1]) ;
#else
  uperx = inverse.matrix[0][0] ;
  vperx = inverse.matrix[0][1] ;
  upery = inverse.matrix[1][0] ;
  vpery = inverse.matrix[1][1] ;
#endif

  /* The 1:1 test only includes the X component, because we can repeat or skip
     lines as necessary. This 1:1 optimisations are only enabled if the entire
     image row blits inside the raster area. */
  if ( (inverse.opt & MATRIX_OPT_1001) == 0 ) {
    if ( inverse.matrix[0][0] == 1.0 &&
         geometry->tx >= dbbox.x1 &&
         geometry->tx + geometry->w - 1 <= dbbox.x2 )
      one_to_one = 1 ;
    else if ( inverse.matrix[0][0] == -1.0 &&
              geometry->tx <= dbbox.x2 &&
              geometry->tx - geometry->w + 1 >= dbbox.x1 )
      one_to_one = -1 ;
  }

  /** \todo ajcd 2014-04-14: We could do the transform in a span function, so
      we can draw outline of parallelogram for a rotated image and save the
      bbox testing for points outside the drawn area. */
  for ( y = dbbox.y1 ; y <= dbbox.y2 ; ++y ) {
    Bool bw ;
#ifdef CHARS_USE_DDA
    image_dda_t u = { 0 }, v = { 0 } ;

    x = dbbox.x1 ;
    HQASSERT(x <= dbbox.x2, "Exceeded raster bounds") ;

    IMAGE_DDA_STEP_NN(u, uperx, basis, x - geometry->tx, &u) ;
    IMAGE_DDA_STEP_NN(v, vperx, basis, x - geometry->tx, &v) ;

    IMAGE_DDA_STEP_NN(u, upery, basis, y - geometry->ty, &u) ;
    IMAGE_DDA_STEP_NN(v, vpery, basis, y - geometry->ty, &v) ;
#else
    struct {
      int32 i ;
    } u, v ;
    double U, V ;

    x = dbbox.x1 ;
    HQASSERT(x <= dbbox.x2, "Exceeded raster bounds") ;

    U = (x - geometry->tx) * uperx + (y - geometry->ty) * upery ;
    SC_C2D_INT(u.i, U) ;
    V = (x - geometry->tx) * vperx + (y - geometry->ty) * vpery ;
    SC_C2D_INT(v.i, V) ;
#endif

    rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                                 y * theFormL(*rb->outputform));
    rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                                 y * theFormL(*rb->clipform));

    /* If the character is orthogonal, test if the entire row can be omitted. */
    if ( (inverse.opt & MATRIX_OPT_1001) == 0 ) {
      if ( v.i < imsbbox->y1 || v.i > imsbbox->y2 )
        continue ;
      if ( one_to_one > 0 ) {
        copy_shift_forward(rb, data + stride * v.i, invert, geometry->tx, geometry->w) ;
        continue ;
      }
      if ( one_to_one < 0 ) {
        copy_shift_backward(rb, data + stride * v.i, invert, geometry->tx, geometry->w) ;
        continue ;
      }
    }
    if ( (inverse.opt & MATRIX_OPT_0011) == 0 ) {
      if ( u.i < imsbbox->x1 || u.i > imsbbox->x2 )
        continue ;
    }

    bw = source_black(data, invert, stride, imsbbox, u.i, v.i) ;
    do {
      dcoord xs = x ;
      int32 ui = u.i, vi = v.i ;

      /* Loop until we find the end of the row, or a change of color. */
      while ( ++x <= dbbox.x2 ) {
#ifdef CHARS_USE_DDA
        IMAGE_DDA_STEP_1(u, uperx, basis, &u) ;
        IMAGE_DDA_STEP_1(v, vperx, basis, &v) ;
#else
        U += uperx ; SC_C2D_INT(u.i, U) ;
        V += vperx ; SC_C2D_INT(v.i, V) ;
#endif
        if ( u.i != ui || v.i != vi ) {
          if ( bw != source_black(data, invert, stride, imsbbox, u.i, v.i) )
            break ;
          ui = u.i ; vi = v.i ;
        }
      }

      /* Found a reason to end the span. Draw it if it was black. */
      if ( bw ) {
        DO_SPAN(rb, y, xs, x - 1) ;
      }
      bw = !bw ;
    } while ( x <= dbbox.x2 ) ;
  }
}

/* $Log$
 */
