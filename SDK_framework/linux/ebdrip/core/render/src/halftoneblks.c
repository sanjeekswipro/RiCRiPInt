/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:halftoneblks.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Block blit functions.
 */

#include "core.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "render.h"     /* x_sep_position */
#include "htrender.h" /* GET_FORM */
#include "converge.h"
#include "caching.h"
#include "spanlist.h"
#include "halftoneblts.h"
#include "halftoneblks.h"
#include "toneblt.h" /* blkclipn */
#include "hqmemset.h"

/* ---------------------------------------------------------------------- */

static void blkfillhs(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs, register dcoord xe)
{
  register int32 wupdate ;
  register blit_t mask ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *halfptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 halfpx = ht_params->px, halfpy = ht_params->py;
  dcoord xsbit, xebit, ww; /* xs, xe in bits, width in words */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "blkfillhs called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillhs" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;
  halfptr = theFormA(*ht_params->form) ;

  /* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1;
  /* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES , xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));

  /* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    /* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    formptr = bformptr ;
    do {
      mask = halfptr[ ( ys + halfpy ) & BLIT_MASK_BITS ] ;
      shiftpwordall(mask, halfpx) ;
      formptr[ 0 ] = ( formptr[ 0 ] & (~firstmask)) | ( firstmask & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    formptr = bformptr ;
    do {
      mask = halfptr[ ( ys + halfpy ) & BLIT_MASK_BITS ] ;
      shiftpwordall(mask, halfpx) ;
      formptr[ 0 ] = ( formptr[ 0 ] & (~firstmask)) | ( firstmask & mask ) ;
      formptr[ 1 ] = ( formptr[ 1 ] & (~lastmask )) | ( lastmask  & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  /* General case. */
  xsbit = (xsbit >> BLIT_SHIFT_BITS) + 1; /* first whole word */
  xebit >>= BLIT_SHIFT_BITS; /* last whole word + 1 */
  ww = xebit - xsbit;
  HQASSERT( ww >= 1, "Too few words in blkfillhs general case") ;
  do
  {
    mask = halfptr[ ( ys + halfpy ) & BLIT_MASK_BITS ] ;
    shiftpwordall(mask, halfpx) ;
    formptr = bformptr ;
    (*formptr) = ((*formptr) & (~firstmask)) | ( firstmask & mask ) ;
    ++formptr ;
    BlitSet(formptr, mask, ww);
    formptr += ww;
    formptr[ 0 ] = ( formptr[ 0 ] & (~lastmask)) | ( lastmask & mask ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate) ;
    ++ys ;
  } while ( ys <= ye ) ;
}

static void blkfillhl(render_blit_t *rb,
                      dcoord ys , dcoord ye , dcoord xs , dcoord xe )
{
  register int32 rotate ;
  register int32 repeat ;
  register blit_t mask ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *halfptr, *halfform_addr ;
  register int32 wupdate ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 n ;
  int32 bypos ;
  int32 blshift ;
  int32 halfydims = ht_params->ydims;
  dcoord xsbit, xebit; /* xs, xe in bits */
  dcoord ww, wr; /* width, remainder of width in words */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "blkfillhl called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillhl" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;
  halfform_addr = halfptr = theFormA(*ht_params->form);

/* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1;

  repeat = ht_params->xdims;
  rotate = ht_params->rotate;

/* Get initial h/t mask position. */
  bypos = (( ys + ht_params->py ) % halfydims );
  halfptr = ( & halfptr[ bypos ] ) ;
  bypos = halfydims - bypos ;
  blshift = ((xsbit & ~BLIT_MASK_BITS) + ht_params->px) % ht_params->xdims;

/* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES, xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));

  ye -= ys ;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    firstmask &= lastmask ;     /* And together firstmask & lastmask. */
    formptr = bformptr ;
    do {
      mask = (*halfptr++) ;
      shiftpword( mask , blshift , repeat ) ; /* Correct alignment of mask. */

      (*formptr) = ((*formptr) & (~firstmask)) | ( firstmask & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      if ( (--bypos) == 0 ) {
        halfptr = halfform_addr;
        bypos = halfydims ;
      }
    } while ((--ye) >= 0 ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    formptr = bformptr ;
    do {
      mask = (*halfptr++) ;
      shiftpword( mask , blshift , repeat ) ; /* Correct alignment of mask. */

      formptr[ 0 ] = ( formptr[ 0 ] & (~firstmask)) | ( firstmask & mask ) ;
      rotatepword( mask , rotate , repeat ) ;
      formptr[ 1 ] = ( formptr[ 1 ] & (~lastmask )) | ( lastmask  & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      if ( (--bypos) == 0 ) {
        halfptr = halfform_addr;
        bypos = halfydims ;
      }
    } while ((--ye) >= 0 ) ;
    return ;
  }
/* General case. */
  xsbit = (xsbit >> BLIT_SHIFT_BITS) + 1; /* first whole word */
  xebit >>= BLIT_SHIFT_BITS; /* last whole word + 1 */
  ww = xebit - xsbit;
  HQASSERT( ww >= 1, "Too few words in blkfillhl general case") ;
  wr = ww & 7; ww >>= 3; /* remainder and quotient by 8 */
  do {
    mask = (*halfptr++) ;
    shiftpword( mask , blshift , repeat ) ; /* Correct alignment of mask. */

    formptr = bformptr ;
    (*formptr) = ((*formptr) & (~firstmask)) | ( firstmask & mask ) ;
    ++formptr ;
    rotatepword( mask , rotate , repeat ) ;
    for ( n = ww ; n != 0 ; n--, formptr += 8 ) {
      PENTIUM_CACHE_LOAD(formptr + 7) ;
      formptr[0] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[1] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[2] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[3] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[4] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[5] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[6] = mask ; rotatepword( mask , rotate , repeat ) ;
      formptr[7] = mask ; rotatepword( mask , rotate , repeat ) ;
    }
    for ( n = wr ; n != 0 ; n--, formptr++ ) {
      formptr[0] = mask ; rotatepword( mask , rotate , repeat ) ;
    }
    (*formptr) = ((*formptr) & (~lastmask)) | ( lastmask & mask ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate) ;
    if ( (--bypos) == 0 ) {
      halfptr = halfform_addr;
      bypos = halfydims ;
    }
  } while ((--ye) >= 0 ) ;
}


static void blkfillho(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs , register dcoord xe)
{
  register int32 wupdate ;
  register blit_t mask ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *halfptr1 ;
  register blit_t *halfptr2 ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 n ;
  dcoord bxpos1 , bypos1 ;
  dcoord bxpos2  ;
  int32 blshift1, blshift2 ;
  int32 halfxdims = ht_params->xdims;
  int32 halfydims = ht_params->ydims;
  int32 halfys_1 = ht_params->ys[ 1 ];
  dcoord xsbit, xebit; /* xs, xe in bits */
  FORM *halfform = ht_params->form ;

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "blkfillho called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillho" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;

/* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1;
/* Calculate the left & right masks.*/
  firstmask = SHIFTRIGHT( ALLONES , xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));
  xsbit &= ~BLIT_MASK_BITS;
  xebit &= ~BLIT_MASK_BITS;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    bxpos1 = ( xsbit + ht_params->px ) % halfxdims;
    bypos1 = ( ys + ht_params->py ) % halfydims;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(theFormA(*halfform), ht_params->ys[bypos1]) ;
    halfptr1 = &halfptr1[bxpos1 >> BLIT_SHIFT_BITS] ;

    firstmask &= lastmask ;     /* And together firstmask & lastmask. */
    formptr = bformptr ;

    do {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      (*formptr) = ((*formptr) & (~firstmask)) | ( firstmask & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1) ;
      if ((++bypos1) == halfydims ) {
        halfptr1 = BLIT_ADDRESS(halfptr1, -theFormS(*halfform)) ;
        bypos1 = 0 ;
      }
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    bxpos1 = ( xsbit + ht_params->px ) % halfxdims;
    bypos1 = ( ys + ht_params->py ) % halfydims;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(theFormA(*halfform), ht_params->ys[bypos1]) ;
    halfptr1 = &halfptr1[bxpos1 >> BLIT_SHIFT_BITS] ;
    bxpos2 = bxpos1 + BLIT_WIDTH_BITS ;
    if ( bxpos2 >= halfxdims ) {
      bxpos2 -= halfxdims ;
      blshift2 = bxpos2 & BLIT_MASK_BITS ;
      halfptr2 = BLIT_ADDRESS(theFormA(*halfform), ht_params->ys[bypos1]) ;
    }
    else {
      blshift2 = blshift1 ;
      halfptr2 = halfptr1 + 1 ;
    }
    formptr = bformptr ;
    do {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      formptr[ 0 ] = ( formptr[ 0 ] & (~firstmask)) | ( firstmask & mask ) ;
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
      formptr[ 1 ] = ( formptr[ 1 ] & (~lastmask )) | ( lastmask  & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1) ;
      halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1) ;
      if ((++bypos1) == halfydims ) {
        halfptr1 = BLIT_ADDRESS(halfptr1, -theFormS(*halfform)) ;
        halfptr2 = BLIT_ADDRESS(halfptr2, -theFormS(*halfform)) ;
        bypos1 = 0 ;
      }
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
/* General case. */
  n = (( xebit - xsbit ) >> BLIT_SHIFT_BITS ) + 1;
  ++eformptr ;
  do {
    register blit_t firstword = *bformptr & ~firstmask ;
    register blit_t lastword = *eformptr & ~lastmask ;
    moreonbitsptr(bformptr, ht_params, xsbit, ys, n);
    *bformptr = firstword | (firstmask & *bformptr);
    *eformptr = lastword | (lastmask & *eformptr);
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
  return ;
}

static void blkfillhg(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs , register dcoord xe)
{
  register int32 wupdate ;
  register blit_t mask ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *halfptr1, *halfptr2 ;
  int32 n ;
  dcoord bxpos1 , bypos1 ;
  dcoord bxpos2 , bypos2 ;
  int32 blshift1, blshift2 ;
  ht_params_t *htp = rb->p_ri->ht_params;
  int32 halfydims = htp->ydims;
  const int32 *halfys = htp->ys;
  int32 halfys_1 = halfys[ 1 ];
  dcoord xsbit, xebit; /* xs, xe in bits */
  blit_t *halfform_addr;

  HQASSERT(!HT_PARAMS_DEGENERATE(htp),
           "blkfillhg called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillhg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(htp->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform_addr = theFormA(*htp->form);
  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;

/* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1;
/* Calculate the left & right masks.*/
  firstmask = SHIFTRIGHT( ALLONES, xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));
  xsbit &= ~BLIT_MASK_BITS;
  xebit &= ~BLIT_MASK_BITS;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    bxpos1 = xsbit - htp->cx;
    bypos1 = ys - htp->cy;
    if ( ! (bxpos1 >= 0 && bxpos1 < htp->exdims &&
            bypos1 >= 0 && bypos1 < halfydims) )
      findgnbits( htp, &bxpos1, &bypos1, xsbit, ys );
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bypos1 = halfydims - bypos1 ;

    firstmask &= lastmask ;     /* And together firstmask & lastmask. */
    formptr = bformptr ;

    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      (*formptr) = ((*formptr) & (~firstmask)) | ( firstmask & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1) ;
      if (++ys > ye)
        break;
      if ( (--bypos1) == 0 ) {
        findgnbits( htp, &bxpos1, &bypos1, xsbit, ys );
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
        bypos1 = halfydims - bypos1 ;
      }
    }
    return ;
  }
  if ( bformptr == eformptr ) {
    bxpos1 = xsbit - htp->cx;
    bypos1 = ys - htp->cy;
    if ( ! (bxpos1 >= 0 && bxpos1 < htp->exdims &&
            bypos1 >= 0 && bypos1 < halfydims) )
      findgnbits( htp, &bxpos1, &bypos1, xsbit, ys );
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bypos1 = halfydims - bypos1 ;
    bxpos2 = bxpos1 + BLIT_WIDTH_BITS ;
    if ( bxpos2 >= htp->exdims ) {
      findgnbits(htp, &bxpos2, &bypos2, xsbit + BLIT_WIDTH_BITS, ys) ;
      blshift2 = bxpos2 & BLIT_MASK_BITS ;
      halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
      halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
      bypos2 = halfydims - bypos2 ;
    }
    else {
      bypos2 = bypos1 ;
      blshift2 = blshift1 ;
      halfptr2 = halfptr1 + 1 ;
    }

    formptr = bformptr ;

    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      formptr[ 0 ] = ( formptr[ 0 ] & (~firstmask)) | ( firstmask & mask ) ;
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
      formptr[ 1 ] = ( formptr[ 1 ] & (~lastmask )) | ( lastmask  & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate) ;
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1) ;
      halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1) ;
      if (++ys > ye)
        break;
      if ( (--bypos1) == 0 ) {
        findgnbits(htp, &bxpos1, &bypos1, xsbit, ys);
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
        bypos1 = halfydims - bypos1 ;
      }
      if ( (--bypos2) == 0 ) {
        findgnbits(htp, &bxpos2, &bypos2, xsbit + BLIT_WIDTH_BITS, ys);
        blshift2 = bxpos2 & BLIT_MASK_BITS ;
        halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
        halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
        bypos2 = halfydims - bypos2 ;
      }
    }
    return ;
  }
/* General case. */
  n = (( xebit - xsbit ) >> BLIT_SHIFT_BITS ) + 1;
  ++eformptr ;
  do {
    register blit_t firstword = *bformptr & ~firstmask ;
    register blit_t lastword = *eformptr & ~lastmask ;
    moregnbitsptr(bformptr, htp, xsbit, ys, n);
    *bformptr = firstword | (firstmask & *bformptr);
    *eformptr = lastword | (lastmask & *eformptr);
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
  return ;
}

static void blkfillhsg(render_blit_t *rb, dcoord ys, dcoord ye,
                       register dcoord xs , register dcoord xe)
{
  register int32 wupdate ;
  register blit_t mask ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *halfptr1, *halfptr2 ;
  int32 n ;
  dcoord bxpos1 , bypos1 ;
  dcoord bxpos2 , bypos2 ;
  int32 blshift1, blshift2 ;
  ht_params_t *htp = rb->p_ri->ht_params;
  int32 halfydims = htp->ydims;
  const int32 *halfys = htp->ys;
  int32 halfys_1 = halfys[ 1 ];
  dcoord xsbit, xebit; /* xs, xe in bits */
  blit_t *halfform_addr;

  HQASSERT(!HT_PARAMS_DEGENERATE(htp),
           "blkfillhsg called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillhsg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(htp->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform_addr = theFormA(*htp->form);
  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;

/* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1;
/* Calculate the left & right masks.*/
  firstmask = SHIFTRIGHT( ALLONES, xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));
  xsbit &= ~BLIT_MASK_BITS;
  xebit &= ~BLIT_MASK_BITS;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    findsgnbits( htp, &bxpos1, &bypos1, xsbit, ys );
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;

    firstmask &= lastmask ;     /* And together firstmask & lastmask. */
    formptr = bformptr ;

    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      (*formptr) = ((*formptr) & (~firstmask)) | ( firstmask & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      if (++ys > ye)
        break;
      if ( (++bypos1) == halfydims ) {
        findsgnbits( htp, &bxpos1, &bypos1, xsbit, ys );
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
      }
    }
    return ;
  }
  if ( bformptr == eformptr ) {
    findsgnbits( htp, &bxpos1, &bypos1, xsbit, ys );
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bxpos2 = bxpos1 + BLIT_WIDTH_BITS ;
    if ( bxpos2 >= htp->xdims ) {
      findsgnbits(htp, &bxpos2, &bypos2, xsbit + BLIT_WIDTH_BITS, ys);
      blshift2 = bxpos2 & BLIT_MASK_BITS ;
      halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
      halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
    }
    else {
      bypos2 = bypos1 ;
      blshift2 = blshift1 ;
      halfptr2 = halfptr1 + 1 ;
    }

    formptr = bformptr ;

    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      formptr[ 0 ] = ( formptr[ 0 ] & (~firstmask)) | ( firstmask & mask ) ;
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
      formptr[ 1 ] = ( formptr[ 1 ] & (~lastmask )) | ( lastmask  & mask ) ;
      formptr = BLIT_ADDRESS(formptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1);
      if (++ys > ye)
        break;
      if ( (++bypos1) == halfydims ) {
        findsgnbits(htp, &bxpos1, &bypos1, xsbit, ys);
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
      }
      if ( (++bypos2) == halfydims ) {
        findsgnbits(htp, &bxpos2, &bypos2, xsbit + BLIT_WIDTH_BITS, ys);
        blshift2 = bxpos2 & BLIT_MASK_BITS ;
        halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
        halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
      }
    }
    return ;
  }
/* General case. */
  n = (( xebit - xsbit ) >> BLIT_SHIFT_BITS ) + 1;
  ++eformptr ;
  do {
    register blit_t firstword = *bformptr & ~firstmask ;
    register blit_t lastword = *eformptr & ~lastmask ;
    moresgnbitsptr(bformptr, htp, xsbit, ys, n);
    *bformptr = firstword | (firstmask & *bformptr);
    *eformptr = lastword | (lastmask & *eformptr);
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
  return ;
}


#ifdef BLIT_HALFTONE_1


/* ---------------------------------------------------------------------- */

static void blkcliphs(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs, register dcoord xe)
{
  register int32 wupdate ;
  register blit_t mask, temp ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *clipptr , *bclipptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 halfpx = ht_params->px, halfpy = ht_params->py;
  blit_t *halfform_addr;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "blkcliphs called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkcliphs" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform_addr = theFormA(*ht_params->form);
  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  wupdate = theFormL(*rb->outputform) ;
  formptr = halfform_addr;

/* Find left-most integer address. */
  bclipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs));
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xe)) - 1 ;
/* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES , xs & BLIT_MASK_BITS ) ;
  lastmask = SHIFTLEFT( ALLONES , BLIT_MASK_BITS - ( xe & BLIT_MASK_BITS )) ;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
/* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    do {
      mask = formptr[ ( ys + halfpy ) & BLIT_MASK_BITS ] ;
      shiftpwordall(mask, halfpx) ;
      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    do {
      mask = formptr[ ( ys + halfpy ) & BLIT_MASK_BITS ] ;
      shiftpwordall(mask, halfpx) ;
      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      temp = bclipptr[ 1 ] & lastmask ;
      bformptr[ 1 ] = ( bformptr[ 1 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
/* General case. */
  do {
    mask = halfform_addr[ ( ys + halfpy ) & BLIT_MASK_BITS ] ;
    shiftpwordall(mask, halfpx) ;
    formptr = bformptr ;
    clipptr = bclipptr ;
    temp = (*clipptr++) & firstmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    ++formptr ;
    do {
      temp = (*clipptr++) ;
      (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
      ++formptr ;
    } while ( formptr <= eformptr ) ;
    temp = (*clipptr) & lastmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate);
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
}


static void blkcliphl(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs , register dcoord xe)
{
  register int32 rotate ;
  register int32 repeat ;
  register int32 wupdate ;
  register blit_t mask, temp ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *clipptr , *bclipptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  dcoord bypos ;
  int32 blshift ;
  int32 halfydims = ht_params->ydims;
  blit_t *halfform_addr;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "blkcliphl called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkcliphl" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform_addr = theFormA(*ht_params->form);
  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  wupdate = theFormL(*rb->outputform) ;
  formptr = halfform_addr;

/* Find left-most integer address. */
  bclipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs));
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xe)) - 1 ;

  repeat = ht_params->xdims;
  rotate = ht_params->rotate;

/* Get initial h/t mask position. */
  bypos = ( ys + ht_params->py ) % halfydims;
  blshift = ((xs & ~BLIT_MASK_BITS) + ht_params->px) % ht_params->xdims ;

/* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES , xs & BLIT_MASK_BITS ) ;
  lastmask = SHIFTLEFT( ALLONES , BLIT_MASK_BITS - ( xe & BLIT_MASK_BITS )) ;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
/* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    do {
      mask = formptr[ bypos ] ;
/* Correct alignment of mask. */
      shiftpword( mask , blshift , repeat ) ;

      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      if ((++bypos) == halfydims )
        bypos = 0 ;

      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    do {
      mask = formptr[ bypos ] ;
/* Correct alignment of mask. */
      shiftpword( mask , blshift , repeat ) ;

      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      rotatepword( mask , rotate , repeat ) ;
      temp = bclipptr[ 1 ] & lastmask  ;
      bformptr[ 1 ] = ( bformptr[ 1 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      if ((++bypos) == halfydims )
        bypos = 0 ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
/* General case. */
  do {
    mask = halfform_addr[ bypos ];
/* Correct alignment of mask. */
    shiftpword( mask , blshift , repeat ) ;

    formptr = bformptr ;
    clipptr = bclipptr ;
    temp = (*clipptr++) & firstmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    ++formptr ;
    rotatepword( mask , rotate , repeat ) ;
    do {
      temp = (*clipptr++) ;
      (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
      ++formptr ;
      rotatepword( mask , rotate , repeat ) ;

    } while ( formptr <= eformptr ) ;
    temp = (*clipptr) & lastmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate);
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
    if ((++bypos) == halfydims )
      bypos = 0 ;
    ++ys ;
  } while ( ys <= ye ) ;
}


static void blkclipho(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs, register dcoord xe)
{
  register blit_t mask,temp ;
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *maskptr , *clipptr , *bclipptr ;
  register blit_t *halfptr1, *halfptr2 ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 n ;
  dcoord bxpos1, bypos1, bxpos2  ;
  int32 blshift1 , blshift2 ;
  int32 halfydims = ht_params->ydims;
  int32 halfys_1 = ht_params->ys[ 1 ];
  FORM *halfform ;
  blit_t *halftonebase = rb->p_ri->p_rs->forms->halftonebase;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "blkclipho called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkclipho" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform = ht_params->form;
  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  wupdate = theFormL(*rb->outputform) ;

/* Find left-most integer address. */
  bclipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xe)) - 1 ;
/* Calculate the left & right masks.*/
  firstmask = SHIFTRIGHT( ALLONES , xs & BLIT_MASK_BITS ) ;
  lastmask = SHIFTLEFT( ALLONES , ( BLIT_MASK_BITS - ( xe & BLIT_MASK_BITS )));
  xs &= ~BLIT_MASK_BITS ;
  xe &= ~BLIT_MASK_BITS ;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    bxpos1 = ( xs + ht_params->px ) % ht_params->xdims;
    bypos1 = ( ys + ht_params->py ) % halfydims;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(theFormA(*halfform), ht_params->ys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;

    firstmask &= lastmask ;     /* And together firstmask & lastmask. */
    do {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      temp = (*bclipptr) & firstmask ;
      (*bformptr) = ((*bformptr) & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      if ((++bypos1) == halfydims ) {
        halfptr1 = BLIT_ADDRESS(halfptr1, -theFormS(*halfform));
        bypos1 = 0 ;
      }
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    bxpos1 = ( xs + ht_params->px ) % ht_params->xdims;
    bypos1 = ( ys + ht_params->py ) % halfydims;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(theFormA(*halfform), ht_params->ys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bxpos2 = bxpos1 + BLIT_WIDTH_BITS ;
    if ( bxpos2 >= ht_params->xdims ) {
      bxpos2 -= ht_params->xdims ;
      blshift2 = bxpos2 & BLIT_MASK_BITS ;
      halfptr2 = BLIT_ADDRESS(theFormA(*halfform), ht_params->ys[bypos1]);
    }
    else {
      blshift2 = blshift1 ;
      halfptr2 = halfptr1 + 1 ;
    }
    do {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
      temp = bclipptr[ 1 ] & lastmask ;
      bformptr[ 1 ] = ( bformptr[ 1 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1);
      if ((++bypos1) == halfydims ) {
        halfptr1 = BLIT_ADDRESS(halfptr1, -theFormS(*halfform));
        halfptr2 = BLIT_ADDRESS(halfptr2, -theFormS(*halfform));
        bypos1 = 0 ;
      }
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  n = (( xe - xs ) >> BLIT_SHIFT_BITS ) + 1 ;
  do {
    formptr = bformptr ;
    clipptr = bclipptr ;
    maskptr = halftonebase;
    moreonbitsptr( maskptr, ht_params, xs, ys, n );
    temp = (*clipptr++) & firstmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
    ++formptr ;
    do {
      temp = (*clipptr++) ;
      (*formptr) = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
      ++formptr ;
    } while ( formptr <= eformptr ) ;
    temp = clipptr[ 0 ] & lastmask ;
    formptr[ 0 ] = ( formptr[ 0 ] & (~temp)) | ( temp & maskptr[ 0 ] ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate);
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
}


static void blkcliphg(render_blit_t *rb, dcoord ys, dcoord ye,
                      register dcoord xs, register dcoord xe)
{
  register blit_t mask , temp ;
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *maskptr , *clipptr , *bclipptr ;
  register blit_t *halfptr1 ;
  register blit_t *halfptr2 ;

  int32 n ;
  dcoord bxpos1 , bypos1 ;
  dcoord bxpos2 , bypos2 ;
  int32 blshift1 , blshift2 ;
  ht_params_t *htp = rb->p_ri->ht_params;
  int32 halfydims = htp->ydims;
  const int32 *halfys = htp->ys;
  int32 halfys_1 = halfys[ 1 ];
  blit_t *halfform_addr;
  blit_t *halftonebase = rb->p_ri->p_rs->forms->halftonebase;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(htp),
           "blkcliphg called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkcliphg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(htp->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform_addr = theFormA(*htp->form);
  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  wupdate = theFormL(*rb->outputform) ;

/* Find left-most integer address. */
  bclipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xe)) - 1 ;

/* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES , xs & BLIT_MASK_BITS ) ;
  lastmask = SHIFTLEFT( ALLONES , BLIT_MASK_BITS - ( xe & BLIT_MASK_BITS )) ;
  xs &= ~BLIT_MASK_BITS ;
  xe &= ~BLIT_MASK_BITS ;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    bxpos1 = xs - htp->cx;
    bypos1 = ys - htp->cy;
    if ( ! (bxpos1 >= 0 && bxpos1 < htp->exdims &&
            bypos1 >= 0 && bypos1 < htp->ydims) )
      findgnbits(htp, &bxpos1, &bypos1, xs, ys) ;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bypos1 = halfydims - bypos1 ;

    firstmask &= lastmask ;     /* And together firstmask & lastmask. */

    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      temp = (*bclipptr) & firstmask ;
      (*bformptr) = ((*bformptr) & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      if (++ys > ye)
        break;
      if ( (--bypos1) == 0 ) {
        findgnbits( htp, &bxpos1, &bypos1, xs, ys );
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
        bypos1 = halfydims - bypos1 ;
      }
    }
    return ;
  }
  if ( bformptr == eformptr ) {
    bxpos1 = xs - htp->cx;
    bypos1 = ys - htp->cy;
    if ( ! (bxpos1 >= 0 && bxpos1 < htp->exdims &&
            bypos1 >= 0 && bypos1 < htp->ydims) )
      findgnbits(htp, &bxpos1, &bypos1, xs, ys) ;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bypos1 = halfydims - bypos1 ;
    bxpos2 = bxpos1 + BLIT_WIDTH_BITS ;
    if ( bxpos2 >= htp->exdims ) {
      findgnbits(htp, &bxpos2, &bypos2, xs + BLIT_WIDTH_BITS, ys) ;
      blshift2 = bxpos2 & BLIT_MASK_BITS ;
      halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
      halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
      bypos2 = halfydims - bypos2 ;
    }
    else {
      bypos2 = bypos1 ;
      blshift2 = blshift1 ;
      halfptr2 = halfptr1 + 1 ;
    }
    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
      temp = bclipptr[ 1 ] & lastmask ;
      bformptr[ 1 ] = ( bformptr[ 1 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys[1]);
      halfptr2 = BLIT_ADDRESS(halfptr2, halfys[1]);
      if (++ys > ye)
        break;
      if ( (--bypos1) == 0 ) {
        findgnbits( htp, &bxpos1, &bypos1, xs, ys );
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
        bypos1 = halfydims - bypos1 ;
      }
      if ( (--bypos2) == 0 ) {
        findgnbits(htp, &bxpos2, &bypos2, xs + BLIT_WIDTH_BITS, ys) ;
        blshift2 = bxpos2 & BLIT_MASK_BITS ;
        halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
        halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
        bypos2 = halfydims - bypos2 ;
      }
    }
    return ;
  }
/* General case. */
  n = (( xe - xs ) >> BLIT_SHIFT_BITS ) + 1 ;
  do {
    formptr = bformptr ;
    clipptr = bclipptr ;
    maskptr = halftonebase;
    moregnbitsptr( maskptr, htp, xs, ys, n );
    temp = (*clipptr++) & firstmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
    ++formptr ;
    do {
      temp = (*clipptr++) ;
      (*formptr) = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
      ++formptr ;
    } while ( formptr <= eformptr ) ;
    temp = clipptr[ 0 ] & lastmask ;
    formptr[ 0 ] = ( formptr[ 0 ] & (~temp)) | ( temp & maskptr[ 0 ] ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate);
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
  return ;
}


static void blkcliphsg(render_blit_t *rb, dcoord ys, dcoord ye,
                       register dcoord xs, register dcoord xe)
{
  register blit_t mask, temp ;
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  register blit_t *maskptr , *clipptr , *bclipptr ;
  register blit_t *halfptr1 ;
  register blit_t *halfptr2 ;
  blit_t *halftonebase = rb->p_ri->p_rs->forms->halftonebase;
  int32 n ;
  dcoord bxpos1 , bypos1 ;
  dcoord bxpos2 , bypos2 ;
  int32 blshift1 , blshift2 ;
  ht_params_t *htp = rb->p_ri->ht_params;
  int32 halfydims = htp->ydims;
  const int32 *halfys = htp->ys;
  int32 halfys_1 = halfys[ 1 ];
  blit_t *halfform_addr;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(htp),
           "blkcliphsg called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkcliphsg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(htp->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  halfform_addr = theFormA(*htp->form);
  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  wupdate = theFormL(*rb->outputform) ;

/* Find left-most integer address. */
  bclipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xe)) - 1 ;
/* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES , xs & BLIT_MASK_BITS ) ;
  lastmask = SHIFTLEFT( ALLONES , BLIT_MASK_BITS - ( xe & BLIT_MASK_BITS )) ;
  xs &= ~BLIT_MASK_BITS ;
  xe &= ~BLIT_MASK_BITS ;

/* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    findsgnbits(htp, &bxpos1, &bypos1, xs, ys) ;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;

    firstmask &= lastmask ;

    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      temp = (*bclipptr) & firstmask ;
      (*bformptr) = ((*bformptr) & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      if (++ys > ye)
        break;
      if ((++bypos1) == halfydims ) {
        findsgnbits(htp, &bxpos1, &bypos1, xs, ys) ;
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
      }
    }
    return ;
  }
  if ( bformptr == eformptr ) {
    findsgnbits(htp, &bxpos1, &bypos1, xs, ys) ;
    blshift1 = bxpos1 & BLIT_MASK_BITS ;
    halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]);
    halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
    bxpos2 = bxpos1 + BLIT_WIDTH_BITS ;
    if ( bxpos2 >= htp->xdims ) {
      findsgnbits(htp, &bxpos2, &bypos2, xs + BLIT_WIDTH_BITS, ys) ;
      blshift2 = bxpos2 & BLIT_MASK_BITS ;
      halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]);
      halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
    }
    else {
      bypos2 = bypos1 ;
      blshift2 = blshift1 ;
      halfptr2 = halfptr1 + 1 ;
    }
    for (;;) {
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr1[0], halfptr1[1], blshift1) ;
      temp = bclipptr[ 0 ] & firstmask ;
      bformptr[ 0 ] = ( bformptr[ 0 ] & (~temp)) | ( temp & mask ) ;
      BLIT_SHIFT_MERGE_SAFE(mask, halfptr2[0], halfptr2[1], blshift2) ;
      temp = bclipptr[ 1 ] & lastmask ;
      bformptr[ 1 ] = ( bformptr[ 1 ] & (~temp)) | ( temp & mask ) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate);
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
      halfptr1 = BLIT_ADDRESS(halfptr1, halfys_1);
      halfptr2 = BLIT_ADDRESS(halfptr2, halfys_1);
      if (++ys > ye)
        break;
      if ((++bypos1) == halfydims ) {
        findsgnbits(htp, &bxpos1, &bypos1, xs, ys) ;
        blshift1 = bxpos1 & BLIT_MASK_BITS ;
        halfptr1 = BLIT_ADDRESS(halfform_addr, halfys[bypos1]) ;
        halfptr1 = ( & halfptr1[ bxpos1 >> BLIT_SHIFT_BITS ] ) ;
      }
      if ((++bypos2) == halfydims ) {
        findsgnbits(htp, &bxpos2, &bypos2, xs + BLIT_WIDTH_BITS, ys) ;
        blshift2 = bxpos2 & BLIT_MASK_BITS ;
        halfptr2 = BLIT_ADDRESS(halfform_addr, halfys[bypos2]) ;
        halfptr2 = ( & halfptr2[ bxpos2 >> BLIT_SHIFT_BITS ] ) ;
      }
    }
    return ;
  }
  n = (( xe - xs ) >> BLIT_SHIFT_BITS ) + 1 ;
  do {
    formptr = bformptr ;
    clipptr = bclipptr ;
    maskptr = halftonebase;
    moresgnbitsptr( maskptr, htp, xs, ys, n );
    temp = (*clipptr++) & firstmask ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
    ++formptr ;
    do {
      temp = (*clipptr++) ;
      (*formptr) = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
      ++formptr ;
    } while ( formptr <= eformptr ) ;
    temp = clipptr[ 0 ] & lastmask ;
    formptr[ 0 ] = ( formptr[ 0 ] & (~temp)) | ( temp & maskptr[ 0 ] ) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate);
    eformptr = BLIT_ADDRESS(eformptr, wupdate);
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate);
    ++ys ;
  } while ( ys <= ye ) ;
}


#endif /* BLIT_HALFTONE_1 for clip fns */


#if defined(BLIT_HALFTONE_1) || defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
static void nblkclip(render_blit_t *rb,
                     dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  /* Cannot dest &= clip, use generic clipping */
  ht_params_t *ht_params = rb->p_ri->ht_params ;

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "nblkclip called with a degenerate screen");
  blkclipn(rb, ys, ye, xs, xe,
           blitsliceh[ht_params->type][BLT_CLP_NONE].spanfn);
}
#endif


#ifdef BLIT_HALFTONE_1
/** Self-modifying blits for 1-bit halftone block fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void blkfillhalftone(render_blit_t *rb,
                            dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for block") ;
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
               "Halftone BLT_HALF block called with black or white") ;
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

  (*slice->blockfn)(rb, ys, ye, xs, xe) ;
}
#endif


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
/** Self-modifying blits for n-bit halftone block fns. */
static void nblkfillhalftone(render_blit_t *rb,
                             dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for block") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  case blit_quantise_min:
    slice = &nbit_blit_slice1[rb->clipmode]; /* Black */
    break;
  case blit_quantise_max:
    slice = &nbit_blit_slice0[rb->clipmode]; /* White */
    break;
  default:
    HQFAIL("Should only be one color channel for halftoning") ;
    /*@fallthrough@*/
  case blit_quantise_mid: {
    ht_params_t *ht_params = rb->p_ri->ht_params;
    COLORVALUE colval = color->quantised.qcv[BLIT_SOLE_CHANNEL];
    HQASSERT(colval > 0 &&
             colval < color->quantised.htmax[BLIT_SOLE_CHANNEL],
             "Halftone BLT_HALF span called with black or white") ;
    HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
             "nbitfillhalftone using a degenerate screen");
    GET_FORM(colval, ht_params);
    HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
             "Halftone form is not bitmap");
    slice = &nbit_blit_sliceh[ht_params->type][rb->clipmode];
  } break;
  }

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->blockfn)(rb, ys, ye, xs, xe) ;
}
#endif

#if defined(BLIT_HALFTONE_1) || defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
static void init_halftone_sliceh(void)
{
  /* Set up both blitsliceh and nbit_blit_sliceh. The n-bit versions can also
     be used for 1-bit, but are a bit less optimised in the complex clip
     case. They pass the spans through RLE or extracted bitmap clip spans
     before calling the base blit function. */
  blitsliceh[SPECIAL][BLT_CLP_NONE].blockfn =
    nbit_blit_sliceh[SPECIAL][BLT_CLP_NONE].blockfn = blkfillhs;
  blitsliceh[SPECIAL][BLT_CLP_RECT].blockfn =
    nbit_blit_sliceh[SPECIAL][BLT_CLP_RECT].blockfn = blkfillhs;
  blitsliceh[SPECIAL][BLT_CLP_COMPLEX].blockfn = blkcliphs ;
  nbit_blit_sliceh[SPECIAL][BLT_CLP_COMPLEX].blockfn = nblkclip;

  blitsliceh[ONELESSWORD][BLT_CLP_NONE].blockfn =
    nbit_blit_sliceh[ONELESSWORD][BLT_CLP_NONE].blockfn = blkfillhl;
  blitsliceh[ONELESSWORD][BLT_CLP_RECT].blockfn =
    nbit_blit_sliceh[ONELESSWORD][BLT_CLP_RECT].blockfn = blkfillhl;
  blitsliceh[ONELESSWORD][BLT_CLP_COMPLEX].blockfn = blkcliphl ;
  nbit_blit_sliceh[ONELESSWORD][BLT_CLP_COMPLEX].blockfn = nblkclip;

  blitsliceh[ORTHOGONAL][BLT_CLP_NONE].blockfn =
    nbit_blit_sliceh[ORTHOGONAL][BLT_CLP_NONE].blockfn = blkfillho;
  blitsliceh[ORTHOGONAL][BLT_CLP_RECT].blockfn =
    nbit_blit_sliceh[ORTHOGONAL][BLT_CLP_RECT].blockfn = blkfillho;
  blitsliceh[ORTHOGONAL][BLT_CLP_COMPLEX].blockfn = blkclipho ;
  nbit_blit_sliceh[ORTHOGONAL][BLT_CLP_COMPLEX].blockfn = nblkclip;

  blitsliceh[GENERAL][BLT_CLP_NONE].blockfn =
    nbit_blit_sliceh[GENERAL][BLT_CLP_NONE].blockfn = blkfillhg;
  blitsliceh[GENERAL][BLT_CLP_RECT].blockfn =
    nbit_blit_sliceh[GENERAL][BLT_CLP_RECT].blockfn = blkfillhg;
  blitsliceh[GENERAL][BLT_CLP_COMPLEX].blockfn = blkcliphg ;
  nbit_blit_sliceh[GENERAL][BLT_CLP_COMPLEX].blockfn = nblkclip;

  blitsliceh[SLOWGENERAL][BLT_CLP_NONE].blockfn =
    nbit_blit_sliceh[SLOWGENERAL][BLT_CLP_NONE].blockfn = blkfillhsg;
  blitsliceh[SLOWGENERAL][BLT_CLP_RECT].blockfn =
    nbit_blit_sliceh[SLOWGENERAL][BLT_CLP_RECT].blockfn = blkfillhsg;
  blitsliceh[SLOWGENERAL][BLT_CLP_COMPLEX].blockfn = blkcliphsg ;
  nbit_blit_sliceh[SLOWGENERAL][BLT_CLP_COMPLEX].blockfn = nblkclip;
}
#endif

#ifdef BLIT_HALFTONE_1
void init_halftone1_block(surface_t *halftone1)
{
  init_halftone_sliceh() ;

  halftone1->baseblits[BLT_CLP_NONE].blockfn =
    halftone1->baseblits[BLT_CLP_RECT].blockfn = blkfillhalftone ;
  halftone1->baseblits[BLT_CLP_COMPLEX].blockfn = blkfillhalftone ;

  /* No min blits */
  halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillspan ;
  halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipspan ;
}
#endif


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
void init_halftonen_block(surface_t *halftonen)
{
  init_halftone_sliceh() ;

  halftonen->baseblits[BLT_CLP_NONE].blockfn =
    halftonen->baseblits[BLT_CLP_RECT].blockfn =
    halftonen->baseblits[BLT_CLP_COMPLEX].blockfn = nblkfillhalftone;

  /* No min blits */
  halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillspan;
  halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipspan;
}
#endif

/* Log stripped */
