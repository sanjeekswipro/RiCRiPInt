/* impl.c.arenash: ARENA CLASS USING SHARED MEMORY
 *
 * $Id: arenash.c,v 1.12.1.1.1.1 2013/12/19 11:27:10 anon Exp $
 * $HopeName: MMsrc!arenash.c(EBDSDK_P.1) $
 * Copyright (c) 2004 Ravenbrook Limited.
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * .design: See design.mps.arena.shared.  Implementation based on
 * impl.c.arenacl, except chunks work like impl.c.arenavm w/o mapping.
 *
 * .improve.remember: One possible performance improvement is to
 * remember (a conservative approximation to) the indices of the first
 * and last free pages in each chunk, and start searching from these
 * in ChunkAlloc.  See request.epcore.170534.  */

#include "boot.h"
#include "tract.h"
#include "shared.h"
#include "bt.h"
#include "mpm.h"
#include "mpsash.h"

SRCID(arenash, "$Id: arenash.c,v 1.12.1.1.1.1 2013/12/19 11:27:10 anon Exp $");


#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))


/* SharedArenaStruct -- Shared-arena structure */

#define SharedArenaSig ((Sig)0x519A6E58) /* SIGnature AREna SHared */

typedef struct SharedArenaStruct {
  ArenaStruct arenaStruct; /* generic arena structure */
  SharedSeg shSeg;         /* segment where the arena itself is stored */
  Size extendBy;           /* size for next extension chunk */
  Size reserveLimit;       /* limit on reserved address space */
  Sig sig;                 /* design.mps.sig */
} SharedArenaStruct;
typedef struct SharedArenaStruct *SharedArena;

#define Arena2SharedArena(arena) PARENT(SharedArenaStruct, arenaStruct, arena)
#define SharedArena2Arena(shArena) (&(shArena)->arenaStruct)


static Size SharedArenaReserved(Arena arena);


/* sharedChunk -- chunk structure */

typedef struct SharedChunkStruct *SharedChunk;

#define SharedChunkSig ((Sig)0x519A658C) /* SIGnature ARena SHared Chunk */

typedef struct SharedChunkStruct {
  ChunkStruct chunkStruct;     /* generic chunk */
  SharedSeg shSeg;             /* shared memory segment handle */
  Size freePages;              /* number of free pages in chunk */
  Sig sig;                     /* design.mps.sig */
} SharedChunkStruct;

#define SharedChunk2Chunk(shChunk) (&(shChunk)->chunkStruct)
#define Chunk2SharedChunk(chunk) PARENT(SharedChunkStruct, chunkStruct, chunk)


/* SharedChunkSharedArena -- get the shared arena from a chunk */

#define SharedChunkSharedArena(shChunk) \
  Arena2SharedArena(ChunkArena(SharedChunk2Chunk(shChunk)))


/* SharedChunkCheck -- check the consistency of a SharedChunk */

static Bool SharedChunkCheck(SharedChunk shChunk)
{
  Chunk chunk;

  CHECKS(SharedChunk, shChunk);
  chunk = SharedChunk2Chunk(shChunk);
  CHECKL(ChunkCheck(chunk));
  CHECKL(SharedSegCheck(shChunk->shSeg));
  CHECKL(shChunk->freePages <= chunk->pages);
  /* check they don't overlap (knowing the order) */
  CHECKL((Addr)(chunk + 1) < (Addr)chunk->allocTable);
  return TRUE;
}


/* SharedArenaCheck -- check the consistency of a shared arena */

static Bool SharedArenaCheck(SharedArena shArena)
{
  Arena arena = SharedArena2Arena(shArena);

  UNUSED(arena); /* not used in non-checking varieties */
  CHECKS(SharedArena, shArena);
  CHECKD(Arena, arena);
  CHECKL(shArena->extendBy > 0);
  CHECKL(shArena->reserveLimit > 0);
  CHECKL(shArena->reserveLimit >= SharedArenaReserved(arena)
         /* .reservelimit.overflow: The arena can breach the reserve
          * limit by a little because of overheads in the shared segment
          * module.  Pick 144 kB as the design limit. */
         || SharedArenaReserved(arena) - shArena->reserveLimit <= 144*1024);
  return TRUE;
}


/* sharedChunkCreate -- create a SharedChunk
 *
 * This is much like vmChunkCreate in impl.c.arenavm.
 */

static Res sharedChunkCreate(Chunk *chunkReturn, SharedArena sharedArena,
                             Size size, Size minSize)
{
  SharedChunk shChunk;
  Addr base, limit;
  SharedSeg shSeg;
  BootBlockStruct bootStruct;
  BootBlock boot = &bootStruct;
  Res res;
  void *p;
  Size trySize = size;
  Size pageSize;

  AVER(chunkReturn != NULL);
  AVERT(SharedArena, sharedArena);
  AVER(size >= minSize);
  AVER(minSize > 0);

  pageSize = SharedAlign();
  /* Make sure we don't try ridiculously small sizes. */
  minSize = MAX(minSize, pageSize);

  /* Try reserving a shared segment of the required size; if that doesn't
   * work, try smaller sizes down to minSize. */
  do {
    res = SharedSegCreate(&shSeg, trySize);
    trySize = trySize / 4 * 3;
  } while (res == ResRESOURCE && trySize > minSize);
  if (res == ResRESOURCE)
    res = SharedSegCreate(&shSeg, minSize);
  if (res != ResOK)
    goto failSegCreate;

  /* The Create will have aligned the size; pick up the actual size. */
  base = SharedSegBase(shSeg);
  limit = SharedSegLimit(shSeg);

  res = BootBlockInit(boot, (void *)base, (void *)limit);
  if (res != ResOK)
    goto failBootInit;

  /* Allocate and map the descriptor. */
  res = BootAlloc(&p, boot, sizeof(SharedChunkStruct), MPS_PF_ALIGN);
  if (res != ResOK)
    goto failChunkAlloc;
  shChunk = p;

  shChunk->shSeg = shSeg;
  res = ChunkInit(SharedChunk2Chunk(shChunk), SharedArena2Arena(sharedArena),
                  base, limit, pageSize, boot);
  if (res != ResOK)
    goto failChunkInit;

  BootBlockFinish(boot);

  shChunk->sig = SharedChunkSig;
  AVERT(SharedChunk, shChunk);
  *chunkReturn = (Chunk)shChunk;
  return ResOK;

failChunkInit:
failChunkAlloc:
failBootInit:
  SharedSegDestroy(shSeg);
failSegCreate:
  return res;
}


/* SharedChunkInit -- initialize a SharedChunk */

static Res SharedChunkInit(Chunk chunk, BootBlock boot)
{
  SharedChunk shChunk;
  Size overheadSize;

  /* chunk is supposed to be uninitialized, so don't check it. */
  shChunk = Chunk2SharedChunk(chunk);
  AVERT(BootBlock, boot);

  overheadSize = SizeAlignUp((Size)BootAllocated(boot), ChunkPageSize(chunk));
  shChunk->freePages = chunk->pages - ChunkSizeToPages(chunk, overheadSize)
                       /* page table not allocated yet, so count separately */
                       - chunk->pageTablePages;
  return ResOK;
}


/* sharedChunkDestroy -- destroy a SharedChunk */

static void sharedChunkDestroy(Chunk chunk)
{
  SharedChunk shChunk;
  SharedSeg shSeg;

  shChunk = Chunk2SharedChunk(chunk);
  AVERT(SharedChunk, shChunk);

  shChunk->sig = SigInvalid;
  shSeg = shChunk->shSeg;
  ChunkFinish(chunk);
  SharedSegDestroy(shSeg);
}


/* SharedChunkFinish -- finish a SharedChunk */

static void SharedChunkFinish(Chunk chunk)
{
  /* Can't check chunk as it's not valid anymore. */
  /* Nothing needs finishing since shSeg must wait until sharedChunkDestroy. */
  UNUSED(chunk); NOOP;
}


/* SharedArenaInit -- create and initialize the shared arena
 *
 * .arena.init: Once the arena has been allocated, we call ArenaInit
 * to do the generic part of init.
 */
static Res SharedArenaInit(Arena *arenaReturn, ArenaClass class,
                           va_list args)
{
  Arena arena;
  SharedArena sharedArena;
  Size userSize;      /* size requested by user */
  Size chunkSize;     /* size actually created */
  Res res;
  SharedSeg arenaSeg;
  Chunk chunk;
  Count nChunks;

  userSize = va_arg(args, Size);
  AVER(arenaReturn != NULL);
  AVER((ArenaClass)mps_arena_class_sh() == class);
  AVER(userSize > 0);

  /* Create a segment to hold the arena. */
  res = SharedSegCreate(&arenaSeg, sizeof(SharedArenaStruct));
  if (res != ResOK)
    goto failSegCreate;
  sharedArena = (SharedArena)SharedSegBase(arenaSeg);

  arena = SharedArena2Arena(sharedArena);
  /* impl.c.arena.init.caller */
  res = ArenaInit(arena, class);
  if (res != ResOK)
    goto failArenaInit;
  arena->committed = AddrOffset(SharedSegBase(arenaSeg),
                                SharedSegLimit(arenaSeg));
  EVENT_PW(CommitSet, arena, arena->committed);
  arena->committedMax = arena->committed;

  sharedArena->shSeg = arenaSeg;
  sharedArena->reserveLimit = userSize;

  /* Have to have a valid arena before calling ChunkCreate. */
  sharedArena->extendBy = 1;
  sharedArena->sig = SharedArenaSig;

  res = sharedChunkCreate(&chunk, sharedArena, userSize, 1);
  if (res != ResOK)
    goto failChunkCreate;
  arena->primary = chunk;

  chunkSize = AddrOffset(chunk->base, chunk->limit);
  /* Set the zone shift to divide the initial chunk into the same */
  /* number of zones as will fit into a reference set (the number of */
  /* bits in a word).  Note that some zones are discontiguous in the */
  /* arena if the size is not a power of 2. */
  arena->zoneShift = SizeFloorLog2(chunkSize >> MPS_WORD_SHIFT);
  arena->alignment = ChunkPageSize(chunk);
  if (arena->alignment > ((Size)1 << arena->zoneShift)) {
    res = ResMEMORY; /* size was too small */
    goto failStripeSize;
  }
  /* design.mps.arena.vm.struct.vmarena */
  sharedArena->extendBy = chunkSize;

  /* Now allocate more chunks upto userSize. */
  nChunks = 1;
  while (SharedArenaReserved(arena) < sharedArena->reserveLimit
         && nChunks < sharedArenaChunkMAX) {
    Size allowed = sharedArena->reserveLimit - SharedArenaReserved(arena);
    res = sharedChunkCreate(&chunk, sharedArena,
                            MIN(sharedArena->extendBy, allowed), 1);
    if (res != ResOK) {
      AVER(ResIsAllocFailure(res) || res == ResLIMITATION);
      sharedArena->extendBy = 1;
      break;
    } else {
      ++nChunks;
      /* Next time, try a chunk of the same size. */
      sharedArena->extendBy = AddrOffset(chunk->base, chunk->limit);
    }
  }
  /* Make sure it doesn't extend. */
  sharedArena->reserveLimit = SharedArenaReserved(arena);

  AVERT(SharedArena, sharedArena);
  EVENT_PWW(ArenaCreateSH, arena, userSize, sharedArena->reserveLimit);
  *arenaReturn = arena;
  return ResOK;

failStripeSize:
  sharedChunkDestroy(chunk);
failChunkCreate:
  ArenaFinish(arena, FALSE);
failArenaInit:
  SharedSegDestroy(arenaSeg);
failSegCreate:
  return res;
}


/* SharedArenaFinish -- finish the arena */

static void SharedArenaFinish(Arena arena, Bool abort)
{
  SharedArena sharedArena;
  Ring node, next;
  SharedSeg arenaSeg;

  sharedArena = Arena2SharedArena(arena);
  AVERT(SharedArena, sharedArena);
  arenaSeg = sharedArena->shSeg;

  /* destroy all chunks */
  RING_FOR(node, &arena->chunkRing, next) {
    Chunk chunk = RING_ELT(Chunk, chunkRing, node);
    if (abort)
      BTResRange(chunk->allocTable, 0, chunk->pages);
    sharedChunkDestroy(chunk);
  }
  AVER(abort || arena->committed == AddrOffset(SharedSegBase(arenaSeg),
                                               SharedSegLimit(arenaSeg)));

  sharedArena->sig = SigInvalid;
  ArenaFinish(arena, abort); /* impl.c.arena.finish.caller */

  SharedSegDestroy(arenaSeg);
}


/* SharedArenaReserved -- return the amount of reserved address space */

Size SharedArenaReserved(Arena arena)
{
  Size size;
  Ring node, nextNode;

  AVERT(Arena, arena);

  size = 0;
  /* .req.extend.slow */
  RING_FOR(node, &arena->chunkRing, nextNode) {
    Chunk chunk = RING_ELT(Chunk, chunkRing, node);
    size += AddrOffset(chunk->base, chunk->limit);
  }

  return size;
}


/* sharedChunkAlloc -- allocate some tracts in a chunk */

static Res sharedChunkAlloc(Addr *baseReturn, Tract *baseTractReturn,
                            Chunk chunk, SegPref pref, Size pages, Pool pool)
{
  Index baseIndex, limitIndex, index;
  Bool b;
  Arena arena;
  SharedChunk shChunk;

  AVER(baseReturn != NULL);
  AVER(baseTractReturn != NULL);
  shChunk = Chunk2SharedChunk(chunk);

  if (pages > shChunk->freePages)
    return ResRESOURCE;

  arena = chunk->arena;

  if (pref->high)
    b = BTFindShortResRangeHigh(&baseIndex, &limitIndex, chunk->allocTable,
				chunk->allocBase, chunk->pages, pages);
  else
    b = BTFindShortResRange(&baseIndex, &limitIndex, chunk->allocTable,
			    chunk->allocBase, chunk->pages, pages);

  if (!b)
    return ResRESOURCE;

  /* Initialize the generic tract structures. */
  AVER(limitIndex > baseIndex);
  for(index = baseIndex; index < limitIndex; ++index)
    PageAlloc(chunk, index, pool);

  shChunk->freePages -= pages;
  arena->committed += ChunkPagesToSize(chunk, pages);
  EVENT_PW(CommitSet, arena, arena->committed);
  if (arena->committed > arena->committedMax)
    arena->committedMax = arena->committed;

  *baseReturn = PageIndexBase(chunk, baseIndex);
  *baseTractReturn = PageTract(&chunk->pageTable[baseIndex]);

  return ResOK;
}


/* SharedAlloc -- allocate a region from the arena */

static Res SharedAlloc(Addr *baseReturn, Tract *baseTractReturn,
                       SegPref pref, Size size, Pool pool)
{
  Arena arena;
  SharedArena sharedArena;
  Chunk chunk;
  Res res;
  Ring node, nextNode;
  Size pages, neededChunkSize, allowed;

  AVER(baseReturn != NULL);
  AVER(baseTractReturn != NULL);
  AVERT(SegPref, pref);
  AVER(size > 0);
  AVERT(Pool, pool);

  arena = PoolArena(pool);
  sharedArena = Arena2SharedArena(arena);
  AVERT(SharedArena, sharedArena);
  /* All chunks have same pageSize. */
  AVER(SizeIsAligned(size, ChunkPageSize(arena->primary)));
  /* NULL is used as a discriminator (see */
  /* design.mps.arenavm.table.disc), therefore the real pool */
  /* must be non-NULL. */
  AVER(pool != NULL);

  /* Early check on commit limit. */
  if (arena->committed + size > arena->commitLimit
      || arena->committed + size < arena->committed)
    return ResCOMMIT_LIMIT;

  pages = ChunkSizeToPages(arena->primary, size);

  /* .req.extend.slow */
  RING_FOR(node, &arena->chunkRing, nextNode) {
    chunk = RING_ELT(Chunk, chunkRing, node);
    res = sharedChunkAlloc(baseReturn, baseTractReturn,
                           chunk, pref, pages, pool);
    if (res == ResOK)
      return res;
  }
  /* try and extend */
  neededChunkSize = size + ChunkOverheadEstimate(arena, size);
  /* Check it's not already over the limit (see .reservelimit.overflow) */
  if (sharedArena->reserveLimit <= SharedArenaReserved(arena))
    return ResRESOURCE;
  allowed = sharedArena->reserveLimit - SharedArenaReserved(arena);
  if (allowed < neededChunkSize)
    return ResRESOURCE;
  res = sharedChunkCreate(&chunk, sharedArena,
                          MIN(MAX(sharedArena->extendBy, neededChunkSize),
                              allowed),
                          neededChunkSize);
  if (res != ResOK) {
    sharedArena->extendBy = MIN(sharedArena->extendBy, neededChunkSize);
    return res;
  } else {
    /* Next time, try a chunk of the same size. */
    sharedArena->extendBy = AddrOffset(chunk->base, chunk->limit);
  }
  /* Verify usable space is enough. */
  AVER(AddrOffset(PageIndexBase(chunk, chunk->allocBase), chunk->limit)
       >= size);
  /* try the allocation again */
  res = sharedChunkAlloc(baseReturn, baseTractReturn,
                         chunk, pref, pages, pool);
  return res;
}


/* SharedFree - free a region in the arena */

static void SharedFree(Addr base, Size size, Pool pool)
{
  Arena arena;
  Chunk chunk;
  Size pages;
  SharedArena sharedArena;
  Index pi, baseIndex, limitIndex;
  Bool foundChunk;
  SharedChunk shChunk;

  AVER(base != NULL);
  AVER(size > (Size)0);
  AVERT(Pool, pool);
  arena = PoolArena(pool);
  AVERT(Arena, arena);
  sharedArena = Arena2SharedArena(arena);
  AVERT(SharedArena, sharedArena);
  AVER(SizeIsAligned(size, ChunkPageSize(arena->primary)));
  AVER(AddrIsAligned(base, ChunkPageSize(arena->primary)));

  foundChunk = ChunkOfAddr(&chunk, arena, base);
  AVER(foundChunk);
  shChunk = Chunk2SharedChunk(chunk);
  AVERT(SharedChunk, shChunk);

  pages = ChunkSizeToPages(chunk, size);
  baseIndex = INDEX_OF_ADDR(chunk, base);
  limitIndex = baseIndex + pages;
  AVER(baseIndex < limitIndex);
  AVER(limitIndex <= chunk->pages);

  for(pi = baseIndex; pi < limitIndex; pi++) {
    Page page = &chunk->pageTable[pi];
    Tract tract = PageTract(page);

    AVER(TractPool(tract) == pool);
    TractFinish(tract);
  }

  AVER(BTIsSetRange(chunk->allocTable, baseIndex, limitIndex));
  BTResRange(chunk->allocTable, baseIndex, limitIndex);

  shChunk->freePages += pages;
  arena->committed -= size;
  EVENT_PW(CommitSet, arena, arena->committed);
  /* @@@@ free empty chunks */
}


/* SharedArenaClass  -- The Shared arena class definition */

DEFINE_ARENA_CLASS(SharedArenaClass, this)
{
  INHERIT_CLASS(this, AbstractArenaClass);
  this->name = "SH";
  this->size = sizeof(SharedArenaStruct);
  this->offset = offsetof(SharedArenaStruct, arenaStruct);
  this->init = SharedArenaInit;
  this->finish = SharedArenaFinish;
  this->reserved = SharedArenaReserved;
  this->extend = ArenaNoExtend;
  this->alloc = SharedAlloc;
  this->free = SharedFree;
  this->chunkInit = SharedChunkInit;
  this->chunkFinish = SharedChunkFinish;
}


/* mps_arena_class_sh -- return the arena class SH */

mps_arena_class_t MPS_CALL mps_arena_class_sh(void)
{
  return (mps_arena_class_t)EnsureSharedArenaClass();
}


/* mps_sh_arena_slave_init -- initialize a slave arena */

mps_res_t MPS_CALL mps_sh_arena_slave_init(mps_sh_arena_details_s *details)
{
  Res res;

  res = SharedSegSlaveInit(details);
  if (res == ResOK)
    /* Make sure this process didn't inherit any other MPS state. */
    GlobalsStaticReset();
  return res;
}


/* mps_sh_arena_slave_finish -- finish a slave arena */

void MPS_CALL mps_sh_arena_slave_finish(mps_sh_arena_details_s *details)
{
  SharedSegSlaveFinish(details);
}
