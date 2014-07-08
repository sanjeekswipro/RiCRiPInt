/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:scmerge.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Common functions for coalescing and clip update for band pixel touching
 * rules.
 *
 * These functions are implemented in a header file so they can be compiled
 * as static inlines.
 */
#ifndef __SCMERGE_H__
#define __SCMERGE_H__

#define ENABLE_BLIT_SPAN(_blits,_bi) MACRO_START                        \
  blit_chain_t *_blits_ = (_blits) ;                                    \
  _blits_->blit_mask |= (1 << (_bi)) ;                                  \
  _blits_->blit_span = highest_bit_set_in_bits[_blits_->blit_mask] ;    \
MACRO_END

#define DISABLE_BLIT_SPAN(_blits,_bi) MACRO_START                       \
  blit_chain_t *_blits_ = (_blits) ;                                    \
  _blits_->blit_mask &= ~(1 << (_bi)) ;                                 \
  _blits_->blit_span = highest_bit_set_in_bits[_blits_->blit_mask] ;    \
MACRO_END

/** Flush merged spans at the end of a scanline. */
static inline void span_merge_flush(render_blit_t *rb, spanlist_t *mergedspans,
                                    dcoord line)
{
  /* Is spanlist merging enabled ? */
  if ( DOING_BLITS(rb->blits, COALESCE_BLIT_INDEX) ) {
    /*
     * Yes, so call down the blit chain to the children, by iterating the
     * merged list using the next_span() function. Disable the coalescing
     * first, so we don't just stick the iterated spans back where they
     * came from :-)
     */
    HQASSERT(mergedspans != NULL,
             "Span merging turned on without span buffer") ;
    DISABLE_BLIT_SPAN(rb->blits, COALESCE_BLIT_INDEX) ;
    spanlist_iterate(mergedspans, &next_span, rb, line) ;
    spanlist_reset(mergedspans) ;
  }
}

/**
 * Turn spanlist merging on for this scanline if its enabled, and has
 * not yet been turned on.
 */
static inline void span_merge_on(render_blit_t *rb, spanlist_t *mergedspans)
{
  if ( mergedspans != NULL ) {
    ENABLE_BLIT_SPAN(rb->blits, COALESCE_BLIT_INDEX) ;
#if 0
    HQASSERT(rb->blits->layer[COALESCE_BLIT_INDEX].functions[BLT_CLP_NONE] == &coalesce_slice &&
             rb->blits->layer[COALESCE_BLIT_INDEX].functions[BLT_CLP_RECT] == &coalesce_slice &&
             rb->blits->layer[COALESCE_BLIT_INDEX].functions[BLT_CLP_COMPLEX] == &coalesce_slice,
             "Coalescing blit function not set up correctly") ;
#endif
    HQASSERT(rb->blits->layer[COALESCE_BLIT_INDEX].data == mergedspans,
             "Coalescing blit data not set up correctly") ;
  }
}

#endif /* __SCMERGE_H__ */

/* Log stripped */
