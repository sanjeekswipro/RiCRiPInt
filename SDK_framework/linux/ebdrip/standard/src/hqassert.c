/** \file
 * \ingroup assertions
 *
 * $HopeName: HQNc-standard!src:hqassert.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Harlequin standard "assert" and "trace" support routines.
 */

/* ----------------------- Includes ---------------------------------------- */

#include "std.h"        /* std.h automatically includes hqassert.h */
#ifdef HQASSERT_LOCAL_FILE
HQASSERT_FILE();
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "hqcstass.h"
#include "hqspin.h"

#ifdef ASSERT_BUILD

/* ----------------------- Types ------------------------------------------- */

#define HQ_MAX_ASSERTS 256
typedef struct assert_history {
  const char *pszFilename;
  int nLine;
  const char *pszMessage;
} HQ_ASSERT_HISTORY ;


/* ----------------------- Data -------------------------------------------- */

/* This variable deliberately extern, even though not used elsewhere, to
   prevent it being optimised out and warnings being produced. */
int                             hqassert_false = FALSE;
static hq_atomic_counter_t      hq_assert_depth = 0;

/* The filename and line saved should only be accessed while
   hq_assert_spinlock is held. */
static const char *             pszFilenameSaved = NULL;
static int                      nLineSaved = -1;

HqBool                          hq_assert_history = TRUE;
static int                      hq_assert_history_count = 0 ;
static HQ_ASSERT_HISTORY        hq_assert_history_table[ HQ_MAX_ASSERTS ] =
                                 { { NULL, 0, NULL } };
static hq_atomic_counter_t      hq_assert_spinlock = 0 ;

static void HQNCALL DefaultCustomAssert(const char *pszFilename, int nLine,
                                        const char *pszFormat, va_list vlist,
                                        int assertflag) ;
static void HQNCALL DefaultCustomTrace(const char *pszFilename, int nLine,
                                       const char *pszFormat, va_list vlist) ;

static HqCustomAssert_fn *pfnHqCustomAssert = DefaultCustomAssert ;
static HqCustomTrace_fn  *pfnHqCustomTrace = DefaultCustomTrace ;

/* ----------------------- Functions --------------------------------------- */

static int HqRememberAssert(const char *pszFilename, int nLine, const char *pszMessage)
{
  int i ;
  int moveupto ;
  HqBool found_assert ;

  moveupto = hq_assert_history_count ;
  if ( moveupto == HQ_MAX_ASSERTS )
    --moveupto ;

  /* Check if assert has been shown already, and ignore if so... */
  found_assert = FALSE ;
  for ( i = 0 ; i < hq_assert_history_count ; ++i )
    if ( hq_assert_history_table[ i ].pszFilename == pszFilename &&
	 hq_assert_history_table[ i ].nLine       == nLine
         /* Don't match on msg pointer, so that mps_lib_assert_fail()
          * can use a local buffer to reprocess it. */
         ) {
      /* Ignore this assert. */
      found_assert = TRUE ;
      /* We want to move this one to the head of the q. */
      moveupto = i ;
      /* Decrement count as we essentially add it in at the head. */
      --hq_assert_history_count ;
      break ;
    }

  /* Move everything up one so we can insert the new assert at the head of the q. */
  for ( i = moveupto ; i > 0 ; --i ) {
    hq_assert_history_table[ i ].pszFilename = hq_assert_history_table[ i - 1 ].pszFilename ;
    hq_assert_history_table[ i ].nLine       = hq_assert_history_table[ i - 1 ].nLine ;
    hq_assert_history_table[ i ].pszMessage  = hq_assert_history_table[ i - 1 ].pszMessage ;
  }

  /* Insert the new assert at the head of the q. */
  hq_assert_history_table[ 0 ].pszFilename = pszFilename ;
  hq_assert_history_table[ 0 ].nLine       = nLine ;
  hq_assert_history_table[ 0 ].pszMessage  = pszMessage ;

  ++hq_assert_history_count ;
  if ( hq_assert_history_count > HQ_MAX_ASSERTS )
    hq_assert_history_count = HQ_MAX_ASSERTS ;

  /* If we found it already, then don't bother reporting it again. */
  return ( found_assert ) ;
}

int HQNCALL HqAssertFalse(void)
{
  return hqassert_false ;
}

void HQNCALL HqAssert(const char *pszFormat, ...)
{
  const char *filename = pszFilenameSaved ;
  int line = nLineSaved ;
  va_list vlist;
  hq_atomic_counter_t dummy;

  pszFilenameSaved = NULL ;
  nLineSaved = -1 ;

  if ( hq_assert_history &&
       HqRememberAssert(filename, line, pszFormat)) {
    spinunlock_counter(&hq_assert_spinlock);
    return;
  }

  spinunlock_counter(&hq_assert_spinlock) ;

  va_start(vlist, pszFormat);

  HqAtomicIncrement(&hq_assert_depth, dummy);
  /* With the tools available, this code can't tell the difference between
     recursive and simultaneous asserts, so allow them, and only break out if
     there are more simultaneous entries than there are threads. (This code
     doesn't have access to that number, either, so just use 64, since the core
     can currently have 32, and the skin may have some more.) */
  if ( hq_assert_depth > 64 )
    pfnHqCustomAssert(__FILE__, __LINE__, "*** ASSERT within ASSERT! ***", vlist, AssertRecursive);

  if ( filename == NULL || line < 0 )
    pfnHqCustomAssert(__FILE__, __LINE__, "*** ASSERT file/line not saved! ***", vlist, AssertRecursive);

  pfnHqCustomAssert(filename, line, pszFormat, vlist, AssertNotRecursive);

  va_end(vlist);

  HqAtomicDecrement(&hq_assert_depth, dummy);
}


void HQNCALL HqAssertPhonyExit( void )
{
}


int HQNCALL HqAssertDepth( void )
{
  hq_atomic_counter_t depth = hq_assert_depth; /* capture a snapshot */
  return depth > MAXINT ? MAXINT : (int)depth;
}


void HQNCALL HqTraceSetFileAndLine(const char *pszFilename, int nLine)
{
  /* Lock the assert data until the following HqTrace/HqAssert call can
     read it. */
  spinlock_counter(&hq_assert_spinlock, 1) ;
  if ( pszFilenameSaved != NULL || nLineSaved >= 0 ) {
    HqAssert("*** HqTrace/HqAssert not called after HqTraceSetFileAndLine ***");
  }
  pszFilenameSaved = pszFilename;
  nLineSaved = nLine;
}


void HQNCALL HqTrace(const char *pszFormat, ...)
{
  const char *filename = pszFilenameSaved ;
  int line = nLineSaved ;
  va_list vlist;

  pszFilenameSaved = NULL ;
  nLineSaved = -1 ;

  spinunlock_counter(&hq_assert_spinlock) ;

  va_start(vlist, pszFormat);

  if ( filename == NULL || line < 0 )
    pfnHqCustomAssert(__FILE__, __LINE__, "*** TRACE file/line not saved! ***", vlist, AssertRecursive);

  pfnHqCustomTrace(filename, line, pszFormat, vlist);

  va_end(vlist);
}

static void HQNCALL DefaultCustomAssert(const char *pszFilename, int nLine,
                                        const char *pszFormat, va_list vlist,
                                        int assertflag)
{
  FILE *out ;

  UNUSED_PARAM(int, assertflag) ;

  fprintf(stderr, "Assert failed in file %s at line %d: ", pszFilename, nLine);
  vfprintf(stderr, pszFormat, vlist);
  fputc('\n', stderr);
  fflush(stderr);

  /* Also write assert to file on disk: */
  if ( (out = fopen("hqassert.txt", "a")) != NULL ) {
    fprintf(out, "Assert failed in file %s at line %d: ", pszFilename, nLine);
    vfprintf(out, pszFormat, vlist);
    fputc('\n', out);
    fflush(out);
    fclose(out) ;
  }

  abort() ;
}

static void HQNCALL DefaultCustomTrace(const char *pszFilename, int nLine,
                                       const char *pszFormat, va_list vlist)
{
  fprintf(stdout, "HQTRACE(%s:%d): ", pszFilename, nLine);
  vfprintf(stdout, pszFormat, vlist);
  fputc('\n', stdout);

  /** \todo ajcd 2012-03-16: Also write to file on disk? */
}

void HQNCALL SetHqAssertHandlers(HqAssertHandlers_t *functions)
{
  if ( functions != NULL ) {
    if ( functions->assert_handler != NULL )
      pfnHqCustomAssert = functions->assert_handler ;

    if ( functions->trace_handler != NULL )
      pfnHqCustomTrace = functions->trace_handler ;
  }
}

#else   /* ! defined( ASSERT_BUILD ) */

void HQNCALL SetHqAssertHandlers(HqAssertHandlers_t *functions)
{
  UNUSED_PARAM(HqAssertHandlers_t *, functions) ;
}

#endif  /* ! defined( ASSERT_BUILD ) */

/*
Log stripped */
