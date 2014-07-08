#ifndef __ORB_IMPL_H__
#define __ORB_IMPL_H__

/* $HopeName: HQN_CPP_CORBA!export:orb_impl.h(EBDSDK_P.1) $
 *
 * This module defines an ORB implementation (dependent on ORB vendor)
 *
 * This file should be included by all CORBA implementation modules
 */

#ifdef PRODUCT_HAS_API

/************************/
/* Stuff visible in 'C' */
/************************/

#include "fwstring.h"

/* Definitions of command-line arguments that should be used to define the
 * ORB localhost policy. These must be kept in sync with those used in Java
 * ServerExecPart, to ensure correct ORB operation.
 */
#define ORBIMPL_ARG_LOCALHOST         "IIOPLocalhost"
#define ORBIMPL_LOCALHOST_LOCALHOST   "localhost"
#define ORBIMPL_ARG_POLICY            "IIOPLocalHostPolicy"
#define ORBIMPL_POLICY_IP             "ip"
#define ORBIMPL_POLICY_IP_OR_LOOPBACK "iplb"
#define ORBIMPL_POLICY_FQDN           "fqdn"
#define ORBIMPL_POLICY_HOSTNAME       "hostname"

/* Maximum number of command-line parameters that can be passed to the ORB.
 * Note that these usually come in pairs, each of which sets one ORB setting, so
 * the number of ORB settings available is typically half this value.
 */
#define ORBIMPL_MAX_CMDLINE 16

/* Policy to use when choosing a host identifier to use in object
 * references to refer to the local host.
 */
typedef enum {
  HOST_POLICY_IP, HOST_POLICY_IP_OR_LOOPBACK, HOST_POLICY_FQDN, HOST_POLICY_HOSTNAME, HOST_POLICY_UNKNOWN
} IIOPLocalHostPolicy;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Return the enum for the policy (HOST_POLICY_UNKNOWN if unrecognised) */
extern IIOPLocalHostPolicy parseLocalHostPolicy(const char* policy);

/* Read the policy from a well-known config file
 * Return TRUE <=> successfully read file (no guarantee that the policy value is a valid option)
 */
extern int32 getLocalHostPolicyFromFile( FwStrRecord * pPolicy );

/* Gets the IIOP name for the given local host policy
 * Returns TRUE <=> success
 */
extern int32 getIIOPname( IIOPLocalHostPolicy iiop_name_policy, char * buffer, int32 len );

#ifdef __cplusplus
}

/* ----------------------- Includes ---------------------------------------- */

#include "orb.hh"   // ORB vendor specific definitions

/* ----------------------- Classes ----------------------------------------- */

// definition of ORBImpl, which implements ORB services
// all functions are static, which means they are class functions

class ORBImpl
{
private:
  ORBImpl()  {}  // class is not instantiable
  ~ORBImpl() {} // class is not instantiable

  /*
   * Set to TRUE only when the ORB and associated POAs have been started.
   */
  static int32 orb_initialised;

  /*
   * Set to FALSE if the no-args initialise_orb method is called,
   * otherwise set to TRUE.  This is used internally to work out
   * whether to create a persistent POA and whether to specify host/ip
   * identity for object references.
   */
  static int32 orb_supports_persistent;

  /* Number of client-defined command-line parameters that will be passed to ORB. */
  static int32 orb_argc;

  /* Client-defined command-line parameters to be passed to ORB. The array is
   * static, but storage for the text will be allocated dynamically, and released on
   * ORB shut-down. Only orb_argc entries are valid; remainder have
   * undefined values.
   */
  static char* orb_argv[ORBIMPL_MAX_CMDLINE];

#if ( OMNIORB_VERSION >= 0x410 )
  /* Redirects OmniORB trace to our LogUtils class. Idempotent. */
  static void initialise_tracing();
#endif

  /* OmniORB logFunction to send log to LogUtils. */
  static void log_function(const char* pszMessage);

public:

  /* Adds the given argument (which must not be null) to the ORB arguments
   * passed during initialise_orb. Only affects ORB if called before
   * initialise_orb.
   * If n is non-zero, it is used as the length of the argument. If it
   * is zero, zero-termination of the argument is assumed.
   * Caller owns memory for arg - a copy is made.
   * Returns true if successfully added or false if not (e.g. limit reached).
   */
  static bool add_arg(char* arg, size_t n = 0);

  /* initialises (creates and activates) the ORB
     NameservicePort, pNameserviceHostName - the port and host of the
     nameservice we're using
     iiop_name - Host identifier to use in object references to
       refer to the local host.
     iiop_port - no. of well-known port to listen on (if > 0)
     giopMaxMsgSize - as OmniORB 4 init param of same name (0 means default)
     throws CORBA::INITIALIZE if initialisation fails */
  static void initialise_orb(int NameservicePort, char *pNameserviceHostname,
                             char *oa_iiop_name, int oa_iiop_port = -1,
                             size_t giopMaxMsgSize = 0);

  /* initialises (creates and activates) the ORB
     NameservicePort, pNameserviceHostName - the port and host of the
     nameservice we're using
     iiop_port - no. of well-known port to listen on (if > 0)
     iiop_name_policy - policy to use when choosing a host identifier to
     use in object references to refer to the local host.
     giopMaxMsgSize - as OmniORB 4 init param of same name (0 means default)
     throws CORBA::INITIALIZE if initialisation fails */
  static void initialise_orb(
    int NameservicePort, char *pNameserviceHostname,
    int oa_iiop_port = -1,
    IIOPLocalHostPolicy iiop_name_policy = HOST_POLICY_IP,
    size_t giopMaxMsgSize = 0);


  /*
   * initialises (creates and activates) the ORB for use in client-style
   * applications.
   * The ORB is not informed of the location of the name service, nor is
   * it given host identity information to allow poa objects to be
   * persistently referencable.  NOTE: If you boot your ORB this way,
   * you do not have access to the persistentPOA().
   */
  static void initialise_orb();


  /* creates an unactivated ORB
     NameservicePort, pNameserviceHostName - the port and host of the
     nameservice we're using.  If pNameserviceHostName is NULL the ORB
     is not informed of the location of the name service, which is OK
     when used with an INS POA.
     iiop_name - Host identifier to use in object references to
       refer to the local host.
     iiop_port - no. of well-known port to listen on (if > 0)
     giopMaxMsgSize - as OmniORB 4 init param of same name (0 means default)
     throws CORBA::INITIALIZE if initialisation fails */
  static void create_orb(int NameservicePort, char *pNameserviceHostname,
                         char *oa_iiop_name, int oa_iiop_port = -1,
                         size_t giopMaxMsgSize = 0);

  /* creates an unactivated ORB
     NameservicePort, pNameserviceHostName - the port and host of the
     nameservice we're using.  If pNameserviceHostName is NULL the ORB
     is not informed of the location of the name service, which is OK
     when used with just an INS POA.
     iiop_port - no. of well-known port to listen on (if > 0)
     iiop_name_policy - policy to use when choosing a host identifier to
     use in object references to refer to the local host.
     giopMaxMsgSize - as OmniORB 4 init param of same name (0 means default)
     throws CORBA::INITIALIZE if initialisation fails */
  static void create_orb(
    int NameservicePort, char *pNameserviceHostname,
    int oa_iiop_port = -1,
    IIOPLocalHostPolicy iiop_name_policy = HOST_POLICY_IP,
    size_t giopMaxMsgSize = 0);

  /* active an ORB created via creat_orb
     persistent_poa - non-zero -> a persistent POA is required
     ins_poa - non-zero -> an INS POA is required */
  static void activate_orb(int persistent_poa, int ins_poa);

  // shuts ORB resources down
  static void shutdown_orb();


  /* returns TRUE if ORB is currently initialised, FALSE otherwise */
  static int32 orb_is_initialised();

  /*
   * Returns a new pointer to the root POA.  Clients should use a _var
   * to manage it.
   *
   * The root POA can be used for TRANSIENT objects which can cease to
   * exist when the RIP exits.  The ServerProcess object is an example.
   */
  static PortableServer::POA_ptr rootPOA();

  /*
   * Returns a new pointer to the persistent POA.  Clients should use
   * a _var to manage it.
   *
   * The persistent POA is for non-TRANSIENT objects whose object
   * references should remain valid in future server sessions.  Hence
   * if your ORB was booted with the no-args initialise_orb method (as
   * in lingen) you SHOULD NOT call this method.  If you do you will
   * get a nil pointer back.
   *
   * Typically, these objects will server "handles" such as the RIP's
   * RIPControl.
   */
  static PortableServer::POA_ptr persistentPOA();

  /*
   * Returns a new pointer to the INS POA.  Clients should use
   * a _var to manage it.
   */
  static PortableServer::POA_ptr INSPOA();

  // returns ORB
  static CORBA::ORB_ptr orb();

  // generate an omniORB object key, with the given data
  static void set_persistent_object_key(PersistentObjectKey& myKey, int32 med);

}; // class ORBImpl

#endif /* __cplusplus     */
#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* __ORB_IMPL_H__ */
