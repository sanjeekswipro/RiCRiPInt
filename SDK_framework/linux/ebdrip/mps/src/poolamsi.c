/* impl.c.poolamsi: AUTOMATIC MARK & SWEEP POOL CLASS C INTERFACE
 *
 * $Id: poolamsi.c,v 1.5.10.1.1.1 2013/12/19 11:27:07 anon Exp $
 * $HopeName: MMsrc!poolamsi.c(EBDSDK_P.1) $
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#include "mpscams.h"
#include "mps.h"
#include "poolams.h"

SRCID(poolamsi, "$Id: poolamsi.c,v 1.5.10.1.1.1 2013/12/19 11:27:07 anon Exp $");


/* mps_class_ams -- return the AMS pool class descriptor */

mps_class_t MPS_CALL mps_class_ams(void)
{
  return (mps_class_t)AMSPoolClassGet();
}


/* mps_class_ams_debug -- return the AMS (debug) pool class descriptor */

mps_class_t MPS_CALL mps_class_ams_debug(void)
{
  return (mps_class_t)AMSDebugPoolClassGet();
}
