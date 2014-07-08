/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!src:gouraud.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Front end decomposition functions for gouraud triangles.
 */

#include "core.h"
#include "swerrors.h" /* VMERROR */
#include "swoften.h"
#include "mm.h"
#include "monitor.h"
#include "namedef_.h" /* NAME_* */
#include "debugging.h"

#include "often.h"
#include "graphics.h"
#include "gstate.h"
#include "gschead.h"  /* gsc_setcolordirect */
#include "functns.h"  /* FN_SHADING etc */
#include "gu_path.h"  /* fill_three, i3cpath */
#include "routedev.h" /* DEVICE_* */
#include "dl_color.h" /* p_ncolor_t etc */
#include "idlom.h"    /* IDLOM_* */
#include "control.h"  /* interrupts_clear */
#include "sobol.h"    /* sobol */
#include "rcbshfil.h" /* rcbs_fn_find_discontinuity */
#include "params.h" /* UserParams */
#include "interrupts.h"

#include "gouraud.h"
#include "shadecev.h" /* vertex_* */
#include "shading.h"  /* SHADING_DEBUG_* */
#include "dl_shade.h"

#define VERTEX_FUNCTION_BIAS(_page, _v, _c) MACRO_START \
  SHADINGvertex *_v_ = (_v) ;                           \
  uint8 _up_ = (uint8)((_c) >= _v_->comps[0]) ;         \
  if ( _v_->upwards != _up_ ) {                         \
    if ( _v_->converted )                               \
      dlc_release(page->dlc_context, &_v_->dlc) ;       \
    if ( _v_->probeconverted )                          \
      dlc_release(page->dlc_context, &_v_->dlc_probe) ; \
    _v_->upwards = _up_ ;                               \
    _v_->converted = FALSE ;                            \
    _v_->probeconverted = FALSE ;                       \
  }                                                     \
MACRO_END

/** Args for tracking flag and memory usage. */
typedef struct {
  int32 remaining ;     /**< How much promise is left? */
  uintptr_t *flagptr ;  /**< pointer to current flag word. */
  int32 bits ;          /**< bits left in current flag word. */
  int32 coercion ;      /**< Which HDLT coercion has succeeded? */
  dl_color_t dlc_gobj ; /**< Merged colour for gouraud listobject. */
  int32 depth ;         /**< Decomposition depth. */
#if defined( ASSERT_BUILD )
  p_ncolor_t *nextcolor ; /**< For checking promise allocations. */
#endif
#if defined( DEBUG_BUILD )
  int32 linear_triangles ; /**< Number of triangles decomposed into. */
#endif
} GOURAUD_DL_ARGS ;

static Bool gouraud_triangle_dl(SHADINGvertex *v0, SHADINGvertex *v1,
                                SHADINGvertex *v2, SHADINGinfo *sinfo,
                                GOURAUD_DL_ARGS *gargs) ;
static Bool gouraud_triangle_coerce(SHADINGvertex *v1, SHADINGvertex *v2,
                                    SHADINGvertex *v3, SHADINGinfo *sinfo,
                                    GOURAUD_DL_ARGS *gargs) ;

/** Primitive operation to put a quadrilateral on display list, decomposed into
   individual gouraud-shaded triangles. Quadrilateral is remainder after
   discontinuous triangle has been removed from a triangle patch which uses
   non-continuous function. The edge v3..v0 is the edge with the discontinuity
   which has been divided. */
static Bool decompose_quad(SHADINGvertex *v0, SHADINGvertex *v1,
                            SHADINGvertex *v2, SHADINGvertex *v3,
                            int32 cindex, SHADINGinfo *sinfo)
{
  SYSTEMVALUE x02, y02, x13, y13 ;
  USERVALUE bounds[2] ;
  SHADINGvertex *v[4] ;
  int32 order, index, vindex ;
  Bool result ;

  v[0] = v0 ;
  v[1] = v1 ;
  v[2] = v2 ;
  v[3] = v3 ;

  HQASSERT(cindex >= 0 && cindex < sinfo->ncomps,
           "Component index out of range in decompose_quad") ;
  HQASSERT(v0->comps[cindex] == v3->comps[cindex],
           "Quadrilateral not divided on discontinuity in decompose_quad") ;
  HQASSERT((v1->comps[cindex] - v0->comps[cindex]) *
           (v2->comps[cindex] - v3->comps[cindex]) > 0,
           "Quadrilateral discontinuity not feasible in decompose_quad") ;

  /* Discontinuities across the quadrilateral in this channel cannot intersect
     the edge v3..v0. Test for discontinuities in edge 0..1, 1..2, and 2..3.
     If we find a discontinuity, we find the other intersection point and
     decompose into a triangle and a quadrilateral, two triangles, two
     quadrilaterals or a triangle and two quadrilaterals. */

  for ( vindex = 0 ; vindex < 2 ; ++vindex ) {
    SHADINGvertex *vtmp[3] ;
    USERVALUE c0, c1, discont ;

    /* Mirror vertices. This is done before the test below so we can use
       continue if the bounds are equal. The same amount of processing is
       done if there are no discontinuities, since it would have been done
       at the end of the loop anyway. */
    vtmp[0] = v[0] ; v[0] = v[3] ; v[3] = vtmp[0] ;
    vtmp[0] = v[1] ; v[1] = v[2] ; v[2] = vtmp[0] ;

    c0 = v[0]->comps[cindex] ;
    c1 = v[1]->comps[cindex] ;

    /* It doesn't matter which way round we look, the discontinuity should
       still be there. So sort the bounds. */
    if ( c0 < c1 ) {
      bounds[0] = c0 ;
      bounds[1] = c1 ;
    } else if ( c0 > c1 ) {
      bounds[0] = c1 ;
      bounds[1] = c0 ;
    } else /* If bounds are same, there can't be a discontinuity */
      continue ;

    for ( index = 0 ; index < sinfo->nfuncs ; ++index ) {
      SYSTEMVALUE weights[2] ;

      if ( !(sinfo->funcs ?
             fn_find_discontinuity(&sinfo->funcs[index], cindex, bounds,
                                   &discont, &order,
                                   FN_SHADING, index,
                                   sinfo->base_fuid, FN_GEN_NA,
                                   &sinfo->fndecode) :
             rcbs_fn_find_discontinuity(sinfo->rfuncs[index], cindex, bounds,
                                        &discont, &order)) )
        return FALSE ;

      if ( order != -1 ) {
        USERVALUE c2 = v[2]->comps[cindex] ;

        HQASSERT(discont > bounds[0] && discont < bounds[1],
                 "Discontinuity not contained in range") ;

        /* Found discontinuity in this edge. It must intersect another edge
           or vertex, so find out which it is and sub-divide along that
           line. */
        weights[1] = (discont - c0) / (c1 - c0) ;
        weights[0] = 1 - weights[1] ;

        if ( !vertex_alloc(vtmp, 3) )
          return error_handler(VMERROR) ;

        /* Always a new vertex at discontinuity */
        vertex_interpolate(sinfo, 2, weights, vtmp[0], &v[0], sinfo->ncomps) ;

        /* Fix up rounding errors in discontinuity interpolation by
           performing relative error check for sanity, then forcing the
           colour value to the discontinuity. Rounding errors in position and
           other colour components may cause minor drift, but there's not
           much we can do about that. */
        HQASSERT(fabs((vtmp[0]->comps[cindex] - discont) / (c1 - c0)) < 1e-6,
                 "Interpolation to discontinuity failed") ;
        vtmp[0]->comps[cindex] = discont ;

        /* Find other vertex where discontinuity intersects. Only one of
           these weights will be used, depending on whether the
           intersection is edge 1..2 or edge 2..3. */
        if ( c1 != c2 )
          weights[0] = (c2 - discont) / (c2 - c1) ;
        else
          weights[0] = 0 ; /* Fails inclusion test */
        if ( c2 != c0 ) /* Really c3, but c3 == c0 */
          weights[1] = (discont - c2) / (c0 - c2) ;
        else
          weights[1] = 0 ; /* Fails inclusion test */

        if ( weights[0] > 0.0 && weights[0] < 1.0 ) { /* 1..2 */
          weights[1] = 1 - weights[0] ;

          /* Discontinuity intersection on edge 1..2 */
          vertex_interpolate(sinfo, 2, weights, vtmp[1], &v[1], sinfo->ncomps) ;

          /* Fix up rounding errors in discontinuity interpolation by
             performing relative error check for sanity, then forcing the
             colour value to the discontinuity. Rounding errors in position and
             other colour components may cause minor drift, but there's not
             much we can do about that. */
          HQASSERT(fabs((vtmp[1]->comps[cindex] - discont) / (c1 - c0)) < 1e-6,
                   "Interpolation to discontinuity failed") ;
          vtmp[1]->comps[cindex] = discont ;

          /* Intersection of c2 on edge 0..1, to split remainder into one
             triangle and two quadrilaterals. */
          weights[1] = (c2 - c0) / (c1 - c0) ;
          weights[0] = 1 - weights[1] ;

          vertex_interpolate(sinfo, 2, weights, vtmp[2], &v[0], sinfo->ncomps) ;

          /* Fix up rounding errors in discontinuity interpolation by
             performing relative error check for sanity, then forcing the
             colour value to the discontinuity. Rounding errors in position and
             other colour components may cause minor drift, but there's not
             much we can do about that. */
          HQASSERT(fabs((vtmp[2]->comps[cindex] - c2) / (c1 - c0)) < 1e-6,
                   "Interpolation to vertex failed") ;
          vtmp[2]->comps[cindex] = c2 ;

#if DEBUG_SHOW_DISCONTINUITY
          monitorf((uint8 *)"Quad discontinuity in color %d edge (%f,%f,%f)..(%f,%f,%f) at %f split along (%f,%f,%f)..(%f,%f,%f)\n",
                   cindex,
                   v[0]->x, v[0]->y, c0,
                   v[1]->x, v[1]->y, c1,
                   discont,
                   vtmp[0]->x, vtmp[0]->y, vtmp[0]->comps[cindex],
                   vtmp[1]->x, vtmp[1]->y, vtmp[1]->comps[cindex]) ;
#endif

          result = (decompose_triangle(vtmp[0], v[1], vtmp[1], sinfo) &&
                    decompose_quad(v[0], vtmp[2], v[2], v[3], cindex, sinfo) &&
                    decompose_quad(vtmp[1], v[2], vtmp[2], vtmp[0], cindex, sinfo)) ;
        } else if ( weights[1] > 0.0 && weights[1] < 1.0 ) { /* 2..3 */
          weights[0] = 1 - weights[1] ;

          /* Discontinuity intersection on edge 2..3 */
          vertex_interpolate(sinfo, 2, weights, vtmp[1], &v[2], sinfo->ncomps) ;

          /* Fix up rounding errors in discontinuity interpolation by
             performing relative error check for sanity, then forcing the
             colour value to the discontinuity. Rounding errors in position and
             other colour components may cause minor drift, but there's not
             much we can do about that. */
          HQASSERT(fabs((vtmp[1]->comps[cindex] - discont) / (c1 - c0)) < 1e-6,
                   "Interpolation to discontinuity failed") ;
          vtmp[1]->comps[cindex] = discont ;

#if DEBUG_SHOW_DISCONTINUITY
          monitorf((uint8 *)"Quad discontinuity in color %d edge (%f,%f,%f)..(%f,%f,%f) at %f split along (%f,%f,%f)..(%f,%f,%f)\n",
                   cindex,
                   v[0]->x, v[0]->y, c0,
                   v[1]->x, v[1]->y, c1,
                   discont,
                   vtmp[0]->x, vtmp[0]->y, vtmp[0]->comps[cindex],
                   vtmp[1]->x, vtmp[1]->y, vtmp[1]->comps[cindex]) ;
#endif

          result = (decompose_quad(vtmp[1], v[3], v[0], vtmp[0], cindex, sinfo) &&
                    decompose_quad(vtmp[0], v[1], v[2], vtmp[1], cindex, sinfo)) ;
        } else { /* Discontinuity is across vertex 2 */
          HQASSERT(v[2]->comps[cindex] - discont == 0.0,
                   "Vertex not at discontinuity") ;

#if DEBUG_SHOW_DISCONTINUITY
          monitorf((uint8 *)"Quad discontinuity in color %d edge (%f,%f,%f)..(%f,%f,%f) at %f through vertex (%f,%f,%f)\n",
                   cindex,
                   v[0]->x, v[0]->y, c0,
                   v[1]->x, v[1]->y, c1,
                   discont,
                   vtmp[0]->x, vtmp[0]->y, vtmp[0]->comps[cindex]) ;
#endif

          result = (decompose_triangle(vtmp[0], v[1], v[2], sinfo) &&
                    decompose_quad(v[0], vtmp[0], v[2], v[3], cindex, sinfo)) ;

        }

        vertex_free(sinfo, vtmp, 3) ;
        return result ;
      }
    }
  }

  /* No discontinuities, decompose into two triangles. Decomposition direction
     is whichever yields shortest new edge. */

  x02 = v0->x - v2->x ;
  y02 = v0->y - v2->y ;
  x13 = v1->x - v3->x ;
  y13 = v1->y - v3->y ;

  if ( x02 * x02 + y02 * y02 < x13 * x13 + y13 * y13 ) {
    return (decompose_triangle(v0, v1, v2, sinfo) &&
            decompose_triangle(v0, v3, v2, sinfo)) ;
  } else {
    return (decompose_triangle(v3, v1, v2, sinfo) &&
            decompose_triangle(v0, v3, v1, sinfo)) ;
  }
}

/** Decompose triangular patch into continuous non-linear Gouraud triangles,
   by looking for function discontinuities. Calls DEVICE_GOURAUD to put
   triangular patches onto DL as necessary. */
Bool decompose_triangle(SHADINGvertex *v0, SHADINGvertex *v1,
                        SHADINGvertex *v2, SHADINGinfo *sinfo)
{

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  /* Detect discontinuous functions and decompose along function boundaries.
     This is only done for one-component functions; two component functions
     are only used for type 1 shading, which is decomposed at a higher level
     because of interpolation rounding errors carrying over between components.
     If we ever extend this code to allow texture mapping of gouraud triangles
     or tensor patches, we may need to add the decomposition for multiple
     components back here. I've left decompose_quad as it is, taking a
     component index parameter.
     */
#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) == 0 )
#endif
  if ( sinfo->nfuncs != 0 && sinfo->ncomps == 1 ) {
    SHADINGvertex *vertices[5], **v ;
    int32 index, order, result ;

    vertices[0] = vertices[3] = v0 ;
    vertices[1] = vertices[4] = v1 ;
    vertices[2] = v2 ;

    /* Test edges for discontinuities. If there is a discontinuity it must
       intersect at two edges, or an edge and a vertex. If we find a
       discontinuity, we decompose into a triangle and a quadrilateral, or two
       triangles, and recurse until all discontinuities are removed from the
       triangle patches. */
    for ( v = vertices ; v < vertices + 3 ; ++v ) {
      USERVALUE bounds[2], discont ;
      USERVALUE c0 = v[0]->comps[0] ;
      USERVALUE c1 = v[1]->comps[0] ;

      /* It doesn't matter which way round we look, the discontinuity should
         still be there. So sort the bounds. */
      if ( c0 < c1 ) {
        bounds[0] = c0 ;
        bounds[1] = c1 ;
      } else if ( c0 > c1 ) {
        bounds[0] = c1 ;
        bounds[1] = c0 ;
      } else /* If bounds are same, there can't be a discontinuity */
        continue ;

      for ( index = 0 ; index < sinfo->nfuncs ; ++index ) {
        SYSTEMVALUE weights[2] ;

        if ( !(sinfo->funcs ?
               fn_find_discontinuity(&sinfo->funcs[index], 0, bounds,
                                     &discont, &order,
                                     FN_SHADING, index,
                                     sinfo->base_fuid, FN_GEN_NA,
                                     &sinfo->fndecode) :
               rcbs_fn_find_discontinuity(sinfo->rfuncs[index], 0, bounds,
                                          &discont, &order)) )
          return FALSE ;

        if ( order != -1 ) {
          SHADINGvertex *vtmp[2] ;
          USERVALUE c2 = v[2]->comps[0] ;

          HQASSERT(discont > bounds[0] && discont < bounds[1],
                   "Discontinuity not contained in range") ;

          /* Found discontinuity in this edge. It must intersect another edge
             or vertex, so find out which it is and sub-divide along that
             line. */
          weights[1] = (discont - c0) / (c1 - c0) ;
          weights[0] = 1 - weights[1] ;

          if ( !vertex_alloc(vtmp, 2) )
            return error_handler(VMERROR) ;

          /* Always a new vertex at discontinuity */
          vertex_interpolate(sinfo, 2, weights, vtmp[0], &v[0], sinfo->ncomps) ;

          /* Fix up rounding errors in discontinuity interpolation by
             performing relative error check for sanity, then forcing the
             colour value to the discontinuity. Rounding errors in position and
             other colour components may cause minor drift, but there's not
             much we can do about that. */
          HQASSERT(fabs((vtmp[0]->comps[0] - discont) / (c1 - c0)) < 1e-6,
                   "Interpolation to discontinuity failed") ;
          vtmp[0]->comps[0] = discont ;

          /* Find other vertex where discontinuity intersects. Only one of
             these weights will be used, depending on whether the
             intersection is edge 2..0 or 1..2. */
          if ( c1 != c2 )
            weights[0] = (c2 - discont) / (c2 - c1) ;
          else
            weights[0] = 0 ; /* Fails inclusion test */
          if ( c2 != c0 )
            weights[1] = (discont - c2) / (c0 - c2) ;
          else
            weights[1] = 0 ; /* Fails inclusion test */

          if ( weights[1] > 0.0 && weights[1] < 1.0 ) { /* Crosses edge 2..0 */
            weights[0] = 1 - weights[1] ;

            vertex_interpolate(sinfo, 2, weights, vtmp[1], &v[2], sinfo->ncomps) ;

            /* Fix up rounding errors in discontinuity interpolation by
               performing relative error check for sanity, then forcing the
               colour value to the discontinuity. Rounding errors in position and
               other colour components may cause minor drift, but there's not
               much we can do about that. */
            HQASSERT(fabs((vtmp[1]->comps[0] - discont) / (c1 - c0)) < 1e-6,
                     "Interpolation to discontinuity failed") ;
            vtmp[1]->comps[0] = discont ;

#if DEBUG_SHOW_DISCONTINUITY
            monitorf((uint8 *)"Triangle discontinuity edge (%f,%f,%f)..(%f,%f,%f) at %f split along (%f,%f,%f)..(%f,%f,%f)\n",
                     v[0]->x, v[0]->y, c0,
                     v[1]->x, v[1]->y, c1,
                     discont,
                     vtmp[0]->x, vtmp[0]->y, vtmp[0]->comps[0],
                     vtmp[1]->x, vtmp[1]->y, vtmp[1]->comps[0]) ;
#endif

            result = (decompose_triangle(v[0], vtmp[0], vtmp[1], sinfo) &&
                      decompose_quad(vtmp[0], v[1], v[2], vtmp[1], 0, sinfo)) ;
          } else if ( weights[0] > 0.0 && weights[0] < 1.0 ) { /* Crosses edge 1..2 */
            weights[1] = 1 - weights[0] ;

            vertex_interpolate(sinfo, 2, weights, vtmp[1], &v[1], sinfo->ncomps) ;

            /* Fix up rounding errors in discontinuity interpolation by
               performing relative error check for sanity, then forcing the
               colour value to the discontinuity. Rounding errors in position and
               other colour components may cause minor drift, but there's not
               much we can do about that. */
            HQASSERT(fabs((vtmp[1]->comps[0] - discont) / (c1 - c0)) < 1e-6,
                     "Interpolation to discontinuity failed") ;
            vtmp[1]->comps[0] = discont ;

#if DEBUG_SHOW_DISCONTINUITY
            monitorf((uint8 *)"Triangle discontinuity edge (%f,%f,%f)..(%f,%f,%f) at %f split along (%f,%f,%f)..(%f,%f,%f)\n",
                     v[0]->x, v[0]->y, c0,
                     v[1]->x, v[1]->y, c1,
                     discont,
                     vtmp[0]->x, vtmp[0]->y, vtmp[0]->comps[0],
                     vtmp[1]->x, vtmp[1]->y, vtmp[1]->comps[0]) ;
#endif

            result = (decompose_triangle(vtmp[0], v[1], vtmp[1], sinfo) &&
                      decompose_quad(vtmp[1], v[2], v[0], vtmp[0], 0, sinfo)) ;
          } else { /* Discontinuity is across opposite vertex */
            HQASSERT(v[2]->comps[0] - discont == 0.0,
                     "Vertex not at discontinuity") ;

#if DEBUG_SHOW_DISCONTINUITY
            monitorf((uint8 *)"Triangle discontinuity edge (%f,%f,%f)..(%f,%f,%f) at %f through vertex (%f,%f,%f)\n",
                     v[0]->x, v[0]->y, c0,
                     v[1]->x, v[1]->y, c1,
                     discont,
                     vtmp[0]->x, vtmp[0]->y, vtmp[0]->comps[0]) ;
#endif

            result = (decompose_triangle(v[0], vtmp[0], v[2], sinfo) &&
                      decompose_triangle(vtmp[0], v[1], v[2], sinfo)) ;
          }

          vertex_free(sinfo, vtmp, 2) ;
          return result ;
        }
      }
    }
  }

  return DEVICE_GOURAUD(sinfo->page, v0, v1, v2, sinfo) ;
}

/** Primitive operation to put single Gouraud-shaded triangle on display list,
   decomposing for colour as necessary. Called through DEVICE_GOURAUD(). Does
   not do function decomposition; higher-level decompose_triangle should be
   called if necessary. Function-based and tensor shadings produce already
   decomposed triangles. */
Bool dodisplaygouraud(DL_STATE *page,
                      SHADINGvertex *v0, SHADINGvertex *v1,
                      SHADINGvertex *v2, SHADINGinfo *sinfo)
{
  mm_pool_t go_pool = dl_choosepool(page->dlpools, MM_ALLOC_CLASS_GOURAUD);

  HQASSERT(v0 && v1 && v2, "Vertices must not be NULL in dodisplaygouraud") ;
  HQASSERT(sinfo, "SHADINGinfo must not be NULL in dodisplaygouraud") ;

  if ( TEST4HUGE(v0->x) || TEST4HUGE(v0->y) ||
       TEST4HUGE(v1->x) || TEST4HUGE(v1->y) ||
       TEST4HUGE(v2->x) || TEST4HUGE(v2->y) )
    return error_handler( LIMITCHECK ) ;

#if defined( DEBUG_BUILD ) || defined(METRICS_BUILD)
  ++n_gouraud_triangles ;
#endif
#if defined( METRICS_BUILD )
  this_decomposition_depth = 1 ;
  this_decomposed_triangles = 0 ;
#endif

  /* Sort vertices Y value, and preserve this invariant thereafter. Equal Y
     values are sorted by X. */
  if ( v0->y > v2->y || (v0->y == v2->y && v0->x > v2->x) ) {
    SHADINGvertex *tmp = v0 ; v0 = v2 ; v2 = tmp ;
  }
  if ( v0->y > v1->y || (v0->y == v1->y && v0->x > v1->x) ) {
    SHADINGvertex *tmp = v0 ; v0 = v1 ; v1 = tmp ;
  } else if ( v1->y > v2->y || (v1->y == v2->y && v1->x > v2->x) ) {
    SHADINGvertex *tmp = v2 ; v2 = v1 ; v1 = tmp ;
  }

  /* Check if whole triangle clipped out */
  if ( v0->y > cclip_bbox.y2 || v2->y < cclip_bbox.y1 )
    return TRUE ;

  if ( v0->x > cclip_bbox.x2 && v1->x > cclip_bbox.x2 &&
       v2->x > cclip_bbox.x2 )
    return TRUE ;

  if ( v0->x < cclip_bbox.x1 && v1->x < cclip_bbox.x1 &&
       v2->x < cclip_bbox.x1 )
    return TRUE ;

#if defined( DEBUG_BUILD ) && defined( ASSERT_BUILD )
  /* Assert that there are no discontinuities across this triangle */
  if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) == 0 &&
       sinfo->nfuncs != 0 && sinfo->ncomps == 1 ) {
    SHADINGvertex *vertices[5], **v ;
    int32 index, order ;

    vertices[0] = vertices[3] = v0 ;
    vertices[1] = vertices[4] = v1 ;
    vertices[2] = v2 ;

    for ( v = vertices ; v < vertices + 3 ; ++v ) {
      USERVALUE bounds[2], discont ;
      USERVALUE c0 = v[0]->comps[0] ;
      USERVALUE c1 = v[1]->comps[0] ;

      /* It doesn't matter which way round we look, the discontinuity should
         still be there. So sort the bounds. */
      if ( c0 < c1 ) {
        bounds[0] = c0 ;
        bounds[1] = c1 ;
      } else if ( c0 > c1 ) {
        bounds[0] = c1 ;
        bounds[1] = c0 ;
      } else /* If bounds are same, there can't be a discontinuity */
        continue ;

      for ( index = 0 ; index < sinfo->nfuncs ; ++index ) {
        if ( !(sinfo->funcs ?
               fn_find_discontinuity(&sinfo->funcs[index], 0, bounds,
                                     &discont, &order,
                                     FN_SHADING, index,
                                     sinfo->base_fuid, FN_GEN_NA,
                                     &sinfo->fndecode) :
               rcbs_fn_find_discontinuity(sinfo->rfuncs[index], 0, bounds,
                                          &discont, &order)) )
          return FALSE ;

        HQASSERT(order == -1, "Discontinuity found across continuous triangle") ;
      }
    }
  }
#endif

  {
    GOURAUDOBJECT *gour = NULL ;
    GOURAUD_DL_ARGS gargs ;
    size_t ugsize ;
    int32 gsize = sizeof(GOURAUDOBJECT) +
                  sizeof(p_ncolor_t) * 3    /* non-decomposed colours */
#if defined(DEBUG_BUILD)
                  + sizeof(int32)           /* linear_triangles counter */
#endif
                  ;

    /* Loop around until the promise is the right size. */

    for (;;) {
      SYSTEMVALUE sc_rnd = SC_PIXEL_ROUND ;
      int32 remaining = gsize;

      if ( mm_dl_promise(go_pool, gsize) != MM_SUCCESS )
        return error_handler(VMERROR);

      /* GOURAUDOBJECTs include pointers that should be aligned for 64-bit
         architectures. Since this is the first alloc from the promise it should
         be so, but assert for it. */
      gour = mm_dl_promise_next(go_pool, sizeof(GOURAUDOBJECT)) ;
      HQASSERT(gour != NULL, "funny memory allocation for gour") ;
      HQASSERT(((uintptr_t)gour & (sizeof(uintptr_t) - 1)) == 0,
               "Allocation not on uintptr_t alignment") ;
      gour->base = gour;
      remaining -= sizeof( GOURAUDOBJECT ) ;

      SC_C2D_UNT_I(gour->coords[0], v0->x, sc_rnd) ;
      SC_C2D_UNT_I(gour->coords[1], v0->y, sc_rnd) ;
      SC_C2D_UNT_I(gour->coords[2], v1->x, sc_rnd) ;
      SC_C2D_UNT_I(gour->coords[3], v1->y, sc_rnd) ;
      SC_C2D_UNT_I(gour->coords[4], v2->x, sc_rnd) ;
      SC_C2D_UNT_I(gour->coords[5], v2->y, sc_rnd) ;
      gour->flags = 0 ;

      /* Reset vertex directions for type 3 functions if necessary */
      if ( sinfo->nfuncs != 0 && sinfo->ncomps == 1 ) {
        SYSTEMVALUE centre = (v0->comps[0] + v1->comps[0] + v2->comps[0]) / 3.0 ;

        VERTEX_FUNCTION_BIAS(page, v0, centre) ;
        VERTEX_FUNCTION_BIAS(page, v1, centre) ;
        VERTEX_FUNCTION_BIAS(page, v2, centre) ;
      }

      /* Add colors of v0, v1, v2 to DL object */
      if ( !vertex_color(v0, sinfo, FALSE) ||
           !vertex_color(v1, sinfo, FALSE) ||
           !vertex_color(v2, sinfo, FALSE) ) {
        mm_dl_promise_free(go_pool);
        return FALSE ;
      }

      dlc_clear(&gargs.dlc_gobj) ;

      if ( (remaining -= sizeof(p_ncolor_t) * 3) >= 0 ) {
        p_ncolor_t *colors = mm_dl_promise_next(go_pool,
                                                sizeof(p_ncolor_t) * 3) ;
        HQASSERT((char *)colors == (char *)(gour + 1),
                 "Promise of initial colors not where expected") ;
        /* These dlc_to_dl's increment the refcounts of the DL colors. If the
           promise does not contain enough space for the whole gouraud object,
           it is disposed of and a new promise with enough space is allocated.
           We don't release the dlcs, so they will have an extra reference
           count, however this is not a problem since the color will be
           regenerated (and the cache hit) second time around, and the colors
           will be purged at erasepage anyway. */
        if ( !dlc_to_dl(page->dlc_context, &colors[0], &v0->dlc) ||
             !dlc_to_dl(page->dlc_context, &colors[1], &v1->dlc) ||
             !dlc_to_dl(page->dlc_context, &colors[2], &v2->dlc) ||
             !dlc_copy(page->dlc_context, &gargs.dlc_gobj, &v0->dlc) ||
             !dlc_merge_shfill(page->dlc_context, &gargs.dlc_gobj, &v1->dlc) ||
             !dlc_merge_shfill(page->dlc_context, &gargs.dlc_gobj, &v2->dlc) ) {
          mm_dl_promise_free(go_pool);
          return FALSE ;
        }
      }

      gargs.bits = sizeof(uintptr_t) * 8 ;
      gargs.remaining = remaining ;
      gargs.flagptr = &gour->flags ;
      gargs.depth = 0 ;
#if defined( ASSERT_BUILD )
      gargs.nextcolor = (p_ncolor_t *)(gour + 1) + 3 ;
#endif
#if defined( DEBUG_BUILD )
      gargs.linear_triangles = 0 ;
#endif

      gargs.coercion = sinfo->coercion ;
      if ( !gouraud_triangle_dl(v0, v1, v2, sinfo, &gargs) ) {
        mm_dl_promise_free(go_pool);
        if ( gargs.coercion == NAME_Discard )
          return TRUE ; /* Was thrown away rather than failing */
        return FALSE ;
      }

#if defined( DEBUG_BUILD )
      if ( (gargs.remaining -= sizeof(int32)) >= 0 ) {
        int32 *triangles = mm_dl_promise_next(go_pool, sizeof(int32));
        *triangles = gargs.linear_triangles ;
      }
#endif

      gsize -= gargs.remaining ; /* Adjust size to amount actually used */
      if ( gargs.remaining == 0 )
        break ;

      HQASSERT(gargs.remaining < 0, "Initial size of promise too large");

      /* Free promise, loop round again with correct size instead of guess */
      mm_dl_promise_free(go_pool);
    } /* for (;;) */

    ugsize = mm_dl_promise_end(go_pool);
    gour->gsize = CAST_UNSIGNED_TO_INT32(ugsize);
    HQASSERT(gour->gsize == gsize, "Gouraud size not consistent with promise");
    track_dl(gour->gsize, MM_ALLOC_CLASS_GOURAUD, TRUE);

    /* Set color to an intermediate value so that DL object will render in all
       channels. */
    dlc_release(page->dlc_context, dlc_currentcolor(page->dlc_context));
    dlc_copy_release(page->dlc_context, dlc_currentcolor(page->dlc_context),
                     &gargs.dlc_gobj);

    /* Now add gouraud object to display list */
    if ( !addgourauddisplay(page, gour) )
      return FALSE ;

#if defined( METRICS_BUILD )
    if ( gsize > max_gouraud_dl_size )
      max_gouraud_dl_size = gsize;
    total_gouraud_dl_size += gsize;
#endif
  }

  return TRUE ;
}

/** Divide up smooth-shading into triangles that can be linearly interpolated.
   This routine does the division, but only stores the flags and device
   colours if it stays within the promise size. This is so that the first time
   we try dividing, we can guess the size of the promise; if it turns out to
   be too small, then we still calculate the correct size of promise to use,
   which will be used second time around. The value pointed to by remaining
   indicates the shortfall on exit. */
static Bool gouraud_triangle_dl(SHADINGvertex *v0, SHADINGvertex *v1,
                                SHADINGvertex *v2, SHADINGinfo *sinfo,
                                GOURAUD_DL_ARGS *gargs)
{
  SHADINGvertex *vsub[3], *vptr[5] ;
  int32 index ;
  int32 ncomp = sinfo->ncomps ;
  Bool result = FALSE, linear = TRUE ;
  uintptr_t mask ;
  dl_color_t dcolor;
  mm_pool_t go_pool = dl_choosepool(sinfo->page->dlpools, MM_ALLOC_CLASS_GOURAUD);
  dlc_context_t *dlc_context = sinfo->page->dlc_context;

  static SYSTEMVALUE halfs[2] = {0.5, 0.5} ;

  HQASSERT(gargs, "gargs pointer NULL in gouraud_triangle_dl") ;
  HQASSERT(gargs->bits >= 0 && gargs->bits <= sizeof(uintptr_t) * 8,
           "Strange number of bits left in flagptr") ;

  HQASSERT(v0 && v0->converted, "Vertex v0 not converted in gouraud_triangle_dl") ;
  HQASSERT(v1 && v1->converted, "Vertex v1 not converted in gouraud_triangle_dl") ;
  HQASSERT(v2 && v2->converted, "Vertex v2 not converted in gouraud_triangle_dl") ;

  /* Maintain points in highest-lowest y order */
#if 0
  /* I'd like to use this assert, to reduce sorting when the linear
     interpolated bitblit is implemented, but it doesn't work because of the
     subdivision; for example, point 2 can be slightly above and
     right of point 3, but when subdivided the new points 2 and 3 are
     truncated to the same line, with the x-coordinates now out of order. */
  HQASSERT((v0->y < v1->y ||
            (v0->y == v1->y && v0->x <= v1->x)) &&
           (v1->y < v2->y ||
            (v1->y == v2->y && v1->x <= v2->x)),
           "Triangle points out of order in gouraud_triangle_dl") ;
  /* Weaker assert follows: */
#else
  HQASSERT(v0->y <= v1->y && v1->y <= v2->y,
           "Triangle points out of order in gouraud_triangle_dl") ;
#endif

  dlc_clear(&dcolor);

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

#if defined( METRICS_BUILD )
  if ( this_decomposition_depth > max_decomposition_depth )
    max_decomposition_depth = this_decomposition_depth ;
#endif

  /* Work out centre colour and half-way colours */
  if ( !vertex_alloc(vsub, 3) )
    return error_handler(VMERROR) ;

  vptr[0] = vptr[3] = v0 ;
  vptr[1] = vptr[4] = v1 ;
  vptr[2] = v2 ;

  /* Assume colour linearity if doing an axial or radial blend; colour
     decomposition for these is done at the blend level. Also, if exceeding
     max decomposition depth, assume linear. */
  if ( sinfo->type != 2 && sinfo->type != 3 &&
       gargs->depth < UserParams.MaxGouraudDecompose ) {
    SYSTEMVALUE minsize = UserParams.MinShadingSize ;
    SYSTEMVALUE xdpi2, ydpi2 ;

    /* Test if triangle is smaller than decomposition limit */
    minsize /= 72.0 ; /* Fractions of an inch */
    minsize *= minsize ; /* work squared to avoid sqrt */

    xdpi2 = sinfo->page->xdpi * sinfo->page->xdpi ; /* Squared resolutions */
    ydpi2 = sinfo->page->ydpi * sinfo->page->ydpi ;

    for ( index = 0 ; index < 3 ; ++index ) {
      SYSTEMVALUE dx = (vptr[index]->x - vptr[index + 1]->x) ;
      SYSTEMVALUE dy = (vptr[index]->y - vptr[index + 1]->y) ;

      dx *= dx ; dy *= dy ; /* Square to avoid sqrt */

      /* Do not decompose if edge is shorter than one pixel, or if edge is
         shorter than MinShadingSize units in default userspace. */
      if ( dx + dy > 1.0 && dx / xdpi2 + dy / ydpi2 > minsize ) {
        dl_color_t* cptr[3] ;
        SYSTEMVALUE weights[3] ;

        /* Initialise probe weights for linearity testing; we start at the centre
           point, then choose a well-distributed repeatable pseudo random order
           of points within the triangle to test. */

        weights[0] = weights[1] = weights[2] = 1.0/3.0 ;

        cptr[0] = &v0->dlc ;
        cptr[1] = &v1->dlc ;
        cptr[2] = &v2->dlc ;

        /* Initialise Sobol' generator. We deliberately do not initialise the
           fractional terms to zero, since the first few terms of both
           sequences add up to 1, which would cause the vertices to be tested,
           wasting several test cycles. */
        sobol(-1, &weights[0], &weights[1]) ;

#if defined( DEBUG_BUILD )
        for ( index = 0 ; index < 3 ; ++index ) {
          if ( (shading_debug_flag & SHADING_DEBUG_TRACE_COLORS) != 0 ) {
            monitorf((uint8 *)"Triangle %d vertex %d input color ",
                     n_gouraud_triangles, index) ;
            debug_print_vertex(vptr[index], sinfo) ;
            monitorf((uint8 *)" DL color ") ;
            debug_print_dlc(cptr[index]) ;
            monitorf((uint8 *)"\n") ;
          }
        }
#endif

        for ( index = 0 ; ; ++index ) {
          /* Interpolate vertices to get sample point */
          vertex_interpolate(sinfo, 3, weights, vsub[0], vptr, ncomp) ;

          if ( !vertex_color(vsub[0], sinfo, TRUE) ||
               !dlc_alloc_interpolate(dlc_context, 3, weights, &dcolor, cptr) )
            goto gtd_error ;

#if defined( DEBUG_BUILD )
          if ( (shading_debug_flag & SHADING_DEBUG_TRACE_COLORS) != 0 ) {
            monitorf((uint8 *)"Triangle %d probe at (%.2f, %.2f, %.2f) input color ",
                     n_gouraud_triangles, weights[0], weights[1], weights[2]) ;
            debug_print_vertex(vsub[0], sinfo) ;
            monitorf((uint8 *)" DL color ") ;
            debug_print_dlc(&dcolor) ;
          }
#endif

          if ( !shading_color_close_enough(sinfo, &vsub[0]->dlc, &dcolor,
                                           gsc_getRS(gstateptr->colorInfo)) ) {
            linear = FALSE ;
            dlc_release(dlc_context, &dcolor);

#if defined( DEBUG_BUILD )
            if ( (shading_debug_flag & SHADING_DEBUG_TRACE_COLORS) != 0 ) {
              monitorf((uint8 *)" FAIL smoothnessbands %d\n",
                       sinfo->smoothnessbands) ;
            }
#endif

            break ;
          }

#if defined( DEBUG_BUILD )
          if ( (shading_debug_flag & SHADING_DEBUG_TRACE_COLORS) != 0 ) {
            monitorf((uint8 *)" OK\n") ;
          }
#endif

          dlc_release(dlc_context, &dcolor);

          if ( index >= UserParams.GouraudLinearity )
            break ; /* Done enough testing */

          /* Select random point to test. See Graphic Gems, p.24 for theory on
             this. */
          sobol(index, &weights[0], &weights[1]) ;

          if ( weights[0] + weights[1] > 1 ) {
            weights[0] = 1 - weights[0] ;
            weights[1] = 1 - weights[1] ;
          }

          weights[2] = 1 - weights[0] - weights[1] ;
        }

        break ; /* Don't test any more edge lengths, we've done color test */
      }
    }
  }

  if ( --gargs->bits < 0 ) { /* Get a new flag area if no more bits */
    gargs->bits = sizeof(uintptr_t) * 8 - 1 ; /* Promise is in word-sized chunks */
    if ( (gargs->remaining -= sizeof(uintptr_t)) >= 0 ) {
      gargs->flagptr = mm_dl_promise_next(go_pool, sizeof(uintptr_t)) ;
      HQASSERT(gargs->flagptr == (uintptr_t *)gargs->nextcolor,
               "promise allocated unexpected flagptr location") ;
#if defined( ASSERT_BUILD )
      HQASSERT(sizeof(uintptr_t) == sizeof(p_ncolor_t), "p_ncolor_t not same as flags") ;
      gargs->nextcolor += 1 ;
#endif
      *(gargs->flagptr) = 0 ;
    } else
      gargs->flagptr = NULL ;
  }
  mask = (uintptr_t)1 << gargs->bits ;

  /* Test to make sure cross products will not overflow when rendering
     decomposed triangle. */
  if ( linear ) {
    dcoord x0, y0, x1, y1, x2, y2 ;
    SYSTEMVALUE sc_rnd = SC_PIXEL_ROUND ;

    SC_C2D_UNT_I(x0, v0->x, sc_rnd) ;
    SC_C2D_UNT_I(y0, v0->y, sc_rnd) ;
    SC_C2D_UNT_I(x1, v1->x, sc_rnd) ;
    SC_C2D_UNT_I(y1, v1->y, sc_rnd) ;
    SC_C2D_UNT_I(x2, v2->x, sc_rnd) ;
    SC_C2D_UNT_I(y2, v2->y, sc_rnd) ;

    /* Cross product adz is x02 * y01 - x01 * y02 */
    linear = cross_check(x0, y0, x1, y1, x2, y2) ;
    if ( linear ) {
      dl_color_iter_t ic0, ic1, ic2 ;
      dlc_iter_result_t ir0, ir1, ir2 ;
      COLORANTINDEX ci0, ci1, ci2 ;
      COLORVALUE cv0, cv1, cv2 ;

      for ( ir0 = dlc_first_colorant(&v0->dlc, &ic0, &ci0, (COLORVALUE *)&cv0),
              ir1 = dlc_first_colorant(&v1->dlc, &ic1, &ci1, (COLORVALUE *)&cv1),
              ir2 = dlc_first_colorant(&v2->dlc, &ic2, &ci2, (COLORVALUE *)&cv2) ;
            ir0 != DLC_ITER_NOMORE ;
            ir0 = dlc_next_colorant(&v0->dlc, &ic0, &ci0, (COLORVALUE *)&cv0),
              ir1 = dlc_next_colorant(&v1->dlc, &ic1, &ci1, (COLORVALUE *)&cv1),
              ir2 = dlc_next_colorant(&v2->dlc, &ic2, &ci2, (COLORVALUE *)&cv2) ) {
        HQASSERT(ir0 == ir1 && ir0 == ir2, "Iteration results not in sync") ;
        HQASSERT(ir0 != DLC_ITER_ALL01, "Need to know number of colorants");
        HQASSERT(ir0 != DLC_ITER_NONE, "Shading with NONE separation colors");
        HQASSERT(ir0 == DLC_ITER_COLORANT || ir0 == DLC_ITER_ALLSEP,
                 "Not a colorant or /All from dlc_first/next_colorant");
        HQASSERT(ci0 == ci1 && ci0 == ci2, "Colorant indices not in sync") ;

        /* Cross product ady is x01 * c02 - x02 * c01 */
        linear = cross_check(x0, cv0, x2, cv2, x1, cv1);
        if ( !linear )
          break ;

        /* Cross product adx is y02 * c01 - y01 * c02 */
        linear = cross_check(y0, cv0, y1, cv1, y2, cv2);
        if ( !linear )
          break ;
      }

      HQASSERT(ir0 == ir1 && ir0 == ir2, "Iteration results not in sync") ;
    }
  }

  if ( linear ) { /* Close enough to linear, so fill with linear triangle */

    if ( isHDLTEnabled(*gstateptr) ) {
      int32 coercion = gargs->coercion ; /* Desired coercion */

      if ( coercion == NAME_Gouraud ) {
        switch ( IDLOM_GOURAUD(v0, v1, v2, sinfo) ) {
        case NAME_Discard: /* Special case; flag discard */
          gargs->coercion = NAME_Discard ;
        case NAME_false:
          goto gtd_error ;
        case NAME_Fill:
          coercion = NAME_Fill ;
          break ;
        default:
          HQFAIL("HDLT callback returned bad value") ;
        case NAME_Add:
          break ;
        }
      }

      if ( coercion == NAME_Fill ) { /* Decompose into many ugly little triangles */
        if ( !gouraud_triangle_coerce(v0, v1, v2, sinfo, gargs) )
          goto gtd_error ;
      }
    }

    if ( gargs->flagptr ) /* Clear this bit to indicate no subdivision */
      *(gargs->flagptr) &= ~mask ;

#if defined( DEBUG_BUILD )
    ++gargs->linear_triangles ;
#endif

    /* Mark or allocate halftone forms. */
    if (gsc_updateHTCacheForShfillDecomposition(gstateptr->colorInfo, sinfo->base_index)) {
      result = TRUE ;

#if defined( METRICS_BUILD )
      ++n_decomposed_triangles ;
      if ( ++this_decomposed_triangles > max_decomposition_triangles )
        max_decomposition_triangles = this_decomposed_triangles ;
#endif
    }
  } else { /* Sub-divide and try again */
    if ( gargs->flagptr ) /* Set this bit to indicate subdivision */
      *(gargs->flagptr) |= mask ;

    /* Divide each edge in half, then convert new vertex colour */
    for ( index = 0 ; index < 3 ; ++index ) {
      SHADINGvertex *vnew = vsub[index] ;

      /* Interpolate new vertex position and components */
      vertex_interpolate(sinfo, 2, halfs, vnew, vptr + index, ncomp) ;

      /* Convert new vertex color */
      if ( !vertex_color(vnew, sinfo, FALSE) )
        goto gtd_error ;
    }

    /* Add new color to DL object */
    if ( (gargs->remaining -= sizeof(p_ncolor_t) * 3) >= 0 ) {
      p_ncolor_t *colors = mm_dl_promise_next(go_pool, sizeof(p_ncolor_t) * 3);
      HQASSERT(colors == gargs->nextcolor,
               "promise allocated unexpected color location") ;
#if defined( ASSERT_BUILD )
      gargs->nextcolor += 3 ;
#endif
      /* These dlc_to_dl's increment the refcounts of the DL colors. If the
         promise does not contain enough space for the whole gouraud object,
         it is disposed of and a new promise with enough space is allocated.
         We don't release the dlcs, so they will have an extra reference
         count, however this is not a problem since the color will be
         regenerated (and the cache hit) second time around, and the colors
         will be purged at erasepage anyway. */
      /* Merge color channels, creating a colour with intermediate values in
         preference to solids/clear. */
      if ( !dlc_to_dl(dlc_context, &colors[0], &vsub[0]->dlc) ||
           !dlc_to_dl(dlc_context, &colors[1], &vsub[1]->dlc) ||
           !dlc_to_dl(dlc_context, &colors[2], &vsub[2]->dlc) ||
           !dlc_merge_shfill(dlc_context, &gargs->dlc_gobj, &vsub[0]->dlc) ||
           !dlc_merge_shfill(dlc_context, &gargs->dlc_gobj, &vsub[1]->dlc) ||
           !dlc_merge_shfill(dlc_context, &gargs->dlc_gobj, &vsub[2]->dlc) )
        goto gtd_error ;
    }

#if defined( METRICS_BUILD )
    ++this_decomposition_depth ;
#endif

    ++gargs->depth ;

    /* Y-order of vertices maintained in recursive calls */
    if ( !gouraud_triangle_dl(v0, vsub[0], vsub[2], sinfo, gargs) ||
         !gouraud_triangle_dl(vsub[0], v1, vsub[1], sinfo, gargs) ||
         !gouraud_triangle_dl(vsub[2], vsub[1], v2, sinfo, gargs) ||
         !gouraud_triangle_dl(vsub[0], vsub[2], vsub[1], sinfo, gargs) )
      goto gtd_error ;

    --gargs->depth ;

    result = TRUE ;

#if defined( METRICS_BUILD )
    --this_decomposition_depth ;
#endif
  }

gtd_error:
  vertex_free(sinfo, vsub, 3) ;

  return result ;
}

static Bool gouraud_triangle_coerce(SHADINGvertex *v0, SHADINGvertex *v1,
                                    SHADINGvertex *v2, SHADINGinfo *sinfo,
                                    GOURAUD_DL_ARGS *gargs)
{
  SYSTEMVALUE minsize = UserParams.MinShadingSize ;
  SYSTEMVALUE xdpi2, ydpi2 ;
  SHADINGvertex *vsub[3], *vptr[5] ;
  int32 index ;
  int32 ncomp = sinfo->ncomps ;
  Bool result = FALSE, tryflat = TRUE ;

  static SYSTEMVALUE halfs[2] = {0.5, 0.5} ;
  static SYSTEMVALUE thirds[3] = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0} ;

  HQASSERT(gargs, "gargs pointer NULL in gouraud_triangle_dl") ;

  HQASSERT(v0 && v0->converted, "Vertex v0 not converted in gouraud_triangle_coerce") ;
  HQASSERT(v1 && v1->converted, "Vertex v1 not converted in gouraud_triangle_coerce") ;
  HQASSERT(v2 && v2->converted, "Vertex v2 not converted in gouraud_triangle_coerce") ;

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  if ( !vertex_alloc(vsub, 3) )
    return error_handler(VMERROR) ;

  vptr[0] = vptr[3] = v0 ;
  vptr[1] = vptr[4] = v1 ;
  vptr[2] = v2 ;

  /* Test if triangle is smaller than decomposition limit */
  minsize /= 72.0 ; /* Fractions of an inch */
  minsize *= minsize ; /* work squared to avoid sqrt */

  xdpi2 = sinfo->page->xdpi * sinfo->page->xdpi ; /* Squared resolutions */
  ydpi2 = sinfo->page->ydpi * sinfo->page->ydpi ;

  for ( index = 0 ; index < 3 ; ++index ) {
    SYSTEMVALUE dx = (vptr[index]->x - vptr[index + 1]->x) ;
    SYSTEMVALUE dy = (vptr[index]->y - vptr[index + 1]->y) ;

    dx *= dx ; dy *= dy ; /* Square to avoid sqrt */

    /* Do not decompose if edge is shorter than two pixels, or if edge is
       shorter than MinShadingSize units in default userspace. */
    if ( dx + dy > 2.0 && dx / xdpi2 + dy / ydpi2 > minsize ) {
      SYSTEMVALUE weights[3] ;

      /* Initialise probe weights for linearity testing; we start at the centre
         point, then choose a well-distributed repeatable pseudo random order
         of points within the triangle to test. */

      weights[0] = weights[1] = weights[2] = 1.0/3.0 ;

      /* Initialise Sobol' generator. We deliberately do not initialise the
         fractional terms to zero, since the first few terms of both
         sequences add up to 1, which would cause the vertices to be tested,
         wasting several test cycles. */
      sobol(-1, &weights[0], &weights[1]) ;

      for ( index = 0 ; ; ++index ) {
        int32 vindex ;

        /* Interpolate vertices to get sample point */
        vertex_interpolate(sinfo, 3, weights, vsub[0], vptr, ncomp) ;

        if ( !vertex_color(vsub[0], sinfo, TRUE) )
          goto gtc_error ;

        for ( vindex = 0 ; tryflat && vindex < 3 ; ++vindex ) {
          if ( !vertex_color(vptr[vindex], sinfo, TRUE) )
            goto gtc_error ;

          if ( !shading_color_close_enough(sinfo, &vsub[0]->dlc_probe, &vptr[vindex]->dlc_probe,
                                           gsc_getRS(gstateptr->colorInfo)) )
            tryflat = FALSE ;
        }

        if ( !tryflat || index >= UserParams.GouraudLinearity )
          break ; /* Done enough testing */

        /* Select random point to test. See Graphic Gems, p.24 for theory on
           this. */
        sobol(index, &weights[0], &weights[1]) ;

        if ( weights[0] + weights[1] > 1 ) {
          weights[0] = 1 - weights[0] ;
          weights[1] = 1 - weights[1] ;
        }

        weights[2] = 1 - weights[0] - weights[1] ;
      }

      break ; /* Don't test any more edge lengths, we've done color test */
    }
  }

  if ( tryflat ) { /* Close enough to flat, so fill flat shade */
    /* Interpolate vertices to get centroid */
    vertex_interpolate(sinfo, 3, thirds, vsub[0], vptr, ncomp) ;

    /* Set color to centre interpolated point */
    if ( ! shading_color_function(vsub[0]->comps, vsub[0]->upwards, sinfo) ||
         ! gsc_setcolordirect(gstateptr->colorInfo, sinfo->base_index, sinfo->scratch) )
      goto gtc_error ;

    path_fill_three(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y) ;

    /* Wouldn't be here unless HDLT on and coercing to fill */
    switch ( IDLOM_FILL(sinfo->base_index, NZFILL_TYPE, &i3cpath, NULL) ) {
    case NAME_Discard: /* Special case; flag discard */
      gargs->coercion = NAME_Discard ;
    case NAME_false:
      break ;
    default:
      HQFAIL("Bad return value from IDLOM_FILL") ;
    case NAME_Add:
      result = TRUE ;
      break ;
    }
  } else { /* Sub-divide and try again */
    /* Divide each edge in half, then convert new vertex colour */
    for ( index = 0 ; index < 3 ; ++index ) {
      SHADINGvertex *vnew = vsub[index] ;

      /* Interpolate new vertex position and components */
      vertex_interpolate(sinfo, 2, halfs, vnew, vptr + index, ncomp) ;

      /* Convert new vertex color */
      if ( !vertex_color(vnew, sinfo, FALSE) )
        goto gtc_error ;
    }

    /* Y-order of vertices maintained in recursive calls */
    if ( !gouraud_triangle_coerce(v0, vsub[0], vsub[2], sinfo, gargs) ||
         !gouraud_triangle_coerce(vsub[0], v1, vsub[1], sinfo, gargs) ||
         !gouraud_triangle_coerce(vsub[2], vsub[1], v2, sinfo, gargs) ||
         !gouraud_triangle_coerce(vsub[0], vsub[2], vsub[1], sinfo, gargs) )
      goto gtc_error ;

    result = TRUE ;
  }

gtc_error:
  vertex_free(sinfo, vsub, 3) ;

  return result ;
}


/* Log stripped */
