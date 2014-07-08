/** \file
 * \ingroup RDR
 *
 * $HopeName: SWrdr!src:rdr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This file provides the ROM Data Resource API.
 *
 * The RDR API is primarily used to share blocks of data between the skin and
 * core, or in general, between multiple Providers and Consumers.
 */

#define RDR_IMPLEMENTOR
#include "rdrpriv.h"

/* -------------------------------------------------------------------------- */

/* Multithreading controls */
pthread_mutex_t rdr_mt = PTHREAD_MUTEX_INITIALIZER ;
pthread_cond_t  rdr_cv ;
int32 rdr_iterations = 0 ;          /* Concurrent iterations (not iterators) */

/* RDR system */
sw_rdr *rdr_list = NULL ; /* A descending list of RDRs (for now) */
sw_rdr_iterator *live_list = NULL ; /* List of active iterators */
sw_rdr_iterator *dead_list = NULL ; /* List of dead/new iterators */

/* -------------------------------------------------------------------------- */

#if 0
sw_rdr_cmp(void * pa, void * pb)
{
  sw_rdr * a = (sw_rdr *) pa ;
  sw_rdr * b = (sw_rdr *) pb ;
  int32 cmp ;
  if ( (cmp = b->Class - a->Class) == 0 &&
       (cmp = b->Type  - a->Type ) == 0 &&
       (cmp = b->Id    - a->Id   ) == 0 )
    cmp = a->Priority - b->Priority ;
  return cmp ;
}
#endif

/* ========================================================================== */
/** Find an iterator in one list and if found move it to another.
    This MUST be protected by a mutex. */

void move_iterator(sw_rdr_iterator * find,
                   sw_rdr_iterator ** from,
                   sw_rdr_iterator ** to)
{
  sw_rdr_iterator ** ptr ;
  sw_rdr_iterator * it ;

  ptr = from ;
  while ((it = *ptr) != find && it != NULL)
    ptr = &it->next ;
  if (it == find) {
    *ptr = it->next ;
    it->next = *to ;
    *to = it ;
  }
}

/* ========================================================================== */
/** \brief Find an rdr from an iterator structure.

    This is the central find routine used by all API find and iterate calls, and
    used internally by the (de)registration calls.

    As well as finding the first matching RDR for a particular set of criteria,
    it returns the parent of the returned RDR (or the last in the list), and
    the next RDR to check (for continuing the search).

    Note that when multithreading this function MUST be protected by mutex on
    entry. It can optionally unlock the mutex during the search.

    \param[in] find    The iterator to use and update. When multithreading,
                       access to iterators MUST be protected by mutex.

    \param[in] unlock  TRUE if called with a mutex that is to be unlocked during
                       the iteration. The concurrency count is incremented and
                       the mutex unlocked, then locked and decremented on exit.

    \return            The RDR found, or NULL. Note that when multithreading the
                       RDR found by find_rdr could nevertheless be deregistered
                       before this function has returned so must be protected
                       against by the caller using mutex.
*/

sw_rdr * find_rdr(sw_rdr_iterator * find, int unlock)
{
  sw_rdr * rdr, * next = 0, * parent = 0 ;
  int32 flags, Class, Type, Id, Priority ;
  HqBool kill_it = FALSE ;

  HQASSERT(find, "No iterator!") ;

  /* Mutex MUST be locked on entry. It can optionally be unlocked under the
     protection of a concurrency count */
  flags = find->flags ;
  find->flags |= ITERATOR_BUSY ;

  /* If we have a new iterator it will have to be moved from the dead list to
     the live list. Do this while we still have the lock! */
  if ((flags & (ITERATOR_DEAD|ITERATOR_BUSY)) == 0 && find->rdr == 0)
    move_iterator(find, &dead_list, &live_list) ;

  /* Now unlock during a normal iteration - there can be many RDRs */
  if (unlock) {
    ++rdr_iterations ;
    pthread_mutex_unlock(&rdr_mt) ;
  }

  if ((flags & ITERATOR_BUSY) != 0) {
    /* This iterator is already in use. This is very bad! Quite how we managed
       to get the same iterator used in multiple threads I can't imagine. */
    HQFAIL("Reusing busy iterator");
    rdr = FAILURE(NULL) ;

  } else { /* not busy - the usual case */

    rdr      = find->rdr ;    /* next RDR to check */
    parent   = find->parent ; /* parent of the previously returned RDR */
    Class    = find->Class ;
    Type     = find->Type ;
    Id       = find->Id ;
    Priority = find->Priority ;

    if ((flags & ITERATOR_DEAD) != 0) {
      /* The previously returned RDR was the last in the list but has since been
         deregistered. If the deregistration had only zeroed the iterator's
         rdr field, the iteration would now begin again... so this flag
         indicates the end of the list has 'already' been reached. */
      flags ^= ITERATOR_DEAD ;
    } else {
      if ( rdr == 0 ) {
      /* New iteration - start at the beginning.
         Iterator has already been moved to the live list */
        parent = 0 ;
        rdr = rdr_list ;

      } else {
        /* Update the parent for the usual case */
        if (parent && parent->next && parent->next->next == rdr)
          parent = parent->next ;
        else {
          /* We've lost track of the parent, find it again */
          if ((parent = rdr_list) == rdr)
            parent = NULL ;
          else
            while (parent && parent->next != rdr)
              parent = parent->next ;
        }
      }
    }

    /* Find the next matching RDR. Note that concurrent Find iterations are
       allowed if the RDR list is not being altered */
    while (rdr) {
      int32 cmp = 0 ;
      if (((flags & CHECK_CLASS)   != 0 && (cmp = rdr->Class    - Class)   != 0) ||
          ((flags & CHECK_TYPE)    != 0 && (cmp = rdr->Type     - Type)    != 0) ||
          ((flags & CHECK_ID)      != 0 && (cmp = rdr->Id       - Id)      != 0) ||
          /* We deliver the above values low first, but priority high first */
          ((flags & CHECK_PRIORITY)!= 0 && (cmp = Priority - rdr->Priority)!= 0)) {

        if (cmp > 0) {        /* list is ascending, so we can bail out early */
          rdr = 0 ;           /* no match, but parent is insertion point */
          break ;             /* failed to find a match */
        }

        parent = rdr ;
        rdr = rdr->next ;
      } else
        break ;               /* found a match */
    }

    if (rdr) {
      /* Now set next RDR to check, skipping lower priority if appropriate */
      next = rdr->next ;
      if ((flags & ITERATE_PRIORITIES) == 0) {
        /* If not iterating through priority levels, skip lower priorities */
        while (next &&
               (next->Class == rdr->Class) &&
               (next->Type  == rdr->Type) &&
               (next->Id    == rdr->Id))
          next = next->next ;
      }
    }

    if (next == 0) {
      flags |= ITERATOR_DEAD ;

      /* If this iterator is in the live list, move it to the dead list */
      kill_it = TRUE ;
    }

    /* It's OK to write into the iterator, because there's no registrations */
    find->rdr    = next ;     /* Not back to zero at end of list */
    find->parent = parent ;
    find->flags  = flags ;    /* No longer busy */
  } /* if !busy */

  /* Optionally lock mutex again and decrement the concurrency count */
  if (unlock) {
    pthread_mutex_lock(&rdr_mt) ;
    if (--rdr_iterations == 0)
      pthread_cond_signal(&rdr_cv) ;  /* signal blocked writer */
  }

  /* If the iterator has reached the end of the list, move it to the
     dead list, but only once the lock has been reapplied. */
  if (kill_it)
    move_iterator(find, &live_list, &dead_list) ;

  return rdr ;
}

/* ========================================================================== */
/** \brief Link an iterator into a list */

void link_iterator(sw_rdr_iterator * iterator, sw_rdr_iterator ** list)
{ /* Critical */
  iterator->next = *list ;
  *list = iterator ;
}


/** \brief unlink an iterator from either list */

void unlink_iterator(sw_rdr_iterator * iterator)
{ /* Critical */
  sw_rdr_iterator ** ptr ;
  sw_rdr_iterator * it ;

  ptr = &live_list ;
  while ((it = *ptr) != iterator && it != NULL)
    ptr = &it->next ;
  if (it != iterator) {
    ptr = &dead_list ;
    while ((it = *ptr) != iterator && it != NULL)
      ptr = &it->next ;
    HQASSERT(it == iterator, "Iterator not found in either list") ;
  }
  if (it == iterator && ptr)
    *ptr = it->next ;
  it->next = NULL ;
}


/** \brief Create a new iterator and add it to the list */

sw_rdr_iterator * new_iterator(int32 flags)
{
  sw_rdr_iterator * it ;

  it = rdr_alloc(sizeof(sw_rdr_iterator), MM_ALLOC_CLASS_RDR_ITER) ;

  if (it) {
    static sw_rdr_iterator zero = {ITERATOR} ;
    *it = zero ;
    it->flags = flags ;
    pthread_mutex_lock(&rdr_mt) ;
    link_iterator(it, &dead_list) ;  /* dead_list in case of long persistence */
    pthread_mutex_unlock(&rdr_mt) ;
  }

  return it ;
}

/* -------------------------------------------------------------------------- */
/** \brief Unlock a previously locked RDR */

void unlock_previous_rdr(sw_rdr_iterator * iterator)
{
  sw_rdr * rdr = iterator->locked ;

  if (rdr) {
    iterator->locked = NULL ;

    /* If SwDeregisterRDR() has been called when locked, next points to self */
    if ((--rdr->lock == 0) && rdr == rdr->next)
      rdr_free(rdr, sizeof(sw_rdr)) ;
  }
}

/* -------------------------------------------------------------------------- */
/** \brief Destroy the iterator */

sw_rdr_result HQNCALL SwFoundRDR(sw_rdr_iterator * iterator)
{
  sw_rdr_result result = SW_RDR_SUCCESS ;

  /* We ignore null iterators to make error handling more lenient */
  if (iterator == 0)
    return SW_RDR_SUCCESS ;

  /* But we don't ignore broken or already-freed ones */
  if (iterator->magic != ITERATOR)
    return FAILURE(SW_RDR_ERROR_SYNTAX) ;

  pthread_mutex_lock(&rdr_mt) ;
  if ((iterator->flags & ITERATOR_BUSY) != 0)
    result = FAILURE(SW_RDR_ERROR_IN_USE) ;
  else {
    unlock_previous_rdr(iterator) ;

    unlink_iterator(iterator) ;

    iterator->magic = 0 ;
  }
  pthread_mutex_unlock(&rdr_mt) ;

  if (iterator->magic == 0)
    rdr_free(iterator, sizeof(sw_rdr_iterator)) ;

  return result ;
}

/* ========================================================================== */
/** \brief Find a specific RDR by Class, Type and ID - the usual case */

sw_rdr_result HQNCALL SwFindRDR(sw_rdr_class rdrclass,
                                sw_rdr_type rdrtype,
                                sw_rdr_id rdrid,
                                void ** pptr,
                                size_t * plength)
{
  sw_rdr_iterator it   = {ITERATOR,CHECK_ALL} ;
  sw_rdr_result result = SW_RDR_ERROR_UNKNOWN ;
  sw_rdr * rdr ;

  it.Class = rdrclass ;
  it.Type  = rdrtype ;
  it.Id    = rdrid ;

  pthread_mutex_lock(&rdr_mt) ;  /* MUST lock mutex before calling find_rdr */
  link_iterator(&it, &live_list) ;
  rdr = find_rdr(&it, TRUE) ;    /* Iteration concurrency ok, mutexed on exit */
  unlink_iterator(&it) ;
  /* Note, cannot unlock mutex until we've finished with returned rdr */

  if (rdr && (rdr->ptr || rdr->length)) {
    /* If registered and not suppressed, return details and success */
    if (pptr)    *pptr    = rdr->ptr ;
    if (plength) *plength = rdr->length ;
    result = SW_RDR_SUCCESS ;
  }
  pthread_mutex_unlock(&rdr_mt) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
/** \brief Restart an iteration of RDRs */

sw_rdr_result HQNCALL SwRestartFindRDR(/*@in@*/ sw_rdr_iterator * iterator)
{
  sw_rdr_result result = SW_RDR_SUCCESS ;

  /* Error unrecognised iterators */
  if (!iterator || iterator->magic != ITERATOR)
    return SW_RDR_ERROR_SYNTAX ;

  pthread_mutex_lock(&rdr_mt) ;
  if ((iterator->flags & ITERATOR_BUSY) != 0)
    result = FAILURE(SW_RDR_ERROR_IN_USE) ;
  else {
    unlock_previous_rdr(iterator) ;

    /* if in the live list move to the dead list */
    move_iterator(iterator, &live_list, &dead_list) ;

    iterator->flags &= ~ITERATOR_DEAD ;
    iterator->rdr    = 0 ;
    iterator->parent = 0 ;
  }
  pthread_mutex_unlock(&rdr_mt) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
/** \brief Begin an iteration of RDRs by Class and Type */

sw_rdr_iterator * HQNCALL SwFindRDRbyType(sw_rdr_class rdrclass,
                                          sw_rdr_type rdrtype)
{
  sw_rdr_iterator * it = new_iterator(CHECK_CLASS|CHECK_TYPE) ;

  if (it) {
    it->Class = rdrclass ;
    it->Type  = rdrtype ;
  }

  return it ;
}

/* -------------------------------------------------------------------------- */
/** \brief Begin an iteration of RDRs by Class */

sw_rdr_iterator * HQNCALL SwFindRDRbyClass(sw_rdr_class rdrclass)
{
  sw_rdr_iterator * it = new_iterator(CHECK_CLASS) ;

  if (it) {
    it->Class = rdrclass ;
  }

  return it ;
}

/* -------------------------------------------------------------------------- */
/** \brief Begin an iteration of all RDRs (the highest priority of each) */

sw_rdr_iterator * HQNCALL SwFindRDRs(void)
{
  return new_iterator(CHECK_ANY) ;
}

/* -------------------------------------------------------------------------- */
/** \brief Find and optionally lock an RDR from an iterator

    This is an internal call but is marked HQNCALL in case that makes it easier
    to optimise the tail-calls in SwNextRDR() and SwLockNextRDR() below.
*/

static sw_rdr_result HQNCALL find_next_rdr(sw_rdr_iterator * iterator,
                                           sw_rdr_class * pclass,
                                           sw_rdr_type * ptype,
                                           sw_rdr_id * pid,
                                           void ** pptr,
                                           size_t * plength,
                                           HqBool lock)
{
  sw_rdr_result result = SW_RDR_ERROR_UNKNOWN ;
  sw_rdr * rdr ;

  if (!iterator || iterator->magic != ITERATOR)
    return FAILURE(SW_RDR_ERROR_SYNTAX) ;

  pthread_mutex_lock(&rdr_mt) ;  /* MUST lock mutex before calling find_rdr */

  unlock_previous_rdr(iterator) ;

  do { /* Find next matching RDR, ignoring suppressed registrations */
    rdr = find_rdr(iterator, TRUE) ;
  } while (rdr && rdr->ptr == 0 && rdr->length == 0) ;

  if (rdr) {
    if (pclass)  *pclass  = rdr->Class ;
    if (ptype)   *ptype   = rdr->Type ;
    if (pid)     *pid     = rdr->Id ;
    if (pptr)    *pptr    = rdr->ptr ;
    if (plength) *plength = rdr->length ;

    result = SW_RDR_SUCCESS ;

    if (lock) {
      iterator->locked = rdr ;
      ++rdr->lock ;
    }
  }
  pthread_mutex_unlock(&rdr_mt) ;

  return result ;
}

/** \brief Find an RDR from an iterator */

sw_rdr_result HQNCALL SwNextRDR(sw_rdr_iterator * iterator,
                                sw_rdr_class * pclass,
                                sw_rdr_type * ptype,
                                sw_rdr_id * pid,
                                void ** pptr,
                                size_t * plength)
{
  return find_next_rdr(iterator, pclass, ptype, pid, pptr, plength, FALSE) ;
}

/** \brief Find and lock an RDR from an iterator */

sw_rdr_result HQNCALL SwLockNextRDR(sw_rdr_iterator * iterator,
                                    sw_rdr_class * pclass,
                                    sw_rdr_type * ptype,
                                    sw_rdr_id * pid,
                                    void ** pptr,
                                    size_t * plength)
{
  return find_next_rdr(iterator, pclass, ptype, pid, pptr, plength, TRUE) ;
}

/* ========================================================================== */
#if 0
/** \brief Map from RDR error to Postscript error */

int32 rdr_error(sw_rdr_result error)
{
  switch (error) {
  case SW_RDR_ERROR_UNKNOWN:
    return UNDEFINEDRESOURCE ;

  case SW_RDR_ERROR_MEMORY:
    return VMERROR ;

  case SW_RDR_ERROR_SYNTAX:
    return SYNTAXERROR ;

  case SW_RDR_ERROR_IN_USE:
    return INVALIDACCESS ;

  case SW_RDR_SUCCESS:
    return NO_ERROR ;
  }

  return UNREGISTERED ;
}
#endif
/* ========================================================================== */
/** \brief Create and initialise a new sw_rdr structure, but don't add it */
static sw_rdr * new_rdr(sw_rdr * rdr,
                        sw_rdr_class rdrclass,
                        sw_rdr_type rdrtype,
                        sw_rdr_id rdrid,
                        void * ptr,
                        size_t length,
                        sw_rdr_priority priority)
{
  if (!rdr)
    rdr = rdr_alloc(sizeof(sw_rdr), MM_ALLOC_CLASS_RDR) ;

  if (rdr) {
    sw_rdr zero = {0} ;
    *rdr = zero ;

    rdr->Class    = rdrclass ;
    rdr->Type     = rdrtype ;
    rdr->Id       = rdrid ;
    rdr->Priority = priority ;
    rdr->ptr      = ptr ;
    rdr->length   = length ;
  }

  return rdr ;
}

/* -------------------------------------------------------------------------- */
/** \brief Register or reregister an RDR

    A registration with zero ptr and length suppressed this Class/Type/ID as
    though nothing has been registered.
*/

sw_rdr_result HQNCALL SwRegisterRDR(sw_rdr_class rdrclass,
                                    sw_rdr_type rdrtype,
                                    sw_rdr_id rdrid,
                                    void * ptr,
                                    size_t length,
                                    sw_rdr_priority priority)
{
  sw_rdr_iterator it = {ITERATOR,CHECK_DUPLICATES} ;
  int finished = FALSE ;
  sw_rdr * newrdr ;
  sw_rdr * rdr ;

  /* We will probably need an allocation, and we don't want to do it inside
     the mutex lock, so do it now and if we still have it at the end, free
     it then. This is not really a performance issue. */
  newrdr = rdr_alloc(sizeof(sw_rdr), MM_ALLOC_CLASS_RDR) ;
  if (newrdr == 0)
    return FAILURE(SW_RDR_ERROR_MEMORY) ;

  it.Class    = rdrclass ;
  it.Type     = rdrtype ;
  it.Id       = rdrid ;

  pthread_mutex_lock(&rdr_mt) ;
  link_iterator(&it, &live_list) ;

  /* If there are iterations in progress, wait for them to finish.
     Note this implicitly unlocks the mutex while it waits. */
  while (rdr_iterations) {
    while (pthread_cond_wait(&rdr_cv, &rdr_mt) != 0) ;
  }

  rdr = find_rdr(&it, FALSE) ;  /* Concurrency not allowed during changes */

  if (rdr) {
    /* There is already a registration of that Class, Type and ID.

       This may be a reregistration, or it may have to slot in amongst
       lower priority matches than the one we've just found. Regardless,
       we'll keep a copy of the parent, it being the parent of the highest
       priority match and the obvious place to start searching for the
       insertion point later.
    */
    sw_rdr * realparent = it.parent ;

    /* First check if this is a reregistration.

       This involves stepping through lower priority matches until we
       find the existing registration or we fail to find a match.
       If we match, it.parent is its parent.
    */
    while (rdr && (rdr->ptr != ptr || rdr->length != length))
      rdr = find_rdr(&it, FALSE) ;  /* Note: Will find lower priorities too */

    if (rdr && rdr->Priority == priority) {
      /* No change, so do nowt */
      finished = TRUE ;  /* drop through and tidy up */
      rdr = NULL ;
    }

    if (rdr) {
      /* This IS a reregistration. Detach and reprioritise. */
      if (it.parent)
        it.parent->next = rdr->next ;
      else
        rdr_list = rdr->next ;

      rdr->Priority = priority ;
      rdr->next = 0 ;
    }

    /* Restore original parent, so we can skip the start of the list when
       looking for the most appropriate insertion point later.
    */
    it.parent = realparent ;
  }

  if (!finished) {
    if (!rdr) {
      /* Create a new RDR */
      rdr = new_rdr(newrdr, rdrclass, rdrtype, rdrid, ptr, length, priority) ;
      newrdr = NULL ;
    }

    if (rdr) {
      /* Find where to put the RDR in the list.

         We start at it.parent, and search through all priority levels for the
         first match (or lower priority, or wrong Class/Type/Id). it.parent will
         then be *OUR* new parent. */
      it.Priority = priority ;
      it.flags = CHECK_INSERT ;
      it.rdr = it.parent ;
      (void)find_rdr(&it, FALSE) ;   /* We are finding the insertion point here */

      /* insert it */
      if (it.parent) {
        rdr->next = it.parent->next ;
        it.parent->next = rdr ;
      } else {
        rdr->next = rdr_list ;
        rdr_list = rdr ;
      }
    }
  }
  unlink_iterator(&it) ;

  /* Unlock the RDR variables, and wake up any other pending updates */
  pthread_cond_signal(&rdr_cv) ;
  pthread_mutex_unlock(&rdr_mt) ;

  /* If it was a reregistration, we didn't need this after all */
  if (newrdr)
    rdr_free(newrdr, sizeof(sw_rdr)) ;

  return SW_RDR_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/** \brief Register an RDR allocating a unique ID

    The allocated ID is guaranteed not to match any existing registration.
    It is returned if required.
*/

sw_rdr_result HQNCALL SwRegisterRDRandID(sw_rdr_class rdrclass,
                                         sw_rdr_type rdrtype,
                                         sw_rdr_id * prdrid,
                                         void * ptr,
                                         size_t length,
                                         sw_rdr_priority priority)
{
  sw_rdr_iterator it = {ITERATOR,CHECK_CLASS|CHECK_TYPE} ;
  sw_rdr_result result = SW_RDR_SUCCESS ;
  sw_rdr_id id = 0 ;          /* Start with id zero */
  sw_rdr * rdr ;

  it.Class = rdrclass ;
  it.Type  = rdrtype ;

  pthread_mutex_lock(&rdr_mt) ;
  link_iterator(&it, &live_list) ;

  do {
    rdr = find_rdr(&it, FALSE) ;  /* Find (another) RDR of this type */

    if (rdr) {                   /* If there is one, is it using this ID? */
      if (rdr->Id == id) {       /* If already used, increment the ID */
        if (id == MAXINT32) {    /* If we can't, go negative and start again */
          id = MININT32 ;
          it.rdr = NULL ;
          it.flags &= ~ITERATOR_DEAD ;
        } else {
          if (++id == 0) {       /* Increment, and if we get to zero, blow up */
            result = FAILURE(SW_RDR_ERROR) ;
            rdr = NULL ;
          }
        }
      } else
        rdr = NULL ;
    }
  } while (rdr) ;
  unlink_iterator(&it) ;
  pthread_mutex_unlock(&rdr_mt) ;

  if (result != SW_RDR_SUCCESS)
    return result ;


  if (prdrid)                    /* Return new ID, if required */
    *prdrid = id ;

  /* Now register it */
  return SwRegisterRDR(rdrclass, rdrtype, id, ptr, length, priority) ;
}

/* -------------------------------------------------------------------------- */
/** \brief Deregister an RDR

    This removes a registration, and also updates any current iterators that
    are in any way linked to this RDR.
*/

sw_rdr_result HQNCALL SwDeregisterRDR(sw_rdr_class rdrclass,
                                      sw_rdr_type rdrtype,
                                      sw_rdr_id rdrid,
                                      void * ptr,
                                      size_t length)
{
  sw_rdr_iterator * live, it = {ITERATOR,CHECK_ALL|ITERATE_PRIORITIES} ;
  sw_rdr_result result = SW_RDR_SUCCESS ;
  sw_rdr * rdr ;

  it.Class = rdrclass ;
  it.Type  = rdrtype ;
  it.Id    = rdrid ;

  pthread_mutex_lock(&rdr_mt) ;
  link_iterator(&it, &live_list) ;

  /* If there are iterations in progress, wait for them to finish.
     Note this implicitly unlocks the mutex while it waits. */
  while (rdr_iterations) {
    while (pthread_cond_wait(&rdr_cv, &rdr_mt) != 0) ;
  }

  do {
    rdr = find_rdr(&it, FALSE) ;
  } while (rdr && (rdr->ptr != ptr || rdr->length != length)) ;

  if (!rdr || rdr->ptr != ptr || rdr->length != length) {
    result = FAILURE(SW_RDR_ERROR_UNKNOWN) ;
    rdr = NULL ;
  }

  /* Detach from RDR list */
  if (rdr) {
    if (it.parent)
      it.parent->next = rdr->next ;
    else
      rdr_list = rdr->next ;
    rdr->next = NULL ;
  }
  unlink_iterator(&it) ;

  /* Update active iterators */
  if (rdr) {
    live = live_list ;
    while (live) {
      sw_rdr_iterator *next ;

      next = live->next ;
      if (live->parent == rdr)
        live->parent = it.parent ;
      if (live->rdr == rdr) {
        live->rdr = rdr->next ;
        if (live->rdr == 0) {
          live->flags |= ITERATOR_DEAD ;
          move_iterator(live, &live_list, &dead_list) ;
        }
      }
      live = next ;
    }

    if (rdr->lock) {
      /* Locked RDRs are not deleted, but are marked for death */
      rdr->next = rdr ;

      result = SW_RDR_ERROR_IN_USE ;
      rdr = NULL ;
    }

  } else { /* !rdr */

    /* Not found in the RDR list, but is it still locked after deregistration
       has been tried once? If so, it will be pointed to by a live iterator
       and will be marked for death. */
    for ( live = live_list ; live != NULL ; live = live->next ) {
      sw_rdr *locked = live->locked ;
      if ( locked != NULL &&
           locked->Class  == rdrclass && locked->Type   == rdrtype &&
           locked->Id     == rdrid    && locked->ptr    == ptr     &&
           locked->length == length ) {
        HQASSERT(locked == locked->next, "Unlinked RDR not marked for death") ;
        result = SW_RDR_ERROR_IN_USE ;
        break ;
      }
    }
  }

  pthread_mutex_unlock(&rdr_mt) ;

  if (rdr)
    rdr_free(rdr, sizeof(sw_rdr)) ;

  return result ;
}

/* ========================================================================== */
