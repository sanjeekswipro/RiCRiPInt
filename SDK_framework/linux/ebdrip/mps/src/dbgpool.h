/* impl.h.dbgpool: POOL DEBUG MIXIN
 *
 * $Id: dbgpool.h,v 1.10.11.1.1.1 2013/12/19 11:27:06 anon Exp $
 * $HopeName: MMsrc!dbgpool.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef dbgpool_h
#define dbgpool_h

#include "splay.h"
#include "mpmtypes.h"
#include <stdarg.h>


/* PoolDebugOptions -- option structure for debug pool init
 *
 * This must be kept in sync with impl.h.mps.mps_pool_debug_option_s.
 */

typedef struct PoolDebugOptionsStruct {
  void* fenceTemplate;
  Size  fenceSize;
  void* freeTemplate;
  Size  freeSize;
  Bool  keepTags;
  Size  debugInfoSize; /* size of the client's DebugInfo structure */
} PoolDebugOptionsStruct;

typedef PoolDebugOptionsStruct *PoolDebugOptions;


/* PoolDebugMixinStruct -- internal structure for debug mixins */

#define PoolDebugMixinSig ((Sig)0x519B0DB9)  /* SIGnature POol DeBuG */

typedef struct PoolDebugMixinStruct {
  Sig sig;
  Addr fenceTemplate;
  Size fenceSize;
  Addr freeTemplate;
  Size freeSize;
  Bool keepTags;
  Size tagSize; /* total size of the tag structure, client fields included */
  Pool tagPool;
  Count missingTags;
  SplayTreeStruct index;
} PoolDebugMixinStruct;


extern Bool PoolDebugMixinCheck(PoolDebugMixin dbg);

extern void PoolClassMixInDebug(PoolClass class);

extern void DebugPoolCheckFences(Pool pool);
extern void DebugPoolCheckFreeSpace(Pool pool);

extern void DebugPoolFreeSplat(Pool pool, Addr base, Addr limit);
extern void DebugPoolFreeCheck(Pool pool, Addr base, Addr limit);

typedef void (*ObjectsStepMethod)(Addr addr, Size size, Format fmt,
                                  Pool pool, DebugInfo info, void *p);
extern void TagWalk(Pool pool, ObjectsStepMethod step, void *p);


#endif /* dbgpool_h */
