/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:patternrender.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Pattern renderer
 */

#include "core.h"
#include "patternrender.h"
#include "pattern.h"
#include "patternreplicators.h"
#include "patternshape.h"
#include "timing.h"
#include "display.h"
#include "formOps.h"
#include "spanlist.h"
#include "toneblt.h"
#include "dl_bbox.h"
#include "hdl.h"
#include "group.h"
#include "constant.h"
#include "surface.h"
#include "bitblts.h" /* DO_SPAN */
#include "bitblth.h" /* area1fill, bitclip1, copyform */
#include "clipblts.h" /* bandrleencoded_to_bandbitmap */

/* The pattern clipping/shapes form starts at bbox.x1,y1 */
static void patternclip_do_span(render_blit_t *rb,
                                dcoord y, dcoord xs, dcoord xe)
{
  const dbbox_t *bbox = patternshape_bbox(rb->p_painting_pattern->patternshape) ;

  DO_SPAN(rb, y, xs + bbox->x1, xe + bbox->x1) ;
}

/** Clip the incoming spans to the pattern's key cell complex clip or to the
   pattern shapes at this level in the pattern stack. */
static void patternclipspan(render_blit_t *rb,
                            dcoord y, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy ;
  pattern_recurse_t *recurse_data ;
  form_array_t *formarray ;
  FORM *clipform ;
  const dbbox_t *bbox ;
  dcoord y_mask ;
  Bool clear_base_clipform = FALSE ;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;

  bbox = patternshape_bbox(tracker->patternshape) ;

  {
    dcoord ys = y, ye = y ;

    bbox_clip_y(bbox, ys, ye) ;
    if ( ys > ye )
      return ;
    bbox_clip_x(bbox, xs, xe) ;
    if ( xs > xe )
      return ;
  }

  rb_copy = *rb ;
  rb = &rb_copy ;

  GET_BLIT_DATA(rb_copy.blits, PATTERNCLIP_BLIT_INDEX, formarray) ;
  formarray_findform(formarray, y, &clipform, &y_mask) ;
  HQASSERT(clipform, "Should have found a form for this y value in patternclipspan") ;
  if ( !clipform || theFormT(*clipform) == FORMTYPE_BLANK )
    return ; /* treat span as being clipped out */

  /* rb_copy.clipform is about to be overridden with a pattern shape/clip mask
     and therefore the original clipform needs storing so it can be put back
     later for the base blit clip. */
  GET_BLIT_DATA(rb_copy.blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;
  if ( !recurse_data->base_clipform ) {
    recurse_data->base_clipform = rb_copy.clipform ;
    clear_base_clipform = TRUE ;
  }

  rb_copy.clipform = clipform ;
  HQASSERT(theFormHOff(*rb_copy.outputform) == 0 &&
           theFormHOff(*rb_copy.clipform) == 0,
           "Height offsets of patternclip forms unexpected") ;
  HQASSERT(0 <= y_mask && y_mask < theFormRH(*rb_copy.clipform), "bad y_mask value") ;

  rb_copy.ylineaddr = BLIT_ADDRESS(theFormA(*rb_copy.outputform), y * theFormL(*rb_copy.outputform)) ;
  rb_copy.ymaskaddr = BLIT_ADDRESS(theFormA(*rb_copy.clipform), y_mask * theFormL(*rb_copy.clipform)) ;

  /* The pattern clipping/shapes form starts at bbox.x1,y1 */
  bitclipn(&rb_copy, y, xs - bbox->x1, xe - bbox->x1, patternclip_do_span) ;

  /* The base clipform can change between invocations of the blit stack when
     doing masked images, for example. */
  if ( clear_base_clipform )
    recurse_data->base_clipform = NULL ;
}

static void patternclipblock(render_blit_t *rb,
                             dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  dcoord y ;

  /* Block itself is replicated, so don't replicate underlying blits. */
  BLOCK_USE_NEXT_BLITS(rb->blits) ;

  for ( y = ys ; y <= ye ; ++y ) {
    patternclipspan(rb, y, xs, xe) ;
  }
}

/** Setup the blit stack for the next level of pattern recursion or the base level. */
static void patternrecursespan(render_blit_t *rb,
                               dcoord y, dcoord xs, dcoord xe)
{
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  pattern_tracker_t *context = tracker->pContextPattern ;
  render_blit_t rb_copy ;
  blit_chain_t blits ;
  pattern_recurse_t *recurse_data ;
  form_array_t *formarray ;

  /* Span functions are pre-clipped to the pattern cell clip, so we only need
     copy the render_blit_t to reset the clipform and offsets. We will,
     however, clip to the replication limits. The replication limits were set
     from the bbox of the parent cell. */
  HQASSERT(context != NULL,
           "Pattern should be defined inside another") ;
  HQASSERT(context->pPattern->tilingtype == 0 ?
           bbox_contains(patternshape_bbox(context->patternshape), &tracker->replim) :
           bbox_equal(patternshape_bbox(context->patternshape), &tracker->replim),
           "Replication limits for recursive pattern should be parent's cell bbox") ;

  {
    dcoord ys = y, ye = y ;

    bbox_clip_y(&tracker->replim, ys, ye) ;
    if ( ys > ye )
      return ;
    bbox_clip_x(&tracker->replim, xs, xe) ;
    if ( xs > xe )
      return ;
  }

  /* Copy render_blit so we can reset clipform and offsets. Also copy the
     blits, because we'll reset all of the pattern blits for the new
     recursion level. When we come out of this recursion, we don't want to
     have messed up the original blit pointers but have their masks
     restored. */
  rb_copy = *rb ;
  rb = &rb_copy ;
  blits = *rb->blits ;
  rb->blits = &blits ;

  GET_BLIT_DATA(&blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;

  /* Setting-up the blit stack for the next level of pattern recursion (or the
     base level) has the side-effect of resetting blit_span/block back to the
     top of the blit stack.  Since I don't want to repeat the blit levels above
     the pattern blits I need to ensure those upper blits are cleared. */
  blits.blit_mask &= BLIT_MASK(PATTERNCLIP_BLIT_INDEX) ;

  set_pattern_replication(context, rb, recurse_data) ;

  formarray = patternshape_maskform(recurse_data->top_pattern, context) ;
  /* No formarray implies an orthogonal rectangular clip only. */
  if ( formarray ) {
    const surface_t *surface = rb_copy.p_ri->surface ;
    SET_BLITS(&blits, PATTERNCLIP_BLIT_INDEX,
              &surface->patternclipblits[BLT_CLP_NONE],
              &surface->patternclipblits[BLT_CLP_RECT],
              &surface->patternclipblits[BLT_CLP_COMPLEX]) ;
    SET_BLIT_DATA(&blits, PATTERNCLIP_BLIT_INDEX, formarray) ;
  }

  DO_SPAN(rb, y, xs, xe) ;
}

/** Setup the blit stack for the next level of pattern recursion. */
static void patternrecurseblock(render_blit_t *rb,
                                dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  pattern_tracker_t *context = tracker->pContextPattern ;
  render_blit_t rb_copy ;
  blit_chain_t blits ;
  pattern_recurse_t *recurse_data ;
  form_array_t *formarray ;

  /* Block functions are pre-clipped to the pattern cell clip, so we only need
     copy the render_blit_t to reset the clipform and offsets. We will,
     however, clip to the replication limits. The replication limits were set
     from the bbox of the parent cell. */
  HQASSERT(context != NULL,
           "Pattern should be defined inside another") ;
  HQASSERT(context->pPattern->tilingtype == 0 ?
           bbox_contains(patternshape_bbox(context->patternshape), &tracker->replim) :
           bbox_equal(patternshape_bbox(context->patternshape), &tracker->replim),
           "Replication limits for recursive pattern should be parent's cell bbox") ;

  bbox_clip_x(&tracker->replim, xs, xe) ;
  if ( xs > xe )
    return ;

  bbox_clip_y(&tracker->replim, ys, ye) ;
  if ( ys > ye )
    return ;

  /* Copy render_blit so we can reset clipform and offsets. Also copy the
     blits, because we'll reset all of the pattern blits for the new
     recursion level. When we come out of this recursion, we don't want to
     have messed up the original blit pointers but have their masks
     restored. */
  rb_copy = *rb ;
  rb = &rb_copy ;
  blits = *rb->blits ;
  rb->blits = &blits ;

  GET_BLIT_DATA(&blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;

  /* Setting-up the blit stack for the next level of pattern recursion (or the
     base level) has the side-effect of resetting blit_span/block back to the
     top of the blit stack.  Since I don't want to repeat the blit levels above
     the pattern blits I need to ensure those upper blits are cleared. */
  blits.blit_mask &= BLIT_MASK(PATTERNCLIP_BLIT_INDEX) ;

  set_pattern_replication(context, rb, recurse_data) ;

  formarray = patternshape_maskform(recurse_data->top_pattern, context) ;
  /* No formarray implies an orthogonal rectangular clip only. */
  if ( formarray ) {
    const surface_t *surface = rb_copy.p_ri->surface ;
    SET_BLITS(&blits, PATTERNCLIP_BLIT_INDEX,
              &surface->patternclipblits[BLT_CLP_NONE],
              &surface->patternclipblits[BLT_CLP_RECT],
              &surface->patternclipblits[BLT_CLP_COMPLEX]) ;
    SET_BLIT_DATA(&blits, PATTERNCLIP_BLIT_INDEX, formarray) ;
  }

  DO_BLOCK(rb, ys, ye, xs, xe) ;
}

/** Set up the blit stack for the next level of character recursion. */
static void patternrecursechar(render_blit_t *rb,
                               FORM *formptr, dcoord x, dcoord y)
{
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  pattern_tracker_t *context = tracker->pContextPattern ;
  render_info_t ri_copy ;
  blit_chain_t blits ;
  pattern_recurse_t *recurse_data ;
  form_array_t *formarray ;

  HQASSERT(context != NULL,
           "Pattern should be defined inside another") ;
  HQASSERT(context->pPattern->tilingtype == 0 ?
           bbox_contains(patternshape_bbox(context->patternshape), &tracker->replim) :
           bbox_equal(patternshape_bbox(context->patternshape), &tracker->replim),
           "Replication limits for recursive pattern should be parent's cell bbox") ;


  /* Copy whole of render_info so we can reset clip, since the char blit
     functions require it. Also copy the blits, because we'll reset all of
     the pattern blits for the new recursion level. When we come out of this
     recursion, we don't want to have messed up the original blit pointers
     but have their masks restored. */
  RI_COPY_FROM_RB(&ri_copy, rb) ;
  rb = &ri_copy.rb ;
  bbox_intersection(&ri_copy.clip, &tracker->replim, &ri_copy.clip) ;
  blits = *rb->blits ;
  rb->blits = &blits ;

  GET_BLIT_DATA(&blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;

  /* Setting-up the blit stack for the next level of pattern recursion (or the
     base level) has the side-effect of resetting blit_span/block back to the
     top of the blit stack.  Since I don't want to repeat the blit levels above
     the pattern blits I need to ensure those upper blits are cleared. */
  blits.blit_mask &= BLIT_MASK(PATTERNCLIP_BLIT_INDEX) ;

  set_pattern_replication(context, rb, recurse_data) ;

  formarray = patternshape_maskform(recurse_data->top_pattern, context) ;
  /* No formarray implies an orthogonal rectangular clip only. */
  if ( formarray ) {
    const surface_t *surface = ri_copy.surface ;
    SET_BLITS(&blits, PATTERNCLIP_BLIT_INDEX,
              &surface->patternclipblits[BLT_CLP_NONE],
              &surface->patternclipblits[BLT_CLP_RECT],
              &surface->patternclipblits[BLT_CLP_COMPLEX]) ;
    SET_BLIT_DATA(&blits, PATTERNCLIP_BLIT_INDEX, formarray) ;
  }

  DO_CHAR(rb, formptr, x, y) ;
}

/** The last layer in the pattern recursion blit stack. After this, the
    span continues down to the base blits. */
static void patternbasespan(render_blit_t *rb,
                            dcoord y, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy ;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  pattern_recurse_t *recurse_data ;
  FORM *clipform, *outputform ;

  HQASSERT(tracker->pContextPattern == NULL,
           "Pattern should be defined at root level") ;

  /* Span functions are pre-clipped to the pattern cell clip, so we only need
     copy the render_blit_t to reset the clipform and offsets. We will,
     however, clip to the replication limits. */
  {
    dcoord ys = y, ye = y ;

    bbox_clip_y(&tracker->replim, ys, ye) ;
    if ( ys > ye )
      return ;
    bbox_clip_x(&tracker->replim, xs, xe) ;
    if ( xs > xe )
      return ;
  }

  /* Copy render_blit so we can reset clipform and offsets. */
  rb_copy = *rb ;
  rb = &rb_copy ;

  /* If we frigged the clipform at any point, restore the original one. Also
     reset the separation and form offsets ready for the final blits. */
  GET_BLIT_DATA(rb->blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;
  if ( recurse_data->base_clipform )
    rb_copy.clipform = recurse_data->base_clipform ;

  rb_copy.x_sep_position = tracker->x_sep_position ;
  rb_copy.y_sep_position = tracker->y_sep_position ;

  outputform = rb_copy.outputform ;
  clipform = rb_copy.clipform ;

  theFormHOff(*outputform) = theFormHOff(*clipform) = tracker->oFormHOff;

  rb_copy.ymaskaddr = BLIT_ADDRESS(theFormA(*clipform),
                                   theFormL(*clipform) * (y - theFormHOff(*clipform) - rb_copy.y_sep_position)) ;
  rb_copy.ylineaddr = BLIT_ADDRESS(theFormA(*outputform),
                                   theFormL(*outputform) * (y - theFormHOff(*outputform) - rb_copy.y_sep_position)) ;

  DO_SPAN(rb, y, xs, xe) ;

  theFormHOff(*outputform) = theFormHOff(*clipform) = 0 ;
}

/** The last layer in the pattern recursion blit stack. After this, the
    block continues down to the base blits. */
static void patternbaseblock(render_blit_t *rb,
                             dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  render_blit_t rb_copy ;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  pattern_recurse_t *recurse_data ;
  FORM *clipform, *outputform ;

  HQASSERT(tracker->pContextPattern == NULL,
           "Pattern should be defined at root level") ;

  /* Block functions are pre-clipped to the pattern cell clip, so we only need
     copy the render_blit_t to reset the clipform and offsets. We will,
     however, clip to the replication limits. */
  bbox_clip_x(&tracker->replim, xs, xe) ;
  if ( xs > xe )
    return ;

  bbox_clip_y(&tracker->replim, ys, ye) ;
  if ( ys > ye )
    return ;

  /* Copy render_blit so we can reset clipform and offsets. */
  rb_copy = *rb ;
  rb = &rb_copy ;

  /* If we frigged the clipform at any point, restore the original one. Also
     reset the separation and form offsets ready for the final blits. */
  GET_BLIT_DATA(rb->blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;
  if ( recurse_data->base_clipform )
    rb_copy.clipform = recurse_data->base_clipform ;

  rb_copy.x_sep_position = tracker->x_sep_position ;
  rb_copy.y_sep_position = tracker->y_sep_position ;

  outputform = rb_copy.outputform ;
  clipform = rb_copy.clipform ;

  theFormHOff(*outputform) = theFormHOff(*clipform) = tracker->oFormHOff;

  rb_copy.ymaskaddr = BLIT_ADDRESS(theFormA(*clipform),
                                   theFormL(*clipform) * (ys - theFormHOff(*clipform) - rb_copy.y_sep_position)) ;
  rb_copy.ylineaddr = BLIT_ADDRESS(theFormA(*outputform),
                                   theFormL(*outputform) * (ys - theFormHOff(*outputform) - rb_copy.y_sep_position)) ;

  DO_BLOCK(rb, ys, ye, xs, xe) ;

  theFormHOff(*outputform) = theFormHOff(*clipform) = 0 ;
}

/** The last layer in the pattern recursion blit stack. After this, the
    char continues down to the base blits. */
static void patternbasechar(render_blit_t *rb,
                            FORM *formptr, dcoord x, dcoord y)
{
  render_info_t ri_copy ;
  pattern_tracker_t *tracker = rb->p_painting_pattern ;
  pattern_recurse_t *recurse_data ;
  FORM *clipform, *outputform ;

  HQASSERT(tracker->pContextPattern == NULL,
           "Pattern should be defined at root level") ;

  /* Copy whole of render_info so we can reset clip, since the char blit
     functions require it. */
  RI_COPY_FROM_RB(&ri_copy, rb) ;
  rb = &ri_copy.rb ;

  /* If we frigged the clipform at any point, restore the original one. Also
     reset the separation and form offsets ready for the final blits. */
  GET_BLIT_DATA(rb->blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;
  if ( recurse_data->base_clipform )
    ri_copy.rb.clipform = recurse_data->base_clipform ;

  ri_copy.rb.x_sep_position = tracker->x_sep_position ;
  ri_copy.rb.y_sep_position = tracker->y_sep_position ;
  bbox_intersection(&ri_copy.clip, &tracker->replim, &ri_copy.clip) ;

  outputform = ri_copy.rb.outputform ;
  clipform = ri_copy.rb.clipform ;

  theFormHOff(*outputform) = theFormHOff(*clipform) = tracker->oFormHOff;

  DO_CHAR(rb, formptr, x, y) ;

  theFormHOff(*outputform) = theFormHOff(*clipform) = 0 ;
}

void surface_pattern_builtin(surface_t *surface)
{
  HQASSERT(surface != NULL, "No surface to hook up") ;

  /* Bottom (recursion) layer */
  surface->patternrecurseblits[BLT_RECURSE_BASE].spanfn = patternbasespan ;
  surface->patternrecurseblits[BLT_RECURSE_BASE].blockfn = patternbaseblock ;
  surface->patternrecurseblits[BLT_RECURSE_BASE].charfn = patternbasechar ;
  surface->patternrecurseblits[BLT_RECURSE_BASE].imagefn = imagebltn ;

  surface->patternrecurseblits[BLT_RECURSE_MORE].spanfn = patternrecursespan ;
  surface->patternrecurseblits[BLT_RECURSE_MORE].blockfn = patternrecurseblock ;
  surface->patternrecurseblits[BLT_RECURSE_MORE].charfn = patternrecursechar ;
  surface->patternrecurseblits[BLT_RECURSE_MORE].imagefn = imagebltn ;

  /* Middle (replication layer) */
  pattern_replicators_builtin(surface) ;

  /* Top (clip) layer. Currently no clip optimisations for builtins */
  surface->patternclipblits[BLT_CLP_NONE].spanfn =
    surface->patternclipblits[BLT_CLP_RECT].spanfn =
    surface->patternclipblits[BLT_CLP_COMPLEX].spanfn = patternclipspan ;
  surface->patternclipblits[BLT_CLP_NONE].blockfn =
    surface->patternclipblits[BLT_CLP_RECT].blockfn =
    surface->patternclipblits[BLT_CLP_COMPLEX].blockfn = patternclipblock ;
  surface->patternclipblits[BLT_CLP_NONE].charfn =
    surface->patternclipblits[BLT_CLP_RECT].charfn =
    surface->patternclipblits[BLT_CLP_COMPLEX].charfn = charbltn ;
  surface->patternclipblits[BLT_CLP_NONE].imagefn =
    surface->patternclipblits[BLT_CLP_RECT].imagefn =
    surface->patternclipblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;
}

/** Sets the replication limits to the area covered by the patterned objects. */
static Bool set_replication_limits(render_blit_t* rb,
                                   pattern_tracker_t* pattern_tracker)
{
  DLRANGE *dlrange = &(pattern_tracker->dlrange);

  if ( !pattern_tracker->pParentPattern ) {
    /* The base pattern - need to find the bbox of the patterned
       objects, intersected with the band. */
    PATTERNOBJECT *patobj = pattern_tracker->pPattern ;

    /* Initialise replication limits to the empty set. */
    bbox_clear(&pattern_tracker->replim) ;

    /* Patterned objects for transparent patterns are split into separate
       begin/end pattern calls, but any soft masks in the pattern DL
       are composited once only, for the first patterned object; therefore,
       we need to widen replim to be the same for all the patterned objects. */
    if ( patobj->painttype == COLOURED_TRANSPARENT_PATTERN ||
         patobj->painttype == UNCOLOURED_TRANSPARENT_PATTERN )
      dlrange_setend(&(pattern_tracker->dlrange), NULL);

    for (dlrange_start(dlrange); !dlrange_done(dlrange);
         dlrange_next(dlrange)) {
      LISTOBJECT *lobj = dlrange_lobj(dlrange);

      PATTERNOBJECT *patternstate = lobj->objectstate->patternstate ;
      if ( patternstate && patternstate->patternid == patobj->patternid ) {
        dbbox_t bbox ;
        dlobj_bbox(lobj, &bbox);
        bbox_union(&pattern_tracker->replim, &bbox, &pattern_tracker->replim) ;
      }
    }

    /* Intersect replication limits with current bounds. */
    bbox_intersection(&pattern_tracker->replim,
                      &rb->p_ri->bounds,
                      &pattern_tracker->replim) ;
  } else if ( pattern_tracker->pContextPattern == NULL ) {
    /* A recursive pattern defined outside of any other pattern.
     Find the top pattern parent and use its replim. */
    pattern_tracker->replim = pattern_tracker->pBasePattern->replim ;
  } else {
    pattern_tracker_t *context = pattern_tracker->pContextPattern ;

    /* A recursive pattern defined relative to another pattern. Find the
       shape drawn in the context pattern and use its bbox as the replim. */
    pattern_tracker->replim = *patternshape_bbox(context->patternshape) ;

    /* Untiled patterns inherit their replication limits from their parents.
       If the context was an untiled pattern, the bbox we've got from it
       needs to be restricted to the replication limits in force for the
       context pattern, in case the context is the base pattern, or there is
       a sequence of untiled patterns from the base. */
    if ( context->pPattern->tilingtype == 0 ) {
      bbox_intersection(&pattern_tracker->replim,
                        &context->replim,
                        &pattern_tracker->replim) ;
    }
  }

  return TRUE ;
}

/**
 * The begin/end pattern routines are called when the pattern state
 * changes. The begin function is called before rendering an object which
 * has a non-null pattern state, which has changed from the previous object.
 * It sets up the output form so that the objects using the pattern are
 * imaged into the clipping mask.
 */
Bool begin_pattern(render_info_t *p_ri, pattern_tracker_t *pPatternInstance,
                   DLRANGE *dlrange)
{
  PATTERNOBJECT *thepattern ;
  pattern_tracker_t *parentinstance ;
  render_blit_t *rb = &p_ri->rb ;

  HQASSERT(RENDER_INFO_CONSISTENT(p_ri), "Render context is inconsistent") ;

  HQASSERT(p_ri->p_rs->dopatterns, "Not doing patterns ?");

  HQASSERT(pPatternInstance, "NULL pattern instance passed");

  thepattern = pPatternInstance->pPattern;
  HQASSERT(thepattern, "No pattern object in pattern instance");
  HQASSERT(thepattern->painttype == COLOURED_PATTERN ||
           thepattern->painttype == COLOURED_TRANSPARENT_PATTERN ||
           thepattern->painttype == UNCOLOURED_PATTERN ||
           thepattern->painttype == UNCOLOURED_TRANSPARENT_PATTERN ||
           thepattern->painttype == LEVEL1SCREEN_PATTERN,
           "Strange painttype in begin pattern rendering") ;

  /* Save the render_blit of the render_info used, so we can restore the
     forms and separation offsets which were in force before this pattern.
     Also need to save the blit stack as this will change now. */
  pPatternInstance->saved_rb = *rb ;
  pPatternInstance->saved_blits = *rb->blits ;
  pPatternInstance->saved_white_on_white = *p_ri->p_rs->cs.p_white_on_white;
  pPatternInstance->saved_surface = p_ri->surface;
  /* No need to change p_rs->cs.erase_color, as there will be no unpacking or
     quantisation within the pattern mask. */

  *p_ri->p_rs->cs.p_white_on_white = FALSE;

  /* Set up so that we can render the shapes into the clipping form,
     and use them as clipping when we finish the pattern. */
  /** \todo ajcd 2009-11-02: This might be better to be a specialised
      pattern mask output. It also needs an assign() call. */
  p_ri->surface = &patternshape_surface;
  parentinstance = pPatternInstance->pParentPattern = rb->p_painting_pattern;
  if (parentinstance) {
    HQASSERT(parentinstance->pPattern, "Parent pattern instance doesn't have pattern object") ;
    HQASSERT(parentinstance->pPattern->painttype == COLOURED_PATTERN ||
             parentinstance->pPattern->painttype == COLOURED_TRANSPARENT_PATTERN,
             "Recursion from non-coloured pattern") ;

    /* Inherit base pattern from parent. */
    pPatternInstance->pBasePattern = parentinstance->pBasePattern ;
  } else {
    FORM *patternform = &p_ri->p_rs->forms->patternform;

    /* This is the base pattern. */
    pPatternInstance->pBasePattern = pPatternInstance ;

#if 0 /** \todo Allow pattern form to be RLE clip. */
    if ( conditions_for_bitmap || !rleclip_initform(patternform) )
#endif
    {
      theFormT(*patternform) = FORMTYPE_BANDBITMAP ;
      area0fill(patternform); /* clear the bitmap */
    }

    /* Save information that may be overwritten later */
    pPatternInstance->oFormHOff = theFormHOff(*rb->outputform);
    pPatternInstance->x_sep_position = rb->x_sep_position ;
    pPatternInstance->y_sep_position = rb->y_sep_position ;

    /* output to pattern band */
    rb->outputform = patternform;
    rb->outputform->hoff = pPatternInstance->oFormHOff;

    rb->clipform = &p_ri->p_rs->forms->clippingform;
    rb->clipform->hoff = pPatternInstance->oFormHOff;
  }

  p_ri->pattern_state = PATTERN_CLIPPING;

  dlrange_init(&(pPatternInstance->dlrange));
  dlrange_setstart(&(pPatternInstance->dlrange), dlrange);

  HQASSERT(dlrange_lobj(dlrange) == p_ri->lobj, "Render state object invalid");
  /* Use this object for color/spotno with uncoloured patterns. */
  pPatternInstance->start_lobj = p_ri->lobj;
  HQASSERT(p_ri->lobj->objectstate, "Patterned object with no object state");

  /* Set the transparency state used by this pattern; this is required so that
     the end routine can composite groups using the transparency information
     that was actually used by the patterned object, not the next object */
  pPatternInstance->transparency = p_ri->lobj->objectstate->tranAttrib ;

  CLEAR_BLITS(rb->blits, PATTERNRECURSE_BLIT_INDEX) ;
  CLEAR_BLITS(rb->blits, PATTERN_BLIT_INDEX) ;
  CLEAR_BLITS(rb->blits, PATTERNCLIP_BLIT_INDEX) ;

  /* Set the relative parent of the pattern instance (the pattern instance
     in which it was defined, rather than just used). If a relative parent
     is known to exist, it must also be in use.

     Form offset is zero for a nested pattern relative to any other pattern.
     If the nested pattern was defined in the base context then we need to
     find real form offset. Similarly for the X and Y separation
     positions. */
  if ( thepattern->context_patternid != INVALID_PATTERN_ID ) {
    pattern_tracker_t *relative_parent ;

    for ( relative_parent = parentinstance ;
          relative_parent &&
            relative_parent->pPattern->patternid != thepattern->context_patternid ;
          relative_parent = relative_parent->pParentPattern )
      EMPTY_STATEMENT() ;

    HQASSERT(relative_parent, "Pattern defined inside another, but used outside of that context.") ;

    pPatternInstance->pContextPattern = relative_parent ;
    pPatternInstance->oFormHOff = 0 ;
    pPatternInstance->x_sep_position = 0 ;
    pPatternInstance->y_sep_position = 0 ;
  } else {
    pattern_tracker_t *basepattern = pPatternInstance->pBasePattern ;
    pPatternInstance->pContextPattern = NULL ;
    pPatternInstance->oFormHOff = basepattern->oFormHOff ;
    pPatternInstance->x_sep_position = basepattern->x_sep_position ;
    pPatternInstance->y_sep_position = basepattern->y_sep_position ;
  }

  return TRUE ;
}

static Bool pattern_dl_render(pattern_tracker_t *pPatternInstance,
                              render_state_t *rs, dbbox_t *bounds,
                              dcoord tile_offset_x, dcoord tile_offset_y)
{
  Bool result = TRUE ;
  PATTERNOBJECT *thepattern = pPatternInstance->pPattern;

  if ( !clip_context_begin(&rs->ri) )
    return FALSE ;
#define return DO_NOT_return!

  /* Translate bounds up to the pattern origin; the reverse translation happens
     within the blit stack. */
  bbox_offset(bounds, -tile_offset_x, -tile_offset_y, &rs->ri.bounds);
  pPatternInstance->tile_offset_x = tile_offset_x;
  pPatternInstance->tile_offset_y = tile_offset_y;

  /* Strictly speaking, it's not necessary to update the clip here,
     because it will be done in the pattern render loop, but it helps
     preserve sanity when debugging. */
  rs->ri.clip = rs->ri.bounds ;

  if ( thepattern->opcode == RENDER_group ) {
    PROBE(SW_TRACE_RENDER_PATTERN, (intptr_t)thepattern->dldata.group,
          result = groupRender(thepattern->dldata.group, &rs->ri,
                               pPatternInstance->transparency));
  } else {
    HDL *hdl = patternHdl(thepattern);
    HQASSERT(tranAttribEqual(pPatternInstance->transparency,
                             pPatternInstance->pPattern->ta),
             "Pattern HDL used for different transparency");
    PROBE(SW_TRACE_RENDER_PATTERN, (intptr_t)hdl,
          result = hdlRender(hdl, &rs->ri, NULL, FALSE /* self-intersecting */));
  }

  clip_context_end(&rs->ri) ;

  /* Pattern clipping needs resetting between tiles. */
  rs->cs.renderTracker->oldstate = NULL;

#undef return
  return result;
}

/*
 * The begin/end pattern routines are called when the pattern state
 * changes. The end function is called after rendering the last object
 * which uses a pattern. It renders the sub-DL, replicating the shapes
 * in the pattern through the clipping mask.
 */
Bool end_pattern(render_info_t *p_ri, pattern_tracker_t *pPatternInstance,
                 DLRANGE *dlrange)
{
  pattern_tracker_t *parentinstance = NULL;
  PATTERNOBJECT *thepattern;
  Bool result = TRUE ;
  render_blit_t *rb = &p_ri->rb ;

  HQASSERT(RENDER_INFO_CONSISTENT(p_ri), "Render context is inconsistent") ;

  HQASSERT(p_ri->p_rs->dopatterns, "Not doing patterns ?");

  HQASSERT(pPatternInstance, "NULL pattern instance passed.");

  thepattern = pPatternInstance->pPattern;
  HQASSERT(thepattern, "No pattern object in pattern instance");

  parentinstance = pPatternInstance->pParentPattern ;
  HQASSERT(parentinstance == NULL ||
           parentinstance->pPattern != NULL,
           "Parent pattern instance doesn't have pattern object") ;

  HQASSERT(thepattern->painttype == COLOURED_PATTERN ||
           thepattern->painttype == COLOURED_TRANSPARENT_PATTERN ||
           thepattern->painttype == UNCOLOURED_PATTERN ||
           thepattern->painttype == UNCOLOURED_TRANSPARENT_PATTERN ||
           thepattern->painttype == LEVEL1SCREEN_PATTERN,
           "Strange painttype");

  /* Restore the forms and separation positioning as they were before this
     pattern. */
  HQASSERT(pPatternInstance->saved_rb.p_ri == p_ri,
           "Saved pattern instance does not match render info") ;
  *rb = pPatternInstance->saved_rb ;
  *rb->blits = pPatternInstance->saved_blits ;
  *p_ri->p_rs->cs.p_white_on_white = pPatternInstance->saved_white_on_white;
  p_ri->surface = pPatternInstance->saved_surface;

  dlrange_setend(&(pPatternInstance->dlrange), dlrange);

  if ( !set_replication_limits(rb, pPatternInstance) )
    return FALSE ;

  /* Only render if actual shapes drawn */
  if ( !bbox_is_empty(&pPatternInstance->replim) &&
       (thepattern->opcode == RENDER_hdl || thepattern->opcode == RENDER_group) ) {
    HDL *hdl ;
    render_state_t rs ;
    blit_chain_t blits ;
    pattern_recurse_t recurse_data ;

    /* Make a copy of the full render state, because we're going to fudge the
       band and bandlimits. Output to the form in effect before this pattern
       was created. Recursive patterns are rendered during the HDL virtual
       call below, so will still have the original output form set when
       rendered. */
    RS_COPY_FROM_RI(&rs, p_ri) ;
    blits = *(rs.ri.rb.blits) ;
    rs.ri.rb.blits = &blits ;

    /* Set bounds to pattern HDL's bounding box. */
    hdl = patternHdl(thepattern);
    hdlBBox(hdl, &rs.ri.bounds);

    /* If doing an untiled pattern, restrict the bounds to the replication
       limits. Untiled patterns don't shift the bbox of the pattern, so it is
       in the same coordinate space as the context. The replication limits
       are already restricted to either the bandlimits (if the top pattern),
       the parent pattern cell, or a slice thereof (if in a recursive
       pattern). */
    if ( thepattern->tilingtype == 0 ) {
      bbox_intersection(&rs.ri.bounds, &pPatternInstance->replim, &rs.ri.bounds) ;
    } else {
      /* Replicated cells need to look at the whole display list for the
         cell, not just one band. */
      rs.band = DL_LASTBAND ;
    }

    /* Tiling type 0 intersection with this band may be empty */
    if ( !bbox_is_empty(&rs.ri.bounds) ) {
      if ( parentinstance == NULL ) {
        /* The top pattern can restrict the part of the band which could be
           touched by recursive rendering. */
        bbox_intersection(&rs.cs.bandlimits, &pPatternInstance->replim, &rs.cs.bandlimits) ;

        HQASSERT(!bbox_is_empty(&rs.cs.bandlimits),
                 "Top pattern restriction doesn't intersect band limits") ;

        copyform(&rs.forms->patternform, /*from*/
                 &rs.forms->patternshapeform /*to*/);
      }

      /* Reset the form offsets for the output and clip form for the sub-DL.
         The sub-DL is replicated with respect to (0,0) rather than the Y
         separation position, so that is set to zero as well. */
      theFormHOff(*rs.ri.rb.outputform) = 0 ;
      rs.ri.rb.y_sep_position = rs.ri.rb.x_sep_position = 0 ;

      /* Set the clipform to patternshapeform.  The pattern DL may be clipped
         to several pattern shapes prior to actually being clipped to the
         patternshapeform, but we need to set the patternshapeform here so
         that if we're doing an intersect blit (just prior to the base blit)
         intersectingclipform can be initialised to the patternshapeform
         correctly. */
      rs.ri.rb.clipform = &rs.forms->patternshapeform ;
      theFormHOff(*rs.ri.rb.clipform) = 0;

      NAME_OBJECT(&recurse_data, PATTERNRECURSEDATA_NAME) ;
      recurse_data.top_pattern = pPatternInstance ;
      recurse_data.base_clipform = NULL ;

      set_pattern_replication(pPatternInstance, &rs.ri.rb, &recurse_data);
      rs.ri.pattern_state = PATTERN_PAINTING;

      if ( thepattern->tilingmethod != TILING_HIGH_LEVEL ) {
        /* Any tiling is done by replication in the blit stack. */
        if ( !pattern_dl_render(pPatternInstance, &rs, &rs.ri.bounds, 0, 0) )
          return FALSE;
      } else if ( thepattern->tilingtype == 1 ||
                  thepattern->tilingtype == 3 ) {
        if ( !patterninttiling(pPatternInstance, pattern_dl_render, &rs) )
          return FALSE;
      } else if ( thepattern->tilingtype == 2 ) {
        if ( !patternrealtiling(pPatternInstance, pattern_dl_render, &rs) )
          return FALSE;
      }
    } /* Bounds are not empty */
  } /* Replim not empty, and actually got a DL to render */

  if ( parentinstance == NULL ) { /* reset to original state */
    /* Pattern state to PATTERN_OFF, clear pattern blits etc. */
    pattern_finish(p_ri) ;

    /* Restore offsets of original output, clip form to their previous value */
    theFormHOff(*rb->outputform) =
      theFormHOff(*rb->clipform) = pPatternInstance->oFormHOff;
  } else if ( pPatternInstance->dlrange.end.dlref ) {
    /* Went recursive, now we're unwound we need to revert the pattern_state
       to PATTERN_PAINTING because we're back to painting the top-level pattern dl.
       note, we do this in the original render info, not the local copy. */
    p_ri->pattern_state = PATTERN_PAINTING;
    HQASSERT(rb->p_painting_pattern != NULL,
             "Reverted to painting pattern but no tracker active") ;
    CLEAR_BLITS(rb->blits, PATTERNCLIP_BLIT_INDEX) ;
  }

  /* Ensure that asserts will fail if this pattern instance is used again
     without begin routine being called */
  pPatternInstance->saved_rb.p_ri = NULL ;

  /* This pattern is now complete. */
  pPatternInstance->pPattern = NULL ;

  return result ;
}

/** Could be optimised by saving adjusted xbase, ybase, in
   patternobject, and restore them quickly when resetting to previously
   converged state. */
void set_pattern_replication(pattern_tracker_t *pPatternInstance,
                             render_blit_t *rb,
                             pattern_recurse_t *recurse_data)
{
  int32 tilingtype ;
  unsigned int orthogonal ;
  SYSTEMVALUE xprev = 0.0, yprev = 0.0;
  SYSTEMVALUE yl1, yl2, xl1, xl2 ;
  PATTERNOBJECT *thepattern = pPatternInstance->pPattern;
  const surface_t *surface ;

  HQASSERT(thepattern, "No pattern to reset to in set_pattern_replication") ;

  tilingtype = thepattern->tilingtype ;
  orthogonal = BLT_CELL_NONORTH ;

  if ( tilingtype != 0 ) { /* Replicated tiling may use special span functions */
    SYSTEMVALUE xstepx = thepattern->xx;
    SYSTEMVALUE xstepy = thepattern->xy;
    SYSTEMVALUE ystepx = thepattern->yx;
    SYSTEMVALUE ystepy = thepattern->yy;
    SYSTEMVALUE xbase, ybase;

    HQASSERT(fabs(xstepx * ystepy) >= fabs(ystepx * xstepy),
             "X step vector steeper in set_pattern_replication") ;

    /* N.B. the "thepattern->bsize?" variables are already negated */

    /* xbase,ybase is the top-left corner of the proposed replication bbox.
       We'll be adding multiples of the stepping vectors until we find an
       xbase/ybase in the area we need.  (y_sep_position is set to zero just
       before this function is called, so don't include it in this
       calculation.) */
    xbase = (SYSTEMVALUE)thepattern->bbx;
    ybase = (SYSTEMVALUE)thepattern->bby;

    /* xl1,yl1,xl2,yl2 is the area where we're trying to find an xbase,ybase.
       xl1,yl1 is the top-left of the replication area stepped back a full
       pattern cell bbox.  xl2,yl2 is half a xstepx and half a ystepy from
       xl1,yl1.  The factor of a half is to ensure rotated patterns (especially
       long thin cells at 45 degrees) are handled properly (request 62550
       contains a diagram illustrating this case). */
    xl1 = thepattern->bsizex + (SYSTEMVALUE)pPatternInstance->replim.x1;
    xl2 = min(xl1 + 0.5 * xstepx, (SYSTEMVALUE)pPatternInstance->replim.x1);

    if ( ystepx <= 0.0 ) {
      yl1 = thepattern->bsizey + (SYSTEMVALUE)pPatternInstance->replim.y1;
      yl2 = min(yl1 + 0.5 * ystepy, (SYSTEMVALUE)pPatternInstance->replim.y1);

      /* shift base to above topleft corner of (used part of) form.  */
      while ( ybase > yl2 || ybase < yl1 || xbase > xl2 || xbase < xl1 ) {
        /* these are done using divide, roundtoint, multiply, because they
         * typically involve many steps, i.e. i is not typically 0 or 1.
         * Similar code in replication procs is done with repeated addition
         * because there, i would mostly be 0, next likely 1, unlikely 2...etc.
         */
        if ( ybase < yl1 ) {
          int32 i = (int32)((yl1 - ybase)/ystepy ) /* + 1 */;
          ybase += ystepy * (SYSTEMVALUE)i;
          xbase += ystepx * (SYSTEMVALUE)i;
        }
        if ( ybase >= yl2 ) {
          int32 i = (int32)((ybase - yl2)/ystepy) + 1;
          ybase -= ystepy * (SYSTEMVALUE)i;
          xbase -= ystepx * (SYSTEMVALUE)i;
        }
        if ( xbase < xl1 ) {
          int32 i = (int32)((xl1 - xbase)/xstepx ) /* + 1 */;
          xbase += xstepx * (SYSTEMVALUE)i;
          ybase += xstepy * (SYSTEMVALUE)i;
        }
        if ( xbase >= xl2 ) {
          int32 i = (int32)((xbase - xl2)/xstepx) + 1;
          xbase -= xstepx * (SYSTEMVALUE)i;
          ybase -= xstepy * (SYSTEMVALUE)i;
        }

        HQTRACE( debug_pattern,
                 ("xbase %g, ybase %g : steps x:%g,%g y:%g,%g\nlimits x:%g,%g, y:%g,%g, prev %g,%g\n",
                  xbase,ybase,xstepx,xstepy,ystepx,ystepy,xl1,xl2,yl1,yl2,xprev,yprev)) ;

        if ( (fabs(xbase - xprev) < EPSILON) && (fabs(ybase - yprev) < EPSILON) )
          /* this is the only suitable value, and we have looped because
           * the step is greater than the size, and no grid point within size
           * of the origin can be found.
           */
          break;

        if ( xbase < xl2 && ybase < yl2 )
          xprev = xbase, yprev = ybase;
      }
    } else {                    /* ystepx is in +x direction */
      yl1 = (SYSTEMVALUE)pPatternInstance->replim.y2;
      yl2 = yl1 + 0.5 * ystepy;

      /* shift base to below bottomleft corner of (used part of) form.  */
      while ( ybase > yl2 || ybase < yl1 || xbase > xl2 || xbase < xl1 ) {
        /* these are done using divide, roundtoint, multiply, because they
         * typically involve many steps, i.e. i is not typically 0 or 1.
         * Similar code in replication procs is done with repeated addition
         * because there, i would mostly be 0, next likely 1, unlikely 2...etc.
         */
        if ( ybase > yl2 ) {
          int32 i = (int32)((ybase - yl2)/ystepy ) /* + 1 */;
          ybase -= ystepy * (SYSTEMVALUE)i;
          xbase -= ystepx * (SYSTEMVALUE)i;
        }
        if ( ybase <= yl1 ) {
          int32 i = (int32)((yl1 - ybase)/ystepy) + 1;
          ybase += ystepy * (SYSTEMVALUE)i;
          xbase += ystepx * (SYSTEMVALUE)i;
        }
        if ( xbase < xl1 ) {
          int32 i = (int32)((xl1 - xbase)/xstepx ) /* + 1 */;
          xbase += xstepx * (SYSTEMVALUE)i;
          ybase += xstepy * (SYSTEMVALUE)i;
        }
        if ( xbase >= xl2 ) {
          int32 i = (int32)((xbase - xl2)/xstepx) + 1;
          xbase -= xstepx * (SYSTEMVALUE)i;
          ybase -= xstepy * (SYSTEMVALUE)i;
        }

        HQTRACE( debug_pattern,
                 ("xbase %g, ybase %g : steps x:%g,%g y:%g,%g\nlimits x:%g,%g, y:%g,%g, prev %g,%g\n",
                  xbase,ybase,xstepx,xstepy,ystepx,ystepy,xl1,xl2,yl1,yl2,xprev,yprev)) ;

        if ( (fabs(xbase - xprev) < EPSILON) && (fabs(ybase - yprev) < EPSILON) )
          /* this is the only suitable value, and we have looped because
           * the step is greater than the size, and no grid point within size
           * of the origin can be found.
           */
          break;

        if ( xbase < xl2 && ybase > yl1 )
          xprev = xbase, yprev = ybase;
      }

      /* We're now below the bottom left. We now need to adjust the point to be
         above the top left, by subtracting enough ysteps */
      yl2 = min(thepattern->bsizey
                + (SYSTEMVALUE)pPatternInstance->replim.y1 + ystepy,
                (SYSTEMVALUE)pPatternInstance->replim.y1);

      if ( ybase >= yl2 ) {
        int32 i = (int32)((ybase - yl2)/ystepy) + 1;
        ybase -= ystepy * (SYSTEMVALUE)i;
        xbase -= ystepx * (SYSTEMVALUE)i;
      }
    }

    if ( xstepy != 0.0 ) {
      /* Rotated pattern: move back one column (need only be one column) as
         this column may contribute, depending on replication limits.  A
         graphical explanation of this is provided in Request 51114. */
      xbase -= xstepx ;
      ybase -= xstepy ;
    }

    if ( tilingtype != 2 ) {
      dcoord ixbase, iybase;

      /* The xbase and ybase coordinates should be rounded down, to preserve the
         distance between pattern cells (xbase and ybase can converge to
         negative coordinates). */
      ixbase = (int32)xbase;
      if ( xbase < (SYSTEMVALUE)ixbase ) /* Did it truncate up towards zero? */
        ixbase-- ;
      iybase = (int32)ybase;
      if ( ybase < (SYSTEMVALUE)iybase ) /* Did it truncate up towards zero? */
        iybase-- ;
      pPatternInstance->ixbase = ixbase; pPatternInstance->iybase = iybase;
      if ( (dcoord)xstepy == 0 && (dcoord)ystepx == 0 )
        orthogonal = BLT_CELL_ORTHOGONAL ;
    }
    else {
      if ( xstepy == 0.0 && ystepx == 0.0 )
        orthogonal =  BLT_CELL_ORTHOGONAL ;
    }

    pPatternInstance->xbase = xbase; pPatternInstance->ybase = ybase;
  }

  surface = rb->p_ri->surface ;

  if ( pPatternInstance->pContextPattern ) { /* Set up for recursive pattern. */
    SET_BLITS(rb->blits, PATTERNRECURSE_BLIT_INDEX,
              &surface->patternrecurseblits[BLT_RECURSE_MORE],
              &surface->patternrecurseblits[BLT_RECURSE_MORE],
              &surface->patternrecurseblits[BLT_RECURSE_MORE]) ;
  } else { /* Final level of replication. */
    SET_BLITS(rb->blits, PATTERNRECURSE_BLIT_INDEX,
              &surface->patternrecurseblits[BLT_RECURSE_BASE],
              &surface->patternrecurseblits[BLT_RECURSE_BASE],
              &surface->patternrecurseblits[BLT_RECURSE_BASE]) ;
  }
  SET_BLIT_DATA(rb->blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;

  HQASSERT(orthogonal == BLT_CELL_ORTHOGONAL ||
           orthogonal == BLT_CELL_NONORTH,
           "Pattern cell orthogonality indicator invalid") ;
  HQASSERT(tilingtype >= 0 && tilingtype < BLT_TILE_N,
           "Pattern cell tiling type indicator invalid") ;
  if ( thepattern->tilingmethod == TILING_HIGH_LEVEL ) { /* High-level tiling */
    SET_BLITS(rb->blits, PATTERN_BLIT_INDEX,
              &surface->patterntranslateblits,
              &surface->patterntranslateblits,
              &surface->patterntranslateblits) ;
  } else { /* Blit-level tiling or no tiling */
    SET_BLITS(rb->blits, PATTERN_BLIT_INDEX,
              &surface->patternreplicateblits[orthogonal][tilingtype],
              &surface->patternreplicateblits[orthogonal][tilingtype],
              &surface->patternreplicateblits[orthogonal][tilingtype]) ;
  }
  CLEAR_BLITS(rb->blits, PATTERNCLIP_BLIT_INDEX) ;
  rb->p_painting_pattern = pPatternInstance;
}

/** Switches pattern state to PATTERN_OFF and nulls the painting pattern.
    Also the pattern blits are cleared and therefore the caller may need
    to copy the blit stack into a local temporary blit_chain_t structure. */
void pattern_finish(render_info_t *ri)
{
  ri->pattern_state = PATTERN_OFF ;
  ri->rb.p_painting_pattern = NULL ;
  CLEAR_BLITS(ri->rb.blits, PATTERNRECURSE_BLIT_INDEX) ;
  CLEAR_BLITS(ri->rb.blits, PATTERN_BLIT_INDEX) ;
  CLEAR_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX) ;
}

/** For recursive patterns which are not defined in the context of their
  immediate parent we need to generate a new mask which is a combination of
  the normal mask augmented with the pattern shapes from another pattern.
  The outmost parent pattern is augmented at render time. */
static void augment_patternshapeform(render_blit_t *rb,
                                     pattern_tracker_t *next_pattern)
{
  render_info_t ri ;
  blit_chain_t blits ;
  pattern_recurse_t *recurse_data ;
  form_array_t *formarray ;
  FORM *form ;
  uint32 nforms ;
  dcoord y ;

  static blit_slice_t patternshapeslice = {
    bitclip1, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
  } ;

  RI_COPY_FROM_RB(&ri, rb) ;
  rb = &ri.rb ;
  blits = *rb->blits ;
  rb->blits = &blits ;

  HQASSERT(next_pattern->pPattern->context_patternid == INVALID_PATTERN_ID &&
           next_pattern->pContextPattern == NULL,
           "Only need to call augment_patternshapeform for patterns relative to the base") ;

  rb->outputform = &ri.p_rs->forms->patternclipform ;
  rb->clipform = &ri.p_rs->forms->patternshapeform ;
  rb->depth_shift = 0;

#if 0 /** \todo Allow pattern form to be RLE clip. */
  if ( conditions_for_bitmap || !rleclip_initform(rb->outputform) )
#endif
  theFormT(*rb->outputform) = FORMTYPE_BANDBITMAP ;
  area0fill(rb->outputform) ;

  GET_BLIT_DATA(rb->blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;
  formarray = patternshape_maskform(recurse_data->top_pattern, next_pattern) ;
  HQASSERT(formarray, "should always have a pattern shape mask here") ;

  set_pattern_replication(next_pattern, rb, recurse_data) ;
  /* PATTERNCLIP_BLIT_INDEX already cleared by set_pattern_replication. */
  SET_BLITS(&blits, BASE_BLIT_INDEX,
            &patternshapeslice, &patternshapeslice, &patternshapeslice) ;

  /* This is what the subsequent charbltn uses to clip the mask it is going
     to render; we want to render the whole mask. */
  ri.clip = *patternshape_bbox(next_pattern->patternshape) ;

  y = formarray->bbox.y1 ;
  form = formarray->forms ;
  for ( nforms = formarray->nforms ; nforms > 0 ; --nforms ) {
    if ( theFormT(*form) == FORMTYPE_BANDBITMAP )
      charbltn(rb, form, ri.clip.x1, y) ;
    else if ( theFormT(*form) == FORMTYPE_BANDRLEENCODED )
      charbltspan(rb, form, ri.clip.x1, y) ;
    y += theFormRH(*form) ;
    ++form ;
  }

  copyform(&ri.p_rs->forms->patternclipform /* from */,
           &ri.p_rs->forms->patternshapeform /* to */) ;
  ri.p_rs->cs.renderTracker->augmented_patternshapeform = TRUE ;
}

/** Setup the patternshapeform for the current stack of patterns. */
static void set_patternshapeform(render_blit_t *rb)
{
  pattern_tracker_t *context_pattern ;
  pattern_recurse_t *recurse_data ;
  pattern_tracker_t *next_pattern ;
  const render_state_t *p_rs = rb->p_ri->p_rs ;

  /* Softmasks inside patterns need careful handling.  Transparent patterns
     force patterned objects into separate begin/end pattern calls
     and this means the patternshapeform only ever contains one patterned
     object at a time.  Since the softmask is indirectly used over all the
     patterned objects, make the patternshapeform solid just for the
     softmask.  The patternshapeform is subsequently reset (in this routine)
     before rendering the pattern DL objects. */
  if ( rb->p_ri->fSoftMaskInPattern ) {
    FORM *patternshapeform = &p_rs->forms->patternshapeform ;
    HQASSERT(rb->clipform == patternshapeform,
             "Expected the clipform to be the patternshapeform") ;
    theFormT(*patternshapeform) = FORMTYPE_BANDBITMAP ;
    area1fill(patternshapeform) ;
    p_rs->cs.renderTracker->augmented_patternshapeform = TRUE ;
    return ;
  }

  /* Regenerate patternshapeform that was augmented for the last pattern. */
  if ( p_rs->cs.renderTracker->augmented_patternshapeform ) {
    copyform(&p_rs->forms->patternform /*from*/,
             &p_rs->forms->patternshapeform /*to*/) ;
    p_rs->cs.renderTracker->augmented_patternshapeform = FALSE ;
  }

  GET_BLIT_DATA(rb->blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
  VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;

  /* Look for any patterns that are not relative to their immediate parent
     and incorporate their pattern shapes into the final
     patternshapeform. */
  context_pattern = recurse_data->top_pattern->pContextPattern ;
  for ( next_pattern = recurse_data->top_pattern->pParentPattern ;
        next_pattern ;
        next_pattern = next_pattern->pParentPattern ) {
    if ( next_pattern == context_pattern ) {
      context_pattern = next_pattern->pContextPattern ;
    } else if ( next_pattern->pContextPattern == NULL ) {
      augment_patternshapeform(rb, next_pattern) ;
    }
  }
}

Bool pattern_clipping_for_shapes(render_info_t *ri, CLIPOBJECT *newcomplex)
{
  render_tracker_t *tracker = ri->p_rs->cs.renderTracker ;

  HQASSERT(ri->pattern_state == PATTERN_CLIPPING,
           "Pattern state should be clipping") ;

  if ( ri->rb.p_painting_pattern ) {
    /* This state means we already have the patternshape, and therefore we
       just want to skip over the DL object as quickly as possible without
       rendering it (but still going into its recursive pattern if
       present). */

    HQASSERT(ri->rb.p_painting_pattern, "p_painting_pattern is missing") ;
    ri->rb.p_painting_pattern->patternshape = ri->lobj->objectstate->patternshape ;

    /* Setting clipping to CLIPPING_rectangular is the cheapest thing. */
    tracker->clipping = CLIPPING_rectangular ;
    CLEAR_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX) ;

    HQASSERT(!DOING_BLITS(ri->rb.blits, PATTERNRECURSE_BLIT_INDEX),
             "Should not be doing pattern recurse blit") ;
    HQASSERT(!DOING_BLITS(ri->rb.blits, PATTERN_BLIT_INDEX),
             "Should not be doing pattern blit") ;
    HQASSERT(!DOING_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX),
             "Should not be doing a pattern clip blit") ;
  } else {
    /* Generating the patternshapes (during interpretation) or generating
       the outer-most pattern's patternshapeform (during rendering). */

    HQASSERT(!ri->rb.p_painting_pattern, "p_painting_pattern should be null") ;

    if ( newcomplex ) {
      /* Transition from simple clipping to complex clipping, or complex
         clipping to complex clipping. */
      if ( !regenerate_clipping(ri, newcomplex) )
        return FALSE ;
    } else {
      if ( tracker->clipping == CLIPPING_complex ) {
        /* Same complex clip as before. */
        if ( theFormT(*ri->rb.clipform) == FORMTYPE_BANDRLEENCODED ) {
          /** \todo Change pattern generation so shape form can use RLE
              clip.

              Now doing a pattern so we need to convert the RLE clip
              to a bitmap, because patterns can't cope with RLE directly. */
          bandrleencoded_to_bandbitmap(ri->rb.clipform,
                                       ri->p_rs->forms->halftonebase,
                                       0, theFormW(*ri->rb.clipform) - 1) ;
        }
      }
    }

    HQASSERT(!DOING_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX),
             "Should not be doing a pattern clip blit") ;
  }

  return TRUE ;
}

void pattern_clipping_for_cells(render_info_t *ri)
{
  render_tracker_t *tracker = ri->p_rs->cs.renderTracker ;

  HQASSERT(ri->pattern_state == PATTERN_PAINTING,
           "Pattern state should be painting") ;

  /* Painting a pattern's DL through its clipping and pattern shapes, and
     through any parent pattern. */

  HQASSERT(ri->rb.p_painting_pattern, "p_painting_pattern is missing") ;
  ri->rb.p_painting_pattern->patternshape = ri->lobj->objectstate->patternshape ;

  HQASSERT(DOING_BLITS(ri->rb.blits, PATTERNRECURSE_BLIT_INDEX),
           "Should have set PATTERNRECURSE_BLIT_INDEX") ;

  if ( tracker->clipping == CLIPPING_rectangular ) {
    /* A simple rectangular clip. */
    CLEAR_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX) ;
  } else if ( tracker->oldstate == NULL ||
              ri->lobj->objectstate->patternshape
              != tracker->oldstate->patternshape ) {
    /* Find the pattern's clip shape mask and setup a pattern clip blit. */
    pattern_recurse_t *recurse_data ;
    form_array_t *formarray ;

    GET_BLIT_DATA(ri->rb.blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
    VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;

    formarray = patternshape_clipform(recurse_data->top_pattern->patternshape) ;
    if ( formarray ) {
      const surface_t *surface = ri->surface ;
      SET_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX,
                &surface->patternclipblits[BLT_CLP_NONE],
                &surface->patternclipblits[BLT_CLP_RECT],
                &surface->patternclipblits[BLT_CLP_COMPLEX]) ;
      SET_BLIT_DATA(ri->rb.blits, PATTERNCLIP_BLIT_INDEX, formarray) ;
      tracker->clipping = CLIPPING_complex ;
    } else {
      CLEAR_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX) ;
      tracker->clipping = CLIPPING_rectangular ;
    }
  }

  /* Prepare patternshapeform for this pattern.  This means doing or
     undoing any pattern augmentation in patternshapeform. */
  if ( tracker->oldstate == NULL ||
       ri->lobj->objectstate->patternshape != tracker->oldstate->patternshape )
    set_patternshapeform(&ri->rb) ;

#if defined( ASSERT_BUILD )
  HQASSERT(DOING_BLITS(ri->rb.blits, PATTERNRECURSE_BLIT_INDEX),
           "Should be doing pattern recurse blit") ;
  HQASSERT(DOING_BLITS(ri->rb.blits, PATTERN_BLIT_INDEX),
           "Should be doing pattern blit") ;
  if ( ri->p_rs->cs.renderTracker->clipping == CLIPPING_rectangular ) {
    HQASSERT(!DOING_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX),
             "Should not be doing a pattern clip blit for a rectangular clip") ;
  } else {
    pattern_recurse_t *recurse_data ;
    form_array_t *formarray ;
    HQASSERT(DOING_BLITS(ri->rb.blits, PATTERNCLIP_BLIT_INDEX),
             "Should be doing a pattern clip blit for a complex clip") ;
    GET_BLIT_DATA(ri->rb.blits, PATTERNRECURSE_BLIT_INDEX, recurse_data) ;
    VERIFY_OBJECT(recurse_data, PATTERNRECURSEDATA_NAME) ;
    GET_BLIT_DATA(ri->rb.blits, PATTERNCLIP_BLIT_INDEX, formarray) ;
    HQASSERT(formarray == patternshape_clipform(recurse_data->top_pattern->patternshape),
             "Wrong clip shape form set in pattern clip blit data") ;
  }
#endif
}


/* Log stripped */
