/** \file
 * \ingroup fontcache
 *
 * $HopeName: SWv20!src:rlecache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * RLE font and userpath caching functions. The RLE format used here is NOT
 * the same as the output RLE format.
 */

#include "core.h"
#include "swdevice.h"
#include "objects.h"
#include "hqmemcpy.h"
#include "basemap.h"
#include "fontparam.h"
#include "swerrors.h"

#include "control.h" /* handleLowMemory */
#include "bitblts.h"
#include "display.h"
#include "params.h"
#include "formOps.h"
#include "render.h"
#include "ndisplay.h"
#include "rlecache.h"
#include "plotops.h"
#include "routedev.h"

/** \page rlechar The structure of run length encoded characters:

Run length encoded characters, userpaths, and rotated image tiles are stored
in "form" bitmaps, just like the uncompressed variety, except that the
information represents runs of common data rather than each pixel bit by
bit. Whether a character is stored in run length form or explicitly (or indeed
not at all) is determined by the PostScript operator setcacheparams which
gives size thresholds at which each technique should be employed.

The form header contains much the same information as the uncompacted
character: theFormH is the height of the character in pixels, theFormA points
to the data; the FormL is the width of the bitmap in bytes (were it to be
expanded), and theFormW, the width of the character in pixels).  theFormS is
the size of the actual form (not the notional bitmap) in bytes. However,
theFormT is set to one of eight values from FORMTYPE_CACHERLE1 to
FORMTYPE_CACHERLE8 to indicate that it isn't an ordinary bitmap.

The run length encoding scheme is two dimensional: that is repeated scan lines
are represented by a repeat count, as well as spans of the same colour within a
scan line.

Each set of repeated scan lines is represented by a repeat count (one byte)
followed by a length of the data (one byte); the length does NOT include the
repeat count byte or the length byte itself. The repeat count represents the
number of lines with the same data.

The length measures the number of bytes of data in the line. Each line contains
white span, black span pairs. Each line starts with a white span, then a black
span, alternating until the data is exhausted. Trailing white spans are
omitted; it is possible that there may be no data at all for a blank line
(just a non-zero repeat count and zero length).

The spans are represented by a nibble-coding scheme. The number of nibbles
used for each span is initially determined by the form type:
FORMTYPE_CACHERLE1 indicates that 1 nibble is used, up to FORMTYPE_CACHERLE8,
which indicates that 8 nibbles are used. The high nibble of each byte is
considered to be first; this allows multi-nibble spans to extract whole bytes
at a time when decoding.

The span width is normally represented by the span data plus one. If a span
width overflows the number of nibbles that the formtype uses, there are two
escape values which can be used. The first escape value indicates that the
width is between the maximum number representable in that number of nibbles
and twice the maximum number representable. The second escape value is a
general extension mechanism, which indicates that the number requires at least
one more nibble to represent it; the number of nibbles is temporarily
increased, and the whole extraction process is retried, with the span width
normalised by twice the maximum representable number (which was the limit of
what could be represented by the first escape value).

The following table indicates how values are stored, where max is the maximum
number representable in the number of nibbles (e.g. 0xF for one nibble, 0xFF
for two, etc.):

        Extracted value(s)      Span width
        0..(max - 2)            1..(max - 1)
        (max - 1)               max + next value in same num_nibbles (*)
        max                     max * 2 + next span width in num_nibbles+1 (+)

        (*) This is just a straight value, with no escapes.
        (+) This is a number represented using the escapes, so it may
            overflow again.

Note that span widths of zero cannot be represented in this scheme; these would
only be used at the left hand edge of the form, if the first span is black. To
compensate for this, an extra white column is added to the left hand edge of
the form, and the form is rendered one pixel to the left. In practice, many
forms start with a white span anyway, so the extra one white row will be
incorporated into the first span count.

In 1995 or thereabouts, Danny Hall and Angus Duggan invented an adaptive
nibble-coding scheme, which achieved much better compression ratios than
this scheme, but suffered the fatal flaw that in order to extract the runs
for a line, all of the previous lines have to be decoded in full. This is
a problem when band boundary clipping is taken into account; successive
bands have to do more and more work to get to the start of the usable
runs. It would be nice to find a compression scheme to replace this with
that compresses characters and userpaths well, but also has a look-ahead
that would allow easy decoding from arbitrary Y offsets.
*/

/** The span structure is used to merge spans resulting from fill ends (caps)
    with the internal fill spans. This will be replaced by the coalescing
    span blits in future. */
typedef struct {
  dcoord x1, x2 ;
} SPAN ;

#define theISpanL(val) ((val)->x1)
#define theISpanR(val) ((val)->x2)

/* --- Internal Functions --- */

#define RLECACHEMAX ((1 << (sizeof(uint8) * 8)) - 1)

static uint32 max_nibble[] = {
  0, 0xF, 0xFF, 0xFFF, 0xFFFF, 0xFFFFF, 0xFFFFFF, 0xFFFFFFF, 0xFFFFFFFF
} ;

static int32 num_nibbles ;      /* Number of nibbles encoding runs */

/* Auxiliary variables and routines for building RLE spans and counts */
static uint8 *rlebase, *rlebuf, *rlelimit, *repeatptr, *lengthptr ;
static uint32 rlesema ; /* basemap1 semaphore */

#define GET_NIBBLES(dest, numnibbles, memory, current, first) MACRO_START \
  register uint32 _gnibbles = (numnibbles) ;                            \
  register uint32 _span = 0 ;                                           \
                                                                        \
  if ( ! (first) ) {                                                    \
    _span = ((current) & 0xF) ;                                         \
    (first) = TRUE ;                                                    \
    _gnibbles-- ;                                                       \
  }                                                                     \
  while ( _gnibbles >= 2 ) {                                            \
    _span <<= 8 ;                                                       \
    _span |= *(memory)++ ;                                              \
    _gnibbles -= 2 ;                                                    \
  }                                                                     \
  if ( _gnibbles > 0 ) {                                                \
    (current) = *(memory)++ ;                                           \
    HQASSERT(((current) >> 4) <= 0xF, "Bogus current value in GET_NIBBLES") ; \
    _span <<= 4 ;                                                       \
    _span |= ((current) >> 4) ;                                         \
    (first) = FALSE ;                                                   \
  }                                                                     \
                                                                        \
  (dest) = _span ;                                                      \
MACRO_END

#define PUT_NIBBLES(value, numnibbles, memory, current, first) MACRO_START \
  int32 _nshift = (numnibbles) * 4 ;                                    \
  HQASSERT( _nshift >= 4, "Bogus nibble shift in PUT_NIBBLES (1)") ;    \
  if ( ! (first) ) {                                                    \
    _nshift -= 4 ;                                                      \
    HQASSERT(((value) >> _nshift) <= 0xF, "Bogus top nibble in PUT_NIBBLES") ; \
    *(memory)++ = (uint8)((current) | ((value) >> _nshift)) ;           \
    (first) = TRUE ;                                                    \
  }                                                                     \
  while ( _nshift >= 8 ) {                                              \
    _nshift -= 8 ;                                                      \
    HQASSERT( _nshift >= 0, "Bogus nibble shift in PUT_NIBBLES (2)") ;  \
    *(memory)++ = (uint8)(((value) >> _nshift) & 0xFF) ;                \
  }                                                                     \
  if ( _nshift > 0 ) {                                                  \
    HQASSERT( _nshift == 4, "Bogus nibble shift in PUT_NIBBLES (3)") ;  \
    (current) = (uint8)(((value) & 0xF) << 4) ;                         \
    (first) = FALSE ;                                                   \
  }                                                                     \
MACRO_END

#define PUT_SPAN(width, memory, limit, current, first, FAILURE) MACRO_START \
  int32 _nibbles = num_nibbles ;                                        \
  register uint32 _span = (uint32)(width) ;                             \
                                                                        \
  for (;;) {                                                            \
    register uint32 _max = max_nibble[_nibbles] ;                       \
                                                                        \
    /* conservative estimate of max memory needed */                    \
    if ( (uint8 *)(memory) + _nibbles + 1 > (uint8 *)(limit) ) {        \
      FAILURE ; /* must return or break */                              \
    }                                                                   \
    if ( _span >= _max ) {                                              \
      _span -= _max ;                                                   \
      if ( _span > _max ) {                                             \
        PUT_NIBBLES(_max, _nibbles, (memory), (current), (first)) ;     \
        _nibbles++ ;                                                    \
        _span -= _max ;                                                 \
        continue ;      /* Try again with larger nibble count */        \
      }                                                                 \
      PUT_NIBBLES(_max - 1, _nibbles, (memory), (current), (first)) ;   \
    } else                                                              \
      _span -= 1 ;                                                      \
                                                                        \
    PUT_NIBBLES(_span, _nibbles, (memory), (current), (first)) ;        \
    break ;                                                             \
  }                                                                     \
MACRO_END

#define FLUSH_NIBBLES(memory, current, first) MACRO_START               \
  if ( ! (first) )                                                      \
    *(memory)++ = (current) ;                                           \
  (first) = TRUE ;                                                      \
MACRO_END

#define RLE_STARTLINE(FAILURE) MACRO_START                              \
  repeatptr = rlebuf ;                                                  \
  lengthptr = rlebuf + 1 ;                                              \
  rlebuf += 2 ;                                                         \
  if ( rlebuf > rlelimit ) {                                            \
    FAILURE ;                                                           \
  }                                                                     \
MACRO_END

#define RLE_ENDLINE(repeatcount, FAILURE) MACRO_START                   \
  int32 _repeat = (repeatcount) ;                                       \
  int32 _length ;                                                       \
                                                                        \
  _length = CAST_PTRDIFFT_TO_INT32((uint8 *)rlebuf - (uint8 *)repeatptr) ; /* inc repeat & length */ \
                                                                        \
  if ( _length - 2 <= RLECACHEMAX ) {                                   \
    *lengthptr = (uint8)(_length - 2) ;                                 \
                                                                        \
    while ( _repeat > RLECACHEMAX ) {                                   \
      if ( rlebuf + _length > rlelimit ) {                              \
        FAILURE ;       /* must return or break */                      \
      }                                                                 \
      HqMemMove( rlebuf , repeatptr , _length ) ;                       \
      *repeatptr = RLECACHEMAX ;                                        \
      repeatptr = rlebuf ;                                              \
      rlebuf += _length ;                                               \
      _repeat -= RLECACHEMAX ;                                          \
    }                                                                   \
                                                                        \
    *repeatptr = (uint8)_repeat ;                                       \
  } else {                                                              \
    FAILURE ;                                                           \
  }                                                                     \
MACRO_END

/* Grr...stupid macro needed for warnings reduction */
#define RLE_ENDONE(FAILURE) MACRO_START                                 \
  int32 _length ;                                                       \
                                                                        \
  _length = CAST_PTRDIFFT_TO_INT32((uint8 *)rlebuf - (uint8 *)repeatptr) ; /* inc repeat & length */ \
                                                                        \
  if ( _length - 2 <= RLECACHEMAX ) {                                   \
    *lengthptr = (uint8)(_length - 2) ;                                 \
    *repeatptr = 1 ;                                                    \
  } else {                                                              \
    FAILURE ;                                                           \
  }                                                                     \
MACRO_END

#define RLE_REPEAT(FAILURE) MACRO_START                                 \
  uint8 _repeat ;                                                       \
                                                                        \
  HQASSERT(repeatptr, "repeat pointer not set") ;                       \
  HQASSERT(lengthptr, "length pointer not set") ;                       \
                                                                        \
  _repeat = *repeatptr ;        /* get previous repeat count */         \
                                                                        \
  if ( _repeat == RLECACHEMAX ) { /* overflow, copy the data again! */  \
    int32 _length = (int32) *lengthptr + 2 ;                            \
                                                                        \
    if ( rlebuf + _length > rlelimit ) {                                \
      FAILURE ; /* must return or break */                              \
    }                                                                   \
    HqMemMove( rlebuf , repeatptr , _length ) ;                         \
    repeatptr = rlebuf ;                                                \
    rlebuf += _length ;                                                 \
    _repeat = 1 ;                                                       \
  } else                                                                \
    _repeat += 1 ;                                                      \
                                                                        \
  *repeatptr = _repeat ;                                                \
MACRO_END

/* --- Exported Variables --- */
/* RLE shifts. */
int32 grleshift[ BLIT_WIDTH_BYTES ] = {
  SHIFT1 , SHIFT2 , SHIFT3 , SHIFT4 ,
#if BLIT_WIDTH_BYTES > 4
  SHIFT5 , SHIFT6 , SHIFT7 , SHIFT8 ,
#endif
} ;

/* Forward declarations */
static int32 rlecachesize(FORM *theform, int32 *rletype, uint8 *bufptr,
                          uint32 bufsize) ;
static void rle_dospan(render_blit_t *rb,
                       register dcoord y, register dcoord xs, register dcoord xe ) ;

#ifdef METRICS_BUILD
#include "metrics.h"

static struct rlecache_metrics {
  sw_metric_histogram_t(8) rleformtypes ;
  int32 rletotal ;
  int32 rlechars ;
} rlecache_metrics ;

static Bool rlecache_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Fonts")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Cache")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("RLEChars")) )
    return FALSE ;
  SW_METRIC_INTEGER("chars", rlecache_metrics.rlechars) ;
  SW_METRIC_INTEGER("bytes", rlecache_metrics.rletotal) ;
  if ( !sw_metric_histogram(metrics,
                            METRIC_NAME_AND_LENGTH("chars_per_nibble"),
                            &rlecache_metrics.rleformtypes.info) )
    return FALSE ;
  sw_metrics_close_group(&metrics) ; /*RLEChars*/
  sw_metrics_close_group(&metrics) ; /*Cache*/
  sw_metrics_close_group(&metrics) ; /*Fonts*/

  return TRUE ;
}

static void rlecache_metrics_reset(int reason)
{
  struct rlecache_metrics init = {0};
  UNUSED_PARAM(int, reason) ;
  rlecache_metrics = init;
  sw_metric_histogram_reset(&rlecache_metrics.rleformtypes.info,
                            SW_METRIC_HISTOGRAM_SIZE(rlecache_metrics.rleformtypes),
                            SW_METRIC_HISTOGRAM_LINEAR,
                            /* Linear mapping with 1:1 correspondence */
                            1, SW_METRIC_HISTOGRAM_SIZE(rlecache_metrics.rleformtypes) + 1) ;
}

static sw_metrics_callbacks rlecache_metrics_hook = {
  rlecache_metrics_update,
  rlecache_metrics_reset,
  NULL
} ;
#endif /*METRICS_BUILD*/

/* See header for doc. */
void rlecache_line_read_init(RLECACHE_LINE_READ_STATE* state,
                             FORM* form,
                             uint8* memory)
{
  state->row_height = memory[0];
  state->next_line = memory + memory[1] + 2;
  state->memory = memory + 2;
  state->line_finished = state->memory >= state->next_line;

  state->first = TRUE;
  state->first_pair_on_line = TRUE;
  state->current = 0;
  state->nibbles = theFormT(*form) - FORMTYPE_CACHERLE1 + 1 ;
  HQASSERT(state->nibbles > 0 || state->nibbles < 9, "Unsupported form type.");
}

/* See header for doc. */
void rlecache_get_span_pair(RLECACHE_LINE_READ_STATE* state, int32* pair)
{
  int32 i;

  HQASSERT(! state->line_finished, "Line should not be finished.") ;

  pair[0] = pair[1] = 0;

  for (i = 0; i < 2; i ++) {
    int32 nibbles = state->nibbles;
    for (;;) {
      uint32 max = max_nibble[nibbles] ;
      uint32 val ;

      GET_NIBBLES(val, nibbles, state->memory, state->current, state->first) ;
      if ( val == max ) {
        nibbles++ ;
        pair[i] += max * 2 ;
      } else {
        val += 1 ;
        pair[i] += val ;
        if ( val == max ) {
          GET_NIBBLES(val, nibbles, state->memory, state->current, state->first) ;
          pair[i] += val ;
        }
        break ;
      }
    }
    HQASSERT(pair[i] > 0, "Invalid span width.") ;
  }

  if (state->first_pair_on_line) {
    state->first_pair_on_line = FALSE;
    /* The RLE encoding works on pairs of white/black spans (neither of which
     * can be zero in length); since a character may not have any white at the
     * left edge, a single white column is added to the left edge, thus the
     * first white run length on a line is one larger than it should be. */
    pair[0] --;
    HQASSERT(pair[0] >= 0, "Invalid first white span length.");
  }

  if (state->memory >= state->next_line) {
    state->line_finished = TRUE ;
    HQASSERT(state->memory == state->next_line, "Read excess data.") ;
  }
}

/** Possibly converts the form to RLE format. */
FORM *form_to_rle( CHARCACHE *thechar , int32 forcerlesize )
{
  corecontext_t *context = get_core_context_interp();
  int32 oldsize ;
  int32 newsize ;
  int32 newtype = -1 ; /* Init with arbitrary value to silence compiler */
  FORM *oldform ;
  FORM *newform = NULL;
  void *buffer ;
  uint32 bufsize ;
  mm_size_t oldAllocSize, newAllocSize;

  /* Invalidate front-end clip map if we're in a character, since the clipmap
     uses basemap1 for the bitmap, which we're about to scribble on. */
  if ( CURRENT_DEVICE() == DEVICE_CHAR )
    clipmapid = -1 ;

  oldform = theForm(*thechar) ;
  oldsize = theFormS(*oldform) ;

  HQASSERT(!got_basemap_semaphore(rlesema), "Old basemap semaphore is valid") ;
  /* Find out size and type of RLE cached form, and encode into basemap1.
     MUST call free_basemap_semaphore before returning, error or not. */
  if ( (rlesema = get_basemap_semaphore(&buffer, &bufsize)) == 0 ) {
    theFormT(*oldform) = FORMTYPE_CACHEBITMAP ;
    return NULL ;
  }

  thechar->rlesize = newsize = rlecachesize(oldform, &newtype, buffer, bufsize) ;
  newAllocSize = ALIGN_FORM_SIZE(newsize);
  oldAllocSize = ALIGN_FORM_SIZE(oldsize);

  if (( newsize < 0 ) ||
      ( newsize >= oldsize && forcerlesize == 0 )) {
    theFormT(*oldform) = FORMTYPE_CACHEBITMAP;
    goto cleanup;
  }
  newform = make_RLEForm(theFormW(*oldform), theFormH(*oldform),
                         newsize, newtype);
  if ( newform == NULL ) {
    error_clear_context(context->error);
    theFormT(*oldform) = FORMTYPE_CACHEBITMAP;
    goto cleanup;
  }

  /* Cast sizes to int32 before doing arithmetic as result may be
     negative. */
  context->fontsparams->CurFontCache +=
    (CAST_SIZET_TO_INT32(newAllocSize) - CAST_SIZET_TO_INT32(oldAllocSize));

  /* This bit here tries to shuffle the form down a bit in memory to see if
     we can defragment the char cache. Only try it if the new (RLE) form is
     smaller than the bitmap form. */
  if (( theFormA(*newform) < theFormA(*oldform)) &&
      ( newAllocSize <= oldAllocSize )) {
    destroy_Form( oldform ) ;
    if (( oldform = make_RLEForm(theFormW(*newform) ,
                         theFormH(*newform) , newsize, newtype )) != NULL)  {
      FORM *tmpform ;
      tmpform = newform ; newform = oldform ; oldform = tmpform ;
    }
  }
  HqMemCpy( theFormA(*newform) , buffer , newsize ) ;

  if ( oldform )
    destroy_Form( oldform ) ;
  theForm(*thechar) = newform ;
 cleanup:
  free_basemap_semaphore(rlesema) ;
  return  newform  ;
}


/** Scan converts the RLE encoded char - see notes above re structure of form. */
void rlechar(render_blit_t *rb, FORM *theform , dcoord sx , dcoord sy )
{
  register dcoord x1 , x2 , ey, h ;
  register int32 wupdate , wclipupdate;
  register dcoord x1c , x2c ;
  dcoord y1c, y2c;
  const dbbox_t *clip ;
  /* register */ uint8 *memory ;
  uint8 repeat ;
  int32 temp ;
  RLECACHE_LINE_READ_STATE state ;
  int32 span_lengths[2] ;

  HQASSERT( theFormH(*theform) > 0, "Rlechar height zero") ;
  HQASSERT( theFormW(*theform) > 0, "Rlechar width zero") ;

/* Preclip bounding box. */
  clip = &rb->p_ri->clip ;
  h = theFormH(*theform) ;
  y1c = clip->y1;
  if ( sy + h <= y1c )
    return ;
  y2c = clip->y2;
  if ( sy > y2c )
    return ;
  x2c = clip->x2;
  if ( sx > x2c )
    return ;
  x1c = clip->x1;
  if ( sx + theFormW(*theform) <= x1c )
    return ;

  memory = (uint8 *)theFormA(*theform) ;

  /* Partial clip of the top. */
  if ( ( temp = sy - y1c ) < 0 ) {
    for (;;) {  /* Skip block if totally clipped. */
      temp += memory[0] ;
      if ( temp > 0 )
        break ;
      memory += 2 + memory[1] ;

      /* Bail out if we've read all the data in the form */
      if ( memory >= (uint8 *)theform->addr + theform->size ) {
        HQFAIL("Off the end of a form");
        return;
      }
    }
    repeat =  (uint8)temp ;
    h -= y1c - sy;
    sy = y1c;
  }
  else {
    repeat = memory[0] ;
  }

  /* Partial clip of the bottom. */
  temp = sy + h ;
  if ( temp > y2c )
    h -= ( temp - y2c - 1 );

  if ( h <= 0 )
    return ;

  if ( h < (int32)repeat )
    repeat = (uint8)h ;

  /* Go for it.. */
  wupdate = theFormL(*rb->outputform) ;
  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
    wupdate * (sy - theFormHOff(*rb->outputform) - rb->y_sep_position)) ;
  wclipupdate = theFormL(*rb->clipform) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
    wclipupdate * (sy - theFormHOff(*rb->clipform) - rb->y_sep_position)) ;

  /* use end coordinates 1 past the end of the span to reduce */
  /* number of +1 and -1 calculations */
  ++x2c ;
  state.next_line = memory;
  for ( ;; ) {
    rlecache_line_read_init(&state, theform, state.next_line);

    ey = sy + (int32)repeat - 1 ;
    x2 = sx ;   /* Start of first white span. */

    /* Last nibble can be ignored, because it would be a single white span */
    while ( ! state.line_finished ) {
      rlecache_get_span_pair(&state, span_lengths) ;
      x1 = x2 + span_lengths[0] ; /* White span - ignore. */
      x2 = x1 + span_lengths[1] ; /* Black span. */

      if ( x1 < x1c )
        x1 = x1c ;

      if ( x2 >= x2c ) { /* Touching or clipped off to the right, so ignore rest. */
        if ( x1 < x2c )
          DO_BLOCK(rb, sy, ey, x1, x2c - 1 );
        break ;
      }
      if ( x1 < x2 )
        DO_BLOCK(rb, sy, ey, x1, x2 - 1 ) ;
    }

    if ( ( h -= (int32)repeat ) <= 0 )
      return ;

    {
      int32 rows = ey - sy + 1 ;
      rb->ylineaddr = BLIT_ADDRESS(rb->ylineaddr, rows * wupdate) ;
      rb->ymaskaddr = BLIT_ADDRESS(rb->ymaskaddr, rows * wclipupdate) ;
      sy += rows ;
    }

    /* Bail out if we've read all the data in the form */
    if (state.next_line >= ((uint8 *) theform->addr) + theform->size) {
      HQFAIL("Off the end of a form");
      return;
    }

    repeat = state.next_line[0] ;
    if ( h < (int32)repeat )
      repeat = (uint8)h ;
  }
}

/** Calculates the amount of memory needed to RLE encode the cached bitmap. */
static int32 rlecachesize(FORM *theform, int32 *rletype, uint8 *bufptr,
                          uint32 bufsize)
{
  register blit_t state ;
  register blit_t *startptr , *endptr , *pstartptr ;

  register uint8 *rlelookup = rlelength ;
  register int32 *rleshift = grleshift ;

  int32 same ;
  int32 h , lbytes ;

  int32 repeatcount ;

  blit_t *cacheptr = NULL; /* init to keep compiler quiet */
  uint32 span ;

  HQASSERT(got_basemap_semaphore(rlesema), "Don't have basemap semaphore") ;
  HQASSERT(bufptr, "Memory pointer null in rlecachesize") ;
  HQASSERT(theform, "Form pointer null in rlecachesize") ;

  /* It's a shame that there isn't the information at this point to guess if
     the character is Roman or Kanji; they have very different span width
     characteristics. Kanji strokes are usually about 1/10th the character
     width, Roman strokes are about 1/6th the character width, with wider
     counters in between them. The estimate chosen is a compromise. */
  span = RLE_CHAR_SPAN_GUESS(theFormW(*theform)) ;

  /* Decide how many nibbles to code into. This is done by passing in an
     estimated average span width. If this span width is 30 or less, it will
     probably be cheaper to store it in a single nibble, because the RLE
     can represent up to 30 in two nibbles and still represent runs shorter
     in one nibble. Runs over 30 are most efficiently coded using the number
     of nibbles necessary to store them. */
  num_nibbles = 1 ;
  if ( span > 30 )
    while ( span > max_nibble[num_nibbles] )
      num_nibbles++ ;

  rlebuf = bufptr ;
  rlelimit = bufptr + bufsize ;

  lbytes = theFormL(*theform) ;
  startptr = theFormA(*theform) ;
  endptr = BLIT_ADDRESS(startptr, lbytes) ;

  HQASSERT(startptr, "Bitmap form pointer zero in rlecachesize") ;

  h = theFormH(*theform) ;
  while ( h > 0 ) {
    register uint8 bstate, first = TRUE, current = 0 ;
    register uint32 width = 1 ; /* width of current run */

    RLE_STARTLINE(return -1) ;

    /* Start off with the current line. */
    repeatcount = 1 ;
    state = 0 ;
    bstate = ( uint8 )0 ;

    /* We now gather together white span, black span pairs. The bitmap is
       analysed a word at a time, and subdivided into bytes if it isn't white
       or black, and finally a lookup table is used to determine the
       transition point if the bytes aren't black or white. */
    pstartptr = startptr ;
    while ( startptr < endptr ) {
      register blit_t temp = *startptr++ ;

      /* Invert bits if current span is black. */
      /* Allocate BLIT_WIDTH_BITS-bits at a time. */
      if ( temp == state )
        width += BLIT_WIDTH_BITS ;
      else {    /* Then try them 8 bits at a time. */
        register int32 i ;

        for ( i = 0 ; i < BLIT_WIDTH_BYTES ; ++i ) {
          register uint8 byte = ( uint8 )( temp >> rleshift[ i ] ) ;
          if ( byte == bstate )
            width += 8 ;
          else {        /* Changes state somewhere in here. */
            register int32 inside = 0 ;
            byte = (uint8) (byte ^ bstate) ;
            width += rlelookup[byte] - inside ;
            inside = rlelookup[byte] ;
            while ( inside != 8 ) {
              PUT_SPAN(width, rlebuf, rlelimit, current, first, return -1) ;

              /* Transform state & byte. */
              byte = (uint8)(~byte) ;
              byte &= SHIFTRIGHT( BONES , inside ) ;
              state = ~state ;
              bstate = ( uint8 )state ;
              width = rlelookup[byte] - inside ;
              inside = rlelookup[byte] ;
            }
          }
        }
      }
    }

    /* Check that may need to output stuff. */
    if ( state != 0 ) {
      PUT_SPAN(width, rlebuf, rlelimit, current, first, return -1) ;
    }

    /* Now check if the next line is the same as this one */
    same = TRUE ;
    while ((--h) > 0 ) {
      cacheptr = startptr ;
      endptr = BLIT_ADDRESS(endptr, lbytes) ;
      while ( startptr < endptr )
        if ((*startptr++) != (*pstartptr++)) {
          same = FALSE ;
          break ;
        }
      if ( ! same )
        break ;
      ++repeatcount ;
    }
    startptr = cacheptr ;

    FLUSH_NIBBLES(rlebuf, current, first) ;
    RLE_ENDLINE(repeatcount, return -1) ;
  }

  *rletype = FORMTYPE_CACHERLE1 + num_nibbles - 1 ;

#ifdef METRICS_BUILD
  rlecache_metrics.rletotal += (int32)(rlebuf - bufptr) ;
  rlecache_metrics.rlechars++ ;
  sw_metric_histogram_count(&rlecache_metrics.rleformtypes.info,
                            num_nibbles, 1) ;
#endif

  return CAST_PTRDIFFT_TO_INT32(rlebuf - bufptr) ;
}

static dcoord last_y, vshift ;
static Bool rle_spanerror ;

static SPAN *lastspan ;                 /* Last span pointer */
static SPAN *thisspan ;                 /* Current span pointer */
static SPAN *spanbuffer ;               /* Start of span buffers */
static SPAN *otherspans ;               /* Spans for last line */
static int32 otherlen ;                 /* Spans in last line */

#if defined( ASSERT_BUILD )
static int32 maxspans ;                 /* max number of spans */
#endif

static blit_chain_t rlecache_blits ;
static render_forms_t rlecache_forms ;
static FORM dummyform ; /* To prevent crash in rnbressfill */

static void rle_dospan(render_blit_t *rb,
                       register dcoord y, register dcoord xs, register dcoord xe ) ;

static blit_slice_t rlechar_slice = {
  rle_dospan, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
} ;

/** Sets up rendering direct into an RLE buffer.

   Prepares to generate RLE caches directly from a single fill. Incoming spans
   are buffered in basemap1 until the scanline is complete, and then converted
   to RLE data. The number of threads in the fill is passed to
   setup_rle_render, and is used as a limit on the number of spans to store.
   Spans are sorted and merged as they arrive, so converting them to RLE is
   simple. There are never more merged spans than the number of threads in the
   fill (this is the absolute limit anyway; the number of merged spans is less
   than or equal to the number of threads intersecting a single scanline).

   setup_rle_render *must* be paired with finish_rle_render, regardless of
   errors in between. */
Bool setup_rle_render(render_state_t *rs,
                      int32 nthreads, int32 width, int32 height, uint32 span)
{
  uint8 *baseend ;
  void *map ;
  uint32 size ;

  HQASSERT(!got_basemap_semaphore(rlesema), "Old basemap semaphore is valid") ;

  if ( (rlesema = get_basemap_semaphore(&map, &size)) == 0 )
    return FALSE ;

  rlebase = map ;
  baseend = rlebase + size ;

  thisspan = lastspan = spanbuffer = (SPAN *)baseend - nthreads ;
  otherspans = spanbuffer - nthreads ;

#if defined( ASSERT_BUILD )
  maxspans = nthreads ;
#endif

  rlebuf = rlebase ;
  rlelimit = (uint8 *)otherspans ;

  if ( rlebuf >= rlelimit ) {
    free_basemap_semaphore(rlesema) ;
    return FALSE ;
  }

  otherlen = -1 ;               /* bogus length to prevent first line repeat */
  last_y = 0;
  rle_spanerror = FALSE ;

  theFormW(dummyform) = width ; /* use the dummy form to save w, h & type */
  theFormH(dummyform) = theFormRH(dummyform) = height;
  theFormHOff(dummyform) = 0;

  /* Decide how many nibbles to code into. This is done by passing in an
     estimated average span width. If this span width is 30 or less, it will
     probably be cheaper to store it in a single nibble, because the RLE
     can represent up to 30 in two nibbles and still represent runs shorter
     in one nibble. */
  num_nibbles = 1 ;
  if ( span > 30 )
    while ( span > max_nibble[num_nibbles] )
      num_nibbles++ ;

  theFormT(dummyform) = FORMTYPE_CACHERLE1 + num_nibbles - 1 ;

  render_state_mask(rs, &rlecache_blits, &rlecache_forms, &invalid_surface,
                    &dummyform) ;
  RESET_BLITS(&rlecache_blits, &rlechar_slice, &rlechar_slice, &rlechar_slice) ;
  return TRUE ;
}

static Bool rle_updatey(dcoord scanline)
{
  Bool same = FALSE ;

  if ( rle_spanerror )
    return FALSE ;

  if ( otherlen == lastspan - spanbuffer ) {    /* try for a repeat */
    register SPAN *pspan ;
    for ( thisspan = spanbuffer, pspan = otherspans ;
          thisspan < lastspan ;
          thisspan++, pspan++ ) {
      if ( theISpanL(thisspan) != theISpanL(pspan) ||
           theISpanR(thisspan) != theISpanR(pspan) )
        break ;
    }
    if ( thisspan == lastspan )
      same = TRUE ;
  }

  if ( same ) {
    RLE_REPEAT(return FALSE) ;
  } else {
    dcoord last = -2 ;                   /* last filled coordinate */
    register uint8 first = TRUE, current = 0 ;

    RLE_STARTLINE(return FALSE) ;

    for ( thisspan = spanbuffer ; thisspan < lastspan ; thisspan++ ) {
      register dcoord start = theISpanL(thisspan) ;
      register dcoord end = theISpanR(thisspan) ;

      HQASSERT(start > last, "Spans out of order in rle_updatey") ;
      HQASSERT(end >= start, "Span coordinates out of order in rle_updatey") ;

      PUT_SPAN(start-last-1, rlebuf, rlelimit, current, first, return FALSE) ;
      PUT_SPAN(end-start+1, rlebuf, rlelimit, current, first, return FALSE) ;
      last = end ;
    }

    FLUSH_NIBBLES(rlebuf, current, first) ;
    RLE_ENDONE(return FALSE) ;
  }

  /* If the scanline jumped, fill in the blanks */
  if ( ++last_y < scanline ) {
    RLE_STARTLINE(return FALSE) ;
    RLE_ENDLINE(scanline - last_y, return FALSE) ;
    last_y = scanline ;
    otherlen = 0 ;
  } else {                              /* swap spanbuffer and otherspans */
    SPAN *tmp = otherspans ;

    otherspans = spanbuffer ;
    otherlen = CAST_PTRDIFFT_TO_INT32(lastspan - spanbuffer) ;
    spanbuffer = tmp ;
  }
  thisspan = lastspan = spanbuffer ;

  return TRUE ;
}

/** This is the dospan function called for direct to rle generation. When a
   fill is done, the line ends and horizontals on the current scanline are
   written first, followed by the spans. After the first iteration of the fill
   loop, these will be mostly in order (* see note), but the caps may not be
   in order. The insertion method used here is basically an insertion sort
   with a current position, which makes it perform better when the spans are
   in or nearly in order.
   *The spans arrive in a sort of order: the end of a span is never to the
   left of the start of the previous span. */
static void rle_dospan(render_blit_t *rb,
                       register dcoord y, register dcoord xs, register dcoord xe )
{
  register dcoord sx1 = xs - 1 ;
  register dcoord sx2 = xe + 1 ;

  UNUSED_PARAM(render_blit_t *, rb) ;

  if ( rle_spanerror )
    return ;

  HQASSERT(got_basemap_semaphore(rlesema), "Don't have basemap semaphore") ;
  HQASSERT( y >= last_y, "Scanline out of order in rle_dospan" ) ;

  if ( y != last_y )    /* flush accumulated stuff in rle buffer */
    if ( ! rle_updatey( y ) ) {
      rle_spanerror = TRUE ;
      return ;
    }

  HQASSERT( y == last_y, "last_y not updated properly" ) ;

  while ( thisspan < lastspan ) {
    if ( sx1 > theISpanR(thisspan) ) { /* span is to right of current */
      thisspan++ ;
    } else if ( sx2 < theISpanL(thisspan) ) { /* span is to left, insert */
      if ( thisspan == spanbuffer || sx1 > theISpanR(thisspan - 1) ) {
        register SPAN *pspan = lastspan ;

        HQASSERT(pspan < spanbuffer + maxspans,
                 "Span buffer overflow (insertion)") ;
        while ( pspan > thisspan ) {
          register SPAN *previous = pspan-- ;
          *previous = *pspan ;
        }
        theISpanL(thisspan) = xs ;
        theISpanR(thisspan) = xe ;
        lastspan++ ;
        break ; /* out of loop */
      } else
        thisspan-- ;
    } else {    /* span overlaps current span */
      register SPAN *high = thisspan ; /* last span to merge */

      while ( thisspan > spanbuffer ) { /* search for left merge */
        register SPAN *next = thisspan - 1 ;
        if ( sx1 > theISpanR(next) )
          break ;
        thisspan = next ;
      }
      for ( ;; ) {      /* search for right merge */
        register SPAN *next = high + 1 ;
        if ( next >= lastspan || sx2 < theISpanL(next) )
          break ;
        high = next ;
      }
      if ( xs < theISpanL(thisspan) )   /* enlarge span */
        theISpanL(thisspan) = xs ;
      if ( theISpanR(high) > xe )
        xe = theISpanR(high) ;
      theISpanR(thisspan) = xe ;
      if ( thisspan != high ) { /* shuffle down remaining elements */
        register SPAN *low = thisspan ;

        while ( ++high < lastspan )
          *++low = *high ;
        lastspan = ++low ;
      }
      break ;
    }
  }
  if ( thisspan == lastspan ) { /* extend span array */
    HQASSERT(lastspan < spanbuffer + maxspans,
             "Span buffer overflow (extension)") ;
    theISpanL(thisspan) = xs ;
    theISpanR(thisspan) = xe ;
    lastspan++ ;
  }
}

/** Fill in a form with the generated RLE data */
FORM *finish_rle_render(void)
{
  int32 rlesize ;
  FORM *form ;

  HQASSERT(got_basemap_semaphore(rlesema), "Don't have basemap semaphore") ;

  if ( rle_spanerror || !rle_updatey(theFormH(dummyform)) ) {
    free_basemap_semaphore(rlesema) ;
    return NULL ;
  }

  rlesize = CAST_PTRDIFFT_TO_INT32(rlebuf - rlebase) ;

  {
    int32 action = 0 ;
    do {
      form = (rlesize == 0 ? MAKE_BLANK_FORM()
              : make_RLEForm(theFormW(dummyform), theFormH(dummyform),
                             rlesize, theFormT(dummyform))) ;
      if ( form == NULL ) {
        HQTRACE( debug_lowmemory,
                 ( "CALL(handleLowMemory): tile_finish with action %d", action )) ;
        action = handleLowMemory( action, TRY_NORMAL_METHODS, NULL ) ;
      }
    } while ( action > 0 ) ;
  }

  if ( form != NULL ) {
    HqMemCpy( theFormA(*form) , rlebase , rlesize ) ;

#ifdef RLE_CACHE_STATS
    rletotal += rlesize ;
    rlechars++ ;
    rleformtypes[num_nibbles]++ ;
#endif
  }

  free_basemap_semaphore(rlesema) ;

  return form ;
}

void init_C_globals_rlecache(void)
{
  blit_chain_t blitinit = { 0 } ;
  render_forms_t formsinit = { 0 } ;
  FORM forminit = { 0 } ;

  num_nibbles = 0 ;
  rlebase = rlebuf = rlelimit = repeatptr = lengthptr = NULL ;
  rlesema = 0 ;

#ifdef METRICS_BUILD
  rlecache_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&rlecache_metrics_hook) ;
#endif

  last_y = vshift = 0 ;
  rle_spanerror = FALSE ;

  lastspan = thisspan = spanbuffer = otherspans = NULL ;
  otherlen = 0 ;                 /* Spans in last line */

#if defined( ASSERT_BUILD )
  maxspans = 0 ;                 /* max number of spans */
#endif

  rlecache_blits = blitinit ;
  rlecache_forms = formsinit ;
  dummyform = forminit ;
}

/* Log stripped */
