/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:monitor.c(EBDSDK_P.1) $
 * Monitor device
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */


/**
 * \file
 * \ingroup skinkit
 * \brief
   The monitor device type.  This implements standard input and output for the rip.

   In this case 'standard output' is the monitor callback function,
   if any, provided to SwLeStart().  'Standard input' is the PostScript
   provided by calls of SwLePs().
*/


#include "std.h"
#include "ripthread.h"
#include "swdevice.h"
#include "skindevs.h"
#include "swoften.h"
#include "kit.h"
#include "mem.h"

#include <stdio.h>

#include <string.h>

/* ---------------------------------------------------------------------- */

static int32 RIPCALL monitor_device_init( DEVICELIST *dev );
static DEVICE_FILEDESCRIPTOR RIPCALL monitor_open_file( DEVICELIST * dev,
                                                        uint8 *filename,
                                                        int32 openflags );
static int32 RIPCALL monitor_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                        uint8 *buff, int32 len );
static int32 RIPCALL monitor_write_file(DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                        uint8 *buff, int32 len );
static int32 RIPCALL monitor_close_file ( DEVICELIST *dev,
                                          DEVICE_FILEDESCRIPTOR descriptor ) ;
static int32 RIPCALL monitor_seek_file ( DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2 *destination,
                                         int32 flags);
static int32 RIPCALL monitor_bytes_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2 * bytes, int32 reason );
static int32 RIPCALL monitor_status_file( DEVICELIST *dev,
                                          uint8 *filename,
                                          STAT *statbuff );
static void* RIPCALL monitor_start_file_list( DEVICELIST *dev,
                                              uint8 *pattern);
static int32 RIPCALL monitor_next_file( DEVICELIST *dev,
                                        void **handle,
                                        uint8 *pattern,
                                        FILEENTRY *entry);
static int32 RIPCALL monitor_end_file_list( DEVICELIST *dev,
                                            void * handle);
static int32 RIPCALL monitor_rename_file( DEVICELIST *dev,
                                          uint8 *file1,
                                          uint8 *file2);
static int32 RIPCALL monitor_delete_file( DEVICELIST *dev,
                                          uint8 *filename );
static int32 RIPCALL monitor_set_param( DEVICELIST *dev,
                                        DEVICEPARAM *param);
static int32 RIPCALL monitor_start_param( DEVICELIST *dev );
static int32 RIPCALL monitor_get_param( DEVICELIST *dev,
                                        DEVICEPARAM *param);
static int32 RIPCALL monitor_status_device( DEVICELIST *dev,
                                            DEVSTAT *devstat);
static int32 RIPCALL monitor_noerror( DEVICELIST *dev );
static int32 RIPCALL monitor_ioerror( DEVICELIST *dev );
static int32 RIPCALL monitor_spare( void );
static void* RIPCALL monitor_void( DEVICELIST *dev );
static int32 RIPCALL monitor_dismount( DEVICELIST * dev );


/**
 * \brief  Structure to hold device-specific state.
 */
typedef struct MonitorDeviceState
{
  /** \brief The total bytes of postscript code read by the monitor device since
      it was opened. */
  Hq32x2 cbAppPSTotal;
} MonitorDeviceState;

/**
 * \brief Encapsulates the function entry points for the monitor device.
 */
DEVICETYPE Monitor_Device_Type = {
  MONITOR_DEVICE_TYPE,    /**< the device ID number */
  DEVICEWRITABLE | DEVICESMALLBUFF | DEVICELINEBUFF,
                          /**< flags to indicate specifics of device */
  sizeof (MonitorDeviceState),   /**< the size of the private data */
  0,                      /**< minimum ticks between tickle functions */
  NULL,                   /**< procedure to service the device */
  skindevices_last_error, /**< return last error for this device */
  monitor_device_init,    /**< call to initialise device */
  monitor_open_file,      /**< call to open file on device */
  monitor_read_file,      /**< call to read data from file on device */
  monitor_write_file,     /**< call to write data to file on device */
  monitor_close_file,     /**< call to close file on device */
  monitor_close_file,     /**< call to abort action on the device */
  monitor_seek_file,      /**< call to seek file on device */
  monitor_bytes_file,     /**< call to get bytes avail on an open file */
  monitor_status_file,    /**< call to check status of file */
  monitor_start_file_list,/**< call to start listing files */
  monitor_next_file,      /**< call to get next file in list */
  monitor_end_file_list,  /**< call to end listing */
  monitor_rename_file,    /**< rename file on the device */
  monitor_delete_file,    /**< remove file from device */
  monitor_set_param,      /**< call to set device parameter */
  monitor_start_param,    /**< call to start getting device parameters */
  monitor_get_param,      /**< call to get the next device parameter */
  monitor_status_device,  /**< call to get the status of the device */
  monitor_dismount,       /**< call to dismount the device */
  monitor_ioerror,        /**< call to determine buffer size */
  NULL,                   /**< monitor shouldn't need ioctl */
  monitor_spare           /**< spare slots */
};


static void monitor_set_lasterror( DEVICELIST *dev, int32 nError )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(nError);
}

static int32 RIPCALL monitor_device_init( DEVICELIST *dev )
{
  MonitorDeviceState* pDeviceState = (MonitorDeviceState*) dev->private_data;

  Hq32x2FromInt32(&pDeviceState->cbAppPSTotal, 0);
  monitor_set_lasterror(dev, DeviceNoError);

  return 0; /* success */
}

static int32 RIPCALL monitor_dismount( DEVICELIST * dev )
{
  /* The monitor device is dismountable at RIP shutdown. */
  if ( isDeviceUndismountable(dev) ) {
    monitor_set_lasterror(dev, DeviceIOError);
    return -1;
  } else {
    monitor_set_lasterror(dev, DeviceNoError);
    return 0;
  }
}

/** \brief The monitor open file call, the only different call for
    this device type. */
static DEVICE_FILEDESCRIPTOR RIPCALL monitor_open_file( DEVICELIST * dev,
                                                    uint8 *filename,
                                                    int32 openflags )
{
  MonitorDeviceState* pDeviceState = (MonitorDeviceState*) dev->private_data;

  UNUSED_PARAM(uint8 *, filename);

  if ( openflags & SW_RDONLY ) {
    /* stdin */
    Hq32x2FromInt32(&pDeviceState->cbAppPSTotal, 0);
    return 0;

  } else { /* stdout */
    return 1;
  }
}

/**
 * \brief monitor_read_file
 */
static int32 RIPCALL monitor_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                        uint8 *buff, int32 len )
{
  static Hq32x2 maxint32x2 = HQ32X2_INIT_MAX;
  Hq32x2 cbAppPS32x2;
  Hq32x2 cbAppPSLimit;
  MonitorDeviceState* pDeviceState = (MonitorDeviceState*) dev->private_data;
  uint8 * pAppPS;
  uint32  cbAppPS;

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  if ( !KGetJobData(len, &pAppPS, &cbAppPS) ) {
    return 0; /* EOF */
  }

  Hq32x2FromUint32(&cbAppPS32x2, cbAppPS);

  /* Check that the job data count running total can be updated without overflowing */
  Hq32x2Subtract(&cbAppPSLimit, &maxint32x2, &cbAppPS32x2);
  if ( Hq32x2Compare(&cbAppPSLimit, &pDeviceState->cbAppPSTotal) < 0 ) {
    monitor_set_lasterror(dev, DeviceIOError);
    return -1;
  }

  Hq32x2Add(&pDeviceState->cbAppPSTotal, &pDeviceState->cbAppPSTotal, &cbAppPS32x2);
  memcpy(buff, pAppPS, CAST_UNSIGNED_TO_SIZET(cbAppPS));
  return (int32)cbAppPS;
}

/**
 * \brief monitor_write_file
 */
static int32 RIPCALL monitor_write_file(DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                        uint8 *buff, int32 len )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  KCallMonitorCallback(len, buff);

  return len;
}


/**
 * \brief As we are using function prototypes where there used
 * to be pointers to any monitor_*error functions we
 * now need to wrap them in the appropriate function
 * pointers, in the cases where needed.
 */
static int32 RIPCALL monitor_close_file( DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  return monitor_noerror( dev );
}

/**
 * \brief monitor_seek_file
 */
static int32 RIPCALL monitor_seek_file( DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor,
                                        Hq32x2 *destination,
                                        int32 flags)
{
  MonitorDeviceState* pDeviceState = (MonitorDeviceState*) dev->private_data;

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  /* Special case for a zero destination */
  if ( Hq32x2IsZero (destination) )
  {
    switch (flags)
    {
    case SW_SET:
      /* We don't support seeking */
      return FALSE ;

    case SW_INCR:
      /* When asked for current position just return number of bytes returned since last open. */
      *destination = pDeviceState->cbAppPSTotal;
      break;

    case SW_XTND:
      /* Flush any pending input. */
      {
        uint8* pBuff;
        uint32 cbWritten;
        while (KGetJobData (1024, &pBuff, &cbWritten))
          ;
      }
      break;
    }

    return TRUE ;
  }

  (void)monitor_ioerror( dev );
  return FALSE ;
}

/**
 * \brief monitor_bytes_file
 */
static int32 RIPCALL monitor_bytes_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2 * bytes, int32 reason )
{
  if( descriptor == 0 )
  {
    /* stdin */
    if( reason == SW_BYTES_AVAIL_REL )
    {
      /* We indicate that bytes are immediately available, but not
         how many by setting *bytes to 0. */
      bytes->high = 0;
      bytes->low = 0;

      return TRUE;
    }
    else if( reason == SW_BYTES_TOTAL_ABS )
    {
      /* We don't know the total size of the stream */
      return FALSE;
    }
  }

  (void)monitor_ioerror( dev );
  return FALSE ;
}

/**
 * \brief monitor_status_file
 */
static int32 RIPCALL monitor_status_file( DEVICELIST *dev,
                                          uint8 *filename,
                                          STAT *statbuff )
{
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *, statbuff);

  return monitor_noerror( dev );
}

/**
 * \brief *monitor_start_file_list
 */
static void* RIPCALL monitor_start_file_list( DEVICELIST *dev,
                                              uint8 *pattern)
{
  UNUSED_PARAM(uint8 *, pattern);

  monitor_set_lasterror( dev, DeviceNoError );
  return monitor_void( dev );
}

/**
 * \brief monitor_next_file
 */
static int32 RIPCALL monitor_next_file( DEVICELIST *dev,
                                        void **handle,
                                        uint8 *pattern,
                                        FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  return FileNameNoMatch ;
}

/**
 * \brief monitor_end_file_list
 */
static int32 RIPCALL monitor_end_file_list( DEVICELIST *dev,
                                            void * handle)
{
  UNUSED_PARAM(void **, handle);

  return monitor_noerror ( dev );
}

/**
 * \brief monitor_rename_file
 */
static int32 RIPCALL monitor_rename_file( DEVICELIST *dev,
                                          uint8 *file1,
                                          uint8 *file2)
{
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  return monitor_ioerror ( dev );
}

/**
 * \brief monitor_delete_file
 */
static int32 RIPCALL monitor_delete_file( DEVICELIST *dev,
                                          uint8 *filename )
{
  UNUSED_PARAM(uint8 *, filename);

  return monitor_ioerror ( dev );
}

/**
 * \brief monitor_set_param
 */
static int32 RIPCALL monitor_set_param( DEVICELIST *dev,
                                        DEVICEPARAM *param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  return ParamIgnored ;
}

/**
 * \brief monitor_start_param
 */
static int32 RIPCALL monitor_start_param( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  return 0;
}

/**
 * \brief monitor_get_param
 */
static int32 RIPCALL monitor_get_param( DEVICELIST *dev,
                                        DEVICEPARAM *param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  return ParamIgnored;
}

/**
 * \brief monitor_status_device
 */
static int32 RIPCALL monitor_status_device( DEVICELIST *dev,
                                            DEVSTAT *devstat)
{
  UNUSED_PARAM(DEVSTAT *, devstat);

  return monitor_ioerror( dev );
}


/**
 * \brief monitor_noerror
 */
static int32 RIPCALL monitor_noerror ( DEVICELIST *dev )
{
  monitor_set_lasterror( dev, DeviceNoError );
  return 0;
}

/**
 * \brief monitor_ioerror
 */
static int32 RIPCALL monitor_ioerror ( DEVICELIST *dev )
{
  monitor_set_lasterror( dev, DeviceIOError );
  return -1;
}

/**
 * \brief monitor_spare
 */
static int32 RIPCALL monitor_spare ( void )
{
  return 0;
}

/**
 * \brief *monitor_void
 */
static void* RIPCALL monitor_void ( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  return NULL;
}

/* end of monitor.c */
