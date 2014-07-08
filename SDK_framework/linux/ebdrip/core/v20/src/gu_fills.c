/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:gu_fills.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Main core API for adding vector (path) objects to the RIP Display List.
 */

#include "core.h"
#include "objects.h"
#include "display.h"
#include "ndisplay.h"
#include "graphics.h"
#include "routedev.h"
#include "fonts.h"
#include "namedef_.h"
#include "swpdfout.h"

#include "gu_fills.h"
#include "pathcons.h"
#include "dl_bres.h"
#include "clipops.h"
#include "gu_path.h"
#include "gstate.h"
#include "gscxfer.h"
#include "system.h"
#include "plotops.h"
#include "pathops.h"
#include "params.h"
#include "idlom.h"
#include "wclip.h"

#include "fcache.h"
#include "trap.h"

#include "vndetect.h"
#include "rcbcntrl.h"
#include "upcache.h"
#include "fontcache.h"
#include "ripdebug.h"
#include "monitor.h"
#include "metrics.h"

/**
 * Vector object has been identified as a rectangle, so create a single
 * rectangle object for addition to the DL.
 */
static Bool do_a_rect(DL_STATE *page, dbbox_t *bbox)
{
  if ( get_core_context_interp()->systemparams->ForceRectWidth &&
       gstateptr->thePDEVinfo.scanconversion == SC_RULE_TESSELATE ) {
    /*
     * Problems with XPS scan conversion rules making thin filled shapes
     * disappear.
     * We need a universal generic solution to this issue, but for now
     * just deal with the only customer use case we have, that of thin
     * horizontal rectangle fills disappearing.
     * Solution for now is to compensate for the XPS rectangle rendering
     * code which will shrink the shape by 1 pixel in both directions.
     * ( see do_render_rect(). )
     */
    bbox->x2++;
    bbox->y2++;
  }
  return DEVICE_RECT(page, bbox);
}

/**
 * Vector object has been identified as a number of rectangles, so create
 * rectangle DL object for each input rectangular path.
 */
static Bool do_multiple_rects(DL_STATE *page, PATHLIST *path, int32 nrects)
{
  PATHLIST *next;
  dbbox_t bbox;
  int32 n, rects_so_far = 0;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM(int32, nrects);
#endif

  while ( path ) {
    /* process one sub-path at a time, saving next link and restoring after */
    next = path->next;
    path->next = NULL;
    n = path_rectangles(path, FALSE, &bbox);
    path->next = next;
    path = path->next;
    if ( n == 0 ) {
      /* May have a "moveto closepath" at the end that we can ignore */
      HQASSERT(rects_so_far == nrects && path == NULL,
          "moveto myclose can only come at the end of a path");
    } else {
      HQASSERT(n == 1,"Confused processing multiple rects");
      rects_so_far += n;
      if ( !do_a_rect(page, &bbox) )
        return FALSE;
    }
  }
  HQASSERT(rects_so_far == nrects,"Confused processing multiple rects");
  return TRUE;
}

/* Empirically found to minimise collisions and spills and maximise hits in
 * the simple cache implementation. Still good with the cuckoo hash. */
#define PATH_CS_SHIFT 3

/**
 * Calculate a 'checksum' for the path, ignoring the actual coordinates.
 */
static void path_shape_checksum(PATHLIST *path, uint32 *checkptr,
                                uint32 *sizeptr)
{
  uint32 checksum = 0, size = 0;
  uint32 lies = 0; /* Folding structure sizes into the checksum is silly */
  LINELIST *theline;
  double ox, oy;

  HQASSERT(path, "No pathlist ptr");
  HQASSERT(checkptr, "No checksum ptr");

  ox = path->subpath->point.x; oy = path->subpath->point.y;
  for ( ; path; path = path->next ) {
    size += sizeof(PATHLIST);
    lies += 12;
    for ( theline = path->subpath; theline; theline = theline->next ) {
      size += sizeof(LINELIST);
      lies += 24;
      checksum ^= (uint32)theline->type; /* max UPATH_MYCLOSE = 13 */
      checksum ^= (checksum << PATH_CS_SHIFT)|(checksum >> (32-PATH_CS_SHIFT));
    }
  }
  checksum ^= lies; /* I did use size, but that's subject to change */
  checksum ^= (checksum << PATH_CS_SHIFT) | (checksum >> (32-PATH_CS_SHIFT));
  /* Zero means don't cache */
  if ( checksum == 0 )
    checksum = 1;
  *checkptr = checksum;
  if ( sizeptr )
    *sizeptr = size;
}

/**
 * Check to see if two paths are near-enough the same, within a fixed
 * translation.
 */
static Bool path_similar(PATHLIST *path1, PATHLIST *path2,
                         double *px, double *py, double epsilon)
{
  LINELIST *line1, *line2;
  double ox = 0, oy = 0;

  if (path1 && path2 && path1->subpath && path2->subpath) {
    line1 = path1->subpath;
    line2 = path2->subpath;
    ox = line1->point.x - line2->point.x;
    oy = line1->point.y - line2->point.y;
  }

  for ( ; path1 && path2; path1 = path1->next, path2 = path2->next ) {
    for ( line1 = path1->subpath , line2 = path2->subpath;
          line1 != NULL         && line2 != NULL;
          line1 = line1->next    , line2 = line2->next ) {

      if ( line1->type != line2->type ||
           fabs(line1->point.x-ox - line2->point.x) > epsilon ||
           fabs(line1->point.y-oy - line2->point.y) > epsilon )
        return FALSE;
    }
    if ( line1 || line2 )
      return FALSE;
  }
  if ( path1 || path2 )
    return FALSE;

  *px = ox;
  *py = oy;
  return TRUE;
}

#ifdef DEBUG_BUILD
enum {
  DEBUG_POLYCACHE_DISABLE = 1,
  DEBUG_POLYCACHE_PRINT = 2
} ;

int32 debug_polycache = 0;

/** Initialse polycache debug */
void init_polycache_debug(void)
{
  register_ripvar(NAME_debug_polycache, OINTEGER, &debug_polycache);
}
# define POLYGON_DEBUG if ( debug_polycache & DEBUG_POLYCACHE_PRINT ) monitorf
#else
# define POLYGON_DEBUG if (FALSE) monitorf
#endif

#define POLYGON_CACHE_BITS 9 /* Hash parameter */
#define PATH_EPSILON 1.1f    /* Coordinates appear to be ±1 */
#define SLOTS_TO_CHECK 32    /* How many collisions to accommodate. */
#define CACHE_THRESHOLD 4    /* #times shape must be seen before caching */

typedef struct POLYGON_CACHE {
  uint32   checksum; /* the shape checksum */
  uint32   size;     /* belt and braces */
  int32    usage;    /* for resolving collisions */
  PATHINFO path;     /* the reference shape */
  CHARCACHE *ccache; /* reference to entry in character cache */
  int32 eraseno;
} POLYGON_CACHE;

static POLYGON_CACHE polygon_cache[1<<POLYGON_CACHE_BITS];

#ifdef METRICS_BUILD
static struct polycache_metrics {
  int32 cache_reuse;      /* Cache hits */
  int32 cache_unique;     /* Apparently unique shapes */
  int32 cache_spills;     /* Number of reference paths discarded */
  int32 cache_size;       /* Current total of cached reference paths */
  int32 cache_peak;       /* Peak total of cached reference paths */
  int32 cache_store;      /* Number of reference paths cached */
  int32 cache_slots;      /* Number of slots used */
  int32 cache_collision;  /* Checksum collisions when no path to discard */
  int32 cache_swaps;      /* Number of cache pair swaps due to usage */
} polycache_metrics;

static void polycache_metrics_reset(int reason)
{
  struct polycache_metrics init = {0};
  UNUSED_PARAM(int, reason);
  polycache_metrics = init;
}

static Bool polycache_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Polycache")) )
    return FALSE ;
  SW_METRIC_INTEGER("cache_reuse", polycache_metrics.cache_reuse);
  SW_METRIC_INTEGER("cache_unique", polycache_metrics.cache_unique);
  SW_METRIC_INTEGER("cache_spills", polycache_metrics.cache_spills);
  SW_METRIC_INTEGER("cache_size", polycache_metrics.cache_size);
  SW_METRIC_INTEGER("cache_peak", polycache_metrics.cache_peak);
  SW_METRIC_INTEGER("cache_store", polycache_metrics.cache_store);
  SW_METRIC_INTEGER("cache_slots", polycache_metrics.cache_slots);
  SW_METRIC_INTEGER("cache_collision", polycache_metrics.cache_collision);
  SW_METRIC_INTEGER("cache_swaps", polycache_metrics.cache_swaps);
  sw_metrics_close_group(&metrics);
  return TRUE ;
}

static sw_metrics_callbacks polycache_metrics_hook = {
  polycache_metrics_update, polycache_metrics_reset, NULL
};
#define METRICS_INC(name, amount) polycache_metrics.name += amount
#else /* !METRICS_BUILD */
#define METRICS_INC(name, amount) /* nothing */
#endif /* METRICS_BUILD */

void purge_polygon_cache(int32 eraseno)
{
  int i;

  for (i = 0 ; i < (1<<POLYGON_CACHE_BITS); i++ ) {
    if ( polygon_cache[i].eraseno < eraseno ) {
      if ( polygon_cache[i].path.firstpath ) {
        path_free_list(polygon_cache[i].path.firstpath, mm_pool_temp);
        polygon_cache[i].path.firstpath = NULL;
      }
      if ( polygon_cache[i].ccache ) {
        free_ccache(polygon_cache[i].ccache);
        polygon_cache[i].ccache = NULL;
      }
      polygon_cache[i].checksum = 0;
#ifdef METRICS_BUILD
      polycache_metrics.cache_size -= polygon_cache[i].size;
#endif
    }
  }
  POLYGON_DEBUG((uint8*)"Purge polygon cache\n");
}

static Bool find_polygon_cache(POLYGON_CACHE **ppc, PATHINFO *the_path,
                               double *ox, double *oy)
{
  uint32 checksum, size, start;
  int32 check, prev = -1;

  /* Assume we won't have to translate the path */
  *ox = *oy = 0;
  *ppc = NULL ;

  /* Calculate shape checksum, and from that a cache slot index.
   * Zero means don't cache.
   */
  path_shape_checksum(the_path->firstpath, &checksum, &size);

  /* Reduce checksum to an index */
  start = checksum ^ (checksum >> (2*POLYGON_CACHE_BITS));
  start ^= (start >> POLYGON_CACHE_BITS);
  start &= ((1<<POLYGON_CACHE_BITS) - 1);

  /* Bounded cuckoo hash - check a number of consecutive slots */
  for ( check = 0; check < SLOTS_TO_CHECK; ++check, ++start ) {
    int32 index = (int32)start & ((1<<POLYGON_CACHE_BITS) - 1);

    if ( polygon_cache[index].checksum == 0 ) {
      /* Empty slot - 1st time we've seen this (particular) shape. */
      POLYGON_DEBUG((uint8 *)" index=%i  new\n", index);

      polygon_cache[index].checksum = checksum;
      polygon_cache[index].size = size;
      polygon_cache[index].usage = 1; /* nothing stored yet, so cheap */
      polygon_cache[index].ccache = NULL;

      METRICS_INC(cache_unique, 1);
      METRICS_INC(cache_slots, 1);

      return FALSE;
    } else if ( polygon_cache[index].checksum == checksum &&
                polygon_cache[index].size     == size ) {
      /* Structure matches. If the 2nd time we've seen it, copy the path.
       * Otherwise compare the path (as it may not be a match)
       */
      if ( polygon_cache[index].path.firstpath == NULL ) {
        /* 2nd time seeing this path, so copy it and pass to the caching */

        if ( polygon_cache[index].usage < CACHE_THRESHOLD ) {
          ++polygon_cache[index].usage;
          return FALSE;
        }
        POLYGON_DEBUG((uint8 *)" index=%i  store\n", index);

        if ( !path_copy(&polygon_cache[index].path, the_path, mm_pool_temp) )
          return FALSE;

        polygon_cache[index].usage = SLOTS_TO_CHECK; /* stored path has value */

        METRICS_INC(cache_store, 1);
        METRICS_INC(cache_size, size);
#ifdef METRICS_BUILD
        if ( polycache_metrics.cache_size > polycache_metrics.cache_peak )
          polycache_metrics.cache_peak = polycache_metrics.cache_size;
#endif
        *ppc = &polygon_cache[index];
        return TRUE ;
      } else {
        /* At least the 3rd time we've seen this shape. Check that the
         * current path is broadly similar, and if so pass the *stored*
         * version to the caching code. Note that there is a performance
         * improvement possible here if userpath caching were split into
         * recognition and reuse stages - we've already recognised it.
         * This also returns the path offset ox,oy if the path is similar.
         */
        if ( path_similar(the_path->firstpath,
                          polygon_cache[index].path.firstpath,
                          ox, oy, PATH_EPSILON) ) {

          POLYGON_DEBUG((uint8 *)" index=%i  similar\n", index);

          /* That's a polygon_cache hit! We must reuse the cached path because
           * this one can be numerically different. Ideally we would
           * have direct access to the cached bitmap/userpath and avoid
           * the secondary recognition step, but for now...
           */
          *the_path = polygon_cache[index].path ;

          METRICS_INC(cache_reuse, 1);

          /* Update usage and promote if necessary */
          if ( polygon_cache[index].usage < 1<<30 )
            polygon_cache[index].usage += SLOTS_TO_CHECK;

          if ( prev > -1 && polygon_cache[index].usage >
                            polygon_cache[prev].usage + SLOTS_TO_CHECK ) {
            POLYGON_CACHE swap = polygon_cache[index];
            polygon_cache[index] = polygon_cache[prev];
            polygon_cache[prev] = swap;

            METRICS_INC(cache_swaps, 1);
            POLYGON_DEBUG((uint8 *)" index=%i  swap %d\n", index, prev);
            index = prev;
          }
          *ppc = &polygon_cache[index];
          return TRUE;
        } /* if path NOT similar, continue to check slots */
      } /* if firstpath */
    } /* if == checksum */
    prev = index;
  } /* for check */

  /* Didn't find it. Decrement the usage counts and if one falls out,
   * spill it. We do this at the end so as not to penalise shape
   * clashes that happen to be earlier than a polygon_cache hit - the
   * promotion will sort that out over time anyway. */
  METRICS_INC(cache_collision, 1);

  for ( check = 0; check < SLOTS_TO_CHECK; ++check, ++start ) {
    int32 index = (int32)start & ((1<<POLYGON_CACHE_BITS) - 1);

    if ( --polygon_cache[index].usage < 1 ) {
      /* Old! Discard old path but don't store this one yet. */

      if ( polygon_cache[index].path.firstpath ) {
        POLYGON_DEBUG((uint8 *)" index=%i  SPILL2\n", index);

        path_free_list(polygon_cache[index].path.firstpath, mm_pool_temp);
        polygon_cache[index].path.firstpath = NULL;
        METRICS_INC(cache_size, -((int32)(polygon_cache[index].size)));
        METRICS_INC(cache_spills, 1);
        METRICS_INC(cache_collision, -1);
      }
      polygon_cache[index].checksum = checksum;
      polygon_cache[index].size = size;
      polygon_cache[index].usage = 1;

      METRICS_INC(cache_unique, 1);
      return FALSE;
    }
  } /* for */

  /* No match in the polygon_cache and contents still too valuable to discard */
  return FALSE;
}

/**
 * Path is definitely a general polygon rather than a rectangle, process it.
 */
static Bool dofill_poly(DL_STATE *page, PATHINFO *path, int32 type,
                        Bool polycache)
{
  NFILLOBJECT *nfill;
  POLYGON_CACHE *pc;
  double dx, dy;
  PATHINFO cachedpath = *path ;

  if ( polycache && find_polygon_cache(&pc, &cachedpath, &dx, &dy) ) {
    if ( fill_using_charcache(page, &cachedpath, type, &pc->ccache, dx, dy) &&
         pc->ccache != NULL ) {
      /* Successfully filled and blitted using a cached form. */
      pc->eraseno = page->eraseno;
      return TRUE ;
    }
    /* Failed to cache it, insufficient space or similar problem, so
       just do it the non-cached way */
  }

  return make_nfill(page, cachedpath.firstpath, NFILL_ISFILL, &nfill) &&
         DEVICE_BRESSFILL(page, type|ISFILL, nfill);
}

/**
 * Internal processing function for input vector objects.
 *
 * Check if path is made-up of one or more rectangles, if so them generate
 * rectangle DL objects, else just a normal NFILL object.
 */
static Bool dofill_internal(DL_STATE *page, PATHINFO *path, int32 type,
                            int32 nrects, dbbox_t *bbox, Bool polycache)
{
  Bool ok = TRUE, doneRects = FALSE;

  if ( !rcbn_enabled() && nrects > 0 ) {
    doneRects = TRUE;
    if ( nrects == 1 )
      ok = do_a_rect(page, bbox);
    else { /* multiple rectangles */
      Bool isOpaque =
        tsOpaque(gsTranState(gstateptr), TsStrokeAndNonStroke,
                 gstateptr->colorInfo);
      if ( isOpaque )
        ok = do_multiple_rects(page, path->firstpath, nrects);
      else /* else fall through to the code below */
        doneRects = FALSE;
    }
  }
  if ( !doneRects )
    ok = dofill_poly(page, path, type, polycache);
  return ok;
}

/**
 * Take the given path, and convert it to suitable object to add to the DL.
 *
 * This is the main entry point for adding vector objects to the RIP DL.
 */
Bool dofill(PATHINFO *path, int32 type, int32 colorType, FILL_OPTIONS options)
{
  corecontext_t *context = get_core_context_interp();
  DL_STATE *page = context->page;
  Bool do_vig = !(options & FILL_NOT_VIGNETTE);
  Bool do_vd = VD_DETECT(do_vig && char_current_context() == NULL);
  Bool do_hdlt = ((options & FILL_NO_HDLT) == 0 && isHDLTEnabled(*gstateptr));
  Bool do_pdfout = ((options & FILL_NO_PDFOUT) == 0 && pdfout_enabled());
  Bool via_vig = ((options & FILL_VIA_VIG) != 0);
  Bool copycharpath = ((options & FILL_COPYCHARPATH) != 0);
  Bool result, need_to_skip_dl = FALSE;
  int32 nrects = 0;
  dbbox_t bbox;

  HQASSERT(type==NZFILL_TYPE || type==EOFILL_TYPE, "Invalid fill type");
  HQASSERT(path, "Trying to fill with NULL path");

#ifdef DEBUG_BUILD
  if ( debug_polycache & DEBUG_POLYCACHE_DISABLE )
    options &= ~FILL_POLYCACHE ;
#endif

  if ( !path->lastline )
    return TRUE;

  if ( !path_close(MYCLOSE, path) )
      return FALSE;

  if ( !via_vig && char_doing_charpath() )
    return add_charpath(path, copycharpath);

  if ( !context->userparams->EnablePseudoErasePage )
    options |= FILL_NOT_ERASE;

  /* See if the path is a rectangle. If so and it is as large as the
   * impositionclipping, call analyze_for_forcepositive to determine if we
   * have a negative page and set up forcePositive. This is done BEFORE
   * DEVICE_SETG() since forcePositive will flip the colors into
   * a positive. */
  if ( !path->curved ) {
    nrects = path_rectangles(path->firstpath, type == NZFILL_TYPE, &bbox);
    if ( (options & FILL_NOT_ERASE) == 0 ) {
      if ( nrects == 1 && is_pagesize(page, &bbox, colorType) &&
           !is_pseudo_erasepage()) {
        if ( !gsc_analyze_for_forcepositive(context, gstateptr->colorInfo, colorType,
                                            &page->forcepositive) )
          return FALSE;

        do_vd = FALSE;
        need_to_skip_dl = TRUE;
      }
    }
  }

  if ( CURRENT_DEVICE() == DEVICE_NULL && !do_hdlt && !do_pdfout )
    return TRUE;

  if ( !via_vig ) {

    /* Note that this can change thePath(gstate) */
    if ( (options & FILL_NO_SETG) == 0 && !DEVICE_SETG(page, colorType,
                                                       DEVICE_SETG_NORMAL) )
      return FALSE;

    if ( degenerateClipping && !do_hdlt && !do_pdfout )
      return TRUE;

    fl_setflat(theFlatness(theILineStyle(gstateptr)));

    if ( !do_vd ) {
      /* Extra test of do_vig option to ignore non-painting cases.
       * All other painting cases (ufill,...) have already done flush.
       */
      if ( do_vig && !flush_vignette(VD_Default) )
        return FALSE;

      if ( do_pdfout &&
           !pdfout_dofill(context->pdfout_h, path, type, 0, colorType) )
        return FALSE;

      if ( do_hdlt ) {
        switch ( IDLOM_FILL( colorType , type , path , NULL )) {
        case NAME_false:     /* PS error in IDLOM callbacks */
          return FALSE;
        case NAME_Discard:   /* just pretending */
          return TRUE;
        default:             /* only add, for now */
          ;
        }
      }
    } else /* Doing vignette detection */
      setup_analyze_vignette();
  }
  result = TRUE;
  if ( !degenerateClipping ) {
    if ( need_to_skip_dl )
      dl_currentexflags |= RENDER_PSEUDOERASE;
    result = dofill_internal(page, path, type, nrects,
                             &bbox, (options & FILL_POLYCACHE) != 0);
    if ( need_to_skip_dl )
      dl_currentexflags &= ~RENDER_PSEUDOERASE;
  }

  if ( do_vd && !via_vig ) {
    reset_analyze_vignette();
    if ( result )
      result = analyze_vignette_f(page, path, ISFILL, type,
                                  copycharpath, colorType);
  }

  if ( !result )
    return FALSE;

  /* If we're recombining or doing imposition and above we found
   * a rectangle that is larger than the imposition or the page
   * then call dlskip_pseudo_erasepage() to adjust the imposition
   * pointers
   */
  if ( !degenerateClipping && need_to_skip_dl )
    if ( rcbn_enabled() || doing_imposition )
      if ( !dlskip_pseudo_erasepage(page) )
        return FALSE;

  return TRUE;
}

void init_C_globals_polycache()
{
  POLYGON_CACHE zero = {0};
  int32 i;

  for (i = 0 ; i < (1<<POLYGON_CACHE_BITS); i++ )
    polygon_cache[i] = zero ;
#ifdef METRICS_BUILD
  sw_metrics_register(&polycache_metrics_hook);
#endif
#ifdef DEBUG_BUILD
  debug_polycache = 0;
#endif
}

/* Log stripped */
