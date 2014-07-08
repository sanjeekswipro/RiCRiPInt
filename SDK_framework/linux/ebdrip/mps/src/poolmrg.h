/*  impl.h.poolmrg: MANUAL RANK GUARDIAN POOL CLASS INTERFACE
 *
 *  $Id: poolmrg.h,v 1.6.11.1.1.1 2013/12/19 11:27:10 anon Exp $
 *  $HopeName: MMsrc!poolmrg.h(EBDSDK_P.1) $
 *
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef poolmrg_h
#define poolmrg_h

#include "mpmtypes.h"

typedef struct MRGStruct *MRG;

extern PoolClass PoolClassMRG(void);
extern Res MRGRegister(Pool, Ref);
extern Res MRGDeregister(Pool, Ref);

#endif /* poolmrg_h */
