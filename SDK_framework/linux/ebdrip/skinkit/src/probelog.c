/* Copyright (C) 2009-2014 Global Graphics Software Ltd. All Rights Reserved.
 *
 * $HopeName: SWskinkit!src:probelog.c(EBDSDK_P.1) $
 *
 * Probe trace logging
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * @file
 * @brief Capture profiling information from RIP
 */

#include "probelog.h"
#include <stdio.h>
#include <string.h>

/****************************************************************************/
#define STRINGIFY(x) #x,

/** Default trace name array */
const static char *default_trace_name[SKINKIT_TRACE_N] = {
  NULL, /* SW_TRACE_INVALID */
  SW_TRACENAMES(STRINGIFY)
  NULL, /* SKINKIT_TRACE_BASE */
  SKINKIT_TRACENAMES(STRINGIFY)
} ;

/** This pointer will point to either the default trace name array, or
    an array that was specified in the init function. */
const char **g_ppTraceNames = &default_trace_name[0];

/** Number of trace names in trace name array */
int g_nTraceNames = SKINKIT_TRACE_N;

/** Default array for trace enabled/disabled state */
static int default_trace_enabled[SKINKIT_TRACE_N];

/** This pointer will point to either the default enabled/disable state array,
    or an array that was specified in the init function. */
int *g_pabTraceEnabled = &default_trace_enabled[0];

/** Default trace type name array */
const static char *default_tracetype_name[SKINKIT_TRACETYPE_N] = {
  NULL, /* SW_TRACETYPE_INVALID */
  SW_TRACETYPES(STRINGIFY)
  NULL, /* SKINKIT_TRACETYPE_BASE */
  SKINKIT_TRACETYPES(STRINGIFY)
} ;

/** This pointer will point to either the default trace type name array, or
    an array that was specified in the init function. */
const char **g_ppTraceTypeNames = &default_tracetype_name[0];

/** Number of trace type names in trace type list */
int g_nTraceTypeNames = SKINKIT_TRACETYPE_N;


/** @brief Default Group details struct.
 *
 * This is used to construct detailed usage messages. The usage message is
 * constructed using a function so that re-ordering names in the core doesn't
 * cause the skin to be out of order.
 *
 * Can be overridden when calling the init function.
 */
static const sw_tracegroup_t default_group_details[] = {
#include "swtracegroups.h" /* Use core trace groups */
} ;

const sw_tracegroup_t *l_pGroupDetails = &default_group_details[0];
int l_nGroupDetails = (sizeof(default_group_details) / sizeof(default_group_details[0]));

#undef STRINGIFY

/****************************************************************************/
/** @brief Probe log filename */
char g_szProbeLog[260];
/** @brief Probe logging command line arg
  * For usage display purposes only. */
char g_szProbeArg[8];

/****************************************************************************/

/** @brief Initialise probe logging.
 *
 * This should come just after starting up the application. It should be
 * before the calls to start up and shutdown the RIP.
 *
 * If ppTraceNames is NULL then skinkit's default list will be used.
 *
 * If ppTraceTypeNames is NULL then skinkit's default list will be used.
 */
void SwLeProbeLogInit(const char **ppTraceNames, int nTraceNames,
                      int *pabTraceEnabled,
                      const char **ppTraceTypeNames, int nTraceTypeNames,
                      const sw_tracegroup_t *pGroupDetails, int nGroupDetails,
                      char *pszArg, char *pszLog, SwWriteProbeLogFn *pfnWriteLog)
{
  int i ;

  if (pszArg && (strlen(pszArg)<(sizeof(g_szProbeArg)-1))) {
    strcpy(&g_szProbeArg[0], pszArg);
  }
  else {
    g_szProbeArg[0] = '\0';
  }

  if (pszLog && (strlen(pszLog)<(sizeof(g_szProbeLog)-1))) {
    strcpy(&g_szProbeLog[0], pszLog);
  }
  else {
    strcpy(g_szProbeLog, "probe.log");
  }


  /* Override default trace names, if supplied */
  if (ppTraceNames) {
    g_ppTraceNames = ppTraceNames;
    g_nTraceNames = nTraceNames;
    g_pabTraceEnabled = pabTraceEnabled;
  }
  else {
    g_ppTraceNames = &default_trace_name[0];
    g_nTraceNames = SKINKIT_TRACE_N;
    for ( i = 0 ; i < SKINKIT_TRACE_N ; ++i )
      default_trace_enabled[i] = FALSE ;
    g_pabTraceEnabled = &default_trace_enabled[0];
  }


  /* Override default trace types, if supplied */
  if (ppTraceTypeNames) {
    g_ppTraceTypeNames = ppTraceTypeNames;
    g_nTraceTypeNames = nTraceTypeNames;
  }
  else {
    g_ppTraceTypeNames = &default_tracetype_name[0];
    g_nTraceTypeNames = SKINKIT_TRACETYPE_N;
  }

  /* Override default group detail list if supplied */
  if (pGroupDetails) {
    l_pGroupDetails = pGroupDetails;
    l_nGroupDetails = nGroupDetails;
  }
  else {
    l_pGroupDetails = &default_group_details[0];
    l_nGroupDetails = (sizeof(default_group_details) / sizeof(default_group_details[0]));
  }


  PKProbeLogInit(pfnWriteLog);
}

/** @brief Flush probe log.
 *
 */
void SwLeProbeLogFlush()
{
  PKProbeLogFlush();
}

/** @brief Finalise probe logging.
 *
 * This should come just before shutting the application down. It should be
 * after the calls to start up and shutdown the RIP.
 */
void SwLeProbeLogFinish(void)
{
  PKProbeLogFinish();
}

/* Enable probes */
HqBool KProbeOption(const char *arg)
{
  int i ;
  HqBool enable = TRUE ;

  if ( *arg == '-' ) {
    enable = FALSE ;
    ++arg ;
  }

  /* Handle "help" (or "-help"). */
  if ( strcmp(arg, "help") == 0 ) {
    SwLeProbeOptionUsage();
    return TRUE ;
  }

  /* Handle "ALL" group specially, to get all probes enabled. */
  if ( strcmp(arg, "all") == 0 ) {
    for ( i = SW_TRACE_INVALID ; ++i < g_nTraceNames ; )
      SwLeTraceEnable(i, enable) ;
    return TRUE ;
  }

  /* The argument we've been passed is either a probe name or a probe group
     name. Search for it in the groups first. */
  for (i=0 ; i<l_nGroupDetails ; i++ ) {
    if ( strcmp(l_pGroupDetails[i].option, arg) == 0 ) {
      const int *probes ;
      for ( probes = l_pGroupDetails[i].ids ;
            *probes != SW_TRACE_INVALID ;
            ++probes ) {
        SwLeTraceEnable(*probes, enable) ;
      }
      return TRUE ;
    }
  }

  /* It wasn't a group, so search the individual trace names for the
     option. */
  for ( i = 0 ; i < g_nTraceNames ; ++i ) {
    if ( g_ppTraceNames[i] != NULL && strcmp(g_ppTraceNames[i], arg) == 0 ) {
      SwLeTraceEnable(i, enable) ;
      return TRUE ;
    }
  }

  /* No such probe or group. */
  return FALSE ;
}

/* Set handler and enable probes */
HqBool SwLeProbeOption(SwTraceHandlerFn *handler, const char *arg)
{
  /* If the caller has provide an alternative function, then use it */
  if (handler) {
    SwLeSetTraceHandler(handler) ;
  }
  else {
    SwLeSetTraceHandler(SwLeProbe) ;
  }

  /* No such probe or group. */
  return KProbeOption(arg);
}

/* Set handler and enable probes */
HqBool SwLeProfileOption(SwTraceHandlerFn *handler, const char *arg)
{
  /* If the caller has provide an alternative function, then use it */
  if (handler) {
    SwLeSetTraceHandler(handler) ;
  }
  else {
    SwLeSetTraceHandler(PKProbeProfile) ;
  }

  /* No such probe or group. */
  return KProbeOption(arg);
}

/* Probe usage details */
void SwLeProbeOptionUsage(void)
{
  int i ;
  size_t len ;
  size_t width ;
  const char *name ;
  const char *prefix ;

  fprintf(stderr,
    "\t%s\tEnable timing probe. Timing information is logged to a textfile\n"
    "\t\tcalled \"%s\", which can be processed to extract detailed\n"
    "\t\tinformation about what the RIP is doing.\n"
    "\t\tProbes are usually enabled using these group names:\n"
    "\t\tall\tAll probes (use carefully, this may affect performance)\n",
    g_szProbeArg, g_szProbeLog );

  /* Print group members of probe_details. */
  for ( i = 0 ; i < l_nGroupDetails ; ++i ) {
    const int *probes ;

    fprintf(stderr, "\t\t%s\t", l_pGroupDetails[i].option) ;
    if (strlen(l_pGroupDetails[i].option) > 7)
      fprintf(stderr, "\n\t\t\t") ;

    name = l_pGroupDetails[i].usage ;
    len = strlen(name) ;
    while (len > 55) {
      int j ;
      for (j = 55; name[i] != 32 && j > 0; j--) ;
      if (j < 1)
        j = 55 ;
      fprintf(stderr, "%.*s\n\t\t\t", j, name) ;
      if (name[j] != 32)
        j += 1 ;
      name += j ;
      len -= j ;
    }
    fprintf(stderr, "%s\n\t\t\t", name) ;

    width = 24 ;
    prefix = "" ;

    for ( probes = l_pGroupDetails[i].ids ;
          *probes != SW_TRACE_INVALID ;
          ++probes ) {
      name = g_ppTraceNames[*probes] ;
      len = strlen(name) ;

      if ( width + len >= 79 ) {
        fprintf(stderr, ",\n\t\t\t") ;
        prefix = "" ;
        width = 24 ;
      }

      fprintf(stderr, "%s%s", prefix, name) ;
      width += len + 2 ;
      prefix = ", " ;
    }
    fprintf(stderr, "\n") ;
  }

  fprintf(stderr,
"\t\tProbes may also be enabled individually using these names:\n"
"\t\t\t"
          );

  width = 24 ;
  prefix = "" ;

  for ( i = 0 ; i < g_nTraceNames ; ++i ) {
    if ( g_ppTraceNames[i] != NULL ) {
      const char *name = g_ppTraceNames[i] ;
      size_t len = strlen(name) ;

      if ( width + len >= 79 ) {
        fprintf(stderr, ",\n\t\t\t") ;
        prefix = "" ;
        width = 24 ;
      }

      fprintf(stderr, "%s%s", prefix, name) ;
      width += len + 2 ;
      prefix = ", " ;
    }
  }
  fprintf(stderr,
"\n\t\tProbes and groups are disabled by prefixing the name with '-'.\n"
          );
}

