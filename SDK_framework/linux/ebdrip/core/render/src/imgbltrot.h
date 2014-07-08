/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:imgbltrot.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the definition of the image loop driver function for
 * rotated images.
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

#ifdef DEBUG_BUILD
extern int32 clipped_rotated_lines ;
extern int32 max_clipped_rotated_lines ;
#endif

/* Callback function to implement rotated image and mask rendering. */
static Bool FUNCTION(render_blit_t *rb, imgblt_params_t *params)
{
  int32 expand_factor, nrows;
  int32 wlo, whi, clo, chi ; /* column limits for all rows, and this row. */
  image_dda_t clox, cloy, chix, chiy ; /* Delta from row start to clo, chi */
  image_dda_t stepx, stepy ; /* Step from one row to next. */
  dbbox_t outclip ;      /* Bbox where reference point is fully outside clip. */
  dbbox_t inclip ;       /* Bbox where reference point is fully inside clip. */
  int32 xperwd, yperwd ; /* Direction of xperw, yperw DDAs as +ceil/0/-floor */
#ifdef DEBUG_BUILD
  int32 n_clipped_rotated_lines = 0 ;
#endif

  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  HQASSERT(params->nrows > 0, "No image rows to render") ;
  HQASSERT(params->out16 == 0 || params->out16 == 1,
           "16-bit output flag is neither 0 nor 1") ;
  expand_factor = params->dcol < 0
    ? params->converted_comps * (params->out16 + 1)
    : 0 ;

  /* Low, high limits on column (exclusive upper limit). */
  clo = wlo = params->lcol ;
  chi = whi = wlo + params->ncols - 1 ;

  /* Calculate position of clo, chi as offsets from quantised row start. */
  IMAGE_DDA_INITIALISE_XY(&clox, params->basis, params->xs.i) ;
  IMAGE_DDA_STEP_N(clox, params->xperwh, params->basis, clo, &clox) ;
  IMAGE_DDA_STEP_N(clox, params->xperwh, params->basis, params->ncols - 1, &chix) ;

  IMAGE_DDA_INITIALISE_XY(&cloy, params->basis, params->ys.i) ;
  IMAGE_DDA_STEP_N(cloy, params->yperwh, params->basis, clo, &cloy) ;
  IMAGE_DDA_STEP_N(cloy, params->yperwh, params->basis, params->ncols - 1, &chiy) ;

  /* Get the maximum offsets from the axis reference to the tile edges. */
  {
    dcoord x_to_r, x_to_l ; /* Quantised max step from axis to R/L of tile. */
    dcoord y_to_t, y_to_b ; /* Quantised max step from axis to T/B of tile. */
    const dbbox_t *clip = &rb->p_ri->clip ;

    INLINE_MIN32(x_to_l, 0, IMAGE_DDA_FLOOR(params->xperhw)) ;
    INLINE_MAX32(x_to_r, IMAGE_DDA_CEIL(params->xperhw), 0) ;
    if ( params->xperwh.i < 0 ) {
      xperwd = IMAGE_DDA_FLOOR(params->xperwh) ;
      x_to_l += xperwd ;
    } else {
      xperwd = IMAGE_DDA_CEIL(params->xperwh) ;
      x_to_r += xperwd ;
    }

    INLINE_MIN32(y_to_t, 0, IMAGE_DDA_FLOOR(params->yperhw)) ;
    INLINE_MAX32(y_to_b, IMAGE_DDA_CEIL(params->yperhw), 0) ;
    if ( params->yperwh.i < 0 ) {
      yperwd = IMAGE_DDA_FLOOR(params->yperwh) ;
      y_to_t += yperwd ;
    } else {
      yperwd = IMAGE_DDA_CEIL(params->yperwh) ;
      y_to_b += yperwd ;
    }

    /* Convert offsets to clip edges, so we can test reference point against
       clip limit directly. Since we render without the right and bottom
       edges for tesselation, the x_to_r and y_to_b values are reduced by
       one, they will never be touched (but since we subtract them from the
       clip limits, we're adding 1 instead of subtracting (x_to_r - 1)). */
    outclip.x1 = clip->x1 - x_to_r + 1 ;
    outclip.x2 = clip->x2 - x_to_l ;
    outclip.y1 = clip->y1 - y_to_b + 1 ;
    outclip.y2 = clip->y2 - y_to_t ;

    if ( rb->p_ri->x1maskclip < rb->p_ri->x2maskclip ) {
      inclip.x1 = rb->p_ri->x1maskclip - x_to_l ;
      inclip.x2 = rb->p_ri->x2maskclip - x_to_r + 1 ;
      inclip.y1 = clip->y1 - y_to_t ;
      inclip.y2 = clip->y2 - y_to_b + 1 ;
    } else {
      bbox_clear(&inclip) ;
    }
  }

  /* Get the step from this row to the next one, in the rendered order. */
  if ( params->drow < 0 ) {
    IMAGE_DDA_NEGATE(params->xperhw, params->basis, &stepx) ;
    IMAGE_DDA_NEGATE(params->yperhw, params->basis, &stepy) ;
  } else {
    stepx = params->xperhw ;
    stepy = params->yperhw ;
  }

  do {
    dcoord drx, dry ; /* Difference between row start positions. */
    dcoord pxs, pys ; /* Previous line quantised start position. */
    image_dda_t nextx, nexty ;

    if ( !interrupts_clear(allow_interrupt) )
      return report_interrupt(allow_interrupt);

    /* Get row differential for *this* row. If iterating rows in normal
       order, this will be the same as the step between rows. If iterating
       rows in reverse order, the step to the previous row will be calculated
       later. */
    pxs = params->xs.i ; pys = params->ys.i ;
    IMAGE_DDA_STEP_1(params->xs, params->xperhw, params->basis, &nextx) ;
    IMAGE_DDA_STEP_1(params->ys, params->yperhw, params->basis, &nexty) ;
    drx = nextx.i - pxs ; dry = nexty.i - pys ;

    /* Oversampled images may not change device coordinate every row. */
    if ( drx | dry ) {
      const void *values ;
      Bool ready ;
#ifndef NOT_HALFTONED
      Bool relock ;
#endif

      /* Trim/expand line to clip limits. */

      /* Adjust clo to the lowest index that is inside the outclip bbox,
         updating clox and cloy DDAs to the device coordinate of the first
         tile reference point. If the entire row is clipped out (because we
         didn't shrink the image space bbox enough), there will be no
         clo <= whi inside outclip, and we can skip the whole row.

         The clip bbox to image bbox conversion sometimes widens the image
         space bounds a little bit. The problem is because the device space
         point may be outside of the image (however, it is clipped to the
         image extent), and when converted to image space such points project
         to an image space point wider than is strictly necessary. It is
         not possible to get exact bounds without exactly duplicating the
         rounding compromises made during the rendering, so the routine
         image_ibbox_covering_dbbox() is conservative about how much it can
         shrink the image space bounding box. */
      for (ready = FALSE ;; ) {
        dcoord xdiff, ydiff ;

        if ( xperwd > 0 )
          xdiff = clox.i - outclip.x1 ;
        else if ( xperwd < 0 )
          xdiff = clox.i - outclip.x2 ;
        else
          xdiff = 0 ;

        if ( yperwd > 0 )
          ydiff = cloy.i - outclip.y1 ;
        else if ( yperwd < 0 )
          ydiff = cloy.i - outclip.y2 ;
        else
          ydiff = 0 ;

        /* If clo is after both outclip start boundaries, can we decrease it
           to move just before the boundary? */
        if ( (xperwd > 0 ? xdiff >= 0 : xperwd < 0 ? xdiff <= 0 : TRUE) &&
             (yperwd > 0 ? ydiff >= 0 : yperwd < 0 ? ydiff <= 0 : TRUE) ) {
          int32 steps = clo - wlo ;

          if ( steps == 0 || ready ) { /* Can't decrease clo any more. */
            /* If clo is after the end boundary, then the entire row is
               clipped out. */
            if ( (clox.i > outclip.x2 && xperwd >= 0) ||
                 (clox.i < outclip.x1 && xperwd <= 0) ||
                 (cloy.i > outclip.y2 && yperwd >= 0) ||
                 (cloy.i < outclip.y1 && yperwd <= 0) ) {
#ifdef DEBUG_BUILD
              ++clipped_rotated_lines ;
              if ( ++n_clipped_rotated_lines > max_clipped_rotated_lines )
                max_clipped_rotated_lines = n_clipped_rotated_lines ;
#endif
              goto row_done ;
            }
            break ;
          }

          if ( xperwd != 0 )
            INLINE_MIN32(steps, steps, xdiff / xperwd) ;

          if ( yperwd != 0 )
            INLINE_MIN32(steps, steps, ydiff / yperwd) ;

          INLINE_MAX32(steps, steps, 1) ;

          IMAGE_DDA_STEP_N_LG2(clox, params->nxperw, params->basis, steps) ;
          IMAGE_DDA_STEP_N_LG2(cloy, params->nyperw, params->basis, steps) ;
          clo -= steps ;
        } else { /* clo is before an outclip start boundary. */
          int32 steps = whi - clo ;

          if ( steps == 0 ) { /* Can't increase clo any more */
#ifdef DEBUG_BUILD
            ++clipped_rotated_lines ;
            if ( ++n_clipped_rotated_lines > max_clipped_rotated_lines )
              max_clipped_rotated_lines = n_clipped_rotated_lines ;
#endif
            goto row_done ;
          }

          if ( xperwd > 0 ? xdiff <= 0 : xperwd < 0 ? xdiff >= 0 : FALSE )
            INLINE_MIN32(steps, steps, -xdiff / xperwd) ;

          if ( yperwd > 0 ? ydiff <= 0 : yperwd < 0 ? ydiff >= 0 : FALSE )
            INLINE_MIN32(steps, steps, -ydiff / yperwd) ;

          if ( steps <= 1 ) {
            steps = 1 ;
            ready = TRUE ;
          }

          IMAGE_DDA_STEP_N_LG2(clox, params->xperwh, params->basis, steps) ;
          IMAGE_DDA_STEP_N_LG2(cloy, params->yperwh, params->basis, steps) ;
          clo += steps ;
        }
      }

      /* Adjust chi to the highest index that is inside the outclip bbox,
         updating chix and chiy DDAs to the device coordinate of the last
         tile reference point. If the entire row is clipped out (because we
         didn't shrink the image space bbox enough), there will be no
         chi >= clo inside outclip, and we can skip the whole row. */
      for (ready = FALSE ;; ) {
        dcoord xdiff, ydiff ;

        if ( xperwd > 0 )
          xdiff = outclip.x2 - chix.i ;
        else if ( xperwd < 0 )
          xdiff = outclip.x1 - chix.i ;
        else
          xdiff = 0 ;

        if ( yperwd > 0 )
          ydiff = outclip.y2 - chiy.i ;
        else if ( yperwd < 0 )
          ydiff = outclip.y1 - chiy.i ;
        else
          ydiff = 0 ;

        /* If chi is before both outclip end boundaries, can we increase it
           to move just after the boundary? */
        if ( (xperwd > 0 ? xdiff >= 0 : xperwd < 0 ? xdiff <= 0 : TRUE) &&
             (yperwd > 0 ? ydiff >= 0 : yperwd < 0 ? ydiff <= 0 : TRUE) ) {
          int32 steps = whi - chi ;

          if ( steps == 0 || ready ) {
            /* Can't increase chi any more. Assert that chi is after the
               start boundary, the clo adjustments should have ensured that
               the entire row is not clipped out. */
            HQASSERT((chix.i >= outclip.x1 && xperwd >= 0) ||
                     (chix.i <= outclip.x2 && xperwd <= 0) ||
                     (chiy.i >= outclip.y1 && yperwd >= 0) ||
                     (chiy.i <= outclip.y2 && yperwd <= 0),
                     "Clipped out row should have been detected sooner") ;
            break ;
          }

          if ( xperwd != 0 )
            INLINE_MIN32(steps, steps, xdiff / xperwd) ;

          if ( yperwd != 0 )
            INLINE_MIN32(steps, steps, ydiff / yperwd) ;

          INLINE_MAX32(steps, steps, 1) ;

          IMAGE_DDA_STEP_N_LG2(chix, params->xperwh, params->basis, steps) ;
          IMAGE_DDA_STEP_N_LG2(chiy, params->yperwh, params->basis, steps) ;
          chi += steps ;
        } else { /* chi is after an outclip end boundary */
          int32 steps = chi - clo ;
          HQASSERT(steps > 0,
                   "Clipped out row should have been detected sooner") ;

          if ( xperwd > 0 ? xdiff <= 0 : xperwd < 0 ? xdiff >= 0 : FALSE )
            INLINE_MIN32(steps, steps, -xdiff / xperwd) ;

          if ( yperwd > 0 ? ydiff <= 0 : yperwd < 0 ? ydiff >= 0 : FALSE )
            INLINE_MIN32(steps, steps, -ydiff / yperwd) ;

          if ( steps <= 1 ) {
            steps = 1 ;
            ready = TRUE ;
          }

          IMAGE_DDA_STEP_N_LG2(chix, params->nxperw, params->basis, steps) ;
          IMAGE_DDA_STEP_N_LG2(chiy, params->nyperw, params->basis, steps) ;
          chi -= steps ;
        }
      }

#ifdef DEBUG_BUILD
      n_clipped_rotated_lines = 0 ;
#endif

#ifndef NOT_HALFTONED
      relock = FALSE ;
      if ( params->ht_params != NULL )
        UNLOCK_HALFTONE_IF_WANTED(params->ht_params, relock);
#endif

      values = EXPAND_FN(params)(params->image->ime, params->image->ims,
                                 params->expand_arg,
                                 clo, params->irow, chi - clo + 1, &nrows,
                                 params->expanded_to_plane,
                                 params->expanded_comps) ;

#ifndef NOT_HALFTONED
      if ( relock )
        LOCK_HALFTONE(params->ht_params);
#endif

      if ( values == NULL )
        return FALSE ;

      /* If x flipping, set up at the end of the data. expand_factor is set
         to 0 if reading the data forwards, the number of bytes in a sample
         if reading the data backwards. Note that this points to the last
         sample, not past it. */
      values = (uint8 *)values + expand_factor * (chi - clo) ;

      ROW_FN(params)(rb, params, values, chi - clo + 1, drx, dry,
                     params->dcol < 0 ? chix : clox,
                     params->dcol < 0 ? chiy : cloy,
                     &inclip) ;

    row_done:
      EMPTY_STATEMENT() ;
    } /* else line is degenerate */

    if ( params->drow < 0 ) {
      /* Step reference point to previous row, and re-calculate shift for
         column offsets. */
      IMAGE_DDA_STEP_1(params->xs, stepx, params->basis, &params->xs) ;
      IMAGE_DDA_STEP_1(params->ys, stepy, params->basis, &params->ys) ;
      drx = params->xs.i - pxs ; dry = params->ys.i - pys ;
    } else {
      /* Use next row start already calculated. */
      params->xs = nextx ; params->ys = nexty ;
    }

    clox.i += drx ; chix.i += drx ;
    cloy.i += dry ; chiy.i += dry ;

    params->irow += params->drow ;
  } while ( --params->nrows ) ;

  return TRUE ;
}

#undef FUNCTION
#undef EXPAND_FN
#undef ROW_FN
#undef NOT_HALFTONED

/* Log stripped */
