/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:htsort.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Quicksort for halftone cell parameters.
 */

#include "core.h"

#include "often.h"
#include "asyncps.h"
#include "progupdt.h"
#include "hpscreen.h"

static Bool htonepivot(register USERVALUE *spotvals ,
                       int32 number ,
                       USERVALUE *pivot );
static int32 htonepartition(USERVALUE *spotvals ,
                            CELLS **cells ,
                            int32 number ,
                            register USERVALUE pivot );

static int32 tholdpartition(uint8 *spotvals ,
                            int16 *xcoords ,
                            int16 *ycoords ,
                            int32 number ,
                            int32 pivot );

static int32 thold16partition(uint16 *spotvals ,
                              int16 *xcoords ,
                              int16 *ycoords ,
                              int32 number ,
                              int32 pivot );

/* ----------------------------------------------------------------------------
   function:            qsorthalftones(..)    author:              Andrew Cave
   creation date:       18-Jul-1989           last modification:   ##-###-####
   arguments:           spotvals , cells , number .
   description:

   Yet another quick sort - this time on the halftone values.

---------------------------------------------------------------------------- */
void qsorthalftones(USERVALUE *spotvals ,
                    CELLS **cells ,
                    int32 number )
{
  int32 k ;
  USERVALUE pivot = 0.0 ;

  HQASSERT( number > 1 , "what a waste of a call" ) ;

  if ( ! htonepivot( spotvals , number , & pivot ))
    return ;
  k = htonepartition( spotvals , cells , number , pivot ) ;
  if ( k > 1 )
    qsorthalftones( spotvals , cells , k ) ;
  if ( number - k > 1 )
    qsorthalftones( & spotvals[ k ] ,
                    & cells[ k ] ,
                    number - k ) ;
}

/* ----------------------------------------------------------------------------
   function:            htonepivot(..)        author:              Andrew Cave
   creation date:       18-Jul-1989           last modification:   ##-###-####
   arguments:           spotvals , number , pivot .
   description:

  This function calculates a seed value for the quicksort function. Returns
  TRUE if one was found, FALSE if the array is already sorted on its field.

---------------------------------------------------------------------------- */
static Bool htonepivot(register USERVALUE *spotvals ,
                       int32 number ,
                       USERVALUE *pivot )
{
  register int32 i ;
  register USERVALUE nextval ;
  register USERVALUE firstval ;

  firstval = spotvals[ 0 ] ;
  for ( i = ( number >> 1 ) + 1 ; i < number ; ++i ) {
    nextval = spotvals[ i ] ;
    if ( firstval != nextval ) {
      (*pivot) = (( firstval > nextval ) ? firstval : nextval ) ;
      return TRUE ;
    }
  }
  for ( i = number >> 1 ; i > 0 ; --i ) {
    nextval = spotvals[ i ] ;
    if ( firstval != nextval ) {
      (*pivot) = (( firstval > nextval ) ? firstval : nextval ) ;
      return TRUE ;
    }
  }
  return FALSE ;
}

/* ----------------------------------------------------------------------------
   function:            htonepartition(..)    author:              Andrew Cave
   creation date:       18-Jul-1989           last modification:   ##-###-####
   arguments:           spotvals , cells , number , pivot .
   description:

  This function partitions the array on the given field using pivot.

---------------------------------------------------------------------------- */
static int32 htonepartition(USERVALUE *spotvals ,
                            CELLS **cells ,
                            int32 number ,
                            register USERVALUE pivot )
{
  register USERVALUE *p , *q ;
  register CELLS **cp , **cq ;

  CELLS *cswap ;
  USERVALUE uswap ;

  SwOftenSafe ();

  p = spotvals ;
  q = p + ( number - 1 ) ;
  cp = cells ;
  cq = cp + ( number - 1 ) ;

  while ( p <= q ) {
    while ((*p) < pivot ) {
      ++p ;
      ++cp ;
    }
    while ((*q) >= pivot ) {
      --q ;
      --cq ;
    }
    if ( p < q ) {
      uswap = (*p)  ; (*p)  = (*q)  ; (*q)  = uswap ;
      cswap = (*cp) ; (*cp) = (*cq) ; (*cq) = cswap ;
      ++p ; ++cp ;
      --q ; --cq ;
    }
  }
  return (CAST_PTRDIFFT_TO_INT32(p - spotvals )) ;
}


/* ---------------------------------------------------------------------------- */
void qsortthreshold(uint8 *spotvals ,
                    int16 *xcoords ,
                    int16 *ycoords ,
                    int32 number ,
                    int32 pivot1 , int32 pivot2 )
{
  int32 k ;
  int32 pivot ;

  HQASSERT( pivot1 < pivot2 , "what a waste of a call" ) ;
  HQASSERT( number > 1      , "what a waste of a call too" ) ;

  pivot = ( pivot1 + pivot2 + 1 ) / 2 ;
  k = tholdpartition( spotvals , xcoords , ycoords , number , pivot ) ;
  if ( k > 1 &&
       pivot - 1 - pivot1 > 0 )
    qsortthreshold( spotvals ,
                    xcoords ,
                    ycoords ,
                    k ,
                    pivot1 , pivot - 1 ) ;
  if ( number - k > 1 &&
       pivot2 - pivot > 0 )
    qsortthreshold( & spotvals[ k ] ,
                    & xcoords[ k ] ,
                    & ycoords[ k ] ,
                    number - k ,
                    pivot , pivot2 ) ;
}

/* ---------------------------------------------------------------------------- */
static int32 tholdpartition(uint8 *spotvals ,
                            int16 *xcoords ,
                            int16 *ycoords ,
                            int32 number ,
                            int32 pivot )
{
  register uint8 *p , *q ;
  register int16 *cpx , *cqx ;
  register int16 *cpy , *cqy ;

  int16 cswap ;
  uint8 uswap ;

  SwOftenSafe ();

  p = spotvals ;
  q = p + ( number - 1 ) ;
  cpx = xcoords ;
  cqx = cpx + ( number - 1 ) ;
  cpy = ycoords ;
  cqy = cpy + ( number - 1 ) ;

  while ( p <= q ) {
    while ( p <= q &&
          ( int32 )(*p) < pivot ) {
        ++p ;
        ++cpx ;
        ++cpy ;
    }
    while ( p <= q &&
          ( int32 )(*q) >= pivot ) {
      --q ;
      --cqx ;
      --cqy ;
    }
    if ( p < q ) {
      uswap = (*p)   ; (*p)   = (*q)   ; (*q)   = uswap ;
      cswap = (*cpx) ; (*cpx) = (*cqx) ; (*cqx) = cswap ;
      cswap = (*cpy) ; (*cpy) = (*cqy) ; (*cqy) = cswap ;
      ++p ; ++cpx ; ++cpy ;
      --q ; --cqx ; --cqy ;
    }
  }
  return (CAST_PTRDIFFT_TO_INT32(p - spotvals)) ;
}


/* ---------------------------------------------------------------------------- */
void qsort16threshold(uint16 *spotvals ,
                      int16 *xcoords ,
                      int16 *ycoords ,
                      int32 number ,
                      int32 pivot1 , int32 pivot2 )
{
  int32 k ;
  int32 pivot ;

  HQASSERT( pivot1 < pivot2 , "what a waste of a call" ) ;
  HQASSERT( number > 1      , "what a waste of a call too" ) ;

  pivot = ( pivot1 + pivot2 + 1 ) / 2 ;
  k = thold16partition( spotvals , xcoords , ycoords , number , pivot ) ;
  if ( k > 1 &&
       pivot - 1 - pivot1 > 0 )
    qsort16threshold( spotvals ,
                      xcoords ,
                      ycoords ,
                      k ,
                      pivot1 , pivot - 1 ) ;
  if ( number - k > 1 &&
       pivot2 - pivot > 0 )
    qsort16threshold( & spotvals[ k ] ,
                      & xcoords[ k ] ,
                      & ycoords[ k ] ,
                      number - k ,
                      pivot , pivot2 ) ;
}

/* ---------------------------------------------------------------------------- */
static int32 thold16partition(uint16 *spotvals ,
                              int16 *xcoords ,
                              int16 *ycoords ,
                              int32 number ,
                              int32 pivot )
{
  register uint16 *p , *q ;
  register int16 *cpx , *cqx ;
  register int16 *cpy , *cqy ;

  int16 cswap ;
  uint16 uswap ;

  SwOftenSafe ();

  p = spotvals ;
  q = p + ( number - 1 ) ;
  cpx = xcoords ;
  cqx = cpx + ( number - 1 ) ;
  cpy = ycoords ;
  cqy = cpy + ( number - 1 ) ;

  while ( p <= q ) {
    while ( p <= q &&
            ( int32 )(*p) < pivot ) {
      ++p ;
      ++cpx ;
      ++cpy ;
    }
    while ( p <= q &&
            ( int32 )(*q) >= pivot ) {
      --q ;
      --cqx ;
      --cqy ;
    }
    if ( p < q ) {
      uswap = (*p)   ; (*p)   = (*q)   ; (*q)   = uswap ;
      cswap = (*cpx) ; (*cpx) = (*cqx) ; (*cqx) = cswap ;
      cswap = (*cpy) ; (*cpy) = (*cqy) ; (*cqy) = cswap ;
      ++p ; ++cpx ; ++cpy ;
      --q ; --cqx ; --cqy ;
    }
  }
  return (CAST_PTRDIFFT_TO_INT32(p - spotvals)) ;
}

/* Log stripped */
