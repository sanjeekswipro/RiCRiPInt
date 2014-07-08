/** \file
 * \ingroup RDR
 *
 * $HopeName: SWrdr!src:event.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This file provides the Event System API.
 *
 * The Event API is primarily used to communicate between the skin and core,
 * or in general, between multiple Issuers and Handlers.
 */

#define EVENT_IMPLEMENTOR
#include "rdrpriv.h"
#include "apis.h"     /* RDR_API_EVENT */

/* -------------------------------------------------------------------------- */
/* Define our API through RDR

   The Event API is published through RDR so that only the RDR API needs to be
   passed to the core.
*/

static sw_event_api_20110330 api = {
  FALSE,
  SwRegisterHandler,
  SwDeregisterHandler,
  SwEvent,
  SwEventTail,
  SwRegisterHandlers,
  SwDeregisterHandlers,
  SwSafeDeregisterHandler
} ;

/* This is the API pointer that will be used by the skin */
sw_event_api_20110330 * event_api = &api ;

/* ========================================================================== */
#ifdef DEBUG_BUILD

/* Unit testing - test registration, reprioritisation and early exit */

enum {
  EVENT_EVENT_TEST = EVENT_EVENT + 99
} ;

typedef struct ctx {
  struct ctx * self ;
  int32        mul ;
} test_ctx ;

typedef struct {
  int32         value ;
  int32         early ;
  sw_event_handler * suicide ;
  sw_event_type type ;
  size_t        length ;
} test_struct ;

static sw_event_result HQNCALL testfn(void * context, sw_event * event)
{
  test_struct * test ;
  int32 mul ;

  if (context == 0 || event == 0)
    return FAILURE(SW_EVENT_ERROR) ;

  test = (test_struct *) event->message ;
  mul = * (int32 *) context ;

  /* These should be identical to the SwEvent call */
  test->type   = event->type ;
  test->length = event->length ;

  /* Do the non-commutative transformation */
  test->value = test->value * mul + 1 ;

  /* Deregister ourself (note test for correct context) */
  if (test->suicide && context == test->suicide->context) {
    /* We expect this to report IN_USE because we are threaded */
    if (SwDeregisterHandler(EVENT_EVENT_TEST, test->suicide) == SW_RDR_ERROR_IN_USE)
      return SW_EVENT_HANDLED ;
    else
      return SW_EVENT_ERROR ;
  }

  /* Early termination */
  if (--test->early == 0)
    return SW_EVENT_HANDLED ;

  return SW_EVENT_CONTINUE ;
}

/* -------------------------------------------------------------------------- */

static HqBool unit_test(void)
{
  int32 evnum = EVENT_EVENT_TEST ;
  static int32 mul1 = 4 ;
  static int32 mul2 = 8 ;
  static int32 mul3 = 64 ;
  static test_struct test = {0} ;
  sw_event_handler fn1 = {testfn, &mul1, 0} ;
  sw_event_handler fn2 = {testfn, &mul2, 0} ;
  sw_event_handler fn3 = {testfn, &mul3, 0} ;

  /* register them in a strange order - should end up ->fn1->fn2->fn3 */
  if (SwRegisterHandler(evnum, &fn1, 100) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ;
  if (SwRegisterHandler(evnum, &fn3, -100) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ;
  if (SwRegisterHandler(evnum, &fn2, 0) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ;

  /* Run a value through all three in that order and check the result */
  test.value = 1 ;
  test.early = 4 ;  /* no 'handled' expected */
  if (SwEvent(evnum, &test, 0) != SW_EVENT_UNHANDLED)
    return FAILURE(FALSE) ; /* wrong return value */
  if (test.type != evnum || test.length != 0)
    return FAILURE(FALSE) ; /* wrong values delivered to handlers */
  if (test.early != 1)
    return FAILURE(FALSE) ; /* wrong number of handlers called */
  if (test.value != ((1*4+1)*8+1)*64+1)
    return FAILURE(FALSE) ; /* handlers called in wrong order */

  /* Reprioritisation which doesn't change ordering */
  if (SwRegisterHandler(evnum, &fn2, -1) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ; /* failed to reprioritise */
  test.value = 1 ;
  test.early = 4 ;  /* no 'handled' expected */
  if (SwEvent(evnum, &test, 0) != SW_EVENT_UNHANDLED)
    return FAILURE(FALSE) ; /* wrong return value */
  if (test.early != 1)
    return FAILURE(FALSE) ; /* wrong number of handlers called */
  if (test.value != ((1*4+1)*8+1)*64+1)
    return FAILURE(FALSE) ; /* handlers called in wrong order */

  /* Reprioritise */
  if (SwRegisterHandler(evnum, &fn3, 50) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ; /* failed to reprioritise */
  /* Test new order */
  test.value = 1 ;
  test.early = 3 ; /* Expect 'handled', but all three to be called */
  if (SwEvent(evnum, &test, 1) != SW_EVENT_HANDLED)
    return FAILURE(FALSE) ; /* wrong return value */
  if (test.length != 1)
    return FAILURE(FALSE) ; /* length parameter not passed correctly */
  if (test.early != 0)
    return FAILURE(FALSE) ; /* all three should have been called */
  if (test.value != ((1*4+1)*64+1)*8+1)
    return FAILURE(FALSE) ; /* handlers called in wrong order */

  /* Early termination */
  test.value = 1 ;
  test.early = 1 ;
  if (SwEvent(evnum, &test, 0) != SW_EVENT_HANDLED)
    return FAILURE(FALSE) ; /* wrong return value */
  if (test.early != 0)
    return FAILURE(FALSE) ; /* wrong number of handlers called */
  if (test.value != 1*4+1)
    return FAILURE(FALSE) ; /* wrong handler called */

  /* Deregister a handler */
  if (SwDeregisterHandler(evnum, &fn3) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ; /* deregistration problem */
  test.value = 1 ;
  test.early = 3 ; /* both to be called, UNHANDLED returned */
  if (SwEvent(evnum, &test, 0) != SW_EVENT_UNHANDLED)
    return FAILURE(FALSE) ; /* wrong return value */
  if (test.value != (1*4+1)*8+1)
    return FAILURE(FALSE) ; /* wrong order */

  /* Cause a Handler to deregister itself */
  test.early = 0 ;
  test.suicide = &fn2 ;
  if (SwEvent(evnum, &test, 0) != SW_EVENT_HANDLED)
    return FAILURE(FALSE) ; /* unexpected result */
  test.suicide = NULL ;

  /* Deregister remaining handler */
  if (SwDeregisterHandler(evnum, &fn1) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ; /* deregistration problem */

  return TRUE ;
}
#endif
/* ========================================================================== */

HqnIdent next_id = 0 ;

int HQNCALL event_start(void)
{
  next_id = 0 ;
  api.valid = FALSE ;

  if (rdr_api == 0 || !rdr_api->valid) {
    HQFAIL("RDR not initialised") ;
    return FAILURE(FALSE) ;
  }

  api.valid = TRUE ;

#ifdef DEBUG_BUILD
  if (!unit_test())
    return FAILURE(FALSE) ;
#endif

  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_EVENT, 20110330,
                    &api, sizeof(api), 0) != SW_RDR_SUCCESS)
    return FAILURE(FALSE) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */

void HQNCALL event_end(void)
{
  HQASSERT(rdr_api && rdr_api->valid, "RDR ended too soon for Event") ;

  (void) SwDeregisterRDR(RDR_CLASS_API, RDR_API_EVENT, 20110330,
                         &api, sizeof(api)) ;

  api.valid = FALSE ;
}

/* ========================================================================== */
/* The Event System */

sw_rdr_result HQNCALL SwRegisterHandler(sw_event_type      type,
                                        sw_event_handler * handler,
                                        sw_event_priority  priority)
{
  if (!handler || !handler->handler)
    return SW_RDR_ERROR_SYNTAX ;

  HQASSERT(handler->reserved == 0,
           "Handler does not have reserved field set to zero") ;

  return SwRegisterRDR(RDR_CLASS_EVENT, type, 0, handler, sizeof(handler),
                       priority) ;
}

/* -------------------------------------------------------------------------- */

sw_rdr_result HQNCALL SwRegisterHandlers(sw_event_handlers * handlers,
                                         int                 count)
{
  int i, done = 0 ;
  sw_rdr_result result = SW_RDR_SUCCESS ;

  if (!handlers || count < 0)
    return SW_RDR_ERROR_SYNTAX ;

  for (i = 0 ; i < count && result == SW_RDR_SUCCESS ; i++) {
    result = SwRegisterHandler(handlers[i].type,
                               (sw_event_handler *)(&handlers[i]),
                               handlers[i].priority) ;
    if (result == SW_RDR_SUCCESS)
      ++done ;
  }

  if (result != SW_RDR_SUCCESS)
    (void) SwDeregisterHandlers(handlers, done) ;

  return result ;
}

/* -------------------------------------------------------------------------- */

sw_rdr_result HQNCALL SwDeregisterHandler(sw_event_type      type,
                                          sw_event_handler * handler)
{
  if (!handler || !handler->handler)
    return SW_RDR_ERROR_SYNTAX ;

  return SwDeregisterRDR(RDR_CLASS_EVENT, type, 0, handler, sizeof(handler)) ;
}

/* -------------------------------------------------------------------------- */

sw_rdr_result HQNCALL SwDeregisterHandlers(sw_event_handlers * handlers,
                                           int                 count)
{
  sw_rdr_result result = SW_RDR_SUCCESS ;
  while (count > 0) {
    sw_rdr_result res ;
    --count ;
    res = SwSafeDeregisterHandler(handlers[count].type,
                                  (sw_event_handler *)&handlers[count]) ;
    if (result == SW_RDR_SUCCESS)
      result = res ;
  }

  return result ;
}

/* -------------------------------------------------------------------------- */

sw_rdr_result HQNCALL SwSafeDeregisterHandler(sw_event_type      type,
                                              sw_event_handler * handler)
{
  sw_rdr_result result ;
  HqBool wait = FALSE ;
  int sanity = 0 ;

  do {
    result = SwDeregisterHandler(type, handler) ;

    if (result == SW_RDR_ERROR_IN_USE) {
      /* We could sleep the thread here, but that would require the system
         time and is an extraordinarily rare thing to have to do anyway.

         Having said that, a sanity-check timeout would save us from oblivion
         if someone insists on calling this from the Handler in question...
      */
      wait = TRUE ;
      ++sanity ;
      HQASSERT(sanity < 1e6, "Is Handler trying to SafeDeregister itself?") ;
    }

  } while (result == SW_RDR_ERROR_IN_USE) ;

  /* If we had to wait, SW_RDR_ERROR_UNKNOWN means eventual success */
  if (wait && result == SW_RDR_ERROR_UNKNOWN)
    result = SW_RDR_SUCCESS ;

  return result ;
}

/* -------------------------------------------------------------------------- */
/* Send an event down the claimant chain */

sw_event_result HQNCALL SwEvent(sw_event_type type,
                                void *        message,
                                size_t        length)
{
  sw_event ev = {0} ;

  /* Prepare the event message */
  ev.type    = type ;
  ev.message = message ;
  ev.length  = length ;

  ev.context = 0 ;        /* No parent iterator, so begin the process */

  return SwEventTail(&ev) ;
}

/* -------------------------------------------------------------------------- */
/* Tail call recursion.
 *
 * This is called by claimants wishing to act upon the result of calling the
 * rest of the chain, and initially by SwEvent() to begin the process.
 */
sw_event_result HQNCALL SwEventTail(sw_event * ev)
{
  sw_rdr_iterator    it = {ITERATOR,CHECK_CLASS|CHECK_TYPE|ITERATE_PRIORITIES} ;
  sw_event_result    result = SW_EVENT_CONTINUE ;
  void *             vpHandler ;
  sw_event_context * parent ;

  /* There must be an event! */
  if (!ev)
    return FAILURE(SW_RDR_ERROR_SYNTAX) ;

  /* Preserve caller's context. */
  parent = ev->context ;

  pthread_mutex_lock(&rdr_mt) ;
  if (parent) {
    /* Copy parent iterator for tail recursion (->next set by link_iterator()).
       Casting is to hide from Handlers the fact that the event context is
       actually an RDR iterator. We don't want Handlers to be able to (easily)
       access other Handlers via this iterator. */
    it = * (sw_rdr_iterator *) parent ;
  } else {
    /* New iterator */
    it.Class = RDR_CLASS_EVENT ;
    it.Type  = ev->type ;

    /* New Event ID */
    if (++next_id == 0)
      ++next_id ;       /* We never use ID zero */
    ev->id = next_id ;  /* These do eventually wrap around and are reused */
  }

  /* Make iterator live and set up new context for the tail call */
  link_iterator(&it, &live_list) ;
  pthread_mutex_unlock(&rdr_mt) ;

  /* Call all interested handlers until the event is claimed or errored */
  while (result == SW_EVENT_CONTINUE &&
         SwLockNextRDR(&it, 0, 0, 0, &vpHandler, 0) == SW_RDR_SUCCESS) {
    sw_event_handler * pHandler = (sw_event_handler *) vpHandler ;

    /* Just in case previous handler corrupted the context, set every time: */
    ev->context = (void *) &it ;

    /* Call the handler */
    result = (*pHandler->handler)(pHandler->context, ev) ;
  }

  /* Discard iterator and restore caller's context */
  pthread_mutex_lock(&rdr_mt) ;
  unlock_previous_rdr(&it) ;
  unlink_iterator(&it) ;
  pthread_mutex_unlock(&rdr_mt) ;
  ev->context = parent ;

  if (result == SW_EVENT_FORCE_UNHANDLED)
    result = SW_EVENT_UNHANDLED ;  /* which is just an alias for CONTINUE */

  return result ;
}

/* ========================================================================== */
