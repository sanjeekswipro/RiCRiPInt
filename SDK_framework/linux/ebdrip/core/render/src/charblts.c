/** \file
 * \ingroup bitblit
 *
 * $HopeName: CORErender!src:charblts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Character blit functions.
 */

#include "core.h"

#include "bitblts.h"
#include "blttables.h"
#include "htrender.h" /* ht_params_t */
#include "toneblt.h"
#include "render.h"

void invalid_char(render_blit_t *rb,
                  FORM *formptr, dcoord x, dcoord y)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(FORM *, formptr) ;
  UNUSED_PARAM(dcoord, x) ;
  UNUSED_PARAM(dcoord, y) ;

  HQFAIL("This function should never be called") ;
}

void next_char(render_blit_t *rb,
               FORM *formptr, dcoord x, dcoord y)
{
  DO_CHAR(rb, formptr, x, y) ;
}

void ignore_char(render_blit_t *rb,
                  FORM *formptr, dcoord x, dcoord y)
{
  UNUSED_PARAM(render_blit_t *, rb);
  UNUSED_PARAM(FORM *, formptr) ;
  UNUSED_PARAM(dcoord, x) ;
  UNUSED_PARAM(dcoord, y) ;
}

/* ----------------------------------------------------------------------------
   functions:           charblt...()        author:              Andrew Cave
   creation date:       01-May-1987        last modification:   ##-###-####
   arguments:           formptr, x, y
   description:

   These functions do all the low level bitblts in the interpreter.
   All the various castings that are done are necessary & sufficient,
   if left out, the compiler shifts things about wrongly.
   Really ***dirty*** C-code, but it compiles down superbly !

---------------------------------------------------------------------------- */
void charblt1(render_blit_t *rb,
              FORM *formptr,
              register dcoord x,
              dcoord y )
{
  register dcoord w , xindex ;
  register dcoord temp;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;

  register dcoord h , hoff ;
/* Masks for the left & right hand edge of bit-blt. */
  register blit_t firstmask, lastmask, ow ;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  dcoord x1c, x2c;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP,
           "Char form is not bitmap") ;

  x1c = rb->p_ri->clip.x1 + rb->x_sep_position;
  x2c = rb->p_ri->clip.x2 + rb->x_sep_position;
  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = theFormA(*toform) ;

/* check & adjust right hand edge of bit-blt. */
  temp = x - x2c ;
  if ( temp > 0 )
    return ;

  lastmask = ALLONES ;
  temp += ( w - 1 ) ;
  if ( temp > 0 ) {
    w -= temp ;
    lastmask = SHIFTLEFT( lastmask , BLIT_MASK_BITS - ( x2c & BLIT_MASK_BITS )) ;
  }

/* Check & adjust left hand edge of bit-blt. */
  xindex = 0 ;
  firstmask = ALLONES ;
  temp = x1c - x ;
  if ( temp > 0 ) {
    w -= temp ;
    if ( w <= 0 )
      return ;

    firstmask = SHIFTRIGHT( firstmask , temp & BLIT_MASK_BITS ) ;

/* modify offset's' into src & dst's bitmaps. */
    wordptr = BLIT_ADDRESS(wordptr, BLIT_OFFSET(temp)) ;
    toword = BLIT_ADDRESS(toword, BLIT_OFFSET(x1c)) ;

    if ( x < 0 )
      x = BLIT_WIDTH_BITS - ((-x) & BLIT_MASK_BITS ) ;

    x &= BLIT_MASK_BITS ;
    temp = ( x1c & BLIT_MASK_BITS ) ;
    if ( x - temp > 0 ) {
      xindex = BLIT_WIDTH_BITS - x ;
      x = 0 ;
    }
    w -= ( x - temp ) ;
  }
  else {
    toword = BLIT_ADDRESS(toword, BLIT_OFFSET(x)) ;
    x &= BLIT_MASK_BITS ;
  }

/* Check & adjust bottom edge of bit-blt. */
  temp = y - rb->p_ri->clip.y2;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
    h -= temp ;

/* Check & adjust top edge of bit-blt. */
  temp = rb->p_ri->clip.y1 - y;
  if ( temp > 0 ) {
    h -= temp ;
    if ( h <= 0 )
      return ;

    wordptr = BLIT_ADDRESS(wordptr, temp * sycoff1) ;
    y = rb->p_ri->clip.y1;
  }
  toword = BLIT_ADDRESS(toword, sycoff2 * (y - hoff)) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp);
  sycoff1 -= BLIT_OFFSET(xindex + temp);

  HQASSERT(x == 0 || xindex == 0, "Neither x nor xindex is zero in charblt1") ;

  if ( x != xindex ) {
    if ( w + x + xindex <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
      if ( x > xindex )
        lastmask = SHIFTLEFT( lastmask, x - xindex );
      else
        lastmask = SHIFTRIGHT( lastmask, xindex - x );
      firstmask &= lastmask ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTRIGHT( ow , x ) ;
        ow = SHIFTLEFT( ow , xindex ) ;
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else if ( w + xindex <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      hoff = BLIT_WIDTH_BITS - x ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTLEFT( ow , xindex ) ;
        *toword++ |= SHIFTRIGHT( ow , x ) ;
        *toword |= SHIFTLEFT( ow, hoff ) & lastmask ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else {    /* doesn't fit into one word at all */
      Bool shiftfirst ;
      dcoord save_w = w ;
      register blit_t tempblit ;

      if ( ! x ) {
        x = BLIT_WIDTH_BITS - xindex ;
        shiftfirst = TRUE ;
      }
      else {
        xindex = BLIT_WIDTH_BITS - x ;
        shiftfirst = FALSE ;
      }
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        if ( shiftfirst )
          goto UNHEALTHYJUMP ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          (*toword) |= tempblit ;
          ++toword ;
UNHEALTHYJUMP:
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        tempblit &= lastmask ;
        (*toword) |= tempblit ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) { /* fits in one word */
      firstmask &= lastmask ;
      x += BLIT_WIDTH_BYTES ;           /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        *toword |= *wordptr & firstmask ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
      }
    } else {    /* doesn't fit into one word */
      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          (*toword) |= ow ;
          ++toword ;
          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        ow &= lastmask ;
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        w = xindex ;
      }
    }
  }
}


void charblt0(render_blit_t *rb,
              FORM *formptr ,
              register dcoord x ,
              dcoord y )
{
  register dcoord w , xindex , endx ;
  register dcoord temp ;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;

  register dcoord h , hoff ;
/* Masks for the left & right hand edge of bit-blt. */
  register blit_t firstmask, lastmask, ow ;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  dcoord x1c , x2c ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP,
           "Char form is not bitmap") ;

  x1c = rb->p_ri->clip.x1 + rb->x_sep_position;
  x2c = rb->p_ri->clip.x2 + rb->x_sep_position;
  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = theFormA(*toform) ;

/* check & adjust right hand edge of bit-blt. */
  if ( x2c < x )
    return ;

  endx = x + ( w - 1 ) ;
  if ( endx > x2c ) {
    w -= ( endx - x2c ) ;
    endx = x2c ;
  }
  lastmask = SHIFTLEFT( ALLONES , BLIT_MASK_BITS - ( endx & BLIT_MASK_BITS )) ;

/* Check & adjust left hand edge of bit-blt. */
  xindex = 0 ;
  firstmask = ALLONES ;
  temp = x1c - x ;
  if ( temp > 0 ) {
    w -= temp ;
    if ( w <= 0 )
      return ;

/* modify offset's' into src & dst's bitmaps. */
    wordptr = BLIT_ADDRESS(wordptr, BLIT_OFFSET(temp)) ;
    toword = BLIT_ADDRESS(toword, BLIT_OFFSET(x1c)) ;

    firstmask = SHIFTRIGHT( firstmask , temp & BLIT_MASK_BITS ) ;

    if ( x < 0 )
      x = BLIT_WIDTH_BITS - ((-x) & BLIT_MASK_BITS ) ;

    x &= BLIT_MASK_BITS ;
    temp = ( x1c & BLIT_MASK_BITS ) ;
    if ( x - temp > 0 ) {
      xindex = BLIT_WIDTH_BITS - x ;
      x = 0 ;
    }
    w -= ( x - temp ) ;
  }
  else {
    toword = BLIT_ADDRESS(toword, BLIT_OFFSET(x)) ;
    x &= BLIT_MASK_BITS ;
  }

/* Check & adjust bottom edge of bit-blt. */
  temp = y - rb->p_ri->clip.y2;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
    h -= temp ;

/* Check & adjust top edge of bit-blt. */
  temp = rb->p_ri->clip.y1 - y;
  if ( temp > 0 ) {
    h -= temp ;
    if ( h <= 0 )
      return ;

    wordptr = BLIT_ADDRESS(wordptr, temp * sycoff1) ;
    y = rb->p_ri->clip.y1;
  }
  toword = BLIT_ADDRESS(toword, sycoff2 * (y - hoff)) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(xindex + temp) ;

  HQASSERT(x == 0 || xindex == 0, "Neither x nor xindex is zero in charblt0") ;

  if ( x != xindex ) {
    if ( w + x + xindex <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      if ( x > xindex )
        lastmask = SHIFTLEFT( lastmask, x - xindex );
      else
        lastmask = SHIFTRIGHT( lastmask, xindex - x );
      firstmask &= lastmask ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTRIGHT( ow , x ) ;
        ow = SHIFTLEFT( ow , xindex ) ;
        *toword &= ~ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else if ( w + xindex <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      hoff = BLIT_WIDTH_BITS - x ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTLEFT( ow , xindex ) ;
        *toword++ &= ~( SHIFTRIGHT( ow , x ) );
        *toword &= ~( SHIFTLEFT( ow, hoff ) & lastmask );
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else {    /* doesn't fit into one word at all */
      Bool shiftfirst ;
      dcoord save_w = w ;
      register blit_t tempblit ;

      if ( ! x ) {
        x = BLIT_WIDTH_BITS - xindex ;
        shiftfirst = TRUE ;
      }
      else {
        xindex = BLIT_WIDTH_BITS - x ;
        shiftfirst = FALSE ;
      }
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        if ( shiftfirst )
          goto UNHEALTHYJUMP ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          (*toword) &= (~tempblit) ;
          ++toword ;
UNHEALTHYJUMP:
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        tempblit &= lastmask ;
        *toword &= (~tempblit) ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) { /* fits in one word */
      firstmask &= lastmask ;
      x += BLIT_WIDTH_BYTES ;           /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        *toword &= (~(*wordptr & firstmask)) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
      }
    } else {    /* doesn't fit into one word */
      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          (*toword) &= (~ow) ;
          ++toword ;
          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        ow &= lastmask ;
        *toword &= (~ow) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        w = xindex ;
      }
    }
  }
}


void charclip1(render_blit_t *rb,
               FORM *formptr ,
               register dcoord x ,
               dcoord y )
{
  register dcoord w, xindex ;
  register dcoord temp ;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;
  register blit_t *clipptr ;

  register dcoord h , hoff ;
/* Masks for the left & right hand edge of bit-blt. */
  register blit_t firstmask, lastmask, ow ;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  dcoord x1c , x2c ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP,
           "Char form is not bitmap") ;

  x1c = rb->p_ri->clip.x1 + rb->x_sep_position;
  x2c = rb->p_ri->clip.x2 + rb->x_sep_position;
  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = theFormA(*toform) ;

/* Extract all the form info. */
  clipptr = theFormA(* rb->clipform ) ;

/* check & adjust right hand edge of bit-blt. */
  temp = x - x2c ;
  if ( temp > 0 )
    return ;

  lastmask = ALLONES ;
  temp += ( w - 1 ) ;
  if ( temp > 0 ) {
    w -= temp ;
    lastmask = SHIFTLEFT( lastmask , BLIT_MASK_BITS - ( x2c & BLIT_MASK_BITS )) ;
  }

/* Check & adjust left hand edge of bit-blt. */
  xindex = 0 ;
  firstmask = ALLONES ;
  temp = x1c - x ;
  if ( temp > 0 ) {
    w -= temp ;
    if ( w <= 0 )
      return ;

    firstmask = SHIFTRIGHT( firstmask , temp & BLIT_MASK_BITS ) ;

/* modify offset's' into src & dst's bitmaps. */
    wordptr = BLIT_ADDRESS(wordptr, BLIT_OFFSET(temp)) ;
    temp = BLIT_OFFSET(x1c) ;
    toword = BLIT_ADDRESS(toword, temp) ;
    clipptr = BLIT_ADDRESS(clipptr, temp) ;

    if ( x < 0 )
      x = BLIT_WIDTH_BITS - ((-x) & BLIT_MASK_BITS ) ;

    x &= BLIT_MASK_BITS ;
    temp = ( x1c & BLIT_MASK_BITS ) ;
    if ( x - temp > 0 ) {
      xindex = BLIT_WIDTH_BITS - x ;
      x = 0 ;
    }
    w -= ( x - temp ) ;
  }
  else {
    temp = BLIT_OFFSET(x) ;
    toword = BLIT_ADDRESS(toword, temp) ;
    clipptr = BLIT_ADDRESS(clipptr, temp) ;
    x &= BLIT_MASK_BITS ;
  }

/* Check & adjust bottom edge of bit-blt. */
  temp = y - rb->p_ri->clip.y2;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
    h -= temp ;

/* Check & adjust top edge of bit-blt. */
  temp = rb->p_ri->clip.y1 - y;
  if ( temp > 0 ) {
    h -= temp ;
    if ( h <= 0 )
      return ;

    wordptr = BLIT_ADDRESS(wordptr, temp * sycoff1) ;
    y = rb->p_ri->clip.y1;
  }
  temp = sycoff2 * (y - hoff) ;
  toword = BLIT_ADDRESS(toword, temp) ;
  clipptr = BLIT_ADDRESS(clipptr, temp) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(xindex + temp) ;

  HQASSERT(x == 0 || xindex == 0, "Neither x nor xindex is zero in charclip1") ;

  if ( x != xindex ) {
    if ( w + x + xindex <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      if ( x > xindex )
        lastmask = SHIFTLEFT( lastmask, x - xindex );
      else
        lastmask = SHIFTRIGHT( lastmask, xindex - x );
      firstmask &= lastmask ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTRIGHT( ow , x ) ;
        ow = SHIFTLEFT( ow , xindex ) ;
        ow &= *clipptr ;
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else if ( w + xindex <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      hoff = BLIT_WIDTH_BITS - x ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTLEFT( ow , xindex ) ;
        *toword |= SHIFTRIGHT( ow , x ) & *clipptr ;
        toword++, clipptr++ ;
        *toword |= SHIFTLEFT( ow, hoff ) & *clipptr & lastmask ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else {    /* doesn't fit into one word at all */
      Bool shiftfirst ;
      dcoord save_w = w ;
      register blit_t tempblit ;

      if ( ! x ) {
        x = BLIT_WIDTH_BITS - xindex ;
        shiftfirst = TRUE ;
      }
      else {
        xindex = BLIT_WIDTH_BITS - x ;
        shiftfirst = FALSE ;
      }
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        if ( shiftfirst )
          goto UNHEALTHYJUMP ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          tempblit &= (*clipptr) ;
          ++clipptr ;
          (*toword) |= tempblit ;
          ++toword ;
UNHEALTHYJUMP:
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        tempblit &= lastmask ;
        tempblit &= *clipptr ;
        *toword |= tempblit ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        clipptr = BLIT_ADDRESS(clipptr, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) { /* fits in one word */
      firstmask &= lastmask ;
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow &= *clipptr ;
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
      }
    } else {    /* doesn't fit into one word */
      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          ow &= (*clipptr) ;
          ++clipptr ;
          (*toword) |= ow ;
          ++toword ;
          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        ow &= lastmask ;
        ow &= *clipptr ;
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        w = xindex ;
      }
    }
  }
}


void charclip0(render_blit_t *rb,
               FORM *formptr ,
               register dcoord x ,
               dcoord y )
{
  register dcoord w , xindex ;
  register dcoord temp ;
  register blit_t *wordptr ;
  FORM *toform ;
  register blit_t *toword ;
  register blit_t *clipptr ;

  register dcoord h , hoff ;
/* Masks for the left & right hand edge of bit-blt. */
  register blit_t firstmask, lastmask, ow ;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;
  dcoord x1c , x2c ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(rb->clipform->type == FORMTYPE_BANDBITMAP,
           "Clip form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP,
           "Char form is not bitmap") ;

  x1c = rb->p_ri->clip.x1 + rb->x_sep_position;
  x2c = rb->p_ri->clip.x2 + rb->x_sep_position;
  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = theFormA(*toform) ;

/* Extract all the form info. */
  clipptr = theFormA(* rb->clipform ) ;

/* check & adjust right hand edge of bit-blt. */
  temp = x - x2c ;
  if ( temp > 0 )
    return ;

  lastmask = ALLONES ;
  temp += ( w - 1 ) ;
  if ( temp > 0 ) {
    w -= temp ;
    lastmask = SHIFTLEFT( lastmask , BLIT_MASK_BITS - ( x2c & BLIT_MASK_BITS )) ;
  }

/* Check & adjust left hand edge of bit-blt. */
  xindex = 0 ;
  firstmask = ALLONES ;
  temp = x1c - x ;
  if ( temp > 0 ) {
    w -= temp ;
    if ( w <= 0 )
      return ;

    firstmask = SHIFTRIGHT( firstmask , temp & BLIT_MASK_BITS ) ;

/* modify offset's' into src & dst's bitmaps. */
    wordptr = BLIT_ADDRESS(wordptr, BLIT_OFFSET(temp)) ;
    temp = BLIT_OFFSET(x1c) ;
    toword = BLIT_ADDRESS(toword, temp) ;
    clipptr = BLIT_ADDRESS(clipptr, temp) ;

    if ( x < 0 )
      x = BLIT_WIDTH_BITS - ((-x) & BLIT_MASK_BITS ) ;

    x &= BLIT_MASK_BITS ;
    temp = ( x1c & BLIT_MASK_BITS ) ;
    if ( x - temp > 0 ) {
      xindex = BLIT_WIDTH_BITS - x ;
      x = 0 ;
    }
    w -= ( x - temp ) ;
  }
  else {
    temp = BLIT_OFFSET(x) ;
    toword = BLIT_ADDRESS(toword, temp) ;
    clipptr = BLIT_ADDRESS(clipptr, temp) ;
    x &= BLIT_MASK_BITS ;
  }

/* Check & adjust bottom edge of bit-blt. */
  temp = y - rb->p_ri->clip.y2;
  if ( temp > 0 )
    return ;

  temp += ( h - 1 ) ;
  if ( temp > 0 )
    h -= temp ;

/* Check & adjust top edge of bit-blt. */
  temp = rb->p_ri->clip.y1 - y;
  if ( temp > 0 ) {
    h -= temp ;
    if ( h <= 0 )
      return ;

    wordptr = BLIT_ADDRESS(wordptr, temp * sycoff1) ;
    y = rb->p_ri->clip.y1;
  }
  temp = sycoff2 * (y - hoff) ;
  toword = BLIT_ADDRESS(toword, temp) ;
  clipptr = BLIT_ADDRESS(clipptr, temp) ;

  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(xindex + temp) ;

  HQASSERT(x == 0 || xindex == 0, "Neither x nor xindex is zero in charclip0") ;

  if ( x != xindex ) {
    if ( w + x + xindex <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      if ( x > xindex )
        lastmask = SHIFTLEFT( lastmask, x - xindex );
      else
        lastmask = SHIFTRIGHT( lastmask, xindex - x );
      firstmask &= lastmask ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTRIGHT( ow , x ) ;
        ow = SHIFTLEFT( ow , xindex ) ;
        ow &= *clipptr ;
        *toword &= ~ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else if ( w + xindex <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      hoff = BLIT_WIDTH_BITS - x ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow = SHIFTLEFT( ow , xindex ) ;
        *toword &= ~( SHIFTRIGHT( ow , x ) & *clipptr );
        toword++ , clipptr++ ;
        *toword &= ~( SHIFTLEFT( ow, hoff ) & *clipptr & lastmask );
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else {    /* doesn't fit into one word at all */
      Bool shiftfirst ;
      dcoord save_w = w ;
      register blit_t tempblit ;

      if ( ! x ) {
        x = BLIT_WIDTH_BITS - xindex ;
        shiftfirst = TRUE ;
      }
      else {
        xindex = BLIT_WIDTH_BITS - x ;
        shiftfirst = FALSE ;
      }
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        if ( shiftfirst )
          goto UNHEALTHYJUMP ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          tempblit &= (*clipptr) ;
          ++clipptr ;
          (*toword) &= (~tempblit) ;
          ++toword ;
UNHEALTHYJUMP:
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        tempblit &= lastmask ;
        tempblit &= *clipptr ;
        *toword &= (~tempblit) ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        clipptr = BLIT_ADDRESS(clipptr, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) { /* fits in one word */
      firstmask &= lastmask ;
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        ow = *wordptr & firstmask ;
        ow &= *clipptr ;
        *toword &= ~ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
      }
    } else {    /* doesn't fit into one word */
      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) & firstmask ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          ow &= (*clipptr) ;
          ++clipptr ;
          (*toword) &= (~ow) ;
          ++toword ;
          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        ow &= lastmask ;
        ow &= *clipptr ;
        *toword &= (~ow) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        clipptr = BLIT_ADDRESS(clipptr, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        w = xindex ;
      }
    }
  }
}


/* ----------------------------------------------------------------------------
   functions:           fastcharblt...()   author:              Angus Duggan
   creation date:       14-Apr-1994        last modification:   ##-###-####
   arguments:           formptr, x, y
   description:

   These functions do low level bitblts for image tiles or characters which
   are completely within the clip zone, so they don't need to do any edge
   adjustment or masking. They are based on the charblt functions.

---------------------------------------------------------------------------- */
static void fastcharblt1(render_blit_t *rb,
                         FORM *formptr ,
                         register dcoord x ,
                         dcoord y )
{
  register dcoord w, xindex ;
  register dcoord temp ;
  register blit_t ow ;
  register blit_t *wordptr ;

  FORM *toform ;
  register blit_t *toword ;

  register dcoord h , hoff ;
  int32 sycoff1 , sycoff2 ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP,
           "Char form is not bitmap") ;

  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = BLIT_ADDRESS(theFormA(*toform), BLIT_OFFSET(x)) ;
  toword = BLIT_ADDRESS(toword, sycoff2 * (y - hoff)) ;

  x &= BLIT_MASK_BITS ;
  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(temp) ;

  if ( x ) {
    if ( w + x <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        ow = SHIFTRIGHT( *wordptr , x ) ;
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else if ( w <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      xindex = BLIT_WIDTH_BITS - x ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr ;
        (*toword++) |= SHIFTRIGHT( ow , x ) ;
        (*toword) |= SHIFTLEFT( ow, xindex ) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else {    /* doesn't fit into one word at all */
      dcoord save_w = w ;
      register blit_t tempblit ;

      xindex = BLIT_WIDTH_BITS - x ;
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) ;
        ++wordptr ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          (*toword) |= tempblit ;
          ++toword ;
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        *toword |= tempblit ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) { /* fits in one word */
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        *toword |= *wordptr ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
      }
    } else {    /* doesn't fit into one word */
      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          (*toword) |= ow ;
          ++toword ;
          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        *toword |= ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        w = xindex ;
      }
    }
  }
}


static void fastcharblt0(render_blit_t *rb,
                         FORM *formptr ,
                         register dcoord x ,
                         dcoord y )
{
  register dcoord w , xindex ;
  register dcoord temp ;
  register blit_t ow, *wordptr ;
  FORM *toform ;
  register blit_t *toword ;

  register dcoord h , hoff ;

/* Important that these variables go on the stack. */
  int32 sycoff1 , sycoff2 ;

  HQASSERT(rb->depth_shift == 0, "1-bit fn called for multibit");
  HQASSERT(rb->outputform->type == FORMTYPE_CACHEBITMAPTORLE ||
           rb->outputform->type == FORMTYPE_CACHEBITMAP ||
           rb->outputform->type == FORMTYPE_BANDBITMAP ||
           rb->outputform->type == FORMTYPE_HALFTONEBITMAP,
           "Output form is not bitmap") ;
  HQASSERT(formptr->type == FORMTYPE_BANDBITMAP ||
           formptr->type == FORMTYPE_CACHEBITMAPTORLE ||
           formptr->type == FORMTYPE_CACHEBITMAP,
           "Char form is not bitmap") ;

  x += rb->x_sep_position ;

/* Extract all the form info. */
  w = theFormW(*formptr) ;
  h = theFormH(*formptr) ;
  sycoff1 = theFormL(*formptr) ;
  wordptr = theFormA(*formptr) ;

/* Extract all the form info. */
  toform = rb->outputform ;
  sycoff2 = theFormL(*toform) ;
  hoff = theFormHOff(*toform) + rb->y_sep_position ;
  toword = BLIT_ADDRESS(theFormA(*toform), BLIT_OFFSET(x)) ;
  toword = BLIT_ADDRESS(toword, sycoff2 * (y - hoff)) ;

  x &= BLIT_MASK_BITS ;
  temp = w + BLIT_MASK_BITS ;
  sycoff2 -= BLIT_OFFSET(x + temp) ;
  sycoff1 -= BLIT_OFFSET(temp) ;

  if ( x ) {
    if ( w + x <= BLIT_WIDTH_BITS ) { /* fits in one destination word */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        ow = SHIFTRIGHT( *wordptr , x ) ;
        *toword &= ~ow ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else if ( w <= BLIT_WIDTH_BITS ) { /* one src word, two dest words */
      w = sycoff1 + BLIT_WIDTH_BYTES ;  /* propagate +1 blit_t * out of loop */
      temp = sycoff2 + BLIT_WIDTH_BYTES ;       /* propagate +1 blit_t * out of loop */
      xindex = BLIT_WIDTH_BITS - x ;
      while ( h > 0 ) {
        --h ;
        ow = *wordptr ;
        (*toword++) &= ~( SHIFTRIGHT( ow , x ) );
        *toword &= ~( SHIFTLEFT( ow, xindex ) );
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, w) ;
      }
    } else {    /* doesn't fit into one word at all */
      dcoord save_w = w ;
      register blit_t tempblit ;

      xindex = BLIT_WIDTH_BITS - x ;
      sycoff2 += BLIT_WIDTH_BYTES ;     /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
/* Now for y-loop. */
/* Extract first two words to consider. */
        ow = (*wordptr) ;
        ++wordptr ;
        tempblit = SHIFTRIGHT( ow , x ) ;
        w -= xindex ;
/* Now for x-loop. */
        while ( w > 0 ) {
          (*toword) &= (~tempblit) ;
          ++toword ;
          tempblit = SHIFTLEFT( ow , xindex ) ;
          w -= x ;
          if ( w > 0 ) {
            ow = (*wordptr) ;
            ++wordptr ;
            tempblit |= SHIFTRIGHT( ow , x ) ;
            w -= xindex ;
          }
        }
        *toword &= (~tempblit) ;
        toword = BLIT_ADDRESS(toword, sycoff2) ;
        wordptr = BLIT_ADDRESS(wordptr, sycoff1) ;
        w = save_w ;
      }
    }
  }
  else {
    x = sycoff1 ;
    temp = sycoff2 + BLIT_WIDTH_BYTES ; /* propagate +1 blit_t * out of loop */
    if ( w <= BLIT_WIDTH_BITS ) { /* fits in one word */
      x += BLIT_WIDTH_BYTES ;   /* propagate +1 blit_t * out of loop */
      while ( h > 0 ) {
        --h ;
        *toword &= ~(*wordptr) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
      }
    } else {    /* doesn't fit into one word */
      xindex = w ;
      while ( h > 0 ) {
        --h ;
        ow = (*wordptr) ;
        ++wordptr ;
        w -= BLIT_WIDTH_BITS ;
        while ( w > 0 ) {
          (*toword) &= (~ow) ;
          ++toword ;
          ow = (*wordptr) ;
          ++wordptr ;
          w -= BLIT_WIDTH_BITS ;
        }
        *toword &= (~ow) ;
        toword = BLIT_ADDRESS(toword, temp) ;
        wordptr = BLIT_ADDRESS(wordptr, x) ;
        w = xindex ;
      }
    }
  }
}

/* ---------------------------------------------------------------------- */

void init_mask_char(void)
{
  blitslice0[BLT_CLP_NONE].charfn = fastcharblt0 ;
  blitslice0[BLT_CLP_RECT].charfn = charblt0 ;
  blitslice0[BLT_CLP_COMPLEX].charfn = charclip0 ;

  blitslice1[BLT_CLP_NONE].charfn = fastcharblt1 ;
  blitslice1[BLT_CLP_RECT].charfn = charblt1 ;
  blitslice1[BLT_CLP_COMPLEX].charfn = charclip1 ;

  nbit_blit_slice0[BLT_CLP_NONE].charfn =
  nbit_blit_slice0[BLT_CLP_RECT].charfn =
  nbit_blit_slice0[BLT_CLP_COMPLEX].charfn = charbltn;

  nbit_blit_slice1[BLT_CLP_NONE].charfn =
  nbit_blit_slice1[BLT_CLP_RECT].charfn =
  nbit_blit_slice1[BLT_CLP_COMPLEX].charfn = charbltn;
}

/* Log stripped */
