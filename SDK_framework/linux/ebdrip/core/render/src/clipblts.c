/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:clipblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Clip blitting functions.
 */

#include "core.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "render.h"   /* render_blit_t */
#include "htrender.h" /* GET_FORM */
#include "caching.h"
#include "spanlist.h"
#include "blttables.h"
#include "clipblts.h"
#include "hqmemset.h"
#include "builtin.h"
#include "toneblt.h" /* charbltn */

#define BLIT_CLIP_DATA_NAME "blit clip data"

typedef struct {
  Bool clipinvert;    /**< Is the clipping inverted? */
  Bool cliplineempty; /**< Is clip line completely empty? */
  dcoord x1lineclip;  /**< Left span limit for clipfill[01] and clipfillrle[01]. */
  dcoord x2lineclip;  /**< Right span limit for clipfill[01] and clipfillrle[01]. */
  int32 llclip, lrclip;
  OBJECT_NAME_MEMBER
} blit_clip_data_t;

static void clipfill1(render_blit_t *rb,
                      register dcoord y , register dcoord xs , dcoord xe );
static void clipfill0(render_blit_t *rb,
                      register dcoord y , register dcoord xs , dcoord xe );

static void clipfillrle0(render_blit_t *rb,
                         register dcoord y, register dcoord xs, dcoord xe);
static void clipfillrle1(render_blit_t *rb,
                         register dcoord y, register dcoord xs, dcoord xe);

static void rleclip_copout(render_blit_t *rb, blit_clip_data_t *clip_data) ;

static blit_slice_t clipfillslice0 = {
  clipfill0, blkfillspan, invalid_snfill, invalid_char, invalid_imgblt
} ;

static blit_slice_t clipfillslice1 = {
  clipfill1, blkfillspan, invalid_snfill, invalid_char, invalid_imgblt
} ;

static blit_slice_t cliprleslice0 = {
  clipfillrle0, blkfillspan, invalid_snfill, invalid_char, invalid_imgblt
} ;

static blit_slice_t cliprleslice1 = {
  clipfillrle1, blkfillspan, invalid_snfill, invalid_char, invalid_imgblt
} ;

static void initialise_complex_clipping(render_blit_t *rb,
                                        blit_clip_data_t *clip_data,
                                        Bool dorleclip)
{
  FORM *clipform ;
  render_tracker_t *tracker;
  const render_info_t *ri ;
  Bool firstclip ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render blit data inconsistent") ;
  ri = rb->p_ri ;
  tracker = ri->p_rs->cs.renderTracker ; /* Ugh! */
  firstclip = (tracker->clipping == CLIPPING_firstcomplex) ;

  HQASSERT(blit_quantise_state(rb->color) == blit_quantise_max ||
           blit_quantise_state(rb->color) == blit_quantise_min,
           "Clipping mask color should be min or max") ;

  if ( firstclip ) {
    tracker->x1bandclip = ri->clip.x1;
    tracker->x2bandclip = ri->clip.x2;
  }
  clip_data->x1lineclip = ri->clip.x1;
  clip_data->x2lineclip = ri->clip.x2;
  clip_data->llclip = (ri->clip.x1 + rb->x_sep_position) >> BLIT_SHIFT_BITS ;
  clip_data->lrclip = (ri->clip.x2 + rb->x_sep_position) >> BLIT_SHIFT_BITS ;
  clip_data->clipinvert = (blit_quantise_state(rb->color) == blit_quantise_max) ;
  clip_data->cliplineempty = !clip_data->clipinvert;

  NAME_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);

  clipform = rb->clipform ;

  if ( firstclip ) {
    /* First time around, we need to set up the form's clip type. If we're
       going to do RLE clipping, we also need to zero the clip form, because
       we need the spanlist structures initialised for each line. */
    if ( dorleclip ) {
      if ( !rleclip_initform(clipform) ) {
        /* Can't do RLE clipping, because the form isn't big enough. Fall back
           to bitmap clipping. */
        theFormT(*clipform) = FORMTYPE_BANDBITMAP ;
      }
    } else {
      theFormT(*clipform) = FORMTYPE_BANDBITMAP ;
    }
  } else {
    /* Not first time around. If we can't do RLE clipping now, but had
       done it before, we need to reset the clipping form type. */
    if ( theFormT(*clipform) == FORMTYPE_BANDRLEENCODED && !dorleclip ) {
      bandrleencoded_to_bandbitmap(clipform,
                                   ri->p_rs->forms->halftonebase,
                                   clip_data->llclip << BLIT_SHIFT_BITS,
                                   clip_data->lrclip << BLIT_SHIFT_BITS);
      HQASSERT(theFormT(*clipform) == FORMTYPE_BANDBITMAP,
               "Clip form not converted to bitmap") ;
    }
  }

  /* Select the right blitter for the purpose */
  if ( theFormT(*clipform) == FORMTYPE_BANDRLEENCODED ) {
    spanlist_t *spans ;

    spans = spanlist_init(ri->p_rs->forms->clippingbase, theFormL(*clipform)) ;
    HQASSERT(spans != NULL,
             "Should have disabled RLE clipping on firstclip") ;

    if ( clip_data->clipinvert ) {
      if ( !spanlist_insert(spans, clip_data->x1lineclip,
                            clip_data->x2lineclip) )
        HQFAIL("Should have checked for span space on firstclip") ;
    }
  } else {
    register blit_t *bptr ;
    register blit_t temp ;

    /* First of all, clear the clip line. */
    bptr = ri->p_rs->forms->clippingbase;
    bptr += clip_data->llclip;
    temp = clip_data->clipinvert ? ALLONES : 0 ;
    BlitSet(bptr, temp, clip_data->lrclip - clip_data->llclip + 1);
  }

  SET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
}

static void clipfillrle1(render_blit_t *rb,
                         register dcoord y , register dcoord xs , dcoord xe )
{
  spanlist_t *clipptr ;
  blit_clip_data_t *clip_data;

  UNUSED_PARAM(dcoord, y) ;

  BITBLT_ASSERT(rb, xs, xe, y, y, "clipfillrle1" ) ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDRLEENCODED,
           "Clip form is not spanlist encoded") ;

  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
  VERIFY_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);

  if ( clip_data->cliplineempty ) { /* no existing span, so set limits */
    clip_data->x1lineclip = xs;
    clip_data->x2lineclip = xe;
    clip_data->cliplineempty = FALSE;
  } else if ( xs > clip_data->x2lineclip + 1 ||
              xe < clip_data->x1lineclip - 1 ) {
    /* doesn't intersect existing span, so fail miserably */
    clip_data->x1lineclip = MAXDCOORD;
    clip_data->x2lineclip = MINDCOORD;
  } else {                      /* enlarge span */
    if ( xs < clip_data->x1lineclip )
      clip_data->x1lineclip = xs;
    if ( xe > clip_data->x2lineclip )
      clip_data->x2lineclip = xe;
  }

  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  clipptr = (spanlist_t *)rb->p_ri->p_rs->forms->clippingbase;
  if ( !spanlist_insert(clipptr, xs, xe) ) {
    /* Spanlist needs emptying. Try to merge first. */
    if ( !spanlist_merge(clipptr) ) {
      /* Convert to bitmap if failed to reduce space used. */
      rleclip_copout(rb, clip_data) ;
    }
  }
#if RLECLIP_MAXSPANS > 0
  else if ( spanlist_count(clipptr) > RLECLIP_MAXSPANS )
    rleclip_copout(rb, clip_data) ;
#endif
}

static void clipfillrle0(render_blit_t *rb,
                         register dcoord y , register dcoord xs , dcoord xe )
{
  spanlist_t *clipptr ;
  blit_clip_data_t *clip_data;
  const dbbox_t *clip_bbox ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "clipfillrle0" ) ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDRLEENCODED,
           "Clip form is not spanlist encoded") ;

  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
  VERIFY_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);

  clip_bbox = &rb->p_ri->clip ;

  /* 0's fill is treated as two implied 1's fills */
  if ( xe < clip_bbox->x2 && xe + 1 > clip_data->x1lineclip ) {
    clip_data->x1lineclip = xe + 1;
  }
  if ( xs > clip_bbox->x1 && xs - 1 < clip_data->x2lineclip ) {
    clip_data->x2lineclip = xs - 1;
  }

  xs += rb->x_sep_position ;
  xe += rb->x_sep_position ;

  clipptr = (spanlist_t *)rb->p_ri->p_rs->forms->clippingbase;
  if ( !spanlist_delete(clipptr, xs, xe) ) {
    /* Spanlist needs emptying. Try to merge first. */
    if ( !spanlist_merge(clipptr) ) {
      /* Convert to bitmap if failed to reduce space used. */
      rleclip_copout(rb, clip_data) ;
    }
  }
#if RLECLIP_MAXSPANS > 0
  else if ( spanlist_count(clipptr) > RLECLIP_MAXSPANS )
    rleclip_copout(rb, clip_data) ;
#endif
}

void clipfill1(render_blit_t *rb,
               register dcoord y , register dcoord xs , dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  blit_clip_data_t *clip_data;

  UNUSED_PARAM(dcoord, y);
  BITBLT_ASSERT(rb, xs, xe, y, y, "clipfill1" ) ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
  VERIFY_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);

  if ( clip_data->cliplineempty ) { /* no existing span, so set limits */
    clip_data->x1lineclip = xs;
    clip_data->x2lineclip = xe;
    clip_data->cliplineempty = FALSE;
  } else if ( xs > clip_data->x2lineclip || xe < clip_data->x1lineclip ) {
    /* doesn't intersect existing span, so fail miserably */
    clip_data->x1lineclip = MAXDCOORD;
    clip_data->x2lineclip = MINDCOORD;
  } else {                      /* enlarge span */
    if ( xs < clip_data->x1lineclip )
      clip_data->x1lineclip = xs;
    if ( xe > clip_data->x2lineclip )
      clip_data->x2lineclip = xe;
  }

  /* Find left-most integer address. */
  xe = xe - xs + 1 ;
  xs += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->p_ri->p_rs->forms->clippingbase, BLIT_OFFSET(xs)) ;
  xs = xs & BLIT_MASK_BITS ;

  /* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

    /* Doesn't cross word border. */
    if ( xe >= 0 ) {
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
      (*formptr) |= temp ;
      return ;
    }
    (*formptr++) |= temp ;
    xe = -xe ;
  }
  xs = xe >> BLIT_SHIFT_BITS ;
  if ( xs )
  {
    BlitSet(formptr, ALLONES, xs);
    formptr += xs ;
  }
  /* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    (*formptr) |= temp ;
  }
}

void clipfill0(render_blit_t *rb,
               register dcoord y , register dcoord xs , dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  blit_clip_data_t *clip_data;
  const dbbox_t *clip_bbox ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, "clipfill0" ) ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;

  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
  VERIFY_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);

  clip_bbox = &rb->p_ri->clip ;

  /* 0's fill is treated as two implied 1's fills */
  if ( xe < clip_bbox->x2 && xe + 1 > clip_data->x1lineclip ) {
    clip_data->x1lineclip = xe + 1;
  }
  if ( xs > clip_bbox->x1 && xs - 1 < clip_data->x2lineclip ) {
    clip_data->x2lineclip = xs - 1;
  }

  /* Find left-most integer address. */
  xe = xe - xs + 1 ;
  xs += rb->x_sep_position ;

  formptr = BLIT_ADDRESS(rb->p_ri->p_rs->forms->clippingbase, BLIT_OFFSET(xs)) ;
  xs = xs & BLIT_MASK_BITS ;

  /* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xs ) ;

    /* Doesn't cross word border. */
    if ( xe >= 0 ) {
      if (xe != 0)
        temp |= ( SHIFTRIGHT( ALLONES , BLIT_WIDTH_BITS - xe )) ;
      (*formptr) &= temp ;
      return ;
    }
    (*formptr++) &= temp ;
    xe = -xe ;
  }
  xs = xe >> BLIT_SHIFT_BITS ;
  if ( xs )
  {
    BlitSet(formptr, 0, xs);
    formptr += xs;
  }
  /* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTRIGHT( ALLONES , xe ) ;
    (*formptr) &= temp ;
  }
}

static void clipchoose(render_blit_t *rb,
                       register dcoord y , register dcoord xs , dcoord xe )
{
  blit_slice_t *slice ;
  blit_color_t *color = rb->color ;

  BITBLT_ASSERT(rb, xs, xe, y, y, "clipchoose" ) ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDRLEENCODED ||
           rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not spanlist or bitmap encoded") ;

  HQASSERT(color->valid & blit_color_quantised, "Quantised color not set for span") ;
  HQASSERT(BLIT_SOLE_CHANNEL < BLIT_MAX_CHANNELS, "No halftone color index") ;
  HQASSERT((color->state[BLIT_SOLE_CHANNEL] & blit_channel_present) != 0,
           "Sole color should not have been overprinted") ;

  switch ( blit_quantise_state(color) ) {
  default:
    HQFAIL("Should only be one color channel for clipping") ;
    /*@fallthrough@*/
  case blit_quantise_min: /* Black */
    slice = rb->clipform->type == FORMTYPE_BANDRLEENCODED
      ? &cliprleslice1 : &clipfillslice1 ;
    break ;
  case blit_quantise_max: /* White */
    slice = rb->clipform->type == FORMTYPE_BANDRLEENCODED
      ? &cliprleslice0 : &clipfillslice0 ;
    break ;
  }

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->spanfn)(rb, y, xs, xe) ;
}

static void updateclip(surface_handle_t handle, render_blit_t *rb, dcoord y)
{
  blit_t *ymaskaddr ;
  uint32 firstclip ;
  blit_clip_data_t *clip_data;
  render_tracker_t *tracker ;
  const render_info_t *ri ;

  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(dcoord, y) ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render blit data inconsistent") ;
  ri = rb->p_ri ;
  tracker = ri->p_rs->cs.renderTracker;
  firstclip = tracker->clipping ;
  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
  VERIFY_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);
  ymaskaddr = rb->ymaskaddr ;

  HQASSERT(rb->clipform == &ri->p_rs->forms->clippingform,
           "Updating bandclip for the wrong form");

  if ( clip_data->cliplineempty ) {
    tracker->x1bandclip = MAXDCOORD;
    tracker->x2bandclip = MINDCOORD;
  } else {      /* reduce band clip to extents set for line */
    if ( clip_data->x1lineclip > tracker->x1bandclip )
      tracker->x1bandclip = clip_data->x1lineclip;
    if ( clip_data->x2lineclip < tracker->x2bandclip )
      tracker->x2bandclip = clip_data->x2lineclip;
  }
  clip_data->x1lineclip = ri->clip.x1;
  clip_data->x2lineclip = ri->clip.x2;
  clip_data->cliplineempty = !clip_data->clipinvert;

 retry:
  if ( theFormT(*rb->clipform) == FORMTYPE_BANDRLEENCODED ) {
    spanlist_t *spans = (spanlist_t *)ri->p_rs->forms->clippingbase ;
    spanlist_t *ymaskcoords = (spanlist_t *)ymaskaddr ;

    if ( firstclip == CLIPPING_firstcomplex ) {
      /* Normal clipping; first time around, copy the spans to the
         destination. Second and subsequent times, clip the existing
         spans to the new clip line. */
      if ( !spanlist_copy(ymaskcoords, spans) )
        HQFAIL("Spanlist should have been large enough") ;
    } else if ( spanlist_count(ymaskcoords) != 0 ) {
      if ( !spanlist_clipto(ymaskcoords, spans)
#if RLECLIP_MAXSPANS > 0
           || spanlist_count(ymaskcoords) > RLECLIP_MAXSPANS
#endif
           ) {
        rleclip_copout(rb, clip_data) ;
        goto retry;
      }
    }

    spanlist_reset(spans);
    if ( clip_data->clipinvert ) {
      if ( !spanlist_insert(spans, clip_data->x1lineclip,
                            clip_data->x2lineclip) )
        HQFAIL("Should have checked for span space on firstclip") ;
    }
  }
  else {
    register int32 n , l ;
    register blit_t *toptr ;
    register blit_t *formptr ;
    register blit_t temp ;
    blit_clip_data_t *clip_data;

    HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
             "Clip form is not bitmap") ;

    GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, clip_data);
    VERIFY_OBJECT(clip_data, BLIT_CLIP_DATA_NAME);

    toptr   = ymaskaddr + clip_data->llclip;
    formptr = ri->p_rs->forms->clippingbase + clip_data->llclip;

    temp = clip_data->clipinvert ? ALLONES : 0 ;

    l = clip_data->lrclip - clip_data->llclip + 1;
    if ( firstclip == CLIPPING_firstcomplex ) {
      for ( n = l >> 3 ; n != 0 ; n--, formptr += 8, toptr += 8 ) {
        PENTIUM_CACHE_LOAD(toptr + 7) ;
        toptr[0] = formptr[0] ; formptr[0] = temp ;
        toptr[1] = formptr[1] ; formptr[1] = temp ;
        toptr[2] = formptr[2] ; formptr[2] = temp ;
        toptr[3] = formptr[3] ; formptr[3] = temp ;
        toptr[4] = formptr[4] ; formptr[4] = temp ;
        toptr[5] = formptr[5] ; formptr[5] = temp ;
        toptr[6] = formptr[6] ; formptr[6] = temp ;
        toptr[7] = formptr[7] ; formptr[7] = temp ;
      }
      l &= 7 ;
      switch ( l ) {
      case 7 : toptr[6] = formptr[6] ; formptr[6] = temp ;
      case 6 : toptr[5] = formptr[5] ; formptr[5] = temp ;
      case 5 : toptr[4] = formptr[4] ; formptr[4] = temp ;
      case 4 : toptr[3] = formptr[3] ; formptr[3] = temp ;
      case 3 : toptr[2] = formptr[2] ; formptr[2] = temp ;
      case 2 : toptr[1] = formptr[1] ; formptr[1] = temp ;
      case 1 : toptr[0] = formptr[0] ; formptr[0] = temp ;
      }
    }
    else {
      for ( n = l >> 3 ; n != 0 ; n--, formptr += 8, toptr += 8 ) {
        toptr[0] &= formptr[0] ; formptr[0] = temp ;
        toptr[1] &= formptr[1] ; formptr[1] = temp ;
        toptr[2] &= formptr[2] ; formptr[2] = temp ;
        toptr[3] &= formptr[3] ; formptr[3] = temp ;
        toptr[4] &= formptr[4] ; formptr[4] = temp ;
        toptr[5] &= formptr[5] ; formptr[5] = temp ;
        toptr[6] &= formptr[6] ; formptr[6] = temp ;
        toptr[7] &= formptr[7] ; formptr[7] = temp ;
      }
      l &= 7 ;
      switch ( l ) {
      case 7 : toptr[6] &= formptr[6] ; formptr[6] = temp ;
      case 6 : toptr[5] &= formptr[5] ; formptr[5] = temp ;
      case 5 : toptr[4] &= formptr[4] ; formptr[4] = temp ;
      case 4 : toptr[3] &= formptr[3] ; formptr[3] = temp ;
      case 3 : toptr[2] &= formptr[2] ; formptr[2] = temp ;
      case 2 : toptr[1] &= formptr[1] ; formptr[1] = temp ;
      case 1 : toptr[0] &= formptr[0] ; formptr[0] = temp ;
      }
    }
  }
}

/* Clear an RLE clip form. */
Bool rleclip_initform(FORM *formptr)
{
  int32 l = theFormL(*formptr) ;
  int32 rh = theFormRH(*formptr) ;
  blit_t *llineaddr = theFormA(*formptr) ;

  /* Insist on a minimum of two spans, because self-intersecting and inverted
     clipping need to insert a black span to cover the whole area. The other
     cases that we use spanlists for won't really suffer from this
     limitation, extracting spans from small bitmaps doesn't touch a huge
     amount of memory. */
  if ( spanlist_size(2) > (size_t)l )
    return FALSE ;

  while ( --rh >= 0 ) {
    if ( spanlist_init(llineaddr, l) == NULL )
      HQFAIL("No space for spanlist, but already checked") ;
    llineaddr = BLIT_ADDRESS(llineaddr, l) ;
  }

  theFormT(*formptr) = FORMTYPE_BANDRLEENCODED ;

  return TRUE ;
}

static void rleclip_copout(render_blit_t *rb, blit_clip_data_t *clip_data)
{
  FORM singlelineform ;
  FORM *clipform = rb->clipform ;
  blit_t *workspace = rb->p_ri->p_rs->forms->halftonebase ;
  blit_slice_t *slice = clip_data->clipinvert ? &clipfillslice0 :
                                                &clipfillslice1 ;

  /* Convert the clipping form to bitmap. */
  bandrleencoded_to_bandbitmap(clipform, workspace,
                               clip_data->llclip << BLIT_SHIFT_BITS,
                               clip_data->lrclip << BLIT_SHIFT_BITS);

  /* Convert the current clipping line to a bitmap. */
  theFormA(singlelineform) = rb->p_ri->p_rs->forms->clippingbase;
  theFormT(singlelineform) = FORMTYPE_BANDRLEENCODED ;
  theFormW(singlelineform) = theFormW(*clipform) ;
  theFormH(singlelineform) = 1 ;
  theFormL(singlelineform) = theFormL(*clipform) ;
  theFormS(singlelineform) = theFormL(*clipform) ;
  theFormRH(singlelineform) = 1 ;
  bandrleencoded_to_bandbitmap(&singlelineform, workspace,
                               clip_data->llclip << BLIT_SHIFT_BITS,
                               clip_data->lrclip << BLIT_SHIFT_BITS);

  /* Reset the base blit to use a bitmap clip fill. */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice);
}

/* Converts RLE form to bitmap
 *
 * Caller must provide a memory block of equal size as a working area.
 */
void bandrleencoded_to_bandbitmap(FORM *theform, blit_t *tmp_mem,
                                  dcoord x1, dcoord x2)
{
  render_state_t rs_mask ;
  blit_chain_t mask_blits ;
  render_forms_t mask_forms ;
  int32 l , rh ;
  dcoord y ;
#if defined( ASSERT_BUILD )
  dcoord original_w = theFormW(*theform) ;

  /* Temporarily round the form width up to the blit width. We're going to
     change the x1,x2 limits such that we clear whole blit words, so we don't
     need any additional masking on the edges of the clip form. */
  theFormW(*theform) += BLIT_MASK_BITS ;
  theFormW(*theform) &= ~BLIT_MASK_BITS ;
#endif

  HQASSERT(theform->type == FORMTYPE_BANDRLEENCODED,
           "Form is not spanlist encoded") ;

  HQASSERT(x1 >= 0, "Start position not within band") ;
  HQASSERT(x2 < theFormW(*theform), "End position not within band") ;
  HQASSERT(x1 <= x2, "No area to convert to bitmap") ;

  /* Round start and end position out to blit boundaries. */
  x1 &= ~BLIT_MASK_BITS ;
  x2 |= BLIT_MASK_BITS ;

  render_state_mask(&rs_mask, &mask_blits, &mask_forms, &invalid_surface, theform) ;

  rs_mask.ri.rb.ylineaddr = theFormA(*theform) ;

  l = theFormL(*theform) ;
  rh = theFormRH(*theform) ;

  /* The clip box is set to the input coordinate space used by bitfill0/1,
     which excludes x_sep_position. spanlist_intersecting doesn't use the
     clip box, but the bitblits may assert it. */
  bbox_store(&rs_mask.ri.clip, x1, theFormHOff(*theform),
             x2, theFormHOff(*theform) + rh - 1) ;

  if ( spanlist_init(tmp_mem, l) == NULL )
    HQFAIL("Line memory should be big enough for spans") ;

  theFormT(*theform) = FORMTYPE_BANDBITMAP ;

  for ( y = theFormHOff(*theform) ; --rh >= 0 ; ++y ) {
    spanlist_t *srcptr = (spanlist_t *)rs_mask.ri.rb.ylineaddr ;
    spanlist_t *dstptr = (spanlist_t *)tmp_mem;

    /* Copy (smaller) RLE to tmp memory, then blit back to source line */
    if ( !spanlist_copy(dstptr, srcptr) )
      HQFAIL("Spanlist copy should not have failed") ;

    spanlist_intersecting(dstptr, bitfill1, bitfill0, &rs_mask.ri.rb,
                          y, x1, x2, 0 /* raw span positions */) ;

    rs_mask.ri.rb.ylineaddr = BLIT_ADDRESS(rs_mask.ri.rb.ylineaddr, l) ;
  }

#if defined( ASSERT_BUILD )
  theFormW(*theform) = original_w ;
#endif
}

static void areaclip(render_blit_t *rb, FORM *formptr)
{
  UNUSED_PARAM(render_blit_t *, rb) ;
  UNUSED_PARAM(FORM *, formptr) ;
  HQFAIL("Area fill for clip form should not be called (yet?)") ;
}


/** Render preparation function for halftone quantises current color. */
static surface_prepare_t render_prepare_clip(surface_handle_t handle,
                                             render_info_t *p_ri)
{
  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(render_info_t *, p_ri) ;

  return SURFACE_PREPARE_OK ;
}

/** Render a bitmap clip. */
static Bool clip_complex_1(surface_handle_t handle,
                           int32 clipid, int32 parent_clipid,
                           render_blit_t *rb,
                           surface_clip_callback_fn *callback,
                           surface_clip_callback_t *data)
{
  blit_clip_data_t blit_clip_data;

  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(int32, clipid) ;
  UNUSED_PARAM(int32, parent_clipid) ;

  HQASSERT((parent_clipid == SURFACE_CLIP_INVALID) == (rb->p_ri->p_rs->cs.renderTracker->clipping == CLIPPING_firstcomplex),
           "Clipping tracker and cached form are inconsistent") ;

  initialise_complex_clipping(rb, &blit_clip_data, FALSE /*spanclip*/) ;

  HQASSERT(callback != NULL, "No callback function for complex clip") ;
  HQASSERT(data != NULL, "No callback data for complex clip") ;

  (*callback)(data) ;

  return TRUE ;
}

/** Render a span clip. */
static Bool clip_complex_span(surface_handle_t handle,
                              int32 clipid, int32 parent_clipid,
                              render_blit_t *rb,
                              surface_clip_callback_fn *callback,
                              surface_clip_callback_t *data)
{
  blit_clip_data_t blit_clip_data;

  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(int32, clipid) ;
  UNUSED_PARAM(int32, parent_clipid) ;

  HQASSERT((parent_clipid == SURFACE_CLIP_INVALID) == (rb->p_ri->p_rs->cs.renderTracker->clipping == CLIPPING_firstcomplex),
           "Clipping tracker and cached form are inconsistent") ;

  initialise_complex_clipping(rb, &blit_clip_data, TRUE /*spanclip*/) ;

  HQASSERT(callback != NULL, "No callback function for complex clip") ;
  HQASSERT(data != NULL, "No callback data for complex clip") ;

  (*callback)(data) ;

  return TRUE ;
}

static clip_surface_t clip_1_surface = CLIP_SURFACE_INIT ;

static clip_surface_t clip_span_surface = CLIP_SURFACE_INIT ;

void init_clip_surface(void)
{
  /* Bit mask clip surface */
  clip_1_surface.base.baseblits[BLT_CLP_NONE].spanfn =
    clip_1_surface.base.baseblits[BLT_CLP_RECT].spanfn =
    clip_1_surface.base.baseblits[BLT_CLP_COMPLEX].spanfn = clipchoose ;

  clip_1_surface.base.baseblits[BLT_CLP_NONE].blockfn =
    clip_1_surface.base.baseblits[BLT_CLP_RECT].blockfn = blkfillspan ;
  clip_1_surface.base.baseblits[BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  clip_1_surface.base.baseblits[BLT_CLP_NONE].charfn =
    clip_1_surface.base.baseblits[BLT_CLP_RECT].charfn =
    clip_1_surface.base.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  clip_1_surface.base.baseblits[BLT_CLP_NONE].imagefn =
    clip_1_surface.base.baseblits[BLT_CLP_RECT].imagefn =
    clip_1_surface.base.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No ROPs, self-intersection, no patterns, no gouraud, no imageclip for
     masks */

  clip_1_surface.base.areafill = areaclip ;
  clip_1_surface.base.prepare = render_prepare_clip ;
  clip_1_surface.base.line_update = updateclip ;

  clip_1_surface.base.n_rollover = 0 ;   /* No rollovers */
  clip_1_surface.base.screened = FALSE ; /* There are no intermediate values */
  /* Prefer longest image row render order, also allow copydot image
     optimisations. */
  clip_1_surface.base.render_order = SURFACE_ORDER_IMAGEROW|SURFACE_ORDER_COPYDOT ;

  clip_1_surface.complex_cached = NULL ;
  clip_1_surface.complex_clip = clip_complex_1 ;
  clip_1_surface.rect_clip = NULL ;

  /* Self reference for clip surface */
  clip_1_surface.base.clip_surface = &clip_1_surface ;

  NAME_OBJECT(&clip_1_surface, CLIP_SURFACE_NAME) ;

  /* Initialise spanlist clipping surface */
  clip_span_surface.base.baseblits[BLT_CLP_NONE].spanfn =
    clip_span_surface.base.baseblits[BLT_CLP_RECT].spanfn =
    clip_span_surface.base.baseblits[BLT_CLP_COMPLEX].spanfn = clipchoose ;

  clip_span_surface.base.baseblits[BLT_CLP_NONE].blockfn =
    clip_span_surface.base.baseblits[BLT_CLP_RECT].blockfn = blkfillspan ;
  clip_span_surface.base.baseblits[BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  clip_span_surface.base.baseblits[BLT_CLP_NONE].charfn =
    clip_span_surface.base.baseblits[BLT_CLP_RECT].charfn =
    clip_span_surface.base.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  clip_span_surface.base.baseblits[BLT_CLP_NONE].imagefn =
    clip_span_surface.base.baseblits[BLT_CLP_RECT].imagefn =
    clip_span_surface.base.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No ROPs, self-intersection, no patterns, no gouraud, no imageclip for
     masks */

  clip_span_surface.base.areafill = areaclip ;
  clip_span_surface.base.prepare = render_prepare_clip ;
  clip_span_surface.base.line_update = updateclip ;

  clip_span_surface.base.n_rollover = 0 ;   /* No rollovers */
  clip_span_surface.base.screened = FALSE ; /* There are no intermediate values */
  /* Prefer left-right span order for rendering. */
  clip_span_surface.base.render_order = SURFACE_ORDER_DEVICELR ;

  clip_span_surface.complex_cached = NULL ;
  clip_span_surface.complex_clip = clip_complex_span ;
  clip_span_surface.rect_clip = NULL ;

  /* Self reference for clip surface */
  clip_span_surface.base.clip_surface = &clip_span_surface ;

  NAME_OBJECT(&clip_span_surface, CLIP_SURFACE_NAME) ;
}

/** Attach builtin bitmask clip surface implementation as clip surface. */
void builtin_clip_1_surface(surface_t *surface, const surface_t *indexed[])
{
  HQASSERT(surface != NULL, "No surface") ;
  VERIFY_OBJECT(&clip_1_surface, CLIP_SURFACE_NAME) ;
  surface->clip_surface = &clip_1_surface ;
  if ( indexed )
    indexed[SURFACE_CLIP] = &clip_1_surface.base ;
}

/** Attach builtin spanlist clip surface implementation as clip surface. */
void builtin_clip_N_surface(surface_t *surface, const surface_t *indexed[])
{
  HQASSERT(surface != NULL, "No surface") ;
  VERIFY_OBJECT(&clip_span_surface, CLIP_SURFACE_NAME) ;
  surface->clip_surface = &clip_span_surface ;
  if ( indexed )
    indexed[SURFACE_CLIP] = &clip_span_surface.base ;
}

/* Log stripped */
