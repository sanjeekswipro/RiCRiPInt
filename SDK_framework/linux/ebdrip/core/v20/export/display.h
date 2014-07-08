/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:display.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Device-oriented display list operation interfaces.
 */

#ifndef __DISPLAY_H__
#define __DISPLAY_H__


#include "displayt.h"           /* typedefs for structs in this file */
#include "ndisplay.h"           /* NFILLOBJECT */
#include "dl_color.h"
#include "bitbltt.h"            /* FORM */
#include "dlstate.h"            /* DL_STATE */

#include "imaget.h"
#include "dl_storet.h"
#include "gs_color.h"           /* COLORSPACE_ID */
#include "gschcms.h"            /* REPRO_N_TYPES */
#include "gschead.h"            /* GSC_BLACK_KIND */
#include "pclAttribTypes.h"

struct core_init_fns ; /* from SWcore */
union surface_handle_t ;
struct NAMECACHE ;
struct Backdrop;
struct task_t; /* taskt.h */

/** \defgroup dl Device Oriented Display List.
 * \ingroup core
 */

/** \{ */

/** Display list clip structure. */
struct CLIPOBJECT {
  DlSSEntry storeEntry;    /**< Base class - must be first member. */

  dbbox_t bounds ;         /**< Device coordinate bounds of clip. */
  CLIPOBJECT *context ;    /**< Clip context chain pointers. */
  NFILLOBJECT *fill ;      /**< Fill associated with this clip. */
  uint16 ncomplex ;        /**< (Transitive) number of complex clips. */
  uint8 rule ;             /**< Fill rule used for this clip. */
  uint8 pad ;              /**< Padding to word boundary. */
  int32 clipno ;           /**< Clip ID, correlate of CLIPRECORD's. */
  int32 pagebasematrixid ; /**< Page base matrix ID. */
} ;

/* Old comment from the ScriptWorks 3.2 to 3.3 clip band re-work:

   For multiple clip band caching we will add a CLIPCACHE pointer to the
   structure above. The CLIPCACHE structure will be:

   typedef struct CLIPCACHE {
     FORM *form ;
     CLIPOBJECT *context ;
   } CLIPCACHE ;

   There will also be a pointers to these clip cache structures from the band
   allocation and rendering code. (These structures may be made into a list,
   so that there is a single-point external method of accessing them too.)
   When regenerating the clips, we check if the current CLIPOBJECT has a
   clipcache pointer; if it doesn't, we take a free one from our pool, or
   re-cycle an LRU list. If it does, we can look at the CLIPOBJECT in the
   clipcache structure; if this matches the CLIPOBJECT itself, then the FORM
   pointer is valid an can be re-used, otherwise we re-render into the form,
   and set the context pointer to the CLIPOBJECT which we just rendered.

   Something like that, anyway.
*/

struct pattern_nonoverlap_t {
  Bool enabled ; /* Check for overlapping tile edges and remove double paint */
  USERVALUE xx, xy, yx, yy ; /* Original (unoptimised) replication vectors */
} ;

/** Display list pattern state; this includes all of the instance independent
    bits which we need to render a pattern */
struct PATTERNOBJECT {
  DlSSEntry storeEntry;        /* Base class - must be first member. */
  USERVALUE xx, xy, yx, yy;    /* Replication vectors */
  USERVALUE bbx, bby;          /* Replication bbox corner */
  USERVALUE bsizex, bsizey;    /* Replication bbox size */
  int32 patternid;             /* pattern identifier; deliberately signed */
  int32 parent_patternid;
  int32 context_patternid;     /* For recursive patterns defined relative */
  uint8 painttype;             /* Pattern paint type (1 or 2) */
  uint8 tilingtype;            /* Tiling type */
  enum {
    TILING_NONE,               /* Not a tiling pattern */
    TILING_BLIT_LEVEL,         /* Faster when many tiles are required per band */
    TILING_HIGH_LEVEL          /* Faster when small part of one tile req per band */
  } tilingmethod;
  uint8 backdrop;              /* Needs backdrop rendering
                                   (i.e. transparency in sub-DL */
  TranAttrib *ta;              /* Transparency of the patterned obj */
  Bool patternDoesOverprint;   /* Do any objects in pattern DL overprint? */
  uint8 overprinting[2];       /* GSC_FILL, GSC_STROKE */
  int32 pageBaseMatrixId;      /* Pattern phase relative to page origin */
  uint32 groupid;              /* containing group */
  struct pattern_nonoverlap_t nonoverlap;  /* Check for overlapping tile edges
                                              and remove the double paint */
  uint8 opcode;                /* RENDER_void, RENDER_hdl or RENDER_group */
  union {
    HDL *hdl;
    Group *group;
  } dldata;                    /* The pattern paint proc DL */
  p_ncolor_t ncolor;           /* Uncolored pattern color, black for colored */
} ;

/** Display list softmask attributes. The softmask is very similar to the
    CLIPOBJECT - it is a complex object that can be associated with any number
    of listobjects. */
struct SoftMaskAttrib {
  DlSSEntry storeEntry; /* Base class - must be first member. */

  SoftMaskType type;
  Group* group;
};

/** Tag block for gstate info tags. */
typedef struct {
  struct NAMECACHE *block_name ;
  int32 type_num, data_words, data_woffset ;
} TAG_BLOCK_INFO ;

typedef struct {
  struct NAMECACHE *block_name , *tag_name ;
  int32 bit_offset, bit_width ;
  /* If the tag is an integer, bit_width will be in the range 1 .. 32.
   * If it's a string, 32 is added to it.
   */
} TAG_INFO ;

struct GSTAGSTRUCTUREOBJECT {
  DlSSEntry storeEntry; /* Base class - must be first member. */

  uint32 num_blocks, num_dl_blocks, num_tags, alloc_words, dl_words ;
  /* This field is extensible - there should be num_blocks blocks
   * followed by num_tags tags. */
  union {
    TAG_BLOCK_INFO block ;
    TAG_INFO tag ;
  } tags[1];
} ;

/** Display list transparency attributes; all of these properties were
    introduced in PDF 1.4, and all are related to transparency. */
struct TranAttrib {
  DlSSEntry storeEntry; /* Base class - must be first member. */

  COLORVALUE alpha;
  uint8 blendMode;
  uint8 alphaIsShape;
  SoftMaskAttrib* softMask;
};

/** Late colour management attributes; data required for the late colour
    management of objects and spans when compositing. Other information is
    derived from the gstate colorInfo context when compositing, which is in
    turn derived from the gstate pertaining in dlBeginPage. This arrangement
    gives Install & BeginPage hooks a chance to modify the gstate color for
    the benefit of late color management.

    origColorModel: often set to CMYK, RGB, Gray, or NamedColor, depending on
    the original object before compositing. In opaque regions, the meaning is
    obvious. In composited regions, we use a 'top-most object wins' rule. It's
    used for choosing the rendering intent override amongst other things.

    renderingIntent: set to the ICC rendering intent pertinent when the
    original object was painted. As above, we use a 'top-most object wins'
    rule for applying this. There is a set of 4 (N_ICC_RENDERING_INTENTS)
    values.

    overprintMode: required for recombine intercepted objects; if the job
    turns out to be composite, the intercepted objects require fixing to
    ensure correct overprinting.

    blackType: contains info to assist 100% black preservation. If an object
    was originally opaque and 100% black, we'll want to preserve that in the
    final output. Similarly if we're overprinting 100% black.

    independentChannels: true if the color chain in the current transparency
    group has independent channels. The color chain in the parent group may
    be affected by this knowledge.

 */
struct LateColorAttrib {
  DlSSEntry storeEntry; /* Base class - must be first member. */

  REPRO_COLOR_MODEL origColorModel;
  uint8 renderingIntent;
  uint8 overprintMode;
  GSC_BLACK_TYPE blackType;
  uint8 independentChannels;
#ifdef METRICS_BUILD
  uint8 is_icc ; /* Is it an ICC color model. */
#endif
};

/** Display list state object is shared between multiple objects which have
    the same clip state, pattern state, RLE screen and soft mask state.
    Also see stateobject_new() which initialises one of these structs. */
struct STATEOBJECT {
  DlSSEntry storeEntry; /* Base class - must be first member. */

  CLIPOBJECT *clipstate;                /* clip state pointer */
  PATTERNOBJECT *patternstate;          /* pattern state pointer */
  pattern_shape_t *patternshape;        /* pattern shapes and clipping */
  int32 spotno;                         /* spotno for rle screens */
  GSTAGSTRUCTUREOBJECT *gstagstructure; /* format for tag data in DL objects */
  TranAttrib *tranAttrib;               /* Transparency attributes */
  LateColorAttrib *lateColorAttrib;     /* Late colour management */
  PclAttrib *pclAttrib;                 /* PCL rendering attributes */
} ;

/** Initialises a STATEOBJECT to default values. */
STATEOBJECT stateobject_new(int32 spotno) ;

/** Bitflags for object \c spflags field. */
enum { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  RENDER_UNTRAPPED   = 0x01,
  RENDER_RECOMBINE   = 0x02,
  RENDER_PSEUDOERASE = 0x04,
  RENDER_KNOCKOUT    = 0x08,
  RENDER_BEGINPAGE   = 0x10,
  RENDER_ENDPAGE     = 0x20,
  RENDER_BACKGROUND  = 0x40,
  RENDER_PATTERN     = 0x80
} ;

/** Bitflags for object \c marker field. */
enum { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  MARKER_VN_VNCANDIDATE  = 0x01,  /**< Is object part of a vignette? */
  MARKER_VN_WHITEOBJECT  = 0x02,  /**< Is object white (e.g., 1.0 setgray) */
  MARKER_VN_FIXKNOCKOUT  = 0x04,
  MARKER_ONDISK          = 0x08,  /**< Has object been purged to disk? */
  MARKER_TRANSPARENT     = 0x10,  /**< Is object transparent? Object may still
                                       require compositing even if not set */
  MARKER_OMNITRANSPARENT = 0x20,  /**< Object is transparent all over;
                                       otherwise we can try per-block region
                                       map setting. */
  MARKER_DEVICECOLOR     = 0x40,  /**< Used to indicate dl color is in device
                                       color space. */
  MARKER_DL_FORALL       = 0x80   /**< Prevents duplication on dl walking. */
} ;


/* There is a nasty overloading of the repro types for erase objects, backdrop
   images and HDLs/Groups. Three additional values are tacked on at the end of
   the REPRO_TYPE enumeration for these (named REPRO_DISPOSITION_* to mitigate
   confusion with real repro types). They are mapped to standard repro types by
   a lookup table when a repro type is required. When rendering, they are used
   to determine the pixel label and render property mask. Groups, HDLs and
   backdrop images use REPRO_DISPOSITION_MIXED. The properties of HDL and group
   sub-objects, or the disposition flags for each colour table entry inside a
   backdrop image will be applied by the render method. Only one rendering
   property can be specified per object. */
enum {
  REPRO_DISPOSITION_ERASE = REPRO_N_TYPES,
  REPRO_DISPOSITION_RENDER, /* This and higher rendered on all seps */
  REPRO_DISPOSITION_MIXED, /* Must be <= DISPOSITION_REPRO_TYPE_MASK */
  REPRO_N_DISPOSITIONS
} ;


/* Masks and shifts to pack reproduction type, colour type, and flags
   into one byte. We are really tight for space, if this byte overflows,
   moving the repro type into the STATEOBJECT would probably be the best
   compromise.

   The plan of record is to separate the rendering properties from the repro
   types by representing the repro criteria as two bits (mapped to
   Saturation, Perceptual, Colorimetric and Relative Colorimetric). The
   rendering properties can then expand to three bits, containing Erase,
   Linework, Text, Vignette, Image and Multi. */

#define DISPOSITION_REPRO_TYPE_MASK    0x07u /* Repro type in bits 0-2 */
#define DISPOSITION_REPRO_TYPE_SHIFT       0
#define DISPOSITION_FLAG_SPARE1        0x08u /* Spare bit 3 */
#define DISPOSITION_FLAG_USER          0x10u /* user flag */
#define DISPOSITION_COLORTYPE_MASK     0xe0u /* Color type in bits 5-7 */
#define DISPOSITION_COLORTYPE_SHIFT        5

#define DISPOSITION_STORE(dest_, reproType_, colorType_, flags_) MACRO_START \
  uint8 _colorType_ ;                                                       \
  HQASSERT(((flags_) & ~DISPOSITION_FLAG_USER) == 0,                    \
           "Invalid disposition flags set");                            \
  HQASSERT((int)(reproType_) < (int)REPRO_N_DISPOSITIONS, "Repro type is not valid"); \
  HQASSERT((reproType_) <= (DISPOSITION_REPRO_TYPE_MASK >> DISPOSITION_REPRO_TYPE_SHIFT), \
           "Repro type overflows mask");                                \
  HQASSERT((colorType_) >= GSC_UNDEFINED && (colorType_) < GSC_N_COLOR_TYPES, \
           "Color type is not valid") ;                                 \
  _colorType_ = CAST_TO_UINT8((colorType_) - GSC_UNDEFINED) ;               \
  HQASSERT(_colorType_ <= (DISPOSITION_COLORTYPE_MASK >> DISPOSITION_COLORTYPE_SHIFT), \
           "Color type overflows mask") ;                                   \
  (dest_) = CAST_TO_UINT8(((reproType_) << DISPOSITION_REPRO_TYPE_SHIFT) |  \
                          (_colorType_ << DISPOSITION_COLORTYPE_SHIFT) |    \
                          (flags_)) ;                                       \
MACRO_END

/* Even though it is declared here, this table is actually in pixelLabels.c,
   with most of the other disposition code. */
extern const uint8 disposition_reproTypes[] ;

#define DISPOSITION_REPRO_TYPE_UNMAPPED(disposition_) \
  CAST_UNSIGNED_TO_UINT8(((disposition_) & DISPOSITION_REPRO_TYPE_MASK) \
                         >> DISPOSITION_REPRO_TYPE_SHIFT)
#define DISPOSITION_REPRO_TYPE(disposition_) \
  (disposition_reproTypes[DISPOSITION_REPRO_TYPE_UNMAPPED(disposition_)])

#define DISPOSITION_MUST_RENDER(disposition_) \
  (DISPOSITION_REPRO_TYPE_UNMAPPED(disposition_) >= REPRO_DISPOSITION_RENDER)

#define DISPOSITION_COLORTYPE(disposition_) \
  (CAST_UNSIGNED_TO_INT8(((disposition_) & DISPOSITION_COLORTYPE_MASK) \
                         >> DISPOSITION_COLORTYPE_SHIFT) \
   + GSC_UNDEFINED)

/** \brief Values and bits for rollover DL values.

    The DL object rollover field either takes the value \c DL_NO_ROLL, \c
    DL_ROLL_POSSIBLE, or is \c DL_ROLL_IT combined with the rollover ID.
    Rollover sequences are marked when adding to the DL. They will be scanned
    at render time, and if there are enough rollover objects in the sequence,
    they will be reversed and rendered through a self-intersection clip map.
    The rollover ID is used to determine if the sequence has been already
    tested for length, so we don't hurt performance by scanning objects
    repeatedly. */
enum {
  DL_NO_ROLL = 0,        /**< Object cannot possibly be in a rollover. */
  DL_ROLL_POSSIBLE = 1,  /**< Object may be the first in a rollover. */
  DL_ROLL_IT = 2,        /**< This bit set for rollover object sequences. */
  DL_ROLL_ID_INCR = 4    /**< Rollover IDs increment by this amount. */
} ;

/**
 * A single character glyph stored in the display list
 */
typedef struct DL_GLYPH
{
  dcoord x, y;
  FORM *form;
} DL_GLYPH;

/**
 * Set of character glyphs stored in the display list
 */
typedef struct DL_CHARS
{
  uint16 nchars, nalloc; /* number of chars used and number allocated */
  DL_GLYPH ch[1];        /* actually variable 'nchars' size */
} DL_CHARS;

/**
 * Display list object.
 *
 * OBJECT - type.  Distinguished by a function opcode & generic data ptr.
 * To render object, call function passing LISTOBJECT.
 *
 * rollover is used by the rollover code to mark rollover sequences.
 *
 * marker is used in multiple ways:
 * a) to store various vignette properties which are used for vignette
 *    splitting and merging.
 * b) to indicate the color values are device codes, as opposed to
 *    virtual device values.
 * c) when freeing the DL bit by bit, it is used as a single-hit flag.
 *
 * planes is used to hold the total recombined planes
 * (including fuzzy matches).
 * color is used to hold the color values. In RGB mode 3 values used, etc...
 */
struct LISTOBJECT {
  /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  STATEOBJECT *objectstate;

  uint8 opcode;       /* e.g. RENDER_void, RENDER_fill, ... */
  uint8 marker;       /* e.g. MARKER_TRANSPARENT, .... */
  uint8 spflags;      /* e.g. RENDER_UNTRAPPED, RENDER_RECOMBINE */
  uint8 disposition;  /* Rendering reproType, colorType, and text/lw flags */

  dbbox_t bbox;       /* intersection of object bbox and clip bbox */

  union {
    /* VOID, RECT   */
    /* ERASE        */  struct { uint8 newpage, with0; } erase;
    /* CHAR         */  DL_CHARS *text;
    /* QUAD1/2      */  uint32 quad;
    /* FILL         */  NFILLOBJECT *nfill;
    /* IMAGE, MASK  */  IMAGEOBJECT *image;
    /* VIGNETTE     */  VIGNETTEOBJECT *vignette;
    /* SHFILL       */  GOURAUDOBJECT *gouraud;
    /* SHFILL       */  SHADINGOBJECT *shade;
    /* SHFILL_PATCH */  rcbs_patch_t *patch;
    /* HDL          */  HDL *hdl;
    /* GROUP        */  Group *group;
    /* BACKDROP     */  struct Backdrop *backdrop;
    /* purged 2 disk */ size_t diskoffset;
    /* CELL         */  CELL *cell;
  } dldata;

  union {
    uintptr_t  rollover;
    p_ncolor_t planes;  /* colorants merged by fuzzy match (recombine only) */
  } attr; /* object attributes */

  p_ncolor_t p_ncolor;

#if defined(DEBUG_BUILD) && defined(DEBUG_TRACK_SETG)
  int32 setg_count ;
#endif
};

/**
 * Defines a range of objects in the Display List. Also stores current
 * position and direction so that it can be used to control and store state
 * for DL iteration.
 *
 * Range is defined to be from 'start' up to but not including 'end'.
 * This mean an end value of NULL can be used to mean iterate to the physical
 * end of the chain of DL objects.
 * Iteration occurs from start to end, unless 'forwards' if FALSE, in which
 * case it happens in the opposite direction.
 */
struct DLRANGE {
  struct {
    DLREF *dlref;        /**< Container holding DL object(s) */
    uint32 index;        /**< Object index if container holds multiple object */
  } start, end, current;
  Bool forwards;         /**< Direction of DL iteration */
  DLREF *backN;          /**< Used to speed-up backwards DL iteration */
  int32 sqrt_len;        /**< sqrt(num-of-dl-items) : used for backwards step */
  Bool common_render;    /**< All Dl objects share common render properties */
  Bool read_lobj;        /**< Has lobj been read from disk */
  LISTOBJECT lobj;       /**< Memory copy of DL obj if purged to disk */
  Bool writeBack;        /**< Memory copy has changed so write it back */
};

void dlrange_init(DLRANGE *dlrange);
void dlrange_start(DLRANGE *dlrange);
Bool dlrange_done(DLRANGE *dlrange);
void dlrange_next(DLRANGE *dlrange);
LISTOBJECT *dlrange_lobj(DLRANGE *dlrange);
void dlrange_setstart(DLRANGE *dlrange, DLRANGE *start);
void dlrange_setend(DLRANGE *dlrange, DLRANGE *end);

/**
 * API to manipulate references to the Display List
 * \todo BMJ 14-Oct-08 :  Work in progress - provide more natural API funcs
 */
LISTOBJECT *dlref_lobj(DLREF *dlref);
void dlref_assign(DLREF *dlref, LISTOBJECT *lobj);
DLREF *dlref_next(DLREF *dlref);
void dlref_setnext(DLREF *dlref, DLREF *next);

extern uint8 dl_currentexflags;
extern uint8 dl_currentdisposition;


/**
 * Bounding box of currently active clipping
 * \todo BMJ 30-Jun-08 :  Needs to be state stored somewhere, rather than a
 *                        global.
 */
extern dbbox_t cclip_bbox;

/* DL_Error should be the same as FALSE, just in case */
enum { DL_Error, DL_Added, DL_Merged, DL_NotAdded } ;

void dl_pool_C_globals(struct core_init_fns *fns);
void dl_misc_C_globals(struct core_init_fns *fns);

void *dl_alloc(const mm_pool_t pools[N_DL_POOLS], size_t bytes,
               uint32 allocclass);
void dl_free(const mm_pool_t pools[N_DL_POOLS], void *addr, size_t size,
             uint32 allocclass);
void *dl_alloc_with_header(const mm_pool_t pools[N_DL_POOLS], size_t bytes,
                           uint32 allocclass);
void dl_free_with_header(const mm_pool_t pools[N_DL_POOLS], void *addr,
                         uint32 allocclass);
mm_pool_t dl_choosepool(const mm_pool_t pools[N_DL_POOLS], uint32 allocclass);

size_t dl_mem_used(DL_STATE *page);

DLREF *alloc_n_dlrefs(size_t n, DL_STATE *page);
void free_n_dlrefs(DLREF *head, int32 n, mm_pool_t dlpools[N_DL_POOLS]);

int32 addobjecttonodl(DL_STATE *page, LISTOBJECT *lobj);
int32 addobjecttoerrordl(DL_STATE *page, LISTOBJECT *lobj);
int32 addobjecttopatterndl(DL_STATE *page, LISTOBJECT *lobj);
int32 addobjecttopagedl(DL_STATE *page, LISTOBJECT *lobj);

Bool make_listobject(DL_STATE *page, int32 opcode, dbbox_t *bbox,
                     LISTOBJECT **plobj);
Bool make_listobject_copy(DL_STATE *page, LISTOBJECT *source,
                          LISTOBJECT **plobj);
void free_listobject(LISTOBJECT *lobj, DL_STATE *page);
Bool add_listobject(DL_STATE *page, LISTOBJECT *lobj, Bool *added);
void init_listobject(LISTOBJECT *lobj, int32 opcode, dbbox_t *bbox);
Bool clip2cclipbbox(dbbox_t *bbox);

#define DL_LASTBAND    (-1)

Bool dlSetPageGroup(DL_STATE *page, OBJECT colorspace, Bool knockout);

Bool update_erase(DL_STATE *page);

Bool dlskip_pseudo_erasepage(DL_STATE *page);

Bool dlreset_imposition(DL_STATE *page);

void dlreset_recombine(DL_STATE *page);

Bool displaylistisempty(DL_STATE *page);

int32 displaylistfreeslots(void) ;

Bool adderasedisplay(DL_STATE *page, Bool newPage);

Bool addchardisplay(
  /*@notnull@*/ /*@in@*/                  DL_STATE* page,
  /*@notnull@*/ /*@only@*/ /*@in@*/       FORM *theform ,
                                          dcoord x1,
                                          dcoord y1) ;

Bool finishaddchardisplay(DL_STATE *page, int32 newlength) ;

Bool addrectdisplay(
  /*@notnull@*/ /*@in@*/                  DL_STATE* page,
  /*@notnull@*/ /*@in@*/                  dbbox_t *therect ) ;

Bool addimagedisplay(
  /*@notnull@*/ /*@in@*/                  DL_STATE* page,
  /*@notnull@*/ /*@only@*/ /*@in@*/       IMAGEOBJECT *theimage ,
                                          int32 imagetype ) ;

extern int dl_safe_recursion ;

/** The number of display lists in processing (interpretation, rendering, and
    any stage in between) at any one time. */
extern int32 dl_pipeline_depth ;

/*@dependent@*/
HDL *dlPageHDL(/*@notnull@*/ /*@in@*/ const DL_STATE *page) ;

Bool dlIsEmpty(/*@notnull@*/ /*@in@*/ const DL_STATE *page ) ;

void dlVirtualDeviceSpace( /*@in@*/ const DL_STATE *page, int32 *name_id,
                           COLORSPACE_ID *spaceId ) ;

Bool dlAllowPartialPaint( /*@in@*/ const DL_STATE *page ) ;

Bool dlSignificantObjectsToRip( /*@notnull@*/ /*@in@*/ const DL_STATE *page ) ;

Bool bandSignificantObjectsToRip(/*@notnull@*/ /*@in@*/ const DL_STATE *page,
                                 int32 band);

Bool dlAddingObject(const DL_STATE *page, Bool isErase);

/* DL query methods */

/**
 * Get the head of the DL for the given page and band
 */
DLREF *dl_get_head(const DL_STATE *page, int32 bandi);

/**
 * Get the z-order DL for the given page
 */
DLREF *dl_get_orderlist(const DL_STATE *page);

Bool tranAttribIsOpaque(TranAttrib *ta);

Bool tranAttribEqual(TranAttrib *ata, TranAttrib *bta);

struct Backdrop *tranAttribSoftMaskBackdrop(TranAttrib *ta);


/** Determine if this object is transparent.  This is not a complete test
 * because images and dl colors with an alpha channel are handled elsewhere
 * (addimagedisplay and dlc_to_lobj/dlc_to_lobj_release respectively).
 */
Bool stateobject_transparent(STATEOBJECT *objectstate);


Bool dlRandomAccess(void);

Bool dlPrepareBanding(DL_STATE *page);

/** \brief Ensure that a mask form will be available for rendering the DL.

    \param page  The DL in which to reserve the band
    \param bits  A bit mask of the RESERVE_BAND_* values.

    \retval TRUE  If the band was reserved successfully.
    \retval FALSE If the band was not reserved successfully.

    The mask or map band(s) indicated by the bitmask will be added to the
    reserved set for the display list. This may result in minimum resources
    being allocated. Bands can only be reserved while the display list is
    still being constructed. */
Bool dl_reserve_band(DL_STATE *page, uint8 bits) ;

/** \brief Replace the DL pending task with this task.

    \param page      The DL in which to replace the pending task.
    \param[in,out] task  A pointer to a new pending task. This task must be in
                     constructing state.

    \retval TRUE     If the replacement was made successfully.
    \retval FALSE    If the replacement could not be made. \a task will have
                     been cancelled, and the existing task is left on the
                     DL.

    The existing DL pending task (if any) is made ready, after a dependency
    is made to it from the new task. Ownership of the reference passed in to
    this function is always transferred to the DL. On exit, the task pointer
    is cleared, regardless of outcome.
*/
Bool dl_pending_task(DL_STATE *page, struct task_t **task) ;

/** \brief Add a task to destroy a DL.

    \param page      The DL to add the erase task to.

    \retval FALSE  The erase task could not be created

    An erase task and task group are created to asynchronously join the
    page's task group, then clean up the page allocations. If the erase task
    could not be added successfully, the previous tasks are cancelled and
    joined immediately, then the erase task is performed synchronously. The
    outcome of the DL tasks preceding the erase may be picked up by looking
    at the error status of the page task group. */
Bool dl_pending_erase(DL_STATE *page) ;

/** \brief Prepare a page for rendering.

    \param nextpage  A pointer to store the next inputpage.
    \param erase_type  What's going to be done to the page.

    \retval TRUE   The handoff succeeded. If the remaining DL preparation
                   succeeds, \c inputpage should be set to \c next. If the
                   remaining DL preparation fails, dl_handoff_undo() should
                   be called.
    \retval FALSE  The handoff failed. The new page cannot be used.
*/
Bool dl_handoff_prepare(/*@notnull@*/ /*@out@*/ DL_STATE **nextpage,
                        dl_erase_t erase_type) ;

/** \brief Commit or undo a page handoff.

    \param nextpage The partially-prepared inputpage.
    \param erase_type  What's going to be done to the page.
    \param success     A boolean, indicating if the handoff was successful.
*/
void dl_handoff_commit(/*@notnull@*/ /*@in@*/ DL_STATE *nextpage,
                       dl_erase_t erase_type, Bool success) ;

/** \brief Flush display lists through the pipeline.

    \param depth  The pipeline depth. This is the number of display lists
                  in flight after this function, including \c inputpage. A
                  depth of 0 means that inputpage should be flushed and erased
                  too.
    \param no_messages
                  TRUE if any messages from asynchronous render errors should
                  be suppressed. FALSE if error message should be sent to the
                  job log.

    \retval TRUE  If the flush was successful.
    \retval FALSE If an error happened whilst flushing the pipeline.

    This function will flush the display list pipeline to ensure that there
    are only \c depth display lists in use, including inputpage. This routine
    will always flush the pipeline to the depth specified, regardless of
    whether any errors were encountered.
*/
Bool dl_pipeline_flush(int32 depth, Bool no_messages) ;

/** \brief Make a DL page ready for interpretation.

    \param page     The display list that's being created.

    \retval TRUE  The new page was initialised correctly.
    \retval FALSE The new page was not initialised correctly.

    The action this function takes depends on the erase type stored in the
    page:

    * DL_ERASE_ALL means that a new input page is being created following a
      handoff for asynchronous rendering. The pagedevice fields in the DL
      state will have been set, the erase fields in the DL state will have
      been reset, all other fields need to be initialised.
    * DL_ERASE_CLEAR means that the previous input page is being reused, after
      all DL objects have been removed from it. The pagedevice fields in the DL
      state will have been set, the erase fields in the DL state will have
      been reset, all other fields need to be initialised.
    * DL_ERASE_PARTIAL means that the previous input page is being reused, but
      there is still a partial display list. The pagedevice fields in the DL
      state will have been set, the erase fields in the DL state will have
      been reset if appropriate, all other fields need to be initialised
      appropriately.
    * DL_ERASE_COPYPAGE means that the previous input page is being reused, but
      there is still a full display list. The pagedevice fields in the DL
      state will have been set, the erase fields in the DL state will have
      been reset if appropriate, all other fields need to be initialised
      appropriately.

   This function is always called before interpreting a page, or continuing
   a partial painted page.
 */
Bool dl_begin_page(/*@notnull@*/ /*@in@*/ DL_STATE *page) ;

/** \brief Explicitly erase a DL page.

    \param page        The display list that's being erased.

    \note This function is \b not nested with respect to \c dl_begin_page().
    It is called when explicitly erasing a page, either through erasepage, or
    through setpagedevice. Pages that are handed off for asynchronous rendering
    do not go through this function when being destroyed.
*/
void dl_clear_page(/*@notnull@*/ /*@in@*/ DL_STATE *page) ;

/** \brief Destroy the color state used for the back-end color transforms.

    \param page        The display list that's being erased.
*/
void dl_color_state_destroy(/*@notnull@*/ /*@in@*/ DL_STATE *page);

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)

enum {
  DEBUG_DL_VERBOSITY = 0xf,     /**< Bottom 4 bits used for verbosity level. */
  DEBUG_DL_PRECONVERT_VERBOSITY = 0xf0, /**< Next 4 bits used for preconverted verbosity level. */
  DEBUG_DL_TRACKDL = 0x100,     /**< Track dl allocs and frees. */
  DEBUG_DL_PIPELINE_SINGLE = 0x1000, /**< Force pipelining with one thread. */
  DEBUG_DL_PIPELINE_JOBS = 0x2000, /**< Don't flush at job boundaries. */
};

extern int32 debug_dl;

void track_dl(size_t bytes, uint32 allocclass, Bool alloc);
void report_track_dl(char *title, size_t size);

Bool debug_dl_skipimage(int32 optimise) ;
Bool debug_dl_skipsetg(void) ;

#else /* !DEBUG_BUILD && !ASSERT_BUILD */
#define track_dl(bytes, allocclass, alloc) EMPTY_STATEMENT()
#define report_track_dl(title, size) EMPTY_STATEMENT()
#define debug_dl_skipimage(optimise) EMPTY_STATEMENT()
#define debug_dl_skipsetg() EMPTY_STATEMENT()
#endif /* !DEBUG_BUILD && !ASSERT_BUILD */

/** \} */

#endif /* protection for multiple inclusion */

/*
Log stripped */
