/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!export:taskh.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Thread-pooling task API functions.
 */

#ifndef __TASKH_H__
#define __TASKH_H__

struct core_init_fns ;
struct error_context_t ;

/** \ingroup multi */
/** \{ */

#include "threadapi.h"
#include "taskt.h"

/** \brief Returns a newly-generated task group.

     \param[out] newgroup The new task group, or NULL if the group could not
                        be created. If non-NULL, this reference must be
                        released by the caller.
     \param type        The group type of the new group.
     \param[in] parent  The parent group of the new group. If this function
                        succeeds, an extra reference is taken to the parent
                        group. This function will fail if the parent group is
                        NULL, or has been cancelled or joined already.
     \param[in] required  The scheduling resources required before tasks
                        in this group will be scheduled.

    \retval TRUE If creation of the new task group succeeded.
    \retval FALSE If creation of the new task group failed.
*/
Bool task_group_create(/*@notnull@*/ /*@out@*/ task_group_t **newgroup,
                       task_grouptype_t type,
                       /*@null@*/ /*@in@*/ task_group_t *parent,
                       /*@null@*/ /*@in@*/ resource_requirement_t *required) ;

/** \brief Clone a reference to a task group.

    \param[in] group  A task group pointer to clone.

    \returns Another pointer referencing the task group. The cloned reference
    MUST be released with \c task_group_release().
*/
/*@dependent@*/ /*@notnull@*/
task_group_t *task_group_acquire(/*@notnull@*/ task_group_t *group) ;

/** \brief Release group reference.

    \param[in,out] group  The group reference to be released.
*/
void task_group_release(/*@notnull@*/ /*@in@*/ /*@out@*/ task_group_t **group) ;

/** \brief Make a task group ready to provision.

    \param[in] group  The group to make provisionable.

    Tasks within a group or subgroups will not run until the group is made
    ready. A group should not be made ready until it has been placed in the
    correct relationship to other groups in the hierarchy by \c
    task_joins_group().

    Assertions will be thrown if an attempt is made to add tasks or sub-groups
    to a group that has not yet been made ready. */
void task_group_ready(/*@notnull@*/ /*@in@*/ task_group_t *group) ;

/** \brief Close a task group.

    \param[in] group  The group to complete.

    Closing a group ensures that no more tasks or sub-groups can be added
    to it, unless by elaboration tasks already existing inside the group.
    This is used to make it safe to know when a task can be run by a helper
    on another thread. If the groups that a task joins group are both
    complete and empty, no new tasks can be added from outside the group, so
    a helper can safely run the task without risking deadlock.

    Assertions will be thrown if an attempt is made to add tasks or sub-groups
    to a group that has been closed, unless the task adding the sub-task or
    sub-group is a member of the group itself. */
void task_group_close(/*@notnull@*/ /*@in@*/ task_group_t *group) ;

/** \brief Cancel all tasks in a specified group.

    \param group The task group to cancel. The group reference is released
                 by the cancel operation.
                 \todo ajcd 2011-06-08: should this release the group
                 reference?
    \param[in] reason  An error code that will be injected into the task.
*/
void task_group_cancel(/*@notnull@*/ /*@in@*/ task_group_t *group,
                       int32 reason) ;

/** \brief Wait for all tasks in a specified group to complete and run their
    cleanup actions.

    \param group The task group to join. A particular group may only be joined
                 once, by the join task responsible for it. After it is joined,
                 no more tasks may be added to the task group.
    \param errcontext An error context into which the errors from the group
                 are propagated. Use CoreContext.error for the current task's
                 error context. If the error status of a task join is to be
                 ignored, this parameter may be NULL.

    \retval TRUE   If the task group succeeded. No error is propagated.
    \retval FALSE  If the task group had failed, and the error was propagated.

    \note Every group \b must be joined exactly once, or the resources for the
    group will never be released.
*/
Bool task_group_join(/*@notnull@*/ /*@in@*/ task_group_t *group,
                     /*@null@*/ /*@in@*/ struct error_context_t *errcontext) ;

/** \brief Return an ancestor task group of the current task.

    \param group_type  The group type to find.

    \returns The specified ancestor group of the current task.
     \todo ajcd 2011-04-09: Haven't decided if we should return NULL if
     ancestor type isn't found, or assert it. Depends on how implementation
     goes.

     \note This function acquires a reference to the group returned. This
     reference \b must be released with \c task_group_release(), or passed
     to a function that consumes the reference.
*/
/*@notnull@*/ /*@dependent@*/
task_group_t *task_group_current(task_grouptype_t group_type) ;

/** \brief Return the root (interpreter) task group.

    \returns Another pointer referencing the root task group. The cloned
    reference MUST be released with \c task_group_release(). */
/*@dependent@*/ /*@notnull@*/
task_group_t *task_group_root(void) ;

/** \brief Indicate that resources have been returned to the task groups by
    the PGB. */
void task_group_resources_signal(void) ;

/** Has the task group been cancelled? */
Bool task_group_is_cancelled(/*@notnull@*/ /*@in@*/ task_group_t *group);

/** \brief Return the error state of the task group.

  This is really meaningful only for a cancelled or joined group.
 */
int32 task_group_error(/*@notnull@*/ /*@in@*/ task_group_t *group);

/** \brief Create a new task object.

    \param[out] newtask
               A pointer to a new task, set in the constructing state, or NULL
               if the task could not be created (either because the RIP is
               shutting down, or because there is no more memory available).
               Dependencies on tasks that must complete before this task is
               scheduled may be added to the task using \c task_depends(). The
               task can not be scheduled until \c task_ready() has been called,
               and all dependee tasks have completed successfully. The task
               pointer MUST be released with \c task_release() when the
               creating thread will no longer reference the task. If the last
               reference to the task is released without calling \c
               task_ready(), the task will be destroyed without running.
    \param[in] specialiser_fn
               Pointer to a function that performs core context
               specialisation for this task. The context set by \a
               specialiser_fn may be re-used for multiple tasks of this type,
               so it should not perform any work specific to the particular
               task's \a worker_args. The \a specialiser_fn pointer itself is
               used to determine which tasks can share specialised contexts.
    \param[in,out] specialiser_args
               The task specialiser argument pointer, passed through to
               task_specialise_done().
    \param[in] worker_fn
               The function that performs the work of the task. This function
               is called when the task is scheduled on a thread, and should
               return when the task is done, indicating success or failure of
               the task.
    \param[in,out] worker_args
               A pointer to task-specific structure that provides parameters
               to a task, and a location to store output from a task.
    \param[in] cleanup_fn
               A function that releases any resources associated with the
               task's parameter structure. If a task is successfully created,
               the cleanup function will ALWAYS be called before the task
               is destroyed. The timing of the call to the cleanup function is
               not specified, and should not be relied upon: it may be called
               immediately or some time after task completion or cancellation.
    \param[in] group
               The task group of a task. If \c task_create() succeeds, it
               takes an extra reference to the new task. This function fails
               if the group is NULL, or has been joined or cancelled already.
    \param[in] trace_id
               A trace ID used for probe begin/end around the worker function
               execution.

    \retval TRUE If creation of the new task succeeded.
    \retval FALSE If creation of the new task failed.
*/
Bool task_create(/*@notnull@*/ /*@out@*/ task_t **newtask,
                 /*@null@*/ /*@in@*/ task_specialiser_fn *specialiser_fn,
                 /*@null@*/ /*@in@*/ /*@out@*/ void *specialiser_args,
                 /*@null@*/ /*@in@*/ task_fn *worker_fn,
                 /*@null@*/ /*@in@*/ /*@out@*/ void *worker_args,
                 /*@null@*/ /*@in@*/ task_cleanup_fn *cleanup_fn,
                 /*@null@*/ /*@in@*/ task_group_t *group,
                 int trace_id) ;

/** \brief Acquire a reference to the current task.

    \returns The current task reference. The reference returned MUST be
             released with \c task_release().
*/
/*@dependent@*/ /*@notnull@*/
task_t *task_current(void) ;

/** \brief Clone a reference to a task.

    \param[in] task  A task pointer to clone.

    \returns Another pointer referencing the input task. The cloned reference
    MUST be released with \c task_release().
*/
/*@dependent@*/ /*@notnull@*/
task_t *task_acquire(/*@notnull@*/ task_t *task) ;

/** \brief Release a reference to a task.

    \param[in,out] taskptr
          A pointer to a task pointer. On entry, the task pointer must
          represent a valid task. On exit, the task pointer is NULL.

    When the last reference to a task is released, the task becomes eligibly
    for destruction. All task references acquired through \c task_create(),
    \c task_current(), or \c task_acquire() MUST be released.
*/
void task_release(/*@notnull@*/ /*@in@*/ /*@out@*/ task_t **taskptr) ;

/** \brief Add a dependency to a non-runnable task.

    \param[in] pre
               The dependee task, which must be completed successfully before
               \a post can be scheduled.
    \param[in] post
               The dependent task, which can only be scheduled if and when
               \a pre completes successfully. Neither of \c task_ready(),
               or \c task_group_cancel() can have acted on this task.

    \retval TRUE   The dependency was added successfully.
    \retval FALSE  The dependency could not be added, because of memory
                   exhaustion.

    Multiple dependencies can be added to a task, including duplicate
    dependencies. If a dependency is added from a task that has already
    run and failed, the dependent task is automatically cancelled.

    The \a post task must either be in constructing state, or must be dependent
    on the current task to ensure deterministic execution.
*/
Bool task_depends(/*@notnull@*/ /*@in@*/ task_t *pre,
                  /*@notnull@*/ /*@in@*/ task_t *post) ;

/** \brief Make the task the joiner for a group.

    \param[in] group
               The group whose joiner will be set.
    \param[in] task
               A task, which will become the join task for \a group, or NULL if
               the group will be joined recursively by its parent group.
    \param args  A new argument value for the joiner task.

    \retval TRUE   Responsibility for joining the group was transferred to
                   the task.
    \retval FALSE  Responsibility for joining the group could not be
                   transferred to the task.

    Joining responsibility for a group may only be transferred by the current
    join task for a group. The task to which it is transferred is specially
    formed; it must be in the constructing state (or cancelled) when
    transferred join responsibility, and its argument must be NULL at the
    time responsibility is transferred. If this function succeeds, the task
    argument is changed to \c args. It is up to the caller to dispose of args
    as appropriate if this function fails. If \c args is the task group being
    joined, it is not necessary to take another reference to it, since the
    group is guaranteed to exist until the task_group_join() operation
    completes.

    If the \c task is NULL, then \c args must also be NULL. In this case, the
    group will be joined when its parent group is joined. The design of the
    group hierarchy should be handled with care: cancellations do not
    propagate out of groups automatically, but will propagate through all
    dependencies except the task ultimately responsibility joining a
    group. If there is no dependent outside of a pure recursive group, then
    errors from task inside the group may be lost.
*/
Bool task_group_set_joiner(/*@notnull@*/ /*@in@*/ task_group_t *group,
                           /*@null@*/ task_t *task,
                           /*@null@*/ void *args) ;

/** \brief Replace a single task in the graph with two.

     \param[in] replace  The task being replaced.
     \param[in] in       The task to which input dependencies are transferred.
     \param[in] out      The task to which output dependencies are transferred.

     Replacing a single task with two dependent tasks allows elaboration of
     the task graph whilst it is operating.

     The \a replace task must either be in the constructing state, must be
     depending on the current task, or must be the current task to ensure
     deterministic execution.

     This function creates a dependency from \a in to \a out to maintain
     transitivity of the existing dependencies.

     \todo ajcd 2012-08-30: Decide whether to always create a dependency
     between \a in and \a out, if it should only happen if there are incoming
     dependencies on \a replace.

     Either the \a in or \a out tasks may be the same as the \a replace
     task (if both are the same as \a replace, nothing is done).

     If the \a replace task is the current task, \a in must be the current
     task too, and \a out must not be the current task. In this case, \a out
     operates as a continuation of the current task.

     If the replaced task is in the constructing or depending state, and
     neither \a in nor \a out are the same as \a replace, the replacement
     task will be set directly to the finalising state. It is the
     responsibility of the task's author to ensure that replaced tasks can
     cope with their worker function not running.

     The task references \a replace, \a in, and \a out are all owned by the
     caller, this function will not release any of these references.

     \retval TRUE   The task replacement was successful.
     \retval FALSE  The task replacement was not successful.
*/
Bool task_replace(/*@notnull@*/ /*@in@*/ task_t *replace,
                  /*@notnull@*/ /*@in@*/ task_t *in,
                  /*@notnull@*/ /*@in@*/ task_t *out) ;

/** \brief Make a task ready to run.

    \param[in] task
               The task to make schedulable, once all dependee tasks
               have completed successfully.
*/
void task_ready(/*@notnull@*/ /*@in@*/ task_t *task) ;

/** \brief Has the current task been cancelled?

    \retval TRUE  if the current task has been cancelled.
    \retval FALSE if the current task has not been cancelled.
*/
Bool task_cancelling(void) ;

/** \brief Task specialiser function for multi-threaded RIPs.

    Task localisation allows changes that must be made to the Core Context
    for a class of tasks to be performed once, and shared between instances
    of the task class. A task specialiser function may be passed to
    \c task_create() when specifying a task.

    The specialiser function can specialise parts of the tasks's core context
    by copying the previous contents to a stack-local variable, and resetting
    the context's pointer to point at the stack variable. If multiple tasks
    share the same specialiser, the task specialiser function may only be
    called once before several instances of the task worker function, so the
    specialiser should not modify the core context in a way that is specific
    to a single task.

    The last action a task specialiser must take is to call \c
    task_specialise_done(), passing it the context and data pointers the
    specialiser received in its own arguments.

    \param context  The context that will be passed to tasks. Specialisers
                    should modify this context. This will usually be done by
                    making copies of context sub-structures and replacing the
                    pointer in this context with a pointer to the stack copy.
    \param args     The task specialiser argument pointer, passed through from
                    task_create().
    \param data     An opaque reference passed through the task specialiser.
*/
void task_specialise_done(/*@notnull@*/ /*@in@*/ corecontext_t *context,
                          /*@null@*/ void *args,
                          /*@null@*/ task_specialise_private *private) ;

/** \brief Task specialiser function for interpreter tasks.
 */
void interpreter_task_specialise(/*@notnull@*/ /*@in@*/ corecontext_t *context,
                                 /*@null@*/ void *args,
                                 /*@null@*/ task_specialise_private *private) ;

/** \brief Create a task vector, with all of the slots empty.

    \param[out] newvec A pointer to a new task vector with \a slots allocated,
          or NULL if a VMERROR occurred. If a new task vector was allocated,
          it has one reference to it, which must be released before the vector
          will be released.
    \param slots The number of slots to allocate in the new task vector.

    \retval TRUE If creation of the new task vector succeeded.
    \retval FALSE If creation of the new task vector failed.
*/
Bool task_vector_create(/*@notnull@*/ /*@out@*/ task_vector_t **newvec,
                        unsigned int slots) ;

/** \brief Clone a reference to a task vector.

    \param[in] vector A task vector pointer to clone.

    \returns Another pointer referencing the input task vector. The cloned
    reference MUST be released with \c task_vector_release().

    Access to the task vector is synchronised.
*/
/*@dependent@*/ /*@notnull@*/
task_vector_t *task_vector_acquire(/*@notnull@*/ task_vector_t *vector) ;

/** \brief Release a reference to a task vector, clearing the reference.

    \param[in,out] vector The vector reference to release. On entry, the
    vector reference must represent a valid vector. On exit, the vector
    pointer is NULL.

    When the last reference to a task vector is released, all of the tasks
    referenced are released, and the memory allocated to the task vector is
    released. Access to the task vector is synchronised.
*/
void task_vector_release(/*@notnull@*/ /*@in@*/ /*@out@*/ task_vector_t **vector) ;

/** \brief Return the length of a task vector.

   \param[in] vector The vector to return the length of.

   \returns The number of slots allocated for this vector by \c
   task_vector_create().
*/
unsigned int task_vector_length(/*@notnull@*/ /*@in@*/ const task_vector_t *vector) ;

/** \brief Store a task and arguments in a task vector, releasing any task
   that previously occupied the slot.

   \param vector A pointer to a task vector allocated by \c task_vector_create.
   \param slot The index to the slot in which the task will be stored. An
               assertion will be raised if this is out of range.
   \param[in,out] task The task to store in the slot specified. This may be
               NULL, if a task reference is to be released without replacing
               it with another task.
   \param[in] args The task args to store in the slot specified. This is
               optional, it is for the client's benefit in being able to keep
               an accessible reference to the task args with the task.

   Access to the task vector is synchronised.
*/
void task_vector_store(/*@notnull@*/ /*@in@*/ /*@out@*/ task_vector_t *vector,
                       unsigned int slot,
                       /*@null@*/ /*@in@*/ /*@out@*/ task_t *task,
                       /*@null@*/ /*@in@*/ void *args) ;

/** \brief Task system initialisation. */
void task_C_globals(struct core_init_fns *fns) ;

/** \brief Contextualised RIP lifetime thread.

    \note Do not use this function. This is for compatibility threads only.

    \param thread_fn  The function to run when the thread is spawned.
    \param started    A pointer to a boolean which will be set to TRUE when the
                      thread is spawned (just before the thread function is
                      called). This boolean should have a scope suitable for
                      accessing at any time during the RIP's lifetime.
    \param thread     The thread handle. This is only valid when the thread
                      has been spawned. The thread handle should have a scope
                      suitable for accessing at any time during the RIP's
                      lifetime.

    \retval TRUE  If the thread was successfully registered for running when
                  the task API starts.
    \retval FALSE If the thread could not be registered.

    This function allows a RIP-lifetime thread to be spawned, with its own
    core context and SwThreadIndex. It should only be called during the
    SwStart() initialisation phase. The thread creation will be deferred
    until the task API is initialised.

    This is not a general thread spawn function. Threads spawned by this
    function permanently consume RIP resources, so it should only be used for
    threads that last the lifetime of the RIP.

    The caller is responsible for shutting down and joining the thread safely
    at the end of the RIP's lifetime.

    Note that there is no capability to either pass arguments to the thread
    function, or return a value from it. This is deliberate: use the task API
    instead.
*/
Bool deferred_thread_spawn(/*@out@*/ /*@notnull@*/ pthread_t *thread,
                           /*@out@*/ /*@notnull@*/ Bool *started,
                                     /*@notnull@*/ void (*thread_fn)(corecontext_t *, void*)) ;


/** \brief Wait for more memory to become available.

    \retval TRUE If did wait and some tasks have released memory.
    \retval FALSE If there was nothing to wait for.

Note that there is no guarantee that the memory released will be available to
the caller, only that if there is none, FALSE will be returned eventually.
*/
Bool task_wait_for_memory(void);


/** \brief Call a RIP function with a RIP thread context.
 *
 * \param[in] func The function to call.
 *
 * \param[in] arg Data pointer to pass to the called function.
 *
 * This function ensures that there is a valid thread context for the thread
 * when calling the RIP function.  It will be called typically in RIP defined
 * event handlers which can be invoked by non-RIP threads.  A temporary context
 * is created if the thread does not already have one, to cope with the case
 * where an event handler is invoked by a RIP thread which already has a
 * context.
 */
void task_call_with_context(
  void (*func)(corecontext_t*, void*),
  void* arg);

/** \brief Return the maximum number of tasks simultaneously active. */
int32 max_simultaneous_tasks(void) ;

#ifdef DEBUG_BUILD
extern int32 debug_tasks ;

/* Debug features bitmask. */
enum {
  DEBUG_TASKS_RUNREADY = 1, /* Don't run tasks as soon as they're ready. */
  DEBUG_TASKS_RESCHEDULE = 2, /* Recompute schedules at every opportunity. */
  DEBUG_TASKS_GRAPH_JOIN = 4, /* Graph schedule on first iteration of join. */
  DEBUG_TASKS_GRAPH_HELP = 8  /* Graph schedule on first iteration of helper. */
} ;
#endif

/** \} */

#endif /* __TASKH_H__ */

/* Log stripped */
