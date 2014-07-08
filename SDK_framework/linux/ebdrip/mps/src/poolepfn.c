/* impl.c.poolepfn: ELECTRONIC PUBLISHING FINALIZING VM CLASS
 *
 * $Id: poolepfn.c,v 1.10.10.1.1.1 2013/12/19 11:27:06 anon Exp $
 * $HopeName: MMsrc!poolepfn.c(EBDSDK_P.1) $
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * .purpose: This is the implementation of the finalizing PostScript
 * Virtual Memory pool class.
 *
 * .design: A subclass of EPVM, see design.mps.poolepvm.  */

#include "mpscepvm.h"
#include "mps.h"
#include "dbgpool.h"
#include "poolepvm.h"
#include "poolams.h"
#include "protocol.h"
#include "mpm.h"
#include "chain.h"
#include <stdarg.h>

SRCID(poolepfn, "$Id: poolepfn.c,v 1.10.10.1.1.1 2013/12/19 11:27:06 anon Exp $");


/* EPFNDebugStruct -- structure for a debug subclass */

typedef struct EPFNDebugStruct {
  EPVMStruct epvmStruct;       /* EPVM structure */
  PoolDebugMixinStruct debug;  /* debug mixin */
} EPFNDebugStruct;

typedef EPFNDebugStruct *EPFNDebug;

#define EPVM2EPFNDebug(epvm)  ((EPFNDebug)(epvm))


/* EPFNSegStruct -- EPFN segment instances */

#define EPFNSegSig ((Sig)0x519EBF49) /* SIGnature EPFN seG */

typedef struct EPFNSegStruct {
  EPVMSegStruct epvmSegStruct; /* superclass fields must come first */
  BT eligibleTable; /* reset = an object not already finalized */
  Sig sig; /* design.mps.pool.outer-structure.sig */
} EPFNSegStruct;

typedef EPFNSegStruct *EPFNSeg;

#define Seg2EPFNSeg(seg) ((EPFNSeg)(seg))
#define EPFNSeg2Seg(epfnSeg) ((Seg)(epfnSeg))


/* forward declarations of functions */

static Res EPFNSegInit(Seg seg, Pool pool, Addr base, Size size, 
                       Bool reservoirPermit, va_list args);
static void EPFNSegFinish(Seg seg);
static Res EPFNSegDescribe(Seg seg, mps_lib_FILE *stream);
static Res EPFNInit(Pool pool, va_list args);
static Res EPFNCondemn(Pool pool, Trace trace, Seg seg);
static Res EPFNBufferFill(Addr *baseReturn, Addr *limitReturn,
                          Pool pool, Buffer buffer, Size size,
                          Bool withReservoirPermit);
static void EPFNBufferEmpty(Pool pool, Buffer buffer, Addr init, Addr limit);
static Res EPFNScan(Bool *totalReturn, ScanState ss, Pool pool, Seg seg);


/* EPFNSegClass -- Class definition for EPFN segments */

DEFINE_SEG_CLASS(EPFNSegClass, class)
{
  INHERIT_CLASS(class, EPVMSegClass);
  class->name = "EPFNSEG";
  class->size = sizeof(EPFNSegStruct);
  class->init = EPFNSegInit;
  class->finish = EPFNSegFinish;
  class->describe = EPFNSegDescribe;
}


/* EPFNPoolClass -- the pool class definition */

DEFINE_ALIAS_CLASS(EPFNPoolClass, AMSPoolClass, this)
{
  AbstractCollectPoolClass acpc = &this->acpClass;

  INHERIT_CLASS(this, EPVMPoolClass);
  acpc->name = "EPFN";
  acpc->size = sizeof(EPVMStruct);
  acpc->offset = offsetof(EPVMStruct, amsStruct.poolStruct);
  acpc->init = EPFNInit;
  /* Finish method not needed as the guardian segment is destroyed by AMS. */
  acpc->bufferClass = RankBufClassGet;
  acpc->bufferFill = EPFNBufferFill;
  acpc->bufferEmpty = EPFNBufferEmpty;
  acpc->whiten = EPFNCondemn;
  acpc->scan = EPFNScan;
  this->segClass = EPFNSegClassGet;
}


/* EPFNDebugMixin - find debug mixin in class EPFNDebug */

static PoolDebugMixin EPFNDebugMixin(Pool pool)
{
  EPVM epvm;

  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);
  /* Can't check EPFNDebug, because this is called during init */
  return &(EPVM2EPFNDebug(epvm)->debug);
}


/* EPFNDebugPoolClass -- the class definition for the debug version */

DEFINE_ALIAS_CLASS(EPFNDebugPoolClass, AMSPoolClass, this)
{
  AbstractCollectPoolClass acpc = &this->acpClass;

  INHERIT_CLASS(this, EPFNPoolClass);
  PoolClassMixInDebug(acpc);
  acpc->name = "EPFNDBG";
  acpc->size = sizeof(EPFNDebugStruct);
  acpc->debugMixin = EPFNDebugMixin;
}


/* EPFNMessage -- a message structure for EPFN finalization messages */

#define EPFNMessageSig ((Sig)0x519EBF43) /* SIGnature EPFN Message */

typedef struct EPFNMessageStruct {
  MessageStruct messageStruct;
  Ref ref;
  Sig sig;
} EPFNMessageStruct;

typedef EPFNMessageStruct *EPFNMessage;

#define Message2EPFNMessage(message) \
  PARENT(EPFNMessageStruct, messageStruct, message)

#define EPFNMessage2Message(epfnMessage) (&(epfnMessage)->messageStruct)


/* EPFNSegCheck - check method of EPFNSegs */

static Bool EPFNSegCheck(EPFNSeg epfnSeg)
{
  Seg seg;
  Index i;

  CHECKS(EPFNSeg, epfnSeg);
  CHECKL(EPVMSegCheck(&epfnSeg->epvmSegStruct));
  seg = EPFNSeg2Seg(epfnSeg);
  for (i = 0; i < Seg2AMSSeg(seg)->grains; ++i)
    CHECKL(AMS_ALLOCED(seg, i) || BTGet(epfnSeg->eligibleTable, i));
  return TRUE;
}


/* EPFNMessage* -- Implementation of EPFN's MessageClass */


/* EPFNMessageDelete -- deletes the message (frees up the guardian) */

static void EPFNMessageDelete(Message message)
{
  ControlFree(MessageArena(message), Message2EPFNMessage(message),
              sizeof(EPFNMessageStruct));
}


/* EPFNMessageFinalizationRef -- extract the finalized reference from the msg */

static void EPFNMessageFinalizationRef(Ref *refReturn,
                                       Arena arena, Message message)
{
  AVER(refReturn != NULL);
  AVERT(Arena, arena);
  AVERT(Message, message);

  *refReturn = Message2EPFNMessage(message)->ref;
}


static MessageClassStruct EPFNMessageClassStruct = {
  MessageClassSig,
  "EPFNFinal",
  EPFNMessageDelete,
  EPFNMessageFinalizationRef,
  MessageNoGCLiveSize,
  MessageNoGCCondemnedSize,
  MessageNoGCNotCondemnedSize,
  MessageClassSig
};


/* EPFNInit -- the pool class initialization method
 * 
 * Takes arguments like EPVM.  Creates a "guardian" segment to control
 * finalization: its only purpose is to be scanned.  */

static Res EPFNInit(Pool pool, va_list args)
{
  Res res;
  Seg seg;

  /* arguments checked by superclass */

  res = (*POOL_SUPERCLASS(EPFNPoolClass)->init)(pool, args);
  if (res != ResOK)
    return res;
  /* Allocate the guardian seg, as small as possible. */
  res = SegAlloc(&seg, GCSegClassGet(), SegPrefDefault(),
                 ArenaAlign(PoolArena(pool)), pool, FALSE);
  if (res != ResOK)
    goto failAlloc;
  SegSetRankAndSummary(seg, RankSetSingle(RankWEAKFINAL), RefSetUNIV);

  return ResOK;

failAlloc:
  (*POOL_SUPERCLASS(EPFNPoolClass)->finish)(pool);
  return res;
}


/* EPFNSegInit -- initialise an EPFN segment */

static Res EPFNSegInit(Seg seg, Pool pool, Addr base, Size size, 
                       Bool reservoirPermit, va_list args)
{
  EPFNSeg epfnSeg;
  Res res;

  /* arguments checked by superclass */

  /* Initialize the superclass fields first. */
  res = (*SEG_SUPERCLASS(EPFNSegClass)->init)(seg, pool, base, size,
                                              reservoirPermit, args);
  if (res != ResOK)
    return res;

  epfnSeg = Seg2EPFNSeg(seg);
  res = BTCreate(&epfnSeg->eligibleTable, PoolArena(pool),
                 Seg2AMSSeg(seg)->grains);
  if (res != ResOK)
    goto failAlloc;
  BTSetRange(epfnSeg->eligibleTable, 0, Seg2AMSSeg(seg)->grains);

  epfnSeg->sig = EPFNSegSig;
  AVERT(EPFNSeg, epfnSeg);

  return ResOK;

failAlloc:
  (*SEG_SUPERCLASS(EPFNSegClass)->finish)(seg);
  return res;
}


/* EPFNSegFinish -- Finish method for EPFN segments */

static void EPFNSegFinish(Seg seg)
{
  EPFNSeg epfnSeg;
  AMSSeg amsSeg;

  AVERT(Seg, seg);
  amsSeg = Seg2AMSSeg(seg);
  AVERT(AMSSeg, amsSeg);
  epfnSeg = Seg2EPFNSeg(seg);
  AVERT(EPFNSeg, epfnSeg);
  BTDestroy(epfnSeg->eligibleTable, PoolArena(AMS2Pool(amsSeg->ams)),
            amsSeg->grains);
  /* finish the superclass fields last */
  (*SEG_SUPERCLASS(EPFNSegClass)->finish)(seg);
}


/* EPFNSegDescribe -- describe an EPFN segment */

static Res EPFNSegDescribe(Seg seg, mps_lib_FILE *stream)
{
  Res res;
  EPFNSeg epfnSeg;

  /* .describe.check: Debugging tools don't AVER things. */
  if (!CHECKT(Seg, seg)) return ResFAIL;
  if (stream == NULL) return ResFAIL;
  epfnSeg = Seg2EPFNSeg(seg);
  if (!CHECKT(EPFNSeg, epfnSeg)) return ResFAIL;

  /* Describe the superclass fields first via next-method call */
  res = SEG_SUPERCLASS(EPFNSegClass)->describe(seg, stream);
  if (res != ResOK) return res;

  res = WriteF(stream,
               "  eligibleTable $P\n", (WriteFP)epfnSeg->eligibleTable,
               NULL);
  return res;
}


/* EPFNBufferFill -- the pool class buffer fill method, initializes eligibleTable */

static Res EPFNBufferFill(Addr *baseReturn, Addr *limitReturn,
                          Pool pool, Buffer buffer, Size size,
                          Bool withReservoirPermit)
{
  Res res;
  Seg seg;
  Bool found;

  /* Arguments checked by superclass. */

  /* Superclass does the real work of BufferFill. */
  res = POOL_SUPERCLASS(EPFNPoolClass)->bufferFill(baseReturn, limitReturn, pool,
                                                   buffer, size,
                                                   withReservoirPermit);
  if (res != ResOK)
    return res;

  found = SegOfAddr(&seg, PoolArena(pool), *baseReturn);
  AVER(found);
  AVERT(Seg, seg);
  /* Make the new objects eligible for finalization. */
  BTResRange(Seg2EPFNSeg(seg)->eligibleTable,
             AMS_ADDR_INDEX(seg, *baseReturn),
             AMS_ADDR_INDEX(seg, *limitReturn));

  return ResOK;
}


/* EPFNBufferEmpty -- the pool class buffer empty method */

static void EPFNBufferEmpty(Pool pool, Buffer buffer, Addr init, Addr limit)
{
  Seg seg;

  /* Arguments checked by superclass. */

  /* Superclass does the real work of BufferEmpty. */
  POOL_SUPERCLASS(EPFNPoolClass)->bufferEmpty(pool, buffer, init, limit);

  if (init < limit) {
    seg = BufferSeg(buffer);
    AVERT(Seg, seg);
    /* Make the unused range ineligible for finalization. */
    BTSetRange(Seg2EPFNSeg(seg)->eligibleTable,
               AMS_ADDR_INDEX(seg, init), AMS_ADDR_INDEX(seg, limit));
  }
}


/* EPFNCondemn -- the pool class condemn method */

static Res EPFNCondemn(Pool pool, Trace trace, Seg seg)
{
  Res res;

  AVERT(Pool, pool);
  AVERT(Trace, trace);
  AVERT(Seg, seg);

  if (SegRankSet(seg) != RankSetSingle(RankWEAKFINAL))
    /* Superclass does the real work of Condemn. */
    res = POOL_SUPERCLASS(EPFNPoolClass)->whiten(pool, trace, seg);
  else  /* Guardian segment is not condemned, will turn grey. */
    res = ResOK; /* Tell tracer this is OK. */
  return res;
}


/* epfnSegFinalize -- run finalizations for a white segment */

static Res epfnSegFinalize(ScanState ss, Pool pool, Seg seg)
{
  Arena arena;
  AMS ams;
  AMSSeg amsSeg;
  EPFNSeg epfnSeg;
  Count grains;
  Index whiteBase, whiteLimit; /* for white ranges */
  BT eligibleTable;
  Index finalBase, finalLimit; /* for eligible objs */
  Res res;

  ams = Pool2AMS(pool);
  AVERT(AMS, ams);

  arena = PoolArena(pool);
  amsSeg = Seg2AMSSeg(seg);
  AVERT(AMSSeg, amsSeg);
  /* It's a white seg, so it must have colour tables. */
  AVER(amsSeg->colourTablesInUse);
  grains = amsSeg->grains;
  epfnSeg = Seg2EPFNSeg(seg);
  AVERT(EPFNSeg, epfnSeg);

  eligibleTable = epfnSeg->eligibleTable;
  whiteLimit = 0;
  while (whiteLimit < grains
         && AMS_FIND_WHITE_RANGE(&whiteBase, &whiteLimit,
                                 seg, whiteLimit, grains)) {
    AVER(!AMS_IS_INVALID_COLOUR(seg, whiteBase));
    finalLimit = whiteBase;
    while (finalLimit < whiteLimit
           && BTFindShortResRange(&finalBase, &finalLimit,
                                  eligibleTable, finalLimit, whiteLimit, 1)) {
      void *p;
      EPFNMessage message;
      Ref base = AMS_INDEX_ADDR(seg, finalBase);
      mps_addr_t next;

      AVER(finalLimit == finalBase + 1);
      /* Resurrect the object. */
      /* No summary to update, so no TRACE_FIX1, call ss->fix directly. */
      (*ss->fix)(ss, &base);
      /* Post finalization message. */
      res = ControlAlloc(&p, arena, sizeof(struct EPFNMessageStruct), FALSE);
      if (res != ResOK) {
        /* Just let it survive, it'll be back next collection. */
        next = (*pool->format->skip)(base);
        finalLimit = AMS_ADDR_INDEX(seg, next);
      } else {
        message = (EPFNMessage)p;
        message->ref = base;
        MessageInit(arena, &message->messageStruct, &EPFNMessageClassStruct,
                    MessageTypeFINALIZATION);
        MessagePost(arena, &message->messageStruct);
        /* Don't finalize it again. */
        next = (*pool->format->skip)(base);
        finalLimit = AMS_ADDR_INDEX(seg, next);
        BTSetRange(eligibleTable, finalBase, finalLimit);
      }
    }
    ++whiteLimit; /* We know the next grain is not white. */
  }
  return ResOK;
}


/* EPFNScan -- the pool class scanning method
 *
 * This method pretends the guardian segment contains finalization
 * guardians for all the other segments. */

static Res EPFNScan(Bool *totalReturn, ScanState ss, Pool pool, Seg seg)
{
  /* Arguments checked only in the branch where the superclass doesn't. */

  if (ss->rank == RankWEAKFINAL) { /* Guardian segment, do finalization work. */
    EPVM epvm;
    Ring node, ring, nextNode;    /* for iterating over the segments */

    AVER(totalReturn != NULL);
    AVERT(ScanState, ss);
    AVERT(Pool, pool);
    epvm = Pool2EPVM(pool);
    AVERT(EPVM, epvm);
    AVER(IsSubclassPoly(pool->class, EPFNPoolClassGet()));
    AVER(SegCheck(seg));

    ring = PoolSegRing(pool);
    RING_FOR(node, ring, nextNode) {
      Seg dataSeg = SegOfPoolRing(node);

      AVERT(Seg, dataSeg);
      /* If it's white, run finalizations. */
      if (TraceSetInter(SegWhite(dataSeg), ss->traces) != TraceSetEMPTY) {
        Res res = epfnSegFinalize(ss, pool, dataSeg);
        if (res != ResOK) {
          *totalReturn = FALSE;
          return res;
        }
      }
    }
    *totalReturn = TRUE;
    return ResOK;
  } else {/* Normal data segment, let superclass scan it. */
    return POOL_SUPERCLASS(EPFNPoolClass)->scan(totalReturn, ss, pool, seg);
  }
}


/* mps_class_epfn -- return the pool class descriptor to the client */

mps_class_t MPS_CALL mps_class_epfn(void)
{
  return (mps_class_t)EPFNPoolClassGet();
}


/* mps_class_epfn_debug -- return the debug pool class descriptor */

mps_class_t MPS_CALL mps_class_epfn_debug(void)
{
  return (mps_class_t)EPFNDebugPoolClassGet();
}
