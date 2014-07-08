/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:stroker.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stroker algorithm to convert paths into their stroked outlines.
 * See document in notes : "Stroking algorithm and formulae" for more details.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "basemap.h"

#include "mathfunc.h"
#include "constant.h"
#include "params.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "ndisplay.h"
#include "graphics.h"
#include "gstack.h"
#include "stacks.h"
#include "routedev.h"
#include "plotops.h"

#include "gstate.h"
#include "pathops.h"
#include "gu_fills.h"
#include "gu_path.h"
#include "system.h"
#include "dl_bres.h"
#include "fbezier.h"
#include "monitor.h"
#include "ripdebug.h"
#include "namedef_.h"

#include "wclip.h"
#include "stroker.h"

#if defined(DEBUG_BUILD)

int32 debug_stroke = 0;

/*
 * Initialse stroke debug
 */
void init_stroke_debug(void)
{
  register_ripvar(NAME_debug_stroke, OINTEGER, &debug_stroke);
}

#define DEBUG_STROKE(_x, _y) if ((debug_stroke & (_x)) != 0 ) { _y; }

#else /* !DEBUG_BUILD */

#define DEBUG_STROKE(_x, _y) /* Do nothing */

#endif /* DEBUG_BUILD */

/*
 * There are three sets of stroke callback methods defined below,
 * each of which can be optionally modified to deal with the 0 linewidth
 * case, giving a total of 6 different possibilities.
 * The three callbacks sets are
 *  a) Outline stroke methods : osm_XXX
 *     Used for strokepath and non-font strokes. May be issues with bits of
 *     resulting stroke path interacting with each other, so need to ensure
 *     the output gets rendered under the umbrella of a single "nfill". This
 *     ensures we get no self-compositing.
 *  b) Poor stroke methods : psm_XXX
 *     Used in the case we want backwards Adobe compatibility, and need
 *     to output the same stroke sections as they do. Used when doing
 *     strokepath and SystemParams.PoorStrokepath is true.
 *  c) Quick stroke methods : qsm_XXX
 *     Used for the case of rendering glyphs into the cache, where we
 *     do not need to worry about stroke segments interacting with each-other.
 *     So each stroke outline primitive (quad/triangle/etc.) can be rendered
 *     independenly there and then.
 */

/*
 * Set of stroker() callbacks for the typical case where we want either the
 * complete output stroked path filled, or we wish to return the outline for
 * the PS operator 'strokepath'.
 * We just save the paths we are given on sideA and sideB separately,
 * and at the end the path made-up from accumulated sideA, and
 * sideB reversed, will give us the complete outline.
 * Get the stroke_begin method to create a structure to manage these
 * incremental sideA/sideB records, and return in in the *data field.
 * All other methods can add to it, and the end method can process and
 * return it in the appropriate format.
 * Note : Doing it this way is advantageous, it minimises the number of
 * vectors and sub-paths that need to be processed by the fill code.
 * However, this is not always possible. If we have a single unbroken huge
 * path, we will run out of memory for storing the output stroked path. So
 * it is necessary to have the ability also to close off what we have done
 * so far, pass it to the nfill code, and then carry on making a new subpath.
 * This is managed by the osm_flush_path() routine.
 */

/*
 * Structure for holding all the local state information for the osm_XXX
 * methods.
 */
typedef struct
{
  uint32 sema;   /**< Handle on the memory we are storing the output points in. */
  PATHLIST *path;             /**< First path in output stroked path. */
  LINELIST *startline;        /**< First linelist of output stroked path. */
  LINELIST *endline;          /**< End of output stroked path. */

  LINELIST *sideA, *sideB;    /**< Track where to put next bit of side A and B. */
  Bool had_curve, hadA, hadB; /**< Have we had a curve, any A or any B yet? */
  int32 had_miter;            /**< If we have had a miter, may need to throw
                                   away start of next quad we see. */
  Bool did_flush;             /**< Have we flushed path out early as we ran
                                   out of output stroked path memory? */

  nfill_builder_t *nbuild ;   /**< Structure to build NFILL details. */
  int32 fill_size;            /**< size of NFILL promise. */
} OSM_PATH;

#define MIN_STROKE_POINTS 256 /* need space for at least this many points */
#define THRESH_STROKE_POINTS 16 /* max# points added each time round the loop */

static Bool osm_stroke_begin(STROKE_PARAMS *sp, void **data, int32 ntries)
{
  static OSM_PATH osmp;
  Bool result = TRUE;
  uint32 size;
  void *base ;

  UNUSED_PARAM(STROKE_PARAMS *, sp);

  osmp.sema  = 0;
  osmp.path  = NULL;
  osmp.startline = NULL;
  osmp.endline = NULL;

  osmp.sideA = NULL;
  osmp.sideB = NULL;
  osmp.had_curve = FALSE;
  osmp.hadA      = FALSE;
  osmp.hadB      = FALSE;
  osmp.had_miter = 0;
  osmp.fill_size = 0;
  osmp.did_flush = FALSE;
  osmp.nbuild    = NULL;

  /*
   * Need a buffer to pass the stroker output through to the renderer.
   * Algorithm will cope with running out of space in this buffer, it
   * just passes the output path through in chunks. However if the
   * buffer size available drops below a minimum, the code can no longer
   * cope. So if minimum is not available, throw a VMERROR.
   *
   * Note : performance degrades and quality "changes" if you make the
   * buffer smaller. The code may no longer have enough context to determine
   * internal miter overlaps, and will have to overlay the stroke quads
   * instead. Mathematically this should be identical, but differences in
   * rounding may lead to very slight pixel differences.
   *
   * Code at the moment uses all of the basemap available, but it could be
   * restricted to a smaller amount down to the specified minimum size. But
   * this will lead to the performance and quality issues noted above.
   */
  if ( (osmp.sema = get_basemap_semaphore(&base, &size)) == 0 )
    return error_handler(VMERROR);

  osmp.nbuild = base ;

  {
    char *ptr = (char *)osmp.nbuild + nfill_builder_size() ;
    osmp.path = PTR_ALIGN_UP_P2(PATHLIST *, ptr, sizeof(uintptr_t)) ;
  }

  {
    char *ptr = (char *)(osmp.path + 1) ;
    osmp.startline = PTR_ALIGN_UP_P2(LINELIST *, ptr, sizeof(SYSTEMVALUE)) ;
  }

  {
    /* Leave a couple of extra points space at the end for closepath
       processing. */
    char *ptr = (char *)base + size ;
    osmp.endline = PTR_ALIGN_DOWN_P2(LINELIST *, ptr, sizeof(SYSTEMVALUE)) - 2;
  }

  /*
   * Allow for MIN_STROKE_POINTS plus a couple extra at each end for
   * closepath processing.
   * N.B. Do size test explicitly with (char *) casts, as some versions of
   * gcc compiler get it wrong if we try and do struct pointer comparisons.
   */
  if ( ((char *)osmp.endline - (char *)osmp.startline) <
       ((MIN_STROKE_POINTS + 4)*sizeof(LINELIST)) )
    result = error_handler(VMERROR);

  if ( result )
  {
    if ( sp->strokedpath != NULL )
    {
      if ( ntries > 0) /* strokepath cannot retry */
        result = FALSE;
    }
    else
    {
      osmp.fill_size = (62*1024) * (1<<ntries);
      if ( ntries > 10) /* Upper limit, stop it asking for silly memory */
        result = error_handler(VMERROR);
      else
        start_nfill(sp->page, osmp.nbuild, osmp.fill_size, NFILL_ISSTRK);
    }
  }

  if ( !result )
  {
    /* Need to tidy-up on error, as error in begin method means end method
       not called */
    if ( osmp.sema != 0 )
      free_basemap_semaphore(osmp.sema);
    osmp.sema = 0;
    *data = NULL;
  }
  else
    *data = (void *)&osmp;

  return result;
}

/**
 * Determine whether the stroke has a thin width compared with the total
 * object bounding box.
 *
 * Applying rollovers to nfills with large bounding boxes but sparse pixel
 * coverage was shown to to a performance slow-down. So add a heuristic to
 * try and determine when such sparse nfills occurs so that rollovers can
 * be disabled. Easiest heuristic is to just mark stroke objects which have
 * thin widths compared with the total bounding box size. Work out the ratio
 * of maximum stroke width in pixels to the minimum of bounding box width and
 * depth. if this is less than 10% then mark the object as 'thin'.
 */
static Bool is_thin_stroke(STROKE_PARAMS *sp, NFILLOBJECT *nfill)
{
  int32 i , j;
  dbbox_t bbox;
  SYSTEMVALUE maxwidth = 0.0, mindim;

  HQASSERT(sp && nfill, "Bad parameters to cacluate thin stroke\n");

  bbox_nfill(nfill, NULL, &bbox, NULL);

  for ( i = 0; i < 2; ++i )
    for ( j = 0; j < 2; ++j )
      if ( fabs(sp->orig_ctm.matrix[i][j]) > maxwidth )
        maxwidth = fabs(sp->orig_ctm.matrix[i][j]);

  maxwidth *= sp->lby2 * 2.0;
  if ( maxwidth <= 1.0 )
    maxwidth = 1.0;

  mindim = (SYSTEMVALUE)(bbox.x2-bbox.x1);
  if ( mindim > (SYSTEMVALUE)(bbox.y2-bbox.y1) )
    mindim = (SYSTEMVALUE)(bbox.y2-bbox.y1);
  if ( mindim <= 1.0 )
    mindim = 1.0;

  return (maxwidth*10 < mindim);
}

static Bool osm_stroke_end(STROKE_PARAMS *sp, Bool result, void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  if ( sp->strokedpath != NULL )
  {
    /* nothing to do */
  }
  else
  {
    if ( result )
    {
      NFILLOBJECT *nfill ;
      int32 nfill_flags = NZFILL_TYPE;

      result = complete_nfill(osmp->nbuild, &nfill);
      if ( result && nfill )
      {
        if ( is_thin_stroke(sp, nfill) )
          nfill_flags |= SPARSE_NFILL;

        result = DEVICE_BRESSFILL(sp->page, nfill_flags, nfill);
      }
    }
    end_nfill(osmp->nbuild);
  }
  if ( osmp->sema != 0 )
    free_basemap_semaphore(osmp->sema);
  return result;
}

static inline void osm_init_path(PATHLIST *path, LINELIST *subpath)
{
  path->systemalloc = PATHTYPE_STRUCT;
  path->order = 0;
  path->next = NULL;
  path->subpath = subpath;
}

static inline void osm_new_subpath(OSM_PATH *osmp)
{
  PATHLIST *path = osmp->path;

  osm_init_path(path, osmp->startline);

  osmp->sideA = path->subpath;
  osmp->sideB = osmp->endline - 1; /* Leave a space for final close */
  osmp->had_miter = 0;
  osmp->hadA = osmp->hadB = FALSE;

  HQASSERT(osmp->sideA <= osmp->sideB - MIN_STROKE_POINTS,
      "Stroker needs a minimum point buffer");
}

static inline void osm_putpoint(LINELIST *line, FPOINT *pt, uint8 type)
{
  line->point = *pt;
  line->systemalloc = PATHTYPE_STRUCT;
  line->type = type;
  line->order = 0;
  line->flags = 0;
  line->next  = line+1;
}

static void osm_complete_closed_path(OSM_PATH *osmp)
{
  PATHLIST *path = osmp->path;

  /*
   * One sub-path is enough.
   * Link A and B together (if A is present). Add a closepath at the end
   * of B (we have left a space), and we are done.
   */
  if ( osmp->hadA ) /* If some sideA, link it to sideB */
    osmp->sideA[-1].next = osmp->sideB+1;
  else /* subpath starts with B */
    path->subpath = osmp->sideB;

  osm_putpoint(osmp->endline, &(path->subpath->point),(uint8)(CLOSEPATH));
  osmp->endline->next = NULL; /* end of linelist */
}

static void inline osm_putA(OSM_PATH *osmp, FPOINT *pt)
{
  LINELIST *last = osmp->sideA - 1;

  if ( !(osmp->hadA) )
  {
    osm_putpoint(osmp->sideA++, pt, MOVETO);
    osmp->hadA = TRUE;
  }
  else if ( last->point.x != pt->x || last->point.y != pt->y )
    osm_putpoint(osmp->sideA++, pt, LINETO);
}

static void inline osm_putB(OSM_PATH *osmp, FPOINT *pt)
{
  LINELIST *last = osmp->sideB + 1;

  if ( !(osmp->hadB) )
  {
    osm_putpoint(osmp->sideB--, pt, LINETO);
    osmp->hadB = TRUE;
  }
  else if ( last->point.x != pt->x || last->point.y != pt->y )
    osm_putpoint(osmp->sideB--, pt, LINETO);
}

#if defined(DEBUG_BUILD)
void stroker_dump_path(PATHLIST *path, int32 retval, int32 force)
{

  monitorf((uint8 *)"%% stroker_dump_path %d %d\n", retval, force);
  monitorf((uint8 *)"%% %d %d\n", sizeof(*path), sizeof(LINELIST));
  for ( ; path != NULL; path = path->next )
  {
    LINELIST *line = path->subpath;

    for ( ; line != NULL; line = line->next )
    {
      FPOINT *pt = &(line->point);

      switch ( line->type )
      {
        case MOVETO:
          monitorf((uint8 *)"%f %f moveto\n",pt->x,pt->y);
          break;
        case LINETO:
          monitorf((uint8 *)"%f %f lineto\n",pt->x,pt->y);
          break;
        case CURVETO:
          monitorf((uint8 *)"%f %f ",pt->x,pt->y);
          line = line->next;
          pt = &(line->point);
          monitorf((uint8 *)"%f %f ",pt->x,pt->y);
          line = line->next;
          pt = &(line->point);
          monitorf((uint8 *)"%f %f curveto\n",pt->x,pt->y);
          break;
        case CLOSEPATH:
          monitorf((uint8 *)"closepath\n");
          break;
        default:
          monitorf((uint8 *)"%% Unknown pathtype %d\n",line->type);
          break;
      }
    }
  }
}
#endif /* DEBUG_BUILD */

/*
 * If we are running short of space for the path, flush it through
 * to the path renderer.
 */
static inline Bool osm_flush_path(OSM_PATH *osmp, STROKE_PARAMS *sp, Bool force)
{
  Bool retval;

  /*
   * Have a safety margin so we have a bit of overhead if we need
   * to flush out a partially completed path. Also this means we do
   * not need to test for overflow quite so frequently.
   */
  if ( ( osmp->sideA < osmp->sideB - THRESH_STROKE_POINTS ) && (!force) )
    retval = TRUE;
  else
  {
    Bool havePath = (osmp->hadA||osmp->hadB);

    HQASSERT(osmp->sideA < osmp->sideB,"stroke path buffers have crashed");

    if ( !havePath )
    {
      HQASSERT(force,"Run out of stroke path memory, but no path");
      /* Got to end of segment, but no path produced : automatic success */
      retval = TRUE;
    }
    else
    {
      FPOINT ptA = {0}, ptB = {0}; /* Remember end of sides A and B */

      if ( !force ) /* Have run out of space half way through making segment */
      {
        HQASSERT((osmp->hadA && osmp->hadB),
            "Ran out of stroke path memory with points only on 1 side");

        /* remember end of current path */
        ptA = osmp->sideA[-1].point;
        ptB = osmp->sideB[1].point;
        osm_complete_closed_path(osmp); /* Finish what we have got so far */
      }

      if ( sp->strokedpath != NULL )
      {
        PATHLIST *path = osmp->path;
        PATHLIST *next = path->next;
        PATHINFO *dest = &(sp->outputpath);

        /*
         * strokepath, so copy the 1 or 2 subpaths we have made to the end
         * of the final resulting path.
         */
        HQASSERT(path->next == NULL || path->next->next == NULL,
            "stroker should only emit max of 2 subpaths");
        path->next = NULL;
        retval = path_append_subpath_copy(path, dest, mm_pool_temp);
        if ( retval && next )
          retval = path_append_subpath_copy(next, dest, mm_pool_temp);
        dest->curved |= osmp->had_curve;
      }
      else
      {
        /*
         * Pass the outline subpath we have generated through to the fill code.
         */
        retval = addpath2_nfill(osmp->nbuild, osmp->path);

        DEBUG_STROKE(DEBUG_STROKE_DUMP_PATH,
                     stroker_dump_path(osmp->path,retval,force));
      }

      if ( !force ) /* Start a new path where the old one left off */
      {
        /*
         * Throw away what we have already passed to fill, and then start a
         * new path with the end points of the old path.
         */
        osm_new_subpath(osmp);
        osm_putA(osmp, &ptA);
        osm_putB(osmp, &ptB);
        osmp->did_flush = TRUE;
      }
    }
  }
  return retval;
}

static Bool osm_segment_begin(STROKE_PARAMS *sp, void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  UNUSED_PARAM(STROKE_PARAMS *, sp);

  osm_new_subpath(osmp);
  osmp->did_flush = FALSE;
  return TRUE;
}

static Bool osm_segment_end(STROKE_PARAMS *sp, Bool join_AtoB, void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;
  Bool someA, someB;

  /*
   * Have been instructed to join sides A and B together, or not, via the
   * bool 'join_AtoB'. But, either or both of paths A and B may be empty.
   * If both A and B are absent, nothing to do, so throw away the subpath
   * structure we created ready for the result.
   * If we are not going to join A and B, and both A and B are present,
   * then we will end up with two subpaths, otherwise only one is needed.
   *
   * Also, if we have previously dumped some of the path because we ran
   * out of memory, then we must join A and B together, as it is no longer
   * being dealt with as a single sub-path.
   */
  someA = osmp->hadA; someB = osmp->hadB;
  if ( osmp->did_flush )
    join_AtoB = TRUE;
  if ( ( !someA  ) && ( !someB) )
  {
    /* no elements at all, so nothing to do */
  }
  else if ( (!join_AtoB) && someA && someB )
  {
    PATHLIST *path = osmp->path;
    /*
     * Result is two subpaths. Need to close both A and B, create an
     * extra PATHLIST struct for the B sub-path, and link everything
     * together.
     */
    osm_putpoint(osmp->sideA++, &(path->subpath->point), (uint8)(CLOSEPATH));
    osmp->sideA[-1].next = NULL; /* End side A */

    osm_putpoint(osmp->endline, &(osmp->sideB[1].point), (uint8)(CLOSEPATH));
    osmp->sideB[1].type = MOVETO;
    osmp->endline->next = NULL; /* End side B */

    path->next = (PATHLIST *)(osmp->sideA); /* next bit of free space */
    path = path->next;
    osm_init_path(path, osmp->sideB+1);
  }
  else
    osm_complete_closed_path(osmp);

  return osm_flush_path(osmp, sp, TRUE);
}

static Bool osm_quad(STROKE_PARAMS *sp, FPOINT pts[4], void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  /*
   * Add the two sideA and two sideB points we have been given for this quad.
   * May have added an internal miter, in which case we can ignore the first
   * point on the side of the internal miter.
   */
  if ( osmp->had_miter != 1 )
    osm_putA(osmp, &pts[0]);
  osm_putpoint(osmp->sideA++, &pts[1], LINETO);

  if ( osmp->had_miter != 2 )
    osm_putB(osmp, &pts[3]);
  osm_putpoint(osmp->sideB--, &pts[2], LINETO);

  osmp->had_miter  = 0;

  return osm_flush_path(osmp, sp, FALSE);
}

static Bool osm_triangle(STROKE_PARAMS *sp, FPOINT pts[3], Bool side_A, void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  UNUSED_PARAM(STROKE_PARAMS *, sp);

  if ( side_A )
  {
    osm_putA(osmp, &pts[0]);
    osm_putpoint(osmp->sideA++, &pts[1], LINETO);
  }
  else
  {
    osm_putB(osmp, &pts[0]);
    osm_putpoint(osmp->sideB--, &pts[1], LINETO);
  }
  return TRUE;
}

static Bool osm_curve(STROKE_PARAMS *sp, FPOINT pts[5], Bool side_A, void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  UNUSED_PARAM(STROKE_PARAMS *, sp);

  if ( side_A )
  {
    osm_putA(osmp, &pts[0]);
    osm_putpoint(osmp->sideA++, &pts[1], CURVETO);
    osm_putpoint(osmp->sideA++, &pts[2], CURVETO);
    osm_putpoint(osmp->sideA++, &pts[3], CURVETO);
  }
  else
  {
    /** \todo @@@ TODO FIXME bmj 24-11-2006: deal with co-incident points */
    osm_putpoint(osmp->sideB--, &pts[0], CURVETO);
    osm_putpoint(osmp->sideB--, &pts[1], CURVETO);
    osm_putpoint(osmp->sideB--, &pts[2], CURVETO);
    osm_putpoint(osmp->sideB--, &pts[3], LINETO);
    osmp->hadB = TRUE;
  }
  osmp->had_curve = TRUE;
  return TRUE;
}

static Bool osm_line(STROKE_PARAMS *sp, FPOINT pts[2], void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  osm_putA(osmp, &pts[0]);
  osm_putpoint(osmp->sideA++, &pts[1], LINETO);

  osm_putB(osmp, &pts[0]);
  osm_putpoint(osmp->sideB--, &pts[1], LINETO);

  return osm_flush_path(osmp, sp, FALSE);
}

static Bool osm_miter(STROKE_PARAMS *sp, FPOINT pts[1], Bool side_A, Bool intersect,
               void *data)
{
  OSM_PATH *osmp = (OSM_PATH *)data;

  UNUSED_PARAM(STROKE_PARAMS *, sp);

  /*
   * Two cases. If its an internal miter, it can replace the two end points
   * on the side specified. Have only been given one of those points so far.
   * So can replace that and set a flag to tell next quad call to throw away
   * its first point too.
   * If its not internal, they we just add it to the side indicated.
   */
  if ( intersect )
  {
    if ( side_A )
      osm_putpoint(osmp->sideA-1, &pts[0], osmp->sideA[-1].type);
    else
      osm_putpoint(osmp->sideB+1, &pts[0], osmp->sideB[1].type);
    /*
     * Remember which side this internal miter was on, so next quad can throw
     * away its first point.
     */
    osmp->had_miter = side_A ? 1 : 2;
  }
  else
  {
    if ( side_A )
      osm_putpoint(osmp->sideA++, &pts[0], LINETO);
    else
      osm_putpoint(osmp->sideB--, &pts[0], LINETO);
  }
  return TRUE;
}

/*
 * Set of stroker() callbacks for the case we are returning the stroked
 * outline, e.g. to the PS operator 'strokepath', but in addition
 * PoorStrokePath is true, so we have to generate the Adobe compatible
 * segments of path representing each quad/triangle/circle.
 *
 * Maintain data local to the callbacks in PSM_DATA.
 * Do two special bits of processing in order to keep the core code clean,
 * but ensure we output Adobe-compatible paths.
 * A) Save triangles until we get two, and then check if they abut. If so,
 *    then output a kite-shaped quad, instead of the two triangles, to get
 *    compatible line-joins.
 * B) Save curve callbacks until we get four, and then output a full circle,
 *    rather than four semi-circles, again for compatibility.
 */

typedef struct
{
  int32 nquads;
  int32 ntriangles;
  FPOINT pts[3];
  Bool side_A;
} PSM_DATA;

static Bool psm_stroke_begin(STROKE_PARAMS *sp, void **data, int32 ntries)
{
  static PSM_DATA psm_data;

  UNUSED_PARAM(STROKE_PARAMS *, sp);
  UNUSED_PARAM(int32, ntries);
  psm_data.nquads     = 0;
  psm_data.ntriangles = 0;
  *data = (void *)&psm_data;
  return TRUE;
}

static Bool psm_do_triangle(STROKE_PARAMS *sp, FPOINT pts[3], Bool side_A)
{
  PATHINFO *dest = &(sp->outputpath);

  /*
   * Need to ouput triangles with a consistent clockwise-ness, so reverse
   * order of co-ords on one side of the line.
   */
  if ( side_A )
    return path_add_three( dest, pts[0].x, pts[0].y, pts[1].x, pts[1].y,
                                 pts[2].x, pts[2].y );
  else
    return path_add_three( dest, pts[0].x, pts[0].y, pts[2].x, pts[2].y,
                                 pts[1].x, pts[1].y );
}

static Bool psm_flush_triangle(STROKE_PARAMS *sp, PSM_DATA *psm)
{
  /* If we have a saved-up triangle, then output it. */
  if ( psm->ntriangles > 0 )
  {
    psm->ntriangles = 0;
    return psm_do_triangle(sp, psm->pts, psm->side_A);
  }
  return TRUE;
}

static Bool psm_segment_end(STROKE_PARAMS *sp, Bool join_AtoB, void *data)
{
  PSM_DATA *psm = (PSM_DATA *)data;

  UNUSED_PARAM(Bool, join_AtoB);

  /*
   * May have a triangle queued-up from the final linejoin, so flush
   * it if it is there.
   */
  return psm_flush_triangle(sp, psm);
}

static Bool psm_quad(STROKE_PARAMS *sp, FPOINT pts[4], void *data)
{
  PSM_DATA *psm = (PSM_DATA *)data;
  PATHINFO *dest = &(sp->outputpath);

  return psm_flush_triangle(sp, psm) &&
         path_add_four( dest, pts[0].x, pts[0].y, pts[1].x, pts[1].y,
                              pts[2].x, pts[2].y, pts[3].x, pts[3].y );
}

static Bool psm_triangle(STROKE_PARAMS *sp, FPOINT pts[3], Bool side_A, void *data)
{
  PSM_DATA *psm = (PSM_DATA *)data;

  UNUSED_PARAM(Bool, side_A);

  if ( psm->ntriangles++ == 0 )
  {
    int32 i;

    /*
     * This is our first triangle, so save it in case we get a 2nd
     * abutting one. If no second triangle comes, then we will always
     * get at least another quad, so can flush the triangle then.
     * [ Exception. If the triangle is from the final linejoin, in which
     *   case we flush it in the segment_end method ]
     */
    for ( i=0; i<3; i++ )
      psm->pts[i] = pts[i];
    psm->side_A = side_A;
    return TRUE;
  }
  else
  {
    /* 2nd triangle, does it abut ? */
    if ( psm->pts[2].x == pts[2].x && psm->pts[2].y == pts[2].y &&
         psm->pts[1].x == pts[0].x && psm->pts[1].y == pts[0].y )
    {
      PATHINFO *dest = &(sp->outputpath);

      /* Yes, so output kite instead of two triangles */
      psm->ntriangles = 0;
      /* Need to ensure same clockwise-ness as other quads */
      if ( side_A )
        return path_add_four( dest, psm->pts[0].x, psm->pts[0].y,
                              pts[0].x, pts[0].y, pts[1].x, pts[1].y,
                              pts[2].x, pts[2].y );
      else
        return path_add_four( dest, psm->pts[0].x, psm->pts[0].y,
                              pts[2].x, pts[2].y, pts[1].x, pts[1].y,
                              pts[0].x, pts[0].y );
    }
    else
    {
      /* No, so just output two triangles */
      return psm_flush_triangle(sp, psm) && psm_do_triangle(sp, pts, side_A);
    }
  }
}

static Bool psm_curve(STROKE_PARAMS *sp, FPOINT pts[5], Bool side_A, void *data)
{
  PSM_DATA *psm = (PSM_DATA *)data;
  PATHINFO *dest = &(sp->outputpath);

  UNUSED_PARAM(Bool, side_A);

  /*
   * Callback for curves in the PoorStrokepath case.
   * Know we will only ever get presented with complete circles as a
   * set of 4 callbacks, so save up until we have been called 4 times,
   * and then emit the circle.
   * A bit inelegant, as it relies on knowing the order the four quadrants
   * will be generated, but the simplest solution for this legacy issue.
   */
  switch (psm->nquads++)
  {
    case 0:
      pclosep.point  = pts[3];
      p4cmove.point  = pts[3];
      pcurvew1.point = pts[2];
      pcurvew2.point = pts[1];
      pcurvew3.point = pts[0];
      return TRUE;
    case 1:
      pcurvex1.point = pts[1];
      pcurvex2.point = pts[2];
      pcurvex3.point = pts[3];
      return TRUE;
    case 2:
      pcurvez1.point = pts[2];
      pcurvez2.point = pts[1];
      pcurvez3.point = pts[0];
      return TRUE;
    case 3:
      pcurvey1.point = pts[1];
      pcurvey2.point = pts[2];
      pcurvey3.point = pts[3];
      psm->nquads = 0;
      dest->curved = TRUE;
      return path_append_subpath_copy(&p4curve, dest, mm_pool_temp);
    default:
      HQFAIL("psm_curve calls not in sets of four");
      return FALSE;
  }
}

static Bool psm_line(STROKE_PARAMS *sp, FPOINT pts[2], void *data)
{
  PATHINFO *dest = &(sp->outputpath);

  UNUSED_PARAM(void *, data);

  return path_add_two( dest, pts[0].x, pts[0].y, pts[1].x, pts[1].y );
}

/*
 * Set of stroker() callbacks for the case we are rendering the resulting
 * stroke outline.
 * Render everything in isolation, so need no state.
 * Begin and end callbacks can be omitted, quad, triangle and curve methods
 * can just generate the appropriate DEVICE BRESS calls.
 */

static Bool qsm_quad(STROKE_PARAMS *sp, FPOINT pts[4], void *data)
{
  NFILLOBJECT *nfill;
  LINELIST *ll;

  UNUSED_PARAM(STROKE_PARAMS *, sp);
  UNUSED_PARAM(void *, data);

  ll = &p4move;
  ll->point = pts[0];
  ll = ll->next;
  HQASSERT(ll == &plinex, "Static linelist order has been changed");
  ll->point = pts[1];
  ll = ll->next;
  HQASSERT(ll == &pliney, "Static linelist order has been changed");
  ll->point = pts[2];
  ll = ll->next;
  HQASSERT(ll == &plinez, "Static linelist order has been changed");
  ll->point = pts[3];
  ll = ll->next;
  HQASSERT(ll == &pclosep, "Static linelist order has been changed");
  ll->point = pts[0];
  HQASSERT(ll->next == NULL, "Static linelist order has been changed");

  if ( !make_nfill(sp->page, &p4cpath, NFILL_ISSTRK, &nfill) )
    return FALSE;
  return DEVICE_BRESSFILL(sp->page, EOFILL_TYPE, nfill);
}

static Bool qsm_triangle(STROKE_PARAMS *sp, FPOINT pts[3], Bool side_A, void *data)
{
  NFILLOBJECT *nfill;
  LINELIST *ll;

  UNUSED_PARAM(STROKE_PARAMS *, sp);
  UNUSED_PARAM(Bool, side_A);
  UNUSED_PARAM(void *, data);

  ll = &p3move;
  ll->point = pts[0];
  ll = ll->next;
  HQASSERT(ll == &pliney, "Static linelist order has been changed");
  ll->point = pts[1];
  ll = ll->next;
  HQASSERT(ll == &plinez, "Static linelist order has been changed");
  ll->point = pts[2];
  ll = ll->next;
  HQASSERT(ll == &pclosep, "Static linelist order has been changed");
  ll->point = pts[0];
  HQASSERT(ll->next == NULL, "Static linelist order has been changed");

  if ( !make_nfill(sp->page, &p3cpath, NFILL_ISSTRK, &nfill) )
    return FALSE;
  return DEVICE_BRESSFILL(sp->page, EOFILL_TYPE, nfill);
}

static Bool qsm_curve(STROKE_PARAMS *sp, FPOINT pts[5], Bool side_A, void *data)
{
  NFILLOBJECT *nfill;

  UNUSED_PARAM(STROKE_PARAMS *, sp);
  UNUSED_PARAM(Bool, side_A);
  UNUSED_PARAM(void *, data);

  HQASSERT(p1lcmove.next == &p1lcline &&
           p1lcline.next == &pcurvez1 &&
           pcurvez1.next == &pcurvez2 &&
           pcurvez2.next == &pcurvez3 &&
           pcurvez3.next == &pclosep &&
           pclosep.next == NULL,
           "Static linelist order has been changed") ;
  HQASSERT(theSubPath(p1lcurve) == &p1lcmove,
           "Static pathlist order has been changed") ;

  p1lcmove.point = pclosep.point = pts[4];
  p1lcline.point = pts[0];
  pcurvez1.point = pts[1];
  pcurvez2.point = pts[2];
  pcurvez3.point = pts[3];

  if ( !make_nfill(sp->page, &p1lcurve, NFILL_ISSTRK, &nfill) )
    return FALSE;
  return DEVICE_BRESSFILL(sp->page, EOFILL_TYPE, nfill);
}

static Bool qsm_line(STROKE_PARAMS *sp, FPOINT pts[2], void *data)
{
  NFILLOBJECT *nfill;
  LINELIST *ll;

  UNUSED_PARAM(STROKE_PARAMS *, sp);
  UNUSED_PARAM(void *, data);

  ll = &p2move;
  ll->point = pts[0];
  ll = ll->next;
  HQASSERT(ll == &plinez, "Static linelist order has been changed");
  ll->point = pts[1];
  ll = ll->next;
  HQASSERT(ll == &pclosep, "Static linelist order has been changed");
  ll->point = pts[0];
  HQASSERT(ll->next == NULL, "Static linelist order has been changed");

  if ( !make_nfill(sp->page, &p2cpath, NFILL_ISSTRK, &nfill) )
    return FALSE;
  return DEVICE_BRESSFILL(sp->page, EOFILL_TYPE, nfill);
}

/*
 * Callback methods if we are generating the stroke outline,
 * e.g. for PS "strokepath" operator.
 */
static STROKE_METHODS outline_sm =
{
  osm_stroke_begin,
  osm_stroke_end,
  osm_segment_begin,
  osm_segment_end,
  osm_quad,
  osm_triangle,
  osm_curve,
  osm_line,
  osm_miter
};

/*
 * Callback methods if we are generating the stroke outline,
 * when SystemParams.PoorStrokepath is true.
 */
static STROKE_METHODS pooroutline_sm =
{
  psm_stroke_begin,
  NULL,
  NULL,
  psm_segment_end,
  psm_quad,
  psm_triangle,
  psm_curve,
  psm_line,
  NULL
};

/*
 * Callback methods if we are rendering the stroke outline by
 * calling the low level bressenham rendering algorithm.
 */
static STROKE_METHODS qsm_sm =
{
  NULL,
  NULL,
  NULL,
  NULL,
  qsm_quad,
  qsm_triangle,
  qsm_curve,
  qsm_line,
  NULL
};

/*****************************************************************************/
/****  Main Stroker algorithm below this point, callback methods above    ****/
/*****************************************************************************/
/*
 * Rotate the given vector by 90 degees in user space.
 */
static void inline vector_rot90(FVECTOR *in, FVECTOR *out, STROKE_PARAMS *sp)
{
  FVECTOR t1, t2;
  MATRIX_TRANSFORM_DXY( in->x, in->y, t1.x, t1.y, &(sp->sadj_inv) ) ;
  t2.x = -t1.y;
  t2.y = t1.x;
  MATRIX_TRANSFORM_DXY( t2.x, t2.y, out->x, out->y, &(sp->sadj_ctm) ) ;
}

typedef struct
{
  STROKER_STATE *ss;
  Bool side_A;
  FPOINT tri[3];
} FLATBEZ_INFO;

/*
 * bezchop callback returning flattened list of points
 */
static int32 flatbez_cb(FPOINT *pt, void *data, int32 flags)
{
  FLATBEZ_INFO *fbi = (FLATBEZ_INFO *)data;
  STROKER_STATE *ss = fbi->ss;

  UNUSED_PARAM(int32, flags);

  fbi->tri[0] = fbi->tri[1]; /* next triangle start point */
  fbi->tri[1] = *pt;
  if ( ! ss->sm->triangle(ss->sp, fbi->tri, fbi->side_A , ss->data) )
    return -1;
  return 1;
}

/*
 * Call the curve method callback, if there is one, else do the flattening
 * ourselves and call the triangle method instead.
 */
static Bool stroke_curve(STROKER_STATE *ss, FPOINT pts[5], Bool side_A)
{
  if ( ss->sm->curve )
    return ss->sm->curve(ss->sp, pts, side_A , ss->data) ;
  else /* Else no curve method, so have to do the flattening ourselves. */
  {
    FLATBEZ_INFO fbi;

    fbi.tri[2] = pts[4]; /* offline point of triangle is centre of circle */
    fbi.tri[1] = pts[0];
    fbi.side_A = side_A;
    return bezchop(pts, flatbez_cb, (void *)&fbi, BEZ_POINTS);
  }
}

/*
 * Add a round cap to the end of the given line.
 * This is just two beziers approximating the semi-circle.
 */
static Bool round_cap(STROKER_STATE *ss, POINT_INFO *pt, FVECTOR *norm, int32 dir)
{
  FVECTOR vec;
  SYSTEMVALUE skew = CIRCLE_FACTOR;
  FPOINT pts[5], tmp;
  int32 sec;

  /* mid-point of arc is 90 rotate of normal */
  vector_rot90(norm, &vec, ss->sp);

  for ( sec = -1; sec < 2; sec += 2 ) /* sector -1 and +1 */
  {
    pts[3].x = pt->adj.x + norm->x * sec;
    pts[3].y = pt->adj.y + norm->y * sec;
    pts[2].x = pts[3].x + skew * vec.x * dir;
    pts[2].y = pts[3].y + skew * vec.y * dir;
    pts[0].x = pt->adj.x + vec.x * dir;
    pts[0].y = pt->adj.y + vec.y * dir;
    pts[1].x = pts[0].x + skew * norm->x * sec;
    pts[1].y = pts[0].y + skew * norm->y * sec;
    pts[4].x = pt->adj.x;
    pts[4].y = pt->adj.y;
    if (dir < 0 ) /* swap if going the other way */
    {
      tmp = pts[0]; pts[0] = pts[3]; pts[3] = tmp;
      tmp = pts[1]; pts[1] = pts[2]; pts[2] = tmp;
    }
    if ( ! stroke_curve(ss, pts, sec > 0) )
      return FALSE;
  }
  return TRUE;
}

/*
 * Have PoorStrokepath enabled, so we do round caps and joins by drawing
 * a complete circle at the specified point.
 * Easiest way is to just to do two round linecaps, facing in
 * opposite directions. Know 1st call can never fail, as callback saves up
 * until it has all four quadrants.
 */
static Bool full_circle(STROKER_STATE *ss, POINT_INFO *pt, FVECTOR *norm)
{
  (void)round_cap(ss, pt, norm, 1);
  return round_cap(ss, pt, norm, -1);
}

/*
 * Special case for PS 0-length dash with butt caps.
 * Need to output a hairline.
 */
static Bool hairline_cap(STROKER_STATE *ss, POINT_INFO *pt, FVECTOR *norm)
{
  FPOINT pts[4];

  pts[0].x = pt->adj.x + norm->x;
  pts[0].y = pt->adj.y + norm->y;
  pts[1].x = pts[0].x;
  pts[1].y = pts[0].y;
  pts[2].x = pt->adj.x - norm->x;
  pts[2].y = pt->adj.y - norm->y;
  pts[3].x = pts[2].x;
  pts[3].y = pts[2].y;

  return ss->sm->quad(ss->sp, pts, ss->data);
}

/*
 * Apply the given cap at co-ordinate 'pt'. Norm is the normal to the line
 * which starts/ends at 'pt'. 'dir' tells us if it is a start or end cap, and
 * hence if we need to make it go away or towards the line.
 */
static Bool stroke_cap(STROKER_STATE *ss, POINT_INFO *pt, FVECTOR *norm, int32 dir,
                       uint8 cap)
{
  FPOINT pts[4];

  if ( ss->sm->line != NULL) /* no caps if zero linewidth */
    return TRUE;

  switch ( cap )
  {
    case BUTT_CAP:
      return TRUE;
    case ROUND_CAP:
      if ( ss->poorstrokepath )
        return full_circle(ss, pt, norm);
      else
        return round_cap(ss, pt, norm, dir);
    case SQUARE_CAP:
    case TRIANGLE_CAP:
    {
      FVECTOR vec;
      int32 off = (dir < 0 ) ? 0 : 1;

      /* Rotate the normal to get the line up the middle of the cap */
      vector_rot90(norm, &vec, ss->sp);

      pts[0+off].x = pt->adj.x + norm->x;
      pts[0+off].y = pt->adj.y + norm->y;
      pts[3-off].x = pt->adj.x - norm->x;
      pts[3-off].y = pt->adj.y - norm->y;

      /* A triangle cap base is the same as a square cap base,
       * just make the apex different */
      if ( cap == TRIANGLE_CAP )
      {
        pts[1-off].x = pt->adj.x + vec.x *dir;
        pts[1-off].y = pt->adj.y + vec.y *dir;
        pts[2+off].x = pts[1-off].x;
        pts[2+off].y = pts[1-off].y;
      }
      else
      {
        pts[1-off].x = pts[0+off].x + vec.x * dir;
        pts[1-off].y = pts[0+off].y + vec.y * dir;
        pts[2+off].x = pts[3-off].x + vec.x * dir;
        pts[2+off].y = pts[3-off].y + vec.y * dir;
      }
      return ss->sm->quad(ss->sp, pts, ss->data);
    }
    default :
      HQFAIL("Unexpected linecap value");
      return FALSE;
  }
}

/*
 * See which way the two lines are bending, to the left or right ?
 * This can be calculated from the cross product or the normals.
 * If this is negative, lines are bending to the right, so line join
 * will be on the left. [ orientation in terms of walking along the
 * lines p0 -> p1 -> p2 ]
 * return  0 if co-linear, points in order
 *         2 if co-linear, points doubling back
 *        +1 if join is on the left
 *        -1 if join is on the right
 * However, we may be running with a transform matrix that mirrors things,
 * and have left and right swapped. If axes follow mathematical usage (origin
 * bottom left, x increasing to the right, y increasing upwards), then have
 * ss->axes_mirrored == 1, else its -1. Use this to modify return value
 * appropriately.
 */
static int32 join_on_left(STROKER_STATE *ss)
{
  SYSTEMVALUE bend;

  bend = ss->norm[0].x * ss->norm[1].y -
         ss->norm[1].x * ss->norm[0].y ;
  /*
   * If bend is very small, can safely treat lines as co-linear, as the
   * join will be so small as to not be visible. In fact extremely small
   * values of bend ( ~ 1e-10) cause later maths to become unstable.
   * So filter small values of bend and convert into the co-linear case.
   * A threshold of EPSILON seems as good as any.
   */
  /**
   * \todo @@@ TODO FIXME bmj 16-01-2007: could optimise by making this
   * test more relaxed, and having fewer joins to deal with.
   */
  if ( bend < EPSILON && bend > -EPSILON )
  {
    /* Differentiate between points in an increasing straight line,
     * and points doubling back in opposite direction */
    if ( ss->norm[0].x * ss->norm[1].x < 0.0 ||
         ss->norm[0].y * ss->norm[1].y < 0.0 )
      return 2;
    return 0;
  }
  else if (bend < 0.0)
    return ss->axes_mirrored;
  else
    return -(ss->axes_mirrored);
}

/*
 * Calculate the cosine of half the angle, given the cosine of the angle,
 * using standard half-angle formula.
 */
static SYSTEMVALUE inline calc_CosAby2(SYSTEMVALUE cosA)
{
  SYSTEMVALUE cosAby2;

  cosAby2 = sqrt((1 + cosA)/2.0);
  return cosAby2;
}

/*
 * Calculate twice the sine of half the angle, given the cosine of the angle,
 * using standard half-angle formula.
 */
static SYSTEMVALUE inline calc_2SinAby2(SYSTEMVALUE cosA)
{
  SYSTEMVALUE sinAby2;

  sinAby2 = sqrt((1 - cosA)/2.0);
  return 2.0*sinAby2;
}

/*
 * Calculate what fraction of the way along the vector we need to put
 * the bezier control point to best approxinmate an arc of a circle.
 */
static SYSTEMVALUE calc_bfac(SYSTEMVALUE cosA)
{
  SYSTEMVALUE bfac = 4.0/3.0;
  SYSTEMVALUE cosAby2;

  cosAby2 = calc_CosAby2(cosA);
  /*
   * As cosAby2 tends to one, the formula below becomes unstable,
   * and can result in large errors, or division by zero issues.
   * But for cosAby2 = 1 - s (s very small), the formula can be
   * appoximated as
   *   bfac = (4/3)*sqrt(s/2)
   * which is stable for small s
   * For s < 0.000001, the appoximation is within 0.00001%.
   */
  if ( cosAby2 > 0.999999 )
    bfac = (4.0/3.0)*sqrt((1-cosAby2)/2);
  else
    bfac = (4.0/3.0)*(1.0-cosAby2)/sqrt(1-cosAby2*cosAby2);

  return bfac;
}

/*
 * Round cap/join can be appoximated by the given bezier, so
 * calculate the control points.
 */
static Bool add_stroke_bezier(STROKER_STATE *ss, POINT_INFO *pt, FVECTOR *left,
                       FVECTOR *right, SYSTEMVALUE cosA, int32 onLeft)
{
  SYSTEMVALUE bfac = calc_bfac(cosA);
  FPOINT pts[5];
  FVECTOR l90, r90;

  /* Rotate normals 90 degrees to make bezier tangents. */
  vector_rot90(left,  &l90, ss->sp);
  vector_rot90(right, &r90, ss->sp);

  /*
   * 4 Bezier control points :
   *   centre point + left normal
   *   centre point + left normal  - bfac * rot90(left normal)
   *   centre point + right normal + bfac * rot90(right normal)
   *   centre point + right normal
   */
  pts[0].x = pt->adj.x + onLeft * left->x;
  pts[0].y = pt->adj.y + onLeft * left->y;
  pts[3].x = pt->adj.x + onLeft * right->x;
  pts[3].y = pt->adj.y + onLeft * right->y;
  pts[1].x = pts[0].x  - l90.x * bfac;
  pts[1].y = pts[0].y  - l90.y * bfac;
  pts[2].x = pts[3].x  + r90.x * bfac;
  pts[2].y = pts[3].y  + r90.y * bfac;
  pts[4].x = pt->adj.x;
  pts[4].y = pt->adj.y;

  return stroke_curve(ss, pts, onLeft > 0);
}

/*
 * Add a round join to the two lines specified.
 * We have already calculated if the join needs to go on the left (onleft == 1)
 * to the right (onLeft == -1), or on-top (onLeft == 2) [ lines doubling back
 * on themselves ].
 * Note we are dealing with two angles; the angle between the two lines, and
 * the angle of the linejoin (which is the angle between the two line normals).
 * The two angle obey
 *   angleBetweenLines = 180 - angleOfLinejoin
 * hence cosine of one is minus the cosine of the other.
 * We are passed the (cosine of the) angle of the linejoin.
 */
static Bool add_round_join(STROKER_STATE *ss, int32 onLeft, SYSTEMVALUE cosA)
{
  POINT_INFO *pt = &(ss->p[1]);

  /*
   * Do we need one or two beziers to approximate the round linejoin ?
   * switch over at 90 degrees, i.e. when cosine is zero.
   */
  if ( cosA >= 0.0 )
  {
    /* One bezier will do */
    if ( ! add_stroke_bezier(ss, pt, &(ss->norm[0]), &(ss->norm[1]),
                             cosA, onLeft) )
      return FALSE;
  }
  else
  {
    FVECTOR mid;
    SYSTEMVALUE div = calc_2SinAby2(-cosA);

    /*
     * Need two beziers, so calculate mid point where we can split.
     * 'onLeft == 2' is the special case of a 180 degree linejoin, i.e.
     * two lines doubling back on themselves. Maths blows up so have to
     * treat it as a special case.
     * Also lines may be slightly off doubling back, but the maths may
     * still result in div being zero, so catch this case too.
     */
    if ( onLeft == 2 || div == 0.0 ) /* special case : lines double back */
    {
      /* midpoint is 90degree rotate of normal */
      vector_rot90(&(ss->norm[1]), &mid, ss->sp);
      onLeft = 1;
    }
    else
    {
      /* midpoint lies on sum of normals, div is ratio */
      mid.x = (ss->norm[0].x + ss->norm[1].x)/div;
      mid.y = (ss->norm[0].y + ss->norm[1].y)/div;
    }

    cosA = calc_CosAby2(cosA); /* two beziers, work with half the angle */
    if ( ! add_stroke_bezier(ss, pt, &(ss->norm[0]), &mid, cosA, onLeft) )
      return FALSE;
    if ( ! add_stroke_bezier(ss, pt, &mid, &(ss->norm[1]), cosA, onLeft) )
      return FALSE;
  }
  return TRUE;
}


/* Work out whether the miter is within the miterlimit or not.
 * These calculations are done in userspace, whereas the calc_miter_info
 * calculations below involve strokeadjusted device space too.
 */
Bool miter_within_limit(STROKER_STATE *ss, int32 onLeft)
{
  STROKE_PARAMS *sp  = ss->sp;
  SYSTEMVALUE dx01, dy01, dx12, dy12;
  SYSTEMVALUE xx = 0, yy = 0, mlen = 0, qq, len01, len12;
  Bool retval;

  HQASSERT(onLeft == -1 || onLeft == 1, "Invalid onLeft value");

  MATRIX_TRANSFORM_DXY(ss->p[1].point.x - ss->p[0].point.x,
                       ss->p[1].point.y - ss->p[0].point.y,
                       dx01, dy01, &(sp->orig_inv) ) ;
  MATRIX_TRANSFORM_DXY(ss->p[2].point.x - ss->p[1].point.x,
                       ss->p[2].point.y - ss->p[1].point.y,
                       dx12, dy12, &(sp->orig_inv) ) ;

  qq    = dx12 * dy01 - dx01 * dy12 ;
  len01 = sqrt(( dx01 * dx01 ) + ( dy01 * dy01 )) ;
  len12 = sqrt(( dx12 * dx12 ) + ( dy12 * dy12 )) ;
  HQASSERT( len01 > 0.0 && len12 > 0.0, "Stroker : 0 length vectors ");

  xx = onLeft*sp->lby2*(dx01*len12 - dx12*len01)/qq ;
  yy = onLeft*sp->lby2*(dy01*len12 - dy12*len01)/qq ;
  mlen = sqrt(( xx * xx ) + ( yy * yy )) ;

  retval = ( mlen < sp->lby2 * sp->linestyle.miterlimit) ;
  return retval;
}


/*
 * Calculate the location of the miter between the two lines we have.
 * Return the result in miter[0].
 * But if we are in XPS mode, and the miter is clipped, we need to return
 * the two clipped miter co-ords. Put these in miter[1] amd miter[2].
 * We have already worked out whether the 2 lines bend to the left
 * (onLeft == 1), the right (onLeft == -1), or double back on themselves
 * (onLeft == 2).
 * Return the status of the miter calculation, one of
 * MITER_AT_INFINITY if at infinity (and not required to be clipped )
 * MITER_TOO_LONG    if too long (and not required to be clipped )
 * MITER_OK          if the miter is OK (within miterlimit).
 * MITER_CLIPPED     if the miter was too big, but has been clipped.
 * Also, work out whether we should use the internal miter or not. The
 * 'internal miter' point is the point where the two internal stroke edges
 * cross each-other, on the opposite side to the normal miter. This point
 * can be used instead of the two end stroke points, making a neater (and
 * simpler) stroked path. However, if the lines at this corner are very short,
 * the internal miter point may be outside the stroked shape, so it cannot be
 * used. In this case the line center-point has to be used instead.
 * Does this calculation as well (as it shares the same algebra), and return
 * MITER_INTERNAL if it is safe to use the internal miter point.
 *
 * Also calculate the cosine of the angle between the two lines, as it shares
 * most of the algebra with the miter caclulation, and may be needed later.
 */
static int32 calc_miter_info(STROKER_STATE *ss, FPOINT *miter, int32 onLeft,
                             SYSTEMVALUE *ptr_cosA)
{
  STROKE_PARAMS *sp  = ss->sp;
  SYSTEMVALUE dx01, dy01, dx12, dy12;
  SYSTEMVALUE xx = 0, yy = 0, mlen = 0, qq, len01, len12, cosA;
  int32 retval;

  /** \todo Review the use of the inverse strokeadjusted CTM here,
   * as the inputs appear to be in non-strokeadjusted device space.
   */
  MATRIX_TRANSFORM_DXY(ss->p[1].point.x - ss->p[0].point.x,
                       ss->p[1].point.y - ss->p[0].point.y,
                       dx01, dy01, &(sp->sadj_inv) ) ;
  MATRIX_TRANSFORM_DXY(ss->p[2].point.x - ss->p[1].point.x,
                       ss->p[2].point.y - ss->p[1].point.y,
                       dx12, dy12, &(sp->sadj_inv) ) ;

  qq    = dx12 * dy01 - dx01 * dy12;
  len01 = sqrt(( dx01 * dx01 ) + ( dy01 * dy01 )) ;
  len12 = sqrt(( dx12 * dx12 ) + ( dy12 * dy12 )) ;
  HQASSERT( len01 > 0.0 && len12 > 0.0, "Stroker : 0 length vectors ");

  cosA = -(dy12*dy01 + dx12*dx01)/(len01*len12);
  /*
   * Floating point maths is unstable, may get rounding error causing
   * ABS(cosA) > 1.0. So will need to clip to [-1,+1]. But want to keep
   * an assert, to verify code stability.
   * So assert to [-1-EPSILON,1+EPSILON] and then clip to [-1,+1]
   */
  HQASSERT( cosA <= (1.0+EPSILON) && cosA >= -(1.0+EPSILON),
      "Stroker : bad cosine ");
  if ( cosA < -1.0)
    cosA = -1.0;
  else if ( cosA > 1.0)
    cosA = 1.0;
  *ptr_cosA = cosA;

  if ( onLeft == 2 || fabs(qq) == 0.0) /* miter at infinity */
    retval = MITER_AT_INFINITY;
  else
  {
    xx = onLeft*sp->lby2*(dx01*len12 - dx12*len01)/qq;
    yy = onLeft*sp->lby2*(dy01*len12 - dy12*len01)/qq;

    if ( ss->sp->linestyle.linejoin == TRIANGLE_JOIN ) {
      /* Rescale the vectors to give a triangle join, rather than
         a miter join. 'ratio' is linewidth/miterlength. */
      SYSTEMVALUE ratio = sqrt((1 - cosA) * 0.5) ;
      MATRIX_TRANSFORM_DXY(xx * ratio, yy * ratio, miter[1].x, miter[1].y, &(sp->sadj_ctm) ) ;
    }

    MATRIX_TRANSFORM_DXY(xx, yy, miter->x, miter->y, &(sp->sadj_ctm) ) ;
    mlen = sqrt(( xx * xx ) + ( yy * yy )) ;

    if ( miter_within_limit(ss, onLeft) ||
         ss->sp->linestyle.linejoin == TRIANGLE_JOIN )
      retval = MITER_OK;
    else
      retval = MITER_TOO_LONG;

    /*
     * On the outside of a stroked shape we use miters to fill in gaps
     * between the stroked vectors. On the inside the vectors overlap and
     * results in a stroked outline which kinks back on itself. These kinks
     * are a pain and slow things down, so try and avoid them where possible.
     * This can be done by replacing the kinked section with the anti-miter
     * point. This is OK most of the time, but it is possible (with short
     * lines stroked with a thick linewidth) for this not to work because
     * either
     *   a) The anti-miter point lies outside of the stroked shape or
     *   b) the line lengths are short enough that one stroker vector outline
     *      not only overlaps but projects beyond the end of its neighbour.
     *      This should produce 'ears' in the output, but they will be missing
     *      if we try and use the anti-miter optimisation.
     *
     * So test to see if this internal miter optimisation is OK to do.
     * a) will be OK provided the anti-miter point is within the stroked
     * shape, i.e. if -miter projects back onto both the vectors 0->1
     * and 1->2. This means working out the angle between the two vectors,
     * or using pythagorus. So do a cheaper test first that is less strict,
     * but will catch the majority of cases, only do the stricter test if
     * we have to.
     *
     * Effect b) starts to happen at half the angle that a) occurs at, so
     * we can actually test for both at once by halving the projection test
     * done for a).
     *
     * See document in notes : "Stroking algorithm and formulae" for more
     * technical details on the geometry of this issue.
     *
     * Internal miter definitely OK if miter length is less than both or the
     * line vectors lengths.
     */
    if ( len01 >= mlen && len12 >= mlen )
      retval |= MITER_INTERNAL;
    else
    {
      SYSTEMVALUE plen; /* projected miter length */

      /*
       * Otherwise use stricter test. mlen will project back onto the two
       * vectors, creating right-angle triangles with sides lby2. Use pythag
       * to find projected length, and we are OK provided this is less then
       * actual vector lengths [ or half the lengths for the stricter 'ear'
       * test ].
       *
       * But this is only true for two line segments with an obtuse angle
       * between them. If the angle is acute, the maths all seems to turn
       * upside-down (and the inequalities go the other way around). So
       * you do not get the 'ears' problem at half the angle. So only need
       * the original test in this case.
       *
       * But its just too hard to work out which case we are in, so just do
       * the stronger half angle test all the time. This means we will not
       * get the best possible path for acute angles, but this will be
       * relatively rare (most line segments will come from flattened curves,
       * so the angle between them will be approaching 180, certainly
       * not acute.
       */
      plen = (mlen*mlen) - (sp->lby2*sp->lby2);
      if ( plen < 0.0 )
        plen = 0.0;
      else
        plen = sqrt(plen);
      if ( len01/2 >= plen && len12/2 >= plen )
        retval |= MITER_INTERNAL;
    }
  }

  if ( (retval & (MITER_AT_INFINITY|MITER_TOO_LONG)) &&
       ss->sp->linestyle.linejoin == MITERCLIP_JOIN )
  {
    SYSTEMVALUE ratio;

    if ( ( retval & MITER_AT_INFINITY ) != 0 )
    {
      /* XPS handles lines that double back on themselves (i.e. first and third
       * points are the same) by projecting the lines to infinity and then
       * clipping the intersection area to the miter limit.  PCL/HPGL2 appears
       * to deal with this simply by ignoring it. */
      if ( ( sp->thepath->flags & PATHINFO_XPS ) != 0 ) {
        FVECTOR end;

        vector_rot90(&(ss->norm[1]), &end, sp);
        ratio = sp->linestyle.miterlimit;
        miter[1].x = ss->norm[0].x + ratio * end.x;
        miter[1].y = ss->norm[0].y + ratio * end.y;
        miter[2].x = ss->norm[1].x + ratio * end.x;
        miter[2].y = ss->norm[1].y + ratio * end.y;
        retval &= ~MITER_AT_INFINITY;
        retval |= MITER_CLIPPED;
      }
    }
    else /* MITER_TOO_LONG */
    {
      SYSTEMVALUE blen, xb, yb;

      ratio = 1.0 - (sp->lby2 * sp->linestyle.miterlimit )/mlen;
      blen = ratio * mlen *mlen / sqrt((mlen*mlen) - (sp->lby2 * sp->lby2));
      xb = xx - blen * dx01/len01;
      yb = yy - blen * dy01/len01;
      MATRIX_TRANSFORM_DXY(xb, yb, miter[1].x, miter[1].y, &(sp->sadj_ctm) );
      xb = xx + blen * dx12/len12;
      yb = yy + blen * dy12/len12;
      MATRIX_TRANSFORM_DXY(xb, yb, miter[2].x, miter[2].y, &(sp->sadj_ctm) );
      retval &= ~MITER_TOO_LONG;
      retval |= MITER_CLIPPED;
    }
  }
  return retval;
}

/*
 * Add a bevel join.
 * A simple triangle method callback .
 */
static Bool add_bevel_join(STROKER_STATE *ss, int32 onLeft)
{
  FPOINT pts[3];

  if ( onLeft == 2) /* lines doubling back on themselves */
    return TRUE;

  pts[0].x = ss->p[1].adj.x + onLeft * ss->norm[0].x;
  pts[0].y = ss->p[1].adj.y + onLeft * ss->norm[0].y;
  pts[1].x = ss->p[1].adj.x + onLeft * ss->norm[1].x;
  pts[1].y = ss->p[1].adj.y + onLeft * ss->norm[1].y;
  pts[2].x = ss->p[1].adj.x;
  pts[2].y = ss->p[1].adj.y;

  return ss->sm->triangle(ss->sp, pts, onLeft > 0, ss->data);
}

/*
 * Add an internal miter.
 * Two cases. For very short lines the internal miter ends-up outside
 * the stroked path, so it is not safe to use it. Instead, we have to add
 * the centre point, to create the correct final path. Both cases are passed
 * through the miter callback.
 */
static Bool add_internal_miter(STROKER_STATE *ss, FPOINT *miter, int32 onLeft,
                        Bool use_internal)
{
  FPOINT pt;

  UNUSED_PARAM(FPOINT *, miter);

  HQASSERT( ss->sm->miter, "Stroke - no miter callback") ;
  if ( use_internal )
  {
    pt.x = ss->p[1].adj.x - miter->x;
    pt.y = ss->p[1].adj.y - miter->y;

    return ss->sm->miter(ss->sp, &pt, !(onLeft > 0), TRUE, ss->data);
  }
  else
  {
    pt.x = ss->p[1].adj.x;
    pt.y = ss->p[1].adj.y;

    return ss->sm->miter(ss->sp, &pt, !(onLeft > 0), FALSE, ss->data);
  }
}

/*
 * Add the miter join we have calculated.
 * If clip == FALSE, miter is a single point, i.e. miter[0].
 * If clip == TRUE, miter has been clipped, with two points resulting,
 * i.e. miter[1] and miter[2].
 * 'onLeft' indicates whether the miter is to the left or right of the
 * pair of lines (+1 or -1). But it can also be == 2, which indicates the
 * lines double-back on themselves at 180 degrees. Need to deal with this as
 * a special case as the maths blows up.
 */
static Bool add_miter_join(STROKER_STATE *ss, FPOINT *miter, int32 onLeft, Bool clip)
{
  FPOINT pts[3];
  int32 i;

  if ( clip )
  {
    HQASSERT(ss->sp->linestyle.linejoin != TRIANGLE_JOIN,
             "Shouldn't be clipping a triangle join") ;

    if ( onLeft == 2 ) /* 180 double-back, can be dealt with as normal case */
      onLeft = 1;
    /*
     * Clipped miter results in a trapezoid, which can be broken down into
     * three triangle method callbacks. Build the co-ords carefully to get
     * make sure we get them in the right order.
     */
    /* Same off-path point for all three */
    pts[2].x = ss->p[1].adj.x;
    pts[2].y = ss->p[1].adj.y;
    for ( i = 0; i < 3; i++ )
    {
      if ( i == 0 )
      {
        pts[0].x = ss->p[1].adj.x + onLeft * ss->norm[0].x;
        pts[0].y = ss->p[1].adj.y + onLeft * ss->norm[0].y;
      }
      else
      {
        pts[0].x = pts[1].x;
        pts[0].y = pts[1].y;
      }
      if ( i == 2 )
      {
        pts[1].x = ss->p[1].adj.x + onLeft * ss->norm[1].x;
        pts[1].y = ss->p[1].adj.y + onLeft * ss->norm[1].y;
      }
      else
      {
        pts[1].x = ss->p[1].adj.x + miter[i+1].x;
        pts[1].y = ss->p[1].adj.y + miter[i+1].y;
      }

      if ( ! ss->sm->triangle(ss->sp, pts, onLeft > 0, ss->data) )
        return FALSE;
    }
  }
  else
  {
    if ( ss->sp->linestyle.linejoin == TRIANGLE_JOIN ) {
      /* User the triangle point store in miter[1]. */
      miter = &miter[1] ;
    }

    /*
     * Turn miter into two triangle method callbacks.
     * Have to be careful to build co-ords in correct order
     */
    for ( i = 0; i < 2; i++ )
    {
      pts[i].x   = ss->p[1].adj.x + onLeft * ss->norm[i].x;
      pts[i].y   = ss->p[1].adj.y + onLeft * ss->norm[i].y;
      pts[1-i].x = ss->p[1].adj.x + miter->x;
      pts[1-i].y = ss->p[1].adj.y + miter->y;
      pts[2].x   = ss->p[1].adj.x;
      pts[2].y   = ss->p[1].adj.y;

      if ( ! ss->sm->triangle(ss->sp, pts, onLeft > 0, ss->data) )
        return FALSE;
    }
  }
  return TRUE;
}

/*
 * Work out which cap we should be putting on.
 * 'start' tells us if we are doing the cap for the start or end of a path.
 * If 'force_open' is TRUE, then force the path to be treated as open, as
 * we have a dashed path and its hard to determine its openness beforehand.
 */
static uint8 which_cap(STROKER_STATE *ss, Bool start, Bool force_open)
{
  STROKE_PARAMS *sp  = ss->sp;
  uint8 cap, scap, ecap;
  Bool isclosed;

  /*
   * Work out of path is 'closed' as far as linecaps are concerned. This
   * requires a closed path and either no dashing, or dashing with solid
   * at the start and end of the path.
   * But given we do not know if the path will be solid at the end when we
   * are still at the beginning, we have a problem. So in the dash case,
   * guess that the path is solid at the start and end. This will prevent
   * us putting an initial linecap on, which may turn out to be wrong by the
   * time we get to the end of the path. If so we will comensate for this
   * error then.
   * Also, path not treated as closed if the initial line segment is unstroked,
   * so factor that into the calculation.
   */
  isclosed = (ss->isclosed && (!ss->unstroked.start) && (!force_open));

  scap = sp->linestyle.startlinecap;
  ecap = sp->linestyle.endlinecap;
  /*
   * The XPS viewer uses dash caps at the ends of closed paths even if there
   * is no dashing enabled !? Spec is unclear, so mimic the viewer for now.
   */
  if ( ss->isclosed )
  {
    scap = sp->linestyle.dashlinecap;
    ecap = sp->linestyle.dashlinecap;
  }
  /*
   * Don't do caps for closed paths, unless we have dashes/unstroked segments,
   * and then just do the 'internal' caps, i.e. not the start of the first
   * section or the end of the last section.
   */
  if ( ( ss->section & FIRST_SECTION ) && start )
    cap = (uint8 )(isclosed ? BUTT_CAP : scap);
  else if ( ( ss->section & LAST_SECTION ) && (!start) )
    cap = (uint8)(isclosed ? BUTT_CAP : ecap);
  else
    cap = sp->linestyle.dashlinecap ;

  return cap;
}

/*
 * Record the current unstroked state
 */
static void set_unstroked_state(STROKER_STATE *ss, LINELIST *line)
{
  ss->unstroked.current = ( ( line->flags & LINELIST_UNSTROKED ) != 0);
  ss->unstroked.line |= ss->unstroked.current;
}

/*
 * This is how we used to work out if we were in an internal bit of a bezier
 * or not, pre-flattening set some extra bits in the order field to give us
 * a hint. Should not need this anymore, as we flatten inline with the stroking
 * now. However, still have the case of XPS combined stroke+fill, where we
 * pre-flatten for performance reasons. Leave this code in to support that,
 * for now, but need to decide on a proper solution.
 */
/**
 * \todo @@@ TODO FIXME bmj 11-01-2007: Get rid of the need for this
 */
static void set_bezier_state(STROKER_STATE *ss, LINELIST *p1, LINELIST *p2)
{
  ss->bez_internal = (p1->order != 0 && (p1->order == p2->order));
}

static Bool is_dash(STROKE_PARAMS *sp)
{
  return ( sp->linestyle.dashlistlen != 0 );
}

/*
 * Placeholder function for the old code which tried to force
 * internal bezier joins to be circles.
 * Uses same logic and variables as old code.
 */
static Bool force_round_join(STROKER_STATE *ss)
{
  STROKE_PARAMS *sp  = ss->sp;

  if ( ss->bez_internal )
  {
    if ( ss->adobesetlinejoin && sp->linestyle.flatness < 2.0f )
      return TRUE;
    if ( is_dash(sp) )
      return TRUE;
  }
  return FALSE;
}

/*
 * Do the linejoin between the pair of lines we have.
 */
static Bool stroke_join(STROKER_STATE *ss, Bool finalJoin)
{
  int32 onLeft;
  uint8 join = ss->sp->linestyle.linejoin;
  FPOINT miter[3];
  int32 miterOK = 0;
  Bool result;
  SYSTEMVALUE cosA = 0.0;

  if ( ss->sm->line != NULL ) /* No linejoins for zero-width lines */
    return TRUE;

  if ( ( onLeft = join_on_left(ss) ) == 0) /* Which way are we bending ? */
    return TRUE; /* lines co-linear, nothing to do */

  if ( force_round_join(ss) )
  {
    join = ROUND_JOIN;
    /*
     * Old code for "force_round_join" & PoorStrokepath used to do a
     * flattened bit of a round cap, and convert to triangles.
     * Can't face all that work just for emulating and old legacy case,
     * so do a bevel for now.
     */
    if ( ss->poorstrokepath )
      join = BEVEL_JOIN;
  }
  else if (join == ROUND_JOIN && ss->poorstrokepath )
    return full_circle(ss, &ss->p[1], &ss->norm[0]);

  /*
   * Calculate where the miter co-ordinate is.
   * Need it even if we are non doing a MITER_JOIN, as we may need the
   * internal anti-miter point.
   * Also find the angle between the lines, which is needed for MITER
   * and ROUND joins.
   */
  if (join == MITER_JOIN || join == MITERCLIP_JOIN ||
      join == ROUND_JOIN || join == TRIANGLE_JOIN ||
      ss->sm->miter != NULL )
    miterOK = calc_miter_info(ss, miter, onLeft, &cosA);

  switch ( join )
  {
    case MITER_JOIN:
    case MITERCLIP_JOIN:
    case TRIANGLE_JOIN:
      if (miterOK & MITER_AT_INFINITY) /* Miter at infinity, ignore */
        result = TRUE;
      else if (miterOK & MITER_TOO_LONG) /* miter too big, do bevel */
        result = add_bevel_join(ss, onLeft);
      else
        result = add_miter_join(ss, miter, onLeft, miterOK & MITER_CLIPPED);
      break;
    case BEVEL_JOIN:
      result = add_bevel_join(ss, onLeft);
      break;
    case NONE_JOIN:
      /*
       * We expect to fall into this case only for the joins
       * between internal bezier flattening line segments
       * when drawing curves in a PCLXL path while the line join style is eNoJoin
       * In between the bezier line segments we need to draw a round join
       * to eliminate the gaps that would otherwise be left by the eNoJoin style.
       */
      HQASSERT((ss->sp->thepath->flags & PATHINFO_PCLXL), "NONE_JOIN line join style is only supported for PCLXL eNoJoin paths, and even then only inside bezier curve flattening");
      HQASSERT((ss->bez_internal), "NONE_JOIN join only anticipated during bezier curves, when we do indeed need a round join to eliminate gaps between flattened bezier line segments");
      /*FALLTHROUGH*/

    case ROUND_JOIN:
      /* angle between lines = 180 - angle between normals, so pass -cosine */
      result = add_round_join(ss, onLeft, -cosA);
      break;

    default:
      HQFAIL("Unexpected linejoin value");
      result = FALSE;
      break;
  }

  /*
   * Now do the internal anti-miter point
   */
  if ( result && ss->sm->miter )
  {
    Bool use_internal;

    use_internal = ((miterOK & MITER_INTERNAL) != 0);
    /*
     * Can't use the miter point on the final join, as we have long ago
     * emitted the first quad for the path, and its to late to change it now.
     */
    /**
     * \todo @@@ TODO FIXME bmj 12-12-2006: Can this result in a bug ?
     */
    if ( finalJoin )
      use_internal = FALSE;
    if ( (miterOK & MITER_AT_INFINITY ) == 0 )
      result = add_internal_miter(ss,miter,onLeft, use_internal);
  }
  return result;
}

/*
 * We have to wrap segment begin and end calls around each output segment
 * we generate, if the calls exist.
 */
static Bool wrap_segment(STROKER_STATE *ss, Bool begin, Bool join)
{
  STROKE_PARAMS *sp  = ss->sp;
  Bool ok = TRUE;

  if ( begin )
  {
    if ( ss->sm->segment_begin )
      ok = ss->sm->segment_begin(sp, ss->data);
  }
  else
  {
    if ( ss->sm->segment_end )
      ok = ss->sm->segment_end(sp, join, ss->data);
  }
  return ok;
}

/*
 * Have got to the stage where we have three co-ordinates in our hands,
 * so can start doing some geometry calculations.
 */
static Bool have3points(STROKER_STATE *ss, Bool all3)
{
  STROKE_PARAMS *sp  = ss->sp;
  STROKE_METHODS *sm = ss->sm;
  Bool first = (ss->npoints <= 3);
  FPOINT pts[4];

  /*
   * If its the first point in the path, then call the begin method
   * and put a start cap on.
   */
  if ( first )
  {
    uint8 cap = which_cap(ss, TRUE, FALSE);

    if ( ! wrap_segment(ss, TRUE, TRUE) )
        return FALSE ;

    if ( ! stroke_cap(ss, &ss->p[0], &ss->norm[0], 1, cap ) )
      return FALSE;
  }
  if ( sm->line )
  {
    /*
     * Zero linewidth case, do not bother with line normals, just call
     * the line methods with the on-line points.
     */
    if ( first )
    {
      pts[0].x = ss->p[0].adj.x;
      pts[0].y = ss->p[0].adj.y;
      pts[1].x = ss->p[1].adj.x;
      pts[1].y = ss->p[1].adj.y;

      if ( ! sm->line(sp, pts, ss->data) )
        return FALSE;
    }
    if ( all3 )
    {
      pts[0].x = ss->p[1].adj.x;
      pts[0].y = ss->p[1].adj.y;
      pts[1].x = ss->p[2].adj.x;
      pts[1].y = ss->p[2].adj.y;

      if ( ! sm->line(sp, pts, ss->data) )
        return FALSE;
    }
    return TRUE;
  }
  /*
   * Have three points P0, P1, P2.
   * If its is the first time we have been called, then do
   * both vectors, i.e. P0 -> P1 and P1 -> P2. Otherwise, we just need
   * to do P1 -> P2, as P0 -> P1 will have been dealt with in the
   * previous call.
   */
  if ( first )
  {
    pts[0].x = ss->p[0].adj.x + ss->norm[0].x;
    pts[0].y = ss->p[0].adj.y + ss->norm[0].y;
    pts[1].x = ss->p[1].adj.x + ss->norm[0].x;
    pts[1].y = ss->p[1].adj.y + ss->norm[0].y;
    pts[2].x = ss->p[1].adj.x - ss->norm[0].x;
    pts[2].y = ss->p[1].adj.y - ss->norm[0].y;
    pts[3].x = ss->p[0].adj.x - ss->norm[0].x;
    pts[3].y = ss->p[0].adj.y - ss->norm[0].y;

    if ( ! sm->quad(sp, pts, ss->data) )
      return FALSE;
  }
  if ( all3 ) /* Don't do P1 -> P2 if only two points in the first place */
  {
    if ( ! stroke_join(ss, FALSE) ) /* Join between (P0->P1) and (P1->P2 ) */
      return FALSE;
    pts[0].x = ss->p[1].adj.x + ss->norm[1].x;
    pts[0].y = ss->p[1].adj.y + ss->norm[1].y;
    pts[1].x = ss->p[2].adj.x + ss->norm[1].x;
    pts[1].y = ss->p[2].adj.y + ss->norm[1].y;
    pts[2].x = ss->p[2].adj.x - ss->norm[1].x;
    pts[2].y = ss->p[2].adj.y - ss->norm[1].y;
    pts[3].x = ss->p[1].adj.x - ss->norm[1].x;
    pts[3].y = ss->p[1].adj.y - ss->norm[1].y;
    if ( ! sm->quad(sp, pts, ss->data) )
      return FALSE;
  }
  return TRUE;
}

/*
 * Calculate the normal of length (linewidth/2) to the specified vector.
 */
static void calc_norm(STROKER_STATE *ss, SYSTEMVALUE dx, SYSTEMVALUE dy, int32 nindex)
{
  STROKE_PARAMS *sp = ss->sp;
  SYSTEMVALUE nx, ny, llength, dtemp;

  /* into user space */
  MATRIX_TRANSFORM_DXY( dx, dy, nx, ny, &(sp->sadj_inv) ) ;
  /* length in user space */
  llength = sqrt(( nx * nx ) + ( ny * ny )) ;
  HQASSERT( llength > 0.0 , "Stroke line length : Division by zero") ;
  /* scale to length (linewidth/2) */
  dtemp = sp->lby2 / llength ;
  /* rotate by 90 degrees to get the normal */
  dx = dtemp * ( -ny ) ;
  dy = dtemp * nx ;
  /* And finally back into device space */
  MATRIX_TRANSFORM_DXY( dx, dy, nx, ny, &(sp->sadj_ctm) ) ;
  if ( nindex < 0 ) /* special record of 1st normal in path */
  {
    ss->begin.n1.x = nx;
    ss->begin.n1.y = ny;
  }
  else
  {
    ss->norm[nindex].x = nx;
    ss->norm[nindex].y = ny;
    ss->have_norm = TRUE;
  }
}

/*
 * Store the given point into the POINT_INFO structure, and also
 * calculate the stroke-adjusted co-ordinate.
 */
static void store_point_and_sa(STROKER_STATE *ss, POINT_INFO *pt,
                               SYSTEMVALUE xx, SYSTEMVALUE yy)
{
  STROKE_PARAMS *sp = ss->sp;

  pt->point.x = xx;
  pt->point.y = yy;
  /* Adjust it for 'strokeadjust'
   *
   * Adjusts the coordinates of all the points in the (flattened) path so
   * that they are all at the same places with respect to the pixel grid.
   * Coordinates are already in device space at this time. The points in
   * "closepath" are transformed, even though they are never used, on the
   * basis that it's quicker to do that than to test.
   * The adjustment is "coord = convert-to-device-integer(coord) + 0.25"
   * This means that a 0 setlinewidth stroke adjusted stroke ends up
   * touching the same path as the outline of a fill.
   */
  {
    SYSTEMVALUE tfx = pt->point.x;
    SYSTEMVALUE tfy = pt->point.y;
    if ( ss->sp->strokeadjust )
    {
      SC_C2D_UNTF_I(tfx, tfx, sp->sc_rndx1);
      SC_C2D_UNTF_I(tfy, tfy, sp->sc_rndy1);
    }
    pt->adj.x = tfx + sp->sc_rndx2;
    pt->adj.y = tfy + sp->sc_rndy2;
  }
}

/*
 * Filters for dashing and unstroked segments have occured, now we are
 * presented with a co-ordinate that will be part of the output stroked path.
 * Store it away until we have got three points, and then we can start doing
 * the real calculations.
 */
static Bool add_stroked_point(STROKER_STATE *ss, SYSTEMVALUE xx, SYSTEMVALUE yy,
                              Bool last_corner_only)
{
  STROKE_PARAMS *sp = ss->sp;
  POINT_INFO *pt, *last = NULL;
  int nindex = 0;
  Bool dashing = is_dash(sp);

  /*
   * Get presented with a stream of co-ords. Once we have three, can start
   * processing them. So deal with the special cases of having seen less
   * than three so.
   */
  if ( ss->npoints == 0 ) /* First point, just store it */
    pt = &(ss->p[0]);
  else if (ss->npoints < 3 ) /* 2nd or 3rd point */
  {
    /* Store it in correct slot, and start calculating line normals */
    pt   = &(ss->p[ss->npoints]);
    last = &(ss->p[ss->npoints-1]);
    nindex = ss->npoints-1;
  }
  else
  {
    /* More than 3 points, so shuffle prvious points down. */
    ss->p[0]    = ss->p[1];
    ss->p[1]    = ss->p[2];
    if ( last_corner_only || (!dashing) )
      if ( ss->have_norm )
        ss->norm[0] = ss->norm[1];
    pt          = &(ss->p[2]);
    last        = &(ss->p[1]);
    nindex      = 1;
  }
  /* Had one more point, store it and adjust it for 'strokeadjust' */
  ss->npoints++;
  store_point_and_sa(ss, pt, xx, yy);

  /*
   * If we have at least two points, and we are not in the zero linewidth
   * case, then calculate the line normal.
   */
  if ( last && ( ss->sm->line == NULL ) && ( last_corner_only || (!dashing) ) )
  {
    SYSTEMVALUE dx, dy;

    dx = pt->point.x - last->point.x;
    dy = pt->point.y - last->point.y;
    calc_norm(ss, dx, dy, nindex);
  }

  /*
   * If we have at least three points (and are not in the special case to deal
   * with the final linejoin), then call the main 3 point geometry routine.
   */
  if ( ss->npoints < 3 || last_corner_only )
    return TRUE;
  else
    return have3points(ss, TRUE);
}

/*
 * Initialise the dashing machinery.
 */
static void start_dashing(STROKER_STATE *ss, LINELIST *p1)
{
  STROKE_PARAMS *sp = ss->sp;
  SYSTEMVALUE len, nextone;
  int32 i;

  if ( ss->section & FIRST_SECTION )
  {
    /*
     * Calculate the total length of vectors within the dash pattern.
     */
    for ( len = 0.0, i = 0; i < sp->linestyle.dashlistlen ; i++ )
      len += sp->linestyle.dashlist[i];
    ss->dash.totalLen = len;
  }

  /* Default initial values, may get changed below */
  ss->dash.index      = 0;
  ss->dash.state      = DASH_START;
  ss->dash.point      = p1->point;
  ss->npoints         = 0;

  /* Normally dashing is reset from the dashoffset at the start of a sub-path,
     however PCL may decide to continue the dashing from the previous sub-path. */
  if ( (p1->flags & LINELIST_CONT_DASH) == 0 ||
       p1 == sp->thepath->firstpath->subpath )
    len = ( SYSTEMVALUE )sp->linestyle.dashoffset;
  else
    len = sp->dashcurrent;

  while (len < 0.0)
  {
    len += ss->dash.totalLen;
    if ( sp->linestyle.dashlistlen & 1 )
      ss->dash.state = (ss->dash.state == DASH_START) ? DASH_END : DASH_START;
  }

  /* Initialise dashcurrent in stroke params.  It is updated in dash_sections so
     the PDL code can track dash position between stroke paths. */
  sp->dashcurrent = 0 ;

  for (;;)
  {
    nextone = sp->linestyle.dashlist[ss->dash.index];
    len -= nextone ;
    if ( len <= 0.0 )
    {
      Bool finished = TRUE;

      /*
       * PostScript and XPS vary in how they handle a dash offset which is
       * exactly equal to some sum of the dash lengths, e.g. "[ x ... ] x"
       *
       * If the offset causes the dash to be exactly at the end of a gap,
       * e.g. "[ 1 1 ] 2", then both PS and XPS start the pattern with the
       * solid bit, i.e. there IS NOT a zero length gap at the start as far
       * as linejoin determination goes.
       *
       * However, if the offset causes the dash to be exactly at the end of a
       * a solid bit, e.g. "[ 1 1 ] 1" then PS starts with the next gap,
       * but XPS starts with a 0 length solid bit !
       *
       * So if we ended exactly on a boundary (len == 0) and the dashoffset
       * was non-zero, deal with this special behaviour...
       */
      if ( len == 0.0 && ( ( SYSTEMVALUE )sp->linestyle.dashoffset ) > 0.0 )
      {
        if ( ( sp->thepath->flags & PATHINFO_XPS ) == 0  )
          finished = FALSE;
        else
        {
          if ( ss->dash.state == DASH_END )
            finished = FALSE;
        }
      }
      if ( finished )
      {
        ss->dash.remains = -len;
        ss->dash.state = (ss->dash.state == DASH_END) ? DASH_GAP : DASH_START;
        ss->dash.startsolid = ( ss->dash.state != DASH_GAP);
        /*
         * If we start in a gap, move on to the middle section so
         * we do not get start linecaps.
         */
        if ( ( ss->section & FIRST_SECTION ) && ( ! ss->dash.startsolid ) )
          ss->section = MIDDLE_SECTION;
        return;
      }
    }
    ss->dash.state = (ss->dash.state == DASH_START) ? DASH_END : DASH_START;
    if ( ++ss->dash.index == sp->linestyle.dashlistlen )
      ss->dash.index = 0;
  }
}

/*
 * Move on to the next place in the array of dashes, and update the 'where'
 * state showing where we are within the current dash.
 *
 * This may be a full move (i.e. to the end of the next dash transition),
 * or a partial one into the middle of the next gap or dash.
 *
 * Note that we do some special-case processing IFF we are doing PCLXL dash
 * (actually *gaps*) because when the end of a gap *exactly* coincides
 * with the end of a line segment, then the next dash appears
 * to *start before* the start of the next line segment
 */
static void next_dash(STROKER_STATE *ss, Bool full, Bool pclxl_dash)
{
  if ( full )
  {
    switch ( ss->dash.state )
    {
      case DASH_START:
      case DASH_MIDDLE:
        ss->dash.state = DASH_END;
        break;

      case DASH_END:
      case DASH_GAP:
        ss->dash.state = DASH_START;
        break;
    }
    ss->dash.index = ((ss->dash.index + 1) % ss->sp->linestyle.dashlistlen);
    ss->dash.remains = ss->sp->linestyle.dashlist[ss->dash.index];
  }
  else /* partial move */
  {
    switch ( ss->dash.state )
    {
      case DASH_START  :
        /*
         * We are at the start of a dash and have been asked to do a partial move
         * If there is still some dash to draw *or we are doing a special PCLXL dash*
         * then we move into the "middle" of the dash
         * Otherwise we move to the end of the dash
         */
        ss->dash.state = ((ss->dash.remains || pclxl_dash) ? DASH_MIDDLE : DASH_END);
        break;

      case DASH_MIDDLE :
        if (pclxl_dash)
        {
          /*
           * We are in the middle of a special PCLXL dash segment
           * and we are going to remain in the "middle" of the dash
           * but we need to none-the-less move onto the next dash
           * exactly as if we had performed a full move
           */
          ss->dash.index = ((ss->dash.index + 1) % ss->sp->linestyle.dashlistlen);
          ss->dash.remains = ss->sp->linestyle.dashlist[ss->dash.index];
        }
        break;

      case DASH_END :
        ss->dash.state = (ss->dash.remains ? DASH_GAP : DASH_START);
        break;

      case DASH_GAP :
        break;
    }
  }
}

/*
 * Special case for dealing with an isolated point.
 * In PostScript terms, could have come from any of the following paths
 *   a) X Y moveto
 *   b) X Y moveto closepath
 *   c) X Y moveto X Y lineto
 *   d) X Y moveto X Y lineto closepath
 * [ With any number of repeats of the same co-ord within some EPSILON
 *   tolerance. ]
 * May also result from a 0 length in the dash array, or a dash which has
 * been clipped to 0% at the start or end of the path.
 */
static Bool only_single_point(STROKER_STATE *ss)
{
  STROKE_PARAMS *sp = ss->sp;
  POINT_INFO *pt = &(ss->p[0]);
  uint8 scap, ecap;
  Bool do_point = TRUE, do_hairline = FALSE, do_caps = TRUE;

  scap = which_cap(ss, TRUE, FALSE);
  ecap = which_cap(ss, FALSE, FALSE);

  if ( ! ss->have_norm )
  {
    if ( ( sp->thepath->flags & PATHINFO_XPS ) != 0 )
    {
      if ( ss->isclosed)
      {
        scap = ROUND_CAP;
        ecap = ROUND_CAP;
      }
    }
    else /* PostScript case */
    {
      scap = sp->linestyle.startlinecap;
      ecap = sp->linestyle.endlinecap;

      if ( scap == ROUND_CAP && ecap == ROUND_CAP )
      {
        if ( !is_dash(sp) )
          do_point = ( ss->isclosed || ss->points_coincident > 0);
      }
      else
        do_point = FALSE;
    }
  }

  if ( do_point )
  {
    FVECTOR norm;
    /*
     * If have_norm is true, then this single point resulted from a zero
     * length dash, so we have a normal to lie it along. Otherwise, we have
     * to make one up. PS only does degenerate round caps, so the direction
     * of the normal is irrelevant. XPS specifies the line caps are drawn in
     * the x direction relative to the current effective render transformation
     * (x,y to x+d,y, with d -> 0).
     */
    if ( ss->have_norm )
    {
      norm = ss->norm[0];

      if ( sp->thepath->flags & PATHINFO_XPS )
      {
        /*
         * XPS does not draw hairlines, period.
         * but may still do line caps for certain styles of line cap
         */

        do_hairline = FALSE;
      }
      else if ( (sp->thepath->flags & PATHINFO_PCLXL) &&
                (ss->section & LAST_SECTION) )
      {
        /*
         * In PCLXL we don't draw a hairline
         * *or* end caps if this is the last section of the path
         */

        do_hairline = FALSE;
        do_caps = FALSE;
      }
      else if ( (sp->thepath->flags & PATHINFO_IGNORE_ZERO_LEN_DASH) &&
                (ss->section & LAST_SECTION) &&
                (ss->dash.remains != 0) )
      {
        /*
         * We also don't draw hairlines in HPGL
         * but only if the PATHINFO_IGNORE_ZERO_LEN_DASH is set
         * and this is at the end of the path
         * and the remaining dash pattern is non-zero-lengthed
         */

        do_hairline = FALSE;
      }
      else if ( ((scap != BUTT_CAP) || (ecap != BUTT_CAP)) &&
                (ss->sm->line == NULL) )
      {
        /*
         * Ok we are (probably) doing a Postscript dashed line
         * But one of the two line end caps is not a butt-cap
         * Or the line itself is degenerate (has no points)
         *
         * In this case we also do not draw this hairline dash segment
         * but may still draw some line caps
         */

        do_hairline = FALSE;
      }
      else
      {
        /*
         * PostScript zero length dashes with butt caps should appear as
         * hairlines but code would naturally generate no output, so need to
         * force hairlines in the PS case.  Also PS 0 linewidth 0 length dashes
         * should appear as a point, and we need to force this to happen in the
         * same way.
         */
        do_hairline = TRUE;
      }
    }
    else
    {
      MATRIX_TRANSFORM_DXY(0.0, sp->lby2, norm.x, norm.y, &(sp->sadj_ctm) ) ;
    }

    if ( do_hairline || do_caps )
    {
      if ( ! wrap_segment(ss, TRUE, TRUE) )
          return FALSE ;

      if ( do_hairline )
      {
        if ( ! hairline_cap(ss, pt, &norm) )
          return FALSE;
      }
      else if ( do_caps )
      {
        if ( ! stroke_cap(ss, pt, &norm,  1, scap) )
          return FALSE;
        if ( ! stroke_cap(ss, pt, &norm, -1, ecap) )
          return FALSE;
      }

      if ( ! wrap_segment(ss, FALSE, TRUE) )
          return FALSE ;
    }
  }
  return TRUE ;
}

/*
 * Line state machine, with three state change routines :-
 *   line_state_first(), line_state_next() and line_state_last()
 * Gets presented with a new-coordinate along the path to be stroked, and
 * passes it on to the machinery that deals with points. The only really
 * difficult bit is the _last() routine which has to deal with the the
 * tidy-up for all the cases which have not quite finished processing.
 */
static Bool line_state_next(STROKER_STATE *ss, FPOINT *p)
{
  return add_stroked_point(ss, p->x, p->y, FALSE);
}

static void line_state_first(STROKER_STATE *ss, FPOINT *p1)
{
  ss->npoints = 0;
  ss->unstroked.line = ss->unstroked.current;
  (void)line_state_next(ss, p1);
}

static Bool line_state_last(STROKER_STATE *ss)
{
  int end = 2;
  uint8 cap;
  Bool DoFinalJoin  = FALSE;
  Bool DoInitialCap = FALSE;
  Bool alldone      = FALSE;

  /*
   * Work out if the path we are dealing with needs a final linejoin.
   * For a plain (undashed) path, this will happen if we have a closed path
   * and we are on the final section (i.e. not an end points caused by
   * unstroked segments).
   * For a dashed path, the resulting segments are open. The one case we have
   * to deal with specially is a dashed path that starts and ends solid, then
   * we need the final join.
   */
  if ( ( ss->section & LAST_SECTION ) && ss->isclosed )
  {
    if ( ! ss->unstroked.start && ss->sp->linestyle.linejoin != NONE_JOIN )
    {
      if ( is_dash(ss->sp ) )
      {
        if ( ss->dash.startsolid && ( ss->dash.state != DASH_GAP) )
          DoFinalJoin = TRUE;
        else
          DoInitialCap = ss->dash.startsolid;
      }
      else
        DoFinalJoin = TRUE;
    }
  }

  /*
   * Point-based algorithm needs three points to get started. If we
   * have not seen three yet, the main algorithm will not have started, so
   * we will need speical tidy-up.
   */
  if ( ss->npoints < 3 )
  {
    /* npoints == 0 is possible, if we started up because we though a dash
     * was coming, but we got to the end of the path first. Nothing to do,
     * just return success */
    if ( ss->npoints == 0 )
      alldone = TRUE;
    else if ( ss->npoints == 2 )
    {
      /*
       * Kick the main point machinery to tell it that the two points it
       * has got is all it will get.
       */
      if ( ! have3points(ss, FALSE) )
        return FALSE;
      end = 1; /* Put the endcap on point #1 below */
      if ( DoFinalJoin && ( !is_dash(ss->sp) ) && ( !ss->unstroked.any ) )
      {
        /*
         * In a somewhat confused state as we are not dashing and do not
         * have unstroked segments, so there was nothing to break the path up.
         * But still we have managed to get a closed path with 2 points ?
         *
         * Path must be closed for us to want to do the final join, but we
         * have only had two points, which seems like a contradiction.
         * If the path was "moveto(A) closepath(A)" we would have filtered the
         * repeated co-ord and only had one point. But if the path was
         * "moveto(A) lineto(B) closepath(A)" we should have three points.
         * So how did we end up with a closed path and 3 points ?
         * Must have been "moveto(A) lineto(B) lineto(B/2) closepath(A)"
         * where A->B is big enough to escape being filtered by
         * coincident_points(), but B->B/2 and B/2->A are too small, and get
         * thrown away.
         * So we have effectively thrown away the closepath, as it was too
         * close to the previous points. So we have to turn off the final
         * join, else the maths will blow-up.
         */
        /** \todo bmj 07-06-2007: Deal with this in a better way */
        DoFinalJoin = FALSE;
      }
    }
    else /* degenerate case of npoints == 1 */
    {
      /*
       * Simple path with only a single point needs to be dealt with as a
       * special degenerate case.
       * Also have 'single' points resulting from unstroked segments
       * and 0 length dashes.
       *
       * If the single point was the result of a totally degenerate path
       * (i.e. we have no normal), then treat the point as both the first
       * and last sections for cap choice.
       */
       if ( ! ss->have_norm )
        ss->section = (FIRST_SECTION | LAST_SECTION);

      if ( ! only_single_point(ss) )
        return FALSE;
      alldone = TRUE;
      if ( DoFinalJoin )
        DoFinalJoin = FALSE;
    }
  }
  /* And closed down the line state machine by doing the final cap/join */
  if ( !alldone )
  {
    if ( DoFinalJoin )
    {
      /*
       * Do the line join at the meeting of the first and last points.
       * Lie to the point machine by giving it the 2nd point in the path again,
       * to allow it to work out the start/end join, but pass an extra flag to
       * make sure it does not do anything else.
       */
      HQASSERT( ss->begin.have_p2 , "No valid 2nd point in stroker" );
      (void)add_stroked_point(ss, ss->begin.p2.x, ss->begin.p2.y, TRUE);
      if ( ! stroke_join(ss, TRUE) )
        return FALSE;
    }
    else /* Do the final cap */
    {
      cap = which_cap(ss, FALSE, TRUE);
      if ( ! stroke_cap(ss, &(ss->p[end]), &(ss->norm[end-1]), -1, cap ) )
        return FALSE;
    }
  }
  /* Reset line machine state and issue done callback */
  ss->npoints = 0;
  if (ss->section & FIRST_SECTION )
    ss->section = MIDDLE_SECTION;
  if ( ! alldone )
  {
    Bool join_AtoB = TRUE;

    if ( ss->isclosed && ( !is_dash(ss->sp) ) && ( !ss->unstroked.any ) &&
         ss->sp->linestyle.linejoin != NONE_JOIN )
      join_AtoB = FALSE;

    if ( ! wrap_segment(ss, FALSE, join_AtoB) )
      return FALSE;
  }
  if ( DoInitialCap )
  {
    /*
     * We did not put an initial linecap in when we started the path
     * as we thought it might end solid, and it would not be needed.
     * But actually the path ended in a gap, so we do need the
     * initial linecap, so we will have to just add it now. Its a bit out
     * of place, but the way the whole output path is wrapped means it
     * cannot interact with itself.
     */
    cap = which_cap(ss, TRUE, FALSE);
    if ( ! wrap_segment(ss, TRUE, TRUE) )
      return FALSE;
    if ( ! stroke_cap(ss, &(ss->begin.p1), &(ss->begin.n1), 1, cap ) )
      return FALSE;
    if ( ! wrap_segment(ss, FALSE, TRUE) )
      return FALSE;
  }
  return TRUE;
}

/*
 * Choose epilson for points_coincident() and degen_bezier() tests.  For round
 * line joins a more generous epsilon (0.45, just under half of one device
 * pixel) is chosen which can result in significant simplification of the path.
 * This is especially useful if clippath is then applied to the path.  Need to
 * use the tighter epsilon for dashed lines or the resultant dashing may be
 * visibly different.
 */
static SYSTEMVALUE points_coincident_epsilon(STROKER_STATE *ss)
{
  return ss->sp->linestyle.linejoin == ROUND_JOIN && !is_dash(ss->sp)
    ? 0.45 : 0.00005 ;
}

/*
 * If the two points are very close to each other then skip the stroke for this
 * line segment to protect against a subsequent division by zero (when the
 * distance between the points is zero).  Missing out the line segment can cause
 * significant differences for miter and bevel line joins.  For this reason the
 * epsilon has to be pretty small.
 */
static Bool points_coincident(STROKER_STATE *ss, FPOINT *p1, FPOINT *p2)
{
  SYSTEMVALUE dx = p2->x - p1->x;
  SYSTEMVALUE dy = p2->y - p1->y;
  SYSTEMVALUE close_pts_epsilon = points_coincident_epsilon(ss);

  if ( dx < close_pts_epsilon && dx > -close_pts_epsilon &&
       dy < close_pts_epsilon && dy > -close_pts_epsilon )
  {
    ss->points_coincident++;
    return TRUE;
  }
  return FALSE;
}

/*
 * Process a point along the dashed outline. A vector in the original path
 * has been broken down into little dashed segments, and this routine called
 * with the resulting co-ords. We get called with both the dash start and end
 * co-ords, plus the start and end co-ords of the original undashed vector,
 * which may or may-not be inside the solid part of a dash. 'where' tells us
 * where the point is in respect of the dash pattern.
 */
static Bool add_dashed_point(STROKER_STATE *ss, FPOINT *p)
{
  int32 dash_state = ss->dash.state;
  Bool moveNorm = FALSE;

  /*
   * If path starts in a gap, we may get DASH_GAP calls before the first
   * DASH_START. So if we have no points, ignore everything but a DASH_START.
   */
  if ( ss->npoints == 0 && dash_state != DASH_START )
    return TRUE;

  if ( dash_state == DASH_GAP ) /* in a gap, so finish of any previous dashes */
    return line_state_last(ss);

  if ( dash_state == DASH_START )
    line_state_first(ss, p);
  else /* in middle or end of a dash */
  {
    /* Need to move the norm if there's already two points, whether the
       third point is coincident or not. */
    moveNorm = ( ss->npoints > 1);

    if ( ! points_coincident(ss, &(ss->dash.lastdash), p) )
    {
      if ( ! line_state_next(ss, p) )
        return FALSE;
    }
  }

  if ( dash_state == DASH_END )
  {
    if ( ! line_state_last(ss) )
      return FALSE;
    if ( moveNorm )
      ss->norm[0] = ss->norm[1];
  }
  ss->dash.lastdash = *p;
  return TRUE;
}


/*
 * Initialise the stroker machinery. This means
 *   a) Initialising the STROKER_STATE structure.
 *   b) Choosing the appropriate set of callback methods.
 *   c) Working out the relationship between left and clockwise.
 *   d) Doing any setup that used to be done in the old code, i.e.
 *      caching round linecaps.
 */
static void init_stroker(STROKER_STATE *ss, STROKE_PARAMS *sp)
{
  SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;

  HQASSERT( sp, "stroke params not set in init_stroker()" ) ;

  ss->sp = sp;
  ss->poorstrokepath = systemparams->PoorStrokepath;
  ss->adobesetlinejoin = systemparams->AdobeSetLineJoin;

  /* Decide on which callback methods we need */
  if ( sp->strokedpath != NULL )
  {
    if ( ss->poorstrokepath )
    {
      ss->sm = &pooroutline_sm;
      if ( sp->lby2 == 0.0f )
        ss->sm->line = psm_line;
      else
        ss->sm->line = NULL;
    }
    else
    {
      ss->sm = &outline_sm;
      if ( sp->lby2 == 0.0f )
        ss->sm->line = osm_line;
      else
        ss->sm->line = NULL;
    }
  }
  else if ( CURRENT_DEVICE() != DEVICE_CHAR || clipmapid < 0 )
  {
    ss->sm = &outline_sm;
    if ( sp->lby2 == 0.0f )
      ss->sm->line = osm_line;
    else
      ss->sm->line = NULL;
  }
  else
  {
    ss->sm = &qsm_sm;
    if ( sp->lby2 == 0.0f )
      ss->sm->line = qsm_line;
    else
      ss->sm->line = NULL;
  }

  /*
   * Need to work out which way is clockwise, for later use.
   * Rotate (1,0) 90 degrees, and transform the result into user space.
   * See if the resulting vector is rotated clockwise of anticlockwise.
   */
  {
    FVECTOR xaxis, yaxis;

    xaxis.x = 1;
    xaxis.y = 0;
    vector_rot90(&xaxis, &yaxis, sp);
    ss->axes_mirrored = 1;
    HQASSERT(yaxis.y != 0.0, "Stroking with singular transform matrix");
    if ( yaxis.y < 0.0 )
      ss->axes_mirrored = -1;
  }

  /*
   * Adobe cache the round line cap, so the next one inherits
   * the old flatness...
   */
  if ( ( sp->linestyle.startlinecap == ROUND_CAP &&
         sp->linestyle.endlinecap   == ROUND_CAP &&
         sp->linestyle.dashlinecap  == ROUND_CAP ) ||
         sp->linestyle.linejoin  == ROUND_JOIN )
  {
    static SYSTEMVALUE clby2    = 0.0 ;
    static OMATRIX     csmatrix = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0 } ;
    static USERVALUE   cflat    = 1.0f ;

    if ( fabs( clby2 - sp->lby2 ) < EPSILON &&
         MATRIX_REQ(&csmatrix, &sp->sadj_ctm))
    {
      fl_setflat( cflat );
    }
    else
    {
      cflat = fl_getflat();
      clby2 = sp->lby2;
      MATRIX_COPY(&csmatrix, &sp->sadj_ctm);
    }
  }
}

/*
 * Calculate the distribution of dashes along the specified line, and
 * track the location within the dash pattern. Also, for extra acuracy and
 * performance, calculate the normals for the dash sections once for the
 * entire line, rather than for each dash section within the line.
 */
static Bool dash_sections(STROKER_STATE *ss, LINELIST *line)
{
  STROKE_PARAMS *sp = ss->sp;
  FPOINT p;
  SYSTEMVALUE dx, dy, vx, vy, len;

  p  = ss->dash.point;
  dx = line->point.x - p.x;
  dy = line->point.y - p.y;

  if ( ss->npoints <= 1)
    calc_norm(ss, dx, dy, 0);
  else
  {
    if ( ss->npoints > 2)
      ss->norm[0] = ss->norm[1];
    calc_norm(ss, dx, dy, 1);
  }

  MATRIX_TRANSFORM_DXY( dx, dy, vx, vy, &(sp->orig_inv) ) ;
  len = sqrt(( vx * vx ) + ( vy * vy )) ;
  HQASSERT(len > 0.0, "Division by zero in dash processing") ;
  dx = vx / len ;
  dy = vy / len ;
  if ( sp->linestyle.dashmode == DASHMODE_ADAPTIVE ) {
    /* Scale dash pattern to fit an integer multiple number of
       complete dashes along the line segment (for HPGL2). */
    SYSTEMVALUE npatterns, npatterns_rounded, rescale ;
    npatterns = len / ss->dash.totalLen ;
    npatterns_rounded = (int32)(npatterns) + 1 ;
    if ( npatterns_rounded < 1 )
      npatterns_rounded = 1 ;
    rescale = npatterns / npatterns_rounded ;
    dx *= rescale ;
    dy *= rescale ;
    len /= rescale ;
  } else if ( sp->linestyle.dashmode == DASHMODE_PERCENTAGE ) {
    /* Treat the numbers in the dashlist as percentage values (for HPGL2). */
    SYSTEMVALUE rescale = len / ss->dash.totalLen ;
    dx *= rescale ;
    dy *= rescale ;
    len = ss->dash.totalLen ;
  }
  MATRIX_TRANSFORM_DXY( dx, dy, vx, vy, &(sp->orig_ctm) ) ;

  /*
   * Dispense dash points along the length of the vector.
   * Test is greater than not greater than or equal so we can
   * do special processing on the final point outside the loop,
   * for the case of the dash finishing exactly on the corner.
   */
  while ( len > ss->dash.remains )
  {
    p.x += ss->dash.remains * vx ;
    p.y += ss->dash.remains * vy ;
    len -= ss->dash.remains;
    next_dash(ss, TRUE, FALSE);
    if ( ! add_dashed_point(ss, &p) )
      return FALSE;
  }
  ss->dash.remains -= len;
  ss->dash.point = line->point;

  /* Got the end of the vector, update where we are within the dash,
   * and then dispense the final dash co-ord.
   * Two special cases we have to deal with carefully are the the dash
   * finishing exactly on the end point (remains == 0), and
   * the dash finishing exactly on the end point and the next dash being
   * zero length as well.
   */
  if ( ss->dash.remains > 0 )
  {
    /*
     * Normal case, dash finished before the end of line, so move partially
     * into the next dash (may be solid or gap), and add the co-ordinate for
     * the end of the line.
     */
    next_dash(ss, FALSE, FALSE);
    if ( ! add_dashed_point(ss, &(line->point) ) )
      return FALSE;
  }
  else
  {
    uint8 next_line_type = (line->next ? line->next->type : MYCLOSE);
    Bool pclxl_dash = (ss->sp->thepath->flags & PATHINFO_PCLXL);

    /*
     * Dash (or gap) finished exactly on the end-point.
     * Move on to the next full dash.
     *
     * Except for PCLXL where we actually want to spoof the start of the dash
     * and then join this start onto the actual start of the dash which is
     * just about to follow
     */

    next_dash(ss, !pclxl_dash, FALSE);

    if ( next_line_type == MYCLOSE )
    {
      /*
       * Unpleasant special case processing.
       * If we are on the last line, and the dash runs out right at the end,
       * we will put the wrong cap on as we don't know yet that it is the end
       * of the path. So have to do a little peek ahead to see if this is the
       * end of the path, and update the section if necessary.
       */
      ss->section = LAST_SECTION;
    }
    else if ( pclxl_dash )
    {
      /*
       * Yet more unpleasant PCLXL-specific corner case handling.
       *
       * In PCLXL we have an interesting difference in dashing behaviour
       * because where a *gap* ends *exactly* at the end of a path segment
       * and the next path segment therefore starts with a dash
       * then the start of the dash is started *before* the "corner"
       * by drawing a hairline and then joining this hairline to the
       * start of the dash (which is round the corner)
       *
       * Note that to achieve this we must convince
       * dash_sections() and add_dashed_point() and next_dash()
       * that we are in fact in the middle of a dash
       * having already seen a preceding *first* point
       * so that it sees the real start of the dash as the *second* point
       * and then sees the end of the dash as the *third*
       * and thus calls have3points() so that the join gets done
       *
       * To achieve this we construct a "fake" point that is *very close to*,
       * but not exactly *at* the corner. And this fake point lies back along
       * the incoming line.
       */

      FPOINT fake_point;

      fake_point.x = (line->point.x - vx);
      fake_point.y = (line->point.y - vy);

      if ( !add_dashed_point(ss, &fake_point) ) return FALSE;

      /*
       * We must now call next_dash() telling it that we are doing a PCLXL dash
       * So that it "consumes" the gap and moves onto the next dash
       * but (still) reports the dash state as being in the middle of the dash
       * that we are about to start
       */

      next_dash(ss, FALSE, pclxl_dash);
    }

    /*
     * Add the final point of the line, which will now either be the start
     * of a dash, or the start of a gap.
     */
    if ( ! add_dashed_point(ss, &(line->point) ) )
      return FALSE;

    /*
     * Deal with any zero length dashes at the end of the line.
     * Keep dispensing dashes 'on the spot' while they are zero length.
     *
     * Hmm, what happens to zero-lengthed dashes that appear
     * immediately after a PCLXL gap that ends at exactly the end of the previous line segment?
     * There is risk here that we will "consume" this PCLXL hairline/dot erroneously
     * and thus mess up the phasing of the dashes and gaps
     *
     * In this case we must be careful to only remove one zero-lengthed segment
     * and place only 1 dashed point.
     */

    if ( sp->linestyle.dashmode != DASHMODE_PERCENTAGE ) {
      if ( ss->dash.remains == 0.0 ) do
      {
        next_dash(ss, !pclxl_dash, pclxl_dash);
        if ( ! add_dashed_point(ss, &(line->point) ) )
          return FALSE;
      }
      while ( (ss->dash.remains == 0.0) && !pclxl_dash );
    }
  }

  /* Store the remains in the stroke params so the PDL code can track
     dash position between stroke paths. */
  {
    int32 i ;
    sp->dashcurrent = 0 ;
    for ( i = 0 ; i < ss->dash.index ; ++i ) {
      sp->dashcurrent += sp->linestyle.dashlist[i] ;
    }
    sp->dashcurrent += sp->linestyle.dashlist[i] - ss->dash.remains ;
  }
  return TRUE;
}

/*
 * Top level stroker state machine, with three state change routines :-
 *   stroker_state_first(), stroker_state_next() and stroker_state_last()
 * Gets presented with a new-coordinate along the path to be stroked, and
 * deals with it appropriately. Needs to distinguish between three cases,
 * plain stroking, stroking with dashes, XPS stroking with unstroked segments.
 */
static Bool stroker_state_first(STROKER_STATE *ss, LINELIST *p1, int32 section)
{
  ss->section = section;
  if ( section & FIRST_SECTION )
  {
    LINELIST *ll = p1, *last = p1;

    /*
     * Check to see if this sub-path is closed and if there are any
     * unstroked segments present.
     */

    ss->unstroked.any = FALSE;
    ss->unstroked.line = FALSE;
    while ( ll )
    {
      if ( (ll->flags & LINELIST_UNSTROKED ) != 0 )
        ss->unstroked.any = TRUE; /* There is an unstroked segment */
      last = ll;
      ll = ll->next;
    }
    /* Starting new stroke state so initialise everything...  */
    ss->isclosed = (last->type == CLOSEPATH );
    ss->have_norm = FALSE;
    ss->norm[0].x = ss->norm[0].y = 0.0;
    ss->norm[1].x = ss->norm[1].y = 0.0;
    ss->bez_internal = FALSE;
    store_point_and_sa(ss, &(ss->begin.p1), p1->point.x, p1->point.y);
    ss->begin.have_p2 = FALSE;
    ss->begin.p2.x = ss->begin.p2.y = 0.0;

    set_unstroked_state(ss, p1);
    ss->unstroked.start = ss->unstroked.current;
  }

  ss->points_coincident = 0;
  /* now start-up the appropriate child state machine */
  if ( is_dash(ss->sp) )
  {
    start_dashing(ss, p1);
    if ( ! add_dashed_point(ss, &(p1->point)) )
      return FALSE;
  }
  else
    line_state_first(ss, &(p1->point));
  return TRUE;
}

static Bool stroker_state_last(STROKER_STATE *ss, int32 section)
{
  /*
   * Record which section we are in. If we go straight from first to last,
   * then keep both bits set, as it is a single line and we need start caps
   * at one end, and end caps at the other.
   */
  if (ss->section == FIRST_SECTION && section == LAST_SECTION)
    ss->section |= section;
  else
    ss->section  = section;
  /*
   * Single points behave differently in a general path to an unstroked path.
   * If we have started a path with a single point, but all subsequent elements
   * were unstroked, we need to supress having the start point behave like a
   * normal single point, and possibly produce a small circle.
   */
  if ( ss->unstroked.line && ss->npoints == 1 )
    ss->npoints = 0;
  return line_state_last(ss);
}

static Bool stroker_state_next(STROKER_STATE *ss, FPOINT *prev_pt, LINELIST *line)
{
  if ( ! ss->begin.have_p2 )
  {
    SYSTEMVALUE dx, dy;

    ss->begin.have_p2 = TRUE;
    ss->begin.p2 = line->point;
    ss->unstroked.start = ss->unstroked.current;

    dx = ss->begin.p2.x - ss->begin.p1.point.x;
    dy = ss->begin.p2.y - ss->begin.p1.point.y;
    calc_norm(ss, dx, dy, -1);
  }

  if ( ss->unstroked.current )
  {
    /* If we have unstroked segments, just end and then re-start the state
     * machine, and it all should be taken care of. Pass an extra flag to tell
     * the stroker state machine it is a re-start, not the beginning of the
     * sub-path. */
    if ( ! stroker_state_last(ss, ss->section) )
      return FALSE;
    return stroker_state_first(ss, line, MIDDLE_SECTION);
  }
  else
  {
    /* A none join is handled by ending and re-starting the state machine
     * (similar to the unstroked case above).  The previous line segment's
     * point is used as the re-start position and then we can continue with
     * the line segment as normal. */
    if ( ss->sp->linestyle.linejoin == NONE_JOIN &&
         prev_pt &&
         ss->npoints >= 2 &&
         !ss->bez_internal )
    {
      /* Use line instead of stroker state routines to avoid re-starting dashing. */
      if ( ! line_state_last(ss) )
        return FALSE;
      line_state_first(ss, prev_pt);
    }

    if ( !is_dash(ss->sp) ) /* plain path just calls line state machine */
      return line_state_next(ss, &(line->point));
    else /* break up line into dashed sections */
      return dash_sections(ss, line);
  }
}

typedef struct
{
  STROKER_STATE *ss; /* current stroker state */
  Bool unstroked;    /* is the bezier unstroked */
  FPOINT last;       /* last bezier point to filter 0 length segments */
} STR_BEZ_INFO;

/*
 * Callback made by the bezier chop code passing us one co-ord along the
 * flattened path at a time.
 */
static int32 bez_cb(FPOINT *pt, void *data, int32 flags)
{
  STR_BEZ_INFO *sbi = (STR_BEZ_INFO *)data;
  STROKER_STATE *ss = sbi->ss;
  Bool retval = 1;

  /* Treat point and control point in the same way ... */
  UNUSED_PARAM(int32, flags);

  /*
   * Bezier is now equivalent to a straight line,
   * output it provided it is not of zero length
   */
  if ( ! points_coincident(ss, &(sbi->last), pt) )
  {
    LINELIST line;
    FPOINT prev_pt = sbi->last;

    sbi->last = *pt;
    line.point = *pt;
    line.next = NULL;
    line.flags = sbi->unstroked ? LINELIST_UNSTROKED : 0;
    line.type = LINETO;
    set_unstroked_state(ss, &line);
    if ( !stroker_state_next(ss, &prev_pt, &line) )
      retval = -1;
    /* After 1st point, turn on flag to show we are inside a bezier */
    ss->bez_internal = TRUE;
  }
  return retval;
}

/*
 * Check to see if the bezier is degenerate.
 *
 * See if the control points are within a fraction of a pixel of the
 * end points. If they are, rendering will result in a linejoin which
 * follows the end-point to control-point tangent. This may be unsightly
 * for large linewidths, and probably not what was intended. If this is
 * the case, pull the control point back onto the end-point, which will
 * prevent any unsightly linejoins.
 *
 * If both control points are pulled back in this way, the bezier has
 * degenerated into a line, so may as well be treated as such.
 *
 * Note : Behaviour in this area differs greatly between an Adobe rip
 * and Acrobat, so total compatibility is difficult to maintain. If you
 * create two microscopically small beziers to draw a circle within a pixel,
 * and then stroke them with a very large linewidth, Acrobat will do the
 * correct mathematical thing and create a large linewidth circle. However,
 * an Adobe RIP flattens the beziers to a tiny line segments and strokes
 * this to produce a tall thin line. The Harlequin rip behaviour caused
 * by this function is a compromise trying to maintain some level of
 * compatibility with both Adobe RIPs and Acrobat, whilst honoring what is
 * believed to be the original intention as often as possible.
 */
static Bool degen_bezier(STROKER_STATE *ss, FPOINT bez[4])
{
  SYSTEMVALUE degen_bez = points_coincident_epsilon(ss);
  int32 n = 0;

  if ( degen_bez < 0.25 )
    degen_bez = 0.25;

  if ( fabs(bez[0].x - bez[1].x) < degen_bez &&
       fabs(bez[0].y - bez[1].y) < degen_bez ) {
    bez[1] = bez[0];
    n++;
  }
  if ( fabs(bez[3].x - bez[2].x) < degen_bez &&
       fabs(bez[3].y - bez[2].y) < degen_bez ) {
    bez[2] = bez[3];
    n++;
  }
  return (n == 2);
}

/*
 * Convert the Bezier curve into an equivalent set of line segments.
 * Rememeber the last point output, so we can filter the points and
 * prevent zero length segments from being emitted.
 * Also turn on the flag to show we inside a bezier after the first point
 * is output, and turn it off at the end.
 */
static Bool stroke_bezier(STROKER_STATE *ss, FPOINT bez[4], Bool unstroked,
                          FPOINT *lastPoint)
{
  STR_BEZ_INFO sbi;
  Bool result;

  sbi.ss = ss;
  sbi.unstroked = unstroked;
  sbi.last = bez[0];

  result = bezchop(bez, bez_cb, (void *)&sbi, BEZ_POINTS|BEZ_CTRLS);
  ss->bez_internal = FALSE;
  *lastPoint = sbi.last;
  return result;
}

/*
 * Convert the subpath provided into a stroked outline.
 */
static Bool stroke_subpath(STROKER_STATE *ss, LINELIST *subpath)
{
  LINELIST *p1, *p2;
  FPOINT lastPoint;
  Bool result = TRUE;

  p1 = subpath;
  HQASSERT(p1->type == MOVETO || p1->type == MYMOVETO,
          "Path to be stroked does not start with a moveto");
  result = stroker_state_first(ss, p1, FIRST_SECTION);
  if ( !result )
    return FALSE;

  lastPoint = p1->point;
  for ( p2 = p1->next; result && p2 && p2->type != MYCLOSE;
        p1 = p2, p2 = p2->next) { /* for each segment in each sub-path */
    Bool is_bez = FALSE;
    Bool unstroked = ((p2->flags & LINELIST_UNSTROKED ) != 0);
    FPOINT bez[4];

    if ( p2->type == CURVETO ) {
      int32 i;

      bez[0] = p1->point;
      for ( i = 1; i <= 3; i++ ) {
        HQASSERT(p2->type == CURVETO, "Corrupt bezier in stroke");
        bez[i] = p2->point;
        if ( i != 3 )
          p2 = p2->next;
      }
      is_bez = !degen_bezier(ss, bez); /* treat a degenerate bezier as a lineto */
    } else {
      HQASSERT(p2->type == LINETO || p2->type == CLOSEPATH,
        "Corrupt path in stroke");
    }

    if ( is_bez ) {
      result = stroke_bezier(ss, bez, unstroked, &lastPoint);
    } else {
      set_bezier_state(ss, p1, p2);
      set_unstroked_state(ss, p2);
      if ( !points_coincident(ss, &lastPoint, &p2->point) ) {
        result = stroker_state_next(ss, &p1->point, p2);
        lastPoint = p2->point;
      }
    }
  }
  if ( result )
    result = stroker_state_last(ss, LAST_SECTION);
  return result;
}

/*
 * Place result of filtering path into this static structure
 */
static LINELIST f_pline = LINELIST_STATIC(LINETO, 0.0, 0.0, NULL);
static LINELIST f_pmove = LINELIST_STATIC(MOVETO, 0.0, 0.0, &f_pline);

/**
 * Examine the input subpath to see if it conforms to various possible idioms.
 * If so replace it with a equivalent path, but one easier to process.
 *
 * Some input subpaths cause nasty combinations of maths to be required, and
 * can easily create unpleasant visual artifacts. A practical approach to this
 * issue is to examine input paths to detect such cases. When found, the input
 * path can be replaced with a simpler but equivalent form.
 * The only case checked for at the moment is :
 *   "a b moveto c d lineto closepath % XPS with clipped miter linejoins"
 * The clipped miter linejoins are just extensions of the stroke edge extended
 * by the miterlimit. But the natural operation of the algorithm results in
 * these co-ords emitted as distinct points from the stroke edges. This can
 * easily result in visually obvious kinks along the stroked edge. So replace
 * the path with :
 *   "e f moveto g h lineto"
 * where the end co-ords are the extension of the line (a,b) -> (c,d) by the
 * miterlength in both directions.
 * When path replacement occurs, an alternative linestyle has to be provided
 * as well. E.g. When a closed path is replaced by an open one, the linestyle
 * needs to have linecaps turned off.
 *
 * \todo BMJ 30-Jan-09 :  Are there any other simple cases that would
 * benefit from this approach.
 */
static LINELIST *filter_subpath(STROKE_PARAMS *sp, LINELIST *path, LINESTYLE *ls)
{
  /* Only filter XPS paths */
  if ( ( sp->thepath->flags & PATHINFO_XPS ) == 0 )
    return NULL;
  /* Only filter if we have XPS clipped miter joins */
  if ( sp->linestyle.linejoin != MITERCLIP_JOIN )
    return NULL;
  /* Only filter if we there is no dashing */
  if ( is_dash(sp) )
    return NULL;

  /* Must be exactly three points : moveto, lineto, closepath */
  if ( path && path->next && path->next->next &&
       path->next->next->next == NULL && path->type == MOVETO &&
       path->next->type == LINETO && path->next->next->type == CLOSEPATH )
  {
    FPOINT p0, p1;
    SYSTEMVALUE dx, dy, len;

    p0 = path->point;
    p1 = path->next->point;

    MATRIX_TRANSFORM_DXY(p1.x - p0.x, p1.y - p0.y, dx, dy, &(sp->orig_inv));

    len = sqrt((dx * dx) + (dy * dy));
    if ( len <= 0.01 ) /* Don't filter tiny lines, maths may be unstable */
      return NULL;
    dx *= sp->lby2*sp->linestyle.miterlimit/len;
    dy *= sp->lby2*sp->linestyle.miterlimit/len;
    MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, &(sp->orig_ctm));

    f_pmove.point.x = p0.x - dx;
    f_pmove.point.y = p0.y - dy;
    f_pline.point.x = p1.x + dx;
    f_pline.point.y = p1.y + dy;
    /* Patch the linestyle to turn on caps */
    *ls = sp->linestyle;
    ls->startlinecap = ls->endlinecap = BUTT_CAP;
    return &f_pmove;
  }
  else
    return NULL;
}

/*
 * Process the path provided, converting it into a form suitable for stroking.
 * Use the stroke parameters provided in the STROKE_PARAMS struct.
 * Return the sections of the stroked path as we generate them, via a series
 * of callbacks. If we are generating a path outline for the 'strokepath'
 * operator, then these callbacks will save the bits of path in order to
 * eventually return the complete outline path. If we are rendering the stroked
 * path, then the callbacks may render each bit as it sees it.
 * This function unifies three old version of the stroking algorithm, which
 * used to be called oldstrokethepath(), newstrokethepath(), and faststroke().
 * See document in notes "Stroking algorithm and formulae" for more details.
 *
 * Note the following System parameters for controlling appearance and quality
 * of stroked lines :-
 * SystemParams.PoorStrokepath
 * SystemParams.AdobeSetLineJoin
 * SystemParams.PoorFlattenpath
 * SystemParams.EnableStroker[3]
 *     EnableStroker[0] = enable stroker (retired old code, now always used)
 *     EnableStroker[1] = honour XPS scan conversion rule
 *     EnableStroker[2] = remove compositing group from around stroke
 *
 *
 * Stroke machinery connects (via callbacks) to Bressenham fill code, which
 * can error due to lack of memory. So code is wrapped in a retry loop,
 * giving the callbacks a chance to increase memory each time around.
 */
Bool stroker(STROKE_PARAMS *sp, PATHLIST *path )
{
  PATHINFO smallpath;
  STROKER_STATE ss;
  Bool result, isHuge = FALSE;
  PATHLIST *p0 = path;
  int32 ntries = 0;

  if ( is_huge_path(path) )
  {
    if ( !clip_huge_path(path, &smallpath, sp) )
      return FALSE;
    path = smallpath.firstpath;
    isHuge = TRUE;
  }

  init_stroker(&ss, sp);
  do
  {
    if ( ss.sm->stroke_begin )
    {
      result = ss.sm->stroke_begin(sp, &ss.data, ntries++);
      if ( ! result )
        break;
    }
    else
      result = TRUE;

    for ( ; result && path ; path = path->next) /* for each sub-path */
    {
      LINESTYLE ls, save_ls = sp->linestyle;
      LINELIST *subpath = path->subpath;
      LINELIST *filt = filter_subpath(sp, subpath, &ls);

      if ( filt != NULL )
      {
        /* Have an alternate filtered sub-path to use.
         * Save old linestle, install new one, and restore it afterwards
         */
        subpath = filt;
        sp->linestyle = ls;
      }
      result = stroke_subpath(&ss, subpath);
      if ( filt != NULL )
        sp->linestyle = save_ls;
    }

    if ( ss.sm->stroke_end )
      result = ss.sm->stroke_end(sp, result, ss.data ) ;

    if ( result )
      ntries = 0;
    else
    {
      path = p0;
      error_clear();
    }
  } while ( ntries != 0 );
  if ( isHuge )
    path_free_list(path, mm_pool_temp);
  return result;
}

void init_C_globals_stroker(void)
{
  LINELIST plineinit = {{0.0, 0.0}, PATHTYPE_STATIC, LINETO, 0, 0, NULL};
  LINELIST pmoveinit = {{0.0, 0.0}, PATHTYPE_STATIC, MOVETO, 0, 0, &f_pline /*sic*/};

#if defined(DEBUG_BUILD)
  debug_stroke = 0;
#endif

  f_pline = plineinit ;
  f_pmove = pmoveinit ;
}

/* Log stripped */
