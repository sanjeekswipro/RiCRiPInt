/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!src:context.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core RIP context handle and function definitions.
 */

#include "core.h"
#include "swerrors.h"
#include "mlock.h"
#include "coreinit.h"
#include "swstart.h"
#include "render.h" /* inputpage */


/* Thread-local vars
 * =================
 */
/** \todo ajcd 2011-02-15: Review requirements for these. Now that the only
    TLS is the core context, we may want the wrapped interface to just
    support "get the TLS value" and "set the TLS value". This would allow
    more efficiency for TLS handling in the core, at the expense of other
    users of the wrapped interface. */
#ifdef WIN32
/* using the Visual C/Windows specific TLS mechanism results in superior
   performance over pthread.
   Platform specific code probably needs a better home.
 */

int NativeTlsAlloc(unsigned long *var);
int NativeTlsFree(unsigned long var);
int NativeTlsSetValue(unsigned long var, void * val);
void * NativeTlsGetValue(unsigned long var);


#define THREAD_LOCAL_DEFINE(type, var) \
  static unsigned long var##_key;

#define THREAD_LOCAL_INIT(var) MACRO_START \
  int _res = NativeTlsAlloc(&var##_key);  \
  UNUSED_PARAM(int, _res) ; \
  HQASSERT( _res != 0xFFFFFFFFul, "TlsAlloc failed"); \
MACRO_END

#define THREAD_LOCAL_FINISH(var) MACRO_START \
  int _res = NativeTlsFree(var##_key); \
  UNUSED_PARAM(int, _res) ; \
  HQASSERT(_res != 0, "TlsFree failed"); \
MACRO_END

#define THREAD_LOCAL_SET(var, value) MACRO_START \
  int _res = NativeTlsSetValue(var##_key, (void*)(intptr_t)(value)); \
  UNUSED_PARAM(int, _res) ; \
  HQASSERT(_res !=0 , "TlsSetValue failed"); \
MACRO_END

#define THREAD_LOCAL_GET(var, type) \
  ((type)(intptr_t)NativeTlsGetValue(var##_key))

#else

#define THREAD_LOCAL_DEFINE(type, var) \
  static pthread_key_t var##_key;

#define THREAD_LOCAL_INIT(var) MACRO_START \
  int _res = pthread_key_create(&var##_key, NULL); \
  UNUSED_PARAM(int, _res) ; \
  HQASSERT(_res == 0, "pthread_key_create failed"); \
MACRO_END

#define THREAD_LOCAL_FINISH(var) MACRO_START \
  int _res = pthread_key_delete(var##_key); \
  UNUSED_PARAM(int, _res) ; \
  HQASSERT(_res == 0, "pthread_key_delete failed"); \
MACRO_END

#define THREAD_LOCAL_SET(var, value) MACRO_START \
  int _res = pthread_setspecific(var##_key, (void *)(value)); \
  UNUSED_PARAM(int, _res) ; \
  HQASSERT(_res == 0, "pthread_setspecific failed"); \
MACRO_END

#define THREAD_LOCAL_GET(var, type) \
  ((type)pthread_getspecific(var##_key))

#endif /* !WIN32  */


/** Core context for RIP's initial context. In the case of multi-threaded
    RIPs, the context is accessed through a thread-local variable. */
static corecontext_t initial_context ;

/** Initial error context. */
static error_context_t error_context ;

THREAD_LOCAL_DEFINE(corecontext_t *, per_thread_context) ;

/** Fast access pointer to singular interpreter context. */
static corecontext_t *interp_cc;

/** Accessor function for per-thread context. */
corecontext_t *get_core_context(void)
{
  return THREAD_LOCAL_GET(per_thread_context, corecontext_t *) ;
}

/*
 * Get the core context, but know we will only ever get called from the
 * interpreter thread, so can just return the cached interpreter context without
 * having to make the slow THREAD_LOCAL_GET call.
 */
corecontext_t *get_core_context_interp(void)
{
  HQASSERT(interp_cc != NULL,
           "interpeter thread context pointer NULL");
  HQASSERT(interp_cc->is_interpreter,
           "interpreter thread context not for interpreter");
  HQASSERT(get_core_context()->is_interpreter,
           "interpreter thread TLS core context not for interpreter");

  return interp_cc;
}

void set_core_context(corecontext_t *context)
{
  HQASSERT(context != NULL, "No core context to set") ;

  if (context->is_interpreter) {
    /* For now, we can only have a single interpreter context. Later, we may
     * allow other threads doing interpreter tasks. They may need to be marked
     * as interpreter threads, in which case this will need reconsidered. */
    interp_cc = context ;
  }
  THREAD_LOCAL_SET(per_thread_context, context) ;
}

void clear_core_context(void)
{
  THREAD_LOCAL_SET(per_thread_context, NULL);
}

/* Initialiser for context globals */
void init_C_globals_context(void)
{
  error_context_t errinit = { 0 } ;
  corecontext_t coreinit = { 0 } ;

  error_context = errinit ;

  initial_context = coreinit ;
  initial_context.error = &error_context ;
  initial_context.is_interpreter = TRUE ;
  interp_cc = &initial_context ;
}

Bool context_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  THREAD_LOCAL_INIT(per_thread_context) ;
  THREAD_LOCAL_SET(per_thread_context, &initial_context) ;

  return TRUE ;
}

/** The current thread context is set again in this function, because it may
    be called from a different thread than \c context_swinit. */
Bool context_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  THREAD_LOCAL_SET(per_thread_context, &initial_context) ;

  CoreContext.page = inputpage_lock(); inputpage_unlock() ;

  return TRUE ;
}

void context_finish(void)
{
  THREAD_LOCAL_FINISH(per_thread_context) ;
}

/*
Log stripped */
