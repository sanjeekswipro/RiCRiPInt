/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:halftoneblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bit blitting functions.
 */

#include "core.h"
#include "objnamer.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "render.h"   /* render_blit_t */
#include "htrender.h" /* GET_FORM */
#include "converge.h"
#include "caching.h"
#include "spanlist.h"
#include "halftoneblts.h"
#include "renderfn.h"
#include "toneblt.h" /* bitclipn */
#include "hqmemset.h"


/** Table of bitfills and bitclips for each halftone method, 1-bit */
blitclip_slice_t blitsliceh[NHALFTONETYPES] = {
  BLITCLIP_SLICE_INIT, /* SPECIAL */
  BLITCLIP_SLICE_INIT, /* ONELESSWORD */
  BLITCLIP_SLICE_INIT, /* ORTHOGONAL */
  BLITCLIP_SLICE_INIT, /* GENERAL */
  BLITCLIP_SLICE_INIT  /* SLOWGENERAL */
} ;

/** Table of bitfills and bitclips for each halftone method, n-bit */
blitclip_slice_t nbit_blit_sliceh[NHALFTONETYPES] = {
  BLITCLIP_SLICE_INIT, /* SPECIAL */
  BLITCLIP_SLICE_INIT, /* ONELESSWORD */
  BLITCLIP_SLICE_INIT, /* ORTHOGONAL */
  BLITCLIP_SLICE_INIT, /* GENERAL */
  BLITCLIP_SLICE_INIT  /* SLOWGENERAL */
};


static void areahalf(render_blit_t *rb, FORM *formptr )
{
  register dcoord scanline , scanmax ;
  render_blit_t rb_copy = *rb ;
  ht_params_t *ht_params = rb_copy.p_ri->ht_params ;
  blit_color_t *color = rb->color ;
  COLORVALUE colval ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for span") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  case blit_quantise_min:
    area1fill(formptr) ; /* Black */
    return ;
  case blit_quantise_max:
    area0fill(formptr) ; /* White */
    return ;
  default:
    HQFAIL("Should only be one color channel for halftoning") ;
    /*@fallthrough@*/
  case blit_quantise_mid:
    break ;
  }

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "areahalf called with a degenerate screen");
  colval = color->quantised.qcv[BLIT_SOLE_CHANNEL] ;
  HQASSERT(colval > 0 &&
           colval < color->quantised.htmax[BLIT_SOLE_CHANNEL],
           "Halftone area fill called with black or white") ;
  GET_FORM(colval, ht_params);
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;

  rb_copy.x_sep_position = 0 ;
  rb_copy.outputform = formptr ;
  rb_copy.ylineaddr = theFormA(*formptr) ;
  rb_copy.clipmode = BLT_CLP_NONE ;

  scanline = theFormHOff(*formptr) ;
  scanmax = scanline + theFormRH(*formptr) - 1 ;

  (*blitsliceh[ht_params->type][BLT_CLP_NONE].blockfn)(&rb_copy,
                                                       scanline, scanmax,
                                                       0, formptr->w - 1) ;
}

static void bitfillhs(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  register blit_t mask ;
  register blit_t *formptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 l ;
  blit_t temp ;
  dcoord xsbit, wbit; /* xs, width in bits */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitfillhs called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillhs" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  wbit = (xe - xs + 1) << rb->depth_shift;
  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));

  mask = theFormA(*ht_params->form)[(y + ht_params->py) & BLIT_MASK_BITS] ;
  shiftpwordall(mask, ht_params->px) ;

  xsbit &= BLIT_MASK_BITS;

  /* Partial left-span. */
  if ( xsbit ) {
    wbit = BLIT_WIDTH_BITS - xsbit - wbit;
    temp = SHIFTRIGHT( ALLONES , xsbit );

    /* Doesn't cross word border. */
    if ( wbit > 0 )
      temp &= ( SHIFTLEFT( ALLONES , wbit ));
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    wbit = -wbit;
    if ( wbit <= 0 )
      return ;
    ++formptr ;
  }
  /* Eight Middle word sets. */
  l = wbit >> BLIT_SHIFT_BITS;
  if ( l )
  {
    BlitSet(formptr, mask, l);
    formptr += l;
  }
  /* Partial right-span. */
  wbit &= BLIT_MASK_BITS;
  if ( wbit > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - wbit );
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
  }
}

static void bitfillhl(render_blit_t *rb,
                      dcoord y , dcoord xs , dcoord xe )
{
  register dcoord rotate ;
  register dcoord repeat ;
  register blit_t mask ;
  register blit_t *formptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 l ;
  blit_t temp ;
  dcoord xsbit, wbit; /* xs, width in bits */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitfillhl called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillhl" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  wbit = (xe - xs + 1) << rb->depth_shift;
  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit)) ;

  repeat = ht_params->xdims ;
  rotate = ht_params->rotate ;

  /* Get initial mask. */
  mask = theFormA(*ht_params->form)[(y + ht_params->py) % ht_params->ydims];

  /* Correct alignment of mask. */
  l = ((xsbit & ~BLIT_MASK_BITS) + ht_params->px) % ht_params->xdims;
  shiftpword( mask , l , repeat ) ;

  xsbit &= BLIT_MASK_BITS;

  /* Partial left-span. */
  if ( xsbit ) {
    wbit = BLIT_WIDTH_BITS - xsbit - wbit;
    temp = SHIFTRIGHT( ALLONES , xsbit );

    /* Doesn't cross word border. */
    if ( wbit >= 0 ) {
      temp &= ( SHIFTLEFT( ALLONES , wbit ));
      (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
      return ;
    }
    wbit = -wbit;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    ++formptr ;
    rotatepword( mask , rotate , repeat ) ;
  }
  l = wbit >> BLIT_SHIFT_BITS;
  if ( l ) {
    register int32 n ;
    for ( n = l >> 3 ; n != 0 ; n--, formptr += 8 ) {
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
    for ( n = (l & 7) ; n != 0 ; n--, formptr++ ) {
      formptr[0] = mask ; rotatepword( mask , rotate , repeat ) ;
    }
  }
  /* Partial right-span. */
  wbit &= BLIT_MASK_BITS;
  if ( wbit > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - wbit );
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
  }
}

static void bitfillho(render_blit_t *rb, register dcoord y,
                      register dcoord xs, dcoord xe)
{
  register blit_t *bformptr, *eformptr ;
  blit_t firstmask, lastmask, firstword, lastword ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  dcoord xsbit, xebit; /* xs, xe in bits */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitfillhsg called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillho" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  firstmask = SHIFTRIGHT(ALLONES, xsbit & BLIT_MASK_BITS) ;
  firstword = *bformptr & ~firstmask ;
  xsbit &= ~BLIT_MASK_BITS;

  xebit = (xe + rb->x_sep_position) << rb->depth_shift;
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit));
  lastmask = SHIFTLEFT(ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                       - (xebit & BLIT_MASK_BITS));
  lastword = *eformptr & ~lastmask ;
  xebit &= ~BLIT_MASK_BITS;

  moreonbitsptr(bformptr, ht_params, xsbit, y,
                ((xebit - xsbit) >> BLIT_SHIFT_BITS) + 1) ;

  *bformptr = firstword | (firstmask & *bformptr) ;
  *eformptr = lastword | (lastmask & *eformptr) ;
}

static void bitfillhg(render_blit_t *rb, register dcoord y,
                      register dcoord xs, dcoord xe)
{
  register blit_t *bformptr, *eformptr ;
  blit_t firstmask, lastmask, firstword, lastword ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  dcoord xsbit, xebit; /* xs, xe in bits */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitfillhsg called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillhg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  firstmask = SHIFTRIGHT(ALLONES, xsbit & BLIT_MASK_BITS) ;
  firstword = *bformptr & ~firstmask ;
  xsbit &= ~BLIT_MASK_BITS;

  xebit = (xe + rb->x_sep_position) << rb->depth_shift;
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit));
  lastmask = SHIFTLEFT(ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                       - (xebit & BLIT_MASK_BITS));
  lastword = *eformptr & ~lastmask ;
  xebit &= ~BLIT_MASK_BITS;

  moregnbitsptr(bformptr, ht_params, xsbit, y,
                ((xebit - xsbit) >> BLIT_SHIFT_BITS) + 1) ;

  *bformptr = firstword | (firstmask & *bformptr) ;
  *eformptr = lastword | (lastmask & *eformptr) ;
}

static void bitfillhsg(render_blit_t *rb, register dcoord y,
                       register dcoord xs, dcoord xe)
{
  register blit_t *bformptr, *eformptr ;
  blit_t firstmask, lastmask, firstword, lastword ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  dcoord xsbit, xebit; /* xs, xe in bits */

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitfillhsg called with a degenerate screen");
  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfillhsg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  firstmask = SHIFTRIGHT(ALLONES, xsbit & BLIT_MASK_BITS) ;
  firstword = *bformptr & ~firstmask ;
  xsbit &= ~BLIT_MASK_BITS;

  xebit = (xe + rb->x_sep_position) << rb->depth_shift;
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit));
  lastmask = SHIFTLEFT(ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                       - (xebit & BLIT_MASK_BITS));
  lastword = *eformptr & ~lastmask ;
  xebit &= ~BLIT_MASK_BITS;

  moresgnbitsptr(bformptr, ht_params, xsbit, y,
                 ((xebit - xsbit) >> BLIT_SHIFT_BITS) + 1) ;

  *bformptr = firstword | (firstmask & *bformptr) ;
  *eformptr = lastword | (lastmask & *eformptr) ;
}


#ifdef BLIT_HALFTONE_1


static void bitcliphs(render_blit_t *rb, register dcoord y,
                      register dcoord xs, register dcoord xe)
{
  register blit_t temp ;
  register blit_t mask ;
  register blit_t *formptr ;
  register blit_t *clipptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitcliphs called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitcliphs" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

/* Find left-most integer address. */
  xe = xe - xs + 1 ;
  xs += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;

  mask = theFormA(*ht_params->form)[(y + ht_params->py) & BLIT_MASK_BITS] ;
  shiftpwordall(mask, ht_params->px) ;

  xs &= BLIT_MASK_BITS ;

/* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

/* Doesn't cross word border. */
    if ( xe > 0 )
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
    temp &= (*clipptr++) ;
    temp = ((*formptr) & (~temp)) | ( temp & mask ) ;
    (*formptr) = temp ;
    xe = -xe ;
    if ( xe <= 0 )
      return ;
    ++formptr ;
  }
/* Middle word sets. */
  xs = BLIT_WIDTH_BITS ;
  while ( xe >= xs ) {
    xe -= xs ;
    temp = (*clipptr++) ;
    temp = ((*formptr) & (~temp)) | ( temp & mask ) ;
    (*formptr++) = temp ;
  }
/* Partial right-span. */
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , xs - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
  }
}

static void bitcliphl(render_blit_t *rb,
                      dcoord y , dcoord xs , dcoord xe )
{
  register int32 rotate ;
  register int32 repeat ;
  register blit_t mask ;
  register blit_t *formptr ;
  register blit_t *clipptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 l ;
  blit_t temp ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitcliphl called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitcliphl" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

/* Find left-most integer address. */
  xe = xe - xs + 1 ;
  xs += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;

  repeat = ht_params->xdims ;
  rotate = ht_params->rotate ;

/* Get initial mask. */
  mask = theFormA(*ht_params->form)[(y + ht_params->py) % ht_params->ydims];

/* Correct alignment of mask. */
  l = ((xs & ~BLIT_MASK_BITS) + ht_params->px) % ht_params->xdims ;
  shiftpword( mask , l , repeat ) ;

  xs &= BLIT_MASK_BITS ;

/* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

/* Doesn't cross word border. */
    if ( xe >= 0 ) {
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
      temp &= (*clipptr) ;
      (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
      return ;
    }
    xe = -xe ;
    temp &= (*clipptr++) ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
    ++formptr ;
    rotatepword( mask , rotate , repeat ) ;
  }
  l = xe >> BLIT_SHIFT_BITS ;
/* Eight Middle word sets. */
  if ( l ) {
    xs = (l & 7) ;
    for ( l = l >> 3 ; l != 0 ; l--, formptr += 8, clipptr += 8 ) {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[1] ;
      formptr[1] = (formptr[1] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[2] ;
      formptr[2] = (formptr[2] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[3] ;
      formptr[3] = (formptr[3] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[4] ;
      formptr[4] = (formptr[4] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[5] ;
      formptr[5] = (formptr[5] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[6] ;
      formptr[6] = (formptr[6] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
      temp = clipptr[7] ;
      formptr[7] = (formptr[7] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
    }
    for ( l = xs ; l != 0 ; l--, formptr++, clipptr++ ) {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | ( temp & mask) ;
      rotatepword(mask, rotate, repeat) ;
    }
  }
/* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & mask ) ;
  }
}

static void bitclipho(render_blit_t *rb,
                      dcoord y , dcoord xs , dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  register blit_t *clipptr ;
  register blit_t *maskptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 l ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitclipho called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipho" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  maskptr = rb->p_ri->p_rs->forms->halftonebase;
  moreonbitsptr(maskptr, ht_params, xs & ~BLIT_MASK_BITS , y,
                (((xe & ~BLIT_MASK_BITS) -
                  (xs & ~BLIT_MASK_BITS)) >> BLIT_SHIFT_BITS) + 1);

/* Find left-most integer address. */
  xe = xe - xs + 1 ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;

  xs &= BLIT_MASK_BITS ;

/* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

/* Doesn't cross word border. */
    if ( xe > 0 )
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
    temp &= (*clipptr++) ;
    temp = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
    (*formptr) = temp ;
    if ( xe >= 0 )
      return ;
    xe = -xe ;
    ++formptr ;
  }
  l = xe >> BLIT_SHIFT_BITS ;
  if ( l ) {
    xs = (l & 7) ;
    for ( l = l >> 3 ; l != 0 ; l--, formptr += 8, clipptr += 8, maskptr += 8 )
    {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | (temp & maskptr[0]) ;
      temp = clipptr[1] ;
      formptr[1] = (formptr[1] & ~temp) | (temp & maskptr[1]) ;
      temp = clipptr[2] ;
      formptr[2] = (formptr[2] & ~temp) | (temp & maskptr[2]) ;
      temp = clipptr[3] ;
      formptr[3] = (formptr[3] & ~temp) | (temp & maskptr[3]) ;
      temp = clipptr[4] ;
      formptr[4] = (formptr[4] & ~temp) | (temp & maskptr[4]) ;
      temp = clipptr[5] ;
      formptr[5] = (formptr[5] & ~temp) | (temp & maskptr[5]) ;
      temp = clipptr[6] ;
      formptr[6] = (formptr[6] & ~temp) | (temp & maskptr[6]) ;
      temp = clipptr[7] ;
      formptr[7] = (formptr[7] & ~temp) | (temp & maskptr[7]) ;
    }
    for ( l = xs ; l != 0 ; l--, formptr++, clipptr++, maskptr++ ) {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | (temp & maskptr[0]) ;
    }
  }
/* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & maskptr[ 0 ] ) ;
  }
}

static void bitcliphg(render_blit_t *rb,
                      register dcoord y , register dcoord xs , dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  register blit_t *clipptr ;
  register blit_t *maskptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 l ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitcliphg called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitcliphg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;

  if ( ! clipptr[ 0 ] ) {
    xs = ( xs + BLIT_WIDTH_BITS ) & ~BLIT_MASK_BITS ;
    ++formptr ;
    ++clipptr ;
    while (( xs < xe ) && ( ! clipptr[ 0 ] )) {
      xs += BLIT_WIDTH_BITS ;
      ++formptr ;
      ++clipptr ;
    }
    if ( xs > xe )
      return ;
  }

  l = ((xe & ~BLIT_MASK_BITS) - (xs & ~BLIT_MASK_BITS)) >> BLIT_SHIFT_BITS ;
  clipptr += l ;
  if ( ! clipptr[ 0 ] ) {
    xe = ( xe & ~BLIT_MASK_BITS) - 1 ;
    --l ;
    --clipptr ;
    while (( xs < xe ) && ( ! clipptr[ 0 ] )) {
      xe -= BLIT_WIDTH_BITS ;
      --l ;
      --clipptr ;
    }
    if ( xs > xe )
      return ;
  }
  clipptr -= l ;

  maskptr = rb->p_ri->p_rs->forms->halftonebase;
  moregnbitsptr(maskptr, ht_params, xs & ~BLIT_MASK_BITS, y, l + 1);

  xe = xe - xs + 1 ;
  xs &= BLIT_MASK_BITS ;

/* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

/* Doesn't cross word border. */
    if ( xe > 0 )
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;

    temp &= (*clipptr++) ;
    temp = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
    (*formptr) = temp ;
    if ( xe >= 0 )
      return ;
    xe = -xe ;
    ++formptr ;
  }
  l = xe >> BLIT_SHIFT_BITS ;
  if ( l ) {
    xs = (l & 7) ;
    for ( l = l >> 3 ; l != 0 ; l--, formptr += 8, clipptr += 8, maskptr += 8 )
    {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | (temp & maskptr[0]) ;
      temp = clipptr[1] ;
      formptr[1] = (formptr[1] & ~temp) | (temp & maskptr[1]) ;
      temp = clipptr[2] ;
      formptr[2] = (formptr[2] & ~temp) | (temp & maskptr[2]) ;
      temp = clipptr[3] ;
      formptr[3] = (formptr[3] & ~temp) | (temp & maskptr[3]) ;
      temp = clipptr[4] ;
      formptr[4] = (formptr[4] & ~temp) | (temp & maskptr[4]) ;
      temp = clipptr[5] ;
      formptr[5] = (formptr[5] & ~temp) | (temp & maskptr[5]) ;
      temp = clipptr[6] ;
      formptr[6] = (formptr[6] & ~temp) | (temp & maskptr[6]) ;
      temp = clipptr[7] ;
      formptr[7] = (formptr[7] & ~temp) | (temp & maskptr[7]) ;
    }
    for ( l = xs ; l != 0 ; l--, formptr++, clipptr++, maskptr++ ) {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | (temp & maskptr[0]) ;
    }
  }
/* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & maskptr[ 0 ] ) ;
  }
}

static void bitcliphsg(render_blit_t *rb,
                       register dcoord y , register dcoord xs , dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  register blit_t *clipptr ;
  register blit_t *maskptr ;
  ht_params_t *ht_params = rb->p_ri->ht_params ;
  int32 l ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "bitcliphsg called with a degenerate screen");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitcliphsg" ) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(ht_params->form->type == FORMTYPE_HALFTONEBITMAP,
           "Halftone form is not bitmap") ;

  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;

  if ( ! clipptr[ 0 ] ) {
    xs = ( xs + BLIT_WIDTH_BITS ) & ~BLIT_MASK_BITS ;
    ++formptr ;
    ++clipptr ;
    while (( xs < xe ) && ( ! clipptr[ 0 ] )) {
      xs += BLIT_WIDTH_BITS ;
      ++formptr ;
      ++clipptr ;
    }
    if ( xs > xe )
      return ;
  }

  l = ((xe & ~BLIT_MASK_BITS) - (xs & ~BLIT_MASK_BITS)) >> BLIT_SHIFT_BITS ;
  clipptr += l ;
  if ( ! clipptr[ 0 ] ) {
    xe = ( xe & ~BLIT_MASK_BITS) - 1 ;
    --l ;
    --clipptr ;
    while (( xs < xe ) && ( ! clipptr[ 0 ] )) {
      xe -= BLIT_WIDTH_BITS ;
      --l ;
      --clipptr ;
    }
    if ( xs > xe )
      return ;
  }
  clipptr -= l ;

  maskptr = rb->p_ri->p_rs->forms->halftonebase;
  moresgnbitsptr(maskptr, ht_params, xs & ~BLIT_MASK_BITS, y, l + 1);

  xe = xe - xs + 1 ;
  xs &= BLIT_MASK_BITS ;

/* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

/* Doesn't cross word border. */
    if ( xe > 0 )
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;

    temp &= (*clipptr++) ;
    temp = ((*formptr) & (~temp)) | ( temp & (*maskptr++)) ;
    (*formptr) = temp ;
    if ( xe >= 0 )
      return ;
    xe = -xe ;
    ++formptr ;
  }
  l = xe >> BLIT_SHIFT_BITS ;
  if ( l ) {
    xs = (l & 7) ;
    for ( l = l >> 3 ; l != 0 ; l--, formptr += 8, clipptr += 8, maskptr += 8 )
    {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | (temp & maskptr[0]) ;
      temp = clipptr[1] ;
      formptr[1] = (formptr[1] & ~temp) | (temp & maskptr[1]) ;
      temp = clipptr[2] ;
      formptr[2] = (formptr[2] & ~temp) | (temp & maskptr[2]) ;
      temp = clipptr[3] ;
      formptr[3] = (formptr[3] & ~temp) | (temp & maskptr[3]) ;
      temp = clipptr[4] ;
      formptr[4] = (formptr[4] & ~temp) | (temp & maskptr[4]) ;
      temp = clipptr[5] ;
      formptr[5] = (formptr[5] & ~temp) | (temp & maskptr[5]) ;
      temp = clipptr[6] ;
      formptr[6] = (formptr[6] & ~temp) | (temp & maskptr[6]) ;
      temp = clipptr[7] ;
      formptr[7] = (formptr[7] & ~temp) | (temp & maskptr[7]) ;
    }
    for ( l = xs ; l != 0 ; l--, formptr++, clipptr++, maskptr++ ) {
      temp = clipptr[0] ;
      formptr[0] = (formptr[0] & ~temp) | (temp & maskptr[0]) ;
    }
  }
/* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) = ((*formptr) & (~temp)) | ( temp & maskptr[ 0 ] ) ;
  }
}


#endif /* BLIT_HALFTONE_1 for clip fns */


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
static void nbitclip(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  /* Cannot dest &= clip, use generic clipping */
  ht_params_t *ht_params = rb->p_ri->ht_params ;

  HQASSERT(!HT_PARAMS_DEGENERATE(ht_params),
           "nbitclip called with a degenerate screen");
  bitclipn(rb, y, xs, xe, blitsliceh[ht_params->type][BLT_CLP_NONE].spanfn);
}
#endif


#ifdef BLIT_HALFTONE_1
/** Self-modifying blits for 1-bit halftone span fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void bitfillhalftone(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

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

  (*slice->spanfn)(rb, y, xs, xe) ;
}
#endif


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
/** Self-modifying blits for n-bit halftone span fns. */
static void nbitfillhalftone(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for span") ;
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

  (*slice->spanfn)(rb, y, xs, xe) ;
}
#endif

/* ---------------------------------------------------------------------- */

/* Halftone maxblt functions. */

/** \fn bitfillOR \fn bitclipOR
   Definition of the bitfill OR function. */
#ifndef DOXYGEN_SKIP
#define OPNAME OR
#define ASSIGNOP |=
#include "halftonebltop.h"
#endif /* !DOXYGEN_SKIP */

blitclip_slice_t ht_or_slice = {
  { bitfillOR, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitfillOR, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitclipOR, blkclipspan, invalid_snfill, charbltn, imagebltht },
} ;

/** \fn bitfillAND \fn bitclipAND
   Definition of the bitfill AND function. */
#ifndef DOXYGEN_SKIP
#define OPNAME AND
#define ASSIGNOP &=
#include "halftonebltop.h"
#endif /* !DOXYGEN_SKIP */

blitclip_slice_t ht_and_slice = {
  { bitfillAND, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitfillAND, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitclipAND, blkclipspan, invalid_snfill, charbltn, imagebltht },
} ;

/** \fn bitfillXOR \fn bitclipXOR
   Definition of the bitfill XOR function. */
#ifndef DOXYGEN_SKIP
#define OPNAME XOR
#define ASSIGNOP ^=
#include "halftonebltop.h"
#endif /* !DOXYGEN_SKIP */

blitclip_slice_t ht_xor_slice = {
  { bitfillXOR, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitfillXOR, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitclipXOR, blkclipspan, invalid_snfill, charbltn, imagebltht },
} ;

/** \fn bitfillXORNOT \fn bitclipXORNOT
   Definition of the bitfill XOR NOT function. */
#ifndef DOXYGEN_SKIP
#define OPNAME XORNOT
#define ASSIGNOP ^=
#define NOTOP ~
#include "halftonebltop.h"
#endif /* !DOXYGEN_SKIP */

blitclip_slice_t ht_xornot_slice = {
  { bitfillXORNOT, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitfillXORNOT, blkfillspan, invalid_snfill, charbltn, imagebltht },
  { bitclipXORNOT, blkclipspan, invalid_snfill, charbltn, imagebltht },
} ;

#if defined(BLIT_HALFTONE_1) || defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
static void init_halftone_sliceh(void)
{
  /* Set up both blitsliceh and nbit_blit_sliceh. The n-bit versions can also
     be used for 1-bit, but are a bit less optimised in the complex clip
     case. They pass the spans through RLE or extracted bitmap clip spans
     before calling the base blit function. */
  blitsliceh[SPECIAL][BLT_CLP_NONE].spanfn =
    nbit_blit_sliceh[SPECIAL][BLT_CLP_NONE].spanfn = bitfillhs;
  blitsliceh[SPECIAL][BLT_CLP_RECT].spanfn =
    nbit_blit_sliceh[SPECIAL][BLT_CLP_RECT].spanfn = bitfillhs;
  blitsliceh[SPECIAL][BLT_CLP_COMPLEX].spanfn = bitcliphs ;
  nbit_blit_sliceh[SPECIAL][BLT_CLP_COMPLEX].spanfn = nbitclip;

  blitsliceh[ONELESSWORD][BLT_CLP_NONE].spanfn =
    nbit_blit_sliceh[ONELESSWORD][BLT_CLP_NONE].spanfn = bitfillhl;
  blitsliceh[ONELESSWORD][BLT_CLP_RECT].spanfn =
    nbit_blit_sliceh[ONELESSWORD][BLT_CLP_RECT].spanfn = bitfillhl;
  blitsliceh[ONELESSWORD][BLT_CLP_COMPLEX].spanfn = bitcliphl ;
  nbit_blit_sliceh[ONELESSWORD][BLT_CLP_COMPLEX].spanfn = nbitclip;

  blitsliceh[ORTHOGONAL][BLT_CLP_NONE].spanfn =
    nbit_blit_sliceh[ORTHOGONAL][BLT_CLP_NONE].spanfn = bitfillho ;
  blitsliceh[ORTHOGONAL][BLT_CLP_RECT].spanfn =
    nbit_blit_sliceh[ORTHOGONAL][BLT_CLP_RECT].spanfn = bitfillho ;
  blitsliceh[ORTHOGONAL][BLT_CLP_COMPLEX].spanfn = bitclipho ;
  nbit_blit_sliceh[ORTHOGONAL][BLT_CLP_COMPLEX].spanfn = nbitclip;

  blitsliceh[GENERAL][BLT_CLP_NONE].spanfn =
    nbit_blit_sliceh[GENERAL][BLT_CLP_NONE].spanfn = bitfillhg ;
  blitsliceh[GENERAL][BLT_CLP_RECT].spanfn =
    nbit_blit_sliceh[GENERAL][BLT_CLP_RECT].spanfn = bitfillhg ;
  blitsliceh[GENERAL][BLT_CLP_COMPLEX].spanfn = bitcliphg ;
  nbit_blit_sliceh[GENERAL][BLT_CLP_COMPLEX].spanfn = nbitclip;

  blitsliceh[SLOWGENERAL][BLT_CLP_NONE].spanfn =
    nbit_blit_sliceh[SLOWGENERAL][BLT_CLP_NONE].spanfn = bitfillhsg ;
  blitsliceh[SLOWGENERAL][BLT_CLP_RECT].spanfn =
    nbit_blit_sliceh[SLOWGENERAL][BLT_CLP_RECT].spanfn = bitfillhsg ;
  blitsliceh[SLOWGENERAL][BLT_CLP_COMPLEX].spanfn = bitcliphsg ;
  nbit_blit_sliceh[SLOWGENERAL][BLT_CLP_COMPLEX].spanfn = nbitclip;
}
#endif

#ifdef BLIT_HALFTONE_1
void init_halftone1_span(surface_t *halftone1)
{
  init_halftone_sliceh() ;

  halftone1->baseblits[BLT_CLP_NONE].spanfn =
    halftone1->baseblits[BLT_CLP_RECT].spanfn =
    halftone1->baseblits[BLT_CLP_COMPLEX].spanfn = bitfillhalftone ;

  /* No min blits */
  halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn =
    halftone1->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = bitfillOR ;

  halftone1->areafill = areahalf ;
}
#endif


#if defined(BLIT_HALFTONE_2) || defined(BLIT_HALFTONE_4)
void init_halftonen_span(surface_t *halftonen)
{
  init_halftone_sliceh() ;

  halftonen->baseblits[BLT_CLP_NONE].spanfn =
    halftonen->baseblits[BLT_CLP_RECT].spanfn =
    halftonen->baseblits[BLT_CLP_COMPLEX].spanfn = nbitfillhalftone;

  /* No min blits */
  halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn =
    halftonen->maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = bitfillOR;

  halftonen->areafill = areahalf;
}
#endif

/* Log stripped */
