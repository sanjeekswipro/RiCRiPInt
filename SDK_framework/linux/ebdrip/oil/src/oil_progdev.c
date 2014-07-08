/* Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_progdev.c(EBDSDK_P.1) $
 */

/**
 * @file
 * @ingroup OIL
 * @brief OIL implementation of progress device type.
 *
 * The progress device allows both qualitative and quantitative progress
 * data to be reported by the RIP.  The device may use the data in whatever
 * way is appropriate, from updating a progress bar to outputting a message in
 * a dialog box.  Qualitative data (such as logging an "operation complete"
 * message) is logged via a different mechanism than quantitative data, which
 * more typically reports the percentage completion of a task of known size.
 *
 * The progress device is an optional device, known as <tt>\%progress\%</tt> in
 * PostScript. It exists when it has been mounted and its device type set
 * during execution of the <tt>Sys/ExtraDevices</tt> file. If it does not
 * exist the RIP works silently.
 *
 * The RIP opens named files on the <tt>\%progress\%</tt> device to record
 * progress information.  Either progress_write_file() (for qualitative data)
 * or progress_seek_file() (for quantitative data) will be called to record
 * progress information.  Any given file will hold either qualitative or
 * quantitative data; therefore, only one function will ever be called to
 * update any one file.
 *
 * For more information see Chapter 8 of the Programmers Reference Manual.
 */
#include "oil_progdev.h"

#include "swevents.h"   /* SW_INTERRUPT */
#include "swtimelines.h"
#include "hqstr.h"
#include "ripthread.h"
#include "skinkit.h"
#include "sync.h"
#include "oil_stream.h"
#include "oil_ebddev.h"

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
  /** @brief The integer code for the most recent error. */
  int32 lastError;

  /** @brief Index ticker for the \c start_param and \c get_param interfaces. */
  int32 iParam;

  /** @brief Number of bytes consumed by RIP. */
  Hq32x2 cbBytesConsumed;

  /** @brief Semaphore to protect concurrent calls. */
  void* pSema;
} ProgressDeviceState;

/* ----------------------- Data -------------------------------------------- */

/**
 * @brief  Whether output from RIP progress text streams is passed
 * to \c SkinMonitorl().
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
 * the usage and accuracy of the screens in a job. Qualitative data.
 *
 * <tt>\%progress\%HalftoneCaching</tt>: This seek file indicates the progress
 * when the RIP is caching a Harlequin Precision Screen (most often when color
 * separating). progress_seek_file() is called first with \c SW_XTND, giving a
 * number that is an indication of the size of the task. A percentage is
 * obtained by dividing subsequent \c SW_SET seek amounts by first
 * \c SW_XTND amount. Quantitative data.
 *
 * <tt>\%progress\%JobLog</tt>: The RIP supports the ability to log job
 * information, such as how long each job took to interpret, rasterize, and
 * output. Qualitative data.
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
    FALSE
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
 * @param[in]  type     The type of progress file.  One of the values defined by aStreams.
 * @param[in]  depth    The handle depth.
 * @return  Returns TRUE if the handle is valid, FALSE otherwise.
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
 * @param[in]  type     The type of progress file.  One of the values defined by aStreams.
 * @param[in]  depth    The handle depth.
 * @return A handle value.
 */
static int32 progressHandle (uint8 type, int32 depth)
{
  return (type + (MAX_OPEN_READ_DIALS - 1)) - depth;
}

/**
 * @brief  Extract the type from a handle.
 * @param[in]  handle     The handle whose type is to be determined.
 * @return     Returns the type of the file, which is one of the values defined by aStreams.
 */
static uint8 progressHandleType (DEVICE_FILEDESCRIPTOR handle)
{
  DEVICE_FILEDESCRIPTOR type = handle - (MAX_OPEN_READ_DIALS - 1);
  return (uint8) (type <= 0 ? 0 : type);
}

/**
 * @brief  Extract the depth from a handle.
 * @param[in]  handle     The handle whose depth is to be extracted.
 * @return     Returns the depth of the file handle.
 */
static intptr_t progressHandleDepth (DEVICE_FILEDESCRIPTOR handle)
{
  intptr_t depth = (MAX_OPEN_READ_DIALS - 1) - handle;
  return (depth < 0) ? 0 : depth;
}

/**
 * @brief  Get the last error code reported to a device
 * @param[in]  dev      The device instance whose error state is to be queried.
 * @return The last reported error code logged with the device.
 */
static int32 RIPCALL progress_lasterror (DEVICELIST* dev)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;
  return pState->lastError;
}

/**
 * @brief  Log an I/O error with the device.
 * @param[in,out]  dev      The device instance whose error state is to be updated.
 * @return This function always returns -1.
 */
static int32 RIPCALL progress_ioerror (DEVICELIST *dev)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;
  pState->lastError = DeviceIOError;
  return -1;
}

/**
 * @brief  Clear the device's error state.
 * @param[in,out]  dev      The device instance whose error state is to be updated.
 * @return This function always returns zero.
 */
static int32 RIPCALL progress_noerror (DEVICELIST *dev)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;
  pState->lastError = DeviceNoError;
  return 0;
}

/**
 * @brief  Perform the required device initialization.
 * @param[in,out]  dev      The device instance to be initialized.
 * @return This function returns zero if initialization succeeds, or -1 if it fails.
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
 * @brief  The progress device \c open_file() callback.
 *
 * This function should return -1 for unsupported progress types. The RIP will
 * not treat this as an error; it just will not generate progress information
 * for this type of progress. However, if the file is opened from the PostScript
 * language an error will be reported unless the file is opened in a stopped
 * context.
 * @param[in,out]  dev        The device instance on which the file should be opened.
 * @param[in]      filename   The name of the file to be opened.
 * @param[in]      openflags  The access modifiers to use to open the file.  Should
                              include at least one of SW_WRONLY and SW_CREAT.
 * @return This function returns a handle to the opened file, or -1 if the file is
           not successfully opened.
 */
static DEVICE_FILEDESCRIPTOR RIPCALL progress_open_file (DEVICELIST* dev,
                                                         uint8* filename,
                                                         int32 openflags)
{
  ProgressDeviceState* pState = (ProgressDeviceState*) dev->private_data;
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
          pState->lastError = DeviceInvalidAccess;
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
  else if( strcmp( (const char *) filename, "%pipeline%/input" ) == 0 )
  {
    /* Check file is write-only */
    if ( (openflags & (SW_WRONLY|SW_CREAT)) == 0)
    {
      pState->lastError = DeviceInvalidAccess;
      return -1;
    }

    if( openCount[ PROGRESS_TYPE_DIAL_READ ] != 0 )
    {
      return progressHandle( PROGRESS_TYPE_DIAL_READ, 0 );
    }

    descriptor = progressHandle( PROGRESS_TYPE_DIAL_READ, 0 );
    openCount[ PROGRESS_TYPE_DIAL_READ ] = 1;

    Hq32x2FromInt32( &pState->cbBytesConsumed, 0 );
  }

  if (descriptor == -1)
    pState->lastError = DeviceUndefined;

  return descriptor;
}

/**
 * @brief  Read from the file; a no-op implementation.
 *
 * All files on the progress device will be opened by the RIP as write-only,
 * so there is no need to implement a comprehensive \c read_file call.
 * @param[in]      dev        Unused parameter. The device instance from which the file should be read.
 * @param[in]      descriptor Unused parameter. The handle of the file to be read.
 * @param[out]     buff       Unused parameter. The buffer to read data into.
 * @param[in]      len        Unused parameter. The amount of data, in bytes, to read.
 * @return This function always sets an I/O error in the device, and returns -1.
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
 * @brief Called to log qualitative progress information to a text file.
 * @param[in]      dev        The device instance on which the file resides.
 * @param[in]      descriptor The handle of the file to receive the data.
 * @param[in]      buff       The buffer containing the data to write to the file.
 * @param[in]      len        The amount of data, in bytes, in the buffer.
 * @return         Returns the amount of data, in bytes, written to the file.
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

  if (type >= PROGRESS_TYPE_DIAL_TOTAL && fCaptureTextStreamOutput)
    SkinMonitorl (len, buff);

  PKSignalSemaphore (pState->pSema);

  return len;
}

/**
 * @brief  Close the specified file.
 * @param[in]      dev        Unused parameter. The device instance on which the file resides.
 * @param[in]      descriptor The handle of the file to be closed.
 * @param[in]      fAbort     Unused parameter.
 * @return         Returns the amount of data, in bytes, written to the file.
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
  if( openCount[ PROGRESS_TYPE_DIAL_READ ] != 0
    && descriptor == progressHandle( PROGRESS_TYPE_DIAL_READ, 0 ) )
  {
    ProgressDeviceState * pState = (ProgressDeviceState*) dev->private_data;

    Stream_SetBytesConsumed( &pState->cbBytesConsumed );
  }

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
 * @brief  Called by the RIP to report quantitative progress information.
 *
 * 'Seek files' are intended to relay the amount of work done in a task by
 * first telling the device how much work has to be done, and then updating it
 * periodically as to how far through that amount of work the RIP has processed. After
 * the progress_open_file() call, a series of calls are made to
 * progress_seek_file() that indicate how far the process has gotten in its task.
 *
 * The first call to progress_seek_file() passes \c SW_XTND
 * to \c flags. The \c destn argument gives an estimate of how much work is
 * expected (for example, the total size of a file in bytes). Later calls will express
 * work done so far as a proportion of this total. Periodically thereafter a
 * number of calls will be made, but with \c flags set to \c SW_SET
 * or \c SW_INCR. \c SW_INCR is used to show how much data remains buffered but
 * unread by the RIP (for unbounded reading), and \c SW_SET says how much has
 * been consumed to date.
 * @param[in]      dev        Unused parameter. The device instance on which the file resides.
 * @param[in]      descriptor The handle of the file to be closed.
 * @param[in]      destn      The size of the seek from the specified origin.  In the first call to
                              this function for a given file, this provides an estimate of the total
                              amount of work to be done.  In subsequent calls,
 * @param[in]      flags      The access modifiers to use to open the file.  The first time a file is
                              updated, it should be set to SW_XTND, which sets the overall size of the
                              file. In subsequent calls, setting it to SW_SET or SW_INCR will move the
                              seek pointer through the file in accordance with the value specified in
                              \c destn, acting as an indicator of progress through the total work.
 * @todo           This implementation appears to discard any call that does not have flags set
                   to SW_SET.  This is at odds with all the documentation - what is correct?
 * @todo           This implementation marks destn and flags as unused parameters; clearly they are not.
 * @return         Returns the amount of data, in bytes, written to the file.
 */
static int32 RIPCALL progress_seek_file (DEVICELIST* dev, DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2* destn, int32 flags)
{
  UNUSED_PARAM (DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM (Hq32x2*, destn);
  UNUSED_PARAM (int32, flags);

  progress_noerror (dev);

  if( flags == SW_SET
    && openCount[ PROGRESS_TYPE_DIAL_READ ] != 0
    && descriptor == progressHandle( PROGRESS_TYPE_DIAL_READ, 0 ) )
  {
    ProgressDeviceState * pState = (ProgressDeviceState*) dev->private_data;

    if( Hq32x2Compare( &pState->cbBytesConsumed, destn ) < 0 )
    {
      pState->cbBytesConsumed = *destn;
    }
  }

  return TRUE;
}

/**
 * @brief  Update a file; a no-op implementation.
 *
 * All files on the progress device will be opened by the RIP as write-only,
 * so there is no need to implement a comprehensive \c read_file call.
 * @param[in]      dev        Unused parameter.
 * @param[in]      descriptor Unused parameter.
 * @param[in]      bytes      Unused parameter.
 * @param[in]      reason     Unused parameter.
 * @return This function always sets an I/O error in the device, and returns FALSE.
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
 * @brief  Update a file; a no-op implementation.
 *
 * All files on the progress device will be opened by the RIP as write-only,
 * so there is no need to implement a comprehensive \c read_file call.
 * @param[in]      dev        Unused parameter.
 * @param[in]      filename   Unused parameter.
 * @param[in]      statbuff   Unused parameter.
 * @return This function always sets an I/O error in the device, and returns FALSE.
 */
static int32 RIPCALL progress_status_file (DEVICELIST* dev,
                                           uint8* filename,
                                           STAT* statbuff)
{
  UNUSED_PARAM (uint8*, filename);
  UNUSED_PARAM (STAT*, statbuff);
  return progress_ioerror (dev);
}

/**
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @param[in]      pattern    Unused parameter.
 * @return This function always clears the device error status, and returns a NULL void pointer.
 */
static void * RIPCALL progress_interp_start_file_list (DEVICELIST* dev, uint8* pattern)
{
  UNUSED_PARAM (uint8*, pattern);

  progress_noerror (dev);
  return (void*) NULL;
}

/**
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @param[in]      handle     Unused parameter.
 * @param[in]      pattern    Unused parameter.
 * @param[in]      entry      Unused parameter.
 * @return This function always clears the device error status, and returns \c FileNameNoMatch.
 */
static int32 RIPCALL progress_interp_next_file
(DEVICELIST* dev, void ** handle, uint8* pattern, FILEENTRY * entry)
{
  UNUSED_PARAM (void **, handle);
  UNUSED_PARAM (uint8*, pattern);
  UNUSED_PARAM (FILEENTRY *, entry);

  progress_noerror (dev);
  return FileNameNoMatch;
}

/**
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @param[in]      handle     Unused parameter.
 * @return This function always clears the device error status, and returns zero.
 */
static int32 RIPCALL progress_interp_end_file_list (DEVICELIST* dev, void * handle)
{
  UNUSED_PARAM (void **, handle);
  return progress_noerror (dev);
}

/**
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @param[in]      file1      Unused parameter.
 * @param[in]      file2      Unused parameter.
 * @return This function always sets an I/O error in the device, and returns -1.
 */
static int32 RIPCALL progress_rename_file (DEVICELIST* dev, uint8* file1, uint8* file2)
{
  UNUSED_PARAM (uint8*, file1);
  UNUSED_PARAM (uint8*, file2);
  return progress_ioerror (dev);
}

/**
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @param[in]      filename   Unused parameter.
 * @return This function always sets an I/O error in the device, and returns -1.
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
 * @param[in]      dev        The device instance.
 * @return This function always returns 1.
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
 * this Boolean value to \c TRUE will allow RIP timing information
 * to be passed to SkinMonitorl().
 * @param[in]      dev        The device instance.
 * @param[in]      param      The device parameter.
 * @return This function will return \c ParamAccepted if the parameter is
           successfully set, \c ParamTypeCheck if an inappropriate value is
           supplied for the parameter, or \c ParamIgnored an attempt is made
           to set an unknown parameter.
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
 * This routine serves two purposes: When the parameter name is \c NULL, it
 * to returns the next parameter in the device parameter list. \c pState->iParam keeps
 * track of how far through the list we are (though in this case there
 * is only one).  When the parameter name is not \c NULL, it gets the named parameter.
 * @param[in]      dev        The device instance.
 * @param[in,out]  param      The device parameter.  If NULL, the next parameter in the list
                              is returned.
 * @return This function will return \c ParamAccepted if the parameter is succesfully retrieved,
           or ParamIgnored if an unknown parameter is requested or the end of the parameter list
           is passed.
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
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @param[in]      devstat    Unused parameter.
 * @return This function always sets an I/O error in the device, and returns -1.
 */
static int32 RIPCALL progress_status_device (DEVICELIST* dev, DEVSTAT * devstat)
{
  UNUSED_PARAM (DEVSTAT *, devstat);
  return progress_ioerror (dev);
}

/**
 * @brief  Destroy state created in progress_init_device().
 * @param[in]      dev        The device instance.
 * @return This function always clears the error state in the device, and returns zero.
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
 * @brief  A no-op implementation.
 *
 * @param[in]      dev        Unused parameter.
 * @return This function always sets an I/O error in the device, and returns -1.
 */
static int32 RIPCALL progress_buffersize (DEVICELIST* dev)
{
  return progress_ioerror (dev);
}

/**
 * @brief  A no-op implementation.
 *
 * @return This function always returns -1.
 */
static int32 RIPCALL progress_void (void)
{
  return -1;
}

/**
 * @brief  The DEVICETYPE object describing the callbacks in this device.
 */
DEVICETYPE EBD_Progress_Device_Type =
{
  PROGRESS_DEVICE_TYPE, /* the device ID number */
  DEVICERELATIVE|DEVICEWRITABLE|DEVICESMALLBUFF|DEVICELINEBUFF,
  /* flags to indicate specifics of device */
  CAST_SIZET_TO_INT32(sizeof(ProgressDeviceState)), /* the size of the private data */
  0, /* minimum ticks between tickle functions */
  NULL, /* procedure to service the device */
  progress_lasterror, /* return last error for this device */
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

/**
 * Timeline generated event indicating something interesting has happened
 * for some progress activity.
 */
static sw_event_result HQNCALL oil_tl_progress(void *context, sw_event *evt)
{
  SWMSG_TIMELINE *timeline = evt->message;
  Hq32x2 pos;

  UNUSED_PARAM(void *, context);

  if ( timeline == NULL || evt->length < sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_CONTINUE;

  if ( timeline->type == SWTLT_RENDER_PAGE ) {
	  if(evt->type == EVENT_TIMELINE_ENDED)
	  {
		intptr_t pageno = (intptr_t)SwTimelineGetContext(timeline->ref, SW_RENDER_PAGE_NUMBER_CTXT);
		ebddev_EndRender((int)pageno);
	  }
  }
  else if ( timeline->type == SWTLT_FILE_INTERPRET ) {
    if(timeline->title != 0)
    {
      if(strcmp ((char *)(timeline->title), "%pipeline%/input") == 0)
      {
        Hq32x2FromDouble(&pos, timeline->progress);
        Stream_SetBytesConsumed( &pos );
      }
    }
  }
  return SW_EVENT_CONTINUE;
}

static sw_event_handlers oil_event_handlers[] = {
  { oil_tl_progress,     NULL, 0, EVENT_TIMELINE_ENDED, SW_EVENT_DEFAULT },
  { oil_tl_progress,     NULL, 0, EVENT_TIMELINE_ABORTED, SW_EVENT_DEFAULT },
};

int oil_progress_init(void)
{
  if ( SwRegisterHandlers(oil_event_handlers, NUM_ARRAY_ITEMS(oil_event_handlers)) != SW_RDR_SUCCESS )
    return FALSE ;

  return TRUE;
}

void oil_progress_finish(void)
{
  (void)SwDeregisterHandlers(oil_event_handlers, NUM_ARRAY_ITEMS(oil_event_handlers)) ;
}

