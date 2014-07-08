/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swevents.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Defines the core's external event API.
 */

#ifndef __SWEVENTS_H__
#define __SWEVENTS_H__ (1)

#ifdef __cplusplus
extern "C" {
#endif

#include "eventapi.h"
#include "timelineapi.h"
#include "swdataapi.h"

#define SWEVT(x_) (EVENT_CORE + (x_))

/** \brief Core event ids.
 *
 * Core events are partitioned into sub-groups so that related events can be
 * added if necessary without disturbing the overall order.
 */
enum {
  SWEVT_INTERRUPT_USER        = SWEVT(0),   /* Consumes SWMSG_INTERRUPT. */
  SWEVT_INTERRUPT_TIMEOUT     = SWEVT(1),   /* Consumes SWMSG_INTERRUPT. */
  SWEVT_ASYNC_PS              = SWEVT(10),  /* Consumes SWMSG_ASYNC_PS. */
  SWEVT_PROGRESS_UPDATE       = SWEVT(20),  /* Consumed, no message. */
  SWEVT_HT_GENERATION_SEARCH  = SWEVT(30), /* Generates SWMSG_HT_GENERATION_SEARCH */
  SWEVT_HT_GENERATION_START   = SWEVT(31), /* Generates SWMSG_HT_GENERATION_START */
  SWEVT_HT_GENERATION_END     = SWEVT(32), /* Generates SWMSG_HT_GENERATION_END */
  SWEVT_HT_USAGE_THRESHOLD    = SWEVT(43), /* Generates SWMSG_HT_USAGE_THRESHOLD */
  SWEVT_HT_USAGE_SPOT         = SWEVT(44), /* Generates SWMSG_HT_USAGE_SPOT */
  SWEVT_HT_USAGE_MODULAR      = SWEVT(45), /* Generates SWMSG_HT_USAGE_MODULAR */
  SWEVT_HT_USAGE_COLORANT     = SWEVT(46), /* Generates SWMSG_HT_USAGE_COLORANT */
  SWEVT_INTERPRET_ERROR       = SWEVT(100), /* Generates SWMSG_ERROR */
  SWEVT_RENDER_ERROR          = SWEVT(101), /* Generates SWMSG_ERROR */
  SWEVT_PRINTER_ERROR         = SWEVT(102), /* Generates SWMSG_ERROR */
  /* Allow plenty of room for more error events, in case we split out
     individual errors. */
  SWEVT_RR_PAGE_DEFINE        = SWEVT(200), /* Generates SWMSG_RR_PAGE_DEFINE */
  SWEVT_RR_PAGE_READY         = SWEVT(201), /* Generates SWMSG_RR_PAGE_REF */
  SWEVT_RR_PAGE_COMPLETE      = SWEVT(202), /* Consumes SWMSG_RR_PAGE_REF */
  SWEVT_RR_ELEMENT_DEFINE     = SWEVT(203), /* Generates SWMSG_RR_ELEMENT_DEFINE */
  SWEVT_RR_ELEMENT_LOCK       = SWEVT(204), /* Generates SWMSG_RR_ELEMENT_REF */
  SWEVT_RR_ELEMENT_UNLOCK     = SWEVT(205), /* Generates SWMSG_RR_ELEMENT_REF */
  SWEVT_RR_ELEMENT_PENDING    = SWEVT(206), /* Generates SWMSG_RR_ELEMENT_REF */
  SWEVT_RR_ELEMENT_QUERY      = SWEVT(207), /* Generates SWMSG_RR_ELEMENT_QUERY */
  SWEVT_RR_ELEMENT_UPDATE_RASTER = SWEVT(208), /* Generates SWMSG_RR_ELEMENT_UPDATE_RASTER */
  SWEVT_RR_ELEMENT_UPDATE_HITS = SWEVT(209), /* Generates SWMSG_RR_ELEMENT_UPDATE_HITS */
  SWEVT_RR_CONNECT             = SWEVT(210), /* Generates SWMSG_RR_CONNECT */
  SWEVT_RR_DISCONNECT          = SWEVT(211),  /* Consumes SWMSG_RR_DISCONNECT */

  /* Event 300 is reserved for monitor by events */
  /* Event 301 is reserved for an internal event */
#ifndef LESDK
  /* We build corelib with this but don't publish it to OEMs */
  SWEVT_CALIBRATION_OP         = SWEVT(301) /* Generates SWMSG_CALIBRATION_OP */
#endif
};

#undef SWEVT

/* Counted length strings for PS names and strings */
struct PS_STRING {
  const uint8* string;
  size_t    length;
};

/*---------------------------------------------------------------------------*/
/* Screen generation event messages */
/** Screen generation search. */
typedef struct SWMSG_HT_GENERATION_SEARCH {
  sw_tl_ref timeline ;
  double  frequency;
} SWMSG_HT_GENERATION_SEARCH ;

/** Screen generation start. */
typedef struct SWMSG_HT_GENERATION_START {
  sw_tl_ref timeline ;
  double  frequency;
  double  angle;
  struct  PS_STRING spot_name;
} SWMSG_HT_GENERATION_START ;

/** Screen generation end. */
typedef struct SWMSG_HT_GENERATION_END {
  sw_tl_ref timeline ;
  double  frequency;
  double  angle;
  struct  PS_STRING spot_name;
  double  deviated_frequency;
  double  frequency_inaccuracy;
  double  angle_inaccuracy;
} SWMSG_HT_GENERATION_END ;


/*---------------------------------------------------------------------------*/
/* Screen usage event messages */


/** Screen usage message for threshold screens. */
typedef struct SWMSG_HT_USAGE_THRESHOLD {
  sw_tl_ref timeline ;
  struct PS_STRING screen_name;
  struct PS_STRING colorant_name;
  struct PS_STRING type_name;
  unsigned int tones_used;
  unsigned int tones_total;
  unsigned int colorants_using;
  int token;
} SWMSG_HT_USAGE_THRESHOLD ;


/** Screen usage message for spot function screens. */
typedef struct SWMSG_HT_USAGE_SPOT {
  sw_tl_ref timeline ;
  double  frequency;
  double  angle;
  struct PS_STRING function_name;
  HqBool unoptimized_angle;
  double  deviated_frequency;
  double  frequency_inaccuracy;
  double  angle_inaccuracy;
  HqBool excessive_inaccuracy;
  unsigned int tones_used;
  unsigned int tones_total;
  int token;
} SWMSG_HT_USAGE_SPOT ;


/** Screen usage message for modular screens. */
typedef struct {
  sw_tl_ref timeline ;
  struct PS_STRING screen_name;
  struct PS_STRING module_name;
  struct PS_STRING colorant_name;
  struct PS_STRING type_name;
  int token;
} SWMSG_HT_USAGE_MODULAR;


/** Screen usage message listing a colorant used by a screen. */
typedef struct {
  int token;
  struct PS_STRING colorant_name;
  struct PS_STRING type_name;
  HqBool last_one;
} SWMSG_HT_USAGE_COLORANT;


/*---------------------------------------------------------------------------*/
#define MAX_ASYNC_ID  (31) /* Range is 0 to MAX_ASYNC_ID inclusive */

/* Async PS event message */
typedef struct SWMSG_ASYNC_PS {
  unsigned int id;
} SWMSG_ASYNC_PS ;


/*---------------------------------------------------------------------------*/
/** Interrupt and timeout event message generated by the skin. For now, we
    share a single message between the two interrupt types. */
typedef struct SWMSG_INTERRUPT {
  sw_tl_ref timeline;
} SWMSG_INTERRUPT ;

/*---------------------------------------------------------------------------*/
/** Detail for unhandled error event.

    Multiple error detail nodes may be on a detail list. Key/Value nodes come
    in pairs, and File nodes may be succeeded by a Line node.
 */
typedef struct ERROR_DETAIL {
  enum {
    ERROR_DETAIL_KEY,    /**< Key related to succeeding value. */
    ERROR_DETAIL_VALUE,  /**< Value related to preceding key. */
    ERROR_DETAIL_FILE,   /**< File/stream name. */
    ERROR_DETAIL_LINE,   /**< Line number for following file. */
    ERROR_DETAIL_INFO,   /**< General informational message. */
    NUM_ERROR_DETAIL
  } type ;
  struct PS_STRING message ;  /**< Formatted detail message. */
  struct ERROR_DETAIL *next ; /**< Next error detail node. */
} ERROR_DETAIL ;

/** Unhandled error event. */
typedef struct SWMSG_ERROR {
  sw_tl_ref timeline ;
  unsigned int page_number ;
  HqBool fail_job ;          /**< Does the RIP consider this to be worthy of
                                failing the whole job? The event handler can
                                set this to FALSE to that this error
                                shouldn't mark the job as failing, or TRUE to
                                indicate it should. This primarily affects
                                the problem status reported by the JOB state
                                end routines, and the log messages output as
                                a result of those. */
  HqBool suppress_handling ; /**< Should the RIP suppress its normal error
                                reporting? The event handler can set this to
                                TRUE to indicate that normal error handling
                                should be suppressed. By the time the error
                                is issued, it is too late to avoid it
                                stopping the job, and there isn't sufficient
                                context to make that decision anyway, but
                                this does afford some control over the
                                messages output. */
  unsigned int error_number ;
  struct PS_STRING error_name ; /**< Counted string for error name. */
  struct PS_STRING command ; /**< Counted string for reported command name. */
  ERROR_DETAIL *detail ;     /**< Linked list of contextual detail items. */
} SWMSG_ERROR ;

/*---------------------------------------------------------------------------*/
/** Length of the string (including trailing nul) for External
    Retained Raster cache IDs. */
#define RR_CACHE_ID_LENGTH 256

/** Length of the string (including trailing nul) for External
    Retained Raster setup IDs. */
#define RR_SETUP_ID_LENGTH 256

/** Message for SWEVT_RR_CONNECT: establishes a connection to a
    Retained Raster cache instance. */
typedef struct SWMSG_RR_CONNECT {
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** A null-terminated string identifying the cache to which we wish
      to connect. This discrimination allows a single RIP to have
      multiple Retained Raster-capable implementations installed, and
      to direct its events at the appropriate one. */
  uint8 cache_id[ RR_CACHE_ID_LENGTH ] ;
  /** A null-terminated string identifying the setup associated with
      this connection to the cache. The contract between provider and
      user of this interface is that this setup id uniquely describes
      a configuration of the RIP in every respect; if this is not the
      case then correct behaviour cannot be guaranteed. That is, if
      any parameter of the page device or other salient configuration
      item differs between the creation and use of items in the cache,
      undefined behavour will result. As a special case, if this ID is
      the empty string then a cache instance is created upon
      connection and is immediately destroyed upon disconnection. */
  uint8 setup_id[ RR_SETUP_ID_LENGTH ] ;
  /** If the event is successfully handled, this is set to an opaque
      handle uniquely identifying the connection. */
  void *connection ;
} SWMSG_RR_CONNECT ;

/** Message for SWEVT_RR_DISCONNECT: disconnects from a Retained
    Raster cache instance. */
typedef struct SWMSG_RR_DISCONNECT {
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** An opaque handle uniquely identifying the connection to be
      relinquished. */
  void *connection ;
} SWMSG_RR_DISCONNECT ;

/** Length of the id strings in External Retained Raster elements. */
#define RR_ELEMENT_ID_LENGTH 16

/** An element on a page in the External Retained Raster interface
    Page Define message. */
typedef struct RR_PAGE_DEFINE_ELEMENT {
  /** Pointer to the unique ID for this element, of length
      RR_ELEMENT_ID_LENGTH (to be matched against the ID field in the
      raster element events). */
  uint8 *id ;
  /** The x offset of the page element. */
  int32 x ;
  /** The y offset of the page element. */
  int32 y ;
} RR_PAGE_DEFINE_ELEMENT ;

/** A page in the External Retained Raster interface Page Define
    message. */
typedef struct RR_PAGE_DEFINE_PAGE {
  /** Number of raster elements on this page. */
  uint32 element_count ;
  /** Array of raster elements. */
  RR_PAGE_DEFINE_ELEMENT *elements ;
} RR_PAGE_DEFINE_PAGE ;

/** An External Retained Raster interface Page Define event. Note that
    the memory referred to in this structure should be treated as
    transient: if the code associated with a handler wishes to use any
    data contained in this event after the handler has returned, it
    should make a copy. */
typedef struct SWMSG_RR_PAGE_DEFINE {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** The number of pages being defined. */
  uint32 page_count ;
  /** Array of pages. */
  RR_PAGE_DEFINE_PAGE *pages ;
} SWMSG_RR_PAGE_DEFINE ;

/** External Retained Raster interface Page reference: passed during
    \c SWEVT_RR_PAGE_READY and \c SWEVT_RR_PAGE_COMPLETE events. */
typedef struct SWMSG_RR_PAGE_REF {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** Zero-based index into the array previously passed in the \c
      SWMSG_RR_PAGE_DEFINE message. */
  uint32 page_index;
} SWMSG_RR_PAGE_REF ;

/** Retained Raster interface Page Element Define event. */
typedef struct SWMSG_RR_ELEMENT_DEFINE {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** Unique ID, length \c RR_ELEMENT_ID_LENGTH. */
  uint8 *id ;
  /** Minimum x extent of the page element. */
  int32 x1 ;
  /** Minimum y extent of the page element. */
  int32 y1 ;
  /** Maximum x extent of the page element. */
  int32 x2 ;
  /** Maximum y extent of the page element. */
  int32 y2 ;
} SWMSG_RR_ELEMENT_DEFINE ;

/** This is a message which identifies a particular raster element,
    and is used by three events:

    SWEVT_RR_ELEMENT_PENDING: Lets the cache instance know that a
    raster for the given id is on its way. This allows the cache to
    respond to queries which arrive before the raster data as if it
    was already there, thus preventing the redundant work of creating
    the raster twice.

    SWEVT_RR_ELEMENT_LOCK: Requests that the cache instance does not
    purge the given element until it sees the corresponding unlock
    event. This controls the low water mark level of the cache such
    that the current page can be guaranteed to complete. Of course the
    cache implementation is free to store raster elements even after
    the unlock, and should be encouraged to do so. This locking
    mechanism simply defines the minimum requirement for successful
    output.

    SWEVT_RR_ELEMENT_UNLOCK: Unlock a raster element. If no other
    locks apply, it is eligible for purging at the discretion of the
    cache implementation. */
typedef struct SWMSG_RR_ELEMENT_REF {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** Unique ID, length \c RR_ELEMENT_ID_LENGTH. */
  uint8 *id ;
} SWMSG_RR_ELEMENT_REF ;

/** Looks for a raster element with the given id. Returns
    SW_EVENT_CONTINUE if nothing found, in which case \c handle is
    undefined. Note that NULL is a valid value for handle which means
    that the element is defined, but is degenerate. */
typedef struct SWMSG_RR_ELEMENT_QUERY {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** Unique ID, length \c RR_ELEMENT_ID_LENGTH. */
  uint8 *id ;
  /** Element handle - an opaque reference to the cache instance, if
      any. This will not be used by the RIP directly if external
      retained raster is on. */
  uintptr_t handle ;
} SWMSG_RR_ELEMENT_QUERY ;

/** Update the raster element with the given values for \c raster and
    \c size. Only used in Internal Retained Raster: not relevant for
    External Retained Raster. */
typedef struct SWMSG_RR_ELEMENT_UPDATE_RASTER {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** Unique ID, length \c RR_ELEMENT_ID_LENGTH. */
  uint8 *id ;
  /** Opaque handle to the raster data itself. */
  uintptr_t raster ;
  /** Size of the raster data in bytes. */
  size_t size ;
} SWMSG_RR_ELEMENT_UPDATE_RASTER ;

/** Increment or decrement the hit count for the element identified by
    \c id by the amount given in \c hits_delta - whether to add or
    subtract to the hits total is determined by the \c raise flag. */
typedef struct SWMSG_RR_ELEMENT_UPDATE_HITS {
  /** Opaque handle to the connection to which this event is
      relevant. All handlers whose connection handle doesn't match
      should immediately return \c SW_EVENT_CONTINUE. */
  void *connection ;
  /** Identifies the job containing the pages. */
  sw_tl_ref timeline ;
  /** Unique ID, length \c RR_ELEMENT_ID_LENGTH. */
  uint8 *id ;
  /** Whether to raise or lower the hit count. */
  HqBool raise ;
  /** Number of hits to add to or subtract from the overall hits
      remaining count. */
  unsigned int hits_delta ;
} SWMSG_RR_ELEMENT_UPDATE_HITS ;


#ifndef LESDK
/** Message for SWEVT_CALIBRATION_OP for calibration setup related actions
    such as calculating a new calibration curve from the previous one. The
    PS calibrationop operator issues this event. */
typedef struct {
  /** The type of setup action required. A handler which does not support
      this action should immediately return \c SW_EVENT_CONTINUE. */
  struct PS_STRING action ;
  /** A representation of the PS stack including a the \c action as the top
      element. Any number of further elements may be available (or none) as
      defined by the action. It is up to the handler to leave the stack
      in an appropriate final state for the result it returns. */
  sw_datum *stack ;
} SWMSG_CALIBRATION_OP ;
#endif

#ifdef __cplusplus
}
#endif

#endif /* !__SWEVENTS_H__ */

