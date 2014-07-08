/** \file
 * \ingroup rendering
 *
 * $HopeName$
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief This file contains the definition of the optimised scanline fill
 * functions for orthogonal halftoned images with halftone type SLOWGENERAL,
 * GENERAL or ORTHOGONAL. This function is used for gridless images when
 * iterating scanlines over device space rows.
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
 *   ys               - Top Y coordinate of the row.
 *   ye               - Bottom Y coordinate of the row.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

static imageinline void FUNCTION(render_blit_t *rb,
                                 const imgblt_params_t *params,
                                 const void *expanded,
                                 dcoord ys, dcoord ye)
{
  dcoord xprev, x1clip, x2clipp1 ;
  image_dda_t u ;
  register blit_color_t *color = rb->color ;
  dcoord x_sep_position = rb->x_sep_position ;
  uint32 depth_shift = rb->depth_shift ;
  int32 bxpos1 = 0, bypos1 = 0, wupdate;
  dcoord xemax, xsmin, height;
  int32 halfypos = -1; /* Silence compiler warning */
  int32 lhalfcx = 0, lhalfccy = 0; /* Silence compiler warning */
  ht_params_t *ht_params = params->ht_params ;
  const int32 *halfys = ht_params->ys;
  int32 halfys_1 = halfys[1];
  int32 halfxdims = ht_params->xdims;
  int32 halfexdims = ht_params->exdims;
  int32 halfydims = ht_params->ydims;
  int32 halfr1 = ht_params->r1, halfr2 = ht_params->r2,
    halfr3 = ht_params->r3, halfr4 = ht_params->r4;
  const int *blit_to_expanded = params->blit_to_expanded ;
  int32 rw = params->ncols ;

  HQASSERT(color->valid & blit_color_quantised,
           "Blit color is not quantised") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT(color->state[BLIT_SOLE_CHANNEL] & blit_channel_present,
           "Sole color should have been overprinted") ;
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "image halftone fast fill called with a degenerate screen");
  CHECK_HALFTONE_LOCK(ht_params);

  height = ye - ys + 1;

  /* Since we don't have an underlying blit, add the X separation position
     into the X coordinates now. */
  u = params->xs ; u.i += x_sep_position ;
  x1clip = rb->p_ri->clip.x1 + x_sep_position ;
  x2clipp1 = rb->p_ri->clip.x2 + 1 + x_sep_position ;
  INLINE_RANGE32(xprev, u.i, x1clip, x2clipp1) ;

  /* Threshold for ignoring right-hand mask when blitting two whole words
     into destination, expressed as a bit coordinate. We can only ignore the
     right-hand mask when blitting left to right, otherwise we may overwrite
     previously rendered pixels. */
  if ( params->xperwh.i >= 0 ) {
    xsmin = MAXDCOORD ;
    INLINE_MIN32(xemax,
                 params->image->bbox.x2 + 1 + x_sep_position, x2clipp1) ;

    /* Convert the right hand word mask threshold coordinate to a bit offset,
       round the bit offset down to a word boundary, take off two words. */
    xemax = ((xemax << depth_shift) & ~BLIT_MASK_BITS) - 2 * BLIT_WIDTH_BITS ;
  } else {
    INLINE_MAX32(xsmin,
                 params->image->bbox.x1 + x_sep_position, x1clip) ;
    /* Convert the left hand word mask threshold coordinate to a bit offset,
       round the bit offset up to a word boundary. */
    xsmin = (((xsmin << depth_shift) + BLIT_MASK_BITS) & ~BLIT_MASK_BITS) ;
    xemax = 0 ; /* disable right-hand mask ignore */
  }

  wupdate = theFormL(*rb->outputform);
  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                               (ys - theFormHOff(*rb->outputform) - rb->y_sep_position) * wupdate) ;

  do {
    int32 npixels = rw ;

    PIXEL_FN(params)(color, &expanded, &npixels, 1, blit_to_expanded) ;
    HQASSERT(npixels > 0 && npixels <= rw, "Wrong number of pixels expanded") ;

    /* Fill as big a block as we can. */
    rw -= npixels ;

    IMAGE_DDA_STEP_N_LG2(u, params->xperwh, params->basis, npixels) ;

    if ( u.i != xprev ) {
      dcoord x1, x2, xcount ;
      register blit_t *formptr;
      register blit_t *halfptr1;
      blit_t *halfform_addr, firstmask, lastmask, mask ;
      register int32 blshift1;

      /* We may be iterating forward or backward in device space, so we need
         to re-sort the X coordinates. Even though we know whether we're
         going forward or backward, the inline minmax operation is almost
         optimal for it. */
      INLINE_MINMAX32(x1, x2, xprev, u.i) ;

      /* We may have stepped over the end of the clip boundary. */
      INLINE_MAX32(x1, x1clip, x1) ;
      INLINE_MIN32(x2, x2clipp1, x2) ;

      --x2 ;

      switch ( color->quantised.state ) {
      case blit_quantise_min:
        blkfill1(rb, ys, ye, x1 - x_sep_position, x2 - x_sep_position);
        goto CONTINUE ;
      case blit_quantise_max:
        blkfill0(rb, ys, ye, x1 - x_sep_position, x2 - x_sep_position);
        goto CONTINUE ;
      default:
        GET_FORM(color->quantised.qcv[BLIT_SOLE_CHANNEL], ht_params);
        halfform_addr = theFormA(*ht_params->form);
        break ;
      }

      /* Convert x1,x2 to position in bits. */
      x1 <<= depth_shift;
      x2 <<= depth_shift;

      formptr = &rb->ylineaddr[x1 >> BLIT_SHIFT_BITS] ;
      firstmask = SHIFTRIGHT(ALLONES , x1 & BLIT_MASK_BITS);
      lastmask = SHIFTLEFT(ALLONES, BLIT_WIDTH_BITS - (1 << depth_shift)
                           - (x2 & BLIT_MASK_BITS));

      /* Convert to position of start of word, expressed in bits. */
      x1 &= ~BLIT_MASK_BITS ;
      x2 &= ~BLIT_MASK_BITS ;

      if ( halfypos < 0 ) {
        FINDSGNBITS(ht_params, bxpos1, bypos1, x1, ys);

        lhalfcx = ht_params->cx;
        lhalfccy = bypos1;

        halfypos = halfys[ bypos1 ];
        HQASSERT(halfypos >= 0, "Halftone Y offset negative") ;
      }

      if ( (xcount = x2 - x1) == 0 ) { /* span is within one word */
        register dcoord dy = height;

        LFINDSGNBITSXHPN(lhalfcx, lhalfccy, bxpos1, bypos1, x1, halfypos);

        blshift1 = bxpos1 & BLIT_MASK_BITS;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfypos);
        halfptr1 = &halfptr1[bxpos1 >> BLIT_SHIFT_BITS] ;
        bypos1 = halfydims - bypos1;

        firstmask &= lastmask;    /* And together firstmask & lastmask. */

        if (dy <= bypos1) {
          if ((mask = SHIFTLEFT( ALLONES, blshift1 ) & firstmask) == 0) {
            /* V.Fast one-word case where HT only needs fetching from right */
            /* column and doesn't wrap round vertically */

            /* use local temporary to make compiler's register allocation */
            /* easier */
            register blit_t lfirstmask = firstmask;
            register int32 shift = BLIT_WIDTH_BITS - blshift1;
            register int32 dh = halfys_1;
            register int32 df = wupdate;

            ++halfptr1;           /* save indexing within loop */
            do {
              mask =  SHIFTRIGHT( halfptr1[ 0 ] , shift );
              *formptr = (~lfirstmask & *formptr) | (lfirstmask & mask);
              formptr = BLIT_ADDRESS(formptr, df);
              halfptr1 = BLIT_ADDRESS(halfptr1, dh);
            } while (--dy > 0);
          }
          else if (mask == firstmask) {
            /* V.Fast one-word case where HT only needs fetching from left */
            /* column and doesn't wrap round vertically */

            /* use local temporary to make compiler's register allocation */
            /* easier */
            register blit_t lfirstmask = firstmask;
            register int32 shift = blshift1;
            register int32 dh = halfys_1;
            register int32 df = wupdate;

            /* This case covers blshift == 0! so no 32 bit right shift case */
            /* occurs for the immediate above or below cases */
            do {
              mask =  SHIFTLEFT( halfptr1[ 0 ] , shift );
              *formptr = (~lfirstmask & *formptr) | (lfirstmask & mask);
              formptr = BLIT_ADDRESS(formptr, df);
              halfptr1 = BLIT_ADDRESS(halfptr1, dh);
            } while (--dy > 0);
          }
          else {
            /* Fast one-word case where HT is split into two columns but does */
            /* not wrap round vertically */

            /* use local temporary to make compiler's register allocation */
            /* easier */
            register blit_t lfirstmask = firstmask;
            register int32 dh = halfys_1;
            register int32 df = wupdate;

            do {
              BLIT_SHIFT_MERGE(mask, halfptr1[0], halfptr1[1], blshift1) ;
              *formptr = (~lfirstmask & *formptr) | (lfirstmask & mask);
              formptr = BLIT_ADDRESS(formptr, df);
              halfptr1 = BLIT_ADDRESS(halfptr1, dh);
            } while (--dy > 0);
          }
        }
        else {
          /* General one-word case where HT may wrap round vertically */

          /* use local temporary to make compiler's register allocation easier */
          register int32 dh = halfys_1;
          register int32 df = wupdate;

          for (;;) {
            BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
            *formptr = (~firstmask & *formptr) | (firstmask & mask);
            formptr = BLIT_ADDRESS(formptr, df);
            halfptr1 = BLIT_ADDRESS(halfptr1, dh);
            if (--dy <= 0)
              break;
            if ( --bypos1 == 0 ) {
              LFINDSGNBITSY1(bxpos1, bypos1);
              blshift1 = bxpos1 & BLIT_MASK_BITS;
              halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
              halfptr1 = &halfptr1[bxpos1 >> BLIT_SHIFT_BITS];
              bypos1 = halfydims - bypos1;
            }
          }
        }
      }
      else if ((xcount >>= BLIT_SHIFT_BITS) == 1) { /* span is within two words */
        register dcoord dy = height;
        register blit_t *halfptr2;
        int32 blshift2;
        int32 bxpos2, bypos2;

        LFINDSGNBITSXHPN(lhalfcx, lhalfccy, bxpos1, bypos1, x1, halfypos);
        bypos2 = bypos1;

        blshift1 = bxpos1 & BLIT_MASK_BITS;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfypos);
        halfptr1 = &halfptr1[bxpos1 >> BLIT_SHIFT_BITS];
        bypos1 = halfydims - bypos1;
        bxpos2 = bxpos1 + BLIT_WIDTH_BITS;
        if ( bxpos2 >= halfxdims ) {
          LFINDSGNBITSX(bxpos2, bypos2);
          blshift2 = bxpos2 & BLIT_MASK_BITS;
          halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]);
          halfptr2 = &halfptr2[bxpos2 >> BLIT_SHIFT_BITS];
          bypos2 = halfydims - bypos2;
        }
        else {
          if (dy <= bypos1) {
            if ( x2 < xemax ) {
              /* Fastest two-word case where HT is not split into two columns
                 AND does not wrap round vertically AND we are rendering left
                 to right AND we are not yet close to the right-hand edge of
                 the image */
              do {
                register blit_t hword1 = halfptr1[ 1 ];

                BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], hword1, blshift1) ;
                formptr[0] = (~firstmask & formptr[0]) | (firstmask & mask);
                BLIT_SHIFT_MERGE_SAFE(mask, hword1, halfptr1[2], blshift1) ;
                formptr[ 1 ] = mask ; /* x2 < xemax => no need to mask it! */
                formptr = BLIT_ADDRESS(formptr, wupdate);
                halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
              } while (--dy > 0);

              goto CONTINUE;
            } else if ( x1 >= xsmin ) {
              /* Fastest two-word case where HT is not split into two columns
                 AND does not wrap round vertically AND we are rendering
                 right to left AND we are not yet close to the left-hand edge
                 of the image */
              do {
                register blit_t hword1 = halfptr1[ 1 ];

                BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], hword1, blshift1) ;
                formptr[0] = mask;
                BLIT_SHIFT_MERGE_SAFE(mask, hword1, halfptr1[2], blshift1) ;
                formptr[1] = (~lastmask & formptr[1]) | (lastmask & mask);
                formptr = BLIT_ADDRESS(formptr, wupdate);
                halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
              } while (--dy > 0);

              goto CONTINUE;
            }
          }
          bypos2 = bypos1;
          blshift2 = blshift1;
          halfptr2 = halfptr1 + 1;
        }

        if (dy <= bypos1 && dy <= bypos2) {
          if ( x2 < xemax ) {
            /* Special two-word case where HT is split into two columns but
               does not wrap round vertically AND we are rendering left to
               right AND we are not yet close to the right-hand edge of the
               image */
            do {
              BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
              formptr[0] = (~firstmask & formptr[0]) | (firstmask & mask);
              BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
              formptr[ 1 ] = mask ; /* x2 < xemax => no need to mask it! */
              formptr = BLIT_ADDRESS(formptr, wupdate);
              halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
              halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1);
            } while (--dy > 0);

            goto CONTINUE ;
          } else if ( x1 >= xsmin ) {
            /* Special two-word case where HT is split into two columns but
               does not wrap round vertically AND we are rendering right to
               left AND we are not yet close to the left-hand edge of the
               image */
            do {
              BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
              formptr[0] = mask ;
              BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
              formptr[1] = (~lastmask & formptr[1]) | (lastmask & mask);
              formptr = BLIT_ADDRESS(formptr, wupdate);
              halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
              halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1);
            } while (--dy > 0);

            goto CONTINUE ;
          }
        }

        /* general two word destination which may wrap around vertically in */
        /* HT cell or needs masking on the right-hand edge because we are */
        /* close to the right-hand edge of the image */
        for (;;) {
          BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
          formptr[0] = (~firstmask & formptr[0]) | (firstmask & mask);
          BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
          formptr[1] = (~lastmask & formptr[1]) | (lastmask & mask);
          formptr = BLIT_ADDRESS(formptr, wupdate);
          halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
          halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1);
          if (--dy <= 0)
            break;
          if ( (--bypos1) == 0 ) {
            LFINDSGNBITSY1( bxpos1 , bypos1 );
            blshift1 = bxpos1 & BLIT_MASK_BITS;
            halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
            halfptr1 = &halfptr1[bxpos1 >> BLIT_SHIFT_BITS];
            bypos1 = halfydims - bypos1;
          }
          if ( (--bypos2) == 0 ) {
            LFINDSGNBITSY1( bxpos2, bypos2 );
            blshift2 = bxpos2 & BLIT_MASK_BITS;
            halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]);
            halfptr2 = &halfptr2[bxpos2 >> BLIT_SHIFT_BITS];
            bypos2 = halfydims - bypos2;
          }
        }
      }
      else { /* span is more than two words */
        blit_t *eformptr;
        dcoord yss;

        /* THIS SHOULD BE OPTIMISED TO USE THE BEST more...() FUNCTION ACCORDING */
        /* TO TYPE OF HALFTONE CELL */

        /* General case. */
        eformptr = formptr + xcount;
        yss = ys;

        xcount += 1;
        do {
          register blit_t firstword = *formptr & ~firstmask ;
          register blit_t lastword = *eformptr & ~lastmask ;
          moresgnbitsptr(formptr, ht_params, x1, yss, xcount);
          *formptr = firstword | (firstmask & *formptr);
          *eformptr = lastword | (lastmask & *eformptr);
          formptr = BLIT_ADDRESS(formptr, wupdate);
          eformptr = BLIT_ADDRESS(eformptr, wupdate);
          ++yss;
        } while ( yss <= ye );
      }

    CONTINUE:
      xprev = u.i ;
    }
  } while ( rw != 0 ) ;
}

#undef FUNCTION
#undef PIXEL_FN

/* $Log$
*/
