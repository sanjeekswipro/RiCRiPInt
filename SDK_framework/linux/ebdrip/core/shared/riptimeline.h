/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:riptimeline.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Expose top-level RIP timelines.
 */

#ifndef __RIPTIMELINE_H__
#define __RIPTIMELINE_H__

#include "timelineapi.h"
#include "swtimelines.h"

extern sw_tl_ref core_tl_ref ;
extern sw_tl_priority core_tl_priority ;

/** \brief Create a new timeline and push it on a stack of timelines.

    \param[in,out] stack  A pointer to a timeline reference.
    \param type       The timeline type to start.
    \param end        The end extent of the timeline.
    \param unit       The units that the timeline is measured in.
    \param context    The timeline's context pointer.
    \param title      The title of the timeline.
    \param length     The length of the title of the timeline.

    \returns The timeline reference of the new timeline, or SW_TIMELINE_INVALID
             if the timeline could not be created.

    On entry, \c stack contains the parent. If this function succeeds in
    creating a new timeline, \c stack will be updated with the reference of
    the new timeline. The new timeline will be created with a lower priority
    than its parent timeline, and a start position of 0. If this function
    fails to create a new timeline, then \c stack is not updated.
 */
sw_tl_ref timeline_push(/*@in@*/ /*@null@*/ sw_tl_ref *stack,
                        sw_tl_type type, sw_tl_extent end, sw_tl_unit unit,
                        /*@in@*/ /*@null@*/ void *context,
                        /*@in@*/ /*@null@*/ const uint8 *title, size_t length) ;

/** \brief End or abort a timeline, popping it and its descendents from a stack.

    \param[in,out] stack  A pointer to a timeline reference.
    \param type           The timeline type to end. This need not be the top
                          reference on the stack, the closest matching type
                          on the stack will be ended. TL_TYPE_ANY can be
                          specified to pop the top item from the stack,
                          regardless of its type.
    \param ok             A flag indicating if the timeline succeeded, and
                          should end, or failed, and should be aborted.

    \returns The timeline reference of the item popped from the stack, or
             SW_TIMELINE_INVALID if the timeline type specified was not
             found on the stack.

    If the timeline type specified was not found on the stack, then the stack
    is not modified.
 */
sw_tl_ref timeline_pop(sw_tl_ref *stack, sw_tl_type type, Bool ok) ;

/** Priorities for special types of timeline. */
enum {
  PRIORITY_RELATIVE = -10,        /**< Relative priority between stack layers */
  PRIORITY_FILE_INTERPRET = -1000 /**< File interpreter never blocks anything. */
} ;

/** Core private secondary timeline context identifiers. We use the same
    partitioning scheme as timeline identifiers, to ensure we're not
    conflicting with other identifiers. */
enum {
  SWTLC_START_TIME = TL_CORE+1, /**< Start time for core timeline. */
  SWTLC_BAND_OWNER              /**< Band owner for PGB bands. */
} ;

/** Check that a timeline function returned SW_TL_SUCCESS, issuing a trace
    message if it failed. */
#define CHECK_TL_SUCCESS(expr_) MACRO_START                             \
  sw_tl_result _res_ = (expr_) ;                                        \
  HQTRACE(_res_ != SW_TL_SUCCESS,                                       \
          ("Timeline result %d unexpected:\n " #expr_, _res_)) ;        \
  UNUSED_PARAM(sw_tl_result, _res_) ;                                   \
MACRO_END

/** Check that a timeline function did not return SW_TIMELINE_INVALID,
    issuing a trace message if it failed. */
#define CHECK_TL_VALID(expr_) MACRO_START                               \
  sw_tl_ref _ref_ = (expr_) ;                                           \
  HQTRACE(_ref_ == SW_TL_REF_INVALID,                                   \
          ("Timeline reference %d not valid:\n " #expr_, _ref_)) ;      \
  UNUSED_PARAM(sw_tl_ref, _ref_) ;                                      \
MACRO_END

#endif /* Protection from multiple inclusion */

/* Log stripped */
