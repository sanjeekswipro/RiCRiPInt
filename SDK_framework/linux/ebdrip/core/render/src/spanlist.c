/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:spanlist.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manage spanlists; pairs of spans on a single scanline.
 * Spanlists can be accumulated, merged, read out, and spans can be clipped
 * against them. Spanlists may be used for thread termination (cap) merging,
 * RLE output, contone clipping. This is a part of the CORErender interface.
 */

#include "core.h"
#include "mm.h" /* PTR_IS_ALIGNED */
#include "spanlist.h"
#include "render.h" /* render_blit_t */
#include "hqmemcpy.h"

/* The spanlist structure is put at the start of the area specified, with the
   spans immediately following. This arrangement makes it easy for clients to
   align and cast an arbitrary pointer to a spanlist. The lastspan pointer
   points to the last span on a line; if no spans have been inserted, it
   points to a sentinel span of (MINDCOORD,MINDCOORD). MINDCOORD is excluded
   as a valid span coordinate. MINDCOORD+1 should not be used if
   spanlist_merge() will be called either. */
struct spanlist_t {
  span_t *lastspan ;  /**< Last span on line (sentinel span if none). */
  span_t *spanlimit ; /**< Address of last possible value of lastspan. */
  span_t spans[1] ;   /**< Spans, with sentinel span at start. */
} ;

/* Return the amount of memory required to store a certain number of spans */
size_t spanlist_size(uint32 nspans)
{
  return sizeof(spanlist_t) + nspans * sizeof(span_t) ;
}

/* Given a block of suitably-aligned memory, and a count in bytes, initialise
   a spanlist as large as is possible within that memory and return an opaque
   pointer to it. */
spanlist_t *spanlist_init(void *memory, size_t nbytes)
{
  spanlist_t *spanlist ;

  HQASSERT(memory, "No memory for spanlist") ;
  HQASSERT(PTR_IS_ALIGNED(Bool, memory, sizeof(spanlist_t *)),
           "Spanlist memory not suitably aligned") ;

  spanlist = memory ;

  /* We need a minimum of one sentinel span and one real span for each
     spanlist (the address of spans[2] is after the first real span). There
     really isn't any point doing this if we can't store more than this,
     though, but we leave that determination to the caller. */
  if ( (char *)&spanlist->spans[2] > (char *)memory + nbytes )
    return NULL ;

  /* Convert nbytes to a span count */
  nbytes = (nbytes - sizeof(spanlist_t)) / sizeof(span_t) ;
  HQASSERT(nbytes > 0, "No spans in line; size check failed") ;

  spanlist->lastspan = &spanlist->spans[0] ;
  spanlist->spanlimit = &spanlist->spans[nbytes] ;

  /* Set up sentinel at start of valid spans */
  spanlist->spans[0].left = spanlist->spans[0].right = MINDCOORD ;

  return spanlist ;
}

void spanlist_reset(spanlist_t *spanlist)
{
  HQASSERT(spanlist, "No spanlist") ;

  spanlist->lastspan = &spanlist->spans[0] ;
  HQASSERT(spanlist->lastspan + 1 <= spanlist->spanlimit,
           "Not enough space in initial spanlist") ;
  HQASSERT(spanlist->lastspan->left == MINDCOORD &&
           spanlist->lastspan->right == MINDCOORD,
           "Spanlist sentinel is corrupted") ;
}

/* Return number of spans stored in a spanlist. */
uint32 spanlist_count(const spanlist_t *spanlist)
{
  ptrdiff_t nspans ;

  HQASSERT(spanlist, "No spanlist") ;

  nspans = spanlist->lastspan - &spanlist->spans[0] ;

  return CAST_PTRDIFFT_TO_UINT32(nspans) ;
}

void *spanlist_limit(const spanlist_t *spanlist)
{
  HQASSERT(spanlist, "No spanlist") ;
  return spanlist->spanlimit + 1 ;
}

/* Add a span to a spanlist. This function returns FALSE if the spanlist is
   full *after* insertion. This function is far more efficient if spans are
   added left to right. */
Bool spanlist_insert(spanlist_t *spanlist, dcoord left, dcoord right)
{
  register span_t *span, *merge ;

  HQASSERT(spanlist, "No spanlist") ;
  HQASSERT(right >= left, "Empty intersection limits") ;
  HQASSERT(left > MINDCOORD, "Span includes sentinel value") ;

  /* When scan converting normally, spans on a line come in two passes, left
     to right. The terminating lines ("caps") are done first, then the spans.
     In both cases, we assume that the spans were X-sorted before the start
     of the pass, so a new span in the same pass can overwrite earlier spans,
     but cannot be disjoint and to the left of the last span. This code does
     not use this property, but performs a general merge of overlapping (but
     not abutting) spans. */
  for ( span = merge = spanlist->lastspan ; left <= span->right ; --span, --merge ) {
    /* There is the possibility that this span will overwrite or merge with
       the stored span. */
    if ( right >= span->left ) {
      /* Span intersects stored span. Merge limits with stored span, then
         continue to back up and merge until we hit the sentinel value at the
         start of the stored spans or find a span we can't merge with. */
      if ( right < span->right )
        right = span->right ;

      if ( left > span->left )
        left = span->left ;

      /* Incrementing the merge pointer here compensates for the automatic
         decrement at the end of the loop. The merge pointer ends up pointing
         at the stored span completely to the right of the new span. The
         increment/decrement is used in preference to an else clause in an
         attempt to reduce pipeline bubbles. */
      ++merge ;
    } /* else new span is disjoint and to the left of stored span. */
  }

  ++span ;
  HQASSERT(span > &spanlist->spans[0] && span <= spanlist->spanlimit,
           "Span address out of range") ;

  /* Span pointer is now location where merged/new span will be stored. */
  if ( span > merge ) {
    register span_t *last ;

    /* Span was disjoint with all stored spans; make space to store it if
       necessary. */
    HQASSERT(spanlist->lastspan + 1 <= spanlist->spanlimit,
             "No spare space in spanlist to insert span") ;
    for ( last = spanlist->lastspan++ ; last > merge ; --last )
      last[1] = last[0] ;
  } else if ( span < merge ) {
    register span_t *copy = span, *last = spanlist->lastspan ;

    /* New span merged with more than one existing span. Shuffle down remaining
       spans to close up the gap. */
    while ( merge < last )
      *++copy = *++merge ;

    spanlist->lastspan = copy ;
  } /* else exactly one span was merged, so the number and order of entries
       match exactly. */

  /* Store merged/new span back into list. */
  span->left = left ;
  span->right = right ;

  /* Determine if spanlist still has space */
  return (spanlist->lastspan < spanlist->spanlimit) ;
}

/* Determines the number of spans in the given bitmap without actually
   inserting any spans. */
uint32 spanlist_bitmap_spans(const blit_t *bitmap, dcoord w)
{
  Bool drawn = FALSE ;
  dcoord right = 0 ;
  uint32 nspans = 0 ;

  for ( ; right < w ; ++bitmap ) {
    if ( right + BLIT_WIDTH_BITS < w && (*bitmap == 0 || *bitmap == ALLONES) ) {
      /* do a whole blit_t in one go */
      if ( *bitmap == ALLONES ) {
        drawn = TRUE ;
      } else {
        if ( drawn )
          ++nspans ;
        drawn = FALSE ;
      }
      right += BLIT_WIDTH_BITS ;
    } else {
      /* do it bit-by-bit */
      int32 bitshift ;
      for ( bitshift = BLIT_MASK_BITS ; right < w && bitshift >= 0 ; ++right, --bitshift ) {
        if ( ((*bitmap >> bitshift) & 0x1) != 0 ) {
          drawn = TRUE ;
        } else {
          if ( drawn )
            ++nspans ;
          drawn = FALSE ;
        }
      }
    }
  }
  if ( drawn )
    ++nspans ;

  return nspans ;
}

/* Encode the given bitmap into spans, assuming the spanlist has been
   initialised already.  Returns FALSE if spanlist is too small, TRUE
   otherwise. */
Bool spanlist_from_bitmap(spanlist_t *spanlist, const blit_t *bitmap, dcoord w)
{
  Bool drawn = FALSE, full = FALSE ;
  dcoord left = 0, right = 0 ;

  spanlist_assert_valid(spanlist) ;

  for ( ; right < w ; ++bitmap ) {
    if ( right + BLIT_WIDTH_BITS < w && (*bitmap == 0 || *bitmap == ALLONES) ) {
      /* do a whole blit_t in one go */
      if ( *bitmap == ALLONES ) {
        if ( !drawn )
          left = right ;
        drawn = TRUE ;
      } else {
        if ( drawn ) {
          if ( full )
            return FALSE ; /* no room for this span */
          if ( !spanlist_insert(spanlist, left, right - 1) )
            full = TRUE ;
        }
        drawn = FALSE ;
      }
      right += BLIT_WIDTH_BITS ;
    } else {
      /* do it bit-by-bit */
      int32 bitshift ;
      for ( bitshift = BLIT_MASK_BITS ; right < w && bitshift >= 0 ; ++right, --bitshift ) {
        if ( ((*bitmap >> bitshift) & 0x1) != 0 ) {
          if ( !drawn )
            left = right ;
          drawn = TRUE ;
        } else {
          if ( drawn ) {
            if ( full )
              return FALSE ; /* no room for this span */
            if ( !spanlist_insert(spanlist, left, right - 1) )
              full = TRUE ;
          }
          drawn = FALSE ;
        }
      }
    }
  }
  if ( drawn ) {
    if ( full )
      return FALSE ; /* no room for this span */
    if ( !spanlist_insert(spanlist, left, right - 1) )
      full = TRUE ;
  }

  return TRUE ;
}

/* Remove a span from a spanlist. This function returns FALSE if the spanlist
   is full and more spans were required to clear the spanlist and it was full,
   TRUE if the span was cleared. */
Bool spanlist_delete(spanlist_t *spanlist, dcoord left, dcoord right)
{
  register span_t *span, *merge ;

  HQASSERT(spanlist, "No spanlist") ;
  HQASSERT(right >= left, "Empty intersection limits") ;
  HQASSERT(left > MINDCOORD, "Span includes sentinel value") ;

  for ( span = merge = spanlist->lastspan ; left <= span->right ; --span, --merge ) {
    /* There is the possibility that this span will remove part or all of a
       stored span. */
    if ( right >= span->left ) {
      /* Span intersects stored span. Remove part or all of the stored span,
         then continue to back up and merge until we hit the sentinel value
         at the start of the stored spans or find a span that doesn't
         intersect. */
      if ( left > span->left ) {
        /* Part of the existing span survives at the left. */
        if ( right < span->right ) {
          /* Part of the existing span survives at the right as well. Split
             the existing span, inserting a new span. It will be necessary to
             shuffle the spans up, since no other spans can have been
             removed. */
          HQASSERT(merge == span,
                   "Spans already removed when deleting sub-span") ;

          HQASSERT(spanlist->lastspan + 1 <= spanlist->spanlimit,
                   "No spare space in spanlist to split span") ;
          for ( merge = spanlist->lastspan++ ; merge > span ; --merge )
            merge[1] = merge[0] ;

          span[1].left = right + 1 ;
          span[1].right = span->right ;
        }

        span->right = left - 1 ;

        /* We must have finished the loop, since this span originally started
           before the excluded span. */
        break ;
      } else {
        /* None of the span survives at the left, but part may survive at
           the right. */
        if ( right < span->right ) {
          span->left = right + 1 ;
        } else {
          /* None of this span survives. Incrementing the merge pointer here
             compensates for the automatic decrement at the end of the loop.
             The merge pointer ends up pointing at the stored span completely
             right of the clipped span. */
          ++merge ;
        }
      }
    } /* else new span is disjoint and to the left of stored span. */
  }

  HQASSERT(span <= merge, "Span pointer cannot be less than merge pointer") ;
  if ( span != merge ) {
    span_t *last = spanlist->lastspan ;

    /* Spans were eliminated. Shuffle down remaining spans to close up the
       gap. */
    while ( merge < last )
      *++span = *++merge ;

    spanlist->lastspan = span ;
  }

  /* Determine if spanlist has space for more spans */
  return (spanlist->lastspan < spanlist->spanlimit) ;
}

/* Merge all of the abutting spans in the spanlist. This function returns
   FALSE if the spanlist is still full after the merge */
Bool spanlist_merge(spanlist_t *spanlist)
{
  register span_t *span, *merge, *last ;

  HQASSERT(spanlist, "No spanlist") ;
  HQASSERT(spanlist->lastspan <= spanlist->spanlimit,
           "Spanlist has overflowed") ;

  last = spanlist->lastspan ;
  merge = span = &spanlist->spans[0] ;

  while ( ++span <= last ) {
    HQASSERT(span->left > span[-1].right,
             "Spans out of order in spanlist merge") ;
    if ( merge->right + 1 == span->left )
      merge->right = span->right ;
    else if ( ++merge != span )
      *merge = *span ;
  }

  spanlist->lastspan = merge ;

  /* If there is a spare slot, return TRUE, indicating we can continue to add
     spans. */
  return (merge < spanlist->spanlimit) ;
}

/* Return the spanlist as a sequence of left,right coordinate pairs, occupying
   the coordinate memory originally passed to spanlist_init. The number of
   spans in the spanlist is returned. Spans may abut, but will not overlap in
   the output. Abutting spans may be merged by calling spanlist_merge before
   this function. */
uint32 spanlist_to_dcoords(const spanlist_t *spanlist, dcoord *coords)
{
  register const span_t *span, *last ;

  HQASSERT(spanlist, "No spanlist") ;
  HQASSERT(spanlist->lastspan <= spanlist->spanlimit,
           "Spanlist has overflowed") ;
  HQASSERT(coords, "No memory for spans") ;

  span = &spanlist->spans[0] ;
  last = spanlist->lastspan ;

  HQASSERT((char *)coords <= (char *)span ||
           (char *)coords >= (char *)(last + 1),
           "Coordinate memory overlaps spanlist memory") ;

  while ( ++span <= last ) {
    HQASSERT(span->left > span[-1].right,
             "Spans out of order in spanlist to span") ;
    coords[0] = span->left ;
    coords[1] = span->right ;
    coords += 2 ;
  }

  return CAST_PTRDIFFT_TO_UINT32(last - &spanlist->spans[0]) ;
}

/* Call a function for each span in the spanlist. Spans may abut each other;
   use spanlist_merge() to join abutting spans if necessary before calling
   this function. */
void spanlist_iterate(spanlist_t *spanlist,
                      BITBLT_FUNCTION callback,
                      render_blit_t *rb, dcoord y)
{
  register span_t *span, *last ;

  HQASSERT(spanlist, "No spanlist") ;
  HQASSERT(spanlist->lastspan <= spanlist->spanlimit,
           "Spanlist has overflowed") ;
  HQASSERT(callback, "No callback function") ;

  span = &spanlist->spans[0] ;
  last = spanlist->lastspan ;

  while ( ++span <= last ) {
    HQASSERT(span->left > span[-1].right,
             "Spans out of order in spanlist iterator") ;
    (*callback)(rb, y, span->left, span->right) ;
  }
}

/* Call a function for each spanlist span intersecting in the range left to
   right. The callback function is called once for each span in the range.
   Spans may abut each other; use spanlist_merge() to join abutting spans if
   necessary before calling this function. */
void spanlist_intersecting(const spanlist_t *spanlist,
                           BITBLT_FUNCTION black,
                           BITBLT_FUNCTION white,
                           render_blit_t *rb,
                           dcoord y, dcoord left, dcoord right, dcoord xoffset)
{
  const register span_t *span, *last, *limit ;
  ptrdiff_t n ;

  HQASSERT(spanlist, "No spanlist") ;
  HQASSERT(spanlist->lastspan <= spanlist->spanlimit,
           "Spanlist has overflowed") ;
  HQASSERT(black != NULL, "Black span callback must be set") ;

  /* Add the xoffset to the left, right span temporarily. The offset will be
     subtracted before passing to the callback functions. This is used to
     compensate for x_sep_position, which may have been added to the spans
     stored in a clipform, but will not have been added to the span which is
     being clipped yet. The callback function will usually add x_sep_position
     to the span position. */
  left += xoffset ;
  right += xoffset ;

  HQASSERT(right >= left, "Empty intersection limits") ;
  HQASSERT(left > MINDCOORD, "Span includes sentinel value") ;

  span = &spanlist->spans[1] ; /* Exclude sentinel */
  last = spanlist->lastspan ;

  /* Binary search to restrict span range for linear search. We test >2
     because the limits are exclusive, and the test is conservative anyway.
     We'll test for trimming the spans during the linear traversal anyway, so
     spending another iteration refining the exact start position is a waste
     of time. */
  for ( limit = last ; (n = (limit - span)) > 2 ; ) {
    const span_t *middle = span + (n >> 1) ;
    if ( middle->left <= left )
      span = middle ;
    else if ( middle->right >= left )
      limit = middle ;
    else
      break ;
  }

  HQASSERT(left > span[-1].right, "Didn't find correct start of spans") ;

  /* Linear search to read out spans of interest. */
  for ( ; span <= last && span->left <= right ; ++span ) {
    dcoord rtmp ;

    HQASSERT(span->left > span[-1].right,
             "Spans out of order in spanlist intersector") ;

    if ( span->right < left )
      continue ;

    if ( span->left > left ) {
      if ( white != NULL )
        (*white)(rb, y, left - xoffset, span->left - xoffset - 1) ;
      left = span->left ;
    }

    rtmp = span->right ;
    if ( rtmp > right )
      rtmp = right ;

    (*black)(rb, y, left - xoffset, rtmp - xoffset) ;

    left = rtmp + 1 ; /* Start of next white span is right of end of this */
  }

  if ( white != NULL && left <= right )
    (*white)(rb, y, left - xoffset, right - xoffset) ;
}

/* Clip spans in one spanlist to span limits supplied in another list. */
Bool spanlist_clipto(spanlist_t *dest, const spanlist_t *clipto)
{
  register span_t *dstnew, *dstspan, *dstlast ;
  register const span_t *clipspan ;

  HQASSERT(dest, "No destination spanlist") ;
  HQASSERT(dest->lastspan <= dest->spanlimit, "Spanlist has overflowed") ;
  HQASSERT(clipto, "No clip spanlist") ;
  HQASSERT(clipto->lastspan <= clipto->spanlimit, "Spanlist has overflowed") ;

  dstnew = dstspan = &dest->spans[1] ; /* Exclude sentinel */
  dstlast = dest->lastspan ;
  clipspan = &clipto->spans[0] ;

  while ( dstspan <= dstlast ) {
    span_t span = *dstspan++ ;

    /* dstspan always points at the next location we will get a span from.
       dstnew points to the next location we will write to. */
    HQASSERT(span.left > dstspan[-2].right,
             "Spans out of order in spanlist clipping") ;

    /* Iterate over clips, trimming the destination spans to
       to clip spans. */
    for (;;) {
      if ( clipspan->right < span.left ) {
        /* Clip is fully left of span, so won't affect it. */
        if ( ++clipspan > clipto->lastspan ) {
          /* Done clipping. */
          goto done ;
        }
        continue ;
      }
      if ( clipspan->left > span.right ) {
        /* Clip is fully right of span, so span should be dropped. */
        break ;
      }

      /* Clip now must intersect or contain span. If the destination would
         overwrite the next span we want to extract, then we need to insert
         a new span into the destination. This can happen when a single
         destination span is overlapped by two clip spans. The number of
         spans in the clipped result can overflow, the maximum number of
         output spans is n_dest + n_clipto - 1 */
      HQASSERT(dstnew <= dstspan,
               "Destination insertion position is beyond next span") ;
      if ( dstnew == dstspan ) {
        span_t *shuffle ;

        if ( dstlast >= dest->spanlimit ) {
          /* Destination is full up. The only way we can have got here is if
             we trimmed part of the previous span off, and now have an extra
             section to insert. Whatever happens, we want to leave the
             destination and clip in a state where we can retry the operation
             after converting to bitmap. So, assert that the span is
             contiguous, restore the original right position, and bail out. */
          HQASSERT(span.left == dstnew[-1].right + 1,
                   "Span is not contiguous with previously clipped span") ;
          dstnew[-1].right = span.right ;
          dest->lastspan = dstlast ;
          return FALSE ;
        }

        for ( shuffle = dstlast ; shuffle >= dstspan ; --shuffle )
          shuffle[1] = shuffle[0] ;

        ++dstlast ;
        ++dstspan ;
      }

      dstnew->left = max(clipspan->left, span.left) ;

      if ( clipspan->right < span.right ) {
        /* Clip finishes before span; reset span to exclude section we've
           used, so that next clip iteration drops the clip. */
        dstnew->right = clipspan->right ;
        ++dstnew ;
        span.left = clipspan->right + 1 ;
      } else {
        dstnew->right = span.right ;
        ++dstnew ;
        /* Done with this span, go find another */
        break ;
      }
    }
  }

 done:
  /* Reset last span of destination to last span written. */
  dest->lastspan = dstnew - 1 ;

  return TRUE ;
}

Bool spanlist_copy(spanlist_t *dest, const spanlist_t *src)
{
  ptrdiff_t nsrc = src->lastspan - &src->spans[0] ;

  if ( nsrc > dest->spanlimit - &dest->spans[0] )
    return FALSE ;

  /* Sentinels should be left alone, only copying spans. */
  HqMemCpy(&dest->spans[1], &src->spans[1], nsrc * sizeof(span_t)) ;
  dest->lastspan = &dest->spans[nsrc] ;
  return TRUE ;
}

#if defined(ASSERT_BUILD)
/* Assert that a spanlist is well-formed. */
void spanlist_assert_valid(spanlist_t *spanlist)
{
  span_t *span, *last ;

  HQASSERT(spanlist, "No spanlist pointer") ;

  last = spanlist->lastspan ;
  HQASSERT(last >= &spanlist->spans[0], "Last span pointer too small") ;
  HQASSERT(last <= spanlist->spanlimit, "Spanlist has overflowed") ;

  span = &spanlist->spans[0] ;
  HQASSERT(span->left == MINDCOORD && span->right == MINDCOORD,
           "Span list sentinel corrupted") ;

  while ( ++span <= last ) {
    HQASSERT(span->left <= span->right, "Span ends inverted") ;
    HQASSERT(span->left > span[-1].right, "Span overlaps previous") ;
  }
}

/* Unit test function for spanlists. Call interactively in the debugger. */
#define SPANLIST_TEST_SPANS 5 /* Space for 5 spans */
#define SPANLIST_TEST_SIZE (sizeof(spanlist_t) + SPANLIST_TEST_SPANS * sizeof(span_t))

void spanlist_unit_test(void)
{
  /* Ensure memory is pointer-aligned by declaring as uintptr_t array. */
  uintptr_t memory[SIZE_ALIGN_UP(SPANLIST_TEST_SIZE, sizeof(uintptr_t)) / sizeof(uintptr_t)] ;
  uintptr_t memcopy[SIZE_ALIGN_UP(SPANLIST_TEST_SIZE, sizeof(uintptr_t)) / sizeof(uintptr_t)] ;
  dcoord xycoords[SPANLIST_TEST_SPANS * 2], *mcoords ;
  spanlist_t *spanlist, *spancopy ;
  uint32 nspans ;

  HQASSERT(spanlist_init(memory, 0) == NULL,
           "Spanlist initialisation with no memory should not succeed") ;
  HQASSERT(spanlist_init(memory, spanlist_size(0)) == NULL,
           "Spanlist initialisation with too little memory should not succeed") ;

  spanlist = spanlist_init(memory, sizeof(memory)) ;
  HQASSERT(spanlist, "Spanlist initialisation failed") ;

  /* Check newly-created spanlist is valid, both through the pointer returned
     and by casting the memory pointer */
  spanlist_assert_valid(spanlist) ;
  spanlist_assert_valid((spanlist_t *)(char *)memory) ;

  spancopy = spanlist_init(memcopy, sizeof(memcopy)) ;
  HQASSERT(spancopy, "Spanlist copy initialisation failed") ;

  /* Check newly-created spanlist copy is valid, both through the pointer
     returned and by casting the memory pointer */
  spanlist_assert_valid(spancopy) ;
  spanlist_assert_valid((spanlist_t *)(char *)memcopy) ;

  /* Insert spans; these should not report that the spanlist is full */
  if ( !spanlist_insert(spanlist, 50, 60) ||  /* (1) initial span */
       !spanlist_insert(spanlist, 20, 30) ||  /* (2) disjoint left; new span */
       !spanlist_insert(spanlist, 15, 25) ||  /* overlap left; merge (2) */
       !spanlist_insert(spanlist, 70, 80) ||  /* (3) disjoint right; new span */
       !spanlist_insert(spanlist, 75, 85) ||  /* overlap right; merge (3) */
       !spanlist_insert(spanlist, 35, 45) ) { /* (4) insert in gap; new span */
    HQFAIL("Insertion should not have filled spanlist") ;
  }

  if ( spanlist_insert(spanlist, 61, 69) ) { /* (5) fill hole; new span */
    HQFAIL("Insertion should have filled spanlist") ;
  }

  /* At this point there should be five disjoint spans */
  spanlist_assert_valid(spanlist) ;
  nspans = spanlist_to_dcoords(spanlist, xycoords) ;
  HQASSERT(spanlist_count(spanlist) == nspans &&
           nspans == 5 &&
           xycoords[0] == 15 && xycoords[1] == 30 && /* (1) */
           xycoords[2] == 35 && xycoords[3] == 45 && /* (2) */
           xycoords[4] == 50 && xycoords[5] == 60 && /* (3) */
           xycoords[6] == 61 && xycoords[7] == 69 && /* (4) */
           xycoords[8] == 70 && xycoords[9] == 85,   /* (5) */
           "Span list was not as expected after insertions") ;

  if ( !spanlist_merge(spanlist) ) {
    HQFAIL("Merge should have made space in spanlist") ;
  }

  /* At this point there should be three disjoint spans */
  spanlist_assert_valid(spanlist) ;
  nspans = spanlist_to_dcoords(spanlist, xycoords) ;
  HQASSERT(spanlist_count(spanlist) == nspans &&
           nspans == 3 &&
           xycoords[0] == 15 && xycoords[1] == 30 && /* (1) */
           xycoords[2] == 35 && xycoords[3] == 45 && /* (2) */
           xycoords[4] == 50 && xycoords[5] == 85,   /* (3) */
           "Span list was not as expected after merge") ;

  if ( !spanlist_copy(spancopy, spanlist) ) {
    HQFAIL("Spanlist copy should have succeeded") ;
  }

  /* Check that the spanlist copy is the same as the original */
  spanlist_assert_valid(spancopy) ;
  nspans = spanlist_to_dcoords(spancopy, xycoords) ;
  HQASSERT(spanlist_count(spancopy) == nspans &&
           nspans == 3 &&
           xycoords[0] == 15 && xycoords[1] == 30 && /* (1) */
           xycoords[2] == 35 && xycoords[3] == 45 && /* (2) */
           xycoords[4] == 50 && xycoords[5] == 85,   /* (3) */
           "Span list was not as expected after copy") ;

  if ( !spanlist_insert(spanlist, 20, 40) ||  /* overlap; merges (1,2) */
       !spanlist_insert(spanlist, 60, 70) ||  /* sub-span (3); no new span */
       !spanlist_insert(spanlist, 10, 90) ) { /* super-span; merges (1-3) */
    HQFAIL("Insertion should not have filled spanlist") ;
  }

  spanlist_assert_valid(spanlist) ;
  nspans = spanlist_to_dcoords(spanlist, xycoords) ;
  HQASSERT(spanlist_count(spanlist) == nspans &&
           nspans == 1 &&
           xycoords[0] == 10 && xycoords[1] == 90,   /* (1) */
           "Span list was not as expected after insertions") ;

  if ( !spanlist_delete(spanlist, 20, 25) ||  /* splits (1) */
       !spanlist_delete(spanlist, 65, 70) ||  /* splits (1) again */
       !spanlist_delete(spanlist, 45, 50) ) { /* splits (1) again */
    HQFAIL("Deletion should not have filled spanlist") ;
  }

  if ( spanlist_delete(spanlist, 80, 85) ) { /* splits again */
    HQFAIL("Deletion should have filled spanlist") ;
  }

  /* At this point there should be five disjoint spans */
  spanlist_assert_valid(spanlist) ;
  nspans = spanlist_to_dcoords(spanlist, xycoords) ;
  HQASSERT(spanlist_count(spanlist) == nspans &&
           nspans == 5 &&
           xycoords[0] == 10 && xycoords[1] == 19 && /* (1) */
           xycoords[2] == 26 && xycoords[3] == 44 && /* (2) */
           xycoords[4] == 51 && xycoords[5] == 64 && /* (3) */
           xycoords[6] == 71 && xycoords[7] == 79 && /* (4) */
           xycoords[8] == 86 && xycoords[9] == 90,   /* (5) */
           "Span list was not as expected after deletions") ;

  if ( !spanlist_clipto(spancopy, spanlist) ) {
    HQFAIL("Spanlist clip failed") ;
  }

  spanlist_assert_valid(spancopy) ;
  nspans = spanlist_to_dcoords(spancopy, xycoords) ;
  HQASSERT(spanlist_count(spancopy) == nspans &&
           nspans == 5 &&
           xycoords[0] == 15 && xycoords[1] == 19 && /* (1) */
           xycoords[2] == 26 && xycoords[3] == 30 && /* (2) */
           xycoords[4] == 35 && xycoords[5] == 44 && /* (3) */
           xycoords[6] == 51 && xycoords[7] == 64 && /* (4) */
           xycoords[8] == 71 && xycoords[9] == 79,   /* (5) */
           "Span list was not as expected after deletions") ;

  if ( spanlist_merge(spanlist) ) {
    HQFAIL("Merge should not have made space in spanlist") ;
  }

  if ( spanlist_delete(spanlist, 40, 45) ||  /* trim right */
       spanlist_delete(spanlist, 25, 30) ||  /* trim left */
       spanlist_delete(spanlist, 35, 55) ) { /* trim right and left */
    HQFAIL("Delete should not have made space in spanlist") ;
  }

  if ( !spanlist_delete(spanlist, 0, 30) ||  /* delete leftmost */
       !spanlist_delete(spanlist, 80, 100) ) {  /* delete rightmost */
    HQFAIL("Delete should have made space in spanlist") ;
  }

  /* At this point there should be three disjoint spans */
  spanlist_assert_valid(spanlist) ;
  nspans = spanlist_to_dcoords(spanlist, xycoords) ;
  HQASSERT(spanlist_count(spanlist) == nspans &&
           nspans == 3 &&
           xycoords[0] == 31 && xycoords[1] == 34 && /* (1) */
           xycoords[2] == 56 && xycoords[3] == 64 && /* (2) */
           xycoords[4] == 71 && xycoords[5] == 79,   /* (3) */
           "Span list was not as expected after deletions") ;

  if ( !spanlist_delete(spanlist, 33, 75) ||  /* trim R, L, remove middle */
       !spanlist_delete(spanlist, 0, 100) ) { /* Nuke the lot */
    HQFAIL("Delete should have made space in spanlist") ;
  }

  /* At this point there should be no spans */
  spanlist_assert_valid(spanlist) ;
  nspans = spanlist_to_dcoords(spanlist, xycoords) ;
  HQASSERT(spanlist_count(spanlist) == nspans && nspans == 0,
           "Spans exist after deletion") ;

  if ( !spanlist_insert(spanlist, 0, 100) ||
       !spanlist_delete(spanlist, 37, 41) ) {
    HQFAIL("Insertions and deletions have had space in spanlist") ;
  }

  if ( spanlist_clipto(spancopy, spanlist) ) {
    HQFAIL("Spanlist clip should have overflowed space") ;
  }

  /* Check that the overflowed clip didn't mess up the original. */
  spanlist_assert_valid(spancopy) ;
  nspans = spanlist_to_dcoords(spancopy, xycoords) ;
  HQASSERT(spanlist_count(spancopy) == nspans &&
           nspans == 5 &&
           xycoords[0] == 15 && xycoords[1] == 19 && /* (1) */
           xycoords[2] == 26 && xycoords[3] == 30 && /* (2) */
           xycoords[4] == 35 && xycoords[5] == 44 && /* (3) */
           xycoords[6] == 51 && xycoords[7] == 64 && /* (4) */
           xycoords[8] == 71 && xycoords[9] == 79,   /* (5) */
           "Span list was not as expected after deletions") ;

  if ( !spanlist_insert(spanlist, 30, 70) ) {
    HQFAIL("Insertions and deletions have had space in spanlist") ;
  }

  /* Finally, compress the spanlist back into its own memory buffer. */
  mcoords = (dcoord *)(char *)memory ;
  nspans = spanlist_to_dcoords(spanlist, mcoords) ;
  HQASSERT(nspans == 1 &&
           mcoords[0] == 0 && mcoords[1] == 100,
           "Span list was not as expected after insertions and deletions") ;
}
#endif

/* Log stripped */
