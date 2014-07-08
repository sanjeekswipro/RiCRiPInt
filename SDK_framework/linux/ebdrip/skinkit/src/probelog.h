/* Copyright (C) 2009-2012 Global Graphics Software Ltd. All Rights Reserved.
 *
 * $HopeName: SWskinkit!src:probelog.h(EBDSDK_P.1) $
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

#include "skinkit.h"

/* Platform dependant function declarations */
void PKProbeLogInit(SwWriteProbeLogFn *pfnWriteLog);
void PKProbeLogFinish(void);
void PKProbeLogFlush(void);
void RIPFASTCALL PKProbeLog(int trace_id,
                            int trace_type,
                            intptr_t trace_designator);
void RIPFASTCALL PKProbeProfile(int trace_id,
                                int trace_type,
                                intptr_t trace_designator);

extern const char **g_ppTraceNames;
extern int g_nTraceNames;
extern int *g_pabTraceEnabled;
extern const char **g_ppTraceTypeNames;
extern int g_nTraceTypeNames;
extern char g_szProbeLog[260];
extern char g_szProbeArg[8];

