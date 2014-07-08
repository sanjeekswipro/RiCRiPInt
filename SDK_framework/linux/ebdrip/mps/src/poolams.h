/* impl.h.poolams: AUTOMATIC MARK & SWEEP POOL CLASS INTERFACE
 *
 * $Id: poolams.h,v 1.33.1.1.1.1 2013/12/19 11:27:05 anon Exp $
 * $HopeName: MMsrc!poolams.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * .purpose: Internal interface to AMS functionality.  */

#ifndef poolams_h
#define poolams_h

#include "mpmtypes.h"
#include "mpmst.h"
#include "ring.h"
#include "bt.h"
#include <stdarg.h>


typedef struct AMSStruct *AMS;
typedef struct AMSSegStruct *AMSSeg;


typedef struct AMSStruct {
  PoolStruct poolStruct;       /* generic pool structure */
  Shift grainShift;            /* log2 of grain size */
  Chain chain;                 /* chain used by this pool */
  PoolGenStruct pgen;          /* generation representing the pool */
  Size free;                   /* total size of free space in the pool */
  RingStruct segRing;          /* ring of segments in the pool */
  Bool shareAllocTable;        /* the alloc table is also used as white table */
  Sig sig;                     /* design.mps.pool.outer-structure.sig */
} AMSStruct;


typedef struct AMSSegStruct {
  GCSegStruct gcSegStruct;  /* superclass fields must come first */
  AMS ams;               /* owning ams */
  RingStruct segRing;    /* ring that this seg belongs to */
  Count grains;          /* number of grains */
  Count free;            /* number of free grains */
  Count newAlloc;        /* number of grains allocated since last GC */
  Bool allocTableInUse;  /* allocTable is used */
  Index firstFree;       /* 1st free grain, if allocTable is not used */
  BT allocTable;         /* set if grain is allocated */
  /* design.mps.poolams.colour.single */
  Bool marksChanged;     /* seg has been marked since last scan */
  Bool ambiguousFixes;   /* seg has been ambiguously marked since last scan */
  Bool colourTablesInUse;/* the colour tables are in use */
  BT nonwhiteTable;      /* set if grain not white */
  BT nongreyTable;       /* set if not first grain of grey object */
  Sig sig;
} AMSSegStruct;


/* macros to get between child and parent structures */

#define Seg2AMSSeg(seg) ((AMSSeg)(void*)(seg))
#define AMSSeg2Seg(amsseg) (&(amsseg)->gcSegStruct.segStruct)

#define Pool2AMS(pool) PARENT(AMSStruct, poolStruct, pool)
#define AMS2Pool(ams) (&(ams)->poolStruct)


/* macros for abstracting index/address computations */
/* design.mps.poolams.addr-index.slow */

/* only use when size is a multiple of the grain size */
#define AMSGrains(ams, size) ((size) >> (ams)->grainShift)

#define AMSGrainsSize(ams, grains) ((grains) << (ams)->grainShift)

#define AMSSegShift(seg) (Seg2AMSSeg(seg)->ams->grainShift)

#define AMS_ADDR_INDEX(seg, addr) \
  ((Index)(AddrOffset(SegBase(seg), addr) >> AMSSegShift(seg)))
#define AMS_INDEX_ADDR(seg, index) \
  AddrAdd(SegBase(seg), (Size)(index) << AMSSegShift(seg))


/* colour ops */

#define AMS_IS_WHITE(seg, index) \
  (!BTGet(Seg2AMSSeg(seg)->nonwhiteTable, index))

#define AMS_IS_GREY(seg, index) \
  (!BTGet(Seg2AMSSeg(seg)->nongreyTable, index))

#define AMS_IS_BLACK(seg, index) \
  (!AMS_IS_GREY(seg, index) && !AMS_IS_WHITE(seg, index))

#define AMS_IS_INVALID_COLOUR(seg, index) \
  (AMS_IS_GREY(seg, index) && !AMS_IS_WHITE(seg, index))

#define AMS_WHITE_GREYEN(seg, index) \
  BEGIN \
    BTRes(Seg2AMSSeg(seg)->nongreyTable, index); \
  END

#define AMS_GREY_BLACKEN(seg, index) \
  BEGIN \
    BTSet(Seg2AMSSeg(seg)->nongreyTable, index); \
    BTSet(Seg2AMSSeg(seg)->nonwhiteTable, index); \
  END

#define AMS_WHITE_BLACKEN(seg, index) \
  BEGIN \
    BTSet(Seg2AMSSeg(seg)->nonwhiteTable, index); \
  END

#define AMS_RANGE_WHITE_BLACKEN(seg, base, limit) \
  BEGIN \
    BTSetRange(Seg2AMSSeg(seg)->nonwhiteTable, base, limit); \
  END

#define AMS_RANGE_BLACKEN(seg, base, limit) \
  BEGIN \
    BTSetRange(Seg2AMSSeg(seg)->nonwhiteTable, base, limit); \
    BTSetRange(Seg2AMSSeg(seg)->nongreyTable, base, limit); \
  END

#define AMS_RANGE_WHITEN(seg, base, limit) \
  BEGIN \
    BTResRange(Seg2AMSSeg(seg)->nonwhiteTable, base, limit); \
    BTSetRange(Seg2AMSSeg(seg)->nongreyTable, base, limit); \
  END

#define AMSFindGrey(pos, dummy, seg, base, limit) \
  BTFindShortResRange(pos, dummy, Seg2AMSSeg(seg)->nongreyTable, \
                      base, limit, 1)

#define AMSFindWhite(pos, dummy, seg, base, limit) \
  BTFindShortResRange(pos, dummy, Seg2AMSSeg(seg)->nonwhiteTable, \
                      base, limit, 1)

#define AMS_FIND_WHITE_RANGE(baseOut, limitOut, seg, base, limit) \
  BTFindLongResRange(baseOut, limitOut, Seg2AMSSeg(seg)->nonwhiteTable, \
                     base, limit, 1)

#define AMS_ALLOCED(seg, index) \
  (Seg2AMSSeg(seg)->allocTableInUse \
   ? BTGet(Seg2AMSSeg(seg)->allocTable, index) \
   : (Seg2AMSSeg(seg)->firstFree > (index)))


/* the rest */

extern Res AMSInitInternal(AMS ams, Format format, Chain chain,
                           Bool shareAllocTable);
extern void AMSFinish(Pool pool);
extern Bool AMSCheck(AMS ams);

extern Res AMSScan(Bool *totalReturn, ScanState ss, Pool pool, Seg seg);

#define AMSChain(ams) ((ams)->chain)

extern void AMSSegFreeWalk(AMSSeg amsseg, FreeBlockStepMethod f, void *p);


typedef SegClass AMSSegClass;
typedef SegClassStruct AMSSegClassStruct;
extern AMSSegClass AMSSegClassGet(void);
extern Bool AMSSegCheck(AMSSeg seg);


/* AMSRingFunction is the type of the method to find the ring that */
/* the AMS pool is allocating on. */
typedef Ring (*AMSRingFunction)(AMS ams, RankSet rankSet, Size size);
/* AMSSegClassFunction is the type of the method to indicate */
/* the segment class of an AMS pool.  Returns a subclass of AMSSegClass. */
/* The type is congruent with SegClassGet functions.  */
typedef AMSSegClass (*AMSSegClassFunction)(void);
/* AMSSegSizePolicyFunction is the type of the method which picks */
/* a segment size given an object size. */
typedef Res (*AMSSegSizePolicyFunction)(Size *sizeReturn,
                                        Pool pool, Size size,
					RankSet rankSet);


typedef struct AMSPoolClassStruct {
  AbstractCollectPoolClassStruct acpClass;
  AMSSegSizePolicyFunction segSize; /* SegSize policy */
  AMSRingFunction allocRing; /* fn to get the ring to allocate from */
  AMSSegClassFunction segClass; /* fn to get the class for segments */
} AMSPoolClassStruct;

typedef AMSPoolClassStruct *AMSPoolClass;

extern AMSPoolClass AMSPoolClassGet(void);
extern AMSPoolClass AMSDebugPoolClassGet(void);

#define AMSClassOf(ams) \
  PARENT(AMSPoolClassStruct, acpClass, AMS2Pool(ams)->class)


#endif /* poolams_h */
