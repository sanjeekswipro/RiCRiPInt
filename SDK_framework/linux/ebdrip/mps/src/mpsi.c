/* impl.c.mpsi: MEMORY POOL SYSTEM C INTERFACE LAYER
 *
 * $Id: mpsi.c,v 1.102.1.1.1.1 2013/12/19 11:27:07 anon Exp $
 * $HopeName: MMsrc!mpsi.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 *
 * .purpose: This code bridges between the MPS interface to C,
 * impl.h.mps, and the internal MPM interfaces, as defined by
 * impl.h.mpm.  .purpose.check: It performs checking of the C client's
 * usage of the MPS Interface.  .purpose.thread: It excludes multiple
 * threads from the MPM by locking the Arena (see .thread-safety).
 *
 * .design: design.mps.interface.c
 *
 *
 * NOTES
 *
 * .note.break-out: Take care not to return when "inside" the Arena
 * (between ArenaEnter and ArenaLeave) as this will leave the Arena in
 * an unsuitable state for re-entry.
 *
 *
 * TRANSGRESSIONS (rule.impl.trans)
 *
 * .check.protocol: (rule.impl.req) More could be done in this code to
 * check that protocols are obeyed by the client.  It probably doesn't
 * meet checking requirements.
 *
 * .poll: (rule.universal.complete) Various allocation methods call
 * ArenaPoll to allow the MPM to "steal" CPU time and get on with
 * background tasks such as incremental GC.
 *
 * .root-mode: (rule.universal.complete) The root "mode", which
 * specifies things like the protectability of roots, is ignored at
 * present.  This is because the MPM doesn't ever try to protect them.
 * In future, it will.
 *
 * .reg-scan: (rule.universal.complete) At present, we only support
 * register scanning using our own ambiguous register and stack scanning
 * method, mps_stack_scan_ambig.  This may never change, but the way the
 * interface is designed allows for the possibility of change.
 *
 * .naming: (rule.impl.guide) The exported identifiers do not follow the
 * normal MPS naming conventions.  See design.mps.interface.c.naming.  */

#ifdef VALGRIND_BUILD
#include <stdlib.h>
#endif

#include "mpm.h"
#include "mps.h"
#include "sac.h"
#include "chain.h"
#include "dbgpool.h"
#include "walk.h"

SRCID(mpsi, "$Id: mpsi.c,v 1.102.1.1.1.1 2013/12/19 11:27:07 anon Exp $");


/* mpsi_check -- check consistency of interface mappings
 *
 * .check.purpose: The mpsi_check function attempts to check whether
 * the defintions in impl.h.mpsi match the equivalent definition in
 * the MPM.  It is checking the assumptions made in the other functions
 * in this implementation.
 *
 * .check.empty: Note that mpsi_check compiles away to almost nothing.
 *
 * .check.enum.cast: enum comparisons have to be cast to avoid a warning
 * from the SunPro C compiler.  See builder.sc.warn.enum.  */

static Bool mpsi_check(void)
{
  /* .check.rc: Check that external and internal result codes match. */
  /* See impl.h.mps.result-codes and impl.h.mpmtypes.result-codes. */
  /* Also see .check.enum.cast. */
  CHECKL(CHECKTYPE(mps_res_t, Res));
  CHECKL((int)MPS_RES_OK == (int)ResOK);
  CHECKL((int)MPS_RES_FAIL == (int)ResFAIL);
  CHECKL((int)MPS_RES_RESOURCE == (int)ResRESOURCE);
  CHECKL((int)MPS_RES_MEMORY == (int)ResMEMORY);
  CHECKL((int)MPS_RES_LIMITATION == (int)ResLIMITATION);
  CHECKL((int)MPS_RES_UNIMPL == (int)ResUNIMPL);
  CHECKL((int)MPS_RES_IO == (int)ResIO);
  CHECKL((int)MPS_RES_COMMIT_LIMIT == (int)ResCOMMIT_LIMIT);

  /* Check that external and internal rank numbers match. */
  /* See impl.h.mps.ranks and impl.h.mpmtypes.ranks. */
  /* Also see .check.enum.cast. */
  CHECKL(CHECKTYPE(mps_rank_t, Rank));
  CHECKL((int)MPS_RANK_AMBIG == (int)RankAMBIG);
  CHECKL((int)MPS_RANK_EXACT == (int)RankEXACT);
  CHECKL((int)MPS_RANK_WEAK == (int)RankWEAK);

  /* The external idea of a word width and the internal one */
  /* had better match.  See design.mps.interface.c.cons. */
  CHECKL(sizeof(mps_word_t) == sizeof(void *));
  CHECKL(CHECKTYPE(mps_word_t, Word));

  /* The external idea of an address and the internal one */
  /* had better match. */
  CHECKL(CHECKTYPE(mps_addr_t, Addr));

  /* The external idea of size and the internal one had */
  /* better match.  See design.mps.interface.c.cons.size */
  /* and design.mps.interface.c.pun.size. */
  CHECKL(CHECKTYPE(size_t, Size));

  /* Check ap_s/APStruct compatibility by hand */
  /* .check.ap: See impl.h.mps.ap and impl.h.buffer.ap. */
  CHECKL(sizeof(mps_ap_s) == sizeof(APStruct));
  CHECKL(CHECKFIELD(mps_ap_s, init,  APStruct, init));
  CHECKL(CHECKFIELD(mps_ap_s, alloc, APStruct, alloc));
  CHECKL(CHECKFIELD(mps_ap_s, limit, APStruct, limit));

  /* Check sac_s/ExternalSACStruct compatibility by hand */
  /* See impl.h.mps.sac and impl.h.sac.sac. */
  CHECKL(sizeof(mps_sac_s) == sizeof(ExternalSACStruct));
  CHECKL(CHECKFIELD(mps_sac_s, mps_middle, ExternalSACStruct, middle));
  CHECKL(CHECKFIELD(mps_sac_s, mps_trapped,
                    ExternalSACStruct, trapped));
  CHECKL(CHECKFIELDAPPROX(mps_sac_s, mps_freelists,
                          ExternalSACStruct, freelists));
  CHECKL(sizeof(mps_sac_freelist_block_s)
         == sizeof(SACFreeListBlockStruct));
  CHECKL(CHECKFIELD(mps_sac_freelist_block_s, mps_size,
                    SACFreeListBlockStruct, size));
  CHECKL(CHECKFIELD(mps_sac_freelist_block_s, mps_count,
                    SACFreeListBlockStruct, count));
  CHECKL(CHECKFIELD(mps_sac_freelist_block_s, mps_count_max,
                    SACFreeListBlockStruct, countMax));
  CHECKL(CHECKFIELD(mps_sac_freelist_block_s, mps_blocks,
                    SACFreeListBlockStruct, blocks));

  /* Check sac_classes_s/SACClassesStruct compatibility by hand */
  /* See impl.h.mps.sacc and impl.h.sac.sacc. */
  CHECKL(sizeof(mps_sac_classes_s) == sizeof(SACClassesStruct));
  CHECKL(CHECKFIELD(mps_sac_classes_s, mps_block_size,
                    SACClassesStruct, blockSize));
  CHECKL(CHECKFIELD(mps_sac_classes_s, mps_cached_count,
                    SACClassesStruct, cachedCount));
  CHECKL(CHECKFIELD(mps_sac_classes_s, mps_frequency,
                    SACClassesStruct, frequency));

  /* .check.ss: Check ss_s/ScanStateStruct compatibility by hand.  See
   * impl.h.mps.ss and impl.h.mpmst.ss.  Note that the sizes of mps_ss_s
   * and ScanStateStruct are not equal.  CHECKFIELDAPPROX is used on the
   * fix field because its type is punned and therefore isn't exactly
   * checkable.  See design.mps.interface.c.pun.addr. */
  CHECKL(CHECKFIELDAPPROX(mps_ss_s, fix, ScanStateStruct, fix));
  CHECKL(CHECKFIELD(mps_ss_s, w0, ScanStateStruct, zoneShift));
  CHECKL(CHECKFIELD(mps_ss_s, w1, ScanStateStruct, white));
  CHECKL(CHECKFIELD(mps_ss_s, w2, ScanStateStruct, unfixedSummary));
  CHECKL(CHECKFIELD(mps_ss_s, w3, ScanStateStruct, fixedSummary));

  /* Check ld_s/LDStruct compatibility by hand */
  /* .check.ld: See also impl.h.mpmst.ld.struct and impl.h.mps.ld */
  CHECKL(sizeof(mps_ld_s) == sizeof(LDStruct));
  CHECKL(CHECKFIELD(mps_ld_s, w0, LDStruct, epoch));
  CHECKL(CHECKFIELD(mps_ld_s, w1, LDStruct, rs));

  /* Check debug_info_s/DebugInfoStruct compatibility by hand */
  CHECKL(sizeof(mps_debug_info_s) == sizeof(DebugInfoStruct));
  CHECKL(CHECKFIELD(mps_debug_info_s, location, DebugInfoStruct, location));
  CHECKL(CHECKFIELD(mps_debug_info_s, mps_class, DebugInfoStruct, class));

  return TRUE;
}


/* Ranks
 *
 * Here a rank-returning function is defined for all client visible
 * ranks.
 *
 * .rank.final.not: RankFINAL does not have a corresponding function as
 * it is only used internally.  */

mps_rank_t MPS_CALL mps_rank_ambig(void)
{
  return RankAMBIG;
}

mps_rank_t MPS_CALL mps_rank_exact(void)
{
  return RankEXACT;
}

mps_rank_t MPS_CALL mps_rank_weak(void)
{
  return RankWEAK;
}


mps_res_t MPS_CALL mps_arena_extend(mps_arena_t mps_arena,
                           mps_addr_t base, size_t size)
{
  Arena arena = (Arena)mps_arena;
  Res res;

  ArenaEnter(arena);
  AVER(size > 0);
  res = ArenaExtend(arena, (Addr)base, (Size)size);
  ArenaLeave(arena);

  return (mps_res_t)res;
}

size_t MPS_CALL mps_arena_reserved(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);
  size = ArenaReserved(arena);
  ArenaLeave(arena);

  return (size_t)size;
}


size_t MPS_CALL mps_arena_committed(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);
  size = ArenaCommitted(arena);
  ArenaLeave(arena);

  return (size_t)size;
}

size_t MPS_CALL mps_arena_committed_max(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);
  size = ArenaCommittedMax(arena);
  ArenaLeave(arena);

  return (size_t)size;
}

size_t MPS_CALL mps_arena_spare_committed(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);
  size = ArenaSpareCommitted(arena);
  ArenaLeave(arena);

  return (size_t)size;
}

size_t MPS_CALL mps_arena_commit_limit(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);
  size = ArenaCommitLimit(arena);
  ArenaLeave(arena);

  return size;
}

mps_res_t MPS_CALL mps_arena_commit_limit_set(mps_arena_t mps_arena, size_t limit)
{
  Res res;
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);
  res = ArenaSetCommitLimit(arena, limit);
  ArenaLeave(arena);

  return res;
}

void MPS_CALL mps_arena_spare_commit_limit_set(mps_arena_t mps_arena, size_t limit)
{
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);
  ArenaSetSpareCommitLimit(arena, limit);
  ArenaLeave(arena);

  return;
}

size_t MPS_CALL mps_arena_spare_commit_limit(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  size_t limit;

  ArenaEnter(arena);
  limit = ArenaSpareCommitLimit(arena);
  ArenaLeave(arena);

  return limit;
}

void MPS_CALL mps_arena_clamp(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  ArenaEnter(arena);
  ArenaClamp(ArenaGlobals(arena));
  ArenaLeave(arena);
}


void MPS_CALL mps_arena_release(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  ArenaEnter(arena);
  ArenaRelease(ArenaGlobals(arena));
  ArenaLeave(arena);
}


void MPS_CALL mps_arena_park(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  ArenaEnter(arena);
  ArenaPark(ArenaGlobals(arena));
  ArenaLeave(arena);
}


mps_res_t MPS_CALL mps_arena_collect(mps_arena_t mps_arena)
{
  Res res;
  Arena arena = (Arena)mps_arena;
  ArenaEnter(arena);
  res = ArenaCollect(ArenaGlobals(arena));
  ArenaLeave(arena);
  return res;
}


/* mps_arena_create -- create an arena object */

mps_res_t mps_arena_create(mps_arena_t *mps_arena_o,
                           mps_arena_class_t mps_arena_class, ...)
{
  mps_res_t res;
  va_list args;

  va_start(args, mps_arena_class);
  res = mps_arena_create_v(mps_arena_o, mps_arena_class, args);
  va_end(args);
  return res;
}


/* mps_arena_create_v -- create an arena object */

mps_res_t MPS_CALL mps_arena_create_v(mps_arena_t *mps_arena_o,
                             mps_arena_class_t mps_arena_class, va_list args)
{
  Arena arena;
  Res res;

  /* This is the first real call that the client will have to make, */
  /* so check static consistency here. */
  AVER(mpsi_check());

  AVER(mps_arena_o != NULL);

  res = ArenaCreateV(&arena, (ArenaClass)mps_arena_class, args);
  if (res != ResOK)
    return res;

  ArenaLeave(arena);
  *mps_arena_o = (mps_arena_t)arena;
  return MPS_RES_OK;
}


/* mps_arena_destroy -- destroy an arena object */

void MPS_CALL mps_arena_destroy(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);
  ArenaDestroy(arena, FALSE);
}


/* mps_arena_abort -- destroy an arena object without checks */

void MPS_CALL mps_arena_abort(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);
  ArenaDestroy(arena, TRUE);
}


/* mps_arena_has_addr -- is this address managed by this arena? */

mps_bool_t MPS_CALL mps_arena_has_addr(mps_arena_t mps_arena, mps_addr_t p)
{
    Bool b;
    Arena arena = (Arena)mps_arena;

    ArenaEnter(arena);
    AVERT(Arena, arena);
    b = ArenaHasAddr(arena, (Addr)p);
    ArenaLeave(arena);
    return b;
}


/* mps_pool_has_addr -- is this address managed by this pool? */

mps_bool_t MPS_CALL mps_pool_has_addr(mps_pool_t mps_pool, mps_addr_t p)
{
  Bool b;
  Pool pool = (Pool)mps_pool;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  /* nothing to check about p */

  arena = PoolArena(pool);
  ArenaEnter(arena);

  AVERT(Pool, pool);

  b = PoolHasAddr(pool, (Addr)p);

  ArenaLeave(arena);
  return b;
}


/* mps_fmt_create_A -- create an object format of variant A
 *
 * .fmt.create.A.purpose: This function converts an object format spec
 * of variant "A" into an MPM Format object.  See
 * design.mps.interface.c.fmt.extend for justification of the way that
 * the format structure is declared as "mps_fmt_A".  */

mps_res_t MPS_CALL mps_fmt_create_A(mps_fmt_t *mps_fmt_o,
                           mps_arena_t mps_arena,
                           mps_fmt_A_s *mps_fmt_A)
{
  Arena arena = (Arena)mps_arena;
  Format format;
  Res res;

  ArenaEnter(arena);

  AVER(mps_fmt_A != NULL);

  res = FormatCreate(&format,
                     arena,
                     (Align)mps_fmt_A->align,
                     FormatVarietyA,
                     (FormatScanMethod)mps_fmt_A->scan,
                     (FormatSkipMethod)mps_fmt_A->skip,
                     (FormatMoveMethod)mps_fmt_A->fwd,
                     (FormatIsMovedMethod)mps_fmt_A->isfwd,
                     (FormatCopyMethod)mps_fmt_A->copy,
                     (FormatPadMethod)mps_fmt_A->pad,
                     NULL,
                     (Size)0);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_fmt_o = (mps_fmt_t)format;
  return MPS_RES_OK;
}


/* mps_fmt_create_B -- create an object format of variant B */

mps_res_t MPS_CALL mps_fmt_create_B(mps_fmt_t *mps_fmt_o,
                           mps_arena_t mps_arena,
                           mps_fmt_B_s *mps_fmt_B)
{
  Arena arena = (Arena)mps_arena;
  Format format;
  Res res;

  ArenaEnter(arena);

  AVER(mps_fmt_B != NULL);

  res = FormatCreate(&format,
                     arena,
                     (Align)mps_fmt_B->align,
                     FormatVarietyB,
                     (FormatScanMethod)mps_fmt_B->scan,
                     (FormatSkipMethod)mps_fmt_B->skip,
                     (FormatMoveMethod)mps_fmt_B->fwd,
                     (FormatIsMovedMethod)mps_fmt_B->isfwd,
                     (FormatCopyMethod)mps_fmt_B->copy,
                     (FormatPadMethod)mps_fmt_B->pad,
                     (FormatClassMethod)mps_fmt_B->mps_class,
                     (Size)0);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_fmt_o = (mps_fmt_t)format;
  return MPS_RES_OK;
}


/* mps_fmt_create_auto_header -- create a format of variant auto_header */

mps_res_t MPS_CALL mps_fmt_create_auto_header(mps_fmt_t *mps_fmt_o,
                                     mps_arena_t mps_arena,
                                     mps_fmt_auto_header_s *mps_fmt)
{
  Arena arena = (Arena)mps_arena;
  Format format;
  Res res;

  ArenaEnter(arena);

  AVER(mps_fmt != NULL);

  res = FormatCreate(&format,
                     arena,
                     (Align)mps_fmt->align,
                     FormatVarietyAutoHeader,
                     (FormatScanMethod)mps_fmt->scan,
                     (FormatSkipMethod)mps_fmt->skip,
                     (FormatMoveMethod)mps_fmt->fwd,
                     (FormatIsMovedMethod)mps_fmt->isfwd,
                     NULL,
                     (FormatPadMethod)mps_fmt->pad,
                     NULL,
                     (Size)mps_fmt->mps_headerSize);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_fmt_o = (mps_fmt_t)format;
  return MPS_RES_OK;
}


/* mps_fmt_create_fixed -- create an object format of variant fixed */

mps_res_t MPS_CALL mps_fmt_create_fixed(mps_fmt_t *mps_fmt_o,
                               mps_arena_t mps_arena,
                               mps_fmt_fixed_s *mps_fmt_fixed)
{
  Arena arena = (Arena)mps_arena;
  Format format;
  Res res;

  ArenaEnter(arena);

  AVER(mps_fmt_fixed != NULL);

  res = FormatCreate(&format,
                     arena,
                     (Align)mps_fmt_fixed->align,
                     FormatVarietyFixed,
                     (FormatScanMethod)mps_fmt_fixed->scan,
                     NULL,
                     (FormatMoveMethod)mps_fmt_fixed->fwd,
                     (FormatIsMovedMethod)mps_fmt_fixed->isfwd,
                     NULL,
                     (FormatPadMethod)mps_fmt_fixed->pad,
                     NULL,
                     (Size)0);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_fmt_o = (mps_fmt_t)format;
  return MPS_RES_OK;
}


/* mps_fmt_destroy -- destroy a format object */

void MPS_CALL mps_fmt_destroy(mps_fmt_t mps_fmt)
{
  Format format = (Format)mps_fmt;
  Arena arena;

  AVER(CHECKT(Format, format));
  arena = FormatArena(format);

  ArenaEnter(arena);

  FormatDestroy(format);

  ArenaLeave(arena);
}


mps_res_t mps_pool_create(mps_pool_t *mps_pool_o, mps_arena_t mps_arena,
                          mps_class_t mps_class, ...)
{
  mps_res_t res;
  va_list args;
  va_start(args, mps_class);
  res = mps_pool_create_v(mps_pool_o, mps_arena, mps_class, args);
  va_end(args);
  return res;
}

mps_res_t MPS_CALL mps_pool_create_v(mps_pool_t *mps_pool_o, mps_arena_t mps_arena,
                            mps_class_t mps_class, va_list args)
{
  Arena arena = (Arena)mps_arena;
  Pool pool;
  PoolClass class = (PoolClass)mps_class;
  Res res;

  ArenaEnter(arena);

  AVER(mps_pool_o != NULL);
  AVERT(Arena, arena);
  AVERT(PoolClass, class);

  res = PoolCreateV(&pool, arena, class, args);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_pool_o = (mps_pool_t)pool;
  return res;
}

void MPS_CALL mps_pool_destroy(mps_pool_t mps_pool)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  PoolDestroy(pool);

  ArenaLeave(arena);
}


#define allocWithGCRetry(res, arena, alloc) \
  BEGIN \
    (res) = (alloc); \
    if (ResIsAllocFailure(res)) { \
      Size avail = ArenaAvail(arena); \
      \
      /* force a GC poll, and see if that found any memory */ \
      ArenaPoll(ArenaGlobals(arena), TRUE); \
      if (ArenaAvail(arena) > avail) \
        (res) = (alloc); \
    } \
  END


mps_res_t MPS_CALL mps_alloc(mps_addr_t *p_o, mps_pool_t mps_pool, size_t size)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  Addr p;
  Res res;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  ArenaPoll(ArenaGlobals(arena), FALSE); /* .poll */

  AVER(p_o != NULL);
  AVERT(Pool, pool);
  AVER(size > 0);
  /* Note: class may allow unaligned size, see */
  /* design.mps.class-interface.alloc.size.align. */

#ifndef VALGRIND_BUILD
  allocWithGCRetry(res, arena, PoolAlloc(&p, pool, size, FALSE, NULL));
#else
  p = malloc(size);
  res = (p != NULL) ? MPS_RES_OK : MPS_RES_MEMORY;
#endif

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *p_o = (mps_addr_t)p;
  return MPS_RES_OK;
}


mps_res_t MPS_CALL mps_alloc_debug(mps_addr_t *p_o, mps_pool_t mps_pool, size_t size,
                          mps_debug_info_s *info)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  Addr p;
  Res res;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  ArenaPoll(ArenaGlobals(arena), FALSE); /* .poll */

  AVER(p_o != NULL);
  AVERT(Pool, pool);
  AVER(size > 0);
  /* Note: class may allow unaligned size, see */
  /* design.mps.class-interface.alloc.size.align. */

#ifndef VALGRIND_BUILD
  allocWithGCRetry(res, arena, PoolAlloc(&p, pool, size, FALSE,
                                         (DebugInfo)info));
#else
  p = malloc(size);
  res = (p != NULL) ? MPS_RES_OK : MPS_RES_MEMORY;
#endif

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *p_o = (mps_addr_t)p;
  return MPS_RES_OK;
}


mps_res_t MPS_CALL mps_alloc_debug2(mps_addr_t *p_o, mps_pool_t mps_pool, size_t size,
                           char *location, mps_word_t class)
{
  mps_debug_info_s info;

  info.location = mps_telemetry_intern(location); info.mps_class = class;
  return mps_alloc_debug(p_o, mps_pool, size, &info);
}


void MPS_CALL mps_free(mps_pool_t mps_pool, mps_addr_t p, size_t size)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  AVERT(Pool, pool);
#ifndef VALGRIND_BUILD
  AVER(PoolHasAddr(pool, p));
#endif
  AVER(size > 0);
  /* Note: class may allow unaligned size, see */
  /* design.mps.class-interface.alloc.size.align. */

#ifndef VALGRIND_BUILD
  PoolFree(pool, (Addr)p, size);
#else
  free(p);
#endif

  ArenaLeave(arena);
}


/* mps_pool_clear -- Clear all allocations in the pool. */

void mps_pool_clear(mps_pool_t mps_pool)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  AVERT(Pool, pool);

  PoolClear(pool);

  ArenaLeave(arena);
}


/* mps_pool_size -- return memory managed by the pool and memory free in that */

void MPS_CALL mps_pool_size(mps_pool_t mps_pool,
                            size_t *managed_size, size_t *free_size)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);
  AVER(managed_size != NULL);
  AVER(free_size != NULL);

  ArenaEnter(arena);

  AVERT(Pool, pool);

  *managed_size = PoolManagedSize(pool);
  *free_size = PoolFreeSize(pool);
  AVER(*managed_size >= *free_size);

  ArenaLeave(arena);
}


/* mps_ap_create -- create an allocation point */

mps_res_t MPS_CALL mps_ap_create(mps_ap_t *mps_ap_o, mps_pool_t mps_pool, ...)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  Buffer buf;
  BufferClass bufclass;
  Res res;
  va_list args;

  AVER(mps_ap_o != NULL);
  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  AVERT(Pool, pool);

  va_start(args, mps_pool);
  bufclass = PoolDefaultBufferClass(pool);
  res = BufferCreateV(&buf, bufclass, pool, TRUE, args);
  va_end(args);

  ArenaLeave(arena);

  if (res != ResOK)
    return res;
  *mps_ap_o = (mps_ap_t)BufferAP(buf);
  return MPS_RES_OK;
}


/* mps_ap_create_v -- create an allocation point, with varargs */

mps_res_t MPS_CALL mps_ap_create_v(mps_ap_t *mps_ap_o, mps_pool_t mps_pool,
                          va_list args)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  Buffer buf;
  BufferClass bufclass;
  Res res;

  AVER(mps_ap_o != NULL);
  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  AVERT(Pool, pool);

  bufclass = PoolDefaultBufferClass(pool);
  res = BufferCreateV(&buf, bufclass, pool, TRUE, args);

  ArenaLeave(arena);

  if (res != ResOK)
    return res;
  *mps_ap_o = (mps_ap_t)BufferAP(buf);
  return MPS_RES_OK;
}

void MPS_CALL mps_ap_destroy(mps_ap_t mps_ap)
{
  Buffer buf = BufferOfAP((AP)mps_ap);
  Arena arena;

  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, buf));
  arena = BufferArena(buf);

  ArenaEnter(arena);

  BufferDestroy(buf);

  ArenaLeave(arena);
}


/* mps_reserve -- allocate store in preparation for initialization
 *
 * .reserve.call: mps_reserve does not call BufferReserve, but instead
 * uses the in-line macro from impl.h.mps.  This is so that it calls
 * mps_ap_fill and thence ArenaPoll (.poll).  The consistency checks are
 * those which can be done outside the MPM.  See also .commit.call.  */

mps_res_t (MPS_CALL mps_reserve)(mps_addr_t *p_o, mps_ap_t mps_ap, size_t size)
{
  mps_res_t res;

  AVER(p_o != NULL);
  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, BufferOfAP((AP)mps_ap)));
  AVER(mps_ap->init == mps_ap->alloc);
  AVER(size > 0);

  MPS_RESERVE_BLOCK(res, *p_o, mps_ap, size);

  return res;
}



mps_res_t MPS_CALL mps_reserve_with_reservoir_permit(mps_addr_t *p_o,
                                            mps_ap_t mps_ap, size_t size)
{
  mps_res_t res;

  AVER(p_o != NULL);
  AVER(size > 0);
  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, BufferOfAP((AP)mps_ap)));
  AVER(mps_ap->init == mps_ap->alloc);

  MPS_RESERVE_WITH_RESERVOIR_PERMIT_BLOCK(res, *p_o, mps_ap, size);

  return res;
}



/* mps_commit -- commit initialized object, finishing allocation
 *
 * .commit.call: mps_commit does not call BufferCommit, but instead uses
 * the in-line commit macro from impl.h.mps.  This is so that it calls
 * mps_ap_trip and thence ArenaPoll in future (.poll).  The consistency
 * checks here are the ones which can be done outside the MPM.  See also
 * .reserve.call.  */

mps_bool_t (MPS_CALL mps_commit)(mps_ap_t mps_ap, mps_addr_t p, size_t size)
{
  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, BufferOfAP((AP)mps_ap)));
  AVER(p != NULL);
  AVER(size > 0);
  AVER(p == mps_ap->init);
  AVER((void *)((char *)mps_ap->init + size) == mps_ap->alloc);

  return mps_commit(mps_ap, p, size);
}


/* Allocation frame support
 *
 * These are candidates for being inlineable as macros.
 * These functions are easier to maintain, so we'll avoid
 * macros for now.  */


/* mps_ap_frame_push -- push a new allocation frame
 *
 * See design.mps.alloc-frame.lw-frame.push. */

mps_res_t MPS_CALL mps_ap_frame_push(mps_frame_t *frame_o, mps_ap_t mps_ap)
{
  AVER(frame_o != NULL);
  AVER(mps_ap != NULL);

  /* Fail if between reserve & commit */
  if ((char *)mps_ap->alloc != (char *)mps_ap->init) {
    return MPS_RES_FAIL;
  }

  if (!mps_ap->lwpoppending) {
    /* Valid state for a lightweight push */
    *frame_o = (mps_frame_t)mps_ap->init;
    return MPS_RES_OK;
  } else {
    /* Need a heavyweight push */
    Buffer buf = BufferOfAP((AP)mps_ap);
    Arena arena;
    AllocFrame frame;
    Res res;

    AVER(CHECKT(Buffer, buf));
    arena = BufferArena(buf);

    ArenaEnter(arena);
    AVERT(Buffer, buf);

    res = BufferFramePush(&frame, buf);

    if (res == ResOK) {
      *frame_o = (mps_frame_t)frame;
    }
    ArenaLeave(arena);
    return (mps_res_t)res;
  }
}

/* mps_ap_frame_pop -- push a new allocation frame
 *
 * See design.mps.alloc-frame.lw-frame.pop.  */

mps_res_t MPS_CALL mps_ap_frame_pop(mps_ap_t mps_ap, mps_frame_t frame)
{
  AVER(mps_ap != NULL);
  /* Can't check frame because it's an arbitrary value */

  /* Fail if between reserve & commit */
  if ((char *)mps_ap->alloc != (char *)mps_ap->init) {
    return MPS_RES_FAIL;
  }

  if (mps_ap->enabled) {
    /* Valid state for a lightweight pop */
    mps_ap->frameptr = (mps_addr_t)frame; /* record pending pop */
    mps_ap->lwpoppending = TRUE;
    mps_ap->limit = (mps_addr_t)0; /* trap the buffer */
    return MPS_RES_OK;

  } else {
    /* Need a heavyweight pop */
    Buffer buf = BufferOfAP((AP)mps_ap);
    Arena arena;
    Res res;

    AVER(CHECKT(Buffer, buf));
    arena = BufferArena(buf);

    ArenaEnter(arena);
    AVERT(Buffer, buf);

    res = BufferFramePop(buf, (AllocFrame)frame);

    ArenaLeave(arena);
    return (mps_res_t)res;
  }
}


/* mps_ap_fill -- called by mps_reserve when an AP hasn't enough arena
 *
 * .ap.fill.internal: Note that mps_ap_fill should never be "called"
 * directly by the client code.  It is invoked by the mps_reserve macro.  */

mps_res_t MPS_CALL mps_ap_fill(mps_addr_t *p_o, mps_ap_t mps_ap, size_t size)
{
  Buffer buf = BufferOfAP((AP)mps_ap);
  Arena arena;
  Addr p;
  Res res;

  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, buf));
  arena = BufferArena(buf);

  ArenaEnter(arena);

  ArenaPoll(ArenaGlobals(arena), FALSE); /* .poll */

  AVER(p_o != NULL);
  AVERT(Buffer, buf);
  AVER(size > 0);
  AVER(SizeIsAligned(size, BufferPool(buf)->alignment));

  allocWithGCRetry(res, arena, BufferFill(&p, buf, size, FALSE));

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *p_o = (mps_addr_t)p;
  return MPS_RES_OK;
}


mps_res_t MPS_CALL mps_ap_fill_with_reservoir_permit(mps_addr_t *p_o, mps_ap_t mps_ap,
                                            size_t size)
{
  Buffer buf = BufferOfAP((AP)mps_ap);
  Arena arena;
  Addr p;
  Res res;

  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, buf));
  arena = BufferArena(buf);

  ArenaEnter(arena);

  ArenaPoll(ArenaGlobals(arena), FALSE); /* .poll */

  AVER(p_o != NULL);
  AVERT(Buffer, buf);
  AVER(size > 0);
  AVER(SizeIsAligned(size, BufferPool(buf)->alignment));

  allocWithGCRetry(res, arena, BufferFill(&p, buf, size, TRUE));

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *p_o = (mps_addr_t)p;
  return MPS_RES_OK;
}


/* mps_ap_trip -- called by mps_commit when an AP is tripped
 *
 * .ap.trip.internal: Note that mps_ap_trip should never be "called"
 * directly by the client code.  It is invoked by the mps_commit macro.  */

mps_bool_t MPS_CALL mps_ap_trip(mps_ap_t mps_ap, mps_addr_t p, size_t size)
{
  Buffer buf = BufferOfAP((AP)mps_ap);
  Arena arena;
  Bool b;

  AVER(mps_ap != NULL);
  AVER(CHECKT(Buffer, buf));
  arena = BufferArena(buf);

  ArenaEnter(arena);

  AVERT(Buffer, buf);
  AVER(size > 0);
  AVER(SizeIsAligned(size, BufferPool(buf)->alignment));

  b = BufferTrip(buf, (Addr)p, size);

  ArenaLeave(arena);

  return b;
}


/* mps_sac_create -- create an SAC object */

mps_res_t MPS_CALL mps_sac_create(mps_sac_t *mps_sac_o, mps_pool_t mps_pool,
                         size_t classes_count, mps_sac_classes_s *mps_classes)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  SACClasses classes;
  SAC sac;
  Res res;

  AVER(mps_sac_o != NULL);
  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  classes = (SACClasses)mps_classes;
  res = SACCreate(&sac, pool, (Count)classes_count, classes);

  ArenaLeave(arena);

  if (res != ResOK) return (mps_res_t)res;
  *mps_sac_o = (mps_sac_t)ExternalSACOfSAC(sac);
  return (mps_res_t)res;
}


/* mps_sac_destroy -- destroy an SAC object */

void MPS_CALL mps_sac_destroy(mps_sac_t mps_sac)
{
  SAC sac = SACOfExternalSAC((ExternalSAC)mps_sac);
  Arena arena;

  AVER(CHECKT(SAC, sac));
  arena = SACArena(sac);

  ArenaEnter(arena);

  SACDestroy(sac);

  ArenaLeave(arena);
}


/* mps_sac_flush -- flush an SAC, releasing all memory held in it */

void MPS_CALL mps_sac_flush(mps_sac_t mps_sac)
{
  SAC sac = SACOfExternalSAC((ExternalSAC)mps_sac);
  Arena arena;

  AVER(CHECKT(SAC, sac));
  arena = SACArena(sac);

  ArenaEnter(arena);

  SACFlush(sac);

  ArenaLeave(arena);
}


/* mps_sac_fill -- alloc an object, and perhaps fill the cache */

mps_res_t MPS_CALL mps_sac_fill(mps_addr_t *p_o, mps_sac_t mps_sac, size_t size,
                       mps_bool_t has_reservoir_permit)
{
  SAC sac = SACOfExternalSAC((ExternalSAC)mps_sac);
  Arena arena;
  Addr p;
  Res res;

  AVER(p_o != NULL);
  AVER(CHECKT(SAC, sac));
  arena = SACArena(sac);

  ArenaEnter(arena);

  res = SACFill(&p, sac, size, (has_reservoir_permit != 0));

  ArenaLeave(arena);

  if (res != ResOK) return (mps_res_t)res;
  *p_o = (mps_addr_t)p;
  return (mps_res_t)res;
}


/* mps_sac_empty -- free an object, and perhaps empty the cache */

void MPS_CALL mps_sac_empty(mps_sac_t mps_sac, mps_addr_t p, size_t size)
{
  SAC sac = SACOfExternalSAC((ExternalSAC)mps_sac);
  Arena arena;

  AVER(CHECKT(SAC, sac));
  arena = SACArena(sac);

  ArenaEnter(arena);

  SACEmpty(sac, (Addr)p, (Size)size);

  ArenaLeave(arena);
}


/* mps_sac_alloc -- alloc an object, using cached space if possible */

mps_res_t MPS_CALL mps_sac_alloc(mps_addr_t *p_o, mps_sac_t mps_sac, size_t size,
                        mps_bool_t has_reservoir_permit)
{
  Res res;

  AVER(p_o != NULL);
  AVER(CHECKT(SAC, SACOfExternalSAC((ExternalSAC)mps_sac)));
  AVER(size > 0);

  MPS_SAC_ALLOC_FAST(res, *p_o, mps_sac, size, (has_reservoir_permit != 0));
  return res;
}


/* mps_sac_free -- free an object, to the cache if possible */

void MPS_CALL mps_sac_free(mps_sac_t mps_sac, mps_addr_t p, size_t size)
{
  AVER(CHECKT(SAC, SACOfExternalSAC((ExternalSAC)mps_sac)));
  /* Can't check p outside arena lock */
  AVER(size > 0);

  MPS_SAC_FREE_FAST(mps_sac, p, size);
}


/* mps_sac_free_size -- free an object, to the cache if possible */

size_t MPS_CALL mps_sac_free_size(mps_sac_t mps_sac)
{
  SAC sac = SACOfExternalSAC((ExternalSAC)mps_sac);
  Arena arena;
  Size size;

  AVER(CHECKT(SAC, sac));
  arena = SACArena(sac);

  ArenaEnter(arena);
  size = SACFreeSize(sac);
  ArenaLeave(arena);
  return (size_t)size;
}


/* Roots */


mps_res_t MPS_CALL mps_root_create(mps_root_t *mps_root_o, mps_arena_t mps_arena,
                          mps_rank_t mps_rank, mps_rm_t mps_rm,
                          mps_root_scan_t mps_root_scan, void *p, size_t s)
{
  Arena arena = (Arena)mps_arena;
  Rank rank = (Rank)mps_rank;
  Root root;
  Res res;

  ArenaEnter(arena);

  AVER(mps_root_o != NULL);
  AVER(mps_rm == (mps_rm_t)0);

  /* See .root-mode. */
  res = RootCreateFun(&root, arena, rank,
                      (RootScanMethod)mps_root_scan, p, s);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_root_o = (mps_root_t)root;
  return MPS_RES_OK;
}

mps_res_t MPS_CALL mps_root_create_table(mps_root_t *mps_root_o, mps_arena_t mps_arena,
                                mps_rank_t mps_rank, mps_rm_t mps_rm,
                                mps_addr_t *base, size_t size)
{
  Arena arena = (Arena)mps_arena;
  Rank rank = (Rank)mps_rank;
  Root root;
  RootMode mode = (RootMode)mps_rm;
  Res res;

  ArenaEnter(arena);

  AVER(mps_root_o != NULL);
  AVER(base != NULL);
  AVER(size > 0);

  /* .root.table-size: size is the length of the array at base, not */
  /* the size in bytes.  However, RootCreateTable expects base and */
  /* limit pointers.  Be careful. */

  res = RootCreateTable(&root, arena, rank, mode,
                        (Addr *)base, (Addr *)base + size);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_root_o = (mps_root_t)root;
  return MPS_RES_OK;
}

mps_res_t MPS_CALL mps_root_create_table_masked(mps_root_t *mps_root_o,
                                       mps_arena_t mps_arena,
                                       mps_rank_t mps_rank, mps_rm_t mps_rm,
                                       mps_addr_t *base, size_t size,
                                       mps_word_t mask)
{
  Arena arena = (Arena)mps_arena;
  Rank rank = (Rank)mps_rank;
  Root root;
  RootMode mode = (RootMode)mps_rm;
  Res res;

  ArenaEnter(arena);

  AVER(mps_root_o != NULL);
  AVER(base != NULL);
  AVER(size > 0);
  /* Can't check anything about mask */

  /* See .root.table-size. */

  res = RootCreateTableMasked(&root, arena, rank, mode,
                              (Addr *)base, (Addr *)base + size,
                              mask);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_root_o = (mps_root_t)root;
  return MPS_RES_OK;
}

mps_res_t MPS_CALL mps_root_create_fmt(mps_root_t *mps_root_o, mps_arena_t mps_arena,
                              mps_rank_t mps_rank, mps_rm_t mps_rm,
                              mps_fmt_scan_t mps_fmt_scan,
                              mps_addr_t base, mps_addr_t limit)
{
  Arena arena = (Arena)mps_arena;
  Rank rank = (Rank)mps_rank;
  FormatScanMethod scan = (FormatScanMethod)mps_fmt_scan;
  Root root;
  RootMode mode = (RootMode)mps_rm;
  Res res;

  ArenaEnter(arena);

  AVER(mps_root_o != NULL);

  res = RootCreateFmt(&root, arena, rank, mode, scan, (Addr)base, (Addr)limit);

  ArenaLeave(arena);
  if (res != ResOK) return res;
  *mps_root_o = (mps_root_t)root;
  return MPS_RES_OK;
}

mps_res_t MPS_CALL mps_root_create_reg(mps_root_t *mps_root_o, mps_arena_t mps_arena,
                              mps_rank_t mps_rank, mps_rm_t mps_rm,
                              mps_thr_t mps_thr, mps_reg_scan_t mps_reg_scan,
                              void *reg_scan_p, size_t mps_size)
{
  Arena arena = (Arena)mps_arena;
  Rank rank = (Rank)mps_rank;
  Thread thread = (Thread)mps_thr;
  Root root;
  Res res;

  ArenaEnter(arena);

  AVER(mps_root_o != NULL);
  AVER(mps_reg_scan != NULL);
  AVER(mps_reg_scan == mps_stack_scan_ambig); /* .reg.scan */
  AVER(reg_scan_p != NULL); /* stackBot */
  AVER(rank == MPS_RANK_AMBIG);
  AVER(mps_rm == (mps_rm_t)0);

  /* See .root-mode. */
  res = RootCreateReg(&root, arena, rank, thread,
                      (RootScanRegMethod)mps_reg_scan,
                      reg_scan_p, mps_size);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_root_o = (mps_root_t)root;
  return MPS_RES_OK;
}


/* mps_stack_scan_ambig -- scan the thread state ambiguously
 *
 * See .reg-scan.  */

mps_res_t MPS_CALL mps_stack_scan_ambig(mps_ss_t mps_ss,
                               mps_thr_t mps_thr, void *p, size_t s)
{
  ScanState ss = (ScanState)mps_ss;
  Thread thread = (Thread)mps_thr;

  UNUSED(s);
  return ThreadScan(ss, thread, p);
}


void MPS_CALL mps_root_destroy(mps_root_t mps_root)
{
  Root root = (Root)mps_root;
  Arena arena;

  arena = RootArena(root);

  ArenaEnter(arena);

  RootDestroy(root);

  ArenaLeave(arena);
}


void MPS_CALL mps_tramp(void **r_o,
                 void *(*f)(void *p, size_t s),
                 void *p, size_t s)
{
  AVER(r_o != NULL);
  AVER(FUNCHECK(f));
  /* Can't check p and s as they are interpreted by the client */

  ProtTramp(r_o, f, p, s);
}


mps_res_t MPS_CALL mps_thread_reg(mps_thr_t *mps_thr_o, mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Thread thread;
  Res res;

  ArenaEnter(arena);

  AVER(mps_thr_o != NULL);
  AVERT(Arena, arena);

  res = ThreadRegister(&thread, arena);

  ArenaLeave(arena);

  if (res != ResOK) return res;
  *mps_thr_o = (mps_thr_t)thread;
  return MPS_RES_OK;
}

void MPS_CALL mps_thread_dereg(mps_thr_t mps_thr)
{
  Thread thread = (Thread)mps_thr;
  Arena arena;

  AVER(ThreadCheckSimple(thread));
  arena = ThreadArena(thread);

  ArenaEnter(arena);

  ThreadDeregister(thread, arena);

  ArenaLeave(arena);
}

void MPS_CALL mps_ld_reset(mps_ld_t mps_ld, mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  LD ld = (LD)mps_ld;

  ArenaEnter(arena);

  LDReset(ld, arena);

  ArenaLeave(arena);
}


/* mps_ld_add -- add a reference to a location dependency
 *
 * See design.mps.interface.c.lock-free.  */

void MPS_CALL mps_ld_add(mps_ld_t mps_ld, mps_arena_t mps_arena, mps_addr_t addr)
{
  Arena arena = (Arena)mps_arena;
  LD ld = (LD)mps_ld;

  LDAdd(ld, arena, (Addr)addr);
}


/* mps_ld_merge -- merge two location dependencies
 *
 * See design.mps.interface.c.lock-free.  */

void MPS_CALL mps_ld_merge(mps_ld_t mps_ld, mps_arena_t mps_arena,
                  mps_ld_t mps_from)
{
  Arena arena = (Arena)mps_arena;
  LD ld = (LD)mps_ld;
  LD from = (LD)mps_from;

  LDMerge(ld, arena, from);
}


/* mps_ld_isstale -- check whether a location dependency is "stale"
 *
 * See design.mps.interface.c.lock-free.  */

mps_bool_t MPS_CALL mps_ld_isstale(mps_ld_t mps_ld, mps_arena_t mps_arena,
                          mps_addr_t addr)
{
  Arena arena = (Arena)mps_arena;
  LD ld = (LD)mps_ld;
  Bool b;

  b = LDIsStale(ld, arena, (Addr)addr);

  return (mps_bool_t)b;
}


/* mps_fix -- fix the reference in *ref_io */

mps_res_t MPS_CALL mps_fix(mps_ss_t mps_ss, mps_addr_t *ref_io)
{
  mps_res_t res;

  MPS_SCAN_BEGIN(mps_ss) {
    res = MPS_FIX(mps_ss, ref_io);
  } MPS_SCAN_END(mps_ss);
  return res;
}


/* mps_resolve -- resolve reference in *ref_io to new address */

mps_bool_t MPS_CALL mps_resolve(mps_ss_t mps_ss, mps_addr_t *ref_io)
{
  ScanState ss = (ScanState)mps_ss;
  Ref *refIO = (Ref *)ref_io;

  /* See design.mps.trace.fix.noaver */
  AVERT_CRITICAL(ScanState, ss);
  AVER_CRITICAL(refIO != NULL);

  return (mps_bool_t)TraceResolve(ss, refIO);
}


/* mps_scan_poke -- update addr to ref, from inside the scanner
 *
 * Takes care of maintaining invariants without tripping barriers. */

void MPS_CALL mps_scan_poke(mps_ss_t mps_ss, mps_addr_t addr, mps_addr_t ref)
{
  ScanState ss = (ScanState)mps_ss;
  Arena arena;
  Bool b;
  Seg seg;

  AVERT(ScanState, ss);
  /* Can't check addr as it is arbitrary */
  /* Can't check ref as it is arbitrary */

  arena = ss->arena;
  b = SegOfAddr(&seg, arena, addr);
  if (b) {
    if (seg == ss->seg) {
      /* If it's in the segment being scanned, update the scan state summary */
      ss->fixedSummary = ZoneSetAdd(arena, ss->fixedSummary, ref);
      *(Ref *)addr = ref;
    } else
      ArenaPokeSeg(arena, seg, addr, ref);
  } else
    *(Ref *)addr = ref;
}


/* mps_collections -- return number of collections performed */

mps_word_t MPS_CALL mps_collections(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  return ArenaEpoch(arena); /* thread safe: see impl.h.arena.epoch.ts */
}


/* mps_finalize -- register for finalization */

mps_res_t MPS_CALL mps_finalize(mps_arena_t mps_arena, mps_addr_t *refref)
{
  Res res;
  Addr object;
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);

  object = (Addr)ArenaPeek(arena, (Addr)refref);
  res = ArenaFinalize(arena, object);

  ArenaLeave(arena);
  return res;
}


/* mps_definalize -- deregister for finalization */

mps_res_t MPS_CALL mps_definalize(mps_arena_t mps_arena, mps_addr_t *refref)
{
  Res res;
  Addr object;
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);

  object = (Addr)ArenaPeek(arena, (Addr)refref);
  res = ArenaDefinalize(arena, object);

  ArenaLeave(arena);
  return res;
}


/* Messages */


mps_bool_t MPS_CALL mps_message_poll(mps_arena_t mps_arena)
{
  Bool b;
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);

  b = MessagePoll(arena);

  ArenaLeave(arena);
  return b;
}


mps_message_type_t MPS_CALL mps_message_type(mps_arena_t mps_arena,
                                    mps_message_t mps_message)
{
  Arena arena = (Arena)mps_arena;
  Message message = (Message)mps_message;
  MessageType type;

  ArenaEnter(arena);

  type = MessageGetType(message);

  ArenaLeave(arena);

  return (mps_message_type_t)type;
}

void MPS_CALL mps_message_discard(mps_arena_t mps_arena,
                         mps_message_t mps_message)
{
  Arena arena = (Arena)mps_arena;
  Message message = (Message)mps_message;

  ArenaEnter(arena);

  MessageDiscard(arena, message);

  ArenaLeave(arena);
}

void MPS_CALL mps_message_type_enable(mps_arena_t mps_arena,
                             mps_message_type_t mps_type)
{
  Arena arena = (Arena)mps_arena;
  MessageType type = (MessageType)mps_type;

  ArenaEnter(arena);

  MessageTypeEnable(arena, type);

  ArenaLeave(arena);
}

void MPS_CALL mps_message_type_disable(mps_arena_t mps_arena,
                              mps_message_type_t mps_type)
{
  Arena arena = (Arena)mps_arena;
  MessageType type = (MessageType)mps_type;

  ArenaEnter(arena);

  MessageTypeDisable(arena, type);

  ArenaLeave(arena);
}

mps_bool_t MPS_CALL mps_message_get(mps_message_t *mps_message_return,
                           mps_arena_t mps_arena,
                           mps_message_type_t mps_type)
{
  Bool b;
  Arena arena = (Arena)mps_arena;
  MessageType type = (MessageType)mps_type;
  Message message;

  ArenaEnter(arena);

  b = MessageGet(&message, arena, type);

  ArenaLeave(arena);
  if (b) {
    *mps_message_return = (mps_message_t)message;
  }
  return b;
}

mps_bool_t MPS_CALL mps_message_queue_type(mps_message_type_t *mps_message_type_return,
                                  mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  MessageType type;
  Bool b;

  ArenaEnter(arena);

  b = MessageQueueType(&type, arena);

  ArenaLeave(arena);
  if (b) {
    *mps_message_type_return = (mps_message_type_t)type;
  }
  return b;
}


/* Message-Type-Specific Methods */

/* MPS_MESSAGE_TYPE_FINALIZATION */

void MPS_CALL mps_message_finalization_ref(mps_addr_t *mps_addr_return,
                                  mps_arena_t mps_arena,
                                  mps_message_t mps_message)
{
  Arena arena = (Arena)mps_arena;
  Message message = (Message)mps_message;
  Ref ref;

  AVER(mps_addr_return != NULL);

  ArenaEnter(arena);

  AVERT(Arena, arena);
  MessageFinalizationRef(&ref, arena, message);
  ArenaPoke(arena, (Addr)mps_addr_return, ref);

  ArenaLeave(arena);
}

/* MPS_MESSAGE_TYPE_GC */

size_t MPS_CALL mps_message_gc_live_size(mps_arena_t mps_arena,
                                              mps_message_t mps_message)
{
  Arena arena = (Arena)mps_arena;
  Message message = (Message)mps_message;
  Size size;

  ArenaEnter(arena);

  AVERT(Arena, arena);
  size = MessageGCLiveSize(message);

  ArenaLeave(arena);
  return (size_t)size;
}

size_t MPS_CALL mps_message_gc_condemned_size(mps_arena_t mps_arena,
                                     mps_message_t mps_message)
{
  Arena arena = (Arena)mps_arena;
  Message message = (Message)mps_message;
  Size size;

  ArenaEnter(arena);

  AVERT(Arena, arena);
  size = MessageGCCondemnedSize(message);

  ArenaLeave(arena);
  return (size_t)size;
}

size_t MPS_CALL mps_message_gc_not_condemned_size(mps_arena_t mps_arena,
                                         mps_message_t mps_message)
{
  Arena arena = (Arena)mps_arena;
  Message message = (Message)mps_message;
  Size size;

  ArenaEnter(arena);

  AVERT(Arena, arena);
  size = MessageGCNotCondemnedSize(message);

  ArenaLeave(arena);
  return (size_t)size;
}


/* Telemetry */

mps_word_t MPS_CALL mps_telemetry_control(mps_word_t resetMask, mps_word_t flipMask)
{
  /* Doesn't require locking and isn't arena-specific. */
  return EventControl((Word)resetMask, (Word)flipMask);
}

mps_word_t MPS_CALL mps_telemetry_intern(const char *label)
{
  AVER(label != NULL);
  return (mps_word_t)EventInternString(label);
}

mps_word_t MPS_CALL mps_telemetry_intern_length(const char *label, size_t length)
{
  AVER(label != NULL);
  return (mps_word_t)EventInternGenString((Size)length, label);
}

void MPS_CALL mps_telemetry_label(mps_addr_t addr, mps_word_t intern_id)
{
  EventLabelAddr((Addr)addr, (Word)intern_id);
}

void MPS_CALL mps_telemetry_flush(void)
{
  /* Telemetry does its own concurrency control, so none here. */
  (void)EventSync();
}


/* Allocation Patterns */


mps_alloc_pattern_t MPS_CALL mps_alloc_pattern_ramp(void)
{
  return (mps_alloc_pattern_t)AllocPatternRamp();
}

mps_alloc_pattern_t MPS_CALL mps_alloc_pattern_ramp_collect_all(void)
{
  return (mps_alloc_pattern_t)AllocPatternRampCollectAll();
}


/* mps_ap_alloc_pattern_begin -- signal start of an allocation pattern
 *
 * .ramp.hack: There are only two allocation patterns, both ramps.  So
 * we assume it's a ramp, and call BufferRampBegin/End directly, without
 * dispatching.  No point in creating a mechanism for that.  */

mps_res_t MPS_CALL mps_ap_alloc_pattern_begin(mps_ap_t mps_ap,
                                     mps_alloc_pattern_t alloc_pattern)
{
  Buffer buf;
  Arena arena;

  AVER(mps_ap != NULL);
  buf = BufferOfAP((AP)mps_ap);
  AVER(CHECKT(Buffer, buf));

  arena = BufferArena(buf);
  ArenaEnter(arena);

  BufferRampBegin(buf, (AllocPattern)alloc_pattern);

  ArenaLeave(arena);
  return MPS_RES_OK;
}


mps_res_t MPS_CALL mps_ap_alloc_pattern_end(mps_ap_t mps_ap,
                                   mps_alloc_pattern_t alloc_pattern)
{
  Buffer buf;
  Arena arena;
  Res res;

  AVER(mps_ap != NULL);
  buf = BufferOfAP((AP)mps_ap);
  AVER(CHECKT(Buffer, buf));
  UNUSED(alloc_pattern); /* .ramp.hack */

  arena = BufferArena(buf);
  ArenaEnter(arena);

  res = BufferRampEnd(buf);
  ArenaPoll(ArenaGlobals(arena), TRUE); /* .poll */

  ArenaLeave(arena);
  return res;
}


mps_res_t MPS_CALL mps_ap_alloc_pattern_reset(mps_ap_t mps_ap)
{
  Buffer buf;
  Arena arena;

  AVER(mps_ap != NULL);
  buf = BufferOfAP((AP)mps_ap);
  AVER(CHECKT(Buffer, buf));

  arena = BufferArena(buf);
  ArenaEnter(arena);

  BufferRampReset(buf);
  ArenaPoll(ArenaGlobals(arena), TRUE); /* .poll */

  ArenaLeave(arena);
  return MPS_RES_OK;
}


/* Low memory reservoir */


/* mps_reservoir_limit_set -- set the reservoir size */

void MPS_CALL mps_reservoir_limit_set(mps_arena_t mps_arena, size_t size)
{
  Arena arena = (Arena)mps_arena;

  ArenaEnter(arena);
  ReservoirSetLimit(ArenaReservoir(arena), size);
  ArenaLeave(arena);
}


/* mps_reservoir_limit -- return the reservoir size */

size_t MPS_CALL mps_reservoir_limit(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);

  size = ReservoirLimit(ArenaReservoir(arena));

  ArenaLeave(arena);
  return size;
}


/* mps_reservoir_available -- return memory available in the reservoir */

size_t MPS_CALL mps_reservoir_available(mps_arena_t mps_arena)
{
  Arena arena = (Arena)mps_arena;
  Size size;

  ArenaEnter(arena);

  size = ReservoirAvailable(ArenaReservoir(arena));

  ArenaLeave(arena);
  return size;
}


/* Chains */


/* mps_chain_create -- create a chain */

mps_res_t MPS_CALL mps_chain_create(mps_chain_t *chain_o, mps_arena_t mps_arena,
                           size_t gen_count, mps_gen_param_s *params)
{
  Arena arena = (Arena)mps_arena;
  Chain chain;
  Res res;

  ArenaEnter(arena);

  AVER(gen_count > 0);
  res = ChainCreate(&chain, arena, gen_count, (GenParamStruct *)params);

  ArenaLeave(arena);
  if (res != ResOK) return res;
  *chain_o = (mps_chain_t)chain;
  return MPS_RES_OK;
}


/* mps_chain_destroy -- destroy a chain */

void MPS_CALL mps_chain_destroy(mps_chain_t mps_chain)
{
  Arena arena;
  Chain chain = (Chain)mps_chain;

  AVER(CHECKT(Chain, chain));
  arena = chain->arena;

  ArenaEnter(arena);
  ChainDestroy(chain);
  ArenaLeave(arena);
}


/* mps_arena_roots_walk -- iterate over all roots */

void MPS_CALL mps_arena_roots_walk(mps_arena_t mps_arena, mps_roots_stepper_t f,
                          void *p, size_t s)
{
  Arena arena = (Arena)mps_arena;
  Res res;

  ArenaEnter(arena);
  AVER(FUNCHECK(f));
  /* p and s are arbitrary, hence can't be checked */

  AVER(ArenaGlobals(arena)->clamped);          /* .assume.parked */
  AVER(arena->busyTraces == TraceSetEMPTY);    /* .assume.parked */

  res = ArenaRootsWalk(ArenaGlobals(arena), f, p, s);
  AVER(res == ResOK);
  ArenaLeave(arena);
}


/* mps_arena_formatted_objects_walk -- iterate over all formatted objects */


static void ArenaFormattedObjectsStep(Addr object, Format format, Pool pool,
                                      void *p)
{
  FormattedObjectsStepClosure c;
  /* Can't check object */
  AVERT(Format, format);
  AVERT(Pool, pool);
  c = p;
  AVERT(FormattedObjectsStepClosure, c);

  (*c->f)((mps_addr_t)object, (mps_fmt_t)format, (mps_pool_t)pool,
          c->p, c->s);
}


void MPS_CALL mps_arena_formatted_objects_walk(mps_arena_t mps_arena,
                                      mps_formatted_objects_stepper_t f,
                                      void *p, size_t s)
{
  Arena arena = (Arena)mps_arena;
  FormattedObjectsStepClosureStruct c;

  ArenaEnter(arena);
  AVERT(Arena, arena);
  AVER(FUNCHECK(f));
  /* p and s are arbitrary, hence can't be checked */

  c.sig = FormattedObjectsStepClosureSig;
  c.f = f;
  c.p = p;
  c.s = s;
  ArenaFormattedObjectsWalk(arena, ArenaFormattedObjectsStep, &c);

  ArenaLeave(arena);
}


/* mps_pool_debug_walk -- iterate over all objects in a debug pool */

void MPS_CALL mps_pool_debug_walk(mps_pool_t mps_pool, mps_tag_stepper_t f,
                                  void *p)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;

  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);
  ArenaEnter(arena);
  AVERT(Pool, pool);
  AVER(FUNCHECK(f));
  /* p is arbitrary, hence can't be checked */

  TagWalk(pool, (ObjectsStepMethod)f, p);

  ArenaLeave(arena);
}
