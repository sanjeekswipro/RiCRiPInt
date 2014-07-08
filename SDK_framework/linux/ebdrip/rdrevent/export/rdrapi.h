/** \file
 * \ingroup RDR
 *
 * $HopeName: SWrdrapi!rdrapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This header file provides the definition of the ROM Data Resource
 * API.
 *
 * The RDR API is primarily used to share blocks of data between the skin and
 * core, or in general, between multiple Providers and Consumers.
 */

#ifndef __RDRAPI_H__
#define __RDRAPI_H__

/** \defgroup swrdr RDR ROM Data Resource
    \ingroup interface
    \{ */

#include <stddef.h>  /* size_t */

#include "hqncall.h"

#ifndef MAXINT32
#define MAXINT32 ((int32)0x7FFFFFFF)
#endif

#ifndef MININT32
#define MININT32 ((int32)(-0x7FFFFFFF - 1))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/** \brief  HqnIdent

    HqnIdent is a superclass of identifiers used by the RDR, Event and Timeline
    APIs and are defined to be interworkable. It is therefore possible to use
    a Timeline reference as an RDR ID within some Class and Type, for example.
 */

typedef int32 HqnIdent ;

#define HQNIDENT_MAX MAXINT32 /* maximum value before wrap-around */
#define HQNIDENT_MIN MININT32 /* minimum value after wrap-around */

/* ========================================================================== */
/* RDR system initialisation and finalisation */


int HQNCALL rdr_start(void) ;

void HQNCALL rdr_end(void) ;

/* -------------------------------------------------------------------------- */
/** \page rdrapi  The ROM Data Resource API
 *
 * The ROM Data Resource (RDR) API allows data to be discovered at run time in
 * RIP, rather than being statically linked at build time. This allows complete
 * decoupling of skin-supplied, RIP-consumed data (or vice versa); allows the
 * skin to optionally override RIP-embedded data; and even allows data to be
 * selected contextually.
 *
 * Individual blocks of data - RDRs - are identified by Class, Type and ID.
 * A Consumer can iterate through available RDRs, filtered by Class and Type if
 * required, or can directly locate a specific RDR.
 *
 * \section RDRClasses  RDR Classes
 *
 * RDR classes are enumerated in this header file. Classes are subdivided into
 * Types. The distinction between Class and Type is usually clear from the
 * context - embedded fonts registered as RDRs might have a Class of "Font",
 * and a Type indicating their format.
 *
 * \section RDRTypes  RDR Types
 *
 * RDR Types are enumerated elsewhere, as they are unique only within a single
 * Class. For example, the Font Class may use the value "1" to indicate a Type1
 * font, "42" to indicate a TrueType, etc. Within another Class, Type "1" will
 * have a different meaning.
 *
 * \section RDRIDs  RDR IDs
 *
 * Individual RDRs of a particular Class and Type are discerned by ID. There
 * can only ever be one RDR with a particular Class:Type:ID combination. The
 * most recently registered (or highest priority) RDR with that combination is
 * the visible one - see below for prioritisation.
 *
 * Previous registrations are remembered in chronological order, and will be
 * restored if the current RDR is deregistered. This allows contextual
 * overriding of a default RDR.
 *
 * \section RDRPriority  RDR Priority
 *
 * Ideally, where two RDRs of the same Class:Type:ID are registered, the first
 * would be the default definition, and the second the overridden version. In
 * practice this chronology may be difficult to ensure, so RDR registration
 * takes a priority parameter. High priority registrations always override
 * lower priority registrations, regardless of registration ordering. Strict
 * chronological ordering is still maintained within any one priority level.
 *
 * \section RDRFormat  RDR Format
 *
 * An RDR is simply a contiguous block of memory - though it doesn't have to
 * be in ROM or even part of the executable, that is its primary purpose.
 * As long as Provider and Consumer agree, the data may not even have to exist.
 * RDRs are registered through the RDR API with a Class, Type and ID, plus a
 * pointer to the data and its length. Class and Type must be specified, but
 * an ID can be allocated automatically if required. RDRs can also be removed,
 * by deregistering them by their Class, Type and ID, or by their location and
 * length.
 *
 * \section RDRUses  RDR Uses
 *
 * RDRs can be used for many things in addition to registering and discovering
 * blocks of prebuilt data. The RDR System itself places no interpretation on
 * the content of any RDR - the interpretation is defined by the owner of the
 * RDR Class in question. The only exception is a null pointer, zero length
 * registration, which has a special meaning described later.
 *
 * RDRs can therefore be used to do any of the following, and many more:
 *
 * 1. Identify a block of data.
 *
 * 2. Identify a memory location, such as a threshold or limit.
 *
 * 3. Identify a memory size, such as a cache limit.
 *
 * 4. Supply a filename or environment string (the data being the C string)
 *
 * 5. Specify PostScript fragments to be run at job start.
 *
 */
/* -------------------------------------------------------------------------- */
/** \brief  RDR Classes

    Classes are defined here and subdivided into Types defined elsewhere.
*/

enum {
  RDR_CLASS_EVENT = -1, /**< Asynchronous Events and Messages */

  RDR_CLASS_RDR = 1,    /**< For RDR's internal use */
  RDR_CLASS_FONT,       /**< Embedded font files */
  RDR_CLASS_PCL,        /**< PCL related data tables */
  RDR_CLASS_API,        /**< Function tables, such as skin-supplied UFST.
                             Type is identifier, ID is version number.
                             Types are defined in apis.h */
  RDR_CLASS_TIMELINE,   /**< Timeline specific definitions */
  RDR_CLASS_LOW_MEM     /**< Low-memory handlers */
} ;

typedef HqnIdent sw_rdr_class ;

/* -------------------------------------------------------------------------- */
/** \brief  RDR Types

    Types are defined on a per-Class basis elsewhere.
*/

typedef HqnIdent sw_rdr_type ;

/* -------------------------------------------------------------------------- */
/** \brief  RDR ID

    IDs are unique per Class and Type, and are either predefined elsewhere or
    allocated on demand by SwRegisterRDRandID().
*/

typedef HqnIdent sw_rdr_id ;

/* -------------------------------------------------------------------------- */
/** \brief  RDR priority

    RDRs defined with the same Class:Type:ID combination are held in strict
    chronological order, with the most recent registration being the visible
    definition. Prioritisation allows this chronology to be overridden, in that
    low priority RDRs are guaranteed to be superseded by higher priority
    registrations, regardless of chronology. Strict chronological ordering is
    maintained for RDRs at the same priority level.
*/

typedef int32 sw_rdr_priority ;

/** \brief  Enumeration of RDR priorities

    It is defined that the usual case is a priority of zero, so SW_RDR_NORMAL
    can be replaced with "0" in actual code.

    Priority is primarily used to discern default from override definitions,
    but within any particular Class and Type, prioritisation can be used to
    implement any weighting, preference or selection process.

    Note however that priority is an attribute of the Provider's registration,
    not of the RDR that is being registered - the priority of the RDR is not
    revealed to the Consumer.
 */

enum {
  SW_RDR_DEFAULT  = -10000, /**< Known to be a default */
  SW_RDR_NORMAL   =      0, /**< The usual case */
  SW_RDR_OVERRIDE =  10000  /**< High priority registration */
} ;

/* -------------------------------------------------------------------------- */
/** \brief  Return values from RDR API calls. */

enum {
  SW_RDR_SUCCESS = 0,   /**< Normal return value */
  SW_RDR_ERROR,         /**< Some unknown failure occurred */
  SW_RDR_ERROR_UNKNOWN, /**< Specified RDR is undefined */
  SW_RDR_ERROR_SYNTAX,  /**< Programming error - illegal parameters */
  SW_RDR_ERROR_IN_USE,  /**< Re-entrancy error - iterator conflict */
  SW_RDR_ERROR_MEMORY   /**< Memory allocation failed */
} ;

typedef int sw_rdr_result ;

/* -------------------------------------------------------------------------- */
/** \brief  Iterator for finding multiple RDRs.

    These are created by any of the SwFindRDR*() calls, passed to SwNextRDR(),
    can be restarted by SwRestartFindRDR() and are destroyed by SwFoundRDR().

    They are opaque and should not be inspected or altered.
 */

typedef struct sw_rdr_iterator sw_rdr_iterator ;

/* -------------------------------------------------------------------------- */
/** \brief  Register an RDR.

    This call registers an RDR of a particular Class, Type and ID.

    \param[in] rdrclass  The Class of the RDR, as enumerated above.

    \param[in] rdrtype   The Type of the RDR.

      RDR Classes are subdivided into Types, which are enumerated elsewhere.

    \param[in] rdrid     The ID of the RDR within this Class and Type.

    \param[in] ptr       A pointer to the start of the data.

      This pointer can be null. See below for special behaviour if length is
      also zero.

    \param[in] length    The length of the data.

      This length can be zero. If ptr is NOT null then this delivers a
      zero-length RDR. This may still be meaningful to the consumer.

      If the length is zero AND the ptr is null (and this RDR is the highest
      priority registration), then any existing RDR of this Class, Type and ID
      is suppressed and therefore appears to the consumer to be unregistered.
      Deregistering this RDR will cause the previous registration to reappear.

    \param[in] priority  The priority of the registration - usually zero.

      Known defaults can be registered with a negative priority such as
      SW_RDR_DEFAULT. This ensures they are placed *below* any existing
      definition of the same Class:Type:ID combination (with a higher priority)
      - as though the default registration occured first.

      Similarly, a positive priority such as SW_RDR_OVERRIDE will remain the
      definitive registration until a registration at the same or higher
      priority is made.

    \return              SW_RDR_SUCCESS if the RDR was successfully registered.

    Reregistering an existing RDR (with the same parameters including ptr and
    length) does not produce multiple RDRs - it simply ensures that this RDR
    definition supersedes any of the same priority.
*/

sw_rdr_result HQNCALL SwRegisterRDR(sw_rdr_class rdrclass,
                                    sw_rdr_type rdrtype,
                                    sw_rdr_id rdrid,
                           /*@in@*/ void * ptr,
                                    size_t length,
                                    sw_rdr_priority priority) ;

/* -------------------------------------------------------------------------- */
/** \brief  Register an RDR and allocate it a unique ID.

    This call registers an RDR as above, but also allocates it a new ID which
    has not been used within that Class and Type.

    \param[in] rdrclass  The Class of the RDR.

    \param[in] rdrtype   The Type of the RDR.

    \param[out] prdrid   A pointer to an RDR ID, or null.

      An unused ID (within this Class and Type) will be allocated and returned
      if this pointer is not null. If no pointer is provided, an ID is still
      allocated but is not returned to the provider, so the RDR cannot be
      deregistered.

    \param[in] ptr       A pointer to the start of the data.

    \param[in] length    The length of the data.

    \param[in] priority  The priority of the registration - usually zero.

    \return              SW_RDR_SUCCESS if the RDR was successfully registered.

    SW_RDR_ERROR is returned in the unlikely event of every ID already having
    been allocated.
*/

sw_rdr_result HQNCALL SwRegisterRDRandID(sw_rdr_class rdrclass,
                                         sw_rdr_type rdrtype,
                               /*@out@*/ sw_rdr_id * prdrid,
                                /*@in@*/ void * ptr,
                                         size_t length,
                                         sw_rdr_priority priority) ;

/* -------------------------------------------------------------------------- */
/** \brief  Deregister an RDR.

    This call deregisters an RDR of a particular Class, Type and ID. Any
    previously registered RDR of that Class, Type and ID will reappear.

    \param[in] rdrclass  The Class of the RDR when registered.

    \param[in] rdrtype   The Type of the RDR when registered.

    \param[in] rdrid     The ID of the RDR within this Class and Type.

      If an ID was automatically allocated, it must be supplied to deregister
      the RDR.

    \param[in] ptr       A pointer to the start of the data.

      This must be the originally registered pointer.

    \param[in] length    The length of the data.

      This must be the originally registered length.

    \return              SW_RDR_SUCCESS if the RDR was deregistered.

      SW_RDR_ERROR_UNKNOWN is returned if the RDR could not be found.

      SW_RDR_ERROR_IN_USE if the RDR is locked. This is informational - the RDR
      has been deregistered and cannot be found again. It will be discarded only
      when there are no remaining locks on it. It is not safe to dispose of the
      RDR contents if this error is returned, see below.

    All parameters MUST match those specified during registration for the RDR
    to be deregistered.

    Note that this prevents (or at least changes) discovery of a particular
    RDR - it does not withdraw access rights from Consumers that have already
    found the RDR - that must be achieved by some additional protocol such as
    an Event or Timeline.

    If the RDR has been locked by SwLockNextRDR(), it is deregistered but
    SW_RDR_ERROR_IN_USE is returned. This call can be repeated and will continue to
    return SW_RDR_ERROR_IN_USE for as long as the RDR is locked. Once unlocked it
    will automatically be discarded and this call will return SW_RDR_ERROR_UNKNOWN.
    See SwLockNextRDR() for more details of RDR locking.

    Important: If the RDR refers to something that is to be discarded after
    deregistration it must NOT be discarded until SwDeregisterRDR() returns
    something other than SW_RDR_ERROR_IN_USE.
*/

sw_rdr_result HQNCALL SwDeregisterRDR(sw_rdr_class rdrclass,
                                      sw_rdr_type rdrtype,
                                      sw_rdr_id rdrid,
                             /*@in@*/ void * ptr,
                                      size_t length) ;

/* -------------------------------------------------------------------------- */
/** \brief  Find an RDR given the Class, Type and ID.

    This call allows the consumer to find a specific RDR.

    \param[in] rdrclass  The Class of the RDR.

    \param[in] rdrtype   The Type of the RDR.

    \param[in] rdrid     The ID of the RDR.

    \param[out] pptr     A pointer to a pointer.

      If not null, this will be filled in with the pointer if the RDR is found.

    \param[out] plength  A pointer to a length.

      If not null, this will be filled in with the length if the RDR is found.

    \return              SW_RDR_SUCCESS if the RDR was found.

      SW_RDR_ERROR_UNKNOWN is returned if no RDR of the specified Class, Type
      and ID was found.

    eg:
    \code
      result = SwFindRDR(myclass, mytype, myid, &ptr, &len);
      if (result == SW_RDR_SUCCESS) {
        ...
      }
    \endcode
*/

sw_rdr_result HQNCALL SwFindRDR(sw_rdr_class rdrclass,
                                sw_rdr_type rdrtype,
                                sw_rdr_id rdrid,
                      /*@out@*/ void ** pptr,
                      /*@out@*/ size_t * plength) ;

/* -------------------------------------------------------------------------- */
/** \brief  Find all RDRs of a specified Class and Type.

    This call begins an iteration to find all RDRs of a specific type.

    \param[in] rdrclass  The Class of the RDR.

    \param[in] rdrtype   The Type of the RDR.

    \return              A pointer to a sw_rdr_iterator or null.

      If null, there was insufficient memory to create the iterator.

      Otherwise, this is passed (multiple times) to SwNextRDR(), and ultimately
      must be destroyed by calling SwFoundRDR().

      It is permissible to create the iterator long in advance of calling
      SwNextRDR() to avoid the possibility of failing due to insufficient
      memory. Iterators can be restarted using SwRestartFindRDR() and hence
      multiple searches can be performed using the same iterator without having
      to call SwFoundRDR() and SwFindRDRbyType() again.

    eg:
    \code
      iterator = SwFindRDRbyType(myclass, mytype);
      if (iterator) {
        while (SwNextRDR(iterator, 0, 0, &id, &ptr, &len)) {
          ...
        }
        SwFoundRDR(iterator);
      }
    \endcode
*/

sw_rdr_iterator * HQNCALL SwFindRDRbyType(sw_rdr_class rdrclass,
                                          sw_rdr_type rdrtype) ;

/* -------------------------------------------------------------------------- */
/** \brief  Find all RDRs of a specified Class.

    This call begins an iteration to find all RDRs of a specific class.

    \param[in] rdrclass  The Class of the RDR.

    \return              A pointer to a sw_rdr_iterator or null.

      If null, there was insufficient memory to create the iterator.

      Otherwise, this is passed (multiple times) to SwNextRDR(), and ultimately
      must be destroyed by calling SwFoundRDR().

      As above, it is permissible to create the iterator in advance.

    eg:
    \code
      iterator = SwFindRDRbyClass(myclass);
      if (iterator) {
        while (SwNextRDR(iterator, 0, &type, &id, &ptr, &len)) {
          ...
        }
        SwFoundRDR(iterator);
      }
    \endcode
*/

sw_rdr_iterator * HQNCALL SwFindRDRbyClass(sw_rdr_class rdrclass) ;

/* -------------------------------------------------------------------------- */
/** \brief  Find all RDRs.

    This call begins an iteration to find all RDRs.

    \return              A pointer to a sw_rdr_iterator or null.

      If null, there was insufficient memory to create the iterator.

      Otherwise, this is passed (multiple times) to SwNextRDR(), and ultimately
      must be destroyed by calling SwFoundRDR().

      As above, it is permissible to create the iterator in advance.

    eg:
    \code
      iterator = SwFindRDRs();
      if (iterator) {
        while (SwNextRDR(iterator, &class, &type, &id, &ptr, &len)) {
          ...
        }
        SwFoundRDR(iterator);
      }
    \endcode
*/

sw_rdr_iterator * HQNCALL SwFindRDRs(void) ;

/* -------------------------------------------------------------------------- */
/** \brief  Find an RDR by iteration.

    This call returns an RDR from the given iterator.

    \param[in] iterator  The sw_rdr_iterator created by SwFindRDR*().

    \param[out] pclass   Optional pointer to an RDR class.

      If not null, this will be filled in with the class of the found RDR.

    \param[out] ptype    Optional pointer to an RDR type.

      If not null, this will be filled in with the type of the found RDR.

    \param[out] pid      Optional pointer to an RDR id.

      If not null, this will be filled in with the id of the found RDR.

    \param[out] pptr     Pointer to a pointer.

      If not null, this will be filled in with the ptr of the found RDR.

    \param[out] plength  Pointer to a length.

      If not null, this will be filled in with the length of the found RDR.

    \return              Returns SW_RDR_SUCCESS if an RDR was found,
                         SW_RDR_ERROR_UNKNOWN if no further RDRs match
                         or SW_RDR_ERROR_SYNTAX if the iterator is invalid.

    Once the iteration is finished, the iterator must be destroyed by calling
    SwFoundRDR(), or reset using SwRestartFindRDR() if the iterator is to
    persist or be reused immediately.

    Note that in a multithreaded system it is possible for an RDR to be
    deregistered after this call has returned but before the caller tries
    to use it. See SwLockNextRDR() below.
*/

sw_rdr_result HQNCALL SwNextRDR(
        /*@out@*/ /*@notnull@*/ sw_rdr_iterator * iterator,
                      /*@out@*/ sw_rdr_class * pclass,
                      /*@out@*/ sw_rdr_type * ptype,
                      /*@out@*/ sw_rdr_id * pid,
                      /*@out@*/ void ** pptr,
                      /*@out@*/ size_t * plength) ;

/* -------------------------------------------------------------------------- */
/** \brief  Find an RDR by iteration and lock it to prevent deregistration.

    All parameters and behavour as SwNextRDR() above, except the returned RDR
    is locked and can not be deregistered. It is unlocked automatically when
    the iterator is next passed to SwNextRDR(), SwLockNextRDR(), SwFoundRDR()
    or SwRestartFindRDR().

    If SwDeregisterRDR() is called on a locked RDR it is deregistered at once
    to prevent further discovery but is only discarded once fully unlocked.
    Note that multiple seperate find operations could have locked the RDR many
    times, and all must unlock before the RDR is finally discarded.

    Important: If the RDR refers to something that is to be discarded after
    deregistration it must NOT be discarded until SwDeregisterRDR() returned
    something other than SW_RDR_ERROR_IN_USE.
*/

sw_rdr_result HQNCALL SwLockNextRDR(
            /*@out@*/ /*@notnull@*/ sw_rdr_iterator * iterator,
                          /*@out@*/ sw_rdr_class * pclass,
                          /*@out@*/ sw_rdr_type * ptype,
                          /*@out@*/ sw_rdr_id * pid,
                          /*@out@*/ void ** pptr,
                          /*@out@*/ size_t * plength) ;

/* -------------------------------------------------------------------------- */
/** \brief  Restart an RDR iterator.

    This call restarts the find process by returning the iterator to its
    initial state. It can be called instead of SwFoundRDR() to reuse a
    persistent iterator. Note that ultimately it is still necessary to call
    SwFoundRDR() to destroy the iterator. It can be called before SwNextRDR()
    has exhausted its results.

    \param[in] iterator  The iterator to restart.

    \return              SW_RDR_SUCCESS, normally; SW_RDR_ERROR_IN_USE if the
                         iterator cannot be restarted because it is threaded;
                         SW_RDR_ERROR_SYNTAX if the iterator is not recognised.

    This automatically unlocks the RDR returned by a call to SwLockNextRDR().
*/

sw_rdr_result HQNCALL SwRestartFindRDR(/*@in@*/ sw_rdr_iterator * iterator) ;

/* -------------------------------------------------------------------------- */
/** \brief  Destroy an RDR iterator.

    This call ends an iteration to find RDRs.

    \param[in] iterator  The iterator to destroy.

      Passing in a null iterator is pointless but safe.

    \return              SW_RDR_SUCCESS, normally; SW_RDR_ERROR_IN_USE if the
                         iterator cannot be destroyed because it is threaded;
                         SW_RDR_ERROR_SYNTAX if the iterator is not recognised.

    This automatically unlocks the RDR returned by a call to SwLockNextRDR().
*/

sw_rdr_result HQNCALL SwFoundRDR(/*@in@*/ sw_rdr_iterator * iterator) ;

/* -------------------------------------------------------------------------- */

typedef struct {
  HqBool           valid ;
  sw_rdr_result    (HQNCALL *register_rdr)(sw_rdr_class, sw_rdr_type, sw_rdr_id,
                                           void*, size_t, sw_rdr_priority);
  sw_rdr_result    (HQNCALL *register_id) (sw_rdr_class, sw_rdr_type, sw_rdr_id*,
                                           void*, size_t, sw_rdr_priority);
  sw_rdr_result    (HQNCALL *deregister)  (sw_rdr_class, sw_rdr_type, sw_rdr_id,
                                           void*, size_t);
  sw_rdr_result    (HQNCALL *find)        (sw_rdr_class, sw_rdr_type, sw_rdr_id,
                                           void**, size_t*);
  sw_rdr_iterator* (HQNCALL *find_type)   (sw_rdr_class, sw_rdr_type);
  sw_rdr_iterator* (HQNCALL *find_class)  (sw_rdr_class);
  sw_rdr_iterator* (HQNCALL *find_all)    (void);
  sw_rdr_result    (HQNCALL *next)        (sw_rdr_iterator*, sw_rdr_class*,
                                           sw_rdr_type*, sw_rdr_id*, void**,
                                           size_t*);
  sw_rdr_result    (HQNCALL *found)       (sw_rdr_iterator*);
  sw_rdr_result    (HQNCALL *restart)     (sw_rdr_iterator*);
  sw_rdr_result    (HQNCALL *lock_next)   (sw_rdr_iterator*, sw_rdr_class*,
                                           sw_rdr_type*, sw_rdr_id*, void**,
                                           size_t*);
} sw_rdr_api_20110201 ;

/* -------------------------------------------------------------------------- */
/* The symmetrical model: Indirection through an RDR-supplied API pointer */

/* Skin and Core both have their own version of this API pointer. The Skin's
   points to the API structure at build time, the Core's is found via RDR at
   run time during DLL initialisation (for example).
*/
extern sw_rdr_api_20110201 * rdr_api ;

/* -------------------------------------------------------------------------- */
/* Function name spoofing.

   For simplicity and symmetry, these function calls are actually indirected
   through an API structure pointed to by the above API pointer. This is hidden
   from the programmer by this series of macros.

   When the API implementor includes this file and wants access to the real
   function addresses it defines RDR_IMPLEMENTOR.
*/
#if !defined(RDR_IMPLEMENTOR)

#define SwRegisterRDR      rdr_api->register_rdr
#define SwRegisterRDRandID rdr_api->register_id
#define SwDeregisterRDR    rdr_api->deregister
#define SwFindRDR          rdr_api->find
#define SwFindRDRbyType    rdr_api->find_type
#define SwFindRDRbyClass   rdr_api->find_class
#define SwFindRDRs         rdr_api->find_all
#define SwNextRDR          rdr_api->next
#define SwRestartFindRDR   rdr_api->restart
#define SwFoundRDR         rdr_api->found
#define SwLockNextRDR      rdr_api->lock_next

#endif

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */

#endif /* __RDRAPI_H__ */
