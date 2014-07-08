/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!src:progupdt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Progress reporting and update event handling.
 */

#include "core.h"

#include "swevents.h"
#include "eventapi.h"
#include "timelineapi.h"
#include "dlstate.h"
#include "corejob.h"
#include "coreinit.h"
#include "progupdt.h"
#include "riptimeline.h"

Bool do_progress_updates;

/* ========================================================================== */
/* Start reporting progress for named thing. */
sw_tl_ref progress_start(const uint8* name, size_t length, sw_tl_extent extent)
{
  /* Old code used to generate a message which included a 'do_report'
   * boolean initialised to FALSE. Unless any client set it to TRUE
   * then this function returned FALSE which stopped any further progress
   * being reported for that file. That caused some issues in the move to
   * using timelines, so is no longer supported. This means that the core
   * will continue to send progress messages even if there is no one
   * listening or interested. Will need to re-investigate this if it is
   * found to be a performance issue in any way. Though it should not as
   * progress is limited by a certain number per second by a timer. Still
   * have the ability for skin clients to reject a particular object for
   * progress by HANDLING the TimelineStart event and thus suppress the
   * timeline creation.
   */
  /**
   * \todo BMJ 15-Jul-11 :  Review progress architecture
   */
  /* Files get their own, special low priority, and also are parented
     directly off the core timeline. This is because the file interface is
     lazy, so the point where a file is noted to the timeline system may be
     long after it was opened, when many contexts have been opened or
     closed. */
  return SwTimelineStart(SWTLT_FILE_INTERPRET, core_tl_ref, 0.0, extent,
                         SW_TL_UNIT_BYTES,
                         core_tl_priority + PRIORITY_FILE_INTERPRET,
                         NULL, name, length);
}

/* Report the current progress for the thing. */
void progress_current(sw_tl_ref tl_ref, sw_tl_extent current)
{
  if ( tl_ref != SW_TL_REF_INVALID )
    CHECK_TL_SUCCESS(SwTimelineSetProgress(tl_ref, current));
}

/* End reporting progress for the thing. */
void progress_end(sw_tl_ref *tl_ptr)
{
  HQASSERT(tl_ptr != NULL, "Nowhere to find timeline") ;

  if ( *tl_ptr != SW_TL_REF_INVALID ) {
    CHECK_TL_SUCCESS(SwTimelineEnd(*tl_ptr)) ;
    *tl_ptr = SW_TL_REF_INVALID ;
  }
}

/* Progress update event handler */
static sw_event_result HQNCALL progress_update_handler(void *context,
                                                       sw_event *ev)
{
  UNUSED_PARAM(void *, context);
  UNUSED_PARAM(sw_event *, ev);

  /* Tickle progress update functions */
  do_progress_updates = TRUE;

  return SW_EVENT_CONTINUE;
}

static sw_event_handlers handlers[] = {
  { progress_update_handler, NULL, 0, SWEVT_PROGRESS_UPDATE, SW_EVENT_NORMAL }
};

static Bool progress_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params);

  return (SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) == SW_RDR_SUCCESS) ;
}

static void progress_finish(void)
{
  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ;
}

/* Initialise progress reporting globals */
void progress_C_globals(core_init_fns *fns)
{
  do_progress_updates = FALSE;

  fns->swstart = progress_swstart ;
  fns->finish = progress_finish ;
}

/* Log stripped */
