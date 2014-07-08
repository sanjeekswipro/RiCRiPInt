/** \file
 * \ingroup EVENTS
 *
 * $HopeName: SWrdrapi!eventapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This header file provides the definition of the Events system, built
 * on the RDR system.
 *
 * Event numbers are split into ranges, defined by evtypes.h, which are
 * subdivided elsewhere.
 */

#ifndef __EVENTAPI_H__
#define __EVENTAPI_H__

/** \defgroup swevents Events system
    \ingroup interface
    \{ */

#include "rdrapi.h"
#include "hqncall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Initialisation and finalisation. */

int HQNCALL event_start(void) ;
void HQNCALL event_end(void) ;

/* -------------------------------------------------------------------------- */
/** \page eventapi  The Event System

    This enables Events or Messages to be sent and received by multiple
    recipients.

    Handlers are defined like this:

    \code
    sw_event_result my_callback(void * context, sw_event * event) ;

    sw_event_handler handler = {my_callback, my_context} ;
    SwRegisterHandler(EVENT_<type>, handler, SW_RDR_NORMAL) ;
    \endcode

    Events are issued like this:

    \code
    my_event_message message = {<fill stuff in here>} ;
    result = SwEvent(EVENT_<type>, message, sizeof(message)) ;
    \endcode

    Or for purely informational signals:

    \code
    result = SwEvent(EVENT_<type>, 0, 0) ;
    \endcode
*/

/* -------------------------------------------------------------------------- */
/** \brief Event types
 *
 * Event numbering follows the numbering conventions for DEVICETYPE numbers.
 *
 * The range 0x00000000-0x0000ffff is reserved for Global Graphics.
 * The range 0xXXXX0000-0xXXXXffff is reserved for customer number XXXX.
 * The range 0xffff0000-0xffffffff is for private use in closed environments.
 *
 * These ranges are sub-allocated here and elsewhere, eg swevents.h.
 */
enum {
  EVENT_EVENT =      0, /* 100 Event system events */
  EVENT_RDR =      100, /* 100 RDR system events */
  EVENT_PCL =      200, /* 100 PCL system events */
  EVENT_TIMELINE = 300, /* 100 Timeline system events */
  EVENT_SKIN =  0x4000, /* 8192 Skin events, defined by the skin. */
  EVENT_PLUGAPI=0x6000, /* 8192 Plugin API events, defined in pic/pim/piu/pip. */
  EVENT_CORE =  0x8000  /* 32768 Core events, defined by swevents.h */
} ;

typedef sw_rdr_type sw_event_type ;

/* -------------------------------------------------------------------------- */
/** \brief Event Handler Priorities
 *
 * The highest priority (or most recently registered) Handler for an Event is
 * called first. If it returns SW_EVENT_CONTINUE the next highest will be
 * called, and so on.
 *
 * These are typical values, but other than the high to low ordering, Handlers
 * can choose whatever priority is appropriate. Handlers can be reprioritised
 * dynamically, even while being executed.
 */

enum {
  SW_EVENT_DEFAULT  = -10000,
  SW_EVENT_NORMAL   =      0,
  SW_EVENT_OVERRIDE =  10000
} ;

typedef sw_rdr_priority sw_event_priority ;

/* -------------------------------------------------------------------------- */

typedef sw_rdr_id sw_event_id ;

/* -------------------------------------------------------------------------- */
/** \brief Result values from Event Handlers

   Handlers for an Event are called in decreasing Priority order until one does
   NOT return SW_EVENT_CONTINUE. The last return code is returned to the Event
   Issuer. SW_EVENT_UNHANDLED is an alias for SW_EVENT_CONTINUE which may be
   clearer in some protocols.

   Other than the special behaviour of SW_EVENT_FORCE_UNHANDLED and
   SW_EVENT_CONTINUE, all other values are returned to the Issuer, so their
   meaning is defined on a per-Event basis. It may be that some Event uses
   return values >= SW_EVENT_ERROR to indicate various successful outcomes.

   These are chosen such that SW_EVENT_HANDLED == SW_RDR_SUCCESS and
   SW_EVENT_ERROR == SW_RDR_ERROR for simplicity of implementation.
 */
enum {
  SW_EVENT_CONTINUE = -2,   /**< Pass Event on to next Handler */
  SW_EVENT_FORCE_UNHANDLED = -1, /**< Immediately return SW_EVENT_UNHANDLED to Issuer */
  SW_EVENT_HANDLED = 0,     /**< Return immediately to Issuer. */
  SW_EVENT_ERROR = 1,       /**< Error, don't pass to any more Handlers.
                                 Specific errors are enumerated from here for
                                 individual Events, all >= SW_EVENT_ERROR */

  SW_EVENT_UNHANDLED = SW_EVENT_CONTINUE /* Alias, for Issuer code clarity */
} ;

typedef sw_rdr_result sw_event_result ;

/* -------------------------------------------------------------------------- */
/** \brief Opaque event context for internal use only

   This is only seen inside the sw_event structure passed to Handlers, and may
   not be usefully inspected or modified. However, it is necessary for the
   correct functioning of SwEventTail().
 */

typedef struct sw_event_context sw_event_context ;

/* -------------------------------------------------------------------------- */
/** \brief Event structure

   This structure is delivered to Event Handlers and contains the type of Event,
   useful in the unusual case of a single Handler being registered for multiple
   different Events, the message pointer and length as supplied to SwEvent(),
   and the opaque Event context used for SwEventTail() calls.

   \param type     The type of the current Event. One Handler function may be
                   registered for multiple Events - this field allows the
                   type of Event to be detected, which could affect how the
                   message is interpreted. Changing this value does NOT change
                   which remaining Handlers are to be called, so it must not be
                   changed by the current Handler.

   \param context  An opaque value that must be preserved for SwEventTail() to
                   function correctly.

   \param message  The pointer passed by the Event Issuer. Its interpretation
                   is defined by the Event in question, but will typically be
                   a pointer to a structure of some kind. Some Events have no
                   message so this pointer may legitimately be NULL.

                   It may be useful during some Events for a Handler to modify
                   this value.

   \param length   The size passed by the Event Issuer. Its interpretation
                   is defined by the Event in question, but will often be the
                   length of the above structure. Alternatively it may be the
                   number of entries in an array, or some other scalar value.

                   It may be useful during some Events for a Handler to modify
                   this value.

   \param id       Each new Event is given a unique identifier. This allows a
                   Handler which has been registered more than once for the same
                   Event to tell the difference between being called again for
                   the same Event at a different priority, and a new Event. The
                   id eventually wraps around so should not be kept for extended
                   periods.

   Handlers that wish to call SwEventTail() with differing message contents must
   modify the original sw_event they are passed, and restore the original
   contents (if appropriate) before passing on (if it does SW_EVENT_CONTINUE).
 */

typedef struct {
  sw_event_type      type ;    /**< EVENT_<type>.            Do not change */
  sw_event_context * context ; /**< Not for Handler's use.   Do not change */
  void *             message ; /**< Issuer's message ptr.    Can be changed */
  size_t             length ;  /**< Issuer's message length. Can be changed */
  HqnIdent           id ;      /**< Unique event identifier. Do not change */
} sw_event ;

/* -------------------------------------------------------------------------- */
/** \brief The Event Handler prototype

    \param[in] context  The Handler's context pointer as specified in the
                        registered sw_event_handler. This can be whatever the
                        Handler requires. Note that this is a void* and must
                        be cast to whatever type the Handler expects.

    \param[in] event    The Event details in sw_event format. Note that the
                        Issuer-supplied event->message is a void* and must be
                        cast to whatever type the Event in question is defined
                        to comprise.

    \return             SW_EVENT_CONTINUE to pass on to other Handlers. This
                        does not necessarily mean that this Handler has done
                        nothing. Whether to 'normally' return SW_EVENT_CONTINUE
                        or SW_EVENT_HANDLED is defined on a per-Event basis.

                        SW_EVENT_HANDLED to return immediately to the Issuer.
                        No further Handlers are called.

                        >= SW_EVENT_ERROR to return immediately to the Issuer.
                        No further Handlers are called. Error codes may be
                        defined on a per-Event basis.

                        SW_EVENT_FORCE_UNHANDLED to return immediately to the
                        Issuer. No further Handlers are called, but the value
                        returned to the Issuer is SW_EVENT_CONTINUE (also known
                        as SW_EVENT_UNHANDLED).

    If all Handlers return SW_EVENT_CONTINUE, this is the value returned to the
    Issuer. For code clarity SW_EVENT_UNHANDLED is an alias for this value,
    which can make Issuer code clearer. Also see SW_EVENT_FORCE_UNHANDLED.
*/
typedef sw_event_result (HQNCALL *event_handler)(void * context,
                                                 sw_event * event) ;

/* -------------------------------------------------------------------------- */
/** \brief Event Handler structure

    This small structure is used to register a Handler. The structure MUST
    persist for the lifetime of the Handler's registration. A local structure
    must NOT be used!

    The same sw_event_handler can be registered for multiple Events at the same
    time, but only once per Event - reregistration for the same Event will be
    treated as a reprioritisation of the previous registration. Note however
    that multiple sw_event_handler structures containing the same Handler and
    context pointers CAN be registered on the same Event.

    In the current implementation, this is registered with the RDR system with
    a Class of RDR_CLASS_EVENT, Type of the Event Type and appropriate ID, but
    this must not be relied upon.

    It is possible to change the contents of this structure while the Handler
    is still registered. Note that extreme care must be taken when doing so.
  */
typedef struct {
  event_handler handler ;  /**< Address of callback function. */
  void *        context ;  /**< Handler's context - can be anything. */
  HqnIdent      reserved ; /**< Reserved for future use - must be set to 0 by
                              auto/static initialisers, must not be read or
                              used by clients. Do not even use the name of
                              this field, it may change in future. */
} sw_event_handler ;

/** \brief Static/auto initialiser for sw_event_handler. */
#define SW_EVENT_HANDLER_INIT { NULL, NULL, 0 }

/** \brief Atomic multiple Handler registration

    This subclass of the above structure is used by SwRegisterHandlers() and
    SwDeregisterHandlers() only, and is always used in arrays.
  */
typedef struct {
  event_handler     handler ;  /**< Address of callback function. */
  void *            context ;  /**< Handler's context - can be anything. */
  HqnIdent          reserved ; /**< Reserved for future use - must be set to
                                  0 by auto/static initialisers, must not be
                                  read or used by clients. Do not even use
                                  the name of this field, it may change in
                                  future. */
  sw_event_type     type ;     /**< The Event type to register for. */
  sw_event_priority priority ; /**< The priority with which to register. */
} sw_event_handlers ;

/** \brief Static/auto initialiser for sw_event_handlers. */
#define SW_EVENT_HANDLERS_INIT { NULL, NULL, 0, EVENT_EVENT, SW_EVENT_NORMAL }

/* -------------------------------------------------------------------------- */
/** \brief  Register an Event Handler.

    This registers a Handler with the Event system, or reregisters a previously
    registered handler with a different priority. This can be done by the
    Handler itself while being executed, if it wishes.

    Note that SwRegisterHandler is just syntactic sugar for an RDR registration.

    \param[in] type     The Event type, of the form EVENT_<type>

    \param[in] handler  A sw_event_handler structure containing a pointer to
                        the Event Handler function, and optionally its private
                        context.

    \param[in] priority A priority, as for RDR registrations.

    \return             SW_RDR_SUCCESS, if successfully registered.

                        SW_RDR_ERROR in the unlikely event of too many Handlers
                        being registered.
*/
sw_rdr_result HQNCALL SwRegisterHandler(sw_event_type      type,
                          /*@notnull@*/ sw_event_handler * handler,
                                        sw_event_priority  priority) ;

/* -------------------------------------------------------------------------- */
/** \brief  Register multiple Event Handlers atomically.

    This registers a number of Handlers in one call, and does so atomically -
    if any fail to register, any that have will be deregistered.

    This is more concise than multiple calls to SwRegisterHandler(), but is
    entirely equivalent.

    \param[in] handlers An array of sw_event_handlers containing a pointer to
                        the Event Handler function, its private context, the
                        Event type and the priority.

    \param[in] count    The number of entries in the above array.

    \return             SW_RDR_SUCCESS, if successfully registered.

                        SW_RDR_ERROR in the unlikely event of too many Handlers
                        being registered.

    Should this call return an error it is guaranteed that no Handlers are
    registered or threaded. However, it cannot guarantee that none were called.
*/
sw_rdr_result HQNCALL SwRegisterHandlers(
                           /*@notnull@*/ sw_event_handlers * handlers,
                                         int                 count) ;

/* -------------------------------------------------------------------------- */
/** \brief  Deregister an Event Handler registered with SwRegisterHandler.

    Deregisters a previously registered Handler. This can be called by the
    Handler itself.

    Although this guarantees that the Handler will not be called again, it
    does NOT guarantee that the Handler is not currently threaded.

    \param[in] type     The Event type, of the form EVENT_<type>.

    \param[in] handler  The sw_event_handler structure previously registered.

    \return             SW_RDR_SUCCESS, if successfully deregistered.
                        SW_RDR_ERROR_IN_USE, if the Handler was theaded.
                        SW_RDR_ERROR_UNKNOWN, if the Handler was not registered.

    Note that even if SW_RDR_ERROR_IN_USE is returned, the Handler HAS been
    successfully deregistered, though it may still be threaded. If so, this
    call can be repeated until it no longer returns SW_RDR_ERROR_IN_USE, or
    SwSafeDeregisterHandler() called instead to wait for the Handler to exit.
*/
sw_rdr_result HQNCALL SwDeregisterHandler(sw_event_type      type,
                            /*@notnull@*/ sw_event_handler * handler) ;

/* -------------------------------------------------------------------------- */
/** \brief  Deregister multiple Event Handlers.

    This deregisters a number of Handlers in one call, regardless of errors,
    and guarantess that they are not threaded on exit.

    This is more concise than multiple calls to SwSafeDeregisterHandler(), but
    is entirely equivalent. If any Handler is not registered an error is
    returned after all other Handlers have been deregistered.

    \param[in] handlers An array of sw_event_handlers as previously passed to
                        SwRegisterHandlers().

    \param[in] count    The number of entries in this array.

    \return             SW_RDR_SUCCESS, if all successfully deregistered.

                        SW_RDR_ERROR_UNKNOWN if any were not registered. Note
                        that ALL Handlers will have been deregistered, this
                        error is purely informational.

    Note that this call cannot be used by a Handler to deregister itself. Be
    careful not to use this call in any code that can be called from a Handler
    that is to be deregistered.
*/
sw_rdr_result HQNCALL SwDeregisterHandlers(
                             /*@notnull@*/ sw_event_handlers * handlers,
                                           int                 count) ;

/* -------------------------------------------------------------------------- */
/** \brief  Deregister an Event Handler detecting Handler threading

    This deregisters a Handler immediately like SwDeregisterHandler() but also
    detects whether the Handler is currently threaded. If so it does not return
    until the Handler exits.

    \param[in] type     The Event type, of the form EVENT_<type>.

    \param[in] handler  The sw_event_handler structure previously registered.

    \return             SW_RDR_SUCCESS, if successfully deregistered.
                        SW_RDR_ERROR_UNKNOWN, if the Handler is not registered.

    Note that this call cannot be used by a Handler to deregister itself. Be
    careful not to use this call in any code that can be called from the Handler
    that is to be deregistered.
*/
sw_rdr_result HQNCALL SwSafeDeregisterHandler(sw_event_type      type,
                                /*@notnull@*/ sw_event_handler * handler) ;

/* -------------------------------------------------------------------------- */
/** \brief  Generate an event, calling relevant handlers in priority order.

   The meaning, parameters and return values pertinent to a particular Event
   are defined by the Event owner. Some Events may merely be used as a signal
   to inform interested parties of a change in state. Some may contain data
   or a reference to a buffer. Others may require specific return codes for
   successful completion. This is all up to the Event owner.

   \param[in] type     The event type of the form EVENT_<type>.

   \param[in] message  The message pointer for this event. Note that much like
                       the RDR system which underpins the Event system, no
                       interpretation of this pointer is mandated - it may be
                       a pointer to some information, or to a buffer to fill in,
                       or some other arbitrary address such as a memory limit.
                       It may often be NULL.

   \param[in] length   The length associated with the above message pointer.
                       Once again, no interpretation is placed on the meaning
                       of this value - it may refer to the length of the message
                       or may be an arbitrary number. This is defined by the
                       issuer of the Event in question. It may often be zero.

   \return             If there are no handlers, or none respond, the return
                       value will be SW_EVENT_CONTINUE. SW_EVENT_UNHANDLED is
                       an alias of this value that may make code clearer.

                       If a handler has acted appropriately, it will return
                       SW_EVENT_HANDLED. Note that either of these cases may
                       be seen as a success or failure by the Event Issuer -
                       this is defined on a per-Event basis.

                       If a handler reports an error, the return value will be
                       >= SW_EVENT_ERROR. Error return values in that range can
                       be defined on a per-Event basis, and may not actually
                       indicate a fault for that Event definition!
*/
sw_event_result HQNCALL SwEvent(sw_event_type type,
                                void *        message,
                                size_t        length) ;

/* -------------------------------------------------------------------------- */
/** \brief  Call the remaining Handlers for the current Event, returning
            the value that would have been returned to the Issuer.

   This call can only be made by a Handler, passing in the sw_event delivered
   to it. The remaining Handlers - the "tail" - can be called multiple times
   if the current Handler wishes. How the return values are combined or what
   value is ultimately returned is up to the current Handler.

   Note that calling SwEventTail() does not automatically prevent the remaining
   Handlers from being called once more if the current Handler returns
   SW_EVENT_CONTINUE - see SW_EVENT_FORCE_UNHANDLED.

   \param[in] event  The sw_event structure passed to the Handler.

                     It is permissible for the Handler to change the message
                     and length fields of the sw_event, but the opaque context
                     field must be preserved for SwEventTail() to function.

   \return           As SwEvent(), SW_EVENT_CONTINUE (SW_EVENT_UNHANDLED),
                     SW_EVENT_HANDLED, or an error code >= SW_EVENT_ERROR may
                     be returned by the remaining Handlers, if any.

                     The current Handler may choose to return some other value
                     if it wishes.
*/
sw_event_result HQNCALL SwEventTail(sw_event * event) ;

/* ========================================================================== */
/* Versions of the Event System APIs, published as RDRs */

typedef struct {
  HqBool          valid ;
  sw_rdr_result   (HQNCALL *register_handler)(sw_event_type, sw_event_handler*,
                                              sw_event_priority);
  sw_rdr_result   (HQNCALL *deregister)      (sw_event_type, sw_event_handler*);
  sw_event_result (HQNCALL *event)           (sw_event_type, void*, size_t);
  sw_event_result (HQNCALL *event_tail)      (sw_event*);
  sw_rdr_result   (HQNCALL *register_handlers)(sw_event_handlers*,int) ;
  sw_rdr_result   (HQNCALL *deregister_handlers)(sw_event_handlers*,int) ;
  sw_rdr_result   (HQNCALL *safe_deregister) (sw_event_type, sw_event_handler*);
} sw_event_api_20110330 ;

/* -------------------------------------------------------------------------- */
/* The symmetrical model: Indirection through an RDR-supplied API pointer */

/* If the Core is built as a DLL, the Skin and Core both have their own version
   of this API pointer. This is the Skin's ptr and points to the API structure
   at build time, the Core's is in skinapis.c and filled in via RDR at run time
   during DLL initialisation.
*/
extern sw_event_api_20110330 * event_api ;

/* -------------------------------------------------------------------------- */
/* Function name spoofing.

   For simplicity and symmetry, these function calls are actually indirected
   through an API structure pointed to by the above API pointer. This is hidden
   from the programmer by this series of macros.
*/
#if !defined(EVENT_IMPLEMENTOR)

#define SwRegisterHandler       event_api->register_handler
#define SwDeregisterHandler     event_api->deregister
#define SwEvent                 event_api->event
#define SwEventTail             event_api->event_tail
#define SwRegisterHandlers      event_api->register_handlers
#define SwDeregisterHandlers    event_api->deregister_handlers
#define SwSafeDeregisterHandler event_api->safe_deregister

#endif

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */

#endif /* __EVENTAPI_H__ */
