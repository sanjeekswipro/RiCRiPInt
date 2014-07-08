/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:progdev.c(EBDSDK_P.1) $
 */

/**
 * @file
 * @ingroup skinkit
 * @brief Implementation of an example progress device.
 *
 * The progress device is an optional device, known as <tt>\%progress\%</tt> in
 * PostScript. It exists when it has been mounted and its device type set
 * during execution of the <tt>Sys/ExtraDevices</tt> file. If it does not
 * exist the RIP works silently.
 *
 * The RIP will open named files on the <tt>\%progress\%</tt> device, which will
 * then either use the progress_write_file() call to write arbitrary data (text
 * files) or the progress_seek_file() call (seek files) to indicate numerical
 * progress information; it will not use both for the same file.
 *
 * For more information see Chapter 8 of the Programmers Reference Manual.
 */
#include "std.h"
#include "hqstr.h"
#include "swdevice.h"
#include "skindevs.h"
#include "ripthread.h"
#include "skinkit.h"
#include "sync.h"
#include "mem.h"

#include <string.h>


/* The RIP enforces this limit on the depth of the read dial stack */
#define MAX_OPEN_READ_DIALS 3

/* ----------------------- Types ------------------------------------------- */

/* entry in table driven approach to progress streams */
typedef struct ProgressStream
{
  char* ptbzFilename;
  uint8 type; /* PROGRESS_TYPE_... */
  int32 fRequired;
} ProgressStream;

/**
 * @brief Encapsulates the state of a single instance of this device.
 */
typedef struct ProgressDeviceState
{
  /** @brief Index ticker for the \c start_param and \c get_param interfaces. */
  int32 iParam;

  /** @brief Semaphore to protect concurrent calls. */
  void* pSema;

} ProgressDeviceState;

/* ----------------------- Data -------------------------------------------- */

/**
 * @brief  Whether output from RIP JobLog is passed to the skin monitor.
 * @hideinitializer
 *
 * This value corresponds to the \c JobTiming device parameter.
 *
 * @see progress_set_param(), progress_write_file()
 */
static int32 fCaptureTextStreamOutput = FALSE;

/* Device parameters */
static const uint8 szJobTimingParam[] = "JobTiming";

/**
 * @brief  progress_open_file() is driven by this table.
 * @hideinitializer
 *
 * <tt>\%progress\%HalftoneInfo</tt>: This text file is responsible for reporting
 * the usage and accuracy of the screens in a job.
 *
 * <tt>\%progress\%HalftoneCaching</tt>: This seek file indicates the progress
 * when the RIP is caching a Harlequin Precision Screen (most often when color
 * separating). progress_seek_file() is called first with \c SW_XTND, giving a
 * number that is an indication of the size of the task. A percentage is
 * obtained by dividing subsequent \c SW_SET seek amounts by first
 * \c SW_XTND amount.
 *
 * <tt>\%progress\%JobLog</tt>: The RIP supports the ability to log job
 * information, such as how long each job took to interpret, rasterize, and
 * output.
 */
static ProgressStream aStreams[] =
{
#define PROGRESS_TYPE_DIAL_READ         0

  /* RIP progress dials */
#define PROGRESS_TYPE_DIAL_HALFTONE     1
  {
    "HalftoneCaching",
    PROGRESS_TYPE_DIAL_HALFTONE,
    FALSE
  },

#define PROGRESS_TYPE_DIAL_TOTAL        2

  /* RIP progress text streams */
#define PROGRESS_TYPE_TEXT_HALFTONE     2
  {
    "HalftoneInfo",
    PROGRESS_TYPE_TEXT_HALFTONE,
    TRUE
  },
#define PROGRESS_TYPE_TEXT_JOB_LOG      3
  {
    "JobLog",
    PROGRESS_TYPE_TEXT_JOB_LOG,
    TRUE
  }

#define PROGRESS_TYPE_TOTAL             4
};

/**
 * @brief  Number of times each of the progress types is open.
 * @hideinitializer
 *
 * @note Only the read dial can be opened multiple times.
 */
static uint8 openCount[PROGRESS_TYPE_TOTAL] = { 0 };

/* ----------------------- Function Implementation ------------------------- */

/**
 * @brief  Validate the specified handle information.
 */
static HqBool validHandle (uint8 type, intptr_t depth)
{
  return (type < PROGRESS_TYPE_TOTAL && depth == openCount[type] - 1);
}

/**
 * @brief Construct a handle from the type and depth.
 *
 * The encoding of type and depth as a handle relies on the fact that depth
 * can only be non zero for type 0 (\c PROGRESS_TYPE_DIAL_READ), and is bounded
 * to 0 <= depth < \c MAX_OPEN_READ_DIALS.
 * This allows the handle to be a zero based index of all possible open streams.
 * <pre>
 *             Handle                   Type  Depth
 *                  0                      0      2
 *                  1                      0      1
 *                  2                      0      0
 *                  3                      1      0
 *                  4                      2      0
 *                ...                    ...    ...
 *                  h                    h-2      0
 * </pre>
 *
 * @return A handle value.
 */
static int32 progressHandle (uint8 type, int32 depth)
{
  return (type + (MAX_OPEN_READ_DIALS - 1)) - depth;
}

/**
 * @brief  Extract the type from a handle.
 */
static uint8 progressHandleType (DEVICE_FILEDESCRIPTOR handle)
{
  DEVICE_FILEDESCRIPTOR type = handle - (MAX_OPEN_READ_DIALS - 1);
  return (uint8) (type <= 0 ? 0 : type);
}

/**
 * @brief  Extract the depth from a handle.
 */
static intptr_t progressHandleDepth (DEVICE_FILEDESCRIPTOR handle)
{
  intptr_t depth = (MAX_OPEN_READ_DIALS - 1) - handle;
  return (depth < 0) ? 0 : depth;
}

static void progress_set_lasterror(DEVICELIST *dev, int32 error)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(error);
}

static int32 RIPCALL progress_ioerror (DEVICELIST *dev)
{
  progress_set_lasterror(dev, DeviceIOError);
  return -1;
}

static int32 RIPCALL progress_noerror (DEVICELIST *dev)
{
  progress_set_lasterror(dev, DeviceNoError);
  return 0;
}

/**
 * @brief  Perform any required device initialization.
 */
static int32 RIPCALL progress_init_device (DEVICELIST* dev)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;

  progress_noerror (dev);

  pState->iParam = 0;
  if ((pState->pSema = PKCreateSemaphore (1)) == NULL)
    return -1;

  return 0;
}

/**
 * @brief  The progress device \c open_file callback.
 *
 * This function should return -1 for unsupported progress types. The RIP will
 * not treat this as an error; it just will not generate progress information
 * for this type of progress. However, if the file is opened from the PostScript
 * language an error will be reported unless the file is opened in a stopped
 * context.
 */
static DEVICE_FILEDESCRIPTOR RIPCALL progress_open_file (DEVICELIST* dev,
                                                         uint8* filename,
                                                         int32 openflags)
{
  int32 descriptor = -1;

  progress_noerror (dev);

  if (filename[0] != '%')
  {
    uint32 i;
    for (i = 0; i < NUM_ARRAY_ITEMS (aStreams); i++)
    {
      if (! strcmp ((char*) filename, aStreams[i].ptbzFilename))
      {
        /* Check file is write-only */
        if ( (openflags & (SW_WRONLY|SW_CREAT)) == 0)
        {
          progress_set_lasterror(dev, DeviceInvalidAccess);
          return -1;
        }

        if ( aStreams[i].fRequired )
        {
          uint8 type = aStreams[i].type;

          if (openCount[type] != 0)
            return progressHandle (type, 0); /* Already open */

          /* Open file */
          openCount[type] = 1;
          descriptor = progressHandle (type, 0);
        }
      }
    }
  }

  if (descriptor == -1)
    progress_set_lasterror(dev, DeviceUndefined);

  return descriptor;
}

/**
 * @brief  A no-op.
 *
 * All files on the progress device will be opened by the RIP as write-only,
 * so there is no need to implement a comprehensive \c read_file call.
 */
static int32 RIPCALL progress_read_file (DEVICELIST* dev, DEVICE_FILEDESCRIPTOR descriptor,
                                         uint8*buff, int32 len)
{
  UNUSED_PARAM (DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM (uint8*, buff);
  UNUSED_PARAM (int32, len);
  return progress_ioerror (dev);
}

/**
 * @brief Called to report human-readable information from progress text files.
 */
static int32 RIPCALL progress_write_file (DEVICELIST* dev, DEVICE_FILEDESCRIPTOR descriptor,
                                          uint8* buff, int32 len)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;
  uint8 type = progressHandleType (descriptor);
  intptr_t depth = progressHandleDepth (descriptor);

  progress_noerror (dev);

  if (! validHandle (type, depth))
    return progress_ioerror (dev);

  PKWaitOnSemaphore (pState->pSema);

  if ( type >= PROGRESS_TYPE_DIAL_TOTAL
       && (type != PROGRESS_TYPE_TEXT_JOB_LOG || fCaptureTextStreamOutput ))
    SkinMonitorl (len, buff);

  PKSignalSemaphore (pState->pSema);

  return len;
}

/**
 * @brief  Close the specified file.
 */
static int32 RIPCALL progress_close_common (DEVICELIST* dev,
                                            DEVICE_FILEDESCRIPTOR descriptor,
                                            int32 fAbort)
{
  uint8 type = progressHandleType (descriptor);
  intptr_t depth = progressHandleDepth (descriptor);

  UNUSED_PARAM (DEVICELIST*, dev);
  UNUSED_PARAM (int32, fAbort);

  progress_noerror (dev);

  if (! validHandle (type, depth))
    return progress_ioerror (dev);

  --openCount[type];

  return 0;
}

/**
 * @see progress_close_common()
 */
static int32 RIPCALL progress_close_file (DEVICELIST* dev, DEVICE_FILEDESCRIPTOR descriptor)
{
  return progress_close_common (dev, descriptor, FALSE);
}

/**
 * @see progress_close_common()
 */
static int32 RIPCALL progress_abort_file (DEVICELIST* dev, DEVICE_FILEDESCRIPTOR descriptor)
{
  return progress_close_common (dev, descriptor, TRUE);
}

/**
 * @brief  Called by the RIP to report bounded progress information.
 *
 * 'Seek files' are intended to relay the amount of work done in a task by
 * first telling the device how much work has to be done, and then telling it
 * repeatedly how far through that amount of work the RIP has processed. After
 * the progress_open_file() call, a series of calls are made to
 * progress_seek_file() that indicate how far the process has gotten in its task.
 *
 * The first call to progress_seek_file() will be passed the \c SW_XTND
 * to \c flags. The \c destn argument gives an estimate of how much work is
 * expected (e.g. the total size of a file in bytes). Later calls will express
 * work done so far as a proportion of this total. Periodically thereafter a
 * number of calls will be made, but with \c flags set to \c SW_SET
 * or \c SW_INCR. \c SW_INCR is used to show how much data remains buffered but
 * unread by the RIP (for unbounded reading), and \c SW_SET says how much has
 * been consumed to date.
 */
static int32 RIPCALL progress_seek_file (DEVICELIST* dev, DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2* destn, int32 flags)
{
  UNUSED_PARAM (DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM (Hq32x2*, destn);
  UNUSED_PARAM (int32, flags);

  progress_noerror (dev);
  return TRUE;
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_bytes_file (DEVICELIST* dev,
                                          DEVICE_FILEDESCRIPTOR descriptor,
                                          Hq32x2* bytes,
                                          int32 reason)
{
  UNUSED_PARAM (DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM (Hq32x2*, bytes);
  UNUSED_PARAM (int32, reason);

  progress_ioerror (dev);
  return FALSE;
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_status_file (DEVICELIST* dev,
                                           uint8* filename,
                                           STAT* statbuff)
{
  UNUSED_PARAM (uint8*, filename);
  UNUSED_PARAM (STAT*, statbuff);
  return progress_ioerror (dev);
}

static void * RIPCALL progress_interp_start_file_list (DEVICELIST* dev, uint8* pattern)
{
  UNUSED_PARAM (uint8*, pattern);

  progress_noerror (dev);
  return (void*) NULL;
}

static int32 RIPCALL progress_interp_next_file
(DEVICELIST* dev, void ** handle, uint8* pattern, FILEENTRY * entry)
{
  UNUSED_PARAM (void **, handle);
  UNUSED_PARAM (uint8*, pattern);
  UNUSED_PARAM (FILEENTRY *, entry);

  progress_noerror (dev);
  return FileNameNoMatch;
}

static int32 RIPCALL progress_interp_end_file_list (DEVICELIST* dev, void * handle)
{
  UNUSED_PARAM (void **, handle);
  return progress_noerror (dev);
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_rename_file (DEVICELIST* dev, uint8* file1, uint8* file2)
{
  UNUSED_PARAM (uint8*, file1);
  UNUSED_PARAM (uint8*, file2);
  return progress_ioerror (dev);
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_delete_file (DEVICELIST* dev, uint8* filename)
{
  UNUSED_PARAM (uint8*, filename);
  return progress_ioerror (dev);
}

/**
 * @brief The \c start_param routine for the progress device type.
 *
 * The routine is called as part of the \c currentdevparams operator; the
 * individual parameters are returned one at a time by subsequent calls to
 * progress_get_param(). \c pState->iParam is used to maintain the state
 * between calls to progress_get_param().
 */
static int32 RIPCALL progress_start_param (DEVICELIST* dev)
{
  const int32 kDeviceSpecificParams = 1;
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;

  progress_noerror (dev);
  pState->iParam = 0;

  return kDeviceSpecificParams;
}

/**
 * @brief The \c set_param routine for the progress device type.
 *
 * In this example the only custom device param is \c JobTiming. Setting
 * this Boolean value to true will allow RIP timing information
 * to be passed to the skin monitor.
 */
static int32 RIPCALL progress_set_param (DEVICELIST* dev, DEVICEPARAM* param)
{
  int32 length;

  progress_noerror (dev);

  length = strlen_int32 ((char*) szJobTimingParam);

  if (param->paramnamelen == length &&
      strncmp ((char*) param->paramname,
               (char*) szJobTimingParam, CAST_SIGNED_TO_SIZET(length)) == 0)
  {
    /* Check param type */
    if (param->type != ParamBoolean)
    {
      progress_ioerror (dev);
      return ParamTypeCheck;
    }

    /* Set new value */
    fCaptureTextStreamOutput = param->paramval.boolval;
    return ParamAccepted;
  }

  return ParamIgnored;
}

/**
 * @brief The \c get_param routine for the progress device type.
 *
 * This routine serves two purposes: When the parameter name is \c NULL, it is
 * to return the next device parameter. \c pState->iParam keeps
 * track of how far through the list we are (though in this case there
 * is only one) otherwise it should return the one called for by the name.
 */
static int32 RIPCALL progress_get_param (DEVICELIST* dev, DEVICEPARAM* param)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;

  progress_noerror (dev);

  if (param->paramname == NULL)
  {
    /* Return the next device parameter */
    switch (pState->iParam ++)
    {
    case 0:
      param->paramname = (uint8*) szJobTimingParam;
      param->paramnamelen = strlen_int32 ((char*) param->paramname);
      param->type = ParamBoolean;
      param->paramval.boolval = fCaptureTextStreamOutput;
      return ParamAccepted;

    default:
      return ParamIgnored;
    }
  }
  else if (strncmp ((char*) param->paramname,
                    (char*) szJobTimingParam,
                    CAST_SIGNED_TO_SIZET(param->paramnamelen)) == 0)
  {
    /* Returned the named device parameter */
    param->type = ParamBoolean;
    param->paramval.boolval = fCaptureTextStreamOutput;
    return ParamAccepted;
  }

  return ParamIgnored;
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_status_device (DEVICELIST* dev, DEVSTAT * devstat)
{
  UNUSED_PARAM (DEVSTAT *, devstat);
  return progress_ioerror (dev);
}

/**
 * @brief  Destroy state created in progress_init_device().
 */
static int32 RIPCALL progress_dismount_device (DEVICELIST* dev)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;

  if (pState->pSema)
  {
    PKDestroySemaphore (pState->pSema);
    pState->pSema = NULL;
  }

  return progress_noerror (dev);
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_buffersize (DEVICELIST* dev)
{
  return progress_ioerror (dev);
}

/**
 * @brief  A no-op.
 */
static int32 RIPCALL progress_void (void)
{
  return -1;
}

/**
 * @brief  The DEVICETYPE object describing the callbacks in this device.
 */
DEVICETYPE Progress_Device_Type =
{
  PROGRESS_DEVICE_TYPE, /* the device ID number */
  DEVICERELATIVE|DEVICEWRITABLE|DEVICESMALLBUFF|DEVICELINEBUFF,
  /* flags to indicate specifics of device */
  CAST_SIZET_TO_INT32(sizeof(ProgressDeviceState)), /* the size of the private data */
  0, /* minimum ticks between tickle functions */
  NULL, /* procedure to service the device */
  skindevices_last_error, /* return last error for this device */
  progress_init_device, /* initialise device */
  progress_open_file, /* open file on device */
  progress_read_file, /* read data from file on device */
  progress_write_file, /* write data to file on device */
  progress_close_file, /* close file on device */
  progress_abort_file, /* abort action on the device */
  progress_seek_file, /* seek file on device */
  progress_bytes_file, /* get bytes available on an open file */
  progress_status_file, /* check status of file */
  progress_interp_start_file_list, /* start listing files */
  progress_interp_next_file, /* get next file in list */
  progress_interp_end_file_list, /* end listing */
  progress_rename_file, /* rename file on the device */
  progress_delete_file, /* remove file from device */
  progress_set_param, /* set device parameter */
  progress_start_param, /* start getting device parameters */
  progress_get_param, /* get the next device parameter */
  progress_status_device, /* get the status of the device */
  progress_dismount_device, /* dismount the device */
  progress_buffersize,
  NULL, /* ioctl not needed */
  progress_void /* spare slots */
};

