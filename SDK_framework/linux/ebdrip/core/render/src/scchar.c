/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:scchar.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Sub-pixel scan conversion for (cached) characters.
 */

#include "core.h"
#include "swoften.h"
#include "hqbitops.h"

#include "bitblts.h"
#include "bitblth.h"
#include "ndisplay.h"
#include "often.h"
#include "render.h"

#include "scanconv.h"
#include "scpriv.h"

/** Special blit, similar to bitclip1 but using the inverse of the clip mask.
   For use in character rendering, the clip mask is set up to point at the
   previous or next line, so we can do dropout spans only if they have not
   already been done. */
static void biticlip1(render_blit_t *rb,
                      register dcoord y , register dcoord xs , dcoord xe )
{
  register blit_t temp ;
  register blit_t *formptr ;
  register blit_t *clipptr ;

  UNUSED_PARAM(dcoord, y);

/* Find left most integer address. */
  xe = xe - xs + 1 ;

  HQASSERT(rb->x_sep_position == 0 &&
           rb->y_sep_position == 0,
           "Separation imposition offsets should be zero in character") ;

  formptr = BLIT_ADDRESS(rb->ylineaddr, BLIT_OFFSET(xs)) ;
  clipptr = BLIT_ADDRESS(rb->ymaskaddr, BLIT_OFFSET(xs)) ;
  xs = xs & BLIT_MASK_BITS ;

/* Partial left-span. */
  if ( xs ) {
    xe = BLIT_WIDTH_BITS - xs - xe ;
    temp = SHIFTRIGHT( ALLONES , xs ) ;

/* Doesn't cross word border. */
    if ( xe >= 0 ) {
      temp &= ( SHIFTLEFT( ALLONES , xe )) ;
      temp &= ~(*clipptr) ;
      (*formptr) |= temp ;
      return ;
    }
    temp &= ~(*clipptr++) ;
    (*formptr++) |= temp ;
    xe = -xe ;
  }
  xs = xe >> BLIT_SHIFT_BITS ;
  if ( xs ) {
    int32 count ;
    for ( count = xs >> 3 ; count != 0 ; count--, formptr += 8, clipptr += 8 ) {
      formptr[0] |= ~clipptr[0] ;
      formptr[1] |= ~clipptr[1] ;
      formptr[2] |= ~clipptr[2] ;
      formptr[3] |= ~clipptr[3] ;
      formptr[4] |= ~clipptr[4] ;
      formptr[5] |= ~clipptr[5] ;
      formptr[6] |= ~clipptr[6] ;
      formptr[7] |= ~clipptr[7] ;
    }
    xs &= 7 ;
    switch ( xs ) {
    case 7 : formptr[6] |= ~clipptr[6] ;
    case 6 : formptr[5] |= ~clipptr[5] ;
    case 5 : formptr[4] |= ~clipptr[4] ;
    case 4 : formptr[3] |= ~clipptr[3] ;
    case 3 : formptr[2] |= ~clipptr[2] ;
    case 2 : formptr[1] |= ~clipptr[1] ;
    case 1 : formptr[0] |= ~clipptr[0] ;
      formptr += xs ;
      clipptr += xs ;
    }
  }
/* Partial right-span. */
  xe &= BLIT_MASK_BITS ;
  if ( xe > 0 ) {
    temp = SHIFTLEFT( ALLONES , BLIT_WIDTH_BITS - xe ) ;
    temp &= ~(*clipptr) ;
    (*formptr) |= temp ;
  }
}

/* Function definitions to be inlined in the DDA scan converter, for sub-pixel
   resolution rendering. The DDA scan converter is used for all
   implementations of scan conversion in the RIP. These functions implement the
   sub-pixel scan conversion rule similarly to the old fixed point accurate
   renderer. They perform multi-stepping of the threads, but do not yet do
   dropout control. */

#define AR_CENTRE_LINE (AR_FACTOR >> 1)

enum {
  AR_THREAD_LT = 1,          /* Thread is a left or top thread */
  AR_THREAD_RB = 2,          /* Thread is a right or bottom thread */
  AR_THREAD_MASK = AR_THREAD_LT|AR_THREAD_RB
} ;

typedef struct {
  render_blit_t *rb ;
  BITBLT_FUNCTION spanfn ;
  dcoord lo ;    /* Left end of current span before stepping */
  Bool twopass ;
  Bool do_min ; /* Force minimum width */
  Bool do_run ; /* Do centre intersection run */
  NBRESS **deadthreads ;  /* Start of expired threads partition */
  NBRESS **newthreads ;   /* First new thread on this line */
} dda_info_t ;

/** Draw span on centre row/column of pixel. Spans drawn on the centre
   row/column of a pixel have a minimum width of one pixel; if they do not
   intersect any pixel centres, they are snapped to the closest pixel
   centre. */
static inline void centrespan(dda_info_t *info, dcoord rc, dcoord lo, dcoord hi)
{
  /* Roundings are added to find the super pixels whose centres are included
     in the sub-pixel span. The left factor is one pixel less than AR_FACTOR/2
     so that spans exactly on a half pixel boundary round include the
     superpixel at the start. */
  lo += AR_FRACBITS >> 1 ;
  hi -= AR_FACTOR >> 1 ;
  if ( lo > hi ) {
    if ( !info->do_min )
      return ; /* No dropout rendering (minimum width is not forced) */

    /* Span is less than one pixel wide. We need to show a span to prevent
       dropouts, so we select the superpixel including the centre of the
       span. The addition of one is because the adjustment above biased the
       left by slightly less than half a pixel; a span from 0 to 0 would
       otherwise round to a negative centre. */
    lo = hi = (lo + hi + 1) >> 1 ;
  } else if ( !info->do_run )
    return ; /* No run rendering */

  /* We disallow negative coordinates because they should never occur inside
     a character form, and right shift is undefined on negative integers. If
     this routine is ever used for non-character-form rendering, we will have
     to clip lo and hi to 0 before rounding. We deliberately do not clip to
     x1clip/x2clip, because this routine is shared between vertical and
     horizontal rendering. Vertical rendering clips to y1clip/y2clip in the
     underlying info->spanfn function. */
  HQASSERT(lo >= 0, "Low coordinate negative in character span") ;
  HQASSERT(lo <= hi, "Span limits out of order") ;
  lo >>= AR_BITS ;
  hi >>= AR_BITS ;

  (*info->spanfn)(info->rb, rc >> AR_BITS, lo, hi) ;
}

/** Draw dropout control span. This span is not on the centre row of pixel, so
   the minimum width rounding is not applied. Dropout control spans mark
   pixels if whose centres they intersect, regardless of the width of the
   span. */
static void inline dropoutspan(dda_info_t *info,
                               dcoord y, dcoord xs, dcoord xe,
                               dcoord ydiff, BITBLT_FUNCTION spanfn)
{
  render_blit_t *rb ;
  FORM *outputform ;

  if ( info->twopass ) /* Two-pass rendering has no need for dropout control */
    return ;

  /* Roundings are added to find the super pixels whose centres are included
     in the sub-pixel span. The left factor is one pixel less than AR_FACTOR/2
     so that spans exactly on a half pixel boundary round include the
     superpixel at the start. */
  xs += AR_FRACBITS >> 1 ;
  xe -= AR_FACTOR >> 1 ;

  /* We disallow negative coordinates because they should never occur inside
     a character form, and right shift is undefined on negative integers. If
     this routine is ever used for non-character-form rendering, we will have
     to clip xs and xe to 0 before rounding. */
  HQASSERT(xs >= 0, "X coordinate negative in character span") ;
  xs >>= AR_BITS ;
  xe >>= AR_BITS ;
  if ( xs > xe )
    return ;
  bbox_clip_x(&info->rb->p_ri->clip, xs, xe);
  if ( xs > xe )
    return ;

  HQASSERT(y >= 0, "Y coordinate negative in character span") ;
  y >>= AR_BITS ;

  rb = info->rb ;
  outputform = rb->outputform ;

  if ( ydiff + y < 0 || ydiff + y >= theFormH(*outputform) )
    ydiff = 0 ;

  rb->ymaskaddr = rb->ylineaddr ;
  rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr, theFormL(*outputform) * ydiff) ;

  (*spanfn)(rb, y + ydiff, xs, xe) ;

  rb->ylineaddr = rb->ymaskaddr ;
}

/** Draw span on centre row of pixel. Spans drawn on the centre row of a pixel
   have a minimum width of one pixel; if they do not intersect any pixel
   centres, they are snapped to the closest pixel centre. */
static void rowspan(render_blit_t *rb,
                    dcoord row, dcoord leftx, dcoord rightx)
{
  const dbbox_t *clip = &rb->p_ri->clip;

  /* Blit span. Use inline clip max/min operations to avoid branches. */
  HQASSERT(sizeof(leftx) == sizeof(int32) && sizeof(rightx) == sizeof(int32),
           "Inline max/min operations use wrong intermediate size") ;

  /* Parameter order is carefully chosen so that pointer dereference is only
     expanded once in a release RIP. */
  INLINE_MAX32(leftx, clip->x1, leftx) ;
  INLINE_MIN32(rightx, rightx, clip->x2) ;

  if ( leftx > rightx )
    return ;

  bitfill1(rb, row, leftx, rightx) ;
}

/** Draw span on centre column of pixel. Spans drawn on the centre column of a
   pixel have a minimum width of one pixel; if they do not intersect any
   pixel centres, they are snapped to the closest pixel centre. X and Y are
   swapped, so apply suitable adjustments to the line address, and draw
   single pixels in a column. */
static void columnspan(render_blit_t *rb,
                       dcoord column, dcoord topy, dcoord bottomy)
{
  int32 wupdate ;
  render_blit_t rb_copy = *rb ;
  const dbbox_t *clip = &rb_copy.p_ri->clip;

  /* Blit span. Use inline clip max/min operations to avoid branches. */
  HQASSERT(sizeof(topy) == sizeof(int32) && sizeof(bottomy) == sizeof(int32),
           "Inline max/min operations use wrong intermediate size") ;

  /* Parameter order is carefully chosen so that pointer dereference is only
     expanded once in a release RIP. */
  INLINE_MAX32(topy, clip->y1, topy) ;
  INLINE_MIN32(bottomy, bottomy, clip->y2) ;

  if ( topy > bottomy )
    return ;

  /* The form line address is currently adjusted to what the scan converter
     thinks is the correct scanline, i.e. the column times the form's line
     length. We need to re-adjust it to point at the start of the the correct
     line, i.e. topy. */
  wupdate = theFormL(*rb_copy.outputform) ;
  rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, (topy - column) * wupdate) ;

  do {
    bitfill1(&rb_copy, topy, column, column) ;
    rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate) ;
  } while ( ++topy <= bottomy ) ;
}

static inline void dda_fill_start(dda_info_t *info, dcoord line,
                                  NFILLOBJECT *nfill, int32 rule)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NFILLOBJECT *, nfill) ;
  UNUSED_PARAM(int32, rule) ;

  HQASSERT((rule & ISCLIP) == 0, "Rule should not be clipping for sub-pixel") ;
}

/* Function to end the fill. Dropouts not resolved in the last scanline should
   be blitted into it. The line is one past the end of the fill, we revert to
   the previous centre scanline. */
static inline void dda_fill_end(dda_info_t *info, dcoord line,
                                NFILLOBJECT *nfill)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;
  UNUSED_PARAM(NFILLOBJECT *, nfill) ;
}

static inline void dda_lines_start(dda_info_t *info, dcoord line,
                                   NBRESS **start, NBRESS **end)
{
  UNUSED_PARAM(dcoord, line) ;

  info->deadthreads = start ;
  info->newthreads = end ;
}

/** Function to return the number of lines to step at once, 0 < nlines
   <= maxlines. Note that DDA_LINE_STEP is only called when stepping multiple
   scanlines; it may not be called before calls to DDA_THREAD_FINAL. We try to
   step directly to the next centre scanline, unless there are no thread, in
   which case we will step all the lines at once. */
static inline void dda_line_step(dda_info_t *info,
                                 dcoord line, dcoord *maxlines,
                                 NBRESS **start, NBRESS **end)
{
  UNUSED_PARAM(dda_info_t *, info) ;

  if ( start != end ) {
    dcoord lines = (line | (AR_FRACBITS >> 1)) + 1 - line ;
    HQASSERT(sizeof(lines) == sizeof(int32) &&
             sizeof(*maxlines) == sizeof(int32),
             "Inline max/min operations use wrong intermediate size") ;
    /* Parameter order is carefully chosen so that pointer dereference is only
       expanded once in a release RIP. */
    INLINE_MIN32(*maxlines, lines, *maxlines) ;
  }
}

/** Function to complete a set of scanlines. If we are transitioning
   across a pixel boundary, update the line address. If we are completing a
   centre line, incorporate the current dropouts into the centre span. If we
   are ending on or past the next centre scanline, update the dropout
   spans. */
static inline void dda_line_end(dda_info_t *info, dcoord line, dcoord nlines,
                                NBRESS **start, NBRESS **end)
{
  dcoord thisline = line >> AR_BITS ;
  dcoord nextline = (line + nlines) >> AR_BITS ;

  UNUSED_PARAM(NBRESS **, start) ;
  UNUSED_PARAM(NBRESS **, end) ;

  if ( nextline != thisline ) {
    render_blit_t *rb = info->rb ;
    rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr,
                                 theFormL(*rb->outputform) * (nextline - thisline)) ;
  }
}

/** Left end of thread. Leftward going lines in the top half of the pixel and
   rightward going lines in the bottom half of the pixel will mark pixels on
   the current line whose centres are intersected. Rightward going lines in
   the top half mark pixels in the previous row, bit only if the current row
   has not been marked. Leftward going lines in the bottom half mark pixels
   in the previous row, bit only if the current row has not been marked.
   These rules prevent dropouts by making sure that every marked area
   intersecting a pixel centre in the vertical direction has a marked pixel. */
static inline void dda_thread_first(dda_info_t *info,
                                    dcoord line, dcoord nlines,
                                    NBRESS *thread)
{
  info->lo = thread->u1.ncx ;
  thread->flags = AR_THREAD_LT ;

  DDA_SCAN_STEP_N(thread, nlines) ;
  if ( (line & AR_FRACBITS) < AR_CENTRE_LINE ) { /* Top half of pixel */
    dropoutspan(info, line, thread->u1.ncx + 1, info->lo, 0, bitfill1) ;
    dropoutspan(info, line, info->lo, thread->u1.ncx - 1, -1, biticlip1) ;
  } else { /* Bottom half of pixel */
    dropoutspan(info, line, thread->u1.ncx + 1, info->lo, 1, biticlip1) ;
    dropoutspan(info, line, info->lo, thread->u1.ncx - 1, 1, bitfill0) ;
    dropoutspan(info, line, info->lo, thread->u1.ncx - 1, 0, bitfill1) ;
  }
}

/** Internal threads. */
static inline void dda_thread_internal(dda_info_t *info,
                                       dcoord line, dcoord nlines,
                                       NBRESS *thread)
{
  UNUSED_PARAM(dda_info_t *, info) ;
  UNUSED_PARAM(dcoord, line) ;

  thread->flags = 0 ;
  DDA_SCAN_STEP_N(thread, nlines) ;
}

/** Right end of thread. We only draw the thread if it is strictly greater
   than zero pixels width. */
static inline void dda_thread_last(dda_info_t *info,
                                   dcoord line, dcoord nlines,
                                   NBRESS *thread)
{
  dcoord hi = thread->u1.ncx ;

  thread->flags = AR_THREAD_RB ;
  DDA_SCAN_STEP_N(thread, nlines) ;

  if ( (line & AR_FRACBITS) < AR_CENTRE_LINE ) { /* Top half of pixel */
    dropoutspan(info, line, hi, thread->u1.ncx - 1, 0, bitfill1) ;
    dropoutspan(info, line, thread->u1.ncx + 1, hi, -1, biticlip1) ;
  } else { /* Bottom half of pixel */
    dropoutspan(info, line, hi, thread->u1.ncx - 1, 1, biticlip1) ;
    dropoutspan(info, line, thread->u1.ncx + 1, hi, 1, bitfill0) ;
    dropoutspan(info, line, thread->u1.ncx + 1, hi, 0, bitfill1) ;
    if ( (line & AR_FRACBITS) == AR_CENTRE_LINE )
      centrespan(info, line, info->lo, hi) ;
  }
}

static inline void dda_clipped_last(dda_info_t *info,
                                    dcoord line, dcoord nlines)
{
  UNUSED_PARAM(dcoord, nlines) ;

  if ( (line & AR_FRACBITS) == AR_CENTRE_LINE ) /* On a centre line */
    centrespan(info, line, info->lo, MAXDCOORD) ;
}

/** Dropout control for zero-width strokes is easy; mark all columns whose
   centres are intersected by the stroke (since it is both the top and bottom
   of the fill, and the skeleton of the filled area). */
static inline void dda_thread_stroke(dda_info_t *info,
                                     dcoord line, dcoord nlines,
                                     NBRESS *thread)
{
  dcoord oldx = thread->u1.ncx, newx ;

  DDA_SCAN_STEP_N(thread, nlines) ;
  newx = thread->u1.ncx ;

  if ( (line & AR_FRACBITS) == AR_CENTRE_LINE ) { /* On a centre line */
    centrespan(info, line, oldx, newx) ;
  }

  if ( oldx > newx ) {
    dropoutspan(info, line, newx + 1, oldx, 0, bitfill1) ;
  } else if ( oldx < newx ) {
    dropoutspan(info, line, oldx, newx - 1, 0, bitfill1) ;
  }
}

/** This function is called for each line segment terminating at the current
   line. Horizontal lines will appear in this function for the first and last
   time. Existing and new threads are contiguous in thread table at this
   time, and continuation of terminating threads to new ones can be noted.
   New threads and continuations are classified for dropout control using
   this process:

   1) Horizontals which have a classification are continuations of existing
      threads. If a thread is continued by a rightward-going horizontal,
      invert the classification. This case copes with corners in threads
      where they change from left to bottom and right to top.
   2) Horizontals without a classification are new threads which were not found
      to be continuations of other threads. If they appeared inside an existing
      L/R span, they are bottom horizontals. The
   3) Bottom horizontals are drawn at or above the current line, top
      horizontals are drawn at or below the current line.
   4) The classification of horizontals continuing to non-horizontals is
      inverted if the continuation is at the right hand end. This is similar to
      case 1, and is only needed where a horizontal starts on the same line as
      a previous one terminates.
   4) If a terminating non-horizontal line was classified, but has no
      continuation, search for the first unclassified new thread starting at
      the terminating thread's X coordinate. Propagate the terminating
      thread's flags to the new thread.

    This code does not take into account cases that should not be permitted
    in character outlines, such as threads crossing over, and horizontals
    continuing with further horizontals. */
static inline void dda_thread_final(dda_info_t *info, dcoord line,
                                    NBRESS **start, NBRESS **end,
                                    NBRESS *thread, Bool more)
{
  UNUSED_PARAM(NBRESS **, start) ;

  if ( thread->denom == 0 ) { /* Horizontal line, needs theINCX updated */
    dcoord oldx = thread->u1.ncx, newx = oldx + thread->si ;
    thread->u1.ncx = newx ;

    if ( oldx > newx ) { /* Swap bounds */
      dcoord tmpx = oldx ; oldx = newx ; newx = tmpx ;
    } else if ( thread->flags != 0 ) {
      thread->flags ^= AR_THREAD_MASK ;
    }

    if ( thread->flags == 0 ) {
      NBRESS **loop ; /* Search for closest classified thread to left */

      dcoord maxx = MINDCOORD ;
      thread->flags = AR_THREAD_LT ;

      for ( loop = info->deadthreads ; loop < info->newthreads ; ++loop ) {
        if ( (*loop)->flags != 0 &&
             ((*loop)->u1.ncx) >= maxx && ((*loop)->u1.ncx) < newx ) {
          thread->flags = CAST_TO_UINT8((*loop)->flags ^ AR_THREAD_MASK) ;
          maxx = ((*loop)->u1.ncx) ;
        }
      }
    }

    if ( (line & AR_FRACBITS) == 0 ) {
      if ( thread->flags == AR_THREAD_RB ) {
        dropoutspan(info, line, oldx, newx, 0, bitfill0) ;
        dropoutspan(info, line, oldx, newx, -1, bitfill1) ;
      } else {
        dropoutspan(info, line, oldx, newx, 0, bitfill1) ;
      }
    } else if ( (line & AR_FRACBITS) < AR_CENTRE_LINE ) {
      if ( thread->flags == AR_THREAD_RB ) {
        dropoutspan(info, line, oldx, newx, -1, biticlip1) ;
      } else {
        dropoutspan(info, line, oldx, newx, 0, bitfill1) ;
      }
    } else if ( (line & AR_FRACBITS) > AR_CENTRE_LINE ) {
      if ( thread->flags == AR_THREAD_RB ) {
        dropoutspan(info, line, oldx, newx, 1, bitfill0) ;
        dropoutspan(info, line, oldx, newx, 0, bitfill1) ;
      } else {
        dropoutspan(info, line, oldx, newx, 1, biticlip1) ;
      }
    } else {
      centrespan(info, line, oldx, newx) ;
    }

    if ( more && thread->u1.ncx == newx )
      thread->flags ^= AR_THREAD_MASK ; /* Horizontal continues */
  } else if ( !more ) {
    NBRESS **loop ; /* Propagate flags to extension of this thread */

    for ( loop = info->newthreads ; loop < end ; ++loop ) {
      if ( ((*loop)->u1.ncx) == thread->u1.ncx ) {
        HQASSERT(*loop != (thread), "New terminating thread is not horizontal") ;
        HQASSERT((*loop)->flags == 0, "New thread already classified") ;
        (*loop)->flags = thread->flags ;
        info->newthreads = loop + 1 ;
        break ;
      }
    }
  }
}

/* Thread sorting doesn't do anything for character renderer. */
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

/* Thread adjacency doesn't need anything for character renderer. */
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


void scanconvert_char(render_blit_t *rb,
                      NFILLOBJECT *nfill, int32 therule,
                      uint32 flags)
{
  dcoord scanline, y1c, y2c ;
  int32 wupdate ;
  NBRESS **startptr ;
  dda_info_t info ;
  FORM *outputform ;

  HQASSERT(rb, "No render info for char") ;
  HQASSERT((therule & ISCLIP) == 0, "sub-pixel render should not be a clip") ;
  HQASSERT ((therule & (~(ISCLIP|CLIPPED_RHS|SPARSE_NFILL))) == NZFILL_TYPE ||
            (therule & (~(ISCLIP|CLIPPED_RHS|SPARSE_NFILL))) == EOFILL_TYPE,
            "rnddafill(): therule should be NZFILL_TYPE or EOFILL_TYPE");

  outputform = rb->outputform ;
  HQASSERT(theFormT(*outputform) == FORMTYPE_CACHEBITMAP ||
           theFormT(*outputform) == FORMTYPE_CACHEBITMAPTORLE,
           "Character scan conversion only works with bitmap output") ;

  info.rb = rb ;
  info.twopass = FALSE ;
  info.spanfn = rowspan ;
  info.do_min = TRUE ;
  info.do_run = TRUE ;
#if defined(DEBUG_BUILD)
  if ( (debug_scanconv & DEBUG_SCAN_NO_HDROPOUT) != 0 )
    info.do_min = FALSE ;
  if ( (debug_scanconv & DEBUG_SCAN_NO_HRENDER) != 0 )
    info.do_run = FALSE ;
#endif

  y1c = rb->p_ri->clip.y1;
  y2c = rb->p_ri->clip.y2;

  if ( (flags & SC_FLAG_TWOPASS) != 0 ) {
    info.twopass = TRUE ;
    if ( (flags & SC_FLAG_SWAPXY) != 0 ) {
      /* X and Y are swapped in scan conversion. We don't want to swap
         the clip boundaries, because we would have to swap them back
         before the final call to the blit functions. The scan converter
         does not use them directly, but does require a low and high
         bound. */
      info.spanfn = columnspan ;
      info.do_min = TRUE ;
      info.do_run = FALSE ;
#if defined(DEBUG_BUILD)
      if ( (debug_scanconv & DEBUG_SCAN_NO_VDROPOUT) != 0 )
        info.do_min = FALSE ;
      if ( (debug_scanconv & DEBUG_SCAN_DO_VRENDER) != 0 )
        info.do_run = TRUE ;
#endif
      y1c = rb->p_ri->clip.x1;
      y2c = rb->p_ri->clip.x2;
    }
  }

#if defined(DEBUG_BUILD)
  /* May want to avoid pass entirely for debugging */
  if ( !info.do_min && !info.do_run )
    return ;
#endif

  HQASSERT( nfill->nexty <= y2c , "next y out of phase" ) ;

  nfill->nexty = y2c + 1 ;

  startptr = nfill->startnptr ;
  HQASSERT(startptr < nfill->thread + nfill->nthreads,
           "Character fill has no active threads") ;

  scanline = ((startptr[0])->ny1 >> AR_BITS) ;
  if ( scanline < y1c )
    scanline = y1c ;

  /* Bitblt variables. */
  wupdate = theFormL(*outputform) ;
  rb->ylineaddr = BLIT_ADDRESS(theFormA(*outputform),
                               wupdate * (scanline - theFormHOff(*outputform))) ;

  ddafill(&info, nfill, therule,
          y1c << AR_BITS, (y2c << AR_BITS) + AR_FRACBITS) ;
}

/* Log stripped */
