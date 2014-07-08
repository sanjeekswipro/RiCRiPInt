/** \file
 * \ingroup rendering
 *
 * $HopeName$
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the definition of the optimised scanline fill functions
 * for orthogonal images, when iterating over image scanlines corresponding to
 * device space rows. This image loop driver does not use image coordinate
 * grids.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro FUNCTION expands to the function name to be defined.
 *
 * The macro PIXEL_FN(params) expands to the pixel extracter function to call.
 *
 * The macro BLOCK_FN is optional. If defined, it is a function with prototype
 * BLKBLT_FUNCTION, which will be called for each pixel in the image. If not
 * defined, the normal blit chain will be called.
 *
 * The function parameters are:
 *
 *   rb               - The render_blit_t state pointer.
 *   params           - The collected image blit parameters.
 *   expanded         - A pointer to the expanded image data.
 *   y1               - Top Y coordinate of the row.
 *   y2               - Bottom Y coordinate of the row.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

static imageinline void FUNCTION(render_blit_t *rb,
                                 const imgblt_params_t *params,
                                 const void *expanded,
                                 dcoord y1, dcoord y2)
{
  dcoord xprev, x1clip, x2clipp1 ;
  int32 rw = params->ncols ;
  image_dda_t u = params->xs ;
#ifndef BLOCK_FN
  const blit_slice_t *slice = &rb->p_ri->surface->baseblits[0] ;
#endif

  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
    (y1 - theFormHOff(*rb->outputform) - rb->y_sep_position) *
     theFormL(*rb->outputform)) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
    (y1 - theFormHOff(*rb->clipform) - rb->y_sep_position) *
     theFormL(*rb->clipform)) ;

  x1clip = rb->p_ri->clip.x1 ;
  x2clipp1 = rb->p_ri->clip.x2 + 1 ;
  INLINE_RANGE32(xprev, u.i, x1clip, x2clipp1) ;

  do {
    int32 npixels = rw ;
    dcoord x1, x2 ;

    /* Use the same expansion pattern to find an optimised pixel extracter */
    PIXEL_FN(params)(rb->color, &expanded, &npixels,
                     params->converted_comps, params->blit_to_expanded) ;
    HQASSERT(npixels > 0 && npixels <= rw, "Wrong number of pixels expanded") ;

    /* Fill as big a block as we can. */
    rw -= npixels ;
    IMAGE_DDA_STEP_N_LG2(u, params->xperwh, params->basis, npixels) ;

    /* We need to test for degenerate pixels anyway, so combine it with a
       directional test to determine whether we may have stepped over the end
       of the clip boundary. */
    if ( u.i > xprev ) {
      x1 = xprev ;
      INLINE_MIN32(x2, x2clipp1, u.i) ;
    } else if ( u.i < xprev ) {
      INLINE_MAX32(x1, x1clip, u.i) ;
      x2 = xprev ;
    } else {
      continue ; /* Degenerate source pixel. */
    }

#ifdef BLOCK_FN
    /* Supplied block function, call it directly. */
    BLOCK_FN(rb, y1, y2, x1, x2 - 1) ;
#else
    /* No defined block function, use the normal blit chain. */
    SET_BLITS_CURRENT(rb->blits, BASE_BLIT_INDEX,
                      &slice[BLT_CLP_NONE],
                      &slice[BLT_CLP_RECT],
                      &slice[BLT_CLP_COMPLEX]) ;
    DO_BLOCK(rb, y1, y2, x1, x2 - 1) ;
#endif

    xprev = u.i ;
  } while ( rw != 0 ) ;
}

#undef FUNCTION
#undef BLOCK_FN
#undef PIXEL_FN

/* $Log$
*/
