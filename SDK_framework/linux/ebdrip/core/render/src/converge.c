/** \file
 * \ingroup halftone
 *
 * $HopeName: CORErender!src:converge.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone convergence and copying.
 *
 * The functions find*bits() and more*bits() are used to find the appropriate
 * position in a halftone cell for the x,y position on the page, and then
 * read out a number of bits into the halftonebase area. The number of blit
 * words worth of halftone data may be larger than the halftone cell width.
 *
 * The halftone parameters repeatb and repeatbw are used to control how much
 * data must be extracted from the halftone cell definition, and how much can
 * be replicated. repeatb is the LCM of the width of the cell in bits and the
 * width of a byte in bits, and gives the number of bytes which must be
 * repeated from the cell definition before a block copy of a number of bytes
 * can fill in the rest. repeatbw is repeatb rounded up to the number of blit
 * words containing those bytes.
 */

#include "core.h"

#include "bitblts.h"
#include "often.h"
#include "swoften.h"
#include "graphics.h"
#include "render.h"
#include "halftone.h"
#include "converge.h"
#include "caching.h"


/* Fast copy routines for halftone replication. When these are called, the
   source pointer will be destination minus repeatb. The copy_x routine
   requires two source words to be complete if repeatb is not a multiple of
   the blit word size, so it can extract the source words and shift them to
   create a complete destination word. The destination and source may
   overlap, but not by less than two blit words. If you decide to
   re-implement the copy to remove or add new restrictions on copying, please
   also update the calculation of theIHalfRepeatB() in halftone.c. */

static inline void copy_0(register blit_t *s, register blit_t *d,
                          register uint32 nw)
{
  while ( nw >= 8 ) {
    PENTIUM_CACHE_LOAD(d + 7) ;
    d[ 0 ] = s[ 0 ] ;
    d[ 1 ] = s[ 1 ] ;
    d[ 2 ] = s[ 2 ] ;
    d[ 3 ] = s[ 3 ] ;
    d[ 4 ] = s[ 4 ] ;
    d[ 5 ] = s[ 5 ] ;
    d[ 6 ] = s[ 6 ] ;
    d[ 7 ] = s[ 7 ] ;
    d += 8 ; s += 8 ; nw -= 8 ;
  }
  while ( nw > 0 ) {
    *d++= *s++ ;
    --nw ;
  }
}

#if defined(highbytefirst) && defined(Unaligned_32bit_access)

#define copy_x copy_0

#else  /* !highbytefirst || !Unaligned_32bit_access */

/* Do as few writes to memory as possible when converging; each source word
   is only read once and each destination word is written once by this loop,
   which is about 10 times faster on some machines (e.g. Alpha) than the
   previous byte extract and shuffle method. */

/** This is a single case of the switch in copy_x. It stores words into the
    destination bitmap by extracting the source bitmap and shifting it left
    by a number of bits. */
#define COPY_CASE(s, d, nw, bits_) MACRO_START                          \
  register blit_t _ping_ = (s)[0], _pong_ ;                             \
  while ( (nw) >= 8 ) {                                                 \
    PENTIUM_CACHE_LOAD((d) + 7) ;                                       \
    _pong_ = (s)[ 1 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[0], _ping_, _pong_, (bits_)) ;                 \
    _ping_ = (s)[ 2 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[1], _pong_, _ping_, (bits_)) ;                 \
    _pong_ = (s)[ 3 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[2], _ping_, _pong_, (bits_)) ;                 \
    _ping_ = (s)[ 4 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[3], _pong_, _ping_, (bits_)) ;                 \
    _pong_ = (s)[ 5 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[4], _ping_, _pong_, (bits_)) ;                 \
    _ping_ = (s)[ 6 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[5], _pong_, _ping_, (bits_)) ;                 \
    _pong_ = (s)[ 7 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[6], _ping_, _pong_, (bits_)) ;                 \
    _ping_ = (s)[ 8 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[7], _pong_, _ping_, (bits_)) ;                 \
    (d) += 8 ; (s) += 8 ; (nw) -= 8 ;                                   \
  }                                                                     \
  while ( (nw) >= 2 ) {                                                 \
    PENTIUM_CACHE_LOAD((d) + 1) ;                                       \
    _pong_ = (s)[ 1 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[0], _ping_, _pong_, (bits_)) ;                 \
    _ping_ = (s)[ 2 ] ;                                                 \
    BLIT_SHIFT_MERGE((d)[1], _pong_, _ping_, (bits_)) ;                 \
    (d) += 2 ; (s) += 2 ; (nw) -= 2 ;                                   \
  }                                                                     \
  if ( nw ) {                                                           \
    BLIT_SHIFT_MERGE((d)[0], _ping_, (s)[1], (bits_)) ;                 \
  }                                                                     \
MACRO_END

static inline void copy_x(blit_t *s, register blit_t *d, register uint32 nw)
{
  intptr_t offset = (intptr_t)s & BLIT_MASK_BYTES ;
  register blit_t *src = BLIT_ADDRESS(s, -offset) ;

  switch ( offset ) {
  case 0 :
    copy_0(src, d, nw) ;
    break ;
  case 1 :
    COPY_CASE(src, d, nw, 8) ;
    break ;
  case 2:
    COPY_CASE(src, d, nw, 16) ;
    break ;
  case 3:
    COPY_CASE(src, d, nw, 24) ;
    break ;
#if BLIT_WIDTH_BYTES > 4
  case 4 :
    COPY_CASE(src, d, nw, 32) ;
    break ;
  case 5 :
    COPY_CASE(src, d, nw, 40) ;
    break ;
  case 6:
    COPY_CASE(src, d, nw, 48) ;
    break ;
  case 7:
    COPY_CASE(src, d, nw, 56) ;
    break ;
#endif
  default:
    HQFAIL("Invalid number of bytes in a blit_t word");
  }
}

#endif /* !highbytefirst || !Unaligned_32bit_access */

#define SETUP_HALFTONE_COPY(ht_params, n, blitstocopy) MACRO_START      \
  blitstocopy = 0 ;                                                     \
  if ( n > ht_params->repeatbw ) {                                      \
    blitstocopy = n - ht_params->repeatbw ;                             \
    n = ht_params->repeatbw ;                                           \
  }                                                                     \
MACRO_END

#define DO_HALFTONE_COPY(ht_params, s, d, blitstocopy) MACRO_START      \
  if ( blitstocopy ) {                                                  \
    (s) = BLIT_ADDRESS((d), -ht_params->repeatb) ;                      \
    copy_x((s), (d), blitstocopy) ;                                     \
  }                                                                     \
MACRO_END

/* ajcd 2005-06-28: Removed workaround. PPC NT is not a supported platform
   anymore, and compilers have probably moved on since then.

   Work around the PPC NT optimiser bug where the code generated for COPYANY()
   above has re-ordered the loads and saves incorrectly, so for example in the
   loop unroll by 2 cases above, this code:
   ...
   (d)[ 0 ] = (blit_t)(SHIFTLEFT(_ping_,24) | SHIFTRIGHT(_pong_,8));
   _ping_ = _src_[ 2 ];
   ...
   compiles to load src[2], then store d[0], which isn't too helpfull when
   src = d - 2.  We use knowledge of where the code gets it wrong to generate
   more data 'properly', so that src and d can be far enough apart to prevent
   clashes.
   */



/* ----------------------------------------------------------------------------
   function:            moreonbitsptr(..)  author:              Andrew Cave
   creation date:       24-May-1987        last modification:   ##-###-####
   arguments:           x , y , n .
   description:

   This procedure obtains the next n bit masks used in the halftoning.
   Version for orthogonal screens using a cached FORM.

-------------------------------------------------------------------------- */
void moreonbitsptr(blit_t *halftonebase, const ht_params_t *ht_params,
                   int32 x, int32 y, int32 n)
{
  register int32 cx ;
  register int32 shiftr , shiftl ;
  register blit_t src1 , src2 ;
  register blit_t *dstptr ;
  register blit_t *srcptr ;
  int32 w , cy ;
  blit_t *baseptr ;
  int32 blitstocopy ;

  SwOftenUnsafe() ;

/* Find out how many words we can start doing a straight copy afterwards. */
  SETUP_HALFTONE_COPY(ht_params, n, blitstocopy) ;

/* Firstly, quick correct jumps in x & y. */
  cx = (int32)((uint32)( x + ht_params->px ) % (uint32)ht_params->xdims) ;
  cy = (int32)((uint32)( y + ht_params->py ) % (uint32)ht_params->ydims) ;

/* Get y-base index into h/t. */
  baseptr = BLIT_ADDRESS(theFormA(*ht_params->form), ht_params->ys[cy]) ;

  srcptr = &baseptr[cx >> BLIT_SHIFT_BITS] ;
  dstptr = halftonebase ;

  src2 = (*srcptr++) ;
  shiftl = cx & BLIT_MASK_BITS ;
  shiftr = BLIT_WIDTH_BITS - shiftl ;

  w  = ht_params->xdims  ;

  while ((--n) > 0 ) {
    src1 = src2 ;
    src2 = (*srcptr++) ;
#ifndef Can_Shift_32
    if ( !shiftl )
      (*dstptr++) = src1;
    else
#endif /* Can_Shift_32 */
      (*dstptr++) = SHIFTLEFT( src1 , shiftl ) | SHIFTRIGHT( src2 , shiftr ) ;

    cx += BLIT_WIDTH_BITS ;
    if ( cx >= w ) {
      cx -= w ;
      srcptr = baseptr ;
      src2 = (*srcptr++) ;
      shiftl = cx & BLIT_MASK_BITS ;
      shiftr = BLIT_WIDTH_BITS - shiftl ;
    }
  }
#ifndef Can_Shift_32
  if ( !shiftl )
    (*dstptr++) = src2;
  else
#endif /* Can_Shift_32 */
    (*dstptr++) = SHIFTLEFT(src2,shiftl) | SHIFTRIGHT(srcptr[0],shiftr) ;

/* Now we've done all the hard work, just replicate the results */
  DO_HALFTONE_COPY(ht_params, srcptr, dstptr, blitstocopy) ;
}

/* ----------------------------------------------------------------------------
   function:            moregnbits(..)  author:              Andrew Cave
   creation date:       24-May-1987     last modification:   ##-###-####
   arguments:           x , y .
   description:

   This procedure obtains the next n bit mask used in the halftoning.
   Version for general (non-orthogonal) screens using a cached FORM.

---------------------------------------------------------------------------- */
void findgnbits( ht_params_t *ht_params,
                 int32 *bxpos, int32 *bypos, int32 x , int32 y )
{
  register int32 cx , cy ;
  int32 halfexdims = ht_params->exdims, halfeydims = ht_params->eydims;
  int32 halfydims = ht_params->ydims;

  SwOftenUnsafe() ;

  cx = x - ht_params->cx ;
  cy = y - ht_params->cy ;

  if ( ! (( cx >= 0 ) && ( cx < halfexdims ) &&
          ( cy >= 0 ) && ( cy < halfydims ))) {
    register int32 ar1 = ht_params->r1 ;
    register int32 ar2 = ht_params->r2 ;
    register int32 ar3 = ht_params->r3 ;
    register int32 ar4 = ht_params->r4 ;

/* Firstly, large jumps in y. */
    while ( cy >= halfeydims )
      cy -= halfeydims ;
    while ( cy < 0 )
      cy += halfeydims ;
/* Secondly, small jumps in y. */
    while ( cy >= halfydims )
      if ( cx >= ar1 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      }
      else {
        cx += ar4 ;
        cy -= ar3 ;  /* --cx , ++cy */
      }
/* Thirdly, correct x. */
    while ( cx >= halfexdims )
      cx -= halfexdims ;
    while ( cx < 0 )
      cx += halfexdims ;

/* Finally, save this for next time. */
    ht_params->cx = x - cx ;
    ht_params->cy = y - cy ;
  }
  (*bxpos) = cx ;
  (*bypos) = cy ;
}

void moregnbitsptr(blit_t *halftonebase, ht_params_t *ht_params,
                   int32 x, int32 y, int32 n)
{
  register int32 cx , cy ;
  register blit_t *dstptr ;
  register blit_t *srcptr ;
  register blit_t src1, src2 ;
  int32 w ;
  blit_t *baseptr ;
  int32 blitstocopy, shiftr, shiftl ;
  int32 halfexdims = ht_params->exdims, halfeydims = ht_params->eydims;
  int32 halfydims = ht_params->ydims;

  SwOftenUnsafe() ;

/* Find out how many words we can start doing a straight copy afterwards. */
  SETUP_HALFTONE_COPY(ht_params, n, blitstocopy) ;

  cx = x - ht_params->cx ;
  cy = y - ht_params->cy ;

  if ( ! (( cx >= 0 ) && ( cx < halfexdims ) &&
          ( cy >= 0 ) && ( cy < halfydims ))) {
    register int32 ar1 = ht_params->r1 ;
    register int32 ar2 = ht_params->r2 ;
    register int32 ar3 = ht_params->r3 ;
    register int32 ar4 = ht_params->r4 ;

/* Firstly, large jumps in y. */
    while ( cy >= halfeydims )
      cy -= halfeydims ;
    while ( cy < 0 )
      cy += halfeydims ;
/* Secondly, small jumps in y. */
    while ( cy >= halfydims )
      if ( cx >= ar1 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      }
      else {
        cx += ar4 ;
        cy -= ar3 ;  /* --cx , ++cy */
      }
/* Thirdly, correct x. */
    while ( cx >= halfexdims )
      cx -= halfexdims ;
    while ( cx < 0 )
      cx += halfexdims ;

/* Finally, save this for next time. */
    ht_params->cx = x - cx ;
    ht_params->cy = y - cy ;
  }

/* Get y-base index into h/t. */
  baseptr = BLIT_ADDRESS(theFormA(*ht_params->form), ht_params->ys[cy]) ;

  srcptr = &baseptr[cx >> BLIT_SHIFT_BITS] ;
  dstptr = halftonebase ;

  src2 = (*srcptr++) ;
  shiftl = cx & BLIT_MASK_BITS ;
  shiftr = BLIT_WIDTH_BITS - shiftl ;

  w  = halfexdims ;

  while ((--n) > 0 ) {
    src1 = src2 ;
    src2 = (*srcptr++) ;
#ifndef Can_Shift_32
    if ( !shiftl )
      (*dstptr++) = src1;
    else
#endif  /* Can_Shift_32 */
      (*dstptr++) = SHIFTLEFT( src1 , shiftl ) | SHIFTRIGHT( src2 , shiftr ) ;

    cx += BLIT_WIDTH_BITS ;
    if ( cx >= w ) {
      do {
        cx -= w ;
      } while ( cx >= w ) ;
      srcptr = baseptr ;
      src2 = (*srcptr++) ;
      shiftl = cx & BLIT_MASK_BITS ;
      shiftr = BLIT_WIDTH_BITS - shiftl ;
    }
  }
#ifndef Can_Shift_32
  if ( !shiftl )
    (*dstptr++) = src2 ;
  else
#endif  /* Can_Shift_32 */
    (*dstptr++) = SHIFTLEFT(src2,shiftl) | SHIFTRIGHT(srcptr[0],shiftr) ;

/* Now we've done all the hard work, just replicate the results */
  DO_HALFTONE_COPY(ht_params, srcptr, dstptr, blitstocopy) ;
}

/* ----------------------------------------------------------------------------
   function:            moresgnbitsptr#(..) author:              Andrew Cave
   creation date:       24-May-1987         last modification:   ##-###-####
   arguments:           x , y .
   description:

   This procedure obtains the next n bit mask used in the halftoning.
   Version for general (non-orthogonal) screens using a cached FORM.

---------------------------------------------------------------------------- */
void findsgnbits( ht_params_t *ht_params,
                  int32 *bxpos, int32 *bypos, int32 x, int32 y )
{
  register int32 cx , cy ;
  int32 halfexdims = ht_params->exdims, halfeydims = ht_params->eydims;
  int32 halfxdims = ht_params->xdims, halfydims = ht_params->ydims;

  SwOftenUnsafe() ;

  cx = x - ht_params->cx ;
  cy = y - ht_params->cy ;

  if ( ! (( cx >= 0 ) && ( cx < halfxdims ) &&
          ( cy >= 0 ) && ( cy < halfydims ))) {
    register int32 ar1 = ht_params->r1 ;
    register int32 ar2 = ht_params->r2 ;
    register int32 ar3 = ht_params->r3 ;
    register int32 ar4 = ht_params->r4 ;

/* Firstly, large jumps in x. */
    while ( cx >= halfexdims )
      cx -= halfexdims ;
    while ( cx < 0 )
      cx += halfexdims ;
/* Secondly, small jumps in x. */
    while ( cx >= halfxdims )
      if ( cy >= ar2 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      }
      else {
        cx -= ar4 ;
        cy += ar3 ;  /* --cx , ++cy */
      }
/* Thirdly, correct y. */
    while ( cy >= halfeydims )
      cy -= halfeydims ;
    while ( cy < 0 )
      cy += halfeydims ;
/* Fourthly, small jumps in y. */
    while ( cy >= halfydims )
      if ( cx >= ar1 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      }
      else {
        cx += ar4 ;
        cy -= ar3 ;  /* --cx , ++cy */
      }

/* Finally, save this for next time. */
    ht_params->cx = x - cx ;
    ht_params->cy = y - cy ;
  }
  (*bxpos) = cx ;
  (*bypos) = cy ;
}

void moresgnbitsptr(blit_t *halftonebase, ht_params_t *ht_params,
                    int32 x, int32 y, int32 n)
{
  register int32 cx , cy ;
  register blit_t *dstptr ;
  register blit_t *srcptr ;
  register blit_t src1, src2 ;
  int32 blitstocopy, shiftr, shiftl ;
  int32 halfexdims = ht_params->exdims, halfeydims = ht_params->eydims;
  int32 halfxdims = ht_params->xdims, halfydims = ht_params->ydims;
  blit_t *halfform_addr = theFormA(*ht_params->form);

  SwOftenUnsafe() ;

  /* Find out how many words we can start doing a straight copy afterwards. */
  SETUP_HALFTONE_COPY(ht_params, n, blitstocopy) ;

  cx = x - ht_params->cx ;
  cy = y - ht_params->cy ;

  if ( ! (( cx >= 0 ) && ( cx < halfxdims ) &&
          ( cy >= 0 ) && ( cy < halfydims ))) {
    register int32 ar1 = ht_params->r1 ;
    register int32 ar2 = ht_params->r2 ;
    register int32 ar3 = ht_params->r3 ;
    register int32 ar4 = ht_params->r4 ;

/* Firstly, large jumps in x. */
    while ( cx >= halfexdims )
      cx -= halfexdims ;
    while ( cx < 0 )
      cx += halfexdims ;
/* Secondly, small jumps in x. */
    while ( cx >= halfxdims )
      if ( cy >= ar2 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      }
      else {
        cx -= ar4 ;
        cy += ar3 ;  /* --cx , ++cy */
      }
/* Thirdly, correct y. */
    while ( cy >= halfeydims )
      cy -= halfeydims ;
    while ( cy < 0 )
      cy += halfeydims ;
/* Fourthly, small jumps in y. */
    while ( cy >= halfydims )
      if ( cx >= ar1 ) {
        cx -= ar1 ;
        cy -= ar2 ;  /* ++cx , ++cy */
      }
      else {
        cx += ar4 ;
        cy -= ar3 ;  /* --cx , ++cy */
      }

/* Finally, save this for next time. */
    ht_params->cx = x - cx ;
    ht_params->cy = y - cy ;
  }

/* Get y-base index into h/t. */
  srcptr = BLIT_ADDRESS(halfform_addr, ht_params->ys[cy]) ;

  srcptr = &srcptr[cx >> BLIT_SHIFT_BITS] ;
  dstptr = halftonebase ;

  src2 = (*srcptr++) ;
  shiftl = cx & BLIT_MASK_BITS ;
  shiftr = BLIT_WIDTH_BITS - shiftl ;

  while ((--n) > 0 ) {
    src1 = src2 ;
    src2 = (*srcptr++) ;
#ifndef Can_Shift_32
    if ( ! shiftl )
      (*dstptr++) = src1 ;
    else
#endif  /* Can_Shift_32 */
      (*dstptr++) = SHIFTLEFT( src1 , shiftl ) | SHIFTRIGHT( src2 , shiftr ) ;

    cx += BLIT_WIDTH_BITS ;
    if ( cx >= halfxdims ) {
      register int32 ar1 = ht_params->r1 ;
      register int32 ar2 = ht_params->r2 ;
      register int32 ar3 = ht_params->r3 ;
      register int32 ar4 = ht_params->r4 ;

/* Firstly, correct x. */
      while ( cx >= halfxdims )
        if ( cy >= ar2 ) {
          cx -= ar1 ;
          cy -= ar2 ;  /* ++cx , ++cy */
        }
        else {
          cx -= ar4 ;
          cy += ar3 ;  /* --cx , ++cy */
        }
/* Secondly, correct y. */
      while ( cy >= halfydims )
        if ( cx >= ar1 ) {
          cx -= ar1 ;
          cy -= ar2 ;  /* ++cx , ++cy */
        }
        else {
          cx += ar4 ;
          cy -= ar3 ;  /* --cx , ++cy */
        }

      srcptr = BLIT_ADDRESS(halfform_addr, ht_params->ys[cy]) ;

      srcptr = &srcptr[cx >> BLIT_SHIFT_BITS] ;

      src2 = (*srcptr++) ;
      shiftl = cx & BLIT_MASK_BITS ;
      shiftr = BLIT_WIDTH_BITS - shiftl ;
    }
  }
#ifndef Can_Shift_32
  if ( ! shiftl )
    (*dstptr++) = src2 ;
  else
#endif  /* Can_Shift_32 */
    (*dstptr++) = SHIFTLEFT(src2,shiftl) | SHIFTRIGHT(srcptr[0],shiftr) ;

  /* Now we've done all the hard work, just replicate the results */
  DO_HALFTONE_COPY(ht_params, srcptr, dstptr, blitstocopy) ;
}

/* Log stripped */
