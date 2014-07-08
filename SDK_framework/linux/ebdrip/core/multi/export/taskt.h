/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!export:taskt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Thread-pooling task API types.
 */

#ifndef __TASKT_H__
#define __TASKT_H__

#include "hqatomic.h"

/** \ingroup multi */
/** \{ */

/** \brief Representation of a task. */
typedef struct task_t task_t ;

/** \brief Opaque type for passing data through the task specialiser. */
typedef struct task_specialise_private task_specialise_private ;

/** \brief Task group type. */
typedef struct task_group_t task_group_t ;

/** \brief Task group types.

    These are defined using a macro expansion, so the names can be re-used
    for other purposes (stringification, usage strings) without having to
    manually keep enumerations and strings in step. The task system doesn't
    assign any semantics or order relationships to these names. They are
    used by tasks to search the group hierarchy for the right ancestor task
    group.
*/
#define TASK_GROUP_TYPES(macro_) \
  macro_(ROOT) \
  macro_(JOB) /* All tasks in a job */ \
  macro_(COMPLETE) /* Special group to contain job join task. */ \
  macro_(PAGE) /* All tasks in a page (inside job) */ \
  macro_(RENDER) /* Render tasks in a page (inside page) */ \
  macro_(ERASE) /* Special group to contain page join task */ \
  macro_(SHEET) \
  macro_(FRAME) \
  macro_(BAND) \
  macro_(TRAP) \
  macro_(ORPHANS) /* Finalised tasks with references to them. */

#define TASK_GROUP_ENUM(x) TASK_GROUP_ ## x,

/** \brief Task group enumeration. */
typedef enum {
  TASK_GROUP_TYPES(TASK_GROUP_ENUM)
  TASK_GROUP_LIMIT /**< Must be last in task group type list. */
} task_grouptype_t ;

/** \brief Resource requirements for a task group. */
typedef struct resource_requirement_t resource_requirement_t ;

/** \brief Expression node describing resource requirement computation. */
typedef struct requirement_node_t requirement_node_t ;

/** \brief Managed task vector for clients.

    This structure is for the convenience of task system clients, so that
    each client that wishes to collect task references does not need to roll
    its own structure. Task vectors with a fixed length can be allocated
    using \c task_vector_create(). The memory for the task vector is managed
    by the task system. Tasks are stored in the vector using
    task_vector_store(). References to the task vector are released using
    task_vector_release(). When the last reference to a task vector is
    released, all of the task references in the vector will be released.
 */
typedef struct task_vector_t {
  unsigned int length ;   /**< Number of elements in task vector. */
  hq_atomic_counter_t refcount ; /**< Reference count to task vector. */
  struct {
    task_t *task ;        /**< Task reference. */
    void *args ;          /**< Task arguments. */
  } element[1] ;          /**< Extendable array of task details. */
} task_vector_t ;

/** \brief Function type to specialise the core context for all tasks of a
    particular class.

    \param context  The core context of the thread. This is modified by the
                    specialiser, in preparation for running tasks.
    \param args     A parameter to the specialiser function, passed in from
                    task_create(), usable for setting up the context. This
                    pointer is also used to distinguish tasks that can be
                    scheduled on this context.
    \param data     An opaque pointer, passed through the task specialiser to
                    task_specialise_done().

    \retval TRUE  The worker function completed successfully.
    \retval FALSE The worker function failed, and has signalled an error.
 */
typedef void (task_specialiser_fn)(corecontext_t *context,
                                   void *args,
                                   task_specialise_private *data) ;

/** \brief Type definition for a task worker function.

    \param[in] context  The core context of the thread on which the worker is
                        scheduled.
    \param[in] args     A task-specific set of arguments.

    \retval TRUE  The worker function completed successfully.
    \retval FALSE The worker function failed, and has signalled an error.
*/
typedef Bool (task_fn)(corecontext_t *context, void *args) ;

/** \brief Function type to cleanup allocations in task arguments.

    \param[in] context  The core context of the thread on which the finaliser
                        is scheduled.
    \param[in] args     A task-specific set of arguments.
*/
typedef void (task_cleanup_fn)(corecontext_t *context, void *args) ;

/** \} */

#endif /* __TASKT_H__ */

/* Log stripped */
