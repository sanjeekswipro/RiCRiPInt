/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:surface.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions and structures for surface destination API.
 */

#ifndef __SURFACE_H__
#define __SURFACE_H__

/** \ingroup rendering */
/** \{ */

#include "surfacet.h" /* blit_t, etc */
#include "bitbltt.h" /* blit_t, etc */
#include "swdataapi.h" /* swdatum */
#include "rendert.h"
#include "objnamer.h"

struct OBJECT ;
struct DL_STATE ;
struct dl_color_t ;
struct TranAttrib ;
struct PclAttrib ;
struct BackdropShared ; /* from COREbackdrop */
struct CompositeContext ; /* from COREbackdrop */
struct blit_colormap_t ;
struct core_init_fns ;

/** The description of the output surface. The surface structures are \e
    immutable. They must \e not be changed while the RIP is interpreting or
    rendering. There should \e not be any data specific to an instantiation of
    a surface stored in the surface structure. */
struct surface_t {
  /* The blit "matrix" definitions */
  blitclip_slice_t baseblits ;        /**< BASE_BLIT_INDEX functions. */
  blitclip_slice_t omblits[BLT_OM_N] ; /**< OM_BLIT_INDEX functions. */
  blitclip_slice_t maxblits[BLT_MAX_N] ; /**< MAXBLT_BLIT_INDEX functions. */
  blit_slice_t ropblits[BLT_ROP_N] ; /**< ROP_BLIT_INDEX functions. */
  blitclip_slice_t pclpatternblits ; /**< PCL_PATTERN_BLIT_INDEX functions. */
  blit_slice_t intersectblits ;       /**< INTERSECT_BLIT_INDEX functions. */
  blit_slice_t patternrecurseblits[BLT_RECURSE_N] ; /**< PATTERNRECURSE_BLIT_INDEX functions. */
  blit_slice_t patternreplicateblits[BLT_CELL_N][BLT_TILE_N] ; /**< PATTERN_BLIT_INDEX functions. */
  blit_slice_t patterntranslateblits ; /**< PATTERN_BLIT_INDEX functions for high-level tiling. */
  blitclip_slice_t patternclipblits ; /**< PATTERNCLIP_BLIT_INDEX functions. */
  blit_slice_t gouraudbaseblits ;     /**< GOURAUD_BASE_BLIT_INDEX functions. */
  blit_slice_t gouraudinterpolateblits[BLT_GOUR_N] ; /**< GOURAUD_NOISE_BLIT_INDEX functions. */
  AREAFILL_FUNCTION areafill ;

  /** Every surface has a clip surface associated with it (even clip
      surfaces). */
  const clip_surface_t *clip_surface ;

  /* Surface management functions. */

  /** Called during DL building, when surface is determined to be necessary.
      This function should be stateless. */
  void (*reserve)(void) ;

  /** Prepare a surface for rendering a tile or band.

      Implementing this method is optional.

      \param[in] handle  The surface set handle.
      \param rs A render state describing the tile or band about to be
      rendered. The surface handle (shared by the entire surface set), band
      limits, blit colorant map, band number, and other details are available
      through the render state pointer. The render state pointer is local for
      each thread, though some references from it may be shared between
      threads.

      \retval TRUE The rendering assignment will be accepted.
      \retval FALSE The rendering assignment could not be accepted; rendering
      will be aborted.
  */
  Bool (*assign)(surface_handle_t handle, render_state_t *rs) ;

  /** \brief Called before each object is rendered.

      This method \e must be implemented by all surfaces.

      \param[in] handle  The surface set handle.
      \param p_ri A render info state describing an object about to be
                  rendered. The surface handle (shared by the entire surface
                  set), clip rectangle, and other relevant details are available
                  through this pointer. It is local for each thread, though some
                  references from it may be shared between threads.

      \retval TRUE The object preparation was successful.
      \retval FALSE The object preparation failed, and rendering will be
                    aborted.
  */
  surface_prepare_t (*prepare)(surface_handle_t handle,
                               /*@notnull@*/ /*@in@*/ render_info_t *p_ri);

  /** \brief Render a completed transparency group region to its parent
      context.

      Render a completed transparency context into its parent context. The
      surface pointer of the parent context is supplied; the render operation
      may either use this to render surface primitives to the parent surface,
      or may recognise that the parent is of the same type and short-circuit
      the operation.

      This function is called within the band scope.

      \param[in] handle  The surface set handle.
      \param[in] rb      The render blit state ready for rendering to the
                         parent surface primitives, with appropriate surface
                         type and bounds set up.
      \param[in] group   The transparency group to render.
      \param[in] target  The transparency group to render into (NULL if the
                         final output).
      \param spotno      A spot number to select. Only spans using this screen
                         id should be rendered. If SPOT_NO_INVALID, then
                         all screens should be rendered. If the output is
                         contone, SPOT_NO_INVALID will be used.
      \param objtype     The halftone type. This is only set for object-based
                         screening, and when \a spotno is not
                         \c SPOT_NO_INVALID, to determine which halftone to
                         select.

      \retval TRUE if the function succeeded.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*backdropblit)(surface_handle_t handle,
                       /*@notnull@*/ render_blit_t *rb,
                       /*@notnull@*/ surface_backdrop_t group,
                       /*@null@*/ surface_backdrop_t target,
                       SPOTNO spotno,
                       HTTYPE objtype) ;

  /** Update function, called by the fill renderer after blitting all spans
      for a line.

      Implementing this method is optional. */
  surface_line_update_fn *line_update ;

  /** \brief Blit colormap optimisation method.

      The surface is given the opportunity to set the packing and expansion
      functions in the blit colormap and the packing word size before it is
      fixed for the render. */
  void (*blit_colormap_optimise)(struct blit_colormap_t *map) ;

  /* Attributes */
  uint32 n_rollover ;  /**< Number of objects before it's worth rolling over */
  Bool screened ;      /**< Is this surface screened? */
  uint32 image_depth ; /**< Max useful image depth in bits. */
  Bool suppress_softmasks; /**< Suppress rendering of softmasks */
  uint32 render_order ; /**< Bitmask of the SURFACE_ORDER_* values. */
} ;

/** Bitmask of values for the surface render order field. These values
    indicate the order in which spans should be presented to the surface,
    especially when rendering images. */
enum {
  SURFACE_ORDER_ORIGINAL = 0,  /**< Do not swap or flip image data. */
  SURFACE_ORDER_DEVICELR = 1,  /**< Prefer left-right along fast scan. */
  SURFACE_ORDER_DEVICETB = 2,  /**< Prefer top-bottom in slow scan. */
  SURFACE_ORDER_IMAGEROW = 4,  /**< Precondition data for longest image rows. */
  SURFACE_ORDER_COPYDOT = 8    /**< Precondition data to allow 1:1 image optimisations. */
} ;

#define SURFACE_INIT \
  { /* The "Matrix" part of the API: */                                 \
    BLITCLIP_SLICE_INIT, /* Base blits */                               \
    { NULLCLIP_SLICE_INIT, NULLCLIP_SLICE_INIT }, /* Object map blits */ \
    { NULLCLIP_SLICE_INIT, NULLCLIP_SLICE_INIT }, /* Max blits */       \
    NULL_SLICE_INIT, /* ROP blits */                              \
    NULLCLIP_SLICE_INIT, /* PCL Pattern blits */ \
    NULL_SLICE_INIT, /* Intersect blits */                            \
    { NULL_SLICE_INIT, NULL_SLICE_INIT }, /* Pattern recurse blits */   \
    {                                                                   \
      {NULL_SLICE_INIT, NULL_SLICE_INIT, NULL_SLICE_INIT, NULL_SLICE_INIT}, \
      {NULL_SLICE_INIT, NULL_SLICE_INIT, NULL_SLICE_INIT, NULL_SLICE_INIT}, \
    }, /* Pattern replicate blits */                                \
    NULL_SLICE_INIT, /* Pattern translation blits */ \
    NULLCLIP_SLICE_INIT, /* Pattern clip blits */                   \
    NULL_SLICE_INIT, /* Gouraud base blits */                       \
    { NULL_SLICE_INIT, NULL_SLICE_INIT}, /* Gouraud noise blits */    \
    NULL, /* areafill */                                             \
    NULL, /* clip_surface */                                          \
    /* Surface management functions: */                               \
    NULL, /* reserve */                                               \
    NULL, /* assign */                                       \
    NULL, /* prepare */                                               \
    NULL, /* backdrop_render */                                      \
    NULL, /* line_update */                                      \
    NULL, /* blit_colormap_optimise */                                \
    0,    /* rollover */                                              \
    FALSE, /* screened */                                            \
    0,     /* Let RIP pick image depth */                               \
    FALSE, /* Let RIP pick when soft mask suppression is required */ \
    SURFACE_ORDER_IMAGEROW, /* Precondition for longest image rows. */ \
  }

/** \brief The surface set structure collects the surface descriptions
    required to render a particular output format together, with management
    functions to control rendering. Surface set structures are \e immutable.
    They must \e not be changed while the RIP is interpreting or rendering.
    There should \e not be any data specific to an instantiation of a surface
    set stored in the surface set structure. */
struct surface_set_t {
  const sw_datum conditions ;        /**< Selection page device conditions. */
  const surface_set_t *next ;        /**< Next in init chain. */
  const surface_t *const *indexed ;  /**< Output surfaces, indexed by type. */
  size_t n_indexed ;                 /**< Number of entries in array. */

  /** \brief The surface set has been selected for rendering a new page.

      \param[in,out] instance  A location to store a surface instance handle.
      \param pagedict  A datum representing the page device dictionary,
                       which the surface can use to configure its operation.
      \param dataapi   The data API that can be used to examine \a pagedict.
      \param continued \c TRUE if the previous surface selection was the same.
                       In this case, \a instance will point to the instance
                       handle as it was left by the previous deselect() method.
                       If \a continued is \c FALSE on entry, \a instance will
                       point to a \c NULL handle.

      This function is called when the interpreter changes its choice of
      surface sets, during pagedevice installation. It is only called when
      the choice of surfaces changes, not for every page. The surface instance
      which this call may create is not associated with any display lists. The
      method \c introduce_dl will be used to associate the surface with one
      or more display lists.

      Implementing this method is optional.

      \retval TRUE The surface set was selected successfully.
      \retval FALSE The surface set could not be selected. The pagedevice
                    installation will fail. In this case, the surface is
                    deselected even if it was previously selected and
                    \a continued was \c TRUE.

      \note This function is called by the interpreter, possibly
      simultaneously with rendering calls using previously selected instances
      of this or other surface sets. It should not do anything that might
      prevent such instances from completing successfully. */
  Bool (*select)(surface_instance_t **instance,
                 const sw_datum *pagedict,
                 const sw_data_api *dataapi,
                 Bool continued
#if 0 /** \todo ajcd 2009-09-25: Should add details? */
                 int32 page_w, int32 page_h, int32 tile_w, int32 tile_h,
                 int32 stride, int32 bits,
                 Bool (*reserve)(size_t memsize)
#endif
) ;

  /** \brief The surface set has been deselected.

      \param instance  A pointer to the surface instance handle provided by
                       the select() method.
      \param continues \c TRUE if the next surface selection is the same. In
                       this case, the surface instance handle on exit will be
                       passed to the following select() method.

      This function is called when the interpreter changes its choice of
      surface sets from this surface set to another set.

      The surface instance passed to this call will not be associated with
      a display list. It may either have just been created by the \c select
      method, or may have been previously been disassociated from a previous
      display list by the \c retire_dl method.

      Implementing this method is optional.

      \note This function is called by the interpreter, possibly
      simultaneously with rendering calls using instances of this surface
      set. It should not do anything that might prevent such instances from
      completing successfully. */
  void (*deselect)(surface_instance_t **instance, Bool continues) ;

  /** \brief Introduce a display list to the surface.

      \param[in,out] handle  A location where a reference to the
                       instance-specific data can found, and page-specific
                       data can be stored.
      \param page      The display list being introduced.
      \param continued \c TRUE if this display list continues the same page as
                       a previous display list. In this case, the surface
                       handle will be as the previous \c retire_dl() method
                       for the page left it. This argument is \c FALSE if the
                       display list does not continue a page, in which case
                       the surface handle will contain the instance handle
                       as initialised by the \c select() method.

      This function is called when the interpreter initialises a display list
      for interpretation and ultimately rendering by the surface. The display
      list may not be rendered by this surface (it may be retired before
      rendering). Multiple display lists may be in existence simultaneously.

      Implementing this method is optional.

      \note This function is called by the interpreter, possibly
      simultaneously with rendering calls using instances of this surface
      set. It should not do anything that might prevent such instances from
      completing successfully. */
  Bool (*introduce_dl)(surface_handle_t *handle, struct DL_STATE *page,
                       Bool continued) ;

  /** \brief Inform the surface that a display list is being retired.

      \param[in,out] handle A pointer to the surface handle, as initialised
                       by the \c introduce_dl call.
      \param page      The display list being retired.
      \param continues \c TRUE if the same page will be continued using a
                       different display list. \c FALSE if this display list
                       completes this page.

      This function is called when the interpreter retires a display list
      from use. The display list may not have been rendered by this surface.
      Multiple display lists may be in existence simultaneously. The surface
      instance passed to this call will have previously been associated with
      a display list by the \c introduce_dl method. This call should
      disassociate the surface instance from the display list. If the \a
      continues parameter is \c TRUE, then the surface instance may be re-used
      by a subsequent display list. A future \c introduce_dl() call will be
      made to continue the same page, with the same instance pointer and a \c
      TRUE value for its \c continued argument.

      Implementing this method is optional.

      \note This function is called by the interpreter, possibly
      simultaneously with rendering calls using instances of this surface
      set. It should not do anything that might prevent such instances from
      completing successfully. */
  void (*retire_dl)(surface_handle_t *handle, struct DL_STATE *page,
                    Bool continues) ;

  /** \brief Determine if the surface set can directly render a ROPped
      object.

      PCL ROPs are expected to be handled either by the output surface or the
      transparency surface. The output surface is expected to be able to
      handle cases where the blit color does not require combining with other
      colors (destination, foreground or source):

        T   - Texture color
        S   - Source color
        0   - White
        255 - Black

      The output surface may handle more cases if desired.

      The transparency surface is expected to handle combinations of ROP and
      transparency that the output surface cannot handle.

      \param[in,out] handle  The surface handle, as initialised by the
                             \c introduce_dl call.
      \param[in] page        The display list on which the PCL object is being
                             introduced.
      \param[in] color       The DL color of the object.
      \param[in,out] attrib  The PCL attributes structure. Callback functions
                             should be used to read and write values to this.

      \retval TRUE  The surface set can handle this ROP and transparency
                    combination. The PCL attrib structure has been updated
                    with new values of the backdrop and patternBlit flags if
                    appropriate.
      \retval FALSE The surface set cannot handle this ROP and transparency
                    combination; the RIP will throw an error.
  */
  Bool (*rop_support)(surface_page_t *handle,
                      const struct DL_STATE *page,
                      const struct dl_color_t *color,
                      struct PclAttrib *attrib) ;

  /** \brief Notify the surface set that a page has started a render pass.

      This is called before any of the surface set functions are used during
      a DL render, or a partial paint.

      A surface can use this function to allocate and/or initialise a \c
      surface_render_t structure, which will be passed to all surface functions
      within the page scope (and all surface functions in the sheet,
      thread/frame, and band scopes if not overridden).

      Implementing this method is optional.

      \param[in,out] handle  A location where a reference to the page-specific
                             data can found, and render-pass specific data can
                             be stored.
      \param page_data       An opaque instance data structure for the page
                             data. Accessor functions are used to get
                             information from this structure.

      \retval TRUE if the function succeeded. In this case, \c render_end
              \e will be called (if implemented).
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*render_begin)(surface_handle_t *handle, page_data_t *page_data) ;

  /** \brief Notify the surface set that a page has completed a render pass.

      This is called after all of the surface set function uses are finished
      during a DL render, or a partial paint. This function can be used to
      destroy a page handle allocated in \c render_begin.

      Implementing this method is optional.

      \param[in] handle   The surface handle, as initialised by \c render_begin.
      \param page_data    An opaque instance data structure for the page
                          data. Accessor functions are used to get
                          information from this structure.
      \param success      A flag indicating if the page render since the
                          \c render_begin call was successful.

      \retval TRUE if the function succeeded.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*render_end)(surface_handle_t handle, page_data_t *page_data,
                     Bool success) ;

  /** \brief Notify the surface set that a sheet has started rendering.

      This function is called within the page scope. It is called by only one
      thread.

      Implementing this method is optional.

      \param[in,out] handle  A location where a reference to the page-specific
                             data can found, and sheet-specific data can be
                             stored.
      \param sheet_data      An opaque instance data structure for the sheet
                             data. Accessor functions are used to get
                             information from this structure.

      \retval TRUE if the function succeeded. In this case, \c sheet_end
              \e will be called (if implemented).
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*sheet_begin)(surface_handle_t *handle, sheet_data_t *sheet_data) ;

  /** \brief Notify the surface set that a sheet has completed rendering.

      This function is called within the page scope.

      Implementing this method is optional.

      \param[in] handle   The surface handle, as initialised by \c sheet_begin.
                          On entry, the surface handle will be in the state
                          it was left by \c render_begin.
      \param sheet_data   An opaque instance data structure for the sheet
                          data. Accessor functions are used to get
                          information from this structure.
      \param success      A flag indicating if the sheet render since the
                          \c sheet_begin call was successful.

      \retval TRUE if the function succeeded.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*sheet_end)(surface_handle_t handle, sheet_data_t *sheet_data,
                    Bool success) ;

  /** \brief Notify the surface set that a frame has started rendering.

      This function is called within the sheet scope. It is called by only one
      thread.

      Implementing this method is optional.

      \param[in,out] handle  A location where a reference to the page-specific
                             data can found, and frame-specific data can be
                             stored. On entry, the surface handle will be in
                             the state it was left by \c sheet_begin.
      \param frame_data      An opaque instance data structure for the frame
                             data. Accessor functions are used to get
                             information from this structure.

      \retval TRUE if the function succeeded. In this case, \c frame_end
              \e will be called (if implemented).
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*frame_begin)(surface_handle_t *handle, frame_data_t *frame_data) ;

  /** \brief Notify the surface set that a frame has completed rendering.

      This function is called within the sheet scope.

      Implementing this method is optional.

      \param[in] handle   The surface handle, as initialised by \c frame_begin.
      \param frame_data   An opaque instance data structure for the frame
                          data. Accessor functions are used to get
                          information from this structure.
      \param success      A flag indicating if the frame render since the
                          \c frame_begin call was successful.

      \retval TRUE if the function succeeded.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*frame_end)(surface_handle_t handle, frame_data_t *frame_data,
                    Bool success) ;

  /** \brief The band localiser function gives the surface set the
      opportunity the keep information live on the C stack whilst rendering a
      band.

      This routine may be called by multiple threads, each with its own
      thread-local surface handle. This function \e must call the callback
      parameter function exactly once, with the data parameter supplied.

      This function is called within the thread/frame scope.

      Implementing this method is optional. */
  surface_band_localiser_fn *band_localiser ;

  /** The size (in bits) of the packing unit for this surface. Values will be
      stored into the raster in units of this size, high bit leftmost. */
  int32 packing_unit_bits ;
} ;

#define SURFACE_SET_INIT(_select) \
  {                      \
    _select, /* selection */ \
    NULL, /* next surface set */ \
    NULL, /* indexed */   \
    0,    /* n_indexed */   \
    NULL, /* select() function. */   \
    NULL, /* deselect() function. */   \
    NULL, /* introduce_dl() function. */   \
    NULL, /* retire_dl() function. */   \
    NULL, /* rop_support() function. */   \
    NULL, /* begin page */                                            \
    NULL, /* end page */                                              \
    NULL, /* begin sheet */                                           \
    NULL, /* end sheet */                                             \
    NULL, /* begin frame */                                           \
    NULL, /* end frame */                                             \
    NULL, /* band localiser */                                        \
    8,    /* packing unit width */                                    \
  }

/** \brief A clip identifier used to indicate that there is no parent or
    complex clip.

    This definition \e must match \c CLIPID_INVALID. */
enum {
  SURFACE_CLIP_INVALID = 0
} ;

/** Clip surfaces take the basic surface data, and specialise it for
    clipping. */
struct clip_surface_t {
  surface_t base ; /**< Blit definitions for clipping. */

  /** \brief Determine if a complex clip state is known.

      This function is called within the band scope.

      Implementing this function in a clip surface is optional. If this
      function is not implemented, the RIP will assume that only one complex
      clip states can be stored, and will set up the render bounds to the
      minimal restriction of all of the complex clips to be combined.

      \param[in] handle  The surface set handle.
      \param clipid      A numeric identifier for the complex clip. For a
                         cached clip state to be valid, it should have been
                         used to create a clip in any unclosed clip context.

      \retval TRUE if the clip identified by \a clipid is cached in the
                   current clip context or a parent context, and can be
                   re-used.
      \retval FALSE if the clip identified by \a clipid is not cached in the
                   current clip context.
  */
  Bool (*complex_cached)(surface_handle_t handle, int32 clipid) ;

  /** \brief Construct a complex clip using a callback, and install it as the
      current clip.

      The clip localiser gives the clip surface the chance to keep
      information live on the C stack during clip construction.

      The clip surface \e must implement this method.

      This function \e must call the callback parameter function exactly
      once, with the data parameter supplied. The callback renders the clip
      to the blit chain, so the

      This routine is called within the band scope.

      \param[in] handle   The surface set handle.
      \param clipid       The identifier of the clip being constructed.
      \param parentid     The identifier of the enclosing complex clip, or \c
                          SURFACE_CLIP_INVALID if none.
      \param[in] rb       The blit render state that will be used by the clip
                          rendering. This state may be changed by clip
                          preparation, but may not be overridden by copying.
                          When the clip localiser is called, the render info
                          has been copied and initialised so that it contains
                          an empty blit chain, a mask colormap and a black or
                          white blit color (for normal and inverted clip
                          respectively), the clip form is set as outputform,
                          and the surface points at the clip surface
                          description.
      \param[in] callback A callback function that renders the clip.
      \param[in] data     Parameters for the callback function.

      \retval TRUE if the function succeeded. The complex clip will have been
              generated.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*complex_clip)(surface_handle_t handle,
                       int32 clipid, int32 parentid,
                       render_blit_t *rb,
                       surface_clip_callback_fn *callback,
                       surface_clip_callback_t *data) ;

  /** \brief Construct a rectangular clip on top of a complex clip, and
      install it as a clip on the parent surface.

      This function is called within the band scope.

      Implementing this function in a clip surface is optional.

      \param[in] handle   The surface set handle.
      \param rectid       The identifier of the rectangular clip.
      \param complexid    The identifier of the underlying complex clip, or \c
                          SURFACE_CLIP_INVALID if none.
      \param[in] bbox     The rectangular restriction to apply on top of the
                          complex clip.

      \retval TRUE if the function succeeded. The rectangular clip will have
              been installed as the current clip state.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*rect_clip)(surface_handle_t handle,
                    int32 rectid, int32 complexid,
                    const dbbox_t *bbox) ;

  /** \brief Indicate that a clip context is starting.

      The same clip identifier may be used at different times when rendering
      a band, but with different bounds restrictions. The clip surface must
      distinguish between these uses. The clip \c context_begin function is
      called when a new clip restriction is entered. If the clip surface is
      caching complex clip results, it should use clip context nesting to
      determine whether the cached clip is reusable. Note that the bounds may
      not always change when a new clip context is entered; if the surface
      wishes to do the best job of re-using clips, it should detect this
      case.

      The clip restriction is characterised by the render bounds, the
      y_sep_position, and the output form's vertical offset.

      This function is called within the band scope.

      Implementing this function in a clip surface is optional.

      \param[in] handle  The surface set handle.
      \param[in] p_ri    The render info state containing the context bounds,
                         separation position, and form offset.

      \retval TRUE if the function succeeded. The clip surface's \c context_end
                   function \e will be called, if implemented.
      \retval FALSE if the function failed. Rendering will be aborted.
  */
  Bool (*context_begin)(surface_handle_t handle,
                        const render_info_t *p_ri) ;

  /** \brief A clip context is ending, so cached clips in this context should
      be discarded.

      The same clip identifier may be used at different times when rendering
      a band, but with different bounds restrictions. The clip surface must
      distinguish between these uses. The clip \c context_end function is
      called when a new clip restriction is ended. If the clip surface is
      caching complex clip results, it should use clip context nesting to
      determine whether the cached clip is reusable. The bounds may not
      always change when a new clip context is entered; if the surface wishes
      to do the best job of re-using clips, it should detect this case.

      The clip restriction is characterised by the render bounds, the
      y_sep_position, and the output form's vertical offset.

      This function is called within the band scope.

      Implementing this function in a clip surface is optional.

      \param[in] handle  The surface set handle.
      \param[in] p_ri    The render info state containing the context bounds,
                         separation position, and form offset.
      \param maybe_reused  A flag indicating if this same restriction context
                           might be reused within the band render. This will
                           only be true for band-interleaved rendering, when
                           another colorant may be restricted in exactly the
                           same way.
  */
  void (*context_end)(surface_handle_t handle,
                      const render_info_t *p_ri,
                      Bool maybe_reused) ;

  OBJECT_NAME_MEMBER
} ;

#define CLIP_SURFACE_NAME "Clip surface implementation"

#define CLIP_SURFACE_INIT \
  { SURFACE_INIT, /* Base surface initialiser */ \
    NULL, /* complex_cached */ \
    NULL, /* complex_clip */ \
    NULL, /* rect_clip */ \
    NULL, /* context_begin */ \
    NULL, /* context_end */ \
  }

/** Transparency surfaces take the basic surface data, and specialise it for
    compositing. */
struct transparency_surface_t {
  surface_t base ; /**< Blit definitions for transparency. */

  /** \brief Determine if a region can be composited as one unit.

      The transparency surface \e must implement this method.

      This function is called repeatedly to extend the region which will be
      sent to the transparency surface. It should return TRUE if the storage
      resources are sufficient to process the specified area, given the
      indicated maximum number of transparency groups in a band.

      This function is called within the band scope.

      \param[in] handle  The surface set handle.
      \param groups      The number of transparency groups being composited in
                         the current band.
      \param[in] bbox    The bounding box of the region to be composited.

      \retval TRUE if the region can be composited.
      \retval FALSE if the region cannot be composited because of a storage
                    limitation. Rendering will abort if the smallest region
                    size cannot be rendered.
  */
  Bool (*region_request)(surface_handle_t handle,
                         /** \todo ajcd 2009-12-29: I don't like the presence
                             of either the bd_shared or composite context.
                             Work out a way of accessing these through the
                             thread-localised surface set handles. */
                         struct BackdropShared *bd_shared,
                         struct CompositeContext *bd_context,
                         int32 groups,
                         const dbbox_t *bbox) ;

  /** \brief The region localiser function gives the transparency surface the
      opportunity the keep information live on the C stack whilst rendering a
      band. This routine may be called by multiple threads, each with its own
      thread-local surface handle. This function \e must call the callback
      parameter function exactly once, with the data parameter supplied.

      The transparency surface \e must implement this method.

      This function is called within the band scope, or within a previous
      region scope.

      \param[in,out] handle  The page, sheet, thread, or band-specific handle.
      \param bd_context      The compositing context for this region.
      \param colorspace      The input blend space of the region.
      \param is_softmask     A flag indicating whether this is a softmask
                             group. If it is a softmask, the region will
                             be composited, but not rendered to the parent
                             group.
      \param is_pattern      A flag indicating whether this is a replicated
                             pattern.
      \param is_knockout     A flag indicating that the group is a knockout
                             group, and all objects should be composited
                             against the initial backdrop handle.
      \param[in] group       A handle identifying the group being composited.
      \param[in] initial     A handle identifying the group from which the
                             initial backdrop for the current group comes from.
                             This will be NULL if the group is isolated.
      \param[in] target      A handle identifying the parent group into
                             which this group will be rendered. This will only
                             be NULL for the top-level group.
      \param[in] to_target   The transparency attributes that will be used to
                             render the region to its parent.
      \param[in] bbox        The bounding box of the region restriction. The
                             group stack will be called for each region
                             restriction.
      \param[in] callback    A callback function that encapsulates all of the
                             rendering for this region will perform. The
                             callback function \e must be called exactly once
                             if the function succeeds.
      \param[in] data        An opaque data pointer that \e must be passed to
                             \c callback.

      \retval TRUE if the function succeeded.
      \retval FALSE if the function failed. The callback function may not have
                    been called in this case.
  */
  Bool (*region_create)(surface_handle_t handle,
                        /** \todo ajcd 2009-12-29: I don't like the presence
                            of the composite context. Work out a way of
                            accessing these through the thread-localised
                            surface set handles. */
                        struct CompositeContext *bd_context,
                        /** \todo ajcd 2010-06-02: Don't want to haul in the
                            whole of gu_chan.h just to get DEVICESPACEID
                            typedef. */
                        int32 colorspace,
                        Bool is_softmask,
                        Bool is_pattern,
                        Bool is_knockout,
                        /*@notnull@*/ surface_backdrop_t group,
                        /*@null@*/ surface_backdrop_t initial,
                        /*@null@*/ surface_backdrop_t target,
                        /*@notnull@*/ struct TranAttrib *to_target,
                        const dbbox_t *bbox,
                        render_region_callback_fn *callback,
                        render_region_callback_t *data) ;

  /** The width of transparency regions. Zero indicates that the maximum
      width should be used. This should be moved into a region surface
      eventually. */
  int32 region_width ;

  OBJECT_NAME_MEMBER
} ;

#define TRANSPARENCY_SURFACE_INIT \
  { SURFACE_INIT, /* Base surface initialiser */ \
    NULL, /* region_request */ \
    NULL, /* region_create */ \
  }

#define TRANSPARENCY_SURFACE_NAME "Transparency surface implementation"

/** \brief Introduce a new surface set to the list of available surfaces.

    \param surface_set  The new surface set to register. This must not have
                        been registered already, and must have all of its
                        fields except for the \a next pointer initialised.
*/
void surface_set_register(surface_set_t *surface_set) ;

/** \brief Select the appropriate surface set for a page device dictionary.

    \param pagedevdict  The page device dictionary used to select the surface
                        set.

    \returns A pointer to the surface set selected, or \c NULL if no surface
             set is suitable.
*/
const surface_set_t *surface_set_select(const struct OBJECT *pagedevdict) ;

/** \brief Mark a display list as requiring a particular surface, running
    the surface reserve function if necessary.

    \param page  The display list in which a surface requirement is marked.
    \param surface  One of the \c SURFACE_* enumeration values.
 */
void dl_surface_used(struct DL_STATE *page,
                     int surface) ;

/** \brief Function to discover a surface suitable for a particular task
    from a surface set.

    \param surfaces  A set of surfaces in which to find the enumerated surface
                     type.
    \param type      The type of surface to find.

    \returns A pointer to the surface description, or NULL if no surface of
             that type is found within the set.
 */
/*@null@*/ /*@observer@*/
const surface_t *surface_find(/*@notnull@*/ const surface_set_t *surfaces,
                              int type) ;

/** \brief Function to find a transparency surface in the surface set, and
    perform appropriate downcasting.

    \param surfaces  A set of surfaces in which to find the transparency
                     surface.

    \returns A pointer to the transparency surface description, or NULL if no
             transparency surface is found within the set.
 */
/*@null@*/ /*@observer@*/
const transparency_surface_t *surface_find_transparency(/*@notnull@*/ const surface_set_t *surfaces) ;

/** \brief Function to return the maximum useful depth for storing images.

    \param surfaces  The selected set of surfaces.
    \param ingroup   A boolean value, indicating whether the image is being
                     used in a transparency group or not.

    \returns 0 to let the RIP decide how many levels (use this for halftone).
    Otherwise, a bit-depth indicating the maximum depth of image data it's
    worth storing. Currently the bit depth is only tested for values of more
    than 8. */
uint32 surfaces_image_depth(/*@notnull@*/ const surface_set_t *surfaces,
                            Bool ingroup) ;

/** \brief A well-known surface that asserts and does nothing if used. */
extern surface_t invalid_surface ;

/** \brief A well-known surface suitable for creating bitmap or spanlist masks.
 */
extern surface_t mask_surface ;

/** \brief A well-known surface suitable for creating bitmap masks. */
extern surface_t mask_bitmap_surface ;

/** \brief A well-known surface suitable for creating pattern shapes. */
extern surface_t patternshape_surface ;

/** \brief Initialise C globals for surface. */
void surface_C_globals(struct core_init_fns *fns) ;

#ifdef DEBUG_BUILD
/** Bitmask of debug values for surface tracing */
enum {
  DEBUG_SURFACE_TRACE_OUTPUT = 1,
  DEBUG_SURFACE_TRACE_CLIP = 2,
  DEBUG_SURFACE_TRACE_TRANSPARENCY = 4,
  DEBUG_SURFACE_TRACE_CUSTOM = 8 /* MUST BE HIGHEST BIT. */
} ;

/** Debug control variable for surfaces. */
extern int32 debug_surface ;
#endif

/** \} */

#endif /* __SURFACE_H__ */

/* Log stripped */
