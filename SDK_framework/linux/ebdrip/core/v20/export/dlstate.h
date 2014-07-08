/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dlstate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list page structure definition.
 */

#ifndef __DLSTATE_H__
#define __DLSTATE_H__

#include "mm.h"
#include "graphict.h"
#include "displayt.h"
#include "bitGrid.h"
#include "dl_storet.h"
#include "rleColorantMappingTypes.h"
#include "imdecodes.h"
#include "gscparams.h"
#include "timelineapi.h" /* sw_tl_ref */
#include "gsc_icc.h" /* NUM_CSA_SIZE */
#include "swdevice.h"

struct surface_set_t ; /* surface.h */
struct surface_instance_t ; /* surface.h */
struct surface_page_t ; /* surface.h */
struct task_t ; /* taskt.h */
struct dlc_context_t ; /* dl_color.h */
struct resource_requirement_t ; /* COREmulti opaque type */
struct irr_state_t ; /* irr.h */
struct irr_store_t ; /* irr.h */
struct IM_EXPBUF ; /* imexpand.h */
struct IM_SHARED;

/*
 * Display List State.
 *
 * This file defines the structure which groups together a number of variables
 * which describe the current state of the page being generated.
 *
 * This used to be important when the RIP supported pipelining, and is still
 * a convenient way of collecting related information.
 */

/** How many display lists can be in existence at a time? This must be at
    least 2 (one for interpretation, one for queued rendering). */
#define NUM_DISPLAY_LISTS 6

/**
 * The DL is created in a number of memory pools corresponding to the
 * expected lifetime.
 * There are currently two distinct DL purging phases, so three DL pools
 * are used :
 *   dl_mem_pool[0] for DL elements that can never be purged
 *   dl_mem_pool[1] for DL elements that are purged reluctantly
 *   dl_mem_pool[2] for DL elements that are candidates for immediate purging
 *   dl_mem_pool[3] for DL elements that are highly likely to be freed in low
 *                  memory.
 *
 * \todo BMJ 19-Aug-08 : work in progress : 2 stage purging not yet implemented
 */
#define N_DL_POOLS 4

/**
 * List of all HDLs used by this DL.
 *
 * This allows easy DL element enumeration guaranteed to find all objects
 * without having to traverse the complex hierachical structure of a DL.
 * It also avoids the need to mark elements as visited to avoid hitting them
 * twice.
 * Implemented as a single linked list as maximum number of HDLs in even
 * the most complex pages has found to be in the thousands.
 */
struct HDL_LIST {
  HDL *hdl;                /**< HDL in question */
  struct HDL_LIST *next;   /**< Single linked list of all HDLs */
};

struct BackdropShared;
struct imlut_t;
struct DL_OMITDATA;

/** One item on the PCL idiom queue. */
typedef struct pcl_idiom_part {
  DLREF *head ;  /**< First DL reference in this part. */
  dbbox_t bbox ; /**< Bounding box of this part. */
  uint32 nobj ;  /**< Number of objects in this part. */
} pcl_idiom_part ;

/**
 * State information about a Display List
 *
 * Do not change this structure without modifying initialization in display.c.
 * The fields marked (P) are set by reset_pagedevice() calls or their
 * descendents, not including dl_begin_page().
 * The fields marked (E) are initialised at startup, and reset by the erase
 * task that runs after rendering the page.
 * The fields marked (B) are set by dl_begin_page().
 * The fields marked (R) are reset just before asynchronous rendering.
 * Other fields have special treatment.
 */
struct DL_STATE {
  /** \todo ajcd 2011-03-02: Data specific to this DL: */
  dl_erase_nr eraseno ;       /**< Incrementing ID for DL instance (B). */
  dl_erase_t erase_type ;     /**< How to erase this display list (RB). */
  Bool forcepositive ;        /**< Result of probing transfer functions for
                                   erase objects (uncategorised). */
  uint8 reserved_bands ;      /**< Bitmask of bands types for render (E). */
  uint32 surfaces_used ;      /**< Surfaces used from surface set (EB). */
  sw_tl_ref timeline ;        /**< The major processing timeline for this page. */
  HDL_LIST *all_hdls;         /**< List of all HDLs used for this DL (EB). */
  struct IMAGEOBJECT *im_list ;   /**< Linked list of all images (E). */
  struct imlut_t *image_lut_list; /**< Images LUTs, shared between images (E). */
  /** Image decodes array cache (see imdecodes.h for details) (E). */
  struct im_decodesobj_t *decodesobj[DECODESOBJ_NUM_SLOTS];

  /* Decode arrays for mapping COLORVALUE values to input color values. */
  float *cv_fdecodes[2]; /**< Shared fdecodes, for additive[0] and
                              subtractive[1] spaces (E). */
  int32 *cv_ndecodes[2]; /**< Shared ndecodes, for additive[0] and
                              subtractive[1] spaces (E). */
  int32  im_bufsize ;    /**< Size of the largest expansion buffer (E). */
  /** Force image expansion buffers to be large enough for
      interleaving, even if it doesn't seem immediately
      necessary. This is for the trapping and CRE surfaces. */
  Bool im_force_interleave ;
  /** Flag for (dis)allowing image store purges by low-memory actions.
      This applies to image store, expanders and filters. This doesn't
      apply to explicit purges by the image code itself (E). */
  Bool im_purge_allowed;
  SPOTNO default_spot_no; /**< Default spot number (B). */
  struct dlc_context_t *dlc_context; /**< dl color cache, const colors, workspace etc (EB). */
  dl_color_t dlc_erase;    /**< Same color as erase listobject (E). */
  dl_color_t dlc_knockout; /**< Color to be used for knockouts:
                              usually but not always the same as the
                              erase color. (e.g. contoneMask) (E). */
  struct task_t *next_task ; /**< DL construction task (B). */
  struct task_group_t *all_tasks ; /**< Group for all DL tasks (B). */
  struct {                      /**< Internal Retained Raster. */
    Bool generating ;           /**< IRR generating phase. Set by pagedevice key (P). */
    struct irr_state_t *state ; /**< IRR state for generating or replaying (E). */
    struct irr_store_t *store ; /**< IRR store for replaying (E). */
  } irr;

  /*
   * The display list is stored in a number of memory pools, each one
   * containing objects with the same expected lifetime (in terms of when
   * they might get freed by a low-memory DL purge action).
   */
  mm_pool_t dlpools[N_DL_POOLS]; /**< Array of DL memory pools (E). */
  mps_ap_t dl_ap; /* Allocation point for lobjs and dlrefs (E). */
  DlSSSet stores;          /**< The state store set (B). */
  Bool trapping_active; /**< Is this DL trapped? */
  struct resource_requirement_t *render_resources ;

  COLOR_STATE *colorState ; /**< Color state and caches for back-end transforms. */

  /** \todo ajcd 2011-03-02: Data common to all DLs for this page: */
  struct DL_OMITDATA *omit_data; /**< Separation omission data (B). */

  int32 highest_sheet_number ; /**< Highest sheet number sent to PGB during partial or final paints of DL (E). */
  struct SPOTNO_LINK *spotlist;    /**< Backdrop list of spotno/objtype (E). */
  dbbox_t page_bb ;                /**< Union of non background marks on page (E). */
  Bool rippedtodisk;               /**< Rendering called for this page (E). */
  Bool rippedsomethingsignificant; /**< Rendered content for this page (E). */
  unsigned int pageno ;            /**< Page sequence number within job. */

  /** \todo ajcd 2011-03-02: Data common to all DLs for this pagedevice: */
  int32 sizedisplaylist ;          /**< Num. bands in page (P) */
  int32 sizedisplayfact ;          /**< DL bands per render band factor (P). */

  int32 sizefactdisplaylist ;      /**< Num. factored bands in page (P) */
  int32 sizefactdisplayband ;      /**< Num. factored lines in band (P) */

  int32 page_w, page_h;            /**< Page width and height (P). */
  SYSTEMVALUE xdpi, ydpi;          /**< Page resolution (P). */
  size_t band_l, band_l1; /**< Bytes on a band line (output/1-bit) (P). */
  int32 band_lines;                /**< Lines in a band (P). */
  size_t scratch_band_size; /**< Size of any scratch band for PGBdev. (P) */

  uint8 rle_flags ;                /**< RLE generation flags (P). */
  uint8 ScanConversion ;           /**< SC_RULE_* (P). */

  GUCR_RASTERSTYLE *hr ; /**< Device rasterstyle (P). */
  const struct surface_set_t *surfaces ; /**< Selected surface set (P). */
  struct surface_instance_t *sfc_inst ;  /**< Selected surface instance data (P). */
  struct surface_page_t *sfc_page ;      /**< Selected surface page data (P). */

  COLORSPACE_ID virtualDeviceSpace; /**< The color space of the virtual
                                         device; only relevant when
                                         backdrop rendering (P). */
  COLORANTINDEX virtualBlackIndex ; /**< The black channel index for the
                                         virtual device (P). */
  COLORANTINDEX deviceBlackIndex ; /**< The black channel index for the
                                        real device (P). */
  Bool fOmitHiddenFills ;  /**< Rollover fills (P). */
  Bool framebuffer ;       /**< Framebuffer device (P). */
  int32 job_number ;       /**< JobNumber parameter (P). */
  int32 trap_effort ; /**< Controls amount of work we allow for trapping (P). */
  COLOR_PAGE_PARAMS colorPageParams; /**< Private params for the color module (P). */

  struct {
    uint32 CompressImageParms;     /**< Fine-tune image compression to specific bit depths. */
    int8   CompressImageSource;    /**< Flags if image source pixels get compressed. */
    Bool   LowMemImagePurgeToDisk; /**< Allow purging of images to disk. */
  } imageParams;

  /** \todo ajcd 2011-03-02: Data common to all DLs for this job: */
  struct corejob_t *job ;          /**< Job this DL belongs to. */
  Bool   pcl5eModeEnabled ;        /**< The job is PCL5e (mono). */
  Bool   opaqueOnly ;              /**< The print model is opaque-only (PCL). */

  /** \todo ajcd 2011-03-02: Data that should be tracked in the front-end
      (target) only: */
  HDL *currentHdl;                 /**< The topmost HDL open (RE). */
  HDL *targetHdl;                  /**< The target HDL for additions (RE). */
  Group *currentGroup;             /**< The topmost Group on currentHdl stack (RE). */
  struct {
    Bool knockout;                 /**< Page group knockout flag (E). */
    OBJECT colorspace, csa[NUM_CSA_SIZE]; /**< Page group colorspace (E). */
  } page_group_details;
  STATEOBJECT *currentdlstate ;    /**< Current DL state (RE). */

  uint32 groupDepth ;              /**< Nesting depth of groups (E). */

  int32 max_region_width;          /**< Maximum columns in a transparency
                                      region (this parameter should be in
                                      page target, not DL) (P). */
  int32 max_region_height;         /**< Maximum lines in a transparency
                                      region (this parameter should be in
                                      page target, not DL) (P). */
  Bool force_deactivate ;          /**< Error in render last deactivation (E). */
  Bool backdropAutoSeparations;    /**< Allow auto separations in the virtual
                                        device when backdrop rendering (P). */
  Bool deviceROP;                  /**< Permit the device to optimise PCL ROPs
                                        directly, rather than using the
                                        backdrop to implement ROPs (P). */

  /** \todo ajcd 2011-03-02: Data that should be tracked in the back-end only: */
  int32 region_width;              /**< Columns in a transparency region (B). */
  int32 region_height;             /**< Lines in a transparency region (B). */
  size_t band_lc ;        /**< Bytes on a band line (MHT contone). */
  struct BackdropShared *backdropShared; /**< State shared between all backdrops. */
  BitGrid *regionMap;              /**< Region map indicates regions of page
                                        requiring compositing (E). */
  int32  im_imagesleft ;           /**< This is a semaphore (E). */
  struct IM_EXPBUF *im_expbuf_shared; /**< Shared (largest) expansion buffer (E). */
  struct IM_SHARED *im_shared ;    /**< Shared image state (E). */

  uint32 watermark_seed ;
  DEVICELIST *pgbdev;              /**< The PGB device for this page */

  /** \todo ajcd 2011-02-26: Data that shouldn't be in the DL structure: */
  Bool output_object_map; /**< \todo ajcd 2011-03-02: Should be represented in rasterstyle (P). */

  /** The PCL idiom queue. */
#define PCL_IDIOM_MAX_LEN 3
  struct {
    /** Circular queue of idiom parts. Each part may have multiple objects,
        usually adjacent, combining to perform some function. The queue has
        one extra slot so that we can delay flushing until we see an object
        that doesn't combine with the last part. */
    pcl_idiom_part parts[PCL_IDIOM_MAX_LEN + 1] ;
    LISTOBJECT *last ; /**< Pointer to final object in last part. */
    uint32 nparts, first ; /**< Number of parts, index of first in queue. */
  } pclIdiomQueue ;
};

/** \brief Acquire lock on current interpret page, and return it.

    This call must be matched by an inputpage_unlock() to release the
    current interpret page. This lock does not guard modifications to the
    contents of the current interpret page, it does prevent the interpret
    page pointer moving on while it is held.

    \note If you're going to lock both outputpage and inputpage
    simultaneously, you MUST take outputpage lock first. This is not enforced
    by a lock rank, because we want them to operate independently most of the
    time, but you MUST follow this rule or risk introducing deadlocks. */
DL_STATE *inputpage_lock(void);

/** \brief Release the current interpret page lock. */
void inputpage_unlock(void);

/** \brief Acquire lock on current render page, and return it.

    This call must be matched by an outputpage_unlock() to release the
    current render page. This lock does not guard modifications to the
    contents of the current render page, it does prevent the render
    page moving on while it is held.

    \note If you're going to lock both outputpage and inputpage
    simultaneously, you MUST take outputpage lock first. This is not enforced
    by a lock rank, because we want them to operate independently most of the
    time, but you MUST follow this rule or risk introducing deadlocks. */
DL_STATE *outputpage_lock(void);

/** \brief Release the current render page lock. */
void outputpage_unlock(void);

/** Default values for page group isolated and knockout flags. */
#define DEFAULT_PAGE_GROUP_ISOLATED   (TRUE)
#define DEFAULT_PAGE_GROUP_KNOCKOUT   (FALSE)

/** Bit flags for reserved bands. Only got 8 bits reserved at the moment. */
enum {
  RESERVED_BAND_CLIPPING = 1,
  RESERVED_BAND_PATTERN = 2,
  RESERVED_BAND_PATTERN_SHAPE = 4,
  RESERVED_BAND_PATTERN_CLIP = 8,
  RESERVED_BAND_MASKED_IMAGE = 16,
  RESERVED_BAND_SELF_INTERSECTING = 32,
  RESERVED_BAND_MODULAR_SCREEN = 64, /* Mask and tone bands */
  RESERVED_BAND_MODULAR_MAP = 128,   /* Object map */
  RESERVED_BAND_MAX_INDEX = 9        /**< One per mask band plus contone bands
                                        for MHT. */
} ;

/** Flags to control RLE generation downloading */
enum {
  RLE_GENERATING = 0x01,       /**< Generate RLE pagebuffer */
  RLE_LINE_OUTPUT = 0x04,      /**< D_OUTPUT line header only. */
  RLE_TRANSPARENCY = 0x08,     /**< Produce transparent RLE. */
  RLE_NO_COMPOSITE = 0x10,     /**< Suppress composited spans - only has effect
                                  when RLE_TRANSPARENCY is set. */
  RLE_OBJECT_TYPE = 0x20
} ;

/* PLEASE don't use these predicate macros outside of rleblt.c unless
   absolutely necessary. Queries should be performed using surface API calls
   or attributes that are appropriate to the functionality needed. */
#define DOING_RUNLENGTH(p) (p->rle_flags & RLE_GENERATING)
#define DOING_TRANSPARENT_RLE(p) \
  ((p->rle_flags & (RLE_GENERATING | RLE_TRANSPARENCY)) == \
    (RLE_GENERATING | RLE_TRANSPARENCY))

#endif /* protection for multiple inclusion */

/*
Log stripped */
