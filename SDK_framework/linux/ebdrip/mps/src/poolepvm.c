/* impl.c.poolepvm: ELECTRONIC PUBLISHING "VIRTUAL MEMORY" CLASS
 *
 * $Id: poolepvm.c,v 1.66.1.1.1.1 2013/12/19 11:27:05 anon Exp $
 * $HopeName: MMsrc!poolepvm.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * .purpose: This is the implementation of the PostScript Virtual Memory
 * pool class for MM/EP-core.
 *
 * .design: see design.mps.poolams and design.mps.poolepvm.  */

#include "poolepvm.h"
#include "mpscepvm.h"
#include "mps.h"
#include "dbgpool.h"
#include "poolams.h"
#include "protocol.h"
#include "mpm.h"
#include "chain.h"
#include <stdarg.h>

SRCID(poolepvm, "$Id: poolepvm.c,v 1.66.1.1.1.1 2013/12/19 11:27:05 anon Exp $");


/* EPVMSaveStruct -- Save level info
 * 
 * See design.mps.poolepvm.arch.save.  */

#define EPVMSaveSig     ((Sig)0x519EBF35) /* SIGnature EPVM Save */

struct EPVMSaveStruct {
  Sig sig;
  Index level;                  /* save level */
  Size size;                    /* total size of segs at this level */
  /* .segment.small: To track if the small segment has been allocated
     (see design.mps.poolepvm.arch.segement.size), we have fields for
     the two types of segments, objects and string.
     .segment.small.epfn: For the subclass EPFN, the two types are exact
     and weak, so to avoid duplicating the segment code, the two types
     are rank=exact (smallObjectSeg) or other (smallStringSeg). */
  Bool smallStringSeg;          /* small seg alloc'ed at this level? */
  Bool smallObjectSeg;          /* small seg alloc'ed at this level? */
  EPVM epvm;                    /* owning epvm */
  RingStruct segRing;           /* ring of segs at this level */
};


/* EPVMDebugStruct -- structure for a debug subclass */

typedef struct EPVMDebugStruct {
  EPVMStruct epvmStruct;       /* EPVM structure */
  PoolDebugMixinStruct debug;  /* debug mixin */
} EPVMDebugStruct;

/* typedef struct EPVMDebugStruct *EPVMDebug; */


/* Forward declarations */

static BufferClass EPVMBufferClassGet(void);


/* macros to get between child and parent structures */

#define EPVMSeg2AMSSeg(epvmSeg) (&(epvmSeg)->amsSegStruct)
#define Seg2EPVMSeg(seg) PARENT(EPVMSegStruct, amsSegStruct, Seg2AMSSeg(seg))
#define EPVMSeg2Seg(epvmSeg) AMSSeg2Seg(EPVMSeg2AMSSeg(epvmSeg))
#define AMS2EPVM(ams)   PARENT(EPVMStruct, amsStruct, ams)
#define EPVM2AMS(epvm)  (&(epvm)->amsStruct)
#define EPVM2EPVMDebug(epvm)  PARENT(EPVMDebugStruct, epvmStruct, epvm)
#define EPVMDebug2EPVM(epvmd) (&((epvmd)->epvmStruct))


/* macros for maneuvering about between structures */

#define EPVMSegEPVM(epvmSeg)       ((epvmSeg)->save->epvm)
#define EPVMCurrentSave(epvm)      (&(epvm)->saves[(epvm)->saveLevel])


/* EPVMSaveCheck -- check the save level structure */

static Bool EPVMSaveCheck(EPVMSave save)
{
  CHECKS(EPVMSave, save);
  CHECKU(EPVM, save->epvm);
  CHECKL(save->level <= save->epvm->maxSaveLevel);
  CHECKL(save->size <= PoolManagedSize(EPVM2Pool(save->epvm)));
  if (save->level > save->epvm->saveLevel) /* nothing at this level */
    CHECKL(save->size == 0);
  CHECKL(SizeIsAligned(save->size,
                       ArenaAlign(PoolArena(EPVM2Pool(save->epvm)))));
  CHECKL(BoolCheck(save->smallStringSeg));
  CHECKL(BoolCheck(save->smallObjectSeg));
  CHECKL(RingCheck(&save->segRing));

  return TRUE;
}


/* EPVMSaveInit -- initialize a save structure */

static void EPVMSaveInit(EPVM epvm, Index level)
{
  EPVMSave save = EPVMLevelSave(epvm, level);

  RingInit(&save->segRing);
  save->epvm = epvm;
  save->level = level;
  save->size = 0;
  save->smallStringSeg = FALSE;
  save->smallObjectSeg = FALSE;
  save->sig = EPVMSaveSig;
  /* .save.nocheck: Can't be checked before the pool init is */
  /* completed, then it is checked by EPVMCheck. */
}


/* EPVMSegCheck -- check the EPVM segment */

Bool EPVMSegCheck(EPVMSeg epvmSeg)
{
  Seg seg;

  CHECKS(EPVMSeg, epvmSeg);
  CHECKL(AMSSegCheck(&epvmSeg->amsSegStruct));
  seg = EPVMSeg2Seg(epvmSeg);
  CHECKU(EPVMSave, epvmSeg->save);
  CHECKL(epvmSeg->save->size >= SegSize(seg));
  /* buffers only on the current save level */
  if (SegBuffer(seg) != NULL)
    CHECKL(EPVMCurrentSave(EPVMSegEPVM(epvmSeg)) == epvmSeg->save);
  /* See design.mps.poolepvm.protection.format and */
  /* d.m.p.protection.hack. */
  AVER(SegSummary(seg) == RefSetUNIV || SegSummary(seg) == RefSetEMPTY);

  return TRUE;
}


/* EPVMSegInit -- initialise an epvm segment */

static Res EPVMSegInit(Seg seg, Pool pool, Addr base, Size size,
                       Bool reservoirPermit, va_list args)
{
  SegClass super;
  EPVMSeg epvmSeg;
  EPVM epvm;
  EPVMSave save;
  Res res;

  AVERT(Seg, seg);
  epvmSeg = Seg2EPVMSeg(seg);
  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);
  /* no useful checks for base and size */
  AVER(BoolCheck(reservoirPermit));

  /* Initialize the superclass fields first via next-method call */
  super = SEG_SUPERCLASS(EPVMSegClass);
  res = super->init(seg, pool, base, size, reservoirPermit, args);
  if (res != ResOK)
    return res;

  save = EPVMCurrentSave(epvm);
  epvmSeg->save = save;
  save->size += SegSize(seg);
  /* The small*Seg flag can't be set here, because the rankset is not set. */

  epvmSeg->sig = EPVMSegSig;
  AVERT(EPVMSeg, epvmSeg);

  return ResOK;
}


/* EPVMSegFinish -- Finish method for EPVM segments */

static void EPVMSegFinish(Seg seg)
{
  EPVMSeg epvmSeg;
  EPVMSave save;
  Size size;

  AVERT(Seg, seg);
  epvmSeg = Seg2EPVMSeg(seg);
  AVERT(EPVMSeg, epvmSeg);

  size = SegSize(seg);
  save = epvmSeg->save;
  AVERT(EPVMSave, save);
  epvmSeg->sig = SigInvalid;

  /* finish the superclass fields last */
  SEG_SUPERCLASS(EPVMSegClass)->finish(seg);

  /* save->size must be consistent when calling super, so update after */
  AVER(save->size >= size);
  save->size -= size;
}


/* EPVMSetRankSet -- rankset set method for EPVM segments
 *
 * .smallseg: Used to update the small*Seg flags, which depend on rankset.
 */

void EPVMSetRankSet(Seg seg, RankSet rankSet)
{
  EPVMSeg epvmSeg;
  EPVMSave save;

  AVERT(Seg, seg);
  epvmSeg = Seg2EPVMSeg(seg);
  AVERT(EPVMSeg, epvmSeg);
  AVER(RankSetCheck(rankSet));

  save = epvmSeg->save;
  if (RankSetEMPTY == rankSet) {
    save->smallStringSeg = TRUE;
  } else {
    save->smallObjectSeg = TRUE;
  }
  SEG_SUPERCLASS(EPVMSegClass)->setRankSet(seg, rankSet);
}


/* EPVMSetRankAndSummary -- rankset & summary set method for EPVM segments
 *
 * .smallseg: Used to update the small*Seg flags, which depend on rankset.
 */

void EPVMSetRankAndSummary(Seg seg, RankSet rankSet, RefSet summary)
{
  EPVMSeg epvmSeg;
  EPVMSave save;

  AVERT(Seg, seg);
  epvmSeg = Seg2EPVMSeg(seg);
  AVERT(EPVMSeg, epvmSeg);
  AVER(RankSetCheck(rankSet));
  /* no useful test for summary */
  UNUSED(summary);

  save = epvmSeg->save;
  if (RankSetEMPTY == rankSet) {
    save->smallStringSeg = TRUE;
  } else {
    save->smallObjectSeg = TRUE;
  }
  SEG_SUPERCLASS(EPVMSegClass)->setRankSummary(seg, rankSet, summary);
}


/* EPVMSegDescribe -- describe an EPVM segment */

static Res EPVMSegDescribe(Seg seg, mps_lib_FILE *stream)
{
  Res res;
  EPVMSeg epvmSeg;
  SegClass super;

  /* .describe.check: Debugging tools don't AVER things. */
  if (!CHECKT(Seg, seg)) 
    return ResFAIL;
  if (stream == NULL) 
    return ResFAIL;
  epvmSeg = Seg2EPVMSeg(seg);
  if (!CHECKT(EPVMSeg, epvmSeg)) 
    return ResFAIL;

  /* Describe the superclass fields first via next-method call */
  super = SEG_SUPERCLASS(EPVMSegClass);
  res = super->describe(seg, stream);
  if (res != ResOK)
    return res;

  res = WriteF(stream,
               "  save $P\n", (WriteFP)epvmSeg->save,
               NULL);
  return res;
}


/* EPVMSegClass -- Class definition for EPVM segments */

typedef SegClassStruct EPVMSegClassStruct;

DEFINE_CLASS(EPVMSegClass, class)
{
  INHERIT_CLASS(class, AMSSegClass);
  SegClassMixInNoSplitMerge(class);
  class->name = "EPVMSEG";
  class->size = sizeof(EPVMSegStruct);
  class->init = EPVMSegInit;
  class->finish = EPVMSegFinish;
  class->setRankSet = EPVMSetRankSet;
  class->setRankSummary = EPVMSetRankAndSummary;
  class->describe = EPVMSegDescribe;
}


/* EPVMSegSizePolicy -- method for deciding the segment size */

static Res EPVMSegSizePolicy(Size *sizeReturn,
                             Pool pool, Size size, RankSet rankSet)
{
  Bool smallSeg;  /* has a small segment already been allocated? */
  EPVM epvm;
  EPVMSave save;

  AVER(sizeReturn != NULL);
  AVERT(Pool, pool);
  AVER(size > 0);
  AVER(RankSetCheck(rankSet));

  epvm = Pool2EPVM(pool);
  save = EPVMCurrentSave(epvm);

  if (RankSetSingle(RankEXACT) == rankSet) /* see .segment.small.epfn */
    smallSeg = save->smallObjectSeg;
  else
    smallSeg = save->smallStringSeg;
  if (smallSeg) {
    /* small segment already allocated, so this is a */
    /* subsequent segment */
    size = SizeRoundUp(size, epvm->subsequentSegRound);
  } else {
    size = SizeAlignUp(size, ArenaAlign(PoolArena(pool)));
  }
  if (size == 0) /* overflow */
    return ResMEMORY;

  *sizeReturn = size;
  return ResOK;
}


/* epvmSaveDiscard -- discards all the segs at one save level */

static void epvmSaveDiscard(EPVMSave save)
{
  Ring ring, node, next;           /* for iterating over the segs */

  /* destroy all the segs */
  ring = &save->segRing;
  RING_FOR(node, ring, next) {
    EPVMSeg epvmSeg = RING_ELT(EPVMSeg, amsSegStruct.segRing, node);
    Seg seg = EPVMSeg2Seg(epvmSeg);
    Buffer buffer;

    AVERT_CRITICAL(EPVMSeg, epvmSeg);

    buffer = SegBuffer(seg);
    /* We assume MPS is synchronized with the buffer, so it can detach
     * the buffer and deallocate the segment. */
    if (buffer != NULL)
      BufferDetach(buffer, EPVM2Pool(save->epvm));

    SegFree(seg);
  }
  save->smallStringSeg = FALSE;
  save->smallObjectSeg = FALSE;

  AVER(save->size == 0);
}


/* EPVMCurrentRing -- the ring of segments for the current save level */

static Ring EPVMCurrentRing(AMS ams, RankSet rankSet, Size size)
{
  UNUSED(rankSet); UNUSED(size);
  return &EPVMCurrentSave(AMS2EPVM(ams))->segRing;
}


/* EPVMInit -- the pool class initialization method
 * 
 * Takes three additional arguments: the format of the objects allocated
 * in the pool, the maximum save level, and the current save level.  */

static Res EPVMInit(Pool pool, va_list args)
{
  EPVM epvm;
  void *p;
  Res res;
  Index i;
  mps_epvm_save_level_t maxSaveLevel;
  Format format;
  Arena arena;
  Chain chain;
  static GenParamStruct epvmGenParam = { SizeMAX, 0.5 /* dummy */ };

  AVERT(Pool, pool);
  arena = PoolArena(pool);

  format = va_arg(args, Format);
  res = ChainCreate(&chain, arena, 1, &epvmGenParam);
  if (res != ResOK)
    return res;
  res = AMSInitInternal(Pool2AMS(pool), format, chain, TRUE);
  if (res != ResOK)
    goto failAMSInit;

  epvm = Pool2EPVM(pool);
  maxSaveLevel = va_arg(args, mps_epvm_save_level_t);
  epvm->maxSaveLevel = (Index)maxSaveLevel;
  AVER(epvm->maxSaveLevel > 0);
  epvm->saveLevel = (Index)va_arg(args, mps_epvm_save_level_t);
  AVER(epvm->saveLevel <= epvm->maxSaveLevel);
  epvm->subsequentSegRound =
    SizeAlignUp(EPVMDefaultSubsequentSegSIZE, ArenaAlign(arena));

  res = ControlAlloc(&p, arena, sizeof(EPVMSaveStruct) * (maxSaveLevel+1),
                     FALSE);
  if (res != ResOK)
    goto failSaveAlloc;
  epvm->saves = p;
  for(i = 0; i <= epvm->maxSaveLevel; ++i)
    EPVMSaveInit(epvm, i);

  epvm->sig = EPVMSig;
  AVERT(EPVM, epvm);
  EVENT_PPPWW(PoolInitEPVM,
              pool, arena, format, maxSaveLevel, epvm->saveLevel);
  return ResOK;

failSaveAlloc:
  AMSFinish(pool);
failAMSInit:
  ChainDestroy(chain);
  return res;
}


/* EPVMFinish -- the pool class finishing method
 * 
 * Destroys all the segs in the pool.  Can't invalidate the EPVM until
 * we've destroyed all the segs, as it may be checked.  */

static void EPVMFinish(Pool pool)
{
  EPVM epvm; AMS ams;
  Index i;
  Chain chain;

  AVERT(Pool, pool);
  ams = Pool2AMS(pool);
  AVERT(AMS, ams);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);

  chain = AMSChain(ams);
  AMSFinish(pool);
  /* Can't invalidate the EPVM until we've destroyed all the segs. */
  epvm->sig = SigInvalid;

  /* Can't invalidate the save levels until after the segs. */
  for(i = 0; i <= epvm->maxSaveLevel; ++i) {
    EPVMSave save = EPVMLevelSave(epvm, i);
    RingFinish(&save->segRing);
    save->sig = SigInvalid;
  }
  ControlFree(PoolArena(pool), epvm->saves,
              sizeof(EPVMSaveStruct) * (epvm->maxSaveLevel + 1));
  ChainDestroy(chain);
}


/* EPVMBufferInit -- initialize a buffer
 *
 * See design.mps.poolepvm.arch.obj-string.  */

static Res EPVMBufferInit (Buffer buffer, Pool pool, va_list args)
{
  Bool isObj = va_arg(args, Bool);
  BufferClass super;
  Res res;

  AVERT(Buffer, buffer);
  AVERT(Pool, pool);
  AVERT(Bool, isObj);

  /* Initialize the superclass fields first via next-method call */
  super = BUFFER_SUPERCLASS(EPVMBufferClass);
  res = super->init(buffer, pool, args);
  if (res != ResOK)
    return res;

  if (isObj)
    BufferSetRankSet(buffer, RankSetSingle(RankEXACT));

  EVENT_PPU(BufferInitEPVM, buffer, pool, isObj);
  return ResOK;
}


/* EPVMBufferClass -- EPVMBufferClass class definition 
 *
 * Like SegBufClass, but with special initialization.  */

DEFINE_BUFFER_CLASS(EPVMBufferClass, class)
{
  INHERIT_CLASS(class, SegBufClass);
  class->name = "EPVMBUF";
  class->init = EPVMBufferInit;
}


/* EPVMScan -- the pool class scanning method */

static Res EPVMScan(Bool *totalReturn, ScanState ss, Pool pool, Seg seg)
{
  Res res;
  EPVM epvm;

  AVER(totalReturn != NULL);
  AVERT(ScanState, ss);
  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);
  AVER(SegCheck(seg));
  /* Summaries must be univ, because we always compute them so. */
  AVER(SegSummary(seg) == RefSetUNIV);

  res = AMSScan(totalReturn, ss, pool, seg);
  /* See design.mps.poolepvm.protection.format. */
  ScanStateSetSummary(ss, RefSetUNIV);
  return res;
}


/* EPVMFix -- the pool class fixing method */

static Res EPVMFix(Pool pool, ScanState ss, Seg seg, Ref *refIO)
{
  AMSSeg amsseg;
  Index i;                      /* the index of the fixed grain */
  Ref ref;
  Bool alloced;                 /* is this grain allocated? */
  Bool isStringSeg;             /* is it a string segment? */

  AVERT_CRITICAL(Pool, pool);
  AVER_CRITICAL(CHECKT(EPVM, Pool2EPVM(pool)));
  UNUSED(pool); /* not used in hot varieties */
  AVERT_CRITICAL(ScanState, ss);
  AVERT_CRITICAL(Seg, seg);
  AVER_CRITICAL(refIO != NULL);

  amsseg = Seg2AMSSeg(seg);
  AVERT_CRITICAL(EPVMSeg, Seg2EPVMSeg(seg)); /* checks amsseg too */
  /* It's a white seg, so it must have colour tables. */
  AVER_CRITICAL(amsseg->colourTablesInUse);

  ref = *refIO;
  i = AMS_ADDR_INDEX(seg, ref);
  AVER_CRITICAL(!AMS_IS_INVALID_COLOUR(seg, i));
  alloced = AMS_ALLOCED(seg, i);
  isStringSeg = (SegRankSet(seg) == RankSetEMPTY);

  switch (ss->rank) {
  case RankAMBIG:
    break; /* all real references to PS VM are unambiguous */
  case RankEXACT: case RankFINAL: case RankWEAK: case RankWEAKFINAL:
    /* A real reference must be both allocated and aligned (except in */
    /* string segments, see design.mps.poolepvm.low.align). */
    AVER_CRITICAL(alloced);
    AVER_CRITICAL(isStringSeg
                  || AddrIsAligned((Addr)ref, PoolAlignment(pool)));
    if (AMS_IS_WHITE(seg, i)) {
      ++ss->preservedInPlaceCount;
      if (isStringSeg) {
        /* turn it black (design.mps.poolepvm.arch.obj-string) */
        AMS_WHITE_BLACKEN(seg, i);
      } else {
        /* turn it grey */
        AMS_WHITE_GREYEN(seg, i);
        /* turn this segment grey */
        SegSetGrey(seg, TraceSetUnion(SegGrey(seg), ss->traces));
        /* mark it for scanning - design.mps.poolams.marked.fix */
        amsseg->marksChanged = TRUE;
      }
    }
    break;
  default:
    NOTREACHED;
  }

  return ResOK;
}


/* EPVMResolve -- the pool class resolve method */

static Bool EPVMResolve(Pool pool, ScanState ss, Seg seg, Ref *refIO)
{
  AMSSeg amsseg;
  Index i;                      /* the index of the fixed grain */
  Ref ref;
  Bool alloced;                 /* is this grain allocated? */
  Bool isStringSeg;             /* is it a string segment? */

  AVERT_CRITICAL(Pool, pool);
  AVER_CRITICAL(CHECKT(EPVM, Pool2EPVM(pool)));
  UNUSED(pool); /* not used in hot varieties */
  AVERT_CRITICAL(ScanState, ss);
  AVERT_CRITICAL(Seg, seg);
  AVER_CRITICAL(refIO != NULL);

  amsseg = Seg2AMSSeg(seg);
  AVERT_CRITICAL(EPVMSeg, Seg2EPVMSeg(seg)); /* checks amsseg too */
  /* It's a white seg, so it must have colour tables. */
  AVER_CRITICAL(amsseg->colourTablesInUse);

  ref = *refIO;
  i = AMS_ADDR_INDEX(seg, ref);
  AVER_CRITICAL(!AMS_IS_INVALID_COLOUR(seg, i));
  alloced = AMS_ALLOCED(seg, i);
  isStringSeg = (SegRankSet(seg) == RankSetEMPTY);

  /* A real reference must be both allocated and aligned (except in */
  /* string segments, see design.mps.poolepvm.low.align). */
  AVER_CRITICAL(alloced);
  AVER_CRITICAL(isStringSeg
                || AddrIsAligned((Addr)ref, PoolAlignment(pool)));
  return !AMS_IS_WHITE(seg, i);
}


/* EPVMFreeWalk -- free block walking method of the pool class */

static void EPVMFreeWalk(Pool pool, FreeBlockStepMethod f, void *p)
{
  EPVM epvm;
  Index i;
  Ring node, ring, nextNode;    /* for iterating over the segments */

  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);

  for(i = 0; i <= epvm->maxSaveLevel; ++i) {
    EPVMSave save = EPVMLevelSave(epvm, i);

    ring = &save->segRing;
    RING_FOR(node, ring, nextNode) {
      AMSSegFreeWalk(RING_ELT(AMSSeg, segRing, node), f, p);
    }
  }
}


/* EPVMDescribe -- the pool class description method
 * 
 * Iterates over the segments, describing all of them.  */

static Res EPVMDescribe(Pool pool, mps_lib_FILE *stream)
{
  EPVM epvm; AMS ams;
  Ring node, nextNode;
  Res res;
  Index i;

  if (!CHECKT(Pool, pool)) return ResFAIL;
  epvm = Pool2EPVM(pool);
  if (!CHECKT(EPVM, epvm)) return ResFAIL;
  ams = EPVM2AMS(epvm);
  if (stream == NULL) return ResFAIL;

  res = WriteF(stream,
               "EPVM $P {\n", (WriteFP)epvm,
               "  pool $P ($U)\n",
               (WriteFP)pool, (WriteFU)pool->serial,
               "  grain shift $U\n", (WriteFU)ams->grainShift,
               "  save level $U (max $U)\n",
               (WriteFU)epvm->saveLevel, (WriteFU)epvm->maxSaveLevel,
               NULL);
  if (res != ResOK) return res;

  res = WriteF(stream,
               "  saves and segments\n"
               "    * = black, + = grey, - = white, . = alloc, ! = bad\n"
               "    buffers: [ = base, < = scan limit, | = init,\n"
               "             > = alloc, ] = limit\n",
               NULL);
  if (res != ResOK) return res;

  for(i = 0; i <= epvm->maxSaveLevel; ++i) {
    EPVMSave save = EPVMLevelSave(epvm, i);
    res = WriteF(stream,
                 "  level $U:\n", (WriteFU)save->level,
                 "    size $U\n", (WriteFU)save->size,
                 "    segments:\n",
                 NULL);
    if (res != ResOK) return res;
    RING_FOR(node, &save->segRing, nextNode) {
      EPVMSeg epvmSeg = RING_ELT(EPVMSeg, amsSegStruct.segRing, node);
      res = SegDescribe(EPVMSeg2Seg(epvmSeg), stream);
      if (res != ResOK) return res;
    }
  }

  res = WriteF(stream, "} EPVM $P\n", (WriteFP)epvm, NULL);
  return res;
}


/* EPVMPoolClass -- the pool class definition */

typedef AMSPoolClassStruct EPVMPoolClassStruct;

DEFINE_CLASS(EPVMPoolClass, this)
{
  AbstractCollectPoolClass acpc = &this->acpClass;

  INHERIT_CLASS(this, AMSPoolClass);
  acpc->name = "EPVM";
  acpc->size = sizeof(EPVMStruct);
  acpc->offset = offsetof(EPVMStruct, amsStruct.poolStruct);
  acpc->init = EPVMInit;
  acpc->finish = EPVMFinish;
  acpc->bufferClass = EPVMBufferClassGet;
  acpc->scan = EPVMScan;
  acpc->fix = EPVMFix;
  acpc->fixEmergency = EPVMFix;
  acpc->resolve = EPVMResolve;
  acpc->freewalk = EPVMFreeWalk;
  acpc->describe = EPVMDescribe;
  this->segSize = EPVMSegSizePolicy;
  this->allocRing = EPVMCurrentRing;
  this->segClass = EPVMSegClassGet;
}


/* EPVMDebugMixin - find debug mixin in class EPVMDebug */

static PoolDebugMixin EPVMDebugMixin(Pool pool)
{
  EPVM epvm;

  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  /* Can't check EPVM details, because this is called from superclasses */
  AVER(CHECKT(EPVM, epvm));
  /* Can't check EPVMDebug, because this is called during init */
  return &(EPVM2EPVMDebug(epvm)->debug);
}


/* EPVMDebugPoolClass -- the class definition for the debug version */

DEFINE_ALIAS_CLASS(EPVMDebugPoolClass, AMSPoolClass, this)
{
  AbstractCollectPoolClass acpc = &this->acpClass;

  INHERIT_CLASS(this, EPVMPoolClass);
  PoolClassMixInDebug(acpc);
  acpc->name = "EPVMDBG";
  acpc->size = sizeof(EPVMDebugStruct);
  acpc->debugMixin = EPVMDebugMixin;
}


/* EPVMCheck -- the check method for an EPVM */

Bool EPVMCheck(EPVM epvm)
{
  Index i;
  Size size;

  CHECKS(EPVM, epvm);
  CHECKL(AMSCheck(EPVM2AMS(epvm)));
  CHECKL(IsSubclassPoly(EPVM2Pool(epvm)->class, EPVMPoolClassGet()));
  CHECKL(epvm->saveLevel <= epvm->maxSaveLevel);
  /* subsequentSegRound is ArenaAligned too, but we can't get arena */
  CHECKL(epvm->subsequentSegRound > 0);
  CHECKL(epvm->saves != NULL);
  /* design.mps.poolepvm.low.size */
  size = 0;
  for(i = 0; i <= epvm->saveLevel; ++i) {
    EPVMSave save = EPVMLevelSave(epvm, i);
    CHECKD(EPVMSave, save);
    CHECKL(save->level == i);
    size += save->size;
  }
  CHECKL(size == PoolManagedSize(EPVM2Pool(epvm)));

  /* Check that the interfaces are type compatible */
  CHECKL(CHECKTYPE(mps_epvm_save_level_t, Index));
  /* We assume all the common types have been checked in mpsi.c. */

  return TRUE;
}


/* mps_class_epvm -- return the pool class descriptor to the client */

mps_class_t MPS_CALL mps_class_epvm(void)
{
  return (mps_class_t)EPVMPoolClassGet();
}


/* mps_class_epvm_debug -- return the debug pool class descriptor */

mps_class_t MPS_CALL mps_class_epvm_debug(void)
{
  return (mps_class_t)EPVMDebugPoolClassGet();
}


/* mps_epvm_check -- check if a pointer points to an EPVM pool
 * 
 * See design.mps.poolepvm.low.check.  */

mps_bool_t MPS_CALL mps_epvm_check(mps_pool_t *mps_pool_o,
                          mps_epvm_save_level_t *mps_level_o,
                          mps_arena_t mps_arena,
                          mps_addr_t mps_addr)
{
  Pool pool;
  EPVMSeg epvmSeg;
  Seg seg;
  Bool b;
  Addr addr;
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);
  
  AVER(mps_pool_o != NULL);
  AVER(mps_level_o != NULL);
  AVER(mps_addr != NULL);

  addr = (Addr)mps_addr;
  b = SegOfAddr(&seg, arena, addr);
  if (!b)
    goto returnFalse;
  
  pool = SegPool(seg);
  if (!IsSubclassPoly(pool->class, EPVMPoolClassGet()))
    goto returnFalse;

  epvmSeg = Seg2EPVMSeg(seg);
  AVERT(EPVMSeg, epvmSeg);
  AVER(AMS_ALLOCED(seg, AMS_ADDR_INDEX(seg, addr)));

  *mps_pool_o = (mps_pool_t)pool;
  *mps_level_o = (mps_epvm_save_level_t)epvmSeg->save->level;
  ArenaLeave(arena);
  return TRUE;

returnFalse:
  ArenaLeave(arena);
  return FALSE;
}


/* mps_epvm_save -- increment the save level */

void MPS_CALL mps_epvm_save(mps_pool_t mps_pool)
{
  Pool pool = (Pool)mps_pool;
  EPVM epvm;
  EPVMSave save;
  Ring node, nextNode;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);
  ArenaEnter(arena);

  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);
  AVER(epvm->saveLevel < epvm->maxSaveLevel);

  /* detach any buffers */
  save = EPVMCurrentSave(epvm);
  RING_FOR(node, &save->segRing, nextNode) {
    EPVMSeg epvmSeg = RING_ELT(EPVMSeg, amsSegStruct.segRing, node);
    Buffer buffer;

    AVERT_CRITICAL(EPVMSeg, epvmSeg);
    buffer = SegBuffer(EPVMSeg2Seg(epvmSeg));
    /* .buffer.single-thread: This relies on single-threaded access, */
    /* otherwise the buffer could be half-way through commit. */
    if (buffer != NULL)
      BufferDetach(buffer, pool);
  }

  ++epvm->saveLevel;

  EVENT_P(PoolPush, pool);
  ArenaLeave(arena);
}


/* mps_epvm_restore -- return to an earlier save level
 *
 * Discard the more recent levels.  */

void MPS_CALL mps_epvm_restore(mps_pool_t mps_pool,
                               mps_epvm_save_level_t mps_level)
{
  Pool pool = (Pool)mps_pool;
  EPVM epvm;
  Arena arena;
  Index level = (Index)mps_level;
  Index i;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);
  ArenaEnter(arena);

  AVERT(Pool, pool);
  epvm = Pool2EPVM(pool);
  AVERT(EPVM, epvm);
  AVER(level < epvm->saveLevel);

  i = level+1;
  while (i <= epvm->saveLevel) {
    epvmSaveDiscard(EPVMLevelSave(epvm, i));
    ++i;
  }
  epvm->saveLevel = level;

  EVENT_PW(PoolPop, pool, level);
  ArenaLeave(arena);
}


/* mps_epvm_collect -- collect two pools
 *
 * This will do a complete collection of the two pools in one go.  This
 * is a complete hack to support the local-only collection of PS. */

mps_res_t MPS_CALL mps_epvm_collect(mps_pool_t mps_pool1, mps_pool_t mps_pool2)
{
  Pool pool1 = (Pool)mps_pool1;
  Pool pool2 = (Pool)mps_pool2;
  EPVM epvm1;
  EPVM epvm2;
  Arena arena;
  Trace trace;
  Res res;

  AVER(CHECKT(Pool, pool1));
  arena = PoolArena(pool1);
  ArenaEnter(arena);

  AVERT(Pool, pool1);
  epvm1 = Pool2EPVM(pool1);
  AVERT(EPVM, epvm1);
  AVERT(Pool, pool2);
  epvm2 = Pool2EPVM(pool2);
  AVERT(EPVM, epvm2);

  res = TraceCreate(&trace, arena);
  AVER(res == ResOK); /* Should be available, since nothing else starts them. */

  res = ChainCondemnAll(AMSChain(EPVM2AMS(epvm1)), trace);
  if (res != ResOK)
    goto failCondemn1;
  res = ChainCondemnAll(AMSChain(EPVM2AMS(epvm2)), trace);
  if (res != ResOK)
    goto failCondemn2;

  TraceStart(trace, 0.0, 0.0);
  TracePerform(trace);
  TraceDestroy(trace);

  ArenaLeave(arena);
  return MPS_RES_OK;

failCondemn2:
failCondemn1:
  TraceDestroy(trace);
  ArenaLeave(arena);
  return res;
}
