/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:scpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions and structures for scan conversion. This is a part of the
 * CORErender interface.
 */

#ifndef __SCPRIV_H__
#define __SCPRIV_H__

#include "spanlist.h" /* spanlist_t, for pixel coalescing */
#include "surface.h" /* surface_update_line_fn */
#include "rendert.h" /* render_blit_t */

struct blit_chain_t ; /* from SWv20, should probably be this compound */
struct NFILLOBJECT ; /* from SWv20 */

/*---------------------------------------------------------------------------*/
/** \brief Update clip for remained of band. */
void updatebandclip(surface_line_update_fn *update,
                    surface_handle_t handle,
                    render_blit_t *rb,
                    register dcoord y1, register dcoord y2) ;

/** \brief Type definition for individual scan converter functions; these
    include a merge spanlist. */
typedef void (scanconvert_fn)(render_blit_t *rb,
                              struct NFILLOBJECT *nfill,
                              int32 therule,
                              spanlist_t *mergedspans);

/** \brief Identity scale pixel rendering with backwards-compatible Bresenham
    pixel rule and clipping map update. */
scanconvert_fn scanconvert_compat ;

/** \brief Identity scale pixel rendering with Adobe pixel touched rule and
    clipping map update. */
scanconvert_fn scanconvert_adobe ;

/** \brief Identity scale pixel rendering with inward-rounding Bresenham
    pixel rule and clipping map update. */
scanconvert_fn scanconvert_bresin ;

/** \brief Identity scale pixel rendering with pixel centre inclusion rule
    and clipping map update. */
scanconvert_fn scanconvert_center ;

/** \brief Identity scale pixel rendering with half-open (non-overlapping)
    rule and clipping map update. This will be automatically called by
    scanconvert_band() if nfill->converter is SC_RULE_TESSELATE. */
scanconvert_fn scanconvert_tesselate ;

/*---------------------------------------------------------------------------*/
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
/** Debugging flags */
enum {
  DEBUG_SCAN_INFO = 2,          /**< Print span info during conversion. */
  DEBUG_SCAN_NO_HRENDER = 4,    /**< No horizontal runs in characters. */
  DEBUG_SCAN_NO_HDROPOUT = 8,   /**< No horizontal dropouts in characters. */
  DEBUG_SCAN_DO_VRENDER = 16,   /**< Vertical runs in characters. */
  DEBUG_SCAN_NO_VDROPOUT = 32   /**< No vertical dropouts in characters. */
} ;

extern uint32 debug_scanconv ;

#endif

#endif /* __SCPRIV_H__ */

/* Log stripped */
