/* $HopeName: SWripiimpl!export:prgadm_i.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module implements the ProgressAdmin interface of the RIP IDL interface
 */

#ifndef _incl_prgadm_i
#define _incl_prgadm_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "ripiface.hh"  // IDL-generated from ripiface.idl
#include "prgcnsmr.h"   // C++ API ProgressConsumer class
#include "ripadm_i.h"   // IDL RIPAdmin, IDL base class of ProgressAdmin

/* ----------------------- Classes ----------------------------------------- */

// forward declaration
class RIPControlImpl;

// implementation class of IDL ProgressAdmin interface
// it is expected that an object of this class is manipulated over a CORBA interface

class ProgressAdminImpl :
  public virtual POA_RIPInterface::ProgressAdmin, public RIPAdminImpl
{
  friend class RIPControlImpl;

protected:
  ProgressAdminImpl();
  ~ProgressAdminImpl();

public:
  long progress_admin_impl_version();

  // get the progress update time
  CORBA::Long progress_update_time_2();

  // set the progress update time
  // if out of range (which depends on whether GUI RIP or not) then
  // set back into range
  void progress_update_time_2(CORBA::Long progress_time);

  // registers consumer interested in progress updates
  // ProgressConsumer interface contains functions to be called when changes occur
  // ConsumerID returned must be used to (later) unregister_consumer
  // throws CORBA::INV_OBJREF if invalid ServerInfo data
  // throws CORBA::NO_MEMORY if memory allocation fails
  // this function not passed onto RIP
  RIPInterface::ConsumerID register_consumer_1(RIPInterface::ProgressConsumer_ptr progress,
                                             const RIPInterface::ProgressTypes & progress_types);
  
  // throws RIPInterface::NotConnected_1
  void update_consumer_3(RIPInterface::ConsumerID id,
                         const RIPInterface::ProgressTypes& progress_types);

}; // class ProgressAdminImpl

#endif /* PRODUCT_HAS_API */

#endif // _incl_prgadm_i
