/** \file
 * \ingroup otherdevs
 *
 * $HopeName: COREdevices!src:calendar.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2000-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to %Calendar% device. See Postscript Language
 * Reference Supplement for Version 2 (Pages 150-151).
 */

/* ----------------------------- Includes ---------------------------------- */

#include "core.h"
#include "hqmemcmp.h"
#include "swdevice.h"
#include "swerrors.h"
#include "devices.h"

#include "calendar.h"

#define CAL_RUNNING   (0x01)
#define CAL_YEAR      (0x02)
#define CAL_MONTH     (0x04)
#define CAL_DAY       (0x08)
#define CAL_HOUR      (0x10)
#define CAL_MINUTE    (0x20)
#define CAL_SECOND    (0x40)

#define CAL_ALL_PRESENT (CAL_RUNNING | CAL_YEAR | CAL_MONTH | CAL_DAY | CAL_HOUR | CAL_MINUTE | CAL_SECOND)

/* ----------------------------- External Definitions ---------------------- */

/* Get all the device parameters from the %Calendar% device */

Bool get_calendar_params(int32 *year, int32 *month, int32 *day, int32 *hour,
                         int32 *minute, int32 *second, int32 *running)
{
  DEVICELIST *calendardev;
  DEVICEPARAM param;
  int32 numparams, iparam, paramlen;
  uint8 *paramname;
  int32 param_flags = 0;

  /* Extract calendar device from list */
  if (( calendardev = find_device((uint8*) "Calendar")) == NULL )
    return error_handler(CONFIGURATIONERROR);

  /* Get the number of parameters for this device */
  if ((numparams = (*theIStartParam(calendardev))(calendardev)) < 0)
    return device_error_handler(calendardev) ;

  /* Extract all the core parameters from the device. */
  for (iparam=0; iparam<numparams; iparam++) {

    theDevParamName(param) = NULL;

    switch ((*theIGetParam(calendardev))(calendardev, &param)) {

    case ParamAccepted:

      paramname = theDevParamName(param);
      HQASSERT(paramname != NULL, "Parameter name NULL");
      paramlen  = theDevParamNameLen(param);
      HQASSERT(paramlen > 0, "Illegal length for parameter name");

      if (paramlen == 4 &&
          !HqMemCmp((uint8 *) "Year", 4, paramname, paramlen)) {
        HQASSERT(theDevParamType(param) == ParamInteger,
                 "Incorrect type returned for year");
        *year = theDevParamInteger(param);
        param_flags |= CAL_YEAR;
      }

      else if (paramlen == 5 &&
               !HqMemCmp((uint8 *) "Month", 5, paramname, paramlen) ) {
        HQASSERT(theDevParamType(param) == ParamInteger,
                 "Incorrect type returned for month");
        *month = theDevParamInteger(param);
        param_flags |= CAL_MONTH;
      }

      else if (paramlen == 3 &&
               !HqMemCmp((uint8 *) "Day", 3, paramname, paramlen)) {
        HQASSERT(theDevParamType(param) == ParamInteger,
                 "Incorrect type returned for day");
        *day = theDevParamInteger(param);
        param_flags |= CAL_DAY;
      }

      else if (paramlen == 4 &&
               !HqMemCmp((uint8 *) "Hour", 4, paramname, paramlen)) {
        HQASSERT(theDevParamType(param) == ParamInteger,
                 "Incorrect type returned for hour");
        *hour = theDevParamInteger(param);
        param_flags |= CAL_HOUR;
      }

      else if (paramlen == 6 &&
               !HqMemCmp((uint8 *) "Minute", 6, paramname, paramlen)) {
        HQASSERT(theDevParamType(param) == ParamInteger,
                 "Incorrect type returned for minute");
        *minute = theDevParamInteger(param);
        param_flags |= CAL_MINUTE;
      }

      else if (paramlen == 6 &&
               !HqMemCmp((uint8 *) "Second", 6, paramname, paramlen)) {
        HQASSERT(theDevParamType(param) == ParamInteger,
                 "Incorrect type returned for second");
        *second = theDevParamInteger(param);
        param_flags |= CAL_SECOND;
      }

      else if (paramlen == 7 &&
               !HqMemCmp((uint8 *) "Running", 7, paramname, paramlen)) {
        HQASSERT(theDevParamType(param) == ParamBoolean,
                 "Incorrect type returned for running");
        *running = theDevParamBoolean(param);
        param_flags |= CAL_RUNNING;
      }

      break;

    case ParamIgnored:
      break;

    case ParamRangeCheck:
      return error_handler(RANGECHECK);

    case ParamTypeCheck:
    case ParamConfigError:
      HQFAIL("Illegal return value from get_param call");
      return error_handler(UNREGISTERED) ;

    case ParamError:
    default:
      /* return with the last error */
      return device_error_handler(calendardev);
    }
  }

  if ( (param_flags&CAL_ALL_PRESENT) != CAL_ALL_PRESENT ) {
    /* Device is not returning all parameters */
    HQFAIL("Calendar device not returning all date/time parameters");
    return(error_handler(UNDEFINED));
  }

  return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Log stripped */

