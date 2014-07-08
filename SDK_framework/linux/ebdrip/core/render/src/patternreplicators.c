/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:patternreplicators.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Pattern replication blitters
 */

#include "core.h"
#include "patternreplicators.h"
#include "patternrender.h" /* pattern_tracker_t */
#include "display.h"
#include "render.h"
#include "often.h"
#include "pattern.h"
#include "surface.h"
#include "bitblts.h" /* DO_SPAN */
#include "toneblt.h" /* imagebltn */
#include "hdl.h"

/* Pattern replicator naming convention:

   pattern [orth] [none|int|int32|real] [span|block|char]

   pattern - all

   orth    - orthogonal tiles (or rotated if missing)

   none    - no tiling
   int     - integer stepping for tiling type 1 (constant stepping)
   real    - real stepping for tiling type 2 (maintains tiling phase)
   int32   - historical, same as int

   span/block/char is obvious

 */

/* For tiling type 2 replicators. */
static inline void do_span_patternreal(render_blit_t *rb,
                                       dcoord iyc, dcoord ixc1, dcoord ixc2,
                                       dcoord y, dcoord xs,
                                       SYSTEMVALUE yc, SYSTEMVALUE xc)
{
  pattern_nonoverlap_t *nonoverlap = &rb->p_painting_pattern->pPattern->nonoverlap ;

  /* Tiling type 2 (real number stepping) can result in unintended gaps between
     tiles that should tesselate precisely.  To avoid this we stretch the key
     cell (in createPatternDL) by up to one device pixel and detect any overlap
     between tiles which should be at most one device pixel (in one axis). */
  if ( nonoverlap->enabled ) {
    SYSTEMVALUE xlast = xc - xs + nonoverlap->xx + nonoverlap->yx ;
    SYSTEMVALUE ylast = yc - y + nonoverlap->yy + nonoverlap->xy ;
    dcoord ixc_last ; /* last x dcoord in this tile */
    dcoord iyc_last ; /* last y dcoord in this tile */

    SC_C2D_INT(ixc_last, xlast) ;
    --ixc_last ;
    if ( ixc2 > ixc_last ) {
      HQASSERT(ixc2 - 2 <= ixc_last,
               "Should only have one or two extra pixels for pattern non-overlapping") ;
      ixc2 = ixc_last ;
      if ( ixc2 < ixc1 )
        return ;
    }

    SC_C2D_INT(iyc_last, ylast) ;
    --iyc_last ;
    if ( iyc > iyc_last ) {
      HQASSERT(iyc - 2 <= iyc_last,
               "Should only have one or two extra lines for pattern non-overlapping") ;
      return ;
    }
  }

  DO_SPAN(rb, iyc, ixc1, ixc2) ;
}

/* For tiling type 2 replicators. */
static inline void do_block_patternreal(render_blit_t *rb,
                                        dcoord iyc1, dcoord iyc2, dcoord ixc1, dcoord ixc2,
                                        dcoord ys, dcoord xs,
                                        SYSTEMVALUE yc, SYSTEMVALUE xc)
{
  pattern_nonoverlap_t *nonoverlap = &rb->p_painting_pattern->pPattern->nonoverlap ;

  /* Tiling type 2 (real number stepping) can result in unintended gaps between
     tiles that should tesselate precisely.  To avoid this we stretch the key
     cell (in createPatternDL) by up to one device pixel and detect any overlap
     between tiles which should be at most one device pixel (in one axis). */
  if ( nonoverlap->enabled ) {
    SYSTEMVALUE xlast = xc - xs + nonoverlap->xx + nonoverlap->yx ;
    SYSTEMVALUE ylast = yc - ys + nonoverlap->yy + nonoverlap->xy ;
    dcoord ixc_last ; /* last x dcoord in this tile */
    dcoord iyc_last ; /* last y dcoord in this tile */

    SC_C2D_INT(ixc_last, xlast) ;
    --ixc_last ;
    if ( ixc2 > ixc_last ) {
      HQASSERT(ixc2 - 2 <= ixc_last,
               "Should only have one or two extra pixels for pattern non-overlapping") ;
      ixc2 = ixc_last ;
      if ( ixc2 < ixc1 )
        return ;
    }

    SC_C2D_INT(iyc_last, ylast) ;
    --iyc_last ;
    if ( iyc2 > iyc_last ) {
      HQASSERT(iyc2 - 2 <= iyc_last,
               "Should only have one or two extra lines for pattern non-overlapping") ;
      iyc2 = iyc_last ;
      if ( iyc2 < iyc1 )
        return ;
    }
  }

  DO_BLOCK(rb, iyc1, iyc2, ixc1, ixc2) ;
}

/* For tiling type 2 replicators. */
static inline Bool do_callback_patternreal(pattern_tracker_t *tracker,
                                           pattern_render_callback callback, void *data,
                                           dcoord iyc1, dcoord iyc2, dcoord ixc1, dcoord ixc2,
                                           dcoord ys, dcoord xs,
                                           SYSTEMVALUE yc, SYSTEMVALUE xc,
                                           dcoord xoffset, dcoord yoffset)
{
  pattern_nonoverlap_t *nonoverlap = &tracker->pPattern->nonoverlap;
  dbbox_t tile;

  /* Tiling type 2 (real number stepping) can result in unintended gaps between
     tiles that should tesselate precisely.  To avoid this we stretch the key
     cell (in createPatternDL) by up to one device pixel and detect any overlap
     between tiles which should be at most one device pixel (in one axis). */
  if ( nonoverlap->enabled ) {
    SYSTEMVALUE xlast = xc - xs + nonoverlap->xx + nonoverlap->yx ;
    SYSTEMVALUE ylast = yc - ys + nonoverlap->yy + nonoverlap->xy ;
    dcoord ixc_last ; /* last x dcoord in this tile */
    dcoord iyc_last ; /* last y dcoord in this tile */

    SC_C2D_INT(ixc_last, xlast) ;
    --ixc_last ;
    if ( ixc2 > ixc_last ) {
      HQASSERT(ixc2 - 2 <= ixc_last,
               "Should only have one or two extra pixels for pattern non-overlapping") ;
      ixc2 = ixc_last ;
      if ( ixc2 < ixc1 )
        return TRUE;
    }

    SC_C2D_INT(iyc_last, ylast) ;
    --iyc_last ;
    if ( iyc2 > iyc_last ) {
      HQASSERT(iyc2 - 2 <= iyc_last,
               "Should only have one or two extra lines for pattern non-overlapping") ;
      iyc2 = iyc_last ;
      if ( iyc2 < iyc1 )
        return TRUE;
    }
  }

  bbox_store(&tile, ixc1, iyc1, ixc2, iyc2);
  if ( !(*callback)(tracker, data, &tile, xoffset, yoffset ) )
    return FALSE;

  return TRUE;
}

static void patternorthrealspan(render_blit_t *rb,
                                dcoord y, dcoord xs, dcoord xe )
{
  int32 iyb, iyc ;
  SYSTEMVALUE xb, yb, yc;
  dcoord len = xe - xs;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  SYSTEMVALUE xstepx = tracker->pPattern->xx;
  SYSTEMVALUE ystepy = tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* For comments on this see equivalent routine patternorthintspan. */

  HQASSERT(xs >= 0, "Start of span is negative coord in patternorthrealspan") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternorthrealspan") ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  yb = tracker->ybase + y; SC_C2D_INT( iyb , yb );
  yc = yb + ystepy ; SC_C2D_INT( iyc , yc ) ;
  if ( iyb >= y1replim ) {
    do {
      yc = yb ; iyc = iyb ;
      yb -= ystepy ; SC_C2D_INT( iyb , yb ) ;
    } while ( iyb >= y1replim ) ;
  }
  else if ( iyc < y1replim ) {
    do {
      yc += ystepy ; SC_C2D_INT( iyc , yc ) ;
    } while ( iyc < y1replim ) ;
  }
  if ( iyc > y2replim )
    return ;

  yb = yc ;
  for ( xb = tracker->xbase + xs ; ; xb += xstepx ) {
    int32 ixc1, ixc2;

    SwOftenUnsafe();

    SC_C2D_INT( ixc1 , xb ) ;
    if ( ixc1 > x2replim )
      break;
    ixc2 = ixc1 + len;
    if ( ixc2 < x1replim )
      continue ;
    if ( ixc1 < x1replim )
      ixc1 = x1replim ;
    if ( ixc2 > x2replim )
      ixc2 = x2replim ;

    for ( yc = yb ; ; yc += ystepy ) {
      SC_C2D_INT( iyc , yc ) ;
      HQASSERT( iyc >= y1replim , "patternorthrealspan; should be converged already in y" ) ;
      if ( iyc > y2replim )
        break ;
      do_span_patternreal(rb, iyc, ixc1, ixc2, y, xs, yc, xb);
    }
  }
}

static void patternrealspan(render_blit_t *rb,
                            dcoord y, dcoord xs, dcoord xe)
{
  SYSTEMVALUE xb, yb;
  dcoord len = xe - xs;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  SYSTEMVALUE xstepx = tracker->pPattern->xx;
  SYSTEMVALUE xstepy = tracker->pPattern->xy;
  SYSTEMVALUE ystepx = tracker->pPattern->yx;
  SYSTEMVALUE ystepy = tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* For comments on this see equivalent routine patternintspan. */

  HQASSERT(xs >= 0, "Start of span is negative coord in patternrealspan") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternrealspan") ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( xb = tracker->xbase + xs,
          yb = tracker->ybase + y ; ;
        xb += xstepx, yb += xstepy ) {
    int32 ixc, iyc, iyb;
    SYSTEMVALUE xc, yc;

    SwOftenUnsafe();

    SC_C2D_INT( iyb , yb ) ;
    yc = yb + ystepy ; SC_C2D_INT( iyc , yc ) ;
    if ( iyb >= y1replim ) {
      do {
        yc = yb ; iyc = iyb ;
        xb -= ystepx ;
        yb -= ystepy ; SC_C2D_INT( iyb , yb ) ;
      } while ( iyb >= y1replim ) ;
    }
    else if ( iyc < y1replim ) {
      do {
        xb += ystepx ;
        yb = yc ; iyb = iyc ;
        yc += ystepy ; SC_C2D_INT( iyc , yc ) ;
      } while ( iyc < y1replim ) ;
    }
    xc = xb + ystepx ; SC_C2D_INT( ixc , xc ) ;
    if ( ixc > x2replim && ystepx >= 0.0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( ystepx >= 0.0 )
          break ;
        else {
          do {
            xc += ystepx; SC_C2D_INT( ixc , xc ) ;
            yc += ystepy; SC_C2D_INT( iyc , yc ) ;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        xc += len; ixc += len;
        if ( ixc < x1replim ) {
          if ( ystepx <= 0.0 ) {
            continue ;
          }
          else {
            do {
              xc += ystepx; SC_C2D_INT( ixc , xc ) ;
              yc += ystepy; SC_C2D_INT( iyc , yc ) ;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        xc -= len; ixc -= len;
      }
      while ( iyc <= y2replim ) {
        if ( ixc >= x1replim && ixc + len <= x2replim ) {
          do_span_patternreal(rb, iyc, ixc, ixc + len, y, xs, yc, xc);
        }
        else {
          int32 txs, txe;
          txs = ixc ;
          txe = ixc + len ;
          if ( txs < x1replim ) txs = x1replim;
          if ( txe > x2replim ) txe = x2replim;
          if ( txs <= txe ) {
            do_span_patternreal(rb, iyc, txs, txe, y, xs, yc, xc);
          } else {
            if ( (txs > x2replim && ystepx >= 0.0) ||
                 (txe < x1replim && ystepx <= 0.0) )
              break; /* early termination, we're outa the inside (ystep) loop */
          }
        }
        xc += ystepx; SC_C2D_INT( ixc , xc ) ;
        yc += ystepy; SC_C2D_INT( iyc , yc ) ;
      }
    }
    if ( ixc > x2replim && ystepx <= 0.0 )
      break;
  }
}

static void patternorthintspan(render_blit_t *rb,
                               dcoord y, dcoord xs, dcoord xe )
{
  int32 iyb, iyc ;
  int32 ixb ;
  dcoord len = xe - xs;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  dcoord ixstepx = (dcoord)tracker->pPattern->xx;
  dcoord iystepy = (dcoord)tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* This code is essentially the same as patternintspan but we've optimised it
   * due to "ixstepy == 0 && iystepx == 0". This therefore means we can remove
   * a fair amount of code and "code motion" constant variables & calculations
   * out of the two loops involved.
   */

  HQASSERT(xs >= 0, "Start of span is negative coord in patternorthintspan") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternorthintspan") ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  /* Converge the top starting y base coordinate. [This is the same for all x base
   * coordinates.] Adjustment done so that iyb (integer y base) is below y1replim
   * and iyc == iyb + iystepy is above y1replim. The first span we consider is iyc.
   */
  iyb = tracker->iybase + y;
  iyc = iyb + iystepy ;
  if ( iyb >= y1replim ) {
    do {
      iyc = iyb ;
      iyb -= iystepy ;
    } while ( iyb >= y1replim ) ;
  }
  else if ( iyc < y1replim ) {
    do {
      iyb = iyc ;
      iyc += iystepy ;
    } while ( iyc < y1replim ) ;
  }

  /* The column may not be in this band at all. This can be caused by either
   * the band height being less than the size of pattern bbox, or, by the step
   * of the pattern being large when compared to the size of the pattern bbox.
   * We can optimise this out in the orthogonal case because "ixstepy == 0".
   */
  if ( iyc > y2replim )
    return ;

  /* Given our base point iyc which we know is in the replication limits, we
   * now need to cover in the outer loop all possible x values horizontally across
   * the page.
   */
  iyb = iyc ;
  for ( ixb = tracker->ixbase + xs ; ; ixb += ixstepx ) {
    int32 ixc1, ixc2;

    SwOftenUnsafe();

    /* If the column is on the right hand side of x2replim then we know we've
     * finished, as all replication goes from left to right (and top to bottom).
     * If the column is on the left hand size of x1replim then we know we're still
     * converging to the start of replication in x.
     * Then given an x position for a replication of the column, we need to clip it.
     */
    ixc1 = ixb ;
    if ( ixc1 > x2replim )
      break;
    ixc2 = ixc1 + len;
    if ( ixc2 < x1replim )
      continue ;
    if ( ixc1 < x1replim )
      ixc1 = x1replim ;
    if ( ixc2 > x2replim )
      ixc2 = x2replim ;

    /* Given a column (ixc1,ixc2) which we know is in the replication limits, we
     * now need to cover in the inner loop all possible y values vertically in
     * this (vertical) column down the page.
     */
    iyc = iyb ;
    HQASSERT( iyc >= y1replim , "patternorthintspan; should be converged already in y" ) ;
    while ( iyc <= y2replim ) {
      DO_SPAN(rb, iyc, ixc1 , ixc2 );
      iyc += iystepy ;
    }
  }
}

static void patternintspan(render_blit_t *rb,
                           dcoord y, dcoord xs , dcoord xe )
{
  int32 ixb, iyb;
  dcoord len = xe - xs;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  dcoord ixstepx = (dcoord)tracker->pPattern->xx;
  dcoord ixstepy = (dcoord)tracker->pPattern->xy;
  dcoord iystepx = (dcoord)tracker->pPattern->yx;
  dcoord iystepy = (dcoord)tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* This code is very similar to patternorthintspan, but we can't pre-converge
   * on the top or left of the replication limits outside of the main loops.
   * That also means that we can't quickly say that this span will not intersect
   * the current replication limits; the only way we can do this is to step over
   * the band, finding out that we go from off the band at the top left to off the
   * band at the bottom right etc...
   */

  HQASSERT(xs >= 0, "Start of span is negative coord in patternintspan") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternintspan") ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( ixb = tracker->ixbase + xs,
          iyb = tracker->iybase + y ; ;
        ixb += ixstepx, iyb += ixstepy ) {
    int32 ixc, iyc;

    SwOftenUnsafe();

    /* Converge the top starting y base coordinate. Adjustment done so that iyb
     * (integer y base) is below y1replim and iyc == iyb + iystepy is above y1replim.
     * The first span we consider is iyc.
     */
    iyc = iyb + iystepy ;
    if ( iyb >= y1replim ) {
      do {
        iyc = iyb ;
        ixb -= iystepx ;
        iyb -= iystepy ;
      } while ( iyb >= y1replim ) ;
    }
    else if ( iyc < y1replim ) {
      do {
        ixb += iystepx ;
        iyb = iyc ;
        iyc += iystepy ;
      } while ( iyc < y1replim ) ;
    }
    ixc = ixb + iystepx ;

    /* The column may not be in this band at all. This can be caused by either the
     * band height being less than the size of pattern bbox, or, by the step of
     * the pattern being large when compared to the size of the pattern bbox,
     * combined with the fact the the steps are not orthogonal and so can step
     * off the left or right hand side of the replication limits.
     * We can only optimise certain cases out here. That is when this column will
     * not touch the replication limits because we've gone off the right hand side
     * and the steps are all positive (note that ixstepx >= 0).
     */
    if ( ixc > x2replim && iystepx >= 0 )
      break ;

    /* The column may not be in this band at all. This can be caused by either the
     * band height being less than the size of pattern bbox, or, by the step of
     * the pattern being large when compared to the size of the pattern bbox,
     * combined with the fact the the steps are not orthogonal and so can step
     * off the left or right hand side of the replication limits.
     * In this variant when compared to the orth case, we can only ignore this
     * column; we have to consider the next column as it may have a different
     * y coordinate (due to ixstepy) which may not be clipped out.
     * Several cases we can consider:
     */
    /* Case 1; column has converged below the replication limits. */
    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        /* Case 2; pattern has converged to the right of the replication limits.
         *         and the steps are all positive.
         */
        if ( iystepx >= 0 )
          break ;
        else {
          /* column has converged to the right of the replication limits so
           * first of all we need to run the inner loop to bring it back in. This
           * may lead to the column being clipped out vertically; picked up later.
           */
          do {
            ixc += iystepx;
            iyc += iystepy;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        ixc += len ;
        if ( ixc < x1replim ) {
          /* Case 3; column has converged to the left of the replication limits.
           *         and the steps are all negative.
           */
          if ( iystepx <= 0 ) {
            continue ;
          }
          else {
            /* column has converged to the left of the replication limits so
             * first of all we need to run the inner loop to bring it back in. This
             * may lead to the column being clipped out vertically; picked up
             * later.
             */
            do {
              ixc += iystepx;
              iyc += iystepy;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        ixc -= len ;
      }
      /* Given a column (ixc1, ixc1+len, iyc) which we know is in the
       * replication limits, we now need to cover in the inner loop all
       * possible y values vertically in this column down the page.
       */
      while ( iyc <= y2replim ) {
        if ( ixc >= x1replim && ixc + len <= x2replim ) {
          DO_SPAN(rb, iyc, ixc , ixc + len );
        }
        /* Given an x position for a replication of the span, we need to clip it. */
        else {
          int32 txs, txe;
          txs = ixc ;
          txe = ixc + len ;
          if ( txs < x1replim ) txs = x1replim;
          if ( txe > x2replim ) txe = x2replim;
          if ( txs <= txe )
            DO_SPAN(rb, iyc, txs , txe );
          else
            if ( (txs > x2replim && iystepx >= 0) ||
                 (txe < x1replim && iystepx <= 0) )
              break; /* early termination, we're outa the inside (ystep) loop */
        }
        ixc += iystepx;
        iyc += iystepy;
      }
    }
    /* The NEXT column may not be in this band at all. This can be caused by either the
     * band height being less than the size of pattern bbox, or, by the step of
     * the pattern being large when compared to the size of the pattern bbox,
     * combined with the fact the the steps are not orthogonal and so can step
     * off the left or right hand side of the replication limits.
     * We can only optimise certain cases out here. That is when the next column will
     * not touch the replication limits because we're off the right hand side
     * and the steps are all negative (note that ixstepx >= 0).
     */
    if ( ixc > x2replim && iystepx <= 0 )
      break;
  }
}

static void patternorthrealblock(render_blit_t *rb,
                                 dcoord ys , dcoord ye, dcoord xs , dcoord xe )
{
  int32 iyb, iyc ;
  SYSTEMVALUE xb, yb, yc;
  dcoord len = xe - xs;
  dcoord height = ye - ys;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  SYSTEMVALUE xstepx = tracker->pPattern->xx;
  SYSTEMVALUE ystepy = tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* For comments on this see equivalent routine patternorthintblock. */

  HQASSERT(xs >= 0 , "Start of span is negative coord in patternorthrealblock") ;
  HQASSERT(ys >= 0 , "Start of span is negative coord in patternorthrealblock") ;
  HQASSERT(ye >= ys, "End of span before start of span in patternorthrealblock") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternorthrealblock") ;

  /* Block itself is replicated, so don't replicate underlying blits. */
  BLOCK_USE_NEXT_BLITS(rb->blits) ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  yb = tracker->ybase + ys; SC_C2D_INT( iyb , yb );
  yc = yb + ystepy ; SC_C2D_INT( iyc , yc ) ;
  if ( iyb + height >= y1replim ) {
    do {
      yc = yb ; iyc = iyb ;
      yb -= ystepy ; SC_C2D_INT( iyb , yb ) ;
    } while ( iyb + height >= y1replim );
  }
  else if ( iyc + height < y1replim ) {
    do {
      yc += ystepy ; SC_C2D_INT( iyc , yc ) ;
    } while ( iyc + height < y1replim );
  }
  if ( iyc > y2replim )
    return ;

  yb = yc ;
  for ( xb = tracker->xbase + xs ; ; xb += xstepx ) {
    int32 ixc1, ixc2;

    SwOftenUnsafe();

    SC_C2D_INT( ixc1 , xb ) ;
    if ( ixc1 > x2replim )
      break;
    ixc2 = ixc1 + len;
    if ( ixc2 < x1replim )
      continue ;
    if ( ixc1 < x1replim )
      ixc1 = x1replim ;
    if ( ixc2 > x2replim )
      ixc2 = x2replim ;

    for ( yc = yb ; ; yc += ystepy ) {
      int32 tys, tye ;
      SC_C2D_INT( iyc , yc ) ;
      tys = iyc ;
      tye = iyc + height ;
      HQASSERT( iyc + height >= y1replim,
                "patternorthrealblock; should be converged already in y" ) ;
      if ( iyc > y2replim )
        break ;
      if ( tys < y1replim )
        tys = y1replim;
      if ( tye > y2replim )
        tye = y2replim;
      do_block_patternreal(rb, tys, tye, ixc1, ixc2, ys, xs, yc, xb);
    }
  }
}

static void patternrealblock(render_blit_t *rb,
                             dcoord ys , dcoord ye, dcoord xs , dcoord xe )
{
  SYSTEMVALUE xb, yb;
  dcoord len = xe - xs;
  dcoord height = ye - ys;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  SYSTEMVALUE xstepx = tracker->pPattern->xx;
  SYSTEMVALUE xstepy = tracker->pPattern->xy;
  SYSTEMVALUE ystepx = tracker->pPattern->yx;
  SYSTEMVALUE ystepy = tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* For comments on this see equivalent routine patternintblock. */

  HQASSERT(xs >= 0 , "Start of span is negative coord in patternrealblock") ;
  HQASSERT(ys >= 0 , "Start of span is negative coord in patternrealblock") ;
  HQASSERT(ye >= ys, "End of span before start of span in patternrealblock") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternrealblock") ;

  /* Block itself is replicated, so don't replicate underlying blits. */
  BLOCK_USE_NEXT_BLITS(rb->blits) ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( xb = tracker->xbase + xs,
          yb = tracker->ybase + ys ; ;
        xb += xstepx, yb += xstepy ) {
    int32 ixc, iyc, iyb;
    SYSTEMVALUE xc, yc;

    SwOftenUnsafe();

    SC_C2D_INT( iyb , yb ) ;
    yc = yb + ystepy ; SC_C2D_INT( iyc , yc ) ;
    if ( iyb + height >= y1replim ) {
      do {
        yc = yb ; iyc = iyb ;
        xb -= ystepx ;
        yb -= ystepy ; SC_C2D_INT( iyb , yb ) ;
      } while ( iyb + height >= y1replim );
    }
    else if ( iyc + height < y1replim ) {
      do {
        xb += ystepx ;
        yb = yc ; iyb = iyc ;
        yc += ystepy ; SC_C2D_INT( iyc , yc ) ;
      } while ( iyc + height < y1replim );
    }
    xc = xb + ystepx ; SC_C2D_INT( ixc , xc ) ;
    if ( ixc > x2replim && ystepx >= 0.0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( ystepx >= 0.0 )
          break ;
        else {
          do {
            xc += ystepx; SC_C2D_INT( ixc , xc ) ;
            yc += ystepy; SC_C2D_INT( iyc , yc ) ;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        xc += len; ixc += len;
        if ( ixc < x1replim ) {
          if ( ystepx <= 0.0 ) {
            continue ;
          }
          else {
            do {
              xc += ystepx; SC_C2D_INT( ixc , xc ) ;
              yc += ystepy; SC_C2D_INT( iyc , yc ) ;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        xc -= len; ixc -= len;
      }
      while ( iyc <= y2replim ) {
        if ( ixc >= x1replim && ixc + len <= x2replim &&
             iyc >= y1replim && iyc + height <= y2replim ) {
          do_block_patternreal(rb, iyc, iyc + height, ixc, ixc + len, ys, xs, yc, xc);
        }
        else {
          int32 txs, txe;
          int32 tys, tye;
          txs = ixc ;
          txe = ixc + len;
          if ( txs < x1replim ) txs = x1replim;
          if ( txe > x2replim ) txe = x2replim;
          if ( txs <= txe ) {
            tys = iyc ;
            tye = iyc + height;
            if ( tys < y1replim ) tys = y1replim;
            if ( tye > y2replim ) tye = y2replim;
            do_block_patternreal(rb, tys, tye, txs, txe, ys, xs, yc, xc);
          }
          else
            if ( (txs > x2replim && ystepx >= 0.0) ||
                 (txe < x1replim && ystepx <= 0.0) )
              break; /* early termination, we're outa the inside (ystep) loop */
        }
        xc += ystepx; SC_C2D_INT( ixc , xc ) ;
        yc += ystepy; SC_C2D_INT( iyc , yc ) ;
      }
    }
    if ( ixc > x2replim && ystepx <= 0.0 )
      break;
  }
}

Bool patternrealtiling(pattern_tracker_t *tracker,
                       pattern_render_callback callback, void *data)
{
  SYSTEMVALUE xb, yb;
  dbbox_t patbounds;
  dcoord xs, ys;
  dcoord len, height;
  SYSTEMVALUE xstepx = tracker->pPattern->xx;
  SYSTEMVALUE xstepy = tracker->pPattern->xy;
  SYSTEMVALUE ystepx = tracker->pPattern->yx;
  SYSTEMVALUE ystepy = tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;
#ifdef DEBUG_BUILD
  int32 pattern_count = 0 ;
#endif

  /* For comments on this see equivalent routine patternintblock. */

  hdlBBox(patternHdl(tracker->pPattern), &patbounds);
  xs = patbounds.x1;
  ys = patbounds.y1;
  len = patbounds.x2 - patbounds.x1;
  height = patbounds.y2 - patbounds.y1;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( xb = tracker->xbase + xs,
          yb = tracker->ybase + ys ; ;
        xb += xstepx, yb += xstepy ) {
    int32 ixc, iyc, iyb;
    SYSTEMVALUE xc, yc;

    SwOftenUnsafe();

    SC_C2D_INT( iyb , yb ) ;
    yc = yb + ystepy ; SC_C2D_INT( iyc , yc ) ;
    if ( iyb + height >= y1replim ) {
      do {
        yc = yb ; iyc = iyb ;
        xb -= ystepx ;
        yb -= ystepy ; SC_C2D_INT( iyb , yb ) ;
      } while ( iyb + height >= y1replim );
    }
    else if ( iyc + height < y1replim ) {
      do {
        xb += ystepx ;
        yb = yc ; iyb = iyc ;
        yc += ystepy ; SC_C2D_INT( iyc , yc ) ;
      } while ( iyc + height < y1replim );
    }
    xc = xb + ystepx ; SC_C2D_INT( ixc , xc ) ;
    if ( ixc > x2replim && ystepx >= 0.0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( ystepx >= 0.0 )
          break ;
        else {
          do {
            xc += ystepx; SC_C2D_INT( ixc , xc ) ;
            yc += ystepy; SC_C2D_INT( iyc , yc ) ;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        xc += len; ixc += len;
        if ( ixc < x1replim ) {
          if ( ystepx <= 0.0 ) {
            continue ;
          }
          else {
            do {
              xc += ystepx; SC_C2D_INT( ixc , xc ) ;
              yc += ystepy; SC_C2D_INT( iyc , yc ) ;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        xc -= len; ixc -= len;
      }
      while ( iyc <= y2replim ) {
        if ( ixc >= x1replim && ixc + len <= x2replim &&
             iyc >= y1replim && iyc + height <= y2replim ) {
#ifdef DEBUG_BUILD
          if ( ++pattern_count >= debug_pattern_first &&
               pattern_count <= debug_pattern_last )
#endif
            if ( !do_callback_patternreal(tracker, callback, data,
                                          iyc, iyc + height, ixc, ixc + len,
                                          ys, xs, yc, xc,
                                          ixc - xs, iyc - ys) )
              return FALSE;
        }
        else {
          int32 txs, txe;
          int32 tys, tye;
          txs = ixc ;
          txe = ixc + len;
          if ( txs < x1replim ) txs = x1replim;
          if ( txe > x2replim ) txe = x2replim;
          if ( txs <= txe ) {
            tys = iyc ;
            tye = iyc + height;
            if ( tys < y1replim ) tys = y1replim;
            if ( tye > y2replim ) tye = y2replim;
#ifdef DEBUG_BUILD
            if ( ++pattern_count >= debug_pattern_first &&
                 pattern_count <= debug_pattern_last )
#endif
              if ( !do_callback_patternreal(tracker, callback, data,
                                            tys, tye, txs, txe,
                                            ys, xs, yc, xc,
                                            ixc - xs, iyc - ys) )
                return FALSE;
          }
          else
            if ( (txs > x2replim && ystepx >= 0.0) ||
                 (txe < x1replim && ystepx <= 0.0) )
              break; /* early termination, we're outa the inside (ystep) loop */
        }
        xc += ystepx; SC_C2D_INT( ixc , xc ) ;
        yc += ystepy; SC_C2D_INT( iyc , yc ) ;
      }
    }
    if ( ixc > x2replim && ystepx <= 0.0 )
      break;
  }
  return TRUE;
}

static void patternorthintblock(render_blit_t *rb,
                                dcoord ys , dcoord ye, dcoord xs , dcoord xe )
{
  int32 iyb, iyc ;
  int32 ixb ;
  dcoord len = xe - xs;
  dcoord height = ye - ys;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  dcoord ixstepx = (dcoord)tracker->pPattern->xx;
  dcoord iystepy = (dcoord)tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* For comments on this see equivalent routine patternorthintspan. The only
   * differences between that routine and this one is that: a) the intial
   * convergence has to be based on the block rather than a span, and, b) that
   * when emitting the block we need to test to see if some of the block is
   * clipped out vertically (as well as horizontally which both cases need).
   */

  HQASSERT(xs >= 0 , "Start of span is negative coord in patternorthintblock") ;
  HQASSERT(ys >= 0 , "Start of span is negative coord in patternorthintblock") ;
  HQASSERT(ye >= ys, "End of span before start of span in patternorthintblock") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternorthintblock") ;

  /* Block itself is replicated, so don't replicate underlying blits. */
  BLOCK_USE_NEXT_BLITS(rb->blits) ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  iyb = tracker->iybase + ys;
  iyc = iyb + iystepy ;
  if ( iyb + height >= y1replim ) {
    do {
      iyc = iyb ;
      iyb -= iystepy ;
    } while ( iyb + height >= y1replim );
  }
  else if ( iyc + height < y1replim ) {
    do {
      iyb = iyc ;
      iyc += iystepy ;
    } while ( iyc + height < y1replim );
  }
  if ( iyc > y2replim )
    return ;

  iyb = iyc ;
  for ( ixb = tracker->ixbase + xs ; ; ixb += ixstepx ) {
    int32 ixc1, ixc2;

    SwOftenUnsafe();

    ixc1 = ixb ;
    if ( ixc1 > x2replim )
      break;
    ixc2 = ixc1 + len ;
    if ( ixc2 < x1replim )
      continue ;
    if ( ixc1 < x1replim )
      ixc1 = x1replim ;
    if ( ixc2 > x2replim )
      ixc2 = x2replim ;

    iyc = iyb ;
    HQASSERT( iyc + height >= y1replim ,
              "patternorthintblock; should be converged already in y" ) ;
    while ( iyc <= y2replim ) {
      if ( iyc >= y1replim && iyc + height <= y2replim ) {
        DO_BLOCK(rb, iyc, iyc + height, ixc1 , ixc2 );
      }
      else {
        int32 tys, tye;
        tys = iyc ;
        tye = iyc + height ;
        if ( tys < y1replim ) tys = y1replim;
        if ( tye > y2replim ) tye = y2replim;
        DO_BLOCK(rb, tys, tye, ixc1 , ixc2 );
      }
      iyc += iystepy;
    }
  }
}

static void patternintblock(render_blit_t *rb,
                            dcoord ys , dcoord ye, dcoord xs , dcoord xe )
{
  int32 ixb, iyb;
  dcoord len = xe - xs;
  dcoord height = ye - ys;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  dcoord ixstepx = (dcoord)tracker->pPattern->xx;
  dcoord ixstepy = (dcoord)tracker->pPattern->xy;
  dcoord iystepx = (dcoord)tracker->pPattern->yx;
  dcoord iystepy = (dcoord)tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;

  /* For comments on this see equivalent routine patternthintspan. The only
   * differences between that routine and this one is that: a) the intial
   * convergence has to be based on the block rather than a span, and, b) that
   * when emitting the block we need to test to see if some of the block is
   * clipped out vertically (as well as horizontally which both cases need).
   */

  HQASSERT(xs >= 0 , "Start of span is negative coord in patternintblock") ;
  HQASSERT(ys >= 0 , "Start of span is negative coord in patternintblock") ;
  HQASSERT(ye >= ys, "End of span before start of span in patternintblock") ;
  HQASSERT(xe >= xs, "End of span before start of span in patternintblock") ;

  /* Block itself is replicated, so don't replicate underlying blits. */
  BLOCK_USE_NEXT_BLITS(rb->blits) ;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( ixb = tracker->ixbase + xs,
          iyb = tracker->iybase + ys; ;
        ixb += ixstepx, iyb += ixstepy ) {
    int32 ixc, iyc;

    SwOftenUnsafe();

    iyc = iyb + iystepy ;
    if ( iyb + height >= y1replim ) {
      do {
        iyc = iyb ;
        ixb -= iystepx ;
        iyb -= iystepy ;
      } while ( iyb + height >= y1replim );
    }
    else if ( iyc + height < y1replim ) {
      do {
        ixb += iystepx ;
        iyb = iyc ;
        iyc += iystepy ;
      } while ( iyc + height < y1replim );
    }
    ixc = ixb + iystepx ;
    if ( ixc > x2replim && iystepx >= 0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( iystepx >= 0 )
          break ;
        else {
          do {
            ixc += iystepx;
            iyc += iystepy;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        ixc += len;
        if ( ixc < x1replim ) {
          if ( iystepx <= 0 ) {
            continue ;
          }
          else {
            do {
              ixc += iystepx;
              iyc += iystepy;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        ixc -= len;
      }
      while ( iyc <= y2replim ) {
        if ( ixc >= x1replim && ixc + len <= x2replim &&
             iyc >= y1replim && iyc + height <= y2replim ) {
          DO_BLOCK(rb, iyc, iyc + height, ixc , ixc + len );
        }
        else {
          int32 txs, txe;
          int32 tys, tye;
          txs = ixc ;
          txe = ixc + len ;
          if ( txs < x1replim ) txs = x1replim;
          if ( txe > x2replim ) txe = x2replim;
          if ( txs <= txe ) {
            tys = iyc ;
            tye = iyc + height ;
            if ( tys < y1replim ) tys = y1replim;
            if ( tye > y2replim ) tye = y2replim;
            DO_BLOCK(rb, tys, tye, txs , txe );
          }
          else
            if ( (txs > x2replim && iystepx >= 0) ||
                 (txe < x1replim && iystepx <= 0) )
              break; /* early termination, we're outa the inside (ystep) loop */
        }
        ixc += iystepx;
        iyc += iystepy;
      }
    }
    if ( ixc > x2replim && iystepx <= 0 )
      break;
  }
}

Bool patterninttiling(pattern_tracker_t *tracker,
                      pattern_render_callback callback, void *data)
{
  int32 ixb, iyb;
  dbbox_t patbounds;
  dcoord xs, ys;
  dcoord len, height;
  dcoord ixstepx = (dcoord)tracker->pPattern->xx;
  dcoord ixstepy = (dcoord)tracker->pPattern->xy;
  dcoord iystepx = (dcoord)tracker->pPattern->yx;
  dcoord iystepy = (dcoord)tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;
#ifdef DEBUG_BUILD
  int32 pattern_count = 0 ;
#endif

  /* For comments on this see equivalent routine patternthintspan. The only
   * differences between that routine and this one is that: a) the intial
   * convergence has to be based on the block rather than a span, and, b) that
   * when emitting the block we need to test to see if some of the block is
   * clipped out vertically (as well as horizontally which both cases need).
   */

  hdlBBox(patternHdl(tracker->pPattern), &patbounds);
  xs = patbounds.x1;
  ys = patbounds.y1;
  len = patbounds.x2 - patbounds.x1;
  height = patbounds.y2 - patbounds.y1;

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( ixb = tracker->ixbase + xs,
          iyb = tracker->iybase + ys; ;
        ixb += ixstepx, iyb += ixstepy ) {
    int32 ixc, iyc;

    SwOftenUnsafe();

    iyc = iyb + iystepy ;
    if ( iyb + height >= y1replim ) {
      do {
        iyc = iyb ;
        ixb -= iystepx ;
        iyb -= iystepy ;
      } while ( iyb + height >= y1replim );
    }
    else if ( iyc + height < y1replim ) {
      do {
        ixb += iystepx ;
        iyb = iyc ;
        iyc += iystepy ;
      } while ( iyc + height < y1replim );
    }
    ixc = ixb + iystepx ;
    if ( ixc > x2replim && iystepx >= 0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( iystepx >= 0 )
          break ;
        else {
          do {
            ixc += iystepx;
            iyc += iystepy;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        ixc += len;
        if ( ixc < x1replim ) {
          if ( iystepx <= 0 ) {
            continue ;
          }
          else {
            do {
              ixc += iystepx;
              iyc += iystepy;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        ixc -= len;
      }
      while ( iyc <= y2replim ) {
        dbbox_t tile;
        if ( ixc >= x1replim && ixc + len <= x2replim &&
             iyc >= y1replim && iyc + height <= y2replim ) {
          bbox_store(&tile, ixc, iyc, ixc + len, iyc + height);
#ifdef DEBUG_BUILD
          if ( ++pattern_count >= debug_pattern_first &&
               pattern_count <= debug_pattern_last )
#endif
            if ( !(*callback)(tracker, data, &tile, ixc - xs, iyc - ys) )
              return FALSE;
        }
        else {
          int32 txs, txe;
          int32 tys, tye;
          txs = ixc ;
          txe = ixc + len ;
          if ( txs < x1replim ) txs = x1replim;
          if ( txe > x2replim ) txe = x2replim;
          if ( txs <= txe ) {
            tys = iyc ;
            tye = iyc + height ;
            if ( tys < y1replim ) tys = y1replim;
            if ( tye > y2replim ) tye = y2replim;
            bbox_store(&tile, txs, tys, txe, tye);
#ifdef DEBUG_BUILD
            if ( ++pattern_count >= debug_pattern_first &&
                 pattern_count <= debug_pattern_last )
#endif
              if ( !(*callback)(tracker, data, &tile, ixc - xs, iyc - ys) )
                return FALSE;
          }
          else
            if ( (txs > x2replim && iystepx >= 0) ||
                 (txe < x1replim && iystepx <= 0) )
              break; /* early termination, we're outa the inside (ystep) loop */
        }
        ixc += iystepx;
        iyc += iystepy;
      }
    }
    if ( ixc > x2replim && iystepx <= 0 )
      break;
  }
  return TRUE;
}

static void patternrealchar(render_blit_t *rb,
                            FORM *formptr, dcoord x, dcoord y )
{
  SYSTEMVALUE xb, yb;
  dcoord len, height;
  dcoord ys, xs;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  SYSTEMVALUE xstepx = tracker->pPattern->xx;
  SYSTEMVALUE xstepy = tracker->pPattern->xy;
  SYSTEMVALUE ystepx = tracker->pPattern->yx;
  SYSTEMVALUE ystepy = tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;
  dbbox_t stepclip;
  /* Cast away the constness; will restore the original clip in the end. */
  dbbox_t *clip = &((render_info_t *)rb->p_ri)->clip;

  /* For comments on this see equivalent routine patternintchar. */

  HQASSERT(formptr, "formptr is NULL in patternrealchar") ;

  ys = y ;
  height = theFormH(*formptr) - 1;
  xs = x ;
  len = theFormW(*formptr) - 1;

  /* Char itself is replicated, so don't replicate underlying blits. */
  HQASSERT(theFormT(*formptr) == FORMTYPE_CACHEBITMAP ||
           theFormT(*formptr) == FORMTYPE_HALFTONEBITMAP,
           "Should not be replication non bitmap form") ;
  CHAR_USE_NEXT_BLITS(rb->blits) ;

  /* the stepping clip is applied relative to the character's position,
   * which is its top left, not that of the pattern cell bbox.  So we
   * subtract the char pos within the cell before using the clip values.
   */
  bbox_offset(clip, -xs, -ys, &stepclip);

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( xb = tracker->xbase + xs,
          yb = tracker->ybase + y ; ;
        xb += xstepx, yb += xstepy ) {
    int32 ixc, iyc, iyb;
    SYSTEMVALUE xc, yc;

    SwOftenUnsafe();

    SC_C2D_INT( iyb , yb ) ;
    yc = yb + ystepy ; SC_C2D_INT( iyc , yc ) ;
    if ( iyb + height >= y1replim ) {
      do {
        yc = yb ; iyc = iyb ;
        xb -= ystepx ;
        yb -= ystepy ; SC_C2D_INT( iyb , yb ) ;
      } while ( iyb + height >= y1replim );
    }
    else if ( iyc + height < y1replim ) {
      do {
        xb += ystepx ;
        yb = yc ; iyb = iyc ;
        yc += ystepy ; SC_C2D_INT( iyc , yc ) ;
      } while ( iyc + height < y1replim );
    }
    xc = xb + ystepx ; SC_C2D_INT( ixc , xc ) ;
    if ( ixc > x2replim && ystepx >= 0.0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( ystepx >= 0.0 )
          break ;
        else {
          do {
            xc += ystepx; SC_C2D_INT( ixc , xc ) ;
            yc += ystepy; SC_C2D_INT( iyc , yc ) ;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        xc += len; ixc += len;
        if ( ixc < x1replim ) {
          if ( ystepx <= 0.0 ) {
            continue ;
          }
          else {
            do {
              xc += ystepx; SC_C2D_INT( ixc , xc ) ;
              yc += ystepy; SC_C2D_INT( iyc , yc ) ;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        xc -= len; ixc -= len;
      }
      while ( iyc <= y2replim ) {
        SwOftenUnsafe();

        bbox_offset(&stepclip, ixc, iyc, clip);
        bbox_intersection_coordinates(clip, x1replim, y1replim, x2replim, y2replim);
        if ( bbox_is_normalised(clip) ) {
          DO_CHAR(rb, formptr, ixc, iyc) ;
        }
        else {
          if ( (clip->x1 > x2replim && ystepx >= 0.0) ||
               (clip->x2 < x1replim && ystepx <= 0.0) )
            break; /* early termination, we're outa the inside (ystep) loop */
        }
        xc += ystepx; SC_C2D_INT( ixc , xc ) ;
        yc += ystepy; SC_C2D_INT( iyc , yc ) ;
      }
    }
    if ( ixc > x2replim && ystepx <= 0.0 )
      break;
  }

  /* replace the clip values correctly, adding the charpos shift back in */
  bbox_offset(&stepclip, xs, ys, clip);
}


static void patternintchar(render_blit_t *rb,
                           FORM *formptr, dcoord x, dcoord y )
{
  int32 ixb, iyb;
  dcoord len, height;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  dcoord ixstepx = (dcoord)tracker->pPattern->xx;
  dcoord ixstepy = (dcoord)tracker->pPattern->xy;
  dcoord iystepx = (dcoord)tracker->pPattern->yx;
  dcoord iystepy = (dcoord)tracker->pPattern->yy;
  dcoord x1replim, x2replim, y1replim, y2replim;
  dcoord ys, xs;
  dbbox_t stepclip;
  /* Cast away the constness; will restore the original clip in the end. */
  dbbox_t *clip = &((render_info_t *)rb->p_ri)->clip;

  /* For comments on this see equivalent routine patternintblock. The only
   * differences between that routine and this one is that we have to set the
   * clipping correctly for the charblt routine. The reason for the latter is
   * that the clipping in the pattern for the character is based around the
   * character itself, and since we move the character around, we also need
   * to move the clip around. Further, this character clipping also needs to
   * be intersected with the replication limits. */

  HQASSERT(formptr, "formptr is NULL in patternintchar") ;

  ys = y ;
  height = theFormH(*formptr) - 1;
  xs = x ;
  len = theFormW(*formptr) - 1;

  /* Char itself is replicated, so don't replicate underlying blits. */
  HQASSERT(theFormT(*formptr) == FORMTYPE_CACHEBITMAP ||
           theFormT(*formptr) == FORMTYPE_HALFTONEBITMAP,
           "Should not be replication non bitmap form") ;
  CHAR_USE_NEXT_BLITS(rb->blits) ;

  /* the stepping clip is applied relative to the character's position,
   * which is its top left, not that of the pattern cell bbox.  So we
   * subtract the char pos within the cell before using the clip values.
   */
  bbox_offset(clip, -xs, -ys, &stepclip);

  bbox_load(&tracker->replim,
            x1replim, y1replim, x2replim, y2replim);

  for ( ixb = tracker->ixbase + xs, iyb = tracker->iybase + ys; ;
        ixb += ixstepx, iyb += ixstepy ) {
    int32 ixc, iyc;

    SwOftenUnsafe();

    iyc = iyb + iystepy ;
    if ( iyb + height >= y1replim ) {
      do {
        iyc = iyb ;
        ixb -= iystepx ;
        iyb -= iystepy ;
      } while ( iyb + height >= y1replim );
    }
    else if ( iyc + height < y1replim ) {
      do {
        ixb += iystepx ;
        iyb = iyc ;
        iyc += iystepy ;
      } while ( iyc + height < y1replim );
    }
    ixc = ixb + iystepx ;
    if ( ixc > x2replim && iystepx >= 0 )
      break ;

    if ( iyc <= y2replim ) {
      if ( ixc > x2replim ) {
        if ( iystepx >= 0 )
          break ;
        else {
          do {
            ixc += iystepx;
            iyc += iystepy;
          } while ( iyc <= y2replim && ixc > x2replim ) ;
        }
      }
      else {
        ixc += len;
        if ( ixc < x1replim ) {
          if ( iystepx <= 0 ) {
            continue ;
          }
          else {
            do {
              ixc += iystepx;
              iyc += iystepy;
            } while ( iyc <= y2replim && ixc < x1replim ) ;
          }
        }
        ixc -= len;
      }
      while ( iyc <= y2replim ) {
        SwOftenUnsafe();

        bbox_offset(&stepclip, ixc, iyc, clip);
        bbox_intersection_coordinates(clip, x1replim, y1replim, x2replim, y2replim);
        if ( bbox_is_normalised(clip) ) {
          DO_CHAR(rb, formptr, ixc, iyc) ;
        }
        else {
          if ( (clip->x1 > x2replim && iystepx >= 0) ||
               (clip->x2 < x1replim && iystepx <= 0) )
            break; /* early termination, we're outa the inside (ystep) loop */
        }
        ixc += iystepx;
        iyc += iystepy;
      }
    }
    if ( ixc > x2replim && iystepx <= 0 )
      break;
  }

  /* replace the clip values correctly, adding the charpos shift back in */
  bbox_offset(&stepclip, xs, ys, clip);
}

static void patterntranslatespan(render_blit_t *rb,
                                 dcoord y, dcoord xs , dcoord xe )
{
  pattern_tracker_t *tracker = rb->p_painting_pattern;

  next_span(rb, y + tracker->tile_offset_y,
            xs + tracker->tile_offset_x, xe + tracker->tile_offset_x);
}

static void patterntranslateblock(render_blit_t *rb,
                                  dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  pattern_tracker_t *tracker = rb->p_painting_pattern;

  next_block(rb, ys + tracker->tile_offset_y, ye + tracker->tile_offset_y,
             xs + tracker->tile_offset_x, xe + tracker->tile_offset_x);
}

static void patterntranslatechar(render_blit_t *rb,
                                 FORM *formptr, dcoord x, dcoord y)
{
  pattern_tracker_t *tracker = rb->p_painting_pattern;
  dbbox_t stepclip;
  dcoord xs, ys;
  /* Cast away the constness; will restore the original clip in the end. */
  dbbox_t *clip = &((render_info_t *)rb->p_ri)->clip;

  /* the stepping clip is applied relative to the character's position,
   * which is its top left, not that of the pattern cell bbox.  So we
   * subtract the char pos within the cell before using the clip values.
   */
  xs = x; ys = y;
  bbox_offset(clip, -xs, -ys, &stepclip);

  /* Translate the char to the current tile's offset. */
  x += tracker->tile_offset_x;
  y += tracker->tile_offset_y;

  bbox_offset(&stepclip, x, y, clip);
  bbox_intersection(clip, &tracker->replim, clip);
  if ( bbox_is_normalised(clip) )
    next_char(rb, formptr, x, y);

  /* replace the clip values correctly, adding the charpos shift back in */
  bbox_offset(&stepclip, xs, ys, clip);
}

#define patternorthintchar   patternintchar
#define patternorthrealchar  patternrealchar

#define patternint32span     patternintspan
#define patternorthint32span patternorthintspan

#define patternint32block     patternintblock
#define patternorthint32block patternorthintblock

#define patternint32char     patternintchar
#define patternorthint32char patternorthintchar

void pattern_replicators_builtin(surface_t *surface)
{
  blit_slice_t *slice ;

  /* Orthogonal replication blits */

  slice = &surface->patternreplicateblits[BLT_CELL_ORTHOGONAL][BLT_TILE_NONE] ;
  slice->spanfn = next_span ;
  slice->blockfn = next_block ;
  slice->charfn = next_char ;
  slice->imagefn = next_imgblt ;

  slice = &surface->patternreplicateblits[BLT_CELL_ORTHOGONAL][BLT_TILE_CONSTANT] ;
  slice->spanfn = patternorthintspan ;
  slice->blockfn = patternorthintblock ;
  slice->charfn = patternorthintchar ;
  slice->imagefn = imagebltn ;

  slice = &surface->patternreplicateblits[BLT_CELL_ORTHOGONAL][BLT_TILE_ACCURATE] ;
  slice->spanfn = patternorthrealspan ;
  slice->blockfn = patternorthrealblock ;
  slice->charfn = patternorthrealchar ;
  slice->imagefn = imagebltn ;

  slice = &surface->patternreplicateblits[BLT_CELL_ORTHOGONAL][BLT_TILE_FAST] ;
  slice->spanfn = patternorthint32span ;
  slice->blockfn = patternorthint32block ;
  slice->charfn = patternorthint32char ;
  slice->imagefn = imagebltn ;

  /* Non-orthogonal replication blits */

  slice = &surface->patternreplicateblits[BLT_CELL_NONORTH][BLT_TILE_NONE] ;
  slice->spanfn = next_span ;
  slice->blockfn = next_block ;
  slice->charfn = next_char ;
  slice->imagefn = next_imgblt ;

  slice = &surface->patternreplicateblits[BLT_CELL_NONORTH][BLT_TILE_CONSTANT] ;
  slice->spanfn = patternintspan ;
  slice->blockfn = patternintblock ;
  slice->charfn = patternintchar ;
  slice->imagefn = imagebltn ;

  slice = &surface->patternreplicateblits[BLT_CELL_NONORTH][BLT_TILE_ACCURATE] ;
  slice->spanfn = patternrealspan ;
  slice->blockfn = patternrealblock ;
  slice->charfn = patternrealchar ;
  slice->imagefn = imagebltn ;

  slice = &surface->patternreplicateblits[BLT_CELL_NONORTH][BLT_TILE_FAST] ;
  slice->spanfn = patternint32span ;
  slice->blockfn = patternint32block ;
  slice->charfn = patternint32char ;
  slice->imagefn = imagebltn ;

  /* High-level tiling translation blits */

  slice = &surface->patterntranslateblits ;
  slice->spanfn = patterntranslatespan ;
  slice->blockfn = patterntranslateblock ;
  slice->charfn = patterntranslatechar ;
  slice->imagefn = imagebltn ;
}

/* Log stripped */
