/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:blkblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Block blit functions.
 */

#include "core.h"

#include "bitblts.h"
#include "clipblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "spanlist.h"
#include "toneblt.h" /* blkclipn */
#include "render.h"  /* render_blit_t */
#include "caching.h"
#include "hqmemset.h"


void invalid_block(render_blit_t *rb,
                   dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(dcoord, ys) ;
  UNUSED_PARAM(dcoord, ye) ;
  UNUSED_PARAM(dcoord, xs) ;
  UNUSED_PARAM(dcoord, xe) ;

  HQFAIL("This function should never be called") ;
}

void next_block(render_blit_t *rb,
                dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  DO_BLOCK(rb, ys, ye, xs, xe) ;
}

void ignore_block(render_blit_t *rb,
                   dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(dcoord, ys) ;
  UNUSED_PARAM(dcoord, ye) ;
  UNUSED_PARAM(dcoord, xs) ;
  UNUSED_PARAM(dcoord, xe) ;
}

/**
 * Fill a  block of memory with the pixel value 1.
 *
 * These functions do all the low level bitblts in the interpreter.
 * All the various castings that are done are necessary & sufficient,
 * if left out, the compiler shifts things about wrongly.
 */
void blkfill1(render_blit_t *rb, dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  dcoord xsbit, xebit, ww; /* xs, xe in bits, width in words */

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill1" ) ;
  HQASSERT(rb->depth_shift < DEPTH_SHIFT_LIMIT, "Invalid bit depth");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;

  /* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1 ;
  /* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES, xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));

  /* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    /* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    do {
      bformptr[ 0 ] |= firstmask ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    do {
      bformptr[ 0 ] |= firstmask ;
      bformptr[ 1 ] |= lastmask ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  /* General case. */
  xsbit = (xsbit >> BLIT_SHIFT_BITS) + 1; /* first whole word */
  xebit >>= BLIT_SHIFT_BITS; /* last whole word + 1 */
  ww = xebit - xsbit;
  HQASSERT( ww >= 1, "Too few words in blkfill1 general case") ;
  do {
    formptr = bformptr ;
    (*formptr++) |= firstmask ;
    BlitSet(formptr, ALLONES, ww);
    formptr += ww;
    *formptr |= lastmask ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate) ;
    ++ys ;
  } while ( ys <= ye );
}

/**
 * Fill a  block of memory with the pixel value 0.
 *
 */
void blkfill0(render_blit_t *rb, dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *formptr , *eformptr , *bformptr ;
  dcoord xsbit, xebit, ww; /* xs, xe in bits, width in words */

  BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfill0" ) ;
  HQASSERT(rb->depth_shift < DEPTH_SHIFT_LIMIT, "Invalid bit depth");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;

  xsbit = (xs + rb->x_sep_position) << rb->depth_shift;
  xebit = (xe + rb->x_sep_position) << rb->depth_shift;

  wupdate = theFormL(*rb->outputform) ;

  /* Find left-most integer address. */
  bformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xsbit));
  eformptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xebit)) - 1;
  /* Calculate the left & right mask .*/
  firstmask = SHIFTRIGHT( ALLONES, xsbit & BLIT_MASK_BITS );
  lastmask = SHIFTLEFT( ALLONES, BLIT_WIDTH_BITS - (1 << rb->depth_shift)
                                 - (xebit & BLIT_MASK_BITS));

  /* Check if only one or two blit_t spans. */
  if ( eformptr < bformptr ) {
    /* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    firstmask = ~firstmask ;
    do {
      bformptr[ 0 ] &= firstmask ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  firstmask = ~firstmask ;
  lastmask = ~lastmask ;
  if ( bformptr == eformptr ) {
    do {
      bformptr[ 0 ] &= firstmask ;
      bformptr[ 1 ] &= lastmask ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  /* General case. */
  xsbit = (xsbit >> BLIT_SHIFT_BITS) + 1; /* first whole word */
  xebit >>= BLIT_SHIFT_BITS; /* last whole word + 1 */
  ww = xebit - xsbit;
  HQASSERT( ww >= 1, "Too few words in blkfill0 general case") ;
  do
  {
    formptr = bformptr ;
    (*formptr++) &= firstmask ;
    BlitSet(formptr, 0, ww);
    formptr += ww;
    (*formptr) &= lastmask ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate) ;
    ++ys ;
  } while ( ys <= ye ) ;
}


static void nblkclip1(render_blit_t *rb,
                      dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "nblkclip1");

  blkclipn(rb, ys, ye, xs, xe, bitfill1);
}


void blkclip1(render_blit_t *rb,
              dcoord ys , dcoord ye , register dcoord xs , register dcoord xe )
{
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *clipptr , *bclipptr ;
  register blit_t *formptr , *eformptr , *bformptr ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkclip1" ) ;
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

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

  /* Check if only one or two spans. */
  if ( eformptr < bformptr ) {
    /* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    do {
      bformptr[ 0 ] |= ( firstmask & bclipptr[ 0 ]) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    do {
      bformptr[ 0 ] |= ( firstmask & bclipptr[ 0 ]) ;
      bformptr[ 1 ] |= ( lastmask  & bclipptr[ 1 ]) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate) ;
     ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  do {
    formptr = bformptr ;
    clipptr = bclipptr ;
    (*formptr++) |= ( firstmask & (*clipptr++)) ;
    while ( formptr <= eformptr )
      (*formptr++) |= (*clipptr++) ;
    (*formptr) |= ( lastmask & (*clipptr)) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate) ;
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate) ;
    ++ys ;
  } while ( ys <= ye ) ;
}


static void nblkclip0(render_blit_t *rb,
                      dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "nblkclip0");

  blkclipn(rb, ys, ye, xs, xe, bitfill0);
}


void blkclip0(render_blit_t *rb,
              dcoord ys , dcoord ye , register dcoord xs , register dcoord xe )
{
  register int32 wupdate ;
  register blit_t firstmask , lastmask ;
  register blit_t *clipptr , *bclipptr ;
  register blit_t *formptr , *eformptr , *bformptr ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkclip0" ) ;
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

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

  /* Check if only one or two spans. */
  if ( eformptr < bformptr ) {
    /* And together firstmask & lastmask. */
    firstmask &= lastmask ;
    do {
      bformptr[ 0 ] &= (~( firstmask & bclipptr[ 0 ])) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  if ( bformptr == eformptr ) {
    do {
      bformptr[ 0 ] &= (~( firstmask & bclipptr[ 0 ])) ;
      bformptr[ 1 ] &= (~( lastmask  & bclipptr[ 1 ])) ;
      bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
      bclipptr = BLIT_ADDRESS(bclipptr, wupdate) ;
      ++ys ;
    } while ( ys <= ye ) ;
    return ;
  }
  do {
    formptr = bformptr ;
    clipptr = bclipptr ;
    (*formptr++) &= (~( firstmask & (*clipptr++))) ;
    while ( formptr <= eformptr )
      (*formptr++) &= (~(*clipptr++)) ;
    (*formptr) &= (~( lastmask & (*clipptr))) ;
    bformptr = BLIT_ADDRESS(bformptr, wupdate) ;
    eformptr = BLIT_ADDRESS(eformptr, wupdate) ;
    bclipptr = BLIT_ADDRESS(bclipptr, wupdate) ;
    ++ys ;
  } while ( ys <= ye ) ;
  return ;
}

/**
 * blkfillspan and blkclipspan are generic block functions which implement
 * the block as a simple call to their span functions. The difference between
 * them is whether they save and update the clipping mask address. There is a
 * similar function blkfillrlespan, which can be used for RLE blocks, where
 * the mask and line addresses do not need updating.
 */
void blkfillspan(render_blit_t *rb, dcoord ys, dcoord ye, register dcoord xs,
                 register dcoord xe)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate ;

  wupdate = theFormL(*rb_copy.outputform);

  /* Can't use this assert, because this can be layered above pattern
     replication. Rely on underlying span asserts.

     BITBLT_ASSERT(rb, xs, xe, ys, ye, "blkfillspan" ) ;
     */

  HQASSERT(rb_copy.ylineaddr == BLIT_ADDRESS(theFormA(*rb_copy.outputform),
    wupdate * ( ys - theFormHOff(*rb_copy.outputform) -
    rb_copy.y_sep_position)), "ylineaddr bad!" );

  do {
    DO_SPAN(&rb_copy, ys, xs, xe );
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
  } while ( ++ys <= ye ) ;
}

void blkclipspan(render_blit_t *rb, dcoord ys, dcoord ye,
                 register dcoord xs , register dcoord xe )
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = theFormL(*rb_copy.outputform);
  register int32 wclipupdate = theFormL(*rb_copy.clipform);

  /* Can't use this assert, because this can be layered above pattern
     replication. Rely on underlying span asserts.

     BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkclipspan" ) ;
     */

  HQASSERT(rb_copy.ylineaddr == BLIT_ADDRESS(theFormA(*rb_copy.outputform),
    wupdate * (ys - theFormHOff(*rb_copy.outputform) -
    rb_copy.y_sep_position)), "ylineaddr bad!");
  HQASSERT(rb_copy.ymaskaddr == BLIT_ADDRESS(theFormA(*rb_copy.clipform),
    wclipupdate * (ys - theFormHOff(*rb_copy.clipform) -
    rb_copy.y_sep_position )), "ymaskaddr bad!");

  do {
    DO_SPAN(&rb_copy, ys, xs, xe );
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
    rb_copy.ymaskaddr = BLIT_ADDRESS(rb_copy.ymaskaddr, wclipupdate);
  } while ( ++ys <= ye ) ;
}

/* ---------------------------------------------------------------------- */

void init_mask_block(void)
{
  blitslice0[BLT_CLP_NONE].blockfn = blkfill0 ;
  blitslice0[BLT_CLP_RECT].blockfn = blkfill0 ;
  blitslice0[BLT_CLP_COMPLEX].blockfn = blkclip0 ;

  blitslice1[BLT_CLP_NONE].blockfn = blkfill1 ;
  blitslice1[BLT_CLP_RECT].blockfn = blkfill1 ;
  blitslice1[BLT_CLP_COMPLEX].blockfn = blkclip1 ;

  nbit_blit_slice0[BLT_CLP_NONE].blockfn = blkfill0;
  nbit_blit_slice0[BLT_CLP_RECT].blockfn = blkfill0;
  nbit_blit_slice0[BLT_CLP_COMPLEX].blockfn = nblkclip0;

  nbit_blit_slice1[BLT_CLP_NONE].blockfn = blkfill1;
  nbit_blit_slice1[BLT_CLP_RECT].blockfn = blkfill1;
  nbit_blit_slice1[BLT_CLP_COMPLEX].blockfn = nblkclip1;
}

/* Log stripped */
