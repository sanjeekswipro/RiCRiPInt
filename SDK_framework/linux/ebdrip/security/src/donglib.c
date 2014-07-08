/* Copyright (C) 1994-2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 $HopeName: SWsecurity!src:donglib.c(EBDSDK_P.1) $
Log stripped */


#include "obfusc.h"

#include "std.h"
#include "extscrty.h"
#include "dongle.h"
#include "donglib.h"

static int  customerID = -1;
static int  productCode = -1;
static int  productVersion = -1;
static int  demonstrationOnly = 0xFFFFFFFF;
static int  resolutionLimit = 0xFFFFFFFF;
static int  upperResolutionLimit = 0;
static int  lowerResolutionLimit = 0;
static int  securityNumber = 0xFFFFFFFF;
static int  featureLevel = -1;
static int  protectedPlugins = 0xFFFFFFFF;
static int  spare1 = 0;
static int  spare2 = 0;
static int  spare3 = 0;

static int * aTestSecDevParams[ nTestSecDevParams ] =
{
  &customerID,
  &productCode,
  &productVersion,
  &demonstrationOnly,
  &resolutionLimit,
  &securityNumber,
  &featureLevel,
  &protectedPlugins,
  &spare1,
  &spare2,
  &spare3
};


int DongleLevel()
{
        return ( 0 );           /* Return zero to remain compatible with older dongles */
}

int DongleCustomerNo()
{
        return ( -1 );
}

int DongleMaxRipVersion()
{
        return ( -1 );
}

int DongleMaxResolution()
{
        return ( 0 );
}
int   dongle_serialnumber (int  * serial_number)
{

  static int  checkCode = -1;

  int val, savedCheckCode;

  savedCheckCode = checkCode;

  val = fullTestSecurityMask ^
        fullTestSecurityDevice (&checkCode, aTestSecDevParams);
  if ( (val == savedCheckCode) && (checkCode == (customerID ^
                                                 productCode ^
                                                 productVersion ^
                                                 demonstrationOnly ^
                                                 resolutionLimit ^
                                                 securityNumber ^
                                                 featureLevel ^
                                                 protectedPlugins ^
                                                 spare1 ^
                                                 spare2 ^
                                                 spare3 ) ) )
  {
    *serial_number = securityNumber;
    return ( 1 );
  }
  else {
    return ( 0 );
  }
}

/*************************************************************************
 *  All the definitions below are not in direct use but there to make    *
 *  the security compound happy.                                         *
 *************************************************************************/

void unixmonitorf(char *test)
{
/* define this procedure (which is used in rnbwdong.obj) to do nothing.  */
}

void gui_warning(char *test)
{
/* define this procedure (which is used in rnbwdong.obj) to do nothing.  */
}

void HQNCALL SecurityExit(int32 code, uint8 *string)
{
/* define this procedure (which is used in dongle.obj) to do nothing.  */
}

#define forcedFullTestSecurityDevice    isPageSizeOK
int forcedFullTestSecurityDevice ()
{
  int checkCode = 0x9A28D409, result;

  result = fullTestSecurityMask ^
    fullTestSecurityDevice ( &checkCode, aTestSecDevParams );
  if ( ( result == 0x9A28D409 ) &&
       ( checkCode == ( customerID ^
                        productCode ^
                        productVersion ^
                        demonstrationOnly ^
                        resolutionLimit ^
                        securityNumber ^
                        featureLevel ^
                        protectedPlugins ^
                        spare1 ^
                        spare2 ^
                        spare3 ) ) )
  {
    lowerResolutionLimit = (resolutionLimit & 0xFFF0000) >> 16;
    upperResolutionLimit = resolutionLimit & 0xFFFF ;
    return ( 1 );
  }
  else
  {
    return (0);
  }
}

int timer = 0;
int * SwTimer = &timer;

int SwOftenActivateSafe()
{
  return 1;
}
