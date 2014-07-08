#ifndef __SRVMOD_I_H__
#define __SRVMOD_I_H__

/* $HopeName: HQN_CPP_CORBA!export:srvmod_i.h(EBDSDK_P.1) $
 *
 * This module implements the ServedByProcess interface of the
 * HqnSystem IDL interface
 */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "HqnSystem.hh" // IDL-generated from HqnSystem.idl

#include "served_i.h"   // IDL ServedByProcess, IDL base class of ServerProcess

/* ----------------------- Classes ----------------------------------------- */

// implementation class of IDL ServedByProcess interface
// it is expected that an object of this class is manipulated over a CORBA interface

class ServerModuleImpl : public virtual POA_HqnSystem::ServerModule, public ServedByProcessImpl 
{
 private:
  // this is not to be freed
  const char * pServerModuleName;

  // "instance" number meaning "this server does not support multiple instances"
  static const int NO_MULTIPLE_INSTANCE_SUPPORT;

 protected:
  ServerModuleImpl();
  ~ServerModuleImpl();

 public:
  CORBA::Long server_module_impl_version( );

  // setter for server module's leaf name TODO maybe no longer nexessary as we can get it from last id element of getName
  void setName( const char * pName );

  // IDL getter for module name
  char * name_2();

  /*
   * Registers this servant with the name service, and calls down to
   * concrete subclass's implementations of getNameServiceModuleName,
   * getNameServiceInterfaceName, and (if fIsInstanced is true)
   * getInstanceNumber to construct the latter parts of the
   * ServerModule's name within the name service.  Returns true on
   * successful registration, and false otherwise.  However, note that
   * it will only return false if the derived class implements null
   * return values for the virtuals.  This is very unlikely -- much
   * more likely that a failure will be announced by an exception
   * being thrown from within namingUtils_put.  So make sure you
   * handle those.
   */ 
  bool registerWithNameService( bool fIsInstanced );

  /* 
   * Concrete subclasses implement this so they can be registered with
   * the name service.  Should return a heap-allocated pointer to a
   * NameComponent, describing the module ID for this ServerModule in
   * the name service.
   */
  virtual const char * getNameServiceModuleName() const = 0;

  /* 
   * Concrete subclasses implement this so they can be registered with
   * the name service.  Should return a heap-allocated pointer to a
   * NameComponent, describing the interface ID for this ServerModule in
   * the name service.
   */
  virtual const char * getNameServiceInterfaceName() const = 0;

  /*
   * Concrete subclasses implement this so they can be registered with
   * the name service.  Should return NO_INSTANCE_SUPPORT if this is
   * not a server which supports multiple instances.  That is what the
   * default implementation does.
   */
  virtual const int getInstanceNumber() const;

  
}; // class ServerModuleImpl

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif // __SRVMOD_I_H__

// eof srvmod_i.h
