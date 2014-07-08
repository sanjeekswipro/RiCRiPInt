/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:halftoneimg.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone optimised image blitting functions.
 */

#include "core.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "render.h"     /* x_sep_position */
#include "toneblt.h"     /* imagebltn */
#include "halftoneblts.h"
#include "halftoneimg.h"
#include "imgblts.h"
#include "imexpand.h"
#include "imageo.h"
#include "htrender.h"
#include "converge.h"
#include "often.h"
#include "control.h"
#include "interrupts.h"

/****************************************************************************/
/* Pixel extracter inlines for halftone image row optimisations. */

#ifdef ASSERT_BUILD
#define htimage_color_pack(c_) MACRO_START \
  VERIFY_OBJECT((c_), BLIT_COLOR_NAME) ; \
  (c_)->valid |= blit_color_packed ; \
MACRO_END
#else
#define htimage_color_pack(c_) EMPTY_STATEMENT()
#endif

/** \fn pixels_ht_1x8_forward
    Halftone pixel extracter for 1 channel, 8 bits, forward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_ht_1x8_forward
#define EXPAND_BITS 8
#define BLIT_COLOR_PACK htimage_color_pack
#include "imgpixels1f.h"
#endif /* !DOXYGEN_SKIP */

/** \fn pixels_ht_1x8_backward
    Halftone pixel extracter for 1 channel, 8 bits, backward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_ht_1x8_backward
#define EXPAND_BITS 8
#define BLIT_COLOR_PACK htimage_color_pack
#include "imgpixels1b.h"
#endif /* !DOXYGEN_SKIP */

/** \fn pixels_ht_1x16_forward
    Halftone pixel extracter for 1 channel, 16 bits, forward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_ht_1x16_forward
#define EXPAND_BITS 16
#define BLIT_COLOR_PACK htimage_color_pack
#include "imgpixels1f.h"
#endif /* !DOXYGEN_SKIP */

/** \fn pixels_ht_1x16_backward
    Halftone pixel extracter for 1 channel, 16 bits, backward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_ht_1x16_backward
#define EXPAND_BITS 16
#define BLIT_COLOR_PACK htimage_color_pack
#include "imgpixels1b.h"
#endif /* !DOXYGEN_SKIP */

static im_pixel_run_fn *const pixel_ht_functions[2][2] = {
  { /* 8-bit image */
    pixels_ht_1x8_forward, pixels_ht_1x8_backward
  },
  { /* 16-bit image */
    pixels_ht_1x16_forward, pixels_ht_1x16_backward
  }
} ;

/****************************************************************************/
/* Partially optimised default row functions for halftoned images. */

static inline void blkfillimageh(render_blit_t *rb,
                                 dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  register const blit_color_t *color = rb->color ;
  BLKBLT_FUNCTION *blockfns ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for block") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  if ( color->quantised.state == blit_quantise_mid ) {
    ht_params_t *ht_params = rb->p_ri->ht_params ;
    COLORVALUE colval = color->quantised.qcv[BLIT_SOLE_CHANNEL] ;
    HQASSERT(colval > 0 &&
             colval < color->quantised.htmax[BLIT_SOLE_CHANNEL],
             "Halftone BLT_HALF block called with black or white") ;
    HQASSERT(!HT_PARAMS_DEGENERATE(ht_params), "Using a degenerate screen");
    GET_FORM(colval, ht_params);
    HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
             "Halftone form is not bitmap") ;
  }

  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, blockfns) ;
  blockfns[color->quantised.state](rb, ys, ye, xs, xe) ;
}

/****************************************************************************/

/** \fn row_orth_ht8
    Fast halftone row fill function for 1 channel of 8 bits. */
#ifndef DOXYGEN_SKIP
#define FUNCTION row_orth_ht8
#define PIXEL_FN(p_) pixels_ht_1x8_forward
#define BLOCK_FN blkfillimageh /* Explicit block call */
#include "imgfillorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_ht8_rows
    Specialised image function for orthogonal images using 8-bit halftone,
    calling im_expandread directly and row fills parameterised on the
    halftone type. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_ht8_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) row_orth_ht8
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn col_orth_ht8
    Fast halftone column fill function for 1 channel of 8 bits. */
#ifndef DOXYGEN_SKIP
#define FUNCTION col_orth_ht8
#define PIXEL_FN(p_) pixels_ht_1x8_forward
#define BLOCK_FN blkfillimageh /* Explicit block call */
#include "imgfillorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_ht8_rows
    Specialised image function for orthogonal images using 8-bit halftone,
    calling im_expandread directly and column fills parameterised on the
    halftone type. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_ht8_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) col_orth_ht8
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn row_orth_ht16
    Fast halftone row fill function for 1 channel of 16 bits. */
#ifndef DOXYGEN_SKIP
#define FUNCTION row_orth_ht16
#define PIXEL_FN(p_) pixels_ht_1x16_forward
#define BLOCK_FN blkfillimageh /* Explicit block call */
#include "imgfillorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_ht16_rows
    Specialised image function for orthogonal images using 16-bit halftone,
    calling im_expandread directly and row fills parameterised on the
    halftone type. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_ht16_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) row_orth_ht16
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn col_orth_ht16
    Fast halftone column fill function for 1 channel of 16 bits. */
#ifndef DOXYGEN_SKIP
#define FUNCTION col_orth_ht16
#define PIXEL_FN(p_) pixels_ht_1x16_forward
#define BLOCK_FN blkfillimageh /* Explicit block call */
#include "imgfillorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_ht16_rows
    Specialised image function for orthogonal images using 16-bit halftone,
    calling im_expandread directly and column fills parameterised on the
    halftone type. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_ht16_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) col_orth_ht16
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

static imgblt_callback_fn *const image_ht_functions[2][2] = {
  { /* 8-bit image */
    image_orth_ht8_rows, image_orth_ht8_cols
  },
  { /* 16-bit image */
    image_orth_ht16_rows, image_orth_ht16_cols
  },
} ;

/****************************************************************************/

/* Optimised scanline functions for unclipped/rectclipped SLOWGENERAL,
   GENERAL, and ORTHOGONAL halftones. */

/** \fn row_orth_sgo8
    Fast halftone row fill function for 1 channel of 8 bits,
    SLOWGENERAL/GENERAL/ORTHOGONAL halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION row_orth_sgo8
#define PIXEL_FN(p_) pixels_ht_1x8_forward
#include "imgfillhsgorows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sgo8_rows
    Specialised image function for orthogonal images using 8-bit halftone,
    calling im_expandread directly and row fills for
    SLOWGENERAL/GENERAL/ORTHOGONAL halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sgo8_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) row_orth_sgo8
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn col_orth_sgo8
    Fast halftone column fill function for 1 channel of 8 bits,
    SLOWGENERAL/GENERAL/ORTHOGONAL halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION col_orth_sgo8
#define PIXEL_FN(p_) pixels_ht_1x8_forward
#include "imgfillhsgocols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sgo8_cols
    Specialised image function for orthogonal images using 8-bit halftone,
    calling im_expandread directly and column fills for
    SLOWGENERAL/GENERAL/ORTHOGONAL halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sgo8_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) col_orth_sgo8
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn row_orth_sgo16
    Fast halftone row fill function for 1 channel of 16 bits,
    SLOWGENERAL/GENERAL/ORTHOGONAL halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION row_orth_sgo16
#define PIXEL_FN(p_) pixels_ht_1x16_forward
#include "imgfillhsgorows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sgo16_rows
    Specialised image function for orthogonal images using 16-bit halftone,
    calling im_expandread directly and row fills for
    SLOWGENERAL/GENERAL/ORTHOGONAL halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sgo16_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) row_orth_sgo16
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn col_orth_sgo16
    Fast halftone column fill function for 1 channel of 16 bits,
    SLOWGENERAL/GENERAL/ORTHOGONAL halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION col_orth_sgo16
#define PIXEL_FN(p_) pixels_ht_1x16_forward
#include "imgfillhsgocols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sgo16_cols
    Specialised image function for orthogonal images using 16-bit halftone,
    calling im_expandread directly and column fills for
    SLOWGENERAL/GENERAL/ORTHOGONAL halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sgo16_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) col_orth_sgo16
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/* Two-dimensional array of optimised image functions, indexed by 8/16 bit,
   rows/cols, for SLOWGENERAL/GENERAL/ORTHOGONAL halftones. */
static imgblt_callback_fn *const image_sgo_functions[2][2] = {
  { /* 8-bit image */
    image_orth_sgo8_rows, image_orth_sgo8_cols
  },
  { /* 16-bit image */
    image_orth_sgo16_rows, image_orth_sgo16_cols
  }
} ;

/****************************************************************************/

/* Optimised scanline functions for unclipped/rectclipped SPECIAL and
   ONELESSWORD halftones. */

/** \fn row_orth_sl8
    Fast halftone row fill function for 1 channel of 8 bits,
    SPECIAL/ONELESSWORD halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION row_orth_sl8
#define PIXEL_FN(p_) pixels_ht_1x8_forward
#include "imgfillhslrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sl8_rows
    Specialised image function for orthogonal images using 8-bit halftone,
    calling im_expandread directly and row fills for
    SPECIAL/ONELESSWORD halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sl8_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) row_orth_sl8
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn col_orth_sl8
    Fast halftone column fill function for 1 channel of 8 bits,
    SPECIAL/ONELESSWORD halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION col_orth_sl8
#define PIXEL_FN(p_) pixels_ht_1x8_forward
#include "imgfillhslcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sl8_cols
    Specialised image function for orthogonal images using 8-bit halftone,
    calling im_expandread directly and column fills for
    SPECIAL/ONELESSWORD halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sl8_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) col_orth_sl8
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn row_orth_sl16
    Fast halftone row fill function for 1 channel of 16 bits,
    SPECIAL/ONELESSWORD halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION row_orth_sl16
#define PIXEL_FN(p_) pixels_ht_1x16_forward
#include "imgfillhslrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sl16_rows
    Specialised image function for orthogonal images using 16-bit halftone,
    calling im_expandread directly and row fills for
    SPECIAL/ONELESSWORD halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sl16_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) row_orth_sl16
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn col_orth_sl16
    Fast halftone column fill function for 1 channel of 16 bits,
    SPECIAL/ONELESSWORD halftones. */
#ifndef DOXYGEN_SKIP
#define FUNCTION col_orth_sl16
#define PIXEL_FN(p_) pixels_ht_1x16_forward
#include "imgfillhslcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_sl16_cols
    Specialised image function for orthogonal images using 16-bit halftone,
    calling im_expandread directly and column fills for
    SPECIAL/ONELESSWORD halftone types. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_sl16_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) col_orth_sl16
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/* Two-dimensional array of optimised image functions, indexed by 8/16 bit,
   rows/cols, for SPECIAL/ONELESSWORD halftones. */
static imgblt_callback_fn *const image_sl_functions[2][2] = {
  { /* 8-bit image */
    image_orth_sl8_rows, image_orth_sl8_cols
  },
  { /* 16-bit image */
    image_orth_sl16_rows, image_orth_sl16_cols
  }
} ;

/****************************************************************************/
/* Generic image blit with halftone-specific pixel extractor. */
void imagebltht(render_blit_t *rb, imgblt_params_t *params,
                imgblt_callback_fn *callback,
                Bool *result)
{
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  /* Use a slightly optimised pixel extractor function for all halftone
     images. Masks don't use the pixel extractor, but we'll set it
     anyway. */
  params->pixel_fn = pixel_ht_functions[params->out16][params->dcol < 0] ;

  /* Deliberately call imagebltn() rather than DO_IMG(). This function is
     only used for ROP_BLIT_INDEX layers, it relies on being able to handle
     the underlying ROP operation using the DO_BLOCK() function. */
  imagebltn(rb, params, callback, result) ;
}

/****************************************************************************/

#ifdef BLIT_HALFTONE_1 /* Optimisations for 1-bit only */

static void imagebltht1(render_blit_t *rb, imgblt_params_t *params,
                        imgblt_callback_fn *callback,
                        Bool *result)
{
  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  ASSERT_BASE_OR_FORWARD_ONLY(rb, imagefn, &next_imgblt,
                              "Halftone image function called after other blits") ;
  HQASSERT(rb->p_ri->pattern_state == PATTERN_OFF,
           "Halftone image function called when patterning") ;
  HQASSERT(rb->depth_shift == 0,
           "Halftone image function should be 1-bit only") ;

  if ( params->type == IM_BLIT_MASK ) {
    /* Strictly black and white masks can use black/white optimisations. */
    switch ( blit_quantise_state(rb->color) ) {
    case blit_quantise_min:
      imageblt1(rb, params, callback, result) ;
      return ;
    case blit_quantise_max:
      imageblt0(rb, params, callback, result) ;
      return ;
    default:
      break ;
    }
  } else if ( params->type == IM_BLIT_IMAGE ) {
    /* Use a slightly optimised pixel extractor function for all halftone
       images. */
    params->pixel_fn = pixel_ht_functions[params->out16][params->dcol < 0] ;

    if ( /* All of the optimisations here are for orthogonal images. */
        params->orthogonal && params->one_color_channel &&
        /** \todo ajcd 2013-12-20: why not on_the_fly? I copied the
            restriction from tone optimisations. Is it still valid? */
        !params->on_the_fly && !rb->color->map->apply_properties ) {
      HQASSERT(params->ht_params, "Halftone image needs halftone params") ;
      HQASSERT(params->ht_params->type >= SPECIAL &&
               params->ht_params->type <= SLOWGENERAL, "Invalid halftone type") ;

      /* Can we do 1:1 optimisations? */
      if ( (theIOptimize(params->image) & IMAGE_OPTIMISE_1TO1) != 0 &&
           /** \todo ajcd 2013-12-20: see todo above
               !params->on_the_fly && */
           im_expand1bit(rb->color, params->image->ime,
                         params->expanded_to_plane, params->expanded_comps,
                         params->blit_to_expanded) ) {
        imageblt1(rb, params, callback, result) ;
        return ;
      }

      /* Can we use optimised fast halftone blits? dcol test is not strictly
         necessary, it just means we don't need backwards extraction versions
         of the optimised functions. This is fine because the halftone
         surface is marked as SURFACE_ORDER_IMAGEROW rather than
         SURFACE_ORDER_DEVICELR, so we shouldn't get wflipped rows. */
      if ( !HT_PARAMS_DEGENERATE(params->ht_params) && params->dcol > 0 ) {
        int rcindex = (params->geometry->wx == 0) ;
        if ( rb->clipmode != BLT_CLP_COMPLEX ) {
          if ( params->ht_params->type == SLOWGENERAL ||
               params->ht_params->type == GENERAL ||
               params->ht_params->type == ORTHOGONAL ) {
            *result = (*image_sgo_functions[params->out16][rcindex])(rb, params) ;
          } else {
            *result = (*image_sl_functions[params->out16][rcindex])(rb, params);
          }
        } else {
          /* Block functions indexed by quantisation state. */
          BLKBLT_FUNCTION blockfns[8] = {
            invalid_block, invalid_block, invalid_block, invalid_block,
            invalid_block, invalid_block, invalid_block, invalid_block,
          } ;

          /* Complex bitmap clip. We can still do some optimisation using an
             appropriate in-lined row/column function, but we have to pass
             the block functions appropriate for the halftone type through. */
          if ( rb->clipform->type != FORMTYPE_BANDRLEENCODED ) {
            blockfns[blit_quantise_max] = blitslice0[rb->clipmode].blockfn ;
            blockfns[blit_quantise_min] = blitslice1[rb->clipmode].blockfn ;
            blockfns[blit_quantise_mid] = blitsliceh[params->ht_params->type][rb->clipmode].blockfn ;
          } else {
            blockfns[blit_quantise_max] = nbit_blit_slice0[rb->clipmode].blockfn ;
            blockfns[blit_quantise_min] = nbit_blit_slice1[rb->clipmode].blockfn ;
            blockfns[blit_quantise_mid] = nbit_blit_sliceh[params->ht_params->type][rb->clipmode].blockfn ;
          }

          SET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, blockfns) ;
          *result = (*image_ht_functions[params->out16][rcindex])(rb, params) ;
        }
        return ;
      }
    }
  }

  imagebltn(rb, params, callback, result) ;
}

/* ---------------------------------------------------------------------- */

void init_halftone1_image(surface_t *halftone1)
{
  unsigned int i ;

  for ( i = 0 ; i < NHALFTONETYPES ; ++i ) {
    blitsliceh[i][BLT_CLP_NONE].imagefn =
      blitsliceh[i][BLT_CLP_RECT].imagefn =
      blitsliceh[i][BLT_CLP_COMPLEX].imagefn = imagebltht1 ;
  }

  halftone1->baseblits[BLT_CLP_NONE].imagefn =
    halftone1->baseblits[BLT_CLP_RECT].imagefn =
    halftone1->baseblits[BLT_CLP_COMPLEX].imagefn = imagebltht1 ;

  /* No min blits. */
  halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn ;
}
#endif /* BLIT_HALFTONE_1 */

#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
static void imageblthtn(render_blit_t *rb, imgblt_params_t *params,
                        imgblt_callback_fn *callback,
                        Bool *result)
{
  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  ASSERT_BASE_OR_FORWARD_ONLY(rb, imagefn, &next_imgblt,
                              "Halftone image function called after other blits") ;
  HQASSERT(rb->p_ri->pattern_state == PATTERN_OFF,
           "Halftone image function called when patterning") ;

  if ( params->type == IM_BLIT_IMAGE ) {
    /* Use a slightly optimised pixel extractor function for all halftone
       images. */
    params->pixel_fn = pixel_ht_functions[params->out16][params->dcol < 0] ;

    if ( /* All of the optimisations here are for orthogonal images. */
        params->orthogonal && params->one_color_channel &&
        /** \todo ajcd 2013-12-20: why not on_the_fly? I copied the
            restriction from tone optimisations. Is it still valid? */
        !params->on_the_fly && !rb->color->map->apply_properties ) {
      HQASSERT(params->ht_params, "Halftone image needs halftone params") ;
      HQASSERT(params->ht_params->type >= SPECIAL &&
               params->ht_params->type <= SLOWGENERAL, "Invalid halftone type") ;

      /* Can we use optimised fast halftone blits? dcol test is not strictly
         necessary, it just means we don't need backwards extraction versions
         of the optimised functions. This is fine because the halftone
         surface is marked as SURFACE_ORDER_IMAGEROW rather than
         SURFACE_ORDER_DEVICELR, so we shouldn't get wflipped rows. */
      if ( !HT_PARAMS_DEGENERATE(params->ht_params) && params->dcol > 0 ) {
        int rcindex = (params->geometry->wx == 0) ;
        if ( rb->clipmode != BLT_CLP_COMPLEX ) {
          if ( params->ht_params->type == SLOWGENERAL ||
               params->ht_params->type == GENERAL ||
               params->ht_params->type == ORTHOGONAL ) {
            *result = (*image_sgo_functions[params->out16][rcindex])(rb, params) ;
          } else {
            *result = (*image_sl_functions[params->out16][rcindex])(rb, params);
          }
        } else {
          /* Block functions indexed by quantisation state. */
          BLKBLT_FUNCTION blockfns[8] = {
            invalid_block, invalid_block, invalid_block, invalid_block,
            invalid_block, invalid_block, invalid_block, invalid_block,
          } ;

          /* Complex bitmap clip. We can still do some optimisation using an
             appropriate in-lined row/column function, but we have to pass
             the block functions appropriate for the halftone type through. */
          blockfns[blit_quantise_max] = nbit_blit_slice0[rb->clipmode].blockfn ;
          blockfns[blit_quantise_min] = nbit_blit_slice1[rb->clipmode].blockfn ;
          blockfns[blit_quantise_mid] = nbit_blit_sliceh[params->ht_params->type][rb->clipmode].blockfn ;

          SET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, blockfns) ;
          *result = (*image_ht_functions[params->out16][rcindex])(rb, params) ;
        }
        return ;
      }
    }
  }

  imagebltn(rb, params, callback, result) ;
}

void init_halftonen_image(surface_t *halftonen)
{
  unsigned int i ;

  for ( i = 0 ; i < NHALFTONETYPES ; ++i ) {
    nbit_blit_sliceh[i][BLT_CLP_NONE].imagefn =
      nbit_blit_sliceh[i][BLT_CLP_RECT].imagefn =
      nbit_blit_sliceh[i][BLT_CLP_COMPLEX].imagefn = imageblthtn;
  }

  halftonen->baseblits[BLT_CLP_NONE].imagefn =
    halftonen->baseblits[BLT_CLP_RECT].imagefn =
    halftonen->baseblits[BLT_CLP_COMPLEX].imagefn = imageblthtn;

  /* No min blits */
  halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn;
}
#endif

/* Log stripped */
