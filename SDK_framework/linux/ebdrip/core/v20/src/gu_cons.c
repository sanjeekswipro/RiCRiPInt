/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!src:gu_cons.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Construction of paths using gstate transformations.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"

#include "matrix.h"
#include "gu_ctm.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "system.h"
#include "gu_path.h"
#include "constant.h"
#include "gstate.h"
#include "stacks.h"
#include "pathcons.h"

#include "gu_cons.h"

Bool checkbbox = TRUE ; /* rangecheck against gstate bounding box? */


/* So far, empirically derived - from the debugger and job from 25661,
 * coordinates from clippapth pathbbox moveto rlineto rlinteo rlineto the
 * relative error was no larger than ~5*10^-9 regardless of resolution!
 */
#define BBOX_REL_ERROR  (1e-8)

/* Test for point outside of bbox includes a relative error check in case x/y is
 * near enough the bbox limit after fp rounding.  Multiple by bbox limit to
 * remove having to handle divide by 0 (bbox limit on page axis).  For very
 * large differences in x/y and bbox limit the test is bound to fail.
 * NOTE: Test is done in device space so y axis is flipped.
 */
#define PT_OUTSIDE_BBOX(bbox, x, y) \
  ( (((x) < (bbox)->x1) && ((bbox)->x1 - (x) > max(fabs((bbox)->x1), fabs(x))*BBOX_REL_ERROR)) || \
    (((x) > (bbox)->x2) && ((x) - (bbox)->x2 > max(fabs((bbox)->x2), fabs(x))*BBOX_REL_ERROR)) || \
    (((y) < (bbox)->y1) && ((bbox)->y1 - (y) > max(fabs((bbox)->y1), fabs(y))*BBOX_REL_ERROR)) || \
    (((y) > (bbox)->y2) && ((y) - (bbox)->y2 > max(fabs((bbox)->y2), fabs(y))*BBOX_REL_ERROR)) )

#define BBOX_CHECK_PT(path, x, y) MACRO_START \
  if ( path->bboxtype == BBOX_SETBBOX ) { \
    if ( checkbbox ) { \
      /* Rangecheck against setbbox */ \
      sbbox_t* bbox = &path->bbox; \
      if ( PT_OUTSIDE_BBOX(bbox, (x), (y)) ) { \
        return(error_handler(RANGECHECK)); \
      } \
    } \
  } else { /* Invalidate existing bbox */ \
    path->bboxtype = BBOX_NOT_SET; \
  } \
MACRO_END

#define BBOX_CHECK_PTS(path, n, args) MACRO_START \
  if ( path->bboxtype == BBOX_SETBBOX ) { \
    if ( checkbbox ) { \
      /* Rangecheck against setbbox */ \
      SYSTEMVALUE* pts = args; \
      sbbox_t* bbox = &path->bbox; \
      int32 count = (n); \
      do { \
        if ( PT_OUTSIDE_BBOX(bbox, pts[0], pts[1]) ) { \
          return(error_handler(RANGECHECK)); \
        } \
        pts += 2; \
      } while ( --count > 0 ); \
    } \
  } else { /* Invalidate existing bbox */ \
    path->bboxtype = BBOX_NOT_SET; \
  } \
MACRO_END

/* ----------------------------------------------------------------------------
   function:            ADD_PATHLIST(..)       author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           x , y , type .
   description:

  Utility function - adds a PATHLIST & LINELIST segment to the current path.

---------------------------------------------------------------------------- */
#define ADD_PATHLIST( _x, _y, _type, _path ) MACRO_START \
  LINELIST *_newline_ ;                                  \
  PATHLIST *_newpath_ ;                                  \
                                                         \
  if ( NULL == ( _newline_ = get_line(mm_pool_temp)))    \
    return FALSE ;                                       \
  if ( NULL == ( _newpath_ = get_path(mm_pool_temp))) {  \
    free_line( _newline_, mm_pool_temp ) ;               \
    return FALSE ;                                       \
  }                                                      \
  ( (_path) )->lastpath->next = _newpath_ ;              \
  _newpath_->next = NULL ;                               \
  theISubPath( _newpath_ ) = _newline_ ;                 \
                                                         \
  theILineType( _newline_ ) = ( uint8 )(_type) ;         \
  theILineOrder( _newline_ ) = ( uint8 )0 ;              \
  theX( theIPoint( _newline_ )) = (_x) ;                 \
  theY( theIPoint( _newline_ )) = (_y) ;                 \
  _newline_->next = NULL ;                               \
                                                         \
  ( (_path) )->lastline = _newline_ ;                    \
  ( (_path) )->lastpath = _newpath_ ;                    \
MACRO_END

void init_C_globals_gu_cons(void)
{
   checkbbox = TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            add_m(..)          author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           xoffset , yoffset .
   description:

   Utility function - adds a moveto segment to the current path.

---------------------------------------------------------------------------- */
/* NOTE: This function leaves the values in the args array unchanged */
Bool gs_moveto(Bool absolute, SYSTEMVALUE args[ 2 ], PATHINFO *path)
{
  LINELIST *theline ;
  SYSTEMVALUE dx , dy ;

  HQASSERT( path , "path NULL in gs_moveto" ) ;

  MATRIX_TRANSFORM_DXY( args[ 0 ] , args[ 1 ] , dx , dy , & theIgsPageCTM( gstateptr )) ;
  if ( absolute ) {
    dx += theIgsPageCTM( gstateptr ).matrix[ 2 ][ 0 ] ;
    dy += theIgsPageCTM( gstateptr ).matrix[ 2 ][ 1 ] ;
  }
  else {
    theline = path->lastline ;
    if ( ! theline )
      return error_handler( NOCURRENTPOINT ) ;
    dx += theX( theIPoint( theline )) ;
    dy += theY( theIPoint( theline )) ;
  }

  BBOX_CHECK_PT(path, dx, dy);

  return path_moveto(dx, dy, MOVETO, path) ;
}

/* ----------------------------------------------------------------------------
   function:            add_l(..)          author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           xoffset , yoffset .
   description:

   Utility function - adds a lineto segment to the current path.

---------------------------------------------------------------------------- */
/* NOTE: This function leaves the values in the args array unchanged */
Bool gs_lineto(Bool absolute, Bool stroked, SYSTEMVALUE args[ 2 ], PATHINFO *path)
{
  LINELIST *theline ;
  SYSTEMVALUE dx , dy ;

  HQASSERT( path , "path NULL in gs_lineto" ) ;

  path->charpath_id = 0 ; /* Y 20Aug97 it is not a charpath anymore */
  theline = path->lastline ;
  if ( ! theline )
    return error_handler( NOCURRENTPOINT ) ;

  MATRIX_TRANSFORM_DXY( args[ 0 ] , args[ 1 ] , dx , dy , & theIgsPageCTM( gstateptr )) ;
  if ( absolute ) {
    dx += theIgsPageCTM( gstateptr ).matrix[ 2 ][ 0 ] ;
    dy += theIgsPageCTM( gstateptr ).matrix[ 2 ][ 1 ] ;
  }
  else {
    dx += theX( theIPoint( theline )) ;
    dy += theY( theIPoint( theline )) ;
  }

  BBOX_CHECK_PT(path, dx, dy);

  switch ( (int32) (theILineType( theline )) ) {
  case MYCLOSE:
    theILineType( theline ) = LINETO ;
    theX( theIPoint( theline )) = dx ;
    theY( theIPoint( theline )) = dy ;
    SET_LINELIST_STROKED( theline, stroked ) ;
    break ;
  case CLOSEPATH:
    ADD_PATHLIST( theX( theIPoint( theline )) ,
                  theY( theIPoint( theline )) ,
                  MYMOVETO, path ) ;
    /* FALLTHRU */
  default:
    return path_segment( dx, dy, LINETO, stroked, path ) ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            add_c(..)          author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           xoffset , yoffset .
   description:

   Utility function - adds a curveto segment to the current path.

---------------------------------------------------------------------------- */
/* NOTE: This function leaves the values in the args array unchanged */
/* N.B. Any extra cases added to handle any linetypes differently need
        to be reflected in pdfop_v in pdfpseg.c */

Bool gs_curveto(Bool absolute, Bool stroked, register SYSTEMVALUE args[ 6 ], PATHINFO *path)
{
  int32 i ;
  register SYSTEMVALUE xoffset , yoffset ;
  SYSTEMVALUE coords[ 6 ] ;
  register SYSTEMVALUE *coordptr = coords ;
  register LINELIST *theline, *templine ;

  HQASSERT( path , "path NULL in gs_curveto" ) ;


  path->charpath_id = 0 ; /* Y 20Aug97 it is not a charpath anymore */
  theline = path->lastline ;
  if ( ! theline )
    return error_handler( NOCURRENTPOINT ) ;

  if ( absolute ) {
    xoffset = theIgsPageCTM( gstateptr ).matrix[ 2 ][ 0 ] ;
    yoffset = theIgsPageCTM( gstateptr ).matrix[ 2 ][ 1 ] ;
  }
  else {
    xoffset = theX( theIPoint( theline )) ;
    yoffset = theY( theIPoint( theline )) ;
  }

  for ( i = 0 ; i < 3 ; ++i ) {
    register SYSTEMVALUE dx , dy ;
    register SYSTEMVALUE ux = *args++ ;
    register SYSTEMVALUE uy = *args++ ;

    MATRIX_TRANSFORM_DXY( ux , uy , dx , dy , & theIgsPageCTM( gstateptr )) ;
    dx += xoffset ;
    dy += yoffset ;
    *coordptr++ = dx ;
    *coordptr++ = dy ;
  }

  switch ( (int32) (theILineType( theline )) ) {
  case MYCLOSE: /* Drop MYCLOSE and replace with bezier */
    templine = theISubPath( path->lastpath ) ;
    while ( templine->next != theline )
      templine = templine->next ;
    free_line( theline, mm_pool_temp ) ;
    templine->next = NULL ;
    path->lastline = templine ;
    BBOX_CHECK_PTS(path, 3, coords);
    if ( ! path_curveto( coords, stroked, path ))
      return FALSE ;
    break ;
  case CLOSEPATH:
    ADD_PATHLIST( theX( theIPoint( theline )) ,
                  theY( theIPoint( theline )) ,
                  MYMOVETO, path ) ;
    /* FALLTHRU */
  default:
    BBOX_CHECK_PTS(path, 3, coords);
    if ( ! path_curveto( coords, stroked, path ))
      return FALSE ;
  }

  return TRUE ;
}

/* Promotes a quadratic bezier to a cubic bezier (the rip can only handle cubic beziers). */
Bool gs_quadraticcurveto(Bool absolute, Bool stroked, SYSTEMVALUE args[ 4 ], PATHINFO *path)
{
  register SYSTEMVALUE xoffset , yoffset ;
  SYSTEMVALUE coords[ 6 ] ;
  register LINELIST *theline, *templine ;
  SYSTEMVALUE cpx , cpy ;
  register SYSTEMVALUE dx , dy ;

  HQASSERT( path , "path NULL in gs_quadraticcurveto" ) ;


  path->charpath_id = 0 ; /* Y 20Aug97 it is not a charpath anymore */
  theline = path->lastline ;
  if ( ! theline )
    return error_handler( NOCURRENTPOINT ) ;

  if ( absolute ) {
    xoffset = theIgsPageCTM( gstateptr ).matrix[ 2 ][ 0 ] ;
    yoffset = theIgsPageCTM( gstateptr ).matrix[ 2 ][ 1 ] ;
  }
  else {
    xoffset = theX( theIPoint( theline )) ;
    yoffset = theY( theIPoint( theline )) ;
  }

  /* Apply the ctm to the quadratic control point. */
  MATRIX_TRANSFORM_DXY( args[0] , args[1] , dx , dy , & theIgsPageCTM( gstateptr )) ;
  cpx = dx + xoffset ;
  cpy = dy + yoffset ;

  /* Apply the ctm to the quadratic end point (the cubic start and end points
     are idential to the quadratic start and end points). */
  MATRIX_TRANSFORM_DXY( args[2] , args[3] , dx , dy , & theIgsPageCTM( gstateptr )) ;
  coords[4] = dx + xoffset ;
  coords[5] = dy + yoffset ;

  /*
    A cubic Bézier curve may be viewed as:

      x = ax*t3 + bx*t2 + cx*t +dx
      y = ay*t3 + by*t2 + cy*t +dy

    Where

      dx = P0.x                     dy = P0.y
      cx = 3*P1.x-3*P0.x            cy = 3*P1.y-3*P0.y
      bx = 3*P2.x-6*P1.x+3*P0.x     by = 3*P2.y-6*P1.y+3*P0.y
      ax = P3.x-3*P2.x+3*P1.x-P0.x  ay = P3.y-3*P2.y+3*P1.y-P0.y

    And a quadratic Bézier curve:

      dx = P0.x                     dy = P0.y
      cx = 2*P1.x-2*P0.x            cy = 2*P1.y-2*P0.y
      bx = P2.x-2*P1.x+P0.x         by = P2.y-2*P1.y+P0.y

    Any quadratic spline can be expressed as a cubic (where the cubic term is
    zero). The end points of the cubic will be the same as the quadratic's.

      CP0 = QP0
      CP3 = QP2

    The two control points for the cubic are:

      CP1 = QP0 + 2/3 *(QP1-QP0)
      CP2 = CP1 + 1/3 *(QP2-QP0)

    Reduced equations for the new cubic control points: */

  coords[0] = ( theX( theIPoint( theline )) + 2.0 * cpx ) / 3.0 ;
  coords[1] = ( theY( theIPoint( theline )) + 2.0 * cpy ) / 3.0 ;

  coords[2] = ( coords[4] + 2.0 * cpx ) / 3.0 ;
  coords[3] = ( coords[5] + 2.0 * cpy ) / 3.0 ;


  switch ( theILineType(theline) ) {
  case MYCLOSE: /* Drop MYCLOSE and replace with bezier */
    templine = theISubPath( path->lastpath ) ;
    while ( templine->next != theline )
      templine = templine->next ;
    free_line( theline, mm_pool_temp ) ;
    templine->next = NULL ;
    path->lastline = templine ;
    BBOX_CHECK_PTS(path, 3, coords);
    if ( ! path_curveto( coords, stroked, path ))
      return FALSE ;
    break ;
  case CLOSEPATH:
    ADD_PATHLIST( theX( theIPoint( theline )) ,
                  theY( theIPoint( theline )) ,
                  MYMOVETO, path ) ;
    /* FALLTHRU */
  default:
    BBOX_CHECK_PTS(path, 3, coords);
    if ( ! path_curveto( coords, stroked, path ))
      return FALSE ;
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            add_arc()          author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           orient , values .
   description:

   Utility function - adds a circular arc to the current path, by
   approximating an arc by a number of Bezier curves. This is done with
   sufficient accuracy to give a faithful  rendition of an arc. The arc is
   built with an orientation given by flags:
   ARCT_ARC => anticlockwise, ARCT_ARCN => clockwise.

---------------------------------------------------------------------------- */

/* NOTE: This function leaves the values in the values array unchanged */

Bool gs_arcto(int32 flags, Bool stroked, register SYSTEMVALUE values[5], PATHINFO *path)
{
  int32 arcto_is_arc;
  SYSTEMVALUE   arc_sign;
  register SYSTEMVALUE x , y ;
  register SYSTEMVALUE xd1 , yd1 ;
  register SYSTEMVALUE xd2 , yd2 ;
  SYSTEMVALUE r ;

  register SYSTEMVALUE angle, t1, axis ;
  register SYSTEMVALUE x_start, y_start ;
  register SYSTEMVALUE x_end, y_end ;

  register LINELIST *theline ;

  SYSTEMVALUE args[ 6 ] ;

  HQASSERT((((flags&(ARCT_ARC|ARCT_ARCN)) != 0) || ((flags&(ARCT_LINETO|ARCT_MOVETO)) != 0)),
           "gs_arcto: no arc or movement defined");
  HQASSERT((((flags&(ARCT_ARC|ARCT_ARCN)) != (ARCT_ARC|ARCT_ARCN))),
           "gs_arcto: both an arc and arcn specified");
  HQASSERT((((flags&(ARCT_LINETO|ARCT_MOVETO)) != (ARCT_LINETO|ARCT_MOVETO))),
           "gs_arcto: both a moveto and lineto specified");

  path->charpath_id = 0 ; /* Y 20Aug97 it is not a charpath anymore */

  /* Adjust angles so that they have the correct values, i.e. for arc
  we must have ang1 <= ang2, ( the other way round for arcn ).
  Also find angular difference between start angle and next axis in
  direction of arc. */

  arcto_is_arc = ((flags&ARCT_ARC) != 0);
  arc_sign = (arcto_is_arc ? 1.0 : -1.0);
  t1 = values[ 3 ] ;
  angle = values[ 4 ] ;
  r = 2 * PI ;
  x = PI * 0.5 ;
  axis = (int32)(t1 / x) * x ;
  if ( arcto_is_arc ) {
    while ( t1 > angle )
      angle += r ;
    if ( t1 - axis > EPSILON )
      axis += x ;
    HQASSERT( axis > t1 - EPSILON,
             "Next axis anti-clockwise is wrong in gs_arcto" ) ;
  } else {
    while ( t1 < angle )
      angle -= r ;
    if ( axis - t1 > EPSILON )
      axis -= x ;
    HQASSERT( axis < t1 + EPSILON,
               "Next axis clockwise is wrong in gs_arcto" ) ;
  }
  axis -= t1 ;

/* Add either a straight line or move to, the beginning of the arc. */
  x = values[ 0 ] ;
  y = values[ 1 ] ;
  r = values[ 2 ] ;
  xd1 = r * cos( t1 ) ;
  yd1 = r * sin( t1 ) ;
  x_end = x + xd1 ;
  y_end = y + yd1 ;

  MATRIX_TRANSFORM_XY( x_end, y_end, xd2, yd2, & theIgsPageCTM( gstateptr )) ;

  BBOX_CHECK_PT(path, xd2, yd2);

  theline = path->lastline ;
  if ( ! theline ) {
    if ( ! path_moveto( xd2, yd2, MOVETO, path ))
      return FALSE ;
  }
  else {
    if ( (flags&ARCT_MOVETO) != 0 ) {
      if ( ! path_moveto( xd2, yd2, MYMOVETO, path) )
        return FALSE ;
    } else if ( (flags&ARCT_LINETO) != 0 ) {
      switch ( theILineType( theline )) {
      case MYCLOSE :
        theILineType( theline ) = LINETO ;
        theX( theIPoint( theline )) = xd2 ;
        theY( theIPoint( theline )) = yd2 ;
        SET_LINELIST_STROKED( theline, stroked ) ;
        break ;
      case CLOSEPATH :
        ADD_PATHLIST( theX( theIPoint( theline )) ,
                      theY( theIPoint( theline )) , MYMOVETO, path ) ;
        /* FALLTHRU */
      default:
        if ( !path_segment( xd2 , yd2 , LINETO , stroked , path ) )
          return FALSE ;
      }
    }
  }

  /* Convert angle to amount left to do */
  angle = fabs( angle - t1 ) ; /* angle left to do */

  r = values[ 2 ] ;

  /* If radius equals zero, or ang1 equals ang2, then have a degenerate
     circle, and so have finished. Unfortunately, we can't just test
     fabs(r) < EPSILON, because we're in userspace, and so if we have a very
     large scaling on the CTM, it could yield a perfectly reasonable curve.
     (e.g. Genoa CET tests 11-20..11-28.) So we just test if radius == 0. */
  if ( angle < EPSILON || r == 0.0 ) { /* can't test r against EPSILON */
     return TRUE ;
  }

  /* We now subdivide the same way as Adobe do; curves boundaries are at 0, 90,
     180, and 270 degrees (in user space), with initial and final portions if
     necessary. This has the nice property that the control points for the
     arc-curve conversion are all within an orthogonal square (in  userspace)
     with sides of length twice the radius, centred at the arc centre. */

  xd1 = fabs( axis ) ;
  if ( angle > xd1 ) { /* subdivide at each axis */
    angle -= xd1 ;
    t1 += axis ;
    xd2 = r * cos( t1 ) ; /* vector from centre to end point */
    yd2 = r * sin( t1 ) ;
    if ( xd1 > EPSILON ) { /* do initial segment up to first axis */
      register SYSTEMVALUE temp ;

      x_start = x_end ; /* start point in user space */
      y_start = y_end ;
      x_end = x + xd2 ; /* end point in user space */
      y_end = y + yd2 ;

      /* The next couple of bits find the intersection point of the
         tangents of the arc at the segment endpoints, then place the control
         points on those tangents. The intersection is found by bisecting the
         angle, and constructing a right-angle triangle with its hypotenuse on
         the bisected angle and its adjacent on the vector from the arc centre
         to the end point of the segment. The opposite of this triangle is
         added to the end point to give the intersection point. Start with
         ratio of opposite/adjacent of bisected angle, adjusted for clockwise
         or anticlockwise arcs: */
      xd1 = tan( axis * 0.5 ) ;
      yd1 = -xd2 * xd1 ; /* vector from end pt to intersection point */
      xd1 = yd2 * xd1 ;

      /*  temp = CIRCLE_FACTOR ; */
      temp = cos ( axis * 0.5 ) ;
      temp = (4.0 * temp) / (3.0 * (1.0 + temp)) ;

      /* Now add circle factor * tangent vector to end points to find off-curve
         points. */
      x_start += (x_end + xd1 - x_start) * temp ; /* first ctrl point */
      y_start += (y_end + yd1 - y_start) * temp ;
      xd1 = x_end + xd1 * temp ; /* second ctrl point */
      yd1 = y_end + yd1 * temp ;

/* Set up bezier's control points. */
      MATRIX_TRANSFORM_XY( x_start, y_start, args[ 0 ], args[ 1 ], & theIgsPageCTM( gstateptr )) ;
      MATRIX_TRANSFORM_XY( xd1    , yd1    , args[ 2 ], args[ 3 ], & theIgsPageCTM( gstateptr )) ;
      MATRIX_TRANSFORM_XY( x_end  , y_end  , args[ 4 ], args[ 5 ], & theIgsPageCTM( gstateptr )) ;

      BBOX_CHECK_PTS(path, 3, args);
      if ( ! path_curveto( args, stroked, path ) )
        return FALSE ;
    }

    /* Calculate vectors for quadrants. (xd2,yd2) is the vector to the current
       start point from the centre. (xd1, yd1) is the vector from the end point
       of the quadrant to the last control point. */
    axis = CIRCLE_FACTOR ; /* re-use axis variable */
    yd1 = yd2 * axis ;
    xd1 = xd2 * axis ;

    axis = PI * 0.5 ; /* re-use axis variable again */

    while ( angle + EPSILON >= axis ) { /* do quadrants */
      register SYSTEMVALUE xd3, yd3 ;

      /* Rotate vectors clockwise or anti-clockwise, depending on arc type.
         After these operations, (xd2,yd2) is the vector from the centre to the
         end point, and (xd3,yd3) is the vector from the start point to the
         first control point. */
      angle -= axis ;
      if ( arcto_is_arc ) {
        t1 += axis ;
        xd3 = -yd2 ; yd2 = xd2 ; xd2 = xd3 ;
        xd3 = -yd1 ; yd3 = xd1 ;
      } else {
        t1 -= axis ;
        xd3 = yd2 ; yd2 = -xd2 ; xd2 = xd3 ;
        xd3 = yd1 ; yd3 = -xd1 ;
      }

      x_start = x_end + xd3 ;
      y_start = y_end + yd3 ;

      x_end = x + xd2 ;
      y_end = y + yd2 ;

/* Set up bezier's control points. */
      MATRIX_TRANSFORM_XY( x_start     , y_start     , args[ 0 ], args[ 1 ], & theIgsPageCTM( gstateptr )) ;
      MATRIX_TRANSFORM_XY( x_end + xd1 , y_end + yd1 , args[ 2 ], args[ 3 ], & theIgsPageCTM( gstateptr )) ;
      MATRIX_TRANSFORM_XY( x_end       , y_end       , args[ 4 ], args[ 5 ], & theIgsPageCTM( gstateptr )) ;

      xd1 = xd3 ; /* update control point vector */
      yd1 = yd3 ;

      BBOX_CHECK_PTS(path, 3, args);
      if ( ! path_curveto( args, stroked, path ))
        return FALSE ;
    }
  }

  /* Do final section; this need not start at a multiple of 90 degrees,
     because we may have a small arc segment which doesn't cross an axis. */
  if ( angle > EPSILON ) {
    register SYSTEMVALUE temp ;

    angle *= arc_sign ; /* make angle signed again */
    r = values[ 2 ] ; /* radius */
    t1 += angle ; /* final angle */

    xd2 = r * cos( t1 ) ; /* vector from centre to end point */
    yd2 = r * sin( t1 ) ;

    x_start = x + xd2 ; /* end point in user space */
    y_start = y + yd2 ; /* start point is [xy]_end! */

    xd1 = tan( angle * 0.5 ) ;
    yd1 = -xd2 * xd1 ; /* vector from end pt to intersection point */
    xd1 = yd2 * xd1 ;

    /*  temp = CIRCLE_FACTOR ; */
    temp = cos ( angle * 0.5 ) ;
    temp = (4.0 * temp) / (3.0 * (1.0 + temp)) ;

    /* Now add circle factor * tangent vector to end points to find off-curve
       points. */
    x_end += (x_start + xd1 - x_end) * temp ; /* first ctrl point */
    y_end += (y_start + yd1 - y_end) * temp ;
    xd1 = x_start + xd1 * temp ; /* second ctrl point */
    yd1 = y_start + yd1 * temp ;

/* Set up bezier's control points. */
    MATRIX_TRANSFORM_XY( x_end  , y_end  , args[ 0 ], args[ 1 ], & theIgsPageCTM( gstateptr )) ;
    MATRIX_TRANSFORM_XY( xd1    , yd1    , args[ 2 ], args[ 3 ], & theIgsPageCTM( gstateptr )) ;
    MATRIX_TRANSFORM_XY( x_start, y_start, args[ 4 ], args[ 5 ], & theIgsPageCTM( gstateptr )) ;

    BBOX_CHECK_PTS(path, 3, args);
    return path_curveto( args, stroked, path ) ;
  }

  return TRUE ;
}

static inline SYSTEMVALUE angle_between_vectors(SYSTEMVALUE ux, SYSTEMVALUE uy,
                                                SYSTEMVALUE vx, SYSTEMVALUE vy)
{
  SYSTEMVALUE angle;

  /* This degenerate case should already have been handled. */
  HQASSERT(ux != 0.0 || uy != 0.0, "ux and uy are both zero - cannot calculate angle");
  HQASSERT(vx != 0.0 || vy != 0.0, "vx and vy are both zero - cannot calculate angle");

  angle = atan2(ux*vy-uy*vx, ux*vx+uy*vy);
  if (angle < 0)
    angle = 2.0 * PI + angle;

  return angle;
}

Bool gs_ellipticalarcto(Bool absolute, Bool stroked, Bool largearc, Bool sweepflag,
                        SYSTEMVALUE rx, SYSTEMVALUE ry, SYSTEMVALUE phi,
                        SYSTEMVALUE x2, SYSTEMVALUE y2, PATHINFO *path)
{
  SYSTEMVALUE x1, y1;
  double sinphi, cosphi, x1prime, y1prime, numerator, root;
  double cxprime, cyprime, cx, cy, theta1, dtheta;
  OMATRIX oldctm;
  Bool ctm_modified = FALSE;

  /* We're doing the calculations in user space as we end up calling gs_arcto
     for the final arcs. */
  if (! gs_currentpoint(path, &x1, &y1))
    return FALSE;

  if (! absolute) {
    x2 += x1;
    y2 += y1;
  }

  /* 1. Correction of out-of-range radii according SVG rules
     http://www.w3.org/TR/SVG/implnote.html#ArcImplementationNotes */

  /* If end points of the arc are nearly the same, then the arc can be omitted. */
  {
    SYSTEMVALUE dspace_x1, dspace_y1, dspace_x2, dspace_y2 ;
    MATRIX_TRANSFORM_XY( x1 , y1 , dspace_x1 , dspace_y1 , & theIgsPageCTM( gstateptr )) ;
    MATRIX_TRANSFORM_XY( x2 , y2 , dspace_x2 , dspace_y2 , & theIgsPageCTM( gstateptr )) ;
    if (fabs(dspace_x1 - dspace_x2) < EPSILON && fabs(dspace_y1 - dspace_y2) < EPSILON)
      return TRUE;
  }

  /* Ensure radii are non-zero.  If either radii are zero treat as straight line
     between the two end points. */
  if (rx == 0.0f || ry == 0.0f) {
    SYSTEMVALUE args[2];
    args[0] = x2;
    args[1] = y2;
    return gs_lineto(TRUE, stroked, args, path);
  }

  /* Ensure radii are positive (take absolute values). */
  if (rx < 0.0)
    rx = -rx;
  if (ry < 0.0)
    ry = -ry;

  sinphi = sin( phi );
  cosphi = cos( phi );

  /* Compute (x1', y1') according to the formula F.6.5.1 */
  x1prime =  cosphi * (x1-x2)*0.5 + sinphi * (y1-y2)*0.5;
  y1prime = -sinphi * (x1-x2)*0.5 + cosphi * (y1-y2)*0.5;

  /* 2. Conversion from endpoint to center parameterization according to
     http://www.w3.org/TR/SVG/implnote.html#ArcImplementationNotes F6.5 */

  /* Compute (cX', cY') according to the formula F.6.5.2 */
  numerator = rx*rx * ry*ry - rx*rx * y1prime*y1prime - ry*ry * x1prime*x1prime;
  if (numerator < 0.0) {
    /* The ellipse is not big enough to span the end points so scale
       the radii until it is just big enough. */
    SYSTEMVALUE rscale =(y1prime*y1prime)/(ry*ry) + (x1prime*x1prime)/(rx*rx);
    rscale = sqrt(rscale);
    rx = rx * rscale;
    ry = ry * rscale;
    root = 0.0;
  } else {
    SYSTEMVALUE denominator = rx*rx * y1prime*y1prime + ry*ry * x1prime*x1prime;
    root = sqrt(numerator / denominator);
    if (largearc == sweepflag)
      root = -root;
  }

  cxprime = root *  rx*y1prime/ry;
  cyprime = root * -ry*x1prime/rx;

  /* Compute (cX, cY) from (cX', cY') F.6.5.3 */
  cx = cosphi * cxprime - sinphi * cyprime + (x1+x2)*0.5;
  cy = sinphi * cxprime + cosphi * cyprime + (y1+y2)*0.5;

  /* Compute theta1 and delta-theta F.6.5.5 */
  {
    /* use temporary variables here for the VxWorks compiler which
     * cannot cope with "complex" floating point expressions
     */
    SYSTEMVALUE x1p  = (x1prime-cxprime) / rx;
    SYSTEMVALUE y1p  = (y1prime-cyprime) / ry;
    SYSTEMVALUE mx1p = (-x1prime-cxprime) / rx;
    SYSTEMVALUE my1p = (-y1prime-cyprime) / ry;

    theta1 = angle_between_vectors(1.0, 0.0, x1p, y1p);
    dtheta = angle_between_vectors(x1p, y1p, mx1p, my1p);
  }

  if (!sweepflag && dtheta>0)
    dtheta -= 2.0*PI;
  else if (sweepflag && dtheta<0)
    dtheta += 2.0*PI;

  /* 3. Modify CTM to transform resulting circle to an ellipse rotated to phi axis. */

  MATRIX_COPY(&oldctm, &thegsPageCTM(*gstateptr));

  if (phi != 0.0) {
    /* phi is the angle from the x-axis of the current coordinate system
       to the x-axis of the ellipse. */
    OMATRIX adjust;

    MATRIX_00(&adjust) = cosphi ;
    MATRIX_01(&adjust) = sinphi ;
    MATRIX_10(&adjust) = -sinphi ;
    MATRIX_11(&adjust) = cosphi ;
    MATRIX_20(&adjust) = -cosphi * cx + sinphi * cy + cx ;
    MATRIX_21(&adjust) = -sinphi * cx - cosphi * cy + cy ;
    MATRIX_SET_OPT_BOTH(&adjust) ;

    gs_modifyctm(& adjust);
    ctm_modified = TRUE;
  }

  if ( fabs(rx - ry) != 0.0 ) {
    /* The radii are significantly different (even a small amount may be
       significant, hence we don't use an inequality test against an
       epsilon), so re-scale the CTM and transform the radius and center to
       compensate. */
    OMATRIX adjust;

    MATRIX_00(&adjust) = rx ;
    MATRIX_01(&adjust) = 0.0 ;
    MATRIX_10(&adjust) = 0.0 ;
    MATRIX_11(&adjust) = ry ;
    MATRIX_20(&adjust) = cx ;
    MATRIX_21(&adjust) = cy ;
    MATRIX_SET_OPT_BOTH(&adjust) ;

    rx = 1.0; ry = 1.0;
    cx = 0.0; cy = 0.0;

    gs_modifyctm(& adjust);
    ctm_modified = TRUE;
  }

  /* 4. Draw the ellipse using a single arcto call. */
  {
    Bool success;
    SYSTEMVALUE args[5];

    args[0] = cx;
    args[1] = cy;
    args[2] = rx;
    args[3] = theta1;
    args[4] = theta1 + dtheta;

    success = gs_arcto(sweepflag ? (ARCT_LINETO|ARCT_ARC) :
                       (ARCT_LINETO|ARCT_ARCN),
                       stroked, args, path);

    if ( ctm_modified )
      success = gs_setctm(&oldctm, FALSE) && success;

    if (! success)
      return FALSE;
  }

  return TRUE;
}

/*
Log stripped */
