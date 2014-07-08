/** \file
 * \ingroup backdrop
 *
 * $HopeName: CORErender!src:backdropblt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blitting functions for backdrop.
 */

#include "core.h"
#include "swerrors.h"
#include "bitblts.h"
#include "backdropblt.h"
#include "blttables.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "surface.h"
#include "builtin.h"
#include "display.h"
#include "render.h"
#include "toneblt.h"
#include "dlstate.h"
#include "imageo.h" /* IM_FLAG_PRESEP_KO_PROCESS */
#include "backdrop.h"
#include "renderbd.h"
#include "params.h" /* SystemParams */

static surface_prepare_t backdropblitinfo(surface_handle_t handle,
                                          render_info_t* p_ri) ;
static void backdrop_blitmap_optimise(blit_colormap_t *map) ;
static void bitfillbackdrop(render_blit_t *rb,
                            dcoord y, dcoord xsp, dcoord xep);
static void bitclipbackdrop(render_blit_t *rb,
                            dcoord y, dcoord xs, dcoord xe);
static void blkfillbackdrop(render_blit_t *rb,
                            dcoord ys, dcoord ye, dcoord xsp, dcoord xep);
static void blkclipbackdrop(render_blit_t *rb,
                            dcoord ys, dcoord ye, dcoord xs, dcoord xe);
static void areahalfbackdrop(render_blit_t *rb, FORM* formptr);

#if defined(ASSERT_BUILD)
/* Check that the render info used for the blit info is the same as that
   passed to the blitters. */
static const render_info_t* bd_ri = NULL;
#endif

void init_C_globals_backdropblt(void)
{
#if defined(ASSERT_BUILD)
  bd_ri = NULL ;
#endif
}

static Bool backdrop_regionRequest(surface_handle_t handle,
                                   struct BackdropShared *bd_shared,
                                   struct CompositeContext *bd_context,
                                   int32 groups,
                                   const dbbox_t *bbox)
{
  UNUSED_PARAM(surface_handle_t, handle) ;
  return bd_requestRegions(bd_shared, bd_context, groups, bbox) ;
}

static Bool backdrop_regionCreate(surface_handle_t handle,
                                  struct CompositeContext *bd_context,
                                  int32 colorspace,
                                  Bool is_soft_mask,
                                  Bool is_pattern,
                                  Bool is_knockout,
                                  surface_backdrop_t group_handle,
                                  surface_backdrop_t initial_handle,
                                  surface_backdrop_t target_handle,
                                  TranAttrib *to_target,
                                  const dbbox_t *bbox,
                                  render_region_callback_fn *callback,
                                  render_region_callback_t *data)
{
  Bool result = TRUE ;

  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(surface_backdrop_t, initial_handle) ;
  UNUSED_PARAM(surface_backdrop_t, target_handle) ;
  UNUSED_PARAM(Bool, is_knockout) ;
  UNUSED_PARAM(int32, colorspace) ;

  if ( (!is_soft_mask && is_pattern) ||
       bd_regionRequiresCompositing(group_handle, bbox, NULL) ) {

    if ( !bd_regionInit(bd_context, group_handle, bbox) )
      return FALSE ;

    HQASSERT(callback, "No backdrop region callback") ;
    result = (*callback)(data) ;

    if ( result )
      result = bd_regionComplete(bd_context, group_handle,
                                 tranAttribIsOpaque(to_target), bbox) ;
  }

  return result ;
}

static Bool backdrop_regionRender(surface_handle_t handle,
                                  render_blit_t *rb,
                                  surface_backdrop_t group_handle,
                                  surface_backdrop_t target_handle,
                                  SPOTNO spotno,
                                  HTTYPE objtype)
{
  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(surface_backdrop_t, group_handle) ;
  UNUSED_PARAM(SPOTNO, spotno) ;
  UNUSED_PARAM(HTTYPE, objtype) ;

  bd_compositeBackdrop(rb->p_ri->p_rs->composite_context,
                       target_handle, &rb->p_ri->clip);

  return TRUE ;
}

static transparency_surface_t backdrop = TRANSPARENCY_SURFACE_INIT ;

/** Attach builtin backdrop surface implementation as transparency surface
    for a surface set. */
void surface_set_transparency_builtin(surface_set_t *set, surface_t *output,
                                      const surface_t *indexed[])
{
  UNUSED_PARAM(surface_set_t *, set) ;
  VERIFY_OBJECT(&backdrop, TRANSPARENCY_SURFACE_NAME) ;
  HQASSERT(set, "No surface set to initialise") ;
  HQASSERT(set->indexed, "No surface array in set") ;
  HQASSERT(set->indexed == indexed, "Surface array inconsistent") ;
  HQASSERT(set->n_indexed > SURFACE_TRANSPARENCY &&
           set->n_indexed > SURFACE_OUTPUT,
           "Surface array too small") ;
  HQASSERT(set->n_indexed > SURFACE_TRANSPARENCY, "Surface array too small") ;
  HQASSERT(set->indexed[SURFACE_TRANSPARENCY] == NULL ||
           set->indexed[SURFACE_TRANSPARENCY] == &backdrop.base,
           "Transparency surface already initialised") ;
  HQASSERT(set->indexed[SURFACE_OUTPUT] == output,
           "Output surface for set not initialised") ;
  HQASSERT(output->backdropblit == NULL ||
           output->backdropblit == &backdropblt_builtin,
           "Output surface backdrop blit already initialised") ;
  indexed[SURFACE_TRANSPARENCY] = &backdrop.base ;
  output->backdropblit = &backdropblt_builtin ;
}

void init_backdropblt(void)
{
  unsigned int i, j ;

  /* Base blits */
  backdrop.base.baseblits[BLT_CLP_NONE].spanfn =
    backdrop.base.baseblits[BLT_CLP_RECT].spanfn = bitfillbackdrop ;
  backdrop.base.baseblits[BLT_CLP_COMPLEX].spanfn = bitclipbackdrop ;

  backdrop.base.baseblits[BLT_CLP_NONE].blockfn =
    backdrop.base.baseblits[BLT_CLP_RECT].blockfn = blkfillbackdrop ;
  backdrop.base.baseblits[BLT_CLP_COMPLEX].blockfn = blkclipbackdrop ;

  backdrop.base.baseblits[BLT_CLP_NONE].charfn =
    backdrop.base.baseblits[BLT_CLP_RECT].charfn =
    backdrop.base.baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  backdrop.base.baseblits[BLT_CLP_NONE].imagefn =
    backdrop.base.baseblits[BLT_CLP_RECT].imagefn =
    backdrop.base.baseblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No object map on the side blits for backdrop */

  /* Ignore maxblits */
  for ( j = 0 ; j < BLT_MAX_N ; ++j ) {
    for ( i = 0 ; i < BLT_CLP_N ; ++i ) {
      backdrop.base.maxblits[j][i].spanfn = next_span ;
      backdrop.base.maxblits[j][i].blockfn = next_block ;
      backdrop.base.maxblits[j][i].charfn = next_char ;
      backdrop.base.maxblits[j][i].imagefn = next_imgblt ;
    }
  }

  /* No ROP blits for backdrop */

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&backdrop.base) ;
  surface_pattern_builtin(&backdrop.base) ;
  surface_gouraud_builtin_tone_multi(&backdrop.base) ;

  backdrop.base.areafill = areahalfbackdrop ;
  backdrop.base.prepare = backdropblitinfo ;
  backdrop.base.backdropblit = backdrop_regionRender ;
  backdrop.base.blit_colormap_optimise = backdrop_blitmap_optimise ;

  backdrop.base.n_rollover = 3 ;
  backdrop.base.screened = FALSE ;
  backdrop.base.image_depth = 16 ;
  backdrop.base.render_order = SURFACE_ORDER_DEVICELR ;

  builtin_clip_N_surface(&backdrop.base, NULL) ;

  /* Now the transparency-specific functionality */
  backdrop.region_request = backdrop_regionRequest ;
  backdrop.region_create = backdrop_regionCreate ;
  backdrop.region_width = 128 ;

  NAME_OBJECT(&backdrop, TRANSPARENCY_SURFACE_NAME) ;
}

/* ---------------------------------------------------------------------- */
static surface_prepare_t backdropblitinfo(surface_handle_t handle,
                                          render_info_t* p_ri)
{
  Bool forceProcessKOs;
  LISTOBJECT *lobj = p_ri->lobj ;

  UNUSED_PARAM(surface_handle_t, handle) ;

#if defined(ASSERT_BUILD)
  bd_ri = p_ri;
#endif

  blit_color_quantise(p_ri->rb.color) ;

  /* A gray overprinted image which went through recombine interception
     may require expanding to CMYK to ensure expected KOs in CMY. */
  forceProcessKOs = (lobj->opcode == RENDER_image &&
                     (lobj->dldata.image->flags & IM_FLAG_PRESEP_KO_PROCESS) != 0);

  bd_runInfo(p_ri->p_rs->composite_context, render_state_backdrop(p_ri->p_rs),
             lobj, p_ri->rb.color,
             forceProcessKOs, CAST_SIGNED_TO_INT8(p_ri->overrideColorType));

  return SURFACE_PREPARE_OK ;
}

/** Blit color packing no-op for backdrop rasterstyles. This allows the
    blit color pack routine to be called unconditionally. */
static void backdrop_color_pack(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Blit color is not quantised") ;
#if 0
  /* Would be nice to assert that we're not wasting work, but the erase
     color is quantised and packed when it is set up, so this won't work. */
  HQASSERT(!(color->valid & blit_color_packed), "Blit color already packed") ;
#endif

#ifdef ASSERT_BUILD
  color->valid |= blit_color_packed ;
#endif
}

/** We don't need to expand because we just use the quantised color. */
static void backdrop_color_expand(blit_color_t *color)
{
  UNUSED_PARAM(blit_color_t *, color) ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
#ifdef ASSERT_BUILD
  color->valid |= blit_color_expanded ;
#endif
}

static void backdrop_blitmap_optimise(blit_colormap_t *map)
{
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  map->pack_quantised_color = backdrop_color_pack ;
  map->expand_packed_color = backdrop_color_expand ;
}

/* ---------------------------------------------------------------------- */
static void bitfillbackdrop(render_blit_t *rb,
                            dcoord y, dcoord xsp, dcoord xep)
{
  bd_compositeSpan(rb->p_ri->p_rs->composite_context,
                   render_state_backdrop(rb->p_ri->p_rs),
                   xsp, y, xep - xsp + 1, rb->color);
}

/* ---------------------------------------------------------------------- */
static void bitclipbackdrop(render_blit_t *rb,
                            dcoord y, dcoord xs, dcoord xe)
{
  bitclipn(rb, y, xs, xe, bitfillbackdrop);
}

/* ---------------------------------------------------------------------- */
static void blkfillbackdrop(render_blit_t *rb,
                            dcoord ys, dcoord ye, dcoord xsp, dcoord xep)
{
  dcoord rows = ye - ys + 1;

  if (rows == 1) {
    bitfillbackdrop(rb, ys, xsp, xep);
  } else {
    bd_compositeBlock(rb->p_ri->p_rs->composite_context,
                      render_state_backdrop(rb->p_ri->p_rs), xsp, ys,
                      xep - xsp + 1, rows, rb->color);
  }
}

/* ---------------------------------------------------------------------- */
static void blkclipbackdrop(render_blit_t *rb,
                     dcoord ys, dcoord ye, dcoord xs, dcoord xe)
{
  blkclipn_coalesce(rb, ys, ye, xs, xe, blkfillbackdrop);
}

/* ---------------------------------------------------------------------- */
static void areahalfbackdrop(render_blit_t *rb, FORM* formptr)
{
  blkfillbackdrop(rb, theFormHOff(*formptr),
                  theFormHOff(*formptr) + theFormRH(*formptr) - 1,
                  0,
                  theFormW(*formptr) - 1);
}

/* ---------------------------------------------------------------------- */
/* Log stripped */
