#ifndef __SERVER_I_H__
#define __SERVER_I_H__

/* $HopeName: HQN_CPP_CORBA!export:server_i.h(EBDSDK_P.1) $
 *
 * This module implements the ServedByProcess interface of the
 * HqnSystem IDL interface
 */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "served_i.h"   // ServedByProcessImpl, servant for IDL base class of ServerProcess

/* ----------------------- Classes ----------------------------------------- */

// implementation class of IDL ServedByProcess interface
// it is expected that an object of this class is manipulated over a CORBA interface

// NOTE: ONLY CREATE ONE INSTANCE OF THIS CLASS PER OS PROCESS.  this
// restriction is not enforced by the class itself.

class ServerProcessImpl : public virtual POA_HqnSystem::ServerProcess, 
                          public ServedByProcessImpl
{ 
public:

  ServerProcessImpl(const HqnSystem::ServerInfo_1 & srvInfo);

  CORBA::Long server_process_impl_version( );

  // called to see if server is still alive
  void ping_1( );

  // causes abnormal exit
  void kill_1( );

  // return a list of the server modules registered with this server process
  HqnSystem::NamedServerModules_1 * get_server_modules_2( );

  // return the root context of the local nameservice
  CosNaming::NamingContext_ptr resolve_local_nameservice_3( );


  // register a server module with this server process
  void register_server_module( const HqnSystem::NamedServerModule_1 & pair );

  // get the server info for this OS process
  static HqnSystem::ServerInfo_1 * get_server_info();

  // Destructor should be called just before process dies
  virtual ~ServerProcessImpl();

private:

  static ServerProcessImpl * singleton;

  // raw pointer because reference/value containment
  // don't work so well here, because we need to statically
  // initialise this member, and there is nothing appropriate
  // with which to do that apart from null.
  static HqnSystem::ServerInfo_1 * serverInfo;

  HqnSystem::NamedServerModules_1 * serverModules;
 
}; // class ServerProcessImpl

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif // __SERVER_I_H__

// eof server_i.h
