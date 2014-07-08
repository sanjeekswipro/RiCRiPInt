/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 * Wrapper layer to turn core RIP into a DLL.
 *
 * $HopeName: SWdllskin!unix:src:pdllskin.c(EBDSDK_P.1) $
 */

#include "pdllskin.h"

#include <time.h>
#include <sys/time.h>


int32 platformGetRealMilliSeconds()
{
  struct timeval tp;
  uint32 ut;
  int32 t;

  gettimeofday( &tp, NULL );

  ut = ((uint32)tp.tv_sec) * 1000 + tp.tv_usec / 1000;
  t = SAFE_UINT32_TO_INT32(ut);

  return t;
}


/* User time is the same as real time */
int32 platformGetUserMilliSeconds()
{
  return platformGetRealMilliSeconds();
}


static uint8 aOperatingSystem[] = "Unix" ;
/* Returns a pointer to a static string giving the operating system */
uint8 * platformGetOperatingSystem( void )
{
  return aOperatingSystem ;
}


/* Log stripped */
