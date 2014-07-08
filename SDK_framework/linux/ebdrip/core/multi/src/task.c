/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!src:task.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Task API implementation.
 */

#include "core.h"
#include "coreinit.h"
#include "coreparams.h"
#include "ripdebug.h"
#include "debugging.h"
#include "exitcodes.h"
#include "genkey.h"
#include "hqatomic.h"
#include "hqmemset.h"
#include "lists.h"
#include "metrics.h"
#include "mlock.h"
#include "mm.h"
#include "multipriv.h"
#include "namedef_.h"
#include "objects.h"
#include "dictscan.h"
#include "objnamer.h"
#include "ripmulti.h"
#include "security.h"
#include "swcopyf.h"
#include "swerrors.h"
#include "swstart.h"
#include "swtrace.h"
#include "taskh.h"
#include "timing.h"
#include "threadapi.h"
#include "taskres.h"
#include "hqspin.h" /* yield_processor() */
#include "dongle.h" /* secDevCanEnableFeature() */

#include <stddef.h> /* offsetof */

#define INTERPRETER_HELPS 0

/*****************************************************************************/
/* Tuning defines and variables. In a non-probe build, the variables are
   fixed to the default values defined by the constants. In a probe build,
   they can be modified to test the effect of different strategies. */

/** Default milliseconds to wait in joins before trying to help the joining
    tasks out. This can be quite long, because the join condition variable is
    signalled if something changes that might make it worth walking the
    graph. */
#define JOIN_WAIT_mS 500

/** Variable for milliseconds to wait in joins. If set to zero or less, joins
    block until a possibly joined task completes. */
static int join_wait_ms ;

/** Default milliseconds to stall task producer in task_helper_locked() when
    we have way too many tasks in the task graph. This is deliberately quite
    a high value, because the only circumstance it's used in is when we're
    desperate to reduce the number of tasks, and there should be a wakeup
    when the number of tasks falls low enough to continue anyway. */
#define HELPER_WAIT_mS 500

/** Variable for milliseconds to wait in task helper. If set to zero, the
    task helper is disabled. */
static int helper_wait_ms ;

/** A crude threshold for determining when there are too many tasks in
    existence, and the interpreter should assist in running some of the
    tasks. This should really be replaced by an adaptive method that keeps
    the task generators a little ahead of the task consumers, but not too far
    ahead. A similar algorithm to that used to determine ethernet
    transmission rates would probably work. */
#define TASK_HELPER_START 200

/** Start helping to run tasks when greater or equal to this threshold. */
static unsigned int helper_start_threshold ;

/** The number of outstanding tasks when the ready/cancel task helper gives
    up trying to help. This must be less than or equal to
    \c TASK_HELPER_START. */
#define TASK_HELPER_DONE (TASK_HELPER_START / 2)

/** Stop helping to run tasks when less than this threshold. */
static unsigned int helper_end_threshold ;

/** A crude threshold for determining when there are way too many tasks in
    existence, and the task system should slow down the producer of tasks to
    allow the consumers to catch up. This is a measure of last resort to avoid
    overflowing the C stack when recursively searching for tasks. */
#define TASK_HELPER_WAIT (TASK_HELPER_START * 2)

/** Throttle the task producer when tasks greater or equal to this threshold. */
static unsigned int helper_wait_threshold ;

/*****************************************************************************/

/** \brief Thread state. */
enum {
  THREAD_RUNNING,               /**< Thread running. */
  THREAD_WAIT_JOIN,             /**< Thread waiting in join. */
  THREAD_WAIT_HELP,             /**< Thread waiting in helper. */
  THREAD_WAIT_DISPATCH,         /**< Thread waiting on dispatch. */
  THREAD_WAIT_MEMORY,           /**< Thread waiting on memory. */
  THREAD_SIGNALLED              /**< Thread signalled to wake up. */
} ;

/** \brief Thread state type is atomic counter, because it sometimes needs
    to be changed outside of the task mutex. */
typedef hq_atomic_counter_t thread_state_t ;

/** \brief Thread-local task context.

    There is one task context per thread. The current task is saved and reset
    for each activation (thread or recursive). */
typedef struct task_context_t {
  corecontext_t *thread_context ; /**< The parent context of the thread. */
  task_t *current_task ;          /**< Current task of the thread. */
  multi_condvar_t cond ;          /**< Condition this thread waits on. */
  dll_link_t thread_list ;        /**< Link on thread list. */
  thread_state_t state ;          /**< Thread state. */
  OBJECT_NAME_MEMBER
} task_context_t ;

#define TASK_CONTEXT_NAME "Task thread context"

/*****************************************************************************/
/** \brief Generation counts for group provisioning, task changes. */
typedef int task_generation_t ;

typedef enum {
  PROVISION_OK,          /**< Group is provisioned. */
  PROVISION_REMOVED,     /**< Group de-provisioned. */
  PROVISION_CANCELLED,   /**< Group cancelled. */
  /* Everything after this indicates that provisioning failed. */
  PROVISION_FAIL_NEVER,  /**< Group never provisioned. */
  PROVISION_FAIL_CONSTR, /**< Group provisioning failed on constructing. */
  PROVISION_FAIL_REQLIMIT, /**< Group provisioning failed on requirement limit. */
  PROVISION_FAIL_REQREADY, /**< Group provisioning failed on requirement ready. */
  PROVISION_FAIL_REQPREV, /**< Group provisioning failed on previous requirement. */
  PROVISION_FAIL_POOL,   /**< Group provisioning failed on pool maximum. */
  PROVISION_FAIL_ALLOC,  /**< Group provisioning failed on alloc. */
  PROVISION_FAIL_PREV,   /**< Provisioning failed on previous group. */
  PROVISION_FAIL_STOP,   /**< Group walk stopped by callback. */
  PROVISION_FAIL_GENERATION, /**< Group provisioning failed on generation. */
  N_GROUP_PROVISIONS
} group_provisions_t ;

/** \brief Determine if this group needs provisioning. */
#define NEEDS_PROVISION(g) ((g)->provisions > PROVISION_CANCELLED)

/** \brief Task group states.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having to
 * manually keep enumerations and strings in step.
 */
#define TASK_GROUP_STATES(macro_) \
  macro_(CONSTRUCTING) /* Task group cannot be provisioned */ \
  macro_(ACTIVE)       /* Tasks may be run from group or added to group */ \
  macro_(CLOSED)       /* Tasks may be run but not added to group */ \
  macro_(CANCELLED)    /* Task group has been cancelled but not joined */ \
  macro_(JOINED)       /* Task group has been joined */

#define GROUP_STATE_ENUM(x) GROUP_STATE_ ## x,

/** \brief Task group states. */
typedef enum {
  TASK_GROUP_STATES(GROUP_STATE_ENUM)
  GROUP_STATE_LIMIT /**< Must be last enum value. */
} task_group_state_t ;

struct task_group_t {
  task_group_state_t state ; /**< What can be done to group? */
  hq_atomic_counter_t refcount ; /**< Reference count. */
  task_group_t *parent ;   /**< Parent group of this group. */
  dll_list_t children ;    /**< Child groups, in creation order. */
  dll_link_t peers ;       /**< Link in parent's group children list. */
  dll_list_t tasks ;       /**< List of tasks in this group. */
  dll_link_t joins_link ;  /**< Link in a task's joins list. */
  dll_link_t order_link ;  /**< Link in provision schedule. */
  task_t *join ;           /**< Join task for this group. */
  resource_requirement_t *required ; /**< Requirements for sub-groups. */
  requirement_node_t *reqnode ; /**< Requirements for tasks in this group. */
  int n_tasks ;            /**< Number of tasks in this group. */
  /** Are scheduling resources assigned? This may turn into a more
      complicated structure in future, such as a Rete net, to be able to
      determine how close this group is to being provisioned. */
  group_provisions_t provisions ;
  task_generation_t generation ; /**< Resource generation used to try provisioning. */
  task_grouptype_t type ;  /**< Type of this group. */
  Bool result ;            /**< Did all of the sub-tasks run successfully? */
  task_mark_t mark ;       /**< Mark for task graphing. */
  error_context_t error ;  /**< Captured errors from this group. */
  /** The number of each resource type that have been fixed in this group,
      and not subsequently unfixed. This includes resources which have been
      detached, which are still counted against the maximum resources for the
      group. This table should only be modified or examined with the relevant
      resource pool locked. */
  unsigned int nfixed[N_RESOURCE_TYPES] ;
#if defined(DEBUG_BUILD)
  Bool being_joined ;      /**< Being joined right now. */
#endif
  OBJECT_NAME_MEMBER
} ;

#define TASK_GROUP_NAME "Task group"

/** \brief Task states.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having to
 * manually keep enumerations and strings in step.
 */
#define TASK_STATES(macro_) \
  macro_(CONSTRUCTING) /* Task is being constructed. */ \
  macro_(DEPENDING)    /* Task depends on another before scheduling. */ \
  macro_(READY)        /* Task is ready to be scheduled. */ \
  macro_(CANCELLED)    /* Task has been cancelled but not finalised. */ \
  macro_(RUNNING)      /* Task is running. */ \
  macro_(CANCELLING)   /* Task is marked cancelled, but is still running. */ \
  macro_(FINALISING)   /* Task is being finalised. */ \
  macro_(DONE)         /* Task is finished and finalised. */

#define TASK_STATE_ENUM(x) TASK_ ## x,

/** \brief Task states. */
typedef enum {
  TASK_STATES(TASK_STATE_ENUM)
  TASK_STATE_LIMIT /**< Must be last enum value. */
} task_state_t ;

/** \brief Runnability levels.

    These are defined using a macro expansion, so the names can be re-used
    for other purposes (stringification, usage strings) without having to
    manually keep enumerations and strings in step. The ordering represents
    the desirability of selecting tasks for various scheduling searches, from
    most to least desirable. */
#define RL_STATES(macro_) \
  macro_(JOINS_IS_EMPTY)    /* Task joins empty groups. */ \
  macro_(JOINS_NOTHING)     /* Task joins no groups. */ \
  macro_(JOINS_MAYBE_EMPTY) /* Task joins groups that may get sub-tasks. */ \
  macro_(JOINS_NOT_EMPTY)   /* Task joins groups with sub-tasks. */ \
  macro_(RUNNING)           /* Task is running. */ \
  macro_(READY_UNPROVISIONED) /* Task is ready, but group is not. */ \
  macro_(NOTREADY)          /* Task is not ready to run. */ \

#define RL_STATE_ENUM(x) RL_ ## x,

/** \brief Runnability level of task.

    This ordering represents the desirability of selecting tasks for various
    scheduling searches, from most to least desirable. */
typedef enum {
  RL_STATES(RL_STATE_ENUM)
  /** All runnability levels strictly before RL_ANY. */
  RL_ANY,
  /** No runnability levels strictly before RL_NONE. */
  RL_NONE = RL_JOINS_IS_EMPTY,
  /** Everything strictly before RL_NOW stops a search immediately. */
  RL_NOW = RL_JOINS_MAYBE_EMPTY,
  /** Everything strictly before RL_HELPABLE is helpable. */
  RL_HELPABLE = RL_JOINS_MAYBE_EMPTY,
  /** Everything strictly before RL_DISPATCHABLE is dispatchable. */
  RL_DISPATCHABLE = RL_RUNNING
} task_runnability_t ;

struct task_t {
  task_state_t state ;           /**< Task state. */
  task_runnability_t runnability ; /**< Runnability for incomplete tasks. */
  hq_atomic_counter_t refcount ; /**< Reference count. */
  task_specialiser_fn *specialiser ; /**< Task specialiser function. */
  void *specialiser_args ;       /**< Task specialiser args. */
  task_fn *worker ;              /**< The meat of the task. */
  task_cleanup_fn *cleaner ;     /**< Cleanup function for args. */
  void *args ;                   /**< Task arguments. */
  task_group_t *group ;          /**< Group that task belongs to. */
  dll_link_t group_link ;        /**< Link in group's task list. */
  dll_link_t order_link ;        /**< Link in schedule. */
  dll_list_t joins_list ;        /**< List of groups this task joins. */
  dll_list_t beforeme ;          /**< Task entry list for predecessors. */
  dll_list_t afterme ;           /**< Task entry list for successors. */
  int trace_id ;                 /**< Trace ID for task. */
  multi_condvar_t *waiting_on ;  /**< Condition that task is waiting on. */
  Bool result ;                  /**< The result of running the task. */
  task_mark_t mark ;             /**< Mark for task graphing. */
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  task_t *recursive ;            /**< Recursively activated task. */
  unsigned int thread_index ;    /**< Thread this was scheduled on. */
#endif
  OBJECT_NAME_MEMBER
} ;

#define TASK_NAME "Task descriptor"

/** A two-way link between two tasks. This is used for 1:1 relationships
    (such as joiner:joinee), 1:M relationships and N:M relationships
    (dependee:dependent). */
typedef struct task_link_t {
  task_t *pre ;
  dll_link_t pre_list ;
  dll_link_t post_list ;
  task_t *post ;
} task_link_t ;

/** Bitmask of what we can follow up this task with. */
typedef enum {
  SUCC_NONE = 0,   /**< No successor tasks. */
  SUCC_GROUP = 1,  /**< Tasks in same group OK. */
  SUCC_SPEC = 2    /**< Tasks with same specialiser OK. */
} task_successor_t ;

/** The data passed through the task specialiser is the task to run, a
    pointer to a group if we're recursively joining (so we can determine
    whether if a dependent task contributes to the join), and an assertion
    count that is used to ensure that task_specialise_done() is called
    exactly once at the end of the specialisation chain. */
struct task_specialise_private {
  task_t *task ;           /**< First task to run */
  task_successor_t succ ;  /**< What can we do after this task? */
#ifdef ASSERT_BUILD
  int assert_count ;       /**< Assert calls to task_specialise_done. */
#endif

  OBJECT_NAME_MEMBER
} ;

#define TASK_SPECIALISE_NAME "Task specialiser data"

/** Registration structure for compatibility threads. */
typedef struct deferred_thread_t {
  void (*thread_fn)(corecontext_t *, void *) ;
  Bool *started ;
  pthread_t *thread ;
  struct deferred_thread_t *next ;

  OBJECT_NAME_MEMBER
} deferred_thread_t ;

#define DEFERRED_THREAD_NAME "Deferred thread"

/****************************************************************************/

/** \brief Dispatch one task recursively on current thread. */
static void task_recursive(corecontext_t *context,
                           task_t *current, task_t *ready,
                           task_successor_t succ) ;

/** \brief Mark a task for cancellation. */
static void task_cancel_locked(/*@notnull@*/ /*@in@*/ task_t *task,
                               /*@notnull@*/ /*@in@*/ const error_context_t *error) ;

/** \brief Mark a group for cancellation. */
static void group_cancel_locked(/*@notnull@*/ /*@in@*/ task_group_t *group,
                                /*@notnull@*/ /*@in@*/ const error_context_t *error) ;

/** \brief Run a task, using the thread's context. */
static void task_run(corecontext_t *context, task_t *task,
                     task_successor_t succ) ;

/** \brief Helper function for task_group_join(), to recursively mark a group
    as joined.

    Groups are marked as joined after joining all sub-groups, because the
    group tasks can introduce new sub-graphs into the group or its sub-groups
    itself. */
static void group_mark_joined(task_group_t *group) ;

/****************************************************************************/

#ifdef DEBUG_BUILD
int32 debug_tasks ;

#define STRINGIFY(x) #x,
const static char *task_names[CORE_TRACE_N] = {
  "INVALID", /* SW_TRACE_INVALID */
  SW_TRACENAMES(STRINGIFY)
} ;

const static char *task_states[] = {
  TASK_STATES(STRINGIFY)
} ;

const static char *group_names[] = {
  TASK_GROUP_TYPES(STRINGIFY)
} ;

const static char *group_states[] = {
  TASK_GROUP_STATES(STRINGIFY)
} ;

const static char *run_states[] = {
  RL_STATES(STRINGIFY)
} ;

#undef STRINGIFY
#endif

/** Number of threads. \note This variable is considered obsolete. */
unsigned int multi_nthreads;

/** This pool is used for task and task group allocations, dynamic
    multi_mutex_t and multi_condvar_t allocations. */
mm_pool_t mm_pool_task ;

/** Type for thread counts and limits. */
typedef hq_atomic_counter_t thread_count_t ;

/** Limit on the number of active threads in the pool. */
static thread_count_t thread_limit_active ;
/** Hard limit on the number of threads in the pool. */
static thread_count_t thread_limit_max ;

/** Soft limit on number of threads set by tag. */
static thread_count_t limit_active_original ;
/** Hard limit on number of threads set by tag. */
static thread_count_t limit_max_original ;

/** Soft limit on number of threads from MaxThreads system param. */
static thread_count_t maxthreads_active ;
/** Hard limit on number of threads from MaxThreads system param. */
static thread_count_t maxthreads_max ;

/** Requested limit on the number of active threads in the pool from system
    param. */
static thread_count_t request_limit_active ;

/** Current limit on the number of active threads in the pool. This may be
    temporarily extended while waiting for a task to complete. Extension of
    the thread pool may cause this variable to exceed thread_limit_max, so
    any tests that use it should be protected by the task mutex, and should
    also test against thread_limit_max.

    Changes to this variable MUST be done using atomic operations, because
    in multi_condvar_wait_task() it is used and modified outside of a task
    mutex lock. All other modifications must use atomic operations to
    interleave with this case safely. */
static thread_count_t thread_limit_current ;

/** Low memory constraint semaphore. Single-threaded low-memory handlers can
    run when this counter is non-zero and thread_limit_current is zero. */
static hq_atomic_counter_t thread_pool_constrained ;

/** Number of threads started successfully. This includes pool threads and
    deferred threads. */
static thread_count_t thread_total_started ;

/** Number of pool threads started successfully. */
static thread_count_t thread_pool_started ;

/** The number of threads currently scheduled, including the interpreter
    threads. This may exceed the maximum number of active threads if the
    active threads limit is temporarily extended while waiting for a task to
    complete. This variable should only be modified when \c
    task_mutex is locked. */
static thread_count_t threads_scheduled ;

/** The number of threads waiting in joins for resources to become available.
    This variable should only be modified when \c task_mutex is locked. */
static thread_count_t threads_waiting_for_resources ;

/** Limit on the number of active threads allowed by limit_active_password */
static thread_count_t limit_active_from_password ;

/** The password to enable the number of threads specified by
    limit_active_from_password. */
static int32 limit_active_password ;

/** The number of tasks that have not yet been completed. The total number of
    tasks allocated is the sum of this value and \c tasks_incomplete. This is
    used to determine when the caller of task_ready() should attempt to run
    tasks. This may ONLY be accessed whilst holding the \c task_mutex. */
static unsigned int tasks_incomplete ;

/** The number of tasks completed, but not yet deallocated. The total number
    of tasks allocated is the sum of this value and \c tasks_complete. This
    may ONLY be accessed whilst holding the \c task_mutex. */
static unsigned int tasks_complete ;

/** The number of tasks that are runnable, provisioned (if ready), and can be
    run by task helpers. This count must only be accessed with the task mutex
    locked. */
static unsigned int tasks_runnable_helpable ;

/** The number of tasks that are runnable, provisioned (if ready), but can
    not be run by task helpers. This count must only be accessed with the
    task mutex locked. */
static unsigned int tasks_runnable_nothelpable ;

/** The number of tasks that are ready, but not yet provisioned. This count
    must only be accessed with the task mutex locked. */
static unsigned int tasks_ready_unprovisioned ;

/** The number of groups with resource requests provisioned. Groups are only
    provisioned when they have tasks ready to be scheduled, and all
    predecessor's groups are provisioned too. If this count is non-zero,
    there are groups that will release resources when their tasks complete.
    This count indicates whether. This may ONLY be accessed whilst holding
    the \c task_mutex. */
static unsigned int groups_provisioned ;

/** The number of task groups that have not yet been completed. The total
    number of groups allocated is the sum of this value and
    \c groups_incomplete. This may ONLY be accessed whilst holding the \c
    task_mutex. */
static unsigned int groups_incomplete ;

/** The number of task groups completed, but not yet deallocated. The total
    number of groups allocated is the sum of this value and \c
    groups_incomplete. This may ONLY be accessed whilst holding the \c
    task_mutex. */
static unsigned int groups_complete ;

#ifdef PROBE_BUILD
/** The number of tasks active on a thread. These tasks are in state
    TASK_RUNNING, TASK_CANCELLING, or TASK_FINALISING. This may
    ONLY be accessed whilst holding the \c task_mutex. */
static int tasks_active ;

#define INCREMENT_TASKS_ACTIVE() MACRO_START \
  HQASSERT(tasks_active >= 0, "Too few tasks active") ; \
  ++tasks_active ; \
  probe_other(SW_TRACE_TASKS_ACTIVE, SW_TRACETYPE_AMOUNT, tasks_active) ; \
MACRO_END

#define DECREMENT_TASKS_ACTIVE() MACRO_START \
  --tasks_active ; \
  HQASSERT(tasks_active >= 0, "Too few tasks active") ; \
  probe_other(SW_TRACE_TASKS_ACTIVE, SW_TRACETYPE_AMOUNT, tasks_active) ; \
MACRO_END

/** Threads doing real work, not waiting. This is used to monitor processor
    core utilisation. */
static thread_count_t threads_active ;

#define INCREMENT_THREADS_ACTIVE() MACRO_START \
  hq_atomic_counter_t _before_ ; \
  HqAtomicIncrement(&threads_active, _before_) ; \
  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_AMOUNT, _before_ + 1) ; \
MACRO_END

#define DECREMENT_THREADS_ACTIVE() MACRO_START \
  hq_atomic_counter_t _after_ ; \
  HqAtomicDecrement(&threads_active, _after_) ; \
  HQASSERT(_after_ >= 0, "Active thread count went negative") ; \
  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_AMOUNT, _after_) ; \
MACRO_END

static int pointless_wakeups ;
static int pointless_timeouts ;
static int pointless_traverses ;

#define TRACE_POINTLESS(trace_, count_, add_) MACRO_START \
  if ( (add_) > 0 ) {                                     \
    count_ += (add_) ;                                    \
    probe_other(trace_, SW_TRACETYPE_VALUE, count_) ;     \
    add_ = 0 ;                                            \
  }                                                       \
MACRO_END

#define MAYBE_POINTLESS(add_) MACRO_START \
  ++(add_) ;                              \
MACRO_END

#define WASNT_POINTLESS(add_) MACRO_START \
  add_ = 0 ;                            \
MACRO_END

static int task_walk_recursion ;
static int max_walk_recursion ;

#define START_WALK_RECURSION() MACRO_START \
  task_walk_recursion = max_walk_recursion = 0 ; \
MACRO_END

#define END_WALK_RECURSION() MACRO_START \
  probe_other(SW_TRACE_TASK_GRAPH_WALK, SW_TRACETYPE_VALUE, max_walk_recursion) ; \
MACRO_END

#define INCREMENT_WALK_RECURSION() MACRO_START \
  if ( ++task_walk_recursion > max_walk_recursion ) \
    max_walk_recursion = task_walk_recursion ; \
MACRO_END

#define DECREMENT_WALK_RECURSION() MACRO_START \
  --task_walk_recursion ; \
MACRO_END

#else /* !PROBE_BUILD */
#define INCREMENT_TASKS_ACTIVE() EMPTY_STATEMENT()
#define DECREMENT_TASKS_ACTIVE() EMPTY_STATEMENT()
#define INCREMENT_THREADS_ACTIVE() EMPTY_STATEMENT()
#define DECREMENT_THREADS_ACTIVE() EMPTY_STATEMENT()
#define TRACE_POINTLESS(trace_, count_, add_) EMPTY_STATEMENT()
#define MAYBE_POINTLESS(add_) EMPTY_STATEMENT()
#define WASNT_POINTLESS(add_) EMPTY_STATEMENT()
#define START_WALK_RECURSION() EMPTY_STATEMENT()
#define END_WALK_RECURSION() EMPTY_STATEMENT()
#define INCREMENT_WALK_RECURSION() EMPTY_STATEMENT()
#define DECREMENT_WALK_RECURSION() EMPTY_STATEMENT()
#endif /* !PROBE_BUILD */

/****************************************************************************/

/** Generation count for task resource changes. This generation count is
    incremented when resources are made available which may make it
    worthwhile trying to provision task groups. This includes returning
    resources to pools, making task requirements that use resources ready,
    deprovisioning groups that use requirements. It is used to reduce futile
    task graph walks looking for resources to provision groups with. This
    must only be accessed with the task mutex locked. */
static task_generation_t group_resource_generation ;

/** Generation count for helpable runnable task list changes. This generation
    count is incremented when a task is added to the helpable tasks list. It
    is used to reduce futile task graph walks looking for tasks to help. This
    must only be accessed with the task mutex locked. */
static task_generation_t task_helpable_generation ;

/** Generation count for non-helpable runnable task list changes. This
    generation count is incremented when a task is added to the non-helpable
    tasks list. It is used to reduce futile task graph walks looking for
    tasks to dispatch. This must only be accessed with the task mutex
    locked. */
static task_generation_t task_nothelpable_generation ;

/** Generation count for unprovisioned ready task list changes. This
    generation count is incremented when a task is added to the unprovisioned
    but ready tasks list. It is used to reduce futile task graph walks
    looking for tasks to dispatch. This must only be accessed with the task
    mutex locked. */
static task_generation_t task_unprovisioned_generation ;

/** Generation count to track last helper activation. This count summarises
    all of the generation counts used to determine if anything has changed
    since the last time the task helper ran. */
static task_generation_t helper_generation ;

/** Generation count to track last dispatch activation. This count summarises
    all of the generation counts used to determine if anything has changed
    since the last time the task dispatcher ran. */
static task_generation_t dispatch_generation ;

/** Do we need to recompute the schedule? */
static Bool need_recompute_schedule ;

/** List of tasks, in execution order. New tasks are added to the tail of the
    list, and an indication is set that the schedule should be recomputed.
    All tasks that have not completed are present on this list, except the
    interpreter task. This list must only be accessed with the task mutex
    locked. */
static dll_list_t task_schedule ;

/** List of task groups, in provision order. New groups are added to the tail
    of the list, and an indication is set that the schedule should be
    recomputed. All groups that have not joined are present on this list,
    except the orphaned task group. This list must only be accessed with the
    task mutex locked. */
static dll_list_t group_schedule ;

/** List of all core threads. This list is constructed at startup, and
    destroyed on finalisation. It may be traversed without the task mutex
    locked, but must not be modified without the task mutex locked. */
static dll_list_t thread_list ;

/** Mutex used to control modification to task structures. */
static multi_mutex_t task_mutex ;

/** List of registered compatibility threads. */
static deferred_thread_t *deferred_threads ;

/* Count of registered compatibility threads. */
static thread_count_t deferred_thread_total ;
/****************************************************************************/

/** The interpreter task. This exists to make it easier to divert the
    interpreter thread without special case code. */
static task_t interpreter_task ;

/** The incomplete task group, containing all tasks that have not been
    finalised. This is the top-level of schedule scans. */
static task_group_t incomplete_task_group ;

/** The initial task system context. */
static task_context_t interpreter_task_context ;

/** The orphaned task group. This exists to make it easier to find, debug,
    and destroy tasks that are mis-counted. Tasks added to the orphaned task
    group are not reference counted, neither is the group. */
static task_group_t orphaned_task_group ;

enum {
  TASK_MARK_INIT /**< Initialiser value for task marks. */
} ;

/** \brief Visitor mark for graphing tasks.

    This is incremented before each task graph traversal. This variable
    should only be tested or changed with the task mutex locked. */
static task_mark_t task_visitor_mark ;

/** \brief A suitable mark to initialise new requirements. */
task_mark_t requirement_mark_init(void)
{
  return task_visitor_mark ;
}

/****************************************************************************/

#ifdef DEBUG_BUILD
/** Debug task graph count, so multiple states can be graphed in a run
    without overwriting files. */
static int task_graph_count = 0 ;

/** Was the debug sleeper started? */
static Bool debug_sleeping ;

/** Should the debug sleeper graph the tasks on wakeup? */
static int debug_sleep_graph ;

/** Should we graph the next schedule recomputations? */
static unsigned int debug_schedule_graph ;

/** The thread for the debug sleeper. */
static pthread_t debug_sleeper_thread ;

/** Condition for the debug sleeper thread. */
static multi_condvar_t debug_sleeper_condition;

/** Condition for the debug sleeper thread. */
static multi_mutex_t debug_sleeper_mutex;

/** This is a thread worked for debugging. It's hard to debug deadlocks,
    because the debugger may not all calling of functions when all threads
    are in kernel mode. So this function deliberately uses a timed condition
    wait, waking up once a minute. When broken in using the debugger, either
    set a breakpoint on the rip_is_quitting() test, or set \c debug_sleep_graph
    to a suitable non-zero value (greater than zero if the task mutex is
    not locked, less than zero if it is) to get a task graph output. */
static void debug_sleeper(corecontext_t *arg, void * xtra_arg)
{
  UNUSED_PARAM(corecontext_t *, arg) ;
  UNUSED_PARAM(void *, xtra_arg) ;

#ifdef DEBUG_BUILD
  set_thread_name("Debug sleeper") ;
#endif

  for (;;) {
    HqU32x2 when ;

    HqU32x2FromUint32( &when, (60 * 1000000) /*one minute from now*/ ) ;
    get_time_from_now(&when) ;
    multi_mutex_lock(&debug_sleeper_mutex);
    if ( !rip_is_quitting() )
      (void)multi_condvar_timedwait(&debug_sleeper_condition, when) ;
    multi_mutex_unlock(&debug_sleeper_mutex);

    if ( rip_is_quitting() )
      return ;

    /* Put a breakpoint here to interrupt the RIP in a deadlock, so you can
       call debug functions from user mode. Or, just set the
       debug_sleep_graph variable, continue the RIP and wait 30 seconds. So
       long is the task mutex is not locked, it will be OK. Check whether the
       task mutex is held in the deadlock before deciding which version of
       debug_graph_tasks() to call. */
    if ( debug_sleep_graph < 0 ) {
      debug_graph_tasks_locked() ;
      ++debug_sleep_graph ;
    } else if ( debug_sleep_graph > 0 ) {
      debug_graph_tasks() ;
      --debug_sleep_graph ;
    }
  }
}
#endif

#if defined(METRICS_BUILD)
static int tasks_total_allocated = 0 ;
static int links_total_allocated = 0 ;
static int groups_total_allocated = 0 ;
static int links_current_allocated = 0 ;
static int tasks_peak_allocated = 0 ;
static int links_peak_allocated = 0 ;
static int groups_peak_allocated = 0 ;
int requirements_total_allocated ;
int requirements_current_allocated ;
int requirements_peak_allocated ;

static Bool tasks_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Tasks")) )
    return FALSE ;
  SW_METRIC_INTEGER("tasks_total_allocated", tasks_total_allocated) ;
  SW_METRIC_INTEGER("tasks_peak_allocated", tasks_peak_allocated) ;
  SW_METRIC_INTEGER("tasks_current_allocated", (int)(tasks_incomplete + tasks_complete)) ;
  SW_METRIC_INTEGER("links_total_allocated", links_total_allocated) ;
  SW_METRIC_INTEGER("links_peak_allocated", links_peak_allocated) ;
  SW_METRIC_INTEGER("links_current_allocated", links_current_allocated) ;
  SW_METRIC_INTEGER("groups_total_allocated", groups_total_allocated) ;
  SW_METRIC_INTEGER("groups_peak_allocated", groups_peak_allocated) ;
  SW_METRIC_INTEGER("groups_current_allocated", (int)(groups_incomplete + groups_complete)) ;
  SW_METRIC_INTEGER("requirements_total_allocated", requirements_total_allocated) ;
  SW_METRIC_INTEGER("requirements_peak_allocated", requirements_peak_allocated) ;
  SW_METRIC_INTEGER("requirements_current_allocated", requirements_current_allocated) ;
  sw_metrics_close_group(&metrics) ; /* Tasks */

  if ( mm_pool_task ) { /* Track peak memory allocated in pool. */
    size_t max_size = 0, max_frag = 0;
    int32 max_objects ;
    mm_debug_total_highest(mm_pool_task, &max_size, &max_objects, &max_frag);
    if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
         !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Tasks")) )
      return FALSE ;
    SW_METRIC_INTEGER("PeakPoolSize", (int32)max_size) ;
    SW_METRIC_INTEGER("PeakPoolObjects", max_objects) ;
    SW_METRIC_INTEGER("PeakPoolFragmentation", (int32)max_frag) ;
    sw_metrics_close_group(&metrics) ; /* Tasks */
    sw_metrics_close_group(&metrics) ; /* MM */
  }

  return TRUE ;
}

static void tasks_metrics_reset(int reason)
{
  UNUSED_PARAM(int, reason) ;

  tasks_peak_allocated = (int)(tasks_incomplete + tasks_complete) ;
  groups_peak_allocated = (int)(groups_incomplete + groups_complete) ;
  links_peak_allocated = links_current_allocated ;
  requirements_peak_allocated = requirements_current_allocated ;
}

static sw_metrics_callbacks tasks_metrics_hook = {
  tasks_metrics_update,
  tasks_metrics_reset,
  NULL
} ;
#endif /* METRICS_BUILD */

/****************************************************************************/

/** \brief Helper function to find the next thread in a particular state. */
static inline task_context_t *find_next_thread(/*@null@*/ task_context_t *thread,
                                               thread_state_t state)
{
  while ( thread != NULL ) {
    if ( thread->state == state )
      return thread ;
    thread = DLL_GET_NEXT(thread, task_context_t, thread_list) ;
  }

  return thread ;
}

/** \brief Helper function to find the first thread in a particular state. */
static inline task_context_t *find_first_thread(thread_state_t state)
{
  return find_next_thread(DLL_GET_HEAD(&thread_list, task_context_t, thread_list),
                          state) ;
}

/** \brief Helper function to change the state of a thread, conditional
    on a previous state. */
static inline Bool thread_state_change(/*@notnull@*/ task_context_t *thread,
                                       thread_state_t oldstate,
                                       thread_state_t newstate)
{
  Bool swapped ;
  HqAtomicCAS(&thread->state, oldstate, newstate, swapped) ;
  if ( swapped ) {
    /* Task mutex may not be locked, so don't use multi_condvar_signal() or
       it'll assert. If the task mutex is not locked, then we accept the
       possibility of missing the signal: this is only used for thread
       extension, where either the thread extension will expire or another
       wakeup will allow the extended thread to be used. The worst case is
       that we're a little less efficient than we could be in using
       threads. */
    int res = pthread_cond_signal(&thread->cond.pcond);
    HQASSERT(res == 0, "condvar signal failed");
    UNUSED_PARAM(int, res) ;
    probe_other(SW_TRACE_THREAD_WAKE, SW_TRACETYPE_MARK,
                thread->thread_context->thread_index) ;
  }
  return swapped ;
}

/** \brief Helper function to change the state of all threads, conditional
    on a previous state. */
static inline void wake_all_threads(thread_state_t oldstate,
                                    thread_state_t newstate)
{
  task_context_t *thread ;

  for ( thread = DLL_GET_HEAD(&thread_list, task_context_t, thread_list) ;
        thread != NULL ;
        thread = DLL_GET_NEXT(thread, task_context_t, thread_list) ){
    /* The memory barrier implied by the CAS in thread_state_change barrier
       is expensive. We test the state before trying to change it, but we
       might just miss transitions into oldstate. */
    if ( thread->state == oldstate )
      (void)thread_state_change(thread, oldstate, newstate) ;
  }
}

/** \brief Helper function to change the state of any one threads, conditional
    on a previous state. */
static inline Bool wake_one_thread(thread_state_t oldstate,
                                   thread_state_t newstate)
{
  task_context_t *thread ;

  for ( thread = DLL_GET_HEAD(&thread_list, task_context_t, thread_list) ;
        thread != NULL ;
        thread = DLL_GET_NEXT(thread, task_context_t, thread_list) ){
    /* The memory barrier implied by the CAS in thread_state_change barrier
       is expensive. We test the state before trying to change it, but we
       might just miss transitions into oldstate. */
    if ( thread->state == oldstate &&
         thread_state_change(thread, oldstate, newstate) )
      return TRUE ;
  }

  return FALSE ;
}

/****************************************************************************/

/** \brief Extend the number of threads usable to compensate for the current
           thread pausing.

    Thread extension should be performed when the RIP is entering a wait, and
    another thread could possibly get some useful work done. The intention is
    to keep the current number of active threads busy.

    \retval TRUE  The number of threads usable has been extended, the caller
                  MUST call \c end_thread_extension() when ready to continue.
    \retval FALSE The number of threads usable has not been extended, the
                  caller MUST NOT call \c end_thread_extension().
*/
static Bool begin_thread_extension(void)
{
  Bool extended_thread_pool = FALSE ;
  volatile thread_count_t *tlc = &thread_limit_current ;
  thread_count_t currthreads = *tlc ;

  /* This thread is now going to block. Temporarily increase thread pool
     size whilst we wait, so we have the right number of active threads.

     We use CAS to increment the number of threads, in case another thread
     was extending at the same time without the task lock. (If there is a
     conflict, the active threads will only be increased by one, which will
     make the RIP very slightly less efficient. This is extremely unlikely to
     happen.)

     We only expand the thread pool limit if we haven't constrained to one
     thread for low memory handling. (I'm not sure if this case is actually
     possible to provoke, but it would cause problems if it happened.) We
     load thread_limit_current first into a local variable through a volatile
     pointer, in case it changes between testing against zero and setting up
     the incremented version for the CAS. This is partly paranoia, another
     thread cannot set it to zero whilst we're running this code, because the
     multi_constrain_to_single() conditions include only one thread being
     scheduled. If only one thread is scheduled, it is this one. We still
     don't want to re-read thread_limit_current whilst setting up for the
     increment, or we could get spurious results. */
  if ( currthreads > 0 && currthreads < thread_limit_max ) {
    thread_count_t morethreads = currthreads + 1 ;
    HqAtomicCAS(&thread_limit_current, currthreads, morethreads,
                extended_thread_pool) ;
    if ( extended_thread_pool ) {
      /* Release exactly one thread from dispatch if we extended the thread
         pool. Releasing threads waiting on a join or help condvar won't help
         us here, they fell asleep for a good reason. We use the raw pthreads
         call, because the task mutex may not be locked and
         multi_condvar_signal() would assert. If we missing the signal
         because it wasn't locked, we'll just be a little more inefficient
         until another wakeup is signalled or the thread extension
         expires. */
      if ( find_first_thread(THREAD_SIGNALLED) == NULL ) {
        /* Go directly to THREAD_RUNNING, because we're not guaranteed to
           hold the task mutex. The thread may be already waking up, and
           have just started writing THREAD_RUNNING into the state. This
           can't prevent another wakeup from happening simultaneously. */
        (void)wake_one_thread(THREAD_WAIT_DISPATCH, THREAD_RUNNING) ;
      }
    }
  }

  return extended_thread_pool ;
}


static void end_thread_extension(void)
{
  hq_atomic_counter_t after ;
  Bool swapped ;
  thread_count_t old_limit_active = thread_limit_active ;
  HqAtomicDecrement(&thread_limit_current, after) ;
  /* We have to assert this against an old copy of the active thread limit,
     because another thread may reset thread_limit_active the moment after
     the atomic decrement above, and we might not be protected by the task
     mutex. */
  HQASSERT(after >= old_limit_active,
           "Restored thread limit falls below active limit") ;

  HqAtomicCAS(&thread_limit_current, thread_limit_active, request_limit_active, swapped) ;
  if ( swapped ) {
    /* Any one of thread_limit_current, thread_limit_active, or
       request_limit_active may have changed since the previous
       statement. However, if either of the latter two have changed it is
       because another thread is: changing the requested limit; or
       resetting the limits to the requested value after the end of
       another thread pool extension. If thread_limit_active was modified
       after taking a copy of it, but before this statement, that means
       that another thread is resetting the limits, so we'll let that
       thread's update succeed. */
    HqAtomicCAS(&thread_limit_active, old_limit_active, request_limit_active, swapped) ;
  }
}

/****************************************************************************/

/** Trampoline from multi_condvar_wait that notes the current task is waiting
    on a condition variable, so it can be broken out of the wait if the task
    is cancelled. */
void multi_condvar_wait_task(multi_condvar_t *cond)
{
  corecontext_t *context = get_core_context() ;
  task_context_t *taskcontext ;
  task_t *current = NULL ;
  int res = 0;
  Bool extended_thread_pool = FALSE ;

  HQASSERT(context, "No core context") ;
  HQASSERT(cond, "No condition variable") ;

  if ( (taskcontext = context->taskcontext) != NULL &&
       (current = taskcontext->current_task) != NULL ) {
    /* Note that this thread is waiting on a condvar. This allows us to
       broadcast to the condvar if we cancel the task that's waiting on it.
       The condvar guard should include task_cancelling(), so it can see
       that it has been cancelled, and stop waiting. */
    HQASSERT(current->waiting_on == NULL,
             "Current task is already waiting on a condition") ;
    current->waiting_on = cond ;
    extended_thread_pool = begin_thread_extension() ;

    DECREMENT_THREADS_ACTIVE() ;

    /* We need to check if the current task is cancelled immediately before
       trying to wait, just in case another thread cancelled it since the
       guard check. There is a very small race period if a broadcast
       happens just after this check, but before the pthread_cond_wait()
       immediately below. The only solution to this race is to add another
       layer of locking (spinlock or mutex), or to retry the broadcast. */
    if ( current->state == TASK_CANCELLING )
      goto cleanup ;
    /* RACE CONDITION HERE, UNTIL pthread_cond_wait(). DO NOT ADD DELAY. */
  }

  res = pthread_cond_wait(&cond->pcond, &cond->mutex->pmutex);

 cleanup:
  if ( current != NULL ) {
    /* We can get spurious wakeups between the pthread_cond_wait() and clearing
       this field, but the guard loop around the wait should handle them. */
    current->waiting_on = NULL ;

    INCREMENT_THREADS_ACTIVE() ;

    if ( extended_thread_pool )
      end_thread_extension() ;
  }

  HQASSERT(res == 0, "condvar wait failed");
}

/****************************************************************************/
/* Task graph walking functions. These perform a depth-first search of the
   task graph, visiting a task and its predecessors, or a group and its
   children at most once. The callback function can determine if the
   search should terminate immediately, continue but return a found value
   later, continue searching into a task or subgroup, or prune the task
   or subgroup out of the search. */

/** Return values from task graph walking functions. The values \c
    TASK_WALK_STOP, \c TASK_WALK_CONTINUE, \c TASK_WALK_FOUND, and \c
    TASK_WALK_PENDING may be returned by callback functions, and by the
    internal walk_task() and walk_graph() graph functions. The top-level
    task_predecessors() and task_group_predecessors() functions translate
    these values into boolean found/not-found values. */
typedef enum {
  TASK_WALK_STOP = -1,        /**< Stop search and accept best result. */
  TASK_WALK_CONTINUE = 0,     /**< Task was not found, keep searching. */
  TASK_WALK_FOUND = 1,        /**< Task found, return immediately. */
  TASK_WALK_PENDING = 2       /**< Task found, but keep looking for better. */
} task_walk_result ;

/** \brief Bitmask of flags to control and inform task graph traversals. */
typedef enum {
  TASK_WALK_JOINED = 1,       /**< Traversed from task to joined group. */
  TASK_WALK_JOINER = 2,       /**< Traversed from group to joiner task. */
  TASK_WALK_SUBTASK = 4,      /**< Traversed from group to contained task. */
  TASK_WALK_SUBGROUP = 8,     /**< Traversed from group to subgroup. */
  TASK_WALK_TASKIN = 16,      /**< Traversed from task to containing group. */
  TASK_WALK_GROUPIN = 32,     /**< Traversed from subgroup to group. */
  TASK_WALK_DEPENDEE = 64,    /**< Traversed from task to dependee task. */
  TASK_WALK_DEPENDENT = 128,  /**< Traversed from task to dependent task. */
  TASK_WALK_RECURSIVE = 256   /**< Traversed recursive task activation. */
} task_walk_flags ;

/** Functions to filter and find groups and tasks during a task graph
    traversal. */
typedef struct task_walk_fns task_walk_fns ;

/** \brief A common data type for callbacks that find tasks. */
typedef struct task_find_t {
  task_t *task ;             /**< The task found. */
  task_runnability_t level ; /**< Runnability level of task found. */
  task_specialiser_fn *specialiser ; /**< Task specialiser to filter on. */
  void *specialiser_args ;   /**< Task specialiser args to filter on. */
} task_find_t ;

/** \brief The type of a callback function used to decide if a task is the
    one we're interested in.

    \param[in] task  The task being visited.
    \param[in] flags A bitmask of TASK_WALK_* flags indicating how the visited
                     task was found.
    \param[in] fns   Task and group selection functions to pass to any
                     subsidary task or group walk.
    \param[in] args  An opaque pointer, usually used for a structure for
                     storing the task found and examining any
                     previous task found for priority.

    \retval TASK_WALK_STOP  Stop searching for a task, and accept the best
            result found previously (if any).
    \retval TASK_WALK_CONTINUE  No task was found.
    \retval TASK_WALK_FOUND  A task was found, and should be returned
            immediately.
    \retval TASK_WALK_PENDING  A task was found, but the search should
            continue.
*/
typedef task_walk_result (task_walk_fn)(/*@notnull@*/ /*@in@*/ task_t *task,
                                        task_walk_flags flags,
                                        /*@notnull@*/ /*@in@*/
                                        const task_walk_fns *fns,
                                        /*@null@*/ void *data) ;

/** \brief The type of a callback function used to decide if a task group is
    the one we're interested in.

    \param[in] group The group being visited.
    \param[in] flags A bitmask of TASK_WALK_* flags indicating how the visited
                     task was found.
    \param[in] fns   Task and group selection functions to pass to any
                     subsidary task or group walk.
    \param[in] args  An opaque pointer, usually used for a structure for
                     storing the task found and examining any
                     previous task found for priority.

    \retval TASK_WALK_STOP  Stop searching for a task, and accept the best
            result found previously (if any).
    \retval TASK_WALK_CONTINUE  No task was found.
    \retval TASK_WALK_FOUND  A task was found, and should be returned
            immediately.
    \retval TASK_WALK_PENDING  A task was found, but the search should
            continue.
*/
typedef task_walk_result (task_group_walk_fn)(/*@notnull@*/ /*@in@*/
                                              task_group_t *group,
                                              task_walk_flags flags,
                                              /*@notnull@*/ /*@in@*/
                                              const task_walk_fns *fns,
                                              /*@null@*/ void *data) ;

struct task_walk_fns {
  /** Function called before a task group is examined during a graph
      traversal. */
  /*@null@*/
  task_group_walk_fn *group_fn ;
  /** Function called before task is examined during a graph traversal. */
  /*@null@*/
  task_walk_fn *task_fn ;

  OBJECT_NAME_MEMBER
} ;

#define TASK_WALK_FNS_NAME "Task walk functions"

/****************************************************************************/

typedef enum {
  HELP_TASK_READY,
  HELP_GROUP_CLOSE,
  HELP_GROUP_CANCEL
} helper_reason_t ;

static void task_helper_locked(corecontext_t *context, helper_reason_t reason) ;

static task_walk_result walk_group(task_group_t *group,
                                   task_walk_flags flags,
                                   /*@notnull@*/ /*@in@*/
                                   const task_walk_fns *fns,
                                   void *data) ;

/****************************************************************************/
/** \brief Test if a group is in the descendent tree of another group.

    \param[in] group     A task group to test.
    \param[in] ancestor  The task group that is tested for containment.

    \retval TRUE  If \a group is the same as \a ancestor, or is a descendent of
                  it.
    \retval FALSE If \a group is not the same as \a ancestor, and is not a
                  descendent of it. If either \a group or \a ancestor is NULL,
                  this function returns FALSE.
*/
static inline Bool group_same_or_subgroup(/*@null@*/ task_group_t *group,
                                          /*@null@*/ const task_group_t *ancestor)
{
  while ( group != NULL ) {
    if ( ancestor == group )
      return TRUE ;
    group = group->parent ;
  }

  return FALSE ;
}

/** Helper function to interpret return values from task walk callbacks and
    recursions.

    \param[in] walk        Result of the immediate result of a callback or
                           task walk recursion.
    \param[in,out] result  A pointer to the existing result of the task walk
                           recursion.
    \retval TRUE  The task walk is done, the existing result has been updated
                  with the value to return.
    \retval FALSE The task walk is not yet done.
*/
static inline Bool walk_done(task_walk_result walk,
                             task_walk_result *result)
{
  switch ( walk ) {
  case TASK_WALK_STOP:
    if ( *result != TASK_WALK_PENDING ) {
      *result = TASK_WALK_STOP ;
      return TRUE ;
    }
    /*@fallthrough@*/
  case TASK_WALK_FOUND:
    *result = TASK_WALK_FOUND ;
    return TRUE ;
  default:
    HQFAIL("Invalid result from task walk callback") ;
    /*@fallthrough@*/
  case TASK_WALK_CONTINUE:
    break ;
  case TASK_WALK_PENDING:
    *result = TASK_WALK_PENDING ;
    break ;
  }

  return FALSE ; /* Not done walking. */
}

/** \brief Internal function implementing recursion task graph walks, starting
    at a task.

    \param[in] task       The task to walk the graph from.
    \param[in] flags      Bitmask of flags determining how this task was
                          reached, and how to traverse the graph.
    \param[in] fns        Callback functions to visit task and graph nodes.
    \param     data       Opaque pointer supplied to the callback functions.

    \retval TASK_WALK_STOP  Stop searching for a task, and accept the best
            result found previously (if any).
    \retval TASK_WALK_CONTINUE  No task was found.
    \retval TASK_WALK_FOUND  A task was found, and should be returned
            immediately.
    \retval TASK_WALK_PENDING  A task was found, but the search should
            continue.
*/
static task_walk_result walk_task(task_t *task,
                                  task_walk_flags flags,
                                  /*@notnull@*/ /*@in@*/
                                  const task_walk_fns *fns,
                                  void *data)
{
  task_walk_result result = TASK_WALK_CONTINUE ;
  task_link_t *entry ;
  task_group_t *joins ;

#define return DO_NOT_return_goto_done_INSTEAD!
  INCREMENT_WALK_RECURSION() ;

  HQASSERT(task != &interpreter_task, "Shouldn't have found interpreter task") ;

  if ( task->state == TASK_DONE ) /* Trivially nothing to do. */
    goto done ;

  if ( task->mark == task_visitor_mark )
    goto done ;

  task->mark = task_visitor_mark ;

  /* Walk dependees of this task, they must end before this task starts. */
  for ( entry = DLL_GET_HEAD(&task->beforeme, task_link_t, pre_list) ;
        entry != NULL ;
        entry = DLL_GET_NEXT(entry, task_link_t, pre_list) ) {
    HQASSERT(entry->post == task, "Beforeme list link broken") ;
    /* Interpreter task cannot be a dependee: */
    HQASSERT(entry->pre->group != NULL, "Dependee task has no group") ;

    if ( walk_done(walk_task(entry->pre, flags|TASK_WALK_DEPENDEE, fns, data),
                   &result) )
      goto done ;
  }

  /* Walk groups joined by this task, they must end before this task ends. */
  for ( joins = DLL_GET_HEAD(&task->joins_list, task_group_t, joins_link)  ;
        joins != NULL ;
        joins = DLL_GET_NEXT(joins, task_group_t, joins_link) ) {
    if ( walk_done(walk_group(joins, flags|TASK_WALK_JOINED, fns, data),
                   &result) )
      goto done ;
  }

  /* Call task visit function, if supplied. */
  if ( fns->task_fn != NULL &&
       walk_done(fns->task_fn(task, flags, fns, data), &result) )
    goto done ;

 done:
  DECREMENT_WALK_RECURSION() ;
#undef return
  return result ;
}

/** \brief Internal function implementing recursion task graph walks, starting
    at a task group.

    \param[in] group      The task group to walk the graph from.
    \param[in] flags      Bitmask of flags determining how this task was
                          reached, and how to traverse the graph.
    \param[in] fns        Callback functions to visit task and graph nodes.
    \param     data       Opaque pointer supplied to the callback functions.

    \retval TASK_WALK_STOP  Stop searching for a task, and accept the best
            result found previously (if any).
    \retval TASK_WALK_CONTINUE  No task was found.
    \retval TASK_WALK_FOUND  A task was found, and should be returned
            immediately.
    \retval TASK_WALK_PENDING  A task was found, but the search should
            continue.
*/
static task_walk_result walk_group(task_group_t *group,
                                   task_walk_flags flags,
                                   /*@notnull@*/ /*@in@*/
                                   const task_walk_fns *fns,
                                   void *data)
{
  task_walk_result result = TASK_WALK_CONTINUE ;
  task_group_t *child ;
  task_t *task ;

#define return DO_NOT_return_goto_done_INSTEAD!
  INCREMENT_WALK_RECURSION() ;

  /* If the group is joined, there is trivially nothing to do. */
  if ( group->state == GROUP_STATE_JOINED )
    goto done ;

  if ( group->mark == task_visitor_mark )
    goto done ;

  group->mark = task_visitor_mark ;

  /* Subgroups should only be examined if looking for predecessors. Testing
     subgroups before subtasks is deliberate, it may result in shorter
     recursion chains, because sub-group structure is a tree rather than a
     graph, with a low nesting depth. The tasks in the subgroups will be
     marked during this low recursion, so if there are long dependency chains
     from subtasks through tasks in subgroups, they won't cause deep
     recursion. */
  for ( child = DLL_GET_HEAD(&group->children, task_group_t, peers) ;
        child != NULL ;
        child = DLL_GET_NEXT(child, task_group_t, peers) ) {
    if ( walk_done(walk_group(child, flags|TASK_WALK_SUBGROUP, fns, data),
                   &result) )
      goto done ;
  }

  /* All tasks in this group end before this group ends, and so do their
     dependees and joined groups. */
  for ( task = DLL_GET_HEAD(&group->tasks, task_t, group_link) ;
        task != NULL ;
        task = DLL_GET_NEXT(task, task_t, group_link) ) {
    if ( walk_done(walk_task(task, flags|TASK_WALK_SUBTASK, fns, data),
                   &result) )
      goto done ;
  }

  /* Call group visit function, if supplied. */
  if ( fns->group_fn != NULL &&
       walk_done(fns->group_fn(group, flags, fns, data), &result) )
    goto done ;

 done:
  DECREMENT_WALK_RECURSION() ;
#undef return
  return result ;
}

#ifdef ASSERT_BUILD
/** \brief Traverse the task graph starting at a task, visiting its
    dependencies, sub-groups, and any predecessor tasks and groups.

    \param[in] task     The task to traverse.
    \param[in] task_fn  A function to call for each task encountered.
    \param[in] group_fn A function to call for each task group encountered.
    \param data         An opaque pointer passed to the task and group
                        callback functions.

    \retval TRUE  If the callback functions found a node of interest, and
                  returned either TASK_WALK_FOUND or TASK_WALK_PENDING.
    \retval FALSE If the callback functions did not find a node of interest.

    The nodes are visited, and the callback functions are called in the order:

    1) The task node itself is visited.
    2) Any tasks this one is dependent on are visited recursively using the
       equivalent of \c task_predecessors().
    3) Any groups joined by the task are visited recursively using the
       equivalent of \c task_group_predecessors().

    This function uses the task visitor mark. */
static Bool task_predecessors(/*@notnull@*/ /*@in@*/ task_t *task,
                              /*@null@*/ task_walk_fn *task_fn,
                              /*@null@*/ task_group_walk_fn *group_fn,
                              /*@null@*/ void *data)
{
  task_walk_result result ;
  task_mark_t mark = ++task_visitor_mark ;
  task_walk_fns fns = { 0 } ;

  UNUSED_PARAM(task_mark_t, mark) ; /* Suppress release build warning */

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task graph traversal needs lock") ;

  START_WALK_RECURSION() ;
  fns.task_fn = task_fn ;
  fns.group_fn = group_fn ;
  NAME_OBJECT(&fns, TASK_WALK_FNS_NAME) ;

  PROBE(SW_TRACE_TASK_GRAPH_WALK, mark,
        result = walk_task(task, 0 /*flags*/, &fns, data)) ;

  UNNAME_OBJECT(&fns) ;
  HQASSERT(result == TASK_WALK_CONTINUE ||
           result == TASK_WALK_FOUND ||
           result == TASK_WALK_PENDING,
           "Invalid result from task walk callback") ;
  HQASSERT(mark == task_visitor_mark,
           "Task mark changed while traversing graph") ;
  END_WALK_RECURSION() ;

  /* Use ordering of values to return TRUE for FOUND or PENDING. */
  return result >= TASK_WALK_FOUND ;
}
#endif

/** \brief Traverse the task graph starting at a group, visiting its tasks,
    sub-groups, and any predecessor task and groups to its tasks.

    \param[in] group    The group to traverse.
    \param[in] task_fn  A function to call for each task encountered.
    \param[in] group_fn A function to call for each task group encountered.
    \param data         An opaque pointer passed to the task and group
                        callback functions.

    \retval TRUE  If the callback functions found a node of interest, and
                  returned either TASK_WALK_FOUND or TASK_WALK_PENDING.
    \retval FALSE If the callback functions did not find a node of interest.

    The nodes are visited, and the callback functions are called in the order:

    1) The group node itself is visited.
    2) The tasks in the group are visited recursively using the equivalent of
       \c task_predecessors().
    3) The sub-groups are visited recursively using the equivalent of
       \c task_group_predecessors().

    This function uses the task visitor mark. */
static Bool task_group_predecessors(/*@notnull@*/ /*@in@*/ task_group_t *group,
                                    /*@null@*/ task_walk_fn *task_fn,
                                    /*@null@*/ task_group_walk_fn *group_fn,
                                    /*@null@*/ void *data)
{
  task_walk_result result ;
  task_mark_t mark = ++task_visitor_mark ;
  task_walk_fns fns = { 0 } ;

  UNUSED_PARAM(task_mark_t, mark) ; /* Suppress release build warning */

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task graph traversal needs lock") ;

  START_WALK_RECURSION() ;
  fns.task_fn = task_fn ;
  fns.group_fn = group_fn ;
  NAME_OBJECT(&fns, TASK_WALK_FNS_NAME) ;

  PROBE(SW_TRACE_TASK_GRAPH_WALK, mark,
        result = walk_group(group, 0 /*flags*/, &fns, data)) ;

  UNNAME_OBJECT(&fns) ;
  HQASSERT(result == TASK_WALK_STOP || result == TASK_WALK_CONTINUE ||
           result == TASK_WALK_FOUND || result == TASK_WALK_PENDING,
           "Invalid result from task group walk callback") ;
  HQASSERT(mark == task_visitor_mark,
           "Task mark changed while traversing graph") ;
  END_WALK_RECURSION() ;

  /* Use ordering of values to return TRUE for FOUND or PENDING. */
  return result >= TASK_WALK_FOUND ;
}

/****************************************************************************/

/** \brief Get a reference to the current task, without changing the reference
    count; or return NULL if no current task.

    This is like \c task_current_uncounted(), but can be called even when
    there's no current task.
 */
static inline task_t *task_current_uncounted_unchecked(void)
{
  /* No lock needed for TLS variables, since no other thread can access them.
     If there is a current task running, then there must still be a reference
     to it from the running list. That reference won't get removed until this
     thread exits from the task worker function that called task_current(),
     so we can rely on this pointer staying valid until we've had a chance to
     increment the reference count. */
  corecontext_t *context = get_core_context();
  task_context_t *taskcontext;

  HQASSERT(context, "No core context for thread") ;
  taskcontext = context->taskcontext ;
  VERIFY_OBJECT(taskcontext, TASK_CONTEXT_NAME) ;
  return taskcontext->current_task;
}


/** \brief Get a reference to the current task, without changing the reference
    count. */
static inline task_t *task_current_uncounted(void)
{
  task_t *current = task_current_uncounted_unchecked();

  VERIFY_OBJECT(current, TASK_NAME) ;
  return current ;
}

/****************************************************************************/

/** \brief Dispose of a task group structure whose reference count has
    reached zero.

    We require the mutex to be locked because SAC allocation is not
    synchronised. */
static inline void group_release_internal(task_group_t *group)
{
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task not locked for release") ;

  HQASSERT(group->refcount == 0,
           "Destroying group that is still referenced") ;
  HQASSERT(group->state == GROUP_STATE_JOINED,
           "Destroying group that is not joined") ;
  HQASSERT(group->join == NULL, "Destroying group that is not joined") ;
  HQASSERT(group->required == NULL, "Destroying group with resource requirements") ;
  HQASSERT(group->provisions != PROVISION_OK,
           "Destroying group with resource assignments") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&group->tasks) && group->n_tasks == 0,
           "Destroying group that still has tasks") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&group->children),
           "Destroying group that has children") ;
  HQASSERT(!DLL_IN_LIST(group, joins_link),
           "Group should have been removed from join list") ;
  HQASSERT(!DLL_IN_LIST(group, order_link),
           "Group should have been removed from schedule list") ;
  DLL_REMOVE(group, peers) ; /* Remove from group list */
  HQASSERT(group->parent == &orphaned_task_group,
           "Group is still child of real parent") ;
  UNNAME_OBJECT(group) ;
  mm_sac_free(mm_pool_task, group, sizeof(*group)) ;
  --groups_complete ;
  probe_other(SW_TRACE_TASK_GROUPS_COMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)groups_complete) ;
}

/** \brief Dispose of a task group structure when the reference count reaches
    zero. */
static inline void group_release_locked(task_group_t **groupptr)
{
  task_group_t *group ;
  hq_atomic_counter_t after ;

  HQASSERT(groupptr, "Nowhere to find task group pointer") ;
  group = *groupptr ;
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;
  *groupptr = NULL ;

  HqAtomicDecrement(&group->refcount, after) ;
  HQASSERT(after >= 0, "Task group already released") ;
  if ( after == 0 ) {
    group_release_internal(group) ;
  }
}

void task_group_release(task_group_t **groupptr)
{
  task_group_t *group ;
  hq_atomic_counter_t after ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(groupptr, "Nowhere to find task group pointer") ;
  group = *groupptr ;
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;
  *groupptr = NULL ;

  HqAtomicDecrement(&group->refcount, after) ;
  HQASSERT(after >= 0, "Task group already released") ;
  if ( after == 0 ) {
    multi_mutex_lock(&task_mutex) ;
    group_release_internal(group) ;
    multi_mutex_unlock(&task_mutex) ;
  }
}

/****************************************************************************/

/** \brief Dispose of a task structure whose reference count has reached
    zero.

    \param[in] task  The task to be deallocated.

    We require the mutex to be locked because SAC allocation is not
    synchronised. */
static inline void task_release_internal(task_t *task)
{
  VERIFY_OBJECT(task, TASK_NAME) ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task not locked for release") ;

  HQASSERT(task->refcount == 0,
           "Destroying task that has references finalised") ;
  HQASSERT(task->state == TASK_DONE,
           "Destroying task that hasn't been finalised") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&task->joins_list),
           "Task didn't join group before being destroyed") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&task->beforeme),
           "Task is being destroyed with dependees") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&task->afterme),
           "Task is being destroyed with dependents") ;
  HQASSERT(!DLL_IN_LIST(task, order_link),
           "Task should have been removed from schedule list") ;
  DLL_REMOVE(task, group_link) ; /* Remove from group list */
  HQASSERT(task->group == &orphaned_task_group,
           "Task is still attached to a real group") ;
  UNNAME_OBJECT(task) ;
  mm_sac_free(mm_pool_task, task, sizeof(*task)) ;
  --tasks_complete ;
  probe_other(SW_TRACE_TASKS_COMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)tasks_complete) ;

  /* If the number of tasks is falling below a helper's completion threshold,
     wake them up so they can get on with more work. Set the state to
     THREAD_RUNNING so we don't prevent other threads from dispatching. */
  if ( tasks_incomplete == helper_end_threshold )
    wake_all_threads(THREAD_WAIT_HELP, THREAD_RUNNING) ;
}

/** \brief Dispose of a task structure when the reference count reaches
    zero.

    \param[in] taskptr  A reference to the task pointer to be released.
 */
static inline void task_release_locked(task_t **taskptr)
{
  task_t *task ;
  hq_atomic_counter_t after ;

  HQASSERT(taskptr, "Nowhere to find task pointer") ;
  task = *taskptr ;
  VERIFY_OBJECT(task, TASK_NAME) ;
  *taskptr = NULL ;

  HqAtomicDecrement(&task->refcount, after) ;
  HQASSERT(after >= 0, "Task already released") ;
  if ( after == 0 ) {
    task_release_internal(task) ;
  }
}

void task_release(task_t **taskptr)
{
  task_t *task ;
  hq_atomic_counter_t after ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(taskptr, "Nowhere to find task pointer") ;
  task = *taskptr ;
  VERIFY_OBJECT(task, TASK_NAME) ;
  *taskptr = NULL ;

  HqAtomicDecrement(&task->refcount, after) ;
  HQASSERT(after >= 0, "Task already released") ;
  if ( after == 0 ) {
    multi_mutex_lock(&task_mutex) ;
    task_release_internal(task) ;
    multi_mutex_unlock(&task_mutex) ;
  }
}

/****************************************************************************/

#ifdef ASSERT_BUILD
/* Assertion functions to determine if a task must complete before another
   task runs. These are used to detect cycles in the dependency graph. */
static task_walk_result task_before(task_t *task,
                                    task_walk_flags flags,
                                    /*@notnull@*/ /*@in@*/
                                    const task_walk_fns *fns,
                                    void *data)
{
  task_t *pre = data ;

  if ( task == pre )
    return TASK_WALK_FOUND ;

  /* If task's recursive field is set, there is a recursive call from task to
     it, and the recursive task must complete before task. (This test should
     be redundant, since the recursive task is only picked from the joins
     list. It depends on the exact timing of when the recursive activation is
     noted.) */
  if ( task->recursive != NULL ) {
    if ( task->recursive == pre )
      return TASK_WALK_FOUND ;

    switch ( walk_task(task->recursive, flags|TASK_WALK_RECURSIVE, fns, pre) ) {
    case TASK_WALK_FOUND:
      return TASK_WALK_FOUND ;
    default:
      HQFAIL("Invalid result from task walk callback") ;
      /*@fallthrough@*/
    case TASK_WALK_CONTINUE:
      return TASK_WALK_CONTINUE ;
    }
  }

  return TASK_WALK_CONTINUE ;
}

static task_walk_result task_group_before(task_group_t *group,
                                          task_walk_flags flags,
                                          /*@notnull@*/ /*@in@*/
                                          const task_walk_fns *fns,
                                          void *data)
{
  task_t *pre = data ;

  UNUSED_PARAM(task_walk_flags, flags) ;
  UNUSED_PARAM(const task_walk_fns *, fns) ;
  VERIFY_OBJECT(pre, TASK_NAME) ;

  /* If the group is in the task's parent hierarchy, then the task must
     complete before the group joins. There are other conditions too, but
     this one is trivial to test here. */
  if ( group_same_or_subgroup(pre->group, group) )
    return TASK_WALK_FOUND ;

  return TASK_WALK_CONTINUE ;
}
#endif

/** \brief Get a link structure that can be used for dependency/waiter
    tracking, and link it into a list. */
static Bool task_link(task_t *pre, dll_list_t *pre_list,
                      task_t *post, dll_list_t *post_list)
{
  task_link_t *entry ;
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(pre, TASK_NAME) ;
  VERIFY_OBJECT(post, TASK_NAME) ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task mutex held") ;
  HQASSERT(pre_list == &post->beforeme,
           "Post task should depend, wait, or join pre task") ;
  HQASSERT(post_list == &pre->afterme,
           "Pre task should be depended on, waited on, or joined by post task") ;
  /* Walk the predecessor set for pre, determining if post is in it. It
     shouldn't be. */
  HQASSERT(!task_predecessors(pre, &task_before, &task_group_before, post),
           "Task graph cycle detected") ;

  if ( (entry = mm_sac_alloc(mm_pool_task, sizeof(*entry), MM_ALLOC_CLASS_TASK_LINK)) == NULL )
    return error_handler(VMERROR) ;

  entry->pre = pre ;
  HqAtomicIncrement(&pre->refcount, before) ;
  DLL_RESET_LINK(entry, pre_list) ;
  DLL_ADD_TAIL(pre_list, entry, pre_list) ;

  entry->post = post ;
  HqAtomicIncrement(&post->refcount, before) ;
  DLL_RESET_LINK(entry, post_list) ;
  DLL_ADD_TAIL(post_list, entry, post_list) ;

  need_recompute_schedule = TRUE ;

#if defined(METRICS_BUILD)
  ++links_total_allocated ;
  ++links_current_allocated ;
  if ( links_current_allocated > links_peak_allocated )
    links_peak_allocated = links_current_allocated ;
#endif

  return TRUE ;
}

/** \brief Unlink a task link entry from a dependent/waiter/constructer list. */
static void task_unlink_locked(task_link_t *entry)
{
  HQASSERT(multi_mutex_is_locked(&task_mutex),
           "Must have task locked to modify refcount") ;
  DLL_REMOVE(entry, pre_list) ;
  task_release_locked(&entry->post) ;
  DLL_REMOVE(entry, post_list) ;
  task_release_locked(&entry->pre) ;
  mm_sac_free(mm_pool_task, entry, sizeof(*entry)) ;
#if defined(METRICS_BUILD)
  --links_current_allocated ;
#endif
}

/****************************************************************************/
/** \brief Helper function to determine if tasks in a group might be
    runnable.

    This function is used when making tasks ready, to check if the runnable
    and helpable generation counters should be changed. It doesn't need to be
    totally accurate with respect to provisionability. */
static void group_runnability(task_group_t *group, task_runnability_t *level)
{
  task_group_t *child ;

 tailcall:
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( group->state == GROUP_STATE_JOINED )
    return ;

  /* Tasks left to do? */
  if ( group->n_tasks > 0 ) {
    *level = RL_JOINS_NOT_EMPTY ;
    return ;
  }

  /* Non-empty subgroups? */
  child = DLL_GET_HEAD(&group->children, task_group_t, peers) ;
  if ( child != NULL ) {
    switch ( group->state ) {
    case GROUP_STATE_CLOSED:
    case GROUP_STATE_CANCELLED:
      if ( *level < RL_JOINS_IS_EMPTY )
        *level = RL_JOINS_IS_EMPTY ;
      break ;
    default:
      HQFAIL("Unexpected task group state") ;
      /*@fallthrough@*/
    case GROUP_STATE_CONSTRUCTING:
    case GROUP_STATE_ACTIVE:
      if ( *level < RL_JOINS_MAYBE_EMPTY )
        *level = RL_JOINS_MAYBE_EMPTY ;
      break ;
    }

    do {
      task_group_t *next = DLL_GET_NEXT(child, task_group_t, peers) ;

      /* Tail-call elimination is used to reduce the number of stack frames. */
      if ( next == NULL ) {
        group = child ;
        goto tailcall ;
      }

      /* Max level of children. */
      group_runnability(child, level) ;
      if ( *level == RL_JOINS_NOT_EMPTY )
        break ;

      child = next ;
    } while ( child != NULL ) ;
  }
}

/** \brief Helper function to set the runnability of a task. */
static void task_runnability(task_t *task)
{
  task_group_t *joins ;
  task_runnability_t level = RL_JOINS_NOTHING ;

  switch ( task->state ) {
  case TASK_READY:
    if ( NEEDS_PROVISION(task->group) ) {
      level = RL_READY_UNPROVISIONED ;
      break ;
    }
    /*@fallthrough@*/
  case TASK_CANCELLED:
    /* If this task joins a group, and the group isn't empty and closed, then
       it's too complex for helper recursion. */
    joins = DLL_GET_HEAD(&task->joins_list, task_group_t, joins_link) ;
    if ( joins != NULL ) {
      level = RL_JOINS_IS_EMPTY ;
      do {
        group_runnability(joins, &level) ;
        if ( level == RL_JOINS_NOT_EMPTY )
          break ;
        joins = DLL_GET_NEXT(joins, task_group_t, joins_link) ;
      } while ( joins != NULL ) ;
    }
    break ;
  case TASK_RUNNING:
  case TASK_CANCELLING:
  case TASK_FINALISING:
    level = RL_RUNNING ;
    break ;
  case TASK_CONSTRUCTING:
  case TASK_DEPENDING:
    level = RL_NOTREADY ;
    break ;
  default:
    HQFAIL("Invalid task state for runnability") ;
    break ;
  }

  if ( level != task->runnability ) {
    unsigned int old_runnable_helpable = tasks_runnable_helpable ;
    unsigned int old_runnable_nothelpable = tasks_runnable_nothelpable ;
    unsigned int old_ready_unprovisioned = tasks_ready_unprovisioned ;

    switch ( task->runnability ) {
    case RL_JOINS_IS_EMPTY:
    case RL_JOINS_NOTHING:
      --tasks_runnable_helpable ;
      break ;
    case RL_JOINS_MAYBE_EMPTY:
    case RL_JOINS_NOT_EMPTY:
      --tasks_runnable_nothelpable ;
      break ;
    case RL_READY_UNPROVISIONED:
      --tasks_ready_unprovisioned ;
      break ;
    default: /* irrelevant */
      break ;
    }

    switch ( level ) {
    case RL_JOINS_IS_EMPTY:
    case RL_JOINS_NOTHING:
      ++tasks_runnable_helpable ;
      break ;
    case RL_JOINS_MAYBE_EMPTY:
    case RL_JOINS_NOT_EMPTY:
      ++tasks_runnable_nothelpable ;
      break ;
    case RL_READY_UNPROVISIONED:
      ++tasks_ready_unprovisioned ;
      break ;
    default: /* irrelevant */
      break ;
    }

    task->runnability = level ;

    if ( tasks_ready_unprovisioned > old_ready_unprovisioned ) {
      ++task_unprovisioned_generation ;
    }
    if ( tasks_runnable_helpable > old_runnable_helpable ) {
      ++task_helpable_generation ;
    }
    if ( tasks_runnable_nothelpable > old_runnable_nothelpable ) {
      ++task_nothelpable_generation ;
    }

#ifdef PROBE_BUILD
    if ( tasks_runnable_helpable + tasks_runnable_nothelpable !=
         old_runnable_helpable + old_runnable_nothelpable )
      probe_other(SW_TRACE_TASKS_RUNNABLE, SW_TRACETYPE_AMOUNT,
                  tasks_runnable_helpable + tasks_runnable_nothelpable) ;
    if ( tasks_ready_unprovisioned != old_ready_unprovisioned )
      probe_other(SW_TRACE_TASKS_UNPROVISIONED, SW_TRACETYPE_AMOUNT,
                  tasks_ready_unprovisioned) ;
#endif
  }
}

/** \brief Dispense schedulable tasks to whichever threads will take them.

    \param[in] current If called by a thread waiting in a join, this is the
                       task context of that thread. When searching for joining
                       threads to wake, we start after this thread. Otherwise
                       it is NULL, indicating we should start at the head of
                       the thread list. This allows all join threads to be
                       iterated over before any dispatch threads are woken.
    \param[in] ignore  A task that has been selected but not yet activated by
                       the current thread. This task should be ignored in
                       deciding whether to wake up another thread.
    \param adjust_scheduled  Amount to adjust threads_scheduled by, to take
                       into account activation and de-activation of current
                       tasks.
*/
static void wake_next_thread(/*@null@*/ task_context_t *current,
                             const task_t *ignore,
                             thread_count_t adjust_scheduled)
{
  enum {
    WAKE_FAIL_THREADS,   /**< Too many threads scheduled already */
    WAKE_FAIL_TASKS,     /**< No tasks to do */
    WAKE_FAIL_SIGNALLED, /**< Thread already signalled. */
    WAKE_FAIL_NOTHING    /**< Nothing to wake up. */
  } ;

#if 0
  /* Can't assert this because of the call from
     multi_unconstrain_from_single(), which may not have the task mutex
     locked. */
  HQASSERT(multi_mutex_is_locked(&task_mutex),
           "Need task mutex locked to access lists") ;
#endif

  /* If all threads are currently scheduled, we shouldn't wake up anything. */
  adjust_scheduled += threads_scheduled ;
  if ( adjust_scheduled >= thread_limit_current ||
       adjust_scheduled >= thread_limit_max ) {
    probe_other(SW_TRACE_THREAD_WAKE_FAIL, SW_TRACETYPE_MARK,
                WAKE_FAIL_THREADS) ;
    return ;
  }

  /* If there are no tasks that are or can be made runnable, don't wake up
     anything. */
  if ( tasks_runnable_helpable + tasks_runnable_nothelpable <=
       (ignore != NULL && ignore->runnability < RL_DISPATCHABLE ? 1u : 0u) &&
       tasks_ready_unprovisioned <=
       (ignore != NULL && ignore->runnability == RL_READY_UNPROVISIONED ? 1u : 0u) ) {
    probe_other(SW_TRACE_THREAD_WAKE_FAIL, SW_TRACETYPE_MARK, WAKE_FAIL_TASKS) ;
    return ;
  }

  /* If we've already signalled a thread to wake up, don't wake any more
     threads. The thread that was woken will take responsibility for waking
     up another if necessary. */
  if ( find_first_thread(THREAD_SIGNALLED) != NULL ) {
    probe_other(SW_TRACE_THREAD_WAKE_FAIL, SW_TRACETYPE_MARK,
                WAKE_FAIL_SIGNALLED) ;
    return ;
  }

  /* If there are any helpable tasks and there is a waiting helper, signal
     the helper. */
  if ( tasks_runnable_helpable >
       (ignore != NULL && ignore->runnability < RL_HELPABLE ? 1u : 0u) &&
       wake_one_thread(THREAD_WAIT_HELP, THREAD_SIGNALLED) )
    return ;

  /* Try all of the waiting join threads in turn, starting at the current one
     (head of the list if NULL). */
  if ( current == NULL )
    current = DLL_GET_HEAD(&thread_list, task_context_t, thread_list) ;
  else
    current = DLL_GET_NEXT(current, task_context_t, thread_list) ;

  while ( (current = find_next_thread(current, THREAD_WAIT_JOIN)) != NULL ) {
    /* We've found another join thread. Wake it up and see if it can handle
       a task. */
    if ( thread_state_change(current, THREAD_WAIT_JOIN, THREAD_SIGNALLED) )
      return ;
  }

  /* No more join threads. If there's a dispatch thread available, wake it
     up. */
  if ( wake_one_thread(THREAD_WAIT_DISPATCH, THREAD_SIGNALLED) )
    return ;

  /* Nothing we can wake up. */
  probe_other(SW_TRACE_THREAD_WAKE_FAIL, SW_TRACETYPE_MARK,
              WAKE_FAIL_NOTHING) ;
}

/****************************************************************************/

/** \brief Return the join task for a group.

    \param[in] group  The group to find the join tasks.

    \return The task joining this group. There will always be a task joining
    a group.
*/
/*@notnull@*/
static inline task_t *group_get_joiner(task_group_t *group)
{
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  while ( group->join == NULL ) {
    group = group->parent ;
    VERIFY_OBJECT(group, TASK_GROUP_NAME) ;
  }

  return group->join ;
}

Bool task_group_create(task_group_t **newgroup,
                       task_grouptype_t type,
                       task_group_t *parent,
                       resource_requirement_t *required)
{
  task_group_t *group = NULL ;
  task_t *current ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

#define return DO_NOT_return_FALL_OUT_INSTEAD!
  multi_mutex_lock(&task_mutex) ;
  current = task_current_uncounted() ;
  /** \todo ajcd 2012-06-29: Do we need to do this? It simplifies the
      lifecycle of tasks, because we don't need to check for the groups that
      a requirement uses when it becomes ready. */
  HQASSERT(required == NULL || resource_requirement_is_ready(required),
           "Creating group with unready requirements") ;
  if ( rip_is_quitting() ) {
    (void)error_handler(INTERRUPT) ;
  } else if ( current->state != TASK_RUNNING ) {
    HQASSERT(current->group->error.old_error != 0,
             "Current group joined or cancelled, but not an error") ;
    (void)error_handler(current->group->error.old_error) ;
  } else if ( parent == NULL ) {
    (void)error_handler(UNDEFINED) ;
  } else if ( parent->state == GROUP_STATE_JOINED ||
              parent->state == GROUP_STATE_CANCELLED ) {
    HQASSERT(parent->error.old_error != 0,
             "Parent group joined or cancelled, but not an error") ;
    (void)error_handler(parent->error.old_error) ;
  } else if ( (group = mm_sac_alloc(mm_pool_task, sizeof(*group), MM_ALLOC_CLASS_TASK_GROUP)) == NULL ) {
    (void)error_handler(VMERROR) ;
  } else {
    error_context_t error_init = ERROR_CONTEXT_INIT ;

    HQASSERT(parent->state == GROUP_STATE_ACTIVE ||
             parent->state == GROUP_STATE_CONSTRUCTING ||
             (parent->state == GROUP_STATE_CLOSED &&
              group_same_or_subgroup(current->group, parent)),
             "Can only create group inside an active parent group") ;
    group->state = GROUP_STATE_CONSTRUCTING ;
    /* One for returned ref, one for parent->child link, one for join task
       link. */
    group->refcount = 3 ;
    group->parent = task_group_acquire(parent) ;
    DLL_RESET_LIST(&group->children) ;
    DLL_RESET_LINK(group, peers) ;
    DLL_RESET_LIST(&group->tasks) ;
    DLL_RESET_LINK(group, joins_link) ;
    DLL_RESET_LINK(group, order_link) ;
    group->join = task_acquire(current) ;
    if ( required != NULL ) {
      HQASSERT(parent->required == required || parent->required == NULL,
               "Parent group has different resource requirements") ;
      group->required = resource_requirement_acquire(required) ;
      group->reqnode = requirement_node_find(group->required, type) ;
    } else if ( parent->required != NULL ) {
      group->required = resource_requirement_acquire(parent->required) ;
      group->reqnode = requirement_node_find(group->required, type) ;
    } else {
      group->required = NULL ;
      group->reqnode = NULL ;
    }
    group->n_tasks = 0 ;
    group->provisions = PROVISION_FAIL_NEVER ;
    group->generation = group_resource_generation - 1; /* Previous generation */
    group->type = type ;
    /* We set the result to TRUE until the group is joined, so we can use it
       in tests as a shorthand for "this group has definitely failed". */
    group->result = TRUE ;
    /* If the parent group is part of an active join, then this group must
       be too. */
    group->mark = task_visitor_mark ;
    group->error = error_init ;
    HqMemZero(&group->nfixed[0], sizeof(group->nfixed)) ;
#if defined(DEBUG_BUILD)
    group->being_joined = FALSE ;
#endif

    NAME_OBJECT(group, TASK_GROUP_NAME) ;

    ++groups_incomplete ;
    probe_other(SW_TRACE_TASK_GROUPS_INCOMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)groups_incomplete) ;
#if defined(METRICS_BUILD)
    ++groups_total_allocated ;
    if ( (int)(groups_incomplete + groups_complete) > groups_peak_allocated )
      groups_peak_allocated = (int)(groups_incomplete + groups_complete) ;
#endif

    /* Add to peers list. Don't refcount peer links because parent links
       are already counted, and these are sufficient to prevent early
       release. */
    /** \todo ajcd 2012-09-05: If parent is in current task's ancestor group
        list, add the new group after the ancestor that is a peer. This would
        make elaboration tasks put the new group in the most likely part of
        the group hierarchy, reducing scan recursion. */
    DLL_ADD_TAIL(&parent->children, group, peers) ;
    DLL_ADD_TAIL(&current->joins_list, group, joins_link) ;
    DLL_ADD_TAIL(&group_schedule, group, order_link) ;
    need_recompute_schedule = TRUE ;

    /* Adding a subgroup affects the runnability of the parent group's join
       task. The current task's runnability isn't affected because it's
       already running. */
    task_runnability(group_get_joiner(parent)) ;
  }

  *newgroup = group ;
  multi_mutex_unlock(&task_mutex) ;
#undef return

  return (group != NULL) ;
}

task_group_t *task_group_acquire(task_group_t *group)
{
  hq_atomic_counter_t before ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  HqAtomicIncrement(&group->refcount, before) ;
  HQASSERT(before > 0, "Task group was previously released") ;

  return group ;
}

task_group_t *task_group_root(void)
{
  hq_atomic_counter_t before ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(&incomplete_task_group, TASK_GROUP_NAME) ;

  HqAtomicIncrement(&incomplete_task_group.refcount, before) ;
  HQASSERT(before > 0, "Task group was previously released") ;

  return &incomplete_task_group ;
}

static void group_ready_locked(task_group_t *group)
{
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Provisioning when unlocked") ;

  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( group->state == GROUP_STATE_CONSTRUCTING ) {
    HQASSERT(group->provisions != PROVISION_OK,
             "Constructing group should not be provisioned") ;
    HQASSERT(group->parent == NULL ||
             group->parent->state != GROUP_STATE_CONSTRUCTING,
             "Cannot ready group with unready parent") ;

    /* Transition from constructing to active does not affect the runnability
       of the joiner. */
    group->state = GROUP_STATE_ACTIVE ;

    /* If we might now be able to provision the group, bump the resource
       generation to force a rescan of the task graph. */
    if ( tasks_ready_unprovisioned > 0 ) {
      /* Might this group be provisionable? */
      if ( group->required == NULL ||
           resource_requirement_is_ready(group->required) ) {
        /* Making this group ready can only affect dispatch and join, because
           task helpers won't join this group. */
        ++task_nothelpable_generation ;
        wake_next_thread(NULL, NULL, 0) ;
      }
    }
  }
}

void task_group_ready(task_group_t *group)
{
  HQASSERT(mm_pool_task, "Task pool not active") ;

  multi_mutex_lock(&task_mutex) ;
  group_ready_locked(group) ;
  multi_mutex_unlock(&task_mutex) ;
}

void task_group_close(task_group_t *group)
{
  corecontext_t *context = get_core_context() ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

  multi_mutex_lock(&task_mutex) ;

  /* Make the group provisionable too, if it hasn't been done already (this
     can only mean the group is empty). */
  group_ready_locked(group) ;

  if ( group->state == GROUP_STATE_ACTIVE ) {
    group->state = GROUP_STATE_CLOSED ;

    /* We may make tasks helpable by completing this group, if the joiner is
       ready, and the group is empty. It doesn't make any difference to
       dispatch or join. */
    task_runnability(group_get_joiner(group)) ;

#if !INTERPRETER_HELPS
    if ( thread_limit_max == 1 || thread_limit_current == 1 ||
         !context->is_interpreter )
#endif
      task_helper_locked(context, HELP_GROUP_CLOSE) ;
  }

  multi_mutex_unlock(&task_mutex) ;
}

/** \brief Return the closest ancestor group with a requirement attached.

    \param[in] group  The group to find the request ancestor for.

    \return The closest ancestor group with a requirement attached,
            or NULL if there is not one.
*/
/*@null@*/
static inline task_group_t *group_requirement_ancestor(task_group_t *group)
{
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;
  VERIFY_OBJECT(group->required, RESOURCE_REQUIREMENT_NAME) ;
  group = group->parent ;
  if ( group != NULL ) {
    VERIFY_OBJECT(group, TASK_GROUP_NAME) ;
    if ( group->required != NULL )
      return group ;
  }

  return NULL ;
}

/** \brief The internals of task group provisioning, where we actually
    try to assign resources to a group.

    \param[in] group  The group we're trying to provision.
    \param nextp A pointer to the next group in the provision iteration. We
                 remove provisioned groups from the group schedule list, so
                 if we are about to remove the next in the iteration order,
                 we need to update the iteration order to contain the next
                 non-provisioned group. */
static void try_group_provision(task_group_t *group, task_group_t **nextp)
{
  resource_requirement_t *req ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Provisioning when unlocked") ;
  HQASSERT(NEEDS_PROVISION(group), "Trying to provision a group twice") ;
  HQASSERT(group->state != GROUP_STATE_JOINED &&
           group->state != GROUP_STATE_CANCELLED,
           "Trying to provision a group which is joined or cancelled,") ;

  /* If we've failed to provision a predecessor group using the same
     requirements in this task walk, we should skip this one. This allows a
     task walk to provision groups from different requirement trees, even if
     they share the same pools, without stopping because one requirement
     failed. */
  req = group->required ;
  if ( req != NULL ) {
    VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;
    if ( req->mark == task_visitor_mark ) {
      group->provisions = PROVISION_FAIL_REQPREV ;
      return ;
    }
  }

#define return DO_NOT_return_FALL_THROUGH_INSTEAD!
  if ( group->state == GROUP_STATE_CONSTRUCTING ) {
    /* If the group is still constructing, then it can't be provisioned. */
    group->provisions = PROVISION_FAIL_CONSTR ;
  } else if ( group->reqnode == NULL ) {
    /* If there are no resource requirements, trivially succeed. */
    group->provisions = PROVISION_OK ;
  } else if ( group->generation == group_resource_generation ) {
    /* If we've tried already with this generation of resources, there's no
       point trying again. */
    group->provisions = PROVISION_FAIL_GENERATION ;
  } else if ( !resource_requirement_is_ready(req) ) {
    group->provisions = PROVISION_FAIL_REQREADY ;
  } else {
    requirement_node_t *node = group->reqnode ;
    group_provisions_t result = PROVISION_OK ;
    requirement_node_t *root ;
    resource_type_t restype ;
    unsigned int needs[N_RESOURCE_TYPES] = { 0 } ;

    VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
    HQASSERT(node->requirement == req, "Requirement node not in same tree") ;

    /* We need to lock the requirement expression, because group
       provisioning can be tried on any thread. It's possible that no
       groups have resources fixed from the pool and the pool minimum might
       be set to zero by another thread whilst this provisioning attempt is
       in progress. */
    requirement_node_lock(req, root) ;

    /* Quick test to determine if this requirement node is being used to
       provision the maximum number of groups it can support already. We're
       deliberately using thread_limit_active rather than
       thread_limit_current because we don't want to provision more groups
       when the thread pool is extended. */
    if ( node->ngroups >= requirement_smax(node, thread_limit_active) ) {
      result = PROVISION_FAIL_REQLIMIT ; /* Failure because of limit */
      goto node_done ;
    }

    /* Task group provisioning is done in two phases, to avoid thrashing
       the memory system, especially when in a low-memory condition:

       1) If the difference between the number of resources provided by
       the pool and the pool's maximum is not sufficient for the group's
       needs, this phase attempts to dynamically allocate the remaining
       resources, using a fairly low cost.
       2) If not enough resources were reserved or allocated, this phase
       undoes any reservations made.
    */

    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      needs[restype] = node->minimum[restype] ;
      if ( needs[restype] > 0 ) {
        resource_pool_t *pool = req->pools[restype] ;
        resource_lookup_t *lookup ;

        resource_lookup_lock(pool, lookup) ;
        HQASSERT(lookup != NULL, "No lookup table for resource pool") ;

        /* Can we possibly get enough, given how much is already promised? */
        if ( needs[restype] + pool->nprovided + pool->ndetached > pool->maximum ) {
          result = PROVISION_FAIL_POOL ; /* Failure because of limit */
          goto done ;
        }

        HQASSERT(pool->nresources >= pool->nprovided + pool->ndetached,
                 "Pool has fewer resources than already provided") ;
        if ( needs[restype] + pool->nprovided + pool->ndetached <= pool->nresources ) {
          /* We can get all we need from existing resources in the pool. */
          pool->nprovided += needs[restype] ;
          needs[restype] = 0 ;
        } else {
          /* Provide all of the existing resources, then start allocating
             new resources. We've already checked that this shouldn't
             exceed the pool maximum. */
          needs[restype] -= pool->nresources - pool->nprovided - pool->ndetached ;
          pool->nprovided = pool->nresources - pool->ndetached ;
          do {
            /* Dynamic allocation of needed resource to pool. We use a low
               cost level because we don't want to thrash memory to get
               this. We already have the minimum resources the pool needs,
               but we might use threads more efficiently if we get this
               resource. We directly assign the new resources to the group,
               they can be stolen or released by low memory handlers if the
               provisioning fails. */
            resource_entry_t *entry ;
            if ( (entry = resource_entry_create(pool, mm_cost_easy,
                                                group)) == NULL ) {
              result = PROVISION_FAIL_ALLOC ;
              goto done ;
            }
            resource_lookup_insert(lookup, entry) ;
            ++pool->nprovided ;
          } while ( --needs[restype] > 0 ) ;
        }

        HQASSERT(needs[restype] == 0, "Didn't provide all resources needed") ;

      done:
        verify_pool_entries(pool, lookup) ;
        resource_lookup_unlock(pool, lookup) ;

        if ( result != PROVISION_OK )
          break ; /* Out of provisioning loop */
      }
    }

    if ( result == PROVISION_OK ) {
      probe_begin(SW_TRACE_TASK_GROUP_RESOURCES, (intptr_t)group) ;
      ++groups_provisioned ;
      ++node->ngroups ;
    } else {
      /* Undo all of the provisioning done so far. We leave the resource
         entry allocations for low memory handlers or other group
         provisioning to recycle, it's likely we'll be trying again
         soon. */
      for (;; --restype ) {
        unsigned int count = node->minimum[restype] - needs[restype] ;

        if ( count != 0 ) { /* We've reserved or allocated resources. */
          resource_pool_t *pool = req->pools[restype] ;
          resource_lookup_t *lookup ;

          resource_lookup_lock(pool, lookup) ;
          HQASSERT(lookup != NULL, "No resource lookup") ;
          HQASSERT(pool->nprovided >= count,
                   "Underflow rolling back resource provision") ;
          pool->nprovided -= count ;
          resource_lookup_unlock(pool, lookup) ;
        }

        if ( restype == 0 )
          break ; /* Out of rollback */
      }
    }

  node_done:
    group->provisions = result ;
    requirement_node_unlock(req, root) ;

    /* Note resource generation we tried to provision group under. */
    group->generation = group_resource_generation ;
  }

  probe_other(SW_TRACE_TASK_GROUP_PROVISION, SW_TRACETYPE_MARK,
              (intptr_t)group->provisions) ;
#undef return

  if ( group->provisions == PROVISION_OK ) {
    task_t *task ;

    /* All of the tasks in this group that are ready are now runnable. */
    for ( task = DLL_GET_HEAD(&group->tasks, task_t, group_link) ;
          task != NULL ;
          task = DLL_GET_NEXT(task, task_t, group_link) ) {
      if ( task->state == TASK_READY )
        task_runnability(task) ;
    }

    /* Remove this group from the group provisioning schedule, updating the
       next iteration position if it was going to be this group. */
    if ( group == *nextp )
      *nextp = DLL_GET_NEXT(group, task_group_t, order_link) ;
    DLL_REMOVE(group, order_link) ;
  } else if ( req != NULL ) {
    /* Group was not provisioned, so indicate that no further provisioning
       will happen from this requirement in this task graph traversal. */
    req->mark = task_visitor_mark ;
  }
}

static void task_group_deprovision(task_group_t *group)
{
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Deprovisioning when unlocked") ;

  /* We can only assign resources to groups that have requirements, so only
     deprovision those groups. */
  if ( group->provisions == PROVISION_OK && group->reqnode != NULL ) {
    requirement_node_t *node, *root ;
    resource_requirement_t *req ;
    resource_type_t restype ;
    task_group_t *pgroup = group_requirement_ancestor(group) ;

    node = group->reqnode ;
    VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;

    req = node->requirement ;

    HQASSERT(resource_requirement_is_ready(req),
             "Resource requirement not ready, but group is provisioned") ;

    requirement_node_lock(req, root) ;

    HQASSERT(node->ngroups > 0, "No groups provisioned with this requirement") ;
    HQASSERT(groups_provisioned > 0, "Groups provisioned already zero") ;

    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      resource_pool_t *pool = req->pools[restype] ;
      if ( pool != NULL ) {
        resource_lookup_t *lookup ;
        unsigned int detached = 0, returned ;

        resource_lookup_lock(pool, lookup) ;
        /* We need the pool locked to access the nfixed table. */
        returned = max(node->minimum[restype], group->nfixed[restype]) ;
        /* No point in searching lookup table if this requirement didn't
           need anything in the first place. */
        if ( lookup != NULL && returned > 0 ) {
          unsigned int i ;

          for ( i = 0 ; i < lookup->nentries ; ++i ) {
            resource_entry_t *entry ;
            if ( (entry = lookup->entries[i]) != NULL &&
                 entry->owner == group ) {
              if ( entry->state == TASK_RESOURCE_DETACHED ) {
                ++detached ; /* Don't reuse detached resources. */
                entry->owner = pool ;
              } else {
                probe_other(SW_TRACE_RESOURCE_FREE, SW_TRACETYPE_MARK,
                            entry->resource) ;
                entry->state = TASK_RESOURCE_FREE ;
                entry->owner = pgroup ;
                if ( !pool->cache_unfixed )
                  entry->id = TASK_RESOURCE_ID_INVALID ;
              }
            }
          }

          HQASSERT(pool->nprovided >= returned,
                   "Returning more resources to pool than provisioned") ;
          pool->nprovided -= returned ;
          pool->ndetached += detached ;
        }
        resource_lookup_unlock(pool, lookup) ;
      }
    }

    --node->ngroups ;
    --groups_provisioned ;

    requirement_node_unlock(req, root) ;

    probe_end(SW_TRACE_TASK_GROUP_RESOURCES, (intptr_t)group) ;

    ++group_resource_generation ; /* Recovered resources or group limit */
    /** \todo ajcd 2012-10-21: Do we need wake_next_thread() here? */
    /* This one lets the interpreter go, not a new runnable task. Go straight
       to running so we don't block any other wakeups (there shouldn't be any,
       if we're stuck waiting on memory handlers). */
    /** \todo ajcd 2012-07-20: Could this be in wake_next_thread() too?
        If so, does it need to be protected by anything, or should we just
        rely on pthreads not to take long signalling if nothing is
        waiting? */
    wake_all_threads(THREAD_WAIT_MEMORY, THREAD_RUNNING) ;
  }

  /* This function might be called with a group that never got provisioned. */
  if ( NEEDS_PROVISION(group) )
    DLL_REMOVE(group, order_link) ;

  group->provisions = PROVISION_REMOVED ;
}

void task_group_resources_signal(void)
{
  multi_mutex_lock(&task_mutex);
  ++group_resource_generation ;
  wake_next_thread(NULL, NULL, -threads_waiting_for_resources) ;
  multi_mutex_unlock(&task_mutex);
}


Bool task_wait_for_memory(void)
{
  Bool retry = FALSE;

  HQASSERT(IS_INTERPRETER(), "Waiting for memory in non-interpreter task") ;
  HQASSERT(!rip_is_quitting(), "Waiting for memory while RIP is quitting") ;

  multi_mutex_lock(&task_mutex);
  if ( groups_provisioned ) {
    /* We want this to extend the thread pool, hence the generic wait. */
    interpreter_task_context.state = THREAD_WAIT_MEMORY ;
    PROBE(SW_TRACE_TASK_MEMORY_WAIT, -1,
          multi_condvar_wait(&interpreter_task_context.cond));
    interpreter_task_context.state = THREAD_RUNNING ;
    retry = TRUE;
  }
  multi_mutex_unlock(&task_mutex);
  return retry;
}

/****************************************************************************/

Bool task_create(task_t **newtask,
                 task_specialiser_fn *specialiser_fn,
                 void *specialiser_args,
                 task_fn *worker_fn, void *worker_args,
                 task_cleanup_fn *cleanup_fn,
                 task_group_t *group, int trace_id)
{
  task_t *task = NULL ;
  task_t *current ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

#define return DO_NOT_return_FALL_OUT_INSTEAD!
  /* SAC is not globally synchronised, so use the task mutex to avoid
     conflict. */
  multi_mutex_lock(&task_mutex) ;
  current = task_current_uncounted() ;
  if ( rip_is_quitting() ) {
    (void)error_handler(INTERRUPT) ;
  } else if ( current->state != TASK_RUNNING ) {
    HQASSERT(current->group->error.old_error != 0,
             "Current group joined or cancelled, but not an error") ;
    (void)error_handler(current->group->error.old_error) ;
  } else if ( group == NULL ) {
    (void)error_handler(UNDEFINED) ;
  } else if ( group->state == GROUP_STATE_CANCELLED ||
              group->state == GROUP_STATE_JOINED ) {
    HQASSERT(group->error.old_error != 0,
             "Task group joined or cancelled, but not an error") ;
    (void)error_handler(group->error.old_error) ;
  } else if ( (task = mm_sac_alloc(mm_pool_task, sizeof(*task), MM_ALLOC_CLASS_TASK)) == NULL ) {
    (void)error_handler(VMERROR) ;
  } else {
    HQASSERT(group->state == GROUP_STATE_CONSTRUCTING ||
             group->state == GROUP_STATE_ACTIVE ||
             (group->state == GROUP_STATE_CLOSED &&
              group_same_or_subgroup(current->group, group)),
             "Can only create task inside a constructing or active group") ;
    task->state = TASK_CONSTRUCTING ;
    task->runnability = RL_NOTREADY ;
    task->refcount = 2 ; /* One for returned ref, one for group list ref. */
    task->specialiser = specialiser_fn ? specialiser_fn : &task_specialise_done ;
    task->specialiser_args = specialiser_args ;
    task->worker = worker_fn ;
    task->cleaner = cleanup_fn ;
    task->args = worker_args ;
    task->group = task_group_acquire(group) ;
    DLL_RESET_LINK(task, group_link) ;
    DLL_RESET_LINK(task, order_link) ;
    DLL_RESET_LIST(&task->joins_list) ;
    DLL_RESET_LIST(&task->beforeme) ;
    DLL_RESET_LIST(&task->afterme) ;
    task->trace_id = trace_id ;
    task->waiting_on = NULL ;
    /* We set the result to TRUE until the finaliser runs, so we can use it
       in tests as a shorthand for "this task has definitely failed". */
    task->result = TRUE ;
    task->mark = task_visitor_mark ;
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
    task->recursive = NULL ;
    task->thread_index = NTHREADS_LIMIT ;
#endif

    NAME_OBJECT(task, TASK_NAME) ;

    ++tasks_incomplete ;
    probe_other(SW_TRACE_TASKS_INCOMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)tasks_incomplete) ;
#if defined(METRICS_BUILD)
    ++tasks_total_allocated ;
    if ( (int)(tasks_incomplete + tasks_complete) > tasks_peak_allocated )
      tasks_peak_allocated = (int)(tasks_incomplete + tasks_complete) ;
#endif

    /* Add to group's task list and the global schedule lists. */
    ++group->n_tasks ;
    DLL_ADD_TAIL(&group->tasks, task, group_link) ;
    DLL_ADD_TAIL(&task_schedule, task, order_link) ;
    need_recompute_schedule = TRUE ;

    HQASSERT(group_get_joiner(group) != NULL, "Group has no join task") ;
    /* A joiner state of TASK_DEPENDING is technically possible, but would be
       an unreliable construction, so we disallow it here. It would require
       that the constructor knows that the joiner won't start because some
       other dependency is not ready, and so the joiner has been made ready.
       It's disallowed because if it occurred by chance rather than design,
       it would mean that some other task apart from either the joiner
       itself, or the joiner's constructor is likely putting tasks in the
       group. This is very likely to produce timing problems in
       multi-threaded RIPs. The runnability of the joiner is not affected by
       adding the task to the group because the joiner is either running or
       constructing. */
    /** \todo ajcd 2011-07-14: Should the states include TASK_CANCELLING,
        TASK_CANCELLED, and TASK_FINALISING? A join task in a peer group
        could be CANCELLING or maybe even CANCELLED but just not have
        reached the join operation. It's pretty dubious whether such a
        construct, or having the task in TASK_FINALISING would be sane, it
        would mean that the task joining is not the task constructing the
        group, and the joiner somehow got cancelled before the group it's
        joining. */
    HQASSERT(group_get_joiner(group)->state == TASK_CONSTRUCTING ||
             group_get_joiner(group)->state == TASK_RUNNING,
             "Group joiner in unusual state") ;

    /* Adding the first task to a group may change the joiner's runnability
       from RL_JOINS_MAYBE_EMPTY to RL_JOINS_NOT_EMPTY. */
    if ( group->n_tasks == 1 )
      task_runnability(group_get_joiner(group)) ;
  }
  *newtask = task ;
  multi_mutex_unlock(&task_mutex) ;
#undef return

  return (task != NULL) ;
}

task_t *task_current(void)
{
  return task_acquire(task_current_uncounted()) ;
}

task_t *task_acquire(task_t *task)
{
  hq_atomic_counter_t before ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(task, TASK_NAME) ;

  HqAtomicIncrement(&task->refcount, before) ;
  HQASSERT(before > 0, "Task was previously released") ;

  return task ;
}

task_group_t *task_group_current(task_grouptype_t group_type)
{
  task_t *task ;
  task_group_t *group ;
  hq_atomic_counter_t before ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(group_type < TASK_GROUP_LIMIT, "Invalid task group type") ;

  task = task_current_uncounted() ;

  /* Note that there is no test condition. We do indeed require that we find
     a task group of the specified type. */
  for ( group = task->group ; ; group = group->parent ) {
    VERIFY_OBJECT(group, TASK_GROUP_NAME) ;
    if ( group->type == group_type )
      break ;
  }

  HqAtomicIncrement(&group->refcount, before) ;
  HQASSERT(before > 0, "Task group was previously released") ;
  return group ;
}

/** \brief Helper function for task dispatch loops, to put a discovered task
    into the right state.

    \param task  The task to be activated
 */
static void task_activate_locked(task_t *task)
{
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(task, TASK_NAME) ;

  HQASSERT(multi_mutex_is_locked(&task_mutex),
           "Activating task when unlocked") ;

#ifdef DEBUG_BUILD
  task->thread_index = CoreContext.thread_index ;
#endif

  if ( task->state == TASK_READY )
    task->state = TASK_RUNNING ;
  else if ( task->state == TASK_CANCELLED )
    task->state = TASK_FINALISING ;
  else
    HQFAIL("Activating task in invalid state") ;

  task_runnability(task) ;
  INCREMENT_TASKS_ACTIVE() ;

  /* Hold a reference to the ready task while we run it. */
  HqAtomicIncrement(&task->refcount, before) ;
  HQASSERT(before > 0, "Task was previously released") ;
}

/****************************************************************************/
/** \brief Task walk function to construct initial task schedule. */
static task_walk_result order_task(task_t *task, task_walk_flags flags,
                                   /*@notnull@*/ /*@in@*/
                                   const task_walk_fns *fns,
                                   void *data)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(task_walk_flags, flags) ;
  UNUSED_PARAM(const task_walk_fns *, fns) ;

  /* All tasks before this task have been visited. This task must already be
     on the schedule (all incomplete tasks are always on the list). Remove it
     from its current position, and add it at the end. Since we visit all
     tasks in the graph during the walk, this will end up re-constructing the
     entire schedule list in a topological order. */
  DLL_REMOVE(task, order_link) ;
  DLL_ADD_TAIL(&task_schedule, task, order_link) ;

  /* Indicate that we've added something to the list, but keep adding more. */
  return TASK_WALK_PENDING ;
}

/** \brief Task walk function to construct initial task group schedule. */
static task_walk_result order_group(task_group_t *group, task_walk_flags flags,
                                    /*@notnull@*/ /*@in@*/
                                    const task_walk_fns *fns,
                                    void *data)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(task_walk_flags, flags) ;
  UNUSED_PARAM(const task_walk_fns *, fns) ;

  if ( NEEDS_PROVISION(group) ) {
    /* There is a conflict between the order in which we want to build the
       group list for peer sub-groups and their parent group. Task groups
       tend to be visited in top-down order, so the task walk function will
       be called after all sub-groups. Within a task group, sub-groups are
       visited first to last, so the task walk function is visited before
       peers. If we add the group at the head of the list, parents will come
       before sub-groups, but peers will be in reverse order. If we add the
       group at the tail of the list, parents will come after sub-groups, but
       peers will be in normal order. Provision order deadlocks may arise if
       the derived graph of group order contains cycles, and is
       underspecified with dependencies. Since the second pass sort is stable
       with respect to this order, execution order usually follows
       construction order, and parent groups are recursively provisioned
       anyway, we are likely to run into fewer potential deadlocks with the
       latter method, so we add to the tail of the list.

       In any case, all unprovisioned incomplete groups are already on the
       schedule, so remove the group from its previous place in the list
       before adding it back in order. */
    DLL_REMOVE(group, order_link) ;
    DLL_ADD_TAIL(&group_schedule, group, order_link) ;
  } else {
    HQASSERT(!DLL_IN_LIST(group, order_link),
             "Provisioned, cancelled or de-provisioned group in schedule") ;
  }

  /* Indicate that we've added something to the list, but keep adding more. */
  return TASK_WALK_PENDING ;
}

/** \brief Try to provision a group, propagating any consequences of failure
    to dependent groups.

    \param group The group we want to provision.
    \param nextp A pointer to the next group in the provision iteration. We
                 remove provisioned groups from the group schedule list, so
                 if we are about to remove the next in the iteration order,
                 we need to update the iteration order to contain the next
                 non-provisioned group. */
static void provision_group(task_group_t *group, task_group_t **nextp)
{
  Bool ok = TRUE ;

  HQASSERT(NEEDS_PROVISION(group), "Trying to provision group that does not need it") ;

  /* We always recurse to the parent, even if this group cannot succeed
     because a predecessor failed, because we need to be sure that the
     parent's successors are marked correctly as failing or succeeding. */
  if ( group->parent != NULL && NEEDS_PROVISION(group->parent) ) {
    provision_group(group->parent, nextp) ;

    /* If we failed to provision the parent, we shouldn't try to provision
       the current group. */
    if ( NEEDS_PROVISION(group->parent) ) {
      group->provisions = PROVISION_FAIL_PREV ;
      ok = FALSE ;
    }
  }

  if ( ok ) {
    /* If we have already tried to provision this group during the same pass,
       don't try again. */
    if ( group->mark != task_visitor_mark )
      try_group_provision(group, nextp) ;

    ok = !NEEDS_PROVISION(group) ;
  }

  group->mark = task_visitor_mark ;

  if ( !ok ) {
    task_t *task ;

    /* Provisioning this group failed. Propagate failure to unprovisioned
       groups that are direct dependees of this group (tasks in this group
       come before tasks in their groups). Failure will propagate to each
       group in turn as we consider them, until we find a group that is not
       part of the same dependency set. */
    for ( task = DLL_GET_HEAD(&group->tasks, task_t, group_link) ;
          task != NULL ;
          task = DLL_GET_NEXT(task, task_t, group_link) ) {
      task_link_t *entry ;

      for ( entry = DLL_GET_HEAD(&task->afterme, task_link_t, post_list) ;
            entry != NULL ;
            entry = DLL_GET_NEXT(entry, task_link_t, post_list) ) {
        task_group_t *postgroup ;

        HQASSERT(entry->pre == task, "Afterme list link broken") ;
        VERIFY_OBJECT(entry->post, TASK_NAME) ;
        postgroup = entry->post->group ;
        VERIFY_OBJECT(postgroup, TASK_GROUP_NAME) ;

        if ( postgroup->mark != task_visitor_mark &&
             NEEDS_PROVISION(postgroup) ) {
          postgroup->provisions = PROVISION_FAIL_PREV ;
          postgroup->mark = task_visitor_mark ;
        }
      }
    }
  }
}

/** \brief Re-compute the task and group schedule lists. */
static void recompute_schedule(void)
{
  task_t *task ;
  task_group_t *group ;
  dll_list_t final_tasks, final_groups ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Recomputing schedule needs lock") ;

  /* Use a depth-first search to re-order the task schedule list, and to
     mark all tasks and groups with the same visitor mark. The visitor mark
     will form a base value for a second breadth-first pass that re-sorts
     the lists. The classic breadth-first scan is inapplicable because it
     is destructive, and also cannot handle the group to group precedence
     relationships we want. */
  if ( !task_group_predecessors(&incomplete_task_group, order_task, order_group, NULL) )
    HQFAIL("First pass task schedule graph walk failed") ;

  /* We've got *a* topological order, but it may not be *the* topologocal
     order we want. In particular, it doesn't take into account group
     provisioning order.

     The second (breadth-first) pass takes into account the group structure
     and global information about the tasks that wasn't available during
     the first pass. The second pass traverses the schedule list last to
     first, marking the predecessors of each task with a number lower than
     the last task, and then sorting the tasks in numerical order. (We use
     a number lower than the last task so we can re-use the mark field.)
     The group structure is taken into account both by altering the amount the
     predecessor task is decremented by, and also be re-numbering groups
     when we detect dependencies from tasks in a group to a different
     group.

     This re-sort retains the depth-first schedule order for mark value ties.

     Note: this algorithm will not detect or assert potential deadlocks
     that can happen if the derived graph of group dependencies is cyclic
     (determined by dependencies from tasks in a group to tasks in other
     groups, excluding dependencies from ancestor to child groups). We will
     try detect deadlocks in the task group join loop. */
  DLL_RESET_LIST(&final_tasks) ;
  DLL_RESET_LIST(&final_groups) ;

  while ( (task = DLL_GET_TAIL(&task_schedule, task_t, order_link)) != NULL ) {
    task_t *final ;
    task_link_t *entry ;

    /* Remove task from initial schedule, and insert into final schedule in
       mark order. */
    DLL_REMOVE(task, order_link) ;
    for ( final = DLL_GET_HEAD(&final_tasks, task_t, order_link) ;
          final != NULL ;
          final = DLL_GET_NEXT(final, task_t, order_link) ) {
      /* Comparing against zero ensures that the comparison works when the
         mark wraps around. */
      if ( task->mark - final->mark <= 0 ) {
        DLL_ADD_BEFORE(final, task, order_link) ;
        break ;
      }
    }
    if ( final == NULL ) /* Didn't find anything to insert before */
      DLL_ADD_TAIL(&final_tasks, task, order_link) ;

    group = task->group ;

    /* Mark predecessors with the minimum of the existing mark and the
       reduced mark. */
    for ( entry = DLL_GET_HEAD(&task->beforeme, task_link_t, pre_list) ;
          entry != NULL ;
          entry = DLL_GET_NEXT(entry, task_link_t, pre_list) ) {
      task_t *pre = entry->pre ;
      task_mark_t mark ;
      HQASSERT(entry->post == task, "Beforeme list link broken") ;

      /* If the predecessor's group is not a subgroup of this group,
         reduce the mark by the number of tasks in this group. Unless there
         are cycles in the derived group graph, this will ensure that the
         first task in the predecessor group must be marked with a lower
         number than any task in this group, and therefore the
         predecessor's group will be provisioned before this group. */
      if ( group_same_or_subgroup(pre->group, group) ) {
        mark = task->mark - 1 ;
      } else {
        mark = task->mark - group->n_tasks ;
        /* This is a cross-group link, or a link from a subgroup to a
           parent group. As well as marking the predecessor task, we want
           to mark the predecessor group lower than this group mark so it
           provisions before this group. */
        if ( pre->group->mark - group->mark >= 0 )
          pre->group->mark = group->mark - 1 ;
      }

      /* Comparing against zero ensures that the comparison works when the
         mark wraps around. */
      if ( mark - pre->mark < 0 )
        pre->mark = mark ;
    }
  }

  /* Re-sort group list into mark order. */
  while ( (group = DLL_GET_TAIL(&group_schedule, task_group_t, order_link)) != NULL ) {
    task_group_t *final ;

    /* Remove group from initial schedule, and insert into final schedule in
       mark order. */
    DLL_REMOVE(group, order_link) ;
    for ( final = DLL_GET_HEAD(&final_groups, task_group_t, order_link) ;
          final != NULL ;
          final = DLL_GET_NEXT(final, task_group_t, order_link) ) {
      /* Comparing against zero ensures that the comparison works when the
         mark wraps around. */
      if ( group->mark - final->mark <= 0 ) {
        DLL_ADD_BEFORE(final, group, order_link) ;
        break ;
      }
    }
    if ( final == NULL ) /* Didn't find anything to insert before */
      DLL_ADD_TAIL(&final_groups, group, order_link) ;
  }

  /* The original lists heave been cleared out, so we can just append the
     entire contents of the final lists to them. Copying doesn't work,
     because the DLL structure requires the head and tail pointers to be
     self-referential. */
  DLL_LIST_APPEND(&task_schedule, &final_tasks) ;
  DLL_LIST_APPEND(&group_schedule, &final_groups) ;
}

/** \brief Search the task schedule for a task that we know should exist.

    \param found A structure used to filter the details of the task being
                 considered, and the current best task and runnability level.
*/
static void find_scheduled_task(task_find_t *found)
{
  task_t *task ;

  HQASSERT(found != NULL &&
           found->specialiser == NULL &&
           found->specialiser_args == NULL,
           "Cannot filter on specialiser when searching for known tasks") ;

  /* If there is already a task on the schedule with the appropriate
     runnability, select it. */
  for ( task = DLL_GET_HEAD(&task_schedule, task_t, order_link) ;
        task != NULL ;
        task = DLL_GET_NEXT(task, task_t, order_link) ) {
    task_runnability_t level = task->runnability ;
    if ( level < found->level ) {
      found->task = task ;
      found->level = level ;
      if ( level < RL_NOW ||
           (level < RL_DISPATCHABLE && tasks_runnable_helpable == 0) )
        break ;
    }
  }
  HQASSERT(found->task != NULL, "Should have found scheduled task") ;
}

/** \brief The main routine used to find a task to run.

    \param found A structure used to filter the details of the task being
                 considered, and the current best task and runnability level.
    \param join  The group being joined. If non-NULL, this group is used
                 to filter the tasks that will be considered.

    \retval TRUE If a task matching the filter criteria was found. In this
                 case, \c found->task contains an uncounted reference to the
                 task and \c found->level contains the runnability level of the
                 task.
    \retval FALSE No matching task was found.
*/
static Bool find_runnable_task(/*@notnull@*/ /*@in@*/ task_find_t *found,
                               /*@null@*/ /*@in@*/ task_group_t *join)
{
  Bool result = FALSE, provision_failed = FALSE ;
  task_group_t *provision ;
  task_mark_t task_filter_mark ;
  task_t *task ;

  if ( need_recompute_schedule ) {
    PROBE(SW_TRACE_RECOMPUTE_SCHEDULE, (intptr_t)join, recompute_schedule()) ;
#ifdef DEBUG_BUILD
    /* Normally avoid recomputing the schedule. Allow debug to override that. */
    if ( (debug_tasks & DEBUG_TASKS_RESCHEDULE) == 0 )
#endif
      need_recompute_schedule = FALSE ;
#ifdef DEBUG_BUILD
    if ( debug_schedule_graph > 0 ) {
      debug_graph_tasks_locked() ;
      --debug_schedule_graph ;
    }
#endif
  }

  if ( join == NULL ) {
    /* Filter includes all groups and tasks. */
    task_filter_mark = task_visitor_mark - MAXINT ;
  } else {
    /* Mark tasks and groups that participate in this particular join. */
    (void)task_group_predecessors(join, NULL, NULL, NULL) ;
    task_filter_mark = task_visitor_mark ;
  }

  /** \todo ajcd 2012-10-03: Now we've got a task order list for this
      join/help/dispatch, we could pro-actively distribute tasks to available
      threads rather than wait for them to join themselves. Would this result
      in lower overheads? */

  /* Walk the task list, looking for a suitable task. We'll try to provision
     groups for tasks as we go, but will use the task provision mark to avoid
     walking groups and tasks repeatedly. */
  ++task_visitor_mark ;
  provision = DLL_GET_HEAD(&group_schedule, task_group_t, order_link) ;
  provision_failed = FALSE ;
  for ( task = DLL_GET_HEAD(&task_schedule, task_t, order_link) ;
        task != NULL ;
        task = DLL_GET_NEXT(task, task_t, order_link) ) {
    if ( task->mark - task_filter_mark >= 0 ) {
      task_group_t *group = task->group ;

      if ( task->runnability == RL_READY_UNPROVISIONED &&
           group->mark != task_visitor_mark ) {
        HQASSERT(task->state == TASK_READY && NEEDS_PROVISION(group),
                 "Task runnability confused") ;
        HQASSERT(provision != NULL,
                 "Nothing on group schedule, but task is unprovisioned") ;

        /* Walk the group provision order, trying to provision groups until
           we have visited this group. Provisioning the group will change the
           runnability of this task. */
        do {
          task_group_t *next = DLL_GET_NEXT(provision, task_group_t, order_link) ;

          /* Provision the first group in the schedule we have not yet tried
             to provision. This group may have been marked as visited and
             failing by a previous call to provision_group, in which case this
             call will propagate that failure to successor groups. */
          provision_group(provision, &next) ;

          provision = next ;

          /* It is possible that we have a perverse task schedule, in which
             the group we want is a parent of a group that appears earlier
             than it on the task schedule. The parent recursion in
             provision_group() will provision the group we want, but we want
             to stop as soon as it has been visited, we then know if this
             task is usable. */
        } while ( group->mark != task_visitor_mark ) ;
      }

      if ( task->runnability < found->level &&
           (found->specialiser == NULL ||
            (task->specialiser == found->specialiser &&
             task->specialiser_args == found->specialiser_args)) ) {
        found->task = task ;
        found->level = task->runnability ;
        result = TRUE ;
        if ( task->runnability < RL_NOW
             /* Dispatchable are good enough if there are no helpable tasks.
                This doesn't account for the possibility that we might
                provision a group that will then contain a helpable task,
                however that could entail testing a whole load of group
                provisioning unnecessarily. */
             || (task->runnability < RL_DISPATCHABLE && tasks_runnable_helpable == 0)
             )
          break ;
      }
    }
  }

  return result ;
}

/** \brief Run tasks recursively if there are too many building up. */
static void task_helper_locked(corecontext_t *context, helper_reason_t reason)
{
  UNUSED_PARAM(helper_reason_t, reason) ; /* Probe builds only */

  HQASSERT(context, "No core context for thread") ;
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Helping when unlocked") ;

#ifdef DEBUG_BUILD
  if ( (debug_tasks & DEBUG_TASKS_RUNREADY) != 0 ) /* Build whole graph */
    return ;
#endif

  if ( helper_wait_ms > 0 && tasks_incomplete >= helper_start_threshold ) {
    Bool throttling = (tasks_incomplete >= helper_wait_threshold) ;
    task_context_t *taskcontext;
    task_t *current ;
    HqU32x2 when ;
#ifdef PROBE_BUILD
    int help_wakeups = 0 ;
    int help_timeouts = 0 ;
#endif

    taskcontext = context->taskcontext ;
    VERIFY_OBJECT(taskcontext, TASK_CONTEXT_NAME) ;
    current = taskcontext->current_task ;
    VERIFY_OBJECT(current, TASK_NAME) ;

    /* The task helper wait is the total timeout from now that we'll wait
       for tasks to either become helpable, or to burn down the task graph
       to a more reasonable size. After that, if we've not managed to
       reduce the task graph, we'll reluctantly continue creating it, but
       will probably stall the producer again soon. This will only be a
       problem if thousands of tasks are created before any task is made
       ready. */
    HqU32x2FromUint32(&when, (uint32)(helper_wait_ms * 1000)) ;
    get_time_from_now(&when) ;

    probe_begin(SW_TRACE_TASK_HELPING, (intptr_t)reason) ;

#ifdef DEBUG_BUILD
    if ( (debug_tasks & DEBUG_TASKS_GRAPH_HELP) != 0 )
      debug_schedule_graph = 1 ;
#endif

    /* Let's see if we need to help out by running some tasks recursively. */
    /* If we throttle and wait because of too many tasks, we get woken again
     * when "tasks_incomplete == helper_end_threshold", so we must ensure this
     * loop will finish when that is true, else it will do one timedwait too
     * many !
     */
    while ( tasks_incomplete > helper_end_threshold &&
            current->state == TASK_RUNNING &&
            !rip_is_quitting() ) {
      task_generation_t new_helper_generation = group_resource_generation +
        task_unprovisioned_generation ;
      task_find_t found = { NULL, RL_HELPABLE, NULL, NULL } ;

      if ( tasks_runnable_helpable > 0 ) {
        find_scheduled_task(&found) ;
        HQASSERT(found.task != NULL, "Didn't find a task to help") ;
        /* If there's nothing on helpable, see if there's anything
           unprovisioned list that could be made helpable. */
      } else if ( tasks_ready_unprovisioned > 0 &&
                  helper_generation != new_helper_generation ) {
        if ( find_runnable_task(&found, NULL) ) {
          HQASSERT(found.task != NULL, "Didn't find a task to help") ;
        } else {
          helper_generation = new_helper_generation ;
          probe_other(SW_TRACE_POINTLESS_GRAPH_WALKS, SW_TRACETYPE_VALUE,
                      ++pointless_traverses) ;
        }

        /* We might have found other dispatchable tasks during the graph
           walk. If we've got spare capacity, wake another thread to look for
           work. Ignore the task we did find (if any) in making the wakeup
           decision. */
        wake_next_thread(NULL, found.task, 0) ;
      }

      if ( found.task == NULL ) {
        TRACE_POINTLESS(SW_TRACE_POINTLESS_WAKEUPS, pointless_wakeups,
                        help_wakeups) ;

        /* No helpable task, so either quit helping or throttle the producer. */
        if ( !throttling ) {
          /* We're not throttling the producer, so quit when nothing to do. */
          break ;
        } else {
          Bool timeout ;
          Bool extended_thread_pool = begin_thread_extension() ;

          DECREMENT_THREADS_ACTIVE() ;
          taskcontext->state = THREAD_WAIT_HELP ;
          PROBE(SW_TRACE_TASK_HELPER_WAIT, -1,
                timeout = multi_condvar_timedwait(&taskcontext->cond, when)) ;
          taskcontext->state = THREAD_RUNNING ;
          INCREMENT_THREADS_ACTIVE() ;

          if ( extended_thread_pool )
            end_thread_extension() ;

          if ( timeout ) {
            /* We timed out waiting to find a helpable task. Reluctantly
               allow task producers to continue. They'll probably stall in
               this function again almost immediately, if they're producing
               significant numbers of tasks. */
            /** \todo ajcd 2012-07-06: This could have been a real wakeup.
                How would we know? */
            MAYBE_POINTLESS(help_timeouts) ;
            break ;
          }

          /* Woken up without a timeout. */
          MAYBE_POINTLESS(help_wakeups) ;
          continue ;
        }
      }

      /* We found a task to help with. Run it recursively. Don't run any
         successor tasks, they may not have been checked for helpability. */
      task_recursive(context, current, found.task, SUCC_NONE) ;

#ifdef PROBE_BUILD
      HQTRACE(limit_max_original > 1 &&
              tasks_incomplete <= (unsigned int)threads_active,
              ("Starved tasks while helping run ready tasks, maybe increase threshold?")) ;
#endif
    }

#ifdef DEBUG_BUILD
    /* Just in case we didn't recompute the schedule */
    if ( (debug_tasks & DEBUG_TASKS_GRAPH_HELP) != 0 )
      debug_schedule_graph = 0 ;
#endif

    probe_end(SW_TRACE_TASK_HELPING, (intptr_t)reason) ;

    TRACE_POINTLESS(SW_TRACE_POINTLESS_TIMEOUTS, pointless_timeouts,
                    help_timeouts) ;
  } else {
    /* Not helping, so see if we can start another thread. */
    wake_next_thread(NULL, NULL, 0) ;
  }
}

/****************************************************************************/
/** \brief Having set up the contexts suitably, run the worker function
    and the finaliser function, moving the task through the running states
    to the completed state */
static void task_execute(corecontext_t *context, task_t *task,
                         task_successor_t succ, task_t **reserved)
{
  task_link_t *entry ;
  hq_atomic_counter_t after ;
  Bool result = FALSE ;
  task_group_t *group ;

  VERIFY_OBJECT(task, TASK_NAME) ;

  HQASSERT(task->state == TASK_RUNNING ||
           task->state == TASK_FINALISING ||
           task->state == TASK_CANCELLING, "Running task in invalid state") ;

  /* Assume read of state variable is atomic. It wouldn't help to lock this,
     because the moment we unlock to run the worker, another task could
     cancel it. It's OK to start the worker in state TASK_CANCELLING, it is
     equivalent to a cancellation arriving before the first statement of the
     function. */
  if ( task->state == TASK_RUNNING ) {
    /* Task is now running, call worker function with args. */
    if ( task->worker != NULL ) {
      result = task->worker(context, task->args) ;
    } else { /* Empty worker allows for finaliser-only tasks. */
      result = TRUE ;
    }
  }

  /** \todo ajcd 2012-07-18: Can this lock be deferred until after the
      finaliser? It can if the transition to TASK_FINALISING is safe when
      unlocked. Running, finalising, and cancelling are equivalent states for
      most operations. Any operations testing current->state are not relevant
      because they cannot be running on another thread and accessing this
      task. */
  multi_mutex_lock(&task_mutex) ;
#define return DO_NOT_return_FALL_THROUGH_INSTEAD!

  HQASSERT(task->state == TASK_RUNNING ||
           task->state == TASK_FINALISING ||
           task->state == TASK_CANCELLING, "Finalising in invalid state") ;

  /* There is a reference created by task_activate_locked() that is used to
     prevent the task disappearing whilst we remove all of the dependents. */
  task->state = TASK_FINALISING ;

  group = task->group ;
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( !result ) {
    /* Cancelling a task is equivalent to cancelling its group. */
    HQASSERT(group != &orphaned_task_group,
             "Propagating cancel to orphaned task group") ;
    task->result = FALSE ;
    group_cancel_locked(group, context->error) ;
  }

  if ( task->cleaner != NULL ) {
    /* The task cleaner needs to run with the mutex unlocked, so it can
       release, join or cancel task references in the args. */
    multi_mutex_unlock(&task_mutex) ;
    task->cleaner(context, task->args) ;
    multi_mutex_lock(&task_mutex) ;
  }

  HQASSERT(DLL_LIST_IS_EMPTY(&task->joins_list),
           "Task didn't join groups it was responsible for") ;

  task->state = TASK_DONE ;
  DECREMENT_TASKS_ACTIVE() ;

  /* Wait until after finalising task to remove dependents. */
  for ( entry = DLL_GET_HEAD(&task->afterme, task_link_t, post_list) ;
        entry != NULL ; ) {
    task_link_t *next = DLL_GET_NEXT(entry, task_link_t, post_list) ;
    task_t *post = entry->post ;

    VERIFY_OBJECT(post, TASK_NAME) ;
    HQASSERT(entry->pre == task, "After list doesn't have self before") ;

    /* It's OK to unlink the entry before accessing post, because there will
       still be a group->task reference until we unlock the task mutex. */
    switch ( post->state ) {
    case TASK_CONSTRUCTING: /* Scheduler dependency link. */
      task_unlink_locked(entry) ;
      if ( !task->result ) {
        /* Subject task failed, so cancel its own group and its dependents
           groups, propagating the error reason, but not to the join task for
           this group. */
        task_cancel_locked(post, context->error) ;
      }
      break ;
    case TASK_DEPENDING:   /* Scheduler dependency link. */
      task_unlink_locked(entry) ;
      if ( !task->result ) {
        /* Subject task failed, so cancel its own group and its dependents
           groups, propagating the error reason, but not to the join task for
           this group. */
        task_cancel_locked(post, context->error) ;
      } else if ( DLL_LIST_IS_EMPTY(&post->beforeme) ) {
        /* Task is now ready to run. */
        post->state = TASK_READY ;
        task_runnability(post) ;

        /* If all went well with this task, and dependent has the same group
           and specialiser, and we're not doing a recursive invocation,
           reserve this task for this thread to do next. This should improve
           schedule quality and data locality. We only allow reservation of
           tasks within the same group. This ensures that they are
           provisioned, it ensures that they contribute to the same join
           group (if joining) and takes care of the vast majority of tasks
           sharing the same data ID. */
        if ( (succ & SUCC_GROUP) != 0 &&
             post->group == group &&
             post->specialiser == task->specialiser &&
             post->specialiser_args == task->specialiser_args &&
             /* We disallow task reservation if we're oversubscribed, because
                we need a thread to pause. If at the limit, it's OK, because
                we're re-using this thread. */
             threads_scheduled <= thread_limit_current &&
             threads_scheduled <= thread_limit_max &&
             *reserved == NULL &&
             !rip_is_quitting() ) {
          task_activate_locked(post) ;
          *reserved = post ;
        }
      }
      break ;
    case TASK_READY:        /* Link should not have existed. */
    case TASK_RUNNING:      /* Link should not have existed. */
    case TASK_CANCELLING:   /* Link should not have existed. */
    default:
      HQFAIL("Invalid state for post-link task") ;
      break ;
    case TASK_CANCELLED:    /* Cancelled dependency before we got there */
    case TASK_FINALISING:   /* Cancelled dependency before we got there */
    case TASK_DONE:         /* Cancelled dependency before we got there */
      /* Constructing dependency cancelled before we got there? */
      HQASSERT(!post->result, "Invalid state for post-link task") ;
      task_unlink_locked(entry) ;
      break ;
    }

    entry = next ;
  }

  --tasks_incomplete ;
  probe_other(SW_TRACE_TASKS_INCOMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)tasks_incomplete) ;
  ++tasks_complete ;
  probe_other(SW_TRACE_TASKS_COMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)tasks_complete) ;

  /* If this was the last task in the group's list, it might have made
     the joiner for the group helpable. The group may also be able to be
     deprovisioned, if it was closed (and so couldn't get any more tasks). */
  --group->n_tasks ;
  DLL_REMOVE(task, group_link) ; /* Remove from group list */
  DLL_REMOVE(task, order_link) ; /* Remove from schedule */

  /* The group still has its parent link, and this task link, until we remove
     the task reference below. */
  HqAtomicDecrement(&group->refcount, after) ;
  HQASSERT(after >= 1, "Task group destroyed without join") ;

  /* Account for the reference from the group's task list. We still have the
     task_activate_locked() reference after this decrement, it will be removed
     when we return from this function. */
  HqAtomicDecrement(&task->refcount, after) ;
  HQASSERT(after >= 1, "Task does not have activation reference") ;

  /* Put the task on the orphaned task group, in case the activation release
     doesn't free it because someone else has a reference. We can trace the
     task in case of leaks using this group. */
  DLL_ADD_TAIL(&orphaned_task_group.tasks, task, group_link) ;
  task->group = &orphaned_task_group ;

  if ( group->n_tasks == 0 ) {
    task_runnability(group_get_joiner(group)) ;

    /* If this group has no sub-groups, we can deprovision it now. If it's a
       nested group, we can join it (which will deprovision the group). Then
       we should check for ancestor groups that might be deprovisioned or
       joined because this was their last sub-group. */
    while ( group->state == GROUP_STATE_CLOSED &&
            group->n_tasks == 0 &&
            DLL_LIST_IS_EMPTY(&group->children) ) {
      task_group_t *parent = group->parent ;

      if ( group->join == NULL ) {
        /* Nested groups can be joined and removed when closed and empty,
           because no new tasks can be added to them. Since errors are
           propagated to the parent group immediately, the ultimate joiner
           task can pick them up from there, we don't need to save the group
           to get its result. */
        group_mark_joined(group) ;
      } else {
        /* Non-nested groups require a joiner task to join them, but can be
           deprovisioned. Since the group won't be removed, there will be at
           least one sub-group of the parent group, and the parent can't be
           deprovisioned. */
        task_group_deprovision(group) ;
        break ;
      }

      group = parent ;
      HQASSERT(group != NULL, "Root group should not have been closed") ;
    }
  }

  /* Release joins in case this task contributes to them. We mark them all
     as signalled, so we won't wake more threads until all of these have
     checked if they can proceed. */
  wake_all_threads(THREAD_WAIT_JOIN, THREAD_SIGNALLED) ;

  /* See if we've made anything else ready to do, either by releasing
     dependencies or joins. */
  /** \todo ajcd 2012-07-17: threads_scheduled has not yet been reduced.
      We may have reserved a task, in which case the caller will loop around,
      and we don't want to wake any more if we're at the limit. */
  wake_next_thread(NULL, NULL, *reserved == NULL ? -1 : 0) ;

  multi_mutex_unlock(&task_mutex) ;
#undef return
}

static Bool task_depends_locked(task_t *pre, task_t *post)
{
  Bool result = TRUE ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Cancelling unlocked task") ;

  VERIFY_OBJECT(pre, TASK_NAME) ;
  VERIFY_OBJECT(post, TASK_NAME) ;
  HQASSERT(pre->trace_id != SW_TRACE_INTERPRET,
           "Shouldn't depend on the interpreter task, it won't finish") ;

  /* We allow failed tasks here, because the task may have been cancelled by
     some other thread before we got to add this dependency (for instance, if
     another predecessor task ran and failed, or was cancelled). If the task
     is depending then it must be a dependee of the current task, to
     ensure deterministic execution. */
  HQASSERT(!post->result ||
           post->state == TASK_CONSTRUCTING ||
           (post->state == TASK_DEPENDING &&
            task_predecessors(post, &task_before, &task_group_before,
                              task_current_uncounted())),
           "Dependent task should have been under construction or dependent of current") ;

  /* It doesn't actually matter if we add multiple identical dependencies,
     the dependency code can handle it. */
  if ( post->result ) {
    switch ( pre->state ) {
    case TASK_DONE:
      /* Done predecessor already, check its result. */
      if ( pre->result )
        break ;
      /*@fallthrough@*/
    case TASK_CANCELLED:
    case TASK_CANCELLING:
      if ( post != group_get_joiner(pre->group) ) {
        /* Cancel post, its dependencies cannot be met. Propagate the error
           that caused the failure to all dependent tasks, and to this task,
           because we need to have an error set up for the current task. */
        task_cancel_locked(post, &pre->group->error) ;
        HQFAIL("Not tested: task cancel cleanup") ;
        result = FALSE ;
      }
      break ;
    case TASK_DEPENDING:
    case TASK_CONSTRUCTING:
    case TASK_READY:
    case TASK_RUNNING:
      HQASSERT(pre->result, "Task failure but not cancelled") ;
      /*@fallthrough@*/
    case TASK_FINALISING:
      /* The previous task result may only be FALSE if the task is in the
         FINALISING state. We have to wait for the task to complete in that
         case anyway, so we'll try to set up the link if the task will
         succeed. The result can be used as a shorthand for testing the
         states, so we'll test it as a matter or course. */
      if ( !pre->result ||
           !task_link(pre, &post->beforeme, post, &pre->afterme) ) {
        /* Failure to set up dependency is treated just the same as a failed
           predecessor task; post is cancelled. */
        task_cancel_locked(post, CoreContext.error) ;
        HQFAIL("Not tested: task cancel cleanup") ;
        result = FALSE ;
      }
      break ;
    default:
      HQFAIL("Predecessor task in unknown state") ;
    }
  }

  return result ;
}

Bool task_depends(task_t *pre, task_t *post)
{
  Bool result ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

  multi_mutex_lock(&task_mutex) ;
  result = task_depends_locked(pre, post) ;
  multi_mutex_unlock(&task_mutex) ;

  return result ;
}

Bool task_group_set_joiner(task_group_t *group, task_t *task, void *args)
{
  task_t *current ;
  Bool result = TRUE ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  current = task_current_uncounted() ;

  multi_mutex_lock(&task_mutex) ;
  HQASSERT(group->join == current,
           "Transferring join responsibility from a task we don't own") ;
  HQASSERT(task == NULL || task->args == NULL, "Join task already has args") ;
  if ( rip_is_quitting() || current->state != TASK_RUNNING ) {
    /* We've probably already signalled a valid error in this case. */
    result = error_handler(UNDEFINED) ;
  } else if ( group->state == GROUP_STATE_CANCELLED ||
              group->state == GROUP_STATE_JOINED ) {
    HQASSERT(group->error.old_error != 0,
             "Joining group with non-constructing task, no error signalled") ;
    result = error_handler(group->error.old_error != 0
                           ? group->error.old_error
                           : UNDEFINED) ;
  } else if ( task == NULL ) {
    hq_atomic_counter_t count ;

    /* Nested group join. The group will be joined by its parent group.
       We've already checked that this group is not joined or cancelled,
       which means its parent group is not joined or cancelled either,
       because joins and cancels do recurse into subgroups. */
    HQASSERT(group->parent != NULL, "No parent group for nested group join") ;
    HQASSERT(group->parent->state != GROUP_STATE_JOINED &&
             group->parent->state != GROUP_STATE_CANCELLED,
             "No parent group for nested group join") ;
    HQASSERT(args == NULL, "Cannot set task args if joining recursively") ;

    /* Remove group's existing joiner. */
    DLL_REMOVE(group, joins_link) ;
    group->join = NULL ;
    HqAtomicDecrement(&current->refcount, count) ;
    HQASSERT(count > 0, "Old join task should still have reference") ;
    HqAtomicDecrement(&group->refcount, count) ;
    HQASSERT(count > 0, "Task group should still have reference") ;
    need_recompute_schedule = TRUE ;
  } else if ( task->state != TASK_CONSTRUCTING ) {
    /* The task we're wanting to transfer join responsibility has been
       cancelled. We may not be able to retrieve the error under which it was
       cancelled. If the task has already executed, the group will be reset
       to the orphaned task group, so we won't be able to get the task's
       original group's error context. We obviously cannot overwrite the
       orphaned group's error for each different task group failure. */
    VERIFY_OBJECT(task, TASK_NAME) ;
    HQASSERT(task->group->error.old_error != 0,
             "Joining group with non-constructing task, no error signalled") ;
    result = error_handler(task->group->error.old_error != 0
                           ? task->group->error.old_error
                           : UNDEFINED) ;
  } else {
    hq_atomic_counter_t count ;

    VERIFY_OBJECT(task, TASK_NAME) ;

    /* This doesn't affect the runnability of the existing joiner, because
       it's already running. It doesn't affect the runnability of the new
       joiner either, because it's constructing. */

    /* Remove group's existing joiner. */
    DLL_REMOVE(group, joins_link) ;
    group->join = NULL ;
    HqAtomicDecrement(&current->refcount, count) ;
    HQASSERT(count > 0, "Old join task should still have reference") ;

    /* Walk the predecessor set for group, determining if task is in it. It
       shouldn't be. */
    HQASSERT(!task_group_predecessors(group, &task_before, &task_group_before,
                                      task),
             "Task cannot join group it must complete before") ;

    HqAtomicIncrement(&task->refcount, count) ;
    HQASSERT(count > 0, "New join task should have had reference") ;
    group->join = task ;
    DLL_ADD_TAIL(&task->joins_list, group, joins_link) ;

    task->args = args ;

    need_recompute_schedule = TRUE ;
  }

  multi_mutex_unlock(&task_mutex) ;

  return result ;
}

Bool task_replace(task_t *replace, task_t *in, task_t *out)
{
  Bool result = FALSE ;
  task_t *current ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(replace, TASK_NAME) ;
  VERIFY_OBJECT(in, TASK_NAME) ;
  VERIFY_OBJECT(out, TASK_NAME) ;

  current = task_current_uncounted() ;

  multi_mutex_lock(&task_mutex) ;

  /* The replace task must either be constructing, or it must be depending on
     the current task, or it must be the current task for deterministic
     execution. We allow failed tasks here, because the replace task may have
     been cancelled by some other thread before we got to add this dependency
     (for instance, if another predecessor task ran and failed, or was
     cancelled.) */
  HQASSERT(!replace->result ||
           current == replace ||
           replace->state == TASK_CONSTRUCTING ||
           (replace->state == TASK_DEPENDING &&
            task_predecessors(replace, &task_before, &task_group_before, current)),
           "Task replacement may not be deterministic") ;

  /* The incoming dependency task is the current task if and only if we're
     replacing the current task (a continuation). */
  HQASSERT((current == replace) == (current == in),
           "Task continuation replacement has invalid incoming task.") ;
  HQASSERT(!in->result ||
           in == replace ||
           (in->state == TASK_CONSTRUCTING &&
            !task_predecessors(replace, &task_before, &task_group_before, in)),
           "Incoming replacement task is not suitable") ;

  /* Using the current task as the outgoing dependency task makes no real
     sense, because we can't impose the incoming dependencies on the current
     task. It might just be usable to edit out parts of the graph, but that's
     dangerous because we might miss out intermediate task dependencies. If
     it was the current task, it would have to be the same as the incoming
     dependency task, and that would be a no-op. */
  HQASSERT(current != out,
           "Outgoing dependencies cannot not be transferred to current task.") ;
  HQASSERT(!out->result ||
           out == replace ||
           (out->state == TASK_CONSTRUCTING &&
            !task_predecessors(replace, &task_before, &task_group_before, out)),
           "Outgoing replacement task is not suitable") ;

  /** \todo ajcd 2012-08-30: Should we also test current->result? That would
      prevent graph elaboration or continuations when in a failing task. We
      could use that to construct a non-failing continuation from a failed
      task? That may be too dangerous to allow? What error propagation should
      happen if we test current->result? */
  if ( !replace->result ) {
    /* Can't replace task because it's already been cancelled. That
       cancellation should propagate to the tasks we were going to replace
       it with. */
    task_cancel_locked(in, &replace->group->error) ;
    task_cancel_locked(out, &replace->group->error) ;
  } else if ( !in->result ) {
    /* Incoming replacement has been cancelled. Propagate cancellation to
       outgoing replacement. */
    task_cancel_locked(out, &in->group->error) ;
  } else if ( !out->result ) {
    /* Outgoing replacement has been cancelled. Don't propagate cancellation
       backwards, let the caller decide how to handle failure. */
    EMPTY_STATEMENT() ;
  } else {
    if ( replace != in ) {
      task_link_t *entry ;

      /* Transfer incoming dependencies from replaced task to in. */
      for ( entry = DLL_GET_HEAD(&replace->beforeme, task_link_t, pre_list) ;
            entry != NULL ; ) {
        task_link_t *next = DLL_GET_NEXT(entry, task_link_t, pre_list) ;
        hq_atomic_counter_t count ;

        HQASSERT(entry->post == replace, "Before list doesn't have replaced task after") ;

        HQASSERT(!task_predecessors(entry->pre, &task_before, &task_group_before, in),
                 "Task graph cycle detected") ;

        /* Removing a reference from replace is OK, there's still a reference
           from the caller. */
        DLL_REMOVE(entry, pre_list) ;
        HqAtomicDecrement(&replace->refcount, count) ;
        HQASSERT(count > 0, "Removed last reference to replaced task") ;
        entry->post = in ;
        HqAtomicIncrement(&in->refcount, count) ;
        DLL_ADD_TAIL(&in->beforeme, entry, pre_list) ;
        need_recompute_schedule = TRUE ;

        entry = next ;
      }
    }

    if ( replace != out ) {
      task_link_t *entry ;

      /* Transfer outgoing dependencies from replaced task to out. */
      for ( entry = DLL_GET_HEAD(&replace->afterme, task_link_t, post_list) ;
            entry != NULL ; ) {
        task_link_t *next = DLL_GET_NEXT(entry, task_link_t, post_list) ;
        hq_atomic_counter_t count ;

        HQASSERT(entry->pre == replace, "After list doesn't have replaced task before") ;

        HQASSERT(!task_predecessors(out, &task_before, &task_group_before, entry->post),
                 "Task graph cycle detected") ;

        /* Removing a reference from replace is OK, there's still a reference
           from the caller. */
        DLL_REMOVE(entry, post_list) ;
        HqAtomicDecrement(&replace->refcount, count) ;
        HQASSERT(count > 0, "Removed last reference to replaced task") ;
        entry->pre = out ;
        HqAtomicIncrement(&out->refcount, count) ;
        DLL_ADD_TAIL(&out->afterme, entry, post_list) ;
        need_recompute_schedule = TRUE ;

        entry = next ;
      }
    }

    if ( replace != in && replace != out ) {
      HQASSERT(replace != current, "Can't replace current unless incoming task is current") ;
      HQASSERT(DLL_LIST_IS_EMPTY(&replace->beforeme) &&
               DLL_LIST_IS_EMPTY(&replace->afterme),
               "Replaced task still has dependents or dependees") ;
      /* Cancel the replaced task, without changing its result. We just want
         to make it go straight to finalising. This may fall afoul of
         assertions that cancelled tasks have a false result. */
      replace->state = TASK_CANCELLED ;
      task_runnability(replace) ;
      wake_next_thread(NULL, NULL, 0) ;
    }

    result = (in == out || task_depends_locked(in, out)) ;
  }

  multi_mutex_unlock(&task_mutex) ;

  return result ;
}

void task_ready(task_t *task)
{
  corecontext_t *context = get_core_context() ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

  VERIFY_OBJECT(task, TASK_NAME) ;

  multi_mutex_lock(&task_mutex) ;

  /** \todo ajcd 2011-01-26: For now, assert that this unlikely operation
      shouldn't be done. It's not impossible to do it, it does require that
      multi_unconstrain_to_single() be aware of the possibility that threads
      are made ready during low memory handling. */
  HQASSERT(thread_pool_constrained == 0 || threads_scheduled == 1,
           "Making task ready in low memory handler?") ;

  /* We allow failed tasks here, because the task may have been cancelled by
     some other thread before we got to add this dependency (for instance, if
     another predecessor task ran and failed, or was cancelled.) */
  HQASSERT(task->state == TASK_CONSTRUCTING || !task->result,
           "Ready task should have been under construction") ;
  HQASSERT(task->group->state != GROUP_STATE_CONSTRUCTING,
           "Cannot ready task in an unready group") ;

  /* Task may have been cancelled while constructing. */
  if ( task->state == TASK_CONSTRUCTING ) {
    if ( DLL_LIST_IS_EMPTY(&task->beforeme) ) {
      task->state = TASK_READY ;
      task_runnability(task) ;
    } else {
      /* Changing the task state from constructing to depending does not
         affect the task's runnability. */
      task->state = TASK_DEPENDING ;
    }
  }

  /* In a single threaded system, or if we're overloading the system, we
     might as well run the task immediately. The task must be helpable in
     order to do this. We may prefer to avoid having the interpreter help, so
     it can get further ahead constructing pages. */
#if !INTERPRETER_HELPS
  if ( thread_limit_max == 1 || thread_limit_current == 1 ||
       !context->is_interpreter )
#endif
    task_helper_locked(context, HELP_TASK_READY) ;

  multi_mutex_unlock(&task_mutex) ;
}

/** \brief The first phase of task cancellation, marking as cancelled and
    following through the dependency chain. */
static void task_cancel_locked(task_t *task, const error_context_t *error)
{
  task_link_t *entry ;

  VERIFY_OBJECT(task, TASK_NAME) ;
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Cancelling unlocked task") ;
  HQASSERT(error != NULL, "No error for task cancellation") ;
  HQASSERT(error->new_error != 0 && error->old_error != 0,
           "Invalid error for task cancellation") ;

  switch ( task->state ) {
    multi_condvar_t *waiting_on ;
  case TASK_CANCELLED:
  case TASK_CANCELLING:
  case TASK_FINALISING:
  case TASK_DONE:
    /* Nothing to do, it's completed or cancelled already. */
    return ;
  default:
    /* Mark unknown states as cancelled, we don't know what else to do with
       them. */
    HQFAIL("Task in unknown state") ;
    /*@fallthrough@*/
  case TASK_CONSTRUCTING:
  case TASK_DEPENDING: /* Finaliser for task needs to run, so prepare for it. */
  case TASK_READY: /* Ready has already been signalled. */
    task->state = TASK_CANCELLED ;
    task_runnability(task) ;
    task->result = FALSE ; /* Mark the task as failing. */
    wake_next_thread(NULL, NULL, 0) ;
    break ;
  case TASK_RUNNING:
    /* We shouldn't have had any beforeme link. */
    HQASSERT(DLL_LIST_IS_EMPTY(&task->beforeme),
             "Waiting on more than one task") ;

    /* Task is already active. */
    task->state = TASK_CANCELLING ;
    task->result = FALSE ; /* Mark the task as failing. */

    /* If a running task is waiting on a non-task condvar, broadcast to
       interrupt that wait. */
    if ( (waiting_on = task->waiting_on) != NULL ) {
      /* Since this isn't a task context condvar (or we would have been in
         task_group_join()), we're assuming it has RIP lifetime, and won't
         vanish before we can signal it. We don't need to lock the mutex
         before broadcasting, because it won't do any good. There is a tiny
         window in the multi_condvar_wait_task() that could cause deadlock,
         where the thread could enter the condition wait immediately after
         checking if the task is cancelled, if the task cancelling statement
         above overlaps it. We can't do anything about that race unless we
         wrap both it and this in another mutex or spinlock. Another approach
         might be to yield the thread and broadcast again here, on the theory
         that the broadcast is much heavierweight than the race window, so
         the other thread is likely to get scheduled, enter the wait, and see
         the second broadcast. */
      /** \todo ajcd 2011-07-15: This is a very dubious set of assumptions.
          Work out some spinlock mechanism to protect this, if we decide to
          keep it. */
      int res = pthread_cond_broadcast(&waiting_on->pcond) ;
      HQASSERT(res == 0, "Cancel wait broadcast failed") ;
      UNUSED_PARAM(int, res);
    }
    break ;
  }

  /* Cancelling a task is equivalent to cancelling its group. */
  HQASSERT(task->group != &orphaned_task_group,
           "Propagating cancel to orphaned task group") ;
  group_cancel_locked(task->group, error) ;

  /* Follow through consequences of cancelling this task. */
  for ( entry = DLL_GET_HEAD(&task->afterme, task_link_t, post_list) ;
        entry != NULL ; ) {
    task_link_t *next = DLL_GET_NEXT(entry, task_link_t, post_list) ;
    task_t *post = entry->post ;

    VERIFY_OBJECT(post, TASK_NAME) ;
    HQASSERT(entry->pre == task, "After list doesn't have self before") ;

    /* It's OK to unlink the entry before accessing post, because there will
       still be a group reference until we unlock the task mutex. */
    switch ( post->state ) {
    case TASK_CONSTRUCTING:
    case TASK_DEPENDING:  /* Scheduler dependency link */
      task_unlink_locked(entry) ;

      /* We allow cancellation of our join task. It's still got to join us,
         by using a finaliser. If you really want to stop propagation, put it
         in a group and just join the group with no dependency from inside
         the group to outside. */
      task_cancel_locked(post, error) ;
      break ;
    case TASK_READY:      /* Should have no before links */
    case TASK_RUNNING:    /* Should have no before links */
    case TASK_CANCELLING: /* Link should have been removed already. */
      /* This shouldn't have happened. But somehow it did. We need to
         remove the link as well as moan about it. */
      task_unlink_locked(entry) ;
      /*@fallthrough@*/
    default:
      HQFAIL("Invalid task ordering link") ;
      break ;
    case TASK_FINALISING: /* Cancelled before we got to it */
    case TASK_DONE:       /* Cancelled before we got to it */
      HQASSERT(!post->result, "Invalid task ordering link") ;
      /*@fallthrough@*/
    case TASK_CANCELLED:  /* Cancelled before we got to it. */
      task_unlink_locked(entry) ;
      break ;
    }

    entry = next ;
  }
}

static void group_cancel_locked(/*@notnull@*/ /*@in@*/ task_group_t *group,
                                const error_context_t *error)
{
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Cancelling when unlocked") ;

  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( group->state != GROUP_STATE_CANCELLED &&
       group->state != GROUP_STATE_JOINED ) {
    task_group_t *child ;
    task_t *task ;

    group->state = GROUP_STATE_CANCELLED ;
    group->result = FALSE ;

    /* If this group still wants provisioning, indicate it won't need it. */
    if ( NEEDS_PROVISION(group) ) {
      group->provisions = PROVISION_CANCELLED ;
      DLL_REMOVE(group, order_link) ;
    }

    /* Capture old error into group. */
    group->error.new_error = error->new_error ;
    if ( group->error.old_error <= 0 )
      group->error.old_error = error->old_error ;
    /** \todo ajcd 2011-06-28: Propagate error detail. */

    /* Cancel all child groups. */
    for ( child = DLL_GET_HEAD(&group->children, task_group_t, peers) ;
          child != NULL ;
          child = DLL_GET_NEXT(child, task_group_t, peers) ) {
      group_cancel_locked(child, error) ;
    }

    /* Mark all of the tasks in the group for cancellation. */
    for ( task = DLL_GET_HEAD(&group->tasks, task_t, group_link) ;
          task != NULL ;
          task = DLL_GET_NEXT(task, task_t, group_link) ) {
      task_cancel_locked(task, error) ;
    }

    /* Nested group join propagates to parent (and entire join set). Changing
       the group state to cancelled may affect the runnability of a joiner
       task. */
    if ( group->join == NULL )
      group_cancel_locked(group->parent, error) ;
    else
      task_runnability(group->join) ;
  }
}

void task_group_cancel(task_group_t *group, int32 reason)
{
  error_context_t errcontext = ERROR_CONTEXT_INIT ;
  corecontext_t* context = get_core_context();

  HQASSERT(mm_pool_task, "Task pool not active") ;

  errcontext.new_error = errcontext.old_error = reason ;

  multi_mutex_lock(&task_mutex) ;
  group_cancel_locked(group, &errcontext) ;
  if (context != NULL && context->taskcontext->current_task != NULL) {
    task_helper_locked(context, HELP_GROUP_CANCEL) ;
  }
  multi_mutex_unlock(&task_mutex) ;
}


Bool task_group_is_cancelled(task_group_t *group)
{
  VERIFY_OBJECT(group, TASK_GROUP_NAME);
  return group->state == GROUP_STATE_CANCELLED;
}


int32 task_group_error(/*@notnull@*/ /*@in@*/ task_group_t *group)
{
  VERIFY_OBJECT(group, TASK_GROUP_NAME);
  return group->error.old_error;
}


static void group_mark_joined(task_group_t *group)
{
  HQASSERT(multi_mutex_is_locked(&task_mutex), "Joining when unlocked");

  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( group->state != GROUP_STATE_JOINED ) {
    task_group_t *child ;

    group->state = GROUP_STATE_JOINED ;

    while ( (child = DLL_GET_HEAD(&group->children, task_group_t, peers)) != NULL ) {
      group_mark_joined(child) ;
    }

    task_group_deprovision(group) ;

    if ( group->required != NULL ) {
      group->reqnode = NULL ;
      resource_requirement_release(&group->required) ;
    }
    HQASSERT(group->reqnode == NULL && group->required == NULL,
             "Didn't dispose of group requirements properly") ;

    HQASSERT(DLL_LIST_IS_EMPTY(&group->tasks) && group->n_tasks == 0,
             "Joined group still has tasks") ;
    HQASSERT(DLL_LIST_IS_EMPTY(&group->children),
             "Joined group still has child links") ;

    /* If a sub-group is joined by a task outside of the top-level joined
       group, then the runnability of the joiner may be affected by the join.
       The joiner of the top-level joined group is removed before marking
       groups, so this won't affect it. */
    if ( group->join != NULL )
      task_runnability(group->join) ;

    /* Remove group from parent */
    if ( group->parent != NULL ) {
      --groups_incomplete ;
      probe_other(SW_TRACE_TASK_GROUPS_INCOMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)groups_incomplete) ;
      ++groups_complete ;
      probe_other(SW_TRACE_TASK_GROUPS_COMPLETE, SW_TRACETYPE_AMOUNT, (intptr_t)groups_complete) ;

      DLL_REMOVE(group, peers) ;
      group_release_locked(&group->parent) ;

      /* Make group a child of uncounted orphaned group, so leaks can be
         found. */
      DLL_ADD_TAIL(&orphaned_task_group.children, group, peers) ;
      group->parent = &orphaned_task_group ;

      group_release_locked(&group) ;
    } else {
      HQASSERT(group == &orphaned_task_group ||
               group == &incomplete_task_group,
               "Normal group has no parent") ;
    }
  }
}

/** \brief Raw condvar waits used by task joining encapsulated so that timed
    waits can be easily used instead. */
static inline Bool join_condvar_wait(task_context_t *taskcontext,
                                     task_grouptype_t grouptype,
                                     thread_count_t wake_for_resources)
{
  Bool timedout = FALSE ;

  UNUSED_PARAM(task_grouptype_t, grouptype) ; /* Probes only */

  /* Note that we use the raw version of the condwait here, because this wait
     must be uncancellable (timed waits are uncancellable too). */
  taskcontext->state = THREAD_WAIT_JOIN ;
  DECREMENT_THREADS_ACTIVE() ;
  threads_waiting_for_resources += wake_for_resources ;
  /** \todo ajcd 2011-09-14: Decide whether to keep timed waits. */
  if ( join_wait_ms > 0 ) {
    HqU32x2 when ;
    HqU32x2FromUint32(&when, (uint32)(join_wait_ms * 1000)) ;
    get_time_from_now(&when);
    PROBE(SW_TRACE_TASK_JOIN_WAIT, grouptype,
          timedout = multi_condvar_timedwait(&taskcontext->cond, when)) ;
  } else {
    PROBE(SW_TRACE_TASK_JOIN_WAIT, grouptype,
          multi_condvar_wait_raw(&taskcontext->cond)) ;
  }
  threads_waiting_for_resources -= wake_for_resources ;
  INCREMENT_THREADS_ACTIVE() ;
#if 0
  if ( taskcontext->state == THREAD_SIGNALLED )
    timedout = FALSE ;
#endif
  taskcontext->state = THREAD_RUNNING ;

  return timedout ;
}

Bool task_group_join(task_group_t *group, error_context_t *errcontext)
{
  /* Task group joining can not be allowed to fail, so we take special
     measures to avoid errors due to low-memory situations, and in extremis,
     we use busy-wait loop until we either find a recursive ready task, or
     complete. */
  corecontext_t *context = get_core_context();
  task_context_t *taskcontext;
  task_t *current ;
  hq_atomic_counter_t after ;
  Bool result ;
#ifdef PROBE_BUILD
  int join_traverses = 0 ;
  int join_wakeups = 0 ;
  int join_timeouts = 0 ;
#endif

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  HQASSERT(rip_is_quitting() ||
           (group != &incomplete_task_group && group != &orphaned_task_group),
           "Cannot join special task groups unless quitting RIP") ;
  HQASSERT(group->state != GROUP_STATE_JOINED, "Group has already been joined") ;

  current = group->join ;
  VERIFY_OBJECT(current, TASK_NAME) ;

  HQASSERT(context, "No core context for thread") ;
  taskcontext = context->taskcontext ;
  VERIFY_OBJECT(taskcontext, TASK_CONTEXT_NAME) ;
  HQASSERT(taskcontext->current_task == current,
           "Current task cannot safely join this task group") ;

  HQASSERT(assert_test_no_locks_held(), "Cannot join tasks with locks held") ;

  /* The reference we were called with should prevent this group from
     disappearing whilst we join it. */
#define return DO_NOT_return_FALL_THROUGH_INSTEAD!
  probe_begin(SW_TRACE_TASK_JOINING, (intptr_t)group->type) ;

  multi_mutex_lock(&task_mutex) ;

#ifdef DEBUG_BUILD
  if ( (debug_tasks & DEBUG_TASKS_GRAPH_JOIN) != 0 )
    debug_schedule_graph = 1 ;

  group->being_joined = TRUE ;
#endif

  /* Search all tasks for any task that contributes to joining this task. */
  for (;;) {
    Bool extended_thread_pool ;
    task_find_t found = { NULL, RL_ANY, NULL, NULL } ;

    MAYBE_POINTLESS(join_traverses) ;
    /* Find any task in the predecessors of the group, provisioning as we go.
       We prefer to select tasks that are immediately runnable. */
    if ( !find_runnable_task(&found, group) ) {
      WASNT_POINTLESS(join_traverses) ;
      HQASSERT(found.task == NULL, "Task still exists in join predecessors") ;
      break ;
    }

    /* We might have provisioned or skipped other dispatchable tasks during
       the graph walk. Ignore any task we did find when deciding whether to
       wake up another thread. We start searching for threads to wake at the
       current task context so that joins don't continually loop around, waking
       a new thread for each failed graph traversal. */
    wake_next_thread(taskcontext, found.task, 0) ;

    switch ( found.level ) {
    case RL_READY_UNPROVISIONED:
      /* This thread will wait until there are enough resources to
         provision a new task group. We won't extend the thread pool,
         because any new thread would just block for the same reason. */

      /** \todo ajcd 2012-10-23: Deadlock detection needs to know if any new
          resources will become available. */

      /* Yes, all of the traverses, timeouts and wakeups noted were
         pointless, because we're just going to go to sleep again. */
      TRACE_POINTLESS(SW_TRACE_POINTLESS_GRAPH_WALKS, pointless_traverses,
                      join_traverses) ;
      TRACE_POINTLESS(SW_TRACE_POINTLESS_TIMEOUTS, pointless_timeouts,
                      join_timeouts) ;
      TRACE_POINTLESS(SW_TRACE_POINTLESS_WAKEUPS, pointless_wakeups,
                      join_wakeups) ;
      if ( join_condvar_wait(taskcontext, group->type, 1) ) {
        /* This timeout may be pointless, we don't know yet. */
        MAYBE_POINTLESS(join_timeouts) ;
      } else {
        /* This wakeup may be pointless, we don't know yet. */
        MAYBE_POINTLESS(join_wakeups) ;
      }
      break ;
    case RL_JOINS_IS_EMPTY:
    case RL_JOINS_NOTHING:
    case RL_JOINS_MAYBE_EMPTY:
    case RL_JOINS_NOT_EMPTY:
      /* Whatever wakeup, timeout, or traverse we just did wasn't pointless,
         because we found something useful to do. */
      WASNT_POINTLESS(join_timeouts) ;
      WASNT_POINTLESS(join_wakeups) ;
      WASNT_POINTLESS(join_traverses) ;
      task_recursive(context, current, found.task, SUCC_GROUP) ;
      break ;
    case RL_NOTREADY: /* Umm...wait for it? Busy wait? */
      /** \todo ajcd 2012-10-23: Deadlock detection needs to know if any
          state changes to ready will happen. */
      multi_mutex_unlock(&task_mutex) ;
      HQFAIL("Join to constructing task") ;
      /** \todo ajcd 2011-06-30: Haven't decided what to do about this. It's
          possible that elaboration tasks might cause this state. */
      yield_processor() ;
      multi_mutex_lock(&task_mutex) ;
      break ;
    case RL_RUNNING:
      extended_thread_pool = begin_thread_extension() ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
      /* Note the recursive wait, we'll graph it in a different colour. */
      current->recursive = task_acquire(found.task) ;
#endif

      /** \todo ajcd 2011-07-17: If we do extend the thread pool, it's quite
          possible we'll just find the same task to wait on (umm...how? the
          extended thread should start at the interpreter group, and only
          look for ready or cancelled tasks. If it joins an ancestor of this
          task, which is quite possible, then it could end up waiting on the
          same task, which is probably why I had to change the signal to a
          broadcast when finishing the tasks. It sounds like that case
          could/would be better handled through more explicit dependencies.
          Explicit dependencies do have the problem that they may make
          task_predecessors() run through long recursion chains, because the
          dependencies are not ordered, and the join from frame end to a
          frame start may run through all bands on the way). That would be a
          complete waste of a thread. We should probably try to detect this
          case, and avoid waiting on the same task. Maybe we could pick any
          other task that's not a related to do. */
      TRACE_POINTLESS(SW_TRACE_POINTLESS_TIMEOUTS, pointless_timeouts,
                      join_timeouts) ;
      TRACE_POINTLESS(SW_TRACE_POINTLESS_WAKEUPS, pointless_wakeups,
                      join_wakeups) ;
      TRACE_POINTLESS(SW_TRACE_POINTLESS_GRAPH_WALKS, pointless_traverses,
                      join_traverses) ;
      if ( join_condvar_wait(taskcontext, group->type, 0) ) {
        MAYBE_POINTLESS(join_timeouts) ;
      }

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
      task_release_locked(&current->recursive) ;
#endif

      if ( extended_thread_pool )
        end_thread_extension() ;

      break ;
    default:
      HQFAIL("Joined task in unusual state") ;
    }
  }

  TRACE_POINTLESS(SW_TRACE_POINTLESS_GRAPH_WALKS, pointless_traverses,
                  join_traverses) ;

  /* Remove the mutual joiner/joinee references. The group should still have
     the reference passed into this function, and the task should still have
     an activation reference, so we can just decrement (and assert) the
     reference counts. This doesn't affect the runnability of the group's
     joiner, because it's the current task and is running. */
  DLL_REMOVE(group, joins_link) ;
  group->join = NULL ;
  HqAtomicDecrement(&current->refcount, after) ;
  HQASSERT(after > 0, "Join task should still have reference") ;
  HqAtomicDecrement(&group->refcount, after) ;
  HQASSERT(after > 0, "Group should still have reference") ;

  result = group->result ;
  if ( !result && errcontext != NULL ) {
    /** \todo ajcd 2011-02-10: detail error information would be transferred
        or augmented here. */
    /* We should not propagate NOT_AN_ERROR back to the interpreter task's
       current error context, because that indicates that an error has been
       signalled and handled in a recursive interpreter. We can propagate it
       back to a non-interpreter task, or to the interpreter task but in a
       non-current context, because in that case it indicates that the task
       was cancelled because it was unwanted, and the caller will handle
       this case. */
    HQASSERT(!IS_INTERPRETER() ||
             errcontext != CoreContext.error ||
             !(group->error.new_error == NOT_AN_ERROR ||
               group->error.old_error == NOT_AN_ERROR),
             "Shouldn't be propagating NOT_AN_ERROR") ;
    errcontext->new_error = group->error.new_error ;
    /** \todo ajcd 2011-02-10: Is this right? Should we be propagating the
        task's old error, or putting its new error in here? */
    if ( !errcontext->old_error )
      errcontext->old_error = group->error.old_error ;
  }

#ifdef DEBUG_BUILD
  /* Just in case we didn't recompute the schedule. */
  if ( (debug_tasks & DEBUG_TASKS_GRAPH_JOIN) != 0 )
    debug_schedule_graph = 0 ;
  group->being_joined = FALSE ;
#endif

  /* Remove joined sub-groups from their parents. */
  group_mark_joined(group) ;

  multi_mutex_unlock(&task_mutex) ;

  probe_end(SW_TRACE_TASK_JOINING, (intptr_t)group->type) ;
#undef return

  return result ;
}

Bool task_cancelling(void)
{
  task_t *current ;

  /* Use the existence of the task pool as a proxy for the task system
     being active. */
  if ( mm_pool_task == NULL )
    return FALSE ;

  current = task_current_uncounted_unchecked();
  if ( current == NULL )
    return FALSE;

  VERIFY_OBJECT(current, TASK_NAME);
  return !current->result ;
}

/****************************************************************************/
/** \brief End of the task specialiser chain.

    This function calls the task worker, then cleans up afterwards. */
void task_specialise_done(corecontext_t *context,
                          void *args,
                          task_specialise_private *data)
{
  task_context_t *taskcontext ;
  task_t *task ;
  task_successor_t succ ;
  error_context_t error_init = ERROR_CONTEXT_INIT;

  UNUSED_PARAM(void *, args) ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

  HQASSERT(context != NULL, "No core context") ;
  HQASSERT(context == get_core_context(),
           "Task specialiser changed core context location") ;
  taskcontext = context->taskcontext ;
  VERIFY_OBJECT(taskcontext, TASK_CONTEXT_NAME) ;

  VERIFY_OBJECT(data, TASK_SPECIALISE_NAME) ;
  task = data->task ;
  succ = data->succ ;
#ifdef ASSERT_BUILD
  data->assert_count++ ;
#endif
  UNNAME_OBJECT(data) ;

  do {
    task_t *current_task ;
    task_t *reserved = NULL ;

#if defined(DEBUG_BUILD)
    set_thread_name(task_names[task->trace_id]) ;
#endif

    /* The only ways we can get here are either:

       1) Via task_run(). In this case, either task_run() was called from
          the top-level thread function task_dispatch(), or it was a recursive
          call through task_recursive().
       2) Repeating this loop, because we finalised the last dependee of a
          task with the same specialiser, group, and attributes.
       3) Repeating this loop, because the next important task to do
          coincidentally has the same specialiser and attributes.

       In either case, task_activate_locked() is called on the task before
       passing it back here. task_activate_locked() changes the task state to
       TASK_RUNNING or TASK_FINALISING, and takes a reference to the task.
       The only transition that the task can make from TASK_RUNNING is to
       TASK_CANCELLING.
    */
    HQASSERT(task->state == TASK_RUNNING ||
             task->state == TASK_FINALISING ||
             task->state == TASK_CANCELLING, "Running task in invalid state") ;

    /* Each task or finaliser starts with a clear error context. */
    *context->error = error_init;

    /* Save the current task and set this as the task */
    current_task = taskcontext->current_task ;
    taskcontext->current_task = task ;

    /* Now finalise the task. The reserve pointer is used to capture the next
       ready task working on the same data. */
    PROBE(task->trace_id, task,
          task_execute(context, task, succ, &reserved)) ;

    /* Restore the previous task and error state. */
    taskcontext->current_task = current_task ;

    /* If we are joining a group, and did not find a dependent to run that
       contributes to completing the group, then we are done. If we are not
       joining and couldn't find a dependent to run, we'll see if we can find
       another ready task with the same task specialisation as this one. */
    if ( reserved == NULL && (succ & SUCC_SPEC) != 0 ) {
      multi_mutex_lock(&task_mutex) ;

      /* If doing a recursive task, we don't incremented threads_scheduled
         before calling this function. But we're not doing a recursive task,
         or joining would be non-NULL, so we need to decrement it to account
         for completing the task. */
      --threads_scheduled ;
      DECREMENT_THREADS_ACTIVE() ;

      /* Can we re-use the specialisation setup for the next task? */
      if ( !rip_is_quitting() &&
           threads_scheduled < thread_limit_current &&
           threads_scheduled < thread_limit_max &&
           tasks_runnable_helpable + tasks_runnable_nothelpable + tasks_ready_unprovisioned > 0 ) {
        task_find_t found = { NULL, RL_DISPATCHABLE, NULL, NULL } ;

        found.specialiser = task->specialiser ;
        found.specialiser_args = task->specialiser_args ;

        if ( find_runnable_task(&found, NULL) ) {
          reserved = found.task ;
          task_activate_locked(reserved) ;
          /* Indicate that a task is scheduled before we loop, so the
             low-memory handler doesn't think it can constrain to one
             thread. It is the job of task_specialise_done() (via
             task_run) to decrement this before picking up another
             task. */
          ++threads_scheduled ;
          INCREMENT_THREADS_ACTIVE() ;
        }
        /* else we couldn't find a dispatchable task, because any tasks
           that are ready don't have resources provisioned for their
           groups. */

        /* There may be other dispatchable tasks we didn't get to in the
           graph walk. */
        wake_next_thread(NULL, NULL, 0) ;
      }
      multi_mutex_unlock(&task_mutex) ;
    }

    /* Account for task_activate_locked() reference. */
    task_release(&task) ;

    /* Transfer the task reserved by task_execute() or
       task_group_predecessors() to this thread. */
    task = reserved ;
  } while ( task != NULL ) ;
}

/** \brief Internal function to call the task specialiser, trampolining through
    to the task worker function with a suitably-specialised context. */
static void task_run(corecontext_t *context, task_t *task,
                     task_successor_t succ)
{
  /* Re-specialise for each different task type. */
  corecontext_t local_context ;
  task_specialise_private data ;

  /* Copy the thread context, rather than the current context. We want the
     base context for task specialisation to be the same for each task, not
     the context as specialised for a different recursive task. Keep the
     current task context and error context in the copied context. */
  HQASSERT(context != NULL, "No core context") ;
  VERIFY_OBJECT(context->taskcontext, TASK_CONTEXT_NAME) ;
  HQASSERT(context->taskcontext->thread_context != NULL,
           "No thread context when running task") ;
  local_context = *context->taskcontext->thread_context ;
  local_context.taskcontext = context->taskcontext ;
  local_context.error = context->error ;

  /* No task is an interpreter task unless explicitly marked as such. */
  local_context.is_interpreter = FALSE ;

  data.task = task ;
  data.succ = succ ;
#ifdef ASSERT_BUILD
  data.assert_count = 0 ;
#endif
  NAME_OBJECT(&data, TASK_SPECIALISE_NAME) ;

  set_core_context(&local_context) ;

  /* This may run several tasks in succession, with this same
     specialisation. */
  task->specialiser(&local_context, task->specialiser_args, &data) ;

  HQASSERT(data.assert_count == 1,
           "Task's specialiser function did not complete correctly") ;

  set_core_context(context) ;
}

/** \brief Task dispatch loop.

    This is the context-specialised worker function spawned by the pool
    thread worker function. */
static void task_dispatch(corecontext_t *thread_context, void *arg)
{
  task_context_t *taskcontext = thread_context->taskcontext ;
#ifdef PROBE_BUILD
  int dispatch_wakeups = 0 ;
#endif

  UNUSED_PARAM(void *, arg);
  HQASSERT(arg == NULL, "Unexpected function arg");
  VERIFY_OBJECT(taskcontext, TASK_CONTEXT_NAME) ;

  multi_mutex_lock(&task_mutex) ;

  while ( !rip_is_quitting() ) {
    /* If we extended the thread pool because of blocked threads when waiting
       for other tasks, we let this thread go back to sleep by falling through
       to the condition wait. */
    if ( threads_scheduled < thread_limit_current &&
         threads_scheduled < thread_limit_max ) {
      task_generation_t new_dispatch_generation = group_resource_generation +
        task_helpable_generation + task_nothelpable_generation +
        task_unprovisioned_generation ;
      task_find_t found = { NULL, RL_DISPATCHABLE, NULL, NULL } ;

      if ( tasks_runnable_helpable + tasks_runnable_nothelpable > 0 ) {
        find_scheduled_task(&found) ;
        HQASSERT(found.task != NULL, "Didn't find a task to dispatch") ;
        /* If there's nothing dispatchable, see if there's anything
           unprovisioned that could be made dispatchable. */
      } else if ( tasks_ready_unprovisioned > 0 &&
                  dispatch_generation != new_dispatch_generation ) {
        if ( find_runnable_task(&found, NULL) ) {
          HQASSERT(found.task != NULL, "Didn't find a task to dispatch") ;
        } else {
          dispatch_generation = new_dispatch_generation ;
          probe_other(SW_TRACE_POINTLESS_GRAPH_WALKS, SW_TRACETYPE_VALUE,
                      ++pointless_traverses) ;
        }
      }

      if ( found.task != NULL ) {
        task_activate_locked(found.task) ;
        /* Indicate that a task is scheduled before we call task_run, so the
           low-memory handler doesn't think it can constrain to one thread.
           It is the job of task_specialise_done() (via task_run) to
           decrement this before picking up another task. */
        ++threads_scheduled ;
        INCREMENT_THREADS_ACTIVE() ;

        /* There may be other dispatchable tasks we didn't get to in the
           graph walk. Do the wakeup after incrementing threads_scheduled, so
           we take into account the task we're running on this thread. */
        wake_next_thread(NULL, NULL, 0) ;

        multi_mutex_unlock(&task_mutex) ;
        task_run(thread_context, found.task, SUCC_GROUP|SUCC_SPEC) ;
        multi_mutex_lock(&task_mutex) ;

        WASNT_POINTLESS(dispatch_wakeups) ;
        continue ;
      }
    }

    TRACE_POINTLESS(SW_TRACE_POINTLESS_WAKEUPS, pointless_wakeups,
                    dispatch_wakeups) ;

#if defined(DEBUG_BUILD)
    set_thread_name("Thread pool acquire") ;
#endif

    taskcontext->state = THREAD_WAIT_DISPATCH ;
    PROBE(SW_TRACE_TASK_DISPATCH_WAIT, -1,
          multi_condvar_wait_raw(&taskcontext->cond)) ;
    taskcontext->state = THREAD_RUNNING ;

    MAYBE_POINTLESS(dispatch_wakeups) ;
  }
  multi_mutex_unlock(&task_mutex) ;
}

/** \brief Helper function for \c task_group_join() and \c task_helper_locked()
    to run a task in a recursive context on the current thread. */
static void task_recursive(/*@notnull@*/ corecontext_t *context,
                           /*@notnull@*/ task_t *current,
                           /*@notnull@*/ task_t *ready,
                           task_successor_t succ)
{
  error_context_t *olderror, error = ERROR_CONTEXT_INIT ;

  VERIFY_OBJECT(current, TASK_NAME) ;
  VERIFY_OBJECT(ready, TASK_NAME) ;

  UNUSED_PARAM(task_t *, current) ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task system not locked for recursive task") ;

  olderror = context->error ;
  context->error = &error ;

  /* This function makes the task ready, and takes an extra reference on it,
     so it doesn't disappear whilst we're running it. */
  task_activate_locked(ready) ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  /* Note the recursive activation so we can include it in graphs. */
  current->recursive = task_acquire(ready) ;
#endif

  multi_mutex_unlock(&task_mutex) ;
  task_run(context, ready, succ) ;
  multi_mutex_lock(&task_mutex) ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  /* Remove recursive activation debug record and reset current thread name. */
  task_release_locked(&current->recursive) ;
#endif
#ifdef DEBUG_BUILD
  set_thread_name(task_names[current->trace_id]) ;
#endif

  context->error = olderror ;
}

/****************************************************************************/

unsigned int RIPCALL SwThreadIndex(void)
{
  corecontext_t *context = get_core_context() ;

  if ( context )
    return context->thread_index ;

  return 0 ;
}

/****************************************************************************/

static pthread_t threads[NTHREADS_LIMIT];

/* Mutex and condition var to make sure context localisation is complete
   before thread starts. We use the raw pthreads implementation for this
   because we don't want the thread spawn to have to have a lock rank, and
   also we're in the internals of the multi-thread implementation compound,
   so we have direct access to these. */
static multi_condvar_t spawn_condition ;
static multi_mutex_t spawn_mutex ;

/* At the moment, we only allow one spawner at once. This is OK until we
   start using threads for more general worker functions, until then the
   interpreter thread is the only one that spawns other threads. */
static Bool spawn_done ;

#define CONTEXT_SPECIALISER_NAME "Thread context specialiser"

struct context_specialise_private {
  context_specialiser_t *head ;       /**< Specialisers still to be called */
  corecontext_t *parent_context ;     /**< The parent context, to copy from */
  Bool doing_spawn;                   /**< Function is to be called in a new thread. */
  void (*thread_fn)(corecontext_t *context, void *) ; /**< The thread function. */
  void* thread_fn_arg;                /**< Context pointer for the function. */

  OBJECT_NAME_MEMBER
} ;

static context_specialiser_t *all_specialisers ;

/* Thread context localisation. */
void context_specialise_add(context_specialiser_t *element)
{
  HQASSERT(element, "No context specialiser element") ;
  HQASSERT(element->fn != NULL, "No context specialiser element function") ;
  HQASSERT(element->next == NULL, "Context specialiser next will be overwritten") ;
  HQASSERT(thread_total_started == 0,
           "Cannot add context specialiser after thread spawned") ;

  element->next = all_specialisers ;
  all_specialisers = element ;
}

/* Call down the specialiser chain, eventually calling the worker function. */
void context_specialise_next(corecontext_t *context,
                             context_specialise_private *data)
{
  context_specialiser_fn *specialise_fn ;

  HQASSERT(context != NULL, "No core context") ;
  HQASSERT(context == get_core_context(),
           "Thread specialiser changed core context location") ;
  VERIFY_OBJECT(context->taskcontext, TASK_CONTEXT_NAME) ;
  HQASSERT(context->taskcontext->thread_context == context,
           "Thread specialiser changed task context's initial thread context") ;
  VERIFY_OBJECT(data, CONTEXT_SPECIALISER_NAME) ;

  if ( data->head == NULL ) {
    void (*thread_fn)(corecontext_t *context, void *) = data->thread_fn ;

    /* We're at the end of the specialiser chain. Indicate we've finished
       spawning, release the waiting parent thread, and run the task dispatch
       loop. */
    UNNAME_OBJECT(data) ;

    if (data->doing_spawn) {
      multi_mutex_lock(&spawn_mutex) ;
      spawn_done = TRUE ;
      multi_condvar_signal(&spawn_condition) ;
      multi_mutex_unlock(&spawn_mutex) ;
    }

    (*thread_fn)(context, data->thread_fn_arg) ;
    return ;
  }

  /* Update the specialiser tracking structure to point at the next specialiser
     before calling the current one. */
  specialise_fn = data->head->fn ;
  data->head = data->head->next ;

  /* Should optimise to a tail call. */
  (*specialise_fn)(context, data) ;
}

/****************************************************************************/

/* Call a RIP function with a RIP thread context. */
void task_call_with_context(
  void (*func)(corecontext_t*, void*),
  void* arg)
{
  context_specialise_private specialise;
  corecontext_t core_context = { 0 };
  error_context_t error_context = { 0 };
  task_context_t task_context;
  corecontext_t* context;

  /* Use existing context if one exists, else manufacture one */
  context = get_core_context();
  if (context == NULL) {
    /* Set up core and task contexts */
    task_context.thread_context = &core_context;
    task_context.current_task = NULL;
    /* No condvar for non-core threads. */
    {
    pthread_cond_t cond_init = PTHREAD_COND_INITIALIZER ;
    task_context.cond.pcond = cond_init ;
    }
    task_context.cond.mutex = NULL ;
    task_context.cond.refcount = 0 ;
#ifdef PROBE_BUILD
    task_context.cond.wait_trace = SW_TRACE_INVALID ;
#endif
    DLL_RESET_LINK(&task_context, thread_list) ;
    task_context.state = THREAD_RUNNING ;
    NAME_OBJECT(&task_context, TASK_CONTEXT_NAME);

    core_context.taskcontext = &task_context;
    core_context.error = &error_context;
    core_context.is_interpreter = FALSE;
    /* Last thread id reserved for out of core threads that need a context */
    core_context.thread_index = NTHREADS_LIMIT - 1;

    context = &core_context;

    set_core_context(context);

    specialise.head = all_specialisers;
  } else {
    HQASSERT(context->taskcontext != NULL &&
             context->taskcontext->current_task != NULL &&
             context->taskcontext->state == THREAD_RUNNING,
             "Task called with context in invalid state") ;
    specialise.head = NULL;
  }

  specialise.parent_context = NULL;
  specialise.doing_spawn = FALSE;
  specialise.thread_fn = func;
  specialise.thread_fn_arg = arg;
  NAME_OBJECT(&specialise, CONTEXT_SPECIALISER_NAME);

  context_specialise_next(context, &specialise);

  if (context == &core_context) {
    clear_core_context();
    UNNAME_OBJECT(&task_context);
  }
}

/** \brief Generic thread initialisation with context localisation.

    This should always be the top-level function called by a new
    pthread_create(). It copies the parts of the core context that we know
    need to be specialised for every new thread, stores the new context in
    thread-local storage, and starts calling down the module and task
    specialiser call chain. */
static void *pool_thread_init(void *arg)
{
  corecontext_t thread_context ;
  error_context_t error = ERROR_CONTEXT_INIT ;
  task_context_t task_context ;
  context_specialise_private *specialise = arg ;

  VERIFY_OBJECT(specialise, CONTEXT_SPECIALISER_NAME) ;

  thread_context = *specialise->parent_context ;

  task_context.thread_context = &thread_context ;
  task_context.current_task = NULL ;
  multi_condvar_init(&task_context.cond, &task_mutex, SW_TRACE_INVALID) ;
  DLL_RESET_LINK(&task_context, thread_list) ;
  task_context.state = THREAD_RUNNING ;
  NAME_OBJECT(&task_context, TASK_CONTEXT_NAME) ;

  thread_context.taskcontext = &task_context ;
  thread_context.error = &error ;

  /* By default, all pool thread tasks are not interpreter. */
  thread_context.is_interpreter = FALSE ;

  thread_context.thread_index = (unsigned int)++thread_total_started ;
  HQASSERT(thread_total_started < NTHREADS_LIMIT, "Too many threads") ;

#if defined(DEBUG_BUILD)
  set_thread_name("Thread pool initialise") ;
#endif

  set_core_context(&thread_context) ;

  multi_mutex_lock(&task_mutex) ;
  DLL_ADD_BEFORE(&interpreter_task_context, &task_context, thread_list) ;
  multi_mutex_unlock(&task_mutex) ;

  PROBE(SW_TRACE_THREAD, (intptr_t)thread_context.thread_index,
        context_specialise_next(&thread_context, specialise)) ;

  multi_mutex_lock(&task_mutex) ;
  DLL_REMOVE(&task_context, thread_list) ;
  multi_mutex_unlock(&task_mutex) ;

  multi_condvar_finish(&task_context.cond) ;
  UNNAME_OBJECT(&task_context) ;

  return NULL ;
}

/** Generic thread spawning with context localisation.

    Set up a specialiser chain and spawn a new thread, waiting for the context
    initialisers to finish before continuing the current thread. If the
    specialisers are used correctly, this assures there are no data races
    copying data from the parent context.

    \note It would be nicer to be able to spawn multiple threads and then
    wait for them all to specialise at once, but we couldn't use a single
    stack-based structure to pass the arguments through.
*/
static Bool pool_thread_spawn(/*@out@*/ pthread_t *thread,
                              /*@in@*/ pthread_attr_t *attr,
                              /*@in@*/ corecontext_t *context,
                              void (/*@in@*/ /*@notnull@*/ *thread_fn)(corecontext_t *, void *))
{
  context_specialise_private pool_init_args ;

  /* Prepare for the thread to call down the specialiser chain. */
  pool_init_args.head = all_specialisers ;
  pool_init_args.parent_context = context ;
  pool_init_args.doing_spawn = TRUE ;
  pool_init_args.thread_fn = thread_fn ;
  pool_init_args.thread_fn_arg = NULL ;

  NAME_OBJECT(&pool_init_args, CONTEXT_SPECIALISER_NAME) ;

  /* Indicate we're waiting for the thread to finish spawning */
  multi_mutex_lock(&spawn_mutex) ;
  HQASSERT(spawn_done,
           "Shouldn't be starting new thread whilst existing spawn active") ;
  spawn_done = FALSE ;
  multi_mutex_unlock(&spawn_mutex) ;

  if (pthread_create(thread, attr, pool_thread_init, &pool_init_args) != 0)
    return FALSE ; /* Failed to create thread, so don't need to wait for it */

  /* Wait for spawn to complete. This has two purposes; one is to ensure the
     specialisers have run, so the current thread can go ahead and modify its
     context. The second purpose is because the pool_init_args are in a
     local stack frame, and we need to wait until they've been fully used
     before exiting that frame. */
  multi_mutex_lock(&spawn_mutex) ;
  /* The condition wait is inside a loop because spurious wakeups may occur
     through interrupts. */
  while ( !spawn_done )
    multi_condvar_wait_raw(&spawn_condition) ;
  multi_mutex_unlock(&spawn_mutex) ;

  return TRUE ;
}

/*****************************************************************************/

Bool multi_constrain_to_single(void)
{
  Bool result = FALSE ;
  Bool unlock = FALSE ;
  int res ;

  /** \todo ajcd 2011-09-19: We use the pthreads trylock call directly because
      we need to see the result. Work out a way of doing this that doesn't
      break the lock checking. */
  res = pthread_mutex_trylock(&task_mutex.pmutex) ;
  switch ( res ) {
  case 0: /* Just acquired lock */
    unlock = TRUE ;
    /*@fallthrough@*/
  case EDEADLK: /* Already have lock. */
    /* Less-than or equal test in case called before tasks_swstart. */
    if ( threads_scheduled <= 1 ) {
      /* Use an atomic operation to interleave with multi_condvar_wait_task()
         safely. If this succeeds, thread_limit_current will be 0 and result
         will be TRUE, so we just need to increment the semaphore. */
      HqAtomicCAS(&thread_limit_current, thread_limit_active, 0, result) ;
      if ( result ) {
        hq_atomic_counter_t before ;
        /* There are no threads running, and we're not in a genuine condvar
           wait, so we can constrain to just the interpreter thread. */
        HqAtomicIncrement(&thread_pool_constrained, before) ;
      }
    }
    if ( unlock ) {
      res = pthread_mutex_unlock(&task_mutex.pmutex) ;
      HQASSERT(res == 0, "Failed to unlock task mutex") ;
    }
    break ;
  case EBUSY: /* Another thread has the lock */
    break ;
  default:
    HQFAIL("Unexpected result from pthread trylock") ;
  }

  return result ;
}

void multi_unconstrain_to_single(void)
{
  hq_atomic_counter_t after ;

  HqAtomicDecrement(&thread_pool_constrained, after) ;
  HQASSERT(after >= 0, "Thread pool wasn't constrained") ;
  if ( after == 0 ) {
    /* Now allow other threads to run again. We don't need an atomic
       operation here because there is no way that another thread can change
       the values: there are no other threads running. */
    thread_limit_current = thread_limit_active = request_limit_active ;
    /* Other threads may have diverted to waiting on the task_dispatch_cond
       condvar because of the constraint. If there were tasks ready to do
       before the constraint, we need to signal them to start now the
       constraint is lifted. */
    wake_next_thread(NULL, NULL, 0) ;
  }
}

/****************************************************************************/

void interpreter_task_specialise(corecontext_t *context,
                                 void *args,
                                 task_specialise_private *data)
{
  HQASSERT(mm_pool_task, "Task pool not active") ;

  /** \todo ajcd 2010-10-22: Make current context the same as the interpreter
      context at call time, rather than the original thread context. This is
      only used if we implement immediate-mode task_do() calls (make ready
      and wait for a task in one go, such that it can be implemented by a
      recursive call).

      Unfortunately, this may already be the case, and we really need to
      re-specialise the context twice at startup, so we can store a pristine
      copy of the thread context. */
  task_specialise_done(context, args, data) ;
}

/****************************************************************************/
/* Task vectors */

static inline size_t task_vector_size(unsigned int length)
{
  return offsetof(task_vector_t, element[0]) +
    length * (offsetof(task_vector_t, element[1]) - offsetof(task_vector_t, element[0])) ;
}

Bool task_vector_create(task_vector_t **newvec, unsigned int length)
{
  task_vector_t *vector ;
  size_t size = task_vector_size(length) ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

  if ( (vector = mm_alloc(mm_pool_task, size, MM_ALLOC_CLASS_TASK_VECTOR)) == NULL ) {
    return error_handler(VMERROR) ;
  }

  HqMemZero(vector, size) ;

  vector->length = length ;
  vector->refcount = 1 ;

  *newvec = vector ;

  return (vector != NULL) ;
}

task_vector_t *task_vector_acquire(task_vector_t *vector)
{
  hq_atomic_counter_t before ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(vector, "No task vector") ;

  HqAtomicIncrement(&vector->refcount, before) ;
  HQASSERT(before > 0, "Task vector was previously released") ;

  return vector ;
}

void task_vector_release(task_vector_t **vectorp)
{
  task_vector_t *vector ;
  hq_atomic_counter_t after ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(vectorp, "Nowhere to get task vector from") ;
  vector = *vectorp ;
  HQASSERT(vector, "No task vector") ;
  *vectorp = NULL ;

  multi_mutex_lock(&task_mutex) ;
  HQASSERT(vector->refcount > 0, "Task vector already released") ;
  HqAtomicDecrement(&vector->refcount, after) ;
  if ( after == 0 ) {
    unsigned int i ;

    for ( i = 0 ; i < vector->length ; ++i ) {
      if ( vector->element[i].task != NULL )
        task_release_locked(&vector->element[i].task) ;
    }

    mm_free(mm_pool_task, vector, task_vector_size(vector->length)) ;
  }
  multi_mutex_unlock(&task_mutex) ;
}

unsigned int task_vector_length(const task_vector_t *vector)
{
  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(vector, "No task vector") ;
  HQASSERT(vector->refcount > 0, "Task vector was previously released") ;
  return vector->length ;
}

void task_vector_store(task_vector_t *vector, unsigned int slot,
                       task_t *task, void *args)
{
  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(vector, "No task vector") ;
  HQASSERT(slot < vector->length, "Slot index out of range") ;
  HQASSERT(vector->refcount > 0, "Task vector was previously released") ;

  multi_mutex_lock(&task_mutex) ;

  if ( task != NULL ) {
    hq_atomic_counter_t before ;
    VERIFY_OBJECT(task, TASK_NAME) ;
    HqAtomicIncrement(&task->refcount, before) ;
    HQASSERT(before > 0, "Task was previously released") ;
  }

  if ( vector->element[slot].task != NULL )
    task_release_locked(&vector->element[slot].task) ;

  vector->element[slot].task = task ;
  vector->element[slot].args = args ;

  multi_mutex_unlock(&task_mutex) ;
}

/*****************************************************************************/
/* Task resource lookups. */

Bool task_resource_fix_n(resource_type_t restype, unsigned int ntofix,
                         const resource_id_t ids[],
                         void *resources[], Bool hit[])
{
  task_t *current = task_current_uncounted() ;
  task_group_t *group = current->group ;
  unsigned int i, istart, j, jstart, nusable, noptimal, nfilled ;
  requirement_node_t *node, *lock ;
  resource_pool_t *pool ;
  resource_lookup_t *lookup ;
  resource_entry_t *entry ;
  Bool result = TRUE ;

  HQASSERT(current->state != TASK_FINALISING,
           "Task resources cannot be fixed in the cleanup function") ;
  HQASSERT(group->provisions == PROVISION_OK,
           "Resources not provisioned for group") ;
  node = group->reqnode ;
  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  VERIFY_OBJECT(node->requirement, RESOURCE_REQUIREMENT_NAME) ;
  HQASSERT(resource_requirement_is_ready(node->requirement),
           "Fixing task resource, but requirement is not ready") ;
  HQASSERT(ntofix > 0, "No resources to fix") ;

  for ( jstart = 0 ; jstart < ntofix ; ++jstart ) {
    /* Note that we initially set the resource pointer to the entry record,
       and later convert this to the entry's resource pointer. We remove any
       resources we find from the hash table, and try to re-insert them at
       the most optimal positions when we're done. */
    resources[jstart] = NULL ;
#ifdef ASSERT_BUILD
    HQASSERT(ids[jstart] != TASK_RESOURCE_ID_INVALID,
             "Cannot fix invalid task resource ID") ;
    for ( j = jstart + 1 ; j < ntofix ; ++j )
      HQASSERT(ids[jstart] != ids[j], "Resource IDs to fix must be unique") ;
#endif
  }

  requirement_node_lock(node->requirement, lock) ;

  pool = node->requirement->pools[restype] ;
  if ( pool == NULL ) {
    /* We shouldn't be fixing resources if they are not ever needed (i.e.,
       the maximum is zero). If we try fixing a resource which has a minimum
       of zero, we may discover that the pool has been removed. However,
       the resource must have been optional, so that's OK. */
    HQASSERT(node->minimum[restype] == 0 && node->maximum[restype] > 0,
             "No resource pool for required resource") ;
    result = FALSE ;
    goto unlock_node ;
  }

  /* Since this group has been provisioned, we have included the minimum for
     this group in pool->nprovided, and should be able to fulfill at least
     that many without problems. If we have already fixed the minimum number
     of resources for this group, then this resource is optional.

     If we have already unfixed resources, or have had free resources
     bequeathed to this group by child group joins, the resources owned by
     this group may not be the ideal resources. So, we'll look for the right
     resource, and if we don't own it but another group does, we may swap for
     that one instead. The full priority order is given by the distance enum
     variable:

     1) Same ID, owned by same group, and fixing, fixed or detached.
     2) Same ID, owned by any group, and free.
     3) Different ID, owned by any group, and free.

     We need to keep enough allocated resources available to start the
     minimum number of simultaneous groups required by the node. We translate
     the number we need to reserve into the maximum number or resources we
     can use out of the existing pool allocation, to make it easier to test
     whether we need to allocate a new entry or not.
  */

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  resource_lookup_lock(pool, lookup) ;
  HQASSERT(lookup != NULL, "No resource lookup for requirements") ;
#define return DO_NOT_return_GOTO_found_INSTEAD!

  {
    unsigned int smin ;

    /* Work out the maximum number of existing free resource entries that we
       can use. We shouldn't be using any more than the maximum we've been
       told we can take, beyond the number currently existing, or if we would
       prevent the minimum number of simultaneous groups from being
       provisioned. */
    nusable = min(pool->nresources, pool->maximum) ;
    if ( nusable > pool->nprovided + pool->ndetached ) {
      nusable -= pool->nprovided + pool->ndetached ;
    } else {
      nusable = 0 ;
    }

    /* We have to use limit_active_original here, because the max active
       threads can change, and we need to keep enough resources in reserve
       for any minimum number of groups possible. It is unlikely that a
       minimum limit will be specified using the number of threads, so this
       shouldn't be a problem. */
    smin = requirement_smin(node, limit_active_original) ;
    if ( smin > node->ngroups ) {
      unsigned int nreserve = node->minimum[restype] * (smin - node->ngroups) ;
      if ( nusable >= nreserve ) {
        nusable -= nreserve ;
      } else {
        HQFAIL("Not enough resources reserved for minimum number of groups") ;
        nusable = 0 ;
      }
    }

    /** \todo ajcd 2013-01-15: What about the minimum resources required by
        other groups at this time? They will have been factored into the
        pool's minimum, but there's nothing to prevent this group from using
        all of the available resources. We don't have access to the number of
        groups currently provisioned by each node in all requirements
        attached to the pool, so we don't know how many free entries to
        reserve for them. It's only because the current group structure
        doesn't use the same resource in different groups that we don't run
        into trouble. Maybe the pool should track the number of resources
        reserved for provisioning the minimum group levels? */

    HQASSERT(nusable <= pool->nresources - pool->nprovided - pool->ndetached,
             "Not enough resources in pool") ;

    if ( node->minimum[restype] > group->nfixed[restype] ) {
      /* We haven't finished using this group's pre-allocated resources. */
      nusable += node->minimum[restype] - group->nfixed[restype] ;
    }
  }

  /* The first pass tries to set optimal assignments using existing resource
     entries. It is structured as a pass over the lookup table, with a nested
     pass over the fix requests. At the end of each iteration of the outer
     loop, we will have assigned the resource from the lookup table to the
     best possible fix request. The lookup table search may be restarted with
     a different starting position if we find an optimal match for a fix
     request, so the same table entries may be considered multiple times.

     In order for this algorithm to terminate, the notion of a "better" match
     is a stable sort order on the entries. We only replace a selection if it
     is strictly better (not equal) in preference. The algorithm only
     restarts the outer or inner loops when a replacement has been made, and
     the number of replacements is finite, so it must terminate. (It will
     terminate when either all of the fix requests have optimal assignments
     or when a complete pass over the lookup table is done with no optimal
     assignments.) When a resource entry is assigned to a fix request, it is
     removed from the lookup table in order to avoid being considered for
     other requests. If an assignment is displaced by a better entry, the
     previously assigned entry is re-tested to determine if it is a better
     choice of a different fix request. Entries are re-inserted into the
     lookup table in a subsequent pass. This pass may assign more existing
     resource entries than we will ultimately be allowed to keep. Subsequent
     passes trim the set of entries we will keep. We're trying to do as much
     work as possible to find appropriate entries in one pass, so we don't
     need to iterate over the resource lookup table multiple times.

     We index lookup table entries using i and istart, fix resources and ids
     using j and jstart. */
  noptimal = nfilled = 0 ;
  jstart = 0 ;
 new_search:
  for ( i = istart = resource_lookup_first(lookup, ids[jstart]) ;; ) {
    if ( (entry = lookup->entries[i]) != NULL ) {
      unsigned int jindex ;

      /* If this entry is free and cannot match any request, and we've already
         found something that does match every request, it is of no possible
         use. */
      if ( entry->id == TASK_RESOURCE_ID_INVALID && nfilled == ntofix ) {
        HQASSERT(entry->state == TASK_RESOURCE_FREE,
                 "Entry with invalid resource ID is not free") ;
        goto next_entry ;
      }

    retry_entry:
      /* Search all fix requests to find if entry is better than the current
         best choice. */
      for ( j = jstart, jindex = UINT_MAX ;; ) {
        resource_entry_t *previous = resources[j] ;

        if ( previous == NULL || previous->state == TASK_RESOURCE_FREE ) {
          /* Previous assignment is not optimal. */
          if ( entry->id == ids[j] ) {
            if ( entry->state != TASK_RESOURCE_FREE ) {
              if ( entry->owner == group ) {
                /* entry is optimal, and it cannot match any other ids[] */
                lookup->entries[i] = previous ;
                resources[j] = entry ;
                entry = previous ;

                if ( ++noptimal == ntofix )
                  goto done_assignments ;

                /* Update the start of the search to a request that does not
                   yet have an optimal match (there must be at least one,
                   because we would just have jumped out if all the requests
                   were optimally matched). */
                do {
                  if ( ++jstart == ntofix )
                    jstart = 0 ;
                  previous = resources[jstart] ;
                } while ( previous != NULL &&
                          previous->state != TASK_RESOURCE_FREE ) ;

                /* If we did not have an entry stored before, restart the
                   lookup table search at the hash index corresponding to the
                   non-optimal resources[] assignment we just found. */
                if ( entry == NULL ) {
                  ++nfilled ;
                  goto new_search ;
                }

                /* If we had a non-optimal entry stored before, see if it
                   matches any request better than their existing entry. */
                goto retry_entry ;
              } else {
                /* Entry is in use by another group, and so cannot be used
                   for anything. */
                goto next_entry ;
              }
            } else if ( previous == NULL || previous->id != ids[j] ) {
              /* entry is better, and it cannot match any other ids[] */
              lookup->entries[i] = previous ;
              resources[j] = entry ;
              entry = previous ;

              /* This assignment is probably right, but we don't know that
                 for sure. There may be an optimal entry for this assignment,
                 so we have to keep searching the table just to be sure. */
              /** \todo ajcd 2013-01-18: We may want to reset jstart and
                  re-start the search as we do for optimal matches, so that
                  the next entry we consider is more likely to match a
                  different fix request. It's unclear if that would result in
                  faster termination on average. */
              if ( entry == NULL ) {
                ++nfilled ;
                goto next_entry ;
              }

              goto retry_entry ;
            } /* else entry is same as current assignment, and it cannot
                 match any other ids[], but could be usable for them as a
                 fallback, so keep searching. */
          } else {
            /* entry doesn't match ID. Anything we've already got is better. */
            if ( entry->state == TASK_RESOURCE_FREE && previous == NULL ) {
              jindex = j ;
              /* If this entry is a new entry, it cannot be better for any
                 other request than this one. */
              if ( entry->id == TASK_RESOURCE_ID_INVALID )
                break ;
            }
          }
        } else {
          /* Previous was optimal, so this entry can't be (we can only have
             one resource fixed with a particular ID per group). It also
             can't be the same entry as previous even though we allow table
             lookup restarts because we remove entries from the lookup table
             when we assign them to resources. This entry could match a
             different request, so keep searching. */
          HQASSERT(entry->state == TASK_RESOURCE_FREE ||
                   entry->id != ids[j] || entry->owner != group,
                   "More than one in-use resource entry matches fix request") ;
        }

        if ( ++j == ntofix )
          j = 0 ;
        if ( j == jstart )
          break ;
      }

      if ( jindex != UINT_MAX ) {
        /* This entry was a worse match than any existing assignments,
           however there was a request that we can use it as a fallback
           for. */
        HQASSERT(resources[jindex] == NULL,
                 "Fallback fix assignment already filled") ;
        lookup->entries[i] = NULL ;
        resources[jindex] = entry ;
        ++nfilled ;
      }
    } /* else no entry exists in this table slot */

  next_entry:
    if ( i == 0 )
      i = lookup->nentries ;
    if ( --i == istart )
      break ;
  }

  { /* At this point, we know the number of optimal resources we will use, we
       know the number of pre-existing resources we might use, so we can fail
       if the number fixed by this group would be too high or if the number
       we would need to allocate would be too high. */
    unsigned int needfree = ntofix - noptimal ;
    unsigned int needalloc = needfree > nusable ? needfree - nusable : 0 ;

    if ( group->nfixed[restype] + needfree > node->maximum[restype] ||
         (needalloc > 0 && pool->nresources + needalloc > pool->maximum) ) {
      result = FALSE ;
      goto done_assignments ;
    }
  }

  /* In the second pass, we'll try allocating new entries for any resources
     we failed to assign entries to and for any where we ran out of re-usable
     entries. */
  for ( j = 0 ; j < ntofix ; ++j ) {
    if ( (entry = resources[j]) != NULL ) {
      if ( entry->state == TASK_RESOURCE_FREE ) {
        /* Have we run out of pre-allocated resources we can use? */
        if ( nusable == 0 ) {
          /* We cannot re-use any more pre-allocated resources, which
             implies that the resource entry is optional, so the allocation
             cost is low. */
          if ( pool->nresources >= pool->maximum ||
               (entry = resource_entry_create(pool, mm_cost_easy, NULL)) == NULL ) {
            result = FALSE ;
            break ;
          }
          /* Re-insert previously selected resource into lookup table, then
             select the new resource entry. */
          resource_lookup_insert(lookup, resources[j]) ;
          resources[j] = entry ;
        } else if ( entry->id != ids[j] && hit ) {
          /* If the caller cares about caching (as indicated by them passing
             a cache hit array as a parameter), we will prefer to use
             resources with the same ID. If we can't get one with the same
             ID, but haven't maxed the pool, try to allocate a new entry
             rather than use one from the same group or another group. The
             new entry allocation uses the lowest cost possible, we don't
             want to throw away anything else to make space for this entry.
             Some pool alloc implementations may automatically refuse to
             allocate entries with this minimum cost, using it as a proxy to
             detect this case. */
          /** \todo ajcd 2013-01-19: Should we make the use case explicit as
              a parameter to resource_entry_create() and the pool alloc
              method, rather than detect the ID miss through the cost? */
          if ( pool->nresources >= pool->maximum ||
               (entry = resource_entry_create(pool, mm_cost_none, NULL)) == NULL ) {
            /* Failed to allocate new resource, so use selected entry. */
            --nusable ;
          } else {
            /* We allocated a new resource. See if the previously assigned
               resource can be used by any other fix request, and if not
               re-insert it into the lookup table. */
            for ( jstart = j + 1 ; jstart < ntofix ; ++jstart ) {
              if ( resources[jstart] == NULL ) {
                resources[jstart] = resources[j] ;
                goto reused_assignment ;
              }
            }
            /* No other request can use previously assigned resource, so
               re-insert it in the lookup table. */
            resource_lookup_insert(lookup, resources[j]) ;
          reused_assignment:
            resources[j] = entry ;
          }
        } else {
          /* Re-use this resource. */
          --nusable ;
        }
      } else {
        /* Previously fixed resource selected. */
        HQASSERT(entry->id == ids[j] && entry->owner == group,
                 "Previously suggested resource does not match ID and group") ;
      }
    } else {
      /* No resource entry selected. If we haven't maxed the pool, allocate a
         new entry. We didn't have an assigned resource beforehand, which
         implies that the entry is optional (or we'd have had a pre-allocated
         entry available), so the allocation cost is low. */
      if ( pool->nresources >= pool->maximum ||
           (entry = resource_entry_create(pool, mm_cost_easy, NULL)) == NULL ) {
        result = FALSE ;
        break ;
      }
      resources[j] = entry ;
    }
  }

 done_assignments:
  /* In the third pass, we convert entries to fixed or fixing, make them
     owned by the group, give them the right IDs, and re-insert them back
     into the lookup table. */
  for ( j = 0 ; j < ntofix ; ++j ) {
    if ( (entry = resources[j]) != NULL ) {
      if ( result ) {
        resource_entry_t *original ;

        if ( hit )
          hit[j] = (entry->id == ids[j]) ;

        entry->owner = group ;
        entry->id = ids[j] ;

        if ( entry->state == TASK_RESOURCE_DETACHED ) {
          entry->state = TASK_RESOURCE_FIXED ;
        } else if ( entry->state == TASK_RESOURCE_FREE ) {
          entry->state = TASK_RESOURCE_FIXME ;
          HQASSERT(group->nfixed[restype] < node->maximum[restype],
                   "Fixed too many resources in group") ;
          group->nfixed[restype]++ ;
          if ( group->nfixed[restype] > node->minimum[restype] )
            pool->nprovided++ ;
        } else {
          HQASSERT(entry->state == TASK_RESOURCE_FIXME ||
                   entry->state == TASK_RESOURCE_FIXING ||
                   entry->state == TASK_RESOURCE_FIXED,
                   "Resource entry state invalid") ;
        }

        /* If there's something at the location we want, move it aside.
           Rehashing like this ensures that subsequent fixes and unfixes are
           more likely to find in-use entries. */
        i = resource_lookup_first(lookup, ids[j]) ;
        if ( (original = lookup->entries[i]) != NULL ) {
          lookup->entries[i] = NULL ;
          resource_lookup_insert(lookup, entry) ;
          entry = original ;
        }
      } /* else !result */

      /* Re-insert the entry regardless whether we succeeded or not. */
      resource_lookup_insert(lookup, entry) ;
    }
  }

  verify_pool_entries(pool, lookup) ;

  /* We want to release the lookup lock before calling the pool's fix
     function, because fix may have to take other locks or interact with
     other parts of the RIP, and there may be lock ordering problems (e.g.,
     the framebuffer band's fix needs the PGB lock). However, when we do
     this, the entry becomes visible to many other routines, including any
     other task resource fix that runs. We need to fix the resource, but only
     once per ID change, so use the intermediate states of
     TASK_RESOURCE_FIXME and TASK_RESOURCE_FIXING to prevent the entry from
     being stolen by another task. We're relying on the memory barrier
     implicit in the spinlocks to flush this change to memory. */
  resource_lookup_unlock(pool, lookup) ;

 unlock_node:
  requirement_node_unlock(node->requirement, lock) ;

  /* The fourth pass ensures that all of the resources are fixed properly, by
     calling the pool's fix() method on any that have not been fixed. If
     another task in the group is simultaneously fixing resources, they may
     share the work of fixing the entry, with the loser waiting in a spinlock
     loop while the winner calls the method. */
  for ( j = 0 ; j < ntofix ; ++j ) {
    if ( (entry = resources[j]) != NULL ) {
      if ( result ) {
        if ( entry->state != TASK_RESOURCE_FIXED ) {
          Bool fix ;
          HqAtomicCAS(&entry->state, TASK_RESOURCE_FIXME, TASK_RESOURCE_FIXING, fix) ;
          HQASSERT(entry->state == TASK_RESOURCE_FIXING ||
                   entry->state == TASK_RESOURCE_FIXED,
                   "Invalid resource entry state while fixing") ;
          if ( fix ) {
            /* This thread won the race to fix this entry. */
            if ( pool->fix != NULL )
              pool->fix(pool, entry) ;
            entry->state = TASK_RESOURCE_FIXED ;
          } else {
            /* If we failed the CAS above, then another thread won a race to
               call pool->fix() on this entry. We'll just spin waiting until
               the resource is fixed. */
            for (;;) {
              HqAtomicCAS(&entry->state, TASK_RESOURCE_FIXED, TASK_RESOURCE_FIXED, fix) ;
              if ( fix )
                break ;
              yield_processor() ;
            }
          }
        }

        /* Convert from a resource entry to the entry's resource pointer. */
        resources[j] = entry->resource ;
        probe_other(SW_TRACE_RESOURCE_FIX, SW_TRACETYPE_MARK, entry->resource) ;
      } else { /* failed */
        if ( entry->state != TASK_RESOURCE_FIXED ) {
          resources[j] = NULL ;
        } else {
          /* Convert from a resource entry to the entry's resource pointer. */
          resources[j] = entry->resource ;
        }
      }
    }
  }

#undef return
  return result ;
}

void *task_resource_fix(resource_type_t restype, resource_id_t id, Bool *hit)
{
  void *resources[1] ;
  if ( !task_resource_fix_n(restype, 1, &id, resources, hit) )
    return NULL ;
  return resources[0] ;
}

void task_resource_unfix(resource_type_t restype, resource_id_t id, void *resource)
{
  task_t *current = task_current_uncounted() ;
  task_group_t *group = current->group ;
  requirement_node_t *node ;
  resource_pool_t *pool ;
  resource_lookup_t *lookup ;
  unsigned int i, start ;

  HQASSERT(group->provisions == PROVISION_OK,
           "Resources not provisioned for group") ;
  node = group->reqnode ;
  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  VERIFY_OBJECT(node->requirement, RESOURCE_REQUIREMENT_NAME) ;
  HQASSERT(resource_requirement_is_ready(node->requirement),
           "Unfixing task resource, but requirement is not ready") ;

  /* Pool cannot change or disappear before this call, because the
     requirement is ready, and setmin/setmax cannot reduce the pool below the
     level of the provisioned resources. */
  pool = node->requirement->pools[restype] ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  HQASSERT(resource != NULL, "Unfixing NULL resource") ;

  /* We could use resource_pool_forall() for this, but that wouldn't
     optimise as well because of the function callback.

     Search for entry from the ID hash. We search backwards to make the
     wraparound test easier. We need to protect the search against the
     possibility that another task in the same group is simultaneously
     modifying entries. */
  resource_lookup_lock(pool, lookup) ;
  HQASSERT(lookup != NULL, "No resource lookup for requirement") ;
#define return DO_NOT_return_GOTO_found_INSTEAD!

  start = resource_lookup_first(lookup, id) ;
  for ( i = start ;; ) {
    resource_entry_t *entry ;

    if ( (entry = lookup->entries[i]) != NULL ) {
      if ( entry->resource == resource ) {
        if ( entry->owner == group ) {
          /* When explicitly unfixing an entry, we push it back to the parent
             group, because it's quite likely that a sibling of this task group
             may want to use the same resource type using the same ID. If we
             wanted to retain it for another task in the same group, we can
             just avoid explicitly unfixing it, and let the group destruction
             deprovision the group on join. */
          HQASSERT(entry->state == TASK_RESOURCE_FIXED ||
                   entry->state == TASK_RESOURCE_DETACHED,
                   "Resource was not fixed or detached") ;
          HQASSERT(group->nfixed[restype] > 0,
                   "Unfixed too many resources in group") ;
          probe_other(SW_TRACE_RESOURCE_UNFIX, SW_TRACETYPE_MARK, entry->resource) ;
          entry->state = TASK_RESOURCE_FREE ;
          if ( group->nfixed[restype]-- > node->minimum[restype] )
            pool->nprovided-- ;
        } else if ( entry->owner == pool ) {
          HQASSERT(entry->state == TASK_RESOURCE_DETACHED,
                   "Resource was not detached") ;
          /* The task group owning the resource has terminated, leaving the
             resource owned by the pool. It was counted as detached when the
             group was deprovisioned, we need to update that count. */
          probe_other(SW_TRACE_RESOURCE_RETURN, SW_TRACETYPE_MARK, entry->resource) ;
          --pool->ndetached ;
          entry->state = TASK_RESOURCE_FREE ;
          entry->owner = NULL ;
        }
        if ( !pool->cache_unfixed )
          entry->id = TASK_RESOURCE_ID_INVALID ;
        break ;
      }
    }
    if ( i == 0 )
      i = lookup->nentries ;
    if ( --i == start ) {
      HQFAIL("Did not find resource to unfix") ;
      break ;
    }
  }
  resource_lookup_unlock(pool, lookup) ;
#undef return
}

void task_resource_detach(resource_type_t restype, resource_id_t id,
                          void *resource)
{
  task_t *current = task_current_uncounted() ;
  task_group_t *group = current->group ;
  requirement_node_t *node ;
  resource_pool_t *pool ;
  resource_lookup_t *lookup ;
  unsigned int i, start ;

  HQASSERT(group->provisions == PROVISION_OK,
           "Resources not provisioned for group") ;
  node = group->reqnode ;
  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  VERIFY_OBJECT(node->requirement, RESOURCE_REQUIREMENT_NAME) ;
  HQASSERT(resource_requirement_is_ready(node->requirement),
           "Detaching task resource, but requirement is not ready") ;

  /* Pool cannot change or disappear before this call, because the
     requirement is ready, and setmin/setmax cannot reduce the pool below the
     level of the provisioned resources. */
  pool = node->requirement->pools[restype] ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  HQASSERT(resource != NULL, "Detaching NULL resource") ;

  /* We could use resource_pool_forall() for this, but that wouldn't
     optimise as well because of the function callback.

     Search for entry from the ID hash. We search backwards to make the
     wraparound test easier. We need to protect the search against the
     possibility that another task in the same group is simultaneously
     modifying entries. */
  resource_lookup_lock(pool, lookup) ;
  HQASSERT(lookup != NULL, "No resource lookup for requirement") ;
#define return DO_NOT_return_GOTO_found_INSTEAD!

  start = resource_lookup_first(lookup, id) ;
  for ( i = start ;; ) {
    resource_entry_t *entry ;

    if ( (entry = lookup->entries[i]) != NULL ) {
      if ( entry->owner == group && entry->resource == resource ) {
        HQASSERT(entry->state == TASK_RESOURCE_FIXED,
                 "Resource was not fixed") ;
        probe_other(SW_TRACE_RESOURCE_DETACH, SW_TRACETYPE_MARK, entry->resource) ;
        entry->state = TASK_RESOURCE_DETACHED ;
        break ;
      }
    }
    if ( i == 0 )
      i = lookup->nentries ;
    if ( --i == start ) {
      HQFAIL("Did not find resource to detach") ;
      break ;
    }
  }
  resource_lookup_unlock(pool, lookup) ;
#undef return
}

/****************************************************************************/

static NAMETYPEMATCH tasks_match[] = {
  { NAME_RendererThreads | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MultiThreadedCompositing | OOPTIONAL, 1, { OINTEGER }}, /* Ignored */
  { NAME_MaxThreads | OOPTIONAL, 2, { OINTEGER, OARRAY }},
  { NAME_MaxThreadsLimit | OOPTIONAL, 1, { OINTEGER }},
  /* These parameters are write-only parameters for tuning experiments.
     They need to be in timing builds because they're controls for
     performance testing, which is why debug ripvars are not used. */
  { NAME_TaskJoinWaitMilliseconds | OOPTIONAL, 1, { OINTEGER }},
  { NAME_TaskHelperWaitMilliseconds | OOPTIONAL, 1, { OINTEGER }},
  { NAME_TaskHelperWaitThreshold | OOPTIONAL, 1, { OINTEGER }},
  { NAME_TaskHelperStartThreshold | OOPTIONAL, 1, { OINTEGER }},
  { NAME_TaskHelperEndThreshold | OOPTIONAL, 1, { OINTEGER }},
  DUMMY_END_MATCH
} ;


static Bool tasks_get_systemparam(corecontext_t *context, uint16 nameid, OBJECT *result)
{
  UNUSED_PARAM(corecontext_t *, context) ;

  HQASSERT(result, "No object for systemparam result") ;

  switch (nameid) {
  case NAME_RendererThreads:
    object_store_integer(result, CAST_SIGNED_TO_INT32(thread_limit_active)) ;
    break;
  case NAME_MaxThreads: {
    static OBJECT mt[2];

    /* Static array must be marked as a NOTVM array. */
    object_store_integer(object_slot_notvm(&mt[0]), thread_limit_active);
    object_store_integer(object_slot_notvm(&mt[1]), thread_limit_max);
    theTags( *result ) = OARRAY | LITERAL | READ_ONLY;
    SETGLOBJECTTO(*result, TRUE);
    theLen(*result) = 2;
    oArray(*result) = &mt[0];
  } break;
  }

  return TRUE ;
}


static void tasks_set_thread_limits(void)
{
  thread_count_t want_active, want_max ;
  Bool swapped ;

  want_active = maxthreads_active ;
  want_max = maxthreads_max ;

  /* Silently clip to the prescribed limits */
  if ( limit_active_password != 0 ||
       secDevCanEnableFeature(GNKEY_FEATURE_MAX_THREADS_LIMIT) ) {
    uint8 featureName[64] ;

    /* Set limit from password. Start by checking if password is same as
       previous limit. */
    if ( limit_active_from_password >= 0 ) {
      GENERATE_MAX_THREADS_STRING(swcopyf, featureName, uint8 *, CAST_SIGNED_TO_UINT32(limit_active_from_password)) ;
      if ( !SWsubfeature_permission(limit_active_password, GNKEY_FEATURE_MAX_THREADS_LIMIT, (char *)featureName) )
        limit_active_from_password = -1 ;
    }
    if ( limit_active_from_password < 0 ) {
      thread_count_t password_limit ;
      /* From 0 because 0 means unlimited, and skip 1 */
      for ( password_limit = 0; password_limit <= NTHREADS_LIMIT; password_limit++ ) {
        if ( password_limit == 1 )
          continue ;
        GENERATE_MAX_THREADS_STRING(swcopyf, featureName, uint8 *, CAST_SIGNED_TO_UINT32(password_limit)) ;
        if ( SWsubfeature_permission(limit_active_password, GNKEY_FEATURE_MAX_THREADS_LIMIT, (char *)featureName) ) {
          limit_active_from_password = password_limit ;
          break ;
        }
      }
    }
    if ( limit_active_from_password < 0 ) {
      /* Incorrect password -> 1 thread */
      limit_active_from_password = 1;
    }
  } else {
    /* No password -> 1 thread */
    limit_active_from_password = 1;
  }

  if ( limit_active_from_password != 0 && want_active > limit_active_from_password )
    want_active = limit_active_from_password ;

  if ( want_active > limit_active_original )
    want_active = limit_active_original ;

  if ( want_max > limit_max_original )
    want_max = limit_max_original ;

  if ( want_active > want_max )
    want_active = want_max ;

  /* Check if we can modify the limit immediately. We can do this only when
     we haven't either constrained the thread pool or expanded it. This is
     also the only time we change thread_limit_max and
     request_limit_active. They are protected by the task mutex because we
     don't want to change them whilst dispatchers are making decisions
     based on them. */
  multi_mutex_lock(&task_mutex) ;
  thread_limit_max = want_max ;
  request_limit_active = want_active ;
  HqAtomicCAS(&thread_limit_current, thread_limit_active,
              request_limit_active, swapped) ;
  if ( swapped )
    thread_limit_active = request_limit_active ;
  multi_mutex_unlock(&task_mutex) ;
}

Bool tasks_set_systemparam(corecontext_t *context, uint16 name, OBJECT *theo)
{
  thread_count_t want_active, want_max ;

  UNUSED_PARAM(corecontext_t *, context) ;

  HQASSERT((theo && name < NAMES_COUNTED) ||
           (!theo && name == NAMES_COUNTED),
           "name and parameter object inconsistent") ;

  switch ( name ) {
  case NAME_MaxThreads:
    /* Try to set the number of active threads, and/or the max number of
       threads. We need to be careful about pool extension and low memory
       constraint, and we always have to stay within the original limits
       passed to SwStart(), as modified by the security checks. So we're
       going to set a requested active limit, and if we can't change the
       thread limit now, we'll let some other part of the task system do it
       when its ready. */
    if ( oType(*theo) == OARRAY ) {
      if ( theLen(*theo) != 2 )
        return error_handler(RANGECHECK) ;

      theo = oArray(*theo) ;
      if ( oType(theo[0]) != OINTEGER || oType(theo[1]) != OINTEGER )
        return error_handler(TYPECHECK) ;

      want_active = oInteger(theo[0]) ;
      want_max = oInteger(theo[1]) ;
    } else {
      want_active = oInteger(*theo) ;
      want_max = max(want_active, maxthreads_max);
    }

    if ( want_active < 1 || want_max < 1 )
      return error_handler(RANGECHECK) ;

    maxthreads_active = want_active ;
    maxthreads_max = want_max ;

    /* Attempt to set values, silently clipping to limits */
    tasks_set_thread_limits() ;
    break;

  case NAME_MaxThreadsLimit:
    limit_active_password = oInteger(*theo) ;

    /* Attempt to set values, as new password may have changed limit clipping */
    tasks_set_thread_limits() ;
    break ;

    /* These parameters are write-only parameters for tuning experiments.
       They need to be in timing builds because they're controls for
       performance testing, which is why debug ripvars are not used. */
  case NAME_TaskJoinWaitMilliseconds:
    join_wait_ms = oInteger(*theo) ; /* <= 0 disables timed waits. */
    break ;
  case NAME_TaskHelperWaitThreshold:
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    /* Should be >= helper_start_threshold, but RIP won't break if it's not,
       so don't bother with finaliser enforcement. */
    helper_wait_threshold = (unsigned int)oInteger(*theo) ;
    break ;
  case NAME_TaskHelperStartThreshold:
    /* Should be >= helper_end_threshold, but RIP won't break if it's not,
       so don't bother with finaliser enforcement. */
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    helper_start_threshold = (unsigned int)oInteger(*theo) ;
    break ;
  case NAME_TaskHelperEndThreshold:
    /* Should be <= helper_start_threshold, but RIP won't break if it's not,
       so don't bother with finaliser enforcement. */
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    multi_mutex_lock(&task_mutex) ;
    helper_end_threshold = (unsigned int)oInteger(*theo) ;
    if ( tasks_incomplete <= helper_end_threshold )
      wake_all_threads(THREAD_WAIT_HELP, THREAD_RUNNING) ;
    multi_mutex_unlock(&task_mutex) ;
    break ;
  }

  return TRUE ;
}

static module_params_t tasks_system_params = {
  tasks_match,
  tasks_set_systemparam,
  tasks_get_systemparam,
  NULL
} ;

int32 max_simultaneous_tasks(void)
{
  return (int32)limit_active_original ;
}

/****************************************************************************/

/** \brief Set the number of threads allowed from the SWSTART parameters. */
static Bool tasks_swstart_params(SWSTART *params)
{
  unsigned int i ;
  thread_count_t tag_value ;

  for (i = 0; params[i].tag != SWEndTag; i++) {
    if (params[i].tag == SWNThreadsMaxTag) {
      if ( params[i].value.int_value < 0 )
        return FAILURE(FALSE) ;

      tag_value = (thread_count_t)params[i].value.int_value ;
      if ( tag_value <= 0 || tag_value >= NTHREADS_LIMIT )
        tag_value = NTHREADS_LIMIT - 1 ;

      if ( limit_max_original != 0 &&
           limit_max_original != tag_value )
        return FAILURE(FALSE) ;

      limit_max_original = tag_value ;

      if ( limit_active_original != 0 &&
           limit_max_original < limit_active_original )
        return FAILURE(FALSE) ;
    } else if (params[i].tag == SWNThreadsTag) {
      if ( params[i].value.int_value < 0 )
        return FAILURE(FALSE) ;

      tag_value = (thread_count_t)params[i].value.int_value ;
      if ( tag_value <= 0 || tag_value >= NTHREADS_LIMIT )
        tag_value = NTHREADS_LIMIT - 1 ;

      if ( limit_active_original != 0 &&
           limit_active_original != tag_value )
        return FAILURE(FALSE) ;

      limit_active_original = tag_value ;

      if ( limit_max_original != 0 &&
           limit_max_original < limit_active_original )
        return FAILURE(FALSE) ;
    }
  }

  return TRUE ;
}

/** \brief Initialise the task system.

    \param[in] params  The \c SwStart() parameters.

    \retval TRUE  If the first-phase initialisation was successful.
    \retval FALSE If the first-phase initialisation failed. In this case, the
                  function should cleanup all resource allocations.
 */
static Bool tasks_swinit(SWSTART *params)
{
  struct mm_sac_classes_t sac_classes[] = { /* size, num, freq */
    { DWORD_ALIGN_UP(size_t, sizeof(task_link_t)), 256, 6 },
    { DWORD_ALIGN_UP(size_t, sizeof(task_group_t)), 32, 1 },
    { DWORD_ALIGN_UP(size_t, sizeof(task_t)), 128, 2 },
  } ;
  corecontext_t *context = get_core_context() ;

  /* Link SystemParam accessors into global list */
  HQASSERT(tasks_system_params.next == NULL,
           "Already linked user params accessor") ;
  tasks_system_params.next = context->systemparamlist ;
  context->systemparamlist = &tasks_system_params ;

  /* Cancel wait, tasks allocated and threads active are only done on the
     init thread. */
  probe_other(SW_TRACE_TASKS_ACTIVE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_TASKS_RUNNABLE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_TASKS_UNPROVISIONED, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;

  probe_other(SW_TRACE_TASKS_INCOMPLETE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_TASK_GROUPS_INCOMPLETE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_TASKS_COMPLETE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_TASK_GROUPS_COMPLETE, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_TASK_GROUP_RESOURCES, SW_TRACETYPE_OPTION,
              SW_TRACEOPTION_TIMELINE|SW_TRACEOPTION_AFFINITY) ;
  probe_other(SW_TRACE_THREAD_WAKE, SW_TRACETYPE_OPTION,
              SW_TRACEOPTION_MARKDATA) ;
  probe_other(SW_TRACE_THREAD_WAKE_FAIL, SW_TRACETYPE_OPTION,
              SW_TRACEOPTION_MARKDATA) ;
  probe_other(SW_TRACE_TASK_GROUP_PROVISION, SW_TRACETYPE_OPTION,
              SW_TRACEOPTION_MARKDATA) ;

  if ( !tasks_swstart_params(params) )
    return FALSE ;

  /* New pool for tasks */
  if ( mm_pool_create(&mm_pool_task, TEMP_POOL_TYPE, TEMP_POOL_PARAMS) != MM_SUCCESS )
    return FALSE ;

  if ( mm_sac_create(mm_pool_task,
                     sac_classes,
                     NUM_ARRAY_ITEMS(sac_classes)) != MM_SUCCESS ) {
    mm_pool_destroy(mm_pool_task) ;
    mm_pool_task = NULL ;
    return FALSE ;
  }

#ifdef DEBUG_BUILD
  if ( !deferred_thread_spawn(&debug_sleeper_thread, &debug_sleeping, debug_sleeper) ) {
    mm_sac_destroy(mm_pool_task) ;
    mm_pool_destroy(mm_pool_task) ;
    mm_pool_task = NULL ;
    return FALSE ;
  }

  multi_mutex_init(&debug_sleeper_mutex, SOLE_LOCK_CLASS, FALSE,
                   SW_TRACE_INVALID, SW_TRACE_INVALID) ;
  multi_condvar_init(&debug_sleeper_condition, &debug_sleeper_mutex,
                     SW_TRACE_INVALID) ;
#endif

  multi_mutex_init(&spawn_mutex, SOLE_LOCK_CLASS, FALSE,
                   SW_TRACE_INVALID, SW_TRACE_INVALID) ;
  multi_condvar_init(&spawn_condition, &spawn_mutex, SW_TRACE_INVALID) ;

  multi_mutex_init(&task_mutex, TASK_LOCK_INDEX, FALSE,
                   SW_TRACE_TASK_ACQUIRE, SW_TRACE_TASK_HOLD) ;

  incomplete_task_group.state = GROUP_STATE_ACTIVE ;
  /* Count two references: one for the interpreter task, and another to
     prevent this group from being ever freed. */
  incomplete_task_group.refcount = 2 ;
  incomplete_task_group.parent = NULL ;
  DLL_RESET_LIST(&incomplete_task_group.children) ;
  DLL_RESET_LINK(&incomplete_task_group, peers) ;
  DLL_RESET_LIST(&incomplete_task_group.tasks) ;
  DLL_RESET_LINK(&incomplete_task_group, joins_link) ;
  DLL_RESET_LINK(&incomplete_task_group, order_link) ;
  incomplete_task_group.join = NULL ;
  incomplete_task_group.reqnode = NULL ;
  incomplete_task_group.n_tasks = 0 ;
  incomplete_task_group.provisions = PROVISION_OK ;
  incomplete_task_group.generation = group_resource_generation ;
  incomplete_task_group.type = TASK_GROUP_ROOT ;
  incomplete_task_group.result = TRUE ;
  incomplete_task_group.mark = TASK_MARK_INIT ;
  incomplete_task_group.error.new_error = 0 ;
  incomplete_task_group.error.old_error = 0 ;
  NAME_OBJECT(&incomplete_task_group, TASK_GROUP_NAME) ;

  interpreter_task.state = TASK_RUNNING ;
  interpreter_task.runnability = RL_RUNNING ;
  /* Count three references: one from the interpreter group that the task
     joins, one from the orphaned task group, and another to prevent this
     task from being ever freed. */
  interpreter_task.refcount = 3 ;
  interpreter_task.specialiser = &interpreter_task_specialise ;
  interpreter_task.specialiser_args = NULL ;
  interpreter_task.worker = NULL ;
  interpreter_task.cleaner = NULL ;
  interpreter_task.args = NULL ;
  interpreter_task.group = NULL ;
  DLL_RESET_LINK(&interpreter_task, group_link) ;
  DLL_RESET_LINK(&interpreter_task, order_link) ;
  DLL_RESET_LIST(&interpreter_task.joins_list) ;
  DLL_RESET_LIST(&interpreter_task.beforeme) ;
  DLL_RESET_LIST(&interpreter_task.afterme) ;
  interpreter_task.trace_id = SW_TRACE_INTERPRET ;
  interpreter_task.waiting_on = NULL ;
  interpreter_task.result = TRUE ;
  interpreter_task.mark = TASK_MARK_INIT ;
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  interpreter_task.recursive = NULL ;
  interpreter_task.thread_index = context->thread_index ;
#endif
  NAME_OBJECT(&interpreter_task, TASK_NAME) ;

  /* Connect interpreter task and interpreter group. */
  incomplete_task_group.join = &interpreter_task ;
  DLL_ADD_TAIL(&interpreter_task.joins_list, &incomplete_task_group, joins_link) ;

  /* Special group for tasks that lose their groups. */
  orphaned_task_group.state = GROUP_STATE_ACTIVE ;
  /* Count two references: one for the interpreter task, and another to
     prevent this group from being ever freed. */
  orphaned_task_group.refcount = 2 ;
  orphaned_task_group.parent = NULL ;
  DLL_RESET_LIST(&orphaned_task_group.children) ;
  DLL_RESET_LINK(&orphaned_task_group, peers) ;
  DLL_RESET_LIST(&orphaned_task_group.tasks) ;
  DLL_RESET_LINK(&orphaned_task_group, joins_link) ;
  DLL_RESET_LINK(&orphaned_task_group, order_link) ;
  orphaned_task_group.join = NULL ;
  orphaned_task_group.reqnode = NULL ;
  orphaned_task_group.n_tasks = 0 ;
  orphaned_task_group.provisions = PROVISION_CANCELLED ;
  orphaned_task_group.generation = group_resource_generation ;
  orphaned_task_group.type = TASK_GROUP_ORPHANS ;
  orphaned_task_group.result = TRUE ;
  orphaned_task_group.mark = TASK_MARK_INIT ;
  orphaned_task_group.error.new_error = NOT_AN_ERROR ;
  orphaned_task_group.error.old_error = NOT_AN_ERROR ;
  NAME_OBJECT(&orphaned_task_group, TASK_GROUP_NAME) ;

  /* Connect interpreter task and interpreter group. */
  orphaned_task_group.join = &interpreter_task ;
  DLL_ADD_TAIL(&interpreter_task.joins_list, &orphaned_task_group, joins_link) ;

  INCREMENT_TASKS_ACTIVE() ;

  /** \todo ajcd 2011-07-06: Is this the actual live interpreter context?
      If so, it will change, and won't be appropriate for building new task
      contexts off. We need to enter a new recursive context when booting
      the interpreter, so we can have a context copy to modify, and a pristine
      context copy to act as the interpreter thread template. */
  interpreter_task_context.thread_context = context ;
  interpreter_task_context.current_task = &interpreter_task ;
  multi_condvar_init(&interpreter_task_context.cond, &task_mutex,
                     SW_TRACE_INVALID) ;
  DLL_RESET_LINK(&interpreter_task_context, thread_list) ;
  interpreter_task_context.state = THREAD_RUNNING ;
  NAME_OBJECT(&interpreter_task_context, TASK_CONTEXT_NAME) ;

  DLL_ADD_TAIL(&thread_list, &interpreter_task_context, thread_list) ;
  context->taskcontext = &interpreter_task_context ;

  return TRUE ;
}

/** \brief Hook up the main interpreter thread task.

    \param[in] params  The \c SwStart() parameters.

    \retval TRUE  If the startup initialisation was successful.
    \retval FALSE If the startup initialisation failed. In this case, the
                  \c tasks_finish() function will be called to cleanup
                  resource allocations. */
static Bool tasks_swstart(SWSTART *params)
{
  Bool success = TRUE ;
  pthread_attr_t attr ;
  corecontext_t *context = get_core_context() ;
  thread_count_t i ;

  probe_begin(SW_TRACE_THREAD, (intptr_t)context->thread_index) ;

  if ( !tasks_swstart_params(params) )
    return FALSE ;

  /* Set default thread limits. */
  if ( limit_active_original == 0 ) {
    if ( limit_max_original == 0 ) {
      /* Neither parameter set, so default to one thread. */
      limit_active_original = limit_max_original = 1 ;
    } else {
      /* They didn't say how many can run, so assume all of them. */
      limit_active_original = limit_max_original ;
    }
  } else if ( limit_max_original == 0 ) {
    /* Allow half of threads to stall in a condvar wait once. This calculation
       ensures that limit_max_original is 1 if limit_active_original is 1. */
    limit_max_original = limit_active_original * 3 / 2 ;
  }

  if ( limit_max_original >= NTHREADS_LIMIT - deferred_thread_total )
    limit_max_original = NTHREADS_LIMIT - deferred_thread_total - 1 ;
  if ( limit_active_original > limit_max_original )
    limit_active_original = limit_max_original ;

  /* Set the limit on the number of max threads from the limit we've derived.
     Start with 1 active thread, which can be increased up to
     limit_active_original by setting the MaxThreadLimit and MaxThread
     systemparams. */
  thread_limit_max = limit_max_original ;
  multi_nthreads = (unsigned int)limit_max_original ; /** \todo ajcd 2011-02-11: Compatibility: remove this. */
  thread_limit_current = thread_limit_active = request_limit_active = 1 ;
  threads_scheduled = 1 ; /* The interpreter thread is always scheduled. */

  /* Set defaults for MaxThreads values */
  maxthreads_active = limit_active_original ;
  maxthreads_max = limit_max_original ;

  probe_other(SW_TRACE_TASKS_INCOMPLETE, SW_TRACETYPE_AMOUNT, 0) ;
  probe_other(SW_TRACE_TASKS_COMPLETE, SW_TRACETYPE_AMOUNT, 0) ;
  /* First threads active probe gives the active threads limit. */
  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_AMOUNT, limit_active_original) ;
  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_AMOUNT, thread_limit_active) ;

  if (pthread_attr_init(&attr) != 0)
    return FAILURE(FALSE);

#define return DO_NOT_return_SET_success_INSTEAD!
  /* Use success variable from here on so that we do destroy the
     attribute. */
  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0)
    success = FAILURE(FALSE) ;

  /* Spawn the maximum number of threads that we can ever use. */
  for (i = 1; success && i < thread_limit_max; ++i) {
    PROBE(SW_TRACE_THREAD_CREATE, i,
          success = pool_thread_spawn(&threads[i], &attr, context, task_dispatch)) ;
  }

  thread_pool_started = thread_total_started ;

  while ( deferred_threads != NULL ) {
    deferred_thread_t *next = deferred_threads->next ;

    VERIFY_OBJECT(deferred_threads, DEFERRED_THREAD_NAME) ;

    if ( success ) {
      PROBE(SW_TRACE_THREAD_CREATE, i,
            success = pool_thread_spawn(deferred_threads->thread,
                                        &attr, context,
                                        deferred_threads->thread_fn)) ;
      *deferred_threads->started = success ;
      ++i ;
    }

    UNNAME_OBJECT(deferred_threads) ;

    mm_free(mm_pool_task, deferred_threads, sizeof(*deferred_threads)) ;
    deferred_threads = next ;
  }

  pthread_attr_destroy(&attr) ;
#undef return

  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_AMOUNT, threads_active) ;

#ifdef DEBUG_BUILD
  register_ripvar(NAME_debug_tasks, OINTEGER, &debug_tasks);
#endif

  /* With only one thread, we might as well run ready tasks as soon as we
     can. */
  if ( limit_max_original > 1 ) {
    helper_start_threshold = TASK_HELPER_START ;
    helper_end_threshold = TASK_HELPER_DONE ;
  } else {
    helper_start_threshold = helper_end_threshold = 1 ;
  }

  /* Check for feature in key now, so that HHR doesn't have to set
     /MaxThreads or /MaxThreadsLimit for -p option to take effect. */
  if ( secDevCanEnableFeature(GNKEY_FEATURE_MAX_THREADS_LIMIT) ) {
    tasks_set_thread_limits();
  }

  return success ;
}

/** \brief Finalise the task system. */
static void tasks_finish(void)
{
  task_group_t *group ;
  task_t *task ;
  task_context_t *thread ;
  thread_count_t i ;

  HQASSERT(rip_is_quitting(), "Not marked as quitting in tasks_finish") ;
  HQASSERT(thread_pool_constrained == 0,
           "Shouldn't be finalising RIP in low memory handler") ;

  /* No new items can be constructed, because we're in RIP shutdown state.
     Cancel all items in all groups, then join them. */
  task_group_cancel(&incomplete_task_group, INTERRUPT) ;
  task_group_cancel(&orphaned_task_group, INTERRUPT) ;

  (void)task_group_join(&incomplete_task_group, NULL) ;

  multi_mutex_lock(&task_mutex) ;
  /* The interpreter task will never fall out (it's run on this thread). It
     shouldn't be waiting on any task (otherwise how would we be here?), it
     can't be joined, can't have predecessors, and doesn't have a cleanup
     function so it's OK to remove it directly without finalisation. */
  HQASSERT(interpreter_task.cleaner == NULL,
           "Interpreter task has a cleanup function") ;
  HQASSERT(DLL_GET_HEAD(&interpreter_task.joins_list, task_group_t,
                        joins_link) == &orphaned_task_group &&
           DLL_GET_NEXT(&orphaned_task_group, task_group_t, joins_link) == NULL,
           "Interpreter task joins groups") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&interpreter_task.beforeme),
           "Interpreter task has predecessors") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&interpreter_task.afterme),
           "Interpreter task has successors") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&incomplete_task_group.tasks),
           "Interpreter group has tasks") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&incomplete_task_group.children),
           "Interpreter group has children") ;

  /* Lie about there being tasks ready, so task dispatch loops will detect
     RIP shutdown exit. We cannot assert the orphaned group is empty yet,
     because even though all tasks have joined, they may still be in
     task_execute() or task_specialise_done(), and just not have reached the
     point where the task reference is released yet. We have to join the
     threads in order to ensure that these references are gone. */
  for ( thread = DLL_GET_HEAD(&thread_list, task_context_t, thread_list) ;
        thread != NULL ;
        thread = DLL_GET_NEXT(thread, task_context_t, thread_list) ) {
    multi_condvar_broadcast(&thread->cond) ;
    probe_other(SW_TRACE_THREAD_WAKE, SW_TRACETYPE_MARK,
                thread->thread_context->thread_index) ;
  }
  multi_mutex_unlock(&task_mutex) ;

  /* Wait for thread pool to complete. */
  for (i = 1; i <= thread_pool_started; ++i) {
    int joined ;

    PROBE(SW_TRACE_THREAD_JOIN, i,
          joined = pthread_join(threads[i], NULL)) ;

    if ( joined != 0 )
      HQFAIL("Problem joining thread.") ;
  }

  multi_mutex_lock(&task_mutex) ;

  HQASSERT(DLL_LIST_IS_EMPTY(&orphaned_task_group.tasks),
           "Orphaned group has tasks") ;
  HQASSERT(DLL_LIST_IS_EMPTY(&orphaned_task_group.children),
           "Orphaned group has children") ;

  /* Forcefully destroy any groups and tasks that did exist. The only way
     they could have been in the orphaned group is through task_group_join()
     or task_execute() respectively, which will have ensured that there are
     no predecessors, successors, joinees/joiners, and that they are
     re-parented to the orphaned task group. */
  for ( group = DLL_GET_HEAD(&orphaned_task_group.children, task_group_t, peers) ;
        group != NULL ; ) {
    task_group_t *next = DLL_GET_NEXT(group, task_group_t, peers) ;
    group->refcount = 1 ;
    group_release_locked(&group) ;
    group = next ;
  }

  for ( task = DLL_GET_HEAD(&orphaned_task_group.tasks, task_t, group_link) ;
        task != NULL ; ) {
    task_t *next = DLL_GET_NEXT(task, task_t, group_link) ;
    task->refcount = 1 ;
    task_release_locked(&task) ;
    task = next ;
  }

  /* We're about to destroy the mutex on which task condvars are based, and
     the MM pool from which tasks were allocated, so force cleanup of all
     remaining tasks, leaving dangling pointers in the callers. Provide
     assertions so that abusers of the task system can find and cleanup their
     own mess. */
  HQASSERT(DLL_LIST_IS_EMPTY(&task_schedule) &&
           DLL_LIST_IS_EMPTY(&group_schedule),
           "Tasks or task groups still on schedule lists") ;
  HQASSERT(tasks_incomplete + tasks_complete == 0, "Tasks still allocated") ;
  HQASSERT(groups_incomplete + groups_complete == 0, "Task groups still allocated") ;
  HQASSERT(groups_provisioned == 0, "Groups still provisioned") ;
#ifdef PROBE_BUILD
  HQASSERT(tasks_active == 1, "Tasks still active") ;
  DECREMENT_TASKS_ACTIVE() ;
#endif
#ifdef METRICS_BUILD
  HQASSERT(links_current_allocated == 0, "Task links still allocated") ;
  HQASSERT(requirements_current_allocated == 0, "Resource requirements still allocated") ;
#endif

  HQASSERT(threads_scheduled == 1,
           "Finished waiting for tasks, but thread still scheduled") ;

  multi_mutex_unlock(&task_mutex) ;

#ifdef DEBUG_BUILD
  multi_mutex_lock(&debug_sleeper_mutex) ;
  multi_condvar_signal(&debug_sleeper_condition) ;
  multi_mutex_unlock(&debug_sleeper_mutex) ;
  if ( debug_sleeping ) {
    if ( pthread_join(debug_sleeper_thread, NULL) != 0 )
      HQFAIL("Problem joining sleeper thread.") ;
  }
  multi_condvar_finish(&debug_sleeper_condition) ;
  multi_mutex_finish(&debug_sleeper_mutex) ;
#endif

  DLL_REMOVE(&interpreter_task_context, thread_list) ;
  HQASSERT(DLL_LIST_IS_EMPTY(&thread_list), "Threads still on list") ;

  multi_condvar_finish(&interpreter_task_context.cond) ;
  multi_mutex_finish(&task_mutex) ;

  multi_condvar_finish(&spawn_condition) ;
  multi_mutex_finish(&spawn_mutex) ;

  /* If there are any deferred threads, they'll never get started now. */
  while ( deferred_threads != NULL ) {
    deferred_thread_t *next = deferred_threads->next ;
    UNNAME_OBJECT(deferred_threads) ;
    mm_free(mm_pool_task, deferred_threads, sizeof(*deferred_threads)) ;
    deferred_threads = next ;
  }

  mm_sac_destroy(mm_pool_task) ;
  mm_pool_destroy(mm_pool_task) ;
  mm_pool_task = NULL ;

  UNNAME_OBJECT(&interpreter_task_context) ;
  UNNAME_OBJECT(&interpreter_task) ;

  probe_other(SW_TRACE_THREADS_ACTIVE, SW_TRACETYPE_AMOUNT, 0) ;
  probe_end(SW_TRACE_THREAD, (intptr_t)CoreContext.thread_index) ;
}

static void init_C_globals_task(void)
{
  task_t init_task = { 0 } ;
  task_group_t init_group = { 0 } ;
  task_context_t init_task_context = { 0 } ;

#ifdef DEBUG_BUILD
  debug_tasks = 0 ;
  debug_sleeping = FALSE ;
  debug_sleep_graph = 0 ;
  debug_schedule_graph = 0 ;
#endif

  join_wait_ms = JOIN_WAIT_mS ;
  helper_wait_ms = HELPER_WAIT_mS ;

  /* Thresholds need to wait until swstart, in case they're based on thread
     limits. */
  helper_wait_threshold = TASK_HELPER_WAIT ;
  helper_start_threshold = TASK_HELPER_START ;
  helper_end_threshold = TASK_HELPER_DONE ;

  tasks_system_params.next = NULL ;

  interpreter_task = init_task ;
  incomplete_task_group = init_group ;
  orphaned_task_group = init_group ;
  interpreter_task_context = init_task_context ;

  need_recompute_schedule = TRUE ;
  DLL_RESET_LIST(&task_schedule) ;
  DLL_RESET_LIST(&group_schedule) ;
  DLL_RESET_LIST(&thread_list) ;

  all_specialisers = NULL ;
  spawn_done = TRUE ;
  mm_pool_task = NULL ;
  limit_max_original = thread_limit_max = 0 ;
  limit_active_original = thread_limit_active = request_limit_active = 0 ;
  multi_nthreads = 0 ;
  thread_limit_current = 0 ;
  thread_total_started = 0 ;
  thread_pool_started = 0 ;
  threads_scheduled = 0 ;
  threads_waiting_for_resources = 0 ;
  thread_pool_constrained = 0 ;
  limit_active_password = 0 ;
  limit_active_from_password = -1 ;
  tasks_incomplete = tasks_complete = 0 ;
  tasks_runnable_helpable = tasks_runnable_nothelpable =
    tasks_ready_unprovisioned = 0 ;
  groups_incomplete = groups_complete = 0 ;
  groups_provisioned = 0 ;
  deferred_threads = NULL ;
  deferred_thread_total = 0 ;
  task_visitor_mark = TASK_MARK_INIT ;
  group_resource_generation = task_helpable_generation =
    task_nothelpable_generation = task_unprovisioned_generation = 0 ;
  /* Set up helper generation tracker to be just before the current (initial)
     generation, so the tests will run the first time it is used. */
  helper_generation = dispatch_generation = -1 ;
#ifdef DEBUG_BUILD
  task_graph_count = 0 ;
#endif
#ifdef PROBE_BUILD
  tasks_active = 0 ;
  threads_active = 1 ; /* Interpreter thread is active */
  pointless_wakeups = 0 ;
  pointless_timeouts = 0 ;
  pointless_traverses = 0 ;
#endif
#if defined(METRICS_BUILD)
  tasks_total_allocated = groups_total_allocated =
    links_total_allocated = requirements_total_allocated = 0 ;
  links_current_allocated = requirements_current_allocated = 0 ;
  tasks_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&tasks_metrics_hook) ;
#endif
}

void task_C_globals(core_init_fns *fns)
{
  init_C_globals_task() ;

  fns->swinit = tasks_swinit ;
  fns->swstart = tasks_swstart ;
  fns->finish = tasks_finish ;
}

/*****************************************************************************/
/* For compatibility polling, we need a thread that lasts the RIP's lifetime.
   We can't start it until after the pool threads, so allow it to be
   registered during SwStart() phase, then started when tasks_swstart() runs.
   If tasks_swstart() runs successfully, the thread will be spawned, and the
   thread started boolean will be set to TRUE. The caller's finish() function
   is responsible for removing and joining the pthread directly.
*/
Bool deferred_thread_spawn(pthread_t *thread, Bool *started,
                           void (*thread_fn)(corecontext_t *, void *))
{
  deferred_thread_t *defer ;

  HQASSERT(thread, "Nowhere to put thread handle") ;
  HQASSERT(started, "Nowhere to put thread status") ;
  HQASSERT(thread_fn, "No thread function to spawn") ;

  if ( mm_pool_task == NULL ) /* Not in SwInit yet, or started shutting down */
    return FAILURE(FALSE) ;

  if ( thread_total_started > 0 ) /* Called after SwStart() */
    return FAILURE(FALSE) ;

  *started = FALSE ;

  if ( (defer = mm_alloc(mm_pool_task, sizeof(*defer), MM_ALLOC_CLASS_DEFERRED_THREAD)) == NULL )
    return FAILURE(FALSE) ;

  defer->thread_fn = thread_fn ;
  defer->started = started ;
  defer->thread = thread ;
  defer->next = deferred_threads ;

  NAME_OBJECT(defer, DEFERRED_THREAD_NAME) ;

  deferred_threads = defer ;
  ++deferred_thread_total ;

  return TRUE ;
}

/*****************************************************************************/
#ifdef DEBUG_BUILD

#include <stdio.h>

#define DEBUG_GRAPH_BUFSIZE 512

static const char *debug_group_error(task_group_t *group)
{
  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( group->result )
    return "OK" ;

  switch ( group->error.old_error ) {
  case DICTFULL: return "DICTFULL" ;
  case DICTSTACKOVERFLOW: return "DICTSTACKOVERFLOW" ;
  case DICTSTACKUNDERFLOW: return "DICTSTACKUNDERFLOW" ;
  case EXECSTACKOVERFLOW: return "EXECSTACKOVERFLOW" ;
  case INTERRUPT: return "INTERRUPT" ;
  case INVALIDACCESS: return "INVALIDACCESS" ;
  case INVALIDEXIT: return "INVALIDEXIT" ;
  case INVALIDFILEACCESS: return "INVALIDFILEACCESS" ;
  case INVALIDFONT: return "INVALIDFONT" ;
  case INVALIDRESTORE: return "INVALIDRESTORE" ;
  case IOERROR: return "IOERROR" ;
  case LIMITCHECK: return "LIMITCHECK" ;
  case NOCURRENTPOINT: return "NOCURRENTPOINT" ;
  case RANGECHECK: return "RANGECHECK" ;
  case STACKOVERFLOW: return "STACKOVERFLOW" ;
  case STACKUNDERFLOW: return "STACKUNDERFLOW" ;
  case SYNTAXERROR: return "SYNTAXERROR" ;
  case TIMEOUT: return "TIMEOUT" ;
  case TYPECHECK: return "TYPECHECK" ;
  case UNDEFINED: return "UNDEFINED" ;
  case UNDEFINEDFILENAME: return "UNDEFINEDFILENAME" ;
  case UNDEFINEDRESULT: return "UNDEFINEDRESULT" ;
  case UNMATCHEDMARK: return "UNMATCHEDMARK" ;
  case UNREGISTERED: return "UNREGISTERED" ;
  case VMERROR: return "VMERROR" ;
  case DISKVMERROR: return "DISKVMERROR" ;
  case CONFIGURATIONERROR: return "CONFIGURATIONERROR" ;
  case UNDEFINEDRESOURCE: return "UNDEFINEDRESOURCE" ;
  }

  return "Unknown" ;
}

static inline void debug_graph_indent(FILE *file, unsigned int indent)
{
  while ( indent > 0 ) {
    fputs("  ", file) ;
    --indent ;
  }
}

static void debug_graph_node(task_t *task, FILE *file, unsigned int indent)
{
  VERIFY_OBJECT(task, TASK_NAME) ;

  if ( task->mark != task_visitor_mark ) {
    uint8 buffer[DEBUG_GRAPH_BUFSIZE] ;
    const char *task_name = task_names[task->trace_id] ;
    const char *state_name = task->state < NUM_ARRAY_ITEMS(task_states) ?
      task_states[task->state] : "INVALID_STATE" ;
    const char *run_name = task->runnability < NUM_ARRAY_ITEMS(run_states) ?
      run_states[task->runnability] : "INVALID_RL" ;
    task_mark_t mark = task->mark ;

    task->mark = task_visitor_mark ;

    debug_graph_indent(file, indent) ;
    (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                   "t%Px [label=\"%s\\n%Px\\n%s\\n%s\\nmark %d\\nthread %u\\nrefs %d\\n%s\"",
                   task, task_name, task, state_name, run_name, mark,
                   task->thread_index, task->refcount,
                   task->result ? "OK" : "FAIL") ;
    fputs((const char *)buffer, file) ;
    if ( task->trace_id == SW_TRACE_INTERPRET ) {
      fputs(",style=bold", file) ;
    }
    if ( task->recursive != NULL ) {
      if ( task->recursive->thread_index == task->thread_index ) {
        fputs(",color=\"purple\"", file) ; /* Recursive join activation */
      } else {
        fputs(",color=\"red\"", file) ; /* Recursive join wait */
      }
    } else if ( task->waiting_on != NULL ) {
      fputs(",color=\"deeppink\"", file) ; /* Waiting on non-task condition */
    } else if ( task->state == TASK_RUNNING ||
                task->state == TASK_CANCELLING ||
                task->state == TASK_FINALISING) {
      fputs(",color=\"green\"", file) ; /* Running, not waiting on anything */
    } else if ( task->state == TASK_READY || task->state == TASK_CANCELLED ) {
      fputs(",color=\"orange\"", file) ; /* Schedulable */
    }
    fputs("];\n", file) ;
  }
}

static void debug_graph_group_nodes(task_group_t *group, FILE *file,
                                    unsigned int indent)
{
  uint8 buffer[DEBUG_GRAPH_BUFSIZE] ;

  VERIFY_OBJECT(group, TASK_GROUP_NAME) ;

  if ( group->mark != task_visitor_mark ) {
    const char *group_name = group->type < NUM_ARRAY_ITEMS(group_names) ?
      group_names[group->type] : "INVALID_TYPE" ;
    const char *state_name = group->state < NUM_ARRAY_ITEMS(group_states) ?
      group_states[group->state] : "INVALID_STATE" ;
    task_t *task ;
    task_group_t *child ;
    unsigned int i ;
    task_mark_t mark = group->mark ;

    group->mark = task_visitor_mark ;

    debug_graph_indent(file, indent) ;
    (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                   "subgraph cluster_%Px {\n", group) ;
    fputs((const char *)buffer, file) ;

    debug_graph_indent(file, indent+1) ;
    (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                   "style=\"dashed\"\n") ;
    fputs((const char *)buffer, file) ;

    debug_graph_indent(file, indent+1) ;
    if ( group->being_joined ) {
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "color=\"purple\"\n") ;
    } else {
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "color=\"black\"\n") ;
    }
    fputs((const char *)buffer, file) ;

    debug_graph_indent(file, indent+1) ;
    (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                   "label=\"%s %Px\\n%s\\n",
                   group_name, group, state_name) ;
    fputs((const char *)buffer, file) ;
    if ( group->reqnode != NULL ) {
      requirement_node_t *node = group->reqnode ;
      resource_requirement_t *req = node->requirement ;
      static const char *provisions[N_GROUP_PROVISIONS] = {
        "P ", "D ", "Ca ", "N ", "Co ", "Rl ", "Rr ", "Rp ", "Po ", "A ", "Pr ", "S ", "G ",
      } ;
      const char *sep = provisions[group->provisions] ;
      for ( i = 0 ; i < N_RESOURCE_TYPES ; ++i ) {
        fputs(sep, file) ; sep = " " ;
        if ( node->maximum[i] == 0 ) {
          fputs("*", file) ;
        } else {
          /* How many do we have? How many total? How many fixed? */
          int32 n = 0, t = 0, f = 0 ;
          if ( req->pools[i] != NULL ) {
            resource_lookup_t *lookup = NULL ;
            Bool locked ;
            resource_lookup_trylock(req->pools[i], lookup, locked) ;
            if ( locked ) {
              if ( lookup != NULL ) {
                unsigned int j ;
                for ( j = 0 ; j < lookup->nentries ; ++j ) {
                  if ( lookup->entries[j] != NULL ) {
                    ++t ;
                    if ( lookup->entries[j]->owner == group ) {
                      ++n ;
                      if ( lookup->entries[j]->state == TASK_RESOURCE_FIXING ||
                           lookup->entries[j]->state == TASK_RESOURCE_FIXED )
                        ++f ;
                    }
                  }
                }
              }
              resource_lookup_unlock(req->pools[i], lookup) ;
            } else {
              n = t = -1 ;
            }
          }
          (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                         (node->minimum[i] == node->maximum[i]
                          ? "%d/%d/%d:%d"
                          : "%d/%d/%d:%d-%d"),
                         f, n, t,
                         (int32)node->minimum[i], (int32)node->maximum[i]) ;
          fputs((const char *)buffer, file) ;
        }
      }
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     " %u/%u-%u ", node->ngroups,
                     requirement_smin(node, thread_limit_active),
                     requirement_smax(node, thread_limit_active)) ;
      fputs((const char *)buffer, file) ;
    } else {
      static const char *provisions[N_GROUP_PROVISIONS] = {
        "Provisioned",
        "Deprovisioned",
        "Cancelled",
        "Never provisioned",
        "Not provisioned (constructing)",
        "Not provisioned (requirement limit)",
        "Not provisioned (requirement ready)",
        "Not provisioned (requirement previous)",
        "Not provisioned (pool)",
        "Not provisioned (alloc)",
        "Not provisioned (previous)",
        "Not provisioned (callback)",
        "Not provisioned (generation)",
      } ;
      fputs(provisions[group->provisions], file) ;
    }
    (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                   "\\nrefs %d\\nmark %d\\n%s\";\nd%Px [style=\"invis\"];\n",
                   group->refcount, mark, debug_group_error(group), group) ;
    fputs((const char *)buffer, file) ;

    for ( task = DLL_GET_HEAD(&group->tasks, task_t, group_link) ;
          task != NULL ;
          task = DLL_GET_NEXT(task, task_t, group_link) ) {
      debug_graph_node(task, file, indent+1) ;
    }

    for ( child = DLL_GET_HEAD(&group->children, task_group_t, peers) ;
          child != NULL ;
          child = DLL_GET_NEXT(child, task_group_t, peers) ) {
      debug_graph_group_nodes(child, file, indent+1) ;
    }

    debug_graph_indent(file, indent) ;
    fputs("}\n", file) ;
  }
}

static void debug_graph_edge(task_t *task, FILE *file, unsigned int indent)
{
  VERIFY_OBJECT(task, TASK_NAME) ;

  if ( task->mark != task_visitor_mark ) {
    task_t *next ;
    task_link_t *entry ;
    uint8 buffer[DEBUG_GRAPH_BUFSIZE] ;

    task->mark = task_visitor_mark ;

    for ( entry = DLL_GET_HEAD(&task->afterme, task_link_t, post_list) ;
          entry != NULL ;
          entry = DLL_GET_NEXT(entry, task_link_t, post_list) ) {
      task_t *post = entry->post ;
      VERIFY_OBJECT(post, TASK_NAME) ;
      HQASSERT(entry->pre == task, "Post-task list link broken") ;

      debug_graph_indent(file, indent) ;
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "t%Px -> t%Px [color=\"black\"];\n",
                     task, post) ;
      fputs((const char *)buffer, file) ;
      debug_graph_edge(post, file, indent) ;
    }

    if ( task->recursive != NULL ) {
      /* Recursive activation link recursive->task */
      VERIFY_OBJECT(task->recursive, TASK_NAME) ;
      debug_graph_indent(file, indent) ;
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "t%Px -> t%Px [color=\"%s\"];\n",
                     task->recursive, task,
                     task->thread_index == task->recursive->thread_index
                     ? "purple" : "red") ;
      fputs((const char *)buffer, file) ;
      debug_graph_edge(task->recursive, file, indent) ;
    }

    /* Only show the schedule links if the schedule is up to date. It's just
       confusion otherwise. */
    if ( !need_recompute_schedule &&
         (next = DLL_GET_NEXT(task, task_t, order_link)) != NULL ) {
      VERIFY_OBJECT(next, TASK_NAME) ;
      debug_graph_indent(file, indent) ;
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "t%Px -> t%Px [color=\"green\"];\n",
                     task, next) ;
      fputs((const char *)buffer, file) ;
    }
  }
}

static void debug_graph_group_edges(task_group_t *group, FILE *file,
                                    unsigned int indent)
{
  uint8 buffer[DEBUG_GRAPH_BUFSIZE] ;

  if ( group->mark != task_visitor_mark ) {
    task_t *task ;
    task_group_t *child, *next ;

    group->mark = task_visitor_mark ;

    if ( group->join != NULL ) {
      debug_graph_indent(file, indent) ;
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "d%Px -> t%Px [ltail=\"cluster_%Px\",style=\"dashed\"",
                     group, group->join, group) ;
      fputs((const char *)buffer, file) ;
      if ( group->being_joined ) {
        fputs(",color=\"purple\"", file) ; /* Currently joining group */
      }
      fputs("];\n", file) ;
    }

    for ( task = DLL_GET_HEAD(&group->tasks, task_t, group_link) ;
          task != NULL ;
          task = DLL_GET_NEXT(task, task_t, group_link) ) {
      debug_graph_edge(task, file, indent) ;
    }

    for ( child = DLL_GET_HEAD(&group->children, task_group_t, peers) ;
          child != NULL ;
          child = DLL_GET_NEXT(child, task_group_t, peers) ) {
      debug_graph_group_edges(child, file, indent) ;
    }

    /* Only show the schedule links if the schedule is up to date. It's just
       confusion otherwise. */
    if ( !need_recompute_schedule &&
         (next = DLL_GET_NEXT(group, task_group_t, order_link)) != NULL ) {
      VERIFY_OBJECT(next, TASK_GROUP_NAME) ;
      debug_graph_indent(file, indent) ;
      (void)swncopyf(buffer, DEBUG_GRAPH_BUFSIZE, (uint8 *)
                     "d%Px -> d%Px [color=\"blue\"];\n",
                     group, next) ;
      fputs((const char *)buffer, file) ;
    }
  }
}

/** \brief Generate a graphviz-format graph of the task system. */
void debug_graph_tasks_locked(void)
{
  uint8 buffer[DEBUG_GRAPH_BUFSIZE] ;
  FILE *file ;

  HQASSERT(multi_mutex_is_locked(&task_mutex), "Task system not locked for graph") ;

  swcopyf(buffer, (uint8 *)"tasks-%d.dot", ++task_graph_count) ;
  if ( (file = fopen((const char *)buffer, "w")) != NULL ) {
    fputs("digraph G\n"
          "{\n"
          "  compound=true;\n"
          "  edge [fontname=\"Helvetica\",fontsize=10,labelfontname=\"Helvetica\",labelfontsize=10];\n"
          "  node [fontname=\"Helvetica\",fontsize=10,shape=record];\n",
          file) ;

    ++task_visitor_mark ;
    debug_graph_node(&interpreter_task, file, 1) ;
    debug_graph_group_nodes(&incomplete_task_group, file, 1) ;
    debug_graph_group_nodes(&orphaned_task_group, file, 1) ;

    ++task_visitor_mark ;
    debug_graph_edge(&interpreter_task, file, 1) ;
    debug_graph_group_edges(&incomplete_task_group, file, 1) ;
    debug_graph_group_edges(&orphaned_task_group, file, 1) ;

    fputs("}\n", file) ;

    fclose(file) ;
  } else {
    HQFAIL("Failed to open task graph file") ;
  }
}

void debug_graph_tasks(void)
{
  multi_mutex_lock(&task_mutex) ;
  debug_graph_tasks_locked() ;
  multi_mutex_unlock(&task_mutex) ;
}
#endif

/* Log stripped */
