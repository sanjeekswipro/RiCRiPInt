/* impl.c.cbs: COALESCING BLOCK STRUCTURE IMPLEMENTATION
 *
 * $Id: cbs.c,v 1.33.4.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!cbs.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 *
 * .intro: This is a portable implementation of coalescing block
 * structures.
 *
 * .purpose: CBSs are used to manage potentially unbounded
 * collections of memory blocks.
 *
 * .sources: design.mps.cbs.
 *
 * TRANSGRESSIONS
 *
 * .addr-is-star: The emergency lists use Addr interchangeably with
 * Addr* (and void*).  This simply hasn't been considered at all.  It
 * should use ADDR_PTR like impl.c.sac.  */

#include "cbs.h"
#include "splay.h"
#include "meter.h"
#include "poolmfs.h"
#include "mpm.h"
#include <stdlib.h>

SRCID(cbs, "$Id: cbs.c,v 1.33.4.1.1.1 2013/12/19 11:27:08 anon Exp $");


static void cbsBlockReinsert(CBS cbs, CBSBlock block, Addr base, Addr limit);


/* See design.mps.cbs.align */
#define cbsMinimumAlignment ((Align)sizeof(void *))

#define cbsOfSplayTree(tree) PARENT(CBSStruct, splayTree, (tree))
#define cbsBlockOfSplayNode(node) PARENT(CBSBlockStruct, splayNode, (node))
#define splayTreeOfCBS(tree) (&((cbs)->splayTree))
#define splayNodeOfCBSBlock(block) (&((block)->splayNode))
#define keyOfCBSBlock(block) ((void *)&((block)->base))


/* CBSEmergencyBlock* -- Getters and setters for emergency blocks
 *
 * See design.mps.cbs.impl.low-mem.inline.block.
 */

#define CBSEmergencyBlockBase(block) ((Addr)(block))
#define CBSEmergencyBlockLimit(block) ((Addr)((block)[1]))
#define CBSEmergencyBlockSize(block) \
  (AddrOffset(CBSEmergencyBlockBase(block), CBSEmergencyBlockLimit(block)))
#define CBSEmergencyBlockNext(block) ((CBSEmergencyBlock)((block)[0]))

#define CBSEmergencyBlockSetNext(block, next) \
  BEGIN (block)[0] = (void *)(next); END
#define CBSEmergencyBlockSetLimit(block, limit) \
  BEGIN (block)[1] = (void *)(limit); END

/* CBSEmergencyBlockCheck -- check emergency block */

#define CBSEmergencyBlockCheck(block) \
  ((Addr)(block) < CBSEmergencyBlockLimit(block) \
   && (CBSEmergencyBlockNext(block) == NULL \
       || CBSEmergencyBlockLimit(block) < (Addr)CBSEmergencyBlockNext(block)))


/* CBSEmergencyGrain* -- Getters and setters for emergency grains
 *
 * See design.mps.cbs.impl.low-mem.inline.grain.
 */

#define CBSEmergencyGrainBase(grain) ((Addr)(grain))
#define CBSEmergencyGrainLimit(cbs, grain) \
  AddrAdd(CBSEmergencyGrainBase(grain), CBSEmergencyGrainSize)
#define CBSEmergencyGrainSize ((Size)sizeof(void*))
#define CBSEmergencyGrainNext(grain) ((CBSEmergencyGrain)((grain)[0]))

#define CBSEmergencyGrainSetNext(grain, next) \
  BEGIN (grain)[0] = (void *)(next); END


static CBSEmergencyBlock CBSEmergencyBlockInit(Addr base, Addr limit)
{
  CBSEmergencyBlock block = (CBSEmergencyBlock)base;
  CBSEmergencyBlockSetNext(block, NULL);
  CBSEmergencyBlockSetLimit(block, limit);
  return block;
}

static CBSEmergencyGrain CBSEmergencyGrainInit(CBS cbs, Addr base, Addr limit)
{
  CBSEmergencyGrain grain = (CBSEmergencyGrain)base;
  UNUSED(cbs);
  AVER(AddrOffset(base, limit) == CBSEmergencyGrainSize);
  CBSEmergencyGrainSetNext(grain, NULL);
  return grain;
}


/* CBSEnter, CBSLeave -- Avoid re-entrance
 *
 * .enter-leave: The callbacks are restricted in what they may call.
 * These functions enforce this.
 *
 * .enter-leave.simple: Simple queries may be called from callbacks.
 */

#if defined(CHECK)

#define CBSEnter(cbs) \
  BEGIN \
    AVER(!(cbs)->inCBS); \
    (cbs)->inCBS = TRUE; \
  END

#define CBSLeave(cbs) \
  BEGIN \
    AVER((cbs)->inCBS); \
    (cbs)->inCBS = FALSE; \
  END

#else

#define CBSEnter(cbs)
#define CBSLeave(cbs) 

#endif


/* CBSCheck -- Check CBS */

Bool CBSCheck(CBS cbs)
{
  /* See .enter-leave.simple. */
  CHECKS(CBS, cbs);
  CHECKL(cbs != NULL);
  CHECKL(SplayTreeCheck(splayTreeOfCBS(cbs)));
  /* nothing to check about splayTreeSize */
  CHECKD(Pool, cbs->blockPool);
  CHECKL(BoolCheck(cbs->mayUseInline));
  CHECKL(BoolCheck(cbs->fastFind));
  CHECKL(BoolCheck(cbs->inCBS));
  CHECKL(cbs->new == NULL || FUNCHECK(cbs->new));
  CHECKL(cbs->delete == NULL || FUNCHECK(cbs->delete));
  CHECKL(cbs->grow == NULL || FUNCHECK(cbs->grow));
  CHECKL(cbs->shrink == NULL || FUNCHECK(cbs->shrink));
  CHECKL(cbs->mayUseInline || cbs->emergencyBlockList == NULL);
  CHECKL(cbs->mayUseInline || cbs->emergencyGrainList == NULL);
  /* See design.mps.cbs.align */
  CHECKL(!cbs->mayUseInline || 
         AlignIsAligned(cbs->alignment, cbsMinimumAlignment));
  /* can't check emergencyBlockList or emergencyGrainList more */
  /* Checking eblSize and eglSize is too laborious without a List ADT */
  /* No MeterCheck */

  return TRUE;
}


/* CBSBlockCheck -- See design.mps.cbs.function.cbs.block.check */

Bool CBSBlockCheck(CBSBlock block)
{
  /* See .enter-leave.simple. */
  UNUSED(block); /* Required because there is no signature */
  CHECKL(block != NULL);
  CHECKL(SplayNodeCheck(splayNodeOfCBSBlock(block)));

  /* If the block is in the middle of being deleted, */
  /* the pointers will be equal. */
  CHECKL(CBSBlockBase(block) <= CBSBlockLimit(block));
  /* Can't check maxSize because it may be invalid at the time */
  return TRUE;
}


/* CBSBlockSize -- see design.mps.cbs.function.cbs.block.size */

Size (CBSBlockSize)(CBSBlock block)
{
  /* See .enter-leave.simple. */
  return CBSBlockSize(block);
}


/* cbsSplayCompare -- Compare key to [base,limit) 
 *
 * See design.mps.splay.type.splay.compare.method
 */

static Compare cbsSplayCompare(void *key, SplayNode node)
{
  Addr base1, base2, limit2;
  CBSBlock cbsBlock;

  /* NULL key compares less than everything. */
  if (key == NULL)
    return CompareLESS;

  AVER(node != NULL);

  base1 = *(Addr *)key;
  cbsBlock = cbsBlockOfSplayNode(node);
  base2 = cbsBlock->base;
  limit2 = cbsBlock->limit;

  if (base1 < base2) 
    return CompareLESS;
  else if (base1 >= limit2)
    return CompareGREATER;
  else
    return CompareEQUAL;
}


/* cbsTestNode, cbsTestTree -- test for nodes larger than the S parameter */

static Bool cbsTestNode(SplayTree tree, SplayNode node, void *closureP)
{
  Size size = (Size)closureP;

  AVERT(SplayTree, tree);
  AVERT(SplayNode, node);
  AVER(size > 0);
  AVER(cbsOfSplayTree(tree)->fastFind);

  return CBSBlockSize(cbsBlockOfSplayNode(node)) >= size;
}

static Bool cbsTestTree(SplayTree tree, SplayNode node, void *closureP)
{
  Size size = (Size)closureP;

  AVERT(SplayTree, tree);
  AVERT(SplayNode, node);
  AVER(size > 0);
  AVER(cbsOfSplayTree(tree)->fastFind);

  return cbsBlockOfSplayNode(node)->maxSize >= size;
}


/* cbsUpdateNode -- update size info after restructuring */

static void cbsUpdateNode(SplayTree tree, SplayNode node,
                          SplayNode leftChild, SplayNode rightChild)
{
  Size maxSize;
  CBSBlock block;

  AVERT(SplayTree, tree);
  AVERT(SplayNode, node);
  if (leftChild != NULL)
    AVERT(SplayNode, leftChild);
  if (rightChild != NULL)
    AVERT(SplayNode, rightChild);
  AVER(cbsOfSplayTree(tree)->fastFind);

  block = cbsBlockOfSplayNode(node);
  maxSize = CBSBlockSize(block);

  if (leftChild != NULL) {
    Size size = cbsBlockOfSplayNode(leftChild)->maxSize;
    if (size > maxSize)
      maxSize = size;
  }

  if (rightChild != NULL) {
    Size size = cbsBlockOfSplayNode(rightChild)->maxSize;
    if (size > maxSize)
      maxSize = size;
  }

  block->maxSize = maxSize;
}


/* CBSInit -- Initialise a CBS structure
 *
 * See design.mps.cbs.function.cbs.init.
 */

Res CBSInit(Arena arena, CBS cbs, void *owner,
            CBSChangeSizeMethod new, CBSChangeSizeMethod delete,
            CBSChangeSizeMethod grow, CBSChangeSizeMethod shrink,
            Size minSize, Align alignment,
            Bool mayUseInline, Bool fastFind)
{
  Res res;
  static Bool symsInterned = FALSE;
  static Word blockPoolSym, splayTreeMeterSym, eblMeterSym, eglMeterSym;

  AVERT(Arena, arena);
  AVER(new == NULL || FUNCHECK(new));
  AVER(delete == NULL || FUNCHECK(delete));
  AVER(BoolCheck(mayUseInline));
  if (mayUseInline) {
    /* See design.mps.cbs.align */
    if (!AlignIsAligned(alignment, cbsMinimumAlignment))
      return ResPARAM;
  }

  if (!symsInterned) {
    blockPoolSym = EventInternString("CBS block pool");
    splayTreeMeterSym = EventInternString("size of splay tree");
    eblMeterSym = EventInternString("size of emergencyBlockList");
    eglMeterSym = EventInternString("size of emergencyGrainList");
    symsInterned = TRUE;
  }

  SplayTreeInit(splayTreeOfCBS(cbs), &cbsSplayCompare, 
                fastFind ? &cbsUpdateNode : NULL);
  res = PoolCreate(&(cbs->blockPool), arena, PoolClassMFS(),
                   sizeof(CBSBlockStruct) * 64, sizeof(CBSBlockStruct));
  if (res != ResOK)
    return res;
  EventLabelAddr((Addr)cbs->blockPool, blockPoolSym);
  cbs->splayTreeSize = 0;

  cbs->new = new;
  cbs->delete = delete;
  cbs->grow = grow;
  cbs->shrink = shrink;
  cbs->minSize = minSize;
  cbs->mayUseInline = mayUseInline;
  cbs->fastFind = fastFind;
  cbs->alignment = alignment;
  cbs->inCBS = TRUE;
  cbs->emergencyBlockList = NULL; cbs->eblSize = 0;
  cbs->emergencyGrainList = NULL; cbs->eglSize = 0;

  METER_INIT(cbs->splaySearch, splayTreeMeterSym, (void *)cbs);
  METER_INIT(cbs->eblSearch, eblMeterSym, (void *)cbs);
  METER_INIT(cbs->eglSearch, eglMeterSym, (void *)cbs);

  cbs->sig = CBSSig;

  AVERT(CBS, cbs);
  EVENT_PP(CBSInit, cbs, owner);
  UNUSED(owner); /* hack: unused in non-event varieties */
  CBSLeave(cbs);
  return ResOK;
}


/* CBSFinish -- Finish a CBS structure
 *
 * See design.mps.cbs.function.cbs.finish.
 */

void CBSFinish(CBS cbs)
{
  AVERT(CBS, cbs);
  CBSEnter(cbs);

  METER_EMIT(&cbs->splaySearch);
  METER_EMIT(&cbs->eblSearch);
  METER_EMIT(&cbs->eglSearch);

  cbs->sig = SigInvalid;

  SplayTreeFinish(splayTreeOfCBS(cbs));
  PoolDestroy(cbs->blockPool);
  cbs->emergencyBlockList = NULL;
  cbs->emergencyGrainList = NULL;
}


/* CBSClear -- Clear all nodes from CBS
 *
 * This doesn't try to inform the client - that may have to change one day.
 */
void CBSClear(CBS cbs)
{
  AVERT(CBS, cbs);
  CBSEnter(cbs);

  PoolClear(cbs->blockPool);
  cbs->splayTreeSize = 0;
  SplayTreeInit(splayTreeOfCBS(cbs), &cbsSplayCompare,
                cbs->fastFind ? &cbsUpdateNode : NULL);
  cbs->emergencyBlockList = NULL; cbs->eblSize = 0;
  cbs->emergencyGrainList = NULL; cbs->eglSize = 0;

  CBSLeave(cbs);
}


/* cbsBlockNew, cbsBlockDelete, cbsBlockMove -- CBS block change operators
 *
 * These functions are called whenever blocks are created, destroyed, or
 * moved.  They report to the client, and perform the necessary memory
 * management.  They are responsible for the client interaction logic.
 */

static void cbsBlockDelete(CBS cbs, CBSBlock block)
{
  Res res;
  Size oldSize;

  AVERT(CBSBlock, block);

  oldSize = CBSBlockSize(block);

  METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
  res = SplayTreeDelete(splayTreeOfCBS(cbs), splayNodeOfCBSBlock(block), 
                        keyOfCBSBlock(block));
  AVER(res == ResOK); /* Must be possible to delete node */
  STATISTIC(--cbs->splayTreeSize);

  /* make invalid */
  block->limit = block->base;

  if (cbs->delete != NULL && oldSize >= cbs->minSize)
    (*(cbs->delete))(cbs, block, oldSize, (Size)0);
  if ((Addr)block != block->base) /* if not internal */
    PoolFree(cbs->blockPool, (Addr)block, sizeof(CBSBlockStruct));
  return;
}


static Res cbsBlockNew(CBS cbs, Addr base, Addr limit)
{
  CBSBlock block;
  Res res;
  Addr p;
  Size newSize = AddrOffset(base, limit);

  res = PoolAlloc(&p, cbs->blockPool, sizeof(CBSBlockStruct), FALSE, NULL);
  if (res == ResOK)
    block = (CBSBlock)p;
  else if (newSize >= sizeof(CBSBlockStruct) && cbs->mayUseInline
           && ResIsAllocFailure(res))
    block = (CBSBlock)base;
  else
    return res;

  SplayNodeInit(splayNodeOfCBSBlock(block));
  block->base = base;
  block->limit = limit;
  block->maxSize = newSize;

  AVERT(CBSBlock, block);

  METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
  res = SplayTreeInsert(splayTreeOfCBS(cbs), splayNodeOfCBSBlock(block),
                        keyOfCBSBlock(block));
  AVER(res == ResOK);
  STATISTIC(++cbs->splayTreeSize);

  if (cbs->new != NULL && newSize >= cbs->minSize)
    (*(cbs->new))(cbs, block, (Size)0, newSize);
  return ResOK;
}


/* cbsBlockMove -- adjust base of internal block */

static void cbsBlockMove(CBS cbs, CBSBlock block, Addr base)
{
  Size blockSize = CBSBlockSize(block);
  Size newSize = AddrOffset(base, block->limit);
  CBSBlock newBlock = (CBSBlock)base;

  if (cbs->delete != NULL && blockSize >= cbs->minSize)
    (*(cbs->delete))(cbs, block, blockSize, 0);
  /* copy to new position, indirectly in case of overlap */
  if (abs((int)blockSize - (int)newSize) >= (int)sizeof(CBSBlockStruct))
    *newBlock = *block;
  else {
    volatile CBSBlockStruct temp = *block;
    *newBlock = temp;
  }
  AVER(cbs->splayTree.root == (SplayNode)block);
  cbs->splayTree.root = (SplayNode)newBlock;
  newBlock->base = base;
  if (cbs->fastFind) {
    SplayNodeRefresh(splayTreeOfCBS(cbs), splayNodeOfCBSBlock(newBlock),
                     keyOfCBSBlock(newBlock));
    AVER(CBSBlockSize(newBlock) <= newBlock->maxSize);
  }
  if (cbs->new != NULL && newSize >= cbs->minSize)
    (*(cbs->new))(cbs, newBlock, 0, newSize);
}


/* cbsBlockShrunk, cbsBlockGrown -- CBS block change notifications
 *
 * These functions are called after blocks have changed size, to report
 * to the client and maintain the splay tree maxSizes.
 */

static void cbsBlockShrunk(CBS cbs, CBSBlock block, Size oldSize)
{
  Size newSize;

  newSize = CBSBlockSize(block);
  AVER(oldSize > newSize);

  if (cbs->fastFind) {
    SplayNodeRefresh(splayTreeOfCBS(cbs), splayNodeOfCBSBlock(block),
                     keyOfCBSBlock(block));
    AVER(CBSBlockSize(block) <= block->maxSize);
  }

  if (cbs->delete != NULL && oldSize >= cbs->minSize && newSize < cbs->minSize)
    (*(cbs->delete))(cbs, block, oldSize, newSize);
  else if (cbs->shrink != NULL && newSize >= cbs->minSize)
    (*(cbs->shrink))(cbs, block, oldSize, newSize);
}

static void cbsBlockGrown(CBS cbs, CBSBlock block, Size oldSize)
{
  Size newSize;

  newSize = CBSBlockSize(block);
  AVER(oldSize < newSize);

  if (cbs->fastFind) {
    SplayNodeRefresh(splayTreeOfCBS(cbs), splayNodeOfCBSBlock(block),
                     keyOfCBSBlock(block));
    AVER(CBSBlockSize(block) <= block->maxSize);
  }

  if (cbs->new != NULL && oldSize < cbs->minSize && newSize >= cbs->minSize)
    (*(cbs->new))(cbs, block, oldSize, newSize);
  else if (cbs->grow != NULL && oldSize >= cbs->minSize)
    (*(cbs->grow))(cbs, block, oldSize, newSize);
}


/* cbsBlockShrinkHigh -- shrink a CBSBlock towards the high end */

static void cbsBlockShrinkHigh(CBS cbs, CBSBlock cbsBlock, Addr newBase)
{
  if ((Addr)cbsBlock == cbsBlock->base) { /* internal block, must adjust */
    if (AddrOffset(newBase, cbsBlock->limit) >= sizeof(CBSBlockStruct))
      /* enough space remains, copy node to remaining part */
      cbsBlockMove(cbs, cbsBlock, newBase);
    else
      cbsBlockReinsert(cbs, cbsBlock, newBase, cbsBlock->limit);
  } else { /* not internal block */
    Size oldSize = CBSBlockSize(cbsBlock);

    cbsBlock->base = newBase;
    cbsBlockShrunk(cbs, cbsBlock, oldSize);
  }
}


/* cbsBlockShrinkLow -- shrink a CBSBlock towards the low end */

static void cbsBlockShrinkLow(CBS cbs, CBSBlock cbsBlock, Addr newLimit)
{
  if ((Addr)cbsBlock == cbsBlock->base
      && AddrOffset(cbsBlock, newLimit) < sizeof(CBSBlockStruct)) {
    /* internal block, remaining part not large enough */
    cbsBlockReinsert(cbs, cbsBlock, (Addr)cbsBlock, newLimit);
  } else { /* not internal block, or still large enough */
    Size oldSize = CBSBlockSize(cbsBlock);

    cbsBlock->limit = newLimit;
    cbsBlockShrunk(cbs, cbsBlock, oldSize);
  }
}


/* cbsInsertIntoTree -- Insert a range into the splay tree */

static Res cbsInsertIntoTree(Addr *baseReturn, Addr *limitReturn,
                             CBS cbs, Addr base, Addr limit)
{
  Res res;
  Addr newBase, newLimit;
  SplayNode leftSplay, rightSplay;
  CBSBlock leftCBS, rightCBS;
  Bool leftMerge, rightMerge;
  Size oldSize;

  AVER(base != (Addr)0);
  AVER(base < limit);
  AVER(AddrIsAligned(base, cbs->alignment));
  AVER(AddrIsAligned(limit, cbs->alignment));

  METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
  res = SplayTreeNeighbours(&leftSplay, &rightSplay,
                            splayTreeOfCBS(cbs), (void *)&limit);
  if (res != ResOK)
    goto fail;

  if (leftSplay == NULL) {
    leftCBS = NULL;
    leftMerge = FALSE;
  } else {
    leftCBS = cbsBlockOfSplayNode(leftSplay);
    if (leftCBS->limit > base) {
      res = ResFAIL;
      goto fail;
    }
    leftMerge = leftCBS->limit == base;
  }

  if (rightSplay == NULL) {
    rightCBS = NULL;
    rightMerge = FALSE;
  } else {
    rightCBS = cbsBlockOfSplayNode(rightSplay);
    if (limit > rightCBS->base) {
      res = ResFAIL;
      goto fail;
    }
    rightMerge = rightCBS->base == limit;
  }

  newBase = leftMerge ? CBSBlockBase(leftCBS) : base;
  newLimit = rightMerge ? CBSBlockLimit(rightCBS) : limit;

  if (leftMerge) {
    if (rightMerge) {
      Size oldLeftSize = CBSBlockSize(leftCBS);
      Size oldRightSize = CBSBlockSize(rightCBS);

      /* must block larger neighbour and destroy smaller neighbour; */
      /* see design.mps.cbs.function.cbs.insert.callback */
      if (oldLeftSize >= oldRightSize) {
        Addr rightLimit = rightCBS->limit;
        cbsBlockDelete(cbs, rightCBS);
        leftCBS->limit = rightLimit;
        cbsBlockGrown(cbs, leftCBS, oldLeftSize);
      } else { /* left block is smaller */
        Addr leftBase = leftCBS->base;
        cbsBlockDelete(cbs, leftCBS);
        if ((Addr)rightCBS == rightCBS->base) { /* internal, must adjust */
          if (cbs->splayTree.root != (SplayNode)rightCBS) {
            /* make sure it's root */
            res = SplayTreeSearch(&rightSplay, splayTreeOfCBS(cbs),
                                  (void*)&rightCBS);
            AVER(res == ResOK);
          }
          cbsBlockMove(cbs, rightCBS, leftBase);
        } else {
          rightCBS->base = leftBase;
          cbsBlockGrown(cbs, rightCBS, oldRightSize);
        }
      }
    } else { /* leftMerge, !rightMerge */
      oldSize = CBSBlockSize(leftCBS);
      leftCBS->limit = limit;
      cbsBlockGrown(cbs, leftCBS, oldSize);
    }
  } else { /* !leftMerge */
    if (rightMerge) {
      oldSize = CBSBlockSize(rightCBS);
      if ((Addr)rightCBS == rightCBS->base) { /* internal, must adjust */
        if (cbs->splayTree.root != (SplayNode)rightCBS) {
          /* make sure it's root */
          res = SplayTreeSearch(&rightSplay, splayTreeOfCBS(cbs),
                                (void*)&rightCBS);
          AVER(res == ResOK);
        }
        cbsBlockMove(cbs, rightCBS, base);
      } else {
        rightCBS->base = base;
        cbsBlockGrown(cbs, rightCBS, oldSize);
      }
    } else { /* !leftMerge, !rightMerge */
      res = cbsBlockNew(cbs, base, limit);
      if (res != ResOK) 
        goto fail;
    }
  }

  AVER(newBase <= base);
  AVER(newLimit >= limit);
  *baseReturn = newBase;
  *limitReturn = newLimit;

  return ResOK;

fail:
  AVER(res != ResOK);
  return res;
}


/* cbsCoalesceWithEmergencyLists -- coalesce received range with EBL and EGL
 *
 * Attempts to extend the range about to be freed by adding ranges from
 * the emergency lists.  May remove blocks from the emergency list.
 */

static Res cbsCoalesceWithEmergencyLists(Addr *baseIO, Addr *limitIO, CBS cbs)
{
  Addr base, limit;
  Count nCoalescences = 0;

  AVER(baseIO != NULL);
  AVER(limitIO != NULL);
  AVER(cbs->mayUseInline);

  base = *baseIO;
  limit = *limitIO;
  AVER(base < limit);

  if (cbs->emergencyBlockList != NULL) {
    CBSEmergencyBlock prev, block, next;
    Addr blockBase, blockLimit;

    METER_ACC(cbs->eblSearch, cbs->eblSize);
    for(block = cbs->emergencyBlockList, prev = NULL;
        block != NULL && CBSEmergencyBlockBase(block) <= limit;
        block = CBSEmergencyBlockNext(block)) {

      blockBase = CBSEmergencyBlockBase(block);
      blockLimit = CBSEmergencyBlockLimit(block);
      AVER(blockBase < blockLimit);
      if (prev != NULL)
        AVER(CBSEmergencyBlockLimit(prev) < blockBase);

      if (blockLimit == base) {
        /* Unlink the block. */
        base = blockBase;
        next = CBSEmergencyBlockNext(block);
        if (prev == NULL)
          cbs->emergencyBlockList = next;
        else
          CBSEmergencyBlockSetNext(prev, next);
        ++nCoalescences;
        STATISTIC(--cbs->eblSize);
        AVER(cbs->emergencyBlockList != NULL || cbs->eblSize == 0);
      } else if (blockBase == limit) {
        limit = blockLimit;
        next = CBSEmergencyBlockNext(block);
        if (prev == NULL)
          cbs->emergencyBlockList = next;
        else
          CBSEmergencyBlockSetNext(prev, next);
        ++nCoalescences;
        STATISTIC(--cbs->eblSize);
        AVER(cbs->emergencyBlockList != NULL || cbs->eblSize == 0);
        break; /* There can't be any more. */
      } else if (blockLimit > base) {
        return ResFAIL; /* range intersects block */
      } else {
        prev = block; /* Only move prev if we didn't delete */
      }
      /* block's next is still valid, even if it's been coalesced */
    }
  }

  if (cbs->emergencyGrainList != NULL) {
    CBSEmergencyGrain prev, grain, next;
    Addr grainBase, grainLimit;

    METER_ACC(cbs->eglSearch, cbs->eglSize);
    for(grain = cbs->emergencyGrainList, prev = NULL;
        grain != NULL && CBSEmergencyGrainBase(grain) <= limit
        && nCoalescences < 2;
        grain = CBSEmergencyGrainNext(grain)) {
      grainBase = CBSEmergencyGrainBase(grain);
      grainLimit = CBSEmergencyGrainLimit(cbs, grain);
      /* Pointless to check grainBase < grainLimit, as they're tied. */


      if (prev != NULL)
        AVER(CBSEmergencyGrainLimit(cbs, prev) < grainBase);

      if (grainLimit == base) {
        base = grainBase;
        next = CBSEmergencyGrainNext(grain);
        if (prev == NULL)
          cbs->emergencyGrainList = next;
        else
          CBSEmergencyGrainSetNext(prev, next);
        ++nCoalescences;
        STATISTIC(--cbs->eglSize);
        AVER(cbs->emergencyGrainList != NULL || cbs->eglSize == 0);
      } else if (grainBase == limit) {
        limit = grainLimit;
        next = CBSEmergencyGrainNext(grain);
        if (prev == NULL)
          cbs->emergencyGrainList = next;
        else
          CBSEmergencyGrainSetNext(prev, next);
        ++nCoalescences;
        STATISTIC(--cbs->eglSize);
        AVER(cbs->emergencyGrainList != NULL || cbs->eglSize == 0);
        break;
      } else if (grainLimit > base) {
        return ResFAIL; /* range intersects grain */
      } else {
        prev = grain;
      }
    }
    /* grain's next is still valid, even if it's been coalesced */
  }

  /* Because the lists are known to have isolated ranges, there can */
  /* be no more than 2 coalescences. */
  AVER(nCoalescences <= 2); 

  *baseIO = base;
  *limitIO = limit;
  return ResOK;
}


/* cbsAddToEmergencyLists -- Adds range to emergency lists
 *
 * The range must be unadjacent to any items on the emergency lists.
 */

static Res cbsAddToEmergencyLists(CBS cbs, Addr base, Addr limit) 
{
  Res res = ResOK;
  Size size;

  AVER(base < limit);
  AVER(cbs->mayUseInline);

  size = AddrOffset(base, limit);
  /* Use the block list if possible.  See design.mps.cbs.align. */
  if (size > CBSEmergencyGrainSize) {
    CBSEmergencyBlock prev, block, new;
    new = CBSEmergencyBlockInit(base, limit);
    METER_ACC(cbs->eblSearch, cbs->eblSize);
    for(prev = NULL, block = cbs->emergencyBlockList;
        block != NULL && CBSEmergencyBlockBase(block) < base;
        prev = block, block = CBSEmergencyBlockNext(block)) {
      if (prev != NULL)
        AVER(CBSEmergencyBlockLimit(prev) < CBSEmergencyBlockBase(block));
      AVER(CBSEmergencyBlockBase(block) < CBSEmergencyBlockLimit(block));
    }

    if (prev != NULL && block != NULL)
      AVER(CBSEmergencyBlockLimit(prev) < CBSEmergencyBlockBase(block));

    /* check ordering: prev ... new ... block */
    if (prev != NULL && CBSEmergencyBlockLimit(prev) >= base)
      return ResFAIL; /* range intersects with existing block */

    if (block != NULL && limit >= CBSEmergencyBlockBase(block))
      return ResFAIL; /* range intersects with existing block */

    if (prev == NULL) 
      cbs->emergencyBlockList = new;
    else
      CBSEmergencyBlockSetNext(prev, new);
    CBSEmergencyBlockSetNext(new, block); /* may be NULL */
    STATISTIC(++cbs->eblSize);
    AVER(CBSEmergencyBlockCheck(new));
  } else if (size == CBSEmergencyGrainSize) {
    CBSEmergencyGrain prev, grain, new;
    new = CBSEmergencyGrainInit(cbs, base, limit);
    METER_ACC(cbs->eglSearch, cbs->eglSize);
    for(prev = NULL, grain = cbs->emergencyGrainList;
        grain != NULL && CBSEmergencyGrainBase(grain) < base;
        prev = grain, grain = CBSEmergencyGrainNext(grain)) {
      if (prev != NULL)
        AVER(CBSEmergencyGrainLimit(cbs, prev)
             < CBSEmergencyGrainBase(grain));
    }

    if (prev != NULL && grain != NULL)
      AVER(CBSEmergencyGrainLimit(cbs, prev) < CBSEmergencyGrainBase(grain));

    /* check ordering: prev ... new ... grain */
    if (prev != NULL && CBSEmergencyGrainLimit(cbs, prev) >= base)
      return ResFAIL; /* range intersects with existing grain */

    if (grain != NULL && limit >= CBSEmergencyGrainBase(grain))
      return ResFAIL; /* range intersects with existing grain */

    if (prev == NULL) 
      cbs->emergencyGrainList = new;
    else
      CBSEmergencyGrainSetNext(prev, new);
    CBSEmergencyGrainSetNext(new, grain); /* may be NULL */
    STATISTIC(++cbs->eglSize);
  } else {
    NOTREACHED;
    res = ResFAIL; /* in case AVERs are compiled out */
  }

  return res;
}

/* cbsFlushEmergencyLists -- Attempt to move ranges to CBS proper */

static void cbsFlushEmergencyLists(CBS cbs) 
{
  Res res = ResOK;

  if (cbs->emergencyBlockList != NULL) {
    CBSEmergencyBlock block;

    METER_ACC(cbs->eblSearch, cbs->eblSize);
    for(block = cbs->emergencyBlockList;
        block != NULL;
        block = CBSEmergencyBlockNext(block)) {
      AVER(CBSEmergencyBlockBase(block) < CBSEmergencyBlockLimit(block));
      res = cbsBlockNew(cbs, CBSEmergencyBlockBase(block),
                        CBSEmergencyBlockLimit(block));
      if (res == ResOK) {
        AVER(cbs->emergencyBlockList == block);
        cbs->emergencyBlockList = CBSEmergencyBlockNext(block);
        STATISTIC(--cbs->eblSize);
        AVER(cbs->emergencyBlockList != NULL || cbs->eblSize == 0);
      } else {
        AVER(ResIsAllocFailure(res));
        goto done;
      }
    }
    AVER(cbs->emergencyBlockList == NULL);
  }

  if (cbs->emergencyGrainList != NULL) {
    CBSEmergencyGrain grain;

    METER_ACC(cbs->eglSearch, cbs->eglSize);
    for(grain = cbs->emergencyGrainList;
        grain != NULL;
        grain = CBSEmergencyGrainNext(grain)) {
      res = cbsBlockNew(cbs, CBSEmergencyGrainBase(grain),
                        CBSEmergencyGrainLimit(cbs, grain));
      if (res == ResOK) {
        AVER(cbs->emergencyGrainList == grain);
        cbs->emergencyGrainList = CBSEmergencyGrainNext(grain);
        STATISTIC(--cbs->eglSize);
        AVER(cbs->emergencyGrainList != NULL || cbs->eglSize == 0);
      } else {
        AVER(ResIsAllocFailure(res));
        goto done;
      }
    }
  }

  done:
  return;
}


/* cbsBlockReinsert -- adjust base or limit of internal block, and reinsert
 *
 * Used when the block will be too small for internal.
 * Encapsulated in case we ever want a less naive implementation.
 */
void cbsBlockReinsert(CBS cbs, CBSBlock block, Addr base, Addr limit)
{
  Res res;

  cbsBlockDelete(cbs, block);
  res = cbsBlockNew(cbs, base, limit); /* try for external */
  if (ResIsAllocFailure(res))
    res = cbsAddToEmergencyLists(cbs, base, limit);
  AVER(res == ResOK); /* since it was already in the list */
}


/* CBSInsert -- Insert a range into the CBS
 *
 * See design.mps.cbs.functions.cbs.insert.
 */

Res CBSInsertReturningRange(Addr *baseReturn, Addr *limitReturn,
                            CBS cbs, Addr base, Addr limit)
{
  Addr newBase, newLimit;
  Res res = ResOK;

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  AVER(base != (Addr)0);
  AVER(base < limit);
  AVER(AddrIsAligned(base, cbs->alignment));
  AVER(AddrIsAligned(limit, cbs->alignment));

  if (cbs->mayUseInline) {
    newBase = base;
    newLimit = limit;

    res = cbsCoalesceWithEmergencyLists(&newBase, &newLimit, cbs);
    if (res != ResOK) {
      AVER(res == ResFAIL);
      goto done;
    }

    res = cbsInsertIntoTree(&newBase, &newLimit, cbs, newBase, newLimit);
    /* newBase and newLimit only changed if res == ResOK */

    if (ResIsAllocFailure(res)) {
      res = cbsAddToEmergencyLists(cbs, newBase, newLimit);
      if (res != ResOK) {
        AVER(res == ResFAIL);
        goto done;
      }
    } else {
      cbsFlushEmergencyLists(cbs);
    }
  } else {
    res = cbsInsertIntoTree(&newBase, &newLimit, cbs, base, limit);
  }

  done:
  if (res == ResOK) {
    AVER(newBase <= base);
    AVER(limit <= newLimit);
    *baseReturn = newBase;
    *limitReturn = newLimit;
  }

  CBSLeave(cbs);
  return res;
}


Res CBSInsert(CBS cbs, Addr base, Addr limit)
{
  Res res;
  Addr newBase, newLimit;

  /* all parameters checked by CBSInsertReturningRange */
  /* CBSEnter/Leave done by CBSInsertReturningRange */

  res = CBSInsertReturningRange(&newBase, &newLimit, cbs, base, limit);
  return res;
}


/* cbsDeleteFrom* -- delete blocks from different parts of the CBS */

static Res cbsDeleteFromTree(CBS cbs, Addr base, Addr limit)
{
  Res res;
  CBSBlock cbsBlock;
  SplayNode splayNode;
  Addr shrinkBase, shrinkLimit;

  /* parameters already checked */

  METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
  res = SplayTreeSearch(&splayNode, splayTreeOfCBS(cbs), (void *)&base);
  if (res != ResOK)
    goto failSplayTreeSearch;
  cbsBlock = cbsBlockOfSplayNode(splayNode);

  if (limit > cbsBlock->limit) {
    res = ResFAIL;
    goto failLimitCheck;
  }

  if (base == cbsBlock->base) {
    if (limit == cbsBlock->limit) { /* entire block */
      cbsBlockDelete(cbs, cbsBlock);
    } else { /* remaining fragment at end */
      AVER(limit < cbsBlock->limit);
      cbsBlockShrinkHigh(cbs, cbsBlock, limit);
    }
  } else {
    AVER(base > cbsBlock->base);
    if (limit == cbsBlock->limit) { /* remaining fragment at beginning */
      cbsBlockShrinkLow(cbs, cbsBlock, base);
    } else { /* two remaining fragments */
      Size leftNewSize = AddrOffset(cbsBlock->base, base);
      Size rightNewSize = AddrOffset(limit, cbsBlock->limit);
      /* must shrink larger fragment and create smaller; */
      /* see design.mps.cbs.function.cbs.delete.callback */
      if (leftNewSize >= rightNewSize) {
        Addr oldLimit = cbsBlock->limit;
        AVER(limit < cbsBlock->limit);
        cbsBlockShrinkLow(cbs, cbsBlock, base);
        res = cbsBlockNew(cbs, limit, oldLimit);
        if (res != ResOK) {
          AVER(ResIsAllocFailure(res));
          if (cbs->mayUseInline) {
            res = cbsAddToEmergencyLists(cbs, limit, oldLimit);
            AVER(res == ResOK);
          } else {
            shrinkBase = base; shrinkLimit = oldLimit;
            goto failNew;
          }
        }
      } else { /* high fragment is larger */
        Addr oldBase = cbsBlock->base;
        AVER(base > cbsBlock->base);
        cbsBlockShrinkHigh(cbs, cbsBlock, limit);
        res = cbsBlockNew(cbs, oldBase, base);
        if (res != ResOK) {
          AVER(ResIsAllocFailure(res));
          if (cbs->mayUseInline) {
            res = cbsAddToEmergencyLists(cbs, oldBase, base);
            AVER(res == ResOK);
          } else {
            shrinkBase = oldBase; shrinkLimit = limit;
            goto failNew;
          }
        }
      }
    }
  }
  return ResOK;

failNew:
  { /* Put the bit that was shrunk back in the block */
    Addr oldBase, oldLimit;

    AVER(!cbs->mayUseInline);
    res = cbsInsertIntoTree(&oldBase, &oldLimit, cbs, shrinkBase, shrinkLimit);
    AVER(res == ResOK); /* succeeds since the rest must be in the tree still */
  }
failLimitCheck:
failSplayTreeSearch:
  return res;
}


static Res cbsDeleteFromEmergencyBlockList(CBS cbs, Addr base, Addr limit)
{
  Res res;
  Addr blockBase, blockLimit;
  CBSEmergencyBlock prev, block;

  /* parameters already checked in caller */
  AVER(cbs->mayUseInline);

  METER_ACC(cbs->eblSearch, cbs->eblSize);
  for(prev = NULL, block = cbs->emergencyBlockList;
      block != NULL && CBSEmergencyBlockLimit(block) < limit;
      prev = block, block = CBSEmergencyBlockNext(block)) {
    AVER(CBSEmergencyBlockBase(block) < CBSEmergencyBlockLimit(block));
    if (CBSEmergencyBlockBase(block) >= base)
      return ResFAIL;
    if (prev != NULL)
      AVER(CBSEmergencyBlockLimit(prev) < CBSEmergencyBlockBase(block));
  }

  if (block != NULL) {
    blockBase = CBSEmergencyBlockBase(block);
    blockLimit = CBSEmergencyBlockLimit(block);
    AVER(blockBase < blockLimit);
    AVER(blockLimit >= limit);

    if (blockBase <= base && limit <= blockLimit) {
      /* remove from list */
      if (prev == NULL)
        cbs->emergencyBlockList = CBSEmergencyBlockNext(block);
      else
        CBSEmergencyBlockSetNext(prev, CBSEmergencyBlockNext(block));
      STATISTIC(--cbs->eblSize);
      /* @@@@ It's not efficient to remove and reinsert in the same place */
      AVER(cbs->emergencyBlockList != NULL || cbs->eblSize == 0);
      if (blockBase < base) {
        res = cbsAddToEmergencyLists(cbs, blockBase, base);
        if (res != ResOK)
          return res;
      }
      if (limit < blockLimit) {
        res = cbsAddToEmergencyLists(cbs, limit, blockLimit);
        if (res != ResOK)
          return res;
      }
      return ResOK;
    } else {
      return ResFAIL; /* partly in list */
    }
  } 
  return ResFAIL; /* not in list at all */
}


static Res cbsDeleteFromEmergencyGrainList(CBS cbs, Addr base, Addr limit)
{
  Addr grainBase, grainLimit;
  CBSEmergencyGrain prev, grain;

  /* parameters already checked in caller */
  AVER(cbs->mayUseInline);
  if (AddrOffset(base, limit) != CBSEmergencyGrainSize)
    return ResFAIL;

  METER_ACC(cbs->eglSearch, cbs->eglSize);
  for(prev = NULL, grain = cbs->emergencyGrainList;
      grain != NULL && CBSEmergencyGrainLimit(cbs, grain) < limit;
      prev = grain, grain = CBSEmergencyGrainNext(grain)) {
    if (prev != NULL)
      AVER(CBSEmergencyGrainLimit(cbs, prev) < CBSEmergencyGrainBase(grain));
  }   

  if (grain != NULL) {
    grainBase = CBSEmergencyGrainBase(grain);
    grainLimit = CBSEmergencyGrainLimit(cbs, grain);
    AVER(grainLimit >= limit);

    if (grainBase <= base && limit <= grainLimit) {
      AVER(grainBase == base);
      AVER(grainLimit == limit);
      /* remove from list */
      if (prev == NULL)
        cbs->emergencyGrainList = CBSEmergencyGrainNext(grain);
      else
        CBSEmergencyGrainSetNext(prev, CBSEmergencyGrainNext(grain));
      STATISTIC(--cbs->eglSize);
      AVER(cbs->emergencyGrainList != NULL || cbs->eglSize == 0);
      return ResOK;
    } else {
      return ResFAIL; /* range is partly in list */
    }
  } 
  return ResFAIL; /* range is not in list at all */
}


/* CBSDelete -- Remove a range from a CBS
 *
 * See design.mps.cbs.function.cbs.delete.
 */

Res CBSDelete(CBS cbs, Addr base, Addr limit)
{
  Res res;

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  AVER(base != NULL);
  AVER(limit > base);
  AVER(AddrIsAligned(base, cbs->alignment));
  AVER(AddrIsAligned(limit, cbs->alignment));

  res = cbsDeleteFromTree(cbs, base, limit);

  /* We rely on the consistency of the three free structures. */
  /* These checks don't distinguish "partially in" from "not in". */
  if (cbs->mayUseInline) {
    AVER(res == ResOK || res == ResFAIL);
    if (res == ResFAIL) { /* wasn't in tree */
      if (AddrOffset(base, limit) > CBSEmergencyGrainSize)
        res = cbsDeleteFromEmergencyBlockList(cbs, base, limit);
      else
        res = cbsDeleteFromEmergencyGrainList(cbs, base, limit);
    }
    if (res == ResOK)
      /* always worth trying, wherever we found the deleted block */
      cbsFlushEmergencyLists(cbs);
  }

  CBSLeave(cbs);
  return res;
}


Res CBSBlockDescribe(CBSBlock block, mps_lib_FILE *stream)
{
  Res res;

  if (stream == NULL) return ResFAIL;

  res = WriteF(stream,
               "[$P,$P) {$U}", 
               (WriteFP)block->base, 
               (WriteFP)block->limit,
               (WriteFU)block->maxSize,
               NULL);
  return res;
}

static Res CBSSplayNodeDescribe(SplayNode splayNode, mps_lib_FILE *stream)
{
  Res res;

  if (splayNode == NULL) return ResFAIL;
  if (stream == NULL) return ResFAIL;

  res = CBSBlockDescribe(cbsBlockOfSplayNode(splayNode), stream);
  return res;
}


/* CBSIterate -- Iterate all blocks in CBS
 *
 * This is not necessarily efficient.
 * See design.mps.cbs.function.cbs.iterate.
 */

/* Internal version without enter/leave checking. */
static void cbsIterateInternal(CBS cbs, CBSIterateMethod iterate, void *closureP)
{
  SplayNode splayNode;
  SplayTree splayTree;
  CBSBlock cbsBlock;

  AVER(FUNCHECK(iterate));

  splayTree = splayTreeOfCBS(cbs);
  /* .splay-iterate.slow: We assume that splay tree iteration does */
  /* searches and meter it. */
  METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
  splayNode = SplayTreeFirst(splayTree);
  while(splayNode != NULL) {
    cbsBlock = cbsBlockOfSplayNode(splayNode);
    if (!(*iterate)(cbs, cbsBlock, closureP)) {
      break;
    }
    METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
    splayNode = SplayTreeNext(splayTree, splayNode, keyOfCBSBlock(cbsBlock));
  }
  return;
}

void CBSIterate(CBS cbs, CBSIterateMethod iterate, void *closureP)
{
  AVERT(CBS, cbs);
  AVER(FUNCHECK(iterate));
  CBSEnter(cbs);

  cbsIterateInternal(cbs, iterate, closureP);

  CBSLeave(cbs);
  return;
}


/* CBSIterateLarge -- Iterate only large blocks
 *
 * This function iterates only blocks that are larger than or equal
 * to the minimum size.
 */

typedef struct CBSIterateLargeClosureStruct {
  void *p;
  CBSIterateMethod f;
} CBSIterateLargeClosureStruct, *CBSIterateLargeClosure;

static Bool cbsIterateLargeAction(CBS cbs, CBSBlock block, void *p)
{
  Bool b = TRUE;
  CBSIterateLargeClosure closure;

  closure = (CBSIterateLargeClosure)p;
  AVER(closure != NULL);

  if (CBSBlockSize(block) >= cbs->minSize)
    b = (closure->f)(cbs, block, closure->p);

  return b;
}


void CBSIterateLarge(CBS cbs, CBSIterateMethod iterate, void *closureP)
{
  CBSIterateLargeClosureStruct closure;

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  AVER(FUNCHECK(iterate));

  closure.p = closureP;
  closure.f = iterate;
  cbsIterateInternal(cbs, &cbsIterateLargeAction, (void *)&closure);

  CBSLeave(cbs);
  return;
}


/* CBSSetMinSize -- Set minimum interesting size for cbs
 *
 * This function may invoke the shrink and grow methods as
 * appropriate.  See design.mps.cbs.function.cbs.set.min-size.
 */

typedef struct {
  Size old;
  Size new;
} CBSSetMinSizeClosureStruct, *CBSSetMinSizeClosure;

static Bool cbsSetMinSizeGrow(CBS cbs, CBSBlock block, void *p)
{
  CBSSetMinSizeClosure closure;
  Size size;

  closure = (CBSSetMinSizeClosure)p;
  AVER(closure->old > closure->new);
  size = CBSBlockSize(block);
  if (size < closure->old && size >= closure->new)
    (*cbs->new)(cbs, block, size, size);

  return TRUE;
}

static Bool cbsSetMinSizeShrink(CBS cbs, CBSBlock block, void *p)
{
  CBSSetMinSizeClosure closure;
  Size size;

  closure = (CBSSetMinSizeClosure)p;
  AVER(closure->old < closure->new);
  size = CBSBlockSize(block);
  if (size >= closure->old && size < closure->new)
    (*cbs->delete)(cbs, block, size, size);

  return TRUE;
}

void CBSSetMinSize(CBS cbs, Size minSize)
{
  CBSSetMinSizeClosureStruct closure;

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  closure.old = cbs->minSize;
  closure.new = minSize;

  if (minSize < cbs->minSize)
    cbsIterateInternal(cbs, &cbsSetMinSizeGrow, (void *)&closure);
  else if (minSize > cbs->minSize)
    cbsIterateInternal(cbs, &cbsSetMinSizeShrink, (void *)&closure);

  cbs->minSize = minSize;

  CBSLeave(cbs);
}


/* CBSFindDeleteCheck -- check method for a CBSFindDelete value */

static Bool CBSFindDeleteCheck(CBSFindDelete findDelete) 
{
  CHECKL(findDelete == CBSFindDeleteNONE || findDelete == CBSFindDeleteLOW
         || findDelete == CBSFindDeleteHIGH
         || findDelete == CBSFindDeleteENTIRE);
  UNUSED(findDelete); /* impl.c.mpm.check.unused */

  return TRUE;
}


/* cbsFindDeleteRange -- delete approriate range of block found */

typedef Res (*cbsDeleteMethod)(CBS cbs, Addr base, Addr limit);

static void cbsFindDeleteRange(Addr *baseReturn, Addr *limitReturn, 
                               CBS cbs, Addr base, Addr limit, Size size, 
                               cbsDeleteMethod delete, 
                               CBSFindDelete findDelete)
{
  Bool callDelete = TRUE;

  AVER(baseReturn != NULL);
  AVER(limitReturn != NULL);
  AVER(base < limit);
  AVER(size > 0);
  AVER(AddrOffset(base, limit) >= size);
  AVER(FUNCHECK(delete));
  AVERT(CBSFindDelete, findDelete);

  switch(findDelete) {

  case CBSFindDeleteNONE: {
    callDelete = FALSE;
  } break;

  case CBSFindDeleteLOW: {
    limit = AddrAdd(base, size);
  } break;

  case CBSFindDeleteHIGH: {
    base = AddrSub(limit, size);
  } break;

  case CBSFindDeleteENTIRE: {
    /* do nothing */
  } break;

  default: {
    NOTREACHED;
  } break;
  }

  if (callDelete) {
    Res res;
    res = (*delete)(cbs, base, limit);
    AVER(res == ResOK);
  }

  *baseReturn = base;
  *limitReturn = limit;
}


/* CBSFindFirst -- find the first block of at least the given size
 *
 * Look in the tree first, then check if there's an earlier one in the lists.
 * Finally, remove the earliest block found from its tree/list.
 */

Bool CBSFindFirst(Addr *baseReturn, Addr *limitReturn,
                  CBS cbs, Size size, CBSFindDelete findDelete) 
{
  Bool found = FALSE;
  Addr base = (Addr)0, limit = (Addr)0; /* only defined when found is TRUE */
  CBSBlock cbsBlock = NULL;
  CBSEmergencyBlock emBlock = NULL, prevEmBlock = NULL;
  CBSEmergencyGrain grain = NULL;
  CBSFindDelete deleteKind = findDelete;

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  AVER(baseReturn != NULL);
  AVER(limitReturn != NULL);
  AVER(size > 0);
  AVER(sizeof(void*) >= sizeof(Size));
  AVER(SizeIsAligned(size, cbs->alignment));
  AVER(cbs->fastFind);
  AVERT(CBSFindDelete, findDelete);

  /* Look in the tree */
  {
    SplayNode node;

    METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
    found = SplayFindFirst(&node, splayTreeOfCBS(cbs), &cbsTestNode,
                           &cbsTestTree, (void*)size);
    if (found) {
      cbsBlock = cbsBlockOfSplayNode(node);
      base = CBSBlockBase(cbsBlock); limit = CBSBlockLimit(cbsBlock);
      if (CBSBlockSize(cbsBlock) == size && findDelete != CBSFindDeleteNONE)
        deleteKind = CBSFindDeleteENTIRE;
    }
  }

  /* Look in the emergency block list */
  if (cbs->emergencyBlockList != NULL) {
    METER_ACC(cbs->eblSearch, cbs->eblSize);
    for(emBlock = cbs->emergencyBlockList, prevEmBlock = NULL;
        emBlock != NULL && (!found || CBSEmergencyBlockBase(emBlock) < base);
        prevEmBlock = emBlock, emBlock = CBSEmergencyBlockNext(emBlock)) {
      Size blockSize = CBSEmergencyBlockSize(emBlock);

      if (blockSize >= size) {
        found = TRUE;
        cbsBlock = NULL; /* reset so that emBlock is preferred to it */
        base = CBSEmergencyBlockBase(emBlock);
        limit = CBSEmergencyBlockLimit(emBlock);
        if (blockSize == size && findDelete != CBSFindDeleteNONE)
          deleteKind = CBSFindDeleteENTIRE;
        else /* override method selected for tree block */
          deleteKind = findDelete;
        break;
      }
    }
  }

  /* Look in the emergency grain list */
  if (cbs->emergencyGrainList != NULL
      && size <= CBSEmergencyGrainSize) {
    /* Take first grain */
    grain = cbs->emergencyGrainList;
    if (!found || CBSEmergencyGrainBase(grain) < base) {
      found = TRUE;
      base = CBSEmergencyGrainBase(grain);
      limit = CBSEmergencyGrainLimit(cbs, grain);
    } else
      grain = NULL;
    /* Now, grain is non-NULL, iff it was chosen. */
  }

  if (found) {
    AVER(AddrOffset(base, limit) >= size);
    if (deleteKind != CBSFindDeleteNONE) {
      if (grain != NULL) {
        /* remove the grain from the list */
        cbs->emergencyGrainList = CBSEmergencyGrainNext(grain);
        STATISTIC(--cbs->eglSize);
      } else if (cbsBlock != NULL) {
        /* chop off result from the CBS block */
        switch (deleteKind) {
        case CBSFindDeleteLOW: {
          limit = AddrAdd(base, size);
          cbsBlockShrinkHigh(cbs, cbsBlock, limit);
        } break;
        case CBSFindDeleteHIGH: {
          base = AddrSub(limit, size);
          cbsBlockShrinkLow(cbs, cbsBlock, base);
        } break;
        case CBSFindDeleteENTIRE: {
          cbsBlockDelete(cbs, cbsBlock);
        } break;
        }
      } else { /* must have found emergency block, chop off result from it */
        AVER(emBlock != NULL);
        switch (deleteKind) {
        case CBSFindDeleteLOW: {
          CBSEmergencyBlock blockNext;
          Addr blockLimit;

          limit = AddrAdd(base, size);
          /* New block base = limit of result */
          blockNext = CBSEmergencyBlockNext(emBlock);
          blockLimit = CBSEmergencyBlockLimit(emBlock);
          if (AddrOffset(limit, blockLimit) == CBSEmergencyGrainSize) {
            /* Too small, unlink from this list, put on grain list. */
            Res res;

            if (prevEmBlock == NULL)
              cbs->emergencyBlockList = blockNext;
            else
              CBSEmergencyBlockSetNext(prevEmBlock, blockNext);
            STATISTIC(--cbs->eblSize);
            res = cbsAddToEmergencyLists(cbs, limit, blockLimit);
            AVER(res == ResOK); /* since it was already in the other list */
          } else {
            /* Keep the rest in the list, update the link to the new base,
             * and restore next and limit at the new base. */
            emBlock = (CBSEmergencyBlock)limit;
            if (prevEmBlock == NULL)
              cbs->emergencyBlockList = emBlock;
            else
              CBSEmergencyBlockSetNext(prevEmBlock, emBlock);
            CBSEmergencyBlockSetNext(emBlock, blockNext);
            CBSEmergencyBlockSetLimit(emBlock, blockLimit);
            AVER(CBSEmergencyBlockCheck(emBlock));
          }
        } break;
        case CBSFindDeleteHIGH: {
          base = AddrSub(limit, size);
          if (AddrOffset((Addr)emBlock, base) == CBSEmergencyGrainSize) {
            /* Too small, unlink from this list, put on grain list. */
            CBSEmergencyBlock blockNext;
            Res res;

            blockNext = CBSEmergencyBlockNext(emBlock);
            if (prevEmBlock == NULL)
              cbs->emergencyBlockList = blockNext;
            else
              CBSEmergencyBlockSetNext(prevEmBlock, blockNext);
            STATISTIC(--cbs->eblSize);
            res = cbsAddToEmergencyLists(cbs, (Addr)emBlock, base);
            AVER(res == ResOK); /* since it was already in the other list */
          } else {
            CBSEmergencyBlockSetLimit(emBlock, (void*)base);
            AVER(CBSEmergencyBlockCheck(emBlock));
          }
        } break;
        case CBSFindDeleteENTIRE: {
          if (prevEmBlock == NULL)
            cbs->emergencyBlockList = CBSEmergencyBlockNext(emBlock);
          else
            CBSEmergencyBlockSetNext(prevEmBlock, CBSEmergencyBlockNext(emBlock));
          STATISTIC(--cbs->eblSize);
        } break;
        }
      }
    }
    cbsFlushEmergencyLists(cbs); /* do this only after allocation */
    *baseReturn = base; *limitReturn = limit;
  }
  CBSLeave(cbs);
  return found;
}


/* CBSFindLast -- find the last block of at least the given size */

Bool CBSFindLast(Addr *baseReturn, Addr *limitReturn,
                 CBS cbs, Size size, CBSFindDelete findDelete) 
{
  Bool found;
  Addr base = (Addr)0, limit = (Addr)0; /* only defined in found is TRUE */
  cbsDeleteMethod deleteMethod = NULL;

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  AVER(baseReturn != NULL);
  AVER(limitReturn != NULL);
  AVER(size > 0);
  AVER(sizeof(void*) >= sizeof(Size));
  AVER(SizeIsAligned(size, cbs->alignment));
  AVER(cbs->fastFind);
  AVERT(CBSFindDelete, findDelete);

  {
    SplayNode node;

    METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
    found = SplayFindLast(&node, splayTreeOfCBS(cbs), &cbsTestNode,
                          &cbsTestTree, (void*)size);
    if (found) {
      CBSBlock block;

      block = cbsBlockOfSplayNode(node);
      AVER(CBSBlockSize(block) >= size);
      base = CBSBlockBase(block);
      limit = CBSBlockLimit(block);
      deleteMethod = &cbsDeleteFromTree;
    }
  }

  if (cbs->emergencyBlockList != NULL) {
    CBSEmergencyBlock block;

    METER_ACC(cbs->eblSearch, cbs->eblSize);
    for(block = cbs->emergencyBlockList;
        block != NULL;
        block = CBSEmergencyBlockNext(block)) {
      if (CBSEmergencyBlockSize(block) >= size
          && (!found || CBSEmergencyBlockBase(block) > base)) {
        found = TRUE;
        base = CBSEmergencyBlockBase(block);
        limit = CBSEmergencyBlockLimit(block);
        deleteMethod = &cbsDeleteFromEmergencyBlockList;
        /* @@@@ Could remove in place more efficiently. */
      }
    }
  }

  if (cbs->emergencyGrainList != NULL && size <= CBSEmergencyGrainSize) {
    CBSEmergencyGrain grain;

    /* Find last grain */
    METER_ACC(cbs->eglSearch, cbs->eglSize);
    for(grain = cbs->emergencyGrainList;
        CBSEmergencyGrainNext(grain) != NULL;
        grain = CBSEmergencyGrainNext(grain))
      NOOP;

    if (!found || CBSEmergencyGrainBase(grain) > base) {
      found = TRUE;
      base = CBSEmergencyGrainBase(grain);
      limit = CBSEmergencyGrainLimit(cbs, grain);
      deleteMethod = &cbsDeleteFromEmergencyGrainList;
      /* @@@@ Could remove in place more efficiently */
    }
  }

  if (found) {
    AVER(AddrOffset(base, limit) >= size);
    cbsFindDeleteRange(baseReturn, limitReturn, cbs, base, limit, size,
                       deleteMethod, findDelete);
    cbsFlushEmergencyLists(cbs); /* do this only after allocation */
  }

  CBSLeave(cbs);
  return found;
}


/* CBSFindLargest -- find the largest block in the CBS */

Bool CBSFindLargest(Addr *baseReturn, Addr *limitReturn,
                    CBS cbs, CBSFindDelete findDelete)
{
  Bool found = FALSE;
  Addr base = (Addr)0, limit = (Addr)0; /* only defined when found is TRUE */
  cbsDeleteMethod deleteMethod = NULL;
  Size size = 0; /* suppress bogus warning from MSVC */

  AVERT(CBS, cbs);
  CBSEnter(cbs);

  AVER(baseReturn != NULL);
  AVER(limitReturn != NULL);
  AVER(sizeof(void*) >= sizeof(Size));
  AVER(cbs->fastFind);
  AVERT(CBSFindDelete, findDelete);

  {
    SplayNode root;
    Bool notEmpty;

    notEmpty = SplayRoot(&root, splayTreeOfCBS(cbs));
    if (notEmpty) {
      CBSBlock block;
      SplayNode node;

      size = cbsBlockOfSplayNode(root)->maxSize;
      METER_ACC(cbs->splaySearch, cbs->splayTreeSize);
      found = SplayFindFirst(&node, splayTreeOfCBS(cbs), &cbsTestNode,
                             &cbsTestTree, (void*)size);
      AVER(found); /* maxSize is exact, so we will find it. */
      block = cbsBlockOfSplayNode(node);
      AVER(CBSBlockSize(block) >= size);
      base = CBSBlockBase(block);
      limit = CBSBlockLimit(block);
      deleteMethod = &cbsDeleteFromTree;
    }
  }

  if (cbs->emergencyBlockList != NULL) {
    CBSEmergencyBlock block;

    /* Scan the whole list -- could maintain a maxSize to avoid it. */
    METER_ACC(cbs->eblSearch, cbs->eblSize);
    for(block = cbs->emergencyBlockList;
        block != NULL;
        block = CBSEmergencyBlockNext(block)) {
      if (CBSEmergencyBlockSize(block) >= size) {
        /* .pref: >= so that it prefers the emerg. list to the tree */
        found = TRUE;
        size = CBSEmergencyBlockSize(block);
        base = CBSEmergencyBlockBase(block);
        limit = CBSEmergencyBlockLimit(block);
        deleteMethod = &cbsDeleteFromEmergencyBlockList;
        /* @@@@ Could remove in place more efficiently. */
      }
    }
  }

  /* If something was found, it will be larger than an emerg. grain. */
  if (!found && cbs->emergencyGrainList != NULL) {
    /* Take first grain */
    CBSEmergencyGrain grain = cbs->emergencyGrainList;

    found = TRUE;
    size = CBSEmergencyGrainSize;
    base = CBSEmergencyGrainBase(grain);
    limit = CBSEmergencyGrainLimit(cbs, grain);
    deleteMethod = &cbsDeleteFromEmergencyGrainList;
  }

  if (found) {
    cbsFindDeleteRange(baseReturn, limitReturn, cbs, base, limit, size,
                       deleteMethod, findDelete);
    cbsFlushEmergencyLists(cbs); /* do this only after allocation */
  }

  CBSLeave(cbs);
  return found;
}


/* CBSDescribe -- describe a CBS
 *
 * See design.mps.cbs.function.cbs.describe.
 */

Res CBSDescribe(CBS cbs, mps_lib_FILE *stream)
{
  Res res;

  if (!CHECKT(CBS, cbs)) return ResFAIL;
  if (stream == NULL) return ResFAIL;

  res = WriteF(stream,
               "CBS $P {\n", (WriteFP)cbs,
               "  blockPool: $P\n", (WriteFP)cbs->blockPool,
               "  new: $F ", (WriteFF)cbs->new,
               "  delete: $F \n", (WriteFF)cbs->delete,
               NULL);
  if (res != ResOK) return res;

  res = SplayTreeDescribe(splayTreeOfCBS(cbs), stream, &CBSSplayNodeDescribe);
  if (res != ResOK) return res;

  res = WriteF(stream, "size of splay tree ");
  if (res != ResOK) return res;
  res = METER_WRITE(cbs->splaySearch, stream);
  if (res != ResOK) return res;
  res = WriteF(stream, "size of emergencyBlockList ");
  if (res != ResOK) return res;
  res = METER_WRITE(cbs->eblSearch, stream);
  if (res != ResOK) return res;
  res = WriteF(stream, "size of emergencyGrainList ");
  if (res != ResOK) return res;
  res = METER_WRITE(cbs->eglSearch, stream);
  if (res != ResOK) return res;

  res = WriteF(stream, "}\n", NULL);
  return res;
}
