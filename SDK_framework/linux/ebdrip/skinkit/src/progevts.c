/* Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:progevts.c(EBDSDK_P.1) $
 */

#include <string.h>
#include <stdio.h>

#include "std.h"

#include "swevents.h"
#include "skinkit.h"
#include "timerapi.h"
#include "progevts.h"
#include "swtimelines.h"
#include "timelineapi.h"

/* Measured progress handlers do nothing in HHR. Define this to build them. */
#undef PROGRESS_HANDLERS

static hqn_timer_t *progress_timer = NULL;

/**
 * Moving from using events to timelines, so event handlers are now
 * timeline aware. Unfortunately returning HANDLED heres stops timeline
 * creation and everything dies. So instead have to return magic
 * SW_EVENT_FORCE_UNHANDLED for now to allow timelines to be created but
 * suppress core stage messages.
 */

/*---------------------------------------------------------------------------*/
/* Event handler for core state events */
static sw_event_result HQNCALL core_tl_start(void *context, sw_event *evt)
{
  SWMSG_TIMELINE * msg = evt->message ;

  UNUSED_PARAM(void *, context);

  if ( msg == NULL || evt->length < sizeof( SWMSG_TIMELINE )) {
    return SW_EVENT_CONTINUE ;
  }

  switch ( msg->type ) {
    case SWTLT_JOB:
    case SWTLT_RECOMBINE_PAGE:
    case SWTLT_RENDER_PREPARE:
      return SW_EVENT_FORCE_UNHANDLED;

    default:
      return SW_EVENT_CONTINUE;
  }
}

/* Event handlers required to report job process timings. */
static sw_event_handlers job_time_handlers[] = {
  {core_tl_start, NULL, 0, EVENT_TIMELINE_START, SW_EVENT_DEFAULT + 10},
};

/*---------------------------------------------------------------------------*/
#ifdef PROGRESS_HANDLERS
/* Timeline event handlers for updated progress reports */
static sw_event_result HQNCALL tl_progress(void *context, sw_event *evt)
{
  SWMSG_TIMELINE *msg = evt->message;

  UNUSED_PARAM(void *, context);

  if ( msg == NULL || evt->length != sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_ERROR;

  if ( msg->type != TL_TYPE_PROGRESS )  /* Ignore all non-progress messages */
    return SW_EVENT_CONTINUE;

  switch (evt->type) {
  case EVENT_TYPE_START:
  case EVENT_TYPE_PROGRESS:
  case EVENT_TYPE_EXTEND:
  case EVENT_TYPE_ENDING:
  case EVENT_TYPE_ABORTING:
    /* Handle message */
    break ;
  }

  return SW_EVENT_CONTINUE;
}

/* Event handler required to report various job progress updates */
static sw_event_handlers measured_progress_handler[] = {
  {tl_progress, NULL, 0, EVENT_TIMELINE_START, SW_EVENT_NORMAL},
  {tl_progress, NULL, 0, EVENT_TIMELINE_PROGRESS, SW_EVENT_NORMAL},
  {tl_progress, NULL, 0, EVENT_TIMELINE_EXTEND, SW_EVENT_NORMAL},
  {tl_progress, NULL, 0, EVENT_TIMELINE_ENDING, SW_EVENT_NORMAL},
  {tl_progress, NULL, 0, EVENT_TIMELINE_ABORTING, SW_EVENT_NORMAL},
};
#endif

/*---------------------------------------------------------------------------*/
/* Timer callback to generate event to cause the interpreter to update various
 * progress updates.
 */
static void HQNCALL progevts_update(hqn_timer_t *timer, void *data)
{
  UNUSED_PARAM(hqn_timer_t*, timer);
  UNUSED_PARAM(void *, data);

  (void)SwEvent(SWEVT_PROGRESS_UPDATE, NULL, 0);
}


/*---------------------------------------------------------------------------*/
void progevts_enable_times(void)
{
  (void)SwDeregisterHandlers(job_time_handlers,
                             NUM_ARRAY_ITEMS(job_time_handlers)) ;
}


/* Initialise progress system event handlers and event generation timer */
int progevts_init(void)
{
#ifdef PROGRESS_HANDLERS
  /* Enable progress reporting */
  if (SwRegisterHandlers(measured_progress_handlers,
                         NUM_ARRAY_ITEMS(measured_progress_handlers))
      != SW_RDR_SUCCESS ) {
    return FALSE;
  }
#endif

  /* Ensure job timings are not being reported, by overriding the RIP's
     handlers. */
  if (SwRegisterHandlers(job_time_handlers, NUM_ARRAY_ITEMS(job_time_handlers))
      != SW_RDR_SUCCESS ) {
    progevts_finish();
    return FALSE;
  }

  /* Ask the interpreter for progress updates once a second. */
  progress_timer = hqn_timer_create(0, 1000, progevts_update, NULL);
  if (progress_timer == NULL) {
    progevts_finish();
    return FALSE;
  }

  return TRUE;
}


/* Shutdown the progress system */
void progevts_finish(void)
{
  /* Stop timer requesting progress updates */
  if (progress_timer != NULL) {
    hqn_timer_destroy(progress_timer);
    progress_timer = NULL;
  }

  /* Ensure job timings are no longer being reported */
  (void)SwDeregisterHandlers(job_time_handlers,
                             NUM_ARRAY_ITEMS(job_time_handlers)) ;

#ifdef PROGRESS_HANDLERS
  /* Disable progress reporting */
  (void)SwDeregisterHandlers(measured_progress_handlers,
                             NUM_ARRAY_ITEMS(measured_progress_handlers)) ;
#endif
}

