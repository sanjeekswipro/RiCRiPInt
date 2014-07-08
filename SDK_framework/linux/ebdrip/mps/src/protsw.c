/* impl.c.protsw: SCRIPTWORKS MEMORY PROTECTION
 *
 * $Id: protsw.c,v 1.6.31.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!protsw.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 *
 * DESIGN
 *
 * Implements no protection.
 */

#include "mpm.h"

#ifndef PROTECTION_NONE
#error "protsw.c implements no protection, but PROTECTION_NONE is not set"
#endif

SRCID(protsw, "$Id: protsw.c,v 1.6.31.1.1.1 2013/12/19 11:27:08 anon Exp $");


/* ProtSetup -- global protection setup */

void ProtSetup(void)
{
  NOOP;
}


/* ProtSet -- set the protection for a page */

void ProtSet(Addr base, Addr limit, AccessSet pm)
{
  UNUSED(base); UNUSED(limit); UNUSED(pm);
  NOTREACHED;
}


/* ProtSync -- synchronize protection settings with hardware */

void ProtSync(Arena arena)
{
  UNUSED(arena);
  NOOP;
}


/* ProtTramp -- protection trampoline */

void ProtTramp(void **rReturn, void *(*f)(void *p, size_t s),
               void *p, size_t s)
{
  AVER(rReturn != NULL);
  AVER(FUNCHECK(f));
  /* Can't check p and s as they are interpreted by the client */

  *(rReturn) = (*(f))(p, s);
}
