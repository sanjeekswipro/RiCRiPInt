/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:toneblt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bitblit functions for contone output.
 */

#include "core.h"

#include "render.h"    /* x_sep_position */
#include "rlecache.h"  /* grleshift */
#include "bitblts.h"
#include "toneblt.h"
#include "spanlist.h"
#include "hqbitops.h" /* INLINE_MIN */

/* ---------------------------------------------------------------------- */
void charbltn(render_blit_t *rb,  FORM *formptr , dcoord x , dcoord y )
{
  /* We need to take the character bitmap and count up one bits until we
     have a span, and then fill (taking account of clipping of course) */

  int32 w, h, l, startskip;
  register blit_t *wordptr;
  dcoord x1c, x2c, y1c, y2c;

  HQASSERT(formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP ||
           formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_HALFTONEBITMAP, /* Pattern screens */
           "Char form is not bitmap") ;

  bbox_load(&rb->p_ri->clip, x1c, y1c, x2c, y2c) ;

  /* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  l = theFormL(*formptr) >> BLIT_SHIFT_BYTES ;
  wordptr = theFormA(*formptr) ;

  if ( y < y1c ) {
    wordptr += l * ( y1c - y );
    h -= y1c - y;
    y = y1c;
  }

  startskip = x1c - x ;
  if ( startskip < 0 )
    startskip = 0 ;
  w -= startskip;
  x += startskip;

  /* are we off the edge of the character because of clipping? */
  if ( x > x2c || y > y2c || x + w <= x1c || y + h <= y1c )
    return ;

  /* does the right clipping fall somewhere in the character; if so
     reduce width */
  if ( x + w - 1 > x2c )
    w = x2c - x + 1 ;

  /* ditto height for bottom */
  if ( y + h - 1 > y2c )
    h = y2c - y + 1;

  wordptr += ( startskip >> BLIT_SHIFT_BITS ) ;
  startskip &= BLIT_MASK_BITS ;

  /* start of scanline in output bitmap, incremented as we go */
  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                  ( y - theFormHOff(*rb->outputform) - rb->y_sep_position ) *
                  theFormL(*rb->outputform)) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                  ( y - theFormHOff(*rb->clipform) - rb->y_sep_position ) *
                  theFormL(*rb->clipform)) ;

  while ( h-- > 0 ) { /* for all (relevant) scan lines in input form */
    register blit_t *hereptr = wordptr;
    register blit_t temp = * hereptr ;
    register int32 wasblack = 0 ;
    register int32  xx = x, xe = x + w ;
    register int32 remaining = BLIT_WIDTH_BITS - startskip ;

    HQASSERT(remaining > 0 && remaining <= BLIT_WIDTH_BITS,
             "Skipped too many edge pixels") ;

    /* skip left edge pixels */
    temp = SHIFTLEFT (temp, startskip) ;

    do {
      if ( remaining == 0 ) {
        temp = * ( ++hereptr ) ;
        remaining = BLIT_WIDTH_BITS ;
      }

      if ( temp == 0 ) {
        if ( wasblack > 0 ) {
          DO_SPAN(rb, y , xx - wasblack , xx - 1 ) ;
        }
        xx += remaining ;
        wasblack = 0 ;
        remaining = 0 ;
      } else if ( temp == ALLONES ) {
        remaining = 0 ;
        xx += BLIT_WIDTH_BITS ;
        wasblack += BLIT_WIDTH_BITS ;
      } else {
        if ( temp & AONE ) {
          ++wasblack;
        } else if ( wasblack > 0 ) {
          DO_SPAN(rb, y , xx - wasblack , xx - 1 ) ;
          wasblack = 0;
        }
        --remaining ;
        temp = SHIFTLEFT (temp, 1);
        ++xx;
      }
    } while ( xx < xe ) ;

    /* any left over black not displayed? */
    xx -= wasblack;
    if ( xx < xe ) {
      DO_SPAN(rb, y , xx , xe - 1 ) ;
    }

    y++;
    wordptr += l ;
    rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr, theFormL(*rb->outputform)) ;
    rb->ymaskaddr = BLIT_ADDRESS(rb->ymaskaddr, theFormL(*rb->clipform)) ;
  }
}

void charbltspan(render_blit_t *rb,
                 FORM *formptr , dcoord x , dcoord y )
{
  int32 w, h, l, startskip;
  register blit_t *wordptr;
  dcoord x1c, x2c, y1c, y2c;

  HQASSERT(formptr->type == FORMTYPE_BANDRLEENCODED,
           "Char form is not span encoded") ;

  bbox_load(&rb->p_ri->clip, x1c, y1c, x2c, y2c) ;

  /* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  l = theFormL(*formptr) >> BLIT_SHIFT_BYTES ;
  wordptr = theFormA(*formptr) ;

  if ( y < y1c ) {
    wordptr += l * ( y1c - y );
    h -= y1c - y;
    y = y1c;
  }

  startskip = x1c - x ;
  if ( startskip < 0 )
    startskip = 0 ;
  w -= startskip;
  x += startskip;

  /* are we off the edge of the character because of clipping? */
  if ( x > x2c || y > y2c || x + w <= x1c || y + h <= y1c )
    return ;

  /* does the right clipping fall somewhere in the character; if so
     reduce width */
  if ( x + w - 1 > x2c )
    w = x2c - x + 1 ;

  /* ditto height for bottom */
  if ( y + h - 1 > y2c )
    h = y2c - y + 1;

  /* start of scanline in output bitmap, incremented as we go */
  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                  ( y - theFormHOff(*rb->outputform) - rb->y_sep_position ) *
                  theFormL(*rb->outputform)) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                  ( y - theFormHOff(*rb->clipform) - rb->y_sep_position ) *
                  theFormL(*rb->clipform)) ;

  while ( h-- > 0 ) { /* for all (relevant) scan lines in input form */
    spanlist_intersecting((spanlist_t *)wordptr, next_span, NULL,
                          rb, y, x, x + w, 0 /* raw spans */) ;

    y++;
    wordptr += l ;
    rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr, theFormL(*rb->outputform)) ;
    rb->ymaskaddr = BLIT_ADDRESS(rb->ymaskaddr, theFormL(*rb->clipform)) ;
  }
}

/* ---------------------------------------------------------------------- */

/* Call a span function for each span intersecting the black or white runs in
   a portion of a bitmap line. */
void bitmap_intersecting(const blit_t *clipptr,
                         BITBLT_FUNCTION black,
                         BITBLT_FUNCTION white,
                         render_blit_t *rb,
                         dcoord y, dcoord xs, dcoord xe, dcoord xoffset)
{
  /* convert this into a series of bitfills counting up 1 bits in the clip
     mask; similar in many ways to charbltn above, except it's one
     dimensional, and we don't necessarily start or end on a word boundary */
  register dcoord pxe ;
  blit_t state, word ;
  const uint8 *rlelookup ;
  const int32 *rleshift ;
  const blit_t *lastptr ;
  int32 i ;

  HQASSERT(black != NULL, "Black span callback must be set") ;

  lastptr = BLIT_ADDRESS(clipptr, BLIT_OFFSET(xe + xoffset)) ;
  clipptr = BLIT_ADDRESS(clipptr, BLIT_OFFSET(xs + xoffset)) ;

  word = *clipptr ;
  if ( clipptr == lastptr ) {
    if ( word == 0u ) {
      if ( white != NULL )
        (*white)(rb, y, xs, xe) ;
      return ;
    }
    if ( word == ALLONES ) {
      (*black)(rb, y, xs, xe) ;
      return ;
    }
  }

  rlelookup = rlelength ;
  rleshift = grleshift ;

  state = 0 ; /* white */

  i = (xs + xoffset) & BLIT_MASK_BITS ;
  word &= SHIFTRIGHT(ALLONES, i) ;
  /* Set the finish of the previous span one before the blit_t word in which
     the first span starts. We'll be adding the white width to this position
     immediately, which should leave the span finish position either just
     before the first span (if the first span is black), or somewhere beyond
     xs (if the first span is white). */
  pxe = xs - i - 1 ;

  for (;;) {
    if ( clipptr == lastptr ) {
      i = BLIT_MASK_BITS - ((xe + xoffset) & BLIT_MASK_BITS) ;
      word &= SHIFTLEFT(ALLONES, i) ;
    }

    if ( word == state ) {
      pxe += BLIT_WIDTH_BITS ;
    } else {
      for ( i = 0 ; i < BLIT_WIDTH_BYTES ; ++i ) {
        blit_t byte = ((word ^ state) >> rleshift[i]) ;
        byte &= 0xff ;
        if ( ! byte ) {
          pxe += 8 ;
        } else {
          int32 inside = rlelookup[ byte ] ;
          pxe += inside ;
          while ( inside != 8 ) {
            if ( state ) {
              HQASSERT(pxe <= xe, "Overflowed extraction length") ;
              (*black)(rb, y, xs, pxe) ;
            } else if ( white != NULL ) {
              /* White can overflow the maximum length allowed, because
                 we mask the extra bits in the last word to clear. */
              INLINE_MIN32(pxe, pxe, xe) ;
              /* We can also have an empty white span at the start due to
                 adjusting the start position to a blit_t boundary, and
                 masking the first blit word. */
              if ( xs <= pxe )
                (*white)(rb, y, xs, pxe) ;
            }
            xs = pxe + 1 ;

            byte = ~byte ;
            byte &= SHIFTRIGHT( 0xff , inside ) ;
            byte &= 0xff ; /* needed if bitsgoleft */

            state = ~state ;
            {
              int32 tb = rlelookup[byte] ;
              pxe += tb - inside ;
              inside = tb ;
            }
          }
        }
      }
    }

    if ( ++clipptr > lastptr )
      break ;

    word = *clipptr ;
  }

  HQASSERT(clipptr - 1 == lastptr, "Clip pointer did not terminate correctly") ;
  if ( xe >= xs ) {
    if ( state )
      (*black)(rb, y, xs, xe) ;
    else if ( white != NULL )
      (*white)(rb, y, xs, xe) ;
  }
}

void bitclipn(render_blit_t *rb, dcoord y,
              dcoord xs, dcoord xe, BITBLT_FUNCTION fillspan)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipn") ;

  if ( theFormT(*rb->clipform) == FORMTYPE_BANDRLEENCODED ) {
    spanlist_intersecting((spanlist_t *)rb->ymaskaddr, fillspan, NULL, rb,
                          y, xs, xe, rb->x_sep_position) ;
  } else {
    HQASSERT(theFormT(*rb->clipform) == FORMTYPE_BANDBITMAP,
             "Clip form is neither RLE spanlist nor bitmap") ;
    bitmap_intersecting(rb->ymaskaddr, fillspan, NULL, rb,
                        y, xs, xe, rb->x_sep_position) ;
  }
}

void blkclipn(render_blit_t *rb,
              register dcoord ys, register dcoord ye, dcoord xs, dcoord xe,
              BITBLT_FUNCTION fillspan)
{
  render_blit_t rb_copy = *rb ;
  register int32 wupdate = theFormL(*rb_copy.outputform) ;
  register int32 wclipupdate = theFormL(*rb_copy.clipform) ;

  BITCLIP_ASSERT(rb, xs, xe, ys, ye, "blkclipn") ;

  if ( theFormT(*rb_copy.clipform) == FORMTYPE_BANDRLEENCODED ) {
    do {
      spanlist_intersecting((spanlist_t *)rb_copy.ymaskaddr, fillspan, NULL,
                            &rb_copy, ys, xs, xe, rb_copy.x_sep_position) ;

      rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
      rb_copy.ymaskaddr = BLIT_ADDRESS(rb_copy.ymaskaddr, wclipupdate);
    } while ( ++ys <= ye ) ;
  } else {
    HQASSERT(theFormT(*rb_copy.clipform) == FORMTYPE_BANDBITMAP,
             "Clip form is neither RLE spanlist nor bitmap") ;
    do {
      bitmap_intersecting(rb_copy.ymaskaddr, fillspan, NULL, &rb_copy,
                          ys, xs, xe, rb_copy.x_sep_position) ;

      rb_copy.ylineaddr = BLIT_ADDRESS(rb_copy.ylineaddr, wupdate);
      rb_copy.ymaskaddr = BLIT_ADDRESS(rb_copy.ymaskaddr, wclipupdate);
    } while ( ++ys <= ye ) ;
  }
}

typedef struct blkclipn_t {
  render_blit_t rb ;
  dbbox_t bbox ;
  dcoord yline ;
  int32 forml ;
  BLKBLT_FUNCTION fillblock ;
} blkclipn_t ;

static void fillspan_coalesce(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blkclipn_t *coalesce = (blkclipn_t *)((char *)rb - offsetof(blkclipn_t, rb)) ;
  dbbox_t *bbox = &coalesce->bbox ;

  if ( bbox_is_empty(bbox) ) {
    bbox_store(bbox, xs, y, xe, y) ;
  } else if ( xs == bbox->x1 && xe == bbox->x2 && y == bbox->y2 + 1 ) {
    ++bbox->y2 ;
  } else {
    rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr,
                                 coalesce->forml * (bbox->y1 - coalesce->yline)) ;
    coalesce->yline = bbox->y1 ;
    (*coalesce->fillblock)(rb, bbox->y1, bbox->y2, bbox->x1, bbox->x2) ;
    bbox_store(bbox, xs, y, xe, y) ;
  }
}

/** blkclipn_coalesce is similar to blkclpn but takes a fillblock callback
    instead of a fillspan callback.  Clipping involves splitting blocks into
    spans, and blkclipn_coalesce attempts to vertically merge the spans back
    into blocks when possible.  There is a small overhead for coalescing spans,
    but if the underlying blit exploits vertical coherence (as backdrop block
    blits do) then the gains from coalescing can be large. */
void blkclipn_coalesce(render_blit_t *rb,
                       register dcoord ys, register dcoord ye,
                       dcoord xs, dcoord xe, BLKBLT_FUNCTION fillblock)
{
  blkclipn_t coalesce ;
  register int32 wclipupdate ;

  coalesce.rb = *rb ;
  coalesce.rb.clipmode = BLT_CLP_NONE ;
  bbox_clear(&coalesce.bbox) ;
  coalesce.yline = ys ;
  coalesce.forml = theFormL(*coalesce.rb.outputform) ;
  coalesce.fillblock = fillblock ;
  wclipupdate = theFormL(*coalesce.rb.clipform) ;

  BITCLIP_ASSERT(&coalesce.rb, xs, xe, ys, ye, "blkclipn_coalesce") ;

  if ( theFormT(*coalesce.rb.clipform) == FORMTYPE_BANDRLEENCODED ) {
    do {
      spanlist_intersecting((spanlist_t *)coalesce.rb.ymaskaddr,
                            fillspan_coalesce, NULL, &coalesce.rb,
                            ys, xs, xe, coalesce.rb.x_sep_position) ;

      coalesce.rb.ymaskaddr = BLIT_ADDRESS(coalesce.rb.ymaskaddr, wclipupdate);
    } while ( ++ys <= ye ) ;
  } else {
    HQASSERT(theFormT(*coalesce.rb.clipform) == FORMTYPE_BANDBITMAP,
             "Clip form is neither RLE spanlist nor bitmap") ;
    do {
      bitmap_intersecting(coalesce.rb.ymaskaddr,
                          fillspan_coalesce, NULL, &coalesce.rb,
                          ys, xs, xe, coalesce.rb.x_sep_position) ;

      coalesce.rb.ymaskaddr = BLIT_ADDRESS(coalesce.rb.ymaskaddr, wclipupdate);
    } while ( ++ys <= ye ) ;
  }

  if ( !bbox_is_empty(&coalesce.bbox) ) {
    coalesce.rb.ylineaddr = BLIT_ADDRESS(coalesce.rb.ylineaddr,
                                         coalesce.forml * (coalesce.bbox.y1 - coalesce.yline)) ;
    (*coalesce.fillblock)(&coalesce.rb, coalesce.bbox.y1, coalesce.bbox.y2,
                          coalesce.bbox.x1, coalesce.bbox.x2) ;
  }
}

/* Log stripped */
