/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:ndisplay.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Scan-conversion pixel conversion defines
 */

#ifndef __NDISPLAY_H__
#define __NDISPLAY_H__


#include "dl_storet.h"

        /* Fractional precision of a device unit that we use for rendering */
#define SC_DEVICE_UNIT                  0

        /* Offset added to device pixels before we convert to an integer */
#define SC_PIXEL_OFFSET                 0.0

        /* Rounding added to device pixels before we convert to an integer */
#define SC_PIXEL_ROUND                  ( 1.0 / 16384.0 )

        /* Method used to round real pixels to fixed point integer pixels */
        /* Used when we need the accuracy, for example in rotated images, */
        /* strokes, fills and characters */

/*
 * The scan conversion code attempts to convert floating-point co-ords
 * into a fixed format, clipping the values if they exceed the limit of the
 * fixed format using the SC_C2D_UNT_I macro below. However, the subsequent
 * code isn't designed to deal with such huge co-ords values, and asserts and
 * crashes in a number of different ways. So add a safety-net here, adding a
 * macro to test for such huge values, allowing a limitcheck to be thrown if
 * such huge numbers are seen (not correct, but better than crashing ).
 * Exact mathematical analysis is needed to find the precise limits, so just
 * enforce the arbitrary upper limit of the fixed format for now, until a
 * better solution can be found.
 * Likely to be time critical, so make test a macro
 */
/** \todo @@@ TODO FIXME bmj 2006-11-02: This is a temporary? hack, code should
    deal properly with huge co-ords via intelligent pre-clipping **/

#define HUGE_COORD ( ( SYSTEMVALUE )0x3FFFFFFF )
#define TEST4HUGE(_v) ( (_v) > HUGE_COORD || (_v) < -HUGE_COORD )


#ifdef FP_TO_INT_OVERFLOW
/* The following code "helps" with removing integer overflow problems
 * when stroking or filling lines including silly numbers in them.
 * The Mac version is more optimised since on that platform we know
 * that fp to int overflow always produces MAXINT.
 */
#ifdef MACINTOSH

/* Equivalent to floor */
#define SC_C2D_UNT_I(_r,_xy,_rnd) MACRO_START   \
  dcoord _ti_ ;                                  \
  SYSTEMVALUE _td_ ;                            \
  _td_ = (_xy) + (_rnd) ;                       \
  _ti_ = ( dcoord )( _td_ ) ;                    \
  if (( _ti_ | ( _ti_ << 1 )) <= 0 ) {          \
    /* Check for overflow. */                   \
    if ( _td_ < ( SYSTEMVALUE )0xC0000000 )     \
      _ti_ = 0xC0000000 ;                       \
    else if ( _td_ > ( SYSTEMVALUE )0x3FFFFFFF )\
      _ti_ = 0x3FFFFFFF ;                       \
    /* Check for negative floor. */             \
    else if ( _td_ < 0.0 )                      \
      _ti_ = _ti_ - 1 ;                         \
  }                                             \
  HQASSERT((_ti_ <= 0 && _td_ <= 0.0) || (_ti_ >= 0 && _td_ >= 0.0), \
           "Coordinate rounding not consistent") ; \
  _r = _ti_ ;                                   \
MACRO_END

#else /* FP_TO_INT_OVERFLOW && !MACINTOSH */

/* Equivalent to floor */
#define SC_C2D_UNT_I(_r,_xy,_rnd) MACRO_START   \
  dcoord _ti_ ;                                  \
  SYSTEMVALUE _td_ ;                            \
  _td_ = (_xy) + (_rnd) ;                       \
  /* Check for overflow. */                     \
  if ( _td_ < ( SYSTEMVALUE )((dcoord)0xC0000000) ) \
    _ti_ = 0xC0000000 ;                         \
  else if ( _td_ > ( SYSTEMVALUE )0x3FFFFFFF )  \
    _ti_ = 0x3FFFFFFF ;                         \
  else {                                        \
    _ti_ = ( dcoord )( _td_ ) ;                  \
    /* Check for negative floor. */             \
    if ( _ti_ <= 0 && _td_ < _ti_ )             \
      _ti_ = _ti_ - 1 ;                         \
  }                                             \
  HQASSERT((_ti_ <= 0 && _td_ <= 0.0) || (_ti_ >= 0 && _td_ >= 0.0), \
           "Coordinate rounding not consistent") ; \
  _r = _ti_ ;                                   \
MACRO_END

#endif /* !MACINTOSH */

#elif !FLOAT_TO_INT_IS_SLOW /* && !FP_TO_INT_OVERFLOW */

/* Equivalent to floor */
#define SC_C2D_UNT_I(_r,_xy,_rnd) MACRO_START   \
  register dcoord _ti_ ;                        \
  register SYSTEMVALUE _td_ ;                   \
  _td_ = (_xy) + (_rnd) ;                       \
  _ti_ = ( dcoord )( _td_ ) ;                   \
  /* Check for negative floor. */               \
  if ( _td_ < _ti_ )                            \
    _ti_ -= 1 ;                                 \
  _r = _ti_ ;                                   \
MACRO_END

#else /* FP_TO_INT_IS_SLOW  && !FP_TO_INT_OVERFLOW */

#ifdef highbytefirst
#define EXPONENT_INDEX 0
#define MANTISSA_INDEX 1
#else
#define EXPONENT_INDEX 1
#define MANTISSA_INDEX 0
#endif

/* Equivalent to floor */
#define SC_C2D_UNT_I(_r,_xy,_rnd) MACRO_START   \
  register dcoord _ti_ ;                        \
  register SYSTEMVALUE _td_ = (_xy) + (_rnd) ;  \
  union { SYSTEMVALUE sv; dcoord c[2]; } _tm_; \
  _tm_.sv = _td_ + 6755399441055744.0 /* (1 << 52) + (1 << 51) */; \
  _ti_ = _tm_.c[MANTISSA_INDEX] ; /* absolute value of double */ \
  if ( _td_ < _ti_ ) /* Convert negative value truncation to floor */ \
    _ti_ -= 1 ;                                 \
  HQASSERT(_ti_ == (dcoord)floor(_td_),         \
           "Fast double to device coord conversion failed") ; \
  (_r) = _ti_ ;                                 \
MACRO_END

#endif /* FP_TO_INT_IS_SLOW && !FP_TO_INT_OVERFLOW */

#define SC_C2D_UNT(_r,_xy)      SC_C2D_UNT_I(_r,_xy,SC_PIXEL_ROUND)

#define SC_C2D_UNTF_I(_r,_xy,_rnd) MACRO_START  \
  dcoord _tr_ ;                                  \
  SC_C2D_UNT_I(_tr_,_xy,_rnd) ;                 \
  _r = ( SYSTEMVALUE )_tr_ ;                    \
MACRO_END

#define SC_C2D_UNTF(_r,_xy)     SC_C2D_UNTF_I(_r,_xy,SC_PIXEL_ROUND)

        /* Method used to round real pixels to exact integer pixels    */
        /* Used when we don't need to store any precision (for example */
        /* in orthogonal images, rectangles, i.e. in scan converting   */
        /* objects that are exactly aligned with the device axis.      */

#define SC_C2D_INT_I(_r,_xy,_rnd)       SC_C2D_UNT_I(_r,_xy,_rnd)

#define SC_C2D_INT(_r,_xy)      SC_C2D_UNT_I(_r,_xy,SC_PIXEL_ROUND)

#define SC_C2D_INTF_I(_r,_xy,_rnd) MACRO_START  \
  dcoord _tr_ ;                                  \
  SC_C2D_INT_I(_tr_,_xy,_rnd) ;                 \
  _r = ( SYSTEMVALUE )_tr_ ;                    \
MACRO_END

#define SC_C2D_INTF(_r,_xy)     SC_C2D_INTF_I(_r,_xy,SC_PIXEL_ROUND)

        /* Method used for rounding a delta to it's nearest integer    */
#define SC_RINT(_r,_xy)         MACRO_START     \
  SYSTEMVALUE _td_ ;                            \
  _td_ = (_xy) ;                                \
  if ( _td_ >= 0.0 )                            \
    _r = ( dcoord )( _td_ + 0.5 ) ;              \
  else                                          \
    _r = ( dcoord )( _td_ - 0.5 ) ;              \
MACRO_END

#define SC_RINTF(_r,_xy)        MACRO_START     \
  dcoord _tr_ ;                                  \
  SC_RINT(_tr_,_xy) ;                           \
  _r = ( SYSTEMVALUE )_tr_ ;                    \
MACRO_END

#if SC_DEVICE_UNIT == 0
#define SC_CONVERT_DEVICE_UNIT_TO_INTS(_xy) \
                                        (_xy)
#else
#define SC_CONVERT_DEVICE_UNIT_TO_INTS(_xy) \
                                        (((_xy)+(1<<(SC_DEVICE_UNIT-1)))>>SC_DEVICE_UNIT)
#endif

/**
 * Continuation deltas from final point of first segment held as a
 * variable length list.
 */
typedef struct DXYLIST
{
  uint8 ndeltas;   /**< Number of deltas stored in the list */
  uint8 deltai;    /**< Current index into the delta list */
  uint8 deltabits; /**< How many bits used to store each delta 4/8/16 */
  uint8 spare;     /**< Not used as yet ... */
  int32 delta[1];  /**< Actually variable type (see deltabits) and variable
                        length (see ndeltas) array of dx,dy pairs */
} DXYLIST;

typedef struct NBRESS {
  /* Most of the fields for the DDA scan converter are referred to directly,
     rather than being part of some grotty union. The pre-initialisation code
     overloads ncx and norient with a pointer to the next nbress in the current
     nfill and a flag to determine if the thread started a new subpath. */
  union {
    dcoord ncx ;          /**< segment current X, segment final X */
    struct NBRESS *next ; /**< previous NBRESS during construction */
  } u1 ;
  dcoord xe ;      /**< current X integral, fractional parts, 0 <= xe < denom */
  dcoord si, sf ;  /**< gradient integral, fractional parts 0 <= sf < denom */
  dcoord denom ;   /**< denominator of DDA (2 * dy), 0 for horizontal */

  int8 norient ;   /**< thread direction */
  uint8 ntype ;    /**< segment dy >= |dx| */
  int16 flags ;    /**< Preset to 0; use this for whatever you want */

  dcoord nx1 , ny1 ; /**< Initial point of first segment */
  dcoord nx2 , ny2 ; /**< Final point of first segment */
  /* We can detect the first and last lines by looking at nmindy, for filling
     half-spans.

     First line is when nmindy == denom
     Last line is when nmindy < 2.

     nmindy is decremented twice per scanline. */
  dcoord nmindy ;    /**< Twice the remaining lines in current segment */

  DXYLIST dxy; /**< List of continuation co-ords in dx,dy format */
} NBRESS ;

struct RCBTRAP ;

/** Scan conversion rules to use for NFILLOBJECTs. Only the first four rules
    can be represented using RENDER_quad type. Fills using the other rules
    will remain as nfill. If you change the order of this array, change the
    order of the dispatch functions in CORErender!src:scband.c too. */
enum {
  SC_RULE_HARLEQUIN,   /**< Old Harlequin Bresenham scan conversion. */
  SC_RULE_TOUCHING,    /**< Pixel touching (aka Adobe) scan conversion. */
  SC_RULE_TESSELATE,   /**< Half-open (aka XPS/non-overlap) scan conversion. */
  SC_RULE_BRESENHAM,   /**< Bresenham scan conversion inward rounding. */
  SC_RULE_CENTER,      /**< Pixel centre exclusion. */
  SC_RULE_N            /**< Number of scan convert rules; must be last. */
} ;

/** Number of fractional bits to allocate for tesselating sub-pixel
    renderer. */
#define SC_SUBPIXEL_BITS 6
#define SC_SUBPIXEL_MASK ((1 << SC_SUBPIXEL_BITS) - 1)

/** Is a coordinate safe for translation to sub-pixels? */
#define SC_SUBPIXEL_SAFE(x) \
  ((x) == BIT_SHIFT32_SIGNED_RIGHT_EXPR((x) << SC_SUBPIXEL_BITS, SC_SUBPIXEL_BITS))

/**
 * How much space for threads in an NFILL object when it is declared on
 * the stack.  When it is allocated in memory, it will have the size adjusted
 * to match the required 'nthreads'.
 */
#define ST_NTHREADS 8

/**
 * Vector object as stored in the display list.
 * Also contains state updated incrementally as it is rendered.
 */
typedef struct NFILLOBJECT
{
  int32 type;                  /**< type of nfill object, NZFILL_TYPE etc. */
  dcoord nexty;                /**< y-coordinate of current updated state */
  dcoord y1clip;               /**< first y clip scanline */
  struct RCBTRAP *rcbtrap;     /**< recombine stuff */
  uint8  converter;            /**< Scan converter to use, from enum above. */
  uint8  clippedout;           /**< status of top/left/bottom/right clipping */
  uint8  waste2, waste3;       /**< Use me first */

  int32  nthreads;             /**< count of number of threads */
  NBRESS **startnptr;          /**< first active thread in list */
  NBRESS **endnptr;            /**< last active thread in list */
  NBRESS *thread[ST_NTHREADS]; /**< Actually list of length 'nthreads' */
} NFILLOBJECT;

typedef struct {
  DlSSEntry storeEntry; /* Base class - must be first member. */
  NFILLOBJECT *nfill ;
} NFILLCACHE ;

/* Utility functions for accessing DXYLISTs */
void dxylist_init(DXYLIST *dxy);
void dxylist_reset(DXYLIST *dxy);
Bool dxylist_get(DXYLIST *dxy, dcoord *px, dcoord *py);
void dxylist_store(DXYLIST *dxy, dcoord dx, dcoord dy);
Bool dxylist_equal(DXYLIST *dxy1, DXYLIST *dxy2);
Bool dxylist_empty(DXYLIST *dxy);
Bool dxylist_full(DXYLIST *dxy);
Bool dxylist_toobig(dcoord dx, dcoord dy);

#define NORIENTDOWN      1      /* thread drawn top-to-bottom, i.e. +devY */
#define NORIENTUP       -1      /* thread drawn bottom-to-top, i.e. -devY */

/* Revolting macro to compare two threads for X sort order. Compare X
   coordinates, then call function for X error and gradient if X is the
   same. */
#define COMPARE_THREADS_X(t1_, t2_, op_) \
  ((t1_)->u1.ncx op_ (t2_)->u1.ncx ||      \
   ((t1_)->u1.ncx == (t2_)->u1.ncx &&      \
    compare_nbress_xes((t1_), (t2_)) op_ 0))

#define COMPARE_THREADS_Y(t1_, t2_, op_) \
  ((t1_)->ny1 op_ (t2_)->ny1 || \
   ((t1_)->ny1 == (t2_)->ny1 && \
    COMPARE_THREADS_X(t1_, t2_, op_)))

/* Sorting functions used by DDA fill routines. */
int32 compare_nbress_xes(register NBRESS *t1, register NBRESS *t2) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
