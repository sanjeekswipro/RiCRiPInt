/* impl.c.eventrep: Allocation replayer routines
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * $Id: eventrep.c,v 1.9.11.1.1.1 2013/12/19 11:27:10 anon Exp $
 * $HopeName: MMsrc!eventrep.c(EBDSDK_P.1) $
 */

#include "config.h"
/* override variety setting for EVENT */
#define EVENT

#include "eventcom.h"
#include "eventrep.h"
#include "eventpro.h"
#include "mpmtypes.h"

#include "mps.h"
#include "mpsavm.h"
#include "mpsacl.h"
#include "mpscmv.h"
#include "mpscmvff.h"
#include "mpscepvm.h"
#include "fmtpstst.h"
#include "mpscepdl.h"

#include "table.h"

#include <stddef.h> /* for size_t */
#include <stdarg.h> /* for va_list */
#include <stdlib.h> /* for EXIT_FAILURE */
#include <stdio.h> /* for printf */
#include "mpstd.h"
#ifdef MPS_OS_SU
#include "ossu.h"
#endif


/* ulongest -- longest integer type
 * FLD -- printf format for ulongest in decimal, with given field size
 */
#if _MSC_VER >= 1300
typedef unsigned long long ulongest;
#define FLD(w) "%" STR(w) "Iu"
#else
typedef unsigned long ulongest;
#define FLD(w) "%" STR(w) "lu"
#endif


/* WORD_FORMAT -- printf format to print a Word */

#if _MSC_VER >= 1300
#define WORD_FORMAT "%Iu"
#else
#define WORD_FORMAT "%lu"
#endif


/* define constants for all event codes */
enum {
#define RELATION(name, code, always, kind, format) \
  Event##name = code,
#include "eventdef.h"
#undef RELATION
};


/* Globals */

static ulongest totalEvents; /* count of events */
static ulongest discardedEvents; /* count of ignored events */
static ulongest unknownEvents; /* count of unknown events */

static Bool partialLog;
static Word eventTime;

/* Dictionaries for translating from log to replay values */
static Table arenaTable; /* dictionary of arenas */
static Table poolTable; /* dictionary of poolReps */
static Table apTable; /* dictionary of apReps */


/* poolSupport -- describes pool support for explicit deallocation */

enum {supportTruncate = 1, supportFree, supportNothing};
typedef int poolSupport;


/* objectTable -- object address mapping structure
 *
 * .obj-mapping.truncate: Pools that support truncate need to keep track
 * of object end points as well.  .obj-mapping.partial-free: Arbitrary
 * partial free is not supported.
 */

typedef struct objectTableStruct {
  Table startTable;
  Table endTable;
} objectTableStruct;
typedef struct objectTableStruct *objectTable;


/* poolRep -- pool tracking structure
 *
 * .pool.object-addr: Pools that support explicit free (or truncate)
 * need to maintain a mapping from the addresses in the log to those in
 * the replay.
 *
 * .bufclass: In order to create APs with the correct arguments, the
 * replayer has to pick the right BufferInit event to use, as there's
 * one for each superclass.  The pool determines the buffer class, so
 * we store its subclass level in the pool representation.
 */

typedef struct poolRepStruct {
  mps_pool_t pool; /* the replay pool */
  objectTable objects;
  int bufferClassLevel; /* subclass level of the buffer class */
} poolRepStruct;
typedef struct poolRepStruct *poolRep;


/* apRep -- ap tracking structure */

typedef struct apRepStruct {
  mps_ap_t ap; /* the replay ap */
  objectTable objects; /* object mapping for the pool of this ap */
} apRepStruct;
typedef struct apRepStruct *apRep;


/* PointerAdd -- add offset to pointer */

#define PointerAdd(p, s) ((void *)((char *)(p) + (s)))
#define PointerSub(p, s) ((void *)((char *)(p) - (s)))


/* error -- error signalling */

static void error(const char *format, ...)
{
  va_list args;

  fflush(stdout); /* sync */
  fprintf(stderr, "Failed @"WORD_FORMAT" ", eventTime);
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  exit(EXIT_FAILURE);
}


/* verify, verifyMPS -- check return values
 *
 * We don't use assert for this, because we want it in release as well.
 */

#define verifyMPS(res) \
  MPS_BEGIN if ((res) != MPS_RES_OK) error("line %d MPS", __LINE__); MPS_END

#define verify(cond) \
  MPS_BEGIN if (!(cond)) error("line %d " #cond, __LINE__); MPS_END


#ifdef MPS_PROD_EPCORE


/* ensurePSFormats -- return the PS formats, creating them, if necessary */

static mps_fmt_t psFixedFormat = NULL;
static mps_fmt_t psTypedFormat = NULL;

static void ensurePSFormats(mps_fmt_t *fixedOut, mps_fmt_t *typedOut,
                            mps_arena_t arena)
{
  mps_res_t eres;

  if (psFixedFormat == NULL) {
    eres = mps_fmt_create_fixed(&psFixedFormat, arena, ps_fmt_fixed());
    verifyMPS(eres);
    eres = mps_fmt_create_A(&psTypedFormat, arena, ps_fmt_typed());
    verifyMPS(eres);
  }
  *fixedOut = psFixedFormat;
  *typedOut = psTypedFormat;
}


/* finishPSFormats -- finish the PS formats, if necessary */

static void finishPSFormats(void)
{
  if (psFixedFormat != NULL) {
    mps_fmt_destroy(psFixedFormat);
    mps_fmt_destroy(psTypedFormat);
  }
}


#endif


/* objectTableCreate -- create an objectTable */

static objectTable objectTableCreate(poolSupport support)
{
  if (support != supportNothing) {
    Res ires;
    objectTable table;

    table = malloc(sizeof(objectTableStruct));
    verify(table != NULL);
    ires = TableCreate(&table->startTable, (size_t)1<<12);
    verify(ires == ResOK);
    if (support == supportTruncate) {
      ires = TableCreate(&table->endTable, (size_t)1<<12);
      verify(ires == ResOK);
    } else {
      table->endTable = NULL;
    }
    return table;
  } else {
    return NULL;
  }
}


/* objectTableDestroy -- destroy an objectTable */

static void objectTableDestroy(objectTable table)
{
  if (table != NULL) {
    TableDestroy(table->startTable);
    if (table->endTable != NULL)
      TableDestroy(table->endTable);
    free(table);
  }
}


/* objDefine -- add a new mapping to an objectTable */

static void objDefine(objectTable table,
                      void *logObj, void *obj, size_t size)
{
  if (table != NULL) {
    Res ires;

    ires = TableDefine(table->startTable, (Word)logObj, obj);
    verify(ires == ResOK);
    if (table->endTable != NULL) {
      ires = TableDefine(table->endTable,
                         (Word)PointerAdd(logObj, size),
                         PointerAdd(obj, size));
      verify(ires == ResOK);
    }
  }
}


/* objRemove -- look up and remove a mapping in an objectTable */

static void objRemove(void **objReturn, objectTable table,
                      void *logObj, size_t size)
{
  Bool found;
  Res ires;
  void *obj;
  void *end;
  void *logEnd;

  found = TableLookup(&obj, table->startTable, (Word)logObj);
  if (found) {
    ires = TableRemove(table->startTable, (Word)logObj);
    verify(ires == ResOK);
    if (table->endTable != NULL) {
      ires = TableRemove(table->endTable,
                         (Word)PointerAdd(logObj, size));
      verify(ires == ResOK);
    }
    *objReturn = obj;
    return;
  }
  /* Must be a truncation. */
  verify(table->endTable != NULL);
  logEnd = PointerAdd(logObj, size);
  found = TableLookup(&end, table->endTable, (Word)logEnd);
  verify(found);
  obj = PointerSub(end, size);
  /* Remove the old end and insert the new one. */
  ires = TableRemove(table->endTable, (Word)logEnd);
  verify(ires == ResOK);
  ires = TableDefine(table->endTable, (Word)logObj, obj);
  verify(ires == ResOK);
  *objReturn = obj;
  return;
}


/* poolRecreate -- create and record a pool */

static void poolRecreate(void *logPool, void *logArena, mps_class_t class,
                         poolSupport support, int bufferClassLevel, ...)
{
  va_list args;
  mps_pool_t pool;
  mps_res_t eres;
  poolRep rep;
  Res ires;
  void *entry;
  Bool found;

  found = TableLookup(&entry, arenaTable, (Word)logArena);
  verify(found);
  va_start(args, bufferClassLevel);
  eres = mps_pool_create_v(&pool, (mps_arena_t)entry, class, args);
  verifyMPS(eres);
  va_end(args);
  rep = malloc(sizeof(poolRepStruct));
  verify(rep != NULL);
  rep->pool = pool;
  rep->objects = objectTableCreate(support);
  rep->bufferClassLevel = bufferClassLevel;
  ires = TableDefine(poolTable, (Word)logPool, (void *)rep);
  verify(ires == ResOK);
}


/* poolRedestroy -- destroy and derecord a pool */

static void poolRedestroy(void *logPool)
{
  Res ires;
  void *entry;
  Bool found;
  poolRep rep;

  found = TableLookup(&entry, poolTable, (Word)logPool);
  verify(found);
  rep = (poolRep)entry;
  mps_pool_destroy(rep->pool);
  ires = TableRemove(poolTable, (Word)logPool);
  verify(ires == ResOK);
  objectTableDestroy(rep->objects);
  free(rep);
}


/* apRecreate -- create and record an ap */

static void apRecreate(void *logAp, void *logPool, ...)
{
  va_list args;
  mps_ap_t ap;
  poolRep pRep;
  apRep aRep;
  mps_res_t eres;
  Res ires;
  void *entry;
  Bool found;

  found = TableLookup(&entry, poolTable, (Word)logPool);
  verify(found);
  pRep = (poolRep)entry;
  va_start(args, logPool);
  eres = mps_ap_create_v(&ap, pRep->pool, args);
  verifyMPS(eres);
  va_end(args);
  aRep = malloc(sizeof(apRepStruct));
  verify(aRep != NULL);
  aRep->ap = ap;
  aRep->objects = pRep->objects;
  ires = TableDefine(apTable, (Word)logAp, (void *)aRep);
  verify(ires == ResOK);
}


/* apRedestroy -- destroy and derecord an ap */

static void apRedestroy(void *logAp)
{
  Res ires;
  void *entry;
  Bool found;
  apRep rep;

  found = TableLookup(&entry, apTable, (Word)logAp);
  verify(found);
  rep = (apRep)entry;
  mps_ap_destroy(rep->ap);
  ires = TableRemove(apTable, (Word)logAp);
  verify(ires == ResOK);
  free(rep);
}


/* EventReplay -- replay event */

static arenaJustCreated = FALSE;

void EventReplay(Event event, Word etime)
{
  mps_res_t eres;
  Res ires;
  Bool found;
  void *entry;

  ++totalEvents;
  eventTime = etime;
  switch(EventGetCode(event)) {
  case EventArenaCreateVM: { /* arena, userSize, chunkSize */
    mps_arena_t arena;

    eres = mps_arena_create(&arena, mps_arena_class_vm(),
                            event->pww.w1);
    verifyMPS(eres);
    ires = TableDefine(arenaTable, (Word)event->pww.p0, (void *)arena);
    verify(ires == ResOK);
    arenaJustCreated = TRUE;
  } break;
  case EventArenaCreateVMNZ: { /* arena, userSize, chunkSize */
    mps_arena_t arena;

    eres = mps_arena_create(&arena, mps_arena_class_vmnz(),
                            event->pww.w1);
    verifyMPS(eres);
    ires = TableDefine(arenaTable, (Word)event->pww.p0, (void *)arena);
    verify(ires == ResOK);
    arenaJustCreated = TRUE;
  } break;
  case EventArenaCreateCL: { /* arena, size, base */
    mps_arena_t arena;
    void *base;

    base = malloc((size_t)event->pwa.w1);
    verify(base != NULL);
    eres = mps_arena_create(&arena, mps_arena_class_cl(),
                            (Size)event->pwa.w1, base);
    verifyMPS(eres);
    ires = TableDefine(arenaTable, (Word)event->pw.p0, (void *)arena);
    verify(ires == ResOK);
    arenaJustCreated = TRUE;
  } break;
  case EventArenaDestroy: { /* arena */
    found = TableLookup(&entry, arenaTable, (Word)event->p.p0);
    verify(found);
#ifdef MPS_PROD_EPCORE
    /* @@@@ assuming there's only one arena at a time */
    finishPSFormats();
#endif
    mps_arena_destroy((mps_arena_t)entry);
    ires = TableRemove(arenaTable, (Word)event->pw.p0);
    verify(ires == ResOK);
  } break;
  case EventPoolInitMVFF: {
    /* pool, arena, extendBy, avgSize, align, slotHigh, arenaHigh, firstFit */
    poolRecreate(event->ppwwwuuu.p0, event->ppwwwuuu.p1,
                 mps_class_mvff(), supportTruncate, 0,
                 (size_t)event->ppwwwuuu.w2,
                 (size_t)event->ppwwwuuu.w3,
                 (size_t)event->ppwwwuuu.w4,
                 (mps_bool_t)event->ppwwwuuu.u5,
                 (mps_bool_t)event->ppwwwuuu.u6,
                 (mps_bool_t)event->ppwwwuuu.u7);
  } break;
  case EventPoolInitMV: { /* pool, arena, extendBy, avgSize, maxSize */
    /* .pool.control: The control pool will get created just after */
    /* its arena; ignore it. */
    if (!arenaJustCreated) {
      poolRecreate(event->ppwww.p0, event->ppwww.p1,
                   mps_class_mv(), supportFree, 0, (size_t)event->ppwww.w2,
                   (size_t)event->ppwww.w3, (size_t)event->ppwww.w4);
    } else {
      arenaJustCreated = FALSE;
    }
  } break;
  case EventPoolInitMFS: { /* pool, arena, extendBy, unitSize */
    /* internal only */
    ++discardedEvents;
  } break;
  case EventPoolInit: { /* pool, arena, class */
    /* all internal only */
    ++discardedEvents;
  } break;
#ifdef MPS_PROD_EPCORE
  case EventPoolInitEPVM: {
    /* pool, arena, format, maxSaveLevel, saveLevel */
    mps_arena_t arena;
    mps_fmt_t fixedFormat, typedFormat;
    static poolCount = 0;

    found = TableLookup(&entry, arenaTable, (Word)event->pppuu.p1);
    verify(found);
    arena = (mps_arena_t)entry;
    /* @@@@ Formats are hardcoded. */
    /* Must create the formats here, where we know the arena. */
    ensurePSFormats(&fixedFormat, &typedFormat, arena);
    poolRecreate(event->pppuu.p0, event->pppuu.p1,
                 mps_class_epvm(), supportNothing, 2,
                 (poolCount++ < 2) ? fixedFormat : typedFormat,
                 (mps_epvm_save_level_t)event->pppuu.u3,
                 (mps_epvm_save_level_t)event->pppuu.u4);
  } break;
  case EventPoolInitEPDL: {
    /* pool, arena, isEPDL, extendBy, avgSize, align */
    poolRecreate(event->ppuwww.p0, event->ppuwww.p1,
                 event->ppuwww.u2 ? mps_class_epdl() : mps_class_epdr(),
                 event->ppuwww.u2 ? supportTruncate : supportFree, 0,
                 (size_t)event->ppuwww.w3, (size_t)event->ppuwww.w4,
                 (size_t)event->ppuwww.w5);
  } break;
#endif
  case EventPoolFinish: { /* pool */
    found = TableLookup(&entry, poolTable, (Word)event->p.p0);
    if (found) {
      poolRedestroy(event->p.p0);
    } else {
      ++discardedEvents;
    }
  } break;
  case EventBufferInit: { /* buffer, pool, isMutator */
    if ((Bool)event->ppu.u2) {
      found = TableLookup(&entry, poolTable, (Word)event->ppu.p1);
      if (found) {
        poolRep rep = (poolRep)entry;

        if(rep->bufferClassLevel == 0) { /* see .bufclass */
          apRecreate(event->ppu.p0, event->ppu.p1);
        } else {
          ++discardedEvents;
        }
      } else {
        ++discardedEvents;
      }
    } else {
      ++discardedEvents;
    }
  } break;
  case EventBufferInitSeg: { /* buffer, pool, isMutator */
    if ((Bool)event->ppu.u2) {
      found = TableLookup(&entry, poolTable, (Word)event->ppu.p1);
      if (found) {
        poolRep rep = (poolRep)entry;

        if(rep->bufferClassLevel == 1) { /* see .bufclass */
          apRecreate(event->ppu.p0, event->ppu.p1);
        } else {
          ++discardedEvents;
        }
      } else {
        ++discardedEvents;
      }
    } else {
      ++discardedEvents;
    }
  } break;
  case EventBufferInitRank: { /* buffer, pool, isMutator, rank */
    if ((Bool)event->ppuu.u2) {
      found = TableLookup(&entry, poolTable, (Word)event->ppuu.p1);
      if (found) {
        poolRep rep = (poolRep)entry;

        if(rep->bufferClassLevel == 2) { /* see .bufclass */
          apRecreate(event->ppuu.p0, event->ppuu.p1, event->ppuu.u3);
        } else {
          ++discardedEvents;
        }
      } else {
        ++discardedEvents;
      }
    } else {
      ++discardedEvents;
    }
  } break;
#ifdef MPS_PROD_EPCORE
  case EventBufferInitEPVM: { /* buffer, pool, isObj */
    found = TableLookup(&entry, poolTable, (Word)event->ppu.p1);
    if (found) {
      poolRep rep = (poolRep)entry;

      if(rep->bufferClassLevel == 2) { /* see .bufclass */
        apRecreate(event->ppu.p0, event->ppu.p1, (mps_bool_t)event->ppu.u2);
      } else {
        ++discardedEvents;
      }
    } else {
      ++discardedEvents;
    }
  } break;
#endif
  case EventBufferFinish: { /* buffer */
    found = TableLookup(&entry, apTable, (Word)event->p.p0);
    if (found) {
      apRedestroy(event->p.p0);
    } else {
      ++discardedEvents;
    }
  } break;
  case EventBufferReserve: { /* buffer, init, size */
    found = TableLookup(&entry, apTable, (Word)event->paw.p0);
    if (found) {
      apRep rep = (apRep)entry;
      mps_addr_t p;

      eres = mps_reserve(&p, rep->ap, (size_t)event->paw.w2);
      verifyMPS(eres);
    } else {
      ++discardedEvents;
    }
  } break;
  case EventBufferCommit: { /* buffer, p, size, clientClass */
    found = TableLookup(&entry, apTable, (Word)event->pawa.p0);
    if (found) {
      apRep rep = (apRep)entry;
      mps_addr_t obj = rep->ap->init;
      mps_bool_t committed;
      size_t size = (size_t)event->pawa.w2;

      committed = mps_commit(rep->ap, obj, size);
      verifyMPS(committed ? MPS_RES_OK : MPS_RES_FAIL);
      objDefine(rep->objects, event->pawa.a1, obj, size);
    } else {
      ++discardedEvents;
    }
  } break;
  case EventPoolAllocDebug: /* pool, obj, size, location, class */
  case EventPoolAlloc: { /* pool, obj, size */
    /* Hack: We use paw accessors into pawww. */
    found = TableLookup(&entry, poolTable, (Word)event->paw.p0);
    if (found) {
      poolRep rep = (poolRep)entry;
      void *obj;
      size_t size = (size_t)event->paw.w2;

      eres = mps_alloc(&obj, rep->pool, size);
      verifyMPS(eres);
      objDefine(rep->objects, event->paw.a1, obj, size);
      /* Simulate init by writing into the object */
      (void)memset(obj, (int)etime, size);
    } else {
      ++discardedEvents;
    }
  } break;
  case EventPoolFree: { /* pool, obj, size */
    found = TableLookup(&entry, poolTable, (Word)event->paw.p0);
    if (found) {
      poolRep rep = (poolRep)entry;
      void *obj;
      size_t size = (size_t)event->paw.w2;

      objRemove(&obj, rep->objects, event->paw.a1, size);
      mps_free(rep->pool, obj, size);
    } else {
      ++discardedEvents;
    }
  } break;
#ifdef MPS_PROD_EPCORE
  case EventPoolPush: { /* pool */
    found = TableLookup(&entry, poolTable, (Word)event->p.p0);
    if (found) {
      poolRep rep = (poolRep)entry;

      /* It must be EPVM. */
      mps_epvm_save(rep->pool);
    }
  } break;
  case EventPoolPop: { /* pool, level */
    found = TableLookup(&entry, poolTable, (Word)event->pu.p0);
    if (found) {
      poolRep rep = (poolRep)entry;

      /* It must be EPVM. */
      mps_epvm_restore(rep->pool, (mps_epvm_save_level_t)event->pu.u1);
    }
  } break;
#endif
  case EventCommitLimitSet: { /* arena, limit, succeeded */
    found = TableLookup(&entry, arenaTable, (Word)event->pwu.p0);
    verify(found);
    eres = mps_arena_commit_limit_set((mps_arena_t)entry,
                                      (size_t)event->pwu.w1);
    verifyMPS(((Bool)event->pwu.u2 == (eres == MPS_RES_OK))
                ? MPS_RES_OK : MPS_RES_FAIL);
  } break;
  case EventSpareCommitLimitSet: { /* arena, limit */
    found = TableLookup(&entry, arenaTable, (Word)event->pw.p0);
    verify(found);
    (void)mps_arena_spare_commit_limit_set((mps_arena_t)entry,
                                           (size_t)event->pw.w1);
  } break;
  case EventReservoirLimitSet: { /* arena, limit */
    found = TableLookup(&entry, arenaTable, (Word)event->pw.p0);
    verify(found);
    mps_reservoir_limit_set((mps_arena_t)entry, (size_t)event->pw.w1);
  } break;
  case EventVMMap: case EventVMUnmap:
  case EventVMCreate: case EventVMDestroy:
  case EventArenaWriteFaults:
  case EventArenaAlloc: case EventArenaAllocFail: case EventArenaFree:
  case EventSegAlloc: case EventSegAllocFail: case EventSegFree:
  case EventSegMerge: case EventSegSplit:
  case EventBufferFill: case EventBufferEmpty:
  case EventCBSInit: case EventMeterInit: case EventMeterValues:
  case EventIntern: case EventLabel: {
    ++discardedEvents;
  } break;
  default: {
    ++unknownEvents;
    if (unknownEvents < 12) /* don't output too much */
      printf("Unknown event @%ld: %s.\n", etime,
             EventCode2Name(EventGetCode(event)));
  } break;
  }
}


/* Checking macros, copied from check.h */

#define CHECKLVALUE(lv1, lv2) \
  ((void)sizeof((lv1) = (lv2)), (void)sizeof((lv2) = (lv1)), TRUE)

#define CHECKTYPE(t1, t2) \
  (sizeof(t1) == sizeof(t2) && \
   CHECKLVALUE(*((t1 *)0), *((t2 *)0)))


/* CHECKCONV -- check t2 can be cast to t1 without loss */

#define CHECKCONV(t1, t2) \
  (sizeof(t1) >= sizeof(t2))


/* EventRepInit -- initialize the module */

Res EventRepInit(Bool partial)
{
  Res res;

  /* Check using pointers as keys in the tables. */
  verify(CHECKCONV(Word, void *));
  /* Check storage of MPS opaque handles in the tables. */
  verify(CHECKTYPE(mps_arena_t, void *));
  verify(CHECKTYPE(mps_ap_t, void *));
  /* .event-conv: Conversion of event fields into the types required */
  /* by the MPS functions is justified by the reverse conversion */
  /* being acceptable (which is upto the event log generator). */

  partialLog = partial;
  totalEvents = 0; discardedEvents = 0; unknownEvents = 0;

  res = TableCreate(&arenaTable, (size_t)1);
  if (res != ResOK) goto failArena;
  res = TableCreate(&poolTable, (size_t)1<<4);
  if (res != ResOK) goto failPool;
  res = TableCreate(&apTable, (size_t)1<<6);
  if (res != ResOK) goto failAp;

  return ResOK;

failAp:
  TableDestroy(poolTable);
failPool:
  TableDestroy(arenaTable);
failArena:
  return res;
}


/* EventRepFinish -- finish the module */

void EventRepFinish(void)
{
  /* @@@@ add listing of remaining objects? */
  /* No point in cleaning up the tables, since we're quitting. */
  printf("Replayed "FLD(0)" and discarded "FLD(0)" events ("FLD(0)" unknown).\n",
         totalEvents - discardedEvents - unknownEvents,
         discardedEvents + unknownEvents, unknownEvents);
}
