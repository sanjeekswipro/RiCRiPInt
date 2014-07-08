/* impl.c.poolabs: ABSTRACT POOL CLASSES
 *
 * $Id: poolabs.c,v 1.20.1.1.1.1 2013/12/19 11:27:05 anon Exp $
 * $HopeName: MMsrc!poolabs.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 *
 * PURPOSE
 *
 * .purpose: This defines the abstract pool classes, giving
 * a single-inheritance framework which concrete classes 
 * may utilize.  The purpose is to reduce the fragility of class 
 * definitions for pool implementations when small changes are 
 * made to the pool protocol.   For now, the class hierarchy for
 * the abstract classes is intended to be useful, but not to
 * represent any particular design for pool inheritance.
 * 
 * HIERARCHY
 *
 * .hierarchy: define the following hierarchy of abstract pool classes:
 *    AbstractPoolClass     - implements init, finish, describe
 *     AbstractAllocFreePoolClass - implements alloc & free
 *     AbstractBufferPoolClass - implements the buffer protocol
 *      AbstractSegBufPoolClass - uses SegBuf buffer class 
 *       AbstractScanPoolClass - implements basic scanning
 *        AbstractCollectPoolClass - implements basic GC 
 */

#include "mpm.h"

SRCID(poolabs, "$Id: poolabs.c,v 1.20.1.1.1.1 2013/12/19 11:27:05 anon Exp $");


static Res PoolTrivInit(Pool pool, va_list arg);
static void PoolTrivFinish(Pool pool);
static void PoolNoClear(Pool pool);
static Res PoolNoAlloc(Addr *pReturn, Pool pool, Size size,
                       Bool withReservoirPermit, DebugInfo info);
static Res PoolTrivAlloc(Addr *pReturn, Pool pool, Size size,
                         Bool withReservoirPermit, DebugInfo info);
static void PoolNoFree(Pool pool, Addr old, Size size);
static void PoolTrivFree(Pool pool, Addr old, Size size);
static Res PoolNoBufferFill(Addr *baseReturn, Addr *limitReturn,
                            Pool pool, Buffer buffer, Size size,
                            Bool withReservoirPermit);
static Res PoolTrivBufferFill(Addr *baseReturn, Addr *limitReturn,
                              Pool pool, Buffer buffer, Size size,
                              Bool withReservoirPermit);
static void PoolNoBufferEmpty(Pool pool, Buffer buffer, 
                              Addr init, Addr limit);
static void PoolTrivBufferEmpty(Pool pool, Buffer buffer, 
                                Addr init, Addr limit);
static Res PoolTrivDescribe(Pool pool, mps_lib_FILE *stream);
static Res PoolNoAccess(Pool pool, Seg seg, Addr addr,
                        AccessSet mode, MutatorFaultContext context);
static Res PoolNoWhiten(Pool pool, Trace trace, Seg seg);
static Res PoolTrivWhiten(Pool pool, Trace trace, Seg seg);
static void PoolNoGrey(Pool pool, Trace trace, Seg seg);
static void PoolTrivGrey(Pool pool, Trace trace, Seg seg);
static void PoolNoBlacken(Pool pool, TraceSet traceSet, Seg seg);
static void PoolTrivBlacken(Pool pool, TraceSet traceSet, Seg seg);
static Res PoolNoScan(Bool *totalReturn, ScanState ss, Pool pool, Seg seg);
static Res PoolNoFix(Pool pool, ScanState ss, Seg seg, Ref *refIO);
static Bool PoolNoResolve(Pool pool, ScanState ss, Seg seg, Ref *refIO);
static void PoolNoReclaim(Pool pool, Trace trace, Seg seg);
static void PoolNoRampBegin(Pool pool, Buffer buf, Bool collectAll);
static void PoolTrivRampBegin(Pool pool, Buffer buf, Bool collectAll);
static void PoolNoRampEnd(Pool pool, Buffer buf);
static void PoolTrivRampEnd(Pool pool, Buffer buf);
static Size PoolScanFreeSize(Pool pool);
static Res PoolNoFramePush(AllocFrame *frameReturn, Pool pool, Buffer buf);
static Res PoolTrivFramePush(AllocFrame *frameReturn, Pool pool, Buffer buf);
static Res PoolNoFramePop(Pool pool, Buffer buf, AllocFrame frame);
static Res PoolTrivFramePop(Pool pool, Buffer buf, AllocFrame frame);
static void PoolNoFramePopPending(Pool pool, Buffer buf, AllocFrame frame);
static void PoolNoWalk(Pool pool, Seg seg, FormattedObjectsStepMethod step,
		       void *p);
static void PoolNoFreeWalk(Pool pool, FreeBlockStepMethod f, void *p);
static PoolDebugMixin PoolNoDebugMixin(Pool pool);
static BufferClass PoolNoBufferClass(void);
static Size PoolNoFreeSize(Pool pool);


/* Mixins:
 *
 * For now (at least) we're avoiding multiple inheritance. 
 * However, there is a significant use of multiple inheritance 
 * in practice amongst the pool classes, as there are several
 * orthogonal sub-protocols included in the pool protocol.
 * The following mixin functions help to provide the inheritance
 * via a simpler means than real multiple inheritance.
 */


/* PoolClassMixInAllocFree -- mix in the protocol for Alloc / Free */

void PoolClassMixInAllocFree(PoolClass class)
{
  /* Can't check class because it's not initialized yet */
  class->attr |= (AttrALLOC | AttrFREE);
  class->alloc = PoolTrivAlloc;
  class->free = PoolTrivFree;
}


/* PoolClassMixInBuffer -- mix in the protocol for buffer reserve / commit */

void PoolClassMixInBuffer(PoolClass class)
{
  /* Can't check class because it's not initialized yet */
  class->attr |= (AttrBUF | AttrBUF_RESERVE);
  class->bufferFill = PoolTrivBufferFill;
  class->bufferEmpty = PoolTrivBufferEmpty;
  /* By default, buffered pools treat frame operations as NOOPs */
  class->framePush = PoolTrivFramePush; 
  class->framePop = PoolTrivFramePop;
  class->bufferClass = BufferClassGet;
}


/* PoolClassMixInScan -- mix in the protocol for scanning */

void PoolClassMixInScan(PoolClass class)
{
  /* Can't check class because it's not initialized yet */
  class->attr |= AttrSCAN;
  class->access = PoolSegAccess;
  class->blacken = PoolTrivBlacken;
  class->grey = PoolTrivGrey;
  /* Scan is part of the scanning protocol - but there is */
  /* no useful default method */
  class->scan = PoolNoScan;
  class->freeSize = PoolScanFreeSize;
}


/* PoolClassMixInFormat -- mix in the protocol for formatted pools */

void PoolClassMixInFormat(PoolClass class)
{
  /* Can't check class because it's not initialized yet */
  class->attr |= AttrFMT;
}


/* PoolClassMixInCollect -- mix in the protocol for GC */

void PoolClassMixInCollect(PoolClass class)
{
  /* Can't check class because it's not initialized yet */
  class->attr |= (AttrGC | AttrINCR_RB);
  class->whiten = PoolTrivWhiten;
  /* Fix & reclaim are part of the collection protocol - but there */
  /* are no useful default methods for them. */
  class->rampBegin = PoolTrivRampBegin;
  class->rampEnd = PoolTrivRampEnd;
}


/* Classes */


DEFINE_CLASS(AbstractPoolClass, class)
{
  INHERIT_CLASS(&class->protocol, ProtocolClass);
  class->name = "ABSTRACT";
  class->size = 0;
  class->offset = 0;
  class->attr = 0;
  class->init = PoolTrivInit;
  class->finish = PoolTrivFinish;
  class->clear = PoolNoClear;
  class->alloc = PoolNoAlloc;
  class->free = PoolNoFree;
  class->bufferFill = PoolNoBufferFill;
  class->bufferEmpty = PoolNoBufferEmpty;
  class->access = PoolNoAccess;
  class->whiten = PoolNoWhiten;
  class->grey = PoolNoGrey;
  class->blacken = PoolNoBlacken;
  class->scan = PoolNoScan;
  class->fix = PoolNoFix;
  class->fixEmergency = PoolNoFix;
  class->resolve = PoolNoResolve;
  class->reclaim = PoolNoReclaim;
  class->rampBegin = PoolNoRampBegin;
  class->rampEnd = PoolNoRampEnd;
  class->framePush = PoolNoFramePush;
  class->framePop = PoolNoFramePop;
  class->framePopPending = PoolNoFramePopPending;
  class->walk = PoolNoWalk;
  class->freewalk = PoolNoFreeWalk;
  class->bufferClass = PoolNoBufferClass;
  class->freeSize = PoolNoFreeSize;
  class->describe = PoolTrivDescribe;
  class->debugMixin = PoolNoDebugMixin;
  class->labelled = FALSE;
  class->sig = PoolClassSig;
}

DEFINE_CLASS(AbstractAllocFreePoolClass, class)
{
  INHERIT_CLASS(class, AbstractPoolClass);
  PoolClassMixInAllocFree(class);
}

DEFINE_CLASS(AbstractBufferPoolClass, class)
{
  INHERIT_CLASS(class, AbstractPoolClass);
  PoolClassMixInBuffer(class);
}

DEFINE_CLASS(AbstractSegBufPoolClass, class)
{
  INHERIT_CLASS(class, AbstractBufferPoolClass);
  class->bufferClass = SegBufClassGet;
}

DEFINE_CLASS(AbstractScanPoolClass, class)
{
  INHERIT_CLASS(class, AbstractSegBufPoolClass);
  PoolClassMixInScan(class);
}

DEFINE_CLASS(AbstractCollectPoolClass, class)
{
  INHERIT_CLASS(class, AbstractScanPoolClass);
  PoolClassMixInCollect(class);
}


/* PoolNo*, PoolTriv* -- Trivial and non-methods for Pool Classes 
 *
 * See design.mps.pool.no and design.mps.pool.triv
 */


void PoolTrivFinish(Pool pool)
{
  AVERT(Pool, pool);
  NOOP;
}

Res PoolTrivInit(Pool pool, va_list args)
{
  AVERT(Pool, pool);
  UNUSED(args);
  return ResOK;
}

void PoolNoClear(Pool pool)
{
  AVERT(Pool, pool);
  NOTREACHED;
}


Res PoolNoAlloc(Addr *pReturn, Pool pool, Size size,
                Bool withReservoirPermit, DebugInfo info)
{
  AVER(pReturn != NULL);
  AVERT(Pool, pool);
  AVER(size > 0);
  AVER(BoolCheck(withReservoirPermit));
  UNUSED(info);
  NOTREACHED;
  return ResUNIMPL;
}

Res PoolTrivAlloc(Addr *pReturn, Pool pool, Size size,
                  Bool withReservoirPermit, DebugInfo info)
{
  AVER(pReturn != NULL);
  AVERT(Pool, pool);
  AVER(size > 0);
  AVER(BoolCheck(withReservoirPermit));
  UNUSED(info);
  return ResUNIMPL;
}

void PoolNoFree(Pool pool, Addr old, Size size)
{
  AVERT(Pool, pool);
  AVER(old != NULL);
  AVER(size > 0);
  NOTREACHED;
}

void PoolTrivFree(Pool pool, Addr old, Size size)
{
  AVERT(Pool, pool);
  AVER(old != NULL);
  AVER(size > 0);
  NOOP;                         /* trivial free has no effect */
}


Res PoolNoBufferFill(Addr *baseReturn, Addr *limitReturn,
                     Pool pool, Buffer buffer, Size size,
                     Bool withReservoirPermit)
{
  AVER(baseReturn != NULL);
  AVER(limitReturn != NULL);
  AVERT(Pool, pool);
  AVERT(Buffer, buffer);
  AVER(size > 0);
  AVER(BoolCheck(withReservoirPermit));
  NOTREACHED;
  return ResUNIMPL;
}

Res PoolTrivBufferFill(Addr *baseReturn, Addr *limitReturn,
                       Pool pool, Buffer buffer, Size size,
                       Bool withReservoirPermit)
{
  Res res;
  Addr p;

  AVER(baseReturn != NULL);
  AVER(limitReturn != NULL);
  AVERT(Pool, pool);
  AVERT(Buffer, buffer);
  AVER(size > 0);
  AVER(BoolCheck(withReservoirPermit));

  res = PoolAlloc(&p, pool, size, withReservoirPermit, NULL);
  if(res != ResOK) return res;

  *baseReturn = p;
  *limitReturn = AddrAdd(p, size);
  return ResOK;
}


void PoolNoBufferEmpty(Pool pool, Buffer buffer, 
                       Addr init, Addr limit)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buffer);
  AVER(BufferIsReady(buffer));
  AVER(init <= limit);
  NOTREACHED;
}

void PoolTrivBufferEmpty(Pool pool, Buffer buffer, Addr init, Addr limit)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buffer);
  AVER(BufferIsReady(buffer));
  AVER(init <= limit);
  if (limit > init)
    PoolFree(pool, init, AddrOffset(init, limit));
}


Res PoolTrivDescribe(Pool pool, mps_lib_FILE *stream)
{
  AVERT(Pool, pool);
  AVER(stream != NULL);
  return WriteF(stream, "  No class-specific description available.\n", NULL);
}


/* NoAccess
 *
 * Should be used (for the access method) by Pool Classes which do
 * not expect to ever have pages which the mutator will fault on.
 * That is, no protected pages, or only pages which are inaccessible
 * by the mutator are protected.
 */
Res PoolNoAccess(Pool pool, Seg seg, Addr addr,
                 AccessSet mode, MutatorFaultContext context)
{
  AVERT(Pool, pool);
  AVERT(Seg, seg);
  AVER(SegBase(seg) <= addr);
  AVER(addr < SegLimit(seg));
  /* can't check AccessSet as there is no Check method */
  /* can't check context as there is no Check method */
  UNUSED(mode);
  UNUSED(context);

  NOTREACHED;
  return ResUNIMPL;
}


/* SegAccess
 *
 * Should be used (for the access method) by Pool Classes which intend
 * to handle page faults by scanning the entire segment and lowering
 * the barrier.
 */
Res PoolSegAccess(Pool pool, Seg seg, Addr addr,
                  AccessSet mode, MutatorFaultContext context)
{
  AVERT(Pool, pool);
  AVERT(Seg, seg);
  AVER(SegBase(seg) <= addr);
  AVER(addr < SegLimit(seg));
  AVER(SegPool(seg) == pool);
  /* can't check AccessSet as there is no Check method */
  /* can't check context as there is no Check method */

  UNUSED(addr);
  UNUSED(context);
  TraceSegAccess(PoolArena(pool), seg, mode);
  return ResOK;
}


/* PoolSingleAccess
 *
 * Handles page faults by attempting emulation.  If the faulting
 * instruction cannot be emulated then this function returns ResFAIL.
 *
 * Due to the assumptions made below, pool classes should only use
 * this function if all words in an object are tagged or traceable.
 *
 * .single-access.assume.ref: It currently assumes that the address
 * being faulted on contains a plain reference or a tagged non-reference.
 * .single-access.improve.format: * later this will be abstracted
 * through the client object format interface, so that
 * no such assumption is necessary.
 */
Res PoolSingleAccess(Pool pool, Seg seg, Addr addr,
                     AccessSet mode, MutatorFaultContext context)
{
  Arena arena;

  AVERT(Pool, pool);
  AVERT(Seg, seg);
  AVER(SegBase(seg) <= addr);
  AVER(addr < SegLimit(seg));
  AVER(SegPool(seg) == pool);
  /* can't check AccessSet as there is no Check method */
  /* can't check context as there is no Check method */

  arena = PoolArena(pool);

  if(ProtCanStepInstruction(context)) {
    Ref ref;
    Res res;

    ShieldExpose(arena, seg);

    if(mode & SegSM(seg) & AccessREAD) {
      /* read access */
      /* .single-access.assume.ref */
      /* .single-access.improve.format */
      ref = *(Ref *)addr;
      /* Check that the reference is aligned to a word boundary */
      /* (we assume it is not a reference otherwise) */
      if(WordIsAligned((Word)ref, sizeof(Word))) {
        /* See the note in TraceSegAccess about using RankEXACT here */
        /* (impl.c.trace.scan.conservative) */
	TraceScanSingleRef(arena->flippedTraces, RankEXACT, arena,
	                   seg, (Ref *)addr);
      }
    }
    res = ProtStepInstruction(context);
    AVER(res == ResOK);

    /* update SegSummary according to the possibly changed reference */
    ref = *(Ref *)addr;
    SegSetSummary(seg, RefSetAdd(arena, SegSummary(seg), ref));

    ShieldCover(arena, seg);

    return ResOK;
  } else {
    /* couldn't single-step instruction */
    return ResFAIL;
  }
}


Res PoolTrivWhiten(Pool pool, Trace trace, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(Trace, trace);
  AVERT(Seg, seg);

  SegSetWhite(seg, TraceSetAdd(SegWhite(seg), trace));

  return ResOK;
}

Res PoolNoWhiten(Pool pool, Trace trace, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(Trace, trace);
  AVERT(Seg, seg);
  NOTREACHED;
  return ResUNIMPL;
}


void PoolNoGrey(Pool pool, Trace trace, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(Trace, trace);
  AVERT(Seg, seg);
  NOTREACHED;
}

void PoolTrivGrey(Pool pool, Trace trace, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(Trace, trace);
  AVERT(Seg, seg);

  /* @@@@ The trivial grey method probably shouldn't exclude */
  /* the white segments, since they might also contain grey objects. */
  if(!TraceSetIsMember(SegWhite(seg), trace))
    SegSetGrey(seg, TraceSetSingle(trace));
}


void PoolNoBlacken(Pool pool, TraceSet traceSet, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(TraceSet, traceSet);
  AVERT(Seg, seg);
  NOTREACHED;
}

void PoolTrivBlacken(Pool pool, TraceSet traceSet, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(TraceSet, traceSet);
  AVERT(Seg, seg);

  /* The trivial blacken method does nothing; for pool classes which do */
  /* not keep additional colour information. */
  NOOP;
}


Res PoolNoScan(Bool *totalReturn, ScanState ss, Pool pool, Seg seg)
{
  AVER(totalReturn != NULL);
  AVERT(ScanState, ss);
  AVERT(Pool, pool);
  AVERT(Seg, seg);
  NOTREACHED;
  return ResUNIMPL;
}

Res PoolNoFix(Pool pool, ScanState ss, Seg seg, Ref *refIO)
{
  AVERT(Pool, pool);
  AVERT(ScanState, ss);
  AVERT(Seg, seg);
  AVER(refIO != NULL);
  NOTREACHED;
  return ResUNIMPL;
}

Bool PoolNoResolve(Pool pool, ScanState ss, Seg seg, Ref *refIO)
{
  AVERT(Pool, pool);
  AVERT(ScanState, ss);
  AVERT(Seg, seg);
  AVER(refIO != NULL);
  NOTREACHED;
  return TRUE;
}

void PoolNoReclaim(Pool pool, Trace trace, Seg seg)
{
  AVERT(Pool, pool);
  AVERT(Trace, trace);
  AVERT(Seg, seg);
  NOTREACHED;
}


void PoolNoRampBegin(Pool pool, Buffer buf, Bool collectAll)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  AVERT(Bool, collectAll);
  NOTREACHED;
}


void PoolNoRampEnd(Pool pool, Buffer buf)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  NOTREACHED;
}


void PoolTrivRampBegin(Pool pool, Buffer buf, Bool collectAll)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  AVERT(Bool, collectAll);
}


void PoolTrivRampEnd(Pool pool, Buffer buf)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
}


/* PoolScanFreeSize -- return the size of free blocks (just the buffers) */

Size PoolScanFreeSize(Pool pool)
{
  Size free = 0;
  Ring node, nextNode;

  AVERT(Pool, pool);

  /* Scannable pools don't have any gaps, so only the buffers are free. */
  RING_FOR(node, &pool->bufferRing, nextNode) {
    Buffer buffer = RING_ELT(Buffer, poolRing, node);
    Addr alloc, limit;

    AVERT(Buffer,buffer);

    alloc = BufferAlloc(buffer);
    limit = BufferLimit(buffer);
    if (alloc == limit)
      continue;
    free += AddrOffset(alloc, limit);
  }

  return free;
}


Res PoolNoFramePush(AllocFrame *frameReturn, Pool pool, Buffer buf)
{
  AVER(frameReturn != NULL);
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  NOTREACHED;
  return ResUNIMPL;
}


Res PoolNoFramePop(Pool pool, Buffer buf, AllocFrame frame)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  /* frame is of a abstract type & can't be checked */
  UNUSED(frame);
  NOTREACHED;
  return ResUNIMPL;
}


void PoolNoFramePopPending(Pool pool, Buffer buf, AllocFrame frame)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  /* frame is of a abstract type & can't be checked */
  UNUSED(frame);
  NOTREACHED;
}


Res PoolTrivFramePush(AllocFrame *frameReturn, Pool pool, Buffer buf)
{
  AVER(frameReturn != NULL);
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  return ResOK;
}


Res PoolTrivFramePop(Pool pool, Buffer buf, AllocFrame frame)
{
  AVERT(Pool, pool);
  AVERT(Buffer, buf);
  /* frame is of a abstract type & can't be checked */
  UNUSED(frame);
  return ResOK;
}


void PoolNoWalk(Pool pool, Seg seg,
                FormattedObjectsStepMethod f, void *p)
{
  AVERT(Pool, pool);
  AVERT(Seg, seg);
  AVER(FUNCHECK(f));
  /* p is arbitrary, hence can't be checked */
  UNUSED(p);

  NOTREACHED;
}


void PoolNoFreeWalk(Pool pool, FreeBlockStepMethod f, void *p)
{
  AVERT(Pool, pool);
  AVER(FUNCHECK(f));
  /* p is arbitrary, hence can't be checked */
  UNUSED(p);

  /* FreeWalk doesn't have be perfect, so just pretend you didn't find any. */
  NOOP;
}


BufferClass PoolNoBufferClass(void)
{
  NOTREACHED;
  return NULL;
}


Size PoolNoFreeSize(Pool pool)
{
  UNUSED(pool);
  NOTREACHED;
  return 0;
}


/* PoolNoDebugMixin -- debug mixin methods for pools with no mixin */

PoolDebugMixin PoolNoDebugMixin(Pool pool)
{
  AVERT(Pool, pool);
  return NULL;
}
