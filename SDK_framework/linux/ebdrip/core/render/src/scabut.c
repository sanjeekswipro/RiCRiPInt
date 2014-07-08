/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:scabut.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Device resolution half-open pixel centre scan conversion for fills.
 *
 * This implements the XPS rule, which is very similar to OpenGL and DirectX.
 * Pixels are lit if the centre of the pixel is included in the shape, or if
 * the centre is touched by the contour and the shape extends beyond the
 * centre in the positive x or y direction. The intention of this rule is so
 * that abutting fills sharing the same coordinates on an edge do not
 * overlap, and do not have gaps between them.
 *
 * The coordinates of the fill have already been rounded to integers, which
 * we treat as pixel centres. The rule is achieved by ignoring horizontals,
 * and including every pixel whose centre is at or beyond the left end of a
 * span, up to but not including any pixel whose right end is at or beyond
 * the end of a span. Ignoring horizontals also has the advantage that spans
 * will be produced in order across a line.
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
#include "surface.h"

#include "scanconv.h"
#include "scpriv.h"

/*---------------------------------------------------------------------------*/
typedef struct {
  render_blit_t *rb ;
  dcoord left, right ;
  dbbox_t clip ;
  surface_line_update_fn *update ;
  surface_handle_t surface_handle ;
} dda_info_t ;

/*---------------------------------------------------------------------------*/
static inline void one_span(dda_info_t *info, dcoord line,
                            dcoord left, dcoord right)
{
  /* Blit span. Use inline clip max/min operations to avoid branches. */
  HQASSERT(sizeof(dcoord) == sizeof(int32),
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
       the fill. */
    updatebandclip(info->update, info->surface_handle,
                   info->rb, line, info->clip.y2);
  }
}

/** Function to start a set of scanlines. This initialises the last stored
    value. */
static inline void dda_lines_start(dda_info_t *info, dcoord line,
                                   NBRESS **start, NBRESS **end)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;
}

/** Function to return the number of lines to step at once, 0 < nlines <=
   maxlines. Note that DDA_LINE_STEP is only called when stepping multiple
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
  if ( info->update )
    (*info->update)(info->surface_handle, rb, line) ;

  rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr, theFormL(*rb->outputform)) ;
  rb->ymaskaddr = BLIT_ADDRESS(rb->ymaskaddr, theFormL(*rb->clipform)) ;
}

static inline void dda_thread_first(dda_info_t *info,
                                    dcoord line, dcoord nlines,
                                    NBRESS *thread)
{
  dcoord x = thread->u1.ncx, xe = thread->xe ;
  dcoord denom = thread->denom ;

  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;
  HQASSERT(denom != 0, "Horizontal in span routine") ;
  HQASSERT(thread->nmindy >= 2, "Final span in step routine") ;

  /* We only check on scanlines, not half scanlines like the pixel-touching
     rule. So, we can simply check if the error is greater than 0.5 for the
     first thread of the scanline. */
  info->left = x ;
  if ( xe > (denom >> 1) )
    info->left += 1 ;

  info->right = info->left ;

  x += thread->si ;
  xe += thread->sf ;
  DDA_SCAN_NORMALISE(denom, x, xe) ;

  thread->u1.ncx = x ;
  thread->xe = xe ;
  thread->nmindy -= 2 ;
}

static inline void dda_thread_internal(dda_info_t *info,
                                       dcoord line, dcoord nlines,
                                       NBRESS *thread)
{
  /* Internal threads just need the DDA stepping. */
  dcoord x = thread->u1.ncx, xe = thread->xe ;
  dcoord denom = thread->denom ;

  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;

  x += thread->si ;
  xe += thread->sf ;
  DDA_SCAN_NORMALISE(denom, x, xe) ;

  thread->u1.ncx = x ;
  thread->xe = xe ;
  thread->nmindy -= 2 ;
}

static inline void dda_thread_last(dda_info_t *info,
                                   dcoord line, dcoord nlines,
                                   NBRESS *thread)
{
  dcoord x = thread->u1.ncx, xe = thread->xe ;
  dcoord denom = thread->denom ;

  UNUSED_PARAM(dcoord, nlines) ;

  HQASSERT(nlines == 1, "Not updating by a single scanline") ;
  HQASSERT(denom != 0, "Horizontal in span routine") ;
  HQASSERT(thread->nmindy >= 2, "Final span in step routine") ;

  /* We only check on scanlines, not half scanlines like the pixel-touching
     rule. So, we can simply check if the error is less than 0.5 for the
     first thread of the scanline. */
  info->right = x ;
  if ( xe <= (denom >> 1) )
    info->right -= 1 ;

  x += thread->si ;
  xe += thread->sf ;
  DDA_SCAN_NORMALISE(denom, x, xe) ;

  thread->u1.ncx = x ;
  thread->xe = xe ;
  thread->nmindy -= 2 ;

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
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(dcoord, nlines) ;
  UNUSED_PARAM(NBRESS *, thread) ;

  HQFAIL("Half-open render should not be called on zero-width lines") ;
}

static inline void dda_thread_final(dda_info_t *info,
                                    dcoord line,
                                    NBRESS **start, NBRESS **end,
                                    NBRESS *thread, Bool more)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;
  UNUSED_PARAM(Bool, more) ;

  /* Non-horizontals have already been stepped. */
  if ( thread->denom == 0 ) {
    HQASSERT(thread->sf == 0,
             "Horizontal thread has non-zero fractional gradient") ;
    thread->u1.ncx += thread->si ;
  }
}

/* Thread sorting doesn't do anything for tesselating renderer. */
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

/* Thread adjacency doesn't need anything for tesselating renderer. */
static inline void dda_thread_sort_overlap(dda_info_t *info,
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


/* Include the body definitions for DDA scan conversion */
#include "ddafill.h"


/** Finally, a function to draw the intersection of a fill and a band. */
void scanconvert_tesselate(render_blit_t *rb, NFILLOBJECT *nfill, int32 therule,
                           spanlist_t *mergedspans)
{
  register dcoord scanline ;
  uint32 wupdate , wclipupdate ;
  NBRESS **startptr , **limitptr ;
  dda_info_t info ;

  UNUSED_PARAM(spanlist_t *, mergedspans) ;

  HQASSERT(nfill->converter == SC_RULE_TESSELATE,
           "Scan conversion rule should be set for tesselation.") ;

  info.clip = rb->p_ri->clip ;
  info.rb = rb ;
  info.update = rb->p_ri->surface ? rb->p_ri->surface->line_update : NULL ;
  info.surface_handle = rb->p_ri->p_rs->surface_handle ;
  HQASSERT(!DOING_BLITS(rb->blits, COALESCE_BLIT_INDEX),
           "Coalescing blit not necessary when using tesselating scan conversion") ;

  HQASSERT((therule & (~(ISCLIP|CLIPPED_RHS|SPARSE_NFILL))) == NZFILL_TYPE ||
           (therule & (~(ISCLIP|CLIPPED_RHS|SPARSE_NFILL))) == EOFILL_TYPE,
           "therule should be NZFILL_TYPE or EOFILL_TYPE");
  /* until just now, literal 1's and 0's were used for the rule, and nsidetst.c
   * had it backwards.  So they're now symbolics, and NZFILL_TYPE isn't 1 to
   * allow this assert to find any bits I've missed (see task 4565).
   */

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

  scanline = ( startptr[ 0 ] )->ny1 ;
  if ( scanline < info.clip.y1 )
    scanline = info.clip.y1 ;

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

#if defined(DEBUG_BUILD)
  /* Don't rely on these values being set after the end of a fill. ymaskaddr
     may not be updated correctly if the fill terminates before the end of
     the clip area, and if the fill is a clip it will be set to the end of
     the band. */
  rb->ylineaddr = NULL ;
  rb->ymaskaddr = NULL ;
#endif
}

/* Log stripped */
