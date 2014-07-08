/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:scband.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Device resolution pixel-touching scan conversion for fills.
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

#include "surface.h"
#include "spanlist.h"
#include "scanconv.h"
#include "scpriv.h"
#include "scmerge.h"

/** Update the clipping form for the rest of a band in which a fill
   terminated. */
void updatebandclip(surface_line_update_fn *update,
                    surface_handle_t handle,
                    render_blit_t *rb,
                    register dcoord y1 , register dcoord y2 )
{
  uint32 wclipupdate ;
  FORM *clipform = rb->clipform ;

  HQASSERT(update != NULL, "Updating clip without update function") ;

  wclipupdate = theFormL(*clipform) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*clipform),
    wclipupdate * (uint32)(y1 - theFormHOff(*clipform) - rb->y_sep_position)) ;

  while ( y1 <= y2 ) {
    (*update)(handle, rb, y1) ;
    ++y1 ;
    rb->ymaskaddr = BLIT_ADDRESS(rb->ymaskaddr, wclipupdate) ;
  }
}

/*---------------------------------------------------------------------------*/

/**
 * Define arbitrary upper limit on the number of spans we are willing to
 * coalesce. Need to keep it small for performance reasons, and will live
 * with issues on the odd extreme case.
 *
 * Unfortunately, some of the XPS CET tests do trip up on the previous value of
 * 25, drawing wavy strokes with multiple terminations on the same line.
 *
 * Merging has been moved into scanconvert_band() as a first stage to making
 * it an atomic operation which can be turned-on on a scanline basis.
 */
#define MAX_MERGE_SPANS 95

/** Span function for coalescing spans before sending down the blit chain. */
static void coalesce_blit(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  spanlist_t *spanlist ;

  GET_BLIT_DATA(rb->blits, COALESCE_BLIT_INDEX, spanlist) ;

  HQASSERT(spanlist, "Not coalescing spans, but blit called") ;
  if ( !spanlist_insert(spanlist, xs, xe) ) {
    if ( !spanlist_merge(spanlist) ) {
      spanlist_iterate(spanlist, &next_span, rb, y) ;
      spanlist_reset(spanlist) ;
    }
  }
}

/** Blit slice for span merging. */
static blit_slice_t coalesce_slice = {
  coalesce_blit, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
} ;

/*---------------------------------------------------------------------------*/

static scanconvert_fn *scanconvert_fns[SC_RULE_N] = {
  scanconvert_compat,    /* SC_RULE_HARLEQUIN */
  scanconvert_adobe,     /* SC_RULE_TOUCHING */
  scanconvert_tesselate, /* SC_RULE_TESSELATE */
  scanconvert_bresin,    /* SC_RULE_BRESENHAM */
  scanconvert_center,    /* SC_RULE_CENTER */
} ;

/** Finally, a function to draw the intersection of a fill and a band. */
void scanconvert_band(render_blit_t *rb, NFILLOBJECT *nfill, int32 therule)
{
  spanlist_t *mergedspans = NULL ;
  span_t cspans[MAX_MERGE_SPANS]; /* Merge over-struck ddafill spans */

  HQASSERT(rb != NULL, "No render state for scan converter") ;
  HQASSERT(nfill != NULL, "No NFILLOBJECT for scan converter") ;
  HQASSERT(nfill->converter < SC_RULE_N, "Scan converter rule out of range") ;
  HQASSERT((therule & (~(ISCLIP|CLIPPED_RHS|MERGE_SPANS|SPARSE_NFILL)))
           == NZFILL_TYPE ||
           (therule & (~(ISCLIP|CLIPPED_RHS|MERGE_SPANS|SPARSE_NFILL)))
           == EOFILL_TYPE,
           "therule should be NZFILL_TYPE or EOFILL_TYPE");
  /* until just now, literal 1's and 0's were used for the rule, and nsidetst.c
   * had it backwards.  So they're now symbolics, and NZFILL_TYPE isn't 1 to
   * allow this assert to find any bits I've missed (see task 4565).
   */

  /*
   * May need to merge spanlists to avoid double-striking pixels.
   * This is done on a per-scanline basis. The coalescing blits and blit
   * data are set up if the merging flag is set, but the blit layer is
   * disabled by removing the blit mask bit. It will then be turned on by
   * enabling the blit mask when the right conditions are detected.
   */
  HQASSERT(!DOING_BLITS(rb->blits, COALESCE_BLIT_INDEX),
           "Already doing coalescing blits") ;

  if ( (therule & MERGE_SPANS) != 0 ) {
    blit_chain_t *blits = rb->blits ;

    mergedspans = spanlist_init(cspans, sizeof(cspans));
    HQASSERT(mergedspans != NULL,
             "Didn't have enough space to create coalescing spanlist") ;
    SET_BLITS(blits, COALESCE_BLIT_INDEX,
              &coalesce_slice, &coalesce_slice, &coalesce_slice) ;
    SET_BLIT_DATA(blits, COALESCE_BLIT_INDEX, mergedspans) ;
    DISABLE_BLIT_SPAN(blits, COALESCE_BLIT_INDEX) ;

    therule &= ~MERGE_SPANS;
  }

  (*scanconvert_fns[nfill->converter])(rb, nfill, therule, mergedspans) ;

#if defined(DEBUG_BUILD)
  /* Don't rely on these values being set after the end of a fill. ymaskaddr
     may not be updated correctly if the fill terminates before the end of
     the clip area, and if the fill is a clip it will be set to the end of
     the band. */
  rb->ylineaddr = NULL ;
  rb->ymaskaddr = NULL ;
#endif

  if ( mergedspans != NULL ) {
    HQASSERT(spanlist_count(mergedspans) == 0, "Merged spans not all flushed") ;
    CLEAR_BLITS(rb->blits, COALESCE_BLIT_INDEX) ;
  }
}

/* Log stripped */
