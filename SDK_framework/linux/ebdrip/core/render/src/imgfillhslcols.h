/** \file
 * \ingroup rendering
 *
 * $HopeName$
 *
 * Copyright (C) 2013, 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief This file contains the definition of the optimised scanline fill
 * functions for orthogonal halftoned images with halftone type SPECIAL or
 * ONELESSWORD. This function is used for gridless images when iterating
 * scanlines over device space columns.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro FUNCTION expands to the function name to be defined.
 *
 * The macro PIXEL_FN(params) expands to the pixel extracter function to call.
 *
 * The function parameters are:
 *
 *   rb               - The render_blit_t state pointer.
 *   params           - The collected image blit parameters.
 *   expanded         - A pointer to the expanded image data.
 *   xs               - Left X coordinate of the column.
 *   xe               - Right X coordinate of the column.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

static imageinline void FUNCTION(render_blit_t *rb,
                                 const imgblt_params_t *params,
                                 const void *expanded,
                                 dcoord xs, dcoord xe)
{
  dcoord yprev, y1clip, y2clipp1, x1, x2 ;
  image_dda_t u ;
  blit_color_t *color = rb->color ;
  dcoord x_sep_position = rb->x_sep_position ;
  uint32 depth_shift = rb->depth_shift ;
  int32 bypos = 0, blshift = 0, wupdate;
  ht_params_t *ht_params = params->ht_params ;
  int32 halfxdims = ht_params->xdims;
  int32 halfydims = ht_params->ydims;
  int32 rotate = ht_params->rotate;
  int32 halfpx = ht_params->px, halfpy = ht_params->py;
  const int *blit_to_expanded = params->blit_to_expanded ;
  int32 rw = params->ncols ;
  blit_t firstmask, lastmask ;
  int32 xcount ;

  HQASSERT(color->valid & blit_color_quantised,
           "Blit color is not quantised") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT(color->state[BLIT_SOLE_CHANNEL] & blit_channel_present,
           "Sole color should have been overprinted") ;
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "image halftone fast fill called with a degenerate screen");
  CHECK_HALFTONE_LOCK(ht_params);

  /* Since we don't have an underlying blit, add the X separation position
     into the X coordinates now. */
  u = params->ys ;
  y1clip = rb->p_ri->clip.y1 ;
  y2clipp1 = rb->p_ri->clip.y2 + 1 ;
  INLINE_RANGE32(yprev, u.i, y1clip, y2clipp1) ;

  /* Convert x1,x2 to position in bits. */
  x1 = ((xs + x_sep_position) << depth_shift);
  x2 = ((xe + x_sep_position) << depth_shift);

  firstmask = SHIFTRIGHT(ALLONES , x1 & BLIT_MASK_BITS);
  lastmask = SHIFTLEFT(ALLONES, BLIT_WIDTH_BITS - (1 << depth_shift)
                                - (x2 & BLIT_MASK_BITS));

  /* Convert to position of start of word, expressed in bits. */
  x1 &= ~BLIT_MASK_BITS ;
  x2 &= ~BLIT_MASK_BITS ;

  xcount = (x2 - x1) >> BLIT_SHIFT_BITS ; /* Number of words. */
  if ( xcount == 0 )
    firstmask &= lastmask;    /* And together firstmask & lastmask. */

  wupdate = theFormL(*rb->outputform);

  do {
    int32 npixels = rw ;

    PIXEL_FN(params)(color, &expanded, &npixels, 1, blit_to_expanded) ;
    HQASSERT(npixels > 0 && npixels <= rw, "Wrong number of pixels expanded") ;

    /* Fill as big a block as we can. */
    rw -= npixels ;

    IMAGE_DDA_STEP_N_LG2(u, params->yperhw, params->basis, npixels) ;

    if ( u.i != yprev ) {
      dcoord y1, y2 ;
      blit_t *formptr;
      blit_t *halfform_addr, mask ;

      /* We may be iterating forward or backward in device space, so we need
         to re-sort the Y coordinates. Even though we know whether we're
         going forward or backward, the inline minmax operation is almost
         optimal for it. */
      INLINE_MINMAX32(y1, y2, yprev, u.i) ;

      /* We may have stepped over the end of the clip boundary. */
      INLINE_MAX32(y1, y1clip, y1) ;
      INLINE_MIN32(y2, y2clipp1, y2) ;

      rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                                   (y1 - theFormHOff(*rb->outputform) - rb->y_sep_position) * wupdate) ;

      switch ( color->quantised.state ) {
      case blit_quantise_min:
        blkfill1(rb, y1, y2 - 1, xs, xe);
        goto CONTINUE ;
      case blit_quantise_max:
        blkfill0(rb, y1, y2 - 1, xs, xe);
        goto CONTINUE ;
      default:
        GET_FORM(color->quantised.qcv[BLIT_SOLE_CHANNEL], ht_params);
        halfform_addr = theFormA(*ht_params->form);
        break ;
      }

      formptr = &rb->ylineaddr[x1 >> BLIT_SHIFT_BITS] ;

      if ( ht_params->type == ONELESSWORD ) {
        bypos = (y1 + halfpy) % halfydims;
        blshift = ((x1 & ~BLIT_MASK_BITS) + halfpx) % halfxdims;
      }

      if ( xcount == 0 ) { /* span is within one word */
        dcoord dy = y2 - y1;

        if ( ht_params->type == SPECIAL ) {
          int32 yi = (y1 + halfpy) & BLIT_MASK_BITS;

          do {
            mask = halfform_addr[yi];
            shiftpwordall(mask, halfpx);
            *formptr = (~firstmask & *formptr) | (firstmask & mask);
            formptr = BLIT_ADDRESS(formptr, wupdate);
            yi = (yi + 1) & BLIT_MASK_BITS;
          } while ( --dy > 0 );
        } else { /* ONELESSWORD */
          blit_t *halfptr = &halfform_addr[bypos];

          bypos = halfydims - bypos;
          if ( dy <= bypos ) { /* not wrapping round vertically */
            do {
              mask = *halfptr++;
              shiftpword(mask, blshift, halfxdims);
              *formptr = (~firstmask & *formptr) | (firstmask & mask);
              formptr = BLIT_ADDRESS(formptr, wupdate);
            } while ( --dy > 0 );
          } else {
            do {
              mask = *halfptr++;
              shiftpword(mask, blshift, halfxdims);
              *formptr = (~firstmask & *formptr) | (firstmask & mask);
              formptr = BLIT_ADDRESS(formptr, wupdate);
              if ( --bypos == 0 ) {
                halfptr = halfform_addr; bypos = halfydims;
              }
            } while ( --dy > 0 );
          }
        }

      } else if ( xcount == 1) { /* span is within two words */

        dcoord dy = y2 - y1;

        if ( ht_params->type == SPECIAL ) {
          int32 yi = (y1 + halfpy) & BLIT_MASK_BITS;

          do {
            mask = halfform_addr[yi];
            shiftpwordall(mask, halfpx);
            formptr[0] = (~firstmask & formptr[0]) | (firstmask & mask);
            formptr[1] = (~lastmask & formptr[1])  | (lastmask & mask);
            formptr = BLIT_ADDRESS(formptr, wupdate);
            yi = (yi + 1) & BLIT_MASK_BITS;
          } while ( --dy > 0 );
        } else { /* ONELESSWORD */
          blit_t *halfptr = &halfform_addr[bypos];

          bypos = halfydims - bypos;
          if ( dy <= bypos ) { /* not wrapping round vertically */
            do {
              mask = *halfptr++;
              shiftpword(mask, blshift, halfxdims);
              formptr[0] = (~firstmask & formptr[0]) | (firstmask & mask);
              rotatepword(mask, rotate, halfxdims);
              formptr[1] = (~lastmask & formptr[1])  | (lastmask & mask);
              formptr = BLIT_ADDRESS(formptr, wupdate);
            } while ( --dy > 0 );
          } else {
            do {
              mask = *halfptr++;
              shiftpword(mask, blshift, halfxdims);
              formptr[0] = (~firstmask & formptr[0]) | (firstmask & mask);
              rotatepword(mask, rotate, halfxdims);
              formptr[1] = (~lastmask & formptr[1])  | (lastmask & mask);
              formptr = BLIT_ADDRESS(formptr, wupdate);
              if ( --bypos == 0 ) {
                halfptr = halfform_addr; bypos = halfydims;
              }
            } while ( --dy > 0 );
          }
        }

      } else { /* span is more than two words */

        dcoord dy = y2 - y1;
        blit_t *bformptr = formptr;

        --xcount;
        if ( ht_params->type == SPECIAL ) {
          int32 yi = (y1 + halfpy) & BLIT_MASK_BITS;

          do {
            mask = halfform_addr[yi];
            shiftpwordall(mask, halfpx);
            yi = (yi + 1) & BLIT_MASK_BITS;
            formptr = bformptr;
            *formptr = (*formptr & ~firstmask) | (firstmask & mask);
            ++formptr;
            BlitSet(formptr, mask, xcount);
            formptr += xcount;
            *formptr = ( *formptr & ~lastmask) | (lastmask & mask);
            bformptr = BLIT_ADDRESS(bformptr, wupdate);
          } while ( --dy > 0 );
        } else { /* ONELESSWORD */
          blit_t *halfptr = &halfform_addr[bypos];
          dcoord wr = xcount & 7, ww = xcount >> 3, n;

          bypos = halfydims - bypos;
          do {
            mask = *halfptr++;
            shiftpword(mask, blshift, halfxdims);
            formptr = bformptr;
            *formptr = (~firstmask & *formptr) | (firstmask & mask);
            ++formptr;
            rotatepword(mask, rotate, halfxdims);
            for ( n = ww ; n != 0 ; n--, formptr += 8 ) {
              formptr[0] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[1] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[2] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[3] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[4] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[5] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[6] = mask; rotatepword(mask, rotate, halfxdims);
              formptr[7] = mask; rotatepword(mask, rotate, halfxdims);
            }
            for ( n = wr ; n != 0 ; n--, formptr++ ) {
              formptr[0] = mask; rotatepword(mask, rotate, halfxdims);
            }
            *formptr = (~lastmask & *formptr) | (lastmask & mask);
            bformptr = BLIT_ADDRESS(bformptr, wupdate);
            if ( --bypos == 0 ) {
              halfptr = halfform_addr; bypos = halfydims;
            }
          } while ( --dy > 0 );
        }
        ++xcount;
      }

    CONTINUE:
      yprev = u.i ;
    }
  } while ( rw != 0 ) ;
}

#undef FUNCTION
#undef PIXEL_FN

/* $Log$
*/
