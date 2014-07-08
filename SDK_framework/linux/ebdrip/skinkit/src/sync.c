/* Copyright (C) 2006-2011, 2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:sync.c(EBDSDK_P.1) $
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Synchronization between the application calling thread and the thread that is
 * running the RIP. Having this synchronization simplifies the application code
 * because it doesn't need to worry about callbacks on the monitor or raster functions
 * happening asynchronously - they will only happen when the application has called into
 * one of the SwLe functions and is blocked waiting for the result.
 */

#include "sync.h"

#include "mem.h"
#include "ripthread.h"
#include "threadapi.h"


typedef struct SEM_T
{
  pthread_mutex_t m;
  pthread_cond_t  cv;
  int32           count;
  int32           waiting;
} SEM_T;


static pthread_t rip_thread_handle;
static HqBool fRipThreadStarted = FALSE;

/** \brief Wrapper to call StartRip() so we have the correct prototype for the
 * thread starting functions. */
static void * StartRipWrapper(void * unused)
{
  UNUSED_PARAM(void *, unused);

  StartRip();

  return NULL;
}


HqBool StartRIPOnNewThread(void)
{
  /* Default attributes include PTHREAD_CREATE_JOINABLE and PTHREAD_SCOPE_SYSTEM
   * so no need to create our own attribute set
   */
  fRipThreadStarted = (pthread_create(&rip_thread_handle, NULL, StartRipWrapper, NULL) == 0);

  return fRipThreadStarted;
}


void WaitForRIPThreadToExit(void)
{
  void * return_value ;
  int    error = 0 ;
  int    a = 0 ;

  if( fRipThreadStarted )
  {
    error = pthread_join(rip_thread_handle, &return_value);
    fRipThreadStarted = FALSE;
  }

  /* Purely so we can break within the debugger. */
  if (error != 0)
  {
    a++ ;
  }
}


/*
 * Semaphore Implementation.
 */

void * PKCreateSemaphore(int32 initialValue)
{
  SEM_T * sem;

  if( (sem = (SEM_T *) MemAlloc(sizeof(SEM_T), FALSE, FALSE)) == NULL )
  {
    return NULL;
  }
  if( pthread_mutex_init (&sem->m, NULL) )
  {
    MemFree(sem);
    return NULL;
  }
  if( pthread_cond_init (&sem->cv, NULL) )
  {
    pthread_mutex_destroy(&sem->m);
    MemFree(sem);
    return NULL;
  }

  sem->count = initialValue;
  sem->waiting = 0;

  return sem;
}

int32 PKSignalSemaphore(void * semaHandle)
{
  SEM_T * sem = (SEM_T *) semaHandle;
  int32   fSuccess = TRUE;

  if( pthread_mutex_lock(&sem->m) )
  {
    return FALSE;
  }

  sem->count++;

  if( sem->waiting > 0 )
  {
    fSuccess = pthread_cond_signal(&sem->cv);
  }

  if( pthread_mutex_unlock(&sem->m) )
  {
    return FALSE;
  }

  return fSuccess;
}

int32 PKWaitOnSemaphore(void * semaHandle)
{
  SEM_T * sem = (SEM_T *) semaHandle;

  if( pthread_mutex_lock(&sem->m) )
  {
    return FALSE;
  }

  sem->waiting++;

  while( sem->count == 0 )
  {
    pthread_cond_wait(&sem->cv, &sem->m);
  }

  sem->waiting--;

  sem->count--;

  if( pthread_mutex_unlock(&sem->m) )
  {
    return FALSE;
  }

  return TRUE;
}

void PKDestroySemaphore(void * semaHandle)
{
  SEM_T * sem = (SEM_T *) semaHandle;

  pthread_mutex_destroy(&sem->m);
  pthread_cond_destroy(&sem->cv);

  MemFree(sem);
}

void PKSemaFinish(void)
{
  /* nothing to do */
}

