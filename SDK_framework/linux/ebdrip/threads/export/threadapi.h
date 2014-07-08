/** \file
 * \ingroup THREADS
 *
 * $HopeName: HQNthreads_api!threadapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This header file provides the definition of the pthread API.
 */

#ifndef __THREADAPI_H__
#define __THREADAPI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* bug 2001734, to fix the compilation error in core\swstart.c as
   not sure why arm9/thradx will require pthread dll
*/
#if !defined(__ghs__) && !defined(__CC_ARM)
#  include <pthread.h>  /* We need all the pthread types. On Linux and
                           MacOS X, we use the functions directly. */
#  if defined(MACINTOSH) || defined(UNIX)
#    include <errno.h>    /* pthreads needs errno for ETIMEDOUT */
#  endif /* MACINTOSH || UNIX */
#endif

#ifdef PTW32_CDECL
#  define THREADCALL PTW32_CDECL
#else
#  define THREADCALL
#endif

extern HqBool THREADCALL load_pthreads_dll(const char *dll_name,
                                           char **error_string) ;
extern HqBool THREADCALL unload_pthreads_dll(void) ;

struct timespec ;
struct HqU32x2 ;

/* -------------------------------------------------------------------------- */
/* Define the pthread API structure.

   We always need one of these to publish via RDR, but we use it for
   indirection in the skin too for symmetry. This means skin compounds that are
   linked to the Core DLL will link to the Core's indirection pointer, while
   other Skin compounds link to the Skin's. We therefore don’t need two
   different headers for Skin and CoreDLL compounds.
*/

/* Thread API version sw_pthread_api_20111021 is a strict superset of the
 * functionality in the sw_pthread_api_200071026 version of the thread
 * interface. This is deliberate choice, and the layout of the 20071026
 * version is a prefix of the 20111021 version so a pointer to the latter may
 * be used where a instance of the former is required.
 */

typedef struct {
  /* pthread calls */
  int (THREADCALL *attr_init)            (pthread_attr_t*);
  int (THREADCALL *attr_destroy)         (pthread_attr_t*);
  int (THREADCALL *attr_setdetachstate)  (pthread_attr_t*, int);
  int (THREADCALL *create)               (pthread_t*, const pthread_attr_t*,
                                       void* (*start) (void*), void*);
  int (THREADCALL *join)                 (pthread_t , void **);
  int (THREADCALL *mutexattr_init)       (pthread_mutexattr_t*);
  int (THREADCALL *mutexattr_destroy)    (pthread_mutexattr_t*);
  int (THREADCALL *mutexattr_getpshared) (const pthread_mutexattr_t*, int *);
  int (THREADCALL *mutexattr_setpshared) (pthread_mutexattr_t*, int);
  int (THREADCALL *mutexattr_settype)    (pthread_mutexattr_t*, int);
  int (THREADCALL *mutexattr_gettype)    (const pthread_mutexattr_t*, int *);
  int (THREADCALL *mutex_init)           (pthread_mutex_t*,
                                       const pthread_mutexattr_t*);
  int (THREADCALL *mutex_destroy)        (pthread_mutex_t*);
  int (THREADCALL *mutex_lock)           (pthread_mutex_t*);
  int (THREADCALL *mutex_trylock)        (pthread_mutex_t*);
  int (THREADCALL *mutex_unlock)         (pthread_mutex_t*);
  int (THREADCALL *cond_init)            (pthread_cond_t*,
                                       const pthread_condattr_t*);
  int (THREADCALL *cond_destroy)         (pthread_cond_t*);
  int (THREADCALL *cond_wait)            (pthread_cond_t*, pthread_mutex_t*);
  int (THREADCALL *cond_timedwait)       (pthread_cond_t*, pthread_mutex_t*,
                                       const struct timespec*);
  int (THREADCALL *cond_signal)          (pthread_cond_t*);
  int (THREADCALL *cond_broadcast)       (pthread_cond_t*);

  /* Internal stuff */
  void (THREADCALL *set_thread_name)     (const char *name);

  /* The this-structure-has-been-filled-in flag */
  HqBool valid ;

  /** \brief Get the representation of time at given offset in the future from
   * the current time, as a time since the epoch.  All times are in
   * microsecond units.
   *  \param time   on  invocation, \c *time contains the offset into the future.
   * On completion, \c *time is updated to represent that future time relative
   * to the epoch. */
  void (THREADCALL *time_from_now)        (struct HqU32x2 *time);

  int (THREADCALL *key_create)           (pthread_key_t*,
                                       void (*destructor) (void*));
  int (THREADCALL *key_delete)           (pthread_key_t);
  int (THREADCALL *setspecific)          (pthread_key_t, const void*);
  void *(THREADCALL *getspecific)        (pthread_key_t);

} sw_pthread_api_20111021 ;

typedef struct {
  /* pthread calls */
  int (THREADCALL *attr_init)            (pthread_attr_t*);
  int (THREADCALL *attr_destroy)         (pthread_attr_t*);
  int (THREADCALL *attr_setdetachstate)  (pthread_attr_t*, int);
  int (THREADCALL *create)               (pthread_t*, const pthread_attr_t*,
                                       void* (*start) (void*), void*);
  int (THREADCALL *join)                 (pthread_t , void **);
  int (THREADCALL *mutexattr_init)       (pthread_mutexattr_t*);
  int (THREADCALL *mutexattr_destroy)    (pthread_mutexattr_t*);
  int (THREADCALL *mutexattr_getpshared) (const pthread_mutexattr_t*, int *);
  int (THREADCALL *mutexattr_setpshared) (pthread_mutexattr_t*, int);
  int (THREADCALL *mutexattr_settype)    (pthread_mutexattr_t*, int);
  int (THREADCALL *mutexattr_gettype)    (const pthread_mutexattr_t*, int *);
  int (THREADCALL *mutex_init)           (pthread_mutex_t*,
                                       const pthread_mutexattr_t*);
  int (THREADCALL *mutex_destroy)        (pthread_mutex_t*);
  int (THREADCALL *mutex_lock)           (pthread_mutex_t*);
  int (THREADCALL *mutex_trylock)        (pthread_mutex_t*);
  int (THREADCALL *mutex_unlock)         (pthread_mutex_t*);
  int (THREADCALL *cond_init)            (pthread_cond_t*,
                                       const pthread_condattr_t*);
  int (THREADCALL *cond_destroy)         (pthread_cond_t*);
  int (THREADCALL *cond_wait)            (pthread_cond_t*, pthread_mutex_t*);
  int (THREADCALL *cond_timedwait)       (pthread_cond_t*, pthread_mutex_t*,
                                       const struct timespec*);
  int (THREADCALL *cond_signal)          (pthread_cond_t*);
  int (THREADCALL *cond_broadcast)       (pthread_cond_t*);

  /* Internal stuff */
  void (THREADCALL *set_thread_name)     (const char *name);

  /* The this-structure-has-been-filled-in flag */
  HqBool valid ;
} sw_pthread_api_20071026 ;

/* -------------------------------------------------------------------------- */
/* The symmetrical model: Indirection through an RDR-supplied API pointer */

/* Skin and Core both have their own version of this API pointer. The Skin's
   points to the API structure at build time, the Core's is found via RDR at
   run time during DLL initialisation (for example).
*/
extern sw_pthread_api_20111021 * pthread_api ;

/* -------------------------------------------------------------------------- */
/* Function name spoofing.

   For simplicity and symmetry, these function calls are actually indirected
   through an API structure pointed to by the above API pointer. This is hidden
   from the programmer by this series of macros.

   These definitions are hidden from Visual Studio so that the original function
   signatures are visible to Intellisense - define FOOL_INTELLISENSE in your
   project settings to switch them off. When the API implementor includes this
   file and wants access to the real function addresses it defines
   PTHREAD_IMPLEMENTOR similarly.
*/
#if !defined(FOOL_INTELLISENSE) && !defined(PTHREAD_IMPLEMENTOR)

#undef pthread_attr_init
#define pthread_attr_init              pthread_api->attr_init
#undef pthread_attr_destroy
#define pthread_attr_destroy           pthread_api->attr_destroy
#undef pthread_attr_setdetachstate
#define pthread_attr_setdetachstate    pthread_api->attr_setdetachstate
#undef pthread_create
#define pthread_create                 pthread_api->create
#undef pthread_join
#define pthread_join                   pthread_api->join
#undef pthread_mutexattr_init
#define pthread_mutexattr_init         pthread_api->mutexattr_init
#undef pthread_mutexattr_destroy
#define pthread_mutexattr_destroy      pthread_api->mutexattr_destroy
#undef pthread_mutexattr_getpshared
#define pthread_mutexattr_getpshared   pthread_api->mutexattr_getpshared
#undef pthread_mutexattr_setpshared
#define pthread_mutexattr_setpshared   pthread_api->mutexattr_setpshared
#undef pthread_mutexattr_settype
#define pthread_mutexattr_settype      pthread_api->mutexattr_settype
#undef pthread_mutexattr_gettype
#define pthread_mutexattr_gettype      pthread_api->mutexattr_gettype
#undef pthread_mutex_init
#define pthread_mutex_init             pthread_api->mutex_init
#undef pthread_mutex_destroy
#define pthread_mutex_destroy          pthread_api->mutex_destroy
#undef pthread_mutex_lock
#define pthread_mutex_lock             pthread_api->mutex_lock
#undef pthread_mutex_trylock
#define pthread_mutex_trylock          pthread_api->mutex_trylock
#undef pthread_mutex_unlock
#define pthread_mutex_unlock           pthread_api->mutex_unlock
#undef pthread_cond_init
#define pthread_cond_init              pthread_api->cond_init
#undef pthread_cond_destroy
#define pthread_cond_destroy           pthread_api->cond_destroy
#undef pthread_cond_wait
#define pthread_cond_wait              pthread_api->cond_wait
#undef pthread_cond_timedwait
#define pthread_cond_timedwait         pthread_api->cond_timedwait
#undef pthread_cond_signal
#define pthread_cond_signal            pthread_api->cond_signal
#undef pthread_cond_broadcast
#define pthread_cond_broadcast         pthread_api->cond_broadcast
#undef pthread_key_create
#define pthread_key_create             pthread_api->key_create
#undef pthread_key_delete
#define pthread_key_delete             pthread_api->key_delete
#undef pthread_setspecific
#define pthread_setspecific            pthread_api->setspecific
#undef pthread_getspecific
#define pthread_getspecific            pthread_api->getspecific
#define set_thread_name                pthread_api->set_thread_name
#define get_time_from_now              pthread_api->time_from_now

#endif

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* !__THREADAPI_H__ */
