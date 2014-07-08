/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:fbezier.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Reduce a Bezier curve to an appoximating set of line segments.
 */

#include "core.h"
#include "swoften.h"
#include "graphics.h"
#include "often.h"
#include "constant.h"
#include "gu_path.h"
#include "fbezier.h"

/*
 * The most straight-forward way to reduce a Bezier to a series of
 * approximating line segments is the "de Casteljau's" sub-division method.
 * This has a natural recursive implementation.
 * However, a non-recursive version can be faster, as it it does not have
 * the function call overhead and it is easier for the compiler to keep
 * variables in registers. So below is the obvious recursive implementation
 * ifdef'd out, followed by the optimised non-recursive version.
 */
#if 0

typedef struct
{
  void *data;
  int32 flags;
  Bool (*func)(FPOINT *, void *, int32);
} BC_INFO;

/**
 * Reduce a Bezier curve to an appoximating set of line segments using
 * "de Casteljau's" recursive sub-division method.
 * But ensure the details of the actual reduction algorithm used are
 * hidden from the external API, so alternate flattening methodologies can be
 * used without clients needing to change code.
 * \param[in] pnts Four bezier control points
 * \param[in] bc   Callback information
 * \param[in] mask Used to determine if we are in the first or last
 *                 approximating line segment.
 * \return         Success status
 */
static Bool r_bezchop(FPOINT pnts[4], BC_INFO *bc, int32 mask)
{
  Bool ok = TRUE;

  SwOftenUnsafe();

  if ( bc->flags & BEZ_BEZIERS )
  {
    int32 retval;

    retval = (*bc->func)(pnts, bc->data, BEZ_BEZIERS);
    if ( retval < 0 )
      return FALSE;
    else if ( retval == 0 )
      return TRUE;
    /* else drop through and do recursive dub-division */
  }

  if ( freduce( pnts ))
  {
    FPOINT ftmp1[4] , ftmp2[4];

    nextfbez( pnts , ftmp1 , ftmp2 );
    return r_bezchop(ftmp1, bc, mask|2) && r_bezchop(ftmp2, bc, mask|1);
  }
  else
  {
    if ( ( bc->flags & BEZ_CTRLS ) && ( ( mask & 1 ) == 0 ) )
      ok = ok & ((*bc->func)(&pnts[1], bc->data, BEZ_CTRLS ) > 0);
    if ( ( bc->flags & BEZ_CTRLS ) && ( ( mask & 2 ) == 0 ) )
      ok = ok & ((*bc->func)(&pnts[2], bc->data, BEZ_CTRLS ) > 0);

    if ( ( bc->flags & BEZ_POINTS ) )
      ok = ok & ((*bc->func)(&(pnts[3]), bc->data, BEZ_POINTS ) > 0);

    return ok;
  }
}

/**
 * Reduce a bezier curve to an approximating set of line segments, and then
 * call the supplied callback function with the end co-ord of each of these
 * line segments in turn.
 * \param[in] pnts  Four bezier control points
 * \param[in] func  Callback function called with each point on flattened path
 * \param[in] data  Opaque data pointer passed to callback function
 * \param[in] flags controls when callback function is called
 * \return          Success status
 */
Bool bezchop(FPOINT pnts[4], int32 (*func)(FPOINT *, void *, int32), void *data,
             int32 flags)
{
  BC_INFO bc_info;

  bc_info.func  = func;
  bc_info.data  = data;
  bc_info.flags = flags;

  return r_bezchop(pnts, &bc_info, 0);
}
#else

/**
 * Maximum Bezier recusion depth.
 * Using de Casteljau reduction, the ratio of deviations from the linear for
 * successive iterations tends asymptotically to a quarter. (The first few
 * iterations may deviate from this figure signicantly, but after that the
 * ratio approaches 0.25 very quickly.
 * So after N recursive calls, the deviation of a Bezier from a straight line
 * will have been reduced by aproximately a factor of (0.25)^N
 * So a Bezier with initial non-linearity of 1e+24 will have been reduced
 * to an error of less than a pixel after 40 iterations, so that seems like
 * an adequate upper limit (with an assert to catch any unexpected cases).
 */
#define MAX_BEZIER_RECURSE 40

/**
 * A structure representing a local stack of bezier co-ords, so we can run
 * the recursion locally.
 */
typedef struct
{
  FPOINT pts[4];
} BSTACK;

/**
 * Reduce a Bezier curve to an appoximating set of line segments using
 * "de Casteljau's" sub-division method. Implementation is non-recursive,
 * using its own local stack to manage recursion, for performance reasons.
 * \param[in] pnts  Four bezier control points
 * \param[in] func  Callback function called with each point on flattened path
 * \param[in] data  Opaque data pointer passed to callback function
 * \param[in] flags controls when callback function is called
 * \return          Success status
 */
Bool bezchop(FPOINT pnts[4], int32 (*func)(FPOINT *, void *, int32), void *data,
             int32 flags)
{
  BSTACK bstack[MAX_BEZIER_RECURSE];
  BSTACK *bez = &bstack[0];
  FPOINT p[4];
  int32 i;
  Bool first = TRUE;

  SwOftenUnsafe();

  for ( i = 0; i < 4 ; i++ )
    p[i] = pnts[i];

  for ( ; ; )
  {
    if ( flags & BEZ_BEZIERS )
    {
      int32 retval;

      retval = (*func)(p, data, BEZ_BEZIERS);
      if ( retval < 0 )
        return FALSE;
      else if ( retval == 0 )
        goto next_bez;
      /* else drop through and do recursive dub-division */
    }
    if ( freduce( p ))
    {
      HQASSERT(bez < &bstack[MAX_BEZIER_RECURSE],"bezchop recursion too deep");
      nextfbez(p, p, bez->pts);
      bez++;
    }
    else
    {
      if ( flags & BEZ_CTRLS )
      {
        if (  first )
          if ( !((*func)(&(p[1]), data, BEZ_CTRLS ) > 0) )
            return FALSE;
        first = FALSE;
        if ( bez == bstack )
          if ( !((*func)(&(p[2]), data, BEZ_CTRLS ) > 0) )
            return FALSE;
      }
      if ( !((*func)(&(p[3]), data, BEZ_POINTS ) > 0) )
        return FALSE;
      next_bez:
      if ( bez > bstack )
      {
        bez--;
        for ( i = 0; i < 4 ; i++ )
          p[i] = bez->pts[i];
      }
      else
        return TRUE;
    }
  }
}

#endif

/**
 * Evalute the Bezier cubic at the given parameter value.
 * \param[in]  pnts Four bezier control points
 * \param[in]  t    Evaluation parameter
 * \param[out] x    Returned x ordinate
 * \param[out] y    Returned y ordinate
 */
void bezeval(FPOINT pnts[4], SYSTEMVALUE t, SYSTEMVALUE *x, SYSTEMVALUE *y)
{
  SYSTEMVALUE s = 1.0 - t;

  *x = 1.0 * pnts[0].x * s*s*s +
       3.0 * pnts[1].x * s*s*t +
       3.0 * pnts[2].x * s*t*t +
       1.0 * pnts[3].x * t*t*t;

  *y = 1.0 * pnts[0].y * s*s*s +
       3.0 * pnts[1].y * s*s*t +
       3.0 * pnts[2].y * s*t*t +
       1.0 * pnts[3].y * t*t*t;
}

/*
 * freduce() tests to see if a bezier curve can be approximated by a straight
 * line. Returns TRUE if so, FALSE if the curve needs to be sub-divided.
 *
 * Consider a cubic Bezier curve with control points (P0, P1, P2, P3) given by
 *   P(t) = P0*B0(t) + P1*B1(t) + P2*B2(t) + P3*B3(t)
 * and a linear parameterisation of the line segment P0 -> P3, i.e.
 *   g(t) = t*P0 + (1-t)*P3
 * Then the distance of the bezier from the line is |P(t) - g(t)|, and the
 * algebra for this distance can be reduced to give :-
 *   P(t) - g(t) = t(1 - t)((1-t)U + tV), where
 *     U = 3*P1 - 2*P0 - P3, V = 3*P2 - P0 - 2*P3
 * We want a bound on |P(t) - g(t)| for 0 <= t <= 1, but we know
 *   t(1-t) < 1/4
 *   ((1-t)U + tV) < max(U, V)
 * So we get an upper bound on the square of the deviation of the bezier from
 * the straight line of
 *   d^2 <= (max(Ux^2,Vx^2) + max(Uy^2,Vy^2))/16
 * Comparing this with flatness^2 gives a termination test on Bezier reduction.
 */
int32 freduce(FPOINT pnts[4])
{
  SYSTEMVALUE ux, uy, vx, vy, m;

  ux = 3*pnts[1].x - 2*pnts[0].x - pnts[3].x; ux *= ux;
  uy = 3*pnts[1].y - 2*pnts[0].y - pnts[3].y; uy *= uy;
  vx = 3*pnts[2].x - pnts[0].x - 2*pnts[3].x; vx *= vx;
  vy = 3*pnts[2].y - pnts[0].y - 2*pnts[3].y; vy *= vy;

  if ( ux > vx )
    m = ux;
  else
    m = vx;

  if ( uy > vy )
    m += uy;
  else
    m += vy;

  return m > 16*fl_getftol();
}

/*
 * This routine sub-divides a bezier curve into two halves.
 *
 * Given a bezier with control points p0,p1,p2,p3, it can be chopped
 * into two beziers the first having control points :-
 *   p0, MID(p0+p1), MID(MID(p0+p1),MID(p1+p2)),
 *   MID(MID(MID(p0+p1),MID(p1+p2)),MID(MID(p1+p2),MID(p2+p3)))
 * where MID(x,y) is the mid-point of x and y, i.e. (x+y)/2
 * The 2nd half is calculated in a similar manner.
 *
 * N.B. The source and destination array of bezier points may be the same,
 * i.e. the bezier chop may be required in-situ for one of the two halves.
 */
void nextfbez(FPOINT pnts[4], FPOINT ftmp1[4], FPOINT ftmp2[4])
{
  SYSTEMVALUE t1, t2, t3, t4;

  t1 = ftmp1[0].x = pnts[0].x;
  t2 = pnts[1].x;
  ftmp1[1].x = t1 = ( t1 + t2 ) * 0.5;

  t4 = ftmp2[3].x = pnts[3].x;
  t3 = pnts[2].x;
  ftmp2[2].x = t4 = ( t3 + t4 ) * 0.5;

  t2 = ( t2 + t3 ) * 0.5;
  ftmp1[2].x = t1 = ( t1 + t2 ) * 0.5;
  ftmp2[1].x = t2 = ( t2 + t4 ) * 0.5;

  ftmp1[3].x = ftmp2[0].x = ( t1 + t2 ) * 0.5;

  t1 = ftmp1[0].y = pnts[0].y;
  t2 = pnts[1].y;
  ftmp1[1].y = t1 = ( t1 + t2 ) * 0.5;

  t4 = ftmp2[3].y = pnts[3].y;
  t3 = pnts[2].y;
  ftmp2[2].y = t4 = ( t3 + t4 ) * 0.5;

  t2 = ( t2 + t3 ) * 0.5;
  ftmp1[2].y = t1 = ( t1 + t2 ) * 0.5;
  ftmp2[1].y = t2 = ( t2 + t4 ) * 0.5;

  ftmp1[3].y = ftmp2[0].y = ( t1 + t2 ) * 0.5;
}


/* Log stripped */
