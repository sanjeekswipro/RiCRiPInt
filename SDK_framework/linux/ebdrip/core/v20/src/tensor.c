/** \file
 * \ingroup coons
 *
 * $HopeName: SWv20!src:tensor.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for providing Coons- and tensor-mesh fills for Postscript
 * Level 3.
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "monitor.h"
#include "mm.h"

#include "control.h" /* interrupts_clear */
#include "often.h"
#include "graphics.h"
#include "routedev.h"
#include "vndetect.h"
#include "idlom.h"
#include "plotops.h"
#include "dl_bres.h"
#include "gstate.h"
#include "utils.h"
#include "gstack.h"
#include "gu_rect.h"
#include "swmemory.h"
#include "functns.h"
#include "gu_fills.h"
#include "constant.h"
#include "spdetect.h"
#include "halftone.h"
#include "pathops.h"
#include "matrix.h"
#include "gu_ctm.h"
#include "gu_path.h"
#include "ripdebug.h"
#include "devops.h"
#include "dlstate.h"
#include "shading.h" /* SHADING_DEBUG_* */
#include "shadecev.h"
#include "rcbshfil.h"
#include "dl_shade.h"
#include "tensor.h"
#include "fbezier.h" /* freduce */

#include "gschtone.h"
#include "params.h"
#include "metrics.h"
#include "interrupts.h"

#include "namedef_.h"

#ifdef DEBUG_BUILD
#include "pathcons.h" /* closepath_ */
#include "gu_cons.h" /* gs_moveto, gs_lineto */
#include "gschead.h" /* gsc_setcolorspacedirect */
#endif

#ifdef METRICS_BUILD
static struct tensor_metrics {
  int32 pdl_patches ;
  int32 g_patches, g_depth ;
  int32 e_patches, e_depth ;
  int32 f_patches, f_depth ;
  int32 c_patches, c_depth ;
  int32 recursion_depth ;
} tensor_metrics ;

static Bool tensor_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Shading")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Tensor")) )
    return FALSE ;
  SW_METRIC_INTEGER("pdl_patches", tensor_metrics.pdl_patches);
  SW_METRIC_INTEGER("geometry_splits", tensor_metrics.g_patches);
  SW_METRIC_INTEGER("max_geometry_splits", tensor_metrics.g_depth);
  SW_METRIC_INTEGER("edge_splits", tensor_metrics.e_patches);
  SW_METRIC_INTEGER("max_edge_splits", tensor_metrics.f_depth);
  SW_METRIC_INTEGER("function_splits", tensor_metrics.f_patches);
  SW_METRIC_INTEGER("max_function_splits", tensor_metrics.f_depth);
  SW_METRIC_INTEGER("color_splits", tensor_metrics.c_patches);
  SW_METRIC_INTEGER("max_color_splits", tensor_metrics.c_depth);
  sw_metrics_close_group(&metrics) ; /*Tensor*/
  sw_metrics_close_group(&metrics) ; /*Shading*/

  return TRUE ;
}

static void tensor_metrics_reset(int reason)
{
  struct tensor_metrics init = {0} ;
  UNUSED_PARAM(int, reason) ;
  tensor_metrics = init ;
}

static sw_metrics_callbacks tensor_metrics_hook = {
  tensor_metrics_update,
  tensor_metrics_reset,
  NULL
} ;

#define METRIC_RECURSE_BEGIN(name_, patches_) MACRO_START \
  tensor_metrics.name_ ## _patches += (patches_) ; \
  if ( ++tensor_metrics.recursion_depth > tensor_metrics.name_ ## _depth ) \
    tensor_metrics.name_ ## _depth = tensor_metrics.recursion_depth ; \
MACRO_END

#define METRIC_RECURSE_END() MACRO_START \
  --tensor_metrics.recursion_depth ; \
MACRO_END

#define METRIC_INCREMENT(name_) MACRO_START \
  ++tensor_metrics.name_ ## _patches ; \
MACRO_END

#else /*!METRICS_BUILD*/

#define METRIC_RECURSE_BEGIN(name_, patches_) EMPTY_STATEMENT()
#define METRIC_RECURSE_END() EMPTY_STATEMENT()
#define METRIC_INCREMENT(name_) EMPTY_STATEMENT()

#endif /*!METRICS_BUILD*/

/* A quick note on sorting the patches.
 *
 * PS3v3010, p92: "Patches can sometimes appear to fold over themselves - for
 * example, if a boundary curve is self-intersecting. [...] If more than one
 * parameter space location (u,v) is mapped to the same location in device
 * space, the value of (u,v) selected will be that with the largest value of
 * v, and if multiple (u,v) values have the same v, that with the largest
 * value of u."
 *
 * So, associated with each patch are two numbers representing the
 * coordinates of the centre of the patch in u,v space.
 */

/** Bitmask flags for outside edges of patches. This is used to make patches
   line up with each other; at any outside edge, decomposition follows the
   Bezier curve for outside edges until such point as they are decomposed to
   straight line segments. Outside edges are linearly interpolated after that
   point. */
enum { EDGE_FLAG_U_0 = 1,
       EDGE_FLAG_U_1 = 2,
       EDGE_FLAG_V_0 = 4,
       EDGE_FLAG_V_1 = 8,
       EDGE_FLAG_ALL = 15 } ;

/** Used for vertex interpolation */
static SYSTEMVALUE halfs[2] = {0.5, 0.5} ;

/** NB: doesn't free the patch itself, just its children. */
static void destroy_patch_children( SHADINGinfo *sinfo,  SHADINGpatch *patch )
{
  HQASSERT(sinfo, "sinfo NULL in free_patch") ;
  HQASSERT(patch, "patch NULL in free_patch") ;

  if ( isIPatchDivided(patch) ) {
    HQASSERT( patch->decomp_type != TENSOR_UV_INVALID,
              "Patch has children which shouldn't in free_patch" ) ;
    destroy_patch_children( sinfo, &patch->children[0] ) ;
    destroy_patch_children( sinfo, &patch->children[1] ) ;
    if ( ! patch->is_first_vertex_shared ) {
      /* Both vertices were allocated for this patch */
      vertex_free(sinfo, &patch->children[1].corner[0][0], 1) ;
    }
    /* Bottom vertex was (also?) allocated on behalf of this patch. */
    vertex_free(sinfo, &patch->children[0].corner[1][1], 1) ;
    mm_free( mm_pool_shading, patch->children, sizeof( SHADINGpatch ) * 2 ) ;
    patch->children = NULL ;
    patch->decomp_type = TENSOR_UV_INVALID;
  }
}

static int32 find_spos(int32 pos)
{
  HQASSERT(pos > 0, "pos should be positive") ;
  while ( pos > 0 )
    pos <<= 1;

  return pos;
}

static void tensor_divide_u(SHADINGtensor *tensor, SHADINGtensor children[],
                            SHADINGvertex *vtx[], int32 first_shared)
{
  int i, j ;
  SHADINGinfo *sinfo ;
  SHADINGtensor *c0, *c1;

  HQASSERT(tensor, "tensor NULL in tensor_divide_u") ;
  HQASSERT(children, "children NULL in tensor_divide_u") ;
  c0 = & children[0] ; c1 = & children[1] ;
  HQASSERT(vtx, "vtx NULL in tensor_divide_u") ;
  sinfo = tensor->sinfo ;
  HQASSERT(sinfo, "sinfo NULL in tensor_divide_u") ;

  for ( i = 0; i < 4; i++ ) {
    for ( j = 0; j < 2; j++ ) {
      SYSTEMVALUE t1, t2, t3, t4 ;

      /* Use same subdivision as Beziers to make rounding as similar as
         possible. */
      t1 = c0->coord[0][i][j] = tensor->coord[0][i][j] ;
      t2 = tensor->coord[1][i][j] ;
      c0->coord[1][i][j] = t1 = (t1 + t2) * 0.5 ;

      t4 = c1->coord[3][i][j] = tensor->coord[3][i][j] ;
      t3 = tensor->coord[2][i][j] ;
      c1->coord[2][i][j] = t4 = (t3 + t4) * 0.5 ;

      t2 = (t2 + t3) * 0.5 ;
      c0->coord[2][i][j] = t1 = (t1 + t2) * 0.5 ;
      c1->coord[1][i][j] = t2 = (t2 + t4) * 0.5 ;

      c0->coord[3][i][j] = c1->coord[0][i][j] = (t1 + t2) * 0.5 ;
    }
  }

  for ( i = 0; i < 2; i++ ) {
    c0->patch->corner[0][i] = tensor->patch->corner[0][i] ;
    c1->patch->corner[1][i] = tensor->patch->corner[1][i] ;
    if ( !first_shared || i == 1 ) {
      SHADINGvertex *v[2] ;

      v[0] = tensor->patch->corner[0][i] ;
      v[1] = tensor->patch->corner[1][i] ;
      vertex_interpolate(sinfo, 2, halfs, vtx[i], &v[0], sinfo->ncomps) ;

      /* Adjust vertex coordinate to reflect position on Bezier */
      vtx[i]->x = c0->coord[3][3 * i][0] ;
      vtx[i]->y = c0->coord[3][3 * i][1] ;
    }
    c0->patch->corner[1][i] = c1->patch->corner[0][i] = vtx[i] ;
  }
}

static void tensor_divide_v(SHADINGtensor *tensor, SHADINGtensor children[],
                            SHADINGvertex *vtx[], int32 first_shared)
{
  int i, j ;
  SHADINGinfo *sinfo ;
  SHADINGtensor *c0, *c1;

  HQASSERT(tensor, "tensor NULL in tensor_divide_v") ;
  HQASSERT(children, "children NULL in tensor_divide_v") ;
  c0 = & children[0] ; c1 = & children[1] ;
  HQASSERT(vtx, "vtx NULL in tensor_divide_v") ;
  sinfo = tensor->sinfo ;
  HQASSERT(sinfo, "sinfo NULL in tensor_divide_v") ;

  for ( i = 0; i < 4; i++ ) {
    for ( j = 0; j < 2; j++ ) {
      SYSTEMVALUE t1, t2, t3, t4 ;

      /* Use same subdivision as Beziers to make rounding as similar as
         possible. */
      t1 = c0->coord[i][0][j] = tensor->coord[i][0][j] ;
      t2 = tensor->coord[i][1][j] ;
      c0->coord[i][1][j] = t1 = (t1 + t2) * 0.5 ;

      t4 = c1->coord[i][3][j] = tensor->coord[i][3][j] ;
      t3 = tensor->coord[i][2][j] ;
      c1->coord[i][2][j] = t4 = (t3 + t4) * 0.5 ;

      t2 = (t2 + t3) * 0.5 ;
      c0->coord[i][2][j] = t1 = (t1 + t2) * 0.5 ;
      c1->coord[i][1][j] = t2 = (t2 + t4) * 0.5 ;

      c0->coord[i][3][j] = c1->coord[i][0][j] = (t1 + t2) * 0.5 ;
    }
  }

  for ( i = 0; i < 2; i++ ) {
    c0->patch->corner[i][0] = tensor->patch->corner[i][0] ;
    c1->patch->corner[i][1] = tensor->patch->corner[i][1] ;
    if ( !first_shared || i == 1 ) {
      SHADINGvertex *v[2] ;

      v[0] = tensor->patch->corner[i][0] ;
      v[1] = tensor->patch->corner[i][1] ;
      vertex_interpolate(sinfo, 2, halfs, vtx[i], &v[0], sinfo->ncomps) ;

      /* Adjust vertex coordinate to reflect position on Bezier */
      vtx[i]->x = c0->coord[3 * i][3][0] ;
      vtx[i]->y = c0->coord[3 * i][3][1] ;
    }
    c0->patch->corner[i][1] = c1->patch->corner[i][0] = vtx[i] ;
  }
}

static Bool tensor_divide(SHADINGtensor *tensor, SHADINGtensor children[], int32 dim)
{
  SHADINGinfo *sinfo ;
  SHADINGpatch *neighbor, *child_patches ;
  SHADINGvertex *shared_vtx , *vtx[2] ;
  int32 i;

  HQASSERT(tensor, "tensor NULL in tensor_divide") ;
  HQASSERT(children, "children NULL in tensor_divide") ;
  HQASSERT(dim == TENSOR_U || dim == TENSOR_V,
           "dim isn't a valid dimension in tensor_divide") ;
  HQASSERT( ! isIPatchDivided( tensor->patch ), "tensor already divided in tensor_divide") ;
  sinfo = tensor->sinfo ;
  HQASSERT(sinfo, "sinfo NULL in tensor_divide") ;

  shared_vtx = NULL ;
  neighbor = tensor->neighbors[1 - dim];
  if ( neighbor ) {
    while ( isIPatchDivided( neighbor ) ) {
      if ( neighbor->decomp_type == dim ) {
        /* Our neighbour divides in half the same way we do */
        shared_vtx = neighbor->children[0].corner[1][1] ;
        break ;
      } else {
        /* Divided the other way - look at the half nearest us */
        neighbor = & neighbor->children[1] ;
      }
    }
  }
  child_patches = (SHADINGpatch *) mm_alloc( mm_pool_shading, sizeof( SHADINGpatch ) * 2,
    MM_ALLOC_CLASS_SHADING ) ;
  if ( child_patches == NULL ) {
    return error_handler( VMERROR ) ;
  }
  if ( shared_vtx ) {
    vtx[0] = shared_vtx ;
    if ( !vertex_alloc(&vtx[1], 1) ) {
      mm_free( mm_pool_shading, child_patches, sizeof( SHADINGpatch ) * 2 ) ;
      return error_handler( VMERROR ) ;
    }
    tensor->patch->is_first_vertex_shared = TRUE ;
  } else {
    if ( !vertex_alloc(vtx, 2) ) {
      mm_free( mm_pool_shading, child_patches, sizeof( SHADINGpatch ) * 2 ) ;
      return error_handler( VMERROR ) ;
    }
    tensor->patch->is_first_vertex_shared = FALSE ;
  }

  tensor->patch->decomp_type = dim ;
  tensor->patch->children = child_patches ;
  for ( i = 0; i < 2; i++ ) {
    children[i].sinfo = tensor->sinfo ;

    child_patches[i].decomp_type = TENSOR_UV_INVALID ;
    child_patches[i].children = NULL ;
    children[i].patch = & child_patches[i] ;

    children[i].pos[dim] = (tensor->pos[dim] << 1) | i ;
    children[i].pos[1 - dim] = tensor->pos[1 - dim] ;
    child_patches[i].spos[dim] = find_spos(children[i].pos[dim]);
    child_patches[i].spos[1 - dim] = tensor->patch->spos[1 - dim] ;
  }

  children[0].neighbors[dim] = tensor->neighbors[dim] ;
  children[1].neighbors[dim] = & child_patches[0] ;

  if ( neighbor && isIPatchDivided( neighbor ) ) {
    HQASSERT( neighbor->decomp_type == dim,
              "neighbor could be specified further in tensor_divide") ;
    children[0].neighbors[1 - dim] = & neighbor->children[0] ;
    children[1].neighbors[1 - dim] = & neighbor->children[1] ;
  } else {
    children[0].neighbors[1 - dim] = children[1].neighbors[1 - dim] = NULL ;
  }

  if ( dim == TENSOR_V ) {
    tensor_divide_v( tensor , children , vtx , tensor->patch->is_first_vertex_shared ) ;
  } else {
    tensor_divide_u( tensor , children , vtx , tensor->patch->is_first_vertex_shared ) ;
  }
  return TRUE ;
}

static void third_accuracy(SYSTEMVALUE *max_error, SYSTEMVALUE vector[],
                           SYSTEMVALUE nearer[], SYSTEMVALUE further[])
{
  SYSTEMVALUE diff, tmp;
  int32 i;

  HQASSERT(max_error, "max_error NULL in third_accuracy") ;
  HQASSERT(vector   , "vector NULL in third_accuracy") ;
  HQASSERT(nearer, "nearer NULL in third_accuracy") ;
  HQASSERT(further, "further NULL in third_accuracy") ;
  diff = 0;
  for (i = 0; i < 2; i++) {
    /* Avoid FP division because it's a significant slowdown */
    tmp = vector[i] - (nearer[i] * (1.0/1.5) + further[i] * (1.0/3.0));
    diff += tmp * tmp;
  }
  if ( diff > *max_error )
    *max_error = diff ;
}

/* Project point onto an extended line passing through two points. */
static void tensor_project(SYSTEMVALUE project[2],
                           SYSTEMVALUE start[2], SYSTEMVALUE end[2])
{
  SYSTEMVALUE dx, dy, tx, ty, delta ;

  tx = end[0] - start[0] ;
  delta = tx * tx ;
  ty = end[1] - start[1] ;
  delta += ty * ty ;

  dx = project[0] - start[0] ;
  dy = project[1] - start[1] ;
  if ( delta >= EPSILON ) {
    /* Project point onto start-end line. t is same as the dot product of
       vectors dxy =(dx,dy) and txy = (tx,ty), which is also
       |dxy| * cos(theta) / |txy| */
    SYSTEMVALUE t = (dx * tx + dy * ty) / delta ;
    project[0] = start[0] + t * tx ;
    project[1] = start[1] + t * ty ;
  } else {
    /* To be here, we must have freduce() failed for this line. We can't
       meaningfully project this line, so we'll just snap to the third accuracy
       that we desire. */
    HQASSERT(dx * dx + dy * dy <= fl_getftol(),
             "Tensor projection initial point too far off curve") ;
    project[0] = start[0] + dx / 3.0 ;
    project[1] = start[1] + dy / 3.0 ;
  }
}

/** Patches arriving at tensor_decompose are rectangular; the edge
   decomposition converts them to straight lines after they are decomposed
   to the same level as a normal Bezier flattening would do. */
static Bool tensor_decompose( SHADINGtensor *tensor )
{
  SHADINGinfo *sinfo ;
  int32 decision, i ;
  SYSTEMVALUE max_error_u, max_error_v ;
  SHADINGtensor children[2] ;

  HQASSERT(tensor, "tensor NULL in tensor_decompose") ;
  sinfo = tensor->sinfo ;
  HQASSERT(sinfo, "sinfo NULL in tensor_decompose") ;
  HQASSERT(!isIPatchDivided( tensor->patch ), "tensor already divided in tensor_decompose") ;

  decision = TENSOR_UV_INVALID ;
  /* Decide based on shape? */
  max_error_u = 0;
  max_error_v = 0;
  for ( i = 0; i < 4; i++ ) {
    third_accuracy( &max_error_u, tensor->coord[1][i],
      tensor->coord[0][i], tensor->coord[3][i] ) ;
    third_accuracy( &max_error_u, tensor->coord[2][i],
      tensor->coord[3][i], tensor->coord[0][i] ) ;
    third_accuracy( &max_error_v, tensor->coord[i][1],
      tensor->coord[i][0], tensor->coord[i][3] ) ;
    third_accuracy( &max_error_v, tensor->coord[i][2],
      tensor->coord[i][3], tensor->coord[i][0] ) ;
  }
  /* freduce */
  if ( max_error_u > max_error_v ) {
    if ( max_error_u > fl_getftol() ) {
      decision = TENSOR_U; /* Divide in U */
    }
  } else {
    if ( max_error_v > fl_getftol() ) {
      decision = TENSOR_V; /* Divide in V */
    }
  }

#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_TENSOR_THIRDS) == 0 )
#endif
    if ( decision != TENSOR_UV_INVALID && tensor->pos[decision] > 0 ) {
      SwOftenUnsafe();
      if ( !interrupts_clear(allow_interrupt) )
        return report_interrupt(allow_interrupt);

      if ( !tensor_divide( tensor, children, decision ) )
        return FALSE ;
      METRIC_RECURSE_BEGIN(g, 2) ;
      if ( !tensor_decompose( &children[0] ) ) {
        destroy_patch_children( tensor->sinfo , tensor->patch ) ;
        METRIC_RECURSE_END() ;
        return FALSE ;
      }
      if ( !tensor_decompose( &children[1] ) ) {
        destroy_patch_children( tensor->sinfo , tensor->patch ) ;
        METRIC_RECURSE_END() ;
        return FALSE ;
      }
      METRIC_RECURSE_END() ;
    }

  return TRUE ;
}

/** Decompose until outside edges are straight (within normal flatness
   tolerances), and then project off-curve control points for outside edges
   to straight line. */
static Bool tensor_decompose_edges( SHADINGtensor *tensor, uint32 edgeflags )
{
  SHADINGinfo *sinfo ;
  int32 i ;
  int32 decision = TENSOR_UV_INVALID ;
  uint32 edgemask = EDGE_FLAG_ALL ;
  SHADINGtensor children[2] ;

  HQASSERT(tensor, "tensor NULL") ;
  sinfo = tensor->sinfo ;
  HQASSERT(sinfo, "sinfo NULL") ;
  HQASSERT(!isIPatchDivided( tensor->patch ), "tensor already divided") ;
  HQASSERT((edgeflags & ~EDGE_FLAG_ALL) == 0, "Invalid tensor edge flags") ;

#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_TENSOR_EDGES) == 0 )
#endif
  {
    /* Divide tensor based on edges? We use freduce to make sure that the
       very same test is used as for Bezier curves. This does not take into
       account the off-curve points as colour modifiers, just the geometry
       decomposition of the patch edges. If we divide the V edges, we retain
       the flags that mark that the U edges of the sub-patch are still
       outside edges, and vice-versa for U. */
    if ( (edgeflags & EDGE_FLAG_U_0) != 0 ) {
      FPOINT points[4] ;

      for ( i = 0 ; i < 4 ; ++i ) {
        points[i].x = tensor->coord[0][i][0] ;
        points[i].y = tensor->coord[0][i][1] ;
      }

      if ( freduce(points) ) {
        decision = TENSOR_V ;
        edgemask = EDGE_FLAG_U_0|EDGE_FLAG_U_1 ; /* Preserve U edges */
      } else { /* Project edge U=0 onto straight line */
        tensor_project(tensor->coord[0][1],
                       tensor->coord[0][0], tensor->coord[0][3]) ;
        tensor_project(tensor->coord[0][2],
                       tensor->coord[0][3], tensor->coord[0][0]) ;
        edgeflags &= ~EDGE_FLAG_U_0 ; /* This edge is now straight */
      }
    }
    if ( (edgeflags & EDGE_FLAG_U_1) != 0 ) {
      FPOINT points[4] ;

      for ( i = 0 ; i < 4 ; ++i ) {
        points[i].x = tensor->coord[3][i][0] ;
        points[i].y = tensor->coord[3][i][1] ;
      }

      if ( freduce(points) ) {
        decision = TENSOR_V ;
        edgemask = EDGE_FLAG_U_0|EDGE_FLAG_U_1 ; /* Preserve U edges */
      } else { /* Project edge U=1 onto straight line */
        tensor_project(tensor->coord[3][1],
                       tensor->coord[3][0], tensor->coord[3][3]) ;
        tensor_project(tensor->coord[3][2],
                       tensor->coord[3][3], tensor->coord[3][0]) ;
        edgeflags &= ~EDGE_FLAG_U_1 ; /* This edge is now straight */
      }
    }

    /* V overrides U. We could try to do a similar test to tensor_decompose,
       but I don't see any point, since we'll end up decomposing this way
       anyway. I can't think of circumstances in which it'll make the
       decomposition level change. If we follow this pattern we may be able
       to move the patch sorting down to a local level, and remove the patch
       stray fixing. */
    if ( decision == TENSOR_UV_INVALID ) {
      if ( (edgeflags & EDGE_FLAG_V_0) != 0 ) {
        FPOINT points[4] ;

        for ( i = 0 ; i < 4 ; ++i ) {
          points[i].x = tensor->coord[i][0][0] ;
          points[i].y = tensor->coord[i][0][1] ;
        }

        if ( freduce(points) ) {
          decision = TENSOR_U ;
          edgemask = EDGE_FLAG_V_0|EDGE_FLAG_V_1 ; /* Preserve V edges */
        } else { /* Project edge V=0 onto straight line */
          tensor_project(tensor->coord[1][0],
                         tensor->coord[0][0], tensor->coord[3][0]) ;
          tensor_project(tensor->coord[2][0],
                         tensor->coord[3][0], tensor->coord[0][0]) ;
          edgeflags &= ~EDGE_FLAG_V_0 ; /* This edge is now straight */
        }
      }
      if ( (edgeflags & EDGE_FLAG_V_1) != 0 ) {
        FPOINT points[4] ;

        for ( i = 0 ; i < 4 ; ++i ) {
          points[i].x = tensor->coord[i][3][0] ;
          points[i].y = tensor->coord[i][3][1] ;
        }

        if ( freduce(points) ) {
          decision = TENSOR_U ;
          edgemask = EDGE_FLAG_V_0|EDGE_FLAG_V_1 ; /* Preserve V edges */
        } else { /* Project edge V=1 onto straight line */
          tensor_project(tensor->coord[1][3],
                         tensor->coord[0][3], tensor->coord[3][3]) ;
          tensor_project(tensor->coord[2][3],
                         tensor->coord[3][3], tensor->coord[0][3]) ;
          edgeflags &= ~EDGE_FLAG_V_1 ; /* This edge is now straight */
        }
      }
    }

    if ( decision != TENSOR_UV_INVALID && tensor->pos[decision] > 0 ) {
      SwOftenUnsafe();
      if ( !interrupts_clear(allow_interrupt) )
        return report_interrupt(allow_interrupt);

      if ( !tensor_divide( tensor, children, decision ) )
        return FALSE ;
      METRIC_RECURSE_BEGIN(e, 2) ;
      if ( !tensor_decompose_edges(&children[0],
                                  edgeflags & (EDGE_FLAG_V_0|EDGE_FLAG_U_0|edgemask)) ) {
        destroy_patch_children( tensor->sinfo , tensor->patch ) ;
        METRIC_RECURSE_END() ;
        return FALSE ;
      }
      if ( !tensor_decompose_edges(&children[1],
                                  edgeflags & (EDGE_FLAG_V_1|EDGE_FLAG_U_1|edgemask)) ) {
        destroy_patch_children( tensor->sinfo , tensor->patch ) ;
        METRIC_RECURSE_END() ;
        return FALSE ;
      }
      METRIC_RECURSE_END() ;
      return TRUE ;
    }
  }

  /* Now decompose based on 1/3 accuracy to flatten out non-linearities in
     patch internal coordinates. */
  return tensor_decompose(tensor) ;
}

static void patch_pull_to_edge( SHADINGpatch *patch,
                                 int32 dim, int32 dir, FPOINT *p0, FPOINT *p1)
{
  HQASSERT(patch, "patch NULL in patch_pull_to_edge") ;
  HQASSERT(dim == TENSOR_U || dim == TENSOR_V,
           "dim isn't a valid dimension in patch_pull_to_edge") ;
  HQASSERT(dir == 0 || dir == 1,
           "dir isn't a valid direction in patch_pull_to_edge") ;

  if ( ! isIPatchDivided( patch ) ) {
    if (dim == TENSOR_U) {
      patch->corner[dir][0]->x = p0->x ;
      patch->corner[dir][0]->y = p0->y ;
      patch->corner[dir][1]->x = p1->x ;
      patch->corner[dir][1]->y = p1->y ;
    } else {
      patch->corner[0][dir]->x = p0->x ;
      patch->corner[0][dir]->y = p0->y ;
      patch->corner[1][dir]->x = p1->x ;
      patch->corner[1][dir]->y = p1->y ;
    }
  } else if ( patch->decomp_type == dim ) {
    patch_pull_to_edge( & patch->children[dir], dim, dir, p0, p1 ) ;
  } else {
    FPOINT pp ;

    SwOftenUnsafe();

    pp.x = (p0->x + p1->x) * 0.5 ;
    pp.y = (p0->y + p1->y) * 0.5 ;
    patch_pull_to_edge( & patch->children[0], dim, dir, p0, &pp ) ;
    patch_pull_to_edge( & patch->children[1], dim, dir, &pp, p1 ) ;
  }
}

static void patch_fix_stray( SHADINGpatch *leaf, SHADINGpatch *nonleaf, int32 dim, int32 dir )
{
  FPOINT p0, p1 ;

  HQASSERT(leaf, "leaf patch NULL") ;
  HQASSERT(nonleaf, "nonleaf patch NULL") ;
  HQASSERT(dim == TENSOR_U || dim == TENSOR_V, "dim isn't a valid dimension in patch_pull_to_edge") ;
  HQASSERT(dir == 0 || dir == 1, "dir isn't a valid direction in patch_pull_to_edge") ;
  HQASSERT(leaf->decomp_type == TENSOR_UV_INVALID,
           "leaf isn't really a leaf") ;
  HQASSERT(nonleaf->decomp_type != TENSOR_UV_INVALID,
           "nonleaf is a leaf really") ;

  if (dim == TENSOR_U) {
    p0.x = leaf->corner[1 - dir][0]->x ;
    p0.y = leaf->corner[1 - dir][0]->y ;
    p1.x = leaf->corner[1 - dir][1]->x ;
    p1.y = leaf->corner[1 - dir][1]->y ;
  } else {
    p0.x = leaf->corner[0][1 - dir]->x ;
    p0.y = leaf->corner[0][1 - dir]->y ;
    p1.x = leaf->corner[1][1 - dir]->x ;
    p1.y = leaf->corner[1][1 - dir]->y ;
  }
  patch_pull_to_edge( nonleaf, dim, dir, &p0, &p1 );
}

static void patch_fix_strays_between( SHADINGpatch *first, SHADINGpatch *second, int32 dim )
{
  HQASSERT(first, "first patch NULL") ;
  HQASSERT(second, "second patch NULL") ;
  HQASSERT(dim == TENSOR_U || dim == TENSOR_V,
           "dim isn't a valid dimension in patch_fix_strays_between") ;

  SwOftenUnsafe();

  while ( first->decomp_type == dim ) {
    first = & first->children[1];
  }
  while ( second->decomp_type == dim ) {
    second = & second->children[0];
  }
  if ( first->decomp_type != TENSOR_UV_INVALID ) {
    if ( second->decomp_type != TENSOR_UV_INVALID ) {
      int i;

      for ( i = 0; i < 2; i++ ) {
        patch_fix_strays_between( & first->children[i] , & second->children[i] , dim ) ;
      }
    } else {
      patch_fix_stray( second, first, dim, 1 ) ;
    }
  } else {
    if ( second->decomp_type != TENSOR_UV_INVALID ) {
      patch_fix_stray( first, second, dim, 0 ) ;
    }
  }
}

static void patch_fix_strays( SHADINGpatch *patch )
{
  HQASSERT(patch, "patch NULL in patch_fix_strays") ;

  if ( patch->decomp_type != TENSOR_UV_INVALID ) {
    int i;

    patch_fix_strays_between( & patch->children[0], & patch->children[1], patch->decomp_type ) ;
    for ( i = 0; i < 2; i++ ) {
      patch_fix_strays( & patch->children[i] ) ;
    }
  }
}

/** Find discontinuity in patch and interpolate vertices to discontinuity if
   found. Called with v01 and v10 swapped to test both V and U. */
static Bool patch_discontinuity_divide(SHADINGinfo *sinfo, SHADINGvertex *v00,
                                        SHADINGvertex *v01, SHADINGvertex *v10,
                                        SHADINGvertex *v11, SHADINGvertex **vr)
{
  USERVALUE discont, bounds[2] ;
  int32 order, vindex, index ;
  SHADINGvertex *v[4] ;

  HQASSERT(sinfo->nfuncs != 0, "shouldn't be looking for discontinuities if no functions") ;

  v[0] = v00 ;
  v[1] = v01 ;
  v[2] = v10 ;
  v[3] = v11 ;

  vr[0] = vr[1] = NULL ;

  for ( vindex = 0 ; vindex < 2 ; ++vindex ) {
    SHADINGvertex *vtmp ;
    USERVALUE c0, c1 ;

    /* Mirror vertices. This is done before the test below so we can use
       continue if the bounds are equal. The same amount of processing is
       done if there are no discontinuities, since it would have been done
       at the end of the loop anyway. */
    vtmp = v[0] ; v[0] = v[2] ; v[2] = vtmp ;
    vtmp = v[1] ; v[1] = v[3] ; v[3] = vtmp ;

    c0 = v[0]->comps[0] ;
    c1 = v[1]->comps[0] ;

    if ( c0 < c1 ) {
      bounds[0] = c0 ;
      bounds[1] = c1 ;
    } else if ( c0 > c1 ) {
      bounds[0] = c1 ;
      bounds[1] = c0 ;
    } else /* Bounds are same, no discontinuity possible */
      continue ;

    for ( index = 0 ; index < sinfo->nfuncs ; ++index ) {
      SYSTEMVALUE weights[2], w ;

      if ( !(sinfo->funcs ?
             fn_find_discontinuity(&sinfo->funcs[index], 0, bounds,
                                   &discont, &order,
                                   FN_SHADING, index, sinfo->base_fuid,
                                   FN_GEN_NA, &sinfo->fndecode) :
             rcbs_fn_find_discontinuity(sinfo->rfuncs[index], 0, bounds,
                                        &discont, &order)) )
        return FALSE ;

      if ( order != -1 ) {
        HQASSERT(discont > bounds[0] && discont < bounds[1],
                 "Discontinuity not contained in range") ;

        if ( !vertex_alloc(vr, 2) )
          return error_handler(VMERROR) ;

        weights[1] = w = (discont - c0) / (c1 - c0) ;
        weights[0] = 1 - weights[1] ;

        /* Always a new vertex at discontinuity */
        vertex_interpolate(sinfo, 2, weights, vr[1 - vindex], &v[0], sinfo->ncomps) ;

        /* Fix up rounding errors in discontinuity interpolation by
           performing relative error check for sanity, then forcing the
           colour value to the discontinuity. Rounding errors in position and
           other colour components may cause minor drift, but there's not
           much we can do about that. */
        HQASSERT(fabs((vr[1 - vindex]->comps[0] - discont) / (c1 - c0)) < 1e-6,
                 "Interpolation to discontinuity failed") ;
        vr[1 - vindex]->comps[0] = discont ;

        vertex_interpolate(sinfo, 2, weights, vr[vindex], &v[2], sinfo->ncomps) ;

        if ( v[2]->comps[0] != v[3]->comps[0] ) {
          /* Fixup opposite vertex if it landed near a discontinuity. If there
             is a discontinuity close to the interpolated point, adjust the
             component value to the discontinuity. "Close" is determined by
             applying an EPSILON more and less than the weights, since we don't
             know the range of the component we can't apply an epsilon to
             that. */
          weights[1] = w - EPSILON ;
          if ( weights[1] < 0.0 )
            weights[1] = 0.0 ;
          weights[0] = 1 - weights[1] ;

          c0 = (USERVALUE)(v[2]->comps[0] * weights[0] + v[3]->comps[0] * weights[1]) ;

          weights[1] = w + EPSILON ;
          if ( weights[1] > 1.0 )
            weights[1] = 1.0 ;
          weights[0] = 1 - weights[1] ;

          c1 = (USERVALUE)(v[2]->comps[0] * weights[0] + v[3]->comps[0] * weights[1]) ;

          if ( c0 < c1 ) {
            bounds[0] = c0 ;
            bounds[1] = c1 ;
          } else if ( c0 > c1 ) {
            bounds[0] = c1 ;
            bounds[1] = c0 ;
          }

          if ( c0 != c1 ) {
            if ( !(sinfo->funcs ?
                   fn_find_discontinuity(&sinfo->funcs[index], 0, bounds,
                                         &discont, &order,
                                         FN_SHADING, index, sinfo->base_fuid,
                                         FN_GEN_NA, &sinfo->fndecode) :
                   rcbs_fn_find_discontinuity(sinfo->rfuncs[index], 0, bounds,
                                              &discont, &order)) ) {
              vertex_free(sinfo, vr, 2) ;
              return FALSE ;
            }

            if ( order != -1 ) {
              HQASSERT(discont > bounds[0] && discont < bounds[1],
                       "Discontinuity not contained in range") ;
              vr[vindex]->comps[0] = discont ;
            }
          }
        } else { /* v2 == v3 */
          HQASSERT(v[2]->comps[0] == vr[vindex]->comps[0],
                   "Interpolation failed to get same value") ;
        }

        return TRUE ;
      }
    }
  }

  return TRUE ;
}

/** If patch edge 00-01 intersects edge 10-11, return sorted vertices
   interpolating to intersection points. Called with v01 and v10 swapped to
   test both V and U. */
static Bool patch_self_intersect(SHADINGinfo *sinfo,
                                  SHADINGvertex *v00, SHADINGvertex *v01,
                                  SHADINGvertex *v10, SHADINGvertex *v11,
                                  SHADINGvertex **vr, int32 *same)
{
  SYSTEMVALUE bx, by, dx, dy, denom ;

  bx = v01->x - v00->x ;
  by = v01->y - v00->y ;
  dx = v11->x - v10->x ;
  dy = v11->y - v10->y ;

  denom = dx * by - dy * bx ;

  vr[0] = vr[1] = vr[2] = vr[3] = NULL ;

  if ( denom != 0.0 ) { /* Extended lines intersect */
    SYSTEMVALUE cxax = v10->x - v00->x ;
    SYSTEMVALUE cyay = v10->y - v00->y ;
    SYSTEMVALUE w0 = (cyay * bx - cxax * by) / denom ;
    SYSTEMVALUE w1 = (cyay * dx - cxax * dy) / denom ;

    if ( w0 > w1 ) { /* Sort weights */
      SYSTEMVALUE tmp = w0 ; w0 = w1 ; w1 = tmp ;
    }

    if ( w0 > EPSILON && w1 < 1.0 - EPSILON ) { /* Line segments intersect. */
      SYSTEMVALUE weights[2] ;
      SHADINGvertex *v[4] ;

      if ( !vertex_alloc(vr, 4) )
        return error_handler(VMERROR) ;

      *same = (w0 + EPSILON > w1 || w1 - EPSILON < w0) ;

      v[0] = v00 ;
      v[1] = v01 ;
      v[2] = v10 ;
      v[3] = v11 ;

      weights[1] = w0 ;
      weights[0] = 1 - weights[1] ;

      vertex_interpolate(sinfo, 2, weights, vr[0], &v[0], sinfo->ncomps) ;
      vertex_interpolate(sinfo, 2, weights, vr[1], &v[2], sinfo->ncomps) ;

      weights[1] = w1 ;
      weights[0] = 1 - weights[1] ;

      vertex_interpolate(sinfo, 2, weights, vr[2], &v[0], sinfo->ncomps) ;
      vertex_interpolate(sinfo, 2, weights, vr[3], &v[2], sinfo->ncomps) ;
    }
  }

  return TRUE ;
}

/** Find weight which will cause interpolation to discontinuity across c01-c10
   diagonal of patch. This weight is the solution of the quadratic equation
   describing the bilinear interpolation along the line U == V (from corner
   c00 to c11). The quadratic equation is:

       -(d - c11) +/- sqrt((d - c11) * (c00 - d))
      --------------------------------------------
                (c00 - d) - (d - c11)

   where d is the value of the component at 01 and 10 (c01, c10). There can
   be at most one root of this equation in the range 0..1. If there is no
   root to the equation, the patch is saddle shaped. The correct root is
   found by a case analysis to ensure that the range of the result is 0..1.
   If the denominator to the equation above is zero, the solution is 0.5,
   since the distance from each corner to the discontinuity is the same.

   The return value is outside the range 0..1 if no solution exists (i.e. the
   patch is a saddle shape). This is only used for function-based shadings
   with discontinuities at the moment, but may be extendable to measure the
   curvature of the colour contour for other patches. The weight returned is
   used to decompose the patch, speeding the convergence to function
   discontinuities. */
static SYSTEMVALUE patch_discontinuity_weight(SHADINGvertex *vd,
                                              SHADINGvertex *v00,
                                              SHADINGvertex *v11
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
                                              , SHADINGinfo *sinfo
#endif
                                              )
{
  SYSTEMVALUE d11, d00, s ;

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
 /* Mark unused even though its used within a HQASSERT below. In a
    non-asserted build, we get warnings otherwise. */
  UNUSED_PARAM(SHADINGinfo *, sinfo) ;
#endif

  HQASSERT(sinfo, "No shading info in patch_discontinuity_weight") ;
  HQASSERT(vd && v00 && v11, "Missing vertex in patch_discontinuity_weight") ;
  HQASSERT(sinfo->nfuncs > 0, "No functions in patch_discontinuity_weight") ;
  HQASSERT(sinfo->funcs != NULL || sinfo->rfuncs != NULL,
           "Function pointers NULL in patch_discontinuity_weight") ;
  HQASSERT(sinfo->ncomps == 1, "More than one component in patch_discontinuity_weight") ;

  d00 = v00->comps[0] - vd->comps[0] ;
  d11 = vd->comps[0] - v11->comps[0] ;

  /* Pick correct root of quadratic. Correct root is determined by case
     analysis of possible ranges. */
  if ( d00 * d11 < 0 ) {
    /* No solution. Patch is saddle-shaped. */
    return -1.0 ;
  }

  if ( d00 == d11 ) {
    /* Half way between corners. Can't use general case because of
       division by zero. */
    return 0.5 ;
  }

  HQASSERT((d00 >= 0 && d11 >= 0) || (d00 <= 0 && d11 <= 0),
           "Both differences must have same sign") ;

  s = sqrt(d00 * d11) ;

  if ( d00 < 0 || d11 < 0 )
    s = -s ;

  s = (s - d11) / (d00 - d11) ;

  HQASSERT(s >= 0.0 && s <= 1.0, "Discontinuity weight not in range 0..1") ;

  return s ;
}

/** Split patch into two triangles. The patch is split so that there are no
   discontinuities across the triangles; by this time, the patches have been
   divided along discontinuities so that the only discontinuities intersect
   patch corners. Since there can only be one discontinuity across the
   diagonal, and its intersections with the patch corners must have the same
   value, we choose to divide along whichever diagonal may have equal
   component values. If three vertices of the patch have the same value,
   the patch is divided so that similar edges are in different triangles.
   This takes care of cases where the colour is the same across the whole
   patch (which can happen with a discontinuous function). */
static void patch_split_triangle(SHADINGinfo *sinfo, SHADINGpatch *patch,
                                 SHADINGvertex *v[4])
{
  SYSTEMVALUE x02, y02, x13, y13 ;

  if ( sinfo->nfuncs != 0 && sinfo->ncomps == 1 ) {
    if ( (patch->corner[0][0]->comps[0] == patch->corner[1][1]->comps[0] &&
          patch->corner[0][0]->comps[0] != patch->corner[0][1]->comps[0] &&
          patch->corner[0][0]->comps[0] != patch->corner[1][0]->comps[0]) ||
         (patch->corner[0][0]->comps[0] == patch->corner[0][1]->comps[0] &&
          patch->corner[0][0]->comps[0] == patch->corner[1][0]->comps[0]) ||
         (patch->corner[1][1]->comps[0] == patch->corner[1][0]->comps[0] &&
          patch->corner[1][1]->comps[0] == patch->corner[0][1]->comps[0]) ) {
      /* Diagonal is same, and it is not three-vertex same patch, or it is a
         three vertex patch with discontinuity in each half. */
      v[0] = patch->corner[0][1] ;
      v[1] = patch->corner[0][0] ; /* Common edge 00-11 */
      v[2] = patch->corner[1][1] ;
      v[3] = patch->corner[1][0] ;

      return ;
    }
    if ( (patch->corner[0][1]->comps[0] == patch->corner[1][0]->comps[0] &&
          patch->corner[0][1]->comps[0] != patch->corner[0][0]->comps[0] &&
          patch->corner[0][1]->comps[0] != patch->corner[1][1]->comps[0]) ||
         (patch->corner[0][1]->comps[0] == patch->corner[0][0]->comps[0] &&
          patch->corner[0][1]->comps[0] == patch->corner[1][1]->comps[0]) ||
         (patch->corner[1][0]->comps[0] == patch->corner[0][0]->comps[0] &&
          patch->corner[1][0]->comps[0] == patch->corner[1][1]->comps[0]) ) {
      /* Diagonal is same, and it is not three-vertex same patch, or it is a
         three vertex patch with discontinuity in each half. */
      v[0] = patch->corner[0][0] ;
      v[1] = patch->corner[0][1] ; /* Common edge 01-10 */
      v[2] = patch->corner[1][0] ;
      v[3] = patch->corner[1][1] ;

      return ;
    }
  }

  /* Divide across shortest diagonal */
  x02 = patch->corner[0][0]->x - patch->corner[1][1]->x ;
  y02 = patch->corner[0][0]->y - patch->corner[1][1]->y ;
  x13 = patch->corner[0][1]->x - patch->corner[1][0]->x ;
  y13 = patch->corner[0][1]->y - patch->corner[1][0]->y ;

  if ( x02 * x02 + y02 * y02 < x13 * x13 + y13 * y13 ) {
    v[0] = patch->corner[0][1] ;
    v[1] = patch->corner[0][0] ; /* Common edge 00-11 */
    v[2] = patch->corner[1][1] ;
    v[3] = patch->corner[1][0] ;
  } else {
    v[0] = patch->corner[0][0] ;
    v[1] = patch->corner[0][1] ; /* Common edge 01-10 */
    v[2] = patch->corner[1][0] ;
    v[3] = patch->corner[1][1] ;
  }
}

/** Bottom-level function for patch decomposition; decomposes for color.
   Color decomposition is dealt with at the patch level rather than the
   tensor level. Color decomposition does not need to decompose until the
   patch is linear, since the bottom level triangle decomposition will do
   that. What it does need to do is decompose until the patch is coplanar,
   which will convert the bilinear interpolation to linear interpolation.
   The problem is knowing how close to coplanar is good enough. */
static Bool patch_leaf_color(SHADINGinfo *sinfo, SHADINGpatch *patch)
{
  SYSTEMVALUE minsize = UserParams.MinShadingSize ;
  SYSTEMVALUE xdpi2, ydpi2 ;
  SHADINGvertex *vtx[5], *v[4] ;
  Bool result = FALSE ;
  int32 i, j ;

  HQASSERT(sinfo, "sinfo NULL in patch_leaf_color") ;
  HQASSERT(patch, "patch NULL in patch_leaf_color") ;
  HQASSERT(patch->decomp_type == TENSOR_UV_INVALID,
           "patch_leaf_color called on non-leaf patch ") ;

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  minsize /= 72.0 ; /* Fractions of an inch */
  minsize *= minsize ; /* Work squared to avoid sqrt */

  xdpi2 = sinfo->page->xdpi * sinfo->page->xdpi ;
  ydpi2 = sinfo->page->ydpi * sinfo->page->ydpi ;

  /* Edges between corners are v[i]..v[i + 1] */
  v[0] = patch->corner[0][0] ;
  v[1] = patch->corner[0][1] ;
  v[2] = patch->corner[1][1] ;
  v[3] = patch->corner[1][0] ;

  /* Test if patch is too small to decompose */
  for ( i = 0 ; i < 4 ; ++i ) {
    SYSTEMVALUE dx, dy ;

    j = (i + 1) % 4 ;

    dx = v[i]->x - v[j]->x ;
    dy = v[i]->y - v[j]->y ;

    dx *= dx ; dy *= dy ; /* Square to avoid sqrt */

    /* This test is best expressed as "dx / xdpi2 + dy / ydpi2 > minsize" but
     * it's done this way to avoid FP division and a significant slowdown.
     */
    if ( dx + dy > 1.0 && (dx * ydpi2 + dy * xdpi2 > minsize * xdpi2 * ydpi2) ) {
      result = TRUE ; /* larger than minsize, so try decomposition */
      break ;
    }
  }

  /* Which way would we split? */
  patch_split_triangle(sinfo, patch, v) ;

  /* Note that we don't do bilinear checking for Coons patches which are used
     to construct radial fills. We know these are linear anyway, two sets of
     adjacent corners have the same colour values. */
#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_TENSOR_COLOR) == 0 )
#endif
  if ( result && sinfo->type != 3 ) {
    SYSTEMVALUE weights[3] ;
    SHADINGvertex *bv[2] ;

    /* Allocate vertices for interpolation */
    if ( !vertex_alloc(vtx, 5) )
      return error_handler( VMERROR ) ;

    /* Test true bilinear interpolation of patch centre against linear
       interpolation to centre of common triangle edge. Triangle edges weights
       are adjusted by epsilon to ensure interpolated vertex biases in correct
       direction. */

    /* Bilinear second step interpolants */
    bv[0] = vtx[0] ;
    bv[1] = vtx[1] ;

    /* Bilinear interpolation */
    weights[0] = weights[1] = 0.5 ;

    vertex_interpolate(sinfo, 2, weights, vtx[0], &v[0], sinfo->ncomps) ;
    vertex_interpolate(sinfo, 2, weights, vtx[1], &v[2], sinfo->ncomps) ;
    vertex_interpolate(sinfo, 2, weights, vtx[2], bv, sinfo->ncomps) ;

    /* Triangle v[0], v[1], v[2] */

    /* Linear interpolation */
    weights[1] = weights[2] = 0.5 - EPSILON ;
    weights[0] = 1 - weights[1] - weights[2] ;

    vertex_interpolate(sinfo, 3, weights, vtx[0], &v[0], sinfo->ncomps) ;

    /* Triangle v[1], v[2], v[3] */

    /* Linear interpolation */
    weights[0] = weights[1] = 0.5 - EPSILON ;
    weights[2] = 1 - weights[0] - weights[1] ;

    vertex_interpolate(sinfo, 3, weights, vtx[1], &v[1], sinfo->ncomps) ;

    if ( !vertex_color(vtx[0], sinfo, TRUE) || /* Get dlcs for vertices */
         !vertex_color(vtx[1], sinfo, TRUE) ||
         !vertex_color(vtx[2], sinfo, TRUE) )
      return FALSE ;

    if ( !shading_color_close_enough(sinfo, &vtx[0]->dlc_probe, &vtx[2]->dlc_probe,
                                     gsc_getRS(gstateptr->colorInfo)) ||
         !shading_color_close_enough(sinfo, &vtx[1]->dlc_probe, &vtx[2]->dlc_probe,
                                     gsc_getRS(gstateptr->colorInfo)) ) {
      SHADINGpatch p1, p2, p3, p4 ;
      SYSTEMVALUE uw = 0.5, vw = 0.5 ;
      USERVALUE discont = 0.0f ; /* Silence compiler */
      Bool is_discont = FALSE ;

      if ( sinfo->nfuncs != 0 && sinfo->ncomps == 1 ) {
        /* Test curvature of line across diagonal */
        if ( patch->corner[0][0]->comps[0] == patch->corner[1][1]->comps[0] ) {
          SYSTEMVALUE w = patch_discontinuity_weight(patch->corner[0][0],
                                                     patch->corner[0][1],
                                                     patch->corner[1][0]
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
                                                     , sinfo
#endif
                                                     ) ;
          if ( w > 0.0 && w < 1.0 ) {
            vw = 1 - w ; /* V weight[0] = 1 - w, V weight[1] = w */
            uw = w ; /* U weight[0] = w, U weight[1] = 1 - w */
            is_discont = TRUE ;
            discont = patch->corner[0][0]->comps[0] ;
          }
        } else if ( patch->corner[0][1]->comps[0] == patch->corner[1][0]->comps[0] ) {
          SYSTEMVALUE w = patch_discontinuity_weight(patch->corner[0][1],
                                                     patch->corner[0][0],
                                                     patch->corner[1][1]
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
                                                     , sinfo
#endif
                                                     ) ;
          if ( w > 0.0 && w < 1.0 ) {
            vw = w ; /* V weight[0] = w, V weight[1] = 1 - w */
            uw = w ; /* U weight[0] = w, U weight[1] = 1 - w */
            is_discont = TRUE ;
            discont = patch->corner[0][1]->comps[0] ;
          }
        }
      }

      weights[0] = vw ;
      weights[1] = 1 - vw ;

      bv[0] = patch->corner[0][0] ;
      bv[1] = patch->corner[0][1] ;
      vertex_interpolate(sinfo, 2, weights, vtx[0], bv, sinfo->ncomps) ;
      bv[0] = patch->corner[1][0] ;
      bv[1] = patch->corner[1][1] ;
      vertex_interpolate(sinfo, 2, weights, vtx[1], bv, sinfo->ncomps) ;

      weights[0] = uw ;
      weights[1] = 1 - uw ;

      bv[0] = patch->corner[0][0] ;
      bv[1] = patch->corner[1][0] ;
      vertex_interpolate(sinfo, 2, weights, vtx[2], bv, sinfo->ncomps) ;
      bv[0] = patch->corner[0][1] ;
      bv[1] = patch->corner[1][1] ;
      vertex_interpolate(sinfo, 2, weights, vtx[3], bv, sinfo->ncomps) ;
      bv[0] = vtx[0] ; /* True bilinear join */
      bv[1] = vtx[1] ;
      vertex_interpolate(sinfo, 2, weights, vtx[4], bv, sinfo->ncomps) ;

      p1.corner[0][0] = patch->corner[0][0] ;
      p1.corner[0][1] = vtx[0] ;
      p1.corner[1][0] = vtx[2] ;
      p1.corner[1][1] = vtx[4] ;
      p1.decomp_type = TENSOR_UV_INVALID ;
      p1.children = NULL ;

      p2.corner[0][0] = vtx[2] ;
      p2.corner[0][1] = vtx[4] ;
      p2.corner[1][0] = patch->corner[1][0] ;
      p2.corner[1][1] = vtx[1] ;
      p2.decomp_type = TENSOR_UV_INVALID ;
      p2.children = NULL ;

      p3.corner[0][0] = vtx[0] ;
      p3.corner[0][1] = patch->corner[0][1] ;
      p3.corner[1][0] = vtx[4] ;
      p3.corner[1][1] = vtx[3] ;
      p3.decomp_type = TENSOR_UV_INVALID ;
      p3.children = NULL ;

      p4.corner[0][0] = vtx[4] ;
      p4.corner[0][1] = vtx[3] ;
      p4.corner[1][0] = vtx[1] ;
      p4.corner[1][1] = patch->corner[1][1] ;
      p4.decomp_type = TENSOR_UV_INVALID ;
      p4.children = NULL ;

      if ( is_discont ) { /* Make sure that interpolation is exact */
        SYSTEMVALUE ex, ey ;

        HQASSERT(discont - EPSILON < vtx[4]->comps[0] &&
                 discont + EPSILON > vtx[4]->comps[0],
                 "Bilinear interpolation to discontinuity drifted too far") ;
        vtx[4]->comps[0] = discont ;

        /* If error between discontinuity and centre of patch is under flatness
           coefficient, we don't need to decompose further */
        ex = (patch->corner[0][0]->x + patch->corner[0][1]->x +
              patch->corner[1][0]->x + patch->corner[1][1]->x) / 4.0 - vtx[4]->x ;
        ey = (patch->corner[0][0]->y + patch->corner[0][1]->y +
              patch->corner[1][0]->y + patch->corner[1][1]->y) / 4.0 - vtx[4]->y ;

        if ( ex * ex + ey * ey <= fl_getftol() )
          result = FALSE ;
      }

      if ( result ) {
        METRIC_RECURSE_BEGIN(c, 4) ;
        result = (patch_leaf_color(sinfo, &p1) &&
                  patch_leaf_color(sinfo, &p2) &&
                  patch_leaf_color(sinfo, &p3) &&
                  patch_leaf_color(sinfo, &p4)) ;
        METRIC_RECURSE_END() ;

        vertex_free(sinfo, vtx, 5) ;

        return result ;
      }
    }
    vertex_free(sinfo, vtx, 5) ;
  }

  /* Patch is close enough to linear to approximate by two gouraud triangles.
     Radial gradients use Coons patches with two of the corner points at the
     same spot. If we are in such a case, one of the triangles will be line
     degenerate, so we can omit it. */
  if ( v[0]->x != v[2]->x || v[0]->y != v[2]->y ) {
    if ( !DEVICE_GOURAUD(sinfo->page, v[0], v[1], v[2], sinfo) )
      return FALSE ;
  }

  if ( v[1]->x != v[3]->x || v[1]->y != v[3]->y ) {
    if ( !DEVICE_GOURAUD(sinfo->page, v[1], v[2], v[3], sinfo) )
      return FALSE ;
  }

  return TRUE ;
}

/** Draw patch, by decomposing for discontinuities and color, then
   approximating with two gouraud triangles */
static Bool patch_draw_leaf(SHADINGinfo *sinfo, SHADINGpatch *patch)
{
  /* Coons patches generated by radial fills are never self-intersecting,
     and don't have function discontinuities, since those are decomposed at
     the radial fill level. */
  if ( sinfo->type != 3 ) {
    SHADINGvertex *vtx[4] ;
    Bool result ;
    int32 same ;

    HQASSERT(sinfo, "sinfo NULL in patch_draw_leaf") ;
    HQASSERT(patch, "patch NULL in patch_draw_leaf") ;
    HQASSERT(patch->decomp_type == TENSOR_UV_INVALID,
             "patch_draw_leaf called on non-leaf patch ") ;

    /* Split self-intersecting patch in V direction */
    if ( !patch_self_intersect(sinfo, patch->corner[0][0], patch->corner[0][1],
                               patch->corner[1][0], patch->corner[1][1],
                               vtx, &same) )
      return FALSE ;

    if ( vtx[0] != NULL ) {
      SHADINGpatch p1, p2, p3 ;

      p1.corner[0][0] = patch->corner[0][0] ;
      p1.corner[0][1] = vtx[0] ;
      p1.corner[1][0] = patch->corner[1][0] ;
      p1.corner[1][1] = vtx[1] ;
      p1.decomp_type = TENSOR_UV_INVALID ;
      p1.children = NULL ;

      p2.corner[0][0] = vtx[0] ;
      p2.corner[0][1] = vtx[2] ;
      p2.corner[1][0] = vtx[1] ;
      p2.corner[1][1] = vtx[3] ;
      p2.decomp_type = TENSOR_UV_INVALID ;
      p2.children = NULL ;

      p3.corner[0][0] = vtx[2] ;
      p3.corner[0][1] = patch->corner[0][1] ;
      p3.corner[1][0] = vtx[3] ;
      p3.corner[1][1] = patch->corner[1][1] ;
      p3.decomp_type = TENSOR_UV_INVALID ;
      p3.children = NULL ;

      result = (patch_draw_leaf(sinfo, &p1) &&
                (same || patch_draw_leaf(sinfo, &p2)) &&
                patch_draw_leaf(sinfo, &p3)) ;

      vertex_free(sinfo, vtx, 4) ;

      return result ;
    }

#if defined( DEBUG_BUILD )
    if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) == 0 )
#endif
      if ( sinfo->nfuncs != 0 ) {
        /* Discontinuity division; divide discontinuities in V direction first,
           then U direction. These can't be done as nested routines, because
           dividing in U may reveal new V discontinuities. */

        /* Discontinuity in V? */
        if ( !patch_discontinuity_divide(sinfo, patch->corner[0][0],
                                         patch->corner[0][1], patch->corner[1][0],
                                         patch->corner[1][1], vtx) )
          return FALSE ;

        if ( vtx[0] != NULL ) {
          SHADINGpatch p1, p2 ;

          p1.corner[0][0] = patch->corner[0][0] ;
          p1.corner[0][1] = vtx[0] ;
          p1.corner[1][0] = patch->corner[1][0] ;
          p1.corner[1][1] = vtx[1] ;
          p1.decomp_type = TENSOR_UV_INVALID ;
          p1.children = NULL ;

          p2.corner[0][0] = vtx[0] ;
          p2.corner[0][1] = patch->corner[0][1] ;
          p2.corner[1][0] = vtx[1] ;
          p2.corner[1][1] = patch->corner[1][1] ;
          p2.decomp_type = TENSOR_UV_INVALID ;
          p2.children = NULL ;

          METRIC_RECURSE_BEGIN(f, 2) ;
          result = (patch_draw_leaf(sinfo, &p1) && patch_draw_leaf(sinfo, &p2)) ;
          METRIC_RECURSE_END() ;
          vertex_free(sinfo, vtx, 2) ;

          return result ;
        }
      }

    /* Split self-intersecting patch in U direction */
    if ( !patch_self_intersect(sinfo, patch->corner[0][0], patch->corner[1][0],
                               patch->corner[0][1], patch->corner[1][1],
                               vtx, &same) )
      return FALSE ;

    if ( vtx[0] != NULL ) {
      SHADINGpatch p1, p2, p3 ;

      p1.corner[0][0] = patch->corner[0][0] ;
      p1.corner[0][1] = patch->corner[0][1] ;
      p1.corner[1][0] = vtx[0] ;
      p1.corner[1][1] = vtx[1] ;
      p1.decomp_type = TENSOR_UV_INVALID ;
      p1.children = NULL ;

      p2.corner[0][0] = vtx[0] ;
      p2.corner[0][1] = vtx[1] ;
      p2.corner[1][0] = vtx[2] ;
      p2.corner[1][1] = vtx[3] ;
      p2.decomp_type = TENSOR_UV_INVALID ;
      p2.children = NULL ;

      p3.corner[0][0] = vtx[2] ;
      p3.corner[0][1] = vtx[3] ;
      p3.corner[1][0] = patch->corner[1][0] ;
      p3.corner[1][1] = patch->corner[1][1] ;
      p3.decomp_type = TENSOR_UV_INVALID ;
      p3.children = NULL ;

      result = (patch_draw_leaf(sinfo, &p1) &&
                (same || patch_draw_leaf(sinfo, &p2)) &&
                patch_draw_leaf(sinfo, &p3)) ;

      vertex_free(sinfo, vtx, 4) ;

      return result ;
    }

#if defined( DEBUG_BUILD )
    if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) == 0 )
#endif
      if ( sinfo->nfuncs != 0 ) {
        /* Discontinuity in U? */
        if ( !patch_discontinuity_divide(sinfo, patch->corner[0][0],
                                         patch->corner[1][0], patch->corner[0][1],
                                         patch->corner[1][1], vtx) )
          return FALSE ;

        if ( vtx[0] != NULL ) {
          SHADINGpatch p1, p2 ;

          p1.corner[0][0] = patch->corner[0][0] ;
          p1.corner[0][1] = patch->corner[0][1] ;
          p1.corner[1][0] = vtx[0] ;
          p1.corner[1][1] = vtx[1] ;
          p1.decomp_type = TENSOR_UV_INVALID ;
          p1.children = NULL ;

          p2.corner[0][0] = vtx[0] ;
          p2.corner[0][1] = vtx[1] ;
          p2.corner[1][0] = patch->corner[1][0] ;
          p2.corner[1][1] = patch->corner[1][1] ;
          p2.decomp_type = TENSOR_UV_INVALID ;
          p2.children = NULL ;

          METRIC_RECURSE_BEGIN(f, 2) ;
          result = (patch_draw_leaf(sinfo, &p1) && patch_draw_leaf(sinfo, &p2)) ;
          METRIC_RECURSE_END() ;

          vertex_free(sinfo, vtx, 2) ;

          return result ;
        }
      }
  }

  /* No more discontinuities in U or V direction. Decompose in color. */
  return patch_leaf_color(sinfo, patch) ;
}

static int32 patch_count_leaves(SHADINGpatch *patch)
{
  if (patch->decomp_type == TENSOR_UV_INVALID) {
    return 1;
  } else {
    return patch_count_leaves( & patch->children[0] ) +
      patch_count_leaves( & patch->children[1] ) ;
  }
}

static void patch_collect_leaves(SHADINGpatch *patch, SHADINGpatch ***list)
{
  HQASSERT(patch, "patch NULL in patch_collect_leaves") ;
  HQASSERT(list, "list NULL in patch_collect_leaves") ;
  HQASSERT(*list, "*list NULL in patch_collect_leaves") ;

  if (patch->decomp_type == TENSOR_UV_INVALID) {
    *(*list)++ = patch;
  } else {
    patch_collect_leaves( & patch->children[0] , list) ;
    patch_collect_leaves( & patch->children[1] , list) ;
  }
}

static int CRT_API patch_compare(const void *a, const void *b)
{
  SHADINGpatch *pa = (*(SHADINGpatch **) a) ;
  SHADINGpatch *pb = (*(SHADINGpatch **) b) ;
  int32 diff ;

  diff = pa->spos[TENSOR_V] - pb->spos[TENSOR_V] ;
  if ( diff == 0 ) {
    /** \todo @@@ TODO FIXME NYI
       If V is the same, we need a special test before comparing U; even
       though the V values are the same, these are decomposition values for
       the whole patch. If the patches are not completely coincident, the
       real V values will differ at every point, and thus they should be
       sorted by V. This can be done by intersecting the U edge vectors and
       finding the equivalent V values at these points. If the U vectors are
       parallel, then the patches are sorted by U. The file
       HOMEangus_pstests!shading:tensoruv.ps contains examples that will
       render incorrectly without this test. */
    diff = pa->spos[TENSOR_U] - pb->spos[TENSOR_U] ;
    HQASSERT(diff != 0, "Two patches with same V and U") ;
  }

  return diff ;
}

Bool tensor_draw( SHADINGtensor *tensor )
{
  SHADINGinfo *sinfo ;
  int32 num_patches = 0, i ;
  SHADINGpatch **patch_array = NULL , **tlist ;
  Bool result = FALSE ;

  HQASSERT(tensor, "tensor NULL") ;
  sinfo = tensor->sinfo ;
  HQASSERT(sinfo, "sinfo NULL") ;
  HQASSERT(tensor->patch->decomp_type == TENSOR_UV_INVALID,
           "tensor already divided") ;

  fl_setflat(theFlatness(theILineStyle(gstateptr))) ;

  if ( !tensor_decompose_edges( tensor, EDGE_FLAG_ALL ) )
    goto tensor_draw_exit ;

  /* The Bezier edge decomposition in tensor_decompose_edges has only ensured
     that patches will line up with each other. This fixes up internal
     decomposition edges so they line up properly. */
#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_PATCH_STRAYS) == 0 )
#endif
    patch_fix_strays( tensor->patch ) ;

  num_patches = patch_count_leaves( tensor->patch ) ;
  patch_array = mm_alloc( mm_pool_shading, num_patches * sizeof(SHADINGpatch * ),
      MM_ALLOC_CLASS_SHADING ) ;
  if ( patch_array == NULL ) {
    result = error_handler( VMERROR  ) ;
    goto tensor_draw_exit ;
  }
  tlist = patch_array ;

  patch_collect_leaves( tensor->patch, &tlist ) ;
  qsort( patch_array, num_patches, sizeof(SHADINGpatch *), patch_compare ) ;

  for ( i = 0; i < num_patches ; i ++ ) {
    SHADINGpatch *patch = patch_array[i] ;
    if (!patch_draw_leaf( sinfo, patch ) )
      goto tensor_draw_exit ;

#if defined( DEBUG_BUILD )
    if ( (shading_debug_flag & SHADING_DEBUG_OUTLINE_PATCH) != 0 ) {
      ps_context_t *pscontext = get_core_context_interp()->pscontext ;
      STROKE_PARAMS params ;
      SYSTEMVALUE c[8] ;
      int32 (*currentaddtodl)(DL_STATE *page, LISTOBJECT *lobj)
        = device_current_addtodl ;

      static USERVALUE red[3] = {1.0f, 0.0f, 0.0f } ;
      static OMATRIX idm = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0, MATRIX_OPT_0011} ;

      c[0] = patch->corner[0][0]->x ;
      c[1] = patch->corner[0][0]->y ;
      c[2] = patch->corner[0][1]->x ;
      c[3] = patch->corner[0][1]->y ;
      c[4] = patch->corner[1][1]->x ;
      c[5] = patch->corner[1][1]->y ;
      c[6] = patch->corner[1][0]->x ;
      c[7] = patch->corner[1][0]->y ;

      if ( !gsave_(pscontext) )
        goto tensor_draw_exit ;

      theLineWidth(theLineStyle(*gstateptr)) = 0.0f ;

      if ( !gsc_setcolorspacedirect(gstateptr->colorInfo, GSC_FILL, SPACE_DeviceRGB) ||
           !gsc_setcolordirect(gstateptr->colorInfo, GSC_FILL, red) ||
           !gs_newpath() ||
           !gs_setctm(&idm, FALSE) ||
           !gs_moveto(TRUE, &c[0], &(thePathInfo(*gstateptr))) ||
           !gs_lineto(TRUE, TRUE, &c[2], &(thePathInfo(*gstateptr))) ||
           !gs_lineto(TRUE, TRUE, &c[4], &(thePathInfo(*gstateptr))) ||
           !gs_lineto(TRUE, TRUE, &c[6], &(thePathInfo(*gstateptr))) ||
           !path_close(CLOSEPATH, &thePathInfo(*gstateptr)) ) {
        (void)grestore_(pscontext) ;
        goto tensor_draw_exit ;
      }

      set_gstate_stroke(&params, &thePathInfo(*gstateptr), NULL, FALSE) ;
      if ( !dostroke(&params, GSC_FILL, STROKE_NORMAL) ||
           !flush_vignette(VD_Default) ) {
        (void)grestore_(pscontext) ;
        goto tensor_draw_exit ;
      }

      if ( !grestore_(pscontext) )
        goto tensor_draw_exit ;

      device_current_addtodl = currentaddtodl ;
    }
#endif
  }

  result = TRUE ;

tensor_draw_exit:
  if ( patch_array ) {
    mm_free( mm_pool_shading, patch_array,  num_patches * sizeof(SHADINGpatch * ) ) ;
  }
  destroy_patch_children( tensor->sinfo, tensor->patch );

  return result ;
}

#define SET_INTERNAL_COORD(coords, uc, un, uf, vc, vn, vf, d) MACRO_START \
    coords[uc][vc][d] = \
      (6 * (coords[uc][vn][d] + coords[un][vc][d]) \
     + 3 * (coords[uc][vf][d] + coords[uf][vc][d]) \
     - 2 * (coords[un][vf][d] + coords[uf][vn][d]) \
     - 4 * coords[un][vn][d] - coords[uf][vf][d]) / 9.0 ; \
  MACRO_END

void tensor_internal_coords( SHADINGtensor *tensor )
{
  int i;

  for (i = 0; i < 2; i++) {
    SET_INTERNAL_COORD(tensor->coord, 1, 0, 3, 1, 0, 3, i) ;
    SET_INTERNAL_COORD(tensor->coord, 1, 0, 3, 2, 3 ,0, i) ;
    SET_INTERNAL_COORD(tensor->coord, 2, 3, 0, 1, 0, 3, i) ;
    SET_INTERNAL_COORD(tensor->coord, 2, 3, 0, 2, 3, 0, i) ;
  }
}

static int32 corner_u_map[4] = { 0, 0, 1, 1 };
static int32 corner_v_map[4] = { 0, 1, 1, 0 };

static int32 tensor_coord_u_map[16] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 2, 1, 1, 1, 2, 2};
static int32 tensor_coord_v_map[16] = {0, 1, 2, 3, 3, 3, 3, 2, 1, 0, 0, 0, 1, 2, 2, 1};

/** Tensor patch mesh. */
Bool tensor_mesh(SHADINGinfo *sinfo, SHADINGsource *sdata, int32 type)
{
  SHADINGtensor prev_tensor = {0} ;
  SHADINGpatch prev_patch = {0} ;
  SHADINGvertex *vtx[4] ;
  Bool result = FALSE ;
  Bool previous = FALSE ;

  HQASSERT( type == 6 || type == 7, "tensor_mesh called on wrong type of shaded fill") ;

  if ( !vertex_alloc(vtx, 4) )
    return error_handler( VMERROR ) ;

  while ( more_mesh_data(sdata) ) {
    SHADINGtensor tensor ;
    SHADINGpatch patch ;
    int32 i, j, flag, vertex_start, vertex_stop, color_start ;

    tensor.sinfo = sinfo ;
    tensor.patch = &patch ;
    tensor.pos[TENSOR_U] = tensor.pos[TENSOR_V] = 1;
    tensor.neighbors[0] = tensor.neighbors[1] = NULL ;

    patch.spos[TENSOR_U] = patch.spos[TENSOR_V] = SPOS_OF_ONE ; /* =find_spos(1) */
    HQASSERT(patch.spos[TENSOR_U] < 0 && (patch.spos[TENSOR_U] << 1) == 0,
             "SPOS_OF_ONE is not just top (sign) bit") ;
    patch.decomp_type = TENSOR_UV_INVALID;
    patch.children = NULL ;

    if ( !get_vertex_flag( sdata, &flag )) {
      goto tensor_mesh_exit ;
    }

    if ( flag != 0 && !previous ) {
      result = error_handler( UNDEFINED ) ;
      goto tensor_mesh_exit ;
    }

    switch ( flag ) {
    case 0:
      patch.corner[0][0] = vtx[0] ; /* read all patch corners */
      patch.corner[0][1] = vtx[1] ;
      patch.corner[1][0] = vtx[2] ;
      patch.corner[1][1] = vtx[3] ;
      vertex_start = 0; color_start = 0 ;
      break ;
    case 1:
      patch.corner[0][0] = prev_patch.corner[0][1] ;
      patch.corner[0][1] = prev_patch.corner[1][1] ;
      patch.corner[1][0] = prev_patch.corner[1][0] ; /* Will be overwritten */
      patch.corner[1][1] = prev_patch.corner[0][0] ;
      for (i = 0; i < 4; i++) {
        tensor.coord[0][i][0] = prev_tensor.coord[i][3][0] ;
        tensor.coord[0][i][1] = prev_tensor.coord[i][3][1] ;
      }
      vertex_start = 4; color_start = 2;
      break;
    case 2:
      patch.corner[0][0] = prev_patch.corner[1][1];
      patch.corner[0][1] = prev_patch.corner[1][0];
      patch.corner[1][0] = prev_patch.corner[0][0] ; /* Will be overwritten */
      patch.corner[1][1] = prev_patch.corner[0][1] ;
      for (i = 0; i < 4; i++) {
        tensor.coord[0][i][0] = prev_tensor.coord[3][3 - i][0];
        tensor.coord[0][i][1] = prev_tensor.coord[3][3 - i][1];
      }
      vertex_start = 4; color_start = 2;
      break;
    case 3:
      patch.corner[0][0] = prev_patch.corner[1][0];
      patch.corner[0][1] = prev_patch.corner[0][0];
      patch.corner[1][0] = prev_patch.corner[0][1] ; /* Will be overwritten */
      patch.corner[1][1] = prev_patch.corner[1][1] ;
      for (i = 3; i >= 0; --i) { /* Use 0,0 value before overwriting */
        tensor.coord[0][i][0] = prev_tensor.coord[3 - i][0][0];
        tensor.coord[0][i][1] = prev_tensor.coord[3 - i][0][1];
      }
      vertex_start = 4; color_start = 2;
      break;
    default:
      result = error_handler(UNDEFINED);
      goto tensor_mesh_exit ;
    }

    vertex_stop = (type == 6) ? 12 : 16 ;

    for ( i = vertex_start; i < vertex_stop ; i++ ) {
      if ( !get_vertex_coords( sdata,
        &tensor.coord[tensor_coord_u_map[i]][tensor_coord_v_map[i]][0],
        &tensor.coord[tensor_coord_u_map[i]][tensor_coord_v_map[i]][1] )) {
        goto tensor_mesh_exit ;
      }
    }

    for ( i = 0; i < 2; i++ ) {
      for ( j = 0; j < 2; j++ ) {
        patch.corner[i][j]->x = tensor.coord[i * 3][j * 3][0] ;
        patch.corner[i][j]->y = tensor.coord[i * 3][j * 3][1] ;
      }
    }
    for ( i = color_start; i < 4; i++ ) {
      if ( !get_vertex_color( sdata,
        patch.corner[corner_u_map[i]][corner_v_map[i]] )) {
        goto tensor_mesh_exit ;
      }
    }

    if ( type == 6 ) {
      tensor_internal_coords( & tensor ) ;
    }

    METRIC_INCREMENT(pdl) ;

    if ( sinfo->preseparated ) {
      if ( ! rcbs_store_tensor( &tensor, sinfo ) )
        goto tensor_mesh_exit ;
    } else if ( ! tensor_draw( & tensor ) ) {
      goto tensor_mesh_exit ;
    }

    previous = TRUE ;
    prev_patch = patch ;
    prev_tensor = tensor ;
  }

  result = TRUE ;

tensor_mesh_exit:
  vertex_free(sinfo, vtx, 4) ;

  return result ;
}

void init_C_globals_tensor(void)
{
#ifdef METRICS_BUILD
  tensor_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&tensor_metrics_hook) ;
#endif
}

/* Log stripped */
