/** \file
 * \ingroup RDR
 *
 * $HopeName: SWrdr!src:rdrpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This file provides definitions for the core ROM Data Resource API.
 */

#include "std.h"

#ifdef LESDK

#  include "mem.h"

#  define rdr_alloc(_size, _type) MemAlloc(_size, FALSE, FALSE)
#  define rdr_free(_block, _size) MemFree(_block)

#else

#  define rdr_alloc(_size, _type) malloc(_size)
#  define rdr_free(_block, _size) free(_block)

#endif

#include "threadapi.h"
#include "rdrapi.h"
#include "eventapi.h"

#ifndef FAILURE
#  define FAILURE(_val) (_val)
#endif

/* -------------------------------------------------------------------------- */
/** \brief Our internal representation of an RDR registration.

    These are never exposed to Providers or Consumers.

    Currently this is a linked list stored in ascending order of Class, Type,
    Id and descending Priority, but this may be restructured if the RDR system
    gains significant use.

    Note that when multithreading, an RDR could conceivably be deregistered by
    another thread after it has been found but before it has been accessed, so
    all such usage MUST be protected by mutex.
*/

typedef struct sw_rdr {
  sw_rdr_class    Class ;    /**< Capitalised because VC recognises 'class' */
  sw_rdr_type     Type ;
  sw_rdr_id       Id ;
  sw_rdr_priority Priority ; /**< Higher priorities take precedence */
  void *          ptr ;      /**< Provider's ptr */
  size_t          length ;   /**< Provider's length */
  int             lock ;     /**< Number of SwLockNextRDR() calls on this RDR */
  struct sw_rdr * next ;     /**< A linked list (for now) */
} sw_rdr ;

/* -------------------------------------------------------------------------- */
/** \brief Our private iterator structure.

    We manage these internally, to cope with (de)registrations mid-iteration.

    These are opaque to Consumers, and are not used by Providers.

    Note that when multithreading, all accesses of any of these values MUST be
    performed under mutex, as the contents can be changed by other threads.
*/

struct sw_rdr_iterator {
  int32           magic ;         /**< Magic value to check iterator */
  int32           flags ;         /**< Which values to check */
  sw_rdr_class    Class ;         /**< if flags.b0 */
  sw_rdr_type     Type ;          /**< if flags.b1 */
  sw_rdr_id       Id ;            /**< if flags.b2 */
  sw_rdr_priority Priority ;      /**< if flags.b3 */
  sw_rdr *        rdr ;           /**< next RDR to check */
  sw_rdr *        parent ;        /**< parent of returned RDR */
  sw_rdr *        locked ;        /**< current RDR if locked */
  struct sw_rdr_iterator * next ; /**< list of current iterators */
} ;

/* -------------------------------------------------------------------------- */

#define ITERATOR 0x4D4ff4D4   /* Magic value to check initialisation */

/** \brief Iterator flags */

#define CHECK_CLASS    1  /**< Class must match */
#define CHECK_TYPE     2  /**< Type must match */
#define CHECK_ID       4  /**< Id must match */
#define CHECK_PRIORITY 8  /**< Priority must match - internal use only */

#define ITERATOR_DEAD  16 /**< Iterator has been killed by a deregistration */
#define ITERATOR_BUSY  32 /**< Iterator is in use */

#define ITERATE_PRIORITIES 64 /**< Lower priorities also returned - internal use */

/* Useful combinations of the above flags */
#define CHECK_ANY        0
#define CHECK_ALL        CHECK_CLASS|CHECK_TYPE|CHECK_ID
#define CHECK_INSERT     CHECK_ALL|CHECK_PRIORITY
#define CHECK_DUPLICATES CHECK_ALL|ITERATE_PRIORITIES

/* -------------------------------------------------------------------------- */

void link_iterator(sw_rdr_iterator * iterator, sw_rdr_iterator ** list) ;
void unlink_iterator(sw_rdr_iterator * iterator) ;
void unlock_previous_rdr(sw_rdr_iterator * iterator) ;

extern pthread_mutex_t rdr_mt ;
extern pthread_cond_t  rdr_cv ;
extern int32 rdr_iterations ;

extern sw_rdr *rdr_list ;
extern sw_rdr_iterator *live_list ;
extern sw_rdr_iterator *dead_list ;

/* ========================================================================== */
