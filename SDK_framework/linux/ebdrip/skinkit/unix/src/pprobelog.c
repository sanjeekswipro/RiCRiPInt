/* Copyright (c) 2008-2014 Global Graphics Software Ltd. All Rights Reserved.
 *
 * $HopeName: SWskinkit!unix:src:pprobelog.c(EBDSDK_P.1) $
 *
 * Probe trace logging
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * @file
 * @brief Capture profiling information from RIP
 *
 * This file implements a log handler that captures events to memory, then
 * runs a lazy write-behind thread to write these to an output file. It uses
 * native threads rather than pthreads, because the RIP build may not be
 * multi-threaded, and we may not have loaded the pthreads DLL. (The multi
 * threaded Windows builds explicitly load the pthreads DLL during RIP
 * initialisation; the logging API may be called by the skin before the RIP
 * is initialised.)
 */

#define _POSIX_C_SOURCE 200112L
#ifdef SOLARIS
#define __EXTENSIONS__
#endif

#include <stddef.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#ifdef MACOSX
#include <mach/mach_time.h>
#endif
#include <sys/time.h>


#include "std.h"
#include "swtrace.h"
#include "probelog.h"

#ifdef GPROF_BUILD
void moncontrol(int) ; /* This prototype is missing from some glibc versions */
#endif

/****************************************************************************/
#if defined(MACOSX)
typedef uint64_t timestamp_t ;
#elif defined(linux)
typedef unsigned long long timestamp_t ;
#else
typedef clock_t timestamp_t ;
#endif

/** A single entry in the memory resident log. */
typedef struct {
  timestamp_t timestamp ;
  pthread_t thread_id ;
  int trace_id ;
  int trace_type ;
  intptr_t trace_designator ;
} tracelog_entry ;

/** Collect tracelog entries into approximately 64k chunks, so the malloc
    of the chunk is quick. */
#define ENTRIES_PER_CHUNK (65536 / sizeof(tracelog_entry))

/** A chunk of entries in the memory resident log, to avoid having to malloc and write out each entry individually. */
typedef struct tracelog_chunk {
  tracelog_entry entries[ENTRIES_PER_CHUNK] ;
  unsigned long nentries ;
  struct tracelog_chunk *next ;
} tracelog_chunk ;

static unsigned long entry_index = 0 ;
static intptr_t entries_lost = 0 ;
static int trace_capture = FALSE ;
static int trace_quit = FALSE ;
static int done_timebase = FALSE ;

static tracelog_chunk *current = NULL ;
static tracelog_chunk *savelist = NULL ;
static tracelog_chunk **savelist_end = &savelist ;
static tracelog_chunk *freelist = NULL ;

static FILE *tracelog_file = NULL ;
static pthread_t wb_thread ;
static pthread_cond_t wb_ready ;
static pthread_cond_t wb_flushed ;
static pthread_mutex_t trace_lock ;

static int probe_ready = FALSE ;

static SwWriteProbeLogFn *pfnWriteProbeLog;

/** \brief Write-behind thread function. */
static void *probe_write_log(void *param) ;

static tracelog_entry *probe_slot(timestamp_t now,
                                  pthread_t thread,
                                  int trace_id,
                                  int trace_type,
                                  intptr_t trace_designator) ;

/** \brief Return values for \c probe_write_log. */
enum {
  WriteBehindOK,
  WriteBehindFailedOpen,
  WriteBehindFailedClose
} ;

/* The selected timestamp function */
static timestamp_t (*timestamp_fn)(void) ;
static timestamp_t (*timestamp_now_fn)(void) = NULL ;
static timestamp_t ticks_per_second ;
static timestamp_t start_time ;
static timestamp_t wasted_delta ;
static timestamp_t wasted_time ;

/* Time functions to catch non-Linux and non-MacOS X platforms, or
   when the time functions fail on Linux and MacOS X.

   For example;
   The embedded Linux-PPC board uses the function timestamp_now_gt4
   since clock_getres fails and sizeof(timestamp_t) is 8.

   If and only if not Linux and not Mac OS X or if get time resolution
   function fail, then the following timestamp_now* functions are
   used;
   - timestamp_now_gt4() used when timestamp_t size is greater
                         than four bytes.
   - timestamp_now_4()   used when timestamp_t size is not greater
                         than four bytes.
*/
static timestamp_t timestamp_now_gt4(void)
{
  struct timeval tNow;
  timestamp_t nNow;
  gettimeofday(&tNow, NULL);

  nNow = ((timestamp_t)tNow.tv_sec * 1000000) + (timestamp_t)tNow.tv_usec;

/*  printf("tNow.tv_sec %llu, tNow.tv_usec %llu, nNow %llu, sizeof(nNow)=%d\n",
  (timestamp_t)tNow.tv_sec, (timestamp_t)tNow.tv_usec, nNow, sizeof(nNow)); */

  return nNow ;
}

static timestamp_t timestamp_now_4(void)
{
  return (timestamp_t)clock() ;
}

static timestamp_t timestamp_clock(void)
{
  return timestamp_now_fn() - start_time ;
}

#if defined(linux)
#define CLOCK_FOR_TRACE CLOCK_PROCESS_CPUTIME_ID

static timestamp_t timestamp_gettime(void)
{
  struct timespec timespec ;

  if ( clock_gettime(CLOCK_FOR_TRACE, &timespec) != 0 )
    return 0 ; /* Now what? */

  return (timestamp_t)timespec.tv_sec * (timestamp_t)1000000000 + (timestamp_t)timespec.tv_nsec - start_time ;
}
#endif

#if defined(MACOSX)
static timestamp_t timestamp_mach(void)
{
  return mach_absolute_time() - start_time ;
}
#endif

/* Initialise critical sections for probe handling */
void PKProbeLogInit(SwWriteProbeLogFn *pfnWriteLog)
{
  pthread_attr_t attr ;
#if defined(MACOSX)
  mach_timebase_info_data_t timebase ;
#elif defined(linux)
  struct timespec timebase ;
  int nResult;
#endif

  trace_capture = FALSE ;
  trace_quit = FALSE ;
  done_timebase = FALSE ;
  entry_index = 0 ;
  entries_lost = 0 ;
  wasted_time = 0 ;
  current = NULL ;
  savelist = NULL ;
  savelist_end = &savelist ;
  freelist = NULL ;
  tracelog_file = NULL ;
  probe_ready = FALSE ;
  pfnWriteProbeLog = pfnWriteLog;

  if ( pthread_mutex_init(&trace_lock, NULL) != 0 )
    return ;

  if ( pthread_cond_init(&wb_ready, NULL) != 0 )
    goto mutex_destroy ;

  if ( pthread_cond_init(&wb_flushed, NULL) != 0 )
    goto cond_destroy ;

  if ( pthread_attr_init(&attr) != 0 )
    goto flush_destroy ;

  if ( pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0 )
    goto attr_destroy ;

  if ( pthread_create(&wb_thread, &attr, probe_write_log, NULL) != 0 )
    goto attr_destroy ;

  (void)pthread_attr_destroy(&attr) ;


#if defined(MACOSX)
  if ( mach_timebase_info(&timebase) == 0 &&
       (ticks_per_second = (timestamp_t)1000000000 * (timestamp_t)timebase.denom / (timestamp_t)timebase.numer) > (timestamp_t)CLOCKS_PER_SEC ) {
    timestamp_fn = &timestamp_mach ;
    start_time = mach_absolute_time() ;
  } else
#elif defined(linux)
  nResult = clock_getres(CLOCK_FOR_TRACE, &timebase);
  if ( nResult == 0 &&
       timebase.tv_sec == 0 &&
       (ticks_per_second = (timestamp_t)1000000000 / timebase.tv_nsec) > (timestamp_t)CLOCKS_PER_SEC ) {
    timestamp_fn = &timestamp_gettime ;
    if ( clock_gettime(CLOCK_FOR_TRACE, &timebase) != 0 )
      goto cond_destroy ;

    start_time = (timestamp_t)timebase.tv_sec * (timestamp_t)1000000000 + (timestamp_t)timebase.tv_nsec ;
  } else
#endif
  {
    if(sizeof(timestamp_t)>4) {
      timestamp_now_fn = timestamp_now_gt4;
    } else {
      timestamp_now_fn = timestamp_now_4;
    }
    timestamp_fn = &timestamp_clock ;
    ticks_per_second = (timestamp_t)CLOCKS_PER_SEC ;
    start_time = timestamp_now_fn();
  }

  /* Set up the number of ticks it must accumulate before we report on time
     the probe handler has wasted. For now, let us know whenever we've
     accumulated 1ms of time handling probes. */
  wasted_delta = ticks_per_second / 1000 ;
  if ( wasted_delta == 0 ) /* Must be at least one tick */
    wasted_delta = 1 ;

  /* If we succeeding in creating the events and objects, then start trace
     capture. */
  trace_capture = TRUE ;
  probe_ready = TRUE ;

  return ;

 attr_destroy:
  (void)pthread_attr_destroy(&attr) ;
 flush_destroy:
  (void)pthread_cond_destroy(&wb_flushed) ;
 cond_destroy:
  (void)pthread_cond_destroy(&wb_ready) ;

 mutex_destroy:
  (void)pthread_mutex_destroy(&trace_lock) ;
}

void PKProbeLogFinish(void)
{
  if ( probe_ready ) {
#define return DO_NOT_RETURN - IN_CRITICAL_SECTION
    if ( pthread_mutex_lock(&trace_lock) == 0 ) {
      /* Prevent any new probes from being logged. */
      trace_capture = FALSE ;

      if ( wasted_time > 0 ) {
        timestamp_t handled_at = (*timestamp_fn)() ;
        pthread_t thread = pthread_self() ;
        tracelog_entry *pbegin, *pend ;
        if ( (pbegin = probe_slot(handled_at - wasted_time, thread,
                                  SW_TRACE_PROBE, SW_TRACETYPE_ENTER,
                                  0)) == NULL ) {
          entries_lost += 2 ;
        } else if ( (pend = probe_slot(handled_at, thread, SW_TRACE_PROBE,
                                       SW_TRACETYPE_EXIT, 0)) == NULL ) {
          /* Can't get second probe slot to record wasted time. We may
             have wasted time trying to get the slot, so account for the
             possible malloc() delay. We also need to fill in the
             allocated probe slot with a no-op, our best bet is a probe
             add with 1 extra lost entry. */
          pbegin->trace_type = SW_TRACETYPE_ADD ;
          pbegin->trace_designator = entries_lost + 1 ;
          entries_lost = 0 ;
        }
      }

      if ( entries_lost > 0 ) {
        timestamp_t handled_at = (*timestamp_fn)() ;
        pthread_t thread = pthread_self() ;
        if ( probe_slot(handled_at, thread, SW_TRACE_PROBE,
                        SW_TRACETYPE_ADD, entries_lost) != NULL )
          entries_lost = 0 ;
      }

      /* Add the last block to the write-behind list. */
      if ( current ) {
        *savelist_end = current ;
        savelist_end = &current->next ;
        current = NULL ;
      }

      /* Indicate to the write-behind thread that it should quit when it's
         finished writing. */
      trace_quit = TRUE ;

      /* Signal write-behind thread to do its work and then quit. */
      (void)pthread_cond_signal(&wb_ready) ;

      (void)pthread_mutex_unlock(&trace_lock) ;
    }
#undef return

    /* Wait for the write-behind thread to terminate. */
    (void)pthread_join(wb_thread, NULL) ;

    probe_ready = FALSE ;

  /* Ensure that the savelist is freed; the write-behind thread should have
     done that, but there is a possibility that it didn't get to initialise
     properly. */
#define return DO_NOT_RETURN - IN_CRITICAL_SECTION
    if ( pthread_mutex_lock(&trace_lock) == 0 ) {
      tracelog_chunk *chunk ;

      while ( (chunk = savelist) != NULL ) {
        savelist = chunk->next ;
        free(chunk) ;
      }

      while ( (chunk = freelist) != NULL ) {
        freelist = chunk->next ;
        free(chunk) ;
      }

      (void)pthread_mutex_unlock(&trace_lock) ;
    }
#undef return

    /* Free the write-behind event handle */
    (void)pthread_cond_destroy(&wb_ready) ;
    (void)pthread_cond_destroy(&wb_flushed) ;
    (void)pthread_mutex_destroy(&trace_lock) ;
  }
}

static tracelog_entry *probe_slot(timestamp_t now,
                                  pthread_t thread,
                                  int trace_id,
                                  int trace_type,
                                  intptr_t trace_designator)
{
  tracelog_entry *entry = NULL ;

  if ( current == NULL ) {
    /* This is the first entry in a chunk; we need to lazily allocate a chunk
       for it. */
    if ( (current = freelist) != NULL ) {
      freelist = freelist->next ;
      current->next = NULL ;
      current->nentries = 0 ;
    } else if ( (current = malloc(sizeof(tracelog_chunk))) != NULL ) {
      current->next = NULL ;
      current->nentries = 0 ;
    } else { /* Can't capture this event. */
      return NULL ;
    }
    entry_index = 0 ;
  }

  if ( current != NULL ) {
    entry = &current->entries[entry_index++] ;

    if ( !done_timebase ) {
      done_timebase = TRUE ;

      /* Add the timebase into the log, high 32 bits first. This is only done
         the first time we allocate a chunk, so there should be space for the
         two entries. */
      HQASSERT(entry_index < ENTRIES_PER_CHUNK,
               "Not enough space for timebase entry") ;
      entry->timestamp = now ;
      entry->thread_id = thread ;
      entry->trace_id = SW_TRACE_PROBE ;
      entry->trace_type = SW_TRACETYPE_MARK ;
      entry->trace_designator = (intptr_t)((ticks_per_second >> 16) >> 16) ;
      entry = &current->entries[entry_index++] ;
      HQASSERT(entry_index < ENTRIES_PER_CHUNK,
               "Not enough space for timebase entry") ;
      entry->timestamp = now ;
      entry->thread_id = thread ;
      entry->trace_id = SW_TRACE_PROBE ;
      entry->trace_type = SW_TRACETYPE_MARK ;
      entry->trace_designator = (intptr_t)(ticks_per_second & 0xffffffffu) ;
      entry = &current->entries[entry_index++] ;
    }

    if ( entries_lost != 0 ) {
      /* We should only enter this case on the first time after a malloc
         succeeded, so there should be space for a "lost entry" trace and
         the trace we were called to do.  */
      HQASSERT(entry_index < ENTRIES_PER_CHUNK,
               "Not enough space for lost trace entry") ;
      entry->timestamp = now ;
      entry->thread_id = thread ;
      entry->trace_id = SW_TRACE_PROBE ;
      entry->trace_type = SW_TRACETYPE_ADD ;
      entry->trace_designator = entries_lost ;

      entries_lost = 0 ;
      entry = &current->entries[entry_index++] ;
    }

    entry->timestamp = now ;
    entry->thread_id = thread ;
    entry->trace_id = trace_id ;
    entry->trace_type = trace_type ;
    entry->trace_designator = trace_designator ;
    current->nentries = entry_index ;

    if ( entry_index == ENTRIES_PER_CHUNK ) {
      /* We've filled up that chunk. Add it to the savelist, clear the
         current block. */
      *savelist_end = current ;
      savelist_end = &current->next ;
      current = NULL ;

      /* Signal write-behind thread to do its work. This won't actually get
         scheduled until we're out of the critical section, which will be
         after this entry is filled in by the caller. */
      (void)pthread_cond_signal(&wb_ready) ;
    }
  }

  return entry ;
}

/* Probe handler for logging. This is used to capture, timestamp and log
   probe information. */
void RIPFASTCALL PKProbeLog(int trace_id,
                            int trace_type,
                            intptr_t trace_designator)
{
  HQASSERT(trace_type > SW_TRACETYPE_INVALID &&
           trace_type < g_nTraceTypeNames, "Invalid trace type") ;

  HQASSERT(trace_id >= 0, "Invalid trace ID") ;
  if ( trace_id >= g_nTraceNames )
    return ;

  /* If probe is not enabled, then check if we're enabling it */
  if ( !g_pabTraceEnabled[trace_id] ) {
    /* If we're enabling it, then enable it and return */
    if ( trace_type == SW_TRACETYPE_ENABLE )
      g_pabTraceEnabled[trace_id] = TRUE ;
    return ;
  } else if ( trace_type == SW_TRACETYPE_DISABLE ) {
    /* otherwise it's already enabled and we're disabling it. */
    g_pabTraceEnabled[trace_id] = FALSE ;
    return ;
  } else if ( trace_type == SW_TRACETYPE_ENABLE ) {
    /* otherwise it's already enabled and it's being enabled - again. */
    return ;
  }

  /* We'll check trace_capture both outside the mutex (as a fast test so we
     don't have to take the mutex), and again inside (to verify we're not
     shutting down the probe system). */
  if ( trace_capture ) {
    timestamp_t now = (*timestamp_fn)();
    pthread_t thread = pthread_self() ;

    /* Check how long the process of logging this event took, especially if we
       had to wait for an event or the critical section lock. So long as the
       timer returns a reasonable multiple of the processor cycles, we should
       see zero cycles most of the time. */
    if ( pthread_mutex_lock(&trace_lock) == 0 ) {
      if ( trace_capture ) {
        tracelog_entry *entry ;
        if ( (entry = probe_slot(now, thread, trace_id, trace_type,
                                 trace_designator)) != NULL ) {
          timestamp_t handled_at = (*timestamp_fn)() ;
          timestamp_t wasted_self = handled_at - now ;
          wasted_time += wasted_self ;
          if ( wasted_time >= wasted_delta ) {
            /* Retrospectively account for wasted time */
            tracelog_entry *pbegin, *pend ;
            if ( (pbegin = probe_slot(handled_at - wasted_time, thread,
                                      SW_TRACE_PROBE, SW_TRACETYPE_ENTER,
                                      (intptr_t)wasted_self)) == NULL ) {
              /* Can't get first probe slot to record wasted time. We may
                 have wasted time trying to get the slot, so account for the
                 possible malloc() delay. We'll note that we couldn't get the
                 probes, even though we will account correctly for the wasted
                 time at a later point. */
              entries_lost += 2 ;
              wasted_time += (*timestamp_fn)() - handled_at ;
            } else if ( (pend = probe_slot(handled_at, thread, SW_TRACE_PROBE,
                                           SW_TRACETYPE_EXIT,
                                           (intptr_t)wasted_self)) == NULL ) {
              /* Can't get second probe slot to record wasted time. We may
                 have wasted time trying to get the slot, so account for the
                 possible malloc() delay. We also need to fill in the
                 allocated probe slot with a no-op, our best bet is a probe
                 add with 1 extra lost entry. */
              pbegin->timestamp = handled_at ;
              pbegin->trace_type = SW_TRACETYPE_ADD ;
              pbegin->trace_designator = entries_lost + 1 ;
              entries_lost = 0 ;
              wasted_time += (*timestamp_fn)() - handled_at ;
            } else {
              /* Got two probe slots. We want to account the delay in
                 handling *this* probe to any section we were entering or
                 exiting. We'll shuffle the entries if we're exiting a
                 section, so the probe is entirely within the section. */
              if ( trace_type == SW_TRACETYPE_EXIT ) {
                tracelog_entry tmp = *entry ;
                *entry = *pbegin ;
                *pbegin = *pend ;
                *pend = tmp ;
              }
              /* Backdate start of probe to count all wasted time. */
              wasted_time = (*timestamp_fn)() - handled_at ;
            }
          }
        } else {
          ++entries_lost ;
        }
      }
      (void)pthread_mutex_unlock(&trace_lock) ;
    } else {
      /* Can't capture this entry, because we can't get the mutex. */
      entries_lost++ ;
    }
  }
}

/** The write-behind thread is a singleton. It waits to be woken, opens the
 * output trace file if necessary, writes the entries to the log file, and
 * then goes back to waiting.
 */
static void *probe_write_log(void *param)
{
  intptr_t result = WriteBehindOK ;
  int looping = TRUE ;

  UNUSED_PARAM(void *, param) ;

  do {
    tracelog_chunk *chunk = NULL, *willfree = NULL ;
    int timedout = FALSE ;
    struct timespec waketime ;

    /* MacOS X does not support clock_gettime(). Try filling in the timespec
       values directly. */
    waketime.tv_sec = time(NULL) + 10 ;
    waketime.tv_nsec = 0 ;

    /* Wait for either the savelist to be non-empty, or the quit flag to be
       set. */
#define return DO_NOT_RETURN - IN_CRITICAL_SECTION
    if ( pthread_mutex_lock(&trace_lock) == 0 ) {

      if ( savelist == NULL && !trace_quit ) {
        /* Wait for a full chunk to write; if nothing happens in 10 seconds,
           see if we can poach an incomplete chunk. In the case of deadlock
           in the RIP we may be able to use the log information to
           reconstruct the timing sequence. */
        if ( pthread_cond_timedwait(&wb_ready, &trace_lock, &waketime) == ETIMEDOUT ) {
          timedout = TRUE ;
          if ( current != NULL && current->nentries > 0 ) {
            /* If we timed out, and there is anything in the current block,
               move it to the savelist. */
            *savelist_end = current ;
            current = NULL ;
          }
        }
      }

      chunk = savelist ;
      savelist = NULL ;
      savelist_end = &savelist ;

      looping = !trace_quit ;

      (void)pthread_mutex_unlock(&trace_lock) ;
    }
#undef return

    if ( chunk != NULL ) {
      /* Write out all pending chunks. */
      if (!pfnWriteProbeLog && tracelog_file == NULL && result == WriteBehindOK) {
        tracelog_file = fopen(g_szProbeLog, "w") ;
        result = WriteBehindFailedOpen ;
      }

      do {
        unsigned long index ;
        tracelog_chunk *next = chunk->next ;

        /* We have a chunk. If the tracelog file is open, write details to the
           file or if a callback has been supplied, call it.
           Whatever happens, we should free the chunk when we're done. */
        if (pfnWriteProbeLog) {
          char szLine[256];

          for ( index = 0 ; index < chunk->nentries ; ++index ) {
            tracelog_entry *entry = &chunk->entries[index] ;
            sprintf(szLine,
                    "Time=%f Thread=%3d Id=%s Type=%s Designator=%" PRIxPTR "\n",
                    entry->timestamp / (double)ticks_per_second,
                    (int)entry->thread_id, g_ppTraceNames[entry->trace_id],
                    g_ppTraceTypeNames[entry->trace_type],
                    entry->trace_designator) ;
            pfnWriteProbeLog(szLine, strlen(szLine));
          }
        }
        else if ( tracelog_file != NULL) {
          for ( index = 0 ; index < chunk->nentries ; ++index ) {
            tracelog_entry *entry = &chunk->entries[index] ;
            fprintf(tracelog_file,
                    "Time=%f Thread=%3d Id=%s Type=%s Designator=%" PRIxPTR "\n",
                    entry->timestamp / (double)ticks_per_second,
                    (int)entry->thread_id, g_ppTraceNames[entry->trace_id],
                    g_ppTraceTypeNames[entry->trace_type],
                    entry->trace_designator) ;
          }

          /* If this wasn't a run of the mill chunk overflow, flush the trace
             file, because it's likely we'll interrupt or crash this process. */
          if ( tracelog_file && timedout )
            fflush(tracelog_file) ;
        }

        chunk->next = willfree ;
        willfree = chunk ;
        chunk = next ;
      } while ( chunk != NULL ) ;
    }

#define return DO_NOT_RETURN - IN_CRITICAL_SECTION
    if ( pthread_mutex_lock(&trace_lock) == 0 ) {
      while ( willfree != NULL ) {
        tracelog_chunk *next = willfree->next ;
        willfree->next = freelist ;
        freelist = willfree ;
        willfree = next ;
      }
      /* Signal flushed event in case flush call is waiting. */
      (void)pthread_cond_signal(&wb_flushed) ;
      (void)pthread_mutex_unlock(&trace_lock) ;
    }
#undef return
  } while ( looping ) ;

  /* That was all, we were told to quit, so close down the log file. */
  if ( tracelog_file != NULL && trace_quit) {
    if ( fclose(tracelog_file) != 0 )
      result = WriteBehindFailedClose ;

    tracelog_file = NULL ;
  }

  return (void *)result ;
}

void PKProbeLogFlush(void)
{
#define return DO_NOT_RETURN - IN_CRITICAL_SECTION
  if ( pthread_mutex_lock(&trace_lock) == 0 ) {
    /* Add the last block to the write-behind list. */
    if ( current ) {
      *savelist_end = current ;
      savelist_end = &current->next ;
      current = NULL ;
    }

    /* Signal write-behind thread to do its work. */
    (void)pthread_cond_signal(&wb_ready) ;

    (void)pthread_mutex_unlock(&trace_lock) ;
  }
#undef return

#define return DO_NOT_RETURN - IN_CRITICAL_SECTION
  /* Wait for flushed event */
  if ( pthread_mutex_lock(&trace_lock) == 0 ) {
    (void)pthread_cond_wait(&wb_flushed, &trace_lock) ;
    (void)pthread_mutex_unlock(&trace_lock) ;
  }
#undef return

}
/****************************************************************************/
/* Probe handler for profiling control. This is used to handle the Quantify
   and gprof profiling control, so the RIP can turn on and off profiling
   for particular features. */

#ifdef GPROF_BUILD
static int trace_level = 0 ;
#endif

void RIPFASTCALL PKProbeProfile(int trace_id,
                                int trace_type,
                                intptr_t trace_designator)
{
  UNUSED_PARAM(intptr_t, trace_designator) ;

  HQASSERT(trace_type > SW_TRACETYPE_INVALID &&
           trace_type < g_nTraceTypeNames, "Invalid trace type") ;

  HQASSERT(trace_id >= 0, "Invalid trace ID") ;
  if ( trace_id >= g_nTraceNames )
    return ;

  if ( !g_pabTraceEnabled[trace_id] ) {
    if ( trace_type == SW_TRACETYPE_ENABLE )
      g_pabTraceEnabled[trace_id] = TRUE ;
    return ;
  } else if ( trace_type == SW_TRACETYPE_DISABLE ) {
    g_pabTraceEnabled[trace_id] = FALSE ;
    return ;
  }

  switch ( trace_type ) {
  case SW_TRACETYPE_DISABLE:
    g_pabTraceEnabled[trace_id] = FALSE ;
    return ;
  case SW_TRACETYPE_ENTER:
#ifdef GPROF_BUILD
    if ( trace_level++ == 0 || trace_id == SW_TRACE_THREAD ) {
      moncontrol(1) ;
    }
#endif /* !GPROF_BUILD */
    break ;
  case SW_TRACETYPE_EXIT:
#ifdef GPROF_BUILD
    if ( --trace_level == 0 || trace_id == SW_TRACE_THREAD ) {
      moncontrol(0) ;
    }
#endif /* !GPROF_BUILD */
    break ;
  case SW_TRACETYPE_RESET:
    if ( trace_id == SW_TRACE_USER ) {
#ifdef GPROF_BUILD
      fprintf(stderr, "Cannot clear gprof profile data\n");
      moncontrol(0) ;
#endif /* !GPROF_BUILD */
    }
    break ;
  }
}


