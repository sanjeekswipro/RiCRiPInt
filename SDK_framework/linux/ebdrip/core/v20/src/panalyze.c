/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:panalyze.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS path analysis functions
 */

#include "core.h"
#include "swerrors.h"

#include "bitblts.h"
#include "display.h"
#include "matrix.h"
#include "objects.h"
#include "graphics.h"
#include "gstate.h"
#include "fbezier.h"

#include "constant.h"
#include "pathops.h"
#include "namedef_.h"
#include "gu_path.h"  /* BBOX_NOT_SET */

#include "panalyze.h"
#include "rcbtrap.h"

#if defined( ASSERT_BUILD )
#include "ripdebug.h"
#endif

/* Comments here on things that have not been done:
 * a) bezier well formed.
 *      (needed for blend detection).
 * b) line/bezier to bezier well joined.
 *      (needed for blend detection).
 * c) for intersection, allow paths to touch.
 *      (needed for improved blend detection).
 * d) (save flatness with each outline) use for path operations.
 *      (partially done).
 */

typedef struct segment {
  SYSTEMVALUE x1 , y1 ;
  SYSTEMVALUE x2 , y2 ;
  int32       type , index ;
  LINELIST    *line , *curv ;
  FPOINT      pnts[ 4 ] ;
} SEGMENT ;

#if defined( ASSERT_BUILD )
static int32 trace_pa = FALSE ;
#endif

#define PA_EPSILON_ACC_R 0.01 /* Rotation accuracy, one hundreth. */

#define SLOP_FACTOR 1.25 /* Allow a 25% margin of error on the epsilons. */

#define CALC_EPSILON( _error , _m , _ex , _ey ) MACRO_START           \
{ \
  SYSTEMVALUE _tmpx1_ , _tmpy1_ ;                                     \
  SYSTEMVALUE _tmpx2_ , _tmpy2_ ;                                     \
                                                                      \
  MATRIX_TRANSFORM_DXY( (_error) , 0.0 , _tmpx1_ , _tmpy1_ , (_m) ) ; \
  MATRIX_TRANSFORM_DXY( 0.0 , (_error) , _tmpx2_ , _tmpy2_ , (_m) ) ; \
                                                                      \
  /* Add some lovely slop as well. */                                 \
  (_ex) = SLOP_FACTOR * ( fabs( _tmpx1_ ) + fabs( _tmpx2_ )) ;        \
  (_ey) = SLOP_FACTOR * ( fabs( _tmpy1_ ) + fabs( _tmpy2_ )) ;        \
}                                                                     \
MACRO_END

/* This routine calculates the accuracy required for the current
 * matrix (which includes both scaling & resolution).  We use 0.001 as
 * our aperture for error since Adobe distiller outputs numbers
 * accurate to 3 decimal places.  We also require another set of
 * epsilons for the displacement tests. The aperture for these tests
 * is 0.06, determined by observing typical errors from a job
 * containing circles distilled by Adobe at 600 (likely worse case). A 25%
 * slop is included in all the epsilons. Distiller use a circle factor
 * accurate to 3dp (0.552), which has to be accounted for in
 * pathisacircle.
 * In addition, we also have problems with Quark which only outputs
 * numbers accurate to 2 decimal places. This in general gets covered
 * by the existing numbers (displacement test is larger), but for the
 * Quark trap fuzzy matches for recombine we need to use these numbers
 * for the center point comparison. We could use the displacement number
 * since this is slightly too large, but sufficient.
 */
void init_path_analysis( void )
{
  OMATRIX *matrix = & theIgsDevicePageCTM( gstateptr ) ;
  pa_epsilon_t * peps = & gstateptr->pa_eps ;

#if defined( ASSERT_BUILD )
  register_ripvar( NAME_trace_pa , OBOOLEAN , ( void * )( & trace_pa )) ;
#endif

  /* Calculate standard error margin. */
  CALC_EPSILON( 0.001 , matrix , peps->ex , peps->ey ) ;
  peps->e2x = 2.0 * peps->ex ;
  peps->e2y = 2.0 * peps->ey ;

  /* Setup error for pathsaresimilar displacement test. */
  CALC_EPSILON( 0.006 , matrix , peps->epx , peps->epy ) ;
  peps->e2px = 2.0 * peps->epx ;
  peps->e2py = 2.0 * peps->epy ;

  /* Setup error for part h) displacement test. */
  /* Observed errors from Adobe, distilling circles. */
  /*   dpi  epsilon */
  /*    72    0.5   */
  /*   300    0.12  */
  /*   600    0.06  */
  /*  2400    0.004 */
  CALC_EPSILON( PA_EPSILON_LARGE , matrix , peps->edx , peps->edy ) ;
  peps->e2dx = 2.0 * peps->edx ;
  peps->e2dy = 2.0 * peps->edy ;

  /* Setup error for pathsaresimilar exact match */
  CALC_EPSILON( PA_EPSILON , matrix , peps->eex , peps->eey ) ;
  peps->ee2x = 2.0 * peps->eex ;
  peps->ee2y = 2.0 * peps->eey ;

  /* Setup error for part d) bbox check of dl object. */
  peps->eix = 2 + ( int32 )peps->ex ;
  peps->eiy = 2 + ( int32 )peps->ey ;
}

/* -------------------------------------------------------------------------- */
/* Same as getsegmentbbox but does not set bbox (for optimisation reasons). */
static LINELIST *getsegment( LINELIST *theline , LINELIST *startline ,
                             SEGMENT *bbox , LINELIST *endline )
{
  pa_epsilon_t * peps = & gstateptr->pa_eps ;

  HQASSERT( theline , "theline NULL in getsegment" ) ;
  HQASSERT( bbox , "bbox NULL in getsegment" ) ;

  for (;;) {
    int32 type ;

    theX( bbox->pnts[ 0 ] ) = theX( theIPoint( theline )) ;
    theY( bbox->pnts[ 0 ] ) = theY( theIPoint( theline )) ;
    theline = theline->next ;
    if ( theline == NULL )
      theline = startline ;
    if ( theline == endline )
      return NULL ;

    type = theILineType( theline ) ;
    switch ( type ) {
    case CURVETO:
      theX( bbox->pnts[ 1 ] ) = theX( theIPoint( theline )) ;
      theY( bbox->pnts[ 1 ] ) = theY( theIPoint( theline )) ;
      theline = theline->next ;
      theX( bbox->pnts[ 2 ] ) = theX( theIPoint( theline )) ;
      theY( bbox->pnts[ 2 ] ) = theY( theIPoint( theline )) ;
      theline = theline->next ;
      theX( bbox->pnts[ 3 ] ) = theX( theIPoint( theline )) ;
      theY( bbox->pnts[ 3 ] ) = theY( theIPoint( theline )) ;
      if ( fabs(theX(bbox->pnts[3]) - theX(bbox->pnts[0])) >= peps->ex ||
           fabs(theY(bbox->pnts[3]) - theY(bbox->pnts[0])) >= peps->ey ) {
        bbox->type = type ;
        bbox->index = 3 ;
        return ( theline ) ;
      }
      break ;
    default:
      theX( bbox->pnts[ 1 ] ) = theX( theIPoint( theline )) ;
      theY( bbox->pnts[ 1 ] ) = theY( theIPoint( theline )) ;
      if ( fabs(theX(bbox->pnts[1]) - theX(bbox->pnts[0])) >= peps->ex ||
           fabs(theY(bbox->pnts[1]) - theY(bbox->pnts[0] )) >= peps->ey ) {
        bbox->type = type ;
        bbox->index = 1 ;
        return ( theline ) ;
      }
      break ;
    }
  }
/* NOT REACHED */
}

/* -------------------------------------------------------------------------- */
/* This routine returns the coordinates of a path that has already been
 * analyzed, or defined as being a rectangle. It returns the number of
 * coordinates filled in, which has to be 4 for an analyzed rectangle, and
 * could be any of 0, 2 or 4 for a defined rectangle. A defined rectangle is
 * one that was produced from rectfill (and so could be degenerate in either
 * one or two axis) as opposed to an analyzed one which is one that was
 * produced (and analyzed to be one) from fill.
 * Routine is similar to getpathcirclepoints.
 */
int32 getpathrectanglepoints( PATHINFO *path , FPOINT *points )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  PATHLIST *thepath ;

  SEGMENT seg ;

  HQASSERT( path , "path NULL in getpathrectanglepoints" ) ;
  HQASSERT( points, "points NULL in getpathrectanglepoints" ) ;

  thepath = path->firstpath ;
  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
    if ( theline == NULL ) {
      HQASSERT( index == 0 || index == 2 ,
              "should only get 0, 2 sections for a degenerate rectangle" ) ;
      return index ; /* aka 0 or 2 */
    }

    HQASSERT( seg.type != CURVETO , "and they should not be beziers" ) ;

    theX( points[ 0 ] ) = theX( seg.pnts[ 0 ] ) ;
    theY( points[ 0 ] ) = theY( seg.pnts[ 0 ] ) ;
    points += 1 ;
  }
#if defined( ASSERT_BUILD )
  { /* Got 4 segments; should not be a fifth. */
    LINELIST *debugline ;
    SEGMENT   debugseg ;
    debugline = getsegment( theline , theISubPath( thepath ) , & debugseg , endline ) ;
    HQASSERT( debugline == NULL , "should only be 4 segments in a rectangle" ) ;
  }
#endif

  return 4 ;
}

/* -------------------------------------------------------------------------- */
/* This routine returns the coordinates of a path that has already been
 * analyzed as being a circle. It returns the number of coordinates filled in,
 * which has to be 12. No reason as to why it couldn't return 0 if for example
 * a call to getsegment returns NULL, but for now this should never happen.
 * Routine is similar to getpathrectanglepoints.
 */
int32 getpathcirclepoints( PATHINFO *path , FPOINT *points )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  PATHLIST *thepath ;

  SEGMENT seg ;

  HQASSERT( path , "path NULL in getpathcirclepoints" ) ;
  HQASSERT( points, "points NULL in getpathcirclepoints" ) ;

  thepath = path->firstpath ;
  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
    HQASSERT( theline != NULL , "should be 4 segments in a circle" ) ;
    HQASSERT( seg.type == CURVETO , "and they should be beziers" ) ;

    theX( points[ 0 ] ) = theX( seg.pnts[ 0 ] ) ;
    theY( points[ 0 ] ) = theY( seg.pnts[ 0 ] ) ;
    theX( points[ 1 ] ) = theX( seg.pnts[ 1 ] ) ;
    theY( points[ 1 ] ) = theY( seg.pnts[ 1 ] ) ;
    theX( points[ 2 ] ) = theX( seg.pnts[ 2 ] ) ;
    theY( points[ 2 ] ) = theY( seg.pnts[ 2 ] ) ;
    points += 3 ;
  }
#if defined( ASSERT_BUILD )
  { /* Got 4 segments; should not be a fifth. */
    LINELIST *debugline ;
    SEGMENT   debugseg ;
    debugline = getsegment( theline , theISubPath( thepath ) , & debugseg , endline ) ;
    HQASSERT( debugline == NULL , "should only be 4 segments in a circle" ) ;
  }
#endif

  return 12 ;
}

/* -------------------------------------------------------------------------- */
#define CHECK_RECTANGLE( _seg , _op , _dx0 , _dy0 ) MACRO_START \
  SYSTEMVALUE _dx1_ , _dy1_ ; \
  _dx1_ = theX( segs[ _seg ].pnts[ 1 ] ) - theX( segs[ _seg ].pnts[ 0 ] ) ; \
  _dy1_ = theY( segs[ _seg ].pnts[ 1 ] ) - theY( segs[ _seg ].pnts[ 0 ] ) ; \
  if ( fabs( _dx0 _op _dx1_ ) >= gstateptr->pa_eps.e2x || \
       fabs( _dy0 _op _dy1_ ) >= gstateptr->pa_eps.e2y ) \
    return FALSE ; \
MACRO_END

/* -------------------------------------------------------------------------- */
static Bool segsarerectangle( SEGMENT segs[ 4 ] , int32 *orientation ,
                              int32 *type , RCB2DVEC *rcbvec , int32 *rcbtype )
{
  SYSTEMVALUE dx0 , dy0 , dx1 , dy1 , ex , ey ;

  HQASSERT( segs , "segs NULL in segsarerectangle" ) ;
  HQASSERT( orientation , "orientation NULL in segsarerectangle" ) ;
  HQASSERT( type , "type NULL in segsarerectangle" ) ;

  ex = gstateptr->pa_eps.ex ;
  ey = gstateptr->pa_eps.ey ;

  /* Given vectors:
   *        0
   *     ------->
   *    /|\     |
   *     |      |  1
   *  3  |      |
   *     |     \|/
   *     <-------
   *         2
   *
   * Check that:
   *  0 == -2
   *  1 == -3
   */

  /* 0 */
  dx0 = theX( segs[ 0 ].pnts[ 1 ] ) - theX( segs[ 0 ].pnts[ 0 ] ) ;
  dy0 = theY( segs[ 0 ].pnts[ 1 ] ) - theY( segs[ 0 ].pnts[ 0 ] ) ;
  CHECK_RECTANGLE( 2 , + , dx0 , dy0 ) ; /* 0 == -2 */

  /* 1 */
  dx1 = theX( segs[ 1 ].pnts[ 1 ] ) - theX( segs[ 1 ].pnts[ 0 ] ) ;
  dy1 = theY( segs[ 1 ].pnts[ 1 ] ) - theY( segs[ 1 ].pnts[ 0 ] ) ;
  CHECK_RECTANGLE( 3 , + , dx1 , dy1 ) ; /* 1 == -3 */

  if (( fabs( dx0 ) < ex && fabs( dy1 ) < ey ) ||
      ( fabs( dx1 ) < ex && fabs( dy0 ) < ey ))
    (*type) = VDT_RectangleDevice ;
  else
    (*type) = VDT_RectangleUser ;

  if ( dx0 * dy1 < dx1 * dy0 )
    (*orientation) = VDO_AntiClockWise ;
  else
    (*orientation) = VDO_ClockWise ;

  if ( rcbvec != NULL ) {
    OMATRIX *matrix;
    if ( rcbtype != NULL )
      (*rcbtype) = RCBTRAP_RECT ;
    matrix = & theIgsDeviceInversePageCTM(gstateptr);
    /* Convert to default user space */
    MATRIX_TRANSFORM_DXY(dx0, dy0, dx0, dy0, matrix);
    MATRIX_TRANSFORM_DXY(dx1, dy1, dx1, dy1, matrix);
    rcbvec->dx0 = ( float )dx0 ;
    rcbvec->dy0 = ( float )dy0 ;
    rcbvec->dx1 = ( float )dx1 ;
    rcbvec->dy1 = ( float )dy1 ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool pathisarectangle( PATHINFO *path , Bool *degenerate ,
                       int32 *orientation , int32 *type , RCBTRAP *rcbtrap )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  SEGMENT segs[ 4 ] ;
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in pathisarectangle" ) ;
  HQASSERT( degenerate , "degenerate NULL in pathisarectangle" ) ;
  HQASSERT( orientation , "orientation NULL in pathisarectangle" ) ;
  HQASSERT( type , "type NULL in pathisarectangle" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in pathisarectangle" ) ;

  (*type) = VDT_Unknown ;
  (*degenerate) = FALSE ;
  (*orientation) = VDO_Unknown ;

  if ( rcbtrap != NULL )
    rcbtrap->type = RCBTRAP_UNKNOWN ;

  /* First of all check that we've got 4 segments that are lines (ignoring degenerates). */
  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & segs[ index ] , endline ) ;
    if ( theline == NULL ) {
      if ( index == 0 || index == 2 ) /* Point or line. */
        (*degenerate) = TRUE ;
      return FALSE ;
    }
    if ( segs[ index ].type == CURVETO )
      return FALSE ;
  }
  /* Got 4 segments; should not be a fifth (or it should be a closepath). */
  theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
  if ( theline != NULL )
    return FALSE ;

  return segsarerectangle( segs , orientation , type ,
                           rcbtrap != NULL ? & rcbtrap->u.s1.v1 : NULL ,
                           rcbtrap != NULL ? & rcbtrap->type : NULL ) ;
}

/* -------------------------------------------------------------------------- */
#define CHECK_CIRCLE1( _seg , _op , _dx0 , _dy0 ) MACRO_START \
  SYSTEMVALUE _dx1_ , _dy1_ ; \
  _dx1_ = theX( segs[ _seg >> 1 ].pnts[ 1 + (( _seg & 1 ) << 1 ) ] ) - \
          theX( segs[ _seg >> 1 ].pnts[ 0 + (( _seg & 1 ) << 1 ) ] ) ; \
  _dy1_ = theY( segs[ _seg >> 1 ].pnts[ 1 + (( _seg & 1 ) << 1 ) ] ) - \
          theY( segs[ _seg >> 1 ].pnts[ 0 + (( _seg & 1 ) << 1 ) ] ) ; \
  if ( fabs( _dx0 _op _dx1_ ) >= gstateptr->pa_eps.e2x || \
       fabs( _dy0 _op _dy1_ ) >= gstateptr->pa_eps.e2y ) \
    return FALSE ; \
MACRO_END

#define CHECK_CIRCLE2( _seg1 , _seg2 ) MACRO_START \
  SYSTEMVALUE _dx1_ , _dy1_ ; \
  SYSTEMVALUE _dx2_ , _dy2_ ; \
  _dx1_ = theX( segs[ _seg1 >> 1 ].pnts[ 2 ] ) - theX( segs[ _seg1 >> 1 ].pnts[ 1 ] ) ; \
  _dy1_ = theY( segs[ _seg1 >> 1 ].pnts[ 2 ] ) - theY( segs[ _seg1 >> 1 ].pnts[ 1 ] ) ; \
  _dx2_ = theX( segs[ _seg2 >> 1 ].pnts[ 2 ] ) - theX( segs[ _seg2 >> 1 ].pnts[ 1 ] ) ; \
  _dy2_ = theY( segs[ _seg2 >> 1 ].pnts[ 2 ] ) - theY( segs[ _seg2 >> 1 ].pnts[ 1 ] ) ; \
  if ( fabs( _dx1_ + _dx2_ ) >= gstateptr->pa_eps.e2x || \
       fabs( _dy1_ + _dy2_ ) >= gstateptr->pa_eps.e2y ) \
    return FALSE ; \
MACRO_END

#define ADOBE_CIRCLE_FACTOR 0.552

#define CHECK_CIRCLE3( _seg ) MACRO_START \
  SYSTEMVALUE _dx1_ , _dy1_ ; \
  SYSTEMVALUE _dx2_ , _dy2_ ; \
  _dx1_ = theX( segs[ _seg >> 1 ].pnts[ 0 ] ) - theX( segs[ _seg >> 1 ].pnts[ 3 ] ) ; \
  _dy1_ = theY( segs[ _seg >> 1 ].pnts[ 0 ] ) - theY( segs[ _seg >> 1 ].pnts[ 3 ] ) ; \
  _dx2_ = theX( segs[ _seg >> 1 ].pnts[ 2 ] ) - theX( segs[ _seg >> 1 ].pnts[ 1 ] ) ; \
  _dy2_ = theY( segs[ _seg >> 1 ].pnts[ 2 ] ) - theY( segs[ _seg >> 1 ].pnts[ 1 ] ) ; \
  if (( fabs( ( CIRCLE_FACTOR - 1.0 ) * _dx1_ - _dx2_ ) >= gstateptr->pa_eps.e2x || \
        fabs( ( CIRCLE_FACTOR - 1.0 ) * _dy1_ - _dy2_ ) >= gstateptr->pa_eps.e2y ) && \
        ( fabs( ( ADOBE_CIRCLE_FACTOR - 1.0 ) * _dx1_ - _dx2_ ) >= gstateptr->pa_eps.e2x || \
          fabs( ( ADOBE_CIRCLE_FACTOR - 1.0 ) * _dy1_ - _dy2_ ) >= gstateptr->pa_eps.e2y )) \
    return FALSE ; \
MACRO_END


/* -------------------------------------------------------------------------- */
static Bool segsarecircle( SEGMENT segs[ 4 ] , int32 *orientation ,
                           int32 *type , RCB2DVEC *rcbvec , int32 *rcbtype )
{
  SYSTEMVALUE dx0 , dy0 , dx1 , dy1 ;

  HQASSERT( segs , "segs NULL in segsarecircle" ) ;
  HQASSERT( orientation , "orientation NULL in segsarecircle" ) ;
  HQASSERT( type , "type NULL in segsarecircle" ) ;

  /* Given vectors:
   *          7     0
   *        ----->----->
   *
   *    /|\               |
   *     |                | 1
   *   6 |                |
   *     |               \|/
   *    /|\               |
   *     |                | 2
   *   5 |                |
   *     |               \|/
   *
   *        <-----<-----
   *           4     3
   *
   * Check that:
   *  0 == 7, 0 == -3, 0 == -4,
   *  1 == 2, 1 == -5, 1 == -6,
   *  (0,1) == -(4,5),
   *  (2,3) == -(6,7),
   *  CIRCLE_FACTOR(0) == CIRCLE_FACTOR(-1),
   *  CIRCLE_FACTOR(2) == CIRCLE_FACTOR(-3).
   *  where CIRCLE_FACTOR(xy):
   *    xy0 + (xy1 - xy0) / CIRCLE_FACTOR == xy3 + (xy2 - xy3) / CIRCLE_FACTOR
   *    CIRCLE_FACTOR * xy0 + xy1 - xy0 == CIRCLE_FACTOR * xy3 + xy2 - xy3
   *    CIRCLE_FACTOR * (xy0 - xy3) + xy3 - xy2 + xy1 - xy0 == 0
   *    CIRCLE_FACTOR * (xy0 - xy3) - (xy0 - xy3) - ( xy2 - xy1 ) == 0
   *    (CIRCLE_FACTOR - 1) * ( xy0 - xy3 ) - ( xy2 - xy1 ) == 0
   */

  /* 0 */
  dx0 = theX( segs[ 0 ].pnts[ 1 ] ) - theX( segs[ 0 ].pnts[ 0 ] ) ;
  dy0 = theY( segs[ 0 ].pnts[ 1 ] ) - theY( segs[ 0 ].pnts[ 0 ] ) ;
  CHECK_CIRCLE1( 7 , - , dx0 , dy0 ) ; /* 0 ==  7 */
  CHECK_CIRCLE1( 3 , + , dx0 , dy0 ) ; /* 0 == -3 */
  CHECK_CIRCLE1( 4 , + , dx0 , dy0 ) ; /* 0 == -4 */

  /* 1 */
  dx1 = theX( segs[ 0 ].pnts[ 3 ] ) - theX( segs[ 0 ].pnts[ 2 ] ) ;
  dy1 = theY( segs[ 0 ].pnts[ 3 ] ) - theY( segs[ 0 ].pnts[ 2 ] ) ;
  CHECK_CIRCLE1( 2 , - , dx1 , dy1 ) ; /* 1 ==  2 */
  CHECK_CIRCLE1( 5 , + , dx1 , dy1 ) ; /* 1 == -5 */
  CHECK_CIRCLE1( 6 , + , dx1 , dy1 ) ; /* 1 == -6 */

  /* (0,1) == -(4,5) */
  CHECK_CIRCLE2( 0 , 4 ) ;

  /* (2,3) == -(6,7) */
  CHECK_CIRCLE2( 2 , 6 ) ;

  /* CIRCLE_FACTOR(0) == CIRCLE_FACTOR(-1) */
  CHECK_CIRCLE3( 0 ) ;

  /* CIRCLE_FACTOR(0) == CIRCLE_FACTOR(-1) */
  CHECK_CIRCLE3( 2 ) ;

  (*type) = VDT_Circle ;

  if ( dx0 * dy1 < dx1 * dy0 )
    (*orientation) = VDO_AntiClockWise ;
  else
    (*orientation) = VDO_ClockWise ;

  if ( rcbvec != NULL ) {
    OMATRIX *matrix;
    if ( rcbtype != NULL )
      (*rcbtype) = RCBTRAP_OVAL ;
    matrix = & theIgsDeviceInversePageCTM(gstateptr);
    /* Convert to default user space */
    MATRIX_TRANSFORM_DXY(dx0, dy0, dx0, dy0, matrix);
    MATRIX_TRANSFORM_DXY(dx1, dy1, dx1, dy1, matrix);
    rcbvec->dx0 = ( float )dx0 ;
    rcbvec->dy0 = ( float )dy0 ;
    rcbvec->dx1 = ( float )dx1 ;
    rcbvec->dy1 = ( float )dy1 ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool segsarerevcircle( SEGMENT segs[ 4 ] , int32 *orientation ,
                              int32 *type , RCB2DVEC *rcbvec , int32 *rcbtype )
{
  SYSTEMVALUE dx0 , dy0 , dx1 , dy1 ;

  HQASSERT( segs , "segs NULL in segsarerevcircle" ) ;
  HQASSERT( orientation , "orientation NULL in segsarerevcircle" ) ;
  HQASSERT( type , "type NULL in segsarerevcircle" ) ;

  /* Given vectors:
   *
   *           /|\|
   *            | | 0
   *          7 | |
   *            |\|/
   *      6            1
   *    ----->       ----->
   *    <-----       <-----
   *       5            2
   *           /|\|
   *            | | 3
   *          4 | |
   *            |\|/
   *
   *
   * Check that:
   *  0 == 3, 0 == -4, 0 == -7,
   *  1 == 6, 1 == -2, 1 == -5,
   *  (0,1) == -(4,5),
   *  (2,3) == -(6,7),
   *  CIRCLE_FACTOR(0) == CIRCLE_FACTOR(-1),
   *  CIRCLE_FACTOR(2) == CIRCLE_FACTOR(-3).
   *  where CIRCLE_FACTOR(xy):
   *    xy0 + (xy1 - xy0) / CIRCLE_FACTOR == xy3 + (xy2 - xy3) / CIRCLE_FACTOR
   *    CIRCLE_FACTOR * xy0 + xy1 - xy0 == CIRCLE_FACTOR * xy3 + xy2 - xy3
   *    CIRCLE_FACTOR * (xy0 - xy3) + xy3 - xy2 + xy1 - xy0 == 0
   *    CIRCLE_FACTOR * (xy0 - xy3) - (xy0 - xy3) - ( xy2 - xy1 ) == 0
   *    (CIRCLE_FACTOR - 1) * ( xy0 - xy3 ) - ( xy2 - xy1 ) == 0
   */

  /* 0 */
  dx0 = theX( segs[ 0 ].pnts[ 1 ] ) - theX( segs[ 0 ].pnts[ 0 ] ) ;
  dy0 = theY( segs[ 0 ].pnts[ 1 ] ) - theY( segs[ 0 ].pnts[ 0 ] ) ;
  CHECK_CIRCLE1( 3 , - , dx0 , dy0 ) ; /* 0 ==  3 */
  CHECK_CIRCLE1( 4 , + , dx0 , dy0 ) ; /* 0 == -4 */
  CHECK_CIRCLE1( 7 , + , dx0 , dy0 ) ; /* 0 == -7 */

  /* 1 */
  dx1 = theX( segs[ 0 ].pnts[ 3 ] ) - theX( segs[ 0 ].pnts[ 2 ] ) ;
  dy1 = theY( segs[ 0 ].pnts[ 3 ] ) - theY( segs[ 0 ].pnts[ 2 ] ) ;
  CHECK_CIRCLE1( 6 , - , dx1 , dy1 ) ; /* 1 ==  6 */
  CHECK_CIRCLE1( 2 , + , dx1 , dy1 ) ; /* 1 == -2 */
  CHECK_CIRCLE1( 5 , + , dx1 , dy1 ) ; /* 1 == -5 */

  /* (0,1) == -(4,5) */
  CHECK_CIRCLE2( 0 , 4 ) ;

  /* (2,3) == -(6,7) */
  CHECK_CIRCLE2( 2 , 6 ) ;

  /* CIRCLE_FACTOR(0) == CIRCLE_FACTOR(-1) */
  CHECK_CIRCLE3( 0 ) ;

  /* CIRCLE_FACTOR(0) == CIRCLE_FACTOR(-1) */
  CHECK_CIRCLE3( 2 ) ;

  (*type) = VDT_ReverseCircle ;

  if ( dx0 * dy1 < dx1 * dy0 )
    (*orientation) = VDO_AntiClockWise ;
  else
    (*orientation) = VDO_ClockWise ;

  if ( rcbvec != NULL ) {
    OMATRIX *matrix;
    if ( rcbtype != NULL )
      (*rcbtype) = RCBTRAP_OVAL ;
    matrix = & theIgsDeviceInversePageCTM(gstateptr);
    /* Convert to default user space */
    MATRIX_TRANSFORM_DXY(dx0, dy0, dx0, dy0, matrix);
    MATRIX_TRANSFORM_DXY(dx1, dy1, dx1, dy1, matrix);
    rcbvec->dx0 = ( float )dx0 ;
    rcbvec->dy0 = ( float )dy0 ;
    rcbvec->dx1 = ( float )dx1 ;
    rcbvec->dy1 = ( float )dy1 ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool pathisacircle( PATHINFO *path , Bool *degenerate , int32 *orientation ,
                     int32 *type , RCBTRAP *rcbtrap )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  SEGMENT segs[ 4 ] ;
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in pathisacircle" ) ;
  HQASSERT( degenerate , "degenerate NULL in pathisacircle" ) ;
  HQASSERT( orientation , "orientation NULL in pathisacircle" ) ;
  HQASSERT( type , "type NULL in pathisacircle" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in pathisacircle" ) ;

  (*type) = VDT_Unknown ;
  (*degenerate) = FALSE ;
  (*orientation) = VDO_Unknown ;

  if ( rcbtrap != NULL )
    rcbtrap->type = RCBTRAP_UNKNOWN ;

  /* First of all check that we've got 4 segments that are beziers (ignoring degenerates). */
  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & segs[ index ] , endline ) ;
    if ( theline == NULL ) {
      if ( index == 0 ) /* Point. */
        (*degenerate) = TRUE ;
      return FALSE ;
    }
    if ( segs[ index ].type != CURVETO )
      return FALSE ;
  }
  /* Got 4 segments; should not be a fifth. */
  theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
  if ( theline != NULL )
    return FALSE ;

  return segsarecircle( segs , orientation , type ,
                        rcbtrap != NULL ? & rcbtrap->u.s1.v1 : NULL ,
                        rcbtrap != NULL ? & rcbtrap->type : NULL ) ;
}

/* -------------------------------------------------------------------------- */
static Bool segsareparallel( SEGMENT *seg1 , SEGMENT *seg2 )
{
  SYSTEMVALUE dx0 , dy0 , dx1 , dy1 , edx , edy ;
  SYSTEMVALUE scn , scd , sc ;

  edx = gstateptr->pa_eps.edx ;
  edy = gstateptr->pa_eps.edy ;

  HQASSERT( seg1 != NULL , "seg1 NULL in segsareparallel" ) ;
  HQASSERT( seg2 != NULL , "seg2 NULL in segsareparallel" ) ;

  dx0 = theX( seg1->pnts[ 1 ] ) - theX( seg1->pnts[ 0 ] ) ;
  dy0 = theY( seg1->pnts[ 1 ] ) - theY( seg1->pnts[ 0 ] ) ;
  dx1 = theX( seg2->pnts[ 1 ] ) - theX( seg2->pnts[ 0 ] ) ;
  dy1 = theY( seg2->pnts[ 1 ] ) - theY( seg2->pnts[ 0 ] ) ;

  scn = fabs( dx0 ) + fabs( dy0 ) ;
  scd = fabs( dx1 ) + fabs( dy1 ) ;

  if ( scd > PA_EPSILON ) {
    sc = scn / scd ;
    dx1 *= sc ;
    dy1 *= sc ;
    if ( fabs( dx1 - dx0 ) < edx && fabs( dy1 - dy0 ) < edy )
      return TRUE ;
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
Bool pathiscorneredrect( PATHINFO *path , Bool *degenerate ,
                         int32 *orientation , int32 *type , RCBTRAP *rcbtrap )
{
  int32 ttype , ptype ;
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  SEGMENT segs1[ 4 ] ;
  SEGMENT segs2[ 4 ] ;
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in pathisacircle" ) ;
  HQASSERT( degenerate , "degenerate NULL in pathisacircle" ) ;
  HQASSERT( orientation , "orientation NULL in pathisacircle" ) ;
  HQASSERT( type , "type NULL in pathisacircle" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in pathisacircle" ) ;

  (*type) = VDT_Unknown ;
  (*degenerate) = FALSE ;
  (*orientation) = VDO_Unknown ;

  if ( rcbtrap != NULL )
    rcbtrap->type = RCBTRAP_UNKNOWN ;

  ptype = VDT_Unknown ;

  /* We need to test for getting 8 segments, of either:
   * a) alternating lines & curves (or curves & lines).
   * b) lines.
   */
  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & segs1[ index ] , endline ) ;
    if ( theline == NULL ) {
      if ( index == 0 ) /* Point. */
        (*degenerate) = TRUE ;
      return FALSE ;
    }
    theline = getsegment( theline , theISubPath( thepath ) , & segs2[ index ] , endline ) ;
    if ( theline == NULL )
      return FALSE ;
    if ( segs1[ index ].type == CURVETO ) {
      if ( segs2[ index ].type == CURVETO )
        return FALSE ;
      else
        ttype = VDT_RoundRectangleCurveLine ;
    }
    else {
      if ( segs2[ index ].type == CURVETO )
        ttype = VDT_RoundRectangleLineCurve ;
      else
        ttype = VDT_Octagon ;
    }
    if ( index == 0 )
      ptype = ttype ;
    if ( ptype != ttype )
      return FALSE ;
  }
  /* Got 4(x2) segments; should not be another (or it should be a closepath). */
  theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
  if ( theline != NULL ) {
    if ( seg.type == CLOSEPATH || seg.type == MYCLOSE ) {
      /* See if last line is parallel to first line and so can extend first line. */
      if ( segs1[ 0 ].type != CURVETO ) {
        if ( segsareparallel( & seg , segs1 )) {
          theX( segs1[ 0 ].pnts[ 0 ] ) = theX( seg.pnts[ 0 ] ) ;
          theY( segs1[ 0 ].pnts[ 0 ] ) = theY( seg.pnts[ 0 ] ) ;
        }
        else
          return FALSE ;
      }
      else
        return FALSE ;
    }
    else
      return FALSE ;
  }

  switch ( ptype ) {
    int32 sub_type1 ;
    int32 sub_type2 ;
  case VDT_Octagon:
    if ( ! segsarerectangle(segs1 , orientation , & sub_type1 ,
                            rcbtrap != NULL ? &rcbtrap->u.s2.v1 : NULL, NULL))
      return FALSE ;
    if ( ! segsarerectangle(segs2 , orientation , & sub_type2 ,
                            rcbtrap != NULL ? &rcbtrap->u.s2.v2 : NULL, NULL ))
      return FALSE ;
    if ( rcbtrap != NULL )
      rcbtrap->type = RCBTRAP_OCT ;
    break ;
  case VDT_RoundRectangleLineCurve:
    if ( ! segsarerectangle(segs1, orientation , & sub_type1 ,
                            rcbtrap != NULL ? &rcbtrap->u.s2.v1 : NULL, NULL ))
      return FALSE ;
    if ( ! segsarecircle(segs2 , orientation , & sub_type2 ,
                         rcbtrap != NULL ? &rcbtrap->u.s2.v2 : NULL, NULL ) &&
         ! segsarerevcircle(segs2, orientation , & sub_type2 ,
                            rcbtrap != NULL ? &rcbtrap->u.s2.v2 : NULL, NULL ))
      return FALSE ;
    if ( sub_type2 == VDT_Circle ) {
      ptype = VDT_RoundRectangle ;
      if ( rcbtrap != NULL )
        rcbtrap->type = RCBTRAP_RND ;
    }
    else {
      ptype = VDT_RevRoundRectangle ;
      if ( rcbtrap != NULL )
        rcbtrap->type = RCBTRAP_REV ;
    }
    break ;
  case VDT_RoundRectangleCurveLine:
    if ( ! segsarecircle(segs1 , orientation , & sub_type1 ,
                         rcbtrap != NULL ? &rcbtrap->u.s2.v1 : NULL, NULL ) &&
         ! segsarerevcircle(segs1, orientation , & sub_type1 ,
                            rcbtrap != NULL ? &rcbtrap->u.s2.v1 : NULL, NULL ))
      return FALSE ;
    if ( ! segsarerectangle(segs2, orientation , & sub_type2 ,
                            rcbtrap != NULL ? &rcbtrap->u.s2.v2 : NULL, NULL ))
      return FALSE ;
    if ( sub_type1 == VDT_Circle ) {
      ptype = VDT_RoundRectangle ;
      if ( rcbtrap != NULL )
        rcbtrap->type = RCBTRAP_RND ;
    }
    else {
      ptype = VDT_RevRoundRectangle ;
      if ( rcbtrap != NULL )
        rcbtrap->type = RCBTRAP_REV ;
    }
    break ;
  default:
    HQFAIL( "Should not get here with unknown type" ) ;
  }

  (*type) = ptype ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* This routine analyzes a path to see if it represents a line.
 */
Bool pathisaline(PATHINFO *path , Bool *degenerate , Bool *closed)
{
  LINELIST *theline ;
  SYSTEMVALUE x1 , y1 , ex , ey ;
  SYSTEMVALUE x2 , y2 ;
  PATHLIST *thepath ;

  ex = gstateptr->pa_eps.ex ;
  ey = gstateptr->pa_eps.ey ;

  HQASSERT( path , "path NULL in pathisaline" ) ;
  HQASSERT( degenerate , "degenerate NULL in pathisaline" ) ;
  HQASSERT( closed , "orientation NULL in pathisaline" ) ;

  /* closed is only set if degenerate or is a line. */
  (*degenerate) = FALSE ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in pathisaline" ) ;

  /* Extract first point */
  theline = theISubPath( thepath ) ;
  x1 = theX( theIPoint( theline )) ;
  y1 = theY( theIPoint( theline )) ;

  /* Extract second point */
  theline = theline->next ;
  x2 = theX( theIPoint( theline )) ;
  y2 = theY( theIPoint( theline )) ;

  /* Check for a (moveto) or a (moveto, closepath). */
  if ( theILineType( theline ) == CLOSEPATH ||
       theILineType( theline ) == MYCLOSE ) {
    (*closed) = theILineType( theline ) == CLOSEPATH ? TRUE : FALSE ;
    (*degenerate) = TRUE ;
    return FALSE ;
  }

  /* If some segments, then must not be curvetos. */
  if ( theILineType( theline ) == CURVETO )
    return FALSE ;

  /* Check that next point is either a closepath, or a lineto back to the start
   * point. Note for now we don't support a lineto back to the start point since
   * the stroke code can't produce a single outline for that case (which vignette
   * detection requires).
   */
  theline = theline->next ;
#if PA_SUPPORT_MOVETO_LINETO_LINETO_CLOSEPATH
  if ( theILineType( theline ) != CLOSEPATH &&
       theILineType( theline ) != MYCLOSE ) {
    if ( theILineType( theline ) != LINETO )
      return FALSE ;
    if ( x1 != theX( theIPoint( theline )) ||
         y1 != theY( theIPoint( theline )))
      return FALSE ;
    theline = theline->next ;
  }
#endif

  /* Check that we end on a closepath. */
  if ( theILineType( theline ) != CLOSEPATH &&
       theILineType( theline ) != MYCLOSE )
    return FALSE ;

  /* Finally check that the line length is non-trivial. */
  if ( fabs( x1 - x2 ) < ex &&
       fabs( y1 - y2 ) < ey )
    return FALSE ;

  (*closed) = (theILineType( theline ) == CLOSEPATH) ;
  (*degenerate) = FALSE ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
void get_path_matrix( PATHINFO *path , OMATRIX *matrix , SYSTEMVALUE length[ 2 ] )
{
  LINELIST *theline ;
  SYSTEMVALUE dx1 , dy1 , dx2 , dy2 ;
  SEGMENT seg ;
  OMATRIX mtmp ;
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in get_path_matrix" ) ;
  HQASSERT( matrix , "matrix NULL in get_path_matrix" ) ;
  HQASSERT( length , "length NULL in get_path_matrix" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in get_path_matrix" ) ;

  length[ 0 ] = 1.0 ;
  length[ 1 ] = 1.0 ;
  MATRIX_COPY( matrix , & identity_matrix ) ;

  theline = theISubPath( thepath ) ;
  theline = getsegment( theline , theISubPath( thepath ) , & seg , theISubPath( thepath )) ;
  if ( theline == NULL )
    return ;

  dx1 = theX( seg.pnts[ 0 ] ) - theX( seg.pnts[ 1 ] ) ;
  dy1 = theY( seg.pnts[ 0 ] ) - theY( seg.pnts[ 1 ] ) ;
  do {
    theline = getsegment( theline , theISubPath( thepath ) , & seg , theISubPath( thepath )) ;
    if ( theline == NULL )
      return ;
    dx2 = theX( seg.pnts[ 1 ] ) - theX( seg.pnts[ 0 ] ) ;
    dy2 = theY( seg.pnts[ 1 ] ) - theY( seg.pnts[ 0 ] ) ;
  } while ( fabs( dx2 * dy1 - dx1 * dy2 ) < PA_EPSILON ) ;

  length[ 0 ] = sqrt( dx1 * dx1 + dy1 * dy1 ) ;
  length[ 1 ] = sqrt( dx2 * dx2 + dy2 * dy2 ) ;

  mtmp.matrix[ 0 ][ 0 ] = dx1 ;
  mtmp.matrix[ 0 ][ 1 ] = dy1 ;
  mtmp.matrix[ 1 ][ 0 ] = dx2 ;
  mtmp.matrix[ 1 ][ 1 ] = dy2 ;
  mtmp.matrix[ 2 ][ 0 ] = theX( seg.pnts[ 0 ] ) ;
  mtmp.matrix[ 2 ][ 1 ] = theY( seg.pnts[ 0 ] ) ;
  MATRIX_SET_OPT_BOTH( & mtmp ) ;
  if ( ! matrix_inverse( & mtmp , matrix ))
    MATRIX_COPY( matrix , & identity_matrix ) ;
}

/* -------------------------------------------------------------------------- */
/* Checks if the two given subpaths are identical. Currently this includes
 * either a translation, or a scaling (or both). The scaling allows for
 * different scalings in userspace x & y. For now we don't care
 * about a rotation (of one path vs the other).
 */
Bool pathsaresimilar( PATHINFO *path1 , PATHINFO *path2 , int32 fcircle ,
                       int32 tests , int32 hint , int32 *match )
{
  int32 i ;
  LINELIST *l1 , *l2 ;
  LINELIST *s1 , *s2 ;
  SYSTEMVALUE dx1 , dy1 , dx2 , dy2 ;
  SEGMENT seg1 , seg2 ;
  OMATRIX m1 , m2 ;
  SYSTEMVALUE len1[ 2 ] , len2[ 2 ] ;
  PATHLIST *p1 , *p2 ;

  HQASSERT( path1 , "path1 is NULL in pathsaresimilar" ) ;
  HQASSERT( path2 , "path2 is NULL in pathsaresimilar" ) ;
  HQASSERT( match , "match is NULL in pathsaresimilar" ) ;
  HQASSERT( hint == VDM_Exact || hint == VDM_Unknown ,
            "hint must be for an exact match or nothing" ) ;

  p1 = path1->firstpath ;
  HQASSERT( p1 , "p1 is NULL in pathsaresimilar" ) ;
  p2 = path2->firstpath ;
  HQASSERT( p2 , "p2 is NULL in pathsaresimilar" ) ;

  (*match) = VDM_Unknown ;

  get_path_matrix( path1 , & m1 , len1 ) ;
  get_path_matrix( path2 , & m2 , len2 ) ;

  /* Scale up the matrices so comparing with epsilons is meaningful */
  for ( i = 0 ; i < 2 ; ++i ) {
    SYSTEMVALUE tmp = (( len1[ i ] < len2[ i ] ) ? len1[ i ] : len2[ i ] ) ;
    m1.matrix[ 0 ][ i ] *= tmp ;
    m1.matrix[ 1 ][ i ] *= tmp ;
    m2.matrix[ 0 ][ i ] *= tmp ;
    m2.matrix[ 1 ][ i ] *= tmp ;
  }

  s1 = l1 = theISubPath( p1 ) ;
  s2 = l2 = theISubPath( p2 ) ;
  HQASSERT( theILineType( l1 ) == MOVETO || theILineType( l1 ) == MYMOVETO ,
            "unknown start point in path" ) ;
  HQASSERT( theILineType( l2 ) == MOVETO || theILineType( l2 ) == MYMOVETO ,
            "unknown start point in path" ) ;

  while ( l1 = getsegment( l1 , s1 , & seg1 , s1) ,
          l2 = getsegment( l2 , s2 , & seg2 , s2) , l1 && l2 ) {
    int32 i , s ;
    if ( seg1.type == CURVETO ) {
      if ( seg2.type != CURVETO )
        return FALSE ;
      s = 3 ;
    }
    else {
      if ( seg2.type == CURVETO )
        return FALSE ;
      s = 1 ;
    }

    for ( i = 0 ; i < s ; ++i ) {
      SYSTEMVALUE tdx , tdy ;

      tdx = theX( seg1.pnts[ i + 1 ] ) - theX( seg1.pnts[ i ] ) ;
      tdy = theY( seg1.pnts[ i + 1 ] ) - theY( seg1.pnts[ i ] ) ;
      MATRIX_TRANSFORM_DXY( tdx , tdy , dx1 , dy1 , & m1 ) ;

      tdx = theX( seg2.pnts[ i + 1 ] ) - theX( seg2.pnts[ i ] ) ;
      tdy = theY( seg2.pnts[ i + 1 ] ) - theY( seg2.pnts[ i ] ) ;
      MATRIX_TRANSFORM_DXY( tdx , tdy , dx2 , dy2 , & m2 ) ;

      if ( fabs( dx1 - dx2 ) >= gstateptr->pa_eps.e2px )
        return FALSE ;
      if ( fabs( dy1 - dy2 ) >= gstateptr->pa_eps.e2py )
        return FALSE ;
    }
  }

  if (( l1 && !l2 ) ||
      ( !l1 && l2 ))
    return FALSE ;

  {
    OMATRIX m1inv , m1invm2 ;
    /* We need to know if this is due to rotation or not. */
    if ( ! matrix_inverse( & m1 , & m1inv ))
      return FALSE ;

    matrix_mult( & m1inv , & m2 , & m1invm2 ) ;

    if ( m1invm2.opt == MATRIX_OPT_0011 ||
         ( fabs( m1invm2.matrix[ 0 ][ 1 ] ) < PA_EPSILON_ACC_R &&
           fabs( m1invm2.matrix[ 1 ][ 0 ] ) < PA_EPSILON_ACC_R )) {

      sbbox_t *bbox0 = & path1->bbox ;
      sbbox_t *bbox1 = & path2->bbox ;
      double lndx , lndy , cpdx , cpdy ;
      int32 fsimilarsize , fsimilarposn ;
      SYSTEMVALUE e2x = gstateptr->pa_eps.e2x;
      SYSTEMVALUE e2y = gstateptr->pa_eps.e2y;

      if ( m1invm2.matrix[ 0 ][ 0 ] < 0.0 ||
           m1invm2.matrix[ 1 ][ 1 ] < 0.0 )
        return FALSE ;

      (*match) = VDM_Unknown ;

      /* Calculate the difference in the length for x and y axes */
      lndx = fabs(( bbox0->x2 - bbox0->x1 ) - ( bbox1->x2 - bbox1->x1 )) ;
      lndy = fabs(( bbox0->y2 - bbox0->y1 ) - ( bbox1->y2 - bbox1->y1 )) ;

      /* Calculate the difference in the centre points for x and y axes */
      cpdx = fabs(( bbox0->x1 + bbox0->x2 ) - ( bbox1->x1 + bbox1->x2 )) * 0.5 ;
      cpdy = fabs(( bbox0->y1 + bbox0->y2 ) - ( bbox1->y1 + bbox1->y2 )) * 0.5 ;

      /* Circle centre points vary enormously, use large epsilon for centre test */
      fsimilarsize = ( lndx < e2x ) && ( lndy < e2y ) ;
      fsimilarposn = (( cpdx < ( fcircle ? gstateptr->pa_eps.e2dx :
                        gstateptr->pa_eps.e2x )) && ( cpdy < ( fcircle ?
                        gstateptr->pa_eps.e2dy : gstateptr->pa_eps.e2y ))) ;

      if ( tests & VDM_Exact ) {

        if ( hint & VDM_Exact ) {
          /* Looking for an exact match use large epsilons */
          if ( fsimilarsize && fsimilarposn )
            (*match) |= VDM_Exact ;
        }
        else {
          /*
           * Not looking for an exact match, use small epsilons to
           * avoid getting one
           */
          if ( lndx < gstateptr->pa_eps.ee2x && lndy < gstateptr->pa_eps.ee2y
               && cpdx < gstateptr->pa_eps.ee2x &&
               cpdy < gstateptr->pa_eps.ee2y )
            (*match) |= VDM_Exact ;
        }
      }

      if ( ( tests & VDM_Translated ) && fsimilarsize )
        (*match) |= VDM_Translated ;

      if ( ( tests & VDM_Scaled ) && fsimilarposn )
        (*match) |= VDM_Scaled ;
    }
    else
      (*match) = VDM_Rotated ;
  }

  return ( *match != VDM_Unknown ) ;
}

/* -------------------------------------------------------------------------- */
/* Checks to see if the two paths are adjacent.
 * Does this by checking if one of the rectangles sides
 * is equal to the vector between the bounding boxes.
 * Only used in the rectangle case.
 */
Bool pathsareadjacent( PATHINFO *path , SYSTEMVALUE dx , SYSTEMVALUE dy )
{
  int32 i ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  SYSTEMVALUE ex , ey ;
  SYSTEMVALUE vx , vy ;

  PATHLIST *thepath ;

  /* HACK, HACK, HACK; see task 20034. */
  ex = 1.0 + gstateptr->pa_eps.e2x ;
  ey = 1.0 + gstateptr->pa_eps.e2y ;

  HQASSERT( path , "path is NULL in pathsareadjacent" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath is NULL in pathsareadjacent" ) ;

  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;

  for ( i = 0 ; i < 2 ; ++i ) {
    theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
    if ( theline == NULL ) /* For degenerate rects. */
      return FALSE ;
    HQASSERT( seg.type != CURVETO , "and they should not be beziers" ) ;
    vx = theX( seg.pnts[ 1 ] ) - theX( seg.pnts[ 0 ] ) ;
    vy = theY( seg.pnts[ 1 ] ) - theY( seg.pnts[ 0 ] ) ;

    /* Check for exact match */
    if (( fabs( vx - dx ) < ex && fabs( vy - dy ) < ey ) ||
        ( fabs( vx + dx ) < ex && fabs( vy + dy ) < ey )) {
      return TRUE ;
    }
    else { /* Check for overlap */
      SYSTEMVALUE ox = 1.0 + ex ;
      SYSTEMVALUE oy = 1.0 + ey ;
      SYSTEMVALUE lenv = sqrt( vx * vx + vy * vy ) ;
      SYSTEMVALUE lend = sqrt( dx * dx + dy * dy ) ;
      if ( lend - lenv < sqrt( ox * ox + oy * oy )) {
        SYSTEMVALUE tx , ty , sxy ;
        HQASSERT( lenv != 0.0 , "should not be dividing by zero" ) ;
        sxy = lend / lenv ;
        tx = sxy * vx ;
        ty = sxy * vy ;
        if (( fabs( tx - dx ) < ex && fabs( ty - dy ) < ey ) ||
            ( fabs( tx + dx ) < ex && fabs( ty + dy ) < ey )) {
          return TRUE ;
        }
      }
    }
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
Bool strokedpathsaresimilar( PATHINFO *path1 , PATHINFO *path2 ,
                              int32 fcircle , int32 tests , int32 hint ,
                              int32 *match )
{
  LINELIST *l1 , *l2 ;
  PATHLIST *p1 , *p2 ;

  HQASSERT( path1 , "path1 NULL in strokedpathsaresimilar" ) ;
  HQASSERT( path1 , "path1 NULL in strokedpathsaresimilar" ) ;
  HQASSERT( match , "match NULL in strokedpathsaresimilar" ) ;

  if ( ! pathsaresimilar( path1 , path2 , fcircle , tests , hint , match ))
    return FALSE ;

  p1 = path1->firstpath ;
  HQASSERT( p1 , "p1 NULL in strokedpathsaresimilar" ) ;
  p2 = path2->firstpath ;
  HQASSERT( p2 , "p2 NULL in strokedpathsaresimilar" ) ;

  l1 = theISubPath( p1 ) ;
  while ( l1->next != NULL )
    l1 = l1->next ;
  HQASSERT( theILineType( l1 ) == CLOSEPATH ||
            theILineType( l1 ) == MYCLOSE , "unknown close type" ) ;

  l2 = theISubPath( p2 ) ;
  while ( l2->next != NULL )
    l2 = l2->next ;
  HQASSERT( theILineType( l2 ) == CLOSEPATH ||
            theILineType( l2 ) == MYCLOSE , "unknown close type" ) ;

  return ( theILineType( l1 ) == theILineType( l2 ) ) ;
}

/* -------------------------------------------------------------------------- */
Bool strokedpathsareadjacent( PATHINFO *path , SYSTEMVALUE dx , SYSTEMVALUE dy , STROKE_PARAMS *sparams )
{
  /* Need to check that:
   * 1a) line segments of both paths are parallel.
   * 1b) line segments are same length.
   * 1c) line segment perpendicular vector equals displacement.
   * or
   * 2a) line segments of both paths are parallel.
   * 2b) line segments are same length.
   * 2c) line segment start point eq previous end point.
   *
   * Due to pre-ceding work, we know that 1/2a) & 1/2b) are TRUE.
   */

  int32 i ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  SYSTEMVALUE ex , ey ;
  SYSTEMVALUE vx , vy ;
  PATHLIST *thepath ;

  /* HACK, HACK, HACK; see task 20034. */
  ex = 1.0 + gstateptr->pa_eps.e2x ;
  ey = 1.0 + gstateptr->pa_eps.e2y ;

  HQASSERT( path , "path NULL in strokedpathsareadjacent" ) ;
  HQASSERT( sparams , "sparams NULL in strokedpathsareadjacent" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in strokedpathsareadjacent" ) ;

  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;

  theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
  if ( theline == NULL ) /* For degenerate lines. */
    return FALSE ;
  HQASSERT( seg.type != CURVETO , "and they should not be beziers" ) ;

  vx = theX( seg.pnts[ 1 ] ) - theX( seg.pnts[ 0 ] ) ;
  vy = theY( seg.pnts[ 1 ] ) - theY( seg.pnts[ 0 ] ) ;

  for ( i = 0 ; i < 2 ; ++i ) {
    if ( i != 0 ) {
      SYSTEMVALUE ln1 ;
      SYSTEMVALUE tx1 , ty1 ;
      SYSTEMVALUE tx2 , ty2 ;
      if ( theLineWidth( sparams->linestyle ) == 0.0 )
        return FALSE ;
      MATRIX_TRANSFORM_DXY( vx, vy, tx1, ty1, & sparams->orig_inv ) ;
      ln1 = tx1 * tx1 + ty1 * ty1 ;
      HQASSERT( ln1 != 0.0 , "length should not be degenerate" ) ;
      ln1 = theLineWidth( sparams->linestyle ) / sqrt( ln1 ) ;
      tx2 = ln1 * (-ty1) ;
      ty2 = ln1 * ( tx1) ;
      MATRIX_TRANSFORM_DXY( tx2, ty2, vx, vy, & sparams->orig_ctm ) ;
    }

    /* Check for exact match */
    if (( fabs( vx - dx ) < ex && fabs( vy - dy ) < ey ) ||
        ( fabs( vx + dx ) < ex && fabs( vy + dy ) < ey )) {
      return TRUE ;
    }
    else { /* Check for overlap */
      SYSTEMVALUE ox = 1.0 + ex ;
      SYSTEMVALUE oy = 1.0 + ey ;
      SYSTEMVALUE lenv = sqrt( vx * vx + vy * vy ) ;
      SYSTEMVALUE lend = sqrt( dx * dx + dy * dy ) ;
      if ( lend - lenv < sqrt( ox * ox + oy * oy )) {
        SYSTEMVALUE tx , ty , sxy ;
        HQASSERT( lenv != 0.0 , "should not be dividing by zero" ) ;
        sxy = lend / lenv ;
        tx = sxy * vx ;
        ty = sxy * vy ;
        if (( fabs( tx - dx ) < ex && fabs( ty - dy ) < ey ) ||
            ( fabs( tx + dx ) < ex && fabs( ty + dy ) < ey )) {
          return TRUE ;
        }
      }
    }
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
Bool pathsplitsrect( PATHINFO *path1 , PATHINFO *path2 , int32 type , int32 *rtype )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  PATHLIST *thepath ;

  SYSTEMVALUE x , y ;
  SEGMENT seg ;

  HQASSERT( path1 , "path1 NULL in pathsplitsrect" ) ;
  HQASSERT( path2 , "path2 NULL in pathsplitsrect" ) ;
  HQASSERT( rtype , "rtype NULL in pathsplitsrect" ) ;
  HQASSERT( type == VDT_Circle || type == VDT_RectangleDevice ||
            type == VDT_RectangleUser , "type is unknown" ) ;

  (*rtype) = VDR_Unknown ;

  thepath = path2->firstpath ;
  HQASSERT( thepath , "thepath (path2) NULL in pathsplitsrect" ) ;

  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
  HQASSERT( theline , "Should have got a line from the rectangle" ) ;

  x = 0.5 * ( theX( seg.pnts[ 0 ] ) + theX( seg.pnts[ 1 ] )) ;
  y = 0.5 * ( theY( seg.pnts[ 0 ] ) + theY( seg.pnts[ 1 ] )) ;

  thepath = path1->firstpath ;
  HQASSERT( thepath , "thepath (path1) NULL in pathsplitsrect" ) ;

  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
    HQASSERT( theline , "should really get 4 sections for a rect or circle." ) ;
    if ( theline == NULL ) {
      HQASSERT( index == 0 || index == 2 ,
                "should only get 0, 2 (or 4) sections for a rectangle" ) ;
      return FALSE ;
    }
    if ( fabs( x - theX( seg.pnts[ 0 ] )) < gstateptr->pa_eps.ex &&
         fabs( y - theY( seg.pnts[ 0 ] )) < gstateptr->pa_eps.ey ) {
      (*rtype) = ( type == VDT_Circle ? VDR_Circular : VDR_Diamond ) ;
      return TRUE ;
    }
  }
#if defined( ASSERT_BUILD )
  { /* Got 4 segments; should not be a fifth. */
    LINELIST *debugline ;
    SEGMENT   debugseg ;
    debugline = getsegment( theline , theISubPath( thepath ) , & debugseg , endline ) ;
    HQASSERT( debugline == NULL , "should only be 4 segments in a rectangle" ) ;
  }
#endif

  if ( seg.type != CURVETO ) {
    x = 0.5 * ( theX( seg.pnts[ 0 ] ) + theX( seg.pnts[ 1 ] )) ;
    y = 0.5 * ( theY( seg.pnts[ 0 ] ) + theY( seg.pnts[ 1 ] )) ;
  }
  else {
    bezeval(seg.pnts, 0.5, &x, &y);
  }

  thepath = path2->firstpath ;

  theline = theISubPath( thepath ) ;
  endline = theISubPath( thepath ) ;
  for ( index = 0 ; index < 4 ; ++index ) {
    theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
    if ( theline == NULL ) {
      HQASSERT( type == VDT_RectangleUser || type == VDT_RectangleDevice ,
                "should only get less segments for a rectangle" ) ;
      HQASSERT( index == 0 || index == 2 ,
                "should only get 0, 2 (or 4) sections for a rectangle" ) ;
      return FALSE ;
    }
    if ( fabs( x - theX( seg.pnts[ 0 ] )) < gstateptr->pa_eps.ex &&
         fabs( y - theY( seg.pnts[ 0 ] )) < gstateptr->pa_eps.ey ) {
      (*rtype) = ( type == VDT_Circle ? VDR_FullCircular : VDR_FullDiamond ) ;
      return TRUE ;
    }
  }
#if defined( ASSERT_BUILD )
  { /* Got 4 segments; should not be a fifth. */
    LINELIST *debugline ;
    SEGMENT   debugseg ;
    debugline = getsegment( theline , theISubPath( thepath ) , & debugseg , endline ) ;
    HQASSERT( debugline == NULL , "should only be 4 segments in a rectangle" ) ;
  }
#endif

  return FALSE ;
}

/* -------------------------------------------------------------------------- */
/* Checks if the first path is inside the second (rectangular) path.
 * Note if some of the control points are outside this may give a false negative.
 */
Bool pathisinsiderectanglepath( PATHINFO *path1 , PATHINFO *path2 , int32 type )
{
  HQASSERT( path1 , "path1 NULL in pathisinsiderectanglepath" ) ;
  HQASSERT( path2 , "path2 NULL in pathisinsiderectanglepath" ) ;

  if ( type == VDT_RectangleDevice ) {
    /* Rectangle is device aligned, just use the bbox. */
    sbbox_t *pathbbox = & path1->bbox ;
    sbbox_t *clipbbox = & path2->bbox ;
    HQASSERT( path1->bboxtype != BBOX_NOT_SET, "using path1 bbox but not set" ) ;
    HQASSERT( path2->bboxtype != BBOX_NOT_SET, "using path2 bbox but not set" ) ;
    if ( !bbox_contains_epsilon(clipbbox, pathbbox, PA_EPSILON, PA_EPSILON) )
      return FALSE ;
  }
  else {
    PATHLIST *path ;
    SYSTEMVALUE length[ 2 ] ;
    OMATRIX matrix ;
    HQASSERT( type == VDT_RectangleUser ,
              "type should rect user in pathisinsiderectanglepath" ) ;
    /* Not device aligned, find the matrix inverse and the bbox is the unit square. */
    get_path_matrix( path2 , & matrix , length ) ;
    path = path1->firstpath ;
    while ( path ) {
      LINELIST *line = theISubPath( path ) ;
      while ( line ) {
        SYSTEMVALUE x = theX( theIPoint ( line )) ;
        SYSTEMVALUE y = theY( theIPoint ( line )) ;
        SYSTEMVALUE tx , ty ;
        /* Apply matrix to each point in the line. */
        MATRIX_TRANSFORM_XY( x , y , tx , ty , & matrix ) ;
        /* Check it is within the unit square. */
        if ( tx < 0.0 - PA_EPSILON ||
             ty < 0.0 - PA_EPSILON ||
             tx > 1.0 + PA_EPSILON ||
             ty > 1.0 + PA_EPSILON )
          return FALSE ;
        line = line->next ;
      }
      path = path->next ;
    }
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Assuming the paths are 'similar' (tested according to pathsaresimiliar)
 * works out the matrix to map path2 to path1. Used to extend a vignette for
 * overprinted white objects; uses same matrix to extrapolate a 'path0'.
 */
Bool pathsmatrixscale( PATHINFO *path1 , PATHINFO *path2 , OMATRIX *t_matrix )
{
  SYSTEMVALUE dx , dy , Tx , Ty , Sx , Sy ;

  HQASSERT( path1 != NULL , "pathsmatrixscale: path1 null" ) ;
  HQASSERT( path2 != NULL , "pathsmatrixscale: path2 null" ) ;
  HQASSERT( t_matrix != NULL , "pathsmatrixscale: t_matrix null" ) ;

#if defined( ASSERT_BUILD )
  {
    int32 match ;
    /* Check that path2/path1 differ by a scaling only */
    HQASSERT( pathsaresimilar(path1, path2, TRUE, VDM_Scaled, VDM_Unknown,
              &match ) , "pathsmatrixscale: paths are not even similar" ) ;
    HQASSERT( match & VDM_Scaled ,
          "pathsmatrixscale: must be a scale match between path1 and path2" ) ;
  }
#endif

  if ( path1->bboxtype == BBOX_NOT_SET || path2->bboxtype == BBOX_NOT_SET ) {
    HQFAIL( "pathsmatrixscale: Must have both bboxes" ) ;
    return error_handler( UNDEFINED ) ;
  }

  dx = ( path1->bbox.x2 - path1->bbox.x1 ) ;
  dy = ( path1->bbox.y2 - path1->bbox.y1 ) ;
  /* Check for dividing by a very small number */
  if ( dx < EPSILON || dy < EPSILON ) {
    HQFAIL( "pathsmatrixscale: "
            "About to error before trying to divide by a very small number" ) ;
    return error_handler( RANGECHECK ) ;
  }

  /* Produces a matrix that will map path1 to a path0 (using info from
   * mapping path2 to path1). Assumes scaling is the only transformation
   * between the paths (no rotation or translation).
   *
   * (   1   0   0 ) (  Sx   0   0 ) (   1   0   0 )
   * (   0   1   0 ) (   0  Sy   0 ) (   0   1   0 )
   * ( -Tx -Ty   1 ).(   0   0   1 ).(  Tx  Ty   1 )
   *
   * where Tx,y is the centre of the paths and Sx,y is the scaling
   * between the paths (calculated from the bounding boxes)
   */

  Tx = ( path1->bbox.x1 + path1->bbox.x2 ) * 0.5 ;
  Ty = ( path1->bbox.y1 + path1->bbox.y2 ) * 0.5 ;

  /* width0 / width1 == ( width1 + ( width1 - width2 ) ) / width1 */
  Sx = 2.0 - (( path2->bbox.x2 - path2->bbox.x1 ) / dx ) ;
  Sy = 2.0 - (( path2->bbox.y2 - path2->bbox.y1 ) / dy ) ;

  t_matrix->matrix[ 0 ][ 0 ] = Sx ;
  t_matrix->matrix[ 0 ][ 1 ] = 0.0 ;
  t_matrix->matrix[ 1 ][ 0 ] = 0.0 ;
  t_matrix->matrix[ 1 ][ 1 ] = Sy ;
  t_matrix->matrix[ 2 ][ 0 ] = Tx * ( 1.0 - Sx ) ;
  t_matrix->matrix[ 2 ][ 1 ] = Ty * ( 1.0 - Sy ) ;

  MATRIX_SET_OPT_BOTH( t_matrix ) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
#ifdef COMPLEX_BLENDS

/*
 * These routines can be used to detect complex blends of various complexities.
 * The code however is not complete and so needs finishing off before using.
 */

#define QUADRANT_1ST 0x00
#define QUADRANT_2ND 0x01
#define QUADRANT_MDL 0x02
#define QUADRANT_LST 0x03

static int32 qd_table[ 0x10 ] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x05, 0x03, 0x04,
  0xFF, 0x07, 0x01, 0x00, 0xFF, 0x06, 0x02, 0xFF
} ;

/* A fast method for verifying a simple shape.
 * Essentially this algorithm checks that the angles between
 * succesive line/curve elements move round the quadrants in
 * small steps (i.e. no acute angles exist). It will pick up
 * circles, rectangles and most concave shapes quickly. It can
 * obviously be extended to divide the circle into octants
 * instead to pick up more shapes (at more compute cost).
 * We also get for free the orientation of the path and if it
 * is a rectangle in device space.
 */
Bool pathisverysimple( PATHINFO *path , Bool *degenerate , int32 *orientation , int32 *type )
{
  int32 notdevicerect ;
  int32 quadrant ;
  int32 quadrant1 = 0 , quadrant2 = 0 , quadrant3 = 0 ;
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in pathisverysimple" ) ;
  HQASSERT( degenerate , "degenerate NULL in pathisverysimple" ) ;
  HQASSERT( orientation , "orientation NULL in pathisverysimple" ) ;
  HQASSERT( type , "type NULL in pathisverysimple" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in pathisverysimple" ) ;

  (*type) = VDT_Unknown ;
  (*degenerate) = FALSE ;
  (*orientation) = VDO_Unknown ;

  theline = theISubPath( thepath ) ;
  theline = getsegment( theline , theISubPath( thepath ) , & seg , theISubPath( thepath )) ;
  seg.index = 0 ;
  if ( theline == NULL ) {
    (*degenerate) = TRUE ;
    return FALSE ;
  }

  index = 0 ;
  endline = seg.line ;
  quadrant = QUADRANT_1ST ;
  notdevicerect = 0 ;
  do {
    int32 i , j ;
    int32 segments ;
    int32 newquadrant ;

    segments = 1 ;
    if ( seg.type == CURVETO )
      segments = 3 ;

    for ( i = 0 ; i < segments ; ++i ) {
      /* Work out quadrant line/curve segment is in by using
       * simple comparisons and table lookup.
       */
      CALCULATE_QUADRANT( & seg , i , newquadrant ) ;

      /* Ignore degenerate points (found by being in all quadrants). */
      if ( newquadrant != 0x0f ) {
        notdevicerect |= QUADRANT_NOT_DEVICE_ALIGNED( newquadrant ) ;
        if ( quadrant == QUADRANT_MDL ) {
          if ( newquadrant == quadrant1 )
            quadrant = QUADRANT_LST ;
          else {
            if ( quadrant2 > quadrant1 ) {
              if ( newquadrant < quadrant1 )
                newquadrant += 0x08 ;

              if ( newquadrant < quadrant3 || newquadrant - quadrant3 >= 0x04 )
                return FALSE ;
            }
            else /* if ( quadrant2 < quadrant1 ) */ {
              if ( newquadrant > quadrant1 )
              newquadrant -= 0x08 ;

              if ( newquadrant > quadrant3 || quadrant3 - newquadrant >= 0x04 )
                return FALSE ;
            }
            quadrant3 = newquadrant ;
          }
        }
        else if ( quadrant == QUADRANT_LST ) {
          /* Once we've gone back to the starting quadrant, we must stay in it. */
          if ( newquadrant != quadrant1 )
            return FALSE ;
        }
        else if ( quadrant == QUADRANT_1ST ) {
#define MIDDLE_QUADRANT 4
          /* Move quadrants round so that start quadrant is middle one. */
          int32 modfact = ( 0x08 + MIDDLE_QUADRANT ) - newquadrant ;
          for ( j = 0 ; j < 0x10 ; ++j )
            qd_table[ j ] = ( modfact + qd_table[ j ] ) & 0x07 ;
          quadrant = QUADRANT_2ND ;
          quadrant1 = MIDDLE_QUADRANT ;
        }
        else /* if ( quadrant == QUADRANT_2ND ) */ {
          if ( newquadrant != quadrant1 ) {
            if ( newquadrant == 0x00 )
              return FALSE ;
            quadrant = QUADRANT_MDL ;
            quadrant2 = quadrant3 = newquadrant ;
            endline = seg.line ; /* So we go back round again. */
          }
        }
      }
    }

    theline = getsegment( theline , theISubPath( thepath ) , & seg , endline ) ;
    seg.index = (++index) ;
  } while ( theline ) ;

  /* Need this return check to ignore completely degenerate paths. */
  if ( quadrant == QUADRANT_LST ) {
    (*type) = ( index == (4+1) && ! notdevicerect ) ? VDT_RectangleDevice : VDT_Simple ;
    (*orientation) = ( quadrant2 > quadrant1 ) ? VDO_ClockWise : VDO_AntiClockWise ;
    return TRUE ;
  }

  if ( quadrant == QUADRANT_1ST || quadrant == QUADRANT_2ND )
    (*degenerate) = TRUE ;

  return FALSE ;
}

Bool pathissimple( PATHINFO *path , int32 *degenerate , int32 *orientation , int32 *type )
{
  PATHLIST *thepath ;

  HQASSERT( path , "path NULL in pathissimple" ) ;
  HQASSERT( degenerate , "degenerate NULL in pathissimple" ) ;
  HQASSERT( orientation , "orientation NULL in pathissimple" ) ;
  HQASSERT( type , "type NULL in pathissimple" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in pathissimple" ) ;

  (*type) = VDT_Unknown ;
  (*degenerate) = FALSE ;
  (*orientation) = VDO_Unknown ;

  if ( ! pathsintersect( thepath , thepath , TRUE ))
    return FALSE ;

  (*type) = VDT_Complex ;
  pathsorientation( thepath , orientation ) ;

  return TRUE ;
}

/* Checks if the first path is correctly inside the second path.
 */
Bool pathisinsidepath( PATHINFO *path1 , PATHINFO *path2 )
{
  return pathsintersect( path1 , path2 , FALSE ) &&
         pointofpathinsidepath( path1 , path2 ) ;
}

/* Checks if any single point of the first path is correctly inside
 * the second path.
 */
Bool pointofpathinsidepath( PATHINFO *path1 , PATHINFO *path2 )
{
  HQASSERT( path1 , "path1 NULL in pointofpathinsidepath" ) ;
  HQASSERT( path2 , "path2 NULL in pointofpathinsidepath" ) ;

  HQFAIL( "NYI: pointofpathinsidepath" ) ;
  return TRUE ;
}

static Bool flatpathsintersect( PATHINFO *path1 , PATHINFO *path2 , int32 checkjoins )
{
  int32 result ;
  PATHINFO tempinfo ;

  HQASSERT( path1 , "path1 NULL in flatpathsintersect" ) ;
  HQASSERT( path2 , "path2 NULL in flatpathsintersect" ) ;

  if ( ! path_flatten( path1->firstpath , & tempinfo ))
    return FALSE ;

  if ( path1 == path2 )
    result = pathsintersect( & tempinfo , & tempinfo , checkjoins ) ;
  else
    result = pathsintersect( & tempinfo , path2 , checkjoins ) ;
  path_free_list( thePath( tempinfo )) ;

  return ( result ) ;
}

/* Check if two line segments intersect.
 * Note that we can actually get a curve here. In this case we need
 * to check the intersection of the line segment between the first
 * and last control points.
 */
static Bool segmentlinesintersect( SEGMENT *seg0 , SEGMENT *seg1 )
{
  int32 i0 , i1 , sign ;
  SYSTEMVALUE r12 ;
  SYSTEMVALUE det ;
  SYSTEMVALUE dx0 , dy0 , dx1 , dy1 ;
  SYSTEMVALUE dxt , dyt ;

  HQASSERT( seg0 , "seg0 NULL in segmentlinesintersect" ) ;
  HQASSERT( seg1 , "seg1 NULL in segmentlinesintersect" ) ;

  i0 = 1 ;
  if ( seg0->type == CURVETO )
    i0 = 3 ;
  i1 = 1 ;
  if ( seg1->type == CURVETO )
    i1 = 3 ;

  dx0 = theX( seg0->pnts[ 0 ] ) - theX( seg0->pnts[ i0 ] ) ;
  dy0 = theY( seg0->pnts[ 0 ] ) - theY( seg0->pnts[ i0 ] ) ;

  dx1 = theX( seg1->pnts[ 0 ] ) - theX( seg1->pnts[ i1 ] ) ;
  dy1 = theY( seg1->pnts[ 0 ] ) - theY( seg1->pnts[ i1 ] ) ;

  det = dx0 * dy1 - dy0 * dx1 ;

  sign = 1 ;
  if ( det < 0.0 ) {
    sign = -1 ;
    det = -det ;
  }

  if ( det < PA_EPSILON ) /* Lines are parallel. */
    return TRUE ;

  dxt = theX( seg0->pnts[ i0 ] ) - theX( seg1->pnts[ i1 ] ) ;
  dyt = theY( seg0->pnts[ i0 ] ) - theY( seg1->pnts[ i1 ] ) ;

  r12 = dyt * dx1 - dxt * dy1 ;
  if ( sign < 0 )
    r12 = -r12 ;

  det += PA_EPSILON ;
  if ( r12 < -PA_EPSILON || r12 > det )
    return TRUE ;

  r12 = dyt * dx0 - dxt * dy0 ;
  if ( sign < 0 )
    r12 = -r12 ;

  if ( r12 < -PA_EPSILON || r12 > det )
    return TRUE ;

  return FALSE ;
}

#define CALCULATE_QUADRANT( _seg, _i, _newquad ) MACRO_START \
  SYSTEMVALUE _t1_ , _t2_ ; \
  (_newquad) = 0 ; \
  _t1_ = theX( (_seg)->pnts[ (_i) ] ) ; \
  _t2_ = theX( (_seg)->pnts[ (_i) + 1 ] ) ; \
  if ( _t1_ + PA_EPSILON >= _t2_ ) \
    (_newquad) |= 0x01 ; \
  if ( _t1_ - PA_EPSILON <= _t2_ ) \
    (_newquad) |= 0x02 ; \
  _t1_ = theY( (_seg)->pnts[ (_i) ] ) ; \
  _t2_ = theY( (_seg)->pnts[ (_i) + 1 ] ) ; \
  if ( _t1_ + PA_EPSILON >= _t2_ ) \
    (_newquad) |= 0x04 ; \
  if ( _t1_ - PA_EPSILON <= _t2_ ) \
    (_newquad) |= 0x08 ; \
  (_newquad) = qd_table[ (_newquad) ] ; \
MACRO_END

#define CALCULATE_VECTOR( _seg, _i1, _i2, _vx, _vy ) MACRO_START \
  (_vx) = theX( (_seg)->pnts[ (_i2) ] ) - \
          theX( (_seg)->pnts[ (_i1) ] ) ; \
  (_vy) = theY( (_seg)->pnts[ (_i2) ] ) - \
          theY( (_seg)->pnts[ (_i1) ] ) ; \
MACRO_END

#define QUADRANT_NOT_DEVICE_ALIGNED( _quadrant ) \
  (((_quadrant) & 0x01 ) ^ ( qd_table[ 0x07 ] & 0x01 ))

/* A method for verifying that a segment is well behaved. That is that it
 * doesn't intersect it's neighbour and that if it's a bezier curve that it's
 * well behaved. Do this by looking at the quadrants of the two segments.
 * They will only intersect if their quadrants are completely opposite, and
 * their gradients are "equal". For bezier curves check that their control
 * points do not cause an inversion.
 */
static Bool segmentwelljoined( SEGMENT *seg0 , SEGMENT *seg1 )
{
  int32 i ;
  int32 quadrant0 , quadrant1 ;

  HQASSERT( seg0 , "seg0 NULL in segmentwelljoined" ) ;
  HQASSERT( seg1 , "seg1 NULL in segmentwelljoined" ) ;


  /* If seg0 is a bezier, check it's well formed. */
  i = 0 ;
  if ( seg0->type == CURVETO ) {
    SEGMENT tseg1 , tseg2 ;
    tseg1.type = LINETO ;
    theX( tseg1.pnts[ 0 ] ) = theX( seg0->pnts[ 0 ] ) ;
    theY( tseg1.pnts[ 0 ] ) = theY( seg0->pnts[ 0 ] ) ;
    theX( tseg1.pnts[ 1 ] ) = theX( seg0->pnts[ 1 ] ) ;
    theY( tseg1.pnts[ 1 ] ) = theY( seg0->pnts[ 1 ] ) ;
    tseg2.type = LINETO ;
    theX( tseg2.pnts[ 0 ] ) = theX( seg0->pnts[ 2 ] ) ;
    theY( tseg2.pnts[ 0 ] ) = theY( seg0->pnts[ 2 ] ) ;
    theX( tseg2.pnts[ 1 ] ) = theX( seg0->pnts[ 3 ] ) ;
    theY( tseg2.pnts[ 1 ] ) = theY( seg0->pnts[ 3 ] ) ;
    /* If line segments of control points (0,1) & (2,3) intersect, then possible
     * loop inside bezier curve.
     */
    HQFAIL( "NYI: segmentwelljoined(bezier curve check)" ) ;
    if ( ! segmentlinesintersect( & tseg1 , & tseg2 )) {
      HQTRACE(trace_pa,("potential loop inside bezier curve"));
      /* Maybe later on if we really wanted to we could improve this, but not now.
       * If we wanted to then we'd have to construct a simple path containing this
       * bezier curve and call flatpathsintersect(..) on it.
       */
      return FALSE ;
    }
    i = 2 ;
  }

  HQASSERT( theX( seg0->pnts[ i + 1 ] ) == theX( seg1->pnts[ 0 ] ) , "seg not x-joined" ) ;
  HQASSERT( theY( seg0->pnts[ i + 1 ] ) == theY( seg1->pnts[ 0 ] ) , "seg not y-joined" ) ;

  if ( seg0->type == CURVETO || seg1->type == CURVETO ) {
    HQFAIL( "NYI: segmentwelljoined(curve check)" ) ;
  }
  else {  /* Both are lines. */

    CALCULATE_QUADRANT( seg0 , 0 , quadrant0 ) ;

    /* Then check join between seg0 & seg1. */
    CALCULATE_QUADRANT( seg1 , 0 , quadrant1 ) ;

    if ( quadrant0 - quadrant1 == 0x04 || quadrant1 - quadrant0 == 0x04 ) {
      SYSTEMVALUE dx1 , dy1 , dx2 , dy2 ;
      /* Quadrants go in opposite directions. Possible intersection. */
      if ( ! QUADRANT_NOT_DEVICE_ALIGNED( quadrant0 )) {
        /* Lines are orthogonal to device axes, so definitely intersect. */
        return FALSE ;
      }

      dx1 = theX( seg0->pnts[ i + 1 ] ) - theX( seg0->pnts[ i ] ) ;
      dy1 = theY( seg0->pnts[ i + 1 ] ) - theY( seg0->pnts[ i ] ) ;
      dx2 = theX( seg1->pnts[ 0 + 1 ] ) - theX( seg1->pnts[ 0 ] ) ;
      dy2 = theY( seg1->pnts[ 0 + 1 ] ) - theY( seg1->pnts[ 0 ] ) ;

      if ( fabs( dy1 * dx2 - dx1 * dy2 ) < PA_EPSILON )
        return FALSE ;
    }
  }
  return TRUE ;
}

/* A method for comparing two segments and seeing if they intersect.
 * Uses bounding box info and subdivides any curves as necessary.
 */
static Bool segmentbboxintersectbez( SEGMENT *checkseg , SEGMENT *reduceseg ,
                                     Bool reducecheckseg , int32 level )
{
  HQASSERT( checkseg , "checkseg NULL in segmentbboxintersectbez" ) ;
  HQASSERT( reduceseg , "reduceseg NULL in segmentbboxintersectbez" ) ;

  /** \todo bmj 12-06-2007: replace freduce/nextfbez with bezchop() call */
  if ( freduce( reduceseg->pnts )) {
    int32 hilo ;
    SEGMENT segtmp[ 2 ] ;
    nextfbez( reduceseg->pnts , segtmp[ 0 ].pnts , segtmp[ 1 ].pnts ) ;

    for ( hilo = 0 ; hilo < 2 ; ++hilo ) {
      int32 index ;
      SEGMENT *seg ;
      SYSTEMVALUE x1 , x2 , y1 , y2 ;

      seg = & segtmp[ hilo ] ;
      x1 = x2 = theX( seg->pnts[ 0 ] ) ;
      y1 = y2 = theY( seg->pnts[ 0 ] ) ;
      for ( index = 1 ; index < 4 ; ++index ) {
        SYSTEMVALUE temp ;

        temp = theX( seg->pnts[ index ] ) ;
        if ( temp < x1 )
          x1 = temp ;
        else if ( temp > x2 )
          x2 = temp ;

        temp = theY( seg->pnts[ index ] ) ;
        if ( temp < y1 )
          y1 = temp ;
        else if ( temp > y2 )
          y2 = temp ;
      }

      if ( checkseg->x1 > x2 || checkseg->x2 < x1 || checkseg->y1 > y2 ||
           checkseg->y2 < y1 ) {
         HQTRACE(trace_pa,("segment (%d) doesn't intersect at all %d",level,hilo));
      }
      else {
        seg->x1 = x1 ;
        seg->y1 = y1 ;
        seg->x2 = x2 ;
        seg->y2 = y2 ;
        seg->type = CURVETO ;
        seg->line = seg->curv = NULL ;
        if ( ! segmentbboxintersectbez( checkseg , seg , reducecheckseg , level + 1 ))
          return FALSE ;
      }
    }
    return TRUE ;
  }
  else if ( reducecheckseg ) {
    if ( checkseg->type == CURVETO )
      return segmentbboxintersectbez( reduceseg , checkseg , FALSE , 1 ) ;
    HQTRACE(trace_pa,("need to check if lines intersect(segmentbboxintersectbez)"));
    return segmentlinesintersect( reduceseg , checkseg ) ;
  }
  else {
    HQTRACE(trace_pa,("need to check if lines intersect(segmentbboxintersectbez)"));
    return segmentlinesintersect( reduceseg , checkseg ) ;
  }
}

static LINELIST *getsegmentbbox( LINELIST *theline , LINELIST *startline ,
                                 SEGMENT *bbox , LINELIST *endline )
{
  HQASSERT( theline , "theline NULL in getsegmentbbox" ) ;
  HQASSERT( bbox , "bbox NULL in getsegmentbbox" ) ;

  for (;;) {
    int32 type ;
    int32 index = 1 ;
    int32 segments = 1 ;
    SYSTEMVALUE x1 , x2 , y1 , y2 ;
    LINELIST *tmpline , *segline , *curline ;

    curline = theline ;
    x1 = x2 = theX( bbox->pnts[ 0 ] ) = theX( theIPoint( theline )) ;
    y1 = y2 = theY( bbox->pnts[ 0 ] ) = theY( theIPoint( theline )) ;
    theline = theline->next ;
    if ( theline == NULL )
      theline = startline ;
    if ( theline == endline )
      return NULL ;

    tmpline = segline = theline ;
    type = theILineType( tmpline ) ;
    switch ( type ) {
    case CURVETO:
      segments = 3 ;
      theline = theline->next ;
      theline = theline->next ;
    default:
      while ((--segments) >= 0 ) {
        SYSTEMVALUE tx, ty ;
        tx = theX( bbox->pnts[ index ] ) = theX( theIPoint( tmpline )) ;
        if ( tx < x1 )
          x1 = tx ;
        else if ( tx > x2 )
          x2 = tx ;
        ty = theY( bbox->pnts[ index ] ) = theY( theIPoint( tmpline )) ;
        if ( ty < y1 )
          y1 = ty ;
        else if ( ty > y2 )
          y2 = ty ;
        ++index ;
        tmpline = tmpline->next ;
      }
      if ( fabs( x1 - x2 ) >= gstateptr->pa_eps.ex ||
           fabs( y1 - y2 ) >= gstateptr->pa_eps.ey ) {
        if ( x1 > x2 ) {
          SYSTEMVALUE tmp = x1 ; x1 = x2 ; x2 = tmp ;
        }
        if ( y1 > y2 ) {
          SYSTEMVALUE tmp = y1 ; y1 = y2 ; y2 = tmp ;
        }
        HQTRACE(trace_pa,("segment: (%f,%f),(%f,%f)",x1,y1,x2,y2));
        bbox->x1 = x1 ;
        bbox->y1 = y1 ;
        bbox->x2 = x2 ;
        bbox->y2 = y2 ;
        bbox->type = type ;
        bbox->line = segline ;
        bbox->curv = curline ;
        return ( theline ) ;
      }
    }
  }
  /* NOT REACHED */
}

static Bool segmentbboxintersect( SEGMENT *checkseg , PATHINFO *path , SEGMENT *ignoreseg1 , SEGMENT *ignoreseg2 )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT seg ;
  PATHLIST *thepath ;

  HQASSERT( checkseg , "checkseg NULL in segmentbboxintersect" ) ;
  HQASSERT( path , "path NULL in segmentbboxintersect" ) ;
  HQASSERT( ignoreseg1 , "ignoreseg1 NULL in segmentbboxintersect" ) ;
  HQASSERT( ignoreseg2 , "ignoreseg2 NULL in segmentbboxintersect" ) ;

  thepath = path->firstpath ;
  HQASSERT( thepath , "thepath NULL in segmentbboxintersect" ) ;

  theline = theISubPath( thepath ) ;
  theline = getsegmentbbox( theline , theISubPath( thepath ) , & seg , theISubPath( thepath )) ;
  seg.index = 0 ;

  index = 0 ;
  endline = seg.line ;
  do {
    if ( seg.line != checkseg->line &&
         seg.line != ignoreseg1->line &&
         seg.line != ignoreseg2->line ) {
      if ( checkseg->x1 > seg.x2 || checkseg->x2 < seg.x1 ||
           checkseg->y1 > seg.y2 || checkseg->y2 < seg.y1 ) {
         HQTRACE(trace_pa,("segment %d doesn't intersect at all",index));
      }
      else {
        if ( checkseg->type == CURVETO ) {
          /* Need to flatten checkseg and recurse. */
          if ( ! segmentbboxintersectbez( & seg , checkseg , TRUE , 1 ))
            return FALSE ;
        }
        else {
          if ( seg.type == CURVETO ) {
            /* Need to flatten seg and recurse. */
            if ( ! segmentbboxintersectbez( checkseg , & seg , FALSE , 1 ))
              return FALSE ;
          }
          else {
            /* Both are lines. Need to check if they intersect or touch. */
            HQTRACE(trace_pa,("need to check if lines intersect(segmentbboxintersect)"));
            if ( ! segmentlinesintersect( checkseg , & seg ))
              return FALSE ;
          }
        }
      }
    }

    theline = getsegmentbbox( theline , theISubPath( thepath ) , & seg , endline ) ;
    seg.index = (++index) ;
  } while ( theline ) ;
  return TRUE ;
}

/* Checks to see if either one path doesn't self intersect or two paths
 * don't intersect with each other. For now they two path case is such
 * that the paths must be fully inside each other. Later on this can be
 * improved so that they are allowed to touch boundaries [accasionally].
 */
Bool pathsintersect( PATHINFO *path1, PATHINFO *path2, int32 checkjoins )
{
  int32 index ;
  LINELIST *theline ;
  LINELIST *endline ;
  SEGMENT segs[ 3 ] ;
  SEGMENT *seg0 , *seg1 , *seg2 ;
  PATHLIST *p1 , *p2 ;

  HQASSERT( path1 , "path1 NULL in pathsintersect" ) ;
  HQASSERT( path2 , "path2 NULL in pathsintersect" ) ;

  p1 = path1->firstpath ;
  HQASSERT( p1 , "p1 NULL in pathsintersect" ) ;
  p2 = path2->firstpath ;
  HQASSERT( p2 , "p2 NULL in pathsintersect" ) ;

  theline = theISubPath( p1 ) ;
  theline = getsegmentbbox( theline , theISubPath( p1 ) , seg0 = & segs[ 0 ] , theISubPath( p1 )) ;
  if ( theline == NULL ) {
    HQTRACE(trace_pa,("degenerate path; no segments"));
    return FALSE ;
  }
  seg0->index = 0 ;

  endline = segs[ 0 ].line ;

  theline = getsegmentbbox( theline , theISubPath( p1 ) , seg1 = & segs[ 1 ] , endline ) ;
  if ( theline == NULL ) {
    if ( seg0->type == CURVETO ) {
   /* Check for sub-curve being ok; do the brute force way. */
      return flatpathsintersect( path1 , path2 , checkjoins ) ;
    }
    HQTRACE(trace_pa,("degenerate path; no second segment"));
    return FALSE ;
  }
  seg1->index = 1 ;

  theline = getsegmentbbox( theline , theISubPath( p1 ) , seg2 = & segs[ 2 ] , endline ) ;
  if ( theline == NULL ) {
    if ( seg0->type == CURVETO ||
         seg1->type == CURVETO ) {
      /* Check for sub-curves being ok; do the brute force way. */
      return flatpathsintersect( path1 , path2 , checkjoins ) ;
    }
    HQTRACE(trace_pa,("degenerate path; no third segment"));
    return FALSE ;
  }
  seg2->index = 2 ;

  index = 2 ;
  endline = segs[ 2 ].line ;
  do {
    /* Now got three segments.
     * Must check that 2 doesn't intersect with any other segment.
     */
    HQTRACE(trace_pa,("segment: %d",index));
    if ( ! segmentbboxintersect( seg1 , path2 , seg0 , seg2 ))
      return FALSE ;

    if ( checkjoins )
      if ( ! segmentwelljoined( seg1 , seg2 ))
        return FALSE ;

    seg0 = seg1 ;
    seg1 = seg2 ;
    seg2 = & segs[ (++index) % 3 ] ;
    theline = getsegmentbbox( theline , theISubPath( p1 ) , seg2 , endline ) ;
    seg2->index = index ;
  } while ( theline ) ;
  return TRUE ;
}

Bool strokedpathsintersect( PATHINFO *path1, PATHINFO *path2, int32 checkjoins )
{
  HQASSERT( path1 , "path1 NULL in strokedpathsintersect" ) ;
  HQASSERT( path2 , "path2 NULL in strokedpathsintersect" ) ;

  HQFAIL( "NYI: strokedpathsintersect" ) ;

  /* Silence compiler. */
  path1 = path1 ;
  path2 = path2 ;
  checkjoins = checkjoins ;

  return FALSE ; /* arbitary till properly supported later on... */
}

/* Works out the orientation of the given path. Does this by taking
 * the cross product of the lowest, rightmost point of the path.
 */
static void pathsorientation( PATHINFO *path , int32 *orientation )
{
  HQFAIL( "NYI: pathsorientation" ) ;
  return ;
}

#endif

void init_C_globals_panalyze(void)
{
#if defined( ASSERT_BUILD )
  trace_pa = FALSE ;
#endif
}

/* Log stripped */
