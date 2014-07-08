/* Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!unix:src:oil_platform.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief Source file for the platform wrapper.
 *
 */

#include <errno.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include "oil.h"
#include "oil_platform.h"
#include "time.h"

/* extern variables */
extern int g_nTimeInit;

/**********************************************
 * Timing Functions.
 **********************************************/

/** \brief Report time since startup.
 *
 * \return Returns the time in milliseconds since startup.\n
 */
unsigned long OIL_TimeInMilliSecs(void)
{
  struct tms tm;
  return (long)((times(&tm) * 1000L /sysconf(_SC_CLK_TCK)) - g_nTimeInit);
}

/** \brief Give up any remaining time in the time slice. */
void OIL_RelinquishTimeSlice(void)
{
#ifdef SDK_PANDA
  PMS_RelinquishTimeSlice();
  return;
#else
#ifdef _POSIX_PRIORITY_SCHEDULING
  int nError;
  if(sched_yield()!=0)
  {
    nError = errno;
    HQFAILV(("sched_yield failed %d", nError));
  }
#else
  /* sleep zero does nothing, sleep (1ms) instead */
  OIL_Delay(1);
#endif
#endif
}

/** \brief Suspend current thread/task for a specified time.
 *
 * \param[in] nMilliSeconds Delay in milliseconds.
 */
void OIL_Delay(int nMilliSeconds)
{
  struct timespec ts;
  ts.tv_sec = ((float)nMilliSeconds)/1000;
  ts.tv_nsec = (nMilliSeconds-ts.tv_sec*1000)*1000000;
  nanosleep(&ts,NULL);
}

HqBool OIL_Init_platform(void)
{
  /* Nothing to do as POSIX pthreads part of Linux kernel!. */
  return TRUE ;
}

/** \brief Swap bytes in a word.
 *
 * \param[in] buffer contaning bytes to be swapped.
 * \param[in] Total no of 4-byte words to be swapped.
 *
 * for example - after swapping words contaning 0x12345678 will be 0x78563412
 */
#ifdef _ARM9_
int OIL_SwapBytesInWord(unsigned char * pBuffer, int nTotalWords)
{
  register unsigned int *printData = NULL, tempData = 0;
  register unsigned int tempData_2 = 0;
  printData = (unsigned int*)pBuffer;

  do
  {
    asm(
    "MVN %[result_2], #0x0000FF00\n\t"
    "EOR %[result], %[value], %[value], ROR#16\n\t"
    "AND %[result], %[result_2], %[result], LSR#8\n\t"
    "EOR %[value], %[result], %[value], ROR#8\n\t"
    : [result] "+r" (tempData), [value] "+r" (*printData), [result_2]    "+r" (tempData_2)
       );
    ++printData;
  }while(--nTotalWords);

  return 0;
}
#else
int OIL_SwapBytesInWord(unsigned char * pBuffer, int nTotalWords)
{
    int i;
    int nTotalBytes = nTotalWords << 2;
#define SwapFourBytes(data) \
  ( (((data) >> 24) & 0x000000FF) | (((data) >> 8) & 0x0000FF00) | \
  (((data) << 8) & 0x00FF0000) | (((data) << 24) & 0xFF000000) )
    for (i=0; i < nTotalBytes; i=i+4)
    {
      unsigned long *data;
      data = (unsigned long *)(pBuffer+i);
      *data = SwapFourBytes(*data);
    }
#undef SwapFourBytes
  return 0;
}
#endif

