/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!src:taskres.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Task scheduling resource integration.
 */

#include "core.h"
#include "hqatomic.h"
#include "hqmemset.h"
#include "mm.h"
#include "multipriv.h"
#include "swerrors.h"
#include "taskh.h"
#include "taskres.h"
#include "lowmem.h"
#include "hsiehhash32.h"
#include "hqspin.h" /* Must be last include because of hqwindows.h */


/** \brief Atomically change min/max for a resource pool, ensuring enough
    resources are available.

    \param[in] pool  The resource pool to update.
    \param state     The requirement state for the new and old values.
    \param oldmin    The old minimum value of an expression
    \param newmin    The new minimum value of the expression
    \param oldmax    The old maximum value of an expression
    \param newmax    The new maximum value of the expression

    \retval TRUE  If the resource pool min and max were updated, and all
                  resources required by the update were allocated.
    \retval FALSE If the resource pool was not updated.
*/
static Bool resource_pool_update(/*@in@*/ /*@notnull@*/ resource_pool_t *pool,
                                 requirement_state_t state,
                                 unsigned int oldmin, unsigned int newmin,
                                 unsigned int oldmax, unsigned int newmax) ;

/*****************************************************************************/
/* Resource requirements. */

resource_requirement_t *resource_requirement_create(void)
{
  resource_requirement_t *req ;

  HQASSERT(mm_pool_task, "Task pool not active") ;

  if ( (req = mm_alloc(mm_pool_task, sizeof(*req),
                       MM_ALLOC_CLASS_RESOURCE_REQUIREMENT)) != NULL ) {
    HqMemZero(req, sizeof(*req)) ;
    req->refcount = 1 ;
    req->state = REQUIREMENT_FUTURE ;
    req->mark = requirement_mark_init() ;
    NAME_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;

#if defined(METRICS_BUILD)
    /** \todo ajcd 2011-08-24: These metrics not atomic w.r.t. readout. */
    ++requirements_total_allocated ;
    ++requirements_current_allocated ;
    if ( requirements_current_allocated > requirements_peak_allocated )
      requirements_peak_allocated = requirements_current_allocated ;
#endif
  } else {
    (void)error_handler(VMERROR) ;
  }

  return req ;
}

resource_requirement_t *resource_requirement_acquire(resource_requirement_t *req)
{
  if ( req != NULL ) {
    hq_atomic_counter_t before ;
    VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;
    HqAtomicIncrement(&req->refcount, before) ;
    HQASSERT(before > 0, "Resource requirement had been previously released") ;
  }
  return req ;
}

/** \brief Remove a requirement's resource pool reference for a resource
    type. */
static void resource_requirement_remove_pool(resource_requirement_t *req,
                                             requirement_node_t *root,
                                             resource_type_t restype)
{
  VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;

  ASSERT_NODE_LOCKED(req, TRUE /*locked*/,
                     "Need resource expression lock to remove pool") ;

  if ( req->pools[restype] != NULL ) {
    /* We're either removing a pool reference because we're destroying the
       requirement, or because it's ready and the minimum value is zero. */
    HQASSERT(req->refcount == 0 ||
             (req->state == REQUIREMENT_NOW && root->minimum[restype] == 0),
             "Removing resource pool that is in use") ;

    if ( !resource_pool_update(req->pools[restype], req->state,
                               root->minimum[restype], 0,
                               root->maximum[restype], 0) )
      HQFAIL("Shouldn't have problem reducing resource pool min/max") ;

    resource_pool_release(&req->pools[restype]) ;
  }
}

Bool resource_requirement_set_pool(resource_requirement_t *req,
                                   resource_type_t restype,
                                   resource_pool_t *pool)
{
  requirement_node_t *node ;
  Bool result = TRUE ;

  VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  HQASSERT(req->state != REQUIREMENT_NOW,
           "Cannot change pool once a requirement is made ready") ;

  /* This pool reference is only updated by this routine and by
     resource_requirement_remove_pool(), and all calls to these are
     serialised by the interpreter or the top-level render task. */
  requirement_node_lock(req, node) ;
  if ( pool != req->pools[restype] ) {
    result = resource_pool_update(pool, req->state,
                                  0, node->minimum[restype],
                                  0, node->maximum[restype]) ;
    if ( result ) {
      /* Don't use resource_requirement_remove_pool because of assertion
         for removal conditions. */
      if ( req->pools[restype] != NULL ) {
        if ( !resource_pool_update(req->pools[restype], req->state,
                                   node->minimum[restype], 0,
                                   node->maximum[restype], 0) )
          HQFAIL("Shouldn't have problem reducing resource pool min/max") ;
        resource_pool_release(&req->pools[restype]) ;
      }
      req->pools[restype] = pool ;
    } /* else no change was made to new or old pool. */
  } else { /* Same as existing pool, so nothing to do. */
    resource_pool_release(&pool) ;
  }
  requirement_node_unlock(req, node) ;

  return result ;
}

resource_pool_t *resource_requirement_get_pool(resource_requirement_t *req,
                                               resource_type_t restype)
{
  requirement_node_t *node ;
  resource_pool_t *pool ;

  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;

  requirement_node_lock(req, node) ;
  if ( (pool = req->pools[restype]) != NULL )
    pool = resource_pool_acquire(pool) ;
  requirement_node_unlock(req, node) ;

  return pool ;
}

void resource_requirement_release(resource_requirement_t **preq)
{
  hq_atomic_counter_t after ;
  resource_requirement_t *req = *preq ;

  HQASSERT(preq != NULL, "Nowhere to find resource requirement") ;
  req = *preq ;
  VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;
  HqAtomicDecrement(&req->refcount, after) ;
  HQASSERT(after >= 0, "Resource requirement already released") ;
  if ( after == 0 ) {
    requirement_node_t *root, *node ;
    resource_type_t restype ;

#if defined(METRICS_BUILD)
    --requirements_current_allocated ;
#endif

    requirement_node_lock(req, root) ;
    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype )
      resource_requirement_remove_pool(req, root, restype) ;
    /* Unlocking with NULL leaves us in sole possesion of the expression
       tree. */
    requirement_node_unlock(req, NULL) ;

    /* Destroy requirement expression tree iteratively. */
    while ( (node = root) != NULL ) {
      /* If the root node has a left child, rotate the tree right. If not,
         free the root, and replace with the right child. */
      requirement_node_t *left = node->left ;

      if ( left != NULL ) { /* Rotate root right */
        node->left = left->right ;
        left->right = node ;
        root = left ;
      } else { /* Destroy root and select right. */
        root = node->right ;
        UNNAME_OBJECT(node) ;
        mm_free(mm_pool_task, node, sizeof(*node)) ;
      }
    }

    UNNAME_OBJECT(req) ;
    mm_free(mm_pool_task, req, sizeof(*req)) ;
  }

  *preq = NULL ;
}

static Bool requirement_node_copy(requirement_node_t *node,
                                  resource_requirement_t *copy)
{
  do {
    resource_type_t restype ;
    requirement_node_t *newnode ;

    if ( (newnode = requirement_node_create(copy, node->id, node->op)) == NULL )
      return FALSE ;

    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      newnode->minimum[restype] = node->minimum[restype] ;
      newnode->maximum[restype] = node->maximum[restype] ;
    }

    if ( node->right != NULL && !requirement_node_copy(node->right, copy) )
      return FALSE ;

    /* Tail call elimination for left branch */
    node = node->left ;
  } while ( node != NULL ) ;

  return TRUE ;
}

resource_requirement_t *resource_requirement_copy(resource_requirement_t *req,
                                                  requirement_state_t state)
{
  resource_requirement_t *copy ;
  resource_type_t restype ;

  VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;

  if ( (copy = resource_requirement_create()) == NULL )
    return NULL ;

  /* Create the resource requirement in never state, so release won't count
     pool max/min. The final action will be to set the state to the intended
     state. */
  copy->state = REQUIREMENT_NEVER ;

  for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
    if ( req->pools[restype] != NULL )
      copy->pools[restype] = resource_pool_acquire(req->pools[restype]) ;
  }

  if ( !requirement_node_copy(req->node, copy) ||
       !resource_requirement_set_state(copy, state) )
    resource_requirement_release(&copy) ;

  return copy ;
}

Bool resource_requirement_set_state(resource_requirement_t *req,
                                    requirement_state_t state)
{
  Bool result = FALSE ;
  requirement_node_t *node ;

#define return DO_NOT_return_goto_cleanup_INSTEAD!
  requirement_node_lock(req, node) ;

  if ( req->state != state ) {
    resource_type_t restype ;

    /* Phase 1: Add min/max to new state min/max, for all resource types. */
    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      resource_pool_t *pool = req->pools[restype] ;
      if ( pool != NULL &&
           !resource_pool_update(pool, state,
                                 0, node->minimum[restype],
                                 0, node->maximum[restype]) ) {
        /* Unwind the pool minimum/maximum changes caused by
           resource_pool_update(). Revert the state so requirement limits are
           counted as future when failing. */
        while ( restype > 0 ) {
          pool = req->pools[--restype] ;
          if ( pool != NULL &&
               !resource_pool_update(pool, state,
                                     node->minimum[restype], 0,
                                     node->maximum[restype], 0) ) {
            HQFAIL("Shouldn't have problems reducing pool min/max") ;
          }
        }
        goto cleanup ;
      }
    }

    /* Phase 2: Remove min/max from current state min/max, for all resource
       types. */
    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      resource_pool_t *pool = req->pools[restype] ;
      if ( pool != NULL &&
           !resource_pool_update(pool, req->state,
                                 node->minimum[restype], 0,
                                 node->maximum[restype], 0) ) {
        HQFAIL("Shouldn't have problems reducing pool min/max") ;
      }
    }

    req->state = state ;
    result = TRUE ;

    /* Phase 3: Remove unnecessary pool references. We've succeeded in making
       the requirement ready, so the minimum values can never increase. If
       any of the minimum requirements are zero, we can remove the pool
       references and let the pool be reclaimed. */
    if ( state == REQUIREMENT_NOW ) {
      for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
        if ( node->minimum[restype] == 0 )
          resource_requirement_remove_pool(req, node, restype) ;
      }
    }
  } else { /* No state change */
    result = TRUE ;
  }

 cleanup:
  requirement_node_unlock(req, node) ;

#undef return
  return result ;
}

Bool resource_requirement_is_ready(const resource_requirement_t *req)
{
  VERIFY_OBJECT(req, RESOURCE_REQUIREMENT_NAME) ;
  return req->state == REQUIREMENT_NOW ;
}

/*****************************************************************************/
/* Requirement expressions. */

unsigned int requirement_node_minimum(requirement_node_t *node,
                                      resource_type_t restype)
{
  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  return node->minimum[restype] ;
}

unsigned int requirement_node_maximum(requirement_node_t *node,
                                      resource_type_t restype)
{
  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  return node->maximum[restype] ;
}

/** Insert a node at the next insertion point in a right-left depth-first
    traversal of the resource expression tree. */
static Bool requirement_node_insert(requirement_node_t **where,
                                    requirement_node_t *insert)
{
  HQASSERT(where != NULL, "No requirement node insertion point") ;
  HQASSERT(insert != NULL, "No requirement node to insert") ;

  for (;;) {
    requirement_node_t *node ;

    /* If there's nothing in this location, insert here. */
    if ( (node = *where) == NULL) {
      *where = insert ;
      return TRUE ;
    }

    switch ( node->op ) {
    default:
      HQFAIL("Invalid expression operator") ;
      /*@fallthrough@*/
    case REQUIREMENT_OP_VALUES:
      /* Can't put an expression under a zero-operand (value) node. */
      return FALSE ;
    case REQUIREMENT_OP_SUM:
    case REQUIREMENT_OP_MAX:
    case REQUIREMENT_OP_LIMIT:
      if ( requirement_node_insert(&node->right, insert) )
        return TRUE ;
      break ;
    }

    /* Tail call elimination for left branch */
    where = &node->left ;
  }
}

requirement_node_t *requirement_node_create(resource_requirement_t *req,
                                            requirement_node_id id,
                                            requirement_op_t op)
{
  requirement_node_t *node ;

  if ( (node = mm_alloc(mm_pool_task, sizeof(*node),
                        MM_ALLOC_CLASS_REQUIREMENT_NODE)) != NULL ) {
    requirement_node_t *root ;

    HqMemZero(node, sizeof(*node)) ;
    node->id = id ;
    node->op = op ;
    node->requirement = req ;
    node->smin = node->smax = 1 ;
    NAME_OBJECT(node, REQUIREMENT_NODE_NAME) ;

    requirement_node_lock(req, root) ;
    if ( !requirement_node_insert(&root, node) )
      HQFAIL("Nowhere to put resource expression") ;
    requirement_node_unlock(req, root) ;
  } else {
    (void)error_handler(VMERROR) ;
  }

  return node ;
}

/** Find an identified resource expression. The resource expression ID should
    be unique, because the search order is not specified. */
static requirement_node_t *find_node_id(requirement_node_t *node,
                                        requirement_node_id id)
{
  while ( node != NULL ) {
    requirement_node_t *right ;

    VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;

    if ( node->id == id )
      return node ;

    if ( (right = find_node_id(node->right, id)) != NULL )
      return right ;

    node = node->left ;
  }

  return NULL ;
}

requirement_node_t *requirement_node_find(resource_requirement_t *req,
                                          requirement_node_id id)
{
  requirement_node_t *root, *node ;

  requirement_node_lock(req, root) ;
  node = find_node_id(root, id) ;
  requirement_node_unlock(req, root) ;

  return node ;
}

static void requirement_node_evaluate(requirement_node_t *node,
                                      resource_type_t restype)
{
  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;

  ASSERT_NODE_LOCKED(node->requirement, TRUE /*locked*/,
                     "Expression evaluation unlocked") ;

  if ( node->op != REQUIREMENT_OP_VALUES ) {
    requirement_node_t *left = node->left ;
    requirement_node_t *right = node->right ;
    /** \todo ajcd 2012-03-09: which limit to use?
        thread_limit_active, maxthreads_active, limit_active_original?

        If any but limit_active_original, then we may need to re-evaluate
        when we change the limit. Need access from tasks.c for anything but
        limit_active_original (which is accessed through
        max_simultaneous_tasks()). */
    unsigned int maxthreads = (unsigned int)max_simultaneous_tasks() ;
    unsigned int lmin, lmax, rmin, rmax ;

    requirement_node_evaluate(left, restype) ;
    requirement_node_evaluate(right, restype) ;

    lmin = left->minimum[restype] * requirement_smin(left, maxthreads) ;
    lmax = left->maximum[restype] * requirement_smax(left, maxthreads) ;
    rmin = right->minimum[restype] * requirement_smin(right, maxthreads) ;
    rmax = right->maximum[restype] * requirement_smax(right, maxthreads) ;

    switch ( node->op ) {
    case REQUIREMENT_OP_LIMIT:
      node->minimum[restype] = max(lmin, rmin) ;
      /* We only want to limit on the maximum if it is set. */
      node->maximum[restype] = (lmax > rmax && rmax > rmin) ? rmax : lmax ;
      if ( node->maximum[restype] < node->minimum[restype] )
        node->maximum[restype] = node->minimum[restype] ;
      break ;
    case REQUIREMENT_OP_SUM:
      node->minimum[restype] = lmin + rmin ;
      node->maximum[restype] = lmax + rmax ;
      break ;
    case REQUIREMENT_OP_MAX:
      node->minimum[restype] = max(lmin, rmin) ;
      node->maximum[restype] = max(lmax, rmax) ;
      break ;
    default:
      HQFAIL("Unknown resource expression operator") ;
      break ;
    }
  }

  HQASSERT(node->minimum[restype] <= node->maximum[restype],
           "Expression minimum is greater than maximum") ;
}

static Bool node_setmin_locked(requirement_node_t *node,
                               requirement_node_t *root,
                               resource_type_t restype,
                               unsigned int minimum)
{
  resource_requirement_t *req ;
  unsigned int oldmin, oldmax, rootmin, rootmax ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  VERIFY_OBJECT(root, REQUIREMENT_NODE_NAME) ;
  HQASSERT(node->requirement == root->requirement,
           "Expression node and root node are from different requirements") ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  HQASSERT(node->op == REQUIREMENT_OP_VALUES,
           "Attempting to modify computed expression node") ;

  req = node->requirement ;
  ASSERT_NODE_LOCKED(req, TRUE /*locked*/,
                     "Expression unlocked for set minimum") ;

  oldmin = node->minimum[restype] ;
  oldmax = node->maximum[restype] ;
  rootmin = root->minimum[restype] ;
  rootmax = root->maximum[restype] ;

  if ( minimum < oldmin ) {
    /* We don't track which specific groups using this requirement are
       provisioned, so if we reduced the minimum, we would not be able to
       guarantee that group deprovisioning would return the number to
       zero. */
    HQASSERT(req->state != REQUIREMENT_NOW || req->pools[restype] == NULL ||
             req->pools[restype]->nprovided == 0,
             "Cannot reduce minimum requirement whilst any group provisioned") ;

    /* We should be able to reduce the minimum at any point. We can let
       low-memory actions reclaim any resource memory lazily. */
    node->minimum[restype] = minimum ;

    requirement_node_evaluate(root, restype) ;

    /* No operator makes the root minimum less than a node minimum. */
    HQASSERT(root->minimum[restype] >= minimum,
             "Operator increased resource expression minimum") ;
    /* Reducing a node min/max shouldn't have increased the root min/max */
    HQASSERT(root->minimum[restype] <= rootmin,
             "Evaluation increased requirement minimum") ;
    HQASSERT(root->maximum[restype] <= rootmax,
             "Evaluation increased requirement maximum") ;
  } else if ( minimum > oldmin ) {
    HQASSERT(req->state != REQUIREMENT_NOW,
             "Cannot increase minimum resource allocation once requirement is ready") ;
    node->minimum[restype] = minimum ;
    if ( minimum > oldmax )
      node->maximum[restype] = minimum ;

    requirement_node_evaluate(root, restype) ;

    HQASSERT(root->minimum[restype] >= rootmin,
             "Evaluation decreased resource requirement minimum") ;
    HQASSERT(root->maximum[restype] >= rootmax,
             "Evaluation decreased resource requirement maximum") ;
  } /* else there is no change to the existing minimum. */

  HQASSERT(root->minimum[restype] == 0 || req->pools[restype] != NULL,
           "Need resource pool if minimum expression evaluates non-zero") ;

  if ( root->minimum[restype] != rootmin ||
       root->maximum[restype] != rootmax ) {
    resource_pool_t *pool = req->pools[restype] ;

    /* Adjust pool minimum, maximum totals, promise extra resources if
       minimum increased, and/or increase lookup table size if maximum
       increased. */
    if ( pool != NULL &&
         !resource_pool_update(pool, req->state,
                               rootmin, root->minimum[restype],
                               rootmax, root->maximum[restype]) ) {
      /* Restore previous values and restore expression tree. */
      node->minimum[restype] = oldmin ;
      node->maximum[restype] = oldmax ;
      requirement_node_evaluate(root, restype) ;
      return FALSE ;
    }
  }

  return TRUE ;
}

Bool requirement_node_setmin(requirement_node_t *node, resource_type_t restype,
                             unsigned int minimum)
{
  requirement_node_t *root ;
  resource_requirement_t *req ;
  Bool result ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  req = node->requirement ;

  requirement_node_lock(req, root) ;
  result = node_setmin_locked(node, root, restype, minimum) ;

  /** \todo ajcd 2012-04-10: Should this be root->maximum[] == 0? What does
      min=0, max>0 mean? */
  if ( req->state == REQUIREMENT_NOW && root->minimum[restype] == 0 )
    resource_requirement_remove_pool(req, root, restype) ;
  requirement_node_unlock(req, root) ;

  /** \todo ajcd 2012-03-09: If changed values, signal task group
        reprovision? We may be able to schedule tasks if they say they
        require fewer resources. This may be worth doing even if this
        requirement isn't ready, because it may reduce a promised pool
        allocation enough to make space for something else. */
  /** \todo ajcd 2012-03-09: Also signal low memory available? Eagerly
      recover some of the resources? */

  return result ;
}

Bool requirement_node_maxmin(requirement_node_t *node, resource_type_t restype,
                             unsigned int minimum)
{
  requirement_node_t *root ;
  Bool result = TRUE ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;

  requirement_node_lock(node->requirement, root) ;
  if ( minimum > node->minimum[restype] )
    result = node_setmin_locked(node, root, restype, minimum) ;
  requirement_node_unlock(node->requirement, root) ;

  /* maxmin cannot decrease the minimum resource value, so it's pointless
     checking if the value is zero. */

  return result ;
}

static Bool node_setmax_locked(requirement_node_t *node,
                               requirement_node_t *root,
                               resource_type_t restype,
                               unsigned int maximum)
{
  resource_requirement_t *req ;
  unsigned int oldmin, oldmax, rootmin, rootmax ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  VERIFY_OBJECT(root, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  HQASSERT(node->op == REQUIREMENT_OP_VALUES,
           "Attempting to modify computed expression node") ;

  req = node->requirement ;
  ASSERT_NODE_LOCKED(req, TRUE /*locked*/,
                     "Expression unlocked for set maximum") ;

  oldmin = node->minimum[restype] ;
  oldmax = node->maximum[restype] ;
  rootmax = root->maximum[restype] ;
  rootmin = root->minimum[restype] ;

  if ( maximum < oldmax ) {
    /* We should be able to reduce the maximum at any point, so long as we
       don't go below provisioned levels (that's checked by the
       resource_pool_update). We can let low-memory actions reclaim any
       resource memory lazily. Reducing the maximum may also affect the
       minimum. */
    node->maximum[restype] = maximum ;
    if ( maximum < node->minimum[restype] ) {
      /* We don't track which specific groups using this requirement are
         provisioned, so if we reduced the minimum, we would not be able to
         guarantee that group deprovisioning would return the number to
         zero. */
      HQASSERT(maximum >= oldmin ||
               req->state != REQUIREMENT_NOW ||
               req->pools[restype] == NULL ||
               req->pools[restype]->nprovided == 0,
               "Cannot reduce minimum requirement whilst any group provisioned") ;
      node->minimum[restype] = maximum ;
    }

    requirement_node_evaluate(root, restype) ;

    /* Reducing a node max/min shouldn't have increased the root max/min */
    HQASSERT(root->maximum[restype] <= rootmax,
             "Evaluation increased resource requirement maximum") ;
    HQASSERT(root->minimum[restype] <= rootmin,
             "Evaluation increased resource requirement minimum") ;
  } else if ( maximum > oldmax ) {
    /* Allow increasing maximum even when requirement is ready. This may
       re-allocate the lookup table, causing a VMERROR, so the caller needs
       to be prepared for that. */
    node->maximum[restype] = maximum ;

    requirement_node_evaluate(root, restype) ;

    HQASSERT(root->maximum[restype] >= rootmax,
             "Evaluation decreased resource requirement maximum") ;
    HQASSERT(root->minimum[restype] >= rootmin,
             "Evaluation decreased resource requirement minimum") ;
  }

  HQASSERT(root->minimum[restype] == 0 || req->pools[restype] != NULL,
           "Need resource pool if minimum expression evaluates non-zero") ;

  if ( root->minimum[restype] != rootmin ||
       root->maximum[restype] != rootmax ) {
    resource_pool_t *pool = req->pools[restype] ;

    /* Adjust pool minimum, maximum totals, promise extra resources if
       minimum increased, and/or increase lookup table size if maximum
       increased. */
    if ( pool != NULL &&
         !resource_pool_update(pool, req->state,
                               rootmin, root->minimum[restype],
                               rootmax, root->maximum[restype]) ) {
      /* Restore previous values and restore expression tree. */
      node->minimum[restype] = oldmin ;
      node->maximum[restype] = oldmax ;
      requirement_node_evaluate(root, restype) ;
      return FALSE ;
    }
  }

  /* If there is no change to the existing maximum, there is nothing to do. */
  return TRUE ;
}

Bool requirement_node_setmax(requirement_node_t *node, resource_type_t restype,
                             unsigned int maximum)
{
  requirement_node_t *root ;
  resource_requirement_t *req ;
  Bool result ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;
  req = node->requirement ;

  requirement_node_lock(req, root) ;
  result = node_setmax_locked(node, root, restype, maximum) ;

  /** \todo ajcd 2012-04-10: Should this be root->maximum[] == 0? What does
      min=0, max>0 mean? */
  if ( req->state == REQUIREMENT_NOW && root->minimum[restype] == 0 )
    resource_requirement_remove_pool(req, root, restype) ;
  requirement_node_unlock(req, root) ;

  /** \todo ajcd 2012-03-09: If changed values, signal task group
        reprovision? We may be able to schedule tasks if they say they
        require fewer resources. This may be worth doing even if this
        requirement isn't ready, because it may reduce a promised pool
        allocation enough to make space for something else. */
  /** \todo ajcd 2012-03-09: Also signal low memory available? Eagerly
      recover some of the resources? */

  return result ;
}

Bool requirement_node_minmax(requirement_node_t *node, resource_type_t restype,
                             unsigned int maximum)
{
  requirement_node_t *root ;
  Bool result = TRUE ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;
  HQASSERT(restype >= 0 && restype < N_RESOURCE_TYPES, "Invalid resource type") ;

  requirement_node_lock(node->requirement, root) ;
  if ( maximum > node->minimum[restype] && maximum < node->maximum[restype] )
    result = node_setmax_locked(node, root, restype, maximum) ;
  requirement_node_unlock(node->requirement, root) ;

  /* minmax cannot decrease the minimum resource value, so it's pointless
     checking if the value is zero. */

  return result ;
}


Bool requirement_node_simmin(requirement_node_t *node, unsigned int minimum)
{
  requirement_node_t *root ;
  resource_requirement_t *req ;
  Bool result = TRUE ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;

  req = node->requirement;
  requirement_node_lock(req, root) ;
  HQASSERT(node != root, "Cannot set simultaneous minimum on root node");
  if ( minimum > node->smin ) {
    resource_type_t restype ;
    unsigned int oldmin = node->smin, oldmax = node->smax ;

    node->smin = minimum ;
    if ( minimum > node->smax )
      node->smax = minimum ;

  retry:
    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      unsigned int rootmin = root->minimum[restype] ;
      unsigned int rootmax = root->maximum[restype] ;
      requirement_node_evaluate(root, restype) ;
      if ( root->minimum[restype] != rootmin ||
           root->maximum[restype] != rootmax ) {
        resource_pool_t *pool = req->pools[restype] ;

        /* Adjust pool minimum, maximum totals, promise extra resources if
           minimum increased, and/or increase lookup table size if maximum
           increased. */
        if ( pool != NULL &&
             !resource_pool_update(pool, req->state,
                                   rootmin, root->minimum[restype],
                                   rootmax, root->maximum[restype]) ) {
          /* Restore previous values and restore expression tree. This will
             repeat the whole loop with the previous values. We should be able
             to reduce the pool updates after increasing them, so this
             shouldn't fail if we've already been here. */
          HQASSERT(result, "Loop restoring old pool values") ;
          node->smin = oldmin ;
          node->smax = oldmax ;
          result = FALSE ;
          goto retry ;
        }
      }
    }
  }
  requirement_node_unlock(req, root) ;

  return result ;
}


Bool requirement_node_simmax(requirement_node_t *node, unsigned int maximum)
{
  requirement_node_t *root ;
  resource_requirement_t *req ;
  Bool result = TRUE ;

  VERIFY_OBJECT(node, REQUIREMENT_NODE_NAME) ;

  req = node->requirement ;
  requirement_node_lock(req, root) ;
  HQASSERT(node != root, "Cannot set simultaneous maximum on root node");
  if ( maximum > node->smax ) {
    resource_type_t restype ;
    unsigned int oldmin = node->smin, oldmax = node->smax ;

    node->smax = maximum ;

  retry:
    for ( restype = 0 ; restype < N_RESOURCE_TYPES ; ++restype ) {
      unsigned int rootmin = root->minimum[restype] ;
      unsigned int rootmax = root->maximum[restype] ;
      requirement_node_evaluate(root, restype) ;
      if ( root->minimum[restype] != rootmin ||
           root->maximum[restype] != rootmax ) {
        resource_pool_t *pool = req->pools[restype] ;

        /* Adjust pool minimum, maximum totals, promise extra resources if
           minimum increased, and/or increase lookup table size if maximum
           increased. */
        if ( pool != NULL &&
             !resource_pool_update(pool, req->state,
                                   rootmin, root->minimum[restype],
                                   rootmax, root->maximum[restype]) ) {
          /* Restore previous values and restore expression tree. This will
             repeat the whole loop with the previous values. We should be able
             to reduce the pool updates after increasing them, so this
             shouldn't fail if we've already been here. */
          HQASSERT(result, "Loop restoring old pool values") ;
          node->smin = oldmin ;
          node->smax = oldmax ;
          result = FALSE ;
          goto retry ;
        }
      }
    }
  }
  requirement_node_unlock(req, root) ;

  return result ;
}

/*****************************************************************************/
/* Resource lookups. */

/** \brief Allocate a resource lookup table.

    \param[in] n           The number of elements in the new lookup table.

    \return A new lookup table, or NULL if the table could not be allocated.
*/
static resource_lookup_t *resource_lookup_alloc(unsigned int n)
{
  size_t s = resource_lookup_size(n) ;
  resource_lookup_t *lookup ;

  if ( (lookup = mm_alloc(mm_pool_task, s,
                          MM_ALLOC_CLASS_RESOURCE_LOOKUP)) != NULL ) {
    HqMemZero(lookup, s) ;
    lookup->nentries = n ;
  } else {
    (void)error_handler(VMERROR) ;
  }

  return lookup ;
}

/** \brief Free a resource lookup table.

    \param[in,out] lookupptr  A pointer to locate a lookup table.
*/
static void resource_lookup_free(resource_lookup_t **lookupptr)
{
  resource_lookup_t *lookup ;

  if ( (lookup = *lookupptr) != NULL ) {
    mm_free(mm_pool_task, lookup, resource_lookup_size(lookup->nentries)) ;
    *lookupptr = NULL ;
  }
}

unsigned int resource_lookup_first(resource_lookup_t *lookup,
                                   resource_id_t id)
{
  uint32 key ;

  HQASSERT(lookup != NULL, "No resource lookup") ;
  HQASSERT(lookup->nentries > 0, "Resource lookup has no entries") ;

  hsieh_hash32_init(&key, 2) ;
  hsieh_hash32_step(&key, (uint32)id) ;
  hsieh_hash32_step(&key, (uint32)((id >> 16) >> 16)) ; /* Don't care if this is zero */
  hsieh_hash32_finish(&key) ;

  return key % lookup->nentries ;
}

void resource_lookup_insert(resource_lookup_t *lookup,
                            resource_entry_t *entry)
{
  unsigned int i, start ;

  start = resource_lookup_first(lookup, entry->id) ;
  for ( i = start ;; ) {
    if ( lookup->entries[i] == NULL ) {
      lookup->entries[i] = entry ;
      break ;
    }
    if ( i == 0 )
      i = lookup->nentries ;
    --i ;
    HQASSERT(i != start, "No free slot found to insert resource entry") ;
  }
}

/*****************************************************************************/
/* Resource pools. */

/** Default finish method for resource pools. */
static void resource_pool_finish(resource_pool_t *pool)
{
  UNUSED_PARAM(resource_pool_t *, pool) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  HQASSERT(pool->refcount == 0, "Pool finish method called at wrong time") ;
}

static Bool resource_key_compare(resource_pool_t *pool, resource_key_t key)
{
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  HQASSERT(pool->refcount > 0, "Resource pool already released") ;
  return pool->key == key ;
}

static inline resource_source_t *handler_source(low_mem_handler_t *handler)
{
  resource_source_t *source = NULL ;

  HQASSERT(handler != NULL, "No handler") ;

  /* Yuck, the handler struct used varies depending on the memory tier we're
     using, so this test is required to retried the resource_source_t given the
     sub-structure pointer. */
  if ( handler->tier == memory_tier_ram ) {
    source = (resource_source_t *)((char *)handler -
                                   offsetof(resource_source_t, low_handler_ram)) ;
  } else if ( handler->tier == memory_tier_reserve_pool ) {
    source = (resource_source_t *)((char *)handler -
                                   offsetof(resource_source_t, low_handler_reserve)) ;
  } else {
    HQFAIL("Unrecognised memory tier for resource low-memory handler") ;
  }
  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME);
  HQASSERT(source->mm_pool != NULL,
           "No MM pool function for resource source low mem solicit") ;
  return source ;
}

/** Task resource offer costs should vary depending on whether we restrict
    ability to use threads. We could define the low-memory handler to operate
    on the resource requirements, and examine the resource expressions to
    determine what effect a change would have in restricting threads. But
    that's a bit too complicated for now, so we just set costs based on the
    resource level in each pool.

    The same costs are also used for allocation, by symmetry.

    Some of the ranges below might be empty, depending on the order of the
    limits, but that'll work out OK. */
const static mm_cost_t resource_low_mem_costs[] = {
  /* nresources > max(maximum, promisemax)
     Essentially free. We don't want these resources now, we can operate
     with all threads, and don't foresee a need for them in the future. */
  { memory_tier_ram, 0.1f},
  /* max(maximum, promisemax) > nresources > maximum
     No immediate impact. We don't want these resources now, we can operate
     with all threads, but we may be impacting future performance if we
     can't allocate more later. But we're very likely to be able to
     allocate them in future, so don't make it too hard to discard now. */
  /* IOW, the cost of allocating resources between the min and max we need in
     future (but now now). */
  { memory_tier_ram, 2.0f},
  /* maximum > nresources > max(minimum, promisemin)
     Immediate impact on performance. We're probably restricting current
     thread use, but we should be able to fulfil current and future
     commitments with degraded performance. Computationally, this is
     expensive, because we're potentially restricting thread use a lot, so
     make the cost high, but at a low tier. */
  /* IOW, the cost of allocating resources between the min and max we need
     right now. */
  { memory_tier_ram, 1e6f},
  /* max(minimum, promisemin) > nresources > minimum
     We're restricting current performance, and we may not be able to
     fulfil future promises unless we can allocate more resources before
     then. This should not be offered as a low-memory action without some
     way of recovering the resources in the future. */
  /* IOW, the cost of allocating the minimum resources. By definition, that's
     mm_cost_normal. */
  { memory_tier_reserve_pool, 1e3},
} ;

/** \brief Helper function to return resource level for a cost index.

    \param[in] pool  The resource pool to examine.
    \param cost_i    The cost table index.
*/
static inline unsigned int resource_low_mem_level(resource_pool_t *pool,
                                                  unsigned int cost_i)
{
  ASSERT_LOOKUP_LOCKED(pool, TRUE /*locked*/,
                       "Resource pool lookup is not locked") ;
  switch ( cost_i ) {
  case 0: return max(pool->maximum, pool->promisemax) ;
    /* Although the limit at index 1 is pool->maximum, restrict to promisemin to
       avoid double-counting any resources maximum < n <= promisemin. */
  case 1: return max(pool->maximum, pool->promisemin) ;
  case 2: return max(pool->minimum, pool->promisemin) ;
  case 3: return pool->minimum ;
  default:
    HQFAIL("Invalid cost level index") ;
    return pool->nresources ;
  }
}

/** \brief Solicit method of the resource source low-memory handlers. */
static low_mem_offer_t *resource_low_mem_solicit(low_mem_handler_t *handler,
                                                 corecontext_t *context,
                                                 size_t count,
                                                 memory_requirement_t* requests)
{
  resource_source_t *source ;
  unsigned int offer_i = 0, cost_i ;
  size_t offered_total = 0 ;
  low_mem_offer_t *res = NULL;

  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(size_t, count);
  UNUSED_PARAM(memory_requirement_t*, requests);

  source = handler_source(handler) ;
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");

  /* Iterate over the list of pools once for each cost that can be offered. */
  for ( cost_i = 0 ; cost_i < NUM_ARRAY_ITEMS(resource_low_mem_costs) - 1
          ; ++cost_i ) {
    resource_pool_t *pool, *prev = NULL, *lock = NULL ;
    size_t offered_size = 0 ;

    /* Only consider cost entries matching this tier. */
    if ( handler->tier != resource_low_mem_costs[cost_i].tier )
      continue ;

    /** \todo ajcd 2012-04-26: If the pool we're working on is removed during
        the iteration, the iteration will terminate immediately (because
        pool->pool_list is set to NULL in resource_pool_release()). This may
        not be optimal behaviour, we might miss making an offer on another
        pool because of it. We really need a safe lock-free list structure to
        handle this. */
    spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;
    for ( pool = lock ; pool != NULL ; pool = pool->pool_list ) {
      resource_lookup_t *lookup = NULL;
      Bool locked ;

      /* If we can't get the entry size for this pool, skip it, keeping the
         spinlock. */
      if ( pool->entry_size == NULL )
        continue ;

      /* Take an extra reference to the pool, and release the spinlock
         immediately so we don't hold it at the same time as other locks. */
      pool = resource_pool_acquire(pool) ;
      spinunlock_pointer(&source->pool_list, lock) ;

      /* If we took an extra reference to a pool in the previous loop
         iteration, free it once we've unlocked the pool list. */
      if ( prev != NULL )
        resource_pool_release(&prev) ;

      /* This lock will not allow recursive entry. */
      resource_lookup_trylock(pool, lookup, locked);
      if ( locked ) {
        unsigned int level, entry_i = 0, nresources = pool->nresources ;

        HQASSERT(pool->nresources >= pool->minimum,
                 "Pool resources already below minimum") ;

        level = resource_low_mem_level(pool, cost_i) ;
        /* We cannot go lower than the number of resources already provided. */
        if ( level < pool->nprovided + pool->ndetached )
          level = pool->nprovided + pool->ndetached ;

        /* Search to see if we can get enough entries. */
        while ( nresources > level && entry_i < lookup->nentries ) {
          resource_entry_t *entry = lookup->entries[entry_i++] ;
          if ( entry != NULL && entry->state == TASK_RESOURCE_FREE ) {
            size_t free_size = pool->entry_size(pool, entry) ;
            if ( free_size > 0 ) {
              offered_size += free_size ;
              --nresources ;
            }
          }
        }

        resource_lookup_unlock(pool, lookup);
      }

      /* Lock the pool list so that pool->pool_list doesn't change. */
      spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;
      prev = pool ;
    }
    spinunlock_pointer(&source->pool_list, lock) ;
    /* If we took an extra reference to a pool in the final loop
       iteration, free it once we've unlocked the pool list. */
    if ( prev != NULL )
      resource_pool_release(&prev) ;

    /* We're offering memory at different costs, but we count up all
       resources over a particular cost level for the pools. This means
       that we're counting the resources for the harder levels multiple
       times. We can account for the previous levels by subtracting the
       previous offer's size from the current offer size. We have to be
       careful though, because size counting may not be exact. The resource
       pools can change between the iterations, and the trylock may not
       succeed, so we may miss or count extra. */
    if ( offered_size > offered_total )
      offered_size -= offered_total ;
    else
      offered_size = 0 ;
    offered_total += offered_size ;

    if ( offered_size > 0 ) {
      HQASSERT(offer_i < N_RESOURCE_LOW_OFFERS,
               "Overflowed low-memory resource offers") ;
      source->low_offer[offer_i].pool = source->mm_pool(source) ;
      source->low_offer[offer_i].offer_size = offered_size ;
      source->low_offer[offer_i].offer_cost = resource_low_mem_costs[cost_i].value;
      source->low_offer[offer_i].next = res ;
      res = &source->low_offer[offer_i] ;
      ++offer_i ;
    }
  }

  return res;
}


/** \brief Release method of the resource pool low-memory handlers. */
static Bool resource_low_mem_release(low_mem_handler_t *handler,
                                     corecontext_t *context,
                                     low_mem_offer_t *offer)
{
  resource_source_t *source ;
  unsigned int cost_i ;
  size_t taken_size = 0, freed_size = 0 ;

  UNUSED_PARAM(corecontext_t*, context);

  source = handler_source(handler) ;
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");

  /* Total up the offers. Regardless of what cost levels the apportioner said
     it'd take offers from, we're going to take from the lowest impact levels
     first. */
  do {
    taken_size += offer->taken_size ;
    offer = offer->next ;
  } while ( offer != NULL ) ;
  HQASSERT(taken_size > 0, "Low memory release wants zero bytes") ;

  /* Try to release the memory in cost level increments, easiest first. */
  for ( cost_i = 0 ;
        cost_i < NUM_ARRAY_ITEMS(resource_low_mem_costs) && freed_size < taken_size ;
        ++cost_i ) {
    resource_pool_t *pool, *prev = NULL, *lock = NULL ;

    /* Only consider cost entries matching this tier. */
    if ( handler->tier != resource_low_mem_costs[cost_i].tier )
      continue ;

    spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;
    for ( pool = lock ;
          pool != NULL && freed_size < taken_size ;
          pool = pool->pool_list ) {
      resource_lookup_t *lookup = NULL;
      Bool locked ;

      /* If we can't get the entry size for this pool, skip it, keeping the
         spinlock. */
      if ( pool->entry_size == NULL )
        continue ;

      /* Take an extra reference to the pool, and release the spinlock
         immediately so we don't hold it at the same time as other locks. */
      pool = resource_pool_acquire(pool) ;
      spinunlock_pointer(&source->pool_list, lock) ;

      /* If we took an extra reference to a pool in the previous loop
         iteration, free it once we've unlocked the pool list. */
      if ( prev != NULL )
        resource_pool_release(&prev) ;

      /* This lock will not allow recursive entry. */
      resource_lookup_trylock(pool, lookup, locked);
      if ( locked ) {
        unsigned int level = resource_low_mem_level(pool, cost_i) ;
        /* We cannot go lower than the number of resources already provided. */
        if ( level < pool->nprovided + pool->ndetached )
          level = pool->nprovided + pool->ndetached ;

        /* Determine how many entries to release based upon the taken size,
           but never go below the cost level (the final level will be the
           pool minimum). Another thread may have changed the pool in between
           the offer and the release. */
        if ( pool->nresources > level ) {
          unsigned int entry_i = 0, canfree = pool->nresources - level ;
          for ( entry_i = 0;
                entry_i < lookup->nentries && freed_size < taken_size && canfree > 0 ;
                ++entry_i ) {
            /* If there is an entry at this location, and it's not owned, and
               it's a suitable size, free it. */
            resource_entry_t *entry = lookup->entries[entry_i] ;
            if ( entry != NULL && entry->state == TASK_RESOURCE_FREE ) {
              size_t free_size = pool->entry_size(pool, entry) ;
              if ( free_size > 0 ) {
                resource_entry_free(pool, &lookup->entries[entry_i]);
                freed_size += free_size ;
                --canfree ;
              }
            }
          }
        }

        resource_lookup_unlock(pool, lookup);
      }

      /* Lock the pool list so that pool->pool_list doesn't change. */
      spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;
      prev = pool ;
    }
    spinunlock_pointer(&source->pool_list, lock) ;
    /* If we took an extra reference to a pool in the final loop
       iteration, free it once we've unlocked the pool list. */
    if ( prev != NULL )
      resource_pool_release(&prev) ;
  }

  return TRUE;
}


resource_pool_t *resource_pool_get(resource_source_t *source,
                                   resource_type_t restype,
                                   resource_key_t key)
{
  resource_pool_t *pool, *lock, *newp = NULL ;
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(IS_INTERPRETER(), "Resource pool not constructed by interpreter") ;

  /* Lock for the pool iteration, and the addition to the source list. */
#define return DO_NOT_return_GOTO_found_OR_done_INSTEAD!
  spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;
  for ( pool = lock ; pool != NULL ; pool = pool->pool_list ) {
    VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

    if ( pool->type == restype && pool->compare(pool, key) ) {
      /* Return another reference to this pool. */
      HqAtomicIncrement(&pool->refcount, before) ;
      HQASSERT(before > 0, "Resource pool had been previously released") ;
      goto found ;
    }
  }

  /* Drop pool list spinlock while constructing new pool, so we can't enter
     low memory on this thread with the pool list locked. The price we pay
     for this is that we need to search the pool list again after
     constructing a new pool, and we may have to discard the new pool
     immediately if another thread has created a suitable pool
     simultaneously. This is unlikely to happen. */
  spinunlock_pointer(&source->pool_list, lock) ;

  if ( (newp = mm_alloc(mm_pool_task, sizeof(*newp),
                        MM_ALLOC_CLASS_RESOURCE_POOL)) == NULL ) {
    (void)error_handler(VMERROR) ;
    goto done ;
  }

  HqMemZero(newp, sizeof(*newp)) ;
  newp->refcount = 1 ;
  newp->type = restype ;
  newp->source = source ;
  newp->key = key ;

  /* Default methods, expected to be overridden. */
  newp->compare = &resource_key_compare ;
  newp->finish = &resource_pool_finish ;

  NAME_OBJECT(newp, RESOURCE_POOL_NAME) ;

  if ( !source->make_pool(source, newp) )
    goto done ;

  HQASSERT(newp->alloc != NULL, "No pool allocation method") ;
  HQASSERT(newp->free != NULL, "No pool free method") ;

  /* We've now got a valid resource pool created and registered. Find out if
     another thread also created a suitable pool. */
  spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;

  for ( pool = lock ; pool != NULL ; pool = pool->pool_list ) {
    VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

    if ( pool->type == restype && pool->compare(pool, key) ) {
      /* Return another reference to this pool. */
      HqAtomicIncrement(&pool->refcount, before) ;
      HQASSERT(before > 0, "Resource pool had been previously released") ;
      goto found ;
    }
  }

  /* No other pool, so add this one to the source list. */
  HqAtomicIncrement(&source->refcount, before) ;
  HQASSERT(before > 0, "Resource source had been previously released") ;
  newp->pool_list = lock ;
  lock = newp ;

  pool = newp ; /* Return this pool. */
  newp = NULL ; /* Don't accidentally free it before returning. */

 found:
  spinunlock_pointer(&source->pool_list, lock) ;

 done:
  /* If we had a new pool, but either hit an error or found that another
     thread had constructed a suitable pool at the same time, free it. */
  if ( newp != NULL ) {
    HQASSERT(newp->refcount == 1, "New resource pool reference count invalid") ;
    --newp->refcount ; /* So finish method doesn't assert. */
    if ( newp->finish != NULL )
      newp->finish(newp);
    UNNAME_OBJECT(newp) ;
    mm_free(mm_pool_task, newp, sizeof(*newp)) ;
  }

#undef return
  return pool ;
}

static Bool resource_pool_update(resource_pool_t *pool,
                                 requirement_state_t state,
                                 unsigned int oldmin, unsigned int newmin,
                                 unsigned int oldmax, unsigned int newmax)
{
  Bool result = FALSE ;
  resource_lookup_t *lookup ;
  unsigned int min_now = 0, max_now = 0 ;
  unsigned int min_future = 0, max_future = 0 ;
  unsigned int minimum, maximum ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  /* All changes to min/max nresources are under the pool's lookup lock.
     Clients will increase current allocations before reducing future
     allocations, to ensure we don't reduce or remove the lookup table
     unnecessarily. */
  resource_lookup_lock(pool, lookup) ;

  min_now = pool->minimum ;
  max_now = pool->maximum ;
  min_future = pool->promisemin ;
  max_future = pool->promisemax ;

  if ( state == REQUIREMENT_NOW ) {
    HQASSERT(min_now >= oldmin, "Underflow in resource minimum") ;
    min_now = min_now + newmin - oldmin ;
    HQASSERT(max_now >= oldmax, "Underflow in resource maximum") ;
    max_now = max_now + newmax - oldmax ;
    HQASSERT(max_now >= min_now, "Resource max is less than min") ;
  } else if ( state == REQUIREMENT_FUTURE ) {
    HQASSERT(min_future >= oldmin, "Underflow in resource minimum") ;
    min_future = min_future + newmin - oldmin ;
    HQASSERT(max_future >= oldmax, "Underflow in resource maximum") ;
    max_future = max_future + newmax - oldmax ;
    HQASSERT(max_future >= min_future, "Resource max is less than min") ;
  } else {
    HQASSERT(state == REQUIREMENT_NEVER, "Invalid requirement state") ;
  }

  HQASSERT(max_now >= pool->nprovided,
           "Trying to reduce pool maximum below existing provision level") ;

  minimum = max(min_now, min_future) ;
  maximum = max(max_now, max_future) ;
  HQASSERT(maximum >= minimum, "Pool maximum is less than minimum") ;

  /* We need to allocate at least min_now from the pool. */
  /** \todo ajcd 2012-04-10: At the moment, we also try to pre-allocate
      enough to cover min_future as well. We shouldn't need to pre-allocate
      the difference, if the source can determine that the current
      allocations can cover it in future. We may decide to move the future
      allocation promise concept into the MM system, and remove resource
      sources altogether. */

  /* Only increase lookup size if we don't have enough space. If we do increase
     the lookup size, give it some headroom for better hash performance. */
  if ( maximum == 0 ) {
    /* Only shrink the lookup table when we have nothing using the pool. */
    if ( lookup != NULL && pool->ndetached == 0 ) { /* Free all resources. */
      unsigned int i ;

      for ( i = 0 ; i < lookup->nentries ; ++i ) {
        if ( lookup->entries[i] != NULL )
          resource_entry_free(pool, &lookup->entries[i]) ;
      }
      resource_lookup_free(&lookup) ;
    }
  } else if ( lookup == NULL ) {
    if ( (lookup = resource_lookup_alloc(maximum * 8 / 5)) == NULL )
      goto cleanup ;
  } else if ( lookup->nentries < maximum ) {
    resource_lookup_t *rehashed ;
    unsigned int i ;

    if ( (rehashed = resource_lookup_alloc(maximum * 8 / 5)) == NULL )
      goto cleanup ;

    for ( i = 0 ; i < lookup->nentries ; ++i ) {
      resource_entry_t *entry ;
      if ( (entry = lookup->entries[i]) != NULL ) {
        lookup->entries[i] = NULL ;
        resource_lookup_insert(rehashed, entry) ;
      }
    }

    resource_lookup_free(&lookup) ;
    lookup = rehashed ;
  }

  while ( pool->nresources < maximum ) { /* Allocate more resources. */
    resource_entry_t *entry ;

    if ( (entry = resource_entry_create(
                    pool,
                    pool->nresources < minimum
                    ? resource_low_mem_costs[3]
                    : pool->nresources < max_now
                    ? resource_low_mem_costs[2]
                    : resource_low_mem_costs[1],
                    NULL))
         == NULL ) {
      if ( pool->nresources < minimum ) {
        (void)error_handler(VMERROR) ;
        goto cleanup ;
      }
      /* We've got the minimum resources guaranteed, just not additional ones
         for future. */
      break ;
    }

    resource_lookup_insert(lookup, entry) ;
  }

  pool->minimum = min_now ;
  pool->maximum = max_now ;
  pool->promisemin = min_future ;
  pool->promisemax = max_future ;

  result = TRUE ;

 cleanup:
  verify_pool_entries(pool, lookup) ;
  resource_lookup_unlock(pool, lookup) ;
#undef return

  return result ;
}

resource_pool_t *resource_pool_acquire(resource_pool_t *pool)
{
  hq_atomic_counter_t before ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  HqAtomicIncrement(&pool->refcount, before) ;
  HQASSERT(before > 0, "Resource pool was previously released") ;

  return pool ;
}

void resource_pool_release(resource_pool_t **poolptr)
{
  resource_pool_t *pool, *lock ;
  hq_atomic_counter_t after ;
  resource_source_t *source ;

  HQASSERT(mm_pool_task, "Task pool not active") ;
  HQASSERT(poolptr, "Nowhere to find resource pool pointer") ;
  pool = *poolptr ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  *poolptr = NULL ;

  source = pool->source ;
  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME) ;

#define return DO_NOT_return_FALL_THROUGH_INSTEAD!
  /* We need to take the pool list spinlock early to avoid interleaving with
     resource_pool_get(), and unlinking a pool that it has just decided to
     re-use. We'll release the lock at the earliest possible opportunity. */
  spinlock_pointer(&source->pool_list, lock, HQSPIN_YIELD_ALWAYS) ;
  HqAtomicDecrement(&pool->refcount, after) ;
  HQASSERT(after >= 0, "Resource pool already released") ;
  if ( after == 0 ) {
    resource_lookup_t *lookup ;
    resource_pool_t **iter ;

    for ( iter = &lock ; *iter != pool ; iter = &(*iter)->pool_list )
      HQASSERT(*iter != NULL, "Didn't find resource pool on source list") ;

    *iter = pool->pool_list ; /* Removed pool from source list */
    pool->pool_list = NULL ;
    spinunlock_pointer(&source->pool_list, lock) ;

    ASSERT_LOOKUP_LOCKED(pool, FALSE /*unlocked*/,
                         "Resource pool lookup locked during release") ;

    /* Lock the lookup table whilst destroying it. The immediate unlock
       replaces the table with a NULL pointer, leaving this as the only
       reference to the lookup table. */
    resource_lookup_lock(pool, lookup) ;
    HQASSERT(pool->nprovided == 0 && pool->ndetached == 0,
             "Destroying resource pool providing task group resources") ;
    resource_lookup_unlock(pool, NULL) ;

    if ( lookup != NULL ) {
      unsigned int i ;

      for ( i = 0 ; i < lookup->nentries ; ++i ) {
        if ( lookup->entries[i] )
          resource_entry_free(pool, &lookup->entries[i]) ;
      }

      resource_lookup_free(&lookup) ;
    }

    if ( pool->finish != NULL )
      pool->finish(pool) ;

    HqAtomicDecrement(&source->refcount, after) ;
    HQASSERT(after >= 0, "Resource source already released") ;
    if ( after == 0 ) {
      HQASSERT(source->finish != NULL,
               "Need resource source finisaliser to dispose of source") ;
      source->finish(source) ;

      UNNAME_OBJECT(source) ;
      mm_free(mm_pool_task, source, sizeof(*source)) ;
    }

    UNNAME_OBJECT(pool) ;
    mm_free(mm_pool_task, pool, sizeof(*pool)) ;
  } else {
    /* Not the last reference, but we did lock the pool list, so we need to
       release it. */
    spinunlock_pointer(&source->pool_list, lock) ;
  }
#undef return
}

Bool resource_pool_forall(resource_pool_t *pool, resource_id_t id,
                          Bool (*callback)(resource_pool_t *pool,
                                           resource_entry_t *entry,
                                           void *data),
                          void *data)
{
  resource_lookup_t *lookup ;
  unsigned int i, start ;
  Bool result = FALSE ;

  resource_lookup_lock(pool, lookup) ;
#define return DO_NOT_return_GOTO_cleanup_INSTEAD!

  start = resource_lookup_first(lookup, id) ;
  for ( i = start ;; ) {
    resource_entry_t *entry ;
    /** \todo ajcd 2011-09-09: This is called with the resource pool locked.
        Should we unlock the pool whilst calling the callback? How would we
        continue the iteration if the lookup changes? */
    if ( (entry = lookup->entries[i]) != NULL && !callback(pool, entry, data) )
      goto cleanup ;
    if ( i == 0 )
      i = lookup->nentries ;
    if ( --i == start )
      break ;
  }

  result = TRUE ;

 cleanup:
  resource_lookup_unlock(pool, lookup) ;
#undef return

  return result ;
}

#ifdef ASSERT_BUILD
void verify_pool_entries(resource_pool_t *pool, resource_lookup_t *lookup)
{
  unsigned int ncounted = 0, nfixed = 0, ndetached = 0, nfullydetached = 0, i ;

  if ( lookup != NULL ) {
    for ( i = 0 ; i < lookup->nentries ; ++i ) {
      resource_entry_t *entry ;
      if ( (entry = lookup->entries[i]) != NULL ) {
        ++ncounted ;
        if ( entry->state == TASK_RESOURCE_DETACHED ) {
          if ( entry->owner == pool )
            ++nfullydetached ;
          else
            ++ndetached ;
        } else if ( entry->state != TASK_RESOURCE_FREE ) {
          ++nfixed ;
        }
      }
    }
  }

  HQASSERT(ncounted == pool->nresources, "Resource pool entry count mismatch") ;
  HQASSERT(nfullydetached <= pool->ndetached,
           "More resources detached than pool knows about") ;
  HQASSERT(nfixed + ndetached <= pool->nprovided, "More fixed than provided") ;
}
#endif

/*****************************************************************************/
/* Resource sources */

Bool resource_source_low_mem_register(resource_source_t *source)
{
  Bool result = TRUE ;

  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME) ;

  if ( source->mm_pool != NULL ) {
    static const low_mem_handler_t handler_init = {
      "Resource pool",
      memory_tier_ram, resource_low_mem_solicit, resource_low_mem_release,
      TRUE /*MT safe*/, 0 /*Id*/, FALSE /*Running*/
    };

    source->low_handler_ram = handler_init ;
    source->low_handler_reserve = handler_init ;
    source->low_handler_reserve.tier = memory_tier_reserve_pool ;

    if ( !low_mem_handler_register(&source->low_handler_ram) ||
         !low_mem_handler_register(&source->low_handler_reserve) ) {
      resource_source_low_mem_deregister(source) ;
      result = FALSE ;
    }
  }

  return result ;
}

void resource_source_low_mem_deregister(resource_source_t *source)
{
  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME) ;

  if ( source->mm_pool != NULL ) {
    if ( source->low_handler_ram.solicit != NULL ||
         source->low_handler_ram.release != NULL )
      low_mem_handler_deregister(&source->low_handler_ram) ;
    if ( source->low_handler_reserve.solicit != NULL ||
         source->low_handler_reserve.release != NULL )
      low_mem_handler_deregister(&source->low_handler_reserve) ;
  }
}

/*****************************************************************************/
/* Resource entries */

resource_entry_t *resource_entry_create(resource_pool_t *pool, mm_cost_t cost,
                                        void *owner)
{
  resource_entry_t *entry ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  /** \todo ajcd 2011-09-09: This is called with the resource pool locked.
      Should we unlock the pool whilst calling mm_alloc(), and undo the
      allocation if it's duplicated? */
  ASSERT_LOOKUP_LOCKED(pool, TRUE /*locked*/,
                       "Resource pool lookup is not locked") ;

  if ( (entry = mm_alloc_cost(mm_pool_task, sizeof(*entry), cost,
                              MM_ALLOC_CLASS_RESOURCE_ENTRY)) == NULL )
    return NULL ;

  entry->state = TASK_RESOURCE_FREE ;
  entry->id = TASK_RESOURCE_ID_INVALID ;
  entry->owner = owner ;
  entry->resource = NULL ;

  if ( !pool->alloc(pool, entry, cost) ) {
    mm_free(mm_pool_task, entry, sizeof(*entry)) ;
    return NULL ;
  }

  ++pool->nresources ;

  return entry ;
}

void resource_entry_free(resource_pool_t *pool, resource_entry_t **ptr)
{
  resource_entry_t *entry ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;

  entry = *ptr ;
  *ptr = NULL ;

  HQASSERT(entry->state == TASK_RESOURCE_FREE,
           "Resource entry is still in use") ;
  pool->free(pool, entry) ;
  mm_free(mm_pool_task, entry, sizeof(*entry)) ;

  HQASSERT(pool->nresources > 0, "Freed too many resource entries") ;
  --pool->nresources ;
}

/* Log stripped */
