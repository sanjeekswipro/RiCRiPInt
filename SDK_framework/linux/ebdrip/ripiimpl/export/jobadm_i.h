/* $HopeName: SWripiimpl!export:jobadm_i.h(EBDSDK_P.1) $
 *
* Log stripped */

/*
 * This module implements the JobStatusAdmin interface of the RIP IDL interface
 */

#ifndef _incl_jobadm_i
#define _incl_jobadm_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code

#include "ripapi.h"     // C/C++ RIP API general header

#include "jobcnsmr.h"   // JobStatusConsumer (abstract class)

#include "ripiface.hh"  // IDL-generated from ripiface.idl
#include "HqnSystem.hh" // HqnSystem generated from IDL 

#include "ripadm_i.h"   // IDL RIPAdmin, IDL base class of JobStatusAdmin

/* ----------------------- Classes ----------------------------------------- */

// forward declaration
class RIPControlImpl;



// definition of JobNoSuchJobException
// thrown if job unknown

class RIPAPI_JobNoSuchJobException
{
}; // class JobNoSuchJobException


// implementation class of IDL JobStatusAdmin interface
// it is expected that an object of this class is manipulated over a CORBA interface

class JobStatusAdminImpl :
  public virtual POA_RIPInterface::JobStatusAdmin, public RIPAdminImpl
{
 private:

  // does the internal work of registering transient and persistent consumers
  // the mask bits explain which JobStatusTypes to report to the consumer
  // if persistent == TRUE, the consumer will be treated as persistent, if FALSE
  // it will be treated as transient
  RIPInterface::ConsumerID register_consumer
    (
      RIPInterface::JobStatusConsumer_ptr pJobStatus, 
      RIPAPI_JobStatusTypesMask mask, 
      int32 persistent
    );

  friend class RIPControlImpl;

 protected:
  JobStatusAdminImpl();
  ~JobStatusAdminImpl();

 public:
  long job_status_admin_impl_version();

  /** 
   * Registers transient consumer interested in job status changes.
   * job_status contains functions to be called when changes occur
   * ConsumerID returned must be used to (later)
   * unregister_consumer_1.  Throws CORBA::INV_OBJREF if invalid
   * ServerInfo data.  Throws CORBA::NO_MEMORY if memory allocation
   * fails.  This function not passed on to RIP.
   *
   * Note: At version 2 of JobStatusAdmin, a new version of this
   * operation was introduced, to allow the client to ask for
   * particular status messages.  (The motivation was that we had
   * added the job_ripped_page_buffer_4 operation to
   * JobStatusConsumer, and not all consumers would want to receive
   * that status information.)  To maintain backwards compatibility,
   * register_consumer_1 has been defined to make the consumer receive
   * the same messages it did before, namely all the operations from
   * JobStatusConsumer before it reached version 4.  
   */
  RIPInterface::ConsumerID register_consumer_1
            (RIPInterface::JobStatusConsumer_ptr job_status);


  /** 
   * Registers persistent consumer interested in job status changes.
   * job_status contains functions to be called when changes occur
   * ConsumerID returned must be used to (later)
   * unregister_consumer_1.  Throws CORBA::INV_OBJREF if invalid
   * ServerInfo data.  Throws CORBA::NO_MEMORY if memory allocation
   * fails.  This function not passed on to RIP.  
   *
   * Note: At version 2 of JobStatusAdmin, a new version of this
   * operation was introduced, to allow the client to ask for
   * particular status messages.  (The motivation was that we had
   * added the job_ripped_page_buffer_4 operation to
   * JobStatusConsumer, and not all consumers would want to receive
   * that status information.)  To maintain backwards compatibility,
   * register_persistent_consumer_1 has been defined to make the
   * consumer receive the same messages it did before, namely all the
   * operations from JobStatusConsumer before it reached version 4.  
   */
  RIPInterface::ConsumerID register_persistent_consumer_1
            (RIPInterface::JobStatusConsumer_ptr job_status);

  /** 
   * Registers transient consumer interested in a particular set of
   * job status changes.  job_status contains functions to be called
   * when changes occur ConsumerID returned must be used to (later)
   * unregister_consumer_1.  Throws CORBA::INV_OBJREF if invalid
   * ServerInfo data.  Throws CORBA::NO_MEMORY if memory allocation
   * fails.  This function not passed on to RIP.  
   */
  RIPInterface::ConsumerID register_consumer_2
            (RIPInterface::JobStatusConsumer_ptr job_status,
             const RIPInterface::JobStatusTypes & job_status_types);


  /** 
   * Registers persistent consumer interested in a particular set of
   * job status changes.  job_status contains functions to be called
   * when changes occur ConsumerID returned must be used to (later)
   * unregister_consumer_1.  Throws CORBA::INV_OBJREF if invalid
   * ServerInfo data.  Throws CORBA::NO_MEMORY if memory allocation
   * fails.  This function not passed on to RIP.  
   */
  RIPInterface::ConsumerID register_persistent_consumer_2
            (RIPInterface::JobStatusConsumer_ptr job_status,
             const RIPInterface::JobStatusTypes & job_status_types);

  
  // throws RIPInterface::NotConnected_1
  void update_consumer_3(RIPInterface::ConsumerID id,
                         const RIPInterface::JobStatusTypes& types);


  /** 
   * Kills specified job.  Throws RIPInterface::NoSuchJob_1 if no such
   * job.  No security, so anyone can (in principle) kill any job.
   * This function passed on to RIP.
   */
  void kill_job_1(RIPInterface::JobID job);
  
        
}; // class JobStatusAdminImpl

#endif /* PRODUCT_HAS_API */

#endif // _incl_jobadm_i
