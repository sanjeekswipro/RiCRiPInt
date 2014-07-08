/* 
 * $HopeName: SWripiimpl!export:ripstatusadm_i.h(EBDSDK_P.1) $
 */

/*
 * This module implements the RIPStatusAdmin interface of the RIP IDL interface
 */

#ifndef _incl_ripstatusadm_i
#define _incl_ripstatusadm_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

//#include "orb_impl.h"   // ORB-dependent code
#include "ripiface.hh"  // IDL-generated from ripiface.idl (includes hqnsystem.idl and hqntypes.idl) 
#include "ripadm_i.h"   // IDL RIPAdmin, IDL base class of MonitorAdmin
#include "ripstatuscnsmr.h"   // RIPStatusConsumer (abstract class)

/* ----------------------- Classes ----------------------------------------- */

// implementation class of IDL RIPStatusAdmin interface
// it is expected that an object of this class is manipulated over a CORBA interface

class RIPStatusAdminImpl :
  public POA_RIPInterface::RIPStatusAdmin, public RIPAdminImpl
{

  friend class RIPControlImpl;

protected:
  RIPStatusAdminImpl();
  ~RIPStatusAdminImpl();

public:

  long rip_status_admin_impl_version();

  RIPInterface::ConsumerID register_consumer_1
            (RIPInterface::RIPStatusConsumer_ptr pRIPStatus );
    // registers consumer interested in RIP status information
    // ConsumerID returned must be used to (later) unregister_consumer
    // throws CORBA::INV_OBJREF if invalid ServerInfo data
    // throws CORBA::NO_MEMORY if memory allocation fails
    // this function not passed onto RIP

}; // class RIPStatusAdminImpl

#endif /* PRODUCT_HAS_API */

#endif // _incl_ripstatusadm_i

/*
* Log stripped */
