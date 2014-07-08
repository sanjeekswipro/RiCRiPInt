/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!export:clipops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Clip operation interfaces.
 */

#ifndef __CLIPOPS_H__
#define __CLIPOPS_H__


#include "graphict.h"           /* GSTATE */

struct DL_STATE ;

/** \brief Re-compute the clip bounds of existing clipping records.

    \param[in] gs  The graphics state in which to re-compute bounds.

    This function is called when imposition changes may result in page matrix
    changes that translate clipping paths, and also to re-initialised the
    device clip bounds from the imposition clipping chain before adding a
    device clip record. */
void imposition_update_rectangular_clipping(
  /*@notnull@*/ /*@in@*/        GSTATE *gs ) ;

/** \brief Construct a new clip record for the device boundary, and add it to
    the empty clip record chain.

    \param[in] gs The graphics state to initialise the device clip record
                  for.

    \retval TRUE  If the device clip record was added successfully.
    \retval FALSE If the device clip record was not added successfully.

    This function is called when clipping is re-initialised for a pagedevice,
    either by initclip, initgraphics, or by turning on imposition. */
Bool clip_device_new(/*@notnull@*/ /*@in@*/ GSTATE *gs) ;

/** \brief Ensure that a clip chain has an appropriate device clip record for
    a target page device.

    \param[in,out] clippath  The clip chain to check for a device clip record.
    \param[in] gs The graphics state containing the target page device.

    \retval TRUE  If the device clip record was already present, or was
                  inserted successfully.
    \retval FALSE If the device clip record was not added successfully.

    This function is called when restoring clip paths that may not have
    originated from the same page device. If the page device is different, a
    device clip record appropriate to the gstate's page device must be
    inserted at the start of the clip chain.

    This function will claim and release reference counts on the clip records
    in the \a clippath chain if it copies clip records. */
Bool clip_device_correct(/*@notnull@*/ /*@in@*/ /*@out@*/ CLIPPATH *clippath,
                         /*@notnull@*/ /*@in@*/ GSTATE *gs) ;

/** \brief Reset the clipping path to the device boundary.

    \param[in] gs  A graphics state in which to reset the clipping path.

    \retval TRUE  The clipping path was reset successfully.
    \retval FALSE The clipping path was not reset successfully.
*/
Bool gs_initclip(/*@notnull@*/ /*@in@*/ GSTATE *gs) ;

/** \brief Add a path clip to the current gstate clip chain.

    \param cliptype  The winding rule and invert flags for the new clip. This
                     is one of NZFILL_TYPE or EOFILL_TYPE, possibly or-ed with
                     CLIPINVERT.
    \param[in] lpath The new path to add to the clip chain.
    \param copyclip  Indicates if the path should be copied (if TRUE) or
                     stolen from the caller (if FALSE).

    \retval TRUE  If the path was added to the clip successfully (or discarded
                  by HDLT successfully).
    \retval FALSE If the path was not successfully added or discarded.
*/
Bool gs_addclip(
                                int32 cliptype ,
  /*@notnull@*/ /*@in@*/        PATHINFO *lpath ,
                                Bool copyclip) ;

/* \brief Return the number of disjoint sub-path rectangles in a path.

   \param[in] thepath  Path to check for rectangles.
   \param nzfill       If nzfill is true, and all the rectangles have the same
                       orientation, then we ignore the disjoint requirements.
   \param[out] rectfill  The extent of the rectangles found.

   \returns The number of disjoint sub-path rectangles there are in the
   specified path, or 0 if any sub-path is not a rectangle or not disjoint. */
int32 path_rectangles(
  /*@notnull@*/ /*@in@*/        PATHLIST *thepath,
                                Bool nzfill,
  /*@notnull@*/ /*@out@*/       dbbox_t *rectfill);

/** \brief Determine if the rectangle passed in is at least as large
    as the (imposed) page, and would erase the whole page.

    \param[in] page       The current display list.
    \param[in] rectfill   The bounding box of the rectangle to be filled.
    \param[in] colorType  The current color type. Rectangles painted in a
                          pattern are never treated as a pseudo-erase.

    \retval TRUE  If the rectangle would obscure the whole page.
    \retval FALSE If the rectangle would not obscure the whole page.
 */
Bool is_pagesize(
  /*@notnull@*/ /*@in@*/        struct DL_STATE *page,
  /*@notnull@*/ /*@in@*/        dbbox_t *rectfill,
                                int32 colorType) ;

/** \brief Return the top complex clip record on the clip chain.

    \param[in] gs  The graphics state to examine for complex clipping.
    \param useimpositionclipping  Indicates whether to examine the imposition
                   clipping chain in addition to the page's clip chain.

    \returns The top complex clip record on the clip chain, or NULL if all
             clipping is rectangular.
*/
/*@null@*/ /*@dependent@*/
CLIPRECORD *clippingiscomplex(
  /*@notnull@*/ /*@in@*/        GSTATE *gs ,
                                Bool useimpositionclipping ) ;

/** \brief Predicate function to determine if the current clipping path is
    degenerate (has no interior area).

    \param[in] gs  The graphics state to examine for degenerate clipping.

    \retval TRUE  If the clipping path in \a gs is degenerate.
    \retval FALSE If the clipping path in \a gs is not necessarily degenerate.

    This function only examines the intersections of the clip record paths.
    It can not determine if the rendered intersection of the paths would not
    have any pixels marked. */
Bool clippingisdegenerate(/*@notnull@*/ /*@in@*/ GSTATE *gs) ;

/** \brief The imposition clipping chain.

    When imposition is turned on, the clipping chain that exists at the end
    of the BeginPage procedure is removed from the graphics state and stored
    in the imposition clipping chain. It is still used to clip objects added
    to the imposed page's display list, but is not affected by initclip,
    initgraphics, cliprestore, or other clipping operators. */
extern CLIPPATH impositionclipping ;

/** \brief Unique ID counter for clipping records. */
extern int32 clipid ;

/** \brief Initialise clip focus. */
void init_clip_debug(void) ;

#endif /* protection for multiple inclusion */

/*
Log stripped */
