/** \file
 * \ingroup timeline
 *
 * $HopeName: SWtimeline!src:timeline.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This file provides the Timeline API.
 *
 * The Timeline API is used to manage, communicate and negotiate the lifespan of
 * entities; arrange timelines into hierarchies; report state, progress along and
 * extent of timelines; and attribute textual feedback and error messages to
 * timeline hierarchies.
 *
 * It makes extensive use of the Event system and is independent of the core.
 */

#include "std.h"

#ifdef LESDK

#  include <string.h>   /* memcmp & memcpy */
#  include "mem.h"

#  define tl_alloc(_size, _type) MemAlloc(_size, FALSE, FALSE)
#  define tl_free(_block, _size) MemFree(_block)

#else

#  define tl_alloc(_size, _type) malloc(_size)
#  define tl_free(_block, _size) free(_block)

#endif


#include "threadapi.h"
#include "rdrapi.h"
#include "apis.h"
#include "eventapi.h"
#define TIMELINE_IMPLEMENTOR
#include "timelineapi.h"

#ifdef DEBUG_BUILD
#include <stdio.h> /* sprintf */
#endif

/* -------------------------------------------------------------------------- */
/* Define this to prevent SetProgress from extending the Timeline extent for
   out-of-range values of progress.
*/
#undef NO_AUTO_EXTEND

/* -------------------------------------------------------------------------- */

#ifndef FAILURE
#  ifdef DEBUG_BUILD
#    define FAILURE(_val) (failure(),_val)
static void failure(void)
{
  return ;
}
#  else
#    define FAILURE(_val) (_val)
#  endif
#endif

/* -------------------------------------------------------------------------- */

typedef struct sw_timeline {  /* Timeline storage */
  struct sw_timeline * parent ;   /* Pointer to parent, or NULL */
  struct sw_timeline * child ;    /* Pointer to first child or NULL */
  struct sw_timeline * sibling ;  /* Pointer to next sibling or NULL */
  struct sw_timeline * next ;     /* Pointer to next on this hash list / NULL */

  SWMSG_TIMELINE tl ;             /* Timeline details in Event Message format */

  uint8         *newtitle ;       /* new title while events are occurring */
  size_t         newlength ;

  int            usage ;          /* usage count to prolong during events */
  HqnIdent       negotiation ;    /* the id. zeroed when a child is added */
} sw_timeline ;

/* The usage count prevents the current title from being discarded during a
   Timeline Event. There's no way of changing the Event message between
   Handlers so the title pointer will still be pointing to the title buffer in
   use at the start of the Event. Any Handler (or other thread) changing the
   title would cause that buffer to be discarded, so the usage count prevents
   that from happening until all such Events are finished.

   negotiation ensures that an ongoing end or abort negotiation knows that it
   has to start again - it is set to the negotiation id at the start of the
   process and if zero at the end the whole thing is repeated.
*/

#define UNCONST(type,var) ((type)(var))

/* -------------------------------------------------------------------------- */

/* Our mutex */
static pthread_mutex_t timeline_mt = PTHREAD_MUTEX_INITIALIZER ;

/* The hashlist of current Timelines */
#define TIMELINE_HASHES 32
static sw_timeline *timelines[TIMELINE_HASHES] = {0} ;
#define TIMELINE_HASH(_ref_) ((int)(_ref_) & (TIMELINE_HASHES - 1))

/* Discarded Timelines are freed outside the mutex */
static sw_timeline *discardable = NULL ;

/* Timeline reference */

#define TL_REF_INIT 1
#define TL_REF_INCREMENT(ref) \
  ((ref) == HQNIDENT_MAX) ? ((ref) = TL_REF_INIT) : (++(ref))

static sw_tl_ref next_ref = TL_REF_INIT ;

/* ========================================================================== */

static sw_timeline_api_20110623 timeline_api_20110623 = {
  FALSE,
  SwTimelineStart,
  SwTimelineEnd,
  SwTimelineAbort,
  SwTimelineSetTitle,
  SwTimelineGetTitle,
  SwTimelineSetExtent,
  SwTimelineSetProgress,
  SwTimelineGetProgress,
  SwTimelineGetAncestor,
  SwTimelineGetType,
  SwTimelineSetContext,
  SwTimelineGetContext,
  SwTimelineGetPriority,
  SwTimelineOfType
} ;

/* This is the API pointer used by the skin */
sw_timeline_api_20110623 * timeline_api = &timeline_api_20110623 ;

/* -------------------------------------------------------------------------- */
/* Debug - ptimelines() called as a result of a debug event */
static void lock_mutex(void) ;
static void unlock_mutex(void) ;
static sw_timeline * timeline(sw_tl_ref ref) ;

#ifdef DEBUG_BUILD
/** \brief  Return the Timeline state if known.

   \param ref  Timeline reference

   \return     The state of the Timeline if known, or TL_STATE_UNKNOWN

   This call does not issue an Event.
 */
static sw_tl_state SwTimelineGetState(sw_tl_ref ref) ;

/** \brief  Unit test of the Timeline System */

/* A structure for communication between the test and the Handler */
typedef struct {
  sw_tl_state  object ;  /* State to object to */
  int          count ;   /* event count */
  uint8 *      title ;   /* replacement title */
  size_t       length ;  /* replacement title length */

  sw_tl_ref    ref ;     /* Timeline reference from the event */
  sw_tl_state  state ;   /* State from the event */
} test_context ;


/* Record certain values, count the events and object if required.
   This Handler is registered for all the Timeline Events (as though there is
   only one amalgamated Event).
 */
static sw_event_result HQNCALL handler_fn(void * context, sw_event * ev)
{
  test_context *  test = (test_context *) context ;
  SWMSG_TIMELINE * msg = (SWMSG_TIMELINE *) ev->message ;

  /* Grab useful info */
  test->ref   = msg->ref ;
  test->state = msg->state ;
  ++test->count ;

  /* Object if required */
  if (test->object == msg->state)
    return SW_EVENT_HANDLED ;  /* object to the event */

  /* Override title if required */
  if (msg->state == TL_STATE_TITLE && test->length > 0) {
    msg->title = test->title ;
    msg->length = test->length ;
  }

  return SW_EVENT_CONTINUE ;
}


/* Unit test - try various things with the Timeline system and see if it works
   as advertised.
 */
static HqBool unit_test(void)
{
  int i ;
  sw_tl_ref mum, kid ;
  size_t length ;
  uint8 buffer[8] = {0} ;
  double start, end, progress ;
  static test_context test, reset = {TL_STATE_UNKNOWN} ;
  static sw_event_handler handler = {handler_fn, &test, 0} ;

  test = reset ;

  /* Register our Handler on all the Events (if more than one) */
  for (i = EVENT_TIMELINE_START ; i <= EVENT_TIMELINE_PROGRESS ; ++i)
    (void) SwRegisterHandler(i, &handler, 0) ;
  for (i = EVENT_TIMELINE_ENDING ; i <= EVENT_TIMELINE_ABORTED ; ++i)
    (void) SwRegisterHandler(i, &handler, 0) ;

  /* Create a Timeline and check we were informed */
  mum = SwTimelineStart(12345, 0, 0.0, 1.0, SW_TL_UNIT_NONE, 0, NULL,
                        (uint8 *)"Mum", 3) ;
  if (!mum) {
    /* Failed to create a Timeline */
    return FAILURE(FALSE) ;
  }
  if (test.count != 1 || test.ref != mum || test.state != TL_STATE_START) {
    /* Handler not called or unexpected event(s) */
    return FAILURE(FALSE) ;
  }

  /* Create a child Timeline */
  kid = SwTimelineStart(54321, mum, 0.0, 1.0, SW_TL_UNIT_NONE, 0, NULL,
                        (uint8 *)"Kid", 3) ;
  if (!kid)
    return FAILURE(FALSE) ;
  if (test.count != 2 || test.ref != kid || test.state != TL_STATE_START)
    return FAILURE(FALSE) ;

  /* Set the title */
  if (SwTimelineSetTitle(kid, (uint8*)"New", 3) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* Title change should work */
  if (test.count != 3 || test.state != TL_STATE_TITLE || test.ref != kid)
    return FAILURE(FALSE) ;  /* Handler didn't get the Title change */

  /* Override title change in a Handler */
  test.title = (uint8*)"Changed" ;
  test.length = 7 ;
  if (SwTimelineSetTitle(kid, (uint8*)"Kid", 3) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* That should have worked */
  if (test.count != 4)
    return FAILURE(FALSE) ;  /* And our Handler should have been called */
  length = SwTimelineGetTitle(kid, NULL, 0) ;
  if (length != 7)
    return FAILURE(FALSE) ;  /* We don't have the updated title */
  if (SwTimelineGetTitle(kid, buffer, 8) != 7)
    return FAILURE(FALSE) ;  /* Title is not the right length */
  if (memcmp(&buffer, test.title, 7) != 0)
    return FAILURE(FALSE) ;  /* Title seems to be wrong */
  test.length = 0 ;

  /* Set Extent, set progress, auto-extend extent with progress */
  if (SwTimelineSetExtent(mum, 0.0, 100.0) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* That should have worked */
  if (test.count != 5 || test.ref != mum || test.state != TL_STATE_EXTEND)
    return FAILURE(FALSE) ;  /* Handler wasn't called properly */
  if (SwTimelineSetProgress(mum, 0.5) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* Progress should be uncontroversial */
  if (test.count != 6 || test.ref != mum || test.state != TL_STATE_PROGRESS)
    return FAILURE(FALSE) ;  /* Handler did not get progress */
  if (SwTimelineSetProgress(mum, 150.0) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* Progress extention failed */
  if (test.count != 7 || test.ref != mum || test.state != TL_STATE_PROGRESS)
    return FAILURE(FALSE) ;  /* Handler not called */
  if (SwTimelineSetProgress(mum, 150.0) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* Duplicate progress didn't work */
  if (test.count != 7)
    return FAILURE(FALSE) ;  /* Handler should not have been called again */
  if (SwTimelineGetProgress(mum,&start,&end,&progress,NULL) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* Getting the progress failed */
  if (start != 0.0 || end != 150.0 || progress != 150.0)
    return FAILURE(FALSE) ;  /* Extent and progress not as expected */

  /* End parent - it should automatically prolong with no events */
  if (SwTimelineEnd(mum) != SW_TL_ERROR_IN_USE)
    return FAILURE(FALSE) ;  /* We were expecting that error */
  if (test.count != 7)
    return FAILURE(FALSE) ;  /* We weren't expecting ANY events */

  /* Get parent */
  if (SwTimelineGetAncestor(kid, SW_TL_TYPE_ANY) != mum)
    return FAILURE(FALSE) ;  /* Parent isn't as expected */
  if (SwTimelineGetAncestor(kid, 12345) != mum)
    return FAILURE(FALSE) ;  /* Ancestor isn't as expected */

  /* End child but get Handler to object */
  test.object = TL_STATE_ENDING ;
  if (SwTimelineEnd(kid) != SW_TL_ERROR_IN_USE)
    return FAILURE(FALSE) ;  /* Handler should have prolonged the child */
  if (test.count != 8)
    return FAILURE(FALSE) ;  /* Was Handler not called? */
  if (SwTimelineGetState(kid) != TL_STATE_START)
    return FAILURE(FALSE) ;  /* Prolonged Timelines should return to normal */

  /* Test the ancillary calls */
  if (SwTimelineGetPriority(kid) != 0)
    return FAILURE(FALSE) ;  /* Could not get priority */
  if (SwTimelineOfType(kid, 54321) != kid)
    return FAILURE(FALSE) ;  /* OfType failed to identify this Timeline */
  if (SwTimelineOfType(kid, 12345) != mum)
    return FAILURE(FALSE) ;  /* OfType very confused */
  if (SwTimelineGetState(mum) != TL_STATE_END)
    return FAILURE(FALSE) ;  /* Parent should still be waiting to end */
  if (test.count != 8)
    return FAILURE(FALSE) ;  /* Did not expect any additional events */

  /* Contexts */
  {
    int unique1 = 0, unique2 = 0 ;
    int * where ;
    if (SwTimelineSetContext(kid, 99999, &unique1) != SW_TL_SUCCESS)
      return FAILURE(FALSE) ;  /* Failed to attach context */
    if (SwTimelineSetContext(kid, 0, &unique2) != SW_TL_ERROR_SYNTAX)
      return FAILURE(FALSE) ;  /* Should not be allowed to change primary */
    if (SwTimelineSetContext(kid, 99999, &unique2) != SW_TL_SUCCESS)
      return FAILURE(FALSE) ;  /* Failed to change context */
    where = SwTimelineGetContext(kid, 99999) ;
    if (where != &unique2)
      return FAILURE(FALSE) ;  /* Failed to find the context */
    if (SwTimelineSetContext(kid, 99999, NULL) != SW_TL_SUCCESS)
      return FAILURE(FALSE) ;  /* Failed to detach context */
    where = SwTimelineGetContext(kid, 99999) ;
    if (where)
      return FAILURE(FALSE) ;  /* Should not have found something */
  }

  /* Now end it without objecting */
  test.object = TL_STATE_UNKNOWN ;
  if (SwTimelineEnd(kid) != SW_TL_SUCCESS)
    return FAILURE(FALSE) ;  /* child should have ended */
  if (SwTimelineGetState(kid) != TL_STATE_UNKNOWN)
    return FAILURE(FALSE) ;  /* child hasn't gone */
  if (SwTimelineGetState(mum) != TL_STATE_UNKNOWN)
    return FAILURE(FALSE) ;  /* parent hasn't gone */
  if (test.count != 12)
    return FAILURE(FALSE) ;  /* unexpected number of events */
  if (test.ref != mum || test.state != TL_STATE_ENDED)
    return FAILURE(FALSE) ;  /* unexpected event ordering */

  /* No timelines at this point */

  /* Deregister the Handler */
  for (i = EVENT_TIMELINE_START ; i <= EVENT_TIMELINE_PROGRESS ; ++i)
    (void) SwSafeDeregisterHandler(i, &handler) ;
  for (i = EVENT_TIMELINE_ENDING ; i <= EVENT_TIMELINE_ABORTED ; ++i)
    (void) SwSafeDeregisterHandler(i, &handler) ;

  return TRUE ;
}


static const char *tl_type(sw_tl_type type)
{
  void *name = NULL ;

  if ( SwFindRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME, type,
                 &name, NULL) != SW_RDR_SUCCESS ) {
    static char unknown[100] ;
    sprintf(unknown, "Unknown<%d>", type) ;
    name = unknown ;
  }
  return name ;
}

static const char *tl_state(sw_tl_state state)
{
  switch ( state ) {
  case TL_STATE_START:     return "START" ;
  case TL_STATE_END:       return "END" ;
  case TL_STATE_ABORT:     return "ABORT" ;
  case TL_STATE_ENDING:    return "ENDING" ;
  case TL_STATE_ABORTING:  return "ABORTING" ;
  case TL_STATE_ENDED:     return "ENDED" ;
  case TL_STATE_ABORTED:   return "ABORTED" ;
  case TL_STATE_UNKNOWN:   return "UNKNOWN" ;
  /* the following should never be seen outside an event */
  case TL_STATE_TITLE:     return "TITLE" ;
  case TL_STATE_EXTEND:    return "EXTEND" ;
  case TL_STATE_PROGRESS:  return "PROGRESS" ;
  }
  return "INVALID" ;
}

static void ptimeline(sw_timeline *tl, int indent,
                      void (HQNCALL *monitorf) (uint8 *format, ...),
                      sw_event *ev)
{
  const static char spaces[] =
    "                                                                " ;

  monitorf((uint8*)(tl->tl.title
                    ? "%.*s%s R=%d P=%d S=%s \"%s\"\n"
                    : "%.*s%s R=%d P=%d S=%s\n"),
           indent, spaces, tl_type(tl->tl.type), tl->tl.ref, tl->tl.priority,
           tl_state(tl->tl.state), tl->tl.title) ;

  if ( ev != NULL ) {
    /* Allow other Handlers to output futher information on this Timeline */
    sw_tl_debug * debug = (sw_tl_debug *) ev->message ;
    debug->tl = tl->tl.ref ;
    (void)SwEventTail(ev) ;
  }

  if (indent < sizeof(spaces))
    indent += 2 ;
  for (tl = tl->child ; tl ; tl = tl->sibling)
    ptimeline(tl, indent, monitorf, ev) ;
}

static int killed_at_start = 0 ;
static int unknown_ref = 0 ;
static int total_made = 0 ;

/* The guts of ptimelines, extracted so they can be called direct from a
   debugger without blocking on the timeline mutex (in which case ev must be 0).
 */
static void ptimelines_locked(sw_tl_ref ref,
                              void (HQNCALL *monitorf) (uint8 * format, ...),
                              sw_event * ev)
{
  sw_timeline *tl ;

  if (ref) {
    /* Just one Timeline */
    tl = timeline(ref) ;

    if (tl) {
      monitorf((uint8*)"Timeline:\n") ;
      ptimeline(tl, 2, monitorf, ev) ;
    } else {
      monitorf((uint8*)"Unknown Timeline\n") ;
    }
  } else {
    int i ;
    monitorf((uint8*)"Timelines:\n") ;

    for (i = 0 ; i < TIMELINE_HASHES ; ++i) {
      /* Look for grandees */
      for (tl = timelines[i] ; tl ; tl = tl->next) {
        if (tl->parent == 0) {
          /* This is the root of a timeline hierarchy */
          ptimeline(tl, 2, monitorf, ev) ;
        }
      }
    }
  }

  monitorf((uint8*)"Total Timelines: %i\n", total_made) ;
  monitorf((uint8*)"Unknown Timelines: %i\n", unknown_ref) ;
  monitorf((uint8*)"Killed on Start: %i\n", killed_at_start) ;
}

static sw_event_result HQNCALL ptimelines(void * ctx, sw_event * ev)
{
  sw_tl_debug * debug = (sw_tl_debug *) ev->message ;

  UNUSED_PARAM( void *, ctx );

  lock_mutex() ;
  ptimelines_locked(debug->tl, debug->monitorf, ev) ;
  unlock_mutex() ;

  return SW_EVENT_HANDLED ;  /* the above calls SwEventTail multiple times */
}

static sw_event_handler ptimelines_handler = {ptimelines, NULL, 0} ;

#endif /* DEBUG_BUILD */

/* -------------------------------------------------------------------------- */

enum {
  TL_DEAD = 0,
  TL_MUTEX = 1,
  TL_HANDLER = 2,
  TL_API = 4
} ;
static int booted = TL_DEAD ;  /* bitfield set during timeline_start() */


static void init_C_globals_timeline(void)
{
  int i ;

  next_ref = TL_REF_INIT ;

  for (i = 0 ; i < TIMELINE_HASHES ; ++i)
    timelines[i] = NULL ;

  discardable = NULL ;
  booted = TL_DEAD ;
  timeline_api_20110623.valid = FALSE ;

#ifdef DEBUG_BUILD
  killed_at_start = 0 ;
  unknown_ref = 0 ;
  total_made = 0 ;
#endif
}


HqBool HQNCALL timeline_start(void)
{
  HQASSERT(booted == TL_DEAD, "timeline_start being called multiple times?") ;

  init_C_globals_timeline() ;

  /* pthreads initialisation is required */
  if (pthread_api == 0 || rdr_api == 0 || event_api == 0 ||
      !pthread_api->valid || !rdr_api->valid || !event_api->valid) {
    HQFAIL("pthreads, RDR or Event not initialised") ;
    return FAILURE(FALSE) ;
  }

  /* Create and initialise mutex */
  if (pthread_mutex_init(&timeline_mt, NULL)) {
    HQFAIL("Can't init mutex") ;
    return FAILURE(FALSE) ;
  }
  booted |= TL_MUTEX ;

  timeline_api_20110623.valid = TRUE ;

#ifdef DEBUG_BUILD
  if (!unit_test()) {
    HQFAIL("Timeline failed unit_test") ;
    timeline_end() ;
    return FAILURE(FALSE) ;
  }

  if (SwRegisterHandler(EVENT_TIMELINE_DEBUG, &ptimelines_handler,
                        SW_EVENT_OVERRIDE)) {
    HQFAIL("Timeline debug registration failed") ;
    timeline_end() ;
    return FAILURE(FALSE) ;
  }
  booted |= TL_HANDLER ;
#endif

  /* Register the Timeline API as an RDR API */
  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_TIMELINE, 20110623,
                    &timeline_api_20110623, sizeof(timeline_api_20110623), 0)) {
    HQFAIL("Timeline API registration failed") ;
    timeline_end() ;
    return FAILURE(FALSE) ;
  }
  booted |= TL_API ;

  return TRUE ;
}


static void tl_discard(sw_timeline * tl) ;


void HQNCALL timeline_end(void)
{
  sw_timeline * tl, * next ;
  int i ;

  HQASSERT(event_api && event_api->valid, "Event ended too soon for Timeline") ;
  HQASSERT(rdr_api && rdr_api->valid, "RDR ended too soon for Timeline") ;
  HQASSERT(pthread_api && pthread_api->valid, "pthreads ended too soon for Timeline") ;

  if ((booted & TL_API) != 0) {
    booted &= ~TL_API ;
    (void) SwDeregisterRDR(RDR_CLASS_API, RDR_API_TIMELINE, 20110623,
                        &timeline_api_20110623, sizeof(timeline_api_20110623)) ;
  }

#ifdef DEBUG_BUILD
  if ((booted & TL_HANDLER) != 0) {
    booted &= ~TL_HANDLER ;
    (void) SwDeregisterHandler(EVENT_TIMELINE+99, &ptimelines_handler) ;
  }
#endif

  timeline_api_20110623.valid = FALSE ;

  if ((booted & TL_MUTEX) != 0) {
    booted &= ~TL_MUTEX ;
    (void) pthread_mutex_destroy(&timeline_mt) ;
  }

  for (i = 0 ; i < TIMELINE_HASHES ; ++i) {
    tl = timelines[i] ;
    timelines[i] = NULL ;
    while (tl) {
      next = tl->next ;
      tl_discard(tl) ;
      tl = next ;
    }
  }

  tl = discardable ;
  discardable = NULL ;
  while (tl) {
    next = tl->next ;
    tl_discard(tl) ;
    tl = next ;
  }

  HQASSERT(booted == TL_DEAD, "timeline_end failed to end what it started") ;
  booted = TL_DEAD ;
}

/* ========================================================================== */
/* Note, this does not set the tl.ref */

static sw_timeline * new_tl(sw_tl_type type, sw_tl_ref parent,
                            sw_tl_extent start, sw_tl_extent end,
                            sw_tl_unit unit, sw_tl_priority priority,
                            void * context, const uint8 * title, size_t length)
{
  const static sw_timeline zero = {0} ;
  uint8 * newtitle = NULL ;
  sw_timeline * tl = tl_alloc(sizeof(sw_timeline), MM_ALLOC_CLASS_TIMELINE) ;

  if (!tl)
    return FAILURE(NULL) ;

  if (title != NULL && length > 0) {
    newtitle = tl_alloc(length+1, MM_ALLOC_CLASS_TIMELINE) ;
    if (!newtitle) {
      /* Did allocate tl, didn't allocate title - fail whole thing */
      tl_free(tl, sizeof(sw_timeline)) ;
      return FAILURE(NULL) ;
    }
    memcpy(newtitle, title, length) ;
    newtitle[length] = 0 ;
  } else {
    length = 0 ;
  }

  *tl = zero ;

  tl->tl.type     = type ;
  tl->tl.state    = TL_STATE_START ;
  tl->tl.parent   = parent ;
  tl->tl.start    = start ;
  tl->tl.end      = end ;
  tl->tl.progress = start ;
  tl->tl.unit     = unit ;
  tl->tl.priority = priority ;
  tl->tl.context  = context ;

  tl->tl.title    = tl->newtitle   = newtitle ;
  tl->tl.length   = tl->newlength  = length ;

  tl->usage       = 1 ; /* as though locked on entry */

#ifdef DEBUG_BUILD
  ++total_made ;
#endif

  return tl ;
}

/* -------------------------------------------------------------------------- */

static void tl_discard(sw_timeline * tl)
{
  HQASSERT(tl, "No timeline to discard!") ;
  HQASSERT(tl->next == NULL && tl->parent == NULL && tl->child == NULL &&
           tl->sibling == NULL, "Discardable timeline still linked") ;

  if (tl->newtitle && tl->newtitle != tl->tl.title)
    tl_free(tl->newtitle, tl->newlength+1) ;

  if (tl->tl.title)
    tl_free(UNCONST(uint8*,tl->tl.title), tl->tl.length+1) ;

  /* Discard contexts we registered with RDR */
  if (tl->tl.ref) {
    sw_rdr_iterator * it = SwFindRDRbyType(RDR_CLASS_TIMELINE, tl->tl.ref) ;
    if (it) {
      sw_rdr_id id ;
      void * found ;
      while (SwNextRDR(it, 0, 0, &id, &found, NULL) == SW_RDR_SUCCESS) {
        (void)SwDeregisterRDR(RDR_CLASS_TIMELINE, tl->tl.ref, id, found, 12345) ;
      }
    }
    SwFoundRDR(it) ;
  }

  tl_free(tl, sizeof(sw_timeline)) ;
}

/* -------------------------------------------------------------------------- */
/* The mutex that protects the Timeline state. Unlocked during Events */

static void lock_mutex(void)
{
  pthread_mutex_lock(&timeline_mt) ;
}

static void unlock_mutex(void)
{
  sw_timeline * tl, * next, ** parent = &discardable, * discards = NULL ;

  /* Detach any that are no longer in use */
  for (tl = discardable ; tl ; tl = next) {
    next = tl->next ;
    if (tl->usage)
      parent = &tl->next ;
    else {
      *parent = next ;
      tl->next = discards ;
      discards = tl ;
    }
  }

  pthread_mutex_unlock(&timeline_mt) ;

  /* And free them outside the mutex */
  for (tl = discards ; tl ; tl = next) {
    next = tl->next ;
    tl_discard(tl) ;
  }
}

/* -------------------------------------------------------------------------- */
/* Find a timeline by reference */
/* MUST be called within a mutex */

static sw_timeline * timeline(sw_tl_ref ref)
{
  int hash = TIMELINE_HASH(ref) ;
  sw_timeline * tl ;

  if (ref == SW_TL_REF_INVALID)
    return NULL ;

  for (tl = timelines[hash] ; tl && tl->tl.ref != ref ; tl = tl->next) ;

#ifdef DEBUG_BUILD
  if (!tl)
    ++unknown_ref ;
#endif

  return tl ;
}

static void hold(sw_timeline * tl)
{
  ++tl->usage ;
}

static void release(sw_timeline * tl)
{
  --tl->usage ;
}

/* lock the mutex and find the timeline. If not found, also unlock the mutex */
static sw_timeline * lock_timeline(sw_tl_ref ref)
{
  sw_timeline * tl ;

  if (ref == 0)
    return NULL ;

  lock_mutex() ;
  tl = timeline(ref) ;
  if (tl)
    hold(tl) ;
  else
    unlock_mutex() ;

  return tl ;
}

/* unlock the mutex, decrement usage count, and free if no longer needed */
static void unlock_timeline(sw_timeline * tl)
{
  HQASSERT(tl, "Null timeline pointer") ;

  release(tl) ;
  unlock_mutex() ;
}

/* -------------------------------------------------------------------------- */
/* This MUST be called with a COPY of the timeline info, and mutexed */

static sw_event_result tl_event(sw_tl_state state, sw_timeline * tl,
                                SWMSG_TIMELINE * msg)
{
  sw_event_result result ;

  HQASSERT(tl, "No timeline!") ;
  HQASSERT(msg && msg != &tl->tl, "Must copy the Timeline info") ;

  msg->state = state ;

  /* Issue an Event, the Timeline is already locked */
  unlock_mutex() ;
  result = SwEvent(EVENT_TIMELINE + state, msg, sizeof(*msg)) ;
  lock_mutex() ;

  if (tl->usage == 1) { /* just us messing with it */
    if (tl->newtitle != tl->tl.title || tl->newlength != tl->tl.length) {
      /* There has been a change of title we can only now complete */
      if (tl->tl.length)
        tl_free(UNCONST(uint8*,tl->tl.title), tl->tl.length+1) ;
      tl->tl.title  = tl->newtitle ;
      tl->tl.length = tl->newlength ;
    }
  }

  return result ;
}

/* -------------------------------------------------------------------------- */

sw_tl_ref HQNCALL SwTimelineStart(sw_tl_type type, sw_tl_ref parent,
                                  sw_tl_extent start, sw_tl_extent end,
                                  sw_tl_unit unit, sw_tl_priority priority,
                                  void * context, const uint8 * title, size_t len)
{
  sw_event_result result ;
  SWMSG_TIMELINE msg ;
  sw_timeline * tl, * mum ;
  sw_tl_ref ref ;
  int hash ;

  HQASSERT(type != SW_TL_TYPE_ANY && priority != SW_TL_PRIORITY_UNKNOWN,
           "Bad parameters for SwTimelineStart") ;

  tl = new_tl(type, parent, start, end, unit, priority, context, title, len) ;
  if (tl == NULL)
    return SW_TL_REF_INVALID ;   /* no memory, presumably */

  lock_mutex() ; /* and tl is created 'locked' */

  /* Link to parent */
  if (parent) {
    sw_timeline * mum = timeline(parent) ;
    int prior ;

    if (!mum || mum->tl.state >= TL_STATE_ENDED) {
      /* Can't connect to an unknown parent timeline, and mustn't orphan */
      unlock_timeline(tl) ; /* and is freed here */
      tl_free(tl, sizeof(sw_timeline)) ;
      return SW_TL_REF_INVALID ;
    }

    tl->parent = mum ;
    tl->sibling = mum->child ;
    mum->child = tl ;

    /* Invalidate any current negotiations */
    do {
      prior = mum->tl.priority ;
      mum->negotiation = 0 ;  /* restart any negotiation on this hierarchy */
      mum = mum->parent ;     /* all the way up this hierarchy, mod priority */
    } while (mum && prior >= mum->tl.priority) ;
  }

  /* Assign reference, link into hash list */
  do {
    ref = TL_REF_INCREMENT(next_ref) ;
    mum = timeline(ref) ;
#ifdef DEBUG_BUILD
    --unknown_ref ; /* Don't count this intentional undefined timeline */
#endif
  } while (ref == 0 || mum) ;

  tl->tl.ref = ref ;
  hash = TIMELINE_HASH(ref) ;
  tl->next = timelines[hash] ;
  timelines[hash] = tl ;

  /* The Timeline now exists. Announce it via an Event, but if that is Handled
     or returns an error, end it and return zero */
  msg = tl->tl ;
  result = tl_event(TL_STATE_START, tl, &msg) ;
  unlock_timeline(tl) ;

  if (result != SW_EVENT_UNHANDLED) {
    /* Something objected, so end the timeline immediately */
    (void) SwTimelineEnd(ref) ;
    ref = SW_TL_REF_INVALID ;
#ifdef DEBUG_BUILD
    ++killed_at_start ;
#endif
  }

  return ref ;  /* Note that it isn't impossible that this is wrong already! */
}

/* -------------------------------------------------------------------------- */

sw_tl_type HQNCALL SwTimelineGetType(sw_tl_ref ref)
{
  sw_timeline * tl = lock_timeline(ref) ;
  sw_tl_type type ;

  if (!tl)
    return SW_TL_TYPE_NONE ;

  type = tl->tl.type ;

  unlock_timeline(tl) ;

  return type ;
}

/* -------------------------------------------------------------------------- */

sw_tl_ref HQNCALL SwTimelineGetAncestor(sw_tl_ref ref, sw_tl_type type)
{
  sw_timeline * mum, * tl = lock_timeline(ref) ;
  if (!tl)
    return SW_TL_REF_INVALID ;

  for (mum = tl->parent ;
       mum && type != SW_TL_TYPE_ANY && mum->tl.type != type ;
       mum = mum->parent) ;

  ref = (mum) ? mum->tl.ref : SW_TL_REF_INVALID ;
  unlock_timeline(tl) ;

  return ref ;
}

/* -------------------------------------------------------------------------- */

sw_tl_ref HQNCALL SwTimelineOfType(sw_tl_ref ref, sw_tl_type type)
{
  sw_timeline * mum, * tl ;

  if (type == SW_TL_TYPE_ANY) {      /* ANY is nonsensical for OfType */
    HQFAIL("Nonsensical paramaters for SwTimelineOfType") ;
    return SW_TL_REF_INVALID ;
  }

  tl = lock_timeline(ref) ;
  if (!tl)
    return SW_TL_REF_INVALID ;

  if (tl->tl.type != type) {      /* Only look for parent if not right type */
    for (mum = tl->parent ;
         mum && type != SW_TL_TYPE_ANY && mum->tl.type != type ;
         mum = mum->parent) ;

    ref = (mum) ? mum->tl.ref : SW_TL_REF_INVALID ;
  }
  unlock_timeline(tl) ;

  return ref ;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

sw_tl_result HQNCALL SwTimelineGetProgress(sw_tl_ref ref,
                                           sw_tl_extent * start,
                                           sw_tl_extent * end,
                                           sw_tl_extent * progress,
                                           sw_tl_unit * unit)
{
  sw_timeline * tl = lock_timeline(ref) ;

  if (!tl)
    return SW_TL_ERROR_UNKNOWN ;

  if (start)
    *start = tl->tl.start ;
  if (end)
    *end = tl->tl.end ;
  if (progress)
    *progress = tl->tl.progress ;
  if (unit)
    *unit = tl->tl.unit ;

  unlock_timeline(tl) ;

  return SW_TL_SUCCESS ;
}

#ifdef DEBUG_BUILD
/* -------------------------------------------------------------------------- */

static sw_tl_state SwTimelineGetState(sw_tl_ref ref)
{
  sw_timeline * tl = lock_timeline(ref) ;
  sw_tl_state state ;

  if (!tl)
    return TL_STATE_UNKNOWN ;

  state = tl->tl.state ;

  unlock_timeline(tl) ;

  return state ;
}
#endif

/* -------------------------------------------------------------------------- */

sw_tl_priority HQNCALL SwTimelineGetPriority(sw_tl_ref ref)
{
  sw_timeline * tl = lock_timeline(ref) ;
  sw_tl_priority priority ;

  if (!tl)
    return SW_TL_PRIORITY_UNKNOWN ;

  priority = tl->tl.priority ;

  unlock_timeline(tl) ;

  return priority ;
}

/* -------------------------------------------------------------------------- */

void * HQNCALL SwTimelineGetContext(sw_tl_ref ref, sw_tl_context id)
{
  void * context = NULL ;

  if (id) {

    (void)SwFindRDR(RDR_CLASS_TIMELINE, ref, id, &context, NULL) ;

  } else {
    sw_timeline * tl = lock_timeline(ref) ;

    if (!tl)
      return NULL ;

    context = tl->tl.context ;

    unlock_timeline(tl) ;
  }

  return context ;
}

/* -------------------------------------------------------------------------- */

sw_tl_result HQNCALL SwTimelineSetContext(sw_tl_ref ref, sw_tl_context id,
                                          void * context)
{
  void * old = NULL ;

  /* The primary context may NOT be changed */
  /* Other contexts must be changed under their own mutex control */
  if (!id)
    return SW_TL_ERROR_SYNTAX ;

  /* Get old one - we never register null contexts, so result isn't necessary */
  (void)SwFindRDR(RDR_CLASS_TIMELINE, ref, id, &old, NULL) ;

  /* Only do something if there’s a change, and register the new one first.
     This immediately takes precedence even though they have the same priority.
     Note magic length that is unlikely to clash with external registrations. */
  if (context != old) {
    if (context)
      (void)SwRegisterRDR(RDR_CLASS_TIMELINE, ref, id, context, 12345, 0) ;

    if (old)
      (void)SwDeregisterRDR(RDR_CLASS_TIMELINE, ref, id, old, 12345) ;
  }

  return SW_TL_SUCCESS ;
}


/* -------------------------------------------------------------------------- */

sw_tl_result HQNCALL SwTimelineSetProgress(sw_tl_ref ref, sw_tl_extent progress)
{
  sw_timeline * tl = lock_timeline(ref) ;

  if (!tl)
    return SW_TL_ERROR_UNKNOWN ;

  if (tl->tl.state < TL_STATE_ENDED) {
    double start = tl->tl.start ;
    double end = tl->tl.end ;

#ifdef NO_AUTO_EXTEND
    /* Clamp progress to the extent */
    if (start > end) {
      double swap = start ;
      start = end ;
      end = swap ;
    }
    if (progress < start)
      progress = start ;
    if (progress > end)
      progress = end ;
#else
    /* Auto extend to include the progress */
    if (start < end) {
      if (progress < start)
        tl->tl.start = progress ;
      if (progress > end)
        tl->tl.end = progress ;
    } else {
      if (progress < end)
        tl->tl.end = progress ;
      if (progress > start)
        tl->tl.start = progress ;
    }
#endif

    if (progress != tl->tl.progress) {
      SWMSG_TIMELINE msg ;

      tl->tl.progress = progress ;

      /* Issue the Event */
      msg = tl->tl ;
      (void)tl_event(TL_STATE_PROGRESS, tl, &msg) ;
    }
  }

  unlock_timeline(tl) ;

  return SW_TL_SUCCESS ;
}

/* -------------------------------------------------------------------------- */

sw_tl_result HQNCALL SwTimelineSetExtent(sw_tl_ref ref, sw_tl_extent start,
                                         sw_tl_extent end)
{
  sw_timeline * tl = lock_timeline(ref) ;

  if (!tl)
    return SW_TL_ERROR_UNKNOWN ;

  if (tl->tl.state < TL_STATE_ENDED &&
      (start != tl->tl.start || end != tl->tl.end)) {
    SWMSG_TIMELINE msg ;

    tl->tl.start = start ;
    tl->tl.end = end ;

    if (start > end) {
      double swap = start ;
      start = end ;
      end = swap ;
    }

    if (tl->tl.progress > end) /* if SW_TL_INDETERMINATE no clipping occurs */
      tl->tl.progress = end ;
    if (tl->tl.progress < start)
      tl->tl.progress = start ;

    /* Issue the Event */
    msg = tl->tl ;
    (void)tl_event(TL_STATE_EXTEND, tl, &msg) ;
  }

  unlock_timeline(tl) ;

  return SW_TL_SUCCESS ;
}

/* -------------------------------------------------------------------------- */

sw_tl_result HQNCALL SwTimelineSetTitle(sw_tl_ref ref, const uint8 * title,
                                        size_t length)
{
  sw_timeline * tl = lock_timeline(ref) ;

  if (!tl)
    return SW_TL_ERROR_UNKNOWN ;

  if (tl->tl.state < TL_STATE_ENDED) {
    /* Issue the event *before* changing the title */
    sw_rdr_result result ;
    SWMSG_TIMELINE msg = tl->tl ;
    msg.title  = title ;
    msg.length = length ;
    result = tl_event(TL_STATE_TITLE, tl, &msg) ;

    /* if nothing objects, update the title with what's in the message */

    if (result != SW_EVENT_UNHANDLED) {
      unlock_timeline(tl) ;
      return SW_TL_ERROR_IN_USE ;
    }

    /* If there's already a pending title change, discard it */
    if (tl->newtitle && tl->newtitle != tl->tl.title)
      tl_free(tl->newtitle, tl->newlength+1) ;

    /* Now 'store' the new title */
    if (msg.title == 0 || msg.length == 0) {
      tl->newtitle  = NULL ;
      tl->newlength = 0 ;
    } else {
      uint8 * newtitle = tl_alloc(msg.length+1, MM_ALLOC_CLASS_TIMELINE) ;
      if (!newtitle) {
        /* No memory. Keep old title. */
        tl->newtitle  = UNCONST(uint8 *,tl->tl.title) ;
        tl->newlength = tl->tl.length ;
        unlock_timeline(tl) ;
        return SW_TL_ERROR_MEMORY ;
      }

      tl->newtitle  = newtitle ;
      tl->newlength = msg.length ;
      memcpy(newtitle, msg.title, msg.length) ;
      newtitle[msg.length] = 0 ;
    }

    if (tl->usage == 1) {
      /* We have sole control - discard old title and update */
      if (tl->tl.title)
        tl_free(UNCONST(uint8*,tl->tl.title), tl->tl.length+1) ;

      tl->tl.title  = tl->newtitle ;
      tl->tl.length = tl->newlength ;
    }
  }

  unlock_timeline(tl) ;

  return SW_TL_SUCCESS ;
}

/* -------------------------------------------------------------------------- */

size_t HQNCALL SwTimelineGetTitle(sw_tl_ref ref, uint8 * buffer, size_t size)
{
  size_t length ;
  sw_timeline * tl = lock_timeline(ref) ;

  if (!tl)
    return 0 ;

  length = tl->newlength ;        /* note - most up-to-date title */
  if (length > 0 && buffer) {
    if (size > length) {
      buffer[length] = 0 ;        /* we do terminate if we can */
      size = length ;
    }
    memcpy(buffer, tl->newtitle, size) ;
  }

  unlock_timeline(tl) ;

  return length ;                 /* actual length, regardless of truncation */
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/** \brief Mark a timeline and all its descendants with a state and reason

    This is necessary because Events are issued while the Timelines are being
    discarded, so we musn't allow any new children to be added into this part
    of the hierarchy
 */
static void tl_mark(sw_timeline * tl, sw_tl_state state, int reason)
{
  sw_timeline * child ;

  tl->tl.state = state ;
  tl->tl.reason = reason ;

  for (child = tl->child ; child ; child = child->sibling)
    tl_mark(child, state, reason) ;
}

/* -------------------------------------------------------------------------- */
/** \brief Recurse through children, then issue an Event and discard timeline

    As Events are issued throughout this process, all the Timelines that are
    about to be discarded must have had their states changed to dead already,
    so any Handlers called can't do crazy things. Note that many harmless
    actions are allowed anyway, because we're lenient.

    On entry the mutex is held, but the Timeline in question is not locked.
 */
static void tl_end(sw_timeline * tl, sw_tl_state state, int reason)
{
  SWMSG_TIMELINE msg ;
  sw_timeline * that ;
  int hash ;

  /* This is the one place we don't copy the tl->tl - there's just no point,
     no further updating can take place, and we don't really care if Handlers
     do stupid things with it. */

  while (tl->child)
    tl_end(tl->child, state, reason) ;

  /* All the children are gone, issue our Event */
  msg = tl->tl ;
  hold(tl) ;
  (void)tl_event(state, tl, &msg) ;
  release(tl) ;

  /* If we got this far, we are really ending the timeline */

  /* Detach from hash... */
  hash = TIMELINE_HASH(tl->tl.ref) ;
  that = timelines[hash] ;
  while (that && that != tl && that->next != tl)
    that = that->next ;

  if (that == tl) {
    /* We are the first on this hash list */
    timelines[hash] = tl->next ;
  } else if (that && that->next == tl) {
    /* We are not the first */
    that->next = tl->next ;
  }
  tl->next = NULL ;

  /* Detach from parent... */
  if (tl->parent) {
    that = tl->parent->child ;
    while (that && that != tl && that->sibling != tl)
      that = that->sibling ;

    if (that == tl) {
      /* We are the first child */
      tl->parent->child = tl->sibling ;
    } else if (that && that->sibling == tl) {
      /* We are not the first child */
      that->sibling = tl->sibling ;
    }
    tl->sibling = NULL ;
    tl->parent = NULL ;
  }

  /* Fully detached, mark it for discard by the caller */
  tl->next = discardable ;
  discardable = tl ;
}

/* -------------------------------------------------------------------------- */

static void reset_negotiation(sw_timeline * tl)
{
  sw_timeline * child = tl->child ;

  tl->negotiation = 0 ;
  while (child) {
    reset_negotiation(child) ;
    child = child->sibling ;
  }
}

static HqBool tl_recurse(sw_timeline * tl, sw_tl_state state, int reason,
                         sw_tl_priority priority, int id)
{
  sw_timeline * child = tl->child, * next ;
  SWMSG_TIMELINE msg ;
  HqBool ok = TRUE ;

  /* High priority children? */
  while (child) {
    if (child->tl.priority >= priority) /* priority of initiator, not parent */
      return FALSE ;
    child = child->sibling ;
  }

  do {

    tl->negotiation = id ; /* will get zeroed by adding a child or increased by
                              a concurrent negotiation */

    /* Any objectors? */
    msg = tl->tl ;
    msg.state = state ;
    msg.reason = reason ;
    if (tl_event(state, tl, &msg) == SW_EVENT_HANDLED)
      return FALSE ;

    /* Now recurse into children (if still are any) */
    for (child = tl->child ; ok && child ; child = next) {
      HqBool ok ;
      next = child->sibling ;

      if (child->negotiation < id) {
        hold(child) ;
        ok = tl_recurse(child, state, reason, priority, id) ;
        release(child) ;                /* May be discarded here */
      }
    }
  } while (ok && tl->negotiation != id) ;

  return ok ;
}

static int concurrent_negotiations = 0 ;
static int negotiation_number = 1 ;

static HqBool negotiate(sw_timeline * tl, sw_tl_state state, int reason,
                        sw_tl_priority priority)
{
  HqBool ok ;

  if (++concurrent_negotiations == 1)
    ++negotiation_number ;

  ok = tl_recurse(tl, state, reason, priority, negotiation_number) ;

  if (--concurrent_negotiations == 0) {
    negotiation_number = 1 ;
    reset_negotiation(tl) ;
  }

  return ok ;
}

/* -------------------------------------------------------------------------- */

/* end_or_abort() relies on state numbers being numerically related */
#if (TL_STATE_ENDING - TL_STATE_END != TL_STATE_ABORTING - TL_STATE_ABORT) || \
    (TL_STATE_ENDED  - TL_STATE_END != TL_STATE_ABORTED  - TL_STATE_ABORT)
#error "TL_STATEs have been reordered incorrectly"
#endif

/** \brief  End or Abort the Timeline

    First child priorities are checked, as high priority children prolong the
    parent. Then a negotiation takes place comprising a number of Events. If
    this fails, the Timeline prolongs. Otherwise the Timeline and children are
    marked as dead to prevent new children being added, then all children and
    finally this Timeline are ended, issuing Events as they go.

    \param ref     The Timeline to end

    \param state   Either TL_STATE_END or TL_STATE_ABORT

    \param reason  The reason for the TL_STATE_ABORT

    \returns       SW_TL_SUCCESS if the Timeline ends.
                   SW_TL_ERROR_IN_USE if the Timeline prolongs.
                   SW_TL_ERROR_UNKNOWN if the Timeline cannot be found.
 */

static sw_tl_result end_or_abort(sw_tl_ref ref, sw_tl_state state, int reason)
{
  sw_timeline * tl = lock_timeline(ref) ;
  sw_timeline * child, * parent ;
  sw_tl_priority priority ;

  if (!tl)
    return SW_TL_ERROR_UNKNOWN ;

  priority = tl->tl.priority ;

  do { /* We loop instead of recursing into the parent (when necessary) */

    if (tl->tl.state >= TL_STATE_ENDED) {
      /* Called by a Handler or other thread during an existing take-down */
      unlock_timeline(tl) ;
      return SW_TL_SUCCESS ;
    }

    /* Mark as pending-end - Timeline isn't necessarily ending yet */
    tl->tl.state = state ;

    /* If there are existing children at a same or higher priority, we don't
       end yet (but remain at pending-end). */
    for (child = tl->child ; child ; child = child->sibling) {
      if (child->tl.priority >= priority) {
        /* We remain at pending-end */
        unlock_timeline(tl) ;
        return SW_TL_ERROR_IN_USE ;
      }
    }

    /* Negotiate */
    if (!negotiate(tl, state + TL_STATE_ENDING - TL_STATE_END, reason, priority)) {
      /* Something objected.
         Objecting to an END returns to normal - Timeline belongs to objector.
         Objecting to ABORT postpones the abort - Timeline WILL abort soon.
       */
      if (state == TL_STATE_END)
        tl->tl.state = TL_STATE_START ;
      unlock_timeline(tl) ;
      return SW_TL_ERROR_IN_USE ;
    }

    parent = tl->parent ;

    /* We are OK to discard this part of the hierarchy */

    /* Mark all these Timelines as dead */
    tl_mark(tl, state + TL_STATE_ENDED - TL_STATE_END, 0) ;
    /* mutex still claimed */
    release(tl) ;
    /* Issue the Events */
    tl_end(tl, state + TL_STATE_ENDED - TL_STATE_END, 0) ;

    /* Finally, if our parent is pending-end or pending-abort OR this is an
       abort with a suitable priority comparison, propogate up */
    tl = NULL ;
    if (parent) {
      if ((state == TL_STATE_ABORT && parent->tl.priority < priority) ||
          (state == TL_STATE_END && parent->tl.state == TL_STATE_ABORT)) {
        state = TL_STATE_ABORT ;
        tl     = parent ;
      } else if (state == TL_STATE_END && parent->tl.state == TL_STATE_END) {
        state = TL_STATE_END ;
        tl     = parent ;
      }
    } /* if parent */
  } while (tl) ;

  unlock_mutex() ;
  return SW_TL_SUCCESS ;
}

/** \brief  End a Timeline normally

    \param[in] ref  The Timeline to end

    \returns        SW_TL_SUCCESS if the Timeline ends.
                    SW_TL_ERROR_IN_USE if the Timeline prolongs.
                    SW_TL_ERROR_UNKNOWN if the Timeline cannot be found.
 */
sw_tl_result HQNCALL SwTimelineEnd(sw_tl_ref ref)
{
  return end_or_abort(ref, TL_STATE_END, 0) ;
}

/** \brief  Abort a Timeline with a given reason

    \param[in] ref     The Timeline to abort

    \param[in] reason  The reason for the abort

    \returns           SW_TL_SUCCESS if the Timeline ends.
                       SW_TL_ERROR_IN_USE if the Timeline prolongs.
                       SW_TL_ERROR_UNKNOWN if the Timeline cannot be found.
 */

sw_tl_result HQNCALL SwTimelineAbort(sw_tl_ref ref, int reason)
{
  return end_or_abort(ref, TL_STATE_ABORT, reason) ;
}

