/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_sw.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * SW Folder zip access
 */

#include "core.h"

#include "mmcompat.h"         /* mm_alloc_static() */
#include "devices.h"          /* DEVICEPARAM */
#include "swcopyf.h"

#include "zip_sw.h"

#include "zipdev.h"
#include "swzip.h"            /* interface device type */

#include <string.h>

#define SWZIP_PREFIX        "%" SWZIP_READ_DEV_NAME "%"
#define SWZIP_DEFAULT_LEAF  "BootFile.bin"

/* The device from which we will attempt to read a zipped SW
   folder. */
static DEVICELIST *swzipreaddevice = NULL ;

/* Get the name of ZIP archive containing SW folder contents.
   Store result in szName if there is enough space allocated, otherwise
   return a default .*/
static uint8* getZipReadArchiveName(uint8* szName, size_t nBytes)
{
  char* szLeaf = zipArchiveName ? (char*) zipArchiveName : SWZIP_DEFAULT_LEAF;
  size_t nBytesRequired = sizeof(SWZIP_PREFIX) + strlen(szLeaf);

  if (nBytesRequired <= nBytes)
  {
    swcopyf(szName, (uint8*) "%s%s", SWZIP_PREFIX, szLeaf);
    return szName;
  }

  /* Use a default */
  return (uint8*) SWZIP_PREFIX SWZIP_DEFAULT_LEAF;
}


/* Make OS device a ZIP device. */
void zip_mount_os(void)
{
  uint8 buffer[256];
  uint8* szZipArchiveName;

  DEVICEPARAM p_readonly = {
    STRING_AND_LENGTH("ReadOnly"),
    ParamBoolean,
    NULL
  };
  DEVICEPARAM p_crc32 = {
    STRING_AND_LENGTH("CheckCRC32"),
    ParamBoolean,
    NULL
  };
  DEVICEPARAM p_filename = {
    STRING_AND_LENGTH("Filename"),
    ParamString,
    NULL
  };

  /* Mount new swzipread device so we can read the zipped SW folder */
  swzipreaddevice = device_alloc(STRING_AND_LENGTH(SWZIP_READ_DEV_NAME)) ;
  if ( !device_connect(swzipreaddevice, SWZIPREAD_DEVICE_TYPE, SWZIP_READ_DEV_NAME,
                       DEVICEUNDISMOUNTABLE|DEVICEENABLED, TRUE)) {
    device_free(swzipreaddevice) ;
    swzipreaddevice = NULL ;
  } else {
    /* Add ZIP read device. */
    device_add(swzipreaddevice);
  }

  /* If we have managed to mount an SW ZIP read device, assume that we
     MUST have a zipped SW folder to use. */
  if (swzipreaddevice != NULL) {
    /* Rename %os% to something else */
    theIDevName(osdevice) = (uint8*)SWZIP_WRITE_DEV_NAME;

    /* Mount new os device as ZIP device */
    if ( ! device_connect(osdevice, ZIP_DEVICE_TYPE, "os",
                          DEVICEUNDISMOUNTABLE|DEVICEENABLED, TRUE)) {
      device_free(swzipreaddevice) ;
      swzipreaddevice = NULL ;
      (void)dispatch_SwExit(swexit_error_zipsw_init, "Cannot initialise ZIP os device");
      return;
    }
    /* Make ZIP OS device first device */
    device_add_first(osdevice);

    /* NOTE: can't do this here since ZIP device calls SwOftenUnsafe which
     * depends on SystemParams, but that has not been set up yet!
     * Originally done in doBootup() which was last thing before interpreter
     * kicks off, so ideally need hook from there!
     */

    /* ZIP device is already mounted enabled - need to turn on checksum checking
     * and modifiable */
    theDevParamBoolean(p_crc32) = TRUE;
    if ( ((theISetParam(osdevice))(osdevice, &p_crc32) != ParamAccepted)) {
      (void)dispatch_SwExit(swexit_error_zipsw_config_01, "Failed to configure OS device(1)");
      return;
    }
    theDevParamBoolean(p_readonly) = FALSE;
    if ( ((theISetParam(osdevice))(osdevice, &p_readonly) != ParamAccepted )) {
      (void)dispatch_SwExit(swexit_error_zipsw_config_02, "Failed to configure OS device(2)");
      return;
    }

    szZipArchiveName = getZipReadArchiveName (buffer, sizeof (buffer));
    theDevParamString(p_filename) = szZipArchiveName;
    theDevParamStringLen(p_filename) = strlen_int32 ((char*) szZipArchiveName);
    if ( ((theISetParam(osdevice))(osdevice, &p_filename) != ParamAccepted ) ) {
      (void)dispatch_SwExit(swexit_error_zipsw_config_03, "Failed to configure OS device(3)");
      return;
    }

  } else {
    /* We did not manage to mount a SW ZIP read device, so just assume a
       normal SW folder. Do nothing. */
  }
} /* zip_mount_os */


/* Unmount the OS ZIP device if its been used. */
void zip_unmount_os(void)
{
  DEVICELIST *dev;

  DEVICEPARAM p_close = {
    STRING_AND_LENGTH("Close"),
    ParamBoolean,
    NULL
  };

  if (swzipreaddevice != NULL) {
    HQASSERT(osdevice != NULL, "osdevice is NULL");
    /* Do our best to unmount the os device. */
    ClearUndismountableDevice(osdevice);

    theDevParamBoolean(p_close) = TRUE;
    if ( (theISetParam(osdevice))(osdevice, &p_close) != ParamAccepted ) {
      HQFAIL("Unable to set Close parameter on zipped SW folder.");
    }

    /* Call the device dismount directly as PS semantics no longer
       apply. */
    if ((dev = find_device((uint8*)"os")) != NULL) {
      if ((*theIDevDismount( dev ))( dev ) == -1) {
        HQFAIL("Unable to dismount os device on zipped SW folder.");
      }
    }

    swzipreaddevice = NULL;
  }
}

void init_C_globals_zip_sw(void)
{
  swzipreaddevice = NULL ;
}

/* Log stripped */

/* eof zip_sw.c */
