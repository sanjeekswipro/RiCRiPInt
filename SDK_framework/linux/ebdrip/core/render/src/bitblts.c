/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:bitblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bit blitting functions.
 */

#include "core.h"

#include "bitblts.h"
#include "blttables.h"
#include "bitblth.h"
#include "clipblts.h"
#include "render.h"   /* render_blit_t */
#include "caching.h"
#include "spanlist.h"
#include "toneblt.h" /* bitclipn */
#include "hqmemset.h"
#include "hqmemcpy.h"


void invalid_span(render_blit_t *rb,
                  dcoord y, dcoord xs, dcoord xe)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(dcoord, y) ;
  UNUSED_PARAM(dcoord, xs) ;
  UNUSED_PARAM(dcoord, xe) ;

  HQFAIL("This function should never be called") ;
}

void next_span(render_blit_t *rb,
               dcoord y, dcoord xs, dcoord xe)
{
  DO_SPAN(rb, y, xs, xe) ;
}

void ignore_span(render_blit_t *rb,
                 dcoord y, dcoord xs, dcoord xe)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(dcoord, y) ;
  UNUSED_PARAM(dcoord, xs) ;
  UNUSED_PARAM(dcoord, xe) ;
}

#if defined( ASSERT_BUILD )
Bool assert_blts = TRUE ;
#endif

/**
 * Set all the bits on scanline 'y' from position 'xs' to 'xe' with the
 * value 1.
 */
void bitfill1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  register blit_t temp ;
  register blit_t *formptr ;
  dcoord xsbit, wbit; /* xs, width in bits */

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill1" ) ;
  HQASSERT(rb->depth_shift < DEPTH_SHIFT_LIMIT, "Invalid bit depth");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;

  /* Find left-most integer address. */
  wbit = (xe - xs + 1) << rb->depth_shift;
  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  xsbit &= BLIT_MASK_BITS;

  /* Partial left-span. */
  if ( xsbit ) {
    wbit = BLIT_WIDTH_BITS - xsbit - wbit;
    temp = SHIFTRIGHT( ALLONES, xsbit );
    /* Doesn't cross word border. */
    if ( wbit >= 0 ) {
      temp &= SHIFTLEFT( ALLONES, wbit );
      (*formptr) |= temp ;
      return ;
    }
    (*formptr++) |= temp ;
    wbit = -wbit;
  }
  temp = ALLONES ;
  xsbit = wbit >> BLIT_SHIFT_BITS;
  if ( xsbit )
  {
    BlitSet(formptr, ALLONES, xsbit);
    formptr += xsbit;
  }
  /* Partial right-span. */
  wbit &= BLIT_MASK_BITS;
  if ( wbit > 0 ) {
    temp = SHIFTLEFT( temp, BLIT_WIDTH_BITS - wbit );
    (*formptr) |= temp ;
  }
}

/**
 * Set all the bits on scanline 'y' from position 'xs' to 'xe' with the
 * value 0.
 */
void bitfill0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  register blit_t temp ;
  register blit_t *formptr ;
  dcoord xsbit, wbit; /* xs, width in bits */

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "bitfill0" ) ;
  HQASSERT(rb->depth_shift < DEPTH_SHIFT_LIMIT, "Invalid bit depth");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;

  /* Find left-most integer address. */
  wbit = (xe - xs + 1) << rb->depth_shift;
  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  xsbit &= BLIT_MASK_BITS;

  /* Partial left-span. */
  if ( xsbit ) {
    wbit = BLIT_WIDTH_BITS - xsbit - wbit;
    temp = SHIFTRIGHT( ALLONES, xsbit );
    /* Doesn't cross word border. */
    if ( wbit >= 0 ) {
      temp &= SHIFTLEFT( ALLONES, wbit );
      (*formptr) &= (~temp) ;
      return ;
    }
    (*formptr++) &= (~temp) ;
    wbit = -wbit;
  }
  xsbit = wbit >> BLIT_SHIFT_BITS;
  if ( xsbit )
  {
    BlitSet(formptr, 0, xsbit);
    formptr += xsbit;
  }
  /* Partial right-span. */
  wbit &= BLIT_MASK_BITS;
  if ( wbit > 0 ) {
    temp = SHIFTRIGHT( ALLONES, wbit );
    (*formptr) &= temp ;
  }
}


static void nbitclip1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "nbitclip1");

  bitclipn(rb, y, xs, xe, bitfill1);
}


void bitclip1(render_blit_t *rb,
              dcoord y, register dcoord xs, dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  register blit_t *clipptr ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip1" ) ;
  UNUSED_PARAM(dcoord, y); /* only in assert */
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

  /* Find left-most integer address. */
  xe = xe - xs + 1 ;
  xs += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs));
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs));
  xs = xs & BLIT_MASK_BITS ;

  /* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

    /* Doesn't cross word border. */
    if ( xe >= 0 ) {
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
      temp &= (*clipptr) ;
      (*formptr) |= temp ;
      return ;
    }
    temp &= (*clipptr++) ;
    (*formptr++) |= temp ;
    xe = -xe ;
  }
  xs = xe >> BLIT_SHIFT_BITS ;
  if ( xs ) {
    register int32 n ;
    for ( n = xs >> 3 ; n != 0 ; n--, formptr += 8, clipptr += 8 ) {
      formptr[0] |= clipptr[0] ;
      formptr[1] |= clipptr[1] ;
      formptr[2] |= clipptr[2] ;
      formptr[3] |= clipptr[3] ;
      formptr[4] |= clipptr[4] ;
      formptr[5] |= clipptr[5] ;
      formptr[6] |= clipptr[6] ;
      formptr[7] |= clipptr[7] ;
    }
    xs &= 7 ;
    switch ( xs ) {
    case 7 : formptr[6] |= clipptr[6] ;
    case 6 : formptr[5] |= clipptr[5] ;
    case 5 : formptr[4] |= clipptr[4] ;
    case 4 : formptr[3] |= clipptr[3] ;
    case 3 : formptr[2] |= clipptr[2] ;
    case 2 : formptr[1] |= clipptr[1] ;
    case 1 : formptr[0] |= clipptr[0] ;
      formptr += xs ;
      clipptr += xs ;
    }
  }
  /* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) |= temp ;
  }
}


static void nbitclip0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "nbitclip0");

  bitclipn(rb, y, xs, xe, bitfill0);
}


void bitclip0(render_blit_t *rb,
              dcoord y, register dcoord xs, dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  register blit_t *clipptr ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclip0" ) ;
  UNUSED_PARAM(dcoord, y); /* only in assert */
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

  /* Find left-most integer address. */
  xe = xe - xs + 1 ;
  xs += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs));
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs));
  xs &= BLIT_MASK_BITS ;

  /* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

    /* Doesn't cross word border. */
    if ( xe >= 0 ) {
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
      temp &= (*clipptr) ;
      (*formptr) &= (~temp ) ;
      return ;
    }
    temp &= (*clipptr++) ;
    (*formptr++) &= (~temp) ;
    xe = -xe ;
  }
  /* Eight Middle word sets. */
  xs = xe >> BLIT_SHIFT_BITS ;
  if ( xs ) {
    register int32 n ;
    for ( n = xs >> 3 ; n != 0 ; n--, formptr += 8, clipptr += 8 ) {
      formptr[0] &= ~clipptr[0] ;
      formptr[1] &= ~clipptr[1] ;
      formptr[2] &= ~clipptr[2] ;
      formptr[3] &= ~clipptr[3] ;
      formptr[4] &= ~clipptr[4] ;
      formptr[5] &= ~clipptr[5] ;
      formptr[6] &= ~clipptr[6] ;
      formptr[7] &= ~clipptr[7] ;
    }
    xs &= 7 ;
    switch ( xs ) {
    case 7 : formptr[6] &= ~clipptr[6] ;
    case 6 : formptr[5] &= ~clipptr[5] ;
    case 5 : formptr[4] &= ~clipptr[4] ;
    case 4 : formptr[3] &= ~clipptr[3] ;
    case 3 : formptr[2] &= ~clipptr[2] ;
    case 2 : formptr[1] &= ~clipptr[1] ;
    case 1 : formptr[0] &= ~clipptr[0] ;
      formptr += xs ;
      clipptr += xs ;
    }
  }
  /* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= (*clipptr) ;
    (*formptr) &= (~temp ) ;
  }
}

void spanclip1(render_blit_t *rb,
               register dcoord y , register dcoord xs , dcoord xe )
{
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDRLEENCODED,
           "Clip form is not RLE") ;

  spanlist_intersecting((spanlist_t*)rb->ymaskaddr, bitfill1, NULL,
                        rb, y, xs, xe, 0 /* raw spans */) ;
}

void copyform(const FORM *fromform, FORM *toform)
{
  blit_t *fromword;
  blit_t *toword;
  int32 n ;

  HQASSERT(fromform, "No source form in copyform") ;
  HQASSERT(toform, "No destination form in copyform") ;
  HQASSERT(theFormS(*fromform) == theFormS(*toform),
           "Form sizes different in copyform") ;
  HQASSERT(theFormS(*fromform) >= 0, "Form sizes negative in copyform") ;

  toword = theFormA(*toform) ;
  fromword = theFormA(*fromform) ;

  if ( theFormT(*fromform) == FORMTYPE_BANDRLEENCODED ) {
    register int32 tolen = theFormL(*toform) ;
    register int32 fromlen = theFormL(*fromform) ;
    Bool tobitmap = (theFormT(*toform) != FORMTYPE_BANDRLEENCODED) ;

    theFormT(*toform) = FORMTYPE_BANDRLEENCODED ;

    for ( n = theFormRH(*fromform) ; n > 0 ; --n ) {
      if ( (tobitmap && spanlist_init((spanlist_t *)toword, tolen) == NULL) ||
           !spanlist_copy((spanlist_t *)toword, (spanlist_t *)fromword) )
        HQFAIL("Destination form not big enough for spans") ;
      toword = BLIT_ADDRESS(toword, tolen) ;
      fromword = BLIT_ADDRESS(fromword, fromlen) ;
    }
    return ;
  }

  HQASSERT(theFormT(*fromform) == FORMTYPE_BANDBITMAP ||
           theFormT(*fromform) == FORMTYPE_HALFTONEBITMAP,
           "Form is neither RLE nor bitmap") ;
  theFormT(*toform) = theFormT(*fromform) ;
  HqMemCpy(toword, fromword, theFormS(*toform));
}

/**
 * Set all the pixels in the given form to the value 0
 */
void area0fill(FORM *formptr)
{
  register blit_t *wordptr;
  int32 totalwords;

  HQASSERT(formptr, "No formptr in area0fill") ;
  HQASSERT(formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP ||
           formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_HALFTONEBITMAP,
           "Form is not bitmap") ;

  totalwords = theFormS(*formptr) ;
  HQASSERT(totalwords >= 0, "Form size negative in area0fill") ;
  totalwords >>= BLIT_SHIFT_BYTES ;

  wordptr = theFormA(*formptr) ;
  HQASSERT(wordptr, "No wordptr in area0fill") ;

  BlitSet(wordptr, 0, totalwords);
}

/**
 * Set all the pixels in the given form to the value 1
 */
void area1fill(FORM *formptr)
{
  register blit_t *wordptr;
  int32 totalwords;

  HQASSERT(formptr, "No formptr in area1fill") ;
  HQASSERT(formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP ||
           formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_HALFTONEBITMAP,
           "Form is not bitmap") ;

  totalwords = theFormS(*formptr) ;
  HQASSERT(totalwords >= 0, "Form size negative in area1fill") ;
  totalwords >>= BLIT_SHIFT_BYTES ;

  wordptr = theFormA(*formptr) ;
  HQASSERT(wordptr, "No wordptr in area1fill") ;

  BlitSet(wordptr, ALLONES, totalwords);
}

void invalid_area(render_blit_t *rb, FORM *formptr)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(FORM *, formptr) ;

  HQFAIL("invalid area function should never be called") ;
}


/* Invalid SNFILL and image routines */
void invalid_snfill(render_blit_t *rb, struct NFILLOBJECT *nfill,
                    dcoord ys, dcoord ye)
{
  UNUSED_PARAM(render_blit_t *, rb) ;
  UNUSED_PARAM(struct NFILLOBJECT *, nfill) ;
  UNUSED_PARAM(dcoord, ys) ;
  UNUSED_PARAM(dcoord, ye) ;
  HQFAIL("Invalid snfill function should never be called") ;
}

void next_snfill(render_blit_t *rb, struct NFILLOBJECT *nfill,
                 dcoord ys, dcoord ye)
{
  DO_SNFILL(rb, nfill, ys, ye) ;
}

void ignore_snfill(render_blit_t *rb, struct NFILLOBJECT *nfill,
                   dcoord ys, dcoord ye)
{
  UNUSED_PARAM(render_blit_t *, rb) ;
  UNUSED_PARAM(struct NFILLOBJECT *, nfill) ;
  UNUSED_PARAM(dcoord, ys) ;
  UNUSED_PARAM(dcoord, ye) ;
}

void invalid_imgblt(render_blit_t *rb, imgblt_params_t *params,
                    imgblt_callback_fn *callback, Bool *result)
{
  UNUSED_PARAM(render_blit_t *, rb) ;
  UNUSED_PARAM(imgblt_params_t *, params) ;
  UNUSED_PARAM(imgblt_callback_fn *, callback) ;
  UNUSED_PARAM(Bool *, result) ;
  HQFAIL("Invalid image blit function should never be called") ;
}

void next_imgblt(render_blit_t *rb, imgblt_params_t *params,
                 imgblt_callback_fn *callback, Bool *result)
{
  DO_IMG(rb, params, callback, result) ;
}

void ignore_imgblt(render_blit_t *rb, imgblt_params_t *params,
                   imgblt_callback_fn *callback, Bool *result)
{
  UNUSED_PARAM(render_blit_t *, rb) ;
  UNUSED_PARAM(imgblt_params_t *, params) ;
  UNUSED_PARAM(imgblt_callback_fn *, callback) ;
  UNUSED_PARAM(Bool *, result) ;
}

blit_slice_t invalid_slice = {
  invalid_span, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
} ;

blit_slice_t next_slice = {
  next_span, next_block, next_snfill, next_char, next_imgblt
} ;

blit_slice_t ignore_slice = {
  ignore_span, ignore_block, ignore_snfill, ignore_char, ignore_imgblt
} ;


void init_mask_span(void)
{
  blitslice0[BLT_CLP_NONE].spanfn =
    blitslice0[BLT_CLP_RECT].spanfn = bitfill0 ;
  blitslice0[BLT_CLP_COMPLEX].spanfn = bitclip0 ;

  blitslice1[BLT_CLP_NONE].spanfn =
    blitslice1[BLT_CLP_RECT].spanfn = bitfill1 ;
  blitslice1[BLT_CLP_COMPLEX].spanfn = bitclip1 ;

  nbit_blit_slice0[BLT_CLP_NONE].spanfn =
    nbit_blit_slice0[BLT_CLP_RECT].spanfn = bitfill0;
  nbit_blit_slice0[BLT_CLP_COMPLEX].spanfn = nbitclip0;

  nbit_blit_slice1[BLT_CLP_NONE].spanfn =
    nbit_blit_slice1[BLT_CLP_RECT].spanfn = bitfill1;
  nbit_blit_slice1[BLT_CLP_COMPLEX].spanfn = nbitclip1;
}

void init_C_globals_bitblts(void)
{
#if defined( ASSERT_BUILD )
  assert_blts = TRUE ;
#endif
}

/* Log stripped */
