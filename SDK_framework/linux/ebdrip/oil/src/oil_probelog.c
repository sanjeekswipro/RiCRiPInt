/* Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_probelog.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief Tracing functions for use with the probe logging functionality.
 *
 * These functions are application-provided functions for the core trace feature.
 */

#include "oil_probelog.h"
#include "oil_interface_oil2pms.h"

/****************************************************************************/
/* Tables of trace names and type names. */

#define STRINGIFY(x) #x,

/* The following list of strings defines the probes available. It consists of
   all probes defined in corelib, all defined in skinkit plus probes defined
   in OIL.
   Note: corelib must have probes compiled in for core probes to available.
   Skinkit and OIL probes can still be used without probes built into corelib.
*/
const char *l_aszProbeTraceNames[OIL_TRACE_N] = {
  NULL, /* SW_TRACE_INVALID */
  SW_TRACENAMES(STRINGIFY)        /* Core probes */
  NULL, /* SKINKIT_TRACE_BASE */
  SKINKIT_TRACENAMES(STRINGIFY)   /* Skinkit probes */
  NULL, /* OIL_TRACE_BASE */
  OIL_TRACENAMES(STRINGIFY)       /* OIL probes */
} ;

int l_bTraceEnabled[OIL_TRACE_N];
static int l_bProbeInit = 0;
#ifdef VXWORKS
static PMS_TyBackChannelWriteFileOut tWriteBuffer;
#endif
#undef STRINGIFY


/****************************************************************************/

/** @brief Definitions of logging probes and probe groups.
 *
 * This structure details the probes and probe groups known to the OIL.  It is
 * used to construct detailed usage messages. The usage message is
 * constructed using a function so that re-ordering names in the core doesn't
 * cause the skin to be out of order.
 */
static const sw_tracegroup_t l_atGroupDetails[] = {
#include "swtracegroups.h" /* Use core trace groups */
  { "all_skin", "Object group, for all skinkit probes:",
    {
      SKINKIT_TRACENAMES(SW_TRACENAME_ENUM)
      SW_TRACE_INVALID
    }
  },
  { "all_oil", "Object group, for all OIL probes:",
    {
      OIL_TRACENAMES(SW_TRACENAME_ENUM)
      SW_TRACE_PROBE,
      SW_TRACE_INVALID
    }
  },
  { "basic_oil", "Object group, for basic OIL probes:",
    {
      SW_TRACE_OIL_RESET, SW_TRACE_OIL_SYSSTART, SW_TRACE_OIL_JOBSTART,
      SW_TRACE_OIL_PAGESTART, SW_TRACE_OIL_CHECKIN, SW_TRACE_OIL_PAGEDONE,
      SW_TRACE_OIL_PAGECHECKSUM,
      SW_TRACE_PROBE,
      SW_TRACE_INVALID
    }
  }
} ;

/** Flags to indicate if trace events should be issued. */

/**
 * \brief Configures the probe logging functionality.
 *
 * This function allows a function pointer to be passed in to the Skin as
 * an alternate handler for probe information.  It also allows the various
 * probes and probe groups to be enabled or disabled by name.
 * This implementation passes the arguments straight to the Skin,
 * and processes the return result to determine whether or not the call
 * was a success.
 * \param[in] handler Pointer to the function which captures, timestamps
 *                    and logs probe information. If this parameter is NULL, 
 *                    the default function will be used.
 * \param[in] arg     A probe or group of probes to be enabled (or disabled)
 *                    by the call. Prefixing the name with '-' disables the
 *                    probe.  The special value 'all' enables or disables all
 *                    probes.  The special value 'help' causes a usage message
 *                    to be output.
 * \return            Returns TRUE if the supplied options are set successfully.  
 *                    If the options are invalid, it will return FALSE.
 */
int32 OIL_ProbeOption(SwTraceHandlerFn *handler, char *arg)
{
  int32 nResult;

  nResult = SwLeProbeOption(handler, arg);

  HQASSERT(nResult, "OIL_ProbeOption(): SwLeProbeOption failed");

  return nResult ;
}

/**
 * \brief Outputs a message to \c stderr detailing the probe logging options.
 *
 * The message includes a list of the names and usage details for each probe 
 * group, and a list of all the probes in each group. 
 */
void OIL_ProbeOptionUsage(void)
{
  SwLeProbeOptionUsage();
}

/**
 * \brief Callback function for outputting the probe log data.
 *
 * This function is currently only used by the VxWorks port to send the log back via
 * the connected socket. It could be used on other platforms if required.
 *
 */
#ifdef VXWORKS
static PMS_TyBackChannelWriteFileOut tWriteBuffer;

static int RIPCALL OIL_WriteLog(char *pBuffer, size_t nLength)
{
  int nWritten;

  PMS_WriteDataStream(PMS_WRITE_FILE_OUT, &tWriteBuffer, pBuffer, nLength, &nWritten);

  return nWritten;
}
#endif
/**
 * \brief Initialize the probe logging functionality.
 *
 * By default, this functionality disables all probes and then enables
 * any probes specified via the command line arguments, which are 
 * passed in to this function.
 *
 * The OIL contains a list of probes and probe groups which includes all the core
 * and Skin probes as well as its own.  Passing this list to \c SwLeProbeLogInit()
 * registers all these probes and groups with the logging functionality.
 *
 * All the probes are disabled by default, and if required, must be enabled by calling 
 * OIL_ProbeOption(), specifying the appropriate probe or probe group as a parameter.
 * \param[in] pszArg   The command line argument to be used to invoke probe logging (used
 *                     for display purposes only).
 * \param[in] pszLog   The name of the file to receive the probe log information.
 */
void OIL_ProbeLogInit(char *pszArg, char *pszLog)
{
  int i;
  SwWriteProbeLogFn *pfnWriteLog;

  /* Skinkit has support for probes built-in. The default lists
     can be used by simply suppling NULL as the list pointer

      SwLeProbeLogInit(NULL, 0,
                       NULL, 0,
                       NULL, 0,
                       pszArg, pszLog,
                       NULL);
   */

  /* Disable all probes. SwLeProbeOption is used to enable only specific
     probes according to the command line argument */
  for ( i = 0 ; i < OIL_TRACE_N ; ++i )
    l_bTraceEnabled[i] = FALSE ;

#ifdef VXWORKS
  memset(&tWriteBuffer, 0, sizeof(tWriteBuffer));
  strcpy(tWriteBuffer.szFilename, pszLog);
  pfnWriteLog = OIL_WriteLog;
#else
  /* If you require the probe log to be sent back via socket then
     do the same as the VXWORKS section above */
  pfnWriteLog = NULL;
#endif

  /* The OIL probe list, l_aszProbeTraceNames, includes all core probe
     names plus all skinkit names.

     Core probe type list already covers all the types of probes that
     we use, therfore we use the default type list defined in skinkit.
     */
  SwLeProbeLogInit(l_aszProbeTraceNames, OIL_TRACE_N, &l_bTraceEnabled[0],
                   NULL, 0,
                   l_atGroupDetails,
                   (sizeof(l_atGroupDetails)/sizeof(l_atGroupDetails[0])),
                   pszArg, pszLog, pfnWriteLog);
  l_bProbeInit = 1;
}

/**
 * \brief Flush probe log.
 */
void OIL_ProbeLogFlush(void)
{
  if(l_bProbeInit)
  {
    SwLeProbeLogFlush();
  }
}

/**
 * \brief Finalize probe logging.
 * 
 * This implementation simply checks whether or not probe logging has been initialized
 * and, if so, calls the Skin version of the same function.
 */
void OIL_ProbeLogFinish(void)
{
  if(l_bProbeInit)
  {
    SwLeProbeLogFinish();
  }
}

/**
 * \brief Handles a trace event.
 *
 * This function is called to handle probe log trace events.  Events which enable or disable a
 * probe are not written to the probe log.  The event will
 * only be written to the log if the trace ID and the trace type are valid.
 * @todo these parameters need checking
 * \param[in] trace_id    Identifies the probe which originated the trace event.
 * \param[in] trace_type  Identifies the trace type, that is, the action being performed.
 * \param[in] designator  An additional identifying tag passed in to the trace logging function
 */
void OIL_ProbeLog(int trace_id, int trace_type, intptr_t designator)
{
  if(l_bProbeInit)
  {
    SwLeProbe(trace_id, trace_type, designator);
  }
}

/* -------------------------------------------------------------------------- */
