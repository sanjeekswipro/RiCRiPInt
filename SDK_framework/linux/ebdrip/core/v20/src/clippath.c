/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:clippath.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Clipping path capture and coalescing.
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "monitor.h"
#include "fileio.h"
#include "hqmemset.h"
#include "namedef_.h" /* NAME_* */

#include "constant.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "ndisplay.h"
#include "routedev.h"
#include "often.h"
#include "gu_path.h"
#include "gu_fills.h"
#include "gstate.h"
#include "system.h"
#include "pathcons.h"
#include "devops.h"
#include "interrupts.h"

#include "clipops.h"
#include "clippath.h"

#include "params.h"
#include "psvm.h"

#include "pathops.h"
#include "vndetect.h"
#include "debugging.h"

#include "ripdebug.h" /* register_ripvar */
#include "control.h" /* interrupt checking */

#undef CP_ROUND_ALL /* Try rounding all points to reduce FP problems? */

static Bool local_clippath(int8 PoorClippath[PCP_ELEMENTS], CLIPRECORD *dev_rec,
                           PATHINFO *outpath, int32 *cliptype);
static Bool term_clippath(const PATHINFO *inpath, PATHINFO *outpath);
static CLIPRECORD *reverse_cliprecords(CLIPRECORD *c_rec);
static Bool make_device_clippath(Bool isdegenerate, PATHINFO *outpath);
static Bool clipprepare( CLIPRECORD *c_rec, PATHINFO *topath, sbbox_t *rbbox );
static int CRT_API xlinecmp( const void *a, const void *b);
static int CRT_API ylinecmp( const void *a, const void *b);
static void fbubblesort( LINELIST *array[], int32 num,
                         int (CRT_API *compare)(const void *a, const void *b) );
static int32 setup_lines( PATHLIST *fpath, int32 ignore_horizontal,
                         LINELIST ***thelines,
                         int32 *success_ptr );
static void anyints( LINELIST **thelines, int32 theone, int32 first,
                     int32 last, SYSTEMVALUE scanline, SYSTEMVALUE *yval );
static void freeany( LINELIST **thelines, int32 start, int32 end );

static Bool point_colinear(SYSTEMVALUE x, SYSTEMVALUE y, LINELIST *line) ;

/* Constants for detecting intersection of paths */
enum { IntersectPath1 = 1,   /* Marker for path1 linelists */
       IntersectPath2 = 2,   /* Marker for path2 linelists */
       IntersectBoth = 3 } ; /* IntersectPath1|IntersectPath2 */

#define INTERSECT_TYPE -1

/* These epsilons are used to determine how close to colinear lines can be
   before they are seen as being the same. The cross epsilon is used in cross
   product testing; since this is proportional to the square of the vector
   lengths, its value is the square of the colinearity point difference
   epsilon. (The cross product represents the signed area in the triangle
   formed by two vectors; this of itself is not an indicator of colinearity,
   there must be some correspondence between the points as well).

   Why these particular values? Coordinates coming in through PostScript have
   24 bits of mantissa. At the sort of resolutions we run, the largest
   coordinates will typically be in orders of magnitude 4-5 (10000-100000).
   This means they will typically have 3-4 decimal places in the fractional
   part. If we set the epsilon just about that, we will catch colinear input
   lines as well as round-off error in the double-precision values used
   internally. This is an absolute value in device pixels, so it should be
   set to a fraction of a device pixel. Epsilon tests are dangerous overall;
   there may be strange effects from extremely shallow gradients. We could
   possibly get around some of these by rounding all of the points to
   multiples of some quantum. */
#define COLINEARITY_EPSILON 0.1
#define COLINEARITY_CROSS_EPSILON (COLINEARITY_EPSILON * COLINEARITY_EPSILON)

/* Amount of slop allowed in coalescing edges. I'll make this smaller than the
   colinearity epsilon so it (hopefully) won't interfere with the values. */
#define COALESCE_EPSILON (COLINEARITY_EPSILON * 0.5)

/* The minimum size of a scanbeam, in device pixels. Intersections will be
   forced up to this device pixel increment if there are no lines starting or
   ending closer to the current scanline. */
#define MINIMUM_SCANBEAM 0.25

/* Size of intersection zone. All intersections falling within this tolerance
   will be treated as happening at the same point. */
#define INTERSECTION_ZONE_EPSILON (MINIMUM_SCANBEAM * 0.5)

#if CP_ROUND_ALL
#include <math.h>
#define CLIPPATH_COORDINATE_QUANTUM 0.001
#define CLIPPATH_QUANTA_PER_COORDINATE (1.0/CLIPPATH_COORDINATE_QUANTUM)

#define CP_ROUND(_xy) (floor((_xy) * CLIPPATH_QUANTA_PER_COORDINATE + 0.5) * CLIPPATH_COORDINATE_QUANTUM)
#define CP_ROUND_UP(_xy) (ceil((_xy) * CLIPPATH_QUANTA_PER_COORDINATE) * CLIPPATH_COORDINATE_QUANTUM)
#else
#define CP_ROUND(_xy) (_xy)
#define CP_ROUND_UP(_xy) (_xy)
#endif

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
/*
 * Debug is controlled by bit flags.
 * Note: setting PATH can generate LOTS of output - make sure you really want this!
 */
#define CLIPPATH_DBG_TRACE            (0x000001)  /* Flow of control (clippath) */
#define CLIPPATH_DBG_TRACE_COALESCE   (0x000002)  /* Flow of control (coalesce) */
#define CLIPPATH_DBG_TRACE_SORT       (0x000004)  /* Trace sorted lines */
#define CLIPPATH_DBG_TRACE_ADD        (0x000008)  /* Trace subpath addition */
#define CLIPPATH_DBG_TRACE_INTERSECT  (0x000010)  /* Trace intersections */
#define CLIPPATH_DBG_TRACE_LINES      (0x000020)  /* Trace new and end lines */
#define CLIPPATH_DBG_TRACE_ADJUST     (0x000040)  /* Trace minimum scanbeam adjustments */
#define CLIPPATH_DBG_TRACE_BEFORE     (0x000100)  /* Dump subpaths before normalise */
#define CLIPPATH_DBG_TRACE_AFTER      (0x000200)  /* Dump subpaths after normalise */
#define CLIPPATH_DBG_STATS_CLEAR      (0x001000)  /* Clear instrumentation for each clippath */
#define CLIPPATH_DBG_STATS_SHOW       (0x002000)  /* Print instrumentation for each clippath */

#define CLIPPATH_DBG_NO_COLINEAR      (0x010000)  /* No colinear coalesce */
#define CLIPPATH_DBG_NO_MERGE         (0x040000)  /* No colinear merge */
#define CLIPPATH_DBG_NO_DEGENERATE    (0x020000)  /* No degenerate removal */

#define CLIPPATH_DBG_INOUT            (0x100000)  /* Before and after */

/*
 * Convenient macro to check for debug requested
 */
#define CLIPPATH_DBG(_f_)     ((clippath_debug&(_f_)) != 0)

/*
 * Registered RIP variable to control debug actions.
 * By default, just always produce internal subpaths, i.e. what a release
 * build does.
 */
static int32 clippath_debug = 0;

/*
 * init_clippath_debug() - registers debug only RIP var to control
 * debug from PS.
 */
void init_clippath_debug(void)
{
  /* PS controllable clippath coalescing debug */
  register_ripvar(NAME_clippath_debug, OINTEGER, (void*)&clippath_debug);

} /* Functions init_clippath_debug */

#endif /* defined( DEBUG_BUILD ) || defined( ASSERT_BUILD ) */


#if defined( DEBUG_BUILD ) /* Should put this under METRICS_BUILD */

#define CLIPPATH_STATS_INCR(_f) ++(clippath_stats._f)

/* Instrumentation for clippath */
static struct {
  int32 normalise_path, cliptoall ;
  int32 max_lines ;
  int32 scanbeams, max_scanbeam_lines, total_scanbeam_lines ;
  SYSTEMVALUE min_scanbeam, max_scanbeam ;
  int32 intersection_tests, colinear, intersections, start_intersections,
    closer_intersections, new_line_intersections, intersections_zone ;
  int32 split_path_horizontal, merge_paths_horizontal ;
  int32 split_path_vertical, merge_paths_vertical ;
  int32 coalesce_edge ;
} clippath_stats ;
#else /* !DEBUG_BUILD */
#define CLIPPATH_STATS_INCR(_f) EMPTY_STATEMENT()
#endif /* !DEBUG_BUILD */

#if defined( DEBUG_BUILD )
/*
 * dump_subpath() - write out subpath in PS form.
 */
void dump_subpath(
  LINELIST*     p_line)   /* I */
{
  OMATRIX minv ;
  Bool result ;

  HQASSERT((p_line != NULL),
           "dump_subpath: NULL subpath pointer");

  result = matrix_inverse(&thegsDevicePageCTM(*gstateptr), &minv) ;

  /* Monitorf truncates the values anyway, so we'll transform to default
     userspace for debug visualisation. */
  HQASSERT(result, "Can't invert device matrix for debugging");

  monitorf((uint8 *)"[%f %f %f %f %f %f] pathmatrix\n",
           minv.matrix[0][0], minv.matrix[0][1],
           minv.matrix[1][0], minv.matrix[1][1],
           minv.matrix[2][0], minv.matrix[2][1]) ;

  monitorf((uint8 *)"%% Sub path:\n");

  do {
    switch ( theLineType(*p_line) ) {
    case MOVETO:
      monitorf((uint8 *)"%f %f moveto\n",
               theX(thePoint(*p_line)), theY(thePoint(*p_line)));
      break;
    case LINETO:
      monitorf((uint8 *)"%f %f lineto\n",
               theX(thePoint(*p_line)), theY(thePoint(*p_line)));
      break;
    case CLOSEPATH:
      monitorf((uint8 *)"closepath\n");
      break;
    default:
      monitorf((uint8 *)"UNKNOWN\n");
      break;
    case MYCLOSE:
      break ;
    }
    p_line = p_line->next;
  } while ( p_line != NULL );
}

void dump_path(
  PATHLIST*     p_path)   /* I */
{
  HQASSERT((p_path != NULL),
           "dump_path: NULL path pointer");

  monitorf((uint8 *)"%% Path list:\n");
  dump_subpath(theSubPath(*p_path));
}

void dump_paths(
  PATHLIST*     p_path)   /* I */
{
  HQASSERT((p_path != NULL),
           "dump_paths: NULL path pointer");

  do {
    dump_path(p_path);
    p_path = p_path->next;
  } while ( p_path != NULL );
}

void dump_fullpath(
  PATHINFO*     p_info)   /* I */
{
  HQASSERT((p_info != NULL),
           "dump_fullpath: NULL path info pointer");

  monitorf((uint8 *)"%% Path:\n");
  if ( p_info->bboxtype != BBOX_NOT_SET ) {
    monitorf((uint8 *)"%f %f %f %f setbbox\n",
             p_info->bbox.x1, p_info->bbox.y1,
             p_info->bbox.x2, p_info->bbox.y2);
  } else {
    monitorf((uint8 *)"%% No BBOX:\n");
  }
  dump_paths(p_info->firstpath);
}
#endif /* DEBUG_BUILD */

#if defined( ASSERT_BUILD )

/* Check lines array is sorted according to criterion given */
static Bool check_sorted(LINELIST *array[], int32 num,
                         int (CRT_API *compare)(const void *a, const void *b) )
{
  register LINELIST **lines = array ;

  while ( --num > 0 ) {
    if ( (*compare)(lines, lines + 1) > 0 )
      return FALSE ;
  }

  return TRUE ;
}

#endif /* ASSERT_BUILD */


/* ----------------------------------------------------------------------------
   function:            clippath_()        author:              Andrew Cave
   creation date:       15-Apr-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 129.

   clippath_ works in a very similar way to rnbressfill. There are some
   optimisation tests to decide whether a clipping path can just be returned
   as it is, but the general case is to flatten each clipping path, then
   normalise its winding direction and convert it into many convex polygons
   (both done by the normalise_path routine), then to intersect these polygons
   with the polygons for the next clipping path, creating a new set of
   polygons covering the intersection of the two paths. The final path will
   be a set of disjoint convex polygons whose union covers the same area that
   a fill through the clip would cover.

---------------------------------------------------------------------------- */
static Bool make_device_clippath(Bool isdegenerate, PATHINFO *outpath)
{
  SYSTEMVALUE w , h ;
  PATHINFO path ;

  if ( isdegenerate ) {
    w = 0.0 ;
    h = 0.0 ;
  }
  else {
    if ( CURRENT_DEVICE() == DEVICE_NULL ) {
      w = 1.0 ;
      h = 1.0 ;
    }
    else {
      w = ( SYSTEMVALUE )thegsDeviceW(*gstateptr) ;
      h = ( SYSTEMVALUE )thegsDeviceH(*gstateptr) ;
    }
  }

  path_init(&path) ;
  if ( !doing_mirrorprint || isdegenerate ) {
    if (! path_add_four(&path, 0.0, 0.0, w, 0.0, w, h, 0.0, h) )
      return FALSE ;
  } else {
    /* For now, until we support mirrorprint properly (in the output stage)
     * we have to reverse the direction of the default clip when we supply it.
     * This is because some jobs append to it rather than doing a second clip.
     * The direction in which the clip goes is then important.
     */
    if (! path_add_four(&path, w, 0.0, 0.0, 0.0, 0.0, h, w, h) )
      return FALSE ;
  }

  return term_clippath(&path, outpath) ;
}

static Bool term_clippath(const PATHINFO *inpath, PATHINFO *outpath)
{
  /* Free any existing path in the output pathinfo. */
  if ( outpath->lastline )
    path_free_list(thePath(*outpath), mm_pool_temp) ;

  *outpath = *inpath ;
  outpath->bboxtype = BBOX_NOT_SET ;

#ifdef DEBUG_BUILD
  if ( CLIPPATH_DBG(CLIPPATH_DBG_INOUT) ) {
    monitorf((uint8 *)"giving:\n") ;
    debug_print_path(outpath) ;
  }
#endif

  return TRUE ;
}

static CLIPRECORD *reverse_cliprecords( CLIPRECORD *c_rec )
{
  CLIPRECORD *reversed = NULL ;

  while ( c_rec ) {
    CLIPRECORD *next = c_rec->next ;

    c_rec->next = reversed ;
    reversed = c_rec ;

    c_rec = next ;
  }

  return reversed ;
}

Bool clippath_(ps_context_t *pscontext)
{
  int32 cliptype ; /* Not used */

  return clippath_internal(ps_core_context(pscontext),
                           &thePathInfo(*gstateptr), &cliptype) ;
}

Bool clippath_internal(corecontext_t *context, PATHINFO *outpath, int32 *cliptype)
{
  Bool result ;
  CLIPRECORD *c_rec_boundary ;
  CLIPRECORD *c_rec ;
  int32 device = CURRENT_DEVICE() ;
  int8 *PoorClippath = context->systemparams->PoorClippath;

#ifdef DEBUG_BUILD
  if ( CLIPPATH_DBG(CLIPPATH_DBG_STATS_CLEAR) ) {
    HqMemZero(&clippath_stats, sizeof(clippath_stats)) ;
  }
#endif

  /* If we're using the device boundary (SystemParams.PoorClippath[0])
     then for imposition clips, we need to add them to the normal clip.
     Do this by temporarily adding the imposition clips to the end of the
     normal clips. */
  c_rec_boundary = NULL ;
  if ( PoorClippath[0] &&
       device != DEVICE_NULL &&
       device != DEVICE_CHAR &&
       device != DEVICE_PATTERN1 &&
       device != DEVICE_PATTERN2 ) {
    for ( c_rec = theClipRecord(thegsPageClip(*gstateptr)) ;
          c_rec ;
          c_rec = c_rec->next) {
      if ( c_rec->next == NULL ) {
        c_rec->next = theClipRecord(impositionclipping) ;
        c_rec_boundary = c_rec ;
        break ;
      }
    }
  }

  /* If we only have a device clip record in the gstate, but we have an
     imposition clipping chain, generate the clippath using the imposition
     clipping chain. The gstate device clip record was generated from the
     bounds created by the imposition chain in the first place, so skipping
     the gstate device record won't miss any information we don't already
     have. This allows the device record elimination below to return just the
     imposition clip path, as would have been done immediately before the
     imposition took effect. */
  c_rec = theClipRecord(thegsPageClip(*gstateptr)) ;
  HQASSERT(c_rec != NULL, "No gstate clip record") ;
  if ( c_rec->next == NULL && theClipRecord(impositionclipping) != NULL )
    c_rec = theClipRecord(impositionclipping) ;

  /* We reverse the clips in place because we need to deal with them in the
     order that they were created rather than in the (reverse) order in which
     they are linked into the gstate (et al). */
  c_rec = reverse_cliprecords(c_rec) ;
  result = local_clippath(PoorClippath, (PoorClippath[0] || c_rec->next == NULL)
                          ? c_rec : c_rec->next,
                          outpath, cliptype) ;
  /* Reverse them back */
  c_rec = reverse_cliprecords(c_rec) ;

  /* If we used the device boundary (c_rec_boundary non-null from first piece
     of code) then we need to clear the end of the normal clip back to a
     null, taking off the imposition clips. */
  if ( c_rec_boundary ) {
    HQASSERT( PoorClippath[0] ,
              "confused use of c_rec-boundary without poor clippath" ) ;
    c_rec_boundary->next = NULL ;
  }

#ifdef DEBUG_BUILD
  if ( CLIPPATH_DBG(CLIPPATH_DBG_STATS_SHOW) ) {
    monitorf((uint8 *)"clippath_stats:");
    monitorf((uint8 *)" cliptoall %d\n", clippath_stats.cliptoall);
    monitorf((uint8 *)" normalise_path %d\n", clippath_stats.normalise_path);
    monitorf((uint8 *)" max_lines %d\n", clippath_stats.max_lines);
    monitorf((uint8 *)" scanbeams %d\n", clippath_stats.scanbeams);
    monitorf((uint8 *)" max_scanbeam_lines %d\n", clippath_stats.max_scanbeam_lines);
    monitorf((uint8 *)" total_scanbeam_lines %d\n", clippath_stats.total_scanbeam_lines);
    monitorf((uint8 *)" min_scanbeam %f\n", clippath_stats.min_scanbeam);
    monitorf((uint8 *)" max_scanbeam %f\n", clippath_stats.max_scanbeam);
    monitorf((uint8 *)" intersection_tests %d\n", clippath_stats.intersection_tests);
    monitorf((uint8 *)" colinear %d\n", clippath_stats.colinear);
    monitorf((uint8 *)" intersections %d\n", clippath_stats.intersections);
    monitorf((uint8 *)" start_intersections %d\n", clippath_stats.start_intersections);
    monitorf((uint8 *)" closer_intersections %d\n", clippath_stats.closer_intersections);
    monitorf((uint8 *)" new_line_intersections %d\n", clippath_stats.new_line_intersections);
    monitorf((uint8 *)" intersections_zone %d\n", clippath_stats.intersections_zone);
    monitorf((uint8 *)" split_path_horizontal %d\n", clippath_stats.split_path_horizontal);
    monitorf((uint8 *)" merge_paths_horizontal %d\n", clippath_stats.merge_paths_horizontal);
    monitorf((uint8 *)" split_path_vertical %d\n", clippath_stats.split_path_vertical);
    monitorf((uint8 *)" merge_paths_vertical %d\n", clippath_stats.merge_paths_vertical);
    monitorf((uint8 *)" coalesce_edge %d\n", clippath_stats.coalesce_edge);
  }
#endif

  return ( result ) ;
}

static Bool local_clippath(int8 PoorClippath[PCP_ELEMENTS], CLIPRECORD *c_rec,
                           PATHINFO *outpath, int32 *cliptype)
{
  Bool gotrect ;
  Bool done_clipprepare = FALSE ;
  CLIPRECORD  f_rec ;
  CLIPRECORD *t_rec ;
  CLIPRECORD *n_rec ;
  CLIPRECORD *gs_rec ;
  PATHINFO rpath ;
  sbbox_t bbox, rbbox ;

  HQASSERT(c_rec != NULL, "No clippath record") ;

  path_init( & rpath ) ;

  gs_rec = theClipRecord(thegsPageClip(*gstateptr)) ;
  HQASSERT(gs_rec, "No gstate clip record") ;

#ifdef DEBUG_BUILD
  if ( CLIPPATH_DBG(CLIPPATH_DBG_INOUT) ) {
    CLIPRECORD *d_rec ;

    for ( d_rec = c_rec ; d_rec != NULL ; d_rec = d_rec->next ) {
      monitorf((uint8 *)"%s %s",
               (d_rec == c_rec ? "Clippath combines:" :
                d_rec == c_rec->next ? "with" : "and"),
               (theClipType(*d_rec) & CLIPRULE) == NZFILL_TYPE ? "NZ" : "EO") ;
      if ( theClipType(*d_rec) & CLIPISDEGN )
        monitorf((uint8 *)",DEGENERATE") ;
      if ( theClipType(*d_rec) & CLIPISRECT )
        monitorf((uint8 *)",RECTANGLE") ;
      if ( theClipType(*d_rec) & CLIPNORMALISED )
        monitorf((uint8 *)",NORMALISED") ;
      if ( theClipType(*d_rec) & CLIPINVERT )
        monitorf((uint8 *)",INVERTED") ;
      monitorf((uint8 *)"\n") ;
      debug_print_path(&theClipPath(*d_rec)) ;
    }
  }
#endif

  /* N.B. Degenerate test uses top clip record, not c_rec. */
  if ( theClipType(*gs_rec) & CLIPISDEGN ) {
    HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("clippath degenerate; return %s path",theISaveLangLevel(workingsave)==1?"empty":"degenerate")) ;
    *cliptype = NZFILL_TYPE | CLIPISRECT | CLIPISDEGN ;
    return (theISaveLangLevel(workingsave) == 1 ?
            term_clippath(&rpath, outpath) : /* Replace outpath with null path */
            make_device_clippath(TRUE, outpath)) ;
  }

  /* N.B. Normalised test uses top clip record, not c_rec. */
  if ( (theClipType(*gs_rec) & CLIPNORMALISED) != 0 &&
       PoorClippath[8] ) {
    /* If the top clip record was normalised early, and this setting is true,
       return the normalised clip record, regardless of the current settings
       of PoorClippath. */
    if ( ! path_copy(&rpath, &theClipPath(*gs_rec), mm_pool_temp) )
      return FALSE ;
    *cliptype = theClipType(*gs_rec) ;
    return term_clippath(&rpath, outpath) ;
  }

  /* Initialise rbbox for (rare) case that iclip doesn't have a previous
     rect. We use the device bounds because the values need to be
     transformable back to sensible userspace floats, and these values
     shouldn't overflow when we do that. */
  bbox_store(&rbbox, 0, 0, thegsDeviceW(*gstateptr), thegsDeviceH(*gstateptr)) ;

  n_rec = c_rec->next ;
  /* Look at if we've got enclosing rects (first in strict order) */
  if ( PoorClippath[1] ) {
    while ( c_rec &&
            n_rec &&
            (theClipType(*c_rec) & CLIPISRECT) != 0 &&
            (path_bbox(&theClipPath(*c_rec), &rbbox, BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD),
             path_bbox(&theClipPath(*n_rec), &bbox, BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD),
             bbox_contains(&rbbox, &bbox))
            ) {
      HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("skip head (enclosing) rectangle clip")) ;
      c_rec = n_rec ;
      n_rec = c_rec->next ;
    }

    /* Look at if we've got enclosing rects (out of strict order) */
    if ( ! PoorClippath[2] ) {
      while ( c_rec &&
              n_rec &&
              (theClipType(*n_rec) & CLIPISRECT) != 0 &&
              (path_bbox(&theClipPath(*n_rec), &bbox, BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD),
               bbox_contains(&bbox, &rbbox))
              ) {
        HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("skip tail (enclosing) rectangle clip")) ;
        n_rec = n_rec->next ;
      }

      if ( c_rec != ( & f_rec )) {
        f_rec = (*c_rec) ;
        c_rec = ( & f_rec ) ;
      }
      c_rec->next = n_rec ;
    }
  }

  /* If only one rec, then don't decompose it if don't need to. If
     inverted clipping, then we always need to decompose. */
  if ( ! n_rec && (theClipType(*c_rec) & CLIPINVERT) == 0 ) {
    PATHLIST *tpath = thePath(theClipPath(*c_rec)) ;
    if ( (theClipType(*c_rec) & CLIPISRECT) ||
         (tpath->next == NULL &&
          (((theClipType(*c_rec) & CLIPRULE) == NZFILL_TYPE &&
            ! PoorClippath[3]) ||
           ((theClipType(*c_rec) & CLIPRULE) == EOFILL_TYPE &&
            ! PoorClippath[5]))) ||
         (tpath->next != NULL &&
          (((theClipType(*c_rec) & CLIPRULE) == NZFILL_TYPE &&
            ! PoorClippath[4]) ||
           ((theClipType(*c_rec) & CLIPRULE) == EOFILL_TYPE &&
            ! PoorClippath[6]))) ) {
      HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE) ,
              ("return single %sclip %s subpath",
               (theClipType(*c_rec) & CLIPRULE) == NZFILL_TYPE ? "" : "eo",
               tpath->next == NULL ? "single" : "multiple")) ;
      if ( ! path_copy(&rpath, &theClipPath(*c_rec), mm_pool_temp) )
        return FALSE ;
      *cliptype = theClipType(*c_rec) ;
      return term_clippath(&rpath, outpath) ;
    }
  }

  gotrect = FALSE ;

  /* To optimise clippath, we reduce all rects first to a path of their bbox */
  for ( t_rec = c_rec ; t_rec ; t_rec = t_rec->next)
    if ( theClipType(*t_rec) & CLIPISRECT ) {
      if ( gotrect ) {
        path_bbox( &theClipPath(*t_rec) , & bbox , BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD ) ;
        bbox_intersection(&rbbox, &bbox, &rbbox) ;
      } else {
        path_bbox( &theClipPath(*t_rec) , &rbbox , BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD ) ;
        gotrect = TRUE ;
      }
    }

/* First path */
  if ( gotrect ) {
    HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("eliminate rects")) ;

    /* now test for only one non-rect clip, completely enclosed. */
    if ( PoorClippath[1] )
      for ( t_rec = c_rec ; t_rec ; t_rec = t_rec->next)
        if ( 0 == (theClipType(*t_rec) & CLIPISRECT) ) {
          CLIPRECORD *x_rec;
          x_rec = t_rec;
          path_bbox( &theClipPath(*x_rec) , & bbox , BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD ) ;
          if ( bbox_contains(&rbbox, &bbox) ) {
            /* then this path is completely within intersection of all rects */
            for ( t_rec = x_rec->next ; t_rec ; t_rec = t_rec->next)
              /* another non-rectangle? cannot exit cleanly */
              if ( 0 == (theClipType(*t_rec) & CLIPISRECT) )
                break;
            if ( NULL == t_rec && (theClipType(*x_rec) & CLIPINVERT) == 0 ) {
              /* did not find another non-rectangle, so can return only this
                 path, if permitted by decomposition control variables. */
              PATHLIST *tpath = thePath(theClipPath(*x_rec)) ;
              if ( (tpath->next == NULL &&
                    (((theClipType(*x_rec) & CLIPRULE) == NZFILL_TYPE &&
                      ! PoorClippath[3]) ||
                     ((theClipType(*x_rec) & CLIPRULE) == EOFILL_TYPE &&
                      ! PoorClippath[5]))) ||
                   (tpath->next != NULL &&
                    (((theClipType(*x_rec) & CLIPRULE) == NZFILL_TYPE &&
                      ! PoorClippath[4]) ||
                     ((theClipType(*x_rec) & CLIPRULE) == EOFILL_TYPE &&
                      ! PoorClippath[6]))) ) {
                if ( ! path_copy(&rpath, &theClipPath(*x_rec), mm_pool_temp) )
                  return FALSE ;
                *cliptype = theClipType(*x_rec) ;
                return term_clippath(&rpath, outpath) ;
              }
            }
          }
          break; /* the outer for loop, found a non-rect, but no good */
        }

#if CP_ROUND_ALL
    rbbox.x1 = CP_ROUND(rbbox.x1) ;
    rbbox.y1 = CP_ROUND(rbbox.y1) ;
    rbbox.x2 = CP_ROUND(rbbox.x2) ;
    rbbox.y2 = CP_ROUND(rbbox.y2) ;
#endif
    if (! path_add_four(&rpath, rbbox.x1, rbbox.y1,
                        rbbox.x2, rbbox.y1,
                        rbbox.x2, rbbox.y2,
                        rbbox.x1, rbbox.y2) )
      return ( FALSE ) ;
    /* Need to reverse the clippath if mirrorprint is on */
    if ( doing_mirrorprint ) {
      path_reverse_linelists( thePath( rpath ) , & rpath.lastline) ;
    }
  }
  else {
    HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("decompose first clip")) ;
    if ( ! clipprepare(c_rec, &rpath, &rbbox) )
      return FALSE ;

    done_clipprepare = TRUE ;
    if ( ! thePath(rpath) ) {
      HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE), ("clippath degenerate; return %s path",theISaveLangLevel(workingsave)==1?"empty":"degenerate")) ;
      *cliptype = NZFILL_TYPE | CLIPISRECT | CLIPISDEGN ;
      return (theISaveLangLevel(workingsave) == 1 ?
              term_clippath(&rpath, outpath) : /* Replace outpath with null path */
              make_device_clippath(TRUE, outpath)) ;
    }
    c_rec = n_rec ;
  }

/* All the other paths. */
  while ( c_rec ) {
    PATHINFO npath ;
    sbbox_t tbbox = { 0.0 , 0.0 , 0.0 , 0.0 } ;
    uint8 protect;

    protect = rpath.protection; /* protect getting outline in bits */

    if ( ! (theClipType(*c_rec) & CLIPISRECT )) {
      PATHLIST *tpath = thePath(theClipPath(*c_rec)) ;

      if ( theClipPath(*c_rec).protection ) {
        if ( ! protect )
          /* assign if not already protected */
          protect = theClipPath(*c_rec).protection;
        else if ( protect != theClipPath(*c_rec).protection )
          /* if already protected differently, blanket protection */
          protect = PROTECTED_BLANKET;
      }

      if (( gotrect ) && ( tpath->next == NULL )) {
        HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("calculate single (sub)path bbox")) ;
        path_bbox( &theClipPath(*c_rec) , & tbbox , BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD ) ;
      }
      HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("decompose %s clip",gotrect?"first":"subsequent")) ;
      if ( ! clipprepare(c_rec, &npath, &rbbox) ) {
        path_free_list(thePath(rpath), mm_pool_temp) ;
        return FALSE ;
      }
      done_clipprepare = TRUE ;

      if ( !interrupts_clear(allow_interrupt) ) {
        path_free_list(thePath(rpath), mm_pool_temp) ;
        return report_interrupt(allow_interrupt);
      }

      if ( ! thePath(npath) ) {
        path_free_list( thePath(rpath), mm_pool_temp ) ;
        HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE), ("clippath degenerate; return %s path",theISaveLangLevel(workingsave)==1?"empty":"degenerate")) ;
        *cliptype = NZFILL_TYPE | CLIPISRECT | CLIPISDEGN ;
        return (theISaveLangLevel(workingsave) == 1 ?
                term_clippath(&npath, outpath) : /* Replace outpath with null path */
                make_device_clippath(TRUE, outpath)) ;
      }

      if (( gotrect ) && ( tpath->next != NULL )) {
        HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("calculate single (mul)path bbox")) ;
        path_bbox( &npath , & tbbox , BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD ) ;
      }

      if ( gotrect && bbox_contains(&rbbox, &tbbox) ) {
        HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("skip cliptoall; fully enclosed by all rectangle clips")) ;
        gotrect = FALSE ;
        path_free_list( thePath(rpath), mm_pool_temp ) ;        /* npath is new baseline */
        rpath = npath ;
      }
      else {
        gotrect = FALSE ;
/* Filter each set of clip paths through each other. */
/* NULL returned implies either NO intersection or error occured. */
        if ( ! cliptoall(&rpath, &npath, &rpath, TRUE) )
          return FALSE ;

        if ( ! thePath(rpath) ) {
          HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE), ("clippath degenerate; return %s path",theISaveLangLevel(workingsave)==1?"empty":"degenerate")) ;
          *cliptype = NZFILL_TYPE | CLIPISRECT | CLIPISDEGN ;
          return (theISaveLangLevel(workingsave) == 1 ?
                  term_clippath(&rpath, outpath) : /* Replace outpath with null path */
                  make_device_clippath(TRUE, outpath)) ;
        }
      }
      if ( protect )
        rpath.protection = protect;
    }
    else {
      HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("skip rect clip")) ;
    }
    c_rec = c_rec->next ;
  }

  HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) && gotrect , ("return intersected rect clips")) ;

  /* Any complex clippath will have to be reversed if mirrorprint is on */

  if ( done_clipprepare && doing_mirrorprint ) {
    path_reverse_linelists( thePath( rpath ) , & rpath.lastline) ;
  }

  *cliptype = NZFILL_TYPE ;
  return term_clippath(&rpath, outpath) ;
}

/* This implementation of cliptoall uses the winding rules to and
   normalise_path (nee makeconcave) to only include areas with winding number
   -2. The input paths must have been normalised before calling this
   function. The input paths are stolen by this function. */
Bool cliptoall(PATHINFO *path1, PATHINFO *path2, PATHINFO *topath,
               Bool no_degenerates)
{
  PATHINFO jpath ;
  PATHLIST *pathlist ;

  HQASSERT(path1, "Missing clip pathinfo") ;
  HQASSERT(path2, "Missing clipped pathinfo") ;
  HQASSERT(topath, "Missing result pathinfo") ;

  CLIPPATH_STATS_INCR(cliptoall) ;

  /* Mark different paths with numbers, so they can be detected by infill. */
  for ( pathlist = path1->firstpath ; pathlist ; pathlist = pathlist->next ) {
    theLineOrder(*pathlist) = IntersectPath1 ;
  }

  for ( pathlist = path2->firstpath ; pathlist ; pathlist = pathlist->next ) {
    theLineOrder(*pathlist) = IntersectPath2 ;
  }

  /* Join input paths into single path */
  jpath = *path1 ;
  jpath.lastpath->next = path2->firstpath ;
  jpath.lastpath = path2->lastpath ;
  jpath.lastline = path2->lastline ;

  path_init(path1) ;
  path_init(path2) ;
  *topath = jpath ;

  return normalise_path(topath, INTERSECT_TYPE, FALSE, no_degenerates) ;
}

/* ----------------------------------------------------------------------------
   function:            clipprepare(..)    author:              Andrew Cave
   creation date:       15-Apr-1987        last modification:   ##-###-####
   arguments:           ...
   description:

   Prepares the given path to be intersected by another path, (by dividing
   the path into a sequence of non-intersecting concave polygons).

---------------------------------------------------------------------------- */
static Bool clipprepare(CLIPRECORD *c_rec, PATHINFO *topath, sbbox_t *rbbox)
{
  PATHLIST *apath ;
  GSTATE *gs ;
  USERVALUE flat ;
  sbbox_t bbox ;

  HQASSERT( topath, "Pathinfo pointer NULL in clipprepare") ;

  gs = gstateptr ;

  flat = theClipFlat(*c_rec) ;
  if ( flat == 0.0f ) {
    flat = theFlatness(theLineStyle(*gs)) ;
    theClipFlat(*c_rec) = flat ;
  }
  fl_setflat( flat ) ;

  path_init(topath) ;
  for ( apath = thePath(theClipPath(*c_rec)) ; apath ; apath = apath->next) {
    path_bbox_subpath( apath, & bbox, BBOX_IGNORE_ALL ) ;
    if ( ! (bbox.x1 >= theXd2Clip(thegsPageClip(*gs)) ||
            bbox.y1 >= theYd2Clip(thegsPageClip(*gs)) ||
            bbox.x2 <= theXd1Clip(thegsPageClip(*gs)) ||
            bbox.y2 <= theYd1Clip(thegsPageClip(*gs))) ) {
      PATHLIST temppath ;
      PATHINFO tempinfo ;
      HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("deal complete sub-path; intersects total bbox")) ;
      theSubPath( temppath ) = theSubPath(*apath) ;
      temppath.next = NULL ;
      theISystemAlloc( &temppath ) = PATHTYPE_STACK ;
      /* The call to doflat here may not be necessary, because the subpath
         may already be flattened, but we don't keep flattening information
         in this granularity, and it also copies the path ready for
         append_subpath to patch into the destination path. */
      if ( ! path_flatten(&temppath, &tempinfo) ) {
        path_free_list( topath->firstpath, mm_pool_temp ) ;
        return FALSE ;
      }
      if ( nooflines > 0 )
        path_append_subpath(thePath(tempinfo), tempinfo.lastline, topath) ;
    }
    else {
      HQTRACE( CLIPPATH_DBG(CLIPPATH_DBG_TRACE) , ("skip complete sub-path; out off total bbox")) ;
    }
  }

  if ( (theClipType(*c_rec) & CLIPINVERT) != 0 ) {
    SYSTEMVALUE x1, x2, y1, y2 ;

    /* It is tricky to create a correct clipping path for iclip, because it
       is defined as an inverted NZFILL. If it were an EOFILL, it would be
       easy. A hard case is an iclip with two overlapping circles, one
       clockwise and the other anti-clockwise. To get this right, we have to
       do normalise_path to get clockwise orientation of the NZFILL interior
       (not what we want), add an anticlockwise path around the clipping
       boundary, and do normalise_path again. This code will work regardless of
       whether the iclip is defined as NZFILL or EOFILL. */
    if ( !normalise_path(topath, theClipType(*c_rec) & CLIPRULE, TRUE, TRUE) )
      return FALSE ;

    HQASSERT(rbbox, "No bbox argument") ;

    /* Can't use theIX1Clip(c_rec) et. al., because they're rounded to
       integer. Use enclosing rectangle box passed in to us. */
    bbox_load(rbbox, x1, y1, x2, y2) ;

    if ( !path_add_four(topath, x1, y1, x1, y2, x2, y2, x2, y1) )
      return FALSE ;
  }

  return normalise_path(topath, theClipType(*c_rec) & CLIPRULE, TRUE, TRUE) ;
}

/* Sort comparison functions. These functions are used by qsort() and
   fbubblesort() to compare line segments. They are used at different phases
   of path normalisation: ylinecmp is used first to Y-sort the lines.
   xlinecmp is used on each scanline to x-sort and gradient sub-sort the
   lines intersecting that scanline; it has special handling for horizontals,
   in case we are normalising for infill. */
static int CRT_API ylinecmp(const void *a, const void *b)
{
  register LINELIST *l1 = *(LINELIST **)a, *l2 = *(LINELIST **)b ;
  register SYSTEMVALUE dy = theY(thePoint(*l1)) - theY(thePoint(*l2)) ;

  SwOftenUnsafe();

  if ( dy > 0 )
    return 1 ;

  if ( dy < 0 )
    return -1 ;

  return 0 ;
}

static int CRT_API xlinecmp(const void *a, const void *b)
{
  register LINELIST *l1 = *(LINELIST **)a, *l2 = *(LINELIST **)b ;
  register SYSTEMVALUE dx, px, py, nx ;
  int32 orient ;

  SwOftenUnsafe();

  HQASSERT(theY(thePoint(*l1)) == theY(thePoint(*l2)),
           "lines do not start at same Y") ;

  px = theX(thePoint(*l1)) ;
  nx = theX(thePoint(*l2)) ;

  dx = px - nx ;

  if ( dx > 0 )
    return 1 ;

  if ( dx < 0 )
    return -1 ;

  /* If the x-coordinates are equal, then the lines are sorted by gradient.
     The lines are sorted into increasing gradient; the first line will be
     the one sloping most to the left, and the last will slope most to the
     right. This means that the first an last lines in the sorted list are on
     the outside of the shape.

     Horizontals are sorted specially; they are sorted so that they are after
     lines of other gradients (this happens automatically by the cross-product
     test). */

  orient = LINELIST_ORIENT(l1) - LINELIST_ORIENT(l2) ;

  py = theY(thePoint(*l1)) ;
  l1 = l1->next ;
  l2 = l2->next ;

  /* If we have not already decided this is colinear, test the gradient. */
  if ( l1->next != l2->next ) {
    dx = ((theX(thePoint(*l2)) - nx) * (py - theY(thePoint(*l1))) -
          (theY(thePoint(*l2)) - py) * (px - theX(thePoint(*l1)))) ;

    if ( dx > 0 )
      return 1 ;

    if ( dx < 0 )
      return -1 ;
  }

  /* Colinear lines are sub-sorted by orientation; this ensures that
     normalised paths which are abutting do not create intersections. */
  return orient ;
}

/* ----------------------------------------------------------------------------
   function:            fbubblesort( .. )     author:              Andrew Cave
   creation date:       20-Apr-1987           last modification:   ##-###-####
   arguments:           array , num , extract .
   description:

  This function does a simple bidirectional bubble sort on the given field.

---------------------------------------------------------------------------- */
static void fbubblesort( LINELIST *array[], int32 num,
                         int (CRT_API *compare)(const void *a, const void *b) )
{
  int32 flag ;
  register LINELIST **top , **bottom , **loop , **prev ;

  bottom = array ;
  top = array + ( num - 1 ) ;

  while ( bottom < top ) {

    SwOftenUnsafe() ;

    flag = TRUE ;
    loop = bottom ;
    while ( loop < top ) {
      prev = loop ;
      ++loop ;
      if ( (*compare)(prev, loop) > 0 ) {
        flag = FALSE ;
        { LINELIST *temp ;
          temp = (*prev) ; (*prev) = (*loop) ; (*loop) = temp ;
        }
      }
    }
    if ( flag )
      return ;
    --top ;
    flag = TRUE ;
    loop = top ;
    while ( loop > bottom ) {
      prev = loop ;
      --loop ;
      if ( (*compare)(loop, prev) > 0 ) {
        flag = FALSE ;
        { LINELIST *temp ;
          temp = (*prev) ; (*prev) = (*loop) ; (*loop) = temp ;
        }
      }
    }
    if ( flag )
      return ;
    ++bottom ;
  }
}

/* The code below makes extensive use of cyclic pathlists. This makes it
   much easier to ensure that the start and end of paths do not appear inside
   internal paths, and avoids many special cases to do with MOVETO/CLOSEPATH.
   The following support functions are used to add cyclic subpaths and convert
   between cyclic and non-cyclic subpaths. */

/* Split cyclic paths, adding MOVETO/CLOSEPATH pair if the lastline pointer
   is not NULL. If the lastline pointer is NULL, split so that path_free_list()
   will succeed and return FALSE. */
static Bool remove_path_cycles(PATHLIST *paths, LINELIST **lastline)
{
  while ( paths ) {
    LINELIST *line ;

    SwOftenUnsafe() ;

    line = theSubPath(*paths) ;
    HQASSERT(line, "No lines in subpath") ;

    HQASSERT(theLineType(*line) == LINETO || theLineType(*line) == MOVETO,
             "Unexpected line type") ;

    if ( theLineType(*line) == LINETO ) { /* This is a cyclic path */
      theSubPath(*paths) = line->next ;

      if ( lastline != NULL ) {
        LINELIST *next = get_line(mm_pool_temp) ;

        if ( !next )
          return FALSE ;

        line->next = next ;

        line = theSubPath(*paths) ;
        theLineType(*line) = MOVETO ;

        next->next = NULL ;
        theLineType(*next) = CLOSEPATH ;
        thePoint(*next) = thePoint(*line) ;

        *lastline = next ;
      } else /* Splitting so that path_free_list() will work. */
        line->next = NULL ;
    }

    paths = paths->next ;
  }

  return (lastline != NULL) ;
}

/* Add a cyclic quadrilateral */
static Bool add_four_cyclic(PATHLIST **ppath, LINELIST **lastline,
                             SYSTEMVALUE x1, SYSTEMVALUE y1,
                             SYSTEMVALUE x2, SYSTEMVALUE y2,
                             SYSTEMVALUE x3, SYSTEMVALUE y3,
                             SYSTEMVALUE x4, SYSTEMVALUE y4)
{
  PATHLIST *path ;
  LINELIST *line ;

  HQASSERT(ppath, "Nowhere to put new path") ;
  HQASSERT(lastline, "Nowhere to put last line") ;

  if ( (path = get_path(mm_pool_temp)) == NULL )
    return FALSE ;

  path->next = NULL ;
  if ( (line = theSubPath(*path) = get_line(mm_pool_temp)) != NULL ) {
    theX(thePoint(*line)) = x1 ;
    theY(thePoint(*line)) = y1 ;
    theLineType(*line) = LINETO ;

    if ( (line = line->next = get_line(mm_pool_temp)) != NULL ) {
      theX(thePoint(*line)) = x2 ;
      theY(thePoint(*line)) = y2 ;
      theLineType(*line) = LINETO ;

      if ( (line = line->next = get_line(mm_pool_temp)) != NULL ) {
        theX(thePoint(*line)) = x3 ;
        theY(thePoint(*line)) = y3 ;
        theLineType(*line) = LINETO ;

        if ( (line = line->next = get_line(mm_pool_temp)) != NULL ) {
          theX(thePoint(*line)) = x4 ;
          theY(thePoint(*line)) = y4 ;
          theLineType(*line) = LINETO ;

          /* Join back to start */
          line->next = theSubPath(*path) ;

          *ppath = path ;
          *lastline = line ;

          return TRUE ;
        }
      }
    }
  }

  path_free_list(path, mm_pool_temp) ;

  return FALSE ;
}

/* Add a cyclic triangle */
static Bool add_three_cyclic(PATHLIST **ppath, LINELIST **lastline,
                             SYSTEMVALUE x1, SYSTEMVALUE y1,
                             SYSTEMVALUE x2, SYSTEMVALUE y2,
                             SYSTEMVALUE x3, SYSTEMVALUE y3)
{
  PATHLIST *path ;
  LINELIST *line ;

  HQASSERT(ppath, "Nowhere to put new path") ;
  HQASSERT(lastline, "Nowhere to put last line") ;

  if ( (path = get_path(mm_pool_temp)) == NULL )
    return FALSE ;

  path->next = NULL ;
  if ( (line = theSubPath(*path) = get_line(mm_pool_temp)) != NULL ) {
    theX(thePoint(*line)) = x1 ;
    theY(thePoint(*line)) = y1 ;
    theLineType(*line) = LINETO ;

    if ( (line = line->next = get_line(mm_pool_temp)) != NULL ) {
      theX(thePoint(*line)) = x2 ;
      theY(thePoint(*line)) = y2 ;
      theLineType(*line) = LINETO ;

      if ( (line = line->next = get_line(mm_pool_temp)) != NULL ) {
        theX(thePoint(*line)) = x3 ;
        theY(thePoint(*line)) = y3 ;
        theLineType(*line) = LINETO ;

        /* Join back to start */
        line->next = theSubPath(*path) ;

        *ppath = path ;
        *lastline = line ;

        return TRUE ;
      }
    }
  }

  path_free_list(path, mm_pool_temp) ;

  return FALSE ;
}

/* Add a cyclic line */
static Bool add_two_cyclic(PATHLIST **ppath, LINELIST **lastline,
                           SYSTEMVALUE x1, SYSTEMVALUE y1,
                           SYSTEMVALUE x2, SYSTEMVALUE y2)
{
  PATHLIST *path ;
  LINELIST *line ;

  HQASSERT(ppath, "Nowhere to put new path") ;
  HQASSERT(lastline, "Nowhere to put last line") ;

  if ( (path = get_path(mm_pool_temp)) == NULL )
    return FALSE ;

  path->next = NULL ;
  if ( (line = theSubPath(*path) = get_line(mm_pool_temp)) != NULL ) {
    theX(thePoint(*line)) = x1 ;
    theY(thePoint(*line)) = y1 ;
    theLineType(*line) = LINETO ;

    if ( (line = line->next = get_line(mm_pool_temp)) != NULL ) {
      theX(thePoint(*line)) = x2 ;
      theY(thePoint(*line)) = y2 ;
      theLineType(*line) = LINETO ;

      /* Join back to start */
      line->next = theSubPath(*path) ;

      *ppath = path ;
      *lastline = line ;

      return TRUE ;
    }
  }

  path_free_list(path, mm_pool_temp) ;

  return FALSE ;
}

/* ----------------------------------------------------------------------------
   function:            normalise_path(..)    author:              Andrew Cave
   creation date:       15-Apr-1987        last modification:   ##-###-####
   arguments:           fpath , therule, success_ptr .
   description:

   Produces a sequence on non-intersecting sub-paths whose union is the
   original path. Similar to rnbressfill; the paths are first flattened and
   converted to a linelist (similar to DL threads). These are sorted, and a
   scanline variable used to track the start of new threads, end of old ones,
   and intersections between them. The edges of the areas filled according to
   the winding rule are used to create a set of triangles and trapeziods, all
   with horizontal base or tops.

   An active edge list is kept, marking which horizontal spans are currently
   in the final shape; this is used to coalesce subsequent shapes into existing
   paths. This process is simplified by keeping the paths as cyclic lists, and
   a look-ahead in the orientation testing to check if the current span can be
   extended to the maximum extent.

   When merging and splitting paths, we leave the subpath pointer at the
   point before a bottom horizontal.

   This code is *very* sensitive to the use of EPSILON tests. Do not use them
   unless you have really carefully analysed their effects. They tend to
   cause inaccuracy and drift, which can result in degenerate segments being
   added, which sometimes extend beyond the ideal clippath.

---------------------------------------------------------------------------- */
typedef struct clipedge {
  struct clipedge *next ;
  LINELIST *pre ; /* Point before bottom horizontal */
  PATHLIST *path ; /* Path on which this point appears */
  SYSTEMVALUE sx, ex ;
} CLIPEDGE ;

static void free_edges(CLIPEDGE *edges)
{
  while ( edges ) {
    CLIPEDGE *next = edges->next ;
    mm_free(mm_pool_temp, (mm_addr_t)edges, sizeof(CLIPEDGE)) ;
    edges = next ;
  }
}

static CLIPEDGE *get_edge(CLIPEDGE **spareedges)
{
  CLIPEDGE *edge ;

  HQASSERT(spareedges, "Nowhere to put get edges from") ;

  edge = *spareedges ;

  if ( edge ) {
    *spareedges = edge->next ;
  } else {
    edge = mm_alloc(mm_pool_temp, sizeof(CLIPEDGE), MM_ALLOC_CLASS_CLIP_PATH) ;
    if ( edge == NULL )
      (void)error_handler( VMERROR );
  }

  return edge ;
}

static void free_edge(CLIPEDGE *thisedge, CLIPEDGE **spareedges)
{
  HQASSERT(spareedges, "Nowhere to put freed edge") ;
  HQASSERT(thisedge, "No edge to free") ;

  HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
          ("discard edge %f-%f", thisedge->sx, thisedge->ex)) ;

  thisedge->next = *spareedges ;
  *spareedges = thisedge ;
}

/* Split an internal subpath off; this is called when two edges from the
   same path touch at the end of a scanline. The path between the two edges
   is split off into a separate internal subpath. It is already oriented in
   reverse. ASCII diagram:

   +------+
   |      |
   | +--+ |
   | \  / |
   +--++--+  <-- Edges touch here.

*/
static Bool split_path_horizontal(PATHINFO *topath,
                                   CLIPEDGE *thisedge, CLIPEDGE *nextedge)
{
  PATHLIST *path ;
  LINELIST *tpre, *tstart, *npre, *nstart, *nend ;

  HQASSERT(thisedge, "Missing edge") ;
  HQASSERT(nextedge, "Missing edge") ;

  HQASSERT(thisedge->path == nextedge->path,
           "Split shouldn't be called with different paths") ;

  HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
          ("Split horizontal edges %f-%f and %f-%f",
           thisedge->sx, thisedge->ex, nextedge->sx, nextedge->ex)) ;

  CLIPPATH_STATS_INCR(split_path_horizontal) ;

  /* Find bottom horizontals */
  tpre = thisedge->pre ;
  HQASSERT(tpre, "Missing line") ;
  tstart = tpre->next ;
  HQASSERT(tstart, "Missing line") ;

  npre = nextedge->pre ;
  HQASSERT(npre, "Missing line") ;
  nstart = npre->next ;
  HQASSERT(nstart, "Missing line") ;
  nend = nstart->next ;
  HQASSERT(nend, "Missing line") ;

  HQASSERT(theY(thePoint(*nstart)) == theY(thePoint(*tstart)),
           "Lines are not at same Y") ;

  if ( nend->next == tpre ) {
    /* This is a special case, caused by floating point roundoff in
       intersection testing. We have two lines very close to parallel, and
       the determinant in the intersection test was too small to use as a
       divisor. The order of the endpoints was not known at the time, but may
       be reverse with respect to the start of the points. To merge the
       horizontals, we just need to forget everything between nstart and
       tend. */
    nstart->next = tstart->next ;
    free_line(nend, mm_pool_temp) ;
    free_line(tpre, mm_pool_temp) ;
    free_line(tstart, mm_pool_temp) ;

    /* Update retained edge for new previous point and end */
    thisedge->ex = nextedge->ex ;
    thisedge->pre = npre ;
    theSubPath(*thisedge->path) = nstart ;

    return TRUE ;
  }

  HQASSERT(fabs(theX(thePoint(*nend)) - theX(thePoint(*tstart))) <= COALESCE_EPSILON &&
           theY(thePoint(*nend)) == theY(thePoint(*tstart)),
           "Start/end points are not coincident") ;

#if 0
  /* There is a possibility that the colinear test will mis-diagnose a very
     sharp acute angle. */
  HQASSERT(!point_colinear(theX(thePoint(*tpre)), theY(thePoint(*tpre)), nend),
           "Internal path join should not be colinear") ;
#endif

  if ( (path = get_path(mm_pool_temp)) == NULL )
    return FALSE ;

  /* Join bottom horizontals together */
  nstart->next = tstart->next ;

  /* Update retained edge for new previous point and end */
  thisedge->ex = nextedge->ex ;
  thisedge->pre = npre ;
  theSubPath(*thisedge->path) = nstart ;

  /* Separate internal path */
  tstart->next = nend->next ;
  HQASSERT(nend != tpre && nend != npre, "Shouldn't be freeing pre") ;
  free_line(nend, mm_pool_temp) ;

  path->next = NULL ;
  theSubPath(*path) = tstart ;

  path_append_subpath(path, tpre, topath) ;

  return TRUE ;
}


/* Merge two separate paths; this is called when two edges from different
   paths touch at the end of a scanline. The path between the two edges
   is joined into one subpath, and the extra pathlist is removed from the final
   path and all subsequent edges. ASCII diagram:

   +      +
   |\    /|
   | \  / |
   +--++--+  <-- Edges touch here.

*/
static void merge_paths_horizontal(PATHINFO *topath, CLIPEDGE **edgeptr,
                                   CLIPEDGE *thisedge, CLIPEDGE *nextedge)
{
  PATHLIST *tpath, *npath, **pathptr ;
  LINELIST *tpre, *tstart, *npre, *nstart, *nend ;

  HQASSERT(thisedge, "Missing edge") ;
  HQASSERT(nextedge, "Missing edge") ;

  HQASSERT(thisedge->path != nextedge->path,
           "Merge shouldn't be called with same paths") ;

  HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
          ("Merge horizontal edges %f-%f and %f-%f",
           thisedge->sx, thisedge->ex, nextedge->sx, nextedge->ex)) ;

  CLIPPATH_STATS_INCR(merge_paths_horizontal) ;

  /* Find bottom horizontals */
  tpre = thisedge->pre ;
  HQASSERT(tpre, "Missing line") ;
  tstart = tpre->next ;
  HQASSERT(tstart, "Missing line") ;

  npre = nextedge->pre ;
  HQASSERT(npre, "Missing line") ;
  nstart = npre->next ;
  HQASSERT(nstart, "Missing line") ;
  nend = nstart->next ;
  HQASSERT(nend, "Missing line") ;

  HQASSERT(theY(thePoint(*nstart)) == theY(thePoint(*tstart)),
           "Lines are not at same Y") ;

  HQASSERT(fabs(theX(thePoint(*nend)) - theX(thePoint(*tstart))) <= COALESCE_EPSILON &&
           theY(thePoint(*nend)) == theY(thePoint(*tstart)),
           "Start/end points are not coincident") ;

  /* Join bottom horizontals together */
  nstart->next = tstart->next ;

#if 0
  /* There is a possibility that the colinear test will mis-diagnose a very
     sharp acute angle. */
  HQASSERT(!point_colinear(theX(thePoint(*tpre)), theY(thePoint(*tpre)), nend),
           "Different path join should not be colinear") ;
#endif

  /* Join top connection */
  tstart->next = nend->next ;
  HQASSERT(nend != tpre && nend != npre, "Should not be freeing edge pre") ;
  free_line(nend, mm_pool_temp) ;

  tpath = thisedge->path ;
  HQASSERT(tpath, "Missing path") ;

  npath = nextedge->path ;
  HQASSERT(npath, "Missing path") ;

  /* Update retained edge for new previous point and end */
  thisedge->ex = nextedge->ex ;
  thisedge->pre = npre ;

  /* This is ugly, but I see no alternative; we have to remove tpath from
     the pathlist. We move the new merged path to the end of the
     list so we can ensure that the last path and last line pointers are
     set correctly. We also need to change all references to
     the removed path in the edgelist. */
  pathptr = &topath->firstpath ;
  while ( *pathptr != NULL ) {
    if ( *pathptr == tpath )
      *pathptr = tpath->next ;
    else if ( *pathptr == npath )
      *pathptr = npath->next ;
    else
      pathptr = &(*pathptr)->next ;
  }

  tpath->next = NULL ;
  theSubPath(*tpath) = nstart ;
  topath->lastpath = *pathptr = tpath ;
  topath->lastline = npre ;

  while ( (thisedge = *edgeptr) != NULL ) {
    if ( thisedge->path == npath )
      thisedge->path = tpath ;
    edgeptr = &thisedge->next ;
  }

  free_path(npath, mm_pool_temp) ;
}


/* Split an internal subpath off; this is called when the extension of an edge
   undercuts another edges from the same path. The path between the two edges
   is split off into a separate internal subpath. It is already oriented in
   reverse. ASCII diagram:

   +------+
   |      |
   | +--+ |
   | \  / |
   |..++--+  <-- Extension of .. undercuts right hand edge
   |      |
   +------+
*/
static Bool split_path_vertical(PATHINFO *topath,
                                CLIPEDGE *thisedge, CLIPEDGE *nextedge)
{
  PATHLIST *path ;
  LINELIST *tpre, *tstart, *tend, *npre, *nstart, *nend ;
  SYSTEMVALUE dx ;

  HQASSERT(thisedge, "Missing edge") ;
  HQASSERT(nextedge, "Missing edge") ;

  HQASSERT(thisedge->path == nextedge->path,
           "Split shouldn't be called with different paths") ;

  HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
          ("Split vertical edges %f-%f and %f-%f",
           thisedge->sx, thisedge->ex, nextedge->sx, nextedge->ex)) ;

  CLIPPATH_STATS_INCR(split_path_vertical) ;

  /* Find bottom horizontals */
  tpre = thisedge->pre ;
  HQASSERT(tpre, "Missing line") ;
  tstart = tpre->next ;
  HQASSERT(tstart, "Missing line") ;
  tend = tstart->next ;
  HQASSERT(tend, "Missing line") ;

  npre = nextedge->pre ;
  HQASSERT(npre, "Missing line") ;
  nstart = npre->next ;
  HQASSERT(nstart, "Missing line") ;
  nend = nstart->next ;
  HQASSERT(nend, "Missing line") ;

  HQASSERT(theY(thePoint(*tstart)) == theY(thePoint(*tend)) &&
           theY(thePoint(*tend)) == theY(thePoint(*nstart)) &&
           theY(thePoint(*nstart)) == theY(thePoint(*nend)),
           "Lines are not at same Y") ;

  if ( (path = get_path(mm_pool_temp)) == NULL )
    return FALSE ;

  /* Join left end */
  dx = theX(thePoint(*nend)) - theX(thePoint(*tstart)) ;
  if ( dx <= COALESCE_EPSILON && dx >= -COALESCE_EPSILON ) {
    tstart->next = nend->next ;
    HQASSERT(nend != tpre && nend != npre, "Should not be freeing edge pre") ;
    free_line(nend, mm_pool_temp) ;
  } else {
    tstart->next = nend ;
  }

  /* Join right end */
  dx = theX(thePoint(*nstart)) - theX(thePoint(*tend)) ;
  if ( dx <= COALESCE_EPSILON && dx >= -COALESCE_EPSILON ) {
    HQASSERT(nstart != tpre && nstart != npre, "Should not be freeing edge pre") ;

    free_line(nstart, mm_pool_temp) ;

    if ( point_colinear(theX(thePoint(*npre)), theY(thePoint(*npre)), tend) ) {
      npre->next = tend->next ;
      HQASSERT(tend != tpre && tend != npre, "Should not be freeing edge pre") ;
      free_line(tend, mm_pool_temp) ;
      theSubPath(*thisedge->path) = npre ;
    } else {
      theSubPath(*thisedge->path) = npre->next = tend ;
    }
  } else if ( dx > 0 ) {
    thisedge->ex = theX(thePoint(*nstart)) ;
    theSubPath(*thisedge->path) = nstart->next = tend ;
  } else {
    theSubPath(*thisedge->path) = nstart->next = tend ;
  }

  /* Update retained edge for new previous point */
  thisedge->pre = npre ;

  /* Add new internal path */
  path->next = NULL ;
  theSubPath(*path) = tstart ;

  path_append_subpath(path, tpre, topath) ;

  return TRUE ;
}

/* Merge two separate paths; this is called when two edges from different
   paths touch at the end of a scanline. The path between the two edges
   is joined into one subpath, and the extra pathlist is removed from the final
   path and all subsequent edges. ASCII diagram:

   +--+   +
   |  |  / \
   |..+-+---+  <-- Extension of .. undercuts right hand edge
   |        |
   +--------+
*/

static void merge_paths_vertical(PATHINFO *topath, CLIPEDGE **edgeptr,
                                 CLIPEDGE *thisedge, CLIPEDGE *nextedge)
{
  PATHLIST *tpath, *npath, **pathptr ;
  LINELIST *tpre, *tstart, *tend, *npre, *nstart, *nend ;
  SYSTEMVALUE dx ;

  HQASSERT(thisedge, "Missing edge") ;
  HQASSERT(nextedge, "Missing edge") ;

  HQASSERT(thisedge->path != nextedge->path,
           "Merge shouldn't be called with same paths") ;

  HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
          ("Merge vertical edges %f-%f and %f-%f",
           thisedge->sx, thisedge->ex, nextedge->sx, nextedge->ex)) ;

  CLIPPATH_STATS_INCR(merge_paths_vertical) ;

  /* Find bottom horizontals */
  tpre = thisedge->pre ;
  HQASSERT(tpre, "Missing line") ;
  tstart = tpre->next ;
  HQASSERT(tstart, "Missing line") ;
  tend = tstart->next ;
  HQASSERT(tend, "Missing line") ;

  npre = nextedge->pre ;
  HQASSERT(npre, "Missing line") ;
  nstart = npre->next ;
  HQASSERT(nstart, "Missing line") ;
  nend = nstart->next ;
  HQASSERT(nend, "Missing line") ;

  HQASSERT(theY(thePoint(*tstart)) == theY(thePoint(*tend)) &&
           theY(thePoint(*tend)) == theY(thePoint(*nstart)) &&
           theY(thePoint(*nstart)) == theY(thePoint(*nend)),
           "Lines are not at same Y") ;

  tpath = thisedge->path ;
  HQASSERT(tpath, "Missing path") ;

  npath = nextedge->path ;
  HQASSERT(npath, "Missing path") ;

  /* Join left end */
  dx = theX(thePoint(*nend)) - theX(thePoint(*tstart)) ;
  if ( dx <= COALESCE_EPSILON && dx >= -COALESCE_EPSILON ) {
    tstart->next = nend->next ;
    HQASSERT(nend != tpre && nend != npre, "Should not be freeing edge pre") ;
    free_line(nend, mm_pool_temp) ;
  } else {
    tstart->next = nend ;
  }

  /* Join right end */
  dx = theX(thePoint(*nstart)) - theX(thePoint(*tend)) ;
  if ( dx <= COALESCE_EPSILON && dx >= -COALESCE_EPSILON ) {
    HQASSERT(nstart != tpre && nstart != npre, "Should not be freeing edge pre") ;
    free_line(nstart, mm_pool_temp) ;

    if ( point_colinear(theX(thePoint(*npre)), theY(thePoint(*npre)), tend) ) {
      npre->next = tend->next ;
      HQASSERT(tend != tpre && tend != npre, "Should not be freeing edge pre") ;
      free_line(tend, mm_pool_temp) ;
      theSubPath(*tpath) = npre ;
    } else {
      theSubPath(*tpath) = npre->next = tend ;
    }
  } else if ( dx > 0 ) {
    thisedge->ex = theX(thePoint(*nstart)) ;
    theSubPath(*tpath) = nstart->next = tend ;
  } else {
    theSubPath(*tpath) = nstart->next = tend ;
  }

  /* Update retained edge for new previous point */
  thisedge->pre = npre ;

  /* This is ugly, but I see no alternative; we have to remove one of the
     pathlist copies from the pathlist. We move the new merged path to the
     end of the list so we can ensure that the last path and last line
     pointers are set correctly. We also need to change all references to
     the removed path in the edgelist. */
  pathptr = &topath->firstpath ;
  while ( *pathptr != NULL ) {
    if ( *pathptr == tpath )
      *pathptr = tpath->next ;
    else if ( *pathptr == npath )
      *pathptr = npath->next ;
    else
      pathptr = &(*pathptr)->next ;
  }

  tpath->next = NULL ;
  topath->lastpath = *pathptr = tpath ;
  topath->lastline = tpre ;

  while ( (thisedge = *edgeptr) != NULL ) {
    if ( thisedge->path == npath )
      thisedge->path = tpath ;
    edgeptr = &thisedge->next ;
  }

  free_path(npath, mm_pool_temp) ;
}

/* Add new points to path indicated in edge structure. The new points may
   extend beyond rightward beneath the next edge. */
static Bool coalesce_edge(CLIPEDGE *thisedge,
                          SYSTEMVALUE px, SYSTEMVALUE tx, SYSTEMVALUE py,
                          SYSTEMVALUE ix1, SYSTEMVALUE ix2, SYSTEMVALUE ny)
{
  PATHLIST *path ;
  LINELIST *pre, *start, *end ;
  SYSTEMVALUE dx ;

  HQASSERT(thisedge, "No edge to extend") ;

  path = thisedge->path ;
  HQASSERT(path, "No path in edge structure") ;

  pre = thisedge->pre ;
  HQASSERT(pre, "No previous linelist in edge structure") ;

  start = pre->next ;
  HQASSERT(start, "No start linelist in edge structure") ;
  HQASSERT(theLineType(*start) == LINETO, "Start linelist is not LINETO") ;

  end = start->next ;
  HQASSERT(end, "No end linelist in edge structure") ;
  HQASSERT(theLineType(*end) == LINETO, "End linelist is not LINETO") ;

  HQASSERT(theY(thePoint(*start)) == theY(thePoint(*end)),
           "Start and end are not on same horizontal") ;

  HQASSERT(thisedge->sx < thisedge->ex, "Extending degenerate edge") ;

  HQASSERT(theX(thePoint(*start)) > theX(thePoint(*end)),
           "Start should be before end") ;

  HQASSERT(px <= tx && ix1 <= ix2,
           "Start of extension should be left of end") ;
  HQASSERT(py < ny, "Start of extension should be lower than end") ;

  HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
          ("coalesce horizontal %f-%f,%f add %f-%f %f-%f,%f",
           thisedge->sx, thisedge->ex, py, px, tx, ix1, ix2, ny)) ;

  CLIPPATH_STATS_INCR(coalesce_edge) ;

  /* Some of these points can be optimised away by testing colinearity. Keep
     the path well-connected always, so it can be freed easily. We have to be
     careful, though, because the end of this horizontal can be the previous
     point to another. */
  dx = px - thisedge->sx ;
  if ( dx > COALESCE_EPSILON || dx < -COALESCE_EPSILON ) {
    if ( (end = get_line(mm_pool_temp)) == NULL )
      return FALSE ;

    theX(thePoint(*end)) = px ;
    theY(thePoint(*end)) = py ;
    theLineType(*end) = LINETO ;
    end->next = start->next ;
    start->next = end ;

    if ( (end = get_line(mm_pool_temp)) == NULL )
      return FALSE ;

    theX(thePoint(*end)) = ix1 ;
    theY(thePoint(*end)) = ny ;
    theLineType(*end) = LINETO ;
    end->next = start->next ;
    start->next = end ;
  } else if ( ny > theY(thePoint(*end->next)) &&
              point_colinear(ix1, ny, end) ) {
    /* Test on Y above is a failsafe to avoid removing acute angle notches
       caused by tiny gradient differences. */
    theX(thePoint(*end)) = ix1 ;
    theY(thePoint(*end)) = ny ;
  } else {
    if ( (end = get_line(mm_pool_temp)) == NULL )
      return FALSE ;

    theX(thePoint(*end)) = ix1 ;
    theY(thePoint(*end)) = ny ;
    theLineType(*end) = LINETO ;
    end->next = start->next ;
    start->next = end ;
  }

  HQASSERT(pre->next == start, "Pre-start order incorrect") ;
  HQASSERT(start->next == end, "Start-end order incorrect") ;

  dx = tx - thisedge->ex ;
  if ( dx > COALESCE_EPSILON || dx < -COALESCE_EPSILON ) {
    if ( (end = get_line(mm_pool_temp)) == NULL )
      return FALSE ;

    theX(thePoint(*end)) = tx ;
    theY(thePoint(*end)) = py ;
    theLineType(*end) = LINETO ;
    end->next = start->next ;
    start->next = end ;

    start = end ;

    if ( ix1 != ix2 ) {
      if ( (end = get_line(mm_pool_temp)) == NULL )
        return FALSE ;

      theX(thePoint(*end)) = ix2 ;
      theY(thePoint(*end)) = ny ;
      theLineType(*end) = LINETO ;
      end->next = start->next ;
      start->next = end ;
      theSubPath(*path) = start ;
    }
  } else if ( ny > theY(thePoint(*pre)) &&
              point_colinear(ix2, ny, pre) ) {
    /* Test on Y above is a failsafe to avoid removing horizontal aligned
       points */
    if ( ix1 == ix2 ) {
      pre->next = end ;
      HQASSERT(start != thisedge->pre, "Should not be freeing edge pre") ;
      free_line(start, mm_pool_temp) ;
    } else {
      theX(thePoint(*start)) = ix2 ;
      theY(thePoint(*start)) = ny ;
    }
    theSubPath(*path) = pre ;
    tx = thisedge->ex ;
  } else if ( ix1 != ix2 ) {
    if ( (end = get_line(mm_pool_temp)) == NULL )
      return FALSE ;

    theX(thePoint(*end)) = ix2 ;
    theY(thePoint(*end)) = ny ;
    theLineType(*end) = LINETO ;
    end->next = start->next ;
    start->next = end ;
    theSubPath(*path) = start ;
    tx = thisedge->ex ;
  } else
    tx = thisedge->ex ;

  thisedge->sx = tx ; /* Change length of edge */

  return TRUE ;
}

Bool normalise_path(PATHINFO *fpath, int32 therule, Bool userectclippath,
                    Bool ignore_degenerates)
{
  register int32 number , first , last , index , loop ;
  register LINELIST *tmpline ;
  register LINELIST *theline ;
  LINELIST **thelines = NULL ;
  SYSTEMVALUE ix1 , ix2 , tx ;
  register SYSTEMVALUE px , nx = 0 , ny , miny, scanline, edgey = 0 ;
  Bool success = TRUE ;
  CLIPEDGE *edges = NULL, *spareedges = NULL ;

  HQASSERT(INTERSECT_TYPE != EOFILL_TYPE && INTERSECT_TYPE != NZFILL_TYPE,
           "Intersection rule is not unique") ;

  HQASSERT((INTERSECT_TYPE & CLIPRULE) != INTERSECT_TYPE,
           "Intersection rule could be produced by bad fill type; select a better value") ;

  HQASSERT(therule == EOFILL_TYPE || therule == NZFILL_TYPE ||
           therule == INTERSECT_TYPE, "Rule unrecognised") ;

  CLIPPATH_STATS_INCR(normalise_path) ;

  if ( ! fpath->firstpath )      /* degenerate path */
    return TRUE ;

#ifdef DEBUG_BUILD
  if ( CLIPPATH_DBG(CLIPPATH_DBG_TRACE_BEFORE) ) {
    monitorf((uint8 *)"%% Subpath before normalise\n", ignore_degenerates) ;
    dump_paths(fpath->firstpath) ;
    monitorf((uint8 *)"originalpath\n", therule) ;
  }

  if ( CLIPPATH_DBG(CLIPPATH_DBG_NO_DEGENERATE) ) /* No degenerate removal? */
    ignore_degenerates = FALSE ;
#endif

  if ( 0 == ( number = setup_lines( fpath->firstpath, ignore_degenerates, & thelines , &success ))) {
    path_init(fpath) ; /* Must clear input path if all degenerate */
    return success ;
  }

#ifdef DEBUG_BUILD
  if ( number > clippath_stats.max_lines )
    clippath_stats.max_lines = number ;
#endif

  first = index = 0 ;
  path_init(fpath) ;

  /* Sort the line pairs on their y-values. */
  qsort(thelines, number, sizeof(LINELIST *), ylinecmp) ;

  do {
    int32 start, orient ;

    HQASSERT(check_sorted(&thelines[first], number - first, ylinecmp),
             "Line list Y order incorrect") ;
    HQASSERT(first <= index && index <= number,
             "Previous last line out of range") ;

    /* Merge in any new lines that are now encountered. */
    theline = thelines[ first ] ;
    scanline = theY(thePoint(*theline)) ;
    if ( userectclippath &&
         scanline >= (SYSTEMVALUE)theY2Clip(thegsPageClip(*gstateptr)) ) {
      break ;
    }

    if ( !interrupts_clear(allow_interrupt) ) {
      (void) report_interrupt(allow_interrupt);
      goto normalise_error;
    }

    CLIPPATH_STATS_INCR(scanbeams) ;

    /* The == test here is OK, because scanline is set from the top Y
       coordinate each time round the loop. On subdivision, all of the
       unfinished lines have their Y coords set to the next value of
       scanline. The next scanline value will never be the same as the current
       one because the intersection tests will not return a value less than
       or equal to the current scanline. Horizontals (for infill) are
       treated specially; they are sorted before non-horizontals, and
       different winding rules applied. */
    for ( last = first ;
          last < number && theY(thePoint(*thelines[last])) == scanline ;
          ++last ) {
      LINELIST *end, *next ;

      end = thelines[last]->next ;
      HQASSERT(end, "No endpoint to line") ;

      next = end->next ;

      /* Reset intersection/colinear linelist marker and group */
      if ( next == NULL ) {
        HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_LINES),
                ("clippath new line %d: (%f,%f)-(%f,%f) orient %d mark %d",
                 last,
                 theX(thePoint(*thelines[last])), theY(thePoint(*thelines[last])),
                 theX(thePoint(*end)), theY(thePoint(*end)),
                 LINELIST_ORIENT(thelines[last]), theLineOrder(*thelines[last]))) ;

        if ( (end->next = next = get_line(mm_pool_temp)) == NULL )
          goto normalise_error ;

        /* Line order of intersection point is used as extra reference
           count (number of connections-1). */
        theLineOrder(*next) = 0 ;
        next->next = NULL ;
      }

      thePoint(*next) = thePoint(*end) ;
    }

    HQASSERT(last >= index, "Last line less than previous last line") ;
    HQASSERT(last > first, "No lines on scanline") ;

    /* Quicksort has its worst behaviour when nearly sorted. We try to avoid
       quicksort when the lines are nearly ordered, by determining how many
       new lines have been introduced since the last iteration. */
    if ( last - index > 4 )
      qsort(&thelines[first], last - first, sizeof(LINELIST *), xlinecmp) ;
    else
      fbubblesort(&thelines[first], last - first, xlinecmp) ;

#ifdef DEBUG_BUILD
    if ( last - first > clippath_stats.max_scanbeam_lines )
      clippath_stats.max_scanbeam_lines = last - first ;

    clippath_stats.total_scanbeam_lines += last - first ;

    for ( loop = first ; loop < last ; ++loop ) {
      theline = thelines[loop] ;
      tmpline = theline->next ;

      HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_SORT),
              ("Linelist %d %f,%f to %f,%f orient %d mark %d %x", loop,
               theX(thePoint(*theline)), theY(thePoint(*theline)),
               theX(thePoint(*tmpline)), theY(thePoint(*tmpline)),
               LINELIST_ORIENT(theline), theLineOrder(*theline),
               tmpline->next)) ;
    }
#endif

    /* Calculating the smallest y-value that there is in the lines. */
    miny = theY(thePoint(*thelines[first]->next)) ;
    loop = first ;
    do {
      start = loop ;
      orient = 0 ;
      do {
        HQASSERT(loop < last, "Overran end of line buffer") ;
        tmpline = thelines[loop] ;

        HQASSERT(LINELIST_ORIENT(tmpline) <= 1 && LINELIST_ORIENT(tmpline) >= -1,
                 "Line orientation not initialised") ;
        orient += (int32)LINELIST_ORIENT(tmpline) ;

        ny = theY(thePoint(*tmpline->next)) ;
        if ( ny < miny )
          miny = ny ;

        if ( therule == EOFILL_TYPE )
          orient &= 1 ; /* Just care if its even or odd */

        ++loop ;
      } while ( orient != 0 ) ;

      /* Now perform required intersection tests. The loop-start differences
         are because if we have one line, there is no need to intersect with
         anything (it must be horizontal and have orientation 0). If we have
         two lines, they are the start and end of a span, and intersecting
         the right line against all other lines will find the lowest
         intersection. If we have more than two, then fall back to intersecting
         all lines against each other. */
      if ( loop - start > 2 ) { /* Need to intersect all lines */
        SYSTEMVALUE stemp = miny ; /* Can't do & on register variable */
        while ( start < loop ) {
          if ( LINELIST_ORIENT(thelines[start]) != 0 )
            anyints(thelines, start, start+1, last, scanline, &stemp) ;
          HQASSERT(miny == scanline || stemp > scanline,
                   "Intersection returned scanline") ;
          ++start ;
        }
        miny = stemp ;
      } else if ( loop - start > 1 ) { /* Only with left & right most lines. */
        SYSTEMVALUE stemp = miny ; /* Can't do & on register variable */

        HQASSERT(LINELIST_ORIENT(thelines[start]) != 0 &&
                 LINELIST_ORIENT(thelines[loop-1]) != 0,
                 "Start and end cannot be horizontal") ;

        /* Since we only have two meaningful lines, the left line can only
           intersect the right. Intersections with lines to the right of the
           right line will cut the right line before the left line. All of
           these tests can be done in one call, by testing the right line
           against all other lines. */
        anyints(thelines, loop-1, start, last, scanline, &stemp) ;
        HQASSERT(miny == scanline || stemp > scanline,
                 "Intersection returned scanline") ;
        miny = stemp ;
      } else {
        HQASSERT(loop - start == 1, "Should be only one line (a horizontal)") ;
      }
    } while ( loop < last ) ;

    /* Check that smallest y is less than 'next y start' value. */
    if ( last < number ) {
      CLIPPATH_STATS_INCR(new_line_intersections) ;
      ny = theY(thePoint(*thelines[last])) ;
      if ( ny < miny )
        miny = ny ;
    }

    HQASSERT(miny >= scanline, "Y order confused") ;

#ifdef DEBUG_BUILD
    if ( miny - scanline > clippath_stats.max_scanbeam )
      clippath_stats.max_scanbeam = miny - scanline ;
    if ( miny - scanline < clippath_stats.max_scanbeam ||
         clippath_stats.min_scanbeam == 0.0 )
      clippath_stats.min_scanbeam = miny - scanline ;
#endif

    /* Process all of the lines starting at this scanline before updating
       any of them. Process any horizontal sections before the rest of the
       lines; NZ and EO rules ensure that horizontals are disjoint with
       respect to the rest of the path. Intersect rule detects horizontals
       overlapping each other, or overlapping other fill sections. We do
       not perform any path coalescing when ignoring degenerates, because it
       is only used for infill testing. */
    if ( scanline == miny ) {
      int32 order = 0, horder = 0 ;

      HQASSERT(!ignore_degenerates,
               "Horizontal found when ignoring degenerates") ;

      start = first ;
      orient = 0 ;

      do {
        int32 lastorder ;
        SYSTEMVALUE lastx ;

        HQASSERT(start < last, "Ran out of lines in horizontal elimination") ;
        theline = thelines[start] ;

        if ( LINELIST_ORIENT(theline) != 0 ) {
          orient += LINELIST_ORIENT(theline) ;
          nx = theX(thePoint(*theline)) ;
          horder = order ;
          order ^= theLineOrder(*theline) ;
          ++start ;
          continue ;
        }

        /* Horizontal found. Now find out its maximum extent */
        lastx = nx ;
        px = theX(thePoint(*theline)) ;
        nx = theX(thePoint(*theline->next)) ;
        lastorder = horder ;
        horder = (order | theLineOrder(*theline)) ;

        /* Find smallest endpoint of all horizontals */
        loop = start ;
        while ( ++loop < last &&
                LINELIST_ORIENT(thelines[loop]) == 0 &&
                theX(thePoint(*thelines[loop])) <= px /* + COLINEARITY_EPSILON */ ) {
          tmpline = thelines[loop]->next ;
          horder |= theLineOrder(*tmpline) ;
          if ( theX(thePoint(*tmpline)) < nx )
            nx = theX(thePoint(*tmpline)) ;
        }

        /* Does the next line start before horizontal is finished? */
        if ( loop < last && theX(thePoint(*thelines[loop])) < nx )
          nx = theX(thePoint(*thelines[loop])) ;

        /* Only add a horizontal segment if there are more than horizontals
           (just one horizontal implies that it is connecting two sides of a
           path together at the top, bottom, or an inflection, and
           intersection can this be dealt with by the path edges). */
        switch ( therule ) {
        case EOFILL_TYPE:
          orient &= 1 ; /* Just care if its even or odd */
          /* Fallthrough to orientation test */
        case NZFILL_TYPE:
          /* We will only add a horizontal if we are not inside a filled
             section. Points are added in the same order as below, even
             though edges are not coalesced. */
          if ( orient == 0 ) {
            LINELIST *line ;
            PATHLIST *path ;

            if ( add_two_cyclic(&path, &line, px, scanline, nx, scanline) ) {
              path_append_subpath(path, line, fpath) ;
            } else
              goto normalise_error ;
          }
          break ;
        case INTERSECT_TYPE:
          /* Intersection is either when we have a line and are in a filled
             area (the paths have been normalised to be disjoint already),
             or when two horizontals with different original paths overlap.

             There is a special case for intersections with zero-width
             vertical lines. There is one case where the horizontal
             intersection currently fails; this is where a horizontal ends
             exactly at a vertical boundary. */
          if ( order != IntersectBoth ) {
            LINELIST *line ;
            PATHLIST *path ;

            if ( horder == IntersectBoth ) {
              if ( add_two_cyclic(&path, &line, px, scanline, nx, scanline) ) {
                path_append_subpath(path, line, fpath) ;
              } else
                goto normalise_error ;
            } else if ( (horder ^ lastorder) == IntersectBoth && lastx == px ) {
              if ( add_two_cyclic(&path, &line, px, scanline, px, scanline) ) {
                path_append_subpath(path, line, fpath) ;
              } else
                goto normalise_error ;
            }
          }
          break ;
        default:
          HQFAIL("Unknown clip rule") ;
        }

        /* Update X points */
        index = start ;
        do {
          tmpline = thelines[index] ;

          /* If we have reached the right end of the horizontal forget about
             it by switching with start and incrementing start. */
          if ( nx >= theX(thePoint(*tmpline->next)) ) {
            LINELIST *tmp = thelines[start] ;

            HQASSERT(start <= index, "Switching with incorrect line") ;
            thelines[start] = thelines[index] ;
            thelines[index] = tmp ;
            ++start ;
          } else
            theX(thePoint(*tmpline)) = nx ;
        } while ( ++index < loop ) ;

        /* Re-sort remaining lines in scanline. We perform a re-sort because
           setting the X coordinate in the update loop may make the X
           coordinate the same as a non-horizontal, and the order of the
           lines should be changed. We use bubblesort because the order will
           be mostly correct, and qsort's worst performance is with sorted
           arrays. */
        if ( start < last )
          fbubblesort(&thelines[start], last - start, xlinecmp) ;
      } while ( start < last ) ;
    } else { /* No horizontals in line list */
      int8 *PoorClippath = get_core_context_interp()->systemparams->PoorClippath;
      CLIPEDGE **edgeptr = &edges ;
      int32 order = 0 ;
      SYSTEMVALUE lastx ; /* Last X value of updated line */

      start = index = first ;
      orient = 0 ;

      /* The lastx variable is used to ensure that the intersection points
         with the next scanline are monotonically increasing. Intersection
         rounding can otherwise make them overlap, if the rounding methods
         for two nearly colinear lines are different (intersection vs.
         scanline interpolation). Initialise lastx to leftmost point of first
         line; no intersection can be further left than this point. */
      lastx = theX(thePoint(*thelines[start])) ;
      if ( theX(thePoint(*thelines[start]->next)) < lastx )
        lastx = theX(thePoint(*thelines[start]->next)) ;

      do {
        HQASSERT(index < last, "Overran end of line buffer") ;
        tmpline = thelines[index] ;

        HQASSERT(LINELIST_ORIENT(tmpline) == 1 || LINELIST_ORIENT(tmpline) == -1,
                 "Line orientation not correct") ;

        orient += (int32)LINELIST_ORIENT(tmpline) ;

        /* Skip to next iteration if not at end of span */
        switch ( therule ) {
        case INTERSECT_TYPE:
          if ( order == IntersectBoth ) {
            order ^= theLineOrder(*tmpline) ;
            break ; /* End of clipped path */
          }

          order ^= theLineOrder(*tmpline) ;

          if ( order == IntersectBoth )
            start = index ; /* Start of clipped path */

          continue ;
        case EOFILL_TYPE:
          orient &= 1 ; /* Just care if its even or odd */
          /* Fallthrough to orientation test */
        case NZFILL_TYPE:
          if ( orient != 0 )
            continue ;

          /* Look ahead to see if next line has been marked as colinear. This
             ensures that we select the maximum spanning polygon, simplifying
             the path created. */
          if ( index + 1 < last &&
               tmpline->next->next == thelines[index + 1]->next->next )
            continue ;

          break ;
        default:
          HQFAIL("Unrecognised clip rule") ;
        }

        HQASSERT(start < index, "No lines in region") ;

        /* Add quadrilateral or triangle if not degenerate, bounded by start
           and index threads. If the intersection point is set to a scanline
           close enough to the new one, its X value will be used; if the Y
           value is sufficiently greater than the new scanline, then it will
           be used to bound the interpolation. */
        theline = thelines[ start ] ;
        px = theX(thePoint(*theline)) ;
        tmpline = theline->next ;
        nx = theX(thePoint(*tmpline)) ;
        ny = theY(thePoint(*tmpline)) ;
        tmpline = tmpline->next ;
        if ( theY(thePoint(*tmpline)) <= miny + INTERSECTION_ZONE_EPSILON ||
             theY(thePoint(*tmpline)) <= scanline + MINIMUM_SCANBEAM ) {
          CLIPPATH_STATS_INCR(intersections_zone) ;
          ix1 = theX(thePoint(*tmpline)) ; /* Intersection already calculated */
        } else {
          ix1 = CP_ROUND(px + (((nx - px) * (miny - scanline)) / (ny - scanline))) ;
          HQASSERT(ny != miny || nx == ix1,
                   "Interpolation should have yielded endpoint") ;
          HQASSERT((ix1 >= theX(thePoint(*theline)) && ix1 <= theX(thePoint(*tmpline))) ||
                   (ix1 >= theX(thePoint(*tmpline)) && ix1 <= theX(thePoint(*theline))) ||
                   theLineOrder(*tmpline) > 0,
                   "Intersection outside bounding region") ;
        }
        if ( ix1 < lastx ) {
          HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADJUST),
                  ("clippath: adjusted left intersection of %d from %f to %f line %f",
                   start, ix1, lastx, miny)) ;
          ix1 = lastx ;
        }
        theX(thePoint(*tmpline)) = theX(thePoint(*theline)) = ix1 ;
        theY(thePoint(*tmpline)) = theY(thePoint(*theline)) = miny ;

        HQASSERT((ix1 >= theX(thePoint(*thelines[start])) &&
                  ix1 <= theX(thePoint(*thelines[start]->next))) ||
                 (ix1 <= theX(thePoint(*thelines[start])) &&
                  ix1 >= theX(thePoint(*thelines[start]->next))),
                 "Left interpolation out of range of left line") ;

        theline = thelines[ index ] ;
        tx = theX(thePoint(*theline)) ;
        tmpline = theline->next ;
        nx = theX(thePoint(*tmpline)) ;
        ny = theY(thePoint(*tmpline)) ;
        tmpline = tmpline->next ;
        if ( theY(thePoint(*tmpline)) <= miny + INTERSECTION_ZONE_EPSILON ||
             theY(thePoint(*tmpline)) <= scanline + MINIMUM_SCANBEAM ) {
          CLIPPATH_STATS_INCR(intersections_zone) ;
          ix2 = theX(thePoint(*tmpline)) ; /* Intersection already calculated */
        } else {
          ix2 = CP_ROUND(tx + (((nx - tx) * (miny - scanline)) / (ny - scanline))) ;
          HQASSERT(ny != miny || nx == ix2,
                   "Interpolation should have yielded endpoint") ;
          HQASSERT((ix2 >= theX(thePoint(*theline)) && ix2 <= theX(thePoint(*tmpline))) ||
                   (ix2 >= theX(thePoint(*tmpline)) && ix2 <= theX(thePoint(*theline))) ||
                   theLineOrder(*tmpline) > 0,
                   "Intersection outside bounding region") ;
        }
        if ( ix2 < ix1 ) {
          HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADJUST),
                  ("clippath: adjusted right intersection of %d from %f to %f line %f",
                   index, ix2, ix1, miny)) ;
          ix2 = ix1 ;
        }
        lastx = ix2 ;
        theX(thePoint(*tmpline)) = theX(thePoint(*theline)) = ix2 ;
        theY(thePoint(*tmpline)) = theY(thePoint(*theline)) = miny ;

        HQASSERT((ix2 >= theX(thePoint(*thelines[index])) &&
                  ix2 <= theX(thePoint(*thelines[index]->next))) ||
                 (ix2 <= theX(thePoint(*thelines[index])) &&
                  ix2 >= theX(thePoint(*thelines[index]->next))),
                 "Right interpolation out of range of right line") ;

        HQASSERT(px <= tx, "Top points out of order") ;
        HQASSERT(ix1 <= ix2, "Bottom points out of order") ;

        if ( !userectclippath ||
             miny > (SYSTEMVALUE)theY1Clip(thegsPageClip(*gstateptr)) ) {
          LINELIST *pre = NULL ;
          PATHLIST *path = NULL ;

          if ( PoorClippath[7] ) {
            CLIPEDGE *thisedge, *nextedge ;

            /* Find next edge, throwing away unused ones from last scanline */
            while ( (thisedge = *edgeptr) != NULL &&
                    (px >= thisedge->ex || scanline > edgey) ) {
              *edgeptr = thisedge->next ;
              free_edge(thisedge, &spareedges) ;
            }

            if ( thisedge && tx > thisedge->sx ) {
              if ( px != tx || ix1 != ix2 ) {
                /* Join new points to existing edge. Don't remove existing
                   edge structure, it will be thrown away if it is not needed
                   when the current extent is exceeded, or at the end of the
                   scanline loop at the latest. This routine leaves the
                   edge's path's initial point at the predecessor of the
                   bottom horizontal. */
                if ( !coalesce_edge(thisedge, px, tx, scanline, ix1, ix2, miny) )
                  goto normalise_error ;

                /* This horizontal may undercut the edges from the previous
                   scanline, requiring that they be joined or split
                   appropriately. The following diagram illustrates the
                   cases whether the undercut edge is and is not on the
                   same path:

                   +-----------+
                   |           |
                   | +-------+ |
                   | |       | |
                   | |   +   | |
                   | |  / \  | |
                   | | +---+ +-+ <- same Y as below, separated for clarity
                   | +---------+ <- left edge of this just joined above
                   |           |
                   +-----------+

                   Note that thisedge->sx has been adjusted to the right end
                   end of the merged horizontal by coalesce_edge; this may
                   cause it to invert with respect to thisedge->ex, which will
                   in turn cause the edge to be dropped as soon as the
                   vertical merge has been done. */
                while ( (nextedge = thisedge->next) != NULL &&
                        nextedge->sx < thisedge->sx ) {
                  /* Merge next edge with current path */
                  if ( thisedge->path == nextedge->path ) {
                    if ( !split_path_vertical(fpath, thisedge, nextedge) )
                      goto normalise_error ;
                  } else {
                    merge_paths_vertical(fpath, &edges, thisedge, nextedge) ;
                  }

                  thisedge->next = nextedge->next ;
                  free_edge(nextedge, &spareedges) ;
                }

                path = thisedge->path ;
                HQASSERT(path, "No path in edge structure") ;
                pre = theSubPath(*path) ;
                HQASSERT(pre, "No predecessor point in edge structure") ;

                if ( thisedge->sx >= thisedge->ex ) { /* Edge finished */
                  *edgeptr = thisedge->next ;
                  free_edge(thisedge, &spareedges) ;
                }
              }
            }
          }

          if ( path == NULL ) {
            /* Add a new cyclic path; the path points are in clockwise order,
               and the bottom horizontal, if any, is first on the path (which
               puts the lastline pointer into the immediately preceding
               line). */

            if ( px != tx ) {
              HQASSERT(px < tx, "X order confused") ;
              if ( ix1 != ix2 ) {
                HQASSERT(ix1 < ix2, "X intersection order confused") ;
                HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADD),
                        ("normalise_path %x quad %f,%f %f,%f %f,%f %f,%f",
                         fpath, px, scanline, tx, scanline, ix2, miny, ix1, miny)) ;

                if ( !add_four_cyclic(&path, &pre, ix2, miny, ix1, miny,
                                      px, scanline, tx, scanline) )
                  goto normalise_error ;
              } else {
                HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADD),
                        ("normalise_path %x triangle %f,%f %f,%f %f,%f",
                         fpath, px, scanline, tx, scanline, ix1, miny)) ;
                if ( !add_three_cyclic(&path, &pre, px, scanline, tx, scanline,
                                       ix1, miny) )
                  goto normalise_error ;
              }

              path_append_subpath(path, pre, fpath) ;
            } else if ( ix1 != ix2 ) { /* Same top point, check bottom points */
              HQASSERT(ix1 < ix2, "X intersection order confused") ;
              HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADD),
                      ("normalise_path %x triangle %f,%f %f,%f %f,%f",
                       fpath, px, scanline, ix2, miny, ix1, miny)) ;
              if ( !add_three_cyclic(&path, &pre, ix2, miny, ix1, miny,
                                     px, scanline) )
                goto normalise_error ;

              path_append_subpath(path, pre, fpath) ;
            } else if ( ! ignore_degenerates ) {
              HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADD),
                      ("normalise_path %x degen %f,%f %f,%f %f,%f %f,%f",
                       fpath, px, scanline, tx, scanline, ix2, miny, ix1, miny)) ;
              if ( !add_four_cyclic(&path, &pre, ix2, miny, ix1, miny,
                                    px, scanline, tx, scanline) )
                goto normalise_error ;

              path_append_subpath(path, pre, fpath) ;
            }
          }

          /* If figure has a bottom horizontal, insert it in the active edge
             list before the current position. If keeping degenerates, try
             to join every path together. */
          if ( ix1 != ix2 && PoorClippath[7] && path ) {
            CLIPEDGE *edge ;

            HQASSERT(pre, "No previous line pointer") ;

            if ( (edge = get_edge(&spareedges)) == NULL )
              goto normalise_error ;

            edge->next = *edgeptr ;
            edge->sx = ix1 ;
            edge->ex = ix2 ;
            edge->path = path ;
            edge->pre = pre ;
            *edgeptr = edge ;
            edgeptr = &edge->next ;

            HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_COALESCE),
                    ("new edge %f-%f,%f", edge->sx, edge->ex, miny)) ;
          }
        }

        start = index + 1 ;
      } while ( ++index < last ) ;

      if ( PoorClippath[7] ) {
        CLIPEDGE *thisedge ;

        /* Throw away remaining edges from previous scanline */
        while ( *edgeptr != NULL ) {
          thisedge = *edgeptr ;
          *edgeptr = thisedge->next ;
          free_edge(thisedge, &spareedges) ;
        }

        /* Now traverse current edge list, joining adjacent paths */
        if ( (thisedge = edges) != NULL ) {
          CLIPEDGE *nextedge ;

          HQASSERT(thisedge->sx <= thisedge->ex, "Degenerate edge") ;
          while ( (nextedge = thisedge->next) != NULL ) {
            HQASSERT(nextedge->sx <= nextedge->ex, "Degenerate edge") ;

            /* We do not assert that thisedge->ex <= nextedge->sx, because
               floating point rounding errors can cause trouble; the
               intersection code will ignore an intersection if the
               determinant is too small, and might possibly cause an overflow
               on division, but this means that lines can cross by a very
               small amount. This only matters if we try to merge two edges
               on the same path together; one of the edges may have its
               pointers corrupted if we're not careful. */
            /* Actually, it might work, given the FP work I've just done... */
            HQASSERT(thisedge->ex <= nextedge->sx,
                     "Edges should not overlap") ;

            if ( thisedge->ex >= nextedge->sx ) { /* Merge edges */
              HQASSERT(thisedge->path, "No PATHLIST for first edge") ;
              HQASSERT(nextedge->path, "No PATHLIST for first edge") ;

              if ( thisedge->path == nextedge->path ) { /* Split internal path */
                if ( !split_path_horizontal(fpath, thisedge, nextedge) )
                  goto normalise_error ;
              } else { /* Merge different paths */
                merge_paths_horizontal(fpath, &edges, thisedge, nextedge) ;
              }

              thisedge->next = nextedge->next ;
              free_edge(nextedge, &spareedges) ;
            } else
              thisedge = nextedge ;
          }
        }
      }
    }

    /* Update all the lines. */
    /* Throwing away any lines where start-y-coord equals end-y-coord. */
    for ( index = first ; index < last ; ++index ) {
      theline = thelines[ index ] ;
      tmpline = theline->next ;
      ny = theY(thePoint(*tmpline)) ;
      if ( ny <= miny ) {
        /* Finished with the line. */
        HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_LINES),
                ("clippath free line %d: (%f,%f)-(%f,%f)",
                 index,
                 theX(thePoint(*theline)), theY(thePoint(*theline)),
                 theX(thePoint(*tmpline)), theY(thePoint(*tmpline)))) ;

        free_line( theline, mm_pool_temp ) ;
        theline = tmpline->next ;
        free_line( tmpline, mm_pool_temp ) ;
        HQASSERT(theline, "No intersection line") ;
        if ( theLineOrder(*theline)-- == 0 )
          free_line(theline, mm_pool_temp) ; /* Intersection lines are refcounted */
        for ( loop = index ; loop > first ; --loop )
          thelines[ loop ] = thelines[ loop - 1 ] ;
        ++first ;
      } else if ( theY(thePoint(*theline)) != miny ) {
        px = theX(thePoint(*theline)) ;
        nx = theX(thePoint(*tmpline)) ;
        tmpline = tmpline->next ;

        if ( theY(thePoint(*tmpline)) <= miny + INTERSECTION_ZONE_EPSILON ) {
          theX(thePoint(*theline)) = theX(thePoint(*tmpline)) ; /* Intersection already calculated */
        } else {
          theX(thePoint(*tmpline)) = theX(thePoint(*theline)) =
            CP_ROUND(px + (((nx - px) * (miny - scanline)) / (ny - scanline))) ;
          HQASSERT(ny != miny || nx == theX(thePoint(*theline)),
                   "Interpolation should have yielded endpoint") ;
        }
        /* Update Y point */
        theY(thePoint(*tmpline)) = theY(thePoint(*theline)) = miny ;
      }
    }

    edgey = miny ;
  } while ( first < number ) ;

  freeany(thelines, first, number) ;
  free_edges(edges) ;
  free_edges(spareedges) ;

  if ( !remove_path_cycles(fpath->firstpath, &fpath->lastline) ) {
    (void)remove_path_cycles(fpath->firstpath, NULL) ;
    return FALSE ;
  }

#ifdef DEBUG_BUILD
  if ( CLIPPATH_DBG(CLIPPATH_DBG_TRACE_AFTER) && fpath->firstpath ) {
    monitorf((uint8 *)"%% Subpath after normalise\n") ;
    dump_paths(fpath->firstpath) ;
    monitorf((uint8 *)"%d normalisedpath\n", therule) ;
  }
#endif

  return TRUE ;

normalise_error:
  (void)remove_path_cycles(fpath->firstpath, NULL) ;
  path_free_list(fpath->firstpath, mm_pool_temp) ;
  freeany(thelines, first, number) ;
  free_edges(edges) ;
  free_edges(spareedges) ;

  return FALSE ;
}

/* setup_lines creates an array of lines from a path. The path is consumed,
   and the array contains two points for each line segment, oriented in
   increasing Y. The orient flag of the first point is set to 1 or -1,
   depending on whether the path was going down or up at that point. The next
   pointer of the second point is initialised to NULL. It is reused in
   normalise_path to indicate which line an intersection has been made with. */
static int32 setup_lines( PATHLIST *fpath, int32 ignore_horizontal,
                          LINELIST ***thelines, int32 *success_ptr )
{
  register int32 number , orient ;
  register LINELIST *tline , *theline ;
  PATHLIST *tpath , *thepath ;
  register LINELIST **rthelines ;
  register SYSTEMVALUE nx , ny , px , py , tx , ty ;

  /* Calculate the 'maximum' number of line segments in the paths. */
  number = 0 ;
  thepath = fpath ;
  while ( thepath ) {
    theline = theSubPath(*thepath) ;
    theline = theline->next ;
    while  ( theline ) {
      ++number ;
      theline = theline->next ;
    }
    thepath = thepath->next ;
  }
  /* Obtain memory in which to sort the lines. */
  rthelines = (LINELIST **)mm_alloc_with_header(mm_pool_temp,
                                                number * (int32)sizeof( LINELIST * ),
                                                MM_ALLOC_CLASS_LINELIST);
  if ( rthelines == NULL )
  {
    path_free_list(fpath, mm_pool_temp) ;       /* Chuck away input path */
    (*success_ptr) = error_handler( VMERROR ) ;
    return FALSE ;
  }
  (*thelines) = rthelines ;

  /* Set up the lines in pairs of LINELIST'S. */
  number = 0 ;
  thepath = fpath ;
  while ( thepath ) {
    theline = theSubPath(*thepath) ;
    nx = CP_ROUND(theX(thePoint(*theline))) ;
    ny = CP_ROUND(theY(thePoint(*theline))) ;
    tline = theline ;
    theline = theline->next ;
    free_line( tline, mm_pool_temp ) ;
    while  ( theline ) {
      px = nx ;
      py = ny ;
      nx = CP_ROUND(theX(thePoint(*theline))) ;
      ny = CP_ROUND(theY(thePoint(*theline))) ;
      if ( py != ny ) { /* Non-horizontal line */
        if ( NULL == (tline = get_line(mm_pool_temp)) ) {
          theSubPath(*thepath) = theline ;
          path_free_list( thepath, mm_pool_temp ) ;
          freeany( rthelines , 0 , number ) ;
          (*success_ptr) = error_handler( VMERROR ) ;
          return FALSE ;
        }
        /* Swap over the coordinates if required so that the first one has the
           smallest y-value. */
        if ( py > ny ) {
          orient = -1 ;
          tx = px ; px = nx ;
          ty = py ; py = ny ;
        }
        else {
          tx = nx ;
          ty = ny ;
          orient = 1 ;
        }
        rthelines[ number++ ] = tline ;
        SET_LINELIST_ORIENT(tline, orient) ;
        SET_LINELIST_ORIENT(theline, 0) ;

        theLineOrder(*tline) = theLineOrder(*theline) = theLineOrder(*thepath) ;
        theX(thePoint(*tline)) = px ;
        theY(thePoint(*tline)) = py ;
        tline->next = theline ;

        theX(thePoint(*theline)) = tx ;
        theY(thePoint(*theline)) = ty ;

        tline = theline->next ;
        theline->next = NULL ;
        theline = tline ;
      }
      else if ( ! ignore_horizontal ) { /* Horizontal line, not ignored */
        if ( NULL == (tline = get_line(mm_pool_temp)) ) {
          theSubPath(*thepath) = theline ;
          path_free_list( thepath, mm_pool_temp ) ;
          freeany( rthelines , 0 , number ) ;
          (*success_ptr) = error_handler( VMERROR ) ;
          return FALSE ;
        }
        /* Swap over the coordinates if required so that the first one has the
           smallest x-value. */
        if ( px > nx ) {
          orient = -1 ;
          tx = px ; px = nx ;
          ty = py ; py = ny ;
        }
        else {
          tx = nx ;
          ty = ny ;
          orient = 1 ;
        }
        rthelines[ number++ ] = tline ;
        SET_LINELIST_ORIENT(tline, 0) ;
        SET_LINELIST_ORIENT(theline, orient) ;

        theLineOrder(*tline) = theLineOrder(*theline) = theLineOrder(*thepath) ;
        theX(thePoint(*tline)) = px ;
        theY(thePoint(*tline)) = py ;
        tline->next = theline ;

        theX(thePoint(*theline)) = tx ;
        theY(thePoint(*theline)) = ty ;

        tline = theline->next ;
        theline->next = NULL ;
        theline = tline ;
      }
      else {      /* Horizontal line which is ignored. */
        tline = theline ;
        theline = theline->next ;
        free_line( tline, mm_pool_temp ) ;
      }
    }
    /* Move onto the next subpath, freeing structure. */
    tpath = thepath ;
    thepath = thepath->next ;
    free_path( tpath, mm_pool_temp ) ;
  }
  if ( ! number )
    freeany( rthelines , 0 , 0 ) ;
  return ( number ) ;
}

/* Search for intersections between theone and all lines between first and
   last. If an intersection happens at or before the minimum Y value, modify
   the minimum Y value and set the intersection pointer in the higher
   numbered line to point to the lower. The intersection group ordinal
   indicates which intersection pointers are valid, and the intersection
   pointers allow all of the intersections which occur on the same line to
   share the intersection calculations, preventing rounding errors causing
   problems in the X direction. The intersection will only be noted if it
   rounds to a line higher than the current scanline. */
static void anyints( LINELIST **thelines, int32 theone, int32 first,
                     int32 last, SYSTEMVALUE scanline, SYSTEMVALUE *yval )
{
  register int32 loop ;
  register LINELIST *theline, *tmpline ;
  SYSTEMVALUE x1, x2, y2, sx, sy, r1, r2, r3, sdx ;
  register SYSTEMVALUE px, py, nx, ny, dx, dy ;

  HQASSERT(first <= last, "No lines to intersect") ;
  HQASSERT(theone < last, "Nothing to intersect with") ;

  theline = thelines[ theone ] ;
  HQASSERT(LINELIST_ORIENT(theline) != 0, "Horizontal disallowed as intersector") ;
  px = theX(thePoint(*theline)) ;
  py = theY(thePoint(*theline)) ;
  tmpline = theline->next ;
  nx = theX(thePoint(*tmpline)) ;
  ny = theY(thePoint(*tmpline)) ;
  dx = nx - px ;
  dy = ny - py ;
  HQASSERT(dy > 0, "Line is not Y ordered") ;
  for ( loop = first ; loop < last ; ++loop ) {
    LINELIST *nloop, *ntheone, *loopline ;

    loopline = thelines[ loop ] ;
    nloop = loopline->next->next ;
    ntheone = theline->next->next ;

    /* Do not try to intersect with the same line, any horizontal line, or
       any line with which we have already been marked colinear. We only do
       single intersection testing (i.e., check that the points on the loop
       line are either side of the reference line, and not vice-versa). If
       the Y intersection turns out to be beyond the end of the reference
       line it will be ignored anyway, the miny test ensures that. Using the
       cross product as a colinearity test can fail if the Y distance is very
       small, so we test if the projection of one vector onto another yields
       a close enough coordinate. */
    if ( loop != theone &&
         LINELIST_ORIENT(loopline) != 0 && /* Ignore horizontals */
         nloop != ntheone ) {
      CLIPPATH_STATS_INCR(intersection_tests) ;

      x1 = theX(thePoint(*loopline)) ;
      HQASSERT(theY(thePoint(*loopline)) == py,
               "Lines do not start at same Y") ;
      tmpline = loopline->next ;
      x2 = theX(thePoint(*tmpline)) ;
      y2 = theY(thePoint(*tmpline)) ;
      sx = x2 - x1 ;
      sy = y2 - py ;
      HQASSERT(sy > 0, "Line is not Y ordered") ;
      sdx = x1 - px ; /* Difference in start X coordinates */
      r1 = sdx * dy ;
      r2 = (x2 - px) * dy - sy * dx ;
      r3 = (nx - x1) * sy - dy * sx ;
      if ( sdx <= COLINEARITY_EPSILON && sdx >= -COLINEARITY_EPSILON &&
           r2 <= dy * COLINEARITY_EPSILON && -r2 <= dy * COLINEARITY_EPSILON &&
           r3 <= sy * COLINEARITY_EPSILON && -r3 <= sy * COLINEARITY_EPSILON
#ifdef DEBUG_BUILD
           && (!CLIPPATH_DBG(CLIPPATH_DBG_NO_MERGE) || r1 * r2 == 0.0)
#endif
           ) {
        CLIPPATH_STATS_INCR(colinear) ;

       /* Colinear lines. Make them both share the intersection point of the
           lower numbered line. */
        if ( loop < theone ) {
          if ( theLineOrder(*nloop) < 255 ) {
            ++theLineOrder(*nloop) ;
            if ( theLineOrder(*ntheone)-- == 0 )
              free_line(ntheone, mm_pool_temp) ;
            thelines[theone]->next->next = nloop ;
          }
        } else {
          HQASSERT(loop > theone, "Line intersected with itself") ;
          if ( theLineOrder(*ntheone) < 255 ) {
            ++theLineOrder(*ntheone) ;
            if ( theLineOrder(*nloop)-- == 0 )
              free_line(nloop, mm_pool_temp) ;
            thelines[loop]->next->next = ntheone ;
          }
        }

        HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_INTERSECT),
                ("clippath lines %d and %d colinear: (%f,%f)-(%f,%f) (%f,%f)-(%f,%f)",
                 theone, loop, px, py, nx, ny, x1, py, x2, y2)) ;
      } else if ( r1 * r2 < 0 ) {
        SYSTEMVALUE determinant = dx * sy - sx * dy ;
        SYSTEMVALUE t ;

        HQASSERT(determinant != 0.0, "Determinant indicates colinear lines") ;

        CLIPPATH_STATS_INCR(intersections) ;

        /* Find out which line ends first */
        if ( y2 > ny )
          y2 = ny ;

        /* Calculate parameter for the intersection. Calculated intersections
           which fall within the minimum scanbeam distance from the current
           scanline will be rounded up to the minimum scanbeam distance, or
           the distance to the closest starting or ending line after the
           current scanline. */
        t = r1 / determinant ;

        sy = CP_ROUND_UP(py + sy * t) ;
        sx = CP_ROUND(x1 + sx * t) ;
        if ( sy <= scanline + MINIMUM_SCANBEAM ) {
          CLIPPATH_STATS_INCR(start_intersections) ;
          HQASSERT(MINIMUM_SCANBEAM > 0.0,
                   "Line intersection must be greater than scanline") ;
          HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_ADJUST),
                  ("clippath scanbeam adjustment lines %d and %d: (%f,%f)-(%f,%f) (%f,%f)-(%f,%f) intersect (%f,%f) adjusted to (%f,%f)",
                   theone, loop, px, py, nx, ny, x1, py,
                   x2, theY(thePoint(*loopline->next)),
                   sx, sy, sx,
                   (y2 < scanline + MINIMUM_SCANBEAM ? y2 : scanline + MINIMUM_SCANBEAM))) ;
          sy = scanline + MINIMUM_SCANBEAM ;
          if ( y2 < sy )
            sy = y2 ;
        }

        HQASSERT(sy > py, "Intersection at start of line") ;

        /* To avoid calculating the X coordinates of intersections
           differently for the two lines, we note the line with which this
           one intersected, and force the intersection values to be the same
           for both lines. */
        if ( sy < *yval ) {
          HQASSERT(t > 0.0 && t < 1.0, "Intersection parameter out of range") ;

          CLIPPATH_STATS_INCR(closer_intersections) ;

          *yval = sy ;

          HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_INTERSECT),
                  ("clippath lines %d and %d intersect: (%f,%f)-(%f,%f) (%f,%f)-(%f,%f) at (%f,%f)",
                   theone, loop, px, py, nx, ny, x1, py,
                   x2, theY(thePoint(*loopline->next)), sx, sy)) ;
        }

        if ( sy < theY(thePoint(*nloop)) ) {
          theX(thePoint(*nloop)) = sx ;
          theY(thePoint(*nloop)) = sy ;

          HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_INTERSECT),
                  ("clippath line %d updated: (%f,%f)-(%f,%f) at (%f,%f)",
                   loop, x1, py, x2, theY(thePoint(*loopline->next)),
                   sx, sy)) ;
        }

        if ( sy < theY(thePoint(*ntheone)) ) {
          theX(thePoint(*ntheone)) = sx ;
          theY(thePoint(*ntheone)) = sy ;

          HQTRACE(CLIPPATH_DBG(CLIPPATH_DBG_TRACE_INTERSECT),
                  ("clippath line %d updated: (%f,%f)-(%f,%f) at (%f,%f)",
                   theone, px, py, nx, ny, sx, sy)) ;
        }
      }
    }
  }
}

static void freeany( LINELIST **thelines, int32 start, int32 end )
{
  while ( start < end ) {
    LINELIST *l1 , *l2 ;

    l1 = thelines[ start ] ;
    l2 = l1->next ;
    free_line( l1, mm_pool_temp ) ;
    l1 = l2->next ;
    free_line( l2, mm_pool_temp ) ;

    if ( l1 && theLineOrder(*l1)-- == 0 )
      free_line(l1, mm_pool_temp) ; /* Intersection lines are refcounted */

    ++start ;
  }
  mm_free_with_header(mm_pool_temp, thelines) ;
}

Bool make_devicebounds_path(PATHINFO *outpath)
{
  return make_device_clippath(theISaveLangLevel(workingsave) == 1 &&
                              CURRENT_DEVICE() == DEVICE_NULL,
                              outpath) ;
}

/* Check for point colinear with line. This includes degenerate line segments;
   the middle point will be removed in these cases. */
static Bool point_colinear(SYSTEMVALUE x, SYSTEMVALUE y, LINELIST *line)
{
  LINELIST *next ;
  SYSTEMVALUE sx, sy, ex, ey, cross ;

  HQASSERT(line, "No line to test point against") ;
  next = line->next ;
  HQASSERT(next, "No second point on line to test against") ;

  /* Cross product == 0 gives colinear */
  sx = theX(thePoint(*line)) ;
  sy = theY(thePoint(*line)) ;
  ex = theX(thePoint(*next)) ;
  ey = theY(thePoint(*next)) ;

  cross = (x - sx) * (ey - sy) - (y - sy) * (ex - sx) ;

  /* The magnitude of the cross product of vector a and b is
     |a||b||sin(theta)|. If this is less than EPSILON, then either |a| is very
     small, |b| is very small, or |sin(theta)| is very small. In any of these
     cases, we can ignore the middle point and pretend they are colinear. */
#ifdef DEBUG_BUILD
  if ( !CLIPPATH_DBG(CLIPPATH_DBG_NO_COLINEAR) )
#endif
    if ( cross <= COLINEARITY_CROSS_EPSILON &&
         cross >= -COLINEARITY_CROSS_EPSILON )
      return TRUE ;

  return FALSE ;
}

void init_C_globals_clippath(void)
{
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  clippath_debug = 0 ;
#endif
}

/*
Log stripped */
