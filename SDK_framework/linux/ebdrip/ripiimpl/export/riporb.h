#ifndef __RIPORB_H__
#define __RIPORB_H__

/* $HopeName: SWripiimpl!export:riporb.h(EBDSDK_P.1) $
 * This module defines the RIPAPI ORB initialisation
 */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "ripapi.h"

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus

#include "orb_impl.h"
#include "server_i.h"
#include "hostserv.hh"
#include "ripctr_i.h"     // RIPControlImpl

extern "C"
{
#endif

/* Initializes RIPAPI (two queues and worker thread) and ORB
 * creates RIPInterface RIPAPI implementing objects,
 * starts ORB server loop.
 *
 * If the RIP was booted in persistent mode (with -instance on command
 * line), register it with the local host's name service.
 *
 * pRegisterServerIOR is the stringified object reference of an
 * HqnSystem::RegisterServer with which the RIPControl object will
 * register itself, or null. See HqnSystem.idl.  The idea here is that
 * if the RIP wasn't booted in persistent mode, but has an IOR, then
 * the RIP can be registered with a client.  If there is no IOR, but
 * the RIP is persistent, it will be registered in the name service
 * and so clients can find it that way.  
 *
 * It will exit the RIP with coreskinexiterrorf if there are
 * initialization failures, so if the method returns to the caller, it
 * can be assumed that the initializations worked.
 */
extern void initialize_ripapi_orb(char* pRegisterServerIOR,
                                  int32  ripcontrol_baseport,
                                  int32  ripcontrol_instance,
                                  int32  ripcontrol_force_instance,
                                  char * pszHostname);

/* Initializes ORB and starts ORB server loop
 *
 * For use in situations where the ORB must be started before
 * initialize_ripapi_orb() can be called (e.g. when using the
 * CORBA-enabled variant of the license server)
 *
 * Returns TRUE <=> success
 */
extern int32 initialize_riporb(int32 ripcontrol_baseport,
                               int32 ripcontrol_instance,
                               char* pszHostname);

extern void shut_down_ripapi_orb(void);

extern void connect_rip_to_soar_servers(void);

extern int32 register_local_host(const char *name);
extern int32 register_local_host_policy(const char *policy);

/* Adds a single argument to the argc/argv that are passed to OmniORB on
 * initialisation. Only has an effect if called before ORB is initialised.
 * Caller owns the memory for the argument.
 * If n is non-zero, it is the length of the argument. If zero, the argument
 * is assumed to be zero-terminated.
 */
extern int32 register_orb_arg(char* arg, size_t n);

extern void riporbNotifyRIPIsReady( int32 fReady );

#ifdef __cplusplus
} /* extern "C" */

extern FwTextString get_instance_name();
extern int32 get_instance_number();

extern CORBA::Object_ptr get_rip_control( void );
extern RIPControlImpl * get_rip_control_impl( void );

extern HostInterface::Host_ptr get_local_host_server( void );

#endif

#endif

/* $HopeName: SWripiimpl!export:riporb.h(EBDSDK_P.1) $
 *
* Log stripped */

#endif

/* eof riporb.h */
