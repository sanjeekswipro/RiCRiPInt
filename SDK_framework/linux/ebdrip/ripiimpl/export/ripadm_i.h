/* $HopeName: SWripiimpl!export:ripadm_i.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module implements the RIPAdmin interface of the RIP IDL interface
 */

#ifndef _incl_ripadm_i
#define _incl_ripadm_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "ripiface.hh"  // IDL-generated from ripiface.idl

#include "ripapi.h" // C/C++ RIP API general header

#include "hqnconsumer.h"

#include "srvmgr_i.h"   // ServerProcessObserver

/* ----------------------- Classes ----------------------------------------- */

// forward declaration
class RIPControl;


// implementation class of IDL RIPAdmin interface
// it is expected that an object of this class is manipulated over a CORBA interface

class RIPAdminImpl : public virtual POA_RIPInterface::RIPAdmin, public PortableServer::RefCountServantBase
{
  friend class RIPControlImpl;

protected:
  RIPAdminImpl();
  ~RIPAdminImpl();

public:
  long rip_admin_impl_version();

  void unregister_consumer_1(RIPInterface::ConsumerID consumer);
    // unregisters consumer interested in job status changes
    // throws RIPInterface::NotConnected_1 if consumer not registered
    // throws CORBA::NO_MEMORY if cannot allocate required memory
    // ConsumerID must be that returned from (earlier) register_consumer
    // no security, so anyone can (in principle) unregister any consumer
    // this function not passed onto RIP

}; // class RIPAdminImpl

// definition of RIPConsumerListener
// this defines a consumer listener for registration with Server Process Manager
// TODO this mirrors ConsumerListener from hqnconsumer.h.  It will have to, until
// or unless the RIP uses HqnFeedback's ConsumerIDs.
class RIPConsumerListener : public ServerProcessListener
{
private:
  RIPInterface::ConsumerID consumer;

  friend class RIPAdminImpl;
  friend class ConsumerListenerCompare;  // implementation class

  RIPConsumerListener(RIPInterface::ConsumerID consumer);

public:
  ~RIPConsumerListener();

  void release(const HqnSystem::ServerInfo_1 & newInfo, 
               HqnSystem::ServedByProcess_ptr pServedByProcess);
    // called when Server Process Manager wants this listener to die
    // no exceptions should be thrown

  static RIPConsumerListener* create_listener
                  (RIPInterface::ConsumerID consumer);
    // throws CORBA::NO_MEMORY if memory allocation fails
    // a listener is created for the consumer and returned

  static RIPConsumerListener* unregister_listener
                  (RIPInterface::ConsumerID consumer);
    // no exceptions thrown
    // returns the listener which was created for the consumer,
    // or 0 if not known

}; // class RIPConsumerListener

#endif /* PRODUCT_HAS_API */

#endif // _incl_ripadm_i
