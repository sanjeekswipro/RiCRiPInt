/* 
 * $HopeName: SWripiimpl!export:ripstatuscnsmr.h(EBDSDK_P.1) $
 */

/*
 * This class defines an abstract C++ API version of the RIPStatusConsumer interface.
 */

#ifndef _incl_ripstatuscnsmr
#define _incl_ripstatuscnsmr

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "ripapi.h"     /* C/C++ RIP API general header */

#ifdef __cplusplus

#include "ripiface.hh"  // RIPInterface generated from IDL, includes HqnTypes
#include "ripcnsmr.h"   // RIPConsumer, (abstract) base class of JobStatusConsumer


/* ----------------------- Classes ----------------------------------------- */

// clients of RIP API service define an implementation of this class via inheritance

class RIPAPI_RIPStatusConsumer : public RIPAPI_RIPConsumer
{
public:
  // abstract definition of rip_shutting_down_1 callback
  // called when the RIP is shutting down
  virtual void rip_shutting_down() = 0;

  // abstract definition of input_channels_enabled callback
  // called when the input channels are enabled or disabled
  virtual void input_channels_enabled(int32 enabled) = 0;

  // abstract definition of license_server_status_changed callback
  // called when the HLS status changes
  virtual void license_server_status_changed
    (RIPInterface::RIPStatusConsumer::LicenseServerStatus status) = 0;

  // abstract definition of callback for RIP readiness change
  virtual void rip_is_ready(int32 ready) = 0;

  virtual ~RIPAPI_RIPStatusConsumer() {}

}; // class RIPStatusConsumer

extern RIPInterface::RIPStatusConsumer::LicenseServerStatus hlsStatusToCORBA( int32 status );

#endif /* __cplusplus */

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/* interface via which the coreskin notifies the RIPAPI that the RIP is shutting down */
extern void notifyRIPShuttingDown();

/* interface via which the coreskin notifies the RIPAPI that the input channel status has changed */
extern void notifyInputChannelsEnabledForRIPStatusConsumer(int32 enabled);

/* interface via which the coreskin notifies the RIPAPI that the HLS status has changed */
extern void notifyLicenseServerStatus(int32 status);

/* interface via which the RIPAPI can be notified that the RIP readiness has changed */
extern void notifyRIPIsReady(int32 ready);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* PRODUCT_HAS_API */


#endif


/*
* Log stripped */
