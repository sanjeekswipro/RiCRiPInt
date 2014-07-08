/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:stackops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS stack operators
 */

#include "core.h"

#include "swerrors.h"           /* error_handler */
#include "namedef_.h"           /* end_ */
#include "psvm.h"               /* ps_restore */
#include "stacks.h"             /* operandstack */
#include "stackops.h"

/* ----------------------------------------------------------------------------
   function:            pop_()             author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 193.

---------------------------------------------------------------------------- */
Bool pop_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            exch_()            author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 151.

---------------------------------------------------------------------------- */
Bool exch_(ps_context_t *pscontext)
{
  register OBJECT *o1 ;
  register OBJECT *o2 ;
  OBJECT tmpo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  tmpo = *o1 ; /* Sets slot properties */
  Copy(o1, o2) ;
  Copy(o2, &tmpo) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            dup_()             author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 148.

---------------------------------------------------------------------------- */
Bool dup_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  return push( theTop( operandstack ) , & operandstack ) ;
}

/* For copy_() operator see file 'shared1ops.c' */

/* ----------------------------------------------------------------------------
   function:            index_()           author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 172.

---------------------------------------------------------------------------- */
Bool index_(ps_context_t *pscontext)
{
  register int32 ssize , theindex ;
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;
  if ( oType(*o1) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  theindex = oInteger(*o1) ;

/*  Check that index is in valid range. */
  if (( theindex < 0 ) || ( theindex >= ssize ))
    return error_handler( RANGECHECK ) ;

/*  Duplicate element which is stackindex elements from top of 'old' stack. */
  o2 = stackindex( theindex + 1 , & operandstack ) ;
  Copy( o1 , o2 ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            roll_()            author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 205.

---------------------------------------------------------------------------- */
Bool roll_(ps_context_t *pscontext)
{
  register int32 n , g , i , j , k ;
  register int32 pos ;
  register OBJECT *o1 , *o2 ;

  int32 t[ 2 ] ;
  int32 inner ;
  OBJECT otemp = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, t , 2 ))
    return FALSE ;

  n = t[ 0 ] ;
  j = t[ 1 ] ;
  if (( n < 0 ) || ( n >= theStackSize( operandstack )))
    return error_handler( RANGECHECK ) ;

  npop( 2 , & operandstack ) ;
  if ( 2 > n ) /* rolling zero things or one thing is no-op */
    return TRUE ;

  /* Standardise j to be in the range [0,n). */
  if ( j < 0 ) {
    /* Optimise common case where -n <= j < 0 */
    j += n ;
    /*
     * General case were original j < -n
     * Note: in C % with -ve dividend produces -ve result so negate j before
     *       and after taking modulus. There is no problem with overflow
     *       when negating since j + n is always > MININT
     */
    if ( j < 0 ) {
      j = (-j) % n ;
      if ( j != 0 )
        j = n - j ;
    }
  }
  else if ( j >= n ) {
    /* Optimise common case where n <= j < 2*n */
    j -= n ;
    /* General case were original j >= 2*n */
    if ( j >= n )
      j = j % n ;
  }
  /* which will also make the gcd go faster, while getting the same result */

  if ( ! j ) /* early out of we happen to get no change */
    return TRUE ;

  HQASSERT( n >= 2 && 0 < j && j < n , "Args for optimised stack roll loop no good" );

  g = gcd( n , j ) ;
  inner = n / g ;
  for ( i = 0 ; i < g ; ++i ) {
    pos = i ;
    o1 = stackindex( pos , & operandstack ) ;
    o2 = ( & otemp ) ;
    Copy( o2 , o1 ) ;
    for ( k = inner ; k > 1 ; --k ) {
      pos = pos + j ;
      if ( pos > n )
        pos -= n ;
      o2 = stackindex( pos , & operandstack ) ;
      Copy( o1 , o2 ) ;
      o1 = o2 ;
    }
    o2 = ( & otemp ) ;
    Copy( o1 , o2 ) ;
  }
  return TRUE ;
}

/* Utility function for above function - calculates gcd of two +ve numbers. */
int32 gcd(int32 n,int32 j)
{
  if ( ! j )
    return  n  ;
  if ( ! n )
    return  j  ;

  return gcd( j , n % j ) ;
}

/* ----------------------------------------------------------------------------
   function:            clear_()           author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 127.

---------------------------------------------------------------------------- */
Bool clear_(ps_context_t *pscontext)
{
  register int32 i ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  i = theStackSize( operandstack ) ;
  if ( i < FRAMESIZE )
    npop( i + 1 , & operandstack ) ;
  else
    while ( i >= 0 ) {
      pop( & operandstack ) ;
      --i ;
    }

  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            count_()           author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 132.

---------------------------------------------------------------------------- */
Bool count_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = theStackSize( operandstack ) + 1 ;
  return push( & inewobj , & operandstack ) ;
}

/* For mark_() operator see file 'setup.c' */

/* ----------------------------------------------------------------------------
   function:            cleartomark_()     author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 127.

---------------------------------------------------------------------------- */
Bool cleartomark_(ps_context_t *pscontext)
{
  register int32 num ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

/*
  Get number of objects between top of stack and mark.
  If this number is negative, then no mark present.
*/
  if (( num = num_to_mark()) < 0 )
    return error_handler( UNMATCHEDMARK ) ;

  if ( num < FRAMESIZE )
    npop( num + 1 , & operandstack ) ; /* Also pop off mark. */
  else
    while ( num-- >= 0 ) /* Also pop off mark. */
      pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            counttomark_()     author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 133.

---------------------------------------------------------------------------- */
Bool counttomark_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/*
  Get number of objects between top of stack and mark.
  If this number is non-negative, then mark present.
*/
  if (( oInteger(inewobj) = num_to_mark()) < 0 )
    return error_handler( UNMATCHEDMARK ) ;

  return push( & inewobj , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            num_to_mark()      author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure counts the number of objects between the top of the stack,
   and the first mark that occurs.

---------------------------------------------------------------------------- */
int32 num_to_mark(void)
{
  register int32 size , loop ;

  size = theStackSize( operandstack ) ;
  for ( loop = 0 ; loop <= size ; ++loop )
    if ( oType(*stackindex(loop, &operandstack)) == OMARK )
      return  loop  ;
/*
  Negative number indicates no mark present.
*/
  return  -1  ;
}

/*-------------------------------------------------------------------------- */
/* A couple of utility functions that are intended to be used around callouts
 * to a recursive interpreter in situations where the callout could fail but
 * we can recover from it. The intention is that restoreStackPositions will
 * restore the state of the stacks prior to the failed callout to allow a
 * retry without risking a future invalidrestore error because of cruft left
 * on the stacks.
 */
void saveStackPositions(STACK_POSITIONS *saved)
{
  HQASSERT(saved, "Nowhere to save stack positions") ;

  saved->operandstackSize   = operandstack.size ;
  saved->dictstackSize      = dictstack.size ;
  saved->temporarystackSize = temporarystack.size ;
  saved->executionstackSize = executionstack.size ;
  saved->savelevel          = get_core_context_interp()->savelevel ;
}

Bool restoreStackPositions(STACK_POSITIONS *saved, Bool restoresavelevel)
{
  Bool success ;
  corecontext_t *corecontext = get_core_context_interp();
  ps_context_t *pscontext = corecontext->pscontext ;

  HQASSERT(saved, "No saved stack positions");

  /* Report stack underflows - we can't fix them */
  success = operandstack.size   >= saved->operandstackSize &&
            dictstack.size      >= saved->dictstackSize &&
            temporarystack.size >= saved->temporarystackSize ;

  /* Discard rubbish left on the stacks */
  if (operandstack.size < saved->operandstackSize)
    HQTRACE(TRUE, ("operandstack underflow")) ;
  while (operandstack.size > saved->operandstackSize)
    pop(&operandstack) ;

  if (dictstack.size < saved->dictstackSize)
    HQTRACE(TRUE, ("dictstack underflow")) ;
  while (dictstack.size > saved->dictstackSize)
    end_(pscontext) ;

  if (temporarystack.size < saved->temporarystackSize)
    HQTRACE(TRUE, ("temporarystack underflow")) ;
  while (temporarystack.size > saved->temporarystackSize)
    pop(&temporarystack) ;

  /* We do not currently attempt to fix this one */
  if (executionstack.size != saved->executionstackSize)
    HQTRACE(TRUE, ("Execution stack size has unexpectedly changed")) ;

  /* Only fix this if we've been asked to */
  if (corecontext->savelevel != saved->savelevel) {
    HQTRACE(1, ("Save level has unexpectedly changed")) ;

    if (restoresavelevel && corecontext->savelevel > saved->savelevel) {
      if ( !ps_restore(pscontext, saved->savelevel))
        success = FALSE ;
    }
  }

  if (!success)
    return FAILURE(FALSE) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Log stripped */
