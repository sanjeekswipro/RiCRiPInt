/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!src:patternshape.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * pattern_shape_t stores the pattern shape mask for recursive patterns, and
 * the key cell clip for all patterns.
 */

#include "core.h"
#include "formOps.h"
#include "swerrors.h"
#include "mps.h"
#include "dl_storet.h"
#include "display.h"
#include "objnamer.h"
#include "bitblts.h"
#include "dl_store.h"
#include "dl_bbox.h"
#include "basemap.h"
#include "patternrender.h"
#include "rlecache.h"
#include "hdlPrivate.h"
#include "dl_foral.h"
#include "toneblt.h"
#include "render.h"
#include "spanlist.h"
#include "namedef_.h"
#include "hqmemcpy.h"
#include "debugging.h"
#include "pattern.h"
#include "patternshape.h"
#include "bitblth.h"
#include "jobmetrics.h"
#include "bandtable.h" /* assign_reserved_bands_to_forms */
#include "surface.h"
#include "gu_chan.h"
#include "blitcolors.h"
#include "blitcolorh.h"

#define PATTERNSHAPE_NAME "PatternShape"

#define SHAPEID_INVALID MAXUINT32
#define SHAPEID_INIT    0 /* shapeid is pre-incremented, so the
                             first will actually be SHAPEID_INIT + 1 */
uint32 shapeid = SHAPEID_INIT ;

typedef struct augmented_mask_t augmented_mask_t ;

/** A pattern_shape_t represents the pattern shape mask or clip shape mask
   of a DL.  Pattern shapes hang off the STATEOBJECT alongside the
   PATTERNOBJECT.  A list of objects with the same PATTERNOBJECT may refer
   to different pattern shapes (this is determined by
   patternshape_lookup).  The pattern shapes for the outermost pattern are
   generated during rendering and are not stored with the DL object. */
struct pattern_shape_t {
  DlSSEntry storeEntry ; /** Base class - must be first member. */

  OBJECT_NAME_MEMBER
  int32 id ; /** The unique id for a pattern shape. */

  /** ids/ids_len is the list of pattern shape ids of this pattern shape and
     its ancestors.  This is required for augmented masks only. */
  int32 *ids ;
  uint32 ids_len ;

  /** The pattern id of the pattern DL which we're representing in the shape mask. */
  int32 patternid ;

  /** patternshape_finish has been done (might result in an empty mask). */
  Bool finished ;

  /** bbox is the union of all the DL object's bboxes, and the area covered
     by the mask. */
  dbbox_t bbox ;
  form_array_t *mask ; /** mask representing the pattern/clip shapes. */

  /** augmented_masks are additional masks required for recursive patterns
     which are not defined in their immediate parent.  This only happens for
     PS (not PDF, XPS). */
  augmented_mask_t *augmented_masks ;
} ;

/** Recursive patterns that are not defined in the context of their
  immediate parent pattern require their masks to be 'augmented'.  As an
  example consider the three recursive patterns below.  Pattern 1 is the
  outer parent pattern and pattern 3 is the innermost child pattern:-

  [ Pattern3 ]         P3 defined in P1
    |
    |   [ Pattern2 ]   P2 defined in P1
    |     |
    V     V
  [ Pattern1 ]         P1 defined in base context

  At render time, the DL contained in pattern 3 is replicated according to
  pattern 3's definition and the spans are then passed down to the next
  level.  These spans are then clipped to a mask containing the pattern
  shapes of pattern 1 ANDed with the results of rendering pattern 2's
  pattern shapes replicated according to pattern 2's definition.  The new
  mask generated, which is a combination of multiple patterns' shapes, is
  what I refer to as the 'augmented' mask.  Only in PS can patterns be
  defined in a context other than a pattern's immediate parent. */
struct augmented_mask_t {
  /** ids/ids_len is the list pattern shape ids of the pattern stack for
     which this augmented mask was created.  It may not necessarily be the
     same as the ids stored in pattern_shape_t. */
  int32 *ids ;
  uint32 ids_len ;

  form_array_t *mask ; /** The augmented mask for a given pattern stack. */

  augmented_mask_t *next ; /** There may be multiple augmented masks. */
} ;

/* Most recently used pattern shape and its HDL context (for patternshape_lookup). */
pattern_shape_t *mru_shape = NULL ;
uint32 mru_hdl_id = 0 ; /* arbitrary initialisation value */

#if defined( DEBUG_BUILD )
enum {
  PATTERNSHAPE_DEBUG_TRACE = 0x1, /* print one line of useful info for each shape */
  PATTERNSHAPE_DEBUG_SOLIDSHAPES = 0x2, /* make all pattern shapes solid */
  PATTERNSHAPE_DEBUG_SPANS = 0xc /* Normal/Always/Never/Random spanlist use */
} ;

uint32 debug_patternshape = 0 ;

void debug_patternshape_formarray(uint32 id, form_array_t *formarray, char *use) ;

static uint32 patternshape_debug_seed = 0 ;

Bool patternshape_use_spanlist(Bool condition)
{
  switch ( debug_patternshape & PATTERNSHAPE_DEBUG_SPANS ) {
  default:
  case 0: /* Normal, use spans if smaller */
    return condition ;
  case 0x4: /* Force spans */
    return TRUE ;
  case 0x8: /* Never use spans */
    return FALSE ;
  case 0xc: /* Randomly use spans. PNRG has 2^32 modulus. */
    patternshape_debug_seed = patternshape_debug_seed * 1664525ul + 1013904223ul ;
    return (patternshape_debug_seed & 1) != 0 ;
  }
}
#else
#define debug_patternshape_formarray(_id, _formarray, _use) EMPTY_STATEMENT()
#define patternshape_use_spanlist(cond_) (cond_)
#endif

void init_C_globals_patternshape(void)
{
  shapeid = SHAPEID_INIT ;
  mru_shape = NULL ;
  mru_hdl_id = 0 ;
#if defined( DEBUG_BUILD )
  debug_patternshape = 0 ;
  patternshape_debug_seed = 0 ;
#endif
}

/** This render state is used for generating pattern shapes. */
static void render_state_patternshape(render_state_t *rs,
                                      int pattern_state, DL_STATE *page)
{
  /** \todo ajcd 2011-03-27: This won't work when we try to build
      patternshapes asynchronously. We'll need separate erase colors and
      blitmaps for each pattern shape construction. */
  static blit_colormap_t blitmap ;
  static blit_color_t erase_color ;
  static blit_color_t knockout_color ;
  static Bool dummy_white_on_white = FALSE ;

  HQASSERT(rs, "Nowhere for raster state") ;
  HQASSERT(page, "No DL page") ;
  HQASSERT(!dummy_white_on_white, "White on white set to TRUE") ;

  HQASSERT(rs->forms != NULL, "forms should be set already") ;
  init_forms(rs->forms, page, FALSE /*!doing_mht*/) ;
  rs->page = page ;
  rs->hr = page->hr ;
  rs->hf = gucr_framesStart( rs->hr ) ;
  rs->nChannel = 0 ;
  rs->lobjErase = NULL;
  rs->composite_context = NULL ;
  rs->backdrop = NULL ;
  rs->band = DL_LASTBAND ;
  rs->pass_region_types = RENDER_REGIONS_NONE;
  rs->fPixelInterleaved = TRUE ;
  rs->maxmode = BLT_MAX_NONE;
  rs->fMergePatterns = TRUE ;
  /* We only want to regenerate the pattern shapes at this level, so
     switch off patterns to avoid going into begin_pattern/end_pattern. */
  rs->dopatterns = FALSE ;
  rs->htm_info = NULL ;
  rs->surface_handle.page = NULL ;
  dlc_clear(&rs->dlc_watermark) ;
  rs->spotno_watermark = SPOT_NO_INVALID ;
  rs->trap_backdrop_context = NULL ;

  rs->cs.overprint_mask = NULL ;
  rs->cs.maxblit_mask = NULL ;
  blit_colormap_mask(&blitmap) ;
  rs->cs.blitmap = &blitmap ;
  blit_color_init(&erase_color, &blitmap) ;
  blit_color_mask(&erase_color, TRUE /*white*/) ;
  rs->cs.erase_color = &erase_color ;
  blit_color_init(&knockout_color, &blitmap) ;
  blit_color_mask(&knockout_color, TRUE /*white*/) ;
  rs->cs.knockout_color = &knockout_color ;
  rs->cs.renderTracker = NULL ;
  rs->cs.hc = NULL ;
  rs->cs.p_white_on_white = &dummy_white_on_white ;
  bbox_store(&rs->cs.bandlimits, 0, 0, page->page_w - 1, page->band_lines - 1) ;
  rs->ri.surface = &patternshape_surface ;
  HQASSERT(rs->ri.surface != NULL, "No output surface") ;
  rs->cs.fIsHalftone = rs->ri.surface->screened ;
  rs->cs.bg_separation = FALSE ;
  rs->cs.fSelfIntersect = FALSE;
  rs->cs.selected_mht = NULL ;
  rs->cs.selected_mht_is_erase = FALSE ;

  rs->ri.p_rs = rs ;
  rs->ri.lobj = NULL ;
  rs->ri.pattern_state = pattern_state ;
  rs->ri.clip = rs->cs.bandlimits ;
  rs->ri.x1maskclip = MAXDCOORD ;
  rs->ri.x1maskclip = MINDCOORD ;
  rs->ri.region_set_type = RENDER_REGIONS_DIRECT;
  rs->ri.fSoftMaskInPattern = FALSE ;
  rs->ri.overrideColorType = GSC_UNDEFINED ;
  rs->ri.bounds = rs->cs.bandlimits ;
  rs->ri.ht_params = NULL ;
  rs->ri.generate_object_map = FALSE;
  rs->ri.group_tracker = NULL ;

  rs->ri.rb.p_ri = &rs->ri ;
  rs->ri.rb.outputform = &rs->forms->patternshapeform ;
  rs->ri.rb.clipform = &rs->forms->clippingform ;
  rs->ri.rb.ylineaddr = NULL ;
  rs->ri.rb.ymaskaddr = NULL ;
  rs->ri.rb.color = rs->cs.erase_color ;
  rs->ri.rb.x_sep_position = 0 ;
  rs->ri.rb.y_sep_position = 0 ;
  rs->ri.rb.p_painting_pattern = NULL ;
  rs->ri.rb.clipmode = BLT_CLP_RECT;
  rs->ri.rb.depth_shift = 0 ;
  rs->ri.rb.maxmode = BLT_MAX_NONE ;
  rs->ri.rb.opmode = BLT_OVP_NONE ;
  RESET_BLITS(rs->ri.rb.blits, &invalid_slice, &invalid_slice, &invalid_slice) ;

  HQASSERT(RENDER_STATE_CONSISTENT(rs), "New patternshape state inconsistent") ;
}


DlSSEntry *patternshape_copy(DlSSEntry *entry, mm_pool_t *pools)
{
  UNUSED_PARAM(DlSSEntry*, entry) ;
  UNUSED_PARAM(mm_pool_t *, pools);

  HQFAIL("copy should never be called - always insert directly instead") ;

  (void)error_handler(UNREGISTERED) ;
  return NULL ;
}

void patternshape_delete(DlSSEntry* entry, mm_pool_t *pools)
{
  pattern_shape_t *shape = (pattern_shape_t*)entry ;

  if ( shape ) {
    VERIFY_OBJECT(shape, PATTERNSHAPE_NAME) ;

    if ( shape->mask )
      formarray_destroy(&shape->mask, pools);

    while ( shape->augmented_masks ) {
      augmented_mask_t *augmented = shape->augmented_masks ;
      if ( augmented->mask )
        formarray_destroy(&augmented->mask, pools);
      dl_free(pools, augmented, sizeof(augmented_mask_t) + sizeof(int32) *
              augmented->ids_len, MM_ALLOC_CLASS_PATTERN_SHAPE);
    }

    if ( shape->ids )
      dl_free(pools, shape->ids, sizeof(int32) * shape->ids_len,
              MM_ALLOC_CLASS_PATTERN_SHAPE);
    dl_free(pools, shape, sizeof(pattern_shape_t),
            MM_ALLOC_CLASS_PATTERN_SHAPE);
  }
}

uintptr_t patternshape_hash(DlSSEntry* entry)
{
  pattern_shape_t *shape = (pattern_shape_t*)entry ;

  HQASSERT(shape, "pattern shape null") ;
  VERIFY_OBJECT(shape, PATTERNSHAPE_NAME) ;

  return (uintptr_t)shape->id ;
}

Bool patternshape_same(DlSSEntry *entryA, DlSSEntry *entryB)
{
  pattern_shape_t *shapeA = (pattern_shape_t*)entryA ;
  pattern_shape_t *shapeB = (pattern_shape_t*)entryB ;

  HQASSERT(shapeA, "pattern shapeA null") ;
  HQASSERT(shapeB, "pattern shapeB null") ;
  VERIFY_OBJECT(shapeA, PATTERNSHAPE_NAME) ;
  VERIFY_OBJECT(shapeB, PATTERNSHAPE_NAME) ;

  return shapeA->id == shapeB->id ;
}

static Bool patternshape_new(pattern_shape_t **new_shape, DL_STATE *page)
{
  pattern_shape_t *shape ;

  *new_shape = NULL ;

  if ( ++shapeid == SHAPEID_INVALID ) {
    HQFAIL("pattern shape id appears to have wrapped round a uint32") ;
    shapeid = SHAPEID_INIT ; /* otherwise the user would have to restart the rip */
    return error_handler(UNDEFINEDRESULT) ;
  }

  shape = dl_alloc(page->dlpools, sizeof(pattern_shape_t),
                   MM_ALLOC_CLASS_PATTERN_SHAPE) ;
  if ( !shape )
    return error_handler(VMERROR) ;

  /* Default values only please (except shape id);
     anything else should be set in the caller. */
  NAME_OBJECT(shape, PATTERNSHAPE_NAME) ;
  shape->id = shapeid ;
  shape->ids = NULL ;
  shape->ids_len = 0 ;
  shape->patternid = INVALID_PATTERN_ID ;
  shape->finished = FALSE ;
  bbox_store(&shape->bbox, MAXDCOORD, MAXDCOORD, MINDCOORD, MINDCOORD) ;
  shape->mask = NULL ;
  shape->augmented_masks = NULL ;

  *new_shape = shape ;

  return TRUE ;
}

/** patternshape_lookup creates a pattern_shape_t to place in the DL
   object's STATEOBJECT.  Initially the mask is empty and the mask is only
   made at the end of creating the pattern DL.  Note: this routine only
   looks at the pattern shape in the current dl state; do not need to hash
   and search through the pattern shape store as the only chance of a hit
   lies with the pattern shape in the current dl state. */
Bool patternshape_lookup(DL_STATE *page,
                         STATEOBJECT *currentstate, STATEOBJECT* newstate)
{
  pattern_shape_t *shape ;
  int32 patternid ;

  newstate->patternshape = NULL ;

  if ( !pattern_executingpaintproc(&patternid) )
    return TRUE ;

  /* Check if we're inside the same pattern paint proc as before.  The current
     shape can only be reused if we are in the same HDL; pattern shapes are not
     allowed to be in multiple HDLs (this rule simplifies
     patternshape_finishcallback). */
  if ( mru_shape &&
       currentstate &&
       currentstate->patternshape &&
       mru_shape->id == currentstate->patternshape->id &&
       mru_hdl_id == hdlId(page->currentHdl) &&
       currentstate->patternshape->patternid == patternid ) {
    PATTERNOBJECT *currentpattern = currentstate->patternstate ;
    PATTERNOBJECT *newpattern = newstate->patternstate ;

    VERIFY_OBJECT(currentstate->patternshape, PATTERNSHAPE_NAME) ;

    /* If the current and new objects refer to the same pattern,
       then the pattern shapes can be coalesced. */
    if ( currentpattern && newpattern &&
         currentpattern->patternid == newpattern->patternid ) {
      newstate->patternshape = currentstate->patternshape ;
      return TRUE ;
    }

    /* If neither the current nor the new objects refer to a pattern, the
       clip shapes can be coalesced but only if they share the same clipping. */
    if ( !currentpattern && !newpattern &&
         currentstate->clipstate == newstate->clipstate ) {
      newstate->patternshape = currentstate->patternshape ;
      return TRUE ;
    }
  }

  if ( !patternshape_new(&shape, page) )
    return FALSE ;

  shape = (pattern_shape_t*)dlSSInsert(page->stores.patternshape,
                                       &shape->storeEntry, FALSE) ;
#ifdef METRICS_BUILD
  dl_metrics()->store.patternshapeCount++;
#endif

  if ( !shape ) {
    patternshape_delete(&shape->storeEntry, page->dlpools);
    return FALSE ;
  }

  shape->patternid = patternid ;

  newstate->patternshape = shape ;

  /* Track the most recently used shape and the containing HDL. */
  mru_shape = shape ;
  mru_hdl_id = hdlId(page->currentHdl);

  return TRUE ;
}

/** Helper blit function to create spanlist. */
static void patternshape_write_spanlist(render_blit_t *rb, dcoord y,
                                        dcoord xs, dcoord xe)
{
  UNUSED_PARAM(dcoord, y) ;
  (void)spanlist_insert((spanlist_t *)rb->ylineaddr, xs, xe) ;
}

/** patternshape_write stores a rectangular sub-section of srcform into
   dstform.  It chooses bitmap or RLE based on whichever results in the more
   compact encoding. */
static Bool patternshape_write(DL_STATE *page, const FORM *srcform, FORM *dstform,
                               dbbox_t *bbox, dcoord y1, dcoord y2)
{
  dcoord x_offset, y_offset, w, h, i ;
  blit_t *src, *dst ;
  uint32 nspans ;
  int32 linebytes ;

#if defined( DEBUG_BUILD )
  if ( (debug_patternshape & PATTERNSHAPE_DEBUG_SOLIDSHAPES) ) {
    theFormT(*(FORM *)srcform) = FORMTYPE_BANDBITMAP ;
    area1fill((FORM *)srcform) ;
  }
#endif

  HQASSERT(theFormT(*dstform) == FORMTYPE_BLANK, "dstform must be blank") ;
  HQASSERT((bbox->x1 & BLIT_MASK_BITS) == 0, "x1 must be a blit_t aligned") ;

  y_offset = 0 ;
  if ( bbox->y1 > y1 )
    y_offset = bbox->y1 - y1 ;

  w = bbox->x2 - bbox->x1 + 1 ;
  h = y2 - y1 + 1 ;
  h -= y_offset ;
  if ( bbox->y2 < y2 )
    h -= y2 - bbox->y2 ;

  x_offset = 0 ;
  if ( theFormT(*srcform) == FORMTYPE_BANDBITMAP )
    x_offset = BLIT_OFFSET(bbox->x1) ;

  src = BLIT_ADDRESS(theFormA(*srcform), y_offset * theFormL(*srcform) + x_offset) ;

  if ( theFormT(*srcform) == FORMTYPE_BANDBITMAP ) {
    for ( nspans = 0, i = 0 ; i < h ; ++i ) {
      uint32 nspans_temp = spanlist_bitmap_spans(src, w) ;
      nspans = max(nspans, nspans_temp) ;
      src = BLIT_ADDRESS(src, theFormL(*srcform)) ;
    }
  } else {
    HQASSERT(theFormT(*srcform) == FORMTYPE_BANDRLEENCODED,
             "Source form is neither bitmap nor spanlist encoded") ;

    for ( nspans = 0, i = 0 ; i < h ; ++i ) {
      /** \todo spanlist_count is not minimal in the intersection of the
          source and (bbox->x1,bbox->x2), but it is fast. We might want to
          make this more accurate by actually counting intersecting spans. */
      uint32 nspans_temp = spanlist_count((spanlist_t *)src) ;
      nspans = max(nspans, nspans_temp) ;
      src = BLIT_ADDRESS(src, theFormL(*srcform)) ;
    }
  }

  if ( nspans == 0 ) {
    /* This form is completely blank, nothing more to do. */
    HQASSERT(theFormT(*dstform) == FORMTYPE_BLANK, "dstform should be set to blank") ;
    return TRUE ;
  }

  linebytes = BLIT_ALIGN_SIZE(spanlist_size(nspans)) ;

  if ( patternshape_use_spanlist(CAST_SIGNED_TO_UINT32(linebytes) < FORM_LINE_BYTES(w)) ) {
    /* Generating the destination form as RLE. */
    if ( !formarray_newform(dstform, page->dlpools,
                            FORMTYPE_BANDRLEENCODED, linebytes) )
      return FALSE ;

    src = BLIT_ADDRESS(theFormA(*srcform), y_offset * theFormL(*srcform) + x_offset) ;
    dst = theFormA(*dstform) ;

    if ( theFormT(*srcform) == FORMTYPE_BANDBITMAP ) {
      for ( i = 0 ; i < h ; ++i ) {
        spanlist_t *spanlist = spanlist_init(dst, theFormL(*dstform)) ;

        /* Use of dstform is deliberate in this calculation, it's working out
           if the copy (of length theFormL(*dstform)) will read too much
           data. */
        HQASSERT(src >= theFormA(*srcform) &&
                 BLIT_ADDRESS(src, theFormL(*dstform) /*sic*/)
                 <= BLIT_ADDRESS(theFormA(*srcform), theFormS(*srcform)),
                 "Trying to read outside of srcform") ;
        HQASSERT(dst >= theFormA(*dstform) &&
                 BLIT_ADDRESS(dst, theFormL(*dstform))
                 <= BLIT_ADDRESS(theFormA(*dstform), theFormS(*dstform)),
                 "Trying to write outside of dstform") ;

        if ( !spanlist || !spanlist_from_bitmap(spanlist, src, w) ) {
          HQFAIL("Unexpectedly, RLE won't fit, so I'll default to bitmap") ;
          break ;
        }

        src = BLIT_ADDRESS(src, theFormL(*srcform)) ;
        dst = BLIT_ADDRESS(dst, theFormL(*dstform)) ;
      }
    } else {
      render_state_t rs_mask ;
      blit_chain_t mask_blits ;
      render_forms_t mask_forms ;

      HQASSERT(theFormT(*srcform) == FORMTYPE_BANDRLEENCODED,
               "Source form is neither bitmap nor spanlist encoded") ;

      render_state_mask(&rs_mask, &mask_blits, &mask_forms, &invalid_surface,
                        dstform) ;

      rs_mask.ri.rb.ylineaddr = dst ;

      /* The clip box is set to the input coordinate space used by bitfill0/1,
         which excludes x_sep_position. spanlist_intersecting doesn't use the
         clip box, but the bitfills will assert it. */
      bbox_store(&rs_mask.ri.clip, 0, 0, w - 1, h - 1) ;

      /* Both source and dest are spanlists. We want to get all of the spans
         intersecting the x1,x2 of the source, but shifted to 0,w-1. */
      for ( i = 0 ; i < h ; ++i ) {
        if ( spanlist_init(rs_mask.ri.rb.ylineaddr, theFormL(*dstform)) == NULL ) {
          HQFAIL("Failed to initialise spanlist") ;
        }
        spanlist_intersecting((spanlist_t *)src,
                              patternshape_write_spanlist, NULL,
                              &rs_mask.ri.rb, i, 0, w-1, bbox->x1) ;

        src = BLIT_ADDRESS(src, theFormL(*srcform)) ;
        rs_mask.ri.rb.ylineaddr = BLIT_ADDRESS(rs_mask.ri.rb.ylineaddr,
                                               theFormL(*dstform)) ;
      }
    }

    if ( i == h )
      return TRUE ; /* successfully encoded as spanlists */

    formarray_destroyform(dstform, page->dlpools);
  }

  linebytes = FORM_LINE_BYTES(w) ;

  if ( !formarray_newform(dstform, page->dlpools,
                          FORMTYPE_BANDBITMAP, linebytes) )
    return FALSE ;

  src = BLIT_ADDRESS(theFormA(*srcform), y_offset * theFormL(*srcform) + x_offset) ;
  dst = theFormA(*dstform) ;

  /* Span list encoding too large, fallback to bitmap. */
  if ( theFormT(*srcform) == FORMTYPE_BANDBITMAP ) {
    if ( theFormL(*srcform) == theFormL(*dstform) ) {
      /* The form lengths match, so we can copy in one go. */
      HQASSERT(x_offset == 0, "Form lengths match, so how can offset differ?") ;
      HqMemCpy(dst, src, h * theFormL(*dstform)) ;
    } else {
      for ( i = 0 ; i < h ; ++i ) {
        /* Use of dstform is deliberate in this calculation, it's working out
           if the copy (of length theFormL(*dstform)) will read too much
           data. */
        HQASSERT(src >= theFormA(*srcform) &&
                 BLIT_ADDRESS(src, theFormL(*dstform) /*sic*/)
                 <= BLIT_ADDRESS(theFormA(*srcform), theFormS(*srcform)),
                 "Trying to read outside of srcform") ;
        HQASSERT(dst >= theFormA(*dstform) &&
                 BLIT_ADDRESS(dst, theFormL(*dstform))
                 <= BLIT_ADDRESS(theFormA(*dstform), theFormS(*dstform)),
                 "Trying to write outside of dstform") ;
        HqMemCpy(dst, src, theFormL(*dstform)) ;
        src = BLIT_ADDRESS(src, theFormL(*srcform)) ;
        dst = BLIT_ADDRESS(dst, theFormL(*dstform)) ;
      }

      HQASSERT(dst <= BLIT_ADDRESS(theFormA(*dstform), theFormS(*dstform)),
               "written beyond the end of form memory") ;
    }
  } else {
    render_state_t rs_mask ;
    blit_chain_t mask_blits ;
    render_forms_t mask_forms ;

    HQASSERT(theFormT(*srcform) == FORMTYPE_BANDRLEENCODED,
             "Source form is neither bitmap nor spanlist encoded") ;

    render_state_mask(&rs_mask, &mask_blits, &mask_forms, &invalid_surface,
                      dstform) ;

    rs_mask.ri.rb.ylineaddr = dst ;

    /* The clip box is set to the input coordinate space used by bitfill0/1,
       which excludes x_sep_position. spanlist_intersecting doesn't use the
       clip box, but the bitfills will assert it. */
    bbox_store(&rs_mask.ri.clip, 0, 0, w - 1, h - 1) ;

    for ( i = 0 ; i < h ; ++i ) {
      spanlist_intersecting((spanlist_t *)src, bitfill1, bitfill0,
                            &rs_mask.ri.rb, i, 0, w - 1, bbox->x1) ;
      src = BLIT_ADDRESS(src, theFormL(*srcform)) ;
      rs_mask.ri.rb.ylineaddr = BLIT_ADDRESS(rs_mask.ri.rb.ylineaddr,
                                             theFormL(*dstform)) ;
    }

    HQASSERT(dst <= BLIT_ADDRESS(theFormA(*dstform), theFormS(*dstform)),
             "written beyond the end of form memory") ;
  }

  return TRUE ;
}

/** patternshape_finish makes the pattern shape mask or clip shape mask.  If
   there's a PATTERNOBJECT in the DL object then we're making a pattern
   shape, otherwise the top of the pattern stack has been reached and a clip
   shape mask is required if we're doing a complex clip.  A clip shape is
   required to clip the pattern DL to the pattern key cell prior to pattern
   replication.
    To produce the mask, the DL is rendered band-by-band, and the relevant
   part of the band (determined by the shape bbox) is stored as a bitmap or
   RLE (whichever is smaller). */
static Bool patternshape_finish(DL_STATE *origpage, pattern_shape_t *shape,
                                DLRANGE *dlrange)
{
  Bool result = FALSE ;
  CLIPOBJECT *clipshape = NULL ;
  DL_STATE page = *origpage ;
  render_state_t rs = {0} ;
  render_forms_t forms = {0} ;
  blit_chain_t blits = {0} ;
  form_array_t *formarray = NULL ;
  uint32 iform = 0 ;
  LISTOBJECT *lobj;
  uint32 basemap_sema = 0;
  void *memptr;
  uint32 memsize;

  HQASSERT(shape, "pattern shape null" ) ;
  VERIFY_OBJECT(shape, PATTERNSHAPE_NAME) ;
  HQASSERT(!shape->finished, "pattern shape already finished") ;
  HQASSERT(!shape->mask, "pattern shape already got a mask") ;
  if ( bbox_is_empty(&shape->bbox) ) {
    /* Union of DL objects' bboxes is empty.  Not useful, but not an error. */
    shape->finished = TRUE ;
    debug_patternshape_formarray(shape->id, formarray, "empty pattern DL") ;
    return TRUE ;
  }

  dlrange_start(dlrange);
  lobj = dlrange_lobj(dlrange);
  /* No patternstate means we possibly require a clip shape mask. */
  if ( !lobj->objectstate->patternstate ) {
    /* Fast forward to the first complex clip. */
    for ( clipshape = lobj->objectstate->clipstate ;
          clipshape && !clipshape->fill ;
          clipshape = clipshape->context )
      EMPTY_STATEMENT() ;
    /* If there's no complex clipping, no clip shape mask is required. */
    if ( !clipshape ) {
      shape->finished = TRUE ;
      debug_patternshape_formarray(shape->id, NULL, "orthogonal clipping") ;
      return TRUE ;
    }
    page.reserved_bands = RESERVED_BAND_CLIPPING ; /* only need clippingform */
  }

  rs.forms = &forms ;
  rs.ri.rb.blits = &blits ;

  /* The shape bbox is in pattern space and can exceed page width and height, so
     change the dimensions of the page to match the shape bbox.  Band height is
     then calculated from basemap size, but must have enough memory for at least
     one line per band. */
  page.page_w = shape->bbox.x2 + 1 ;
  page.band_l = page.band_l1 = FORM_LINE_BYTES(page.page_w) ;
  page.page_h = shape->bbox.y2 + 1 ;

  if ( !max_basemap_band_height(&page) )
    return FALSE;
#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!
  basemap_sema = get_basemap_semaphore(&memptr, &memsize);
  HQASSERT(basemap_sema != 0, "Basemap must be available");

  render_state_patternshape(&rs, PATTERN_CLIPPING, &page) ;
  HQASSERT(theFormT(*rs.ri.rb.outputform) == FORMTYPE_BANDBITMAP,
           "outputform should be a bitmap") ;

  if ( !mask_bands_from_basemap(rs.forms, &page) ||
       !formarray_new(page.dlpools, &shape->bbox,
                      theFormRH(*rs.ri.rb.outputform), &formarray) )
    goto cleanup ;

  /* Fast forward to the first relevant band. */
  while ( shape->bbox.y1 > rs.cs.bandlimits.y2 ) {
    rs.cs.bandlimits.y1 += theFormRH(*rs.ri.rb.outputform) ;
    rs.cs.bandlimits.y2 += theFormRH(*rs.ri.rb.outputform) ;
  }

  while ( shape->bbox.y2 >= rs.cs.bandlimits.y1 ) {
    Bool ok = TRUE ;

    HQASSERT(iform < formarray->nforms, "iforms is out of range") ;
    rs.ri.bounds = rs.cs.bandlimits ;

    theFormHOff(*rs.ri.rb.outputform) = rs.cs.bandlimits.y1 ;
    theFormHOff(*rs.ri.rb.clipform) = rs.cs.bandlimits.y1 ;

    if ( !clip_context_begin(&rs.ri) )
      goto cleanup ;

    if ( clipshape ) {
      dbbox_t clipbox ;

      /* Check to see if the clip box, when adjusted for tesselating
         clipping, will intersect the current bounds. If not, leave the form
         blank. */
      if ( clip_to_bounds(&rs.ri, clipshape, &clipbox) ) {
        render_tracker_t render_tracker = {0} ;

        rs.cs.renderTracker = &render_tracker ;
        render_tracker_init(&render_tracker) ;
        render_tracker.clipping = CLIPPING_firstcomplex ;

        ok = regenerate_clipping(&rs.ri, clipshape) ;

        rs.cs.renderTracker = NULL ;

        if ( ok )
          ok = patternshape_write(&page, rs.ri.rb.clipform, &formarray->forms[iform],
                                  &shape->bbox, rs.cs.bandlimits.y1, rs.cs.bandlimits.y2) ;
      }
    } else {
      theFormT(*rs.ri.rb.outputform) = FORMTYPE_BANDBITMAP ;
      area0fill(rs.ri.rb.outputform) ;

      ok = (render_objects_of_z_order_band(dlrange, &rs, rs.cs.bandlimits.y1,
                                           rs.cs.bandlimits.y2) &&
            patternshape_write(&page, rs.ri.rb.outputform, &formarray->forms[iform],
                               &shape->bbox, rs.cs.bandlimits.y1, rs.cs.bandlimits.y2)) ;
    }

    clip_context_end(&rs.ri) ;

    if ( !ok )
      goto cleanup ;

    ++iform ;
    rs.cs.bandlimits.y1 += theFormRH(*rs.ri.rb.outputform) ;
    rs.cs.bandlimits.y2 += theFormRH(*rs.ri.rb.outputform) ;
  }

  shape->mask = formarray ;
  shape->finished = TRUE ;

  debug_patternshape_formarray(shape->id, formarray,
                               clipshape ? "clipping" : "shapes") ;

  result = TRUE ;
 cleanup :
  if ( !result ) {
    if ( formarray )
      formarray_destroy(&formarray, page.dlpools);
  }
  if ( basemap_sema != 0 )
    free_basemap_semaphore(basemap_sema);

#undef return
  return result ;
}

/**
 * Do a breadth-first iteration over this display-list.
 * Breadth-first is needed because pattern shapes are
 * generated from multiple adjacent listobjects, all at the same level in
 * the DL.  Need to do this manually because dl_forall is depth-first.
 * dl_forall is required to ensure all HDLs are found.
 */
static Bool patternshape_finishcallback(DL_FORALL_INFO *info)
{
  HDL **last_hdl = info->data;
  pattern_shape_t *shape = NULL;
  DLRANGE dlrange, pat_range;

  if ( info->hdl == *last_hdl )
    return TRUE;
  *last_hdl = info->hdl;

  dlrange_init(&pat_range);
  hdlDlrange(info->hdl, &dlrange);

  for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
        dlrange_next(&dlrange) ) {
    LISTOBJECT *lobj = dlrange_lobj(&dlrange);

    dlrange_setend(&pat_range, &dlrange);
    if ( lobj->objectstate->patternshape ) {
      dbbox_t bbox;

      VERIFY_OBJECT(lobj->objectstate->patternshape, PATTERNSHAPE_NAME);

      if ( shape != lobj->objectstate->patternshape ) {
        /* Pattern shape is different from the last one
         * (if there was a last one).
         */
        if ( shape && !shape->finished ) {
          if ( !patternshape_finish(info->page, shape, &pat_range) )
            return FALSE;
        }
        shape = lobj->objectstate->patternshape;
        dlrange_setstart(&pat_range, &dlrange);
        dlrange_setend(&pat_range, NULL);
      }

      /* Calculate the mask bbox as we go. */
      dlobj_bbox(lobj, &bbox);
      /* Round x1 down to align to blit_t. */
      bbox.x1 &= ~BLIT_MASK_BITS;
      HQASSERT(!shape->finished || bbox_contains(&shape->bbox, &bbox),
               "pattern shape must have been finished without this lobj");
      bbox_union(&shape->bbox, &bbox, &shape->bbox);

    } else {
      /* No pattern shape; finish the last one (if there was a last one). */
      if ( shape && !shape->finished ) {
        if ( !patternshape_finish(info->page, shape, &pat_range) )
          return FALSE;
        shape = NULL;
        dlrange_init(&pat_range);
      }
    }
  }
  dlrange_setend(&pat_range, NULL);

  /* Finish the trailing pattern shape. */
  if ( shape && !shape->finished ) {
    if ( !patternshape_finish(info->page, shape, &pat_range) )
      return FALSE;
  }

  return TRUE;
}

/** Ids are used in creating augmented shapes and rendering. */
static Bool patternshape_makeids(DL_STATE *page, pattern_tracker_t *tracker)
{
  pattern_tracker_t *next ;
  int32 *ids ;
  uint32 ids_len ;
  uint32 i ;

  HQASSERT(tracker->patternshape, "pattern shape missing from pattern tracker") ;
  HQASSERT(!tracker->patternshape->ids && tracker->patternshape->ids_len == 0,
           "ids already created") ;

  for ( next = tracker, ids_len = 0 ;
        next ;
        next = next->pParentPattern, ++ids_len )
    EMPTY_STATEMENT() ;

  ids = dl_alloc(page->dlpools,
                 sizeof(int32) * ids_len, MM_ALLOC_CLASS_PATTERN_SHAPE) ;
  if ( !ids )
    return error_handler(VMERROR) ;

  for ( next = tracker, i = 0 ;
        next ;
        next = next->pParentPattern, ++i ) {
    ids[i] = next->patternshape->id ;
  }

  tracker->patternshape->ids = ids ;
  tracker->patternshape->ids_len = ids_len ;
  return TRUE ;
}

/** Have we generated an augmented mask for this pattern shape with this set of ids? */
static augmented_mask_t *patternshape_findaugmentedmask(pattern_shape_t *shape,
                                                        int32 *ids, uint32 ids_len)
{
  augmented_mask_t *augmented ;

  /** \todo ajcd 2008-02-07: Possibly use MRU list here, but need to be
      careful of threading issues. */
  if ( !ids )
    return NULL ;

  for ( augmented = shape->augmented_masks ; augmented ; augmented = augmented->next ) {
    uint32 i ;
    for ( i = 0 ; i < ids_len ; ++i ) {
      if ( ids[i] != augmented->ids[i] )
        break ;
    }
    if ( i == ids_len )
      return augmented ;
  }

  return NULL ;
}

/** Creates a new empty augmented mask item. */
static Bool patternshape_newaugmentedmask(DL_STATE *page,
                                          augmented_mask_t **p_augmented_list,
                                          int32 *ids, uint32 ids_len)
{
  augmented_mask_t *augmented ;
  uint32 i ;

  augmented = dl_alloc(page->dlpools,
                       sizeof(augmented_mask_t) + sizeof(int32) * ids_len,
                       MM_ALLOC_CLASS_PATTERN_SHAPE) ;
  if ( !augmented )
    return error_handler(VMERROR) ;

  augmented->next = *p_augmented_list ;
  augmented->ids = (int32*)(augmented + 1) ;
  for ( i = 0 ; i < ids_len ; ++i ) {
    augmented->ids[i] = ids[i] ;
  }
  augmented->ids_len = ids_len ;
  augmented->mask = NULL ;

  *p_augmented_list = augmented ;

  return TRUE ;
}

/** For recursive patterns which are not defined in the context of their
  immediate parent we need to generate a new mask which is a combination of
  an existing mask augmented with the pattern shapes from another pattern.
  Futher details are given above the definition of augmented_mask_t. */
static Bool patternshape_augment(DL_STATE *origpage, pattern_tracker_t *tracker,
                                 int32 *ids, uint32 ids_len)
{
  Bool result = FALSE ;
  DL_STATE page = *origpage ;
  pattern_shape_t *shape ;
  augmented_mask_t *augmented ;
  render_state_t rs = {0} ;
  render_forms_t forms = {0} ;
  blit_chain_t blits = {0} ;
  pattern_recurse_t recurse_data ;
  form_array_t *formarray = NULL, *clipformarray, *maskformarray ;
  uint32 iform = 0 ;
  pattern_tracker_t augment_tracker ;
  uint32 basemap_sema = 0;
  void *memptr;
  uint32 memsize;

  static blit_slice_t bitmap_slice = {
    bitclip1, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
  } ;

  static blit_slice_t span_slice = {
    spanclip1, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
  } ;

  /* If the shape is not relative to another pattern shape then we need do
     no more; patternshapeform will be augmented by shape at render time. */
  if ( tracker->pContextPattern == NULL )
    return TRUE ;

  shape = tracker->pContextPattern->patternshape ;

  /* Find/allocate an augmented mask to put the results into. */
  augmented = patternshape_findaugmentedmask(shape, ids, ids_len) ;
  if ( augmented ) {
    clipformarray = augmented->mask ;
  } else {
    if ( !patternshape_newaugmentedmask(&page, &shape->augmented_masks,
                                        ids, ids_len) )
      return FALSE ;

    augmented = shape->augmented_masks ;
    clipformarray = shape->mask ; /* use the default pattern shape as the clip */
  }

  /* Find the mask we're going to paint (either the normal pattern shape or
     one that has previously been augmented). */
  {
    augmented_mask_t *augmented_to_paint =
      patternshape_findaugmentedmask(tracker->patternshape, ids, ids_len) ;
    if ( augmented_to_paint )
      maskformarray = augmented_to_paint->mask ;
    else
      maskformarray = tracker->patternshape->mask ;
  }

  rs.forms = &forms ;
  rs.ri.rb.blits = &blits ;

  /* The shape bbox is in pattern space and can exceed page width and height, so
     change the dimensions of the page to match the shape bbox.  Band height is
     then calculated from basemap size, but must have enough memory for at least
     one line per band. */
  page.page_w = shape->bbox.x2 + 1 ;
  page.band_l = page.band_l1 = FORM_LINE_BYTES(page.page_w) ;
  page.page_h = shape->bbox.y2 + 1 ;

  if ( !max_basemap_band_height(&page) )
    return FALSE;
#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!
  basemap_sema = get_basemap_semaphore(&memptr, &memsize);
  HQASSERT(basemap_sema != 0, "Basemap must be available");

  /* Prepare a new tracker which will replicate just one level appropriately */
  augment_tracker = *tracker ;
  augment_tracker.pContextPattern = NULL ;

  recurse_data.top_pattern = &augment_tracker ;
  recurse_data.base_clipform = NULL ;
  NAME_OBJECT(&recurse_data, PATTERNRECURSEDATA_NAME) ;

  render_state_patternshape(&rs, PATTERN_PAINTING, &page) ;

  /* The clip is not necessarily contained in the bandlimits, because we are
     replicating a pattern cell. The bandlimits represent a slice of the
     parent cell (and are copied into the pattern replication limits), the
     bounds and clip represent the pattern cell size.  */
  rs.ri.clip = rs.ri.bounds = tracker->patternshape->bbox ;

  if ( !mask_bands_from_basemap(rs.forms, &page) ||
       !formarray_new(page.dlpools, &shape->bbox,
                      theFormRH(*rs.ri.rb.outputform), &formarray) )
    goto cleanup ;

  /* Fast forward to the first relevant band. */
  while ( shape->bbox.y1 > rs.cs.bandlimits.y2 ) {
    rs.cs.bandlimits.y1 += theFormRH(*rs.ri.rb.outputform) ;
    rs.cs.bandlimits.y2 += theFormRH(*rs.ri.rb.outputform) ;
  }

  /* Band-by-band, render the mask, replicating and clipping along the way. */
  while ( shape->bbox.y2 >= rs.cs.bandlimits.y1 ) {
    /* The first and last maskforms may not be full band height. */
    rs.cs.bandlimits.y1 = max(rs.cs.bandlimits.y1, shape->bbox.y1) ;
    rs.cs.bandlimits.y2 = min(rs.cs.bandlimits.y2, shape->bbox.y2) ;

    augment_tracker.oFormHOff = rs.cs.bandlimits.y1 ;

    bbox_intersection(&shape->bbox, &rs.cs.bandlimits, &augment_tracker.replim) ;

    rs.ri.rb.clipform = &clipformarray->forms[iform] ;
    theFormHOff(*rs.ri.rb.outputform) = rs.cs.bandlimits.y1 ;
    theFormHOff(*rs.ri.rb.clipform) = rs.cs.bandlimits.y1 ;

    set_pattern_replication(&augment_tracker, &rs.ri.rb, &recurse_data) ;
    /* PATTERNCLIP_BLIT_INDEX already cleared by set_pattern_replication. */

    if ( theFormT(*rs.ri.rb.clipform) != FORMTYPE_BLANK ) {
      FORM *maskform ;
      uint32 nmaskforms ;
      dcoord x, y ;
      Bool ok = TRUE ;

      if ( !clip_context_begin(&rs.ri) )
        goto cleanup ;

      theFormT(*rs.ri.rb.outputform) = FORMTYPE_BANDBITMAP ;
      area0fill(rs.ri.rb.outputform) ;

      x = tracker->patternshape->bbox.x1 ;
      y = maskformarray->bbox.y1 ;
      maskform = maskformarray->forms ;
      for ( nmaskforms = maskformarray->nforms ; nmaskforms > 0 ; --nmaskforms ) {
        blit_slice_t *slice ;

        if ( theFormT(*rs.ri.rb.clipform) == FORMTYPE_BANDBITMAP ) {
          slice = &bitmap_slice ;
        } else if ( theFormT(*rs.ri.rb.clipform) == FORMTYPE_BANDRLEENCODED ) {
          slice = &span_slice ;
        } else {
          HQFAIL("Clipform is not bitmap, RLE encoded, or blank") ;
          slice = &invalid_slice ;
        }

        SET_BLITS(&blits, BASE_BLIT_INDEX, slice, slice, slice) ;

        if ( theFormT(*maskform) == FORMTYPE_BANDBITMAP ) {
          charbltn(&rs.ri.rb, maskform, tracker->patternshape->bbox.x1, y) ;
        } else if ( theFormT(*maskform) == FORMTYPE_BANDRLEENCODED ) {
          charbltspan(&rs.ri.rb, maskform, tracker->patternshape->bbox.x1, y) ;
        } else {
          HQASSERT(theFormT(*maskform) == FORMTYPE_BLANK,
                   "Pattern shape form type unknown") ;
        }

        y += theFormRH(*maskform) ;
        ++maskform ;
      }

      /* The clipform is taken from the clipformarray, so may need the height
         offset restoring (maybe not, it's probably not actually used for
         anything). The outputform is allocated for this render state, and
         its contents will be copied by patternshape_write(), so doesn't need
         restoring. */
      theFormHOff(*rs.ri.rb.clipform) = 0 ;

      ok = patternshape_write(&page, rs.ri.rb.outputform, &formarray->forms[iform],
                              &shape->bbox, rs.cs.bandlimits.y1, rs.cs.bandlimits.y2) ;

      clip_context_end(&rs.ri) ;

      if ( !ok )
        goto cleanup ;
    }

    ++iform ;
    rs.cs.bandlimits.y1 = rs.cs.bandlimits.y2 + 1 ;
    rs.cs.bandlimits.y2 += theFormRH(*rs.ri.rb.outputform) ;
  }

  if ( augmented->mask )
    formarray_destroy(&augmented->mask, page.dlpools);
  augmented->mask = formarray ;

  debug_patternshape_formarray(shape->id, formarray, "augmented shapes") ;

  result = TRUE ;
 cleanup:
  if ( !result ) {
    if ( formarray )
      formarray_destroy(&formarray, page.dlpools);
  }
  if ( basemap_sema != 0 )
    free_basemap_semaphore(basemap_sema);

#undef return
  return result ;
}

/** Whilst generating the final set of augmented masks, a number of
   temporary augmented masks may be created; afterwards these masks can be
   destroyed. */
static void patternshape_removeaugment(DL_STATE *page,
                                       pattern_tracker_t *tracker,
                                       int32 *ids, uint32 ids_len)
{
  augmented_mask_t **p_augmented ;

  for ( p_augmented = &tracker->patternshape->augmented_masks ;
        *p_augmented ;
        p_augmented = &(*p_augmented)->next ) {
    uint32 i ;
    for ( i = 0 ; i < ids_len ; ++i ) {
      if ( ids[i] != (*p_augmented)->ids[i] )
        break ;
    }
    if ( i == ids_len ) {
      augmented_mask_t *augmented = *p_augmented ;
      *p_augmented = augmented->next ;

      debug_patternshape_formarray(tracker->patternshape->id, augmented->mask,
                                  "deleted intermediate") ;

      formarray_destroy(&augmented->mask, page->dlpools);
      dl_free(page->dlpools, augmented, sizeof(augmented_mask_t) +
              sizeof(int32) * augmented->ids_len, MM_ALLOC_CLASS_PATTERN_SHAPE);
      HQASSERT(!patternshape_findaugmentedmask(tracker->patternshape, ids, ids_len),
               "There should only be one augmented pattern form for this id") ;
      break ;
    }
  }
}

/** Manually iterate over the DL making any augmented masks as required.
   Unfortunately I cannot use a dl_forall here because I need to be build a
   stack of pattern trackers similar to the one created in the final render.
   But since pattern augmenting is only required for PS, at least I don't
   have to worry about soft mask groups etc. */
static Bool patternshape_augmentiterate(DL_STATE *page,
                                        pattern_tracker_t *parent_tracker,
                                        PATTERNOBJECT *patobj)
{
  pattern_tracker_t tracker = { 0 } ;
  DLRANGE dlrange;
  HDL *pathdl = patternHdl(patobj);

  /* There is only an HDL if there is a pattern DL. */
  if ( pathdl == NULL )
    return TRUE ;

  tracker.pParentPattern = parent_tracker ;
  for ( tracker.pContextPattern = parent_tracker ;
        tracker.pContextPattern &&
          tracker.pContextPattern->pPattern->patternid != patobj->context_patternid ;
        tracker.pContextPattern = tracker.pContextPattern->pParentPattern )
    EMPTY_STATEMENT() ;

  tracker.pBasePattern = parent_tracker ? parent_tracker->pBasePattern : &tracker ;

  tracker.pPattern = patobj ;
  tracker.patternshape = NULL ;
  patobj = NULL ;

  hdlDlrange(pathdl, &dlrange);
  for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
        dlrange_next(&dlrange) ) {
    LISTOBJECT *lobj = dlrange_lobj(&dlrange);
    Bool patternstate_change = patobj != lobj->objectstate->patternstate ;
    Bool patternshape_change = tracker.patternshape != lobj->objectstate->patternshape ;

    patobj = lobj->objectstate->patternstate ;
    tracker.patternshape = lobj->objectstate->patternshape ;
    HQASSERT(!tracker.patternshape ||
             tracker.patternshape->patternid == tracker.pPattern->patternid,
             "pattern shape / pattern object mismatch") ;

    if ( patobj ) {
      if ( patternstate_change ) {
        /* Found a recursive pattern. */
        if ( !patternshape_augmentiterate(page, &tracker, patobj) )
          return FALSE ;
      }
    } else if ( patternshape_change ) {
      int32 context_patternid ;
      pattern_tracker_t *current, *next ;
      int32 *ids ;
      uint32 ids_len ;

      /* Augment any pattern shapes that require it for the current pattern. */

      if ( !tracker.patternshape->ids ) {
        if ( !patternshape_makeids(page, &tracker) )
          return FALSE ;
      }
      ids = tracker.patternshape->ids ;
      ids_len = tracker.patternshape->ids_len ;

      current = &tracker ;
      context_patternid = current->pPattern->context_patternid ;
      for ( next = tracker.pParentPattern ; next ; next = next->pParentPattern ) {
        if ( next->patternshape->patternid == context_patternid ) {
          /* Pattern defined relative to its immediate parent. */
          current = next ;
          context_patternid = current->pPattern->context_patternid ;
        } else if ( next->pParentPattern ) {
          /* An 'absolute' pattern which needs augmenting. */
          if ( !patternshape_augment(page, next, ids, ids_len) )
            return FALSE ;
        }
      }

      /* Only the augmented masks of the patterns which are replicated in
         the final render need to be kept.  The shapes of the patterns that
         aren't replicated have now been merged with the shapes of those
         that are replicated, and the intermediate augmented masks can be
         removed. */
      current = &tracker ;
      context_patternid = current->pPattern->context_patternid ;
      for ( next = tracker.pParentPattern ; next ; next = next->pParentPattern ) {
        if ( next->patternshape->patternid == context_patternid ) {
          current = next ;
          context_patternid = current->pPattern->context_patternid ;
        } else if ( next->pParentPattern ) {
          /* Don't need this intermediate augmented mask any more. */
          patternshape_removeaugment(page, next, ids, ids_len) ;
        }
      }
    }
  }

  return TRUE ;
}

/** At the end of creating a pattern DL we produce the all masks. */
Bool patternshape_finishdl(DL_STATE *page, PATTERNOBJECT *patobj)
{
  HDL *pathdl = patternHdl(patobj), *last_hdl = NULL;
  DL_FORALL_INFO info;

  /* There is only an HDL if there is a pattern DL. */
  if ( pathdl == NULL )
    return TRUE ;

  info.page    = page;
  info.hdl     = pathdl;
  info.data    = &last_hdl;
  info.inflags = DL_FORALL_USEMARKER|DL_FORALL_GROUP|DL_FORALL_SOFTMASK;

  if ( !dl_forall(&info, patternshape_finishcallback) )
    return FALSE;

  if ( patobj->parent_patternid == INVALID_PATTERN_ID ) {
    /* dl_forall is not used here because we need to recreate the stack of
       pattern_tracker_t structures that happens in the renderer.
       Fortunately augmented masks can happen in PS only, and therefore we
       don't need to worry about reaching patterns in soft masks etc. */
    if ( !patternshape_augmentiterate(page, NULL, patobj) )
      return FALSE ;
  }

  return TRUE ;
}

/** Give me the clip shape mask for this innermost child pattern. */
form_array_t *patternshape_clipform(pattern_shape_t *shape)
{
  VERIFY_OBJECT(shape, PATTERNSHAPE_NAME) ;
  HQASSERT(shape->finished, "Trying to use an unfinished pattern shape's clip mask") ;

  return shape->mask ;
}

/** Give me the appropriate pattern shape mask for this level in this stack
   of patterns. */
form_array_t *patternshape_maskform(pattern_tracker_t *top_pattern,
                                    pattern_tracker_t *relative_pattern)
{
  augmented_mask_t *augmented = NULL ;
  form_array_t *formarray ;

  VERIFY_OBJECT(top_pattern->patternshape, PATTERNSHAPE_NAME) ;
  VERIFY_OBJECT(relative_pattern->patternshape, PATTERNSHAPE_NAME) ;
  HQASSERT(top_pattern->patternshape->finished, "Trying to use an unfinished pattern shape's mask") ;
  HQASSERT(relative_pattern->patternshape, "Trying to use an unfinished pattern shape's mask") ;

  augmented = patternshape_findaugmentedmask(relative_pattern->patternshape,
                                             top_pattern->patternshape->ids,
                                             top_pattern->patternshape->ids_len) ;

  if ( augmented )
    formarray = augmented->mask ;
  else
    formarray = relative_pattern->patternshape->mask ;

  return formarray ;
}

const dbbox_t *patternshape_bbox(pattern_shape_t *shape)
{
  VERIFY_OBJECT(shape, PATTERNSHAPE_NAME) ;
  HQASSERT(shape->finished, "Trying to use an unfinished pattern shape's bbox") ;

  return &shape->bbox ;
}

#if defined( DEBUG_BUILD )
#include "ripdebug.h"

void init_patternshape_debug(void)
{
  register_ripvar(NAME_debug_patternshape, OINTEGER, &debug_patternshape) ;
}

void debug_patternshape_formarray(uint32 id, form_array_t *formarray, char *use)
{
 /* Mark unused even though they are used within a HQTRACE below. In a
    non-asserted build, we get warnings otherwise. */
  UNUSED_PARAM( char *, use ) ;
  UNUSED_PARAM( uint32, id ) ;

  if ( formarray ) {
    FORM *form = formarray->forms ;
    uint32 iform ;
    uint32 n_bitmap = 0, n_rle = 0 ;
    uint32 n_area = 0, n_bytes = 0 ;

    for ( iform = 0 ; iform < formarray->nforms ; ++iform, ++form ) {
      HQASSERT(theFormT(*form) == FORMTYPE_BLANK ||
               theFormT(*form) == FORMTYPE_BANDBITMAP ||
               theFormT(*form) == FORMTYPE_BANDRLEENCODED,
               "form must be blank, bitmap or RLE only") ;
      if ( theFormT(*form) == FORMTYPE_BANDBITMAP )
        ++n_bitmap ;
      else if ( theFormT(*form) == FORMTYPE_BANDRLEENCODED )
        ++n_rle ;
      n_area += theFormRH(*form) * FORM_LINE_BYTES(theFormW(*form)) ;
      n_bytes += theFormS(*form) ;
    }
    HQTRACE((debug_patternshape & PATTERNSHAPE_DEBUG_TRACE),
            ("patternshape %s (id %d): %d forms (bitmaps %d / RLE %d); "
             "bytes %d / area %d = %.2f%%",
             use, id, n_bitmap + n_rle, n_bitmap, n_rle,
             n_bytes, n_area, 100.0 * ((float)n_bytes / (float)n_area))) ;
  } else {
    HQTRACE((debug_patternshape & PATTERNSHAPE_DEBUG_TRACE),
            ("patternshape %s (id %d): no forms", use, id)) ;
  }
}
#endif

/* =============================================================================
* Log stripped */
