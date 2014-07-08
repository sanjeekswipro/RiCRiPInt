/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!src:corejob.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core job tracking.
 */

#include "core.h"
#include "corejob.h"
#include "coreinit.h"
#include "hqatomic.h"
#include "dlstate.h"
#include "timing.h"
#include "taskh.h"
#include "hqmemcpy.h"
#include "hqmemset.h" /* HqMemZero */
#include "swenv.h"    /* get_date, get_utime */
#include "swcopyf.h"  /* swcopyf */
#include "progress.h"
#include "swevents.h"
#include "swerrors.h"
#include "eventapi.h"
#include "dicthash.h"
#include "namedef_.h"
#include "uvms.h"
#include "mm.h"
#include "display.h"
#include "timerapi.h"
#include "timelineapi.h"
#include "riptimeline.h"
#include "hqunicode.h"  /* utf8_has_bom */
#include "devices.h"    /* find_device() */
#include "monitori.h"
#include "interrupts.h" /* raise_interrupt */
#include "mlock.h" /* multi_mutex_* */

static mps_word_t jobStart, jobEnd, configStart, configEnd;

#ifdef ASSERT_BUILD
static unsigned int corejob_allocs ;
#endif

#ifdef DEBUG_BUILD
#ifndef PIPELINE_JOBS
#define PIPELINE_JOBS 0 /* Default to not pipelining jobs, but compile in. */
#endif
#endif

#ifdef PIPELINE_JOBS
/** Flag to turn on job pipelining. This is global to prevent warnings, and
    also to allow it to be turned on by a system param or some such. */
Bool pipeline_jobs ;

/** \brief Finaliser function for asynchronous job completion task. */
static void corejob_finalise(corecontext_t *context, void *args)
{
  task_group_t *job_group = args ;

  UNUSED_PARAM(corecontext_t *, context) ;

  /* Do nothing if there are no args; this means that the task was cancelled
     before having join responsibility for the job group transferred to it.
     The task that tried to hand over is responsible for joining the job
     tasks. */
  if ( job_group == NULL )
    return ;

  /* Don't bother propagating error from job group to completion group, we
     don't want to cause the next job to fail because a previous one did. */
  (void)task_group_join(job_group, NULL) ;
  task_group_release(&job_group) ;
}
#endif /* PIPELINE_JOBS */


/* Job interrupt information. */
struct JOB_IRQ {
  corejob_t*  job;    /* Job being interrupted. */
  int32 error; /* Reason for aborting, an error code. */
};

/** Abort a job.

  This can be used as callback for \c task_call_with_context(). */
static
void irq_handler(
  corecontext_t*  context,
  void*           arg)
{
  struct JOB_IRQ* irq = arg;

  UNUSED_PARAM(corecontext_t*, context);

  HQASSERT(irq->job != NULL, "NULL job pointer");

  task_group_cancel(irq->job->task_group, irq->error);
  /* Cancel first so that the error propagates. */
  if (irq->job->state == CORE_JOB_RUNNING) {
    /* Raise interrupt for interpreter if the job is still running. */
    if (irq->error == TIMEOUT)
      raise_timeout();
    else
      raise_interrupt();
  }
}


static void corejob_interrupt(
  corejob_t*  job,
  int32 type)
{
  struct JOB_IRQ irq_args;

  HQASSERT(type == TIMEOUT || type == INTERRUPT, "Invalid interrupt type.");

  irq_args.job = job;
  irq_args.error = type;
  task_call_with_context(irq_handler, &irq_args);
}


/* Job interrupt event handler. Handles user and timeout interrupts. */
static
sw_event_result HQNCALL corejob_interrupt_handler(
  void* context,
  sw_event* evt)
{
  SWMSG_INTERRUPT* irq_msg;
  corejob_t* job;

  if (evt->message == NULL || evt->length < sizeof(SWMSG_INTERRUPT)) {
    return (SW_EVENT_CONTINUE);
  }

  irq_msg = evt->message;
  job = context;
  HQASSERT(job != NULL, "NULL job context pointer");

  /* Match the interrupt timeline to this handler's job */
  if (irq_msg->timeline == SwTimelineGetAncestor(job->timeline, SWTLT_JOB)) {
    corejob_interrupt(job, INTERRUPT);

  } else if (irq_msg->timeline == SwTimelineGetAncestor(job->timeline, SWTLT_JOB_STREAM)) {
    corejob_interrupt(job, TIMEOUT);
  }

  return (SW_EVENT_CONTINUE);
}

/* -------------------------------------------------------------------------- */
/* This mutex is used to stop concurrent Progress Channel messages being written
   to the Progress Device simultaneously */

static multi_mutex_t progress_mutex ;

/* -------------------------------------------------------------------------- */
/* The Monitor Event Handler for the Progress Channel - output to the job
   logfile (if the message got this far). If we can't, pass on. */

static sw_event_result HQNCALL mon_progress(void * context, sw_event * ev)
{
  SWMSG_MONITOR * mon = ev->message ;
  corejob_t     * job = context ;

  if (mon == 0 || ev->length < sizeof(SWMSG_MONITOR) || /* Weird message */
      mon->channel != MON_CHANNEL_PROGRESS)             /* or not ours,  */
    return SW_EVENT_CONTINUE ;                          /* then pass on. */

  if (job == 0 || job->logfile < 0) {                   /* Can't output! */
    HQASSERT(job, "mon_progress registered with no corejob_t") ;
    HQFAIL("Corejob message had to be redirected") ;    /* Can this happen? */
    return SW_EVENT_CONTINUE ;                          /* Then use fallback */
  }

  /* We assume the progress device is not re-entrant, so mutex: */
  multi_mutex_lock(&progress_mutex) ;
  (void) (*theIWriteFile(progressdev))(progressdev, job->logfile,
                                       mon->text, (int32)mon->length) ;
  multi_mutex_unlock(&progress_mutex) ;

  return SW_EVENT_HANDLED ;
}

/* The Handler structure for the above - the context will point to the current
   corejob_t. */
static sw_event_handler prog_handler = {mon_progress} ;

/* -------------------------------------------------------------------------- */

Bool corejob_create(DL_STATE *page, task_t *previous)
{
  corejob_t *job ;
  DEVICE_FILEDESCRIPTOR logfile = -1 ;

  HQASSERT(page != NULL && page->job == NULL,
           "Input page already has job object") ;

  if ( (job = mm_alloc(mm_pool_temp, sizeof(corejob_t), MM_ALLOC_CLASS_JOB)) != NULL ) {
    task_group_t *root = task_group_root() ;

    job->refcount = 1 ; /* One for the returned ref. */
    job->state = CORE_JOB_NONE ;
    job->previous = previous ;
    job->task_group = NULL ;
    job->join_group = NULL ;
    job->first_dl = job->last_dl = page ;
    job->has_output = FALSE ;
    job->pages_output = 0 ;
    job->failed = FALSE ;
    /* We always have a valid pointer for the name */
    job->name.string = (uint8 *)"" ;
    job->name.length = 0 ;
    job->interrupt_handler.handler = corejob_interrupt_handler;
    job->interrupt_handler.context = NULL;
    job->interrupt_handler.reserved = 0;
    job->timeout_timer = NULL;
    job->timeline = core_tl_ref ;

    /* Open the %progress%JobLog file for progress reporting [65510] */
    if (progressdev != NULL && isDeviceEnabled(progressdev) &&
        isDeviceRelative(progressdev)) {
      logfile = (*theIOpenFile(progressdev))(
                 progressdev, (uint8*)"JobLog", SW_WRONLY | SW_CREAT) ;
      HQASSERT(logfile >= 0,
               "Unable to open JobLog for progress reporting") ;
    }
    job->logfile = logfile ;

    if ( !task_group_create(&job->task_group, TASK_GROUP_JOB,
                            root, NULL /*resources*/) ) {
      if ( job->previous != NULL )
        task_release(&job->previous) ;
      mm_free(mm_pool_temp, job, sizeof(corejob_t)) ;
      job = NULL ;
    } else {
      /* We want the job object valid before we start the job timeline, because
         event handlers want to know they're dealing with a */
      NAME_OBJECT(job, CORE_JOB_NAME) ;

      /* The job task group can be made provisionable immediately, so we can
         start tasks as they are created. */
      task_group_ready(job->task_group) ;

      /* The timeline object's context is not counted as a reference against
         the job object. Instead, the job stream timeline ended/aborted
         handler is made responsible for final freeing of the job object.
         This is required because the primary context of the timeline cannot
         be changed safely, and we may receive interrupts at any time. The
         job object cannot be allowed to go out of scope whilst any interrupt
         could dereference it, which is for the duration of the timeline. */
      if ( (page->timeline = timeline_push(&job->timeline, SWTLT_JOB_STREAM,
                                           0 /*end*/, SW_TL_UNIT_PAGES,
                                           job, NULL, 0))
           == SW_TL_REF_INVALID ) {
        (void)task_group_join(job->task_group, NULL) ;
        task_group_release(&job->task_group) ;
        if ( job->previous != NULL )
          task_release(&job->previous) ;
        UNNAME_OBJECT(job) ;
        mm_free(mm_pool_temp, job, sizeof(corejob_t)) ;
        job = NULL ;
      } else {
#ifdef ASSERT_BUILD
        ++corejob_allocs ;
#endif
      }
    }

    task_group_release(&root) ;

    if (!job) { /* Close feedback files if job was not created */
      if (logfile != -1)
        (void)(*theICloseFile(progressdev))(progressdev, logfile) ;
    }
  }

  page->job = job ;
  page->pageno = 0 ;

  if (job && logfile >= 0) {
    /* Register the default handler for the %progress%LogFile channel, just
       above the fallback route to the monitor device */
    prog_handler.context = job ;
    (void)SwRegisterHandler(SWEVT_MONITOR, &prog_handler, SW_EVENT_DEFAULT+1) ;
  }

  return (job != NULL) ;
}

corejob_t *corejob_acquire(corejob_t *job)
{
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(job, CORE_JOB_NAME) ;

  HqAtomicIncrement(&job->refcount, before) ;
  HQASSERT(before > 0, "Job was previously released") ;

  return job ;
}

void corejob_release(corejob_t **jobptr)
{
  corejob_t *job ;
  hq_atomic_counter_t after ;

  HQASSERT(jobptr, "Nowhere to find job pointer") ;
  job = *jobptr ;
  VERIFY_OBJECT(job, CORE_JOB_NAME) ;
  *jobptr = NULL ;

  HqAtomicDecrement(&job->refcount, after) ;
  HQASSERT(after >= 0, "Job already released") ;
  if ( after == 0 ) {
    corejob_set_timeout(job, 0) ; /* Remove any existing timer */

    /* De-register handler for user and timeout interrupts */
    if (job->interrupt_handler.context != NULL) {
      (void)SwSafeDeregisterHandler(SWEVT_INTERRUPT_USER, &job->interrupt_handler);
      (void)SwSafeDeregisterHandler(SWEVT_INTERRUPT_TIMEOUT, &job->interrupt_handler);
      job->interrupt_handler.context = NULL;
    }

    switch ( job->state ) {
    default:
      HQFAIL("Invalid core job state") ;
    case CORE_JOB_NONE: /* Nothing to do */
      break ;
    case CORE_JOB_CONFIG:
      mps_telemetry_label(0, configEnd);
      CHECK_TL_VALID(timeline_pop(&job->timeline, SWTLT_JOB_CONFIG, !job->failed)) ;
      probe_end(SW_TRACE_JOB_CONFIG, (intptr_t)job);
      break ;
    case CORE_JOB_RUNNING:
      mps_telemetry_label(0, jobEnd);
      CHECK_TL_VALID(timeline_pop(&job->timeline, SWTLT_JOB, !job->failed)) ;
      probe_end(SW_TRACE_JOB, (intptr_t)job);
      break ;
    }

    if ( job->previous != NULL )
      task_release(&job->previous) ;

    if ( job->join_group != NULL ) {
      /* Ensure that asynchronous join task has completed. */
      (void)task_group_join(job->join_group, NULL) ;
      task_group_release(&job->join_group) ;
    }

    task_group_release(&job->task_group) ;

    if ( job->name.length > 0 ) {
      mm_free(mm_pool_temp, job->name.string, job->name.length) ;
      job->name.length = 0 ;
      job->name.string = (uint8 *)"" ;
    }

    /* Responsibility for actually freeing the job object is passed to
       the timeline ended event. */
    CHECK_TL_VALID(timeline_pop(&job->timeline, SWTLT_JOB_STREAM, !job->failed)) ;
  }
}

void corejob_begin(DL_STATE *page)
{
  corejob_t *job ;
  job = page->job ;

  VERIFY_OBJECT(job, CORE_JOB_NAME) ;
  HQASSERT(job->first_dl == page && job->last_dl == page,
           "Job tracker out of sync with DL page") ;
  HQASSERT(job->state == CORE_JOB_NONE, "Unexpected core job state") ;

  page->pageno = 0 ;

  mps_telemetry_label(0, jobStart);

  job->state = CORE_JOB_CONFIG ;
  job->has_output = FALSE ;
  job->pages_output = 0 ;
  job->failed = FALSE ;

  probe_begin(SW_TRACE_JOB_CONFIG, (intptr_t)job);

  if ( (page->timeline = timeline_push(&job->timeline, SWTLT_JOB_CONFIG,
                                       0 /*end*/, SW_TL_UNIT_NONE,
                                       job, job->name.string, job->name.length))
       == SW_TL_REF_INVALID ) {
    job->failed = TRUE ;
    /** \todo ajcd 2011-07-20: Decide how to clean up */
    HQFAIL("Job config timeline start failed") ;
  }

  mps_telemetry_label(0, configStart);
}

void corejob_running(DL_STATE *page)
{
  corejob_t *job = page->job ;

  VERIFY_OBJECT(job, CORE_JOB_NAME) ;
  HQASSERT(job->first_dl == page && job->last_dl == page,
           "Job tracker out of sync with DL page") ;
  HQASSERT(job->state == CORE_JOB_CONFIG, "Unexpected core job state") ;

  mps_telemetry_label(0, configEnd);
  timeline_pop(&job->timeline, SWTLT_JOB_CONFIG, !job->failed) ;
  probe_end(SW_TRACE_JOB_CONFIG, (intptr_t)job);

  job->state = CORE_JOB_RUNNING ;

  probe_begin(SW_TRACE_JOB, (intptr_t)job);
  if ( (page->timeline = timeline_push(&job->timeline, SWTLT_JOB,
                                       SW_TL_EXTENT_INDETERMINATE /*end*/,
                                       SW_TL_UNIT_PAGES,
                                       job, job->name.string, job->name.length))
       == SW_TL_REF_INVALID ) {
    job->failed = TRUE ;
    /** \todo ajcd 2011-07-20: Decide how to clean up */
    HQFAIL("Job timeline start failed") ;
  }

  /* Register user and timeout interrupt handlers for this job */
  job->interrupt_handler.context = job;
  if (SwRegisterHandler(SWEVT_INTERRUPT_USER, &job->interrupt_handler,
                        SW_EVENT_NORMAL) != SW_RDR_SUCCESS ||
      SwRegisterHandler(SWEVT_INTERRUPT_TIMEOUT, &job->interrupt_handler,
                        SW_EVENT_NORMAL) != SW_RDR_SUCCESS) {
    job->failed = TRUE;
    /* Deregister user interrupt event handler in case it succeeded. */
    (void)SwSafeDeregisterHandler(SWEVT_INTERRUPT_USER, &job->interrupt_handler);
    /** \todo mrw 2011-08-19: needs clean up along lines of what happens with
     * timeline. */
    HQFAIL("Job start interrupt handler register failed") ;
    job->interrupt_handler.context = NULL;
  }

  mps_telemetry_label(0, jobStart);
}

void corejob_end(DL_STATE *page, task_t **complete, Bool failure_handled)
{
  task_group_t *root_group = task_group_root() ;
  corecontext_t *context = get_core_context() ;
  error_context_t error = ERROR_CONTEXT_INIT, *olderror ;
  corejob_t *job = page->job ;

  VERIFY_OBJECT(job, CORE_JOB_NAME) ;

  /* Suppress errors from this function and sub-functions. We're able to cope
     with failure by joining immediately. */
  olderror = context->error ;
  context->error = &error ;

  /* The job's task group is now complete. If we pipeline jobs, this means
     that we can run tasks in the job through task_helper_locked(). */
  task_group_close(job->task_group) ;

  if ( complete != NULL ) {
    *complete = NULL ;
#ifdef PIPELINE_JOBS
#ifdef DEBUG_BUILD
    pipeline_jobs = (debug_dl & DEBUG_DL_PIPELINE_JOBS) != 0 ;
#endif
    if ( pipeline_jobs ) {
      task_group_t *job_group = task_group_acquire(job->task_group) ;
      if ( task_group_create(&job->join_group, TASK_GROUP_COMPLETE,
                             root_group, NULL) ) {
        task_group_ready(job->join_group) ;
        if ( task_create(complete,
                         NULL /*specialiser*/, NULL /*spec args*/,
                         NULL /*no worker*/, NULL /*args*/, &corejob_finalise,
                         job->join_group, SW_TRACE_JOB_COMPLETE) ) {
          /* If successful, this next call atomically sets the task args to
             the job object: */
          Bool transferred =
            task_group_set_joiner(job_group, *complete, job_group) ;
          task_ready(*complete) ;
          task_group_close(job->join_group) ;
          if ( transferred )
            goto done ;
          task_release(complete) ;
        }
        task_group_cancel(job->join_group, context->error->new_error) ;
        (void)task_group_join(job->join_group, NULL) ;
        task_group_release(&job->join_group) ;
      }
      task_group_release(&job_group) ;
    }
#endif /* PIPELINE_JOBS */
  }

  /* Ignore the error return value; if the job is failed because of an
     asynchronous flush, the job's failure field is set. */
  (void)dl_pipeline_flush(1, failure_handled) ;

  /* We failed to create a separate completion task for this job. We need to
     join the job group synchronously. */
  (void)task_group_join(job->task_group, NULL) ;

#ifdef PIPELINE_JOBS
 done:
#endif
  context->error = olderror ;
  task_group_release(&root_group) ;

  /* Don't increment job number on a font query etc... */
  if ( job->has_output )
    ++page->job_number ;

  corejob_release(&page->job) ;
}

static void current_jobname(corejob_t *job)
{
  OBJECT* statusdict;
  OBJECT* jobname;

  /* Extract jobname from statusdict for now */
  statusdict = fast_extract_hash_name(&systemdict, NAME_statusdict);
  jobname = fast_extract_hash_name(statusdict, NAME_jobname);
  if (oType(*jobname) == OSTRING && theLen(*jobname) > 0 ) {
    corejob_name(job, oString(*jobname), theLen(*jobname));
  } else {
    corejob_name(job, NAME_AND_LENGTH("UnSpecified JobName"));
  }
}

void corejob_page_begin(DL_STATE *page)
{
  ++page->pageno ;
  current_jobname(page->job);
  probe_begin(SW_TRACE_INTERPRET, page->pageno);
  CHECK_TL_VALID(timeline_push(&page->timeline, SWTLT_INTERPRET_PAGE, 0 /*end*/,
                               SW_TL_UNIT_NONE, page, NULL, 0));
}

void corejob_page_end(DL_STATE *page, Bool will_render)
{
  current_jobname(page->job);
  timeline_pop(&page->timeline, SWTLT_INTERPRET_PAGE, will_render) ;
  CHECK_TL_SUCCESS(SwTimelineSetProgress(page->job->timeline, page->pageno)) ;
  probe_end(SW_TRACE_INTERPRET, page->pageno);
}

void corejob_name(corejob_t *job, uint8 *jobname, size_t len)
{
  CHECK_TL_SUCCESS(SwTimelineSetTitle(job->timeline, jobname, len));
}

static void HQNCALL timeout_fire(hqn_timer_t* timer, void* data)
{
  SWMSG_INTERRUPT irq;

  UNUSED_PARAM(hqn_timer_t*, timer);
  UNUSED_PARAM(void*, data);

  /* Send a timeout interrupt event. */
  irq.timeline = (sw_tl_ref)((intptr_t)data);
  HQASSERT((void *)((intptr_t)irq.timeline) == data, "Truncated timeline ref") ;
  (void)SwEvent(SWEVT_INTERRUPT_TIMEOUT, &irq, sizeof(irq));
}

void corejob_set_timeout(corejob_t *job, int32 timeout)
{
  VERIFY_OBJECT(job, CORE_JOB_NAME);

  /* Remove any existing timer */
  if (job->timeout_timer != NULL) {
    hqn_timer_destroy(job->timeout_timer);
    job->timeout_timer = NULL;
  }
  /* Set a new one if needed */
  if (timeout > 0) {
    sw_tl_ref ref ;
    CHECK_TL_VALID((ref = SwTimelineOfType(job->timeline, SWTLT_JOB_STREAM)));
    job->timeout_timer = hqn_timer_create(timeout*1000, 0, timeout_fire,
                                          (void *)((intptr_t)ref));
  }
}

sw_tl_ref timeline_push(sw_tl_ref *stack,
                        sw_tl_type type, sw_tl_extent end, sw_tl_unit unit,
                        void *context, const uint8 *title, size_t length)
{
  sw_tl_ref ref ;
  sw_tl_priority priority = SW_TL_PRIORITY_NORMAL ;

  if ( *stack != SW_TL_REF_INVALID ) {
    if ( (priority = SwTimelineGetPriority(*stack)) == SW_TL_PRIORITY_UNKNOWN )
      return SW_TL_REF_INVALID ; /* Parent must have ended */
    priority += PRIORITY_RELATIVE ;
  }

  ref = SwTimelineStart(type, *stack, 0 /*start*/, end, unit,
                        priority, context, title, length) ;
  if ( ref != SW_TL_REF_INVALID )
    *stack = ref ;

  return ref ;
}

sw_tl_ref timeline_pop(sw_tl_ref *stack, sw_tl_type type, Bool ok)
{
  sw_tl_ref ref ;

  HQASSERT(stack != NULL, "Nowever to find timeline") ;

  ref = *stack ;

  if ( type != SW_TL_TYPE_ANY )
    ref = SwTimelineOfType(ref, type) ;

  if ( ref != SW_TL_REF_INVALID ) {
    *stack = SwTimelineGetAncestor(ref, SW_TL_TYPE_ANY) ;

    /* Let priorities automatically cause end on any intermediate timelines. */
    if ( ok ) {
      CHECK_TL_SUCCESS(SwTimelineEnd(ref)) ;
    } else {
      CHECK_TL_SUCCESS(SwTimelineAbort(ref, 0 /*no interpretation of reason*/)) ;
    }
    HQASSERT(SwTimelineGetType(core_tl_ref) == SWTLT_CORE,
             "Core timeline accidentally destroyed") ;
  }

  return ref ;
}

/** Attach a start time to a timeline, using the SWTLC_START_TIME secondary
    context. */
static void timeline_set_start(sw_tl_ref tl, int32 time)
{
  CHECK_TL_SUCCESS(SwTimelineSetContext(tl, SWTLC_START_TIME,
                                        (void *)((intptr_t)time))) ;
}

/** Get the start time for a timeline, using the SWTLC_START_TIME secondary
    context. */
static int32 timeline_get_start(sw_tl_ref tl)
{
  void *context = SwTimelineGetContext(tl, SWTLC_START_TIME) ;
  int32 time = (int32)((intptr_t)context) ;
  HQASSERT((void *)((intptr_t)time) == context, "Truncated start time") ;
  return time ;
}

/** Format phase duration for reporting. */
static void format_time(uint8 *buffer, int32 time)
{
  int32 hours  = time/(1000*60*60);
  int32 minutes = time/(1000*60) - 60*hours;
  int32 seconds = time/1000 - 60*(minutes + 60*hours);
  int32 millisecs = time - 1000*(seconds + 60*(minutes + 60*hours));

  if (hours > 0) {
    if (hours >= 10) { /* Just hours */
      swcopyf(buffer, (uint8 *)"%d hours (%02d:%02d:%02d.%03d)",
              hours,
              hours, minutes, seconds, millisecs);
    } else { /* Report hours and minutes */
      swcopyf(buffer, (uint8 *)"%d hours %d minutes (%02d:%02d:%02d.%03d)",
              hours, minutes,
              hours, minutes, seconds, millisecs);
    }

  } else if (minutes > 0) {
    if (minutes > 10) { /* Just minutes */
      swcopyf(buffer, (uint8 *)"%d minutes (%02d:%02d:%02d.%03d)",
              minutes,
              hours, minutes, seconds, millisecs);
    } else { /* Report minutes and seconds */
      swcopyf(buffer, (uint8 *)"%d minutes %d seconds (%02d:%02d:%02d.%03d)",
              minutes, seconds,
              hours, minutes, seconds, millisecs);
    }

  } else { /* Seconds including milliseconds */
    swcopyf(buffer, (uint8 *)"%d.%03d seconds (%02d:%02d:%02d.%03d)",
            seconds, millisecs,
            hours, minutes, seconds, millisecs);
  }
}

/* ========================================================================== */
/**
 * Default core timeline-event handler to report elapsed states/times etc.
 * This is installed with a default (low) priority, so the skin can observe
 * or override it.
 */
static sw_event_result HQNCALL corejob_tl_start(void *context, sw_event *evt)
{
  SWMSG_TIMELINE *msg = evt->message;

  UNUSED_PARAM(void *, context);

  HQASSERT(evt->type == EVENT_TIMELINE_START, "Unexpected event type");

  if ( msg == NULL || evt->length < sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_CONTINUE;

  if ( msg->type >= TL_CORE && msg->type < TL_CORE_END ) {
    sw_tl_ref tl = msg->ref ;
    int32 start_time = timeline_get_start(tl) ;

    /* Don't mark start time if going recursive. */
    if ( start_time == 0 ) {
      timeline_set_start(tl, get_rtime()) ;

      switch ( msg->type ) {
      case SWTLT_JOB:
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_JOBSTART,
                  UVM("Starting Job On %s\n"), (char*)get_date(TRUE));
        break ;
      case SWTLT_RECOMBINE_PAGE:
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_RECOMBINESTART,
                  UVS("Recombining separations\n"));
        break;
      case SWTLT_RENDER_PREPARE:
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_RENDERPREPSTART,
                  SwTimelineGetAncestor(tl, SWTLT_RENDER_PAGE) != SW_TL_REF_INVALID
                  ? UVS("Preparing to render page\n")
                  : UVS("Preparing to partial paint page\n"));
        break;
      }
    }
  }

  return SW_EVENT_CONTINUE;
}

/* -------------------------------------------------------------------------- */

static sw_event_result HQNCALL corejob_tl_ending(void *context, sw_event *evt)
{
  SWMSG_TIMELINE *msg = evt->message;

  UNUSED_PARAM(void *, context);

  /** \todo TODO FIXME @@@ Should be the ENDED/ABORTED events */
  HQASSERT(evt->type == EVENT_TIMELINE_ENDING ||
           evt->type == EVENT_TIMELINE_ABORTING, "Unexpected event type");

  if ( msg == NULL || evt->length < sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_CONTINUE;

  if ( msg->type == SWTLT_JOB ) {
    corejob_t *job = msg->context ;
    int32 duration ;

    VERIFY_OBJECT(job, CORE_JOB_NAME) ;

    /* Only report the time if the start handler was not suppressed, and this
       was the first recursive exit. Note that this means that start and end
       are not strictly nested; the instances act more like a queue, with the
       first start paired with the first exit, etc. */
    duration = timeline_get_start(msg->ref) ;
    if ( duration != 0 ) {
      uint8 time[128] ;
      sw_tl_ref tl = msg->ref ;

      timeline_set_start(tl, 0) ;

      duration = get_rtime() - duration ;
      format_time(time, duration) ;

      /* translation template: UVM("Total time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
      emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_TOTALTIME,
                (uint8 *)"Total time: %s\n", time);

      /* translation template: UVM("Job Completed: %s%{; document: %s%}") */
      /* translation template: UVM("Job Not Completed: %s%{; document: %s%}") */
      if ( utf8_has_bom(msg->title, msg->title + msg->length) ) {
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_JOBEND,
                  (uint8 *)(UTF8_BOM "Job %sCompleted: %.*s\n\n"),
                  !job->failed ? "" : "Not ", msg->length - UTF8_BOM_LEN,
                  msg->title + UTF8_BOM_LEN);
      } else {
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_JOBEND,
                  (uint8 *)("Job %sCompleted: %.*s\n\n"),
                  !job->failed ? "" : "Not ", msg->length, msg->title);
      }
    }
  } else if ( msg->type >= TL_CORE && msg->type < TL_CORE_END &&
              /* If the phase end has a problem, don't report the time,
                 and don't modify the existing time. This allows recursive
                 interprets to avoid resetting the last page time. */
              evt->type != EVENT_TIMELINE_ABORTING ) {
    int32 duration ;

    /* Only report the time if the start handler was not suppressed, and
       this was the first recursive exit. Note that this means that start
       and end are not strictly nested; the instances act more like a
       queue, with the first start paired with the first exit, etc. */
    duration = timeline_get_start(msg->ref) ;
    if ( duration != 0 ) {
      uint8 time[128];
      sw_tl_ref tl = msg->ref ;

      timeline_set_start(tl, 0) ;

      duration = get_rtime() - duration ;
      format_time(time, duration);

      switch (msg->type) {
      case SWTLT_INTERPRET_PAGE:
        /* translation template: UVM("Interpretation time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_INTERPRETTIME,
                  (uint8 *)"Interpretation time: %s\n", time);
        break;
      case SWTLT_PRESCANNING_PAGES:
        /* Scanning is entirely within interpretation, so add the time to
         * the interpret start to compensate. */
        CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref,
                                                   SWTLT_INTERPRET_PAGE))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Scan time adjusting zero interpret time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM("HVD pre-scan time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_HVDSCANTIME,
                  (uint8 *)"HVD pre-scan time: %s\n", time);
        break;
      case SWTLT_SCANNING_PAGES:
        /* Scanning is entirely within interpretation, so add the time to
         * the interpret start to compensate. */
        CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref,
                                                   SWTLT_INTERPRET_PAGE))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Scan time adjusting zero interpret time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM("HVD scan time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_HVDSCANTIME,
                  (uint8 *)"HVD scan time: %s\n", time);
        break;
      case SWTLT_POSTSCANNING_PAGES:
        /* Scanning is entirely within interpretation, so add the time to
         * the interpret start to compensate. */
        CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref,
                                                   SWTLT_INTERPRET_PAGE))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Scan time adjusting zero interpret time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM("HVD post-scan time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_HVDSCANTIME,
                  (uint8 *)"HVD post-scan time: %s\n", time);
        break;
      case SWTLT_RECOMBINE_PAGE:
        /* Recombine is entirely within interpretation, so add the time to
         * the interpret start to compensate. */
        CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref,
                                                   SWTLT_INTERPRET_PAGE))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Recombine time adjusting zero interpret time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM("Recombining separations time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_RECOMBINETIME,
                  (uint8 *)"Recombining separations time: %s\n", time);
        break;
      case SWTLT_COMPOSITE_PAGE:
        /* Compositing is entirely within rendering, so add the time to the
         * render start to compensate. */
        if ( (tl = SwTimelineGetAncestor(msg->ref, SWTLT_RENDER_PAGE)) == SW_TL_REF_INVALID )
          if ( (tl = SwTimelineGetAncestor(msg->ref, SWTLT_RENDER_CACHE)) == SW_TL_REF_INVALID )
            CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref, SWTLT_RENDER_PARTIAL))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Composite time adjusting zero render time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        if ( (tl = SwTimelineGetAncestor(tl, SWTLT_INTERPRET_PAGE)) != SW_TL_REF_INVALID )
          timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM("Compositing time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_COMPOSITETIME,
                  (uint8 *)"Composite time: %s\n", time);
        break;
      case SWTLT_RENDER_PREPARE:
        /* Preconvert is entirely within rendering, so add the time to the
         * render start to compensate. */
        if ( (tl = SwTimelineGetAncestor(msg->ref, SWTLT_RENDER_PAGE)) == SW_TL_REF_INVALID )
          if ( (tl = SwTimelineGetAncestor(msg->ref, SWTLT_RENDER_CACHE)) == SW_TL_REF_INVALID )
            CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref, SWTLT_RENDER_PARTIAL))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Render prepare time adjusting zero render time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        if ( (tl = SwTimelineGetAncestor(tl, SWTLT_INTERPRET_PAGE)) != SW_TL_REF_INVALID )
          timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM("Render prepare time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_RENDERPREPTIME,
                  (uint8 *)"Render prepare time: %s\n", time);
        break;
      case SWTLT_RENDER_PAGE:
        /* translation template: UVM("Print time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_PRINTTIME,
                  (uint8 *)"Print time: %s\n", time);
        break;
      case SWTLT_RENDER_SEPARATION:
        /* translation template: UVM("Print time for separation:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)") */
        emonitorf(msg->ref, MON_CHANNEL_PROGRESS, MON_TYPE_SEPPRINTTIME,
                  (uint8 *)"Print time for separation: %s\n", time);
        break;
      case SWTLT_RENDER_PARTIAL:
        /* Partial paint is entirely within interpretation, so add the time
         * to the interpret start to compensate. */
        CHECK_TL_VALID((tl = SwTimelineGetAncestor(msg->ref,
                                                   SWTLT_INTERPRET_PAGE))) ;
        HQASSERT(timeline_get_start(tl) != 0,
                 "Partial paint time adjusting zero interpret time") ;
        timeline_set_start(tl, timeline_get_start(tl) + duration) ;
        /* translation template: UVM(Partial paint time:%{ %d hours%}%{ %d minutes%}%{ %g seconds%} (%02d:%02d:%02g)) */
        emonitorf(tl, MON_CHANNEL_PROGRESS, MON_TYPE_PARTIALPAINTTIME,
                  (uint8 *)"Partial paint time: %s\n", time);
        break ;
      }
    }
  }

  return SW_EVENT_CONTINUE;
}

static sw_event_result HQNCALL corejob_tl_ended(void *context, sw_event *evt)
{
  SWMSG_TIMELINE *msg = evt->message;

  UNUSED_PARAM(void *, context);

  HQASSERT(evt->type == EVENT_TIMELINE_ENDED ||
           evt->type == EVENT_TIMELINE_ABORTED, "Unexpected event type");
  if ( msg == NULL || evt->length < sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_CONTINUE;

  if ( msg->type == SWTLT_JOB_STREAM) {
    corejob_t *job = msg->context ;

    VERIFY_OBJECT(job, CORE_JOB_NAME) ;

    /* Close the JobLog file [65510] */
    if (job->logfile >= 0) {
      /* Detach the default handler for the %progress%JobLog channel */
      (void)SwSafeDeregisterHandler(SWEVT_MONITOR, &prog_handler) ;
      prog_handler.context = NULL ;
      if (progressdev != NULL)
        (void)(*theICloseFile(progressdev))(progressdev, job->logfile) ;
      job->logfile = -1 ;
    }

    /* This is where we finally release the core job object. */
    HQASSERT(job->refcount == 0, "Live job object in ended timeline") ;
    HQASSERT(job->name.length == 0 &&
             job->join_group == NULL && job->task_group == NULL &&
             job->previous == NULL, "Job object not properly released") ;

#ifdef ASSERT_BUILD
    --corejob_allocs ;
#endif

    UNNAME_OBJECT(job) ;
    mm_free(mm_pool_temp, job, sizeof(corejob_t)) ;

    /* Since the context has now disappeared, it wouldn't be safe for any
       other handlers to run. */
    return SW_EVENT_HANDLED;
  }

  return SW_EVENT_CONTINUE;
}

/* -------------------------------------------------------------------------- */
/** Handle SWTLT_JOB title changes - this is the job name */

static sw_event_result HQNCALL corejob_name_handler(void *context, sw_event *ev)
{
  SWMSG_TIMELINE *msg = ev->message;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || ev->length < sizeof(SWMSG_TIMELINE))
    return SW_EVENT_CONTINUE;

  HQASSERT(ev->type == EVENT_TIMELINE_TITLE, "Unexpected event type");

  if ( msg->type == SWTLT_JOB ) {
    corejob_t *job = msg->context;

    VERIFY_OBJECT(job, CORE_JOB_NAME) ;

    if ( job->name.length > 0 ) {
      mm_free(mm_pool_temp, job->name.string, job->name.length) ;
      job->name.length = 0 ;
      job->name.string = (uint8 *)"" ;
    }

    if ( msg->length > 0 ) {
      uint8 *name = mm_alloc(mm_pool_temp, msg->length, MM_ALLOC_CLASS_JOB) ;

      if ( name == NULL )
        return SW_EVENT_ERROR ;

      HqMemCpy(name, msg->title, msg->length) ;
      job->name.string = name ;
      job->name.length = msg->length ;
    }
  }

  return SW_EVENT_CONTINUE;
}

/* -------------------------------------------------------------------------- */

static sw_event_handlers handlers[] = {
  { corejob_tl_start,     NULL, 0, EVENT_TIMELINE_START, SW_EVENT_DEFAULT },
  { corejob_tl_ending,    NULL, 0, EVENT_TIMELINE_ENDING, SW_EVENT_DEFAULT },
  { corejob_tl_ending,    NULL, 0, EVENT_TIMELINE_ABORTING, SW_EVENT_DEFAULT },
  { corejob_tl_ended,     NULL, 0, EVENT_TIMELINE_ENDED, SW_EVENT_DEFAULT },
  { corejob_tl_ended,     NULL, 0, EVENT_TIMELINE_ABORTED, SW_EVENT_DEFAULT },
  { corejob_name_handler, NULL, 0, EVENT_TIMELINE_TITLE, SW_EVENT_DEFAULT }
} ;

static Bool corejob_postboot(void)
{
  multi_mutex_init(&progress_mutex, MONITOR_LOCK_INDEX, FALSE,
                   SW_TRACE_MONITOR_ACQUIRE, SW_TRACE_MONITOR_HOLD);
  /* Create the MPS telemetry labels */
  configStart = mps_telemetry_intern("start config");
  configEnd = mps_telemetry_intern("end config");
  jobStart = mps_telemetry_intern("start job");
  jobEnd = mps_telemetry_intern("end job");
  /* Install handlers with default (low) priority, so they can be easily
     overridden by the skin. */
  return (SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) == SW_RDR_SUCCESS) ;
}

static void corejob_finish(void)
{
  HQASSERT(corejob_allocs == 0, "Core jobs still allocated") ;
  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ;
  multi_mutex_finish(&progress_mutex);
}


void corejob_C_globals(core_init_fns *fns)
{
  jobStart = jobEnd = configStart = configEnd = 0 ;
#ifdef ASSERT_BUILD
  corejob_allocs = 0 ;
#endif
#ifdef PIPELINE_JOBS
  pipeline_jobs = PIPELINE_JOBS ;
#endif

  fns->postboot = corejob_postboot ;
  fns->finish = corejob_finish ;
}

/* Log stripped */

