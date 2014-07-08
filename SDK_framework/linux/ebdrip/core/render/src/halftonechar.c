/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:halftonechar.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Character blit functions.
 */

#include "core.h"

#include "halftonechar.h"
#include "bitblts.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "htrender.h" /* ht_params_t */
#include "render.h"
#include "converge.h"
#include "halftoneblts.h"
#include "toneblt.h" /* charbltn */


#ifdef BLIT_HALFTONE_1


static void charblth(render_blit_t *rb,
                     FORM *formptr ,
                     register dcoord x ,
                     dcoord y )
{
  register int32 w , xindex ;
  register int32 temp ;
  register blit_t mask, firstmask, lastmask, ow ;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;
  register blit_t *maskptr ;

  int32 rotate , repeat ;
  int32 bxpos , bypos , blshift ;
  int32 h , tx , ty , trx , hoff ;
  const render_info_t *p_ri = rb->p_ri ;
  ht_params_t *ht_params = p_ri->ht_params ;
  int32 halfpx = ht_params->px, halfpy = ht_params->py;
  int32 halfxdims = ht_params->xdims;
  int32 halfydims = ht_params->ydims;
  int32 halftype = ht_params->type;
  int32 *halfys = ht_params->ys;
  int32 halfr1 = ht_params->r1, halfr2 = ht_params->r2,
    halfr3 = ht_params->r3, halfr4 = ht_params->r4;
  blit_t *halfform_addr;

/* Masks for the left & right hand edge of bit-blt. */

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  int32 x1c , x2c ;
  int32 halfys1 ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->color->valid & blit_color_quantised, "Quantised color not set for char") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((rb->color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;
  HQASSERT(rb->color->quantised.spotno != SPOT_NO_INVALID,
           "No screen set up for halftone blit") ;

  HQASSERT(rb->color->quantised.qcv[BLIT_SOLE_CHANNEL] > 0 &&
           rb->color->quantised.qcv[BLIT_SOLE_CHANNEL] < rb->color->quantised.htmax[BLIT_SOLE_CHANNEL],
           "Halftone char fill called with black or white") ;
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "charblth called with a degenerate screen");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP ||
           formptr->type == FORMTYPE_BANDBITMAP,
           "Char form is not bitmap") ;

  blshift = repeat = bypos = rotate = 0; /* init to keep compiler quiet */
  mask = 0;                     /* init to keep compiler quiet */
  halfform_addr = theFormA(*ht_params->form);

  x1c = p_ri->clip.x1 + rb->x_sep_position;
  x2c = p_ri->clip.x2 + rb->x_sep_position;
  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = theFormA(*toform) ;

/* check & adjust right hand edge of bit-blt. */
  temp = x - x2c ;
  if ( temp > 0 )
    return ;

  trx = (( x + w ) & ~BLIT_MASK_BITS) ;
  lastmask = ALLONES ;
  temp += ( w - 1 ) ;
  if ( temp > 0 ) {
    trx = ( x2c & ~BLIT_MASK_BITS) ;
    w -= temp ;
    lastmask = SHIFTLEFT( lastmask , BLIT_MASK_BITS - ( x2c & BLIT_MASK_BITS )) ;
  }

/* Check & adjust left hand edge of bit-blt. */
  xindex = 0 ;
  firstmask = ALLONES ;
  temp = x1c - x ;
  if ( temp > 0 ) {
    w -= temp ;
    if ( w <= 0 )
      return ;

    firstmask = SHIFTRIGHT( firstmask , temp & BLIT_MASK_BITS ) ;

/* modify offset's' into src & dst's bitmaps. */
    wordptr = BLIT_ADDRESS(wordptr, BLIT_OFFSET(temp)) ;
    toword = BLIT_ADDRESS(toword, BLIT_OFFSET(x1c)) ;

    if ( x < 0 )
      x = BLIT_WIDTH_BITS - ((-x) & BLIT_MASK_BITS ) ;

    tx = ( x1c & ~BLIT_MASK_BITS) ;

    x &= BLIT_MASK_BITS ;
    temp = ( x1c & BLIT_MASK_BITS ) ;
    if ( x - temp > 0 ) {
      xindex = BLIT_WIDTH_BITS - x ;
      x = 0 ;
    }
    w -= ( x - temp ) ;
  }
  else {
    toword = BLIT_ADDRESS(toword, BLIT_OFFSET(x)) ;
    tx = ( x & ~BLIT_MASK_BITS) ;
    x &= BLIT_MASK_BITS ;
  }

/* Check & adjust bottom edge of bit-blt. */
  temp = y - p_ri->clip.y2;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
    h -= temp ;

/* Check & adjust top edge of bit-blt. */
  temp = p_ri->clip.y1 - y;
  if ( temp > 0 ) {
    h -= temp ;
    if ( h <= 0 )
      return ;

    wordptr = BLIT_ADDRESS(wordptr, temp * sycoff1) ;
    y = p_ri->clip.y1;
  }
  toword = BLIT_ADDRESS(toword, sycoff2 * (y - hoff)) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(xindex + temp) ;

  maskptr = theFormA(*ht_params->form) ;
  if ( halftype == ONELESSWORD ) {
    rotate = ht_params->rotate;
    repeat = halfxdims;
/* Get initial h/t mask position. */
    bypos = ( y + halfpy ) % halfydims ;
    blshift = ((tx & ~BLIT_MASK_BITS) + halfpx) % halfxdims ;
  }

  ty = y ;
  trx = (( trx - tx ) >> BLIT_SHIFT_BITS ) + 1 ;

  HQASSERT(x == 0 || xindex == 0, "Neither x nor xindex is zero in charblth") ;

  if ( x != xindex ) {
    if ( w + x + xindex <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      if ( x > xindex )
        lastmask = SHIFTLEFT( lastmask, x - xindex );
      else
        lastmask = SHIFTRIGHT( lastmask, xindex - x );
      firstmask &= lastmask ;
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr & firstmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          ow = SHIFTLEFT( ow , xindex ) ;
          *toword = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ( ++bypos == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr & firstmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          ow = SHIFTLEFT( ow , xindex ) ;
          *toword = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);
        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( h <= bypos ) {     /* No vertical wrap around */
          /* haven't implemented cases for one mask address */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            ow = SHIFTLEFT( ow , xindex ) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* wraps around vertically */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            ow = SHIFTLEFT( ow , xindex ) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            if ( (--bypos) != 0 ) {     /* still in same halftone cell */
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {    /* off bottom of cell */
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in charblth") ;
      }
    } else if ( w + xindex <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      register blit_t other ;
      int32 bxpos2, bypos2 ;
      int32 blshift2 ;

      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      hoff = BLIT_WIDTH_BITS - x ;
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr & firstmask ;
          ow = SHIFTLEFT( ow , xindex ) ;
          other = SHIFTLEFT( ow, hoff ) & lastmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          *toword = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword++ ;
          *toword = ((*toword) & (~other)) | ( other & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ( ++bypos == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr & firstmask ;
          ow = SHIFTLEFT( ow , xindex ) ;
          other = SHIFTLEFT( ow, hoff ) & lastmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          *toword = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword++ ;
          rotatepword( mask , rotate , repeat ) ;
          *toword = ((*toword) & (~other)) | ( other & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);

        bxpos2 = bxpos + BLIT_WIDTH_BITS ;
        bypos2 = bypos ;

        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( bxpos2 < halfxdims && h <= bypos ) {
          /* No horizontal or vertical wrap around */
          register blit_t hword1 ;

          while ( h > 0 ) {
            --h ;
            ow = *wordptr & firstmask ;
            ow = SHIFTLEFT( ow , xindex ) ;
            other = SHIFTLEFT( ow, hoff ) & lastmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            hword1 = maskptr[ 1 ] ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], hword1, blshift) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword++ ;
            BLIT_SHIFT_MERGE_SAFE(mask, hword1, maskptr[2], blshift) ;
            (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* Horizontal or vertical wrap around */
          register blit_t *maskptr2 ;

          LFINDSGNBITSX( bxpos2, bypos2 );
          blshift2 = bxpos2 & BLIT_MASK_BITS ;
          maskptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
          maskptr2 = ( & maskptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
          bypos2 = halfydims - bypos2 ;

          while ( h > 0 ) {
            --h ;
            ow = *wordptr & firstmask ;
            ow = SHIFTLEFT( ow , xindex ) ;
            other = SHIFTLEFT( ow, hoff ) & lastmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword++ ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr2[0], maskptr2[1], blshift2) ;
            (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            if ( (--bypos) != 0 ) {
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
            if ( (--bypos2) != 0 ) {
              maskptr2 = BLIT_ADDRESS(maskptr2, halfys1);
            } else {
              LFINDSGNBITSY1( bxpos2, bypos2 ) ;
              blshift2 = bxpos2 & BLIT_MASK_BITS ;
              maskptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
              maskptr2 = ( & maskptr2[ bxpos2 >> BLIT_SHIFT_BITS ] );
              bypos2 = halfydims - bypos2 ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in charblth") ;
      }
    } else {    /* doesn't fit into one word at all */
      blit_t *halftonebase = p_ri->p_rs->forms->halftonebase;
      Bool shiftfirst ;
      dcoord save_w = w ;
      register blit_t tempblit ;

      if ( ! x ) {
        x = BLIT_WIDTH_BITS - xindex ;
        shiftfirst = TRUE ;
      }
      else {
        xindex = BLIT_WIDTH_BITS - x ;
        shiftfirst = FALSE ;
      }
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) & firstmask ;
        ++wordptr ;

        switch ( halftype ) {
        case SPECIAL :
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          break ;
        case ONELESSWORD :
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          break ;
        case ORTHOGONAL :
          maskptr = halftonebase;
          moreonbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case GENERAL :
          maskptr = halftonebase;
          moregnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case SLOWGENERAL :
          maskptr = halftonebase;
          moresgnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        default:
          HQFAIL( "Impossible halftone type in charblth") ;
        }
        if ( shiftfirst )
          goto UNHEALTHYJUMP ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          if ( halftype >= ORTHOGONAL )
            mask = (*maskptr++) ;

          (*toword) = ((*toword) & (~tempblit)) | ( tempblit & mask ) ;
          ++toword ;

          if ( halftype == ONELESSWORD )
            rotatepword( mask , rotate , repeat ) ;

UNHEALTHYJUMP:
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        if ( halftype >= ORTHOGONAL )
          mask = (*maskptr) ;

        tempblit &= lastmask ;
        (*toword) = ((*toword) & (~tempblit)) | ( tempblit & mask ) ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
        ++ty ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) {     /* does it fit in one word? */
      firstmask &= lastmask ;
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr & firstmask ;
          *toword = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, x) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr & firstmask ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, x) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);
        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( h <= bypos ) {     /* No vertical wrap around */
          /* haven't implemented cases for one mask address */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, x) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* wraps around vertically */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, x) ;
            if ( (--bypos) != 0 ) {     /* still in same halftone cell */
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {    /* off bottom of cell */
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in charblth") ;
      }
    } else {    /* doesn't fit into a word */
      blit_t *halftonebase = p_ri->p_rs->forms->halftonebase;

      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = firstmask & (*wordptr) ;
        ++wordptr ;

        switch ( halftype ) {
        case SPECIAL :
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          break ;
        case ONELESSWORD :
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          break ;
        case ORTHOGONAL :
          maskptr = halftonebase;
          moreonbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case GENERAL :
          maskptr = halftonebase;
          moregnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case SLOWGENERAL :
          maskptr = halftonebase;
          moresgnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        default:
          HQFAIL( "Impossible halftone type in charblth") ;
        }
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          if ( halftype >= ORTHOGONAL )
            mask = (*maskptr++) ;

          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          ++toword ;

          if ( halftype == ONELESSWORD )
            rotatepword( mask , rotate , repeat ) ;

          ow = (*wordptr++) ;
          w -= BLIT_WIDTH_BITS ;
        }
        ow &= lastmask ;

        if ( halftype >= ORTHOGONAL )
          mask = (*maskptr) ;

        (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        ++ty ;
        w = xindex ;
      }
    }
  }
}


static void charcliph(render_blit_t *rb,
                      FORM *formptr ,
                      register dcoord x ,
                      dcoord y )
{
  register int32 w, xindex ;
  register int32 temp ;
  register blit_t mask, ow ;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;
  register blit_t *clipptr ;
  register blit_t *maskptr ;
  const render_info_t *p_ri = rb->p_ri ;
  ht_params_t *ht_params = p_ri->ht_params ;
  int32 rotate , repeat ;
  int32 bxpos , bypos , blshift ;
  int32 h , tx , ty , trx , hoff ;
  int32 halfpx = ht_params->px, halfpy = ht_params->py;
  int32 halfxdims = ht_params->xdims;
  int32 halfydims = ht_params->ydims;
  int32 halftype = ht_params->type;
  int32 *halfys = ht_params->ys;
  int32 halfr1 = ht_params->r1, halfr2 = ht_params->r2,
    halfr3 = ht_params->r3, halfr4 = ht_params->r4;
  blit_t *halfform_addr;
/* Masks for the left & right hand edge of bit-blt. */
  register blit_t firstmask , lastmask ;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  int32 x1c , x2c ;
  int32 halfys1 ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->color->valid & blit_color_quantised, "Quantised color not set for char") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((rb->color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;
  HQASSERT(rb->color->quantised.spotno != SPOT_NO_INVALID,
           "No screen set up for halftone blit") ;

  HQASSERT(rb->color->quantised.qcv[BLIT_SOLE_CHANNEL] > 0 &&
           rb->color->quantised.qcv[BLIT_SOLE_CHANNEL] < rb->color->quantised.htmax[BLIT_SOLE_CHANNEL],
           "Halftone char fill called with black or white") ;
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "charcliph called with a degenerate screen");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP ||
           formptr->type == FORMTYPE_BANDBITMAP,
           "Char form is not bitmap") ;

  blshift = repeat = bypos = rotate = 0; /* init to keep compiler quiet */
  mask = 0;                     /* init to keep compiler quiet */
  halfform_addr = theFormA(*ht_params->form);

  x1c = p_ri->clip.x1 + rb->x_sep_position;
  x2c = p_ri->clip.x2 + rb->x_sep_position;
  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = theFormA(*toform) ;

/* Extract all the form info. */
  clipptr = theFormA(*rb->clipform) ;

/* check & adjust right hand edge of bit-blt. */
  temp = x - x2c ;
  if ( temp > 0 )
    return ;

  trx = (( x + w ) & ~BLIT_MASK_BITS) ;
  lastmask = ALLONES ;
  temp += ( w - 1 ) ;
  if ( temp > 0 ) {
    trx = ( x2c & ~BLIT_MASK_BITS) ;
    w -= temp ;
    lastmask = SHIFTLEFT( lastmask , BLIT_MASK_BITS - ( x2c & BLIT_MASK_BITS )) ;
  }

/* Check & adjust left hand edge of bit-blt. */
  xindex = 0 ;
  firstmask = ALLONES ;
  temp = x1c - x ;
  if ( temp > 0 ) {
    w -= temp ;
    if ( w <= 0 )
      return ;

    firstmask = SHIFTRIGHT( firstmask , temp & BLIT_MASK_BITS ) ;

/* modify offset's' into src & dst's bitmaps. */
    wordptr = BLIT_ADDRESS(wordptr, BLIT_OFFSET(temp)) ;
    temp = BLIT_OFFSET(x1c) ;
    toword = BLIT_ADDRESS(toword, temp) ;
    clipptr = BLIT_ADDRESS(clipptr, temp) ;

    if ( x < 0 )
      x = BLIT_WIDTH_BITS - ((-x) & BLIT_MASK_BITS ) ;

    tx = ( x1c & ~BLIT_MASK_BITS) ;

    x &= BLIT_MASK_BITS ;
    temp = ( x1c & BLIT_MASK_BITS ) ;
    if ( x - temp > 0 ) {
      xindex = BLIT_WIDTH_BITS - x ;
      x = 0 ;
    }
    w -= ( x - temp ) ;
  }
  else {
    temp = BLIT_OFFSET(x) ;
    toword = BLIT_ADDRESS(toword, temp) ;
    clipptr = BLIT_ADDRESS(clipptr, temp) ;
    tx = ( x & ~BLIT_MASK_BITS) ;
    x &= BLIT_MASK_BITS ;
  }

/* Check & adjust bottom edge of bit-blt. */
  temp = y - p_ri->clip.y2;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
    h -= temp ;

/* Check & adjust top edge of bit-blt. */
  temp = p_ri->clip.y1 - y;
  if ( temp > 0 ) {
    h -= temp ;
    if ( h <= 0 )
      return ;

    wordptr = BLIT_ADDRESS(wordptr, temp * sycoff1) ;
    y = p_ri->clip.y1;
  }
  temp = sycoff2 * (y - hoff) ;
  toword = BLIT_ADDRESS(toword, temp) ;
  clipptr = BLIT_ADDRESS(clipptr, temp) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(xindex + temp) ;

  maskptr = theFormA(*ht_params->form) ;
  if ( halftype == ONELESSWORD ) {
    rotate = ht_params->rotate;
    repeat = halfxdims;
/* Get initial h/t mask position. */
    bypos = ( y + halfpy ) % halfydims ;
    blshift = ((tx & ~BLIT_MASK_BITS) + halfpx) % halfxdims ;
  }

  ty = y ;
  trx = (( trx - tx ) >> BLIT_SHIFT_BITS ) + 1 ;

  HQASSERT(x == 0 || xindex == 0, "Neither x nor xindex is zero in charcliph") ;

  if ( x != xindex ) {
    if ( w + x + xindex <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      if ( x > xindex )
        lastmask = SHIFTLEFT( lastmask, x - xindex );
      else
        lastmask = SHIFTRIGHT( lastmask, xindex - x );
      firstmask &= lastmask ;
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr & firstmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          ow = SHIFTLEFT( ow , xindex ) ;
          ow &= *clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          clipptr = BLIT_ADDRESS(clipptr, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ( ++bypos == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr & firstmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          ow = SHIFTLEFT( ow , xindex ) ;
          ow &= *clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          clipptr = BLIT_ADDRESS(clipptr, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);
        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( h <= bypos ) {     /* No vertical wrap around */
          /* haven't implemented cases for one mask address */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            ow = SHIFTLEFT( ow , xindex ) ;
            ow &= *clipptr ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            clipptr = BLIT_ADDRESS(clipptr, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* wraps around vertically */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            ow = SHIFTLEFT( ow , xindex ) ;
            ow &= *clipptr ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            clipptr = BLIT_ADDRESS(clipptr, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            if ( (--bypos) != 0 ) {     /* still in same halftone cell */
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {    /* off bottom of cell */
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in charcliph") ;
      }
    } else if ( w + xindex <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      register blit_t other ;
      int32 bxpos2, bypos2 ;
      int32 blshift2 ;

      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      hoff = BLIT_WIDTH_BITS - x ;
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr & firstmask ;
          ow = SHIFTLEFT( ow , xindex ) ;
          other = SHIFTLEFT( ow, hoff ) & lastmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          ow &= *clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword++, clipptr++ ;
          other &= *clipptr ;
          (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          clipptr = BLIT_ADDRESS(clipptr, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ( ++bypos == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr & firstmask ;
          ow = SHIFTLEFT( ow , xindex ) ;
          other = SHIFTLEFT( ow, hoff ) & lastmask ;
          ow = SHIFTRIGHT( ow , x ) ;
          ow &= *clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword++, clipptr++ ;
          other &= *clipptr ;
          rotatepword( mask , rotate , repeat ) ;
          (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          clipptr = BLIT_ADDRESS(clipptr, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);

        bxpos2 = bxpos + BLIT_WIDTH_BITS ;
        bypos2 = bypos ;

        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( bxpos2 < halfxdims && h <= bypos ) {
          /* No horizontal or vertical wrap around */
          register blit_t hword1 ;

          while ( h > 0 ) {
            --h ;
            ow = *wordptr & firstmask ;
            ow = SHIFTLEFT( ow , xindex ) ;
            other = SHIFTLEFT( ow, hoff ) & lastmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            ow &= *clipptr ;
            hword1 = maskptr[ 1 ] ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], hword1, blshift) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword++, clipptr++ ;
            BLIT_SHIFT_MERGE_SAFE(mask, hword1, maskptr[2], blshift) ;
            other &= *clipptr ;
            (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            clipptr = BLIT_ADDRESS(clipptr, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* Horizontal or vertical wrap around */
          register blit_t *maskptr2 ;

          LFINDSGNBITSX( bxpos2, bypos2 );
          blshift2 = bxpos2 & BLIT_MASK_BITS ;
          maskptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
          maskptr2 = ( & maskptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
          bypos2 = halfydims - bypos2 ;

          while ( h > 0 ) {
            --h ;
            ow = *wordptr & firstmask ;
            ow = SHIFTLEFT( ow , xindex ) ;
            other = SHIFTLEFT( ow, hoff ) & lastmask ;
            ow = SHIFTRIGHT( ow , x ) ;
            ow &= *clipptr ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword++, clipptr++ ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr2[0], maskptr2[1], blshift2) ;
            other &= *clipptr ;
            (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            clipptr = BLIT_ADDRESS(clipptr, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            if ( (--bypos) != 0 ) {
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
            if ( (--bypos2) != 0 ) {
              maskptr2 = BLIT_ADDRESS(maskptr2, halfys1);
            } else {
              LFINDSGNBITSY1( bxpos2, bypos2 ) ;
              blshift2 = bxpos2 & BLIT_MASK_BITS ;
              maskptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
              maskptr2 = ( & maskptr2[ bxpos2 >> BLIT_SHIFT_BITS ] );
              bypos2 = halfydims - bypos2 ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in charcliph") ;
      }
    } else {    /* doesn't fit into one word at all */
      blit_t *halftonebase = p_ri->p_rs->forms->halftonebase;
      Bool shiftfirst ;
      dcoord save_w = w ;
      register blit_t tempblit ;

      if ( ! x ) {
        x = BLIT_WIDTH_BITS - xindex ;
        shiftfirst = TRUE ;
      }
      else {
        xindex = BLIT_WIDTH_BITS - x ;
        shiftfirst = FALSE ;
      }
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = firstmask & (*wordptr) ;
        ++wordptr ;

        switch ( halftype ) {
        case SPECIAL :
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          break ;
        case ONELESSWORD :
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          break ;
        case ORTHOGONAL :
          maskptr = halftonebase;
          moreonbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case GENERAL :
          maskptr = halftonebase;
          moregnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case SLOWGENERAL :
          maskptr = halftonebase;
          moresgnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        default:
          HQFAIL( "Impossible halftone type in charcliph") ;
        }
        if ( shiftfirst )
          goto UNHEALTHYJUMP ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          if ( halftype >= ORTHOGONAL )
            mask = (*maskptr++) ;

          tempblit &= (*clipptr) ;
          ++clipptr ;
          (*toword) = ((*toword) & (~tempblit)) | ( tempblit & mask ) ;
          ++toword ;

          if ( halftype == ONELESSWORD )
            rotatepword( mask , rotate , repeat ) ;

UNHEALTHYJUMP:
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        if ( halftype >= ORTHOGONAL )
          mask = (*maskptr) ;

        tempblit &= lastmask ;
        tempblit &= *clipptr ;
        (*toword) = ((*toword) & (~tempblit)) | ( tempblit & mask ) ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        clipptr = BLIT_ADDRESS(clipptr, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
        ++ty ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) {     /* does it fit in one word? */
      firstmask &= lastmask ;
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr & firstmask ;
          ow &= *clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          clipptr = BLIT_ADDRESS(clipptr, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, x) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr & firstmask ;
          ow &= *clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          clipptr = BLIT_ADDRESS(clipptr, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, x) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);
        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( h <= bypos ) {     /* No vertical wrap around */
          /* haven't implemented cases for one mask address */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            ow &= *clipptr ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            clipptr = BLIT_ADDRESS(clipptr, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, x) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* wraps around vertically */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr & firstmask ;
            ow &= *clipptr ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            clipptr = BLIT_ADDRESS(clipptr, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, x) ;
            if ( (--bypos) != 0 ) {     /* still in same halftone cell */
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {    /* off bottom of cell */
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in charcliph") ;
      }
    } else {    /* doesn't fit into a word */
      blit_t *halftonebase = p_ri->p_rs->forms->halftonebase;

      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;

        switch ( halftype ) {
        case SPECIAL :
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          break ;
        case ONELESSWORD :
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          break ;
        case ORTHOGONAL :
          maskptr = halftonebase;
          moreonbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case GENERAL :
          maskptr = halftonebase;
          moregnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case SLOWGENERAL :
          maskptr = halftonebase;
          moresgnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        default:
          HQFAIL( "Impossible halftone type in charcliph") ;
        }

        while ( w > 0 ) {
          if ( halftype >= ORTHOGONAL )
            mask = (*maskptr++) ;

          ow &= (*clipptr) ;
          ++clipptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          ++toword ;

          if ( halftype == ONELESSWORD )
            rotatepword( mask , rotate , repeat ) ;

          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        ow &= lastmask ;
        ow &= *clipptr ;

        if ( halftype >= ORTHOGONAL )
          mask = (*maskptr) ;

        (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        ++ty ;
        w = xindex ;
      }
    }
  }
}


static void fastcharblth(render_blit_t *rb,
                         FORM *formptr ,
                         register dcoord x ,
                         dcoord y )
{
  register int32 w, xindex ;
  register int32 temp ;
  register blit_t mask, ow ;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;
  register blit_t *maskptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 rotate , repeat ;
  int32 bxpos, bypos , blshift ;
  int32 h , tx , ty , trx , hoff ;
  int32 halfpx = ht_params->px, halfpy = ht_params->py;
  int32 halfxdims = ht_params->xdims;
  int32 halfydims = ht_params->ydims;
  int32 halftype = ht_params->type;
  int32 *halfys = ht_params->ys;
  int32 halfr1 = ht_params->r1, halfr2 = ht_params->r2,
    halfr3 = ht_params->r3, halfr4 = ht_params->r4;
  blit_t *halfform_addr;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  int32 halfys1 ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->color->valid & blit_color_quantised, "Quantised color not set for char") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((rb->color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;
  HQASSERT(rb->color->quantised.spotno != SPOT_NO_INVALID,
           "No screen set up for halftone blit") ;

  HQASSERT(rb->color->quantised.qcv[BLIT_SOLE_CHANNEL] > 0 &&
           rb->color->quantised.qcv[BLIT_SOLE_CHANNEL] < rb->color->quantised.htmax[BLIT_SOLE_CHANNEL],
           "Halftone char fill called with black or white") ;
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "fastcharblth called with a degenerate screen");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP ||
           formptr->type == FORMTYPE_BANDBITMAP,
           "Char form is not bitmap") ;

  blshift = repeat = bypos = rotate = 0; /* init to keep compiler quiet */
  mask = 0;                     /* init to keep compiler quiet */
  halfform_addr = theFormA(*ht_params->form);

  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = BLIT_ADDRESS(theFormA(*toform), BLIT_OFFSET(x)) ;
  toword = BLIT_ADDRESS(toword, sycoff2 * (y - hoff)) ;

  tx = ( x & ~BLIT_MASK_BITS) ;
  trx = (( x + w ) & ~BLIT_MASK_BITS) ;
  x &= BLIT_MASK_BITS ;
  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(temp) ;

  maskptr = theFormA(*ht_params->form) ;
  if ( halftype == ONELESSWORD ) {
    rotate = ht_params->rotate;
    repeat = halfxdims;
/* Get initial h/t mask position. */
    bypos = ( y + halfpy ) % halfydims ;
    blshift = ((tx & ~BLIT_MASK_BITS) + halfpx) % halfxdims ;
  }

  ty = y ;
  trx = (( trx - tx ) >> BLIT_SHIFT_BITS ) + 1 ;

  if ( x ) {
    if ( w + x <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = SHIFTRIGHT( *wordptr , x ) ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ( ++bypos == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = SHIFTRIGHT( *wordptr , x ) ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);
        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( h <= bypos ) {     /* No vertical wrap around */
          /* haven't implemented cases for one mask address */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = SHIFTRIGHT( *wordptr , x ) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* wraps around vertically */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = SHIFTRIGHT( *wordptr , x ) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            if ( (--bypos) != 0 ) {     /* still in same halftone cell */
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {    /* off bottom of cell */
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in fastcharblth") ;
      }
    } else if ( w <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      register blit_t other ;
      int32 bxpos2, bypos2 ;
      int32 blshift2 ;

      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      xindex = BLIT_WIDTH_BITS - x ;
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr ;
          other = SHIFTLEFT( ow, xindex ) ;
          ow = SHIFTRIGHT( ow , x ) ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword++ ;
          (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ( ++bypos == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr ;
          other = SHIFTLEFT( ow, xindex ) ;
          ow = SHIFTRIGHT( ow , x ) ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword++ ;
          rotatepword( mask , rotate , repeat ) ;
          (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, w) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);

        bxpos2 = bxpos + BLIT_WIDTH_BITS ;
        bypos2 = bypos ;

        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( bxpos2 < halfxdims && h <= bypos ) {
          /* No horizontal or vertical wrap around */
          register blit_t hword1 ;

          while ( h > 0 ) {
            --h ;
            ow = *wordptr ;
            other = SHIFTLEFT( ow, xindex ) ;
            ow = SHIFTRIGHT( ow , x ) ;
            hword1 = maskptr[ 1 ] ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], hword1, blshift) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword++ ;
            BLIT_SHIFT_MERGE_SAFE(mask, hword1, maskptr[2], blshift) ;
            (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* Horizontal or vertical wrap around */
          register blit_t *maskptr2 ;

          LFINDSGNBITSX( bxpos2, bypos2 );
          blshift2 = bxpos2 & BLIT_MASK_BITS ;
          maskptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
          maskptr2 = ( & maskptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
          bypos2 = halfydims - bypos2 ;

          while ( h > 0 ) {
            --h ;
            ow = *wordptr ;
            other = SHIFTLEFT( ow, xindex ) ;
            ow = SHIFTRIGHT( ow , x ) ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword++ ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr2[0], maskptr2[1], blshift2) ;
            (*toword) = ((*toword) & (~other)) | ( other & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, w) ;
            if ( (--bypos) != 0 ) {
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
            if ( (--bypos2) != 0 ) {
              maskptr2 = BLIT_ADDRESS(maskptr2, halfys1);
            } else {
              LFINDSGNBITSY1( bxpos2, bypos2 ) ;
              blshift2 = bxpos2 & BLIT_MASK_BITS ;
              maskptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
              maskptr2 = ( & maskptr2[ bxpos2 >> BLIT_SHIFT_BITS ] );
              bypos2 = halfydims - bypos2 ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in fastcharblth") ;
      }
    } else {    /* doesn't fit into one word at all */
      blit_t *halftonebase = rb->p_ri->p_rs->forms->halftonebase;
      dcoord save_w = w ;
      register blit_t tempblit ;

      xindex = BLIT_WIDTH_BITS - x ;
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr)  ;
        ++wordptr ;

        switch ( halftype ) {
        case SPECIAL :
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          break ;
        case ONELESSWORD :
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          break ;
        case ORTHOGONAL :
          maskptr = halftonebase;
          moreonbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case GENERAL :
          maskptr = halftonebase;
          moregnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case SLOWGENERAL :
          maskptr = halftonebase;
          moresgnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        default:
          HQFAIL( "Impossible halftone type in fastcharblth") ;
        }
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          if ( halftype >= ORTHOGONAL )
            mask = (*maskptr++) ;

          (*toword) = ((*toword) & (~tempblit)) | ( tempblit & mask ) ;
          ++toword ;

          if ( halftype == ONELESSWORD )
            rotatepword( mask , rotate , repeat ) ;

          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        if ( halftype >= ORTHOGONAL )
          mask = (*maskptr) ;

        (*toword) = ((*toword) & (~tempblit)) | ( tempblit & mask ) ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
        ++ty ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) {     /* does it fit in one word? */
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      switch ( halftype ) {
      case SPECIAL :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          ty++ ;
          ow = *wordptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, x) ;
        }
        break ;
      case ONELESSWORD :
        while ( h > 0 ) {
          --h ;
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          ow = *wordptr ;
          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          toword = BLIT_ADDRESS(toword, temp) ;
          wordptr = BLIT_ADDRESS(wordptr, x) ;
        }
        break ;
      case ORTHOGONAL:
      case GENERAL:
      case SLOWGENERAL:
        FINDSGNBITS(ht_params, bxpos, bypos, tx, ty);
        halfys1 = halfys[ 1 ];

        blshift = bxpos & BLIT_MASK_BITS ;
        maskptr = BLIT_ADDRESS(maskptr, halfys[bypos]);
        maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
        bypos = halfydims - bypos ;

        if ( h <= bypos ) {     /* No vertical wrap around */
          /* haven't implemented cases for one mask address */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, x) ;
            maskptr = BLIT_ADDRESS(maskptr, halfys1);
          }
        } else {        /* wraps around vertically */
          while ( h > 0 ) {
            --h ;
            BLIT_SHIFT_MERGE_SAFE(mask, maskptr[0], maskptr[1], blshift) ;
            ow = *wordptr ;
            (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
            toword = BLIT_ADDRESS(toword, temp) ;
            wordptr = BLIT_ADDRESS(wordptr, x) ;
            if ( (--bypos) != 0 ) {     /* still in same halftone cell */
              maskptr = BLIT_ADDRESS(maskptr, halfys1);
            } else {    /* off bottom of cell */
              LFINDSGNBITSY1( bxpos, bypos ) ;
              blshift = bxpos & BLIT_MASK_BITS ;
              maskptr = BLIT_ADDRESS(halfform_addr, halfys[bypos]) ;
              maskptr = ( & maskptr[ bxpos >> BLIT_SHIFT_BITS ] );
              bypos = halfydims - bypos ;
            }
          }
        }
        break ;
      default:
        HQFAIL( "Impossible halftone type in fastcharblth") ;
      }
    } else {    /* doesn't fit into a word */
      blit_t *halftonebase = rb->p_ri->p_rs->forms->halftonebase;

      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) ;
        ++wordptr ;

        switch ( halftype ) {
        case SPECIAL :
          mask = maskptr[ ( ty + halfpy ) & BLIT_MASK_BITS ] ;
          shiftpwordall(mask, halfpx) ;
          break ;
        case ONELESSWORD :
          mask = maskptr[ bypos ] ;
          if ((++bypos) == halfydims )
            bypos = 0 ;
          shiftpword( mask , blshift , repeat ) ;
          break ;
        case ORTHOGONAL :
          maskptr = halftonebase;
          moreonbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case GENERAL :
          maskptr = halftonebase;
          moregnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        case SLOWGENERAL :
          maskptr = halftonebase;
          moresgnbitsptr(maskptr, ht_params, tx, ty, trx) ;
          break ;
        default:
          HQFAIL( "Impossible halftone type in fastcharblth") ;
        }
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          if ( halftype >= ORTHOGONAL )
            mask = (*maskptr++) ;

          (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
          ++toword ;

          if ( halftype == ONELESSWORD )
            rotatepword( mask , rotate , repeat ) ;

          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }

        if ( halftype >= ORTHOGONAL )
          mask = (*maskptr) ;

        (*toword) = ((*toword) & (~ow)) | ( ow & mask ) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        ++ty ;
        w = xindex ;
      }
    }
  }
}


/* ---------------------------------------------------------------------- */
static void charblthmax(render_blit_t *rb,
                        FORM *formptr, dcoord x, dcoord y)
{
  /* charblts differ from bitblts in that they operate over a set of lines,
     so we need to narrow their focus to a single line, repeatedly, for our
     purposes here, and they also use outputform directly rather than
     ylineaddr, so we need to swap that out temporarily */
  /* Cast away the constness; will restore the original clip in the end. */
  dbbox_t *clip = &((render_info_t *)rb->p_ri)->clip;
  int32 xs, xe, ys, ye, xword;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;

  y += rb->y_sep_position;

  ys = y; ye = y + theFormH(*formptr) - 1;
  bbox_clip_y(clip, ys, ye);

  if (ye >= ys) {
    FORM hmaxForm;
    render_blit_t rb_hmax ;
    blit_t * pTarget, * pSpareLine;
    dbbox_t saved_clip;

    xs = x + rb->x_sep_position;
    xe = x + rb->x_sep_position + theFormW(*formptr) - 1;
    bbox_clip_x(clip, xs, xe);
    if (xe < xs)
      return;
    xs = xs >> BLIT_SHIFT_BITS;
    xe = xe >> BLIT_SHIFT_BITS;

    pSpareLine = rb->p_ri->p_rs->forms->maxbltbase;

    /* This will make more sense when clips are in rb->bounds */
    rb_hmax = *rb ;

    rb_hmax.outputform = & hmaxForm;
    theFormA(hmaxForm) = pSpareLine;
    theFormHOff(hmaxForm) = 0; /* for now */
    theFormW(hmaxForm) = theFormW(*rb->outputform);
    theFormH(hmaxForm) = 1;
    theFormL(hmaxForm) = theFormL(*rb->outputform);
    theFormT(hmaxForm) = theFormT(*rb->outputform);
    theFormS(hmaxForm) = theFormL(hmaxForm);

    saved_clip = *clip;

    rb_hmax.y_sep_position = 0 ;
    pTarget = BLIT_ADDRESS(theFormA(*rb->outputform),
                             theFormL(hmaxForm) * (ys - theFormHOff(*rb->outputform))) ;

    while (ys <= ye) {
      theFormHOff(hmaxForm) = ys;
      clip->y1 = clip->y2 = ys;

      for (xword = xs; xword <= xe; xword++)
        pSpareLine[xword] = 0;

      DO_CHAR(&rb_hmax, formptr, x, y);

      for (xword = xs; xword <= xe; xword++)
        pTarget[xword] |= pSpareLine[xword];

      ys++;
      pTarget = BLIT_ADDRESS(pTarget, theFormL(hmaxForm)) ;
    }

    *clip = saved_clip;
  }
}


static void charcliphmax(render_blit_t *rb,
                         FORM *formptr, dcoord x, dcoord y)
{
  /* charblts differ from bitblts in that they operate over a set of lines,
     so we need to narrow their focus to a single line, repeatedly, for our
     purposes here, and they also use outputform directly rather than
     ylineaddr, so we need to swap that out temporarily */
  /* Cast away the constness; will restore the original clip in the end. */
  dbbox_t *clip = &((render_info_t *)rb->p_ri)->clip;
  int32 xs, xe, ys, ye, xword;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

  y += rb->y_sep_position;

  ys = y; ye = y + theFormH(*formptr) - 1;
  bbox_clip_y(clip, ys, ye);

  if (ye >= ys) {
    FORM hmaxForm, hmaxClipForm ;
    render_blit_t rb_hmax ;
    blit_t * pTarget, * pSpareLine;
    dbbox_t saved_clip;
    int32 adjust;

    xs = x + rb->x_sep_position;
    xe = x + rb->x_sep_position + theFormW(*formptr) - 1;
    bbox_clip_x(clip, xs, xe);
    if (xe < xs)
      return;
    xs = xs >> BLIT_SHIFT_BITS;
    xe = xe >> BLIT_SHIFT_BITS;

    pSpareLine = rb->p_ri->p_rs->forms->maxbltbase;

    /* This will make more sense when clips are in rb->bounds */
    rb_hmax = *rb ;

    rb_hmax.outputform = & hmaxForm;
    theFormA(hmaxForm) = pSpareLine;
    theFormHOff(hmaxForm) = 0; /* for now */
    theFormW(hmaxForm) = theFormW(*rb->outputform);
    theFormH(hmaxForm) = 1;
    theFormL(hmaxForm) = theFormL(*rb->outputform);
    theFormT(hmaxForm) = theFormT(*rb->outputform);
    theFormS(hmaxForm) = theFormL(hmaxForm);

    rb_hmax.clipform = &hmaxClipForm ;
    theFormHOff(hmaxClipForm) = 0; /* for now */
    theFormW(hmaxClipForm) = theFormW(*rb->clipform);
    theFormH(hmaxClipForm) = 1;
    theFormL(hmaxClipForm) = theFormL(*rb->clipform);
    theFormT(hmaxClipForm) = theFormT(*rb->clipform);
    theFormS(hmaxClipForm) = theFormL(hmaxClipForm);

    saved_clip = *clip;

    rb_hmax.y_sep_position = 0;

    adjust = theFormL(hmaxForm) * (ys - theFormHOff(*rb->outputform)) ;
    pTarget = BLIT_ADDRESS(theFormA(*rb->outputform), adjust) ;
    theFormA(hmaxClipForm) = BLIT_ADDRESS(theFormA(*rb->clipform), adjust) ;

    while (ys <= ye) {
      theFormHOff(hmaxForm) = theFormHOff(hmaxClipForm) = ys;
      clip->y1 = clip->y2 = ys;

      for (xword = xs; xword <= xe; xword++)
        pSpareLine[xword] = 0;

      DO_CHAR(&rb_hmax, formptr, x, y);

      for (xword = xs; xword <= xe; xword++)
        pTarget[xword] |= pSpareLine[xword];

      ys++;
      theFormA(hmaxClipForm) = BLIT_ADDRESS(theFormA(hmaxClipForm),
                                              theFormL(hmaxForm)) ;
      pTarget = BLIT_ADDRESS(pTarget, theFormL(hmaxForm)) ;
    }

    *clip = saved_clip;
  }
}


/** Self-modifying blits for halftones. This works out what the appropriate
    blit to call is, calls it, and also installs it in place of the current
    blit. */
static void charhalftone(render_blit_t *rb,
                         FORM *formptr, dcoord x, dcoord y)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for span") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  case blit_quantise_min:
    slice = &blitslice1[rb->clipmode] ; /* Black */
    break ;
  case blit_quantise_max:
    slice = &blitslice0[rb->clipmode] ; /* White */
    break ;
  default:
    HQFAIL("Should only be one color channel for halftoning") ;
    /*@fallthrough@*/
  case blit_quantise_mid:
    {
      ht_params_t *ht_params = rb->p_ri->ht_params ;
      COLORVALUE colval = color->quantised.qcv[BLIT_SOLE_CHANNEL] ;
      HQASSERT(colval > 0 &&
               colval < color->quantised.htmax[BLIT_SOLE_CHANNEL],
               "Halftone BLT_HALF span called with black or white") ;
      HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
               "bitfillhalftone using a degenerate screen");
      GET_FORM(colval, ht_params);
      HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
               "Halftone form is not bitmap") ;
      slice = &blitsliceh[ht_params->type][rb->clipmode] ;
    }
    break ;
  }

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->charfn)(rb, formptr, x, y) ;
}

/* ---------------------------------------------------------------------- */

void init_halftone1_char(surface_t *halftone1)
{
  unsigned int i ;

  for ( i = 0 ; i < NHALFTONETYPES ; ++i ) {
    blitsliceh[i][BLT_CLP_NONE].charfn = fastcharblth ;
    blitsliceh[i][BLT_CLP_RECT].charfn = charblth ;
    blitsliceh[i][BLT_CLP_COMPLEX].charfn = charcliph ;
  }

  halftone1->baseblits[BLT_CLP_NONE].charfn =
    halftone1->baseblits[BLT_CLP_RECT].charfn =
    halftone1->baseblits[BLT_CLP_COMPLEX].charfn = charhalftone ;

  /* No min blits */
  halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn = charblthmax ;
  halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charcliphmax ;
}


#endif /* BLIT_HALFTONE_1 for all char fns */


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
void init_halftonen_char(surface_t *halftonen)
{
  unsigned int i ;

  for ( i = 0 ; i < NHALFTONETYPES ; ++i ) {
    nbit_blit_sliceh[i][BLT_CLP_NONE].charfn =
      nbit_blit_sliceh[i][BLT_CLP_RECT].charfn =
      nbit_blit_sliceh[i][BLT_CLP_COMPLEX].charfn = charbltn;
  }

  halftonen->baseblits[BLT_CLP_NONE].charfn =
    halftonen->baseblits[BLT_CLP_RECT].charfn =
    halftonen->baseblits[BLT_CLP_COMPLEX].charfn = charbltn;

  /* No min blits */
  halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charbltn;
}
#endif /* BLIT_HALFTONE_2 || BLIT_HALFTONE_4 */

/* Log stripped */
