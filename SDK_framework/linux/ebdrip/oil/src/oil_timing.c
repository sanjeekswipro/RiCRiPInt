/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_timing.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief OIL Performance Measurement Functions.
 *
 */


#include "oil.h"
#include "oil_timing.h"
#include "oil_interface_oil2pms.h"
#include <stdio.h>
#include <string.h>

#include "skinkit.h"
#include "oil_probelog.h"


/* extern variables */
extern int g_bLogTiming;
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
static unsigned int  g_ulGGPPM_AllJobPgCnt = 0;
static unsigned int  g_ulGGPPM_CurrentJobPgCnt = 0;

/* global variables */
/*! \brief Array of recorded events used for PPM calculations. */
unsigned int            g_ulGGPPM_Data[GG_PPM_DATAEND];

/*! \brief Current pagecount, to allow accurate numbering of pages done. */
unsigned int            g_ulGGtiming_pagecount;


/**
 * \brief 
 * Function to place data into the timing log.
 *
 * Log entries are automatically timestamped, and two
 * items of user-supplied data are recorded:
 * \arg   eType - type of event to log
 * \arg   nData - event specific data (eg time spent waiting)
 *
 * Examples of use:\n
 * \code
 *         GGglobal_timing (SW_TRACE_OIL_RESET, 0);
 *         GGglobal_timing (SW_TRACE_OIL_CHECKIN, 0);
 *         GGglobal_timing (SW_TRACE_OIL_PAGEDONE, 2);
 * \endcode
 * \param[in]   nTraceId    An integer identifying the event type being logged.
 * \param[in]   nData    An integer identifying the event type being logged.
 */
void GGglobal_timing(int nTraceId, int nData)
{
  unsigned long   current_time;

  /* Use probe mechanism in skinkit to log this trace */
  OIL_ProbeLog(nTraceId,
               SW_TRACETYPE_MARK,
               (intptr_t)nData);

  /* If we're not calculating ppm, then get out early */
  if(!g_bLogTiming)
    return;

  /* This needs to be secure, potentialy any thread can all call
     in here to log a timing event. Probe Log above is thread safe. */
  /* TODO - platform dependant task lock here */

  /* get the timestamp */
  current_time = OIL_TimeInMilliSecs();

  /* special actions complete, now record data in timing log */
  switch(nTraceId)
  {
  default:
    break;
  case SW_TRACE_OIL_RESET:
    g_ulGGtiming_pagecount = 0;
    break;
  
  case SW_TRACE_OIL_JOBSTART :
    g_ulGGPPM_CurrentJobPgCnt = 0;
    if(!g_ulGGPPM_AllJobPgCnt)
    {
      g_ulGGPPM_Data[GG_PPM_FIRSTJOBSTART] = current_time ;
    }
    g_ulGGPPM_Data[GG_PPM_CURRENTJOBSTART] = current_time;
    break;

  case SW_TRACE_OIL_CHECKIN:
    g_ulGGtiming_pagecount++;
    g_ulGGPPM_Data[GG_PPM_LAST_CHECKIN] = current_time ;
    break;

  case SW_TRACE_OIL_PAGEDONE:
    g_ulGGtiming_pagecount--;
    g_ulGGPPM_AllJobPgCnt++;
    g_ulGGPPM_CurrentJobPgCnt++;
    break;

  }
  /* TODO - platform dependant task unlock here */
}

/**
 * \brief Output data from the timing log to a file.
 *
 * Typically this function is called at the end of a job to dump all
 * the recorded data to a text file for later analysis.\n
 * The output filename is 'timing.log'.
 * \param[in]   Jobname     Job name.  Output to the log file as identification.
 */
void GGglobal_timing_dumplog(unsigned char *Jobname)
{
  unsigned int nCurrentJobPPM = 0;
  unsigned int nCurrentJobRunTime = 0 ;
  
  /* in a multithreaded system it's possible to arrive here before all the
     pages have been output - so wait */
  while (g_ulGGtiming_pagecount != 0)
  {
    /* wait - until all the pages have been output */
    OIL_RelinquishTimeSlice();
  }

  /* write out the headers */
  GG_SHOW(GG_SHOW_TIMING,"*** OIL Timing Data for this job ***\n");
  GG_SHOW(GG_SHOW_TIMING,"OIL Timing Data (");
  GG_SHOW(GG_SHOW_TIMING,(char *)Jobname);
  GG_SHOW(GG_SHOW_TIMING,"):\n");
  GG_SHOW(GG_SHOW_TIMING,"Total Pages printed in current job = %d \n",g_ulGGPPM_CurrentJobPgCnt);

  if(g_ulGGPPM_Data[GG_PPM_LAST_CHECKIN]>g_ulGGPPM_Data[GG_PPM_CURRENTJOBSTART])
  {
    nCurrentJobRunTime = g_ulGGPPM_Data[GG_PPM_LAST_CHECKIN] - g_ulGGPPM_Data[GG_PPM_CURRENTJOBSTART];
  }

  GG_SHOW(GG_SHOW_TIMING,"Run time taken for the Current Job = %d mS\n",nCurrentJobRunTime);

  HQASSERT(nCurrentJobRunTime, "PPM can not be calculated as run time is Zero.");

  if(nCurrentJobRunTime)
  {
    nCurrentJobPPM = (g_ulGGPPM_CurrentJobPgCnt*1000*60)/nCurrentJobRunTime;
  }

  GG_SHOW(GG_SHOW_TIMING,"PPM for the current job is %d ppm\n",nCurrentJobPPM);
  GG_SHOW(GG_SHOW_TIMING,"\n");
}
/**
 * \brief Output pages-per-minute (PPM) data
 *
 * Typically this function is called at the end of the run to
 * calculate and display PPM data for performance analysis.\n
 */
void GGglobal_timing_PPMlog()
{
  unsigned int nCurrentRunPPM = 0;
  unsigned int nTotalRunTime = 0 ;
  
  /* in a multithreaded system it's possible to arrive here before all the
     pages have been output - so wait */
  while (g_ulGGtiming_pagecount != 0)
  {
    /* wait - until all the pages have been output */
    OIL_RelinquishTimeSlice();
  }

  /* write out the headers */
  GG_SHOW(GG_SHOW_TIMING,"*** OIL Timing Data for the entire run ***\n");
  GG_SHOW(GG_SHOW_TIMING,"Total Pages printed during the run = %d \n",g_ulGGPPM_AllJobPgCnt);

  if(g_ulGGPPM_Data[GG_PPM_LAST_CHECKIN] > g_ulGGPPM_Data[GG_PPM_FIRSTJOBSTART])
  {
    nTotalRunTime = g_ulGGPPM_Data[GG_PPM_LAST_CHECKIN] - g_ulGGPPM_Data[GG_PPM_FIRSTJOBSTART];
  }

  GG_SHOW(GG_SHOW_TIMING,"Total time taken for the run = %d mS\n",nTotalRunTime );

  HQASSERT(nTotalRunTime, "PPM can not be calculated as run time is Zero.");
  if(nTotalRunTime)
  {
    nCurrentRunPPM = (g_ulGGPPM_AllJobPgCnt*1000*60)/nTotalRunTime;
  }

  GG_SHOW(GG_SHOW_TIMING,"Average PPM of the run is %d ppm\n",nCurrentRunPPM);
}



