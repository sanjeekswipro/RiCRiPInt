/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!src:shadecev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Colour and vertex utility functions for smooth shading.
 */

#include "core.h"
#include "jobmetrics.h"
#include "swerrors.h"
#include "mm.h"
#include "monitor.h"

#include "dl_color.h" /* p_ncolor_t etc */
#include "graphics.h"
#include "gstack.h"
#include "functns.h"
#include "shadex.h"   /* SHADINGinfo etc */
#include "gu_chan.h"  /* GUCR_RASTERSTYLE etc */
#include "gs_color.h" /* GSC_SHFILL */
#include "color.h" /* ht_getClear */
#include "dlstate.h" /* inputpage */
#include "preconvert.h" /* preconvert_probe */

#include "gschead.h"
#include "gschcms.h" /* REPRO_TYPE_* */

#include "rcbshfil.h" /* rcbs_* */


/** Closeness measure; is device color close enough to interpolated and
    transformed color after transformation? Input color is assumed to be
    vertex, if this color is solid special dispensation is made to prevent
    excessive sub-dividing in a fruitless attempt to achieve the same color.
    The dl colors contain device raster style colorants (shading_color probes
    each group transformation and the virtual device). */
Bool shading_color_close_enough(SHADINGinfo *sinfo,
                                dl_color_t * pDlc_i,
                                dl_color_t * pDlc_v,
                                GUCR_RASTERSTYLE *hRasterStyle)
{
  dl_color_iter_t dl_color_iter_i, dl_color_iter_v;
  dlc_iter_result_t iter_i, iter_v; /* DLC_ITER_ALLSEP, DLC_ITER_ALL01,
                                     DLC_ITER_COLORANT, DLC_ITER_NOMORE */
  COLORANTINDEX ci_i, ci_v;
  COLORVALUE cv_i, cv_v;

  /* for all colorants... */
  for (iter_i = dlc_first_colorant(pDlc_i, &dl_color_iter_i, &ci_i, &cv_i),
         iter_v = dlc_first_colorant(pDlc_v, &dl_color_iter_v, &ci_v, &cv_v);
       iter_i != DLC_ITER_NOMORE && iter_v != DLC_ITER_NOMORE;
       iter_i = dlc_next_colorant(pDlc_i, &dl_color_iter_i, &ci_i, &cv_i),
         iter_v = dlc_next_colorant(pDlc_v, &dl_color_iter_v, &ci_v, &cv_v)) {
    int32 distance = (int32)cv_i - (int32)cv_v;
    uint32 bands;

    if (distance < 0)
      distance = -distance;

    HQASSERT(iter_i == iter_v,
             "colorants walked differently; should have been transformed through same chain");
    HQASSERT(iter_i != DLC_ITER_ALL01,
             "we can't cope with DLC_ITER_ALL01 - we don't know how many colorants there are");
    HQASSERT(iter_i != DLC_ITER_NONE,
             "somehow we're are shading with NONE separation colors");
    HQASSERT(iter_i == DLC_ITER_COLORANT || iter_i == DLC_ITER_ALLSEP,
             "unexpected result from dlc_first/next_colorant");
    HQASSERT(ci_i == ci_v, "different colorant indexes from dlc_first/next_colorant");

    /* The dl colors contain device raster style colorants (shading_color probes
       each group transformation and the virtual device). */
    bands = ht_getClear(sinfo->spotno, REPRO_TYPE_VIGNETTE, ci_i, hRasterStyle);

    if ( sinfo->smoothnessbands < bands )
      bands = sinfo->smoothnessbands ;

    /* The colorant step per band is COLORVALUE_MAX/bands, both sides of the
       inequality distance > COLORVALUE_MAX/bands are multiplied to make the
       arithmetic quicker and more exact. */
    if ( (uint32)distance * bands > COLORVALUE_MAX && distance > 1 )
      return FALSE ;
  }

  /* Test opacity... */
  {
    int32 distance = (int32)dlc_color_opacity(pDlc_i) - (int32)dlc_color_opacity(pDlc_v);
    if (distance < 0)
      distance = -distance;
    if ( (uint32)distance * sinfo->smoothnessbands > COLORVALUE_MAX && distance > 1 )
      return FALSE ;
  }

  HQASSERT(iter_i == iter_v, "loop terminated with different conditions");
  return TRUE ;
}

/** Extract evaluate shading function, but don't color convert. */
Bool shading_color_function(USERVALUE *comps, Bool upwards, SHADINGinfo *sinfo)
{
  Bool flip = FALSE;
  int32 i = 0, o = 0 ;
  int32 nvalues = sinfo->nfuncs != 0 ? sinfo->nfuncs : sinfo->ncolors ;

  if (sinfo->funcs == NULL &&
      ColorspaceIsSubtractive(gsc_getcolorspace(gstateptr->colorInfo, sinfo->base_index)))
    flip = TRUE;

  while ( o < sinfo->ncolors ) {
    if ( i >= nvalues ) {
      sinfo->scratch[o++] = 0.0f ; /* Zero remaining colors */
    } else if ( sinfo->nfuncs == 0 ) {
      sinfo->scratch[o++] = comps[i++] ; /* Use value as colour directly */
    } else { /* Apply input component(s) to all functions. */
      HQASSERT((sinfo->type != 1 && theLen(sinfo->fndecode) == 2) ||
               (sinfo->type == 1 && theLen(sinfo->fndecode) == 4),
               "Decode array wrong length in shading_color") ;

      if ( !(sinfo->funcs ?
             fn_evaluate_with_direction(&sinfo->funcs[i], comps,
                                        &sinfo->scratch[o], upwards,
                                        FN_SHADING, i, sinfo->base_fuid,
                                        FN_GEN_NA, &sinfo->fndecode) :
             rcbs_fn_evaluate_with_direction(sinfo->rfuncs[i], comps,
                                             &sinfo->scratch[o], upwards)) )
        return FALSE ;

      /* Recombine always stores additive values in the function;
         may need to flip to subtractive to match color space. */
      if (flip)
        sinfo->scratch[o] = 1.0f - sinfo->scratch[o];

      ++i ;
      o += sinfo->noutputs ;
    }
  }

  return TRUE ;
}

/** Set the opacity value, mapping opacity through a function if necs (ie
    when there are gradient stops). */
static Bool shading_opacity_function(USERVALUE opacity_input,
                                     Bool upwards, SHADINGinfo *sinfo)
{
  COLORVALUE opacity_output;

  /* If opacity_func is null, opacity varies linearly between a lower and an
     upper value and we just use opacity directly. */
  if ( sinfo->opacity_func ) {
    if ( !fn_evaluate_with_direction(sinfo->opacity_func,
                                     &opacity_input, &opacity_input, upwards,
                                     FN_SHOPACITY, 0,
                                     sinfo->base_fuid, FN_GEN_NA, NULL))
      return FALSE ;
  }

  opacity_output = FLOAT_TO_COLORVALUE(opacity_input);
  dl_set_currentopacity(sinfo->page->dlc_context, opacity_output);

  return TRUE ;
}

/** Extract color and prepare colorconvert, including lookup. */
Bool shading_color(USERVALUE *comps, USERVALUE opacity,
                   Bool upwards, SHADINGinfo *sinfo, Bool probe)
{
  Bool success;
  /* remember flags for the shfill */
  uint8 saved_spflags = dl_currentspflags(sinfo->page->dlc_context);
  /* save/restore dl_currentopacity as this is changed by shading_opacity_function */
  COLORVALUE saved_opacity = dl_currentopacity(sinfo->page->dlc_context);

  success = shading_color_function(comps, upwards, sinfo) &&
            shading_opacity_function(opacity, upwards, sinfo) &&
            gsc_setcolordirect(gstateptr->colorInfo, sinfo->base_index, sinfo->scratch) &&
            gsc_invokeChainSingle(gstateptr->colorInfo, sinfo->base_index) ;

  if ( success && probe )
    /* Requested color is for linearity testing and must take account of
       backend color transforms. */
    success = preconvert_probe(sinfo->page->currentGroup, GSC_SHFILL,
                               sinfo->spotno, REPRO_TYPE_VIGNETTE,
                               dlc_currentcolor(sinfo->page->dlc_context),
                               sinfo->lca) ;

  dl_set_currentopacity(sinfo->page->dlc_context, saved_opacity);
  dl_set_currentspflags(sinfo->page->dlc_context, saved_spflags);
  return success;
}

/** Extract vertex color, cache in vertex structure and allocate halftone. */
Bool vertex_color(SHADINGvertex *vertex, SHADINGinfo *sinfo, Bool probe)
{
  /* Only change vertex color if not already looked up */
  if ( !vertex->converted ) {
    if ( !shading_color(vertex->comps, vertex->opacity, vertex->upwards,
                        sinfo, FALSE) )
      return FALSE ;

    /* Save DL color in vertex structure */
    dlc_copy_release(sinfo->page->dlc_context, &vertex->dlc,
                     dlc_currentcolor(sinfo->page->dlc_context));

    vertex->converted = TRUE ;
  }

  /* Only change vertex probe color if not already looked up */
  if ( probe && !vertex->probeconverted ) {
    /* dlc_probe is used for linearity testing and must take account of
       backend color transforms. */
    if ( !preconvert_probe(sinfo->page->currentGroup, GSC_SHFILL,
                           sinfo->spotno, REPRO_TYPE_VIGNETTE,
                           &vertex->dlc, sinfo->lca) )
      return FALSE;

    /* Save probe DL color in vertex structure */
    dlc_copy_release(sinfo->page->dlc_context, &vertex->dlc_probe,
                     dlc_currentcolor(sinfo->page->dlc_context));

    vertex->probeconverted = TRUE ;
  }

  return TRUE ;
}

#if defined( DEBUG_BUILD )
void debug_print_vertex(SHADINGvertex *v, SHADINGinfo *info)
{
  int index ;
  int prefix = '(' ;

  (void)vertex_color(v, info, FALSE) ; /* Make sure scratch is up to date */

  monitorf((uint8 *)"x=%.2f y=%.2f alpha=%.4f %s ",
           v->x, v->y, v->opacity, v->upwards ? "up" : "down") ;

  for ( index = 0 ; index < info->ncomps ; ++index ) {
    monitorf((uint8 *)"%c%.4f", prefix , v->comps[index]) ;
    prefix = ' ' ;
  }

  if ( info->ncolors != 0 ) {
    monitorf((uint8 *)" ->") ;
    for ( index = 0 ; index < info->ncolors ; ++index ) {
      monitorf((uint8 *)" %.4f", info->scratch[index]) ;
    }
  }

  monitorf((uint8 *)")") ;
}
#endif


#ifdef METRICS_BUILD


static struct vertex_metrics {
  size_t vertex_pool_max_size;
  int32 vertex_pool_max_objects;
  size_t vertex_pool_max_frag;
} vertex_metrics;

static Bool vertex_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Shading")) )
    return FALSE;
  SW_METRIC_INTEGER("PeakPoolSize", (int32)vertex_metrics.vertex_pool_max_size);
  SW_METRIC_INTEGER("PeakPoolObjects", vertex_metrics.vertex_pool_max_objects);
  SW_METRIC_INTEGER("PeakPoolFragmentation", (int32)vertex_metrics.vertex_pool_max_frag);
  sw_metrics_close_group(&metrics);
  sw_metrics_close_group(&metrics);
  return TRUE;
}

static void vertex_metrics_reset(int reason)
{
  struct vertex_metrics init = { 0 };
  UNUSED_PARAM(int, reason);
  vertex_metrics = init;
}

static sw_metrics_callbacks vertex_metrics_hook = {
  vertex_metrics_update,
  vertex_metrics_reset,
  NULL
};


#endif /* METRICS_BUILD */


#define VERTEX_SIZE(_ncomp) (sizeof(SHADINGvertex) + (_ncomp) * sizeof(USERVALUE))

mm_pool_t mm_pool_shading = NULL ;

static int32 vertex_alloc_ncomp = 0 ;

Bool vertex_pool_create(int32 ncomp)
{
  int32 size ;

  HQASSERT(mm_pool_shading == NULL, "Shading pool already created") ;
  HQASSERT(vertex_alloc_ncomp == 0, "Vertex allocation components corrupted") ;

  size = VERTEX_SIZE(ncomp) ;

  if ( mm_pool_create(&mm_pool_shading, SHADING_POOL_TYPE,
                      SHADING_POOL_PARAMS(size)) == MM_SUCCESS ) {
    struct mm_sac_classes_t sac_classes[] = { /* size, num, freq */
      { DWORD_ALIGN_UP(size_t, sizeof(SHADINGvertex)), 128,  1 },
    } ;
    sac_classes[0].block_size = DWORD_ALIGN_UP(size_t, size) ;
    if ( mm_sac_create(mm_pool_shading,
                       sac_classes,
                       sizeof(sac_classes) / sizeof(sac_classes[0])) == MM_SUCCESS ) {
      vertex_alloc_ncomp = ncomp ;
      return TRUE ;
    }
  }

  return error_handler(VMERROR) ;
}


void vertex_pool_destroy(void)
{
  if ( mm_pool_shading ) {
#if defined(METRICS_BUILD)
    { /* Track peak memory allocated in pool. */
      size_t max_size = 0, max_frag = 0;
      int32 max_objects ;
      mm_debug_total_highest(mm_pool_shading, &max_size, &max_objects, &max_frag);
      if ( vertex_metrics.vertex_pool_max_size < max_size ) {
        vertex_metrics.vertex_pool_max_size = max_size;
        vertex_metrics.vertex_pool_max_objects = max_objects;
      }
      if ( vertex_metrics.vertex_pool_max_frag < max_frag )
        vertex_metrics.vertex_pool_max_frag = max_frag;
    }
#endif
    mm_sac_destroy(mm_pool_shading) ;
    mm_pool_destroy(mm_pool_shading) ;
    mm_pool_shading = NULL ;
  }

  vertex_alloc_ncomp = 0 ;
}


Bool vertex_alloc(SHADINGvertex **vert, int32 n)
{
  size_t size;

  HQASSERT(mm_pool_shading != NULL, "Shading pool not created") ;
  HQASSERT(n > 0, "Number of vertices to allocate not positive") ;
  HQASSERT(vertex_alloc_ncomp > 0, "Vertex alloc components corrupt") ;

  size = VERTEX_SIZE(vertex_alloc_ncomp) ;

  do {
    SHADINGvertex *result ;

    /* Doesn't matter if we bail out on error, the already allocated
       vertices will be reclaimed when the pool is destroyed */
    result = (SHADINGvertex *)mm_sac_alloc(mm_pool_shading,
                                           size, MM_ALLOC_CLASS_SHADING);
    if ( result == NULL ) {
      report_track_dl("vertex_alloc failure", size);
      return error_handler(VMERROR) ;
    }
    track_dl(size, MM_ALLOC_CLASS_SHADING, TRUE);

    result->comps = (USERVALUE *)(result + 1) ;
    result->converted = FALSE ;
    result->probeconverted = FALSE ;
    result->upwards = TRUE ;
    dlc_clear(&result->dlc);
    dlc_clear(&result->dlc_probe);

    *vert++ = result ;
  } while ( --n > 0 ) ;
  return TRUE ;
}


void vertex_free(SHADINGinfo *sinfo, SHADINGvertex **vert, int32 n)
{
  int32 size ;

  HQASSERT(mm_pool_shading != NULL, "Shading pool not created") ;
  HQASSERT(n > 0, "Number of vertices to free not positive") ;
  HQASSERT(vertex_alloc_ncomp > 0, "Vertex alloc components corrupt") ;

  size = VERTEX_SIZE(vertex_alloc_ncomp) ;

  do {
    SHADINGvertex *release = *vert++ ;

    if ( release->converted )
      dlc_release(sinfo->page->dlc_context, &release->dlc) ;
    if ( release->probeconverted )
      dlc_release(sinfo->page->dlc_context, &release->dlc_probe) ;

    mm_sac_free(mm_pool_shading, (mm_addr_t)release, size) ;
    track_dl(size, MM_ALLOC_CLASS_SHADING, FALSE);
  } while ( --n > 0 ) ;
}


/** Interpolate between vertices. Coordinates and component values are
    interpolated according to weight vector. */
void vertex_interpolate(SHADINGinfo *sinfo,
                        int32 nweights, SYSTEMVALUE *weights,
                        SHADINGvertex *overtex, SHADINGvertex **ivertex,
                        int32 ncomp)
{
  SYSTEMVALUE opacity_value = 0.0 ;
  int32 cindex, windex ;

  HQASSERT(overtex, "NULL overtex in vertex_interpolate") ;
  HQASSERT(ivertex, "NULL icolors in vertex_interpolate") ;
  HQASSERT(nweights > 0, "need at least one weight in vertex_interpolate") ;
  HQASSERT(ncomp > 0, "no components to interpolate in vertex_interpolate") ;

  if ( overtex->converted )
    dlc_release(sinfo->page->dlc_context, &overtex->dlc) ;
  if ( overtex->probeconverted )
    dlc_release(sinfo->page->dlc_context, &overtex->dlc_probe) ;

  overtex->x = overtex->y = 0.0 ;
  overtex->converted = FALSE ;
  overtex->probeconverted = FALSE ;
  overtex->upwards = FALSE ;

  for ( windex = 0 ; windex < nweights ; ++windex ) {
    overtex->x += ivertex[windex]->x * weights[windex] ;
    overtex->y += ivertex[windex]->y * weights[windex] ;
    if ( ivertex[windex]->upwards )
      overtex->upwards = TRUE ;
    opacity_value += ivertex[windex]->opacity * weights[windex];
  }
  overtex->opacity = (USERVALUE)opacity_value;

  for ( cindex = 0 ; cindex < ncomp ; ++cindex ) {
    SYSTEMVALUE value = 0.0 ;

    for ( windex = 0 ; windex < nweights ; ++windex )
      value += ivertex[windex]->comps[cindex] * weights[windex] ;

    overtex->comps[cindex] = (USERVALUE)value ;
  }
}

void init_C_globals_shadecev(void)
{
  mm_pool_shading = NULL ;
  vertex_alloc_ncomp = 0 ;
#ifdef METRICS_BUILD
  vertex_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&vertex_metrics_hook);
#endif
}

/* ----------------------------------------------------------------------------
Log stripped */
