/* Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!unix:src:pms_platform.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for Platform Wrapper.
 *
 */

#ifndef MACOSX
#define _XOPEN_SOURCE 600
#endif
#ifdef __NetBSD__
#define _NETBSD_SOURCE 1
#else
#define _BSD_SOURCE 1
#endif
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#include <sys/errno.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h> /* for PMS_CheckKeyPress select */
#include <string.h>

#include <termios.h> /* tcgetattr(), tcsetattr() */

#include <stdlib.h> /* atexit(), exit() */
#include <dirent.h>

#include "pms.h"
#include "pms_platform.h"
#include "pms_malloc.h"



#ifdef MACOSX
#define SEM_NAME_LEN 32

typedef struct {
  sem_t *sem;
  int semno;
} mac_sema_desc_t;


static int semno = 0;
static unsigned sem_count = 0;
#endif

static struct termios g_old_kbd_mode; /* remember keyboard mode for PMS_CheckKeyPress */

/*! \brief Structure type thread function args
*/
typedef struct PMS_TyThreadFunction
{
  pthread_t tid;
  pthread_mutex_t mutexThread;
  pthread_cond_t condThread;
  int bCondBroadcasted;
  void (*start_routine)(void*);
  void *arg;
} PMS_TyThreadFunction;

/**********************************************
 * User input
 **********************************************/

/**
* \brief Restore old keyboard mode settings
*
* Reset stdin back from single char mode
* to mode before set_singlecharmode() was called.
*/
static void set_old(void)
{
  tcsetattr(0, TCSANOW, &g_old_kbd_mode);
}

/**
 * \brief Set the input to single char mode
 *
 * Set the keyboard input (stdin) in a raw single character mode.
 */
static void set_singlecharmode(void)
{
  struct termios new_kbd_mode;

/* put keyboard (stdin, actually) in raw, unbuffered mode */
  tcgetattr(0, &g_old_kbd_mode);
  memcpy(&new_kbd_mode, &g_old_kbd_mode, sizeof(struct termios));
  new_kbd_mode.c_lflag &= ~(ICANON | ECHO);
  new_kbd_mode.c_cc[VTIME] = 0;
  new_kbd_mode.c_cc[VMIN] = 1;
  tcsetattr(0, TCSANOW, &new_kbd_mode);

/* when we exit, go back to normal, "cooked" mode */
  atexit(set_old);
}

/*! \brief Check for keyboard input
 *
 * UNIX/Linux
 *
 * Use \c PMS_GetKeyPress to get user input.
 * \return Zero no input detected. Non-zero for input detected.
*/
int PMS_CheckKeyPress(void)
{
  struct timeval timeout;
  fd_set read_handles;
  int status=0;
  static char init=0;
  static char selectfails=0;

  /* only set the single character mode once */
  if (!init) {
    set_singlecharmode();
    init = 1;
  }

  /* The select function ocassionally fails on Linux-PPC.
     Suppress further key press checks if failure count reaches three.
  */
  if(selectfails<4)
  {
    /* check stdin (fd 0) for activity */
    FD_ZERO(&read_handles);
    FD_SET(0, &read_handles);
    timeout.tv_sec = timeout.tv_usec = 0;
    status = select(0 + 1, &read_handles, NULL, NULL, &timeout);
    if(status < 0)
    {
      printf("select() failed in PMS_CheckKeyPress()\n");
      selectfails++;
      if(selectfails > 3)
      {
        printf("select() has failed again... disabling PMS_CheckKeyPress()\n");
      }
      return 0;
    }
  }
  return status;
}

/*! \brief Get the keyboard key pressed
 *
 * UNIX/Linux
 *
 * \return Character returned by read()
*/
unsigned char PMS_GetKeyPress(void)
{
  unsigned char temp;

  /* stdin = fd 0 */
  if(read(0, &temp, 1) != 1)
    return 0;
  return temp;
}

/**********************************************
 * Timing Functions.
 **********************************************/

/*! \brief Allow other threads to have a go
 *
 * UNIX/Linux
 *
*/
void PMS_RelinquishTimeSlice(void)
{
#ifdef _POSIX_PRIORITY_SCHEDULING
#ifndef LIBuClibc
  int nError;
  if(sched_yield()!=0)
  {
    nError = errno;
    printf("sched_yield failed %d\n", nError);
  }
#else /* LIBuClibc */
 PMS_Delay(5);
 #endif
 #else /* _POSIX_PRIORITY_SCHEDULING */
  /* sleep zero does nothing, sleep (1ms) instead */
  PMS_Delay(1);
#endif
}

/*! \brief Put the current thread to sleep for a while
 *
 * UNIX/Linux
 *
 * \param[in] nMilliSeconds Sleep time in milliseconds.
*/
void PMS_Delay(int nMilliSeconds)
{
  struct timespec ts;
  ts.tv_sec = ((float)nMilliSeconds)/1000;
  ts.tv_nsec = (nMilliSeconds-ts.tv_sec*1000)*1000000;
  nanosleep(&ts,NULL);
}

/*! \brief Get the elasped time since \c g_nTimeZero in milliseconds
 *
 * UNIX/Linux
 *
 * \return Time since \c g_nTimeZero, in milliseconds.
*/
unsigned long PMS_TimeInMilliSecs(void)
{
  struct tms tm;
  return (long)((times(&tm) * 1000L /sysconf(_SC_CLK_TCK)) - g_nTimeZero);
}

/**********************************************
 * Thread Implementation.
 **********************************************/

/**
 * \brief Thread wrapper function.
 *
 * \param pThreadInfo Pointer to structure containing function address,
 *                    semaphore handle, and argument list.
 *
 * This wrapper function enables us to keep the platform
 * specifics away from the common code.
 */
void *PMSThreadWrapper(void *pThreadInfo)
{
  PMS_TyThreadFunction *pTInfo = pThreadInfo;

  if(pTInfo->start_routine) {
    pTInfo->start_routine(pTInfo->arg);
  }

  /* signal waiting threads that this thread is about to terminate */
  pthread_mutex_lock(&pTInfo->mutexThread);
  pthread_cond_broadcast(&pTInfo->condThread);
  pTInfo->bCondBroadcasted = 1;
  pthread_mutex_unlock(&pTInfo->mutexThread);

  return (NULL);
}

/*! \brief Start a new thread.
 *
 * UNIX/Linux
 *
 * \param start_address Funtion pointer to the threads entry point.
 * \param stack_size Size of stack to use, in bytes.
 * \param arglist Pointer to argument list.
 * \return Pointer to thread (casted to void * for platform portability)
 *
*/
void *PMS_BeginThread(void( *start_address )( void * ), unsigned int stack_size, void *arglist )
{
  pthread_attr_t attr;
  unsigned int result;
  PMS_TyThreadFunction *ptThreadInfo = OSMalloc(sizeof(PMS_TyThreadFunction), PMS_MemoryPoolPMS);

  if(!ptThreadInfo) {
    return (NULL);
  }

  ptThreadInfo->bCondBroadcasted = 0;

  if ( pthread_mutex_init(&ptThreadInfo->mutexThread, NULL) != 0 ) {
    goto thread_info_destroy;
  }

  if ( pthread_cond_init(&ptThreadInfo->condThread, NULL) != 0 ) {
    goto thread_mutex_destroy;
  }

  if (pthread_attr_init(&attr)) {
    goto thread_cond_destroy;
  }

  ptThreadInfo->start_routine = start_address;
  ptThreadInfo->arg = arglist;
  result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) ||
           pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM) ||
           pthread_create(&ptThreadInfo->tid, &attr, PMSThreadWrapper, (void*)ptThreadInfo);

  (void)pthread_attr_destroy(&attr) ;

  if (result != 0) {
    PMS_SHOW_ERROR("Failed to create thread: \n");
    goto thread_cond_destroy;
  }

  return (void*)ptThreadInfo;

thread_cond_destroy:
  (void)pthread_cond_destroy(&ptThreadInfo->condThread) ;
thread_mutex_destroy:
  (void)pthread_mutex_destroy(&ptThreadInfo->mutexThread) ;
thread_info_destroy:
  OSFree(ptThreadInfo, PMS_MemoryPoolPMS);
  return (NULL);
}

/*! \brief Close and free a thread.
 *
 * UNIX/Linux
 *
 * \param[in] pThread Pointer to thread created by PMS_BeginThread.
 * \param[in] nWaitForThreadToExit Timeout in milliseconds.
 *              Negative: wait forever or until thread exits.
 *              Zero: do not wait.
 *              Postive: wait for up to # milliseconds or until thread exits.
 * \return 0: failure, 1: success, -1: success, but timeout occured whilst waiting for thread to exit.
*/
int PMS_CloseThread(void *pThread, int nWaitForThreadToExit)
{
  PMS_TyThreadFunction *pTInfo = pThread;
  int nRetVal = 0;

  if(!pTInfo)
    return 0;

  if(nWaitForThreadToExit > 0) {

    struct timeval now;
    struct timespec ts;
    int e;

    gettimeofday(&now, NULL);
    ts.tv_sec = now.tv_sec;
    ts.tv_nsec = now.tv_usec*1000;

    ts.tv_sec += nWaitForThreadToExit / 1000;

    ts.tv_nsec += nWaitForThreadToExit % 1000 * 1000000;

    if (ts.tv_nsec > 999999999){
      ts.tv_sec++;
      ts.tv_nsec = ts.tv_nsec % 1000000000;
    }

    /* wait (with timeout) until thread has finished */
    pthread_mutex_lock(&pTInfo->mutexThread);

    /* If the thread has already broadcasted the cond signal then we
       shouldn't wait for another one... there won't be another one. */
    if(pTInfo->bCondBroadcasted == 1) {
      e = 0;
    } else {
      e = pthread_cond_timedwait(&pTInfo->condThread, &pTInfo->mutexThread, &ts);
      pthread_mutex_unlock(&pTInfo->mutexThread);
    }

    /* now join so we wait until thread has -really- finished */
    if (e == ETIMEDOUT) {
      PMS_SHOW_ERROR("Timed out waiting for thread to exit.\n");
      (void)pthread_cancel(pTInfo->tid); /* try to forcefully stop it at
                                            a cancellation point */
      nRetVal = -1;
    } else {
      (void)pthread_join(pTInfo->tid, NULL);
      nRetVal = 1;
    }
  }
  else if (nWaitForThreadToExit < 0) {
    (void)pthread_join(pTInfo->tid, NULL) ;
    nRetVal = 1;
  }
  else {
    (void)pthread_cancel(pTInfo->tid); /* try to forcefully stop it at
                                          a cancellation point */
    nRetVal = 1;
  }

  (void)pthread_cond_destroy(&pTInfo->condThread) ;
  (void)pthread_mutex_destroy(&pTInfo->mutexThread) ;
  OSFree(pTInfo, PMS_MemoryPoolPMS);

  return nRetVal;
}

/**********************************************
 * Semaphore Implementation.
 **********************************************/

/*! \brief Create a semaphore
 *
 * UNIX/Linux
 *
 * Semaphore is casted to \c void* for platform portability.
 *
 * \param[in] initialValue Initial semaphore value.
 * \return Pointer to semaphore created (casted to \c void*).
*/
void * PMS_CreateSemaphore(int initialValue)
{
#ifdef MACOSX
  mac_sema_desc_t *sem;
  /* Mac OS X returns a failure for sem_init, so we have to use sem_open
     instead. sem_open requires a name, so we will invent one. */
  char name[SEM_NAME_LEN];
  sem_t *ossem;

  if ( (sem = OSMalloc(sizeof(mac_sema_desc_t), PMS_MemoryPoolPMS)) == NULL )
    return NULL;
  do {
    sprintf(name, "pms-%d-%d", getpid(), ++semno) ;
    if ( (ossem = sem_open(name, O_CREAT | O_EXCL, 0777, initialValue))
         == (sem_t *)SEM_FAILED )
      semno += 100; /* try to avoid other semaphores from the same crash */
  } while (ossem == (sem_t *)SEM_FAILED && errno == EEXIST);
  if (ossem == (sem_t *)SEM_FAILED) {
    OSFree(sem, PMS_MemoryPoolPMS);
    return NULL;
  }
  sem->sem = ossem; sem->semno = semno;
  sem_count++;
#else
  sem_t *sem;

  sem = (sem_t *) OSMalloc(sizeof(sem_t), PMS_MemoryPoolPMS);

  if (sem == NULL)
    return NULL;
  if (sem_init(sem, 0, initialValue)) {
    OSFree(sem, PMS_MemoryPoolPMS);
    return NULL;
  }
#endif
  return (void*)sem;
}

/*! \brief Increment a semaphore
 *
 * UNIX/Linux
 *
 * \param[in] semaHandle Pointer returned by \c PMS_CreateSemaphore.
 * \return TRUE if successful, FALSE otherwise.
*/
int PMS_IncrementSemaphore(void * semaHandle)
{
#ifdef MACOSX
  sem_t *sem = ((mac_sema_desc_t*)semaHandle)->sem;
#else
  sem_t *sem = (sem_t*)semaHandle;
#endif
  return ! sem_post(sem);
}

/*! \brief Wait forever for a semaphore
 *
 * UNIX/Linux
 *
 * \param[in] semaHandle Pointer returned by \c PMS_CreateSemaphore.
 * \return 1 when successful, otherwise never returns.
*/
int PMS_WaitOnSemaphore_Forever(void * semaHandle)
{
  int val;
#ifdef MACOSX
  sem_t *sem = ((mac_sema_desc_t*)semaHandle)->sem;
#else
  sem_t *sem = (sem_t*)semaHandle;
#endif
  do {
    val = sem_wait(sem);
  } while (val != 0);
  return 1;
}

/*! \brief Wait for a semaphore for a specified time.
 *
 * UNIX/Linux
 *
 * \param[in] semaHandle Pointer returned by \c PMS_CreateSemaphore.
 * \param[in] nMilliSeconds Timeout in milliseconds.
 * \return 1 if successful, 0 otherwise.
*/
int PMS_WaitOnSemaphore(void * semaHandle, unsigned int nMilliSeconds)
{
  int val;
  sem_t *sem = (sem_t*)semaHandle;
#ifndef MACOSX
  struct timespec ts;

  /* CLOCK_REALTIME can go up forward in leaps, and occasionally backwards.
     CLOCK_MONOTONIC does neither, it just keeps going forwards.
  */
  val = clock_gettime( CLOCK_MONOTONIC, &ts );
  if (val != 0) {
      val = clock_gettime( CLOCK_REALTIME, &ts );
      if (val != 0) {
        PMS_SHOW_ERROR("PMS_WaitOnSemaphore: clock_gettime failed\n");
      }
  }

  ts.tv_sec += (nMilliSeconds/1000);
  ts.tv_nsec += (nMilliSeconds%1000)*1000000;
  if( ts.tv_nsec > 1000000000L ) {
    ts.tv_sec++;
    ts.tv_nsec = ts.tv_nsec % 1000000000L;
  }
#endif

  do {
#ifdef MACOSX
    /* TODO: no sem_timedwait() on Mac OS X */
    val = sem_wait(sem);
#else
#ifdef __NetBSD__
    /* TODO: no sem_timedwait() on NetBSD 6.0 */
    val = sem_wait(sem);
#else
    val = sem_timedwait(sem,&ts);
#endif
#endif
    if(errno == ETIMEDOUT)
      return 0;
  } while (val != 0 && errno == EINTR);

  return 1;
}

/*! \brief Close and free a semaphore
 *
 * UNIX/Linux
 *
 * \param[in] semaHandle Pointer returned by \c PMS_CreateSemaphore.
*/
void PMS_DestroySemaphore(void * semaHandle)
{
#ifdef MACOSX
  int res;
  mac_sema_desc_t *macsem = (mac_sema_desc_t*)semaHandle;
  char name[SEM_NAME_LEN];

  sprintf(name, "pms-%d-%d", getpid(), macsem->semno);
  res = sem_unlink(name);
  res = sem_close(macsem->sem);
  OSFree(macsem,PMS_MemoryPoolPMS);
  sem_count--;
#else
  sem_t * sem = (sem_t*)semaHandle;

  while (sem_destroy(sem)) {
    if (sem_post(sem)) break;
  }
  /* No option to return an error - just free sem and hope. */
  OSFree(sem,PMS_MemoryPoolPMS);
#endif
}

/**********************************************
 * CRITICAL SECTION Functions.
 **********************************************/

/*! \brief Create and initialize a critical section
 *
 * UNIX/Linux
 *
 * \return Pointer to critical section created and initialized (casted to \c void *).
*/
void * PMS_CreateCriticalSection(void)
{
  pthread_mutex_t * ptCS;          /* Mutex for critical section */
  pthread_mutexattr_t mutexattr;   /* Mutex attribute variable */

  ptCS = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  if(!ptCS) {
    PMS_SHOW_ERROR("Failed to allocation critical section structure\n");
    return NULL;
  }

  /* Initialize the attributes set */
  pthread_mutexattr_init(&mutexattr);

#ifdef MACOSX
  /* Set the mutex as a normal mutex */
  pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
#else
#ifdef __NetBSD__
  /* Set the mutex as a normal mutex */
  pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
#else
  /* Set the mutex as a fast mutex */
  pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
#endif

  /* create the mutex with the attributes set */
  pthread_mutex_init(ptCS, &mutexattr);

  /* After initializing the mutex, the attribute can be destroyed */
  pthread_mutexattr_destroy(&mutexattr);

  return (void*)ptCS;
}

/*! \brief Enter a critical section
 *
 * UNIX/Linux
 *
 * \param[in] ptCritical_Section Critical section to lock.
*/
void PMS_EnterCriticalSection(void * ptCritical_Section)
{
  pthread_mutex_t *ptCS = (pthread_mutex_t *)ptCritical_Section;
  pthread_mutex_lock(ptCS);
}

/*! \brief Leave a critical section
 *
 * UNIX/Linux
 *
 * \param[in] ptCritical_Section Pointer to critical section created by \c PMS_CreateCriticalSection.
*/
void PMS_LeaveCriticalSection(void * ptCritical_Section)
{
  pthread_mutex_t *ptCS = (pthread_mutex_t *)ptCritical_Section;
  pthread_mutex_unlock(ptCS);
}

/*! \brief Close and free critical section
 *
 * UNIX/Linux
 *
 * \param[in] ptCritical_Section Pointer to a critical section created by \c PMS_CreateCriticalSection.
*/
void PMS_DestroyCriticalSection(void * ptCritical_Section)
{
  pthread_mutex_t *ptCS = (pthread_mutex_t *)ptCritical_Section;
  pthread_mutex_destroy(ptCS);
  free(ptCS);
}
/**********************************************
 * HDD Directoty Search Functions Functions.
 **********************************************/
void *PMS_Initiate_DirectorySearch(char *szDir )
{
     /* no action required */
     return NULL;
}

int PMS_Run_DirectorySearch(char *szDir, void *hFind, char *pFileName)
{
struct dirent **namelist;
int filecount;
int i;
int result = FALSE;

  filecount = scandir(szDir, &namelist, 0, alphasort);
  if(filecount > 0)
  {
    for(i = 0; i < filecount; i++)
    {
      if(namelist[i]->d_name[0] != '.')
      {
        sprintf(pFileName,"%s/%s", szDir, namelist[i]->d_name);
	result = TRUE;
        break;
      }
    }
    while(filecount--)
    {
      free(namelist[filecount]);
    }
    free(namelist);
  }
  return result;
}

void PMS_Stop_DirectorySearch(void *hFind)
{
   /* no action required */
}
