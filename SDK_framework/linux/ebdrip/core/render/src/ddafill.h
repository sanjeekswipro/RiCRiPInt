/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:ddafill.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file implements a scan converter using a fixed-point digital
 * differential analyser. This file is NOT compiled separately, which is why
 * it has a .h extension; it is included in other files which define the span
 * methods and types appropriate for the application of the scan converter.
 * The file which includes this scan converter must define macros or (inline)
 * functions for all of the thread prototypes, listed in comments below.
 * This is an extremely ugly way of compiling, but indirection through function
 * pointers is just not fast enough (the best results achieved through
 * extensive hand-optimisation were 50% slower using function pointers).
 *
 * The scan converter can be used efficiently for both device-resolution
 * rendering and sub-pixel rendering. It is configurable through the callback
 * macros for any pixel inclusion rule; the scan converter manages thread
 * sorting, winding rules, starting and stopping new threads, etc.
 *
 * The scan converter is a variable-step scan converter; it may step more
 * than one scanline at a time, allowing sub-pixel rendering to avoid wasting
 * time on scanlines which are of no interest. All line segments are passed
 * to the callback macros for consideration; the callbacks can determine
 * whether to step over line segments directly.
 *
 * Do NOT include any header files in this file; the file that includes this
 * should include all of the relevant headers.
 */

/** \page ddas DDA scan converters

   \section imports Required functions for a scan converter implementation.

   Types and macros/functions required to be defined before including this
   file are:

   dda_info_t
       Typedef used by DDA callbacks to store information between calls. The
       thread functions can use this to note the start and end positions of
       spans on a scanline. The exact definition is left to the scan conversion
       application because it may want to store information about the thread
       that started the span, the error constants at the start of the span,
       etc., (e.g. for dropout control in character rendering). A variable
       of this type is put on the ddafill() stack; it can be initialised in
       dda_fill_start() and finalised in dda_fill_end().
   dda_fill_start(info, scanline, nfill, rule)
       Initialise the local dda_info_t structure info before scan converting
       an nfill structure.
   dda_fill_end(info, scanline, nfill)
       Finalise the local dda_info_t structure info after scan converting an
       nfill structure.
   dda_lines_start(info, scanline, startptr, endptr)
   dda_line_step(info, scanline, maxlines, startptr, endptr)
       This macro resets the maxlines value to the number of scanlines that
       will be stepped in one go for the threads starting at the current
       scanline. The new value should be greater than zero but less than or
       equal to the original maxlines.
   dda_line_end(info, scanline, nlines, start, end)
       This macro/function is called after each set of thread calls. Note that
       dda_line_step() is not necessarily called before each set of thread
       calls; terminating threads may call dda_thread_final() before
       dda_line_step(), or there may not even be any thread calls between
       dda_line_end() calls. The start pointer to this call includes threads
       that terminated at the start of the set of scanlines; it can be used to
       transfer dropout control information to new threads introduced on the
       first scanline.
   dda_thread_first(info, scanline, nlines, thread)
       This macro/function is called for the first thread intersecting the
       current scanline. It should initialise the info structure appropriately
       for the start and end of the pixel span touched on the scanline, and
       then step the DDA by the number of lines indicated.
   dda_thread_internal(info, scanline, nlines, thread)
       This macro/function is called for internal threads of non-zero winding
       rule fills (even-odd fills do not have internal threads). It should
       update the info structure to indicate the pixels touched appropriately,
       and then step the DDA by the number of lines indicated. Threads are
       sorted by X and gradient, however, the pixel inclusion rule for a
       particular type of rendering may cause internal threads with shallower
       gradients than the end threads to overlap the end threads and mark more
       pixels on the line.
   dda_thread_last(info, scanline, nlines, thread)
       This macro/function is called for the last thread intersecting the
       current scanline, but only if the last thread is not clipped out at the
       right hand side of the fill. It should update the info structure
       appropriately, step the DDA, and issue appropriate calls to the blit
       functions to draw the span if desired.
   dda_clipped_last(info, scanline, nlines)
       This macro/function is called instead of dda_thread_last() if the fill
       is clipped out on the right hand side. There is no thread to update, but
       it should perform the appropriate actions to blit spans if necessary,
       using x2clip as the right hand limit of the span.
   dda_thread_stroke(info, scanline, nlines, thread)
       This macro/function is called for threads representing zero-width
       strokes. It should work out the left and right limits of the pixels
       touched and blit them if appropriate, then update the DDA by the number
       of lines specified.
   dda_thread_final(info, scanline, start, end, thread, more)
       This macro/function is called on the scanline that a segment of a thread
       terminates. It should blit any appropriate remaining parts of the
       thread, and update the thread's current position to the end of the line
       segment; if more is true the thread continues. Horizontal
       lines are only presented through this interface, since they terminate
       on the same scanline as they start. The current position of a horizontal
       line should be updated by adding the integral part of the gradient.
   dda_thread_sort_swapped(info, scanline, left, right, maybefinal)
       This macro/function is called when threads are swapped during line
       updating. left is the new left thread, right is the new right thread.
       The boolean flag maybefinal indicates if this might be the final pass
       of the sort.
   dda_thread_sort_overlap(info, scanline, left, right, maybefinal)
       This macro/function is called during line update sorting to test if the
       pixels lit by a pair of threads might overlap. The boolean flag
       maybefinal indicates if this might be the final pass of the sort.
*/

/** Sorts threads by x, and as a side-effect detect if any of the threads
 * are crossing each other, aiding caller in making spanlist merging decisions.
 */
static void bxsortnbress(dda_info_t *info, dcoord scanline,
                         register NBRESS **bottom, register NBRESS **top)
{
  Bool flag = TRUE ;

  --top ;

  while ( bottom < top ) {
    register NBRESS *tmp0 ;
    register NBRESS *tmp1 ;
    register NBRESS **loop ;

    loop = bottom ;
    tmp1 = loop[ 0 ] ;
    while ( loop < top ) {
      tmp0 = tmp1 ;
      ++loop ;
      tmp1 = loop[ 0 ] ;
      if ( COMPARE_THREADS_X(tmp0, tmp1, >) ) {
        flag = FALSE ;
        loop[ -1 ] = tmp1 ;
        loop[  0 ] = tmp0 ;
        dda_thread_sort_swapped(info, scanline, tmp1, tmp0, flag) ;
        tmp1 = tmp0 ;
      } else
        dda_thread_sort_overlap(info, scanline, tmp0, tmp1, flag) ;
    }

    if ( flag )
      return ;
    --top ;

    loop = top ;
    tmp1 = loop[ 0 ] ;
    while ( loop > bottom ) {
      tmp0 = tmp1 ;
      --loop ;
      tmp1 = loop[ 0 ] ;
      if ( COMPARE_THREADS_X(tmp0, tmp1, <) ) {
        flag = TRUE ;
        loop[ 1 ] = tmp1 ;
        loop[ 0 ] = tmp0 ;
        dda_thread_sort_swapped(info, scanline, tmp0, tmp1, !flag) ;
        tmp1 = tmp0 ;
      } else
        dda_thread_sort_overlap(info, scanline, tmp1, tmp0, !flag) ;
    }

    if ( ! flag )
      return ;
    ++bottom ;

    SwOftenUnsafe() ;
  }
}

static inline void ddafill(dda_info_t *info, NFILLOBJECT *nfill,
                           int32 therule, dcoord y1c, dcoord y2c)
{
  register dcoord scanline ;
  register NBRESS **indexptr, **endptr ;
  int32 cthreads ;
  NBRESS **startptr , **limitptr ;
  int32 filltype ;

  HQASSERT((therule & (~(ISCLIP|CLIPPED_RHS|SPARSE_NFILL))) == NZFILL_TYPE ||
           (therule & (~(ISCLIP|CLIPPED_RHS|SPARSE_NFILL))) == EOFILL_TYPE,
           "therule should be NZFILL_TYPE or EOFILL_TYPE");
  /* until just now, literal 1's and 0's were used for the rule, and nsidetst.c
   * had it backwards.  So they're now symbolics, and NZFILL_TYPE isn't 1 to
   * allow this assert to find any bits I've missed (see task 4565).
   */

/* Set up start & end pointers. */
  startptr = nfill->startnptr ;
  limitptr = nfill->thread + nfill->nthreads;
  HQASSERT(startptr < limitptr,
           "Fill structure not prepared; no lines in fill") ;

  scanline = ( startptr[ 0 ] )->ny1 ;
  if ( scanline < y1c )
    scanline = y1c ;

  filltype = (therule & FILLRULE) ;
  endptr = nfill->endnptr ;
  cthreads = CAST_PTRDIFFT_TO_INT32(endptr - startptr) ;
  cthreads -= 2 ;

  dda_fill_start(info, scanline, nfill, therule) ;

  /***********************************/
  /* Loop down through the scanlines */
  /***********************************/

  /* Go for it then... */
  do {
    dcoord linesleft ;

    HQTRACE((debug_scanconv & DEBUG_SCAN_INFO) != 0,
            ("at top of loop, scanline=%d\n", scanline));
    /* Line start macro is called before updating start and end pointers, so
       that terminated and new lines can be detected. */
    dda_lines_start(info, scanline, startptr, endptr) ;

    /* Optimise performance by determining how far we can go in a single
       loop before we need to merge lines, change segments within a thread
       or stop because we reach the end of the clip region. This enables
       us to loop without performing redundant tests every iteration.
       Assume it is the end of the clipped region - best case! */

    linesleft = y2c + 1 ;

    /* Check new threads */

    while ( endptr < limitptr ) {
      dcoord tmpy ;

      tmpy = ((*endptr))->ny1 ;
      if ( tmpy != scanline ) {
        /* This will be the next thread to be merged */
        if ( tmpy < linesleft )
          linesleft = tmpy ;
        break ;
      }

      ++cthreads ;
      ++endptr ;
    }

    linesleft -= scanline ;
    HQASSERT(linesleft > 0, "End of fill segment is before scanline") ;

    /* Check for any terminating threads. We have new threads and terminating
       threads together at this point, so we should be able to determine
       continuity of threads. */
    if ( startptr < endptr ) {
      for ( indexptr = startptr ; indexptr < endptr ; ++indexptr )
      {
        NBRESS *thread = (*indexptr);
        dcoord lines2do = thread->nmindy >> 1;

        /* Deal with threads which have reached their end of a segment. They
           may have continuation segments, or the threads may terminate. If
           they terminate, we swap them out of the active thread set, and
           avoid updating the number of lines left. */
        while ( lines2do == 0 )
        {
          Bool more;
          dcoord dx = 0, dy = 0;

          more = dxylist_get(&(thread->dxy), &dx, &dy);
          dda_thread_final(info, scanline, startptr, endptr, thread, more);
          if ( more )
          {
            /* Re-initialise thread for next line segment. */
            DDA_SCAN_INITIALISE(thread, dx, dy);

            /* We'll set things up for the non-horizontal case, which is far
               more common than the horizontal case. Update the delta index
               and set the number of lines left in the new segment (which
               may be zero). */
            lines2do = dy; /* Re-load continuation */
          }
          else
          {
            NBRESS **loopptr;
            /* Really finished with the whole bress structure. Move it to
               the start of the active thread list, update the active thread
               list to avoid it, and do not update the number of lines
               left. */
            for ( loopptr = indexptr ; loopptr > startptr ; --loopptr )
              loopptr[0] = loopptr[-1];
            *startptr++ = thread;
            cthreads -= 1;
            lines2do = 1; /* just get out of the loop */
          }
        }
        if ( linesleft > lines2do )
          linesleft = lines2do ;
      }

      if ( startptr == limitptr ) {
        /* Update any end lines just done and finish the scan. */
        dda_line_end(info, scanline, 1, startptr, endptr) ;

        scanline += 1 ;
        break ;
      }
    }

    /* Fill section nlines long from scanline. */
    if ( ! cthreads ) { /* optimise for a common case - two threads */
      register NBRESS *thread1 , *thread2 ;
      Bool clipped = FALSE ;

      HQASSERT(endptr == startptr + 2,
               "End pointer not consistent with number of threads") ;

      /* If NZ and both threads have the same orientation, there were threads
         clipped at the right hand side. */
      thread1 = startptr[0] ;
      thread2 = startptr[1] ;
      if ( filltype == NZFILL_TYPE &&
           thread1->norient + thread2->norient != 0 )
        clipped = TRUE ;

      do {
        dcoord nlines = linesleft ;

        /* Sort these lines on increasing x values and slope */
        if ( COMPARE_THREADS_X(thread1, thread2, >) ) {
          startptr[0] = thread2 ;
          startptr[1] = thread1 ;
          thread2 = thread1 ;
          thread1 = startptr[0] ;
          dda_thread_sort_swapped(info, scanline, thread1, thread2, TRUE) ;
        } /* else: we don't call dda_thread_sort_overlap() in this case,
             because the two-thread case only has one filled section, and so
             cannot have a later fill section overlapping an earlier one. */

        dda_line_step(info, scanline, &nlines, startptr, endptr) ;

        HQASSERT(nlines > 0 && nlines <= linesleft,
                 "Scanbeam used too few/too many lines") ;

        SwOftenUnsafe() ;

        dda_thread_first(info, scanline, nlines, thread1) ;
        if ( clipped ) {
          dda_thread_internal(info, scanline, nlines, thread2) ;
          dda_clipped_last(info, scanline, nlines) ;
        } else {
          dda_thread_last(info, scanline, nlines, thread2) ;
        }
        dda_line_end(info, scanline, nlines, startptr, endptr) ;

        linesleft -= nlines ;
        scanline += nlines ;
      } while ( linesleft > 0 ) ;
    } else if ( filltype == NZFILL_TYPE ) {
      do {
        dcoord nlines = linesleft ;

        bxsortnbress(info, scanline, startptr, endptr);

        dda_line_step(info, scanline, &nlines, startptr, endptr) ;

        HQASSERT(nlines > 0 && nlines <= linesleft,
                 "Scanbeam used too few/too many lines") ;

        SwOftenUnsafe() ;

        /* Run along collecting scan-lines. */
        /* Normal postscript non-zero winding rule. */
        indexptr = startptr ;
        while ( indexptr < endptr ) {
          NBRESS *thread = *indexptr++ ;
          int32 winding = thread->norient ;

          dda_thread_first(info, scanline, nlines, thread) ;

          for (;;) {
            if ( indexptr == endptr ) {
              /* If we reached the end of the active thread list without
                 reducing the winding number to zero, then there are lines
                 clipped out at the right and x2clip should be used as the
                 right span boundary. */
              dda_clipped_last(info, scanline, nlines) ;
              break ;
            }

            thread = *indexptr++ ;
            winding += thread->norient;

            if ( winding == 0 ) {
              dda_thread_last(info, scanline, nlines, thread) ;
              break ;
            }

            dda_thread_internal(info, scanline, nlines, thread) ;
          }
        }

        dda_line_end(info, scanline, nlines, startptr, endptr) ;

        linesleft -= nlines ;
        scanline += nlines ;
      } while ( linesleft > 0 ) ;
    } else if ( filltype == EOFILL_TYPE ) { /* EOFILL_TYPE */
      do {
        dcoord nlines = linesleft ;

        bxsortnbress(info, scanline, startptr, endptr);

        dda_line_step(info, scanline, &nlines, startptr, endptr) ;

        HQASSERT(nlines > 0 && nlines <= linesleft,
                 "Scanbeam used too few/too many lines") ;

        SwOftenUnsafe() ;

        /* Run along collecting scan-lines. */
        /* Alternative even-odd rule. */
        indexptr = startptr ;
        while ( indexptr < endptr ) {
          NBRESS *thread = *indexptr++ ;

          dda_thread_first(info, scanline, nlines, thread) ;

          if ( indexptr < endptr ) {
            thread = *indexptr++ ;
            dda_thread_last(info, scanline, nlines, thread) ;
          } else {
            dda_clipped_last(info, scanline, nlines) ;
          }
        }

        dda_line_end(info, scanline, nlines, startptr, endptr) ;

        linesleft -= nlines ;
        scanline += nlines ;
      } while ( linesleft > 0 ) ;
    } else { /* Zero-width stroke */
      HQASSERT(filltype == ISSTRK, "Fill type is none of NZ, EO or thin stroke") ;

      do {
        dcoord nlines = linesleft ;

        /* The only reason to sort these is to preserve span ordering along
           a line. The threads don't have a winding rule, so they don't need
           to match left and right. */
        bxsortnbress(info, scanline, startptr, endptr);

        dda_line_step(info, scanline, &nlines, startptr, endptr) ;

        HQASSERT(nlines > 0 && nlines <= linesleft,
                 "Scanbeam used too few/too many lines") ;

        SwOftenUnsafe() ;

        /* Run along collecting scan-lines. */
        /* Alternative even-odd rule. */
        for ( indexptr = startptr ; indexptr < endptr ; ++indexptr ) {
          NBRESS *thread = *indexptr ;

          dda_thread_stroke(info, scanline, nlines, thread) ;
        }

        dda_line_end(info, scanline, nlines, startptr, endptr) ;

        linesleft -= nlines ;
        scanline += nlines ;
      } while ( linesleft > 0 ) ;
    }
  } while ( scanline <= y2c ) ;

  /*********************************/
  /* End of loop through scanlines */
  /*********************************/

  /* Reset paramaters in FILLOBJECT so that can continue with fill. */
  nfill->startnptr = startptr ;
  nfill->endnptr = endptr ;

  dda_fill_end(info, scanline, nfill) ;
}

/* Log stripped */
