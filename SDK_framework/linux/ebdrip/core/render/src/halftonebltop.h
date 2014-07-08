/** \file
 * \ingroup rendering
 *
 * Copyright (C) 2013-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief This file contains the definition of the optimised halftone
 * AND/OR/XOR blit functions for use by the ROP/MAXBLIT layer.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro ASSIGNOP expands to the assignment operation to be used (|=, &=,
 * ^=).
 *
 * The macro NOTOP expands to a unary operator put in front of the span
 * initialisation and span operand.
 *
 * The macro OPNAME expands to the name of the assignment operation used. This
 * is used to construct the names bitfill##OPNAME and bitclip##OPNAME.
 *
 * The macro SPAN_FN is optional. If defined, it is a function with the
 * prototype BITBLT_FUNCTION, which will be called to render the underlying
 * span. If not defined, the normal blit chain will be called.
 *
 * The function parameters are:
 *
 *   rb               - The render_blit_t state pointer.
 *   y                - Y coordinate of the blit. ylineaddr and ymaskaddr are
 *                      expected to be already set up.
 *   x1               - Left X coordinate of span.
 *   x2               - Right X coordinate of span.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

/** \brief Add the expansion unit width to the end of a token name

    We need a three-level macro expansion to guarantee expansion of the
    preprocessing tokens we're concatenating. FUNCTION introduces the
    bit/fillclip/op qualifiers on names, but fillclip and op are macros which
    we want expanded themselves. The FUNCTION level guarantees that its
    arguments are fully expanded (C99, 6.10.3.1) before expanding CONCAT,
    which performs the token concatenation. */
#define FUNCTION(x_,y_) CONCAT(x_,y_)
#define CONCAT(x_,y_) bit ## x_ ## y_
#define STRINGIFY(x_) #x_

/* If NOTOP is not defined, it should be blank. */
#ifndef NOTOP
#define NOTOP
#endif

static void FUNCTION(fill,OPNAME)(render_blit_t *rb,
                                  dcoord y, dcoord xs, dcoord xe)
{
  dcoord txs, txe;
  blit_t *pTarget, *pSpareLine, init ;

  BITBLT_ASSERT(rb, xs, xe, y, y, STRINGIFY(FUNCTION(fill,OPNAME)));

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;

  /* use the maxblt scan line to build the span, rather than the
     normal raster line (which we hang onto to write into later) */

  pTarget = rb->ylineaddr;
  pSpareLine = rb->ylineaddr = rb->p_ri->p_rs->forms->maxbltbase;

  txs = ((xs + rb->x_sep_position) << rb->depth_shift) >> BLIT_SHIFT_BITS;
  txe = ((xe + rb->x_sep_position) << rb->depth_shift) >> BLIT_SHIFT_BITS;

  /* For the fill function, we only need to set or clear the first and last
     words. The underlying bitfill will overwrite everything in between. The
     initialiser for AND and XORNOT should be all ones, for OR and XOR it
     should be 0. This is computed as ~(ALLONES OP 0), the compiler should be
     able to fold this into a constant expression. */
  init = ALLONES ;
  init ASSIGNOP NOTOP 0 ;
  init = ~init ;
  pSpareLine[txs] = pSpareLine[txe] = init;

  /* make the span */
#ifdef SPAN_FN
  SPAN_FN(rb, y, xs, xe);
#else
  DO_SPAN(rb, y, xs, xe);
#endif

  /* Loop unroll this if necessary */
  do {
    pTarget[txs] ASSIGNOP NOTOP pSpareLine[txs];
    ++txs ;
  } while ( txs <= txe ) ;

  rb->ylineaddr = pTarget ;
}

static void FUNCTION(clip,OPNAME)(render_blit_t *rb,
                                  dcoord y, dcoord xs, dcoord xe)
{
  dcoord txs, txe;
  blit_t *pTarget, *pSpareLine, init;

  BITBLT_ASSERT(rb, xs, xe, y, y, STRINGIFY(FUNCTION(clip,OPNAME)));

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not bitmap") ;

  /* use the maxblt scan line to build the span, rather than the
     normal raster line (which we hang onto to write into later) */

  pTarget = rb->ylineaddr;
  pSpareLine = rb->ylineaddr = rb->p_ri->p_rs->forms->maxbltbase;

  txs = ((xs + rb->x_sep_position) << rb->depth_shift) >> BLIT_SHIFT_BITS;
  txe = ((xe + rb->x_sep_position) << rb->depth_shift) >> BLIT_SHIFT_BITS;

  /* Convert txe into a word count */
  txe = txe - txs + 1 ;

  /* For the clip function, we need to set or clear the entire spare line,
     because the clip may not touch it all. The initialiser for AND and
     XORNOT should be all ones, for OR and XOR it should be 0. This is
     computed as ~(ALLONES OP 0), the compiler should be able to fold this
     into a constant expression. */
  init = ALLONES ;
  init ASSIGNOP NOTOP 0 ;
  init = ~init ;
  BlitSet(&pSpareLine[txs], init, txe) ;

  /* make the span */
#ifdef SPAN_FN
  SPAN_FN(rb, y, xs, xe);
#else
  DO_SPAN(rb, y, xs, xe);
#endif

  /* Loop unroll this if necessary */
  do {
    pTarget[txs] ASSIGNOP NOTOP pSpareLine[txs];
    ++txs ;
  } while ( --txe ) ;

  rb->ylineaddr = pTarget ;
}

#undef ASSIGNOP
#undef NOTOP
#undef OPNAME
#undef SPAN_FN
#undef FUNCTION
#undef CONCAT
#undef STRINGIFY

/* $Log
*/
