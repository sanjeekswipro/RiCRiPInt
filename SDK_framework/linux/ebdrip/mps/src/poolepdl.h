/*  impl.h.poolepdl: ELECTRONIC PUBLISHING DISPLAY LIST POOL
 *
 * $Id: poolepdl.h,v 1.4.31.1.1.1 2013/12/19 11:27:04 anon Exp $
 * $HopeName: MMsrc!poolepdl.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * 
 * .purpose: This is a pool class for EPcore display lists. See
 * impl.c.poolepdl.purpose.
 * 
 * .status: See impl.c.poolepdl.status.
 * 
 * .readership: MM developers
 * 
 * .design: design.mps.poolepdl
 * 
 * .req: See impl.c.poolepdl.req
 */

#ifndef poolepdl_h
#define poolepdl_h

#include "mpm.h"

extern PoolClass PoolClassEPDL(void);
extern PoolClass PoolClassEPDR(void);

#endif /* poolepdl_h */
