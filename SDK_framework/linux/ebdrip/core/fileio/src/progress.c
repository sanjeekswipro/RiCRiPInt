/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!src:progress.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of progress device
 */


#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swcopyf.h"
#include "swevents.h"
#include "riptimeline.h"
#include "devices.h"
#include "objects.h"

#include "fileio.h"
#include "progupdt.h"
#include "progress.h"

static void update_read_file_progress(void);
static void pop_file_timeline(void);

/*
  Records the stack of files that are interpreted so that dial/progress
  updates can happen
*/

#define PROG_READ_DIALS 3
static struct {
  FILELIST *flptr;
  sw_tl_ref tl_ref;
} aReadFile[PROG_READ_DIALS];
static int32 maxIndex = -1;  /* # on stack - 1 */
static int32 progLock = 0;   /* to stop reentrancy */

/**
 * Event handler for file progress.
 *
 * Catch job timeline-end events so that we can tidy-up any file
 * progress that may have been left open.
 */
static sw_event_result HQNCALL FileProgress_handler(void *context,
                                                    sw_event *evt)
{
  SWMSG_TIMELINE *msg = evt->message;

  UNUSED_PARAM(void *, context);

  if ( msg == NULL || evt->length < sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_CONTINUE;

  /*
   * If the job has ended, close any file progress timelines that may
   * have been left open.
   *
   * This should not happen typically, but is possible especially when
   * a job has been aborted. If it has been caused by a file being left open,
   * the root cause should be tracked-down and fixed too, as that may be
   * causing a memory leak of file handles.
   */
  if ( msg->type == SWTLT_JOB ) {
    while ( maxIndex >= 0 )
      pop_file_timeline();
  }
  return SW_EVENT_CONTINUE;
}

static sw_event_handlers handlers[] = {
  {FileProgress_handler,  NULL, 0, EVENT_TIMELINE_ENDED,   SW_EVENT_DEFAULT },
  {FileProgress_handler,  NULL, 0, EVENT_TIMELINE_ABORTED, SW_EVENT_DEFAULT }
};

Bool initReadFileProgress(void)
{
  return SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ==
         SW_RDR_SUCCESS;
}

void termReadFileProgress(void)
{
  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers));
}

void init_C_globals_progress(void)
{
  maxIndex = -1;
  progLock = 0;
}

/* Abstract file progress stack to localise globals used */
static Bool stack_full(void)
{
  return(maxIndex >= PROG_READ_DIALS - 1);
}

static Bool stack_empty(void)
{
  return(maxIndex < 0);
}

static void push_file_timeline(FILELIST* flptr, const uint8 *title, size_t len,
                               sw_tl_extent extent)
{
  sw_tl_ref tl_ref ;

  HQASSERT(!stack_full(), "file timeline stack full");

  CHECK_TL_VALID(tl_ref = progress_start(title, len, extent)) ;

  if ( tl_ref != SW_TL_REF_INVALID ) {
    ++maxIndex;
    aReadFile[maxIndex].flptr  = flptr;
    aReadFile[maxIndex].tl_ref = tl_ref;
  }
}

static void pop_file_timeline(void)
{
  HQASSERT(!stack_empty(), "file timeline stack empty");

  progress_end(&aReadFile[maxIndex].tl_ref);
  aReadFile[maxIndex].flptr = NULL;
  maxIndex--;
}

static FILELIST* stack_top(sw_tl_ref *tl_ptr)
{
  HQASSERT(!stack_empty(), "stack_top: file stack empty");

  *tl_ptr = aReadFile[maxIndex].tl_ref;
  return(aReadFile[maxIndex].flptr);
}

/* A horrible merge of two stack iterating functions that are similar enough to
 * make it worth while to see what is going on.
 *
 * The difference is whether the given file is closed as well, and when closing
 * files there is a final update on the file's progress.
 */
static
Bool stack_search(
  FILELIST* flptr,
  Bool closing)
{
  int i;

  HQASSERT(flptr != NULL, "stack_search: NULL file pointer");

  /* search stack of current ones for match */
  for (i = maxIndex; i >= 0; i--) {
    if (flptr == aReadFile[i].flptr) {
      HQASSERT(isIOpenFile(flptr),
               "stack_search: reporting progress on closed file");
      /* if found close any later ones on the stack */
      if (closing) {
        while (maxIndex > i) {
          update_read_file_progress();
          pop_file_timeline();
        }
      }
      /* close the given one as well */
      if (closing) {
        update_read_file_progress();
        pop_file_timeline();
      }
      return(TRUE);
    }
  }
  return(FALSE);
}

/* Hide the candy machine interface behind wrapper macros */
#define STACK_CONTAINS(flptr) (stack_search(flptr, FALSE))
#define STACK_CLOSE(flptr)    (stack_search(flptr, TRUE))


/* Add a file (or the device file for a filter chain) to the top of the stack of
 * files whose read progress will be reported.
 *
 * This implements the same logic as the previous version that used the progress
 * PS device to report progress.
 */
static void set_read_file_progress(FILELIST* flptr)
{
  /* Total length including device name delimiter */
#define MAX_PS_FILENAME (1 + LONGESTDEVICENAME + 1 + LONGESTFILENAME)
  uint8 buff[MAX_PS_FILENAME + 1]; /* Include terminating NUL */
  sw_tl_extent tl_extent;
  Hq32x2 extent;
  DEVICELIST* dev;
  size_t len;
  int32 deverr;

  /* do nothing if already being reported */
  if (STACK_CONTAINS(flptr)) {
    return;
  }

  /* report filter chain device files */
  if ((dev = theIDeviceList(flptr)) == NULL ||
      (theIFlags(flptr) & FILTER_FLAG) != 0) {
    if (theIUnderFile(flptr) &&
        isIOpenFileFilterById(theIUnderFilterId(flptr), theIUnderFile(flptr))) {
      set_read_file_progress(theIUnderFile(flptr));
    }
    return;
  }

  /* catch filter chain device files already closed */
  if (!isIOpenFile(flptr)) {
    return;
  }

  /* if full end reporting last file added */
  if (stack_full()) {
    pop_file_timeline();
  }

  /* find file extent */
  if (!(*theIBytesFile(dev))(dev, theIDescriptor(flptr), &extent,
                             SW_BYTES_TOTAL_ABS)) {
    deverr = (*theILastErr(dev))(dev);
    if (!(deverr == DeviceNoError || deverr == DeviceIOError)) {
      (void)device_error_handler(dev);
      return;
    }
    tl_extent = SW_TL_EXTENT_INDETERMINATE;
  } else {
    tl_extent = (sw_tl_extent)Hq32x2ToDouble(&extent);
  }

  /*
   * Read-write files are too painful to try and track progress of, and
   * are not really that important, so don't bother trying.
   */
  if ( isIReadWriteFile(flptr) )
    return;

  /* add to top of stack if reporting */
  len = swncopyf(buff, sizeof(buff), (uint8*)"%%%s%%%s", theIDevName(dev),
                 theICList(flptr));

  push_file_timeline(flptr, buff, len, tl_extent);

  return;
}

Bool setReadFileProgress(
  FILELIST* flptr)
{
  progLock++;
  set_read_file_progress(flptr);
  progLock--;
  return(TRUE);
}

Bool closeReadFileProgress(
  FILELIST* flptr)
{
  progLock++;
  STACK_CLOSE(flptr);
  progLock--;
  return(TRUE);
}

/**
 * Update the read file progress for the top item on the stack.
 */
static void update_read_file_progress(void)
{
  Hq32x2 size;
  DEVICE_FILEDESCRIPTOR d;
  sw_tl_ref tl_ref;
  FILELIST *flptr;
  DEVICELIST *dev;
  int32 localcount;

  /* updating file on top of stack */
  flptr = stack_top(&tl_ref);
  d = theIDescriptor(flptr);
  if ((dev = theIDeviceList(flptr)) == NULL) {
    HQFAIL("update_file_progress: file with no device blocking updates, report to core");
    return;
  }
  HQASSERT(!isIReadWriteFile(flptr),
      "Should not be tracking progress on read-write files");

  /* theICount will sometimes be -1. Treat this as though it was zero */
  localcount = theICount(flptr);
  if (localcount < 0) {
    localcount = 0;
  }

  /* Do a relative seek of 0 bytes to find out where we are */
  Hq32x2FromInt32(&size, 0);
  if (!(*theISeekFile(dev))(dev, d, &size, SW_INCR))
    return;

  if (isIInputFile(flptr)) {
    Hq32x2SubtractInt32(&size, &size, localcount);
  }

  progress_current(tl_ref, (sw_tl_extent)Hq32x2ToDouble(&size));
  return;
}

/* External API that adds a lock */
void updateReadFileProgress(void)
{
  if ( !stack_empty() && progLock == 0 ) {
    progLock++;
    update_read_file_progress();
    progLock--;
  }
}

/* Log stripped */
