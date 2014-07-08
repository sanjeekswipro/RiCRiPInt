/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:objstack.h(EBDSDK_P.1) $
 * $Id: export:objstack.h,v 1.23.2.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support for stacks of objects.
 */

#ifndef __OBJSTACK_H__
#define __OBJSTACK_H__

#include "objects.h" /* Needs full OBJECT definition */

/* CONSTANT DEFINITIONS */

#define THEFRAMESIZE       512

#if (THEFRAMESIZE & (THEFRAMESIZE-1)) != 0
#error THEFRAMESIZE must be a power of two
#endif

#define FRAMESIZE           ((int32)THEFRAMESIZE)
#define EMPTY_STACK         -1

/* STRUCTURE DEFINITIONS */

enum { /* Stack type codes. Used as index, don't alter values */
  STACK_TYPE_OPERAND = 0,
  STACK_TYPE_EXECUTION,
  STACK_TYPE_DICTIONARY,
  N_STACK_TYPES
} ;

typedef struct SFRAME {
  struct SFRAME *link    ;
  OBJECT objs[FRAMESIZE] ;
} SFRAME ;

struct STACK {
  int32 size ;     /* One less than the number of elements on the stack */
  SFRAME *fptr ;
  int32 limit ;   /* Max elements on stack */
  int32 type ;    /* Type of stack */
} ;

/* ACCESSOR MACRO DEFINITIONS */

/* PLEASE NOTE: the stack "size" is a misnomer. It actually stores the index
   of the last entry in the stack, which is one less than the size. So "size"
   is -1 when empty, 0 if there is one entry, etc. */
#define theStackSize(val)     ((val).size)
#define theIStackSize(val)    ((val)->size)
#define theStackFrame(val)    ((val).fptr)
#define theIStackFrame(val)   ((val)->fptr)
#define theIFrameOList(frame) ((frame)->objs)

#define isEmpty(_stack)         (theStackSize((_stack))<0)
#define EmptyStack(_stacksize)  ((_stacksize)<0)

#define TopStack(_stack,_stacksize) \
  (&theIFrameOList(theStackFrame(_stack))[(_stacksize)&(FRAMESIZE-1)])
#define theTop(_stack) \
  (&theIFrameOList(theStackFrame((_stack)))[theStackSize((_stack))&(FRAMESIZE-1)])
#define theITop(_stack) \
  (&theIFrameOList(theIStackFrame((_stack)))[theIStackSize((_stack))&(FRAMESIZE-1)])

#define fastStackAccess(_stack)         (theStackSize((_stack))<FRAMESIZE)
#define fastIStackAccess(_stack)        (theIStackSize((_stack))<FRAMESIZE)

#define contiguousStackSize(_stack)     ((1+theStackSize(_stack))&(FRAMESIZE-1))

#define pop(_stack) MACRO_START \
  if ( theIStackSize((_stack)) >= FRAMESIZE ) \
    slowpop((_stack)) ; \
  else \
    theIStackSize((_stack)) -= 1 ; \
  HQASSERT( theIStackSize((_stack)) >= EMPTY_STACK, "too many items pop'd off stack" ) ; \
MACRO_END

#define npop(_n,_stack) MACRO_START \
  if ( theIStackSize((_stack)) >= FRAMESIZE ) \
    slownpop((_n),(_stack)) ; \
  else \
    theIStackSize((_stack)) -= (_n) ; \
  HQASSERT( theIStackSize((_stack)) >= EMPTY_STACK, "too many items npop'd off stack" ) ; \
MACRO_END

Bool push(register OBJECT *obj, register STACK *thestack);
Bool push2(OBJECT *o1, OBJECT *o2, STACK *thestack);
Bool push3(OBJECT *o1, OBJECT *o2, OBJECT *o3, STACK *thestack);
Bool push4(OBJECT *o1, OBJECT *o2, OBJECT *o3, OBJECT *o4, STACK *thestack);

/* Replacements for setup_real, setup_integer. stack_push_number pushes the
   object as an integer if possible, a real number otherwise. */
Bool stack_push_integer(int32 i, STACK *thestack) ;
Bool stack_push_real(SYSTEMVALUE r, STACK *thestack) ;
Bool stack_push_numeric(SYSTEMVALUE n, STACK *thestack) ;

void slowpop(STACK *thestack);
OBJECT *stackindex(int32 down, STACK *thestack);
void slownpop(int32 n, STACK *thestack);

/* Get one or more numbers off a designated stack, using object_get_numeric. */
Bool stack_get_numeric(STACK *thestack, SYSTEMVALUE *numbers, int32 count) ;

/* Get one or more numbers off a designated stack, using object_get_real. */
Bool stack_get_reals(STACK *thestack, USERVALUE *numbers, int32 count) ;

/* Get one or more integers off a designated stack. Only integers are
   accepted, not integer-valued reals. */
Bool stack_get_integers(STACK *thestack, int32 *numbers, int32 count) ;

/*
Log stripped */
#endif
