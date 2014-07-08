/* $HopeName: SWripiimpl!export:tpcnsmr.h(EBDSDK_P.1) $
 */

/*
 * This module defines an abstract C++ API version of the ThroughputStatusConsumer interface
 */

#ifndef _incl_tpcnsmr
#define _incl_tpcnsmr

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "ripapi.h"     /* C/C++ RIP API general header */

#ifdef __cplusplus

#include "ripiface.hh"  // RIPInterface generated from IDL, includes HqnTypes
#include "ripcnsmr.h"   // RIPConsumer, (abstract) base class of ThroughputStatusConsumer

#include "hostserv.hh"  // Host

/* ----------------------- Classes ----------------------------------------- */

// definition of abstract class JobStatusConsumer
// clients of RIP API service define an implementation of this class via inheritance

class RIPAPI_ThroughputStatusConsumer : public RIPAPI_RIPConsumer
{
public:
  // abstract definition of tp_stat_output_enabled callback
  virtual void tp_stat_output_enabled(int32 enabled) = 0;

  // abstract definition of tp_stat_pagebuffers_added callback
  virtual void tp_stat_pagebuffer_added(
    RIPInterface::TPQueue queue,
    const HostInterface::File_var & vPageBuffer,
    RIPInterface::JobID job ) = 0;

  // abstract definition of tp_stat_pagebuffers_removed callback
  virtual void tp_stat_pagebuffer_removed(
    RIPInterface::TPQueue queue,
    RIPInterface::ThroughputStatusRemovalReason reason,
    const HostInterface::File_var & vPageBuffer,
    RIPInterface::JobID job ) = 0;

  // abstract definition of tp_stat_output_error callback
  virtual void tp_stat_output_error(
    const HostInterface::File_var & vPageBuffer,
    RIPInterface::JobID job,
    int32 errornumber,
    const HqnTypes::UnicodeString & errorstring,
    int32 disable ) = 0;

  // abstract definition of tp_stat_all_pages_output callback
  virtual void tp_stat_all_pages_output(
    RIPInterface::JobID job, int32 nPagesOutput ) = 0;

  virtual ~RIPAPI_ThroughputStatusConsumer() {}

}; // class RIPAPI_ThroughputStatusConsumer

extern RIPInterface::TPQueue tpQueueToCORBA( int32 iQueue );
extern int32 tpQueueFromCORBA( RIPInterface::TPQueue iQueue );

extern RIPInterface::ThroughputStatusRemovalReason tpRemovalReasonToCORBA( int32 reason );

#endif /* __cplusplus */

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

extern void notifyOutputEnabled( int32 enabled );

extern void notifyPageBufferAdded(
  int32 iQueue,
  int32 pageID,
  int32 jobNumber
);

extern void notifyPageBufferRemoved(
  int32 iQueue,
  int32 reason,
  int32 pageID,
  int32 jobNumber
);

extern void notifyOutputError(
  int32 pageID,
  int32 jobNumber,
  int32 errorNumber,
  FwTextString errorString,
  int32 disable
);

extern void notifyAllPagesOutput( int32 jobNumber, int32 nPagesOutput );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* PRODUCT_HAS_API  */

/*
* Log stripped */

#endif /* _incl_tpcnsmr */

/* tpcnsmr.h */
