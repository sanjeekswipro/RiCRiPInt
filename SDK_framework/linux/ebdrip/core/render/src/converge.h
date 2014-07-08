/** \file
 * \ingroup halftone
 *
 * $HopeName: CORErender!src:converge.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone convergence functions and macros.
 */

#ifndef __CONVERGE_H__
#define __CONVERGE_H__

#include "htrender.h" /* ht_params_t */
#include "bitbltt.h" /* blit_t */


/* This file contains macro versions of some of the converge routines from
   converge.c which are optimised to make use of some locality in specific
   cases. */

/* Macros prefixed by L do NOT update the (ht_params->cx, ht_params->cy)
 * cache and may also use local variable versions of ht_params->cx and
 * previous cy values for speed. */

/** General convergence to determine cx, cy for the absolute position x, y
    optimised for locality with respect to the last converged position/ */
#define FINDSGNBITS(ht_params, _cx, _cy, x, y) MACRO_START \
  if ( (_cx = (x) - (ht_params)->cx) < 0 || (_cx) >= (ht_params)->xdims || \
       (_cy = (y) - (ht_params)->cy) < 0 || (_cy) >= (ht_params)->ydims ) { \
    /* use local variables to allow _cx, _cy to be allocated in registers */ \
    int32 lcx, lcy; \
    /* cast away the constness of ht_params, to update cx, cy */ \
    findsgnbits((ht_params_t *)ht_params, &lcx, &lcy, x, y); \
    _cx = lcx; _cy = lcy; \
  } \
MACRO_END

/** Convergence for general halftone cells, where cy is known to be within
    range and cx has only been increased since it was last known to be in
    range.

    We have stepped over the right-hand edge of the halftone cell by an
    arbitrary amount cx and cy are the desired offsets relative to the last
    known halftone cell position: cy is already in range, converge until cx
    is in range. */
#define LFINDSGNBITSX(cx, cy) MACRO_START \
  while (cx >= halfxdims) {               \
    if (cy >= halfr2) {                   \
      cy -= halfr2;                       \
      cx -= halfr1;                       \
    }                                     \
    else {                                \
      cy += halfr3;                       \
      cx -= halfr4;                       \
    }                                     \
  }                                       \
  while ( cy >= halfydims )               \
    if ( cx >= halfr1 ) {                 \
      cx -= halfr1 ;                      \
      cy -= halfr2 ;                      \
    }                                     \
    else {                                \
      cx += halfr4 ;                      \
      cy -= halfr3 ;                      \
    }                                     \
MACRO_END

/** Convergence for general halftone cells, where cy is known to have stepped
    exactly one line below the halftone cell with the previous, correct cx
    supplied and only one step is required to reposition.

    We have stepped over the bottom edge of the halftone cell by one scan
    line cx and cy are the desired offsets relative to the last known
    halftone cell position.

    cx is already in range
    cy is undefined, but assumed to be == halfydims

    Converge by one step to put cy in range and adjust cx to be in range. */
#define LFINDSGNBITSY1(cx, cy) MACRO_START \
  if (cx >= halfr1) {                      \
    cx -= halfr1;                          \
    cy  = halfydims - halfr2;              \
  }                                        \
  else {                                   \
    cx += halfr4;                          \
    cy  = halfydims - halfr3;              \
  }                                        \
MACRO_END

/** Convergence for general halftone cells, where hccy is known to be within
    range and x has only been increased since it was last known to be in
    range.

    cx and cy are un-defined.

    We have stepped over the right-hand edge of the halftone cell by an
    arbitrary x, hcx and hccy define the position relative to the last known
    halftone cell position:

    hccy is already in range, converge until x is in range.

    hcx contains the current cell's cx value, halfypos contains the offset in
    bytes from the top of the halftone cell to the current line given by
    hccy.

    halfypos is updated if cy is changed. */
#define LFINDSGNBITSXHP(hcx, hccy, cx, cy, x, halfypos) MACRO_START \
  cy = (hccy);                                                      \
  HQASSERT(cy >= 0 && cy < halfydims, "Y convergence initially out of range") ;   \
  if ((cx = (x) - (hcx)) >= halfxdims) {                            \
    do {                                                            \
      if ((cy) >= halfr2) {                                         \
        cy -= halfr2;                                               \
        cx -= halfr1;                                               \
      }                                                             \
      else {                                                        \
        cy += halfr3;                                               \
        cx -= halfr4;                                               \
      }                                                             \
    } while ((cx) >= halfxdims);                                    \
    if ( (cy) >= halfydims ) {                                      \
      if ( cx >= halfr1 ) {                                         \
        cx -= halfr1 ;                                              \
        cy -= halfr2 ;                                              \
      }                                                             \
      else {                                                        \
        cx += halfr4 ;                                              \
        cy -= halfr3 ;                                              \
      }                                                             \
    }                                                                   \
    HQASSERT(cx >= 0 && cx < halfxdims, "X convergence out of range") ; \
    HQASSERT(cy >= 0 && cy < halfydims, "Y convergence out of range") ; \
    hccy = (cy);                                                    \
    hcx = (x) - (cx);                                               \
    halfypos = halfys[cy];                                          \
  }                                                                 \
MACRO_END


/** Convergence for general halftone cells, where hccy is known to be within
    range and x may have increased or decreased since it was last known to be in
    range.

    cx and cy are un-defined.

    We have stepped over the left or right-hand edge of the halftone cell by an
    arbitrary x, hcx and hccy define the position relative to the last known
    halftone cell position:

    hccy is already in range, converge until x is in range.

    hcx contains the current cell's cx value, halfypos contains the offset in
    bytes from the top of the halftone cell to the current line given by
    hccy.

    halfypos is updated if cy is changed. */
#define LFINDSGNBITSXHPN(hcx, hccy, cx, cy, x, halfypos) MACRO_START    \
  cy = (hccy);                                                          \
  HQASSERT(cy >= 0 && cy < halfydims, "Y convergence initially out of range") ;   \
  cx = (x) - (hcx);                                                     \
  if ( cx < 0 || cx  >= halfxdims) {                                    \
    while ( cx < 0 )                                                    \
      cx += halfexdims ;                                                \
    while ((cx) >= halfxdims) {                                         \
      if ((cy) >= halfr2) {                                             \
        cy -= halfr2;                                                   \
        cx -= halfr1;                                                   \
      }                                                                 \
      else {                                                            \
        cy += halfr3;                                                   \
        cx -= halfr4;                                                   \
      }                                                                 \
    }                                                                   \
    if ((cy) >= halfydims) {                                            \
      if ( cx >= halfr1 ) {                                             \
        cx -= halfr1 ;                                                  \
        cy -= halfr2 ;                                                  \
      }                                                                 \
      else {                                                            \
        cx += halfr4 ;                                                  \
        cy -= halfr3 ;                                                  \
      }                                                                 \
    }                                                                   \
    HQASSERT(cx >= 0 && cx < halfxdims, "X convergence out of range") ; \
    HQASSERT(cy >= 0 && cy < halfydims, "Y convergence out of range") ; \
    hccy = (cy);                                                        \
    hcx = (x) - (cx);                                                   \
    halfypos = halfys[cy];                                              \
  }                                                                     \
MACRO_END

/** Small corrections to converge general halftone cells, where hccx is known
    to be within range and y may have increased or decreased since it was
    last known to be in range.

    cx and cy are un-defined.

    We have stepped over the bottom or top-hand edge of the halftone cell by an
    arbitrary y, hcy and hccx define the position relative to the last known
    halftone cell position:

    hccx is already in range, converge until y is in range.

    hcy contains the current cell's cy value, halfypos contains the offset in
    bytes from the top of the halftone cell to the current line given by
    hccy.

    halfypos is updated if cy is changed. */
#define LFINDSGNBITSYHPN(hccx, hcy, cx, cy, y, halfypos) MACRO_START    \
  cx = (hccx);                                                          \
  HQASSERT(cx >= 0 && cx < halfxdims, "X convergence initially out of range") ;   \
  cy = (y) - (hcy) ;                                                    \
  if ( cy < 0 || cy >= halfydims) {                                     \
    while ( cy < 0 )                                                    \
      cy += halfeydims ;                                                \
    while ((cy) >= halfydims) {                                         \
      if ( cx >= halfr1 ) {                                             \
        cx -= halfr1 ;                                                  \
        cy -= halfr2 ;                                                  \
      }                                                                 \
      else {                                                            \
        cx += halfr4 ;                                                  \
        cy -= halfr3 ;                                                  \
      }                                                                 \
    }                                                                   \
    HQASSERT(cx >= 0 && cx < halfxdims, "X convergence out of range") ; \
    HQASSERT(cy >= 0 && cy < halfydims, "Y convergence out of range") ; \
    hccx = (cx);                                                        \
    hcy = (y) - (cy);                                                   \
  }                                                                     \
  halfypos = halfys[cy];                                                \
MACRO_END


/* external functions */

void moreonbitsptr(blit_t *res, const ht_params_t *ht_params,
                   dcoord x, dcoord y, dcoord n);
void findgnbits(ht_params_t *ht_params,
                int32 *bxpos, int32 *bypos, int32 x , int32 y);
void moregnbitsptr(blit_t *res, ht_params_t *ht_params,
                   dcoord x, dcoord y, dcoord n);
void findsgnbits(ht_params_t *ht_params,
                 int32 *bxpos, int32 *bypos, int32 x, int32 y);
void moresgnbitsptr(blit_t *res, ht_params_t *ht_params,
                    dcoord x, dcoord y, dcoord n);

#endif  /* !__CONVERGE_H__ */


/* Log stripped */
