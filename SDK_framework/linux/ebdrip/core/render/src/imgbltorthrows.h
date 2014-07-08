/** \file
 * \ingroup rendering
 *
 * $HopeName$
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the definition of the image loop driver function for
 * orthogonal images, when iterating over image scanlines corresponding to
 * device space rows. This image loop driver does not use image coordinate
 * grids.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro FUNCTION expands to the function name to be defined.
 *
 * The macro EXPAND_FN(params) expands to the expander function to call.
 *
 * The macro ROW_FN(params) expands to the row fill function to call.
 *
 * If the macro NOT_HALFTONED is defined, halftone locking will not be
 * attempted.
 *
 * The function parameters are:
 *
 *   rb               - The render_blit_t state pointer.
 *   params           - The collected image blit parameters
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

/* Callback function to implement orthogonal image and mask rendering. */
static Bool FUNCTION(render_blit_t *rb, imgblt_params_t *params)
{
  dcoord yprev, y1clip, y2clipp1 ;
  int32 expand_adjust ;

  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  /* Start Y coordinate of row may be top or bottom, depending on yperhw
     direction. */
  y1clip = rb->p_ri->clip.y1 ;
  y2clipp1 = rb->p_ri->clip.y2 + 1 ;
  INLINE_RANGE32(yprev, params->ys.i, y1clip, y2clipp1) ;

  HQASSERT(params->nrows > 0, "No image rows to render") ;
  HQASSERT(params->out16 == 0 || params->out16 == 1,
           "16-bit output flag is neither 0 nor 1") ;
  expand_adjust = params->dcol < 0
    ? params->converted_comps * (params->out16 + 1) * (params->ncols - 1)
    : 0 ;

  do {
    dcoord y1, y2, nrows;
    const void *values ;
#ifndef NOT_HALFTONED
    Bool relock ;
#endif

    if ( !interrupts_clear(allow_interrupt) )
      return report_interrupt(allow_interrupt);

    IMAGE_DDA_STEP_1(params->ys, params->yperhw, params->basis, &params->ys) ;

#ifndef NOT_HALFTONED
    relock = FALSE ;
    if ( params->ht_params != NULL )
      UNLOCK_HALFTONE_IF_WANTED(params->ht_params, relock);
#endif

    values = EXPAND_FN(params)(params->image->ime, params->image->ims,
                               params->expand_arg,
                               params->lcol, params->irow, params->ncols,
                               &nrows, params->expanded_to_plane,
                               params->expanded_comps) ;
    if ( nrows > 1 ) {
      if ( nrows > params->nrows )
        nrows = params->nrows ;
      IMAGE_DDA_STEP_N_LG2(params->ys, params->yperhw, params->basis, nrows-1);
      params->irow += (nrows - 1)*params->drow;
      params->nrows -= (nrows - 1);
    }

    /* Oversampled images may not change device coordinate every row. Combine
       the oversample check with setting the min/max row boundaries. */
    if ( params->ys.i > yprev ) {
      y1 = yprev ;
      INLINE_MIN32(y2, y2clipp1, params->ys.i) ;
    } else if ( params->ys.i < yprev ) {
      INLINE_MAX32(y1, y1clip, params->ys.i) ;
      y2 = yprev ;
    } else {
      goto more ;
    }

#ifndef NOT_HALFTONED
    if ( relock )
      LOCK_HALFTONE(params->ht_params);
#endif

    if ( values == NULL )
      return FALSE ;

    /* If x flipping, set up at the end of the data. expand_adjust is set
       to 0 if reading the data forwards, the number of bytes in a scanline
       if reading the data backwards. */
    values = (uint8 *)values + expand_adjust ;

    ROW_FN(params)(rb, params, values, y1, y2 - 1) ;

    yprev = params->ys.i ;

  more:
    params->irow += params->drow ;
  } while ( --params->nrows ) ;

  return TRUE ;
}

#undef FUNCTION
#undef EXPAND_FN
#undef ROW_FN
#undef NOT_HALFTONED

/* $Log$
*/
