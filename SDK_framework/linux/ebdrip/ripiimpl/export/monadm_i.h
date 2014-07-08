/* $HopeName: SWripiimpl!export:monadm_i.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module implements the MonitorAdmin interface of the RIP IDL interface
 */

#ifndef _incl_monadm_i
#define _incl_monadm_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#ifdef __cplusplus

#include "orb_impl.h"   // ORB-dependent code

#include "ripiface.hh"  // IDL-generated from ripiface.idl (includes hqnsystem.idl and hqntypes.idl) 

#include "ripadm_i.h"   // IDL RIPAdmin, IDL base class of MonitorAdmin

#include "moncnsmr.h"   // MonitorConsumer (abstract class)

/* ----------------------- Classes ----------------------------------------- */

// forward declaration
class RIPControlImpl;

// implementation class of IDL MonitorAdmin interface
// it is expected that an object of this class is manipulated over a CORBA interface

class MonitorAdminImpl :
  public virtual POA_RIPInterface::MonitorAdmin, public RIPAdminImpl
{
  friend class RIPControlImpl;

protected:
  MonitorAdminImpl();
  ~MonitorAdminImpl();

public:
  // ORB_ENV_ARG below because Orbix has extra argument

  long monitor_admin_impl_version(ORB_ENV_ARG_NONE);

  void initial_registration_complete_2();

  RIPInterface::ConsumerID register_consumer_1
            (RIPInterface::MonitorConsumer_ptr monitor ORB_ENV_ARG_LAST );
    // registers consumer interested in monitor information
    // monitor contains function to be called with monitor informatoin
    // ConsumerID returned must be used to (later) unregister_consumer
    // throws CORBA::INV_OBJREF if invalid ServerInfo data
    // throws CORBA::NO_MEMORY if memory allocation fails
    // this function not passed onto RIP

  void set_monitor_output_type_3( RIPInterface::MonitorAdmin::MonitorOutputType outputType );

}; // class MonitorAdminImpl

#endif 
/* __cplusplus */

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif 

extern void setInitialRegistrationComplete( void );

#ifdef __cplusplus
}
/* extern "C" */
#endif 

#endif /* PRODUCT_HAS_API */

#endif /* _incl_monadm_i */
