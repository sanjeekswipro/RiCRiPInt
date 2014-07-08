/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:file_devices.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Iterator functions to find a device to write extracted ZIP archive
 * files to.
 *
 * The ZIP device needs a mounted relative device to write the files extracted
 * from the ZIP archive to.  \c zipdev_device_first() returns the first
 * preferred device available that is mounted and can be used.
 *
 * It is possible for extracted files to persist across RIP reboots, should the
 * RIP quit suddenly.  On reboot, the RIP will try and delete any such files by
 * iterating over all the devices that could be used by the ZIP device and
 * deleting any files that appear to be extracted files on them.
 */

#include "core.h"

#include "devices.h"  /* find_device */

#include "zipdev.h"
#include "zip_sw.h"   /* OS_DEV_NAME */

/**
 * \brief Array of names of device that can be used to holding files extracted
 * from ZIP archives.
 *
 * The names are stored in preferred order of usage by the ZIP device - it will
 * use the first one that is mounted when the ZIP device opens the archive.
 * \internal
 */
static /*@observer@*/
uint8* zip_files_devicename[] = {
  (uint8*)SWZIP_WRITE_DEV_NAME,
  (uint8*)"tmp",
  (uint8*)"disk0",
  (uint8*)"os"
};

#define NUM_FILE_DEVICES  NUM_ARRAY_ITEMS(zip_files_devicename)

/*@null@*/
DEVICELIST* zipdev_device_first(
/*@out@*/ /*@notnull@*/
  ZIPDEV_DEVICE_ITERATOR* p_iter)
{
  HQASSERT((p_iter != NULL),
           "zipdev_device_first: NULL iterator pointer");

  /* Restart device list iterator */
  *p_iter = 0;

  return(zipdev_device_next(p_iter));

} /* zipdev_device_first */


/*@null@*/
DEVICELIST* zipdev_device_next(
/*@in@*/ /*@notnull@*/
  ZIPDEV_DEVICE_ITERATOR* p_iter)
{
  DEVICELIST* device;

  HQASSERT((p_iter != NULL),
           "zipdev_device_next: NULL iterator pointer");

  /* Return next mounted device in list */
  while ( *p_iter < NUM_FILE_DEVICES ) {
    device = find_device(zip_files_devicename[*p_iter]);
    *p_iter += 1;
    if ( device != NULL ) {
      return(device);
    }
  }

  return(NULL);

} /* zipdev_device_next */


/* Log stripped */
