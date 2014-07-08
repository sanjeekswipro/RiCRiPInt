/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!adjust:src:rcbshfil.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines for recombination of shfill objects. Shfill objects are recombined
 * by storing the high-level coordinate, color, and function information
 * during the initial passes, and expanding these into a full shfill object
 * in the adjustment phase.
 */

#include "core.h"
#include "objects.h"
#include "rcbshfil.h"

#include "swoften.h"
#include "swerrors.h"
#include "often.h"
#include "mm.h"
#include "graphics.h"
#include "gstate.h"

#include "dlstate.h"
#include "dl_free.h"  /* free_dl_object */
#include "functns.h"  /* FN_EVAL_ALWAYS_UPWARDS */
#include "matrix.h"   /* MATRIX_TRANSFORM_XY */
#include "control.h"  /* error_handler */
#include "pathops.h"  /* transform_bbox */
#include "shadex.h"   /* SHADINGinfo */
#include "shadecev.h" /* vertex_* */
#include "tensor.h"   /* SHADINGtensor */
#include "gouraud.h"  /* decompose_triangle */
#include "blends.h"   /* axialblend, radialblend */
#include "sobol.h"    /* sobol */
#include "dl_color.h" /* dl_color_t, p_ncolor_t */
#include "constant.h" /* EPSILON */
#include "vndetect.h" /* VIGNETTEOBJECT */
#include "gschead.h"  /* gsc_getcolorspace */
#include "dl_store.h" /* same_clip_objects */
#include "dl_bres.h"  /* BRESIF, initBresWalk */
#include "display.h"  /* STATEOBJECT */
#include "params.h"
#include "hdl.h"      /* HDL operations */

#include "rcbcntrl.h" /* rcbn_current_colorant */
#include "rcbadjst.h" /* rcba_doing_color_adjust */
#include "shadefunc.h"  /* shading_function_decompose */

static Bool rcbs_fn_merged_dl_color(USERVALUE domain[2],
                                    int32 inwards,
                                    SHADINGinfo *sinfo) ;
static Bool rcbs_fn_overprint(SHADINGinfo *sinfo, rcbs_function_h *lfnh,
                              int32 ninputs) ;

#if defined( ASSERT_BUILD )
static int32 rcbs_trace = 0 ;
#endif

/* Struct to store information required to recombine shfill */
struct rcbs_patch_t {
  int32 type ;
  union {
    struct {
      USERVALUE comps[4][2] ;
      SYSTEMVALUE x[4], y[4] ;
    } function ;
    struct {
      SYSTEMVALUE coords[4] ;
      Bool extend[2] ;
      USERVALUE domain[2] ;
    } axial ;
    struct {
      SYSTEMVALUE coords[6] ;
      Bool extend[2] ;
      USERVALUE domain[2] ;
    } radial ;
    struct {
      SYSTEMVALUE x[3], y[3] ;
      uint8 usefn, spare1, spare2, spare3 ;
      union {
        p_ncolor_t ncolor[3] ;
        USERVALUE fin[3] ;
      } colors ;
    } gouraud ;
    struct {
      SHADINGpatch patch ;
      SHADINGtensor tensor ;
      union {
        p_ncolor_t ncolor[4] ;
        USERVALUE fin[4] ;
      } colors ;
      uint8 usefn, spare1, spare2, spare3 ;
    } tensor ;
  } u ;
  dbbox_t bbox ;
  void *base_addr ;
};

static rcbs_patch_t *rcbs_alloc_patch(DL_STATE *page) ;

/*****************************************************************************/
static Bool rcbs_add_patch(DL_STATE *page, rcbs_patch_t *patch, sbbox_t *sbbox)
{
  LISTOBJECT *lobj;
  dbbox_t bbox;

  SC_C2D_INT(patch->bbox.x1, sbbox->x1);
  SC_C2D_INT(patch->bbox.y1, sbbox->y1);
  SC_C2D_INT(patch->bbox.x2, sbbox->x2);
  SC_C2D_INT(patch->bbox.y2, sbbox->y2);

  bbox = patch->bbox;
  if ( !clip2cclipbbox(&bbox) ) /* Clipped out patch? */
    return TRUE ;

  /* Create dummy object for blend info. object color is merged colour from
     all sample points in function's domain; this ensures that the final
     adjustment gets all of the correct colors. */
  if ( !make_listobject(page, RENDER_shfill_patch, &bbox, &lobj) )
    return FALSE;
  lobj->dldata.patch = patch;
  return add_listobject(page, lobj, NULL);
}

/*****************************************************************************/
/* Functions to store information about the shfill operation for later use in
   comparison and merging */

Bool rcbs_store_function(SHADINGvertex *corners[4], SHADINGinfo *sinfo)
{
  int32 i ;
  USERVALUE domain[4] ;
  rcbs_patch_t *fn_info = rcbs_alloc_patch(sinfo->page) ;
  sbbox_t sbbox ;

  if ( fn_info == NULL )
    return error_handler(VMERROR) ;

  HQASSERT(sinfo->noutputs == 1, "Cannot recombine multi-channel functions") ;

  fn_info->type = sinfo->type ;
  HQASSERT(fn_info->type == 1, "Should be function based shfill") ;

  bbox_store(&sbbox,
            corners[0]->x, corners[0]->y, corners[0]->x, corners[0]->y) ;

  /* Save domain and find bounding box */
  for ( i = 0 ; i < 4 ; ++i ) {
    SYSTEMVALUE x, y ;

    fn_info->u.function.x[i] = corners[i]->x ;
    fn_info->u.function.y[i] = corners[i]->y ;
    fn_info->u.function.comps[i][0] = corners[i]->comps[0] ;
    fn_info->u.function.comps[i][1] = corners[i]->comps[1] ;

    MATRIX_TRANSFORM_XY(corners[i]->comps[0], corners[i]->comps[1], x, y,
                        &thegsPageCTM(*gstateptr)) ;

    bbox_union_point(&sbbox, x, y) ;
  }
  domain[0] = corners[0]->comps[0] ;
  domain[1] = corners[1]->comps[0] ;
  domain[2] = corners[0]->comps[1] ;
  domain[3] = corners[2]->comps[1] ;

  dlc_release(sinfo->page->dlc_context,
              dlc_currentcolor(sinfo->page->dlc_context)) ;
  if ( !rcbs_fn_merged_dl_color(domain, TRUE, sinfo) )
    return FALSE ;

  return rcbs_add_patch(sinfo->page, fn_info, &sbbox) ;
}

Bool rcbs_store_blend(SYSTEMVALUE coords[], USERVALUE domain[2],
                      Bool extend[2], SHADINGinfo *sinfo)
{
  Bool *extend_save ;
  int32 i ;
  SYSTEMVALUE *coord_save ;
  USERVALUE *domain_save ;
  rcbs_patch_t *blend_info ;
  sbbox_t sbbox ;

  HQASSERT(sinfo->noutputs == 1, "Cannot recombine multi-channel functions") ;

  if ( sinfo->type == 2 ) {
    SYSTEMVALUE vx, vy ;

    vx = (coords[2] - coords[0]) ;
    vy = (coords[3] - coords[1]) ;

    /* Don't paint anything if axis is degenerate */
    if ( vx*vx + vy*vy == 0.0 )
      return TRUE ;

   if ( (blend_info = rcbs_alloc_patch(sinfo->page)) == NULL )
      return error_handler(VMERROR) ;

    coord_save = blend_info->u.axial.coords ;
    for ( i = 0 ; i < 4 ; ++i )
      coord_save[i] = coords[i] ;

    extend_save = blend_info->u.axial.extend ;
    domain_save = blend_info->u.axial.domain ;
  } else {
    HQASSERT(sinfo->type == 3, "Should be radial shfill since not axial") ;

    /* Don't paint anything if both radii are zero */
    if ( coords[2] == 0.0 && coords[5] == 0.0 )
      return TRUE ;

    if ( (blend_info = rcbs_alloc_patch(sinfo->page)) == NULL )
      return error_handler(VMERROR) ;

    coord_save = blend_info->u.radial.coords ;
    for ( i = 0 ; i < 6 ; ++i )
      coord_save[i] = coords[i] ;

    extend_save = blend_info->u.radial.extend ;
    domain_save = blend_info->u.radial.domain ;
  }

  blend_info->type = sinfo->type ;

  /* Save extension flags */
  extend_save[0] = extend[0] ;
  extend_save[1] = extend[1] ;

  /* Save domain */
  domain_save[0] = domain[0] ;
  domain_save[1] = domain[1] ;

  dlc_release(sinfo->page->dlc_context,
              dlc_currentcolor(sinfo->page->dlc_context)) ;
  if ( !rcbs_fn_merged_dl_color(domain, FALSE, sinfo) )
    return FALSE ;

  /* Use transformed bbox, because calculating exact boundary is too awkward */
  bbox_transform(&sinfo->bbox, &sbbox, &thegsPageCTM(*gstateptr)) ;

  return rcbs_add_patch(sinfo->page, blend_info, &sbbox) ;
}

Bool rcbs_store_gouraud(SHADINGvertex *v0, SHADINGvertex *v1,
                        SHADINGvertex *v2, SHADINGinfo *sinfo)
{
  SHADINGvertex *v[3] ;
  int32 i ;
  uint8 usefn = (uint8)(sinfo->nfuncs != 0) ;
  rcbs_patch_t *gour_info = rcbs_alloc_patch(sinfo->page) ;
  sbbox_t sbbox ;

  if ( gour_info == NULL )
    return error_handler(VMERROR) ;

  v[0] = v0 ;
  v[1] = v1 ;
  v[2] = v2 ;

  gour_info->type = sinfo->type ;
  HQASSERT(gour_info->type == 4 || gour_info->type == 5,
           "Should be gouraud based shfill") ;

  gour_info->u.gouraud.usefn = usefn ;

  bbox_store(&sbbox, v0->x, v0->y, v0->x, v0->y) ;

  for ( i = 0 ; i < 3 ; ++i ) {
    gour_info->u.gouraud.x[i] = v[i]->x ;
    gour_info->u.gouraud.y[i] = v[i]->y ;

    if ( usefn )
      gour_info->u.gouraud.colors.fin[i] = v[i]->comps[0] ;
    else if ( !vertex_color(v[i], sinfo, FALSE) ||
              !dlc_to_dl(sinfo->page->dlc_context,
                         &gour_info->u.gouraud.colors.ncolor[i], &v[i]->dlc) )
      return FALSE ;

    bbox_union_point(&sbbox, v[i]->x, v[i]->y) ;
  }

  /* Create representative colour for gouraud patch */
  if ( usefn ) {
    USERVALUE domain[2] ;

    if ( v0->comps[0] <= v1->comps[0] ) {
      domain[0] = v0->comps[0] ;
      domain[1] = v1->comps[0] ;
    } else {
      domain[0] = v1->comps[0] ;
      domain[1] = v0->comps[0] ;
    }

    if ( v2->comps[0] < domain[0] )
      domain[0] = v2->comps[0] ;
    else if ( v2->comps[0] > domain[1] )
      domain[1] = v2->comps[0] ;

    dlc_release(sinfo->page->dlc_context,
                dlc_currentcolor(sinfo->page->dlc_context)) ;
    if ( !rcbs_fn_merged_dl_color(domain, TRUE, sinfo) )
      return FALSE ;
  } else { /* Mix non-function colours of corners */
    dl_color_t *dlc_current = dlc_currentcolor(sinfo->page->dlc_context) ;
    dlc_release(sinfo->page->dlc_context, dlc_current) ;
    if ( !dlc_copy(sinfo->page->dlc_context, dlc_current, &v0->dlc) ||
         !dlc_merge_shfill(sinfo->page->dlc_context, dlc_current, &v1->dlc) ||
         !dlc_merge_shfill(sinfo->page->dlc_context, dlc_current, &v2->dlc) )
      return FALSE ;
  }

  return rcbs_add_patch(sinfo->page, gour_info, &sbbox) ;
}

Bool rcbs_store_tensor(SHADINGtensor *tensor, SHADINGinfo *sinfo)
{
  SHADINGpatch *new_patch, *patch ;
  SHADINGtensor *new_tensor ;
  int32 i, j ;
  uint8 usefn = (uint8)(sinfo->nfuncs != 0) ;
  rcbs_patch_t *tensor_info = rcbs_alloc_patch(sinfo->page) ;
  sbbox_t sbbox ;

  HQASSERT(tensor, "Invalid tensor parameter") ;
  HQASSERT(sinfo, "Invalid shading info parameter") ;

  if ( tensor_info == NULL )
    return error_handler(VMERROR) ;

  new_tensor = &tensor_info->u.tensor.tensor ;
  new_patch = &tensor_info->u.tensor.patch ;
  patch = tensor->patch ;

  *new_tensor = *tensor ;
  new_tensor->patch = new_patch ;
  new_tensor->sinfo = NULL ;
  new_tensor->neighbors[0] = NULL ;
  new_tensor->neighbors[1] = NULL ;

  *new_patch = *patch ;
  new_patch->children = NULL ;
  new_patch->corner[0][0] = NULL ;
  new_patch->corner[0][1] = NULL ;
  new_patch->corner[1][0] = NULL ;
  new_patch->corner[1][1] = NULL ;

  tensor_info->type = sinfo->type ;
  HQASSERT(tensor_info->type == 6 || tensor_info->type == 7,
           "Should be tensor based shfill") ;

  tensor_info->u.tensor.usefn = usefn ;

  /* Store components and create representative colour for tensor patch */
  if ( usefn ) {
    USERVALUE domain[2] ;

    for ( i = 0 ; i < 4 ; ++i ) {
      SHADINGvertex *v = patch->corner[i >> 1][i & 1] ;
      USERVALUE vc = v->comps[0] ;

      tensor_info->u.tensor.colors.fin[i] = vc ;

      if ( i == 0 ) {
        domain[0] = domain[1] = vc ;
      } else if ( vc < domain[0] )
        domain[0] = vc ;
      else if ( vc > domain[1] )
        domain[1] = vc ;
    }

    dlc_release(sinfo->page->dlc_context,
                dlc_currentcolor(sinfo->page->dlc_context)) ;
    if ( !rcbs_fn_merged_dl_color(domain, TRUE, sinfo) )
      return FALSE ;
  } else { /* Mix non-function colours of corners */
    dl_color_t dlc_merged ;

    dlc_clear(&dlc_merged) ;

    for ( i = 0 ; i < 4 ; ++i ) {
      SHADINGvertex *v = patch->corner[i >> 1][i & 1] ;

      if ( !vertex_color(v, sinfo, FALSE) ||
           !dlc_to_dl(sinfo->page->dlc_context,
                      &tensor_info->u.tensor.colors.ncolor[i], &v->dlc) )
        return FALSE ;

      if ( !(i == 0 ?
             dlc_copy(sinfo->page->dlc_context, &dlc_merged, &v->dlc) :
             dlc_merge_shfill(sinfo->page->dlc_context, &dlc_merged, &v->dlc)) )
        return FALSE ;
    }

    dlc_copy_release(sinfo->page->dlc_context,
                     dlc_currentcolor(sinfo->page->dlc_context), &dlc_merged) ;
  }

  bbox_store(&sbbox, tensor->coord[0][0][0], tensor->coord[0][0][1],
             tensor->coord[0][0][0], tensor->coord[0][0][1]) ;

  for ( i = 0 ; i < 4 ; ++i )
    for ( j = 0 ; j < 4 ; ++j ) {
      SYSTEMVALUE x = tensor->coord[i][j][0], y = tensor->coord[i][j][1] ;

      bbox_union_point(&sbbox, x, y) ;
    }

  return rcbs_add_patch(sinfo->page, tensor_info, &sbbox) ;
}

/*****************************************************************************/
/* Allocate patch, ensuring it is 8-byte aligned. */
static rcbs_patch_t *rcbs_alloc_patch(DL_STATE *page)
{
  rcbs_patch_t *result ;
  void *base = dl_alloc(page->dlpools, sizeof(rcbs_patch_t) + 4,
                        MM_ALLOC_CLASS_SHADING) ;
  if (base == NULL) {
    (void)error_handler(VMERROR);
    return NULL ;
  }

  result = (rcbs_patch_t *)DWORD_ALIGN_UP(uintptr_t, base) ;
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, result), "Patch is not 8-byte aligned") ;

  result->base_addr = base ;

  return result ;
}

/*****************************************************************************/
/* Free patch info */

void rcbs_free_patch(rcbs_patch_t **patchh, DL_STATE *page)
{
  rcbs_patch_t *patch = *patchh ;

  if ( patch ) {
    int32 i ;

    switch ( patch->type ) {
    case 4: case 5:
      if ( !patch->u.gouraud.usefn ) {
        for ( i = 0 ; i < 3 ; ++i )
          dl_release(page->dlc_context, &patch->u.gouraud.colors.ncolor[i]) ;
      }
      break ;
    case 6: case 7:
      if ( !patch->u.tensor.usefn ) {
        for ( i = 0 ; i < 4 ; ++i )
          dl_release(page->dlc_context, &patch->u.tensor.colors.ncolor[i]) ;
      }
      break ;
    }
    dl_free(page->dlpools, patch->base_addr, sizeof(rcbs_patch_t) + 4,
            MM_ALLOC_CLASS_SHADING);

    *patchh = NULL ;
  }
}


/*****************************************************************************/
/* Info functions. Return patch type */
int32 rcbs_patch_type(rcbs_patch_t *patch)
{
  HQASSERT(patch, "Patch parameter invalid") ;

  return patch->type ;
}

/* Return patch bounding box. */
void rcbs_bbox(rcbs_patch_t *patch, dbbox_t *bbox)
{
  HQASSERT(patch, "Patch parameter invalid") ;

  *bbox = patch->bbox ;
}


/*****************************************************************************/
/* Comparison functions; compare two sets of stored information to determine
   if they represent the same patch */

Bool rcbs_compare_function(rcbs_patch_t *patch1, rcbs_patch_t *patch2)
{
  int32 i ;

  HQASSERT(patch1, "Invalid patch1 parameter");
  HQASSERT(patch2, "Invalid patch2 parameter");

  for ( i = 0 ; i < 4 ; ++i ) {
    if ( patch1->u.function.x[i] != patch2->u.function.x[i] ||
         patch1->u.function.y[i] != patch2->u.function.y[i] ||
         patch1->u.function.comps[i][0] != patch2->u.function.comps[i][0] ||
         patch1->u.function.comps[i][1] != patch2->u.function.comps[i][1] )
      return FALSE ;
  }

  return TRUE ;
}

Bool rcbs_compare_axial(rcbs_patch_t *patch1, rcbs_patch_t *patch2)
{
  int32 i ;

  HQASSERT(patch1, "Invalid patch1 parameter") ;
  HQASSERT(patch2, "Invalid patch2 parameter") ;

  for ( i = 0 ; i < 4 ; ++i )
    if ( patch1->u.axial.coords[i] != patch2->u.axial.coords[i] )
      return FALSE ;

  for ( i = 0 ; i < 2 ; ++i ) {
    if ( patch1->u.axial.extend[i] != patch2->u.axial.extend[i] ||
         patch1->u.axial.domain[i] != patch2->u.axial.domain[i] )
      return FALSE ;
  }

  return TRUE ;
}

Bool rcbs_compare_radial(rcbs_patch_t *patch1, rcbs_patch_t *patch2)
{
  int32 i ;

  HQASSERT(patch1, "Invalid patch1 parameter");
  HQASSERT(patch2, "Invalid patch2 parameter");

  for ( i = 0 ; i < 6 ; ++i )
    if ( patch1->u.radial.coords[i] != patch2->u.radial.coords[i] )
      return FALSE ;

  for ( i = 0 ; i < 2 ; ++i ) {
    if ( patch1->u.radial.extend[i] != patch2->u.radial.extend[i] ||
         patch1->u.radial.domain[i] != patch2->u.radial.domain[i] )
      return FALSE ;
  }
  return TRUE;
}

Bool rcbs_compare_gouraud(rcbs_patch_t *patch1, rcbs_patch_t *patch2)
{
  int32 i ;

  HQASSERT(patch1, "Invalid patch1 parameter");
  HQASSERT(patch2, "Invalid patch2 parameter");

  if ( patch1->u.gouraud.usefn != patch2->u.gouraud.usefn )
    return FALSE ;

  for ( i = 0 ; i < 3 ; ++i ) {
    if ( patch1->u.gouraud.x[i] != patch2->u.gouraud.x[i] ||
         patch1->u.gouraud.y[i] != patch2->u.gouraud.y[i] )
      return FALSE ;
    if ( patch1->u.gouraud.usefn &&
         patch1->u.gouraud.colors.fin[i] != patch2->u.gouraud.colors.fin[i] )
      return FALSE ;
  }

  return TRUE ;
}

Bool rcbs_compare_tensor(rcbs_patch_t *patch1, rcbs_patch_t *patch2)
{
  int32 i, j ;
  SHADINGtensor *tensor1, *tensor2 ;

  HQASSERT(patch1, "Invalid patch 1 parameter") ;
  HQASSERT(patch2, "Invalid patch 2 parameter") ;

  tensor1 = &patch1->u.tensor.tensor ;
  tensor2 = &patch2->u.tensor.tensor ;

  if ( patch1->u.tensor.usefn != patch2->u.tensor.usefn )
    return FALSE ;

  for ( i = 0 ; i < 4 ; ++i ) {
    if ( patch1->u.tensor.usefn &&
         patch1->u.tensor.colors.fin[i] != patch2->u.tensor.colors.fin[i] )
      return FALSE ;

    for ( j = 0 ; j < 4 ; ++j ) {
      if ( tensor1->coord[i][j][0] != tensor2->coord[i][j][0] ||
           tensor1->coord[i][j][1] != tensor2->coord[i][j][1] )
        return FALSE ;
    }
  }

  return TRUE ;
}

/*****************************************************************************/
/* Merging functions; merge two sets of stored information to into a single
   set of patches */
Bool rcbs_merge_shfill(LISTOBJECT *lobj_old, LISTOBJECT *lobj_new)
{
  SHADINGinfo *sinfo_old, *sinfo_new ;
  SHADINGOBJECT *sobj_old, *sobj_new ;
  DLREF *link_old, *link_new ;
  DL_STATE *page ;

  HQASSERT( lobj_old != NULL , "Old LISTOBJECT invalid" ) ;
  HQASSERT( lobj_new != NULL , "New LISTOBJECT invalid" ) ;

  sobj_old = lobj_old->dldata.shade;
  sobj_new = lobj_new->dldata.shade;

  HQASSERT( sobj_old != NULL , "Old SHADINGOBJECT invalid" ) ;
  HQASSERT( sobj_new != NULL , "New SHADINGOBJECT invalid" ) ;

  sinfo_old = sobj_old->info;
  sinfo_new = sobj_new->info;

  HQASSERT( sinfo_old != NULL , "Old SHADINGinfo invalid" ) ;
  HQASSERT( sinfo_new != NULL , "New SHADINGinfo invalid" ) ;

  /* Merge functions into single array of linearised functions */
  HQASSERT( (sinfo_old->rfuncs == NULL) == (sinfo_new->rfuncs == NULL) &&
            (sinfo_old->nfuncs == 0) == (sinfo_new->nfuncs == 0),
            "Function information not compatible between VIGNETTEOBJECTS" ) ;

  page = sinfo_old->page ;

  sinfo_old->smoothnessbands = (uint16)max(sinfo_old->smoothnessbands,
                                           sinfo_new->smoothnessbands) ;
  sinfo_old->rflat = min(sinfo_old->rflat, sinfo_new->rflat) ;
  sinfo_old->spotno = lobj_old->objectstate->spotno ;
  sinfo_old->ncolors = dl_num_channels(lobj_old->p_ncolor) ;

  /* Make sure number of channels is same in merged shfills */
  HQASSERT(sobj_old->nchannels == sobj_new->nchannels,
           "Mismatch in recombine shfill channels") ;
  sobj_old->mbands = (uint16)max(sobj_old->mbands, sobj_new->mbands) ;
  sobj_old->noisesize = (uint16)min(sobj_old->noisesize, sobj_new->noisesize) ;
  sobj_old->noise = (USERVALUE)min(sobj_old->noise, sobj_new->noise) ;

  if ( sinfo_old->rfuncs ) {
    int32 i, j ;
    int32 nfuncs = sinfo_old->nfuncs + sinfo_new->nfuncs ;
    uint32 rfunc_size = RCBS_FUNC_HARRAY_SPACE(nfuncs) ;

    /* Reallocate function handle array if it needs extending */
    if ( RCBS_FUNC_HARRAY_SPACE(sinfo_old->nfuncs) != rfunc_size ) {
      rcbs_function_h *rfuncs =
        (rcbs_function_h *)dl_alloc(page->dlpools, rfunc_size,
                                    MM_ALLOC_CLASS_SHADING) ;
      COLORANTINDEX *rfcis ;

      if ( rfuncs == NULL )
        return error_handler(VMERROR) ;

      rfcis = (COLORANTINDEX *)((uint8 *)rfuncs + RCBS_FUNC_CINDEX_OFFSET(nfuncs)) ;

      for ( i = 0 ; i < sinfo_old->nfuncs ; ++i ) {
        rfuncs[i] = sinfo_old->rfuncs[i] ;
        rfcis[i] = sinfo_old->rfcis[i] ;
      }

      dl_free(page->dlpools, (mm_addr_t)sinfo_old->rfuncs,
              RCBS_FUNC_HARRAY_SPACE(sinfo_old->nfuncs),
              MM_ALLOC_CLASS_SHADING);

      sinfo_old->rfuncs = rfuncs ;
      sinfo_old->rfcis = rfcis ;
    }

    /* Transfer new functions to old object */
    for ( i = 0, j = sinfo_old->nfuncs ; i < sinfo_new->nfuncs ; ++i, ++j ) {
      sinfo_old->rfuncs[j] = sinfo_new->rfuncs[i] ;
      sinfo_old->rfcis[j] = sinfo_new->rfcis[i] ;
    }

    sinfo_old->nfuncs += sinfo_new->nfuncs ;

    dl_free(page->dlpools, (mm_addr_t)sinfo_new->rfuncs,
            RCBS_FUNC_HARRAY_SPACE(sinfo_new->nfuncs),
            MM_ALLOC_CLASS_SHADING);

    sinfo_new->rfuncs = NULL ;
    sinfo_new->rfcis = NULL ;
    sinfo_new->nfuncs = 0 ;
  } else { /* No functions, so number of components must be number of colors */
    sinfo_old->ncomps = sinfo_old->ncolors ;
  }

  /* Walk sub-DLs merging objects */
  link_old = hdlOrderList(lobj_old->dldata.shade->hdl) ;
  link_new = hdlOrderList(lobj_new->dldata.shade->hdl) ;

  HQASSERT( link_old != NULL , "Old shfill sub-DL invalid" ) ;
  HQASSERT( link_new != NULL , "New shfill sub-DL invalid" ) ;

  while ( link_old != NULL && link_new != NULL ) {
    LISTOBJECT *slobj_old , *slobj_new ;

    slobj_old = dlref_lobj(link_old);
    slobj_new = dlref_lobj(link_new);

    if ( !dl_merge_extra(page->dlc_context,
                         &slobj_old->p_ncolor, &slobj_new->p_ncolor) )
      return FALSE ;

    HQASSERT(slobj_old->opcode == slobj_new->opcode,
             "Merging incompatible types on shfill sub-DL") ;

    if ( slobj_old->opcode == RENDER_shfill_patch ) {
      rcbs_patch_t *info_old = slobj_old->dldata.patch;
      rcbs_patch_t *info_new = slobj_new->dldata.patch;

      HQASSERT(info_old != NULL , "Old shfill recombine info invalid" ) ;
      HQASSERT(info_new != NULL , "New shfill recombine info invalid" ) ;

      HQASSERT(info_old->type == info_new->type,
               "Shfill recombine info type mismatch" ) ;

      switch ( info_old->type ) {
      case 1: case 2: case 3: /* No more to merge, colours require function */
        break ;
      case 4: case 5: /* If no functions, merge corner colour planes */
        if ( !info_old->u.gouraud.usefn ) {
          int32 i ;

          for ( i = 0 ; i < 3 ; ++i ) {
            if ( !dl_merge_extra(page->dlc_context,
                                 &info_old->u.gouraud.colors.ncolor[i],
                                 &info_new->u.gouraud.colors.ncolor[i]) )
              return FALSE ;
          }
        }
        break ;
      case 6: case 7: /* If no functions, merge corner colour planes */
        if ( !info_old->u.tensor.usefn ) {
          int32 i ;

          for ( i = 0 ; i < 4 ; ++i ) {
            if ( !dl_merge_extra(page->dlc_context,
                                 &info_old->u.tensor.colors.ncolor[i],
                                 &info_new->u.tensor.colors.ncolor[i]) )
              return FALSE ;
          }
        }
        break ;
      default:
        HQFAIL("Invalid shfill recombine info type") ;
      }
    }

    link_old = dlref_next(link_old);
    link_new = dlref_next(link_new);
  }

  HQASSERT(link_old == NULL && link_new == NULL, "shfill sub-DL mismatch") ;

  return TRUE ;
}

/*****************************************************************************/
/* Adjusting functions. These expand stored patch descriptions into full
   shfills. */

/* Get colorvalues from DL color. Unknown channels must be overprints, and so
   are forced to zero */
static void rcba_vertex_color(SHADINGvertex *v,
                              p_ncolor_t ncolor,
                              COLORANTINDEX *colorants,
                              SHADINGinfo *sinfo)
{
  dl_color_t dlc ;
  int32 i ;
  Bool flip_values;

  dlc_from_dl_weak(ncolor, &dlc) ;

  HQASSERT(sinfo->ncomps == sinfo->ncolors,
           "No functions, so components should equal colors") ;

  /* Recombine color values are additive and therefore requiring flipping
     when the input colorspace is subtractive. */
  flip_values = (ColorspaceIsSubtractive(gsc_getcolorspace(gstateptr->colorInfo,
                                                           sinfo->base_index)));

  for ( i = 0 ; i < sinfo->ncomps ; ++i ) {
    COLORVALUE cv = COLORVALUE_PRESEP_WHITE ;
    COLORANTINDEX ci = colorants[i] ;

    if ( ci != COLORANTINDEX_NONE &&
         dlc_get_indexed_colorant(&dlc, ci, &cv) ) {
      HQASSERT( cv <= COLORVALUE_MAX ,
                "colorant value out of range" ) ;
    }

    if (flip_values)
      COLORVALUE_FLIP(cv, cv); /* Flip from additive to subtractive. */

    v->comps[i] = COLORVALUE_TO_USERVALUE(cv) ;
  }
}

Bool rcbs_adjust_function(rcbs_patch_t *patch, SHADINGinfo *sinfo)
{
  SHADINGvertex *corners[4] ;
  int32 i ;
  Bool result ;

  HQASSERT(patch, "Patch parameter invalid") ;
  HQASSERT(sinfo, "Shading info parameter invalid") ;
  HQASSERT(sinfo->ncomps == 2, "Number of components for shading info wrong") ;

  if ( !vertex_alloc(corners, 4) )
    return FALSE ;

  for ( i = 0 ; i < 4 ; ++i ) {
    corners[i]->x = patch->u.function.x[i] ;
    corners[i]->y = patch->u.function.y[i] ;
    corners[i]->comps[0] = patch->u.function.comps[i][0] ;
    corners[i]->comps[1] = patch->u.function.comps[i][1] ;
    corners[i]->opacity = 1.0;
  }

  result = shading_function_decompose(corners[0], corners[1],
                                      corners[2], corners[3],
                                      sinfo, 0) ;

  vertex_free(sinfo, corners, 4) ;

  return result ;
}

Bool rcbs_adjust_axial(rcbs_patch_t *patch, SHADINGinfo *sinfo)
{
  USERVALUE opacity[2] = {1.0f, 1.0f};

  HQASSERT(patch, "Patch parameter invalid") ;
  HQASSERT(sinfo, "Shading info parameter invalid") ;

  return axialblend(patch->u.axial.coords,
                    patch->u.axial.domain,
                    opacity,
                    patch->u.axial.extend,
                    sinfo) ;
}

Bool rcbs_adjust_radial(rcbs_patch_t *patch, SHADINGinfo *sinfo)
{
  USERVALUE opacity[2] = {1.0f, 1.0f};

  HQASSERT(patch, "Patch parameter invalid") ;
  HQASSERT(sinfo, "Shading info parameter invalid") ;

  return radialblend(patch->u.radial.coords,
                     patch->u.radial.domain,
                     opacity,
                     patch->u.radial.extend,
                     sinfo) ;
}

Bool rcbs_adjust_gouraud(rcbs_patch_t *patch, SHADINGinfo *sinfo,
                         COLORANTINDEX *colorants)
{
  SHADINGvertex *corners[3] ;
  int32 i ;
  Bool result ;

  HQASSERT(patch, "Patch parameter invalid") ;
  HQASSERT(sinfo, "Shading info parameter invalid") ;
  HQASSERT(colorants, "Colorant index list parameter invalid") ;

  if ( !vertex_alloc(corners, 3) )
    return FALSE ;

  for ( i = 0 ; i < 3 ; ++i ) {
    corners[i]->x = patch->u.gouraud.x[i] ;
    corners[i]->y = patch->u.gouraud.y[i] ;

    if ( patch->u.gouraud.usefn ) {
      corners[i]->comps[0] = patch->u.gouraud.colors.fin[i] ;
    } else {
      rcba_vertex_color(corners[i],
                        patch->u.gouraud.colors.ncolor[i],
                        colorants, sinfo) ;
    }

    corners[i]->opacity = 1.0;
  }

  result = decompose_triangle(corners[0], corners[1], corners[2], sinfo) ;

  vertex_free(sinfo, corners, 3) ;

  return result ;
}

Bool rcbs_adjust_tensor(rcbs_patch_t *patch, SHADINGinfo *sinfo,
                        COLORANTINDEX *colorants)
{
  SHADINGtensor *tensor;
  SHADINGpatch *shade;
  SHADINGvertex *corners[4] ;
  int32 i ;
  Bool result ;

  HQASSERT(patch, "Patch parameter invalid") ;
  HQASSERT(sinfo, "Shading info parameter invalid") ;
  HQASSERT(colorants, "Colorant index list parameter invalid") ;

  tensor = &patch->u.tensor.tensor ;
  shade = tensor->patch;

  if ( !vertex_alloc(corners, 4) )
    return FALSE ;

  for ( i = 0 ; i < 4 ; ++i ) {
    int32 x = (i >> 1), y = (i & 1) ;

    shade->corner[x][y] = corners[i] ;

    corners[i]->x = tensor->coord[x * 3][y * 3][0] ;
    corners[i]->y = tensor->coord[x * 3][y * 3][1] ;

    if ( patch->u.tensor.usefn ) {
      corners[i]->comps[0] = patch->u.tensor.colors.fin[i] ;
    } else {
      rcba_vertex_color(corners[i],
                        patch->u.tensor.colors.ncolor[i],
                        colorants, sinfo) ;
    }

    corners[i]->opacity = 1.0;
  }

  tensor->sinfo = sinfo ;

  result = tensor_draw(tensor) ;

  vertex_free(sinfo, corners, 4) ;

  return result ;
}

/* Sort functions from pseudo-colorant index into real colorant order.
   colorants[i] is pseudo colorant of i'th colorvalue in color space. I
   believe that colorants[] should be either a strict subset, equal, or a
   strict superset of sinfo->rfcis[], however the safest algorithm is to
   deal with partial intersection by always allocating a new array. */
Bool rcbs_adjust_fn_order(SHADINGinfo *sinfo, COLORANTINDEX *colorants)
{
  COLORANTINDEX *rfcis = sinfo->rfcis, *nrfcis ;
  rcbs_function_h *rfuncs = sinfo->rfuncs, *nrfuncs ;
  int32 nfuncs = sinfo->nfuncs, ncolors = sinfo->ncolors, i, ci ;
  uint32 rfunc_size = RCBS_FUNC_HARRAY_SPACE(ncolors) ;

  HQASSERT(ncolors > 0, "No colors for shading functions") ;

  nrfuncs = (rcbs_function_h *)dl_alloc(sinfo->page->dlpools,
                                        rfunc_size, MM_ALLOC_CLASS_SHADING) ;

  if ( nrfuncs == NULL )
    return error_handler(VMERROR) ;

  nrfcis = (COLORANTINDEX *)((uint8 *)nrfuncs + RCBS_FUNC_CINDEX_OFFSET(ncolors)) ;

  for ( ci = 0 ; ci < ncolors ; ++ci ) {
    /* Find function with pseudo index colorant[ci] and insert */
    for ( i = 0 ; i < nfuncs ; ++i ) {
      if ( rfcis[i] == colorants[ci] )
        break ;
    }

    if ( i == nfuncs ) { /* Colorant not found. Install overprint function */
      if ( !rcbs_fn_overprint(sinfo, &nrfuncs[ci], sinfo->ncomps) ) {
        dl_free(sinfo->page->dlpools, (mm_addr_t)nrfuncs, rfunc_size,
                MM_ALLOC_CLASS_SHADING);
        return FALSE ;
      }
    } else { /* Used up a function */
      nrfuncs[ci] = rfuncs[i] ;
      /* Shorten functions array to reduce searching */
      --nfuncs ;
      rfuncs[i] = rfuncs[nfuncs] ;
      rfcis[i] = rfcis[nfuncs] ;
    }

    nrfcis[ci] = colorants[ci] ;
  }

  /* We may have more functions than colorants if knockouts were removed in
     generating the recombined colors. Free the remaining functions. */
  for ( i = 0 ; i < nfuncs ; ++i )
    rcbs_fn_free(&rfuncs[i], sinfo->page) ;

  /* Free the old function header */
  dl_free(sinfo->page->dlpools, (mm_addr_t)rfuncs,
          RCBS_FUNC_HARRAY_SPACE(sinfo->nfuncs),
          MM_ALLOC_CLASS_SHADING);

  /* And set the number of functions to the number of colours */
  sinfo->rfuncs = nrfuncs ;
  sinfo->rfcis = nrfcis ;
  sinfo->nfuncs = sinfo->ncolors ;

  return TRUE ;
}


/*****************************************************************************/
/* Linearised function structures. Since transfer functions are pre-applied in
   pre-separated and recombined jobs, it is awkward to try to save the whole
   of the colorspace, function, and transfer data for each separation in order
   to reconstruct a composite shfill during recombine adjustment.

   These functions are used instead, to create a piecewise linearly
   interpolated representation of the color values resulting from the function,
   with the transfers applied. These are used instead of the normal function
   application when reconstructing recombined shfills. The function is
   represented over the range of input parameters used. Discontinuities are
   represented so that evaluation upward/downward will not introduce aliasing
   artifacts.

   All external references use the opaque rcbs_function_h type to access
   these functions.

   The functions represented can multi-dimensional input, but are required
   to be single output. Multi-dimensional functions are represented by
   chaining single-input functions together. */

typedef struct rcbs_sample_t {
  USERVALUE in  ; /* Input parameter to function */
  union {
    USERVALUE out ;        /* Output result */
    rcbs_function_h next ; /* Next dimension's function */
  } u ;
  int8 order ;             /* Discontinuity order (-1 if none) */
  uint8 spare1, spare2, spare3 ;
} rcbs_sample_t ;

typedef struct rcbs_function_t {
  int32 nsamples ;           /* Number of samples in linearised function */
  int32 ninputs ;            /* Number of input dimensions */
  rcbs_sample_t samples[1] ; /* Extended by following memory allocation */
} rcbs_function_t ;

#define RCBS_FUNCTION_SIZE(_s_) \
        (((_s_) - 1) * sizeof(rcbs_sample_t) + sizeof(rcbs_function_t))

#define RCBS_FN_MAX_INPUTS 2
static USERVALUE rcbs_fn_inputs[RCBS_FN_MAX_INPUTS] ;

/* Work out current color component as a normalised uservalue, invoking
   function and pre-applying transfers. */
static Bool rcbs_shading_color(USERVALUE *out, int32 cindex, USERVALUE t,
                               Bool upwards, SHADINGinfo *sinfo)
{
  COLORVALUE cv ;
  USERVALUE opacity = 1.0f;

  *out = -1.0f; /* silence compiler warning */

  rcbs_fn_inputs[cindex] = t ;

  if ( !shading_color(rcbs_fn_inputs, opacity, upwards, sinfo, FALSE) )
    return FALSE ;

  if ( !dlc_get_indexed_colorant(dlc_currentcolor(sinfo->page->dlc_context),
                                 rcbn_current_colorant(), &cv) ) {
    HQFAIL("should not have failed to get current colorant index" ) ;
    return error_handler(CONFIGURATIONERROR) ;
  }

  HQASSERT(cv <= COLORVALUE_MAX,
           "colorant value out of range" ) ;
  *out = COLORVALUE_TO_USERVALUE(cv) ;

  return TRUE ;
}

/* Add a single sample point evaluated as specified to a linearised
   function. */
static Bool rcbs_add_sample(rcbs_function_t *linfn, int32 *remaining,
                            int32 cindex, USERVALUE t, Bool upwards,
                            int32 order, SHADINGinfo *sinfo)
{
  mm_pool_t sh_pool = dl_choosepool(sinfo->page->dlpools, MM_ALLOC_CLASS_SHADING);

  HQASSERT(linfn, "Invalid linearised function pointer") ;
  HQASSERT(remaining, "Invalid remaining pointer") ;

  HQASSERT(linfn->nsamples >= 0,
           "Linearised function number of samples out of range") ;

  if ( (*remaining -= sizeof(rcbs_sample_t)) >= 0 ) {
    rcbs_sample_t *sample = mm_dl_promise_next(sh_pool, sizeof(rcbs_sample_t)) ;

    HQASSERT(sample == &(linfn->samples[linfn->nsamples]),
             "promise allocation at unexpected location") ;

    if ( cindex + 1 == sinfo->ncomps ) { /* Last component */
      if ( !rcbs_shading_color(&sample->u.out, cindex, t, upwards, sinfo) )
        return FALSE ;

      HQTRACE(rcbs_trace > 1,
              ("Sample %d parameter %f value %f", linfn->nsamples, t, sample->u.out)) ;
    } else {
      /* More dimensions to come; set next dimension function ptr to NULL */
      sample->u.next = NULL ;
    }
    sample->in = t ;
    sample->order = (int8)order ;
  }

  linfn->nsamples += 1 ;

  return TRUE ;
}

/* Linearise a section of the function, excluding the endpoints (unless an
   order zero discontinuity exists there). */
static Bool rcbs_fn_linearise_section(rcbs_function_t *linfn,
                                      int32 *remaining,
                                      int32 cindex,
                                      USERVALUE d0, USERVALUE d1,
                                      SHADINGinfo *sinfo, int32 offset)
{
  int32 order, index, lincount, linear = TRUE ;
  USERVALUE discont, bounds[2], c0, c1 ;
  SYSTEMVALUE weights[2] ;

  bounds[0] = d0 ;
  bounds[1] = d1 ;

  HQASSERT(sinfo->nfuncs == 1, "Should only be one function in preseparated") ;

  if ( !fn_find_discontinuity(&sinfo->funcs[offset], cindex,
                              bounds, &discont, &order,
                              FN_SHADING, offset, sinfo->base_fuid,
                              FN_GEN_NA, &sinfo->fndecode) )
    return FALSE ;

  if ( order >= 0 ) {
    /* Discontinuity found. Store samples either side of discontinuity if
       order 0, or single sample at discontinuity if higher order. */

    if ( !rcbs_fn_linearise_section(linfn, remaining, cindex,
                                    d0, discont, sinfo, offset) )
      return FALSE ;

    if ( order == 0 ) { /* Discontinuous in 0th derivative (unconnected) */
      /* Adjacent sections will deal with d0(DOWNWARDS) and d1(UPWARDS) */

      if ( discont > d0 &&
           !rcbs_add_sample(linfn, remaining, cindex, discont,
                            FALSE, order, sinfo) )
        return FALSE ;

      if ( discont < d1 &&
           !rcbs_add_sample(linfn, remaining, cindex, discont,
                            TRUE, order, sinfo) )
        return FALSE ;
    } else /* Connected but discontinuous in higher derivative. Evaluation
              direction is unimportant. */
      if ( !rcbs_add_sample(linfn, remaining, cindex, discont,
                            TRUE, order, sinfo) )
        return FALSE ;

    return rcbs_fn_linearise_section(linfn, remaining, cindex,
                                     discont, d1, sinfo, offset) ;
  }

  if ( !rcbs_shading_color(&c0, cindex, d0, TRUE, sinfo) ||
       !rcbs_shading_color(&c1, cindex, d1, FALSE, sinfo) )
    return FALSE ;

  /* No discontinuities between d0 & d1. Now determine if it is linear */
  weights[0] = weights[1] = 0.5 ;
  sobol(-1, &weights[0], &weights[1]) ;

  /* Add 2 to linearity test count to ensure that points either side of
     centre are always tested. */
  lincount = max(UserParams.BlendLinearity,
                 UserParams.GouraudLinearity) + 2 ;

  /* Quasi-random sampling of continuous section to determine if linear. Index
     starts at 1 to skip first term in Sobol' sequence, since both (1,0) and
     (2,1) sequences used have first term 0.5, which would result in the
     second sample point being at d1. */
  for ( index = 1 ; ; ++index ) {
    USERVALUE t = (USERVALUE)(weights[0] * d0 + weights[1] * d1) ;
    USERVALUE cti = (USERVALUE)(weights[0] * c0 + weights[1] * c1) ;
    USERVALUE ct ;

    HQASSERT(fabs(1 - weights[0] - weights[1]) < EPSILON,
             "sum of weights should be unity") ;

    if ( !rcbs_shading_color(&ct, cindex, t, t < d1, sinfo) )
      return FALSE ;

    HQTRACE(rcbs_trace > 2,
            ("Sobol' sampling weights %f,%f : %f -> %f : %f",
             weights[0], weights[1], t, ct, cti)) ;

    if ( fabs(ct - cti) * sinfo->smoothnessbands > 1.0 ) {
      linear = FALSE ;
      break ;
    }

    if ( index > lincount ) /* Done enough testing */
      break ;

    sobol(index, &weights[0], &weights[1]) ;
    weights[1] = 1.0 - weights[0] ;
  }

  if ( !linear ) {
    USERVALUE half = (d0 + d1) / 2 ;

    /* Subdivide and add point at half way. Nothing special about dividing at
       half-way point, it just keeps the code simple. Evaluation direction
       is irrelevant. */

    if ( !rcbs_fn_linearise_section(linfn, remaining, cindex, d0, half, sinfo, offset) ||
         !rcbs_add_sample(linfn, remaining, cindex, half, TRUE, -1, sinfo) ||
         !rcbs_fn_linearise_section(linfn, remaining, cindex, half, d1, sinfo, offset) )
      return FALSE ;
  }

  return TRUE ;
}

Bool rcbs_fn_linearise(rcbs_function_h *linearised_fn,
                       int32 cindex, USERVALUE *domain,
                       SHADINGinfo *sinfo, int32 offset)
{
  int32 size = RCBS_FUNCTION_SIZE(256) ;
  size_t usize ;
  int32 actual_size ;
  rcbs_function_t *linfn ;
  USERVALUE d0, d1 ;
  mm_pool_t sh_pool = dl_choosepool(sinfo->page->dlpools, MM_ALLOC_CLASS_SHADING);


  HQASSERT(linearised_fn, "Invalid return handle pointer") ;
  HQASSERT(domain, "Invalid domain pointer") ;
  HQASSERT(sinfo, "Invalid shading info pointer") ;
  HQASSERT(cindex >= 0 && cindex < sinfo->ncomps, "Invalid component index") ;
  HQASSERT(sinfo->ncomps <= RCBS_FN_MAX_INPUTS, "Too many input components") ;

  if ( domain[0] <= domain[1] ) {
    d0 = domain[0] ;
    d1 = domain[1] ;
  } else {
    d0 = domain[1] ;
    d1 = domain[0] ;
  }

  for (;;) {
    int32 remaining = size ;

    /* Use DL promise functions to guess at appropriate number of samples */
    if ( mm_dl_promise(sh_pool, size) != MM_SUCCESS )
      return error_handler(VMERROR) ;

    linfn = (rcbs_function_t *)mm_dl_promise_next(sh_pool,
                              sizeof(rcbs_function_t) - sizeof(rcbs_sample_t)) ;
    remaining -= sizeof(rcbs_function_t) - sizeof(rcbs_sample_t) ;

    HQASSERT(linfn, "Promise was broken") ;

    linfn->nsamples = 0 ;
    linfn->ninputs = sinfo->ncomps - cindex ;

    /* Initial point is d0 with FN_EVAL_DOWN, final point is d1 with
       FN_EVAL_UP. Section between is sub-divided for discontinuities and
       linearised. */
    if ( !rcbs_add_sample(linfn, &remaining, cindex, d0, FALSE, -1, sinfo) ||
         !rcbs_fn_linearise_section(linfn, &remaining, cindex, d0, d1, sinfo, offset) ||
         !rcbs_add_sample(linfn, &remaining, cindex, d1, TRUE, -1, sinfo) ) {
      mm_dl_promise_free(sh_pool);
      return FALSE ;
    }

    size -= remaining ; /* Adjust size to amount actually used */
    if ( remaining >= 0 )
      break ;

    /* Free promise, loop round again with correct size instead of guess */
    mm_dl_promise_free(sh_pool);
  }

  usize = mm_dl_promise_end(sh_pool);
  actual_size = CAST_UNSIGNED_TO_INT32(usize);
  HQASSERT(actual_size == size, "Rcb shfill size not consistent with promise");

  /* If the dimensionality was more than 1, we just created sample structures.
     Now recurse and fill in the other dimensions. The recursion is done at
     the end because we can't promises recursively. */
  if ( linfn->ninputs > 1 ) {
    int32 i ;

    domain += 2 ; /* Next dimension's input range */

    for ( i = 0 ; i < linfn->nsamples ; ++i ) {
      rcbs_sample_t *sample = &linfn->samples[i] ;

      rcbs_fn_inputs[cindex] = sample->in ;

      HQASSERT(sample->u.next == NULL, "Shouldn't have filled in sample yet") ;
      if ( !rcbs_fn_linearise(&sample->u.next, cindex + 1, domain, sinfo, offset) ) {
        rcbs_fn_free((rcbs_function_h *)&linfn, sinfo->page) ;
        return FALSE ;
      }
    }
  }

  *linearised_fn = (rcbs_function_h)linfn ;

  HQTRACE(rcbs_trace, ("Linearised function: %d samples", linfn->nsamples)) ;

  return TRUE ;
}

/* Evaluate function to preseparated color by piecewise linear interpolation.
   Result is a DL color. */
Bool rcbs_fn_evaluate_with_direction(rcbs_function_h lfnhandle,
                                     USERVALUE *inputs,
                                     USERVALUE *output,
                                     Bool upwards)
{
  int32 last ;
  rcbs_function_t *linfn ;
  rcbs_sample_t *samples ;
  USERVALUE t ;

  HQASSERT(inputs, "Invalid input parameter pointer") ;
  HQASSERT(output, "Invalid output parameter pointer") ;

  linfn = (rcbs_function_t *)lfnhandle ;
  HQASSERT(linfn, "No linearised function pointer") ;
  HQASSERT(linfn->nsamples >= 2, "Not enough samples in linearised function") ;

  /* Deal with inputs one at a time; multi-dim functions represented as
     arrays of functions. */
  t = *inputs++ ;

  last = linfn->nsamples - 1 ; /* index of last sample */
  samples = &linfn->samples[0] ;

  /* Binary search through samples for bounding samples. If evaluating upwards,
     samples at point t are compared less than t, if downwards samples at t
     compare greater than t. */
  while ( last >= 2 ) {
    int32 index = last / 2 ;
    if ( t < samples[index].in ||
         (t == samples[index].in && !upwards) ) {
      last = index ;
    } else {
      samples += index ;
      last -= index ;
    }
  }

  HQASSERT(last == 1, "Binary search did not converge properly") ;

  /* Check parameter is in domain of function. EPSILON limits are because
     rounding errors can cause tiny overflows in axial blend testing; the
     code below clips these to the function's domain. */
  HQASSERT(samples[0].in - EPSILON <= t && samples[1].in + EPSILON >= t,
           "Parameter not in domain of linearised function") ;

  /* Check if DL color interpolation is unnecessary */
  if ( t <= samples[0].in ) {
    int32 index = (upwards && t == samples[1].in) ;

    if ( linfn->ninputs > 1 )
      return rcbs_fn_evaluate_with_direction(samples[index].u.next,
                                             inputs, output, upwards) ;

    *output = samples[index].u.out ;
  } else if ( t >= samples[1].in ) {
    int32 index = (upwards || t != samples[0].in) ;

    if ( linfn->ninputs > 1 )
      return rcbs_fn_evaluate_with_direction(samples[index].u.next,
                                             inputs, output, upwards) ;

    *output = samples[index].u.out ;
  } else if ( linfn->ninputs > 1 ) {
    USERVALUE c0, c1 ;
    SYSTEMVALUE weights[2], delta ;

    /* Linearly interpolate between samples[0] and samples[1]. */
    HQASSERT(samples[1].in > samples[0].in,
             "Samples not ordered in multi-dimen linearised function") ;
    delta = samples[1].in - samples[0].in ;
    weights[0] = (samples[1].in - t) / delta ;
    weights[1] = (t - samples[0].in) / delta ;

    if ( !rcbs_fn_evaluate_with_direction(samples[0].u.next, inputs,
                                          &c0, upwards) ||
         !rcbs_fn_evaluate_with_direction(samples[1].u.next, inputs,
                                          &c1, upwards) )
      return FALSE ;

    *output = (USERVALUE)(weights[0] * c0 + weights[1] * c1) ;
  } else {
    SYSTEMVALUE weights[2], delta ;

    /* Linearly interpolate between samples[0] and samples[1]. */
    HQASSERT(samples[1].in > samples[0].in,
             "Samples not ordered in linearised function") ;
    delta = samples[1].in - samples[0].in ;
    weights[0] = (samples[1].in - t) / delta ;
    weights[1] = (t - samples[0].in) / delta ;

    *output = (USERVALUE)(weights[0] * samples[0].u.out +
                          weights[1] * samples[1].u.out) ;
  }

  return TRUE ;
}

Bool rcbs_fn_find_discontinuity(rcbs_function_h lfnhandle,
                                int32 dimen,
                                USERVALUE bounds[2],
                                USERVALUE *discont,
                                int32 *order)
{
  rcbs_function_t *linfn = (rcbs_function_t *)lfnhandle ;

  HQASSERT(linfn, "Invalid linearised function pointer") ;
  HQASSERT(discont, "Invalid discontinuity return pointer") ;
  HQASSERT(order, "Invalid order return pointer") ;
  HQASSERT(bounds[1] >= bounds[0], "Bounds out of order") ;

  /* Multi-dimensional functions should have same discontinuities in
     each sub-function. */
  while ( dimen > 0 ) {
    HQASSERT(linfn->ninputs > 1, "Invalid discontinuity search dimension") ;

    linfn = (rcbs_function_t *)linfn->samples[0].u.next ;
    --dimen ;
  }

  if ( bounds[1] > bounds[0] ) {
    rcbs_sample_t *samples = &linfn->samples[0] ;
    int32 last = linfn->nsamples - 1 ;
    while ( last >= 0 ) {
      int32 index = last / 2;
      if ( samples[index].in <= bounds[0] ) {
        samples += index + 1 ;
        last -= index + 1 ;
      } else if ( samples[index].in >= bounds[1] ) {
        last = index - 1 ;
      } else if ( samples[index].order >= 0 ) {
        /* We've found one. */
        *discont = samples[index].in ;
        *order = samples[index].order ;
        return TRUE ;
      } else { /* Found non-discontinuity sample. Try again. */
        USERVALUE range[2] ;

        range[0] = bounds[0] ;
        range[1] = samples[index].in ;

        if ( !rcbs_fn_find_discontinuity((rcbs_function_h)linfn, 0,
                                         range, discont, order) )
          return FALSE ;

        if ( *order != -1 )
          return TRUE ;

        range[0] = samples[index].in ;
        range[1] = bounds[1] ;

        return rcbs_fn_find_discontinuity((rcbs_function_h)linfn, 0,
                                          range, discont, order) ;
      }
    }
  }

  *order = -1 ;

  return TRUE ;
}

static void rcbs_fn_free_samples(rcbs_function_h lfnhandle, int32 *total)
{
  int32 index ;
  rcbs_function_t *linfn = (rcbs_function_t *)lfnhandle ;

  for ( index = 0 ; index < linfn->nsamples ; ++index ) {
    if ( linfn->ninputs > 1 && linfn->samples[index].u.next )
      rcbs_fn_free_samples(linfn->samples[index].u.next, total) ;
  }

  *total += RCBS_FUNCTION_SIZE(linfn->nsamples) ;
}

void rcbs_fn_free(rcbs_function_h *lfnhandle, DL_STATE *page)
{
  HQASSERT(lfnhandle, "Invalid recombined function handle pointer") ;
  HQASSERT(page, "Invalid DL page pointer") ;

  if ( *lfnhandle != NULL ) {
    int32 size = 0 ;

    rcbs_fn_free_samples(*lfnhandle, &size) ;

    dl_free(page->dlpools, (mm_addr_t)*lfnhandle, size,
            MM_ALLOC_CLASS_SHADING);

    *lfnhandle = (rcbs_function_h)NULL ;
  }
}

/* Create a merged color for a function evaluated over part of its domain. We
   don't actually need the true merge of all of the colours, just a
   representative colour which is clear, solid or intermediate depending on
   whether the function is all clear, all solid, or mixed or intermediate. */
static Bool rcbs_fn_merged_color(rcbs_function_h lfnhandle,
                                 USERVALUE domain[2],
                                 COLORVALUE *out,
                                 Bool inwards)
{
  rcbs_function_t *linfn = (rcbs_function_t *)lfnhandle ;
  int32 nsamples, nmerged = 0 ;
  rcbs_sample_t *samples ;
  COLORVALUE c = 0, c0 = 0 ;

  HQASSERT(linfn, "Invalid linearised function pointer") ;
  HQASSERT(domain[0] <= domain[1], "Domain out of order") ;

  nsamples = linfn->nsamples ;
  samples = linfn->samples ;

  /* If inwards flag is set, eval d1 downwards and d0 upwards. Otherwise
     evaluate both upwards */
  while ( nsamples > 1 && samples[1].in <= domain[0] ) {
    --nsamples ;
    ++samples ;
  }

  /* Found lower limit. Now merge colours until upper limit is exceeded. */
  while ( nsamples > 0 && samples->in <= domain[1] ) {
    HQTRACE(rcbs_trace > 1, ("Merged color %f..%f mixing %f",
                             domain[0], domain[1], samples->in)) ;

    if ( linfn->ninputs > 1 ) {
      if ( !rcbs_fn_merged_color(samples->u.next, domain + 2, &c, inwards) )
        return FALSE ;
    } else {
      c = (COLORVALUE)(samples->u.out * COLORVALUE_MAX) ;
    }

    if ( c > 0 && c < COLORVALUE_MAX ) /* Quit if intermediate found */
      break ;

    if ( nmerged++ == 0 ) { /* First sample, set initial value */
      c0 = c ;
    } else if ( c0 != c ) { /* Subsequent sample, merge with previous */
      c = COLORVALUE_HALF ;
      break ;
    }

    if ( samples[0].in == domain[1] && inwards )
      break ;

    ++samples ;
    --nsamples ;
  }

  *out = c ;

  return TRUE ;
}

static Bool rcbs_fn_merged_dl_color(USERVALUE domain[2],
                                    Bool inwards,
                                    SHADINGinfo *sinfo)
{
  int32 i ;
  COLORVALUE *cvscratch = (COLORVALUE *)sinfo->scratch ;

  HQASSERT(sizeof(COLORVALUE) <= sizeof(USERVALUE),
           "Abused scratch space too small") ;
  HQASSERT(sinfo->rfuncs, "Invalid linearised function array") ;

  for ( i = 0 ; i < sinfo->nfuncs ; ++i ) {
    HQASSERT(i == 0 || sinfo->rfcis[i] > sinfo->rfcis[i - 1],
             "Colorant indices not ordered; dlc_alloc_fillin will fail") ;
    if ( !rcbs_fn_merged_color(sinfo->rfuncs[i], domain,
                               &cvscratch[i], inwards) )
      return FALSE ;
  }

  return dlc_alloc_fillin(sinfo->page->dlc_context, sinfo->nfuncs,
                          sinfo->rfcis, cvscratch,
                          dlc_currentcolor(sinfo->page->dlc_context)) ;
}

/* Create a function evaluating everything to zero */
static Bool rcbs_fn_overprint(SHADINGinfo *sinfo, rcbs_function_h *lfnh,
                              int32 ninputs)
{
  int32 i ;
  rcbs_function_t *linfn = dl_alloc(sinfo->page->dlpools, RCBS_FUNCTION_SIZE(2),
                                    MM_ALLOC_CLASS_SHADING);

  if ( linfn == NULL )
    return error_handler(VMERROR) ;

  linfn->nsamples = 2 ;
  linfn->ninputs = ninputs ;
  for ( i = 0 ; i < 2 ; ++i ) {
    linfn->samples[i].in = (USERVALUE)i ;
    linfn->samples[i].order = (int8)-1 ;
    if ( ninputs > 1 ) {
      if ( !rcbs_fn_overprint(sinfo, &linfn->samples[i].u.next, ninputs - 1) )
        return FALSE ;
    } else
      linfn->samples[i].u.out = 0.0f ;
  }

  *lfnh = (rcbs_function_h)linfn ;

  return TRUE ;
}

void init_C_globals_rcbshfil(void)
{
  size_t i ;

#if defined( ASSERT_BUILD )
  rcbs_trace = 0 ;
#endif
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(rcbs_fn_inputs) ; ++i )
    rcbs_fn_inputs[i] = 0.0f ;
}


/*
* Log stripped */
