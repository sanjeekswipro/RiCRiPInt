/* impl.c.sac: SEGREGATED ALLOCATION CACHES
 *
 * $Id: sac.c,v 1.10.1.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!sac.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 */

#include "mpm.h"
#include "sac.h"

SRCID(sac, "$Id: sac.c,v 1.10.1.1.1.1 2013/12/19 11:27:08 anon Exp $");


#define MAX(a, b) (((a) > (b)) ? (a) : (b))


/* SACCheck -- check function for SACs */

static Bool sacFreeListBlockCheck(SACFreeListBlock fb)
{
  Count j;
  Addr cb;

  /* nothing to check about size */
  CHECKL(fb->count <= fb->countMax);
  /* check the freelist has the right number of blocks */
  for (j = 0, cb = fb->blocks; j < fb->count; ++j) {
    CHECKL(cb != NULL);
    /* @@@@ ignoring shields for now */
    cb = *ADDR_PTR(Addr, cb);
  }
  CHECKL(cb == NULL);
  return TRUE;
}

static Bool SACCheck(SAC sac)
{
  Index i, j;
  Bool b;
  Size prevSize;

  CHECKS(SAC, sac);
  CHECKU(Pool, sac->pool);
  CHECKL(sac->classesCount > 0);
  CHECKL(sac->classesCount > sac->middleIndex);
  CHECKL(BoolCheck(sac->esacStruct.trapped));
  CHECKL(sac->esacStruct.middle > 0);
  /* check classes above middle */
  prevSize = sac->esacStruct.middle;
  for (j = sac->middleIndex + 1, i = 0;
       j <= sac->classesCount; ++j, i += 2) {
    CHECKL(prevSize < sac->esacStruct.freelists[i].size);
    b = sacFreeListBlockCheck(&(sac->esacStruct.freelists[i]));
    if (!b) return b;
    prevSize = sac->esacStruct.freelists[i].size;
  }
  /* check overlarge class */
  CHECKL(sac->esacStruct.freelists[i-2].size == SizeMAX);
  CHECKL(sac->esacStruct.freelists[i-2].count == 0);
  CHECKL(sac->esacStruct.freelists[i-2].countMax == 0);
  CHECKL(sac->esacStruct.freelists[i-2].blocks == NULL);
  /* check classes below middle */
  prevSize = sac->esacStruct.middle;
  for (j = sac->middleIndex, i = 1; j > 0; --j, i += 2) {
    CHECKL(prevSize > sac->esacStruct.freelists[i].size);
    b = sacFreeListBlockCheck(&(sac->esacStruct.freelists[i]));
    if (!b) return b;
    prevSize = sac->esacStruct.freelists[i].size;
  }
  /* check smallest class */
  CHECKL(sac->esacStruct.freelists[i].size == 0);
  b = sacFreeListBlockCheck(&(sac->esacStruct.freelists[i]));
  return b;
}


/* sacSize -- calculate size of a SAC structure */

static Size sacSize(Index middleIndex, Count classesCount)
{
  Index indexMax; /* max index for the freelist */
  SACStruct dummy;

  if (middleIndex + 1 < classesCount - middleIndex)
    indexMax = 2 * (classesCount - middleIndex - 1);
  else
    indexMax = 1 + 2 * middleIndex;
  return PointerOffset(&dummy, &dummy.esacStruct.freelists[indexMax+1]);
}


/* SACCreate -- create an SAC object */

Res SACCreate(SAC *sacReturn, Pool pool, Count unsortedCount,
              SACClasses unsortedClasses)
{
  void *p;
  SAC sac;
  Res res;
  Index i, j;
  Size classesCount = 0;
  SACClassesStruct classes[sacClassLIMIT];
  Index middleIndex;  /* index of the size in the middle */
  unsigned totalFreq = 0;

  AVER(sacReturn != NULL);
  AVERT(Pool, pool);
  AVER(unsortedCount > 0 && unsortedCount <= sacClassLIMIT);
  for (i = 0; i < unsortedCount; ++i) {
    AVER(unsortedClasses[i].blockSize > 0);
    /* no restrictions on count */
    /* no restrictions on frequency */
  }

  /* Insertion sort for classes into sorted array, combined with merging
     duplicate block sizes. We expect most arrays to be in order, or
     approximately in order, so insertion sort will likely be quickest. */
  for (i = 0 ; i < unsortedCount ; ++i) {
    Index curr, prev = classesCount;
    /* Make size large enough for freelist link and align. */
    Size realSize = SizeAlignUp(MAX(unsortedClasses[i].blockSize, sizeof(Addr)),
                                PoolAlignment(pool));

    while ((curr = prev) > 0) {
      --prev;
      if ( classes[prev].blockSize == realSize ) {
        classes[prev].cachedCount += unsortedClasses[i].cachedCount;
        classes[prev].frequency += unsortedClasses[i].frequency;
        goto merged;
      }
      if ( classes[prev].blockSize < realSize )
        break;
    }
    /* Make space by moving up larger classes. */
    for (j = classesCount++ ; j > curr ; --j)
      classes[j] = classes[j - 1];
    classes[curr].blockSize = realSize;
    classes[curr].cachedCount = unsortedClasses[i].cachedCount;
    classes[curr].frequency = unsortedClasses[i].frequency;
  merged: /* Nothing more to do if we merged this class. */
    NOOP;
  }

  /* Calculate frequency scale */
  for (i = 0; i < classesCount; ++i) {
    unsigned oldFreq = totalFreq;
    totalFreq += classes[i].frequency;
    AVER(oldFreq <= totalFreq); /* check for overflow */
    UNUSED(oldFreq); /* impl.c.mpm.check.unused */
  }

  /* Find middle one */
  totalFreq /= 2;
  for (i = 0; i < classesCount; ++i) {
    if (totalFreq < classes[i].frequency) break;
    totalFreq -= classes[i].frequency;
  }
  if (totalFreq <= classes[i].frequency / 2)
    middleIndex = i;
  else
    middleIndex = i + 1; /* there must exist another class at i+1 */

  /* Allocate SAC */
  res = ControlAlloc(&p, PoolArena(pool), sacSize(middleIndex, classesCount),
                     FALSE);
  if (res != ResOK)
    goto failSACAlloc;
  sac = p;

  /* Move classes in place */
  /* It's important this matches SACFind. */
  for (j = middleIndex + 1, i = 0; j < classesCount; ++j, i += 2) {
    sac->esacStruct.freelists[i].size = classes[j].blockSize;
    sac->esacStruct.freelists[i].count = 0;
    sac->esacStruct.freelists[i].countMax = classes[j].cachedCount;
    sac->esacStruct.freelists[i].blocks = NULL;
  }
  sac->esacStruct.freelists[i].size = SizeMAX;
  sac->esacStruct.freelists[i].count = 0;
  sac->esacStruct.freelists[i].countMax = 0;
  sac->esacStruct.freelists[i].blocks = NULL;
  for (j = middleIndex, i = 1; j > 0; --j, i += 2) {
    sac->esacStruct.freelists[i].size = classes[j-1].blockSize;
    sac->esacStruct.freelists[i].count = 0;
    sac->esacStruct.freelists[i].countMax = classes[j].cachedCount;
    sac->esacStruct.freelists[i].blocks = NULL;
  }
  sac->esacStruct.freelists[i].size = 0;
  sac->esacStruct.freelists[i].count = 0;
  sac->esacStruct.freelists[i].countMax = classes[j].cachedCount;
  sac->esacStruct.freelists[i].blocks = NULL;

  /* finish init */
  sac->esacStruct.trapped = FALSE;
  sac->esacStruct.middle = classes[middleIndex].blockSize;
  sac->pool = pool;
  sac->classesCount = classesCount;
  sac->middleIndex = middleIndex;
  sac->sig = SACSig;
  AVERT(SAC, sac);
  *sacReturn = sac;
  return ResOK;

failSACAlloc:
  return res;
}


/* SACDestroy -- destroy an SAC object */

void SACDestroy(SAC sac)
{
  AVERT(SAC, sac);
  SACFlush(sac);
  sac->sig = SigInvalid;
  ControlFree(PoolArena(sac->pool), sac,
              sacSize(sac->middleIndex, sac->classesCount));
}


/* sacFind -- find the index corresponding to size
 *
 * This function replicates the loop in MPS_SAC_ALLOC_FAST, only with
 * added checks.
 */

static void sacFind(Index *iReturn, Size *blockSizeReturn,
                    SAC sac, Size size)
{
  Index i, j;

  if (size > sac->esacStruct.middle) {
    i = 0; j = sac->middleIndex + 1;
    AVER(j <= sac->classesCount);
    while (size > sac->esacStruct.freelists[i].size) {
      AVER(j < sac->classesCount);
      i += 2; ++j;
    }
    *blockSizeReturn = sac->esacStruct.freelists[i].size;
  } else {
    Size prevSize = sac->esacStruct.middle;

    i = 1; j = sac->middleIndex;
    while (size <= sac->esacStruct.freelists[i].size) {
      AVER(j > 0);
      prevSize = sac->esacStruct.freelists[i].size;
      i += 2; --j;
    }
    *blockSizeReturn = prevSize;
  }
  *iReturn = i;
}


/* SACFill -- alloc an object, and perhaps fill the cache */

Res SACFill(Addr *p_o, SAC sac, Size size, Bool hasReservoirPermit)
{
  Index i;
  Count blockCount, j;
  Size blockSize;
  Addr p, fl;
  Res res = ResOK; /* stop compiler complaining */

  AVER(p_o != NULL);
  AVERT(SAC, sac);
  AVER(size != 0);
  AVER(BoolCheck(hasReservoirPermit));

  sacFind(&i, &blockSize, sac, size);
  /* Check it's empty (in the future, there will be other cases). */
  AVER(sac->esacStruct.freelists[i].count == 0);

  /* Fill 1/3 of the cache for this class. */
  blockCount = sac->esacStruct.freelists[i].countMax / 3;
  /* Adjust size for the overlarge class. */
  if (blockSize == SizeMAX)
    /* .align: align 'cause some classes don't accept unaligned. */
    blockSize = SizeAlignUp(size, PoolAlignment(sac->pool));
  for (j = 0, fl = sac->esacStruct.freelists[i].blocks;
       j <= blockCount; ++j) {
    res = PoolAlloc(&p, sac->pool, blockSize, hasReservoirPermit, NULL);
    if (res != ResOK)
      break;
    /* @@@@ ignoring shields for now */
    *ADDR_PTR(Addr, p) = fl; fl = p;
  }
  /* If didn't get any, just return. */
  if (j == 0) {
    AVER(res != ResOK);
    return res;
  }

  /* Take the last one off, and return it. */
  sac->esacStruct.freelists[i].count = j - 1;
  *p_o = fl;
  /* @@@@ ignoring shields for now */
  sac->esacStruct.freelists[i].blocks = *ADDR_PTR(Addr, fl);
  return ResOK;
}


/* sacClassFlush -- discard elements from the cache for a given class
 *
 * blockCount says how many elements to discard.
 */

static void sacClassFlush(SAC sac, Index i, Size blockSize,
                          Count blockCount)
{
  Addr cb, fl;
  Count j;

  for (j = 0, fl = sac->esacStruct.freelists[i].blocks;
       j < blockCount; ++j) {
    /* @@@@ ignoring shields for now */
    cb = fl; fl = *ADDR_PTR(Addr, cb);
    PoolFree(sac->pool, cb, blockSize);
  }
  sac->esacStruct.freelists[i].count -= blockCount;
  sac->esacStruct.freelists[i].blocks = fl;
}


/* SACEmpty -- free an object, and perhaps empty the cache */

void SACEmpty(SAC sac, Addr p, Size size)
{
  Index i;
  Size blockSize;

  AVERT(SAC, sac);
  AVER(p != NULL);
  AVER(PoolHasAddr(sac->pool, p));
  AVER(size > 0);

  sacFind(&i, &blockSize, sac, size);
  /* Check it's full (in the future, there will be other cases). */
  AVER(sac->esacStruct.freelists[i].count
       == sac->esacStruct.freelists[i].countMax);

  /* Adjust size for the overlarge class. */
  if (blockSize == SizeMAX)
    /* see .align */
    blockSize = SizeAlignUp(size, PoolAlignment(sac->pool));
  if (sac->esacStruct.freelists[i].countMax > 0) {
    Count blockCount;

    /* Flush 2/3 of the cache for this class. */
    /* Computed as count - count/3, so that the rounding works out right. */
    blockCount = sac->esacStruct.freelists[i].count;
    blockCount -= sac->esacStruct.freelists[i].count / 3;
    sacClassFlush(sac, i, blockSize, (blockCount > 0) ? blockCount : 1);
    /* Leave the current one in the cache. */
    sac->esacStruct.freelists[i].count += 1;
    /* @@@@ ignoring shields for now */
    *ADDR_PTR(Addr, p) = sac->esacStruct.freelists[i].blocks;
    sac->esacStruct.freelists[i].blocks = p;
  } else {
    /* Free even the current one. */
    PoolFree(sac->pool, p, blockSize);
  }
}


/* SACFlush -- flush the cache, releasing all memory held in it */

void SACFlush(SAC sac)
{
  Index i, j;
  Size prevSize;

  AVERT(SAC, sac);

  for (j = sac->middleIndex + 1, i = 0;
       j < sac->classesCount; ++j, i += 2) {
    sacClassFlush(sac, i, sac->esacStruct.freelists[i].size,
                  sac->esacStruct.freelists[i].count);
    AVER(sac->esacStruct.freelists[i].blocks == NULL);
  }
  /* no need to flush overlarge, there's nothing there */
  prevSize = sac->esacStruct.middle;
  for (j = sac->middleIndex, i = 1; j > 0; --j, i += 2) {
    sacClassFlush(sac, i, prevSize, sac->esacStruct.freelists[i].count);
    AVER(sac->esacStruct.freelists[i].blocks == NULL);
    prevSize = sac->esacStruct.freelists[i].size;
  }
  /* flush smallest class */
  sacClassFlush(sac, i, prevSize, sac->esacStruct.freelists[i].count);
  AVER(sac->esacStruct.freelists[i].blocks == NULL);
}


/* SACFreeSize -- return the total size of the blocks in the cache */

Size SACFreeSize(SAC sac)
{
  Index i, j;
  Size prevSize;
  Size freeSize = 0;

  AVERT(SAC, sac);

  /* count classes up from the middle */
  for (j = sac->middleIndex + 1, i = 0;
       j < sac->classesCount; ++j, i += 2) {
    freeSize += sac->esacStruct.freelists[i].size
                * sac->esacStruct.freelists[i].count;
  }
  /* no free blocks held for overlarge */
  /* count classes down from the middle */
  prevSize = sac->esacStruct.middle;
  for (j = sac->middleIndex, i = 1; j > 0; --j, i += 2) {
    freeSize += prevSize * sac->esacStruct.freelists[i].count;
  }
  /* count smallest class */
  freeSize += prevSize * sac->esacStruct.freelists[i].count;

  return freeSize;
}
