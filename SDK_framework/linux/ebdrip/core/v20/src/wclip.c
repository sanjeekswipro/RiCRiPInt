/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:wclip.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Clip an arbitrary path to a specified rectangular clipping window.
 *
 * Used to limit the range of ordinates in a path to values that won't
 * overflow the integer point format. This allows snippets like
 *   "0 0 moveto 1e22 1e22 lineto stroke"
 * to be processed successfully without having to have complex overflow
 * logic in the Bressenham rendering code.
 * Implemented using a simple version of Sutherland-Hodgman polygon clipping
 * algorithm. [ Based on pseudo-code description in "Procedural Elements for
 * Compuetr Graphics - David F. Rogers" ]
 * Implementation is very simple and non-optimal from an effeciency
 * point-of-view, as it is only expected to be used in a few extreme cases.
 * If the code is to be used in more general circumstances, it will need
 * structural optimisation.
 * Also maintain the phase of a dash pattern along the line, i.e. ensure
 * any lines thrown away always have lengths which are multiples of the
 * total dash pattern length. This is done by adding extra small "spikes"
 * outside the clipping window to keep the phase in sync.
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

/**
 * Master structure holding all of the rectangular window clipping info.
 */
typedef struct WCLIP
{
  struct
  {
    Bool doPhase;     /**< Do we want to try and maintain the dash phase ? */
    SYSTEMVALUE x, y; /**< Total length of the dash pattern in device space */
    SYSTEMVALUE frac; /**< What fraction of the dash pattern have we clipped */
  } dashlen;          /**< Information about the path dash settings */
  PATHINFO outpath;   /**< Incrementally generated output path */
  sbbox_t *clipbox;   /**< Bounding box we are clipping against */
  int32 edge;         /**< Which edge (top/bottom/left/right) are we clipping */
  Bool inside;        /**< Are we currently inside the clip window ? */
  Bool moveto;        /**< Was the leading moveto inside the clip window ? */
} WCLIP;

/**
 * Test to see which side of a line an ordinate lies.
 * \param  v ordinate to test
 * \return   -1/0/+1 if to-left/on-line/to-right
 */
static int wc_pcmp(SYSTEMVALUE v)
{
  SYSTEMVALUE vabs = v;

  if ( vabs < 0.0 )
    vabs = -vabs;
  if ( vabs < EPSILON )
    return 0;
  else if ( v < 0 )
    return -1;
  else
    return 1;
}

/**
 * Is the specified point inside/outisde/on-the-border of the given edge
 * of the clipbox ?
 * \param[in] pt point to test
 * \param[in] wc window clip context
 * \return       -1 if outside, 0 if on-the border, +1 if inside
 */
static int wc_inside(FPOINT *pt, WCLIP *wc)
{
  switch ( wc->edge )
  {
  case 0: /* top */
    return wc_pcmp(wc->clipbox->y2 - pt->y);
  case 1: /* right */
    return wc_pcmp(wc->clipbox->x2 - pt->x);
  case 2: /* bottom */
    return wc_pcmp(pt->y - wc->clipbox->y1);
  case 3: /* left */
    return wc_pcmp(pt->x - wc->clipbox->x1);
  default:
    HQFAIL("wc_inside() : Illegal edge value");
    return 0;
  }
}

/**
 * Have determined that the specified point is inside the clipping window,
 * so add it to the output path.
 * Ensure the output path structure always maintains its validity, so that if
 * we error half way through we can easily free what we have generated so far.
 * \param[in]     pt   Point to be added to the output path
 * \param[in,out] wc   window clip context
 * \param[in]     type LINETO/MOVETO/CLOSEPATH
 * \param[in]     uns  Is the line to be added unstroked ?
 * \return             Success status
 */
static Bool wc_addpoint(FPOINT *pt, WCLIP *wc, uint8 type, Bool uns)
{
  PATHINFO *out = &(wc->outpath);
  LINELIST *line;
  PATHLIST *path;

  if ( wc->dashlen.doPhase && wc->dashlen.frac > 0.0 )
  {
    SYSTEMVALUE frac = (1.0 - wc->dashlen.frac);
    FPOINT spike = *pt;
    /*
     * Between adding the last point and adding this one, we have
     * chopped out a bit of the path which means the dash pattern will
     * now be out of phase.
     * But if we have just chopped a bit out, that means the point we
     * are about to add must be on the edge of the clipping window.
     * If we add a small spike going outwards from the boundary it will
     * not be visible, and if the length is chosen correctly it will
     * maintain the dash phase.
     */
    wc->dashlen.frac = 0.0;
    if ( wc->edge == 0 || wc->edge == 2 ) /* top or bottom */
      frac *= (wc->dashlen.y/2.0);
    else /* left or right */
      frac *= (wc->dashlen.x/2.0);
    switch ( wc->edge )
    {
      case 0: /* top */
        spike.y += frac;
        break;
      case 1: /* right */
        spike.x += frac;
        break;
      case 2: /* bottom */
        spike.y -= frac;
        break;
      case 3: /* left */
        spike.x -= frac;
        break;
      default:
        HQFAIL("wc_addpoint() : Illegal edge value");
    }
    if ( !wc_addpoint(&spike, wc, LINETO, uns) ||
         !wc_addpoint(&spike, wc, LINETO, uns) )
      return FALSE;
    /* And then drop through to add the original point normally */
  }

  if ( (line = get_line(mm_pool_temp)) == NULL )
    return FALSE;
  line->next = NULL;
  line->point = *pt;
  line->flags = uns ? LINELIST_UNSTROKED : 0;
  if ( out->lastline == NULL )
  {
    if ( (path = get_path(mm_pool_temp)) == NULL )
    {
      free_line(line, mm_pool_temp);
      return FALSE;
    }
    path->subpath = NULL;
    path->next = NULL;

    if ( out->firstpath == NULL )
    {
      out->firstpath = path;
      out->lastpath  = path;
    }
    else
    {
      out->lastpath->next = path;
      out->lastpath       = path;
    }
    path->subpath = line;
    line->type  = MOVETO;
    out->lastline = line;
  }
  else
  {
    line->type  = type;
    out->lastline->next = line;
    out->lastline = line;
  }
  return TRUE;
}

/**
 * Does the line from p1 to p2 cross the specified edge of the clipbox
 * If it does, return the intersection point.
 * \param[in]  p1    First point
 * \param[in]  p2    Second point
 * \param[out] cross Crossing point
 * \param[in]  wc    Window clip context
 * \return           Did lines cross ?
 */
static Bool wc_cross(FPOINT *p1, FPOINT *p2, FPOINT *cross, WCLIP *wc)
{
  int32 i1, i2;
  SYSTEMVALUE f, xx = 0.0, yy = 0.0;
  sbbox_t *box = wc->clipbox;

  i1 = wc_inside(p1, wc);
  i2 = wc_inside(p2, wc);
  if ( i1 * i2 >= 0 )
    return FALSE;

  switch ( wc->edge )
  {
  case 0: /* top */
    f = (box->y2 - p1->y)/(p2->y - p1->y);
    xx = (1.0 - f) * p1->x +  f * p2->x;
    yy = box->y2;
    break;
  case 1: /* right */
    f = (box->x2 - p1->x)/(p2->x - p1->x);
    xx = box->x2;
    yy = (1.0 - f) * p1->y +  f * p2->y;
    break;
  case 2: /* bottom */
    f = (box->y1 - p1->y)/(p2->y - p1->y);
    xx = (1.0 - f) * p1->x +  f * p2->x;
    yy = box->y1;
    break;
  case 3: /* left */
    f = (box->x1 - p1->x)/(p2->x - p1->x);
    xx = box->x1;
    yy = (1.0 - f) * p1->y +  f * p2->y;
    break;
  default:
    HQFAIL("wc_inside() : Illegal edge value");
  }
  cross->x = xx;
  cross->y = yy;
  return TRUE;
}

/**
 * Have chopped out the bit of line specified, but may also need to
 * maintain the dash phase along the path by adding extra spikes to
 * force the length of removed lines to be multiples of the dash length.
 * \param[in]     p1 First point
 * \param[in]     p2 Second point
 * \param[in,out] wc Window clip context
 */
static void wc_chop(FPOINT *p1, FPOINT *p2, WCLIP *wc)
{
  SYSTEMVALUE dx, dy, len2, frac;

  /* If we are fill or undashed stroke, then we don't care about dash phase. */
  if ( !(wc->dashlen.doPhase) )
    return;

  dx = p2->x - p1->x;
  if ( dx < 0.0 )
    dx = -dx;
  dy = p2->y - p1->y;
  if ( dy < 0.0 )
    dx = -dy;
  len2 = (dx*dx)+(dy*dy);
  HQASSERT(len2 > 0.0,"wc_chop : division by zero");
  frac = (dx * wc->dashlen.x + dy * wc->dashlen.y)/len2;
  frac = frac - floor(frac);
  wc->dashlen.frac = frac;
}

/**
 * Clip the given line against the specified edge of the clipping window,
 * adding any resulting output co-ords.
 * \param[in]     p1     Start co-cord of line
 * \param[in]     p2     End co-cord of line
 * \param[in,out] wc     Window clip context.
 * \param[in]     type   Type of line segment
 * \param[in]     uns    Is line unstroked ?
 * \return               Did we process this line segment successfully ?
 */
static Bool wc_line(FPOINT *p1, FPOINT *p2, WCLIP *wc, uint8 type, Bool uns)
{
  Bool ok = TRUE;

  if ( type == MYCLOSE )
  {
    if ( wc->outpath.lastline )
    {
      FPOINT *first = &(wc->outpath.lastpath->subpath->point);

      ok &= wc_addpoint(first, wc, type, uns);
    }
    return ok;
  }

  if ( p2 == NULL ) /* First point of the path */
  {
    wc->dashlen.frac = 0.0; /* Dash is in phase at the start */
    if ( wc_inside(p1, wc) >= 0 )
    {
      ok = wc_addpoint(p1, wc, LINETO, uns);
      wc->inside = wc->moveto = TRUE;
    }
    else
      wc->inside = wc->moveto = FALSE;
  }
  else
  {
    FPOINT cross;

    if ( wc_cross(p1, p2, &cross, wc) )
    {
      if ( wc->inside )
        wc_chop(&cross, p2, wc);
      else
        wc_chop(p1, &cross, wc);

      ok = wc_addpoint(&cross, wc, LINETO, uns);
    }

    if ( wc_inside(p2, wc) >= 0 )
    {
      ok &= wc_addpoint(p2, wc, type, uns);
      wc->inside = TRUE;
    }
    else
    {
      wc_chop(p1, p2, wc);
      wc->inside = FALSE;
    }

    if ( (type == MYCLOSE || type == CLOSEPATH) && (!wc->moveto) )
    {
      if ( wc->outpath.lastline )
      {
        FPOINT *first = &(wc->outpath.lastpath->subpath->point);

        ok &= wc_addpoint(first, wc, type, uns);
      }
    }
  }
  return ok;
}

/**
 * Initialise a pathinfo struct.
 * \param[in,out] path  Structure to be initialised.
 */
static void wc_init_pathinfo(PATHINFO *path)
{
  path->firstpath = NULL;
  path->lastpath  = NULL;
  path->lastline  = NULL;
  path->curved    = FALSE;
}

/**
 * Structure defining all the information that needs to be passed to the
 * Bezier callback.
 */
typedef struct
{
  WCLIP *wc;
  Bool uns;
  FPOINT last;
} BC_BEZ;

/**
 * Bezier callback function to deal with clipping curves to the clip bbox.
 * Does exactly the same as normally bezier reduction, except having more
 * intelligence in being able to throw away beziers which are entirely outside
 * the clip region, and pass through unflattened beziers entirely inside the
 * clip region.
 * \param[in] bez   4 Bezier co-ords to be processed
 * \param[in] data  State information passed as opaque pointer
 * \param[in] flags why we are being called by bezier chop function
 * \return          +1 for success, -1 on failure, 0 if bezier can be left
 *                  as a bezier and does not need to be further sub-divided.
 */

static int32 wc_bezcb(FPOINT *bez, void *data, int32 flags)
{
  int32 i;
  BC_BEZ *bc_bez = (BC_BEZ *)data;
  WCLIP *wc = bc_bez->wc;
  Bool uns = bc_bez->uns;

  if ( flags == BEZ_BEZIERS ) /* do we want to continue chopping ? */
  {
    /*
     * We are interested in two special cases before we start chopping-up
     * the bezier curves.
     *   1) If all four control points lie outside the clip bbox, and on the
     *      same side of one edge, then it can be replaced by a line
     *      (unless we need to maintain dash phase).
     *   2) If all four econtrol points lie inside the clip bbox, then the
     *      bezier can be passed through as is.
     */
    if ( bbox_contains_point(wc->clipbox, bez[0].x, bez[0].y) )
    {
      /* First point is inside, are the other three ? */
      Bool totallyInside = TRUE;

      for ( i = 1; i < 4; i++ )
        if ( !bbox_contains_point(wc->clipbox, bez[i].x, bez[i].y) )
          totallyInside = FALSE;
      if ( totallyInside )
      {
        if ( wc_addpoint(&bez[1], wc, CURVETO, uns) &&
             wc_addpoint(&bez[2], wc, CURVETO, uns) &&
             wc_addpoint(&bez[3], wc, CURVETO, uns) )
        {
          bc_bez->last = bez[3];
          return 0;
        }
        else
          return -1;
      }
    }
    else
    {
      /* First point is outside, are the other 3 (and on the same side ) ? */
      sbbox_t *bbox = wc->clipbox;
      Bool above = TRUE, below = TRUE, left = TRUE, right = TRUE;

      for ( i = 0; i < 4; i++ )
      {
        if ( bez[i].x > bbox->x1 )
          left = FALSE;
        if ( bez[i].x < bbox->x2 )
          right = FALSE;
        if ( bez[i].y > bbox->y1 )
          below = FALSE;
        if ( bez[i].y < bbox->y2 )
          above = FALSE;
      }
      if ( above || below || left || right )
      {
        if ( wc_line(&bez[0], &bez[3], wc, LINETO, uns) )
        {
          bc_bez->last = bez[3];
          return 0;
        }
        else
          return -1;
      }
    }
    /* Drop through if not the two special cases, so chop the bezier... */
    return 1;
  }
  else /* flags == BEZ_POINTS */
  {
    HQASSERT(flags == BEZ_POINTS,"unknown wclip bezier callback");
    if ( wc_line(&(bc_bez->last), bez, wc, LINETO, uns) )
    {
      bc_bez->last = bez[0];
      return 1;
    }
    else
      return -1;
  }
}

/**
 * Deal with a bezier curve in the output path. Only need to process
 * once (on the first pass), else can be just passed through.
 * \param[in]     bez  4 Bezier co-ords to be processed
 * \param[in,out] wc   Window clip context
 * \param[in]     uns  Is the bezier unstroked ?
 * \return             Success status
 */
static Bool wc_bezier(FPOINT bez[4], WCLIP *wc, Bool uns)
{
  if ( wc->edge == 0 ) /* First pass, so clip the bezier */
  {
    BC_BEZ bc_bez;

    bc_bez.wc   = wc;
    bc_bez.uns  = uns;
    bc_bez.last = bez[0];

    return bezchop(bez, wc_bezcb, (void *)&bc_bez, BEZ_BEZIERS|BEZ_POINTS);
  }
  else /* subsequent passes, just push beziers through into output */
  {
    return ( wc_addpoint(&bez[1], wc, CURVETO, uns) &&
             wc_addpoint(&bez[2], wc, CURVETO, uns) &&
             wc_addpoint(&bez[3], wc, CURVETO, uns) );
  }
}

/**
 * Had an error having partially created the output path.
 * Free what we have created so far, and return the error.
 * \param[in,out]  wc  Window clip context
 * \return             FALSE to indicate error
 */
static Bool wc_error(WCLIP *wc)
{
  PATHLIST *out = wc->outpath.firstpath;

  path_free_list(out, mm_pool_temp);
  return FALSE;
}

/**
 * Clip the given path using the various parameters passed in the
 * WCLIP structure.
 * \param[in]     path  Path to be clipped.
 * \param[in,out] wc    Window clip context
 * \return              Success status
 */
static Bool path_wclip(PATHLIST *path, WCLIP *wc)
{
  LINELIST *l1 = NULL, *l2 = NULL;
  PATHLIST *inpath  = path;
  Bool uns;

  for ( wc->edge = 0; wc->edge < 4; wc->edge++ ) /* four edges of the clipbox */
  {
    /* Create an empty result path, and build it up incrementally */
    wc_init_pathinfo(&(wc->outpath));

    for ( ; inpath != NULL; inpath = inpath->next )
    {
      wc->outpath.lastline  = NULL;

      for ( l2 = inpath->subpath; l2 != NULL; l2 = l2->next )
      {
        uns = ( (l2->flags & LINELIST_UNSTROKED) != 0 );
        if ( uns )
          wc->outpath.flags |= PATHINFO_UNSTROKED_SEGMENTS;

        switch ( l2->type )
        {
          case MOVETO:
            l1 = l2;
            if ( !wc_line(&(l1->point), NULL, wc, MOVETO, uns) )
              return wc_error(wc);
            break;
          case LINETO:
          case CLOSEPATH:
          case MYCLOSE:
            if ( !wc_line(&(l1->point), &(l2->point), wc, l2->type, uns) )
              return wc_error(wc);
            l1 = l2;
            break;

          case CURVETO:
          {
            FPOINT bez[4];
            int32 i;

            bez[0] = l1->point;
            for ( i = 1; i <= 3 ; i++ )
            {
              bez[i] = l2->point;
              if ( i != 3 )
                l2 = l2->next;
            }
            if ( !wc_bezier(bez, wc, uns) )
              return wc_error(wc);
            break;
          }
          default:
            HQFAIL("path_wclip() : path corrupt or unflattened");
            return wc_error(wc);
        }
      }
    }
    /*
     * Finished clipping against an edge.
     * Free the input path (unless it was the first edge, when the
     * input will have been the original path parameter.
     * Then make the output from this iteration the input to the next.
     */
    if ( wc->edge > 0 )
      path_free_list(inpath, mm_pool_temp);
    inpath = wc->outpath.firstpath;
  }
  return TRUE;
}

/**
 * Is the path completely contained in the specified bbox ?
 * (If so, then no need to bother trying to clip the path to the bbox).
 * \param[in] path  path to be tested
 * \param[in] bbox  bbox to test against
 * \return          Is the path inside ?
 */
static Bool path_inside_bbox(PATHLIST *path, sbbox_t *bbox)
{
  LINELIST *line;

  for ( ; path != NULL; path = path->next )
  {
    for ( line = path->subpath; line != NULL; line = line->next )
    {
      if ( !bbox_contains_point(bbox, line->point.x, line->point.y) )
        return FALSE;
    }
  }
  return TRUE;
}

/*
 * Now implement the external API for rectangular window clipping
 */

/*
 * TEMP : need to study the fill algorithm to decide on what its actual
 * numerical limits are. Some empirical testing has shown that the value
 * below works with all known tests, but need a bit more logical derivation.
 */
/** \todo bmj 11-05-2007: sort out HUGE definitions */
#define HUGE_LIMIT ( ( SYSTEMVALUE )0x0FFFFFFF )

/**
 * Test to see if the given path contains any co-ordinates that are
 * so big that they will need special processing.
 * \param[in] path  Path to test
 * \return          Does the path include huge ordinates ?
 */
Bool is_huge_path(PATHLIST *path)
{
  sbbox_t clipbox;
  SYSTEMVALUE limit = HUGE_LIMIT;

  bbox_store(&clipbox, -limit, -limit, limit, limit);

  return !path_inside_bbox(path, &clipbox);
}

/**
 * Test to see if the given points is so big that it will blow-up any integer
 * based algorithm.
 *
 * On the x86 architecture, if a floating point to integer overflows the
 * integer range the value is converted to the indefinite integer, regardless
 * of sign. Therefore, if any coordinate matches this value, it is most likely
 * that it will have been generated through a floating point overflow.
 * This used to be tested for throughout the codebase by having explicit tests
 * against the value 'INDEFINITE_INTEGER == 0x80000000'. But now paths get
 * pre-clipped against HUGE_LIMIT, so it should be impossible to see values
 * bigger than this. Hence we can just have a !is_huge_point assert in the
 * nfill processing now, and not have to deal with overflows.
 *
 * \param[in] x    X value to test
 * \param[in] y    Y value to test
 * \return         Is the point too big ?
 */
Bool is_huge_point(dcoord x, dcoord y)
{
  SYSTEMVALUE limit = HUGE_LIMIT;

  return ( (SYSTEMVALUE)x >  limit ||
           (SYSTEMVALUE)x < -limit ||
           (SYSTEMVALUE)y >  limit ||
           (SYSTEMVALUE)y < -limit );
}

/**
 * Clip the given path to a very large rectangular area to ensure there are
 * no ridiculously large co-ordinates that will kill the rendering code.
 * If we are given some stroke parameters as well, ensure the new path
 * maintains phase with respect to and dash pattern.
 * \param[in]  path        Path to be clipped
 * \param[out] clippedpath Resulting clipped path
 * \param[in]  sp          Stroke parameters if we need to maintain dash phase.
 * \return                 Success status
 */
Bool clip_huge_path(PATHLIST *path, PATHINFO *clippedpath, STROKE_PARAMS *sp)
{
  WCLIP wc;
  sbbox_t clipbox;
  Bool ok;
  SYSTEMVALUE limit = HUGE_LIMIT;

  if ( sp && sp->linestyle.dashlistlen != 0 ) /* Dashing */
  {
    LINESTYLE *ls = &(sp->linestyle);
    SYSTEMVALUE len, x1, x2, y1, y2;
    int32 i;

    for ( i = 0, len = 0.0; i < ls->dashlistlen ; i++ )
      len += sp->linestyle.dashlist[i];

    MATRIX_TRANSFORM_DXY( 1.0, 0.0, x1, y1, &(sp->orig_inv) ) ;
    x1 *= len;
    y1 *= len;
    MATRIX_TRANSFORM_DXY( x1, y1, x2, y2, &(sp->orig_ctm) ) ;
    wc.dashlen.x = x2;
    MATRIX_TRANSFORM_DXY( 0.0, 1.0, x1, y1, &(sp->orig_inv) ) ;
    x1 *= len;
    y1 *= len;
    MATRIX_TRANSFORM_DXY( x1, y1, x2, y2, &(sp->orig_ctm) ) ;
    wc.dashlen.y = y2;

    /*
     * I think the above code is OK, and will correctly force huge
     * path reduction to maintain dash phase. But it is such an obscure
     * feature, I'm not going to spend ages debugging it. Instead I
     * will leave it turned off until it is provded that it is required
     * in the real world.
     */
    /** \todo bmj 11-05-2007: Test dash phase issues */
    wc.dashlen.doPhase = FALSE;
  }
  else
  {
    wc.dashlen.doPhase = FALSE;
  }
  /*
   * Also, my test cases seem to work OK, but take an eternity to
   * run. This is because once the path has been clipped, the stroke code
   * then spends ages dispensing dashes along the bit that will be
   * clipped. So pull the huge bbox in by a factor of a 100 to get
   * things running in finite time.
   * Really should be using a number based on the page-size of clippath
   * bounding box, but these enormous upper limits should be OK for now.
   */
  /** \todo bmj 11-05-2007: determine proper limit */
  limit /= 100;

  bbox_store(&clipbox, -limit, -limit, limit, limit);

  wc_init_pathinfo(clippedpath); /* ensure nothing returned if we error early */
  wc_init_pathinfo(&(wc.outpath)); /* start with a clean state */

  wc.clipbox = &clipbox;

  ok = path_wclip(path, &wc);

  if (ok )
    *clippedpath = wc.outpath;
  return ok;
}

/* Log stripped */
