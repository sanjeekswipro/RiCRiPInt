/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!export:render.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Render state structures and main render function prototypes.
 */

#ifndef __RENDER_H__
#define __RENDER_H__ 1

#include "dlstate.h"  /* DL_STATE */
#include "displayt.h" /* LISTOBJECT et al. */
#include "bitbltt.h"  /* blit_t */
#include "swdevice.h" /* DEVICELIST */
#include "swhtm.h" /* sw_htm_render_info */
#include "rendert.h"
#include "surface.h"
#include "basemap.h" /* BASEMAP_INTER/RENDER for
                        render_objects_of_z_order_band_prepare */

struct MODHTONE_REF; /* from gu_htm.h */
struct ht_params_t; /* from htrender.h */
struct blit_color_t ; /* from CORErender */
struct blit_colormap_t ; /* from CORErender */
union blit_packed_t ; /* from CORErender */
struct surface_t ; /* from CORErender */
struct Backdrop; /* from backdrop.h */
struct CompositeContext; /* from backdrop.h */
struct task_specialise_private ; /* from COREmulti */
struct multi_mutex_t; /* from COREmulti */
struct trap_context_t; /* from COREtrapping */

/** \defgroup rendering Rendering functions.
 * \ingroup core
 *
 * Definitions of structures and functions used to encapsulate and track
 * rendering state.
 *
 * \{
 */


extern Bool trim_to_page;


/** Mutex to regulate access to the PGB device. */
extern struct multi_mutex_t pagebuffer_mutex;


/** Tracker references are used to track data relevant to particular calls
   of render_object_list_of_band. There will be a chain of pattern trackers,
   one per nested pattern. */
typedef struct pattern_tracker_t pattern_tracker_t;

/** Tracker references are used to track data relevant to particular calls of
   render_object_list_of_band. There will be a single render tracker for each
   render_objects_of_band call, tracking the current state of rendering
   through all recursive HDLs. */
typedef struct render_tracker_t render_tracker_t;

/** Tracker references are used to track data relevant to particular calls
   of render_object_list_of_band. There will be a chain of pattern trackers,
   one per nested pattern. */
typedef struct group_tracker_t group_tracker_t;


/* Bit-number values for render_state_t.pass_region_types and
   render_info_t.region_set_type */
enum {
  RENDER_REGIONS_NONE      = 0, /* region iteration is not expected */
  RENDER_REGIONS_BACKDROP  = 1,
  RENDER_REGIONS_DIRECT    = 2
};


#define ri_converting_on_the_fly(ri) \
  ((ri)->p_rs->pass_region_types == (RENDER_REGIONS_BACKDROP | RENDER_REGIONS_DIRECT) \
   && (ri)->region_set_type == RENDER_REGIONS_DIRECT)


struct render_forms_t {
  FORM retainedform;          /**< The normal output form. */
  FORM clippingform;          /**< Clipping mask form. */
  FORM patternform;           /**< Mask for patterned objects. */
  FORM patternclipform;       /**< Mask for augmenting patterned objects. */
  FORM patternshapeform;      /**< Mark for patterned objects augmented with
                                   recursive pattern shapes. */
  FORM maskedimageform;       /**< Mask in which masked image mask is stored. */
  FORM intersectingclipform;  /**< Mask form for self-intersecting objects. */
  FORM contoneform;
  FORM objectmapform;
  FORM htmaskform;
  blit_t *halftonebase;       /**< Spare mask line for halftone convergence. */
  blit_t *maxbltbase;         /**< Spare mask line for maxblting. */
  blit_t *clippingbase;       /**< Spare mask line for clipping creation. */
} ;

/*
 * Structure holding render states. render_blit_t contains the information
 * most frequently used and mutated by the blit functions. render_info_t
 * holds the information that may change per object. render_state_t hold
 * information set up for a whole band/frame/colorant render; if only the
 * render_info_t pointer is passed to a function, the render state info is
 * readable, but not mutable.
 *
 * These three structures approximately correspond to the page level
 * (render_state_t), object level (render_info_t), and sub-object/span level
 * (render_blit_t).
 *
 * When copying render_state_t or render_info_t, the internal pointers from
 * the render_info_t and render_blit_t sub-structures MUST be updated to
 * reflect the destination copy. Routines can copy just the render_info_t or
 * render_blit_t and update the internal pointers to "shadow" part of the
 * structure.
 */
struct render_blit_t {
  /* This structure is quite carefully laid-out to access the most important
     elements quickest. Don't shuffle the fields unless you have very good
     reason. */
  /*@dependent@*/
  FORM *outputform ;         /**< The current output form. */
  blit_t *ylineaddr ;        /**< Line address within the output form. */
  /*@dependent@*/
  FORM *clipform ;           /**< The current clipping form. */
  blit_t *ymaskaddr ;        /**< Line address within the clipping form. */
  dcoord x_sep_position ;    /**< Separation imposition X offset, added late. */
  dcoord y_sep_position ;    /**< Separation imposition Y offset, added early. */

  pattern_tracker_t *p_painting_pattern; /**< Pattern being painted. */

  uint8 clipmode ;           /**< Complex clip flag for object. */
  uint8 depth_shift;         /**< Shift for bit depth of raster. */
  uint8 maxmode;             /**< minblit/maxblit/no maxing. */
  uint8 opmode;              /**< overprint none/some/all. */

  struct blit_color_t *color ; /**< The color to be painted. */

  /*@dependent@*/
  const render_info_t *p_ri ; /**< Rest of render info data. */
  /*@dependent@*/
  blit_chain_t *blits ;       /**< Bitblit chain. */
} ;


/** Pattern rendering states. */
enum {
  PATTERN_OFF,      /**< No pattern in force. */
  PATTERN_PAINTING, /**< Painting a pattern. */
  PATTERN_CLIPPING  /**< Rendering top-level patterned shapes into mask. */
} ;

struct render_info_t {
  /*@dependent@*/
  const render_state_t *p_rs;              /**< Reference to non-mutable render state. */
  LISTOBJECT*           lobj;              /**< The object to render. */
  int                   pattern_state;     /**< PATTERN_* state. */
  dbbox_t               clip;              /**< Clip bounds, in current DL space. Should always be a subset of bounds. */
  /** x1maskclip and x2maskclip are the left and right edge of the
   * largest unclipped rectangle between clip.x1 and
   * clip.x2.  Any objects falling between these need not use the
   * clip mask.  x2maskclip can be larger than x1maskclip, in which case
   * all objects should be masked. */
  dcoord                x1maskclip, x2maskclip;
  uint8                 region_set_type; /**< Rendering type for current set of regions */
  Bool                  fSoftMaskInPattern; /**< Currently rendering a softmask inside a pattern? */
  int32                 overrideColorType; /**< Default value is GSC_UNDEFINED. */
  Bool                  generate_object_map; /**< Generate an object map. */
  dbbox_t               bounds;            /**< Restriction for region rendered, in current DL space. */
  struct ht_params_t   *ht_params;         /**< Halftone parameters. */
  group_tracker_t      *group_tracker ;    /**< Transparency group context. */
  const struct surface_t *surface ;        /**< Current surface. */
  render_blit_t         rb ;               /**< Blit render information. */
};


struct render_state_t {
  /* Per-band render state. This state should not be modified below into
     render_colorants_of_band(), because the multi-threaded RIP copies the
     interpreter's render state to initialise the renderer threads' render
     state. All per-colorant state is separated into a separate
     sub-structure, and must be initialised in render_colorants_of_band(). */

  DL_STATE          *page;                 /**< The page being rendered. */
  render_forms_t    *forms;                /**< Forms to render into. */
  GUCR_RASTERSTYLE*  hr;                   /**< Raster handle for current rendering. */
  GUCR_CHANNEL*      hf;                   /**< Handle of current frame. */
  unsigned int       nChannel;             /**< Index of current channel. */
  LISTOBJECT*        lobjErase;            /**< Erase dl object for band. */
  struct CompositeContext *composite_context; /**< Workspace for backdrop to do compositing. */
  struct Backdrop   *backdrop;             /**< Backdrop for full object compositing (optional). */
  uint32             nCi;                  /**< Total number of colorants. */
  int32              band;                 /**< Actual band in DL being rendered. */
  uint32             pass_region_types;    /**< Region types done in the current pass (used to iterate regions in the page group) */
  Bool               fPixelInterleaved;    /**< Doing pixel interleaving? */
  uint8              maxmode;              /**< Maxblit mode, in case needed. */
  Bool               fMergePatterns;       /**< Knockout blend mode? */
  Bool               dopatterns;           /**< Do we do patterns recursively? */
  struct sw_htm_render_info *htm_info;     /**< Page-wide data for modular hts. */
  surface_handle_t   surface_handle ;      /**< Surface set specific data. */
  dl_color_t         dlc_watermark ;       /**< DL color for watermarking. */
  SPOTNO             spotno_watermark ;    /**< Spotno for watermarking. */
  /** trap_backdrop_context is used to pass color information from backdrop
   * objects to the trapping engine. It is only used for rendering backdrop
   * objects. */
  struct trap_context_t *trap_backdrop_context;

  /* Per-colorant render state. This state should all be initialised in
     render_colorants_of_band(). */
  struct {
    union blit_packed_t *overprint_mask ;  /**< Overprint channel mask. */
    union blit_packed_t *maxblit_mask ;    /**< Maxblit channel mask. */
    struct blit_colormap_t *blitmap ;      /**< Output color-channel mapping. */
    struct blit_color_t *erase_color ;     /**< Storage for erase blit color. */
    struct blit_color_t *knockout_color ;  /**< Storage for knockout blit color. */
    /*@dependent@*/
    render_tracker_t *renderTracker ;      /**< Current tracking details. */

    GUCR_COLORANT*   hc;                   /**< Current colorant handle. */
    Bool            *p_white_on_white;     /**< Pointer to w-o-w flag. */

    /** bandlimits is the portion of the band that could be touched by
        rendering, in the base DL coordinate space. It is initialised to the
        limits of the band at the top level of rendering. It is restricted in
        two cases: the top-level pattern restricts the bandlimits to the area
        touched by patterned objects, and rendering for groups outside of
        replicated pattern cells restricts the bandlimits to the set of regions
        being iterated over. The bandlimits are used by softmask rendering to
        cache the set of blocks a softmask appears in, and also by group
        rendering to tell which regions are currently being iterated over. */
    dbbox_t          bandlimits;

    Bool             fIsHalftone;          /**< Doing halftone rendering? */
    Bool             bg_separation;        /**< Is current separation a background one? */
    Bool             fSelfIntersect;       /**< Render in self-intersect mode? */
  /* The modular halftone and screening fields interact in this way:

                          Rendering Rendering  Generating  Generating  Modular
                          normal    normal     modular     modular     halftone
                          halftone  contone    halftone    halftone    in-RIP
                          output    output     colors      mask        screening

     fIsHalftone          TRUE      FALSE      FALSE       TRUE        TRUE
     htm_info             NULL      NULL       mht_info    mht_info    mht_info
     selected_mht         NULL      NULL       NULL        mht         NULL
  */
    const struct MODHTONE_REF *selected_mht; /**< Currently screening modular ht. */
    Bool             selected_mht_is_erase; /**< The erase color uses this ht. */
  } cs ; /**< Per-colorant render state */

  /* The render_info_t sub-structure is also per-colorant state, but is left
     outside the structure to reduce the changes required. */
  render_info_t      ri;                   /**< Render function info. */
};

/* Consistency conditions for use in assertions. The first three tests check
   that the internal pointers are consistent. The next two check if it is safe
   to copy a parent structure for a render pointer. */
#define RENDER_STATE_CONSISTENT(rs_) \
  ((rs_) && (rs_)->ri.p_rs == (rs_) && (rs_)->ri.rb.p_ri == &(rs_)->ri)
#define RENDER_INFO_CONSISTENT(ri_) \
  ((ri_) && (ri_)->rb.p_ri == (ri_) && RENDER_STATE_CONSISTENT((ri_)->p_rs))
#define RENDER_BLIT_CONSISTENT(rb_) \
  ((rb_) && RENDER_INFO_CONSISTENT((rb_)->p_ri))
#define RENDER_BLIT_COPY_INFO(rb_) \
  ((rb_) && (rb_) == &(rb_)->p_ri->rb) /* i.e. pointer of blit's info is blit */
#define RENDER_INFO_COPY_STATE(ri_) \
  ((ri_) && (ri_) == &(ri_)->p_rs->ri) /* i.e. pointer of info's state is info */

/* Make a copy of a render_info_t from various sources, fixing up internal
   pointers. First argument is destination, second is source. */
#define RS_COPY_FROM_RS(to_, from_) MACRO_START \
  register render_state_t *_to_ = (to_) ;       \
  *_to_ = *(from_) ;                            \
  _to_->ri.p_rs = _to_ ;                        \
  _to_->ri.rb.p_ri = &_to_->ri ;                \
MACRO_END

#define RS_COPY_FROM_RI(to_, from_) MACRO_START \
  register render_state_t *_to_ = (to_) ;       \
  const register render_info_t *_from_ = (from_) ; \
  *_to_ = *_from_->p_rs ;                       \
  _to_->ri = *_from_ ;                          \
  _to_->ri.p_rs = _to_ ;                        \
  _to_->ri.rb.p_ri = &_to_->ri ;                \
MACRO_END

#define RS_COPY_FROM_RB(to_, from_) MACRO_START \
  register render_state_t *_to_ = (to_) ;       \
  const register render_blit_t *_from_ = (from_) ; \
  *_to_ = *_from_->p_ri->p_rs ;                 \
  _to_->ri = *_from_->p_ri ;                    \
  _to_->ri.rb = *_from_ ;                       \
  _to_->ri.p_rs = _to_ ;                        \
  _to_->ri.rb.p_ri = &_to_->ri ;                \
MACRO_END

/* Make a copy of a render_info_t from various sources, fixing up internal
   pointers. First argument is destination, second is source. */
#define RI_COPY_FROM_RS(to_, from_) MACRO_START \
  register render_info_t *_to_ = (to_) ;        \
  *_to_ = (from_)->ri ;                         \
  _to_->rb.p_ri = _to_ ;                        \
MACRO_END

#define RI_COPY_FROM_RI(to_, from_) MACRO_START \
  register render_info_t *_to_ = (to_) ;        \
  *_to_ = *(from_) ;                            \
  _to_->rb.p_ri = _to_ ;                        \
MACRO_END

#define RI_COPY_FROM_RB(to_, from_) MACRO_START \
  register render_info_t *_to_ = (to_) ;        \
  const register render_blit_t *_from_ = (from_) ; \
  *_to_ = *_from_->p_ri ;                       \
  _to_->rb = *_from_ ;                          \
  _to_->rb.p_ri = _to_ ;                        \
MACRO_END

/* Copies of render_blit_t can just be made, they don't have back-pointers to
   fix. */

/** States of clipping. */
enum {
  CLIPPING_rectangular = 0, /* Default state is rectangular clipping */
  CLIPPING_firstcomplex,
  CLIPPING_complex
} ;

/** Current render details. This structure is used to track the current state
   of rendering through recursive HDL calls, so that clip damaging changes in
   HDLs are noticed by their parents, and so that simple recursions avoid
   re-generating pattern or clip forms. */
struct render_tracker_t {
  STATEOBJECT *oldstate ; /**< The current state. */
  CLIPOBJECT *oldclip ; /**< The current clipping state. */
  uint32 clipping ; /**< One of the CLIPPING_* enum values. */
  CLIPOBJECT *clippingformstate ; /**< The complex clip in the clipform */
  Bool checkstate ; /**< Force a check of details, even if states are same */
  dcoord x1bandclip, x2bandclip; /**< the extents of the clipping mask */
  Bool augmented_patternshapeform; /**< augmented with the replicated clip from a child pattern */
  SPOTNO oldspotno; /**< The current spot number. */
} ;

/** Initialise a render tracker. */
void render_tracker_init(render_tracker_t *tracker) ;

/** Set the clip context key, in response to a band limits change. */
Bool clip_context_begin(render_info_t *ri) ;

/* Stop the clipform from being reused for subsequent objects,
   without changing bandclips or pattern replication limits. */
void clip_context_end(render_info_t *ri) ;

/** Initialise a set of render forms. */
void init_forms(render_forms_t *forms, DL_STATE *page, Bool doing_mht) ;

/* Prepare a render state for mask blit into form */
void render_state_mask(
  /*@notnull@*/ /*@out@*/  render_state_t *rs,
  /*@notnull@*/ /*@out@*/  blit_chain_t *blits,
  /*@notnull@*/ /*@out@*/  render_forms_t *forms,
  /*@notnull@*/ /*@in@*/   struct surface_t *surface,
  /*@notnull@*/ /*@in@*/   FORM *to) ;


/** Indicates whether rendering bitmaps is likely to be faster than RLE.
 *
 * This is allowed to be wrong, since it's only used for optimization.
 * In the future, when modular and in-RIP halftones can be mixed, these
 * optimizations cannot always know what to pick, anyway. */
Bool rendering_prefers_bitmaps(DL_STATE *page);


Bool rip_to_disk(corecontext_t *context);

/** Indicates if a partial paint is allowed in the current state. */
Bool partial_paint_allowed(corecontext_t *context);

Bool external_retained_raster(corecontext_t *context, uint8 id[],
                              int32 use_count);

void init_render_c(void);
void finish_render_c(void);

void render_band_debug_marks(render_state_t *p_rs, int32 dl_bandnum);
Bool render_objects(render_state_t *p_rs); /* watermark rendering */

/** Prepare the render state for rendering the first (erase) object of a band.
 *
 * \param p_rs The render state which will be prepared
 * \param[in] abs_target_band The index of the target band into which the erase
 *                            will be rendered. NOTE: This index should be an
 *                            absolute band index; the current band scaling
 *                            factor will be applied by this function.
 *
 * This function will find the erase object, and unpack it and the erase color
 * into the render state. It should be called for all bands, regardless of
 * whether the band data is retrieved from partial paint or rendered by
 * the erase object.
 *
 * \see render_erase_of_band
 */
void render_erase_prepare(render_state_t *p_rs, int32 abs_target_band) ;

/** Erase the band with the erase color.
 *
 * \param p_rs The render state
 * \param[out] ripped_something_out A pointer to a Boolean that will be set
 *                                  to indicate whether anything was painted.
 * \param[out] do_modular_erase A pointer to a Boolean that will be set to
 *                              indicate if the erase should be rendered by
 *                              a screening module.
 *
 * \retval TRUE Erased OK.
 * \retval FALSE An error occurred during rendering.
 *
 * \c render_erase_prepare must have been called prior to this function. Note
 * that this function also initialises the \c p_white_on_white field of the
 * render state.
 *
 * \see render_erase_prepare
 */
Bool render_erase_of_band(/*@notnull@*/ /*@in@*/ render_state_t *p_rs,
                          /*@notnull@*/ /*@out@*/ Bool *ripped_something_out,
                          /*@notnull@*/ /*@out@*/ Bool *do_modular_erase);

Bool render_object_list_of_band(
  /*@notnull@*/ /*@in@*/ render_info_t *p_ri,
  /*@notnull@*/ /*@in@*/ DLRANGE *dlrange);

Bool render_single_listobj(
  /*@notnull@*/ /*@in@*/ render_info_t *p_ri,
  /*@notnull@*/ /*@in@*/ LISTOBJECT *lobj);

Bool render_objects_of_z_order_band(
  /*@notnull@*/ /*@in@*/        DLRANGE *dlrange,
  /*@notnull@*/ /*@in@*/        render_state_t *rs,
                                dcoord ystart ,
                                dcoord yend ) ;

/** Prepare clipping bbox for an object, intersecting it with the bounds and
    compensating for the tesselating scan conversion rule if necessary. */
Bool clip_to_bounds(
  /*@notnull@*/ /*@in@*/        render_info_t *ri,
  /*@notnull@*/ /*@in@*/        CLIPOBJECT *newclip,
  /*@notnull@*/ /*@out@*/       dbbox_t *clip) ;

Bool regenerate_clipping(
  /*@notnull@*/ /*@in@*/        render_info_t *ri,
  /*@notnull@*/ /*@in@*/        CLIPOBJECT *newclip) ;

Bool get_pagebuff_param(
                                        DL_STATE *page,
  /*@notnull@*/ /*@in@*/ /*@observer@*/ uint8 *name ,
  /*@notnull@*/ /*@out@*/               int32 *value ,
                                        int32 int_or_bool ,
                                        int32 default_value ) ;

/** Send the param to the PGBdev and translate any error to PS. */
Bool send_pagebuff_param(DL_STATE *page, DEVICEPARAM *param);


/** Clean up page buffers after a render. */
void erase_page_buffers(DL_STATE *page);

struct Backdrop *render_state_backdrop(const render_state_t* rs);

void render_task_specialise(corecontext_t *context,
                            void *args,
                            struct task_specialise_private *data) ;

#if defined(DEBUG_BUILD)
enum {
  DEBUG_RENDER_SHOW_BANDS = 1,  /* Display band boundaries */
  DEBUG_RENDER_RASTERSTYLE = 2, /* Print rasterstyle just before rendering */
  DEBUG_RENDER_ROLLOVER = 4,    /* bbox for rollovers */
  DEBUG_RENDER_SHOW_THREADS = 8, /* show which thread is doing which band */
  DEBUG_RENDER_GRAPH_TASKS = 16 /* graphviz output of render task graph */
} ;

extern int32 debug_render ;

void init_render_debug(void) ;
#endif

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
