/* $HopeName: SWripiimpl!export:tpadm_i.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module implements the ThroughputStatusAdmin interface of the RIP IDL interface
 */

#ifndef _incl_tpadm_i
#define _incl_tpadm_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "ripapi.h"     // C/C++ RIP API general header

#include "tpcnsmr.h"    // ThroughputStatusConsumer (abstract class)

#include "ripiface.hh"  // IDL-generated from ripiface.idl
#include "HqnSystem.hh" // HqnSystem generated from IDL 

#include "ripadm_i.h"   // IDL RIPAdmin, IDL base class of ThroughputStatusAdmin

/* ----------------------- Classes ----------------------------------------- */

// forward declaration
class RIPControlImpl;


// implementation class of IDL ThroughputStatusAdmin interface
// it is expected that an object of this class is manipulated over a CORBA interface

class ThroughputStatusAdminImpl :
  public virtual POA_RIPInterface::ThroughputStatusAdmin, public RIPAdminImpl
{
  friend class RIPControlImpl;

 protected:
  ThroughputStatusAdminImpl();
  ~ThroughputStatusAdminImpl();

 public:
  long throughput_status_admin_impl_version();

  RIPInterface::ConsumerID register_consumer_1
    (
      RIPInterface::ThroughputStatusConsumer_ptr pThroughputStatus, 
      const RIPInterface::ThroughputStatusTypes & tp_status_types,
      const RIPInterface::TPQueues & queues,
      const RIPInterface::ThroughputStatusRemovalReasons & tp_removal_reasons
    );

  // throws RIPInterface::NotConnected_1
  void update_consumer_1
    (
      RIPInterface::ConsumerID id, 
      const RIPInterface::ThroughputStatusTypes & tp_status_types,
      const RIPInterface::TPQueues & queues,
      const RIPInterface::ThroughputStatusRemovalReasons & tp_removal_reasons
    );
        
}; // class ThroughputStatusAdminImpl

#endif /* PRODUCT_HAS_API */

#endif // _incl_tpadm_i
