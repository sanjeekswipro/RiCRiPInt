/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:pcl5Blit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blitters to implement the PCL5 paint model.
 */
#include "core.h"
#include "pcl5Blit.h"

#include "render.h"
#include "pclAttribTypes.h"
#include "pclGstate.h"
#include "pclAttrib.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "toneblt.h"
#include "surface.h"
#include "display.h"

/** Returns true if the span color is white.
 */
static inline Bool isSourceWhite(render_blit_t *rb)
{
  VERIFY_OBJECT(rb->color, BLIT_COLOR_NAME) ;
  HQASSERT(rb->color->ncolors == 1, "Should be one color in PCL5e") ;
  HQASSERT(rb->color->nchannels == 1, "Should be one channel in PCL5e") ;
  return blit_quantise_state(rb->color) == blit_quantise_max ;
}

/** Inline function used by specialised ROPping functions. */
static inline void blitMonoRop(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe,
                               uint8 ropmask)
{
  const PclAttrib *attrib ;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, attrib) ;

  /* The output of the ROP is also in additive space. */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode,
                 attrib->rop & ropmask
                 ? &blitslice0[rb->clipmode] : &blitslice1[rb->clipmode]);

  DO_SPAN(rb, y, xs, xe);
}

static void rop_p0s0d0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 128) ;
}

static void rop_p0s0d1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 64) ;
}

static void rop_p0s1d0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 32) ;
}

static void rop_p0s1d1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 16) ;
}

static void rop_p1s0d0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 8) ;
}

static void rop_p1s0d1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 4) ;
}

static void rop_p1s1d0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 2) ;
}

static void rop_p1s1d1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  blitMonoRop(rb, y, xs, xe, 1) ;
}

/** Table of all combinations of source, pattern, and destination bits. These
    are used to call a specialised function that extracts the color from the
    ROP code, and calls the base blit to splat it to the destination. */
static const BITBLT_FUNCTION pattern_source_destination[2][2][2] = {
  { /* Pattern 0 */
    { rop_p0s0d0, rop_p0s0d1 }, { rop_p0s1d0, rop_p0s1d1 },
  },
  { /* Pattern 1 */
    { rop_p1s0d0, rop_p1s0d1 }, { rop_p1s1d0, rop_p1s1d1 },
  }
} ;

/**
 * Apply a ROP to the passed span. The source span is broken into sub-spans
 * as required by the rendered output already present in the band.
 */
static void applyMonoRop(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe,
                         Bool needsDest, uint32 source, uint32 pattern)
{
  HQASSERT(source == 0 || source == 1, "Source not 0/1") ;
  HQASSERT(pattern == 0 || pattern == 1, "Pattern not 0/1") ;

  if ( needsDest ) {
    bitmap_intersecting(rb->ylineaddr,
                        pattern_source_destination[pattern][source][1],
                        pattern_source_destination[pattern][source][0],
                        rb, y, xs, xe, rb->x_sep_position) ;
  } else {
    /* ROP does not require destination color, arbitrarily set it to 0. */
    (*pattern_source_destination[pattern][source][0])(rb, y, xs, xe);
  }
}

/**
 * PCL5 monochrome blit function. Implements PCL5 patterns and transparency.
 */
static void bitfillpcl5_mono(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  const PclAttrib* attrib ;
  const void* cacheEntry ;
  Bool needsDest ;
  uint32 source = isSourceWhite(rb) ? 0 : 1;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, attrib) ;
  cacheEntry = attrib->cacheEntry.pointer.pointer ;

  /** \todo should convert dl color to none at a higher level */
  if ( cacheEntry == &invalidPattern )
    return;

  needsDest = pclROPRequiresDestination(attrib->rop) ;

  if ( cacheEntry == &erasePattern ) {
    /* Erase is implemented as an opaque white pattern.
       Transparency settings are ignored.*/
    applyMonoRop(rb, y, xs, xe, needsDest, source, 0);
    return;
  }

  if ( source == 0 && attrib->sourceTransparent )
    return;

  if ( !attrib->dlPattern ) {
    /* No pattern; assume opaque black. */
    applyMonoRop(rb, y, xs, xe, needsDest, source, 1);
    return;
  }

  /* Lookup the pattern pixel, coalescing into as large a span as possible. */
  {
    PclDLPatternIterator iterator ;
    dcoord w = xe - xs + 1 ;

    pclDLPatternIteratorStart(&iterator, attrib->dlPattern, xs, y, w) ;
    for (;;) {
      if ( !(attrib->patternTransparent && !iterator.color.packed && source != 0) ) {
        applyMonoRop(rb, y, xs, xs + iterator.cspan - 1, needsDest, source,
                     iterator.color.packed);
      }

      xs += iterator.cspan ;
      w -= iterator.cspan;
      if ( w <= 0 )
        break ;

      pclDLPatternIteratorNext(&iterator, w) ;
    }
  }
}

static void bitfillpcl5_mono_src0(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  const PclAttrib* attrib ;
  uint32 pattern = 1;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, attrib) ;

  if ( attrib->cacheEntry.pointer.pointer == &erasePattern )
    pattern = 0;

  (*pattern_source_destination[pattern][0 /*source*/][0 /*arbitrary dest*/])(rb, y, xs, xe) ;
}

static void bitfillpcl5_mono_src1(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  const PclAttrib* attrib ;
  uint32 pattern = 1;

  GET_BLIT_DATA(rb->blits, ROP_BLIT_INDEX, attrib) ;

  if ( attrib->cacheEntry.pointer.pointer == &erasePattern )
    pattern = 0;

  (*pattern_source_destination[pattern][1 /*source*/][0 /*arbitrary dest*/])(rb, y, xs, xe) ;
}

/**
 * pcl5_ropmode determines which set of blitters to use.  If only source is used
 * to render the object then the simplified source-only blitters can be selected
 * which are faster than the general blitter.
 */
void pcl5_ropmode(render_blit_t *rb, uint8 *ropmode)
{
  const render_info_t *ri = rb->p_ri;
  const PclAttrib* attrib = ri->lobj->objectstate->pclAttrib;
  const void* cacheEntry = attrib->cacheEntry.pointer.pointer;
  Bool sourceWhite;

  *ropmode = BLT_ROP_GEN;

  /** \todo should convert dl color to none at a higher level */
  if ( cacheEntry == &invalidPattern )
    return;

  /* Cannot use simplified (and faster) PCL mono blitters for images because
     image rendering changes source; also reject if need to read the destination. */
  if ( ri->lobj->opcode == RENDER_image ||
       pclROPRequiresDestination(attrib->rop) )
    return;

  sourceWhite = isSourceWhite(rb) ;

  /* Erase is implemented as an opaque white pattern.
     Transparency settings are ignored. */
  if ( cacheEntry == &erasePattern ) {
    *ropmode = sourceWhite ? BLT_ROP_SRC0 : BLT_ROP_SRC1;
    return;
  }

  if ( sourceWhite && attrib->sourceTransparent )
    return;

  if ( attrib->dlPattern )
    /* Has a pattern so cannot use simplified blitter. */
    return;

  *ropmode = sourceWhite ? BLT_ROP_SRC0 : BLT_ROP_SRC1;
}

static void init_halftone1_rops_mode(surface_t *halftone1, uint8 ropmode,
                                     BITBLT_FUNCTION fillblit)
{
  halftone1->ropblits[ropmode].spanfn = fillblit ;
  halftone1->ropblits[ropmode].blockfn = blkfillspan ;
  halftone1->ropblits[ropmode].charfn = charbltn ;
  halftone1->ropblits[ropmode].imagefn = imagebltn ;
}

void init_halftone1_rops(surface_t *halftone1)
{
  init_halftone1_rops_mode(halftone1, BLT_ROP_GEN, bitfillpcl5_mono);
  init_halftone1_rops_mode(halftone1, BLT_ROP_SRC0, bitfillpcl5_mono_src0);
  init_halftone1_rops_mode(halftone1, BLT_ROP_SRC1, bitfillpcl5_mono_src1);
}

/* See header for doc. */
Bool pcl5_mono_blitter_required(render_info_t* p_ri)
{
  const LISTOBJECT* object = p_ri->lobj;
  const PclAttrib* attrib = object->objectstate->pclAttrib;
  /* If the blit color comes from the DL color, can a normal opaque render
     be expected to get it right? */
  Bool blit_dlc = ((attrib->rop == PCL_ROP_T ||
                    attrib->rop == PCL_ROP_BLACK ||
                    attrib->rop == PCL_ROP_WHITE) &&
                   attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND);
  /* If the blit color comes from the object, can a normal opaque render be
     expected to get it right? */
  Bool blit_obj = (attrib->rop == PCL_ROP_S &&
                   attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB) ;

  if ( attrib->sourceTransparent ||
       attrib->patternTransparent ||
       attrib->patternColors != PCL_PATTERN_NONE ||
       (!blit_obj && !blit_dlc) ||
       (attrib->rop != PCL_ROP_D && pclROPRequiresDestination(attrib->rop)) )
    return TRUE;

  return FALSE;
}

/* Log stripped */

