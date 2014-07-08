#ifndef __SERVED_I_HH__
#define __SERVED_I_HH__

/* $HopeName: HQN_CPP_CORBA!export:served_i.h(EBDSDK_P.1) $
 *
 * This module implements the ServedByProcess interface of the
 * HqnSystem IDL interface
 */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "HqnSystem.hh" // IDL-generated from HqnSystem.idl

/* ----------------------- Classes ----------------------------------------- */


// implementation class of IDL ServedByProcess interface
// it is expected that an object of this class is manipulated over a CORBA interface
class ServedByProcessImpl : public virtual POA_HqnSystem::ServedByProcess, public PortableServer::RefCountServantBase
{

protected:
  ServedByProcessImpl();
  ~ServedByProcessImpl();

public:

  CORBA::Long served_by_process_impl_version( );

  HqnSystem::ServerInfo_1 *server_info_1( );
    // returns server information

}; // class ServedByProcessImpl

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif // __SERVED_I_HH__


// eof served_i.h
