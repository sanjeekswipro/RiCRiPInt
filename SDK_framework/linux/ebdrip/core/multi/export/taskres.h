/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!export:taskres.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Task API resource integration.
 */

#ifndef __TASKRES_H__
#define __TASKRES_H__

#include "hqatomic.h"
#include "objnamer.h"
#include "taskt.h"
#include "mm.h"
#include "lowmem.h"

/** \ingroup multi */
/** \{ */

/** \brief Task resource types. */
typedef enum {
  TASK_RESOURCE_LINE_OUT,  /**< Output line, in suitable bit-depth. */
  TASK_RESOURCE_BAND_1,    /**< Bitmap (1-bit) band. */
  TASK_RESOURCE_LINE_1,    /**< Bitmap (1-bit) line. */
  TASK_RESOURCE_BAND_CT,   /**< Modular halftone contone band. */
  TASK_RESOURCE_COMPOSITE_CONTEXT, /**< Compositing context. */
  TASK_RESOURCE_BACKDROP_RESOURCE, /**< Full-size backdrop resource. */
  TASK_RESOURCE_BACKDROP_BLOCK,    /**< Compressed backdrop block header. */
  TASK_RESOURCE_COMPRESS_DEVICE,   /**< Compress device handle. */
  TASK_RESOURCE_IMAGE_EXPANDER,    /**< Image expansion buffers. */
  TASK_RESOURCE_RLE_STATES,        /**< RLE states. */
  TASK_RESOURCE_BAND_SCRATCH, /**< Scratch band for PGBdev. */
  /* Also:
     Image store file descriptors
     DDA caches for shfills
     NFILL state caches
     Retained raster capture structures
     Halftone forms
     ...others?
   */
  /** \todo ajcd 2011-10-01: Output bands are currently last in the order,
      ironically because they're most likely to succeed. The limits for them
      will be higher with dynamic bands than most other resources, so rather
      than allocate them dynamically and then be likely to have to undo
      the allocation, we'll leave the allocation to the end. They can be
      moved back to their normal place when a better algorithm for avoiding
      multiple provisioning tries is implemented. A counter-argument could be
      made, that it's best to fail immediately if a very large allocation
      cannot be made. However, the other resources are more likely to fail on
      pool maximum limits, and so not even try allocation. */
  TASK_RESOURCE_BAND_OUT,  /**< Output band, in suitable bit-depth. */
  N_RESOURCE_TYPES  /**< Number of task resource types. Must be last enum. */
} resource_type_t ;

/** \brief Identifier for a particular resource instance, in a particular pool.

    Conceptually, when assigned to a task group, all resources are unnamed.
    When they are acquired by a task, the ID passed by the task is associated
    with the resource, and the resource is marked in use. When the resource
    is released by a task, its ID is retained but the resource is marked not
    in use. If another task requests the same ID, any unused resource with
    that ID will be provided; if none are available an unnamed resource will
    be provided; if none of those are available any unused resource with a
    different ID will be repurposed. The same ID can be used by multiple
    tasks simultaneously.

    This allows resources to be cached across band boundaries if possible,
    it allows tasks in a group to arrange to share the same resource across
    tasks by using the same ID for it. */
typedef intptr_t resource_id_t ;

/** \brief The invalid resource ID.

    This ID may not be used when fixing resources. */
enum {
  TASK_RESOURCE_ID_INVALID = -1
} ;

/** \brief Identifier for a particular resource pool instance, for a
    particular source and pool type.

    The resource pool key for simple resources will usually just be the size
    of the memory allocated for a resource. For complex resources, it may be
    the address of a copied or reference-counted structure containing
    parameters that are compared to match resource properties. The default
    comparison function for resource keys tests key identity. */
typedef intptr_t resource_key_t ;

/** \brief A single resource entry structure. */
typedef struct resource_entry_t resource_entry_t ;

/** \brief An array or hash table of resource entries. */
typedef struct resource_lookup_t resource_lookup_t ;

/** \brief A factory for resource pools. */
typedef struct resource_source_t resource_source_t ;

/** \brief A management structure to recycle and assign resources. */
typedef struct resource_pool_t resource_pool_t ;

/** \brief Possible values of the resource entry state field. */
enum {
  TASK_RESOURCE_FREE,    /**< Task resource is free to use. */
  TASK_RESOURCE_FIXME,   /**< Task resource needs fix() method called. */
  TASK_RESOURCE_FIXING,  /**< Task resource is being fixed. */
  TASK_RESOURCE_FIXED,   /**< Task resource is fixed. */
  TASK_RESOURCE_DETACHED /**< Task resource is detached from group. */
} ;

/** \brief A resource entry structure.

    Resource entries are stored in a lookup table in a resource pool, and
    assigned to a particular task group when \c task_resource_fix() is
    called. */
struct resource_entry_t {
  /** Is the resource entry in use by the owner? This should only be moved
      away from TASK_RESOURCE_FREE while the resource_pool_t::lookup table
      pointer is spinlocked. */
  hq_atomic_counter_t state ;
  resource_id_t id ;          /**< Resource ID (lazily attached). */
  /** Group, PGB, or other entity owning this resource. The owner pointer is
      used as a hint to implement locality of reference for resource lookups.
      Group provisioning and task fixing will take free resources from the
      closest ancestor group in preference to other free resources. The owner
      pointer may only be modified while the resource_pool_t::lookup table
      pointer is spinlocked. */
  /*@null@*/ void *owner ;
  /*@null@*/ void *resource ; /**< Resource or NULL. */
} ;

/** \brief Possible values of the resource requirement state. */
typedef enum {
  REQUIREMENT_NOW,    /**< Resource requirement is needed now. */
  REQUIREMENT_FUTURE, /**< Resource requirement is needed in future. */
  REQUIREMENT_NEVER   /**< Resource requirement is not counted against pool. */
} requirement_state_t ;

/** \brief Create a new resource requirement.

    \returns The new resource requirement reference, or NULL if the creation
             failed. If a resource requirement was created, the reference must
             be released with \c resource_requirement_release().

    New resource requirements are created in the not ready state, with an
    empty expression tree. An expression tree must be constructed before any
    task group can use the resources managed by the requirement. Until the
    resource requirement is made ready, the expression tree can be edited,
    maxima and minima may be increased or decreased, but task groups that use
    the requirement's expression tree will not have resources provisioned
    until the resource requirement is made ready (and therefore these task
    groups cannot be scheduled). */
/*@null@*/
resource_requirement_t *resource_requirement_create(void);

/** \brief Set the resource pool for a resource type, for this requirement.

    \param req      The resource requirement in which a pool is to be set.
    \param restype  The resource type for which the pool will be used.
    \param pool     The resource pool to be used in the requirement. This pool
                    reference is owned by this function, regardless of the
                    return value.

    \retval TRUE  The resource pool was changed successfully.
    \retval FALSE The resource pool was not changed successfully. This usually
                  indicates that the minimum resources for the requirements
                  could not be promised from the new pool before changing
                  over.
*/
Bool resource_requirement_set_pool(/*@in@*/ /*@notnull@*/
                                   resource_requirement_t *req,
                                   resource_type_t restype,
                                   /*@in@*/ /*@notnull@*/
                                   resource_pool_t *pool);

/** \brief Get the resource pool for a resource type, for this requirement.

    \param[in] req  The resource requirement to get a pool reference from.
    \param restype      The resource type for the pool to get.

    \returns Either NULL if there is no pool set for the resource type, or
    a reference to the resource pool. This reference must be released.
*/
/*@null@*/
resource_pool_t *resource_requirement_get_pool(/*@in@*/ /*@notnull@*/
                                               resource_requirement_t *req,
                                               resource_type_t restype) ;

/** \brief Acquire another reference to a resource requirement.

    \param[in] req  The requirement to acquire another reference for.

    \return Another pointer to the resource requirement. This pointer must be
            released using \c resource_requirement_release().
*/
/*@notnull@*/ /*@dependent@*/
resource_requirement_t *resource_requirement_acquire(/*@in@*/ /*@notnull@*/
                                                     resource_requirement_t *req);

/** \brief Release a reference to a resource requirement.

    \param[in] req  The requirement to release the reference to.

    All references acquired with \c resource_requirement_acquire need to be
    released. */
void resource_requirement_release(/*@notnull@*/ /*@in@*/
                                  resource_requirement_t **req);

/** \brief Copy a resource requirement tree.

    \param[in] req  The requirement to clone.
    \param state    The new resource requirement's state.

    \returns  A pointer to a new resource requirement, in \a state, or NULL
    if the requirement could not be cloned */
/*@null@*/
resource_requirement_t *resource_requirement_copy(/*@notnull@*/ /*@in@*/
                                                  resource_requirement_t *req,
                                                  requirement_state_t state) ;

/** \brief Make a resource requirement ready for provisioning task groups.

    \param[in] req  A resource requirement to make ready.
    \param state    The new resource requirement state.

    \retval TRUE  The resource requirement state change was made successfully.
    \retval FALSE The resource requirement could not change state successfully.

    Until the resource requirement is made current, its expression tree may be
    modified, maxima and minima may be increased or decreased, but no task
    groups using the resource requirement will be provisioned. */
Bool resource_requirement_set_state(/*@in@*/ /*@notnull@*/
                                    resource_requirement_t *req,
                                    requirement_state_t state) ;

/** \brief Test if resource requirement is ready for provisioning task groups.

    \param[in] req  A resource requirement to make ready.

    \retval TRUE  The resource requirement is ready for provisioning groups.
    \retval FALSE The resource requirement is not ready for provisioning groups.

    Until the resource requirement is made ready, its expression tree may be
    modified, maxima and minima may be increased or decreased, but no task
    groups using the resource requirement will be provisioned. */
Bool resource_requirement_is_ready(/*@in@*/ /*@notnull@*/
                                   const resource_requirement_t *req) ;

/* Make sure the requirements ids for group requirements match the group type
   ids. */
#define EXPR_ID_FOR_GROUP(x) REQUIREMENTS_ ## x ## _GROUP = TASK_GROUP_ ## x,

/** \brief Requirement node IDs.

    These are used for finding the expression in the resource requirement tree.
    They should be unique for expression nodes that are re-discoverable. */
typedef enum {
  TASK_GROUP_TYPES(EXPR_ID_FOR_GROUP) /* Expression ID for every task group */
  REQUIREMENTS_ROOT,      /**< Root of requirements tree. */
  REQUIREMENTS_PAGELIMIT, /**< Min/Max Limits of resources usable by page. */
  REQUIREMENTS_PAGEADD,   /**< Page additions to band rendering resources. */
  REQUIREMENTS_CALCULATE, /**< Unnamed intermediate calculation expression. */
  N_REQUIREMENT_NODE      /**< Must be last in expression ID enum. */
} requirement_node_id ;

/** \brief Types of requirement tree node.

    Resource requirements contain an expression tree. Nodes may be added to the
    tree, updated, and then the entire tree re-evaluated. Nodes of particular
    types can be searched for within the tree, so that clients do not need to
    retain references to all of the nodes within the tree. The operations
    provided in the tree are such that when evaluated, the top-level nodes
    will contain the minimum and maximum resource requirements for the pools. */
typedef enum {
  /* Zero operand nodes: */
  REQUIREMENT_OP_VALUES,    /**< Provided values. */
  /* Two operand nodes: */
  REQUIREMENT_OP_SUM,       /**< Add children's resources together. */
  REQUIREMENT_OP_MAX,       /**< Maximum of children's resources. */
  REQUIREMENT_OP_LIMIT,     /**< No less than right min, no more than right max. */
  N_REQUIREMENT_OP          /**< Must be last in resource op enum. */
} requirement_op_t ;

/** \brief Create a requirement tree node.

     \param[in] req The resource requirement to which this expression node
                        will be attached.
     \param id  The identifier of a requirement tree node.
     \param op  The operand for a requirement tree node.

     \returns The requirement tree node created, or NULL if the node could
              not be created. This function does not acquire any references to
              \a req.

     The requirement tree node will be created at the first insertion
     point in a right-left depth-first traversal of the requirement's
     expression tree. If no such insertion point exists, an assert will be
     raised.

     \note There is no function to delete requirement tree nodes. They are
     reclaimed automatically when the resource requirement is freed. The
     resource requirement will be freed when all explicit reference to it are
     released, and when all task groups referencing its expression tree
     are complete. */
/*@null@*/
requirement_node_t *requirement_node_create(/*@in@*/ /*@notnull@*/
                                            resource_requirement_t *req,
                                            requirement_node_id id,
                                            requirement_op_t op) ;

/** \brief Find a named expression node within a requirement's expression
    tree.

    \param[in] req The resource requirement in which the expression node is
                       searched.
    \param id          The expression node identifier to search for.

    \returns A pointer to an expression node matching the \a id, or NULL.
             Expression node ids should be made unique, since this function
             does not guarantee to traverse the expression tree in any
             particular order.
*/
/*@null@*/ /*@dependent@*/
requirement_node_t *requirement_node_find(/*@notnull@*/ /*@in@*/
                                          resource_requirement_t *req,
                                          requirement_node_id id);

/** \brief Get the minimum value from a requirement tree.

    \param[in] expr   The requirement tree to get the minimum value.
    \param restype    The resource type for the expression minimum to get.

    \returns The minimum value from the requirement tree node.
*/
unsigned int requirement_node_minimum(/*@in@*/ /*@notnull@*/
                                      requirement_node_t *expr,
                                      resource_type_t restype) ;

/** \brief Get the maximum value from a requirement tree.

    \param[in] expr   The requirement tree to get the maximum value.
    \param restype    The resource type for the expression minimum to get.

    \returns The maximum value from the requirement tree node.
*/
unsigned int requirement_node_maximum(/*@in@*/ /*@notnull@*/
                                      requirement_node_t *expr,
                                      resource_type_t restype) ;

/** \brief Set the minimum value in a requirement tree node.

    \param[in] expr The expression node which is to be changed.
    \param restype  The resource type to change.
    \param minimum  The new minimum value. While the resource requirement
                    associated with this expression is not ready, minimum
                    values may be increased or decreased. Once the resource
                    requirement is made ready, minimum values may only be
                    decreased.

    \retval FALSE If the change resulted in a resource allocation failure. In
                  this case, the previous minimum is re-instated.
    \retval TRUE  If the minimum value was successfully changed.

    \note This may trigger a re-evaluation of the expression tree, and result
    in allocating new resources from a source.

    \note If resource requirements are reduced after a requirement is made
    ready, it is the caller's responsibility to ensure that no task groups
    that might be affected are currently provisioned and have fixed
    resources. */
Bool requirement_node_setmin(/*@notnull@*/ /*@in@*/ requirement_node_t *expr,
                             resource_type_t restype,
                             unsigned int minimum) ;

/** \brief Require a minimum value in a requirement tree node.

    \param[in] expr The expression node which is to be required.
    \param restype  The resource type to require.
    \param minimum  The minimum value to require. While the resource requirement
                    associated with this expression is not ready, minimum
                    values may be increased.

    \retval FALSE If the change resulted in a resource allocation failure. In
                  this case, the previous minimum is re-instated.
    \retval TRUE  If the minimum value was successfully changed.

    This function will only increase minimum resource requirements. If the
    existing minimum requirements are greater than \a minimum, it will return
    \c TRUE without modifying the resource requirements.

    \note This may trigger a re-evaluation of the expression tree, and result
    in allocating new resources from a source. */
Bool requirement_node_maxmin(/*@notnull@*/ /*@in@*/ requirement_node_t *expr,
                             resource_type_t restype,
                             unsigned int minimum) ;

/** \brief Set the maximum value in a requirement tree node.

    \param[in] expr The expression node which is to be changed.
    \param restype  The resource type to change.
    \param maximum  The new maximum value.

    \retval FALSE If the change resulted in a resource allocation failure. In
                  this case, the previous maximum is re-instated.
    \retval TRUE  If the minimum value was successfully changed.

    \note This may trigger a re-evaluation of the expression tree, and result
    in allocating new resources from a source.

    \note If resource requirements are reduced after a requirement is made
    ready, it is the caller's responsibility to ensure that no task groups
    that might be affected are currently provisioned and have fixed
    resources. */
Bool requirement_node_setmax(/*@notnull@*/ /*@in@*/ requirement_node_t *expr,
                             resource_type_t restype,
                             unsigned int maximum) ;

/** \brief Limit the maximum value in a requirement tree node.

    \param[in] expr The expression node which is to be limited.
    \param restype  The resource type to limited.
    \param minimum  The maximum value to limit. While the resource requirement
                    associated with this expression is not ready, maximum
                    values may be decreased.

    \retval FALSE If the maximum value was not successfully changed.
    \retval TRUE  If the maximum value was successfully changed.

    This function will only decrease maximum resource requirements. This
    function will never reduce the minimum resource requirements. If the
    existing maximum requirements are less than \a maximum, it will return \c
    TRUE without modifying the resource requirements.

    \note This may trigger a re-evaluation of the expression tree, and result
    in allocating new resources from a source. */
Bool requirement_node_minmax(/*@notnull@*/ /*@in@*/ requirement_node_t *expr,
                             resource_type_t restype,
                             unsigned int maximum) ;


/** \brief Special value of the simultaneous sub-requirements to indicate they
    should only be limited per thread. */
#define REQUIREMENT_SIMULTANEOUS_THREADS MAXUINT

/** \brief Ensure a minimum number of simultaneous groups can be provisioned
    from a requirement node.

    \param[in] expr The expression node which is to be changed.
    \param minimum  A minimum number of simultaneous groups to provision. If
                    this is REQUIREMENT_SIMULTANEOUS_THREADS, the maximum
                    number of threads will be used.

    \retval FALSE If the change resulted in a resource allocation failure. In
                  this case, the previous minimum is re-instated.
    \retval TRUE  If the minimum value was successfully changed.

    \note This may trigger a re-evaluation of the expression tree, and result
    in allocating new resources from a source. */
Bool requirement_node_simmin(/*@notnull@*/ /*@in@*/ requirement_node_t *expr,
                             unsigned int minimum) ;

/** \brief Ensure a maximum number of simultaneous groups can be provisioned
    from a requirement node.

    \param[in] expr The expression node which is to be changed.
    \param maximum  A maximum number of simultaneous groups to provision. If
                    this is REQUIREMENT_SIMULTANEOUS_THREADS, the maximum of
                    the number of threads or the minimum limit will be used.

    \retval FALSE If the change resulted in a resource allocation failure. In
                  this case, the previous maximum is re-instated.
    \retval TRUE  If the maximum value was successfully changed.

    \note This may trigger a re-evaluation of the expression tree, and result
    in allocating new resources from a source. */
Bool requirement_node_simmax(/*@notnull@*/ /*@in@*/ requirement_node_t *expr,
                             unsigned int maximum) ;

/** \brief Resource pools are used to fulfill resource requirements.

    Resource pools provide a sub-classable method to construct complex
    resource objects and recycle them. Resource pool methods are provided to
    determine the size of a single resource, to construct resources from a
    source, to return them to the source, to assign them to groups.

    We expect to have a resource pool creation function for each pool type,
    which will construct the pool object, create any private data for the
    pool instance, attach the pool to a source. Note that the source need not
    be provisioned at this stage.

    Resource pools must not be shared between resources of different types,
    even if they use the same functions and draw from the same source. The
    minimum, maximum, and number of resources available are calculated for
    each resource type separately, and sharing pools would result in incorrect
    calculation and allocation of resources. */
struct resource_pool_t {
  hq_atomic_counter_t refcount ; /**< # references to this pool. */
  resource_type_t type ;         /**< Type of resources this pool */
  resource_source_t *source ;    /**< Source for objects in this pool (refcounted). */
  void *data ;                   /**< Private data for this pool instance. */
  /** Link in list of pools sharing the same source. This pointer should only
      be dereferenced or updated when the resource_source_t::pool_list
      pointer is spinlocked. */
  resource_pool_t *pool_list ;
  /* The nresources, nprovided, minimum, maximum, promisemin and promisemax
     fields should only be changed whilst the lookup table pointer is
     spinlocked. */
  unsigned int nresources ;      /**< Number of resources in this pool. */
  /* We maintain the following invariants. Any time the lookup table lock is
     taken or released:

     minimum <= maximum
     promisemin <= promisemax
     minimum <= nresources
     nprovided + ndetached <= nresources
     nprovided <= maximum

     Note that we don't maintain nresources <= maximum. This is because we
     may want to keep resources for future use. Any resources over the larger
     of maximum and promisemax will be eagerly reclaimed by the low-memory
     handler.
  */
  unsigned int nprovided ;       /**< Number of resources provided to groups. */
  unsigned int ndetached ;       /**< Number of resources fully detached. */
  /* We should be maintaining nresources >= minimum at all times. */
  unsigned int minimum ;         /**< Minimum resources required now. */
  unsigned int maximum ;         /**< Maximum resources useful now. */
  /* Unless in desperate low memory, we should be maintaining
     nresources >= promisemin. */
  unsigned int promisemin ;      /**< Minimum resources required in future. */
  unsigned int promisemax ;      /**< Maximum resources useful in future. */
  resource_key_t key ;           /**< Key used to compare pool instances. */
  /** TRUE if entries from this pool can be cached across unfix/fix cycles. */
  Bool cache_unfixed ;
  /** Resources assigned to this pool. The lookup table may be NULL if the
      pool minimum is zero, even if the pool maximum is greater than zero.
      This pointer can only be dereferenced under spinlock control. */
  resource_lookup_t *lookup ;
  /** \brief Construct a preallocated (undiscriminated) resource instance in
      a newly-allocated resource entry.

      \param[in] pool       The resource pool to preallocate an entry for.
      \param[in,out] entry  A resource pool entry to fill in.
      \param[in] cost       The allocation cost for the entry.

      \retval TRUE  The resource entry was filled in with details of the
                    resource.
      \retval FALSE A resource could not be preallocated.

      This method is used to preallocate the minimum resources required when
      updating resource requirements. It may either allocate a resource
      immediately, or if the source can ensure a resource will be available
      on demand, it may fill in the resource entry with a marker that will
      allow the \c fix method to discover the correct resource.

      This method is required. The resource source's \c make_pool method
      should ensure that a suitable function is provided, there is no
      suitable default function.

      This method is called with the resource_pool_t::lookup table pointer
      spinlocked. It may only block on memory management system locks. */
  Bool (*alloc)(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
                /*@notnull@*/ /*@in@*/ resource_entry_t *entry,
                /*@notnull@*/ /*@in@*/ mm_cost_t cost) ;
  /** \brief Free a resource instance, returning to the source.

      \param[in] pool     The resource pool to free an entry for.
      \param[in] entry    A resource pool entry to clear out.

      This method is called with the resource_pool_t::lookup table pointer
      spinlocked. It may only block on memory management system locks. */
  void (*free)(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
               /*@notnull@*/ /*@in@*/ resource_entry_t *entry) ;
  /** \brief Compare a key against this pool to see if matches.

      \param[in] pool  The resource pool to compare.
      \param key       The resource pool key to compare against.

      \retval TRUE  The resource pool keys match, so the resource pool can be
                    shared with another page.
      \retval FALSE The resource pool keys do not match, so the resource pool
                    cannot be shared.

      A default method is provided for new pools by \c resource_pool_get(),
      which compares keys using identity. The source's \c make_pool
      constructor function may override this method.

      This method will be called with the resource_source_t::pool_list
      pointer spinlocked. It should not block on any lock. */
  Bool (*compare)(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
                  resource_key_t key) ;
  /** \brief Fix a resource entry to a particular ID.

      \param[in] pool       The resource pool containing the entry.
      \param[in,out] entry  The resource entry to fix.

      Some resource pools need to know the ID requested before determining
      the address of the resource to return (e.g., the framebuffer band pool
      will calculate the resource address based on the band number, which
      is used as the resource ID).

      If present, this method is called just before returning the resource
      address for an entry. It must not modify any field in \c entry except
      for the resource address. This method is optional, if not supplied the
      resource address in the \c entry will be used. */
  void (*fix)(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
              /*@notnull@*/ /*@in@*/ resource_entry_t *entry) ;
  /** Return the amount of memory used by a resource entry.

      This routine is used to calculate the size of resource entries offered
      to and taken by the low memory handler. A specific resource entry
      pointer is provided, even though resource instances in a pool are
      expected to be the same size, because it's often easier to calculate
      sizes using a traversal over an instantiated structure. Memory alignment
      can also be taken into account in the entry size.

      \param[in] pool       The resource pool containing the entry.
      \param[in] entry      The resource entry to determine the size of.

      \returns The size of a particular resource entry, or 0 if the particular
               entry should not be offered or freed by the low memory handler.

      If this method is not present, the resource source's low-memory handler
      ignore this pool. */
  size_t (*entry_size)(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
                       /*@null@*/ /*@in@*/ const resource_entry_t *entry);
  /** Destructor for this pool, called when the reference count reaches zero.

      \param[in,out] pool  The resource pool to destroy.

      This method should dispose of any allocated data and references taken
      by the pool. It should not unname the pool object itself, nor
      change its reference count, nor free the memory holding the pool:
      these functions will be performed by the caller.

      A default method is provided for new pools by \c resource_pool_get(),
      which does nothing. The source's \c make_pool constructor function may
      override this method. */
  void (*finish)(resource_pool_t *pool) ;

  OBJECT_NAME_MEMBER
} ;

#define RESOURCE_POOL_NAME "Task resource pool"

/** \brief Find or create a reference-counted resource pool.

    \param[in] source  A source factory for resource pools
    \param restype     The type of resource pool to find.
    \param key         Key identifying the resources in the pool.

    \return A pointer to a resource pool. This reference must be
            released using resource_pool_release().
*/
/*@null@*/
resource_pool_t *resource_pool_get(/*@in@*/ /*@notnull@*/
                                   resource_source_t *source,
                                   resource_type_t restype,
                                   resource_key_t key) ;

/** \brief Acquire a new reference to a resource pool.

    \param[in] pool  A resource pool.

    \return Another pointer to the resource pool. This pointer must be
            released using \c resource_pool_release().
*/
/*@notnull@*/ /*@dependent@*/
resource_pool_t *resource_pool_acquire(/*@in@*/ /*@notnull@*/
                                       resource_pool_t *pool) ;

/** \brief Release a reference-counted resource pool.

    \param[in,out] poolptr  A pointer to the pool to release.

    On exit, the pool pointer is set to NULL. If the last reference count to
    the pool was released, the pool will be destroyed. */
void resource_pool_release(/*@notnull@*/ /*@in@*/ resource_pool_t **poolptr) ;

/** \brief Iterate over all entries in a resource pool.

    \param[in] pool      The pool to iterate over.
    \param id            The starting ID to iterate from.
    \param[in] callback  A function to call for each resource entry.
    \param[in] data      Data to pass to the callback function.

    \retval TRUE  If none of the callbacks returned FALSE.
    \retval FALSE If a callback returned FALSE.

    The resource pool's lookup table is locked during the pool iteration,
    so callback functions should not block, and should be very brief.
    This function must not be used recursively on the same or on
    another pool. The callback function should take care to filter out
    resource entries by owner and that are in use or not appropriately. */
Bool resource_pool_forall(/*@notnull@*/ /*@in@*/ resource_pool_t *pool,
                          resource_id_t id,
                          Bool (*callback)(/*@notnull@*/ /*@in@*/
                                           resource_pool_t *pool,
                                           /*@notnull@*/ /*@in@*/
                                           resource_entry_t *entry,
                                           /*@null@*/ void *data),
                          /*@null@*/ void *data) ;

/** \brief Source of fulfillment for task resources.

    Sources are expected to be shared between multiple resource pools. */
struct resource_source_t {
  hq_atomic_counter_t refcount ; /**< # references to this source. */
  /** List of resource pools based on this source. This pointer should only
      be dereferenced under spinlock control. While this pointer is
      spinlocked, no other locks should be taken. */
  resource_pool_t *pool_list ;
  void *data ;                   /**< Private data for this source. */
  /** \brief Construct a pool for use with this source.

      \param[in,out] source  The resource source.
      \param[in,out] pool    The resource pool under construction construct.

      \retval TRUE  Resource pool construction completed successfully.
      \retval FALSE Resource pool construction failed. The pool's finish
                    method will be called even if pool construction failed,
                    so care should be take to ensure it is either not
                    installed in the pool definition until the pool is fully
                    constructed, or that it can handle partially constructed
                    resource pools.

      This method finishes construction of a resource pool object for use
      with this source. This method should not change the source or pool
      reference counts, nor attach the pool to the source: these functions
      will be performed by the caller.

      This method is required for all sources. */
  Bool (*make_pool)(/*@notnull@*/ /*@in@*/ /*@out@*/ resource_source_t *source,
                    /*@notnull@*/ /*@in@*/ /*@out@*/ resource_pool_t *pool) ;
  /** \brief Destructor for this source, called if the reference count
      reaches zero.

      \param[in,out] source  The resource source to destroy.

      This method should dispose of any allocated data and references taken
      by the source. It should not unname the source object itself, nor
      change its reference count, nor free the memory holding the source:
      these functions will be performed by the caller.

      This method is required, unless the source is never released. */
  void (*finish)(/*@notnull@*/ /*@in@*/ resource_source_t *source) ;

  /** \brief A function to return the MM pool used by this source.

      \param[in] source  The resource source to return an MM pool for.

      \returns The MM pool pointer to use for low-memory handling.

      If this method does not exist, the source will not be registered
      for low-memory handling. */
  /*@notnull@*/ /*@dependent@*/
  mm_pool_t (*mm_pool)(/*@notnull@*/ /*@in@*/ const resource_source_t *source) ;

  /** A memory_tier_ram low-memory handler for this source. */
  low_mem_handler_t low_handler_ram;
  /** A memory_tier_reserve low-memory handler for this source. */
  low_mem_handler_t low_handler_reserve;
  /** Max offers for any resource low-memory tier. */
#define N_RESOURCE_LOW_OFFERS 3
  /** Offers used by the low-memory handler. */
  low_mem_offer_t low_offer[N_RESOURCE_LOW_OFFERS];

  OBJECT_NAME_MEMBER
} ;

#define RESOURCE_SOURCE_NAME "Task resource source"

/** \brief Register a resource source for low-memory handling.

    \param[in] source  The resource source to register for low-memory handling.

    \retval TRUE  If the resource source was successfully registered.
    \retval FALSE If the resource source was not registered for low-memory
                  handling.

    If the \c resource_source_t::mm_pool method does not exist, this function
    will return TRUE, but the source will not release any memory.
 */
Bool resource_source_low_mem_register(/*@in@*/ /*@out@*/ /*@notnull@*/
                                      resource_source_t *source) ;

/** \brief De-register a resource source from low-memory handling.

    \param[in] source  The resource source to de-register from low-memory
                       handling.

    This function can be called on a resource source safely even if it
    not been registered for low-memory handling. */
void resource_source_low_mem_deregister(/*@in@*/ /*@notnull@*/
                                        resource_source_t *source) ;

/** \brief Lock down multiple resources for use by this task group.

    \param restype  The type of resources to acquire.
    \param ntofix   The number of resources to acquire. This must be greater
                    than zero.
    \param ids      An array of \a ntofix resource IDs for the resource
                    instances to acquire. The resource IDs must all be
                    different.
    \param[out] resources
                    An array of \a ntofix pointers, to receive the resource
                    instance pointers. The interpretation of these pointers
                    depends on the resource type.
    \param[out] hit An array of \a ntofix booleans, indicating whether the
                    corresponding resource matched the ID and type of a
                    pre-existing resource entry. This may be used to assist
                    cache matching. This array may be omitted (NULL) if the
                    caching information is not required.

    \retval TRUE  If the operation succeeded, in which case \a resources will
                  contain the resources fixed for use with this task group.
                  The contents of the \a resources and \a hit arrays are only
                  valid in this case.
    \retval FALSE If the operation failed. No new resources are fixed. The
                  \a resources and \a hit array entries will be valid for any
                  resources previously fixed by tasks in the group, invalid for
                  any new resources requested.

    This function tries to find several resource with the same type and ID in
    the current task group's lookup table. If suitable entries do not exist,
    free resource of the appropriate type will be found or allocated if
    possible.

    The fixing of a resource to a particular ID lasts for the lifetime of the
    lookup table (until the task group assigned the resources is joined).
    Within that scope, requesting the same ID and type will return the same
    resource, in the same state that it was previously used, unless the
    resource is explicitly unfixed. Resource entries may be unfixed
    explicitly so they can be re-used with different IDs by calling \c
    task_resource_unfix().

    The tasks in a group should coordinate resource usage patterns, to ensure
    that they unfix resources that will be recycled with different IDs, and
    that they do not fix resources that they have not been assigned.

    This function will always succeed while the number of resources fixed by
    a group is less than the minimum required by the group. Between the
    minimum and maximum number of resources, it may fail depending on the
    memory state of the RIP, so clients should be prepared to handle failure
    in that case. Task groups will be provisioned with enough resources for
    the minimum simultaneous number of resources required by its tasks before
    any tasks are scheduled.

    The failure semantics allow a client to incrementally add resource IDs to
    the \a ids array, calling this function with larger values of \a ntofix
    until it fails, and the \a resources pointers for the last successful
    call will be valid. The \a hit array values will only be useful for the
    first call which fixes a particular set of resources.

    \note The creator of the task group must make sure that \c
    task_group_new() is passed a resource requirement with the minimum number
    of any resource that the group's tasks will use simultaneously. */
Bool task_resource_fix_n(resource_type_t restype, unsigned int ntofix,
                         /*@notnull@*/ /*@in@*/ const resource_id_t ids[],
                         /*@notnull@*/ /*@in@*/ void *resources[],
                         /*@null@*/ /*@out@*/ Bool hit[]) ;

/** \brief Get a resource that was assigned to this task group.

    \param restype  The type of resource to acquire.
    \param id       The ID of the resource instance to acquire.
    \param[out] hit An indicator whether the resource matched the ID and
                    type of a pre-existing resource. This may be used to assist
                    cache matching.

    \return A pointer to the resource, or NULL if no resource could be fixed.
            The interpretation of this pointer depends on the resource type.

    This function tries to find a resource with the same type and ID in
    the current task group's lookup table. If an entry does not exist, a
    free resource of the appropriate type will be found, and its ID will be
    fixed.

    The fixing of a resource to a particular ID lasts for the lifetime of the
    lookup table (until the task group assigned the resources is joined).
    Within that scope, requesting the same ID and type will return the same
    resource, in the same state that it was previously used, unless the
    resource is explicitly unfixed. Resource entries may be unfixed
    explicitly so they can be re-used with a different IDs by calling \c
    task_resource_unfix().

    The tasks in a group should coordinate resource usage patterns, to ensure
    that they unfix resources that will be recycled with different IDs, and
    that they do not fix resources that they have not been assigned.

    This function will always succeed while the number of resources fixed by
    a group is less than the minimum required by the group. Between the
    minimum and maximum number of resources, it may fail depending on the
    memory state of the RIP, so clients should be prepared to handle failure
    in that case. Task groups will be provisioned with enough resources for
    the minimum simultaneous number of resources required by its tasks before
    any tasks are scheduled.

    \note The creator of the task group must make sure that \c
    task_group_new() is passed a resource requirement with the minimum number
    of any resource that the group's tasks will use simultaneously. */
void *task_resource_fix(resource_type_t restype, resource_id_t id,
                        /*@null@*/ /*@out@*/ Bool *hit) ;

/** \brief Release a resource that has been fixed back to the task group set.

    \param restype       The type of resource being released.
    \param id            The ID of the resource instance to unfix. This is
                         the starting location for the search, the resource
                         instance will be searched for if it is not found with
                         this ID.
    \param[in] resource  The resource pointer, as returned by
                         task_resource_fix().
*/
void task_resource_unfix(resource_type_t restype, resource_id_t id,
                         /*@in@*/ void *resource) ;

/** \brief Detach a fixed resource from the task group set.

    \param restype       The type of resource being detached.
    \param id            The ID of the resource instance to detach. This is
                         the starting location for the search, the resource
                         instance will be searched for if it is not found with
                         this ID.
    \param[in] resource  The resource pointer, as returned by
                         task_resource_fix().

    Resources detached from a task group will not be used by. This state
    survives the lifetime of the task group in which the resource was fixed,
    so care must be taken to ensure that detached resources are explicitly
    returned to the resource pool. Within the same task group, detached
    resources may be re-fixed using \c task_resource_fix(), or unfixed using
    \c task_resource_unfix. After completion of the task group, detached
    resources may be only returned to the pool with an explicit \c
    task_resource_unfix(). */
void task_resource_detach(resource_type_t restype, resource_id_t id,
                          /*@in@*/ void *resource) ;

/** \} */

#endif /* __TASKRES_H__ */

/* Log stripped */
