/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:pathcons.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS path construction functions
 */

#define NEED_MYATAN2

#include "core.h"
#include "objects.h"
#include "swerrors.h"

#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "constant.h"
#include "gu_path.h"
#include "system.h"
#include "mathfunc.h" /* myatan2 */
#include "pathops.h"
#include "gu_ctm.h"
#include "gu_cons.h"
#include "stacks.h"
#include "params.h"

#include "pathcons.h"

#define ISZERO(x) (fabs(x) < EPSILON)

static Bool arc_t_to(Bool push_results);

/* ----------------------------------------------------------------------------
   function:            newpath_()         author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 187.

---------------------------------------------------------------------------- */
Bool gs_newpath(void)
{
  PATHINFO *lpath = &(thePathInfo(*gstateptr)) ;

  if ( lpath->lastline )
    path_free_list( lpath->firstpath, mm_pool_temp) ;

  path_init(lpath) ;

  return TRUE ;
}

Bool newpath_(void)
{
  return gs_newpath() ;
}

/* ----------------------------------------------------------------------------
   function:            currpoint_()       author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 138.

---------------------------------------------------------------------------- */
Bool currpoint_(void)
{
  SYSTEMVALUE tx ;
  SYSTEMVALUE ty ;

  if ( !gs_currentpoint(&thePathInfo(*gstateptr), &tx, &ty) )
    return FALSE ;

  return (stack_push_real( tx, &operandstack ) &&
          stack_push_real( ty, &operandstack )) ;
}

Bool gs_currentpoint(PATHINFO *path, SYSTEMVALUE *currx, SYSTEMVALUE *curry)
{
  register SYSTEMVALUE tx ;
  register SYSTEMVALUE ty ;
  register LINELIST *theline = (path->lastline);

  if ( ! theline )
    return error_handler( NOCURRENTPOINT ) ;

  SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
  if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
    return error_handler( UNDEFINEDRESULT ) ;

  MATRIX_TRANSFORM_XY( theX( theIPoint( theline )), theY( theIPoint( theline )),
                       tx, ty, & sinv ) ;

  *currx = tx ;
  *curry = ty ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            moveto_()          author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 186.

---------------------------------------------------------------------------- */
Bool moveto_(void)
{
  SYSTEMVALUE args[ 2 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 2) )
    return FALSE ;

  if ( ! gs_moveto(TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

Bool rmoveto_(void)
{
  SYSTEMVALUE args[ 2 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 2) )
    return FALSE ;

  if ( ! gs_moveto(FALSE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            lineto_()          author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 180.

---------------------------------------------------------------------------- */
Bool lineto_(void)
{
  SYSTEMVALUE args[ 2 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 2) )
    return FALSE ;

  if ( ! gs_lineto(TRUE, TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

Bool rlineto_(void)
{
  SYSTEMVALUE args[ 2 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 2) )
    return FALSE ;

  if ( ! gs_lineto(FALSE, TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            arc_()             author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 117.

---------------------------------------------------------------------------- */
Bool arc_(void)
{
  SYSTEMVALUE d_to_r ;
  SYSTEMVALUE args[ 5 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 5) )
    return FALSE ;

  d_to_r = DEG_TO_RAD ;
  args[ 3 ] *= d_to_r ;
  args[ 4 ] *= d_to_r ;

  if ( ! gs_arcto(ARCT_ARC|ARCT_LINETO, TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 5 , & operandstack ) ;

  return TRUE ;
}

Bool arcn_(void)
{
  SYSTEMVALUE d_to_r ;
  SYSTEMVALUE args[ 5 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 5) )
    return FALSE ;

  d_to_r = DEG_TO_RAD ;
  args[ 3 ] *= d_to_r ;
  args[ 4 ] *= d_to_r ;

  if ( ! gs_arcto(ARCT_ARCN|ARCT_LINETO, TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 5 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            arcto_()           author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 119.

---------------------------------------------------------------------------- */

Bool arct_(void)
{
  return arc_t_to(FALSE);
}

Bool arcto_(void)
{
  return arc_t_to(TRUE);
}

static Bool arc_t_to(int32 push_results)
{
  int32 flags;
  register SYSTEMVALUE dev_x, dev_y, cur_x , cur_y ;
  SYSTEMVALUE tangents[4] ;
  SYSTEMVALUE args[8] ;
  LINELIST *theline = CurrentPoint ;

  if ( ! theline )
    return error_handler( NOCURRENTPOINT ) ;

  if ( ! stack_get_numeric(&operandstack, args, 5) )
    return FALSE ;

  dev_x = theX( theIPoint( theline )) ;
  dev_y = theY( theIPoint( theline )) ;

  SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
  if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
    return error_handler( UNDEFINEDRESULT ) ;

  MATRIX_TRANSFORM_XY( dev_x, dev_y, cur_x, cur_y, & sinv ) ;

  if ( !arct_convert(cur_x, cur_y, args, (push_results ? tangents : NULL), &flags) ) {
    return(FALSE);
  }

  if ( (flags&(ARCT_ARC|ARCT_ARCN)) != 0 ) {
    /* Arct as a arc(n) - looks after lineto et al */
    if ( !gs_arcto(flags, TRUE, args, &(thePathInfo(*gstateptr))) ) {
      return(FALSE);
    }

  } else { /* Arct using single curveto - check for lineto and curveto */
    if ( (flags&ARCT_LINETO) != 0 ) {
      if ( !gs_lineto(TRUE, TRUE, args, &(thePathInfo(*gstateptr))) ) {
        return(FALSE);
      }
    }
    if ( (flags&ARCT_CURVETO) != 0 ) {
      if ( !gs_curveto(TRUE, TRUE, &args[2], &(thePathInfo(*gstateptr))) ) {
        return(FALSE);
      }
    }
  }

  npop(5 , &operandstack) ;

  if (push_results) {
    register int32 index ;
    for ( index = 0 ; index < 4 ; index++ )
      if ( ! stack_push_real(tangents[index], &operandstack) )
        return FALSE ;
  }

  return TRUE ;
}

/*
 * Convert arct coordinates into either a lineto/curveto combination, or into
 * an arc(n) depending on the AdobeArct use param
 * Function returns TRUE if arct setup ok, else FALSE.
 * x0 and y0 are the coords of the current point.
 * args as passed contains the coords of the two points defining the tangent
 * lines, and on exit contains the args for a lineto ([0] and [1]) followed by
 * a curveto ([2] thru [7]) or an arc(n) ([0] thru [5])
 * tangents on exit contains the coords of the two tangent points, only
 * if tangents is not NULL.
 * flags is a bit vector describing how to build the arct.
 */
Bool arct_convert(
  SYSTEMVALUE   x0,           /* I */
  SYSTEMVALUE   y0,           /* I */
  SYSTEMVALUE   args[8],      /* I/O */
  SYSTEMVALUE   tangents[4],  /* O */
  int32*        flags)        /* O */
{
  int32       action, fHqnArct;
  SYSTEMVALUE x1, y1, x2, y2;
  SYSTEMVALUE mx, my, xt1, yt1, xt2, yt2;
  SYSTEMVALUE ax, ay, bx, by, vx, vy;
  SYSTEMVALUE sinalpha, costheta, sinphi;
  SYSTEMVALUE ang1, ang2, phi, phitick;
  SYSTEMVALUE radius, temp, factor, sign;

  HQASSERT((args != NULL),
           "arct_convert: NULL pointer to arguments array");
  HQASSERT((flags != NULL),
           "arct_convert: NULL pointer to returned arct flags");

  /* Clear arct flags */
  action = 0;

  /* Extract tangent line projection points */
  x1 = args[0];
  y1 = args[1];
  x2 = args[2];
  y2 = args[3];

  if ( ((x0 == x1) && (y0 == y1)) || ((x1 == x2) && (y1 == y2)) ) {
    /* First point same as current point or second point same as first point - error */
    return(error_handler(UNDEFINEDRESULT));
  }

  /* Get unit vectors a and b for tangents to current point and P2 from P1 */
  ax = x0 - x1;
  ay = y0 - y1;
  temp = sqrt(ax*ax + ay*ay);
  ax /= temp;
  ay /= temp;

  bx = x2 - x1;
  by = y2 - y1;
  temp = sqrt(bx*bx + by*by);
  bx /= temp;
  by /= temp;

  /* Type of arct curve to produce */
  fHqnArct = !UserParams.AdobeArct;

  /* Calc sin of angle between tangent vectors at first point */
  sinphi = bx*ay - ax*by;

  if ( fabs(sinphi) < EPSILON ) {
    /* Tangent lines are collinear - just do a lineto to first tangent line point */
    action |= ARCT_LINETO;

    if ( fHqnArct ) {
      /* Clear angle and radius values for arc(n) version */
      args[2] = args[3] = args[4] = 0.0;
    }

    if ( tangents != NULL ) {
      /* Both tangent points are first tangent line point */
      tangents[0] = tangents[2] = x1;
      tangents[1] = tangents[3] = y1;
    }

  } else { /* Tangent lines are not collinear - need a curveto/arc(n) */

    /* Sign of some terms depends whether vectors a, v, & b are cw or ccw */
    sign = (sinphi < 0.0) ? -1.0 : 1.0;

    /* Get normalised vector bisecting vectors a and b */
    vx = ax + bx;
    vy = ay + by;
    temp = sqrt(vx*vx + vy*vy);
    vx /= temp;
    vy /= temp;

    /* Calc sin of angle between bisector and tangent line vectors
       Can only be in range (0,1) since angle is always between
       0 and 180 deg and cant be 90 as that gives a straight line
       which is caught above */
    sinalpha = sign*(vx*ay - ax*vy);
    HQASSERT((sinalpha != 0.0),
             "arct_to_curveto: about to divide by zero - aaahhh!");

    radius = args[4];

    /* Get centre point of arc */
    temp = radius/sinalpha;
    mx = x1 + vx*temp;
    my = y1 + vy*temp;

    /* Calc arct tangent points - t1 has vec a rot 90 deg cw, t2 has vec b rot 90 deg ccw */
    temp = sign*radius;
    xt1 = mx - temp*ay;
    yt1 = my + temp*ax;
    xt2 = mx + temp*by;
    yt2 = my - temp*bx;

    if ( fHqnArct ) {
      /* Calc arc(n) arguments - setup simple arc args - centre and radius */
      args[0] = mx;
      args[1] = my;
      args[2] = radius;

      /* Get angle for start of arc relative to centre */
      ang1 = myatan2((yt1 - my), (xt1 - mx));
      if ( ang1 < 0.0 ) {
        ang1 += 2*PI;
      }

      /* Get angle for end of arc. We know the angle between the tangent
         vectors (or at least its sin) but that is not enough since
         sin(x)= sin(x + 90) so we double the angle between the bisector
         and the tangent lines (guaranteed to be less than 90) to find
         the angle at the arc centre and adjust according to the arc
         direction */
      phi = 2.0*asin(sinalpha);
      phitick = PI - phi;

      if ( sinphi < 0.0 ) {
        action |= ARCT_ARCN;
        ang2 = ang1 - phitick;
      } else {
        action |= ARCT_ARC;
        ang2 = ang1 + phitick;
      }

      /* Setup final arc args */
      args[3] = ang1;
      args[4] = ang2;

    } else { /* Calc curveto args - calc circle factor */
      HQASSERT((sinalpha <= 1.0),
               "arct_convert: yuck - sinalpha > 1.0 when really not expected");
      costheta = sinalpha;
      factor = (4.0*costheta)/(3.0*(1.0 + costheta));

      /* Calc returned curveto control points and second tangent point as end point */
      args[2] = xt1 + factor*(x1 - xt1);
      args[3] = yt1 + factor*(y1 - yt1);
      args[4] = xt2 + factor*(x1 - xt2);
      args[5] = yt2 + factor*(y1 - yt2);
      args[6] = xt2;
      args[7] = yt2;

      action |= ARCT_CURVETO;
    }

    if ( !ISZERO(xt1 - x0) || !ISZERO(yt1 - y0) ) {
      /* Need an initial lineto */
      action |= ARCT_LINETO;

      if ( !fHqnArct ) {
        /* Explicit lineto first tangent point */
        args[0] = xt1;
        args[1] = yt1;
      }
    }

    if ( tangents != NULL ) {
      /* Return tangent points */
      tangents[0] = xt1;
      tangents[1] = yt1;
      tangents[2] = xt2;
      tangents[3] = yt2;
    }
  }

  HQASSERT_FPEXCEP("arct_convert FP exception");

  HQASSERT((action != 0),
           "arct_convert: no arct flags set");

  *flags = action;
  return(TRUE);

} /* Function arct_convert */


/* ----------------------------------------------------------------------------
   function:            curveto_()         author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 140.

---------------------------------------------------------------------------- */
Bool curveto_(void)
{
  SYSTEMVALUE args[ 6 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 6) )
    return FALSE ;

  if ( ! gs_curveto(TRUE, TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 6 , & operandstack ) ;

  return TRUE ;
}

Bool rcurveto_(void)
{
  SYSTEMVALUE args[ 6 ] ;

  if ( ! stack_get_numeric(&operandstack, args, 6) )
    return FALSE ;

  if ( ! gs_curveto(FALSE, TRUE, args, &thePathInfo(*gstateptr)) )
    return FALSE ;

  npop( 6 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            closepath_()       author:              Andrew Cave
   creation date:       03-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 130.

---------------------------------------------------------------------------- */
Bool closepath_(void)
{
  return path_close( CLOSEPATH, &thePathInfo(*gstateptr) ) ;
}


/* Log stripped */
