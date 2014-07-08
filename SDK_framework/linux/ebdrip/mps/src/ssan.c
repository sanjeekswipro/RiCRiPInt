/* impl.c.ssan: ANSI STACK SCANNER
 *
 * $Id: ssan.c,v 1.4.31.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!ssan.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 *
 * This module provides zero functionality.  It exists to feed the
 * linker (prevent linker errors).
 */

#include "mpmtypes.h"
#include "misc.h"
#include "ss.h"


SRCID(ssan, "$Id: ssan.c,v 1.4.31.1.1.1 2013/12/19 11:27:08 anon Exp $");


Res StackScan(ScanState ss, Addr *stackBot)
{
  UNUSED(ss); UNUSED(stackBot);
  return ResUNIMPL;
}
