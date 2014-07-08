#ifndef __RIPIIMLP_H__
#define __RIPIIMLP_H__

/* $HopeName: SWripiimpl!export:ripiimpl.h(EBDSDK_P.1) $ */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "coreskin.h"    /* includes std.h ... */

#ifdef __cplusplus
extern "C" {
#endif


/* ----------------------- Types ------------------------------------------- */


/* ----------------------- Functions --------------------------------------- */

/* Boot up the interface compound
 *
 * pRegisterServerIOR is the stringified object reference of an
 * HqnSystem::RegisterServer with which the RIPControl object will
 * register itself, or null. See HqnSystem.idl.
 *
 * coreskinexiterrorf if fails
 */
extern void ripiimplInitialize(char * pRegisterServerIOR,
                               int32  ripcontrol_baseport,
                               int32  ripcontrol_instance,
                               int32  ripcontrol_force_instance,
                               char * pszHostname);

/* shutdown the RIP interface and clean up resources */
extern void ripiimplShutdown(void);

extern void connectRIPtoSOARServers(void);


/* 
 * Return non-zero if RIP successfully booted itself into the SOAR
 * world.
 *
 * A RIP will attempt to boot itself into the SOAR world (which
 * entails booting the CORBA ORB) if it was invoked either
 * persistently -- that is, f_rip_is_persistent_server would return
 * non-zero -- or with an explicit IOR upon which to call
 * register_server_1.  In addition, if the RIP is booted persistently,
 * it will register itself with a CORBA naming service running on the
 * local host.
 *
 * NOTE: A non-zero value DOES NOT imply that the RIP has registered with
 * any persistent SOAR servers such as the job logger, config server,
 * or host server.
 */
extern int32 fRIPBootedIntoSOARWorld(void);

/*
 * Sets the value returned by fRIPBootedIntoSOARWorld.
 */
extern void fSetRIPBootedIntoSOARWorld( int32 );




/*
 * Is RIP running in "persistent" mode?  That is, was it invoked with the
 * '-instance' command-line argument?  Return non-zero if so.
 *
 * A non-zero value implies that, as long as the RIP interface has
 * booted correctly (that is, fRIPBootedIntoSOARWorld also
 * returns non-zero), the RIP is permitted to communicate with
 * persistent servers such as the SOAR job logger, host server, and
 * config server.  
 *
 * NOTE: A true value DOES NOT imply that the RIP has successfully
 * booted into the SOAR world.  
 */
extern int32 fRIPIsPersistentServer( void );


/*
 * Sets the value returned by fRIPIsPersistentServer.
 */
extern void fSetRIPIsPersistentServer( int32 );




/* Initialises the Ptask that takes messages from the incoming queue
 * for the API and transfers them to the RIPs queue. This should not
 * be called until the RIP has booted sufficiently to cope with this.
 */
extern void createAPIWorkerPtask(void);

/* Returns flag indicating whether the Ptask for RIP API has been started. It
 * is set within the ripiimpl module, so there is no public setter.
 */
extern int32 fStartedAPIWorkerPtask(void);

/* Returns flag indicating if the Ptask for RIP API is blocked.
 */
extern int32 fIsAPIWorkerPtaskBlocked(void);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_HAS_API */

/* $HopeName: SWripiimpl!export:ripiimpl.h(EBDSDK_P.1) $
 *
 * Print File Queue Handling
 *
* Log stripped */

#endif /* ! __RIPIIMLP_H_ */

/* eof ripiimpl.h */
