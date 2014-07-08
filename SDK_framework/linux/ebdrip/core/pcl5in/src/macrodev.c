/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:macrodev.c(EBDSDK_P.1) $
 * $Id: src:macrodev.c,v 1.11.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 *
 * Implemention of a PCL5 macro reading device so that one can read
 * PCL5 macro streams via a FILELIST within the RIP. The macro stream
 * reading is layered above the macro implementation.
 */

#include "core.h"
#include "swdevice.h"
#include "lists.h"
#include "mm.h"
#include "gcscan.h"
#include "objects.h"
#include "objnamer.h"
#include "fileio.h"
#include "devices.h"
#include "devs.h"
#include "macrodev.h"
#include "macros.h"

#define PCL5MACRO_DEVICE_TYPE   (33)

#define PCL5_MACRO_DEVICE_OBJECT_NAME "PCL5 macro device"

#define PCL5_MACRO_DEVICE_NAME "_pcl5macro"

/* The device from which we will attempt to read all macros. */
DEVICELIST *pcl5_macrodevice = NULL ;

/* Maximum PCL5 macro device id. */
#define PCL5MACRO_DEVICE_MAX_ID  (0xff)

/* List of all mounted PCL5 macro devices.
 *
 * The list of Pcl5Macro devices is kept in increasing
 * PCL5MACRO_DEVICE::pcl5macro_id order. When a PCL5 macro device is
 * mounted, the smallest unused id is reused and the new device
 * inserted into the list in order.
 */
static dll_list_t dls_pcl5macrodevs ;

typedef struct PCL5MACRO_DEVICE {
  dll_link_t           link ;                  /* PCL5 macro device list link. */
  int32                pcl5macro_id ;          /* Mounted PCL5 macro device id. */
  PCL5Context          *pcl5_ctxt ;            /* PCL5 job context. */

  OBJECT_NAME_MEMBER
} PCL5MACRO_DEVICE ;

/* ============================================================================
 * PCL5 macro device
 * ============================================================================
 */
static int32 RIPCALL pcl5macro_init_device(DEVICELIST * dev)
{
  int32 pcl5macro_id;
  PCL5MACRO_DEVICE* pcl5macrodev = (PCL5MACRO_DEVICE*)dev->private_data;
  PCL5MACRO_DEVICE* pcl5macrodevT;

  devices_set_last_error(DeviceNoError);

  /* Name object as memory has been allocated - by the RIP */
  NAME_OBJECT(pcl5macrodev, PCL5_MACRO_DEVICE_OBJECT_NAME);

  /* Find unused PCL5 id to use. Devices are held in ascending id order. */
  pcl5macro_id = 0;
  pcl5macrodevT = DLL_GET_HEAD(&dls_pcl5macrodevs, PCL5MACRO_DEVICE, link);
  while ( (pcl5macrodevT != NULL) && (pcl5macro_id == pcl5macrodevT->pcl5macro_id) ) {
    pcl5macrodevT = DLL_GET_NEXT(pcl5macrodevT, PCL5MACRO_DEVICE, link);
    pcl5macro_id++;
  }
  if ( pcl5macro_id > PCL5MACRO_DEVICE_MAX_ID ) {
    devices_set_last_error(DeviceLimitCheck);
    return -1 ;
  }
  HQASSERT(((pcl5macrodevT == NULL) || (pcl5macro_id < pcl5macrodevT->pcl5macro_id)),
           "Active pcl5macro device list out of order");
  pcl5macrodev->pcl5macro_id = pcl5macro_id;
  /* Add device in sequence */
  DLL_RESET_LINK(pcl5macrodev, link);
  if ( pcl5macrodevT == NULL ) {
    DLL_ADD_TAIL(&dls_pcl5macrodevs, pcl5macrodev, link);
  } else {
    DLL_ADD_BEFORE(pcl5macrodevT, pcl5macrodev, link);
  }

  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL pcl5macro_open_file(DEVICELIST * dev,
                                                         uint8 * filename, int32 openflags)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(int32, openflags);
  devices_set_last_error(DeviceNoError);
  return 1;
}

/* I declare this here (rather than in macros.h) because I do not want
   people to call this function directly as it should only ever be
   used by the macro reading device. */
int32 Getc_macro(PCL5Context *pcl5_ctxt, DEVICE_FILEDESCRIPTOR descriptor) ;

static int32 RIPCALL pcl5macro_read_file(DEVICELIST * dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         uint8 * buf, int32 len)
{
  uint8* limit = buf + len ;
  uint8* insert = buf ;
  int32 ch ;
  PCL5MACRO_DEVICE *pcl5macrodev ;
  PCL5Context *pcl5_ctxt ;

  HQASSERT(dev != NULL, "dev is NULL") ;
  HQASSERT(buf != NULL, "buf is NULL") ;

  pcl5macrodev = (PCL5MACRO_DEVICE*)dev->private_data ;
  VERIFY_OBJECT(pcl5macrodev, PCL5_MACRO_DEVICE_OBJECT_NAME) ;
  pcl5_ctxt = pcl5macrodev->pcl5_ctxt ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  while (insert != limit) {
    if ( (ch = Getc_macro(pcl5_ctxt, descriptor)) == EOF)
      break ;
    *insert++ = (uint8)ch ;
  }
  len = CAST_PTRDIFFT_TO_INT32(insert - buf) ;

  devices_set_last_error(DeviceNoError) ;
  return len ;
}

static int32 RIPCALL pcl5macro_write_file(DEVICELIST *dev ,
                                          DEVICE_FILEDESCRIPTOR descriptor,
                                          uint8 *buf,
                                          int32 len)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buf);
  UNUSED_PARAM(int32, len);
  devices_set_last_error(DeviceNoError);
  return len;
}

static int32 RIPCALL pcl5macro_close_file(DEVICELIST *dev,
                                           DEVICE_FILEDESCRIPTOR descriptor)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  devices_set_last_error(DeviceNoError);
  if (descriptor != 1) {
    devices_set_last_error(DeviceIOError);
    return -1;
  }
  return 0;
}

static int32 RIPCALL pcl5macro_seek_file(DEVICELIST * dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2 * destn, int32 flags)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(Hq32x2 *, destn);
  UNUSED_PARAM(int32, flags);
  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL pcl5macro_bytes_file(DEVICELIST * dev,
                                          DEVICE_FILEDESCRIPTOR descriptor,
                                          Hq32x2 * bytes, int32 reason)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM( DEVICE_FILEDESCRIPTOR, descriptor );
  UNUSED_PARAM( Hq32x2 *, bytes );
  UNUSED_PARAM( int32, reason );

  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL pcl5macro_status_file(DEVICELIST * dev,
                                           uint8 * filename,
                                           STAT * statbuf)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *, statbuf);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static void * RIPCALL pcl5macro_start_list(DEVICELIST * dev,
                                           uint8 * pattern)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, pattern);
  devices_set_last_error(DeviceNoError);
  return NULL;
}

static int32 RIPCALL pcl5macro_next_list(DEVICELIST * dev, void ** handle,
                                         uint8 * pattern, FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL pcl5macro_end_list(DEVICELIST * dev, void * handle)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void *, handle);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL pcl5macro_rename_file(DEVICELIST * dev, uint8 * file1,
                                           uint8 * file2)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL pcl5macro_delete_file(DEVICELIST * dev, uint8 * filename)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL pcl5macro_set_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);
  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

static int32 RIPCALL pcl5macro_start_param(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL pcl5macro_get_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);
  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

static int32 RIPCALL pcl5macro_status_device(DEVICELIST * dev, DEVSTAT * devstat)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVSTAT *, devstat);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL pcl5macro_dismount(DEVICELIST * dev)
{
  PCL5MACRO_DEVICE* pcl5macrodev;
  HQASSERT((dev != NULL), "dev is NULL") ;

  pcl5macrodev = (PCL5MACRO_DEVICE*)(dev->private_data);
  VERIFY_OBJECT(pcl5macrodev, PCL5_MACRO_DEVICE_OBJECT_NAME) ;

  if ( isDeviceUndismountable(dev) ) {
    devices_set_last_error(DeviceIOError);
    return -1;
  } else {
    devices_set_last_error(DeviceNoError);

    /* Remove from list of PCL5 macro devices. */
    DLL_REMOVE(pcl5macrodev, link) ;

    return 0;
  }
}

static int32 RIPCALL pcl5macro_buffer_size(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL pcl5macro_spare(void)
{
  devices_set_last_error(DeviceIOError);
  return -1;
}

DEVICETYPE Pcl5Macro_Device_Type = {
  PCL5MACRO_DEVICE_TYPE,               /* the device ID number */
  DEVICESMALLBUFF ,                    /* flags to indicate specifics of device */
  sizeof(PCL5MACRO_DEVICE),            /* the size of the private data */
  0,                                   /* minimum ticks between tickle functions */
  NULL,                                /* procedure to service the device */
  devices_last_error,                  /* return last error for this device */
  pcl5macro_init_device,               /* call to initialise device */
  pcl5macro_open_file,                 /* call to open file on device */
  pcl5macro_read_file,                 /* call to read data from file on device */
  pcl5macro_write_file,                /* call to write data to file on device */
  pcl5macro_close_file,                /* call to close file on device */
  pcl5macro_close_file,                /* call to abort action on the device */
  pcl5macro_seek_file,                 /* call to seek file on device */
  pcl5macro_bytes_file,                /* call to get bytes avail on an open file */
  pcl5macro_status_file,               /* call to check status of file */
  pcl5macro_start_list,                /* call to start listing files */
  pcl5macro_next_list,                 /* call to get next file in list */
  pcl5macro_end_list,                  /* call to end listing */
  pcl5macro_rename_file,               /* rename file on the device */
  pcl5macro_delete_file,               /* remove file from device */
  pcl5macro_set_param,                 /* call to set device parameter */
  pcl5macro_start_param,               /* call to start getting device parameters */
  pcl5macro_get_param,                 /* call to get the next device parameter */
  pcl5macro_status_device,             /* call to get the status of the device */
  pcl5macro_dismount,                  /* call to dismount the device */
  pcl5macro_buffer_size,
  NULL,                                /* ignore ioctl calls */
  pcl5macro_spare                      /* spare slots */
} ;

/* ============================================================================
 * Mount a single PCL5 macro reading device. This is done at the
 * start/end of a PCL5 job because the device needs access to the PCL5
 * context.
 * ============================================================================
 */

Bool pcl5_mount_macrodev(PCL5Context *pcl5_ctxt)
{
  PCL5MACRO_DEVICE* pcl5macrodev;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Mount new swzipread device so we can read the zipped SW folder */
  pcl5_macrodevice = device_alloc(STRING_AND_LENGTH(PCL5_MACRO_DEVICE_NAME)) ;

  if (! device_connect(pcl5_macrodevice, PCL5MACRO_DEVICE_TYPE, PCL5_MACRO_DEVICE_NAME,
                       DEVICEUNDISMOUNTABLE|DEVICEENABLED, TRUE)) {
    device_free(pcl5_macrodevice) ;
    pcl5_macrodevice = NULL ;
    return FALSE ;
  }

  pcl5macrodev = (PCL5MACRO_DEVICE*)(pcl5_macrodevice->private_data);
  VERIFY_OBJECT(pcl5macrodev, PCL5_MACRO_DEVICE_OBJECT_NAME) ;
  pcl5macrodev->pcl5_ctxt = pcl5_ctxt ;

  return TRUE ;
}

/* Unmount the macro reading device. */
void pcl5_unmount_macrodev(PCL5Context *pcl5_ctxt)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  if (pcl5_macrodevice != NULL) {
    /* Do our best to unmount the PCL5 macro read device. */
    ClearUndismountableDevice(pcl5_macrodevice);

    /* Call the device dismount directly as PS semantics do not
       apply. */
    if ((*theIDevDismount( pcl5_macrodevice ))( pcl5_macrodevice ) == -1) {
      HQFAIL("Unable to dismount PCL5 macro read device.");
    }

    device_free(pcl5_macrodevice) ;
    pcl5_macrodevice = NULL;
  }
}

/* ============================================================================
 * PCL5 macro device init/finish. Executed at RIP startup/shutdown.
 * ============================================================================
 */
void init_C_globals_macrodev(void)
{
  pcl5_macrodevice = NULL ;
  DLL_RESET_LIST(&dls_pcl5macrodevs);
}

Bool pcl5_macrodev_init(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;

  DLL_RESET_LIST(&dls_pcl5macrodevs);

  if (! device_type_add(&Pcl5Macro_Device_Type))
    return FALSE ;

  return TRUE ;
}

void pcl5_macrodev_finish(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;
}

/* ============================================================================
* Log stripped */
