/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:sccompat.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Device resolution pixel-touching scan conversion for fills, compatible
 * with original Harlequin Bresenham rules.
 *
 * Function definitions to be inlined in the DDA scan converter, for device
 * resolution rendering. The DDA scan converter is used for all
 * implementations of scan conversion in the RIP. These macros implement the
 * pixel-touching scan conversion rule identically to the old Bresenham's
 * algorithm code. They optimise the stepping for shallow gradient lines by
 * performing a half-step the first time a line is encountered (moving the
 * current point from the centre of the span intersection with the scanline
 * to the transition point between two scanlines), full steps for the
 * intermediate scanlines, and another half step when the line terminates.
 */

#include "core.h"
#include "swoften.h"
#include "hqbitops.h"

#include "bitblts.h"
#include "bitblth.h"
#include "ndisplay.h"
#include "often.h"
#include "devops.h"
#include "render.h"
#include "tables.h"

#include "spanlist.h"
#include "surface.h"
#include "scanconv.h"
#include "scpriv.h"
#include "scmerge.h"

/*---------------------------------------------------------------------------*/
/** If span coalescing is on for a scanline, spans are accumulated in a
    spanlist by a layered blit function. We use the private data for coalesce
    blit functions to store the spanlist pointer, and unpack it into the
    dda_info_t for fast access. Support for coalescing pixels is simplified if
    we can flush the spans at the end of a line, rather than having to save
    the render info and detect a change of line in the blit function. */
typedef struct {
  render_blit_t *rb ;
  dcoord left, right ;
  dbbox_t clip ;
  surface_line_update_fn *update ;
  surface_handle_t surface_handle ;
  spanlist_t *mergedspans;
} dda_info_t ;

/*---------------------------------------------------------------------------*/
/** Add a full gradient step onto the DDA, possibly adjusting by a half step
   if on the first line (leaving the current position at the transition point
   between scanlines). */
static inline void step_to_transition(NBRESS *thread, dcoord denom,
                                      dcoord *xp, dcoord *xep,
                                      dcoord *ox, dcoord *oxe2)
{
  dcoord x = *xp, xe = *xep ;

  *ox = x ;
  *oxe2 = xe + xe ;
  if ( (thread->nmindy & 1) == 0 ) {
    dcoord si = thread->si, sf = thread->sf ;
    /* We're adjusted to a pixel centre. We want to be adjusted to the
       edge halfway between two scanlines, so we can easily work out the
       span limits covered on this scanline. We can subtract half the
       gradient step before the normal case adds the full gradient step.
       This effectively does a half step for the first scanline. The
       final scanline will be adjusted for a matching half span. */
    HQASSERT((denom & 1) == 0, "Gradient denominator is not even") ;
    if ( (si & 1) != 0 ) {
      /* Ensure division gives floor(si/2), which includes a compensating
         term because we should be subtracting rather than adding 0.5 for
         the carry if si is negative. If si is positive it still works. */
      si -= 1 ;
      sf += denom ;
    }
    HQASSERT((sf & 1) == 0, "Gradient fraction is not even") ;
    x -= si / 2 + 1 ; /* Divide because signed; should optimise to shift */
    xe += denom - (sf >> 1) ;
    DDA_SCAN_NORMALISE(denom, x, xe) ;
    if ( thread->nmindy != denom ) {
      /* This is not the first scanline, so adjust the initial position
         to the new span edge. This will happen on the first line after
         rupdatebress() is used to converge for a band or clip
         boundary. */
      *ox = x ;
      *oxe2 = xe + xe ;
    }
    thread->nmindy += 1 ;
  }
  x += thread->si ;
  xe += thread->sf ;
  DDA_SCAN_NORMALISE(denom, x, xe) ;

  *xp = x ;
  *xep = xe ;
}

static inline void one_span(dda_info_t *info, dcoord line,
                            dcoord left, dcoord right)
{
  /* Blit span. Use inline clip max/min operations to avoid branches. */
  HQASSERT(sizeof(left) == sizeof(int32) && sizeof(right) == sizeof(int32),
           "Inline max/min operations use wrong intermediate size") ;

  /* Parameter order is carefully chosen so that pointer dereference is only
     expanded once in a release RIP. */
  INLINE_MAX32(left, info->clip.x1, left) ;
  INLINE_MIN32(right, right, info->clip.x2) ;

  if ( left > right )
    return ;

  HQASSERT(left <= right, "Span limits out of order") ;
  DO_SPAN(info->rb, line, left, right) ;
}

/** Step the first thread of a scanline. This can simply set the span limits
   rather than check for extensions. */
static inline void one_step_set(NBRESS *thread, dda_info_t *span)
{
  dcoord x = thread->u1.ncx, xe = thread->xe ;
  dcoord denom = thread->denom ;

  HQASSERT(denom != 0, "Horizontal in span routine") ;
  HQASSERT(thread->nmindy >= 2, "Final span in step routine") ;

  if ( !thread->ntype ) {
    dcoord ox, oxe2 ;

    step_to_transition(thread, denom, &x, &xe, &ox, &oxe2) ;

    /* All pixel centres from the original position to the transition
       point are included in the span. If this is the first scanline for the
       thread, the original pixel centre position is used, otherwise we round
       the start up or down appropriately. The end pixel is always rounded
       down or up. */
    if ( thread->si < 0 ) { /* Negative shallow gradient */
      span->right = ox ;
      if ( oxe2 <= denom ) {
        if ( thread->nmindy <= denom )
          span->right -= 1 ;
      }
      span->left = x ;
      if ( xe + xe > denom )
        span->left += 1 ;
    } else { /* Positive shallow gradient */
      span->left = ox ;
      if ( oxe2 >= denom ) {
        if ( thread->nmindy < denom )
          span->left += 1 ;
      }
      span->right = x ;
      if ( xe + xe < denom )
        span->right -= 1 ;
    }
  } else {
    /* Steep gradient line does not cover more than the pixel in which it
       originated. It uses the pixel centres rather than the edges. */
    dcoord tx = x ;

    /* Since sf >= 0, testing si|sf > 0 tests if this is a positive gradient,
       but not a vertical line; we could dispense with being so picky,
       because vertical lines will always have xe = dy, which is non-zero. */
    if ( (thread->si|thread->sf) > 0 && xe == 0 ) {
      /* Positive steep gradient, exactly on pixel boundary rounds down. */
      tx -= 1 ;
    }

    span->left = span->right = tx ;

    x += thread->si ;
    xe += thread->sf ;
    DDA_SCAN_NORMALISE(denom, x, xe) ;
  }

  thread->u1.ncx = x ;
  thread->xe = xe ;
  thread->nmindy -= 2 ;
}

/** Extend the span for an interior or final thread of a filled area. In
   device-resolution rendering, all threads need checking in case they
   crossover the boundary threads of the fill within a pixel of the scanline
   and extend the span limits. */
static inline void one_step_extend(NBRESS *thread, dda_info_t *span)
{
  dcoord x = thread->u1.ncx, xe = thread->xe ;
  dcoord denom = thread->denom ;

  HQASSERT(denom != 0, "Horizontal in span routine") ;
  HQASSERT(thread->nmindy >= 2, "Final span in step routine") ;

  if ( !thread->ntype ) {
    dcoord ox, oxe2 ;

    step_to_transition(thread, denom, &x, &xe, &ox, &oxe2) ;

    /* All pixel centres from the original position to the transition
       point are included in the span. If this is the first scanline for the
       thread, the original pixel centre position is used, otherwise we round
       the start up or down appropriately. The end pixel is always rounded
       down or up. */
    if ( thread->si < 0 ) { /* Negative shallow gradient */
      if ( ox > span->right ) {
        span->right = ox ;
        if ( oxe2 <= denom ) {
          if ( thread->nmindy < denom )
            span->right -= 1 ;
        }
      }
      if ( x < span->left ) {
        span->left = x ;
        if ( xe + xe > denom )
          span->left += 1 ;
      }
    } else { /* Positive shallow gradient */
      if ( ox < span->left ) {
        span->left = ox ;
        if ( oxe2 >= denom ) {
          if ( thread->nmindy < denom )
            span->left += 1 ;
        }
      }
      if ( x > span->right ) {
        span->right = x ;
        if ( xe + xe < denom )
          span->right -= 1 ;
      }
    }
  } else {
    /* Steep gradient line does not cover more than the pixel in which it
       originated. It uses the pixel centres rather than the edges. */
    dcoord tx = x ;

    /* Since sf >= 0, testing si|sf > 0 tests if this is a positive gradient,
       but not a vertical line; we could dispense with being so picky,
       because vertical lines will always have xe = dy, which is non-zero. */
    if ( (thread->si|thread->sf) > 0 && xe == 0 ) {
      /* Positive steep gradient, exactly on pixel boundary rounds down. */
      tx -= 1 ;
    }

    if ( tx > span->right )
      span->right = tx ;
    else if ( tx < span->left )
      span->left = tx ;

    x += thread->si ;
    xe += thread->sf ;
    DDA_SCAN_NORMALISE(denom, x, xe) ;
  }

  thread->u1.ncx = x ;
  thread->xe = xe ;
  thread->nmindy -= 2 ;
}

static inline void one_final(NBRESS *thread, dda_info_t *span)
{
  dcoord si, sf, denom ;
  dcoord ox, oxe ;

  HQASSERT(thread, "No thread for horizontal") ;
  HQASSERT(thread->nmindy < 2, "Not final span in final routine") ;

  si = thread->si ;
  sf = thread->sf ;
  denom = thread->denom ;

  ox = thread->u1.ncx ;
  oxe = thread->xe ;

  if ( denom != 0 ) {
    /* Not a horizontal line. Horizontal lines will add the gradient to the
       current position to find the final position. Non-horizontals have
       already been stepped to either the exact end, or half a span from the
       end. */
    if ( !thread->ntype ) {
      /* Divide the gradient in two, ready for a half step to the final
         position to match the half step on the first scanline of the thread.
         It is possible that there was no half step, because all of the
         thread except the final scanline was clipped out. In that case, we
         need to back up half a step, so we need the gradient divided
         anyway. */
      if ( (si & 1) != 0 ) {
        /* Ensure division gives floor(si/2), which includes a compensating
           term because we should be subtracting rather than adding 0.5 for
           the carry if si is negative. If si is positive it still works. */
        si -= 1 ;
        sf += denom ;
      }

      HQASSERT((sf & 1) == 0, "Gradient fraction is not even") ;
      si /= 2 ; /* Divide because signed; should optimise to shift */
      sf >>= 1 ;

      if ( thread->nmindy == 0 ) { /* Back old position up half a step */
        ox -= si + 1 ;
        oxe += denom - sf ;
        DDA_SCAN_NORMALISE(denom, ox, oxe) ;
      } else { /* Finish the thread */
        HQASSERT(thread->nmindy == 1,
                 "Finishing thread with too much to do") ;
        thread->u1.ncx += si ;
        thread->xe += sf ;
        DDA_SCAN_NORMALISE(denom, thread->u1.ncx, thread->xe) ;
      }

      oxe <<= 1 ;
      if ( si < 0 ) {
        if ( oxe <= denom )
          ox -= 1 ;

        span->left = thread->u1.ncx ;
        span->right = ox ;
      } else {
        if ( oxe >= denom )
          ox += 1 ;

        span->left = ox ;
        span->right = thread->u1.ncx ;
      }
    } else {
      /* Steep gradient lines should already have been stepped to their end. */
      HQASSERT(thread->nmindy == 0, "Steep line not stepped to end") ;
      span->left = span->right = thread->u1.ncx ;
    }
  } else {
    HQASSERT(sf == 0, "Horizontal line fraction not zero") ;
    thread->u1.ncx = ox + si ;
    INLINE_MINMAX32(span->left, span->right, thread->u1.ncx, ox) ;
  }
}

/*---------------------------------------------------------------------------*/
/** Fill start stores clipping flag and sets up sentinel span value which will
   never be merged, so we don't need to test if we're at the start of the
   span array. */
static inline void dda_fill_start(dda_info_t *info, dcoord line,
                                  NFILLOBJECT *nfill, int32 rule)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NFILLOBJECT *, nfill) ;
  UNUSED_PARAM(int32, rule) ;
}

static inline void dda_fill_end(dda_info_t *info,
                                dcoord line, NFILLOBJECT *nfill)
{
  UNUSED_PARAM(NFILLOBJECT *, nfill) ;

  HQASSERT(line >= info->clip.y1, "Scanline smaller after fill");
  if ( line <= info->clip.y2 && info->update ) {
    /* Make sure we update the clipping for scanlines in the band succeeding
       the fill */
    updatebandclip(info->update, info->surface_handle,
                   info->rb, line, info->clip.y2);
  }
}

/** Function to start a set of scanlines. This initialises the last
   stored value */
static inline void dda_lines_start(dda_info_t *info, dcoord line,
                                   NBRESS **start, NBRESS **end)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;
}

/** Function to return the number of lines to step at once, 0 < nlines
   <= maxlines. Note that DDA_LINE_STEP is only called when stepping multiple
   scanlines; it may not be called before calls to DDA_THREAD_FINAL. */
static inline void dda_line_step(dda_info_t *info,
                                 dcoord line, dcoord *maxlines,
                                 NBRESS **start, NBRESS **end)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;

  *maxlines = 1 ;
}

/** Function to complete a set of scanlines. */
static inline void dda_line_end(dda_info_t *info,
                                dcoord line, dcoord nlines,
                                NBRESS **start, NBRESS **end)
{
  render_blit_t *rb ;

  UNUSED_PARAM(dcoord, nlines) ;
  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;

  rb = info->rb ;
  span_merge_flush(rb, info->mergedspans, line) ;
  if ( info->update )
    (*info->update)(info->surface_handle, rb, line) ;

  rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr, theFormL(*rb->outputform)) ;
  rb->ymaskaddr = BLIT_ADDRESS(rb->ymaskaddr, theFormL(*rb->clipform)) ;
}

static inline void dda_thread_first(dda_info_t *info,
                                    dcoord line, dcoord nlines,
                                    NBRESS *thread)
{
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;
  one_step_set(thread, info) ;
}

static inline void dda_thread_internal(dda_info_t *info,
                                       dcoord line, dcoord nlines,
                                       NBRESS *thread)
{
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;

  one_step_extend(thread, info) ;
}

static inline void dda_thread_last(dda_info_t *info,
                                   dcoord line, dcoord nlines,
                                   NBRESS *thread)
{
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;

  one_step_extend(thread, info) ;
  one_span(info, line, info->left, info->right) ;
}

static inline void dda_clipped_last(dda_info_t *info,
                                    dcoord line, dcoord nlines)
{
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;

  one_span(info, line, info->left, info->clip.x2) ;
}

static inline void dda_thread_stroke(dda_info_t *info,
                                     dcoord line, dcoord nlines,
                                     NBRESS *thread)
{
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;

  one_step_set(thread, info) ;
  one_span(info, line, info->left, info->right) ;
}

static inline void dda_thread_final(dda_info_t *info,
                                    dcoord line,
                                    NBRESS **start, NBRESS **end,
                                    NBRESS *thread, Bool more)
{
  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;
  UNUSED_PARAM(Bool, more) ;

  one_final(thread, info) ;
  span_merge_on(info->rb, info->mergedspans);
  one_span(info, line, info->left, info->right) ;
}

static inline void dda_thread_sort_swapped(dda_info_t *info,
                                           dcoord line,
                                           NBRESS *left, NBRESS *right,
                                           Bool maybefinal)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NBRESS *, left) ;
  UNUSED_PARAM(NBRESS *, right) ;
  UNUSED_PARAM(Bool, maybefinal) ;
}

/** \brief Decide whether to turn on spanlist merging.
 *
 * Because of the number of pixels than can be lit on a single scanline for a
 * shallow line, a simple crossing test may not be enough. We really want to
 * work out if it will cross either entering of leaving the scanline, so need
 * to take the slopes into account, whilst keeping the test cheap. If we are
 * a little bit too cautious, it will just mean we thing about scanlist
 * merging for scanlines near the crossing point, which won't make any
 * noticeable performance impact.
 */
static inline void dda_thread_sort_overlap(dda_info_t *info,
                                           dcoord line,
                                           NBRESS *left, NBRESS *right,
                                           Bool maybefinal)
{
  UNUSED_PARAM(dcoord, line) ;

  /* Only test if threads may touch if this might be the last pass of the
     sort, otherwise the test will be repeated sometime later. */
  if ( maybefinal ) {
    register dcoord lsi, rsi ;

    /* If the rightmost pixel of the left thread might overlap the leftmost
       pixel of the right thread, turn on span merging.

       The step taken in step_to_transition() is the gradient si (but si may
       be +ve or -ve), and there may be another +1 if xe has rolled over.
       Then we may adjust span->left or span->right by +/-1 depending on the
       sign/steepness of the slope in one_step_set() or one_step_extend().

       Note that we are interested whether two filled sections overlap,
       so the gradient for our left thread is calculated as the right thread
       of a filled section, and the gradient for our right thread is
       calculated as the left thread of a filled section.

       This gives the cases:

       Left    Right   Visual  Count as touching when:
       -ve     -ve     //      ncx_L (-1) >= ncx_R + si_R (+1) (+1)
       -ve     +ve     /\      ncx_L (-1) == ncx_R (+1)
       +ve     -ve     \/      ncx_L + si_L (+1) (-1) >= ncx_R + si_R (+1) (+1)
       +ve     +ve     \\      ncx_L + si_L (+1) (-1) >= ncx_R (+1)

       where (+1) indicates a conditional add of one.

       This gives us the worst case tests:
                       //      ncx_L >= ncx_R + si_R
                       /\      ncx_L == ncx_R
                       \/      ncx_L + si_L +1 >= ncx_R + si_R
                       \\      ncx_L + si_L +1 >= ncx_R

       which reduces to the general

         ncx_L + MAX(0,si_L) + 1 >= ncx_R + MIN(0,si_L)

       This is a conservative test, it will turn on for some lines which
       don't overlap, however the overall performance impact should be low.
    */
    INLINE_MAX32(lsi, left->si, 0) ;
    INLINE_MIN32(rsi, 0, right->si) ;

    if ( left->u1.ncx + lsi + 1 >= right->u1.ncx + rsi )
      span_merge_on(info->rb, info->mergedspans);
  }
}

/* Include the body definitions for DDA scan conversion */
#include "ddafill.h"

/** Finally, a function to draw the intersection of a fill and a band. */
void scanconvert_compat(render_blit_t *rb, NFILLOBJECT *nfill, int32 therule,
                        spanlist_t *mergedspans)
{
  register dcoord scanline ;
  uint32 wupdate , wclipupdate ;
  NBRESS **startptr , **limitptr ;
  dda_info_t info ;

  HQASSERT(nfill->converter == SC_RULE_HARLEQUIN,
           "Scan conversion rule should be set for compatibility.") ;

  info.clip = rb->p_ri->clip ;
  info.rb = rb ;
  info.update = rb->p_ri->surface ? rb->p_ri->surface->line_update : NULL ;
  info.surface_handle = rb->p_ri->p_rs->surface_handle ;
  info.mergedspans = mergedspans ;

  HQASSERT(nfill->nexty <= info.clip.y2, "next y out of phase") ;
  nfill->nexty = info.clip.y2 + 1 ;

/* Set up start & end pointers. */
  startptr = nfill->startnptr ;
  limitptr = nfill->thread + nfill->nthreads ;

  if ( startptr >= limitptr ) {
    /* Fill finished in an earlier band; make sure we update the clipping */
    if ( info.update ) {
      updatebandclip(info.update, info.surface_handle,
                     rb, info.clip.y1, info.clip.y2) ;
    }
    return ;
  }

  HQASSERT(sizeof((startptr[0])->ny1) == sizeof(int32) &&
           sizeof(info.clip.y1) == sizeof(int32),
           "Inline max/min operations use wrong intermediate size") ;
  /* Parameter order is carefully chosen so that pointer dereference is only
     expanded once in a release RIP. */
  INLINE_MAX32(scanline, (startptr[0])->ny1, info.clip.y1) ;

  if ( scanline > info.clip.y2 ) {
    /* Fill commences in a later band; make sure we update the clipping */
    if ( info.update ) {
      updatebandclip(info.update, info.surface_handle,
                     rb, info.clip.y1, info.clip.y2) ;
    }
    return ;
  }

  if ( scanline > info.clip.y1 ) {
    /* Make sure we update the clipping for scanlines in the band preceding
       the fill */
    if ( info.update ) {
      updatebandclip(info.update, info.surface_handle,
                     rb, info.clip.y1, scanline - 1) ;
    }
  }

  /* Bitblt variables. */
  wupdate = theFormL(*rb->outputform) ;
  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
    wupdate * (uint32)(scanline - theFormHOff(*rb->outputform) - rb->y_sep_position));

  wclipupdate = theFormL(*rb->clipform) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
    wclipupdate * (uint32)(scanline - theFormHOff(*rb->clipform) - rb->y_sep_position)) ;

  HQTRACE((debug_scanconv & DEBUG_SCAN_INFO) != 0,
          ("scanline=%d, y1clip=%d, y2clip=%d, ylineaddr=%x\n",
           scanline, info.clip.y1, info.clip.y2, rb->ylineaddr));
  HQTRACE((debug_scanconv & DEBUG_SCAN_INFO) != 0,
          ("About to loop down scanlines, wupdate=%d\n",
           wupdate));

  ddafill(&info, nfill, therule, info.clip.y1, info.clip.y2) ;
}

/* Log stripped */
