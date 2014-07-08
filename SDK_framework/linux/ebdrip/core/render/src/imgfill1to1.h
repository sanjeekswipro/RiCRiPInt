/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:imgfill1to1.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the definition of the optimised row fill functions for
 * orthogonal 1:1 images.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro FUNCTION expands to the function name to be defined.
 *
 * The macro CHARBLT_FN is the name of the char blit function used to
 * implement the fill function. It is also used to suffix the fill function
 * to distinguish it.
 *
 * The function parameters are:
 *
 *   rb               - The render_blit_t state pointer.
 *   params           - The collected image blit parameters.
 *   expanded         - A pointer to the expanded image data.
 *   rw               - The number of pixels in the row.
 *   sw               - The offset into the row where we start drawing.
 *   y1               - Top Y coordinate of the row.
 *   y2               - Bottom Y coordinate of the row.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

static void FUNCTION(render_blit_t *rb,
                     const imgblt_params_t *params,
                     const void *expanded,
                     dcoord y1, dcoord y2)
{
  FORM form;
  dcoord x1;
  int32 sw ;

  /* ROP blits share a blit chain slot with max blits. Allow this function to
     be called from base blit slot only if not ROPping or MAX blitting. */
  HQASSERT(!DOING_BLITS(rb->blits, MAXBLT_BLIT_INDEX) ||
           rb->blits->blit_img >= 0, /* i.e., there is something under this. */
           "Image maxed with 1 to 1 optimisation");
  HQASSERT(rb->p_ri->pattern_state == PATTERN_OFF,
           "Image pattern replication with 1 to 1 optimisation") ;

  /* We adjust the image expander boundary down to a multiple of the blit
     word size. We have to shift the form left or right depending on whether
     this extra padding is at the left or the right of the row. */
  sw = params->lcol & BLIT_MASK_BITS ;

  /* Undo the automatic modification of the expansion pointer done by the
     orthogonal row loop. */
  if ( params->dcol < 0 ) {
    HQASSERT(params->converted_comps == 1,
             "Should be only one channel for 1:1") ;
    expanded = (uint8 *)expanded - (params->out16 + 1) * (params->ncols - 1) ;
  }

  theFormT(form) = FORMTYPE_BANDBITMAP ;
  theFormA(form) = (blit_t *)expanded;
  theFormW(form) = ((sw + params->ncols + BLIT_MASK_BITS) & ~BLIT_MASK_BITS);
  theFormS(form) = theFormL(form) = (theFormW(form) >> 3);
  theFormH(form) = 1;

  HQASSERT(theFormA(form) == BLIT_ALIGN_DOWN(theFormA(form)),
           "Image clip values not blit aligned") ;

  /* The 1-bit HT surface is agnostic to device order, so wflip should never
     be set. */
  HQASSERT(!params->wflip, "Image 1:1 with wflip set") ;
  if ( params->wflip ) {
    x1 = params->xs.i - theFormW(form) + params->ncols + sw ;
  } else if ( params->xperwh.i < 0 ) {
    x1 = params->xs.i - theFormW(form) + sw ;
  } else {
    x1 = params->xs.i - sw ;
  }

  do {
    CHARBLT_FN(rb, &form, x1, y1);
  } while ( ++y1 <= y2 ) ;
}

#undef FUNCTION
#undef CHARBLT_FN

/* Log stripped */
