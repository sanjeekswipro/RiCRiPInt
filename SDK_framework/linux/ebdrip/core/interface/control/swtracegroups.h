/** \file
 * \ingroup swtrace
 *
 * $HopeName: COREinterface_control!swtracegroups.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief This header file defines the default core trace groups, for the
 * lightweight tracing API.
 *
 * The lightweight tracing API is used to extract fine-grained timing
 * information from the RIP.
 *
 * This header file is meant to be included in-line inside a definition of
 * a sw_tracegroup_t array. As such, it does not have any guards, and does not
 * include any other headers, not even swtrace.h. swtrace.h must be included
 * before this file.
 */

#if 0 /* Use as follows: */
/* This ifdeffed-out definition helps code formatters keep the indentation
   straight, please leave it in place. */
const static sw_tracegroup_t trace_groups[] = {
#endif
  { "user", "User group, for profile control:",
    {
      SW_TRACE_CORE, SW_TRACE_THREAD, SW_TRACE_USER, SW_TRACE_INVALID
    }
  },
  { "basic", "Basic group, for functional overview:",
    {
      SW_TRACE_JOB, SW_TRACE_JOB_CONFIG, SW_TRACE_INTERPRET,
      SW_TRACE_PDF_PAGE, SW_TRACE_XPS_PAGE,
      SW_TRACE_INTERPRET_PDF, SW_TRACE_INTERPRET_XML,
      SW_TRACE_INTERPRET_PCL5, SW_TRACE_INTERPRET_PCLXL,
      SW_TRACE_COMPOSITE, SW_TRACE_RENDER, SW_TRACE_PROBE, SW_TRACE_INVALID
    }
  },
  { "job", "Job group, for tracking job timelines:",
    {
      SW_TRACE_JOB_STREAM_TL, SW_TRACE_JOB_TL,
      SW_TRACE_INTERPRET_PAGE_TL, SW_TRACE_RENDER_PAGE_TL,
      SW_TRACE_INVALID
    }
  },
  { "interpret", "Interpret group, for tracking interpreter detail:",
    {
      SW_TRACE_JOB, SW_TRACE_JOB_CONFIG,
      SW_TRACE_INTERPRET, SW_TRACE_INTERPRET_LEVEL,
      SW_TRACE_INTERPRET_PDF, SW_TRACE_PDF_PAGE,
      SW_TRACE_INTERPRET_XML, SW_TRACE_XPS_PAGE,
      SW_TRACE_INTERPRET_PCL5,
      SW_TRACE_INTERPRET_PCL5_IMAGE, SW_TRACE_INTERPRET_PCL5_FONT,
      SW_TRACE_INTERPRET_PCLXL,
      SW_TRACE_INTERPRET_PCLXL_IMAGE, SW_TRACE_INTERPRET_PCLXL_FONT,
      SW_TRACE_INTERPRET_HPGL2,
      SW_TRACE_FONT_CACHE, SW_TRACE_FONT_PFIN, SW_TRACE_USERPATH_CACHE,
      SW_TRACE_INTERPRET_IMAGE, SW_TRACE_INTERPRET_JPEG,
      SW_TRACE_INTERPRET_TOMSTABLE,
      SW_TRACE_PROBE, SW_TRACE_INVALID
    }
  },
  { "render", "Render group, for compositing, rendering and output detail:",
    {
      SW_TRACE_COMPOSITE, SW_TRACE_COMPOSITE_BAND, SW_TRACE_RENDER,
      SW_TRACE_RENDER_INIT, SW_TRACE_RENDER_INIT_FINAL,
      SW_TRACE_RENDER_INIT_PARTIAL,
      SW_TRACE_DL_PREPARE, SW_TRACE_DL_PRECONVERT,
      SW_TRACE_TRAP_INIT, SW_TRACE_TRAP,
      SW_TRACE_RENDER_BAND, SW_TRACE_COMPRESS_BAND, SW_TRACE_OUTPUT_BAND,
      SW_TRACE_RENDER_SPLIT_X, SW_TRACE_RENDER_SPLIT_Y,
      SW_TRACE_MHT_GATE,
      SW_TRACE_RETAINED_RASTER_CAPTURE,
      SW_TRACE_PROBE, SW_TRACE_INVALID
    }
  },
  { "dl", "Display list group, for adding, constructing, and purging DLs:",
    {
      SW_TRACE_DL, SW_TRACE_DL_HDL, SW_TRACE_DL_GROUP, SW_TRACE_DL_FILL,
      SW_TRACE_DL_IMAGE, SW_TRACE_DL_SHFILL,
      SW_TRACE_DL_OBJECT, SW_TRACE_PROBE, SW_TRACE_DL_COMPLETE,
      SW_TRACE_DL_ERASE,
      SW_TRACE_INVALID
    }
  },
  { "pdfrr", "PDF retained raster group, for tracking retained raster:",
    {
      SW_TRACE_JOB_TL, SW_TRACE_INTERPRET_PAGE_TL, SW_TRACE_RENDER_PAGE_TL,
      SW_TRACE_INTERPRET_PDF, SW_TRACE_PDF_PAGE,
      SW_TRACE_RR_SCANNING_TL, SW_TRACE_PGB_TL,
      SW_TRACE_RETAINED_RASTER_CAPTURE,
      SW_TRACE_INVALID
    }
  },
  { "object", "Object group, for tracking individual DL objects:",
    {
      SW_TRACE_DL_HDL, SW_TRACE_DL_GROUP, SW_TRACE_DL_OBJECT,
      SW_TRACE_COMPOSITE_OBJECT,
      SW_TRACE_RENDER_HDL, SW_TRACE_RENDER_GROUP, SW_TRACE_RENDER_PATTERN,
      SW_TRACE_RENDER_OBJECT, SW_TRACE_RENDER_CLIP, SW_TRACE_RENDER_SOFTMASK,
      SW_TRACE_RENDER_VECTOR, SW_TRACE_RENDER_TEXT,
      SW_TRACE_RENDER_SHADE, SW_TRACE_RENDER_IMAGE,
      SW_TRACE_RENDER_BACKDROP, SW_TRACE_RENDER_ERASE,
      SW_TRACE_PROBE,
      SW_TRACE_INVALID
    }
  },
  { "multi", "Multithreading group, for threading control:",
    {
      SW_TRACE_THREAD, SW_TRACE_THREAD_CREATE, SW_TRACE_THREAD_JOIN,
      SW_TRACE_TASK_JOINING, SW_TRACE_TASK_JOIN_WAIT,
      SW_TRACE_TASK_DISPATCH_WAIT, SW_TRACE_TASK_HELPER_WAIT,
      SW_TRACE_TASK_MEMORY_WAIT, SW_TRACE_TASK_HELPING,
      SW_TRACE_THREAD_WAKE, SW_TRACE_THREAD_WAKE_FAIL,
      SW_TRACE_INVALID
    }
  },
  { "task", "Task group, for task-level tracing:",
    {
      SW_TRACE_DL_COMPLETE, SW_TRACE_DL_ERASE, SW_TRACE_RENDER,
      SW_TRACE_COMPOSITE_BAND, SW_TRACE_RENDER_BAND, SW_TRACE_MHT_GATE,
      SW_TRACE_COMPRESS_BAND, SW_TRACE_OUTPUT_BAND, SW_TRACE_READBACK_BAND,
      SW_TRACE_JOB_COMPLETE,
      SW_TRACE_TASK_HELPING, SW_TRACE_TASK_HELPER_WAIT,
      SW_TRACE_RENDER_FRAME_START, SW_TRACE_RENDER_FRAME_DONE,
      SW_TRACE_SHEET_START, SW_TRACE_SHEET_DONE,
      SW_TRACE_SHEET_RENDER_DONE, SW_TRACE_SHEET_OUTPUT_DONE,
      SW_TRACE_TASK_JOINING, SW_TRACE_TASK_JOIN_WAIT,
      SW_TRACE_TASKS_INCOMPLETE, SW_TRACE_TASK_GROUPS_INCOMPLETE,
      SW_TRACE_TASKS_COMPLETE, SW_TRACE_TASK_GROUPS_COMPLETE,
      SW_TRACE_THREADS_ACTIVE, SW_TRACE_TASKS_ACTIVE, SW_TRACE_TASKS_RUNNABLE,
      SW_TRACE_TASKS_UNPROVISIONED,
      SW_TRACE_TASK_GROUP_RESOURCES, SW_TRACE_TASK_GRAPH_WALK,
      SW_TRACE_POINTLESS_GRAPH_WALKS, SW_TRACE_POINTLESS_TIMEOUTS,
      SW_TRACE_POINTLESS_WAKEUPS, SW_TRACE_TASK_GROUP_PROVISION,
      SW_TRACE_RECOMPUTE_SCHEDULE,
      SW_TRACE_INVALID
    }
  },
  { "band", "Band group, for band cycle control:",
    {
      SW_TRACE_COMPOSITE_BAND, SW_TRACE_RENDER_BAND, SW_TRACE_MHT_GATE,
      SW_TRACE_COMPRESS_BAND, SW_TRACE_OUTPUT_BAND, SW_TRACE_READBACK_BAND,
      SW_TRACE_RENDER_FRAME_START, SW_TRACE_RENDER_FRAME_DONE,
      SW_TRACE_SHEET_START, SW_TRACE_SHEET_DONE,
      SW_TRACE_SHEET_RENDER_DONE, SW_TRACE_SHEET_OUTPUT_DONE,
      SW_TRACE_LINES_COPIED,
      SW_TRACE_INVALID
    }
  },
  { "output", "Output group, for band output control:",
    {
      SW_TRACE_OUTPUT_BAND,
      SW_TRACE_SHEET_START, SW_TRACE_SHEET_OUTPUT_DONE, SW_TRACE_SHEET_DONE,
      SW_TRACE_LINES_COPIED,
      SW_TRACE_RETAINED_RASTER_CAPTURE,
      SW_TRACE_PAGEBUFFER,
      SW_TRACE_INVALID
    }
  },
  { "tickle", "Tickle group, for tickle control:",
    {
      SW_TRACE_TICKLE,
      SW_TRACE_INVALID
    }
  },
  { "trap", "Trap group, for trapping control:",
    {
      SW_TRACE_TRAP_PREPARATION_TL,
      SW_TRACE_TRAP_GENERATION_TL,
      SW_TRACE_INVALID
    }
  },
  { "model", "RIP modelling group:",
    {
      SW_TRACE_JOB,
      SW_TRACE_INTERPRET,
      SW_TRACE_DL_IMAGE, SW_TRACE_DL_SHFILL, SW_TRACE_DL_FILL,
      SW_TRACE_DL_PREPARE, SW_TRACE_DL_PRECONVERT,
      SW_TRACE_RENDER, SW_TRACE_RENDER_INIT, SW_TRACE_RENDER_INIT_FINAL,
      SW_TRACE_RENDER_BAND,
      SW_TRACE_COMPOSITE, SW_TRACE_COMPOSITE_BAND,
      SW_TRACE_TRAP_INIT, SW_TRACE_TRAP,
      SW_TRACE_COMPRESS_BAND,
      SW_TRACE_OUTPUT_BAND,
      SW_TRACE_RETAINED_RASTER_CAPTURE,
      SW_TRACE_INVALID
    }
  },
  { "timeline", "Timeline group, for timeline lifecycles:",
    {
      SW_TRACE_TIMELINE,
      SW_TRACE_INVALID
    }
  },
  { "progress", "Timeline progress, for timeline progress updates:",
    {
      SW_TRACE_FILE_PROGRESS, SW_TRACE_JOB_TL, SW_TRACE_PGB_TL,
      SW_TRACE_INVALID
    }
  },
  { "acquire", "Mutex and read-write lock acquires:",
    {
      SW_TRACE_COMPOSITE_ACQUIRE, SW_TRACE_TRAP_ACQUIRE,
      SW_TRACE_PAGEBUFFER_ACQUIRE,
      SW_TRACE_TASK_ACQUIRE, SW_TRACE_LOWMEM_ACQUIRE,
      SW_TRACE_RESERVE_ACQUIRE, SW_TRACE_IM_BLOCK_ACQUIRE,
      SW_TRACE_IM_STORE_ACQUIRE, SW_TRACE_RSD_ACQUIRE,
      SW_TRACE_HT_CACHE_ACQUIRE, SW_TRACE_MONITOR_ACQUIRE,
      SW_TRACE_ASYNC_PS_ACQUIRE, SW_TRACE_COLCVT_ACQUIRE,
      SW_TRACE_INPUT_PAGE_ACQUIRE, SW_TRACE_OUTPUT_PAGE_ACQUIRE,
      SW_TRACE_HT_ACQUIRE, SW_TRACE_NFILL_ACQUIRE,
      SW_TRACE_GOURAUD_ACQUIRE, SW_TRACE_RETAINEDRASTER_ACQUIRE,
      SW_TRACE_IRR_ACQUIRE, SW_TRACE_GST_ACQUIRE,
      SW_TRACE_INVALID
    }
  },
  { "hold", "Mutex and read-write lock holds:",
    {
      SW_TRACE_COMPOSITE_HOLD, SW_TRACE_TRAP_HOLD,
      SW_TRACE_PAGEBUFFER_HOLD,
      SW_TRACE_TASK_HOLD, SW_TRACE_LOWMEM_HOLD,
      SW_TRACE_RESERVE_HOLD, SW_TRACE_IM_BLOCK_HOLD,
      SW_TRACE_IM_STORE_HOLD, SW_TRACE_RSD_HOLD,
      SW_TRACE_HT_CACHE_HOLD, SW_TRACE_MONITOR_HOLD,
      SW_TRACE_ASYNC_PS_HOLD, SW_TRACE_COLCVT_HOLD,
      SW_TRACE_INPUT_PAGE_HOLD, SW_TRACE_OUTPUT_PAGE_HOLD,
      SW_TRACE_HT_READ_HOLD, SW_TRACE_HT_WRITE_HOLD,
      SW_TRACE_NFILL_READ_HOLD, SW_TRACE_NFILL_WRITE_HOLD,
      SW_TRACE_GOURAUD_READ_HOLD, SW_TRACE_GOURAUD_WRITE_HOLD,
      SW_TRACE_RETAINEDRASTER_HOLD, SW_TRACE_IRR_HOLD, SW_TRACE_GST_HOLD,
      SW_TRACE_INVALID
    }
  },
  { "wait", "Condition variable waits:",
    {
      SW_TRACE_LOWMEM_WAIT, SW_TRACE_TASK_DISPATCH_WAIT,
      SW_TRACE_TASK_HELPER_WAIT, SW_TRACE_TASK_MEMORY_WAIT,
      SW_TRACE_TASK_JOIN_WAIT,
      SW_TRACE_IM_LOAD_WAIT, SW_TRACE_IM_GET_WAIT,
      SW_TRACE_RETAINEDRASTER_WAIT,
      SW_TRACE_INVALID
    }
  },
  { "resource", "Resource group, for task resource tracing:",
    {
      SW_TRACE_RESOURCE_FIX, SW_TRACE_RESOURCE_UNFIX,
      SW_TRACE_RESOURCE_DETACH, SW_TRACE_RESOURCE_RETURN,
      SW_TRACE_RESOURCE_FREE, SW_TRACE_TASK_GROUP_PROVISION,
      SW_TRACE_INVALID
    }
  },
  { "memory", "Memory group, for memory tracing:",
    {
      SW_TRACE_HANDLING_LOWMEM, SW_TRACE_MPS_COMMITTED, SW_TRACE_INVALID
    }
  },
  /* Last element should have trailing comma, for skin groups that follow. */
#if 0 /* Use as previous */
} ;
#endif

