/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWpdfspool!src:spooldev.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup pdfspool
 * \brief Implementation of PDF spooler device type.
 *
 * This PS device is used to present spooled PDF job data to the RIP.
 * The link to a spooled PDF is made by calling spooldev_set_spoolfile().
 * This spooled PDF will be used next time device is opened.
 *
 * To add the device to the ExtraDevices file:
 *
 * (%pdfspool%) dup devmount pop
 * <<
 *   /Password 0
 *   /DeviceType 16#ffff1000
 *   /Enable true
 * >> setdevparams
 *
 *
 * To invoke the RIP to process a spooled PDF:
 *
 * (%pdfspool%) (r) file pdfexec
 */

#include <string.h>

#include "spooler.h"
#include "pdfspool.h"
#include "spooldev.h"


/* Helper macro for initialising device parameter names */
#define PARAM_NAME(s)   (uint8*)"" s "", sizeof("" s "") - 1

/* Default device parameters */
static
DEVICEPARAM default_params[] = {
  {PARAM_NAME("Type"), ParamString, NULL, 0}
};

/* Index of device parameter */
#define SPOOLDEV_PARAM_TYPE  (0)

/* Total number of device parameters */
#define NUM_DEVICE_PARAMS   (sizeof(default_params)/sizeof(default_params[0]))

static uint8* type = (uint8*)"FileSystem";


typedef struct SPOOLER_DEVICE {
  int32       last_error;   /* Last device error set */

  HqBool      device_open;  /* Device is open for reading */
  HqBool      read_error;   /* There was an error while reading the spooled PDF data */

  SPOOLER*    spooler;      /* Spooled PDF data */

  int32       next_param;   /* Device parameter enumeration index */
  DEVICEPARAM params[NUM_DEVICE_PARAMS];  /* Device parameters */
} SPOOLER_DEVICE;


/* Use a pseudo non-obvious value for valid file descriptors */
#define SPOOLDEV_FD    (0x01020304)


/* Spooled data to use the next time the device is opened */
static SPOOLER* dev_spooler;


/** \brief   Set the spool file for the spool device to read */
void spooldev_set_spoolfile(
  SPOOLER*  spooler)
{
  dev_spooler = spooler;

} /* spooldev_set_spoolfile */


/** \brief
 * The last error call for the PDF spooler device type.
 */
static
int32 RIPCALL spooldev_last_error(
  DEVICELIST* dev)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  return(p_spooldev->last_error);

} /* spooldev_last_error */


/** \brief
 * PDF spooler device initialisation. See PR5.8.7. This is called for each
 * device (note: not device type) when its type number is assigned by
 * a call to setdevparams.
 */
static
int32 RIPCALL spooldev_device_init(
  DEVICELIST* dev)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Initialise device state */
  p_spooldev->last_error = DeviceNoError;

  p_spooldev->device_open = FALSE;
  p_spooldev->read_error = FALSE;

  p_spooldev->spooler = NULL;

  memcpy(p_spooldev->params, default_params, sizeof(default_params));
  p_spooldev->params[SPOOLDEV_PARAM_TYPE].paramval.strval = type;
  p_spooldev->params[SPOOLDEV_PARAM_TYPE].strvallen = sizeof(type) - 1;

  return(0);

} /* spooldev_device_init */


/** \brief
 * The file_open call for the PDF spooler device type. See PR 5.8.8.
 * Open the PDF spooler device to read PDF job data.
 */
static
DEVICE_FILEDESCRIPTOR RIPCALL spooldev_open_file(
  DEVICELIST* dev,
  uint8*      filename,
  int32       openflags)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Since the device is an absolute device, there should never be a filename */
  UNUSED_PARAM(uint8*, filename);

  /* The device only supports being opened once at a time */
  if ( p_spooldev->device_open ) {
    p_spooldev->last_error = DeviceIOError;
    return(-1);
  }

  /* The device does not support writing, so report invalid open flags if not
   * just reading */
  if ( openflags & (SW_WRONLY|SW_RDWR) ) {
    p_spooldev->last_error = DeviceInvalidAccess;
    return(-1);
  }

  /* Pick up spooled PDF data to read */
  p_spooldev->spooler = dev_spooler;
  if ( p_spooldev->spooler == NULL ) {
    p_spooldev->last_error = DeviceIOError;
    return(-1);
  }
  dev_spooler = NULL;

  p_spooldev->device_open = TRUE;

  return(SPOOLDEV_FD);

} /* spooldev_open_file */


/** \brief   The read_file routine for the PDF spooler device type. See PR 5.8.9.
 *
 * Read spooled PDF job data described by `descriptor' which is on the device
 * `dev' into the buffer `buff'.  For this device `descriptor' is a fixed value
 * as only one file can be open at once.
 */
static
int32 RIPCALL spooldev_read_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  uint8*      buff,
  int32       len)
{
  int32 status;
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Catch invalid descriptor, the device is not currently open, or there has
   * been an error reading the PDF data  */
  if ( !p_spooldev->device_open || (descriptor != SPOOLDEV_FD) ||
       p_spooldev->read_error ) {
    p_spooldev->last_error = DeviceIOError;
    return(-1);
  }

  /* Read spooled PDF data */
  status = spooler_read_pdf(p_spooldev->spooler, buff, len);
  if ( status < 0 ) {
    p_spooldev->last_error = DeviceIOError;
    p_spooldev->read_error = TRUE;
  }

  return(status);

} /* spooldev_read_file */


/** \brief   The write_file routine for the PDF spooler device type. See PR 5.8.10.
 *
 * This device does not support write operations.
 */
static
int32 RIPCALL spooldev_write_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  uint8*      buff,
  int32       len)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8*, buff);
  UNUSED_PARAM(int32, len);

  /* Attempting to write to the spool device is always an error, even if the
   * device has not been opened or the descriptor is invalid.
   */
  p_spooldev->last_error = DeviceIOError;
  return(-1);

} /* spooldev_write_file */


/** \brief   The close_file routine for the PDF spooler device type. See PR 5.8.11.
 *
 * Close open PDF job data stream.
 */
static
int32 RIPCALL spooldev_close_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor)
{
  int32 status;
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Check that the device has actually been opened */
  if ( !p_spooldev->device_open || (descriptor != SPOOLDEV_FD) ) {
    p_spooldev->last_error = DeviceIOError;
    return(-1);
  }

  /* Flag that the device no longer open for reading PDF data and reset read
   * error flag.
   */
  p_spooldev->device_open = FALSE;
  p_spooldev->read_error = FALSE;

  /* Tell the underlying spooler that the device has finished with the PDF data */
  status = spooler_close_pdf(p_spooldev->spooler);
  p_spooldev->spooler = NULL;

  if ( !status ) {
    p_spooldev->last_error = DeviceIOError;
  }

  return(status);

} /* spooldev_close_file */


/** \brief   The abort_file routine for the PDF spooler device type. See PR 5.8.12.
 *
 * Abort spooled PDF job data stream.
 */
static
int32 RIPCALL spooldev_abort_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor)
{
  /* Same as closing the file with this implementation */
  return(spooldev_close_file(dev, descriptor));

} /* spooldev_abort_file */


/** \brief   The seek_file routine for the PDF spooler device type. See PR 5.8.13.
 *
 * Seek on the spooled PDF job data.
 */
static
int32 RIPCALL spooldev_seek_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  Hq32x2*     destn,
  int32       flags)
{
  int32 status;
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Catch invalid descriptor, the device is not currently open, or there has
   * been an error reading the PDF data  */
  if ( !p_spooldev->device_open || (descriptor != SPOOLDEV_FD) ||
       p_spooldev->read_error ) {
    p_spooldev->last_error = DeviceIOError;
    return(FALSE);
  }

  /* Seek on the spooled PDF data */
  status = spooler_seek_pdf(p_spooldev->spooler, destn, flags);
  if ( !status ) {
    p_spooldev->last_error = DeviceIOError;
    p_spooldev->read_error = TRUE;
  }

  return(status);

} /* spooldev_seek_file */


/** \brief   The bytes_file routine for the PDF spooler device type. See 5.8.14.
 *
 * Return the size of spooled PDF job data.
 */
static
int32 RIPCALL spooldev_bytes_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  Hq32x2*     bytes,
  int32       reason)
{
  int32 status;
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Catch invalid descriptor or the device is not currently open */
  if ( !p_spooldev->device_open || (descriptor != SPOOLDEV_FD) ) {
    p_spooldev->last_error = DeviceIOError;
    return(FALSE);
  }

  /* If there has been an error while reading PDF data then querying how much
   * data is available to read can not be done.
   */
  if ( (reason == SW_BYTES_AVAIL_REL) && p_spooldev->read_error ) {
    p_spooldev->last_error = DeviceIOError;
    return(FALSE);
  }

  status = spooler_bytes_pdf(p_spooldev->spooler, bytes, reason);
  if ( !status ) {
    p_spooldev->last_error = DeviceIOError;
  }

  return(status);

} /* spooldev_bytes_file */


/** \brief   The status_file routine for the PDF spooler device type. See 5.8.15
 *
 * PDF spooler device file status - never called for absolute devices
 */
static
int32 RIPCALL spooldev_status_file(
  DEVICELIST* dev,
  uint8*      filename,
  STAT*       statbuff)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Since the device is an absolute device, there is never be a filename */
  UNUSED_PARAM(uint8*, filename);
  UNUSED_PARAM(STAT*, statbuff);

  p_spooldev->last_error = DeviceIOError;
  return(-1);

} /* spooldev_status_file */


/** \brief   Initialise PDF spooler device file iterator. */
static
void* RIPCALL spooldev_start_file_list(
  DEVICELIST* dev,
  uint8*      pattern)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  UNUSED_PARAM(uint8*, pattern);

  /* The RIP should not try get a list of files from the device, but in case it
   * does the following effectively tells the RIP there are no files matching the
   * pattern. */
  p_spooldev->last_error = DeviceNoError;
  return(NULL);

} /* spooldev_start_file_list */


/** \brief   Return next file on PDF spooler device - should never be called. */
static
int32 RIPCALL spooldev_next_file(
  DEVICELIST* dev,
  void**      handle,
  uint8*      pattern,
  FILEENTRY*  entry)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(void**, handle);
  UNUSED_PARAM(uint8*, pattern);
  UNUSED_PARAM(FILEENTRY*, entry);

  /* From the previous function, this one should not be called by the RIP, but
   * just in case indicate there is no match */
  return(FileNameNoMatch);

} /* spooldev_next_file */


/** \brief   Tidy up PDF spooler device file iterator - should never be called. */
static
int32 RIPCALL spooldev_end_file_list(
  DEVICELIST* dev,
  void*       handle)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(void*, handle);

  /* Should not be called, and nothing to be done anyway */
  return(0);

} /* spooldev_end_file_list */


/** \brief   PDF spooler device file rename - should never be called. */
static
int32 RIPCALL spooldev_rename_file(
  DEVICELIST* dev,
  uint8*      file1,
  uint8*      file2)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Since the device is an absolute device, there is never a file to rename */
  UNUSED_PARAM(uint8*, file1);
  UNUSED_PARAM(uint8*, file2);

  p_spooldev->last_error = DeviceInvalidAccess;
  return(-1);

} /* spooldev_rename_file */


/** \brief    PDF spooler device file delete - should never be called. */
static
int32 RIPCALL spooldev_delete_file(
  DEVICELIST* dev,
  uint8*      filename)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Since the device is an absolute device, there is never a file to delete */
  UNUSED_PARAM(uint8*, filename);

  p_spooldev->last_error = DeviceInvalidAccess;
  return(-1);

} /* spooldev_delete_file */


/** \brief   The set_param routine for the PDF spooler device type. See PR 5.8.21.
 *
 * Set PDF spooler device parameter
 */
static
int32 RIPCALL spooldev_set_param(
  DEVICELIST*   dev,
  DEVICEPARAM*  param)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* The Type device parameter is read only, so it is an error to try and set it */
  if ( (param->paramnamelen == p_spooldev->params[SPOOLDEV_PARAM_TYPE].paramnamelen) &&
       (memcmp(param->paramname, p_spooldev->params[SPOOLDEV_PARAM_TYPE].paramname,
               param->paramnamelen) == 0) ) {
    return(ParamConfigError);
  }

  /* Ignore all other parameters */
  return(ParamIgnored);

} /* spooldev_set_param */


/** \brief   The start_param routine for the PDF spooler device type. See 5.8.22.
 *
 * The routine is called as part of the currentdevparams operator; the
 * individual parameters are returned one at a time by subsequent calls to
 * the get_param function. sock_param_count is used to maintain
 * the state between calls to get_param.
 *
 *  Also return the number of parameters recognized by this implementation
 * of the PDF spooler device type.
 */
static
int32 RIPCALL spooldev_start_param(
  DEVICELIST* dev)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* Reset enumerator to first parameter */
  p_spooldev->next_param = 0;

  return(NUM_DEVICE_PARAMS);

} /* spooldev_start_param */


/* \brief   The get_param routine for the PDF spooler device type. See PR 5.8.23
 *
 * This routine serves two purposes: when the parameter name is NULL, it is
 * to return the next device parameter - next_param keeps
 * track of how far through the list we are, though in this case there
 * is only one - otherwise it should return the one called for by the name.
 */
static
int32 RIPCALL spooldev_get_param(
  DEVICELIST*   dev,
  DEVICEPARAM*  param)
{
  int32 i;
  DEVICEPARAM*  p_param;
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  if ( param->paramname == NULL ) {
    /* Iterating over device parameters */
    if ( p_spooldev->next_param > NUM_DEVICE_PARAMS ) {
      return(0);
    }
    *param = p_spooldev->params[p_spooldev->next_param];
    p_spooldev->next_param++;
    return(ParamAccepted);
  }

  /* Looking for a specific device parameter */
  for ( i = 0; i < NUM_DEVICE_PARAMS; i++ ) {
    p_param = &p_spooldev->params[i];
    if ( (param->paramnamelen == p_param->paramnamelen) &&
         (memcmp(param->paramname, p_param->paramname, p_param->paramnamelen) == 0) ) {
      *param = *p_param;
      return(ParamAccepted);
    }
  }

  return(ParamIgnored);

} /* spooldev_get_param */


/** \brief   The status_device routine for the PDF spooler device type.
 *
 * Reports details of the device holding the spooled data.
 */
static
int32 RIPCALL spooldev_status_device(
  DEVICELIST* dev,
  DEVSTAT*    devstat)
{
  int32 status;
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* If the device is not open then there is no underlying device with the
   * spooled PDF data to query.
   */
  if ( !p_spooldev->device_open ) {
    p_spooldev->last_error = DeviceIOError;
    return(-1);
  }

  /* The spooler code reports details of the low level device actually
   * holding the spooled PDF data.
   */
  status = spooler_status_device(p_spooldev->spooler, devstat);
  if ( status != 0 ) {
    p_spooldev->last_error = DeviceIOError;
  }

  return(status);

} /* spooldev_status_device */


/** /brief   Dismount PDF spooler device */
static
int32 RIPCALL spooldev_dev_dismount(
  DEVICELIST* dev)
{
  SPOOLER_DEVICE* p_spooldev = (SPOOLER_DEVICE*)dev->private_data;

  /* The device should be closed before dismounting */
  if ( p_spooldev->device_open ) {
    p_spooldev->last_error = DeviceIOError;
    return(-1);
  }

  return(0);

} /* spooldev_dev_dismount */


/** /brief   Return suggested buffer size to use when accessing the PDF spooler device */
static
int32 RIPCALL spooldev_buffer_size(
  DEVICELIST* dev)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  /* The RIP will use a default buffer size with the spooler device */
  return(-1);

} /* spooldev_buffer_size */


/** /brief   PDF spooler device ioctl function. */
static
int32 RIPCALL spooldev_ioctl(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  int32       opcode,
  intptr_t    arg)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);
  UNUSED_PARAM(int32, opcode);
  UNUSED_PARAM(intptr_t, arg);

  /* Do not expect to be called, but flag an error if it is */
  return(-1);

} /* spooldev_ioctl */


/* Device type number - example uses OEM internal range, OEM expected to use OEM
 * specific device type number */
#define OEM_INTERNAL_DEVICE_TYPE  (0xffff0000)
/* Have to use a higher end OEM device number as skinkit devices use the low
 * end */
#define SPOOLER_DEVICE_TYPE       (OEM_INTERNAL_DEVICE_TYPE | 0x1000 | 0)

/* device flags of 0 implies absolute (no filenames) read only device */

/**
 * \brief The device type structure for the PDF spooler device. This
 * is a specific example of the structure defined in Programmer's
 * Reference manual section 5.8.
 */
DEVICETYPE PDF_Spooler_Device_Type = {
  SPOOLER_DEVICE_TYPE,              /* the device ID number */
  0,                                /* flags to indicate specifics of device */
  sizeof(SPOOLER_DEVICE),           /* the size of the private_data */
  0,                                /* minimum ticks between tickle functions */
  NULL,                             /* procedure to service the device */
  spooldev_last_error,              /* return last error for this device */
  spooldev_device_init,             /* call to initialise device */
  spooldev_open_file,               /* call to open file on device */
  spooldev_read_file,               /* call to read data from file on device */
  spooldev_write_file,              /* call to write data to file on device */
  spooldev_close_file,              /* call to close file on device */
  spooldev_abort_file,              /* call to abort action on the device */
  spooldev_seek_file,               /* call to seek file on device */
  spooldev_bytes_file,              /* call to get bytes avail on an open file */
  spooldev_status_file,             /* call to check status of file */
  spooldev_start_file_list,         /* call to start listing files */
  spooldev_next_file,               /* call to get next file in list */
  spooldev_end_file_list,           /* call to end listing */
  spooldev_rename_file,             /* rename file on the device */
  spooldev_delete_file,             /* remove file from device */
  spooldev_set_param,               /* call to set device parameter */
  spooldev_start_param,             /* call to start getting device parameters */
  spooldev_get_param,               /* call to get the next device parameter */
  spooldev_status_device,           /* call to get the status of the device */
  spooldev_dev_dismount,            /* call to dismount the device */
  spooldev_buffer_size,             /* call to determine buffer size */
  spooldev_ioctl,                   /* call to allow low level control of the device */
  NULL                              /* spare slots */
};

/* eof */
