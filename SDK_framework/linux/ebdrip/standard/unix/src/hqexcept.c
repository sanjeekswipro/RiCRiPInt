/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!unix:src:hqexcept.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Platform specific exception and signal handling
 */

#include "std.h"
#include <stdio.h>
#include <execinfo.h>
#include <stdlib.h>
#include <signal.h>

static int hqexcept_depth = 0; /**< Exception recursion/call depth */
static int hqexcept_verb  = 0; /**< Exception info verbosity level */
static void (* hqexcept_print)(char *) = NULL; /**< client callback */

/**
 * Default exception handler print function if none specified by the client.
 *
 * Any client supplied exception print callback function has to be totally
 * self-contained. i.e. it cannot rely on any initialisation, or have any
 * dependencies on other code. If not, then a crash in that initialisation or
 * dependent code would mean that exception messages could not be shown, and
 * can easily lead to recursive crashes. This is not always easy to achieve,
 * so provide a very simple default exception print callback. This just writes
 * the messages to a fixed file. It does a lazy init (opening the file when
 * required), flushes after each message. It has no shutdown, relying on the
 * O/S to close and tidy-up the file pointer. This makes it as bomb-proof as
 * is possible.
 */
static void hqexcept_mess(char *mess)
{
  static FILE *hqexcept_f = NULL;

  if ( hqexcept_f == NULL )
    hqexcept_f = fopen("hqexcept.txt", "w");
  if ( hqexcept_f ) {
    fprintf(hqexcept_f, "%s\n", mess);
    fflush(hqexcept_f);
  }
}

/**
 * Call the client output function with the given exception message.
 */
static void except_mess(char *mess)
{
  if ( hqexcept_print )
    (*hqexcept_print)(mess);
}

/**
 * Print the current C callstacks.
 *
 * \todo BMJ 16-Jan-12 : Work out how to get the stacks for each thread.
 */
static void print_c_stacks(char *title)
{
  void *callstack[128];
  int i, frames = backtrace(callstack, 128);
  char **strs = backtrace_symbols(callstack, frames);

  /** \todo ajcd 2012-03-02: Add process/thread ID to title message. */
  except_mess(title);
  for (i = 0; i < frames; ++i) {
    except_mess(strs[i]);
  }
  free(strs);
}

static void HqExceptHandler(int signum)
{
  if ( hqexcept_depth++ > 0 ) {
    /* Recursive exception, or called from a second thread for some reason.
     * Just get out as quick as we can
     */
    exit(1);
  }

  except_mess("=+=+= An Unexpected Program Exception Occurred: =+=+=");
  if ( hqexcept_verb > 0 )
    print_c_stacks("C call stack");
  hqexcept_depth--;
  exit(1);
}

/**
 * Unix exception catching : Not yet Implemented
 */
void HQNCALL HqCatchExceptions(void (HQNCALL *func)(char *))
{
  static int i, sigs[] = { SIGILL, SIGSEGV, SIGFPE, SIGBUS, SIGSYS };

  hqexcept_depth = 0;

  /* Establish the verbosity level depending on the flavour of the build */
  hqexcept_verb  = 0;
#ifdef TIMING_BUILD
  hqexcept_verb  = 1;
#endif
#ifdef DEBUG_BUILD
  hqexcept_verb  = 2;
#endif

  if ( func != NULL )
    hqexcept_print = func;
  else
    hqexcept_print = hqexcept_mess;

  for ( i = 0; i < sizeof(sigs)/sizeof(sigs[0]); i++ ) {
    (void)signal(sigs[i], HqExceptHandler);
  }
}

/**
 * Print out the current C stacks for all threads.
 */
void HQNCALL HqCStacks(char *title)
{
  print_c_stacks(title);
}

/*
* Log stripped */
