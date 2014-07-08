/* 
 * $HopeName: SWripiimpl!export:ripcnsmr.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module defines an abstract C++ API version of the RIPConsumer interface
 */

#ifndef _incl_ripcnsmr
#define _incl_ripcnsmr

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#ifdef __cplusplus

#include "hqnconsumer.h"
#include "logutils.h"

/* ---------------------- Macros ------------------------------------- */

/* used for tests in consumers that can take a set of types to report on */

#define IS_REGISTERED(_type_) ( typesRegisteredMask & BIT(_type_) )

// Convenience macros for logging errors. These should only be used for
// pretty serious, unlikely errors, because the resultant log is aimed
// more at Development and Support personnel debugging problems than end-users.
//
// Requires a macro LOG_SUBJECT to be defined in the specific .cpp source file
//
#define LOG_MESSAGE(exception, method) UVM(exception " in " method " for job %d")
#define LOG_ERROR(jobNumber, exception, method) \
    LogUtils::logf(LOG_SUBJECT, HqnLogAdmin::HAL_ERROR, \
                   LOG_MESSAGE(exception, method), (jobNumber));

#define LOGFAIL(literalText) LogUtils::logFail(UVS("RIPConsumer"), UVS(literalText))

/** Macro to generate catch clauses to "handle" exceptions that have not
 *  been properly handled. Calls LogUtils::logFail(), so should only be used
 *  where we believe such exceptions would indicate a programming error (bug).
 */
#define CNSMR_CATCH_UNEXPECTED(literalText) \
   catch (QueueException&) { LOGFAIL( "Unexpected QueueException " literalText ); } \
   catch (CORBA::UserException&) { LOGFAIL( "Unexpected UserException " literalText ); } \
   catch (...) { LOGFAIL( "Unexpected Unknown Exception " literalText ); }

/* ----------------------- Classes ----------------------------------------- */

/* definition of abstract class RIPConsumer
 * clients of RIP API service define an implementation of this class via
 * inheritance */

class RIPAPI_RIPConsumer : public HqnConsumerProxyBase
{
 private:

 protected:
  RIPAPI_RIPConsumer() {}
  virtual ~RIPAPI_RIPConsumer() {}

 public:

}; /* class RIPAPI_RIPConsumer */

#endif

#ifdef __cplusplus
extern "C" {
#endif

  /* for calling shut_consumer_queues_down from a C context */
  extern void shut_queues_down();

#ifdef __cplusplus
}
#endif

#endif

#endif
