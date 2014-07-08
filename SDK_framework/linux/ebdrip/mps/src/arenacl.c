/* impl.c.arenacl: ARENA CLASS USING CLIENT MEMORY
 *
 * $Id: arenacl.c,v 1.37.1.1.1.1 2013/12/19 11:27:10 anon Exp $
 * $HopeName: MMsrc!arenacl.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * .design: See design.mps.arena.client.
 *
 * .improve.remember: One possible performance improvement is to
 * remember (a conservative approximation to) the indices of the first
 * and last free pages in each chunk, and start searching from these
 * in ChunkAlloc.  See request.epcore.170534.
 */

#include "boot.h"
#include "tract.h"
#include "bt.h"
#include "mpm.h"
#include "mpsacl.h"

SRCID(arenacl, "$Id: arenacl.c,v 1.37.1.1.1.1 2013/12/19 11:27:10 anon Exp $");


/* ClientArenaStruct -- Client Arena Structure */

#define ClientArenaSig ((Sig)0x519A6EC7) /* SIGnature AREna CLient */

typedef struct ClientArenaStruct {
  ArenaStruct arenaStruct; /* generic arena structure */
  Sig sig;                 /* design.mps.sig */
} ClientArenaStruct;
typedef struct ClientArenaStruct *ClientArena;

#define Arena2ClientArena(arena) PARENT(ClientArenaStruct, arenaStruct, arena)
#define ClientArena2Arena(clArena) (&(clArena)->arenaStruct)


/* CLChunk -- chunk structure */

typedef struct ClientChunkStruct *ClientChunk;

#define ClientChunkSig ((Sig)0x519A6C2C) /* SIGnature ARena CLient Chunk */

typedef struct ClientChunkStruct {
  ChunkStruct chunkStruct;     /* generic chunk */
  Size freePages;              /* number of free pages in chunk */
  Addr pageBase;               /* base of first managed page in chunk */
  Sig sig;                     /* design.mps.sig */
} ClientChunkStruct;

#define ClientChunk2Chunk(clchunk) (&(clchunk)->chunkStruct)
#define Chunk2ClientChunk(chunk) PARENT(ClientChunkStruct, chunkStruct, chunk)


/* ClientChunkClientArena -- get the client arena from a client chunk */

#define ClientChunkClientArena(clchunk) \
  Arena2ClientArena(ChunkArena(ClientChunk2Chunk(clchunk)))


/* ClientChunkCheck -- check the consistency of a client chunk */

static Bool ClientChunkCheck(ClientChunk clChunk)
{
  Chunk chunk;

  CHECKS(ClientChunk, clChunk);
  chunk = ClientChunk2Chunk(clChunk);
  CHECKL(ChunkCheck(chunk));
  CHECKL(clChunk->freePages <= chunk->pages);
  /* check they don't overlap (knowing the order) */
  CHECKL((Addr)(chunk + 1) < (Addr)chunk->allocTable);
  return TRUE;
}


/* ClientArenaCheck -- check the consistency of a client arena */

static Bool ClientArenaCheck(ClientArena clientArena)
{
  CHECKS(ClientArena, clientArena);
  CHECKD(Arena, ClientArena2Arena(clientArena));
  return TRUE;
}


/* clientChunkCreate -- create a ClientChunk */

static Res clientChunkCreate(Chunk *chunkReturn, Addr base, Addr limit,
                             ClientArena clientArena)
{
  ClientChunk clChunk;
  Chunk chunk;
  Addr alignedBase;
  BootBlockStruct bootStruct;
  BootBlock boot = &bootStruct;
  Res res;
  void *p;
  ArenaStruct* arena;

  AVER(chunkReturn != NULL);
  AVER(base != (Addr)0);
  /* @@@@ Should refuse on small chunks, instead of AVERring. */
  AVER(limit != (Addr)0);
  AVER(limit > base);

  /* Initialize boot block. */
  /* Chunk has to be page-aligned, and the boot allocs must be within it. */
  alignedBase = AddrAlignUp(base, ARENA_CLIENT_PAGE_SIZE);
  AVER(alignedBase < limit);
  res = BootBlockInit(boot, (void *)alignedBase, (void *)limit);
  if (res != ResOK)
    goto failBootInit;

  /* Allocate the chunk. */
  /* See design.mps.arena.@@@@ */
  res = BootAlloc(&p, boot, sizeof(ClientChunkStruct), MPS_PF_ALIGN);
  if (res != ResOK)
    goto failChunkAlloc;
  clChunk = p;  chunk = ClientChunk2Chunk(clChunk);

  res = ChunkInit(chunk, ClientArena2Arena(clientArena),
                  alignedBase, AddrAlignDown(limit, ARENA_CLIENT_PAGE_SIZE),
                  ARENA_CLIENT_PAGE_SIZE, boot);
  if (res != ResOK)
    goto failChunkInit;

  arena = ClientArena2Arena(clientArena);
  arena->committed += AddrOffset(base, PageIndexBase(chunk, chunk->allocBase));
  EVENT_PW(CommitSet, arena, arena->committed);
  if (arena->committed > arena->committedMax)
    arena->committedMax = arena->committed;

  BootBlockFinish(boot);

  clChunk->sig = ClientChunkSig;
  AVERT(ClientChunk, clChunk);
  *chunkReturn = chunk;
  return ResOK;

failChunkInit:
failChunkAlloc:
failBootInit:
  return res;
}


/* ClientChunkInit -- initialize a ClientChunk */

static Res ClientChunkInit(Chunk chunk, BootBlock boot)
{
  ClientChunk clChunk;

  /* chunk is supposed to be uninitialized, so don't check it. */
  clChunk = Chunk2ClientChunk(chunk);
  AVERT(BootBlock, boot);
  UNUSED(boot);

  clChunk->freePages = chunk->pages; /* too large @@@@ */

  return ResOK;
}


/* clientChunkDestroy -- destroy a ClientChunk */

static void clientChunkDestroy(Chunk chunk)
{
  ClientChunk clChunk;

  clChunk = Chunk2ClientChunk(chunk);
  AVERT(ClientChunk, clChunk);

  clChunk->sig = SigInvalid;
  ChunkFinish(chunk);
}


/* ClientChunkFinish -- finish a ClientChunk */

static void ClientChunkFinish(Chunk chunk)
{
  /* Can't check chunk as it's not valid anymore. */
  UNUSED(chunk); NOOP;
}


/* ClientArenaInit -- create and initialize the client arena
 *
 * .init.memory: Creates the arena structure in the chuck given, and
 * makes the first chunk from the memory left over.
 * .arena.init: Once the arena has been allocated, we call ArenaInit
 * to do the generic part of init.
 */
static Res ClientArenaInit(Arena *arenaReturn, ArenaClass class,
                           va_list args)
{
  Arena arena;
  ClientArena clientArena;
  Size size;
  Size clArenaSize;   /* aligned size of ClientArenaStruct */
  Addr base, limit, chunkBase;
  Res res;
  Chunk chunk;

  size = va_arg(args, Size);
  base = va_arg(args, Addr);
  AVER(arenaReturn != NULL);
  AVER((ArenaClass)mps_arena_class_cl() == class);
  AVER(base != (Addr)0);

  clArenaSize = SizeAlignUp(sizeof(ClientArenaStruct), MPS_PF_ALIGN);
  if (size < clArenaSize)
    return ResMEMORY;

  limit = AddrAdd(base, size);

  /* allocate the arena */
  base = AddrAlignUp(base, MPS_PF_ALIGN);
  clientArena = (ClientArena)base;
  chunkBase = AddrAlignUp(AddrAdd(base, clArenaSize), MPS_PF_ALIGN);
  if (chunkBase > limit)
    return ResMEMORY;

  arena = ClientArena2Arena(clientArena);
  /* impl.c.arena.init.caller */
  res = ArenaInit(arena, class);
  if (res != ResOK)
    return res;

  /* have to have a valid arena before calling ChunkCreate */
  clientArena->sig = ClientArenaSig;

  res = clientChunkCreate(&chunk, chunkBase, limit, clientArena);
  if (res != ResOK)
    goto failChunkCreate;
  arena->primary = chunk;

  /* Set the zone shift to divide the initial chunk into the same */
  /* number of zones as will fit into a reference set (the number of */
  /* bits in a word). Note that some zones are discontiguous in the */
  /* arena if the size is not a power of 2. */
  arena->zoneShift = SizeFloorLog2(size >> MPS_WORD_SHIFT);
  arena->alignment = ChunkPageSize(chunk);
  if (arena->alignment > ((Size)1 << arena->zoneShift)) {
    res = ResMEMORY; /* size was too small */
    goto failStripeSize;
  }

  EVENT_PWA(ArenaCreateCL, arena, size, base);
  AVERT(ClientArena, clientArena);
  *arenaReturn = arena;
  return ResOK;

failStripeSize:
  clientChunkDestroy(chunk);
failChunkCreate:
  ArenaFinish(arena, FALSE);
  return res;
}


/* ClientArenaFinish -- finish the arena */

static void ClientArenaFinish(Arena arena, Bool abort)
{
  ClientArena clientArena;
  Ring node, next;

  clientArena = Arena2ClientArena(arena);
  AVERT(ClientArena, clientArena);

  /* destroy all chunks */
  RING_FOR(node, &arena->chunkRing, next) {
    Chunk chunk = RING_ELT(Chunk, chunkRing, node);
    if (abort)
      BTResRange(chunk->allocTable, 0, chunk->pages);
    clientChunkDestroy(chunk);
  }

  clientArena->sig = SigInvalid;

  ArenaFinish(arena, abort); /* impl.c.arena.finish.caller */
}


/* ClientArenaExtend -- extend the arena */

static Res ClientArenaExtend(Arena arena, Addr base, Size size)
{
  ClientArena clientArena;
  Chunk chunk;
  Res res;
  Addr limit;

  AVERT(Arena, arena);
  AVER(base != (Addr)0);
  AVER(size > 0);
  limit = AddrAdd(base, size);

  clientArena = Arena2ClientArena(arena);
  res = clientChunkCreate(&chunk, base, limit, clientArena);
  return res;
}


/* ClientArenaReserved -- return the amount of reserved address space */

static Size ClientArenaReserved(Arena arena)
{
  Size size;
  Ring node, nextNode;

  AVERT(Arena, arena);

  size = 0;
  /* .req.extend.slow */
  RING_FOR(node, &arena->chunkRing, nextNode) {
    Chunk chunk = RING_ELT(Chunk, chunkRing, node);
    AVERT(Chunk, chunk);
    size += AddrOffset(chunk->base, chunk->limit);
  }

  return size;
}


/* chunkAlloc -- allocate some tracts in a chunk */

static Res chunkAlloc(Addr *baseReturn, Tract *baseTractReturn,
                      SegPref pref, Size pages, Pool pool, Chunk chunk)
{
  Index baseIndex, limitIndex, index;
  Bool b;
  Arena arena;
  ClientChunk clChunk;

  AVER(baseReturn != NULL);
  AVER(baseTractReturn != NULL);
  clChunk = Chunk2ClientChunk(chunk);

  if (pages > clChunk->freePages)
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
  for(index = baseIndex; index < limitIndex; ++index) {
    PageAlloc(chunk, index, pool);
  }

  clChunk->freePages -= pages;
  arena->committed += ChunkPagesToSize(chunk, pages);
  EVENT_PW(CommitSet, arena, arena->committed);
  if (arena->committed > arena->committedMax)
    arena->committedMax = arena->committed;

  *baseReturn = PageIndexBase(chunk, baseIndex);
  *baseTractReturn = PageTract(&chunk->pageTable[baseIndex]);

  return ResOK;
}


/* ClientAlloc -- allocate a region from the arena */

static Res ClientAlloc(Addr *baseReturn, Tract *baseTractReturn,
                       SegPref pref, Size size, Pool pool)
{
  Arena arena;
  Res res;
  Ring node, nextNode;
  Size pages, committed;

  AVER(baseReturn != NULL);
  AVER(baseTractReturn != NULL);
  AVERT(SegPref, pref);
  AVER(size > 0);
  AVERT(Pool, pool);

  arena = PoolArena(pool);
  AVERT(Arena, arena);
  /* All chunks have same pageSize. */
  AVER(SizeIsAligned(size, ChunkPageSize(arena->primary)));
  /* NULL is used as a discriminator (see */
  /* design.mps.arenavm.table.disc), therefore the real pool */
  /* must be non-NULL. */
  AVER(pool != NULL);

  /* Early check on commit limit. */
  committed = arena->committed;
  if (committed + size > arena->commitLimit || committed + size < committed)
    return ResCOMMIT_LIMIT;

  pages = ChunkSizeToPages(arena->primary, size);
  /* .req.extend.slow */
  RING_FOR(node, &arena->chunkRing, nextNode) {
    Chunk chunk = RING_ELT(Chunk, chunkRing, node);
    res = chunkAlloc(baseReturn, baseTractReturn, pref, pages, pool, chunk);
    if (res == ResOK || res == ResCOMMIT_LIMIT) {
      return res;
    }
  }
  return ResRESOURCE;
}


/* ClientFree - free a region in the arena */

static void ClientFree(Addr base, Size size, Pool pool)
{
  Arena arena;
  Chunk chunk;
  Size pages;
  ClientArena clientArena;
  Index pi, baseIndex, limitIndex;
  Bool foundChunk;
  ClientChunk clChunk;

  AVER(base != NULL);
  AVER(size > (Size)0);
  AVERT(Pool, pool);
  arena = PoolArena(pool);
  AVERT(Arena, arena);
  clientArena = Arena2ClientArena(arena);
  AVERT(ClientArena, clientArena);
  AVER(SizeIsAligned(size, ChunkPageSize(arena->primary)));
  AVER(AddrIsAligned(base, ChunkPageSize(arena->primary)));

  foundChunk = ChunkOfAddr(&chunk, arena, base);
  AVER(foundChunk);
  clChunk = Chunk2ClientChunk(chunk);
  AVERT(ClientChunk, clChunk);

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

  clChunk->freePages += pages;
  arena->committed -= size;
  EVENT_PW(CommitSet, arena, arena->committed);
}


/* ClientArenaClass  -- The Client arena class definition */

DEFINE_ARENA_CLASS(ClientArenaClass, this)
{
  INHERIT_CLASS(this, AbstractArenaClass);
  this->name = "CL";
  this->size = sizeof(ClientArenaStruct);
  this->offset = offsetof(ClientArenaStruct, arenaStruct);
  this->init = ClientArenaInit;
  this->finish = ClientArenaFinish;
  this->reserved = ClientArenaReserved;
  this->extend = ClientArenaExtend;
  this->alloc = ClientAlloc;
  this->free = ClientFree;
  this->chunkInit = ClientChunkInit;
  this->chunkFinish = ClientChunkFinish;
}


/* mps_arena_class_cl -- return the arena class CL */

mps_arena_class_t MPS_CALL mps_arena_class_cl(void)
{
  return (mps_arena_class_t)EnsureClientArenaClass();
}
