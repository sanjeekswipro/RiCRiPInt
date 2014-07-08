#ifndef __LOGUTILS_H__
#define __LOGUTILS_H__

/*
 * $HopeName: HQN_CPP_CORBA!export:logutils.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifdef PRODUCT_HAS_API

#include "fwstring.h"

#ifdef __cplusplus

#include "queue.h"
#include "hqn_cpp_corba.h"     // general header
#include "HqnLogAdmin.h"       // HqnLogAdmin stubs

class LogUtils
{

public:
  /** Writes a log with the given subject, message, and alarm level.
   * If a SOAR log has been registered, it writes to that.  It also
   * ALWAYS writes to the default FWOS log.
   *
   * Will not throw any CORBA exceptions, even if a remote SOAR logger
   * has been registered, because SOAR logging happens on a
   * separate thread. In fact, no exceptions at all ought to get thrown.
   *
   * The caller must manage the memory for the subject and message
   * strings. If residing on the heap, they may be freed immediately after
   * the call. Alternatively, this method works fine with stack or static data
   * as subject and/or message.
   *
   * Accepts FwStrPrintf style template and varargs.
   */
  static void logf(FwTextString subject, HqnLogAdmin::HqnAlarmLevel alarmLevel,
                   FwTextString messageTemplate, ...);

  /* Version for use when varargs parsing is already in progress. */
  static void vlogf(FwTextString subject, HqnLogAdmin::HqnAlarmLevel alarmLevel,
                    FwTextString messageTemplate, va_list args);

  /** Like logf(), but has implicit ERROR log level and will HQFAIL in
   *  an ASSERT build. Use this for situations where a programming error
   *  (bug) has been found, but want to continue, with logging, in a
   *  non-ASSERT build. Do not use where a programming error has not
   *  been found.
   *
   * Accepts FwStrPrintf style template and varargs.
   */
  static void logFail(FwTextString subject, FwTextString messageTemplate, ...);

  /** Register an HqnLog object for logging to.  The new HqnLog will
   * override any HqnLog already registered.  As the const _var ref
   * calling convention implies, THE CALLER IS RESPONSIBLE FOR
   * MANAGING THE MEMORY FOR THE HqnLog.
   */
  static void registerRemoteLog(const HqnLogAdmin::HqnLog_var& vLog);

  /* Unregister any registered HqnLog object. If none is registered, or one
   * has already been unregistered, this method is safe to call, but does
   * nothing.
   */
  static void unregisterRemoteLog();

  /* Just for use by LoggingData, really. */
  static HqnLogAdmin::HqnLog_var& getRegisteredLog();

  /* Permanently shut down logging for this application. */
  static void shutdown();

private:

  static bool isShuttingDown;
  static bool isQueueStarted;
  static HqnLogAdmin::HqnLog_var vRegisteredLog;
  static Queue * pLogQueue;

  /* Opaque class for the data to be logged by the following actions. */
  class LoggingData;

  /* Internal method to log using given data. Takes ownership of the LoggingData. */
  static void log(LoggingData* pLoggingData);

  /* Never instantiate. */
  LogUtils();

  /* Never instantiate. */
  ~LogUtils();

};

extern "C" {
#endif  /* __cplusplus */

/** C-callable logging functions. */
extern void doLogInfoMsg(FwTextString subject, FwTextString messageTemplate, ...);
extern void doLogWarningMsg(FwTextString subject, FwTextString messageTemplate, ...);
extern void doLogErrorMsg(FwTextString subject, FwTextString messageTemplate, ...);

/* C-callable version of LogUtils::unregisterRemoteLog() */
extern void logUtilsUnregisterRemoteLog();

/* C-callable version of LogUtils::shutdown() */
extern void logUtilsShutdown();

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* PRODUCT_HAS_API */

#endif  /* __LOGUTILS_H__ */

/* Log stripped */
