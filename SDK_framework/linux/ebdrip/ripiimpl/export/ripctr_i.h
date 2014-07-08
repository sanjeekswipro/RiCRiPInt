/* $HopeName: SWripiimpl!export:ripctr_i.h(EBDSDK_P.1) $
 */

/*
 * This module implements the RIPControl interface of the RIP IDL interface
 */

#ifndef _incl_ripctr_i
#define _incl_ripctr_i

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes -------------------------------------- */

/* HQN_CPP_CORBA */
#include "orb_impl.h"   // ORB-dependent code
#include "srvmod_i.h"   // IDL ServerProcess, IDL base class of RIPControl
#include "comfin_i.h"   // implementation of ComponentFinder

/* ripiface */

#include "ripapi.h"     // C/C++ RIP API general header
#include "hostserv.hh"  // IDL-generated from hostserv.idl
#include "ripiface.hh"  // IDL-generated from ripiface.idl
#include "monadm_i.h"
#include "prgadm_i.h"
#include "jobadm_i.h"
#include "ripstatusadm_i.h"
#include "tpadm_i.h"

#ifdef PRODUCT_HAS_CDF_API
#include "resmgr_i.h"
#endif

/* ----------------------- Classes ----------------------------------------- */

// ImplementationLimitExceptions converts to a CORBA:IMP_LIMIT exception
class RIPAPI_ImplementationLimitException
{
};

// definition of QueueFileNotFoundException
// thrown when trying to queue local file which RIP cannot find

class RIPAPI_QueueFileNotFoundException
{
}; // class QueueFileNotFoundException

// definition of QueueInvalidSetupNameException
// thrown when trying to queue file, calibration strip or
// exposure sweep whose page setup RIP cannot find

class RIPAPI_QueueInvalidSetupNameException
{
}; // class QueueInvalidSetupNameException

// definition of QueueInvalidParametersException
// thrown when trying to queue exposure sweep with invalid parameters

class RIPAPI_QueueInvalidParametersException
{
}; // class QueueInvalidParametersException


// NoSuchJob
class RIPAPI_NoSuchJobException
{
};


// implementation class of IDL RIPControl interface.  It is expected
// that an object of this class is manipulated over a CORBA interface
class RIPControlImpl : public virtual POA_RIPInterface::RIPControl,
                       public ServerModuleImpl,
                       public ComponentFinderImpl
{

/* ----------------------- Public Interface ------------------------------ */

public:
  RIPControlImpl();
    // throws CORBA::NO_MEMORY if cannot allocate required memory

  // pMyPOA is the POA in which you will EXPLICITLY activate the
  // RIPControl object.  The purpose of this is to avoid accidental
  // implicit activations in the wrong POA.  Implicit activation is a
  // bad thing.  Instead, when time comes to activate, call
  // _default_POA() to get this POA back and then use it to
  // do the activation explicitly.  If a programmer forgets to do this and
  // implicitly activates a RIPControlImpl by doing _this(), at least
  // it will
  RIPControlImpl( const PortableServer::POA_ptr pMyPoa );

  ~RIPControlImpl();

  void common_constructor();


  // virtual from ServerModuleImpl
  const char * getNameServiceModuleName() const;
  // virtual from ServerModuleImpl
  const char * getNameServiceInterfaceName() const;

  PortableServer::POA_ptr pDefaultPOA;
  virtual PortableServer::POA_ptr _default_POA();

  HqnSystem::ComponentVersionInfo_1 * component_version_info_1();

  long rip_control_impl_version();
  long profile_manager_impl_version();
  long component_version_impl_version();

  void quit_1(  );

  CORBA::Boolean rip_is_ready_21(  );

  void set_rip_is_ready( CORBA::Boolean fReady );

  void kill_job_1(  );

  void kill_queued_job_16(RIPInterface::JobID job);

  RIPInterface::JobID uni_queue_local_file_1
    (
      const HqnTypes::UnicodeString& local_file_name,
      const HqnTypes::UnicodeString& page_setup_name,
      const RIPInterface::PostScriptString& override_postscript
    );
    // there is a limit of MAX_OVERRIDE_POSTSCRIPT on the override_postscript
    // throws CORBA::NO_MEMORY if cannot allocate required memory
    // throws CORBA::UNKNOWN if cannot complete request for some unknown reason
    // throws CORBA::IMP_LIMIT if override_postscript too long
    // this function passed onto RIP

  RIPInterface::JobID uni_queue_hostserver_file_8
    (
     HostInterface::File_ptr remote_file,
     const HqnTypes::UnicodeString& page_setup_name,
     const RIPInterface::PostScriptString& override_postscript
     );
     // See uni_queue_hostserver_file_19

  RIPInterface::JobID uni_queue_hostserver_file_19
    (
     HostInterface::File_ptr remote_file,
     const HqnTypes::UnicodeString& page_setup_name,
     const RIPInterface::PostScriptString& override_postscript,
     RIPInterface::RIPControl::QueueFlags queue_flags
    );
    // there is a limit of MAX_OVERRIDE_POSTSCRIPT on the override_postscript
    // throws CORBA::NO_MEMORY if cannot allocate required memory
    // throws CORBA::UNKNOWN if cannot complete request for some unknown reason
    // throws CORBA::IMP_LIMIT if override_postscript too long
    // this function passed onto RIP

  RIPInterface::JobID uni_queue_calibration_strip_2
    (
      RIPInterface::CalibrationType cal_type,
      RIPInterface::CalibrationStripScope cal_scope,
      const HqnTypes::UnicodeString& page_setup_name,
      const RIPInterface::PostScriptString& override_postscript
    );
    // there is a limit of MAX_OVERRIDE_POSTSCRIPT on the override_postscript
    // throws CORBA::NO_MEMORY if cannot allocate required memory
    // throws CORBA::UNKNOWN if cannot complete request for some unknown reason
    // throws CORBA::IMP_LIMIT if override_postscript too long
    // this function passed onto RIP.
    // Note that version 1 of this function has been removed.

  RIPInterface::JobID uni_queue_exposure_sweep_2
    (
      RIPInterface::CalibrationStripScope cal_scope,
      const HqnTypes::UnicodeString& page_setup_name,
      const RIPInterface::PostScriptString& override_postscript,
      CORBA::Long start_exp,
      CORBA::Long step_exp,
      CORBA::Long end_exp
    );
    // there is a limit of MAX_OVERRIDE_POSTSCRIPT on the override_postscript
    // throws CORBA::NO_MEMORY if cannot allocate required memory
    // throws CORBA::UNKNOWN if cannot complete request for some unknown reason
    // throws CORBA::IMP_LIMIT if override_postscript too long
    // this function passed onto RIP
    // Note that version 1 of this function has been removed.

  HqnTypes::PostScriptString *uni_filename_to_postscript_20
    (
       const HqnTypes::UnicodeString &uniFilename
    );

  CORBA::Boolean soar_action_queue_blocked_20(  );

  CORBA::Boolean tp_get_output_enabled_20(  );

  void tp_set_output_enabled_20( CORBA::Boolean fEnabled );

  RIPInterface::TPMode tp_get_throughput_mode_20(  );

  void tp_set_throughput_mode_20
    ( RIPInterface::TPMode mode );

  HqnTypes::UnicodeStrings* tp_deliver_local_pagebuffers_20
    (
       const HostInterface::Files& pagebuffers,
       CORBA::Boolean report_delivered_pagebuffers,
       HostInterface::Files_out delivered_pagebuffers
    );

  void tp_append_pagebuffers_to_queue_20
    (
       const HqnTypes::UnicodeStrings& pagebuffers,
       RIPInterface::TPQueue queueSelector
    );

  void tp_remove_pagebuffers_from_queue_20
    (
       const HqnTypes::UnicodeStrings& pagebuffers
    );

  CORBA::Long tp_get_queue_size_20
    (
       RIPInterface::TPQueue queueSelector
    );

  void tp_set_num_copies_21
    (
       const HqnTypes::UnicodeStrings& pagebuffers,
       CORBA::Long numCopies,
       CORBA::Boolean collated
    );

  void tp_kill_current_job_21();

  void uni_install_emulation_profile_1
    (
      const HqnTypes::UnicodeString& profile_path
    );

  CORBA::ULong count_queued_jobs_23();

  RIPInterface::JobIDs* list_queued_jobs_23();

  RIPInterface::JobID get_next_job_id_24();

  void kill_all_queued_jobs_23();

  RIPInterface::MonitorAdmin_ptr get_monitor_admin_1(  );
    // this function not passed onto RIP

  RIPInterface::ProgressAdmin_ptr get_progress_admin_1(  );
    // this function not passed onto RIP

  RIPInterface::JobStatusAdmin_ptr get_job_admin_1(  );
    // this function not passed onto RIP

  RIPInterface::RIPStatusAdmin_ptr get_rip_admin_14(  );
    // this function not passed onto RIP

  RIPInterface::ThroughputStatusAdmin_ptr get_throughput_admin_20(  );
    // this function not passed onto RIP

  RIPInterface::HarlequinResourceManager_ptr get_resource_manager_16( );

  void set_input_channels_running_4( CORBA::Boolean state );

  void get_input_channels_running_4
    (
     CORBA::Boolean  &configured_state,
     CORBA::Boolean  &current_state
    );

  HqnTypes::UnicodeStrings * uni_get_page_setup_names_6( );

  HqnTypes::UnicodeStrings * uni_get_device_page_setup_names_20
    (
      const HqnTypes::UnicodeStrings& device_names,
      CORBA::Boolean exclude_devices
    );

  HqnTypes::UnicodeString * uni_get_instance_name_7( );

  CORBA::Long get_instance_number_24();

  void identify_rip_controller_11
    (const RIPInterface::RIPControl::RipControllerIdentifier &identity);

  HqnTypes::UnicodeStrings * uni_list_installed_fonts_13();

  // This method is called by the RIP machinery to hand over the list of fonts that the previous
  // IDL-defined method is probably sitting there waiting for.
  static void notify_font_listing(FwStrRecord * pRec);

  RIPInterface::ICCProfileDescription_5* examine_ICC_profile_15
     ( HostInterface::File_ptr file );

  void object_is_ready();
    // states that this object is ready to act as CORBA server

#ifdef PRODUCT_HAS_CDF_API
  HarlequinResourceManagerImpl* getResourceManagerImpl();
#endif

  /*
   * Returns TRUE if sufficient HIPP capabilities are enabled in order to
   * permit installing an ICC profile with the given options.
   */
  int32 checkICCImportPermissions( RIPInterface::ICCImportOptions options );

  /* ------------------------- Private --------------------------------- */

private:
  MonitorAdminImpl * m_monitor_admin;
  ProgressAdminImpl * m_progress_admin;
  JobStatusAdminImpl * m_job_status_admin;
  RIPStatusAdminImpl * m_rip_status_admin;
  ThroughputStatusAdminImpl * m_tp_status_admin;

#ifdef PRODUCT_HAS_CDF_API
  HarlequinResourceManagerImpl * m_resource_manager;
#endif

  CORBA::Boolean m_rip_is_ready;

  static HqnSystem::ComponentVersionInfo_1 ripv;

  static CORBA::ULong MAX_OVERRIDE_POSTSCRIPT; // = 8k-1

}; // class RIPControlImpl

#endif /* PRODUCT_HAS_API */

#endif // _incl_ripctr_i

/* Log stripped */
