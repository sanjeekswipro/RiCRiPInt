/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!export:corejob.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core job structure.
 */

#ifndef __COREJOB_H__
#define __COREJOB_H__

#include "hqatomic.h"
#include "objnamer.h"
#include "swtimelines.h" /* TL_END_CORE */
#include "swdevice.h"  /* [65510] */

#include "monitori.h"  /* sw_mon_type for now */

struct task_t ; /* from COREmulti */
struct task_group_t ; /* from COREmulti */
struct DL_STATE ; /* from SWv20 */
struct core_init_fns ; /* from SWcore */

/** \brief Type definition for a job. */
typedef struct corejob_t corejob_t ;

/** \brief Representation of a job. */
struct corejob_t {
  hq_atomic_counter_t refcount ; /**< Reference count. */

  /** \brief Core job states.

      \note These states are ORed into the core job pointer when constructing
      cookies for events, so don't add new states or renumber them without
      making sure that it is safe to do so. */
  enum {
    CORE_JOB_NONE = 0, /**< Job stream in progress. */
    CORE_JOB_CONFIG,   /**< Configuration in progress. */
    CORE_JOB_RUNNING,  /**< Running job stream provided by %config% device. */
    CORE_JOB_DONE      /**< No job stream in progress. */
  } state ;

  /** A task that must complete before the current DL completion task. This
      is initialised to the previous job's completion task (which is never
      cancelled). It is updated to the previous DL's erase task when a page
      is handed off for asynchronous rendering. This means that all pages
      in a job are linked together. If a rendering page fails, and isn't
      handled by the render cleanup loop, then the error will propagate all
      of the way to the current inputpage. */
  struct task_t *previous ;

  /** The task group encompassing all tasks for this job, *except* the
      job's completion task. Cancelling and joining this group will cancel
      the entire job. This is normally done by the completion task. */
  struct task_group_t *task_group ;

  /** The completion task group used for pipelining across jobs. This group
      should never be cancelled. We cannot re-use task_group for this,
      because job tasks may navigate to task_group in order to cancel the
      whole job. */
  struct task_group_t *join_group ;

  /** The first DL sent for rendering. This in initialised to inputpage on job
      creation, and should only be updated by the render completion. */
  struct DL_STATE *first_dl ;

  /** The last DL created during this job. */
  struct DL_STATE *last_dl ;

  Bool has_output ;   /**< Have pages been sent to the PGB device? */
  unsigned int pages_output; /**< Running count of pages output (incl. copies). */
  Bool failed ;       /**< Has this job failed? */

  struct {
    uint8 *string ;
    size_t length ;
  } name ;            /**< Copy of job name. This is the job name sent out
                         through SwEvent(), and re-captured by a job name
                         handler. Doing this allows the skin to modify the
                         name. */

  /** Timeline stack from the core timeline to the job level. */
  sw_tl_ref timeline ;

  /** Interrupt event handler context. */
  sw_event_handler  interrupt_handler;

  /** Job timeout timer.  NULL if no timeout time. */
  struct TIMER_HANDLE* timeout_timer;

  /* Handle for %progress%JobLog channel - there's only one, */
  DEVICE_FILEDESCRIPTOR logfile ;

  /** \note Oddly, the job number is NOT represented in the core job
      structure. This is because it can be changed by setpagedevice, and this
      doesn't correlate with the lifetime of the job structure. Pages that
      have been sent for output need to use the job number that was in force
      when their pagedevice was created, thus they use the job number stored
      in the DL state. */

  OBJECT_NAME_MEMBER
} ;

#define CORE_JOB_NAME "Core Job"

/** \brief Create a new job object tracker.

    \param[in] page  The current input page.
    \param[in] previous  The completion task of the previous job.

    \retval TRUE  If a new job object was created and associated with the
                  current page.
    \retval FALSE If a new job object could not be created.

    This function associates the current input page with the new job
    object. The completion task of the previous job is used as a dependent for
    all rendering tasks of the new job. The completion task reference is
    released by this function, regardless of return status.
*/
Bool corejob_create(/*@notnull@*/ /*@in@*/ struct DL_STATE *page,
                    /*@null@*/ /*@in@*/ struct task_t *previous) ;

/** \brief Clone a reference to a job tracker.

    \param[in] job  A job pointer to clone.

    \returns Another pointer referencing the job. The cloned reference
    MUST be released with \c corejob_release().
*/
/*@dependent@*/ /*@notnull@*/
corejob_t *corejob_acquire(/*@notnull@*/ /*@in@*/ corejob_t *job) ;

/** \brief Release a reference to a job tracker.

    \param[in,out] jobptr
          A pointer to a job pointer. On entry, the job pointer must
          represent a valid job. On exit, the job pointer is NULL.

    When the last reference to a job is released, an event will be generated
    indicating that the job is complete. All job references acquired through
    \c corejob_create() or \c corejob_acquire() MUST be released.
*/
void corejob_release(/*@notnull@*/ /*@in@*/ /*@out@*/ corejob_t **jobptr) ;

/** Mark the start of interpreting a page.
 *
 * \param[in] page   The page to start.
 */
void corejob_page_begin(/*@notnull@*/ /*@in@*/ struct DL_STATE *page);

/** Mark the end of interpreting a page.
 *
 * \param[in] page   The page to end.
 * \param[in] will_render  TRUE if the page just ended had content and will be
 *                         rendered, FALSE if it had no content.
 */
void corejob_page_end(/*@notnull@*/ /*@in@*/ struct DL_STATE *page,
                      Bool will_render);

/** Note when a user job begins.
 *
 * \param[in] page  The current inputpage.
 */
void corejob_begin(/*@notnull@*/ /*@in@*/ struct DL_STATE *page);

/** Note when a user job changes from configuring to running.
 *
 * \param[in] page  The current inputpage.
 */
void corejob_running(/*@notnull@*/ /*@in@*/ struct DL_STATE *page);

/** \brief Note when a user job ends.

    Create a completion task for a job, which the next job can use as a
    dependent, and transfer responsibility for joining the job's task group to
    the completion task.

    \param[in] page    Current job input page.
    \param[out] taskptr  A pointer where a completion task for the job will
                         be stored. The completion task should be used as a
                         predecessor to any rendering tasks for the next job.
    \param failure_handled
                       TRUE if job failure has already been handled by the
                       interpreter. FALSE if any asynchronous render errors
                       have not caused an interpreter failure.

    If the completion task could not be created, the job will have been
    joined synchronously. On a successful exit, the job's \c previous field
    will be NULL, and its' \c task_group field will refer to the completion
    task's group.

    This function disassociates the current input page from its core job
    object. The core job object may continue to exist until all previously
    spawned pages finish rendering. */
void corejob_end(/*@notnull@*/ /*@in@*/ struct DL_STATE *page,
                 /*@null@*/ /*@out@*/ struct task_t **taskptr,
                 Bool failure_handled) ;

/** \brief Set the job name used to report progress to the progress device.

    \param[in] job     Job to set title for.
    \param[in] jobname The job name.
    \param[in] len     The length of the job name.
 */
void corejob_name(/*@notnull@*/ /*@in@*/ corejob_t *job,
                  /*@notnull@*/ /*@in@*/ uint8 *jobname, size_t len);

/** \brief Set a timeout period for this job.

    \param[in] job     The job to set the timeout for.
    \param[in] timeout The job timeout period in seconds.
 */
void corejob_set_timeout(/*@notnull@*/ /*@in@*/ corejob_t *job,
                         int32 timeout);

/** Core init tables for job tracking */
void corejob_C_globals(struct core_init_fns *fns) ;

#endif /* __COREJOB_H__ */

/* Log stripped */
