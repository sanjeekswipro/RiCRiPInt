/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:devutils.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
/**
 * @file
 * @brief Contains functions which can be shared between multiple devices.
 */
#include "devutils.h"

#include "file.h"


int32 KMapPlatformError(int32 pkError)
{
  int32  swError = DeviceIOError;

  if ( pkError == PKErrorNone )
    return DeviceNoError;

  if (pkError >= PKErrorParameter && pkError < PKErrorOperationDenied )
  {
    /* Parameter error */
    if (pkError >= PKErrorStringEmpty)
    {
      swError = DeviceUndefined;
    }
  }
  else if (pkError >= PKErrorOperationDenied && pkError < PKErrorOperationFailed)
  {
    /* Operation denied error */
    if (pkError == PKErrorNonExistent)
    {
      swError = DeviceUndefined;
    }
    else
    {
      swError = DeviceInvalidAccess;
    }
  }
  else if (pkError >= PKErrorOperationFailed && pkError <= PKErrorFatal)
  {
    /* operation failed error */
    if (pkError == PKErrorAbort)
    {
      swError = DeviceInterrupted;
    }
    else if (pkError == PKErrorNoMemory)
    {
      swError = DeviceVMError;
    }
    else if (pkError > PKErrorNoMemory && pkError <= PKErrorSoftwareLimit)
    {
      swError = DeviceLimitCheck;
    }
  }

  return swError;
}


