/** \file
 * \ingroup timeline
 *
 * $HopeName: SWtimelineapi!timelineapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This header file provides the definition of the Timeline API.
 *
 * The Timeline API is used to manage, communicate and negotiate the lifespan of
 * entities; arrange timelines into hierarchies; report state, progress along and
 * extent of timelines; and attribute textual feedback and error messages to
 * timeline hierarchies.
 *
 * It makes extensive use of the Event system and is independent of the core.
 */

/* -------------------------------------------------------------------------- */

#ifndef __TIMELINEAPI_H__
#define __TIMELINEAPI_H__

/** \defgroup timeline Timeline API
    \ingroup interface
    \{ */

#include <float.h>    /* DBL_MAX */
#include <limits.h>   /* INT_MIN */
#include "eventapi.h" /* includes rdrapi.h */

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Timeline system initialisation and finalisation */


HqBool HQNCALL timeline_start(void) ;

void HQNCALL timeline_end(void) ;

/* -------------------------------------------------------------------------- */
/** \page timelineapi  The Timeline API
 *
 * The Timeline API allows entities with a finite lifespan to be uniquely
 * identified, with a type and human-readable title. The resulting reference
 * can then be used to identify the entity, and detect when it no longer exists.
 *
 * Additionally, the lifespan of such entities can be managed, communicated to
 * interested parties and negotiated. They can be linked to related superior
 * or subordinate entities. Related information can be attached to a Timeline
 * and retrieved later, even after the Timeline has ended.
 *
 * Parties can communicate extent of, progress through and unit of measure of a
 * Timeline, and send human-readable feedback, status messages, error reports or
 * detailed logging related to a specific Timeline.
 *
 * \section timelinerefs  Timeline References
 *
 * Once started, a Timeline is referred to by a Timeline Reference. This is an
 * opaque identifier which is guaranteed unique for the lifetime of the Timeline
 * and for a significant time afterwards. Obsolete references will eventually be
 * reused, but as the reference is an integer of at least 32bits, this should
 * take some time.
 *
 * \section timelinetypes  Timeline Types
 *
 * Timeline types are enumerated in this file. Since a Timeline can represent
 * a wide variety of entities - a print job, an ongoing process, cached data,
 * removeable peripheral, physical sensor - the range of types is subdivided
 * into subsets for customer use. A Timeline is not necessarily something that
 * is communicated from the Core to the Skin, nor vice versa.
 *
 * As any system is free to attach child Timelines to any other Timeline, no
 * assumption should be made of the topography of a Timeline hierarchy. Calls
 * are provided to find the type and title of a Timeline, and its parent of a
 * particular type, if any. This allows clients to discover if Timeline Events
 * relate to the Timeline it is most interested in, even if only distantly
 * related.
 *
 * Some uses of Timelines:
 *
 * * Skin's concept of a "print job"
 * * Core's concept of a "print job", which may not be the same thing
 * * Font cartridge, so its removal can be negotiated
 * * Fuser unit, so failure and current temperature can be reported
 * * Print head and ink cartridges as a hierarchy for status monitoring
 *
 * \section timelineevents  Timeline Events
 *
 * All significant Timeline state changes issue Events. This allows interested
 * parties to be informed immediately of the change, and in many cases allows
 * such Handlers to modify or prevent the change from happening. For example,
 * the interpreter can communicate the job title as it is discovered. The Core
 * can object to a Font Cartridge being ejected if it is currently in use. A
 * debugging extension can log all messages and errors emitted by a subsystem.
 *
 * \section timelinenotes  Timeline Notes
 *
 * The Timeline system is multi-thread and multi-client system. Timelines can
 * therefore appear and disappear at unpredictable times. In particular, this
 * means a Timeline can disappear after a call that returns some state from that
 * Timeline, but before the caller uses that state. Care must be taken that no
 * assumption is made of Timeline continuance where such Timelines could end
 * unexpectedly.
 */

/* -------------------------------------------------------------------------- */

typedef HqnIdent sw_tl_ref ;  /* unique identifier for a Timeline */

/* Zero is never used as a Timeline reference (they are also always +ve) */
enum {
  SW_TL_REF_INVALID = 0
} ;

/* -------------------------------------------------------------------------- */

typedef int sw_tl_state ;  /* Timeline Event subdivision */

/** \brief Timeline states.

    The existing state numbers will not change between releases.
*/
enum {
  /* transient states are used in events but do not persist */
  /* internal states are never seen in events */
  TL_STATE_START = 0,  /* Timeline has started */
  TL_STATE_TITLE,      /* Timeline title is changing (transient) */
  TL_STATE_EXTEND,     /* Timeline has extended (transient) */
  TL_STATE_PROGRESS,   /* The Timeline is now at this point (transient) */
  /* insert new live states here */
  TL_STATE_END = 32,   /* Pending, will end at first opportunity (internal) */
  TL_STATE_ABORT,      /* Pending, will abort at first opportunity (internal) */
  TL_STATE_ENDING,     /* Timeline is ending normally - may prolong */
  TL_STATE_ABORTING,   /* Timeline is aborting with an error - may prolong */
  TL_STATE_ENDED,      /* Timeline is now over (transient) */
  TL_STATE_ABORTED,    /* Timeline has now aborted (transient) */
  /* insert new dead states here */
  TL_STATE_UNKNOWN = 64 /* Timeline is unknown */
} ;

/** \brief Timeline event numbers. */
enum {
  EVENT_TIMELINE_START    = EVENT_TIMELINE+TL_STATE_START,
  EVENT_TIMELINE_TITLE    = EVENT_TIMELINE+TL_STATE_TITLE,
  EVENT_TIMELINE_EXTEND   = EVENT_TIMELINE+TL_STATE_EXTEND,
  EVENT_TIMELINE_PROGRESS = EVENT_TIMELINE+TL_STATE_PROGRESS,
  EVENT_TIMELINE_ENDING   = EVENT_TIMELINE+TL_STATE_ENDING,
  EVENT_TIMELINE_ENDED    = EVENT_TIMELINE+TL_STATE_ENDED,
  EVENT_TIMELINE_ABORTING = EVENT_TIMELINE+TL_STATE_ABORTING,
  EVENT_TIMELINE_ABORTED  = EVENT_TIMELINE+TL_STATE_ABORTED
} ;

/** \brief Handler considerations for Timeline Events
 *
 * The Timeline system issues many Events. Authors must be careful to attach
 * their Handlers to the appropriate Events and return the correct return codes.
 *
 * EVENT_TIMELINE_START    The Timeline has started. The Timeline reference is
 *                         now valid, and children can be attached. Handlers
 *                         should return SW_EVENT_CONTINUE. However, if a
 *                         Handler returns SW_EVENT_HANDLED the Timeline will be
 *                         ended immediately and the caller of SwTimelineStart()
 *                         will get a SW_TL_REF_INVALID reference returned.
 *
 * EVENT_TIMELINE_TITLE    The Timeline is about to be renamed. Handlers can
 *                         change the title pointer and length in the Event
 *                         message if they wish to modify the title change. They
 *                         can remove the title by setting the pointer or length
 *                         to zero, or retain the existing title by returning
 *                         SW_EVENT_HANDLED. Normally they should return
 *                         SW_EVENT_CONTINUE.
 *
 * EVENT_TIMELINE_EXTEND   The extent of the Timeline has changed. Handlers
 *                         should return SW_EVENT_CONTINUE. Returning anything
 *                         else will merely prevent other Handlers getting the
 *                         Event - the change has already occured.
 *
 * EVENT_TIMELINE_PROGRESS The Timeline progress has changed. Handlers should
 *                         return SW_EVENT_CONTINUE. Returning anything else
 *                         will merely prevent other Handlers getting the Event
 *                         - the change has already occured.
 *
 * EVENT_TIMELINE_ENDING   The Timeline may be about to end - if there are no
 *                         objections. Handlers should return SW_EVENT_CONTINUE
 *                         normally to signal that that they do not object.
 *                         If a Handler returns SW_EVENT_HANDLED however, the
 *                         Timeline will be prolonged. It is then that Handler's
 *                         responsibility to arrange for the Timeline to end.
 *                         Note that multiple ENDING Events may be issued for a
 *                         Timeline before all Handlers agree to the end.
 *
 * EVENT_TIMELINE_ENDED    The Timeline has ended. The Timeline reference will
 *                         only continue to be valid for the duration of the
 *                         Event and will be unknown thereafter. There will only
 *                         be one EVENT_TIMELINE_ENDED or EVENT_TIMELINE_ABORTED
 *                         Event issued for a Timeline.
 *
 * EVENT_TIMELINE_ABORTING The Timeline may be about to abort if there are no
 *                         objections. Handlers should normally return
 *                         SW_EVENT_CONTINUE to allow the abort to occur. If
 *                         a Handler returns SW_EVENT_HANDLED to object to the
 *                         abort, the Timeline will nevertheless stay in an
 *                         about-to-abort state, and further attempts will be
 *                         made to abort at an appropriate time. The Handler
 *                         does not gain ownership of the Timeline as it does
 *                         when objecting to an EVENT_TIMELINE_ENDING event.
 *
 * EVENT_TIMELINE_ABORTED  The Timeline has aborted. The Timeline reference will
 *                         only continue to be valid for the duration of the
 *                         Event and will be unknown thereafter. There will only
 *                         be one EVENT_TIMELINE_ENDED or EVENT_TIMELINE_ABORTED
 *                         Event issued for a Timeline.
 */

/* -------------------------------------------------------------------------- */
/** \brief Timeline types
 *
 * Timeline numbering follows the numbering conventions for DEVICETYPE numbers.
 *
 * The range 0x00000000-0x0000ffff is reserved for Global Graphics.
 * The range 0xXXXX0000-0xXXXXffff is reserved for customer number XXXX.
 * The range 0xffff0000-0xffffffff is for private use in closed environments.
 *
 * These ranges are sub-allocated here and elsewhere.
 */
typedef HqnIdent sw_tl_type ;  /* Timeline types */

/** Enumeration of Timeline types reflecting the partition of type definitions
 * for RIP components.
 */
enum {
  TL_SKIN = 0x4000,      /* 8192 Timeline types for Skin use */
  TL_PLUGAPI = 0x6000,   /* 8192 Timeline types for plugin/skin API */
  TL_CORE = 0x8000,      /* 32768 Timeline types for Core use */

  SW_TL_TYPE_ANY = 0,            /* As passed to SwTimelineGetParent() */
  SW_TL_TYPE_NONE = 0            /* As returned by SwTimelineGetType() */
} ;

/* -------------------------------------------------------------------------- */
/** \brief Timeline units
 *
 * Each Timeline has an associated unit in which extent and progress is
 * measured, if meaningful for that Timeline Type.
 *
 * Timeline units follow the numbering conventions for DEVICETYPE numbers.
 *
 * The range 0x00000000-0x0000ffff is reserved for Global Graphics.
 * The range 0xXXXX0000-0xXXXXffff is reserved for customer number XXXX.
 * The range 0xffff0000-0xfffffffe is for private use in closed environments.
 *
 * These ranges are sub-allocated here and elsewhere. Other clients may use the
 * values of the Global Graphics units defined contiguously from zero. These
 * units will not change in future releases.
 */
typedef HqnIdent sw_tl_unit ;  /* Timeline units */

enum {
  SW_TL_UNIT_INVALID = 0,
  SW_TL_UNIT_NONE,
  SW_TL_UNIT_BYTES,
  SW_TL_UNIT_JOBS,
  SW_TL_UNIT_PAGES,
  SW_TL_UNIT_BANDS,
  SW_TL_UNIT_LINES
} ;

/* -------------------------------------------------------------------------- */
/** \brief Timeline priorities
 *
 * Timelines within a hierarchy can have different priorities. These decide
 * whether ending a Timeline automatically ends its children or affects its
 * parent.
 *
 * Higher priority Timelines outrank lower priority Timelines. The priority of
 * the Timeline being Ended or Aborted is compared with all descendants (and
 * ancestors, in the case of Abort) to decide whether to End or Abort
 * immediately, or whether to propogate the Abort upwards. Timelines will
 * only end children or abort children and/or parents if they are strictly
 * greater priority than the child or parent.
 *
 * See SwTimelineEnd() and SwTimelineAbort() for details.
 */

typedef int sw_tl_priority ;  /* Timeline Priorities */

enum {
  SW_TL_PRIORITY_UNKNOWN = INT_MIN, /* see SwTimelineGetPriority() */
  SW_TL_PRIORITY_NORMAL = 0         /* The usual case */
} ;

/* -------------------------------------------------------------------------- */
/** \brief Timeline extents and progress
 *
 * A Timeline can have an extent, measured in a unit. Progress along the
 * Timeline is measured as a fraction of that extent.
 *
 * A Timeline for which there is no way of knowing its extent can have an
 * indeterminate extent.
 *
 * Note that a UI will only display progress once a progress event has been
 * received.
 */

typedef double sw_tl_extent ;

#define SW_TL_EXTENT_INDETERMINATE DBL_MAX

/* -------------------------------------------------------------------------- */
/** \brief Return values
 *
 * If the Timeline reference passed in a call is not recognised, usually because
 * that Timeline has ended, SW_TL_ERROR_UNKNOWN is returned.
 *
 * SwTimelineEnd() and SwTimelineAbort() may not immediately end the Timeline,
 * either because the Timeline has a higher priority child or a Handler has
 * Handled the Timeline End/Abort Event. In such a case, SW_TL_ERROR_IN_USE is
 * returned. Note that this is purely informational - the caller does not need
 * to do anything else, responsibility for the Timeline is taken by the Handler
 * or the Timeline system.
 */

typedef int sw_tl_result ;    /* Return values */

enum {
  SW_TL_SUCCESS = 0,    /**< successful call */
  SW_TL_ERROR,          /**< Some unknown failure occurred */
  SW_TL_ERROR_UNKNOWN,  /**< Timeline ref is unknown (or has closed) */
  SW_TL_ERROR_SYNTAX,   /**< Programming error - illegal parameters */
  SW_TL_ERROR_IN_USE,   /**< Timeline has not yet ended as requested */
  SW_TL_ERROR_MEMORY    /**< Memory allocation failed */
} ;

/* -------------------------------------------------------------------------- */

/** \brief Timeline context identifiers.
 *
 * Any producer or consumer of timelines may attach a void pointer to the
 * timeline, so it can associate its own data with the timeline. Contexts are
 * identified by a number, used by the SwTimelineGetContext() and
 * SwTimelineSetContext() calls to identify the particular piece of data.
 * Timeline context identifier numbering follows the numbering conventions
 * for DEVICETYPE numbers.
 *
 * The value 0 is reserved for the creator of the timeline type, regardless of
 * whether it is Global Graphics or a customer.
 * The range 0x00000001-0x0000ffff is reserved for Global Graphics.
 * The range 0xXXXX0000-0xXXXXffff is reserved for customer number XXXX.
 * The range 0xffff0000-0xffffffff is for private use in closed environments.
 *
 * These ranges are sub-allocated here and elsewhere.
 *
 * The owner context (ID zero) may only be associated with the timeline by
 * the creator of the timeline, as an argument to the SwTimelineStart() call.
 *
 * Callers that associate data with a timeline must also take responsibility
 * for managing the lifetime of that data. In particular, callers should be
 * aware that timelines may be prolonged beyond their normal scope, if a child
 * timeline with higher priority defers ending. The caller should either detach
 * context references from the timeline when it sees an EVENT_TIMELINE_END, or
 * use reference counting or another liveness management technique to ensure
 * the context can survive until EVENT_TIMELINE_ENDED is issued.
 */
typedef HqnIdent sw_tl_context ;  /* Context identifier */

enum {
  SW_TL_CONTEXT_OWNER = 0   /* Timeline owner's id is always zero */
} ;

/* -------------------------------------------------------------------------- */
/* The common format for all Event messages */

typedef struct {
  sw_tl_ref      ref ;      /**< Unique reference of the Timeline */
  sw_tl_ref      parent ;   /**< Unique ref of our parent */
  sw_tl_type     type ;     /**< Type of this Timeline */
  sw_tl_state    state ;    /**< eg TL_STATE_START, TL_STATE_ENDING etc */
  sw_tl_unit     unit ;     /**< Unit this Timeline is measured in */
  void *         context ;  /**< Timeline creator's context pointer */
  sw_tl_extent   start ;    /**< Start of the Timeline extent, eg zero */
  sw_tl_extent   end ;      /**< End of the Timeline extent, eg file length */
  sw_tl_extent   progress ; /**< Current progress */
  sw_tl_priority priority ; /**< Priority of the Timeline or message */
  int            reason ;   /**< Type identifier of the message or abort */
  const uint8 *  title ;    /**< Pointer to the title */
  size_t         length ;   /**< Length of the above */
} SWMSG_TIMELINE ;

/* -------------------------------------------------------------------------- */
/** \brief  Create a Timeline, issuing a Start Event immediately.

   \param type         Timeline type, such as Job, Document, Interpret, Render

   \param parent       ID of parent Timeline, or SW_TL_REF_INVALID for an
                       autonomous Timeline

   \param start        Start of the Timeline extent in some units, eg total
                       bytes, number of pages. Often zero.

   \param end          End of the Timeline extent, or SW_TL_EXTENT_INDETERMINATE

   \param unit         Unit of the above extent, eg Bytes, Pages, Lines

   \param priority     Used to resolve Ending a Timeline with ongoing child
                       Timelines

   \param[in] context  The primary context supplied by the Timeline owner

   \param[in] title    A title for the Timeline. Could be leafname, page title
                       or job phase

   \param length       The length of the title

   \return             The new Timeline reference or SW_TL_REF_INVALID if it
                       fails.

   A Timeline that has no concept of length, for which progress has no real
   meaning, should have an extent end of SW_TL_EXTENT_INDETERMINATE. A GUI may
   represent this as an indeterminate progress bar instead of a standard
   progress bar.

   A EVENT_TIMELINE_START event is issued immediately after creating the
   Timeline. If a Handler returns SW_EVENT_HANDLED the Timeline is immediately
   ended and SW_TL_REF_INVALID will be returned to the creator.

   See SwTimelineEnd() for usage of the priority parameter.

   The title is copied so need not be maintained.

   The supplied context is delivered in Timeline Events and is identified by
   context id zero. It can be retrieved with SwTimelineGetContext() but
   cannot be changed - Timeline events can occur at any time and in other
   threads, so changing this primary context would invalidate events currently
   in flow.
 */
sw_tl_ref HQNCALL SwTimelineStart(sw_tl_type type, sw_tl_ref parent,
                                  sw_tl_extent start, sw_tl_extent end,
                                  sw_tl_unit unit, sw_tl_priority priority,
              /*@in@*/ /*@null@*/ void * context,
              /*@in@*/ /*@null@*/ const uint8 * title, size_t length) ;

/* -------------------------------------------------------------------------- */
/** \brief  Potentially end a Timeline. Whether the Timeline End Event is issued
   immediately depends on whether there are ongoing descendant Timelines of the
   same or higher priority.

   \param ref  Timeline reference to end

   \return     SW_TL_SUCCESS if the Timeline has ended,
               SW_TL_ERROR_UNKNOWN if the Timeline is not known, or
               SW_TL_ERROR_IN_USE if the Timeline is being kept alive by
               a child or Handler

   The priority can be used to achieve automatic Timeline ending when
   subordinate Timelines end. The priority of the Timeline being ended is
   compared with all descendant Timelines, and if any are the same or higher,
   the Timeline will be prolonged.

   If not, an EVENT_TIMELINE_ENDING event is issued. If no Handler objects by
   returning SW_EVENT_HANDLED, then the Timeline ends. If the Timeline's parent
   is waiting, it may end or abort also, depending on the priorities of other
   descendants. If a Handler does object then responsibility for ending the
   Timeline passes to the Handler.

   Note that if SW_TL_ERROR_IN_USE is returned, the Timeline is being prolonged.
   This means that the attached primary context must also continue to exist. For
   such contexts that must be discarded, it may be best to discard them in a
   default priority EVENT_TIMELINE_ENDED Handler for that Timeline reference.
 */
sw_tl_result HQNCALL SwTimelineEnd(sw_tl_ref ref) ;

/* -------------------------------------------------------------------------- */
/** \brief  Potentially abort a Timeline. Whether the Timeline Abort Event is
   issued immediately depends on whether there are ongoing descendant Timelines
   of the same or higher priority.

   \param ref     Timeline reference to abort

   \param reason  A reason code for the abort, defined per Timeline type

   \return        SW_TL_SUCCESS if the Timeline has ended,
                  SW_TL_ERROR_UNKNOWN if the Timeline is not known, or
                  SW_TL_ERROR_IN_USE if the Timeline is being kept alive by
                  a child or Handler

   If the Timeline is prolonged by a descenant, it stays at pending-abort, and
   will attempt to abort again when that child ends. If prolonged by a Handler,
   it also stays at pending abort. This would be an unusual thing for a Handler
   to do.

   If the Timeline does abort, it will also try to abort its parent if its
   parent's priority is lower than the priority of the aborting Timeline.

   Note that if SW_TL_ERROR_IN_USE is returned, the Timeline is being prolonged.
   This means that the attached primary context must also continue to exist. For
   such contexts that must be discarded, it may be best to discard them in a
   default priority EVENT_TIMELINE_ABORTED Handler for that Timeline reference.
*/
sw_tl_result HQNCALL SwTimelineAbort(sw_tl_ref ref, int reason) ;

/* -------------------------------------------------------------------------- */
/** \brief  Change the title of a Timeline. Issues an Event before changing the
   title.

   \param ref        Timeline reference to rename

   \param[in] title  Pointer to a string, or null to remove the name

   \param length     Length of the unterminated string

   \return           SW_TL_SUCCESS normally,
                     SW_TL_ERROR_UNKNOWN if the Timeline is not known

   The title will be copied so does not need to be maintained.

   An EVENT_TIMELINE_TITLE event is issued. A Handler may choose to modify the
   title or suppress it by changing the pointer and length in the Event Message.
   If a Handler returns SW_EVENT_HANDLED, no title change occurs at all.
 */
sw_tl_result HQNCALL SwTimelineSetTitle(sw_tl_ref ref,
                               /*@in@*/ const uint8 * title, size_t length) ;

/* -------------------------------------------------------------------------- */
/** \brief  Return the Timeline's title and length.

   \param ref          Timeline reference

   \param[out] buffer  If not null, this is filled in with the title, space
                       permitting

   \param size         The size of the supplied buffer

   \return             The actual length of the title

   This call allows the Timeline's title to be retrieved. It can be called with
   a null buffer pointer to find the length of the title so a suitable buffer
   can be allocated before calling this function again, but be prepared for the
   title to be changed by another thread between the calls.

   If the returned title length is greater than the buffer size passed in, the
   title returned will have been truncated.

   This call does not issue an Event.
 */
size_t HQNCALL SwTimelineGetTitle(sw_tl_ref ref, uint8 * buffer, size_t size) ;

/* -------------------------------------------------------------------------- */
/** \brief  Extend the length of the Timeline. Issues an Event for a nonzero
   change.

   \param ref    Timeline reference

   \param start  New start of the Timeline extent

   \param end    New end of the Timeline extent, or SW_TL_EXTENT_INDETERMINATE

   \return       SW_TL_SUCCESS normally,
                 SW_TL_ERROR_UNKNOWN if the Timeline is not known

   A Timeline created with an end of SW_TL_EXTENT_INDETERMINATE will not be
   represented by a GUI as having a known length.

   An EVENT_TIMELINE_EXTEND event will be issued. The Timeline system ignores
   the return code from this event.
 */
sw_tl_result HQNCALL SwTimelineSetExtent(sw_tl_ref ref, sw_tl_extent start,
                                         sw_tl_extent end) ;

/* -------------------------------------------------------------------------- */
/** \brief  Update the progress of the Timeline, in the units given when the
   Timeline was started. Issues an Event if the current value changes.

   \param ref       Timeline reference

   \param progress  Current progress value, automatically extending the
                    Timeline extent if it is outside that range.

   \return          SW_TL_SUCCESS normally,
                    SW_TL_ERROR_UNKNOWN if the Timeline is not known

   Progressing beyond the current Timeline extent automatically extends the
   extent, unless the end is at SW_TL_EXTENT_INDETERMINATE.

   A Handler may choose to reflect the progress of a child Timeline in the
   parent Timeline, eg by reducing the parent's extent start by some amount,
   and then updating the parent's progress accordingly.

   An EVENT_TIMELINE_PROGRESS event is issued. The Timeline system ignores
   the return code from this event.
 */
sw_tl_result HQNCALL SwTimelineSetProgress(sw_tl_ref ref,
                                           sw_tl_extent progress) ;

/* -------------------------------------------------------------------------- */
/** \brief  Return the extent, unit and progress through a Timeline.

   \param ref            Timeline reference

   \param[out] start     If not null, this is filled in with the extent start

   \param[out] end       If not null, this is filled in with the extent end

   \param[out] progress  If not null, this is filled in with the progress

   \param[out] unit      If not null, this is filled in with the unit

   \return               SW_TL_SUCCESS normally,
                         SW_TL_ERROR_UNKNOWN if the Timeline is not known

   An extent end of SW_TL_EXTENT_INDETERMINATE means the Timeline is not of a
   known length. The progress and unit may still be informational.

   This call does not issue an Event
 */
sw_tl_result HQNCALL SwTimelineGetProgress(sw_tl_ref ref,
                               /* @out@ */ sw_tl_extent * start,
                               /* @out@ */ sw_tl_extent * end,
                               /* @out@ */ sw_tl_extent * progress,
                               /* @out@ */ sw_tl_unit * unit) ;

/* -------------------------------------------------------------------------- */
/** \brief  Return the NEAREST ancestor of the Timeline of the specified type.

   \param ref   Timeline reference

   \param type  Type of ancestor to find, or SW_TL_TYPE_ANY to find immediate
                parent.

   \return      The Timeline reference of the parent, or SW_TL_REF_INVALID

   This can be used to find the immediate parent of a Timeline, if it has one,
   or to find a Timeline's ancestor of a particular known type.

   This allows something that cares about Job Timelines, for example, to find
   out whether a particular message Event belongs to its job. Note however that
   it may in principle be possible for there to be multiple nested Job
   Timelines, so the first Job parent found may itself be a child of the Job
   Timeline in question.

   This call does not issue an Event.
 */
sw_tl_ref HQNCALL SwTimelineGetAncestor(sw_tl_ref ref, sw_tl_type type) ;

/* -------------------------------------------------------------------------- */
/** \brief  Ensure the Timeline or an ancestor is of the given type

   \param ref   Timeline reference

   \param type  Type of Timeline to find

   \return      The Timeline reference of that type, or SW_TL_REF_INVALID

   This is similar to SwTimelineGetAncestor, but can return the Timeline
   passed in and does not accept SW_TL_TYPE_ANY as a parameter.

   If the Timeline passed in and all of its ancestors are not of the required
   type, SW_TL_REF_INVALID is returned.

   It is equivalent to:
   \code
   type = SwTimelineGetType(ref) ;
   if (type == SW_TL_TYPE_NONE)
     ref = SW_TL_REF_INVALID ;
   else if (type != requiredType)
     ref = SwTimelineGetAncestor(ref, requiredType) ;
   \endcode

   This call does not issue an Event.
 */
sw_tl_ref HQNCALL SwTimelineOfType(sw_tl_ref ref, sw_tl_type type) ;

/* -------------------------------------------------------------------------- */
/** \brief  Return the Timeline type if known.

   \param ref  Timeline reference

   \return     The type of the Timeline if known, or SW_TL_TYPE_NONE

   This call does not issue an Event.
 */

sw_tl_type HQNCALL SwTimelineGetType(sw_tl_ref ref) ;

/* -------------------------------------------------------------------------- */
/** \brief  Return the Timeline priority if known.

   \param ref  Timeline reference

   \return     The priority of the Timeline if known, or SW_TL_PRIORITY_UNKNOWN

   This call does not issue an Event.
 */

sw_tl_priority HQNCALL SwTimelineGetPriority(sw_tl_ref ref) ;

/* -------------------------------------------------------------------------- */
/** \brief  Attach a secondary context to a Timeline.

   \param ref          Timeline reference

   \param id           Unique identifier for context.

   \param[in] context  The context pointer to attach to the Timeline, or null

   \retval SW_TL_SUCCESS  The context was attached to the timeline.
   \retval SW_TL_ERROR_UNKNOWN If the Timeline is not known
   \retval SW_TL_ERROR_SYNTAX  If an attempt was made to change the owner
           context (identifier zero).

   The Timeline system places no interpretation on these contexts, they are for
   client use. Context management is not performed by the Timeline system, and
   these contexts can only be retrieved using this call while the Timeline
   continues to exist.

   Note: Entities that wish to associate a context with a Timeline that will
   outlive the Timeline itself should register the context with RDR using a
   Class of RDR_CLASS_TIMELINE, the Timeline reference as the RDR Type, and
   their context identifier as the RDR ID. Such an RDR will have to be manually
   deregistered when the Timeline ends (though not necessarily immediately).

   This call does not issue an Event.
 */
sw_tl_result HQNCALL SwTimelineSetContext(sw_tl_ref ref, sw_tl_context id,
                                          /*@in@*/ /*@null@*/ void * context) ;

/* -------------------------------------------------------------------------- */
/** \brief  Return a context from the Timeline.

   \param ref     Timeline reference

   \param[in] id  Unique identifier for context. Zero for Timeline creator's
                  primary context

   returns        The pointer registered with that identifier, or null

   The Timeline system places no interpretation on these contexts, they are for
   client use. Note that NULL is returned if the Timeline reference or context
   id is not known.

   This call does not issue an Event.
*/
void * HQNCALL SwTimelineGetContext(sw_tl_ref ref, sw_tl_context id) ;

/* -------------------------------------------------------------------------- */

typedef struct {
  HqBool       valid ;
  sw_tl_ref    (HQNCALL *start)       (sw_tl_type, sw_tl_ref, sw_tl_extent,
                                       sw_tl_extent, sw_tl_unit, sw_tl_priority,
                                       void*, const uint8*, size_t) ;
  sw_tl_result (HQNCALL *end)         (sw_tl_ref) ;
  sw_tl_result (HQNCALL *abort)       (sw_tl_ref, int) ;
  sw_tl_result (HQNCALL *set_title)   (sw_tl_ref, const uint8*, size_t) ;
  size_t       (HQNCALL *get_title)   (sw_tl_ref, uint8*, size_t) ;
  sw_tl_result (HQNCALL *set_extent)  (sw_tl_ref, sw_tl_extent, sw_tl_extent) ;
  sw_tl_result (HQNCALL *set_progress)(sw_tl_ref, sw_tl_extent) ;
  sw_tl_result (HQNCALL *get_progress)(sw_tl_ref, sw_tl_extent*, sw_tl_extent*,
                                       sw_tl_extent*, sw_tl_unit*) ;
  sw_tl_ref    (HQNCALL *get_ancestor)(sw_tl_ref, sw_tl_type) ;
  sw_tl_type   (HQNCALL *get_type)    (sw_tl_ref) ;
  sw_tl_result (HQNCALL *set_context) (sw_tl_ref, sw_tl_context, void*) ;
  void *       (HQNCALL *get_context) (sw_tl_ref, sw_tl_context) ;
  sw_tl_priority (HQNCALL *get_priority)(sw_tl_ref) ;
  sw_tl_ref    (HQNCALL *of_type)     (sw_tl_ref, sw_tl_type) ;
} sw_timeline_api_20110623 ;

/* Skin and Core both have their own version of this API pointer. The Skin's
   points to the API structure at build time, the Core's is found via RDR at
   run time during DLL initialisation (for example).
*/
extern sw_timeline_api_20110623 * timeline_api ;

/* -------------------------------------------------------------------------- */
/* Function name spoofing.

   For simplicity and symmetry, these function calls are actually indirected
   through an API structure pointed to by the above API pointer. This is hidden
   from the programmer by this series of macros.

   When the API implementor includes this file and wants access to the real
   function addresses it defines TIMELINE_IMPLEMENTOR.
*/
#if !defined(TIMELINE_IMPLEMENTOR)

#define SwTimelineStart       timeline_api->start
#define SwTimelineEnd         timeline_api->end
#define SwTimelineAbort       timeline_api->abort
#define SwTimelineSetTitle    timeline_api->set_title
#define SwTimelineGetTitle    timeline_api->get_title
#define SwTimelineSetExtent   timeline_api->set_extent
#define SwTimelineSetProgress timeline_api->set_progress
#define SwTimelineGetProgress timeline_api->get_progress
#define SwTimelineGetAncestor timeline_api->get_ancestor
#define SwTimelineGetType     timeline_api->get_type
#define SwTimelineSetContext  timeline_api->set_context
#define SwTimelineGetContext  timeline_api->get_context
#define SwTimelineGetPriority timeline_api->get_priority
#define SwTimelineOfType      timeline_api->of_type

#endif

/* -------------------------------------------------------------------------- */

#ifdef DEBUG_BUILD

/* Timeline API references are positive integers, so we can use negative RDR
   type values for system purposes. */
enum {
  TL_DEBUG_TYPE_NAME = -1 /**< RDR type used to find type NUL-terminated char *
                             name for timeline types. */
} ;

enum {
  EVENT_TIMELINE_DEBUG = EVENT_TIMELINE+99
} ;

typedef struct {
  sw_tl_ref tl ;
  void (HQNCALL *monitorf)(uint8 *format, ...) ;
} sw_tl_debug ;

#endif /* DEBUG_BUILD */

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */

#endif /* __TLAPI_H__ */
