/* impl.c.eventcnv: Simple event log converter
 *
 * $Id: eventcnv.c,v 1.16.1.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!eventcnv.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 */

#include "config.h"
/* override variety setting for EVENT */
#define EVENT

#include "eventcom.h"
#include "eventpro.h"
#include "mpmtypes.h"
#include "splay.h"
#include "table.h"

#include <stddef.h> /* for size_t */
#include <stdio.h> /* for printf */
#include <stdarg.h> /* for va_list */
#include <stdlib.h> /* for EXIT_FAILURE, strtoul et.al. */
#include <stdint.h> /* for intptr_t */
#include <assert.h> /* for assert */
#include <string.h> /* for strcmp */
#include <math.h> /* for sqrt */
#include <ctype.h> /* for isdigit et. al. */
#include "mpstd.h"
#include "misc.h" /* for STR, PARENT */
#ifdef MPS_OS_SU
#include "ossu.h"
#endif


/* ulongest -- longest integer type
 * FLD -- printf format for ulongest in decimal, with given field size
 * FLX -- printf format for ulongest in hex, with given field size
 */
#if _MSC_VER >= 1300
  typedef unsigned long long ulongest;
# define FLD(w) "%" STR(w) "I64u"
# define FLX(w) "%" STR(w) "I64X"
#else
  typedef unsigned long ulongest;
#  define FLD(w) "%" STR(w) "lu"
#  define FLX(w) "%" STR(w) "lX"
#endif


/* F64D -- printf format for 64-bit int in decimal, with given field size
 * F64X -- printf format for 64-bit int in hex, with given field size
 */
#if _MSC_VER >= 1300
# define F64D(w) "%" STR(w) "I64u"
# define F64X(w) "%" STR(w) "I64X"
#elif defined(_LP64)
# define F64D(w) "%" STR(w) "lu"
# define F64X(w) "%" STR(w) "lX"
#else
# define F64D(w) "%" STR(w) "llu"
# define F64X(w) "%" STR(w) "llX"
#endif


/* FWD -- printf format for Word in decimal, with given field size
 * FWX -- printf format for Word in hex, with given field size
 */
#if MPS_WORD_WIDTH == 64
# define FWD(w) F64D(w)
# define FWX(w) F64X(w)
# ifdef _MSC_VER
#   define STRTOUL _strtoui64
# elif defined(_LP64)
#   define STRTOUL strtoul
# else
#   define STRTOUL strtoull
# endif
#elif _MSC_VER >= 1300
# define FWD(w) "%" STR(w) "Iu"
# define FWX(w) "%" STR(w) "IX"
# define STRTOUL strtoul
#else
# define FWD(w) "%" STR(w) "lu"
# define FWX(w) "%" STR(w) "lX"
# define STRTOUL strtoul
#endif


/* FAX -- printf format for an address (represented as a Word)
 * HDW -- number of hex digits needed to print a Word
 *
 * These can't be replaced by MPS_WORD_WIDTH/4, because they are
 * processed into strings.
 */
#if MPS_WORD_WIDTH == 32
# define FAX FWX(08)
# define HDW 8
#elif MPS_WORD_WIDTH == 64
# define FAX FWX(016)
# define HDW 16
#else
# error "Unimplemented word width"
#endif


/* define constants for all event codes */
enum {
#define RELATION(name, code, always, kind, format) \
  EventCode##name = code,
#include "eventdef.h"
#undef RELATION
};

#define STRSIZE(str) (sizeof("" str "") - 1)

static mps_clock_t eventTime; /* current event time */
static mps_clock_t clocks_per_second = 1; /* clock rate */


/* event counters */

typedef ulongest eventCountArray[EventCodeMAX+1];
static ulongest bucketEventCount[EventCodeMAX+1];
static ulongest totalEventCount[EventCodeMAX+1];

/* structures for collecting detail information: */
typedef struct arena_data_t {
  void *addr ; /* address of arena */
  EventString name ; /* arena name */
  /* sorted tree of pool data in this arena */
  Table pools ;
  /* sorted tree of class data in this arena */
  Table classes ;
  Word curr_size, curr_resv, curr_count ; /* current size, reserved and count of allocs */
  Word peak_size, peak_resv, peak_count ; /* peak size, reserved and count of allocs */
  Word life_size, life_count ; /* lifetime size, reserved and count of allocs */
} arena_data_t ;

static void newArena(void *a) ;
static void endArena(void *a) ;

typedef struct pool_data_t {
  void *addr ; /* address of arena */
  EventString name ; /* pool name */
  struct arena_data_t *arena ; /* arena this pool is attached to */
  /* sorted tree of class data in this pool */
  Table classes ;
  /* sorted tree of addresses to allocation size and class */
  SplayTreeStruct allocs ;
  Word curr_size, curr_resv, curr_count ; /* current size and count of allocs */
  Word peak_size, peak_resv, peak_count ; /* peak size and count of allocs */
  Word life_size, life_count ; /* lifetime size and count of allocs */
} pool_data_t ;

static void newPool(void *p, void *a) ;
static void endPool(void *p) ;

static void trackReserve(void *p, Word size) ;
static void trackRelease(void *p, Word size) ;
static void trackAlloc(void *p, void *addr, Word size,
                       EventString name, EventString locn) ;
static void trackFree(void *p, void *addr, Word size) ;
static void trackCommit(void *a, Word size) ;

typedef struct class_data_t {
  EventString name ; /* class name */
  struct class_data_t *parent ; /* outer scope tracking the same class */
  Word curr_size, curr_count ; /* current size and count of allocs */
  Word peak_size, peak_count ; /* peak size and count of allocs */
  Word life_size, life_count ; /* lifetime size and count of allocs */
} class_data_t ;

/* We need to use a tree for addresses, because we search for any address in
   the range allocated. */
typedef struct alloc_range_t {
  Word addr ; /* address of allocation */
  size_t size ;    /* size of allocation */
} alloc_range_t ;

typedef struct alloc_node_t {
  SplayNodeStruct splayNode ;
  alloc_range_t alloc ;
  class_data_t *type ; /* class of allocation */
} alloc_node_t ;

#define Splay2Alloc(node) PARENT(alloc_node_t, splayNode, (node))

static Compare allocCompare(void *key, SplayNode splay)
{
  alloc_range_t *range = key ;
  alloc_node_t *node ;

  if ( range == NULL )
    return CompareLESS ;

  node = Splay2Alloc(splay) ;
  /* Any range overlap returns equal */
  if ( range->addr + range->size <= node->alloc.addr )
    return CompareLESS;
  if ( range->addr >= node->alloc.addr + node->alloc.size )
    return CompareGREATER;
  return CompareEQUAL ;
}

/* All arenas and pools */
static Table all_arenas, all_pools, all_classes ;

/* current size, reserved and count of all arena allocs */
Word curr_size = 0, curr_resv = 0, curr_count = 0 ;
/* peak size, reserved and count of all arena allocs */
Word peak_size = 0, peak_resv = 0, peak_count = 0 ;
/* lifetime size and count of all arena allocs */
Word life_size = 0, life_count = 0 ;

static Bool dumpSpec(EventProc proc, EventString str) ;

/* We also use a tree for size range filtering, to simplify searching.
   We could unify the implementation of allocations and size ranges,
   however allocations are exclusive and filter ranges are inclusive,
   so we either have to be very careful about boundary conditions,
   or would need different comparison functions. */
typedef struct filter_range_t {
  size_t start, end ;
} filter_range_t ;

typedef struct filter_node_t {
  SplayNodeStruct splayNode ;
  filter_range_t range ;
} filter_node_t ;

#define Splay2Filter(node) PARENT(filter_node_t, splayNode, (node))

static Compare filterCompare(void *key, SplayNode splay)
{
  filter_range_t *filter = key ;
  filter_node_t *node ;

  if ( filter == NULL )
    return CompareLESS ;

  node = Splay2Filter(splay) ;
  /* Any range overlap returns equal */
  if ( filter->end < node->range.start )
    return CompareLESS;
  if ( filter->start > node->range.end )
    return CompareGREATER;
  return CompareEQUAL ;
}

static Bool filtering = FALSE ;
static SplayTreeStruct filterRanges ;

static char *prog; /* program name */


/* command-line arguments */

static Bool verbose = FALSE;
/* Bitmask controlling when to summarise current state: */
static enum {
  DUMP_NEVER,
  DUMP_LOG_END = 1,
  DUMP_POOL_ALLOC = 2,
  DUMP_POOL_END = 4,
  DUMP_ARENA_ALLOC = 8,
  DUMP_ARENA_END = 16,
  DUMP_COMMIT = 32,
  DUMP_CLOCK = 64,
  DUMP_LABELS = 128
} track = DUMP_NEVER ;
/* style: '\0' for human-readable, 'L' for Lisp, 'C' for CDF, 'J' for Java. */
static char style = '\0';
static Bool reportEvents = FALSE;
static Bool eventEnabled[EventCodeMAX+1];
static Bool partialLog = FALSE;
static mps_clock_t bucketSize = 0;


/* error -- error signalling */

static void error(const char *format, ...)
{
  va_list args;

  fflush(stdout); /* sync */
  fprintf(stderr, "%s: @"F64D(0)" ", prog, eventTime);
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  exit(EXIT_FAILURE);
}


/* usage -- usage message */

static void usage(void)
{
  fprintf(stderr,
          "Usage: %s [-f logfile] [-p] [-v] [-e events] [-b size]"
          " [-S[CJLD]] [-t[apctEAP]] [-r range...] [-?]\n"
          "\t-f logfile\tInput logfile name (default mpsio.log).\n"
          "\t-p\tDo not report parse errors for incomplete logfiles.\n"
          "\t-v\tVerbose mode: print all events.\n"
          "\t-e events\tEvent statistics.\n"
          "\t\tEvents are '+' or '-' followed by an MPS event name, or 'all'.\n"
          "\t\tMultiple events may be concatenated in one specification.\n"
          "\t-b size\tBucket size for event statistics.\n"
          "\t-S[CJL]\tChange output format:\n"
          "\t\tNo style\tHuman readable output format.\n"
          "\t\tC\tCSV output format.\n"
          "\t\tJ\tJava output format.\n"
          "\t\tL\tLisp output format.\n"
          "\t-t[apEAP]\tTrack allocations.\n"
          "\t\ta\tPrint arena allocations to/from pools.\n"
          "\t\tp\tPrint pool allocations to/from client.\n"
          "\t\tc\tPrint arena commit changes.\n"
          "\t\tt\tInclude clock in printed allocations.\n"
          "\t\tE\tPrint live allocations at end of logfile.\n"
          "\t\tA\tPrint live allocations on arena destruction.\n"
          "\t\tP\tPrint live allocations on pool destruction.\n"
          "\t-r range\tRestrict line-by-line information to size ranges.\n"
          "\t\tRange is either a single size, or a low and high size separated\n"
          "\t\tby '-'. Either the low or high size may be omitted. Multiple\n"
          "\t\tsizes and ranges may be separated by commas. Size ranges\n"
          "\t\tmay not overlap.\n"
          "See doc.mps.guide.telemetry for instructions.\n",
          prog);
}


/* usageError -- explain usage and error */

static void usageError(void)
{
  usage();
  error("Bad usage");
}


/* parseEventSpec -- parses an event spec
 *
 * The spec is of the form: <name>[(+|-)<name>]...
 * The first name can be 'all'.
 */

static void parseEventSpec(const char *arg)
{
  size_t arglen;
  EventCode i;
  const char *end;
  char name[EventNameMAX+1];
  Bool enabled = TRUE;

  end = arg + strlen(arg);
  for(i = 0; i <= EventCodeMAX; ++i)
    eventEnabled[i] = FALSE;
  do {
    arglen = strcspn(arg, "+-");
    (void)strncpy(name, arg, arglen); name[arglen] = '\0';
    if (strcmp(name, "all") == 0) {
      for(i = 0; i <= EventCodeMAX; ++i)
        eventEnabled[i] = EventCodeIsValid(i);
    } else
      eventEnabled[EventName2Code(name)] = enabled;
    enabled = (arg[arglen] == '+'); arg += arglen + 1;
  } while (arg < end);
}


/* parseArgs -- parse command line arguments, return log file name */

static char *parseArgs(int argc, char *argv[])
{
  char *name = "mpsio.log";
  int i = 1;

  if (argc >= 1)
    prog = argv[0];
  else
    prog = "unknown";

  while (i < argc) { /* consider argument i */
    if (argv[i][0] == '-') { /* it's an option argument */
      switch (argv[i][1]) {
      case 'f': /* file name */
        ++ i;
        if (i == argc)
          usageError();
        else
          name = argv[i];
        break;
      case 'p': /* partial log */
        partialLog = TRUE;
        break;
      case 'v': /* verbosity */
        verbose = TRUE;
        break;
      case 'e': { /* event statistics */
        reportEvents = TRUE;
        ++ i;
        if (i == argc)
          usageError();
        else
          parseEventSpec(argv[i]);
      } break;
      case 'b': { /* bucket size */
        ++ i;
        if (i == argc)
          usageError();
        else {
          char *end;
          bucketSize = strtoul(argv[i], &end, 0) ;
          if (*end != '\0') usageError();
        }
      } break;
      case 'S': /* style */
        style = argv[i][2];
        break;
      case 't': { /* track and print */
        int j ;
        for ( j = 2 ; argv[i][j] ; ++j ) {
          switch ( argv[i][j] ) {
          case 'A':
            track |= DUMP_ARENA_END ;
            break ;
          case 'P':
            track |= DUMP_POOL_END ;
            break ;
          case 'E':
            track |= DUMP_LOG_END ;
            break ;
          case 'p':
            track |= DUMP_POOL_ALLOC ;
            break ;
          case 'a':
            track |= DUMP_ARENA_ALLOC ;
            break ;
          case 'c':
            track |= DUMP_COMMIT ;
            break ;
          case 't':
            track |= DUMP_CLOCK ;
            break ;
          default:
            usage() ;
            error("Unknown dump code '%c'", argv[i][j]);
          }
        }
        track |= DUMP_LABELS ;
      } break;
      case 'r': { /* range */
        if (++i == argc) {
          usageError();
        } else {
          char *curr = argv[i] ;

          for (;;) {
            filter_node_t *node ;
            char *end ;
            Res res ;

            if ( (node = malloc(sizeof(*node))) == NULL )
              error("Ran out of memory allocating filter range") ;
            SplayNodeInit(&node->splayNode) ;

            if ( *curr == '-' ) { /* No start of range */
              node->range.start = 0 ;
              node->range.end = strtoul(++curr, &end, 0) ;
              if ( curr == end )
                error("Invalid filter range: %s.", argv[i]) ;
            } else {
              node->range.start = node->range.end = strtoul(curr, &end, 0) ;
              if ( curr == end ) {
                error("Invalid filter range: %s.", argv[i]) ;
              } else if ( *end == '-' ) {
                curr = ++end ;
                node->range.end = strtoul(curr, &end, 0) ;
                if ( curr == end ) { /* No end of range */
                  node->range.end = ~(size_t)0 ;
                }
              }
            }

            if ( node->range.end < node->range.start )
              error("Invalid filter range: %s.", argv[i]) ;

            if ( (res = SplayTreeInsert(&filterRanges, &node->splayNode, &node->range)) != ResOK )
              error("Failed to insert filter record: error %d.", res) ;

            if ( *end == '\0' )
              break ;

            if ( *end != ',' )
              error("Invalid filter range: %s.", argv[i]) ;

            curr = ++end ;
          }

          filtering = TRUE ;
        }
      } break;
      case '?': case 'h': /* help */
        usage();
        break;
      default:
        usageError();
      }
    } /* if option */
    ++ i;
  }
  return name;
}

/* processEvent -- process event */

static void processEvent(EventProc proc, EventCode code, Event event)
{
  Res res;

  /* Dump pool/arena before removing label association */
  if ( track )
    switch(code) {
    case EventCodeArenaDestroy: /* P */
      endArena(event->p.p0) ;
      break ;
    case EventCodePoolFinish:   /* P */
      endPool(event->p.p0) ;
      break ;
    default:
      break;
    }

  res = EventRecord(proc, event, eventTime);
  if (res != ResOK)
    error("Can't record event: error %d.", res);

  if ( track )
    switch(code) {
    case EventCodeArenaCreateSH:  /* PWW */
      newArena(event->pww.p0) ;
      break ;
    case EventCodeArenaCreateCL:  /* PWA */
      newArena(event->pwa.p0) ;
      break ;
    case EventCodeArenaCreateVM:  /* PWW */
      newArena(event->pww.p0) ;
      break ;
    case EventCodeArenaCreateVMNZ: /* PWW */
      newArena(event->pww.p0) ;
      break ;
    case EventCodeArenaAlloc:     /* PPAWP */
      trackReserve(event->ppawp.p4, event->ppawp.w3) ;
      break ;
    case EventCodeArenaFree:      /* PAWP */
      trackRelease(event->pawp.p3, event->pawp.w2) ;
      break ;
    case EventCodePoolInit:       /* PPP */
      newPool(event->ppp.p0, event->ppp.p1) ;
      break ;
    case EventCodePoolInitMFS:    /* PPWW */
      newPool(event->ppww.p0, event->ppww.p1) ;
      break ;
    case EventCodePoolInitMVFF:   /* PPWWWUUU */
      newPool(event->ppwwwuuu.p0, event->ppwwwuuu.p1) ;
      break ;
    case EventCodePoolInitMV:     /* PPWWW */
      newPool(event->ppwww.p0, event->ppwww.p1) ;
      break ;
    case EventCodePoolInitEPVM:   /* PPPWW */
      newPool(event->ppwww.p0, event->ppwww.p1) ;
      break ;
    case EventCodePoolAlloc:      /* PAW */
      trackAlloc(event->paw.p0, event->paw.a1, event->paw.w2, NULL, NULL) ;
      break ;
    case EventCodePoolAllocDebug: /* PAWWW */
      trackAlloc(event->pawww.p0, event->pawww.a1, event->pawww.w2,
                 LabelText(proc, event->pawww.w4),
                 LabelText(proc, event->pawww.w3)) ;
      break ;
    case EventCodePoolFree:       /* PAW */
      trackFree(event->paw.p0, event->paw.a1, event->paw.w2) ;
      break ;
    case EventCodePoolPop:        /* P */
    case EventCodePoolPush:       /* PW */
      break ;
    case EventCodeCommitSet:      /* PW */
      trackCommit(event->pw.p0, event->pw.w1) ;
      break ;
    case EventCodeLabel:          /* AW */
      if ( track & DUMP_LABELS ) {
        Addr addr = event->aw.a0 ;
        Word label = event->aw.w1 ;

        if (label != 0) {
          if ( addr == 0 ) {
            EventString spec ;
            if ( (spec = LabelText(proc, label)) != NULL &&
                 !dumpSpec(proc, spec) )
              error("Invalid dump spec: %.*s.", spec->len, spec->str);
          } else {
            void *found ;
            if ( TableLookup(&found, all_pools, (Word)addr) ) {
              pool_data_t *pool = found ;
              pool->name = LabelText(proc, label);
            } else if ( TableLookup(&found, all_arenas, (Word)addr) ) {
              arena_data_t *arena = found ;
              arena->name = LabelText(proc, label);
            }
          }
        }
      }
      break ;
    default:
      break;
    }
}


/* Printing routines */


/* printStr -- print an EventString */

static void printStr(EventString str, Bool quotes)
{
  size_t i, len;

  if (quotes) putchar('"');
  len = str->len;
  for (i = 0; i < len; ++ i) {
    char c = str->str[i];
    if (quotes && (c == '"' || c == '\\')) putchar('\\');
    putchar(c);
  }
  if (quotes) putchar('"');
}


/* printAddr -- print an Addr or its label */

static void printAddr(EventProc proc, Addr addr)
{
  Word label;

  label = AddrLabel(proc, addr);
  if (label != 0 && addr != 0) {
    /* We assume labelling zero is meant to record a point in time */
    EventString sym = LabelText(proc, label);
    if (sym != NULL) {
      putchar(' ');
      printStr(sym, (style == 'C'));
    } else {
      printf((style == '\0') ? " sym "FWX(05) : " \"sym "FWX(0)"\"",
             label);
    }
  } else {
    if (style != 'C')
      printf(" "FAX, (Word)addr);
    else
      printf(" "FWD(0), (Word)addr);
  }
}


/* reportEventResults -- report event counts from a count array */

static void reportEventResults(eventCountArray eventCounts)
{
  EventCode i;
  ulongest total = 0;

  for(i = 0; i <= EventCodeMAX; ++i) {
    total += eventCounts[i];
    if (eventEnabled[i])
      switch (style) {
      case '\0':
        printf(" "FLD(5), eventCounts[i]);
        break;
      case 'L':
        printf(" "FLX(0), eventCounts[i]);
        break;
      case 'C': putchar(','); /* fall-through */
      case 'J':
        printf(" "FLD(0), eventCounts[i]);
        break;
      }
  }
  switch (style) {
  case '\0':
    printf(" "FLD(5)"\n", total);
    break;
  case 'L':
    printf(" "FLX(0)")\n", total);
    break;
  case 'C': putchar(','); /* fall-through */
  case 'J':
    printf(" "FLD(0)"\n", total);
    break;
  }
}


/* reportBucketResults -- report results of the current bucket */

static void reportBucketResults(mps_clock_t bucketLimit)
{
  switch (style) {
  case '\0':
    printf(F64D(8)":", bucketLimit);
    break;
  case 'L':
    printf("("F64X(0), bucketLimit);
    break;
  case 'C': case 'J':
    printf(F64D(0), bucketLimit);
    break;
  }
  if (reportEvents) {
    reportEventResults(bucketEventCount);
  }
}


/* clearBucket -- clear bucket */

static void clearBucket(void)
{
  EventCode i;

  for(i = 0; i <= EventCodeMAX; ++i)
    bucketEventCount[i] = 0;
}

/* Indicate if we should print line-by-line information for a particular
   allocation. */
static Bool dumpSize(Word size)
{
  filter_range_t range ;
  SplayNode node ;

  if ( !filtering )
    return TRUE ;

  range.start = range.end = size ;
  return (SplayTreeSearch(&node, &filterRanges, &range) == ResOK) ;
}

/* Dump data selection. This is complex because we use maps to store the
   data, but output in sorted order, and also because the label data command
   can select particular subsets of data to dump. We start by constructing
   a match record, and then iterating over all arenas, pools, and classes
   determining if they match, adding a selection record to a sorted list if
   they do, then printing the list and freeing it. */
typedef struct dump_select_t {
  arena_data_t *arena ;   /* NULL = top-level totals. */
  pool_data_t *pool ;     /* NULL = no pool, dumping arena stats/classes */
  class_data_t *classes ; /* NULL = no classes, just arena/pool totals */
  int indent ;
  struct dump_select_t *next ;
} dump_select_t ;

typedef enum match_type_t {
  MatchNone = 1,     /* Summarise Arenas/Pools/Classes */
  MatchAny = 2,      /* Any Arenas/Pools/Classes */
  MatchAll = MatchNone|MatchAny, /* All and summary */
  MatchAddr = 4,     /* Match just this address for Arena, Pool only. */
  MatchName = 8      /* Match this name for Arena/Pool/Class. */
} match_type_t ;

typedef struct dump_match_t {
  match_type_t arena_match ;
  union {
    void *addr ;
    EventString name ;
  } arena ;
  match_type_t pool_match ;
  union {
    void *addr ;
    EventString name ;
  } pool ;
  match_type_t class_match ;
  EventString class_name ;
  dump_select_t **list ; /* The list we're adding to. */
  dump_select_t current ; /* The current data we're matching. */
} dump_match_t ;


/* dump a selected item */
static void dumpSelected(dump_select_t *select)
{
  dump_select_t old = { 0 } ;

  while ( select != NULL ) {
    arena_data_t *arena = select->arena ;
    pool_data_t *pool = select->pool ;
    class_data_t *classes = select->classes ;
    static const char spaces[16] = "               " ;
    const char *indent = &spaces[sizeof(spaces) - select->indent - 1] ;

    if ( classes != NULL ) {
      /* Dump class data */
      if ( arena != NULL && arena != old.arena ) {
        printf("%sArena "FAX" %.*s\n",
               indent + 2, (Word)arena->addr,
               arena->name ? arena->name->len : 0,
               arena->name ? arena->name->str : "") ;
      }

      if ( pool != NULL && pool != old.pool ) {
        printf("%sPool "FAX" %.*s\n",
               indent + 1, (Word)pool->addr,
               pool->name ? pool->name->len : 0,
               pool->name ? pool->name->str : "") ;
      }

#define NO_CLASS_NAME "not specified"
      printf("%sClass %.*s\n"
             "%s Current  allocated "FWD(10)" count "FWD(10)"\n"
             "%s Peak     allocated "FWD(10)" count "FWD(10)"\n"
             "%s Lifetime allocated "FWD(10)" count "FWD(10)"\n",
             indent,
             classes->name ? classes->name->len : (int)STRSIZE(NO_CLASS_NAME),
             classes->name ? classes->name->str : NO_CLASS_NAME,
             indent, classes->curr_size, classes->curr_count,
             indent, classes->peak_size, classes->peak_count,
             indent, classes->life_size, classes->life_count) ;
    } else if ( pool != NULL ) {
      /* Summarise pool */
      if ( arena != NULL && arena != old.arena ) {
        printf("%sArena "FAX" %.*s\n",
               indent + 1, (Word)arena->addr,
               arena->name ? arena->name->len : 0,
               arena->name ? arena->name->str : "") ;
      }

      printf("%sPool "FAX" %.*s\n"
             "%s Current  allocated "FWD(10)" reserved "FWD(10)" count "FWD(10)"\n"
             "%s Peak     allocated "FWD(10)" reserved "FWD(10)" count "FWD(10)"\n"
             "%s Lifetime allocated "FWD(10)" count    "FWD(10)"\n",
             indent, (Word)pool->addr,
             pool->name ? pool->name->len : 0,
             pool->name ? pool->name->str : "",
             indent, pool->curr_size, pool->curr_resv, pool->curr_count,
             indent, pool->peak_size, pool->peak_resv, pool->peak_count,
             indent, pool->life_size, pool->life_count) ;
    } else if ( arena != NULL ) {
      /* Summarise arena */
      printf("%sArena "FAX" %.*s\n"
             "%s Current  allocated "FWD(10)" reserved "FWD(10)" count "FWD(10)"\n"
             "%s Peak     allocated "FWD(10)" reserved "FWD(10)" count "FWD(10)"\n"
             "%s Lifetime allocated "FWD(10)" count    "FWD(10)"\n",
             indent, (Word)arena->addr,
             arena->name ? arena->name->len : 0,
             arena->name ? arena->name->str : "",
             indent, arena->curr_size, arena->curr_resv, arena->curr_count,
             indent, arena->peak_size, arena->peak_resv, arena->peak_count,
             indent, arena->life_size, arena->life_count) ;
    } else {
      /* Dump overall stats */
      printf("%s Current  allocated "FWD(10)" reserved "FWD(10)" count "FWD(10)"\n"
             "%s Peak     allocated "FWD(10)" reserved "FWD(10)" count "FWD(10)"\n"
             "%s Lifetime allocated "FWD(10)" count    "FWD(10)"\n",
             indent, curr_size, curr_resv, curr_count,
             indent, peak_size, peak_resv, peak_count,
             indent, life_size, life_count) ;
    }

    old = *select ;
    free(select) ;
    select = old.next ;
  }
}

static int compareEventString(EventString a, EventString b)
{
  int result, lendiff ;

  if ( a == NULL ) {
    return b == NULL ? 0 : -1 ;
  } else if ( b == NULL ) {
    return 1 ;
  }

  lendiff = (int)a->len - (int)b->len ;
  if ( (result = memcmp(a->str, b->str, lendiff < 0 ? a->len : b->len)) != 0 )
    return result ;

  return lendiff ;
}

typedef struct select_match_t {
  const dump_match_t *match ;
  dump_select_t **list ;
  dump_select_t current ;
} select_match_t ;

static void addMatch(select_match_t *iter)
{
  dump_select_t *newselect, *curr, **prev ;

  if ( (newselect = malloc(sizeof(*newselect))) == NULL )
    error("Ran out of memory allocating selection") ;

  *newselect = iter->current ;

  /* Insert into order */
  for ( prev = iter->list ; (curr = *prev) != NULL ; prev = &curr->next ) {
    if ( newselect->arena < curr->arena ||
         (newselect->arena == curr->arena &&
          (newselect->pool < curr->pool ||
           (newselect->pool == curr->pool &&
            ((newselect->classes == NULL || curr->classes == NULL)
             ? newselect->classes <= curr->classes
             : compareEventString(newselect->classes->name,
                                  curr->classes->name) <= 0)))) ) {
      /* If identical, don't add the new entry */
      if ( newselect->arena == curr->arena &&
           newselect->pool == curr->pool &&
           newselect->classes == curr->classes ) {
        free(newselect) ;
        return ;
      }
      break ;
    }
  }

  newselect->next = *prev ;
  *prev = newselect ;
}

static void selectClass(Word key, void *value, void *data)
{
  select_match_t *iter = data ;
  class_data_t *classes = value ;

  UNUSED(key) ;

  switch ( iter->match->class_match ) {
  case MatchNone:
  case MatchAddr:
    error("Internal error: invalid match in class select") ;
    return ;
  case MatchAny:
  case MatchAll:
    break ;
  case MatchName:
    if ( classes->name == NULL ||
         compareEventString(iter->match->class_name, classes->name) != 0 )
      return ;
    break ;
  }

  iter->current.classes = classes ;
  addMatch(iter) ;
  iter->current.classes = NULL ;
}

static void selectPool(Word key, void *value, void *data)
{
  select_match_t *iter = data ;
  pool_data_t *pool = value ;

  UNUSED(key) ;

  switch ( iter->match->pool_match ) {
  case MatchNone:
    error("Internal error: invalid match in pool select") ;
    return ;
  case MatchAny:
  case MatchAll:
    break ;
  case MatchAddr:
    if ( iter->match->pool.addr != pool->addr )
      return ;
    break ;
  case MatchName:
    if ( pool->name == NULL ||
         compareEventString(iter->match->pool.name, pool->name) != 0 )
      return ;
    break ;
  }

  iter->current.pool = pool ;
  iter->current.classes = NULL ;

  /* Pool summary */
  if ( (iter->match->class_match & MatchNone) )
    addMatch(iter) ;

  iter->current.indent += 1 ;
  /* All classes in this pool */
  if ( (iter->match->class_match != MatchNone) )
    TableMap(pool->classes, selectClass, iter) ;
  iter->current.indent -= 1 ;

  iter->current.pool = NULL ;
}

static void selectArena(Word key, void *value, void *data)
{
  select_match_t *iter = data ;
  arena_data_t *arena = value ;

  UNUSED(key) ;

  switch ( iter->match->arena_match ) {
  case MatchNone:
    error("Internal error: invalid match in arena select") ;
    return ;
  case MatchAny:
  case MatchAll:
    break ;
  case MatchAddr:
    if ( iter->match->arena.addr != arena->addr )
      return ;
    break ;
  case MatchName:
    if ( arena->name == NULL ||
         compareEventString(iter->match->arena.name, arena->name) != 0 )
      return ;
    break ;
  }

  iter->current.arena = arena ;
  iter->current.pool = NULL ;
  iter->current.classes = NULL ;

  /* Arena summary */
  if ( (iter->match->pool_match & MatchNone) &&
       (iter->match->class_match & MatchNone) )
    addMatch(iter) ;

  iter->current.indent += 1 ;
  /* All pools in arena */
  if ( iter->match->pool_match != MatchNone )
    TableMap(arena->pools, selectPool, iter) ;
  /* All classes in this arena */
  if ( (iter->match->pool_match & MatchNone) &&
       iter->match->class_match != MatchNone )
    TableMap(arena->classes, selectClass, iter) ;
  iter->current.indent -= 1 ;

  iter->current.arena = NULL ;
}


/* Add matching items to a selection of stuff to print. */
static void selectMatch(const dump_match_t *match, dump_select_t **list,
                        int indent)
{
  select_match_t iter = { 0 } ;

  iter.match = match ;
  iter.list = list ;
  iter.current.indent = indent ;

  /* global summary? */
  if ( (match->arena_match & MatchNone) &&
       (match->pool_match & MatchNone) &&
       (match->class_match & MatchNone) )
    addMatch(&iter) ;

  iter.current.indent += 1 ;
  /* forall arenas */
  if ( match->arena_match != MatchNone )
    TableMap(all_arenas, selectArena, &iter) ;
  /* forall pools (in all arenas) */
  if ( (match->arena_match & MatchNone) &&
       match->pool_match != MatchNone )
    TableMap(all_pools, selectPool, &iter) ;
  /* forall classes (summarised across all arenas and pools) */
  if ( (match->arena_match & MatchNone) &&
       (match->pool_match & MatchNone) &&
       match->class_match != MatchNone )
    TableMap(all_classes, selectClass, &iter) ;
  iter.current.indent -= 1 ;
}

/* dump all arenas */
static void dumpAll(void)
{
  dump_match_t match = { 0 } ;
  dump_select_t *selected = NULL ;

  match.arena_match = MatchAll ;
  match.pool_match = MatchAll ;
  match.class_match = MatchAll ;

  selectMatch(&match, &selected, 0) ;

  dumpSelected(selected) ;
}

/* parse and dump according to a specification. Dump specifications allow
   individual arenas, pools, classes to be summarised. The spec parsing will
   update specp to point at the first character that is not recognised as
   part of a dump spec. Dump spec grammar is:

   eventcnv:list

   list = spec
        | spec,list
        | spec|list
   spec = label=text
        | total
        | all
        | arena=asel
        | [arena=asel&]?pool=asel
        | [arena=asel&]?[pool=asel&]?class=nsel
   asel = addr
        | name
        | *
        | +
   nsel = name
        | *
        | +

   Some examples:

     eventcnv:out=Loc1,all - dumps everything, with all summaries
     eventcnv:out=Loc2,class=* - dumps all class totals across all arenas
     eventcnv:arena=+&pool=DL_POOL_TYPE&class=+
             - dumps just classes in DL pools in all arenas
     eventcnv:total|pool=0xa4bd0103+&class=*
             - dump and summarise classes in a particular pool

   etc.
*/
static EventString getName(EventProc proc, char **specp)
{
  char *spec = *specp ;
  EventStringStruct item = { 0 } ;

  for (;;) {
    char ch = *spec ;

    switch (ch) {
      Word id ;
    case '&':
    case ',':
    case '|':
    case '\0':
      if ( item.len == 0 )
        return NULL ;
      /* Need to intern name using EventProc internTable. */
      id = TextLabel(proc, &item) ;
      if ( id == 0 ) {
        printf("eventcnv name not found") ;
        return NULL ;
      }
      *specp = spec ;
      return LabelText(proc, id) ;
    default:
      item.str[item.len++] = ch ;
      break ;
    }

    ++spec ;
  }
}

static Bool dumpSpec(EventProc proc, EventString str)
{
  char buffer[EventStringLengthMAX + 1] ;
#define NOLABEL "Unlabelled"
  char *spec, *selstart, *label = NOLABEL ;
  int labellen = (int)STRSIZE(NOLABEL) ;
  dump_select_t *selected = NULL ;

#define DUMP "eventcnv:"
  if ( str->len < STRSIZE(DUMP) ||
       strncmp(str->str, DUMP, STRSIZE(DUMP)) != 0 )
    return TRUE ;

  /* NUL-termination makes this all much easier. */
  (void)memcpy(buffer, str->str, str->len);
  buffer[str->len] = '\0' ;

  /* Parse comma-separated list of specs. */
  for ( spec = selstart = &buffer[STRSIZE(DUMP)] ;; ++spec ) {
    const char *start = spec ;
    dump_match_t match = { 0 } ;

#define SPEC_OUT "out="
    if ( strncmp(spec, SPEC_OUT, STRSIZE(SPEC_OUT)) == 0 ) {
      spec += STRSIZE(SPEC_OUT) ;
      label = spec ;
      while ( *spec != '\0' && *spec != ',' )
        ++spec ;
      labellen = (int)(spec - label) ;
      if ( *spec != ',' )
        break ;
      start = selstart = ++spec ;
    }

    match.arena_match = MatchNone ;
    match.pool_match = MatchNone ;
    match.class_match = MatchNone ;

#define SPEC_ARENA "arena="
    if ( strncmp(spec, SPEC_ARENA, STRSIZE(SPEC_ARENA)) == 0 ) {
      spec += STRSIZE(SPEC_ARENA) ;
      if ( *spec == '*' ) {
        ++spec ;
        match.arena_match = MatchAll ;
      } else if ( *spec == '+' ) {
        ++spec ;
        match.arena_match = MatchAny ;
      } else if ( (match.arena.addr = (void *)STRTOUL(spec, &spec, 0)) != NULL ) {
        match.arena_match = MatchAddr ;
      } else if ( (match.arena.name = getName(proc, &spec)) != NULL ) {
        match.arena_match = MatchName ;
      } else {
        return FALSE ;
      }
      if ( *spec != '&' )
        goto ready ;
      ++spec ;
    }

#define SPEC_POOL "pool="
    if ( strncmp(spec, SPEC_POOL, STRSIZE(SPEC_POOL)) == 0 ) {
      spec += STRSIZE(SPEC_POOL) ;
      if ( *spec == '*' ) {
        ++spec ;
        match.pool_match = MatchAll ;
      } else if ( *spec == '+' ) {
        ++spec ;
        match.pool_match = MatchAny ;
      } else if ( (match.pool.addr = (void *)STRTOUL(spec, &spec, 0)) != NULL ) {
        match.pool_match = MatchAddr ;
      } else if ( (match.pool.name = getName(proc, &spec)) != NULL ) {
        match.pool_match = MatchName ;
      } else {
        return FALSE ;
      }
      if ( *spec != '&' )
        goto ready ;
      ++spec ;
    }

#define SPEC_CLASS "class="
    if ( strncmp(spec, SPEC_CLASS, STRSIZE(SPEC_CLASS)) == 0 ) {
      spec += STRSIZE(SPEC_CLASS) ;
      if ( *spec == '*' ) {
        ++spec ;
        match.class_match = MatchAll ;
      } else if ( *spec == '+' ) {
        ++spec ;
        match.class_match = MatchAny ;
      } else if ( (match.class_name = getName(proc, &spec)) != NULL ) {
        match.class_match = MatchName ;
      } else {
        return FALSE ;
      }
    }

    if ( spec == start ) {
#define SPEC_TOTAL "total"
      if ( strncmp(spec, SPEC_TOTAL, STRSIZE(SPEC_TOTAL)) == 0 ) {
        spec += STRSIZE(SPEC_TOTAL) ;
#define SPEC_ALL "all"
      } else if ( strncmp(spec, SPEC_ALL, STRSIZE(SPEC_ALL)) == 0 ) {
        spec += STRSIZE(SPEC_ALL) ;
        match.arena_match = MatchAll ;
        match.pool_match = MatchAll ;
        match.class_match = MatchAll ;
      } else
        return FALSE ;
    }

  ready:
    selectMatch(&match, &selected, 1) ;
    if ( *spec != '|' ) { /* Add some more to this before dumping. */
      printf("%.*s: %.*s\n", labellen, label, (int)(spec - selstart), selstart) ;
      dumpSelected(selected) ;
      if ( *spec != ',' )
        break ;
      selstart = spec + 1 ;
    }
  }

  return *spec == 0 ;
}

/* Arena, pool, and allocation tracking. */
static void endPoolClass(Word key, void *value, void *data)
{
  arena_data_t *arena = data ;
  class_data_t *class = value, *next ;

  UNUSED(key) ;

  for ( next = class->parent ; next ; next = next->parent ) {
    next->curr_size -= class->curr_size ;
    next->curr_count -= class->curr_count ;
  }
  arena->curr_size -= class->curr_size ;
  arena->curr_count -= class->curr_count ;
  curr_size -= class->curr_size ;
  curr_count -= class->curr_count ;
  free(class) ;
}

static void newPool(void *p, void *a)
{
  void *found ;
  if ( TableLookup(&found, all_arenas, (Word)a) ) {
    arena_data_t *arena = found ;
    pool_data_t *pool ;
    Res res ;

    if ( (pool = malloc(sizeof(*pool))) == NULL )
      error("Ran out of memory allocating pool "FAX" in arena "FAX, (Word)p, (Word)a) ;

    pool->addr = p ;
    pool->name = NULL ;
    pool->arena = arena ;

    if ( (res = TableCreate(&pool->classes, 20)) != ResOK)
      error("Can't init pool classes: error %d.", res);

    SplayTreeInit(&pool->allocs, allocCompare, NULL) ;

    pool->curr_size = pool->curr_resv = pool->curr_count = 0 ;
    pool->peak_size = pool->peak_resv = pool->peak_count = 0 ;
    pool->life_size = pool->life_count = 0 ;

    if ( (res = TableDefine(all_pools, (Word)p, pool)) != ResOK ||
         (res = TableDefine(arena->pools, (Word)p, pool)) != ResOK )
      error("Can't insert new pool into table: error %d.", res);
  }
}

static void endPool(void *p)
{
  void *found ;
  if ( TableLookup(&found, all_pools, (Word)p) ) {
    pool_data_t *pool = found ;
    SplayNode node ;

    if ( track & DUMP_POOL_END ) {
      dump_match_t match = { 0 } ;
      dump_select_t *selected = NULL ;

      match.arena_match = MatchNone ;
      match.pool_match = MatchAddr ;
      match.pool.addr = p ;
      match.class_match = MatchAll ;

      if ( !verbose )
        puts("PoolFinish") ;

      selectMatch(&match, &selected, 0) ;
      dumpSelected(selected) ;
    }

    TableRemove(all_pools, (Word)p) ;
    TableRemove(pool->arena->pools, (Word)p) ;

    while ( (node = SplayTreeFirst(&pool->allocs)) != NULL ) {
      alloc_node_t *record = Splay2Alloc(node) ;
      alloc_range_t *alloc = &record->alloc ;
      Res res ;

      if ( (res = SplayTreeDelete(&pool->allocs, node, alloc)) != ResOK )
        error("Failed to delete address record: error %d.", res) ;
      SplayNodeFinish(node) ;
      free(record) ;
    }
    SplayTreeFinish(&pool->allocs) ;

    TableMap(pool->classes, endPoolClass, pool->arena) ;
    TableDestroy(pool->classes) ;

    free(pool) ;
  }
}

static void newArena(void *a)
{
  arena_data_t *arena ;
  Res res ;

  if ( (arena = malloc(sizeof(*arena))) == NULL )
    error("Ran out of memory allocating arena "FAX, (Word)a) ;

  arena->addr = a ;
  arena->name = NULL ;

  if ( (res = TableCreate(&arena->pools, 20)) != ResOK ||
       (res = TableCreate(&arena->classes, 20)) != ResOK )
    error("Can't init arena table: error %d.", res);

  arena->curr_size = arena->curr_resv = arena->curr_count = 0 ;
  arena->peak_size = arena->peak_resv = arena->peak_count = 0 ;
  arena->life_size = arena->life_count = 0 ;

  if ( (res = TableDefine(all_arenas, (Word)a, arena)) != ResOK )
    error("Can't insert new arena into table: error %d.", res);
}

static void endArenaPool(Word key, void *value, void *data)
{
  pool_data_t *pool = value ;
  UNUSED(key) ;
  UNUSED(data) ;
  endPool(pool->addr) ;
}

static void endArenaClass(Word key, void *value, void *data)
{
  class_data_t *class = value, *next ;

  UNUSED(key) ;
  UNUSED(data) ;

  for ( next = class->parent ; next ; next = next->parent ) {
    next->curr_size -= class->curr_size ;
    next->curr_count -= class->curr_count ;
  }
  curr_size -= class->curr_size ;
  curr_count -= class->curr_count ;
  free(class) ;
}

static void endArena(void *a)
{
  void *found ;

  if ( TableLookup(&found, all_arenas, (Word)a) ) {
    arena_data_t *arena = found ;

    if ( track & DUMP_ARENA_END ) {
      dump_match_t match ;
      dump_select_t *selected = NULL ;

      match.arena_match = MatchAddr ;
      match.arena.addr = a ;
      match.pool_match = MatchAll ;
      match.class_match = MatchAll ;

      if ( !verbose )
        puts("ArenaDestroy") ;

      selectMatch(&match, &selected, 0) ;
      dumpSelected(selected) ;
    }

    TableRemove(all_arenas, (Word)a) ;

    TableMap(arena->pools, endArenaPool, arena) ;
    TableDestroy(arena->pools) ;

    TableMap(arena->classes, endArenaClass, arena) ;
    TableDestroy(arena->classes) ;

    free(arena) ;
  }
}

static void printArena(arena_data_t *arena)
{
  printf(" "FAX, (Word)arena) ;
  if ( arena->name )
    printf("<%.*s>", arena->name->len, arena->name->str) ;
  printf(" AT="FWD(0)" AR="FWD(0)" AC="FWD(0)" AP="FWD(0),
         arena->curr_size, arena->curr_resv,
         arena->curr_count, arena->peak_resv) ;
}

static void printPool(pool_data_t *pool)
{
  printf(" "FAX, (Word)pool) ;
  if ( pool->name )
    printf("<%.*s>", pool->name->len, pool->name->str) ;
  printf(" PT="FWD(0)" PR="FWD(0)" PC="FWD(0)" PP="FWD(0),
         pool->curr_size, pool->curr_resv,
         pool->curr_count, pool->peak_resv) ;
}

static void printTotal(void)
{
  printf(" T="FWD(0)" R="FWD(0)" C="FWD(0)" P="FWD(0),
         curr_size, curr_resv, curr_count, peak_resv) ;
}

static void printClock(void)
{
  if ( track & DUMP_CLOCK )
    printf("%.7f ", (double)eventTime / (double)clocks_per_second) ;
}

static void reserve_or_release(pool_data_t *pool, intptr_t size)
{
  arena_data_t *arena = pool->arena ;
  pool->curr_resv += size ;
  if ( pool->curr_resv > pool->peak_resv )
    pool->peak_resv = pool->curr_resv ;
  arena->curr_resv += size ;
  if ( arena->curr_resv > arena->peak_resv )
    arena->peak_resv = arena->curr_resv ;
  curr_resv += size ;
  if ( curr_resv > peak_resv )
    peak_resv = curr_resv ;
}

static void trackReserve(void *p, Word size)
{
  void *found ;
  if ( TableLookup(&found, all_pools, (Word)p) ) {
    reserve_or_release(found, (intptr_t)size) ;
    if ( (track & DUMP_ARENA_ALLOC) != 0 && dumpSize(size) ) {
      printClock() ;
      printf("AA "FWD(0), size) ;
      printPool(found) ;
      printTotal() ;
      putchar('\n') ;
    }
  }
}

static void trackRelease(void *p, Word size)
{
  void *found ;
  if ( TableLookup(&found, all_pools, (Word)p) ) {
    reserve_or_release(found, -(intptr_t)size) ;
    if ( (track & DUMP_ARENA_ALLOC) != 0 && dumpSize(size) ) {
      printClock() ;
      printf("AF "FWD(0), size) ;
      printPool(found) ;
      printTotal() ;
      putchar('\n') ;
    }
  }
}

static class_data_t *newClass(Table table, EventString name, class_data_t *parent)
{
  class_data_t *ac ;
  Res res ;

  if ( (ac = malloc(sizeof(*ac))) == NULL )
    error("Ran out of memory allocating class data") ;

  ac->name = name ;
  ac->parent = parent ;
  ac->curr_size = ac->curr_count = 0 ;
  ac->peak_size = ac->peak_count = 0 ;
  ac->life_size = ac->life_count = 0 ;

  if ( (res = TableDefine(table, (Word)name, ac)) != ResOK )
    error("Failed table insertion: error %d.", res);

  return ac;
}

static void trackClassAlloc(class_data_t *cd, size_t size)
{
  while ( cd ) {
    cd->curr_size += size ;
    if ( cd->curr_size > cd->peak_size )
      cd->peak_size = cd->curr_size ;

    ++cd->curr_count ;
    if ( cd->curr_count > cd->peak_count )
      cd->peak_count = cd->curr_count ;

    cd->life_size += size ;
    ++cd->life_count ;

    cd = cd->parent ;
  }
}

static void trackAlloc(void *p, void *addr, Word size,
                       EventString type, EventString locn)
{
  void *found ;
  if ( TableLookup(&found, all_pools, (Word)p) ) {
    pool_data_t *pool = found ;
    arena_data_t *arena = pool->arena ;
    alloc_node_t *record ;
    Res res ;

    ++pool->curr_count ;
    if ( pool->curr_count > pool->peak_count )
      pool->peak_count = pool->curr_count ;
    ++arena->curr_count ;
    if ( arena->curr_count > arena->peak_count )
      arena->peak_count = arena->curr_count ;
    ++curr_count ;
    if ( curr_count > peak_count )
      peak_count = curr_count ;

    ++pool->life_count ;
    ++arena->life_count ;
    ++life_count ;

    pool->curr_size += size ;
    if ( pool->curr_size > pool->peak_size )
      pool->peak_size = pool->curr_size ;
    arena->curr_size += size ;
    if ( arena->curr_size > arena->peak_size )
      arena->peak_size = arena->curr_size ;
    curr_size += size ;
    if ( curr_size > peak_size )
      peak_size = curr_size ;

    pool->life_size += size ;
    arena->life_size += size ;
    life_size += size ;

    /* Find/create class entries */
    if ( !TableLookup(&found, pool->classes, (Word)type) ) {
      if ( !TableLookup(&found, arena->classes, (Word)type) ) {
        if ( !TableLookup(&found, all_classes, (Word)type) )
          found = newClass(all_classes, type, NULL) ;
        found = newClass(arena->classes, type, found) ;
      }
      found = newClass(pool->classes, type, found) ;
    }

    trackClassAlloc(found, size) ;

    if ( (record = malloc(sizeof(*record))) == NULL )
      error("Ran out of memory allocating address record") ;
    SplayNodeInit(&record->splayNode) ;
    record->alloc.addr = (Word)addr ;
    record->alloc.size = size ;
    record->type = found ;

    if ( (res = SplayTreeInsert(&pool->allocs, &record->splayNode, &record->alloc)) != ResOK )
      error("Failed to insert address record: error %d.", res) ;

    if ( (track & DUMP_POOL_ALLOC) != 0 && dumpSize(size) ) {
      printClock() ;
      printf("PA "FWD(0), size) ;
      printPool(pool) ;
      printTotal() ;
      if ( type != NULL )
        printf(" %.*s", type->len, type->str) ;
      if ( locn != NULL )
        printf(" %.*s", locn->len, locn->str) ;
      putchar('\n') ;
    }
  }
}

static void trackClassFree(class_data_t *cd, size_t size, Bool count)
{
  while ( cd ) {
    cd->curr_size -= size ;

    if ( count )
      --cd->curr_count ;

    cd = cd->parent ;
  }
}

static void trackFree(void *p, void *addr, Word size)
{
  void *found ;
  if ( TableLookup(&found, all_pools, (Word)p) ) {
    pool_data_t *pool = found ;
    arena_data_t *arena = pool->arena ;
    class_data_t *type = NULL ;
    alloc_range_t range ;
    Word range_end ;
    SplayNode node ;
    Res res ;

    /* Find alloc in splay tree. */
    range.addr = (Word)addr ;
    range.size = size ;
    range_end = range.addr + range.size ;
    while ( (res = SplayTreeSearch(&node, &pool->allocs, &range)) == ResOK ) {
      alloc_node_t *record = Splay2Alloc(node) ;
      alloc_range_t *alloc = &record->alloc ;
      Word alloc_end = alloc->addr + alloc->size ;
      assert(range.addr < alloc_end && range_end > alloc->addr) ;
      assert(range.addr >= alloc->addr && range_end <= alloc_end) ;
      type = record->type ;
      if ( range.addr <= alloc->addr && range_end >= alloc_end ) {
        /* Delete entire record */
        size = alloc->size ;
        trackClassFree(record->type, size, TRUE) ;
        if ( (res = SplayTreeDelete(&pool->allocs, node, alloc)) != ResOK )
          error("Failed to delete address record: error %d.", res) ;
        SplayNodeFinish(node) ;
        free(record) ;

        --pool->curr_count ;
        --arena->curr_count ;
        --curr_count ;
      } else if ( range.addr > alloc->addr ) {
        /* Trim back end */
        size = alloc_end - range.addr ;
        trackClassFree(record->type, size, FALSE) ;
        alloc->size -= size ;

        /* Did we free a hole in an alloc? */
        if ( range_end < alloc_end ) {
          if ( (record = malloc(sizeof(*record))) == NULL )
            error("Ran out of memory allocating address record") ;
          SplayNodeInit(&record->splayNode) ;
          record->alloc.addr = range_end ;
          record->alloc.size = alloc_end - range_end ;
          record->type = type ;
          if ( (res = SplayTreeInsert(&pool->allocs, &record->splayNode, &record->alloc)) != ResOK )
            error("Failed to insert address record: error %d.", res) ;
        }
      } else if ( range_end > alloc->addr ) {
        /* Trim front end */
        size = range_end - alloc->addr ;
        trackClassFree(record->type, size, FALSE) ;
        alloc->addr += size ;
        alloc->size -= size ;
      }

      pool->curr_size -= size ;
      arena->curr_size -= size ;
      curr_size -= size ;
    }

    if ( (track & DUMP_POOL_ALLOC) != 0 && dumpSize(size) ) {
      printClock() ;
      printf("PF "FWD(0), size) ;
      printPool(pool) ;
      printTotal() ;
      putchar('\n') ;
    }
  }
}

static void trackCommit(void *a, Word size)
{
  if ( track & DUMP_COMMIT ) {
    void *found ;
    if ( TableLookup(&found, all_arenas, (Word)a) ) {
      printClock() ;
      printf("CS "FWD(0), size) ;
      printArena(found) ;
      printTotal() ;
      putchar('\n') ;
    }
  }
}

/* readLog -- read and parse log
 *
 * This is the heart of eventcnv: It reads an event log using EventRead.
 * It updates the counters.  If verbose is true, it looks up the format,
 * parses the arguments, and prints a representation of the event.  Each
 * argument is printed using printArg (see RELATION, below), except for
 * some event types that are handled specially.
 */

static void printArg(EventProc proc,
                     void *arg, char argType, char *styleConv)
{
  switch (argType) {
  case 'A': {
    switch (style) {
    case 'C': putchar(','); /* fall-through */
    case '\0': {
      printAddr(proc, *(Addr *)arg);
    } break;
    case 'L': case 'J': {
      printf(styleConv, (Word)*(Addr *)arg);
    } break;
    }
  } break;
  case 'P': {
    printf(styleConv, (Word)*(void **)arg);
  } break;
  case 'U': {
    printf(styleConv, (Word)*(unsigned *)arg);
  } break;
  case 'W': {
    printf(styleConv, *(Word *)arg);
  } break;
  case 'D': {
    switch (style) {
    case '\0':
      printf(" %#8.3g", *(double *)arg); break;
    case 'C':
      printf(", %.10G", *(double *)arg); break;
    case 'L': case 'J':
      printf(" %#.10G", *(double *)arg); break;
    }
  } break;
  case 'S': {
    if (style == 'C') putchar(',');
    putchar(' ');
    printStr((EventStringStruct *)arg, style != '\0');
  } break;
  default: error("Can't print format >%c<", argType);
  }
}


#define RELATION(name, code, always, kind, format) \
  case code: { \
    printArg(proc, EVENT_##format##_FIELD_PTR(event, i), \
             eventFormat[i], styleConv); \
  } break;


static void readLog(EventProc proc)
{
  EventCode c;
  mps_clock_t bucketLimit = bucketSize;
  char *styleConv = NULL; /* suppress uninit warning */

  /* Print event count header. */
  if (reportEvents) {
    if (style == '\0') {
      printf("  bucket:");
      for(c = 0; c <= EventCodeMAX; ++c)
        if (eventEnabled[c])
          printf("  %04X", (unsigned)c);
      printf("   all\n");
    }
  }

  /* Init event counts. */
  for(c = 0; c <= EventCodeMAX; ++c)
    totalEventCount[c] = 0;
  clearBucket();

  /* Init style. */
  switch (style) {
  case '\0':
    styleConv = " "FWX(HDW); break;
  case 'C':
    styleConv = ", "FWD(0); break;
  case 'L':
    styleConv = " "FWX(0); break;
  case 'J':
    styleConv = " "FWD(0); break;
  default:
    error("Unknown style code '%c'", style);
  }

  for (;;) { /* loop for each event */
    char *eventFormat;
    size_t argCount, i;
    Event event;
    EventCode code;
    Res res;

    /* Read and parse event. */
    res = EventRead(&code, &eventTime, &event, proc);
    if (res == ResFAIL) break; /* eof */
    if (res != ResOK) error("Truncated log");

    /* Output bucket, if necessary, and update counters */
    if (bucketSize != 0 && eventTime >= bucketLimit) {
      reportBucketResults(bucketLimit-1);
      clearBucket();
      do {
        bucketLimit += bucketSize;
      } while (eventTime >= bucketLimit);
    }
    if (reportEvents) {
      ++bucketEventCount[code];
      ++totalEventCount[code];
    }

    /* Output event. */
    if (verbose) {
      eventFormat = EventCode2Format(code);
      argCount = strlen(eventFormat);
      if (eventFormat[0] == '0') argCount = 0;

      if (style == 'L') putchar('(');

      switch (style) {
      case '\0': case 'L': case 'J': {
        printf("%-19s", EventCode2Name(code));
      } break;
      case 'C':
        printf("%u", (unsigned)code);
        break;
      }

     switch (style) {
     case '\0':
       printf(" "F64D(8), eventTime); break;
     case 'C':
       printf(", "F64D(0), eventTime); break;
     case 'L':
       printf(" "F64X(0), eventTime); break;
     case 'J':
       printf(" "F64D(0), eventTime); break;
     }

     switch (code) {
     case EventCodeLabel: {
       switch (style) {
       case '\0': case 'C': {
         EventString sym = LabelText(proc, event->aw.w1);
         printf((style == '\0') ? " "FAX" " : ", "FWD(0)", ",
                (Word)event->aw.a0);
         if (sym != NULL) {
           printStr(sym, (style == 'C'));
         } else {
           printf((style == '\0') ? "sym "FWX(05) : "\"sym "FWX(0)"\"",
                  event->aw.w1);
         }
       } break;
       case 'L': {
         printf(" "FWX(0)" "FWX(0), (Word)event->aw.a0, event->aw.w1);
       } break;
       case 'J': {
         printf(" "FWD(0)" "FWD(0), (Word)event->aw.a0, event->aw.w1);
       } break;
       }
     } break;
     case EventCodeMeterValues: {
       switch (style) {
       case '\0': {
         if (event->pddwww.w3 == 0) {
           printf(" "FAX"        0      N/A      N/A      N/A      N/A",
                  (Word)event->pddwww.p0);
         } else {
           double mean = event->pddwww.d1 / (double)event->pddwww.w3;
           /* .stddev: stddev = sqrt(meanSquared - mean^2), but see */
           /* impl.c.meter.limitation.variance. */
           double stddev = sqrt(fabs(event->pddwww.d2 - (mean * mean)));
           printf(" "FAX" "FWD(HDW)" "FWD(HDW)" "FWD(HDW)" %#8.3g %#8.3g",
                  (Word)event->pddwww.p0, event->pddwww.w3,
                  event->pddwww.w4, event->pddwww.w5,
                  mean, stddev);
         }
         printAddr(proc, (Addr)event->pddwww.p0);
       } break;
       case 'C': {
         putchar(',');
         printAddr(proc, (Addr)event->pddwww.p0);
         printf(", %.10G, %.10G, "FWD(0)", "FWD(0)", "FWD(0),
                event->pddwww.d1, event->pddwww.d2,
                event->pddwww.w3, event->pddwww.w4, event->pddwww.w5);
       } break;
       case 'L': {
         printf(" "FWX(0)" %#.10G %#.10G "FWX(0)" "FWX(0)" "FWX(0),
                (Word)event->pddwww.p0, event->pddwww.d1, event->pddwww.d2,
                event->pddwww.w3, event->pddwww.w4, event->pddwww.w5);
       } break;
       case 'J': {
         printf(" "FWD(0)" %#.10G %#.10G "FWD(0)" "FWD(0)" "FWD(0),
                (Word)event->pddwww.p0, event->pddwww.d1, event->pddwww.d2,
                event->pddwww.w3, event->pddwww.w4, event->pddwww.w5);
       } break;
       }
     } break;
     case EventCodePoolInit: { /* pool, arena, class */
       printf(styleConv, (Word)event->ppp.p0);
       printf(styleConv, (Word)event->ppp.p1);
       /* class is a Pointer, but we label them, so call printAddr */
       switch (style) {
       case 'C': putchar(','); /* fall-through */
       case '\0': {
         printAddr(proc, (Addr)event->ppp.p2);
       } break;
       case 'L': case 'J': {
         printf(styleConv, (Word)event->ppp.p2);
       } break;
       }
     } break;
     default:
       for (i = 0; i < argCount; ++i) {
         switch(code) {
#include "eventdef.h"
#undef RELATION
         }
       }
     }

      if (style == 'L') putchar(')');
      putchar('\n');
      fflush(stdout);
    }
    processEvent(proc, code, event);
    EventDestroy(proc, event);
  } /* while(!feof(input)) */

  /* report last bucket (partial) */
  if (bucketSize != 0) {
    reportBucketResults(eventTime);
  }
  if (reportEvents) {
    /* report totals */
    switch (style) {
    case '\0': {
      printf("\n     run:");
    } break;
    case 'L': {
      printf("(t");
    } break;
    case 'C': {
      printf(F64D(0), eventTime+1);
    } break;
    }
    if (style != 'J')
      reportEventResults(totalEventCount);

    /* explain event codes */
    if (style == '\0') {
      printf("\n");
      for(c = 0; c <= EventCodeMAX; ++c)
        if (eventEnabled[c])
          printf(" %04X %s\n", (unsigned)c, EventCode2Name(c));
      if (bucketSize == 0)
        printf("\nevent clock stopped at "F64D(0)"\n", eventTime);
    }
  }
}


/* logReader -- reader function for a file log */

static FILE *input;

static Res logReader(void *file, void *p, size_t len)
{
  size_t n;

  n = fread(p, 1, len, (FILE *)file);
  return (n < len) ? (feof((FILE *)file) ? ResFAIL : ResIO) : ResOK;
}


/* CHECKCONV -- check t2 can be cast to t1 without loss */

#define CHECKCONV(t1, t2) \
  (sizeof(t1) >= sizeof(t2))


/* main */

int main(int argc, char *argv[])
{
  char *filename;
  EventProc proc;
  Res res;

#if !defined(MPS_OS_FR)
  /* GCC -ansi -pedantic -Werror on FreeBSD will fail here
   * with the warning "statement with no effect". */

  assert(CHECKCONV(ulongest, Word));
  assert(CHECKCONV(Word, void *));
  assert(EventCodeMAX <= UINT_MAX);
  assert(CHECKCONV(Addr, void *)); /* for labelled pointers */
#endif

  SplayTreeInit(&filterRanges, filterCompare, NULL) ;

  filename = parseArgs(argc, argv);

  if (strcmp(filename, "-") == 0)
    input = stdin;
  else {
    input = fopen(filename, "rb");
    if (input == NULL)
      error("unable to open \"%s\"\n", filename);
  }

  if ( (res = TableCreate(&all_arenas, 1)) != ResOK ||
       (res = TableCreate(&all_pools, 50)) != ResOK ||
       (res = TableCreate(&all_classes, 100)) != ResOK )
    error("Can't init arena/pool/class table: error %d.", res);

  res = EventProcCreate(&proc, partialLog, logReader, (void *)input);
  if (res != ResOK)
    error("Can't init EventProc module: error %d.", res);

  readLog(proc);

  if ( track & DUMP_LOG_END )
    dumpAll() ;

  EventProcDestroy(proc);

  TableDestroy(all_classes) ;
  TableDestroy(all_pools) ;
  TableDestroy(all_arenas) ;

  SplayTreeFinish(&filterRanges) ;

  return EXIT_SUCCESS;
}
