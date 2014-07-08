/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:caldev.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/** \file
 * \ingroup skinkit
 * \brief Implementation of the calendar device type. See Postscript
 * Language Reference Supplement for Version 2 (Pages 150-151).
 */

/* ----------------------------- Includes ---------------------------------- */

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "kit.h"
#include "ripthread.h"
#include "swdevice.h"
#include "skindevs.h"

/* ----------------------------- Structures -------------------------------- */

/* Private data for Calendar device */
/* NOTE. The date is stored in local time */

typedef struct _calprivate {
  int32 year;          /* 1980 =< year <= 2079
                          or 0 which indicates clock is turned off */
  int32 month;         /* 1 =< month  <= 12 */
  int32 day;           /* 1 =< day    <= 31  */
  int32 hour;          /* 0 =< hour   <= 23 */
  int32 minute;        /* 0 =< minute <= 59 */
  int32 second;        /* 0 =< second <= 59 */
  int32 running;       /* Is the clock turned on? */
  int32 devparamcnt;   /* Counter for currentdevparams */
} CALPRIVATE;

/* ----------------------------- Macros ------------------------------------ */

#define theICalYear(dev)       (((CALPRIVATE *) theIPrivate(dev))->year)
#define theICalMonth(dev)      (((CALPRIVATE *) theIPrivate(dev))->month)
#define theICalDay(dev)        (((CALPRIVATE *) theIPrivate(dev))->day)
#define theICalHour(dev)       (((CALPRIVATE *) theIPrivate(dev))->hour)
#define theICalMinute(dev)     (((CALPRIVATE *) theIPrivate(dev))->minute)
#define theICalSecond(dev)     (((CALPRIVATE *) theIPrivate(dev))->second)
#define theICalRunning(dev)    (((CALPRIVATE *) theIPrivate(dev))->running)
#define theIDevParamCount(dev) (((CALPRIVATE *) theIPrivate(dev))->devparamcnt)

/* ----------------------------- Enumerations ------------------------------ */

/* Indices into the %Calendar% parameter table for each key */
enum { NAME_Year = 0,
       NAME_Month,
       NAME_Day,
       NAME_Hour,
       NAME_Minute,
       NAME_Second,
       NAME_Running,
       N_CALENDAR_PARAMS };

/* ----------------------------- Data -------------------------------------- */

/* The keys for the %Calendar% parameter table */
static char *CalendarDevNames[N_CALENDAR_PARAMS] =
{
  "Year",
  "Month",
  "Day",
  "Hour",
  "Minute",
  "Second",
  "Running"
};

/* ----------------------------- Function Prototypes ----------------------- */

static int32 RIPCALL calendarInit( DEVICELIST *dev );

static DEVICE_FILEDESCRIPTOR RIPCALL calendarOpen( DEVICELIST *dev, uint8 *filename, int32 openflags );

static int32 RIPCALL calendarRead( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff,
                           int32 len );

static int32 RIPCALL calendarWrite( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor , uint8 *buff,
                           int32 len );

static int32 RIPCALL calendarClose( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );

static int32 RIPCALL calendarAbort( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );

static int32 RIPCALL calendarSeek( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 *destn, int32 flags );

static int32 RIPCALL calendarBytes( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 *bytes, int32 reason );

static int32 RIPCALL calendarStatusFile( DEVICELIST *dev, uint8 *filename,
                                         STAT *statbuff );

static void * RIPCALL calendarStart( DEVICELIST *dev, uint8 *pattern );

static int32 RIPCALL calendarNext( DEVICELIST *dev, void **handle,
                                   uint8 *pattern, FILEENTRY *entry );

static int32 RIPCALL calendarEnd( DEVICELIST *dev, void *handle );

static int32 RIPCALL calendarRename( DEVICELIST *dev, uint8 *file1, uint8 *file2 );

static int32 RIPCALL calendarDelete( DEVICELIST *dev, uint8 *filename );

static int32 RIPCALL calendarStatus( DEVICELIST *dev, DEVSTAT *devstat );

static int32 RIPCALL calendarSetParam( DEVICELIST *dev, DEVICEPARAM *ptParam );

static int32 RIPCALL calendarStartParam( DEVICELIST *dev );

static int32 RIPCALL calendarGetParam( DEVICELIST *dev, DEVICEPARAM *param );

static int32 RIPCALL calendarDevDismount( DEVICELIST *dev );

static int32 RIPCALL calendarBufferSize( DEVICELIST *dev );

static int32 RIPCALL calendarVoid(void);

static int32 calendarGetDateTime( DEVICELIST *dev );

static int32 calendarSetError( DEVICELIST *dev, int32 errorcode );

/* ----------------------------- Device Declaration ------------------------ */

DEVICETYPE          CALENDAR_Device_Type =
{
  CALENDAR_DEVICE_TYPE,               /* The device ID number */
  DEVICEABSOLUTE,                     /* Device-specific flags */
  sizeof(CALPRIVATE),                 /* The size of the private data */
  0,                                  /* Min. ticks between tickle fn.s */
  NULL,                               /* Procedure to service the device */
  skindevices_last_error,             /* Ret. last error for this device */
  calendarInit,                       /* Initialise device */
  calendarOpen,                       /* Open file on device */
  calendarRead,                       /* Read data from file on device */
  calendarWrite,                      /* Write data to file on device */
  calendarClose,                      /* Close file on device */
  calendarAbort,                      /* Abort action on the device */
  calendarSeek,                       /* Seek file on device */
  calendarBytes,                      /* Get bytes avail on an open file */
  calendarStatusFile,                 /* Check status of file */
  calendarStart,                      /* Start listing files */
  calendarNext,                       /* Get next file in list */
  calendarEnd,                        /* End listing */
  calendarRename,                     /* Rename file on the device */
  calendarDelete,                     /* Remove file from device */
  calendarSetParam,                   /* Set device parameter */
  calendarStartParam,                 /* Start getting device parameters */
  calendarGetParam,                   /* Get the next device parameter */
  calendarStatus,                     /* Get the status of the device */
  calendarDevDismount,                /* Dismount the device */
  calendarBufferSize,                 /* Determine buffer size */
  NULL,                               /* No need for an ioctl */
  calendarVoid                        /* Spare */
};

/* ----------------------------- Device Definitions ------------------------ */

static int32 RIPCALL calendarInit( DEVICELIST *dev )
{
  /* Initialise device */
  (void)calendarSetError(dev, DeviceNoError);

  /* Initialise date and time. */
  theICalRunning(dev) = FALSE;
  (void) calendarGetDateTime(dev);

#ifdef NO_BATTERY_CALENDAR
  /* No battery-powered time-of-day clock on target platform. */
  theICalRunning(dev) = FALSE;
#else
  /* Battery-powered time-of-day clock is available. */
  theICalRunning(dev) = TRUE;
#endif

  theIDevParamCount(dev) = 0;

  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL calendarOpen( DEVICELIST *dev, uint8 *filename, int32 openflags )
{
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(int32, openflags);

  return calendarSetError(dev, DeviceUndefined);
}


static int32 RIPCALL calendarRead( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,  uint8 *buff,
                           int32 len )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buff);
  UNUSED_PARAM(int32, len);

  return calendarSetError(dev, DeviceIOError);
}

static int32 RIPCALL calendarWrite( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,  uint8 *buff,
                           int32 len )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buff);
  UNUSED_PARAM(int32, len);

  return calendarSetError(dev, DeviceIOError);
}

static int32 RIPCALL calendarClose( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  return calendarSetError(dev, DeviceIOError);
}

static int32 RIPCALL calendarAbort( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  return calendarSetError(dev, DeviceIOError);;
}

static int32 RIPCALL calendarSeek( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                           Hq32x2 *destn, int32 whence )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(Hq32x2 *, destn);
  UNUSED_PARAM(int32, whence);

  (void)calendarSetError(dev, DeviceIOError);
  return FALSE ;
}

static int32 RIPCALL calendarBytes(DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 *bytes, int32 reason )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(Hq32x2 *, bytes);
  UNUSED_PARAM(int32, reason);

  (void)calendarSetError(dev, DeviceIOError);
  return FALSE ;
}

static int32 RIPCALL calendarStatusFile( DEVICELIST *dev, uint8 *filename,
                                         STAT *statbuff )
{
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *, statbuff);

  return calendarSetError(dev, DeviceUndefined);
}

static void *RIPCALL calendarStart( DEVICELIST *dev, uint8 *pattern )
{
  UNUSED_PARAM(uint8 *, pattern);

  (void) calendarSetError(dev, DeviceNoError);

  return NULL;
}

static int32 RIPCALL calendarNext( DEVICELIST *dev, void **handle,
                                   uint8 *pattern, FILEENTRY *entry )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  return FileNameNoMatch ;
}

static int32 RIPCALL calendarEnd( DEVICELIST *dev, void *handle )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);

  return 0 ;
}

static int32 RIPCALL calendarRename( DEVICELIST *dev, uint8 *file1, uint8 *file2 )
{
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  return calendarSetError(dev, DeviceIOError);
}

static int32 RIPCALL calendarDelete( DEVICELIST *dev, uint8 *filename )
{
  UNUSED_PARAM(uint8 *, filename);

  return calendarSetError(dev, DeviceIOError);
}

static int32 RIPCALL calendarSetParam( DEVICELIST *dev, DEVICEPARAM *ptParam )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, ptParam);

  /* At present this device is only READONLY */
  return ParamIgnored;
}

/* Return the number of parameters to be returned by currentdevparams */
static int32 RIPCALL calendarStartParam( DEVICELIST *dev )
{
  if ( calendarSetError(dev, DeviceNoError) )
    return -1;

  theIDevParamCount(dev) = 0;

  return N_CALENDAR_PARAMS;
}

static int32 RIPCALL calendarGetParam( DEVICELIST *dev, DEVICEPARAM *param )
{
  int32 paramlen , namelen, iParam;
  uint8 *paramname ;
  int getdatetime = FALSE;

  if ( theIPrivate(dev) == NULL )
    return ParamError;

  if ((paramname = theIDevParamName(param)) != NULL &&
      (paramlen  = theIDevParamNameLen(param)) > 0) {

    /* Make sure we get the date every time a parameter is requested.
       Care should be taken that repeat calls will cause the date/time
       to roll over */
    getdatetime = TRUE;

    /* Asking for a specific parameter */
    for (iParam = 0; iParam < N_CALENDAR_PARAMS; iParam++) {
      namelen = (int32) strlen(CalendarDevNames[iParam]);
      if (paramlen == namelen &&
          !strncmp(CalendarDevNames[iParam], (char *)paramname, paramlen))
        break;
    }
  }
  else if ((iParam = theIDevParamCount(dev)) >= 0 &&
            iParam < N_CALENDAR_PARAMS)
  {
    /* Listing all for currentdevparams call */
    getdatetime = (iParam == 0);
    namelen = (int32) strlen(CalendarDevNames[iParam]);
    theIDevParamCount(dev)++;
    theIDevParamName(param) = (uint8 *)CalendarDevNames[iParam];
    theIDevParamNameLen(param) = namelen;
  }

  if (iParam < 0 || iParam >= N_CALENDAR_PARAMS) {
    return ParamIgnored;
  }

  /* Get current date and time */
  if ( getdatetime && !calendarGetDateTime(dev) ) {
    /* No clock */
    (void) calendarSetError(dev, DeviceIOError);
    return ParamError;
  }

  switch ( iParam ) {

  case NAME_Year :
    if (theICalYear(dev) < 1980 || theICalYear(dev) > 2079)
      return ParamRangeCheck;
    theIDevParamType(param)    = ParamInteger;
    theIDevParamInteger(param) = theICalYear(dev);
    break;

  case NAME_Month :
    if (theICalMonth(dev) < 1 || theICalMonth(dev) > 12)
      return ParamRangeCheck;
    theIDevParamType(param)    = ParamInteger;
    theIDevParamInteger(param) = theICalMonth(dev);
    break;

  case NAME_Day :
    if (theICalDay(dev) < 1 || theICalDay(dev) > 31)
      return ParamRangeCheck;
    theIDevParamType(param)    = ParamInteger;
    theIDevParamInteger(param) = theICalDay(dev);
    break;

  case NAME_Hour :
    if (theICalHour(dev) < 0 || theICalHour(dev) > 23)
      return ParamRangeCheck;
    theIDevParamType(param)    = ParamInteger;
    theIDevParamInteger(param) = theICalHour(dev);
    break;

  case NAME_Minute :
    if (theICalMinute(dev) < 0 || theICalMinute(dev) > 59)
      return ParamRangeCheck;
    theIDevParamType(param)    = ParamInteger;
    theIDevParamInteger(param) = theICalMinute(dev);
    break;

  case NAME_Second :
    if (theICalSecond(dev) < 0 || theICalSecond(dev) > 59)
      return ParamRangeCheck;
    theIDevParamType(param)    = ParamInteger;
    theIDevParamInteger(param) = theICalSecond(dev);
    break;

  case NAME_Running :
    theIDevParamType(param)    = ParamBoolean;
    theIDevParamBoolean(param) = theICalRunning(dev);
    break;

  default:
    return ParamIgnored;
  }

  return ParamAccepted;
}

static int32 RIPCALL calendarStatus( DEVICELIST *dev, DEVSTAT *devstat )
{
  UNUSED_PARAM(DEVSTAT *, devstat);

  return calendarSetError(dev, DeviceIOError);
}

static int32 RIPCALL calendarDevDismount( DEVICELIST *dev )
{
  return calendarSetError(dev, DeviceNoError);
}

static int32 RIPCALL calendarBufferSize( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  return 0;
}

static int32 RIPCALL calendarVoid( void )
{
  return 0;
}

/* Get the current date and time from the %Calendar% device */
static int32 calendarGetDateTime( DEVICELIST *dev )
{
  if ( theICalRunning(dev) ) {

    struct tm *local;

    /* Get the current date and time from the system in local and UTC time zones */
    /* Machine dependant - START */
    time_t now = time(NULL);
    if ( ( local = localtime(&now) ) == NULL )
      return FALSE;

    theICalYear(dev)   = 1900 + local->tm_year; /* Offset from year 1900 */
    theICalMonth(dev)  = local->tm_mon + 1;
    theICalDay(dev)    = local->tm_mday;
    theICalHour(dev)   = local->tm_hour;
    theICalMinute(dev) = local->tm_min;
    theICalSecond(dev) = local->tm_sec;
    /* Machine dependant - END */
  }
  else {

    /* The clock is NOT running so set to default (January 1, 1980 00:00:00) */
    theICalYear(dev)      = 1980;
    theICalMonth(dev)     = 1;
    theICalDay(dev)       = 1;
    theICalHour(dev)      = 0;
    theICalMinute(dev)    = 0;
    theICalSecond(dev)    = 0;
  }

  return TRUE;
}

/* Set the error code for the %Calendar% device */
static int32 calendarSetError( DEVICELIST *dev, int32 error )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(error);
  return error == DeviceNoError ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
