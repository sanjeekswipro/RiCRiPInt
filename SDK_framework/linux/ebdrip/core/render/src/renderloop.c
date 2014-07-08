/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderloop.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software.  All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Main object rendering loop.
 */

#include "core.h"
#include "render.h"

#include "mlock.h"
#include "often.h"
#include "control.h" /* interrupts_clear */
#include "timing.h"
#include "md5.h"
#include "asyncps.h"
#include "progupdt.h"

#include "display.h"  /* DISPOSITION_MUST_RENDER */
#include "dl_bbox.h" /* dlobj_intersects */
#include "dl_color.h" /* dlc_from_lobj_weak */
#include "imaget.h" /* IMAGEOBJECT */
#include "dl_image.h" /* bd_is_backdrop_image */
#include "pattern.h"
#include "patternrender.h"
#include "patternshape.h"
#include "dlstate.h" /* DOING_TRANSPARENT_RLE */
#include "color.h" /* ht_applyTransform */
#include "htrender.h" /* ht_getModularHalftoneRef */
#include "gu_htm.h" /* MODHTONE_REF */
#include "trap.h" /* trapUpdateRenderInfo */
#include "vnobj.h"    /* VIGNETTEOBJECT */
#include "watermrk.h"
#include "dl_bres.h"
#include "gu_chan.h"
#include "gscxfer.h" /* gsc_forcepositive */
#include "interrupts.h"

#include "renderfn.h"
#include "bitblts.h"  /* BITBLT_FUNCTION tables */
#include "bitblth.h"  /* area0fill */
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "clipblts.h"
#include "toneblt.h"  /* maxbltfills */
#include "rleblt.h" /* rleSetTransparency */
#include "scanconv.h"
#include "scpriv.h"

#include "params.h"
#include "group.h" /* groupRender for soft masks */
#include "pcl5Blit.h"
#include "pixelLabels.h"
#include "dl_purge.h"
#include "preconvert.h"
#include "pclAttrib.h"
#include "hdl.h" /* hdlTransparent */



/** Black blit color for masks. */
static blit_color_t mask_black_color;


/** Call render function for current listobject, after running the
    appropriate preparation function for the surface. */
static inline Bool do_render_function(render_info_t *p_ri, Bool screened)
{
  Bool result = TRUE ;
  const surface_t *surface = p_ri->surface ;

#ifdef PROBE_BUILD
  int trace_id = SW_TRACE_INVALID;

  switch ( p_ri->lobj->opcode ) {
  case RENDER_rect:
  case RENDER_quad:
  case RENDER_fill:
    trace_id = SW_TRACE_RENDER_VECTOR;
    break;

  case RENDER_char:
    trace_id = SW_TRACE_RENDER_TEXT;
    break;

  case RENDER_vignette:
  case RENDER_gouraud:
  case RENDER_shfill:
  case RENDER_shfill_patch:
    trace_id = SW_TRACE_RENDER_SHADE;
    break;

  case RENDER_mask:
  case RENDER_image:
    trace_id = SW_TRACE_RENDER_IMAGE;
    break;

  case RENDER_backdrop:
    trace_id = SW_TRACE_RENDER_BACKDROP;
    break;

  case RENDER_erase:
    trace_id = SW_TRACE_RENDER_ERASE;
    break;

  case RENDER_void:
  case RENDER_hdl:
  case RENDER_group:
  default:
    /* Don't bother with probes for these for now */
    trace_id = SW_TRACE_INVALID;
    break;
  }

  probe_begin(p_ri->region_set_type == RENDER_REGIONS_BACKDROP
              ? SW_TRACE_COMPOSITE_OBJECT
              : SW_TRACE_RENDER_OBJECT,
              (intptr_t)p_ri->lobj->opcode) ;
  if ( trace_id != SW_TRACE_INVALID )
    probe_begin(trace_id, (intptr_t)p_ri->lobj);
#endif

  HQASSERT(surface != NULL, "No output surface") ;

  switch ( (*surface->prepare)(p_ri->p_rs->surface_handle, p_ri) ) {
  case SURFACE_PREPARE_OK:
    result = (*renderfuncs[p_ri->lobj->opcode])(p_ri, screened) ;
    /*@fallthrough@*/
  case SURFACE_PREPARE_SKIP:
    break ;
  default:
    HQFAIL("Invalid return value from surface prepare function") ;
    /*@fallthrough@*/
  case SURFACE_PREPARE_FAIL:
    result = FALSE ;
    break ;
  }

#ifdef PROBE_BUILD
  if ( trace_id != SW_TRACE_INVALID )
    probe_end(trace_id, (intptr_t)p_ri->lobj);
  probe_end(p_ri->region_set_type == RENDER_REGIONS_BACKDROP
            ? SW_TRACE_COMPOSITE_OBJECT
            : SW_TRACE_RENDER_OBJECT,
            (intptr_t)p_ri->lobj->opcode) ;
#endif

  return result ;
}


#ifdef DEBUG_BUILD
static void debug_render_rect(const render_state_t *p_rs,
                              dcoord x, dcoord y, dcoord w, dcoord h,
                              STATEOBJECT *state, Bool isblack);
#endif

multi_rwlock_t nfill_lock;

/* Regenerate clipping state; rectangular clipping is already in [xy][12]clip
   from render loop. find_clipping returns a pointer to the top complex
   clipping object that needs regenerating, or NULL if there is no complex
   clip regeneration, and sets clipping to the clipping state
   (CLIPPING_rectangular for rectangular only, CLIPPING_firstcomplex for
   first complex clip, CLIPPING_complex for subsequent complex
   clips). regenerate_clipping does the clipping form regeneration. */
static CLIPOBJECT *find_clipping(CLIPOBJECT *theclip,
                                 render_tracker_t *tracker)
{
  CLIPOBJECT *newcomplex = NULL ;

  tracker->oldclip = theclip ;
  tracker->clipping = CLIPPING_rectangular ; /* Initialise to no complex clipping */

  /* Search for last clip to do; this may be the end of the chain, or it may
     be the complex clip which is represented in the clippingform */
  while (theclip && theclip != tracker->clippingformstate ) {
    if ( theclip->fill && ! newcomplex )
      newcomplex = theclip ;
    theclip = theclip->context ;
  }

  /* Since rectangular clipping is already set up by the render loop, we only
     need to work out if there is complex clipping to amalgamate */
  if ( theclip ) {                     /* Have existing clipping form */
    tracker->clipping = CLIPPING_complex ;
  } else if ( newcomplex ) {           /* Regenerating from scratch */
    tracker->clipping = CLIPPING_firstcomplex ;
    tracker->clippingformstate = NULL ;
  }

  return newcomplex ;
}

/** Name for clip regeneration data. */
#define SURFACE_CLIP_CALLBACK_NAME "Clipping regeneration data"

/** Callback data for clip regeneration. */
struct surface_clip_callback_t {
  NFILLOBJECT *nfill ;
  dcoord y1 ;
  int32 rule ;
  render_blit_t *rb ;

  OBJECT_NAME_MEMBER
} ;

/** Callback function for clip regeneration. */
static void regenerate_clip_callback(surface_clip_callback_t *data)
{
  VERIFY_OBJECT(data, SURFACE_CLIP_CALLBACK_NAME) ;

  NFILL_LOCK_WR_CLAIM(data->nfill);

  REPAIR_NFILL(data->nfill, data->y1);
  scanconvert_band(data->rb, data->nfill, data->rule) ;

  NFILL_LOCK_RELEASE();
}

/** Regenerate a complex clip. This is done by substituting the clip surface,
    blit color and blit chain for the current surface, setting a mask color
    for the clip type, and running a localiser in the complex clip surface
    which calls back to fill the clip. */
static Bool regenerate_clip(render_info_t *ri,
                            CLIPOBJECT *theclip,
                            render_tracker_t *tracker)
{
  const clip_surface_t *clip_surface ;
  const render_state_t *p_rs ;
  render_info_t ri_copy ;
  blit_chain_t clip_blits ;
  blit_color_t clip_color ;
  blit_colormap_t clip_colormap ;
  surface_clip_callback_t data ;
  Bool result ;

  RI_COPY_FROM_RI(&ri_copy, ri) ;

  p_rs = ri_copy.p_rs ;

  /* Swap in the clip surface. */
  clip_surface = ri_copy.surface->clip_surface ;
  ri_copy.surface = &clip_surface->base ;

  HQASSERT(tracker->clipping != CLIPPING_rectangular,
           "clip tracking wrong in regenerate_clip") ;
  HQASSERT(ri_copy.pattern_state != PATTERN_PAINTING,
           "Replicating in regenerate_clip") ;

  blit_colormap_mask(&clip_colormap) ;
  blit_color_init(&clip_color, &clip_colormap) ;
  blit_color_mask(&clip_color, (theclip->rule & CLIPINVERT) != 0) ;

  ri_copy.rb.outputform = ri_copy.rb.clipform ;
  ri_copy.rb.blits = &clip_blits ;
  ri_copy.rb.color = &clip_color ;

  /* Reset the clip blits to the clip surface base only. */
  RESET_BLITS(ri_copy.rb.blits,
              &ri_copy.surface->baseblits[BLT_CLP_NONE],
              &ri_copy.surface->baseblits[BLT_CLP_RECT],
              &ri_copy.surface->baseblits[BLT_CLP_COMPLEX]) ;

  data.nfill = theclip->fill ;
  data.rb = &ri_copy.rb ;
  data.y1 = ri_copy.clip.y1 ;
  data.rule = (theclip->rule & CLIPRULE) | ISCLIP ;
  NAME_OBJECT(&data, SURFACE_CLIP_CALLBACK_NAME) ;

  result = (*clip_surface->complex_clip)(p_rs->surface_handle,
                                         theclip->clipno,
                                         /* Previous complex clipid. */
                                         tracker->clippingformstate
                                         ? tracker->clippingformstate->clipno
                                         : SURFACE_CLIP_INVALID,
                                         &ri_copy.rb,
                                         &regenerate_clip_callback, &data) ;

  UNNAME_OBJECT(&data) ;

  return result ;
}

Bool clip_to_bounds(render_info_t *p_ri, CLIPOBJECT *newclip,
                    dbbox_t *clipped)
{
  *clipped = newclip->bounds ;
  if ( p_ri->p_rs->page->ScanConversion == SC_RULE_TESSELATE ) {
    /** \todo Don't decrement x2 or y2 if they are at the edge of the page,
        otherwise nothing would be painted in the last row or column
        (a proper fix is required for this). If we're in a pattern cell, we
        don't want to check the edges of the page, because they're different
        from the pattern cell. We don't want to leave the clip at the pattern
        cell size, because it won't tesselate properly. */
    if ( p_ri->rb.p_painting_pattern != NULL ||
         clipped->y2 < p_ri->p_rs->page->page_h - 1 )
      --clipped->y2 ;
    if ( p_ri->rb.p_painting_pattern != NULL ||
         clipped->x2 < p_ri->p_rs->page->page_w - 1 )
      --clipped->x2 ;
  }
  bbox_intersection(clipped, &p_ri->bounds, clipped);

  return !bbox_is_empty(clipped) ;
}

static Bool regenerate_clipping_recurse(render_info_t *ri,
                                        CLIPOBJECT *newcomplex)
{
  CLIPOBJECT *prevcomplex = newcomplex ;
  const render_state_t *p_rs = ri->p_rs ;
  render_tracker_t *tracker = p_rs->cs.renderTracker ;
  const clip_surface_t *clip_surface = ri->surface->clip_surface ;

  HQASSERT(newcomplex, "regenerate_clipping called with NULL clip object") ;
  HQASSERT(newcomplex != tracker->clippingformstate,
           "regenerate_clipping called with current clip object state") ;
  HQASSERT(newcomplex->fill,
           "regenerate_clipping called with simple clip object") ;

  HQASSERT(tracker->clipping != CLIPPING_rectangular,
           "clip tracking not set correctly in regenerate_clipping") ;

  /* Regenerate the previous complex clip. We need to use a recursive
     regeneration algorithm so that the clip surface sees calls bottom-up.
     The clip surface may cache and re-use multiple clip states. */
  while ( (prevcomplex = prevcomplex->context) != NULL &&
          prevcomplex != tracker->clippingformstate ) {
    if ( prevcomplex->fill ) {
      if ( clip_surface->complex_cached &&
           (*clip_surface->complex_cached)(p_rs->surface_handle,
                                           prevcomplex->clipno) ) {
        /* Already cached this complex clip. */
        tracker->clipping = CLIPPING_complex ;
        tracker->clippingformstate = prevcomplex ;
      } else {
        if ( !regenerate_clipping_recurse(ri, prevcomplex) )
          return FALSE ;
      }
      break ;
    }
  }

  /* If caching, regenerate clip with its own clip boundaries. */
  if ( clip_surface->complex_cached &&
       !clip_to_bounds(ri, newcomplex, &ri->clip) )
    HQFAIL("clipping regeneration should not be degenerate") ;

  if ( !regenerate_clip(ri, newcomplex, tracker) )
    return FALSE ;

  tracker->clipping = CLIPPING_complex ; /* AND other clips */
  tracker->clippingformstate = newcomplex ; /* Note state we're generating */

  return TRUE ;
}

/* regenerate_clipping should only be called when regenerating complex
   clipping (i.e. clipping is not CLIPPING_rectangular from find_clipping) */
Bool regenerate_clipping(render_info_t *ri, CLIPOBJECT *newcomplex)
{
  dbbox_t saved_clip;
  Bool result ;

  HQASSERT(ri, "No render info") ;

  /* If not caching clips, we will regenerate the entire stack of clips using
     the bounds of the top complex clip. This allows the clipping form to be
     re-used with different rectangular restrictions on top of it. If we are
     caching clips, we'll regenerate each complex clip with its own bounds,
     so they all can be re-used. */
  saved_clip = ri->clip;

  /* Top complex bounds. This will be overridden if caching clips. */
  if ( !clip_to_bounds(ri, newcomplex, &ri->clip) )
    HQFAIL("clipping regeneration should not be degenerate") ;

  result = regenerate_clipping_recurse(ri, newcomplex) ;

  ri->clip = saved_clip;

  return result ;
}


/** Get the dlc from an object and convert to a device colour if necessary. */
static Bool get_device_dlc(dl_color_t *dlc, Bool *release_out,
                           LISTOBJECT *lobj, const render_info_t *p_ri)
{
  dlc_from_lobj_weak(lobj, dlc);
  /* On-the-fly onversion is done when not rendering to backdrop, but
   * the DL object has a device-independent color (because it overlaps
   * at least one region which is to be backdrop-rendered). */
  if (ri_converting_on_the_fly(p_ri)
      && (lobj->marker & MARKER_DEVICECOLOR) == 0
      /* Exclude some cases where the color will not be used (not a
         problem for rollover_overprints(), since none is overprinted
         anyway, and patterned objects are never rolled over. */
      && !dl_is_none(lobj->p_ncolor)
      && (lobj->spflags & RENDER_PATTERN) == 0) {
    int32 colorType = (p_ri->overrideColorType != GSC_UNDEFINED
                       ? p_ri->overrideColorType
                       : DISPOSITION_COLORTYPE(lobj->disposition));

    if ( !preconvert_on_the_fly(groupRendering(p_ri), lobj, colorType, dlc, dlc) )
      return FALSE;
    /* This device color is not stored and must be freed after use. */
    *release_out = TRUE;
  } else
    *release_out = FALSE;
  return TRUE;
}


/* Render a softmask group. */
static Bool render_softmask(render_info_t *p_ri, SoftMaskAttrib *softmask)
{
  Bool result = TRUE ;
  render_state_t rs ;

  HQASSERT(p_ri, "No render info for softmask") ;
  HQASSERT(softmask->group != NULL, "Softmask group is missing");
  HQASSERT(!p_ri->p_rs->cs.fSelfIntersect,
           "Attempted recursive use of intersectingclipform");

  /* Set up a local renderstate, so we can restrict the softmask compositing
     limits to the minimum size possible (the intersection of the area of the
     objects referencing the softmask and the current render limits). */
  RS_COPY_FROM_RI(&rs, p_ri) ;

  if ( rs.ri.pattern_state == PATTERN_PAINTING ) {
    rs.ri.fSoftMaskInPattern = TRUE ;
  } else {
    dbbox_t bbox ;

    /* Restrict to the bounding box of the objects referencing the
       softmask. We can only do this for non-replicated softmasks, because
       the DL for pattern cells is in a different coordinate space. */
    bbox = groupSoftMaskArea(softmask->group) ;
    bbox_intersection(&rs.cs.bandlimits, &bbox, &rs.cs.bandlimits) ;

    /* Test for degenerate region */
    if ( bbox_is_empty(&rs.cs.bandlimits) )
      return TRUE ;

    rs.ri.clip = rs.ri.bounds = rs.cs.bandlimits ;
    if ( !clip_context_begin(&rs.ri) )
      return FALSE ;
#define return DO_NOT_return!
  }

  result = groupRender(softmask->group, &rs.ri, NULL /* transparency */);

  if ( rs.ri.pattern_state != PATTERN_PAINTING ) {
    clip_context_end(&rs.ri) ;
#undef return
  }

  return result ;
}


/** Test if the object is overprinting or maxblitting in the current set of
    channels. */
static Bool rollover_overprints(Bool *overprints,
                                LISTOBJECT *lobj, const render_info_t *p_ri)
{
  dl_color_t dlc ;
  Bool release_dlc;

  HQASSERT(lobj != NULL, "No DL object") ;
  HQASSERT(p_ri != NULL, "No render info") ;

  if ( !get_device_dlc(&dlc, &release_dlc, lobj, p_ri) )
    return FALSE;

  /* If we're pixel-interleaving, any maxblt or implicit overprint will
     prevent rollovers. If not pixel-interleaving, test the selected channel
     for a missing colorant or a maxblit. */
  *overprints =
    (p_ri->p_rs->fPixelInterleaved ?
     ((lobj->spflags & RENDER_KNOCKOUT) == 0 || dlc_doing_maxblt_overprints(&dlc)) :
     dlc_colorant_is_overprinted(&dlc, blit_map_sole_index(p_ri->p_rs->cs.blitmap)));
  if (release_dlc)
    dlc_release(p_ri->p_rs->page->dlc_context, &dlc);
  return TRUE;
}

Bool clip_context_begin(render_info_t *ri)
{
  Bool result = TRUE ;
  const clip_surface_t *clip_surface ;

  HQASSERT(ri, "No render info") ;
  HQASSERT(ri->surface, "No current surface") ;

  clip_surface = ri->surface->clip_surface ;
  HQASSERT(clip_surface, "No clip surface") ;

  if ( clip_surface->context_begin ) {
    /* Call the clip surface's context begin */
    result = (*clip_surface->context_begin)(ri->p_rs->surface_handle, ri) ;
  }

  return result ;
}

void clip_context_end(render_info_t *ri)
{
  render_tracker_t *tracker ;
  const clip_surface_t *clip_surface ;
  const render_state_t *p_rs ;

  HQASSERT(ri, "No render info") ;

  p_rs = ri->p_rs ;
  HQASSERT(p_rs, "No render state") ;

  HQASSERT(ri->surface, "No current surface") ;

  clip_surface = ri->surface->clip_surface ;
  HQASSERT(clip_surface, "No clip surface") ;

  if ( clip_surface->context_end ) {
    GUCR_COLORANT *hc = p_rs->cs.hc ;

    /* Test if there is a next colorant in band. */
    gucr_colorantsNext(&hc) ;

    /* Call the clip surface's context end routine, passing the context ID,
       all of the information needed to disambiguate the context, and a flag
       indicating if the context might be reused again for a different
       colorant. */
    (*clip_surface->context_end)(p_rs->surface_handle, ri,
                                 gucr_colorantsMore(hc, !GUCR_INCLUDING_PIXEL_INTERLEAVED)) ;
  }

  if ( (tracker = p_rs->cs.renderTracker) != NULL ) {
    /* Some patternshape invocations can be made without a render tracker */
    tracker->clippingformstate = NULL ;
    tracker->oldclip = NULL ;
    tracker->checkstate = TRUE ;
  }
}

/* Initialise the render tracking variables */
void render_tracker_init(render_tracker_t *tracker)
{
  tracker->oldstate = NULL ;
  tracker->oldclip = NULL ;
  tracker->clipping = CLIPPING_rectangular ;
  tracker->clippingformstate = NULL ;
  tracker->checkstate = TRUE ;
  tracker->x1bandclip = MAXDCOORD; tracker->x2bandclip = MINDCOORD;
  tracker->augmented_patternshapeform = FALSE ;
  tracker->oldspotno = SPOT_NO_INVALID;
}

#if 0
/** \todo ajcd 2008-10-16: Rework object map generation to use a
    GUCR_COLORANT. */

/* om_do_knockout -- knockout in a combined object map?
 *
 * Decide whether to knockout or overprint (OR) in a combined object map
 * for all colorants.
 */
Bool om_do_knockout(const render_state_t *p_rs, LISTOBJECT *lobj,
                    const dl_color_t *dlc)
{
  Bool allowMaxBlts;
  size_t i;

  if ( (lobj->spflags & RENDER_KNOCKOUT) != 0 )
    return TRUE;

  allowMaxBlts = guc_allowMaxBlts(p_rs->page);
  if (p_rs->nCiUnique == (uint32)dl_num_channels(lobj->p_ncolor))
    return !allowMaxBlts || !dlc_doing_maxblt_overprints(dlc);
  else if (p_rs->nCiUnique == p_rs->nCi)
    return FALSE; /* colorant missing from dlc */
  else { /* there are duplicates */
    for (i = 0; i < (size_t)p_rs->nCiUnique; i++)
      if (!dlc_indexed_colorant_present(dlc, p_rs->cis[i])
          || (allowMaxBlts && dlc_colorant_is_overprinted(dlc, p_rs->cis[i])))
        return FALSE;
    return TRUE;
  }
}
#endif

PclAttrib *pcl_attrib_from_ri(const render_info_t *p_ri)
{
  return p_ri->lobj->objectstate->pclAttrib;
}

/**
 * Enable PCL pattern blitters as required. Other PCL blitter optimisations
 * are left to the surface's prepare() call.
 */
static void pcl_blit_setup(render_info_t *p_ri,
                           const DL_STATE *page,
                           const surface_t *surface)
{
  PclAttrib* attrib = p_ri->lobj->objectstate->pclAttrib;

  if (attrib == NULL)
    return;

  /* Clear blits from any previous object. */
  CLEAR_BLITS(p_ri->rb.blits, ROP_BLIT_INDEX);
  CLEAR_BLITS(p_ri->rb.blits, PCL_PATTERN_BLIT_INDEX);

  HQASSERT(ROP_BLIT_INDEX == BASE_BLIT_INDEX + 1,
           "Rop blit will write directly to the form.");

  /* The surface has control over when the pattern blit span functions are
     installed. The description below is for the default pattern span
     blitters, however a surface could implement any blitter it wants.

     The pattern blitter is an optimisation, so we don't complain if the
     surface doesn't support the blitter. The default pattern blitter filters
     out the non-white spans in the pattern, and passes them to the
     underlying span function. It does not change the blit color, which is
     set to the foreground color (for objects defined as source black, i.e.,
     rects, fills and character black spans), or the source color (for images
     and character white spans). It is only applicable if the pattern is
     transparent, if the pattern color is white plus a single color (and
     therefore is in the foreground storage, either DL color or attribute),
     and if the ROP doesn't require complex backdrop handling.

     The white span test for source color must be made using all color
     channels.
  */
  /** \todo ajcd 2013-07-03: Determine if we can use these blits for PCL5e as
      well. We should be able to, we may need to remove some of the
      equivalent functionality in the PCL mono blits and may be able to
      optimise the remaining functionality. */
  if ( !page->pcl5eModeEnabled &&
       attrib->patternBlit &&
       surface->pclpatternblits[BLT_CLP_NONE].spanfn != NULL ) {
    p_ncolor_t nc_white ;

    if ( attrib->patternTransparent ) {
      dl_color_t dlc_white ;
      dlc_clear(&dlc_white) ;
      dlc_get_white(page->dlc_context, &dlc_white) ;
      dlc_to_dl_weak(&nc_white, &dlc_white) ;
    } else {
      nc_white = NULL ; /* This won't match any pattern color. */
    }

    SET_BLITS(p_ri->rb.blits, PCL_PATTERN_BLIT_INDEX,
              &surface->pclpatternblits[BLT_CLP_NONE],
              &surface->pclpatternblits[BLT_CLP_RECT],
              &surface->pclpatternblits[BLT_CLP_COMPLEX]);
    SET_BLIT_DATA(p_ri->rb.blits, PCL_PATTERN_BLIT_INDEX, nc_white) ;
  }
}

inline Bool mht_selected(const render_info_t *p_ri, SPOTNO spotno, HTTYPE httype)
{
  const render_state_t *p_rs ;

  HQASSERT(p_ri != NULL, "No render info") ;
  p_rs = p_ri->p_rs ;
  HQASSERT(p_rs, "No render state") ;

  if ( p_rs->htm_info != NULL
       && p_rs->cs.selected_mht != NULL /* MHT HT mask pass */ ) {
    MODHTONE_REF *object_mht =
      ht_getModularHalftoneRef(spotno, httype,
                               blit_map_sole_index(p_rs->cs.blitmap));

    /* Note that multi-spot images, HDLs and groups have
       DISPOSITION_MUST_RENDER, and so are handled through route that will
       select the sub-objects individually. */
    return object_mht == p_rs->cs.selected_mht;
  }
  return TRUE ;
}


/**
 * All the state variables required for rendering a single DL object
 */
typedef struct ONE_OBJECT_STATE {
  pattern_tracker_t PatternInstance;
  render_info_t     ri;
  blit_color_t      color ;
  uintptr_t rollid;
  corecontext_t     *context;
} ONE_OBJECT_STATE;

/**
 * Render a single object from the DL
 * \todo BMJ 31-Jul-08 :  Function too big : simplify and break it down
 */
static Bool render_one_object(ONE_OBJECT_STATE *oos, DLRANGE *dlrange,
                              Bool *done)
{
  LISTOBJECT *lobj = dlrange_lobj(dlrange);
  render_info_t *ri = &oos->ri;
  const surface_t *surface = ri->surface;
  const render_state_t *p_rs = ri->p_rs ;
  render_tracker_t *tracker = p_rs->cs.renderTracker;
  Bool checkstate = tracker->checkstate;
  STATEOBJECT *newstate;
  CLIPOBJECT *newclip;
  int renderCase;
  dl_color_t dlc;
  uint8 spflags;
  SPOTNO spotno;
  HTTYPE httype;
  Bool releaseDLColor;

  enum {
    renderCase_Ordinary,
    renderCase_OverprintAll,
    renderCase_OverprintSome,
    renderCase_MaxOverprint,
    renderCase_MaxOverprintSome,
    renderCase_Pattern,
    renderCase_None
  };

  SwOftenSafe();

  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  ri->lobj = lobj;
  HQASSERT(ri->lobj != NULL, "Link on DL didn't have an object attached");
  HQASSERT(ri->lobj->opcode != RENDER_erase,
           "Erase object should not be rendered in normal loop");

  spflags = ri->lobj->spflags;

  newstate = ri->lobj->objectstate;
  HQASSERT(newstate != NULL, "No state object while rendering DL");
  HQASSERT(newstate->patternstate == NULL ||
           (ri->lobj->opcode != RENDER_hdl &&
            ri->lobj->opcode != RENDER_group),
           "HDL subclass should not be patterned") ;

  /* Use the loop's colors structure unless redirected to the pattern color. */
  ri->rb.color = &oos->color ;
  ri->rb.opmode = BLT_OVP_NONE ;
  ri->rb.maxmode = BLT_MAX_NONE ;

  if ( newstate != tracker->oldstate )
    checkstate = TRUE ;

  /* The spflags should be consistent with the pattern state; either both
     are set, or neither are set. */
  HQASSERT(((spflags & RENDER_PATTERN) != 0) == (newstate->patternstate != NULL),
           "DL object flags not consistent with pattern state") ;

  /* Set up the clipping here. This is not a virtual address: it originates
     hanging off the graphics state during interpretation */
  newclip = newstate->clipstate;
  HQASSERT(newclip != NULL, "No clip state while rendering DL");

  /* The object could be clipped out because it's not in this band's bounds,
     or because the above x2/y2 clipping decrement for tessellating scan
     conversion has made the clipping degenerate. */
  if ( !clip_to_bounds(ri, newclip, &ri->clip) )
    return TRUE;

  /* If we're rendering a backdrop region, the object could be outside of
     the region's current X extent. */
  if ( !bbox_intersects(&ri->lobj->bbox, &ri->clip) )
    return TRUE ;

  /* The only time a transparent object should be seen in direct regions is when
     the object is either completely transparent or completely opaque in the
     direct regions.  If it's transparent everywhere we can simply skip the
     object, otherwise it's opaque and the object is rendered with any
     transparency settings ignored.  Currently this applies only to type 12
     images.  As an exception, during pattern shape generation, a transparent
     object can be seen, but should be handled like an opaque one. */
  if ( (ri->lobj->marker & MARKER_TRANSPARENT) != 0 &&
       (ri->lobj->marker & MARKER_OMNITRANSPARENT) != 0 &&
       ri->region_set_type == RENDER_REGIONS_DIRECT &&
       ri->pattern_state == PATTERN_OFF )
    return TRUE;

  if ( checkstate ) {
    PATTERNOBJECT *oldpattern = oos->PatternInstance.pPattern;
    PATTERNOBJECT *newpattern = newstate->patternstate;
    TranAttrib *newtrans = newstate->tranAttrib ;
    TranAttrib *oldtrans =
      tracker->oldstate ? tracker->oldstate->tranAttrib : NULL;
    SPOTNO newspotno = newstate->spotno;

    /* Only do this stuff if we're doing the final output */
    if ( p_rs->dopatterns ) {
      if (ri->region_set_type == RENDER_REGIONS_BACKDROP &&
          newtrans != NULL && newtrans->softMask != NULL &&
          newtrans->softMask->type != EmptySoftMask &&
          newtrans->softMask->group != NULL &&
          !groupRetainedSoftMask(ri, newtrans->softMask->group) &&
          !surface->suppress_softmasks) {
        /* Composite associated soft mask to make it ready for use in compositing
           the dl object. */
        Bool ok ;
        PROBE(SW_TRACE_RENDER_SOFTMASK,
              (intptr_t)newtrans->softMask,
              ok = render_softmask(ri, newtrans->softMask)) ;
        if ( !ok )
          return FALSE ;
      }

      /* Uncolored patterns use the screen active when painting rather than at
         instantiation. We must therefore track changes in spotno. */
      if ( newspotno != tracker->oldspotno )
        tracker->oldspotno = newspotno ;

      HQASSERT(oldpattern == NULL || oldpattern == newpattern,
               "Should have ended pattern already");

      if ( newpattern != NULL && newpattern != oldpattern ) {
        oos->PatternInstance.pPattern = newpattern;

        oos->PatternInstance.forcepattern = FALSE;

        /* Patterned objects with transparency must be handled individually to
           ensure they composite against each other correctly. Force an
           end_pattern at the end of the render loop */
        if ( newtrans != oldtrans ||
             newpattern->painttype == UNCOLOURED_TRANSPARENT_PATTERN ||
             newpattern->painttype == COLOURED_TRANSPARENT_PATTERN )
          oos->PatternInstance.forcepattern = TRUE;

        /* Detect if the clipping state, pattern state, or RLE Screen ID have
           changed. If we're rendering coloured patterned objects and we can't
           merge them (this happens in a non-knockout group), we need to
           regenerate the pattern clipping for each object individually. */
        if ( !p_rs->fMergePatterns )
          oos->PatternInstance.forcepattern = TRUE;

        if ( !begin_pattern(ri, &oos->PatternInstance, dlrange) )
          return FALSE ;
      }
    }

    {
      /* Clipping checks -- must be after end pattern call,
         which uses the clip. */
      CLIPOBJECT *newcomplex = NULL;
      const clip_surface_t *clip_surface = surface->clip_surface ;
      Bool result = TRUE ;

      HQASSERT(surface->clip_surface != NULL, "No clip surface") ;

      if ( newclip != tracker->oldclip ) {
        newcomplex = find_clipping(newclip, tracker);
      }

      switch ( ri->pattern_state ) {
      case PATTERN_OFF:
        if ( newcomplex ) {
          /* Transition from simple clipping to complex clipping, or complex
             clipping to complex clipping. */
          PROBE(SW_TRACE_RENDER_CLIP,
                (intptr_t)ri->lobj,
                result = regenerate_clipping(ri, newcomplex)) ;
        }
        break ;
      case PATTERN_CLIPPING:
        PROBE(SW_TRACE_RENDER_CLIP,
              (intptr_t)ri->lobj,
              result = pattern_clipping_for_shapes(ri, newcomplex)) ;
        break ;
      case PATTERN_PAINTING:
        PROBE(SW_TRACE_RENDER_CLIP,
              (intptr_t)ri->lobj,
              pattern_clipping_for_cells(ri)) ;
        break ;
      default:
        HQFAIL("Unknown pattern state") ;
        break ;
      }

      if ( !result )
        return FALSE ;

      /* Transition from simple clipping to simple clipping requires no
         action. */
      HQASSERT(tracker->clipping != CLIPPING_firstcomplex,
               "clipping still in transition state after clipping");

      if ( newclip != newcomplex ) {
        /* There is a rectangular clip on top of the top complex clip. Tell
           the surface about it. The surface may also get this information
           through the \c prepare() call. */
        if ( clip_surface->rect_clip ) {
          HQASSERT(newclip->clipno != SURFACE_CLIP_INVALID &&
                   (newcomplex == NULL ||
                    newcomplex->clipno != SURFACE_CLIP_INVALID),
                   "Clip object number is invalid") ;
          if ( !(*clip_surface->rect_clip)(ri->p_rs->surface_handle,
                                           newclip->clipno,
                                           newcomplex == NULL
                                           ? SURFACE_CLIP_INVALID
                                           : newcomplex->clipno,
                                           &newclip->bounds) )
            return FALSE ;
        }
      }

      /* Set clipping if we're in a clipped recursive pattern, or if we've
         got complex clipping */
      if  ( ri->pattern_state == PATTERN_PAINTING ||
            tracker->clipping != CLIPPING_rectangular)
        ri->rb.clipmode = BLT_CLP_COMPLEX ;
#if 0
      /** \todo ajcd 2008-08-21: We can't yet turn on this optimisation,
          because the bbox in the lobj is the intersection of the object's
          bbox and the clip bbox. For this optimisation to work, we need to
          object's bbox to not be intersected with the clip, so we can be
          certain all parts of the object fall within the rectangular clip
          bounds. */
      else if ( bbox_contains(&ri->clip, ri->lobj->bbox) )
        ri->rb.clipmode = BLT_CLP_NONE ;
#endif
      else
        ri->rb.clipmode = BLT_CLP_RECT ;
    }

    /* For pattern (and softmask) purposes, remember the state of the
       previous obj in this dl, ignoring any in sub-dls possibly
       rendered by end pattern function and render_softmask. */
    tracker->oldstate = newstate;
    tracker->checkstate = FALSE ;
  }

  ri->x1maskclip = ri->clip.x1; ri->x2maskclip = ri->clip.x2;

  if ( ri->rb.clipmode == BLT_CLP_COMPLEX ) {
    if ( ri->rb.clipform != &p_rs->forms->clippingform ) {
      /* Cannot use maskclip optimisation in this case, because we are
         clipping through patternshapeform or some other form, not
         clippingform. The maskclip limits are only relevant to
         clippingform. */
      ri->x1maskclip = MAXDCOORD;
      ri->x2maskclip = MINDCOORD;
    } else {
      /* Set largest unclipped band limits in clippingform. */
      if ( tracker->x1bandclip > ri->x1maskclip )
        ri->x1maskclip = tracker->x1bandclip;
      if ( tracker->x2bandclip < ri->x2maskclip )
        ri->x2maskclip = tracker->x2bandclip;
    }
  }

  /* Uncolored patterns use the screen and color active at paint time
     rather than instantiation. So set lobj to the pattern object being
     painted. */
  if (ri->pattern_state == PATTERN_PAINTING) {
    pattern_tracker_t *pattern_tracker ;
    PATTERNOBJECT *pPattern ;

    pattern_tracker = ri->rb.p_painting_pattern ;
    HQASSERT(pattern_tracker != NULL, "No pattern tracker") ;

    pPattern = pattern_tracker->pPattern ;
    HQASSERT(pPattern, "Painting pattern, but no PATTERNOBJECT") ;

    if ( pPattern->painttype == UNCOLOURED_PATTERN ||
         pPattern->painttype == UNCOLOURED_TRANSPARENT_PATTERN ) {
      lobj = pattern_tracker->start_lobj;
      HQASSERT(ri->lobj->opcode != RENDER_image,
               "Images not allowed in uncolored patterns");
    }
  }

  if ( !get_device_dlc(&dlc, &releaseDLColor, lobj, ri) )
    return FALSE;
  spotno = lobj->objectstate->spotno;
  httype = DISPOSITION_REPRO_TYPE(lobj->disposition);
  lobj = NULL; /* make sure we don't use this for anything else */

  /* Determine the render case. */
  if ( dlc_is_none(&dlc) ) {
    renderCase = renderCase_None;
  } else if ( (spflags & RENDER_PATTERN) != 0 ) {
    HQASSERT(ri->pattern_state == PATTERN_CLIPPING,
             "Should be clipping pattern when handling RENDER_PATTERN object") ;
    /* A patterned object inside a painting pattern is a recursive
       pattern, and all of these have pre-generated pattern shapes so we
       can just skip over this object (but we still need to do any
       begin/end pattern calls). */
    if ( ri->rb.p_painting_pattern ) {
      /* A patterned object inside a painting pattern is a recursive
         pattern, and all of these have pre-generated pattern shapes so we
         can just skip over this object (but we still need to do any
         begin_pattern/end_pattern calls). */
      renderCase = renderCase_None; /* already generated the pattern shape */
    } else {
      /* Patterned objects as the top level need their shapes blitting into a
         mask, which will be used to clip the replicated pattern cell
         contents. */
      renderCase = renderCase_Pattern; /* generating the pattern shape now */
    }
  } else {
    Bool selected = mht_selected(ri, spotno, httype);
    object_type_t object_label;

    /* The object label doesn't get used unless the object is rendered. */
    object_label = pixelLabelFromDisposition(ri->lobj->disposition) ;

    /* Unpack the DL color into the blit colour. */
    blit_color_unpack(ri->rb.color, &dlc,
                      object_label /*type*/,
                      ri->lobj->objectstate->lateColorAttrib,
                      (spflags & RENDER_KNOCKOUT) != 0,
                      selected, FALSE, FALSE);

    if ( DISPOSITION_MUST_RENDER(ri->lobj->disposition) ) {
      /* Backdrop images are always rendered; the render properties are dealt
         with inside the backdrop linework expander. Some debug marks are
         rendered with this disposition also, so that they appear on all
         separations. */
      switch ( blit_color_overprinted(ri->rb.color) ) {
      case BLT_OVP_SOME:
        renderCase = renderCase_OverprintSome ;
        break ;
      case BLT_OVP_ALL:
        renderCase = renderCase_OverprintAll ;
        break ;
      default:
        HQFAIL("Unknown overprint state") ;
        /*@fallthrough@*/
      case BLT_OVP_NONE:
        renderCase = renderCase_Ordinary;
        break ;
      }
    } else if ( p_rs->cs.bg_separation && (spflags & RENDER_BACKGROUND) == 0 ) {
      /* This is an optimisation to avoid scanning the whole of the display list
         when rendering the background separation of an imposition. All of the
         background objects must come before all other objects. */
      *done = TRUE;
      return TRUE;
    } else {
      if ( blit_color_maxblitted(ri->rb.color)
           && ( ri->lobj->opcode != RENDER_vignette ||
                !ri->lobj->dldata.vignette->recurse )) {
        /* Only simple objects get maxblitted. Vignettes get maxblitted if
           they are directly implemented, not if they call the renderloop
           recursively (the inner loop does it). */
        HQASSERT(ri->lobj->opcode != RENDER_hdl &&
                 ri->lobj->opcode != RENDER_group,
                 "HDL or group should have been handled by render all disposition") ;
        renderCase = renderCase_MaxOverprint;
        if ( blit_color_overprinted(ri->rb.color) == BLT_OVP_SOME )
          /* Pixel-interleaved maxblit has some channels totally missing. */
          renderCase = renderCase_MaxOverprintSome;
      } else {
        switch ( blit_color_overprinted(ri->rb.color) ) {
        case BLT_OVP_SOME:
          renderCase = renderCase_OverprintSome ;
          break ;
        case BLT_OVP_ALL:
          renderCase = renderCase_OverprintAll ;
          break ;
        default:
          HQFAIL("Unknown overprint state") ;
          /*@fallthrough@*/
        case BLT_OVP_NONE:
          renderCase = renderCase_Ordinary;
          break ;
        }
      }
    }
  }

#if 0
/** \todo ajcd 2008-10-16: Rework object map generation to use a
    GUCR_COLORANT. */

  if ( ri->generate_object_map ) {
    if ( renderCase == renderCase_Pattern )
      CLEAR_BLITS(ri->rb.blits, OM_BLIT_INDEX);
    else {
      if ( p_rs->page->output_object_map
           && !om_do_knockout(p_rs, ri->lobj, &dlc) ) {
        SET_BLITS(ri->rb.blits, OM_BLIT_INDEX,
                  &surface->omblits[BLT_OM_COMBINE][BLT_CLP_NONE],
                  &surface->omblits[BLT_OM_COMBINE][BLT_CLP_RECT],
                  &surface->omblits[BLT_OM_COMBINE][BLT_CLP_COMPLEX]) ;
      } else {
        SET_BLITS(ri->rb.blits, OM_BLIT_INDEX,
                  &surface->omblits[BLT_OM_REPLACE][BLT_CLP_NONE],
                  &surface->omblits[BLT_OM_REPLACE][BLT_CLP_RECT],
                  &surface->omblits[BLT_OM_REPLACE][BLT_CLP_COMPLEX]) ;
      }
    }
  }
#endif

  HQASSERT( ri->rb.outputform->size > 0, "Zero size output form" );

  switch (renderCase) {
    Bool result ;

  case renderCase_None:
  case renderCase_OverprintAll:
    break;

  case renderCase_OverprintSome:
    /* Some colorants in a pixel-interleaved channel are completely missing.
       Set up a packed mask for the overprint blits to use if they want to. */
    blit_overprint_mask(p_rs->cs.overprint_mask, ri->rb.color,
                        blit_channel_present, blit_channel_present) ;
    ri->rb.opmode = BLT_OVP_SOME ;
    /*@fallthrough@*/
  case renderCase_Ordinary:
    /* Set the screen to be used for quantising color values, in case that's
       requested. */
    blit_quantise_set_screen(ri->rb.color, spotno, httype);
    if ( p_rs->cs.fIsHalftone )
      render_gethalftone(ri->ht_params, spotno, httype,
                         blit_map_sole_index(ri->rb.color->map), oos->context);

    pcl_blit_setup(ri, p_rs->page, surface);

    if ( !do_render_function(ri, p_rs->cs.fIsHalftone) )
      return FALSE ;

    break;

  case renderCase_MaxOverprintSome:
    /* Some colorants in a pixel-interleaved channel are completely missing.
       Set up a packed mask for the overprint blits to use if they want to. */
    blit_overprint_mask(p_rs->cs.overprint_mask, ri->rb.color,
                        blit_channel_present, blit_channel_present) ;
    ri->rb.opmode = BLT_OVP_SOME ;
    /*@fallthrough@*/
  case renderCase_MaxOverprint:
    blit_overprint_mask(p_rs->cs.maxblit_mask, ri->rb.color,
                        blit_channel_maxblit, 0);
    ri->rb.maxmode = p_rs->maxmode;
    /* Only set up the maxblit layer if the maxblit mode is implemented. */
    if ( surface->maxblits[ri->rb.maxmode][BLT_CLP_NONE].spanfn != NULL ) {
      SET_BLITS(ri->rb.blits, MAXBLT_BLIT_INDEX,
                &surface->maxblits[ri->rb.maxmode][BLT_CLP_NONE],
                &surface->maxblits[ri->rb.maxmode][BLT_CLP_RECT],
                &surface->maxblits[ri->rb.maxmode][BLT_CLP_COMPLEX]);
    }

    /* Set the screen to be used for quantising color values, in case that's
       requested. */
    blit_quantise_set_screen(ri->rb.color, spotno, httype);
    if ( p_rs->cs.fIsHalftone )
      render_gethalftone(ri->ht_params, spotno, httype,
                         blit_map_sole_index(ri->rb.color->map), oos->context);

    /* Maxblits are incompatible with PCL ROPs, so don't call
       pcl_blit_setup() for this case. */
    HQASSERT(ri->lobj->objectstate->pclAttrib == NULL,
             "PCL should not be maxblitted") ;

    /** \todo ajcd 2008-08-28: Raw vignette needs this setup, but
        structured objects (HDL) shouldn't have the maxblit set. */
    result = do_render_function(ri, p_rs->cs.fIsHalftone) ;

    CLEAR_BLITS(ri->rb.blits, MAXBLT_BLIT_INDEX) ;
    ri->rb.maxmode = BLT_MAX_NONE ;

    if (! result)
      return FALSE ;

    break;

  case renderCase_Pattern:
    ri->rb.color = &mask_black_color;

    /* This is strictly black masking, so don't call pcl_blit_setup() for
       this case. */
    HQASSERT(ri->lobj->objectstate->pclAttrib == NULL,
             "PCL should not be pattern masking") ;

    if ( !do_render_function(ri, TRUE) )
      return FALSE ;
    break;

  default:
    HQFAIL("unrecognised renderCase");
  }

  if ( releaseDLColor )
    dlc_release(p_rs->page->dlc_context, &dlc);
  return TRUE;
}

/**
 * Setup ready for rendering a section of the DL backwwards.
 * Do not actually physically reverse the DL in memory but leave
 * it to the DL iterator to provide it in the reverse order.
 */
static void render_reverse_begin(render_info_t *p_ri, Bool *firsttime)
{
  const render_state_t *p_rs = p_ri->p_rs ;
  const surface_t *surface = p_ri->surface ;

  HQASSERT(surface->intersectblits.spanfn != NULL,
           "Self-intersection blits must be implemented for reverse rendering") ;

  /* Setup sub-dl for self-intersection. We need to reverse the sub-dl and
     set up the special clip form and clip blits. */

  /* If this is the top-level DL we're reversing, then prepare the
     intersection clipping form and blits. */
  *firsttime = !p_rs->cs.fSelfIntersect ;
  if ( *firsttime ) {
    FORM *intersectingclipform = &p_rs->forms->intersectingclipform ;

    /* Sneakily change the self intersection flag of the render state. */
    ((render_state_t *)p_rs)->cs.fSelfIntersect = TRUE;

    /* Pattern rendering does some fairly interesting things with the
       band's height offset, so make sure it is updated to the current
       value before using it. */
    intersectingclipform->hoff = p_ri->rb.outputform->hoff;

    SET_BLITS(p_ri->rb.blits, INTERSECT_BLIT_INDEX,
              &surface->intersectblits,
              &surface->intersectblits,
              &surface->intersectblits);

    /* Set up intersection clipping form. It is set to a solid rectangle
       the size of the bandlimits. We do not force clipping on, but all
       spans will be filtered through the intersection clipping form by
       the intersection blit layer anyway. */
    if ( rleclip_initform(intersectingclipform) ) {
      int32 rh = intersectingclipform->rh ;
      int32 len = intersectingclipform->l ;
      blit_t *spans = intersectingclipform->addr ;
      while ( --rh >= 0 ) {
        if ( !spanlist_insert((spanlist_t *)spans, p_rs->cs.bandlimits.x1,
                              p_rs->cs.bandlimits.x2) )
          HQFAIL("rleclip_initform check for span space") ;
        spans = BLIT_ADDRESS(spans, len) ;
      }
    } else {
      intersectingclipform->type = FORMTYPE_BANDBITMAP;
      area1fill(intersectingclipform);
    }

    SET_BLIT_DATA(p_ri->rb.blits, INTERSECT_BLIT_INDEX, intersectingclipform);
  } else { /* not starting, assert that everything's set up */
    HQASSERT(DOING_BLITS(p_ri->rb.blits, INTERSECT_BLIT_INDEX),
             "No longer doing self-intersection blits");
  }
}

static void render_reverse_end(render_info_t *p_ri, Bool firsttime)
{
  if ( firsttime ) {
    render_state_t *p_rs = (render_state_t *)(p_ri->p_rs) ;
    /* Undo sneaky modification of blit. */
    p_rs->cs.fSelfIntersect = FALSE ;
    CLEAR_BLITS(p_ri->rb.blits, INTERSECT_BLIT_INDEX);
  }
}

/**
 * Render the specified section of the DL backwards
 */
static int32 render_backwards(ONE_OBJECT_STATE *oos, DLRANGE *dlrange)
{
  int32 nobjs = 0;
  Bool first, result = TRUE;

  HQASSERT(!dlrange->forwards, "Supposed to be rendering backwards");
  render_reverse_begin(&oos->ri, &first);
  for ( dlrange_start(dlrange); !dlrange_done(dlrange);
        dlrange_next(dlrange) ) {
    Bool done = FALSE;

    /* Render the rollover objects, using the same loop state
       storage. */
    if ( !render_one_object(oos, dlrange, &done) ) {
      result = FALSE;
      break;
    }
    HQASSERT(!done, "Shouldn't exit early during rollover");
    nobjs++;
  }
  render_reverse_end(&oos->ri, first);

  if ( !result )
    return -1;

  return nobjs;
}

static int32 render_rollovers(ONE_OBJECT_STATE *oos, DLRANGE *dlrange)
{
  const render_state_t *p_rs = oos->ri.p_rs;
  const surface_t *surface = oos->ri.surface ;
  Group *group = groupRendering(&oos->ri);
  DLRANGE seek;
  uint32 numobjects = 0; /* starts at zero to include current object */
#ifdef DEBUG_BUILD /* Union bbox of rollover objects */
  dbbox_t debug_bbox = { MAXDCOORD, MAXDCOORD, MINDCOORD, MINDCOORD };
#endif

  /* Disable rollovers for a surface by setting n_rollover to zero. */
  if ( surface->n_rollover == 0 )
    return 0 ;

  /* Test if the object involves transparency. Err on the side of caution and
     disable rollovers if the enclosing group contains any transparent objects.
     This is done at render time after group elimination. */
  if ( group != NULL &&
       !groupGetAttrs(group)->knockout && hdlTransparent(groupHdl(group)) )
    return 0;

  /* Search for last object in this rollover. */
  dlrange_init(&seek);
  seek.start = dlrange->current;
  seek.end   = dlrange->end;
  for ( dlrange_start(&seek); !dlrange_done(&seek); dlrange_next(&seek) ) {
    LISTOBJECT *next_lobj = dlrange_lobj(&seek);
    Bool overprints;

    if ( (next_lobj->attr.rollover & ~DL_ROLL_POSSIBLE) != oos->rollid )
      break;

    if ( !rollover_overprints(&overprints, next_lobj, &oos->ri) )
      return -1;
    if ( overprints ) {
      /* If the object overprints, we can't roll it over. However, we
         may have other knockout objects with the same rollover ID
         after this, so reset the rollover ID so we can continue
         looking for them. */
      oos->rollid = DL_NO_ROLL;
      break;
    }

    if ( bbox_intersects(&next_lobj->bbox, &oos->ri.bounds) )
      ++numobjects;

#ifdef DEBUG_BUILD
    bbox_union(&debug_bbox, &next_lobj->bbox, &debug_bbox);
#endif
  }

  if ( numobjects >= surface->n_rollover ) {
    int32 nobjs;

    /* If we were doing a pattern when a rollover starts, end it properly,
       before we put a self-intersecting form into place. */
    if ( oos->PatternInstance.pPattern && p_rs->dopatterns ) {
      if ( !end_pattern(&oos->ri, &oos->PatternInstance, dlrange) )
        return -1;
      surface = oos->ri.surface; /* end_pattern changes surface */
    }

    /* Render sections of DL backwards */
    seek.end = seek.current;
    seek.forwards = FALSE;
    if ( (nobjs = render_backwards(oos, &seek)) < 0 )
      return -1;

#ifdef DEBUG_BUILD
    if ( (debug_render & DEBUG_RENDER_ROLLOVER) != 0 ) {
      STATEOBJECT *state = dlrange_lobj(dlrange)->objectstate;
      debug_render_rect(p_rs, debug_bbox.x1, debug_bbox.y1,
                        0, debug_bbox.y2 - debug_bbox.y1,
                        state, TRUE);
      debug_render_rect(p_rs, debug_bbox.x1, debug_bbox.y1,
                        debug_bbox.x2 - debug_bbox.x1, 0,
                        state, TRUE);
      debug_render_rect(p_rs, debug_bbox.x2, debug_bbox.y1,
                        0, debug_bbox.y2 - debug_bbox.y1,
                        state, TRUE);
      debug_render_rect(p_rs, debug_bbox.x1, debug_bbox.y2,
                        debug_bbox.x2 - debug_bbox.x1, 0,
                        state, TRUE);
    }
#endif

    return nobjs;
  }

  return 0;
}

/**
 * render a range of objects to the current band
 */
static Bool render_dlrange(render_info_t *p_ri, DLRANGE *dlrange)
{
  ONE_OBJECT_STATE oos = { 0 };
  Bool done = FALSE;
  const render_state_t *p_rs;

  /* Take copy of render info for safety in recursive rendering */
  HQASSERT(p_ri, "No render info");

  p_rs = p_ri->p_rs;
  HQASSERT(p_rs, "No render state");
  HQASSERT(p_rs->cs.renderTracker, "No render tracking variables");

  oos.context = get_core_context();
  oos.rollid = DL_NO_ROLL; /* No rollover in action */
  RI_COPY_FROM_RI(&oos.ri, p_ri);
  p_rs = oos.ri.p_rs ;
  blit_color_init(&oos.color, p_rs->cs.blitmap) ;

  /* Reset clip flag. This is the same condition that is used during the
     state check below, it resets to the same values as the parent DL's state
     check, if in a sub-DL. */
  if  ( oos.ri.pattern_state == PATTERN_PAINTING ||
        p_rs->cs.renderTracker->clipping != CLIPPING_rectangular)
    oos.ri.rb.clipmode = BLT_CLP_COMPLEX ;
  else
    oos.ri.rb.clipmode = BLT_CLP_RECT ;

  for ( dlrange_start(dlrange);
        !dlrange_done(dlrange) && !done;
        dlrange_next(dlrange) ) { /* Main DL iteration loop... */
    LISTOBJECT *lobj = dlrange_lobj(dlrange);

    HQASSERT(RENDER_INFO_CONSISTENT(&oos.ri), "Render info inconsistent");

    /*
     * Test to see if object falls into the output band.
     * DL may be banded or unbanded. In the banded case this test is
     * not required. But out-of-bounds test is cheap enough that we may
     * as well do it in both cases.
     */
    if ( bbox_intersects(&lobj->bbox, &oos.ri.bounds) ) {
      /* Test for rollovers if the ID is not the same as the last one we
         looked at. */
      if ( (lobj->attr.rollover & ~DL_ROLL_POSSIBLE) != oos.rollid ) {
        /* Note that we've looked at this rollover, so we don't keep
           traversing the same objects. */
        oos.rollid = (lobj->attr.rollover & ~DL_ROLL_POSSIBLE) ;

        /* Only try to rollover sequences that we saw multiple objects in,
           but only when no already reversed for some other reason. If dl
           purging is active, don't bother trying rollovers as it is just to
           hard to try and step through the DL backwards when its on disk. */
        if ( (oos.rollid & DL_ROLL_IT) != 0 &&
             !p_rs->cs.fSelfIntersect && !dlpurge_inuse() ) {
          int32 nobjs;

          if ( (nobjs = render_rollovers(&oos, dlrange)) < 0 )
            return FALSE;

          while ( nobjs > 0 ) {
            nobjs--;
            dlrange_next(dlrange);
          }

          /* We might have finished off the whole DL doing the rollover. */
          if ( dlrange_done(dlrange) )
            break;
        }
      }

      /* Call end_pattern if treating patterned objects individually or if this
         object doesn't continue the previous pattern. */
      if ( oos.PatternInstance.pPattern != NULL && p_rs->dopatterns &&
           (oos.PatternInstance.forcepattern ||
            oos.PatternInstance.pPattern != lobj->objectstate->patternstate) ) {
        if ( !end_pattern(&oos.ri, &oos.PatternInstance, dlrange) )
          return FALSE;
        p_rs->cs.renderTracker->checkstate = TRUE;
      }

      if ( !render_one_object(&oos, dlrange, &done) )
        return FALSE;

#if defined( DEBUG_BUILD )
      {
        LISTOBJECT *viglobj = debug_vignette_red_outline(lobj);

        if ( viglobj )
          if ( !render_single_listobj(p_ri, viglobj) )
            return FALSE;
      }
#endif
    }
  }

  /* If we were doing a pattern when the DL finishes, end it properly. */
  if ( oos.PatternInstance.pPattern != NULL && p_rs->dopatterns )
    if ( !end_pattern(&oos.ri, &oos.PatternInstance, dlrange) )
      return FALSE;

  CLEAR_BLITS(oos.ri.rb.blits, OM_BLIT_INDEX);
  CLEAR_BLITS(oos.ri.rb.blits, ROP_BLIT_INDEX);

  return TRUE;
}

/**
 * Render the current DL object without needing to do all the state checks
 */
static Bool render_simple(render_info_t *p_ri)
{
  const render_state_t *p_rs = p_ri->p_rs ;
  dl_color_t dlc ;
  object_type_t object_label;
  LISTOBJECT *object = p_ri->lobj;
  /* The spotnos of all of the objects should be the same. Use the same
     calculation as the render loop to determine whether to select these
     objects for the blit color unpacking. */
  Bool selected = mht_selected(p_ri, object->objectstate->spotno,
                               DISPOSITION_REPRO_TYPE(object->disposition)) ;

  dlc_clear(&dlc);

  HQASSERT(object != NULL, "No DL object to render");
  HQASSERT((object->spflags & RENDER_PATTERN) == 0,
           "Cannot common render pattern shape");

  /* Need to switch to using the color from the sub dl object. */
  dlc_from_lobj_weak(object, &dlc);

  /* The object label doesn't get set unless the object is rendered. */
  object_label = pixelLabelFromDisposition(object->disposition) ;

  /* Unpack the DL color, ready for normal rendering. The spot numbers
     should all be the same, so we shouldn't need to reset the screen
     information or halftone, and the colorant sets should be
     consistent, so we shouldn't need to check for overprints. */
  blit_color_unpack(p_ri->rb.color, &dlc,
                    object_label /*type*/,
                    object->objectstate->lateColorAttrib,
                    (object->spflags & RENDER_KNOCKOUT) != 0,
                    selected, FALSE, FALSE);

  return do_render_function(p_ri, p_rs->cs.fIsHalftone) ;
}

/**
 * Render all of the DL objects in the given range.
 *
 * Have to deal with the case that the range is specified backwards.
 * This is more difficult if the DL has been purged to disk, as a
 * backwards enumeration is difficult in such cases. So turn of backwards
 * rendering which is purley an optimisation in such cases (i.e. rollovers).
 */
Bool render_object_list_of_band(render_info_t *p_ri, DLRANGE *dlrange)
{
  Bool result = TRUE;
  Bool start_intersect = FALSE;
  Bool backwards = !dlrange->forwards; /* Remember as forwards flag changes */

  if ( dlrange->start.dlref == NULL ) /* Rendering an empty range is a no-op */
    return TRUE;

  if ( backwards ) {
    const surface_t *surface = p_ri->surface ;

    /** \todo ajcd 2008-11-21: If the surface doesn't implement
        self-intersection blits, then it must have some other way of avoiding
        overlaps between elements. Maybe there should be a surface function
        called to indicate the start and end of such sections?
     */
    if ( surface->intersectblits.spanfn != NULL )
      render_reverse_begin(p_ri, &start_intersect);
    else
      backwards = FALSE ; /* Not implemented by output surface. */
  }

  /*
   * The 'common_render' flag is used to indicate that all elements in the DL
   * share enough common properties (especially common clipping) for a special
   * optimised render loop to be used to avoid having to re-check state
   * changes for each object. This allows us to whip through them here.
   */
  if ( !dlrange->common_render ) {
    result = render_dlrange(p_ri, dlrange);
  } else {

    for ( dlrange_start(dlrange);
          result && !dlrange_done(dlrange);
          dlrange_next(dlrange) ) {

      /* Update render info object with next vignette object */
      p_ri->lobj = dlrange_lobj(dlrange);

      result = render_simple(p_ri);
    }
  }

  if ( backwards )
    render_reverse_end(p_ri, start_intersect);

  return result;
}


void render_erase_prepare(render_state_t *p_rs, int32 abs_target_band)
{
  DLREF *dlobj;
  dl_color_t dlcErase;
  dl_color_t dlcKnockout;
  int32 targetBand;
  SPOTNO spotNo ;

  /* Convert band indices into factored indices. */
  targetBand = abs_target_band / p_rs->page->sizedisplayfact;
  HQASSERT(targetBand >= 0 && targetBand < p_rs->page->sizefactdisplaylist,
           "render_erase_prepare: band is not on the display list.");

  /* Erase object is first in dl link list */
  dlobj = dl_get_head(p_rs->page, targetBand);
  HQASSERT(dlobj, "No erase object on target band") ;

  /* Remember erase object, get its DL color and unpack it into the erase
     color. */
  p_rs->lobjErase = dlref_lobj(dlobj);
  HQASSERT(p_rs->lobjErase, "No erase object in target band reference") ;
  p_rs->cs.selected_mht_is_erase = FALSE;

  if ( p_rs->cs.blitmap->nrendered == 0 )
    return; /* skip the rest since nothing will be rendered */

  dlc_from_lobj_weak(p_rs->lobjErase, &dlcErase);
  spotNo = p_rs->lobjErase->objectstate->spotno;
  dlc_copy(p_rs->page->dlc_context, &dlcKnockout,
           &p_rs->page->dlc_knockout);

  if ( p_rs->cs.fIsHalftone ) {
    render_gethalftone(p_rs->ri.ht_params,
                       spotNo, REPRO_TYPE_OTHER /* linework */,
                       blit_map_sole_index(p_rs->cs.blitmap), NULL);
  } else if ( p_rs->cs.selected_mht != NULL ) {
    MODHTONE_REF *mod_ht;

    HQASSERT(p_rs->htm_info != NULL,
             "MHT selected, but not using modular screens") ;
    mod_ht = ht_getModularHalftoneRef(spotNo, REPRO_TYPE_OTHER,
                                      blit_map_sole_index(p_rs->cs.blitmap));

    /* Determine if the mht mask should be erased to black, to have the band
       erased with a modular screen. */
    p_rs->cs.selected_mht_is_erase =
      mod_ht != NULL && mod_ht == p_rs->cs.selected_mht
      /* We only set this flag for the first paint; telling the module to erase
         would overwrite all of the previously partial-painted data. */
      && p_rs->lobjErase->dldata.erase.newpage;
  }

  blit_color_init(p_rs->cs.erase_color, p_rs->cs.blitmap) ;
  blit_color_init(p_rs->cs.knockout_color, p_rs->cs.blitmap) ;

  /* Always unpack and quantise the erase and knockout colors. */
  blit_color_unpack(p_rs->cs.erase_color, &dlcErase,
                    0, /*object type*/
                    p_rs->lobjErase->objectstate->lateColorAttrib,
                    TRUE, /*knockout*/
                    /* Non-mht masks erase to white, mht depend on selection. */
                    p_rs->cs.selected_mht_is_erase,
                    TRUE /* this is an erase color */, FALSE);

  blit_quantise_set_screen(p_rs->cs.erase_color, spotNo, REPRO_TYPE_OTHER);
  blit_color_quantise(p_rs->cs.erase_color) ;

  blit_color_unpack(p_rs->cs.knockout_color, &dlcKnockout,
                    0, /*object type*/
                    p_rs->lobjErase->objectstate->lateColorAttrib,
                    TRUE, /*knockout*/
                    /* Non-mht masks erase to white, mht depend on selection. */
                    p_rs->cs.selected_mht_is_erase,
                    FALSE, TRUE /* this is a knockout color */);

  blit_quantise_set_screen(p_rs->cs.knockout_color, spotNo, REPRO_TYPE_OTHER);
  blit_color_quantise(p_rs->cs.knockout_color) ;
}


Bool render_erase_of_band(render_state_t *p_rs,
                          Bool *ripped_something_out, Bool *do_modular_erase)
{
  Bool result = TRUE ;
  MODHTONE_REF *mod_ht = NULL ; /* By default, not a modular halftone */
  render_info_t ri_copy ;

  RI_COPY_FROM_RS(&ri_copy, p_rs) ;
  HQASSERT(ri_copy.p_rs == p_rs, "Render state pointer inconsistent") ;

  /* Make the erase object the current ripping object. */
  ri_copy.lobj = p_rs->lobjErase ;

  if ( p_rs->cs.fIsHalftone && p_rs->htm_info != NULL
       && p_rs->cs.blitmap->nrendered != 0 )
    mod_ht = ht_getModularHalftoneRef(ri_copy.lobj->objectstate->spotno,
                                      REPRO_TYPE_OTHER /* linework */,
                                      blit_map_sole_index(p_rs->cs.blitmap));

  /* Use the erase color for rendering. It's already unpacked and quantised. */
  ri_copy.rb.color = p_rs->cs.erase_color ;

  *do_modular_erase = FALSE;
  /* If masking for a modular halftone, explicitly set or clear the
     output form. */
  if ( p_rs->cs.selected_mht_is_erase ) {
    /* This is the mask for a modular halftone that is being used for the
       erase, and it's the first paint. We can't guarantee that the modular
       erase is white. */
    area1fill(ri_copy.rb.outputform);
    *p_rs->cs.p_white_on_white = FALSE;
  } else if ( p_rs->cs.selected_mht != NULL ) {
    /* This is the mask for a modular halftone, but it's either not being
       used for the erase, or it's been reloaded from a partial paint. */
    area0fill(ri_copy.rb.outputform);
    *p_rs->cs.p_white_on_white = TRUE;
  } else if ( p_rs->cs.blitmap->nrendered == 0 ) {
    /* This is an omitted or unmapped colorant in a required channel. Set the
       form to zero rather than running the render function, because this is
       not meaningful output, just clear the noise. */
    area0fill(ri_copy.rb.outputform);
    *p_rs->cs.p_white_on_white = TRUE;
  } else if ( mod_ht != NULL ) {
    HQASSERT(p_rs->cs.fIsHalftone,
             "Modular halftone should only be valid for halftoning") ;
    /* This is the final halftone output form for modular halftoning,
       and the erase is a modular halftone (rendered through
       render_objs_of_band_mod_ht()). */
    *do_modular_erase = TRUE;
    *p_rs->cs.p_white_on_white = FALSE; /* The module might not erase white. */
  } else {
    /* All other cases need to render the erase on the first paint,
       including the in-RIP halftone pass when modular halftoning. Erase
       the band with no clipping, and no separation imposition. This makes
       bounds and clip the actual coordinates of the real band, rather than
       the DL band. */
    ri_copy.rb.clipmode = BLT_CLP_NONE;
    bbox_store(&ri_copy.bounds, 0, theFormHOff(*ri_copy.rb.outputform),
               theFormW(*ri_copy.rb.outputform) - 1,
               theFormHOff(*ri_copy.rb.outputform) + theFormRH(*ri_copy.rb.outputform)- 1) ;
    ri_copy.clip = ri_copy.bounds ;
    ri_copy.rb.x_sep_position = ri_copy.rb.y_sep_position = 0 ;

    /* Erase fn will initialize white_on_white. */
    result = do_render_function(&ri_copy, p_rs->cs.fIsHalftone);
  }

  if ( ri_copy.generate_object_map ) {
    area0fill(&p_rs->forms->objectmapform);
    /* Can't w-o-w optimize if producing an object map */
    *p_rs->cs.p_white_on_white = FALSE;
  }

  /* If not white_on_white, we've ripped something that needs outputting.
   * The need for the second test is that, for contone, white_on_white
   * works with comparing the background color vs. the current color
   * (i.e., it's always TRUE to start with), so if the background was
   * originally not zero, then we really do need to output the band.
   */
  HQASSERT(ripped_something_out, "Nowhere to store ripped_something flag") ;
  *ripped_something_out = (!*p_rs->cs.p_white_on_white
                           || ri_copy.lobj->dldata.erase.with0);

  return result;
} /* Function render_erase_of_band */

#if defined(DEBUG_BUILD) || defined(WATERMARK)

/**
 * Populate a rect DL object ready for rendering
 */
static void mk_rect(LISTOBJECT *wm, dcoord x1, dcoord y1, int32 w, int32 h)
{
  wm->bbox.x1 = x1;
  wm->bbox.y1 = y1;
  wm->bbox.x2 = x1 + w;
  wm->bbox.y2 = y1 + h;
}

#endif /* DEBUG_BUILD || WATERMARK */


#if defined(DEBUG_BUILD)

static void debug_render_rect(const render_state_t *p_rs,
                              dcoord x, dcoord y, dcoord w, dcoord h,
                              STATEOBJECT *state, Bool isblack)
{
  dl_color_t dlc ;
  render_state_t rs_local ;
  LISTOBJECT lobj ;
  blit_chain_t debug_blits ;
  blit_color_t debug_color ;

  RS_COPY_FROM_RS(&rs_local, p_rs) ;

  /* Select appropriate rectangle rendering function. */
  dlc_clear(&dlc) ;
  if ( isblack )
    dlc_get_black(p_rs->page->dlc_context, &dlc) ;
  else
    dlc_get_white(p_rs->page->dlc_context, &dlc) ;

  rs_local.ri.lobj = &lobj ;
  rs_local.ri.rb.clipmode = BLT_CLP_RECT ;
  rs_local.ri.rb.x_sep_position = 0 ;
  rs_local.ri.rb.y_sep_position = 0 ;

  rs_local.ri.clip = rs_local.ri.bounds = rs_local.cs.bandlimits ;

  /* Set a local colour. */
  blit_color_init(&debug_color, rs_local.cs.blitmap) ;
  blit_color_unpack(&debug_color, &dlc,
                    0 /*type*/, NULL /* LateColorAttrib */,
                    TRUE, /*knockout*/
                    TRUE, /*selected*/
                    FALSE, /* not erase */
                    FALSE /* not knockout */);
  blit_quantise_set_screen(&debug_color, state->spotno, REPRO_TYPE_OTHER);
  rs_local.ri.rb.color = &debug_color ;

  /* We only want the base blits */
  rs_local.ri.rb.blits = &debug_blits ;
  RESET_BLITS(rs_local.ri.rb.blits,
              rs_local.ri.rb.blits->layer[BASE_BLIT_INDEX].functions[BLT_CLP_NONE],
              rs_local.ri.rb.blits->layer[BASE_BLIT_INDEX].functions[BLT_CLP_RECT],
              rs_local.ri.rb.blits->layer[BASE_BLIT_INDEX].functions[BLT_CLP_COMPLEX]) ;

  init_listobject(&lobj, RENDER_rect, NULL);
  lobj.objectstate = state;
  lobj.marker = MARKER_DEVICECOLOR ;
  DISPOSITION_STORE(lobj.disposition, REPRO_DISPOSITION_RENDER, GSC_FILL, 0) ;
  /* !!! This could fail, but this is debug-only code. */
  ( void )dlc_to_lobj(p_rs->page->dlc_context, &lobj, &dlc) ;

  mk_rect(&lobj, x, y, w, h);

  /* !!! This could fail, but this is debug-only code. */
  (void)do_render_function(&rs_local.ri, rs_local.cs.fIsHalftone) ;
}


/* Draw a stick-figure number with the top-left at the x,y coordinate */
static int debug_render_number(render_state_t *p_rs, dcoord x, dcoord y,
                               STATEOBJECT *state, int number)
{
  size_t i;
  int result = 0;
#define RENDER_DIGIT_SCALE 5
#define RENDER_DIGIT_ADVANCE 8

  typedef struct {
    dcoord x, y, w, h ;
  } irect_t ;

  static irect_t vectors[11][6] = {
    /* 0 */ { {0, 0, 1, 0}, {0, 2, 1, 0}, {0, 0, 0, 2}, {1, 0, 0, 2},
              {0, 0, 0, 0} },
    /* 1 */ { {1, 0, 0, 2}, {0, 0, 0, 0} },
    /* 2 */ { {0, 0, 1, 0}, {0, 1, 1, 0}, {0, 2, 1, 0},
              {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 0, 0} },
    /* 3 */ { {0, 0, 1, 0}, {0, 1, 1, 0}, {0, 2, 1, 0}, {1, 0, 0, 2},
              {0, 0, 0, 0} },
    /* 4 */ { {0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 0, 2}, {0, 0, 0, 0} },
    /* 5 */ { {0, 0, 1, 0}, {0, 1, 1, 0}, {0, 2, 1, 0},
              {0, 0, 0, 1}, {1, 1, 0, 1}, {0, 0, 0, 0} },
    /* 6 */ { {0, 0, 1, 0}, {0, 1, 1, 0}, {0, 2, 1, 0},
              {0, 0, 0, 2}, {1, 1, 0, 1}, {0, 0, 0, 0} },
    /* 7 */ { {0, 0, 1, 0}, {1, 0, 0, 2}, {0, 0, 0, 0} },
    /* 8 */ { {0, 0, 1, 0}, {0, 1, 1, 0}, {0, 2, 1, 0},
              {0, 0, 0, 2}, {1, 0, 0, 2}, {0, 0, 0, 0} },
    /* 9 */ { {0, 0, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 1}, {1, 0, 0, 2},
              {0, 0, 0, 0} },
    /* - */ { {0, 1, 1, 0}, {0, 0, 0, 0} },
  } ;

  if ( number < 0 ) {
    result = debug_render_number(p_rs, x + RENDER_DIGIT_ADVANCE, y, state, -number);
    number = 10 ; /* i.e. the minus sign! */
  } else if ( number > 9 ) {
    result = debug_render_number(p_rs, x, y, state, number / 10);
    x += result * RENDER_DIGIT_ADVANCE ;
    number %= 10 ;
  }

  /* Draw vectors in black, scaled */
  for ( i = 0 ;; ++i ) {
    irect_t *r = &vectors[number][i] ;

    if ( r->w == 0 && r->h == 0 )
      break ;

    debug_render_rect(p_rs,
                      x + r->x * RENDER_DIGIT_SCALE,
                      y + r->y * RENDER_DIGIT_SCALE,
                      r->w * RENDER_DIGIT_SCALE,
                      r->h * RENDER_DIGIT_SCALE,
                      state, TRUE);
  }

  return result + 1 ;
}


/* Show tick-marks at corners of bands, and band number. Use spotno of
   erase colour. */
void render_band_debug_marks(render_state_t *p_rs, int32 dl_bandnum)
{
  DLREF *dlobj = dl_get_head(p_rs->page, dl_bandnum);
  STATEOBJECT *state = dlref_lobj(dlobj)->objectstate;
  dcoord x, y, w, h, tmp;
  dbbox_t corners ;

  bbox_store(&corners, 0, p_rs->forms->retainedform.hoff,
             p_rs->forms->retainedform.w - 1,
             p_rs->forms->retainedform.hoff + p_rs->forms->retainedform.rh - 1) ;

  tmp = corners.x2 / 3 ;
  w = (dcoord)p_rs->page->xdpi / 10 ;
  if ( w > tmp )
    w = tmp ;

  tmp = (corners.y2 - corners.y1) / 3 ;
  h = (dcoord)p_rs->page->ydpi / 10 ;
  if ( h > tmp )
    h = tmp ;

  x = corners.x1; y = corners.y1;
  /* Top left tick marks */
  debug_render_rect(p_rs, x, y, w, 1, state, FALSE);
  debug_render_rect(p_rs, x, y, 1, h, state, FALSE);
  debug_render_rect(p_rs, x, y, w - 1, 0, state, TRUE);
  debug_render_rect(p_rs, x, y, 0, h - 1, state, TRUE);

  /* Render band number */
  (void)debug_render_number( p_rs, x + 2, y + 2, state, dl_bandnum ) ;

  x = corners.x2; y = corners.y2;
  /* Bottom right tick marks */
  debug_render_rect(p_rs, x - w, y - 1, w, 1, state, FALSE);
  debug_render_rect(p_rs, x - 1, y - h, 1, h, state, FALSE);
  debug_render_rect(p_rs, x - w - 1, y, w - 1, 0, state, TRUE);
  debug_render_rect(p_rs, x, y - h - 1, 0, h - 1, state, TRUE);

  *p_rs->cs.p_white_on_white = FALSE ;
}


#endif /* defined(DEBUG_BUILD) */



void init_C_globals_renderloop(void)
{
  blit_colormap_t mask_blitmap;

  blit_colormap_mask(&mask_blitmap);
  blit_color_init(&mask_black_color, &mask_blitmap);
  blit_color_mask(&mask_black_color, FALSE /*black*/);
}


/* Log stripped */
