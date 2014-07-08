/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:surfacet.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Types for surface destination API.
 */

#ifndef __SURFACET_H__
#define __SURFACET_H__

/** \ingroup rendering */
/** \{ */

#include "rendert.h" /* render_state_t, etc */

/** Uses for surface destinations. Do not rely on the order of these entries
    for initialisation; explicitly initialise using these as indices. */
enum {
  SURFACE_TRANSPARENCY, /**< Surface represents transparency group */
  SURFACE_MHT_CONTONE_FF,  /**< Surface will be used as an 8-bit modular halftone source */
  SURFACE_MHT_CONTONE_FF00,  /**< Surface will be used as a pseudo 16-bit modular halftone source */
  SURFACE_MHT_MASK,     /**< Surface will be used as a modular halftone mask */
  SURFACE_OUTPUT,       /**< Surface represents final output */
  SURFACE_CLIP,         /**< Surface will be used as a clip */
  SURFACE_TRAP_PREPARE, /**< Surface will be used for trap preparation */
  SURFACE_TRAP_GENERATE, /**< Surface will be used for trap generation */
#if 0 /** \todo ajcd 2009-09-29: These not yet implemented. */
  SURFACE_SOFTMASK,     /**< Surface will be used as a softmask */
  SURFACE_SHAPE,        /**< Surface is a non-aligned mask */
  SURFACE_PATTERNCELL,  /**< Surface will be used as a pattern cell */
  SURFACE_REGIONMAP,    /**< Surface will be used as a region map */
#endif
  N_SURFACE_TYPES
} ;
typedef int surface_type_t ;

/** \brief Incomplete type definition for surface instance data handle.

    Surface-specific structure for private data associated with each surface
    activation. A single surface activation may be shared between multiple
    pages. */
typedef struct surface_instance_t surface_instance_t ;

/** \brief Incomplete type definition for surface page data handle.

    Surface definitions can supply their own definition of this struct if
    they wish to store data between surface \c introduce_dl and \c retire_dl
    calls. A single \c surface_page_t structure may persist for all display
    lists used to represent a page when partial painting. */
typedef struct surface_page_t surface_page_t ;

/** \brief Incomplete type definition for surface render pass data handle.

    Surface definitions can supply their own definition of this struct if
    they wish to store data between surface \c render_begin and \c render_end
    calls. */
typedef struct surface_render_t surface_render_t ;

/** \brief Incomplete type definition for surface sheet data handle.

    Surface definitions can supply their own definition of this struct if
    they wish to store data between surface \c sheet_begin and \c sheet_end
    calls. */
typedef struct surface_sheet_t surface_sheet_t ;

/** \brief Incomplete type definition for surface thread data handle.

    Surface definitions can supply their own definition of this struct if
    they wish to store data in the scope of the surface thread localiser
    function.

    The thread localiser happens to be at the same scope as frames, so can
    also be used for frame local data. */
typedef struct surface_frame_t surface_frame_t ;

/** \brief Incomplete type definition for surface band data handle.

    Surface definitions can supply their own definition of this struct if
    they wish to store data in the scope of the surface band localiser
    function. */
typedef struct surface_band_t surface_band_t ;

/** \brief Incomplete type definition for surface set page handle.

    The RIP provides one \c surface_handle_t storage location in the render
    state. The surface can optionally store a reference to its own data at
    the page scope, sheet scope, frame scope, and/or band scope. Since
    only one location is provided for the surface handle, the surface must
    replace the handle pointer at the start of a sheet, frame, or
    band, if it wishes the functions called within that scope to use a local
    data pointer. The RIP will restore the parent scope's handle on exit from
    a child scope.

    To pass a surface handle into blit functions, the surface \c prepare
    function should set it in the appropriate blit layer data pointer. */
typedef union surface_handle_t {
  surface_instance_t *instance ;
  surface_page_t *page ;
  surface_render_t *render ;
  surface_sheet_t *sheet ;
  surface_frame_t *frame ;
  surface_band_t *band ;
} surface_handle_t ;

/** \brief Possible return values for surface prepare function. */
enum {
  SURFACE_PREPARE_FAIL, /**< Error in surface prepare function. */
  SURFACE_PREPARE_OK,   /**< Surface prepared for rendering object. */
  SURFACE_PREPARE_SKIP  /**< Skip rendering this object. */
} ;
typedef int surface_prepare_t ;

/** \brief Actions to control render retries.

    This enumeration may be extended in future to allow arbitrary bounding
    boxes to be re-rendered, giving the surface enough control to render
    bands in multiple passes. */
enum {
  SURFACE_PASS_DONE = 0, /**< Rendering is done, proceed to output. */
  SURFACE_PASS_YSPLIT,   /**< Split into sub-bands. */
  SURFACE_PASS_XSPLIT    /**< Split into side-by-side tiles. */
} ;

/** \brief Surface control over rendering passes.

    A surface can implement rendering using multiple passes over a band. This
    structure is used to allow the surface to control the render passes. For
    the first pass on a particular band all of the fields are zero. On exit
    from the surface's band localiser trampoline function, if \c
    surface_bandpass_t::action in not \c SURFACE_PASS_DONE, the renderer will
    repeat some or all of the band.

    If \c surface_bandpass_t::action is \c SURFACE_PASS_YSPLIT on exit,
    the renderer will split the band into two smaller bands, create a new
    task group to handle the remainder of the band, and the initial set of
    lines will be retried using the current task, but not necessarily with
    the current output band (so data cannot be stored in the current band
    when sub-dividing it). The output value of the
    \c surface_bandpass_t::next_pass field will be passed through to the
    remainder of the band in its band localiser call's
    \c surface_bandpass_t::current_pass field. The
    \c surface_bandpass_t::next_pass field may be zero in this case.

    If \c surface_bandpass_t::action is \c SURFACE_PASS_XSPLIT on exit, the
    renderer will split the band into two smaller side-by-side tiles, create
    a new task group to handle the remainder of the band, and the initial
    tile will be retried using the current task, but not necessarily with the
    current output band (so data cannot be stored in the current band when
    sub-dividing it). The output value of the
    \c surface_bandpass_t::next_pass field will be passed through to the
    remainder of the band in its band localiser call's
    \c surface_bandpass_t::current_pass field. The
    \c surface_bandpass_t::next_pass field may be zero in this case.

    When creating a new task group for the remainder of a band, if the
    \c surface_bandpass_t::serialize field is set to \c TRUE, then the render
    task of the new band group will depend on the current pass or band
    section, and will not be called in parallel. If \c FALSE, then the
    render tasks for the sub-divisions of the band may happen in parallel.
*/
typedef struct surface_bandpass_t {
  int action ;             /**< One of the SURFACE_PASS_* enumeration values. */
  intptr_t current_pass ;  /**< Passed to the retry render. */
  intptr_t next_pass ;     /**< Passed to remainder or retry render. */
  Bool serialize ;         /**< Retry and remainder serialized. */
} surface_bandpass_t ;

/** \brief Static/auto initialiser for surface_bandpass_t. */
#define SURFACE_BANDPASS_INIT { SURFACE_PASS_DONE, 0, 0, FALSE }

/** \brief The band localiser gives the surface set the opportunity to keep
    information live on the C stack during rendering of an output band.

    The surface handle created by \c page_begin, \c sheet_begin or \c
    frame_begin may be modified by the band localiser; all band scoped
    functions will be passed the handle as modified by the localiser. (The
    band localiser could copy the page/sheet/thread handle to a local stack
    frame, for example.)

    \param[in,out] handle  A location where a reference to the page,
                           sheet, or thread-specific data can found, and
                           band-specific data can be stored.
    \param[in] rs          The render state that will be used for rendering
                           this band. The band localiser may not modify this
                           render state, or override it.
    \param[in] callback    A callback function that encapsulates all of the
                           rendering for this band will perform. The callback
                           function \e must be called exactly once if the
                           function succeeds.
    \param[in] data        An opaque data pointer that \e must be passed to
                           \c callback.
    \param[in,out] bandpass A structure used to control multiple passes for
                           surface band rendering. On entry, all fields
                           except \c surface_bandpass_t::current_pass are
                           zero. On exit, the \c
                           surface_bandpass_t::redo_lines field must be in
                           the range zero to the number of lines in the band.
                           If non-zero, one or more repeat passes over the
                           band will be made.

    \retval TRUE if the function succeeded.
    \retval FALSE if the function failed. The callback function may not have
                  been called in this case. The band localiser will not
                  be repeated regardless of the output value of \c
                  surface_bandpass_t::redo_lines.
*/
typedef Bool (surface_band_localiser_fn)(surface_handle_t *handle,
                                         const render_state_t *rs,
                                         render_band_callback_fn *callback,
                                         render_band_callback_t *data,
                                         surface_bandpass_t *bandpass) ;

/** \brief Opaque definition for parameters passed back to
    \c surface_clip_callback_fn. */
typedef struct surface_clip_callback_t surface_clip_callback_t ;

/** \brief Clip generation callback function.

    This function is used when localising clip rendering, permitting the clip
    surface to keep local data on the C stack.

    This routine is called within the band scope.

    \param[in] data An opaque data pointer containing data supplied by the
    caller.
*/
typedef void (surface_clip_callback_fn)(surface_clip_callback_t *data) ;

/** \brief Line update function.

    This function is normally used by clip surfaces. When rendering fills, it
    is called once for each line in a band, after the spans for the line have
    been passed down the blit chain.

    This routine is called within the band scope.

    \param[in] handle The surface set handle, as localised by the band
                      localiser.
    \param rb         The blit rendering render state used to blit spans.
    \param line       The Y coordinate of the completely line.
*/
typedef void (surface_line_update_fn)(surface_handle_t handle,
                                      render_blit_t *rb,
                                      dcoord line);

/** \brief Type for surface backdrop references.

    This type should be treated as an opaque type by surfaces. It provides
    a unique reference to a group, which may be used to associate a backdrop
    with the group. */
typedef struct Backdrop *surface_backdrop_t ;

/** \brief Indices for maxblit optimisations.

    These indices are used to extract optimised maxblit functions for min,
    max, and no maxblits. No maxblits doesn't have its own blit functions,
    so shares the iteration limit with the none value. */
enum {
  BLT_MAX_MIN,  /**< Minblit. */
  BLT_MAX_MAX,  /**< Maxblit. */
  BLT_MAX_NONE, /**< Not max/minblitting. */
  BLT_MAX_N = BLT_MAX_NONE /**< Share max with none because we don't store non-maxblit blits. */
} ;

/** \brief Indices for halftone1 ropping optimisations.

    These indices are used to extract ropping functions for the general ropping
    case and source only optimised cases (source value of zero and one). */
enum {
  BLT_ROP_GEN,  /**< Handles all cases of ropping. */
  BLT_ROP_SRC0, /**< Handles source only, where source is zero. */
  BLT_ROP_SRC1, /**< Handles source only, where source is one. */
  BLT_ROP_N
} ;

/** \brief Indices for object map optimisations.

    These indices are used to extract blit functions for object map on the
    side blits, for blits that replace or combine the object map value. */
enum {
  BLT_OM_REPLACE, /**< Replace object map with new value. */
  BLT_OM_COMBINE, /**< Combine object map with new value. */
  BLT_OM_N
} ;

/** \brief Indices for pattern recursion optimisations.

    These indices are used to extract optimised blit functions for pattern
    recursion. */
enum {
  BLT_RECURSE_BASE, /**< Final level of pattern recursion. */
  BLT_RECURSE_MORE, /**< Not final level of pattern recursion. */
  BLT_RECURSE_N
} ;

/** \brief Indices for pattern cell optimisations.

    These indices are used to extract optimised blit functions for pattern
    cells which are orthogonal or non-orthogonal blits from appropriate
    tables. */
enum {
  BLT_CELL_ORTHOGONAL, /**< Pattern cell orthogonal. */
  BLT_CELL_NONORTH,    /**< Pattern cell non-orthogonal. */
  BLT_CELL_N
} ;

/** \brief Indices for pattern tiling optimisations.

    These indices are used to extract optimised blit functions for pattern
    tilings from appropriate tables. */
enum {
  BLT_TILE_NONE,      /**< Pattern cell untiled. */
  BLT_TILE_CONSTANT,  /**< Pattern cell tiling constant spacing. */
  BLT_TILE_ACCURATE,  /**< Pattern cell tiling without distortion. */
  BLT_TILE_FAST,      /**< Pattern cell tiling constant, distortion allowed. */
  BLT_TILE_N
} ;

/** \brief Indices for gouraud interpolation optimisations.

    These indices are used to extract optimised blit functions for gouraud
    interpolation without and with noise or anti-aliasing. */
enum {
  BLT_GOUR_SMOOTH, /**< Gouraud blits without noise. */
  BLT_GOUR_NOISE,  /**< Gouraud blits with noise/anti-alias. */
  BLT_GOUR_N
} ;

/** \brief Forward type definition for generic surface description. */
typedef struct surface_t surface_t ;

/** \brief Forward type definition for surface set description. */
typedef struct surface_set_t surface_set_t ;

/** \brief Forward type definition for clip-specialised surface description. */
typedef struct clip_surface_t clip_surface_t ;

/** \brief Forward type definition for transparency-specialised surface
    description. */
typedef struct transparency_surface_t transparency_surface_t ;

/** \} */

#endif /* __SURFACET_H__ */

/* Log stripped */
