/* $HopeName: SWripiimpl!export:moncnsmr.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module defines an abstract C++ API version of the MonitorConsumer interface
 */

#ifndef _incl_moncnsmr
#define _incl_moncnsmr

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "ripapi.h"     /* C/C++ RIP API general header */

#ifdef __cplusplus

#include "ripcnsmr.h"   // RIPConsumer, (abstract) base class of MonitorConsumer
#include "registry.h"
#include "consumer.h"   /* CONSUMER_NOJOBID */

/* ----------------------- Classes ----------------------------------------- */

// definition of abstract class MonitorConsumer
// clients of RIP API service define an implementation of this class via inheritance

class RIPAPI_MonitorConsumer : public RIPAPI_RIPConsumer
{
public:
  virtual void uni_write_monitor(FwTextString data,
                                 int32 jobid = CONSUMER_NOJOBID) = 0;

  virtual void flush_monitor() = 0;

  virtual ~RIPAPI_MonitorConsumer() {}

}; // class MonitorConsumer

// Message that can be queued to request any deferred RIP Monitor Log to be written
// to the real consumers. Unusually, this message class is used by more than one
// source module, so it has to be in a header, rather than module-private.
class FlushMonitorMessage : public ConsumerMessage
{
public:
  FlushMonitorMessage();

  ~FlushMonitorMessage();

  void message(HqnConsumerProxyBase* consumer);
};

#endif

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif
/* __cplusplus */

/* Enqueue a consumer message for the given RIP Monitor text, with optional
 * associated RIP Job ID. Note that the RIP Monitor text may be buffered
 * for later, rather than emitted straight away when the message is handled.
 */
extern void notifyMonitor(FwTextString str, int32 jobid);

/* If there are any messages being queued-up, from RIP start-up, enqueue
 * them to any registered consumers. If not, discard them. If called more
 * than once, subsequent calls do nothing and are cheap.
 * Returns TRUE for success (including doing nothing because no messages
 * were queued-up), FALSE for failure.
 */
extern int32 enqueuePendingMonitorMessages();

#ifdef __cplusplus
}
/* extern "C" */
#endif
/* __cplusplus */

#endif
/* PRODUCT_HAS_API */

#endif
/* _incl_moncnsmr */

