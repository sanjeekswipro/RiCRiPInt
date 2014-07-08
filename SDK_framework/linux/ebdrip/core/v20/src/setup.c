/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:setup.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS setup operators
 */

#include "core.h"
#include "objects.h"
#include "objstack.h"
#include "stacks.h"
#include "control.h"
#include "namedef_.h"

#include "setup.h"

/* ----------------------------------------------------------------------------
   function:            mark_()            author:              Andrew Cave
   creation date:       10-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 184.

---------------------------------------------------------------------------- */
Bool mark_(ps_context_t *pscontext)
{
  OBJECT omark = OBJECT_NOTVM_MARK ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(&omark, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            null_()            author:              Andrew Cave
   creation date:       10-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 189.

---------------------------------------------------------------------------- */
Bool null_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(&onull, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            xsetup_operator(..) author:              Andrew Cave
   creation date:       10-Oct-1987         last modification:   ##-###-####
   arguments:           theop .
   description:

   This procedure creates an operator object on the top of the execution stack.
   The argument type is very  important - a non-zero value indicates a looping
   context which has that number of arguments on the execution stack.  A value
   of zero means that it is one of the font plotting control operators.

---------------------------------------------------------------------------- */
Bool xsetup_operator(int32 opnumber, int32 type)
/* opnumber is a NAME_... thingy */
{
  OBJECT setup_nothing = OBJECT_NOTVM_NOTHING ;

  theTags(setup_nothing) = ONOTHING | EXECUTABLE | READ_ONLY ;
  theLen(setup_nothing) = CAST_TO_UINT16(type) ;
  oOp(setup_nothing) = &system_ops[opnumber];

  execStackSizeNotChanged = FALSE ;
  return push(&setup_nothing, &executionstack) ;
}



/* Log stripped */
