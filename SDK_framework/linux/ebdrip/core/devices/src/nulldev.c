/** \file
 * \ingroup otherdevs
 *
 * $HopeName: COREdevices!src:nulldev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of the null device
 */


#include "core.h"
#include "swdevice.h"
#include "devices.h"
#include "devs.h"
#include "nulldev.h"

static int32 RIPCALL null_init_device(DEVICELIST * dev);
static DEVICE_FILEDESCRIPTOR RIPCALL null_open_file(DEVICELIST * dev,
                                                    uint8 * filename,
                                                    int32 openflags);
static int32 RIPCALL null_read_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8 * buff,
                                    int32 len);
static int32 RIPCALL null_write_file(DEVICELIST *dev ,
                                     DEVICE_FILEDESCRIPTOR descriptor ,
                                     uint8 *buff ,
                                     int32 len);
static int32 RIPCALL null_close_file(DEVICELIST *dev ,
                                     DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL null_seek_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * destn,
                                    int32 flags);
static int32 RIPCALL null_bytes_file(DEVICELIST * dev,
                                     DEVICE_FILEDESCRIPTOR descriptor,
                                     Hq32x2 * bytes,
                                     int32 reason);
static int32 RIPCALL null_status_file(DEVICELIST * dev,
                                      uint8 * filename,
                                      STAT * statbuff);
static void * RIPCALL null_start_list(DEVICELIST * dev,
                                      uint8 * pattern);
static int32 RIPCALL null_next_list(DEVICELIST * dev,
                                    void ** handle,
                                    uint8 * pattern,
                                    FILEENTRY *entry);
static int32 RIPCALL null_end_list(DEVICELIST * dev,
                                   void * handle);
static int32 RIPCALL null_rename_file(DEVICELIST * dev,
                                      uint8 * file1,
                                      uint8 * file2);
static int32 RIPCALL null_delete_file(DEVICELIST * dev,
                                      uint8 * filename);
static int32 RIPCALL null_set_param(DEVICELIST * dev,
                                    DEVICEPARAM * param);
static int32 RIPCALL null_start_param(DEVICELIST * dev);
static int32 RIPCALL null_get_param(DEVICELIST * dev,
                                    DEVICEPARAM * param);
static int32 RIPCALL null_status_device(DEVICELIST * dev,
                                        DEVSTAT * devstat);
static int32 RIPCALL null_dismount(DEVICELIST * dev);
static int32 RIPCALL null_buffer_size(DEVICELIST * dev);
static int32 RIPCALL null_spare( void );

/* ----------------------------------------------------------------------
The Null Device type (1), the implementation of which is internal to the rip
---------------------------------------------------------------------- */

static int32 RIPCALL null_init_device(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL null_open_file(DEVICELIST * dev,
                                                    uint8 * filename,
                                                    int32 openflags)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(int32, openflags);

  devices_set_last_error(DeviceNoError);
  return 1;
}

static int32 RIPCALL null_read_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8 * buff,
                                    int32 len)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buff);
  UNUSED_PARAM(int32, len);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL null_write_file(DEVICELIST *dev ,
                                     DEVICE_FILEDESCRIPTOR descriptor ,
                                     uint8 *buff ,
                                     int32 len)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buff);
  UNUSED_PARAM(int32, len);

  devices_set_last_error(DeviceNoError);
  return len;
}

static int32 RIPCALL null_close_file(DEVICELIST *dev,
                                     DEVICE_FILEDESCRIPTOR descriptor)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  if (descriptor != 1) {
    devices_set_last_error(DeviceIOError);
    return -1;
  }
  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL null_seek_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * destn,
                                    int32 flags)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(Hq32x2 *, destn);
  UNUSED_PARAM(int32, flags);

  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL null_bytes_file(DEVICELIST * dev,
                                     DEVICE_FILEDESCRIPTOR descriptor,
                                     Hq32x2 * bytes, int32 reason )
{
  UNUSED_PARAM( DEVICELIST *, dev );
  UNUSED_PARAM( DEVICE_FILEDESCRIPTOR, descriptor );
  UNUSED_PARAM( Hq32x2 *, bytes );
  UNUSED_PARAM( int32, reason );

  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL null_status_file(DEVICELIST * dev,
                                      uint8 * filename,
                                      STAT * statbuff)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *, statbuff);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static void * RIPCALL null_start_list(DEVICELIST * dev,
              uint8 * pattern)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, pattern);

  devices_set_last_error(DeviceNoError);
  return NULL;
}

static int32 RIPCALL null_next_list(DEVICELIST * dev,
              void ** handle,
              uint8 * pattern,
              FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL null_end_list(DEVICELIST * dev,
                                   void * handle)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void *, handle);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL null_rename_file(DEVICELIST * dev,
                                      uint8 * file1,
                                      uint8 * file2)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL null_delete_file(DEVICELIST * dev,
                                      uint8 * filename)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL null_set_param(DEVICELIST * dev,
                                    DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

static int32 RIPCALL null_start_param(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL null_get_param(DEVICELIST * dev,
              DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

static int32 RIPCALL null_status_device(DEVICELIST * dev,
                                        DEVSTAT * devstat)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVSTAT *, devstat);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL null_dismount(DEVICELIST * dev)
{
  /* The boot device is dismountable at RIP shutdown. */
  if ( isDeviceUndismountable(dev) ) {
    devices_set_last_error(DeviceIOError);
    return -1;
  } else {
    devices_set_last_error(DeviceNoError);
    return 0;
  }
}

static int32 RIPCALL null_buffer_size(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL null_spare( void )
{
  devices_set_last_error(DeviceIOError);
  return -1;
}

DEVICETYPE Null_Device_Type = {
  NULL_DEVICE_TYPE,               /* the device ID number */
  DEVICESMALLBUFF ,               /* flags to indicate specifics of device */
  0,                              /* the size of the private data */
  0,                              /* minimum ticks between tickle functions */
  NULL,                           /* procedure to service the device */
  devices_last_error,             /* return last error for this device */
  null_init_device,               /* call to initialise device */
  null_open_file,                 /* call to open file on device */
  null_read_file,                 /* call to read data from file on device */
  null_write_file,                /* call to write data to file on device */
  null_close_file,                /* call to close file on device */
  null_close_file,                /* call to abort action on the device */
  null_seek_file,                 /* call to seek file on device */
  null_bytes_file,                /* call to get bytes avail on an open file */
  null_status_file,               /* call to check status of file */
  null_start_list,                /* call to start listing files */
  null_next_list,                 /* call to get next file in list */
  null_end_list,                  /* call to end listing */
  null_rename_file,               /* rename file on the device */
  null_delete_file,               /* remove file from device */
  null_set_param,                 /* call to set device parameter */
  null_start_param,               /* call to start getting device parameters */
  null_get_param,                 /* call to get the next device parameter */
  null_status_device,             /* call to get the status of the device */
  null_dismount,                  /* call to dismount the device */
  null_buffer_size,
  NULL,                           /* ignore ioctl calls */
  null_spare                      /* spare slots */
};

/* Log stripped */
