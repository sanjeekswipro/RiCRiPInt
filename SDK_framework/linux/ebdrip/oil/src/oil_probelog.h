/* Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_probelog.h(EBDSDK_P.1) $
 * 
 */

/*! \file
 *  \ingroup OIL
 *  \brief Probe log functions
 *
 * These functions are application-provided functions for the probe logging feature.
 */

#ifndef _OIL_PROBELOG_H_
#define _OIL_PROBELOG_H_

#include "oil.h"
#include "skinkit.h"
#include "swtrace.h"

#ifdef __cplusplus
extern "C" {
#endif


/** \brief Outputs a message to \c stderr detailing the probe logging options.
 */
void OIL_ProbeOptionUsage(void);

/** @brief Set up probing. */
int32 OIL_ProbeOption(SwTraceHandlerFn *handler, char *arg);

/** Initialize the probe logging functionality.
 */
void OIL_ProbeLogInit(char *pszArg, char *pszLog) ;

/** \brief Finalize probe logging.
 */
void OIL_ProbeLogFinish(void) ;

/** \brief Handle a trace event.
 */
void OIL_ProbeLog(int trace_id, int trace_type, intptr_t designator);

/**
 * @brief Probe trace names for OIL.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having
 * maintainers having to modify every skin which uses the trace facility.
 */
#define OIL_TRACENAMES(macro_) \
  macro_(OIL_RESET)                /*!< Reset and initialise */ \
  macro_(OIL_SYSSTART)             /*!< System startup*/  \
  macro_(OIL_JOBSTART)             /*!< Start of job */ \
  macro_(OIL_CHECKIN)              /*!< Page checkin to PMS */ \
  macro_(OIL_PAGESTART)            /*!< Start interpreting the page */ \
  macro_(OIL_PAGECHECKSUM)         /*!< Page checksum */ \
  macro_(OIL_GETBANDDATA)          /*!< In OIL_GetBandData function */ \
  macro_(OIL_SUBMITPAGETOPMS)      /*!< In SubmitPageToPMS routine */ \
  macro_(OIL_RASTERCALLBACK)       /*!< In OIL_RasterCallback routine */ \
  macro_(OIL_CREATEPMSPAGE)        /*!< In CreatePMSPage routine */ \
  macro_(OIL_DELETEPMSPAGE)        /*!< In DeletePMSPage routine */ \
  macro_(OIL_PAGEDONE)             /*!< In OIL_PageDone routine */ \
  macro_(OIL_BACKCHANNELWRITE)     /*!< In OIL_BackChannelWrite routine */ \
  macro_(OIL_GETMEDIAFROMPMS)      /*!< In GetMediaFromPMS routine */ \
  macro_(OIL_GETBANDINFOFROMPMS)   /*!< In GetBandInfoFromPMS routine */ \
  macro_(OIL_GETSCREENINFOFROMPMS) /*!< In GetScreenInfoFromPMS routine */ \
  macro_(OIL_GETPMSSYSTEMINFO)     /*!< In GetPMSSystemInfo routine */

/** @brief Define a list of OIL probes.
 *
 *  Start the list where skinkit finished (SKINKIT_TRACE_N).
 *  You should simply add probes to the OIL_TRACENAMES define above.
 */
enum {
  OIL_TRACE_BASE = SKINKIT_TRACE_N, /**< Base value for OIL trace names.
                                        This MUST be the first enum
                                        value. */
  OIL_TRACENAMES(SW_TRACENAME_ENUM)
  OIL_TRACE_N          /**< Starting point for next layer (PMS) trace
                            identifiers, if implemented.
                            This MUST be the last enum value. */
} ;


#ifdef __cplusplus
}
#endif

#endif /* _OIL_PROBELOG_H_ */
