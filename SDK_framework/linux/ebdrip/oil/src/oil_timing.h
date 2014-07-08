/* Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_timing.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief Header file for OIL timing functions.
 *
 */


#ifndef _OIL_TIMING_H_
#define _OIL_TIMING_H_

/*! \brief Timing events used by PPM calculations */
enum {
    GG_PPM_FIRSTJOBSTART = 0,   /*!< Start of first job */
    GG_PPM_CURRENTJOBSTART,     /*!< Start of current job */
    GG_PPM_LAST_CHECKIN,        /*!< Successful last page checkin to PMS */
    GG_PPM_LAST_PAGEDONE,       /*!< Successful last page output from PMS */

    GG_PPM_DATAEND,             /*!< End of PPM Timing data. Must be last entry in this list. */
};
typedef int OIL_eTy_GGPPMTimingType;      /*!< Complete list of PPM events timings that can be logged */

/*! \brief Array of recorded events used for PPM calculations. */
extern unsigned int            g_ulGGPPM_Data[GG_PPM_DATAEND];

/*! \brief Current pagecount, to allow accurate numbering of pages done. */
extern unsigned int            g_ulGGtiming_pagecount;

void    GGglobal_timing(int nTraceId, int nData);
void    GGglobal_timing_dumplog(unsigned char* pszJobName);  
void    GGglobal_timing_PPMlog(void);  


#endif /* _OIL_TIMING_H_ */
