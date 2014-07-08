/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!src:glue.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Glue functions to integrate core RIP modules together. Most of these
 * functions are simple trampolines, which satisfy dependencies in one module
 * by calling a function in another module. It is better to satisfy the
 * dependency here, rather than by providing a direct definition in another
 * compound, because multiple compounds may ways to satisfy dependencies,
 * which can be joined together by these functions.
 */


#include <stdarg.h>
#include <string.h>

/* SWcore_interface includes */
#include "core.h"
#include "swcopyf.h"  /* vswcopyf */
#include "swdevice.h"
#include "swstart.h" /* SwThreadIndex */
#include "swtrace.h" /* SW_TRACE_INVALID */
#include "coreinit.h"
#include "swtimelines.h" /* EVENT_TIMELINE_DEBUG_NAME */

/* SWmm_common includes */
#include "mmcompat.h" /* mm_alloc_static */

/* COREobjects includes */
#include "objects.h" /* OBJECT */

/* COREdevices includes */
#include "devices.h" /* device_closing() */

/* COREfileio includes */
#include "fileio.h"  /* fileio_check_device() */

#include "params.h"   /* SystemParams */
#include "psvm.h"     /* workingsave */
#include "miscops.h"  /* in_super_exec */
#include "std_file.h" /* std_files */
#include "chartype.h" /* LF */
#include "filters.h"  /* ps_filter_preflight */
#include "fileops.h"  /* ps_file_standard */

/* SWpdf includes */
#include "swpdfin.h"  /* pdf_X1_filter_preflight */

/* SWcore includes */
#include "debugging.h"
#include "monitor.h"
#include "monitori.h"
#include "corejob.h"  /* corejob_t */
#include "dlstate.h"  /* DL_STATE */

/* HQNc-standard includes */
#include "hqmemcpy.h"

/* SWmulti includes */
#include "mlock.h" /* multi_mutex_lock */

#include "swerrors.h" /* FAILURE() */

#ifdef DEBUG_BUILD
static void debug_register_tl_names(void) ;
static void debug_deregister_tl_names(void) ;

/* This mutex protects the debug output buffer */
static multi_mutex_t dmonitor_mutex ;
#else
#define debug_register_tl_names() EMPTY_STATEMENT()
#define debug_deregister_tl_names() EMPTY_STATEMENT()
#endif

/* This mutex is used to avoid writing to the monitor device concurrently.
   The devices are not required to be threadsafe, as they were only originally
   written to by the interpreter thread. This is no longer the case, so a mutex
   is required. However, the Message Event itself is not mutexed - a skin that
   overrides the mon_monitor default handler must therefore be written in a
   threadsafe manner. */
static multi_mutex_t monitor_mutex ;

/* Monitor device and file descriptor */
static DEVICELIST * monitordev = NULL ;
static DEVICE_FILEDESCRIPTOR monitorfile = -1 ;

/* -------------------------------------------------------------------------- */
/** The Monitor Event Default Handler - outputs via %monitor% which shall always
    be available. This is registered below all other channel handlers. */

static sw_event_result HQNCALL mon_monitor(void * context, sw_event * ev)
{
  SWMSG_MONITOR * mon = ev->message ;
  UNUSED_PARAM(void *, context) ;

  if (mon == 0 || ev->length < sizeof(SWMSG_MONITOR)) /* Weird event */
    return SW_EVENT_CONTINUE ;

  /* Open the monitor device if we haven't already */
  if (monitordev == NULL) {
    monitordev = SwFindDevice((uint8*)"monitor") ;
    if (monitordev == NULL)
      monitordev = SwFindDevice((uint8*)"console") ;
    if (monitordev == NULL) {
      /* We're completely stuck - no way to output anything */
      return FAILURE(SW_EVENT_ERROR) ;
    }

    monitorfile = (*theIOpenFile(monitordev))(
                     monitordev, (uint8*)"", SW_WRONLY | SW_CREAT) ;
    if (monitorfile < 0) {
      /* We're completely stuck - no way to output anything */
      monitordev = NULL ;
      return FAILURE(SW_EVENT_ERROR) ;
    }
  }

  /* We are the default Handler - output anything to monitor device */
  multi_mutex_lock(&monitor_mutex) ;
  (void) (*theIWriteFile(monitordev))(monitordev, monitorfile,
                                      mon->text, (int32)mon->length) ;
  multi_mutex_unlock(&monitor_mutex) ;

  return SW_EVENT_HANDLED ;
}

static sw_event_handler mon_handler = {mon_monitor} ;

/* -------------------------------------------------------------------------- */

Bool monitor_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  /* When the mutex code gets refactored to handle error conditions
     appropriately, there will be an opportunity to return FALSE. */
  multi_mutex_init(&monitor_mutex, MONITOR_LOCK_INDEX, FALSE,
                   SW_TRACE_MONITOR_ACQUIRE, SW_TRACE_MONITOR_HOLD);
#ifdef DEBUG_BUILD
  multi_mutex_init(&dmonitor_mutex, MONITOR_LOCK_INDEX, FALSE,
                   SW_TRACE_MONITOR_ACQUIRE, SW_TRACE_MONITOR_HOLD);
#endif

  monitordev = NULL ;
  /* Register the monitor handler for the Monitor Channel */
  (void)SwRegisterHandler(SWEVT_MONITOR, &mon_handler, SW_EVENT_DEFAULT) ;

  debug_register_tl_names() ;
  return TRUE ;
}


void monitor_finish(void)
{
  debug_deregister_tl_names() ;

  (void)SwSafeDeregisterHandler(SWEVT_MONITOR, &mon_handler) ;
  /* All devices have gone by now, so can't close it...
    if (monitordev) {
      (void)(*theICloseFile(monitordev))(monitordev, monitorfile) ;
      monitordev = NULL ;
    }
  */
#ifdef DEBUG_BUILD
  multi_mutex_finish(&dmonitor_mutex);
#endif
  multi_mutex_finish(&monitor_mutex);
}

/* -------------------------------------------------------------------------- */

/* Satisfy dependency from object subsystem for routine to determine if a
   dictionary can be extended by fast_insert_hash et. al. */
Bool object_extend_dict(const corecontext_t *context,
                        const OBJECT *theo, Bool PSmem_allocator)
{
  return ( /* Dictionary and allocator must match */
           !NOTVMOBJECT(*theo) == PSmem_allocator &&
           /* PSVM dictionary expansion only if lang level allows */
           (NOTVMOBJECT(*theo) ||
            theISaveLangLevel(workingsave) != 1 ||
            context->systemparams->Level1ExpandDict) );
}

/* Satisfy dependency from object subsystem for routine to determine if the
   access permissions on an object can be overridden. */
Bool object_access_override(const OBJECT *theo)
{
  UNUSED_PARAM(const OBJECT *, theo) ;

  return in_super_exec() ;
}

/* ========================================================================== */
/* Routines to send printf-like messages to logfile and monitor window */

/* Issue a monitor event */
static void swmonitor(sw_tl_ref tl, sw_mon_channel channel, sw_mon_type type,
                      uint8* msg, size_t len)
{
  SWMSG_MONITOR mon = {0} ;

  mon.timeline = tl ;
  mon.channel  = channel ;
  mon.type     = type ;
  mon.text     = msg ;
  mon.length   = len ;

  (void)SwEvent(SWEVT_MONITOR, &mon, sizeof(mon)) ;
}

void vmonitorf(sw_tl_ref tl, sw_mon_channel channel, sw_mon_type type,
               uint8 *format, va_list vlist )
{
#define VMONITORF_SIZE 1000
  size_t len ;
  uint8 buffer[VMONITORF_SIZE];  /* tough turnips to anyone who wants more than 1K */

  len = (size_t)vswncopyf(buffer, VMONITORF_SIZE, format, vlist);
  if ( len >= VMONITORF_SIZE ) {
    HQFAIL("Monitor output too long") ;
    len = VMONITORF_SIZE - 1 ;
  }
  swmonitor(tl, channel, type, buffer, len) ;

  if (len > 0 && buffer[len-1] != (uint8)'\n') {
    /* Building output by multiple calls to monitorf can be unpredictable with
       multiple threads, so should be avoided. Place a breakpoint here to
       find such usage: */
    len = 0 ;
  }
}

/* Parameterised version of monitorf for the monitor event */
void emonitorf(sw_tl_ref tl, sw_mon_channel channel, sw_mon_type type,
               uint8 *format, ...)
{
  va_list vlist ;

  va_start( vlist, format ) ;
  vmonitorf( tl, channel, type, format, vlist ) ;
  va_end( vlist ) ;
}

/* Output messages of type INFO on the MONITOR channel. This will do for now,
   but the ~1250 uses of monitorf will have to be separated into different
   groups using different versions of this veneer, or emonitorf above. */
void monitorf( uint8 *format, ... )
{
  corecontext_t *context = &CoreContext;
  DL_STATE *page = context != NULL ? context->page : NULL;
  va_list vlist ;
  sw_tl_ref tl = 0 ;
  if (page && page->job)
    tl = page->job->timeline ;

  va_start( vlist, format ) ;
  vmonitorf( tl, MON_CHANNEL_MONITOR, MON_TYPE_INFO, format, vlist ) ;
  va_end( vlist ) ;
}

/* ========================================================================== */
/* Satisfy dependency from fileio subsystem for hook called when trying to
   close a device. */
Bool device_closing(DEVICELIST *dev)
{
  return fileio_check_device(dev) ;
}

/* Satisfy dependency from fileio subsystem for hook called when trying to
   open a filter. */
Bool filter_create_hook(FILELIST *filter, OBJECT *args, STACK *stack)
{
  return (ps_filter_preflight(filter) &&
          pdf_x_filter_preflight(filter, args, stack) &&
          ps_filter_install(filter, args, stack)) ;
}

/* Satisfy dependency from fileio subsystem for hook called when trying to
   open an unknown device-only file. */
Bool file_standard_open(uint8 *name, int32 len, int32 flags, OBJECT *file)
{
  return ps_file_standard(name, len, flags, file) ;
}

#ifdef DEBUG_BUILD
void dmonitorf(debug_buffer_t *buffer, char *format, ...)
{
  size_t overlap = 0 ;

  multi_mutex_lock(&dmonitor_mutex);

  for (;;) {
    size_t n, written, newline, remaining = DEBUG_BUFFER_SIZE - buffer->count ;
    va_list ap;

    va_start(ap, format);
    n = (size_t)vswncopyf(&buffer->chars[buffer->count],
                          CAST_SIZET_TO_INT32(remaining), (uint8 *)format, ap);
    va_end(ap);

    if ( n >= DEBUG_BUFFER_SIZE ) {
      /* If this string is too big for the debug buffer, we can't output it
         all in one go regardless how many times we try. The test is "ge"
         because vswncopyf() NUL-terminates the string, taking one byte. */
      HQFAIL("Debug buffer isn't big enough for output") ;
      break ;
    }

    if ( (written = n) >= remaining ) {
      /* Output is NUL-terminated, so we actually wrote this number of bytes */
      written = remaining - 1 ;
    }

    if ( overlap > 0 && overlap < written ) {
      /* We tried before, but ran out of space. When we tried before, we
         flushed some of the characters from this string out to the output.
         Remove those characters from the amount we store. */
      HqMemMove(&buffer->chars[buffer->count],
                &buffer->chars[buffer->count + overlap],
                written - overlap) ;
    }

    buffer->count += written - overlap ;

    /* If we did not fill up the buffer, we're done. */
    if ( n < remaining )
      break ;

    /* We need to flush the buffer, it's full. Find the last newline, and
       flush up to there. */
    newline = buffer->count ;

    while ( newline > 0 ) {
      if ( buffer->chars[--newline] == '\n' ) {
        ++newline ;
        break ;
      }
    }

    if ( newline == 0 ) /* No newline in buffer. Flush the whole thing. */
      newline = buffer->count ;

    swmonitor(0, MON_CHANNEL_MONITOR, MON_TYPE_DEBUG, buffer->chars, newline) ;

    buffer->count -= newline ;
    if ( buffer->count > 0 )
      HqMemMove(&buffer->chars[0], &buffer->chars[newline], buffer->count) ;

    /* Shift down the bit we didn't write out, and calculate the overlap. */
    if ( newline > DEBUG_BUFFER_SIZE - remaining ) {
      /* The newline was inside the new section, so we have an overlap. */
      overlap = DEBUG_BUFFER_SIZE - newline ;
    } else {
      /* The newline was inside the old section. */
      overlap = written ;
    }
  }

  multi_mutex_unlock(&dmonitor_mutex);
}

void dflush(debug_buffer_t *buffer)
{
  if ( buffer->count > 0 ) {
    multi_mutex_lock(&dmonitor_mutex);
    swmonitor(0, MON_CHANNEL_MONITOR, MON_TYPE_DEBUG, buffer->chars, buffer->count) ;
    multi_mutex_unlock(&dmonitor_mutex);
    buffer->chars[0] = '\0' ;
    buffer->count = 0 ;
  }
}

static struct {
  sw_tl_type type ;
  char *name ;
} timeline_names[] = {
  {SWTLT_CORE,               "Core"},
  {SWTLT_FILE_INTERPRET,     "ReadFile"},
  {SWTLT_JOB_STREAM,         "CoreJobStream"},
  {SWTLT_JOB_CONFIG,         "CoreJobConfig"},
  {SWTLT_JOB,                "CoreJob"},
  {SWTLT_INTERPRET_PAGE,     "InterpretPage"},
  {SWTLT_COUNTING_PAGES,     "Counting"},
  {SWTLT_SCANNING_PAGES,     "Scanning"},
  {SWTLT_HALFTONE_USAGE,     "HalftoneUsage"},
  {SWTLT_HALFTONE_CACHING,   "HalftoneCache"},
  {SWTLT_RECOMBINE_PAGE,     "Recombine"},
  {SWTLT_TRAP_PREPARATION,   "TrapPrepare"},
  {SWTLT_TRAP_GENERATION,    "TrapGeneration"},
  {SWTLT_TRAP_IMAGES,        "TrapImage"},
  {SWTLT_RENDER_PREPARE,     "RenderPrepare"},
  {SWTLT_COMPOSITE_PAGE,     "Compositing"},
  {SWTLT_RENDER_PAGE,        "RenderPage"},
  {SWTLT_RENDER_PARTIAL,     "PartialPaint"},
  {SWTLT_RENDER_SEPARATION,  "Separation"},
  {SWTLT_PGB,                "PGB"},
  {SWTLT_PRESCANNING_PAGES,  "PreScanning"},
  {SWTLT_POSTSCANNING_PAGES, "PostScanning"},
  {SWTLT_RENDER_CACHE,       "RenderCache"},
} ;

static void debug_register_tl_names(void)
{
  int i ;

  for ( i = 0 ; i < NUM_ARRAY_ITEMS(timeline_names) ; ++i ) {
    /* Debug, we don't care if it's not registered properly. */
    (void)SwRegisterRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME,
                        timeline_names[i].type, timeline_names[i].name,
                        strlen(timeline_names[i].name), SW_RDR_NORMAL) ;
  }
}

static void debug_deregister_tl_names(void)
{
  int i ;

  for ( i = 0 ; i < NUM_ARRAY_ITEMS(timeline_names) ; ++i ) {
    (void)SwDeregisterRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME,
                          timeline_names[i].type, timeline_names[i].name,
                          strlen(timeline_names[i].name)) ;
  }
}

static void HQNCALL debug_linkable_monitorf(uint8 *format, ...)
{
  va_list vlist;

  va_start( vlist, format ) ;
  vmonitorf( 0, MON_CHANNEL_MONITOR, MON_TYPE_DEBUG, format, vlist );
  va_end( vlist );
}

void debug_print_timelines(sw_tl_ref tl)
{
  sw_tl_debug debug ;
  debug.tl = tl ;
  debug.monitorf = debug_linkable_monitorf ;
  (void)SwEvent(EVENT_TIMELINE_DEBUG, &debug, sizeof(debug)) ;
}
#endif

/*
Log stripped */
