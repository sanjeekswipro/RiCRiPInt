/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:stacks.c(EBDSDK_P.1) $
 * $Id: src:stacks.c,v 1.25.1.1.1.1 2013/12/19 11:25:00 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support for stacks of objects
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "mm.h"
#include "mps.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "objimpl.h"
#include "gcscan.h"


static Bool newStackFrame(STACK *thestack) ;


/* ----------------------------------------------------------------------------
   function:            push(..)           author:              Andrew Cave
   creation date:       21_Aug-1987        last modification:   04-Feb-1987
   arguments:           obj .
   description:

   This procedure pushes the data object as given by obj onto the given stack

   errors:
   VMerror.

---------------------------------------------------------------------------- */
Bool push(register OBJECT *obj, register STACK *thestack)
{
  register uint32 temp ;
  register OBJECT *tempo ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(obj, "No object to push") ;
  HQASSERT(thestack, "No stack to push onto") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  temp = ( ++theIStackSize( thestack )) ;
  if ( temp >= FRAMESIZE ) {
    temp &= FRAMESIZE - 1 ;
    if ( temp == 0 )
      if ( ! newStackFrame( thestack ))
        return FALSE ;
  }
  tempo = ( & theIFrameOList( theIStackFrame( thestack ))[ temp ] ) ;

  /* The destination stack slot properties are initialised lazily, right
     now. */
  theMark(*tempo) = ISLOCAL | ISNOTVM | SAVEMASK ;
  Copy( tempo, obj ) ;

  return TRUE ;
}

Bool push2(OBJECT *o1, OBJECT *o2, STACK *thestack)
{
  register uint32 temp ;
  register OBJECT *d ;
  SFRAME *frame ;

  HQASSERT(o1, "No object to push") ;
  HQASSERT(o2, "No second object to push") ;
  HQASSERT(thestack, "No stack to push onto") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  frame = theIStackFrame(thestack) ;
  HQASSERT(frame, "Stack has no frame attached") ;

  temp = theIStackSize( thestack ) ;
  if (( temp + 2 ) >= FRAMESIZE ) {
    if ( push(o1, thestack) &&
         push(o2, thestack) )
      return TRUE ;

    /* The only things that can cause push() to fail are either reaching the
       stack limit, or failing to allocate a new frame. Since the number of
       objects we are trying to push is smaller than FRAMESIZE, we can at most
       have attempted to add one frame. If the frame allocation fails, the
       stack is left with the old frame intact. Therefore, we assert that on
       failure the frame is as it was before the push operations, and that all
       we need to do is to restore the original stack size. */
    HQASSERT(theIStackFrame(thestack) == frame,
             "Frame has changed in unsuccessful push") ;
    theIStackSize(thestack) = temp ;

    return FALSE ;
  }

  theIStackSize( thestack ) = temp + 2 ;

  d = &theIFrameOList(frame)[temp + 1] ;
  /* The destination stack slot properties are initialised lazily, right
     now. */
  theMark(d[0]) = ISLOCAL | ISNOTVM | SAVEMASK ;
  theMark(d[1]) = ISLOCAL | ISNOTVM | SAVEMASK ;

  OCopy(d[0], *o1) ;
  OCopy(d[1], *o2) ;

  return TRUE ;
}

Bool push3(OBJECT *o1, OBJECT *o2, OBJECT *o3, STACK *thestack)
{
  register uint32 temp ;
  register OBJECT *d ;
  SFRAME *frame ;

  HQASSERT(o1, "No object to push") ;
  HQASSERT(o2, "No second object to push") ;
  HQASSERT(o3, "No third object to push") ;
  HQASSERT(thestack, "No stack to push onto") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  frame = theIStackFrame(thestack) ;
  HQASSERT(frame, "Stack has no frame attached") ;

  temp = theIStackSize( thestack ) ;
  if (( temp + 3 ) >= FRAMESIZE ) {
    if ( push(o1, thestack) &&
         push(o2, thestack) &&
         push(o3, thestack) )
      return TRUE ;

    /* The only things that can cause push() to fail are either reaching the
       stack limit, or failing to allocate a new frame. Since the number of
       objects we are trying to push is smaller than FRAMESIZE, we can at most
       have attempted to add one frame. If the frame allocation fails, the
       stack is left with the old frame intact. Therefore, we assert that on
       failure the frame is as it was before the push operations, and that all
       we need to do is to restore the original stack size. */
    HQASSERT(theIStackFrame(thestack) == frame,
             "Frame has changed in unsuccessful push") ;
    theIStackSize(thestack) = temp ;

    return FALSE ;
  }

  theIStackSize( thestack ) = temp + 3 ;

  d = &theIFrameOList(frame)[temp + 1] ;
  /* The destination stack slot properties are initialised lazily, right
     now. */
  theMark(d[0]) = ISLOCAL | ISNOTVM | SAVEMASK ;
  theMark(d[1]) = ISLOCAL | ISNOTVM | SAVEMASK ;
  theMark(d[2]) = ISLOCAL | ISNOTVM | SAVEMASK ;

  OCopy(d[0], *o1) ;
  OCopy(d[1], *o2) ;
  OCopy(d[2], *o3) ;

  return TRUE ;
}

Bool push4(OBJECT *o1, OBJECT *o2, OBJECT *o3 ,OBJECT *o4, STACK *thestack)
{
  register uint32 temp ;
  register OBJECT *d ;
  SFRAME *frame ;

  HQASSERT(o1, "No object to push") ;
  HQASSERT(o2, "No second object to push") ;
  HQASSERT(o3, "No third object to push") ;
  HQASSERT(o4, "No fourth object to push") ;
  HQASSERT(thestack, "No stack to push onto") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  frame = theIStackFrame(thestack) ;
  HQASSERT(frame, "Stack has no frame attached") ;

  temp = theIStackSize( thestack ) ;
  if (( temp + 4 ) >= FRAMESIZE ) {
    if ( push(o1, thestack) &&
         push(o2, thestack) &&
         push(o3, thestack) &&
         push(o4, thestack) )
      return TRUE ;

    /* The only things that can cause push() to fail are either reaching the
       stack limit, or failing to allocate a new frame. Since the number of
       objects we are trying to push is smaller than FRAMESIZE, we can at most
       have attempted to add one frame. If the frame allocation fails, the
       stack is left with the old frame intact. Therefore, we assert that on
       failure the frame is as it was before the push operations, and that all
       we need to do is to restore the original stack size. */
    HQASSERT(theIStackFrame(thestack) == frame,
             "Frame has changed in unsuccessful push") ;
    theIStackSize(thestack) = temp ;

    return FALSE ;
   }

  theIStackSize( thestack ) = temp + 4 ;

  d = &theIFrameOList(frame)[ temp + 1 ] ;
  /* The destination stack slot properties are initialised lazily, right
     now. */
  theMark(d[0]) = ISLOCAL | ISNOTVM | SAVEMASK ;
  theMark(d[1]) = ISLOCAL | ISNOTVM | SAVEMASK ;
  theMark(d[2]) = ISLOCAL | ISNOTVM | SAVEMASK ;
  theMark(d[3]) = ISLOCAL | ISNOTVM | SAVEMASK ;

  OCopy(d[0], *o1) ;
  OCopy(d[1], *o2) ;
  OCopy(d[2], *o3) ;
  OCopy(d[3], *o4) ;

  return TRUE ;
}

/* Push an integer onto the designated stack */
Bool stack_push_integer(int32 i, STACK *thestack)
{
  OBJECT iobj = OBJECT_NOTVM_INTEGER(0) ;
  oInteger(iobj) = i ;
  return push(&iobj, thestack) ;
}

/* Push a real or infinity on the designated stack. */
Bool stack_push_real(SYSTEMVALUE r, STACK *thestack)
{
  OBJECT robj = OBJECT_NOTVM_INFINITY ;
  object_store_XPF(&robj, r) ;
  return push(&robj, thestack) ;
}

/* Push a number on the designated stack. If representable as an integer,
   an integer is pushed, otherwise a real or infinity. */
Bool stack_push_numeric(SYSTEMVALUE n, STACK *thestack)
{
  OBJECT nobj = OBJECT_NOTVM_NOTHING ;
  object_store_numeric(&nobj, n) ;
  return push(&nobj, thestack) ;
}

/* Get one or more numbers from the top of a stack into a SYSTEMVALUE array.
   The array must be long enough to hold all of the values; it is the
   caller's responsibility to ensure it is. */
Bool stack_get_numeric(STACK *thestack, SYSTEMVALUE *numbers, int32 count)
{
  SFRAME *theframe ;
  int32 thesize ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(thestack, "No stack to get numbers from") ;
  HQASSERT(numbers, "Nowhere to put numbers") ;
  HQASSERT(count > 0, "Nothing to extract from stack") ;

  thesize = theStackSize(*thestack) ;
  if ( thesize < count - 1 )
    return error_handler(STACKUNDERFLOW) ;

  /* Top of the stack goes in the last number. Optimise the stack access by
     noting the frame index and pointer, and stepping it when exhausted. */
  numbers += count ;
  thesize &= FRAMESIZE - 1 ;
  theframe = theStackFrame(*thestack) ;

  while ( count > 0 ) {
    --numbers ;

    HQASSERT(theframe, "No stack frame for entry") ;
    if ( !object_get_numeric(&theIFrameOList(theframe)[thesize], numbers) )
      return FALSE ;

    if ( thesize-- == 0 ) {
      thesize = FRAMESIZE - 1 ;
      theframe = theframe->link;
    }

    --count ;
  }

  return TRUE ;
}

/* Get one or more numbers from the top of a stack into a USERVALUE array.
   The array must be long enough to hold all of the values; it is the
   caller's responsibility to ensure it is. */
Bool stack_get_reals(STACK *thestack, USERVALUE *numbers, int32 count)
{
  SFRAME *theframe ;
  int32 thesize ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(thestack, "No stack to get numbers from") ;
  HQASSERT(numbers, "Nowhere to put numbers") ;
  HQASSERT(count > 0, "Nothing to extract from stack") ;

  thesize = theStackSize(*thestack) ;
  if ( thesize < count - 1 )
    return error_handler(STACKUNDERFLOW) ;

  /* Top of the stack goes in the last number. Optimise the stack access by
     noting the frame index and pointer, and stepping it when exhausted. */
  numbers += count ;
  thesize &= FRAMESIZE - 1 ;
  theframe = theStackFrame(*thestack) ;

  while ( count > 0 ) {
    --numbers ;

    HQASSERT(theframe, "No stack frame for entry") ;
    if ( !object_get_real(&theIFrameOList(theframe)[thesize], numbers) )
      return FALSE ;

    if ( thesize-- == 0 ) {
      thesize = FRAMESIZE - 1 ;
      theframe = theframe->link;
    }

    --count ;
  }

  return TRUE ;
}

/* Get one or more integers from the top of a stack into an array. The array
   must be long enough to hold all of the values; it is the caller's
   responsibility to ensure it is. */
Bool stack_get_integers(STACK *thestack, int32 *numbers, int32 count)
{
  SFRAME *theframe ;
  int32 thesize ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(thestack, "No stack to get numbers from") ;
  HQASSERT(numbers, "Nowhere to put numbers") ;
  HQASSERT(count > 0, "Nothing to extract from stack") ;

  thesize = theStackSize(*thestack) ;
  if ( thesize < count - 1 )
    return error_handler(STACKUNDERFLOW) ;

  /* Top of the stack goes in the last number. Optimise the stack access by
     noting the frame index and pointer, and stepping it when exhausted. */
  numbers += count ;
  thesize &= FRAMESIZE - 1 ;
  theframe = theStackFrame(*thestack) ;

  while ( count > 0 ) {
    OBJECT *theo ;

    HQASSERT(theframe, "No stack frame for entry") ;

    theo = &theIFrameOList(theframe)[thesize] ;

    if ( oType(*theo) != OINTEGER )
      return error_handler(TYPECHECK) ;

    *--numbers = oInteger(*theo) ;

    if ( thesize-- == 0 ) {
      thesize = FRAMESIZE - 1 ;
      theframe = theframe->link;
    }

    --count ;
  }

  return TRUE ;
}

static Bool newStackFrame(STACK *thestack)
{
  SFRAME *newframe ;

  HQASSERT(thestack, "No stack to extend") ;
  HQASSERT(thestack->limit >= 0, "Stack limit is negative") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  if ( theIStackSize( thestack ) >= thestack->limit ) {
    static int32 errcode[N_STACK_TYPES] = {
      STACKOVERFLOW, EXECSTACKOVERFLOW, DICTSTACKOVERFLOW
    } ;

    --theIStackSize( thestack ) ;
    return error_handler( errcode[thestack->type] ) ;
  }
  if ( NULL == (newframe = mm_alloc(mm_pool_temp, sizeof(SFRAME),
                                    MM_ALLOC_CLASS_STACK_FRAME)) ) {
    --theIStackSize( thestack ) ;
    return error_handler( VMERROR ) ;
  }

  newframe->link = theIStackFrame( thestack ) ;
  theIStackFrame( thestack ) = newframe ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            pop(..)            author:              Andrew Cave
   creation date:       21-Aug-1987        last modification:   04-Feb-1987
   arguments:           none .
   description:

  This procedure pops the data object on top of the given stack.  Note that
   no checking is made to see if the stack is not empty.

---------------------------------------------------------------------------- */
void slowpop(STACK *thestack)
{
  SFRAME  *tempframe ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(thestack, "No stack to pop") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  if ( theIStackSize( thestack ) >= FRAMESIZE )
    if ( (theIStackSize(thestack) & (FRAMESIZE - 1)) == 0 ) {
      tempframe = theIStackFrame( thestack )->link;
      mm_free(mm_pool_temp, (mm_addr_t)theIStackFrame(thestack),
              sizeof(SFRAME)) ;
      theIStackFrame( thestack ) = tempframe ;
    }
  --theIStackSize( thestack ) ;
}

/* ----------------------------------------------------------------------------
   function:            myindex(..)          author:              Andrew Cave
   creation date:       21-Aug-1987        last modification:   05-Mar-1987
   arguments:           down .
   description:

  This procedure  returns the data object a distance 'down' from the top of
  the given stack. Note that no checking is made to see if the stack is the
  correct size.

---------------------------------------------------------------------------- */
OBJECT *stackindex(int32 down, STACK *thestack)
{
  uint32 thesize ;
  SFRAME *theframe ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(thestack, "No stack to index") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;
  HQASSERT(down >= 0, "Stack index is negative") ;
  HQASSERT(thestack->size >= down, "Stack index is too large for stack") ;

  theframe = theIStackFrame( thestack ) ;
  thesize = theIStackSize( thestack ) ;

  if ( thesize >= FRAMESIZE ) {
    down = down - ( thesize & (FRAMESIZE - 1) ) ;
    while ( down > 0 ) {
      theframe = theframe->link;
      down = down - FRAMESIZE ;
    }
    thesize = 0 ;
  }
  return ( & theIFrameOList( theframe ) [ thesize - down ] ) ;
}

/* ----------------------------------------------------------------------------
   function:            npop(..)           author:              Andrew Cave
   creation date:       18-Jun-1987        last modification:   04-Feb-1987
   arguments:           n .
   description:

  This procedure pops n data objects off the top of the given stack.  Note that
  no checking is made to see if the stack is not empty, and n must be less than
  the size of the frames used - not really a limitation.

---------------------------------------------------------------------------- */
void slownpop(int32 n, STACK *thestack)
{
  int32 temp ;
  SFRAME *tempframe ;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;
  HQASSERT(thestack, "No stack to pop") ;
  HQASSERT(thestack->type == STACK_TYPE_OPERAND ||
           thestack->type == STACK_TYPE_EXECUTION ||
           thestack->type == STACK_TYPE_DICTIONARY, "Stack type invalid") ;

  temp = theIStackSize( thestack ) ;
  if ( temp >= FRAMESIZE ) {
    while (n >= FRAMESIZE) {
      tempframe = theIStackFrame( thestack )->link;
      mm_free(mm_pool_temp, (mm_addr_t)theIStackFrame(thestack),
              sizeof(SFRAME)) ;
      theIStackFrame( thestack ) = tempframe ;
      n -= FRAMESIZE;
      temp -= FRAMESIZE;
    }
    if ( n > (temp & (FRAMESIZE - 1)) ) {
      tempframe = theIStackFrame( thestack )->link;
      if ( tempframe != NULL ) {
        mm_free(mm_pool_temp, (mm_addr_t)theIStackFrame(thestack),
              sizeof(SFRAME)) ;
        theIStackFrame( thestack ) = tempframe ;
      }
    }
  }
  theIStackSize( thestack ) = temp - n;
}


/*  ps_scan_stack -- a GC scanning fn for a stack */
mps_res_t MPS_CALL ps_scan_stack(mps_ss_t ss, STACK *stack)
{
  int32 nLeft, nThisFrame;
  SFRAME *frame;
  mps_res_t res;

  HQASSERT((FRAMESIZE & (FRAMESIZE - 1)) == 0,
           "Stack framesize is not a power of two") ;

  frame = theIStackFrame( stack );
  nLeft = theIStackSize( stack ) + 1; /* There's one more element than size! */
  nThisFrame = ( theIStackSize( stack ) & (FRAMESIZE - 1) ) + 1;
  while ( nLeft > 0 ) {
    /* Scan all active slots in this frame */
    res = ps_scan( ss, (mps_addr_t) & theIFrameOList( frame ) [ 0 ],
                   (mps_addr_t) & theIFrameOList( frame )[ nThisFrame ]);
    if ( res != MPS_RES_OK ) return res;
    frame = frame->link;
    nLeft -= nThisFrame; nThisFrame = FRAMESIZE;
  }
  return MPS_RES_OK;
}


/*
Log stripped */
