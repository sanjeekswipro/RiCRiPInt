/* ============================================================================
 * $HopeName: HQNthreads!unix:src:ggsthreads.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */

#define _XOPEN_SOURCE 600 /* Linux needs this for pthread_mutexattr_settype */

#include "std.h"

#include <sys/time.h> /* gettimeofday */

#ifdef linux
#include <sys/prctl.h>
#endif

#define PTHREAD_IMPLEMENTOR
#include "threadapi.h"

/* ========================================================================== */

/* On UNIX, there is no DLL to load or unload. */
HqBool THREADCALL unload_pthreads_dll(void)
{
  return TRUE ;
}

HqBool THREADCALL load_pthreads_dll(const char *dll_name,
                                    char **error_string)
{
  UNUSED_PARAM(const char*, dll_name) ;

  *error_string = NULL ;

  return TRUE ;
}

static void THREADCALL set_thread_name(const char *name)
{
#ifdef DEBUG_BUILD

#if defined(linux) && defined(PR_SET_NAME)
  /* The man page says this sets the process name, but other sources indicate
     this sets the thread name. It does indeed set the thread name (as seen
     by ps -Hcx), but gdb doesn't yet display those names. It appears that
     patches were submitted to gdb in Jan 2011 to show the name set here. */
  prctl(PR_SET_NAME, name, 0, 0, 0) ;
#else
  UNUSED_PARAM(const char*, name) ;
#endif

#else
  UNUSED_PARAM(const char*, name) ;
#endif
}

static void THREADCALL ggs_time_from_now( HqU32x2 *time)
{
  struct timeval tv;

  if ( gettimeofday(&tv, NULL) < 0 ) {
    HQFAIL("gettimeofday FAILED");
    /* infinite wait if failed */
    time->high = 0xffffffffu; /* Infinite wait time */
    time->low  = 0; /* Allow minute increment to avoid overflow */
  } else {
    uint64 usecs = /* need 64bits for calculation */
      (((uint64)tv.tv_sec)*1000*1000 + tv.tv_usec);
    HqU32x2 u32x2 ;

    u32x2.low = (uint32)(usecs);
    u32x2.high = (uint32)(usecs >> 32);
    HqU32x2Add(time, &u32x2, time) ;
  }

  return ;
}

#ifdef __NetBSD__

/* pthread_mutexattr_setpshared and pthread_mutexattr_getpshared are not
 * supported on NetBSD 6.0 so need to be stubbed for our API to pthreads.
 *
 * Return error as functionality is not being supported.
 */
int pthread_mutexattr_setpshared(
  pthread_mutexattr_t *attr,
  int pshared)
{
  UNUSED_PARAM(const pthread_mutexattr_t *, attr);
  UNUSED_PARAM(int, pshared);

  return (EINVAL);
}

int pthread_mutexattr_getpshared(
  const pthread_mutexattr_t *attr,
  int *pshared)
{
  UNUSED_PARAM(const pthread_mutexattr_t *, attr);
  UNUSED_PARAM(int *, pshared);

  return (EINVAL);
}

#endif /* __NetBSD__ */

/* ========================================================================== */
/* An API structure for registration with RDR (though not by us) */

/* This will be defined as an RDR API by the RDR system once it has started */
static sw_pthread_api_20111021 api = {
  pthread_attr_init,
  pthread_attr_destroy,
  pthread_attr_setdetachstate,
  pthread_create,
  pthread_join,
  pthread_mutexattr_init,
  pthread_mutexattr_destroy,
  pthread_mutexattr_getpshared,
  pthread_mutexattr_setpshared,
  pthread_mutexattr_settype,
  pthread_mutexattr_gettype,
  pthread_mutex_init,
  pthread_mutex_destroy,
  pthread_mutex_lock,
  pthread_mutex_trylock,
  pthread_mutex_unlock,
  pthread_cond_init,
  pthread_cond_destroy,
  pthread_cond_wait,
  pthread_cond_timedwait,
  pthread_cond_signal,
  pthread_cond_broadcast,
  set_thread_name,
  TRUE,
  ggs_time_from_now,
  pthread_key_create,
  pthread_key_delete,
  pthread_setspecific,
  pthread_getspecific,
} ;

/* This is the API pointer that will be used by the skin */
sw_pthread_api_20111021 * pthread_api = &api ;

