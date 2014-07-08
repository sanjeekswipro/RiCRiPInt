/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 * Tickle timer related utility functions
 *
 * (Mostly copied from SWcoreskin!unix:src:ctrlmain.c)
 *
 * $HopeName: SWdllskin!unix:src:ptickle.c(EBDSDK_P.1) $
 */

#include "tickle.h"

#include "swoften.h" /* SwTimer */

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef Solaris
#define SOLARIS_THREADS
#endif

#ifdef SOLARIS_THREADS
#include <thread.h>
#define TICKLE_USEC 50000
#else
#ifndef NO_PTHREADS
#include <pthread.h>
#define TICKLE_USEC 100000
#else
#include <signal.h>
#define TICKLE_USEC 50000
#endif
#endif

/*
 * Like ctrlmain.c, we leave the tickling threads running, and just
 * change a bit of global state to tell them whether or not they should
 * be tickling the timers.  This saves stopping and starting the threads
 * whenever the timers are turned on and off.
 */

/* TRUE iff the timers are being tickled */
static volatile int fTimerStarted = FALSE;

#if defined(SOLARIS_THREADS) || ! defined(NO_PTHREADS)
/* TRUE iff a thread is responsible for tickling the timers */
static volatile int fThreadStarted = FALSE;
#endif

#ifdef SOLARIS_THREADS
thread_t tid;
#else
#ifndef NO_PTHREADS
pthread_t h_tickle_thread;
#endif
#endif


/* Need similar structure definition to coreskin: */
static struct {
  int32  swtimer; /* decremented */
  uint32 gptimer; /* incremented */
} timers;

/* Called every TICKLE_USEC microseconds for manipulating the timers */
static void decrement_SwTimer(int unused) {
  /* Timers decremented for every millisecond */
  const int32 count = TICKLE_USEC / 1000;

  (void)unused; /* UNUSED */

  /* If we are meant to be tickoing the timers... */
  if (fTimerStarted) {
    timers.swtimer -= count;
    timers.gptimer += count;

#if !defined(SOLARIS_THREADS) && defined(NO_PTHREADS)
    /* Must reschedule the SIGALRM */
    signal(SIGALRM, decrement_SwTimer);
    ualarm(TICKLE_USEC, 0);
#endif
  }
}

#if defined(SOLARIS_THREADS) || !defined(NO_PTHREADS)

static void * tickling_func(void * unused) {
  struct timeval timeout;

  (void)unused; /* UNUSED */
  /* Call decrement_SwTimer() every TICKLE_USEC */
  while (fTimerStarted) {
    /* Sleep for TICKLE_USEC */
    timeout.tv_sec = 0;
    timeout.tv_usec = TICKLE_USEC;
    select(0, NULL, NULL, NULL, &timeout);

    decrement_SwTimer(0);
  }
  return NULL; /* return value will be ignored */
}

#endif

void startTickleTimer(void) {
  if (fTimerStarted) return;
  fTimerStarted = TRUE;

  /* Set up the timer for the RIP */
  SwTimer = &timers.swtimer;

#ifdef SOLARIS_THREADS
  /* Use a thread to decrement the timers.  The thread is created with
   * THR_BOUND so that it will compete for CPU time on a system-wide
   * basis, rather than waiting for the main thread to yield.
   */
  if (! fThreadStarted) {
    /* Create thread running tickling_func(NULL) */
    if (thr_create(NULL, 0, tickling_func, NULL, THR_BOUND, &tid))
      HQFAIL("thr_create()");
    fThreadStarted = TRUE;
  }

#else
  /* No Solaris threads */
#ifndef NO_PTHREADS
  /* But we do have POSIX threads.  Do the same as above. */
  if (!fThreadStarted) {
    pthread_attr_t attr;

    /* Create non-default attributes for the thread */
    if (pthread_attr_init(&attr)) HQFAIL("pthread_attr_init()");
    /* Avoid having to call pthread_join(): */
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
      HQTRACE(TRUE, ("pthread_attr_setdetachstate()"));
    /* For system-wide based scheduling, uncomment the following:
    if (pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM))
      HQTRACE(TRUE, ("pthread_attr_setscope()"));
     */

    /* Create thread running tickling_func(NULL) */
    if (pthread_create(&h_tickle_thread, &attr, tickling_func, NULL))
      HQFAIL("pthread_create()");

    /* Release thread attributes */
    if (pthread_attr_destroy(&attr)) HQTRACE(TRUE, ("pthread_attr_destroy()"));

    fThreadStarted = TRUE;
  }
#else
  /* No Solaris threads, and no POSIX threads.  Use brute force
   * signals. */
  signal(SIGALRM, decrement_SwTimer);
  ualarm(TICKLE_USEC, 0);
#endif
#endif
}


void stopTickleTimer(void) {
  void *return_val;

  if (! fTimerStarted) return;
  fTimerStarted = FALSE;
#ifdef SOLARIS_THREADS
  thr_join(tid, NULL, &return_val);
#else
#ifndef NO_PTHREADS
  pthread_join(h_tickle_thread, &return_val);
#endif
#endif
}

void init_C_globals_ptickle(void)
{
#if defined(SOLARIS_THREADS) || !defined(NO_PTHREADS)
  fThreadStarted = FALSE ;
#endif
  fTimerStarted = FALSE ;
}

/* Log stripped */
