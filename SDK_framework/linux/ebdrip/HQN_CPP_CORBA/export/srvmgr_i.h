#ifndef __SRVMGR_I_H__
#define __SRVMGR_I_H__

/* $HopeName: HQN_CPP_CORBA!export:srvmgr_i.h(EBDSDK_P.1) $
 */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "HqnSystem.hh" // IDL-generated from HqnSystem.idl
#include "hqnlist.h"      // List


/* ----------------------- Classes ----------------------------------------- */

// definition of abstract class ServerProcessListener
// listeners should be derived classes of this

class ServerProcessListener
{
public:
  ServerProcessListener() {}
  virtual ~ServerProcessListener() {}

  virtual void release(const HqnSystem::ServerInfo_1 & deadServerInfo,
                       HqnSystem::ServedByProcess_ptr pServedByProcess) = 0;
    // called when the Server Process Manager wants the listener to die
};


// definition of ServerProcessManager

class ServerProcessManager
{

 private:

  // don't want copy construction or assignment: so declare them private
  ServerProcessManager(const ServerProcessManager &);
  ServerProcessManager & operator=(const ServerProcessManager &);

  // does the internal work of registration for the transient and persistent methods below
  static void register_listener(ServerProcessListener * listener,
                                const HqnSystem::ServerInfo_1_var & vServerInfo,
                                HqnSystem::ServedByProcess_ptr pServedByProcess);
  
  // listener notifications on server process replacement implemented via C thread;
  // this function is handed to the thread pool initialization
  static void c_notify_listeners(void * detailsArg);

  // using lists is not dumb as long as there are not many observations
  static List server_list; // list of ServerDetails, each entry has a list of observations
  static omni_mutex global_mutex; // mutex to control access to the above

public:
  ServerProcessManager();
  ~ServerProcessManager();

  // this registers the given listener for the given vServerInfo,
  // returns observation that can be used to unregister the listener
  //
  // throws CORBA::NO_MEMORY if memory allocation fails
  static void register_transient_listener(ServerProcessListener *listener,
                                          const HqnSystem::ServerInfo_1_var & vServerInfo);

  // this registers the given listener for the given vServerInfo and pServedByProcess
  // the pServedByProcess should be a persistent ServedByProcess
  // returns observation that can be used to unregister the listener
  //
  // throws CORBA::NO_MEMORY if memory allocation fails
  static void register_persistent_listener(ServerProcessListener * listener,
                                           const HqnSystem::ServerInfo_1_var & vServerInfo,
                                           HqnSystem::ServedByProcess_ptr pServedByProcess);

  static void unregister_listener(ServerProcessListener * listener);
    // this unregisters the given listener (and destroys the wrapping observation)
    // this does not throw an exception if listener not registered

  static omni_mutex& get_global_mutex();

  static void delete_from_list(CompareInterface& compare_details);

}; // class ServerProcessManager


#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif // __SRVMGR_I_H__

// eof srvmgr_i.h
