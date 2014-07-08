/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:htblits.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone form blitting functions.
 */

#include "core.h"

#include "bitblts.h"
#include "params.h"
#include "often.h"
#include "htrender.h" /* GET_FORM */

#ifdef bitsgoright
#define AONE_MULTI(depth) (AONE >> ((depth) - 1))
#else
#define AONE_MULTI(depth) ((blit_t)1)
#endif

#define SETBIT( x, y, formptr, ys, depth ) \
  (*BLIT_ADDRESS(formptr, (ys)[y] + BLIT_OFFSET(x)) += \
    SHIFTRIGHT(AONE_MULTI(depth), (x) & BLIT_MASK_BITS));

#define CLRBIT( x, y, formptr, ys, depth ) \
  (*BLIT_ADDRESS(formptr, (ys)[y] + BLIT_OFFSET(x)) -= \
    SHIFTRIGHT(AONE_MULTI(depth), (x) & BLIT_MASK_BITS));

#define CHECKSETBIT( x, y, formptr, w, h, ys, depth ) \
  if (( x >= 0 ) && ( y >= 0 ) && ( x < w ) && ( y < h ))               \
    SETBIT( x, y, wordptr, ys, depth );

#define CHECKCLRBIT( x, y, formptr, w, h, ys, depth ) \
  if (( x >= 0 ) && ( y >= 0 ) && ( x < w ) && ( y < h ))               \
    CLRBIT( x, y, wordptr, ys, depth );

#define CHECKSET_ALLBITS( x, y, formptr, w, h, ys, depth, dx, dy ) \
  while ( (x >= 0) && (y >=0) && (x < w) && (y < h) ) {                 \
    SETBIT( x, y, wordptr, ys, depth ); \
    x += dx;                                                            \
    y += dy ;                                                           \
  }

#define CHECKCLR_ALLBITS( x, y, formptr, w, h, ys, depth, dx, dy ) \
  while ( (x >= 0) && (y >=0) && (x < w) && (y < h) ) {                 \
    CLRBIT( x, y, wordptr, ys, depth ); \
    x += dx;                                                            \
    y += dy ;                                                           \
  }


/* ----------------------------------------------------------------------------
   function:            alignbits(..)      author:              Andrew Cave
   creation date:       24-May-1987        last modification:   ##-###-####
   arguments:           x , y .
   description:

   This procedure pads out a form to fill a blit_t (normally 32 or 64 bits).

---------------------------------------------------------------------------- */
static blit_t alignbits(const ht_params_t *ht_params,
                        register int32 cx, register int32 cy,
                        register blit_t *baseptr)
{
  register int32 ar1 , ar2 ;
  register int32 ar3 , ar4 ;
  register int32 t1 , t2 ;

  register blit_t *bitptr ;

  int32 needword ;
  blit_t bits ;
  int32 halfxdims = ht_params->xdims, halfydims = ht_params->ydims;

  SwOftenUnsafe() ;

  ar1 = ht_params->r1 ;
  ar2 = ht_params->r2 ;
  ar3 = ht_params->r3 ;
  ar4 = ht_params->r4 ;

  bits = 0 ;
  needword = 0 ;
  while ( needword < BLIT_WIDTH_BITS ) {
    /* Firstly, correct x. */
    while ( cx >= halfxdims )
      if ( cy >= ar2 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      } else {
        cx -= ar4 ;
        cy += ar3 ;  /* --cx , ++cy */
      }
    /* Secondly, correct y. */
    while ( cy >= halfydims )
      if ( cx >= ar1 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      } else {
        cx += ar4 ;
        cy -= ar3 ;  /* --cx , ++cy */
      }

    /* Have got position, so extract the appropriate bits. */
    t2 = cx ;
    t1 = ( halfxdims - t2 ) ;  /* Number of bits available from form. */
    bitptr = BLIT_ADDRESS(baseptr, ht_params->ys[cy] + BLIT_OFFSET(t2)) ;
    t2 = t2 & BLIT_MASK_BITS ;
    bits |= SHIFTRIGHT(SHIFTLEFT(*bitptr, t2), needword) ;
    t2 = ( BLIT_WIDTH_BITS - t2 ) ;
    if ( t1 > t2 )
      t1 = t2 ;
    needword += t1 ;
    if ( needword >= BLIT_WIDTH_BITS )
      t1 -= ( needword - BLIT_WIDTH_BITS ) ;
    cx += t1 ;
  }
  return bits ;
}

/* ----------------------------------------------------------------------------
   function:            padbits(..)       author:              Andrew Cave
   creation date:       24-May-1987       last modification:   ##-###-####
   arguments:           x , y .
   description:

   This procedure obtains the next n bit mask used in the halftoning.
   Version for general (non-orthogonal) screens using a cached FORM.

---------------------------------------------------------------------------- */
static void padbits(const ht_params_t *ht_params,
                    int32 x, int32 y, int32 n,
                    register blit_t *dstptr, register blit_t *baseptr)
{
  register int32 cx , cy ;
  register int32 ar1 , ar2 ;
  register int32 ar3 , ar4 ;
  register int32 t2 ;

  int32 needword ;
  blit_t bits ;
  blit_t *bitptr ;
  int32 halfxdims = ht_params->xdims, halfydims = ht_params->ydims;
  int32 *halfys = ht_params->ys;

  SwOftenUnsafe() ;

  cx = x ;
  cy = y ;

  ar1 = ht_params->r1 ;
  ar2 = ht_params->r2 ;
  ar3 = ht_params->r3 ;
  ar4 = ht_params->r4 ;

  while ((--n) >= 0 ) {
/* Firstly, correct x. */
    while ( cx >= halfxdims )
      if ( cy >= ar2 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      } else {
        cx -= ar4 ;
        cy += ar3 ;  /* --cx , ++cy */
      }
/* Secondly, correct y. */
    while ( cy >= halfydims )
      if ( cx >= ar1 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      } else {
        cx += ar4 ;
        cy -= ar3 ;  /* --cx , ++cy */
      }
/* Start off with correct position, so extract the first lot of bits. */
    bits = *BLIT_ADDRESS(baseptr, halfys[cy] + BLIT_OFFSET(cx)) ;
    t2 = cx & BLIT_MASK_BITS ;
    bits = SHIFTLEFT( bits , t2 ) ;
    t2 = ( BLIT_WIDTH_BITS - t2 ) ;
    needword = t2 ;
    cx += t2 ;

    while ( needword < BLIT_WIDTH_BITS ) {
/* Firstly, correct x. */
      while ( cx >= halfxdims )
        if ( cy >= ar2 ) {
          cx -= ar1 ;
          cy -= ar2 ;  /* ++cx , ++cy */
        } else {
          cx -= ar4 ;
          cy += ar3 ;  /* --cx , ++cy */
        }
/* Secondly, correct y. */
      while ( cy >= halfydims )
        if ( cx >= ar1 ) {
          cx -= ar1 ;
          cy -= ar2 ;  /* ++cx , ++cy */
        } else {
          cx += ar4 ;
          cy -= ar3 ;  /* --cx , ++cy */
        }

/* Have got position, so extract the appropriate bits. */
      bitptr = BLIT_ADDRESS(baseptr, halfys[cy] + BLIT_OFFSET(cx)) ;
      t2 = cx & BLIT_MASK_BITS ;
      bits |= SHIFTRIGHT(SHIFTLEFT(*bitptr, t2), needword) ;
      t2 = ( BLIT_WIDTH_BITS - t2 ) ;
      needword += t2 ;
      if ( needword >= BLIT_WIDTH_BITS ) {
        t2 -= ( needword - BLIT_WIDTH_BITS ) ;
        cx += t2 ;
        break ;
      }
      cx += t2 ;
    }
    (*dstptr++) = bits ;
  }
}


/* Blits from (0,0) to (x, y) -- x or y is usually 0 */
static void extendform(FORM *inform ,
                       register int32 x, int32 y,
                       int32 x2c , int32 y2c )
{
  register int32 w, temp ;
  register blit_t ow ;
  register blit_t *toform ;
  register blit_t *formptr ;

  register int32 h ;
  register blit_t firstmask ;

  int32 sycoff1 , sycoff2 , width ;

  /* Extract all the form info. */
  w = theFormW(*inform) ;
  h = theFormH(*inform) ;
  sycoff1 = sycoff2 = theFormL(*inform) ;
  formptr = toform = theFormA(*inform) ;

  /* check & adjust right hand edge of bit-blt. */
  temp = x - x2c ;
  if ( temp > 0 )
    return ;

  temp += ( w - 1 ) ;
  if ( temp > 0 )
    w -= temp ;

  toform = BLIT_ADDRESS(toform, BLIT_OFFSET(x)) ;
  x = x & BLIT_MASK_BITS ;

  /* Check & adjust top edge of bit-blt. */
  temp = y - y2c ;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
     h -= temp ;

  toform = BLIT_ADDRESS(toform, sycoff2 * y) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(temp) ;

  width = w ;
  if ( x ) {
    temp = BLIT_WIDTH_BITS - x ;
    firstmask = SHIFTLEFT( ALLONES , temp ) ;
    while ( h > 0 ) {
      --h ;
      w = width ;
      ow = *formptr++ ;
      *toform = (*toform & firstmask) | SHIFTRIGHT( ow , x ) ;
      toform++ ;
      w -= temp ;
      while ( w > 0 ) {
        register blit_t tw = SHIFTLEFT( ow , temp ) ;
        ow = *formptr++ ;
        *toform++ = tw | SHIFTRIGHT( ow , x ) ;
        w -= BLIT_WIDTH_BITS ;
      }
      formptr = BLIT_ADDRESS(formptr, sycoff1) ;
      toform = BLIT_ADDRESS(toform, sycoff2) ;
    }
  } else {
    while ( h > 0 ) {
      --h ;
      w = width ;
      while ( w > 0 ) {
        *toform++ = *formptr++ ;
        w -= BLIT_WIDTH_BITS ;
      }
      formptr = BLIT_ADDRESS(formptr, sycoff1) ;
      toform = BLIT_ADDRESS(toform, sycoff2) ;
    }
  }
}


/* Expands a small screen form to at least a word wide. */
void bitexpandform( const ht_params_t *ht_params,
                    int32 mxdims, int32 mydims, FORM *newform )
{
  register int32 inner , outer , alignment ;
  register int32 i , n ;
  register int32 xdims  , ydims  ;
  register blit_t *iptr ;
  blit_t mask ;

  xdims = ht_params->xdims; ydims = ht_params->ydims;

  switch ( ht_params->type ) {
  case SPECIAL :
  case ONELESSWORD :
  case ORTHOGONAL :
    if (( xdims < mxdims ) || ( ydims < mydims )) {

      while ( xdims < mxdims ) {
        extendform(newform, xdims, 0, mxdims - 1, mydims - 1);
        xdims <<= 1 ;
      }
      while ( ydims < mydims ) {
        extendform(newform, 0, ydims, mxdims - 1, mydims - 1);
        ydims <<= 1 ;
      }
    }
    return ;

  default:
    inner = FORM_LINE_BYTES(xdims) ;
    outer = theFormL(*newform) ;
    /* First of all pad out to a whole word. */
    if (( xdims & BLIT_MASK_BITS ) != 0 ) {
      /* first clear the bits to the right of the form so that if the form
       * had been set to all ones, the alignbits can OR in the bits to be set.
       */
      iptr = BLIT_ADDRESS(theFormA(*newform), inner - BLIT_WIDTH_BYTES) ;
      mask = SHIFTLEFT( ALLONES , ( BLIT_WIDTH_BITS - (xdims & BLIT_MASK_BITS))) ;
      for ( i = 0 ; i < mydims ; ++i ) {
        *iptr &= mask ;
        iptr = BLIT_ADDRESS(iptr, outer) ;
      }
      alignment = ( xdims & ~BLIT_MASK_BITS) ;
      iptr = BLIT_ADDRESS(theFormA(*newform), inner - BLIT_WIDTH_BYTES) ;
      for ( i = 0 ; i < mydims ; ++i ) {
        iptr[ 0 ] = alignbits( ht_params, alignment, i, theFormA(*newform));
        iptr = BLIT_ADDRESS(iptr, outer) ;
      }
    }
    /* Then pad out to the whole form. */
    n = ( outer - inner ) >> BLIT_SHIFT_BYTES ;
    alignment = xdims & ~BLIT_MASK_BITS ;
    if (( xdims & BLIT_MASK_BITS ) != 0 )
      alignment += BLIT_WIDTH_BITS ;

    iptr = BLIT_ADDRESS(theFormA(*newform), inner) ;
    for ( i = 0 ; i < mydims ; ++i ) {
      padbits( ht_params, alignment, i, n, iptr, theFormA(*newform));
      iptr = BLIT_ADDRESS(iptr, outer) ;
    }
    return ;
  }
}


#define DOT_CENTRED_ROSETTES TRUE
#define CLR_CENTRED_ROSETTES TRUE

void set_cell_bits(FORM *formptr, const ht_params_t *ht_params,
                   int16 *xptr, int16 *yptr, int32 n,
                   int32 R1, int32 R2, int32 R3, int32 R4,
                   int32 *ys, int32 depth,
                   int32 scms, Bool dot_centered,
                   Bool accurate, Bool thresholdScreen)
{
  register blit_t *wordptr;
  register int32 x , y ;
  register int32 w , h ;
  register int32 tx , ty ;
  register int32 dx , dy ;
  Bool set = TRUE;
  int32 halfr1 = ht_params->r1, halfr2 = ht_params->r2,
    halfr3 = ht_params->r3, halfr4 = ht_params->r4;
  int32 halfxdims = ht_params->xdims, halfydims = ht_params->ydims;

  if ( n < 0 ) {
    set = FALSE ;
    n = -n;
    xptr -= n ;
    yptr -= n ;
  }

  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  wordptr = theFormA(*formptr) ;

  if ( thresholdScreen ) {
    dx = 0 ;
    dy = 0 ;
  } else {
    dx = ( halfr1 + halfr4 ) / 2 ;
    dy = ( halfr3 + halfr2 ) / 2 ;

    if ( !accurate || !dot_centered ) {
      if ( CLR_CENTRED_ROSETTES && ( ( scms & 1 ))) {
        dx -= (( halfr1 - halfr4 ) / ( 2 * scms )) ;
        dy -= (( halfr2 + halfr3 ) / ( 2 * scms )) ;
      }
      if ( CLR_CENTRED_ROSETTES && (!( scms & 1 ))) {
        /* Nothing more to do. */
      }
    } else {
      if ( DOT_CENTRED_ROSETTES && ( ( scms & 1 ))) {
        /* Nothing more to do. */
      }
      if ( DOT_CENTRED_ROSETTES && (!( scms & 1 ))) {
        dx -= (( halfr1 - halfr4 ) / ( 2 * scms )) ;
        dy -= (( halfr2 + halfr3 ) / ( 2 * scms )) ;
      }
    }
    dx -= dx % depth; /* truncate to pixel boundary */
  }

  while ((--n) >= 0 ) {
    x = (*xptr++) - dx ;
    y = (*yptr++) - dy ;

    while ( y < 0 )
      if ( x > halfr4 ) {
        x -= halfr4 ;
        y += halfr3 ;  /* --cx , ++cy */
      } else {
        x += halfr1 ;
        y += halfr2 ;  /* --cx , --cy */
      }
    while ( x < 0 )
      if ( y > halfr3 ) {
        x += halfr4 ;
        y -= halfr3 ;  /* --cx , ++cy */
      } else {
        x += halfr1 ;
        y += halfr2 ;  /* --cx , --cy */
      }
    while ( y >= halfydims )
      if ( x >= halfr1 ) {
        x -= halfr1 ;
        y -= halfr2 ;  /* ++cx , ++cy */
      } else {
        x += halfr4 ;
        y -= halfr3 ;  /* --cx , ++cy */
      }
    while ( x >= halfxdims )
      if ( y >= halfr2 ) {
        x -= halfr1 ;
        y -= halfr2 ;  /* ++cx , ++cy */
      } else {
        x -= halfr4 ;
        y += halfr3 ;  /* --cx , ++cy */
      }

    if ( set ) {

      CHECKSETBIT( x, y, wordptr, w, h, ys, depth );

      tx = x - R1 ;
      ty = y - R2 ;
      CHECKSET_ALLBITS( tx, ty, wordptr, w, h, ys, depth, -R1, -R2 );

      tx = x + R1 ;
      ty = y + R2 ;
      CHECKSET_ALLBITS( tx, ty, wordptr, w, h, ys, depth, R1, R2 );

      tx = x - R4 ;
      ty = y + R3 ;
      CHECKSET_ALLBITS( tx, ty, wordptr, w, h, ys, depth, -R4, R3 );

      tx = x + R4 ;
      ty = y - R3 ;
      CHECKSET_ALLBITS( tx, ty, wordptr, w, h, ys, depth, R4, -R3 );

    } else { /* clear the bits */

      CHECKCLRBIT( x, y, wordptr, w, h, ys, depth );

      tx = x - R1 ;
      ty = y - R2 ;
      CHECKCLR_ALLBITS( tx, ty, wordptr, w, h, ys, depth, -R1, -R2 );

      tx = x + R1 ;
      ty = y + R2 ;
      CHECKCLR_ALLBITS( tx, ty, wordptr, w, h, ys, depth, R1, R2 );

      tx = x - R4 ;
      ty = y + R3 ;
      CHECKCLR_ALLBITS( tx, ty, wordptr, w, h, ys, depth, -R4, R3 );

      tx = x + R4 ;
      ty = y - R3 ;
      CHECKCLR_ALLBITS( tx, ty, wordptr, w, h, ys, depth, R4, -R3 );
    }
  }
}

/* Log stripped */
