/* Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: HQNtimer!unix:src:timerimp.c(EBDSDK_P.1) $
 */

#include "std.h"

#define TIMER_IMPLEMENTOR (1)
#include "timerapi.h"
#include "timerimp.h"

#include <sys/time.h>
#ifndef NO_PTHREADS
#include <pthread.h>
#else
#error HQNtimer is only implemented for pthreads
#endif


static void *(HQNCALL *hqn_timer_alloc)(size_t size);
static void  (HQNCALL *hqn_timer_free)(void* mem);

/* Initialise the platform's timer system. */
int hqn_timer_system_init(void *(HQNCALL *alloc)(size_t size),
                          void  (HQNCALL *free)(void* mem))
{
  hqn_timer_alloc = alloc;
  hqn_timer_free = free;

  return(TRUE);
}

/* Finalise the platform's timer system. */
void hqn_timer_system_finish(void)
{
}

/* Internal timer callback structure */
struct TIMER_HANDLE {
  struct timeval    first;                      /* Time until first call */
  struct timeval    repeat;                     /* Time between calls */
  void              (*callback)(hqn_timer_t* timer,
                                void* data);    /* Function to be called */
  void*             data;                       /* Function data */
  pthread_t         thread_id;                  /* Thread which TimerThread() is running in */
};

static
hqn_timer_t*  hqn_timer_new(
  unsigned int  first,
  unsigned int  repeat,
  void    (*callback)(hqn_timer_t* timer, void* data),
  void*   data)
{
  hqn_timer_t*  timer;

  if ((timer = hqn_timer_alloc(sizeof(hqn_timer_t))) != NULL) {
    timer->first.tv_sec = first / 1000;
    timer->first.tv_usec = (first % 1000) * 1000;
    timer->repeat.tv_sec = repeat / 1000;
    timer->repeat.tv_usec = (repeat % 1000) * 1000;
    timer->callback = callback;
    timer->data = data;
    timer->thread_id = 0;
  }

  return(timer);
}

static
void hqn_timer_release(
  hqn_timer_t*  timer)
{
  hqn_timer_free(timer);
}

/* Thread function which repeatedly calls our timer callback
 * function.
 */
static void * TimerThread( void * data )
{
  hqn_timer_t* timer = data;
  struct timeval tv;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

  tv = timer->first;
  do {
    select(0, NULL, NULL, NULL, &tv);
    timer->callback(timer, timer->data);
    tv = timer->repeat;
  } while (tv.tv_usec > 0 || tv.tv_sec > 0);

  return NULL;
}

/* Timer API functions */

hqn_timer_t *HQNCALL hqn_timer_create(
  unsigned int  first,
  unsigned int  repeat,
  hqn_timer_callback_fn *callback,
  void*         data)
{
  hqn_timer_t* timer;

  if ((timer = hqn_timer_new(first, repeat, callback, data)) == NULL) {
    return(NULL);
  }

  {
    /* Create new thread to repeatedly call callback function */
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    if (pthread_create(&timer->thread_id, &attr, TimerThread, timer) == 0) {
      pthread_attr_destroy(&attr);
      return(timer);
    }
    pthread_attr_destroy(&attr);
  }

  hqn_timer_release(timer);
  return(NULL);
}

void HQNCALL hqn_timer_reset(
  hqn_timer_t* timer,
  unsigned int  next,
  unsigned int  repeat)
{
  UNUSED_PARAM(hqn_timer_t*, timer);
  UNUSED_PARAM(unsigned int, next);
  UNUSED_PARAM(unsigned int, repeat);

  /* NYI */
}

void HQNCALL hqn_timer_destroy(
  hqn_timer_t* timer)
{
  pthread_cancel(timer->thread_id);
  pthread_join(timer->thread_id, NULL);
  hqn_timer_release(timer);
}


