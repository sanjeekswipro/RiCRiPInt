/* impl.h.mpscamc: MEMORY POOL SYSTEM CLASS "AMC"
 *
 * $Id: export:mpscamc.h,v 1.10.11.1.1.1 2013/12/19 11:27:03 anon Exp $
 * $HopeName: SWmps!export:mpscamc.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef mpscamc_h
#define mpscamc_h

#include "mps.h"

extern mps_class_t MPS_CALL mps_class_amc(void);
extern mps_class_t MPS_CALL mps_class_amcw(void);
extern mps_class_t MPS_CALL mps_class_amcz(void);

extern void MPS_CALL mps_amc_apply(mps_pool_t,
                                   void (MPS_CALL *)(mps_addr_t, void *, size_t),
                                   void *, size_t);

#endif /* mpscamc_h */
