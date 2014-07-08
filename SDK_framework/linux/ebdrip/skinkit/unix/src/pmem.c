/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!unix:src:pmem.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * Memory related utility functions for UNIX.
 */

/** \file
 * \ingroup skinkit
 * \brief Memory related utility functions for UNIX.
 */

#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mem.h"

#if defined(MACOSX)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <ApplicationServices/ApplicationServices.h>
#endif

uint32 GetPhysicalRAMSizeInMegabytes()
{

  /* This is Linux specific code. */
#if defined(__linux__)
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);

  if (pages != -1 && page_size != -1) {
    pages *= page_size;
    return CAST_LONG_TO_UINT32(pages / 1024 / 1024);
  }

  /* This is MacOS-X specific code. */
#elif defined(MACOSX)
  /* Quiz OS about installed RAM */
  OSErr  err;
  long   response;

  err = Gestalt( gestaltSystemVersion, &response );

  if( err == noErr && response >= 0x1030 ) {
    /* Panther (10.3) or later
     * Use sysctl(), which can handle > 2GB
     */
    int32    mib[ 2 ] = { CTL_HW, HW_MEMSIZE };
    uint64_t memSize;
    size_t   dataSize = sizeof( memSize );

    if( sysctl( mib, 2, &memSize, &dataSize, NULL, 0 ) == 0 ) {
      return (uint32)(memSize / 1024 / 1024);
    }
  }

  /* Before Panther (10.3), or if something went wrong above, we use
   * Gestalt.  Even on G5s there is no guaranteed way of determining
   * if we have > 2GB RAM when running 10.2.  Note that the Gestalt
   * return value is never > 2GB.
   */
  err = Gestalt( gestaltPhysicalRAMSize, &response );

  if( err == noErr ) {
    return (uint32)(response / 1024 / 1024);
  }
#endif

  return 0;
}


/* This is Linux specific code. */
#if defined(__linux__)
static unsigned long int parseVmSizeLine(char* line)
{
  unsigned long int result;
  size_t i = strlen(line);

  while (! isdigit(*line)) {
    line++;
  }

  /* Note that we know the line contains at least "VmHWM:". */
  line[i-3] = '\0';

  errno = 0;
  result = strtoul(line, NULL, 10);
  if (errno != 0) {
    result = 0;
  }

  return result;
}

/* See: "man 5 proc" via google.
 * VmHWM: Peak resident set size ("high water mark").
 *
 * This seems to at least give a reasonable figure which at least
 * shows the RIP using more or less memory. Finding some good
 * documentation on this stuff is difficult. Need to look at the Linux
 * kernal code really --johnk
 */
static unsigned long int GetPeakMemoryUsageInKiB()
{
  FILE* file;
  unsigned long int result = 0;
  char line[128];

  if ((file = fopen("/proc/self/status", "r")) != NULL) {
    while (fgets(line, 128, file) != NULL) {
      if (strncmp(line, "VmHWM:", 6) == 0) {
        result = parseVmSizeLine(line);
        break;
      }
    }
    fclose(file);
  }

  return result;
}
#endif

uint32 GetPeakMemoryUsageInMegabytes()
{
  /* This is Linux specific code. */
#if defined(__linux__)
  return (uint32)(GetPeakMemoryUsageInKiB() / 1024);
#endif
  return 0 ;
}

/** Not implemented for UNIX platforms at this time. */
int32 GetCurrentProcessHandleCount()
{
  return -1 ;
}

