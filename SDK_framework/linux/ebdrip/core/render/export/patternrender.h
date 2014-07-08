/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:patternrender.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Pattern renderer API
 */

#ifndef __PATTERNRENDER_H__
#define __PATTERNRENDER_H__

#include "render.h" /* render_info_t */
#include "display.h"
#include "bitblts.h" /* blit_chain_t */
#include "objnamer.h"

#define REPLICATING(ri) ((ri)->pattern_state == PATTERN_PAINTING)


/** Instance-specific pattern state.

   This includes all of the bits which are specific to a particular use of a
   pattern, important for the multi-threaded case when a pattern can be
   referenced during rendering of different parts of a page. */
struct pattern_tracker_t {
  pattern_tracker_t *pParentPattern;  /**< Reference to parent pattern state
                                           (pattern in which this was used). */
  pattern_tracker_t *pContextPattern; /**< Relative parent pattern (pattern
                                          in which this was defined). */
  pattern_tracker_t *pBasePattern;    /**< Outermost pattern. */
  PATTERNOBJECT     *pPattern;        /**< Pattern to render. */
  pattern_shape_t   *patternshape ;   /**< Pre-computed pattern shape mask. */
  DLRANGE            dlrange;         /**< DL range of the patterned objects */
  LISTOBJECT        *start_lobj;      /**< Used for colour/spotno in
                                           uncoloured patterns */
  Bool               forcepattern;    /**< end_pattern called between patterned objs */
  TranAttrib        *transparency ;   /**< Transparency of patterned object. */
  render_blit_t      saved_rb ;       /**< Saved forms, positioning info. */
  blit_chain_t       saved_blits ;    /**< Saved bitblit chain. */
  const struct surface_t *saved_surface;    /**< Saved surface. */
  Bool               saved_white_on_white; /**< Saved white-on-white state. */
  dcoord             oFormHOff;       /**< Actual band vertical offset. */
  SYSTEMVALUE        xbase, ybase;    /**< Converged base coords. */
  dcoord             ixbase, iybase;  /**< Integer version of base coords. */
  dcoord             x_sep_position;  /**< Saved base X separation position. */
  dcoord             y_sep_position;  /**< Saved base Y separation position. */
  dbbox_t            replim;          /**< Replication limits. */
  dcoord             tile_offset_x,   /**< Current tile's offset; for high-level tiling */
                     tile_offset_y;
};

#define PATTERNRECURSEDATA_NAME "PatternRecurseData"

/** The blit data for PATTERNRECURSE_BLIT_INDEX blits. */
typedef struct {
  /** The top pattern (or innermost) is used to make the pattern id list
      used for pattern shapes.  It is required at any point in the pattern
      blit stack. */
  pattern_tracker_t *top_pattern ;

  /** The clipform used for the base blit clip.  This needs storing so it
      can be put back for the final blit. */
  FORM *base_clipform ;

  OBJECT_NAME_MEMBER
} pattern_recurse_t ;

/** Start using a pattern. This function is called for the first object using
    a pattern. The corresponding \c end_pattern() function will be called
    when this pattern can no longer be used; normally, multiple objects can
    be accumulated into a pattern shape before applying the pattern.
    Patterned objects which have transparency properties may have to be
    rendered individually, so their transparency can be applied separately.

    \param p_ri The render_info_t in the render loop using the pattern.

    \param pPatternInstance A pattern tracker common to all objects using
    this pattern. Information about this pattern instance is accumulated in
    the pattern tracker for use when the pattern DL is traversed.

    \param dlrange DL iterator pointing at the first display list object
    using this pattern.

    \retval FALSE Failed to start rendering the pattern. The caller should
    report an error.

    \retval TRUE Successfully started capturing data to render the pattern.
*/
Bool begin_pattern(render_info_t *p_ri,
                   pattern_tracker_t *pPatternInstance,
                   DLRANGE *dlrange) ;

/** Complete rendering a pattern. The patterned objects will have been captured
    in the patternshapeform for use as a clip.

    \param p_ri The render_info_t in the render loop using the pattern.

    \param pPatternInstance A pattern tracker common to all objects using
    this pattern. The pattern's display list will be traversed using the
    information accumulated in the pattern tracker.

    \param dlrange The display list iterator after the final patterned object.
    This may be \c NULL if the last object on a display list was patterned.

    \retval FALSE Failed to render the pattern. The caller should report an
    error.

    \retval TRUE Successfully rendered the pattern.
*/
Bool end_pattern(render_info_t *p_ri,
                 pattern_tracker_t *pPatternInstance,
                 DLRANGE *dlrange);

void set_pattern_replication(pattern_tracker_t *pPatternInstance,
                             render_blit_t *rb,
                             pattern_recurse_t *recurse_data) ;

void pattern_finish(render_info_t *ri) ;

Bool pattern_clipping_for_shapes(render_info_t *ri, CLIPOBJECT *newcomplex) ;

void pattern_clipping_for_cells(render_info_t *ri) ;

typedef Bool (*pattern_render_callback)(pattern_tracker_t *pPatternInstance,
                                        render_state_t *rs, dbbox_t *bounds,
                                        dcoord xoffset, dcoord yoffset);

/* Log stripped */

#endif /* protection for multiple inclusion */
