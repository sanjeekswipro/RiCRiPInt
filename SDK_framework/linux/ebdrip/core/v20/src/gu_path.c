/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:gu_path.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manipulate paths.
 */

#include "core.h"
#include "swoften.h"
#include "swerrors.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "graphics.h"
#include "monitor.h"
#include "debugging.h"

#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "system.h"
#include "often.h"
#include "constant.h" /* EPSILON */
#include "fbezier.h"
#include "params.h"
#include "psvm.h"
#include "clipops.h"
#include "pathcons.h"
#include "pathops.h" /* transform_bbox */
#include "gstack.h"
#include "rcbcntrl.h" /* rcbn_enabled() */

#include "gu_path.h"

/*---------------------------------------------------------------------------*/
/* PATHINFO contains firstpath, lastpath, lastline, flags(bboxtype,
   curves, protected, spare), bbox, charpath */
/* PATHLIST contains systemalloc, waste, charpath, subpath, next */
/* LINELIST contains point, systemalloc, type, order, orient, next */
LINELIST pclosep = LINELIST_STATIC( CLOSEPATH, 0.0, 0.0, NULL ) ;
LINELIST plinez =  LINELIST_STATIC( LINETO, 0.0, 0.0, &pclosep ) ;
LINELIST pliney =  LINELIST_STATIC( LINETO, 0.0, 0.0, &plinez ) ;
LINELIST plinex =  LINELIST_STATIC( LINETO, 0.0, 0.0, &pliney ) ;
LINELIST p4move =  LINELIST_STATIC( MOVETO, 0.0, 0.0, &plinex ) ;
PATHLIST p4cpath = PATHLIST_STATIC( &p4move, NULL ) ;
PATHINFO i4cpath = PATHINFO_STATIC( &p4cpath, &p4cpath, &pclosep ) ;

LINELIST p3move =  LINELIST_STATIC( MOVETO, 0.0, 0.0, &pliney ) ;
PATHLIST p3cpath = PATHLIST_STATIC( &p3move, NULL ) ;
PATHINFO i3cpath = PATHINFO_STATIC( &p3cpath, &p3cpath, &pclosep ) ;

LINELIST p2move =  LINELIST_STATIC( MOVETO, 0.0, 0.0, &plinez ) ;
PATHLIST p2cpath = PATHLIST_STATIC( &p2move, NULL ) ;

LINELIST pcurvez3 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pclosep ) ;
LINELIST pcurvez2 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvez3 ) ;
LINELIST pcurvez1 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvez2 ) ;
LINELIST pcurvey3 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvez1 ) ;
LINELIST pcurvey2 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvey3 ) ;
LINELIST pcurvey1 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvey2 ) ;
LINELIST pcurvex3 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvey1 ) ;
LINELIST pcurvex2 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvex3 ) ;
LINELIST pcurvex1 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvex2 ) ;
LINELIST pcurvew3 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvex1 ) ;
LINELIST pcurvew2 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvew3 ) ;
LINELIST pcurvew1 = LINELIST_STATIC( CURVETO, 0.0, 0.0, &pcurvew2 ) ;

/* four curve segments */
LINELIST p4cmove = LINELIST_STATIC( MOVETO, 0.0, 0.0, &pcurvew1 ) ;
PATHLIST p4curve = PATHLIST_STATIC( &p4cmove, NULL ) ;

/* two curve segments */
LINELIST p2cmove = LINELIST_STATIC( MOVETO, 0.0, 0.0, &pcurvey1 ) ;
PATHLIST p2curve = PATHLIST_STATIC( &p2cmove, NULL ) ;

/* one curve segment */
LINELIST p1cmove = LINELIST_STATIC( MOVETO, 0.0, 0.0, &pcurvez1 ) ;
PATHLIST p1curve = PATHLIST_STATIC( &p1cmove, NULL ) ;

/* one line/curve segment */
LINELIST p1lcline = LINELIST_STATIC( LINETO, 0, 0, &pcurvez1 ) ;
LINELIST p1lcmove = LINELIST_STATIC( MOVETO, 0, 0, &p1lcline ) ;
PATHLIST p1lcurve = PATHLIST_STATIC( &p1lcmove, NULL ) ;

/* two line/curve segments */
LINELIST p2lcline = LINELIST_STATIC( LINETO, 0, 0, &pcurvey1 ) ;
LINELIST p2lcmove = LINELIST_STATIC( MOVETO, 0, 0, &p2lcline ) ;
PATHLIST p2lcurve = PATHLIST_STATIC( &p2lcmove, NULL ) ;

/*****************************************************************************/

USERVALUE fl_ftol = 1.0f ; /* Given flat tolerance */
USERVALUE fl_flat = 1.0f ; /* Given flatness       */

int32 nooflines = 0 ;
uint8 flatorder = 1 ; /* Always 1 or 2. */
LINELIST *flatline = NULL ;

/* -------------------------------------------------------------------------- */
/* Macro to check whether the PATHINFO's PATHLIST is shared when it is about to
   be modified and make a copy if it is.

   Note that path must already have been asserted as nonzero, and that the error
   return value can be blank for use in void functions.
*/

#define LAZY_PATH_COPY(path, error)                                           \
  if ((path)->firstpath && (path)->firstpath->shared) {                       \
    (path)->firstpath->shared-- ;                                             \
    if (!path_copy_list((path)->firstpath, &(path)->firstpath,                \
                        &(path)->lastpath, &(path)->lastline, mm_pool_temp))  \
      return error ;                                                          \
  }

/*---------------------------------------------------------------------------*/
/* Routines to initialise a path and add line segments to an existing path.
   The coordinates are the device coordinates added to the path. No
   transformation or rangechecking is performed on the coordinates. */
void path_init(PATHINFO *lpath)
{
  HQASSERT( lpath, "No pathinfo pointer" ) ;

  lpath->firstpath = NULL ;
  lpath->lastpath = NULL ;
  lpath->lastline = NULL ;
  lpath->protection = PROTECTED_NONE ;
  lpath->bboxtype = BBOX_NOT_SET ;
  lpath->curved = FALSE ;
  lpath->flags = PATHINFO_CLEAR_FLAGS ;
  lpath->charpath_id = 0 ;
}

/* Add a MOVETO or MYMOVETO segment if needed, or replace the existing MOVETO
   coordinates with new x,y values. This replaces the old do_moveto and
   add_moveto functions which each performed half of the function. */
Bool path_moveto(SYSTEMVALUE x, SYSTEMVALUE y, int32 type, PATHINFO *path)
{
  register LINELIST *theline, *newline ;
  register PATHLIST *thepath, **attachpath ;

  HQASSERT(path, "No path to add move to") ;
  HQASSERT(type == MOVETO || type == MYMOVETO,
           "Moveto type must be MOVETO or MYMOVETO") ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(path, FALSE) ;

  attachpath = &path->firstpath ;
  if ( (theline = path->lastline) != NULL ) {
    switch ( theILineType(theline) ) {
    case MOVETO :
    case MYMOVETO :
      theILineType(theline) = ( uint8 )type ;
      theline->point.x = x ;
      theline->point.y = y ;
      return TRUE ;
    default:
      theline = theISubPath( path->lastpath );

      if ( NULL == (newline = get_line(mm_pool_temp)) )
        return FALSE ;

      theILineType( newline ) = ( uint8 )MYCLOSE ;
      theILineOrder( newline ) = ( uint8 )0 ;
      newline->point = theline->point ;
      newline->next = NULL ;

      path->lastline->next = newline ;
      path->lastline = newline ;
      /* FALLTHRU */
    case CLOSEPATH :
    case MYCLOSE :
      break ;
    }
    attachpath = &path->lastpath->next ;
  }

  if ( NULL == (newline = get_line(mm_pool_temp)) )
    return FALSE ;

  if ( NULL == (thepath = get_path(mm_pool_temp)) ) {
    free_line( newline, mm_pool_temp ) ;
    return FALSE ;
  }

  theISubPath( thepath ) = newline ;
  thepath->next = NULL ;

  theILineType( newline ) = ( uint8 )type ;
  theILineOrder( newline ) = ( uint8 )0 ;
  theX( theIPoint( newline )) = x ;
  theY( theIPoint( newline )) = y ;
  newline->next = NULL ;

  path->lastpath = *attachpath = thepath ;
  path->lastline = newline ;

  return TRUE ;
}

/* General line/curve/move/close segment addition for path. Asserts that
   the path will be well-formed. */
Bool path_segment(SYSTEMVALUE x, SYSTEMVALUE y,
                  int32 type, Bool stroked, PATHINFO *path)
{
  LINELIST *newline ;

  HQASSERT(path && path->lastline, "No path to add segment to") ;
  HQASSERT(theILineType(path->lastline) == MOVETO ||
           theILineType(path->lastline) == MYMOVETO ||
           theILineType(path->lastline) == LINETO ||
           theILineType(path->lastline) == CURVETO,
           "Cannot append to existing path segment") ;
  HQASSERT(type == LINETO || type == CURVETO || type == CLOSEPATH ||
           type == MYCLOSE || type == TEMPORARYCLOSE,
           "Incorrect type for new path segment") ;

  /* Copy the path if it is currently shared, unless TEMPORARY */
  if (type == TEMPORARYCLOSE) {
    type = MYCLOSE ;
  } else {
    LAZY_PATH_COPY(path, FALSE) ;
  }

  if ( NULL == ( newline = get_line(mm_pool_temp)))
    return FALSE ;

  theILineType(newline) = ( uint8 )type ;
  theILineOrder(newline) = ( uint8 )0 ;
  SET_LINELIST_STROKED(newline, stroked) ;
  theX( theIPoint(newline)) = x ;
  theY( theIPoint(newline)) = y ;
  newline->next = NULL ;

  path->lastline->next = newline ;
  path->lastline = newline ;
  if ( !stroked )
    path->flags |= PATHINFO_UNSTROKED_SEGMENTS ;

  return TRUE ;
}

/* Add three linelists forming a curve to a path. Asserts that the path will
   be well-formed. */
Bool path_curveto(register SYSTEMVALUE args[6], Bool stroked, PATHINFO *path)
{
  register LINELIST *l1 ;

  HQASSERT(path && path->lastline, "No path to add segment to") ;
  HQASSERT(theILineType(path->lastline) == MOVETO ||
           theILineType(path->lastline) == MYMOVETO ||
           theILineType(path->lastline) == LINETO ||
           theILineType(path->lastline) == CURVETO,
           "Cannot append to existing path segment") ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(path, FALSE) ;

  if ( NULL == ( l1 = get_3line(mm_pool_temp)))
    return FALSE ;

  path->lastline->next = l1 ;

  theILineType( l1 ) = ( uint8 )CURVETO ;
  theILineOrder( l1 ) = ( uint8 )0 ;
  SET_LINELIST_STROKED( l1, stroked ) ;
  theX( theIPoint( l1 )) = args[ 0 ] ;
  theY( theIPoint( l1 )) = args[ 1 ] ;

  l1 = l1->next ;

  theILineType( l1 ) = ( uint8 )CURVETO ;
  theILineOrder( l1 ) = ( uint8 )0 ;
  SET_LINELIST_STROKED( l1, stroked ) ;
  theX( theIPoint( l1 )) = args[ 2 ] ;
  theY( theIPoint( l1 )) = args[ 3 ] ;

  l1 = l1->next ;

  theILineType( l1 ) = ( uint8 )CURVETO ;
  theILineOrder( l1 ) = ( uint8 )0 ;
  SET_LINELIST_STROKED( l1, stroked ) ;
  theX( theIPoint( l1 )) = args[ 4 ] ;
  theY( theIPoint( l1 )) = args[ 5 ] ;

  path->lastline = l1 ;

  path->curved = TRUE ; /* Note that path contains curves */
  if ( !stroked )
    path->flags |= PATHINFO_UNSTROKED_SEGMENTS ;

  return TRUE ;
}

/* Close a path with either an implicit or explicit closepath. */
Bool path_close(int32 type, PATHINFO *path)
{
  LINELIST *theline ;

  HQASSERT(path, "No path to close") ;

  theline = path->lastline ;

  if ( ! theline )
    return TRUE ;

  switch( theILineType( theline )) {
  case MYCLOSE :
    if (type == TEMPORARYCLOSE)
      return TRUE ; /* Temporary close is just a MYCLOSE anyway */

    /* Copy the path if it is currently shared */
    if (type != MYCLOSE) {
      LAZY_PATH_COPY(path, FALSE) ;
      theline = path->lastline ;               /* in case path was copied */

      theILineType( theline ) = (uint8) type ;
    }

    /* FALLTHRU */
  case CLOSEPATH :
    return TRUE ;
  default:
    theline = theISubPath( path->lastpath ) ;
    return path_segment( theX( theIPoint( theline )) ,
                         theY( theIPoint( theline )) ,
                         type , TRUE , path ) ;
  }
}

/* ------------------------------------------------------------------------ */
void fl_setflat( USERVALUE flat )
{
  HQASSERT( flat >= 0.2f && flat <= 100.0f , "flat out of range" ) ;
  fl_flat = flat ;

  if ( get_core_context_interp()->systemparams->AdobeSetFlat )
    flat = ( USERVALUE )log10(( double )flat ) + 1.0f ;

  fl_ftol = flat * flat ;
}

/*
 * Callback from the bezier flattening code, passed each line
 * co-ordinate in turn.
 */
static int32 flat_cb(FPOINT *pt, void *data, int32 flags)
{
  LINELIST *line = (LINELIST *)data;

  UNUSED_PARAM(int32, flags);

  /* Add line segment since curve is good enougth. */
  flatline = flatline->next;
  if ( NULL == ( flatline->next = get_line(mm_pool_temp)))
    return -1;

  ++nooflines;
  flatline->type = LINETO;
  flatline->order = flatorder;
  flatline->flags = 0;
  if ( (line->flags & LINELIST_UNSTROKED) )
    flatline->flags |= LINELIST_UNSTROKED;
  flatline->point.x = pt->x;
  flatline->point.y = pt->y;
  return 1;
}

/*
 * Flatten the supplied path, returning a new one which has had all its
 * bezier sections replaced by an approximating set of line segments.
 */
Bool path_flatten(PATHLIST *thepath, PATHINFO *fpath)
{
  register uint8 ltype ;
  register uint8 torder ;
  register uint8 lorder ;
  register LINELIST *flat , *theline , *nextlflat ;
  register PATHLIST *flatpath , *nextpflat , *flatstart ;
  uint8 unstrokedsegments = FALSE ;
  FPOINT ctrl_pts[ 4 ] ;
  Bool poorflattenpath = get_core_context_interp()->systemparams->PoorFlattenpath;

  nooflines = 0 ;

  if ( NULL == (nextpflat = flatstart = get_path(mm_pool_temp)) )
    return FALSE ;

  flatpath = NULL ; /* init to keep compiler quiet */

  flat = NULL ;
  while ( thepath ) {
    HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
             theISystemAlloc(thepath) == PATHTYPE_STATIC ||
             theISystemAlloc(thepath) == PATHTYPE_STACK ||
             theISystemAlloc(thepath) == PATHTYPE_STRUCT,
             "Invalid allocation type for path") ;

    flatpath = nextpflat ;
    flatpath->subpath = nextlflat = get_line(mm_pool_temp);
    if ( nextlflat == NULL ) {
      flatpath->next = NULL ;
      path_free_list( flatstart, mm_pool_temp ) ;
      return FALSE ;
    }
    lorder = 0 ;
    theline = thepath->subpath;
    while ( theline ) {
      HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(theline) == PATHTYPE_STATIC ||
               theISystemAlloc(theline) == PATHTYPE_STACK ||
               theISystemAlloc(theline) == PATHTYPE_STRUCT,
               "Invalid allocation type for line") ;

      SwOftenUnsafe() ;

      if ( (theline->flags & LINELIST_UNSTROKED) != 0 )
        unstrokedsegments = TRUE ;

      ltype = theline->type;
      torder = theline->order;
      if ( torder != 0 ) {
        if ( poorflattenpath ) {
          torder = 0 ;
        }
        else {
          HQASSERT( ltype != CURVETO , "line order must be 0 for curves" ) ;
          if ( lorder != torder ) {
            lorder = torder ;
            flatorder = ( uint8 )( 3 - flatorder ) ;
          }
          torder = flatorder ;
        }
      }

      switch ( ltype ) {
        case CURVETO :
          flatorder = ( uint8 )( 3 - flatorder ) ;

          ctrl_pts[0].x = flat->point.x;
          ctrl_pts[0].y = flat->point.y;
          ctrl_pts[1].x = theline->point.x;
          ctrl_pts[1].y = theline->point.y;
          theline = theline->next ;
          ctrl_pts[2].x = theline->point.x;
          ctrl_pts[2].y = theline->point.y;
          theline = theline->next ;
          ctrl_pts[3].x = theline->point.x;
          ctrl_pts[3].y = theline->point.y;

          if ( ! bezchop(ctrl_pts, flat_cb, (void *)theline, BEZ_POINTS) ) {
            flatpath->next = NULL ;
            path_free_list( flatstart, mm_pool_temp ) ;
            return FALSE ;
          }
          nextlflat = flatline->next ;
          flat = theline ;
          break ;

        default:
          ++nooflines ;

          nextlflat->type  = ltype;
          nextlflat->order = torder;
          nextlflat->point = theline->point;
          nextlflat->flags = theline->flags;

          flatline = nextlflat ;
          if ( NULL == (flatline->next = nextlflat = get_line(mm_pool_temp)) ) {
            flatpath->next = NULL ;
            path_free_list( flatstart, mm_pool_temp ) ;
            return FALSE ;
          }
          flat = theline ;
          break ;
      }
      theline = theline->next ;
    }
    --nooflines ;
    free_line( nextlflat, mm_pool_temp ) ;
    flatline->next = NULL ;

    if ( NULL == (flatpath->next = nextpflat = get_path(mm_pool_temp)) ) {
      path_free_list( flatstart, mm_pool_temp ) ;
      return FALSE ;
    }
    thepath = thepath->next ;
  }
  free_path( nextpflat, mm_pool_temp ) ;
  flatpath->next = NULL ;

  fpath->firstpath = flatstart;
  fpath->lastpath  = flatpath;
  fpath->lastline  = flatline;
  fpath->curved    = FALSE ;
  if ( unstrokedsegments )
    fpath->flags |= PATHINFO_UNSTROKED_SEGMENTS ;
  else
    fpath->flags &= ~PATHINFO_UNSTROKED_SEGMENTS ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            path_bbox(..)        author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           thepath .
   description:

   Utility function - calculates the bounding box of the given path.

---------------------------------------------------------------------------- */
void path_bbox_subpath(PATHLIST *thepath, sbbox_t *bbox, uint32 flags)
{
  register LINELIST *theline ;
  register SYSTEMVALUE x1 , x2 , y1 , y2 ;
  register SYSTEMVALUE temp ;

  HQASSERT(thepath, "NULL PATHLIST parameter" ) ;
  HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
           theISystemAlloc(thepath) == PATHTYPE_STATIC ||
           theISystemAlloc(thepath) == PATHTYPE_STACK ||
           theISystemAlloc(thepath) == PATHTYPE_STRUCT,
           "Invalid allocation type for path") ;

  HQASSERT(bbox, "NULL sbbox_t parameter" ) ;
  HQASSERT((flags & BBOX_TYPE_MASK) == BBOX_IGNORE_NONE ||
    (flags & BBOX_TYPE_MASK) == BBOX_IGNORE_ALL ||
    (flags & BBOX_TYPE_MASK) == BBOX_IGNORE_LEVEL2, "flag value is bogus") ;

  theline = theISubPath( thepath ) ;
  if ( (flags & BBOX_UNION) != 0 ) { /* Union of existing bbox and new one */
    bbox_load(bbox, x1, y1, x2, y2) ;
  } else { /* Initialise all from first point */
    x1 = x2 = theX( theIPoint( theline )) ;
    y1 = y2 = theY( theIPoint( theline )) ;
  }

  HQASSERT(theILineType(theline) == MOVETO ||
           theILineType(theline) == MYMOVETO,
           "Line does not start with a (MY)MOVETO") ;

  /* In English: if the path consists of a moveto followed by an implicit
     closepath (or is unclosed), and we're ignoring all degenerate subpaths,
     don't do anything. */
  if ( (flags & BBOX_TYPE_MASK) != BBOX_IGNORE_ALL ||
       (theline->next &&
        theILineType(theline->next) != MYCLOSE) ) {
    do {
      HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(theline) == PATHTYPE_STATIC ||
               theISystemAlloc(theline) == PATHTYPE_STACK ||
               theISystemAlloc(theline) == PATHTYPE_STRUCT,
               "Invalid allocation type for line") ;
      switch ( theILineType( theline )) {
      case MYMOVETO :
        break ;
      case MOVETO :
        /* any strictly trailing moveto's are ignored in level 2 (RB2 p564) */
        if ( (flags & BBOX_TYPE_MASK) == BBOX_IGNORE_LEVEL2 &&
             theISaveLangLevel( workingsave ) >= 2 ) {
          if ( theline->next == NULL ) {
            break ; /* Path is unclosed, ignore moveto */
          } else if ( theILineType(theline->next) == MYCLOSE ) {
            theline = theline->next; /* ignore the close too */
            HQASSERT(theline->next == NULL,
                     "Implicit close after moveto has successor") ;
            break ;
          }
        }
        /* otherwise fall through */
      default:
        temp = theX( theIPoint( theline )) ;
        if ( temp < x1 )
          x1 = temp ;
        else if ( temp > x2 )
          x2 = temp ;

        temp = theY( theIPoint( theline )) ;
        if ( temp < y1 )
          y1 = temp ;
        else if ( temp > y2 )
          y2 = temp ;
      }
      theline = theline->next ;
    } while ( theline ) ;
  }

  bbox_store(bbox, x1, y1, x2, y2) ;
}

/* Calculate bbox of path. Cache the result if BBOX_SAVE flag is set. Use
   previously cached results if BBOX_LOAD flag is set (but never overwrite a
   bbox set by an explicit setbbox_). Create union of old and new bboxes if
   BBOX_UNION flag is set. Other flags control how degenerate sub-paths are
   handled. */
sbbox_t *path_bbox(register PATHINFO *path, sbbox_t *bbox, uint32 flags)
{
  register PATHLIST *thepath ;

  HQASSERT(path, "No PATHINFO parameter") ;

  HQASSERT((flags & BBOX_TYPE_MASK) == BBOX_IGNORE_NONE ||
     (flags & BBOX_TYPE_MASK) == BBOX_IGNORE_ALL ||
     (flags & BBOX_TYPE_MASK) == BBOX_IGNORE_LEVEL2, "flag value is bogus") ;
  HQASSERT(!(flags & BBOX_UNION) != !(flags & (BBOX_SAVE|BBOX_LOAD)),
           "Can't get bbox union when saving/loading bbox") ;

  /* If no bbox supplied, we want to use the cached one */
  if ( bbox == NULL ) {
    HQASSERT((flags & BBOX_UNION) == 0, "Can't perform union with NULL bbox") ;
    HQASSERT((flags & BBOX_SAVE) != 0, "Can't save to cached bbox") ;
    bbox = &path->bbox ;
  }

  /* Return cached bbox if it is the same type, or if it was set by setbbox_ */
  if ( (flags & BBOX_LOAD) != 0 &&
       (path->bboxtype == (flags & BBOX_TYPE_MASK) ||
        path->bboxtype == BBOX_SETBBOX) ) {
    if ( bbox != &path->bbox )
      *bbox = path->bbox ;
    return bbox ;
  }

  thepath = path->firstpath ;
  HQASSERT(thepath, "No pathlist in pathinfo" ) ;

  do {
    path_bbox_subpath(thepath, bbox, flags) ;

    flags |= BBOX_UNION ; /* Subsequent subpaths are added to bbox */

    thepath = thepath->next ;
  } while ( thepath ) ;

  /* Save bounding box only if it was not previously set by setbbox. */
  if ( (flags & BBOX_SAVE) != 0 &&
       path->bboxtype != BBOX_SETBBOX ) {
    if ( bbox != &path->bbox )
      path->bbox = *bbox ;
    path->bboxtype = (uint8)(flags & BBOX_TYPE_MASK) ;
  }

  return bbox ;
}

/* Calculate bbox of path in transformed space. This will in general be
   smaller than the transformed bbox. The BBOX_LOAD and BBOX_SAVE flags are
   invalid, and therefore we require a bbox on entry. */
sbbox_t *path_transform_bbox(register PATHINFO *path, sbbox_t *bbox, uint32 flags,
                             OMATRIX *transform)
{
  register SYSTEMVALUE x1 , x2 , y1 , y2 ;
  PATHLIST *thepath ;

  HQASSERT(path, "No PATHINFO parameter") ;
  HQASSERT(bbox, "bbox is required for path_transform_bbox") ;
  HQASSERT(flags == BBOX_IGNORE_ALL ||
           flags == BBOX_IGNORE_NONE ||
           flags == BBOX_IGNORE_LEVEL2, "flags value is bogus") ;
  HQASSERT(transform, "No transform matrix") ;

  thepath = path->firstpath ;
  HQASSERT(thepath, "No pathlist in pathinfo") ;

  /* If we have an orthogonal matrix, then we can use path_bbox() and transform
   * the resulting bbox.
   * Otherwise, we ought to transform every point in the path, and compute the
   * bounding box of these transformed points (to get the tightest, most
   * accurate results).
   */
  if ( transform->opt != MATRIX_OPT_BOTH ) {
    SYSTEMVALUE tmp ;
    sbbox_t *pbox;

    pbox = path_bbox( path, NULL, flags|BBOX_SAVE|BBOX_LOAD ) ;

    MATRIX_TRANSFORM_XY( pbox->x1, pbox->y1, x1, y1, transform ) ;
    MATRIX_TRANSFORM_XY( pbox->x2, pbox->y2, x2, y2, transform ) ;

    if ( x2 < x1 ) {
      tmp = x1 ; x1 = x2 ; x2 = tmp ;
    }
    if ( y2 < y1 ) {
      tmp = y1 ; y1 = y2 ; y2 = tmp ;
    }
  } else {
    register LINELIST *theline;
    SYSTEMVALUE tx1, ty1;

    theline = theISubPath(thepath) ;
    MATRIX_TRANSFORM_XY( theX(theIPoint(theline)), theY(theIPoint(theline)),
                         x1, y1, transform ) ;
    x2 = x1 ;
    y2 = y1 ;

    do {
      HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(thepath) == PATHTYPE_STATIC ||
               theISystemAlloc(thepath) == PATHTYPE_STACK ||
               theISystemAlloc(thepath) == PATHTYPE_STRUCT,
               "Invalid allocation type for path") ;

      theline = theISubPath(thepath) ;
      HQASSERT(theILineType(theline) == MOVETO ||
               theILineType(theline) == MYMOVETO,
               "Line does not start with a (MY)MOVETO") ;

      /* In English: if the path consists of a moveto followed by an implicit
         closepath (or is unclosed), and we're ignoring all degenerate
         subpaths, don't do anything. */
      if ( flags != BBOX_IGNORE_ALL ||
           (theline->next &&
            theILineType(theline->next) != MYCLOSE) ) {
        do {
          HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
                   theISystemAlloc(theline) == PATHTYPE_STATIC ||
                   theISystemAlloc(theline) == PATHTYPE_STACK ||
                   theISystemAlloc(theline) == PATHTYPE_STRUCT,
                   "Invalid allocation type for line") ;

          switch (theILineType(theline)) {
          case MYMOVETO :
            break ;
          case MOVETO :
            /* any strictly trailing moveto's are ignored in level 2 (RB2 p564) */
            if ( flags == BBOX_IGNORE_LEVEL2 &&
                 theISaveLangLevel(workingsave) >= 2 ) {
              if ( theline->next == NULL ) {
                break ; /* Path is unclosed, ignore moveto */
              } else if ( theILineType(theline->next) == MYCLOSE ) {
                theline = theline->next; /* ignore the close too */
                HQASSERT(theline->next == NULL,
                         "Implicit close after moveto has successor") ;
                break ;
              }
            }
            /* otherwise fall through */
          default:
            MATRIX_TRANSFORM_XY( theX(theIPoint(theline)),
                                 theY(theIPoint(theline)),
                                 tx1, ty1, transform ) ;
            if ( tx1 < x1 )
              x1 = tx1 ;
            else if ( tx1 > x2 )
              x2 = tx1 ;
            if ( ty1 < y1 )
              y1 = ty1 ;
            else if ( ty1 > y2 )
              y2 = ty1 ;
          }
          theline = theline->next ;
        } while (theline) ;
      }
      thepath = thepath->next ;
    } while (thepath) ;
  }

  bbox_store(bbox, x1, y1, x2, y2) ;

  return bbox ;
}

/* ----------------------------------------------------------------------------
   function:            path_copy(..)       author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           thepath .
   description:

   Utility function - returs a copy of the given path.

---------------------------------------------------------------------------- */
Bool path_copy(PATHINFO *topath, PATHINFO *frompath, mm_pool_t pool)
{
  HQASSERT( topath , "topath NULL in path_copy" ) ;
  HQASSERT( frompath , "frompath NULL in path_copy" ) ;
  HQASSERT( topath->firstpath == NULL ,
            "Non-empty or uninitialised target in path_copy: possible leak!" ) ;

  topath->bboxtype = frompath->bboxtype ;
  if ( frompath->bboxtype != BBOX_NOT_SET )
    topath->bbox = frompath->bbox ;
  topath->curved = frompath->curved ;
  topath->protection = frompath->protection;
  topath->charpath_id = frompath->charpath_id ;
  topath->flags = frompath->flags ;

  return path_copy_list( frompath->firstpath , & topath->firstpath ,
                         & topath->lastpath , & topath->lastline, pool) ;
}

Bool path_copy_lazy(PATHINFO *topath, PATHINFO *frompath, mm_pool_t pool)
{
  HQASSERT( topath , "topath NULL in path_copy" ) ;
  HQASSERT( frompath , "frompath NULL in path_copy" ) ;
  HQASSERT( topath->firstpath == NULL ,
            "Non-empty or uninitialised target in path_copy: possible leak!" ) ;

  topath->bboxtype = frompath->bboxtype ;
  if ( frompath->bboxtype != BBOX_NOT_SET )
    topath->bbox = frompath->bbox ;
  topath->curved = frompath->curved ;
  topath->protection = frompath->protection;
  topath->charpath_id = frompath->charpath_id ;
  topath->flags = frompath->flags ;

  if (frompath->firstpath && frompath->firstpath->shared < MAX_PATH_SHARES &&
      frompath->firstpath->systemalloc == PATHTYPE_DYNMALLOC &&
      !rcbn_enabled()) { /* disable path sharing if recombine is on */
    /* Share the pathlist, don't copy it */
    frompath->firstpath->shared++ ;
    topath->firstpath = frompath->firstpath ;
    topath->lastpath  = frompath->lastpath ;
    topath->lastline  = frompath->lastline ;

    return TRUE ;
  }

  return path_copy_list( frompath->firstpath , & topath->firstpath ,
                         & topath->lastpath , & topath->lastline, pool) ;
}

Bool path_copy_list(PATHLIST *thepath , PATHLIST **fpath ,
                    PATHLIST **lpath , LINELIST **lline,
                    mm_pool_t pool)
{
  register LINELIST *nextline ;
  register LINELIST *templine , *theline ;
  PATHLIST *temppath , *nextpath , *startpath ;

  startpath = temppath = NULL ;
  templine = NULL ;

  if ( thepath ) {
    if ( NULL == (nextpath = startpath = get_path(pool)) )
      return FALSE ;

    do {
      HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(thepath) == PATHTYPE_STATIC ||
               theISystemAlloc(thepath) == PATHTYPE_STACK ||
               theISystemAlloc(thepath) == PATHTYPE_STRUCT,
               "Invalid allocation type for path") ;

      temppath = nextpath;
      if ( NULL == (theISubPath( temppath ) = nextline = get_line(pool)) ) {
        temppath->next = NULL ;
        path_free_list( startpath, pool ) ;
        return FALSE ;
      }
      theline = theISubPath( thepath ) ;
      do {
        HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
                 theISystemAlloc(theline) == PATHTYPE_STATIC ||
                 theISystemAlloc(theline) == PATHTYPE_STACK ||
                 theISystemAlloc(theline) == PATHTYPE_STRUCT,
                 "Invalid allocation type for line") ;

        templine = nextline ;
        theILineType( templine ) = theILineType( theline ) ;
        theILineOrder( templine ) = theILineOrder( theline ) ;
        theX( theIPoint( templine )) = theX( theIPoint( theline )) ;
        theY( theIPoint( templine )) = theY( theIPoint( theline )) ;

        if ( NULL == (templine->next = nextline = get_line(pool)) ) {
          temppath->next = NULL ;
          path_free_list( startpath, pool ) ;
          return FALSE ;
        }
        theline = theline->next ;
      } while ( theline ) ;

      free_line( nextline, pool ) ;
      templine->next = NULL ;

      if ( NULL == (temppath->next = nextpath = get_path(pool)) ) {
        path_free_list( startpath, pool ) ;
        return FALSE ;
      }
      thepath = thepath->next ;
    } while ( thepath ) ;

    free_path( nextpath, pool ) ;
    temppath->next = NULL ;
  }

  if ( fpath )          /* return values of firstpath, lastpath and lastline */
    *fpath = startpath ;
  if ( lpath )
    *lpath = temppath ;
  if ( lline )
    *lline = templine ;

  return TRUE ;
}

/* path_append_subpath_copy adds a copy of a single subpath to the end of the
   path specified. It should be used when the path is statically-allocated
   (e.g. in the path_add_two, path_add_three, path_add_four macros). */
Bool path_append_subpath_copy( PATHLIST *thepath, PATHINFO *topath, mm_pool_t pool )
{
  PATHLIST *pathcopy ;
  LINELIST *lastline ;

  HQASSERT( thepath, "No subpath to copy in path_append_subpath_copy" ) ;
  HQASSERT( ! thepath->next, "More than one subpath in path_append_subpath_copy" ) ;
  HQASSERT( topath, "Nothing to add subpath to in path_append_subpath_copy" ) ;

  if ( ! path_copy_list(thepath, &pathcopy, NULL, &lastline, pool) ) {
    path_free_list(topath->firstpath, pool) ;
    path_init(topath) ;
    return FALSE ;
  }

  path_append_subpath(pathcopy, lastline, topath) ;

  return TRUE ;
}

/* path_append_subpath adds a single subpath to the end of the path
   specified. It should *not* be used when the path is statically-allocated
   (e.g. in the add_two, add_three, add_four macros). */
void path_append_subpath( PATHLIST *thepath, LINELIST *theline, PATHINFO *topath )
{
  HQASSERT( thepath, "No subpath to append in path_append_subpath" ) ;
  HQASSERT( theline, "No last line given for path in path_append_subpath" ) ;
  HQASSERT( ! thepath->next, "More than one subpath in path_append_subpath" ) ;
  HQASSERT( topath, "Nothing to add subpath to in path_append_subpath" ) ;

  HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC,
           "Should not be appending non-allocated path") ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(topath, /*void*/) ;

  if ( ! topath->firstpath )
    topath->firstpath = thepath ;
  else
    topath->lastpath->next = thepath ;

  topath->lastpath = thepath ;
  topath->lastline = theline ;
  topath->bboxtype = BBOX_NOT_SET ;
}

/* path_remove_last_subpath removes the last subpath from the end of a path,
   adjusting the last path and line pointers of the path appropriately. The
   subpath removed is returned. */
void path_remove_last_subpath(PATHINFO *frompath, PATHLIST **removed)
{
  PATHLIST **prevpath, *subpath, *lastpath = NULL ;
  LINELIST *lastline = NULL ;

  HQASSERT(frompath, "No path from which to extract last subpath") ;
  HQASSERT(removed, "Nowhere to put last subpath") ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(frompath, /*void*/) ;

  /* Find the predecessor to the last subpath */
  for ( prevpath = &thePath(*frompath) ;
        (subpath = *prevpath) != (frompath->lastpath) ;
        prevpath = &subpath->next )
    lastpath = subpath ;

  *prevpath = NULL ;

  if ( lastpath ) {
    /* If the trimmed path is not empty, find the last line segment */
    for ( lastline = theSubPath(*lastpath) ;
          lastline->next != NULL ;
          lastline = lastline->next )
      EMPTY_STATEMENT() ;
  }

  (frompath->lastpath) = lastpath ;
  (frompath->lastline) = lastline ;

  *removed = subpath ;
}

Bool path_add_two( PATHINFO *topath ,
                   SYSTEMVALUE x1 , SYSTEMVALUE y1 ,
                   SYSTEMVALUE x2 , SYSTEMVALUE y2 )
{
  static LINELIST close2 = LINELIST_STATIC( CLOSEPATH, 0.0, 0.0, NULL ) ;
  static LINELIST linez2 = LINELIST_STATIC( LINETO, 0.0, 0.0, &close2 ) ;
  static LINELIST lmove2 = LINELIST_STATIC( MOVETO, 0.0, 0.0, &linez2 ) ;
  static PATHLIST pathl2 = PATHLIST_STATIC( &lmove2, NULL ) ;

  HQASSERT( topath != NULL, "topath is null");

  theX( thePoint( lmove2 )) = x1 ;
  theY( thePoint( lmove2 )) = y1 ;
  theX( thePoint( linez2 )) = x2 ;
  theY( thePoint( linez2 )) = y2 ;
  theX( thePoint( close2 )) = x1 ;
  theY( thePoint( close2 )) = y1 ;

  return path_append_subpath_copy( & pathl2 , topath, mm_pool_temp ) ;
}

Bool path_add_three( PATHINFO *topath ,
                     SYSTEMVALUE x1 , SYSTEMVALUE y1 ,
                     SYSTEMVALUE x2 , SYSTEMVALUE y2 ,
                     SYSTEMVALUE x3 , SYSTEMVALUE y3 )
{
  static LINELIST close3 = LINELIST_STATIC( CLOSEPATH, 0.0, 0.0, NULL ) ;
  static LINELIST linez3 = LINELIST_STATIC( LINETO, 0.0, 0.0, &close3 ) ;
  static LINELIST liney3 = LINELIST_STATIC( LINETO, 0.0, 0.0, &linez3 ) ;
  static LINELIST lmove3 = LINELIST_STATIC( MOVETO, 0.0, 0.0, &liney3 ) ;
  static PATHLIST pathl3 = PATHLIST_STATIC( &lmove3, NULL ) ;

  HQASSERT( topath != NULL, "topath is null");

  theX( thePoint( lmove3 )) = x1 ;
  theY( thePoint( lmove3 )) = y1 ;
  theX( thePoint( liney3 )) = x2 ;
  theY( thePoint( liney3 )) = y2 ;
  theX( thePoint( linez3 )) = x3 ;
  theY( thePoint( linez3 )) = y3 ;
  theX( thePoint( close3 )) = x1 ;
  theY( thePoint( close3 )) = y1 ;

  return path_append_subpath_copy( & pathl3, topath, mm_pool_temp ) ;
}

Bool path_add_four( PATHINFO *topath ,
                    SYSTEMVALUE x1 , SYSTEMVALUE y1 ,
                    SYSTEMVALUE x2 , SYSTEMVALUE y2 ,
                    SYSTEMVALUE x3 , SYSTEMVALUE y3 ,
                    SYSTEMVALUE x4 , SYSTEMVALUE y4 )
{
  static LINELIST close4 = LINELIST_STATIC( CLOSEPATH, 0.0, 0.0, NULL ) ;
  static LINELIST linez4 = LINELIST_STATIC( LINETO, 0.0, 0.0, &close4 ) ;
  static LINELIST liney4 = LINELIST_STATIC( LINETO, 0.0, 0.0, &linez4 ) ;
  static LINELIST linex4 = LINELIST_STATIC( LINETO, 0.0, 0.0, &liney4 ) ;
  static LINELIST lmove4 = LINELIST_STATIC( MOVETO, 0.0, 0.0, &linex4 ) ;
  static PATHLIST pathl4 = PATHLIST_STATIC( &lmove4, NULL ) ;

  HQASSERT( topath != NULL, "topath is null");

  theX( thePoint( lmove4 )) = x1 ;
  theY( thePoint( lmove4 )) = y1 ;
  theX( thePoint( linex4 )) = x2 ;
  theY( thePoint( linex4 )) = y2 ;
  theX( thePoint( liney4 )) = x3 ;
  theY( thePoint( liney4 )) = y3 ;
  theX( thePoint( linez4 )) = x4 ;
  theY( thePoint( linez4 )) = y4 ;
  theX( thePoint( close4 )) = x1 ;
  theY( thePoint( close4 )) = y1 ;

  return path_append_subpath_copy( & pathl4, topath, mm_pool_temp ) ;
}

/* Utility functions to translate a path and transform it by a matrix. */
void path_translate( PATHINFO *path, SYSTEMVALUE tx, SYSTEMVALUE ty )
{
  PATHLIST *thepath ;
  LINELIST *theline ;

  HQASSERT( path, "No pathinfo supplied" ) ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(path, /*void*/) ;

  if ( path->bboxtype != BBOX_NOT_SET ) { /* Adjust Bounding Box */
    path->bbox.x1 += tx ;
    path->bbox.x2 += tx ;
    path->bbox.y1 += ty ;
    path->bbox.y2 += ty ;
  }
  thepath = path->firstpath ;
  while (thepath) {
    HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
             theISystemAlloc(thepath) == PATHTYPE_STATIC ||
             theISystemAlloc(thepath) == PATHTYPE_STACK ||
             theISystemAlloc(thepath) == PATHTYPE_STRUCT,
             "Invalid allocation type for path") ;

    theline = theISubPath(thepath) ;
    while (theline) {
      HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(theline) == PATHTYPE_STATIC ||
               theISystemAlloc(theline) == PATHTYPE_STACK ||
               theISystemAlloc(theline) == PATHTYPE_STRUCT,
               "Invalid allocation type for line") ;

      theX(theIPoint(theline)) += tx ;
      theY(theIPoint(theline)) += ty ;
      theline = theline->next ;
    }
    thepath = thepath->next ;
  }
}

/* Transform path in-place by matrix */
void path_transform( PATHINFO *path, OMATRIX *t_matrix )
{
  PATHLIST *thepath ;
  LINELIST *theline ;

  HQASSERT( path != NULL , "path null" ) ;
  HQASSERT( t_matrix != NULL , "t_matrix null" ) ;
  HQASSERT( matrix_assert( t_matrix ) , "matrix incorrectly opt" ) ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(path, /*void*/) ;

  /* Apply the matrix to the given path */
  thepath = path->firstpath ;
  while (thepath) {
    HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
             theISystemAlloc(thepath) == PATHTYPE_STATIC ||
             theISystemAlloc(thepath) == PATHTYPE_STACK ||
             theISystemAlloc(thepath) == PATHTYPE_STRUCT,
             "Invalid allocation type for path") ;

    theline = theISubPath(thepath) ;
    while (theline) {
      HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(theline) == PATHTYPE_STATIC ||
               theISystemAlloc(theline) == PATHTYPE_STACK ||
               theISystemAlloc(theline) == PATHTYPE_STRUCT,
               "Invalid allocation type for line") ;

      MATRIX_TRANSFORM_XY( theX(theIPoint(theline)) , theY(theIPoint(theline)) ,
                           theX(theIPoint(theline)) , theY(theIPoint(theline)) ,
                           t_matrix ) ;
      theline = theline->next ;
    }
    thepath = thepath->next ;
  }

  /* Recalculate the bbox if required */
  if ( path->bboxtype != BBOX_NOT_SET ) {
    if ( ( t_matrix->opt & MATRIX_OPT_1001 ) == 0 ||
         path->bboxtype == BBOX_SETBBOX ) {
      /* No rotation, or setbbox therefore just apply the matrix to the bbox */
      bbox_transform(&path->bbox, &path->bbox, t_matrix) ;
    }
    else {
      /* Matrix includes rotation therefore need recalculate the bbox altogether */
      (void)path_bbox( path , NULL , path->bboxtype|BBOX_SAVE ) ;
    }
  }
}

/* Utility function to count number of subpaths in a path. */
int32 path_count_subpaths( PATHINFO *path )
{
  int32 numpaths ;
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in path_count_subpaths" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in path_count_subpaths" ) ;

  numpaths = 0 ;
  while ( thepath ) {
    LINELIST *theline = theISubPath( thepath ) ;

    HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
             theISystemAlloc(thepath) == PATHTYPE_STATIC ||
             theISystemAlloc(thepath) == PATHTYPE_STACK ||
             theISystemAlloc(thepath) == PATHTYPE_STRUCT,
             "Invalid allocation type for path") ;
    HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
             theISystemAlloc(theline) == PATHTYPE_STATIC ||
             theISystemAlloc(theline) == PATHTYPE_STACK ||
             theISystemAlloc(theline) == PATHTYPE_STRUCT,
             "Invalid allocation type for line") ;

    if ( (theline->next != NULL) &&
         ( int32 )theILineType( theline->next) != MYCLOSE )
      ++numpaths ;
    thepath = thepath->next ;
  }

  return ( numpaths ) ;
}

/* Reverse a path in-place. This reverses the linelists of each sub-path,
   but not the order of the sub-paths. */
void path_reverse_linelists(PATHLIST *thepath, LINELIST **lastline)
{
  LINELIST *linea, *lineb = NULL , *linec, *startline = NULL ;
  uint8 starttype ;

  HQASSERT(thepath, "No path to reverse") ;

  /* Copy the path if it is currently shared */
  /*LAZY_PATH_COPY(thepath,*/ /*void*//*) ;*/
  /* Path should NOT have been copied - this is clippath stuff */
  HQASSERT(thepath->shared == 0,
           "Can't path_reverse_linelists() on a shared path") ;

  while ( thepath ) {
    linea = NULL;
    lineb = startline = theISubPath( thepath );
    linec = lineb->next;
    if (linec) {
      /* linec should never be null really, because there should
         always be at least two nodes on the list */
      starttype = theILineType ( lineb );
      for (;;) {
        HQASSERT(theISystemAlloc(lineb) == PATHTYPE_DYNMALLOC,
                 "Should not be reversing non-allocated lines") ;
        lineb->next = linea;
        if (! linec)
          break;
        theILineType (lineb) = theILineType (linec);
        linea = lineb;
        lineb = linec;
        linec = linec->next;
      }
      /* linea is now the head of the list - patch up the head and tail */
      theILineType (linea) = starttype;
      lineb->next = NULL;
      startline->next = lineb;
      theX (theIPoint (lineb)) = theX (theIPoint (linea));
      theY (theIPoint (lineb)) = theY (theIPoint (linea));
      theISubPath( thepath ) = linea;
    }

    thepath = thepath->next;
  }

  if (lastline)
    *lastline = lineb;
}

/* Reverse the subpaths of a path in-place. This reverses the pathists of the
   path, but not the order of the linelists. */
void path_reverse_subpaths(PATHLIST **pathptr, LINELIST **lastline)
{
  PATHLIST *path , *reversed, *lastpath ;

  HQASSERT(pathptr, "No subpaths to reverse") ;

  /* Complain if the path is currently shared */
  HQASSERT((*pathptr)->shared == 0,
           "Cannot path_reverse_subpaths() on a shared path") ;

  lastpath = path = *pathptr ;
  reversed = NULL ;

  while ( path ) {
    PATHLIST *next = path->next ;

    HQASSERT(theISystemAlloc(path) == PATHTYPE_DYNMALLOC,
             "Should not be reversing non-allocated path") ;

    path->next = reversed ;
    reversed = path ;

    path = next ;
  }

  *pathptr = reversed ;

  if ( lastline ) { /* Find linelist of last subpath */
    LINELIST *line = NULL ;

    if ( lastpath ) {
      line = theISubPath(lastpath) ;
      while ( line->next )
        line = line->next ;
    }

    *lastline = line;
  }
}

/*---------------------------------------------------------------------------*/
/* Create a path from an array of transformed rectangles, obliterating
   previous contents of path. */
Bool path_from_rectangles(PATHINFO *path, RECTANGLE *rects, uint32 nrects,
                          OMATRIX *matrix)
{
  HQASSERT(path, "Null path") ;
  HQASSERT(rects, "Null rectangle") ;
  HQASSERT(matrix, "Null matrix") ;

  path_init(path) ;

  for ( ; nrects > 0 ; --nrects, ++rects ) {
    register SYSTEMVALUE x, y, w, h ;
    register SYSTEMVALUE x1, y1, x2, y2, x3, y3 ;

    /* make rectangle counter clockwise */
    x = rects->x ;
    y = rects->y ;
    if ( (w = rects->w) < 0.0 ) {
      w = -w ;
      x -= w ;
    }
    if ( (h = rects->h) < 0.0 ) {
      h = -h ;
      y -= h ;
    }

    /* Transform rectangle to device space */
    if ( matrix->opt != MATRIX_OPT_BOTH ) {
      MATRIX_TRANSFORM_XY( x, y, x1, y1, matrix) ;
      MATRIX_TRANSFORM_DXY( w, h, x3, y3, matrix) ;
      x3 += x1 ;
      y3 += y1 ;
      x2 = x3 ;
      y2 = y1 ;
    } else {
      MATRIX_TRANSFORM_XY( x, y, x1, y1, matrix) ;
      MATRIX_TRANSFORM_DXY( w , 0.0, x2, y2, matrix) ;
      x2 += x1 ;
      y2 += y1 ;
      MATRIX_TRANSFORM_DXY( 0.0, h , x3, y3, matrix) ;
      x3 += x2 ;
      y3 += y2 ;
    }

    if ( !path_add_four(path, x1, y1, x2, y2, x3, y3,
                        x1 - (x2 - x3), y1 - (y2 - y3)) ) {
      path_free_list(path->firstpath, mm_pool_temp) ;
      return FALSE ;
    }
  }

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* Compare paths. Allows an epsilon value to be supplied, and the bbox and
   flags to be ignored. */
Bool path_compare(PATHINFO *path1, PATHINFO *path2, uint32 flags,
                  SYSTEMVALUE epsilon)
{
  PATHLIST *pl1, *pl2 ;

  HQASSERT(path1 && path2, "Missing path") ;

  if ( (flags & PATH_COMPARE_FLAGS) != 0 &&
       path1->curved != path2->curved )
    return FALSE ;

  if ( (flags & PATH_COMPARE_BBOX) != 0 &&
       path1->bboxtype == path2->bboxtype &&
       path1->bboxtype != BBOX_NOT_SET ) /* bbox same? */
    if ( !bbox_equal(&path1->bbox, &path2->bbox) )
      return FALSE ;

  for ( pl1 = path1->firstpath, pl2 = path2->firstpath ;
        pl1 && pl2 ;
        pl1 = pl1->next, pl2 = pl2->next ) {
    LINELIST *ll1, *ll2 ;
    for ( ll1 = theISubPath(pl1), ll2 = theISubPath(pl2) ;
          ll1 && ll2 ;
          ll1 = ll1->next, ll2 = ll2->next ) {
      if ( theILineType(ll1) != theILineType(ll2) )
        return FALSE ;

      if ( epsilon == 0.0 ) {
        if ( theX(theIPoint(ll1)) != theX(theIPoint(ll2)) ||
             theY(theIPoint(ll1)) != theY(theIPoint(ll2)) )
          return FALSE ;
      } else {
        if ( fabs(theX(theIPoint(ll1)) - theX(theIPoint(ll2))) > epsilon ||
             fabs(theY(theIPoint(ll1)) - theY(theIPoint(ll2))) > epsilon )
          return FALSE ;
      }
    }
    if ( ll1 || ll2 ) /* different lengths of subpath */
      return FALSE ;
  }

  return (pl1 == NULL && pl2 == NULL) ; /* same subpaths? */
}

/*---------------------------------------------------------------------------*/
/* Calculate the storage space required and a checksum for a path */

#define ROTATE_WORD(word, rotate) \
  (uint32)(( (uint32)(word) << (rotate) ) | \
           ( (uint32)(word) >> (sizeof(uint32) * 8 - (rotate))))
#define CHECKSUM_ROT 13 /* rotation for checksum */

void path_checksum(PATHINFO *path, uint32 *checkptr, uint32 *sizeptr)
{
  uint32 checksum = 0 ;
  uint32 size = 0 ;
  LINELIST *theline ;
  PATHLIST *thepath ;

  HQASSERT(path, "No pathinfo ptr") ;
  HQASSERT(checkptr, "No checksum ptr") ;
  HQASSERT(sizeptr, "No size ptr") ;

  if ( path->bboxtype == BBOX_SETBBOX ) {
    checksum ^= (uint32)path->bbox.x1;
    checksum = ROTATE_WORD(checksum, CHECKSUM_ROT) ;
    checksum ^= (uint32)path->bbox.y1;
    checksum = ROTATE_WORD(checksum, CHECKSUM_ROT) ;
    checksum ^= (uint32)path->bbox.x2;
    checksum = ROTATE_WORD(checksum, CHECKSUM_ROT) ;
    checksum ^= (uint32)path->bbox.y2;
  }

  for ( thepath = path->firstpath ; thepath ; thepath = thepath->next ) {
    size += sizeof(PATHLIST) ;
    for ( theline = theISubPath(thepath) ; theline ; theline = theline->next ) {
      size += sizeof(LINELIST) ;
      checksum = ROTATE_WORD(checksum, CHECKSUM_ROT) ;
      checksum ^= (uint32)theX(theIPoint(theline)) ;
      checksum = ROTATE_WORD(checksum, CHECKSUM_ROT) ;
      checksum ^= (uint32)theY(theIPoint(theline)) ;
    }
  }

  *checkptr = checksum ;
  *sizeptr = size ;
}

/*---------------------------------------------------------------------------*/
/* Append a path to another path. Flags determine if the path is patched
   directly into the destination path, or whether it is copied. */
Bool path_append(PATHINFO *topath, PATHINFO *path, uint32 flags)
{
  LINELIST *lastline ;
  PATHLIST *ppath, *lastpath ;

  HQASSERT(path_assert_valid(topath), "destination path invalid" ) ;
  HQASSERT(path_assert_valid(path), "source path invalid" ) ;

  if ( NULL == (ppath = path->firstpath) ) /* Nothing to add? */
    return TRUE ;

  /* Copy the path if it is currently shared */
  LAZY_PATH_COPY(path, FALSE) ;

  topath->curved |= path->curved ;
  if (path->protection) {
    if ( ! topath->protection )
      /* assign if not already protected */
      topath->protection = path->protection;
    else if ( topath->protection != path->protection )
      /* if already protected differently, blanket protection */
      topath->protection = PROTECTED_BLANKET;
  }
  topath->flags |= path->flags ;

  if ( topath->bboxtype != BBOX_NOT_SET ) {
    /* enlarge bounding box (this is what Adobe do) */
    sbbox_t *bbox = & topath->bbox ;
    sbbox_t bboxn ;

    (void)path_bbox(path, & bboxn, BBOX_IGNORE_LEVEL2|BBOX_SAVE|BBOX_LOAD) ;

    bbox_union(bbox, &bboxn, bbox) ;
  }

  if ( (flags & PATH_APPEND_COPY) != 0 ) {
    /* can't just patch in path passed to us */
    if ( ! path_copy_list(path->firstpath, &ppath,
                          &lastpath, &lastline, mm_pool_temp) )
      return FALSE ;
  } else {
    lastpath = path->lastpath ;
    lastline = path->lastline ;
    path_init(path) ; /* patch in path directly; clear original pathinfo */
  }

  /* Append new path to existing path */
  if ( !topath->firstpath )
    topath->firstpath = ppath ;
  else
    topath->lastpath->next = ppath ;

  topath->lastpath = lastpath ;
  topath->lastline = lastline ;

  return TRUE ;
}


#ifdef ASSERT_BUILD
/* Assert that a path is well-formed */
Bool path_assert_valid(PATHINFO *path)
{
  LINELIST *theline ;
  PATHLIST *thepath ;
  LINELIST *line1 , *line2 , *lastline ;

  int32 bezier_count ;

  HQASSERT( path , "NULL path" ) ;

  thepath = path->firstpath ;
  if (!thepath)
    HQASSERT( !path->lastpath , "non-NULL theILastPath" ) ;
  else
    HQASSERT( path->lastpath , "NULL theILastPath" ) ;

  if ( path->bboxtype != BBOX_NOT_SET ) {
    sbbox_t *bbox = & path->bbox ;
    HQASSERT(!bbox_is_empty(bbox), "bbox is empty" ) ;
  }

  while ( thepath ) {
    HQASSERT(theISystemAlloc(thepath) == PATHTYPE_DYNMALLOC ||
             theISystemAlloc(thepath) == PATHTYPE_STATIC ||
             theISystemAlloc(thepath) == PATHTYPE_STACK ||
             theISystemAlloc(thepath) == PATHTYPE_STRUCT,
             "Invalid allocation type for path") ;

    line1 = theISubPath( thepath ) ;
    HQASSERT( line1 , "NULL theline; no (MY)MOVETO" ) ;

    HQASSERT( theILineType( line1 ) == MYMOVETO ||
              theILineType( line1 ) == MOVETO , "wrong first line type" ) ;

    line2 = line1->next ;
    HQASSERT( line2 , "NULL theline; no (MY)CLOSE(PATH)" ) ;

    bezier_count = 0 ;

    for ( theline = lastline = line1 ; theline ; lastline = theline , theline = theline->next ) {
      HQASSERT(theISystemAlloc(theline) == PATHTYPE_DYNMALLOC ||
               theISystemAlloc(theline) == PATHTYPE_STATIC ||
               theISystemAlloc(theline) == PATHTYPE_STACK ||
               theISystemAlloc(theline) == PATHTYPE_STRUCT,
               "Invalid allocation type for line") ;

      HQASSERT( theline != theline->next , "recursive line chain" ) ;
      switch ( theILineType( theline )) {
      case MYMOVETO:
        HQASSERT( theline == line1 , "MYMOVETO must be first in a (sub)path segment" ) ;
        HQASSERT( bezier_count == 0 , "MYMOVETO in middle of cubic" ) ;
        break ;
      case MOVETO:
        HQASSERT( theline == line1 , "MOVETO must be first in a (sub)path segment" ) ;
        HQASSERT( bezier_count == 0 , "MOVETO in middle of cubic" ) ;
        break ;
      case LINETO:
        HQASSERT( theline != line1 , "LINETO can't be first in a (sub)path segment" ) ;
        HQASSERT( bezier_count == 0 , "LINETO in middle of cubic" ) ;
        break ;
      case CURVETO:
        HQASSERT( path->curved , "path marked as not curved" ) ;
        HQASSERT( theline != line1 , "CURVETO can't be first in a (sub)path segment" ) ;
        bezier_count = ( bezier_count + 1 ) % 3 ;
        break ;
      case CLOSEPATH:
        HQASSERT( theline != line1 , "CLOSEPATH can't be first in a (sub)path segment" ) ;
        HQASSERT( bezier_count == 0 , "CLOSEPATH in middle of cubic" ) ;
        HQASSERT( theline->next == NULL , "line segment after a CLOSEPATH" ) ;
        HQASSERT( theX( theIPoint( line1 )) == theX( theIPoint( theline )) &&
                  theY( theIPoint( line1 )) == theY( theIPoint( theline )) ,
                  "start point must be equal to end point" ) ;
        if ( thepath->next == NULL )
          HQASSERT( theline == path->lastline , "last LINE doesn't equal theILastLine" ) ;
        break ;
      case MYCLOSE:
        HQASSERT( theline != line1 , "MYCLOSE can't be first in a (sub)path segment" ) ;
        HQASSERT( bezier_count == 0 , "MYCLOSE in middle of cubic" ) ;
        HQASSERT( theline->next == NULL , "line segment after a CLOSEPATH" ) ;
        HQASSERT( theX( theIPoint( line1 )) == theX( theIPoint( theline )) &&
                  theY( theIPoint( line1 )) == theY( theIPoint( theline )) ,
                  "start point must be equal to end point" ) ;
        if ( thepath->next == NULL )
          HQASSERT( theline == path->lastline , "last LINE doesn't equal theILastLine" ) ;
        break ;
      default:
        HQFAIL( "unknown line type" ) ;
        break ;
      }
    }

    HQASSERT( theILineType( lastline ) == MYCLOSE ||
              theILineType( lastline ) == CLOSEPATH , "wrong last line type" );

    if ( thepath->next == NULL )
      HQASSERT( thepath == path->lastpath , "last path doesn't equal theILastPath" ) ;

    HQASSERT( thepath != thepath->next , "recursive path chain" ) ;
    thepath = thepath->next ;
  }
  return TRUE ;
}
#endif

#if defined( DEBUG_BUILD )
/* Debugging utility functions */
void debug_print_path( PATHINFO *path_ptr )
{
  char *tstring[ ] = { "setbbox" , "moveto" , "rmoveto" , "lineto" ,
                       "rlineto" , "curveto", "rcurveto" , "arc" ,
                       "arcn" ,    "arct" ,   "closepath", "ucache" ,
                       "moveto %implicit%" , "closepath %implicit%" } ;
  PATHINFO *pinfo ;
  PATHLIST *path ;
  LINELIST *line ;
  int32 i ;

  if ( path_ptr )
    pinfo = path_ptr ;
  else
    pinfo = & theIPathInfo( gstateptr ) ;
  path = pinfo->firstpath ;

  if ( path_ptr->bboxtype == BBOX_NOT_SET ) {
    monitorf(( uint8 * )"%% Bounding box not set\n" ) ;
  } else {
    monitorf(( uint8 * )"%% Bounding box [%5.5f,%5.5f %5.5f,%5.5f] ",
             pinfo->bbox.x1, pinfo->bbox.y1,
             pinfo->bbox.x2, pinfo->bbox.y2) ;
    switch ( path_ptr->bboxtype ) {
    case BBOX_IGNORE_NONE:
      monitorf(( uint8 * )"(all points)") ;
      break ;
    case BBOX_IGNORE_LEVEL2:
      monitorf(( uint8 * )"(ignore trailing degenerates)") ;
      break ;
    case BBOX_IGNORE_ALL:
      monitorf(( uint8 * )"(ignore all degenerates)") ;
      break ;
    case BBOX_SETBBOX:
      monitorf(( uint8 * )"(setbbox)") ;
      break ;
    }
    monitorf(( uint8 * )"\n") ;
  }

  for ( i = 0 ; path ; path = path->next , i++ ) {
    int32 curvepts = 3 ;

    monitorf(( uint8 * )"%% Subpath %d:\n" , i ) ;

    for ( line = theISubPath( path ) ; line ; line = line->next) {
      switch ( theILineType(line) ) {
      case CLOSEPATH:
      case MYCLOSE:
        monitorf(( uint8 * )" %s %% %5.5f %5.5f\n" ,
                 tstring[theILineType(line)],
                 theX(theIPoint(line)), theY(theIPoint(line))) ;
        break ;
      case CURVETO:
      case UPATH_RCURVETO:
        monitorf(( uint8 * )" %5.5f %5.5f" ,
                 theX(theIPoint(line)), theY(theIPoint(line))) ;
        if ( --curvepts == 0 ) {
          curvepts = 3 ;
          monitorf(( uint8 * )" %s" , tstring[theILineType(line)]) ;
        }
        monitorf(( uint8 * )"\n") ;
        break ;
      default:
        monitorf(( uint8 * )" %5.5f %5.5f %s\n" ,
                 theX(theIPoint(line)), theY(theIPoint(line)),
                 tstring[theILineType(line)]) ;
        break ;
      }
    }
  }
}

#endif

void init_C_globals_gu_path(void)
{
  fl_ftol = 1.0f ;
  fl_flat = 1.0f ;
  nooflines = 0 ;
  flatorder = 1 ;
  flatline = NULL ;
}

/* Log stripped */
