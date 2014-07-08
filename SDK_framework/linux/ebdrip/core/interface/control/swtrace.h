/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swtrace.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief This header file provides access to the lightweight tracing API.
 *
 * The lightweight tracing API is used to extract fine-grained timing
 * information from the RIP.
 */
#ifndef __SWTRACE_H__
#define __SWTRACE_H__

/** \defgroup swtrace Lightweight tracing API.
 * \ingroup interface
 * \{
 */

#include "ripcall.h"
#include <stddef.h> /* intptr_t */

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Typedef for the trace handler function.
 *
 * A trace handler function can be provided to the core RIP through the
 * SwStart parameter list. There are three parameters provided to the
 * handler; the trace id is one of the SW_TRACE_* enumeration values,
 * the trace type is one of the SW_TRACETYPE_* enumeration values,
 * and the trace_designator is an opaque value which can be used to
 * correlate related trace events.
 */
typedef void (RIPFASTCALL SwTraceHandlerFn)(int trace_id,
                                            int trace_type,
                                            intptr_t trace_designator) ;

/** \brief Core trace names.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having core
 * maintainers having to modify every skin which uses the trace facility.
 */
#define SW_TRACENAMES(macro_) \
  macro_(CORE)             /* Trace startup and shutdown events. */ \
  macro_(THREAD)           /* Trace start/end of additional threads. */ \
  macro_(THREAD_CREATE)    /* Spawn time of threads. */ \
  macro_(THREAD_JOIN)      /* Join time of threads. */ \
  macro_(JOB_CONFIG)       /* Start to end of job configuration. */ \
  macro_(JOB)              /* Start to end of job. */ \
  macro_(JOB_COMPLETE)     /* Job completion task. */ \
  macro_(INTERPRET)        /* Interpreter time (in job and not render). */ \
  macro_(INTERPRET_LEVEL)  /* Time in interpreter levels. */ \
  macro_(INTERPRET_PDF)    /* Time executing PDF. */ \
  macro_(INTERPRET_XML)    /* Time interpreting XML. */ \
  macro_(INTERPRET_HPGL2)  /* HPGL2 interpretation time. */ \
  macro_(INTERPRET_PCL5)   /* Time executing PCL5 commands. */ \
  macro_(INTERPRET_PCL5_IMAGE) /* Time in PCL 5 image reading. */ \
  macro_(INTERPRET_PCL5_FONT) /* Time in PCL 5 font downloading. */ \
  macro_(INTERPRET_PCLXL)  /* Time executing PCLXL commands. */ \
  macro_(INTERPRET_PCLXL_IMAGE) /* Time in PCL XL image reading. */ \
  macro_(INTERPRET_PCLXL_FONT) /* Time in PCL XL font downloading. */ \
  macro_(INTERPRET_IMAGE)  /* Time spent in image interpretation */ \
  macro_(INTERPRET_JPEG)   /* Time spent in JPEG interpretation */ \
  macro_(INTERPRET_TOMSTABLE) /* Interpretation time in Toms Table code */  \
  macro_(FONT_CACHE)       /* Time building font caches. */ \
  macro_(FONT_PFIN)        /* Time spent in PFIN modules. */ \
  macro_(USERPATH_CACHE)   /* Time building userpath caches. */ \
  macro_(DL)               /* Display list operations. */ \
  macro_(DL_HDL)           /* Time building DL hierarchical grouping. */ \
  macro_(DL_GROUP)         /* Time building DL transparency grouping. */ \
  macro_(DL_FILL)          /* Time building DL fill object. */ \
  macro_(DL_IMAGE)         /* Time building DL image object. */ \
  macro_(DL_SHFILL)        /* Time building DL shfill objects. */ \
  macro_(DL_OBJECT)        /* Marks for adding DL objects. */ \
  macro_(DL_COMPLETE)      /* Post-interpret DL collection task. */ \
  macro_(DL_ERASE)         /* DL erase task. */ \
  macro_(COMPOSITE)        /* Compositing time. */ \
  macro_(COMPOSITE_BAND)   /* Per-band compositing time. */ \
  macro_(COMPOSITE_OBJECT) /* Per-object compositing time. */ \
  macro_(COMPOSITE_ACQUIRE) /* Time waiting for compositing mutex. */ \
  macro_(COMPOSITE_HOLD)   /* Time holding compositing mutex. */ \
  macro_(RENDER)           /* Rendering time. */ \
  macro_(RENDER_INIT)      /* Render initialisation time. */ \
  macro_(RENDER_INIT_FINAL) /* Render end of initialisation time. */ \
  macro_(RENDER_INIT_PARTIAL) /* Render end of initialisation time. */ \
  macro_(DL_PREPARE)       /* DL prepare time. */ \
  macro_(DL_PRECONVERT)    /* DL colors preconversion time. */ \
  macro_(TRAP_INIT)        /* Trapping initialisation time. */ \
  macro_(TRAP_ACQUIRE)     /* Time waiting on trapping mutex. */ \
  macro_(TRAP_HOLD)        /* Time holding trapping mutex. */ \
  macro_(TRAP)             /* Trapping time. */ \
  macro_(SHEET_START)      /* Per-sheet render and output start. */ \
  macro_(SHEET_RENDER_DONE) /* Per-sheet render completion. */ \
  macro_(SHEET_OUTPUT_DONE) /* Per-sheet output completion. */ \
  macro_(SHEET_DONE)       /* Per-sheet completion. */ \
  macro_(RENDER_FRAME_START) /* Per-frame render start. */ \
  macro_(RENDER_FRAME_DONE) /* Per-frame render completion. */ \
  macro_(RENDER_BAND)      /* Per-band rendering time. */ \
  macro_(RENDER_HDL)       /* Per-hdl rendering time. */ \
  macro_(RENDER_GROUP)     /* Per-group rendering time. */ \
  macro_(RENDER_PATTERN)   /* Pattern sub-DL rendering time. */ \
  macro_(RENDER_OBJECT)    /* Per-object (top-level) rendering time. */ \
  macro_(RENDER_CLIP)      /* Preparing clipping for object. */ \
  macro_(RENDER_SOFTMASK)  /* Preparing softmask for object. */ \
  macro_(RENDER_VECTOR)    /* Time spent rendering a vector object */ \
  macro_(RENDER_TEXT)      /* Time spent rendering a text object */ \
  macro_(RENDER_SHADE)     /* Time spent rendering a shade object */ \
  macro_(RENDER_IMAGE)     /* Time spent rendering an image object */ \
  macro_(RENDER_BACKDROP)  /* Time spent rendering a backdrop object */ \
  macro_(RENDER_ERASE)     /* Time spent rendering an erase object */ \
  macro_(COMPRESS_BAND)    /* Per-band compression time. */ \
  macro_(OUTPUT_BAND)      /* Per-band output time. */ \
  macro_(READBACK_BAND)    /* Read band band from PGB device. */ \
  macro_(RETAINED_RASTER_CAPTURE) /* Time capturing retained raster. */ \
  macro_(TICKLE)           /* Tickle start/end. */ \
  macro_(PAGEBUFFER)       /* Pagebuffer open/close. */ \
  macro_(PAGEBUFFER_ACQUIRE) /* Pagebuffer mutex acquire. */ \
  macro_(PAGEBUFFER_HOLD)  /* Pagebuffer mutex hold. */ \
  macro_(TASK_JOINING)     /* Time spent in join loop. */ \
  macro_(TASK_HELPING)     /* Time spent helping other tasks. */ \
  macro_(TASK_JOIN_WAIT)   /* Time spent waiting for a task in join. */ \
  macro_(TASK_DISPATCH_WAIT) /* Time spent waiting in task dispatch loops. */ \
  macro_(TASK_HELPER_WAIT) /* Time spent waiting in task helper. */ \
  macro_(TASK_MEMORY_WAIT) /* Time spent waiting for resource deprovision. */ \
  macro_(THREAD_WAKE)      /* Waking another thread. */ \
  macro_(THREAD_WAKE_FAIL) /* Why thread wake failed. */ \
  macro_(TASK_ACQUIRE)     /* Task system lock acquire. */ \
  macro_(TASK_HOLD)        /* Task system lock hold. */ \
  macro_(TASKS_INCOMPLETE) /* Number of tasks not completed. */ \
  macro_(TASKS_COMPLETE)   /* Number of tasks done but not deallocated. */ \
  macro_(TASK_GROUPS_INCOMPLETE) /* Number of task groups not completed. */ \
  macro_(TASK_GROUPS_COMPLETE) /* Number of task groups done but not deallocated. */ \
  macro_(TASK_GROUP_RESOURCES) /* Time with task resources. */ \
  macro_(TASK_GRAPH_WALK)  /* Time walking the task graph. */ \
  macro_(THREADS_ACTIVE)   /* Threads doing useful work. */ \
  macro_(TASKS_ACTIVE)     /* Number of tasks running. */ \
  macro_(TASKS_RUNNABLE)   /* Number of tasks ready to run. */ \
  macro_(TASKS_UNPROVISIONED) /* Number of tasks ready but unprovisioned. */ \
  macro_(LINES_COPIED)     /* PGB timeline progress event received. */ \
  macro_(USER)             /* Scriptable user control. */ \
  macro_(PDF_PAGE)         /* Interpreting a PDF page. */ \
  macro_(XPS_PAGE)         /* Interpreting an XPS page. */ \
  macro_(HANDLING_LOWMEM)  /* Handling low memory. */ \
  macro_(LOWMEM_WAIT)      /* Condvar wait in low memory handler. */ \
  macro_(LOWMEM_ACQUIRE)   /* Low memory mutex acquire. */ \
  macro_(LOWMEM_HOLD)      /* Low memory mutex hold. */ \
  macro_(RESERVE_ACQUIRE)  /* Reserve mutex acquire. */ \
  macro_(RESERVE_HOLD)     /* Reserve mutex hold. */ \
  macro_(IM_GET_WAIT)      /* Condvar wait for image blocks. */ \
  macro_(IM_LOAD_WAIT)     /* Condvar wait for image load from disk. */ \
  macro_(IM_BLOCK_ACQUIRE) /* Image block mutex acquire. */ \
  macro_(IM_BLOCK_HOLD)    /* Image block mutex hold. */ \
  macro_(IM_STORE_ACQUIRE) /* Image store mutex acquire. */ \
  macro_(IM_STORE_HOLD)    /* Image store mutex hold. */ \
  macro_(RSD_ACQUIRE)      /* RSD mutex acquire. */ \
  macro_(RSD_HOLD)         /* RSD mutex hold. */ \
  macro_(HT_CACHE_ACQUIRE) /* Halftone cache mutex acquire. */ \
  macro_(HT_CACHE_HOLD)    /* Halftone cache mutex hold. */ \
  macro_(MONITOR_ACQUIRE)  /* Monitor mutex acquire. */ \
  macro_(MONITOR_HOLD)     /* Monitor mutex hold. */ \
  macro_(ASYNC_PS_ACQUIRE) /* Async PS mutex acquire. */ \
  macro_(ASYNC_PS_HOLD)    /* Async PS mutex hold. */ \
  macro_(COLCVT_ACQUIRE)   /* Color convert PS mutex acquire. */ \
  macro_(COLCVT_HOLD)      /* Color convert mutex hold. */ \
  macro_(INPUT_PAGE_ACQUIRE) /* Input page mutex acquire. */ \
  macro_(INPUT_PAGE_HOLD)  /* Input page mutex hold. */ \
  macro_(OUTPUT_PAGE_ACQUIRE) /* Output page mutex acquire. */ \
  macro_(OUTPUT_PAGE_HOLD) /* Output page mutex hold. */ \
  macro_(HT_ACQUIRE)       /* Halftone read-write lock acquire. */ \
  macro_(HT_READ_HOLD)     /* Halftone read-write read lock hold. */ \
  macro_(HT_WRITE_HOLD)    /* Halftone read-write read lock hold. */ \
  macro_(HT_FORM_ACQUIRE)  /* Halftone form class acquire. */ \
  macro_(HT_FORM_HOLD)     /* Halftone form class hold. */ \
  macro_(NFILL_ACQUIRE)    /* NFILL read-write lock acquire. */ \
  macro_(NFILL_READ_HOLD)  /* NFILL read-write read lock hold. */ \
  macro_(NFILL_WRITE_HOLD) /* NFILL read-write read lock hold. */ \
  macro_(GOURAUD_ACQUIRE)  /* Gouraud read-write lock acquire. */ \
  macro_(GOURAUD_READ_HOLD) /* Gouraud read-write read lock hold. */ \
  macro_(GOURAUD_WRITE_HOLD) /* Gouraud read-write read lock hold. */ \
  macro_(POINTLESS_WAKEUPS) /* Wakeups with nothing to do in task system. */ \
  macro_(POINTLESS_TIMEOUTS) /* Timeouts with nothing to do in task system. */ \
  macro_(POINTLESS_GRAPH_WALKS) /* Task graph walks finding nothing. */ \
  macro_(RETAINEDRASTER_ACQUIRE) /* Retained Raster mutex acquire. */ \
  macro_(RETAINEDRASTER_HOLD) /* Retained Raster mutex hold. */ \
  macro_(RETAINEDRASTER_WAIT) /* Retained Raster mutex wait. */ \
  macro_(IRR_HOLD)            /* Internal Retained Raster mutex hold. */ \
  macro_(IRR_ACQUIRE)         /* Internal Retained Raster mutex acquire. */ \
  macro_(GST_HOLD)            /* GST mutex hold. */ \
  macro_(GST_ACQUIRE)         /* GST mutex acquire. */ \
  macro_(RR_PAGE_DEFINE)            /* Retained Raster define event */ \
  macro_(RR_PAGE_READY)             /* Retained Raster ready event */ \
  macro_(RR_PAGE_COMPLETE)          /* Retained Raster complete event */ \
  macro_(RR_ELEMENT_DEFINE)         /* Retained Raster define event */ \
  macro_(RR_ELEMENT_LOCK)           /* Retained Raster lock event */ \
  macro_(RR_ELEMENT_UNLOCK)         /* Retained Raster unlock event */ \
  macro_(RR_ELEMENT_PENDING)        /* Retained Raster pending event */ \
  macro_(RR_ELEMENT_QUERY)          /* Retained Raster query event */ \
  macro_(RR_ELEMENT_UPDATE_RASTER)  /* Retained Raster update raster event */ \
  macro_(RR_ELEMENT_UPDATE_HITS)    /* Retained Raster update hits event */ \
  macro_(RR_CONNECT)                /* Retained Raster connect event */ \
  macro_(RR_DISCONNECT)             /* Retained Raster disconnect event */ \
  macro_(RR_PREOP)                  /* Retained Raster pre operator */ \
  macro_(RR_HASHOP)                 /* Retained Raster operator hashing */ \
  macro_(RR_POSTOP)                 /* Retained Raster post operator */ \
  macro_(RESOURCE_FIX)              /* Task resource fix */ \
  macro_(RESOURCE_UNFIX)            /* Task resource unfix */ \
  macro_(RESOURCE_DETACH)           /* Task resource detach */ \
  macro_(RESOURCE_RETURN)           /* Task resource return to pool */ \
  macro_(RESOURCE_FREE)             /* Task resource freed from provision */ \
  macro_(TASK_GROUP_PROVISION)      /* Task group provision results */ \
  macro_(MPS_COMMITTED)             /* Memory committed by MPS */ \
  macro_(MHT_GATE)          /* Delay rendering of band for MHT latency. */ \
  macro_(MHT_WAIT)          /* Condvar wait for async MHT. */ \
  macro_(MHT_ACQUIRE)       /* Async MHT mutex acquire. */ \
  macro_(MHT_HOLD)          /* Async MHT mutex hold. */ \
  macro_(RENDER_SPLIT_Y)    /* Split band rendering. */ \
  macro_(RENDER_SPLIT_X)    /* Split band rendering. */ \
  macro_(RECOMPUTE_SCHEDULE) /* Time spent recomputing the task schedule. */ \

/** \brief Convert a raw symbol to a trace enumeration name. */
#define SW_TRACENAME_ENUM(x) SW_TRACE_ ## x,

/** \brief Core trace name enumeration.
 *
 * Trace name values can be used in the \c SWSTART::value::int_value field of
 * \c SWTraceEnableTag tags passed to \c SwStart() if
 * SW_TRACEOPTION_INDIVIDUAL is added to the group value. Individual trace
 * names may be disabled by adding SW_TRACEOPTION_DISABLE to the \c
 * SWSTART::value::int_value field.
 */
enum {
  SW_TRACE_INVALID,    /**< Base value for core trace names, usable as an out
                          of band init value. This MUST be the first enum
                          value. */
  SW_TRACENAMES(SW_TRACENAME_ENUM)
  CORE_TRACE_N         /**< Starting point for skin trace identifiers. This
                          MUST be the last enum value. */
} ;

/** \brief Core trace types.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having core
 * maintainers having to modify every skin which uses the trace facility.
 */
#define SW_TRACETYPES(macro_) \
  macro_(RESET)      /* Reset this event. */ \
  macro_(ENABLE)     /* Enable this trace name. */ \
  macro_(DISABLE)    /* Disable this trace name. */ \
  macro_(ENTER)      /* Entering a module. */ \
  macro_(EXIT)       /* Exiting a module. */ \
  macro_(MARK)       /* Mark that some event has happened. */ \
  macro_(AMOUNT)     /* Set a continuously available value. */ \
  macro_(ADD)        /* Add to a continuously available value. */ \
  macro_(VALUE)      /* Set a point value. */ \
  macro_(OPTION)     /* Meta-data for trace name. */

/** \brief Convert a raw symbol to a trace type enumeration name. */
#define SW_TRACETYPE_ENUM(x) SW_TRACETYPE_ ## x,

/** \brief Core trace types enum. */
enum {
  SW_TRACETYPE_INVALID,    /**< Base value for core trace types. This
                                MUST be the first enum value. */
  SW_TRACETYPES(SW_TRACETYPE_ENUM)
  CORE_TRACETYPE_N         /**< Starting point for skin trace types. This
                              MUST be the last enum value. */
} ;

/** \brief Core trace option enums. This is not macro-ised, because the
    values are not stringified; they are used in the designator of a probe. */
enum {
  SW_TRACEOPTION_INVALID,        /**< Unused option base */
  SW_TRACEOPTION_AFFINITY = 1,   /**< Treat all instances as happening on one thread. */
  SW_TRACEOPTION_MARKDATA = 2,   /**< MARK type data should always be shown. */
  SW_TRACEOPTION_AMOUNTDATA = 4, /**< AMOUNT type data should always be shown. */
  SW_TRACEOPTION_TIMELINE = 8,   /**< Enter/exit match on designator rather than thread. */
  CORE_TRACEOPTION_NEXT = 16     /**< Next bit after last trace option */
} ;

/** \brief Structure definition for probe control groups.

    The core defines some standard probe control groups, however it is the
    responsibility of the skin to determine whether and how it will use these
    groups. */
typedef struct sw_tracegroup_t {
  const char *option ;          /**< Option name. */
  const char *usage ;           /**< Usage message. */
  const int ids[CORE_TRACE_N] ; /**< Trace names, terminated by SW_TRACE_INVALID. */
} sw_tracegroup_t ;

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWTRACE_H__ */
