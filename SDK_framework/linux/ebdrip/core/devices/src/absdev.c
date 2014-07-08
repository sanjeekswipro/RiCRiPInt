/** \file
 * \ingroup filedevs
 *
 * $HopeName: COREdevices!src:absdev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The Abs Device type (10), the implementation of which is internal to the rip
 */

#include "core.h"
#include "swdevice.h"
#include "devices.h"
#include "devs.h"
#include "absdev.h"

static int32 RIPCALL abs_init_device(DEVICELIST * dev);
static DEVICE_FILEDESCRIPTOR RIPCALL abs_open_file(DEVICELIST * dev,
                                                   uint8 * filename,
                                                   int32 openflags);
static int32 RIPCALL abs_read_file(DEVICELIST * dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   uint8 * buff,
                                   int32 len);
static int32 RIPCALL abs_write_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8 * buff,
                                    int32 len);
static int32 RIPCALL abs_close_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor);
static int32 RIPCALL abs_abort_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor);
static int32 RIPCALL abs_seek_file(DEVICELIST * dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 * destn,
                                   int32 flags);
static int32 RIPCALL abs_bytes_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * bytes,
                                    int32 reason);
static int32 RIPCALL abs_status_file(DEVICELIST * dev,
                                     uint8 * filename,
                                     STAT * statbuff);
static void * RIPCALL abs_start_list(DEVICELIST * dev,
                                     uint8 * pattern);
static int32 RIPCALL abs_next_list(DEVICELIST * dev,
                                   void ** handle,
                                   uint8 * pattern,
                                   FILEENTRY *entry);
static int32 RIPCALL abs_end_list(DEVICELIST * dev,
                                  void * handle);
static int32 RIPCALL abs_rename_file(DEVICELIST * dev,
                                     uint8 * file1,
                                     uint8 * file2);
static int32 RIPCALL abs_delete_file(DEVICELIST * dev,
                                     uint8 * filename);
static int32 RIPCALL abs_set_param(DEVICELIST * dev,
                                   DEVICEPARAM * param);
static int32 RIPCALL abs_start_param(DEVICELIST * dev);
static int32 RIPCALL abs_get_param(DEVICELIST * dev,
                                   DEVICEPARAM * param);
static int32 RIPCALL abs_status_device(DEVICELIST * dev,
                                       DEVSTAT * devstat);
static int32 RIPCALL abs_dismount(DEVICELIST * dev);
static int32 RIPCALL abs_buffer_size(DEVICELIST * dev);
static int32 RIPCALL abs_spare( void );

/* ----------------------------------------------------------------------
The Abs Device type (10), the implementation of which is internal to the rip
---------------------------------------------------------------------- */

static int32 RIPCALL abs_init_device(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  return 0;
}

/* ---------------------------------------------------------------------- */
static DEVICE_FILEDESCRIPTOR RIPCALL abs_open_file(DEVICELIST *dev,
                                                   uint8 * filename,
                                                   int32 openflags)
{
  DEVICE_FILEDESCRIPTOR res;

  UNUSED_PARAM(uint8 *, filename);

  if ( (res = (* theIOpenFile(osdevice)) (osdevice, theIDevName (dev), openflags)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_read_file(DEVICELIST *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   uint8 * buff, int32 len)
{
  int32 res;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( (res = (* theIReadFile(osdevice)) (osdevice, descriptor, buff, len)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_write_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8 * buff, int32 len)
{
  int32 res;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( (res = (* theIWriteFile(osdevice)) (osdevice, descriptor, buff, len) ) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_close_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor)
{
  int32 res;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( (res = (* theICloseFile(osdevice)) (osdevice, descriptor)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_abort_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor)
{
  int32 res;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( (res = (* theIAbortFile(osdevice)) (osdevice, descriptor)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_seek_file(DEVICELIST *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 * destn, int32 flags)
{
  int32 res;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( (res = (* theISeekFile(osdevice)) (osdevice, descriptor, destn, flags)) == FALSE )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_bytes_file(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 * bytes, int32 reason )
{
  int32 res;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( (res = (* theIBytesFile(osdevice)) (osdevice, descriptor, bytes, reason )) == FALSE )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_status_file(DEVICELIST * dev,
                                     uint8 * filename, STAT * statbuff)
{
  int32 res;

  UNUSED_PARAM(uint8 *, filename);

  if ( (res = (* theIStatusFile(osdevice)) (osdevice, theIDevName (dev), statbuff)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static void * RIPCALL abs_start_list(DEVICELIST * dev, uint8 * pattern)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, pattern);

  devices_set_last_error(DeviceNoError);
  return NULL;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_next_list(DEVICELIST * dev, void ** handle,
                                   uint8 * pattern, FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_end_list(DEVICELIST * dev, void * handle)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void *, handle);

  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_rename_file(DEVICELIST * dev,
                                     uint8 * file1, uint8 * file2)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_delete_file(DEVICELIST * dev, uint8 * filename)
{
  int32 res;

  UNUSED_PARAM(uint8 *, filename);

  if ( (res = (* theIDeleteFile(osdevice)) (osdevice, theIDevName (dev))) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_set_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_start_param(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  return 0;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_get_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_status_device(DEVICELIST * dev, DEVSTAT * devstat)
{
  int32 res;

  if ( (res = (* theIStatusDevice(osdevice)) (osdevice, devstat)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_dismount(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_buffer_size(DEVICELIST * dev)
{
  int32 res;

  if ( (res = (* theIGetBuffSize(osdevice)) (osdevice)) == -1 )
    devices_set_last_error((*theILastErr(osdevice)) (dev));
  return res;
}

/* ---------------------------------------------------------------------- */
static int32 RIPCALL abs_spare( void )
{
  devices_set_last_error(DeviceIOError);
  return -1;
}

/* ---------------------------------------------------------------------- */
DEVICETYPE Abs_Device_Type = {
  ABS_DEVICE_TYPE,                /* the device ID number */
  DEVICEWRITABLE,                 /* flags to indicate specifics of device */
  0,                              /* the size of the private data */
  0,                              /* minimum ticks between tickle functions */
  NULL,                           /* procedure to service the device */
  devices_last_error,             /* return last error for this device */
  abs_init_device,                /* call to initialise device */
  abs_open_file,                  /* call to open file on device */
  abs_read_file,                  /* call to read data from file on device */
  abs_write_file,                 /* call to write data to file on device */
  abs_close_file,                 /* call to close file on device */
  abs_abort_file,                 /* call to abort action on the device */
  abs_seek_file,                  /* call to seek file on device */
  abs_bytes_file,                 /* call to get bytes avail on an open file */
  abs_status_file,                /* call to check status of file */
  abs_start_list,                 /* call to start listing files */
  abs_next_list,                  /* call to get next file in list */
  abs_end_list,                   /* call to end listing */
  abs_rename_file,                /* rename file on the device */
  abs_delete_file,                /* remove file from device */
  abs_set_param,                  /* call to set device parameter */
  abs_start_param,                /* call to start getting device parameters */
  abs_get_param,                  /* call to get the next device parameter */
  abs_status_device,              /* call to get the status of the device */
  abs_dismount,                   /* call to dismount the device */
  abs_buffer_size,
  NULL,                         /* ignore ioctl calls */
  abs_spare                       /* spare slots */
};

/* Log stripped */
