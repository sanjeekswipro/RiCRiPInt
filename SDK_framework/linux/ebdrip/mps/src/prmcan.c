/* impl.c.prmcan: PROTECTION MUTATOR CONTEXT (ANSI)
 *
 * $Id: prmcan.c,v 1.3.31.1.1.1 2013/12/19 11:27:04 anon Exp $
 * $HopeName: MMsrc!prmcan.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 *
 * .design: See design.mps.prot for the generic design of the interface
 * which is implemented in this module including the contracts for the
 * functions.
 *
 * .purpose: This module implements the part of the protection module
 * that implements the MutatorFaultContext type.  In this ANSI version
 * none of the functions have a useful implementation.
 */

#include "mpm.h"

SRCID(prmcan, "$Id: prmcan.c,v 1.3.31.1.1.1 2013/12/19 11:27:04 anon Exp $");


/* ProtCanStepInstruction -- can the current instruction be single-stepped */

Bool ProtCanStepInstruction(MutatorFaultContext context)
{
  UNUSED(context);

  return FALSE;
}


/* ProtStepInstruction -- step over instruction by modifying context */

Res ProtStepInstruction(MutatorFaultContext context)
{
  UNUSED(context);

  return ResUNIMPL;
}
